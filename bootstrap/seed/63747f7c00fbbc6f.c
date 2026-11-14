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
typedef struct { uint64_t f0; uint64_t f1; uint64_t f2; uint64_t f3; } gm_t66j1im9ip128;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint32_t f0; uint8_t f1; uint8_t pe[3]; } gm_t9805qtonbsag;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { void * f0; uint32_t f1; uint8_t pe[4]; } gm_tbl8il92l97ut;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint8_t f0; uint8_t p1[1]; uint16_t f1; } gm_tjcj4j2gbh6c8;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { uint64_t f0; uint32_t f1; uint8_t pe[4]; } gm_tn68el8d14vku;
_Static_assert(sizeof(gm_csguankko4lmhd) == 4 && _Alignof(gm_csguankko4lmhd) == 4 && offsetof(gm_csguankko4lmhd, f0) == 0, "layout of `Mutex`");
_Static_assert(sizeof(gm_t2lqu8jsj511d) == 16 && _Alignof(gm_t2lqu8jsj511d) == 8 && offsetof(gm_t2lqu8jsj511d, f0) == 0 && offsetof(gm_t2lqu8jsj511d, f1) == 8, "layout of `(u64, u64)`");
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { gm_tbl8il92l97ut f0; uint8_t f1; uint8_t pe[7]; } gm_t5lf9i3n1r3co;
typedef struct { uint64_t f0; gm_t9qkh0rsooc8d f1; uint8_t pe[7]; } gm_t617vr72bd7b3;
_Static_assert(sizeof(gm_t66j1im9ip128) == 32 && _Alignof(gm_t66j1im9ip128) == 8 && offsetof(gm_t66j1im9ip128, f0) == 0 && offsetof(gm_t66j1im9ip128, f1) == 8 && offsetof(gm_t66j1im9ip128, f2) == 16 && offsetof(gm_t66j1im9ip128, f3) == 24, "layout of `(types: usize, values: usize, fns: usize, consts: usize)`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tn68el8d14vku v0; gm_tn68el8d14vku v1; gm_tf5srh4mp4b3i v2; uint8_t v3; void * v4; uint32_t v5; } f1; } gm_t76123gfet73g;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
_Static_assert(sizeof(gm_t9805qtonbsag) == 8 && _Alignof(gm_t9805qtonbsag) == 4 && offsetof(gm_t9805qtonbsag, f0) == 0 && offsetof(gm_t9805qtonbsag, f1) == 4, "layout of `?u32`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
typedef struct { gm_b16_8 f0; uint32_t f1; uint32_t f2; gm_b16_8 f3; } gm_tb40qn7hk0tl9;
typedef struct { uint64_t f0; uint64_t f1; gm_b16_8 f2; } gm_tbccvikg6b96e;
_Static_assert(sizeof(gm_tbl8il92l97ut) == 16 && _Alignof(gm_tbl8il92l97ut) == 8 && offsetof(gm_tbl8il92l97ut, f0) == 0 && offsetof(gm_tbl8il92l97ut, f1) == 8, "layout of `SpecCall`");
typedef struct { uint8_t f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; } gm_tbngattl1fo51;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tcmkompbl09il;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; gm_t88rcucg1b7uk f2; } gm_tcmtrmhj7ak4u;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; } gm_tgglga8ro3isc;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
_Static_assert(sizeof(gm_tjcj4j2gbh6c8) == 4 && _Alignof(gm_tjcj4j2gbh6c8) == 2 && offsetof(gm_tjcj4j2gbh6c8, f0) == 0 && offsetof(gm_tjcj4j2gbh6c8, f1) == 2, "layout of `(signed: bool, bits: u16)`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t f1; uint8_t pe[3]; } gm_tkn42u7na3t81;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_tn68el8d14vku) == 16 && _Alignof(gm_tn68el8d14vku) == 8 && offsetof(gm_tn68el8d14vku, f0) == 0 && offsetof(gm_tn68el8d14vku, f1) == 8, "layout of `(i: usize, x: u32)`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t88rcucg1b7uk v0; } f1; } gm_tnnjeerolos0a;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; void * v1; } f1; } gm_tq7s98ihonkju;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t f2; uint8_t p3[3]; gm_tl874t2atnlh3 f3; uint8_t pe[4]; } gm_tsoie1ppgdvcg;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tcmkompbl09il v0; gm_tiilmfd16v3fc v1; gm_b16_8 v2; } f1; } gm_t428ue2u3hf5g;
typedef struct { uint64_t f0; gm_tsoie1ppgdvcg f1; } gm_t5im2v50m3hk6;
_Static_assert(sizeof(gm_t5lf9i3n1r3co) == 24 && _Alignof(gm_t5lf9i3n1r3co) == 8 && offsetof(gm_t5lf9i3n1r3co, f0) == 0 && offsetof(gm_t5lf9i3n1r3co, f1) == 16, "layout of `?SpecCall`");
_Static_assert(sizeof(gm_t617vr72bd7b3) == 16 && _Alignof(gm_t617vr72bd7b3) == 8 && offsetof(gm_t617vr72bd7b3, f0) == 0 && offsetof(gm_t617vr72bd7b3, f1) == 8, "layout of `DirEntry`");
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_t76123gfet73g) == 24 && _Alignof(gm_t76123gfet73g) == 8 && offsetof(gm_t76123gfet73g, f0) == 0 && offsetof(gm_t76123gfet73g, f1) == 8, "layout of `Flow`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t76123gfet73g v0; uint64_t v1; } f1; } gm_t833u93mr4iob;
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; gm_tnnjeerolos0a f2; uint8_t pe[4]; } gm_t8nckeqfenl8a;
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
_Static_assert(sizeof(gm_tb40qn7hk0tl9) == 40 && _Alignof(gm_tb40qn7hk0tl9) == 8 && offsetof(gm_tb40qn7hk0tl9, f0) == 0 && offsetof(gm_tb40qn7hk0tl9, f1) == 16 && offsetof(gm_tb40qn7hk0tl9, f2) == 20 && offsetof(gm_tb40qn7hk0tl9, f3) == 24, "layout of `SourceLocation`");
_Static_assert(sizeof(gm_tbccvikg6b96e) == 32 && _Alignof(gm_tbccvikg6b96e) == 8 && offsetof(gm_tbccvikg6b96e, f0) == 0 && offsetof(gm_tbccvikg6b96e, f1) == 8 && offsetof(gm_tbccvikg6b96e, f2) == 16, "layout of `Layout`");
_Static_assert(sizeof(gm_tbngattl1fo51) == 8 && _Alignof(gm_tbngattl1fo51) == 4 && offsetof(gm_tbngattl1fo51, f0) == 0 && offsetof(gm_tbngattl1fo51, f1) == 4, "layout of `(is_const: bool, pointee: Ty)`");
_Static_assert(sizeof(gm_tcmkompbl09il) == 24 && _Alignof(gm_tcmkompbl09il) == 8 && offsetof(gm_tcmkompbl09il, f0) == 0 && offsetof(gm_tcmkompbl09il, f1) == 8, "layout of `(ty: Ty, bytes: string)`");
_Static_assert(sizeof(gm_tcmtrmhj7ak4u) == 12 && _Alignof(gm_tcmtrmhj7ak4u) == 4 && offsetof(gm_tcmtrmhj7ak4u, f0) == 0 && offsetof(gm_tcmtrmhj7ak4u, f1) == 4 && offsetof(gm_tcmtrmhj7ak4u, f2) == 8, "layout of `(op: LenOp, lhs: Ty, rhs: Ty)`");
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t p2[4]; gm_tnpifv20irnph f2; uint8_t f3; uint8_t pe[7]; } gm_tddtjfggdat0d;
typedef struct { void * f0; gm_b16_8 f1; uint8_t f2; uint8_t p3[7]; gm_tf5srh4mp4b3i f3; gm_tkn42u7na3t81 f4; void * f5; uint64_t f6; gm_b16_8 f7; gm_b16_8 f8; gm_b16_8 f9; gm_b16_8 f10; uint64_t f11; uint64_t f12; } gm_tfe0d7m5kus9m;
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_tgglga8ro3isc) == 8 && _Alignof(gm_tgglga8ro3isc) == 4 && offsetof(gm_tgglga8ro3isc, f0) == 0 && offsetof(gm_tgglga8ro3isc, f1) == 4, "layout of `(err: Ty, ok: Ty)`");
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
typedef struct { uint64_t f0; uint32_t f1; uint32_t f2; void * f3; gm_b16_8 f4; void * f5; gm_b16_8 f6; gm_b16_8 f7; gm_b16_8 f8; gm_trqfvmjtt4sij f9; gm_trqfvmjtt4sij f10; gm_trqfvmjtt4sij f11; gm_t5lf9i3n1r3co f12; uint8_t f13; uint8_t p14[7]; void * f14; } gm_tkej6osnbjrnq;
_Static_assert(sizeof(gm_tkn42u7na3t81) == 8 && _Alignof(gm_tkn42u7na3t81) == 4 && offsetof(gm_tkn42u7na3t81, f0) == 0 && offsetof(gm_tkn42u7na3t81, f1) == 4, "layout of `?Ty`");
_Static_assert(sizeof(gm_tnnjeerolos0a) == 8 && _Alignof(gm_tnnjeerolos0a) == 4 && offsetof(gm_tnnjeerolos0a, f0) == 0 && offsetof(gm_tnnjeerolos0a, f1) == 4, "layout of `ArrayKind`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t2lqu8jsj511d v0; gm_t617vr72bd7b3 v1; gm_t66j1im9ip128 v2; gm_t2lqu8jsj511d v3; uint64_t v4; gm_t617vr72bd7b3 v5; } f1; } gm_tpekhfs32op0f;
_Static_assert(sizeof(gm_tq7s98ihonkju) == 16 && _Alignof(gm_tq7s98ihonkju) == 8 && offsetof(gm_tq7s98ihonkju, f0) == 0 && offsetof(gm_tq7s98ihonkju, f1) == 8, "layout of `AllocError!^HandlerDecl`");
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_tsoie1ppgdvcg) == 40 && _Alignof(gm_tsoie1ppgdvcg) == 8 && offsetof(gm_tsoie1ppgdvcg, f0) == 0 && offsetof(gm_tsoie1ppgdvcg, f1) == 16 && offsetof(gm_tsoie1ppgdvcg, f2) == 20 && offsetof(gm_tsoie1ppgdvcg, f3) == 24, "layout of `LocalDecl`");
_Static_assert(sizeof(gm_t428ue2u3hf5g) == 32 && _Alignof(gm_t428ue2u3hf5g) == 8 && offsetof(gm_t428ue2u3hf5g, f0) == 0 && offsetof(gm_t428ue2u3hf5g, f1) == 8, "layout of `ConstKey`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tjcj4j2gbh6c8 v0; uint16_t v1; gm_tbngattl1fo51 v2; gm_tbngattl1fo51 v3; gm_tbngattl1fo51 v4; gm_t8nckeqfenl8a v5; gm_t6j6smj7qoctu v6; gm_t88rcucg1b7uk v7; gm_tgglga8ro3isc v8; gm_tddtjfggdat0d v9; gm_tddtjfggdat0d v10; gm_tiilmfd16v3fc v11; gm_t88rcucg1b7uk v12; uint32_t v13; gm_tgglga8ro3isc v14; gm_t428ue2u3hf5g v15; gm_tcmtrmhj7ak4u v16; uint32_t v17; gm_tnpifv20irnph v18; uint32_t v19; } f1; } gm_t53at80pfdus3;
_Static_assert(sizeof(gm_t5im2v50m3hk6) == 48 && _Alignof(gm_t5im2v50m3hk6) == 8 && offsetof(gm_t5im2v50m3hk6, f0) == 0 && offsetof(gm_t5im2v50m3hk6, f1) == 8, "layout of `(i: usize, x: LocalDecl)`");
_Static_assert(sizeof(gm_t833u93mr4iob) == 32 && _Alignof(gm_t833u93mr4iob) == 8 && offsetof(gm_t833u93mr4iob, f0) == 0 && offsetof(gm_t833u93mr4iob, f1) == 8, "layout of `Flow!u64`");
_Static_assert(sizeof(gm_t8nckeqfenl8a) == 24 && _Alignof(gm_t8nckeqfenl8a) == 8 && offsetof(gm_t8nckeqfenl8a, f0) == 0 && offsetof(gm_t8nckeqfenl8a, f1) == 8 && offsetof(gm_t8nckeqfenl8a, f2) == 12, "layout of `(len: u64, elem: Ty, kind: ArrayKind)`");
_Static_assert(sizeof(gm_tddtjfggdat0d) == 48 && _Alignof(gm_tddtjfggdat0d) == 8 && offsetof(gm_tddtjfggdat0d, f0) == 0 && offsetof(gm_tddtjfggdat0d, f1) == 16 && offsetof(gm_tddtjfggdat0d, f2) == 24 && offsetof(gm_tddtjfggdat0d, f3) == 40, "layout of `FnTy`");
_Static_assert(sizeof(gm_tfe0d7m5kus9m) == 152 && _Alignof(gm_tfe0d7m5kus9m) == 8 && offsetof(gm_tfe0d7m5kus9m, f0) == 0 && offsetof(gm_tfe0d7m5kus9m, f1) == 8 && offsetof(gm_tfe0d7m5kus9m, f2) == 24 && offsetof(gm_tfe0d7m5kus9m, f3) == 32 && offsetof(gm_tfe0d7m5kus9m, f4) == 48 && offsetof(gm_tfe0d7m5kus9m, f5) == 56 && offsetof(gm_tfe0d7m5kus9m, f6) == 64 && offsetof(gm_tfe0d7m5kus9m, f7) == 72 && offsetof(gm_tfe0d7m5kus9m, f8) == 88 && offsetof(gm_tfe0d7m5kus9m, f9) == 104 && offsetof(gm_tfe0d7m5kus9m, f10) == 120 && offsetof(gm_tfe0d7m5kus9m, f11) == 136 && offsetof(gm_tfe0d7m5kus9m, f12) == 144, "layout of `FnInfo`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tpekhfs32op0f v0; } f1; } gm_tgfdnufetdnfc;
_Static_assert(sizeof(gm_tkej6osnbjrnq) == 256 && _Alignof(gm_tkej6osnbjrnq) == 8 && offsetof(gm_tkej6osnbjrnq, f0) == 0 && offsetof(gm_tkej6osnbjrnq, f1) == 8 && offsetof(gm_tkej6osnbjrnq, f2) == 12 && offsetof(gm_tkej6osnbjrnq, f3) == 16 && offsetof(gm_tkej6osnbjrnq, f4) == 24 && offsetof(gm_tkej6osnbjrnq, f5) == 40 && offsetof(gm_tkej6osnbjrnq, f6) == 48 && offsetof(gm_tkej6osnbjrnq, f7) == 64 && offsetof(gm_tkej6osnbjrnq, f8) == 80 && offsetof(gm_tkej6osnbjrnq, f9) == 96 && offsetof(gm_tkej6osnbjrnq, f10) == 136 && offsetof(gm_tkej6osnbjrnq, f11) == 176 && offsetof(gm_tkej6osnbjrnq, f12) == 216 && offsetof(gm_tkej6osnbjrnq, f13) == 240 && offsetof(gm_tkej6osnbjrnq, f14) == 248, "layout of `Frame`");
_Static_assert(sizeof(gm_tpekhfs32op0f) == 40 && _Alignof(gm_tpekhfs32op0f) == 8 && offsetof(gm_tpekhfs32op0f, f0) == 0 && offsetof(gm_tpekhfs32op0f, f1) == 8, "layout of `MemError`");
_Static_assert(sizeof(gm_t53at80pfdus3) == 56 && _Alignof(gm_t53at80pfdus3) == 8 && offsetof(gm_t53at80pfdus3, f0) == 0 && offsetof(gm_t53at80pfdus3, f1) == 8, "layout of `TyKind`");
_Static_assert(sizeof(gm_tgfdnufetdnfc) == 48 && _Alignof(gm_tgfdnufetdnfc) == 8 && offsetof(gm_tgfdnufetdnfc, f0) == 0 && offsetof(gm_tgfdnufetdnfc, f1) == 8, "layout of `MemError!()`");
struct gm_fr_align_15fjgq1 {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_align_of_k6eqqrn {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_alloc_frame_31ks03n {
    gm_frame h;
    void * ret;
};
struct gm_fr_clear_1tf0nio {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_clear_6i2ni9k {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_clear_rqatj1i {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_destroy_ca2nrm8 {
    gm_frame h;
    gm_tb40qn7hk0tl9 l1; /* site */
    gm_b0_1 ret;
};
struct gm_fr_empty_slice_84b0b46 {
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
struct gm_fr_empty_slice_s1lbe5s {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_end_frame_cetr4jh {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_end_frame_slow_mnbcrps {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fill_kd2qt72 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_addr_odbbf6a {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_fn_from_addr_7a6bpqd {
    gm_frame h;
    gm_t9805qtonbsag ret;
};
struct gm_fr_fn_info_hocist4 {
    gm_frame h;
    void * ret;
};
struct gm_fr_frame_handle_k12gn88 {
    gm_frame h;
    uint32_t ret;
};
struct gm_fr_frame_of_dgl92on {
    gm_frame h;
    void * ret;
};
struct gm_fr_frame_up_ghkkuqg {
    gm_frame h;
    void * ret;
};
struct gm_fr_func_ene90ss {
    gm_frame h;
    void * ret;
};
struct gm_fr_grow_mem_buf_930r21a {
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
struct gm_fr_list_ib5k4gb_2 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_k2ij3i7 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_kl0t9m0 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_s83jn2q {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_local_addr_o51qq3k {
    gm_frame h;
    gm_t833u93mr4iob ret;
};
struct gm_fr_local_alloc_qm9q4fo {
    gm_frame h;
    gm_t833u93mr4iob ret;
};
struct gm_fr_lower_body_g4bo546 {
    gm_frame h;
    void * ret;
};
struct gm_fr_make_fn_info_4dk91jl {
    gm_frame h;
    void * ret;
};
struct gm_fr_make_info_aau8il9 {
    gm_frame h;
    gm_trqfvmjtt4sij l4; /* offs */
    gm_trqfvmjtt4sij l5; /* sizes */
    gm_trqfvmjtt4sij l6; /* aligns */
    gm_trqfvmjtt4sij l7; /* addressed */
    gm_tsoie1ppgdvcg l15; /* l */
    void * ret;
};
struct gm_fr_new_frame_vgm56mt {
    gm_frame h;
    void * ret;
};
struct gm_fr_offsets_qpt7c30 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_pop_570g79n {
    gm_frame h;
    gm_t9805qtonbsag ret;
};
struct gm_fr_us_cuv01fp {
    gm_frame h;
    uint64_t ret;
};
inline __attribute__((aligned(4))) uint64_t gm_f_align_15fjgq1(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) uint64_t gm_f_align_of_k6eqqrn(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) __attribute__((noinline)) void * gm_f_alloc_frame_31ks03n(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_alloc_mem_0vu6kcs(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, uint64_t a2, uint64_t a3, gm_t9qkh0rsooc8d a4, uint8_t a5);
inline __attribute__((aligned(4))) void gm_f_clear_1tf0nio(void * a0);
inline __attribute__((aligned(4))) void gm_f_clear_6i2ni9k(void * a0);
inline __attribute__((aligned(4))) void gm_f_clear_rqatj1i(void * a0);
__attribute__((aligned(4))) void gm_f_deinit_6u0ld99(void * a0);
__attribute__((aligned(4))) void gm_f_deinit_lb56u7s(void * a0);
__attribute__((aligned(4))) void gm_f_deinit_v2ik7pl(void * a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_destroy_ca2nrm8(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_84b0b46(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_d3uqusp_2(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_lopbqdt(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_s1lbe5s(void);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_b0_1 gm_f_end_frame_cetr4jh(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) __attribute__((noinline)) gm_b0_1 gm_f_end_frame_slow_mnbcrps(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) void * gm_f_expr_a84t331(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_fill_kd2qt72(gm_b16_8 a0, uint64_t a1);
inline __attribute__((aligned(4))) uint64_t gm_f_fn_addr_odbbf6a(void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_t9805qtonbsag gm_f_fn_from_addr_7a6bpqd(void * a0, uint64_t a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void * gm_f_fn_info_hocist4(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
__attribute__((aligned(4))) uint64_t gm_f_frame_cost_oei64tv(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, void * a2);
__attribute__((aligned(4))) uint32_t gm_f_frame_handle_k12gn88(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) void * gm_f_frame_of_dgl92on(void * a0, uint64_t a1);
__attribute__((aligned(4))) void * gm_f_frame_up_ghkkuqg(void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_free_l0kjo6g(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, const void * a1);
inline __attribute__((aligned(4))) void * gm_f_func_ene90ss(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
__attribute__((aligned(4))) void * gm_f_func_in17r5n(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
__attribute__((aligned(4))) __attribute__((noinline)) gm_b0_1 gm_f_grow_mem_buf_930r21a(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint64_t a2);
__attribute__((aligned(4))) void gm_f_init_9fuf99v(void *gm_out, void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) uint8_t gm_f_kill_3kcavfd(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
inline __attribute__((aligned(4))) void gm_f_kind_htv6093(void *gm_out, void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void gm_f_kind_jcnt4hm(void *gm_out, void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void gm_f_layout_a5ovnrm(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) void gm_f_layout_of_e6hfj1r(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) void gm_f_list_ib5k4gb_2(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_k2ij3i7(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_kl0t9m0(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_s83jn2q(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_local_addr_o51qq3k(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, void * a2, uint32_t a3);
__attribute__((aligned(4))) __attribute__((noinline)) void gm_f_local_alloc_qm9q4fo(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, void * a2, uint32_t a3);
inline __attribute__((aligned(4))) void * gm_f_lower_body_g4bo546(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2);
__attribute__((aligned(4))) void gm_f_make_fill_e6rv4na(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint64_t a0, uint64_t a1, const void * a2);
__attribute__((aligned(4))) __attribute__((noinline)) void * gm_f_make_fn_info_4dk91jl(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
__attribute__((aligned(4))) void * gm_f_make_info_aau8il9(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_b16_8 a2, void * a3);
__attribute__((aligned(4))) void gm_f_mark_addressed_qtnunkg(void * a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_mark_captured_addressed_tmp4jtp(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, uint32_t a2, gm_b16_8 a3);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void * gm_f_new_frame_vgm56mt(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, void * a2, gm_b16_8 a3, void * a4, gm_b16_8 a5);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_ifv16tr_2(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_r6kqujv(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_offsets_qpt7c30(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_t9805qtonbsag gm_f_pop_570g79n(void * a0);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_7o6oubd(void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_ghpucvc(void * a0, void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_im9d4ro(void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_se7jhqh(void * a0, void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_vd1290p(void * a0, uint8_t a1);
__attribute__((aligned(4))) uint64_t gm_f_size_hr4b76n(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline uint64_t gm_f_us_cuv01fp(uint64_t a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_vm_assemble_k5irtcr(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2);
__attribute__((aligned(4))) void * gm_f_vm_compile_gef97rj(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2);
extern uint8_t gm_cjdu4hivd9h[16] __attribute__((aligned(8)));
extern uint8_t gm_cmiriali67r[8] __attribute__((aligned(8)));
extern uint8_t gm_k68ol137ncd[40] __attribute__((aligned(8)));
extern uint8_t gm_k7bkj4i7gae[40] __attribute__((aligned(8)));
extern uint8_t gm_kk6gotfp7pf[40] __attribute__((aligned(8)));
extern uint8_t gm_kl5pit792cd[40] __attribute__((aligned(8)));
extern uint8_t gm_ksoekcu07mu[40] __attribute__((aligned(8)));
extern uint8_t gm_ku7k9fgdnj6[40] __attribute__((aligned(8)));
extern const char gm_src0ih77f43mhf2[];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_src6hq50r4rk7e6[];
extern const char gm_srcc24pnihhgkug[];
extern const char gm_srcrgc9bkquf6nr[];
extern const gm_type gm_tyo86ks9qkvt;
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
    GM_STACK_CHECK(&F, 120u);
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
    gm_tbccvikg6b96e t5 = t4;
    uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)&t5) + 8));
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
inline __attribute__((aligned(4))) void gm_f_clear_1tf0nio(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_clear_1tf0nio F;
    void * gm_l0; /* l */
    GM_STACK_CHECK_SHALLOW(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        void * t3 = gm_l0;
        uint64_t t4 = GM_LD(uintptr_t, (((uint8_t *)t3) + 0) + 8);
        void * t5 = ((uint8_t *)GM_LD(void *, (((uint8_t *)t3) + 0)));
        uint64_t t6 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t7 = (uintptr_t)((uint64_t)0ULL);
        if (t6 > t7 || t7 > (uintptr_t)t4) gm_trap_slice(t6, t7, t4, gm_srcrgc9bkquf6nr, 137, 15);
        gm_b16_8 t8;
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 8)));
        GM_ST(uintptr_t, (uint8_t *)&t8 + 8, (uintptr_t)(t7 - t6));
        GM_ST(gm_b16_8, (((uint8_t *)t2) + 0), t8);
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_clear_6i2ni9k(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_clear_6i2ni9k F;
    void * gm_l0; /* l */
    GM_STACK_CHECK_SHALLOW(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        void * t3 = gm_l0;
        uint64_t t4 = GM_LD(uintptr_t, (((uint8_t *)t3) + 0) + 8);
        void * t5 = ((uint8_t *)GM_LD(void *, (((uint8_t *)t3) + 0)));
        uint64_t t6 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t7 = (uintptr_t)((uint64_t)0ULL);
        if (t6 > t7 || t7 > (uintptr_t)t4) gm_trap_slice(t6, t7, t4, gm_srcrgc9bkquf6nr, 137, 15);
        gm_b16_8 t8;
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 24)));
        GM_ST(uintptr_t, (uint8_t *)&t8 + 8, (uintptr_t)(t7 - t6));
        GM_ST(gm_b16_8, (((uint8_t *)t2) + 0), t8);
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_clear_rqatj1i(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_clear_rqatj1i F;
    void * gm_l0; /* l */
    GM_STACK_CHECK_SHALLOW(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        void * t3 = gm_l0;
        uint64_t t4 = GM_LD(uintptr_t, (((uint8_t *)t3) + 0) + 8);
        void * t5 = ((uint8_t *)GM_LD(void *, (((uint8_t *)t3) + 0)));
        uint64_t t6 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t7 = (uintptr_t)((uint64_t)0ULL);
        if (t6 > t7 || t7 > (uintptr_t)t4) gm_trap_slice(t6, t7, t4, gm_srcrgc9bkquf6nr, 137, 15);
        gm_b16_8 t8;
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 24)));
        GM_ST(uintptr_t, (uint8_t *)&t8 + 8, (uintptr_t)(t7 - t6));
        GM_ST(gm_b16_8, (((uint8_t *)t2) + 0), t8);
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_destroy_ca2nrm8(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_destroy_ca2nrm8 F;
    void * gm_l0; /* p */
    gm_t3mf58gv9kf3n gm_l2; /* a */
    GM_STACK_CHECK(&F, 88u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_node *pn2 = gm_find(cx, 587034433);
        if (!pn2) gm_trap("no handler installed for effect `Alloc`", gm_src2ah81i3aa8hm, 708, 10);
        gm_t3mf58gv9kf3n t3;
        t3 = ((gm_t3mf58gv9kf3n (*)(void *, gm_node *, gm_node *))pn2->clauses[0])(pn2->env, pn2->hctx, pn2);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l2 = t3;
    }
    {
    }
    {
        void * t4 = GM_LD(void *, (((uint8_t *)&gm_l2) + 8));
        void * t5 = GM_LD(void *, (((uint8_t *)t4) + 16));
        void * t6 = GM_LD(void *, (((uint8_t *)&gm_l2) + 0));
        void * t7 = gm_l0;
        void * t8 = t7;
        void * t9 = t5;
        gm_b0_1 t10;
        ((void (*)(void *, void *, uint64_t, uint64_t))GM_PTR_UNTAG(t9))(t6, t8, ((uint64_t)256ULL), ((uint64_t)8ULL));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_84b0b46(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_84b0b46 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_d3uqusp_2(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_d3uqusp_2 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_lopbqdt(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_lopbqdt F;
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_b0_1 gm_f_end_frame_cetr4jh(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_end_frame_cetr4jh F;
    void * gm_l0; /* m */
    void * gm_l1; /* fr */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint8_t t3 = 0;
        uint8_t t4 = 0;
        uint8_t t5 = 0;
        uint8_t t6 = 0;
        void * t7 = gm_l1;
        uint64_t t8 = GM_LD(uintptr_t, ((((uint8_t *)t7) + 96) + 0) + 8);
        uint8_t t9 = (t8 > ((uint64_t)0ULL));
        if (!t9) {
            void * t10 = gm_l1;
            uint64_t t11 = GM_LD(uintptr_t, ((((uint8_t *)t10) + 136) + 0) + 8);
            uint8_t t12 = (t11 > ((uint64_t)0ULL));
            t6 = t12;
        } else t6 = 1;
        if (!t6) {
            void * t13 = gm_l1;
            uint64_t t14 = GM_LD(uintptr_t, ((((uint8_t *)t13) + 176) + 0) + 8);
            uint8_t t15 = (t14 > ((uint64_t)0ULL));
            t5 = t15;
        } else t5 = 1;
        if (!t5) {
            void * t16 = gm_l1;
            uint32_t t17 = GM_LD(uint32_t, (((uint8_t *)t16) + 8));
            uint8_t t18 = (!(t17 == ((uint32_t)0ULL)));
            t4 = t18;
        } else t4 = 1;
        if (!t4) {
            void * t19 = gm_l0;
            uint32_t t20 = GM_LD(uint32_t, (((uint8_t *)t19) + 544));
            uint8_t t21 = (t20 >= ((uint32_t)256ULL));
            t3 = t21;
        } else t3 = 1;
        if (!t3) goto ie2;
        {
            void * t23 = gm_l0;
            void * t24 = gm_l1;
            gm_b0_1 t25 = gm_f_end_frame_slow_mnbcrps((void *)NULL, cx, NULL, t23, t24);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_b0_1 t26;
            uint8_t t27 = 0;
            F.ret = t26;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t28 = gm_l1;
        void * t29 = gm_l0;
        void * t30 = GM_LD(void *, (((uint8_t *)t29) + 536));
        GM_ST(void *, (((uint8_t *)t28) + 248), t30);
    }
    {
        void * t31 = gm_l0;
        void * t32 = gm_l1;
        GM_ST(void *, (((uint8_t *)t31) + 536), t32);
    }
    {
        void * t33 = gm_l0;
        void * t34 = gm_l0;
        uint32_t t35 = GM_LD(uint32_t, (((uint8_t *)t34) + 544));
        uint32_t r36; bool o37 = __builtin_add_overflow(t35, ((uint32_t)1ULL), &r36);
        if (o37) gm_trap("integer overflow in `+`", gm_src6hq50r4rk7e6, 152, 5);
        uint32_t t38 = r36;
        GM_ST(uint32_t, (((uint8_t *)t33) + 544), t38);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_fill_kd2qt72(gm_b16_8 a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_fill_kd2qt72 F;
    gm_b16_8 gm_l0; /* s */
    uint64_t gm_l1; /* x */
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
                    uint64_t t12 = t11;
                    uint64_t t15 = GM_LD(uint64_t, ((uint8_t *)&t12));
                    gm_l5 = t15;
                    {
                        uint64_t t17 = gm_l5;
                        uint64_t t18 = (uintptr_t)t17;
                        void * t19 = gm_l2;
                        uint64_t t20 = gm_l1;
                        GM_ST(uint64_t, ((uint8_t *)t19 + t18 * 8), t20);
                    }
                    goto me13;
mn14:;
                    gm_trap("no match arm matched the scrutinee", gm_srcc24pnihhgkug, 18, 5);
me13:;
                }
lc5:;
                {
                    uint64_t t22 = gm_l4;
                    uint64_t r23; bool o24 = __builtin_add_overflow(t22, ((uint64_t)1ULL), &r23);
                    uint64_t t25 = r23;
                    gm_l4 = t25;
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
inline __attribute__((aligned(4))) uint64_t gm_f_fn_addr_odbbf6a(void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_fn_addr_odbbf6a F;
    void * gm_l0; /* m */
    uint32_t gm_l1; /* f */
    GM_STACK_CHECK_SHALLOW(&F, 64u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint64_t t1 = GM_LD(uint64_t, gm_cmiriali67r);
    uint32_t t2 = gm_l1;
    uint64_t t3 = (uint64_t)t2;
    uint64_t r4; bool o5 = __builtin_mul_overflow(((uint64_t)16ULL), t3, &r4);
    if (o5) gm_trap("integer overflow in `*`", gm_src6hq50r4rk7e6, 5, 64);
    uint64_t t6 = r4;
    uint64_t r7; bool o8 = __builtin_add_overflow(t1, t6, &r7);
    if (o8) gm_trap("integer overflow in `+`", gm_src6hq50r4rk7e6, 5, 52);
    uint64_t t9 = r7;
    F.ret = t9;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline void * gm_f_fn_info_hocist4(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1) {
    struct gm_fr_fn_info_hocist4 F;
    void * gm_l0; /* m */
    uint32_t gm_l1; /* f */
    uint64_t gm_l2; /* i */
    void * gm_l3; /* x */
    GM_STACK_CHECK(&F, 175u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint32_t t2 = gm_l1;
        uint64_t t3 = (uint64_t)t2;
        uint64_t t4 = gm_f_us_cuv01fp(t3);
        gm_l2 = t4;
    }
    {
        uint64_t t6 = gm_l2;
        void * t7 = gm_l0;
        uint64_t t8 = GM_LD(uintptr_t, ((((uint8_t *)t7) + 720) + 0) + 8);
        uint8_t t9 = (t6 < t8);
        if (!t9) goto ie5;
        {
            uint64_t t12 = gm_l2;
            uint64_t t13 = (uintptr_t)t12;
            void * t14 = gm_l0;
            uint64_t t15 = GM_LD(uintptr_t, ((((uint8_t *)t14) + 720) + 0) + 8);
            if (t13 >= (uintptr_t)(t15)) gm_trap_index(t13, t15, gm_src6hq50r4rk7e6, 19, 17);
            void * t16 = GM_LD(void *, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t14) + 720) + 0)) + t13 * 8));
            void * t17 = t16;
            if (!(GM_LD(void *, ((uint8_t *)&t17)) != 0)) goto ie11;
            void * t18 = GM_LD(void *, ((uint8_t *)&t17));
            gm_l3 = t18;
            {
                void * t20 = gm_l3;
                uint8_t t21 = 0;
                F.ret = t20;
                goto gm_fret;
            }
            goto ix11;
ie11:;
ix11:;
        }
        goto ix5;
ie5:;
ix5:;
    }
    {
        void * t22 = gm_l0;
        uint32_t t23 = gm_l1;
        void * t24 = gm_f_make_fn_info_4dk91jl((void *)NULL, cx, NULL, t22, t23);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t25 = 0;
        F.ret = t24;
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
inline __attribute__((aligned(4))) void * gm_f_func_ene90ss(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1) {
    struct gm_fr_func_ene90ss F;
    void * gm_l0; /* m */
    uint32_t gm_l1; /* f */
    GM_STACK_CHECK(&F, 56u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    void * t2 = GM_LD(void *, (((uint8_t *)t1) + 0));
    uint32_t t3 = gm_l1;
    void * t4 = gm_f_func_in17r5n((void *)NULL, cx, NULL, t2, t3);
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
inline __attribute__((aligned(4))) void gm_f_list_k2ij3i7(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_k2ij3i7 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_lopbqdt();
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
inline __attribute__((aligned(4))) void gm_f_list_kl0t9m0(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_kl0t9m0 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_84b0b46();
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_local_addr_o51qq3k(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, void * a2, uint32_t a3) {
    struct gm_fr_local_addr_o51qq3k F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    void * gm_l2; /* fr */
    uint32_t gm_l3; /* l */
    uint64_t gm_l4; /* i */
    GM_STACK_CHECK(&F, 238u);
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
            gm_t833u93mr4iob t18;
            GM_ST(uint8_t, (uint8_t *)&t18 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t18 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t18 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t18 + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&t18 + 24, 0);
            ((uint8_t *)&t18)[0] = 0;
            GM_ST(uint64_t, ((uint8_t *)&t18 + 8), t17);
            gm_t833u93mr4iob t19 = t18;
            uint8_t t20 = ((((uint8_t *)&t19))[0] != 0);
            F.ret = t18;
            goto gm_fret;
        }
        goto ix5;
ie5:;
ix5:;
    }
    {
        void * t21 = gm_l0;
        gm_tl874t2atnlh3 t22 = gm_l1;
        void * t23 = gm_l2;
        uint32_t t24 = gm_l3;
        gm_f_local_alloc_qm9q4fo((void *)&(*(gm_t833u93mr4iob *)gm_out), (void *)NULL, cx, NULL, t21, t22, t23, t24);
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
inline __attribute__((aligned(4))) void * gm_f_lower_body_g4bo546(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2) {
    struct gm_fr_lower_body_g4bo546 F;
    void * gm_l0; /* m */
    void * gm_l1; /* info */
    void * gm_l2; /* e */
    gm_t3mf58gv9kf3n gm_l3; /* lw */
    GM_STACK_CHECK(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        void * t3 = gm_l1;
        gm_t3mf58gv9kf3n t4;
        GM_ST(void *, ((uint8_t *)&t4 + 0), t2);
        GM_ST(void *, ((uint8_t *)&t4 + 8), t3);
        gm_l3 = t4;
    }
    {
        void * t5 = (void *)((uint8_t *)&gm_l3);
        void * t6 = gm_l2;
        void * t7 = gm_f_expr_a84t331((void *)NULL, cx, NULL, t5, t6);
        if (gm_unw.kind) {
            goto gm_lad;
        }
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline void * gm_f_new_frame_vgm56mt(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, void * a2, gm_b16_8 a3, void * a4, gm_b16_8 a5) {
    struct gm_fr_new_frame_vgm56mt F;
    void * gm_l0; /* m */
    uint32_t gm_l1; /* fn_id */
    void * gm_l2; /* info */
    gm_b16_8 gm_l3; /* ret */
    void * gm_l4; /* env */
    gm_b16_8 gm_l5; /* store */
    uint64_t gm_l6; /* n */
    void * gm_l7; /* fr */
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
        void * t2 = gm_l2;
        uint64_t t3 = GM_LD(uintptr_t, (((uint8_t *)t2) + 120) + 8);
        gm_l6 = t3;
    }
    {
        void * t4 = gm_l0;
        void * t5 = GM_LD(void *, (((uint8_t *)t4) + 536));
        void * t6 = t5;
        void * t7;
        if ((GM_LD(void *, ((uint8_t *)&t6)) != 0)) {
            void * t8 = GM_LD(void *, (((uint8_t *)&t6) + 0));
            t7 = t8;
        } else {
            void * t9 = gm_l0;
            void * t10 = gm_l2;
            gm_b16_8 t11 = gm_l3;
            void * t12 = gm_f_alloc_frame_31ks03n((void *)NULL, cx, NULL, t9, t10, t11);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            t7 = t12;
        }
        gm_l7 = t7;
    }
    {
        void * t13 = gm_l0;
        void * t14 = gm_l7;
        void * t15 = GM_LD(void *, (((uint8_t *)t14) + 248));
        GM_ST(void *, (((uint8_t *)t13) + 536), t15);
    }
    {
        void * t16 = gm_l0;
        void * t17 = gm_l0;
        uint32_t t18 = GM_LD(uint32_t, (((uint8_t *)t17) + 544));
        uint32_t r19; bool o20 = __builtin_sub_overflow(t18, ((uint32_t)1ULL), &r19);
        if (o20) gm_trap("integer overflow in `-`", gm_src6hq50r4rk7e6, 93, 5);
        uint32_t t21 = r19;
        GM_ST(uint32_t, (((uint8_t *)t16) + 544), t21);
    }
    {
        void * t23 = gm_l7;
        uint64_t t24 = GM_LD(uintptr_t, (((uint8_t *)t23) + 80) + 8);
        uint64_t t25 = gm_l6;
        uint8_t t26 = (t24 < t25);
        if (!t26) goto ie22;
        {
            void * t28 = gm_l0;
            void * t29 = gm_l7;
            uint64_t t30 = gm_l6;
            gm_b0_1 t31 = gm_f_grow_mem_buf_930r21a((void *)NULL, cx, NULL, t28, t29, t30);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix22;
ie22:;
ix22:;
    }
    {
        void * t32 = gm_l7;
        void * t33 = gm_l7;
        uint64_t t34 = GM_LD(uintptr_t, (((uint8_t *)t33) + 80) + 8);
        void * t35 = ((uint8_t *)GM_LD(void *, (((uint8_t *)t33) + 80)));
        uint64_t t36 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t37 = gm_l6;
        uint64_t t38 = (uintptr_t)t37;
        if (t36 > t38 || t38 > (uintptr_t)t34) gm_trap_slice(t36, t38, t34, gm_src6hq50r4rk7e6, 96, 14);
        gm_b16_8 t39;
        GM_ST(void *, (uint8_t *)&t39, (void *)(((uint8_t *)t35 + t36 * 8)));
        GM_ST(uintptr_t, (uint8_t *)&t39 + 8, (uintptr_t)(t38 - t36));
        GM_ST(gm_b16_8, (((uint8_t *)t32) + 64), t39);
    }
    {
        void * t40 = gm_l7;
        void * t41 = gm_l0;
        uint64_t t42 = GM_LD(uint64_t, (((uint8_t *)t41) + 760));
        GM_ST(uint64_t, (((uint8_t *)t40) + 0), t42);
    }
    {
        void * t43 = gm_l7;
        uint32_t t44 = gm_l1;
        GM_ST(uint32_t, (((uint8_t *)t43) + 12), t44);
    }
    {
        void * t45 = gm_l7;
        void * t46 = gm_l2;
        GM_ST(void *, (((uint8_t *)t45) + 16), t46);
    }
    {
        void * t47 = gm_l7;
        gm_b16_8 t48 = gm_l3;
        GM_ST(gm_b16_8, (((uint8_t *)t47) + 24), t48);
    }
    {
        void * t49 = gm_l7;
        void * t50 = gm_l4;
        GM_ST(void *, (((uint8_t *)t49) + 40), t50);
    }
    {
        void * t51 = gm_l7;
        gm_b16_8 t52 = gm_l5;
        GM_ST(gm_b16_8, (((uint8_t *)t51) + 48), t52);
    }
    {
        void * t53 = gm_l7;
        gm_t5lf9i3n1r3co t54;
        memset(&t54, 0, sizeof t54);
        GM_ST(gm_t5lf9i3n1r3co, (((uint8_t *)t53) + 216), t54);
    }
    {
        void * t55 = gm_l7;
        GM_ST(uint8_t, (((uint8_t *)t55) + 240), ((uint8_t)0));
    }
    {
        void * t56 = gm_l0;
        void * t57 = gm_l0;
        uint64_t t58 = GM_LD(uint64_t, (((uint8_t *)t57) + 760));
        uint64_t r59; bool o60 = __builtin_add_overflow(t58, ((uint64_t)1ULL), &r59);
        if (o60) gm_trap("integer overflow in `+`", gm_src6hq50r4rk7e6, 105, 5);
        uint64_t t61 = r59;
        GM_ST(uint64_t, (((uint8_t *)t56) + 760), t61);
    }
    {
        void * t62 = gm_l7;
        GM_ST(uint32_t, (((uint8_t *)t62) + 8), ((uint32_t)0ULL));
    }
    {
        void * t63 = gm_l7;
        uint8_t t64 = 0;
        F.ret = t63;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_offsets_qpt7c30(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_offsets_qpt7c30 F;
    void * gm_l0; /* m */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 144u);
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
    gm_tbccvikg6b96e t5 = t4;
    gm_b16_8 t6 = GM_LD(gm_b16_8, (((uint8_t *)&t5) + 16));
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_t9805qtonbsag gm_f_pop_570g79n(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_pop_570g79n F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* n */
    uint32_t gm_l2; /* x */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        uint64_t t3 = GM_LD(uintptr_t, (((uint8_t *)t2) + 0) + 8);
        gm_l1 = t3;
    }
    {
        uint64_t t5 = gm_l1;
        uint8_t t6 = ((t5 == ((uint64_t)0ULL)));
        if (!t6) goto ie4;
        {
            gm_t9805qtonbsag t8;
            memset(&t8, 0, sizeof t8);
            uint8_t t9 = 0;
            F.ret = t8;
            goto gm_fret;
        }
        goto ix4;
ie4:;
ix4:;
    }
    {
        uint64_t t10 = gm_l1;
        uint64_t r11; bool o12 = __builtin_sub_overflow(t10, ((uint64_t)1ULL), &r11);
        if (o12) gm_trap("integer overflow in `-`", gm_srcrgc9bkquf6nr, 130, 18);
        uint64_t t13 = r11;
        uint64_t t14 = (uintptr_t)t13;
        void * t15 = gm_l0;
        uint64_t t16 = GM_LD(uintptr_t, (((uint8_t *)t15) + 0) + 8);
        if (t14 >= (uintptr_t)(t16)) gm_trap_index(t14, t16, gm_srcrgc9bkquf6nr, 130, 10);
        uint32_t t17 = GM_LD(uint32_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)t15) + 0)) + t14 * 4));
        gm_l2 = t17;
    }
    {
        void * t18 = gm_l0;
        void * t19 = gm_l0;
        uint64_t t20 = GM_LD(uintptr_t, (((uint8_t *)t19) + 0) + 8);
        void * t21 = ((uint8_t *)GM_LD(void *, (((uint8_t *)t19) + 0)));
        uint64_t t22 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t23 = gm_l1;
        uint64_t r24; bool o25 = __builtin_sub_overflow(t23, ((uint64_t)1ULL), &r24);
        if (o25) gm_trap("integer overflow in `-`", gm_srcrgc9bkquf6nr, 131, 26);
        uint64_t t26 = r24;
        uint64_t t27 = (uintptr_t)t26;
        if (t22 > t27 || t27 > (uintptr_t)t20) gm_trap_slice(t22, t27, t20, gm_srcrgc9bkquf6nr, 131, 15);
        gm_b16_8 t28;
        GM_ST(void *, (uint8_t *)&t28, (void *)(((uint8_t *)t21 + t22 * 4)));
        GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(t27 - t22));
        GM_ST(gm_b16_8, (((uint8_t *)t18) + 0), t28);
    }
    {
        uint32_t t29 = gm_l2;
        gm_t9805qtonbsag t30;
        GM_ST(uint8_t, (uint8_t *)&t30 + 5, 0);
        GM_ST(uint16_t, (uint8_t *)&t30 + 6, 0);
        GM_ST(uint32_t, ((uint8_t *)&t30), t29);
        ((uint8_t *)&t30)[4] = 1;
        uint8_t t31 = 0;
        F.ret = t30;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
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
__attribute__((aligned(4))) __attribute__((noinline)) void * gm_f_alloc_frame_31ks03n(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_b16_8 a2) {
    struct gm_fr_alloc_frame_31ks03n F;
    void * gm_l0; /* m */
    void * gm_l1; /* info */
    gm_b16_8 gm_l2; /* ret */
    void * gm_l3; /* fr */
    GM_STACK_CHECK(&F, 521u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        gm_tkej6osnbjrnq t2;
        memset((uint8_t *)&t2 + 241, 0, 7);
        GM_ST(uint64_t, (uint8_t *)&t2 + 0, ((uint64_t)0ULL));
        GM_ST(uint32_t, (uint8_t *)&t2 + 8, ((uint32_t)0ULL));
        GM_ST(uint32_t, (uint8_t *)&t2 + 12, ((uint32_t)0ULL));
        void * t3 = gm_l1;
        GM_ST(void *, (uint8_t *)&t2 + 16, t3);
        gm_b16_8 t4 = gm_l2;
        GM_ST(gm_b16_8, (uint8_t *)&t2 + 24, t4);
        GM_ST(void *, (uint8_t *)&t2 + 40, ((void *)0));
        gm_b16_8 t5 = gm_f_empty_slice_ijtp9lo();
        GM_ST(gm_b16_8, (uint8_t *)&t2 + 48, t5);
        gm_b16_8 t6 = gm_f_empty_slice_84b0b46();
        GM_ST(gm_b16_8, (uint8_t *)&t2 + 64, t6);
        gm_b16_8 t7 = gm_f_empty_slice_84b0b46();
        GM_ST(gm_b16_8, (uint8_t *)&t2 + 80, t7);
        gm_f_list_kl0t9m0((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&t2 + 96)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_list_k2ij3i7((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&t2 + 136)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_list_ib5k4gb_2((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&t2 + 176)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t5lf9i3n1r3co t8;
        memset(&t8, 0, sizeof t8);
        GM_ST(gm_t5lf9i3n1r3co, (uint8_t *)&t2 + 216, t8);
        GM_ST(uint8_t, (uint8_t *)&t2 + 240, ((uint8_t)0));
        GM_ST(void *, (uint8_t *)&t2 + 248, ((void *)0));
        gm_tq7s98ihonkju t9 = gm_f_new_ifv16tr_2((void *)NULL, cx, NULL, &t2, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kl5pit792cd)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tq7s98ihonkju t10 = t9;
        if (((((uint8_t *)&t10))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t10) + 8, gm_src6hq50r4rk7e6, 115, 11);
        void * t11 = GM_LD(void *, (((uint8_t *)&t10) + 8));
        gm_l3 = t11;
    }
    {
        void * t12 = gm_l3;
        void * t13 = gm_l0;
        void * t14 = GM_LD(void *, (((uint8_t *)t13) + 536));
        GM_ST(void *, (((uint8_t *)t12) + 248), t14);
    }
    {
        void * t15 = gm_l0;
        void * t16 = gm_l3;
        GM_ST(void *, (((uint8_t *)t15) + 536), t16);
    }
    {
        void * t17 = gm_l0;
        void * t18 = gm_l0;
        uint32_t t19 = GM_LD(uint32_t, (((uint8_t *)t18) + 544));
        uint32_t r20; bool o21 = __builtin_add_overflow(t19, ((uint32_t)1ULL), &r20);
        if (o21) gm_trap("integer overflow in `+`", gm_src6hq50r4rk7e6, 118, 5);
        uint32_t t22 = r20;
        GM_ST(uint32_t, (((uint8_t *)t17) + 544), t22);
    }
    {
        void * t23 = gm_l3;
        uint8_t t24 = 0;
        F.ret = t23;
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
extern __attribute__((aligned(4))) gm_b0_1 gm_f_end_frame_cetr4jh(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) __attribute__((noinline)) gm_b0_1 gm_f_end_frame_slow_mnbcrps(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_end_frame_slow_mnbcrps F;
    void * gm_l0; /* m */
    void * gm_l1; /* fr */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    uint64_t gm_l4; /* a */
    GM_STACK_CHECK(&F, 645u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t3 = gm_l1;
        uint64_t t4 = GM_LD(uintptr_t, ((((uint8_t *)t3) + 96) + 0) + 8);
        uint8_t t5 = (t4 > ((uint64_t)0ULL));
        if (!t5) goto ie2;
        {
            {
                void * t8 = gm_l1;
                gm_b16_8 t9 = GM_LD(gm_b16_8, ((((uint8_t *)t8) + 96) + 0));
                gm_l2 = t9;
            }
            {
                gm_l3 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t11 = gm_l3;
                    uint64_t t12 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                    uint8_t t13 = (t11 < t12);
                    if (!t13) goto lb10;
                    {
                        uint64_t t16 = gm_l3;
                        uint64_t t17 = (uintptr_t)t16;
                        uint64_t t18 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                        uint64_t t19 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t17 * 8));
                        uint64_t t20 = t19;
                        uint64_t t23 = GM_LD(uint64_t, ((uint8_t *)&t20));
                        gm_l4 = t23;
                        {
                            {
                                void * t26 = gm_l0;
                                void * t27 = (void *)(((uint8_t *)t26) + 184);
                                uint64_t t28 = gm_l4;
                                uint8_t t29 = gm_f_kill_3kcavfd((void *)NULL, cx, NULL, t27, t28);
                                if (gm_unw.kind) {
                                    goto gm_ll14;
                                }
                            }
                        }
                        goto me21;
mn22:;
                        gm_trap("no match arm matched the scrutinee", gm_src6hq50r4rk7e6, 159, 9);
me21:;
                    }
lc10:;
                    {
                        uint64_t t31 = gm_l3;
                        uint64_t r32; bool o33 = __builtin_add_overflow(t31, ((uint64_t)1ULL), &r32);
                        uint64_t t34 = r32;
                        gm_l3 = t34;
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
            void * t35 = gm_l1;
            void * t36 = (void *)(((uint8_t *)t35) + 96);
            gm_f_clear_1tf0nio(t36);
        }
        {
            void * t37 = gm_l1;
            gm_b16_8 t38 = GM_LD(gm_b16_8, (((uint8_t *)t37) + 64));
            gm_f_fill_kd2qt72(t38, ((uint64_t)0ULL));
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t40 = gm_l1;
        uint64_t t41 = GM_LD(uintptr_t, ((((uint8_t *)t40) + 136) + 0) + 8);
        uint8_t t42 = (t41 > ((uint64_t)0ULL));
        if (!t42) goto ie39;
        {
            void * t44 = gm_l1;
            void * t45 = (void *)(((uint8_t *)t44) + 136);
            gm_f_clear_rqatj1i(t45);
        }
        goto ix39;
ie39:;
ix39:;
    }
    {
        void * t47 = gm_l1;
        uint64_t t48 = GM_LD(uintptr_t, ((((uint8_t *)t47) + 176) + 0) + 8);
        uint8_t t49 = (t48 > ((uint64_t)0ULL));
        if (!t49) goto ie46;
        {
            void * t51 = gm_l1;
            void * t52 = (void *)(((uint8_t *)t51) + 176);
            gm_f_clear_6i2ni9k(t52);
        }
        goto ix46;
ie46:;
ix46:;
    }
    {
        void * t54 = gm_l1;
        uint32_t t55 = GM_LD(uint32_t, (((uint8_t *)t54) + 8));
        uint8_t t56 = (!(t55 == ((uint32_t)0ULL)));
        if (!t56) goto ie53;
        {
            void * t58 = gm_l0;
            gm_b16_8 t59 = GM_LD(gm_b16_8, ((((uint8_t *)t58) + 456) + 0));
            gm_b16_8 t60 = t59;
            void * t61 = gm_l1;
            uint32_t t62 = GM_LD(uint32_t, (((uint8_t *)t61) + 8));
            uint32_t r63; bool o64 = __builtin_sub_overflow(t62, ((uint32_t)1ULL), &r63);
            if (o64) gm_trap("integer overflow in `-`", gm_src6hq50r4rk7e6, 166, 27);
            uint32_t t65 = r63;
            uint64_t t66 = (uint64_t)t65;
            uint64_t t67 = gm_f_us_cuv01fp(t66);
            uint64_t t68 = (uintptr_t)t67;
            gm_b16_8 t69 = t60;
            uint64_t t70 = GM_LD(uintptr_t, ((uint8_t *)&t69) + 8);
            if (t68 >= (uintptr_t)(t70)) gm_trap_index(t68, t70, gm_src6hq50r4rk7e6, 166, 9);
            GM_ST(void *, ((uint8_t *)GM_LD(void *, ((uint8_t *)&t69)) + t68 * 8), ((void *)0));
        }
        {
            void * t71 = gm_l0;
            void * t72 = (void *)(((uint8_t *)t71) + 592);
            void * t73 = gm_l1;
            uint32_t t74 = GM_LD(uint32_t, (((uint8_t *)t73) + 8));
            uint32_t r75; bool o76 = __builtin_sub_overflow(t74, ((uint32_t)1ULL), &r75);
            if (o76) gm_trap("integer overflow in `-`", gm_src6hq50r4rk7e6, 167, 28);
            uint32_t t77 = r75;
            gm_tftb1odc739t5 t78 = gm_f_push_im9d4ro(t72, t77);
            gm_tftb1odc739t5 t79 = t78;
            if (((((uint8_t *)&t79))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t79) + 1, gm_src6hq50r4rk7e6, 167, 9);
            gm_b0_1 t80 = GM_LD(gm_b0_1, (((uint8_t *)&t79) + 1));
        }
        goto ix53;
ie53:;
ix53:;
    }
    {
        void * t82 = gm_l0;
        uint32_t t83 = GM_LD(uint32_t, (((uint8_t *)t82) + 544));
        uint8_t t84 = (t83 < ((uint32_t)256ULL));
        if (!t84) goto ie81;
        {
            void * t86 = gm_l1;
            void * t87 = gm_l0;
            void * t88 = GM_LD(void *, (((uint8_t *)t87) + 536));
            GM_ST(void *, (((uint8_t *)t86) + 248), t88);
        }
        {
            void * t89 = gm_l0;
            void * t90 = gm_l1;
            GM_ST(void *, (((uint8_t *)t89) + 536), t90);
        }
        {
            void * t91 = gm_l0;
            void * t92 = gm_l0;
            uint32_t t93 = GM_LD(uint32_t, (((uint8_t *)t92) + 544));
            uint32_t r94; bool o95 = __builtin_add_overflow(t93, ((uint32_t)1ULL), &r94);
            if (o95) gm_trap("integer overflow in `+`", gm_src6hq50r4rk7e6, 172, 9);
            uint32_t t96 = r94;
            GM_ST(uint32_t, (((uint8_t *)t91) + 544), t96);
        }
        {
            gm_b0_1 t97;
            uint8_t t98 = 0;
            F.ret = t97;
            goto gm_fret;
        }
        goto ix81;
ie81:;
ix81:;
    }
    {
        void * t99 = gm_l1;
        void * t100 = (void *)(((uint8_t *)t99) + 96);
        gm_f_deinit_v2ik7pl(t100);
    }
    {
        void * t101 = gm_l1;
        void * t102 = (void *)(((uint8_t *)t101) + 136);
        gm_f_deinit_lb56u7s(t102);
    }
    {
        void * t103 = gm_l1;
        void * t104 = (void *)(((uint8_t *)t103) + 176);
        gm_f_deinit_6u0ld99(t104);
    }
    {
        void * t106 = gm_l1;
        uint64_t t107 = GM_LD(uintptr_t, (((uint8_t *)t106) + 80) + 8);
        uint8_t t108 = (t107 > ((uint64_t)0ULL));
        if (!t108) goto ie105;
        {
            void * t110 = gm_l1;
            gm_b16_8 t111 = GM_LD(gm_b16_8, (((uint8_t *)t110) + 80));
            gm_b16_8 t112 = t111;
            gm_b16_8 t113 = GM_LD(gm_b16_8, ((uint8_t *)&t112));
            gm_b0_1 t114 = gm_f_free_l0kjo6g((void *)NULL, cx, NULL, t113, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kk6gotfp7pf)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix105;
ie105:;
ix105:;
    }
    {
        void * t115 = gm_l1;
        gm_b0_1 t116 = gm_f_destroy_ca2nrm8((void *)NULL, cx, NULL, t115, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_ksoekcu07mu)));
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
extern __attribute__((aligned(4))) uint64_t gm_f_fn_addr_odbbf6a(void * a0, uint32_t a1);
inline __attribute__((aligned(4))) gm_t9805qtonbsag gm_f_fn_from_addr_7a6bpqd(void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_fn_from_addr_7a6bpqd F;
    void * gm_l0; /* m */
    uint64_t gm_l1; /* addr */
    uint64_t gm_l2; /* id */
    GM_STACK_CHECK_SHALLOW(&F, 203u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint8_t t3 = 0;
        uint64_t t4 = gm_l1;
        uint64_t t5 = GM_LD(uint64_t, gm_cmiriali67r);
        uint8_t t6 = (t4 < t5);
        if (!t6) {
            uint64_t t7 = gm_l1;
            uint64_t t8 = GM_LD(uint64_t, gm_cmiriali67r);
            uint64_t r9; bool o10 = __builtin_sub_overflow(t7, t8, &r9);
            if (o10) gm_trap("integer overflow in `-`", gm_src6hq50r4rk7e6, 8, 29);
            uint64_t t11 = r9;
            if (((uint64_t)16ULL) == 0) gm_trap("division by zero", gm_src6hq50r4rk7e6, 8, 28);
            uint64_t t12 = (uint64_t)(t11 % ((uint64_t)16ULL));
            uint8_t t13 = (!(t12 == ((uint64_t)0ULL)));
            t3 = t13;
        } else t3 = 1;
        if (!t3) goto ie2;
        {
            gm_t9805qtonbsag t15;
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
        uint64_t t17 = gm_l1;
        uint64_t t18 = GM_LD(uint64_t, gm_cmiriali67r);
        uint64_t r19; bool o20 = __builtin_sub_overflow(t17, t18, &r19);
        if (o20) gm_trap("integer overflow in `-`", gm_src6hq50r4rk7e6, 9, 12);
        uint64_t t21 = r19;
        if (((uint64_t)16ULL) == 0) gm_trap("division by zero", gm_src6hq50r4rk7e6, 9, 11);
        uint64_t t22 = (uint64_t)(t21 / ((uint64_t)16ULL));
        gm_l2 = t22;
    }
    {
        uint64_t t24 = gm_l2;
        void * t25 = gm_l0;
        void * t26 = GM_LD(void *, (((uint8_t *)t25) + 8));
        uint64_t t27 = GM_LD(uintptr_t, ((((uint8_t *)t26) + 344) + 0) + 8);
        uint64_t t28 = (uint64_t)t27;
        uint8_t t29 = (t24 >= t28);
        if (!t29) goto ie23;
        {
            gm_t9805qtonbsag t31;
            memset(&t31, 0, sizeof t31);
            uint8_t t32 = 0;
            F.ret = t31;
            goto gm_fret;
        }
        goto ix23;
ie23:;
ix23:;
    }
    {
        uint64_t t33 = gm_l2;
        uint32_t t34 = (uint32_t)t33;
        gm_t9805qtonbsag t35;
        GM_ST(uint8_t, (uint8_t *)&t35 + 5, 0);
        GM_ST(uint16_t, (uint8_t *)&t35 + 6, 0);
        GM_ST(uint32_t, ((uint8_t *)&t35), t34);
        ((uint8_t *)&t35)[4] = 1;
        uint8_t t36 = 0;
        F.ret = t35;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) void * gm_f_fn_info_hocist4(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
inline __attribute__((aligned(4))) uint32_t gm_f_frame_handle_k12gn88(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_frame_handle_k12gn88 F;
    void * gm_l0; /* m */
    void * gm_l1; /* fr */
    uint32_t gm_l2; /* h */
    GM_STACK_CHECK(&F, 259u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t3 = gm_l1;
        uint32_t t4 = GM_LD(uint32_t, (((uint8_t *)t3) + 8));
        uint8_t t5 = (!(t4 == ((uint32_t)0ULL)));
        if (!t5) goto ie2;
        {
            void * t7 = gm_l1;
            uint32_t t8 = GM_LD(uint32_t, (((uint8_t *)t7) + 8));
            uint8_t t9 = 0;
            F.ret = t8;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t11 = gm_l0;
        void * t12 = (void *)(((uint8_t *)t11) + 592);
        gm_t9805qtonbsag t13 = gm_f_pop_570g79n(t12);
        gm_t9805qtonbsag t14 = t13;
        if (!((((uint8_t *)&t14))[4] != 0)) goto ie10;
        uint32_t t15 = GM_LD(uint32_t, ((uint8_t *)&t14));
        gm_l2 = t15;
        {
            void * t17 = gm_l0;
            gm_b16_8 t18 = GM_LD(gm_b16_8, ((((uint8_t *)t17) + 456) + 0));
            gm_b16_8 t19 = t18;
            uint32_t t20 = gm_l2;
            uint64_t t21 = (uint64_t)t20;
            uint64_t t22 = gm_f_us_cuv01fp(t21);
            uint64_t t23 = (uintptr_t)t22;
            gm_b16_8 t24 = t19;
            uint64_t t25 = GM_LD(uintptr_t, ((uint8_t *)&t24) + 8);
            if (t23 >= (uintptr_t)(t25)) gm_trap_index(t23, t25, gm_src6hq50r4rk7e6, 134, 9);
            void * t26 = gm_l1;
            GM_ST(void *, ((uint8_t *)GM_LD(void *, ((uint8_t *)&t24)) + t23 * 8), t26);
        }
        {
            void * t27 = gm_l1;
            uint32_t t28 = gm_l2;
            uint32_t r29; bool o30 = __builtin_add_overflow(t28, ((uint32_t)1ULL), &r29);
            if (o30) gm_trap("integer overflow in `+`", gm_src6hq50r4rk7e6, 135, 21);
            uint32_t t31 = r29;
            GM_ST(uint32_t, (((uint8_t *)t27) + 8), t31);
        }
        goto ix10;
ie10:;
        {
            void * t33 = gm_l0;
            void * t34 = (void *)(((uint8_t *)t33) + 456);
            void * t35 = gm_l1;
            gm_tftb1odc739t5 t36 = gm_f_push_se7jhqh(t34, t35);
            gm_tftb1odc739t5 t37 = t36;
            if (((((uint8_t *)&t37))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t37) + 1, gm_src6hq50r4rk7e6, 137, 9);
            gm_b0_1 t38 = GM_LD(gm_b0_1, (((uint8_t *)&t37) + 1));
        }
        {
            void * t39 = gm_l1;
            void * t40 = gm_l0;
            uint64_t t41 = GM_LD(uintptr_t, ((((uint8_t *)t40) + 456) + 0) + 8);
            uint32_t t42 = (uint32_t)t41;
            GM_ST(uint32_t, (((uint8_t *)t39) + 8), t42);
        }
ix10:;
    }
    {
        void * t43 = gm_l1;
        uint32_t t44 = GM_LD(uint32_t, (((uint8_t *)t43) + 8));
        uint8_t t45 = 0;
        F.ret = t44;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void * gm_f_frame_of_dgl92on(void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_frame_of_dgl92on F;
    void * gm_l0; /* m */
    uint64_t gm_l1; /* h */
    GM_STACK_CHECK_SHALLOW(&F, 165u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint8_t t3 = 0;
        uint64_t t4 = gm_l1;
        uint8_t t5 = ((t4 == ((uint64_t)0ULL)));
        if (!t5) {
            uint64_t t6 = gm_l1;
            void * t7 = gm_l0;
            uint64_t t8 = GM_LD(uintptr_t, ((((uint8_t *)t7) + 456) + 0) + 8);
            uint64_t t9 = (uint64_t)t8;
            uint8_t t10 = (t6 > t9);
            t3 = t10;
        } else t3 = 1;
        if (!t3) goto ie2;
        {
            uint8_t t12 = 0;
            F.ret = ((void *)0);
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t13 = gm_l0;
        gm_b16_8 t14 = GM_LD(gm_b16_8, ((((uint8_t *)t13) + 456) + 0));
        gm_b16_8 t15 = t14;
        uint64_t t16 = gm_l1;
        uint64_t r17; bool o18 = __builtin_sub_overflow(t16, ((uint64_t)1ULL), &r17);
        if (o18) gm_trap("integer overflow in `-`", gm_src6hq50r4rk7e6, 82, 30);
        uint64_t t19 = r17;
        uint64_t t20 = gm_f_us_cuv01fp(t19);
        uint64_t t21 = (uintptr_t)t20;
        gm_b16_8 t22 = t15;
        uint64_t t23 = GM_LD(uintptr_t, ((uint8_t *)&t22) + 8);
        if (t21 >= (uintptr_t)(t23)) gm_trap_index(t21, t23, gm_src6hq50r4rk7e6, 82, 12);
        void * t24 = GM_LD(void *, ((uint8_t *)GM_LD(void *, ((uint8_t *)&t22)) + t21 * 8));
        uint8_t t25 = 0;
        F.ret = t24;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void * gm_f_frame_up_ghkkuqg(void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_frame_up_ghkkuqg F;
    void * gm_l0; /* fr */
    uint32_t gm_l1; /* levels */
    void * gm_l2; /* cur */
    uint32_t gm_l3; /* <tmp> */
    uint32_t gm_l4; /* <tmp> */
    GM_STACK_CHECK_SHALLOW(&F, 131u);
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
        {
            gm_l4 = ((uint32_t)0ULL);
        }
        {
            uint32_t t4 = gm_l1;
            gm_l3 = t4;
        }
        {
            for (;;) {
                uint32_t t6 = gm_l4;
                uint32_t t7 = gm_l3;
                uint8_t t8 = (t6 < t7);
                if (!t8) goto lb5;
                {
                    uint32_t t11 = gm_l4;
                    uint32_t t12 = t11;
                    {
                        void * t16 = gm_l2;
                        void * t17 = GM_LD(void *, (((uint8_t *)t16) + 40));
                        void * t18 = t17;
                        void * t19;
                        if ((GM_LD(void *, ((uint8_t *)&t18)) != 0)) {
                            void * t20 = GM_LD(void *, (((uint8_t *)&t18) + 0));
                            t19 = t20;
                        } else {
                            uint8_t t21 = 0;
                            F.ret = ((void *)0);
                            goto gm_fret;
                        }
                        gm_l2 = t19;
                    }
                    goto me13;
mn14:;
                    gm_trap("no match arm matched the scrutinee", gm_src6hq50r4rk7e6, 185, 5);
me13:;
                }
lc5:;
                {
                    uint32_t t23 = gm_l4;
                    uint32_t r24; bool o25 = __builtin_add_overflow(t23, ((uint32_t)1ULL), &r24);
                    uint32_t t26 = r24;
                    gm_l4 = t26;
                }
            }
lb5:;
        }
    }
    {
        void * t27 = gm_l2;
        uint8_t t28 = 0;
        F.ret = t27;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) void * gm_f_func_ene90ss(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
__attribute__((aligned(4))) __attribute__((noinline)) gm_b0_1 gm_f_grow_mem_buf_930r21a(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint64_t a2) {
    struct gm_fr_grow_mem_buf_930r21a F;
    void * gm_l0; /* m */
    void * gm_l1; /* fr */
    uint64_t gm_l2; /* n */
    GM_STACK_CHECK(&F, 193u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t3 = gm_l1;
        uint64_t t4 = GM_LD(uintptr_t, (((uint8_t *)t3) + 80) + 8);
        uint8_t t5 = (t4 > ((uint64_t)0ULL));
        if (!t5) goto ie2;
        {
            void * t7 = gm_l1;
            gm_b16_8 t8 = GM_LD(gm_b16_8, (((uint8_t *)t7) + 80));
            gm_b16_8 t9 = t8;
            gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
            gm_b0_1 t11 = gm_f_free_l0kjo6g((void *)NULL, cx, NULL, t10, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_ku7k9fgdnj6)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t12 = gm_l1;
        uint8_t *t13 = (((uint8_t *)t12) + 80);
        uint64_t t14 = gm_l2;
        uint64_t t15 = (t14 > ((uint64_t)8ULL) ? t14 : ((uint64_t)8ULL));
        gm_tabb710d9mq2e t16;
        gm_f_make_fill_e6rv4na((void *)&t16, (void *)NULL, cx, NULL, t15, ((uint64_t)0ULL), &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_k68ol137ncd)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t17 = t16;
        if (((((uint8_t *)&t17))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t17) + 8, gm_src6hq50r4rk7e6, 126, 18);
        gm_b16_8 t18 = GM_LD(gm_b16_8, (((uint8_t *)&t17) + 8));
        GM_ST(gm_b16_8, t13, t18);
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
extern __attribute__((aligned(4))) void gm_f_local_addr_o51qq3k(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, void * a2, uint32_t a3);
__attribute__((aligned(4))) __attribute__((noinline)) void gm_f_local_alloc_qm9q4fo(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, void * a2, uint32_t a3) {
    struct gm_fr_local_alloc_qm9q4fo F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    void * gm_l2; /* fr */
    uint32_t gm_l3; /* l */
    uint64_t gm_l4; /* i */
    uint64_t gm_l5; /* addr */
    uint64_t gm_l6; /* off */
    GM_STACK_CHECK(&F, 790u);
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
        void * t5 = gm_l0;
        gm_tl874t2atnlh3 t6 = gm_l1;
        uint64_t t7 = gm_l4;
        uint64_t t8 = (uintptr_t)t7;
        void * t9 = gm_l2;
        void * t10 = GM_LD(void *, (((uint8_t *)t9) + 16));
        uint64_t t11 = GM_LD(uintptr_t, (((uint8_t *)t10) + 88) + 8);
        if (t8 >= (uintptr_t)(t11)) gm_trap_index(t8, t11, gm_src6hq50r4rk7e6, 201, 35);
        uint64_t t12 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)t10) + 88)) + t8 * 8));
        uint64_t t13 = gm_l4;
        uint64_t t14 = (uintptr_t)t13;
        void * t15 = gm_l2;
        void * t16 = GM_LD(void *, (((uint8_t *)t15) + 16));
        uint64_t t17 = GM_LD(uintptr_t, (((uint8_t *)t16) + 104) + 8);
        if (t14 >= (uintptr_t)(t17)) gm_trap_index(t14, t17, gm_src6hq50r4rk7e6, 201, 53);
        uint64_t t18 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)t16) + 104)) + t14 * 8));
        gm_t9qkh0rsooc8d t19;
        GM_ST(uint8_t, ((uint8_t *)&t19 + 0), ((uint8_t)0ULL));
        gm_t833u93mr4iob t20;
        gm_f_alloc_mem_0vu6kcs((void *)&t20, (void *)NULL, cx, NULL, t5, t6, t12, t18, t19, ((uint8_t)1));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t833u93mr4iob t21 = t20;
        if (((((uint8_t *)&t21))[0] != 0)) {
            gm_t76123gfet73g t22 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t21) + 8));
            gm_t833u93mr4iob t23;
            GM_ST(uint8_t, (uint8_t *)&t23 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t23 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t23 + 4, 0);
            ((uint8_t *)&t23)[0] = 1;
            GM_ST(gm_t76123gfet73g, ((uint8_t *)&t23 + 8), t22);
            F.ret = t23;
            goto gm_fret;
        }
        uint64_t t24 = GM_LD(uint64_t, (((uint8_t *)&t21) + 8));
        gm_l5 = t24;
    }
    {
        uint64_t t25 = gm_l4;
        uint64_t t26 = (uintptr_t)t25;
        void * t27 = gm_l2;
        void * t28 = GM_LD(void *, (((uint8_t *)t27) + 16));
        uint64_t t29 = GM_LD(uintptr_t, (((uint8_t *)t28) + 72) + 8);
        if (t26 >= (uintptr_t)(t29)) gm_trap_index(t26, t29, gm_src6hq50r4rk7e6, 203, 15);
        uint64_t t30 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)t28) + 72)) + t26 * 8));
        uint64_t t31 = gm_f_us_cuv01fp(t30);
        gm_l6 = t31;
    }
    {
        {
            void * t33 = gm_l0;
            void * t34 = (void *)(((uint8_t *)t33) + 184);
            uint64_t t35 = gm_l5;
            void * t36 = gm_l2;
            gm_b16_8 t37 = GM_LD(gm_b16_8, (((uint8_t *)t36) + 48));
            gm_b16_8 t38 = t37;
            uint64_t t39 = GM_LD(uintptr_t, ((uint8_t *)&t38) + 8);
            void * t40 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t38)));
            uint64_t t41 = gm_l6;
            uint64_t t42 = (uintptr_t)t41;
            uint64_t t43 = gm_l6;
            uint64_t t44 = gm_l4;
            uint64_t t45 = (uintptr_t)t44;
            void * t46 = gm_l2;
            void * t47 = GM_LD(void *, (((uint8_t *)t46) + 16));
            uint64_t t48 = GM_LD(uintptr_t, (((uint8_t *)t47) + 88) + 8);
            if (t45 >= (uintptr_t)(t48)) gm_trap_index(t45, t48, gm_src6hq50r4rk7e6, 204, 49);
            uint64_t t49 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)t47) + 88)) + t45 * 8));
            uint64_t t50 = gm_f_us_cuv01fp(t49);
            uint64_t r51; bool o52 = __builtin_add_overflow(t43, t50, &r51);
            if (o52) gm_trap("integer overflow in `+`", gm_src6hq50r4rk7e6, 204, 40);
            uint64_t t53 = r51;
            uint64_t t54 = (uintptr_t)t53;
            if (t42 > t54 || t54 > (uintptr_t)t39) gm_trap_slice(t42, t54, t39, gm_src6hq50r4rk7e6, 204, 26);
            gm_b16_8 t55;
            GM_ST(void *, (uint8_t *)&t55, (void *)(((uint8_t *)t40 + t42 * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t55 + 8, (uintptr_t)(t54 - t42));
            gm_b16_8 t56 = t55;
            gm_b16_8 t57 = GM_LD(gm_b16_8, ((uint8_t *)&t56));
            gm_tgfdnufetdnfc t58;
            gm_f_init_9fuf99v((void *)&t58, t34, t35, t57);
        }
    }
    {
        uint64_t t59 = gm_l4;
        uint64_t t60 = (uintptr_t)t59;
        void * t61 = gm_l2;
        uint64_t t62 = GM_LD(uintptr_t, (((uint8_t *)t61) + 64) + 8);
        if (t60 >= (uintptr_t)(t62)) gm_trap_index(t60, t62, gm_src6hq50r4rk7e6, 205, 5);
        uint64_t t63 = gm_l5;
        GM_ST(uint64_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)t61) + 64)) + t60 * 8), t63);
    }
    {
        void * t64 = gm_l2;
        void * t65 = (void *)(((uint8_t *)t64) + 96);
        uint64_t t66 = gm_l5;
        gm_tftb1odc739t5 t67 = gm_f_push_7o6oubd(t65, t66);
        gm_tftb1odc739t5 t68 = t67;
        if (((((uint8_t *)&t68))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t68) + 1, gm_src6hq50r4rk7e6, 206, 5);
        gm_b0_1 t69 = GM_LD(gm_b0_1, (((uint8_t *)&t68) + 1));
    }
    {
        uint64_t t70 = gm_l5;
        gm_t833u93mr4iob t71;
        GM_ST(uint8_t, (uint8_t *)&t71 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t71 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t71 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t71 + 16, 0);
        GM_ST(uint64_t, (uint8_t *)&t71 + 24, 0);
        ((uint8_t *)&t71)[0] = 0;
        GM_ST(uint64_t, ((uint8_t *)&t71 + 8), t70);
        gm_t833u93mr4iob t72 = t71;
        uint8_t t73 = ((((uint8_t *)&t72))[0] != 0);
        F.ret = t71;
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
__attribute__((aligned(4))) __attribute__((noinline)) void * gm_f_make_fn_info_4dk91jl(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1) {
    struct gm_fr_make_fn_info_4dk91jl F;
    void * gm_l0; /* m */
    uint32_t gm_l1; /* f */
    uint64_t gm_l2; /* i */
    void * gm_l3; /* x */
    void * gm_l4; /* def */
    void * gm_l5; /* made */
    gm_b16_8 gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    uint32_t gm_l8; /* p */
    void * gm_l9; /* b */
    void * gm_l10; /* body */
    void * gm_l11; /* c */
    GM_STACK_CHECK(&F, 1131u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint32_t t2 = gm_l1;
        uint64_t t3 = (uint64_t)t2;
        uint64_t t4 = gm_f_us_cuv01fp(t3);
        gm_l2 = t4;
    }
    {
        for (;;) {
            void * t6 = gm_l0;
            uint64_t t7 = GM_LD(uintptr_t, ((((uint8_t *)t6) + 720) + 0) + 8);
            uint64_t t8 = gm_l2;
            uint8_t t9 = (t7 <= t8);
            if (!t9) goto lb5;
            {
                void * t12 = gm_l0;
                void * t13 = (void *)(((uint8_t *)t12) + 720);
                gm_tftb1odc739t5 t14 = gm_f_push_ghpucvc(t13, ((void *)0));
                gm_tftb1odc739t5 t15 = t14;
                if (((((uint8_t *)&t15))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t15) + 1, gm_src6hq50r4rk7e6, 28, 36);
                gm_b0_1 t16 = GM_LD(gm_b0_1, (((uint8_t *)&t15) + 1));
            }
lc5:;
        }
lb5:;
    }
    {
        uint64_t t18 = gm_l2;
        uint64_t t19 = (uintptr_t)t18;
        void * t20 = gm_l0;
        uint64_t t21 = GM_LD(uintptr_t, ((((uint8_t *)t20) + 720) + 0) + 8);
        if (t19 >= (uintptr_t)(t21)) gm_trap_index(t19, t21, gm_src6hq50r4rk7e6, 29, 13);
        void * t22 = GM_LD(void *, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t20) + 720) + 0)) + t19 * 8));
        void * t23 = t22;
        if (!(GM_LD(void *, ((uint8_t *)&t23)) != 0)) goto ie17;
        void * t24 = GM_LD(void *, ((uint8_t *)&t23));
        gm_l3 = t24;
        {
            void * t26 = gm_l3;
            uint8_t t27 = 0;
            F.ret = t26;
            goto gm_fret;
        }
        goto ix17;
ie17:;
ix17:;
    }
    {
        void * t28 = gm_l0;
        uint32_t t29 = gm_l1;
        void * t30 = gm_f_func_ene90ss((void *)NULL, cx, NULL, t28, t29);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l4 = t30;
    }
    {
        void * t31 = gm_l0;
        uint32_t t32 = gm_l1;
        void * t33 = gm_l4;
        gm_b16_8 t34 = GM_LD(gm_b16_8, (((uint8_t *)t33) + 48));
        void * t35 = gm_l4;
        void * t36 = GM_LD(void *, (((uint8_t *)t35) + 152));
        void * t37 = gm_f_make_info_aau8il9((void *)NULL, cx, NULL, t31, t32, t34, t36);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l5 = t37;
    }
    {
        void * t38 = gm_l5;
        uint8_t *t39 = (((uint8_t *)t38) + 144);
        void * t40 = gm_l0;
        void * t41 = GM_LD(void *, (((uint8_t *)t40) + 8));
        void * t42 = gm_l4;
        gm_b16_8 t43 = GM_LD(gm_b16_8, (((uint8_t *)t42) + 48));
        void * t44 = gm_l4;
        void * t45 = GM_LD(void *, (((uint8_t *)t44) + 152));
        uint64_t t46 = gm_f_frame_cost_oei64tv((void *)NULL, cx, NULL, t41, t43, t45);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(uint64_t, t39, t46);
    }
    {
        void * t47 = gm_l5;
        void * t48 = gm_l4;
        gm_b16_8 t49 = GM_LD(gm_b16_8, (((uint8_t *)t48) + 32));
        GM_ST(gm_b16_8, (((uint8_t *)t47) + 8), t49);
    }
    {
        {
            void * t51 = gm_l4;
            gm_b16_8 t52 = GM_LD(gm_b16_8, (((uint8_t *)t51) + 32));
            gm_l6 = t52;
        }
        {
            gm_l7 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t54 = gm_l7;
                uint64_t t55 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                uint8_t t56 = (t54 < t55);
                if (!t56) goto lb53;
                {
                    uint64_t t59 = gm_l7;
                    uint64_t t60 = (uintptr_t)t59;
                    uint64_t t61 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                    uint32_t t62 = GM_LD(uint32_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t60 * 4));
                    uint32_t t63 = t62;
                    uint32_t t66 = GM_LD(uint32_t, ((uint8_t *)&t63));
                    gm_l8 = t66;
                    {
                        void * t69 = gm_l5;
                        gm_b16_8 t70 = GM_LD(gm_b16_8, (((uint8_t *)t69) + 120));
                        gm_b16_8 t71 = t70;
                        uint32_t t72 = gm_l8;
                        uint64_t t73 = (uint64_t)t72;
                        uint64_t t74 = gm_f_us_cuv01fp(t73);
                        uint64_t t75 = (uintptr_t)t74;
                        gm_b16_8 t76 = t71;
                        uint64_t t77 = GM_LD(uintptr_t, ((uint8_t *)&t76) + 8);
                        if (t75 >= (uintptr_t)(t77)) gm_trap_index(t75, t77, gm_src6hq50r4rk7e6, 35, 12);
                        uint8_t t78 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&t76)) + t75 * 1));
                        if (!t78) goto ie68;
                        {
                            void * t80 = gm_l5;
                            GM_ST(uint8_t, (((uint8_t *)t80) + 24), ((uint8_t)1));
                        }
                        goto ix68;
ie68:;
ix68:;
                    }
                    goto me64;
mn65:;
                    gm_trap("no match arm matched the scrutinee", gm_src6hq50r4rk7e6, 34, 5);
me64:;
                }
lc53:;
                {
                    uint64_t t82 = gm_l7;
                    uint64_t r83; bool o84 = __builtin_add_overflow(t82, ((uint64_t)1ULL), &r83);
                    uint64_t t85 = r83;
                    gm_l7 = t85;
                }
            }
lb53:;
        }
    }
    {
        void * t87 = gm_l0;
        void * t88 = gm_l4;
        gm_t88rcucg1b7uk t89 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t88) + 64));
        gm_t53at80pfdus3 t90;
        gm_f_kind_htv6093((void *)&t90, t87, GM_CONV(gm_csguankko4lmhd, t89));
        gm_t53at80pfdus3 t91 = t90;
        uint8_t t92;
        uint8_t t95 = GM_LD(uint8_t, (((uint8_t *)&t91) + 0));
        if (t95 != ((uint8_t)14ULL)) goto mn94;
        t92 = ((uint8_t)1);
        goto me93;
mn94:;
        t92 = ((uint8_t)0);
        goto me93;
mn96:;
        gm_trap("no match arm matched the scrutinee", gm_src6hq50r4rk7e6, 37, 8);
me93:;
        if (!t92) goto ie86;
        {
            void * t98 = gm_l5;
            uint8_t *t99 = (((uint8_t *)t98) + 32);
            uint64_t t100 = (uintptr_t)((uint64_t)0ULL);
            void * t101 = gm_l0;
            void * t102 = gm_l4;
            gm_t88rcucg1b7uk t103 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t102) + 64));
            gm_b16_8 t104 = gm_f_offsets_qpt7c30((void *)NULL, cx, NULL, t101, t103);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_b16_8 t105 = t104;
            uint64_t t106 = GM_LD(uintptr_t, ((uint8_t *)&t105) + 8);
            if (t100 >= (uintptr_t)(t106)) gm_trap_index(t100, t106, gm_src6hq50r4rk7e6, 37, 59);
            uint64_t t107 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&t105)) + t100 * 8));
            gm_tf5srh4mp4b3i t108;
            GM_ST(uint8_t, (uint8_t *)&t108 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t108 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t108 + 12, 0);
            GM_ST(uint64_t, ((uint8_t *)&t108), t107);
            ((uint8_t *)&t108)[8] = 1;
            GM_ST(gm_tf5srh4mp4b3i, t99, t108);
        }
        goto ix86;
ie86:;
ix86:;
    }
    {
        void * t109 = gm_l5;
        void * t110 = gm_l4;
        gm_t88rcucg1b7uk t111 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t110) + 64));
        gm_tkn42u7na3t81 t112;
        GM_ST(uint8_t, (uint8_t *)&t112 + 5, 0);
        GM_ST(uint16_t, (uint8_t *)&t112 + 6, 0);
        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t112), t111);
        ((uint8_t *)&t112)[4] = 1;
        GM_ST(gm_tkn42u7na3t81, (((uint8_t *)t109) + 48), t112);
    }
    {
        uint64_t t113 = gm_l2;
        uint64_t t114 = (uintptr_t)t113;
        void * t115 = gm_l0;
        uint64_t t116 = GM_LD(uintptr_t, ((((uint8_t *)t115) + 720) + 0) + 8);
        if (t114 >= (uintptr_t)(t116)) gm_trap_index(t114, t116, gm_src6hq50r4rk7e6, 40, 5);
        void * t117 = gm_l5;
        GM_ST(void *, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t115) + 720) + 0)) + t114 * 8), t117);
    }
    {
        void * t118 = gm_l5;
        void * t119 = gm_l5;
        uint64_t t120 = GM_LD(uint64_t, (((uint8_t *)t119) + 136));
        GM_ST(uint64_t, (((uint8_t *)t118) + 64), t120);
    }
    {
        void * t122 = gm_l4;
        void * t123 = GM_LD(void *, (((uint8_t *)t122) + 152));
        void * t124 = t123;
        if (!(GM_LD(void *, ((uint8_t *)&t124)) != 0)) goto ie121;
        void * t125 = GM_LD(void *, ((uint8_t *)&t124));
        gm_l9 = t125;
        {
            void * t127 = gm_l0;
            void * t128 = gm_l5;
            void * t129 = gm_l9;
            void * t130 = gm_f_lower_body_g4bo546((void *)NULL, cx, NULL, t127, t128, t129);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l10 = t130;
        }
        {
            void * t131 = gm_l5;
            void * t132 = gm_l10;
            GM_ST(void *, (((uint8_t *)t131) + 0), t132);
        }
        {
            void * t133 = gm_l0;
            void * t134 = gm_l5;
            void * t135 = gm_l10;
            void * t136 = gm_f_vm_compile_gef97rj((void *)NULL, cx, NULL, t133, t134, t135);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l11 = t136;
        }
        {
            void * t137 = gm_l0;
            void * t138 = gm_l5;
            void * t139 = gm_l11;
            gm_b0_1 t140 = gm_f_vm_assemble_k5irtcr((void *)NULL, cx, NULL, t137, t138, t139);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t141 = gm_l5;
            void * t142 = gm_l11;
            GM_ST(void *, (((uint8_t *)t141) + 56), t142);
        }
        {
            void * t143 = gm_l5;
            void * t144 = gm_l11;
            uint64_t t145 = GM_LD(uint64_t, (((uint8_t *)t144) + 104));
            GM_ST(uint64_t, (((uint8_t *)t143) + 64), t145);
        }
        goto ix121;
ie121:;
ix121:;
    }
    {
        void * t146 = gm_l5;
        uint8_t t147 = 0;
        F.ret = t146;
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
__attribute__((aligned(4))) void * gm_f_make_info_aau8il9(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_b16_8 a2, void * a3) {
    struct gm_fr_make_info_aau8il9 F;
    void * gm_l0; /* m */
    uint32_t gm_l1; /* f */
    gm_b16_8 gm_l2; /* locals */
    void * gm_l3; /* body */
    gm_b16_8 gm_l8; /* <tmp> */
    uint64_t gm_l9; /* <tmp> */
    void * gm_l10; /* b */
    uint64_t gm_l11; /* top */
    gm_b16_8 gm_l12; /* <tmp> */
    uint64_t gm_l13; /* <tmp> */
    uint64_t gm_l14; /* i */
    uint64_t gm_l16; /* n */
    uint64_t gm_l17; /* a */
    uint8_t gm_l18; /* word */
    GM_STACK_CHECK(&F, 1136u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        gm_f_list_kl0t9m0((void *)&F.l4, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_f_list_kl0t9m0((void *)&F.l5, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_f_list_kl0t9m0((void *)&F.l6, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_f_list_s83jn2q((void *)&F.l7, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t3 = gm_l2;
            gm_l8 = t3;
        }
        {
            gm_l9 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t5 = gm_l9;
                uint64_t t6 = GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8);
                uint8_t t7 = (t5 < t6);
                if (!t7) goto lb4;
                {
                    uint64_t t10 = gm_l9;
                    uint64_t t11 = (uintptr_t)t10;
                    uint64_t t12 = GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8);
                    {
                        void * t16 = (void *)((uint8_t *)&F.l7);
                        gm_tftb1odc739t5 t17 = gm_f_push_vd1290p(t16, ((uint8_t)0));
                        gm_tftb1odc739t5 t18 = t17;
                        if (((((uint8_t *)&t18))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t18) + 1, gm_src6hq50r4rk7e6, 61, 23);
                        gm_b0_1 t19 = GM_LD(gm_b0_1, (((uint8_t *)&t18) + 1));
                    }
                    goto me13;
mn14:;
                    gm_trap("no match arm matched the scrutinee", gm_src6hq50r4rk7e6, 61, 5);
me13:;
                }
lc4:;
                {
                    uint64_t t21 = gm_l9;
                    uint64_t r22; bool o23 = __builtin_add_overflow(t21, ((uint64_t)1ULL), &r22);
                    uint64_t t24 = r22;
                    gm_l9 = t24;
                }
            }
lb4:;
        }
    }
    {
        void * t26 = gm_l3;
        void * t27 = t26;
        if (!(GM_LD(void *, ((uint8_t *)&t27)) != 0)) goto ie25;
        void * t28 = GM_LD(void *, ((uint8_t *)&t27));
        gm_l10 = t28;
        {
            void * t30 = gm_l0;
            void * t31 = gm_l10;
            gm_b16_8 t32 = GM_LD(gm_b16_8, (((uint8_t *)&F.l7) + 0));
            gm_f_mark_addressed_qtnunkg(t30, t31, t32);
        }
        goto ix25;
ie25:;
ix25:;
    }
    {
        void * t33 = gm_l0;
        uint32_t t34 = gm_l1;
        gm_b16_8 t35 = GM_LD(gm_b16_8, (((uint8_t *)&F.l7) + 0));
        gm_b0_1 t36 = gm_f_mark_captured_addressed_tmp4jtp((void *)NULL, cx, NULL, t33, t34, ((uint32_t)0ULL), t35);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_l11 = ((uint64_t)0ULL);
    }
    {
        {
            gm_b16_8 t38 = gm_l2;
            gm_l12 = t38;
        }
        {
            gm_l13 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t40 = gm_l13;
                uint64_t t41 = GM_LD(uintptr_t, ((uint8_t *)&gm_l12) + 8);
                uint8_t t42 = (t40 < t41);
                if (!t42) goto lb39;
                {
                    gm_t5im2v50m3hk6 t45;
                    uint64_t t46 = gm_l13;
                    GM_ST(uint64_t, (uint8_t *)&t45 + 0, t46);
                    uint64_t t47 = gm_l13;
                    uint64_t t48 = (uintptr_t)t47;
                    uint64_t t49 = GM_LD(uintptr_t, ((uint8_t *)&gm_l12) + 8);
                    memcpy((uint8_t *)&t45 + 8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l12)) + t48 * 40), 40);
                    gm_t5im2v50m3hk6 t50 = t45;
                    uint64_t t53 = GM_LD(uint64_t, (((uint8_t *)&t50) + 0));
                    gm_l14 = t53;
                    gm_tsoie1ppgdvcg t54 = GM_LD(gm_tsoie1ppgdvcg, (((uint8_t *)&t50) + 8));
                    F.l15 = t54;
                    {
                        void * t56 = gm_l0;
                        gm_t88rcucg1b7uk t57 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l15) + 16));
                        uint64_t t58 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t56, t57);
                        if (gm_unw.kind) {
                            goto gm_ll43;
                        }
                        gm_l16 = t58;
                    }
                    {
                        void * t59 = gm_l0;
                        gm_t88rcucg1b7uk t60 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l15) + 16));
                        uint64_t t61 = gm_f_align_15fjgq1((void *)NULL, cx, NULL, t59, t60);
                        if (gm_unw.kind) {
                            goto gm_ll43;
                        }
                        uint64_t t62 = (t61 > ((uint64_t)1ULL) ? t61 : ((uint64_t)1ULL));
                        uint64_t t63 = (t62 < ((uint64_t)8ULL) ? t62 : ((uint64_t)8ULL));
                        gm_l17 = t63;
                    }
                    {
                        uint8_t t64 = 0;
                        uint8_t t65 = 0;
                        uint64_t t66 = gm_l16;
                        uint8_t t67 = (t66 > ((uint64_t)0ULL));
                        if (t67) {
                            uint64_t t68 = gm_l16;
                            uint8_t t69 = (t68 <= ((uint64_t)8ULL));
                            t65 = t69;
                        } else t65 = 0;
                        if (t65) {
                            uint64_t t70 = gm_l14;
                            uint64_t t71 = (uintptr_t)t70;
                            uint64_t t72 = GM_LD(uintptr_t, (((uint8_t *)&F.l7) + 0) + 8);
                            if (t71 >= (uintptr_t)(t72)) gm_trap_index(t71, t72, gm_src6hq50r4rk7e6, 68, 42);
                            uint8_t t73 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l7) + 0)) + t71 * 1));
                            uint8_t t74 = (uint8_t)!t73;
                            t64 = t74;
                        } else t64 = 0;
                        gm_l18 = t64;
                    }
                    {
                        uint8_t t76 = gm_l18;
                        if (!t76) goto ie75;
                        {
                            gm_l17 = ((uint64_t)8ULL);
                        }
                        goto ix75;
ie75:;
ix75:;
                    }
                    {
                        uint64_t t78 = gm_l11;
                        uint64_t t79 = gm_l17;
                        uint64_t r80; bool o81 = __builtin_add_overflow(t78, t79, &r80);
                        if (o81) gm_trap("integer overflow in `+`", gm_src6hq50r4rk7e6, 70, 16);
                        uint64_t t82 = r80;
                        uint64_t r83; bool o84 = __builtin_sub_overflow(t82, ((uint64_t)1ULL), &r83);
                        if (o84) gm_trap("integer overflow in `-`", gm_src6hq50r4rk7e6, 70, 16);
                        uint64_t t85 = r83;
                        uint64_t t86 = gm_l17;
                        if (t86 == 0) gm_trap("division by zero", gm_src6hq50r4rk7e6, 70, 15);
                        uint64_t t87 = (uint64_t)(t85 / t86);
                        uint64_t t88 = gm_l17;
                        uint64_t r89; bool o90 = __builtin_mul_overflow(t87, t88, &r89);
                        if (o90) gm_trap("integer overflow in `*`", gm_src6hq50r4rk7e6, 70, 15);
                        uint64_t t91 = r89;
                        gm_l11 = t91;
                    }
                    {
                        void * t92 = (void *)((uint8_t *)&F.l4);
                        uint64_t t93 = gm_l11;
                        gm_tftb1odc739t5 t94 = gm_f_push_7o6oubd(t92, t93);
                        gm_tftb1odc739t5 t95 = t94;
                        if (((((uint8_t *)&t95))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t95) + 1, gm_src6hq50r4rk7e6, 71, 9);
                        gm_b0_1 t96 = GM_LD(gm_b0_1, (((uint8_t *)&t95) + 1));
                    }
                    {
                        void * t97 = (void *)((uint8_t *)&F.l5);
                        uint64_t t98 = gm_l16;
                        gm_tftb1odc739t5 t99 = gm_f_push_7o6oubd(t97, t98);
                        gm_tftb1odc739t5 t100 = t99;
                        if (((((uint8_t *)&t100))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t100) + 1, gm_src6hq50r4rk7e6, 72, 9);
                        gm_b0_1 t101 = GM_LD(gm_b0_1, (((uint8_t *)&t100) + 1));
                    }
                    {
                        void * t102 = (void *)((uint8_t *)&F.l6);
                        void * t103 = gm_l0;
                        gm_t88rcucg1b7uk t104 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l15) + 16));
                        uint64_t t105 = gm_f_align_15fjgq1((void *)NULL, cx, NULL, t103, t104);
                        if (gm_unw.kind) {
                            goto gm_ll43;
                        }
                        uint64_t t106 = (t105 > ((uint64_t)1ULL) ? t105 : ((uint64_t)1ULL));
                        gm_tftb1odc739t5 t107 = gm_f_push_7o6oubd(t102, t106);
                        gm_tftb1odc739t5 t108 = t107;
                        if (((((uint8_t *)&t108))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t108) + 1, gm_src6hq50r4rk7e6, 73, 9);
                        gm_b0_1 t109 = GM_LD(gm_b0_1, (((uint8_t *)&t108) + 1));
                    }
                    {
                        uint64_t t110 = gm_l11;
                        uint64_t t111;
                        uint8_t t113 = gm_l18;
                        if (!t113) goto ie112;
                        uint64_t t115 = ((uint64_t)8ULL);
                        t111 = t115;
                        goto ix112;
ie112:;
                        uint64_t t117 = gm_l16;
                        uint64_t t118 = t117;
                        t111 = t118;
ix112:;
                        uint64_t r119; bool o120 = __builtin_add_overflow(t110, t111, &r119);
                        if (o120) gm_trap("integer overflow in `+`", gm_src6hq50r4rk7e6, 74, 9);
                        uint64_t t121 = r119;
                        gm_l11 = t121;
                    }
                    goto me51;
mn52:;
                    gm_trap("no match arm matched the scrutinee", gm_src6hq50r4rk7e6, 65, 5);
me51:;
                }
lc39:;
                {
                    uint64_t t123 = gm_l13;
                    uint64_t r124; bool o125 = __builtin_add_overflow(t123, ((uint64_t)1ULL), &r124);
                    uint64_t t126 = r124;
                    gm_l13 = t126;
                }
            }
gm_ll43:;
            if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb39; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc39; }
            }
            goto gm_lad;
lb39:;
        }
    }
    {
        gm_tfe0d7m5kus9m t127;
        memset((uint8_t *)&t127 + 25, 0, 7);
        GM_ST(void *, (uint8_t *)&t127 + 0, ((void *)0));
        gm_b16_8 t128 = GM_LD(gm_b16_8, gm_cjdu4hivd9h);
        GM_ST(gm_b16_8, (uint8_t *)&t127 + 8, t128);
        GM_ST(uint8_t, (uint8_t *)&t127 + 24, ((uint8_t)0));
        gm_tf5srh4mp4b3i t129;
        memset(&t129, 0, sizeof t129);
        GM_ST(gm_tf5srh4mp4b3i, (uint8_t *)&t127 + 32, t129);
        gm_tkn42u7na3t81 t130;
        memset(&t130, 0, sizeof t130);
        GM_ST(gm_tkn42u7na3t81, (uint8_t *)&t127 + 48, t130);
        GM_ST(void *, (uint8_t *)&t127 + 56, ((void *)0));
        GM_ST(uint64_t, (uint8_t *)&t127 + 64, ((uint64_t)0ULL));
        gm_b16_8 t131 = GM_LD(gm_b16_8, (((uint8_t *)&F.l4) + 0));
        GM_ST(gm_b16_8, (uint8_t *)&t127 + 72, t131);
        gm_b16_8 t132 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 0));
        GM_ST(gm_b16_8, (uint8_t *)&t127 + 88, t132);
        gm_b16_8 t133 = GM_LD(gm_b16_8, (((uint8_t *)&F.l6) + 0));
        GM_ST(gm_b16_8, (uint8_t *)&t127 + 104, t133);
        gm_b16_8 t134 = GM_LD(gm_b16_8, (((uint8_t *)&F.l7) + 0));
        GM_ST(gm_b16_8, (uint8_t *)&t127 + 120, t134);
        uint64_t t135 = gm_l11;
        GM_ST(uint64_t, (uint8_t *)&t127 + 136, t135);
        GM_ST(uint64_t, (uint8_t *)&t127 + 144, ((uint64_t)0ULL));
        gm_tq7s98ihonkju t136 = gm_f_new_r6kqujv((void *)NULL, cx, NULL, &t127, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_k7bkj4i7gae)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tq7s98ihonkju t137 = t136;
        if (((((uint8_t *)&t137))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t137) + 8, gm_src6hq50r4rk7e6, 76, 12);
        void * t138 = GM_LD(void *, (((uint8_t *)&t137) + 8));
        uint8_t t139 = 0;
        F.ret = t138;
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
extern __attribute__((aligned(4))) void * gm_f_new_frame_vgm56mt(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, void * a2, gm_b16_8 a3, void * a4, gm_b16_8 a5);
