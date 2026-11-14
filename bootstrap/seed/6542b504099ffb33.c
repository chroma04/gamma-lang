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
typedef struct gm_cs6mnftt383dnt { void * f0; void * f1; } gm_cs6mnftt383dnt;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { uint8_t f0; uint8_t p1[3]; union { int32_t v0; } f1; } gm_toe5fmr3vvf4d;
typedef struct { uint8_t f0; uint8_t f1; } gm_tqs5p4n2tq19v;
_Static_assert(sizeof(gm_cs6mnftt383dnt) == 16 && _Alignof(gm_cs6mnftt383dnt) == 8 && offsetof(gm_cs6mnftt383dnt, f0) == 0 && offsetof(gm_cs6mnftt383dnt, f1) == 8, "layout of `Allocator`");
typedef struct { gm_tl874t2atnlh3 f0; gm_tl874t2atnlh3 f1; } gm_t2p74ki8sl4an;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { gm_b16_8 f0; uint64_t f1; uint64_t f2; uint64_t f3; gm_t3mf58gv9kf3n f4; } gm_t4c7ipjj9s3fp;
typedef struct { gm_b16_8 f0; uint32_t f1; uint32_t f2; } gm_t5hdnnftkup6m;
typedef struct { uint64_t f0; gm_t9qkh0rsooc8d f1; uint8_t p2[7]; int64_t f2; uint32_t f3; uint8_t pe[4]; } gm_t5j14m8ru132p;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t f1; } gm_taari1s0m51a7;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
typedef struct { gm_b16_8 f0; uint32_t f1; uint32_t f2; gm_b16_8 f3; } gm_tb40qn7hk0tl9;
typedef struct { uint64_t f0; gm_b16_8 f1; } gm_tf1bjg81u0glv;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { gm_b16_8 f0; gm_t9qkh0rsooc8d f1; uint8_t pe[7]; } gm_tibupt4dmcqai;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_toe5fmr3vvf4d v0; gm_t9qkh0rsooc8d v1; } f1; } gm_tj5jn79kq9pnm;
typedef struct { gm_t3mf58gv9kf3n f0; void * f1; uint64_t f2; uint64_t f3; uint64_t f4; } gm_tjt4ka19str13;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_toe5fmr3vvf4d) == 8 && _Alignof(gm_toe5fmr3vvf4d) == 4 && offsetof(gm_toe5fmr3vvf4d, f0) == 0 && offsetof(gm_toe5fmr3vvf4d, f1) == 4, "layout of `IoError`");
typedef struct { gm_b16_8 f0; uint32_t f1; uint8_t pe[4]; } gm_tommf4j9upomc;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; void * v1; } f1; } gm_tq7s98ihonkju;
_Static_assert(sizeof(gm_tqs5p4n2tq19v) == 2 && _Alignof(gm_tqs5p4n2tq19v) == 1 && offsetof(gm_tqs5p4n2tq19v, f0) == 0 && offsetof(gm_tqs5p4n2tq19v, f1) == 1, "layout of `(u8, u8)`");
typedef struct { void * f0; gm_b16_8 f1; uint64_t f2; } gm_tqsio0a8q8soi;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { void * f0; gm_b16_8 f1; gm_trqfvmjtt4sij f2; gm_trqfvmjtt4sij f3; gm_trqfvmjtt4sij f4; gm_t4c7ipjj9s3fp f5; gm_t4c7ipjj9s3fp f6; } gm_t1s7atuoc83ls;
_Static_assert(sizeof(gm_t2p74ki8sl4an) == 24 && _Alignof(gm_t2p74ki8sl4an) == 4 && offsetof(gm_t2p74ki8sl4an, f0) == 0 && offsetof(gm_t2p74ki8sl4an, f1) == 12, "layout of `(Span, Span)`");
_Static_assert(sizeof(gm_t4c7ipjj9s3fp) == 56 && _Alignof(gm_t4c7ipjj9s3fp) == 8 && offsetof(gm_t4c7ipjj9s3fp, f0) == 0 && offsetof(gm_t4c7ipjj9s3fp, f1) == 16 && offsetof(gm_t4c7ipjj9s3fp, f2) == 24 && offsetof(gm_t4c7ipjj9s3fp, f3) == 32 && offsetof(gm_t4c7ipjj9s3fp, f4) == 40, "layout of `HashMap(TyKind, u32, null, null)`");
_Static_assert(sizeof(gm_t5hdnnftkup6m) == 24 && _Alignof(gm_t5hdnnftkup6m) == 8 && offsetof(gm_t5hdnnftkup6m, f0) == 0 && offsetof(gm_t5hdnnftkup6m, f1) == 16 && offsetof(gm_t5hdnnftkup6m, f2) == 20, "layout of `(string, u32, u32)`");
_Static_assert(sizeof(gm_t5j14m8ru132p) == 32 && _Alignof(gm_t5j14m8ru132p) == 8 && offsetof(gm_t5j14m8ru132p, f0) == 0 && offsetof(gm_t5j14m8ru132p, f1) == 8 && offsetof(gm_t5j14m8ru132p, f2) == 16 && offsetof(gm_t5j14m8ru132p, f3) == 24, "layout of `FileInfo`");
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_taari1s0m51a7) == 2 && _Alignof(gm_taari1s0m51a7) == 1 && offsetof(gm_taari1s0m51a7, f0) == 0 && offsetof(gm_taari1s0m51a7, f1) == 1, "layout of `?UnOp`");
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
_Static_assert(sizeof(gm_tb40qn7hk0tl9) == 40 && _Alignof(gm_tb40qn7hk0tl9) == 8 && offsetof(gm_tb40qn7hk0tl9, f0) == 0 && offsetof(gm_tb40qn7hk0tl9, f1) == 16 && offsetof(gm_tb40qn7hk0tl9, f2) == 20 && offsetof(gm_tb40qn7hk0tl9, f3) == 24, "layout of `SourceLocation`");
typedef struct { gm_b16_8 f0; gm_t5hdnnftkup6m f1; uint32_t f2; uint32_t f3; } gm_td1bmbv6cf6nk;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_toe5fmr3vvf4d v0; gm_tibupt4dmcqai v1; } f1; } gm_te2n4j2vtm53d;
_Static_assert(sizeof(gm_tf1bjg81u0glv) == 24 && _Alignof(gm_tf1bjg81u0glv) == 8 && offsetof(gm_tf1bjg81u0glv, f0) == 0 && offsetof(gm_tf1bjg81u0glv, f1) == 8, "layout of `(i: usize, x: string)`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tj5jn79kq9pnm v0; gm_b16_8 v1; } f1; } gm_tgvh15qshf643;
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
typedef struct { gm_taari1s0m51a7 f0; gm_tqs5p4n2tq19v f1; uint8_t p2[4]; gm_th2lqbo77pvsc f2; gm_th2lqbo77pvsc f3; gm_th2lqbo77pvsc f4; gm_th2lqbo77pvsc f5; gm_th2lqbo77pvsc f6; gm_th2lqbo77pvsc f7; } gm_ti0g3bmheuq9o;
_Static_assert(sizeof(gm_tibupt4dmcqai) == 24 && _Alignof(gm_tibupt4dmcqai) == 8 && offsetof(gm_tibupt4dmcqai, f0) == 0 && offsetof(gm_tibupt4dmcqai, f1) == 16, "layout of `StrLit`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_toe5fmr3vvf4d v0; gm_t5j14m8ru132p v1; } f1; } gm_tih6h4avv5nrb;
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
typedef struct { gm_tqsio0a8q8soi f0; uint8_t f1; uint8_t pe[7]; } gm_timk834mo3ug9;
typedef struct { gm_trqfvmjtt4sij f0; } gm_tin97tt4ud0s1;
_Static_assert(sizeof(gm_tj5jn79kq9pnm) == 12 && _Alignof(gm_tj5jn79kq9pnm) == 4 && offsetof(gm_tj5jn79kq9pnm, f0) == 0 && offsetof(gm_tj5jn79kq9pnm, f1) == 4, "layout of `ReadError`");
_Static_assert(sizeof(gm_tjt4ka19str13) == 48 && _Alignof(gm_tjt4ka19str13) == 8 && offsetof(gm_tjt4ka19str13, f0) == 0 && offsetof(gm_tjt4ka19str13, f1) == 16 && offsetof(gm_tjt4ka19str13, f2) == 24 && offsetof(gm_tjt4ka19str13, f3) == 32 && offsetof(gm_tjt4ka19str13, f4) == 40, "layout of `Arena`");
typedef struct { gm_b16_8 f0; gm_t9qkh0rsooc8d f1; uint8_t p2[7]; gm_b16_8 f2; gm_th2lqbo77pvsc f3; } gm_tl5cuhjfnc4cr;
typedef struct { gm_b16_8 f0; gm_trqfvmjtt4sij f1; } gm_tl7hpf5nlilqq;
typedef struct { gm_t5hdnnftkup6m f0; gm_t5hdnnftkup6m f1; } gm_tmo31gpro8pgo;
typedef struct { gm_t5hdnnftkup6m f0; uint8_t f1; uint8_t pe[7]; } gm_tnlphins84ju6;
_Static_assert(sizeof(gm_tommf4j9upomc) == 24 && _Alignof(gm_tommf4j9upomc) == 8 && offsetof(gm_tommf4j9upomc, f0) == 0 && offsetof(gm_tommf4j9upomc, f1) == 16, "layout of `(string, u32)`");
_Static_assert(sizeof(gm_tq7s98ihonkju) == 16 && _Alignof(gm_tq7s98ihonkju) == 8 && offsetof(gm_tq7s98ihonkju, f0) == 0 && offsetof(gm_tq7s98ihonkju, f1) == 8, "layout of `AllocError!^HandlerDecl`");
typedef struct { gm_trqfvmjtt4sij f0; uint8_t f1; uint8_t pe[7]; } gm_tqetrgo0q24b5;
_Static_assert(sizeof(gm_tqsio0a8q8soi) == 32 && _Alignof(gm_tqsio0a8q8soi) == 8 && offsetof(gm_tqsio0a8q8soi, f0) == 0 && offsetof(gm_tqsio0a8q8soi, f1) == 8 && offsetof(gm_tqsio0a8q8soi, f2) == 24, "layout of `Entry`");
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
typedef struct { gm_b16_8 f0; gm_th2lqbo77pvsc f1; gm_th2lqbo77pvsc f2; } gm_tt008mgut8glb;
typedef struct { gm_t4c7ipjj9s3fp f0; } gm_ttiitul0263ld;
_Static_assert(sizeof(gm_t1s7atuoc83ls) == 256 && _Alignof(gm_t1s7atuoc83ls) == 8 && offsetof(gm_t1s7atuoc83ls, f0) == 0 && offsetof(gm_t1s7atuoc83ls, f1) == 8 && offsetof(gm_t1s7atuoc83ls, f2) == 24 && offsetof(gm_t1s7atuoc83ls, f3) == 64 && offsetof(gm_t1s7atuoc83ls, f4) == 104 && offsetof(gm_t1s7atuoc83ls, f5) == 144 && offsetof(gm_t1s7atuoc83ls, f6) == 200, "layout of `ModuleFacts`");
_Static_assert(sizeof(gm_td1bmbv6cf6nk) == 48 && _Alignof(gm_td1bmbv6cf6nk) == 8 && offsetof(gm_td1bmbv6cf6nk, f0) == 0 && offsetof(gm_td1bmbv6cf6nk, f1) == 16 && offsetof(gm_td1bmbv6cf6nk, f2) == 40 && offsetof(gm_td1bmbv6cf6nk, f3) == 44, "layout of `LocalName`");
_Static_assert(sizeof(gm_te2n4j2vtm53d) == 32 && _Alignof(gm_te2n4j2vtm53d) == 8 && offsetof(gm_te2n4j2vtm53d, f0) == 0 && offsetof(gm_te2n4j2vtm53d, f1) == 8, "layout of `IoError!Entry`");
typedef struct { gm_tl5cuhjfnc4cr f0; gm_tl874t2atnlh3 f1; uint32_t f2; uint32_t f3; uint8_t pe[4]; } gm_tfgiovingnnea;
_Static_assert(sizeof(gm_tgvh15qshf643) == 24 && _Alignof(gm_tgvh15qshf643) == 8 && offsetof(gm_tgvh15qshf643, f0) == 0 && offsetof(gm_tgvh15qshf643, f1) == 8, "layout of `ReadError![]u8`");
_Static_assert(sizeof(gm_ti0g3bmheuq9o) == 152 && _Alignof(gm_ti0g3bmheuq9o) == 8 && offsetof(gm_ti0g3bmheuq9o, f0) == 0 && offsetof(gm_ti0g3bmheuq9o, f1) == 2 && offsetof(gm_ti0g3bmheuq9o, f2) == 8 && offsetof(gm_ti0g3bmheuq9o, f3) == 32 && offsetof(gm_ti0g3bmheuq9o, f4) == 56 && offsetof(gm_ti0g3bmheuq9o, f5) == 80 && offsetof(gm_ti0g3bmheuq9o, f6) == 104 && offsetof(gm_ti0g3bmheuq9o, f7) == 128, "layout of `BuildSettings`");
_Static_assert(sizeof(gm_tih6h4avv5nrb) == 40 && _Alignof(gm_tih6h4avv5nrb) == 8 && offsetof(gm_tih6h4avv5nrb, f0) == 0 && offsetof(gm_tih6h4avv5nrb, f1) == 8, "layout of `IoError!FileInfo`");
_Static_assert(sizeof(gm_timk834mo3ug9) == 40 && _Alignof(gm_timk834mo3ug9) == 8 && offsetof(gm_timk834mo3ug9, f0) == 0 && offsetof(gm_timk834mo3ug9, f1) == 32, "layout of `?Entry`");
_Static_assert(sizeof(gm_tin97tt4ud0s1) == 40 && _Alignof(gm_tin97tt4ud0s1) == 8 && offsetof(gm_tin97tt4ud0s1, f0) == 0, "layout of `SourceMap`");
_Static_assert(sizeof(gm_tl5cuhjfnc4cr) == 64 && _Alignof(gm_tl5cuhjfnc4cr) == 8 && offsetof(gm_tl5cuhjfnc4cr, f0) == 0 && offsetof(gm_tl5cuhjfnc4cr, f1) == 16 && offsetof(gm_tl5cuhjfnc4cr, f2) == 24 && offsetof(gm_tl5cuhjfnc4cr, f3) == 40, "layout of `Item`");
_Static_assert(sizeof(gm_tl7hpf5nlilqq) == 56 && _Alignof(gm_tl7hpf5nlilqq) == 8 && offsetof(gm_tl7hpf5nlilqq, f0) == 0 && offsetof(gm_tl7hpf5nlilqq, f1) == 16, "layout of `Fix`");
typedef struct { gm_th2lqbo77pvsc f0; gm_th2lqbo77pvsc f1; gm_trqfvmjtt4sij f2; gm_ti0g3bmheuq9o f3; gm_trqfvmjtt4sij f4; gm_trqfvmjtt4sij f5; } gm_tm43kqe3e8b4a;
_Static_assert(sizeof(gm_tmo31gpro8pgo) == 48 && _Alignof(gm_tmo31gpro8pgo) == 8 && offsetof(gm_tmo31gpro8pgo, f0) == 0 && offsetof(gm_tmo31gpro8pgo, f1) == 24, "layout of `RefPair`");
_Static_assert(sizeof(gm_tnlphins84ju6) == 32 && _Alignof(gm_tnlphins84ju6) == 8 && offsetof(gm_tnlphins84ju6, f0) == 0 && offsetof(gm_tnlphins84ju6, f1) == 24, "layout of `?Loc`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tin97tt4ud0s1 v0; gm_b16_8 v1; gm_b16_8 v2; gm_b16_8 v3; } f1; } gm_toq2f2q4opoa3;
_Static_assert(sizeof(gm_tqetrgo0q24b5) == 48 && _Alignof(gm_tqetrgo0q24b5) == 8 && offsetof(gm_tqetrgo0q24b5, f0) == 0 && offsetof(gm_tqetrgo0q24b5, f1) == 40, "layout of `?List(usize)`");
_Static_assert(sizeof(gm_tt008mgut8glb) == 64 && _Alignof(gm_tt008mgut8glb) == 8 && offsetof(gm_tt008mgut8glb, f0) == 0 && offsetof(gm_tt008mgut8glb, f1) == 16 && offsetof(gm_tt008mgut8glb, f2) == 40, "layout of `UrlSource`");
_Static_assert(sizeof(gm_ttiitul0263ld) == 56 && _Alignof(gm_ttiitul0263ld) == 8 && offsetof(gm_ttiitul0263ld, f0) == 0, "layout of `Namer`");
typedef struct { gm_b16_8 f0; gm_toq2f2q4opoa3 f1; gm_th2lqbo77pvsc f2; } gm_te5jk64pjl28k;
_Static_assert(sizeof(gm_tfgiovingnnea) == 88 && _Alignof(gm_tfgiovingnnea) == 8 && offsetof(gm_tfgiovingnnea, f0) == 0 && offsetof(gm_tfgiovingnnea, f1) == 64 && offsetof(gm_tfgiovingnnea, f2) == 76 && offsetof(gm_tfgiovingnnea, f3) == 80, "layout of `LocalScope`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tf1bjg81u0glv v0; gm_tm43kqe3e8b4a v1; } f1; } gm_tjgmtdrk88t7m;
_Static_assert(sizeof(gm_tm43kqe3e8b4a) == 320 && _Alignof(gm_tm43kqe3e8b4a) == 8 && offsetof(gm_tm43kqe3e8b4a, f0) == 0 && offsetof(gm_tm43kqe3e8b4a, f1) == 24 && offsetof(gm_tm43kqe3e8b4a, f2) == 48 && offsetof(gm_tm43kqe3e8b4a, f3) == 88 && offsetof(gm_tm43kqe3e8b4a, f4) == 240 && offsetof(gm_tm43kqe3e8b4a, f5) == 280, "layout of `Manifest`");
_Static_assert(sizeof(gm_toq2f2q4opoa3) == 48 && _Alignof(gm_toq2f2q4opoa3) == 8 && offsetof(gm_toq2f2q4opoa3, f0) == 0 && offsetof(gm_toq2f2q4opoa3, f1) == 8, "layout of `GitSpec`");
_Static_assert(sizeof(gm_te5jk64pjl28k) == 88 && _Alignof(gm_te5jk64pjl28k) == 8 && offsetof(gm_te5jk64pjl28k, f0) == 0 && offsetof(gm_te5jk64pjl28k, f1) == 16 && offsetof(gm_te5jk64pjl28k, f2) == 64, "layout of `GitSource`");
_Static_assert(sizeof(gm_tjgmtdrk88t7m) == 328 && _Alignof(gm_tjgmtdrk88t7m) == 8 && offsetof(gm_tjgmtdrk88t7m, f0) == 0 && offsetof(gm_tjgmtdrk88t7m, f1) == 8, "layout of `ManifestError!Manifest`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; gm_te5jk64pjl28k v1; gm_tt008mgut8glb v2; } f1; } gm_tnlfbo6r4vlhg;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_tnlfbo6r4vlhg f2; } gm_t7tiv6vif86qg;
_Static_assert(sizeof(gm_tnlfbo6r4vlhg) == 96 && _Alignof(gm_tnlfbo6r4vlhg) == 8 && offsetof(gm_tnlfbo6r4vlhg, f0) == 0 && offsetof(gm_tnlfbo6r4vlhg, f1) == 8, "layout of `Source`");
_Static_assert(sizeof(gm_t7tiv6vif86qg) == 120 && _Alignof(gm_t7tiv6vif86qg) == 8 && offsetof(gm_t7tiv6vif86qg, f0) == 0 && offsetof(gm_t7tiv6vif86qg, f1) == 16 && offsetof(gm_t7tiv6vif86qg, f2) == 24, "layout of `Dependency`");
struct gm_fr_add_siblings_38k13fr {
    gm_frame h;
    gm_trqfvmjtt4sij l9; /* names */
    gm_b0_1 ret;
};
struct gm_fr_allocator_5m783ja {
    gm_frame h;
    gm_t3mf58gv9kf3n ret;
};
struct gm_fr_contains_gkki4kq {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_destroy_602qeev {
    gm_frame h;
    gm_tb40qn7hk0tl9 l1; /* site */
    gm_b0_1 ret;
};
struct gm_fr_dir_of_ho73hue {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_26afi2j {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_36vim41 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_5nn7ph0 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_5ns1sqk {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_esq6vjf {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_jaianik {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_k21v01c {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_facts_of_prcp83r {
    gm_frame h;
    void * l0; /* a */
    gm_b16_8 l1; /* dir */
    void * l3; /* facts */
    gm_tfgiovingnnea l12; /* s */
    gm_trqfvmjtt4sij l14; /* top */
    void * ret;
    gm_b0_1 w0;
    gm_b0_1 w1;
};
struct gm_fr_facts_s8nso10 {
    gm_frame h;
    void * ret;
};
struct gm_fr_file_o15fqku {
    gm_frame h;
    void * ret;
};
struct gm_fr_find_module_dirs_5og6qvq {
    gm_frame h;
    gm_b16_8 l0; /* root */
    uint8_t l2; /* has_gm */
    gm_trqfvmjtt4sij l3; /* subdirs */
    gm_b0_1 ret;
    gm_b0_1 w0;
};
struct gm_fr_fn_7eo9uvj {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_fn_la5hvvo {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_fn_vhek9ql {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_fnv_7bujc3t {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_for_body_ghum27l {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_for_body_l1ll8kh {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_for_body_qg33ouo {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_found_text_sajr2an {
    gm_frame h;
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_hash_map_6phbftb {
    gm_frame h;
    gm_t4c7ipjj9s3fp ret;
};
struct gm_fr_hash_map_8h9shb4 {
    gm_frame h;
    gm_t4c7ipjj9s3fp ret;
};
struct gm_fr_hash_map_cuh4fs9 {
    gm_frame h;
    gm_t4c7ipjj9s3fp ret;
};
struct gm_fr_list_1vva2qi {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_epvducg {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_qo41s1n {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_u1fokev {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_loc_equal_h2ruinn {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_loc_key_n6bogvi {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_loc_less_gf4kba8 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_loc_of_q5lnkjn {
    gm_frame h;
    gm_tnlphins84ju6 ret;
};
struct gm_fr_mentions_gartt1q {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_module_dirs_72lhhi5 {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* dirs */
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_module_files_in_249j100 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_new_jr5ejs0 {
    gm_frame h;
    gm_tjt4ka19str13 ret;
};
struct gm_fr_next_name_3cu8kk6 {
    gm_frame h;
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_package_root_q7uuush {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_package_roots_uh3pgnt {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* roots */
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_path_dependencies_270fncm {
    gm_frame h;
    gm_tm43kqe3e8b4a l3; /* manifest */
    gm_t7tiv6vif86qg l6; /* dep */
    gm_b0_1 ret;
};
struct gm_fr_path_less_702ltp5 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_release_7gtbra1 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_release_facts_faaib92 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_skipped_dir_orfng8m {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_sort_by_s9l01h6 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_source_text_7ru912k {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_stamp_aq4ff3c {
    gm_frame h;
    gm_trqfvmjtt4sij l6; /* names */
    uint64_t ret;
};
struct gm_fr_starts_with_ahdikpt {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_store_q18c91o {
    gm_frame h;
    gm_trqfvmjtt4sij l6; /* dirs */
    gm_trqfvmjtt4sij l8; /* kept */
    void * ret;
    gm_b0_1 w0;
};
struct gm_fr_text_in_sgvn4eo {
    gm_frame h;
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_truncate_vir0vu7 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_workspace_ubjeikp {
    gm_frame h;
    gm_ttiitul0263ld ret;
};
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b);
__attribute__((aligned(4))) gm_b0_1 gm_f_add_siblings_38k13fr(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) gm_cs6mnftt383dnt gm_f_allocator_5m783ja(void * a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_canonical_path_q6lq4b6(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void * gm_f_check_module_6elsf9l(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) uint8_t gm_f_contains_gkki4kq(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) uint64_t gm_f_dedup_6a2r2j0(gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_deinit_sejdpqv(void * a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_destroy_602qeev(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_dir_of_ho73hue(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_dupe_g9iqpvn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, const void * a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_26afi2j(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_36vim41(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_5nn7ph0(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_5ns1sqk(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_esq6vjf(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_jaianik(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_k21v01c(void);
__attribute__((aligned(4))) uint8_t gm_f_ends_with_biriu5l(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_entries_44o5n9f_skhefef(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_entries_bdtp5sv_stqor56(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_entries_bdtp5sv_sv0hilk(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void * gm_f_facts_of_prcp83r(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void * gm_f_facts_s8nso10(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void * gm_f_file_o15fqku(void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_find_module_dirs_5og6qvq(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, void * a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_find_nab29co(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_find_package_root_fs4fuqj(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) uint8_t gm_f_fn_7eo9uvj(void * a0, void * a1);
inline __attribute__((aligned(4))) uint8_t gm_f_fn_la5hvvo(void * a0, void * a1);
inline __attribute__((aligned(4))) uint8_t gm_f_fn_vhek9ql(void * a0, void * a1);
__attribute__((aligned(4))) uint64_t gm_f_fnv_7bujc3t(uint64_t a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_for_body_ghum27l(void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_for_body_l1ll8kh(void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_for_body_qg33ouo(void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_45lt1qt(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_found_text_sajr2an(void *gm_out, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_th2lqbo77pvsc gm_f_found_text_sajr2an_c(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_get_rbph851(void *gm_out, void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) void gm_f_hash_map_6phbftb(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_hash_map_8h9shb4(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_hash_map_cuh4fs9(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_introsort_r6p6ckl(gm_b16_8 a0, gm_closure a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_join_qveh5b2(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) void gm_f_list_1vva2qi(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_epvducg(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_qo41s1n(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_u1fokev(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) uint8_t gm_f_loc_equal_h2ruinn(void * a0, void * a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_loc_key_n6bogvi(void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) uint8_t gm_f_loc_less_gf4kba8(void * a0, void * a1);
__attribute__((aligned(4))) void gm_f_loc_of_q5lnkjn(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1);
__attribute__((aligned(4))) uint8_t gm_f_mentions_gartt1q(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_module_dirs_72lhhi5(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) void gm_f_module_files_in_249j100(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_module_files_with_abkh6r4(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_cjjsm4n(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_cqfitgo(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
inline __attribute__((aligned(4))) void gm_f_new_jr5ejs0(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_next_name_3cu8kk6(void *gm_out, gm_b16_8 a0, void * a1);
__attribute__((aligned(4))) gm_th2lqbo77pvsc gm_f_next_name_3cu8kk6_c(gm_b16_8 a0, void * a1);
__attribute__((aligned(4))) void gm_f_overlay_text_haaaurk(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_package_root_q7uuush(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_package_roots_uh3pgnt(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_parent_lk0iero(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_parse_manifest_b61s3ln(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_path_dependencies_270fncm(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, void * a1);
__attribute__((aligned(4))) uint8_t gm_f_path_less_702ltp5(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_3ef746b(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_45feu3o(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_5gfgdpl(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_mt0qbbj(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_5c6qgui(void * a0, gm_b16_8 a1, uint8_t a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_j784qqi(void * a0, gm_b16_8 a1, const void * a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_m40ee64(void * a0, gm_b16_8 a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_read_all_r9rh9qd(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_real_path_al8cv9r(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_release_7gtbra1(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_release_facts_faaib92(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) uint8_t gm_f_skipped_dir_orfng8m(gm_b16_8 a0);
inline __attribute__((aligned(4))) void gm_f_sort_by_s9l01h6(gm_b16_8 a0, gm_closure a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_source_text_7ru912k(void * a0, uint32_t a1);
__attribute__((aligned(4))) void gm_f_split_cbk4ks8(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) uint64_t gm_f_stamp_aq4ff3c(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) uint8_t gm_f_starts_with_6vvt01e(gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) uint8_t gm_f_starts_with_ahdikpt(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_std_root_e0juohb(void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void * gm_f_store_q18c91o(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, void * a2, gm_b16_8 a3);
__attribute__((aligned(4))) void gm_f_strip_prefix_vl3t0h4(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_text_in_sgvn4eo(void *gm_out, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_th2lqbo77pvsc gm_f_text_in_sgvn4eo_c(void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) void gm_f_truncate_vir0vu7(void * a0, uint64_t a1);
inline __attribute__((aligned(4))) void gm_f_workspace_ubjeikp(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline bool gm_lt0iqiubfm7e(const uint8_t *a, const uint8_t *b);
extern uint8_t gm_cjlj93gp0or[24] __attribute__((aligned(8)));
extern void *const gm_hd3t1u951rul[];
extern void *const gm_hlal1cg2mvd1[];
extern void *const gm_hlfkkokngdk1[];
extern void *const gm_hlqklhf369l0[];
extern uint8_t gm_k3h72k4hfps[40] __attribute__((aligned(8)));
extern uint8_t gm_k6dqjucsagv[40] __attribute__((aligned(8)));
extern uint8_t gm_k9o17uvn1g2[40] __attribute__((aligned(8)));
extern uint8_t gm_kbnuij4naoh[40] __attribute__((aligned(8)));
extern uint8_t gm_kbri4nir86d[40] __attribute__((aligned(8)));
extern uint8_t gm_kebr2u4173v[40] __attribute__((aligned(8)));
extern uint8_t gm_kec2ujc0693[40] __attribute__((aligned(8)));
extern uint8_t gm_klhvim4pn5v[40] __attribute__((aligned(8)));
extern uint8_t gm_km2d9tibmvp[40] __attribute__((aligned(8)));
extern uint8_t gm_kpi7tiio4tn[40] __attribute__((aligned(8)));
extern uint8_t gm_ksccp6j96fm[40] __attribute__((aligned(8)));
extern uint8_t gm_ktjao6lvic2[40] __attribute__((aligned(8)));
extern const uint8_t gm_s0ucactj6hj0a[4];
extern const uint8_t gm_sajpbdgkjnse7[8];
extern const uint8_t gm_se24c7hpp401n[11];
extern const uint8_t gm_shqn2dgvujf5o[13];
extern const uint8_t gm_sl3q52ep6aa53[2];
extern const uint8_t gm_snoai0j6d9u36[7];
extern const uint8_t gm_sr4qbrk6tlgaa[1];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_src3d0v42kavhv7[];
extern const char gm_srcb7e7uf3pvdko[];
extern const char gm_srcbsc23is03ttm[];
extern const char gm_srcip69en21ht7v[];
extern const char gm_srcne58m9cqp42d[];
extern const char gm_srcrgc9bkquf6nr[];
extern const char gm_srcv1nri2247d2r[];
extern const uint8_t gm_st3ja0nqi37bv[2];
extern const gm_type gm_tyo86ks9qkvt;
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b) {
    uintptr_t n = GM_LD(uintptr_t, a + 8);
    if (n != GM_LD(uintptr_t, b + 8)) return false;
    const uint8_t *pa = GM_LD(void *, a), *pb = GM_LD(void *, b);
    return n == 0 || memcmp(pa, pb, n * 1) == 0;
}
inline __attribute__((aligned(4))) gm_cs6mnftt383dnt gm_f_allocator_5m783ja(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_allocator_5m783ja F;
    void * gm_l0; /* a */
    GM_STACK_CHECK_SHALLOW(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    void * t2 = t1;
    void * t3 = (void *)((uint8_t *)gm_cjlj93gp0or);
    gm_t3mf58gv9kf3n t4;
    GM_ST(void *, ((uint8_t *)&t4 + 0), t2);
    GM_ST(void *, ((uint8_t *)&t4 + 8), t3);
    F.ret = t4;
gm_fret:
    return GM_CONV(gm_cs6mnftt383dnt, F.ret);
gm_fprop:
    return GM_CONV(gm_cs6mnftt383dnt, F.ret);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_destroy_602qeev(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_destroy_602qeev F;
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
        ((void (*)(void *, void *, uint64_t, uint64_t))GM_PTR_UNTAG(t9))(t6, t8, ((uint64_t)48ULL), ((uint64_t)8ULL));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_dir_of_ho73hue(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_dir_of_ho73hue F;
    gm_b16_8 gm_l0; /* p */
    GM_STACK_CHECK(&F, 160u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t1 = gm_l0;
    gm_th2lqbo77pvsc t2;
    gm_f_parent_lk0iero((void *)&t2, (void *)NULL, cx, NULL, t1);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_th2lqbo77pvsc t3 = t2;
    gm_b16_8 t4;
    if (((((uint8_t *)&t3))[16] != 0)) {
        gm_b16_8 t5 = GM_LD(gm_b16_8, (((uint8_t *)&t3) + 0));
        t4 = t5;
    } else {
        gm_b16_8 t6;
        GM_ST(void *, (uint8_t *)&t6, (void *)(gm_sr4qbrk6tlgaa));
        GM_ST(uintptr_t, (uint8_t *)&t6 + 8, (uintptr_t)(0));
        t4 = t6;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_26afi2j(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_26afi2j F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_36vim41(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_36vim41 F;
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
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 48)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_5ns1sqk(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_5ns1sqk F;
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
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 56)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_esq6vjf(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_esq6vjf F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_jaianik(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_jaianik F;
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
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 48)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_k21v01c(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_k21v01c F;
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
inline __attribute__((aligned(4))) void * gm_f_file_o15fqku(void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_file_o15fqku F;
    void * gm_l0; /* sm */
    uint32_t gm_l1; /* id */
    GM_STACK_CHECK_SHALLOW(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint32_t t1 = gm_l1;
    uint64_t t2 = (uint64_t)t1;
    uint64_t t3 = (uintptr_t)t2;
    void * t4 = gm_l0;
    uint64_t t5 = GM_LD(uintptr_t, ((((uint8_t *)t4) + 0) + 0) + 8);
    if (t3 >= (uintptr_t)(t5)) gm_trap_index(t3, t5, gm_srcv1nri2247d2r, 78, 75);
    void * t6 = (void *)((uint8_t *)GM_LD(void *, ((((uint8_t *)t4) + 0) + 0)) + t3 * 80);
    F.ret = t6;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_fn_7eo9uvj(void * a0, void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_fn_7eo9uvj F;
    void * gm_l0; /* x */
    void * gm_l1; /* y */
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
    uint8_t t5 = gm_f_path_less_702ltp5(t2, t4);
    F.ret = t5;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_fn_la5hvvo(void * a0, void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_fn_la5hvvo F;
    void * gm_l0; /* x */
    void * gm_l1; /* y */
    GM_STACK_CHECK_SHALLOW(&F, 99u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    gm_b16_8 t2 = GM_LD(gm_b16_8, ((uint8_t *)t1));
    void * t3 = gm_l1;
    gm_b16_8 t4 = GM_LD(gm_b16_8, ((uint8_t *)t3));
    gm_b16_8 t5 = t2;
    gm_b16_8 t6 = t4;
    uint8_t t7 = (uint8_t)gm_lt0iqiubfm7e(((uint8_t *)&t5), ((uint8_t *)&t6));
    F.ret = t7;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_fn_vhek9ql(void * a0, void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_fn_vhek9ql F;
    void * gm_l0; /* x */
    void * gm_l1; /* y */
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
    uint8_t t5 = gm_f_path_less_702ltp5(t2, t4);
    F.ret = t5;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_hash_map_6phbftb(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_hash_map_6phbftb F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_b16_8 t2 = gm_f_empty_slice_26afi2j();
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
inline __attribute__((aligned(4))) void gm_f_hash_map_8h9shb4(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_hash_map_8h9shb4 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_b16_8 t2 = gm_f_empty_slice_5ns1sqk();
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
inline __attribute__((aligned(4))) void gm_f_list_1vva2qi(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_1vva2qi F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_jaianik();
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
inline __attribute__((aligned(4))) void gm_f_list_qo41s1n(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_qo41s1n F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_36vim41();
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
inline __attribute__((aligned(4))) void gm_f_list_u1fokev(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_u1fokev F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_esq6vjf();
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
inline __attribute__((aligned(4))) uint8_t gm_f_loc_equal_h2ruinn(void * a0, void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_loc_equal_h2ruinn F;
    void * gm_l0; /* a */
    void * gm_l1; /* b */
    GM_STACK_CHECK_SHALLOW(&F, 151u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint8_t t1 = 0;
    uint8_t t2 = 0;
    void * t3 = gm_l0;
    gm_b16_8 t4 = GM_LD(gm_b16_8, (((uint8_t *)t3) + 0));
    void * t5 = gm_l1;
    gm_b16_8 t6 = GM_LD(gm_b16_8, (((uint8_t *)t5) + 0));
    gm_b16_8 t7 = t4;
    gm_b16_8 t8 = t6;
    uint8_t t9 = (gm_eq0iqiubfm7e(((uint8_t *)&t7), ((uint8_t *)&t8)));
    if (t9) {
        void * t10 = gm_l0;
        uint32_t t11 = GM_LD(uint32_t, (((uint8_t *)t10) + 16));
        void * t12 = gm_l1;
        uint32_t t13 = GM_LD(uint32_t, (((uint8_t *)t12) + 16));
        uint8_t t14 = ((t11 == t13));
        t2 = t14;
    } else t2 = 0;
    if (t2) {
        void * t15 = gm_l0;
        uint32_t t16 = GM_LD(uint32_t, (((uint8_t *)t15) + 20));
        void * t17 = gm_l1;
        uint32_t t18 = GM_LD(uint32_t, (((uint8_t *)t17) + 20));
        uint8_t t19 = ((t16 == t18));
        t1 = t19;
    } else t1 = 0;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_loc_key_n6bogvi(void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_loc_key_n6bogvi F;
    gm_t5hdnnftkup6m gm_l0; /* l */
    GM_STACK_CHECK(&F, 200u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memcpy(&gm_l0, a0, sizeof gm_l0);
    gm_t5hdnnftkup6m t1;
    gm_b16_8 t2 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l0) + 0));
    GM_ST(gm_b16_8, (uint8_t *)&t1 + 0, t2);
    uint32_t t3 = GM_LD(uint32_t, (((uint8_t *)&gm_l0) + 16));
    GM_ST(uint32_t, (uint8_t *)&t1 + 16, t3);
    uint32_t t4 = GM_LD(uint32_t, (((uint8_t *)&gm_l0) + 20));
    GM_ST(uint32_t, (uint8_t *)&t1 + 20, t4);
    gm_tabb710d9mq2e t5;
    gm_f_format_45lt1qt((void *)&t5, (void *)NULL, cx, NULL, &t1);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tabb710d9mq2e t6 = t5;
    if (((((uint8_t *)&t6))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t6) + 8, gm_srcb7e7uf3pvdko, 58, 48);
    gm_b16_8 t7 = GM_LD(gm_b16_8, (((uint8_t *)&t6) + 8));
    gm_b16_8 t8 = t7;
    gm_b16_8 t9 = GM_LD(gm_b16_8, ((uint8_t *)&t8));
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
inline __attribute__((aligned(4))) void gm_f_module_files_in_249j100(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1) {
    struct gm_fr_module_files_in_249j100 F;
    gm_b16_8 gm_l0; /* dir */
    gm_b16_8 gm_l1; /* overlays */
    GM_STACK_CHECK(&F, 281u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        gm_b16_8 t2 = gm_l0;
        gm_b16_8 t3 = gm_l1;
        gm_tqetrgo0q24b5 t4;
        gm_f_module_files_with_abkh6r4((void *)&t4, (void *)NULL, cx, NULL, t2, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tqetrgo0q24b5 t5 = t4;
        gm_trqfvmjtt4sij t6;
        if (((((uint8_t *)&t5))[40] != 0)) {
            gm_trqfvmjtt4sij t7 = GM_LD(gm_trqfvmjtt4sij, (((uint8_t *)&t5) + 0));
            t6 = t7;
        } else {
            gm_trqfvmjtt4sij t8;
            gm_f_list_epvducg((void *)&t8, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            t6 = t8;
        }
        uint8_t t9 = 0;
        F.ret = t6;
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
inline __attribute__((aligned(4))) void gm_f_new_jr5ejs0(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_new_jr5ejs0 F;
    GM_STACK_CHECK(&F, 16u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_node *pn1 = gm_find(cx, 587034433);
    if (!pn1) gm_trap("no handler installed for effect `Alloc`", gm_srcip69en21ht7v, 41, 55);
    gm_t3mf58gv9kf3n t2;
    t2 = ((gm_t3mf58gv9kf3n (*)(void *, gm_node *, gm_node *))pn1->clauses[0])(pn1->env, pn1->hctx, pn1);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    GM_ST(gm_t3mf58gv9kf3n, (uint8_t *)&(*(gm_tjt4ka19str13 *)gm_out) + 0, t2);
    GM_ST(void *, (uint8_t *)&(*(gm_tjt4ka19str13 *)gm_out) + 16, ((void *)0));
    GM_ST(uint64_t, (uint8_t *)&(*(gm_tjt4ka19str13 *)gm_out) + 24, ((uint64_t)0ULL));
    GM_ST(uint64_t, (uint8_t *)&(*(gm_tjt4ka19str13 *)gm_out) + 32, ((uint64_t)0ULL));
    GM_ST(uint64_t, (uint8_t *)&(*(gm_tjt4ka19str13 *)gm_out) + 40, ((uint64_t)0ULL));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_package_root_q7uuush(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_package_root_q7uuush F;
    gm_b16_8 gm_l0; /* dir */
    GM_STACK_CHECK(&F, 160u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t1 = gm_l0;
    gm_th2lqbo77pvsc t2;
    gm_f_find_package_root_fs4fuqj((void *)&t2, (void *)NULL, cx, NULL, t1);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_th2lqbo77pvsc t3 = t2;
    gm_b16_8 t4;
    if (((((uint8_t *)&t3))[16] != 0)) {
        gm_b16_8 t5 = GM_LD(gm_b16_8, (((uint8_t *)&t3) + 0));
        t4 = t5;
    } else {
        gm_b16_8 t6 = gm_l0;
        t4 = t6;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_release_7gtbra1(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_release_7gtbra1 F;
    void * gm_l0; /* a */
    void * gm_l1; /* ar */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 0));
        gm_l1 = t3;
    }
    {
        void * t4 = gm_l1;
        gm_f_deinit_sejdpqv(t4);
    }
    {
        void * t5 = gm_l1;
        gm_b0_1 t6 = gm_f_destroy_602qeev((void *)NULL, cx, NULL, t5, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_k3h72k4hfps)));
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_release_facts_faaib92(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_release_facts_faaib92 F;
    void * gm_l0; /* f */
    void * gm_l1; /* ar */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 0));
        gm_l1 = t3;
    }
    {
        void * t4 = gm_l1;
        gm_f_deinit_sejdpqv(t4);
    }
    {
        void * t5 = gm_l1;
        gm_b0_1 t6 = gm_f_destroy_602qeev((void *)NULL, cx, NULL, t5, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kpi7tiio4tn)));
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
inline __attribute__((aligned(4))) uint8_t gm_f_skipped_dir_orfng8m(gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_skipped_dir_orfng8m F;
    gm_b16_8 gm_l0; /* name */
    GM_STACK_CHECK_SHALLOW(&F, 55u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t t1 = 0;
    uint8_t t2 = 0;
    gm_b16_8 t3 = gm_l0;
    gm_b16_8 t4;
    GM_ST(void *, (uint8_t *)&t4, (void *)(gm_sl3q52ep6aa53));
    GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(1));
    uint8_t t5 = gm_f_starts_with_6vvt01e(t3, t4);
    if (!t5) {
        uint8_t t6 = (((GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8) == 6) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l0)), gm_snoai0j6d9u36, 6) == 0));
        t2 = t6;
    } else t2 = 1;
    if (!t2) {
        uint8_t t7 = (((GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8) == 12) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l0)), gm_shqn2dgvujf5o, 12) == 0));
        t1 = t7;
    } else t1 = 1;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_sort_by_s9l01h6(gm_b16_8 a0, gm_closure a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_sort_by_s9l01h6 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_source_text_7ru912k(void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_source_text_7ru912k F;
    void * gm_l0; /* a */
    uint32_t gm_l1; /* file */
    GM_STACK_CHECK_SHALLOW(&F, 88u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    void * t2 = (void *)(((uint8_t *)t1) + 8);
    uint32_t t3 = gm_l1;
    void * t4 = gm_f_file_o15fqku(t2, t3);
    gm_b16_8 t5 = GM_LD(gm_b16_8, (((uint8_t *)t4) + 16));
    F.ret = t5;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_starts_with_ahdikpt(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1) {
    struct gm_fr_starts_with_ahdikpt F;
    gm_b16_8 gm_l0; /* p */
    gm_b16_8 gm_l1; /* base */
    GM_STACK_CHECK(&F, 115u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_b16_8 t1 = gm_l0;
    gm_b16_8 t2 = gm_l1;
    gm_th2lqbo77pvsc t3;
    gm_f_strip_prefix_vl3t0h4((void *)&t3, (void *)NULL, cx, NULL, t1, t2);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_th2lqbo77pvsc t4 = t3;
    uint8_t t5;
    if (((((uint8_t *)&t4))[16] != 0)) goto mn7;
    t5 = ((uint8_t)0);
    goto me6;
mn7:;
    t5 = ((uint8_t)1);
    goto me6;
mn8:;
    gm_trap("no match arm matched the scrutinee", gm_srcne58m9cqp42d, 120, 72);
me6:;
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
inline __attribute__((aligned(4))) void gm_f_truncate_vir0vu7(void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_truncate_vir0vu7 F;
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
            if (t12 > t14 || t14 > (uintptr_t)t10) gm_trap_slice(t12, t14, t10, gm_srcrgc9bkquf6nr, 122, 36);
            gm_b16_8 t15;
            GM_ST(void *, (uint8_t *)&t15, (void *)(((uint8_t *)t11 + t12 * 16)));
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
inline __attribute__((aligned(4))) void gm_f_workspace_ubjeikp(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_workspace_ubjeikp F;
    GM_STACK_CHECK(&F, 56u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_t4c7ipjj9s3fp t1;
    gm_f_hash_map_8h9shb4((void *)&t1, (void *)NULL, cx, NULL);
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
inline bool gm_lt0iqiubfm7e(const uint8_t *a, const uint8_t *b) {
    uintptr_t na = GM_LD(uintptr_t, a + 8), nb = GM_LD(uintptr_t, b + 8);
    const uint8_t *pa = GM_LD(void *, a), *pb = GM_LD(void *, b);
    uintptr_t m = na < nb ? na : nb;
    int c = m == 0 ? 0 : memcmp(pa, pb, m);
    return c != 0 ? c < 0 : na < nb;
}
__attribute__((aligned(4))) gm_b0_1 gm_f_add_siblings_38k13fr(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2, gm_b16_8 a3) {
    struct gm_fr_add_siblings_38k13fr F;
    void * gm_l0; /* f */
    gm_b16_8 gm_l1; /* path */
    gm_b16_8 gm_l2; /* syms */
    gm_b16_8 gm_l3; /* group */
    uint64_t gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    uint64_t gm_l6; /* i */
    void * gm_l7; /* s */
    gm_t5hdnnftkup6m gm_l8; /* loc */
    uint64_t gm_l10; /* <tmp> */
    uint64_t gm_l11; /* <tmp> */
    uint64_t gm_l12; /* j */
    GM_STACK_CHECK(&F, 650u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        {
            gm_l5 = ((uint64_t)0ULL);
        }
        {
            uint64_t t3 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
            gm_l4 = t3;
        }
        {
            for (;;) {
                uint64_t t5 = gm_l5;
                uint64_t t6 = gm_l4;
                uint8_t t7 = (t5 < t6);
                if (!t7) goto lb4;
                {
                    uint64_t t10 = gm_l5;
                    uint64_t t11 = t10;
                    uint64_t t14 = GM_LD(uint64_t, ((uint8_t *)&t11));
                    gm_l6 = t14;
                    {
                        uint64_t t16 = gm_l6;
                        uint64_t t17 = (uintptr_t)t16;
                        uint64_t t18 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                        if (t17 >= (uintptr_t)(t18)) gm_trap_index(t17, t18, gm_srcb7e7uf3pvdko, 143, 15);
                        void * t19 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t17 * 80);
                        gm_l7 = t19;
                    }
                    {
                        gm_b16_8 t20 = gm_l1;
                        GM_ST(gm_b16_8, (uint8_t *)&gm_l8 + 0, t20);
                        void * t21 = gm_l7;
                        uint32_t t22 = GM_LD(uint32_t, ((((uint8_t *)t21) + 52) + 4));
                        GM_ST(uint32_t, (uint8_t *)&gm_l8 + 16, t22);
                        void * t23 = gm_l7;
                        uint32_t t24 = GM_LD(uint32_t, ((((uint8_t *)t23) + 52) + 8));
                        GM_ST(uint32_t, (uint8_t *)&gm_l8 + 20, t24);
                    }
                    {
                        void * t25 = gm_l0;
                        void * t26 = (void *)(((uint8_t *)t25) + 144);
                        gm_b16_8 t27 = gm_f_loc_key_n6bogvi((void *)NULL, cx, NULL, &(*(gm_t5hdnnftkup6m *)((uint8_t *)&gm_l8)));
                        if (gm_unw.kind) {
                            goto gm_ll8;
                        }
                        gm_b16_8 t28 = gm_l3;
                        gm_tftb1odc739t5 t29 = gm_f_put_m40ee64(t26, t27, t28);
                        gm_tftb1odc739t5 t30 = t29;
                        if (((((uint8_t *)&t30))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t30) + 1, gm_srcb7e7uf3pvdko, 145, 9);
                        gm_b0_1 t31 = GM_LD(gm_b0_1, (((uint8_t *)&t30) + 1));
                    }
                    {
                        gm_f_list_epvducg((void *)&F.l9, (void *)NULL, cx, NULL);
                        if (gm_unw.kind) {
                            goto gm_ll8;
                        }
                    }
                    {
                        {
                            gm_l11 = ((uint64_t)0ULL);
                        }
                        {
                            void * t33 = gm_l7;
                            uint64_t t34 = GM_LD(uintptr_t, (((uint8_t *)t33) + 64) + 8);
                            gm_l10 = t34;
                        }
                        {
                            for (;;) {
                                uint64_t t36 = gm_l11;
                                uint64_t t37 = gm_l10;
                                uint8_t t38 = (t36 < t37);
                                if (!t38) goto lb35;
                                {
                                    uint64_t t41 = gm_l11;
                                    uint64_t t42 = t41;
                                    uint64_t t45 = GM_LD(uint64_t, ((uint8_t *)&t42));
                                    gm_l12 = t45;
                                    {
                                        void * t47 = (void *)((uint8_t *)&F.l9);
                                        uint64_t t48 = gm_l12;
                                        uint64_t t49 = (uintptr_t)t48;
                                        void * t50 = gm_l7;
                                        uint64_t t51 = GM_LD(uintptr_t, (((uint8_t *)t50) + 64) + 8);
                                        if (t49 >= (uintptr_t)(t51)) gm_trap_index(t49, t51, gm_srcb7e7uf3pvdko, 147, 54);
                                        gm_b16_8 t52 = GM_LD(gm_b16_8, (((uint8_t *)GM_LD(void *, (((uint8_t *)t50) + 64)) + t49 * 80) + 0));
                                        gm_tabb710d9mq2e t53;
                                        gm_f_dupe_g9iqpvn((void *)&t53, (void *)NULL, cx, NULL, t52, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kebr2u4173v)));
                                        if (gm_unw.kind) {
                                            goto gm_ll39;
                                        }
                                        gm_tabb710d9mq2e t54 = t53;
                                        if (((((uint8_t *)&t54))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t54) + 8, gm_srcb7e7uf3pvdko, 147, 49);
                                        gm_b16_8 t55 = GM_LD(gm_b16_8, (((uint8_t *)&t54) + 8));
                                        gm_b16_8 t56 = t55;
                                        gm_b16_8 t57 = GM_LD(gm_b16_8, ((uint8_t *)&t56));
                                        gm_tftb1odc739t5 t58 = gm_f_push_3ef746b(t47, t57);
                                        gm_tftb1odc739t5 t59 = t58;
                                        if (((((uint8_t *)&t59))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t59) + 1, gm_srcb7e7uf3pvdko, 147, 38);
                                        gm_b0_1 t60 = GM_LD(gm_b0_1, (((uint8_t *)&t59) + 1));
                                    }
                                    goto me43;
mn44:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcb7e7uf3pvdko, 147, 9);
me43:;
                                }
lc35:;
                                {
                                    uint64_t t62 = gm_l11;
                                    uint64_t r63; bool o64 = __builtin_add_overflow(t62, ((uint64_t)1ULL), &r63);
                                    uint64_t t65 = r63;
                                    gm_l11 = t65;
                                }
                            }
gm_ll39:;
                            if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb35; }
                                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc35; }
                            }
                            goto gm_ll8;
lb35:;
                        }
                    }
                    {
                        void * t66 = gm_l0;
                        gm_b16_8 t67 = gm_l1;
                        void * t68 = gm_l7;
                        gm_b16_8 t69 = GM_LD(gm_b16_8, (((uint8_t *)t68) + 64));
                        gm_b16_8 t70 = t69;
                        gm_b16_8 t71 = GM_LD(gm_b16_8, ((uint8_t *)&t70));
                        gm_b16_8 t72 = GM_LD(gm_b16_8, (((uint8_t *)&F.l9) + 0));
                        gm_b0_1 t73 = gm_f_add_siblings_38k13fr((void *)NULL, cx, NULL, t66, t67, t71, t72);
                        if (gm_unw.kind) {
                            goto gm_ll8;
                        }
                    }
                    goto me12;
mn13:;
                    gm_trap("no match arm matched the scrutinee", gm_srcb7e7uf3pvdko, 142, 5);
me12:;
                }
lc4:;
                {
                    uint64_t t75 = gm_l5;
                    uint64_t r76; bool o77 = __builtin_add_overflow(t75, ((uint64_t)1ULL), &r76);
                    uint64_t t78 = r76;
                    gm_l5 = t78;
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
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) void * gm_f_facts_of_prcp83r(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1) {
    struct gm_fr_facts_of_prcp83r F;
    void * gm_l2; /* ar */
    void * gm_l4; /* <tmp> */
    gm_b16_8 gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    gm_t2p74ki8sl4an gm_l7; /* r */
    gm_t5hdnnftkup6m gm_l8; /* from */
    gm_t5hdnnftkup6m gm_l9; /* to */
    gm_b16_8 gm_l10; /* <tmp> */
    uint64_t gm_l11; /* <tmp> */
    gm_t5hdnnftkup6m gm_l13; /* def */
    gm_b16_8 gm_l15; /* <tmp> */
    uint64_t gm_l16; /* <tmp> */
    gm_tiilmfd16v3fc gm_l17; /* o */
    uint64_t gm_l18; /* <tmp> */
    uint64_t gm_l19; /* <tmp> */
    uint64_t gm_l20; /* i */
    gm_b16_8 gm_l21; /* <tmp> */
    uint64_t gm_l22; /* <tmp> */
    gm_b16_8 gm_l23; /* n */
    gm_b16_8 gm_l24; /* <tmp> */
    uint64_t gm_l25; /* <tmp> */
    gm_tiilmfd16v3fc gm_l26; /* o */
    gm_b16_8 gm_l27; /* path */
    GM_STACK_CHECK(&F, 2761u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    F.l1 = a1;
    {
        gm_tjt4ka19str13 t2;
        gm_f_new_jr5ejs0((void *)&t2, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tq7s98ihonkju t3 = gm_f_new_cqfitgo((void *)NULL, cx, NULL, &t2, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_klhvim4pn5v)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tq7s98ihonkju t4 = t3;
        if (((((uint8_t *)&t4))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t4) + 8, gm_srcb7e7uf3pvdko, 101, 11);
        void * t5 = GM_LD(void *, (((uint8_t *)&t4) + 8));
        gm_l2 = t5;
    }
    {
        void * t6 = gm_l2;
        gm_t3mf58gv9kf3n t7 = GM_CONV(gm_t3mf58gv9kf3n, gm_f_allocator_5m783ja(t6));
        gm_t3mf58gv9kf3n t8 = t7;
        gm_node wn9 = {.eff = 587034433, .kind = GM_DECL, .clauses = gm_hd3t1u951rul, .env = (void *)&t8, .hctx = cx, .with_frame = &F.h, .with_label = 0, .result = (void *)&F.w0, .fw = NULL, .next = cx};
        {
            gm_t1s7atuoc83ls t13;
            void * t14 = gm_l2;
            GM_ST(void *, (uint8_t *)&t13 + 0, t14);
            gm_b16_8 t15 = F.l1;
            gm_tabb710d9mq2e t16;
            gm_f_dupe_g9iqpvn((void *)&t16, (void *)NULL, (&wn9), NULL, t15, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kbnuij4naoh)));
            if (gm_unw.kind) {
                goto gm_ll11;
            }
            gm_tabb710d9mq2e t17 = t16;
            if (((((uint8_t *)&t17))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t17) + 8, gm_srcb7e7uf3pvdko, 105, 18);
            gm_b16_8 t18 = GM_LD(gm_b16_8, (((uint8_t *)&t17) + 8));
            gm_b16_8 t19 = t18;
            gm_b16_8 t20 = GM_LD(gm_b16_8, ((uint8_t *)&t19));
            GM_ST(gm_b16_8, (uint8_t *)&t13 + 8, t20);
            gm_f_list_qo41s1n((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&t13 + 24)), (void *)NULL, (&wn9), NULL);
            if (gm_unw.kind) {
                goto gm_ll11;
            }
            gm_f_list_u1fokev((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&t13 + 64)), (void *)NULL, (&wn9), NULL);
            if (gm_unw.kind) {
                goto gm_ll11;
            }
            gm_f_list_1vva2qi((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&t13 + 104)), (void *)NULL, (&wn9), NULL);
            if (gm_unw.kind) {
                goto gm_ll11;
            }
            gm_f_hash_map_6phbftb((void *)&(*(gm_t4c7ipjj9s3fp *)((uint8_t *)&t13 + 144)), (void *)NULL, (&wn9), NULL);
            if (gm_unw.kind) {
                goto gm_ll11;
            }
            gm_f_hash_map_cuh4fs9((void *)&(*(gm_t4c7ipjj9s3fp *)((uint8_t *)&t13 + 200)), (void *)NULL, (&wn9), NULL);
            if (gm_unw.kind) {
                goto gm_ll11;
            }
            gm_tq7s98ihonkju t21 = gm_f_new_cjjsm4n((void *)NULL, (&wn9), NULL, &t13, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_ksccp6j96fm)));
            if (gm_unw.kind) {
                goto gm_ll11;
            }
            gm_tq7s98ihonkju t22 = t21;
            if (((((uint8_t *)&t22))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t22) + 8, gm_srcb7e7uf3pvdko, 103, 18);
            void * t23 = GM_LD(void *, (((uint8_t *)&t22) + 8));
            F.l3 = t23;
        }
        {
            {
                void * t25 = F.l0;
                void * t26 = (void *)(((uint8_t *)t25) + 480);
                gm_l4 = t26;
            }
            gm_hval t27 = ((gm_hval){(void *)&F.h, (&wn9)});
            gm_node wn28 = {.eff = 1575796949, .kind = GM_LIT, .clauses = gm_hlal1cg2mvd1, .env = t27.env, .hctx = t27.cx, .with_frame = &F.h, .with_label = 1, .result = (void *)&F.w1, .fw = NULL, .next = (&wn9)};
            void * t31 = gm_l4;
            gm_b0_1 t32 = gm_f_entries_bdtp5sv_stqor56((void *)NULL, (&wn28), NULL, t31);
            if (gm_unw.kind) {
                goto gm_ll30;
            }
            F.w1 = t32;
            goto gm_ls33;
gm_ll30:;
            if (gm_unw.kind == GM_BREAK && gm_unw.frame == &F.h && gm_unw.label == 1) { gm_unw.kind = GM_NONE; goto we29; }
            goto gm_ll11;
gm_ls33:;
we29:;
            gm_b0_1 t34 = F.w1;
            gm_b0_1 t35 = t34;
        }
        {
            {
                void * t37 = F.l0;
                gm_b16_8 t38 = GM_LD(gm_b16_8, (((((uint8_t *)t37) + 120) + 0) + 0));
                gm_l5 = t38;
            }
            {
                gm_l6 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t40 = gm_l6;
                    uint64_t t41 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                    uint8_t t42 = (t40 < t41);
                    if (!t42) goto lb39;
                    {
                        uint64_t t45 = gm_l6;
                        uint64_t t46 = (uintptr_t)t45;
                        uint64_t t47 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                        gm_t2p74ki8sl4an t50 = GM_LD(gm_t2p74ki8sl4an, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l5)) + t46 * 24));
                        gm_l7 = t50;
                        {
                            void * t52 = F.l0;
                            gm_tl874t2atnlh3 t53 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l7) + 0));
                            gm_tnlphins84ju6 t54;
                            gm_f_loc_of_q5lnkjn((void *)&t54, (void *)NULL, (&wn9), NULL, t52, t53);
                            if (gm_unw.kind) {
                                goto gm_ll43;
                            }
                            gm_tnlphins84ju6 t55 = t54;
                            gm_t5hdnnftkup6m t56;
                            if (((((uint8_t *)&t55))[24] != 0)) {
                                gm_t5hdnnftkup6m t57 = GM_LD(gm_t5hdnnftkup6m, (((uint8_t *)&t55) + 0));
                                t56 = t57;
                            } else {
                                goto lc39;
                            }
                            gm_l8 = t56;
                        }
                        {
                            void * t58 = F.l0;
                            gm_tl874t2atnlh3 t59 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l7) + 12));
                            gm_tnlphins84ju6 t60;
                            gm_f_loc_of_q5lnkjn((void *)&t60, (void *)NULL, (&wn9), NULL, t58, t59);
                            if (gm_unw.kind) {
                                goto gm_ll43;
                            }
                            gm_tnlphins84ju6 t61 = t60;
                            gm_t5hdnnftkup6m t62;
                            if (((((uint8_t *)&t61))[24] != 0)) {
                                gm_t5hdnnftkup6m t63 = GM_LD(gm_t5hdnnftkup6m, (((uint8_t *)&t61) + 0));
                                t62 = t63;
                            } else {
                                goto lc39;
                            }
                            gm_l9 = t62;
                        }
                        {
                            gm_b16_8 t65 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l8) + 0));
                            gm_b16_8 t66 = gm_f_dir_of_ho73hue((void *)NULL, (&wn9), NULL, t65);
                            if (gm_unw.kind) {
                                goto gm_ll43;
                            }
                            gm_b16_8 t67 = F.l1;
                            gm_b16_8 t68 = t66;
                            gm_b16_8 t69 = t67;
                            uint8_t t70 = (gm_eq0iqiubfm7e(((uint8_t *)&t68), ((uint8_t *)&t69)));
                            if (!t70) goto ie64;
                            {
                                void * t72 = F.l3;
                                void * t73 = (void *)(((uint8_t *)t72) + 24);
                                gm_tmo31gpro8pgo t74;
                                memcpy((uint8_t *)&t74 + 0, ((uint8_t *)&gm_l8), 24);
                                memcpy((uint8_t *)&t74 + 24, ((uint8_t *)&gm_l9), 24);
                                gm_tftb1odc739t5 t75 = gm_f_push_45feu3o(t73, &t74);
                                gm_tftb1odc739t5 t76 = t75;
                                if (((((uint8_t *)&t76))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t76) + 1, gm_srcb7e7uf3pvdko, 120, 43);
                                gm_b0_1 t77 = GM_LD(gm_b0_1, (((uint8_t *)&t76) + 1));
                            }
                            goto ix64;
ie64:;
ix64:;
                        }
                        goto me48;
mn49:;
                        gm_trap("no match arm matched the scrutinee", gm_srcb7e7uf3pvdko, 117, 9);
me48:;
                    }
lc39:;
                    {
                        uint64_t t79 = gm_l6;
                        uint64_t r80; bool o81 = __builtin_add_overflow(t79, ((uint64_t)1ULL), &r80);
                        uint64_t t82 = r80;
                        gm_l6 = t82;
                    }
                }
gm_ll43:;
                if (gm_unw.frame == &F.h && gm_unw.label == 2) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb39; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc39; }
                }
                goto gm_ll11;
lb39:;
            }
        }
        {
            {
                void * t84 = F.l0;
                gm_b16_8 t85 = GM_LD(gm_b16_8, (((((uint8_t *)t84) + 120) + 136) + 0));
                gm_l10 = t85;
            }
            {
                gm_l11 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t87 = gm_l11;
                    uint64_t t88 = GM_LD(uintptr_t, ((uint8_t *)&gm_l10) + 8);
                    uint8_t t89 = (t87 < t88);
                    if (!t89) goto lb86;
                    {
                        uint64_t t92 = gm_l11;
                        uint64_t t93 = (uintptr_t)t92;
                        uint64_t t94 = GM_LD(uintptr_t, ((uint8_t *)&gm_l10) + 8);
                        gm_tfgiovingnnea t97 = GM_LD(gm_tfgiovingnnea, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l10)) + t93 * 88));
                        F.l12 = t97;
                        {
                            void * t99 = F.l0;
                            gm_tl874t2atnlh3 t100 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l12) + 64));
                            gm_tnlphins84ju6 t101;
                            gm_f_loc_of_q5lnkjn((void *)&t101, (void *)NULL, (&wn9), NULL, t99, t100);
                            if (gm_unw.kind) {
                                goto gm_ll90;
                            }
                            gm_tnlphins84ju6 t102 = t101;
                            gm_t5hdnnftkup6m t103;
                            if (((((uint8_t *)&t102))[24] != 0)) {
                                gm_t5hdnnftkup6m t104 = GM_LD(gm_t5hdnnftkup6m, (((uint8_t *)&t102) + 0));
                                t103 = t104;
                            } else {
                                goto lc86;
                            }
                            gm_l13 = t103;
                        }
                        {
                            gm_b16_8 t106 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l13) + 0));
                            gm_b16_8 t107 = gm_f_dir_of_ho73hue((void *)NULL, (&wn9), NULL, t106);
                            if (gm_unw.kind) {
                                goto gm_ll90;
                            }
                            gm_b16_8 t108 = F.l1;
                            gm_b16_8 t109 = t107;
                            gm_b16_8 t110 = t108;
                            uint8_t t111 = (gm_eq0iqiubfm7e(((uint8_t *)&t109), ((uint8_t *)&t110)));
                            if (!t111) goto ie105;
                            {
                                void * t113 = F.l3;
                                void * t114 = (void *)(((uint8_t *)t113) + 104);
                                gm_td1bmbv6cf6nk t115;
                                gm_b16_8 t116 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l12) + 0) + 0));
                                gm_tabb710d9mq2e t117;
                                gm_f_dupe_g9iqpvn((void *)&t117, (void *)NULL, (&wn9), NULL, t116, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kbri4nir86d)));
                                if (gm_unw.kind) {
                                    goto gm_ll90;
                                }
                                gm_tabb710d9mq2e t118 = t117;
                                if (((((uint8_t *)&t118))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t118) + 8, gm_srcb7e7uf3pvdko, 125, 52);
                                gm_b16_8 t119 = GM_LD(gm_b16_8, (((uint8_t *)&t118) + 8));
                                gm_b16_8 t120 = t119;
                                gm_b16_8 t121 = GM_LD(gm_b16_8, ((uint8_t *)&t120));
                                GM_ST(gm_b16_8, (uint8_t *)&t115 + 0, t121);
                                memcpy((uint8_t *)&t115 + 16, ((uint8_t *)&gm_l13), 24);
                                uint32_t t122 = GM_LD(uint32_t, (((uint8_t *)&F.l12) + 76));
                                GM_ST(uint32_t, (uint8_t *)&t115 + 40, t122);
                                uint32_t t123 = GM_LD(uint32_t, (((uint8_t *)&F.l12) + 80));
                                GM_ST(uint32_t, (uint8_t *)&t115 + 44, t123);
                                gm_tftb1odc739t5 t124 = gm_f_push_mt0qbbj(t114, &t115);
                                gm_tftb1odc739t5 t125 = t124;
                                if (((((uint8_t *)&t125))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t125) + 1, gm_srcb7e7uf3pvdko, 125, 17);
                                gm_b0_1 t126 = GM_LD(gm_b0_1, (((uint8_t *)&t125) + 1));
                            }
                            goto ix105;
ie105:;
ix105:;
                        }
                        goto me95;
mn96:;
                        gm_trap("no match arm matched the scrutinee", gm_srcb7e7uf3pvdko, 122, 9);
me95:;
                    }
lc86:;
                    {
                        uint64_t t128 = gm_l11;
                        uint64_t r129; bool o130 = __builtin_add_overflow(t128, ((uint64_t)1ULL), &r129);
                        uint64_t t131 = r129;
                        gm_l11 = t131;
                    }
                }
gm_ll90:;
                if (gm_unw.frame == &F.h && gm_unw.label == 3) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb86; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc86; }
                }
                goto gm_ll11;
lb86:;
            }
        }
        {
            gm_f_list_epvducg((void *)&F.l14, (void *)NULL, (&wn9), NULL);
            if (gm_unw.kind) {
                goto gm_ll11;
            }
        }
        {
            {
                void * t133 = F.l0;
                gm_b16_8 t134 = GM_LD(gm_b16_8, (((((uint8_t *)t133) + 120) + 96) + 0));
                gm_l15 = t134;
            }
            {
                gm_l16 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t136 = gm_l16;
                    uint64_t t137 = GM_LD(uintptr_t, ((uint8_t *)&gm_l15) + 8);
                    uint8_t t138 = (t136 < t137);
                    if (!t138) goto lb135;
                    {
                        uint64_t t141 = gm_l16;
                        uint64_t t142 = (uintptr_t)t141;
                        uint64_t t143 = GM_LD(uintptr_t, ((uint8_t *)&gm_l15) + 8);
                        gm_tiilmfd16v3fc t146 = GM_LD(gm_tiilmfd16v3fc, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l15)) + t142 * 24));
                        gm_l17 = t146;
                        {
                            {
                                gm_l19 = ((uint64_t)0ULL);
                            }
                            {
                                uint64_t t149 = GM_LD(uintptr_t, (((uint8_t *)&gm_l17) + 8) + 8);
                                gm_l18 = t149;
                            }
                            {
                                for (;;) {
                                    uint64_t t151 = gm_l19;
                                    uint64_t t152 = gm_l18;
                                    uint8_t t153 = (t151 < t152);
                                    if (!t153) goto lb150;
                                    {
                                        uint64_t t156 = gm_l19;
                                        uint64_t t157 = t156;
                                        uint64_t t160 = GM_LD(uint64_t, ((uint8_t *)&t157));
                                        gm_l20 = t160;
                                        {
                                            void * t162 = (void *)((uint8_t *)&F.l14);
                                            uint64_t t163 = gm_l20;
                                            uint64_t t164 = (uintptr_t)t163;
                                            uint64_t t165 = GM_LD(uintptr_t, (((uint8_t *)&gm_l17) + 8) + 8);
                                            if (t164 >= (uintptr_t)(t165)) gm_trap_index(t164, t165, gm_srcb7e7uf3pvdko, 130, 55);
                                            gm_b16_8 t166 = GM_LD(gm_b16_8, (((uint8_t *)GM_LD(void *, (((uint8_t *)&gm_l17) + 8)) + t164 * 80) + 0));
                                            gm_tabb710d9mq2e t167;
                                            gm_f_dupe_g9iqpvn((void *)&t167, (void *)NULL, (&wn9), NULL, t166, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_km2d9tibmvp)));
                                            if (gm_unw.kind) {
                                                goto gm_ll154;
                                            }
                                            gm_tabb710d9mq2e t168 = t167;
                                            if (((((uint8_t *)&t168))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t168) + 8, gm_srcb7e7uf3pvdko, 130, 50);
                                            gm_b16_8 t169 = GM_LD(gm_b16_8, (((uint8_t *)&t168) + 8));
                                            gm_b16_8 t170 = t169;
                                            gm_b16_8 t171 = GM_LD(gm_b16_8, ((uint8_t *)&t170));
                                            gm_tftb1odc739t5 t172 = gm_f_push_3ef746b(t162, t171);
                                            gm_tftb1odc739t5 t173 = t172;
                                            if (((((uint8_t *)&t173))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t173) + 1, gm_srcb7e7uf3pvdko, 130, 41);
                                            gm_b0_1 t174 = GM_LD(gm_b0_1, (((uint8_t *)&t173) + 1));
                                        }
                                        goto me158;
mn159:;
                                        gm_trap("no match arm matched the scrutinee", gm_srcb7e7uf3pvdko, 130, 13);
me158:;
                                    }
lc150:;
                                    {
                                        uint64_t t176 = gm_l19;
                                        uint64_t r177; bool o178 = __builtin_add_overflow(t176, ((uint64_t)1ULL), &r177);
                                        uint64_t t179 = r177;
                                        gm_l19 = t179;
                                    }
                                }
gm_ll154:;
                                if (gm_unw.frame == &F.h && gm_unw.label == 5) {
                                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb150; }
                                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc150; }
                                }
                                goto gm_ll139;
lb150:;
                            }
                        }
                        goto me144;
mn145:;
                        gm_trap("no match arm matched the scrutinee", gm_srcb7e7uf3pvdko, 129, 9);
me144:;
                    }
lc135:;
                    {
                        uint64_t t181 = gm_l16;
                        uint64_t r182; bool o183 = __builtin_add_overflow(t181, ((uint64_t)1ULL), &r182);
                        uint64_t t184 = r182;
                        gm_l16 = t184;
                    }
                }
gm_ll139:;
                if (gm_unw.frame == &F.h && gm_unw.label == 4) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb135; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc135; }
                }
                goto gm_ll11;
lb135:;
            }
        }
        {
            {
                gm_b16_8 t186 = GM_LD(gm_b16_8, (((uint8_t *)&F.l14) + 0));
                gm_l21 = t186;
            }
            {
                gm_l22 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t188 = gm_l22;
                    uint64_t t189 = GM_LD(uintptr_t, ((uint8_t *)&gm_l21) + 8);
                    uint8_t t190 = (t188 < t189);
                    if (!t190) goto lb187;
                    {
                        uint64_t t193 = gm_l22;
                        uint64_t t194 = (uintptr_t)t193;
                        uint64_t t195 = GM_LD(uintptr_t, ((uint8_t *)&gm_l21) + 8);
                        gm_b16_8 t198 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l21)) + t194 * 16));
                        gm_l23 = t198;
                        {
                            void * t200 = F.l3;
                            void * t201 = (void *)(((uint8_t *)t200) + 200);
                            gm_b16_8 t202 = gm_l23;
                            gm_tftb1odc739t5 t203 = gm_f_put_5c6qgui(t201, t202, ((uint8_t)1));
                            gm_tftb1odc739t5 t204 = t203;
                            if (((((uint8_t *)&t204))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t204) + 1, gm_srcb7e7uf3pvdko, 132, 30);
                            gm_b0_1 t205 = GM_LD(gm_b0_1, (((uint8_t *)&t204) + 1));
                        }
                        goto me196;
mn197:;
                        gm_trap("no match arm matched the scrutinee", gm_srcb7e7uf3pvdko, 132, 9);
me196:;
                    }
lc187:;
                    {
                        uint64_t t207 = gm_l22;
                        uint64_t r208; bool o209 = __builtin_add_overflow(t207, ((uint64_t)1ULL), &r208);
                        uint64_t t210 = r208;
                        gm_l22 = t210;
                    }
                }
lb187:;
            }
        }
        {
            {
                void * t212 = F.l0;
                gm_b16_8 t213 = GM_LD(gm_b16_8, (((((uint8_t *)t212) + 120) + 96) + 0));
                gm_l24 = t213;
            }
            {
                gm_l25 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t215 = gm_l25;
                    uint64_t t216 = GM_LD(uintptr_t, ((uint8_t *)&gm_l24) + 8);
                    uint8_t t217 = (t215 < t216);
                    if (!t217) goto lb214;
                    {
                        uint64_t t220 = gm_l25;
                        uint64_t t221 = (uintptr_t)t220;
                        uint64_t t222 = GM_LD(uintptr_t, ((uint8_t *)&gm_l24) + 8);
                        gm_tiilmfd16v3fc t225 = GM_LD(gm_tiilmfd16v3fc, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l24)) + t221 * 24));
                        gm_l26 = t225;
                        {
                            void * t227 = F.l0;
                            uint32_t t228 = GM_LD(uint32_t, (((uint8_t *)&gm_l26) + 0));
                            gm_th2lqbo77pvsc t229;
                            gm_f_real_path_al8cv9r((void *)&t229, (void *)NULL, (&wn9), NULL, t227, t228);
                            if (gm_unw.kind) {
                                goto gm_ll218;
                            }
                            gm_th2lqbo77pvsc t230 = t229;
                            gm_b16_8 t231;
                            if (((((uint8_t *)&t230))[16] != 0)) {
                                gm_b16_8 t232 = GM_LD(gm_b16_8, (((uint8_t *)&t230) + 0));
                                t231 = t232;
                            } else {
                                goto lc214;
                            }
                            gm_l27 = t231;
                        }
                        {
                            void * t233 = F.l3;
                            gm_b16_8 t234 = gm_l27;
                            gm_b16_8 t235 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l26) + 8));
                            gm_b16_8 t236 = t235;
                            gm_b16_8 t237 = GM_LD(gm_b16_8, ((uint8_t *)&t236));
                            gm_b16_8 t238 = GM_LD(gm_b16_8, (((uint8_t *)&F.l14) + 0));
                            gm_b0_1 t239 = gm_f_add_siblings_38k13fr((void *)NULL, (&wn9), NULL, t233, t234, t237, t238);
                            if (gm_unw.kind) {
                                goto gm_ll218;
                            }
                        }
                        goto me223;
mn224:;
                        gm_trap("no match arm matched the scrutinee", gm_srcb7e7uf3pvdko, 133, 9);
me223:;
                    }
lc214:;
                    {
                        uint64_t t241 = gm_l25;
                        uint64_t r242; bool o243 = __builtin_add_overflow(t241, ((uint64_t)1ULL), &r242);
                        uint64_t t244 = r242;
                        gm_l25 = t244;
                    }
                }
gm_ll218:;
                if (gm_unw.frame == &F.h && gm_unw.label == 7) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb214; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc214; }
                }
                goto gm_ll11;
lb214:;
            }
        }
        {
            void * t245 = F.l3;
            uint8_t t246 = 0;
            F.ret = t245;
            goto gm_fret;
        }
        goto gm_ls247;
gm_ll11:;
        if (gm_unw.kind == GM_BREAK && gm_unw.frame == &F.h && gm_unw.label == 0) { gm_unw.kind = GM_NONE; goto we10; }
        goto gm_lad;
gm_ls247:;
we10:;
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
__attribute__((aligned(4))) void * gm_f_facts_s8nso10(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2) {
    struct gm_fr_facts_s8nso10 F;
    void * gm_l0; /* ws */
    gm_b16_8 gm_l1; /* dir */
    gm_b16_8 gm_l2; /* overlays */
    gm_tqsio0a8q8soi gm_l3; /* e */
    gm_th2lqbo77pvsc gm_l4; /* primary */
    gm_b16_8 gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    gm_b16_8 gm_l7; /* f */
    gm_b16_8 gm_l8; /* p */
    gm_b16_8 gm_l9; /* main */
    void * gm_l10; /* a */
    void * gm_l11; /* result */
    GM_STACK_CHECK(&F, 1061u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t3 = gm_l0;
        void * t4 = (void *)(((uint8_t *)t3) + 0);
        gm_b16_8 t5 = gm_l1;
        gm_timk834mo3ug9 t6;
        gm_f_get_rbph851((void *)&t6, t4, t5);
        gm_timk834mo3ug9 t7 = t6;
        if (!((((uint8_t *)&t7))[32] != 0)) goto ie2;
        gm_tqsio0a8q8soi t8 = GM_LD(gm_tqsio0a8q8soi, ((uint8_t *)&t7));
        gm_l3 = t8;
        {
            gm_b16_8 t11 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 8));
            gm_b16_8 t12 = t11;
            gm_b16_8 t13 = GM_LD(gm_b16_8, ((uint8_t *)&t12));
            gm_b16_8 t14 = gm_l2;
            uint64_t t15 = gm_f_stamp_aq4ff3c((void *)NULL, cx, NULL, t13, t14);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint64_t t16 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 24));
            uint8_t t17 = ((t15 == t16));
            if (!t17) goto ie10;
            {
                void * t19 = GM_LD(void *, (((uint8_t *)&gm_l3) + 0));
                uint8_t t20 = 0;
                F.ret = t19;
                goto gm_fret;
            }
            goto ix10;
ie10:;
ix10:;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        gm_th2lqbo77pvsc t21;
        memset(&t21, 0, sizeof t21);
        gm_l4 = t21;
    }
    {
        {
            gm_b16_8 t23 = gm_l1;
            gm_b16_8 t24 = gm_l2;
            gm_trqfvmjtt4sij t25;
            gm_f_module_files_in_249j100((void *)&t25, (void *)NULL, cx, NULL, t23, t24);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_trqfvmjtt4sij t26 = t25;
            gm_b16_8 t27 = GM_LD(gm_b16_8, (((uint8_t *)&t26) + 0));
            gm_l5 = t27;
        }
        {
            gm_l6 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t29 = gm_l6;
                uint64_t t30 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                uint8_t t31 = (t29 < t30);
                if (!t31) goto lb28;
                {
                    uint64_t t34 = gm_l6;
                    uint64_t t35 = (uintptr_t)t34;
                    uint64_t t36 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                    gm_b16_8 t39 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l5)) + t35 * 16));
                    gm_l7 = t39;
                    {
                        if (!((((uint8_t *)&gm_l4))[16] != 0)) goto ie41;
                        gm_b16_8 t42 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l4));
                        gm_l8 = t42;
                        {
                            gm_b16_8 t45 = gm_l7;
                            gm_b16_8 t46 = gm_l8;
                            uint8_t t47 = gm_f_path_less_702ltp5(t45, t46);
                            if (!t47) goto ie44;
                            {
                                gm_b16_8 t49 = gm_l7;
                                gm_th2lqbo77pvsc t50;
                                GM_ST(uint8_t, (uint8_t *)&t50 + 17, 0);
                                GM_ST(uint16_t, (uint8_t *)&t50 + 18, 0);
                                GM_ST(uint32_t, (uint8_t *)&t50 + 20, 0);
                                GM_ST(gm_b16_8, ((uint8_t *)&t50), t49);
                                ((uint8_t *)&t50)[16] = 1;
                                gm_l4 = t50;
                            }
                            goto ix44;
ie44:;
ix44:;
                        }
                        goto ix41;
ie41:;
                        {
                            gm_b16_8 t52 = gm_l7;
                            gm_th2lqbo77pvsc t53;
                            GM_ST(uint8_t, (uint8_t *)&t53 + 17, 0);
                            GM_ST(uint16_t, (uint8_t *)&t53 + 18, 0);
                            GM_ST(uint32_t, (uint8_t *)&t53 + 20, 0);
                            GM_ST(gm_b16_8, ((uint8_t *)&t53), t52);
                            ((uint8_t *)&t53)[16] = 1;
                            gm_l4 = t53;
                        }
ix41:;
                    }
                    goto me37;
mn38:;
                    gm_trap("no match arm matched the scrutinee", gm_srcb7e7uf3pvdko, 310, 5);
me37:;
                }
lc28:;
                {
                    uint64_t t55 = gm_l6;
                    uint64_t r56; bool o57 = __builtin_add_overflow(t55, ((uint64_t)1ULL), &r56);
                    uint64_t t58 = r56;
                    gm_l6 = t58;
                }
            }
lb28:;
        }
    }
    {
        gm_b16_8 t59 = gm_l1;
        gm_b16_8 t60;
        GM_ST(void *, (uint8_t *)&t60, (void *)(gm_sajpbdgkjnse7));
        GM_ST(uintptr_t, (uint8_t *)&t60 + 8, (uintptr_t)(7));
        gm_b16_8 t61 = gm_f_join_qveh5b2((void *)NULL, cx, NULL, t59, t60);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l9 = t61;
    }
    {
        gm_b16_8 t62 = gm_l1;
        gm_b16_8 t63 = gm_l2;
        gm_th2lqbo77pvsc t64 = gm_l4;
        gm_th2lqbo77pvsc t65 = t64;
        gm_b16_8 t66;
        if (((((uint8_t *)&t65))[16] != 0)) {
            gm_b16_8 t67 = GM_LD(gm_b16_8, (((uint8_t *)&t65) + 0));
            t66 = t67;
        } else {
            gm_b16_8 t68 = gm_l9;
            t66 = t68;
        }
        gm_b16_8 t69 = gm_f_canonical_path_q6lq4b6((void *)NULL, cx, NULL, t66);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t70 = gm_f_check_module_6elsf9l((void *)NULL, cx, NULL, t62, t63, t69);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l10 = t70;
    }
    {
        void * t71 = gm_l0;
        gm_b16_8 t72 = gm_l1;
        void * t73 = gm_l10;
        gm_b16_8 t74 = gm_l2;
        void * t75 = gm_f_store_q18c91o((void *)NULL, cx, NULL, t71, t72, t73, t74);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l11 = t75;
    }
    {
        void * t76 = gm_l10;
        gm_b0_1 t77 = gm_f_release_7gtbra1((void *)NULL, cx, NULL, t76);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t78 = gm_l11;
        uint8_t t79 = 0;
        F.ret = t78;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_find_module_dirs_5og6qvq(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, void * a1) {
    struct gm_fr_find_module_dirs_5og6qvq F;
    void * gm_l1; /* out */
    gm_b16_8 gm_l4; /* <tmp> */
    gm_b16_8 gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    gm_b16_8 gm_l7; /* d */
    GM_STACK_CHECK(&F, 328u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    gm_l1 = a1;
    {
        F.l2 = ((uint8_t)0);
    }
    {
        gm_f_list_epvducg((void *)&F.l3, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t3 = F.l0;
            gm_l4 = t3;
        }
        gm_hval t4 = ((gm_hval){(void *)&F.h, cx});
        gm_node wn5 = {.eff = 2076700068, .kind = GM_LIT, .clauses = gm_hlqklhf369l0, .env = t4.env, .hctx = t4.cx, .with_frame = &F.h, .with_label = 0, .result = (void *)&F.w0, .fw = NULL, .next = cx};
        gm_b16_8 t8 = gm_l4;
        gm_b0_1 t9 = gm_f_entries_44o5n9f_skhefef((void *)NULL, (&wn5), NULL, t8);
        if (gm_unw.kind) {
            goto gm_ll7;
        }
        F.w0 = t9;
        goto gm_ls10;
gm_ll7:;
        if (gm_unw.kind == GM_BREAK && gm_unw.frame == &F.h && gm_unw.label == 0) { gm_unw.kind = GM_NONE; goto we6; }
        goto gm_lad;
gm_ls10:;
we6:;
        gm_b0_1 t11 = F.w0;
        gm_b0_1 t12 = t11;
    }
    {
        uint8_t t14 = F.l2;
        if (!t14) goto ie13;
        {
            void * t16 = gm_l1;
            gm_b16_8 t17 = F.l0;
            gm_b16_8 t18 = gm_f_canonical_path_q6lq4b6((void *)NULL, cx, NULL, t17);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tftb1odc739t5 t19 = gm_f_push_3ef746b(t16, t18);
            gm_tftb1odc739t5 t20 = t19;
            if (((((uint8_t *)&t20))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t20) + 1, gm_srcb7e7uf3pvdko, 231, 17);
            gm_b0_1 t21 = GM_LD(gm_b0_1, (((uint8_t *)&t20) + 1));
        }
        goto ix13;
ie13:;
ix13:;
    }
    {
        gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
        gm_closure t23 = ((gm_closure){GM_PTR_TAG(gm_f_fn_la5hvvo), NULL, cx});
        gm_f_sort_by_s9l01h6(t22, t23);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t25 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
            gm_l5 = t25;
        }
        {
            gm_l6 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t27 = gm_l6;
                uint64_t t28 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                uint8_t t29 = (t27 < t28);
                if (!t29) goto lb26;
                {
                    uint64_t t32 = gm_l6;
                    uint64_t t33 = (uintptr_t)t32;
                    uint64_t t34 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                    gm_b16_8 t37 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l5)) + t33 * 16));
                    gm_l7 = t37;
                    {
                        void * t40 = gm_l1;
                        uint64_t t41 = GM_LD(uintptr_t, (((uint8_t *)t40) + 0) + 8);
                        uint8_t t42 = (t41 >= ((uint64_t)4000ULL));
                        if (!t42) goto ie39;
                        {
                            gm_b0_1 t44;
                            uint8_t t45 = 0;
                            F.ret = t44;
                            goto gm_fret;
                        }
                        goto ix39;
ie39:;
ix39:;
                    }
                    {
                        gm_b16_8 t46 = gm_l7;
                        void * t47 = gm_l1;
                        gm_b0_1 t48 = gm_f_find_module_dirs_5og6qvq((void *)NULL, cx, NULL, t46, t47);
                        if (gm_unw.kind) {
                            goto gm_ll30;
                        }
                    }
                    goto me35;
mn36:;
                    gm_trap("no match arm matched the scrutinee", gm_srcb7e7uf3pvdko, 233, 5);
me35:;
                }
lc26:;
                {
                    uint64_t t50 = gm_l6;
                    uint64_t r51; bool o52 = __builtin_add_overflow(t50, ((uint64_t)1ULL), &r51);
                    uint64_t t53 = r51;
                    gm_l6 = t53;
                }
            }
gm_ll30:;
            if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb26; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc26; }
            }
            goto gm_lad;
lb26:;
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
extern __attribute__((aligned(4))) uint8_t gm_f_fn_7eo9uvj(void * a0, void * a1);
extern __attribute__((aligned(4))) uint8_t gm_f_fn_la5hvvo(void * a0, void * a1);
extern __attribute__((aligned(4))) uint8_t gm_f_fn_vhek9ql(void * a0, void * a1);
inline __attribute__((aligned(4))) uint64_t gm_f_fnv_7bujc3t(uint64_t a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_fnv_7bujc3t F;
    uint64_t gm_l0; /* h_in */
    gm_b16_8 gm_l1; /* bytes */
    uint64_t gm_l2; /* h */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    uint8_t gm_l5; /* b */
    GM_STACK_CHECK_SHALLOW(&F, 199u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint64_t t2 = gm_l0;
        gm_l2 = t2;
    }
    {
        {
            gm_b16_8 t4 = gm_l1;
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
                    uint8_t t14 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t12 * 1));
                    uint8_t t15 = t14;
                    uint8_t t18 = GM_LD(uint8_t, ((uint8_t *)&t15));
                    gm_l5 = t18;
                    {
                        uint64_t t20 = gm_l2;
                        uint8_t t21 = gm_l5;
                        uint64_t t22 = (uint64_t)t21;
                        uint64_t t23 = (uint64_t)(t20 ^ t22);
                        uint64_t r24; bool o25 = __builtin_mul_overflow(t23, ((uint64_t)1099511628211ULL), &r24);
                        uint64_t t26 = r24;
                        gm_l2 = t26;
                    }
                    goto me16;
mn17:;
                    gm_trap("no match arm matched the scrutinee", gm_srcb7e7uf3pvdko, 171, 5);
me16:;
                }
lc5:;
                {
                    uint64_t t28 = gm_l4;
                    uint64_t r29; bool o30 = __builtin_add_overflow(t28, ((uint64_t)1ULL), &r29);
                    uint64_t t31 = r29;
                    gm_l4 = t31;
                }
            }
lb5:;
        }
    }
    {
        uint64_t t32 = gm_l2;
        uint8_t t33 = 0;
        F.ret = t32;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_ghum27l(void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_for_body_ghum27l F;
    gm_te2n4j2vtm53d gm_l0; /* <item> */
    gm_te2n4j2vtm53d gm_l1; /* r */
    gm_tibupt4dmcqai gm_l2; /* e */
    gm_b16_8 gm_l3; /* p */
    uint8_t gm_l4; /* is_directory */
    gm_t5j14m8ru132p gm_l5; /* info */
    GM_STACK_CHECK(&F, 547u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    memcpy(&gm_l0, a0, sizeof gm_l0);
    gm_te2n4j2vtm53d t3 = GM_LD(gm_te2n4j2vtm53d, ((uint8_t *)&gm_l0));
    gm_l1 = t3;
    {
        gm_te2n4j2vtm53d t5 = gm_l1;
        gm_te2n4j2vtm53d t6 = t5;
        gm_tibupt4dmcqai t7;
        if (!((((uint8_t *)&t6))[0] != 0)) {
            gm_tibupt4dmcqai t8 = GM_LD(gm_tibupt4dmcqai, (((uint8_t *)&t6) + 8));
            t7 = t8;
        } else {
            gm_unw.kind = GM_BREAK; gm_unw.frame = inst->with_frame; gm_unw.label = inst->with_label; gm_unw.is_err = 0;
            goto gm_lad;
        }
        gm_l2 = t7;
    }
    {
        gm_b16_8 t9 = GM_LD(gm_b16_8, ((uint8_t *)&((struct gm_fr_find_module_dirs_5og6qvq *)((&F.h)->up))->l0));
        gm_b16_8 t10 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l2) + 0));
        gm_b16_8 t11 = gm_f_join_qveh5b2((void *)NULL, cx, NULL, t9, t10);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l3 = t11;
    }
    {
        gm_l4 = ((uint8_t)0);
    }
    {
        gm_b16_8 t13 = gm_l3;
        gm_node *pn14 = gm_find(cx, 2126215853);
        if (!pn14) gm_trap("no handler installed for effect `Fs`", gm_srcb7e7uf3pvdko, 224, 20);
        gm_tih6h4avv5nrb t15;
        ((void (*)(void *, void *, gm_node *, gm_node *, gm_b16_8))pn14->clauses[5])((void *)&t15, pn14->env, pn14->hctx, pn14, t13);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tih6h4avv5nrb t16 = t15;
        if (((((uint8_t *)&t16))[0] != 0)) goto ie12;
        gm_t5j14m8ru132p t17 = GM_LD(gm_t5j14m8ru132p, (((uint8_t *)&t16) + 8));
        gm_l5 = t17;
        {
            uint8_t t19 = ((GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 8) + 0) == ((uint8_t)1ULL)));
            gm_l4 = t19;
        }
        goto ix12;
ie12:;
ix12:;
    }
    {
        uint8_t t21 = gm_l4;
        if (!t21) goto ie20;
        {
            gm_b16_8 t24 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l2) + 0));
            uint8_t t25 = gm_f_skipped_dir_orfng8m(t24);
            uint8_t t26 = (uint8_t)!t25;
            if (!t26) goto ie23;
            {
                void * t28 = (void *)((uint8_t *)&((struct gm_fr_find_module_dirs_5og6qvq *)((&F.h)->up))->l3);
                gm_b16_8 t29 = gm_l3;
                gm_tftb1odc739t5 t30 = gm_f_push_3ef746b(t28, t29);
                gm_tftb1odc739t5 t31 = t30;
                if (((((uint8_t *)&t31))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t31) + 1, gm_srcb7e7uf3pvdko, 226, 47);
                gm_b0_1 t32 = GM_LD(gm_b0_1, (((uint8_t *)&t31) + 1));
            }
            goto ix23;
ie23:;
ix23:;
        }
        goto ix20;
ie20:;
        uint64_t t34 = GM_LD(uintptr_t, (((uint8_t *)&gm_l2) + 0) + 8);
        uint8_t t35 = (t34 > ((uint64_t)3ULL));
        if (!t35) goto ie33;
        gm_b16_8 t36 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l2) + 0));
        gm_b16_8 t37;
        GM_ST(void *, (uint8_t *)&t37, (void *)(gm_s0ucactj6hj0a));
        GM_ST(uintptr_t, (uint8_t *)&t37 + 8, (uintptr_t)(3));
        uint8_t t38 = gm_f_ends_with_biriu5l(t36, t37);
        if (!t38) goto ie33;
        {
            GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_find_module_dirs_5og6qvq *)((&F.h)->up))->l2), ((uint8_t)1));
        }
        goto ix33;
ie33:;
ix33:;
ix20:;
    }
    goto me1;
mn2:;
    gm_trap("no match arm matched the scrutinee", gm_srcb7e7uf3pvdko, 220, 5);
me1:;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_l1ll8kh(void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_for_body_l1ll8kh F;
    gm_tommf4j9upomc gm_l0; /* <item> */
    gm_tommf4j9upomc gm_l1; /* kv */
    gm_b16_8 gm_l2; /* d */
    GM_STACK_CHECK(&F, 310u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    memcpy(&gm_l0, a0, sizeof gm_l0);
    gm_tommf4j9upomc t3 = GM_LD(gm_tommf4j9upomc, ((uint8_t *)&gm_l0));
    gm_l1 = t3;
    {
        gm_b16_8 t5 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l1) + 0));
        gm_b16_8 t6 = gm_f_dir_of_ho73hue((void *)NULL, cx, NULL, t5);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l2 = t6;
    }
    {
        gm_b16_8 t8 = gm_l2;
        gm_b16_8 t9 = GM_LD(gm_b16_8, (((uint8_t *)&((struct gm_fr_store_q18c91o *)((&F.h)->up))->l6) + 0));
        gm_b16_8 t10 = t9;
        uint8_t t11 = 0;
        gm_b16_8 t12 = t8;
        for (uintptr_t i = 0; i < (uintptr_t)(GM_LD(uintptr_t, ((uint8_t *)&t10) + 8)); i++) if (gm_eq0iqiubfm7e(((uint8_t *)GM_LD(void *, ((uint8_t *)&t10))) + i * 16, ((uint8_t *)&t12))) { t11 = 1; break; }
        uint8_t t13 = (uint8_t)(!t11);
        if (!t13) goto ie7;
        {
            void * t15 = (void *)((uint8_t *)&((struct gm_fr_store_q18c91o *)((&F.h)->up))->l6);
            gm_b16_8 t16 = gm_l2;
            gm_tabb710d9mq2e t17;
            gm_f_dupe_g9iqpvn((void *)&t17, (void *)NULL, cx, NULL, t16, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_ktjao6lvic2)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t18 = t17;
            if (((((uint8_t *)&t18))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t18) + 8, gm_srcb7e7uf3pvdko, 332, 44);
            gm_b16_8 t19 = GM_LD(gm_b16_8, (((uint8_t *)&t18) + 8));
            gm_b16_8 t20 = t19;
            gm_b16_8 t21 = GM_LD(gm_b16_8, ((uint8_t *)&t20));
            gm_tftb1odc739t5 t22 = gm_f_push_3ef746b(t15, t21);
            gm_tftb1odc739t5 t23 = t22;
            if (((((uint8_t *)&t23))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t23) + 1, gm_srcb7e7uf3pvdko, 332, 34);
            gm_b0_1 t24 = GM_LD(gm_b0_1, (((uint8_t *)&t23) + 1));
        }
        goto ix7;
ie7:;
ix7:;
    }
    goto me1;
mn2:;
    gm_trap("no match arm matched the scrutinee", gm_srcb7e7uf3pvdko, 330, 5);
me1:;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_qg33ouo(void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_for_body_qg33ouo F;
    gm_tommf4j9upomc gm_l0; /* <item> */
    gm_tommf4j9upomc gm_l1; /* kv */
    GM_STACK_CHECK(&F, 441u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    memcpy(&gm_l0, a0, sizeof gm_l0);
    gm_tommf4j9upomc t3 = GM_LD(gm_tommf4j9upomc, ((uint8_t *)&gm_l0));
    gm_l1 = t3;
    {
        gm_b16_8 t6 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l1) + 0));
        gm_b16_8 t7 = gm_f_dir_of_ho73hue((void *)NULL, cx, NULL, t6);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b16_8 t8 = GM_LD(gm_b16_8, ((uint8_t *)&((struct gm_fr_facts_of_prcp83r *)((&F.h)->up))->l1));
        gm_b16_8 t9 = t7;
        gm_b16_8 t10 = t8;
        uint8_t t11 = (gm_eq0iqiubfm7e(((uint8_t *)&t9), ((uint8_t *)&t10)));
        if (!t11) goto ie5;
        {
            void * t13 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_facts_of_prcp83r *)((&F.h)->up))->l3));
            void * t14 = (void *)(((uint8_t *)t13) + 64);
            gm_t6j6smj7qoctu t15;
            gm_b16_8 t16 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l1) + 0));
            gm_tabb710d9mq2e t17;
            gm_f_dupe_g9iqpvn((void *)&t17, (void *)NULL, cx, NULL, t16, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kec2ujc0693)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t18 = t17;
            if (((((uint8_t *)&t18))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t18) + 8, gm_srcb7e7uf3pvdko, 114, 50);
            gm_b16_8 t19 = GM_LD(gm_b16_8, (((uint8_t *)&t18) + 8));
            gm_b16_8 t20 = t19;
            gm_b16_8 t21 = GM_LD(gm_b16_8, ((uint8_t *)&t20));
            GM_ST(gm_b16_8, (uint8_t *)&t15 + 0, t21);
            void * t22 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_facts_of_prcp83r *)((&F.h)->up))->l0));
            uint32_t t23 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 16));
            gm_b16_8 t24 = gm_f_source_text_7ru912k(t22, t23);
            gm_tabb710d9mq2e t25;
            gm_f_dupe_g9iqpvn((void *)&t25, (void *)NULL, cx, NULL, t24, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_k9o17uvn1g2)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t26 = t25;
            if (((((uint8_t *)&t26))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t26) + 8, gm_srcb7e7uf3pvdko, 114, 71);
            gm_b16_8 t27 = GM_LD(gm_b16_8, (((uint8_t *)&t26) + 8));
            gm_b16_8 t28 = t27;
            gm_b16_8 t29 = GM_LD(gm_b16_8, ((uint8_t *)&t28));
            GM_ST(gm_b16_8, (uint8_t *)&t15 + 16, t29);
            gm_tftb1odc739t5 t30 = gm_f_push_5gfgdpl(t14, &t15);
            gm_tftb1odc739t5 t31 = t30;
            if (((((uint8_t *)&t31))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t31) + 1, gm_srcb7e7uf3pvdko, 114, 17);
            gm_b0_1 t32 = GM_LD(gm_b0_1, (((uint8_t *)&t31) + 1));
        }
        goto ix5;
ie5:;
ix5:;
    }
    goto me1;
mn2:;
    gm_trap("no match arm matched the scrutinee", gm_srcb7e7uf3pvdko, 112, 9);
me1:;
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
inline __attribute__((aligned(4))) void gm_f_found_text_sajr2an(void *gm_out, void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_found_text_sajr2an F;
    void * gm_l0; /* f */
    gm_b16_8 gm_l1; /* path */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    void * gm_l4; /* m */
    gm_b16_8 gm_l5; /* t */
    GM_STACK_CHECK_SHALLOW(&F, 331u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        {
            void * t3 = gm_l0;
            gm_b16_8 t4 = GM_LD(gm_b16_8, ((((uint8_t *)t3) + 40) + 0));
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
                    void * t14 = GM_LD(void *, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t12 * 8));
                    void * t15 = t14;
                    void * t18 = GM_LD(void *, ((uint8_t *)&t15));
                    gm_l4 = t18;
                    {
                        void * t21 = gm_l4;
                        gm_b16_8 t22 = gm_l1;
                        gm_th2lqbo77pvsc t23;
                        gm_f_text_in_sgvn4eo((void *)&t23, t21, t22);
                        gm_th2lqbo77pvsc t24 = t23;
                        if (!((((uint8_t *)&t24))[16] != 0)) goto ie20;
                        gm_b16_8 t25 = GM_LD(gm_b16_8, ((uint8_t *)&t24));
                        gm_l5 = t25;
                        {
                            gm_b16_8 t27 = gm_l5;
                            gm_th2lqbo77pvsc t28;
                            GM_ST(uint8_t, (uint8_t *)&t28 + 17, 0);
                            GM_ST(uint16_t, (uint8_t *)&t28 + 18, 0);
                            GM_ST(uint32_t, (uint8_t *)&t28 + 20, 0);
                            GM_ST(gm_b16_8, ((uint8_t *)&t28), t27);
                            ((uint8_t *)&t28)[16] = 1;
                            uint8_t t29 = 0;
                            F.ret = t28;
                            goto gm_fret;
                        }
                        goto ix20;
ie20:;
ix20:;
                    }
                    goto me16;
mn17:;
                    gm_trap("no match arm matched the scrutinee", gm_srcb7e7uf3pvdko, 353, 5);
me16:;
                }
lc5:;
                {
                    uint64_t t31 = gm_l3;
                    uint64_t r32; bool o33 = __builtin_add_overflow(t31, ((uint64_t)1ULL), &r32);
                    uint64_t t34 = r32;
                    gm_l3 = t34;
                }
            }
lb5:;
        }
    }
    {
        gm_th2lqbo77pvsc t35;
        memset(&t35, 0, sizeof t35);
        uint8_t t36 = 0;
        F.ret = t35;
        goto gm_fret;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_th2lqbo77pvsc gm_f_found_text_sajr2an_c(void * a0, gm_b16_8 a1) { gm_th2lqbo77pvsc gm_r; gm_f_found_text_sajr2an(&gm_r, a0, a1); return gm_r; }
extern __attribute__((aligned(4))) uint8_t gm_f_loc_equal_h2ruinn(void * a0, void * a1);
extern __attribute__((aligned(4))) gm_b16_8 gm_f_loc_key_n6bogvi(void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) uint8_t gm_f_loc_less_gf4kba8(void * a0, void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_loc_less_gf4kba8 F;
    void * gm_l0; /* a */
    void * gm_l1; /* b */
    GM_STACK_CHECK_SHALLOW(&F, 226u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t3 = gm_l0;
        gm_b16_8 t4 = GM_LD(gm_b16_8, (((uint8_t *)t3) + 0));
        void * t5 = gm_l1;
        gm_b16_8 t6 = GM_LD(gm_b16_8, (((uint8_t *)t5) + 0));
        gm_b16_8 t7 = t4;
        gm_b16_8 t8 = t6;
        uint8_t t9 = (!gm_eq0iqiubfm7e(((uint8_t *)&t7), ((uint8_t *)&t8)));
        if (!t9) goto ie2;
        {
            void * t11 = gm_l0;
            gm_b16_8 t12 = GM_LD(gm_b16_8, (((uint8_t *)t11) + 0));
            void * t13 = gm_l1;
            gm_b16_8 t14 = GM_LD(gm_b16_8, (((uint8_t *)t13) + 0));
            uint8_t t15 = gm_f_path_less_702ltp5(t12, t14);
            uint8_t t16 = 0;
            F.ret = t15;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t18 = gm_l0;
        uint32_t t19 = GM_LD(uint32_t, (((uint8_t *)t18) + 16));
        void * t20 = gm_l1;
        uint32_t t21 = GM_LD(uint32_t, (((uint8_t *)t20) + 16));
        uint8_t t22 = (!(t19 == t21));
        if (!t22) goto ie17;
        {
            void * t24 = gm_l0;
            uint32_t t25 = GM_LD(uint32_t, (((uint8_t *)t24) + 16));
            void * t26 = gm_l1;
            uint32_t t27 = GM_LD(uint32_t, (((uint8_t *)t26) + 16));
            uint8_t t28 = (t25 < t27);
            uint8_t t29 = 0;
            F.ret = t28;
            goto gm_fret;
        }
        goto ix17;
ie17:;
ix17:;
    }
    {
        void * t30 = gm_l0;
        uint32_t t31 = GM_LD(uint32_t, (((uint8_t *)t30) + 20));
        void * t32 = gm_l1;
        uint32_t t33 = GM_LD(uint32_t, (((uint8_t *)t32) + 20));
        uint8_t t34 = (t31 < t33);
        uint8_t t35 = 0;
        F.ret = t34;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_loc_of_q5lnkjn(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1) {
    struct gm_fr_loc_of_q5lnkjn F;
    void * gm_l0; /* a */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_b16_8 gm_l2; /* p */
    GM_STACK_CHECK(&F, 242u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        uint32_t t3 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 0));
        gm_th2lqbo77pvsc t4;
        gm_f_real_path_al8cv9r((void *)&t4, (void *)NULL, cx, NULL, t2, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_th2lqbo77pvsc t5 = t4;
        gm_b16_8 t6;
        if (((((uint8_t *)&t5))[16] != 0)) {
            gm_b16_8 t7 = GM_LD(gm_b16_8, (((uint8_t *)&t5) + 0));
            t6 = t7;
        } else {
            gm_tnlphins84ju6 t8;
            memset(&t8, 0, sizeof t8);
            uint8_t t9 = 0;
            F.ret = t8;
            goto gm_fret;
        }
        gm_l2 = t6;
    }
    {
        gm_t5hdnnftkup6m t10;
        gm_b16_8 t11 = gm_l2;
        GM_ST(gm_b16_8, (uint8_t *)&t10 + 0, t11);
        uint32_t t12 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 4));
        GM_ST(uint32_t, (uint8_t *)&t10 + 16, t12);
        uint32_t t13 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 8));
        GM_ST(uint32_t, (uint8_t *)&t10 + 20, t13);
        gm_tnlphins84ju6 t14;
        GM_ST(uint8_t, (uint8_t *)&t14 + 25, 0);
        GM_ST(uint16_t, (uint8_t *)&t14 + 26, 0);
        GM_ST(uint32_t, (uint8_t *)&t14 + 28, 0);
        GM_ST(gm_t5hdnnftkup6m, ((uint8_t *)&t14), t10);
        ((uint8_t *)&t14)[24] = 1;
        uint8_t t15 = 0;
        F.ret = t14;
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
inline __attribute__((aligned(4))) uint8_t gm_f_mentions_gartt1q(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1, gm_b16_8 a2) {
    struct gm_fr_mentions_gartt1q F;
    gm_b16_8 gm_l0; /* dir */
    gm_b16_8 gm_l1; /* name */
    gm_b16_8 gm_l2; /* overlays */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    gm_b16_8 gm_l5; /* f */
    gm_b16_8 gm_l6; /* t */
    gm_b16_8 gm_l7; /* bytes */
    GM_STACK_CHECK(&F, 600u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        {
            gm_b16_8 t3 = gm_l0;
            gm_b16_8 t4 = gm_l2;
            gm_trqfvmjtt4sij t5;
            gm_f_module_files_in_249j100((void *)&t5, (void *)NULL, cx, NULL, t3, t4);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_trqfvmjtt4sij t6 = t5;
            gm_b16_8 t7 = GM_LD(gm_b16_8, (((uint8_t *)&t6) + 0));
            gm_l3 = t7;
        }
        {
            gm_l4 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t9 = gm_l4;
                uint64_t t10 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                uint8_t t11 = (t9 < t10);
                if (!t11) goto lb8;
                {
                    uint64_t t14 = gm_l4;
                    uint64_t t15 = (uintptr_t)t14;
                    uint64_t t16 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                    gm_b16_8 t19 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t15 * 16));
                    gm_l5 = t19;
                    {
                        gm_b16_8 t22 = gm_l2;
                        gm_b16_8 t23 = gm_l5;
                        gm_th2lqbo77pvsc t24;
                        gm_f_overlay_text_haaaurk((void *)&t24, (void *)NULL, cx, NULL, t22, t23);
                        if (gm_unw.kind) {
                            goto gm_ll12;
                        }
                        gm_th2lqbo77pvsc t25 = t24;
                        if (!((((uint8_t *)&t25))[16] != 0)) goto ie21;
                        gm_b16_8 t26 = GM_LD(gm_b16_8, ((uint8_t *)&t25));
                        gm_l6 = t26;
                        {
                            gm_b16_8 t29 = gm_l6;
                            gm_b16_8 t30 = gm_l1;
                            uint8_t t31 = gm_f_contains_gkki4kq(t29, t30);
                            if (!t31) goto ie28;
                            {
                                uint8_t t33 = 0;
                                F.ret = ((uint8_t)1);
                                goto gm_fret;
                            }
                            goto ix28;
ie28:;
ix28:;
                        }
                        goto ix21;
ie21:;
                        gm_b16_8 t35 = gm_l5;
                        gm_tgvh15qshf643 t36;
                        gm_f_read_all_r9rh9qd((void *)&t36, (void *)NULL, cx, NULL, t35);
                        if (gm_unw.kind) {
                            goto gm_ll12;
                        }
                        gm_tgvh15qshf643 t37 = t36;
                        if (((((uint8_t *)&t37))[0] != 0)) goto ie34;
                        gm_b16_8 t38 = GM_LD(gm_b16_8, (((uint8_t *)&t37) + 8));
                        gm_l7 = t38;
                        {
                            gm_b16_8 t41 = gm_l7;
                            gm_b16_8 t42 = t41;
                            gm_b16_8 t43 = GM_LD(gm_b16_8, ((uint8_t *)&t42));
                            gm_b16_8 t44 = gm_l1;
                            uint8_t t45 = gm_f_contains_gkki4kq(t43, t44);
                            if (!t45) goto ie40;
                            {
                                uint8_t t47 = 0;
                                F.ret = ((uint8_t)1);
                                goto gm_fret;
                            }
                            goto ix40;
ie40:;
ix40:;
                        }
                        goto ix34;
ie34:;
ix34:;
ix21:;
                    }
                    goto me17;
mn18:;
                    gm_trap("no match arm matched the scrutinee", gm_srcb7e7uf3pvdko, 294, 5);
me17:;
                }
lc8:;
                {
                    uint64_t t49 = gm_l4;
                    uint64_t r50; bool o51 = __builtin_add_overflow(t49, ((uint64_t)1ULL), &r50);
                    uint64_t t52 = r50;
                    gm_l4 = t52;
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
    {
        uint8_t t53 = 0;
        F.ret = ((uint8_t)0);
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
__attribute__((aligned(4))) void gm_f_module_dirs_72lhhi5(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1) {
    struct gm_fr_module_dirs_72lhhi5 F;
    gm_b16_8 gm_l0; /* roots */
    gm_b16_8 gm_l1; /* overlays */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    gm_b16_8 gm_l5; /* r */
    gm_b16_8 gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    gm_t6j6smj7qoctu gm_l8; /* o */
    gm_b16_8 gm_l9; /* d */
    uint8_t gm_l10; /* under */
    gm_b16_8 gm_l11; /* <tmp> */
    uint64_t gm_l12; /* <tmp> */
    gm_b16_8 gm_l13; /* r */
    uint8_t gm_l14; /* skip */
    gm_b16_8 gm_l15; /* <tmp> */
    uint64_t gm_l16; /* <tmp> */
    gm_b16_8 gm_l17; /* name */
    GM_STACK_CHECK(&F, 1066u);
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
        {
            gm_b16_8 t3 = gm_l0;
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
                    gm_b16_8 t15 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t11 * 16));
                    gm_l5 = t15;
                    {
                        gm_b16_8 t17 = gm_l5;
                        void * t18 = (void *)((uint8_t *)&F.l2);
                        gm_b0_1 t19 = gm_f_find_module_dirs_5og6qvq((void *)NULL, cx, NULL, t17, t18);
                        if (gm_unw.kind) {
                            goto gm_ll8;
                        }
                    }
                    goto me13;
mn14:;
                    gm_trap("no match arm matched the scrutinee", gm_srcb7e7uf3pvdko, 272, 5);
me13:;
                }
lc4:;
                {
                    uint64_t t21 = gm_l4;
                    uint64_t r22; bool o23 = __builtin_add_overflow(t21, ((uint64_t)1ULL), &r22);
                    uint64_t t24 = r22;
                    gm_l4 = t24;
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
        {
            gm_b16_8 t26 = gm_l1;
            gm_l6 = t26;
        }
        {
            gm_l7 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t28 = gm_l7;
                uint64_t t29 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                uint8_t t30 = (t28 < t29);
                if (!t30) goto lb27;
                {
                    uint64_t t33 = gm_l7;
                    uint64_t t34 = (uintptr_t)t33;
                    uint64_t t35 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                    gm_t6j6smj7qoctu t38 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t34 * 32));
                    gm_l8 = t38;
                    {
                        gm_b16_8 t40 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l8) + 0));
                        gm_th2lqbo77pvsc t41;
                        gm_f_parent_lk0iero((void *)&t41, (void *)NULL, cx, NULL, t40);
                        if (gm_unw.kind) {
                            goto gm_ll31;
                        }
                        gm_th2lqbo77pvsc t42 = t41;
                        gm_b16_8 t43;
                        if (((((uint8_t *)&t42))[16] != 0)) {
                            gm_b16_8 t44 = GM_LD(gm_b16_8, (((uint8_t *)&t42) + 0));
                            t43 = t44;
                        } else {
                            goto lc27;
                        }
                        gm_l9 = t43;
                    }
                    {
                        gm_l10 = ((uint8_t)0);
                    }
                    {
                        {
                            gm_b16_8 t46 = gm_l0;
                            gm_l11 = t46;
                        }
                        {
                            gm_l12 = ((uint64_t)0ULL);
                        }
                        {
                            for (;;) {
                                uint64_t t48 = gm_l12;
                                uint64_t t49 = GM_LD(uintptr_t, ((uint8_t *)&gm_l11) + 8);
                                uint8_t t50 = (t48 < t49);
                                if (!t50) goto lb47;
                                {
                                    uint64_t t53 = gm_l12;
                                    uint64_t t54 = (uintptr_t)t53;
                                    uint64_t t55 = GM_LD(uintptr_t, ((uint8_t *)&gm_l11) + 8);
                                    gm_b16_8 t58 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l11)) + t54 * 16));
                                    gm_l13 = t58;
                                    {
                                        gm_b16_8 t61 = gm_l9;
                                        gm_b16_8 t62 = gm_l13;
                                        uint8_t t63 = gm_f_starts_with_ahdikpt((void *)NULL, cx, NULL, t61, t62);
                                        if (gm_unw.kind) {
                                            goto gm_ll51;
                                        }
                                        if (!t63) goto ie60;
                                        {
                                            gm_l10 = ((uint8_t)1);
                                        }
                                        goto ix60;
ie60:;
ix60:;
                                    }
                                    goto me56;
mn57:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcb7e7uf3pvdko, 276, 9);
me56:;
                                }
lc47:;
                                {
                                    uint64_t t66 = gm_l12;
                                    uint64_t r67; bool o68 = __builtin_add_overflow(t66, ((uint64_t)1ULL), &r67);
                                    uint64_t t69 = r67;
                                    gm_l12 = t69;
                                }
                            }
gm_ll51:;
                            if (gm_unw.frame == &F.h && gm_unw.label == 2) {
                                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb47; }
                                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc47; }
                            }
                            goto gm_ll31;
lb47:;
                        }
                    }
                    {
                        uint8_t t71 = gm_l10;
                        uint8_t t72 = (uint8_t)!t71;
                        if (!t72) goto ie70;
                        {
                            goto lc27;
                        }
                        goto ix70;
ie70:;
ix70:;
                    }
                    {
                        gm_l14 = ((uint8_t)0);
                    }
                    {
                        {
                            gm_b16_8 t75 = gm_l9;
                            gm_tl7hpf5nlilqq t76;
                            gm_f_split_cbk4ks8((void *)&t76, (void *)NULL, cx, NULL, t75);
                            if (gm_unw.kind) {
                                goto gm_ll31;
                            }
                            gm_tl7hpf5nlilqq t77 = t76;
                            gm_b16_8 t78 = GM_LD(gm_b16_8, ((((uint8_t *)&t77) + 16) + 0));
                            gm_l15 = t78;
                        }
                        {
                            gm_l16 = ((uint64_t)0ULL);
                        }
                        {
                            for (;;) {
                                uint64_t t80 = gm_l16;
                                uint64_t t81 = GM_LD(uintptr_t, ((uint8_t *)&gm_l15) + 8);
                                uint8_t t82 = (t80 < t81);
                                if (!t82) goto lb79;
                                {
                                    uint64_t t85 = gm_l16;
                                    uint64_t t86 = (uintptr_t)t85;
                                    uint64_t t87 = GM_LD(uintptr_t, ((uint8_t *)&gm_l15) + 8);
                                    gm_b16_8 t90 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l15)) + t86 * 16));
                                    gm_l17 = t90;
                                    {
                                        gm_b16_8 t93 = gm_l17;
                                        uint8_t t94 = gm_f_skipped_dir_orfng8m(t93);
                                        if (!t94) goto ie92;
                                        {
                                            gm_l14 = ((uint8_t)1);
                                        }
                                        goto ix92;
ie92:;
ix92:;
                                    }
                                    goto me88;
mn89:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcb7e7uf3pvdko, 281, 9);
me88:;
                                }
lc79:;
                                {
                                    uint64_t t97 = gm_l16;
                                    uint64_t r98; bool o99 = __builtin_add_overflow(t97, ((uint64_t)1ULL), &r98);
                                    uint64_t t100 = r98;
                                    gm_l16 = t100;
                                }
                            }
lb79:;
                        }
                    }
                    {
                        uint8_t t102 = 0;
                        uint8_t t103 = gm_l14;
                        if (!t103) {
                            gm_b16_8 t104 = gm_l9;
                            gm_b16_8 t105 = GM_LD(gm_b16_8, (((uint8_t *)&F.l2) + 0));
                            gm_b16_8 t106 = t105;
                            uint8_t t107 = 0;
                            gm_b16_8 t108 = t104;
                            for (uintptr_t i = 0; i < (uintptr_t)(GM_LD(uintptr_t, ((uint8_t *)&t106) + 8)); i++) if (gm_eq0iqiubfm7e(((uint8_t *)GM_LD(void *, ((uint8_t *)&t106))) + i * 16, ((uint8_t *)&t108))) { t107 = 1; break; }
                            uint8_t t109 = (uint8_t)(t107);
                            t102 = t109;
                        } else t102 = 1;
                        if (!t102) goto ie101;
                        {
                            goto lc27;
                        }
                        goto ix101;
ie101:;
ix101:;
                    }
                    {
                        void * t111 = (void *)((uint8_t *)&F.l2);
                        gm_b16_8 t112 = gm_l9;
                        gm_tftb1odc739t5 t113 = gm_f_push_3ef746b(t111, t112);
                        gm_tftb1odc739t5 t114 = t113;
                        if (((((uint8_t *)&t114))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t114) + 1, gm_srcb7e7uf3pvdko, 285, 9);
                        gm_b0_1 t115 = GM_LD(gm_b0_1, (((uint8_t *)&t114) + 1));
                    }
                    goto me36;
mn37:;
                    gm_trap("no match arm matched the scrutinee", gm_srcb7e7uf3pvdko, 273, 5);
me36:;
                }
lc27:;
                {
                    uint64_t t117 = gm_l7;
                    uint64_t r118; bool o119 = __builtin_add_overflow(t117, ((uint64_t)1ULL), &r118);
                    uint64_t t120 = r118;
                    gm_l7 = t120;
                }
            }
gm_ll31:;
            if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb27; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc27; }
            }
            goto gm_lad;
lb27:;
        }
    }
    {
        gm_b16_8 t121 = GM_LD(gm_b16_8, (((uint8_t *)&F.l2) + 0));
        gm_closure t122 = ((gm_closure){GM_PTR_TAG(gm_f_fn_7eo9uvj), NULL, cx});
        gm_f_sort_by_s9l01h6(t121, t122);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t123 = (void *)((uint8_t *)&F.l2);
        gm_b16_8 t124 = GM_LD(gm_b16_8, (((uint8_t *)&F.l2) + 0));
        uint64_t t125 = gm_f_dedup_6a2r2j0(t124);
        gm_f_truncate_vir0vu7(t123, t125);
    }
    {
        memmove(gm_out, ((uint8_t *)&F.l2), 40);
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
extern __attribute__((aligned(4))) void gm_f_module_files_in_249j100(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) void gm_f_next_name_3cu8kk6(void *gm_out, gm_b16_8 a0, void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_next_name_3cu8kk6 F;
    gm_b16_8 gm_l0; /* p */
    void * gm_l1; /* at */
    uint64_t gm_l2; /* start */
    uint64_t gm_l3; /* end */
    gm_b16_8 gm_l4; /* name */
    GM_STACK_CHECK_SHALLOW(&F, 404u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        for (;;) {
            void * t3 = gm_l1;
            uint64_t t4 = GM_LD(uint64_t, ((uint8_t *)t3));
            uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            uint8_t t6 = (t4 < t5);
            if (!t6) goto lb2;
            {
                void * t9 = gm_l1;
                uint64_t t10 = GM_LD(uint64_t, ((uint8_t *)t9));
                gm_l2 = t10;
            }
            {
                uint64_t t12 = gm_l2;
                uint8_t t13 = ((t12 == ((uint64_t)0ULL)));
                if (!t13) goto ie11;
                uint64_t t14 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t15 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                if (t14 >= (uintptr_t)(t15)) gm_trap_index(t14, t15, gm_srcb7e7uf3pvdko, 37, 27);
                uint8_t t16 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t14 * 1));
                uint8_t t17 = ((t16 == ((uint8_t)47ULL)));
                if (!t17) goto ie11;
                {
                    void * t19 = gm_l1;
                    GM_ST(uint64_t, ((uint8_t *)t19), ((uint64_t)1ULL));
                }
                {
                    gm_b16_8 t20;
                    GM_ST(void *, (uint8_t *)&t20, (void *)(gm_st3ja0nqi37bv));
                    GM_ST(uintptr_t, (uint8_t *)&t20 + 8, (uintptr_t)(1));
                    gm_th2lqbo77pvsc t21;
                    GM_ST(uint8_t, (uint8_t *)&t21 + 17, 0);
                    GM_ST(uint16_t, (uint8_t *)&t21 + 18, 0);
                    GM_ST(uint32_t, (uint8_t *)&t21 + 20, 0);
                    GM_ST(gm_b16_8, ((uint8_t *)&t21), t20);
                    ((uint8_t *)&t21)[16] = 1;
                    uint8_t t22 = 0;
                    F.ret = t21;
                    goto gm_fret;
                }
                goto ix11;
ie11:;
ix11:;
            }
            {
                uint64_t t23 = gm_l2;
                gm_l3 = t23;
            }
            {
                for (;;) {
                    uint64_t t25 = gm_l3;
                    uint64_t t26 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                    uint8_t t27 = (t25 < t26);
                    if (!t27) goto lb24;
                    uint64_t t28 = gm_l3;
                    uint64_t t29 = (uintptr_t)t28;
                    uint64_t t30 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                    if (t29 >= (uintptr_t)(t30)) gm_trap_index(t29, t30, gm_srcb7e7uf3pvdko, 42, 31);
                    uint8_t t31 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t29 * 1));
                    uint8_t t32 = (!(t31 == ((uint8_t)47ULL)));
                    if (!t32) goto lb24;
                    {
                        uint64_t t35 = gm_l3;
                        uint64_t r36; bool o37 = __builtin_add_overflow(t35, ((uint64_t)1ULL), &r36);
                        if (o37) gm_trap("integer overflow in `+`", gm_srcb7e7uf3pvdko, 42, 51);
                        uint64_t t38 = r36;
                        gm_l3 = t38;
                    }
lc24:;
                }
lb24:;
            }
            {
                void * t39 = gm_l1;
                uint64_t t40 = gm_l3;
                uint64_t r41; bool o42 = __builtin_add_overflow(t40, ((uint64_t)1ULL), &r41);
                if (o42) gm_trap("integer overflow in `+`", gm_srcb7e7uf3pvdko, 43, 15);
                uint64_t t43 = r41;
                GM_ST(uint64_t, ((uint8_t *)t39), t43);
            }
            {
                uint64_t t44 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                void * t45 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)));
                uint64_t t46 = gm_l2;
                uint64_t t47 = (uintptr_t)t46;
                uint64_t t48 = gm_l3;
                uint64_t t49 = (uintptr_t)t48;
                if (t47 > t49 || t49 > (uintptr_t)t44) gm_trap_slice(t47, t49, t44, gm_srcb7e7uf3pvdko, 44, 17);
                gm_b16_8 t50;
                GM_ST(void *, (uint8_t *)&t50, (void *)(((uint8_t *)t45 + t47 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t50 + 8, (uintptr_t)(t49 - t47));
                gm_l4 = t50;
            }
            {
                uint64_t t52 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                uint8_t t53 = (t52 > ((uint64_t)0ULL));
                if (!t53) goto ie51;
                uint8_t t54 = (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8) == 1) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l4)), gm_sl3q52ep6aa53, 1) == 0));
                if (!t54) goto ie51;
                {
                    gm_b16_8 t56 = gm_l4;
                    gm_th2lqbo77pvsc t57;
                    GM_ST(uint8_t, (uint8_t *)&t57 + 17, 0);
                    GM_ST(uint16_t, (uint8_t *)&t57 + 18, 0);
                    GM_ST(uint32_t, (uint8_t *)&t57 + 20, 0);
                    GM_ST(gm_b16_8, ((uint8_t *)&t57), t56);
                    ((uint8_t *)&t57)[16] = 1;
                    uint8_t t58 = 0;
                    F.ret = t57;
                    goto gm_fret;
                }
                goto ix51;
ie51:;
ix51:;
            }
lc2:;
        }
lb2:;
    }
    {
        gm_th2lqbo77pvsc t59;
        memset(&t59, 0, sizeof t59);
        uint8_t t60 = 0;
        F.ret = t59;
        goto gm_fret;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_th2lqbo77pvsc gm_f_next_name_3cu8kk6_c(gm_b16_8 a0, void * a1) { gm_th2lqbo77pvsc gm_r; gm_f_next_name_3cu8kk6(&gm_r, a0, a1); return gm_r; }
extern __attribute__((aligned(4))) gm_b16_8 gm_f_package_root_q7uuush(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) void gm_f_package_roots_uh3pgnt(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_package_roots_uh3pgnt F;
    gm_b16_8 gm_l0; /* dir */
    gm_b16_8 gm_l1; /* root */
    GM_STACK_CHECK(&F, 132u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_b16_8 t2 = gm_l0;
        gm_b16_8 t3 = gm_f_package_root_q7uuush((void *)NULL, cx, NULL, t2);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b16_8 t4 = gm_f_canonical_path_q6lq4b6((void *)NULL, cx, NULL, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l1 = t4;
    }
    {
        gm_f_list_epvducg((void *)&F.l2, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t5 = (void *)((uint8_t *)&F.l2);
        gm_b16_8 t6 = gm_l1;
        gm_tftb1odc739t5 t7 = gm_f_push_3ef746b(t5, t6);
        gm_tftb1odc739t5 t8 = t7;
        if (((((uint8_t *)&t8))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t8) + 1, gm_srcb7e7uf3pvdko, 263, 5);
        gm_b0_1 t9 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
    }
    {
        gm_b16_8 t10 = gm_l1;
        void * t11 = (void *)((uint8_t *)&F.l2);
        gm_b0_1 t12 = gm_f_path_dependencies_270fncm((void *)NULL, cx, NULL, t10, t11);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        memmove(gm_out, ((uint8_t *)&F.l2), 40);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_path_dependencies_270fncm(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, void * a1) {
    struct gm_fr_path_dependencies_270fncm F;
    gm_b16_8 gm_l0; /* root */
    void * gm_l1; /* seen */
    gm_b16_8 gm_l2; /* body */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    gm_b16_8 gm_l7; /* p */
    gm_b16_8 gm_l8; /* d */
    GM_STACK_CHECK(&F, 1970u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        gm_b16_8 t2 = gm_l0;
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_se24c7hpp401n));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(10));
        gm_b16_8 t4 = gm_f_join_qveh5b2((void *)NULL, cx, NULL, t2, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tgvh15qshf643 t5;
        gm_f_read_all_r9rh9qd((void *)&t5, (void *)NULL, cx, NULL, t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tgvh15qshf643 t6 = t5;
        gm_b16_8 t7;
        if (!((((uint8_t *)&t6))[0] != 0)) {
            gm_b16_8 t8 = GM_LD(gm_b16_8, (((uint8_t *)&t6) + 8));
            t7 = t8;
        } else {
            gm_b0_1 t9;
            uint8_t t10 = 0;
            F.ret = t9;
            goto gm_fret;
        }
        gm_l2 = t7;
    }
    {
        gm_b16_8 t11 = gm_l2;
        gm_b16_8 t12 = t11;
        gm_b16_8 t13 = GM_LD(gm_b16_8, ((uint8_t *)&t12));
        gm_tjgmtdrk88t7m t14;
        gm_f_parse_manifest_b61s3ln((void *)&t14, (void *)NULL, cx, NULL, t13);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tjgmtdrk88t7m t15 = t14;
        gm_tm43kqe3e8b4a t16;
        if (!((((uint8_t *)&t15))[0] != 0)) {
            gm_tm43kqe3e8b4a t17 = GM_LD(gm_tm43kqe3e8b4a, (((uint8_t *)&t15) + 8));
            t16 = t17;
        } else {
            gm_b0_1 t18;
            uint8_t t19 = 0;
            F.ret = t18;
            goto gm_fret;
        }
        F.l3 = t16;
    }
    {
        {
            gm_b16_8 t21 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l3) + 48) + 0));
            gm_l4 = t21;
        }
        {
            gm_l5 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t23 = gm_l5;
                uint64_t t24 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                uint8_t t25 = (t23 < t24);
                if (!t25) goto lb22;
                {
                    uint64_t t28 = gm_l5;
                    uint64_t t29 = (uintptr_t)t28;
                    uint64_t t30 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                    gm_t7tiv6vif86qg t33 = GM_LD(gm_t7tiv6vif86qg, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t29 * 120));
                    F.l6 = t33;
                    {
                        uint8_t t36 = GM_LD(uint8_t, ((((uint8_t *)&F.l6) + 24) + 0));
                        if (t36 != ((uint8_t)0ULL)) goto ie35;
                        gm_b16_8 t37 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l6) + 24) + 8));
                        gm_l7 = t37;
                        {
                            gm_b16_8 t39 = gm_l0;
                            gm_b16_8 t40 = gm_l7;
                            gm_b16_8 t41 = gm_f_join_qveh5b2((void *)NULL, cx, NULL, t39, t40);
                            if (gm_unw.kind) {
                                goto gm_ll26;
                            }
                            gm_b16_8 t42 = gm_f_canonical_path_q6lq4b6((void *)NULL, cx, NULL, t41);
                            if (gm_unw.kind) {
                                goto gm_ll26;
                            }
                            gm_l8 = t42;
                        }
                        {
                            gm_b16_8 t44 = gm_l8;
                            void * t45 = gm_l1;
                            gm_b16_8 t46 = GM_LD(gm_b16_8, (((uint8_t *)t45) + 0));
                            gm_b16_8 t47 = t46;
                            uint8_t t48 = 0;
                            gm_b16_8 t49 = t44;
                            for (uintptr_t i = 0; i < (uintptr_t)(GM_LD(uintptr_t, ((uint8_t *)&t47) + 8)); i++) if (gm_eq0iqiubfm7e(((uint8_t *)GM_LD(void *, ((uint8_t *)&t47))) + i * 16, ((uint8_t *)&t49))) { t48 = 1; break; }
                            uint8_t t50 = (uint8_t)(!t48);
                            if (!t50) goto ie43;
                            {
                                void * t52 = gm_l1;
                                gm_b16_8 t53 = gm_l8;
                                gm_tftb1odc739t5 t54 = gm_f_push_3ef746b(t52, t53);
                                gm_tftb1odc739t5 t55 = t54;
                                if (((((uint8_t *)&t55))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t55) + 1, gm_srcb7e7uf3pvdko, 251, 17);
                                gm_b0_1 t56 = GM_LD(gm_b0_1, (((uint8_t *)&t55) + 1));
                            }
                            {
                                gm_b16_8 t57 = gm_l8;
                                void * t58 = gm_l1;
                                gm_b0_1 t59 = gm_f_path_dependencies_270fncm((void *)NULL, cx, NULL, t57, t58);
                                if (gm_unw.kind) {
                                    goto gm_ll26;
                                }
                            }
                            goto ix43;
ie43:;
ix43:;
                        }
                        goto ix35;
ie35:;
ix35:;
                    }
                    goto me31;
mn32:;
                    gm_trap("no match arm matched the scrutinee", gm_srcb7e7uf3pvdko, 247, 5);
me31:;
                }
lc22:;
                {
                    uint64_t t61 = gm_l5;
                    uint64_t r62; bool o63 = __builtin_add_overflow(t61, ((uint64_t)1ULL), &r62);
                    uint64_t t64 = r62;
                    gm_l5 = t64;
                }
            }
gm_ll26:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb22; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc22; }
            }
            goto gm_lad;
lb22:;
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
inline __attribute__((aligned(4))) uint8_t gm_f_path_less_702ltp5(gm_b16_8 a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_path_less_702ltp5 F;
    gm_b16_8 gm_l0; /* a */
    gm_b16_8 gm_l1; /* b */
    uint64_t gm_l2; /* i */
    uint64_t gm_l3; /* j */
    gm_th2lqbo77pvsc gm_l4; /* ai */
    gm_th2lqbo77pvsc gm_l5; /* bj */
    gm_b16_8 gm_l6; /* x */
    gm_b16_8 gm_l7; /* y */
    GM_STACK_CHECK_SHALLOW(&F, 506u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        gm_l2 = ((uint64_t)0ULL);
    }
    {
        gm_l3 = ((uint64_t)0ULL);
    }
    {
        for (;;) {
            if (!((uint8_t)1)) goto lb2;
            {
                gm_b16_8 t5 = gm_l0;
                void * t6 = (void *)((uint8_t *)&gm_l2);
                gm_f_next_name_3cu8kk6((void *)&gm_l4, t5, t6);
            }
            {
                gm_b16_8 t7 = gm_l1;
                void * t8 = (void *)((uint8_t *)&gm_l3);
                gm_f_next_name_3cu8kk6((void *)&gm_l5, t7, t8);
            }
            {
                uint8_t t10;
                if (((((uint8_t *)&gm_l4))[16] != 0)) goto mn12;
                t10 = ((uint8_t)1);
                goto me11;
mn12:;
                t10 = ((uint8_t)0);
                goto me11;
mn13:;
                gm_trap("no match arm matched the scrutinee", gm_srcb7e7uf3pvdko, 25, 12);
me11:;
                if (!t10) goto ie9;
                {
                    uint8_t t15;
                    if (((((uint8_t *)&gm_l5))[16] != 0)) goto mn17;
                    t15 = ((uint8_t)0);
                    goto me16;
mn17:;
                    t15 = ((uint8_t)1);
                    goto me16;
mn18:;
                    gm_trap("no match arm matched the scrutinee", gm_srcb7e7uf3pvdko, 25, 32);
me16:;
                    uint8_t t19 = 0;
                    F.ret = t15;
                    goto gm_fret;
                }
                goto ix9;
ie9:;
ix9:;
            }
            {
                uint8_t t21;
                if (((((uint8_t *)&gm_l5))[16] != 0)) goto mn23;
                t21 = ((uint8_t)1);
                goto me22;
mn23:;
                t21 = ((uint8_t)0);
                goto me22;
mn24:;
                gm_trap("no match arm matched the scrutinee", gm_srcb7e7uf3pvdko, 26, 12);
me22:;
                if (!t21) goto ie20;
                {
                    uint8_t t26 = 0;
                    F.ret = ((uint8_t)0);
                    goto gm_fret;
                }
                goto ix20;
ie20:;
ix20:;
            }
            {
                gm_th2lqbo77pvsc t27 = gm_l4;
                gm_th2lqbo77pvsc t28 = t27;
                gm_b16_8 t29;
                if (((((uint8_t *)&t28))[16] != 0)) {
                    gm_b16_8 t30 = GM_LD(gm_b16_8, (((uint8_t *)&t28) + 0));
                    t29 = t30;
                } else {
                    gm_b16_8 t31;
                    GM_ST(void *, (uint8_t *)&t31, (void *)(gm_sr4qbrk6tlgaa));
                    GM_ST(uintptr_t, (uint8_t *)&t31 + 8, (uintptr_t)(0));
                    t29 = t31;
                }
                gm_l6 = t29;
            }
            {
                gm_th2lqbo77pvsc t32 = gm_l5;
                gm_th2lqbo77pvsc t33 = t32;
                gm_b16_8 t34;
                if (((((uint8_t *)&t33))[16] != 0)) {
                    gm_b16_8 t35 = GM_LD(gm_b16_8, (((uint8_t *)&t33) + 0));
                    t34 = t35;
                } else {
                    gm_b16_8 t36;
                    GM_ST(void *, (uint8_t *)&t36, (void *)(gm_sr4qbrk6tlgaa));
                    GM_ST(uintptr_t, (uint8_t *)&t36 + 8, (uintptr_t)(0));
                    t34 = t36;
                }
                gm_l7 = t34;
            }
            {
                gm_b16_8 t38 = gm_l6;
                gm_b16_8 t39 = gm_l7;
                gm_b16_8 t40 = t38;
                gm_b16_8 t41 = t39;
                uint8_t t42 = (!gm_eq0iqiubfm7e(((uint8_t *)&t40), ((uint8_t *)&t41)));
                if (!t42) goto ie37;
                {
                    gm_b16_8 t44 = gm_l6;
                    gm_b16_8 t45 = gm_l7;
                    gm_b16_8 t46 = t44;
                    gm_b16_8 t47 = t45;
                    uint8_t t48 = (uint8_t)gm_lt0iqiubfm7e(((uint8_t *)&t46), ((uint8_t *)&t47));
                    uint8_t t49 = 0;
                    F.ret = t48;
                    goto gm_fret;
                }
                goto ix37;
ie37:;
ix37:;
            }
lc2:;
        }
lb2:;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) gm_b0_1 gm_f_release_facts_faaib92(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) uint64_t gm_f_stamp_aq4ff3c(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1) {
    struct gm_fr_stamp_aq4ff3c F;
    gm_b16_8 gm_l0; /* dirs */
    gm_b16_8 gm_l1; /* overlays */
    uint64_t gm_l2; /* h */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    gm_b16_8 gm_l5; /* dir */
    gm_b16_8 gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    gm_b16_8 gm_l9; /* f */
    gm_b16_8 gm_l10; /* t */
    gm_b16_8 gm_l11; /* bytes */
    GM_STACK_CHECK(&F, 763u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        gm_l2 = ((uint64_t)14695981039346656037ULL);
    }
    {
        {
            gm_b16_8 t3 = gm_l0;
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
                    gm_b16_8 t15 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t11 * 16));
                    gm_l5 = t15;
                    {
                        uint64_t t17 = gm_l2;
                        gm_b16_8 t18 = gm_l5;
                        uint64_t t19 = gm_f_fnv_7bujc3t(t17, t18);
                        gm_l2 = t19;
                    }
                    {
                        gm_b16_8 t20 = gm_l5;
                        gm_b16_8 t21 = gm_l1;
                        gm_f_module_files_in_249j100((void *)&F.l6, (void *)NULL, cx, NULL, t20, t21);
                        if (gm_unw.kind) {
                            goto gm_ll8;
                        }
                    }
                    {
                        gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)&F.l6) + 0));
                        gm_closure t23 = ((gm_closure){GM_PTR_TAG(gm_f_fn_vhek9ql), NULL, cx});
                        gm_f_sort_by_s9l01h6(t22, t23);
                        if (gm_unw.kind) {
                            goto gm_ll8;
                        }
                    }
                    {
                        {
                            gm_b16_8 t25 = GM_LD(gm_b16_8, (((uint8_t *)&F.l6) + 0));
                            gm_l7 = t25;
                        }
                        {
                            gm_l8 = ((uint64_t)0ULL);
                        }
                        {
                            for (;;) {
                                uint64_t t27 = gm_l8;
                                uint64_t t28 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                                uint8_t t29 = (t27 < t28);
                                if (!t29) goto lb26;
                                {
                                    uint64_t t32 = gm_l8;
                                    uint64_t t33 = (uintptr_t)t32;
                                    uint64_t t34 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                                    gm_b16_8 t37 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l7)) + t33 * 16));
                                    gm_l9 = t37;
                                    {
                                        uint64_t t39 = gm_l2;
                                        gm_b16_8 t40 = gm_l9;
                                        uint64_t t41 = gm_f_fnv_7bujc3t(t39, t40);
                                        gm_l2 = t41;
                                    }
                                    {
                                        gm_b16_8 t43 = gm_l1;
                                        gm_b16_8 t44 = gm_l9;
                                        gm_th2lqbo77pvsc t45;
                                        gm_f_overlay_text_haaaurk((void *)&t45, (void *)NULL, cx, NULL, t43, t44);
                                        if (gm_unw.kind) {
                                            goto gm_ll30;
                                        }
                                        gm_th2lqbo77pvsc t46 = t45;
                                        if (!((((uint8_t *)&t46))[16] != 0)) goto ie42;
                                        gm_b16_8 t47 = GM_LD(gm_b16_8, ((uint8_t *)&t46));
                                        gm_l10 = t47;
                                        {
                                            uint64_t t49 = gm_l2;
                                            gm_b16_8 t50 = gm_l10;
                                            uint64_t t51 = gm_f_fnv_7bujc3t(t49, t50);
                                            gm_l2 = t51;
                                        }
                                        goto ix42;
ie42:;
                                        gm_b16_8 t53 = gm_l9;
                                        gm_tgvh15qshf643 t54;
                                        gm_f_read_all_r9rh9qd((void *)&t54, (void *)NULL, cx, NULL, t53);
                                        if (gm_unw.kind) {
                                            goto gm_ll30;
                                        }
                                        gm_tgvh15qshf643 t55 = t54;
                                        if (((((uint8_t *)&t55))[0] != 0)) goto ie52;
                                        gm_b16_8 t56 = GM_LD(gm_b16_8, (((uint8_t *)&t55) + 8));
                                        gm_l11 = t56;
                                        {
                                            uint64_t t58 = gm_l2;
                                            gm_b16_8 t59 = gm_l11;
                                            gm_b16_8 t60 = t59;
                                            gm_b16_8 t61 = GM_LD(gm_b16_8, ((uint8_t *)&t60));
                                            uint64_t t62 = gm_f_fnv_7bujc3t(t58, t61);
                                            gm_l2 = t62;
                                        }
                                        goto ix52;
ie52:;
ix52:;
ix42:;
                                    }
                                    goto me35;
mn36:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcb7e7uf3pvdko, 183, 9);
me35:;
                                }
lc26:;
                                {
                                    uint64_t t64 = gm_l8;
                                    uint64_t r65; bool o66 = __builtin_add_overflow(t64, ((uint64_t)1ULL), &r65);
                                    uint64_t t67 = r65;
                                    gm_l8 = t67;
                                }
                            }
gm_ll30:;
                            if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb26; }
                                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc26; }
                            }
                            goto gm_ll8;
lb26:;
                        }
                    }
                    goto me13;
mn14:;
                    gm_trap("no match arm matched the scrutinee", gm_srcb7e7uf3pvdko, 179, 5);
me13:;
                }
lc4:;
                {
                    uint64_t t69 = gm_l4;
                    uint64_t r70; bool o71 = __builtin_add_overflow(t69, ((uint64_t)1ULL), &r70);
                    uint64_t t72 = r70;
                    gm_l4 = t72;
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
        uint64_t t73 = gm_l2;
        uint8_t t74 = 0;
        F.ret = t73;
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
__attribute__((aligned(4))) void * gm_f_store_q18c91o(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, void * a2, gm_b16_8 a3) {
    struct gm_fr_store_q18c91o F;
    void * gm_l0; /* ws */
    gm_b16_8 gm_l1; /* dir */
    void * gm_l2; /* a */
    gm_b16_8 gm_l3; /* overlays */
    void * gm_l4; /* f */
    gm_b16_8 gm_l5; /* std */
    void * gm_l7; /* <tmp> */
    gm_b16_8 gm_l9; /* <tmp> */
    uint64_t gm_l10; /* <tmp> */
    gm_b16_8 gm_l11; /* d */
    uint64_t gm_l12; /* s */
    gm_tqsio0a8q8soi gm_l13; /* old */
    GM_STACK_CHECK(&F, 954u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        void * t2 = gm_l2;
        gm_b16_8 t3 = gm_l1;
        void * t4 = gm_f_facts_of_prcp83r((void *)NULL, cx, NULL, t2, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l4 = t4;
    }
    {
        gm_b16_8 t5 = gm_f_std_root_e0juohb((void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b16_8 t6 = gm_f_canonical_path_q6lq4b6((void *)NULL, cx, NULL, t5);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l5 = t6;
    }
    {
        gm_f_list_epvducg((void *)&F.l6, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            void * t8 = gm_l2;
            void * t9 = (void *)(((uint8_t *)t8) + 480);
            gm_l7 = t9;
        }
        gm_hval t10 = ((gm_hval){(void *)&F.h, cx});
        gm_node wn11 = {.eff = 1575796949, .kind = GM_LIT, .clauses = gm_hlfkkokngdk1, .env = t10.env, .hctx = t10.cx, .with_frame = &F.h, .with_label = 0, .result = (void *)&F.w0, .fw = NULL, .next = cx};
        void * t14 = gm_l7;
        gm_b0_1 t15 = gm_f_entries_bdtp5sv_sv0hilk((void *)NULL, (&wn11), NULL, t14);
        if (gm_unw.kind) {
            goto gm_ll13;
        }
        F.w0 = t15;
        goto gm_ls16;
gm_ll13:;
        if (gm_unw.kind == GM_BREAK && gm_unw.frame == &F.h && gm_unw.label == 0) { gm_unw.kind = GM_NONE; goto we12; }
        goto gm_lad;
gm_ls16:;
we12:;
        gm_b0_1 t17 = F.w0;
        gm_b0_1 t18 = t17;
    }
    {
        gm_b16_8 t20 = gm_l1;
        gm_b16_8 t21 = GM_LD(gm_b16_8, (((uint8_t *)&F.l6) + 0));
        gm_b16_8 t22 = t21;
        uint8_t t23 = 0;
        gm_b16_8 t24 = t20;
        for (uintptr_t i = 0; i < (uintptr_t)(GM_LD(uintptr_t, ((uint8_t *)&t22) + 8)); i++) if (gm_eq0iqiubfm7e(((uint8_t *)GM_LD(void *, ((uint8_t *)&t22))) + i * 16, ((uint8_t *)&t24))) { t23 = 1; break; }
        uint8_t t25 = (uint8_t)(!t23);
        if (!t25) goto ie19;
        {
            void * t27 = (void *)((uint8_t *)&F.l6);
            gm_b16_8 t28 = gm_l1;
            gm_tftb1odc739t5 t29 = gm_f_push_3ef746b(t27, t28);
            gm_tftb1odc739t5 t30 = t29;
            if (((((uint8_t *)&t30))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t30) + 1, gm_srcb7e7uf3pvdko, 334, 32);
            gm_b0_1 t31 = GM_LD(gm_b0_1, (((uint8_t *)&t30) + 1));
        }
        goto ix19;
ie19:;
ix19:;
    }
    {
        gm_f_list_epvducg((void *)&F.l8, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t33 = GM_LD(gm_b16_8, (((uint8_t *)&F.l6) + 0));
            gm_l9 = t33;
        }
        {
            gm_l10 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t35 = gm_l10;
                uint64_t t36 = GM_LD(uintptr_t, ((uint8_t *)&gm_l9) + 8);
                uint8_t t37 = (t35 < t36);
                if (!t37) goto lb34;
                {
                    uint64_t t40 = gm_l10;
                    uint64_t t41 = (uintptr_t)t40;
                    uint64_t t42 = GM_LD(uintptr_t, ((uint8_t *)&gm_l9) + 8);
                    gm_b16_8 t45 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l9)) + t41 * 16));
                    gm_l11 = t45;
                    {
                        gm_b16_8 t48 = gm_l11;
                        gm_b16_8 t49 = gm_l5;
                        uint8_t t50 = gm_f_starts_with_ahdikpt((void *)NULL, cx, NULL, t48, t49);
                        if (gm_unw.kind) {
                            goto gm_ll38;
                        }
                        uint8_t t51 = (uint8_t)!t50;
                        if (!t51) goto ie47;
                        {
                            void * t53 = (void *)((uint8_t *)&F.l8);
                            gm_b16_8 t54 = gm_l11;
                            gm_tftb1odc739t5 t55 = gm_f_push_3ef746b(t53, t54);
                            gm_tftb1odc739t5 t56 = t55;
                            if (((((uint8_t *)&t56))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t56) + 1, gm_srcb7e7uf3pvdko, 337, 43);
                            gm_b0_1 t57 = GM_LD(gm_b0_1, (((uint8_t *)&t56) + 1));
                        }
                        goto ix47;
ie47:;
ix47:;
                    }
                    goto me43;
mn44:;
                    gm_trap("no match arm matched the scrutinee", gm_srcb7e7uf3pvdko, 336, 5);
me43:;
                }
lc34:;
                {
                    uint64_t t59 = gm_l10;
                    uint64_t r60; bool o61 = __builtin_add_overflow(t59, ((uint64_t)1ULL), &r60);
                    uint64_t t62 = r60;
                    gm_l10 = t62;
                }
            }
gm_ll38:;
            if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb34; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc34; }
            }
            goto gm_lad;
lb34:;
        }
    }
    {
        gm_b16_8 t63 = GM_LD(gm_b16_8, (((uint8_t *)&F.l8) + 0));
        gm_b16_8 t64 = t63;
        gm_b16_8 t65 = GM_LD(gm_b16_8, ((uint8_t *)&t64));
        gm_b16_8 t66 = gm_l3;
        uint64_t t67 = gm_f_stamp_aq4ff3c((void *)NULL, cx, NULL, t65, t66);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l12 = t67;
    }
    {
        void * t69 = gm_l0;
        void * t70 = (void *)(((uint8_t *)t69) + 0);
        gm_b16_8 t71 = gm_l1;
        gm_timk834mo3ug9 t72;
        gm_f_get_rbph851((void *)&t72, t70, t71);
        gm_timk834mo3ug9 t73 = t72;
        if (!((((uint8_t *)&t73))[32] != 0)) goto ie68;
        gm_tqsio0a8q8soi t74 = GM_LD(gm_tqsio0a8q8soi, ((uint8_t *)&t73));
        gm_l13 = t74;
        {
            void * t76 = GM_LD(void *, (((uint8_t *)&gm_l13) + 0));
            gm_b0_1 t77 = gm_f_release_facts_faaib92((void *)NULL, cx, NULL, t76);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix68;
ie68:;
ix68:;
    }
    {
        void * t78 = gm_l0;
        void * t79 = (void *)(((uint8_t *)t78) + 0);
        gm_b16_8 t80 = gm_l1;
        gm_tabb710d9mq2e t81;
        gm_f_dupe_g9iqpvn((void *)&t81, (void *)NULL, cx, NULL, t80, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_k6dqjucsagv)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t82 = t81;
        if (((((uint8_t *)&t82))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t82) + 8, gm_srcb7e7uf3pvdko, 341, 18);
        gm_b16_8 t83 = GM_LD(gm_b16_8, (((uint8_t *)&t82) + 8));
        gm_b16_8 t84 = t83;
        gm_b16_8 t85 = GM_LD(gm_b16_8, ((uint8_t *)&t84));
        gm_tqsio0a8q8soi t86;
        void * t87 = gm_l4;
        GM_ST(void *, (uint8_t *)&t86 + 0, t87);
        gm_b16_8 t88 = GM_LD(gm_b16_8, (((uint8_t *)&F.l8) + 0));
        GM_ST(gm_b16_8, (uint8_t *)&t86 + 8, t88);
        uint64_t t89 = gm_l12;
        GM_ST(uint64_t, (uint8_t *)&t86 + 24, t89);
        gm_tftb1odc739t5 t90 = gm_f_put_j784qqi(t79, t85, &t86);
        gm_tftb1odc739t5 t91 = t90;
        if (((((uint8_t *)&t91))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t91) + 1, gm_srcb7e7uf3pvdko, 341, 5);
        gm_b0_1 t92 = GM_LD(gm_b0_1, (((uint8_t *)&t91) + 1));
    }
    {
        void * t93 = gm_l4;
        uint8_t t94 = 0;
        F.ret = t93;
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
inline __attribute__((aligned(4))) void gm_f_text_in_sgvn4eo(void *gm_out, void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_text_in_sgvn4eo F;
    void * gm_l0; /* f */
    gm_b16_8 gm_l1; /* path */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    gm_t6j6smj7qoctu gm_l4; /* t */
    GM_STACK_CHECK_SHALLOW(&F, 324u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        {
            void * t3 = gm_l0;
            gm_b16_8 t4 = GM_LD(gm_b16_8, ((((uint8_t *)t3) + 64) + 0));
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
                    gm_t6j6smj7qoctu t16 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t12 * 32));
                    gm_l4 = t16;
                    {
                        gm_b16_8 t19 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l4) + 0));
                        gm_b16_8 t20 = gm_l1;
                        gm_b16_8 t21 = t19;
                        gm_b16_8 t22 = t20;
                        uint8_t t23 = (gm_eq0iqiubfm7e(((uint8_t *)&t21), ((uint8_t *)&t22)));
                        if (!t23) goto ie18;
                        {
                            gm_b16_8 t25 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l4) + 16));
                            gm_th2lqbo77pvsc t26;
                            GM_ST(uint8_t, (uint8_t *)&t26 + 17, 0);
                            GM_ST(uint16_t, (uint8_t *)&t26 + 18, 0);
                            GM_ST(uint32_t, (uint8_t *)&t26 + 20, 0);
                            GM_ST(gm_b16_8, ((uint8_t *)&t26), t25);
                            ((uint8_t *)&t26)[16] = 1;
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
                    gm_trap("no match arm matched the scrutinee", gm_srcb7e7uf3pvdko, 154, 5);
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
        gm_th2lqbo77pvsc t33;
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
__attribute__((aligned(4))) gm_th2lqbo77pvsc gm_f_text_in_sgvn4eo_c(void * a0, gm_b16_8 a1) { gm_th2lqbo77pvsc gm_r; gm_f_text_in_sgvn4eo(&gm_r, a0, a1); return gm_r; }
extern __attribute__((aligned(4))) void gm_f_workspace_ubjeikp(void *gm_out, void *env, gm_node *cx, gm_node *inst);
