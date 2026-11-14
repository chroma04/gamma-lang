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
typedef struct gm_csguankko4lmhd { uint32_t f0; } gm_csguankko4lmhd;
typedef struct { uint32_t f0; uint8_t p1[4]; void * f1; void * f2; } gm_t017ud7s0np4m;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint64_t f0; } gm_t4ce0jd3tdro4;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint32_t f0; uint8_t f1; uint8_t pe[3]; } gm_t9805qtonbsag;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint8_t f0; uint8_t p1[7]; void * f1; void * f2; } gm_ta612fil812pm;
typedef struct { void * f0; void * f1; void * f2; uint8_t f3; uint8_t pe[7]; } gm_tao63pk3r18v5;
typedef struct { void * f0; void * f1; void * f2; } gm_tbbqefuhv2r8h;
typedef struct { void * f0; uint32_t f1; uint8_t pe[4]; } gm_tbl8il92l97ut;
typedef struct { uint32_t f0; uint32_t f1; } gm_tcc1rq4n1avb2;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint8_t f0; uint8_t p1[7]; void * f1; } gm_tgtub0v94d5u5;
typedef struct { uint8_t f0; uint8_t p1[1]; uint16_t f1; } gm_tjcj4j2gbh6c8;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { void * f0; void * f1; uint8_t f2; uint8_t pe[7]; } gm_tlcl4u558l6p5;
typedef struct { void * f0; void * f1; uint32_t f2; uint32_t f3; } gm_tmm67r2c94d5j;
typedef struct { uint8_t f0; uint8_t p1[3]; union { uint32_t v0; } f1; } gm_tmn7n0n9raou5;
typedef struct { uint8_t f0; uint8_t f1; uint8_t f2; } gm_toh0ootodhjue;
typedef struct { void * f0; uint8_t f1; uint8_t pe[7]; } gm_tsplfr33dsvji;
typedef struct { uint32_t f0; uint8_t p1[4]; void * f1; } gm_ttggq7ng4kp3p;
_Static_assert(sizeof(gm_cseahvg8v7p0vs) == 12 && _Alignof(gm_cseahvg8v7p0vs) == 4 && offsetof(gm_cseahvg8v7p0vs, f0) == 0 && offsetof(gm_cseahvg8v7p0vs, f1) == 4 && offsetof(gm_cseahvg8v7p0vs, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_csguankko4lmhd) == 4 && _Alignof(gm_csguankko4lmhd) == 4 && offsetof(gm_csguankko4lmhd, f0) == 0, "layout of `Mutex`");
_Static_assert(sizeof(gm_t017ud7s0np4m) == 24 && _Alignof(gm_t017ud7s0np4m) == 8 && offsetof(gm_t017ud7s0np4m, f0) == 0 && offsetof(gm_t017ud7s0np4m, f1) == 8 && offsetof(gm_t017ud7s0np4m, f2) == 16, "layout of `(f: u32, sizes: ^Expr, aligns: ^Expr)`");
typedef struct { void * f0; gm_t9805qtonbsag f1; uint32_t f2; uint8_t pe[4]; } gm_t083kd7b1kkag;
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_t08olq97rscn3;
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; gm_t9qkh0rsooc8d f2; uint8_t p3[3]; gm_tf5srh4mp4b3i f3; } gm_t0mglksrkfa5e;
typedef struct { gm_b16_8 f0; uint64_t f1; } gm_t3877pk7lv748;
typedef struct { void * f0; gm_t9805qtonbsag f1; void * f2; } gm_t3dcoc7077t1q;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; uint8_t f2; uint8_t pe[3]; } gm_t3rcjibl8kkg4;
_Static_assert(sizeof(gm_t4ce0jd3tdro4) == 8 && _Alignof(gm_t4ce0jd3tdro4) == 8 && offsetof(gm_t4ce0jd3tdro4, f0) == 0, "layout of `Instant`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint64_t v0; gm_t88rcucg1b7uk v1; } f1; } gm_t5e6kkuq21nig;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; void * f2; void * f3; } gm_t710lsrq1m8gk;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; void * f1; } gm_t8ghgl2jj0qha;
typedef struct { gm_tmn7n0n9raou5 f0; void * f1; } gm_t8jsc4n9vp2ib;
_Static_assert(sizeof(gm_t9805qtonbsag) == 8 && _Alignof(gm_t9805qtonbsag) == 4 && offsetof(gm_t9805qtonbsag, f0) == 0 && offsetof(gm_t9805qtonbsag, f1) == 4, "layout of `?u32`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
_Static_assert(sizeof(gm_ta612fil812pm) == 24 && _Alignof(gm_ta612fil812pm) == 8 && offsetof(gm_ta612fil812pm, f0) == 0 && offsetof(gm_ta612fil812pm, f1) == 8 && offsetof(gm_ta612fil812pm, f2) == 16, "layout of `(is_and: bool, lhs: ^Expr, rhs: ^Expr)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
_Static_assert(sizeof(gm_tao63pk3r18v5) == 32 && _Alignof(gm_tao63pk3r18v5) == 8 && offsetof(gm_tao63pk3r18v5, f0) == 0 && offsetof(gm_tao63pk3r18v5, f1) == 8 && offsetof(gm_tao63pk3r18v5, f2) == 16 && offsetof(gm_tao63pk3r18v5, f3) == 24, "layout of `SliceExpr`");
typedef struct { gm_b16_8 f0; uint32_t f1; uint32_t f2; gm_b16_8 f3; } gm_tb40qn7hk0tl9;
_Static_assert(sizeof(gm_tbbqefuhv2r8h) == 24 && _Alignof(gm_tbbqefuhv2r8h) == 8 && offsetof(gm_tbbqefuhv2r8h, f0) == 0 && offsetof(gm_tbbqefuhv2r8h, f1) == 8 && offsetof(gm_tbbqefuhv2r8h, f2) == 16, "layout of `Arm`");
typedef struct { uint64_t f0; uint64_t f1; gm_b16_8 f2; } gm_tbccvikg6b96e;
_Static_assert(sizeof(gm_tbl8il92l97ut) == 16 && _Alignof(gm_tbl8il92l97ut) == 8 && offsetof(gm_tbl8il92l97ut, f0) == 0 && offsetof(gm_tbl8il92l97ut, f1) == 8, "layout of `SpecCall`");
typedef struct { uint8_t f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; } gm_tbngattl1fo51;
_Static_assert(sizeof(gm_tcc1rq4n1avb2) == 8 && _Alignof(gm_tcc1rq4n1avb2) == 4 && offsetof(gm_tcc1rq4n1avb2, f0) == 0 && offsetof(gm_tcc1rq4n1avb2, f1) == 4, "layout of `LineCol`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tcmkompbl09il;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; gm_t88rcucg1b7uk f2; } gm_tcmtrmhj7ak4u;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_tf5srh4mp4b3i f1; uint8_t f2; uint8_t p3[3]; gm_t88rcucg1b7uk f3; } gm_tcu60n24kv26r;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t88rcucg1b7uk v0; gm_b16_8 v1; } f1; } gm_te3magq8drarp;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_tcc1rq4n1avb2 v0; } f1; } gm_te9hrk0u35rev;
typedef struct { uint8_t f0; union { gm_toh0ootodhjue v0; } f1; } gm_tesaltf0mhcki;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_b16_8 f0; void * f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_tgabaikr08mjg;
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; } gm_tgglga8ro3isc;
_Static_assert(sizeof(gm_tgtub0v94d5u5) == 16 && _Alignof(gm_tgtub0v94d5u5) == 8 && offsetof(gm_tgtub0v94d5u5, f0) == 0 && offsetof(gm_tgtub0v94d5u5, f1) == 8, "layout of `(is_const: bool, inner: ^Expr)`");
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { gm_tf5srh4mp4b3i f0; uint8_t f1; uint8_t pe[7]; } gm_th3eueiipivq6;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_tf5srh4mp4b3i f1; uint8_t f2; uint8_t pe[7]; } gm_tj4mbomtvccf0;
_Static_assert(sizeof(gm_tjcj4j2gbh6c8) == 4 && _Alignof(gm_tjcj4j2gbh6c8) == 2 && offsetof(gm_tjcj4j2gbh6c8, f0) == 0 && offsetof(gm_tjcj4j2gbh6c8, f1) == 2, "layout of `(signed: bool, bits: u16)`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_tf5srh4mp4b3i f1; uint8_t f2; uint8_t pe[7]; } gm_tjpqvfd72o62u;
typedef struct { void * f0; gm_b16_8 f1; } gm_tkaapcii86udk;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t f1; uint8_t pe[3]; } gm_tkn42u7na3t81;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_tlcl4u558l6p5) == 24 && _Alignof(gm_tlcl4u558l6p5) == 8 && offsetof(gm_tlcl4u558l6p5, f0) == 0 && offsetof(gm_tlcl4u558l6p5, f1) == 8 && offsetof(gm_tlcl4u558l6p5, f2) == 16, "layout of `(lo: ^Expr, hi: ^Expr, inclusive: bool)`");
_Static_assert(sizeof(gm_tmm67r2c94d5j) == 24 && _Alignof(gm_tmm67r2c94d5j) == 8 && offsetof(gm_tmm67r2c94d5j, f0) == 0 && offsetof(gm_tmm67r2c94d5j, f1) == 8 && offsetof(gm_tmm67r2c94d5j, f2) == 16 && offsetof(gm_tmm67r2c94d5j, f3) == 20, "layout of `Printer`");
_Static_assert(sizeof(gm_tmn7n0n9raou5) == 8 && _Alignof(gm_tmn7n0n9raou5) == 4 && offsetof(gm_tmn7n0n9raou5, f0) == 0 && offsetof(gm_tmn7n0n9raou5, f1) == 4, "layout of `Coercion`");
typedef struct { gm_t017ud7s0np4m f0; uint8_t f1; uint8_t pe[7]; } gm_tn6p6lpnve7b0;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t88rcucg1b7uk v0; } f1; } gm_tnnjeerolos0a;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
typedef struct { gm_t88rcucg1b7uk a[1]; } gm_tnqk2qt7j42o9;
_Static_assert(sizeof(gm_toh0ootodhjue) == 3 && _Alignof(gm_toh0ootodhjue) == 1 && offsetof(gm_toh0ootodhjue, f0) == 0 && offsetof(gm_toh0ootodhjue, f1) == 1 && offsetof(gm_toh0ootodhjue, f2) == 2, "layout of `Restr`");
typedef struct { gm_b16_8 f0; void * f1; } gm_tpe1hv3fekn05;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; void * v1; } f1; } gm_tq7s98ihonkju;
typedef struct { gm_b16_8 f0; void * f1; void * f2; } gm_trl2khmuphncd;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_t9qkh0rsooc8d v3; } f1; } gm_ts6oqppjh0ba3;
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t f2; uint8_t p3[3]; gm_tl874t2atnlh3 f3; uint8_t pe[4]; } gm_tsoie1ppgdvcg;
_Static_assert(sizeof(gm_tsplfr33dsvji) == 16 && _Alignof(gm_tsplfr33dsvji) == 8 && offsetof(gm_tsplfr33dsvji, f0) == 0 && offsetof(gm_tsplfr33dsvji, f1) == 8, "layout of `(expr: ^Expr, is_result: bool)`");
_Static_assert(sizeof(gm_ttggq7ng4kp3p) == 16 && _Alignof(gm_ttggq7ng4kp3p) == 8 && offsetof(gm_ttggq7ng4kp3p, f0) == 0 && offsetof(gm_ttggq7ng4kp3p, f1) == 8, "layout of `(index: u32, payload: ?^Value)`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_tcc1rq4n1avb2 v3; uint32_t v4; uint32_t v5; } f1; } gm_tvdg94bk7aa22;
_Static_assert(sizeof(gm_t083kd7b1kkag) == 24 && _Alignof(gm_t083kd7b1kkag) == 8 && offsetof(gm_t083kd7b1kkag, f0) == 0 && offsetof(gm_t083kd7b1kkag, f1) == 8 && offsetof(gm_t083kd7b1kkag, f2) == 16, "layout of `TryExpr`");
_Static_assert(sizeof(gm_t08olq97rscn3) == 32 && _Alignof(gm_t08olq97rscn3) == 8 && offsetof(gm_t08olq97rscn3, f0) == 0 && offsetof(gm_t08olq97rscn3, f1) == 16, "layout of `File`");
_Static_assert(sizeof(gm_t0mglksrkfa5e) == 40 && _Alignof(gm_t0mglksrkfa5e) == 8 && offsetof(gm_t0mglksrkfa5e, f0) == 0 && offsetof(gm_t0mglksrkfa5e, f1) == 16 && offsetof(gm_t0mglksrkfa5e, f2) == 20 && offsetof(gm_t0mglksrkfa5e, f3) == 24, "layout of `FieldDef`");
_Static_assert(sizeof(gm_t3877pk7lv748) == 24 && _Alignof(gm_t3877pk7lv748) == 8 && offsetof(gm_t3877pk7lv748, f0) == 0 && offsetof(gm_t3877pk7lv748, f1) == 16, "layout of `WasmReader`");
_Static_assert(sizeof(gm_t3dcoc7077t1q) == 24 && _Alignof(gm_t3dcoc7077t1q) == 8 && offsetof(gm_t3dcoc7077t1q, f0) == 0 && offsetof(gm_t3dcoc7077t1q, f1) == 8 && offsetof(gm_t3dcoc7077t1q, f2) == 16, "layout of `ElseExpr`");
_Static_assert(sizeof(gm_t3rcjibl8kkg4) == 12 && _Alignof(gm_t3rcjibl8kkg4) == 4 && offsetof(gm_t3rcjibl8kkg4, f0) == 0 && offsetof(gm_t3rcjibl8kkg4, f1) == 4 && offsetof(gm_t3rcjibl8kkg4, f2) == 8, "layout of `(a: Ty, b: Ty, negated: bool)`");
typedef struct { gm_tiilmfd16v3fc f0; uint32_t f1; uint8_t pe[4]; } gm_t419mo45g7j0a;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tcmkompbl09il v0; gm_tiilmfd16v3fc v1; gm_b16_8 v2; } f1; } gm_t428ue2u3hf5g;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t p3[4]; gm_trqfvmjtt4sij f3; gm_trqfvmjtt4sij f4; gm_trqfvmjtt4sij f5; gm_th2lqbo77pvsc f6; } gm_t4gut2c9fj1kt;
_Static_assert(sizeof(gm_t5e6kkuq21nig) == 16 && _Alignof(gm_t5e6kkuq21nig) == 8 && offsetof(gm_t5e6kkuq21nig, f0) == 0 && offsetof(gm_t5e6kkuq21nig, f1) == 8, "layout of `ArrayLen`");
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_t710lsrq1m8gk) == 40 && _Alignof(gm_t710lsrq1m8gk) == 8 && offsetof(gm_t710lsrq1m8gk, f0) == 0 && offsetof(gm_t710lsrq1m8gk, f1) == 8 && offsetof(gm_t710lsrq1m8gk, f2) == 24 && offsetof(gm_t710lsrq1m8gk, f3) == 32, "layout of `WithExpr`");
typedef struct { gm_tjpqvfd72o62u f0; uint8_t f1; uint8_t pe[7]; } gm_t8cc1mtbrhlpt;
_Static_assert(sizeof(gm_t8ghgl2jj0qha) == 16 && _Alignof(gm_t8ghgl2jj0qha) == 8 && offsetof(gm_t8ghgl2jj0qha, f0) == 0 && offsetof(gm_t8ghgl2jj0qha, f1) == 8, "layout of `(op: UnOp, operand: ^Expr)`");
_Static_assert(sizeof(gm_t8jsc4n9vp2ib) == 16 && _Alignof(gm_t8jsc4n9vp2ib) == 8 && offsetof(gm_t8jsc4n9vp2ib, f0) == 0 && offsetof(gm_t8jsc4n9vp2ib, f1) == 8, "layout of `(kind: Coercion, expr: ^Expr)`");
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; gm_tnnjeerolos0a f2; uint8_t pe[4]; } gm_t8nckeqfenl8a;
typedef struct { uint64_t f0; gm_t0mglksrkfa5e f1; } gm_t8ta482bl6be0;
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
typedef struct { gm_tesaltf0mhcki f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tandvbt63uk7n;
_Static_assert(sizeof(gm_tb40qn7hk0tl9) == 40 && _Alignof(gm_tb40qn7hk0tl9) == 8 && offsetof(gm_tb40qn7hk0tl9, f0) == 0 && offsetof(gm_tb40qn7hk0tl9, f1) == 16 && offsetof(gm_tb40qn7hk0tl9, f2) == 20 && offsetof(gm_tb40qn7hk0tl9, f3) == 24, "layout of `SourceLocation`");
_Static_assert(sizeof(gm_tbccvikg6b96e) == 32 && _Alignof(gm_tbccvikg6b96e) == 8 && offsetof(gm_tbccvikg6b96e, f0) == 0 && offsetof(gm_tbccvikg6b96e, f1) == 8 && offsetof(gm_tbccvikg6b96e, f2) == 16, "layout of `Layout`");
typedef struct { uint8_t f0; uint8_t p1[7]; gm_trqfvmjtt4sij f1; } gm_tbci5ospk5nnn;
typedef struct { gm_th3eueiipivq6 f0; uint8_t f1; uint8_t pe[7]; } gm_tbm0n9ujhrlhf;
_Static_assert(sizeof(gm_tbngattl1fo51) == 8 && _Alignof(gm_tbngattl1fo51) == 4 && offsetof(gm_tbngattl1fo51, f0) == 0 && offsetof(gm_tbngattl1fo51, f1) == 4, "layout of `(is_const: bool, pointee: Ty)`");
_Static_assert(sizeof(gm_tcmkompbl09il) == 24 && _Alignof(gm_tcmkompbl09il) == 8 && offsetof(gm_tcmkompbl09il, f0) == 0 && offsetof(gm_tcmkompbl09il, f1) == 8, "layout of `(ty: Ty, bytes: string)`");
_Static_assert(sizeof(gm_tcmtrmhj7ak4u) == 12 && _Alignof(gm_tcmtrmhj7ak4u) == 4 && offsetof(gm_tcmtrmhj7ak4u, f0) == 0 && offsetof(gm_tcmtrmhj7ak4u, f1) == 4 && offsetof(gm_tcmtrmhj7ak4u, f2) == 8, "layout of `(op: LenOp, lhs: Ty, rhs: Ty)`");
_Static_assert(sizeof(gm_tcu60n24kv26r) == 32 && _Alignof(gm_tcu60n24kv26r) == 8 && offsetof(gm_tcu60n24kv26r, f0) == 0 && offsetof(gm_tcu60n24kv26r, f1) == 8 && offsetof(gm_tcu60n24kv26r, f2) == 24 && offsetof(gm_tcu60n24kv26r, f3) == 28, "layout of `SoaMade`");
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t p2[4]; gm_tnpifv20irnph f2; uint8_t f3; uint8_t pe[7]; } gm_tddtjfggdat0d;
_Static_assert(sizeof(gm_te3magq8drarp) == 24 && _Alignof(gm_te3magq8drarp) == 8 && offsetof(gm_te3magq8drarp, f0) == 0 && offsetof(gm_te3magq8drarp, f1) == 8, "layout of `SoaTy`");
_Static_assert(sizeof(gm_te9hrk0u35rev) == 12 && _Alignof(gm_te9hrk0u35rev) == 4 && offsetof(gm_te9hrk0u35rev, f0) == 0 && offsetof(gm_te9hrk0u35rev, f1) == 4, "layout of `Target`");
typedef struct { gm_te9hrk0u35rev f0; uint8_t p1[4]; void * f1; } gm_teo9d4mncjfdu;
_Static_assert(sizeof(gm_tesaltf0mhcki) == 4 && _Alignof(gm_tesaltf0mhcki) == 1 && offsetof(gm_tesaltf0mhcki, f0) == 0 && offsetof(gm_tesaltf0mhcki, f1) == 1, "layout of `SoaOp`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_tgabaikr08mjg) == 40 && _Alignof(gm_tgabaikr08mjg) == 8 && offsetof(gm_tgabaikr08mjg, f0) == 0 && offsetof(gm_tgabaikr08mjg, f1) == 16 && offsetof(gm_tgabaikr08mjg, f2) == 24, "layout of `Block`");
_Static_assert(sizeof(gm_tgglga8ro3isc) == 8 && _Alignof(gm_tgglga8ro3isc) == 4 && offsetof(gm_tgglga8ro3isc, f0) == 0 && offsetof(gm_tgglga8ro3isc, f1) == 4, "layout of `(err: Ty, ok: Ty)`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
_Static_assert(sizeof(gm_th3eueiipivq6) == 24 && _Alignof(gm_th3eueiipivq6) == 8 && offsetof(gm_th3eueiipivq6, f0) == 0 && offsetof(gm_th3eueiipivq6, f1) == 16, "layout of `(len: ?u64, is_const: bool)`");
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
_Static_assert(sizeof(gm_tj4mbomtvccf0) == 32 && _Alignof(gm_tj4mbomtvccf0) == 8 && offsetof(gm_tj4mbomtvccf0, f0) == 0 && offsetof(gm_tj4mbomtvccf0, f1) == 8 && offsetof(gm_tj4mbomtvccf0, f2) == 24, "layout of `SoaTemplate`");
_Static_assert(sizeof(gm_tjpqvfd72o62u) == 32 && _Alignof(gm_tjpqvfd72o62u) == 8 && offsetof(gm_tjpqvfd72o62u, f0) == 0 && offsetof(gm_tjpqvfd72o62u, f1) == 8 && offsetof(gm_tjpqvfd72o62u, f2) == 24, "layout of `SoaInfo`");
_Static_assert(sizeof(gm_tkaapcii86udk) == 24 && _Alignof(gm_tkaapcii86udk) == 8 && offsetof(gm_tkaapcii86udk, f0) == 0 && offsetof(gm_tkaapcii86udk, f1) == 8, "layout of `Place`");
_Static_assert(sizeof(gm_tkn42u7na3t81) == 8 && _Alignof(gm_tkn42u7na3t81) == 4 && offsetof(gm_tkn42u7na3t81, f0) == 0 && offsetof(gm_tkn42u7na3t81, f1) == 4, "layout of `?Ty`");
_Static_assert(sizeof(gm_tn6p6lpnve7b0) == 32 && _Alignof(gm_tn6p6lpnve7b0) == 8 && offsetof(gm_tn6p6lpnve7b0, f0) == 0 && offsetof(gm_tn6p6lpnve7b0, f1) == 24, "layout of `?(f: u32, sizes: ^Expr, aligns: ^Expr)`");
_Static_assert(sizeof(gm_tnnjeerolos0a) == 8 && _Alignof(gm_tnnjeerolos0a) == 4 && offsetof(gm_tnnjeerolos0a, f0) == 0 && offsetof(gm_tnnjeerolos0a, f1) == 4, "layout of `ArrayKind`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
_Static_assert(sizeof(gm_tnqk2qt7j42o9) == 4 && _Alignof(gm_tnqk2qt7j42o9) == 4 && offsetof(gm_tnqk2qt7j42o9, a) == 0, "layout of `[1]Ty`");
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t p2[7]; gm_tf5srh4mp4b3i f2; gm_tkn42u7na3t81 f3; } gm_tpautr4br7h3l;
_Static_assert(sizeof(gm_tpe1hv3fekn05) == 24 && _Alignof(gm_tpe1hv3fekn05) == 8 && offsetof(gm_tpe1hv3fekn05, f0) == 0 && offsetof(gm_tpe1hv3fekn05, f1) == 16, "layout of `(attrs: []Attribute, inner: ^Expr)`");
_Static_assert(sizeof(gm_tq7s98ihonkju) == 16 && _Alignof(gm_tq7s98ihonkju) == 8 && offsetof(gm_tq7s98ihonkju, f0) == 0 && offsetof(gm_tq7s98ihonkju, f1) == 8, "layout of `AllocError!^HandlerDecl`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_ttggq7ng4kp3p v0; gm_tpe1hv3fekn05 v1; gm_t3mf58gv9kf3n v2; gm_tpe1hv3fekn05 v3; void * v4; gm_tgtub0v94d5u5 v5; } f1; } gm_trcmmd12cefb1;
_Static_assert(sizeof(gm_trl2khmuphncd) == 32 && _Alignof(gm_trl2khmuphncd) == 8 && offsetof(gm_trl2khmuphncd, f0) == 0 && offsetof(gm_trl2khmuphncd, f1) == 16 && offsetof(gm_trl2khmuphncd, f2) == 24, "layout of `IfExpr`");
typedef struct { gm_ts6oqppjh0ba3 f0; uint8_t p1[6]; void * f1; void * f2; } gm_trl9d7m1k5lbd;
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_ts6oqppjh0ba3) == 2 && _Alignof(gm_ts6oqppjh0ba3) == 1 && offsetof(gm_ts6oqppjh0ba3, f0) == 0 && offsetof(gm_ts6oqppjh0ba3, f1) == 1, "layout of `BinOp`");
_Static_assert(sizeof(gm_tsoie1ppgdvcg) == 40 && _Alignof(gm_tsoie1ppgdvcg) == 8 && offsetof(gm_tsoie1ppgdvcg, f0) == 0 && offsetof(gm_tsoie1ppgdvcg, f1) == 16 && offsetof(gm_tsoie1ppgdvcg, f2) == 20 && offsetof(gm_tsoie1ppgdvcg, f3) == 24, "layout of `LocalDecl`");
typedef struct { gm_tvdg94bk7aa22 f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tt8o5kebou62r;
_Static_assert(sizeof(gm_tvdg94bk7aa22) == 12 && _Alignof(gm_tvdg94bk7aa22) == 4 && offsetof(gm_tvdg94bk7aa22, f0) == 0 && offsetof(gm_tvdg94bk7aa22, f1) == 4, "layout of `Builtin`");
_Static_assert(sizeof(gm_t419mo45g7j0a) == 32 && _Alignof(gm_t419mo45g7j0a) == 8 && offsetof(gm_t419mo45g7j0a, f0) == 0 && offsetof(gm_t419mo45g7j0a, f1) == 24, "layout of `(effect_: EffectRef, op: u32)`");
_Static_assert(sizeof(gm_t428ue2u3hf5g) == 32 && _Alignof(gm_t428ue2u3hf5g) == 8 && offsetof(gm_t428ue2u3hf5g, f0) == 0 && offsetof(gm_t428ue2u3hf5g, f1) == 8, "layout of `ConstKey`");
_Static_assert(sizeof(gm_t4gut2c9fj1kt) == 184 && _Alignof(gm_t4gut2c9fj1kt) == 8 && offsetof(gm_t4gut2c9fj1kt, f0) == 0 && offsetof(gm_t4gut2c9fj1kt, f1) == 8 && offsetof(gm_t4gut2c9fj1kt, f2) == 24 && offsetof(gm_t4gut2c9fj1kt, f3) == 40 && offsetof(gm_t4gut2c9fj1kt, f4) == 80 && offsetof(gm_t4gut2c9fj1kt, f5) == 120 && offsetof(gm_t4gut2c9fj1kt, f6) == 160, "layout of `Diagnostic`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tjcj4j2gbh6c8 v0; uint16_t v1; gm_tbngattl1fo51 v2; gm_tbngattl1fo51 v3; gm_tbngattl1fo51 v4; gm_t8nckeqfenl8a v5; gm_t6j6smj7qoctu v6; gm_t88rcucg1b7uk v7; gm_tgglga8ro3isc v8; gm_tddtjfggdat0d v9; gm_tddtjfggdat0d v10; gm_tiilmfd16v3fc v11; gm_t88rcucg1b7uk v12; uint32_t v13; gm_tgglga8ro3isc v14; gm_t428ue2u3hf5g v15; gm_tcmtrmhj7ak4u v16; uint32_t v17; gm_tnpifv20irnph v18; uint32_t v19; } f1; } gm_t53at80pfdus3;
typedef struct { gm_tbci5ospk5nnn f0; gm_tbci5ospk5nnn f1; uint8_t f2; uint8_t pe[7]; } gm_t59juu943t8sl;
_Static_assert(sizeof(gm_t8cc1mtbrhlpt) == 40 && _Alignof(gm_t8cc1mtbrhlpt) == 8 && offsetof(gm_t8cc1mtbrhlpt, f0) == 0 && offsetof(gm_t8cc1mtbrhlpt, f1) == 32, "layout of `?SoaInfo`");
_Static_assert(sizeof(gm_t8nckeqfenl8a) == 24 && _Alignof(gm_t8nckeqfenl8a) == 8 && offsetof(gm_t8nckeqfenl8a, f0) == 0 && offsetof(gm_t8nckeqfenl8a, f1) == 8 && offsetof(gm_t8nckeqfenl8a, f2) == 12, "layout of `(len: u64, elem: Ty, kind: ArrayKind)`");
_Static_assert(sizeof(gm_t8ta482bl6be0) == 48 && _Alignof(gm_t8ta482bl6be0) == 8 && offsetof(gm_t8ta482bl6be0, f0) == 0 && offsetof(gm_t8ta482bl6be0, f1) == 8, "layout of `(i: usize, x: FieldDef)`");
_Static_assert(sizeof(gm_tandvbt63uk7n) == 24 && _Alignof(gm_tandvbt63uk7n) == 8 && offsetof(gm_tandvbt63uk7n, f0) == 0 && offsetof(gm_tandvbt63uk7n, f1) == 8, "layout of `(op: SoaOp, args: []Expr)`");
_Static_assert(sizeof(gm_tbci5ospk5nnn) == 48 && _Alignof(gm_tbci5ospk5nnn) == 8 && offsetof(gm_tbci5ospk5nnn, f0) == 0 && offsetof(gm_tbci5ospk5nnn, f1) == 8, "layout of `Int`");
_Static_assert(sizeof(gm_tbm0n9ujhrlhf) == 32 && _Alignof(gm_tbm0n9ujhrlhf) == 8 && offsetof(gm_tbm0n9ujhrlhf, f0) == 0 && offsetof(gm_tbm0n9ujhrlhf, f1) == 24, "layout of `?(len: ?u64, is_const: bool)`");
_Static_assert(sizeof(gm_tddtjfggdat0d) == 48 && _Alignof(gm_tddtjfggdat0d) == 8 && offsetof(gm_tddtjfggdat0d, f0) == 0 && offsetof(gm_tddtjfggdat0d, f1) == 16 && offsetof(gm_tddtjfggdat0d, f2) == 24 && offsetof(gm_tddtjfggdat0d, f3) == 40, "layout of `FnTy`");
_Static_assert(sizeof(gm_teo9d4mncjfdu) == 24 && _Alignof(gm_teo9d4mncjfdu) == 8 && offsetof(gm_teo9d4mncjfdu, f0) == 0 && offsetof(gm_teo9d4mncjfdu, f1) == 16, "layout of `BreakExpr`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tpautr4br7h3l v0; gm_tcmkompbl09il v1; gm_b16_8 v2; } f1; } gm_ti0kgh3pc3in2;
typedef struct { gm_tbci5ospk5nnn f0; gm_tbci5ospk5nnn f1; } gm_tngtuiuskn938;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t88rcucg1b7uk v0; gm_t428ue2u3hf5g v1; } f1; } gm_toa2vhe3l4hfr;
_Static_assert(sizeof(gm_tpautr4br7h3l) == 48 && _Alignof(gm_tpautr4br7h3l) == 8 && offsetof(gm_tpautr4br7h3l, f0) == 0 && offsetof(gm_tpautr4br7h3l, f1) == 16 && offsetof(gm_tpautr4br7h3l, f2) == 24 && offsetof(gm_tpautr4br7h3l, f3) == 40, "layout of `StructDef`");
_Static_assert(sizeof(gm_trcmmd12cefb1) == 32 && _Alignof(gm_trcmmd12cefb1) == 8 && offsetof(gm_trcmmd12cefb1, f0) == 0 && offsetof(gm_trcmmd12cefb1, f1) == 8, "layout of `Stmt`");
_Static_assert(sizeof(gm_trl9d7m1k5lbd) == 24 && _Alignof(gm_trl9d7m1k5lbd) == 8 && offsetof(gm_trl9d7m1k5lbd, f0) == 0 && offsetof(gm_trl9d7m1k5lbd, f1) == 8 && offsetof(gm_trl9d7m1k5lbd, f2) == 16, "layout of `(op: BinOp, lhs: ^Expr, rhs: ^Expr)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint32_t v0; gm_t419mo45g7j0a v1; void * v2; } f1; } gm_tsth9qu1dv0g3;
_Static_assert(sizeof(gm_tt8o5kebou62r) == 32 && _Alignof(gm_tt8o5kebou62r) == 8 && offsetof(gm_tt8o5kebou62r, f0) == 0 && offsetof(gm_tt8o5kebou62r, f1) == 16, "layout of `(op: Builtin, args: []Expr)`");
typedef struct { gm_toa2vhe3l4hfr a[1]; } gm_t2ubvi1j1aq22;
_Static_assert(sizeof(gm_t53at80pfdus3) == 56 && _Alignof(gm_t53at80pfdus3) == 8 && offsetof(gm_t53at80pfdus3, f0) == 0 && offsetof(gm_t53at80pfdus3, f1) == 8, "layout of `TyKind`");
_Static_assert(sizeof(gm_t59juu943t8sl) == 104 && _Alignof(gm_t59juu943t8sl) == 8 && offsetof(gm_t59juu943t8sl, f0) == 0 && offsetof(gm_t59juu943t8sl, f1) == 48 && offsetof(gm_t59juu943t8sl, f2) == 96, "layout of `(lo: Int, hi: Int, inclusive: bool)`");
typedef struct { gm_b16_8 f0; uint64_t f1; gm_ti0kgh3pc3in2 f2; gm_b16_8 f3; gm_b16_8 f4; uint8_t f5; uint8_t p6[7]; gm_b16_8 f6; gm_b16_8 f7; uint8_t f8; uint8_t p9[3]; gm_tl874t2atnlh3 f9; gm_tf5srh4mp4b3i f10; } gm_t6art7bprjiqb;
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint32_t v0; void * v1; gm_t59juu943t8sl v2; void * v3; void * v4; void * v5; gm_ttggq7ng4kp3p v6; gm_b16_8 v7; gm_b16_8 v8; } f1; } gm_tb38v5s9j1r32;
_Static_assert(sizeof(gm_ti0kgh3pc3in2) == 56 && _Alignof(gm_ti0kgh3pc3in2) == 8 && offsetof(gm_ti0kgh3pc3in2, f0) == 0 && offsetof(gm_ti0kgh3pc3in2, f1) == 8, "layout of `AdtKind`");
_Static_assert(sizeof(gm_tngtuiuskn938) == 96 && _Alignof(gm_tngtuiuskn938) == 8 && offsetof(gm_tngtuiuskn938, f0) == 0 && offsetof(gm_tngtuiuskn938, f1) == 48, "layout of `Rat`");
typedef struct { gm_tsth9qu1dv0g3 f0; gm_b16_8 f1; } gm_to76jli3bqddq;
_Static_assert(sizeof(gm_toa2vhe3l4hfr) == 40 && _Alignof(gm_toa2vhe3l4hfr) == 8 && offsetof(gm_toa2vhe3l4hfr, f0) == 0 && offsetof(gm_toa2vhe3l4hfr, f1) == 8, "layout of `GenericArg`");
_Static_assert(sizeof(gm_tsth9qu1dv0g3) == 40 && _Alignof(gm_tsth9qu1dv0g3) == 8 && offsetof(gm_tsth9qu1dv0g3, f0) == 0 && offsetof(gm_tsth9qu1dv0g3, f1) == 8, "layout of `Callee`");
typedef struct { gm_tngtuiuskn938 f0; gm_tngtuiuskn938 f1; } gm_tu15p0hi2qlv6;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tbci5ospk5nnn v0; gm_tngtuiuskn938 v1; uint8_t v2; gm_tu15p0hi2qlv6 v3; } f1; } gm_t0ialelq3ql1k;
_Static_assert(sizeof(gm_t2ubvi1j1aq22) == 40 && _Alignof(gm_t2ubvi1j1aq22) == 8 && offsetof(gm_t2ubvi1j1aq22, a) == 0, "layout of `[1]GenericArg`");
_Static_assert(sizeof(gm_t6art7bprjiqb) == 184 && _Alignof(gm_t6art7bprjiqb) == 8 && offsetof(gm_t6art7bprjiqb, f0) == 0 && offsetof(gm_t6art7bprjiqb, f1) == 16 && offsetof(gm_t6art7bprjiqb, f2) == 24 && offsetof(gm_t6art7bprjiqb, f3) == 80 && offsetof(gm_t6art7bprjiqb, f4) == 96 && offsetof(gm_t6art7bprjiqb, f5) == 112 && offsetof(gm_t6art7bprjiqb, f6) == 120 && offsetof(gm_t6art7bprjiqb, f7) == 136 && offsetof(gm_t6art7bprjiqb, f8) == 152 && offsetof(gm_t6art7bprjiqb, f9) == 156 && offsetof(gm_t6art7bprjiqb, f10) == 168, "layout of `AdtInfo`");
_Static_assert(sizeof(gm_tb38v5s9j1r32) == 112 && _Alignof(gm_tb38v5s9j1r32) == 8 && offsetof(gm_tb38v5s9j1r32, f0) == 0 && offsetof(gm_tb38v5s9j1r32, f1) == 8, "layout of `PatKind`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_tb38v5s9j1r32 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_tc9qv69frb08e;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tbci5ospk5nnn v0; double v1; void * v2; void * v3; uint8_t v4; gm_b16_8 v5; gm_b16_8 v6; uint32_t v7; gm_tkn42u7na3t81 v8; gm_tkn42u7na3t81 v9; gm_t88rcucg1b7uk v10; gm_t3rcjibl8kkg4 v11; gm_t08olq97rscn3 v12; uint32_t v13; gm_tcc1rq4n1avb2 v14; gm_t3877pk7lv748 v15; void * v16; gm_tbl8il92l97ut v17; gm_tlcl4u558l6p5 v18; gm_tao63pk3r18v5 v19; void * v20; void * v21; void * v22; void * v23; uint32_t v24; uint32_t v25; uint32_t v26; gm_t8ghgl2jj0qha v27; gm_trl9d7m1k5lbd v28; gm_ta612fil812pm v29; gm_t8jsc4n9vp2ib v30; gm_tt8o5kebou62r v31; gm_to76jli3bqddq v32; gm_b16_8 v33; gm_b16_8 v34; void * v35; gm_ttggq7ng4kp3p v36; void * v37; gm_tsplfr33dsvji v38; gm_t3dcoc7077t1q v39; gm_t083kd7b1kkag v40; gm_tlcl4u558l6p5 v41; gm_tkaapcii86udk v42; gm_tlcl4u558l6p5 v43; void * v44; gm_trl2khmuphncd v45; gm_t710lsrq1m8gk v46; gm_teo9d4mncjfdu v47; gm_te9hrk0u35rev v48; gm_ttggq7ng4kp3p v49; void * v50; gm_t3mf58gv9kf3n v51; void * v52; void * v53; void * v54; gm_tandvbt63uk7n v55; gm_t710lsrq1m8gk v56; } f1; } gm_teti411eujvf4;
_Static_assert(sizeof(gm_to76jli3bqddq) == 56 && _Alignof(gm_to76jli3bqddq) == 8 && offsetof(gm_to76jli3bqddq, f0) == 0 && offsetof(gm_to76jli3bqddq, f1) == 40, "layout of `(callee: Callee, args: []Expr)`");
_Static_assert(sizeof(gm_tu15p0hi2qlv6) == 192 && _Alignof(gm_tu15p0hi2qlv6) == 8 && offsetof(gm_tu15p0hi2qlv6, f0) == 0 && offsetof(gm_tu15p0hi2qlv6, f1) == 96, "layout of `(re: Rat, im: Rat)`");
_Static_assert(sizeof(gm_t0ialelq3ql1k) == 200 && _Alignof(gm_t0ialelq3ql1k) == 8 && offsetof(gm_t0ialelq3ql1k, f0) == 0 && offsetof(gm_t0ialelq3ql1k, f1) == 8, "layout of `Untyped`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_teti411eujvf4 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_t69m56meqefs8;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t88rcucg1b7uk v0; uint32_t v1; uint32_t v2; gm_tiilmfd16v3fc v3; gm_tnpifv20irnph v4; uint32_t v5; gm_tcc1rq4n1avb2 v6; gm_t88rcucg1b7uk v7; gm_t0ialelq3ql1k v8; uint64_t v9; gm_t88rcucg1b7uk v10; } f1; } gm_t6qooom8qa2do;
_Static_assert(sizeof(gm_tc9qv69frb08e) == 136 && _Alignof(gm_tc9qv69frb08e) == 8 && offsetof(gm_tc9qv69frb08e, f0) == 0 && offsetof(gm_tc9qv69frb08e, f1) == 8 && offsetof(gm_tc9qv69frb08e, f2) == 120, "layout of `Pat`");
_Static_assert(sizeof(gm_teti411eujvf4) == 64 && _Alignof(gm_teti411eujvf4) == 8 && offsetof(gm_teti411eujvf4, f0) == 0 && offsetof(gm_teti411eujvf4, f1) == 8, "layout of `ExprKind`");
_Static_assert(sizeof(gm_t69m56meqefs8) == 88 && _Alignof(gm_t69m56meqefs8) == 8 && offsetof(gm_t69m56meqefs8, f0) == 0 && offsetof(gm_t69m56meqefs8, f1) == 8 && offsetof(gm_t69m56meqefs8, f2) == 72, "layout of `Expr`");
_Static_assert(sizeof(gm_t6qooom8qa2do) == 208 && _Alignof(gm_t6qooom8qa2do) == 8 && offsetof(gm_t6qooom8qa2do, f0) == 0 && offsetof(gm_t6qooom8qa2do, f1) == 8, "layout of `DeclValue`");
struct gm_fr_adt_luat93d {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_compiler_adt_eiq6rf1 {
    gm_frame h;
    gm_ti0kgh3pc3in2 l2; /* kind */
    gm_trqfvmjtt4sij l4; /* defaults */
    gm_trqfvmjtt4sij l5; /* default_exprs */
    gm_t6art7bprjiqb l8; /* info */
    gm_t6art7bprjiqb ret;
};
struct gm_fr_core_fn_of_jr7r6ef {
    gm_frame h;
    gm_t9805qtonbsag ret;
};
struct gm_fr_empty_slice_3v2gjd4_3 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_5nn7ph0 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_8q3kspo_2 {
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
struct gm_fr_empty_slice_ose1ba9 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_rcd2jln {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_s1lbe5s {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_vj3ckdj_2 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_struct_b5i94nj {
    gm_frame h;
    gm_ti0kgh3pc3in2 ret;
};
struct gm_fr_error_89glhjr {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_error_ki8i3k6 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_fresh_adt_u9rnf9f {
    gm_frame h;
    uint32_t ret;
};
struct gm_fr_has_param_u9t0efs {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_error_8k27gd1 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_kind_jcnt4hm {
    gm_frame h;
    gm_t53at80pfdus3 ret;
};
struct gm_fr_list_58nr42v_3 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_epvducg {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_ir0h3tc_2 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_pmp06kq_2 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_s83jn2q {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_spqn36o {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_uvfu1k1 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_vp1djv1_2 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_lookup_59juqoo {
    gm_frame h;
    gm_tf5srh4mp4b3i ret;
};
struct gm_fr_many_ptr_l4v11f8 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_mk_9bc97fd {
    gm_frame h;
    gm_teti411eujvf4 l1; /* kind */
    void * ret;
};
struct gm_fr_mk_pat_hr2l6c4 {
    gm_frame h;
    gm_tb38v5s9j1r32 l1; /* kind */
    void * ret;
};
struct gm_fr_one_arg_f1gj3pp {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* out */
    gm_b16_8 ret;
};
struct gm_fr_printer_h7jigd7 {
    gm_frame h;
    gm_tmm67r2c94d5j ret;
};
struct gm_fr_report_eardf07 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_report_soa_errors_m1bqopj {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* seen */
    gm_trqfvmjtt4sij l3; /* errors */
    gm_b0_1 ret;
};
struct gm_fr_result_o9ras10 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_same_len_bnkvi6r {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_set_template_o09q96o {
    gm_frame h;
    gm_b0_1 ret;
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
struct gm_fr_slice_5dt9121 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_soa_block_ir2b1jv {
    gm_frame h;
    void * ret;
};
struct gm_fr_soa_column_7a5htvb {
    gm_frame h;
    void * ret;
};
struct gm_fr_soa_copy_columns_s4nhe5h {
    gm_frame h;
    gm_trqfvmjtt4sij l9; /* stmts */
    gm_t0mglksrkfa5e l13; /* field */
    gm_trqfvmjtt4sij l16; /* pending */
    gm_trqfvmjtt4sij l19; /* args */
    void * ret;
};
struct gm_fr_soa_core_sd8eq62 {
    gm_frame h;
    gm_trqfvmjtt4sij l7; /* pending */
    gm_tn6p6lpnve7b0 ret;
};
struct gm_fr_soa_expand_2e8rmqf {
    gm_frame h;
    void * ret;
};
struct gm_fr_soa_fields_dg06fgc {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_soa_free_columns_n1tvn1j {
    gm_frame h;
    gm_trqfvmjtt4sij l11; /* args */
    void * ret;
};
struct gm_fr_soa_gather_of_4timqsa {
    gm_frame h;
    gm_trqfvmjtt4sij l6; /* parts */
    gm_t0mglksrkfa5e l10; /* f */
    void * ret;
};
struct gm_fr_soa_generic_kvk21mq {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_soa_info_pq4q43l {
    gm_frame h;
    gm_t8cc1mtbrhlpt ret;
};
struct gm_fr_soa_layout_n4ibc7b {
    gm_frame h;
    gm_trqfvmjtt4sij l4; /* sizes */
    gm_trqfvmjtt4sij l5; /* aligns */
    gm_t0mglksrkfa5e l8; /* f */
    gm_t3mf58gv9kf3n ret;
};
struct gm_fr_soa_len_of_4l5vk85 {
    gm_frame h;
    void * ret;
};
struct gm_fr_soa_make_columns_ardr0ua {
    gm_frame h;
    gm_trqfvmjtt4sij l10; /* args */
    gm_trqfvmjtt4sij l17; /* parts */
    gm_trqfvmjtt4sij l26; /* arms */
    void * ret;
};
struct gm_fr_soa_repeatable_rqn3pcn {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_soa_scatter_h5ruqu8 {
    gm_frame h;
    gm_trqfvmjtt4sij l7; /* stmts */
    gm_t0mglksrkfa5e l11; /* f */
    gm_b16_8 ret;
};
struct gm_fr_soa_shape_755ppqp {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_soa_shape_of_ohenvjc {
    gm_frame h;
    gm_tbm0n9ujhrlhf ret;
};
struct gm_fr_soa_slice_column_6ogdk5f {
    gm_frame h;
    void * ret;
};
struct gm_fr_soa_slice_columns_kqq8dfr {
    gm_frame h;
    gm_trqfvmjtt4sij l11; /* parts */
    void * ret;
};
struct gm_fr_soa_stand_in_kdtmkn7 {
    gm_frame h;
    gm_t2ubvi1j1aq22 l5; /* <temporary array> */
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_soa_temp_2pshn8g {
    gm_frame h;
    uint32_t ret;
};
struct gm_fr_soa_template_fa784hs {
    gm_frame h;
    uint32_t ret;
};
struct gm_fr_soa_to_const_hhiu49k {
    gm_frame h;
    gm_trqfvmjtt4sij l7; /* parts */
    void * ret;
};
struct gm_fr_soa_type_42lvsj0 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_soa_type_expr_go3p0n7 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_soa_type_of_q8vtd5j {
    gm_frame h;
    gm_t6art7bprjiqb l8; /* info */
    gm_tpautr4br7h3l l10; /* def */
    gm_t0mglksrkfa5e l13; /* f */
    gm_trqfvmjtt4sij l18; /* columns */
    gm_t0mglksrkfa5e l21; /* f */
    gm_t2ubvi1j1aq22 l26; /* <temporary array> */
    gm_te3magq8drarp ret;
};
struct gm_fr_subst_soa_2chqte3 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_unit_2age34u {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_usize_lit_a8hojea {
    gm_frame h;
    void * ret;
};
struct gm_fr_usize_ty_qnrd30f {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b);
inline bool gm_eq467d5cm1d5(const uint8_t *a, const uint8_t *b);
inline bool gm_eq9m6mvtmgde(const uint8_t *a, const uint8_t *b);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_adt_luat93d(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_t5e6kkuq21nig gm_f_array_len_eielif2(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_array_th8sdi3(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) void * gm_f_clone_expr_af0u1qc(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_compiler_adt_eiq6rf1(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, uint64_t a1, const void * a2, uint64_t a3);
__attribute__((aligned(4))) gm_t9805qtonbsag gm_f_core_fn_of_jr7r6ef(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_3v2gjd4_3(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_5nn7ph0(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_8q3kspo_2(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_g3tddrb_2(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ose1ba9(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_rcd2jln(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_s1lbe5s(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_vj3ckdj_2(void);
inline __attribute__((aligned(4))) void gm_f_empty_struct_b5i94nj(void *gm_out);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_error_89glhjr(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_error_ki8i3k6(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_error_s6hn871(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_format_16dceus(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_3dfoht6(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_4nn4ect(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_agi9f7f(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_d04m6io(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
__attribute__((aligned(4))) void gm_f_format_fambdq4(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_j0i128e(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_mlqiori(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_thmq1t9(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) uint32_t gm_f_fresh_adt_u9rnf9f(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_from_u64_ud13q5v(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint64_t a0);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_get_espvf96(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_get_nt3g9ko(void *gm_out, void * a0, uint32_t a1);
inline __attribute__((aligned(4))) uint8_t gm_f_has_param_u9t0efs(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) uint32_t gm_f_instantiate_35r5pb2(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_b16_8 a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) void gm_f_int_key_dmcraup(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, const void * a2);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_intern_ia19mhl(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_error_8k27gd1(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void gm_f_kind_jcnt4hm(void *gm_out, void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void gm_f_layout_a5ovnrm(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) void gm_f_list_58nr42v_3(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_epvducg(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_ir0h3tc_2(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_pmp06kq_2(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_s83jn2q(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_spqn36o(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_vp1djv1_2(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_lookup_59juqoo(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_many_ptr_l4v11f8(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, gm_t88rcucg1b7uk a2);
inline __attribute__((aligned(4))) void * gm_f_mk_9bc97fd(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0, const void * a1, gm_tl874t2atnlh3 a2);
inline __attribute__((aligned(4))) void * gm_f_mk_pat_hr2l6c4(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0, const void * a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_1b2khgi(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_new_adt_info_5pl2srl(void *gm_out, gm_b16_8 a0, uint64_t a1, const void * a2, gm_cseahvg8v7p0vs a3, gm_tf5srh4mp4b3i a4);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_dsbahke(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_ve8bb01_2(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_one_arg_f1gj3pp(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) void gm_f_printer_h7jigd7(void *gm_out, void * a0, void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_3dc3qci(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_3ef746b(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_am7bo0v(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_bpovr2k_3(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_d6c76ud_2(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_d943vgp_2(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_im9d4ro(void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_j84ih6c(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_mf0vgpj(void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_push_o4ckdr1(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_r2k6rjf_2(void * a0, void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_vd1290p(void * a0, uint8_t a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_vrmo7c5(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_ftmshk7(void * a0, uint32_t a1, const void * a2);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_report_eardf07(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_report_soa_errors_m1bqopj(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1);
__attribute__((aligned(4))) void gm_f_resolve_decl_pus042v(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_resolve_type_jjodbei(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_result_o9ras10(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) uint8_t gm_f_same_len_bnkvi6r(gm_tf5srh4mp4b3i a0, gm_tf5srh4mp4b3i a1);
inline __attribute__((aligned(4))) void gm_f_set_template_o09q96o(void * a0, uint32_t a1, uint32_t a2);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_8uijkbs(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_kmjeh8s(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_slice_5dt9121(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) void * gm_f_soa_block_ir2b1jv(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, void * a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) void * gm_f_soa_column_7a5htvb(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, const void * a2, uint64_t a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) void * gm_f_soa_copy_columns_s4nhe5h(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, const void * a2, void * a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) void gm_f_soa_core_sd8eq62(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, const void * a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) void * gm_f_soa_expand_2e8rmqf(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_soa_fields_dg06fgc(void * a0, const void * a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_soa_fields_dg06fgc_c(void * a0, gm_tjpqvfd72o62u a1);
__attribute__((aligned(4))) void * gm_f_soa_free_columns_n1tvn1j(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, const void * a2, void * a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) void * gm_f_soa_gather_of_4timqsa(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, const void * a2, void * a3, gm_tl874t2atnlh3 a4);
inline __attribute__((aligned(4))) uint8_t gm_f_soa_generic_kvk21mq(void * a0, const void * a1);
__attribute__((aligned(4))) uint8_t gm_f_soa_generic_kvk21mq_c(void * a0, gm_tjpqvfd72o62u a1);
__attribute__((aligned(4))) void gm_f_soa_info_pq4q43l(void *gm_out, void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) gm_t8cc1mtbrhlpt gm_f_soa_info_pq4q43l_c(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) gm_t3mf58gv9kf3n gm_f_soa_layout_n4ibc7b(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) void * gm_f_soa_len_of_4l5vk85(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, const void * a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) void * gm_f_soa_make_columns_ardr0ua(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, const void * a3, void * a4, void * a5, gm_tl874t2atnlh3 a6);
__attribute__((aligned(4))) uint8_t gm_f_soa_repeatable_rqn3pcn(void * a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_soa_scatter_h5ruqu8(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, const void * a2, void * a3, void * a4, gm_tl874t2atnlh3 a5);
__attribute__((aligned(4))) gm_b16_8 gm_f_soa_shape_755ppqp(void *env, gm_node *cx, gm_node *inst, gm_tf5srh4mp4b3i a0, uint8_t a1);
__attribute__((aligned(4))) void gm_f_soa_shape_of_ohenvjc(void *gm_out, void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_tbm0n9ujhrlhf gm_f_soa_shape_of_ohenvjc_c(void * a0, uint32_t a1);
__attribute__((aligned(4))) void * gm_f_soa_slice_column_6ogdk5f(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, const void * a2, uint64_t a3, uint8_t a4, void * a5, void * a6, uint8_t a7, gm_t88rcucg1b7uk a8, gm_tl874t2atnlh3 a9);
__attribute__((aligned(4))) void * gm_f_soa_slice_columns_kqq8dfr(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, const void * a2, uint8_t a3, void * a4, void * a5, uint8_t a6, gm_tl874t2atnlh3 a7);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_soa_stand_in_kdtmkn7(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_tf5srh4mp4b3i a2, uint8_t a3);
__attribute__((aligned(4))) uint32_t gm_f_soa_temp_2pshn8g(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) uint32_t gm_f_soa_template_fa784hs(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tf5srh4mp4b3i a1, uint8_t a2);
__attribute__((aligned(4))) void * gm_f_soa_to_const_hhiu49k(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, gm_t88rcucg1b7uk a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_soa_type_42lvsj0(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_tf5srh4mp4b3i a2, uint8_t a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_soa_type_expr_go3p0n7(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) void gm_f_soa_type_of_q8vtd5j(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_tf5srh4mp4b3i a2, uint8_t a3);
__attribute__((aligned(4))) void gm_f_struct_fields_chqu3it(void *gm_out, void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_subst_soa_2chqte3(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, const void * a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_ty_vka2r8h(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_unit_2age34u(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) void * gm_f_usize_lit_a8hojea(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_tl874t2atnlh3 a2);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_usize_ty_qnrd30f(void *env, gm_node *cx, gm_node *inst, void * a0);
extern uint8_t gm_cejn1dj0054[12] __attribute__((aligned(4)));
extern uint8_t gm_k2ok1rai48t[40] __attribute__((aligned(8)));
extern uint8_t gm_k3tttsr3hp1[40] __attribute__((aligned(8)));
extern uint8_t gm_k652m2ksgeg[40] __attribute__((aligned(8)));
extern uint8_t gm_k6tu981lmau[40] __attribute__((aligned(8)));
extern uint8_t gm_khikhlemh0i[40] __attribute__((aligned(8)));
extern const uint8_t gm_s3ndsddjcvskd[3];
extern const uint8_t gm_s58rgrgmho9sa[68];
extern const uint8_t gm_sfflovsm8rlpo[51];
extern const uint8_t gm_sg7n1u5l63uaa[9];
extern const uint8_t gm_sgvvkov5kvprt[10];
extern const uint8_t gm_shsv7bl83q04d[9];
extern const uint8_t gm_shv1k5m05egbe[6];
extern const uint8_t gm_sidairr6d8hcq[45];
extern const uint8_t gm_sm2k5tp7dtg07[4];
extern const char gm_src0ih77f43mhf2[];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_src4nj79mj9jk57[];
extern const char gm_srcc2qutubarto1[];
extern const char gm_srcod98slk3vnra[];
extern const char gm_srcrgc9bkquf6nr[];
extern const uint8_t gm_ss496b65r9omj[6];
extern const uint8_t gm_ssnb4or831s5l[78];
extern const uint8_t gm_suh0878eve8t9[10];
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_adt_luat93d(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_b16_8 a2) {
    struct gm_fr_adt_luat93d F;
    void * gm_l0; /* t */
    uint32_t gm_l1; /* id */
    gm_b16_8 gm_l2; /* args */
    GM_STACK_CHECK(&F, 148u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t1 = gm_l0;
    gm_tiilmfd16v3fc t2;
    memset((uint8_t *)&t2 + 4, 0, 4);
    uint32_t t3 = gm_l1;
    GM_ST(uint32_t, (uint8_t *)&t2 + 0, t3);
    gm_b16_8 t4 = gm_l2;
    GM_ST(gm_b16_8, (uint8_t *)&t2 + 8, t4);
    gm_t53at80pfdus3 t5;
    GM_ST(uint8_t, (uint8_t *)&t5 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t5 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t5 + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&t5 + 32, 0);
    GM_ST(uint64_t, (uint8_t *)&t5 + 40, 0);
    GM_ST(uint64_t, (uint8_t *)&t5 + 48, 0);
    GM_ST(uint8_t, ((uint8_t *)&t5 + 0), ((uint8_t)17ULL));
    GM_ST(gm_tiilmfd16v3fc, ((uint8_t *)&t5 + 8), t2);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_3v2gjd4_3(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_3v2gjd4_3 F;
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
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 24)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_8q3kspo_2(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_8q3kspo_2 F;
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
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 8)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ose1ba9(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_ose1ba9 F;
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
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 40)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_s1lbe5s(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_s1lbe5s F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_vj3ckdj_2(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_vj3ckdj_2 F;
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
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 32)));
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
inline __attribute__((aligned(4))) void gm_f_empty_struct_b5i94nj(void *gm_out) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_struct_b5i94nj F;
    GM_STACK_CHECK_SHALLOW(&F, 144u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_tpautr4br7h3l t1;
    memset((uint8_t *)&t1 + 17, 0, 7);
    gm_b16_8 t2 = gm_f_empty_slice_ose1ba9();
    GM_ST(gm_b16_8, (uint8_t *)&t1 + 0, t2);
    GM_ST(uint8_t, (uint8_t *)&t1 + 16, ((uint8_t)0));
    gm_tf5srh4mp4b3i t3;
    memset(&t3, 0, sizeof t3);
    GM_ST(gm_tf5srh4mp4b3i, (uint8_t *)&t1 + 24, t3);
    gm_tkn42u7na3t81 t4;
    memset(&t4, 0, sizeof t4);
    GM_ST(gm_tkn42u7na3t81, (uint8_t *)&t1 + 40, t4);
    gm_ti0kgh3pc3in2 t5;
    GM_ST(uint8_t, (uint8_t *)&t5 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t5 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t5 + 4, 0);
    GM_ST(uint8_t, ((uint8_t *)&t5 + 0), ((uint8_t)0ULL));
    GM_ST(gm_tpautr4br7h3l, ((uint8_t *)&t5 + 8), t1);
    F.ret = t5;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
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
inline __attribute__((aligned(4))) uint32_t gm_f_fresh_adt_u9rnf9f(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1) {
    struct gm_fr_fresh_adt_u9rnf9f F;
    void * gm_l0; /* t */
    gm_b16_8 gm_l1; /* name */
    uint32_t gm_l2; /* id */
    GM_STACK_CHECK(&F, 121u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        uint64_t t3 = GM_LD(uintptr_t, ((((uint8_t *)t2) + 136) + 0) + 8);
        uint32_t t4 = (uint32_t)t3;
        gm_l2 = t4;
    }
    {
        void * t5 = gm_l0;
        void * t6 = (void *)(((uint8_t *)t5) + 136);
        gm_b16_8 t7 = gm_l1;
        gm_tftb1odc739t5 t8 = gm_f_push_3ef746b(t6, t7);
        gm_tftb1odc739t5 t9 = t8;
        if (((((uint8_t *)&t9))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t9) + 1, gm_src0ih77f43mhf2, 263, 5);
        gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
    }
    {
        void * t11 = gm_l0;
        void * t12 = (void *)(((uint8_t *)t11) + 176);
        uint32_t t13 = gm_l2;
        gm_tftb1odc739t5 t14 = gm_f_push_im9d4ro(t12, t13);
        gm_tftb1odc739t5 t15 = t14;
        if (((((uint8_t *)&t15))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t15) + 1, gm_src0ih77f43mhf2, 264, 5);
        gm_b0_1 t16 = GM_LD(gm_b0_1, (((uint8_t *)&t15) + 1));
    }
    {
        uint32_t t17 = gm_l2;
        uint8_t t18 = 0;
        F.ret = t17;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_has_param_u9t0efs(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_has_param_u9t0efs F;
    void * gm_l0; /* t */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    GM_STACK_CHECK_SHALLOW(&F, 53u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    uint32_t t1 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 0));
    uint64_t t2 = (uint64_t)t1;
    uint64_t t3 = (uintptr_t)t2;
    void * t4 = gm_l0;
    uint64_t t5 = GM_LD(uintptr_t, ((((uint8_t *)t4) + 40) + 0) + 8);
    if (t3 >= (uintptr_t)(t5)) gm_trap_index(t3, t5, gm_src0ih77f43mhf2, 249, 57);
    uint8_t t6 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t4) + 40) + 0)) + t3 * 1));
    uint8_t t7 = (uint8_t)(t6 & ((uint8_t)1ULL));
    uint8_t t8 = (!(t7 == ((uint8_t)0ULL)));
    F.ret = t8;
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
inline __attribute__((aligned(4))) void gm_f_list_58nr42v_3(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_58nr42v_3 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_3v2gjd4_3();
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
inline __attribute__((aligned(4))) void gm_f_list_ir0h3tc_2(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_ir0h3tc_2 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_vj3ckdj_2();
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
inline __attribute__((aligned(4))) void gm_f_list_pmp06kq_2(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_pmp06kq_2 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_8q3kspo_2();
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
inline __attribute__((aligned(4))) void gm_f_list_s83jn2q(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_s83jn2q F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_s1lbe5s();
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
inline __attribute__((aligned(4))) void gm_f_list_spqn36o(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_spqn36o F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_ose1ba9();
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_many_ptr_l4v11f8(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, gm_t88rcucg1b7uk a2) {
    struct gm_fr_many_ptr_l4v11f8 F;
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
    GM_ST(uint8_t, ((uint8_t *)&t5 + 0), ((uint8_t)9ULL));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_one_arg_f1gj3pp(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_one_arg_f1gj3pp F;
    void * gm_l0; /* e */
    GM_STACK_CHECK(&F, 77u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_vp1djv1_2((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        void * t3 = gm_l0;
        gm_tftb1odc739t5 t4 = gm_f_push_d943vgp_2(t2, &(*(gm_t69m56meqefs8 *)((uint8_t *)t3)));
        gm_tftb1odc739t5 t5 = t4;
        if (((((uint8_t *)&t5))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t5) + 1, gm_src4nj79mj9jk57, 404, 5);
        gm_b0_1 t6 = GM_LD(gm_b0_1, (((uint8_t *)&t5) + 1));
    }
    {
        gm_b16_8 t7 = GM_LD(gm_b16_8, (((uint8_t *)&F.l1) + 0));
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_result_o9ras10(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_t88rcucg1b7uk a2) {
    struct gm_fr_result_o9ras10 F;
    void * gm_l0; /* t */
    gm_t88rcucg1b7uk gm_l1; /* err */
    gm_t88rcucg1b7uk gm_l2; /* ok */
    GM_STACK_CHECK(&F, 108u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t1 = gm_l0;
    gm_t88rcucg1b7uk t2 = gm_l1;
    gm_t88rcucg1b7uk t3 = gm_l2;
    gm_tgglga8ro3isc t4;
    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t4 + 0), t2);
    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t4 + 4), t3);
    gm_t53at80pfdus3 t5;
    memset(&t5, 0, sizeof t5);
    GM_ST(uint8_t, ((uint8_t *)&t5 + 0), ((uint8_t)14ULL));
    GM_ST(gm_tgglga8ro3isc, ((uint8_t *)&t5 + 8), t4);
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
inline __attribute__((aligned(4))) void gm_f_set_template_o09q96o(void * a0, uint32_t a1, uint32_t a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_set_template_o09q96o F;
    void * gm_l0; /* t */
    uint32_t gm_l1; /* id */
    uint32_t gm_l2; /* template */
    GM_STACK_CHECK_SHALLOW(&F, 56u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        uint32_t t2 = gm_l1;
        uint64_t t3 = (uint64_t)t2;
        uint64_t t4 = (uintptr_t)t3;
        void * t5 = gm_l0;
        uint64_t t6 = GM_LD(uintptr_t, ((((uint8_t *)t5) + 176) + 0) + 8);
        if (t4 >= (uintptr_t)(t6)) gm_trap_index(t4, t6, gm_src0ih77f43mhf2, 272, 5);
        uint32_t t7 = gm_l2;
        GM_ST(uint32_t, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t5) + 176) + 0)) + t4 * 4), t7);
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_soa_fields_dg06fgc(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_soa_fields_dg06fgc F;
    void * gm_l0; /* s */
    gm_tjpqvfd72o62u gm_l1; /* info */
    GM_STACK_CHECK_SHALLOW(&F, 212u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    gm_t88rcucg1b7uk t2 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)a1) + 0));
    gm_th2lqbo77pvsc t3;
    gm_f_struct_fields_chqu3it((void *)&t3, t1, GM_CONV(gm_csguankko4lmhd, t2));
    gm_th2lqbo77pvsc t4 = t3;
    gm_b16_8 t5;
    if (((((uint8_t *)&t4))[16] != 0)) {
        gm_b16_8 t6 = GM_LD(gm_b16_8, (((uint8_t *)&t4) + 0));
        t5 = t6;
    } else {
        gm_b16_8 t7 = gm_f_empty_slice_ose1ba9();
        t5 = t7;
    }
    gm_b16_8 t8 = t5;
    gm_b16_8 t9 = GM_LD(gm_b16_8, ((uint8_t *)&t8));
    F.ret = t9;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_soa_generic_kvk21mq(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_soa_generic_kvk21mq F;
    void * gm_l0; /* s */
    gm_tjpqvfd72o62u gm_l1; /* info */
    GM_STACK_CHECK_SHALLOW(&F, 63u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    void * t2 = (void *)(((uint8_t *)t1) + 8);
    gm_t88rcucg1b7uk t3 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)a1) + 0));
    uint8_t t4 = gm_f_has_param_u9t0efs(t2, GM_CONV(gm_csguankko4lmhd, t3));
    F.ret = t4;
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
inline __attribute__((aligned(4))) void * gm_f_usize_lit_a8hojea(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_tl874t2atnlh3 a2) {
    struct gm_fr_usize_lit_a8hojea F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* v */
    gm_tl874t2atnlh3 gm_l2; /* span */
    GM_STACK_CHECK(&F, 204u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t1 = gm_l0;
    void * t2 = (void *)(((uint8_t *)t1) + 8);
    gm_t88rcucg1b7uk t3 = gm_f_usize_ty_qnrd30f((void *)NULL, cx, NULL, t2);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint64_t t4 = gm_l1;
    gm_tbci5ospk5nnn t5;
    gm_f_from_u64_ud13q5v((void *)&t5, (void *)NULL, cx, NULL, t4);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_teti411eujvf4 t6;
    GM_ST(uint8_t, (uint8_t *)&t6 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t6 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t6 + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&t6 + 56, 0);
    GM_ST(uint8_t, ((uint8_t *)&t6 + 0), ((uint8_t)0ULL));
    GM_ST(gm_tbci5ospk5nnn, ((uint8_t *)&t6 + 8), t5);
    gm_tl874t2atnlh3 t7 = gm_l2;
    void * t8 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t3, &t6, t7);
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
inline __attribute__((aligned(4))) void gm_f_compiler_adt_eiq6rf1(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, uint64_t a1, const void * a2, uint64_t a3) {
    struct gm_fr_compiler_adt_eiq6rf1 F;
    gm_b16_8 gm_l0; /* name */
    uint64_t gm_l1; /* module */
    uint64_t gm_l3; /* fields */
    uint64_t gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    GM_STACK_CHECK(&F, 213u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    memcpy(&F.l2, a2, sizeof F.l2);
    gm_l3 = a3;
    {
        gm_f_list_s83jn2q((void *)&F.l4, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_f_list_pmp06kq_2((void *)&F.l5, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_l7 = ((uint64_t)0ULL);
        }
        {
            uint64_t t3 = gm_l3;
            gm_l6 = t3;
        }
        {
            for (;;) {
                uint64_t t5 = gm_l7;
                uint64_t t6 = gm_l6;
                uint8_t t7 = (t5 < t6);
                if (!t7) goto lb4;
                {
                    uint64_t t10 = gm_l7;
                    uint64_t t11 = t10;
                    {
                        void * t15 = (void *)((uint8_t *)&F.l4);
                        gm_tftb1odc739t5 t16 = gm_f_push_vd1290p(t15, ((uint8_t)0));
                        gm_tftb1odc739t5 t17 = t16;
                        if (((((uint8_t *)&t17))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t17) + 1, gm_src4nj79mj9jk57, 141, 9);
                        gm_b0_1 t18 = GM_LD(gm_b0_1, (((uint8_t *)&t17) + 1));
                    }
                    {
                        void * t19 = (void *)((uint8_t *)&F.l5);
                        gm_tftb1odc739t5 t20 = gm_f_push_r2k6rjf_2(t19, ((void *)0));
                        gm_tftb1odc739t5 t21 = t20;
                        if (((((uint8_t *)&t21))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t21) + 1, gm_src4nj79mj9jk57, 142, 9);
                        gm_b0_1 t22 = GM_LD(gm_b0_1, (((uint8_t *)&t21) + 1));
                    }
                    goto me12;
mn13:;
                    gm_trap("no match arm matched the scrutinee", gm_src4nj79mj9jk57, 140, 5);
me12:;
                }
lc4:;
                {
                    uint64_t t24 = gm_l7;
                    uint64_t r25; bool o26 = __builtin_add_overflow(t24, ((uint64_t)1ULL), &r25);
                    uint64_t t27 = r25;
                    gm_l7 = t27;
                }
            }
lb4:;
        }
    }
    {
        gm_b16_8 t28 = gm_l0;
        uint64_t t29 = gm_l1;
        gm_tl874t2atnlh3 t30 = GM_LD(gm_tl874t2atnlh3, gm_cejn1dj0054);
        gm_tf5srh4mp4b3i t31;
        memset(&t31, 0, sizeof t31);
        gm_f_new_adt_info_5pl2srl((void *)&F.l8, t28, t29, &(*(gm_ti0kgh3pc3in2 *)((uint8_t *)&F.l2)), GM_CONV(gm_cseahvg8v7p0vs, t30), t31);
    }
    {
        gm_b16_8 t32 = GM_LD(gm_b16_8, (((uint8_t *)&F.l4) + 0));
        GM_ST(gm_b16_8, (((uint8_t *)&F.l8) + 80), t32);
    }
    {
        gm_b16_8 t33 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 0));
        GM_ST(gm_b16_8, (((uint8_t *)&F.l8) + 96), t33);
    }
    {
        memmove(gm_out, ((uint8_t *)&F.l8), 184);
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
__attribute__((aligned(4))) gm_t9805qtonbsag gm_f_core_fn_of_jr7r6ef(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2) {
    struct gm_fr_core_fn_of_jr7r6ef F;
    void * gm_l0; /* s */
    gm_b16_8 gm_l1; /* module */
    gm_b16_8 gm_l2; /* name */
    gm_b16_8 gm_l3; /* mname */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    uint64_t gm_l6; /* m */
    uint64_t gm_l7; /* d */
    uint32_t gm_l8; /* f */
    GM_STACK_CHECK(&F, 1027u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        gm_b16_8 t2 = gm_l1;
        gm_tnpifv20irnph t3;
        GM_ST(gm_b16_8, ((uint8_t *)&t3 + 0), t2);
        gm_tabb710d9mq2e t4;
        gm_f_format_agi9f7f((void *)&t4, (void *)NULL, cx, NULL, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t5 = t4;
        if (((((uint8_t *)&t5))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t5) + 8, gm_src4nj79mj9jk57, 55, 14);
        gm_b16_8 t6 = GM_LD(gm_b16_8, (((uint8_t *)&t5) + 8));
        gm_l3 = t6;
    }
    {
        {
            void * t8 = gm_l0;
            void * t9 = GM_LD(void *, (((uint8_t *)t8) + 0));
            gm_b16_8 t10 = GM_LD(gm_b16_8, ((((uint8_t *)t9) + 336) + 0));
            gm_l4 = t10;
        }
        {
            gm_l5 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t12 = gm_l5;
                uint64_t t13 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                uint8_t t14 = (t12 < t13);
                if (!t14) goto lb11;
                {
                    uint64_t t17 = gm_l5;
                    uint64_t t18 = (uintptr_t)t17;
                    uint64_t t19 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                    uint64_t t20 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t18 * 8));
                    uint64_t t21 = t20;
                    uint64_t t24 = GM_LD(uint64_t, ((uint8_t *)&t21));
                    gm_l6 = t24;
                    {
                        uint64_t t27 = gm_l6;
                        uint64_t t28 = (uintptr_t)t27;
                        void * t29 = gm_l0;
                        void * t30 = GM_LD(void *, (((uint8_t *)t29) + 0));
                        uint64_t t31 = GM_LD(uintptr_t, ((((uint8_t *)t30) + 200) + 0) + 8);
                        if (t28 >= (uintptr_t)(t31)) gm_trap_index(t28, t31, gm_src4nj79mj9jk57, 57, 12);
                        gm_b16_8 t32 = GM_LD(gm_b16_8, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t30) + 200) + 0)) + t28 * 184) + 0));
                        gm_b16_8 t33 = gm_l3;
                        gm_b16_8 t34 = t33;
                        gm_b16_8 t35 = GM_LD(gm_b16_8, ((uint8_t *)&t34));
                        gm_b16_8 t36 = t32;
                        gm_b16_8 t37 = t35;
                        uint8_t t38 = (!gm_eq0iqiubfm7e(((uint8_t *)&t36), ((uint8_t *)&t37)));
                        if (!t38) goto ie26;
                        {
                            goto lc11;
                        }
                        goto ix26;
ie26:;
ix26:;
                    }
                    {
                        void * t40 = gm_l0;
                        void * t41 = GM_LD(void *, (((uint8_t *)t40) + 0));
                        uint64_t t42 = gm_l6;
                        gm_b16_8 t43 = gm_l2;
                        gm_tf5srh4mp4b3i t44 = gm_f_lookup_59juqoo(t41, t42, t43);
                        gm_tf5srh4mp4b3i t45 = t44;
                        uint64_t t46;
                        if (((((uint8_t *)&t45))[8] != 0)) {
                            uint64_t t47 = GM_LD(uint64_t, (((uint8_t *)&t45) + 0));
                            t46 = t47;
                        } else {
                            gm_t9805qtonbsag t48;
                            memset(&t48, 0, sizeof t48);
                            uint8_t t49 = 0;
                            F.ret = t48;
                            goto gm_fret;
                        }
                        gm_l7 = t46;
                    }
                    {
                        void * t51 = gm_l0;
                        uint64_t t52 = gm_l7;
                        gm_t6qooom8qa2do t53;
                        gm_f_resolve_decl_pus042v((void *)&t53, (void *)NULL, cx, NULL, t51, t52);
                        if (gm_unw.kind) {
                            goto gm_ll15;
                        }
                        gm_t6qooom8qa2do t54 = t53;
                        uint8_t t55 = GM_LD(uint8_t, (((uint8_t *)&t54) + 0));
                        if (t55 != ((uint8_t)5ULL)) goto ie50;
                        uint32_t t56 = GM_LD(uint32_t, (((uint8_t *)&t54) + 8));
                        gm_l8 = t56;
                        {
                            uint32_t t58 = gm_l8;
                            gm_t9805qtonbsag t59;
                            GM_ST(uint8_t, (uint8_t *)&t59 + 5, 0);
                            GM_ST(uint16_t, (uint8_t *)&t59 + 6, 0);
                            GM_ST(uint32_t, ((uint8_t *)&t59), t58);
                            ((uint8_t *)&t59)[4] = 1;
                            uint8_t t60 = 0;
                            F.ret = t59;
                            goto gm_fret;
                        }
                        goto ix50;
ie50:;
                        {
                            gm_t9805qtonbsag t62;
                            memset(&t62, 0, sizeof t62);
                            uint8_t t63 = 0;
                            F.ret = t62;
                            goto gm_fret;
                        }
ix50:;
                    }
                    goto me22;
mn23:;
                    gm_trap("no match arm matched the scrutinee", gm_src4nj79mj9jk57, 56, 5);
me22:;
                }
lc11:;
                {
                    uint64_t t65 = gm_l5;
                    uint64_t r66; bool o67 = __builtin_add_overflow(t65, ((uint64_t)1ULL), &r66);
                    uint64_t t68 = r66;
                    gm_l5 = t68;
                }
            }
gm_ll15:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb11; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc11; }
            }
            goto gm_lad;
lb11:;
        }
    }
    {
        gm_t9805qtonbsag t69;
        memset(&t69, 0, sizeof t69);
        uint8_t t70 = 0;
        F.ret = t69;
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
extern __attribute__((aligned(4))) gm_b16_8 gm_f_one_arg_f1gj3pp(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_report_soa_errors_m1bqopj(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1) {
    struct gm_fr_report_soa_errors_m1bqopj F;
    void * gm_l0; /* s */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    gm_b16_8 gm_l6; /* msg */
    GM_STACK_CHECK(&F, 455u);
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
        void * t2 = gm_l0;
        gm_trqfvmjtt4sij t3 = GM_LD(gm_trqfvmjtt4sij, (((uint8_t *)t2) + 2456));
        F.l3 = t3;
    }
    {
        void * t4 = gm_l0;
        uint8_t *t5 = (((uint8_t *)t4) + 2456);
        gm_f_list_epvducg((void *)&(*(gm_trqfvmjtt4sij *)t5), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t7 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
            gm_l4 = t7;
        }
        {
            gm_l5 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t9 = gm_l5;
                uint64_t t10 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                uint8_t t11 = (t9 < t10);
                if (!t11) goto lb8;
                {
                    uint64_t t14 = gm_l5;
                    uint64_t t15 = (uintptr_t)t14;
                    uint64_t t16 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                    gm_b16_8 t19 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t15 * 16));
                    gm_l6 = t19;
                    {
                        gm_b16_8 t22 = gm_l6;
                        gm_b16_8 t23 = GM_LD(gm_b16_8, (((uint8_t *)&F.l2) + 0));
                        gm_b16_8 t24 = t23;
                        uint8_t t25 = 0;
                        gm_b16_8 t26 = t22;
                        for (uintptr_t i = 0; i < (uintptr_t)(GM_LD(uintptr_t, ((uint8_t *)&t24) + 8)); i++) if (gm_eq0iqiubfm7e(((uint8_t *)GM_LD(void *, ((uint8_t *)&t24))) + i * 16, ((uint8_t *)&t26))) { t25 = 1; break; }
                        uint8_t t27 = (uint8_t)(t25);
                        if (!t27) goto ie21;
                        {
                            goto lc8;
                        }
                        goto ix21;
ie21:;
ix21:;
                    }
                    {
                        void * t29 = gm_l0;
                        gm_tl874t2atnlh3 t30 = gm_l1;
                        gm_b16_8 t31 = gm_l6;
                        gm_tnpifv20irnph t32;
                        GM_ST(gm_b16_8, ((uint8_t *)&t32 + 0), t31);
                        gm_tabb710d9mq2e t33;
                        gm_f_format_mlqiori((void *)&t33, (void *)NULL, cx, NULL, t32);
                        if (gm_unw.kind) {
                            goto gm_ll12;
                        }
                        gm_tabb710d9mq2e t34 = t33;
                        if (((((uint8_t *)&t34))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t34) + 8, gm_src4nj79mj9jk57, 196, 23);
                        gm_b16_8 t35 = GM_LD(gm_b16_8, (((uint8_t *)&t34) + 8));
                        gm_b16_8 t36 = t35;
                        gm_b16_8 t37 = GM_LD(gm_b16_8, ((uint8_t *)&t36));
                        gm_b0_1 t38 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t29, t30, t37);
                        if (gm_unw.kind) {
                            goto gm_ll12;
                        }
                    }
                    {
                        void * t39 = (void *)((uint8_t *)&F.l2);
                        gm_b16_8 t40 = gm_l6;
                        gm_tftb1odc739t5 t41 = gm_f_push_3ef746b(t39, t40);
                        gm_tftb1odc739t5 t42 = t41;
                        if (((((uint8_t *)&t42))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t42) + 1, gm_src4nj79mj9jk57, 197, 9);
                        gm_b0_1 t43 = GM_LD(gm_b0_1, (((uint8_t *)&t42) + 1));
                    }
                    goto me17;
mn18:;
                    gm_trap("no match arm matched the scrutinee", gm_src4nj79mj9jk57, 194, 5);
me17:;
                }
lc8:;
                {
                    uint64_t t45 = gm_l5;
                    uint64_t r46; bool o47 = __builtin_add_overflow(t45, ((uint64_t)1ULL), &r46);
                    uint64_t t48 = r46;
                    gm_l5 = t48;
                }
            }
gm_ll12:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb8; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc8; }
            }
            goto gm_lad;
lb8:;
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
inline __attribute__((aligned(4))) uint8_t gm_f_same_len_bnkvi6r(gm_tf5srh4mp4b3i a0, gm_tf5srh4mp4b3i a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_same_len_bnkvi6r F;
    gm_tf5srh4mp4b3i gm_l0; /* a */
    gm_tf5srh4mp4b3i gm_l1; /* b */
    uint64_t gm_l2; /* x */
    uint64_t gm_l3; /* y */
    GM_STACK_CHECK_SHALLOW(&F, 87u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        if (!((((uint8_t *)&gm_l0))[8] != 0)) goto ie2;
        uint64_t t3 = GM_LD(uint64_t, ((uint8_t *)&gm_l0));
        gm_l2 = t3;
        {
            if (!((((uint8_t *)&gm_l1))[8] != 0)) goto ie5;
            uint64_t t6 = GM_LD(uint64_t, ((uint8_t *)&gm_l1));
            gm_l3 = t6;
            {
                uint64_t t8 = gm_l2;
                uint64_t t9 = gm_l3;
                uint8_t t10 = ((t8 == t9));
                uint8_t t11 = 0;
                F.ret = t10;
                goto gm_fret;
            }
            goto ix5;
ie5:;
ix5:;
        }
        {
            uint8_t t12 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        uint8_t t13;
        if (((((uint8_t *)&gm_l1))[8] != 0)) goto mn15;
        t13 = ((uint8_t)1);
        goto me14;
mn15:;
        t13 = ((uint8_t)0);
        goto me14;
mn16:;
        gm_trap("no match arm matched the scrutinee", gm_src4nj79mj9jk57, 50, 12);
me14:;
        uint8_t t17 = 0;
        F.ret = t13;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void * gm_f_soa_block_ir2b1jv(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, void * a1, gm_tl874t2atnlh3 a2) {
    struct gm_fr_soa_block_ir2b1jv F;
    gm_b16_8 gm_l0; /* stmts */
    void * gm_l1; /* tail */
    gm_tl874t2atnlh3 gm_l2; /* span */
    GM_STACK_CHECK(&F, 283u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        uint64_t t3 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        uint8_t t4 = ((t3 == ((uint64_t)0ULL)));
        if (!t4) goto ie2;
        {
            void * t6 = gm_l1;
            uint8_t t7 = 0;
            F.ret = t6;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t8 = gm_l1;
        gm_t88rcucg1b7uk t9 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t8) + 0));
        gm_tgabaikr08mjg t10;
        memset((uint8_t *)&t10 + 36, 0, 4);
        gm_b16_8 t11 = gm_l0;
        GM_ST(gm_b16_8, (uint8_t *)&t10 + 0, t11);
        void * t12 = gm_l1;
        GM_ST(void *, (uint8_t *)&t10 + 16, t12);
        gm_tl874t2atnlh3 t13 = gm_l2;
        GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t10 + 24, t13);
        gm_tq7s98ihonkju t14 = gm_f_new_dsbahke((void *)NULL, cx, NULL, &t10, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_khikhlemh0i)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tq7s98ihonkju t15 = t14;
        if (((((uint8_t *)&t15))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t15) + 8, gm_src4nj79mj9jk57, 238, 35);
        void * t16 = GM_LD(void *, (((uint8_t *)&t15) + 8));
        gm_teti411eujvf4 t17;
        memset(&t17, 0, sizeof t17);
        GM_ST(uint8_t, ((uint8_t *)&t17 + 0), ((uint8_t)46ULL));
        GM_ST(void *, ((uint8_t *)&t17 + 8), t16);
        gm_tl874t2atnlh3 t18 = gm_l2;
        void * t19 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t9, &t17, t18);
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
__attribute__((aligned(4))) void * gm_f_soa_column_7a5htvb(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, const void * a2, uint64_t a3, gm_tl874t2atnlh3 a4) {
    struct gm_fr_soa_column_7a5htvb F;
    void * gm_l0; /* s */
    void * gm_l1; /* v */
    gm_tjpqvfd72o62u gm_l2; /* info */
    uint64_t gm_l3; /* k */
    gm_tl874t2atnlh3 gm_l4; /* span */
    gm_b16_8 gm_l5; /* fields */
    gm_t88rcucg1b7uk gm_l6; /* ft */
    void * gm_l7; /* col */
    uint8_t gm_l8; /* is_const */
    gm_t88rcucg1b7uk gm_l9; /* elem */
    void * gm_l10; /* len */
    GM_STACK_CHECK(&F, 900u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l3 = a3;
    gm_l4 = a4;
    {
        void * t2 = gm_l0;
        void * t3 = gm_l1;
        gm_t88rcucg1b7uk t4 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t3) + 0));
        gm_th2lqbo77pvsc t5;
        gm_f_struct_fields_chqu3it((void *)&t5, t2, GM_CONV(gm_csguankko4lmhd, t4));
        gm_th2lqbo77pvsc t6 = t5;
        gm_b16_8 t7;
        if (((((uint8_t *)&t6))[16] != 0)) {
            gm_b16_8 t8 = GM_LD(gm_b16_8, (((uint8_t *)&t6) + 0));
            t7 = t8;
        } else {
            gm_b16_8 t9 = gm_f_empty_slice_ose1ba9();
            t7 = t9;
        }
        gm_l5 = t7;
    }
    {
        uint64_t t10 = gm_l3;
        uint64_t t11 = (uintptr_t)t10;
        uint64_t t12 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
        if (t11 >= (uintptr_t)(t12)) gm_trap_index(t11, t12, gm_src4nj79mj9jk57, 258, 11);
        gm_t88rcucg1b7uk t13 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l5)) + t11 * 40) + 16));
        gm_l6 = t13;
    }
    {
        gm_t88rcucg1b7uk t14 = gm_l6;
        void * t15 = gm_l1;
        void * t16 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t15);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint64_t t17 = gm_l3;
        uint32_t t18 = (uint32_t)t17;
        gm_tbl8il92l97ut t19;
        GM_ST(uint32_t, (uint8_t *)&t19 + 12, 0);
        GM_ST(void *, ((uint8_t *)&t19 + 0), t16);
        GM_ST(uint32_t, ((uint8_t *)&t19 + 8), t18);
        gm_teti411eujvf4 t20;
        memset(&t20, 0, sizeof t20);
        GM_ST(uint8_t, ((uint8_t *)&t20 + 0), ((uint8_t)19ULL));
        GM_ST(gm_tbl8il92l97ut, ((uint8_t *)&t20 + 8), t19);
        gm_tl874t2atnlh3 t21 = gm_l4;
        void * t22 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t14, &t20, t21);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l7 = t22;
    }
    {
        uint8_t t24;
        if ((((((uint8_t *)a2) + 8))[8] != 0)) goto mn26;
        t24 = ((uint8_t)0);
        goto me25;
mn26:;
        t24 = ((uint8_t)1);
        goto me25;
mn27:;
        gm_trap("no match arm matched the scrutinee", gm_src4nj79mj9jk57, 260, 8);
me25:;
        if (!t24) goto ie23;
        {
            void * t29 = gm_l7;
            uint8_t t30 = 0;
            F.ret = t29;
            goto gm_fret;
        }
        goto ix23;
ie23:;
ix23:;
    }
    {
        void * t32 = gm_l0;
        void * t33 = (void *)(((uint8_t *)t32) + 8);
        gm_t88rcucg1b7uk t34 = gm_l6;
        gm_t53at80pfdus3 t35;
        gm_f_kind_jcnt4hm((void *)&t35, t33, GM_CONV(gm_csguankko4lmhd, t34));
        gm_t53at80pfdus3 t36 = t35;
        uint8_t t37 = GM_LD(uint8_t, (((uint8_t *)&t36) + 0));
        if (t37 != ((uint8_t)9ULL)) goto ie31;
        uint8_t t38 = GM_LD(uint8_t, ((((uint8_t *)&t36) + 8) + 0));
        gm_l8 = t38;
        gm_t88rcucg1b7uk t39 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t36) + 8) + 4));
        gm_l9 = t39;
        {
            void * t41 = gm_l0;
            void * t42 = (void *)(((uint8_t *)t41) + 8);
            gm_t88rcucg1b7uk t43 = gm_f_usize_ty_qnrd30f((void *)NULL, cx, NULL, t42);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            void * t44 = gm_l1;
            void * t45 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t44);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint64_t t46 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
            uint64_t r47; bool o48 = __builtin_sub_overflow(t46, ((uint64_t)1ULL), &r47);
            if (o48) gm_trap("integer overflow in `-`", gm_src4nj79mj9jk57, 262, 64);
            uint64_t t49 = r47;
            uint32_t t50 = (uint32_t)t49;
            gm_tbl8il92l97ut t51;
            GM_ST(uint32_t, (uint8_t *)&t51 + 12, 0);
            GM_ST(void *, ((uint8_t *)&t51 + 0), t45);
            GM_ST(uint32_t, ((uint8_t *)&t51 + 8), t50);
            gm_teti411eujvf4 t52;
            memset(&t52, 0, sizeof t52);
            GM_ST(uint8_t, ((uint8_t *)&t52 + 0), ((uint8_t)19ULL));
            GM_ST(gm_tbl8il92l97ut, ((uint8_t *)&t52 + 8), t51);
            gm_tl874t2atnlh3 t53 = gm_l4;
            void * t54 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t43, &t52, t53);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l10 = t54;
        }
        {
            void * t55 = gm_l0;
            void * t56 = (void *)(((uint8_t *)t55) + 8);
            uint8_t t57 = gm_l8;
            gm_t88rcucg1b7uk t58 = gm_l9;
            gm_t88rcucg1b7uk t59 = gm_f_slice_5dt9121((void *)NULL, cx, NULL, t56, t57, t58);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tao63pk3r18v5 t60;
            memset((uint8_t *)&t60 + 25, 0, 7);
            void * t61 = gm_l7;
            GM_ST(void *, (uint8_t *)&t60 + 0, t61);
            void * t62 = gm_l0;
            gm_tl874t2atnlh3 t63 = gm_l4;
            void * t64 = gm_f_usize_lit_a8hojea((void *)NULL, cx, NULL, t62, ((uint64_t)0ULL), t63);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(void *, (uint8_t *)&t60 + 8, t64);
            void * t65 = gm_l10;
            GM_ST(void *, (uint8_t *)&t60 + 16, t65);
            GM_ST(uint8_t, (uint8_t *)&t60 + 24, ((uint8_t)0));
            gm_teti411eujvf4 t66;
            GM_ST(uint8_t, (uint8_t *)&t66 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t66 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t66 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t66 + 40, 0);
            GM_ST(uint64_t, (uint8_t *)&t66 + 48, 0);
            GM_ST(uint64_t, (uint8_t *)&t66 + 56, 0);
            GM_ST(uint8_t, ((uint8_t *)&t66 + 0), ((uint8_t)21ULL));
            GM_ST(gm_tao63pk3r18v5, ((uint8_t *)&t66 + 8), t60);
            gm_tl874t2atnlh3 t67 = gm_l4;
            void * t68 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t59, &t66, t67);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t69 = 0;
            F.ret = t68;
            goto gm_fret;
        }
        goto ix31;
ie31:;
        {
            void * t71 = gm_l7;
            uint8_t t72 = 0;
            F.ret = t71;
            goto gm_fret;
        }
ix31:;
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
__attribute__((aligned(4))) void * gm_f_soa_copy_columns_s4nhe5h(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, const void * a2, void * a3, gm_tl874t2atnlh3 a4) {
    struct gm_fr_soa_copy_columns_s4nhe5h F;
    void * gm_l0; /* s */
    void * gm_l1; /* dst */
    gm_tjpqvfd72o62u gm_l2; /* info */
    void * gm_l3; /* src */
    gm_tl874t2atnlh3 gm_l4; /* span */
    void * gm_l5; /* tys */
    uint32_t gm_l6; /* f */
    gm_tjpqvfd72o62u gm_l7; /* from */
    gm_b16_8 gm_l8; /* fields */
    gm_b16_8 gm_l10; /* <tmp> */
    uint64_t gm_l11; /* <tmp> */
    uint64_t gm_l12; /* k */
    void * gm_l14; /* d */
    void * gm_l15; /* c */
    gm_tnqk2qt7j42o9 gm_l17; /* <temporary array> */
    uint32_t gm_l18; /* inst */
    GM_STACK_CHECK(&F, 1901u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l3 = a3;
    gm_l4 = a4;
    {
        void * t2 = gm_l0;
        void * t3 = (void *)(((uint8_t *)t2) + 8);
        gm_l5 = t3;
    }
    {
        void * t4 = gm_l0;
        gm_b16_8 t5;
        GM_ST(void *, (uint8_t *)&t5, (void *)(gm_ss496b65r9omj));
        GM_ST(uintptr_t, (uint8_t *)&t5 + 8, (uintptr_t)(5));
        gm_b16_8 t6;
        GM_ST(void *, (uint8_t *)&t6, (void *)(gm_sgvvkov5kvprt));
        GM_ST(uintptr_t, (uint8_t *)&t6 + 8, (uintptr_t)(9));
        gm_t9805qtonbsag t7 = gm_f_core_fn_of_jr7r6ef((void *)NULL, cx, NULL, t4, t5, t6);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t9805qtonbsag t8 = t7;
        uint32_t t9;
        if (((((uint8_t *)&t8))[4] != 0)) {
            uint32_t t10 = GM_LD(uint32_t, (((uint8_t *)&t8) + 0));
            t9 = t10;
        } else {
            {
                void * t12 = gm_l0;
                gm_tl874t2atnlh3 t13 = gm_l4;
                gm_b16_8 t14;
                GM_ST(void *, (uint8_t *)&t14, (void *)(gm_sidairr6d8hcq));
                GM_ST(uintptr_t, (uint8_t *)&t14 + 8, (uintptr_t)(44));
                gm_b0_1 t15 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t12, t13, t14);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            void * t16 = gm_l5;
            gm_t88rcucg1b7uk t17 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t16);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_b16_8 t18 = gm_f_empty_slice_g3tddrb_2();
            gm_teti411eujvf4 t19;
            memset(&t19, 0, sizeof t19);
            GM_ST(uint8_t, ((uint8_t *)&t19 + 0), ((uint8_t)35ULL));
            GM_ST(gm_b16_8, ((uint8_t *)&t19 + 8), t18);
            gm_tl874t2atnlh3 t20 = gm_l4;
            void * t21 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t17, &t19, t20);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t22 = 0;
            F.ret = t21;
            goto gm_fret;
        }
        gm_l6 = t9;
    }
    {
        void * t23 = gm_l0;
        void * t24 = gm_l3;
        gm_t88rcucg1b7uk t25 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t24) + 0));
        gm_t8cc1mtbrhlpt t26;
        gm_f_soa_info_pq4q43l((void *)&t26, t23, GM_CONV(gm_csguankko4lmhd, t25));
        gm_t8cc1mtbrhlpt t27 = t26;
        gm_tjpqvfd72o62u t28;
        if (((((uint8_t *)&t27))[32] != 0)) {
            gm_tjpqvfd72o62u t29 = GM_LD(gm_tjpqvfd72o62u, (((uint8_t *)&t27) + 0));
            t28 = t29;
        } else {
            void * t30 = gm_l5;
            gm_t88rcucg1b7uk t31 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t30);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_b16_8 t32 = gm_f_empty_slice_g3tddrb_2();
            gm_teti411eujvf4 t33;
            memset(&t33, 0, sizeof t33);
            GM_ST(uint8_t, ((uint8_t *)&t33 + 0), ((uint8_t)35ULL));
            GM_ST(gm_b16_8, ((uint8_t *)&t33 + 8), t32);
            gm_tl874t2atnlh3 t34 = gm_l4;
            void * t35 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t31, &t33, t34);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t36 = 0;
            F.ret = t35;
            goto gm_fret;
        }
        gm_l7 = t28;
    }
    {
        void * t37 = gm_l0;
        gm_b16_8 t38 = gm_f_soa_fields_dg06fgc(t37, &(*(gm_tjpqvfd72o62u *)((uint8_t *)a2)));
        gm_l8 = t38;
    }
    {
        gm_f_list_ir0h3tc_2((void *)&F.l9, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t40 = gm_l8;
            gm_l10 = t40;
        }
        {
            gm_l11 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t42 = gm_l11;
                uint64_t t43 = GM_LD(uintptr_t, ((uint8_t *)&gm_l10) + 8);
                uint8_t t44 = (t42 < t43);
                if (!t44) goto lb41;
                {
                    gm_t8ta482bl6be0 t47;
                    uint64_t t48 = gm_l11;
                    GM_ST(uint64_t, (uint8_t *)&t47 + 0, t48);
                    uint64_t t49 = gm_l11;
                    uint64_t t50 = (uintptr_t)t49;
                    uint64_t t51 = GM_LD(uintptr_t, ((uint8_t *)&gm_l10) + 8);
                    memcpy((uint8_t *)&t47 + 8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l10)) + t50 * 40), 40);
                    gm_t8ta482bl6be0 t52 = t47;
                    uint64_t t55 = GM_LD(uint64_t, (((uint8_t *)&t52) + 0));
                    gm_l12 = t55;
                    gm_t0mglksrkfa5e t56 = GM_LD(gm_t0mglksrkfa5e, (((uint8_t *)&t52) + 8));
                    F.l13 = t56;
                    {
                        void * t58 = gm_l0;
                        void * t59 = gm_l1;
                        uint64_t t60 = gm_l12;
                        gm_tl874t2atnlh3 t61 = gm_l4;
                        void * t62 = gm_f_soa_column_7a5htvb((void *)NULL, cx, NULL, t58, t59, &(*(gm_tjpqvfd72o62u *)((uint8_t *)a2)), t60, t61);
                        if (gm_unw.kind) {
                            goto gm_ll45;
                        }
                        gm_l14 = t62;
                    }
                    {
                        uint8_t t64;
                        if ((((((uint8_t *)a2) + 8))[8] != 0)) goto mn66;
                        t64 = ((uint8_t)0);
                        goto me65;
mn66:;
                        t64 = ((uint8_t)1);
                        goto me65;
mn67:;
                        gm_trap("no match arm matched the scrutinee", gm_src4nj79mj9jk57, 438, 12);
me65:;
                        if (!t64) goto ie63;
                        {
                            void * t69 = gm_l5;
                            gm_t88rcucg1b7uk t70 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l13) + 16));
                            gm_t88rcucg1b7uk t71 = gm_f_slice_5dt9121((void *)NULL, cx, NULL, t69, ((uint8_t)0), t70);
                            if (gm_unw.kind) {
                                goto gm_ll45;
                            }
                            gm_tao63pk3r18v5 t72;
                            memset((uint8_t *)&t72 + 25, 0, 7);
                            void * t73 = gm_l14;
                            GM_ST(void *, (uint8_t *)&t72 + 0, t73);
                            GM_ST(void *, (uint8_t *)&t72 + 8, ((void *)0));
                            GM_ST(void *, (uint8_t *)&t72 + 16, ((void *)0));
                            GM_ST(uint8_t, (uint8_t *)&t72 + 24, ((uint8_t)0));
                            gm_teti411eujvf4 t74;
                            GM_ST(uint8_t, (uint8_t *)&t74 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t74 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t74 + 4, 0);
                            GM_ST(uint64_t, (uint8_t *)&t74 + 40, 0);
                            GM_ST(uint64_t, (uint8_t *)&t74 + 48, 0);
                            GM_ST(uint64_t, (uint8_t *)&t74 + 56, 0);
                            GM_ST(uint8_t, ((uint8_t *)&t74 + 0), ((uint8_t)21ULL));
                            GM_ST(gm_tao63pk3r18v5, ((uint8_t *)&t74 + 8), t72);
                            gm_tl874t2atnlh3 t75 = gm_l4;
                            void * t76 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t71, &t74, t75);
                            if (gm_unw.kind) {
                                goto gm_ll45;
                            }
                            gm_l14 = t76;
                        }
                        goto ix63;
ie63:;
ix63:;
                    }
                    {
                        void * t77 = gm_l0;
                        void * t78 = gm_l3;
                        uint64_t t79 = gm_l12;
                        gm_tl874t2atnlh3 t80 = gm_l4;
                        void * t81 = gm_f_soa_column_7a5htvb((void *)NULL, cx, NULL, t77, t78, &(*(gm_tjpqvfd72o62u *)((uint8_t *)&gm_l7)), t79, t80);
                        if (gm_unw.kind) {
                            goto gm_ll45;
                        }
                        gm_l15 = t81;
                    }
                    {
                        void * t82 = gm_l0;
                        gm_trqfvmjtt4sij t83 = GM_LD(gm_trqfvmjtt4sij, (((uint8_t *)t82) + 2456));
                        F.l16 = t83;
                    }
                    {
                        void * t84 = gm_l0;
                        uint8_t *t85 = (((uint8_t *)t84) + 2456);
                        gm_f_list_epvducg((void *)&(*(gm_trqfvmjtt4sij *)t85), (void *)NULL, cx, NULL);
                        if (gm_unw.kind) {
                            goto gm_ll45;
                        }
                    }
                    {
                    }
                    {
                        void * t86 = gm_l0;
                        uint32_t t87 = gm_l6;
                        {
                            gm_t88rcucg1b7uk t89 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l13) + 16));
                            gm_tnqk2qt7j42o9 t90;
                            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t90 + 0), t89);
                            gm_l17 = t90;
                        }
                        void * t91 = ((uint8_t *)&gm_l17);
                        if ((uintptr_t)0 > (uintptr_t)1 || (uintptr_t)1 > (uintptr_t)1) gm_trap_slice((uintptr_t)0, (uintptr_t)1, 1, gm_src4nj79mj9jk57, 445, 34);
                        gm_b16_8 t92;
                        GM_ST(void *, (uint8_t *)&t92, (void *)(((uint8_t *)t91 + (uintptr_t)0 * 4)));
                        GM_ST(uintptr_t, (uint8_t *)&t92 + 8, (uintptr_t)((uintptr_t)1 - (uintptr_t)0));
                        gm_b16_8 t93 = t92;
                        gm_tl874t2atnlh3 t94 = gm_l4;
                        uint32_t t95 = gm_f_instantiate_35r5pb2((void *)NULL, cx, NULL, t86, t87, t93, t94);
                        if (gm_unw.kind) {
                            goto gm_ll45;
                        }
                        gm_l18 = t95;
                    }
                    {
                        void * t96 = gm_l0;
                        gm_trqfvmjtt4sij t97 = F.l16;
                        GM_ST(gm_trqfvmjtt4sij, (((uint8_t *)t96) + 2456), t97);
                    }
                    {
                        gm_f_list_vp1djv1_2((void *)&F.l19, (void *)NULL, cx, NULL);
                        if (gm_unw.kind) {
                            goto gm_ll45;
                        }
                    }
                    {
                        void * t98 = (void *)((uint8_t *)&F.l19);
                        void * t99 = gm_l14;
                        gm_tftb1odc739t5 t100 = gm_f_push_d943vgp_2(t98, &(*(gm_t69m56meqefs8 *)((uint8_t *)t99)));
                        gm_tftb1odc739t5 t101 = t100;
                        if (((((uint8_t *)&t101))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t101) + 1, gm_src4nj79mj9jk57, 448, 9);
                        gm_b0_1 t102 = GM_LD(gm_b0_1, (((uint8_t *)&t101) + 1));
                    }
                    {
                        void * t103 = (void *)((uint8_t *)&F.l19);
                        void * t104 = gm_l15;
                        gm_tftb1odc739t5 t105 = gm_f_push_d943vgp_2(t103, &(*(gm_t69m56meqefs8 *)((uint8_t *)t104)));
                        gm_tftb1odc739t5 t106 = t105;
                        if (((((uint8_t *)&t106))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t106) + 1, gm_src4nj79mj9jk57, 449, 9);
                        gm_b0_1 t107 = GM_LD(gm_b0_1, (((uint8_t *)&t106) + 1));
                    }
                    {
                        void * t108 = (void *)((uint8_t *)&F.l9);
                        uint32_t t109 = gm_l18;
                        uint64_t t110 = (uint64_t)t109;
                        uint64_t t111 = (uintptr_t)t110;
                        void * t112 = gm_l0;
                        uint64_t t113 = GM_LD(uintptr_t, ((((uint8_t *)t112) + 344) + 0) + 8);
                        if (t111 >= (uintptr_t)(t113)) gm_trap_index(t111, t113, gm_src4nj79mj9jk57, 450, 33);
                        gm_t88rcucg1b7uk t114 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t112) + 344) + 0)) + t111 * 136) + 56));
                        gm_to76jli3bqddq t115;
                        uint32_t t116 = gm_l18;
                        gm_tsth9qu1dv0g3 t117;
                        memset(&t117, 0, sizeof t117);
                        GM_ST(uint8_t, ((uint8_t *)&t117 + 0), ((uint8_t)0ULL));
                        GM_ST(uint32_t, ((uint8_t *)&t117 + 8), t116);
                        GM_ST(gm_tsth9qu1dv0g3, (uint8_t *)&t115 + 0, t117);
                        gm_b16_8 t118 = GM_LD(gm_b16_8, (((uint8_t *)&F.l19) + 0));
                        GM_ST(gm_b16_8, (uint8_t *)&t115 + 40, t118);
                        gm_teti411eujvf4 t119;
                        GM_ST(uint8_t, (uint8_t *)&t119 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t119 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t119 + 4, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t119 + 0), ((uint8_t)34ULL));
                        GM_ST(gm_to76jli3bqddq, ((uint8_t *)&t119 + 8), t115);
                        gm_tl874t2atnlh3 t120 = gm_l4;
                        void * t121 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t114, &t119, t120);
                        if (gm_unw.kind) {
                            goto gm_ll45;
                        }
                        gm_trcmmd12cefb1 t122;
                        GM_ST(uint8_t, (uint8_t *)&t122 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t122 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t122 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t122 + 16, 0);
                        GM_ST(uint64_t, (uint8_t *)&t122 + 24, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t122 + 0), ((uint8_t)4ULL));
                        GM_ST(void *, ((uint8_t *)&t122 + 8), t121);
                        gm_tftb1odc739t5 t123 = gm_f_push_d6c76ud_2(t108, &t122);
                        gm_tftb1odc739t5 t124 = t123;
                        if (((((uint8_t *)&t124))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t124) + 1, gm_src4nj79mj9jk57, 450, 9);
                        gm_b0_1 t125 = GM_LD(gm_b0_1, (((uint8_t *)&t124) + 1));
                    }
                    goto me53;
mn54:;
                    gm_trap("no match arm matched the scrutinee", gm_src4nj79mj9jk57, 436, 5);
me53:;
                }
lc41:;
                {
                    uint64_t t127 = gm_l11;
                    uint64_t r128; bool o129 = __builtin_add_overflow(t127, ((uint64_t)1ULL), &r128);
                    uint64_t t130 = r128;
                    gm_l11 = t130;
                }
            }
gm_ll45:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb41; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc41; }
            }
            goto gm_lad;
lb41:;
        }
    }
    {
        void * t131 = gm_l5;
        gm_t88rcucg1b7uk t132 = gm_f_unit_2age34u((void *)NULL, cx, NULL, t131);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tgabaikr08mjg t133;
        memset((uint8_t *)&t133 + 36, 0, 4);
        gm_b16_8 t134 = GM_LD(gm_b16_8, (((uint8_t *)&F.l9) + 0));
        GM_ST(gm_b16_8, (uint8_t *)&t133 + 0, t134);
        GM_ST(void *, (uint8_t *)&t133 + 16, ((void *)0));
        gm_tl874t2atnlh3 t135 = gm_l4;
        GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t133 + 24, t135);
        gm_tq7s98ihonkju t136 = gm_f_new_dsbahke((void *)NULL, cx, NULL, &t133, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_k6tu981lmau)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tq7s98ihonkju t137 = t136;
        if (((((uint8_t *)&t137))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t137) + 8, gm_src4nj79mj9jk57, 452, 38);
        void * t138 = GM_LD(void *, (((uint8_t *)&t137) + 8));
        gm_teti411eujvf4 t139;
        memset(&t139, 0, sizeof t139);
        GM_ST(uint8_t, ((uint8_t *)&t139 + 0), ((uint8_t)46ULL));
        GM_ST(void *, ((uint8_t *)&t139 + 8), t138);
        gm_tl874t2atnlh3 t140 = gm_l4;
        void * t141 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t132, &t139, t140);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t142 = 0;
        F.ret = t141;
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
inline __attribute__((aligned(4))) void gm_f_soa_core_sd8eq62(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, const void * a2, gm_tl874t2atnlh3 a3) {
    struct gm_fr_soa_core_sd8eq62 F;
    void * gm_l0; /* s */
    gm_b16_8 gm_l1; /* name */
    gm_tjpqvfd72o62u gm_l2; /* info */
    gm_tl874t2atnlh3 gm_l3; /* span */
    uint32_t gm_l4; /* f */
    gm_t3mf58gv9kf3n gm_l5; /* layouts */
    gm_t88rcucg1b7uk gm_l6; /* k */
    gm_tnqk2qt7j42o9 gm_l8; /* <temporary array> */
    uint32_t gm_l9; /* inst */
    GM_STACK_CHECK(&F, 774u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l3 = a3;
    {
        void * t2 = gm_l0;
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_ss496b65r9omj));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(5));
        gm_b16_8 t4 = gm_l1;
        gm_t9805qtonbsag t5 = gm_f_core_fn_of_jr7r6ef((void *)NULL, cx, NULL, t2, t3, t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t9805qtonbsag t6 = t5;
        uint32_t t7;
        if (((((uint8_t *)&t6))[4] != 0)) {
            uint32_t t8 = GM_LD(uint32_t, (((uint8_t *)&t6) + 0));
            t7 = t8;
        } else {
            {
                void * t10 = gm_l0;
                gm_tl874t2atnlh3 t11 = gm_l3;
                gm_b16_8 t12;
                GM_ST(void *, (uint8_t *)&t12, (void *)(gm_sfflovsm8rlpo));
                GM_ST(uintptr_t, (uint8_t *)&t12 + 8, (uintptr_t)(50));
                gm_b0_1 t13 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t10, t11, t12);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_tn6p6lpnve7b0 t14;
            memset(&t14, 0, sizeof t14);
            uint8_t t15 = 0;
            F.ret = t14;
            goto gm_fret;
        }
        gm_l4 = t7;
    }
    {
        void * t16 = gm_l0;
        gm_tl874t2atnlh3 t17 = gm_l3;
        gm_t3mf58gv9kf3n t18 = gm_f_soa_layout_n4ibc7b((void *)NULL, cx, NULL, t16, &(*(gm_tjpqvfd72o62u *)((uint8_t *)a2)), t17);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l5 = t18;
    }
    {
        void * t19 = gm_l0;
        void * t20 = (void *)(((uint8_t *)t19) + 8);
        void * t21 = gm_l0;
        void * t22 = gm_l0;
        void * t23 = (void *)(((uint8_t *)t22) + 8);
        gm_t88rcucg1b7uk t24 = gm_f_usize_ty_qnrd30f((void *)NULL, cx, NULL, t23);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t25 = gm_l0;
        gm_b16_8 t26 = gm_f_soa_fields_dg06fgc(t25, &(*(gm_tjpqvfd72o62u *)((uint8_t *)a2)));
        gm_b16_8 t27 = t26;
        uint64_t t28 = GM_LD(uintptr_t, ((uint8_t *)&t27) + 8);
        uint64_t t29 = (uint64_t)t28;
        gm_tbci5ospk5nnn t30;
        gm_f_from_u64_ud13q5v((void *)&t30, (void *)NULL, cx, NULL, t29);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t428ue2u3hf5g t31;
        gm_f_int_key_dmcraup((void *)&t31, (void *)NULL, cx, NULL, t21, t24, &t30);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t53at80pfdus3 t32;
        GM_ST(uint8_t, (uint8_t *)&t32 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t32 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t32 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t32 + 40, 0);
        GM_ST(uint64_t, (uint8_t *)&t32 + 48, 0);
        GM_ST(uint8_t, ((uint8_t *)&t32 + 0), ((uint8_t)21ULL));
        GM_ST(gm_t428ue2u3hf5g, ((uint8_t *)&t32 + 8), t31);
        gm_t88rcucg1b7uk t33 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t20, &t32);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l6 = t33;
    }
    {
        void * t34 = gm_l0;
        gm_trqfvmjtt4sij t35 = GM_LD(gm_trqfvmjtt4sij, (((uint8_t *)t34) + 2456));
        F.l7 = t35;
    }
    {
        void * t36 = gm_l0;
        uint8_t *t37 = (((uint8_t *)t36) + 2456);
        gm_f_list_epvducg((void *)&(*(gm_trqfvmjtt4sij *)t37), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
    }
    {
        void * t38 = gm_l0;
        uint32_t t39 = gm_l4;
        {
            gm_t88rcucg1b7uk t41 = gm_l6;
            gm_tnqk2qt7j42o9 t42;
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t42 + 0), t41);
            gm_l8 = t42;
        }
        void * t43 = ((uint8_t *)&gm_l8);
        if ((uintptr_t)0 > (uintptr_t)1 || (uintptr_t)1 > (uintptr_t)1) gm_trap_slice((uintptr_t)0, (uintptr_t)1, 1, gm_src4nj79mj9jk57, 350, 30);
        gm_b16_8 t44;
        GM_ST(void *, (uint8_t *)&t44, (void *)(((uint8_t *)t43 + (uintptr_t)0 * 4)));
        GM_ST(uintptr_t, (uint8_t *)&t44 + 8, (uintptr_t)((uintptr_t)1 - (uintptr_t)0));
        gm_b16_8 t45 = t44;
        gm_tl874t2atnlh3 t46 = gm_l3;
        uint32_t t47 = gm_f_instantiate_35r5pb2((void *)NULL, cx, NULL, t38, t39, t45, t46);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l9 = t47;
    }
    {
        void * t48 = gm_l0;
        gm_trqfvmjtt4sij t49 = F.l7;
        GM_ST(gm_trqfvmjtt4sij, (((uint8_t *)t48) + 2456), t49);
    }
    {
        gm_t017ud7s0np4m t50;
        memset((uint8_t *)&t50 + 4, 0, 4);
        uint32_t t51 = gm_l9;
        GM_ST(uint32_t, (uint8_t *)&t50 + 0, t51);
        void * t52 = GM_LD(void *, (((uint8_t *)&gm_l5) + 0));
        GM_ST(void *, (uint8_t *)&t50 + 8, t52);
        void * t53 = GM_LD(void *, (((uint8_t *)&gm_l5) + 8));
        GM_ST(void *, (uint8_t *)&t50 + 16, t53);
        gm_tn6p6lpnve7b0 t54;
        GM_ST(uint8_t, (uint8_t *)&t54 + 25, 0);
        GM_ST(uint16_t, (uint8_t *)&t54 + 26, 0);
        GM_ST(uint32_t, (uint8_t *)&t54 + 28, 0);
        GM_ST(gm_t017ud7s0np4m, ((uint8_t *)&t54), t50);
        ((uint8_t *)&t54)[24] = 1;
        uint8_t t55 = 0;
        F.ret = t54;
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
__attribute__((aligned(4))) void * gm_f_soa_expand_2e8rmqf(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2) {
    struct gm_fr_soa_expand_2e8rmqf F;
    void * gm_l0; /* s */
    void * gm_l1; /* locals */
    void * gm_l2; /* e */
    gm_tl874t2atnlh3 gm_l3; /* span */
    gm_tandvbt63uk7n gm_l4; /* x */
    gm_tesaltf0mhcki gm_l5; /* op */
    gm_b16_8 gm_l6; /* args */
    void * gm_l7; /* err */
    gm_t88rcucg1b7uk gm_l8; /* operand */
    gm_t88rcucg1b7uk gm_l9; /* ok */
    gm_tjpqvfd72o62u gm_l10; /* info */
    gm_b16_8 gm_l11; /* stmts */
    gm_toh0ootodhjue gm_l12; /* b */
    gm_tjpqvfd72o62u gm_l13; /* to */
    uint64_t gm_l14; /* next */
    void * gm_l15; /* lo */
    void * gm_l16; /* hi */
    GM_STACK_CHECK(&F, 1929u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l2;
        gm_tl874t2atnlh3 t3 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t2) + 72));
        gm_l3 = t3;
    }
    {
        void * t5 = gm_l2;
        uint8_t t6 = GM_LD(uint8_t, ((((uint8_t *)t5) + 8) + 0));
        if (t6 != ((uint8_t)57ULL)) goto ie4;
        gm_tandvbt63uk7n t7 = GM_LD(gm_tandvbt63uk7n, ((((uint8_t *)t5) + 8) + 8));
        gm_l4 = t7;
        {
            gm_tesaltf0mhcki t9 = GM_LD(gm_tesaltf0mhcki, (((uint8_t *)&gm_l4) + 0));
            gm_l5 = t9;
        }
        {
            gm_b16_8 t10 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l4) + 8));
            gm_l6 = t10;
        }
        {
            void * t11 = gm_l0;
            void * t12 = (void *)(((uint8_t *)t11) + 8);
            gm_t88rcucg1b7uk t13 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t12);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_b16_8 t14 = gm_f_empty_slice_g3tddrb_2();
            gm_teti411eujvf4 t15;
            memset(&t15, 0, sizeof t15);
            GM_ST(uint8_t, ((uint8_t *)&t15 + 0), ((uint8_t)35ULL));
            GM_ST(gm_b16_8, ((uint8_t *)&t15 + 8), t14);
            gm_tl874t2atnlh3 t16 = gm_l3;
            void * t17 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t13, &t15, t16);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l7 = t17;
        }
        {
            void * t18 = gm_l0;
            void * t19 = (void *)(((uint8_t *)t18) + 8);
            gm_t88rcucg1b7uk t20 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t19);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l8 = t20;
        }
        {
            uint8_t t22 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l5) + 0) == ((uint8_t)5ULL)));
            if (!t22) goto ie21;
            {
                void * t25 = gm_l0;
                void * t26 = (void *)(((uint8_t *)t25) + 8);
                void * t27 = gm_l2;
                gm_t88rcucg1b7uk t28 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t27) + 0));
                gm_t53at80pfdus3 t29;
                gm_f_kind_jcnt4hm((void *)&t29, t26, GM_CONV(gm_csguankko4lmhd, t28));
                gm_t53at80pfdus3 t30 = t29;
                uint8_t t31 = GM_LD(uint8_t, (((uint8_t *)&t30) + 0));
                if (t31 != ((uint8_t)14ULL)) goto ie24;
                gm_t88rcucg1b7uk t32 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t30) + 8) + 4));
                gm_l9 = t32;
                {
                    gm_t88rcucg1b7uk t34 = gm_l9;
                    gm_l8 = t34;
                }
                goto ix24;
ie24:;
ix24:;
            }
            goto ix21;
ie21:;
            {
                uint64_t t36 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t37 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                if (t36 >= (uintptr_t)(t37)) gm_trap_index(t36, t37, gm_src4nj79mj9jk57, 468, 19);
                gm_t88rcucg1b7uk t38 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t36 * 88) + 0));
                gm_l8 = t38;
            }
ix21:;
        }
        {
            void * t39 = gm_l0;
            gm_t88rcucg1b7uk t40 = gm_l8;
            gm_t8cc1mtbrhlpt t41;
            gm_f_soa_info_pq4q43l((void *)&t41, t39, GM_CONV(gm_csguankko4lmhd, t40));
            gm_t8cc1mtbrhlpt t42 = t41;
            gm_tjpqvfd72o62u t43;
            if (((((uint8_t *)&t42))[32] != 0)) {
                gm_tjpqvfd72o62u t44 = GM_LD(gm_tjpqvfd72o62u, (((uint8_t *)&t42) + 0));
                t43 = t44;
            } else {
                void * t45 = gm_l7;
                uint8_t t46 = 0;
                F.ret = t45;
                goto gm_fret;
            }
            gm_l10 = t43;
        }
        {
            void * t48 = gm_l0;
            uint8_t t49 = gm_f_soa_generic_kvk21mq(t48, &(*(gm_tjpqvfd72o62u *)((uint8_t *)&gm_l10)));
            if (!t49) goto ie47;
            {
                void * t51 = gm_l7;
                uint8_t t52 = 0;
                F.ret = t51;
                goto gm_fret;
            }
            goto ix47;
ie47:;
ix47:;
        }
        {
            uint8_t t63 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
            switch (t63) { case ((uint8_t)0ULL): goto ma54; case ((uint8_t)1ULL): goto ma55; case ((uint8_t)2ULL): goto ma56; case ((uint8_t)3ULL): goto ma57; case ((uint8_t)4ULL): goto ma58; case ((uint8_t)5ULL): goto ma59; case ((uint8_t)6ULL): goto ma60; case ((uint8_t)7ULL): goto ma61; default: goto ma62; }
ma54:;
            uint8_t t65 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
            if (t65 != ((uint8_t)0ULL)) goto mn64;
            void * t66 = gm_l0;
            uint64_t t67 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t68 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
            if (t67 >= (uintptr_t)(t68)) gm_trap_index(t67, t68, gm_src4nj79mj9jk57, 473, 41);
            void * t69 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t67 * 88);
            uint64_t t70 = (uintptr_t)((uint64_t)1ULL);
            uint64_t t71 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
            if (t70 >= (uintptr_t)(t71)) gm_trap_index(t70, t71, gm_src4nj79mj9jk57, 473, 57);
            void * t72 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t70 * 88);
            gm_tl874t2atnlh3 t73 = gm_l3;
            void * t74 = gm_f_soa_gather_of_4timqsa((void *)NULL, cx, NULL, t66, t69, &(*(gm_tjpqvfd72o62u *)((uint8_t *)&gm_l10)), t72, t73);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t75 = 0;
            F.ret = t74;
            goto gm_fret;
            goto me53;
mn64:;
ma55:;
            uint8_t t77 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
            if (t77 != ((uint8_t)1ULL)) goto mn76;
            {
                void * t79 = gm_l0;
                uint64_t t80 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t81 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                if (t80 >= (uintptr_t)(t81)) gm_trap_index(t80, t81, gm_src4nj79mj9jk57, 475, 37);
                void * t82 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t80 * 88);
                uint64_t t83 = (uintptr_t)((uint64_t)1ULL);
                uint64_t t84 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                if (t83 >= (uintptr_t)(t84)) gm_trap_index(t83, t84, gm_src4nj79mj9jk57, 475, 53);
                void * t85 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t83 * 88);
                uint64_t t86 = (uintptr_t)((uint64_t)2ULL);
                uint64_t t87 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                if (t86 >= (uintptr_t)(t87)) gm_trap_index(t86, t87, gm_src4nj79mj9jk57, 475, 63);
                void * t88 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t86 * 88);
                gm_tl874t2atnlh3 t89 = gm_l3;
                gm_b16_8 t90 = gm_f_soa_scatter_h5ruqu8((void *)NULL, cx, NULL, t79, t82, &(*(gm_tjpqvfd72o62u *)((uint8_t *)&gm_l10)), t85, t88, t89);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l11 = t90;
            }
            {
                void * t91 = gm_l0;
                void * t92 = (void *)(((uint8_t *)t91) + 8);
                gm_t88rcucg1b7uk t93 = gm_f_unit_2age34u((void *)NULL, cx, NULL, t92);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tgabaikr08mjg t94;
                memset((uint8_t *)&t94 + 36, 0, 4);
                gm_b16_8 t95 = gm_l11;
                GM_ST(gm_b16_8, (uint8_t *)&t94 + 0, t95);
                GM_ST(void *, (uint8_t *)&t94 + 16, ((void *)0));
                gm_tl874t2atnlh3 t96 = gm_l3;
                GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t94 + 24, t96);
                gm_tq7s98ihonkju t97 = gm_f_new_dsbahke((void *)NULL, cx, NULL, &t94, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_k2ok1rai48t)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tq7s98ihonkju t98 = t97;
                if (((((uint8_t *)&t98))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t98) + 8, gm_src4nj79mj9jk57, 476, 48);
                void * t99 = GM_LD(void *, (((uint8_t *)&t98) + 8));
                gm_teti411eujvf4 t100;
                memset(&t100, 0, sizeof t100);
                GM_ST(uint8_t, ((uint8_t *)&t100 + 0), ((uint8_t)46ULL));
                GM_ST(void *, ((uint8_t *)&t100 + 8), t99);
                gm_tl874t2atnlh3 t101 = gm_l3;
                void * t102 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t93, &t100, t101);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t103 = 0;
                F.ret = t102;
                goto gm_fret;
            }
            goto me53;
mn76:;
ma56:;
            uint8_t t105 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
            if (t105 != ((uint8_t)2ULL)) goto mn104;
            void * t106 = gm_l0;
            uint64_t t107 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t108 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
            if (t107 >= (uintptr_t)(t108)) gm_trap_index(t107, t108, gm_src4nj79mj9jk57, 478, 38);
            void * t109 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t107 * 88);
            gm_tl874t2atnlh3 t110 = gm_l3;
            void * t111 = gm_f_soa_len_of_4l5vk85((void *)NULL, cx, NULL, t106, t109, &(*(gm_tjpqvfd72o62u *)((uint8_t *)&gm_l10)), t110);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t112 = 0;
            F.ret = t111;
            goto gm_fret;
            goto me53;
mn104:;
ma57:;
            uint8_t t114 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
            if (t114 != ((uint8_t)3ULL)) goto mn113;
            gm_toh0ootodhjue t115 = GM_LD(gm_toh0ootodhjue, (((uint8_t *)&gm_l5) + 1));
            gm_l12 = t115;
            {
                void * t117 = gm_l0;
                void * t118 = gm_l2;
                gm_t88rcucg1b7uk t119 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t118) + 0));
                gm_t8cc1mtbrhlpt t120;
                gm_f_soa_info_pq4q43l((void *)&t120, t117, GM_CONV(gm_csguankko4lmhd, t119));
                gm_t8cc1mtbrhlpt t121 = t120;
                gm_tjpqvfd72o62u t122;
                if (((((uint8_t *)&t121))[32] != 0)) {
                    gm_tjpqvfd72o62u t123 = GM_LD(gm_tjpqvfd72o62u, (((uint8_t *)&t121) + 0));
                    t122 = t123;
                } else {
                    void * t124 = gm_l7;
                    uint8_t t125 = 0;
                    F.ret = t124;
                    goto gm_fret;
                }
                gm_l13 = t122;
            }
            {
                void * t127 = gm_l0;
                uint8_t t128 = gm_f_soa_generic_kvk21mq(t127, &(*(gm_tjpqvfd72o62u *)((uint8_t *)&gm_l13)));
                if (!t128) goto ie126;
                {
                    void * t130 = gm_l7;
                    uint8_t t131 = 0;
                    F.ret = t130;
                    goto gm_fret;
                }
                goto ix126;
ie126:;
ix126:;
            }
            {
                gm_l14 = ((uint64_t)1ULL);
            }
            {
                gm_l15 = ((void *)0);
            }
            {
                gm_l16 = ((void *)0);
            }
            {
                uint8_t t133 = GM_LD(uint8_t, (((uint8_t *)&gm_l12) + 0));
                if (!t133) goto ie132;
                {
                    uint64_t t135 = gm_l14;
                    uint64_t t136 = (uintptr_t)t135;
                    uint64_t t137 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                    if (t136 >= (uintptr_t)(t137)) gm_trap_index(t136, t137, gm_src4nj79mj9jk57, 486, 23);
                    void * t138 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t136 * 88);
                    gm_l15 = t138;
                }
                {
                    uint64_t t139 = gm_l14;
                    uint64_t r140; bool o141 = __builtin_add_overflow(t139, ((uint64_t)1ULL), &r140);
                    if (o141) gm_trap("integer overflow in `+`", gm_src4nj79mj9jk57, 487, 17);
                    uint64_t t142 = r140;
                    gm_l14 = t142;
                }
                goto ix132;
ie132:;
ix132:;
            }
            {
                uint8_t t144 = GM_LD(uint8_t, (((uint8_t *)&gm_l12) + 1));
                if (!t144) goto ie143;
                {
                    uint64_t t146 = gm_l14;
                    uint64_t t147 = (uintptr_t)t146;
                    uint64_t t148 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                    if (t147 >= (uintptr_t)(t148)) gm_trap_index(t147, t148, gm_src4nj79mj9jk57, 489, 29);
                    void * t149 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t147 * 88);
                    gm_l16 = t149;
                }
                goto ix143;
ie143:;
ix143:;
            }
            {
                void * t150 = gm_l0;
                uint64_t t151 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t152 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                if (t151 >= (uintptr_t)(t152)) gm_trap_index(t151, t152, gm_src4nj79mj9jk57, 490, 41);
                void * t153 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t151 * 88);
                uint8_t t154 = GM_LD(uint8_t, (((uint8_t *)&gm_l13) + 24));
                void * t155 = gm_l15;
                void * t156 = gm_l16;
                uint8_t t157 = GM_LD(uint8_t, (((uint8_t *)&gm_l12) + 2));
                gm_tl874t2atnlh3 t158 = gm_l3;
                void * t159 = gm_f_soa_slice_columns_kqq8dfr((void *)NULL, cx, NULL, t150, t153, &(*(gm_tjpqvfd72o62u *)((uint8_t *)&gm_l10)), t154, t155, t156, t157, t158);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t160 = 0;
                F.ret = t159;
                goto gm_fret;
            }
            goto me53;
mn113:;
ma58:;
            uint8_t t162 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
            if (t162 != ((uint8_t)4ULL)) goto mn161;
            void * t163 = gm_l0;
            uint64_t t164 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t165 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
            if (t164 >= (uintptr_t)(t165)) gm_trap_index(t164, t165, gm_src4nj79mj9jk57, 492, 44);
            void * t166 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t164 * 88);
            uint64_t t167 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t168 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
            if (t167 >= (uintptr_t)(t168)) gm_trap_index(t167, t168, gm_src4nj79mj9jk57, 492, 53);
            gm_t88rcucg1b7uk t169 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t167 * 88) + 0));
            void * t170 = gm_l2;
            gm_t88rcucg1b7uk t171 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t170) + 0));
            gm_tl874t2atnlh3 t172 = gm_l3;
            void * t173 = gm_f_soa_to_const_hhiu49k((void *)NULL, cx, NULL, t163, t166, t169, t171, t172);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t174 = 0;
            F.ret = t173;
            goto gm_fret;
            goto me53;
mn161:;
ma59:;
            uint8_t t176 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
            if (t176 != ((uint8_t)5ULL)) goto mn175;
            void * t177 = gm_l0;
            void * t178 = gm_l1;
            gm_t88rcucg1b7uk t179 = gm_l8;
            uint64_t t180 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t181 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
            if (t180 >= (uintptr_t)(t181)) gm_trap_index(t180, t181, gm_src4nj79mj9jk57, 493, 68);
            void * t182 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t180 * 88);
            uint64_t t183 = (uintptr_t)((uint64_t)1ULL);
            uint64_t t184 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
            if (t183 >= (uintptr_t)(t184)) gm_trap_index(t183, t184, gm_src4nj79mj9jk57, 493, 78);
            void * t185 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t183 * 88);
            gm_tl874t2atnlh3 t186 = gm_l3;
            void * t187 = gm_f_soa_make_columns_ardr0ua((void *)NULL, cx, NULL, t177, t178, t179, &(*(gm_tjpqvfd72o62u *)((uint8_t *)&gm_l10)), t182, t185, t186);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t188 = 0;
            F.ret = t187;
            goto gm_fret;
            goto me53;
mn175:;
ma60:;
            uint8_t t190 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
            if (t190 != ((uint8_t)6ULL)) goto mn189;
            void * t191 = gm_l0;
            uint64_t t192 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t193 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
            if (t192 >= (uintptr_t)(t193)) gm_trap_index(t192, t193, gm_src4nj79mj9jk57, 494, 45);
            void * t194 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t192 * 88);
            uint64_t t195 = (uintptr_t)((uint64_t)1ULL);
            uint64_t t196 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
            if (t195 >= (uintptr_t)(t196)) gm_trap_index(t195, t196, gm_src4nj79mj9jk57, 494, 61);
            void * t197 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t195 * 88);
            gm_tl874t2atnlh3 t198 = gm_l3;
            void * t199 = gm_f_soa_free_columns_n1tvn1j((void *)NULL, cx, NULL, t191, t194, &(*(gm_tjpqvfd72o62u *)((uint8_t *)&gm_l10)), t197, t198);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t200 = 0;
            F.ret = t199;
            goto gm_fret;
            goto me53;
mn189:;
ma61:;
            uint8_t t202 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
            if (t202 != ((uint8_t)7ULL)) goto mn201;
            void * t203 = gm_l0;
            uint64_t t204 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t205 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
            if (t204 >= (uintptr_t)(t205)) gm_trap_index(t204, t205, gm_src4nj79mj9jk57, 495, 49);
            void * t206 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t204 * 88);
            uint64_t t207 = (uintptr_t)((uint64_t)1ULL);
            uint64_t t208 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
            if (t207 >= (uintptr_t)(t208)) gm_trap_index(t207, t208, gm_src4nj79mj9jk57, 495, 65);
            void * t209 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t207 * 88);
            gm_tl874t2atnlh3 t210 = gm_l3;
            void * t211 = gm_f_soa_copy_columns_s4nhe5h((void *)NULL, cx, NULL, t203, t206, &(*(gm_tjpqvfd72o62u *)((uint8_t *)&gm_l10)), t209, t210);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t212 = 0;
            F.ret = t211;
            goto gm_fret;
            goto me53;
mn201:;
ma62:;
            gm_trap("no match arm matched the scrutinee", gm_src4nj79mj9jk57, 472, 5);
me53:;
        }
        goto ix4;
ie4:;
        {
            void * t214 = gm_l2;
            uint8_t t215 = 0;
            F.ret = t214;
            goto gm_fret;
        }
ix4:;
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
extern __attribute__((aligned(4))) gm_b16_8 gm_f_soa_fields_dg06fgc(void * a0, const void * a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_soa_fields_dg06fgc_c(void * a0, gm_tjpqvfd72o62u a1) { return gm_f_soa_fields_dg06fgc(a0, &a1); }
__attribute__((aligned(4))) void * gm_f_soa_free_columns_n1tvn1j(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, const void * a2, void * a3, gm_tl874t2atnlh3 a4) {
    struct gm_fr_soa_free_columns_n1tvn1j F;
    void * gm_l0; /* s */
    void * gm_l1; /* v */
    gm_tjpqvfd72o62u gm_l2; /* info */
    void * gm_l3; /* site */
    gm_tl874t2atnlh3 gm_l4; /* span */
    void * gm_l5; /* tys */
    gm_t017ud7s0np4m gm_l6; /* c */
    gm_t88rcucg1b7uk gm_l7; /* first */
    void * gm_l8; /* base */
    void * gm_l9; /* raw */
    void * gm_l10; /* len */
    GM_STACK_CHECK(&F, 1254u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l3 = a3;
    gm_l4 = a4;
    {
        void * t2 = gm_l0;
        void * t3 = (void *)(((uint8_t *)t2) + 8);
        gm_l5 = t3;
    }
    {
        void * t4 = gm_l0;
        gm_b16_8 t5;
        GM_ST(void *, (uint8_t *)&t5, (void *)(gm_sg7n1u5l63uaa));
        GM_ST(uintptr_t, (uint8_t *)&t5 + 8, (uintptr_t)(8));
        gm_tl874t2atnlh3 t6 = gm_l4;
        gm_tn6p6lpnve7b0 t7;
        gm_f_soa_core_sd8eq62((void *)&t7, (void *)NULL, cx, NULL, t4, t5, &(*(gm_tjpqvfd72o62u *)((uint8_t *)a2)), t6);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tn6p6lpnve7b0 t8 = t7;
        gm_t017ud7s0np4m t9;
        if (((((uint8_t *)&t8))[24] != 0)) {
            gm_t017ud7s0np4m t10 = GM_LD(gm_t017ud7s0np4m, (((uint8_t *)&t8) + 0));
            t9 = t10;
        } else {
            void * t11 = gm_l5;
            gm_t88rcucg1b7uk t12 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t11);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_b16_8 t13 = gm_f_empty_slice_g3tddrb_2();
            gm_teti411eujvf4 t14;
            memset(&t14, 0, sizeof t14);
            GM_ST(uint8_t, ((uint8_t *)&t14 + 0), ((uint8_t)35ULL));
            GM_ST(gm_b16_8, ((uint8_t *)&t14 + 8), t13);
            gm_tl874t2atnlh3 t15 = gm_l4;
            void * t16 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t12, &t14, t15);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t17 = 0;
            F.ret = t16;
            goto gm_fret;
        }
        gm_l6 = t9;
    }
    {
        uint64_t t18 = (uintptr_t)((uint64_t)0ULL);
        void * t19 = gm_l0;
        void * t20 = gm_l1;
        gm_t88rcucg1b7uk t21 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t20) + 0));
        gm_th2lqbo77pvsc t22;
        gm_f_struct_fields_chqu3it((void *)&t22, t19, GM_CONV(gm_csguankko4lmhd, t21));
        gm_th2lqbo77pvsc t23 = t22;
        gm_b16_8 t24;
        if (((((uint8_t *)&t23))[16] != 0)) {
            gm_b16_8 t25 = GM_LD(gm_b16_8, (((uint8_t *)&t23) + 0));
            t24 = t25;
        } else {
            gm_b16_8 t26 = gm_f_empty_slice_ose1ba9();
            t24 = t26;
        }
        gm_b16_8 t27 = t24;
        uint64_t t28 = GM_LD(uintptr_t, ((uint8_t *)&t27) + 8);
        if (t18 >= (uintptr_t)(t28)) gm_trap_index(t18, t28, gm_src4nj79mj9jk57, 412, 14);
        gm_t88rcucg1b7uk t29 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, ((uint8_t *)&t27)) + t18 * 40) + 16));
        gm_l7 = t29;
    }
    {
        gm_t88rcucg1b7uk t30 = gm_l7;
        void * t31 = gm_l1;
        void * t32 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t31);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tbl8il92l97ut t33;
        GM_ST(uint32_t, (uint8_t *)&t33 + 12, 0);
        GM_ST(void *, ((uint8_t *)&t33 + 0), t32);
        GM_ST(uint32_t, ((uint8_t *)&t33 + 8), ((uint32_t)0ULL));
        gm_teti411eujvf4 t34;
        memset(&t34, 0, sizeof t34);
        GM_ST(uint8_t, ((uint8_t *)&t34 + 0), ((uint8_t)19ULL));
        GM_ST(gm_tbl8il92l97ut, ((uint8_t *)&t34 + 8), t33);
        gm_tl874t2atnlh3 t35 = gm_l4;
        void * t36 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t30, &t34, t35);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l8 = t36;
    }
    {
        void * t37 = gm_l5;
        gm_t53at80pfdus3 t38;
        memset(&t38, 0, sizeof t38);
        GM_ST(uint8_t, ((uint8_t *)&t38 + 0), ((uint8_t)7ULL));
        gm_t88rcucg1b7uk t39 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t37, &t38);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tmn7n0n9raou5 t40;
        GM_ST(uint8_t, (uint8_t *)&t40 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t40 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t40 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t40 + 0), ((uint8_t)6ULL));
        void * t41 = gm_l8;
        gm_t8jsc4n9vp2ib t42;
        GM_ST(gm_tmn7n0n9raou5, ((uint8_t *)&t42 + 0), t40);
        GM_ST(void *, ((uint8_t *)&t42 + 8), t41);
        gm_teti411eujvf4 t43;
        memset(&t43, 0, sizeof t43);
        GM_ST(uint8_t, ((uint8_t *)&t43 + 0), ((uint8_t)32ULL));
        GM_ST(gm_t8jsc4n9vp2ib, ((uint8_t *)&t43 + 8), t42);
        gm_tl874t2atnlh3 t44 = gm_l4;
        void * t45 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t39, &t43, t44);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l9 = t45;
    }
    {
        void * t46 = gm_l0;
        void * t47 = gm_l1;
        gm_tl874t2atnlh3 t48 = gm_l4;
        void * t49 = gm_f_soa_len_of_4l5vk85((void *)NULL, cx, NULL, t46, t47, &(*(gm_tjpqvfd72o62u *)((uint8_t *)a2)), t48);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l10 = t49;
    }
    {
        gm_f_list_vp1djv1_2((void *)&F.l11, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t50 = (void *)((uint8_t *)&F.l11);
        void * t51 = GM_LD(void *, (((uint8_t *)&gm_l6) + 8));
        gm_tftb1odc739t5 t52 = gm_f_push_d943vgp_2(t50, &(*(gm_t69m56meqefs8 *)((uint8_t *)t51)));
        gm_tftb1odc739t5 t53 = t52;
        if (((((uint8_t *)&t53))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t53) + 1, gm_src4nj79mj9jk57, 417, 5);
        gm_b0_1 t54 = GM_LD(gm_b0_1, (((uint8_t *)&t53) + 1));
    }
    {
        void * t55 = (void *)((uint8_t *)&F.l11);
        void * t56 = GM_LD(void *, (((uint8_t *)&gm_l6) + 16));
        gm_tftb1odc739t5 t57 = gm_f_push_d943vgp_2(t55, &(*(gm_t69m56meqefs8 *)((uint8_t *)t56)));
        gm_tftb1odc739t5 t58 = t57;
        if (((((uint8_t *)&t58))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t58) + 1, gm_src4nj79mj9jk57, 418, 5);
        gm_b0_1 t59 = GM_LD(gm_b0_1, (((uint8_t *)&t58) + 1));
    }
    {
        void * t60 = (void *)((uint8_t *)&F.l11);
        void * t61 = gm_l10;
        gm_tftb1odc739t5 t62 = gm_f_push_d943vgp_2(t60, &(*(gm_t69m56meqefs8 *)((uint8_t *)t61)));
        gm_tftb1odc739t5 t63 = t62;
        if (((((uint8_t *)&t63))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t63) + 1, gm_src4nj79mj9jk57, 419, 5);
        gm_b0_1 t64 = GM_LD(gm_b0_1, (((uint8_t *)&t63) + 1));
    }
    {
        void * t65 = (void *)((uint8_t *)&F.l11);
        void * t66 = gm_l9;
        gm_tftb1odc739t5 t67 = gm_f_push_d943vgp_2(t65, &(*(gm_t69m56meqefs8 *)((uint8_t *)t66)));
        gm_tftb1odc739t5 t68 = t67;
        if (((((uint8_t *)&t68))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t68) + 1, gm_src4nj79mj9jk57, 420, 5);
        gm_b0_1 t69 = GM_LD(gm_b0_1, (((uint8_t *)&t68) + 1));
    }
    {
        void * t70 = (void *)((uint8_t *)&F.l11);
        void * t71 = gm_l3;
        gm_tftb1odc739t5 t72 = gm_f_push_d943vgp_2(t70, &(*(gm_t69m56meqefs8 *)((uint8_t *)t71)));
        gm_tftb1odc739t5 t73 = t72;
        if (((((uint8_t *)&t73))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t73) + 1, gm_src4nj79mj9jk57, 421, 5);
        gm_b0_1 t74 = GM_LD(gm_b0_1, (((uint8_t *)&t73) + 1));
    }
    {
        uint32_t t75 = GM_LD(uint32_t, (((uint8_t *)&gm_l6) + 0));
        uint64_t t76 = (uint64_t)t75;
        uint64_t t77 = (uintptr_t)t76;
        void * t78 = gm_l0;
        uint64_t t79 = GM_LD(uintptr_t, ((((uint8_t *)t78) + 344) + 0) + 8);
        if (t77 >= (uintptr_t)(t79)) gm_trap_index(t77, t79, gm_src4nj79mj9jk57, 422, 19);
        gm_t88rcucg1b7uk t80 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t78) + 344) + 0)) + t77 * 136) + 56));
        gm_to76jli3bqddq t81;
        uint32_t t82 = GM_LD(uint32_t, (((uint8_t *)&gm_l6) + 0));
        gm_tsth9qu1dv0g3 t83;
        memset(&t83, 0, sizeof t83);
        GM_ST(uint8_t, ((uint8_t *)&t83 + 0), ((uint8_t)0ULL));
        GM_ST(uint32_t, ((uint8_t *)&t83 + 8), t82);
        GM_ST(gm_tsth9qu1dv0g3, (uint8_t *)&t81 + 0, t83);
        gm_b16_8 t84 = GM_LD(gm_b16_8, (((uint8_t *)&F.l11) + 0));
        GM_ST(gm_b16_8, (uint8_t *)&t81 + 40, t84);
        gm_teti411eujvf4 t85;
        GM_ST(uint8_t, (uint8_t *)&t85 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t85 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t85 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t85 + 0), ((uint8_t)34ULL));
        GM_ST(gm_to76jli3bqddq, ((uint8_t *)&t85 + 8), t81);
        gm_tl874t2atnlh3 t86 = gm_l4;
        void * t87 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t80, &t85, t86);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t88 = 0;
        F.ret = t87;
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
inline __attribute__((aligned(4))) void * gm_f_soa_gather_of_4timqsa(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, const void * a2, void * a3, gm_tl874t2atnlh3 a4) {
    struct gm_fr_soa_gather_of_4timqsa F;
    void * gm_l0; /* s */
    void * gm_l1; /* v */
    gm_tjpqvfd72o62u gm_l2; /* info */
    void * gm_l3; /* i */
    gm_tl874t2atnlh3 gm_l4; /* span */
    gm_b16_8 gm_l5; /* fields */
    gm_b16_8 gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    uint64_t gm_l9; /* k */
    void * gm_l11; /* col */
    GM_STACK_CHECK(&F, 686u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l3 = a3;
    gm_l4 = a4;
    {
        void * t2 = gm_l0;
        gm_b16_8 t3 = gm_f_soa_fields_dg06fgc(t2, &(*(gm_tjpqvfd72o62u *)((uint8_t *)a2)));
        gm_l5 = t3;
    }
    {
        gm_f_list_vp1djv1_2((void *)&F.l6, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t5 = gm_l5;
            gm_l7 = t5;
        }
        {
            gm_l8 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t7 = gm_l8;
                uint64_t t8 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                uint8_t t9 = (t7 < t8);
                if (!t9) goto lb6;
                {
                    gm_t8ta482bl6be0 t12;
                    uint64_t t13 = gm_l8;
                    GM_ST(uint64_t, (uint8_t *)&t12 + 0, t13);
                    uint64_t t14 = gm_l8;
                    uint64_t t15 = (uintptr_t)t14;
                    uint64_t t16 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                    memcpy((uint8_t *)&t12 + 8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l7)) + t15 * 40), 40);
                    gm_t8ta482bl6be0 t17 = t12;
                    uint64_t t20 = GM_LD(uint64_t, (((uint8_t *)&t17) + 0));
                    gm_l9 = t20;
                    gm_t0mglksrkfa5e t21 = GM_LD(gm_t0mglksrkfa5e, (((uint8_t *)&t17) + 8));
                    F.l10 = t21;
                    {
                        void * t23 = gm_l0;
                        void * t24 = gm_l1;
                        uint64_t t25 = gm_l9;
                        gm_tl874t2atnlh3 t26 = gm_l4;
                        void * t27 = gm_f_soa_column_7a5htvb((void *)NULL, cx, NULL, t23, t24, &(*(gm_tjpqvfd72o62u *)((uint8_t *)a2)), t25, t26);
                        if (gm_unw.kind) {
                            goto gm_ll10;
                        }
                        gm_l11 = t27;
                    }
                    {
                        void * t28 = (void *)((uint8_t *)&F.l6);
                        gm_t88rcucg1b7uk t29 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l10) + 16));
                        gm_tlcl4u558l6p5 t30;
                        memset((uint8_t *)&t30 + 17, 0, 7);
                        void * t31 = gm_l11;
                        GM_ST(void *, (uint8_t *)&t30 + 0, t31);
                        void * t32 = gm_l3;
                        void * t33 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t32);
                        if (gm_unw.kind) {
                            goto gm_ll10;
                        }
                        GM_ST(void *, (uint8_t *)&t30 + 8, t33);
                        GM_ST(uint8_t, (uint8_t *)&t30 + 16, ((uint8_t)1));
                        gm_teti411eujvf4 t34;
                        memset(&t34, 0, sizeof t34);
                        GM_ST(uint8_t, ((uint8_t *)&t34 + 0), ((uint8_t)20ULL));
                        GM_ST(gm_tlcl4u558l6p5, ((uint8_t *)&t34 + 8), t30);
                        gm_tl874t2atnlh3 t35 = gm_l4;
                        void * t36 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t29, &t34, t35);
                        if (gm_unw.kind) {
                            goto gm_ll10;
                        }
                        gm_tftb1odc739t5 t37 = gm_f_push_d943vgp_2(t28, &(*(gm_t69m56meqefs8 *)((uint8_t *)t36)));
                        gm_tftb1odc739t5 t38 = t37;
                        if (((((uint8_t *)&t38))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t38) + 1, gm_src4nj79mj9jk57, 280, 9);
                        gm_b0_1 t39 = GM_LD(gm_b0_1, (((uint8_t *)&t38) + 1));
                    }
                    goto me18;
mn19:;
                    gm_trap("no match arm matched the scrutinee", gm_src4nj79mj9jk57, 278, 5);
me18:;
                }
lc6:;
                {
                    uint64_t t41 = gm_l8;
                    uint64_t r42; bool o43 = __builtin_add_overflow(t41, ((uint64_t)1ULL), &r42);
                    uint64_t t44 = r42;
                    gm_l8 = t44;
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
        gm_t88rcucg1b7uk t45 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)a2) + 0));
        gm_b16_8 t46 = GM_LD(gm_b16_8, (((uint8_t *)&F.l6) + 0));
        gm_teti411eujvf4 t47;
        memset(&t47, 0, sizeof t47);
        GM_ST(uint8_t, ((uint8_t *)&t47 + 0), ((uint8_t)35ULL));
        GM_ST(gm_b16_8, ((uint8_t *)&t47 + 8), t46);
        gm_tl874t2atnlh3 t48 = gm_l4;
        void * t49 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t45, &t47, t48);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t50 = 0;
        F.ret = t49;
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
extern __attribute__((aligned(4))) uint8_t gm_f_soa_generic_kvk21mq(void * a0, const void * a1);
__attribute__((aligned(4))) uint8_t gm_f_soa_generic_kvk21mq_c(void * a0, gm_tjpqvfd72o62u a1) { return gm_f_soa_generic_kvk21mq(a0, &a1); }
inline __attribute__((aligned(4))) void gm_f_soa_info_pq4q43l(void *gm_out, void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_soa_info_pq4q43l F;
    void * gm_l0; /* s */
    gm_t88rcucg1b7uk gm_l1; /* t */
    uint32_t gm_l2; /* id */
    gm_b16_8 gm_l3; /* args */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    gm_tj4mbomtvccf0 gm_l6; /* tm */
    gm_t88rcucg1b7uk gm_l7; /* elem */
    GM_STACK_CHECK(&F, 593u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    {
        void * t3 = gm_l0;
        void * t4 = (void *)(((uint8_t *)t3) + 8);
        gm_t88rcucg1b7uk t5 = gm_l1;
        gm_t53at80pfdus3 t6;
        gm_f_kind_jcnt4hm((void *)&t6, t4, GM_CONV(gm_csguankko4lmhd, t5));
        gm_t53at80pfdus3 t7 = t6;
        uint8_t t8 = GM_LD(uint8_t, (((uint8_t *)&t7) + 0));
        if (t8 != ((uint8_t)17ULL)) goto ie2;
        uint32_t t9 = GM_LD(uint32_t, ((((uint8_t *)&t7) + 8) + 0));
        gm_l2 = t9;
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((((uint8_t *)&t7) + 8) + 8));
        gm_l3 = t10;
        {
            {
                void * t13 = gm_l0;
                gm_b16_8 t14 = GM_LD(gm_b16_8, ((((uint8_t *)t13) + 2416) + 0));
                gm_l4 = t14;
            }
            {
                gm_l5 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t16 = gm_l5;
                    uint64_t t17 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                    uint8_t t18 = (t16 < t17);
                    if (!t18) goto lb15;
                    {
                        uint64_t t21 = gm_l5;
                        uint64_t t22 = (uintptr_t)t21;
                        uint64_t t23 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                        gm_tj4mbomtvccf0 t26 = GM_LD(gm_tj4mbomtvccf0, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t22 * 32));
                        gm_l6 = t26;
                        {
                            uint32_t t29 = GM_LD(uint32_t, (((uint8_t *)&gm_l6) + 0));
                            uint32_t t30 = gm_l2;
                            uint8_t t31 = ((t29 == t30));
                            if (!t31) goto ie28;
                            {
                                uint64_t t34 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                                uint8_t t35 = (t34 > ((uint64_t)0ULL));
                                if (!t35) goto ie33;
                                uint64_t t36 = (uintptr_t)((uint64_t)0ULL);
                                uint64_t t37 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                                if (t36 >= (uintptr_t)(t37)) gm_trap_index(t36, t37, gm_src4nj79mj9jk57, 74, 48);
                                uint8_t t38 = GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t36 * 40) + 0));
                                if (t38 != ((uint8_t)0ULL)) goto ie33;
                                gm_t88rcucg1b7uk t39 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t36 * 40) + 8));
                                gm_l7 = t39;
                                {
                                    gm_tjpqvfd72o62u t41;
                                    memset((uint8_t *)&t41 + 4, 0, 4);
                                    memset((uint8_t *)&t41 + 25, 0, 7);
                                    gm_t88rcucg1b7uk t42 = gm_l7;
                                    GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t41 + 0, t42);
                                    gm_tf5srh4mp4b3i t43 = GM_LD(gm_tf5srh4mp4b3i, (((uint8_t *)&gm_l6) + 8));
                                    GM_ST(gm_tf5srh4mp4b3i, (uint8_t *)&t41 + 8, t43);
                                    uint8_t t44 = GM_LD(uint8_t, (((uint8_t *)&gm_l6) + 24));
                                    GM_ST(uint8_t, (uint8_t *)&t41 + 24, t44);
                                    gm_t8cc1mtbrhlpt t45;
                                    GM_ST(uint8_t, (uint8_t *)&t45 + 33, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t45 + 34, 0);
                                    GM_ST(uint32_t, (uint8_t *)&t45 + 36, 0);
                                    GM_ST(gm_tjpqvfd72o62u, ((uint8_t *)&t45), t41);
                                    ((uint8_t *)&t45)[32] = 1;
                                    uint8_t t46 = 0;
                                    F.ret = t45;
                                    goto gm_fret;
                                }
                                goto ix33;
ie33:;
ix33:;
                            }
                            {
                                gm_t8cc1mtbrhlpt t47;
                                memset(&t47, 0, sizeof t47);
                                uint8_t t48 = 0;
                                F.ret = t47;
                                goto gm_fret;
                            }
                            goto ix28;
ie28:;
ix28:;
                        }
                        goto me24;
mn25:;
                        gm_trap("no match arm matched the scrutinee", gm_src4nj79mj9jk57, 72, 5);
me24:;
                    }
lc15:;
                    {
                        uint64_t t50 = gm_l5;
                        uint64_t r51; bool o52 = __builtin_add_overflow(t50, ((uint64_t)1ULL), &r51);
                        uint64_t t53 = r51;
                        gm_l5 = t53;
                    }
                }
lb15:;
            }
        }
        {
            void * t54 = gm_l0;
            void * t55 = (void *)(((uint8_t *)t54) + 2320);
            uint32_t t56 = gm_l2;
            gm_f_get_nt3g9ko((void *)&(*(gm_t8cc1mtbrhlpt *)gm_out), t55, t56);
            goto gm_fdone;
        }
        goto ix2;
ie2:;
        {
            gm_t8cc1mtbrhlpt t58;
            memset(&t58, 0, sizeof t58);
            uint8_t t59 = 0;
            F.ret = t58;
            goto gm_fret;
        }
ix2:;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_t8cc1mtbrhlpt gm_f_soa_info_pq4q43l_c(void * a0, gm_csguankko4lmhd a1) { gm_t8cc1mtbrhlpt gm_r; gm_f_soa_info_pq4q43l(&gm_r, a0, a1); return gm_r; }
inline __attribute__((aligned(4))) gm_t3mf58gv9kf3n gm_f_soa_layout_n4ibc7b(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_tl874t2atnlh3 a2) {
    struct gm_fr_soa_layout_n4ibc7b F;
    void * gm_l0; /* s */
    gm_tjpqvfd72o62u gm_l1; /* info */
    gm_tl874t2atnlh3 gm_l2; /* span */
    gm_b16_8 gm_l3; /* fields */
    gm_b16_8 gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    gm_tbccvikg6b96e gm_l9; /* l */
    uint64_t gm_l10; /* n */
    gm_t88rcucg1b7uk gm_l11; /* t */
    GM_STACK_CHECK(&F, 702u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        gm_b16_8 t3 = gm_f_soa_fields_dg06fgc(t2, &(*(gm_tjpqvfd72o62u *)((uint8_t *)a1)));
        gm_l3 = t3;
    }
    {
        gm_f_list_vp1djv1_2((void *)&F.l4, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_f_list_vp1djv1_2((void *)&F.l5, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t5 = gm_l3;
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
                    gm_t0mglksrkfa5e t17 = GM_LD(gm_t0mglksrkfa5e, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t13 * 40));
                    F.l8 = t17;
                    {
                        void * t19 = gm_l0;
                        gm_t88rcucg1b7uk t20 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l8) + 16));
                        gm_f_layout_a5ovnrm((void *)&gm_l9, (void *)NULL, cx, NULL, t19, t20);
                        if (gm_unw.kind) {
                            goto gm_ll10;
                        }
                    }
                    {
                        void * t21 = (void *)((uint8_t *)&F.l4);
                        void * t22 = gm_l0;
                        uint64_t t23 = GM_LD(uint64_t, (((uint8_t *)&gm_l9) + 0));
                        gm_tl874t2atnlh3 t24 = gm_l2;
                        void * t25 = gm_f_usize_lit_a8hojea((void *)NULL, cx, NULL, t22, t23, t24);
                        if (gm_unw.kind) {
                            goto gm_ll10;
                        }
                        gm_tftb1odc739t5 t26 = gm_f_push_d943vgp_2(t21, &(*(gm_t69m56meqefs8 *)((uint8_t *)t25)));
                        gm_tftb1odc739t5 t27 = t26;
                        if (((((uint8_t *)&t27))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t27) + 1, gm_src4nj79mj9jk57, 362, 9);
                        gm_b0_1 t28 = GM_LD(gm_b0_1, (((uint8_t *)&t27) + 1));
                    }
                    {
                        void * t29 = (void *)((uint8_t *)&F.l5);
                        void * t30 = gm_l0;
                        uint64_t t31 = GM_LD(uint64_t, (((uint8_t *)&gm_l9) + 8));
                        gm_tl874t2atnlh3 t32 = gm_l2;
                        void * t33 = gm_f_usize_lit_a8hojea((void *)NULL, cx, NULL, t30, t31, t32);
                        if (gm_unw.kind) {
                            goto gm_ll10;
                        }
                        gm_tftb1odc739t5 t34 = gm_f_push_d943vgp_2(t29, &(*(gm_t69m56meqefs8 *)((uint8_t *)t33)));
                        gm_tftb1odc739t5 t35 = t34;
                        if (((((uint8_t *)&t35))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t35) + 1, gm_src4nj79mj9jk57, 363, 9);
                        gm_b0_1 t36 = GM_LD(gm_b0_1, (((uint8_t *)&t35) + 1));
                    }
                    goto me15;
mn16:;
                    gm_trap("no match arm matched the scrutinee", gm_src4nj79mj9jk57, 360, 5);
me15:;
                }
lc6:;
                {
                    uint64_t t38 = gm_l7;
                    uint64_t r39; bool o40 = __builtin_add_overflow(t38, ((uint64_t)1ULL), &r39);
                    uint64_t t41 = r39;
                    gm_l7 = t41;
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
        uint64_t t42 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
        uint64_t t43 = (uint64_t)t42;
        gm_l10 = t43;
    }
    {
        void * t44 = gm_l0;
        void * t45 = (void *)(((uint8_t *)t44) + 8);
        uint64_t t46 = gm_l10;
        void * t47 = gm_l0;
        void * t48 = (void *)(((uint8_t *)t47) + 8);
        gm_t88rcucg1b7uk t49 = gm_f_usize_ty_qnrd30f((void *)NULL, cx, NULL, t48);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t88rcucg1b7uk t50 = gm_f_array_th8sdi3((void *)NULL, cx, NULL, t45, t46, t49);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l11 = t50;
    }
    {
        gm_t88rcucg1b7uk t51 = gm_l11;
        gm_b16_8 t52 = GM_LD(gm_b16_8, (((uint8_t *)&F.l4) + 0));
        gm_teti411eujvf4 t53;
        memset(&t53, 0, sizeof t53);
        GM_ST(uint8_t, ((uint8_t *)&t53 + 0), ((uint8_t)36ULL));
        GM_ST(gm_b16_8, ((uint8_t *)&t53 + 8), t52);
        gm_tl874t2atnlh3 t54 = gm_l2;
        void * t55 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t51, &t53, t54);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t88rcucg1b7uk t56 = gm_l11;
        gm_b16_8 t57 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 0));
        gm_teti411eujvf4 t58;
        memset(&t58, 0, sizeof t58);
        GM_ST(uint8_t, ((uint8_t *)&t58 + 0), ((uint8_t)36ULL));
        GM_ST(gm_b16_8, ((uint8_t *)&t58 + 8), t57);
        gm_tl874t2atnlh3 t59 = gm_l2;
        void * t60 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t56, &t58, t59);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t3mf58gv9kf3n t61;
        GM_ST(void *, ((uint8_t *)&t61 + 0), t55);
        GM_ST(void *, ((uint8_t *)&t61 + 8), t60);
        uint8_t t62 = 0;
        F.ret = t61;
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
inline __attribute__((aligned(4))) void * gm_f_soa_len_of_4l5vk85(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, const void * a2, gm_tl874t2atnlh3 a3) {
    struct gm_fr_soa_len_of_4l5vk85 F;
    void * gm_l0; /* s */
    void * gm_l1; /* v */
    gm_tjpqvfd72o62u gm_l2; /* info */
    gm_tl874t2atnlh3 gm_l3; /* span */
    uint64_t gm_l4; /* n */
    gm_b16_8 gm_l5; /* fields */
    uint64_t gm_l6; /* k */
    GM_STACK_CHECK(&F, 467u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l3 = a3;
    {
        if (!(((((uint8_t *)a2) + 8))[8] != 0)) goto ie2;
        uint64_t t3 = GM_LD(uint64_t, (((uint8_t *)a2) + 8));
        gm_l4 = t3;
        {
            void * t5 = gm_l0;
            uint64_t t6 = gm_l4;
            gm_tl874t2atnlh3 t7 = gm_l3;
            void * t8 = gm_f_usize_lit_a8hojea((void *)NULL, cx, NULL, t5, t6, t7);
            if (gm_unw.kind) {
                goto gm_lad;
            }
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
        void * t11 = gm_l1;
        gm_t88rcucg1b7uk t12 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t11) + 0));
        gm_th2lqbo77pvsc t13;
        gm_f_struct_fields_chqu3it((void *)&t13, t10, GM_CONV(gm_csguankko4lmhd, t12));
        gm_th2lqbo77pvsc t14 = t13;
        gm_b16_8 t15;
        if (((((uint8_t *)&t14))[16] != 0)) {
            gm_b16_8 t16 = GM_LD(gm_b16_8, (((uint8_t *)&t14) + 0));
            t15 = t16;
        } else {
            gm_b16_8 t17 = gm_f_empty_slice_ose1ba9();
            t15 = t17;
        }
        gm_l5 = t15;
    }
    {
        uint64_t t18;
        uint64_t t20 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
        uint8_t t21 = ((t20 == ((uint64_t)0ULL)));
        if (!t21) goto ie19;
        uint64_t t23 = ((uint64_t)0ULL);
        t18 = t23;
        goto ix19;
ie19:;
        uint64_t t25 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
        uint64_t r26; bool o27 = __builtin_sub_overflow(t25, ((uint64_t)1ULL), &r26);
        if (o27) gm_trap("integer overflow in `-`", gm_src4nj79mj9jk57, 270, 42);
        uint64_t t28 = r26;
        uint64_t t29 = t28;
        t18 = t29;
ix19:;
        gm_l6 = t18;
    }
    {
        void * t30 = gm_l0;
        void * t31 = (void *)(((uint8_t *)t30) + 8);
        gm_t88rcucg1b7uk t32 = gm_f_usize_ty_qnrd30f((void *)NULL, cx, NULL, t31);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t33 = gm_l1;
        void * t34 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t33);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint64_t t35 = gm_l6;
        uint32_t t36 = (uint32_t)t35;
        gm_tbl8il92l97ut t37;
        GM_ST(uint32_t, (uint8_t *)&t37 + 12, 0);
        GM_ST(void *, ((uint8_t *)&t37 + 0), t34);
        GM_ST(uint32_t, ((uint8_t *)&t37 + 8), t36);
        gm_teti411eujvf4 t38;
        memset(&t38, 0, sizeof t38);
        GM_ST(uint8_t, ((uint8_t *)&t38 + 0), ((uint8_t)19ULL));
        GM_ST(gm_tbl8il92l97ut, ((uint8_t *)&t38 + 8), t37);
        gm_tl874t2atnlh3 t39 = gm_l3;
        void * t40 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t32, &t38, t39);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t41 = 0;
        F.ret = t40;
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
__attribute__((aligned(4))) void * gm_f_soa_make_columns_ardr0ua(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, const void * a3, void * a4, void * a5, gm_tl874t2atnlh3 a6) {
    struct gm_fr_soa_make_columns_ardr0ua F;
    void * gm_l0; /* s */
    void * gm_l1; /* locals */
    gm_t88rcucg1b7uk gm_l2; /* t */
    gm_tjpqvfd72o62u gm_l3; /* info */
    void * gm_l4; /* n */
    void * gm_l5; /* site */
    gm_tl874t2atnlh3 gm_l6; /* span */
    void * gm_l7; /* tys */
    gm_t017ud7s0np4m gm_l8; /* c */
    gm_t88rcucg1b7uk gm_l9; /* rt */
    void * gm_l11; /* call */
    gm_t88rcucg1b7uk gm_l12; /* err */
    gm_t88rcucg1b7uk gm_l13; /* ok */
    uint32_t gm_l14; /* p */
    uint32_t gm_l15; /* e */
    gm_b16_8 gm_l16; /* fields */
    uint64_t gm_l18; /* <tmp> */
    uint64_t gm_l19; /* <tmp> */
    uint64_t gm_l20; /* k */
    void * gm_l21; /* raw */
    gm_t88rcucg1b7uk gm_l22; /* result */
    void * gm_l23; /* value */
    void * gm_l24; /* wrapped */
    void * gm_l25; /* failed */
    GM_STACK_CHECK(&F, 3049u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l4 = a4;
    gm_l5 = a5;
    gm_l6 = a6;
    {
        void * t2 = gm_l0;
        void * t3 = (void *)(((uint8_t *)t2) + 8);
        gm_l7 = t3;
    }
    {
        void * t4 = gm_l0;
        gm_b16_8 t5;
        GM_ST(void *, (uint8_t *)&t5, (void *)(gm_suh0878eve8t9));
        GM_ST(uintptr_t, (uint8_t *)&t5 + 8, (uintptr_t)(9));
        gm_tl874t2atnlh3 t6 = gm_l6;
        gm_tn6p6lpnve7b0 t7;
        gm_f_soa_core_sd8eq62((void *)&t7, (void *)NULL, cx, NULL, t4, t5, &(*(gm_tjpqvfd72o62u *)((uint8_t *)a3)), t6);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tn6p6lpnve7b0 t8 = t7;
        gm_t017ud7s0np4m t9;
        if (((((uint8_t *)&t8))[24] != 0)) {
            gm_t017ud7s0np4m t10 = GM_LD(gm_t017ud7s0np4m, (((uint8_t *)&t8) + 0));
            t9 = t10;
        } else {
            void * t11 = gm_l7;
            gm_t88rcucg1b7uk t12 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t11);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_b16_8 t13 = gm_f_empty_slice_g3tddrb_2();
            gm_teti411eujvf4 t14;
            memset(&t14, 0, sizeof t14);
            GM_ST(uint8_t, ((uint8_t *)&t14 + 0), ((uint8_t)35ULL));
            GM_ST(gm_b16_8, ((uint8_t *)&t14 + 8), t13);
            gm_tl874t2atnlh3 t15 = gm_l6;
            void * t16 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t12, &t14, t15);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t17 = 0;
            F.ret = t16;
            goto gm_fret;
        }
        gm_l8 = t9;
    }
    {
        uint32_t t18 = GM_LD(uint32_t, (((uint8_t *)&gm_l8) + 0));
        uint64_t t19 = (uint64_t)t18;
        uint64_t t20 = (uintptr_t)t19;
        void * t21 = gm_l0;
        uint64_t t22 = GM_LD(uintptr_t, ((((uint8_t *)t21) + 344) + 0) + 8);
        if (t20 >= (uintptr_t)(t22)) gm_trap_index(t20, t22, gm_src4nj79mj9jk57, 374, 11);
        gm_t88rcucg1b7uk t23 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t21) + 344) + 0)) + t20 * 136) + 56));
        gm_l9 = t23;
    }
    {
        gm_f_list_vp1djv1_2((void *)&F.l10, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t24 = (void *)((uint8_t *)&F.l10);
        void * t25 = GM_LD(void *, (((uint8_t *)&gm_l8) + 8));
        gm_tftb1odc739t5 t26 = gm_f_push_d943vgp_2(t24, &(*(gm_t69m56meqefs8 *)((uint8_t *)t25)));
        gm_tftb1odc739t5 t27 = t26;
        if (((((uint8_t *)&t27))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t27) + 1, gm_src4nj79mj9jk57, 376, 5);
        gm_b0_1 t28 = GM_LD(gm_b0_1, (((uint8_t *)&t27) + 1));
    }
    {
        void * t29 = (void *)((uint8_t *)&F.l10);
        void * t30 = GM_LD(void *, (((uint8_t *)&gm_l8) + 16));
        gm_tftb1odc739t5 t31 = gm_f_push_d943vgp_2(t29, &(*(gm_t69m56meqefs8 *)((uint8_t *)t30)));
        gm_tftb1odc739t5 t32 = t31;
        if (((((uint8_t *)&t32))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t32) + 1, gm_src4nj79mj9jk57, 377, 5);
        gm_b0_1 t33 = GM_LD(gm_b0_1, (((uint8_t *)&t32) + 1));
    }
    {
        void * t34 = (void *)((uint8_t *)&F.l10);
        void * t35 = gm_l4;
        void * t36 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t35);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tftb1odc739t5 t37 = gm_f_push_d943vgp_2(t34, &(*(gm_t69m56meqefs8 *)((uint8_t *)t36)));
        gm_tftb1odc739t5 t38 = t37;
        if (((((uint8_t *)&t38))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t38) + 1, gm_src4nj79mj9jk57, 378, 5);
        gm_b0_1 t39 = GM_LD(gm_b0_1, (((uint8_t *)&t38) + 1));
    }
    {
        void * t40 = (void *)((uint8_t *)&F.l10);
        void * t41 = gm_l5;
        gm_tftb1odc739t5 t42 = gm_f_push_d943vgp_2(t40, &(*(gm_t69m56meqefs8 *)((uint8_t *)t41)));
        gm_tftb1odc739t5 t43 = t42;
        if (((((uint8_t *)&t43))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t43) + 1, gm_src4nj79mj9jk57, 379, 5);
        gm_b0_1 t44 = GM_LD(gm_b0_1, (((uint8_t *)&t43) + 1));
    }
    {
        gm_t88rcucg1b7uk t45 = gm_l9;
        gm_to76jli3bqddq t46;
        uint32_t t47 = GM_LD(uint32_t, (((uint8_t *)&gm_l8) + 0));
        gm_tsth9qu1dv0g3 t48;
        memset(&t48, 0, sizeof t48);
        GM_ST(uint8_t, ((uint8_t *)&t48 + 0), ((uint8_t)0ULL));
        GM_ST(uint32_t, ((uint8_t *)&t48 + 8), t47);
        GM_ST(gm_tsth9qu1dv0g3, (uint8_t *)&t46 + 0, t48);
        gm_b16_8 t49 = GM_LD(gm_b16_8, (((uint8_t *)&F.l10) + 0));
        GM_ST(gm_b16_8, (uint8_t *)&t46 + 40, t49);
        gm_teti411eujvf4 t50;
        GM_ST(uint8_t, (uint8_t *)&t50 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t50 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t50 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t50 + 0), ((uint8_t)34ULL));
        GM_ST(gm_to76jli3bqddq, ((uint8_t *)&t50 + 8), t46);
        gm_tl874t2atnlh3 t51 = gm_l6;
        void * t52 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t45, &t50, t51);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l11 = t52;
    }
    {
        void * t54 = gm_l7;
        gm_t88rcucg1b7uk t55 = gm_l9;
        gm_t53at80pfdus3 t56;
        gm_f_kind_jcnt4hm((void *)&t56, t54, GM_CONV(gm_csguankko4lmhd, t55));
        gm_t53at80pfdus3 t57 = t56;
        uint8_t t58 = GM_LD(uint8_t, (((uint8_t *)&t57) + 0));
        if (t58 != ((uint8_t)14ULL)) goto ie53;
        gm_t88rcucg1b7uk t59 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t57) + 8) + 0));
        gm_l12 = t59;
        gm_t88rcucg1b7uk t60 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t57) + 8) + 4));
        gm_l13 = t60;
        {
            void * t62 = gm_l1;
            gm_t88rcucg1b7uk t63 = gm_l13;
            gm_tl874t2atnlh3 t64 = gm_l6;
            uint32_t t65 = gm_f_soa_temp_2pshn8g((void *)NULL, cx, NULL, t62, t63, t64);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l14 = t65;
        }
        {
            void * t66 = gm_l1;
            gm_t88rcucg1b7uk t67 = gm_l12;
            gm_tl874t2atnlh3 t68 = gm_l6;
            uint32_t t69 = gm_f_soa_temp_2pshn8g((void *)NULL, cx, NULL, t66, t67, t68);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l15 = t69;
        }
        {
            void * t70 = gm_l0;
            gm_t88rcucg1b7uk t71 = gm_l2;
            gm_th2lqbo77pvsc t72;
            gm_f_struct_fields_chqu3it((void *)&t72, t70, GM_CONV(gm_csguankko4lmhd, t71));
            gm_th2lqbo77pvsc t73 = t72;
            gm_b16_8 t74;
            if (((((uint8_t *)&t73))[16] != 0)) {
                gm_b16_8 t75 = GM_LD(gm_b16_8, (((uint8_t *)&t73) + 0));
                t74 = t75;
            } else {
                gm_b16_8 t76 = gm_f_empty_slice_ose1ba9();
                t74 = t76;
            }
            gm_l16 = t74;
        }
        {
            gm_f_list_vp1djv1_2((void *)&F.l17, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            {
                gm_l19 = ((uint64_t)0ULL);
            }
            {
                uint64_t t78 = GM_LD(uintptr_t, ((uint8_t *)&gm_l16) + 8);
                uint64_t r79; bool o80 = __builtin_sub_overflow(t78, ((uint64_t)1ULL), &r79);
                if (o80) gm_trap("integer overflow in `-`", gm_src4nj79mj9jk57, 387, 17);
                uint64_t t81 = r79;
                gm_l18 = t81;
            }
            {
                for (;;) {
                    uint64_t t83 = gm_l19;
                    uint64_t t84 = gm_l18;
                    uint8_t t85 = (t83 < t84);
                    if (!t85) goto lb82;
                    {
                        uint64_t t88 = gm_l19;
                        uint64_t t89 = t88;
                        uint64_t t92 = GM_LD(uint64_t, ((uint8_t *)&t89));
                        gm_l20 = t92;
                        {
                            void * t94 = gm_l7;
                            gm_t53at80pfdus3 t95;
                            memset(&t95, 0, sizeof t95);
                            GM_ST(uint8_t, ((uint8_t *)&t95 + 0), ((uint8_t)7ULL));
                            gm_t88rcucg1b7uk t96 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t94, &t95);
                            if (gm_unw.kind) {
                                goto gm_ll86;
                            }
                            gm_tlcl4u558l6p5 t97;
                            memset((uint8_t *)&t97 + 17, 0, 7);
                            gm_t88rcucg1b7uk t98 = gm_l13;
                            uint32_t t99 = gm_l14;
                            gm_teti411eujvf4 t100;
                            memset(&t100, 0, sizeof t100);
                            GM_ST(uint8_t, ((uint8_t *)&t100 + 0), ((uint8_t)15ULL));
                            GM_ST(uint32_t, ((uint8_t *)&t100 + 8), t99);
                            gm_tl874t2atnlh3 t101 = gm_l6;
                            void * t102 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t98, &t100, t101);
                            if (gm_unw.kind) {
                                goto gm_ll86;
                            }
                            GM_ST(void *, (uint8_t *)&t97 + 0, t102);
                            void * t103 = gm_l0;
                            uint64_t t104 = gm_l20;
                            uint64_t t105 = (uint64_t)t104;
                            gm_tl874t2atnlh3 t106 = gm_l6;
                            void * t107 = gm_f_usize_lit_a8hojea((void *)NULL, cx, NULL, t103, t105, t106);
                            if (gm_unw.kind) {
                                goto gm_ll86;
                            }
                            GM_ST(void *, (uint8_t *)&t97 + 8, t107);
                            GM_ST(uint8_t, (uint8_t *)&t97 + 16, ((uint8_t)0));
                            gm_teti411eujvf4 t108;
                            memset(&t108, 0, sizeof t108);
                            GM_ST(uint8_t, ((uint8_t *)&t108 + 0), ((uint8_t)20ULL));
                            GM_ST(gm_tlcl4u558l6p5, ((uint8_t *)&t108 + 8), t97);
                            gm_tl874t2atnlh3 t109 = gm_l6;
                            void * t110 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t96, &t108, t109);
                            if (gm_unw.kind) {
                                goto gm_ll86;
                            }
                            gm_l21 = t110;
                        }
                        {
                            void * t111 = (void *)((uint8_t *)&F.l17);
                            uint64_t t112 = gm_l20;
                            uint64_t t113 = (uintptr_t)t112;
                            uint64_t t114 = GM_LD(uintptr_t, ((uint8_t *)&gm_l16) + 8);
                            if (t113 >= (uintptr_t)(t114)) gm_trap_index(t113, t114, gm_src4nj79mj9jk57, 389, 27);
                            gm_t88rcucg1b7uk t115 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l16)) + t113 * 40) + 16));
                            gm_tt8o5kebou62r t116;
                            memset((uint8_t *)&t116 + 12, 0, 4);
                            gm_tvdg94bk7aa22 t117;
                            GM_ST(uint8_t, (uint8_t *)&t117 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t117 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t117 + 4, 0);
                            GM_ST(uint32_t, (uint8_t *)&t117 + 8, 0);
                            GM_ST(uint8_t, ((uint8_t *)&t117 + 0), ((uint8_t)8ULL));
                            GM_ST(gm_tvdg94bk7aa22, (uint8_t *)&t116 + 0, t117);
                            void * t118 = gm_l21;
                            gm_b16_8 t119 = gm_f_one_arg_f1gj3pp((void *)NULL, cx, NULL, t118);
                            if (gm_unw.kind) {
                                goto gm_ll86;
                            }
                            GM_ST(gm_b16_8, (uint8_t *)&t116 + 16, t119);
                            gm_teti411eujvf4 t120;
                            GM_ST(uint8_t, (uint8_t *)&t120 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t120 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t120 + 4, 0);
                            GM_ST(uint64_t, (uint8_t *)&t120 + 40, 0);
                            GM_ST(uint64_t, (uint8_t *)&t120 + 48, 0);
                            GM_ST(uint64_t, (uint8_t *)&t120 + 56, 0);
                            GM_ST(uint8_t, ((uint8_t *)&t120 + 0), ((uint8_t)33ULL));
                            GM_ST(gm_tt8o5kebou62r, ((uint8_t *)&t120 + 8), t116);
                            gm_tl874t2atnlh3 t121 = gm_l6;
                            void * t122 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t115, &t120, t121);
                            if (gm_unw.kind) {
                                goto gm_ll86;
                            }
                            gm_tftb1odc739t5 t123 = gm_f_push_d943vgp_2(t111, &(*(gm_t69m56meqefs8 *)((uint8_t *)t122)));
                            gm_tftb1odc739t5 t124 = t123;
                            if (((((uint8_t *)&t124))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t124) + 1, gm_src4nj79mj9jk57, 389, 9);
                            gm_b0_1 t125 = GM_LD(gm_b0_1, (((uint8_t *)&t124) + 1));
                        }
                        goto me90;
mn91:;
                        gm_trap("no match arm matched the scrutinee", gm_src4nj79mj9jk57, 387, 5);
me90:;
                    }
lc82:;
                    {
                        uint64_t t127 = gm_l19;
                        uint64_t r128; bool o129 = __builtin_add_overflow(t127, ((uint64_t)1ULL), &r128);
                        uint64_t t130 = r128;
                        gm_l19 = t130;
                    }
                }
gm_ll86:;
                if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb82; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc82; }
                }
                goto gm_lad;
lb82:;
            }
        }
        {
            void * t131 = (void *)((uint8_t *)&F.l17);
            void * t132 = gm_l4;
            gm_tftb1odc739t5 t133 = gm_f_push_d943vgp_2(t131, &(*(gm_t69m56meqefs8 *)((uint8_t *)t132)));
            gm_tftb1odc739t5 t134 = t133;
            if (((((uint8_t *)&t134))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t134) + 1, gm_src4nj79mj9jk57, 391, 5);
            gm_b0_1 t135 = GM_LD(gm_b0_1, (((uint8_t *)&t134) + 1));
        }
        {
            void * t136 = gm_l7;
            gm_t88rcucg1b7uk t137 = gm_l12;
            gm_t88rcucg1b7uk t138 = gm_l2;
            gm_t88rcucg1b7uk t139 = gm_f_result_o9ras10((void *)NULL, cx, NULL, t136, t137, t138);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l22 = t139;
        }
        {
            gm_t88rcucg1b7uk t140 = gm_l2;
            gm_b16_8 t141 = GM_LD(gm_b16_8, (((uint8_t *)&F.l17) + 0));
            gm_teti411eujvf4 t142;
            memset(&t142, 0, sizeof t142);
            GM_ST(uint8_t, ((uint8_t *)&t142 + 0), ((uint8_t)35ULL));
            GM_ST(gm_b16_8, ((uint8_t *)&t142 + 8), t141);
            gm_tl874t2atnlh3 t143 = gm_l6;
            void * t144 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t140, &t142, t143);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l23 = t144;
        }
        {
            gm_t88rcucg1b7uk t145 = gm_l22;
            gm_tmn7n0n9raou5 t146;
            GM_ST(uint8_t, (uint8_t *)&t146 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t146 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t146 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t146 + 0), ((uint8_t)3ULL));
            void * t147 = gm_l23;
            gm_t8jsc4n9vp2ib t148;
            GM_ST(gm_tmn7n0n9raou5, ((uint8_t *)&t148 + 0), t146);
            GM_ST(void *, ((uint8_t *)&t148 + 8), t147);
            gm_teti411eujvf4 t149;
            memset(&t149, 0, sizeof t149);
            GM_ST(uint8_t, ((uint8_t *)&t149 + 0), ((uint8_t)32ULL));
            GM_ST(gm_t8jsc4n9vp2ib, ((uint8_t *)&t149 + 8), t148);
            gm_tl874t2atnlh3 t150 = gm_l6;
            void * t151 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t145, &t149, t150);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l24 = t151;
        }
        {
            gm_t88rcucg1b7uk t152 = gm_l22;
            gm_t88rcucg1b7uk t153 = gm_l12;
            uint32_t t154 = gm_l15;
            gm_teti411eujvf4 t155;
            memset(&t155, 0, sizeof t155);
            GM_ST(uint8_t, ((uint8_t *)&t155 + 0), ((uint8_t)15ULL));
            GM_ST(uint32_t, ((uint8_t *)&t155 + 8), t154);
            gm_tl874t2atnlh3 t156 = gm_l6;
            void * t157 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t153, &t155, t156);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_teti411eujvf4 t158;
            memset(&t158, 0, sizeof t158);
            GM_ST(uint8_t, ((uint8_t *)&t158 + 0), ((uint8_t)39ULL));
            GM_ST(void *, ((uint8_t *)&t158 + 8), t157);
            gm_tl874t2atnlh3 t159 = gm_l6;
            void * t160 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t152, &t158, t159);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l25 = t160;
        }
        {
            gm_f_list_58nr42v_3((void *)&F.l26, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t161 = (void *)((uint8_t *)&F.l26);
            gm_tbbqefuhv2r8h t162;
            gm_t88rcucg1b7uk t163 = gm_l9;
            gm_t88rcucg1b7uk t164 = gm_l13;
            uint32_t t165 = gm_l14;
            gm_tb38v5s9j1r32 t166;
            memset(&t166, 0, sizeof t166);
            GM_ST(uint8_t, ((uint8_t *)&t166 + 0), ((uint8_t)1ULL));
            GM_ST(uint32_t, ((uint8_t *)&t166 + 8), t165);
            gm_tl874t2atnlh3 t167 = gm_l6;
            void * t168 = gm_f_mk_pat_hr2l6c4((void *)NULL, cx, NULL, t164, &t166, t167);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tb38v5s9j1r32 t169;
            memset(&t169, 0, sizeof t169);
            GM_ST(uint8_t, ((uint8_t *)&t169 + 0), ((uint8_t)6ULL));
            GM_ST(void *, ((uint8_t *)&t169 + 8), t168);
            gm_tl874t2atnlh3 t170 = gm_l6;
            void * t171 = gm_f_mk_pat_hr2l6c4((void *)NULL, cx, NULL, t163, &t169, t170);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(void *, (uint8_t *)&t162 + 0, t171);
            GM_ST(void *, (uint8_t *)&t162 + 8, ((void *)0));
            void * t172 = gm_l24;
            GM_ST(void *, (uint8_t *)&t162 + 16, t172);
            gm_tftb1odc739t5 t173 = gm_f_push_bpovr2k_3(t161, &t162);
            gm_tftb1odc739t5 t174 = t173;
            if (((((uint8_t *)&t174))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t174) + 1, gm_src4nj79mj9jk57, 397, 5);
            gm_b0_1 t175 = GM_LD(gm_b0_1, (((uint8_t *)&t174) + 1));
        }
        {
            void * t176 = (void *)((uint8_t *)&F.l26);
            gm_tbbqefuhv2r8h t177;
            gm_t88rcucg1b7uk t178 = gm_l9;
            gm_t88rcucg1b7uk t179 = gm_l12;
            uint32_t t180 = gm_l15;
            gm_tb38v5s9j1r32 t181;
            memset(&t181, 0, sizeof t181);
            GM_ST(uint8_t, ((uint8_t *)&t181 + 0), ((uint8_t)1ULL));
            GM_ST(uint32_t, ((uint8_t *)&t181 + 8), t180);
            gm_tl874t2atnlh3 t182 = gm_l6;
            void * t183 = gm_f_mk_pat_hr2l6c4((void *)NULL, cx, NULL, t179, &t181, t182);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tb38v5s9j1r32 t184;
            memset(&t184, 0, sizeof t184);
            GM_ST(uint8_t, ((uint8_t *)&t184 + 0), ((uint8_t)7ULL));
            GM_ST(void *, ((uint8_t *)&t184 + 8), t183);
            gm_tl874t2atnlh3 t185 = gm_l6;
            void * t186 = gm_f_mk_pat_hr2l6c4((void *)NULL, cx, NULL, t178, &t184, t185);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(void *, (uint8_t *)&t177 + 0, t186);
            GM_ST(void *, (uint8_t *)&t177 + 8, ((void *)0));
            void * t187 = gm_l25;
            GM_ST(void *, (uint8_t *)&t177 + 16, t187);
            gm_tftb1odc739t5 t188 = gm_f_push_bpovr2k_3(t176, &t177);
            gm_tftb1odc739t5 t189 = t188;
            if (((((uint8_t *)&t189))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t189) + 1, gm_src4nj79mj9jk57, 398, 5);
            gm_b0_1 t190 = GM_LD(gm_b0_1, (((uint8_t *)&t189) + 1));
        }
        {
            gm_t88rcucg1b7uk t191 = gm_l22;
            gm_tkaapcii86udk t192;
            void * t193 = gm_l11;
            GM_ST(void *, (uint8_t *)&t192 + 0, t193);
            gm_b16_8 t194 = GM_LD(gm_b16_8, (((uint8_t *)&F.l26) + 0));
            GM_ST(gm_b16_8, (uint8_t *)&t192 + 8, t194);
            gm_teti411eujvf4 t195;
            memset(&t195, 0, sizeof t195);
            GM_ST(uint8_t, ((uint8_t *)&t195 + 0), ((uint8_t)44ULL));
            GM_ST(gm_tkaapcii86udk, ((uint8_t *)&t195 + 8), t192);
            gm_tl874t2atnlh3 t196 = gm_l6;
            void * t197 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t191, &t195, t196);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t198 = 0;
            F.ret = t197;
            goto gm_fret;
        }
        goto ix53;
ie53:;
        {
            void * t200 = gm_l7;
            gm_t88rcucg1b7uk t201 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t200);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_b16_8 t202 = gm_f_empty_slice_g3tddrb_2();
            gm_teti411eujvf4 t203;
            memset(&t203, 0, sizeof t203);
            GM_ST(uint8_t, ((uint8_t *)&t203 + 0), ((uint8_t)35ULL));
            GM_ST(gm_b16_8, ((uint8_t *)&t203 + 8), t202);
            gm_tl874t2atnlh3 t204 = gm_l6;
            void * t205 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t201, &t203, t204);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t206 = 0;
            F.ret = t205;
            goto gm_fret;
        }
ix53:;
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
inline __attribute__((aligned(4))) uint8_t gm_f_soa_repeatable_rqn3pcn(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_soa_repeatable_rqn3pcn F;
    void * gm_l0; /* e */
    GM_STACK_CHECK_SHALLOW(&F, 24u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    uint8_t t2;
    uint8_t t10 = GM_LD(uint8_t, ((((uint8_t *)t1) + 8) + 0));
    switch (t10) { case ((uint8_t)0ULL): goto ma8; case ((uint8_t)1ULL): goto ma8; case ((uint8_t)2ULL): goto ma8; case ((uint8_t)3ULL): goto ma8; case ((uint8_t)4ULL): goto ma8; case ((uint8_t)5ULL): goto ma8; case ((uint8_t)6ULL): goto ma8; case ((uint8_t)7ULL): goto ma8; case ((uint8_t)8ULL): goto ma8; case ((uint8_t)9ULL): goto ma8; case ((uint8_t)10ULL): goto ma8; case ((uint8_t)11ULL): goto ma8; case ((uint8_t)12ULL): goto ma8; case ((uint8_t)13ULL): goto ma8; case ((uint8_t)14ULL): goto ma6; case ((uint8_t)15ULL): goto ma4; case ((uint8_t)16ULL): goto ma5; case ((uint8_t)17ULL): goto ma7; case ((uint8_t)18ULL): goto ma8; case ((uint8_t)19ULL): goto ma8; case ((uint8_t)20ULL): goto ma8; case ((uint8_t)21ULL): goto ma8; case ((uint8_t)22ULL): goto ma8; case ((uint8_t)23ULL): goto ma8; case ((uint8_t)24ULL): goto ma8; case ((uint8_t)25ULL): goto ma8; case ((uint8_t)26ULL): goto ma8; case ((uint8_t)27ULL): goto ma8; case ((uint8_t)28ULL): goto ma8; case ((uint8_t)29ULL): goto ma8; case ((uint8_t)30ULL): goto ma8; case ((uint8_t)31ULL): goto ma8; case ((uint8_t)32ULL): goto ma8; case ((uint8_t)33ULL): goto ma8; case ((uint8_t)34ULL): goto ma8; case ((uint8_t)35ULL): goto ma8; case ((uint8_t)36ULL): goto ma8; case ((uint8_t)37ULL): goto ma8; case ((uint8_t)38ULL): goto ma8; case ((uint8_t)39ULL): goto ma8; case ((uint8_t)40ULL): goto ma8; case ((uint8_t)41ULL): goto ma8; case ((uint8_t)42ULL): goto ma8; case ((uint8_t)43ULL): goto ma8; case ((uint8_t)44ULL): goto ma8; case ((uint8_t)45ULL): goto ma8; case ((uint8_t)46ULL): goto ma8; case ((uint8_t)47ULL): goto ma8; case ((uint8_t)48ULL): goto ma8; case ((uint8_t)49ULL): goto ma8; case ((uint8_t)50ULL): goto ma8; case ((uint8_t)51ULL): goto ma8; case ((uint8_t)52ULL): goto ma8; case ((uint8_t)53ULL): goto ma8; case ((uint8_t)54ULL): goto ma8; case ((uint8_t)55ULL): goto ma8; case ((uint8_t)56ULL): goto ma8; case ((uint8_t)57ULL): goto ma8; case ((uint8_t)58ULL): goto ma8; default: goto ma8; }
ma4:;
    uint8_t t12 = GM_LD(uint8_t, ((((uint8_t *)t1) + 8) + 0));
    if (t12 != ((uint8_t)15ULL)) goto mn11;
    t2 = ((uint8_t)1);
    goto me3;
mn11:;
ma5:;
    uint8_t t14 = GM_LD(uint8_t, ((((uint8_t *)t1) + 8) + 0));
    if (t14 != ((uint8_t)16ULL)) goto mn13;
    t2 = ((uint8_t)1);
    goto me3;
mn13:;
ma6:;
    uint8_t t16 = GM_LD(uint8_t, ((((uint8_t *)t1) + 8) + 0));
    if (t16 != ((uint8_t)14ULL)) goto mn15;
    t2 = ((uint8_t)1);
    goto me3;
mn15:;
ma7:;
    uint8_t t18 = GM_LD(uint8_t, ((((uint8_t *)t1) + 8) + 0));
    if (t18 != ((uint8_t)17ULL)) goto mn17;
    t2 = ((uint8_t)1);
    goto me3;
mn17:;
ma8:;
    t2 = ((uint8_t)0);
    goto me3;
mn19:;
ma9:;
    gm_trap("no match arm matched the scrutinee", gm_src4nj79mj9jk57, 250, 53);
me3:;
    F.ret = t2;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_soa_scatter_h5ruqu8(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, const void * a2, void * a3, void * a4, gm_tl874t2atnlh3 a5) {
    struct gm_fr_soa_scatter_h5ruqu8 F;
    void * gm_l0; /* s */
    void * gm_l1; /* v */
    gm_tjpqvfd72o62u gm_l2; /* info */
    void * gm_l3; /* i */
    void * gm_l4; /* x */
    gm_tl874t2atnlh3 gm_l5; /* span */
    gm_b16_8 gm_l6; /* fields */
    gm_b16_8 gm_l8; /* <tmp> */
    uint64_t gm_l9; /* <tmp> */
    uint64_t gm_l10; /* k */
    void * gm_l12; /* col */
    void * gm_l13; /* place */
    void * gm_l14; /* part */
    GM_STACK_CHECK(&F, 834u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_l5 = a5;
    {
        void * t2 = gm_l0;
        gm_b16_8 t3 = gm_f_soa_fields_dg06fgc(t2, &(*(gm_tjpqvfd72o62u *)((uint8_t *)a2)));
        gm_l6 = t3;
    }
    {
        gm_f_list_ir0h3tc_2((void *)&F.l7, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t5 = gm_l6;
            gm_l8 = t5;
        }
        {
            gm_l9 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t7 = gm_l9;
                uint64_t t8 = GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8);
                uint8_t t9 = (t7 < t8);
                if (!t9) goto lb6;
                {
                    gm_t8ta482bl6be0 t12;
                    uint64_t t13 = gm_l9;
                    GM_ST(uint64_t, (uint8_t *)&t12 + 0, t13);
                    uint64_t t14 = gm_l9;
                    uint64_t t15 = (uintptr_t)t14;
                    uint64_t t16 = GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8);
                    memcpy((uint8_t *)&t12 + 8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l8)) + t15 * 40), 40);
                    gm_t8ta482bl6be0 t17 = t12;
                    uint64_t t20 = GM_LD(uint64_t, (((uint8_t *)&t17) + 0));
                    gm_l10 = t20;
                    gm_t0mglksrkfa5e t21 = GM_LD(gm_t0mglksrkfa5e, (((uint8_t *)&t17) + 8));
                    F.l11 = t21;
                    {
                        void * t23 = gm_l0;
                        void * t24 = gm_l1;
                        uint64_t t25 = gm_l10;
                        gm_tl874t2atnlh3 t26 = gm_l5;
                        void * t27 = gm_f_soa_column_7a5htvb((void *)NULL, cx, NULL, t23, t24, &(*(gm_tjpqvfd72o62u *)((uint8_t *)a2)), t25, t26);
                        if (gm_unw.kind) {
                            goto gm_ll10;
                        }
                        gm_l12 = t27;
                    }
                    {
                        gm_t88rcucg1b7uk t28 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l11) + 16));
                        gm_tlcl4u558l6p5 t29;
                        memset((uint8_t *)&t29 + 17, 0, 7);
                        void * t30 = gm_l12;
                        GM_ST(void *, (uint8_t *)&t29 + 0, t30);
                        void * t31 = gm_l3;
                        void * t32 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t31);
                        if (gm_unw.kind) {
                            goto gm_ll10;
                        }
                        GM_ST(void *, (uint8_t *)&t29 + 8, t32);
                        GM_ST(uint8_t, (uint8_t *)&t29 + 16, ((uint8_t)1));
                        gm_teti411eujvf4 t33;
                        memset(&t33, 0, sizeof t33);
                        GM_ST(uint8_t, ((uint8_t *)&t33 + 0), ((uint8_t)20ULL));
                        GM_ST(gm_tlcl4u558l6p5, ((uint8_t *)&t33 + 8), t29);
                        gm_tl874t2atnlh3 t34 = gm_l5;
                        void * t35 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t28, &t33, t34);
                        if (gm_unw.kind) {
                            goto gm_ll10;
                        }
                        gm_l13 = t35;
                    }
                    {
                        gm_t88rcucg1b7uk t36 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l11) + 16));
                        void * t37 = gm_l4;
                        void * t38 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t37);
                        if (gm_unw.kind) {
                            goto gm_ll10;
                        }
                        uint64_t t39 = gm_l10;
                        uint32_t t40 = (uint32_t)t39;
                        gm_tbl8il92l97ut t41;
                        GM_ST(uint32_t, (uint8_t *)&t41 + 12, 0);
                        GM_ST(void *, ((uint8_t *)&t41 + 0), t38);
                        GM_ST(uint32_t, ((uint8_t *)&t41 + 8), t40);
                        gm_teti411eujvf4 t42;
                        memset(&t42, 0, sizeof t42);
                        GM_ST(uint8_t, ((uint8_t *)&t42 + 0), ((uint8_t)19ULL));
                        GM_ST(gm_tbl8il92l97ut, ((uint8_t *)&t42 + 8), t41);
                        gm_tl874t2atnlh3 t43 = gm_l5;
                        void * t44 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t36, &t42, t43);
                        if (gm_unw.kind) {
                            goto gm_ll10;
                        }
                        gm_l14 = t44;
                    }
                    {
                        void * t45 = (void *)((uint8_t *)&F.l7);
                        void * t46 = gm_l13;
                        void * t47 = gm_l14;
                        gm_t3mf58gv9kf3n t48;
                        GM_ST(void *, ((uint8_t *)&t48 + 0), t46);
                        GM_ST(void *, ((uint8_t *)&t48 + 8), t47);
                        gm_trcmmd12cefb1 t49;
                        GM_ST(uint8_t, (uint8_t *)&t49 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t49 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t49 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t49 + 24, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t49 + 0), ((uint8_t)2ULL));
                        GM_ST(gm_t3mf58gv9kf3n, ((uint8_t *)&t49 + 8), t48);
                        gm_tftb1odc739t5 t50 = gm_f_push_d6c76ud_2(t45, &t49);
                        gm_tftb1odc739t5 t51 = t50;
                        if (((((uint8_t *)&t51))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t51) + 1, gm_src4nj79mj9jk57, 293, 9);
                        gm_b0_1 t52 = GM_LD(gm_b0_1, (((uint8_t *)&t51) + 1));
                    }
                    goto me18;
mn19:;
                    gm_trap("no match arm matched the scrutinee", gm_src4nj79mj9jk57, 289, 5);
me18:;
                }
lc6:;
                {
                    uint64_t t54 = gm_l9;
                    uint64_t r55; bool o56 = __builtin_add_overflow(t54, ((uint64_t)1ULL), &r55);
                    uint64_t t57 = r55;
                    gm_l9 = t57;
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
        gm_b16_8 t58 = GM_LD(gm_b16_8, (((uint8_t *)&F.l7) + 0));
        uint8_t t59 = 0;
        F.ret = t58;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_soa_shape_755ppqp(void *env, gm_node *cx, gm_node *inst, gm_tf5srh4mp4b3i a0, uint8_t a1) {
    struct gm_fr_soa_shape_755ppqp F;
    gm_tf5srh4mp4b3i gm_l0; /* len */
    uint8_t gm_l1; /* is_const */
    uint64_t gm_l2; /* n */
    GM_STACK_CHECK(&F, 260u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        if (!((((uint8_t *)&gm_l0))[8] != 0)) goto ie2;
        uint64_t t3 = GM_LD(uint64_t, ((uint8_t *)&gm_l0));
        gm_l2 = t3;
        {
            uint64_t t5 = gm_l2;
            gm_t4ce0jd3tdro4 t6;
            GM_ST(uint64_t, ((uint8_t *)&t6 + 0), t5);
            gm_tabb710d9mq2e t7;
            gm_f_format_d04m6io((void *)&t7, (void *)NULL, cx, NULL, t6);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t8 = t7;
            if (((((uint8_t *)&t8))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t8) + 8, gm_src4nj79mj9jk57, 229, 26);
            gm_b16_8 t9 = GM_LD(gm_b16_8, (((uint8_t *)&t8) + 8));
            gm_b16_8 t10 = t9;
            gm_b16_8 t11 = GM_LD(gm_b16_8, ((uint8_t *)&t10));
            uint8_t t12 = 0;
            F.ret = t11;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        gm_b16_8 t13;
        uint8_t t15 = gm_l1;
        if (!t15) goto ie14;
        gm_b16_8 t17;
        GM_ST(void *, (uint8_t *)&t17, (void *)(gm_shsv7bl83q04d));
        GM_ST(uintptr_t, (uint8_t *)&t17 + 8, (uintptr_t)(8));
        gm_b16_8 t18 = t17;
        t13 = t18;
        goto ix14;
ie14:;
        gm_b16_8 t20;
        GM_ST(void *, (uint8_t *)&t20, (void *)(gm_s3ndsddjcvskd));
        GM_ST(uintptr_t, (uint8_t *)&t20 + 8, (uintptr_t)(2));
        gm_b16_8 t21 = t20;
        t13 = t21;
ix14:;
        uint8_t t22 = 0;
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
inline __attribute__((aligned(4))) void gm_f_soa_shape_of_ohenvjc(void *gm_out, void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_soa_shape_of_ohenvjc F;
    void * gm_l0; /* s */
    uint32_t gm_l1; /* id */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    gm_tj4mbomtvccf0 gm_l4; /* tm */
    GM_STACK_CHECK_SHALLOW(&F, 297u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        {
            void * t3 = gm_l0;
            gm_b16_8 t4 = GM_LD(gm_b16_8, ((((uint8_t *)t3) + 2416) + 0));
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
                    gm_tj4mbomtvccf0 t16 = GM_LD(gm_tj4mbomtvccf0, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t12 * 32));
                    gm_l4 = t16;
                    {
                        uint32_t t19 = GM_LD(uint32_t, (((uint8_t *)&gm_l4) + 0));
                        uint32_t t20 = gm_l1;
                        uint8_t t21 = ((t19 == t20));
                        if (!t21) goto ie18;
                        {
                            gm_th3eueiipivq6 t23;
                            memset((uint8_t *)&t23 + 17, 0, 7);
                            gm_tf5srh4mp4b3i t24 = GM_LD(gm_tf5srh4mp4b3i, (((uint8_t *)&gm_l4) + 8));
                            GM_ST(gm_tf5srh4mp4b3i, (uint8_t *)&t23 + 0, t24);
                            uint8_t t25 = GM_LD(uint8_t, (((uint8_t *)&gm_l4) + 24));
                            GM_ST(uint8_t, (uint8_t *)&t23 + 16, t25);
                            gm_tbm0n9ujhrlhf t26;
                            GM_ST(uint8_t, (uint8_t *)&t26 + 25, 0);
                            GM_ST(uint16_t, (uint8_t *)&t26 + 26, 0);
                            GM_ST(uint32_t, (uint8_t *)&t26 + 28, 0);
                            GM_ST(gm_th3eueiipivq6, ((uint8_t *)&t26), t23);
                            ((uint8_t *)&t26)[24] = 1;
                            uint8_t t27 = 0;
                            F.ret = t26;
                            goto gm_fret;
                        }
                        goto ix18;
ie18:;
ix18:;
                    }
                    goto me14;
mn15:;
                    gm_trap("no match arm matched the scrutinee", gm_src4nj79mj9jk57, 170, 5);
me14:;
                }
lc5:;
                {
                    uint64_t t29 = gm_l3;
                    uint64_t r30; bool o31 = __builtin_add_overflow(t29, ((uint64_t)1ULL), &r30);
                    uint64_t t32 = r30;
                    gm_l3 = t32;
                }
            }
lb5:;
        }
    }
    {
        gm_tbm0n9ujhrlhf t33;
        memset(&t33, 0, sizeof t33);
        uint8_t t34 = 0;
        F.ret = t33;
        goto gm_fret;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_tbm0n9ujhrlhf gm_f_soa_shape_of_ohenvjc_c(void * a0, uint32_t a1) { gm_tbm0n9ujhrlhf gm_r; gm_f_soa_shape_of_ohenvjc(&gm_r, a0, a1); return gm_r; }
inline __attribute__((aligned(4))) void * gm_f_soa_slice_column_6ogdk5f(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, const void * a2, uint64_t a3, uint8_t a4, void * a5, void * a6, uint8_t a7, gm_t88rcucg1b7uk a8, gm_tl874t2atnlh3 a9) {
    struct gm_fr_soa_slice_column_6ogdk5f F;
    void * gm_l0; /* s */
    void * gm_l1; /* v */
    gm_tjpqvfd72o62u gm_l2; /* info */
    uint64_t gm_l3; /* k */
    uint8_t gm_l4; /* is_const */
    void * gm_l5; /* lo */
    void * gm_l6; /* hi */
    uint8_t gm_l7; /* inclusive */
    gm_t88rcucg1b7uk gm_l8; /* ft */
    gm_tl874t2atnlh3 gm_l9; /* span */
    void * gm_l10; /* col */
    void * gm_l11; /* l */
    void * gm_l12; /* x */
    void * gm_l13; /* h */
    void * gm_l14; /* x */
    GM_STACK_CHECK(&F, 437u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_l5 = a5;
    gm_l6 = a6;
    gm_l7 = a7;
    gm_l8 = a8;
    gm_l9 = a9;
    {
        void * t2 = gm_l0;
        void * t3 = gm_l1;
        uint64_t t4 = gm_l3;
        gm_tl874t2atnlh3 t5 = gm_l9;
        void * t6 = gm_f_soa_column_7a5htvb((void *)NULL, cx, NULL, t2, t3, &(*(gm_tjpqvfd72o62u *)((uint8_t *)a2)), t4, t5);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l10 = t6;
    }
    {
        gm_l11 = ((void *)0);
    }
    {
        void * t8 = gm_l5;
        void * t9 = t8;
        if (!(GM_LD(void *, ((uint8_t *)&t9)) != 0)) goto ie7;
        void * t10 = GM_LD(void *, ((uint8_t *)&t9));
        gm_l12 = t10;
        {
            void * t12 = gm_l12;
            void * t13 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t12);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l11 = t13;
        }
        goto ix7;
ie7:;
ix7:;
    }
    {
        gm_l13 = ((void *)0);
    }
    {
        void * t15 = gm_l6;
        void * t16 = t15;
        if (!(GM_LD(void *, ((uint8_t *)&t16)) != 0)) goto ie14;
        void * t17 = GM_LD(void *, ((uint8_t *)&t16));
        gm_l14 = t17;
        {
            void * t19 = gm_l14;
            void * t20 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t19);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l13 = t20;
        }
        goto ix14;
ie14:;
ix14:;
    }
    {
        void * t21 = gm_l0;
        void * t22 = (void *)(((uint8_t *)t21) + 8);
        uint8_t t23 = gm_l4;
        gm_t88rcucg1b7uk t24 = gm_l8;
        gm_t88rcucg1b7uk t25 = gm_f_slice_5dt9121((void *)NULL, cx, NULL, t22, t23, t24);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tao63pk3r18v5 t26;
        memset((uint8_t *)&t26 + 25, 0, 7);
        void * t27 = gm_l10;
        GM_ST(void *, (uint8_t *)&t26 + 0, t27);
        void * t28 = gm_l11;
        GM_ST(void *, (uint8_t *)&t26 + 8, t28);
        void * t29 = gm_l13;
        GM_ST(void *, (uint8_t *)&t26 + 16, t29);
        uint8_t t30 = gm_l7;
        GM_ST(uint8_t, (uint8_t *)&t26 + 24, t30);
        gm_teti411eujvf4 t31;
        GM_ST(uint8_t, (uint8_t *)&t31 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t31 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t31 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t31 + 40, 0);
        GM_ST(uint64_t, (uint8_t *)&t31 + 48, 0);
        GM_ST(uint64_t, (uint8_t *)&t31 + 56, 0);
        GM_ST(uint8_t, ((uint8_t *)&t31 + 0), ((uint8_t)21ULL));
        GM_ST(gm_tao63pk3r18v5, ((uint8_t *)&t31 + 8), t26);
        gm_tl874t2atnlh3 t32 = gm_l9;
        void * t33 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t25, &t31, t32);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t34 = 0;
        F.ret = t33;
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
__attribute__((aligned(4))) void * gm_f_soa_slice_columns_kqq8dfr(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, const void * a2, uint8_t a3, void * a4, void * a5, uint8_t a6, gm_tl874t2atnlh3 a7) {
    struct gm_fr_soa_slice_columns_kqq8dfr F;
    void * gm_l0; /* s */
    void * gm_l1; /* v */
    gm_tjpqvfd72o62u gm_l2; /* info */
    uint8_t gm_l3; /* is_const */
    void * gm_l4; /* lo */
    void * gm_l5; /* hi */
    uint8_t gm_l6; /* inclusive */
    gm_tl874t2atnlh3 gm_l7; /* span */
    void * gm_l8; /* tys */
    gm_t88rcucg1b7uk gm_l9; /* t */
    gm_b16_8 gm_l10; /* fields */
    uint64_t gm_l12; /* <tmp> */
    uint64_t gm_l13; /* <tmp> */
    uint64_t gm_l14; /* k */
    void * gm_l15; /* sl */
    void * gm_l16; /* first */
    GM_STACK_CHECK(&F, 842u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_l5 = a5;
    gm_l6 = a6;
    gm_l7 = a7;
    {
        void * t2 = gm_l0;
        void * t3 = (void *)(((uint8_t *)t2) + 8);
        gm_l8 = t3;
    }
    {
        void * t4 = gm_l0;
        gm_t88rcucg1b7uk t5 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)a2) + 0));
        gm_tf5srh4mp4b3i t6;
        memset(&t6, 0, sizeof t6);
        uint8_t t7 = gm_l3;
        gm_tl874t2atnlh3 t8 = gm_l7;
        gm_t88rcucg1b7uk t9 = gm_f_soa_type_42lvsj0((void *)NULL, cx, NULL, t4, t5, t6, t7, t8);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l9 = t9;
    }
    {
        void * t10 = gm_l0;
        gm_b16_8 t11 = gm_f_soa_fields_dg06fgc(t10, &(*(gm_tjpqvfd72o62u *)((uint8_t *)a2)));
        gm_l10 = t11;
    }
    {
        gm_f_list_vp1djv1_2((void *)&F.l11, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_l13 = ((uint64_t)0ULL);
        }
        {
            uint64_t t13 = GM_LD(uintptr_t, ((uint8_t *)&gm_l10) + 8);
            gm_l12 = t13;
        }
        {
            for (;;) {
                uint64_t t15 = gm_l13;
                uint64_t t16 = gm_l12;
                uint8_t t17 = (t15 < t16);
                if (!t17) goto lb14;
                {
                    uint64_t t20 = gm_l13;
                    uint64_t t21 = t20;
                    uint64_t t24 = GM_LD(uint64_t, ((uint8_t *)&t21));
                    gm_l14 = t24;
                    {
                        void * t26 = gm_l0;
                        void * t27 = gm_l1;
                        uint64_t t28 = gm_l14;
                        uint8_t t29 = gm_l3;
                        void * t30 = gm_l4;
                        void * t31 = gm_l5;
                        uint8_t t32 = gm_l6;
                        uint64_t t33 = gm_l14;
                        uint64_t t34 = (uintptr_t)t33;
                        uint64_t t35 = GM_LD(uintptr_t, ((uint8_t *)&gm_l10) + 8);
                        if (t34 >= (uintptr_t)(t35)) gm_trap_index(t34, t35, gm_src4nj79mj9jk57, 305, 75);
                        gm_t88rcucg1b7uk t36 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l10)) + t34 * 40) + 16));
                        gm_tl874t2atnlh3 t37 = gm_l7;
                        void * t38 = gm_f_soa_slice_column_6ogdk5f((void *)NULL, cx, NULL, t26, t27, &(*(gm_tjpqvfd72o62u *)((uint8_t *)a2)), t28, t29, t30, t31, t32, t36, t37);
                        if (gm_unw.kind) {
                            goto gm_ll18;
                        }
                        gm_l15 = t38;
                    }
                    {
                        void * t39 = (void *)((uint8_t *)&F.l11);
                        void * t40 = gm_l8;
                        uint8_t t41 = gm_l3;
                        uint64_t t42 = gm_l14;
                        uint64_t t43 = (uintptr_t)t42;
                        uint64_t t44 = GM_LD(uintptr_t, ((uint8_t *)&gm_l10) + 8);
                        if (t43 >= (uintptr_t)(t44)) gm_trap_index(t43, t44, gm_src4nj79mj9jk57, 306, 50);
                        gm_t88rcucg1b7uk t45 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l10)) + t43 * 40) + 16));
                        gm_t88rcucg1b7uk t46 = gm_f_many_ptr_l4v11f8((void *)NULL, cx, NULL, t40, t41, t45);
                        if (gm_unw.kind) {
                            goto gm_ll18;
                        }
                        void * t47 = gm_l15;
                        gm_teti411eujvf4 t48;
                        memset(&t48, 0, sizeof t48);
                        GM_ST(uint8_t, ((uint8_t *)&t48 + 0), ((uint8_t)23ULL));
                        GM_ST(void *, ((uint8_t *)&t48 + 8), t47);
                        gm_tl874t2atnlh3 t49 = gm_l7;
                        void * t50 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t46, &t48, t49);
                        if (gm_unw.kind) {
                            goto gm_ll18;
                        }
                        gm_tftb1odc739t5 t51 = gm_f_push_d943vgp_2(t39, &(*(gm_t69m56meqefs8 *)((uint8_t *)t50)));
                        gm_tftb1odc739t5 t52 = t51;
                        if (((((uint8_t *)&t52))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t52) + 1, gm_src4nj79mj9jk57, 306, 9);
                        gm_b0_1 t53 = GM_LD(gm_b0_1, (((uint8_t *)&t52) + 1));
                    }
                    goto me22;
mn23:;
                    gm_trap("no match arm matched the scrutinee", gm_src4nj79mj9jk57, 304, 5);
me22:;
                }
lc14:;
                {
                    uint64_t t55 = gm_l13;
                    uint64_t r56; bool o57 = __builtin_add_overflow(t55, ((uint64_t)1ULL), &r56);
                    uint64_t t58 = r56;
                    gm_l13 = t58;
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
        void * t59 = gm_l0;
        void * t60 = gm_l1;
        uint8_t t61 = gm_l3;
        void * t62 = gm_l4;
        void * t63 = gm_l5;
        uint8_t t64 = gm_l6;
        uint64_t t65 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t66 = GM_LD(uintptr_t, ((uint8_t *)&gm_l10) + 8);
        if (t65 >= (uintptr_t)(t66)) gm_trap_index(t65, t66, gm_src4nj79mj9jk57, 308, 74);
        gm_t88rcucg1b7uk t67 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l10)) + t65 * 40) + 16));
        gm_tl874t2atnlh3 t68 = gm_l7;
        void * t69 = gm_f_soa_slice_column_6ogdk5f((void *)NULL, cx, NULL, t59, t60, &(*(gm_tjpqvfd72o62u *)((uint8_t *)a2)), ((uint64_t)0ULL), t61, t62, t63, t64, t67, t68);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l16 = t69;
    }
    {
        void * t70 = (void *)((uint8_t *)&F.l11);
        void * t71 = gm_l8;
        gm_t88rcucg1b7uk t72 = gm_f_usize_ty_qnrd30f((void *)NULL, cx, NULL, t71);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t73 = gm_l16;
        gm_teti411eujvf4 t74;
        memset(&t74, 0, sizeof t74);
        GM_ST(uint8_t, ((uint8_t *)&t74 + 0), ((uint8_t)22ULL));
        GM_ST(void *, ((uint8_t *)&t74 + 8), t73);
        gm_tl874t2atnlh3 t75 = gm_l7;
        void * t76 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t72, &t74, t75);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tftb1odc739t5 t77 = gm_f_push_d943vgp_2(t70, &(*(gm_t69m56meqefs8 *)((uint8_t *)t76)));
        gm_tftb1odc739t5 t78 = t77;
        if (((((uint8_t *)&t78))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t78) + 1, gm_src4nj79mj9jk57, 309, 5);
        gm_b0_1 t79 = GM_LD(gm_b0_1, (((uint8_t *)&t78) + 1));
    }
    {
        gm_t88rcucg1b7uk t80 = gm_l9;
        gm_b16_8 t81 = GM_LD(gm_b16_8, (((uint8_t *)&F.l11) + 0));
        gm_teti411eujvf4 t82;
        memset(&t82, 0, sizeof t82);
        GM_ST(uint8_t, ((uint8_t *)&t82 + 0), ((uint8_t)35ULL));
        GM_ST(gm_b16_8, ((uint8_t *)&t82 + 8), t81);
        gm_tl874t2atnlh3 t83 = gm_l7;
        void * t84 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t80, &t82, t83);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t85 = 0;
        F.ret = t84;
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_soa_stand_in_kdtmkn7(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_tf5srh4mp4b3i a2, uint8_t a3) {
    struct gm_fr_soa_stand_in_kdtmkn7 F;
    void * gm_l0; /* s */
    gm_t88rcucg1b7uk gm_l1; /* elem */
    gm_tf5srh4mp4b3i gm_l2; /* len */
    uint8_t gm_l3; /* is_const */
    uint32_t gm_l4; /* id */
    GM_STACK_CHECK(&F, 179u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        void * t2 = gm_l0;
        gm_tf5srh4mp4b3i t3 = gm_l2;
        uint8_t t4 = gm_l3;
        uint32_t t5 = gm_f_soa_template_fa784hs((void *)NULL, cx, NULL, t2, t3, t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l4 = t5;
    }
    {
    }
    {
        void * t6 = gm_l0;
        void * t7 = (void *)(((uint8_t *)t6) + 8);
        uint32_t t8 = gm_l4;
        {
            gm_t88rcucg1b7uk t10 = gm_l1;
            gm_toa2vhe3l4hfr t11;
            memset(&t11, 0, sizeof t11);
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t11 + 8), t10);
            GM_ST(gm_toa2vhe3l4hfr, (uint8_t *)&(*(gm_t2ubvi1j1aq22 *)((uint8_t *)&F.l5)) + 0, t11);
        }
        void * t12 = ((uint8_t *)&F.l5);
        if ((uintptr_t)0 > (uintptr_t)1 || (uintptr_t)1 > (uintptr_t)1) gm_trap_slice((uintptr_t)0, (uintptr_t)1, 1, gm_src4nj79mj9jk57, 154, 26);
        gm_b16_8 t13;
        GM_ST(void *, (uint8_t *)&t13, (void *)(((uint8_t *)t12 + (uintptr_t)0 * 40)));
        GM_ST(uintptr_t, (uint8_t *)&t13 + 8, (uintptr_t)((uintptr_t)1 - (uintptr_t)0));
        gm_b16_8 t14 = t13;
        gm_t88rcucg1b7uk t15 = gm_f_adt_luat93d((void *)NULL, cx, NULL, t7, t8, t14);
        if (gm_unw.kind) {
            goto gm_lad;
        }
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
inline __attribute__((aligned(4))) uint32_t gm_f_soa_temp_2pshn8g(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_tl874t2atnlh3 a2) {
    struct gm_fr_soa_temp_2pshn8g F;
    void * gm_l0; /* locals */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    gm_tl874t2atnlh3 gm_l2; /* span */
    GM_STACK_CHECK(&F, 145u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        gm_tsoie1ppgdvcg t3;
        memset((uint8_t *)&t3 + 21, 0, 3);
        memset((uint8_t *)&t3 + 36, 0, 4);
        gm_b16_8 t4;
        GM_ST(void *, (uint8_t *)&t4, (void *)(gm_shv1k5m05egbe));
        GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(5));
        GM_ST(gm_b16_8, (uint8_t *)&t3 + 0, t4);
        gm_t88rcucg1b7uk t5 = gm_l1;
        GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t3 + 16, t5);
        GM_ST(uint8_t, (uint8_t *)&t3 + 20, ((uint8_t)1));
        gm_tl874t2atnlh3 t6 = gm_l2;
        GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t3 + 24, t6);
        gm_tftb1odc739t5 t7 = gm_f_push_am7bo0v(t2, &t3);
        gm_tftb1odc739t5 t8 = t7;
        if (((((uint8_t *)&t8))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t8) + 1, gm_src4nj79mj9jk57, 245, 5);
        gm_b0_1 t9 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
    }
    {
        void * t10 = gm_l0;
        uint64_t t11 = GM_LD(uintptr_t, (((uint8_t *)t10) + 0) + 8);
        uint64_t r12; bool o13 = __builtin_sub_overflow(t11, ((uint64_t)1ULL), &r12);
        if (o13) gm_trap("integer overflow in `-`", gm_src4nj79mj9jk57, 246, 13);
        uint64_t t14 = r12;
        uint32_t t15 = (uint32_t)t14;
        uint8_t t16 = 0;
        F.ret = t15;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) uint32_t gm_f_soa_template_fa784hs(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tf5srh4mp4b3i a1, uint8_t a2) {
    struct gm_fr_soa_template_fa784hs F;
    void * gm_l0; /* s */
    gm_tf5srh4mp4b3i gm_l1; /* len */
    uint8_t gm_l2; /* is_const */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    gm_tj4mbomtvccf0 gm_l5; /* tm */
    gm_b16_8 gm_l6; /* name */
    uint32_t gm_l7; /* id */
    GM_STACK_CHECK(&F, 830u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        {
            void * t3 = gm_l0;
            gm_b16_8 t4 = GM_LD(gm_b16_8, ((((uint8_t *)t3) + 2416) + 0));
            gm_l3 = t4;
        }
        {
            gm_l4 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t6 = gm_l4;
                uint64_t t7 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                uint8_t t8 = (t6 < t7);
                if (!t8) goto lb5;
                {
                    uint64_t t11 = gm_l4;
                    uint64_t t12 = (uintptr_t)t11;
                    uint64_t t13 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                    gm_tj4mbomtvccf0 t16 = GM_LD(gm_tj4mbomtvccf0, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t12 * 32));
                    gm_l5 = t16;
                    {
                        gm_tf5srh4mp4b3i t19 = GM_LD(gm_tf5srh4mp4b3i, (((uint8_t *)&gm_l5) + 8));
                        gm_tf5srh4mp4b3i t20 = gm_l1;
                        uint8_t t21 = gm_f_same_len_bnkvi6r(t19, t20);
                        if (!t21) goto ie18;
                        uint8_t t22 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 24));
                        uint8_t t23 = gm_l2;
                        uint8_t t24 = ((t22 == t23));
                        if (!t24) goto ie18;
                        {
                            uint32_t t26 = GM_LD(uint32_t, (((uint8_t *)&gm_l5) + 0));
                            uint8_t t27 = 0;
                            F.ret = t26;
                            goto gm_fret;
                        }
                        goto ix18;
ie18:;
ix18:;
                    }
                    goto me14;
mn15:;
                    gm_trap("no match arm matched the scrutinee", gm_src4nj79mj9jk57, 158, 5);
me14:;
                }
lc5:;
                {
                    uint64_t t29 = gm_l4;
                    uint64_t r30; bool o31 = __builtin_add_overflow(t29, ((uint64_t)1ULL), &r30);
                    uint64_t t32 = r30;
                    gm_l4 = t32;
                }
            }
lb5:;
        }
    }
    {
        gm_tf5srh4mp4b3i t33 = gm_l1;
        uint8_t t34 = gm_l2;
        gm_b16_8 t35 = gm_f_soa_shape_755ppqp((void *)NULL, cx, NULL, t33, t34);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tnpifv20irnph t36;
        GM_ST(gm_b16_8, ((uint8_t *)&t36 + 0), t35);
        gm_tabb710d9mq2e t37;
        gm_f_format_thmq1t9((void *)&t37, (void *)NULL, cx, NULL, t36);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t38 = t37;
        if (((((uint8_t *)&t38))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t38) + 8, gm_src4nj79mj9jk57, 161, 13);
        gm_b16_8 t39 = GM_LD(gm_b16_8, (((uint8_t *)&t38) + 8));
        gm_l6 = t39;
    }
    {
        void * t40 = gm_l0;
        void * t41 = (void *)(((uint8_t *)t40) + 8);
        gm_b16_8 t42 = gm_l6;
        gm_b16_8 t43 = t42;
        gm_b16_8 t44 = GM_LD(gm_b16_8, ((uint8_t *)&t43));
        uint32_t t45 = gm_f_fresh_adt_u9rnf9f((void *)NULL, cx, NULL, t41, t44);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l7 = t45;
    }
    {
        void * t46 = gm_l0;
        void * t47 = (void *)(((uint8_t *)t46) + 1088);
        gm_b16_8 t48 = gm_l6;
        gm_b16_8 t49 = t48;
        gm_b16_8 t50 = GM_LD(gm_b16_8, ((uint8_t *)&t49));
        gm_ti0kgh3pc3in2 t51;
        gm_f_empty_struct_b5i94nj((void *)&t51);
        gm_t6art7bprjiqb t52;
        gm_f_compiler_adt_eiq6rf1((void *)&t52, (void *)NULL, cx, NULL, t50, ((uint64_t)0ULL), &t51, ((uint64_t)0ULL));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tftb1odc739t5 t53 = gm_f_push_j84ih6c(t47, &t52);
        gm_tftb1odc739t5 t54 = t53;
        if (((((uint8_t *)&t54))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t54) + 1, gm_src4nj79mj9jk57, 163, 5);
        gm_b0_1 t55 = GM_LD(gm_b0_1, (((uint8_t *)&t54) + 1));
    }
    {
        void * t56 = gm_l0;
        void * t57 = (void *)(((uint8_t *)t56) + 2416);
        gm_tj4mbomtvccf0 t58;
        memset((uint8_t *)&t58 + 4, 0, 4);
        memset((uint8_t *)&t58 + 25, 0, 7);
        uint32_t t59 = gm_l7;
        GM_ST(uint32_t, (uint8_t *)&t58 + 0, t59);
        gm_tf5srh4mp4b3i t60 = gm_l1;
        GM_ST(gm_tf5srh4mp4b3i, (uint8_t *)&t58 + 8, t60);
        uint8_t t61 = gm_l2;
        GM_ST(uint8_t, (uint8_t *)&t58 + 24, t61);
        gm_tftb1odc739t5 t62 = gm_f_push_3dc3qci(t57, &t58);
        gm_tftb1odc739t5 t63 = t62;
        if (((((uint8_t *)&t63))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t63) + 1, gm_src4nj79mj9jk57, 164, 5);
        gm_b0_1 t64 = GM_LD(gm_b0_1, (((uint8_t *)&t63) + 1));
    }
    {
        uint32_t t65 = gm_l7;
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
__attribute__((aligned(4))) void * gm_f_soa_to_const_hhiu49k(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, gm_t88rcucg1b7uk a3, gm_tl874t2atnlh3 a4) {
    struct gm_fr_soa_to_const_hhiu49k F;
    void * gm_l0; /* s */
    void * gm_l1; /* v */
    gm_t88rcucg1b7uk gm_l2; /* src */
    gm_t88rcucg1b7uk gm_l3; /* target */
    gm_tl874t2atnlh3 gm_l4; /* span */
    gm_b16_8 gm_l5; /* fields */
    gm_b16_8 gm_l6; /* target_fields */
    uint64_t gm_l8; /* <tmp> */
    uint64_t gm_l9; /* <tmp> */
    uint64_t gm_l10; /* k */
    void * gm_l11; /* part */
    GM_STACK_CHECK(&F, 915u);
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
        gm_t88rcucg1b7uk t3 = gm_l2;
        gm_th2lqbo77pvsc t4;
        gm_f_struct_fields_chqu3it((void *)&t4, t2, GM_CONV(gm_csguankko4lmhd, t3));
        gm_th2lqbo77pvsc t5 = t4;
        gm_b16_8 t6;
        if (((((uint8_t *)&t5))[16] != 0)) {
            gm_b16_8 t7 = GM_LD(gm_b16_8, (((uint8_t *)&t5) + 0));
            t6 = t7;
        } else {
            gm_b16_8 t8 = gm_f_empty_slice_ose1ba9();
            t6 = t8;
        }
        gm_l5 = t6;
    }
    {
        void * t9 = gm_l0;
        gm_t88rcucg1b7uk t10 = gm_l3;
        gm_th2lqbo77pvsc t11;
        gm_f_struct_fields_chqu3it((void *)&t11, t9, GM_CONV(gm_csguankko4lmhd, t10));
        gm_th2lqbo77pvsc t12 = t11;
        gm_b16_8 t13;
        if (((((uint8_t *)&t12))[16] != 0)) {
            gm_b16_8 t14 = GM_LD(gm_b16_8, (((uint8_t *)&t12) + 0));
            t13 = t14;
        } else {
            gm_b16_8 t15 = gm_f_empty_slice_ose1ba9();
            t13 = t15;
        }
        gm_l6 = t13;
    }
    {
        gm_f_list_vp1djv1_2((void *)&F.l7, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_l9 = ((uint64_t)0ULL);
        }
        {
            uint64_t t17 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
            uint64_t t18 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
            uint64_t t19 = (t17 < t18 ? t17 : t18);
            gm_l8 = t19;
        }
        {
            for (;;) {
                uint64_t t21 = gm_l9;
                uint64_t t22 = gm_l8;
                uint8_t t23 = (t21 < t22);
                if (!t23) goto lb20;
                {
                    uint64_t t26 = gm_l9;
                    uint64_t t27 = t26;
                    uint64_t t30 = GM_LD(uint64_t, ((uint8_t *)&t27));
                    gm_l10 = t30;
                    {
                        uint64_t t32 = gm_l10;
                        uint64_t t33 = (uintptr_t)t32;
                        uint64_t t34 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                        if (t33 >= (uintptr_t)(t34)) gm_trap_index(t33, t34, gm_src4nj79mj9jk57, 328, 24);
                        gm_t88rcucg1b7uk t35 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l5)) + t33 * 40) + 16));
                        void * t36 = gm_l1;
                        void * t37 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t36);
                        if (gm_unw.kind) {
                            goto gm_ll24;
                        }
                        uint64_t t38 = gm_l10;
                        uint32_t t39 = (uint32_t)t38;
                        gm_tbl8il92l97ut t40;
                        GM_ST(uint32_t, (uint8_t *)&t40 + 12, 0);
                        GM_ST(void *, ((uint8_t *)&t40 + 0), t37);
                        GM_ST(uint32_t, ((uint8_t *)&t40 + 8), t39);
                        gm_teti411eujvf4 t41;
                        memset(&t41, 0, sizeof t41);
                        GM_ST(uint8_t, ((uint8_t *)&t41 + 0), ((uint8_t)19ULL));
                        GM_ST(gm_tbl8il92l97ut, ((uint8_t *)&t41 + 8), t40);
                        gm_tl874t2atnlh3 t42 = gm_l4;
                        void * t43 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t35, &t41, t42);
                        if (gm_unw.kind) {
                            goto gm_ll24;
                        }
                        gm_l11 = t43;
                    }
                    {
                        uint64_t t45 = gm_l10;
                        uint64_t t46 = (uintptr_t)t45;
                        uint64_t t47 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                        if (t46 >= (uintptr_t)(t47)) gm_trap_index(t46, t47, gm_src4nj79mj9jk57, 329, 12);
                        gm_t88rcucg1b7uk t48 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l5)) + t46 * 40) + 16));
                        uint64_t t49 = gm_l10;
                        uint64_t t50 = (uintptr_t)t49;
                        uint64_t t51 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                        if (t50 >= (uintptr_t)(t51)) gm_trap_index(t50, t51, gm_src4nj79mj9jk57, 329, 28);
                        gm_t88rcucg1b7uk t52 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t50 * 40) + 16));
                        gm_t88rcucg1b7uk t53 = t48;
                        gm_t88rcucg1b7uk t54 = t52;
                        uint8_t t55 = (gm_eq9m6mvtmgde(((uint8_t *)&t53), ((uint8_t *)&t54)));
                        if (!t55) goto ie44;
                        {
                            void * t57 = (void *)((uint8_t *)&F.l7);
                            void * t58 = gm_l11;
                            gm_tftb1odc739t5 t59 = gm_f_push_d943vgp_2(t57, &(*(gm_t69m56meqefs8 *)((uint8_t *)t58)));
                            gm_tftb1odc739t5 t60 = t59;
                            if (((((uint8_t *)&t60))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t60) + 1, gm_src4nj79mj9jk57, 330, 13);
                            gm_b0_1 t61 = GM_LD(gm_b0_1, (((uint8_t *)&t60) + 1));
                        }
                        goto ix44;
ie44:;
                        {
                            void * t63 = (void *)((uint8_t *)&F.l7);
                            uint64_t t64 = gm_l10;
                            uint64_t t65 = (uintptr_t)t64;
                            uint64_t t66 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                            if (t65 >= (uintptr_t)(t66)) gm_trap_index(t65, t66, gm_src4nj79mj9jk57, 332, 31);
                            gm_t88rcucg1b7uk t67 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t65 * 40) + 16));
                            gm_tmn7n0n9raou5 t68;
                            GM_ST(uint8_t, (uint8_t *)&t68 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t68 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t68 + 4, 0);
                            GM_ST(uint8_t, ((uint8_t *)&t68 + 0), ((uint8_t)6ULL));
                            void * t69 = gm_l11;
                            gm_t8jsc4n9vp2ib t70;
                            GM_ST(gm_tmn7n0n9raou5, ((uint8_t *)&t70 + 0), t68);
                            GM_ST(void *, ((uint8_t *)&t70 + 8), t69);
                            gm_teti411eujvf4 t71;
                            memset(&t71, 0, sizeof t71);
                            GM_ST(uint8_t, ((uint8_t *)&t71 + 0), ((uint8_t)32ULL));
                            GM_ST(gm_t8jsc4n9vp2ib, ((uint8_t *)&t71 + 8), t70);
                            gm_tl874t2atnlh3 t72 = gm_l4;
                            void * t73 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t67, &t71, t72);
                            if (gm_unw.kind) {
                                goto gm_ll24;
                            }
                            gm_tftb1odc739t5 t74 = gm_f_push_d943vgp_2(t63, &(*(gm_t69m56meqefs8 *)((uint8_t *)t73)));
                            gm_tftb1odc739t5 t75 = t74;
                            if (((((uint8_t *)&t75))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t75) + 1, gm_src4nj79mj9jk57, 332, 13);
                            gm_b0_1 t76 = GM_LD(gm_b0_1, (((uint8_t *)&t75) + 1));
                        }
ix44:;
                    }
                    goto me28;
mn29:;
                    gm_trap("no match arm matched the scrutinee", gm_src4nj79mj9jk57, 327, 5);
me28:;
                }
lc20:;
                {
                    uint64_t t78 = gm_l9;
                    uint64_t r79; bool o80 = __builtin_add_overflow(t78, ((uint64_t)1ULL), &r79);
                    uint64_t t81 = r79;
                    gm_l9 = t81;
                }
            }
gm_ll24:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb20; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc20; }
            }
            goto gm_lad;
lb20:;
        }
    }
    {
        gm_t88rcucg1b7uk t82 = gm_l3;
        gm_b16_8 t83 = GM_LD(gm_b16_8, (((uint8_t *)&F.l7) + 0));
        gm_teti411eujvf4 t84;
        memset(&t84, 0, sizeof t84);
        GM_ST(uint8_t, ((uint8_t *)&t84 + 0), ((uint8_t)35ULL));
        GM_ST(gm_b16_8, ((uint8_t *)&t84 + 8), t83);
        gm_tl874t2atnlh3 t85 = gm_l4;
        void * t86 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t82, &t84, t85);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t87 = 0;
        F.ret = t86;
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_soa_type_42lvsj0(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_tf5srh4mp4b3i a2, uint8_t a3, gm_tl874t2atnlh3 a4) {
    struct gm_fr_soa_type_42lvsj0 F;
    void * gm_l0; /* s */
    gm_t88rcucg1b7uk gm_l1; /* elem */
    gm_tf5srh4mp4b3i gm_l2; /* len */
    uint8_t gm_l3; /* is_const */
    gm_tl874t2atnlh3 gm_l4; /* span */
    gm_t88rcucg1b7uk gm_l5; /* t */
    gm_b16_8 gm_l6; /* msg */
    GM_STACK_CHECK(&F, 230u);
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
        gm_tf5srh4mp4b3i t4 = gm_l2;
        uint8_t t5 = gm_l3;
        gm_te3magq8drarp t6;
        gm_f_soa_type_of_q8vtd5j((void *)&t6, (void *)NULL, cx, NULL, t2, t3, t4, t5);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_te3magq8drarp t7 = t6;
        uint8_t t10 = GM_LD(uint8_t, (((uint8_t *)&t7) + 0));
        if (t10 != ((uint8_t)0ULL)) goto mn9;
        gm_t88rcucg1b7uk t11 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t7) + 8));
        gm_l5 = t11;
        gm_t88rcucg1b7uk t12 = gm_l5;
        uint8_t t13 = 0;
        F.ret = t12;
        goto gm_fret;
        goto me8;
mn9:;
        uint8_t t15 = GM_LD(uint8_t, (((uint8_t *)&t7) + 0));
        if (t15 != ((uint8_t)1ULL)) goto mn14;
        gm_b16_8 t16 = GM_LD(gm_b16_8, (((uint8_t *)&t7) + 8));
        gm_l6 = t16;
        {
            void * t18 = gm_l0;
            gm_tl874t2atnlh3 t19 = gm_l4;
            gm_b16_8 t20 = gm_l6;
            gm_b0_1 t21 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t18, t19, t20);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t22 = gm_l0;
            void * t23 = (void *)(((uint8_t *)t22) + 8);
            gm_t88rcucg1b7uk t24 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t23);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t25 = 0;
            F.ret = t24;
            goto gm_fret;
        }
        goto me8;
mn14:;
        gm_trap("no match arm matched the scrutinee", gm_src4nj79mj9jk57, 84, 5);
me8:;
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
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_soa_type_expr_go3p0n7(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2, gm_tl874t2atnlh3 a3) {
    struct gm_fr_soa_type_expr_go3p0n7 F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* module */
    void * gm_l2; /* inner */
    gm_tl874t2atnlh3 gm_l3; /* span */
    gm_t3mf58gv9kf3n gm_l4; /* a */
    void * gm_l5; /* len */
    gm_t88rcucg1b7uk gm_l6; /* elem */
    uint64_t gm_l7; /* n */
    uint64_t gm_l8; /* k */
    uint8_t gm_l9; /* is_const */
    void * gm_l10; /* inner_ty */
    gm_t88rcucg1b7uk gm_l11; /* elem */
    GM_STACK_CHECK(&F, 535u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        void * t2 = gm_l2;
        uint8_t t8 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        switch (t8) { case ((uint8_t)0ULL): goto ma6; case ((uint8_t)1ULL): goto ma6; case ((uint8_t)2ULL): goto ma6; case ((uint8_t)3ULL): goto ma6; case ((uint8_t)4ULL): goto ma6; case ((uint8_t)5ULL): goto ma6; case ((uint8_t)6ULL): goto ma6; case ((uint8_t)7ULL): goto ma6; case ((uint8_t)8ULL): goto ma6; case ((uint8_t)9ULL): goto ma6; case ((uint8_t)10ULL): goto ma6; case ((uint8_t)11ULL): goto ma6; case ((uint8_t)12ULL): goto ma6; case ((uint8_t)13ULL): goto ma6; case ((uint8_t)14ULL): goto ma6; case ((uint8_t)15ULL): goto ma6; case ((uint8_t)16ULL): goto ma6; case ((uint8_t)17ULL): goto ma6; case ((uint8_t)18ULL): goto ma6; case ((uint8_t)19ULL): goto ma6; case ((uint8_t)20ULL): goto ma6; case ((uint8_t)21ULL): goto ma6; case ((uint8_t)22ULL): goto ma6; case ((uint8_t)23ULL): goto ma6; case ((uint8_t)24ULL): goto ma6; case ((uint8_t)25ULL): goto ma6; case ((uint8_t)26ULL): goto ma5; case ((uint8_t)27ULL): goto ma6; case ((uint8_t)28ULL): goto ma4; case ((uint8_t)29ULL): goto ma6; case ((uint8_t)30ULL): goto ma6; case ((uint8_t)31ULL): goto ma6; case ((uint8_t)32ULL): goto ma6; case ((uint8_t)33ULL): goto ma6; case ((uint8_t)34ULL): goto ma6; case ((uint8_t)35ULL): goto ma6; case ((uint8_t)36ULL): goto ma6; case ((uint8_t)37ULL): goto ma6; case ((uint8_t)38ULL): goto ma6; case ((uint8_t)39ULL): goto ma6; case ((uint8_t)40ULL): goto ma6; case ((uint8_t)41ULL): goto ma6; case ((uint8_t)42ULL): goto ma6; case ((uint8_t)43ULL): goto ma6; case ((uint8_t)44ULL): goto ma6; case ((uint8_t)45ULL): goto ma6; case ((uint8_t)46ULL): goto ma6; case ((uint8_t)47ULL): goto ma6; case ((uint8_t)48ULL): goto ma6; case ((uint8_t)49ULL): goto ma6; case ((uint8_t)50ULL): goto ma6; default: goto ma6; }
ma4:;
        uint8_t t10 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t10 != ((uint8_t)28ULL)) goto mn9;
        gm_t3mf58gv9kf3n t11 = GM_LD(gm_t3mf58gv9kf3n, ((((uint8_t *)t2) + 8) + 8));
        gm_l4 = t11;
        void * t13 = GM_LD(void *, (((uint8_t *)&gm_l4) + 0));
        void * t14 = t13;
        if (!(GM_LD(void *, ((uint8_t *)&t14)) != 0)) goto ie12;
        void * t15 = GM_LD(void *, ((uint8_t *)&t14));
        gm_l5 = t15;
        {
            void * t17 = gm_l0;
            uint64_t t18 = gm_l1;
            void * t19 = GM_LD(void *, (((uint8_t *)&gm_l4) + 8));
            gm_t88rcucg1b7uk t20 = gm_f_resolve_type_jjodbei((void *)NULL, cx, NULL, t17, t18, t19);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l6 = t20;
        }
        {
            gm_l7 = ((uint64_t)0ULL);
        }
        {
            void * t21 = gm_l0;
            uint64_t t22 = gm_l1;
            void * t23 = gm_l5;
            gm_t5e6kkuq21nig t24 = gm_f_array_len_eielif2((void *)NULL, cx, NULL, t21, t22, t23);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t5e6kkuq21nig t25 = t24;
            uint8_t t31 = GM_LD(uint8_t, (((uint8_t *)&t25) + 0));
            switch (t31) { case ((uint8_t)0ULL): goto ma27; case ((uint8_t)1ULL): goto ma28; case ((uint8_t)2ULL): goto ma29; default: goto ma30; }
ma27:;
            uint8_t t33 = GM_LD(uint8_t, (((uint8_t *)&t25) + 0));
            if (t33 != ((uint8_t)0ULL)) goto mn32;
            uint64_t t34 = GM_LD(uint64_t, (((uint8_t *)&t25) + 8));
            gm_l8 = t34;
            {
                uint64_t t36 = gm_l8;
                gm_l7 = t36;
            }
            goto me26;
mn32:;
ma28:;
            uint8_t t38 = GM_LD(uint8_t, (((uint8_t *)&t25) + 0));
            if (t38 != ((uint8_t)1ULL)) goto mn37;
            {
                void * t40 = gm_l0;
                void * t41 = gm_l5;
                gm_tl874t2atnlh3 t42 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t41) + 72));
                gm_b16_8 t43;
                GM_ST(void *, (uint8_t *)&t43, (void *)(gm_s58rgrgmho9sa));
                GM_ST(uintptr_t, (uint8_t *)&t43 + 8, (uintptr_t)(67));
                gm_b0_1 t44 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t40, t42, t43);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t45 = gm_l0;
                void * t46 = (void *)(((uint8_t *)t45) + 8);
                gm_t88rcucg1b7uk t47 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t46);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t48 = 0;
                F.ret = t47;
                goto gm_fret;
            }
            goto me26;
mn37:;
ma29:;
            uint8_t t50 = GM_LD(uint8_t, (((uint8_t *)&t25) + 0));
            if (t50 != ((uint8_t)2ULL)) goto mn49;
            void * t51 = gm_l0;
            void * t52 = (void *)(((uint8_t *)t51) + 8);
            gm_t88rcucg1b7uk t53 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t52);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t54 = 0;
            F.ret = t53;
            goto gm_fret;
            goto me26;
mn49:;
ma30:;
            gm_trap("no match arm matched the scrutinee", gm_src4nj79mj9jk57, 207, 13);
me26:;
        }
        {
            void * t55 = gm_l0;
            gm_t88rcucg1b7uk t56 = gm_l6;
            uint64_t t57 = gm_l7;
            gm_tf5srh4mp4b3i t58;
            GM_ST(uint8_t, (uint8_t *)&t58 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t58 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t58 + 12, 0);
            GM_ST(uint64_t, ((uint8_t *)&t58), t57);
            ((uint8_t *)&t58)[8] = 1;
            gm_tl874t2atnlh3 t59 = gm_l3;
            gm_t88rcucg1b7uk t60 = gm_f_soa_type_42lvsj0((void *)NULL, cx, NULL, t55, t56, t58, ((uint8_t)0), t59);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t61 = 0;
            F.ret = t60;
            goto gm_fret;
        }
        goto ix12;
ie12:;
ix12:;
        goto me3;
mn9:;
ma5:;
        uint8_t t63 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t63 != ((uint8_t)26ULL)) goto mn62;
        uint8_t t64 = GM_LD(uint8_t, (((((uint8_t *)t2) + 8) + 8) + 0));
        gm_l9 = t64;
        void * t65 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 8));
        gm_l10 = t65;
        {
            void * t67 = gm_l0;
            uint64_t t68 = gm_l1;
            void * t69 = gm_l10;
            gm_t88rcucg1b7uk t70 = gm_f_resolve_type_jjodbei((void *)NULL, cx, NULL, t67, t68, t69);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l11 = t70;
        }
        {
            void * t71 = gm_l0;
            gm_t88rcucg1b7uk t72 = gm_l11;
            gm_tf5srh4mp4b3i t73;
            memset(&t73, 0, sizeof t73);
            uint8_t t74 = gm_l9;
            gm_tl874t2atnlh3 t75 = gm_l3;
            gm_t88rcucg1b7uk t76 = gm_f_soa_type_42lvsj0((void *)NULL, cx, NULL, t71, t72, t73, t74, t75);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t77 = 0;
            F.ret = t76;
            goto gm_fret;
        }
        goto me3;
mn62:;
ma6:;
        goto me3;
mn78:;
ma7:;
        gm_trap("no match arm matched the scrutinee", gm_src4nj79mj9jk57, 203, 5);
me3:;
    }
    {
        void * t80 = gm_l0;
        gm_tl874t2atnlh3 t81 = gm_l3;
        gm_b16_8 t82;
        GM_ST(void *, (uint8_t *)&t82, (void *)(gm_ssnb4or831s5l));
        GM_ST(uintptr_t, (uint8_t *)&t82 + 8, (uintptr_t)(77));
        gm_b0_1 t83 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t80, t81, t82);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t84 = gm_l0;
        void * t85 = (void *)(((uint8_t *)t84) + 8);
        gm_t88rcucg1b7uk t86 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t85);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t87 = 0;
        F.ret = t86;
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
__attribute__((aligned(4))) void gm_f_soa_type_of_q8vtd5j(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_tf5srh4mp4b3i a2, uint8_t a3) {
    struct gm_fr_soa_type_of_q8vtd5j F;
    void * gm_l0; /* s */
    gm_t88rcucg1b7uk gm_l1; /* elem */
    gm_tf5srh4mp4b3i gm_l2; /* len */
    uint8_t gm_l3; /* is_const0 */
    void * gm_l4; /* tys */
    uint8_t gm_l5; /* is_const */
    gm_b16_8 gm_l6; /* not_struct */
    uint32_t gm_l7; /* id */
    gm_b16_8 gm_l9; /* fields */
    gm_b16_8 gm_l11; /* <tmp> */
    uint64_t gm_l12; /* <tmp> */
    gm_b16_8 gm_l14; /* <tmp> */
    uint64_t gm_l15; /* <tmp> */
    gm_tcu60n24kv26r gm_l16; /* m */
    gm_b16_8 gm_l17; /* name */
    gm_b16_8 gm_l19; /* <tmp> */
    uint64_t gm_l20; /* <tmp> */
    uint64_t gm_l22; /* n */
    gm_t88rcucg1b7uk gm_l23; /* ct */
    uint32_t gm_l24; /* template */
    uint32_t gm_l25; /* soa */
    gm_t88rcucg1b7uk gm_l27; /* t */
    GM_STACK_CHECK(&F, 3358u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        void * t2 = gm_l0;
        void * t3 = (void *)(((uint8_t *)t2) + 8);
        gm_l4 = t3;
    }
    {
        void * t5 = gm_l4;
        gm_t88rcucg1b7uk t6 = gm_l1;
        uint8_t t7 = gm_f_is_error_8k27gd1(t5, GM_CONV(gm_csguankko4lmhd, t6));
        if (!t7) goto ie4;
        {
            gm_t88rcucg1b7uk t9 = gm_l1;
            gm_te3magq8drarp t10;
            GM_ST(uint8_t, (uint8_t *)&t10 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t10 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t10 + 4, 0);
            GM_ST(uint32_t, (uint8_t *)&t10 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t10 + 16, 0);
            GM_ST(uint8_t, ((uint8_t *)&t10 + 0), ((uint8_t)0ULL));
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t10 + 8), t9);
            uint8_t t11 = 0;
            F.ret = t10;
            goto gm_fret;
        }
        goto ix4;
ie4:;
ix4:;
    }
    {
        uint8_t t12 = 0;
        uint8_t t13 = gm_l3;
        if (t13) {
            uint8_t t14;
            if (((((uint8_t *)&gm_l2))[8] != 0)) goto mn16;
            t14 = ((uint8_t)1);
            goto me15;
mn16:;
            t14 = ((uint8_t)0);
            goto me15;
mn17:;
            gm_trap("no match arm matched the scrutinee", gm_src4nj79mj9jk57, 98, 31);
me15:;
            t12 = t14;
        } else t12 = 0;
        gm_l5 = t12;
    }
    {
        void * t19 = gm_l4;
        gm_t88rcucg1b7uk t20 = gm_l1;
        uint8_t t21 = gm_f_has_param_u9t0efs(t19, GM_CONV(gm_csguankko4lmhd, t20));
        if (!t21) goto ie18;
        void * t22 = gm_l4;
        gm_t88rcucg1b7uk t23 = gm_l1;
        gm_t53at80pfdus3 t24;
        gm_f_kind_jcnt4hm((void *)&t24, t22, GM_CONV(gm_csguankko4lmhd, t23));
        gm_t53at80pfdus3 t25 = t24;
        uint8_t t26;
        uint8_t t32 = GM_LD(uint8_t, (((uint8_t *)&t25) + 0));
        switch (t32) { case ((uint8_t)0ULL): goto ma30; case ((uint8_t)1ULL): goto ma30; case ((uint8_t)2ULL): goto ma30; case ((uint8_t)3ULL): goto ma30; case ((uint8_t)4ULL): goto ma30; case ((uint8_t)5ULL): goto ma30; case ((uint8_t)6ULL): goto ma30; case ((uint8_t)7ULL): goto ma30; case ((uint8_t)8ULL): goto ma30; case ((uint8_t)9ULL): goto ma30; case ((uint8_t)10ULL): goto ma30; case ((uint8_t)11ULL): goto ma30; case ((uint8_t)12ULL): goto ma30; case ((uint8_t)13ULL): goto ma30; case ((uint8_t)14ULL): goto ma30; case ((uint8_t)15ULL): goto ma30; case ((uint8_t)16ULL): goto ma30; case ((uint8_t)17ULL): goto ma29; case ((uint8_t)18ULL): goto ma30; case ((uint8_t)19ULL): goto ma28; case ((uint8_t)20ULL): goto ma30; case ((uint8_t)21ULL): goto ma30; case ((uint8_t)22ULL): goto ma30; case ((uint8_t)23ULL): goto ma30; case ((uint8_t)24ULL): goto ma30; case ((uint8_t)25ULL): goto ma30; case ((uint8_t)26ULL): goto ma30; default: goto ma30; }
ma28:;
        uint8_t t34 = GM_LD(uint8_t, (((uint8_t *)&t25) + 0));
        if (t34 != ((uint8_t)19ULL)) goto mn33;
        t26 = ((uint8_t)1);
        goto me27;
mn33:;
ma29:;
        uint8_t t36 = GM_LD(uint8_t, (((uint8_t *)&t25) + 0));
        if (t36 != ((uint8_t)17ULL)) goto mn35;
        t26 = ((uint8_t)1);
        goto me27;
mn35:;
ma30:;
        t26 = ((uint8_t)0);
        goto me27;
mn37:;
ma31:;
        gm_trap("no match arm matched the scrutinee", gm_src4nj79mj9jk57, 99, 32);
me27:;
        if (!t26) goto ie18;
        {
            void * t39 = gm_l0;
            gm_t88rcucg1b7uk t40 = gm_l1;
            gm_tf5srh4mp4b3i t41 = gm_l2;
            uint8_t t42 = gm_l5;
            gm_t88rcucg1b7uk t43 = gm_f_soa_stand_in_kdtmkn7((void *)NULL, cx, NULL, t39, t40, t41, t42);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_te3magq8drarp t44;
            GM_ST(uint8_t, (uint8_t *)&t44 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t44 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t44 + 4, 0);
            GM_ST(uint32_t, (uint8_t *)&t44 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t44 + 16, 0);
            GM_ST(uint8_t, ((uint8_t *)&t44 + 0), ((uint8_t)0ULL));
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t44 + 8), t43);
            uint8_t t45 = 0;
            F.ret = t44;
            goto gm_fret;
        }
        goto ix18;
ie18:;
ix18:;
    }
    {
        void * t46 = gm_l0;
        gm_t88rcucg1b7uk t47 = gm_l1;
        gm_b16_8 t48 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t46, t47);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tnpifv20irnph t49;
        GM_ST(gm_b16_8, ((uint8_t *)&t49 + 0), t48);
        gm_tabb710d9mq2e t50;
        gm_f_format_16dceus((void *)&t50, (void *)NULL, cx, NULL, t49);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t51 = t50;
        if (((((uint8_t *)&t51))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t51) + 8, gm_src4nj79mj9jk57, 100, 19);
        gm_b16_8 t52 = GM_LD(gm_b16_8, (((uint8_t *)&t51) + 8));
        gm_l6 = t52;
    }
    {
        void * t54 = gm_l4;
        gm_t88rcucg1b7uk t55 = gm_l1;
        gm_t53at80pfdus3 t56;
        gm_f_kind_jcnt4hm((void *)&t56, t54, GM_CONV(gm_csguankko4lmhd, t55));
        gm_t53at80pfdus3 t57 = t56;
        uint8_t t58 = GM_LD(uint8_t, (((uint8_t *)&t57) + 0));
        if (t58 != ((uint8_t)17ULL)) goto ie53;
        uint32_t t59 = GM_LD(uint32_t, ((((uint8_t *)&t57) + 8) + 0));
        gm_l7 = t59;
        {
            uint32_t t61 = gm_l7;
            uint64_t t62 = (uint64_t)t61;
            uint64_t t63 = (uintptr_t)t62;
            void * t64 = gm_l0;
            uint64_t t65 = GM_LD(uintptr_t, ((((uint8_t *)t64) + 1088) + 0) + 8);
            if (t63 >= (uintptr_t)(t65)) gm_trap_index(t63, t65, gm_src4nj79mj9jk57, 102, 13);
            gm_t6art7bprjiqb t66 = GM_LD(gm_t6art7bprjiqb, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t64) + 1088) + 0)) + t63 * 184));
            F.l8 = t66;
        }
        {
            gm_b16_8 t67 = gm_f_empty_slice_ose1ba9();
            gm_b16_8 t68 = t67;
            gm_b16_8 t69 = GM_LD(gm_b16_8, ((uint8_t *)&t68));
            gm_l9 = t69;
        }
        {
            uint8_t t72 = GM_LD(uint8_t, ((((uint8_t *)&F.l8) + 24) + 0));
            if (t72 != ((uint8_t)0ULL)) goto mn71;
            gm_tpautr4br7h3l t73 = GM_LD(gm_tpautr4br7h3l, ((((uint8_t *)&F.l8) + 24) + 8));
            F.l10 = t73;
            {
                uint8_t t76;
                if ((((((uint8_t *)&F.l10) + 40))[4] != 0)) goto mn78;
                t76 = ((uint8_t)0);
                goto me77;
mn78:;
                t76 = ((uint8_t)1);
                goto me77;
mn79:;
                gm_trap("no match arm matched the scrutinee", gm_src4nj79mj9jk57, 106, 16);
me77:;
                if (!t76) goto ie75;
                {
                    void * t81 = gm_l0;
                    gm_t88rcucg1b7uk t82 = gm_l1;
                    gm_b16_8 t83 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t81, t82);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tnpifv20irnph t84;
                    GM_ST(gm_b16_8, ((uint8_t *)&t84 + 0), t83);
                    gm_tabb710d9mq2e t85;
                    gm_f_format_fambdq4((void *)&t85, (void *)NULL, cx, NULL, t84);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tabb710d9mq2e t86 = t85;
                    if (((((uint8_t *)&t86))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t86) + 8, gm_src4nj79mj9jk57, 106, 51);
                    gm_b16_8 t87 = GM_LD(gm_b16_8, (((uint8_t *)&t86) + 8));
                    gm_b16_8 t88 = t87;
                    gm_b16_8 t89 = GM_LD(gm_b16_8, ((uint8_t *)&t88));
                    gm_te3magq8drarp t90;
                    GM_ST(uint8_t, (uint8_t *)&t90 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t90 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t90 + 4, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t90 + 0), ((uint8_t)1ULL));
                    GM_ST(gm_b16_8, ((uint8_t *)&t90 + 8), t89);
                    uint8_t t91 = 0;
                    F.ret = t90;
                    goto gm_fret;
                }
                goto ix75;
ie75:;
ix75:;
            }
            {
                gm_b16_8 t92 = GM_LD(gm_b16_8, (((uint8_t *)&F.l10) + 0));
                gm_b16_8 t93 = t92;
                gm_b16_8 t94 = GM_LD(gm_b16_8, ((uint8_t *)&t93));
                gm_l9 = t94;
            }
            goto me70;
mn71:;
            gm_b16_8 t96 = gm_l6;
            gm_b16_8 t97 = t96;
            gm_b16_8 t98 = GM_LD(gm_b16_8, ((uint8_t *)&t97));
            gm_te3magq8drarp t99;
            GM_ST(uint8_t, (uint8_t *)&t99 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t99 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t99 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t99 + 0), ((uint8_t)1ULL));
            GM_ST(gm_b16_8, ((uint8_t *)&t99 + 8), t98);
            uint8_t t100 = 0;
            F.ret = t99;
            goto gm_fret;
            goto me70;
mn95:;
            gm_trap("no match arm matched the scrutinee", gm_src4nj79mj9jk57, 104, 5);
me70:;
        }
        {
            uint64_t t102 = GM_LD(uintptr_t, ((uint8_t *)&gm_l9) + 8);
            uint8_t t103 = ((t102 == ((uint64_t)0ULL)));
            if (!t103) goto ie101;
            {
                void * t105 = gm_l0;
                gm_t88rcucg1b7uk t106 = gm_l1;
                gm_b16_8 t107 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t105, t106);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tnpifv20irnph t108;
                GM_ST(gm_b16_8, ((uint8_t *)&t108 + 0), t107);
                gm_tabb710d9mq2e t109;
                gm_f_format_4nn4ect((void *)&t109, (void *)NULL, cx, NULL, t108);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t110 = t109;
                if (((((uint8_t *)&t110))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t110) + 8, gm_src4nj79mj9jk57, 111, 38);
                gm_b16_8 t111 = GM_LD(gm_b16_8, (((uint8_t *)&t110) + 8));
                gm_b16_8 t112 = t111;
                gm_b16_8 t113 = GM_LD(gm_b16_8, ((uint8_t *)&t112));
                gm_te3magq8drarp t114;
                GM_ST(uint8_t, (uint8_t *)&t114 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t114 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t114 + 4, 0);
                GM_ST(uint8_t, ((uint8_t *)&t114 + 0), ((uint8_t)1ULL));
                GM_ST(gm_b16_8, ((uint8_t *)&t114 + 8), t113);
                uint8_t t115 = 0;
                F.ret = t114;
                goto gm_fret;
            }
            goto ix101;
ie101:;
ix101:;
        }
        {
            {
                gm_b16_8 t117 = gm_l9;
                gm_l11 = t117;
            }
            {
                gm_l12 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t119 = gm_l12;
                    uint64_t t120 = GM_LD(uintptr_t, ((uint8_t *)&gm_l11) + 8);
                    uint8_t t121 = (t119 < t120);
                    if (!t121) goto lb118;
                    {
                        uint64_t t124 = gm_l12;
                        uint64_t t125 = (uintptr_t)t124;
                        uint64_t t126 = GM_LD(uintptr_t, ((uint8_t *)&gm_l11) + 8);
                        gm_t0mglksrkfa5e t129 = GM_LD(gm_t0mglksrkfa5e, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l11)) + t125 * 40));
                        F.l13 = t129;
                        {
                            uint8_t t132 = (((GM_LD(uintptr_t, (((uint8_t *)&F.l13) + 0) + 8) == 3) && memcmp(GM_LD(void *, (((uint8_t *)&F.l13) + 0)), gm_sm2k5tp7dtg07, 3) == 0));
                            if (!t132) goto ie131;
                            {
                                void * t134 = gm_l0;
                                gm_t88rcucg1b7uk t135 = gm_l1;
                                gm_b16_8 t136 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t134, t135);
                                if (gm_unw.kind) {
                                    goto gm_ll122;
                                }
                                gm_tnpifv20irnph t137;
                                GM_ST(gm_b16_8, ((uint8_t *)&t137 + 0), t136);
                                gm_tabb710d9mq2e t138;
                                gm_f_format_3dfoht6((void *)&t138, (void *)NULL, cx, NULL, t137);
                                if (gm_unw.kind) {
                                    goto gm_ll122;
                                }
                                gm_tabb710d9mq2e t139 = t138;
                                if (((((uint8_t *)&t139))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t139) + 8, gm_src4nj79mj9jk57, 113, 42);
                                gm_b16_8 t140 = GM_LD(gm_b16_8, (((uint8_t *)&t139) + 8));
                                gm_b16_8 t141 = t140;
                                gm_b16_8 t142 = GM_LD(gm_b16_8, ((uint8_t *)&t141));
                                gm_te3magq8drarp t143;
                                GM_ST(uint8_t, (uint8_t *)&t143 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t143 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t143 + 4, 0);
                                GM_ST(uint8_t, ((uint8_t *)&t143 + 0), ((uint8_t)1ULL));
                                GM_ST(gm_b16_8, ((uint8_t *)&t143 + 8), t142);
                                uint8_t t144 = 0;
                                F.ret = t143;
                                goto gm_fret;
                            }
                            goto ix131;
ie131:;
ix131:;
                        }
                        goto me127;
mn128:;
                        gm_trap("no match arm matched the scrutinee", gm_src4nj79mj9jk57, 112, 5);
me127:;
                    }
lc118:;
                    {
                        uint64_t t146 = gm_l12;
                        uint64_t r147; bool o148 = __builtin_add_overflow(t146, ((uint64_t)1ULL), &r147);
                        uint64_t t149 = r147;
                        gm_l12 = t149;
                    }
                }
gm_ll122:;
                if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb118; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc118; }
                }
                goto gm_lad;
lb118:;
            }
        }
        {
            {
                void * t151 = gm_l0;
                gm_b16_8 t152 = GM_LD(gm_b16_8, ((((uint8_t *)t151) + 2376) + 0));
                gm_l14 = t152;
            }
            {
                gm_l15 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t154 = gm_l15;
                    uint64_t t155 = GM_LD(uintptr_t, ((uint8_t *)&gm_l14) + 8);
                    uint8_t t156 = (t154 < t155);
                    if (!t156) goto lb153;
                    {
                        uint64_t t159 = gm_l15;
                        uint64_t t160 = (uintptr_t)t159;
                        uint64_t t161 = GM_LD(uintptr_t, ((uint8_t *)&gm_l14) + 8);
                        gm_tcu60n24kv26r t164 = GM_LD(gm_tcu60n24kv26r, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l14)) + t160 * 32));
                        gm_l16 = t164;
                        {
                            gm_t88rcucg1b7uk t167 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l16) + 0));
                            gm_t88rcucg1b7uk t168 = gm_l1;
                            gm_t88rcucg1b7uk t169 = t167;
                            gm_t88rcucg1b7uk t170 = t168;
                            uint8_t t171 = (gm_eq9m6mvtmgde(((uint8_t *)&t169), ((uint8_t *)&t170)));
                            if (!t171) goto ie166;
                            gm_tf5srh4mp4b3i t172 = GM_LD(gm_tf5srh4mp4b3i, (((uint8_t *)&gm_l16) + 8));
                            gm_tf5srh4mp4b3i t173 = gm_l2;
                            uint8_t t174 = gm_f_same_len_bnkvi6r(t172, t173);
                            if (!t174) goto ie166;
                            uint8_t t175 = GM_LD(uint8_t, (((uint8_t *)&gm_l16) + 24));
                            uint8_t t176 = gm_l5;
                            uint8_t t177 = ((t175 == t176));
                            if (!t177) goto ie166;
                            {
                                gm_t88rcucg1b7uk t179 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l16) + 28));
                                gm_te3magq8drarp t180;
                                GM_ST(uint8_t, (uint8_t *)&t180 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t180 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t180 + 4, 0);
                                GM_ST(uint32_t, (uint8_t *)&t180 + 12, 0);
                                GM_ST(uint64_t, (uint8_t *)&t180 + 16, 0);
                                GM_ST(uint8_t, ((uint8_t *)&t180 + 0), ((uint8_t)0ULL));
                                GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t180 + 8), t179);
                                uint8_t t181 = 0;
                                F.ret = t180;
                                goto gm_fret;
                            }
                            goto ix166;
ie166:;
ix166:;
                        }
                        goto me162;
mn163:;
                        gm_trap("no match arm matched the scrutinee", gm_src4nj79mj9jk57, 115, 5);
me162:;
                    }
lc153:;
                    {
                        uint64_t t183 = gm_l15;
                        uint64_t r184; bool o185 = __builtin_add_overflow(t183, ((uint64_t)1ULL), &r184);
                        uint64_t t186 = r184;
                        gm_l15 = t186;
                    }
                }
lb153:;
            }
        }
        {
            gm_t6j6smj7qoctu t187;
            gm_tf5srh4mp4b3i t188 = gm_l2;
            uint8_t t189 = gm_l5;
            gm_b16_8 t190 = gm_f_soa_shape_755ppqp((void *)NULL, cx, NULL, t188, t189);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(gm_b16_8, (uint8_t *)&t187 + 0, t190);
            void * t191 = gm_l0;
            gm_t88rcucg1b7uk t192 = gm_l1;
            gm_b16_8 t193 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t191, t192);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(gm_b16_8, (uint8_t *)&t187 + 16, t193);
            gm_tabb710d9mq2e t194;
            gm_f_format_j0i128e((void *)&t194, (void *)NULL, cx, NULL, &t187);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t195 = t194;
            if (((((uint8_t *)&t195))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t195) + 8, gm_src4nj79mj9jk57, 118, 13);
            gm_b16_8 t196 = GM_LD(gm_b16_8, (((uint8_t *)&t195) + 8));
            gm_l17 = t196;
        }
        {
            gm_f_list_spqn36o((void *)&F.l18, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            {
                gm_b16_8 t198 = gm_l9;
                gm_l19 = t198;
            }
            {
                gm_l20 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t200 = gm_l20;
                    uint64_t t201 = GM_LD(uintptr_t, ((uint8_t *)&gm_l19) + 8);
                    uint8_t t202 = (t200 < t201);
                    if (!t202) goto lb199;
                    {
                        uint64_t t205 = gm_l20;
                        uint64_t t206 = (uintptr_t)t205;
                        uint64_t t207 = GM_LD(uintptr_t, ((uint8_t *)&gm_l19) + 8);
                        gm_t0mglksrkfa5e t210 = GM_LD(gm_t0mglksrkfa5e, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l19)) + t206 * 40));
                        F.l21 = t210;
                        {
                            gm_t88rcucg1b7uk t212;
                            if (!((((uint8_t *)&gm_l2))[8] != 0)) goto ie213;
                            uint64_t t214 = GM_LD(uint64_t, ((uint8_t *)&gm_l2));
                            gm_l22 = t214;
                            void * t216 = gm_l4;
                            uint64_t t217 = gm_l22;
                            gm_t88rcucg1b7uk t218 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l21) + 16));
                            gm_t88rcucg1b7uk t219 = gm_f_array_th8sdi3((void *)NULL, cx, NULL, t216, t217, t218);
                            if (gm_unw.kind) {
                                goto gm_ll203;
                            }
                            gm_t88rcucg1b7uk t220 = t219;
                            t212 = t220;
                            goto ix213;
ie213:;
                            void * t222 = gm_l4;
                            uint8_t t223 = gm_l5;
                            gm_t88rcucg1b7uk t224 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l21) + 16));
                            gm_t88rcucg1b7uk t225 = gm_f_many_ptr_l4v11f8((void *)NULL, cx, NULL, t222, t223, t224);
                            if (gm_unw.kind) {
                                goto gm_ll203;
                            }
                            gm_t88rcucg1b7uk t226 = t225;
                            t212 = t226;
ix213:;
                            gm_l23 = t212;
                        }
                        {
                            void * t227 = (void *)((uint8_t *)&F.l18);
                            gm_t0mglksrkfa5e t228;
                            memset((uint8_t *)&t228 + 21, 0, 3);
                            gm_b16_8 t229 = GM_LD(gm_b16_8, (((uint8_t *)&F.l21) + 0));
                            GM_ST(gm_b16_8, (uint8_t *)&t228 + 0, t229);
                            gm_t88rcucg1b7uk t230 = gm_l23;
                            GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t228 + 16, t230);
                            gm_t9qkh0rsooc8d t231;
                            GM_ST(uint8_t, ((uint8_t *)&t231 + 0), ((uint8_t)2ULL));
                            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&t228 + 20, t231);
                            gm_tf5srh4mp4b3i t232;
                            memset(&t232, 0, sizeof t232);
                            GM_ST(gm_tf5srh4mp4b3i, (uint8_t *)&t228 + 24, t232);
                            gm_tftb1odc739t5 t233 = gm_f_push_vrmo7c5(t227, &t228);
                            gm_tftb1odc739t5 t234 = t233;
                            if (((((uint8_t *)&t234))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t234) + 1, gm_src4nj79mj9jk57, 122, 9);
                            gm_b0_1 t235 = GM_LD(gm_b0_1, (((uint8_t *)&t234) + 1));
                        }
                        goto me208;
mn209:;
                        gm_trap("no match arm matched the scrutinee", gm_src4nj79mj9jk57, 120, 5);
me208:;
                    }
lc199:;
                    {
                        uint64_t t237 = gm_l20;
                        uint64_t r238; bool o239 = __builtin_add_overflow(t237, ((uint64_t)1ULL), &r238);
                        uint64_t t240 = r238;
                        gm_l20 = t240;
                    }
                }
gm_ll203:;
                if (gm_unw.frame == &F.h && gm_unw.label == 2) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb199; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc199; }
                }
                goto gm_lad;
lb199:;
            }
        }
        {
            uint8_t t242;
            if (((((uint8_t *)&gm_l2))[8] != 0)) goto mn244;
            t242 = ((uint8_t)1);
            goto me243;
mn244:;
            t242 = ((uint8_t)0);
            goto me243;
mn245:;
            gm_trap("no match arm matched the scrutinee", gm_src4nj79mj9jk57, 124, 8);
me243:;
            if (!t242) goto ie241;
            {
                void * t247 = (void *)((uint8_t *)&F.l18);
                gm_t0mglksrkfa5e t248;
                memset((uint8_t *)&t248 + 21, 0, 3);
                gm_b16_8 t249;
                GM_ST(void *, (uint8_t *)&t249, (void *)(gm_sm2k5tp7dtg07));
                GM_ST(uintptr_t, (uint8_t *)&t249 + 8, (uintptr_t)(3));
                GM_ST(gm_b16_8, (uint8_t *)&t248 + 0, t249);
                void * t250 = gm_l4;
                gm_t88rcucg1b7uk t251 = gm_f_usize_ty_qnrd30f((void *)NULL, cx, NULL, t250);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t248 + 16, t251);
                gm_t9qkh0rsooc8d t252;
                GM_ST(uint8_t, ((uint8_t *)&t252 + 0), ((uint8_t)2ULL));
                GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&t248 + 20, t252);
                gm_tf5srh4mp4b3i t253;
                memset(&t253, 0, sizeof t253);
                GM_ST(gm_tf5srh4mp4b3i, (uint8_t *)&t248 + 24, t253);
                gm_tftb1odc739t5 t254 = gm_f_push_vrmo7c5(t247, &t248);
                gm_tftb1odc739t5 t255 = t254;
                if (((((uint8_t *)&t255))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t255) + 1, gm_src4nj79mj9jk57, 124, 22);
                gm_b0_1 t256 = GM_LD(gm_b0_1, (((uint8_t *)&t255) + 1));
            }
            goto ix241;
ie241:;
ix241:;
        }
        {
            void * t257 = gm_l0;
            gm_tf5srh4mp4b3i t258 = gm_l2;
            uint8_t t259 = gm_l5;
            uint32_t t260 = gm_f_soa_template_fa784hs((void *)NULL, cx, NULL, t257, t258, t259);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l24 = t260;
        }
        {
            void * t261 = gm_l4;
            gm_b16_8 t262 = gm_l17;
            gm_b16_8 t263 = t262;
            gm_b16_8 t264 = GM_LD(gm_b16_8, ((uint8_t *)&t263));
            uint32_t t265 = gm_f_fresh_adt_u9rnf9f((void *)NULL, cx, NULL, t261, t264);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l25 = t265;
        }
        {
            void * t266 = gm_l4;
            uint32_t t267 = gm_l25;
            uint32_t t268 = gm_l24;
            gm_f_set_template_o09q96o(t266, t267, t268);
        }
        {
            void * t269 = gm_l0;
            void * t270 = (void *)(((uint8_t *)t269) + 1088);
            gm_b16_8 t271 = gm_l17;
            gm_b16_8 t272 = t271;
            gm_b16_8 t273 = GM_LD(gm_b16_8, ((uint8_t *)&t272));
            uint64_t t274 = GM_LD(uint64_t, (((uint8_t *)&F.l8) + 16));
            gm_tpautr4br7h3l t275;
            memset((uint8_t *)&t275 + 17, 0, 7);
            gm_b16_8 t276 = GM_LD(gm_b16_8, (((uint8_t *)&F.l18) + 0));
            GM_ST(gm_b16_8, (uint8_t *)&t275 + 0, t276);
            GM_ST(uint8_t, (uint8_t *)&t275 + 16, ((uint8_t)0));
            gm_tf5srh4mp4b3i t277;
            memset(&t277, 0, sizeof t277);
            GM_ST(gm_tf5srh4mp4b3i, (uint8_t *)&t275 + 24, t277);
            gm_tkn42u7na3t81 t278;
            memset(&t278, 0, sizeof t278);
            GM_ST(gm_tkn42u7na3t81, (uint8_t *)&t275 + 40, t278);
            gm_ti0kgh3pc3in2 t279;
            GM_ST(uint8_t, (uint8_t *)&t279 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t279 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t279 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t279 + 0), ((uint8_t)0ULL));
            GM_ST(gm_tpautr4br7h3l, ((uint8_t *)&t279 + 8), t275);
            uint64_t t280 = GM_LD(uintptr_t, (((uint8_t *)&F.l18) + 0) + 8);
            gm_t6art7bprjiqb t281;
            gm_f_compiler_adt_eiq6rf1((void *)&t281, (void *)NULL, cx, NULL, t273, t274, &t279, t280);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tftb1odc739t5 t282 = gm_f_push_j84ih6c(t270, &t281);
            gm_tftb1odc739t5 t283 = t282;
            if (((((uint8_t *)&t283))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t283) + 1, gm_src4nj79mj9jk57, 129, 5);
            gm_b0_1 t284 = GM_LD(gm_b0_1, (((uint8_t *)&t283) + 1));
        }
        {
            void * t285 = gm_l0;
            void * t286 = (void *)(((uint8_t *)t285) + 2320);
            uint32_t t287 = gm_l25;
            gm_tjpqvfd72o62u t288;
            memset((uint8_t *)&t288 + 4, 0, 4);
            memset((uint8_t *)&t288 + 25, 0, 7);
            gm_t88rcucg1b7uk t289 = gm_l1;
            GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t288 + 0, t289);
            gm_tf5srh4mp4b3i t290 = gm_l2;
            GM_ST(gm_tf5srh4mp4b3i, (uint8_t *)&t288 + 8, t290);
            uint8_t t291 = gm_l5;
            GM_ST(uint8_t, (uint8_t *)&t288 + 24, t291);
            gm_tftb1odc739t5 t292 = gm_f_put_ftmshk7(t286, t287, &t288);
            gm_tftb1odc739t5 t293 = t292;
            if (((((uint8_t *)&t293))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t293) + 1, gm_src4nj79mj9jk57, 130, 5);
            gm_b0_1 t294 = GM_LD(gm_b0_1, (((uint8_t *)&t293) + 1));
        }
        {
        }
        {
            void * t295 = gm_l4;
            uint32_t t296 = gm_l25;
            {
                gm_t88rcucg1b7uk t298 = gm_l1;
                gm_toa2vhe3l4hfr t299;
                memset(&t299, 0, sizeof t299);
                GM_ST(uint8_t, ((uint8_t *)&t299 + 0), ((uint8_t)0ULL));
                GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t299 + 8), t298);
                GM_ST(gm_toa2vhe3l4hfr, (uint8_t *)&(*(gm_t2ubvi1j1aq22 *)((uint8_t *)&F.l26)) + 0, t299);
            }
            void * t300 = ((uint8_t *)&F.l26);
            if ((uintptr_t)0 > (uintptr_t)1 || (uintptr_t)1 > (uintptr_t)1) gm_trap_slice((uintptr_t)0, (uintptr_t)1, 1, gm_src4nj79mj9jk57, 131, 23);
            gm_b16_8 t301;
            GM_ST(void *, (uint8_t *)&t301, (void *)(((uint8_t *)t300 + (uintptr_t)0 * 40)));
            GM_ST(uintptr_t, (uint8_t *)&t301 + 8, (uintptr_t)((uintptr_t)1 - (uintptr_t)0));
            gm_b16_8 t302 = t301;
            gm_t88rcucg1b7uk t303 = gm_f_adt_luat93d((void *)NULL, cx, NULL, t295, t296, t302);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l27 = t303;
        }
        {
            void * t304 = gm_l0;
            void * t305 = (void *)(((uint8_t *)t304) + 2376);
            gm_tcu60n24kv26r t306;
            memset((uint8_t *)&t306 + 4, 0, 4);
            memset((uint8_t *)&t306 + 25, 0, 3);
            gm_t88rcucg1b7uk t307 = gm_l1;
            GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t306 + 0, t307);
            gm_tf5srh4mp4b3i t308 = gm_l2;
            GM_ST(gm_tf5srh4mp4b3i, (uint8_t *)&t306 + 8, t308);
            uint8_t t309 = gm_l5;
            GM_ST(uint8_t, (uint8_t *)&t306 + 24, t309);
            gm_t88rcucg1b7uk t310 = gm_l27;
            GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t306 + 28, t310);
            gm_tftb1odc739t5 t311 = gm_f_push_mf0vgpj(t305, &t306);
            gm_tftb1odc739t5 t312 = t311;
            if (((((uint8_t *)&t312))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t312) + 1, gm_src4nj79mj9jk57, 132, 5);
            gm_b0_1 t313 = GM_LD(gm_b0_1, (((uint8_t *)&t312) + 1));
        }
        {
            gm_t88rcucg1b7uk t314 = gm_l27;
            gm_te3magq8drarp t315;
            GM_ST(uint8_t, (uint8_t *)&t315 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t315 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t315 + 4, 0);
            GM_ST(uint32_t, (uint8_t *)&t315 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t315 + 16, 0);
            GM_ST(uint8_t, ((uint8_t *)&t315 + 0), ((uint8_t)0ULL));
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t315 + 8), t314);
            uint8_t t316 = 0;
            F.ret = t315;
            goto gm_fret;
        }
        goto ix53;
ie53:;
        {
            gm_b16_8 t318 = gm_l6;
            gm_b16_8 t319 = t318;
            gm_b16_8 t320 = GM_LD(gm_b16_8, ((uint8_t *)&t319));
            gm_te3magq8drarp t321;
            GM_ST(uint8_t, (uint8_t *)&t321 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t321 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t321 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t321 + 0), ((uint8_t)1ULL));
            GM_ST(gm_b16_8, ((uint8_t *)&t321 + 8), t320);
            uint8_t t322 = 0;
            F.ret = t321;
            goto gm_fret;
        }
ix53:;
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_subst_soa_2chqte3(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, const void * a2) {
    struct gm_fr_subst_soa_2chqte3 F;
    void * gm_l0; /* s */
    gm_t88rcucg1b7uk gm_l1; /* elem */
    gm_th3eueiipivq6 gm_l2; /* shape */
    gm_t88rcucg1b7uk gm_l3; /* t */
    gm_b16_8 gm_l4; /* msg */
    GM_STACK_CHECK(&F, 225u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        gm_t88rcucg1b7uk t3 = gm_l1;
        gm_tf5srh4mp4b3i t4 = GM_LD(gm_tf5srh4mp4b3i, (((uint8_t *)a2) + 0));
        uint8_t t5 = GM_LD(uint8_t, (((uint8_t *)a2) + 16));
        gm_te3magq8drarp t6;
        gm_f_soa_type_of_q8vtd5j((void *)&t6, (void *)NULL, cx, NULL, t2, t3, t4, t5);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_te3magq8drarp t7 = t6;
        uint8_t t10 = GM_LD(uint8_t, (((uint8_t *)&t7) + 0));
        if (t10 != ((uint8_t)0ULL)) goto mn9;
        gm_t88rcucg1b7uk t11 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t7) + 8));
        gm_l3 = t11;
        gm_t88rcucg1b7uk t12 = gm_l3;
        uint8_t t13 = 0;
        F.ret = t12;
        goto gm_fret;
        goto me8;
mn9:;
        uint8_t t15 = GM_LD(uint8_t, (((uint8_t *)&t7) + 0));
        if (t15 != ((uint8_t)1ULL)) goto mn14;
        gm_b16_8 t16 = GM_LD(gm_b16_8, (((uint8_t *)&t7) + 8));
        gm_l4 = t16;
        {
            void * t18 = gm_l0;
            void * t19 = (void *)(((uint8_t *)t18) + 2456);
            gm_b16_8 t20 = gm_l4;
            gm_tftb1odc739t5 t21 = gm_f_push_3ef746b(t19, t20);
            gm_tftb1odc739t5 t22 = t21;
            if (((((uint8_t *)&t22))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t22) + 1, gm_src4nj79mj9jk57, 182, 13);
            gm_b0_1 t23 = GM_LD(gm_b0_1, (((uint8_t *)&t22) + 1));
        }
        {
            void * t24 = gm_l0;
            void * t25 = (void *)(((uint8_t *)t24) + 8);
            gm_t88rcucg1b7uk t26 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t25);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t27 = 0;
            F.ret = t26;
            goto gm_fret;
        }
        goto me8;
mn14:;
        gm_trap("no match arm matched the scrutinee", gm_src4nj79mj9jk57, 179, 5);
me8:;
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
extern __attribute__((aligned(4))) void * gm_f_usize_lit_a8hojea(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_tl874t2atnlh3 a2);
