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
_Static_assert(sizeof(gm_trl9d7m1k5lbd) == 24 && _Alignof(gm_trl9d7m1k5lbd) == 8 && offsetof(gm_trl9d7m1k5lbd, f0) == 0 && offsetof(gm_trl9d7m1k5lbd, f1) == 8 && offsetof(gm_trl9d7m1k5lbd, f2) == 16, "layout of `(op: BinOp, lhs: ^Expr, rhs: ^Expr)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint32_t v0; gm_t419mo45g7j0a v1; void * v2; } f1; } gm_tsth9qu1dv0g3;
_Static_assert(sizeof(gm_tt8o5kebou62r) == 32 && _Alignof(gm_tt8o5kebou62r) == 8 && offsetof(gm_tt8o5kebou62r, f0) == 0 && offsetof(gm_tt8o5kebou62r, f1) == 16, "layout of `(op: Builtin, args: []Expr)`");
_Static_assert(sizeof(gm_t53at80pfdus3) == 56 && _Alignof(gm_t53at80pfdus3) == 8 && offsetof(gm_t53at80pfdus3, f0) == 0 && offsetof(gm_t53at80pfdus3, f1) == 8, "layout of `TyKind`");
typedef struct { gm_tsth9qu1dv0g3 f0; gm_b16_8 f1; } gm_to76jli3bqddq;
_Static_assert(sizeof(gm_tsth9qu1dv0g3) == 40 && _Alignof(gm_tsth9qu1dv0g3) == 8 && offsetof(gm_tsth9qu1dv0g3, f0) == 0 && offsetof(gm_tsth9qu1dv0g3, f1) == 8, "layout of `Callee`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tbci5ospk5nnn v0; double v1; void * v2; void * v3; uint8_t v4; gm_b16_8 v5; gm_b16_8 v6; uint32_t v7; gm_tkn42u7na3t81 v8; gm_tkn42u7na3t81 v9; gm_t88rcucg1b7uk v10; gm_t3rcjibl8kkg4 v11; gm_t08olq97rscn3 v12; uint32_t v13; gm_tcc1rq4n1avb2 v14; gm_t3877pk7lv748 v15; void * v16; gm_tbl8il92l97ut v17; gm_tlcl4u558l6p5 v18; gm_tao63pk3r18v5 v19; void * v20; void * v21; void * v22; void * v23; uint32_t v24; uint32_t v25; uint32_t v26; gm_t8ghgl2jj0qha v27; gm_trl9d7m1k5lbd v28; gm_ta612fil812pm v29; gm_t8jsc4n9vp2ib v30; gm_tt8o5kebou62r v31; gm_to76jli3bqddq v32; gm_b16_8 v33; gm_b16_8 v34; void * v35; gm_ttggq7ng4kp3p v36; void * v37; gm_tsplfr33dsvji v38; gm_t3dcoc7077t1q v39; gm_t083kd7b1kkag v40; gm_tlcl4u558l6p5 v41; gm_tkaapcii86udk v42; gm_tlcl4u558l6p5 v43; void * v44; gm_trl2khmuphncd v45; gm_t710lsrq1m8gk v46; gm_teo9d4mncjfdu v47; gm_te9hrk0u35rev v48; gm_ttggq7ng4kp3p v49; void * v50; gm_t3mf58gv9kf3n v51; void * v52; void * v53; void * v54; gm_tandvbt63uk7n v55; gm_t710lsrq1m8gk v56; } f1; } gm_teti411eujvf4;
_Static_assert(sizeof(gm_to76jli3bqddq) == 56 && _Alignof(gm_to76jli3bqddq) == 8 && offsetof(gm_to76jli3bqddq, f0) == 0 && offsetof(gm_to76jli3bqddq, f1) == 40, "layout of `(callee: Callee, args: []Expr)`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_teti411eujvf4 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_t69m56meqefs8;
_Static_assert(sizeof(gm_teti411eujvf4) == 64 && _Alignof(gm_teti411eujvf4) == 8 && offsetof(gm_teti411eujvf4, f0) == 0 && offsetof(gm_teti411eujvf4, f1) == 8, "layout of `ExprKind`");
_Static_assert(sizeof(gm_t69m56meqefs8) == 88 && _Alignof(gm_t69m56meqefs8) == 8 && offsetof(gm_t69m56meqefs8, f0) == 0 && offsetof(gm_t69m56meqefs8, f1) == 8 && offsetof(gm_t69m56meqefs8, f2) == 72, "layout of `Expr`");
struct gm_fr_bool_ty_kotvo8t {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_bool_ty_pn2p277 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_empty_slice_5nn7ph0 {
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
struct gm_fr_fn_b1j5nho {
    gm_frame h;
    gm_t5o0tpv5jka2l ret;
};
struct gm_fr_fn_dahlogl {
    gm_frame h;
    gm_t69m56meqefs8 l2; /* at */
    gm_t5o0tpv5jka2l ret;
};
struct gm_fr_fn_kvivfpi {
    gm_frame h;
    gm_t69m56meqefs8 l3; /* at */
    gm_t5o0tpv5jka2l ret;
};
struct gm_fr_fn_tk14v5n {
    gm_frame h;
    gm_t69m56meqefs8 l2; /* at */
    gm_t5o0tpv5jka2l ret;
};
struct gm_fr_fresh_5kokjmg {
    gm_frame h;
    uint32_t ret;
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
struct gm_fr_kind_ghvav22 {
    gm_frame h;
    gm_t53at80pfdus3 ret;
};
struct gm_fr_kind_jcnt4hm {
    gm_frame h;
    gm_t53at80pfdus3 ret;
};
struct gm_fr_line_l6u87p9 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_list_epvducg {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_uvfu1k1 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_loc_u9c0aqb {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_ptr_bytes_eocneng {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_slice_len_a3383br {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_slice_ptr_hojfgr5 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_to_decimal_8bshn5b {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_trqfvmjtt4sij l1; /* out */
    gm_b16_8 ret;
};
struct gm_fr_unit_2age34u {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_unit_8ekedsl {
    gm_frame h;
    gm_b16_8 ret;
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
    gm_t69m56meqefs8 l12; /* at */
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_bool_ty_kotvo8t(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_bool_ty_pn2p277(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_cty_66ee0kf(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_5nn7ph0(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_rcd2jln(void);
__attribute__((aligned(4))) void gm_f_expr_porbc9t(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_extend_0cs85gs(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_float_from_r6b8fu0(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_fn_b1j5nho(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_fn_dahlogl(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_fn_kvivfpi(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_fn_tk14v5n(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_format_0gjpujh(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_1mojd22(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
__attribute__((aligned(4))) void gm_f_format_21ahlbb(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_27c89bp(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
__attribute__((aligned(4))) void gm_f_format_368205k(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
__attribute__((aligned(4))) void gm_f_format_3tnvb6a(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
__attribute__((aligned(4))) void gm_f_format_48m8fhm(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
__attribute__((aligned(4))) void gm_f_format_4f5imdp(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
__attribute__((aligned(4))) void gm_f_format_61d2klb(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
__attribute__((aligned(4))) void gm_f_format_7lru3c2(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
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
inline __attribute__((aligned(4))) uint32_t gm_f_fresh_5kokjmg(void * a0);
__attribute__((aligned(4))) void gm_f_ii_min_lku7pdi(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tuglhjjmj2g58 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_indent_line_b3mckqa(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_int_binary_kp3u7i8(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_ts6oqppjh0ba3 a2, gm_t88rcucg1b7uk a3, gm_b16_8 a4, gm_b16_8 a5);
__attribute__((aligned(4))) void gm_f_int_info_2tcdf1n(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_int_lit_ai37456(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, const void * a2);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_intern_ia19mhl(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_into_slice_hq0te56(void *gm_out, void * a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_float_gthejtb(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_integer_8d4f9ek(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_integer_eo6jkc5(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void gm_f_join_sfknljb(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) void gm_f_kind_ghvav22(void *gm_out, void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void gm_f_kind_jcnt4hm(void *gm_out, void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_line_l6u87p9(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
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
inline __attribute__((aligned(4))) void gm_f_list_epvducg(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_loc_u9c0aqb(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
inline __attribute__((aligned(4))) uint64_t gm_f_ptr_bytes_eocneng(void * a0);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_3ef746b(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_t83rg2m(void * a0, uint8_t a1);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_simd_eafgrv9(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_t88rcucg1b7uk a2);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_slice_len_a3383br(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_slice_ptr_hojfgr5(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_src_loc_lrhtj7i(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_tmp_uninit_jr39s5u(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_tmp_uohsptq(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_to_decimal_8bshn5b(void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_unit_2age34u(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_unit_8ekedsl(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_unit_ty_vgceilu(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_usize_ty_9mijhtc(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_usize_ty_qnrd30f(void *env, gm_node *cx, gm_node *inst, void * a0);
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
__attribute__((aligned(4))) gm_b0_1 gm_f_write_decimal_ke367ab(void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_zero_tlk13aa(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
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
extern const char gm_src0gtuh94b1fce[];
extern const char gm_src0ih77f43mhf2[];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_src7hmnlrtmirgt[];
extern const char gm_srckfibkpt681aa[];
extern const char gm_srclv3pj5es0f8o[];
extern const char gm_srcmok80ho8hh01[];
extern const char gm_srcrgc9bkquf6nr[];
extern const uint8_t gm_srs1vrn2uaams[2];
extern const uint8_t gm_st3ja0nqi37bv[2];
extern const uint8_t gm_stnlf4pvbut1c[3];
extern const uint8_t gm_svaet5ugb1fj3[22];
extern const gm_type gm_tyo86ks9qkvt;
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
inline __attribute__((aligned(4))) uint32_t gm_f_fresh_5kokjmg(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_fresh_5kokjmg F;
    void * gm_l0; /* fg */
    GM_STACK_CHECK_SHALLOW(&F, 77u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        void * t3 = gm_l0;
        uint32_t t4 = GM_LD(uint32_t, (((uint8_t *)t3) + 232));
        uint32_t r5; bool o6 = __builtin_add_overflow(t4, ((uint32_t)1ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_srcmok80ho8hh01, 480, 5);
        uint32_t t7 = r5;
        GM_ST(uint32_t, (((uint8_t *)t2) + 232), t7);
    }
    {
        void * t8 = gm_l0;
        void * t9 = gm_l0;
        uint32_t t10 = GM_LD(uint32_t, (((uint8_t *)t9) + 360));
        uint32_t r11; bool o12 = __builtin_add_overflow(t10, ((uint32_t)1ULL), &r11);
        if (o12) gm_trap("integer overflow in `+`", gm_srcmok80ho8hh01, 481, 5);
        uint32_t t13 = r11;
        GM_ST(uint32_t, (((uint8_t *)t8) + 360), t13);
    }
    {
        void * t14 = gm_l0;
        uint32_t t15 = GM_LD(uint32_t, (((uint8_t *)t14) + 232));
        uint8_t t16 = 0;
        F.ret = t15;
        goto gm_fret;
    }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_line_l6u87p9(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1) {
    struct gm_fr_line_l6u87p9 F;
    void * gm_l0; /* fg */
    gm_b16_8 gm_l1; /* s */
    GM_STACK_CHECK(&F, 84u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        gm_b0_1 t3 = gm_f_indent_line_b3mckqa((void *)NULL, cx, NULL, t2);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t4 = gm_l0;
        void * t5 = (void *)(((uint8_t *)t4) + 184);
        gm_b16_8 t6 = gm_l1;
        gm_tftb1odc739t5 t7 = gm_f_extend_0cs85gs(t5, t6);
        if (((((uint8_t *)&t7))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t7) + 1, gm_srcmok80ho8hh01, 464, 5);
        gm_b0_1 t8 = GM_LD(gm_b0_1, (((uint8_t *)&t7) + 1));
    }
    {
        void * t9 = gm_l0;
        void * t10 = (void *)(((uint8_t *)t9) + 184);
        gm_tftb1odc739t5 t11 = gm_f_push_t83rg2m(t10, ((uint8_t)10ULL));
        if (((((uint8_t *)&t11))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t11) + 1, gm_srcmok80ho8hh01, 465, 5);
        gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t11) + 1));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_loc_u9c0aqb(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_loc_u9c0aqb F;
    void * gm_l0; /* fg */
    void * gm_l1; /* e */
    GM_STACK_CHECK(&F, 100u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    void * t2 = GM_LD(void *, (((uint8_t *)t1) + 0));
    void * t3 = gm_l1;
    gm_tl874t2atnlh3 t4 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t3) + 72));
    gm_b16_8 t5 = gm_f_src_loc_lrhtj7i((void *)NULL, cx, NULL, t2, t4);
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
inline __attribute__((aligned(4))) uint64_t gm_f_ptr_bytes_eocneng(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_ptr_bytes_eocneng F;
    void * gm_l0; /* fg */
    GM_STACK_CHECK_SHALLOW(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    void * t2 = GM_LD(void *, (((uint8_t *)t1) + 0));
    uint64_t t3 = GM_LD(uint64_t, ((((((uint8_t *)t2) + 8) + 8) + 0) + 0));
    F.ret = t3;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_slice_len_a3383br(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1) {
    struct gm_fr_slice_len_a3383br F;
    void * gm_l0; /* fg */
    gm_b16_8 gm_l1; /* addr */
    GM_STACK_CHECK(&F, 168u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_t3877pk7lv748 t1;
    gm_b16_8 t2 = gm_l1;
    GM_ST(gm_b16_8, (uint8_t *)&t1 + 0, t2);
    void * t3 = gm_l0;
    uint64_t t4 = gm_f_ptr_bytes_eocneng(t3);
    GM_ST(uint64_t, (uint8_t *)&t1 + 16, t4);
    gm_tabb710d9mq2e t5;
    gm_f_format_0gjpujh((void *)&t5, (void *)NULL, cx, NULL, &t1);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    if (((((uint8_t *)&t5))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t5) + 8, gm_src0gtuh94b1fce, 291, 72);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_slice_ptr_hojfgr5(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_slice_ptr_hojfgr5 F;
    gm_b16_8 gm_l0; /* addr */
    GM_STACK_CHECK(&F, 136u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t1 = gm_l0;
    gm_tnpifv20irnph t2;
    GM_ST(gm_b16_8, ((uint8_t *)&t2 + 0), t1);
    gm_tabb710d9mq2e t3;
    gm_f_format_7lru3c2((void *)&t3, (void *)NULL, cx, NULL, t2);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    if (((((uint8_t *)&t3))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t3) + 8, gm_src0gtuh94b1fce, 289, 60);
    gm_b16_8 t4 = GM_LD(gm_b16_8, (((uint8_t *)&t3) + 8));
    gm_b16_8 t5 = GM_LD(gm_b16_8, ((uint8_t *)&t4));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_unit_8ekedsl(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_unit_8ekedsl F;
    void * gm_l0; /* fg */
    GM_STACK_CHECK(&F, 92u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, (((uint8_t *)t2) + 0));
    void * t4 = (void *)(((uint8_t *)t3) + 8);
    gm_t88rcucg1b7uk t5 = gm_f_unit_ty_vgceilu((void *)NULL, cx, NULL, t4);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_b16_8 t6 = gm_f_zero_tlk13aa((void *)NULL, cx, NULL, t1, t5);
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
inline __attribute__((aligned(4))) void gm_f_fn_b1j5nho(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_b1j5nho F;
    gm_b16_8 gm_l0; /* i */
    gm_b16_8 gm_l1; /* x */
    gm_b16_8 gm_l2; /* pbv */
    gm_b16_8 gm_l3; /* y */
    gm_b16_8 gm_l4; /* f */
    gm_tuglhjjmj2g58 gm_l5; /* ii */
    gm_b16_8 gm_l6; /* min */
    GM_STACK_CHECK(&F, 925u);
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
            if (((((uint8_t *)&t32))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t32) + 8, gm_src7hmnlrtmirgt, 350, 44);
            gm_b16_8 t33 = GM_LD(gm_b16_8, (((uint8_t *)&t32) + 8));
            gm_b16_8 t34 = GM_LD(gm_b16_8, ((uint8_t *)&t33));
            gm_b16_8 t35 = gm_f_float_from_r6b8fu0((void *)NULL, cx, NULL, t26, t27, t34);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
            ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t35);
            goto gm_fdone;
        }
        goto ix8;
ie8:;
ix8:;
    }
    {
        void * t36 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_vec_minmax_abs_4le8bpj *)((&F.h)->up))->l0));
        gm_t88rcucg1b7uk t37 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&((struct gm_fr_vec_minmax_abs_4le8bpj *)((&F.h)->up))->l7));
        gm_tqcc6dmlne11b t38;
        gm_f_int_info_2tcdf1n((void *)&t38, (void *)NULL, cx, NULL, t36, t37);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (!((((uint8_t *)&t38))[16] != 0)) gm_trap("unwrap of null", gm_src7hmnlrtmirgt, 352, 19);
        gm_tuglhjjmj2g58 t39 = GM_LD(gm_tuglhjjmj2g58, ((uint8_t *)&t38));
        gm_l5 = t39;
    }
    {
        void * t40 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_vec_minmax_abs_4le8bpj *)((&F.h)->up))->l0));
        gm_t88rcucg1b7uk t41 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&((struct gm_fr_vec_minmax_abs_4le8bpj *)((&F.h)->up))->l7));
        gm_tuglhjjmj2g58 t42 = gm_l5;
        gm_tbci5ospk5nnn t43;
        gm_f_ii_min_lku7pdi((void *)&t43, (void *)NULL, cx, NULL, t42);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b16_8 t44 = gm_f_int_lit_ai37456((void *)NULL, cx, NULL, t40, t41, &t43);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l6 = t44;
    }
    {
        void * t45 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_vec_minmax_abs_4le8bpj *)((&F.h)->up))->l0));
        gm_tfeab279c16rr t46;
        gm_b16_8 t47 = gm_l1;
        GM_ST(gm_b16_8, (uint8_t *)&t46 + 0, t47);
        gm_b16_8 t48 = gm_l6;
        GM_ST(gm_b16_8, (uint8_t *)&t46 + 16, t48);
        gm_b16_8 t49 = GM_LD(gm_b16_8, ((uint8_t *)&((struct gm_fr_vec_minmax_abs_4le8bpj *)((&F.h)->up))->l15));
        GM_ST(gm_b16_8, (uint8_t *)&t46 + 32, t49);
        gm_b0_1 t50 = gm_f_linef_qno93os((void *)NULL, cx, NULL, t45, &t46);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_tfeab279c16rr t51;
        gm_b16_8 t52 = gm_l1;
        GM_ST(gm_b16_8, (uint8_t *)&t51 + 0, t52);
        gm_b16_8 t53 = gm_l1;
        GM_ST(gm_b16_8, (uint8_t *)&t51 + 16, t53);
        gm_b16_8 t54 = gm_l1;
        GM_ST(gm_b16_8, (uint8_t *)&t51 + 32, t54);
        gm_tabb710d9mq2e t55;
        gm_f_format_iqjnutf((void *)&t55, (void *)NULL, cx, NULL, &t51);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t55))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t55) + 8, gm_src7hmnlrtmirgt, 355, 20);
        gm_b16_8 t56 = GM_LD(gm_b16_8, (((uint8_t *)&t55) + 8));
        gm_b16_8 t57 = GM_LD(gm_b16_8, ((uint8_t *)&t56));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
        ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t57);
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
inline __attribute__((aligned(4))) void gm_f_fn_dahlogl(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_dahlogl F;
    gm_b16_8 gm_l0; /* i */
    gm_b16_8 gm_l1; /* y */
    GM_STACK_CHECK(&F, 339u);
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
        memset((uint8_t *)&F.l2 + 84, 0, 4);
        gm_t88rcucg1b7uk t8 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&((struct gm_fr_vector_unary_h7ar7ml *)((&F.h)->up))->l7));
        GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&F.l2 + 0, t8);
        gm_teti411eujvf4 t9;
        memset(&t9, 0, sizeof t9);
        GM_ST(uint8_t, ((uint8_t *)&t9 + 0), ((uint8_t)8ULL));
        GM_ST(gm_teti411eujvf4, (uint8_t *)&F.l2 + 8, t9);
        void * t10 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_vector_unary_h7ar7ml *)((&F.h)->up))->l1));
        gm_tl874t2atnlh3 t11 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t10) + 72));
        GM_ST(gm_tl874t2atnlh3, (uint8_t *)&F.l2 + 72, t11);
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
        if (((((uint8_t *)&t19))[0] != 0)) {
            gm_t5o0tpv5jka2l t20;
            GM_ST(uint8_t, (uint8_t *)&t20 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t20 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t20 + 4, 0);
            ((uint8_t *)&t20)[0] = 1;
            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t20 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t19) + 8)));
            F.ret = t20;
            goto gm_fret;
        }
        gm_b16_8 t21 = GM_LD(gm_b16_8, (((uint8_t *)&t19) + 8));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
        ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t21);
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
inline __attribute__((aligned(4))) void gm_f_fn_kvivfpi(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_kvivfpi F;
    gm_b16_8 gm_l0; /* i */
    gm_b16_8 gm_l1; /* x */
    gm_b16_8 gm_l2; /* y */
    GM_STACK_CHECK(&F, 422u);
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
        memset((uint8_t *)&F.l3 + 84, 0, 4);
        gm_t88rcucg1b7uk t14 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&((struct gm_fr_vector_binary_etnngue *)((&F.h)->up))->l8));
        GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&F.l3 + 0, t14);
        gm_teti411eujvf4 t15;
        memset(&t15, 0, sizeof t15);
        GM_ST(uint8_t, ((uint8_t *)&t15 + 0), ((uint8_t)8ULL));
        GM_ST(gm_teti411eujvf4, (uint8_t *)&F.l3 + 8, t15);
        void * t16 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_vector_binary_etnngue *)((&F.h)->up))->l1));
        gm_tl874t2atnlh3 t17 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t16) + 72));
        GM_ST(gm_tl874t2atnlh3, (uint8_t *)&F.l3 + 72, t17);
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
        if (((((uint8_t *)&t24))[0] != 0)) {
            gm_t5o0tpv5jka2l t25;
            GM_ST(uint8_t, (uint8_t *)&t25 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t25 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t25 + 4, 0);
            ((uint8_t *)&t25)[0] = 1;
            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t25 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t24) + 8)));
            F.ret = t25;
            goto gm_fret;
        }
        gm_b16_8 t26 = GM_LD(gm_b16_8, (((uint8_t *)&t24) + 8));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
        ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t26);
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
inline __attribute__((aligned(4))) void gm_f_fn_tk14v5n(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_tk14v5n F;
    gm_b16_8 gm_l0; /* i */
    gm_b16_8 gm_l1; /* x */
    GM_STACK_CHECK(&F, 338u);
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
        memset((uint8_t *)&F.l2 + 84, 0, 4);
        gm_t88rcucg1b7uk t8 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&((struct gm_fr_vector_binary_etnngue *)((&F.h)->up))->l8));
        GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&F.l2 + 0, t8);
        gm_teti411eujvf4 t9;
        memset(&t9, 0, sizeof t9);
        GM_ST(uint8_t, ((uint8_t *)&t9 + 0), ((uint8_t)8ULL));
        GM_ST(gm_teti411eujvf4, (uint8_t *)&F.l2 + 8, t9);
        void * t10 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_vector_binary_etnngue *)((&F.h)->up))->l1));
        gm_tl874t2atnlh3 t11 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t10) + 72));
        GM_ST(gm_tl874t2atnlh3, (uint8_t *)&F.l2 + 72, t11);
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
        if (((((uint8_t *)&t18))[0] != 0)) {
            gm_t5o0tpv5jka2l t19;
            GM_ST(uint8_t, (uint8_t *)&t19 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t19 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t19 + 4, 0);
            ((uint8_t *)&t19)[0] = 1;
            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t19 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t18) + 8)));
            F.ret = t19;
            goto gm_fret;
        }
        gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&t18) + 8));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
        ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t20);
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
    GM_STACK_CHECK(&F, 811u);
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
        if (((((uint8_t *)&t22))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t22) + 8, gm_src7hmnlrtmirgt, 232, 11);
        gm_b16_8 t23 = GM_LD(gm_b16_8, (((uint8_t *)&t22) + 8));
        gm_l9 = t23;
    }
    {
        void * t24 = gm_l0;
        gm_tud7oovfvnf43 t25;
        gm_b16_8 t26 = gm_l7;
        GM_ST(gm_b16_8, (uint8_t *)&t25 + 0, t26);
        gm_b16_8 t27 = gm_l9;
        GM_ST(gm_b16_8, (uint8_t *)&t25 + 16, t27);
        gm_b16_8 t28 = gm_l7;
        GM_ST(gm_b16_8, (uint8_t *)&t25 + 32, t28);
        gm_b16_8 t29 = gm_l2;
        GM_ST(gm_b16_8, (uint8_t *)&t25 + 48, t29);
        gm_b16_8 t30 = gm_l6;
        GM_ST(gm_b16_8, (uint8_t *)&t25 + 64, t30);
        gm_b0_1 t31 = gm_f_linef_di76emp((void *)NULL, cx, NULL, t24, &t25);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_th2d9c7aju8hp t32;
        gm_b16_8 t33 = gm_l8;
        GM_ST(gm_b16_8, (uint8_t *)&t32 + 0, t33);
        gm_b16_8 t34 = gm_l7;
        GM_ST(gm_b16_8, (uint8_t *)&t32 + 16, t34);
        gm_b16_8 t35 = gm_l3;
        GM_ST(gm_b16_8, (uint8_t *)&t32 + 32, t35);
        gm_b16_8 t36 = gm_l9;
        GM_ST(gm_b16_8, (uint8_t *)&t32 + 48, t36);
        gm_b16_8 t37 = gm_l7;
        GM_ST(gm_b16_8, (uint8_t *)&t32 + 64, t37);
        gm_b16_8 t38 = gm_l4;
        GM_ST(gm_b16_8, (uint8_t *)&t32 + 80, t38);
        gm_b16_8 t39 = gm_l9;
        GM_ST(gm_b16_8, (uint8_t *)&t32 + 96, t39);
        gm_tabb710d9mq2e t40;
        gm_f_format_l9df9v3((void *)&t40, (void *)NULL, cx, NULL, &t32);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t40))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t40) + 8, gm_src7hmnlrtmirgt, 234, 12);
        gm_b16_8 t41 = GM_LD(gm_b16_8, (((uint8_t *)&t40) + 8));
        gm_b16_8 t42 = GM_LD(gm_b16_8, ((uint8_t *)&t41));
        uint8_t t43 = 0;
        F.ret = t42;
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
    GM_STACK_CHECK(&F, 749u);
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
        if (((((uint8_t *)&t25))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t25) + 8, gm_src7hmnlrtmirgt, 220, 10);
        gm_b16_8 t26 = GM_LD(gm_b16_8, (((uint8_t *)&t25) + 8));
        gm_l10 = t26;
    }
    {
        void * t27 = gm_l0;
        gm_tif3elqk01bdl t28;
        gm_b16_8 t29 = gm_l8;
        GM_ST(gm_b16_8, (uint8_t *)&t28 + 0, t29);
        gm_b16_8 t30 = gm_l10;
        GM_ST(gm_b16_8, (uint8_t *)&t28 + 16, t30);
        gm_b16_8 t31 = gm_l8;
        GM_ST(gm_b16_8, (uint8_t *)&t28 + 32, t31);
        gm_b16_8 t32 = gm_l3;
        GM_ST(gm_b16_8, (uint8_t *)&t28 + 48, t32);
        gm_b16_8 t33 = gm_l2;
        GM_ST(gm_b16_8, (uint8_t *)&t28 + 64, t33);
        gm_b16_8 t34 = gm_l4;
        GM_ST(gm_b16_8, (uint8_t *)&t28 + 80, t34);
        gm_b0_1 t35 = gm_f_linef_c0cfb71((void *)NULL, cx, NULL, t27, &t28);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t36 = gm_l0;
        void * t37 = gm_l6;
        uint64_t t38 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 0));
        gm_t88rcucg1b7uk t39 = gm_l7;
        gm_t88rcucg1b7uk t40 = gm_f_simd_eafgrv9((void *)NULL, cx, NULL, t37, t38, t39);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t6j6smj7qoctu t41;
        gm_b16_8 t42 = gm_l10;
        GM_ST(gm_b16_8, (uint8_t *)&t41 + 0, t42);
        gm_b16_8 t43 = gm_l9;
        GM_ST(gm_b16_8, (uint8_t *)&t41 + 16, t43);
        gm_tabb710d9mq2e t44;
        gm_f_format_f89fec3((void *)&t44, (void *)NULL, cx, NULL, &t41);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t44))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t44) + 8, gm_src7hmnlrtmirgt, 222, 50);
        gm_b16_8 t45 = GM_LD(gm_b16_8, (((uint8_t *)&t44) + 8));
        gm_b16_8 t46 = GM_LD(gm_b16_8, ((uint8_t *)&t45));
        gm_b16_8 t47 = gm_f_vec_store_6d0dm94((void *)NULL, cx, NULL, t36, t40, t46);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t48 = 0;
        F.ret = t47;
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
inline __attribute__((aligned(4))) void gm_f_vec_compare_vrcged3(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tvdg94bk7aa22 a1, gm_b16_8 a2) {
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
    GM_STACK_CHECK(&F, 678u);
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
        gm_t88rcucg1b7uk t4 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t2 * 88) + 0));
        gm_l3 = t4;
    }
    {
        void * t5 = gm_l0;
        uint64_t t6 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t7 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        if (t6 >= (uintptr_t)(t7)) gm_trap_index(t6, t7, gm_src7hmnlrtmirgt, 254, 23);
        void * t8 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t6 * 88);
        gm_t5o0tpv5jka2l t9;
        gm_f_expr_porbc9t((void *)&t9, (void *)NULL, cx, NULL, t5, t8);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t9))[0] != 0)) {
            gm_t5o0tpv5jka2l t10;
            GM_ST(uint8_t, (uint8_t *)&t10 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t10 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t10 + 4, 0);
            ((uint8_t *)&t10)[0] = 1;
            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t10 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t9) + 8)));
            F.ret = t10;
            goto gm_fret;
        }
        gm_b16_8 t11 = GM_LD(gm_b16_8, (((uint8_t *)&t9) + 8));
        gm_l4 = t11;
    }
    {
        void * t12 = gm_l0;
        uint64_t t13 = (uintptr_t)((uint64_t)1ULL);
        uint64_t t14 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        if (t13 >= (uintptr_t)(t14)) gm_trap_index(t13, t14, gm_src7hmnlrtmirgt, 255, 23);
        void * t15 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t13 * 88);
        gm_t5o0tpv5jka2l t16;
        gm_f_expr_porbc9t((void *)&t16, (void *)NULL, cx, NULL, t12, t15);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t16))[0] != 0)) {
            gm_t5o0tpv5jka2l t17;
            GM_ST(uint8_t, (uint8_t *)&t17 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t17 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t17 + 4, 0);
            ((uint8_t *)&t17)[0] = 1;
            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t17 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t16) + 8)));
            F.ret = t17;
            goto gm_fret;
        }
        gm_b16_8 t18 = GM_LD(gm_b16_8, (((uint8_t *)&t16) + 8));
        gm_l5 = t18;
    }
    {
        void * t19 = gm_l0;
        gm_t88rcucg1b7uk t20 = gm_l3;
        gm_b16_8 t21 = gm_l4;
        gm_b16_8 t22 = gm_f_vec_load_8m1nglt((void *)NULL, cx, NULL, t19, t20, t21);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l6 = t22;
    }
    {
        void * t23 = gm_l0;
        gm_t88rcucg1b7uk t24 = gm_l3;
        gm_b16_8 t25 = gm_l5;
        gm_b16_8 t26 = gm_f_vec_load_8m1nglt((void *)NULL, cx, NULL, t23, t24, t25);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l7 = t26;
    }
    {
        gm_b16_8 t27;
        uint8_t t36 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        switch (t36) { case ((uint8_t)0ULL): goto ma34; case ((uint8_t)1ULL): goto ma34; case ((uint8_t)2ULL): goto ma34; case ((uint8_t)3ULL): goto ma34; case ((uint8_t)4ULL): goto ma34; case ((uint8_t)5ULL): goto ma34; case ((uint8_t)6ULL): goto ma34; case ((uint8_t)7ULL): goto ma34; case ((uint8_t)8ULL): goto ma34; case ((uint8_t)9ULL): goto ma34; case ((uint8_t)10ULL): goto ma34; case ((uint8_t)11ULL): goto ma34; case ((uint8_t)12ULL): goto ma34; case ((uint8_t)13ULL): goto ma34; case ((uint8_t)14ULL): goto ma34; case ((uint8_t)15ULL): goto ma34; case ((uint8_t)16ULL): goto ma34; case ((uint8_t)17ULL): goto ma34; case ((uint8_t)18ULL): goto ma34; case ((uint8_t)19ULL): goto ma34; case ((uint8_t)20ULL): goto ma34; case ((uint8_t)21ULL): goto ma34; case ((uint8_t)22ULL): goto ma34; case ((uint8_t)23ULL): goto ma34; case ((uint8_t)24ULL): goto ma34; case ((uint8_t)25ULL): goto ma34; case ((uint8_t)26ULL): goto ma34; case ((uint8_t)27ULL): goto ma34; case ((uint8_t)28ULL): goto ma34; case ((uint8_t)29ULL): goto ma34; case ((uint8_t)30ULL): goto ma34; case ((uint8_t)31ULL): goto ma34; case ((uint8_t)32ULL): goto ma34; case ((uint8_t)33ULL): goto ma34; case ((uint8_t)34ULL): goto ma34; case ((uint8_t)35ULL): goto ma34; case ((uint8_t)36ULL): goto ma34; case ((uint8_t)37ULL): goto ma34; case ((uint8_t)38ULL): goto ma34; case ((uint8_t)39ULL): goto ma34; case ((uint8_t)40ULL): goto ma29; case ((uint8_t)41ULL): goto ma30; case ((uint8_t)42ULL): goto ma31; case ((uint8_t)43ULL): goto ma32; case ((uint8_t)44ULL): goto ma33; case ((uint8_t)45ULL): goto ma34; case ((uint8_t)46ULL): goto ma34; case ((uint8_t)47ULL): goto ma34; case ((uint8_t)48ULL): goto ma34; case ((uint8_t)49ULL): goto ma34; case ((uint8_t)50ULL): goto ma34; case ((uint8_t)51ULL): goto ma34; case ((uint8_t)52ULL): goto ma34; case ((uint8_t)53ULL): goto ma34; case ((uint8_t)54ULL): goto ma34; case ((uint8_t)55ULL): goto ma34; case ((uint8_t)56ULL): goto ma34; case ((uint8_t)57ULL): goto ma34; case ((uint8_t)58ULL): goto ma34; case ((uint8_t)59ULL): goto ma34; case ((uint8_t)60ULL): goto ma34; case ((uint8_t)61ULL): goto ma34; case ((uint8_t)62ULL): goto ma34; case ((uint8_t)63ULL): goto ma34; case ((uint8_t)64ULL): goto ma34; case ((uint8_t)65ULL): goto ma34; case ((uint8_t)66ULL): goto ma34; case ((uint8_t)67ULL): goto ma34; case ((uint8_t)68ULL): goto ma34; case ((uint8_t)69ULL): goto ma34; case ((uint8_t)70ULL): goto ma34; case ((uint8_t)71ULL): goto ma34; case ((uint8_t)72ULL): goto ma34; case ((uint8_t)73ULL): goto ma34; case ((uint8_t)74ULL): goto ma34; case ((uint8_t)75ULL): goto ma34; case ((uint8_t)76ULL): goto ma34; case ((uint8_t)77ULL): goto ma34; case ((uint8_t)78ULL): goto ma34; case ((uint8_t)79ULL): goto ma34; case ((uint8_t)80ULL): goto ma34; case ((uint8_t)81ULL): goto ma34; default: goto ma34; }
ma29:;
        uint8_t t38 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t38 != ((uint8_t)40ULL)) goto mn37;
        gm_b16_8 t39;
        GM_ST(void *, (uint8_t *)&t39, (void *)(gm_sphr6fu31dtrl));
        GM_ST(uintptr_t, (uint8_t *)&t39 + 8, (uintptr_t)(2));
        t27 = t39;
        goto me28;
mn37:;
ma30:;
        uint8_t t41 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t41 != ((uint8_t)41ULL)) goto mn40;
        gm_b16_8 t42;
        GM_ST(void *, (uint8_t *)&t42, (void *)(gm_stnlf4pvbut1c));
        GM_ST(uintptr_t, (uint8_t *)&t42 + 8, (uintptr_t)(2));
        t27 = t42;
        goto me28;
mn40:;
ma31:;
        uint8_t t44 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t44 != ((uint8_t)42ULL)) goto mn43;
        gm_b16_8 t45;
        GM_ST(void *, (uint8_t *)&t45, (void *)(gm_s1f440st4mnkp));
        GM_ST(uintptr_t, (uint8_t *)&t45 + 8, (uintptr_t)(1));
        t27 = t45;
        goto me28;
mn43:;
ma32:;
        uint8_t t47 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t47 != ((uint8_t)43ULL)) goto mn46;
        gm_b16_8 t48;
        GM_ST(void *, (uint8_t *)&t48, (void *)(gm_s4g3cajt15dmi));
        GM_ST(uintptr_t, (uint8_t *)&t48 + 8, (uintptr_t)(2));
        t27 = t48;
        goto me28;
mn46:;
ma33:;
        uint8_t t50 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t50 != ((uint8_t)44ULL)) goto mn49;
        gm_b16_8 t51;
        GM_ST(void *, (uint8_t *)&t51, (void *)(gm_sm5be4659ulci));
        GM_ST(uintptr_t, (uint8_t *)&t51 + 8, (uintptr_t)(1));
        t27 = t51;
        goto me28;
mn49:;
ma34:;
        gm_b16_8 t53;
        GM_ST(void *, (uint8_t *)&t53, (void *)(gm_s02ru83e3fffc));
        GM_ST(uintptr_t, (uint8_t *)&t53 + 8, (uintptr_t)(2));
        t27 = t53;
        goto me28;
mn52:;
ma35:;
        gm_trap("no match arm matched the scrutinee", gm_src7hmnlrtmirgt, 258, 19);
me28:;
        gm_l8 = t27;
    }
    {
        void * t54 = gm_l0;
        gm_t88rcucg1b7uk t55 = gm_l3;
        gm_b16_8 t56 = gm_l8;
        gm_b16_8 t57 = gm_l6;
        gm_b16_8 t58 = gm_l7;
        gm_b16_8 t59 = gm_f_vec_compare_mask_1e833o5((void *)NULL, cx, NULL, t54, t55, t56, t57, t58);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
        ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t59);
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
inline __attribute__((aligned(4))) void gm_f_vec_convert_94b6r5u(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2) {
    struct gm_fr_vec_convert_94b6r5u F;
    void * gm_l0; /* fg */
    void * gm_l1; /* e */
    void * gm_l2; /* x */
    gm_tismvprs8r13f gm_l3; /* vp */
    gm_b16_8 gm_l4; /* v */
    gm_b16_8 gm_l5; /* va */
    gm_b16_8 gm_l6; /* tv */
    GM_STACK_CHECK(&F, 474u);
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
        if (((((uint8_t *)&t7))[0] != 0)) {
            gm_t5o0tpv5jka2l t8;
            GM_ST(uint8_t, (uint8_t *)&t8 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t8 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t8 + 4, 0);
            ((uint8_t *)&t8)[0] = 1;
            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t8 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t7) + 8)));
            F.ret = t8;
            goto gm_fret;
        }
        gm_b16_8 t9 = GM_LD(gm_b16_8, (((uint8_t *)&t7) + 8));
        gm_l4 = t9;
    }
    {
        void * t10 = gm_l0;
        void * t11 = gm_l2;
        gm_t88rcucg1b7uk t12 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t11) + 0));
        gm_b16_8 t13 = gm_l4;
        gm_b16_8 t14 = gm_f_vec_load_8m1nglt((void *)NULL, cx, NULL, t10, t12, t13);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l5 = t14;
    }
    {
        void * t15 = gm_l0;
        uint64_t t16 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 0));
        gm_t88rcucg1b7uk t17 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l3) + 8));
        gm_tqs5p4n2tq19v t18;
        memset(&t18, 0, sizeof t18);
        gm_b16_8 t19 = gm_f_vec_ty_adod8ur((void *)NULL, cx, NULL, t15, t16, t17, t18);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l6 = t19;
    }
    {
        void * t20 = gm_l0;
        void * t21 = gm_l1;
        gm_t88rcucg1b7uk t22 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t21) + 0));
        gm_t6j6smj7qoctu t23;
        gm_b16_8 t24 = gm_l5;
        GM_ST(gm_b16_8, (uint8_t *)&t23 + 0, t24);
        gm_b16_8 t25 = gm_l6;
        GM_ST(gm_b16_8, (uint8_t *)&t23 + 16, t25);
        gm_tabb710d9mq2e t26;
        gm_f_format_s8juhic((void *)&t26, (void *)NULL, cx, NULL, &t23);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t26))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t26) + 8, gm_src7hmnlrtmirgt, 385, 31);
        gm_b16_8 t27 = GM_LD(gm_b16_8, (((uint8_t *)&t26) + 8));
        gm_b16_8 t28 = GM_LD(gm_b16_8, ((uint8_t *)&t27));
        gm_b16_8 t29 = gm_f_vec_store_6d0dm94((void *)NULL, cx, NULL, t20, t22, t28);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
        ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t29);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_vec_lane_at_i7st991(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, uint64_t a2, gm_b16_8 a3, gm_b16_8 a4) {
    struct gm_fr_vec_lane_at_i7st991 F;
    void * gm_l0; /* fg */
    gm_t88rcucg1b7uk gm_l1; /* lane */
    uint64_t gm_l2; /* bytes */
    gm_b16_8 gm_l3; /* p */
    gm_b16_8 gm_l4; /* i */
    gm_b16_8 gm_l5; /* ce */
    GM_STACK_CHECK(&F, 341u);
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
        if (((((uint8_t *)&t14))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t14) + 8, gm_src7hmnlrtmirgt, 101, 25);
        gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&t14) + 8));
        gm_b16_8 t16 = GM_LD(gm_b16_8, ((uint8_t *)&t15));
        gm_b16_8 t17 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t7, t8, t16);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t18 = 0;
        F.ret = t17;
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
    GM_STACK_CHECK_SHALLOW(&F, 167u);
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
    uint64_t t6;
    uint8_t t12 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
    switch (t12) { case ((uint8_t)0ULL): goto ma8; case ((uint8_t)1ULL): goto ma10; case ((uint8_t)2ULL): goto ma10; case ((uint8_t)3ULL): goto ma9; case ((uint8_t)4ULL): goto ma10; case ((uint8_t)5ULL): goto ma10; case ((uint8_t)6ULL): goto ma10; case ((uint8_t)7ULL): goto ma10; case ((uint8_t)8ULL): goto ma10; case ((uint8_t)9ULL): goto ma10; case ((uint8_t)10ULL): goto ma10; case ((uint8_t)11ULL): goto ma10; case ((uint8_t)12ULL): goto ma10; case ((uint8_t)13ULL): goto ma10; case ((uint8_t)14ULL): goto ma10; case ((uint8_t)15ULL): goto ma10; case ((uint8_t)16ULL): goto ma10; case ((uint8_t)17ULL): goto ma10; case ((uint8_t)18ULL): goto ma10; case ((uint8_t)19ULL): goto ma10; case ((uint8_t)20ULL): goto ma10; case ((uint8_t)21ULL): goto ma10; case ((uint8_t)22ULL): goto ma10; case ((uint8_t)23ULL): goto ma10; case ((uint8_t)24ULL): goto ma10; case ((uint8_t)25ULL): goto ma10; case ((uint8_t)26ULL): goto ma10; default: goto ma10; }
ma8:;
    uint8_t t14 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
    if (t14 != ((uint8_t)0ULL)) goto mn13;
    uint16_t t15 = GM_LD(uint16_t, ((((uint8_t *)&t5) + 8) + 2));
    gm_l2 = t15;
    uint16_t t16 = gm_l2;
    uint64_t t17 = (uint64_t)t16;
    if (((uint64_t)8ULL) == 0) gm_trap("division by zero", gm_src7hmnlrtmirgt, 22, 22);
    uint64_t t18 = (uint64_t)(t17 / ((uint64_t)8ULL));
    t6 = t18;
    goto me7;
mn13:;
ma9:;
    uint8_t t20 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
    if (t20 != ((uint8_t)3ULL)) goto mn19;
    uint16_t t21 = GM_LD(uint16_t, (((uint8_t *)&t5) + 8));
    gm_l3 = t21;
    uint16_t t22 = gm_l3;
    uint64_t t23 = (uint64_t)t22;
    if (((uint64_t)8ULL) == 0) gm_trap("division by zero", gm_src7hmnlrtmirgt, 23, 21);
    uint64_t t24 = (uint64_t)(t23 / ((uint64_t)8ULL));
    t6 = t24;
    goto me7;
mn19:;
ma10:;
    t6 = ((uint64_t)1ULL);
    goto me7;
mn25:;
ma11:;
    gm_trap("no match arm matched the scrutinee", gm_src7hmnlrtmirgt, 21, 54);
me7:;
    F.ret = t6;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_vec_lane_loop_a3gvooh(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, uint64_t a2, gm_closure a3) {
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
    GM_STACK_CHECK(&F, 832u);
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
        if (((((uint8_t *)&t15))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t15) + 8, gm_src7hmnlrtmirgt, 88, 10);
        gm_b16_8 t16 = GM_LD(gm_b16_8, (((uint8_t *)&t15) + 8));
        gm_l7 = t16;
    }
    {
        void * t17 = gm_l0;
        gm_tmr5oep1578tb t18;
        gm_b16_8 t19 = gm_l7;
        GM_ST(gm_b16_8, (uint8_t *)&t18 + 0, t19);
        gm_b16_8 t20 = gm_l7;
        GM_ST(gm_b16_8, (uint8_t *)&t18 + 16, t20);
        uint64_t t21 = gm_l2;
        GM_ST(uint64_t, (uint8_t *)&t18 + 32, t21);
        gm_b16_8 t22 = gm_l7;
        GM_ST(gm_b16_8, (uint8_t *)&t18 + 40, t22);
        gm_b0_1 t23 = gm_f_linef_gjfdarm((void *)NULL, cx, NULL, t17, &t18);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t24 = gm_l0;
        void * t25 = gm_l0;
        uint64_t t26 = GM_LD(uint64_t, (((uint8_t *)t25) + 224));
        uint64_t r27; bool o28 = __builtin_add_overflow(t26, ((uint64_t)1ULL), &r27);
        if (o28) gm_trap("integer overflow in `+`", gm_src7hmnlrtmirgt, 90, 5);
        uint64_t t29 = r27;
        GM_ST(uint64_t, (((uint8_t *)t24) + 224), t29);
    }
    {
        gm_closure t30 = gm_l3;
        gm_b16_8 t31 = gm_l7;
        gm_b16_8 t32 = GM_LD(gm_b16_8, ((uint8_t *)&t31));
        gm_closure t33 = t30;
        gm_node *ccx34 = t33.cx;
        gm_node cn35; { gm_node *f = gm_find(cx, 587034433); if (f) { cn35 = *f; cn35.next = ccx34; ccx34 = &cn35; } }
        gm_node cn36; { gm_node *f = gm_find(cx, 2126215853); if (f) { cn36 = *f; cn36.next = ccx34; ccx34 = &cn36; } }
        void * t37 = t33.code;
        gm_t5o0tpv5jka2l t38;
        if (GM_PTR_TAGGED(t37)) {
            t38 = ((gm_t5o0tpv5jka2l (*)(gm_b16_8))GM_PTR_UNTAG(t37))(t32);
        } else {
            ((void (*)(void *, void *, gm_node *, gm_node *, gm_b16_8))t37)((void *)&t38, t33.env, ccx34, NULL, t32);
        }
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t38))[0] != 0)) {
            gm_t5o0tpv5jka2l t39;
            GM_ST(uint8_t, (uint8_t *)&t39 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t39 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t39 + 4, 0);
            ((uint8_t *)&t39)[0] = 1;
            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t39 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t38) + 8)));
            F.ret = t39;
            goto gm_fret;
        }
        gm_b16_8 t40 = GM_LD(gm_b16_8, (((uint8_t *)&t38) + 8));
        gm_l8 = t40;
    }
    {
        void * t41 = gm_l0;
        gm_t89vrq05jgle5 t42;
        gm_b16_8 t43 = gm_l5;
        GM_ST(gm_b16_8, (uint8_t *)&t42 + 0, t43);
        gm_b16_8 t44 = gm_l6;
        GM_ST(gm_b16_8, (uint8_t *)&t42 + 16, t44);
        gm_b16_8 t45 = gm_l7;
        GM_ST(gm_b16_8, (uint8_t *)&t42 + 32, t45);
        uint64_t t46 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 16));
        GM_ST(uint64_t, (uint8_t *)&t42 + 48, t46);
        gm_b16_8 t47 = gm_l8;
        GM_ST(gm_b16_8, (uint8_t *)&t42 + 56, t47);
        gm_b0_1 t48 = gm_f_linef_ou0dv81((void *)NULL, cx, NULL, t41, &t42);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t49 = gm_l0;
        void * t50 = gm_l0;
        uint64_t t51 = GM_LD(uint64_t, (((uint8_t *)t50) + 224));
        uint64_t r52; bool o53 = __builtin_sub_overflow(t51, ((uint64_t)1ULL), &r52);
        if (o53) gm_trap("integer overflow in `-`", gm_src7hmnlrtmirgt, 93, 5);
        uint64_t t54 = r52;
        GM_ST(uint64_t, (((uint8_t *)t49) + 224), t54);
    }
    {
        void * t55 = gm_l0;
        gm_b16_8 t56;
        GM_ST(void *, (uint8_t *)&t56, (void *)(gm_s2cuflhqkkuaq));
        GM_ST(uintptr_t, (uint8_t *)&t56 + 8, (uintptr_t)(1));
        gm_b0_1 t57 = gm_f_line_l6u87p9((void *)NULL, cx, NULL, t55, t56);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_b16_8 t58 = gm_l6;
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
        ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t58);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_vec_load_8m1nglt(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2) {
    struct gm_fr_vec_load_8m1nglt F;
    void * gm_l0; /* fg */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_b16_8 gm_l2; /* v */
    gm_tismvprs8r13f gm_l3; /* p */
    gm_b16_8 gm_l4; /* vt */
    gm_b16_8 gm_l5; /* pa */
    gm_b16_8 gm_l6; /* name */
    GM_STACK_CHECK(&F, 483u);
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
        if (((((uint8_t *)&t16))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t16) + 8, gm_src7hmnlrtmirgt, 68, 13);
        gm_b16_8 t17 = GM_LD(gm_b16_8, (((uint8_t *)&t16) + 8));
        gm_l6 = t17;
    }
    {
        void * t18 = gm_l0;
        gm_tud7oovfvnf43 t19;
        gm_b16_8 t20 = gm_l4;
        GM_ST(gm_b16_8, (uint8_t *)&t19 + 0, t20);
        gm_b16_8 t21 = gm_l6;
        GM_ST(gm_b16_8, (uint8_t *)&t19 + 16, t21);
        gm_b16_8 t22 = gm_l6;
        GM_ST(gm_b16_8, (uint8_t *)&t19 + 32, t22);
        gm_b16_8 t23 = gm_l5;
        GM_ST(gm_b16_8, (uint8_t *)&t19 + 48, t23);
        gm_b16_8 t24 = gm_l6;
        GM_ST(gm_b16_8, (uint8_t *)&t19 + 64, t24);
        gm_b0_1 t25 = gm_f_linef_5476lg2((void *)NULL, cx, NULL, t18, &t19);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_b16_8 t26 = gm_l6;
        gm_b16_8 t27 = GM_LD(gm_b16_8, ((uint8_t *)&t26));
        uint8_t t28 = 0;
        F.ret = t27;
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
    GM_STACK_CHECK(&F, 1420u);
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
        gm_t88rcucg1b7uk t13 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t11 * 88) + 0));
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
        void * t21 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t19 * 88);
        void * t22 = t21;
        t15 = t22;
        goto ix16;
ie16:;
        uint64_t t24 = (uintptr_t)((uint64_t)1ULL);
        uint64_t t25 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
        if (t24 >= (uintptr_t)(t25)) gm_trap_index(t24, t25, gm_src7hmnlrtmirgt, 421, 43);
        void * t26 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t24 * 88);
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
            void * t39 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t37 * 88);
            gm_t5o0tpv5jka2l t40;
            gm_f_expr_porbc9t((void *)&t40, (void *)NULL, cx, NULL, t36, t39);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t40))[0] != 0)) {
                gm_t5o0tpv5jka2l t41;
                GM_ST(uint8_t, (uint8_t *)&t41 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t41 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t41 + 4, 0);
                ((uint8_t *)&t41)[0] = 1;
                GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t41 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t40) + 8)));
                F.ret = t41;
                goto gm_fret;
            }
            gm_b16_8 t42 = GM_LD(gm_b16_8, (((uint8_t *)&t40) + 8));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l9)) + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l9)) + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l9)) + 20, 0);
            ((uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l9)))[16] = 1;
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l9)) + 0, t42);
        }
        goto ix32;
ie32:;
ix32:;
    }
    {
        void * t43 = gm_l0;
        void * t44 = gm_l6;
        gm_t5o0tpv5jka2l t45;
        gm_f_expr_porbc9t((void *)&t45, (void *)NULL, cx, NULL, t43, t44);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t45))[0] != 0)) {
            gm_t5o0tpv5jka2l t46;
            GM_ST(uint8_t, (uint8_t *)&t46 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t46 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t46 + 4, 0);
            ((uint8_t *)&t46)[0] = 1;
            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t46 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t45) + 8)));
            F.ret = t46;
            goto gm_fret;
        }
        gm_b16_8 t47 = GM_LD(gm_b16_8, (((uint8_t *)&t45) + 8));
        gm_l10 = t47;
    }
    {
        void * t48 = gm_l0;
        void * t49 = gm_l6;
        gm_t88rcucg1b7uk t50 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t49) + 0));
        gm_b16_8 t51 = gm_l10;
        gm_b16_8 t52 = gm_f_addr_of_hiaqkn0((void *)NULL, cx, NULL, t48, t50, t51);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l11 = t52;
    }
    {
        gm_b16_8 t53 = gm_l11;
        gm_b16_8 t54 = gm_f_slice_ptr_hojfgr5((void *)NULL, cx, NULL, t53);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l12 = t54;
    }
    {
        void * t55 = gm_l0;
        gm_b16_8 t56 = gm_l11;
        gm_b16_8 t57 = gm_f_slice_len_a3383br((void *)NULL, cx, NULL, t55, t56);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l13 = t57;
    }
    {
        void * t59 = gm_l0;
        void * t60 = GM_LD(void *, (((uint8_t *)t59) + 0));
        uint8_t t61 = GM_LD(uint8_t, ((((uint8_t *)t60) + 1136) + 0));
        if (!t61) goto ie58;
        {
            void * t63 = gm_l0;
            void * t64 = gm_l1;
            gm_b16_8 t65 = gm_f_loc_u9c0aqb((void *)NULL, cx, NULL, t63, t64);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l14 = t65;
        }
        {
            gm_b16_8 t66;
            uint8_t t68 = gm_l4;
            if (!t68) goto ie67;
            gm_b16_8 t70;
            GM_ST(void *, (uint8_t *)&t70, (void *)(gm_sqfrv3g5v9m2n));
            GM_ST(uintptr_t, (uint8_t *)&t70 + 8, (uintptr_t)(4));
            gm_b16_8 t71 = t70;
            t66 = t71;
            goto ix67;
ie67:;
            gm_b16_8 t73;
            GM_ST(void *, (uint8_t *)&t73, (void *)(gm_s0ugop4l95o1j));
            GM_ST(uintptr_t, (uint8_t *)&t73 + 8, (uintptr_t)(5));
            gm_b16_8 t74 = t73;
            t66 = t74;
ix67:;
            gm_l15 = t66;
        }
        {
            void * t75 = gm_l0;
            gm_ttq7m9ctqad93 t76;
            gm_b16_8 t77 = gm_l13;
            GM_ST(gm_b16_8, (uint8_t *)&t76 + 0, t77);
            uint64_t t78 = gm_l8;
            GM_ST(uint64_t, (uint8_t *)&t76 + 16, t78);
            gm_b16_8 t79 = gm_l15;
            GM_ST(gm_b16_8, (uint8_t *)&t76 + 24, t79);
            uint64_t t80 = gm_l8;
            GM_ST(uint64_t, (uint8_t *)&t76 + 40, t80);
            gm_b16_8 t81 = gm_l13;
            GM_ST(gm_b16_8, (uint8_t *)&t76 + 48, t81);
            gm_b16_8 t82 = gm_l14;
            GM_ST(gm_b16_8, (uint8_t *)&t76 + 64, t82);
            gm_b0_1 t83 = gm_f_linef_7e7j1ug((void *)NULL, cx, NULL, t75, &t76);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix58;
ie58:;
ix58:;
    }
    {
        uint64_t t84 = gm_l8;
        uint64_t t85 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 16));
        uint64_t r86; bool o87 = __builtin_mul_overflow(t84, t85, &r86);
        if (o87) gm_trap("integer overflow in `*`", gm_src7hmnlrtmirgt, 435, 13);
        uint64_t t88 = r86;
        gm_l16 = t88;
    }
    {
        if (!((((uint8_t *)&gm_l9))[16] != 0)) goto ie89;
        gm_b16_8 t90 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l9));
        gm_l17 = t90;
        {
            void * t92 = gm_l0;
            gm_t88rcucg1b7uk t93 = gm_l5;
            gm_b16_8 t94 = gm_l17;
            gm_b16_8 t95 = gm_f_addr_of_hiaqkn0((void *)NULL, cx, NULL, t92, t93, t94);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l18 = t95;
        }
        {
            void * t96 = gm_l0;
            gm_tftksr4op114e t97;
            gm_b16_8 t98 = gm_l12;
            GM_ST(gm_b16_8, (uint8_t *)&t97 + 0, t98);
            gm_b16_8 t99 = gm_l18;
            GM_ST(gm_b16_8, (uint8_t *)&t97 + 16, t99);
            uint64_t t100 = gm_l16;
            GM_ST(uint64_t, (uint8_t *)&t97 + 32, t100);
            gm_b0_1 t101 = gm_f_linef_4ht2lgi((void *)NULL, cx, NULL, t96, &t97);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t102 = gm_l0;
            gm_b16_8 t103 = gm_f_unit_8ekedsl((void *)NULL, cx, NULL, t102);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
            ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t103);
            goto gm_fdone;
        }
        goto ix89;
ie89:;
ix89:;
    }
    {
        void * t104 = gm_l0;
        gm_t88rcucg1b7uk t105 = gm_l5;
        gm_b16_8 t106 = gm_f_tmp_uninit_jr39s5u((void *)NULL, cx, NULL, t104, t105);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l19 = t106;
    }
    {
        void * t107 = gm_l0;
        gm_tftksr4op114e t108;
        gm_b16_8 t109 = gm_l19;
        GM_ST(gm_b16_8, (uint8_t *)&t108 + 0, t109);
        gm_b16_8 t110 = gm_l12;
        GM_ST(gm_b16_8, (uint8_t *)&t108 + 16, t110);
        uint64_t t111 = gm_l16;
        GM_ST(uint64_t, (uint8_t *)&t108 + 32, t111);
        gm_b0_1 t112 = gm_f_linef_6cjbh5j((void *)NULL, cx, NULL, t107, &t108);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_b16_8 t113 = gm_l19;
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
        ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t113);
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
inline __attribute__((aligned(4))) void gm_f_vec_mask_test_9mgimk6(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_tvdg94bk7aa22 a2, void * a3) {
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
    GM_STACK_CHECK(&F, 759u);
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
        uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)&t5) + 0));
        gm_l4 = t6;
    }
    {
        void * t7 = gm_l0;
        void * t8 = gm_l3;
        gm_t5o0tpv5jka2l t9;
        gm_f_expr_porbc9t((void *)&t9, (void *)NULL, cx, NULL, t7, t8);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t9))[0] != 0)) {
            gm_t5o0tpv5jka2l t10;
            GM_ST(uint8_t, (uint8_t *)&t10 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t10 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t10 + 4, 0);
            ((uint8_t *)&t10)[0] = 1;
            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t10 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t9) + 8)));
            F.ret = t10;
            goto gm_fret;
        }
        gm_b16_8 t11 = GM_LD(gm_b16_8, (((uint8_t *)&t9) + 8));
        gm_l5 = t11;
    }
    {
        void * t12 = gm_l0;
        void * t13 = gm_l3;
        gm_t88rcucg1b7uk t14 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t13) + 0));
        gm_b16_8 t15 = gm_l5;
        gm_b16_8 t16 = gm_f_addr_of_hiaqkn0((void *)NULL, cx, NULL, t12, t14, t15);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l6 = t16;
    }
    {
        void * t17 = gm_l0;
        void * t18 = gm_l0;
        void * t19 = GM_LD(void *, (((uint8_t *)t18) + 0));
        void * t20 = (void *)(((uint8_t *)t19) + 8);
        gm_t88rcucg1b7uk t21 = gm_f_usize_ty_9mijhtc((void *)NULL, cx, NULL, t20);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b16_8 t22;
        GM_ST(void *, (uint8_t *)&t22, (void *)(gm_sicgmpca63jln));
        GM_ST(uintptr_t, (uint8_t *)&t22 + 8, (uintptr_t)(1));
        gm_b16_8 t23 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t17, t21, t22);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l7 = t23;
    }
    {
        void * t24 = gm_l0;
        gm_t7neromnu8t62 t25;
        uint64_t t26 = gm_l4;
        GM_ST(uint64_t, (uint8_t *)&t25 + 0, t26);
        gm_b16_8 t27 = gm_l7;
        GM_ST(gm_b16_8, (uint8_t *)&t25 + 8, t27);
        gm_b16_8 t28 = gm_l6;
        GM_ST(gm_b16_8, (uint8_t *)&t25 + 24, t28);
        gm_b0_1 t29 = gm_f_linef_nac2kse((void *)NULL, cx, NULL, t24, &t25);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_b16_8 t30;
        uint8_t t36 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        switch (t36) { case ((uint8_t)0ULL): goto ma34; case ((uint8_t)1ULL): goto ma34; case ((uint8_t)2ULL): goto ma34; case ((uint8_t)3ULL): goto ma34; case ((uint8_t)4ULL): goto ma34; case ((uint8_t)5ULL): goto ma34; case ((uint8_t)6ULL): goto ma34; case ((uint8_t)7ULL): goto ma34; case ((uint8_t)8ULL): goto ma34; case ((uint8_t)9ULL): goto ma34; case ((uint8_t)10ULL): goto ma34; case ((uint8_t)11ULL): goto ma34; case ((uint8_t)12ULL): goto ma34; case ((uint8_t)13ULL): goto ma34; case ((uint8_t)14ULL): goto ma34; case ((uint8_t)15ULL): goto ma34; case ((uint8_t)16ULL): goto ma34; case ((uint8_t)17ULL): goto ma34; case ((uint8_t)18ULL): goto ma34; case ((uint8_t)19ULL): goto ma34; case ((uint8_t)20ULL): goto ma34; case ((uint8_t)21ULL): goto ma34; case ((uint8_t)22ULL): goto ma34; case ((uint8_t)23ULL): goto ma34; case ((uint8_t)24ULL): goto ma34; case ((uint8_t)25ULL): goto ma34; case ((uint8_t)26ULL): goto ma34; case ((uint8_t)27ULL): goto ma34; case ((uint8_t)28ULL): goto ma34; case ((uint8_t)29ULL): goto ma34; case ((uint8_t)30ULL): goto ma34; case ((uint8_t)31ULL): goto ma34; case ((uint8_t)32ULL): goto ma34; case ((uint8_t)33ULL): goto ma34; case ((uint8_t)34ULL): goto ma34; case ((uint8_t)35ULL): goto ma34; case ((uint8_t)36ULL): goto ma34; case ((uint8_t)37ULL): goto ma34; case ((uint8_t)38ULL): goto ma34; case ((uint8_t)39ULL): goto ma34; case ((uint8_t)40ULL): goto ma34; case ((uint8_t)41ULL): goto ma34; case ((uint8_t)42ULL): goto ma34; case ((uint8_t)43ULL): goto ma34; case ((uint8_t)44ULL): goto ma34; case ((uint8_t)45ULL): goto ma34; case ((uint8_t)46ULL): goto ma34; case ((uint8_t)47ULL): goto ma34; case ((uint8_t)48ULL): goto ma34; case ((uint8_t)49ULL): goto ma32; case ((uint8_t)50ULL): goto ma33; case ((uint8_t)51ULL): goto ma34; case ((uint8_t)52ULL): goto ma34; case ((uint8_t)53ULL): goto ma34; case ((uint8_t)54ULL): goto ma34; case ((uint8_t)55ULL): goto ma34; case ((uint8_t)56ULL): goto ma34; case ((uint8_t)57ULL): goto ma34; case ((uint8_t)58ULL): goto ma34; case ((uint8_t)59ULL): goto ma34; case ((uint8_t)60ULL): goto ma34; case ((uint8_t)61ULL): goto ma34; case ((uint8_t)62ULL): goto ma34; case ((uint8_t)63ULL): goto ma34; case ((uint8_t)64ULL): goto ma34; case ((uint8_t)65ULL): goto ma34; case ((uint8_t)66ULL): goto ma34; case ((uint8_t)67ULL): goto ma34; case ((uint8_t)68ULL): goto ma34; case ((uint8_t)69ULL): goto ma34; case ((uint8_t)70ULL): goto ma34; case ((uint8_t)71ULL): goto ma34; case ((uint8_t)72ULL): goto ma34; case ((uint8_t)73ULL): goto ma34; case ((uint8_t)74ULL): goto ma34; case ((uint8_t)75ULL): goto ma34; case ((uint8_t)76ULL): goto ma34; case ((uint8_t)77ULL): goto ma34; case ((uint8_t)78ULL): goto ma34; case ((uint8_t)79ULL): goto ma34; case ((uint8_t)80ULL): goto ma34; case ((uint8_t)81ULL): goto ma34; default: goto ma34; }
ma32:;
        uint8_t t38 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t38 != ((uint8_t)49ULL)) goto mn37;
        gm_t3877pk7lv748 t39;
        gm_b16_8 t40 = gm_l7;
        GM_ST(gm_b16_8, (uint8_t *)&t39 + 0, t40);
        uint64_t t41 = gm_l4;
        GM_ST(uint64_t, (uint8_t *)&t39 + 16, t41);
        gm_tabb710d9mq2e t42;
        gm_f_format_nvhh8d2((void *)&t42, (void *)NULL, cx, NULL, &t39);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t42))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t42) + 8, gm_src7hmnlrtmirgt, 309, 21);
        gm_b16_8 t43 = GM_LD(gm_b16_8, (((uint8_t *)&t42) + 8));
        gm_b16_8 t44 = GM_LD(gm_b16_8, ((uint8_t *)&t43));
        t30 = t44;
        goto me31;
mn37:;
ma33:;
        uint8_t t46 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t46 != ((uint8_t)50ULL)) goto mn45;
        gm_b16_8 t47 = gm_l7;
        gm_tnpifv20irnph t48;
        GM_ST(gm_b16_8, ((uint8_t *)&t48 + 0), t47);
        gm_tabb710d9mq2e t49;
        gm_f_format_vmsom9v((void *)&t49, (void *)NULL, cx, NULL, t48);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t49))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t49) + 8, gm_src7hmnlrtmirgt, 310, 21);
        gm_b16_8 t50 = GM_LD(gm_b16_8, (((uint8_t *)&t49) + 8));
        gm_b16_8 t51 = GM_LD(gm_b16_8, ((uint8_t *)&t50));
        t30 = t51;
        goto me31;
mn45:;
ma34:;
        gm_b16_8 t53 = gm_l7;
        t30 = t53;
        goto me31;
mn52:;
ma35:;
        gm_trap("no match arm matched the scrutinee", gm_src7hmnlrtmirgt, 308, 17);
me31:;
        gm_l8 = t30;
    }
    {
        void * t54 = gm_l0;
        void * t55 = gm_l1;
        gm_t88rcucg1b7uk t56 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t55) + 0));
        gm_b16_8 t57 = gm_l8;
        gm_b16_8 t58 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t54, t56, t57);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
        ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t58);
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
    GM_STACK_CHECK(&F, 2672u);
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
        void * t12 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t10 * 88);
        gm_t5o0tpv5jka2l t13;
        gm_f_expr_porbc9t((void *)&t13, (void *)NULL, cx, NULL, t9, t12);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t13))[0] != 0)) {
            gm_t5o0tpv5jka2l t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t14 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t13) + 8)));
            F.ret = t14;
            goto gm_fret;
        }
        gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&t13) + 8));
        gm_l9 = t15;
    }
    {
        gm_th2lqbo77pvsc t16;
        memset(&t16, 0, sizeof t16);
        gm_l10 = t16;
    }
    {
        uint64_t t18 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
        uint8_t t19 = (t18 > ((uint64_t)1ULL));
        if (!t19) goto ie17;
        {
            void * t21 = F.l0;
            uint64_t t22 = (uintptr_t)((uint64_t)1ULL);
            uint64_t t23 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
            if (t22 >= (uintptr_t)(t23)) gm_trap_index(t22, t23, gm_src7hmnlrtmirgt, 336, 40);
            void * t24 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t22 * 88);
            gm_t5o0tpv5jka2l t25;
            gm_f_expr_porbc9t((void *)&t25, (void *)NULL, cx, NULL, t21, t24);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t25))[0] != 0)) {
                gm_t5o0tpv5jka2l t26;
                GM_ST(uint8_t, (uint8_t *)&t26 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t26 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t26 + 4, 0);
                ((uint8_t *)&t26)[0] = 1;
                GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t26 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t25) + 8)));
                F.ret = t26;
                goto gm_fret;
            }
            gm_b16_8 t27 = GM_LD(gm_b16_8, (((uint8_t *)&t25) + 8));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l10)) + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l10)) + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l10)) + 20, 0);
            ((uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l10)))[16] = 1;
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l10)) + 0, t27);
        }
        goto ix17;
ie17:;
ix17:;
    }
    {
        uint8_t t28 = ((GM_LD(uint8_t, ((uint8_t *)&F.l2) + 0) == ((uint8_t)55ULL)));
        gm_l11 = t28;
    }
    {
        uint8_t t30 = 0;
        uint8_t t31 = 0;
        uint8_t t32 = gm_l11;
        uint8_t t33 = (uint8_t)!t32;
        if (t33) {
            void * t34 = F.l0;
            void * t35 = GM_LD(void *, (((uint8_t *)t34) + 0));
            void * t36 = (void *)(((uint8_t *)t35) + 8);
            gm_t88rcucg1b7uk t37 = F.l7;
            uint8_t t38 = gm_f_is_float_gthejtb(t36, GM_CONV(gm_csguankko4lmhd, t37));
            t31 = t38;
        } else t31 = 0;
        if (!t31) {
            uint8_t t39 = 0;
            uint8_t t40 = 0;
            uint8_t t41 = gm_l11;
            if (t41) {
                void * t42 = F.l0;
                void * t43 = GM_LD(void *, (((uint8_t *)t42) + 0));
                void * t44 = (void *)(((uint8_t *)t43) + 8);
                gm_t88rcucg1b7uk t45 = F.l7;
                uint8_t t46 = gm_f_is_integer_eo6jkc5(t44, GM_CONV(gm_csguankko4lmhd, t45));
                t40 = t46;
            } else t40 = 0;
            if (t40) {
                void * t47 = F.l0;
                void * t48 = GM_LD(void *, (((uint8_t *)t47) + 0));
                uint8_t t49 = GM_LD(uint8_t, ((((uint8_t *)t48) + 1136) + 0));
                t39 = t49;
            } else t39 = 0;
            t30 = t39;
        } else t30 = 1;
        if (!t30) goto ie29;
        {
            void * t51 = F.l0;
            gm_t88rcucg1b7uk t52 = gm_l4;
            gm_b16_8 t53 = gm_l9;
            gm_b16_8 t54 = gm_f_addr_of_hiaqkn0((void *)NULL, cx, NULL, t51, t52, t53);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.l12 = t54;
        }
        {
            gm_th2lqbo77pvsc t55;
            memset(&t55, 0, sizeof t55);
            F.l13 = t55;
        }
        {
            if (!((((uint8_t *)&gm_l10))[16] != 0)) goto ie56;
            gm_b16_8 t57 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l10));
            gm_l14 = t57;
            {
                void * t59 = F.l0;
                gm_t88rcucg1b7uk t60 = gm_l4;
                gm_b16_8 t61 = gm_l14;
                gm_b16_8 t62 = gm_f_addr_of_hiaqkn0((void *)NULL, cx, NULL, t59, t60, t61);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&F.l13)) + 17, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&F.l13)) + 18, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&F.l13)) + 20, 0);
                ((uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&F.l13)))[16] = 1;
                GM_ST(gm_b16_8, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&F.l13)) + 0, t62);
            }
            goto ix56;
ie56:;
ix56:;
        }
        {
            void * t63 = F.l0;
            void * t64 = gm_l1;
            gm_b16_8 t65 = gm_f_loc_u9c0aqb((void *)NULL, cx, NULL, t63, t64);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.l15 = t65;
        }
        {
            void * t66 = F.l0;
            gm_t88rcucg1b7uk t67 = gm_l4;
            uint64_t t68 = gm_l6;
            gm_closure t69 = ((gm_closure){(void *)gm_f_fn_b1j5nho, (void *)&F.h, cx});
            gm_t5o0tpv5jka2l t70;
            gm_f_vec_lane_loop_a3gvooh((void *)&t70, (void *)NULL, cx, NULL, t66, t67, t68, t69);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t70))[0] != 0)) {
                gm_t5o0tpv5jka2l t71;
                GM_ST(uint8_t, (uint8_t *)&t71 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t71 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t71 + 4, 0);
                ((uint8_t *)&t71)[0] = 1;
                GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t71 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t70) + 8)));
                F.ret = t71;
                goto gm_fret;
            }
            gm_b16_8 t72 = GM_LD(gm_b16_8, (((uint8_t *)&t70) + 8));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
            ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t72);
            goto gm_fdone;
        }
        goto ix29;
ie29:;
ix29:;
    }
    {
        void * t73 = F.l0;
        gm_t88rcucg1b7uk t74 = gm_l4;
        gm_b16_8 t75 = gm_l9;
        gm_b16_8 t76 = gm_f_vec_load_8m1nglt((void *)NULL, cx, NULL, t73, t74, t75);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l16 = t76;
    }
    {
        void * t77 = F.l0;
        uint64_t t78 = gm_l6;
        gm_t88rcucg1b7uk t79 = F.l7;
        gm_tqs5p4n2tq19v t80;
        GM_ST(uint8_t, ((uint8_t *)&t80), ((uint8_t)0));
        ((uint8_t *)&t80)[1] = 1;
        gm_b16_8 t81 = gm_f_vec_ty_adod8ur((void *)NULL, cx, NULL, t77, t78, t79, t80);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l17 = t81;
    }
    {
        void * t82 = F.l0;
        uint64_t t83 = gm_l6;
        gm_t88rcucg1b7uk t84 = F.l7;
        gm_tqs5p4n2tq19v t85;
        memset(&t85, 0, sizeof t85);
        gm_b16_8 t86 = gm_f_vec_ty_adod8ur((void *)NULL, cx, NULL, t82, t83, t84, t85);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l18 = t86;
    }
    {
        gm_b16_8 t87;
        GM_ST(void *, (uint8_t *)&t87, (void *)(gm_sr4qbrk6tlgaa));
        GM_ST(uintptr_t, (uint8_t *)&t87 + 8, (uintptr_t)(0));
        gm_l19 = t87;
    }
    {
        uint8_t t89 = gm_l11;
        if (!t89) goto ie88;
        void * t90 = F.l0;
        void * t91 = GM_LD(void *, (((uint8_t *)t90) + 0));
        void * t92 = (void *)(((uint8_t *)t91) + 8);
        gm_t88rcucg1b7uk t93 = F.l7;
        uint8_t t94 = gm_f_is_float_gthejtb(t92, GM_CONV(gm_csguankko4lmhd, t93));
        if (!t94) goto ie88;
        {
            gm_b16_8 t96;
            uint64_t t98 = F.l8;
            uint8_t t99 = ((t98 == ((uint64_t)4ULL)));
            if (!t99) goto ie97;
            gm_b16_8 t101;
            GM_ST(void *, (uint8_t *)&t101, (void *)(gm_snd30h4tliigb));
            GM_ST(uintptr_t, (uint8_t *)&t101 + 8, (uintptr_t)(11));
            gm_b16_8 t102 = t101;
            t96 = t102;
            goto ix97;
ie97:;
            gm_b16_8 t104;
            GM_ST(void *, (uint8_t *)&t104, (void *)(gm_svaet5ugb1fj3));
            GM_ST(uintptr_t, (uint8_t *)&t104 + 8, (uintptr_t)(21));
            gm_b16_8 t105 = t104;
            t96 = t105;
ix97:;
            gm_l20 = t96;
        }
        {
            gm_t0uuhaljsf410 t106;
            gm_b16_8 t107 = gm_l18;
            GM_ST(gm_b16_8, (uint8_t *)&t106 + 0, t107);
            gm_b16_8 t108 = gm_l17;
            GM_ST(gm_b16_8, (uint8_t *)&t106 + 16, t108);
            gm_b16_8 t109 = gm_l16;
            GM_ST(gm_b16_8, (uint8_t *)&t106 + 32, t109);
            gm_b16_8 t110 = gm_l20;
            GM_ST(gm_b16_8, (uint8_t *)&t106 + 48, t110);
            gm_tabb710d9mq2e t111;
            gm_f_format_l0pq86n((void *)&t111, (void *)NULL, cx, NULL, &t106);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t111))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t111) + 8, gm_src7hmnlrtmirgt, 364, 13);
            gm_b16_8 t112 = GM_LD(gm_b16_8, (((uint8_t *)&t111) + 8));
            gm_b16_8 t113 = GM_LD(gm_b16_8, ((uint8_t *)&t112));
            gm_l19 = t113;
        }
        goto ix88;
ie88:;
        uint8_t t115 = gm_l11;
        if (!t115) goto ie114;
        {
            void * t117 = F.l0;
            uint32_t t118 = gm_f_fresh_5kokjmg(t117);
            gm_t88rcucg1b7uk t119;
            GM_ST(uint32_t, ((uint8_t *)&t119 + 0), t118);
            gm_tabb710d9mq2e t120;
            gm_f_format_61d2klb((void *)&t120, (void *)NULL, cx, NULL, t119);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t120))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t120) + 8, gm_src7hmnlrtmirgt, 366, 15);
            gm_b16_8 t121 = GM_LD(gm_b16_8, (((uint8_t *)&t120) + 8));
            gm_l21 = t121;
        }
        {
            void * t122 = F.l0;
            gm_tttl9nsdo1iu6 t123;
            gm_b16_8 t124 = gm_l18;
            GM_ST(gm_b16_8, (uint8_t *)&t123 + 0, t124);
            gm_b16_8 t125 = gm_l21;
            GM_ST(gm_b16_8, (uint8_t *)&t123 + 16, t125);
            gm_b16_8 t126 = gm_l16;
            GM_ST(gm_b16_8, (uint8_t *)&t123 + 32, t126);
            uint64_t t127 = F.l8;
            uint64_t r128; bool o129 = __builtin_mul_overflow(t127, ((uint64_t)8ULL), &r128);
            if (o129) gm_trap("integer overflow in `*`", gm_src7hmnlrtmirgt, 367, 50);
            uint64_t t130 = r128;
            uint64_t r131; bool o132 = __builtin_sub_overflow(t130, ((uint64_t)1ULL), &r131);
            if (o132) gm_trap("integer overflow in `-`", gm_src7hmnlrtmirgt, 367, 50);
            uint64_t t133 = r131;
            GM_ST(uint64_t, (uint8_t *)&t123 + 48, t133);
            gm_b0_1 t134 = gm_f_linef_7cnqmuh((void *)NULL, cx, NULL, t122, &t123);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_tif3elqk01bdl t135;
            gm_b16_8 t136 = gm_l18;
            GM_ST(gm_b16_8, (uint8_t *)&t135 + 0, t136);
            gm_b16_8 t137 = gm_l17;
            GM_ST(gm_b16_8, (uint8_t *)&t135 + 16, t137);
            gm_b16_8 t138 = gm_l16;
            GM_ST(gm_b16_8, (uint8_t *)&t135 + 32, t138);
            gm_b16_8 t139 = gm_l21;
            GM_ST(gm_b16_8, (uint8_t *)&t135 + 48, t139);
            gm_b16_8 t140 = gm_l17;
            GM_ST(gm_b16_8, (uint8_t *)&t135 + 64, t140);
            gm_b16_8 t141 = gm_l21;
            GM_ST(gm_b16_8, (uint8_t *)&t135 + 80, t141);
            gm_tabb710d9mq2e t142;
            gm_f_format_adcs7to((void *)&t142, (void *)NULL, cx, NULL, &t135);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t142))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t142) + 8, gm_src7hmnlrtmirgt, 368, 13);
            gm_b16_8 t143 = GM_LD(gm_b16_8, (((uint8_t *)&t142) + 8));
            gm_b16_8 t144 = GM_LD(gm_b16_8, ((uint8_t *)&t143));
            gm_l19 = t144;
        }
        goto ix114;
ie114:;
        {
            void * t146 = F.l0;
            gm_t88rcucg1b7uk t147 = gm_l4;
            gm_th2lqbo77pvsc t148 = gm_l10;
            if (!((((uint8_t *)&t148))[16] != 0)) gm_trap("unwrap of null", gm_src7hmnlrtmirgt, 370, 30);
            gm_b16_8 t149 = GM_LD(gm_b16_8, ((uint8_t *)&t148));
            gm_b16_8 t150 = gm_f_vec_load_8m1nglt((void *)NULL, cx, NULL, t146, t147, t149);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l22 = t150;
        }
        {
            gm_b16_8 t151;
            uint8_t t153 = ((GM_LD(uint8_t, ((uint8_t *)&F.l2) + 0) == ((uint8_t)53ULL)));
            if (!t153) goto ie152;
            gm_b16_8 t155;
            GM_ST(void *, (uint8_t *)&t155, (void *)(gm_s1f440st4mnkp));
            GM_ST(uintptr_t, (uint8_t *)&t155 + 8, (uintptr_t)(1));
            gm_b16_8 t156 = t155;
            t151 = t156;
            goto ix152;
ie152:;
            gm_b16_8 t158;
            GM_ST(void *, (uint8_t *)&t158, (void *)(gm_sm5be4659ulci));
            GM_ST(uintptr_t, (uint8_t *)&t158 + 8, (uintptr_t)(1));
            gm_b16_8 t159 = t158;
            t151 = t159;
ix152:;
            gm_l23 = t151;
        }
        {
            void * t160 = F.l0;
            uint64_t t161 = gm_l6;
            gm_t88rcucg1b7uk t162 = F.l7;
            gm_tqs5p4n2tq19v t163;
            GM_ST(uint8_t, ((uint8_t *)&t163), ((uint8_t)1));
            ((uint8_t *)&t163)[1] = 1;
            gm_b16_8 t164 = gm_f_vec_ty_adod8ur((void *)NULL, cx, NULL, t160, t161, t162, t163);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l24 = t164;
        }
        {
            void * t165 = F.l0;
            uint32_t t166 = gm_f_fresh_5kokjmg(t165);
            gm_t88rcucg1b7uk t167;
            GM_ST(uint32_t, ((uint8_t *)&t167 + 0), t166);
            gm_tabb710d9mq2e t168;
            gm_f_format_3tnvb6a((void *)&t168, (void *)NULL, cx, NULL, t167);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t168))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t168) + 8, gm_src7hmnlrtmirgt, 373, 14);
            gm_b16_8 t169 = GM_LD(gm_b16_8, (((uint8_t *)&t168) + 8));
            gm_l25 = t169;
        }
        {
            void * t170 = F.l0;
            gm_th2d9c7aju8hp t171;
            gm_b16_8 t172 = gm_l17;
            GM_ST(gm_b16_8, (uint8_t *)&t171 + 0, t172);
            gm_b16_8 t173 = gm_l25;
            GM_ST(gm_b16_8, (uint8_t *)&t171 + 16, t173);
            gm_b16_8 t174 = gm_l17;
            GM_ST(gm_b16_8, (uint8_t *)&t171 + 32, t174);
            gm_b16_8 t175 = gm_l24;
            GM_ST(gm_b16_8, (uint8_t *)&t171 + 48, t175);
            gm_b16_8 t176 = gm_l16;
            GM_ST(gm_b16_8, (uint8_t *)&t171 + 64, t176);
            gm_b16_8 t177 = gm_l23;
            GM_ST(gm_b16_8, (uint8_t *)&t171 + 80, t177);
            gm_b16_8 t178 = gm_l22;
            GM_ST(gm_b16_8, (uint8_t *)&t171 + 96, t178);
            gm_b0_1 t179 = gm_f_linef_sk93pav((void *)NULL, cx, NULL, t170, &t171);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_th2d9c7aju8hp t180;
            gm_b16_8 t181 = gm_l18;
            GM_ST(gm_b16_8, (uint8_t *)&t180 + 0, t181);
            gm_b16_8 t182 = gm_l17;
            GM_ST(gm_b16_8, (uint8_t *)&t180 + 16, t182);
            gm_b16_8 t183 = gm_l16;
            GM_ST(gm_b16_8, (uint8_t *)&t180 + 32, t183);
            gm_b16_8 t184 = gm_l25;
            GM_ST(gm_b16_8, (uint8_t *)&t180 + 48, t184);
            gm_b16_8 t185 = gm_l17;
            GM_ST(gm_b16_8, (uint8_t *)&t180 + 64, t185);
            gm_b16_8 t186 = gm_l22;
            GM_ST(gm_b16_8, (uint8_t *)&t180 + 80, t186);
            gm_b16_8 t187 = gm_l25;
            GM_ST(gm_b16_8, (uint8_t *)&t180 + 96, t187);
            gm_tabb710d9mq2e t188;
            gm_f_format_l9df9v3((void *)&t188, (void *)NULL, cx, NULL, &t180);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t188))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t188) + 8, gm_src7hmnlrtmirgt, 375, 13);
            gm_b16_8 t189 = GM_LD(gm_b16_8, (((uint8_t *)&t188) + 8));
            gm_b16_8 t190 = GM_LD(gm_b16_8, ((uint8_t *)&t189));
            gm_l19 = t190;
        }
ix114:;
ix88:;
    }
    {
        void * t191 = F.l0;
        gm_t88rcucg1b7uk t192 = gm_l4;
        gm_b16_8 t193 = gm_l19;
        gm_b16_8 t194 = gm_f_vec_store_6d0dm94((void *)NULL, cx, NULL, t191, t192, t193);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
        ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t194);
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
inline __attribute__((aligned(4))) void gm_f_vec_parts_srl0rr9(void *gm_out, void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_vec_parts_srl0rr9 F;
    void * gm_l0; /* fg */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_tlcm4lvu3g1a8 gm_l2; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 136u);
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
        if (!((((uint8_t *)&t7))[16] != 0)) gm_trap("unwrap of null", gm_src7hmnlrtmirgt, 28, 10);
        gm_tlcm4lvu3g1a8 t8 = GM_LD(gm_tlcm4lvu3g1a8, ((uint8_t *)&t7));
        gm_l2 = t8;
    }
    {
        memset((uint8_t *)&(*(gm_tismvprs8r13f *)gm_out) + 12, 0, 4);
        uint64_t t9 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 0));
        gm_t88rcucg1b7uk t10 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l2) + 8));
        void * t11 = gm_l0;
        gm_t88rcucg1b7uk t12 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l2) + 8));
        uint64_t t13 = gm_f_vec_lane_bytes_c5pmsp6(t11, GM_CONV(gm_csguankko4lmhd, t12));
        GM_ST(uint64_t, (uint8_t *)&(*(gm_tismvprs8r13f *)gm_out) + 0, t9);
        GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&(*(gm_tismvprs8r13f *)gm_out) + 8, t10);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_tismvprs8r13f *)gm_out) + 16, t13);
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
    GM_STACK_CHECK(&F, 1992u);
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
        if (((((uint8_t *)&t9))[0] != 0)) {
            gm_t5o0tpv5jka2l t10;
            GM_ST(uint8_t, (uint8_t *)&t10 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t10 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t10 + 4, 0);
            ((uint8_t *)&t10)[0] = 1;
            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t10 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t9) + 8)));
            F.ret = t10;
            goto gm_fret;
        }
        gm_b16_8 t11 = GM_LD(gm_b16_8, (((uint8_t *)&t9) + 8));
        gm_l7 = t11;
    }
    {
        void * t12 = gm_l0;
        gm_t88rcucg1b7uk t13 = gm_l4;
        gm_b16_8 t14 = gm_l7;
        gm_b16_8 t15 = gm_f_addr_of_hiaqkn0((void *)NULL, cx, NULL, t12, t13, t14);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l8 = t15;
    }
    {
        void * t16 = gm_l0;
        gm_t88rcucg1b7uk t17 = gm_l6;
        uint64_t t18 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 16));
        gm_b16_8 t19 = gm_l8;
        gm_b16_8 t20;
        GM_ST(void *, (uint8_t *)&t20, (void *)(gm_sicgmpca63jln));
        GM_ST(uintptr_t, (uint8_t *)&t20 + 8, (uintptr_t)(1));
        gm_b16_8 t21 = gm_f_vec_lane_at_i7st991((void *)NULL, cx, NULL, t16, t17, t18, t19, t20);
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
        gm_f_format_c0h6o29((void *)&t25, (void *)NULL, cx, NULL, t24);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t25))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t25) + 8, gm_src7hmnlrtmirgt, 276, 10);
        gm_b16_8 t26 = GM_LD(gm_b16_8, (((uint8_t *)&t25) + 8));
        gm_l10 = t26;
    }
    {
        void * t27 = gm_l0;
        gm_tmr5oep1578tb t28;
        gm_b16_8 t29 = gm_l10;
        GM_ST(gm_b16_8, (uint8_t *)&t28 + 0, t29);
        gm_b16_8 t30 = gm_l10;
        GM_ST(gm_b16_8, (uint8_t *)&t28 + 16, t30);
        uint64_t t31 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 0));
        GM_ST(uint64_t, (uint8_t *)&t28 + 32, t31);
        gm_b16_8 t32 = gm_l10;
        GM_ST(gm_b16_8, (uint8_t *)&t28 + 40, t32);
        gm_b0_1 t33 = gm_f_linef_6mfrmej((void *)NULL, cx, NULL, t27, &t28);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t34 = gm_l0;
        void * t35 = gm_l0;
        uint64_t t36 = GM_LD(uint64_t, (((uint8_t *)t35) + 224));
        uint64_t r37; bool o38 = __builtin_add_overflow(t36, ((uint64_t)1ULL), &r37);
        if (o38) gm_trap("integer overflow in `+`", gm_src7hmnlrtmirgt, 278, 5);
        uint64_t t39 = r37;
        GM_ST(uint64_t, (((uint8_t *)t34) + 224), t39);
    }
    {
        void * t40 = gm_l0;
        gm_t88rcucg1b7uk t41 = gm_l6;
        uint64_t t42 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 16));
        gm_b16_8 t43 = gm_l8;
        gm_b16_8 t44 = gm_l10;
        gm_b16_8 t45 = GM_LD(gm_b16_8, ((uint8_t *)&t44));
        gm_b16_8 t46 = gm_f_vec_lane_at_i7st991((void *)NULL, cx, NULL, t40, t41, t42, t43, t45);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l11 = t46;
    }
    {
        memset((uint8_t *)&F.l12 + 4, 0, 4);
        memset((uint8_t *)&F.l12 + 84, 0, 4);
        gm_t88rcucg1b7uk t47 = gm_l6;
        GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&F.l12 + 0, t47);
        gm_teti411eujvf4 t48;
        memset(&t48, 0, sizeof t48);
        GM_ST(uint8_t, ((uint8_t *)&t48 + 0), ((uint8_t)8ULL));
        GM_ST(gm_teti411eujvf4, (uint8_t *)&F.l12 + 8, t48);
        void * t49 = gm_l1;
        gm_tl874t2atnlh3 t50 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t49) + 72));
        GM_ST(gm_tl874t2atnlh3, (uint8_t *)&F.l12 + 72, t50);
    }
    {
        gm_b16_8 t51;
        GM_ST(void *, (uint8_t *)&t51, (void *)(gm_sr4qbrk6tlgaa));
        GM_ST(uintptr_t, (uint8_t *)&t51 + 8, (uintptr_t)(0));
        gm_l13 = t51;
    }
    {
        uint8_t t53 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l2) + 0) == ((uint8_t)46ULL)));
        if (!t53) goto ie52;
        {
            void * t56 = gm_l0;
            void * t57 = GM_LD(void *, (((uint8_t *)t56) + 0));
            void * t58 = (void *)(((uint8_t *)t57) + 8);
            gm_t88rcucg1b7uk t59 = gm_l6;
            uint8_t t60 = gm_f_is_float_gthejtb(t58, GM_CONV(gm_csguankko4lmhd, t59));
            if (!t60) goto ie55;
            {
                void * t62 = gm_l0;
                gm_t88rcucg1b7uk t63 = gm_l6;
                gm_t6j6smj7qoctu t64;
                gm_b16_8 t65 = gm_l9;
                GM_ST(gm_b16_8, (uint8_t *)&t64 + 0, t65);
                gm_b16_8 t66 = gm_l11;
                GM_ST(gm_b16_8, (uint8_t *)&t64 + 16, t66);
                gm_tabb710d9mq2e t67;
                gm_f_format_s4jotpe((void *)&t67, (void *)NULL, cx, NULL, &t64);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t67))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t67) + 8, gm_src7hmnlrtmirgt, 284, 30);
                gm_b16_8 t68 = GM_LD(gm_b16_8, (((uint8_t *)&t67) + 8));
                gm_b16_8 t69 = GM_LD(gm_b16_8, ((uint8_t *)&t68));
                gm_b16_8 t70 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t62, t63, t69);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l13 = t70;
            }
            goto ix55;
ie55:;
            {
                void * t72 = gm_l0;
                void * t73 = (void *)((uint8_t *)&F.l12);
                gm_t9qkh0rsooc8d t74;
                GM_ST(uint8_t, ((uint8_t *)&t74 + 0), ((uint8_t)0ULL));
                gm_ts6oqppjh0ba3 t75;
                GM_ST(uint8_t, ((uint8_t *)&t75 + 0), ((uint8_t)0ULL));
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t75 + 1), t74);
                gm_t88rcucg1b7uk t76 = gm_l6;
                gm_b16_8 t77 = gm_l9;
                gm_b16_8 t78 = gm_l11;
                gm_t5o0tpv5jka2l t79;
                gm_f_int_binary_kp3u7i8((void *)&t79, (void *)NULL, cx, NULL, t72, t73, t75, t76, t77, t78);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t79))[0] != 0)) {
                    gm_t5o0tpv5jka2l t80;
                    GM_ST(uint8_t, (uint8_t *)&t80 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t80 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t80 + 4, 0);
                    ((uint8_t *)&t80)[0] = 1;
                    GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t80 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t79) + 8)));
                    F.ret = t80;
                    goto gm_fret;
                }
                gm_b16_8 t81 = GM_LD(gm_b16_8, (((uint8_t *)&t79) + 8));
                gm_l13 = t81;
            }
ix55:;
        }
        goto ix52;
ie52:;
        void * t83 = gm_l0;
        void * t84 = GM_LD(void *, (((uint8_t *)t83) + 0));
        void * t85 = (void *)(((uint8_t *)t84) + 8);
        gm_t88rcucg1b7uk t86 = gm_l6;
        uint8_t t87 = gm_f_is_float_gthejtb(t85, GM_CONV(gm_csguankko4lmhd, t86));
        if (!t87) goto ie82;
        {
            gm_b16_8 t89;
            uint8_t t91 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l2) + 0) == ((uint8_t)47ULL)));
            if (!t91) goto ie90;
            gm_b16_8 t93;
            GM_ST(void *, (uint8_t *)&t93, (void *)(gm_s12ng38ve3205));
            GM_ST(uintptr_t, (uint8_t *)&t93 + 8, (uintptr_t)(7));
            gm_b16_8 t94 = t93;
            t89 = t94;
            goto ix90;
ie90:;
            gm_b16_8 t96;
            GM_ST(void *, (uint8_t *)&t96, (void *)(gm_sgjc0ebf56hf4));
            GM_ST(uintptr_t, (uint8_t *)&t96 + 8, (uintptr_t)(7));
            gm_b16_8 t97 = t96;
            t89 = t97;
ix90:;
            gm_l14 = t89;
        }
        {
            void * t98 = gm_l0;
            gm_t88rcucg1b7uk t99 = gm_l6;
            gm_tfeab279c16rr t100;
            gm_b16_8 t101 = gm_l14;
            GM_ST(gm_b16_8, (uint8_t *)&t100 + 0, t101);
            gm_b16_8 t102 = gm_l9;
            GM_ST(gm_b16_8, (uint8_t *)&t100 + 16, t102);
            gm_b16_8 t103 = gm_l11;
            GM_ST(gm_b16_8, (uint8_t *)&t100 + 32, t103);
            gm_tabb710d9mq2e t104;
            gm_f_format_lqnh3lt((void *)&t104, (void *)NULL, cx, NULL, &t100);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t104))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t104) + 8, gm_src7hmnlrtmirgt, 290, 35);
            gm_b16_8 t105 = GM_LD(gm_b16_8, (((uint8_t *)&t104) + 8));
            gm_b16_8 t106 = GM_LD(gm_b16_8, ((uint8_t *)&t105));
            gm_b16_8 t107 = gm_f_float_from_r6b8fu0((void *)NULL, cx, NULL, t98, t99, t106);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l15 = t107;
        }
        {
            void * t108 = gm_l0;
            gm_t88rcucg1b7uk t109 = gm_l6;
            gm_b16_8 t110 = gm_l15;
            gm_b16_8 t111 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t108, t109, t110);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l13 = t111;
        }
        goto ix82;
ie82:;
        {
            gm_b16_8 t113;
            uint8_t t115 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l2) + 0) == ((uint8_t)47ULL)));
            if (!t115) goto ie114;
            gm_b16_8 t117;
            GM_ST(void *, (uint8_t *)&t117, (void *)(gm_s1f440st4mnkp));
            GM_ST(uintptr_t, (uint8_t *)&t117 + 8, (uintptr_t)(1));
            gm_b16_8 t118 = t117;
            t113 = t118;
            goto ix114;
ie114:;
            gm_b16_8 t120;
            GM_ST(void *, (uint8_t *)&t120, (void *)(gm_sm5be4659ulci));
            GM_ST(uintptr_t, (uint8_t *)&t120 + 8, (uintptr_t)(1));
            gm_b16_8 t121 = t120;
            t113 = t121;
ix114:;
            gm_l16 = t113;
        }
        {
            void * t122 = gm_l0;
            gm_t88rcucg1b7uk t123 = gm_l6;
            gm_tud7oovfvnf43 t124;
            gm_b16_8 t125 = gm_l9;
            GM_ST(gm_b16_8, (uint8_t *)&t124 + 0, t125);
            gm_b16_8 t126 = gm_l16;
            GM_ST(gm_b16_8, (uint8_t *)&t124 + 16, t126);
            gm_b16_8 t127 = gm_l11;
            GM_ST(gm_b16_8, (uint8_t *)&t124 + 32, t127);
            gm_b16_8 t128 = gm_l9;
            GM_ST(gm_b16_8, (uint8_t *)&t124 + 48, t128);
            gm_b16_8 t129 = gm_l11;
            GM_ST(gm_b16_8, (uint8_t *)&t124 + 64, t129);
            gm_tabb710d9mq2e t130;
            gm_f_format_eqjcikj((void *)&t130, (void *)NULL, cx, NULL, &t124);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t130))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t130) + 8, gm_src7hmnlrtmirgt, 294, 26);
            gm_b16_8 t131 = GM_LD(gm_b16_8, (((uint8_t *)&t130) + 8));
            gm_b16_8 t132 = GM_LD(gm_b16_8, ((uint8_t *)&t131));
            gm_b16_8 t133 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t122, t123, t132);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l13 = t133;
        }
ix82:;
ix52:;
    }
    {
        void * t134 = gm_l0;
        gm_t6j6smj7qoctu t135;
        gm_b16_8 t136 = gm_l9;
        GM_ST(gm_b16_8, (uint8_t *)&t135 + 0, t136);
        gm_b16_8 t137 = gm_l13;
        GM_ST(gm_b16_8, (uint8_t *)&t135 + 16, t137);
        gm_b0_1 t138 = gm_f_linef_uu8i6ue((void *)NULL, cx, NULL, t134, &t135);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t139 = gm_l0;
        void * t140 = gm_l0;
        uint64_t t141 = GM_LD(uint64_t, (((uint8_t *)t140) + 224));
        uint64_t r142; bool o143 = __builtin_sub_overflow(t141, ((uint64_t)1ULL), &r142);
        if (o143) gm_trap("integer overflow in `-`", gm_src7hmnlrtmirgt, 297, 5);
        uint64_t t144 = r142;
        GM_ST(uint64_t, (((uint8_t *)t139) + 224), t144);
    }
    {
        void * t145 = gm_l0;
        gm_b16_8 t146;
        GM_ST(void *, (uint8_t *)&t146, (void *)(gm_s2cuflhqkkuaq));
        GM_ST(uintptr_t, (uint8_t *)&t146 + 8, (uintptr_t)(1));
        gm_b0_1 t147 = gm_f_line_l6u87p9((void *)NULL, cx, NULL, t145, t146);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_b16_8 t148 = gm_l9;
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
        ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t148);
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
    GM_STACK_CHECK(&F, 812u);
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
        void * t7 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t5 * 88);
        gm_t5o0tpv5jka2l t8;
        gm_f_expr_porbc9t((void *)&t8, (void *)NULL, cx, NULL, t4, t7);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t8))[0] != 0)) {
            gm_t5o0tpv5jka2l t9;
            GM_ST(uint8_t, (uint8_t *)&t9 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t9 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t9 + 4, 0);
            ((uint8_t *)&t9)[0] = 1;
            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t9 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t8) + 8)));
            F.ret = t9;
            goto gm_fret;
        }
        gm_b16_8 t10 = GM_LD(gm_b16_8, (((uint8_t *)&t8) + 8));
        gm_l4 = t10;
    }
    {
        void * t11 = gm_l0;
        uint64_t t12 = (uintptr_t)((uint64_t)1ULL);
        uint64_t t13 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        if (t12 >= (uintptr_t)(t13)) gm_trap_index(t12, t13, gm_src7hmnlrtmirgt, 319, 23);
        void * t14 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t12 * 88);
        gm_t5o0tpv5jka2l t15;
        gm_f_expr_porbc9t((void *)&t15, (void *)NULL, cx, NULL, t11, t14);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t15))[0] != 0)) {
            gm_t5o0tpv5jka2l t16;
            GM_ST(uint8_t, (uint8_t *)&t16 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t16 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t16 + 4, 0);
            ((uint8_t *)&t16)[0] = 1;
            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t16 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t15) + 8)));
            F.ret = t16;
            goto gm_fret;
        }
        gm_b16_8 t17 = GM_LD(gm_b16_8, (((uint8_t *)&t15) + 8));
        gm_l5 = t17;
    }
    {
        void * t18 = gm_l0;
        uint64_t t19 = (uintptr_t)((uint64_t)2ULL);
        uint64_t t20 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        if (t19 >= (uintptr_t)(t20)) gm_trap_index(t19, t20, gm_src7hmnlrtmirgt, 320, 23);
        void * t21 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t19 * 88);
        gm_t5o0tpv5jka2l t22;
        gm_f_expr_porbc9t((void *)&t22, (void *)NULL, cx, NULL, t18, t21);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t22))[0] != 0)) {
            gm_t5o0tpv5jka2l t23;
            GM_ST(uint8_t, (uint8_t *)&t23 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t23 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t23 + 4, 0);
            ((uint8_t *)&t23)[0] = 1;
            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t23 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t22) + 8)));
            F.ret = t23;
            goto gm_fret;
        }
        gm_b16_8 t24 = GM_LD(gm_b16_8, (((uint8_t *)&t22) + 8));
        gm_l6 = t24;
    }
    {
        void * t25 = gm_l0;
        uint64_t t26 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t27 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        if (t26 >= (uintptr_t)(t27)) gm_trap_index(t26, t27, gm_src7hmnlrtmirgt, 321, 23);
        gm_t88rcucg1b7uk t28 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t26 * 88) + 0));
        gm_b16_8 t29 = gm_l4;
        gm_b16_8 t30 = gm_f_vec_load_8m1nglt((void *)NULL, cx, NULL, t25, t28, t29);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l7 = t30;
    }
    {
        void * t31 = gm_l0;
        gm_t88rcucg1b7uk t32 = gm_l3;
        gm_b16_8 t33 = gm_l5;
        gm_b16_8 t34 = gm_f_vec_load_8m1nglt((void *)NULL, cx, NULL, t31, t32, t33);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l8 = t34;
    }
    {
        void * t35 = gm_l0;
        gm_t88rcucg1b7uk t36 = gm_l3;
        gm_b16_8 t37 = gm_l6;
        gm_b16_8 t38 = gm_f_vec_load_8m1nglt((void *)NULL, cx, NULL, t35, t36, t37);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l9 = t38;
    }
    {
        void * t39 = gm_l0;
        gm_t88rcucg1b7uk t40 = gm_l3;
        gm_b16_8 t41 = gm_l7;
        gm_b16_8 t42 = gm_l8;
        gm_b16_8 t43 = gm_l9;
        gm_b16_8 t44 = gm_f_vec_blend_2oerv54((void *)NULL, cx, NULL, t39, t40, t41, t42, t43);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l10 = t44;
    }
    {
        void * t45 = gm_l0;
        gm_t88rcucg1b7uk t46 = gm_l3;
        gm_b16_8 t47 = gm_l10;
        gm_b16_8 t48 = gm_f_vec_store_6d0dm94((void *)NULL, cx, NULL, t45, t46, t47);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
        ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t48);
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
    GM_STACK_CHECK(&F, 1597u);
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
        gm_t88rcucg1b7uk t4 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t2 * 88) + 0));
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
        uint64_t t13 = GM_LD(uint64_t, (((uint8_t *)&t12) + 0));
        gm_l7 = t13;
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
            uint64_t t15 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
            gm_l9 = t15;
        }
        {
            for (;;) {
                uint64_t t17 = gm_l10;
                uint64_t t18 = gm_l9;
                uint8_t t19 = (t17 < t18);
                if (!t19) goto lb16;
                {
                    uint64_t t22 = gm_l10;
                    uint64_t t25 = GM_LD(uint64_t, ((uint8_t *)&t22));
                    gm_l11 = t25;
                    {
                        uint64_t t28 = gm_l11;
                        uint64_t t29 = (uintptr_t)t28;
                        uint64_t t30 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                        if (t29 >= (uintptr_t)(t30)) gm_trap_index(t29, t30, gm_src7hmnlrtmirgt, 396, 23);
                        uint8_t t31 = GM_LD(uint8_t, ((((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t29 * 88) + 8) + 0));
                        if (t31 != ((uint8_t)0ULL)) goto ie27;
                        gm_tbci5ospk5nnn t32 = GM_LD(gm_tbci5ospk5nnn, ((((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t29 * 88) + 8) + 8));
                        F.l12 = t32;
                        {
                            void * t34 = (void *)((uint8_t *)&F.l8);
                            gm_b16_8 t35 = gm_f_to_decimal_8bshn5b((void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l12)));
                            if (gm_unw.kind) {
                                goto gm_ll20;
                            }
                            gm_b16_8 t36 = GM_LD(gm_b16_8, ((uint8_t *)&t35));
                            gm_tftb1odc739t5 t37 = gm_f_push_3ef746b(t34, t36);
                            if (((((uint8_t *)&t37))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t37) + 1, gm_src7hmnlrtmirgt, 397, 13);
                            gm_b0_1 t38 = GM_LD(gm_b0_1, (((uint8_t *)&t37) + 1));
                        }
                        goto ix27;
ie27:;
                        {
                            void * t40 = (void *)((uint8_t *)&F.l8);
                            gm_b16_8 t41;
                            GM_ST(void *, (uint8_t *)&t41, (void *)(gm_sicgmpca63jln));
                            GM_ST(uintptr_t, (uint8_t *)&t41 + 8, (uintptr_t)(1));
                            gm_tftb1odc739t5 t42 = gm_f_push_3ef746b(t40, t41);
                            if (((((uint8_t *)&t42))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t42) + 1, gm_src7hmnlrtmirgt, 399, 13);
                            gm_b0_1 t43 = GM_LD(gm_b0_1, (((uint8_t *)&t42) + 1));
                        }
ix27:;
                    }
                    goto me23;
mn24:;
                    gm_trap("no match arm matched the scrutinee", gm_src7hmnlrtmirgt, 395, 5);
me23:;
                }
lc16:;
                {
                    uint64_t t45 = gm_l10;
                    uint64_t r46; bool o47 = __builtin_add_overflow(t45, ((uint64_t)1ULL), &r46);
                    uint64_t t48 = r46;
                    gm_l10 = t48;
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
        void * t49 = gm_l0;
        uint64_t t50 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t51 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        if (t50 >= (uintptr_t)(t51)) gm_trap_index(t50, t51, gm_src7hmnlrtmirgt, 402, 23);
        void * t52 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t50 * 88);
        gm_t5o0tpv5jka2l t53;
        gm_f_expr_porbc9t((void *)&t53, (void *)NULL, cx, NULL, t49, t52);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t53))[0] != 0)) {
            gm_t5o0tpv5jka2l t54;
            GM_ST(uint8_t, (uint8_t *)&t54 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t54 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t54 + 4, 0);
            ((uint8_t *)&t54)[0] = 1;
            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t54 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t53) + 8)));
            F.ret = t54;
            goto gm_fret;
        }
        gm_b16_8 t55 = GM_LD(gm_b16_8, (((uint8_t *)&t53) + 8));
        gm_l13 = t55;
    }
    {
        void * t56 = gm_l0;
        gm_t88rcucg1b7uk t57 = gm_l3;
        gm_b16_8 t58 = gm_l13;
        gm_b16_8 t59 = gm_f_vec_load_8m1nglt((void *)NULL, cx, NULL, t56, t57, t58);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l14 = t59;
    }
    {
        void * t60 = gm_l0;
        uint64_t t61 = gm_l7;
        gm_t88rcucg1b7uk t62 = gm_l5;
        gm_tqs5p4n2tq19v t63;
        memset(&t63, 0, sizeof t63);
        gm_b16_8 t64 = gm_f_vec_ty_adod8ur((void *)NULL, cx, NULL, t60, t61, t62, t63);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l15 = t64;
    }
    {
        void * t65 = gm_l0;
        void * t66 = gm_l1;
        gm_t88rcucg1b7uk t67 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t66) + 0));
        gm_b16_8 t68 = gm_f_tmp_uninit_jr39s5u((void *)NULL, cx, NULL, t65, t67);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l16 = t68;
    }
    {
        void * t69 = gm_l0;
        uint32_t t70 = gm_f_fresh_5kokjmg(t69);
        gm_t88rcucg1b7uk t71;
        GM_ST(uint32_t, ((uint8_t *)&t71 + 0), t70);
        gm_tabb710d9mq2e t72;
        gm_f_format_1mojd22((void *)&t72, (void *)NULL, cx, NULL, t71);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t72))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t72) + 8, gm_src7hmnlrtmirgt, 406, 10);
        gm_b16_8 t73 = GM_LD(gm_b16_8, (((uint8_t *)&t72) + 8));
        gm_l17 = t73;
    }
    {
        void * t74 = gm_l0;
        gm_b16_8 t75;
        GM_ST(void *, (uint8_t *)&t75, (void *)(gm_sb22ajpcscr7v));
        GM_ST(uintptr_t, (uint8_t *)&t75 + 8, (uintptr_t)(40));
        gm_b0_1 t76 = gm_f_line_l6u87p9((void *)NULL, cx, NULL, t74, t75);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t77 = gm_l0;
        gm_tud7oovfvnf43 t78;
        gm_b16_8 t79 = gm_l15;
        GM_ST(gm_b16_8, (uint8_t *)&t78 + 0, t79);
        gm_b16_8 t80 = gm_l17;
        GM_ST(gm_b16_8, (uint8_t *)&t78 + 16, t80);
        gm_b16_8 t81 = gm_l14;
        GM_ST(gm_b16_8, (uint8_t *)&t78 + 32, t81);
        gm_b16_8 t82 = gm_l14;
        GM_ST(gm_b16_8, (uint8_t *)&t78 + 48, t82);
        gm_b16_8 t83 = GM_LD(gm_b16_8, (((uint8_t *)&F.l8) + 0));
        gm_b16_8 t84 = GM_LD(gm_b16_8, ((uint8_t *)&t83));
        gm_b16_8 t85;
        GM_ST(void *, (uint8_t *)&t85, (void *)(gm_sg5gdnrith2i6));
        GM_ST(uintptr_t, (uint8_t *)&t85 + 8, (uintptr_t)(2));
        gm_tabb710d9mq2e t86;
        gm_f_join_sfknljb((void *)&t86, (void *)NULL, cx, NULL, t84, t85);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t86))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t86) + 8, gm_src7hmnlrtmirgt, 408, 77);
        gm_b16_8 t87 = GM_LD(gm_b16_8, (((uint8_t *)&t86) + 8));
        GM_ST(gm_b16_8, (uint8_t *)&t78 + 64, t87);
        gm_b0_1 t88 = gm_f_linef_ssnaj62((void *)NULL, cx, NULL, t77, &t78);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t89 = gm_l0;
        gm_tfeab279c16rr t90;
        gm_b16_8 t91 = gm_l16;
        GM_ST(gm_b16_8, (uint8_t *)&t90 + 0, t91);
        gm_b16_8 t92 = gm_l17;
        GM_ST(gm_b16_8, (uint8_t *)&t90 + 16, t92);
        gm_b16_8 t93 = gm_l16;
        GM_ST(gm_b16_8, (uint8_t *)&t90 + 32, t93);
        gm_b0_1 t94 = gm_f_linef_ar0alse((void *)NULL, cx, NULL, t89, &t90);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t95 = gm_l0;
        gm_b16_8 t96;
        GM_ST(void *, (uint8_t *)&t96, (void *)(gm_s0p6vt35dv9nv));
        GM_ST(uintptr_t, (uint8_t *)&t96 + 8, (uintptr_t)(5));
        gm_b0_1 t97 = gm_f_line_l6u87p9((void *)NULL, cx, NULL, t95, t96);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t99 = GM_LD(gm_b16_8, (((uint8_t *)&F.l8) + 0));
            gm_b16_8 t100 = GM_LD(gm_b16_8, ((uint8_t *)&t99));
            gm_l18 = t100;
        }
        {
            gm_l19 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t102 = gm_l19;
                uint64_t t103 = GM_LD(uintptr_t, ((uint8_t *)&gm_l18) + 8);
                uint8_t t104 = (t102 < t103);
                if (!t104) goto lb101;
                {
                    gm_tf1bjg81u0glv t107;
                    uint64_t t108 = gm_l19;
                    GM_ST(uint64_t, (uint8_t *)&t107 + 0, t108);
                    uint64_t t109 = gm_l19;
                    uint64_t t110 = (uintptr_t)t109;
                    uint64_t t111 = GM_LD(uintptr_t, ((uint8_t *)&gm_l18) + 8);
                    gm_b16_8 t112 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l18)) + t110 * 16));
                    GM_ST(gm_b16_8, (uint8_t *)&t107 + 8, t112);
                    uint64_t t115 = GM_LD(uint64_t, (((uint8_t *)&t107) + 0));
                    gm_l20 = t115;
                    gm_b16_8 t116 = GM_LD(gm_b16_8, (((uint8_t *)&t107) + 8));
                    gm_l21 = t116;
                    {
                        void * t118 = gm_l0;
                        gm_tte9sujb6sb62 t119;
                        gm_b16_8 t120 = gm_l16;
                        GM_ST(gm_b16_8, (uint8_t *)&t119 + 0, t120);
                        uint64_t t121 = gm_l20;
                        uint64_t t122 = (uint64_t)t121;
                        uint64_t t123 = gm_l6;
                        uint64_t r124; bool o125 = __builtin_mul_overflow(t122, t123, &r124);
                        if (o125) gm_trap("integer overflow in `*`", gm_src7hmnlrtmirgt, 412, 85);
                        uint64_t t126 = r124;
                        GM_ST(uint64_t, (uint8_t *)&t119 + 16, t126);
                        gm_b16_8 t127 = gm_l14;
                        GM_ST(gm_b16_8, (uint8_t *)&t119 + 24, t127);
                        gm_b16_8 t128 = gm_l21;
                        GM_ST(gm_b16_8, (uint8_t *)&t119 + 40, t128);
                        uint64_t t129 = gm_l6;
                        GM_ST(uint64_t, (uint8_t *)&t119 + 56, t129);
                        uint64_t t130 = gm_l6;
                        GM_ST(uint64_t, (uint8_t *)&t119 + 64, t130);
                        gm_b0_1 t131 = gm_f_linef_pai11tb((void *)NULL, cx, NULL, t118, &t119);
                        if (gm_unw.kind) {
                            goto gm_ll105;
                        }
                    }
                    goto me113;
mn114:;
                    gm_trap("no match arm matched the scrutinee", gm_src7hmnlrtmirgt, 411, 5);
me113:;
                }
lc101:;
                {
                    uint64_t t133 = gm_l19;
                    uint64_t r134; bool o135 = __builtin_add_overflow(t133, ((uint64_t)1ULL), &r134);
                    uint64_t t136 = r134;
                    gm_l19 = t136;
                }
            }
gm_ll105:;
            if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb101; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc101; }
            }
            goto gm_lad;
lb101:;
        }
    }
    {
        void * t137 = gm_l0;
        gm_b16_8 t138;
        GM_ST(void *, (uint8_t *)&t138, (void *)(gm_s9d990kf69gae));
        GM_ST(uintptr_t, (uint8_t *)&t138 + 8, (uintptr_t)(6));
        gm_b0_1 t139 = gm_f_line_l6u87p9((void *)NULL, cx, NULL, t137, t138);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_b16_8 t140 = gm_l16;
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
        ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t140);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_vec_store_6d0dm94(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2) {
    struct gm_fr_vec_store_6d0dm94 F;
    void * gm_l0; /* fg */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_b16_8 gm_l2; /* x */
    gm_b16_8 gm_l3; /* res */
    gm_b16_8 gm_l4; /* n */
    GM_STACK_CHECK(&F, 393u);
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
        if (((((uint8_t *)&t8))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t8) + 8, gm_src7hmnlrtmirgt, 76, 10);
        gm_b16_8 t9 = GM_LD(gm_b16_8, (((uint8_t *)&t8) + 8));
        gm_l4 = t9;
    }
    {
        void * t10 = gm_l0;
        gm_tif3elqk01bdl t11;
        gm_b16_8 t12 = gm_l2;
        GM_ST(gm_b16_8, (uint8_t *)&t11 + 0, t12);
        gm_b16_8 t13 = gm_l4;
        GM_ST(gm_b16_8, (uint8_t *)&t11 + 16, t13);
        gm_b16_8 t14 = gm_l2;
        GM_ST(gm_b16_8, (uint8_t *)&t11 + 32, t14);
        gm_b16_8 t15 = gm_l3;
        GM_ST(gm_b16_8, (uint8_t *)&t11 + 48, t15);
        gm_b16_8 t16 = gm_l4;
        GM_ST(gm_b16_8, (uint8_t *)&t11 + 64, t16);
        gm_b16_8 t17 = gm_l3;
        GM_ST(gm_b16_8, (uint8_t *)&t11 + 80, t17);
        gm_b0_1 t18 = gm_f_linef_4bjepb3((void *)NULL, cx, NULL, t10, &t11);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_b16_8 t19 = gm_l3;
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
    GM_STACK_CHECK(&F, 1116u);
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
                if (((((uint8_t *)&t19))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t19) + 8, gm_src7hmnlrtmirgt, 41, 17);
                gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&t19) + 8));
                gm_b16_8 t21 = GM_LD(gm_b16_8, ((uint8_t *)&t20));
                gm_l5 = t21;
            }
            {
                uint64_t t22 = gm_l8;
                gm_t4ce0jd3tdro4 t23;
                GM_ST(uint64_t, ((uint8_t *)&t23 + 0), t22);
                gm_tabb710d9mq2e t24;
                gm_f_format_27c89bp((void *)&t24, (void *)NULL, cx, NULL, t23);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t24))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t24) + 8, gm_src7hmnlrtmirgt, 42, 19);
                gm_b16_8 t25 = GM_LD(gm_b16_8, (((uint8_t *)&t24) + 8));
                gm_b16_8 t26 = GM_LD(gm_b16_8, ((uint8_t *)&t25));
                gm_l6 = t26;
            }
            goto ix14;
ie14:;
            {
                uint64_t t28 = gm_l8;
                gm_t4ce0jd3tdro4 t29;
                GM_ST(uint64_t, ((uint8_t *)&t29 + 0), t28);
                gm_tabb710d9mq2e t30;
                gm_f_format_368205k((void *)&t30, (void *)NULL, cx, NULL, t29);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t30))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t30) + 8, gm_src7hmnlrtmirgt, 44, 17);
                gm_b16_8 t31 = GM_LD(gm_b16_8, (((uint8_t *)&t30) + 8));
                gm_b16_8 t32 = GM_LD(gm_b16_8, ((uint8_t *)&t31));
                gm_l5 = t32;
            }
            {
                uint64_t t33 = gm_l8;
                gm_t4ce0jd3tdro4 t34;
                GM_ST(uint64_t, ((uint8_t *)&t34 + 0), t33);
                gm_tabb710d9mq2e t35;
                gm_f_format_4f5imdp((void *)&t35, (void *)NULL, cx, NULL, t34);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t35))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t35) + 8, gm_src7hmnlrtmirgt, 45, 19);
                gm_b16_8 t36 = GM_LD(gm_b16_8, (((uint8_t *)&t35) + 8));
                gm_b16_8 t37 = GM_LD(gm_b16_8, ((uint8_t *)&t36));
                gm_l6 = t37;
            }
ix14:;
        }
        goto ix7;
ie7:;
        {
            void * t39 = gm_l0;
            void * t40 = GM_LD(void *, (((uint8_t *)t39) + 0));
            void * t41 = (void *)(((uint8_t *)t40) + 8);
            gm_t88rcucg1b7uk t42 = gm_l2;
            gm_t53at80pfdus3 t43;
            gm_f_kind_ghvav22((void *)&t43, t41, GM_CONV(gm_csguankko4lmhd, t42));
            uint8_t t49 = GM_LD(uint8_t, (((uint8_t *)&t43) + 0));
            switch (t49) { case ((uint8_t)0ULL): goto ma46; case ((uint8_t)1ULL): goto ma47; case ((uint8_t)2ULL): goto ma47; case ((uint8_t)3ULL): goto ma45; case ((uint8_t)4ULL): goto ma47; case ((uint8_t)5ULL): goto ma47; case ((uint8_t)6ULL): goto ma47; case ((uint8_t)7ULL): goto ma47; case ((uint8_t)8ULL): goto ma47; case ((uint8_t)9ULL): goto ma47; case ((uint8_t)10ULL): goto ma47; case ((uint8_t)11ULL): goto ma47; case ((uint8_t)12ULL): goto ma47; case ((uint8_t)13ULL): goto ma47; case ((uint8_t)14ULL): goto ma47; case ((uint8_t)15ULL): goto ma47; case ((uint8_t)16ULL): goto ma47; case ((uint8_t)17ULL): goto ma47; case ((uint8_t)18ULL): goto ma47; case ((uint8_t)19ULL): goto ma47; case ((uint8_t)20ULL): goto ma47; case ((uint8_t)21ULL): goto ma47; case ((uint8_t)22ULL): goto ma47; case ((uint8_t)23ULL): goto ma47; case ((uint8_t)24ULL): goto ma47; case ((uint8_t)25ULL): goto ma47; case ((uint8_t)26ULL): goto ma47; default: goto ma47; }
ma45:;
            uint8_t t51 = GM_LD(uint8_t, (((uint8_t *)&t43) + 0));
            if (t51 != ((uint8_t)3ULL)) goto mn50;
            uint16_t t52 = GM_LD(uint16_t, (((uint8_t *)&t43) + 8));
            gm_l9 = t52;
            {
                gm_b16_8 t54;
                uint16_t t56 = gm_l9;
                uint8_t t57 = ((t56 == ((uint16_t)32ULL)));
                if (!t57) goto ie55;
                gm_b16_8 t59;
                GM_ST(void *, (uint8_t *)&t59, (void *)(gm_s4cess8gum6i7));
                GM_ST(uintptr_t, (uint8_t *)&t59 + 8, (uintptr_t)(5));
                gm_b16_8 t60 = t59;
                t54 = t60;
                goto ix55;
ie55:;
                gm_b16_8 t62;
                GM_ST(void *, (uint8_t *)&t62, (void *)(gm_s866ofni9sb8u));
                GM_ST(uintptr_t, (uint8_t *)&t62 + 8, (uintptr_t)(6));
                gm_b16_8 t63 = t62;
                t54 = t63;
ix55:;
                gm_l5 = t54;
            }
            {
                uint16_t t64 = gm_l9;
                gm_tu0pangbbaeec t65;
                GM_ST(uint16_t, ((uint8_t *)&t65 + 0), t64);
                gm_tabb710d9mq2e t66;
                gm_f_format_h9otq43((void *)&t66, (void *)NULL, cx, NULL, t65);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t66))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t66) + 8, gm_src7hmnlrtmirgt, 51, 23);
                gm_b16_8 t67 = GM_LD(gm_b16_8, (((uint8_t *)&t66) + 8));
                gm_b16_8 t68 = GM_LD(gm_b16_8, ((uint8_t *)&t67));
                gm_l6 = t68;
            }
            goto me44;
mn50:;
ma46:;
            uint8_t t70 = GM_LD(uint8_t, (((uint8_t *)&t43) + 0));
            if (t70 != ((uint8_t)0ULL)) goto mn69;
            uint8_t t71 = GM_LD(uint8_t, ((((uint8_t *)&t43) + 8) + 0));
            gm_l10 = t71;
            uint16_t t72 = GM_LD(uint16_t, ((((uint8_t *)&t43) + 8) + 2));
            gm_l11 = t72;
            {
                gm_tvaas22dl64e0 t74;
                memset((uint8_t *)&t74 + 18, 0, 6);
                gm_b16_8 t75;
                uint8_t t77 = gm_l10;
                if (!t77) goto ie76;
                gm_b16_8 t79;
                GM_ST(void *, (uint8_t *)&t79, (void *)(gm_sr4qbrk6tlgaa));
                GM_ST(uintptr_t, (uint8_t *)&t79 + 8, (uintptr_t)(0));
                gm_b16_8 t80 = t79;
                t75 = t80;
                goto ix76;
ie76:;
                gm_b16_8 t82;
                GM_ST(void *, (uint8_t *)&t82, (void *)(gm_s0m9d22aen6f8));
                GM_ST(uintptr_t, (uint8_t *)&t82 + 8, (uintptr_t)(1));
                gm_b16_8 t83 = t82;
                t75 = t83;
ix76:;
                GM_ST(gm_b16_8, (uint8_t *)&t74 + 0, t75);
                uint16_t t84 = gm_l11;
                GM_ST(uint16_t, (uint8_t *)&t74 + 16, t84);
                gm_tabb710d9mq2e t85;
                gm_f_format_hjgrmth((void *)&t85, (void *)NULL, cx, NULL, &t74);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t85))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t85) + 8, gm_src7hmnlrtmirgt, 54, 21);
                gm_b16_8 t86 = GM_LD(gm_b16_8, (((uint8_t *)&t85) + 8));
                gm_b16_8 t87 = GM_LD(gm_b16_8, ((uint8_t *)&t86));
                gm_l5 = t87;
            }
            {
                gm_tvaas22dl64e0 t88;
                memset((uint8_t *)&t88 + 18, 0, 6);
                gm_b16_8 t89;
                uint8_t t91 = gm_l10;
                if (!t91) goto ie90;
                gm_b16_8 t93;
                GM_ST(void *, (uint8_t *)&t93, (void *)(gm_sitlbs33jqqal));
                GM_ST(uintptr_t, (uint8_t *)&t93 + 8, (uintptr_t)(1));
                gm_b16_8 t94 = t93;
                t89 = t94;
                goto ix90;
ie90:;
                gm_b16_8 t96;
                GM_ST(void *, (uint8_t *)&t96, (void *)(gm_s0m9d22aen6f8));
                GM_ST(uintptr_t, (uint8_t *)&t96 + 8, (uintptr_t)(1));
                gm_b16_8 t97 = t96;
                t89 = t97;
ix90:;
                GM_ST(gm_b16_8, (uint8_t *)&t88 + 0, t89);
                uint16_t t98 = gm_l11;
                GM_ST(uint16_t, (uint8_t *)&t88 + 16, t98);
                gm_tabb710d9mq2e t99;
                gm_f_format_sojmgl9((void *)&t99, (void *)NULL, cx, NULL, &t88);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t99))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t99) + 8, gm_src7hmnlrtmirgt, 55, 23);
                gm_b16_8 t100 = GM_LD(gm_b16_8, (((uint8_t *)&t99) + 8));
                gm_b16_8 t101 = GM_LD(gm_b16_8, ((uint8_t *)&t100));
                gm_l6 = t101;
            }
            goto me44;
mn69:;
ma47:;
            goto me44;
mn102:;
ma48:;
            gm_trap("no match arm matched the scrutinee", gm_src7hmnlrtmirgt, 48, 9);
me44:;
        }
ix7:;
    }
    {
        void * t104 = gm_l0;
        void * t105 = GM_LD(void *, (((uint8_t *)t104) + 0));
        void * t106 = (void *)(((uint8_t *)t105) + 8);
        gm_b16_8 t107 = gm_l5;
        gm_b16_8 t108 = gm_l6;
        uint64_t t109 = gm_l1;
        uint64_t t110 = gm_l4;
        gm_b16_8 t111 = gm_f_vector_typedef_vpgf6u8((void *)NULL, cx, NULL, t106, t107, t108, t109, t110);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t112 = 0;
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
    GM_STACK_CHECK(&F, 2908u);
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
                if (((((uint8_t *)&t68))[0] != 0)) {
                    gm_t5o0tpv5jka2l t69;
                    GM_ST(uint8_t, (uint8_t *)&t69 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t69 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t69 + 4, 0);
                    ((uint8_t *)&t69)[0] = 1;
                    GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t69 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t68) + 8)));
                    F.ret = t69;
                    goto gm_fret;
                }
                gm_b16_8 t70 = GM_LD(gm_b16_8, (((uint8_t *)&t68) + 8));
                gm_l15 = t70;
            }
            {
                void * t71 = F.l0;
                void * t72 = gm_l4;
                gm_t5o0tpv5jka2l t73;
                gm_f_expr_porbc9t((void *)&t73, (void *)NULL, cx, NULL, t71, t72);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t73))[0] != 0)) {
                    gm_t5o0tpv5jka2l t74;
                    GM_ST(uint8_t, (uint8_t *)&t74 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t74 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t74 + 4, 0);
                    ((uint8_t *)&t74)[0] = 1;
                    GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t74 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t73) + 8)));
                    F.ret = t74;
                    goto gm_fret;
                }
                gm_b16_8 t75 = GM_LD(gm_b16_8, (((uint8_t *)&t73) + 8));
                F.l16 = t75;
            }
            {
                void * t76 = F.l0;
                gm_t88rcucg1b7uk t77 = gm_l5;
                gm_b16_8 t78 = gm_l15;
                gm_b16_8 t79 = gm_f_addr_of_hiaqkn0((void *)NULL, cx, NULL, t76, t77, t78);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                F.l17 = t79;
            }
            {
                void * t80 = F.l0;
                gm_t88rcucg1b7uk t81 = gm_l5;
                uint64_t t82 = gm_l7;
                gm_closure t83 = ((gm_closure){(void *)gm_f_fn_tk14v5n, (void *)&F.h, cx});
                gm_t5o0tpv5jka2l t84;
                gm_f_vec_lane_loop_a3gvooh((void *)&t84, (void *)NULL, cx, NULL, t80, t81, t82, t83);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t84))[0] != 0)) {
                    gm_t5o0tpv5jka2l t85;
                    GM_ST(uint8_t, (uint8_t *)&t85 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t85 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t85 + 4, 0);
                    ((uint8_t *)&t85)[0] = 1;
                    GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t85 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t84) + 8)));
                    F.ret = t85;
                    goto gm_fret;
                }
                gm_b16_8 t86 = GM_LD(gm_b16_8, (((uint8_t *)&t84) + 8));
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
                ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
                GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t86);
                goto gm_fdone;
            }
            goto ix52;
ie52:;
ix52:;
        }
        {
            void * t87 = F.l0;
            void * t88 = gm_l3;
            gm_t5o0tpv5jka2l t89;
            gm_f_expr_porbc9t((void *)&t89, (void *)NULL, cx, NULL, t87, t88);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t89))[0] != 0)) {
                gm_t5o0tpv5jka2l t90;
                GM_ST(uint8_t, (uint8_t *)&t90 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t90 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t90 + 4, 0);
                ((uint8_t *)&t90)[0] = 1;
                GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t90 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t89) + 8)));
                F.ret = t90;
                goto gm_fret;
            }
            gm_b16_8 t91 = GM_LD(gm_b16_8, (((uint8_t *)&t89) + 8));
            gm_l18 = t91;
        }
        {
            void * t92 = F.l0;
            void * t93 = gm_l4;
            gm_t5o0tpv5jka2l t94;
            gm_f_expr_porbc9t((void *)&t94, (void *)NULL, cx, NULL, t92, t93);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t94))[0] != 0)) {
                gm_t5o0tpv5jka2l t95;
                GM_ST(uint8_t, (uint8_t *)&t95 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t95 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t95 + 4, 0);
                ((uint8_t *)&t95)[0] = 1;
                GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t95 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t94) + 8)));
                F.ret = t95;
                goto gm_fret;
            }
            gm_b16_8 t96 = GM_LD(gm_b16_8, (((uint8_t *)&t94) + 8));
            gm_l19 = t96;
        }
        {
            void * t97 = F.l0;
            gm_t88rcucg1b7uk t98 = gm_l5;
            gm_b16_8 t99 = gm_l18;
            gm_b16_8 t100 = gm_f_addr_of_hiaqkn0((void *)NULL, cx, NULL, t97, t98, t99);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.l20 = t100;
        }
        {
            void * t101 = F.l0;
            gm_t88rcucg1b7uk t102 = gm_l5;
            gm_b16_8 t103 = gm_l19;
            gm_b16_8 t104 = gm_f_addr_of_hiaqkn0((void *)NULL, cx, NULL, t101, t102, t103);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.l21 = t104;
        }
        {
            void * t105 = F.l0;
            gm_t88rcucg1b7uk t106 = gm_l5;
            uint64_t t107 = gm_l7;
            gm_closure t108 = ((gm_closure){(void *)gm_f_fn_kvivfpi, (void *)&F.h, cx});
            gm_t5o0tpv5jka2l t109;
            gm_f_vec_lane_loop_a3gvooh((void *)&t109, (void *)NULL, cx, NULL, t105, t106, t107, t108);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t109))[0] != 0)) {
                gm_t5o0tpv5jka2l t110;
                GM_ST(uint8_t, (uint8_t *)&t110 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t110 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t110 + 4, 0);
                ((uint8_t *)&t110)[0] = 1;
                GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t110 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t109) + 8)));
                F.ret = t110;
                goto gm_fret;
            }
            gm_b16_8 t111 = GM_LD(gm_b16_8, (((uint8_t *)&t109) + 8));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
            ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t111);
            goto gm_fdone;
        }
        goto ix49;
ie49:;
ix49:;
    }
    {
        void * t112 = F.l0;
        void * t113 = gm_l3;
        gm_t5o0tpv5jka2l t114;
        gm_f_expr_porbc9t((void *)&t114, (void *)NULL, cx, NULL, t112, t113);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t114))[0] != 0)) {
            gm_t5o0tpv5jka2l t115;
            GM_ST(uint8_t, (uint8_t *)&t115 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t115 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t115 + 4, 0);
            ((uint8_t *)&t115)[0] = 1;
            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t115 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t114) + 8)));
            F.ret = t115;
            goto gm_fret;
        }
        gm_b16_8 t116 = GM_LD(gm_b16_8, (((uint8_t *)&t114) + 8));
        gm_l22 = t116;
    }
    {
        void * t117 = F.l0;
        void * t118 = gm_l4;
        gm_t5o0tpv5jka2l t119;
        gm_f_expr_porbc9t((void *)&t119, (void *)NULL, cx, NULL, t117, t118);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t119))[0] != 0)) {
            gm_t5o0tpv5jka2l t120;
            GM_ST(uint8_t, (uint8_t *)&t120 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t120 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t120 + 4, 0);
            ((uint8_t *)&t120)[0] = 1;
            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t120 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t119) + 8)));
            F.ret = t120;
            goto gm_fret;
        }
        gm_b16_8 t121 = GM_LD(gm_b16_8, (((uint8_t *)&t119) + 8));
        gm_l23 = t121;
    }
    {
        void * t122 = F.l0;
        gm_t88rcucg1b7uk t123 = gm_l5;
        gm_b16_8 t124 = gm_l22;
        gm_b16_8 t125 = gm_f_vec_load_8m1nglt((void *)NULL, cx, NULL, t122, t123, t124);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l24 = t125;
    }
    {
        uint8_t t127;
        uint8_t t133 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 0));
        switch (t133) { case ((uint8_t)0ULL): goto ma131; case ((uint8_t)1ULL): goto ma131; case ((uint8_t)2ULL): goto ma131; case ((uint8_t)3ULL): goto ma129; case ((uint8_t)4ULL): goto ma131; case ((uint8_t)5ULL): goto ma131; case ((uint8_t)6ULL): goto ma130; case ((uint8_t)7ULL): goto ma131; case ((uint8_t)8ULL): goto ma131; case ((uint8_t)9ULL): goto ma131; case ((uint8_t)10ULL): goto ma131; case ((uint8_t)11ULL): goto ma131; case ((uint8_t)12ULL): goto ma131; case ((uint8_t)13ULL): goto ma131; default: goto ma131; }
ma129:;
        uint8_t t135 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 0));
        if (t135 != ((uint8_t)3ULL)) goto mn134;
        t127 = ((uint8_t)1);
        goto me128;
mn134:;
ma130:;
        uint8_t t137 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 0));
        if (t137 != ((uint8_t)6ULL)) goto mn136;
        t127 = ((uint8_t)1);
        goto me128;
mn136:;
ma131:;
        t127 = ((uint8_t)0);
        goto me128;
mn138:;
ma132:;
        gm_trap("no match arm matched the scrutinee", gm_src7hmnlrtmirgt, 176, 8);
me128:;
        if (!t127) goto ie126;
        {
            gm_b16_8 t140;
            uint8_t t142 = ((GM_LD(uint8_t, ((uint8_t *)&F.l2) + 0) == ((uint8_t)6ULL)));
            if (!t142) goto ie141;
            gm_b16_8 t144;
            GM_ST(void *, (uint8_t *)&t144, (void *)(gm_s9hi985r6f61m));
            GM_ST(uintptr_t, (uint8_t *)&t144 + 8, (uintptr_t)(2));
            gm_b16_8 t145 = t144;
            t140 = t145;
            goto ix141;
ie141:;
            gm_b16_8 t147;
            GM_ST(void *, (uint8_t *)&t147, (void *)(gm_si62ejtp0vbct));
            GM_ST(uintptr_t, (uint8_t *)&t147 + 8, (uintptr_t)(2));
            gm_b16_8 t148 = t147;
            t140 = t148;
ix141:;
            gm_l25 = t140;
        }
        {
            uint64_t t149 = F.l9;
            uint64_t r150; bool o151 = __builtin_mul_overflow(t149, ((uint64_t)8ULL), &r150);
            if (o151) gm_trap("integer overflow in `*`", gm_src7hmnlrtmirgt, 180, 17);
            uint64_t t152 = r150;
            uint64_t r153; bool o154 = __builtin_sub_overflow(t152, ((uint64_t)1ULL), &r153);
            if (o154) gm_trap("integer overflow in `-`", gm_src7hmnlrtmirgt, 180, 17);
            uint64_t t155 = r153;
            gm_l26 = t155;
        }
        {
            gm_b16_8 t156;
            GM_ST(void *, (uint8_t *)&t156, (void *)(gm_sr4qbrk6tlgaa));
            GM_ST(uintptr_t, (uint8_t *)&t156 + 8, (uintptr_t)(0));
            gm_l27 = t156;
        }
        {
            uint8_t t158 = ((GM_LD(uint8_t, ((uint8_t *)&F.l2) + 0) == ((uint8_t)6ULL)));
            if (!t158) goto ie157;
            {
                gm_tftksr4op114e t160;
                gm_b16_8 t161 = gm_l24;
                GM_ST(gm_b16_8, (uint8_t *)&t160 + 0, t161);
                gm_b16_8 t162 = gm_l23;
                GM_ST(gm_b16_8, (uint8_t *)&t160 + 16, t162);
                uint64_t t163 = gm_l26;
                GM_ST(uint64_t, (uint8_t *)&t160 + 32, t163);
                gm_tabb710d9mq2e t164;
                gm_f_format_t4gbkjg((void *)&t164, (void *)NULL, cx, NULL, &t160);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t164))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t164) + 8, gm_src7hmnlrtmirgt, 183, 17);
                gm_b16_8 t165 = GM_LD(gm_b16_8, (((uint8_t *)&t164) + 8));
                gm_b16_8 t166 = GM_LD(gm_b16_8, ((uint8_t *)&t165));
                gm_l27 = t166;
            }
            goto ix157;
ie157:;
            {
                void * t168 = F.l0;
                uint64_t t169 = gm_l7;
                gm_t88rcucg1b7uk t170 = F.l8;
                gm_tqs5p4n2tq19v t171;
                GM_ST(uint8_t, ((uint8_t *)&t171), ((uint8_t)0));
                ((uint8_t *)&t171)[1] = 1;
                gm_b16_8 t172 = gm_f_vec_ty_adod8ur((void *)NULL, cx, NULL, t168, t169, t170, t171);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l28 = t172;
            }
            {
                void * t173 = F.l0;
                uint64_t t174 = gm_l7;
                gm_t88rcucg1b7uk t175 = F.l8;
                gm_tqs5p4n2tq19v t176;
                memset(&t176, 0, sizeof t176);
                gm_b16_8 t177 = gm_f_vec_ty_adod8ur((void *)NULL, cx, NULL, t173, t174, t175, t176);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l29 = t177;
            }
            {
                gm_tjftu5su6fh8e t178;
                gm_b16_8 t179 = gm_l29;
                GM_ST(gm_b16_8, (uint8_t *)&t178 + 0, t179);
                gm_b16_8 t180 = gm_l28;
                GM_ST(gm_b16_8, (uint8_t *)&t178 + 16, t180);
                gm_b16_8 t181 = gm_l24;
                GM_ST(gm_b16_8, (uint8_t *)&t178 + 32, t181);
                gm_b16_8 t182 = gm_l25;
                GM_ST(gm_b16_8, (uint8_t *)&t178 + 48, t182);
                gm_b16_8 t183 = gm_l23;
                GM_ST(gm_b16_8, (uint8_t *)&t178 + 64, t183);
                uint64_t t184 = gm_l26;
                GM_ST(uint64_t, (uint8_t *)&t178 + 80, t184);
                gm_tabb710d9mq2e t185;
                gm_f_format_cjnp8ds((void *)&t185, (void *)NULL, cx, NULL, &t178);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t185))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t185) + 8, gm_src7hmnlrtmirgt, 187, 17);
                gm_b16_8 t186 = GM_LD(gm_b16_8, (((uint8_t *)&t185) + 8));
                gm_b16_8 t187 = GM_LD(gm_b16_8, ((uint8_t *)&t186));
                gm_l27 = t187;
            }
ix157:;
        }
        {
            void * t188 = F.l0;
            gm_t88rcucg1b7uk t189 = gm_l5;
            gm_b16_8 t190 = gm_l27;
            gm_b16_8 t191 = gm_f_vec_store_6d0dm94((void *)NULL, cx, NULL, t188, t189, t190);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
            ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t191);
            goto gm_fdone;
        }
        goto ix126;
ie126:;
ix126:;
    }
    {
        void * t192 = F.l0;
        gm_t88rcucg1b7uk t193 = gm_l5;
        gm_b16_8 t194 = gm_l23;
        gm_b16_8 t195 = gm_f_vec_load_8m1nglt((void *)NULL, cx, NULL, t192, t193, t194);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l30 = t195;
    }
    {
        gm_b16_8 t196;
        uint8_t t206 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 0));
        switch (t206) { case ((uint8_t)0ULL): goto ma198; case ((uint8_t)1ULL): goto ma199; case ((uint8_t)2ULL): goto ma200; case ((uint8_t)3ULL): goto ma204; case ((uint8_t)4ULL): goto ma201; case ((uint8_t)5ULL): goto ma204; case ((uint8_t)6ULL): goto ma204; case ((uint8_t)7ULL): goto ma202; case ((uint8_t)8ULL): goto ma203; case ((uint8_t)9ULL): goto ma204; case ((uint8_t)10ULL): goto ma204; case ((uint8_t)11ULL): goto ma204; case ((uint8_t)12ULL): goto ma204; case ((uint8_t)13ULL): goto ma204; default: goto ma204; }
ma198:;
        uint8_t t208 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 0));
        if (t208 != ((uint8_t)0ULL)) goto mn207;
        gm_b16_8 t209;
        GM_ST(void *, (uint8_t *)&t209, (void *)(gm_sajhum9b4nr96));
        GM_ST(uintptr_t, (uint8_t *)&t209 + 8, (uintptr_t)(1));
        t196 = t209;
        goto me197;
mn207:;
ma199:;
        uint8_t t211 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 0));
        if (t211 != ((uint8_t)1ULL)) goto mn210;
        gm_b16_8 t212;
        GM_ST(void *, (uint8_t *)&t212, (void *)(gm_skolmnfc0ccbd));
        GM_ST(uintptr_t, (uint8_t *)&t212 + 8, (uintptr_t)(1));
        t196 = t212;
        goto me197;
mn210:;
ma200:;
        uint8_t t214 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 0));
        if (t214 != ((uint8_t)2ULL)) goto mn213;
        gm_b16_8 t215;
        GM_ST(void *, (uint8_t *)&t215, (void *)(gm_s6s96daqmhcs9));
        GM_ST(uintptr_t, (uint8_t *)&t215 + 8, (uintptr_t)(1));
        t196 = t215;
        goto me197;
mn213:;
ma201:;
        uint8_t t217 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 0));
        if (t217 != ((uint8_t)4ULL)) goto mn216;
        gm_b16_8 t218;
        GM_ST(void *, (uint8_t *)&t218, (void *)(gm_st3ja0nqi37bv));
        GM_ST(uintptr_t, (uint8_t *)&t218 + 8, (uintptr_t)(1));
        t196 = t218;
        goto me197;
mn216:;
ma202:;
        uint8_t t220 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 0));
        if (t220 != ((uint8_t)7ULL)) goto mn219;
        gm_b16_8 t221;
        GM_ST(void *, (uint8_t *)&t221, (void *)(gm_smerqm8g793l6));
        GM_ST(uintptr_t, (uint8_t *)&t221 + 8, (uintptr_t)(1));
        t196 = t221;
        goto me197;
mn219:;
ma203:;
        uint8_t t223 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 0));
        if (t223 != ((uint8_t)8ULL)) goto mn222;
        gm_b16_8 t224;
        GM_ST(void *, (uint8_t *)&t224, (void *)(gm_s2u5posfkbgnq));
        GM_ST(uintptr_t, (uint8_t *)&t224 + 8, (uintptr_t)(1));
        t196 = t224;
        goto me197;
mn222:;
ma204:;
        gm_b16_8 t226;
        GM_ST(void *, (uint8_t *)&t226, (void *)(gm_srs1vrn2uaams));
        GM_ST(uintptr_t, (uint8_t *)&t226 + 8, (uintptr_t)(1));
        t196 = t226;
        goto me197;
mn225:;
ma205:;
        gm_trap("no match arm matched the scrutinee", gm_src7hmnlrtmirgt, 192, 19);
me197:;
        gm_l31 = t196;
    }
    {
        gm_b16_8 t227;
        GM_ST(void *, (uint8_t *)&t227, (void *)(gm_sr4qbrk6tlgaa));
        GM_ST(uintptr_t, (uint8_t *)&t227 + 8, (uintptr_t)(0));
        gm_l32 = t227;
    }
    {
        void * t229 = F.l0;
        void * t230 = GM_LD(void *, (((uint8_t *)t229) + 0));
        void * t231 = (void *)(((uint8_t *)t230) + 8);
        gm_t88rcucg1b7uk t232 = F.l8;
        uint8_t t233 = gm_f_is_integer_eo6jkc5(t231, GM_CONV(gm_csguankko4lmhd, t232));
        if (!t233) goto ie228;
        uint8_t t234;
        uint8_t t241 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 0));
        switch (t241) { case ((uint8_t)0ULL): goto ma236; case ((uint8_t)1ULL): goto ma237; case ((uint8_t)2ULL): goto ma238; case ((uint8_t)3ULL): goto ma239; case ((uint8_t)4ULL): goto ma239; case ((uint8_t)5ULL): goto ma239; case ((uint8_t)6ULL): goto ma239; case ((uint8_t)7ULL): goto ma239; case ((uint8_t)8ULL): goto ma239; case ((uint8_t)9ULL): goto ma239; case ((uint8_t)10ULL): goto ma239; case ((uint8_t)11ULL): goto ma239; case ((uint8_t)12ULL): goto ma239; case ((uint8_t)13ULL): goto ma239; default: goto ma239; }
ma236:;
        uint8_t t243 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 0));
        if (t243 != ((uint8_t)0ULL)) goto mn242;
        t234 = ((uint8_t)1);
        goto me235;
mn242:;
ma237:;
        uint8_t t245 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 0));
        if (t245 != ((uint8_t)1ULL)) goto mn244;
        t234 = ((uint8_t)1);
        goto me235;
mn244:;
ma238:;
        uint8_t t247 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 0));
        if (t247 != ((uint8_t)2ULL)) goto mn246;
        t234 = ((uint8_t)1);
        goto me235;
mn246:;
ma239:;
        t234 = ((uint8_t)0);
        goto me235;
mn248:;
ma240:;
        gm_trap("no match arm matched the scrutinee", gm_src7hmnlrtmirgt, 202, 37);
me235:;
        if (!t234) goto ie228;
        {
            void * t250 = F.l0;
            uint64_t t251 = gm_l7;
            gm_t88rcucg1b7uk t252 = F.l8;
            gm_tqs5p4n2tq19v t253;
            GM_ST(uint8_t, ((uint8_t *)&t253), ((uint8_t)0));
            ((uint8_t *)&t253)[1] = 1;
            gm_b16_8 t254 = gm_f_vec_ty_adod8ur((void *)NULL, cx, NULL, t250, t251, t252, t253);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l33 = t254;
        }
        {
            void * t255 = F.l0;
            uint64_t t256 = gm_l7;
            gm_t88rcucg1b7uk t257 = F.l8;
            gm_tqs5p4n2tq19v t258;
            memset(&t258, 0, sizeof t258);
            gm_b16_8 t259 = gm_f_vec_ty_adod8ur((void *)NULL, cx, NULL, t255, t256, t257, t258);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l34 = t259;
        }
        {
            gm_tif3elqk01bdl t260;
            gm_b16_8 t261 = gm_l34;
            GM_ST(gm_b16_8, (uint8_t *)&t260 + 0, t261);
            gm_b16_8 t262 = gm_l33;
            GM_ST(gm_b16_8, (uint8_t *)&t260 + 16, t262);
            gm_b16_8 t263 = gm_l24;
            GM_ST(gm_b16_8, (uint8_t *)&t260 + 32, t263);
            gm_b16_8 t264 = gm_l31;
            GM_ST(gm_b16_8, (uint8_t *)&t260 + 48, t264);
            gm_b16_8 t265 = gm_l33;
            GM_ST(gm_b16_8, (uint8_t *)&t260 + 64, t265);
            gm_b16_8 t266 = gm_l30;
            GM_ST(gm_b16_8, (uint8_t *)&t260 + 80, t266);
            gm_tabb710d9mq2e t267;
            gm_f_format_21ahlbb((void *)&t267, (void *)NULL, cx, NULL, &t260);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t267))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t267) + 8, gm_src7hmnlrtmirgt, 206, 13);
            gm_b16_8 t268 = GM_LD(gm_b16_8, (((uint8_t *)&t267) + 8));
            gm_b16_8 t269 = GM_LD(gm_b16_8, ((uint8_t *)&t268));
            gm_l32 = t269;
        }
        goto ix228;
ie228:;
        {
            gm_tfeab279c16rr t271;
            gm_b16_8 t272 = gm_l24;
            GM_ST(gm_b16_8, (uint8_t *)&t271 + 0, t272);
            gm_b16_8 t273 = gm_l31;
            GM_ST(gm_b16_8, (uint8_t *)&t271 + 16, t273);
            gm_b16_8 t274 = gm_l30;
            GM_ST(gm_b16_8, (uint8_t *)&t271 + 32, t274);
            gm_tabb710d9mq2e t275;
            gm_f_format_mdlbfc1((void *)&t275, (void *)NULL, cx, NULL, &t271);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t275))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t275) + 8, gm_src7hmnlrtmirgt, 208, 13);
            gm_b16_8 t276 = GM_LD(gm_b16_8, (((uint8_t *)&t275) + 8));
            gm_b16_8 t277 = GM_LD(gm_b16_8, ((uint8_t *)&t276));
            gm_l32 = t277;
        }
ix228:;
    }
    {
        void * t278 = F.l0;
        gm_t88rcucg1b7uk t279 = gm_l5;
        gm_b16_8 t280 = gm_l32;
        gm_b16_8 t281 = gm_f_vec_store_6d0dm94((void *)NULL, cx, NULL, t278, t279, t280);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
        ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t281);
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
__attribute__((aligned(4))) void gm_f_vector_builtin_a1f1ati(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_tvdg94bk7aa22 a2, gm_b16_8 a3) {
    struct gm_fr_vector_builtin_a1f1ati F;
    void * gm_l0; /* fg */
    void * gm_l1; /* e */
    gm_tvdg94bk7aa22 gm_l2; /* op */
    gm_b16_8 gm_l3; /* args */
    GM_STACK_CHECK(&F, 1381u);
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
        if (((((uint8_t *)&t31))[0] != 0)) {
            gm_t6j4d6ei14c2t t32;
            GM_ST(uint8_t, (uint8_t *)&t32 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t32 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t32 + 4, 0);
            ((uint8_t *)&t32)[0] = 1;
            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t32 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t31) + 8)));
            F.ret = t32;
            goto gm_fret;
        }
        gm_b16_8 t33 = GM_LD(gm_b16_8, (((uint8_t *)&t31) + 8));
        gm_th2lqbo77pvsc t34;
        GM_ST(uint8_t, (uint8_t *)&t34 + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&t34 + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&t34 + 20, 0);
        GM_ST(gm_b16_8, ((uint8_t *)&t34), t33);
        ((uint8_t *)&t34)[16] = 1;
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t6j4d6ei14c2t *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t6j4d6ei14c2t *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t6j4d6ei14c2t *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t6j4d6ei14c2t *)gm_out) + 32, 0);
        ((uint8_t *)&(*(gm_t6j4d6ei14c2t *)gm_out))[0] = 0;
        GM_ST(gm_th2lqbo77pvsc, (uint8_t *)&(*(gm_t6j4d6ei14c2t *)gm_out) + 8, t34);
        goto gm_fdone;
        goto me2;
mn14:;
ma4:;
        uint8_t t38 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t38 != ((uint8_t)46ULL)) goto pa37;
        goto po36;
pa37:;
        uint8_t t40 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t40 != ((uint8_t)47ULL)) goto pa39;
        goto po36;
pa39:;
        uint8_t t42 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t42 != ((uint8_t)48ULL)) goto pa41;
        goto po36;
pa41:;
        goto mn35;
po36:;
        void * t43 = gm_l0;
        void * t44 = gm_l1;
        gm_tvdg94bk7aa22 t45 = gm_l2;
        uint64_t t46 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t47 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
        if (t46 >= (uintptr_t)(t47)) gm_trap_index(t46, t47, gm_src7hmnlrtmirgt, 241, 80);
        void * t48 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t46 * 88);
        gm_t5o0tpv5jka2l t49;
        gm_f_vec_reduce_m5gdqn2((void *)&t49, (void *)NULL, cx, NULL, t43, t44, t45, t48);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t49))[0] != 0)) {
            gm_t6j4d6ei14c2t t50;
            GM_ST(uint8_t, (uint8_t *)&t50 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t50 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t50 + 4, 0);
            ((uint8_t *)&t50)[0] = 1;
            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t50 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t49) + 8)));
            F.ret = t50;
            goto gm_fret;
        }
        gm_b16_8 t51 = GM_LD(gm_b16_8, (((uint8_t *)&t49) + 8));
        gm_th2lqbo77pvsc t52;
        GM_ST(uint8_t, (uint8_t *)&t52 + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&t52 + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&t52 + 20, 0);
        GM_ST(gm_b16_8, ((uint8_t *)&t52), t51);
        ((uint8_t *)&t52)[16] = 1;
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t6j4d6ei14c2t *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t6j4d6ei14c2t *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t6j4d6ei14c2t *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t6j4d6ei14c2t *)gm_out) + 32, 0);
        ((uint8_t *)&(*(gm_t6j4d6ei14c2t *)gm_out))[0] = 0;
        GM_ST(gm_th2lqbo77pvsc, (uint8_t *)&(*(gm_t6j4d6ei14c2t *)gm_out) + 8, t52);
        goto gm_fdone;
        goto me2;
mn35:;
ma5:;
        uint8_t t56 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t56 != ((uint8_t)49ULL)) goto pa55;
        goto po54;
pa55:;
        uint8_t t58 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t58 != ((uint8_t)50ULL)) goto pa57;
        goto po54;
pa57:;
        uint8_t t60 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t60 != ((uint8_t)51ULL)) goto pa59;
        goto po54;
pa59:;
        goto mn53;
po54:;
        void * t61 = gm_l0;
        void * t62 = gm_l1;
        gm_tvdg94bk7aa22 t63 = gm_l2;
        uint64_t t64 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t65 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
        if (t64 >= (uintptr_t)(t65)) gm_trap_index(t64, t65, gm_src7hmnlrtmirgt, 242, 79);
        void * t66 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t64 * 88);
        gm_t5o0tpv5jka2l t67;
        gm_f_vec_mask_test_9mgimk6((void *)&t67, (void *)NULL, cx, NULL, t61, t62, t63, t66);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t67))[0] != 0)) {
            gm_t6j4d6ei14c2t t68;
            GM_ST(uint8_t, (uint8_t *)&t68 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t68 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t68 + 4, 0);
            ((uint8_t *)&t68)[0] = 1;
            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t68 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t67) + 8)));
            F.ret = t68;
            goto gm_fret;
        }
        gm_b16_8 t69 = GM_LD(gm_b16_8, (((uint8_t *)&t67) + 8));
        gm_th2lqbo77pvsc t70;
        GM_ST(uint8_t, (uint8_t *)&t70 + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&t70 + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&t70 + 20, 0);
        GM_ST(gm_b16_8, ((uint8_t *)&t70), t69);
        ((uint8_t *)&t70)[16] = 1;
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t6j4d6ei14c2t *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t6j4d6ei14c2t *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t6j4d6ei14c2t *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t6j4d6ei14c2t *)gm_out) + 32, 0);
        ((uint8_t *)&(*(gm_t6j4d6ei14c2t *)gm_out))[0] = 0;
        GM_ST(gm_th2lqbo77pvsc, (uint8_t *)&(*(gm_t6j4d6ei14c2t *)gm_out) + 8, t70);
        goto gm_fdone;
        goto me2;
mn53:;
ma6:;
        uint8_t t72 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t72 != ((uint8_t)52ULL)) goto mn71;
        void * t73 = gm_l0;
        void * t74 = gm_l1;
        gm_b16_8 t75 = gm_l3;
        gm_t5o0tpv5jka2l t76;
        gm_f_vec_select_3f37tvf((void *)&t76, (void *)NULL, cx, NULL, t73, t74, t75);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t76))[0] != 0)) {
            gm_t6j4d6ei14c2t t77;
            GM_ST(uint8_t, (uint8_t *)&t77 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t77 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t77 + 4, 0);
            ((uint8_t *)&t77)[0] = 1;
            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t77 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t76) + 8)));
            F.ret = t77;
            goto gm_fret;
        }
        gm_b16_8 t78 = GM_LD(gm_b16_8, (((uint8_t *)&t76) + 8));
        gm_th2lqbo77pvsc t79;
        GM_ST(uint8_t, (uint8_t *)&t79 + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&t79 + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&t79 + 20, 0);
        GM_ST(gm_b16_8, ((uint8_t *)&t79), t78);
        ((uint8_t *)&t79)[16] = 1;
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t6j4d6ei14c2t *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t6j4d6ei14c2t *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t6j4d6ei14c2t *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t6j4d6ei14c2t *)gm_out) + 32, 0);
        ((uint8_t *)&(*(gm_t6j4d6ei14c2t *)gm_out))[0] = 0;
        GM_ST(gm_th2lqbo77pvsc, (uint8_t *)&(*(gm_t6j4d6ei14c2t *)gm_out) + 8, t79);
        goto gm_fdone;
        goto me2;
mn71:;
ma7:;
        uint8_t t83 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t83 != ((uint8_t)53ULL)) goto pa82;
        goto po81;
pa82:;
        uint8_t t85 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t85 != ((uint8_t)54ULL)) goto pa84;
        goto po81;
pa84:;
        uint8_t t87 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t87 != ((uint8_t)55ULL)) goto pa86;
        goto po81;
pa86:;
        goto mn80;
po81:;
        void * t88 = gm_l0;
        void * t89 = gm_l1;
        gm_tvdg94bk7aa22 t90 = gm_l2;
        gm_b16_8 t91 = gm_l3;
        gm_t5o0tpv5jka2l t92;
        gm_f_vec_minmax_abs_4le8bpj((void *)&t92, (void *)NULL, cx, NULL, t88, t89, t90, t91);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t92))[0] != 0)) {
            gm_t6j4d6ei14c2t t93;
            GM_ST(uint8_t, (uint8_t *)&t93 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t93 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t93 + 4, 0);
            ((uint8_t *)&t93)[0] = 1;
            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t93 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t92) + 8)));
            F.ret = t93;
            goto gm_fret;
        }
        gm_b16_8 t94 = GM_LD(gm_b16_8, (((uint8_t *)&t92) + 8));
        gm_th2lqbo77pvsc t95;
        GM_ST(uint8_t, (uint8_t *)&t95 + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&t95 + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&t95 + 20, 0);
        GM_ST(gm_b16_8, ((uint8_t *)&t95), t94);
        ((uint8_t *)&t95)[16] = 1;
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t6j4d6ei14c2t *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t6j4d6ei14c2t *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t6j4d6ei14c2t *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t6j4d6ei14c2t *)gm_out) + 32, 0);
        ((uint8_t *)&(*(gm_t6j4d6ei14c2t *)gm_out))[0] = 0;
        GM_ST(gm_th2lqbo77pvsc, (uint8_t *)&(*(gm_t6j4d6ei14c2t *)gm_out) + 8, t95);
        goto gm_fdone;
        goto me2;
mn80:;
ma8:;
        uint8_t t97 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t97 != ((uint8_t)56ULL)) goto mn96;
        void * t98 = gm_l0;
        void * t99 = gm_l1;
        uint64_t t100 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t101 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
        if (t100 >= (uintptr_t)(t101)) gm_trap_index(t100, t101, gm_src7hmnlrtmirgt, 245, 55);
        void * t102 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t100 * 88);
        gm_t5o0tpv5jka2l t103;
        gm_f_vec_convert_94b6r5u((void *)&t103, (void *)NULL, cx, NULL, t98, t99, t102);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t103))[0] != 0)) {
            gm_t6j4d6ei14c2t t104;
            GM_ST(uint8_t, (uint8_t *)&t104 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t104 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t104 + 4, 0);
            ((uint8_t *)&t104)[0] = 1;
            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t104 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t103) + 8)));
            F.ret = t104;
            goto gm_fret;
        }
        gm_b16_8 t105 = GM_LD(gm_b16_8, (((uint8_t *)&t103) + 8));
        gm_th2lqbo77pvsc t106;
        GM_ST(uint8_t, (uint8_t *)&t106 + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&t106 + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&t106 + 20, 0);
        GM_ST(gm_b16_8, ((uint8_t *)&t106), t105);
        ((uint8_t *)&t106)[16] = 1;
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t6j4d6ei14c2t *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t6j4d6ei14c2t *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t6j4d6ei14c2t *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t6j4d6ei14c2t *)gm_out) + 32, 0);
        ((uint8_t *)&(*(gm_t6j4d6ei14c2t *)gm_out))[0] = 0;
        GM_ST(gm_th2lqbo77pvsc, (uint8_t *)&(*(gm_t6j4d6ei14c2t *)gm_out) + 8, t106);
        goto gm_fdone;
        goto me2;
mn96:;
ma9:;
        uint8_t t108 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t108 != ((uint8_t)57ULL)) goto mn107;
        void * t109 = gm_l0;
        void * t110 = gm_l1;
        gm_b16_8 t111 = gm_l3;
        gm_t5o0tpv5jka2l t112;
        gm_f_vec_shuffle_j6jmffl((void *)&t112, (void *)NULL, cx, NULL, t109, t110, t111);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t112))[0] != 0)) {
            gm_t6j4d6ei14c2t t113;
            GM_ST(uint8_t, (uint8_t *)&t113 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t113 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t113 + 4, 0);
            ((uint8_t *)&t113)[0] = 1;
            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t113 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t112) + 8)));
            F.ret = t113;
            goto gm_fret;
        }
        gm_b16_8 t114 = GM_LD(gm_b16_8, (((uint8_t *)&t112) + 8));
        gm_th2lqbo77pvsc t115;
        GM_ST(uint8_t, (uint8_t *)&t115 + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&t115 + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&t115 + 20, 0);
        GM_ST(gm_b16_8, ((uint8_t *)&t115), t114);
        ((uint8_t *)&t115)[16] = 1;
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t6j4d6ei14c2t *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t6j4d6ei14c2t *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t6j4d6ei14c2t *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t6j4d6ei14c2t *)gm_out) + 32, 0);
        ((uint8_t *)&(*(gm_t6j4d6ei14c2t *)gm_out))[0] = 0;
        GM_ST(gm_th2lqbo77pvsc, (uint8_t *)&(*(gm_t6j4d6ei14c2t *)gm_out) + 8, t115);
        goto gm_fdone;
        goto me2;
mn107:;
ma10:;
        uint8_t t119 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t119 != ((uint8_t)58ULL)) goto pa118;
        goto po117;
pa118:;
        uint8_t t121 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t121 != ((uint8_t)59ULL)) goto pa120;
        goto po117;
pa120:;
        goto mn116;
po117:;
        void * t122 = gm_l0;
        void * t123 = gm_l1;
        gm_tvdg94bk7aa22 t124 = gm_l2;
        gm_b16_8 t125 = gm_l3;
        gm_t5o0tpv5jka2l t126;
        gm_f_vec_load_store_ng9gfpc((void *)&t126, (void *)NULL, cx, NULL, t122, t123, t124, t125);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t126))[0] != 0)) {
            gm_t6j4d6ei14c2t t127;
            GM_ST(uint8_t, (uint8_t *)&t127 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t127 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t127 + 4, 0);
            ((uint8_t *)&t127)[0] = 1;
            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t127 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t126) + 8)));
            F.ret = t127;
            goto gm_fret;
        }
        gm_b16_8 t128 = GM_LD(gm_b16_8, (((uint8_t *)&t126) + 8));
        gm_th2lqbo77pvsc t129;
        GM_ST(uint8_t, (uint8_t *)&t129 + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&t129 + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&t129 + 20, 0);
        GM_ST(gm_b16_8, ((uint8_t *)&t129), t128);
        ((uint8_t *)&t129)[16] = 1;
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t6j4d6ei14c2t *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t6j4d6ei14c2t *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t6j4d6ei14c2t *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t6j4d6ei14c2t *)gm_out) + 32, 0);
        ((uint8_t *)&(*(gm_t6j4d6ei14c2t *)gm_out))[0] = 0;
        GM_ST(gm_th2lqbo77pvsc, (uint8_t *)&(*(gm_t6j4d6ei14c2t *)gm_out) + 8, t129);
        goto gm_fdone;
        goto me2;
mn116:;
ma11:;
        gm_th2lqbo77pvsc t131;
        memset(&t131, 0, sizeof t131);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t6j4d6ei14c2t *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t6j4d6ei14c2t *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t6j4d6ei14c2t *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t6j4d6ei14c2t *)gm_out) + 32, 0);
        ((uint8_t *)&(*(gm_t6j4d6ei14c2t *)gm_out))[0] = 0;
        GM_ST(gm_th2lqbo77pvsc, (uint8_t *)&(*(gm_t6j4d6ei14c2t *)gm_out) + 8, t131);
        goto gm_fdone;
        goto me2;
mn130:;
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
    GM_STACK_CHECK(&F, 1255u);
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
        if (((((uint8_t *)&t11))[0] != 0)) {
            gm_t5o0tpv5jka2l t12;
            GM_ST(uint8_t, (uint8_t *)&t12 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t12 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t12 + 4, 0);
            ((uint8_t *)&t12)[0] = 1;
            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t12 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t11) + 8)));
            F.ret = t12;
            goto gm_fret;
        }
        gm_b16_8 t13 = GM_LD(gm_b16_8, (((uint8_t *)&t11) + 8));
        gm_l9 = t13;
    }
    {
        uint8_t t15 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l2) + 0) == ((uint8_t)0ULL)));
        if (!t15) goto ie14;
        void * t16 = F.l0;
        void * t17 = GM_LD(void *, (((uint8_t *)t16) + 0));
        void * t18 = (void *)(((uint8_t *)t17) + 8);
        gm_t88rcucg1b7uk t19 = F.l7;
        uint8_t t20 = gm_f_is_integer_eo6jkc5(t18, GM_CONV(gm_csguankko4lmhd, t19));
        if (!t20) goto ie14;
        void * t21 = F.l0;
        void * t22 = GM_LD(void *, (((uint8_t *)t21) + 0));
        uint8_t t23 = GM_LD(uint8_t, ((((uint8_t *)t22) + 1136) + 0));
        if (!t23) goto ie14;
        {
            void * t25 = F.l0;
            gm_t88rcucg1b7uk t26 = gm_l4;
            gm_b16_8 t27 = gm_l9;
            gm_b16_8 t28 = gm_f_addr_of_hiaqkn0((void *)NULL, cx, NULL, t25, t26, t27);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.l10 = t28;
        }
        {
            void * t29 = F.l0;
            gm_t88rcucg1b7uk t30 = gm_l4;
            uint64_t t31 = gm_l6;
            gm_closure t32 = ((gm_closure){(void *)gm_f_fn_dahlogl, (void *)&F.h, cx});
            gm_t5o0tpv5jka2l t33;
            gm_f_vec_lane_loop_a3gvooh((void *)&t33, (void *)NULL, cx, NULL, t29, t30, t31, t32);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t33))[0] != 0)) {
                gm_t5o0tpv5jka2l t34;
                GM_ST(uint8_t, (uint8_t *)&t34 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t34 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t34 + 4, 0);
                ((uint8_t *)&t34)[0] = 1;
                GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t34 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t33) + 8)));
                F.ret = t34;
                goto gm_fret;
            }
            gm_b16_8 t35 = GM_LD(gm_b16_8, (((uint8_t *)&t33) + 8));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
            ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t35);
            goto gm_fdone;
        }
        goto ix14;
ie14:;
ix14:;
    }
    {
        void * t36 = F.l0;
        gm_t88rcucg1b7uk t37 = gm_l4;
        gm_b16_8 t38 = gm_l9;
        gm_b16_8 t39 = gm_f_vec_load_8m1nglt((void *)NULL, cx, NULL, t36, t37, t38);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l11 = t39;
    }
    {
        gm_b16_8 t40;
        GM_ST(void *, (uint8_t *)&t40, (void *)(gm_sr4qbrk6tlgaa));
        GM_ST(uintptr_t, (uint8_t *)&t40 + 8, (uintptr_t)(0));
        gm_l12 = t40;
    }
    {
        uint8_t t42 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l2) + 0) == ((uint8_t)0ULL)));
        if (!t42) goto ie41;
        {
            void * t45 = F.l0;
            void * t46 = GM_LD(void *, (((uint8_t *)t45) + 0));
            void * t47 = (void *)(((uint8_t *)t46) + 8);
            gm_t88rcucg1b7uk t48 = F.l7;
            uint8_t t49 = gm_f_is_float_gthejtb(t47, GM_CONV(gm_csguankko4lmhd, t48));
            if (!t49) goto ie44;
            {
                gm_b16_8 t51 = gm_l11;
                gm_tnpifv20irnph t52;
                GM_ST(gm_b16_8, ((uint8_t *)&t52 + 0), t51);
                gm_tabb710d9mq2e t53;
                gm_f_format_dlemvkb((void *)&t53, (void *)NULL, cx, NULL, t52);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t53))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t53) + 8, gm_src7hmnlrtmirgt, 124, 17);
                gm_b16_8 t54 = GM_LD(gm_b16_8, (((uint8_t *)&t53) + 8));
                gm_b16_8 t55 = GM_LD(gm_b16_8, ((uint8_t *)&t54));
                gm_l12 = t55;
            }
            goto ix44;
ie44:;
            {
                void * t57 = F.l0;
                uint64_t t58 = gm_l6;
                gm_t88rcucg1b7uk t59 = F.l7;
                gm_tqs5p4n2tq19v t60;
                GM_ST(uint8_t, ((uint8_t *)&t60), ((uint8_t)0));
                ((uint8_t *)&t60)[1] = 1;
                gm_b16_8 t61 = gm_f_vec_ty_adod8ur((void *)NULL, cx, NULL, t57, t58, t59, t60);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l13 = t61;
            }
            {
                void * t62 = F.l0;
                uint64_t t63 = gm_l6;
                gm_t88rcucg1b7uk t64 = F.l7;
                gm_tqs5p4n2tq19v t65;
                memset(&t65, 0, sizeof t65);
                gm_b16_8 t66 = gm_f_vec_ty_adod8ur((void *)NULL, cx, NULL, t62, t63, t64, t65);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l14 = t66;
            }
            {
                gm_t0uuhaljsf410 t67;
                gm_b16_8 t68 = gm_l14;
                GM_ST(gm_b16_8, (uint8_t *)&t67 + 0, t68);
                gm_b16_8 t69 = gm_l13;
                GM_ST(gm_b16_8, (uint8_t *)&t67 + 16, t69);
                gm_b16_8 t70 = gm_l13;
                GM_ST(gm_b16_8, (uint8_t *)&t67 + 32, t70);
                gm_b16_8 t71 = gm_l11;
                GM_ST(gm_b16_8, (uint8_t *)&t67 + 48, t71);
                gm_tabb710d9mq2e t72;
                gm_f_format_jo1gjr5((void *)&t72, (void *)NULL, cx, NULL, &t67);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t72))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t72) + 8, gm_src7hmnlrtmirgt, 128, 17);
                gm_b16_8 t73 = GM_LD(gm_b16_8, (((uint8_t *)&t72) + 8));
                gm_b16_8 t74 = GM_LD(gm_b16_8, ((uint8_t *)&t73));
                gm_l12 = t74;
            }
ix44:;
        }
        goto ix41;
ie41:;
        void * t76 = F.l0;
        void * t77 = GM_LD(void *, (((uint8_t *)t76) + 0));
        void * t78 = (void *)(((uint8_t *)t77) + 8);
        gm_t88rcucg1b7uk t79 = F.l7;
        gm_t53at80pfdus3 t80;
        gm_f_kind_ghvav22((void *)&t80, t78, GM_CONV(gm_csguankko4lmhd, t79));
        uint8_t t81 = ((GM_LD(uint8_t, ((uint8_t *)&t80) + 0) == ((uint8_t)4ULL)));
        if (!t81) goto ie75;
        {
            gm_b16_8 t83 = gm_l11;
            gm_tnpifv20irnph t84;
            GM_ST(gm_b16_8, ((uint8_t *)&t84 + 0), t83);
            gm_tabb710d9mq2e t85;
            gm_f_format_njau13r((void *)&t85, (void *)NULL, cx, NULL, t84);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t85))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t85) + 8, gm_src7hmnlrtmirgt, 131, 13);
            gm_b16_8 t86 = GM_LD(gm_b16_8, (((uint8_t *)&t85) + 8));
            gm_b16_8 t87 = GM_LD(gm_b16_8, ((uint8_t *)&t86));
            gm_l12 = t87;
        }
        goto ix75;
ie75:;
        {
            gm_b16_8 t89 = gm_l11;
            gm_tnpifv20irnph t90;
            GM_ST(gm_b16_8, ((uint8_t *)&t90 + 0), t89);
            gm_tabb710d9mq2e t91;
            gm_f_format_nhrpa25((void *)&t91, (void *)NULL, cx, NULL, t90);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t91))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t91) + 8, gm_src7hmnlrtmirgt, 133, 13);
            gm_b16_8 t92 = GM_LD(gm_b16_8, (((uint8_t *)&t91) + 8));
            gm_b16_8 t93 = GM_LD(gm_b16_8, ((uint8_t *)&t92));
            gm_l12 = t93;
        }
ix75:;
ix41:;
    }
    {
        void * t94 = F.l0;
        gm_t88rcucg1b7uk t95 = gm_l4;
        gm_b16_8 t96 = gm_l12;
        gm_b16_8 t97 = gm_f_vec_store_6d0dm94((void *)NULL, cx, NULL, t94, t95, t96);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
        ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t97);
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
