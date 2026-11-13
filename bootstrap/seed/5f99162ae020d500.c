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
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint32_t f0; uint8_t f1; uint8_t pe[3]; } gm_t9805qtonbsag;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint8_t f0; uint8_t p1[7]; void * f1; void * f2; } gm_ta612fil812pm;
typedef struct { void * f0; void * f1; void * f2; uint8_t f3; uint8_t pe[7]; } gm_tao63pk3r18v5;
typedef struct { void * f0; void * f1; void * f2; } gm_tbbqefuhv2r8h;
typedef struct { void * f0; uint32_t f1; uint8_t pe[4]; } gm_tbl8il92l97ut;
typedef struct { uint32_t f0; uint32_t f1; } gm_tcc1rq4n1avb2;
typedef struct { uint8_t f0; uint8_t p1[7]; void * f1; } gm_tgtub0v94d5u5;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { void * f0; void * f1; uint8_t f2; uint8_t pe[7]; } gm_tlcl4u558l6p5;
typedef struct { uint8_t f0; uint8_t p1[3]; union { uint32_t v0; } f1; } gm_tmn7n0n9raou5;
typedef struct { uint8_t f0; uint8_t f1; uint8_t f2; } gm_toh0ootodhjue;
typedef struct { void * f0; uint8_t f1; uint8_t pe[7]; } gm_tsplfr33dsvji;
typedef struct { uint32_t f0; uint8_t p1[4]; void * f1; } gm_ttggq7ng4kp3p;
typedef struct { void * f0; gm_t9805qtonbsag f1; uint32_t f2; uint8_t pe[4]; } gm_t083kd7b1kkag;
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_t08olq97rscn3;
typedef struct { gm_b16_8 f0; uint64_t f1; } gm_t3877pk7lv748;
typedef struct { void * f0; gm_t9805qtonbsag f1; void * f2; } gm_t3dcoc7077t1q;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; uint8_t f2; uint8_t pe[3]; } gm_t3rcjibl8kkg4;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; void * f2; void * f3; } gm_t710lsrq1m8gk;
typedef struct { uint8_t f0; gm_t9qkh0rsooc8d f1; uint8_t f2; uint8_t f3; uint8_t f4; uint8_t p5[3]; uint32_t f5; gm_t9805qtonbsag f6; gm_t9qkh0rsooc8d f7; uint8_t pe[3]; } gm_t7g88i52rrtm5;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; void * f1; } gm_t8ghgl2jj0qha;
typedef struct { gm_tmn7n0n9raou5 f0; void * f1; } gm_t8jsc4n9vp2ib;
_Static_assert(sizeof(gm_t9805qtonbsag) == 8 && _Alignof(gm_t9805qtonbsag) == 4 && offsetof(gm_t9805qtonbsag, f0) == 0 && offsetof(gm_t9805qtonbsag, f1) == 4, "layout of `?u32`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
_Static_assert(sizeof(gm_ta612fil812pm) == 24 && _Alignof(gm_ta612fil812pm) == 8 && offsetof(gm_ta612fil812pm, f0) == 0 && offsetof(gm_ta612fil812pm, f1) == 8 && offsetof(gm_ta612fil812pm, f2) == 16, "layout of `(is_and: bool, lhs: ^Expr, rhs: ^Expr)`");
_Static_assert(sizeof(gm_tao63pk3r18v5) == 32 && _Alignof(gm_tao63pk3r18v5) == 8 && offsetof(gm_tao63pk3r18v5, f0) == 0 && offsetof(gm_tao63pk3r18v5, f1) == 8 && offsetof(gm_tao63pk3r18v5, f2) == 16 && offsetof(gm_tao63pk3r18v5, f3) == 24, "layout of `SliceExpr`");
typedef struct { gm_b16_8 f0; uint32_t f1; uint32_t f2; gm_b16_8 f3; } gm_tb40qn7hk0tl9;
_Static_assert(sizeof(gm_tbbqefuhv2r8h) == 24 && _Alignof(gm_tbbqefuhv2r8h) == 8 && offsetof(gm_tbbqefuhv2r8h, f0) == 0 && offsetof(gm_tbbqefuhv2r8h, f1) == 8 && offsetof(gm_tbbqefuhv2r8h, f2) == 16, "layout of `Arm`");
_Static_assert(sizeof(gm_tbl8il92l97ut) == 16 && _Alignof(gm_tbl8il92l97ut) == 8 && offsetof(gm_tbl8il92l97ut, f0) == 0 && offsetof(gm_tbl8il92l97ut, f1) == 8, "layout of `SpecCall`");
_Static_assert(sizeof(gm_tcc1rq4n1avb2) == 8 && _Alignof(gm_tcc1rq4n1avb2) == 4 && offsetof(gm_tcc1rq4n1avb2, f0) == 0 && offsetof(gm_tcc1rq4n1avb2, f1) == 4, "layout of `LineCol`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_tcc1rq4n1avb2 v0; } f1; } gm_te9hrk0u35rev;
typedef struct { uint8_t f0; union { gm_toh0ootodhjue v0; } f1; } gm_tesaltf0mhcki;
_Static_assert(sizeof(gm_tgtub0v94d5u5) == 16 && _Alignof(gm_tgtub0v94d5u5) == 8 && offsetof(gm_tgtub0v94d5u5, f0) == 0 && offsetof(gm_tgtub0v94d5u5, f1) == 8, "layout of `(is_const: bool, inner: ^Expr)`");
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
typedef struct { void * f0; gm_b16_8 f1; } gm_tkaapcii86udk;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t f1; uint8_t pe[3]; } gm_tkn42u7na3t81;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_tlcl4u558l6p5) == 24 && _Alignof(gm_tlcl4u558l6p5) == 8 && offsetof(gm_tlcl4u558l6p5, f0) == 0 && offsetof(gm_tlcl4u558l6p5, f1) == 8 && offsetof(gm_tlcl4u558l6p5, f2) == 16, "layout of `(lo: ^Expr, hi: ^Expr, inclusive: bool)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { void * v0; gm_t3mf58gv9kf3n v1; } f1; } gm_tlr9dp305htm7;
_Static_assert(sizeof(gm_tmn7n0n9raou5) == 8 && _Alignof(gm_tmn7n0n9raou5) == 4 && offsetof(gm_tmn7n0n9raou5, f0) == 0 && offsetof(gm_tmn7n0n9raou5, f1) == 4, "layout of `Coercion`");
_Static_assert(sizeof(gm_toh0ootodhjue) == 3 && _Alignof(gm_toh0ootodhjue) == 1 && offsetof(gm_toh0ootodhjue, f0) == 0 && offsetof(gm_toh0ootodhjue, f1) == 1 && offsetof(gm_toh0ootodhjue, f2) == 2, "layout of `Restr`");
typedef struct { gm_b16_8 f0; void * f1; } gm_tpe1hv3fekn05;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; void * v1; } f1; } gm_tq7s98ihonkju;
typedef struct { gm_b16_8 f0; void * f1; void * f2; } gm_trl2khmuphncd;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_t9qkh0rsooc8d v3; } f1; } gm_ts6oqppjh0ba3;
_Static_assert(sizeof(gm_tsplfr33dsvji) == 16 && _Alignof(gm_tsplfr33dsvji) == 8 && offsetof(gm_tsplfr33dsvji, f0) == 0 && offsetof(gm_tsplfr33dsvji, f1) == 8, "layout of `(expr: ^Expr, is_result: bool)`");
_Static_assert(sizeof(gm_ttggq7ng4kp3p) == 16 && _Alignof(gm_ttggq7ng4kp3p) == 8 && offsetof(gm_ttggq7ng4kp3p, f0) == 0 && offsetof(gm_ttggq7ng4kp3p, f1) == 8, "layout of `(index: u32, payload: ?^Value)`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_tcc1rq4n1avb2 v3; uint32_t v4; uint32_t v5; } f1; } gm_tvdg94bk7aa22;
_Static_assert(sizeof(gm_t083kd7b1kkag) == 24 && _Alignof(gm_t083kd7b1kkag) == 8 && offsetof(gm_t083kd7b1kkag, f0) == 0 && offsetof(gm_t083kd7b1kkag, f1) == 8 && offsetof(gm_t083kd7b1kkag, f2) == 16, "layout of `TryExpr`");
_Static_assert(sizeof(gm_t08olq97rscn3) == 32 && _Alignof(gm_t08olq97rscn3) == 8 && offsetof(gm_t08olq97rscn3, f0) == 0 && offsetof(gm_t08olq97rscn3, f1) == 16, "layout of `File`");
_Static_assert(sizeof(gm_t3877pk7lv748) == 24 && _Alignof(gm_t3877pk7lv748) == 8 && offsetof(gm_t3877pk7lv748, f0) == 0 && offsetof(gm_t3877pk7lv748, f1) == 16, "layout of `WasmReader`");
_Static_assert(sizeof(gm_t3dcoc7077t1q) == 24 && _Alignof(gm_t3dcoc7077t1q) == 8 && offsetof(gm_t3dcoc7077t1q, f0) == 0 && offsetof(gm_t3dcoc7077t1q, f1) == 8 && offsetof(gm_t3dcoc7077t1q, f2) == 16, "layout of `ElseExpr`");
_Static_assert(sizeof(gm_t3rcjibl8kkg4) == 12 && _Alignof(gm_t3rcjibl8kkg4) == 4 && offsetof(gm_t3rcjibl8kkg4, f0) == 0 && offsetof(gm_t3rcjibl8kkg4, f1) == 4 && offsetof(gm_t3rcjibl8kkg4, f2) == 8, "layout of `(a: Ty, b: Ty, negated: bool)`");
typedef struct { gm_tiilmfd16v3fc f0; uint32_t f1; uint8_t pe[4]; } gm_t419mo45g7j0a;
typedef struct { uint8_t f0; uint8_t p1[7]; union { void * v0; gm_tpe1hv3fekn05 v1; } f1; } gm_t4lnf71mruqpt;
_Static_assert(sizeof(gm_t710lsrq1m8gk) == 40 && _Alignof(gm_t710lsrq1m8gk) == 8 && offsetof(gm_t710lsrq1m8gk, f0) == 0 && offsetof(gm_t710lsrq1m8gk, f1) == 8 && offsetof(gm_t710lsrq1m8gk, f2) == 24 && offsetof(gm_t710lsrq1m8gk, f3) == 32, "layout of `WithExpr`");
_Static_assert(sizeof(gm_t7g88i52rrtm5) == 24 && _Alignof(gm_t7g88i52rrtm5) == 4 && offsetof(gm_t7g88i52rrtm5, f0) == 0 && offsetof(gm_t7g88i52rrtm5, f1) == 1 && offsetof(gm_t7g88i52rrtm5, f2) == 2 && offsetof(gm_t7g88i52rrtm5, f3) == 3 && offsetof(gm_t7g88i52rrtm5, f4) == 4 && offsetof(gm_t7g88i52rrtm5, f5) == 8 && offsetof(gm_t7g88i52rrtm5, f6) == 12 && offsetof(gm_t7g88i52rrtm5, f7) == 20, "layout of `FmtSpec`");
_Static_assert(sizeof(gm_t8ghgl2jj0qha) == 16 && _Alignof(gm_t8ghgl2jj0qha) == 8 && offsetof(gm_t8ghgl2jj0qha, f0) == 0 && offsetof(gm_t8ghgl2jj0qha, f1) == 8, "layout of `(op: UnOp, operand: ^Expr)`");
_Static_assert(sizeof(gm_t8jsc4n9vp2ib) == 16 && _Alignof(gm_t8jsc4n9vp2ib) == 8 && offsetof(gm_t8jsc4n9vp2ib, f0) == 0 && offsetof(gm_t8jsc4n9vp2ib, f1) == 8, "layout of `(kind: Coercion, expr: ^Expr)`");
typedef struct { gm_tesaltf0mhcki f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tandvbt63uk7n;
_Static_assert(sizeof(gm_tb40qn7hk0tl9) == 40 && _Alignof(gm_tb40qn7hk0tl9) == 8 && offsetof(gm_tb40qn7hk0tl9, f0) == 0 && offsetof(gm_tb40qn7hk0tl9, f1) == 16 && offsetof(gm_tb40qn7hk0tl9, f2) == 20 && offsetof(gm_tb40qn7hk0tl9, f3) == 24, "layout of `SourceLocation`");
typedef struct { uint8_t f0; uint8_t p1[7]; gm_trqfvmjtt4sij f1; } gm_tbci5ospk5nnn;
_Static_assert(sizeof(gm_te9hrk0u35rev) == 12 && _Alignof(gm_te9hrk0u35rev) == 4 && offsetof(gm_te9hrk0u35rev, f0) == 0 && offsetof(gm_te9hrk0u35rev, f1) == 4, "layout of `Target`");
typedef struct { gm_te9hrk0u35rev f0; uint8_t p1[4]; void * f1; } gm_teo9d4mncjfdu;
_Static_assert(sizeof(gm_tesaltf0mhcki) == 4 && _Alignof(gm_tesaltf0mhcki) == 1 && offsetof(gm_tesaltf0mhcki, f0) == 0 && offsetof(gm_tesaltf0mhcki, f1) == 1, "layout of `SoaOp`");
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
_Static_assert(sizeof(gm_tkaapcii86udk) == 24 && _Alignof(gm_tkaapcii86udk) == 8 && offsetof(gm_tkaapcii86udk, f0) == 0 && offsetof(gm_tkaapcii86udk, f1) == 8, "layout of `Place`");
_Static_assert(sizeof(gm_tkn42u7na3t81) == 8 && _Alignof(gm_tkn42u7na3t81) == 4 && offsetof(gm_tkn42u7na3t81, f0) == 0 && offsetof(gm_tkn42u7na3t81, f1) == 4, "layout of `?Ty`");
_Static_assert(sizeof(gm_tlr9dp305htm7) == 24 && _Alignof(gm_tlr9dp305htm7) == 8 && offsetof(gm_tlr9dp305htm7, f0) == 0 && offsetof(gm_tlr9dp305htm7, f1) == 8, "layout of `CondPart`");
_Static_assert(sizeof(gm_tpe1hv3fekn05) == 24 && _Alignof(gm_tpe1hv3fekn05) == 8 && offsetof(gm_tpe1hv3fekn05, f0) == 0 && offsetof(gm_tpe1hv3fekn05, f1) == 16, "layout of `(attrs: []Attribute, inner: ^Expr)`");
_Static_assert(sizeof(gm_tq7s98ihonkju) == 16 && _Alignof(gm_tq7s98ihonkju) == 8 && offsetof(gm_tq7s98ihonkju, f0) == 0 && offsetof(gm_tq7s98ihonkju, f1) == 8, "layout of `AllocError!^HandlerDecl`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_ttggq7ng4kp3p v0; gm_tpe1hv3fekn05 v1; gm_t3mf58gv9kf3n v2; gm_tpe1hv3fekn05 v3; void * v4; gm_tgtub0v94d5u5 v5; } f1; } gm_trcmmd12cefb1;
_Static_assert(sizeof(gm_trl2khmuphncd) == 32 && _Alignof(gm_trl2khmuphncd) == 8 && offsetof(gm_trl2khmuphncd, f0) == 0 && offsetof(gm_trl2khmuphncd, f1) == 16 && offsetof(gm_trl2khmuphncd, f2) == 24, "layout of `IfExpr`");
typedef struct { gm_ts6oqppjh0ba3 f0; uint8_t p1[6]; void * f1; void * f2; } gm_trl9d7m1k5lbd;
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_ts6oqppjh0ba3) == 2 && _Alignof(gm_ts6oqppjh0ba3) == 1 && offsetof(gm_ts6oqppjh0ba3, f0) == 0 && offsetof(gm_ts6oqppjh0ba3, f1) == 1, "layout of `BinOp`");
typedef struct { gm_tvdg94bk7aa22 f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tt8o5kebou62r;
_Static_assert(sizeof(gm_tvdg94bk7aa22) == 12 && _Alignof(gm_tvdg94bk7aa22) == 4 && offsetof(gm_tvdg94bk7aa22, f0) == 0 && offsetof(gm_tvdg94bk7aa22, f1) == 4, "layout of `Builtin`");
_Static_assert(sizeof(gm_t419mo45g7j0a) == 32 && _Alignof(gm_t419mo45g7j0a) == 8 && offsetof(gm_t419mo45g7j0a, f0) == 0 && offsetof(gm_t419mo45g7j0a, f1) == 24, "layout of `(effect_: EffectRef, op: u32)`");
_Static_assert(sizeof(gm_t4lnf71mruqpt) == 32 && _Alignof(gm_t4lnf71mruqpt) == 8 && offsetof(gm_t4lnf71mruqpt, f0) == 0 && offsetof(gm_t4lnf71mruqpt, f1) == 8, "layout of `HandlerValue`");
typedef struct { gm_tbci5ospk5nnn f0; gm_tbci5ospk5nnn f1; uint8_t f2; uint8_t pe[7]; } gm_t59juu943t8sl;
_Static_assert(sizeof(gm_tandvbt63uk7n) == 24 && _Alignof(gm_tandvbt63uk7n) == 8 && offsetof(gm_tandvbt63uk7n, f0) == 0 && offsetof(gm_tandvbt63uk7n, f1) == 8, "layout of `(op: SoaOp, args: []Expr)`");
_Static_assert(sizeof(gm_tbci5ospk5nnn) == 48 && _Alignof(gm_tbci5ospk5nnn) == 8 && offsetof(gm_tbci5ospk5nnn, f0) == 0 && offsetof(gm_tbci5ospk5nnn, f1) == 8, "layout of `Int`");
_Static_assert(sizeof(gm_teo9d4mncjfdu) == 24 && _Alignof(gm_teo9d4mncjfdu) == 8 && offsetof(gm_teo9d4mncjfdu, f0) == 0 && offsetof(gm_teo9d4mncjfdu, f1) == 16, "layout of `BreakExpr`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t419mo45g7j0a v0; void * v1; } f1; } gm_th1g69d7vpipg;
typedef struct { gm_tbci5ospk5nnn f0; gm_tbci5ospk5nnn f1; } gm_tngtuiuskn938;
_Static_assert(sizeof(gm_trcmmd12cefb1) == 32 && _Alignof(gm_trcmmd12cefb1) == 8 && offsetof(gm_trcmmd12cefb1, f0) == 0 && offsetof(gm_trcmmd12cefb1, f1) == 8, "layout of `Stmt`");
_Static_assert(sizeof(gm_trl9d7m1k5lbd) == 24 && _Alignof(gm_trl9d7m1k5lbd) == 8 && offsetof(gm_trl9d7m1k5lbd, f0) == 0 && offsetof(gm_trl9d7m1k5lbd, f1) == 8 && offsetof(gm_trl9d7m1k5lbd, f2) == 16, "layout of `(op: BinOp, lhs: ^Expr, rhs: ^Expr)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint32_t v0; gm_t419mo45g7j0a v1; void * v2; } f1; } gm_tsth9qu1dv0g3;
_Static_assert(sizeof(gm_tt8o5kebou62r) == 32 && _Alignof(gm_tt8o5kebou62r) == 8 && offsetof(gm_tt8o5kebou62r, f0) == 0 && offsetof(gm_tt8o5kebou62r, f1) == 16, "layout of `(op: Builtin, args: []Expr)`");
_Static_assert(sizeof(gm_t59juu943t8sl) == 104 && _Alignof(gm_t59juu943t8sl) == 8 && offsetof(gm_t59juu943t8sl, f0) == 0 && offsetof(gm_t59juu943t8sl, f1) == 48 && offsetof(gm_t59juu943t8sl, f2) == 96, "layout of `(lo: Int, hi: Int, inclusive: bool)`");
typedef struct { gm_th1g69d7vpipg f0; void * f1; void * f2; uint32_t f3; uint8_t pe[4]; } gm_t9i38ih07bojq;
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint32_t v0; void * v1; gm_t59juu943t8sl v2; void * v3; void * v4; void * v5; gm_ttggq7ng4kp3p v6; gm_b16_8 v7; gm_b16_8 v8; } f1; } gm_tb38v5s9j1r32;
_Static_assert(sizeof(gm_th1g69d7vpipg) == 40 && _Alignof(gm_th1g69d7vpipg) == 8 && offsetof(gm_th1g69d7vpipg, f0) == 0 && offsetof(gm_th1g69d7vpipg, f1) == 8, "layout of `WriteSink`");
_Static_assert(sizeof(gm_tngtuiuskn938) == 96 && _Alignof(gm_tngtuiuskn938) == 8 && offsetof(gm_tngtuiuskn938, f0) == 0 && offsetof(gm_tngtuiuskn938, f1) == 48, "layout of `Rat`");
typedef struct { gm_th1g69d7vpipg f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; } gm_tntn2tf15puib;
typedef struct { gm_tsth9qu1dv0g3 f0; gm_b16_8 f1; } gm_to76jli3bqddq;
_Static_assert(sizeof(gm_tsth9qu1dv0g3) == 40 && _Alignof(gm_tsth9qu1dv0g3) == 8 && offsetof(gm_tsth9qu1dv0g3, f0) == 0 && offsetof(gm_tsth9qu1dv0g3, f1) == 8, "layout of `Callee`");
typedef struct { gm_tngtuiuskn938 f0; gm_tngtuiuskn938 f1; } gm_tu15p0hi2qlv6;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tbci5ospk5nnn v0; double v1; gm_tngtuiuskn938 v2; gm_tu15p0hi2qlv6 v3; uint8_t v4; gm_b16_8 v5; gm_b16_8 v6; uint32_t v7; gm_tkn42u7na3t81 v8; gm_tkn42u7na3t81 v9; gm_t88rcucg1b7uk v10; gm_t3rcjibl8kkg4 v11; gm_t08olq97rscn3 v12; uint32_t v13; gm_tcc1rq4n1avb2 v14; gm_t3877pk7lv748 v15; void * v16; gm_tbl8il92l97ut v17; gm_tlcl4u558l6p5 v18; gm_tao63pk3r18v5 v19; void * v20; void * v21; void * v22; void * v23; uint32_t v24; uint32_t v25; uint32_t v26; gm_t8ghgl2jj0qha v27; gm_trl9d7m1k5lbd v28; gm_ta612fil812pm v29; gm_t8jsc4n9vp2ib v30; gm_tt8o5kebou62r v31; gm_to76jli3bqddq v32; gm_b16_8 v33; gm_b16_8 v34; void * v35; gm_ttggq7ng4kp3p v36; void * v37; gm_tsplfr33dsvji v38; gm_t3dcoc7077t1q v39; gm_t083kd7b1kkag v40; gm_tlcl4u558l6p5 v41; gm_tkaapcii86udk v42; gm_tlcl4u558l6p5 v43; void * v44; gm_trl2khmuphncd v45; gm_t710lsrq1m8gk v46; gm_teo9d4mncjfdu v47; gm_te9hrk0u35rev v48; gm_ttggq7ng4kp3p v49; void * v50; gm_t3mf58gv9kf3n v51; void * v52; gm_tntn2tf15puib v53; gm_t9i38ih07bojq v54; gm_tandvbt63uk7n v55; gm_t710lsrq1m8gk v56; } f1; } gm_t5brlu7fal70j;
_Static_assert(sizeof(gm_t9i38ih07bojq) == 64 && _Alignof(gm_t9i38ih07bojq) == 8 && offsetof(gm_t9i38ih07bojq, f0) == 0 && offsetof(gm_t9i38ih07bojq, f1) == 40 && offsetof(gm_t9i38ih07bojq, f2) == 48 && offsetof(gm_t9i38ih07bojq, f3) == 56, "layout of `WriteDeferredExpr`");
_Static_assert(sizeof(gm_tb38v5s9j1r32) == 112 && _Alignof(gm_tb38v5s9j1r32) == 8 && offsetof(gm_tb38v5s9j1r32, f0) == 0 && offsetof(gm_tb38v5s9j1r32, f1) == 8, "layout of `PatKind`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_tb38v5s9j1r32 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_tc9qv69frb08e;
_Static_assert(sizeof(gm_tntn2tf15puib) == 88 && _Alignof(gm_tntn2tf15puib) == 8 && offsetof(gm_tntn2tf15puib, f0) == 0 && offsetof(gm_tntn2tf15puib, f1) == 40 && offsetof(gm_tntn2tf15puib, f2) == 56 && offsetof(gm_tntn2tf15puib, f3) == 72, "layout of `WriteExpr`");
_Static_assert(sizeof(gm_to76jli3bqddq) == 56 && _Alignof(gm_to76jli3bqddq) == 8 && offsetof(gm_to76jli3bqddq, f0) == 0 && offsetof(gm_to76jli3bqddq, f1) == 40, "layout of `(callee: Callee, args: []Expr)`");
_Static_assert(sizeof(gm_tu15p0hi2qlv6) == 192 && _Alignof(gm_tu15p0hi2qlv6) == 8 && offsetof(gm_tu15p0hi2qlv6, f0) == 0 && offsetof(gm_tu15p0hi2qlv6, f1) == 96, "layout of `(re: Rat, im: Rat)`");
_Static_assert(sizeof(gm_t5brlu7fal70j) == 200 && _Alignof(gm_t5brlu7fal70j) == 8 && offsetof(gm_t5brlu7fal70j, f0) == 0 && offsetof(gm_t5brlu7fal70j, f1) == 8, "layout of `ExprKind`");
_Static_assert(sizeof(gm_tc9qv69frb08e) == 136 && _Alignof(gm_tc9qv69frb08e) == 8 && offsetof(gm_tc9qv69frb08e, f0) == 0 && offsetof(gm_tc9qv69frb08e, f1) == 8 && offsetof(gm_tc9qv69frb08e, f2) == 120, "layout of `Pat`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_t5brlu7fal70j f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_tfpf0s89fpb3r;
_Static_assert(sizeof(gm_tfpf0s89fpb3r) == 224 && _Alignof(gm_tfpf0s89fpb3r) == 8 && offsetof(gm_tfpf0s89fpb3r, f0) == 0 && offsetof(gm_tfpf0s89fpb3r, f1) == 8 && offsetof(gm_tfpf0s89fpb3r, f2) == 208, "layout of `Expr`");
struct gm_fr_any_child_ffc8h1k {
    gm_frame h;
    gm_closure l1; /* pred */
    uint8_t l2; /* found */
    uint8_t ret;
};
struct gm_fr_binop_symbol_9ijp0eo {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_block_children_r2qinti {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_conds_children_3kg6846 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_default_spec_9ntjq2s {
    gm_frame h;
    gm_t7g88i52rrtm5 ret;
};
struct gm_fr_fn_7u87bne {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_hdas5h7 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_for_each_child_fm49s73 {
    gm_frame h;
    gm_t710lsrq1m8gk l39; /* w */
    gm_tsth9qu1dv0g3 l54; /* callee */
    gm_t710lsrq1m8gk l70; /* x */
    gm_tntn2tf15puib l74; /* w */
    gm_t9i38ih07bojq l79; /* w */
    gm_b0_1 ret;
};
struct gm_fr_for_each_subpat_arlrtrm {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_is_default_spec_cdd8jng {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_mk_9bc97fd {
    gm_frame h;
    gm_t5brlu7fal70j l1; /* kind */
    void * ret;
};
struct gm_fr_mk_pat_hr2l6c4 {
    gm_frame h;
    gm_tb38v5s9j1r32 l1; /* kind */
    void * ret;
};
struct gm_fr_pat_children_e3peovr {
    gm_frame h;
    gm_closure l1; /* f */
    gm_b0_1 ret;
};
struct gm_fr_sys_name_pcdoujk {
    gm_frame h;
    gm_b16_8 ret;
};
__attribute__((aligned(4))) uint8_t gm_f_any_child_ffc8h1k(void * a0, gm_closure a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_binop_symbol_9ijp0eo(gm_ts6oqppjh0ba3 a0);
__attribute__((aligned(4))) void gm_f_block_children_r2qinti(void * a0, gm_closure a1);
__attribute__((aligned(4))) void gm_f_conds_children_3kg6846(gm_b16_8 a0, gm_closure a1);
__attribute__((aligned(4))) void gm_f_default_spec_9ntjq2s(void *gm_out);
__attribute__((aligned(4))) gm_t7g88i52rrtm5 gm_f_default_spec_9ntjq2s_c(void);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_7u87bne(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_hdas5h7(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_for_each_child_fm49s73(void * a0, gm_closure a1);
__attribute__((aligned(4))) void gm_f_for_each_subpat_arlrtrm(void * a0, gm_closure a1);
__attribute__((aligned(4))) uint8_t gm_f_is_default_spec_cdd8jng(void * a0);
__attribute__((aligned(4))) void * gm_f_mk_9bc97fd(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0, const void * a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) void * gm_f_mk_pat_hr2l6c4(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0, const void * a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_1b2khgi(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_ve8bb01_2(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_pat_children_e3peovr(void * a0, gm_closure a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_sys_name_pcdoujk(gm_t9qkh0rsooc8d a0);
extern uint8_t gm_k3tttsr3hp1[40] __attribute__((aligned(8)));
extern uint8_t gm_k652m2ksgeg[40] __attribute__((aligned(8)));
extern const uint8_t gm_s02ru83e3fffc[3];
extern const uint8_t gm_s1f440st4mnkp[2];
extern const uint8_t gm_s1pfh89airaa0[13];
extern const uint8_t gm_s26ul1d5snk55[9];
extern const uint8_t gm_s2jvv6mjt9vs3[13];
extern const uint8_t gm_s2u5posfkbgnq[2];
extern const uint8_t gm_s4g3cajt15dmi[3];
extern const uint8_t gm_s4jhjvgae4896[13];
extern const uint8_t gm_s59a022mrijic[9];
extern const uint8_t gm_s65khs4s753q2[11];
extern const uint8_t gm_s6f0l2e1iha9i[8];
extern const uint8_t gm_s6s96daqmhcs9[2];
extern const uint8_t gm_s78m4481dj77r[9];
extern const uint8_t gm_s7ri8monh3c4h[8];
extern const uint8_t gm_s84p47580vnhr[12];
extern const uint8_t gm_s86kdga2ul6h4[9];
extern const uint8_t gm_s8o3vqolbfl8t[11];
extern const uint8_t gm_s9819enjtsd7t[10];
extern const uint8_t gm_s9hi985r6f61m[3];
extern const uint8_t gm_sa831sfid0hf3[2];
extern const uint8_t gm_saamkuuf6jrtf[8];
extern const uint8_t gm_sajhum9b4nr96[2];
extern const uint8_t gm_sal1kdi1rij5t[10];
extern const uint8_t gm_sb641udihu5hs[10];
extern const uint8_t gm_sbgj0biroecr0[13];
extern const uint8_t gm_sd9o9rh5qe8e7[11];
extern const uint8_t gm_sdtlgdl6ed52a[14];
extern const uint8_t gm_se3l2nlomk2pj[9];
extern const uint8_t gm_seajfhgtfmhv7[12];
extern const uint8_t gm_sed3elknidrs3[11];
extern const uint8_t gm_sf5jsgjp29cqn[14];
extern const uint8_t gm_sfbjh3dq66kkg[17];
extern const uint8_t gm_sfcjpmkuggqo3[10];
extern const uint8_t gm_sfd9hctnph9ef[12];
extern const uint8_t gm_sfku76mf5fn5d[9];
extern const uint8_t gm_shegcik3mpeut[12];
extern const uint8_t gm_si62ejtp0vbct[3];
extern const uint8_t gm_si8jv2q8d22kl[10];
extern const uint8_t gm_siamquc19uikr[2];
extern const uint8_t gm_sik1l49uru5dh[13];
extern const uint8_t gm_siph7o6oug1g2[14];
extern const uint8_t gm_siqaapb0mqsrg[14];
extern const uint8_t gm_sj49v74ko3ghf[15];
extern const uint8_t gm_sk0bnuggr6sri[14];
extern const uint8_t gm_sk1d0v1onojc1[8];
extern const uint8_t gm_sk77bvr488rq3[10];
extern const uint8_t gm_skk6lm5m17t6v[11];
extern const uint8_t gm_skm4ndn7op5l5[14];
extern const uint8_t gm_skolmnfc0ccbd[2];
extern const uint8_t gm_skqa2fjpbsodp[9];
extern const uint8_t gm_skti0sb80pmgp[12];
extern const uint8_t gm_slginip33cenb[14];
extern const uint8_t gm_slkubuckp49ih[12];
extern const uint8_t gm_sm5be4659ulci[2];
extern const uint8_t gm_smatmsv0oedot[8];
extern const uint8_t gm_smerqm8g793l6[2];
extern const uint8_t gm_smfprcpea3ibe[10];
extern const uint8_t gm_smvhngj8mlfjc[13];
extern const uint8_t gm_sp8bmcd3m68n6[7];
extern const char gm_srcod98slk3vnra[];
extern const uint8_t gm_ssj8r83l87dr2[16];
extern const uint8_t gm_sso3tuh819o23[9];
extern const uint8_t gm_st3ja0nqi37bv[2];
extern const uint8_t gm_stclkv4qnta30[9];
extern const uint8_t gm_stl47boisb31o[11];
extern const uint8_t gm_su6ipjjrnjs7p[10];
extern const uint8_t gm_suno9ilftpfht[12];
extern const uint8_t gm_sv1bu4l4kq188[8];
extern const uint8_t gm_sv9p44pp2n1jk[10];
extern const uint8_t gm_svkvl31jmj9ne[12];
extern const gm_type gm_tyo86ks9qkvt;
inline __attribute__((aligned(4))) uint8_t gm_f_any_child_ffc8h1k(void * a0, gm_closure a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_any_child_ffc8h1k F;
    void * gm_l0; /* e */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    F.l1 = a1;
    {
        F.l2 = ((uint8_t)0);
    }
    {
        void * t2 = gm_l0;
        gm_closure t3 = ((gm_closure){(void *)gm_f_fn_7u87bne, (void *)&F.h, cx});
        gm_f_for_each_child_fm49s73(t2, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        uint8_t t4 = F.l2;
        uint8_t t5 = 0;
        F.ret = t4;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_binop_symbol_9ijp0eo(gm_ts6oqppjh0ba3 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_binop_symbol_9ijp0eo F;
    gm_ts6oqppjh0ba3 gm_l0; /* op */
    GM_STACK_CHECK(&F, 289u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t1;
    uint8_t t18 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    switch (t18) { case ((uint8_t)0ULL): goto ma3; case ((uint8_t)1ULL): goto ma4; case ((uint8_t)2ULL): goto ma5; case ((uint8_t)3ULL): goto ma6; case ((uint8_t)4ULL): goto ma7; case ((uint8_t)5ULL): goto ma8; case ((uint8_t)6ULL): goto ma9; case ((uint8_t)7ULL): goto ma10; case ((uint8_t)8ULL): goto ma11; case ((uint8_t)9ULL): goto ma12; case ((uint8_t)10ULL): goto ma13; case ((uint8_t)11ULL): goto ma14; case ((uint8_t)12ULL): goto ma15; case ((uint8_t)13ULL): goto ma16; default: goto ma17; }
ma3:;
    uint8_t t20 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t20 != ((uint8_t)0ULL)) goto mn19;
    gm_b16_8 t21;
    GM_ST(void *, (uint8_t *)&t21, (void *)(gm_sajhum9b4nr96));
    GM_ST(uintptr_t, (uint8_t *)&t21 + 8, (uintptr_t)(1));
    t1 = t21;
    goto me2;
mn19:;
ma4:;
    uint8_t t23 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t23 != ((uint8_t)1ULL)) goto mn22;
    gm_b16_8 t24;
    GM_ST(void *, (uint8_t *)&t24, (void *)(gm_skolmnfc0ccbd));
    GM_ST(uintptr_t, (uint8_t *)&t24 + 8, (uintptr_t)(1));
    t1 = t24;
    goto me2;
mn22:;
ma5:;
    uint8_t t26 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t26 != ((uint8_t)2ULL)) goto mn25;
    gm_b16_8 t27;
    GM_ST(void *, (uint8_t *)&t27, (void *)(gm_s6s96daqmhcs9));
    GM_ST(uintptr_t, (uint8_t *)&t27 + 8, (uintptr_t)(1));
    t1 = t27;
    goto me2;
mn25:;
ma6:;
    uint8_t t29 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t29 != ((uint8_t)3ULL)) goto mn28;
    gm_b16_8 t30;
    GM_ST(void *, (uint8_t *)&t30, (void *)(gm_si62ejtp0vbct));
    GM_ST(uintptr_t, (uint8_t *)&t30 + 8, (uintptr_t)(2));
    t1 = t30;
    goto me2;
mn28:;
ma7:;
    uint8_t t32 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t32 != ((uint8_t)4ULL)) goto mn31;
    gm_b16_8 t33;
    GM_ST(void *, (uint8_t *)&t33, (void *)(gm_st3ja0nqi37bv));
    GM_ST(uintptr_t, (uint8_t *)&t33 + 8, (uintptr_t)(1));
    t1 = t33;
    goto me2;
mn31:;
ma8:;
    uint8_t t35 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t35 != ((uint8_t)5ULL)) goto mn34;
    gm_b16_8 t36;
    GM_ST(void *, (uint8_t *)&t36, (void *)(gm_siamquc19uikr));
    GM_ST(uintptr_t, (uint8_t *)&t36 + 8, (uintptr_t)(1));
    t1 = t36;
    goto me2;
mn34:;
ma9:;
    uint8_t t38 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t38 != ((uint8_t)6ULL)) goto mn37;
    gm_b16_8 t39;
    GM_ST(void *, (uint8_t *)&t39, (void *)(gm_s9hi985r6f61m));
    GM_ST(uintptr_t, (uint8_t *)&t39 + 8, (uintptr_t)(2));
    t1 = t39;
    goto me2;
mn37:;
ma10:;
    uint8_t t41 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t41 != ((uint8_t)7ULL)) goto mn40;
    gm_b16_8 t42;
    GM_ST(void *, (uint8_t *)&t42, (void *)(gm_smerqm8g793l6));
    GM_ST(uintptr_t, (uint8_t *)&t42 + 8, (uintptr_t)(1));
    t1 = t42;
    goto me2;
mn40:;
ma11:;
    uint8_t t44 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t44 != ((uint8_t)8ULL)) goto mn43;
    gm_b16_8 t45;
    GM_ST(void *, (uint8_t *)&t45, (void *)(gm_s2u5posfkbgnq));
    GM_ST(uintptr_t, (uint8_t *)&t45 + 8, (uintptr_t)(1));
    t1 = t45;
    goto me2;
mn43:;
ma12:;
    uint8_t t47 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t47 != ((uint8_t)9ULL)) goto mn46;
    gm_b16_8 t48;
    GM_ST(void *, (uint8_t *)&t48, (void *)(gm_sa831sfid0hf3));
    GM_ST(uintptr_t, (uint8_t *)&t48 + 8, (uintptr_t)(1));
    t1 = t48;
    goto me2;
mn46:;
ma13:;
    uint8_t t50 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t50 != ((uint8_t)10ULL)) goto mn49;
    gm_b16_8 t51;
    GM_ST(void *, (uint8_t *)&t51, (void *)(gm_s1f440st4mnkp));
    GM_ST(uintptr_t, (uint8_t *)&t51 + 8, (uintptr_t)(1));
    t1 = t51;
    goto me2;
mn49:;
ma14:;
    uint8_t t53 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t53 != ((uint8_t)11ULL)) goto mn52;
    gm_b16_8 t54;
    GM_ST(void *, (uint8_t *)&t54, (void *)(gm_s4g3cajt15dmi));
    GM_ST(uintptr_t, (uint8_t *)&t54 + 8, (uintptr_t)(2));
    t1 = t54;
    goto me2;
mn52:;
ma15:;
    uint8_t t56 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t56 != ((uint8_t)12ULL)) goto mn55;
    gm_b16_8 t57;
    GM_ST(void *, (uint8_t *)&t57, (void *)(gm_sm5be4659ulci));
    GM_ST(uintptr_t, (uint8_t *)&t57 + 8, (uintptr_t)(1));
    t1 = t57;
    goto me2;
mn55:;
ma16:;
    uint8_t t59 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t59 != ((uint8_t)13ULL)) goto mn58;
    gm_b16_8 t60;
    GM_ST(void *, (uint8_t *)&t60, (void *)(gm_s02ru83e3fffc));
    GM_ST(uintptr_t, (uint8_t *)&t60 + 8, (uintptr_t)(2));
    t1 = t60;
    goto me2;
mn58:;
ma17:;
    gm_trap("no match arm matched the scrutinee", gm_srcod98slk3vnra, 342, 48);
me2:;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) void gm_f_block_children_r2qinti(void * a0, gm_closure a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_block_children_r2qinti F;
    void * gm_l0; /* b */
    gm_closure gm_l1; /* f */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    gm_trcmmd12cefb1 gm_l4; /* s */
    void * gm_l5; /* x */
    void * gm_l6; /* x */
    void * gm_l7; /* p */
    void * gm_l8; /* v */
    gm_b16_8 gm_l9; /* ps */
    void * gm_l10; /* v */
    gm_b16_8 gm_l11; /* <tmp> */
    uint64_t gm_l12; /* <tmp> */
    void * gm_l13; /* p */
    void * gm_l14; /* x */
    void * gm_l15; /* x */
    void * gm_l16; /* x */
    void * gm_l17; /* t */
    GM_STACK_CHECK(&F, 1066u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        {
            void * t3 = gm_l0;
            gm_b16_8 t4 = GM_LD(gm_b16_8, (((uint8_t *)t3) + 0));
            gm_l2 = t4;
        }
        {
            gm_l3 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t6 = gm_l3;
                uint64_t t7 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                uint8_t t8 = (t6 < t7);
                if (!t8) goto lb5;
                {
                    uint64_t t11 = gm_l3;
                    uint64_t t12 = (uintptr_t)t11;
                    uint64_t t13 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                    gm_trcmmd12cefb1 t16 = GM_LD(gm_trcmmd12cefb1, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t12 * 32));
                    gm_l4 = t16;
                    {
                        uint8_t t26 = GM_LD(uint8_t, (((uint8_t *)&gm_l4) + 0));
                        switch (t26) { case ((uint8_t)0ULL): goto ma19; case ((uint8_t)1ULL): goto ma21; case ((uint8_t)2ULL): goto ma22; case ((uint8_t)3ULL): goto ma23; case ((uint8_t)4ULL): goto ma24; case ((uint8_t)5ULL): goto ma24; default: goto ma25; }
ma19:;
                        uint8_t t28 = GM_LD(uint8_t, (((uint8_t *)&gm_l4) + 0));
                        if (t28 != ((uint8_t)0ULL)) goto mn27;
                        if ((GM_LD(void *, ((((uint8_t *)&gm_l4) + 8) + 8)) != 0)) goto mn27;
                        goto me18;
mn27:;
ma20:;
                        uint8_t t31 = GM_LD(uint8_t, (((uint8_t *)&gm_l4) + 0));
                        if (t31 != ((uint8_t)0ULL)) goto mn30;
                        if (!(GM_LD(void *, ((((uint8_t *)&gm_l4) + 8) + 8)) != 0)) goto mn30;
                        void * t32 = GM_LD(void *, ((((uint8_t *)&gm_l4) + 8) + 8));
                        gm_l5 = t32;
                        gm_closure t33 = gm_l1;
                        void * t34 = gm_l5;
                        gm_closure t35 = t33;
                        gm_node *ccx36 = t35.cx;
                        void * t37 = t35.code;
                        gm_b0_1 t38;
                        if (GM_PTR_TAGGED(t37)) {
                            ((void (*)(void *))GM_PTR_UNTAG(t37))(t34);
                        } else {
                            t38 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t37)(t35.env, ccx36, NULL, t34);
                        }
                        if (gm_unw.kind) {
                            goto gm_ll9;
                        }
                        goto me18;
mn30:;
ma21:;
                        uint8_t t40 = GM_LD(uint8_t, (((uint8_t *)&gm_l4) + 0));
                        if (t40 != ((uint8_t)1ULL)) goto mn39;
                        void * t41 = GM_LD(void *, ((((uint8_t *)&gm_l4) + 8) + 16));
                        gm_l6 = t41;
                        gm_closure t42 = gm_l1;
                        void * t43 = gm_l6;
                        gm_closure t44 = t42;
                        gm_node *ccx45 = t44.cx;
                        void * t46 = t44.code;
                        gm_b0_1 t47;
                        if (GM_PTR_TAGGED(t46)) {
                            ((void (*)(void *))GM_PTR_UNTAG(t46))(t43);
                        } else {
                            t47 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t46)(t44.env, ccx45, NULL, t43);
                        }
                        if (gm_unw.kind) {
                            goto gm_ll9;
                        }
                        goto me18;
mn39:;
ma22:;
                        uint8_t t49 = GM_LD(uint8_t, (((uint8_t *)&gm_l4) + 0));
                        if (t49 != ((uint8_t)2ULL)) goto mn48;
                        void * t50 = GM_LD(void *, ((((uint8_t *)&gm_l4) + 8) + 0));
                        gm_l7 = t50;
                        void * t51 = GM_LD(void *, ((((uint8_t *)&gm_l4) + 8) + 8));
                        gm_l8 = t51;
                        {
                            gm_closure t53 = gm_l1;
                            void * t54 = gm_l7;
                            gm_closure t55 = t53;
                            gm_node *ccx56 = t55.cx;
                            void * t57 = t55.code;
                            gm_b0_1 t58;
                            if (GM_PTR_TAGGED(t57)) {
                                ((void (*)(void *))GM_PTR_UNTAG(t57))(t54);
                            } else {
                                t58 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t57)(t55.env, ccx56, NULL, t54);
                            }
                            if (gm_unw.kind) {
                                goto gm_ll9;
                            }
                        }
                        {
                            gm_closure t59 = gm_l1;
                            void * t60 = gm_l8;
                            gm_closure t61 = t59;
                            gm_node *ccx62 = t61.cx;
                            void * t63 = t61.code;
                            gm_b0_1 t64;
                            if (GM_PTR_TAGGED(t63)) {
                                ((void (*)(void *))GM_PTR_UNTAG(t63))(t60);
                            } else {
                                t64 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t63)(t61.env, ccx62, NULL, t60);
                            }
                            if (gm_unw.kind) {
                                goto gm_ll9;
                            }
                        }
                        goto me18;
mn48:;
ma23:;
                        uint8_t t66 = GM_LD(uint8_t, (((uint8_t *)&gm_l4) + 0));
                        if (t66 != ((uint8_t)3ULL)) goto mn65;
                        gm_b16_8 t67 = GM_LD(gm_b16_8, ((((uint8_t *)&gm_l4) + 8) + 0));
                        gm_l9 = t67;
                        void * t68 = GM_LD(void *, ((((uint8_t *)&gm_l4) + 8) + 16));
                        gm_l10 = t68;
                        {
                            {
                                gm_b16_8 t71 = gm_l9;
                                gm_l11 = t71;
                            }
                            {
                                gm_l12 = ((uint64_t)0ULL);
                            }
                            {
                                for (;;) {
                                    uint64_t t73 = gm_l12;
                                    uint64_t t74 = GM_LD(uintptr_t, ((uint8_t *)&gm_l11) + 8);
                                    uint8_t t75 = (t73 < t74);
                                    if (!t75) goto lb72;
                                    {
                                        uint64_t t78 = gm_l12;
                                        uint64_t t79 = (uintptr_t)t78;
                                        uint64_t t80 = GM_LD(uintptr_t, ((uint8_t *)&gm_l11) + 8);
                                        void * t81 = GM_LD(void *, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l11)) + t79 * 8));
                                        void * t82 = t81;
                                        void * t85 = GM_LD(void *, ((uint8_t *)&t82));
                                        gm_l13 = t85;
                                        {
                                            void * t88 = gm_l13;
                                            void * t89 = t88;
                                            if (!(GM_LD(void *, ((uint8_t *)&t89)) != 0)) goto ie87;
                                            void * t90 = GM_LD(void *, ((uint8_t *)&t89));
                                            gm_l14 = t90;
                                            {
                                                gm_closure t92 = gm_l1;
                                                void * t93 = gm_l14;
                                                gm_closure t94 = t92;
                                                gm_node *ccx95 = t94.cx;
                                                void * t96 = t94.code;
                                                gm_b0_1 t97;
                                                if (GM_PTR_TAGGED(t96)) {
                                                    ((void (*)(void *))GM_PTR_UNTAG(t96))(t93);
                                                } else {
                                                    t97 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t96)(t94.env, ccx95, NULL, t93);
                                                }
                                                if (gm_unw.kind) {
                                                    goto gm_ll76;
                                                }
                                            }
                                            goto ix87;
ie87:;
ix87:;
                                        }
                                        goto me83;
mn84:;
                                        gm_trap("no match arm matched the scrutinee", gm_srcod98slk3vnra, 669, 17);
me83:;
                                    }
lc72:;
                                    {
                                        uint64_t t99 = gm_l12;
                                        uint64_t r100; bool o101 = __builtin_add_overflow(t99, ((uint64_t)1ULL), &r100);
                                        uint64_t t102 = r100;
                                        gm_l12 = t102;
                                    }
                                }
gm_ll76:;
                                if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb72; }
                                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc72; }
                                }
                                goto gm_ll9;
lb72:;
                            }
                        }
                        {
                            gm_closure t103 = gm_l1;
                            void * t104 = gm_l10;
                            gm_closure t105 = t103;
                            gm_node *ccx106 = t105.cx;
                            void * t107 = t105.code;
                            gm_b0_1 t108;
                            if (GM_PTR_TAGGED(t107)) {
                                ((void (*)(void *))GM_PTR_UNTAG(t107))(t104);
                            } else {
                                t108 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t107)(t105.env, ccx106, NULL, t104);
                            }
                            if (gm_unw.kind) {
                                goto gm_ll9;
                            }
                        }
                        goto me18;
mn65:;
ma24:;
                        uint8_t t112 = GM_LD(uint8_t, (((uint8_t *)&gm_l4) + 0));
                        if (t112 != ((uint8_t)4ULL)) goto pa111;
                        void * t113 = GM_LD(void *, (((uint8_t *)&gm_l4) + 8));
                        gm_l15 = t113;
                        goto po110;
pa111:;
                        uint8_t t115 = GM_LD(uint8_t, (((uint8_t *)&gm_l4) + 0));
                        if (t115 != ((uint8_t)5ULL)) goto pa114;
                        void * t116 = GM_LD(void *, ((((uint8_t *)&gm_l4) + 8) + 8));
                        gm_l15 = t116;
                        goto po110;
pa114:;
                        goto mn109;
po110:;
                        gm_closure t117 = gm_l1;
                        void * t118 = gm_l15;
                        gm_closure t119 = t117;
                        gm_node *ccx120 = t119.cx;
                        void * t121 = t119.code;
                        gm_b0_1 t122;
                        if (GM_PTR_TAGGED(t121)) {
                            ((void (*)(void *))GM_PTR_UNTAG(t121))(t118);
                        } else {
                            t122 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t121)(t119.env, ccx120, NULL, t118);
                        }
                        if (gm_unw.kind) {
                            goto gm_ll9;
                        }
                        goto me18;
mn109:;
ma25:;
                        gm_trap("no match arm matched the scrutinee", gm_srcod98slk3vnra, 660, 9);
me18:;
                    }
                    goto me14;
mn15:;
                    gm_trap("no match arm matched the scrutinee", gm_srcod98slk3vnra, 659, 5);
me14:;
                }
lc5:;
                {
                    uint64_t t124 = gm_l3;
                    uint64_t r125; bool o126 = __builtin_add_overflow(t124, ((uint64_t)1ULL), &r125);
                    uint64_t t127 = r125;
                    gm_l3 = t127;
                }
            }
gm_ll9:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb5; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc5; }
            }
            goto gm_lad;
lb5:;
        }
    }
    {
        void * t129 = gm_l0;
        void * t130 = GM_LD(void *, (((uint8_t *)t129) + 16));
        void * t131 = t130;
        if (!(GM_LD(void *, ((uint8_t *)&t131)) != 0)) goto ie128;
        void * t132 = GM_LD(void *, ((uint8_t *)&t131));
        gm_l17 = t132;
        {
            gm_closure t134 = gm_l1;
            void * t135 = gm_l17;
            gm_closure t136 = t134;
            gm_node *ccx137 = t136.cx;
            void * t138 = t136.code;
            gm_b0_1 t139;
            if (GM_PTR_TAGGED(t138)) {
                ((void (*)(void *))GM_PTR_UNTAG(t138))(t135);
            } else {
                t139 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t138)(t136.env, ccx137, NULL, t135);
            }
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix128;
ie128:;
ix128:;
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
inline __attribute__((aligned(4))) void gm_f_conds_children_3kg6846(gm_b16_8 a0, gm_closure a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_conds_children_3kg6846 F;
    gm_b16_8 gm_l0; /* cond */
    gm_closure gm_l1; /* f */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    gm_tlr9dp305htm7 gm_l4; /* c */
    void * gm_l5; /* x */
    void * gm_l6; /* p */
    void * gm_l7; /* v */
    GM_STACK_CHECK(&F, 395u);
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
                    gm_tlr9dp305htm7 t15 = GM_LD(gm_tlr9dp305htm7, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t11 * 24));
                    gm_l4 = t15;
                    {
                        uint8_t t19 = GM_LD(uint8_t, (((uint8_t *)&gm_l4) + 0));
                        if (t19 != ((uint8_t)0ULL)) goto mn18;
                        void * t20 = GM_LD(void *, (((uint8_t *)&gm_l4) + 8));
                        gm_l5 = t20;
                        gm_closure t21 = gm_l1;
                        void * t22 = gm_l5;
                        gm_closure t23 = t21;
                        gm_node *ccx24 = t23.cx;
                        void * t25 = t23.code;
                        gm_b0_1 t26;
                        if (GM_PTR_TAGGED(t25)) {
                            ((void (*)(void *))GM_PTR_UNTAG(t25))(t22);
                        } else {
                            t26 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t25)(t23.env, ccx24, NULL, t22);
                        }
                        if (gm_unw.kind) {
                            goto gm_ll8;
                        }
                        goto me17;
mn18:;
                        uint8_t t28 = GM_LD(uint8_t, (((uint8_t *)&gm_l4) + 0));
                        if (t28 != ((uint8_t)1ULL)) goto mn27;
                        void * t29 = GM_LD(void *, ((((uint8_t *)&gm_l4) + 8) + 0));
                        gm_l6 = t29;
                        void * t30 = GM_LD(void *, ((((uint8_t *)&gm_l4) + 8) + 8));
                        gm_l7 = t30;
                        {
                            void * t32 = gm_l6;
                            gm_closure t33 = gm_l1;
                            gm_f_pat_children_e3peovr(t32, t33);
                            if (gm_unw.kind) {
                                goto gm_ll8;
                            }
                        }
                        {
                            gm_closure t34 = gm_l1;
                            void * t35 = gm_l7;
                            gm_closure t36 = t34;
                            gm_node *ccx37 = t36.cx;
                            void * t38 = t36.code;
                            gm_b0_1 t39;
                            if (GM_PTR_TAGGED(t38)) {
                                ((void (*)(void *))GM_PTR_UNTAG(t38))(t35);
                            } else {
                                t39 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t38)(t36.env, ccx37, NULL, t35);
                            }
                            if (gm_unw.kind) {
                                goto gm_ll8;
                            }
                        }
                        goto me17;
mn27:;
                        gm_trap("no match arm matched the scrutinee", gm_srcod98slk3vnra, 682, 9);
me17:;
                    }
                    goto me13;
mn14:;
                    gm_trap("no match arm matched the scrutinee", gm_srcod98slk3vnra, 681, 5);
me13:;
                }
lc4:;
                {
                    uint64_t t41 = gm_l3;
                    uint64_t r42; bool o43 = __builtin_add_overflow(t41, ((uint64_t)1ULL), &r42);
                    uint64_t t44 = r42;
                    gm_l3 = t44;
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
inline __attribute__((aligned(4))) void gm_f_default_spec_9ntjq2s(void *gm_out) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_default_spec_9ntjq2s F;
    GM_STACK_CHECK(&F, 10u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memset((uint8_t *)&(*(gm_t7g88i52rrtm5 *)gm_out) + 5, 0, 3);
    memset((uint8_t *)&(*(gm_t7g88i52rrtm5 *)gm_out) + 21, 0, 3);
    gm_t9qkh0rsooc8d t1;
    GM_ST(uint8_t, ((uint8_t *)&t1 + 0), ((uint8_t)0ULL));
    gm_t9805qtonbsag t2;
    memset(&t2, 0, sizeof t2);
    gm_t9qkh0rsooc8d t3;
    GM_ST(uint8_t, ((uint8_t *)&t3 + 0), ((uint8_t)0ULL));
    GM_ST(uint8_t, (uint8_t *)&(*(gm_t7g88i52rrtm5 *)gm_out) + 0, ((uint8_t)32ULL));
    GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_t7g88i52rrtm5 *)gm_out) + 1, t1);
    GM_ST(uint8_t, (uint8_t *)&(*(gm_t7g88i52rrtm5 *)gm_out) + 2, ((uint8_t)0));
    GM_ST(uint8_t, (uint8_t *)&(*(gm_t7g88i52rrtm5 *)gm_out) + 3, ((uint8_t)0));
    GM_ST(uint8_t, (uint8_t *)&(*(gm_t7g88i52rrtm5 *)gm_out) + 4, ((uint8_t)0));
    GM_ST(uint32_t, (uint8_t *)&(*(gm_t7g88i52rrtm5 *)gm_out) + 8, ((uint32_t)0ULL));
    GM_ST(gm_t9805qtonbsag, (uint8_t *)&(*(gm_t7g88i52rrtm5 *)gm_out) + 12, t2);
    GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_t7g88i52rrtm5 *)gm_out) + 20, t3);
    goto gm_fdone;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_t7g88i52rrtm5 gm_f_default_spec_9ntjq2s_c(void) { gm_t7g88i52rrtm5 gm_r; gm_f_default_spec_9ntjq2s(&gm_r); return gm_r; }
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_7u87bne(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_fn_7u87bne F;
    void * gm_l0; /* c */
    GM_STACK_CHECK(&F, 75u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_any_child_ffc8h1k *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            gm_closure t6 = GM_LD(gm_closure, ((uint8_t *)&((struct gm_fr_any_child_ffc8h1k *)((&F.h)->up))->l1));
            void * t7 = gm_l0;
            gm_closure t8 = t6;
            gm_node *ccx9 = t8.cx;
            void * t10 = t8.code;
            uint8_t t11;
            if (GM_PTR_TAGGED(t10)) {
                t11 = ((uint8_t (*)(void *))GM_PTR_UNTAG(t10))(t7);
            } else {
                t11 = ((uint8_t (*)(void *, gm_node *, gm_node *, void *))t10)(t8.env, ccx9, NULL, t7);
            }
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_any_child_ffc8h1k *)((&F.h)->up))->l2), t11);
        }
        goto ix2;
ie2:;
ix2:;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_hdas5h7(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_fn_hdas5h7 F;
    void * gm_l0; /* q */
    GM_STACK_CHECK(&F, 40u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        gm_closure t3 = GM_LD(gm_closure, ((uint8_t *)&((struct gm_fr_pat_children_e3peovr *)((&F.h)->up))->l1));
        gm_f_pat_children_e3peovr(t2, t3);
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
__attribute__((aligned(4))) void gm_f_for_each_child_fm49s73(void * a0, gm_closure a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_for_each_child_fm49s73 F;
    void * gm_l0; /* e */
    gm_closure gm_l1; /* f */
    void * gm_l2; /* a */
    void * gm_l3; /* a */
    void * gm_l4; /* a */
    void * gm_l5; /* a */
    void * gm_l6; /* a */
    void * gm_l7; /* a */
    void * gm_l8; /* a */
    void * gm_l9; /* a */
    gm_tao63pk3r18v5 gm_l10; /* sl */
    void * gm_l11; /* x */
    void * gm_l12; /* x */
    void * gm_l13; /* a */
    void * gm_l14; /* a */
    void * gm_l15; /* a */
    gm_t3dcoc7077t1q gm_l16; /* x */
    gm_t083kd7b1kkag gm_l17; /* x */
    void * gm_l18; /* a */
    void * gm_l19; /* a */
    void * gm_l20; /* k */
    void * gm_l21; /* v */
    void * gm_l22; /* k */
    void * gm_l23; /* a */
    void * gm_l24; /* b */
    void * gm_l25; /* a */
    void * gm_l26; /* b */
    void * gm_l27; /* a */
    void * gm_l28; /* b */
    void * gm_l29; /* a */
    void * gm_l30; /* b */
    void * gm_l31; /* a */
    void * gm_l32; /* b */
    void * gm_l33; /* s */
    gm_b16_8 gm_l34; /* arms */
    gm_b16_8 gm_l35; /* <tmp> */
    uint64_t gm_l36; /* <tmp> */
    gm_tbbqefuhv2r8h gm_l37; /* a */
    void * gm_l38; /* g */
    gm_b16_8 gm_l40; /* <tmp> */
    uint64_t gm_l41; /* <tmp> */
    gm_t4lnf71mruqpt gm_l42; /* h */
    void * gm_l43; /* x */
    void * gm_l44; /* x */
    void * gm_l45; /* x */
    gm_b16_8 gm_l46; /* args */
    uint64_t gm_l47; /* <tmp> */
    uint64_t gm_l48; /* <tmp> */
    uint64_t gm_l49; /* i */
    gm_tandvbt63uk7n gm_l50; /* x */
    uint64_t gm_l51; /* <tmp> */
    uint64_t gm_l52; /* <tmp> */
    uint64_t gm_l53; /* i */
    gm_b16_8 gm_l55; /* args */
    void * gm_l56; /* c */
    uint64_t gm_l57; /* <tmp> */
    uint64_t gm_l58; /* <tmp> */
    uint64_t gm_l59; /* i */
    gm_b16_8 gm_l60; /* xs */
    gm_b16_8 gm_l61; /* xs */
    uint64_t gm_l62; /* <tmp> */
    uint64_t gm_l63; /* <tmp> */
    uint64_t gm_l64; /* i */
    gm_ttggq7ng4kp3p gm_l65; /* v */
    void * gm_l66; /* p */
    void * gm_l67; /* b */
    gm_trl2khmuphncd gm_l68; /* x */
    void * gm_l69; /* e2 */
    void * gm_l71; /* st */
    gm_teo9d4mncjfdu gm_l72; /* x */
    void * gm_l73; /* v */
    void * gm_l75; /* x */
    uint64_t gm_l76; /* <tmp> */
    uint64_t gm_l77; /* <tmp> */
    uint64_t gm_l78; /* i */
    void * gm_l80; /* x */
    void * gm_l81; /* a */
    GM_STACK_CHECK(&F, 5482u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        uint8_t t33 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        switch (t33) { case ((uint8_t)0ULL): goto ma31; case ((uint8_t)1ULL): goto ma31; case ((uint8_t)2ULL): goto ma31; case ((uint8_t)3ULL): goto ma31; case ((uint8_t)4ULL): goto ma31; case ((uint8_t)5ULL): goto ma31; case ((uint8_t)6ULL): goto ma31; case ((uint8_t)7ULL): goto ma31; case ((uint8_t)8ULL): goto ma31; case ((uint8_t)9ULL): goto ma31; case ((uint8_t)10ULL): goto ma31; case ((uint8_t)11ULL): goto ma31; case ((uint8_t)12ULL): goto ma31; case ((uint8_t)13ULL): goto ma31; case ((uint8_t)14ULL): goto ma31; case ((uint8_t)15ULL): goto ma31; case ((uint8_t)16ULL): goto ma31; case ((uint8_t)17ULL): goto ma31; case ((uint8_t)18ULL): goto ma4; case ((uint8_t)19ULL): goto ma11; case ((uint8_t)20ULL): goto ma15; case ((uint8_t)21ULL): goto ma5; case ((uint8_t)22ULL): goto ma4; case ((uint8_t)23ULL): goto ma4; case ((uint8_t)24ULL): goto ma4; case ((uint8_t)25ULL): goto ma4; case ((uint8_t)26ULL): goto ma31; case ((uint8_t)27ULL): goto ma31; case ((uint8_t)28ULL): goto ma31; case ((uint8_t)29ULL): goto ma6; case ((uint8_t)30ULL): goto ma16; case ((uint8_t)31ULL): goto ma16; case ((uint8_t)32ULL): goto ma7; case ((uint8_t)33ULL): goto ma20; case ((uint8_t)34ULL): goto ma22; case ((uint8_t)35ULL): goto ma23; case ((uint8_t)36ULL): goto ma23; case ((uint8_t)37ULL): goto ma4; case ((uint8_t)38ULL): goto ma24; case ((uint8_t)39ULL): goto ma4; case ((uint8_t)40ULL): goto ma8; case ((uint8_t)41ULL): goto ma9; case ((uint8_t)42ULL): goto ma10; case ((uint8_t)43ULL): goto ma17; case ((uint8_t)44ULL): goto ma18; case ((uint8_t)45ULL): goto ma17; case ((uint8_t)46ULL): goto ma25; case ((uint8_t)47ULL): goto ma26; case ((uint8_t)48ULL): goto ma27; case ((uint8_t)49ULL): goto ma28; case ((uint8_t)50ULL): goto ma31; case ((uint8_t)51ULL): goto ma12; case ((uint8_t)52ULL): goto ma4; case ((uint8_t)53ULL): goto ma13; case ((uint8_t)54ULL): goto ma14; case ((uint8_t)55ULL): goto ma29; case ((uint8_t)56ULL): goto ma30; case ((uint8_t)57ULL): goto ma21; case ((uint8_t)58ULL): goto ma19; default: goto ma31; }
ma4:;
        uint8_t t37 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t37 != ((uint8_t)18ULL)) goto pa36;
        void * t38 = GM_LD(void *, ((((uint8_t *)t2) + 8) + 8));
        gm_l2 = t38;
        goto po35;
pa36:;
        uint8_t t40 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t40 != ((uint8_t)24ULL)) goto pa39;
        void * t41 = GM_LD(void *, ((((uint8_t *)t2) + 8) + 8));
        gm_l2 = t41;
        goto po35;
pa39:;
        uint8_t t43 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t43 != ((uint8_t)25ULL)) goto pa42;
        void * t44 = GM_LD(void *, ((((uint8_t *)t2) + 8) + 8));
        gm_l2 = t44;
        goto po35;
pa42:;
        uint8_t t46 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t46 != ((uint8_t)39ULL)) goto pa45;
        void * t47 = GM_LD(void *, ((((uint8_t *)t2) + 8) + 8));
        gm_l2 = t47;
        goto po35;
pa45:;
        uint8_t t49 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t49 != ((uint8_t)37ULL)) goto pa48;
        void * t50 = GM_LD(void *, ((((uint8_t *)t2) + 8) + 8));
        gm_l2 = t50;
        goto po35;
pa48:;
        uint8_t t52 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t52 != ((uint8_t)52ULL)) goto pa51;
        void * t53 = GM_LD(void *, ((((uint8_t *)t2) + 8) + 8));
        gm_l2 = t53;
        goto po35;
pa51:;
        uint8_t t55 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t55 != ((uint8_t)22ULL)) goto pa54;
        void * t56 = GM_LD(void *, ((((uint8_t *)t2) + 8) + 8));
        gm_l2 = t56;
        goto po35;
pa54:;
        uint8_t t58 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t58 != ((uint8_t)23ULL)) goto pa57;
        void * t59 = GM_LD(void *, ((((uint8_t *)t2) + 8) + 8));
        gm_l2 = t59;
        goto po35;
pa57:;
        goto mn34;
po35:;
        gm_closure t60 = gm_l1;
        void * t61 = gm_l2;
        gm_closure t62 = t60;
        gm_node *ccx63 = t62.cx;
        void * t64 = t62.code;
        gm_b0_1 t65;
        if (GM_PTR_TAGGED(t64)) {
            ((void (*)(void *))GM_PTR_UNTAG(t64))(t61);
        } else {
            t65 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t64)(t62.env, ccx63, NULL, t61);
        }
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me3;
mn34:;
ma5:;
        uint8_t t67 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t67 != ((uint8_t)21ULL)) goto mn66;
        gm_tao63pk3r18v5 t68 = GM_LD(gm_tao63pk3r18v5, ((((uint8_t *)t2) + 8) + 8));
        gm_l10 = t68;
        {
            gm_closure t70 = gm_l1;
            void * t71 = GM_LD(void *, (((uint8_t *)&gm_l10) + 0));
            gm_closure t72 = t70;
            gm_node *ccx73 = t72.cx;
            void * t74 = t72.code;
            gm_b0_1 t75;
            if (GM_PTR_TAGGED(t74)) {
                ((void (*)(void *))GM_PTR_UNTAG(t74))(t71);
            } else {
                t75 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t74)(t72.env, ccx73, NULL, t71);
            }
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t77 = GM_LD(void *, (((uint8_t *)&gm_l10) + 8));
            void * t78 = t77;
            if (!(GM_LD(void *, ((uint8_t *)&t78)) != 0)) goto ie76;
            void * t79 = GM_LD(void *, ((uint8_t *)&t78));
            gm_l11 = t79;
            {
                gm_closure t81 = gm_l1;
                void * t82 = gm_l11;
                gm_closure t83 = t81;
                gm_node *ccx84 = t83.cx;
                void * t85 = t83.code;
                gm_b0_1 t86;
                if (GM_PTR_TAGGED(t85)) {
                    ((void (*)(void *))GM_PTR_UNTAG(t85))(t82);
                } else {
                    t86 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t85)(t83.env, ccx84, NULL, t82);
                }
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix76;
ie76:;
ix76:;
        }
        {
            void * t88 = GM_LD(void *, (((uint8_t *)&gm_l10) + 16));
            void * t89 = t88;
            if (!(GM_LD(void *, ((uint8_t *)&t89)) != 0)) goto ie87;
            void * t90 = GM_LD(void *, ((uint8_t *)&t89));
            gm_l12 = t90;
            {
                gm_closure t92 = gm_l1;
                void * t93 = gm_l12;
                gm_closure t94 = t92;
                gm_node *ccx95 = t94.cx;
                void * t96 = t94.code;
                gm_b0_1 t97;
                if (GM_PTR_TAGGED(t96)) {
                    ((void (*)(void *))GM_PTR_UNTAG(t96))(t93);
                } else {
                    t97 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t96)(t94.env, ccx95, NULL, t93);
                }
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix87;
ie87:;
ix87:;
        }
        goto me3;
mn66:;
ma6:;
        uint8_t t99 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t99 != ((uint8_t)29ULL)) goto mn98;
        void * t100 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 8));
        gm_l13 = t100;
        gm_closure t101 = gm_l1;
        void * t102 = gm_l13;
        gm_closure t103 = t101;
        gm_node *ccx104 = t103.cx;
        void * t105 = t103.code;
        gm_b0_1 t106;
        if (GM_PTR_TAGGED(t105)) {
            ((void (*)(void *))GM_PTR_UNTAG(t105))(t102);
        } else {
            t106 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t105)(t103.env, ccx104, NULL, t102);
        }
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me3;
mn98:;
ma7:;
        uint8_t t108 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t108 != ((uint8_t)32ULL)) goto mn107;
        void * t109 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 8));
        gm_l14 = t109;
        gm_closure t110 = gm_l1;
        void * t111 = gm_l14;
        gm_closure t112 = t110;
        gm_node *ccx113 = t112.cx;
        void * t114 = t112.code;
        gm_b0_1 t115;
        if (GM_PTR_TAGGED(t114)) {
            ((void (*)(void *))GM_PTR_UNTAG(t114))(t111);
        } else {
            t115 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t114)(t112.env, ccx113, NULL, t111);
        }
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me3;
mn107:;
ma8:;
        uint8_t t117 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t117 != ((uint8_t)40ULL)) goto mn116;
        void * t118 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 0));
        gm_l15 = t118;
        gm_closure t119 = gm_l1;
        void * t120 = gm_l15;
        gm_closure t121 = t119;
        gm_node *ccx122 = t121.cx;
        void * t123 = t121.code;
        gm_b0_1 t124;
        if (GM_PTR_TAGGED(t123)) {
            ((void (*)(void *))GM_PTR_UNTAG(t123))(t120);
        } else {
            t124 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t123)(t121.env, ccx122, NULL, t120);
        }
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me3;
mn116:;
ma9:;
        uint8_t t126 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t126 != ((uint8_t)41ULL)) goto mn125;
        gm_t3dcoc7077t1q t127 = GM_LD(gm_t3dcoc7077t1q, ((((uint8_t *)t2) + 8) + 8));
        gm_l16 = t127;
        {
            gm_closure t129 = gm_l1;
            void * t130 = GM_LD(void *, (((uint8_t *)&gm_l16) + 0));
            gm_closure t131 = t129;
            gm_node *ccx132 = t131.cx;
            void * t133 = t131.code;
            gm_b0_1 t134;
            if (GM_PTR_TAGGED(t133)) {
                ((void (*)(void *))GM_PTR_UNTAG(t133))(t130);
            } else {
                t134 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t133)(t131.env, ccx132, NULL, t130);
            }
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_closure t135 = gm_l1;
            void * t136 = GM_LD(void *, (((uint8_t *)&gm_l16) + 16));
            gm_closure t137 = t135;
            gm_node *ccx138 = t137.cx;
            void * t139 = t137.code;
            gm_b0_1 t140;
            if (GM_PTR_TAGGED(t139)) {
                ((void (*)(void *))GM_PTR_UNTAG(t139))(t136);
            } else {
                t140 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t139)(t137.env, ccx138, NULL, t136);
            }
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto me3;
mn125:;
ma10:;
        uint8_t t142 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t142 != ((uint8_t)42ULL)) goto mn141;
        gm_t083kd7b1kkag t143 = GM_LD(gm_t083kd7b1kkag, ((((uint8_t *)t2) + 8) + 8));
        gm_l17 = t143;
        gm_closure t144 = gm_l1;
        void * t145 = GM_LD(void *, (((uint8_t *)&gm_l17) + 0));
        gm_closure t146 = t144;
        gm_node *ccx147 = t146.cx;
        void * t148 = t146.code;
        gm_b0_1 t149;
        if (GM_PTR_TAGGED(t148)) {
            ((void (*)(void *))GM_PTR_UNTAG(t148))(t145);
        } else {
            t149 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t148)(t146.env, ccx147, NULL, t145);
        }
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me3;
mn141:;
ma11:;
        uint8_t t151 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t151 != ((uint8_t)19ULL)) goto mn150;
        void * t152 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 0));
        gm_l18 = t152;
        gm_closure t153 = gm_l1;
        void * t154 = gm_l18;
        gm_closure t155 = t153;
        gm_node *ccx156 = t155.cx;
        void * t157 = t155.code;
        gm_b0_1 t158;
        if (GM_PTR_TAGGED(t157)) {
            ((void (*)(void *))GM_PTR_UNTAG(t157))(t154);
        } else {
            t158 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t157)(t155.env, ccx156, NULL, t154);
        }
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me3;
mn150:;
ma12:;
        uint8_t t160 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t160 != ((uint8_t)51ULL)) goto mn159;
        void * t161 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 8));
        gm_l19 = t161;
        gm_closure t162 = gm_l1;
        void * t163 = gm_l19;
        gm_closure t164 = t162;
        gm_node *ccx165 = t164.cx;
        void * t166 = t164.code;
        gm_b0_1 t167;
        if (GM_PTR_TAGGED(t166)) {
            ((void (*)(void *))GM_PTR_UNTAG(t166))(t163);
        } else {
            t167 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t166)(t164.env, ccx165, NULL, t163);
        }
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me3;
mn159:;
ma13:;
        uint8_t t169 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t169 != ((uint8_t)53ULL)) goto mn168;
        void * t170 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 0));
        gm_l20 = t170;
        void * t171 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 8));
        gm_l21 = t171;
        {
            gm_closure t173 = gm_l1;
            void * t174 = gm_l20;
            gm_closure t175 = t173;
            gm_node *ccx176 = t175.cx;
            void * t177 = t175.code;
            gm_b0_1 t178;
            if (GM_PTR_TAGGED(t177)) {
                ((void (*)(void *))GM_PTR_UNTAG(t177))(t174);
            } else {
                t178 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t177)(t175.env, ccx176, NULL, t174);
            }
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_closure t179 = gm_l1;
            void * t180 = gm_l21;
            gm_closure t181 = t179;
            gm_node *ccx182 = t181.cx;
            void * t183 = t181.code;
            gm_b0_1 t184;
            if (GM_PTR_TAGGED(t183)) {
                ((void (*)(void *))GM_PTR_UNTAG(t183))(t180);
            } else {
                t184 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t183)(t181.env, ccx182, NULL, t180);
            }
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto me3;
mn168:;
ma14:;
        uint8_t t186 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t186 != ((uint8_t)54ULL)) goto mn185;
        void * t187 = GM_LD(void *, ((((uint8_t *)t2) + 8) + 8));
        gm_l22 = t187;
        gm_closure t188 = gm_l1;
        void * t189 = gm_l22;
        gm_closure t190 = t188;
        gm_node *ccx191 = t190.cx;
        void * t192 = t190.code;
        gm_b0_1 t193;
        if (GM_PTR_TAGGED(t192)) {
            ((void (*)(void *))GM_PTR_UNTAG(t192))(t189);
        } else {
            t193 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t192)(t190.env, ccx191, NULL, t189);
        }
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me3;
mn185:;
ma15:;
        uint8_t t195 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t195 != ((uint8_t)20ULL)) goto mn194;
        void * t196 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 0));
        gm_l23 = t196;
        void * t197 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 8));
        gm_l24 = t197;
        {
            gm_closure t199 = gm_l1;
            void * t200 = gm_l23;
            gm_closure t201 = t199;
            gm_node *ccx202 = t201.cx;
            void * t203 = t201.code;
            gm_b0_1 t204;
            if (GM_PTR_TAGGED(t203)) {
                ((void (*)(void *))GM_PTR_UNTAG(t203))(t200);
            } else {
                t204 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t203)(t201.env, ccx202, NULL, t200);
            }
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_closure t205 = gm_l1;
            void * t206 = gm_l24;
            gm_closure t207 = t205;
            gm_node *ccx208 = t207.cx;
            void * t209 = t207.code;
            gm_b0_1 t210;
            if (GM_PTR_TAGGED(t209)) {
                ((void (*)(void *))GM_PTR_UNTAG(t209))(t206);
            } else {
                t210 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t209)(t207.env, ccx208, NULL, t206);
            }
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto me3;
mn194:;
ma16:;
        uint8_t t214 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t214 != ((uint8_t)30ULL)) goto pa213;
        void * t215 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 8));
        gm_l25 = t215;
        void * t216 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 16));
        gm_l26 = t216;
        goto po212;
pa213:;
        uint8_t t218 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t218 != ((uint8_t)31ULL)) goto pa217;
        void * t219 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 8));
        gm_l25 = t219;
        void * t220 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 16));
        gm_l26 = t220;
        goto po212;
pa217:;
        goto mn211;
po212:;
        {
            gm_closure t222 = gm_l1;
            void * t223 = gm_l25;
            gm_closure t224 = t222;
            gm_node *ccx225 = t224.cx;
            void * t226 = t224.code;
            gm_b0_1 t227;
            if (GM_PTR_TAGGED(t226)) {
                ((void (*)(void *))GM_PTR_UNTAG(t226))(t223);
            } else {
                t227 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t226)(t224.env, ccx225, NULL, t223);
            }
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_closure t228 = gm_l1;
            void * t229 = gm_l26;
            gm_closure t230 = t228;
            gm_node *ccx231 = t230.cx;
            void * t232 = t230.code;
            gm_b0_1 t233;
            if (GM_PTR_TAGGED(t232)) {
                ((void (*)(void *))GM_PTR_UNTAG(t232))(t229);
            } else {
                t233 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t232)(t230.env, ccx231, NULL, t229);
            }
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto me3;
mn211:;
ma17:;
        uint8_t t237 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t237 != ((uint8_t)43ULL)) goto pa236;
        void * t238 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 0));
        gm_l29 = t238;
        void * t239 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 8));
        gm_l30 = t239;
        goto po235;
pa236:;
        uint8_t t241 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t241 != ((uint8_t)45ULL)) goto pa240;
        void * t242 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 0));
        gm_l29 = t242;
        void * t243 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 8));
        gm_l30 = t243;
        goto po235;
pa240:;
        goto mn234;
po235:;
        {
            gm_closure t245 = gm_l1;
            void * t246 = gm_l29;
            gm_closure t247 = t245;
            gm_node *ccx248 = t247.cx;
            void * t249 = t247.code;
            gm_b0_1 t250;
            if (GM_PTR_TAGGED(t249)) {
                ((void (*)(void *))GM_PTR_UNTAG(t249))(t246);
            } else {
                t250 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t249)(t247.env, ccx248, NULL, t246);
            }
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_closure t251 = gm_l1;
            void * t252 = gm_l30;
            gm_closure t253 = t251;
            gm_node *ccx254 = t253.cx;
            void * t255 = t253.code;
            gm_b0_1 t256;
            if (GM_PTR_TAGGED(t255)) {
                ((void (*)(void *))GM_PTR_UNTAG(t255))(t252);
            } else {
                t256 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t255)(t253.env, ccx254, NULL, t252);
            }
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto me3;
mn234:;
ma18:;
        uint8_t t258 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t258 != ((uint8_t)44ULL)) goto mn257;
        void * t259 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 0));
        gm_l33 = t259;
        gm_b16_8 t260 = GM_LD(gm_b16_8, (((((uint8_t *)t2) + 8) + 8) + 8));
        gm_l34 = t260;
        {
            gm_closure t262 = gm_l1;
            void * t263 = gm_l33;
            gm_closure t264 = t262;
            gm_node *ccx265 = t264.cx;
            void * t266 = t264.code;
            gm_b0_1 t267;
            if (GM_PTR_TAGGED(t266)) {
                ((void (*)(void *))GM_PTR_UNTAG(t266))(t263);
            } else {
                t267 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t266)(t264.env, ccx265, NULL, t263);
            }
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            {
                gm_b16_8 t269 = gm_l34;
                gm_l35 = t269;
            }
            {
                gm_l36 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t271 = gm_l36;
                    uint64_t t272 = GM_LD(uintptr_t, ((uint8_t *)&gm_l35) + 8);
                    uint8_t t273 = (t271 < t272);
                    if (!t273) goto lb270;
                    {
                        uint64_t t276 = gm_l36;
                        uint64_t t277 = (uintptr_t)t276;
                        uint64_t t278 = GM_LD(uintptr_t, ((uint8_t *)&gm_l35) + 8);
                        gm_tbbqefuhv2r8h t281 = GM_LD(gm_tbbqefuhv2r8h, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l35)) + t277 * 24));
                        gm_l37 = t281;
                        {
                            void * t283 = GM_LD(void *, (((uint8_t *)&gm_l37) + 0));
                            gm_closure t284 = gm_l1;
                            gm_f_pat_children_e3peovr(t283, t284);
                            if (gm_unw.kind) {
                                goto gm_ll274;
                            }
                        }
                        {
                            void * t286 = GM_LD(void *, (((uint8_t *)&gm_l37) + 8));
                            void * t287 = t286;
                            if (!(GM_LD(void *, ((uint8_t *)&t287)) != 0)) goto ie285;
                            void * t288 = GM_LD(void *, ((uint8_t *)&t287));
                            gm_l38 = t288;
                            {
                                gm_closure t290 = gm_l1;
                                void * t291 = gm_l38;
                                gm_closure t292 = t290;
                                gm_node *ccx293 = t292.cx;
                                void * t294 = t292.code;
                                gm_b0_1 t295;
                                if (GM_PTR_TAGGED(t294)) {
                                    ((void (*)(void *))GM_PTR_UNTAG(t294))(t291);
                                } else {
                                    t295 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t294)(t292.env, ccx293, NULL, t291);
                                }
                                if (gm_unw.kind) {
                                    goto gm_ll274;
                                }
                            }
                            goto ix285;
ie285:;
ix285:;
                        }
                        {
                            gm_closure t296 = gm_l1;
                            void * t297 = GM_LD(void *, (((uint8_t *)&gm_l37) + 16));
                            gm_closure t298 = t296;
                            gm_node *ccx299 = t298.cx;
                            void * t300 = t298.code;
                            gm_b0_1 t301;
                            if (GM_PTR_TAGGED(t300)) {
                                ((void (*)(void *))GM_PTR_UNTAG(t300))(t297);
                            } else {
                                t301 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t300)(t298.env, ccx299, NULL, t297);
                            }
                            if (gm_unw.kind) {
                                goto gm_ll274;
                            }
                        }
                        goto me279;
mn280:;
                        gm_trap("no match arm matched the scrutinee", gm_srcod98slk3vnra, 599, 13);
me279:;
                    }
lc270:;
                    {
                        uint64_t t303 = gm_l36;
                        uint64_t r304; bool o305 = __builtin_add_overflow(t303, ((uint64_t)1ULL), &r304);
                        uint64_t t306 = r304;
                        gm_l36 = t306;
                    }
                }
gm_ll274:;
                if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb270; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc270; }
                }
                goto gm_lad;
lb270:;
            }
        }
        goto me3;
mn257:;
ma19:;
        uint8_t t308 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t308 != ((uint8_t)58ULL)) goto mn307;
        gm_t710lsrq1m8gk t309 = GM_LD(gm_t710lsrq1m8gk, ((((uint8_t *)t2) + 8) + 8));
        F.l39 = t309;
        {
            {
                gm_b16_8 t312 = GM_LD(gm_b16_8, (((uint8_t *)&F.l39) + 8));
                gm_l40 = t312;
            }
            {
                gm_l41 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t314 = gm_l41;
                    uint64_t t315 = GM_LD(uintptr_t, ((uint8_t *)&gm_l40) + 8);
                    uint8_t t316 = (t314 < t315);
                    if (!t316) goto lb313;
                    {
                        uint64_t t319 = gm_l41;
                        uint64_t t320 = (uintptr_t)t319;
                        uint64_t t321 = GM_LD(uintptr_t, ((uint8_t *)&gm_l40) + 8);
                        gm_t4lnf71mruqpt t324 = GM_LD(gm_t4lnf71mruqpt, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l40)) + t320 * 32));
                        gm_l42 = t324;
                        {
                            uint8_t t328 = GM_LD(uint8_t, (((uint8_t *)&gm_l42) + 0));
                            if (t328 != ((uint8_t)0ULL)) goto mn327;
                            void * t329 = GM_LD(void *, (((uint8_t *)&gm_l42) + 8));
                            gm_l43 = t329;
                            gm_closure t330 = gm_l1;
                            void * t331 = gm_l43;
                            gm_closure t332 = t330;
                            gm_node *ccx333 = t332.cx;
                            void * t334 = t332.code;
                            gm_b0_1 t335;
                            if (GM_PTR_TAGGED(t334)) {
                                ((void (*)(void *))GM_PTR_UNTAG(t334))(t331);
                            } else {
                                t335 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t334)(t332.env, ccx333, NULL, t331);
                            }
                            if (gm_unw.kind) {
                                goto gm_ll317;
                            }
                            goto me326;
mn327:;
                            uint8_t t337 = GM_LD(uint8_t, (((uint8_t *)&gm_l42) + 0));
                            if (t337 != ((uint8_t)1ULL)) goto mn336;
                            void * t338 = GM_LD(void *, ((((uint8_t *)&gm_l42) + 8) + 16));
                            gm_l44 = t338;
                            gm_closure t339 = gm_l1;
                            void * t340 = gm_l44;
                            gm_closure t341 = t339;
                            gm_node *ccx342 = t341.cx;
                            void * t343 = t341.code;
                            gm_b0_1 t344;
                            if (GM_PTR_TAGGED(t343)) {
                                ((void (*)(void *))GM_PTR_UNTAG(t343))(t340);
                            } else {
                                t344 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t343)(t341.env, ccx342, NULL, t340);
                            }
                            if (gm_unw.kind) {
                                goto gm_ll317;
                            }
                            goto me326;
mn336:;
                            gm_trap("no match arm matched the scrutinee", gm_srcod98slk3vnra, 607, 17);
me326:;
                        }
                        goto me322;
mn323:;
                        gm_trap("no match arm matched the scrutinee", gm_srcod98slk3vnra, 606, 13);
me322:;
                    }
lc313:;
                    {
                        uint64_t t346 = gm_l41;
                        uint64_t r347; bool o348 = __builtin_add_overflow(t346, ((uint64_t)1ULL), &r347);
                        uint64_t t349 = r347;
                        gm_l41 = t349;
                    }
                }
gm_ll317:;
                if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb313; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc313; }
                }
                goto gm_lad;
lb313:;
            }
        }
        {
            void * t351 = GM_LD(void *, (((uint8_t *)&F.l39) + 24));
            void * t352 = t351;
            if (!(GM_LD(void *, ((uint8_t *)&t352)) != 0)) goto ie350;
            void * t353 = GM_LD(void *, ((uint8_t *)&t352));
            gm_l45 = t353;
            {
                gm_closure t355 = gm_l1;
                void * t356 = gm_l45;
                gm_closure t357 = t355;
                gm_node *ccx358 = t357.cx;
                void * t359 = t357.code;
                gm_b0_1 t360;
                if (GM_PTR_TAGGED(t359)) {
                    ((void (*)(void *))GM_PTR_UNTAG(t359))(t356);
                } else {
                    t360 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t359)(t357.env, ccx358, NULL, t356);
                }
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix350;
ie350:;
ix350:;
        }
        {
            gm_closure t361 = gm_l1;
            void * t362 = GM_LD(void *, (((uint8_t *)&F.l39) + 32));
            gm_closure t363 = t361;
            gm_node *ccx364 = t363.cx;
            void * t365 = t363.code;
            gm_b0_1 t366;
            if (GM_PTR_TAGGED(t365)) {
                ((void (*)(void *))GM_PTR_UNTAG(t365))(t362);
            } else {
                t366 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t365)(t363.env, ccx364, NULL, t362);
            }
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto me3;
mn307:;
ma20:;
        uint8_t t368 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t368 != ((uint8_t)33ULL)) goto mn367;
        gm_b16_8 t369 = GM_LD(gm_b16_8, (((((uint8_t *)t2) + 8) + 8) + 16));
        gm_l46 = t369;
        {
            gm_l48 = ((uint64_t)0ULL);
        }
        {
            uint64_t t371 = GM_LD(uintptr_t, ((uint8_t *)&gm_l46) + 8);
            gm_l47 = t371;
        }
        {
            for (;;) {
                uint64_t t373 = gm_l48;
                uint64_t t374 = gm_l47;
                uint8_t t375 = (t373 < t374);
                if (!t375) goto lb372;
                {
                    uint64_t t378 = gm_l48;
                    uint64_t t379 = t378;
                    uint64_t t382 = GM_LD(uint64_t, ((uint8_t *)&t379));
                    gm_l49 = t382;
                    {
                        gm_closure t384 = gm_l1;
                        uint64_t t385 = gm_l49;
                        uint64_t t386 = (uintptr_t)t385;
                        uint64_t t387 = GM_LD(uintptr_t, ((uint8_t *)&gm_l46) + 8);
                        if (t386 >= (uintptr_t)(t387)) gm_trap_index(t386, t387, gm_srcod98slk3vnra, 615, 56);
                        void * t388 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l46)) + t386 * 224);
                        gm_closure t389 = t384;
                        gm_node *ccx390 = t389.cx;
                        void * t391 = t389.code;
                        gm_b0_1 t392;
                        if (GM_PTR_TAGGED(t391)) {
                            ((void (*)(void *))GM_PTR_UNTAG(t391))(t388);
                        } else {
                            t392 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t391)(t389.env, ccx390, NULL, t388);
                        }
                        if (gm_unw.kind) {
                            goto gm_ll376;
                        }
                    }
                    goto me380;
mn381:;
                    gm_trap("no match arm matched the scrutinee", gm_srcod98slk3vnra, 615, 30);
me380:;
                }
lc372:;
                {
                    uint64_t t394 = gm_l48;
                    uint64_t r395; bool o396 = __builtin_add_overflow(t394, ((uint64_t)1ULL), &r395);
                    uint64_t t397 = r395;
                    gm_l48 = t397;
                }
            }
gm_ll376:;
            if (gm_unw.frame == &F.h && gm_unw.label == 2) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb372; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc372; }
            }
            goto gm_lad;
lb372:;
        }
        goto me3;
mn367:;
ma21:;
        uint8_t t399 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t399 != ((uint8_t)57ULL)) goto mn398;
        gm_tandvbt63uk7n t400 = GM_LD(gm_tandvbt63uk7n, ((((uint8_t *)t2) + 8) + 8));
        gm_l50 = t400;
        {
            gm_l52 = ((uint64_t)0ULL);
        }
        {
            uint64_t t402 = GM_LD(uintptr_t, (((uint8_t *)&gm_l50) + 8) + 8);
            gm_l51 = t402;
        }
        {
            for (;;) {
                uint64_t t404 = gm_l52;
                uint64_t t405 = gm_l51;
                uint8_t t406 = (t404 < t405);
                if (!t406) goto lb403;
                {
                    uint64_t t409 = gm_l52;
                    uint64_t t410 = t409;
                    uint64_t t413 = GM_LD(uint64_t, ((uint8_t *)&t410));
                    gm_l53 = t413;
                    {
                        gm_closure t415 = gm_l1;
                        uint64_t t416 = gm_l53;
                        uint64_t t417 = (uintptr_t)t416;
                        uint64_t t418 = GM_LD(uintptr_t, (((uint8_t *)&gm_l50) + 8) + 8);
                        if (t417 >= (uintptr_t)(t418)) gm_trap_index(t417, t418, gm_srcod98slk3vnra, 616, 56);
                        void * t419 = (void *)((uint8_t *)GM_LD(void *, (((uint8_t *)&gm_l50) + 8)) + t417 * 224);
                        gm_closure t420 = t415;
                        gm_node *ccx421 = t420.cx;
                        void * t422 = t420.code;
                        gm_b0_1 t423;
                        if (GM_PTR_TAGGED(t422)) {
                            ((void (*)(void *))GM_PTR_UNTAG(t422))(t419);
                        } else {
                            t423 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t422)(t420.env, ccx421, NULL, t419);
                        }
                        if (gm_unw.kind) {
                            goto gm_ll407;
                        }
                    }
                    goto me411;
mn412:;
                    gm_trap("no match arm matched the scrutinee", gm_srcod98slk3vnra, 616, 28);
me411:;
                }
lc403:;
                {
                    uint64_t t425 = gm_l52;
                    uint64_t r426; bool o427 = __builtin_add_overflow(t425, ((uint64_t)1ULL), &r426);
                    uint64_t t428 = r426;
                    gm_l52 = t428;
                }
            }
gm_ll407:;
            if (gm_unw.frame == &F.h && gm_unw.label == 3) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb403; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc403; }
            }
            goto gm_lad;
lb403:;
        }
        goto me3;
mn398:;
ma22:;
        uint8_t t430 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t430 != ((uint8_t)34ULL)) goto mn429;
        gm_tsth9qu1dv0g3 t431 = GM_LD(gm_tsth9qu1dv0g3, (((((uint8_t *)t2) + 8) + 8) + 0));
        F.l54 = t431;
        gm_b16_8 t432 = GM_LD(gm_b16_8, (((((uint8_t *)t2) + 8) + 8) + 40));
        gm_l55 = t432;
        {
            uint8_t t435 = GM_LD(uint8_t, (((uint8_t *)&F.l54) + 0));
            if (t435 != ((uint8_t)2ULL)) goto ie434;
            void * t436 = GM_LD(void *, (((uint8_t *)&F.l54) + 8));
            gm_l56 = t436;
            {
                gm_closure t438 = gm_l1;
                void * t439 = gm_l56;
                gm_closure t440 = t438;
                gm_node *ccx441 = t440.cx;
                void * t442 = t440.code;
                gm_b0_1 t443;
                if (GM_PTR_TAGGED(t442)) {
                    ((void (*)(void *))GM_PTR_UNTAG(t442))(t439);
                } else {
                    t443 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t442)(t440.env, ccx441, NULL, t439);
                }
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix434;
ie434:;
ix434:;
        }
        {
            {
                gm_l58 = ((uint64_t)0ULL);
            }
            {
                uint64_t t445 = GM_LD(uintptr_t, ((uint8_t *)&gm_l55) + 8);
                gm_l57 = t445;
            }
            {
                for (;;) {
                    uint64_t t447 = gm_l58;
                    uint64_t t448 = gm_l57;
                    uint8_t t449 = (t447 < t448);
                    if (!t449) goto lb446;
                    {
                        uint64_t t452 = gm_l58;
                        uint64_t t453 = t452;
                        uint64_t t456 = GM_LD(uint64_t, ((uint8_t *)&t453));
                        gm_l59 = t456;
                        {
                            gm_closure t458 = gm_l1;
                            uint64_t t459 = gm_l59;
                            uint64_t t460 = (uintptr_t)t459;
                            uint64_t t461 = GM_LD(uintptr_t, ((uint8_t *)&gm_l55) + 8);
                            if (t460 >= (uintptr_t)(t461)) gm_trap_index(t460, t461, gm_srcod98slk3vnra, 619, 39);
                            void * t462 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l55)) + t460 * 224);
                            gm_closure t463 = t458;
                            gm_node *ccx464 = t463.cx;
                            void * t465 = t463.code;
                            gm_b0_1 t466;
                            if (GM_PTR_TAGGED(t465)) {
                                ((void (*)(void *))GM_PTR_UNTAG(t465))(t462);
                            } else {
                                t466 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t465)(t463.env, ccx464, NULL, t462);
                            }
                            if (gm_unw.kind) {
                                goto gm_ll450;
                            }
                        }
                        goto me454;
mn455:;
                        gm_trap("no match arm matched the scrutinee", gm_srcod98slk3vnra, 619, 13);
me454:;
                    }
lc446:;
                    {
                        uint64_t t468 = gm_l58;
                        uint64_t r469; bool o470 = __builtin_add_overflow(t468, ((uint64_t)1ULL), &r469);
                        uint64_t t471 = r469;
                        gm_l58 = t471;
                    }
                }
gm_ll450:;
                if (gm_unw.frame == &F.h && gm_unw.label == 4) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb446; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc446; }
                }
                goto gm_lad;
lb446:;
            }
        }
        goto me3;
mn429:;
ma23:;
        uint8_t t475 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t475 != ((uint8_t)35ULL)) goto pa474;
        gm_b16_8 t476 = GM_LD(gm_b16_8, ((((uint8_t *)t2) + 8) + 8));
        gm_l60 = t476;
        goto po473;
pa474:;
        uint8_t t478 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t478 != ((uint8_t)36ULL)) goto pa477;
        gm_b16_8 t479 = GM_LD(gm_b16_8, ((((uint8_t *)t2) + 8) + 8));
        gm_l60 = t479;
        goto po473;
pa477:;
        goto mn472;
po473:;
        {
            gm_l63 = ((uint64_t)0ULL);
        }
        {
            uint64_t t481 = GM_LD(uintptr_t, ((uint8_t *)&gm_l60) + 8);
            gm_l62 = t481;
        }
        {
            for (;;) {
                uint64_t t483 = gm_l63;
                uint64_t t484 = gm_l62;
                uint8_t t485 = (t483 < t484);
                if (!t485) goto lb482;
                {
                    uint64_t t488 = gm_l63;
                    uint64_t t489 = t488;
                    uint64_t t492 = GM_LD(uint64_t, ((uint8_t *)&t489));
                    gm_l64 = t492;
                    {
                        gm_closure t494 = gm_l1;
                        uint64_t t495 = gm_l64;
                        uint64_t t496 = (uintptr_t)t495;
                        uint64_t t497 = GM_LD(uintptr_t, ((uint8_t *)&gm_l60) + 8);
                        if (t496 >= (uintptr_t)(t497)) gm_trap_index(t496, t497, gm_srcod98slk3vnra, 621, 63);
                        void * t498 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l60)) + t496 * 224);
                        gm_closure t499 = t494;
                        gm_node *ccx500 = t499.cx;
                        void * t501 = t499.code;
                        gm_b0_1 t502;
                        if (GM_PTR_TAGGED(t501)) {
                            ((void (*)(void *))GM_PTR_UNTAG(t501))(t498);
                        } else {
                            t502 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t501)(t499.env, ccx500, NULL, t498);
                        }
                        if (gm_unw.kind) {
                            goto gm_ll486;
                        }
                    }
                    goto me490;
mn491:;
                    gm_trap("no match arm matched the scrutinee", gm_srcod98slk3vnra, 621, 39);
me490:;
                }
lc482:;
                {
                    uint64_t t504 = gm_l63;
                    uint64_t r505; bool o506 = __builtin_add_overflow(t504, ((uint64_t)1ULL), &r505);
                    uint64_t t507 = r505;
                    gm_l63 = t507;
                }
            }
gm_ll486:;
            if (gm_unw.frame == &F.h && gm_unw.label == 5) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb482; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc482; }
            }
            goto gm_lad;
lb482:;
        }
        goto me3;
mn472:;
ma24:;
        uint8_t t509 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t509 != ((uint8_t)38ULL)) goto mn508;
        gm_ttggq7ng4kp3p t510 = GM_LD(gm_ttggq7ng4kp3p, ((((uint8_t *)t2) + 8) + 8));
        gm_l65 = t510;
        void * t512 = GM_LD(void *, (((uint8_t *)&gm_l65) + 8));
        void * t513 = t512;
        if (!(GM_LD(void *, ((uint8_t *)&t513)) != 0)) goto ie511;
        void * t514 = GM_LD(void *, ((uint8_t *)&t513));
        gm_l66 = t514;
        {
            gm_closure t516 = gm_l1;
            void * t517 = gm_l66;
            gm_closure t518 = t516;
            gm_node *ccx519 = t518.cx;
            void * t520 = t518.code;
            gm_b0_1 t521;
            if (GM_PTR_TAGGED(t520)) {
                ((void (*)(void *))GM_PTR_UNTAG(t520))(t517);
            } else {
                t521 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t520)(t518.env, ccx519, NULL, t517);
            }
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix511;
ie511:;
ix511:;
        goto me3;
mn508:;
ma25:;
        uint8_t t523 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t523 != ((uint8_t)46ULL)) goto mn522;
        void * t524 = GM_LD(void *, ((((uint8_t *)t2) + 8) + 8));
        gm_l67 = t524;
        void * t525 = gm_l67;
        gm_closure t526 = gm_l1;
        gm_f_block_children_r2qinti(t525, t526);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me3;
mn522:;
ma26:;
        uint8_t t528 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t528 != ((uint8_t)47ULL)) goto mn527;
        gm_trl2khmuphncd t529 = GM_LD(gm_trl2khmuphncd, ((((uint8_t *)t2) + 8) + 8));
        gm_l68 = t529;
        {
            gm_b16_8 t531 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l68) + 0));
            gm_closure t532 = gm_l1;
            gm_f_conds_children_3kg6846(t531, t532);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_closure t533 = gm_l1;
            void * t534 = GM_LD(void *, (((uint8_t *)&gm_l68) + 16));
            gm_closure t535 = t533;
            gm_node *ccx536 = t535.cx;
            void * t537 = t535.code;
            gm_b0_1 t538;
            if (GM_PTR_TAGGED(t537)) {
                ((void (*)(void *))GM_PTR_UNTAG(t537))(t534);
            } else {
                t538 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t537)(t535.env, ccx536, NULL, t534);
            }
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t540 = GM_LD(void *, (((uint8_t *)&gm_l68) + 24));
            void * t541 = t540;
            if (!(GM_LD(void *, ((uint8_t *)&t541)) != 0)) goto ie539;
            void * t542 = GM_LD(void *, ((uint8_t *)&t541));
            gm_l69 = t542;
            {
                gm_closure t544 = gm_l1;
                void * t545 = gm_l69;
                gm_closure t546 = t544;
                gm_node *ccx547 = t546.cx;
                void * t548 = t546.code;
                gm_b0_1 t549;
                if (GM_PTR_TAGGED(t548)) {
                    ((void (*)(void *))GM_PTR_UNTAG(t548))(t545);
                } else {
                    t549 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t548)(t546.env, ccx547, NULL, t545);
                }
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix539;
ie539:;
ix539:;
        }
        goto me3;
mn527:;
ma27:;
        uint8_t t551 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t551 != ((uint8_t)48ULL)) goto mn550;
        gm_t710lsrq1m8gk t552 = GM_LD(gm_t710lsrq1m8gk, ((((uint8_t *)t2) + 8) + 8));
        F.l70 = t552;
        {
            gm_b16_8 t554 = GM_LD(gm_b16_8, (((uint8_t *)&F.l70) + 8));
            gm_closure t555 = gm_l1;
            gm_f_conds_children_3kg6846(t554, t555);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t556 = GM_LD(void *, (((uint8_t *)&F.l70) + 24));
            gm_closure t557 = gm_l1;
            gm_f_block_children_r2qinti(t556, t557);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t559 = GM_LD(void *, (((uint8_t *)&F.l70) + 32));
            void * t560 = t559;
            if (!(GM_LD(void *, ((uint8_t *)&t560)) != 0)) goto ie558;
            void * t561 = GM_LD(void *, ((uint8_t *)&t560));
            gm_l71 = t561;
            {
                gm_closure t563 = gm_l1;
                void * t564 = gm_l71;
                gm_closure t565 = t563;
                gm_node *ccx566 = t565.cx;
                void * t567 = t565.code;
                gm_b0_1 t568;
                if (GM_PTR_TAGGED(t567)) {
                    ((void (*)(void *))GM_PTR_UNTAG(t567))(t564);
                } else {
                    t568 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t567)(t565.env, ccx566, NULL, t564);
                }
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix558;
ie558:;
ix558:;
        }
        goto me3;
mn550:;
ma28:;
        uint8_t t570 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t570 != ((uint8_t)49ULL)) goto mn569;
        gm_teo9d4mncjfdu t571 = GM_LD(gm_teo9d4mncjfdu, ((((uint8_t *)t2) + 8) + 8));
        gm_l72 = t571;
        void * t573 = GM_LD(void *, (((uint8_t *)&gm_l72) + 16));
        void * t574 = t573;
        if (!(GM_LD(void *, ((uint8_t *)&t574)) != 0)) goto ie572;
        void * t575 = GM_LD(void *, ((uint8_t *)&t574));
        gm_l73 = t575;
        {
            gm_closure t577 = gm_l1;
            void * t578 = gm_l73;
            gm_closure t579 = t577;
            gm_node *ccx580 = t579.cx;
            void * t581 = t579.code;
            gm_b0_1 t582;
            if (GM_PTR_TAGGED(t581)) {
                ((void (*)(void *))GM_PTR_UNTAG(t581))(t578);
            } else {
                t582 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t581)(t579.env, ccx580, NULL, t578);
            }
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix572;
ie572:;
ix572:;
        goto me3;
mn569:;
ma29:;
        uint8_t t584 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t584 != ((uint8_t)55ULL)) goto mn583;
        gm_tntn2tf15puib t585 = GM_LD(gm_tntn2tf15puib, ((((uint8_t *)t2) + 8) + 8));
        F.l74 = t585;
        {
            uint8_t t588 = GM_LD(uint8_t, ((((uint8_t *)&F.l74) + 0) + 0));
            if (t588 != ((uint8_t)1ULL)) goto ie587;
            void * t589 = GM_LD(void *, ((((uint8_t *)&F.l74) + 0) + 8));
            gm_l75 = t589;
            {
                gm_closure t591 = gm_l1;
                void * t592 = gm_l75;
                gm_closure t593 = t591;
                gm_node *ccx594 = t593.cx;
                void * t595 = t593.code;
                gm_b0_1 t596;
                if (GM_PTR_TAGGED(t595)) {
                    ((void (*)(void *))GM_PTR_UNTAG(t595))(t592);
                } else {
                    t596 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t595)(t593.env, ccx594, NULL, t592);
                }
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix587;
ie587:;
ix587:;
        }
        {
            {
                gm_l77 = ((uint64_t)0ULL);
            }
            {
                uint64_t t598 = GM_LD(uintptr_t, (((uint8_t *)&F.l74) + 72) + 8);
                gm_l76 = t598;
            }
            {
                for (;;) {
                    uint64_t t600 = gm_l77;
                    uint64_t t601 = gm_l76;
                    uint8_t t602 = (t600 < t601);
                    if (!t602) goto lb599;
                    {
                        uint64_t t605 = gm_l77;
                        uint64_t t606 = t605;
                        uint64_t t609 = GM_LD(uint64_t, ((uint8_t *)&t606));
                        gm_l78 = t609;
                        {
                            gm_closure t611 = gm_l1;
                            uint64_t t612 = gm_l78;
                            uint64_t t613 = (uintptr_t)t612;
                            uint64_t t614 = GM_LD(uintptr_t, (((uint8_t *)&F.l74) + 72) + 8);
                            if (t613 >= (uintptr_t)(t614)) gm_trap_index(t613, t614, gm_srcod98slk3vnra, 637, 41);
                            void * t615 = (void *)((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l74) + 72)) + t613 * 224);
                            gm_closure t616 = t611;
                            gm_node *ccx617 = t616.cx;
                            void * t618 = t616.code;
                            gm_b0_1 t619;
                            if (GM_PTR_TAGGED(t618)) {
                                ((void (*)(void *))GM_PTR_UNTAG(t618))(t615);
                            } else {
                                t619 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t618)(t616.env, ccx617, NULL, t615);
                            }
                            if (gm_unw.kind) {
                                goto gm_ll603;
                            }
                        }
                        goto me607;
mn608:;
                        gm_trap("no match arm matched the scrutinee", gm_srcod98slk3vnra, 637, 13);
me607:;
                    }
lc599:;
                    {
                        uint64_t t621 = gm_l77;
                        uint64_t r622; bool o623 = __builtin_add_overflow(t621, ((uint64_t)1ULL), &r622);
                        uint64_t t624 = r622;
                        gm_l77 = t624;
                    }
                }
gm_ll603:;
                if (gm_unw.frame == &F.h && gm_unw.label == 6) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb599; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc599; }
                }
                goto gm_lad;
lb599:;
            }
        }
        goto me3;
mn583:;
ma30:;
        uint8_t t626 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t626 != ((uint8_t)56ULL)) goto mn625;
        gm_t9i38ih07bojq t627 = GM_LD(gm_t9i38ih07bojq, ((((uint8_t *)t2) + 8) + 8));
        F.l79 = t627;
        {
            uint8_t t630 = GM_LD(uint8_t, ((((uint8_t *)&F.l79) + 0) + 0));
            if (t630 != ((uint8_t)1ULL)) goto ie629;
            void * t631 = GM_LD(void *, ((((uint8_t *)&F.l79) + 0) + 8));
            gm_l80 = t631;
            {
                gm_closure t633 = gm_l1;
                void * t634 = gm_l80;
                gm_closure t635 = t633;
                gm_node *ccx636 = t635.cx;
                void * t637 = t635.code;
                gm_b0_1 t638;
                if (GM_PTR_TAGGED(t637)) {
                    ((void (*)(void *))GM_PTR_UNTAG(t637))(t634);
                } else {
                    t638 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t637)(t635.env, ccx636, NULL, t634);
                }
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix629;
ie629:;
ix629:;
        }
        {
            gm_closure t639 = gm_l1;
            void * t640 = GM_LD(void *, (((uint8_t *)&F.l79) + 40));
            gm_closure t641 = t639;
            gm_node *ccx642 = t641.cx;
            void * t643 = t641.code;
            gm_b0_1 t644;
            if (GM_PTR_TAGGED(t643)) {
                ((void (*)(void *))GM_PTR_UNTAG(t643))(t640);
            } else {
                t644 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t643)(t641.env, ccx642, NULL, t640);
            }
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t646 = GM_LD(void *, (((uint8_t *)&F.l79) + 48));
            void * t647 = t646;
            if (!(GM_LD(void *, ((uint8_t *)&t647)) != 0)) goto ie645;
            void * t648 = GM_LD(void *, ((uint8_t *)&t647));
            gm_l81 = t648;
            {
                gm_closure t650 = gm_l1;
                void * t651 = gm_l81;
                gm_closure t652 = t650;
                gm_node *ccx653 = t652.cx;
                void * t654 = t652.code;
                gm_b0_1 t655;
                if (GM_PTR_TAGGED(t654)) {
                    ((void (*)(void *))GM_PTR_UNTAG(t654))(t651);
                } else {
                    t655 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t654)(t652.env, ccx653, NULL, t651);
                }
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix645;
ie645:;
ix645:;
        }
        goto me3;
mn625:;
ma31:;
        goto me3;
mn656:;
ma32:;
        gm_trap("no match arm matched the scrutinee", gm_srcod98slk3vnra, 563, 5);
me3:;
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
__attribute__((aligned(4))) void gm_f_for_each_subpat_arlrtrm(void * a0, gm_closure a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_for_each_subpat_arlrtrm F;
    void * gm_l0; /* p */
    gm_closure gm_l1; /* f */
    void * gm_l2; /* q */
    void * gm_l3; /* q */
    void * gm_l4; /* q */
    void * gm_l5; /* q */
    void * gm_l6; /* x */
    gm_b16_8 gm_l7; /* fs */
    gm_b16_8 gm_l8; /* <tmp> */
    uint64_t gm_l9; /* <tmp> */
    gm_ttggq7ng4kp3p gm_l10; /* x */
    gm_b16_8 gm_l11; /* ps */
    gm_b16_8 gm_l12; /* <tmp> */
    uint64_t gm_l13; /* <tmp> */
    void * gm_l14; /* q */
    GM_STACK_CHECK(&F, 721u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        uint8_t t10 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        switch (t10) { case ((uint8_t)0ULL): goto ma8; case ((uint8_t)1ULL): goto ma8; case ((uint8_t)2ULL): goto ma8; case ((uint8_t)3ULL): goto ma8; case ((uint8_t)4ULL): goto ma8; case ((uint8_t)5ULL): goto ma4; case ((uint8_t)6ULL): goto ma4; case ((uint8_t)7ULL): goto ma4; case ((uint8_t)8ULL): goto ma5; case ((uint8_t)9ULL): goto ma6; case ((uint8_t)10ULL): goto ma7; default: goto ma8; }
ma4:;
        uint8_t t14 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t14 != ((uint8_t)5ULL)) goto pa13;
        void * t15 = GM_LD(void *, ((((uint8_t *)t2) + 8) + 8));
        gm_l2 = t15;
        goto po12;
pa13:;
        uint8_t t17 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t17 != ((uint8_t)6ULL)) goto pa16;
        void * t18 = GM_LD(void *, ((((uint8_t *)t2) + 8) + 8));
        gm_l2 = t18;
        goto po12;
pa16:;
        uint8_t t20 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t20 != ((uint8_t)7ULL)) goto pa19;
        void * t21 = GM_LD(void *, ((((uint8_t *)t2) + 8) + 8));
        gm_l2 = t21;
        goto po12;
pa19:;
        goto mn11;
po12:;
        gm_closure t22 = gm_l1;
        void * t23 = gm_l2;
        gm_closure t24 = t22;
        gm_node *ccx25 = t24.cx;
        void * t26 = t24.code;
        gm_b0_1 t27;
        if (GM_PTR_TAGGED(t26)) {
            ((void (*)(void *))GM_PTR_UNTAG(t26))(t23);
        } else {
            t27 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t26)(t24.env, ccx25, NULL, t23);
        }
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me3;
mn11:;
ma5:;
        uint8_t t29 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t29 != ((uint8_t)8ULL)) goto mn28;
        void * t30 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 8));
        gm_l5 = t30;
        void * t32 = gm_l5;
        void * t33 = t32;
        if (!(GM_LD(void *, ((uint8_t *)&t33)) != 0)) goto ie31;
        void * t34 = GM_LD(void *, ((uint8_t *)&t33));
        gm_l6 = t34;
        {
            gm_closure t36 = gm_l1;
            void * t37 = gm_l6;
            gm_closure t38 = t36;
            gm_node *ccx39 = t38.cx;
            void * t40 = t38.code;
            gm_b0_1 t41;
            if (GM_PTR_TAGGED(t40)) {
                ((void (*)(void *))GM_PTR_UNTAG(t40))(t37);
            } else {
                t41 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t40)(t38.env, ccx39, NULL, t37);
            }
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix31;
ie31:;
ix31:;
        goto me3;
mn28:;
ma6:;
        uint8_t t43 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t43 != ((uint8_t)9ULL)) goto mn42;
        gm_b16_8 t44 = GM_LD(gm_b16_8, ((((uint8_t *)t2) + 8) + 8));
        gm_l7 = t44;
        {
            gm_b16_8 t46 = gm_l7;
            gm_l8 = t46;
        }
        {
            gm_l9 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t48 = gm_l9;
                uint64_t t49 = GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8);
                uint8_t t50 = (t48 < t49);
                if (!t50) goto lb47;
                {
                    uint64_t t53 = gm_l9;
                    uint64_t t54 = (uintptr_t)t53;
                    uint64_t t55 = GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8);
                    gm_ttggq7ng4kp3p t58 = GM_LD(gm_ttggq7ng4kp3p, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l8)) + t54 * 16));
                    gm_l10 = t58;
                    {
                        gm_closure t60 = gm_l1;
                        void * t61 = GM_LD(void *, (((uint8_t *)&gm_l10) + 8));
                        gm_closure t62 = t60;
                        gm_node *ccx63 = t62.cx;
                        void * t64 = t62.code;
                        gm_b0_1 t65;
                        if (GM_PTR_TAGGED(t64)) {
                            ((void (*)(void *))GM_PTR_UNTAG(t64))(t61);
                        } else {
                            t65 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t64)(t62.env, ccx63, NULL, t61);
                        }
                        if (gm_unw.kind) {
                            goto gm_ll51;
                        }
                    }
                    goto me56;
mn57:;
                    gm_trap("no match arm matched the scrutinee", gm_srcod98slk3vnra, 705, 24);
me56:;
                }
lc47:;
                {
                    uint64_t t67 = gm_l9;
                    uint64_t r68; bool o69 = __builtin_add_overflow(t67, ((uint64_t)1ULL), &r68);
                    uint64_t t70 = r68;
                    gm_l9 = t70;
                }
            }
gm_ll51:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb47; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc47; }
            }
            goto gm_lad;
lb47:;
        }
        goto me3;
mn42:;
ma7:;
        uint8_t t72 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t72 != ((uint8_t)10ULL)) goto mn71;
        gm_b16_8 t73 = GM_LD(gm_b16_8, ((((uint8_t *)t2) + 8) + 8));
        gm_l11 = t73;
        {
            gm_b16_8 t75 = gm_l11;
            gm_l12 = t75;
        }
        {
            gm_l13 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t77 = gm_l13;
                uint64_t t78 = GM_LD(uintptr_t, ((uint8_t *)&gm_l12) + 8);
                uint8_t t79 = (t77 < t78);
                if (!t79) goto lb76;
                {
                    uint64_t t82 = gm_l13;
                    uint64_t t83 = (uintptr_t)t82;
                    uint64_t t84 = GM_LD(uintptr_t, ((uint8_t *)&gm_l12) + 8);
                    void * t85 = GM_LD(void *, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l12)) + t83 * 8));
                    void * t86 = t85;
                    void * t89 = GM_LD(void *, ((uint8_t *)&t86));
                    gm_l14 = t89;
                    {
                        gm_closure t91 = gm_l1;
                        void * t92 = gm_l14;
                        gm_closure t93 = t91;
                        gm_node *ccx94 = t93.cx;
                        void * t95 = t93.code;
                        gm_b0_1 t96;
                        if (GM_PTR_TAGGED(t95)) {
                            ((void (*)(void *))GM_PTR_UNTAG(t95))(t92);
                        } else {
                            t96 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t95)(t93.env, ccx94, NULL, t92);
                        }
                        if (gm_unw.kind) {
                            goto gm_ll80;
                        }
                    }
                    goto me87;
mn88:;
                    gm_trap("no match arm matched the scrutinee", gm_srcod98slk3vnra, 706, 20);
me87:;
                }
lc76:;
                {
                    uint64_t t98 = gm_l13;
                    uint64_t r99; bool o100 = __builtin_add_overflow(t98, ((uint64_t)1ULL), &r99);
                    uint64_t t101 = r99;
                    gm_l13 = t101;
                }
            }
gm_ll80:;
            if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb76; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc76; }
            }
            goto gm_lad;
lb76:;
        }
        goto me3;
mn71:;
ma8:;
        goto me3;
mn102:;
ma9:;
        gm_trap("no match arm matched the scrutinee", gm_srcod98slk3vnra, 702, 5);
me3:;
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_default_spec_cdd8jng(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_default_spec_cdd8jng F;
    void * gm_l0; /* s */
    GM_STACK_CHECK(&F, 98u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t2 = 0;
        uint8_t t3 = 0;
        uint8_t t4 = 0;
        uint8_t t5 = 0;
        uint8_t t6 = 0;
        uint8_t t7 = 0;
        uint8_t t8 = 0;
        void * t9 = gm_l0;
        uint8_t t10 = GM_LD(uint8_t, (((uint8_t *)t9) + 0));
        uint8_t t11 = ((t10 == ((uint8_t)32ULL)));
        if (t11) {
            void * t12 = gm_l0;
            uint8_t t13 = ((GM_LD(uint8_t, (((uint8_t *)t12) + 1) + 0) == ((uint8_t)0ULL)));
            t8 = t13;
        } else t8 = 0;
        if (t8) {
            void * t14 = gm_l0;
            uint8_t t15 = GM_LD(uint8_t, (((uint8_t *)t14) + 2));
            uint8_t t16 = (uint8_t)!t15;
            t7 = t16;
        } else t7 = 0;
        if (t7) {
            void * t17 = gm_l0;
            uint8_t t18 = GM_LD(uint8_t, (((uint8_t *)t17) + 3));
            uint8_t t19 = (uint8_t)!t18;
            t6 = t19;
        } else t6 = 0;
        if (t6) {
            void * t20 = gm_l0;
            uint8_t t21 = GM_LD(uint8_t, (((uint8_t *)t20) + 4));
            uint8_t t22 = (uint8_t)!t21;
            t5 = t22;
        } else t5 = 0;
        if (t5) {
            void * t23 = gm_l0;
            uint32_t t24 = GM_LD(uint32_t, (((uint8_t *)t23) + 8));
            uint8_t t25 = ((t24 == ((uint32_t)0ULL)));
            t4 = t25;
        } else t4 = 0;
        if (t4) {
            void * t26 = gm_l0;
            uint8_t t27;
            if ((((((uint8_t *)t26) + 12))[4] != 0)) goto mn29;
            t27 = ((uint8_t)1);
            goto me28;
mn29:;
            t27 = ((uint8_t)0);
            goto me28;
mn30:;
            gm_trap("no match arm matched the scrutinee", gm_srcod98slk3vnra, 527, 112);
me28:;
            t3 = t27;
        } else t3 = 0;
        if (t3) {
            void * t31 = gm_l0;
            uint8_t t32 = ((GM_LD(uint8_t, (((uint8_t *)t31) + 20) + 0) == ((uint8_t)0ULL)));
            t2 = t32;
        } else t2 = 0;
        uint8_t t33 = 0;
        F.ret = t2;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void * gm_f_mk_9bc97fd(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0, const void * a1, gm_tl874t2atnlh3 a2) {
    struct gm_fr_mk_9bc97fd F;
    gm_t88rcucg1b7uk gm_l0; /* ty */
    gm_tl874t2atnlh3 gm_l2; /* span */
    GM_STACK_CHECK(&F, 312u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&F.l1, a1, sizeof F.l1);
    gm_l2 = a2;
    gm_tfpf0s89fpb3r t1;
    memset((uint8_t *)&t1 + 4, 0, 4);
    memset((uint8_t *)&t1 + 220, 0, 4);
    gm_t88rcucg1b7uk t2 = gm_l0;
    GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t1 + 0, t2);
    memcpy((uint8_t *)&t1 + 8, ((uint8_t *)&F.l1), 200);
    gm_tl874t2atnlh3 t3 = gm_l2;
    GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t1 + 208, t3);
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
inline __attribute__((aligned(4))) void * gm_f_mk_pat_hr2l6c4(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0, const void * a1, gm_tl874t2atnlh3 a2) {
    struct gm_fr_mk_pat_hr2l6c4 F;
    gm_t88rcucg1b7uk gm_l0; /* ty */
    gm_tl874t2atnlh3 gm_l2; /* span */
    GM_STACK_CHECK(&F, 224u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&F.l1, a1, sizeof F.l1);
    gm_l2 = a2;
    gm_tc9qv69frb08e t1;
    memset((uint8_t *)&t1 + 4, 0, 4);
    memset((uint8_t *)&t1 + 132, 0, 4);
    gm_t88rcucg1b7uk t2 = gm_l0;
    GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t1 + 0, t2);
    memcpy((uint8_t *)&t1 + 8, ((uint8_t *)&F.l1), 112);
    gm_tl874t2atnlh3 t3 = gm_l2;
    GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t1 + 120, t3);
    gm_tq7s98ihonkju t4 = gm_f_new_1b2khgi((void *)NULL, cx, NULL, &t1, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_k652m2ksgeg)));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tq7s98ihonkju t5 = t4;
    if (((((uint8_t *)&t5))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t5) + 8, gm_srcod98slk3vnra, 311, 83);
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
inline __attribute__((aligned(4))) void gm_f_pat_children_e3peovr(void * a0, gm_closure a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_pat_children_e3peovr F;
    void * gm_l0; /* p */
    void * gm_l2; /* e */
    GM_STACK_CHECK(&F, 129u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    F.l1 = a1;
    {
        void * t3 = gm_l0;
        uint8_t t4 = GM_LD(uint8_t, ((((uint8_t *)t3) + 8) + 0));
        if (t4 != ((uint8_t)2ULL)) goto ie2;
        void * t5 = GM_LD(void *, ((((uint8_t *)t3) + 8) + 8));
        gm_l2 = t5;
        {
            gm_closure t7 = F.l1;
            void * t8 = gm_l2;
            gm_closure t9 = t7;
            gm_node *ccx10 = t9.cx;
            void * t11 = t9.code;
            gm_b0_1 t12;
            if (GM_PTR_TAGGED(t11)) {
                ((void (*)(void *))GM_PTR_UNTAG(t11))(t8);
            } else {
                t12 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t11)(t9.env, ccx10, NULL, t8);
            }
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t13 = gm_l0;
        gm_closure t14 = ((gm_closure){(void *)gm_f_fn_hdas5h7, (void *)&F.h, cx});
        gm_f_for_each_subpat_arlrtrm(t13, t14);
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
__attribute__((aligned(4))) gm_b16_8 gm_f_sys_name_pcdoujk(gm_t9qkh0rsooc8d a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_sys_name_pcdoujk F;
    gm_t9qkh0rsooc8d gm_l0; /* op */
    GM_STACK_CHECK(&F, 985u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t1;
    uint8_t t59 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    switch (t59) { case ((uint8_t)0ULL): goto ma3; case ((uint8_t)1ULL): goto ma4; case ((uint8_t)2ULL): goto ma5; case ((uint8_t)3ULL): goto ma6; case ((uint8_t)4ULL): goto ma7; case ((uint8_t)5ULL): goto ma8; case ((uint8_t)6ULL): goto ma9; case ((uint8_t)7ULL): goto ma10; case ((uint8_t)8ULL): goto ma11; case ((uint8_t)9ULL): goto ma12; case ((uint8_t)10ULL): goto ma13; case ((uint8_t)11ULL): goto ma14; case ((uint8_t)12ULL): goto ma15; case ((uint8_t)13ULL): goto ma16; case ((uint8_t)14ULL): goto ma17; case ((uint8_t)15ULL): goto ma18; case ((uint8_t)16ULL): goto ma19; case ((uint8_t)17ULL): goto ma20; case ((uint8_t)18ULL): goto ma21; case ((uint8_t)19ULL): goto ma22; case ((uint8_t)20ULL): goto ma23; case ((uint8_t)21ULL): goto ma24; case ((uint8_t)22ULL): goto ma25; case ((uint8_t)23ULL): goto ma26; case ((uint8_t)24ULL): goto ma27; case ((uint8_t)25ULL): goto ma28; case ((uint8_t)26ULL): goto ma29; case ((uint8_t)27ULL): goto ma30; case ((uint8_t)28ULL): goto ma31; case ((uint8_t)29ULL): goto ma32; case ((uint8_t)30ULL): goto ma33; case ((uint8_t)31ULL): goto ma34; case ((uint8_t)32ULL): goto ma35; case ((uint8_t)33ULL): goto ma36; case ((uint8_t)34ULL): goto ma37; case ((uint8_t)35ULL): goto ma38; case ((uint8_t)36ULL): goto ma39; case ((uint8_t)37ULL): goto ma40; case ((uint8_t)38ULL): goto ma41; case ((uint8_t)39ULL): goto ma42; case ((uint8_t)40ULL): goto ma43; case ((uint8_t)41ULL): goto ma44; case ((uint8_t)42ULL): goto ma45; case ((uint8_t)43ULL): goto ma46; case ((uint8_t)44ULL): goto ma47; case ((uint8_t)45ULL): goto ma48; case ((uint8_t)46ULL): goto ma49; case ((uint8_t)47ULL): goto ma50; case ((uint8_t)48ULL): goto ma51; case ((uint8_t)49ULL): goto ma52; case ((uint8_t)50ULL): goto ma53; case ((uint8_t)51ULL): goto ma54; case ((uint8_t)52ULL): goto ma55; case ((uint8_t)53ULL): goto ma56; case ((uint8_t)54ULL): goto ma57; default: goto ma58; }
ma3:;
    uint8_t t61 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t61 != ((uint8_t)0ULL)) goto mn60;
    gm_b16_8 t62;
    GM_ST(void *, (uint8_t *)&t62, (void *)(gm_s78m4481dj77r));
    GM_ST(uintptr_t, (uint8_t *)&t62 + 8, (uintptr_t)(8));
    t1 = t62;
    goto me2;
mn60:;
ma4:;
    uint8_t t64 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t64 != ((uint8_t)1ULL)) goto mn63;
    gm_b16_8 t65;
    GM_ST(void *, (uint8_t *)&t65, (void *)(gm_sv9p44pp2n1jk));
    GM_ST(uintptr_t, (uint8_t *)&t65 + 8, (uintptr_t)(9));
    t1 = t65;
    goto me2;
mn63:;
ma5:;
    uint8_t t67 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t67 != ((uint8_t)2ULL)) goto mn66;
    gm_b16_8 t68;
    GM_ST(void *, (uint8_t *)&t68, (void *)(gm_s8o3vqolbfl8t));
    GM_ST(uintptr_t, (uint8_t *)&t68 + 8, (uintptr_t)(10));
    t1 = t68;
    goto me2;
mn66:;
ma6:;
    uint8_t t70 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t70 != ((uint8_t)3ULL)) goto mn69;
    gm_b16_8 t71;
    GM_ST(void *, (uint8_t *)&t71, (void *)(gm_skti0sb80pmgp));
    GM_ST(uintptr_t, (uint8_t *)&t71 + 8, (uintptr_t)(11));
    t1 = t71;
    goto me2;
mn69:;
ma7:;
    uint8_t t73 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t73 != ((uint8_t)4ULL)) goto mn72;
    gm_b16_8 t74;
    GM_ST(void *, (uint8_t *)&t74, (void *)(gm_sb641udihu5hs));
    GM_ST(uintptr_t, (uint8_t *)&t74 + 8, (uintptr_t)(9));
    t1 = t74;
    goto me2;
mn72:;
ma8:;
    uint8_t t76 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t76 != ((uint8_t)5ULL)) goto mn75;
    gm_b16_8 t77;
    GM_ST(void *, (uint8_t *)&t77, (void *)(gm_s6f0l2e1iha9i));
    GM_ST(uintptr_t, (uint8_t *)&t77 + 8, (uintptr_t)(7));
    t1 = t77;
    goto me2;
mn75:;
ma9:;
    uint8_t t79 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t79 != ((uint8_t)6ULL)) goto mn78;
    gm_b16_8 t80;
    GM_ST(void *, (uint8_t *)&t80, (void *)(gm_sfku76mf5fn5d));
    GM_ST(uintptr_t, (uint8_t *)&t80 + 8, (uintptr_t)(8));
    t1 = t80;
    goto me2;
mn78:;
ma10:;
    uint8_t t82 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t82 != ((uint8_t)7ULL)) goto mn81;
    gm_b16_8 t83;
    GM_ST(void *, (uint8_t *)&t83, (void *)(gm_saamkuuf6jrtf));
    GM_ST(uintptr_t, (uint8_t *)&t83 + 8, (uintptr_t)(7));
    t1 = t83;
    goto me2;
mn81:;
ma11:;
    uint8_t t85 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t85 != ((uint8_t)8ULL)) goto mn84;
    gm_b16_8 t86;
    GM_ST(void *, (uint8_t *)&t86, (void *)(gm_se3l2nlomk2pj));
    GM_ST(uintptr_t, (uint8_t *)&t86 + 8, (uintptr_t)(8));
    t1 = t86;
    goto me2;
mn84:;
ma12:;
    uint8_t t88 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t88 != ((uint8_t)9ULL)) goto mn87;
    gm_b16_8 t89;
    GM_ST(void *, (uint8_t *)&t89, (void *)(gm_sv1bu4l4kq188));
    GM_ST(uintptr_t, (uint8_t *)&t89 + 8, (uintptr_t)(7));
    t1 = t89;
    goto me2;
mn87:;
ma13:;
    uint8_t t91 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t91 != ((uint8_t)10ULL)) goto mn90;
    gm_b16_8 t92;
    GM_ST(void *, (uint8_t *)&t92, (void *)(gm_smatmsv0oedot));
    GM_ST(uintptr_t, (uint8_t *)&t92 + 8, (uintptr_t)(7));
    t1 = t92;
    goto me2;
mn90:;
ma14:;
    uint8_t t94 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t94 != ((uint8_t)11ULL)) goto mn93;
    gm_b16_8 t95;
    GM_ST(void *, (uint8_t *)&t95, (void *)(gm_s9819enjtsd7t));
    GM_ST(uintptr_t, (uint8_t *)&t95 + 8, (uintptr_t)(9));
    t1 = t95;
    goto me2;
mn93:;
ma15:;
    uint8_t t97 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t97 != ((uint8_t)12ULL)) goto mn96;
    gm_b16_8 t98;
    GM_ST(void *, (uint8_t *)&t98, (void *)(gm_sal1kdi1rij5t));
    GM_ST(uintptr_t, (uint8_t *)&t98 + 8, (uintptr_t)(9));
    t1 = t98;
    goto me2;
mn96:;
ma16:;
    uint8_t t100 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t100 != ((uint8_t)13ULL)) goto mn99;
    gm_b16_8 t101;
    GM_ST(void *, (uint8_t *)&t101, (void *)(gm_svkvl31jmj9ne));
    GM_ST(uintptr_t, (uint8_t *)&t101 + 8, (uintptr_t)(11));
    t1 = t101;
    goto me2;
mn99:;
ma17:;
    uint8_t t103 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t103 != ((uint8_t)14ULL)) goto mn102;
    gm_b16_8 t104;
    GM_ST(void *, (uint8_t *)&t104, (void *)(gm_siph7o6oug1g2));
    GM_ST(uintptr_t, (uint8_t *)&t104 + 8, (uintptr_t)(13));
    t1 = t104;
    goto me2;
mn102:;
ma18:;
    uint8_t t106 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t106 != ((uint8_t)15ULL)) goto mn105;
    gm_b16_8 t107;
    GM_ST(void *, (uint8_t *)&t107, (void *)(gm_seajfhgtfmhv7));
    GM_ST(uintptr_t, (uint8_t *)&t107 + 8, (uintptr_t)(11));
    t1 = t107;
    goto me2;
mn105:;
ma19:;
    uint8_t t109 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t109 != ((uint8_t)16ULL)) goto mn108;
    gm_b16_8 t110;
    GM_ST(void *, (uint8_t *)&t110, (void *)(gm_slginip33cenb));
    GM_ST(uintptr_t, (uint8_t *)&t110 + 8, (uintptr_t)(13));
    t1 = t110;
    goto me2;
mn108:;
ma20:;
    uint8_t t112 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t112 != ((uint8_t)17ULL)) goto mn111;
    gm_b16_8 t113;
    GM_ST(void *, (uint8_t *)&t113, (void *)(gm_sbgj0biroecr0));
    GM_ST(uintptr_t, (uint8_t *)&t113 + 8, (uintptr_t)(12));
    t1 = t113;
    goto me2;
mn111:;
ma21:;
    uint8_t t115 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t115 != ((uint8_t)18ULL)) goto mn114;
    gm_b16_8 t116;
    GM_ST(void *, (uint8_t *)&t116, (void *)(gm_sso3tuh819o23));
    GM_ST(uintptr_t, (uint8_t *)&t116 + 8, (uintptr_t)(8));
    t1 = t116;
    goto me2;
mn114:;
ma22:;
    uint8_t t118 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t118 != ((uint8_t)19ULL)) goto mn117;
    gm_b16_8 t119;
    GM_ST(void *, (uint8_t *)&t119, (void *)(gm_shegcik3mpeut));
    GM_ST(uintptr_t, (uint8_t *)&t119 + 8, (uintptr_t)(11));
    t1 = t119;
    goto me2;
mn117:;
ma23:;
    uint8_t t121 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t121 != ((uint8_t)20ULL)) goto mn120;
    gm_b16_8 t122;
    GM_ST(void *, (uint8_t *)&t122, (void *)(gm_sk1d0v1onojc1));
    GM_ST(uintptr_t, (uint8_t *)&t122 + 8, (uintptr_t)(7));
    t1 = t122;
    goto me2;
mn120:;
ma24:;
    uint8_t t124 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t124 != ((uint8_t)21ULL)) goto mn123;
    gm_b16_8 t125;
    GM_ST(void *, (uint8_t *)&t125, (void *)(gm_sp8bmcd3m68n6));
    GM_ST(uintptr_t, (uint8_t *)&t125 + 8, (uintptr_t)(6));
    t1 = t125;
    goto me2;
mn123:;
ma25:;
    uint8_t t127 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t127 != ((uint8_t)22ULL)) goto mn126;
    gm_b16_8 t128;
    GM_ST(void *, (uint8_t *)&t128, (void *)(gm_smvhngj8mlfjc));
    GM_ST(uintptr_t, (uint8_t *)&t128 + 8, (uintptr_t)(12));
    t1 = t128;
    goto me2;
mn126:;
ma26:;
    uint8_t t130 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t130 != ((uint8_t)23ULL)) goto mn129;
    gm_b16_8 t131;
    GM_ST(void *, (uint8_t *)&t131, (void *)(gm_s2jvv6mjt9vs3));
    GM_ST(uintptr_t, (uint8_t *)&t131 + 8, (uintptr_t)(12));
    t1 = t131;
    goto me2;
mn129:;
ma27:;
    uint8_t t133 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t133 != ((uint8_t)24ULL)) goto mn132;
    gm_b16_8 t134;
    GM_ST(void *, (uint8_t *)&t134, (void *)(gm_sd9o9rh5qe8e7));
    GM_ST(uintptr_t, (uint8_t *)&t134 + 8, (uintptr_t)(10));
    t1 = t134;
    goto me2;
mn132:;
ma28:;
    uint8_t t136 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t136 != ((uint8_t)25ULL)) goto mn135;
    gm_b16_8 t137;
    GM_ST(void *, (uint8_t *)&t137, (void *)(gm_sfd9hctnph9ef));
    GM_ST(uintptr_t, (uint8_t *)&t137 + 8, (uintptr_t)(11));
    t1 = t137;
    goto me2;
mn135:;
ma29:;
    uint8_t t139 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t139 != ((uint8_t)26ULL)) goto mn138;
    gm_b16_8 t140;
    GM_ST(void *, (uint8_t *)&t140, (void *)(gm_ssj8r83l87dr2));
    GM_ST(uintptr_t, (uint8_t *)&t140 + 8, (uintptr_t)(15));
    t1 = t140;
    goto me2;
mn138:;
ma30:;
    uint8_t t142 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t142 != ((uint8_t)27ULL)) goto mn141;
    gm_b16_8 t143;
    GM_ST(void *, (uint8_t *)&t143, (void *)(gm_suno9ilftpfht));
    GM_ST(uintptr_t, (uint8_t *)&t143 + 8, (uintptr_t)(11));
    t1 = t143;
    goto me2;
mn141:;
ma31:;
    uint8_t t145 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t145 != ((uint8_t)28ULL)) goto mn144;
    gm_b16_8 t146;
    GM_ST(void *, (uint8_t *)&t146, (void *)(gm_s65khs4s753q2));
    GM_ST(uintptr_t, (uint8_t *)&t146 + 8, (uintptr_t)(10));
    t1 = t146;
    goto me2;
mn144:;
ma32:;
    uint8_t t148 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t148 != ((uint8_t)29ULL)) goto mn147;
    gm_b16_8 t149;
    GM_ST(void *, (uint8_t *)&t149, (void *)(gm_sj49v74ko3ghf));
    GM_ST(uintptr_t, (uint8_t *)&t149 + 8, (uintptr_t)(14));
    t1 = t149;
    goto me2;
mn147:;
ma33:;
    uint8_t t151 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t151 != ((uint8_t)30ULL)) goto mn150;
    gm_b16_8 t152;
    GM_ST(void *, (uint8_t *)&t152, (void *)(gm_stl47boisb31o));
    GM_ST(uintptr_t, (uint8_t *)&t152 + 8, (uintptr_t)(10));
    t1 = t152;
    goto me2;
mn150:;
ma34:;
    uint8_t t154 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t154 != ((uint8_t)31ULL)) goto mn153;
    gm_b16_8 t155;
    GM_ST(void *, (uint8_t *)&t155, (void *)(gm_s86kdga2ul6h4));
    GM_ST(uintptr_t, (uint8_t *)&t155 + 8, (uintptr_t)(8));
    t1 = t155;
    goto me2;
mn153:;
ma35:;
    uint8_t t157 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t157 != ((uint8_t)32ULL)) goto mn156;
    gm_b16_8 t158;
    GM_ST(void *, (uint8_t *)&t158, (void *)(gm_s59a022mrijic));
    GM_ST(uintptr_t, (uint8_t *)&t158 + 8, (uintptr_t)(8));
    t1 = t158;
    goto me2;
mn156:;
ma36:;
    uint8_t t160 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t160 != ((uint8_t)33ULL)) goto mn159;
    gm_b16_8 t161;
    GM_ST(void *, (uint8_t *)&t161, (void *)(gm_sk77bvr488rq3));
    GM_ST(uintptr_t, (uint8_t *)&t161 + 8, (uintptr_t)(9));
    t1 = t161;
    goto me2;
mn159:;
ma37:;
    uint8_t t163 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t163 != ((uint8_t)34ULL)) goto mn162;
    gm_b16_8 t164;
    GM_ST(void *, (uint8_t *)&t164, (void *)(gm_s4jhjvgae4896));
    GM_ST(uintptr_t, (uint8_t *)&t164 + 8, (uintptr_t)(12));
    t1 = t164;
    goto me2;
mn162:;
ma38:;
    uint8_t t166 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t166 != ((uint8_t)35ULL)) goto mn165;
    gm_b16_8 t167;
    GM_ST(void *, (uint8_t *)&t167, (void *)(gm_sfbjh3dq66kkg));
    GM_ST(uintptr_t, (uint8_t *)&t167 + 8, (uintptr_t)(16));
    t1 = t167;
    goto me2;
mn165:;
ma39:;
    uint8_t t169 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t169 != ((uint8_t)36ULL)) goto mn168;
    gm_b16_8 t170;
    GM_ST(void *, (uint8_t *)&t170, (void *)(gm_skqa2fjpbsodp));
    GM_ST(uintptr_t, (uint8_t *)&t170 + 8, (uintptr_t)(8));
    t1 = t170;
    goto me2;
mn168:;
ma40:;
    uint8_t t172 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t172 != ((uint8_t)37ULL)) goto mn171;
    gm_b16_8 t173;
    GM_ST(void *, (uint8_t *)&t173, (void *)(gm_s84p47580vnhr));
    GM_ST(uintptr_t, (uint8_t *)&t173 + 8, (uintptr_t)(11));
    t1 = t173;
    goto me2;
mn171:;
ma41:;
    uint8_t t175 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t175 != ((uint8_t)38ULL)) goto mn174;
    gm_b16_8 t176;
    GM_ST(void *, (uint8_t *)&t176, (void *)(gm_sk0bnuggr6sri));
    GM_ST(uintptr_t, (uint8_t *)&t176 + 8, (uintptr_t)(13));
    t1 = t176;
    goto me2;
mn174:;
ma42:;
    uint8_t t178 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t178 != ((uint8_t)39ULL)) goto mn177;
    gm_b16_8 t179;
    GM_ST(void *, (uint8_t *)&t179, (void *)(gm_siqaapb0mqsrg));
    GM_ST(uintptr_t, (uint8_t *)&t179 + 8, (uintptr_t)(13));
    t1 = t179;
    goto me2;
mn177:;
ma43:;
    uint8_t t181 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t181 != ((uint8_t)40ULL)) goto mn180;
    gm_b16_8 t182;
    GM_ST(void *, (uint8_t *)&t182, (void *)(gm_sdtlgdl6ed52a));
    GM_ST(uintptr_t, (uint8_t *)&t182 + 8, (uintptr_t)(13));
    t1 = t182;
    goto me2;
mn180:;
ma44:;
    uint8_t t184 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t184 != ((uint8_t)41ULL)) goto mn183;
    gm_b16_8 t185;
    GM_ST(void *, (uint8_t *)&t185, (void *)(gm_smfprcpea3ibe));
    GM_ST(uintptr_t, (uint8_t *)&t185 + 8, (uintptr_t)(9));
    t1 = t185;
    goto me2;
mn183:;
ma45:;
    uint8_t t187 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t187 != ((uint8_t)42ULL)) goto mn186;
    gm_b16_8 t188;
    GM_ST(void *, (uint8_t *)&t188, (void *)(gm_si8jv2q8d22kl));
    GM_ST(uintptr_t, (uint8_t *)&t188 + 8, (uintptr_t)(9));
    t1 = t188;
    goto me2;
mn186:;
ma46:;
    uint8_t t190 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t190 != ((uint8_t)43ULL)) goto mn189;
    gm_b16_8 t191;
    GM_ST(void *, (uint8_t *)&t191, (void *)(gm_skm4ndn7op5l5));
    GM_ST(uintptr_t, (uint8_t *)&t191 + 8, (uintptr_t)(13));
    t1 = t191;
    goto me2;
mn189:;
ma47:;
    uint8_t t193 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t193 != ((uint8_t)44ULL)) goto mn192;
    gm_b16_8 t194;
    GM_ST(void *, (uint8_t *)&t194, (void *)(gm_stclkv4qnta30));
    GM_ST(uintptr_t, (uint8_t *)&t194 + 8, (uintptr_t)(8));
    t1 = t194;
    goto me2;
mn192:;
ma48:;
    uint8_t t196 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t196 != ((uint8_t)45ULL)) goto mn195;
    gm_b16_8 t197;
    GM_ST(void *, (uint8_t *)&t197, (void *)(gm_s7ri8monh3c4h));
    GM_ST(uintptr_t, (uint8_t *)&t197 + 8, (uintptr_t)(7));
    t1 = t197;
    goto me2;
mn195:;
ma49:;
    uint8_t t199 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t199 != ((uint8_t)46ULL)) goto mn198;
    gm_b16_8 t200;
    GM_ST(void *, (uint8_t *)&t200, (void *)(gm_s26ul1d5snk55));
    GM_ST(uintptr_t, (uint8_t *)&t200 + 8, (uintptr_t)(8));
    t1 = t200;
    goto me2;
mn198:;
ma50:;
    uint8_t t202 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t202 != ((uint8_t)47ULL)) goto mn201;
    gm_b16_8 t203;
    GM_ST(void *, (uint8_t *)&t203, (void *)(gm_s1pfh89airaa0));
    GM_ST(uintptr_t, (uint8_t *)&t203 + 8, (uintptr_t)(12));
    t1 = t203;
    goto me2;
mn201:;
ma51:;
    uint8_t t205 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t205 != ((uint8_t)48ULL)) goto mn204;
    gm_b16_8 t206;
    GM_ST(void *, (uint8_t *)&t206, (void *)(gm_slkubuckp49ih));
    GM_ST(uintptr_t, (uint8_t *)&t206 + 8, (uintptr_t)(11));
    t1 = t206;
    goto me2;
mn204:;
ma52:;
    uint8_t t208 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t208 != ((uint8_t)49ULL)) goto mn207;
    gm_b16_8 t209;
    GM_ST(void *, (uint8_t *)&t209, (void *)(gm_sf5jsgjp29cqn));
    GM_ST(uintptr_t, (uint8_t *)&t209 + 8, (uintptr_t)(13));
    t1 = t209;
    goto me2;
mn207:;
ma53:;
    uint8_t t211 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t211 != ((uint8_t)50ULL)) goto mn210;
    gm_b16_8 t212;
    GM_ST(void *, (uint8_t *)&t212, (void *)(gm_sik1l49uru5dh));
    GM_ST(uintptr_t, (uint8_t *)&t212 + 8, (uintptr_t)(12));
    t1 = t212;
    goto me2;
mn210:;
ma54:;
    uint8_t t214 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t214 != ((uint8_t)51ULL)) goto mn213;
    gm_b16_8 t215;
    GM_ST(void *, (uint8_t *)&t215, (void *)(gm_skk6lm5m17t6v));
    GM_ST(uintptr_t, (uint8_t *)&t215 + 8, (uintptr_t)(10));
    t1 = t215;
    goto me2;
mn213:;
ma55:;
    uint8_t t217 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t217 != ((uint8_t)52ULL)) goto mn216;
    gm_b16_8 t218;
    GM_ST(void *, (uint8_t *)&t218, (void *)(gm_sed3elknidrs3));
    GM_ST(uintptr_t, (uint8_t *)&t218 + 8, (uintptr_t)(10));
    t1 = t218;
    goto me2;
mn216:;
ma56:;
    uint8_t t220 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t220 != ((uint8_t)53ULL)) goto mn219;
    gm_b16_8 t221;
    GM_ST(void *, (uint8_t *)&t221, (void *)(gm_sfcjpmkuggqo3));
    GM_ST(uintptr_t, (uint8_t *)&t221 + 8, (uintptr_t)(9));
    t1 = t221;
    goto me2;
mn219:;
ma57:;
    uint8_t t223 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t223 != ((uint8_t)54ULL)) goto mn222;
    gm_b16_8 t224;
    GM_ST(void *, (uint8_t *)&t224, (void *)(gm_su6ipjjrnjs7p));
    GM_ST(uintptr_t, (uint8_t *)&t224 + 8, (uintptr_t)(9));
    t1 = t224;
    goto me2;
mn222:;
ma58:;
    gm_trap("no match arm matched the scrutinee", gm_srcod98slk3vnra, 772, 44);
me2:;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
