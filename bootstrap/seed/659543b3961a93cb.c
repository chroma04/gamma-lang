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
typedef struct { uint32_t f0; uint8_t p1[4]; uint64_t f1; void * f2; } gm_t5r2drn9pjqkt;
typedef struct { uint64_t f0; uint64_t f1; uint64_t f2; uint64_t f3; } gm_t66j1im9ip128;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint32_t f0; uint8_t f1; uint8_t pe[3]; } gm_t9805qtonbsag;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { void * f0; void * f1; void * f2; uint8_t f3; uint8_t pe[7]; } gm_tao63pk3r18v5;
typedef struct { uint32_t f0; uint32_t f1; } gm_tcc1rq4n1avb2;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint8_t f0; uint8_t p1[7]; void * f1; } gm_tgtub0v94d5u5;
typedef struct { uint8_t f0; uint8_t p1[1]; uint16_t f1; } gm_tjcj4j2gbh6c8;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { void * f0; void * f1; uint32_t f2; uint32_t f3; } gm_tmm67r2c94d5j;
typedef struct { uint64_t f0; uint32_t f1; uint8_t pe[4]; } gm_tn68el8d14vku;
_Static_assert(sizeof(gm_csguankko4lmhd) == 4 && _Alignof(gm_csguankko4lmhd) == 4 && offsetof(gm_csguankko4lmhd, f0) == 0, "layout of `Mutex`");
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_t08olq97rscn3;
typedef struct { gm_b16_8 f0; uint64_t f1; } gm_t3877pk7lv748;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
_Static_assert(sizeof(gm_t5r2drn9pjqkt) == 24 && _Alignof(gm_t5r2drn9pjqkt) == 8 && offsetof(gm_t5r2drn9pjqkt, f0) == 0 && offsetof(gm_t5r2drn9pjqkt, f1) == 8 && offsetof(gm_t5r2drn9pjqkt, f2) == 16, "layout of `PendingBody`");
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; gm_tl874t2atnlh3 f2; void * f3; gm_t9805qtonbsag f4; uint8_t f5; uint8_t pe[7]; } gm_t5v0c8ia6a5mo;
_Static_assert(sizeof(gm_t66j1im9ip128) == 32 && _Alignof(gm_t66j1im9ip128) == 8 && offsetof(gm_t66j1im9ip128, f0) == 0 && offsetof(gm_t66j1im9ip128, f1) == 8 && offsetof(gm_t66j1im9ip128, f2) == 16 && offsetof(gm_t66j1im9ip128, f3) == 24, "layout of `(types: usize, values: usize, fns: usize, consts: usize)`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_t79rbdv4rafe5;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; void * f1; } gm_t8ghgl2jj0qha;
_Static_assert(sizeof(gm_t9805qtonbsag) == 8 && _Alignof(gm_t9805qtonbsag) == 4 && offsetof(gm_t9805qtonbsag, f0) == 0 && offsetof(gm_t9805qtonbsag, f1) == 4, "layout of `?u32`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_b16_8 f2; gm_b16_8 f3; } gm_tammvh1iespi3;
_Static_assert(sizeof(gm_tao63pk3r18v5) == 32 && _Alignof(gm_tao63pk3r18v5) == 8 && offsetof(gm_tao63pk3r18v5, f0) == 0 && offsetof(gm_tao63pk3r18v5, f1) == 8 && offsetof(gm_tao63pk3r18v5, f2) == 16 && offsetof(gm_tao63pk3r18v5, f3) == 24, "layout of `SliceExpr`");
typedef struct { uint8_t f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; } gm_tbngattl1fo51;
_Static_assert(sizeof(gm_tcc1rq4n1avb2) == 8 && _Alignof(gm_tcc1rq4n1avb2) == 4 && offsetof(gm_tcc1rq4n1avb2, f0) == 0 && offsetof(gm_tcc1rq4n1avb2, f1) == 4, "layout of `LineCol`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tcmkompbl09il;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; gm_t88rcucg1b7uk f2; } gm_tcmtrmhj7ak4u;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; gm_b16_8 f1; gm_b16_8 f2; void * f3; gm_t9805qtonbsag f4; uint8_t f5; uint8_t pe[7]; } gm_tdgfduqjheqdm;
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t pe[4]; } gm_tep2drsl9408b;
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t f2; uint8_t p3[6]; void * f3; void * f4; gm_tl874t2atnlh3 f5; uint8_t pe[4]; } gm_tf2orqcrphmek;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; } gm_tgglga8ro3isc;
_Static_assert(sizeof(gm_tgtub0v94d5u5) == 16 && _Alignof(gm_tgtub0v94d5u5) == 8 && offsetof(gm_tgtub0v94d5u5, f0) == 0 && offsetof(gm_tgtub0v94d5u5, f1) == 8, "layout of `(is_const: bool, inner: ^Expr)`");
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { gm_b16_8 f0; gm_t9qkh0rsooc8d f1; uint8_t pe[7]; } gm_tibupt4dmcqai;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
_Static_assert(sizeof(gm_tjcj4j2gbh6c8) == 4 && _Alignof(gm_tjcj4j2gbh6c8) == 2 && offsetof(gm_tjcj4j2gbh6c8, f0) == 0 && offsetof(gm_tjcj4j2gbh6c8, f1) == 2, "layout of `(signed: bool, bits: u16)`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t f1; uint8_t pe[3]; } gm_tkn42u7na3t81;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
typedef struct { void * f0; gm_b16_8 f1; uint8_t f2; uint8_t pe[7]; } gm_tlljg61a2ctgq;
_Static_assert(sizeof(gm_tmm67r2c94d5j) == 24 && _Alignof(gm_tmm67r2c94d5j) == 8 && offsetof(gm_tmm67r2c94d5j, f0) == 0 && offsetof(gm_tmm67r2c94d5j, f1) == 8 && offsetof(gm_tmm67r2c94d5j, f2) == 16 && offsetof(gm_tmm67r2c94d5j, f3) == 20, "layout of `Printer`");
_Static_assert(sizeof(gm_tn68el8d14vku) == 16 && _Alignof(gm_tn68el8d14vku) == 8 && offsetof(gm_tn68el8d14vku, f0) == 0 && offsetof(gm_tn68el8d14vku, f1) == 8, "layout of `(i: usize, x: u32)`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t88rcucg1b7uk v0; } f1; } gm_tnnjeerolos0a;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
typedef struct { gm_b16_8 f0; uint32_t f1; uint8_t pe[4]; } gm_tommf4j9upomc;
typedef struct { gm_b16_8 f0; void * f1; } gm_tpe1hv3fekn05;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_t9qkh0rsooc8d v3; } f1; } gm_ts6oqppjh0ba3;
typedef struct { void * f0; uint32_t f1; gm_tl874t2atnlh3 f2; } gm_tupk62cr6lbdu;
typedef struct { gm_b16_8 f0; gm_t9qkh0rsooc8d f1; uint8_t p2[7]; gm_b16_8 f2; gm_t88rcucg1b7uk f3; uint8_t pe[4]; } gm_tv0k2vg3054a6;
_Static_assert(sizeof(gm_t08olq97rscn3) == 32 && _Alignof(gm_t08olq97rscn3) == 8 && offsetof(gm_t08olq97rscn3, f0) == 0 && offsetof(gm_t08olq97rscn3, f1) == 16, "layout of `File`");
typedef struct { gm_t08olq97rscn3 f0; uint8_t f1; uint8_t pe[7]; } gm_t1s75i08qtptd;
_Static_assert(sizeof(gm_t3877pk7lv748) == 24 && _Alignof(gm_t3877pk7lv748) == 8 && offsetof(gm_t3877pk7lv748, f0) == 0 && offsetof(gm_t3877pk7lv748, f1) == 16, "layout of `WasmReader`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tcmkompbl09il v0; gm_tiilmfd16v3fc v1; gm_b16_8 v2; } f1; } gm_t428ue2u3hf5g;
typedef struct { uint64_t f0; gm_tiilmfd16v3fc f1; } gm_t4f5fr60fo50r;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t p3[4]; gm_trqfvmjtt4sij f3; gm_trqfvmjtt4sij f4; gm_trqfvmjtt4sij f5; gm_th2lqbo77pvsc f6; } gm_t4gut2c9fj1kt;
_Static_assert(sizeof(gm_t5v0c8ia6a5mo) == 56 && _Alignof(gm_t5v0c8ia6a5mo) == 8 && offsetof(gm_t5v0c8ia6a5mo, f0) == 0 && offsetof(gm_t5v0c8ia6a5mo, f1) == 16 && offsetof(gm_t5v0c8ia6a5mo, f2) == 20 && offsetof(gm_t5v0c8ia6a5mo, f3) == 32 && offsetof(gm_t5v0c8ia6a5mo, f4) == 40 && offsetof(gm_t5v0c8ia6a5mo, f5) == 48, "layout of `ParamInfo`");
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_t79rbdv4rafe5) == 40 && _Alignof(gm_t79rbdv4rafe5) == 8 && offsetof(gm_t79rbdv4rafe5, f0) == 0 && offsetof(gm_t79rbdv4rafe5, f1) == 8 && offsetof(gm_t79rbdv4rafe5, f2) == 24, "layout of `Block`");
_Static_assert(sizeof(gm_t8ghgl2jj0qha) == 16 && _Alignof(gm_t8ghgl2jj0qha) == 8 && offsetof(gm_t8ghgl2jj0qha, f0) == 0 && offsetof(gm_t8ghgl2jj0qha, f1) == 8, "layout of `(op: UnOp, operand: ^Expr)`");
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; gm_tnnjeerolos0a f2; uint8_t pe[4]; } gm_t8nckeqfenl8a;
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
_Static_assert(sizeof(gm_tammvh1iespi3) == 56 && _Alignof(gm_tammvh1iespi3) == 8 && offsetof(gm_tammvh1iespi3, f0) == 0 && offsetof(gm_tammvh1iespi3, f1) == 16 && offsetof(gm_tammvh1iespi3, f2) == 24 && offsetof(gm_tammvh1iespi3, f3) == 40, "layout of `EffectInfo`");
typedef struct { uint8_t f0; uint8_t p1[7]; gm_trqfvmjtt4sij f1; } gm_tbci5ospk5nnn;
_Static_assert(sizeof(gm_tbngattl1fo51) == 8 && _Alignof(gm_tbngattl1fo51) == 4 && offsetof(gm_tbngattl1fo51, f0) == 0 && offsetof(gm_tbngattl1fo51, f1) == 4, "layout of `(is_const: bool, pointee: Ty)`");
typedef struct { gm_t08olq97rscn3 f0; gm_t9qkh0rsooc8d f1; uint8_t p2[7]; gm_b16_8 f2; void * f3; gm_tl874t2atnlh3 f4; uint8_t pe[4]; } gm_tc0sbs1tp6d7f;
_Static_assert(sizeof(gm_tcmkompbl09il) == 24 && _Alignof(gm_tcmkompbl09il) == 8 && offsetof(gm_tcmkompbl09il, f0) == 0 && offsetof(gm_tcmkompbl09il, f1) == 8, "layout of `(ty: Ty, bytes: string)`");
_Static_assert(sizeof(gm_tcmtrmhj7ak4u) == 12 && _Alignof(gm_tcmtrmhj7ak4u) == 4 && offsetof(gm_tcmtrmhj7ak4u, f0) == 0 && offsetof(gm_tcmtrmhj7ak4u, f1) == 4 && offsetof(gm_tcmtrmhj7ak4u, f2) == 8, "layout of `(op: LenOp, lhs: Ty, rhs: Ty)`");
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t p2[4]; gm_tnpifv20irnph f2; uint8_t f3; uint8_t pe[7]; } gm_tddtjfggdat0d;
_Static_assert(sizeof(gm_tdgfduqjheqdm) == 64 && _Alignof(gm_tdgfduqjheqdm) == 8 && offsetof(gm_tdgfduqjheqdm, f0) == 0 && offsetof(gm_tdgfduqjheqdm, f1) == 8 && offsetof(gm_tdgfduqjheqdm, f2) == 24 && offsetof(gm_tdgfduqjheqdm, f3) == 40 && offsetof(gm_tdgfduqjheqdm, f4) == 48 && offsetof(gm_tdgfduqjheqdm, f5) == 56, "layout of `FnDef`");
_Static_assert(sizeof(gm_tep2drsl9408b) == 24 && _Alignof(gm_tep2drsl9408b) == 8 && offsetof(gm_tep2drsl9408b, f0) == 0 && offsetof(gm_tep2drsl9408b, f1) == 16, "layout of `ValTy`");
typedef struct { gm_tnpifv20irnph f0; uint8_t f1; uint8_t pe[7]; } gm_teseu4tn5o422;
typedef struct { gm_t08olq97rscn3 f0; gm_th2lqbo77pvsc f1; } gm_tet9guhev5r4u;
_Static_assert(sizeof(gm_tf2orqcrphmek) == 56 && _Alignof(gm_tf2orqcrphmek) == 8 && offsetof(gm_tf2orqcrphmek, f0) == 0 && offsetof(gm_tf2orqcrphmek, f1) == 16 && offsetof(gm_tf2orqcrphmek, f2) == 17 && offsetof(gm_tf2orqcrphmek, f3) == 24 && offsetof(gm_tf2orqcrphmek, f4) == 32 && offsetof(gm_tf2orqcrphmek, f5) == 40, "layout of `Param`");
typedef struct { gm_tiilmfd16v3fc a[1]; } gm_tfe2ufaqq8re9;
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_tgglga8ro3isc) == 8 && _Alignof(gm_tgglga8ro3isc) == 4 && offsetof(gm_tgglga8ro3isc, f0) == 0 && offsetof(gm_tgglga8ro3isc, f1) == 4, "layout of `(err: Ty, ok: Ty)`");
typedef struct { uint64_t f0; gm_t5v0c8ia6a5mo f1; } gm_tgsvmddj3e85e;
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
_Static_assert(sizeof(gm_tibupt4dmcqai) == 24 && _Alignof(gm_tibupt4dmcqai) == 8 && offsetof(gm_tibupt4dmcqai, f0) == 0 && offsetof(gm_tibupt4dmcqai, f1) == 16, "layout of `StrLit`");
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
typedef struct { void * f0; gm_t08olq97rscn3 f1; } gm_tioie2ju3q4ul;
_Static_assert(sizeof(gm_tkn42u7na3t81) == 8 && _Alignof(gm_tkn42u7na3t81) == 4 && offsetof(gm_tkn42u7na3t81, f0) == 0 && offsetof(gm_tkn42u7na3t81, f1) == 4, "layout of `?Ty`");
_Static_assert(sizeof(gm_tlljg61a2ctgq) == 32 && _Alignof(gm_tlljg61a2ctgq) == 8 && offsetof(gm_tlljg61a2ctgq, f0) == 0 && offsetof(gm_tlljg61a2ctgq, f1) == 8 && offsetof(gm_tlljg61a2ctgq, f2) == 24, "layout of `(callee: ^Expr, args: []Arg, trailing_comma: bool)`");
_Static_assert(sizeof(gm_tnnjeerolos0a) == 8 && _Alignof(gm_tnnjeerolos0a) == 4 && offsetof(gm_tnnjeerolos0a, f0) == 0 && offsetof(gm_tnnjeerolos0a, f1) == 4, "layout of `ArrayKind`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
_Static_assert(sizeof(gm_tommf4j9upomc) == 24 && _Alignof(gm_tommf4j9upomc) == 8 && offsetof(gm_tommf4j9upomc, f0) == 0 && offsetof(gm_tommf4j9upomc, f1) == 16, "layout of `(string, u32)`");
_Static_assert(sizeof(gm_tpe1hv3fekn05) == 24 && _Alignof(gm_tpe1hv3fekn05) == 8 && offsetof(gm_tpe1hv3fekn05, f0) == 0 && offsetof(gm_tpe1hv3fekn05, f1) == 16, "layout of `(attrs: []Attribute, inner: ^Expr)`");
typedef struct { gm_t08olq97rscn3 f0; void * f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_tpv68m88f9lcr;
typedef struct { gm_tiilmfd16v3fc f0; uint8_t f1; uint8_t pe[7]; } gm_tqu3m2l3n8mnc;
typedef struct { gm_ts6oqppjh0ba3 f0; uint8_t p1[6]; void * f1; void * f2; } gm_trl9d7m1k5lbd;
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_ts6oqppjh0ba3) == 2 && _Alignof(gm_ts6oqppjh0ba3) == 1 && offsetof(gm_ts6oqppjh0ba3, f0) == 0 && offsetof(gm_ts6oqppjh0ba3, f1) == 1, "layout of `BinOp`");
_Static_assert(sizeof(gm_tupk62cr6lbdu) == 24 && _Alignof(gm_tupk62cr6lbdu) == 8 && offsetof(gm_tupk62cr6lbdu, f0) == 0 && offsetof(gm_tupk62cr6lbdu, f1) == 8 && offsetof(gm_tupk62cr6lbdu, f2) == 12, "layout of `(base: ^Expr, index: u32, index_span: Span)`");
_Static_assert(sizeof(gm_tv0k2vg3054a6) == 48 && _Alignof(gm_tv0k2vg3054a6) == 8 && offsetof(gm_tv0k2vg3054a6, f0) == 0 && offsetof(gm_tv0k2vg3054a6, f1) == 16 && offsetof(gm_tv0k2vg3054a6, f2) == 24 && offsetof(gm_tv0k2vg3054a6, f3) == 40, "layout of `OpInfo`");
_Static_assert(sizeof(gm_t1s75i08qtptd) == 40 && _Alignof(gm_t1s75i08qtptd) == 8 && offsetof(gm_t1s75i08qtptd, f0) == 0 && offsetof(gm_t1s75i08qtptd, f1) == 32, "layout of `?Ident`");
_Static_assert(sizeof(gm_t428ue2u3hf5g) == 32 && _Alignof(gm_t428ue2u3hf5g) == 8 && offsetof(gm_t428ue2u3hf5g, f0) == 0 && offsetof(gm_t428ue2u3hf5g, f1) == 8, "layout of `ConstKey`");
_Static_assert(sizeof(gm_t4f5fr60fo50r) == 32 && _Alignof(gm_t4f5fr60fo50r) == 8 && offsetof(gm_t4f5fr60fo50r, f0) == 0 && offsetof(gm_t4f5fr60fo50r, f1) == 8, "layout of `(i: usize, x: EffectRef)`");
_Static_assert(sizeof(gm_t4gut2c9fj1kt) == 184 && _Alignof(gm_t4gut2c9fj1kt) == 8 && offsetof(gm_t4gut2c9fj1kt, f0) == 0 && offsetof(gm_t4gut2c9fj1kt, f1) == 8 && offsetof(gm_t4gut2c9fj1kt, f2) == 24 && offsetof(gm_t4gut2c9fj1kt, f3) == 40 && offsetof(gm_t4gut2c9fj1kt, f4) == 80 && offsetof(gm_t4gut2c9fj1kt, f5) == 120 && offsetof(gm_t4gut2c9fj1kt, f6) == 160, "layout of `Diagnostic`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tjcj4j2gbh6c8 v0; uint16_t v1; gm_tbngattl1fo51 v2; gm_tbngattl1fo51 v3; gm_tbngattl1fo51 v4; gm_t8nckeqfenl8a v5; gm_t6j6smj7qoctu v6; gm_t88rcucg1b7uk v7; gm_tgglga8ro3isc v8; gm_tddtjfggdat0d v9; gm_tddtjfggdat0d v10; gm_tiilmfd16v3fc v11; gm_t88rcucg1b7uk v12; uint32_t v13; gm_tgglga8ro3isc v14; gm_t428ue2u3hf5g v15; gm_tcmtrmhj7ak4u v16; uint32_t v17; gm_tnpifv20irnph v18; uint32_t v19; } f1; } gm_t53at80pfdus3;
_Static_assert(sizeof(gm_t8nckeqfenl8a) == 24 && _Alignof(gm_t8nckeqfenl8a) == 8 && offsetof(gm_t8nckeqfenl8a, f0) == 0 && offsetof(gm_t8nckeqfenl8a, f1) == 8 && offsetof(gm_t8nckeqfenl8a, f2) == 12, "layout of `(len: u64, elem: Ty, kind: ArrayKind)`");
_Static_assert(sizeof(gm_tbci5ospk5nnn) == 48 && _Alignof(gm_tbci5ospk5nnn) == 8 && offsetof(gm_tbci5ospk5nnn, f0) == 0 && offsetof(gm_tbci5ospk5nnn, f1) == 8, "layout of `Int`");
_Static_assert(sizeof(gm_tc0sbs1tp6d7f) == 80 && _Alignof(gm_tc0sbs1tp6d7f) == 8 && offsetof(gm_tc0sbs1tp6d7f, f0) == 0 && offsetof(gm_tc0sbs1tp6d7f, f1) == 32 && offsetof(gm_tc0sbs1tp6d7f, f2) == 40 && offsetof(gm_tc0sbs1tp6d7f, f3) == 56 && offsetof(gm_tc0sbs1tp6d7f, f4) == 64, "layout of `OpDecl`");
typedef struct { gm_t1s75i08qtptd f0; void * f1; } gm_tchr52g0ema53;
_Static_assert(sizeof(gm_tddtjfggdat0d) == 48 && _Alignof(gm_tddtjfggdat0d) == 8 && offsetof(gm_tddtjfggdat0d, f0) == 0 && offsetof(gm_tddtjfggdat0d, f1) == 16 && offsetof(gm_tddtjfggdat0d, f2) == 24 && offsetof(gm_tddtjfggdat0d, f3) == 40, "layout of `FnTy`");
_Static_assert(sizeof(gm_teseu4tn5o422) == 24 && _Alignof(gm_teseu4tn5o422) == 8 && offsetof(gm_teseu4tn5o422, f0) == 0 && offsetof(gm_teseu4tn5o422, f1) == 16, "layout of `?EffectSet`");
_Static_assert(sizeof(gm_tet9guhev5r4u) == 56 && _Alignof(gm_tet9guhev5r4u) == 8 && offsetof(gm_tet9guhev5r4u, f0) == 0 && offsetof(gm_tet9guhev5r4u, f1) == 32, "layout of `IntrinsicExpr`");
_Static_assert(sizeof(gm_tfe2ufaqq8re9) == 24 && _Alignof(gm_tfe2ufaqq8re9) == 8 && offsetof(gm_tfe2ufaqq8re9, a) == 0, "layout of `[1]EffectRef`");
_Static_assert(sizeof(gm_tgsvmddj3e85e) == 64 && _Alignof(gm_tgsvmddj3e85e) == 8 && offsetof(gm_tgsvmddj3e85e, f0) == 0 && offsetof(gm_tgsvmddj3e85e, f1) == 8, "layout of `(i: usize, x: ParamInfo)`");
typedef struct { void * f0; gm_t1s75i08qtptd f1; void * f2; } gm_tioi3jh9v22g5;
_Static_assert(sizeof(gm_tioie2ju3q4ul) == 40 && _Alignof(gm_tioie2ju3q4ul) == 8 && offsetof(gm_tioie2ju3q4ul, f0) == 0 && offsetof(gm_tioie2ju3q4ul, f1) == 8, "layout of `(base: ^Expr, name: Ident)`");
typedef struct { gm_tbci5ospk5nnn f0; gm_tbci5ospk5nnn f1; } gm_tngtuiuskn938;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t88rcucg1b7uk v0; gm_t428ue2u3hf5g v1; } f1; } gm_toa2vhe3l4hfr;
_Static_assert(sizeof(gm_tpv68m88f9lcr) == 56 && _Alignof(gm_tpv68m88f9lcr) == 8 && offsetof(gm_tpv68m88f9lcr, f0) == 0 && offsetof(gm_tpv68m88f9lcr, f1) == 32 && offsetof(gm_tpv68m88f9lcr, f2) == 40, "layout of `Constraint`");
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t p2[4]; uint64_t f2; gm_b16_8 f3; gm_t88rcucg1b7uk f4; uint8_t p5[4]; gm_tnpifv20irnph f5; gm_b16_8 f6; gm_tqu3m2l3n8mnc f7; uint8_t f8; uint8_t pe[7]; } gm_tqqhoivahirr0;
_Static_assert(sizeof(gm_tqu3m2l3n8mnc) == 32 && _Alignof(gm_tqu3m2l3n8mnc) == 8 && offsetof(gm_tqu3m2l3n8mnc, f0) == 0 && offsetof(gm_tqu3m2l3n8mnc, f1) == 24, "layout of `?FnInstance`");
_Static_assert(sizeof(gm_trl9d7m1k5lbd) == 24 && _Alignof(gm_trl9d7m1k5lbd) == 8 && offsetof(gm_trl9d7m1k5lbd, f0) == 0 && offsetof(gm_trl9d7m1k5lbd, f1) == 8 && offsetof(gm_trl9d7m1k5lbd, f2) == 16, "layout of `(op: BinOp, lhs: ^Expr, rhs: ^Expr)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { void * v0; void * v1; gm_tibupt4dmcqai v2; uint8_t v3; gm_t08olq97rscn3 v4; gm_t08olq97rscn3 v5; void * v6; gm_b16_8 v7; void * v8; gm_t8ghgl2jj0qha v9; gm_trl9d7m1k5lbd v10; gm_tioi3jh9v22g5 v11; void * v12; void * v13; gm_t3mf58gv9kf3n v14; void * v15; gm_tlljg61a2ctgq v16; gm_t3mf58gv9kf3n v17; gm_tao63pk3r18v5 v18; gm_tioie2ju3q4ul v19; gm_tupk62cr6lbdu v20; void * v21; gm_tgtub0v94d5u5 v22; gm_tgtub0v94d5u5 v23; gm_tgtub0v94d5u5 v24; gm_t3mf58gv9kf3n v25; gm_t3mf58gv9kf3n v26; void * v27; void * v28; void * v29; void * v30; void * v31; gm_tpe1hv3fekn05 v32; void * v33; void * v34; gm_tibupt4dmcqai v35; void * v36; void * v37; void * v38; void * v39; void * v40; gm_t79rbdv4rafe5 v41; void * v42; gm_tchr52g0ema53 v43; gm_t1s75i08qtptd v44; gm_t3mf58gv9kf3n v45; gm_tet9guhev5r4u v46; } f1; } gm_t2gtbh39ptekd;
_Static_assert(sizeof(gm_t53at80pfdus3) == 56 && _Alignof(gm_t53at80pfdus3) == 8 && offsetof(gm_t53at80pfdus3, f0) == 0 && offsetof(gm_t53at80pfdus3, f1) == 8, "layout of `TyKind`");
_Static_assert(sizeof(gm_tchr52g0ema53) == 48 && _Alignof(gm_tchr52g0ema53) == 8 && offsetof(gm_tchr52g0ema53, f0) == 0 && offsetof(gm_tchr52g0ema53, f1) == 40, "layout of `BreakExpr`");
typedef struct { gm_tqqhoivahirr0 f0; uint8_t f1; uint8_t pe[7]; } gm_tg93b272d11vo;
_Static_assert(sizeof(gm_tioi3jh9v22g5) == 56 && _Alignof(gm_tioi3jh9v22g5) == 8 && offsetof(gm_tioi3jh9v22g5, f0) == 0 && offsetof(gm_tioi3jh9v22g5, f1) == 8 && offsetof(gm_tioi3jh9v22g5, f2) == 48, "layout of `ElseExpr`");
_Static_assert(sizeof(gm_tngtuiuskn938) == 96 && _Alignof(gm_tngtuiuskn938) == 8 && offsetof(gm_tngtuiuskn938, f0) == 0 && offsetof(gm_tngtuiuskn938, f1) == 48, "layout of `Rat`");
_Static_assert(sizeof(gm_toa2vhe3l4hfr) == 40 && _Alignof(gm_toa2vhe3l4hfr) == 8 && offsetof(gm_toa2vhe3l4hfr, f0) == 0 && offsetof(gm_toa2vhe3l4hfr, f1) == 8, "layout of `GenericArg`");
_Static_assert(sizeof(gm_tqqhoivahirr0) == 136 && _Alignof(gm_tqqhoivahirr0) == 8 && offsetof(gm_tqqhoivahirr0, f0) == 0 && offsetof(gm_tqqhoivahirr0, f1) == 16 && offsetof(gm_tqqhoivahirr0, f2) == 32 && offsetof(gm_tqqhoivahirr0, f3) == 40 && offsetof(gm_tqqhoivahirr0, f4) == 56 && offsetof(gm_tqqhoivahirr0, f5) == 64 && offsetof(gm_tqqhoivahirr0, f6) == 80 && offsetof(gm_tqqhoivahirr0, f7) == 96 && offsetof(gm_tqqhoivahirr0, f8) == 128, "layout of `FnSig`");
typedef struct { gm_tngtuiuskn938 f0; gm_tngtuiuskn938 f1; } gm_tu15p0hi2qlv6;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tbci5ospk5nnn v0; gm_tngtuiuskn938 v1; uint8_t v2; gm_tu15p0hi2qlv6 v3; } f1; } gm_t0ialelq3ql1k;
_Static_assert(sizeof(gm_t2gtbh39ptekd) == 64 && _Alignof(gm_t2gtbh39ptekd) == 8 && offsetof(gm_t2gtbh39ptekd, f0) == 0 && offsetof(gm_t2gtbh39ptekd, f1) == 8, "layout of `ExprKind`");
typedef struct { uint32_t f0; uint8_t p1[4]; gm_t2gtbh39ptekd f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_tc3oht4ksth9d;
_Static_assert(sizeof(gm_tg93b272d11vo) == 144 && _Alignof(gm_tg93b272d11vo) == 8 && offsetof(gm_tg93b272d11vo, f0) == 0 && offsetof(gm_tg93b272d11vo, f1) == 136, "layout of `?FnSig`");
_Static_assert(sizeof(gm_tu15p0hi2qlv6) == 192 && _Alignof(gm_tu15p0hi2qlv6) == 8 && offsetof(gm_tu15p0hi2qlv6, f0) == 0 && offsetof(gm_tu15p0hi2qlv6, f1) == 96, "layout of `(re: Rat, im: Rat)`");
_Static_assert(sizeof(gm_t0ialelq3ql1k) == 200 && _Alignof(gm_t0ialelq3ql1k) == 8 && offsetof(gm_t0ialelq3ql1k, f0) == 0 && offsetof(gm_t0ialelq3ql1k, f1) == 8, "layout of `Untyped`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t88rcucg1b7uk v0; uint32_t v1; uint32_t v2; gm_tiilmfd16v3fc v3; gm_tnpifv20irnph v4; uint32_t v5; gm_tcc1rq4n1avb2 v6; gm_t88rcucg1b7uk v7; gm_t0ialelq3ql1k v8; uint64_t v9; gm_t88rcucg1b7uk v10; } f1; } gm_t6qooom8qa2do;
_Static_assert(sizeof(gm_tc3oht4ksth9d) == 88 && _Alignof(gm_tc3oht4ksth9d) == 8 && offsetof(gm_tc3oht4ksth9d, f0) == 0 && offsetof(gm_tc3oht4ksth9d, f1) == 8 && offsetof(gm_tc3oht4ksth9d, f2) == 72, "layout of `Expr`");
_Static_assert(sizeof(gm_t6qooom8qa2do) == 208 && _Alignof(gm_t6qooom8qa2do) == 8 && offsetof(gm_t6qooom8qa2do, f0) == 0 && offsetof(gm_t6qooom8qa2do, f1) == 8, "layout of `DeclValue`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t6qooom8qa2do v0; } f1; } gm_tagrkbqh054h7;
_Static_assert(sizeof(gm_tagrkbqh054h7) == 216 && _Alignof(gm_tagrkbqh054h7) == 8 && offsetof(gm_tagrkbqh054h7, f0) == 0 && offsetof(gm_tagrkbqh054h7, f1) == 8, "layout of `DeclState`");
struct gm_fr_add_fn_t4t83qh {
    gm_frame h;
    gm_tqqhoivahirr0 l1; /* sig */
    gm_tdgfduqjheqdm l2; /* def */
    uint32_t ret;
};
struct gm_fr_bare_effect_ref_a5ofmji {
    gm_frame h;
    gm_trqfvmjtt4sij l7; /* args */
    gm_tiilmfd16v3fc ret;
};
struct gm_fr_bodiless_def_u9kqo09 {
    gm_frame h;
    gm_tdgfduqjheqdm ret;
};
struct gm_fr_check_distinct_d4svq2h {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_collect_terms_n8okg7m {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_declare_effect_9n12hc8 {
    gm_frame h;
    gm_trqfvmjtt4sij l6; /* params */
    gm_tf2orqcrphmek l9; /* p */
    gm_trqfvmjtt4sij l18; /* ops */
    gm_tc0sbs1tp6d7f l21; /* op */
    gm_trqfvmjtt4sij l22; /* op_params */
    gm_tf2orqcrphmek l25; /* p */
    gm_t6qooom8qa2do ret;
};
struct gm_fr_declare_fn_6piolnr {
    gm_frame h;
    gm_tqqhoivahirr0 l4; /* sig */
    gm_t5v0c8ia6a5mo l7; /* p */
    gm_t6qooom8qa2do ret;
};
struct gm_fr_effect_name_prpp0ip {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_effect_param_named_h2jeoo7 {
    gm_frame h;
    gm_tkn42u7na3t81 ret;
};
struct gm_fr_effect_terms_bphj7th {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* out */
    gm_b16_8 ret;
};
struct gm_fr_empty_effects_r0iv3pv {
    gm_frame h;
    gm_tnpifv20irnph ret;
};
struct gm_fr_empty_slice_5nn7ph0 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_6trg8r7 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_92arfe8 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_d914i5u {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_fh7svmj {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_he774nl {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ijtp9lo {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ito0pap {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_lp0ck0u {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_p6mn4us {
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
struct gm_fr_error_ki8i3k6 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_fresh_effect_2msfvhg {
    gm_frame h;
    uint32_t ret;
};
struct gm_fr_has_param_u9t0efs {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_has_splice_80pgqas {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_effect_sum_dd9le16 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_effects_kind_uutbf9u {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_site_8sr7vam {
    gm_frame h;
    gm_tet9guhev5r4u l2; /* i */
    uint8_t ret;
};
struct gm_fr_kind_jcnt4hm {
    gm_frame h;
    gm_t53at80pfdus3 ret;
};
struct gm_fr_list_084fcd6 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_a7rbkus {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_af74s0q {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_go2qg0b {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_qs7fohu {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_uvfu1k1 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_vmvl81b {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_may_coincide_f6v7r98 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_named_effect_set_s5gn1lr {
    gm_frame h;
    gm_teseu4tn5o422 ret;
};
struct gm_fr_new_literal_fn_oqjp4lg {
    gm_frame h;
    uint32_t ret;
};
struct gm_fr_new_nested_fn_r5u4ioo {
    gm_frame h;
    uint32_t ret;
};
struct gm_fr_param_n8kgsdm {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_printer_h7jigd7 {
    gm_frame h;
    gm_tmm67r2c94d5j ret;
};
struct gm_fr_provably_distinct_q4efcr7 {
    gm_frame h;
    gm_t53at80pfdus3 l3; /* ka */
    gm_t53at80pfdus3 l4; /* kb */
    uint8_t ret;
};
struct gm_fr_report_eardf07 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_resolve_effect_arg_4h6q4qv {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_resolve_effect_list_uhjlh6o {
    gm_frame h;
    gm_trqfvmjtt4sij l3; /* terms */
    gm_tnpifv20irnph ret;
};
struct gm_fr_resolve_effect_ref_bggirs2 {
    gm_frame h;
    gm_trqfvmjtt4sij l13; /* targs */
    gm_tc3oht4ksth9d l19; /* t */
    gm_tqu3m2l3n8mnc ret;
};
struct gm_fr_resolve_effect_set_o7rc5od {
    gm_frame h;
    gm_trqfvmjtt4sij l3; /* v */
    gm_tnpifv20irnph ret;
};
struct gm_fr_resolve_fn_sig_pamuhve {
    gm_frame h;
    gm_trqfvmjtt4sij l6; /* params */
    gm_trqfvmjtt4sij l7; /* generics */
    gm_tf2orqcrphmek l12; /* p */
    gm_t5v0c8ia6a5mo l37; /* p */
    gm_t5v0c8ia6a5mo l40; /* q */
    gm_tpv68m88f9lcr l47; /* c */
    gm_tg93b272d11vo ret;
};
struct gm_fr_restore_scope_edqq1v8 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_scope_mark_i3b2mvo {
    gm_frame h;
    gm_t66j1im9ip128 ret;
};
struct gm_fr_set_state_3j300fi {
    gm_frame h;
    gm_tagrkbqh054h7 l2; /* v */
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
struct gm_fr_show_ref_aqt1t7c {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* out */
    gm_b16_8 ret;
};
struct gm_fr_template_9o92cvv {
    gm_frame h;
    uint32_t ret;
};
struct gm_fr_truncate_7r782uc {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_truncate_daelje0 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_truncate_dsb08pk {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_truncate_upeumbt {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_type_ty_9ph2gbr {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_unit_2age34u {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
inline bool gm_eq467d5cm1d5(const uint8_t *a, const uint8_t *b);
inline bool gm_eq9m6mvtmgde(const uint8_t *a, const uint8_t *b);
inline __attribute__((aligned(4))) uint32_t gm_f_add_fn_t4t83qh(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, const void * a2);
__attribute__((aligned(4))) void gm_f_bare_effect_ref_a5ofmji(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
inline __attribute__((aligned(4))) void gm_f_bodiless_def_u9kqo09(void *gm_out, gm_t9qkh0rsooc8d a0);
__attribute__((aligned(4))) gm_tdgfduqjheqdm gm_f_bodiless_def_u9kqo09_c(gm_t9qkh0rsooc8d a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_check_distinct_d4svq2h(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) int32_t gm_f_cmp_ref_8ntrqll(void * a0, void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_collect_terms_n8okg7m(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_declare_constraint_s1juggf(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
__attribute__((aligned(4))) void gm_f_declare_effect_9n12hc8(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, uint64_t a2, const void * a3, void * a4);
__attribute__((aligned(4))) void gm_f_declare_fn_6piolnr(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, const void * a2, void * a3);
__attribute__((aligned(4))) gm_b0_1 gm_f_declare_method_candidate_e20bo1i(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, const void * a2, gm_t88rcucg1b7uk a3);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_effect_name_prpp0ip(void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_effect_param_named_h2jeoo7(void * a0, void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_effect_ref_6sjtehp(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) gm_tnpifv20irnph gm_f_effect_set_vtcpipr(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_effect_terms_bphj7th(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_tnpifv20irnph gm_f_empty_effects_r0iv3pv(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_5nn7ph0(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_6trg8r7(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_92arfe8(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_d914i5u(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_fh7svmj(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_he774nl(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ito0pap(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_lp0ck0u(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_p6mn4us(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_rcd2jln(void);
__attribute__((aligned(4))) gm_b0_1 gm_f_end_constraint_defer_52jt3m9(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_error_89glhjr(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_error_ki8i3k6(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_error_s6hn871(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_extend_74ac4hb(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_format_37dbjte(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_4e7kcl2(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_askmr98(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_kjp7rmf(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_n6tblha(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_uvukia4(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_vilpuor(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) uint32_t gm_f_fresh_effect_2msfvhg(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) uint8_t gm_f_has_param_u9t0efs(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) uint8_t gm_f_has_splice_80pgqas(gm_tnpifv20irnph a0);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_intern_ia19mhl(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) uint8_t gm_f_is_effect_sum_dd9le16(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
inline __attribute__((aligned(4))) uint8_t gm_f_is_effects_kind_uutbf9u(void * a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_site_8sr7vam(void * a0);
inline __attribute__((aligned(4))) void gm_f_kind_jcnt4hm(void *gm_out, void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void gm_f_list_084fcd6(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_a7rbkus(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_af74s0q(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_go2qg0b(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_qs7fohu(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_vmvl81b(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_lookup_module_name_cukfp7t(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) uint8_t gm_f_may_coincide_f6v7r98(void * a0, void * a1, void * a2);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_module_member_5fd2qm9(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, const void * a2, uint64_t a3);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_module_named_n1mcl3b(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
__attribute__((aligned(4))) void gm_f_named_effect_set_s5gn1lr(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
__attribute__((aligned(4))) uint8_t gm_f_names_error_oet8t6b(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
inline __attribute__((aligned(4))) uint32_t gm_f_new_literal_fn_oqjp4lg(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_tl874t2atnlh3 a2, uint64_t a3, gm_b16_8 a4, gm_t88rcucg1b7uk a5, gm_tnpifv20irnph a6);
__attribute__((aligned(4))) uint32_t gm_f_new_nested_fn_r5u4ioo(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t9qkh0rsooc8d a1, gm_b16_8 a2, gm_tl874t2atnlh3 a3, uint64_t a4, gm_b16_8 a5, gm_t88rcucg1b7uk a6, gm_tnpifv20irnph a7);
__attribute__((aligned(4))) uint32_t gm_f_new_param_7010dt4(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_tnnjeerolos0a a2);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_param_n8kgsdm(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
inline __attribute__((aligned(4))) void gm_f_printer_h7jigd7(void *gm_out, void * a0, void * a1);
__attribute__((aligned(4))) uint8_t gm_f_provably_distinct_q4efcr7(void * a0, gm_csguankko4lmhd a1, gm_csguankko4lmhd a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_3ef746b(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_472i7vl(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_57kepgg(void * a0, void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_700doiq(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_d1r4h19(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_hv483nm(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_im9d4ro(void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_o022ckl(void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_push_o4ckdr1(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_o8u01sh(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_puil3ql(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_roqi806(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_s5vr2a2(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_sp4si0f(void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_report_eardf07(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_resolve_decl_pus042v(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_resolve_effect_arg_4h6q4qv(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
__attribute__((aligned(4))) gm_tnpifv20irnph gm_f_resolve_effect_list_uhjlh6o(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_resolve_effect_ref_bggirs2(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
__attribute__((aligned(4))) gm_tnpifv20irnph gm_f_resolve_effect_set_o7rc5od(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_resolve_fn_sig_pamuhve(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2, uint8_t a3, gm_b16_8 a4);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_resolve_type_jjodbei(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
inline __attribute__((aligned(4))) void gm_f_restore_scope_edqq1v8(void * a0, const void * a1);
inline __attribute__((aligned(4))) void gm_f_scope_mark_i3b2mvo(void *gm_out, void * a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_set_state_3j300fi(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, const void * a2);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_8uijkbs(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_kmjeh8s(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_ref_aqt1t7c(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) void gm_f_splice_tj8iedl(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
__attribute__((aligned(4))) void gm_f_state_a5bnsjb(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
inline __attribute__((aligned(4))) uint32_t gm_f_template_9o92cvv(void * a0, uint32_t a1);
inline __attribute__((aligned(4))) void gm_f_truncate_7r782uc(void * a0, uint64_t a1);
inline __attribute__((aligned(4))) void gm_f_truncate_daelje0(void * a0, uint64_t a1);
inline __attribute__((aligned(4))) void gm_f_truncate_dsb08pk(void * a0, uint64_t a1);
inline __attribute__((aligned(4))) void gm_f_truncate_upeumbt(void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_ty_vka2r8h(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_type_param_3av4r8i(void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_type_ty_9ph2gbr(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_unit_2age34u(void *env, gm_node *cx, gm_node *inst, void * a0);
extern const uint8_t gm_s0ad3307s8jmo[105];
extern const uint8_t gm_s0sivi9n1gpid[74];
extern const uint8_t gm_s2737i1iq57nf[5];
extern const uint8_t gm_s8p6h6db1ftub[32];
extern const uint8_t gm_sa7gjgqof9s56[44];
extern const uint8_t gm_sa9e77lq9q04p[16];
extern const uint8_t gm_sba4cjt08ps1i[58];
extern const uint8_t gm_sdsd71bjtv81c[98];
extern const uint8_t gm_sfdbfrv46lihd[2];
extern const uint8_t gm_sfvsgjcg86bbl[45];
extern const uint8_t gm_sgbumttiou4de[8];
extern const uint8_t gm_snnov1h58rig5[52];
extern const uint8_t gm_sp929adkqo1cd[93];
extern const uint8_t gm_spfda095eoqse[34];
extern const uint8_t gm_spskd7qirpoe3[25];
extern const uint8_t gm_sq5p8t05ccn5h[24];
extern const uint8_t gm_sr4qbrk6tlgaa[1];
extern const char gm_src0ih77f43mhf2[];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_srclv0dcbbfhves[];
extern const char gm_srcmhupam4se3h5[];
extern const char gm_srcrgc9bkquf6nr[];
extern const uint8_t gm_sshv7blujc475[52];
extern const uint8_t gm_svurqcoqldl32[83];
extern const gm_type gm_tyo86ks9qkvt;
inline bool gm_eq467d5cm1d5(const uint8_t *a, const uint8_t *b) {
    return GM_LD(uint32_t, a) == GM_LD(uint32_t, b);
}
inline bool gm_eq9m6mvtmgde(const uint8_t *a, const uint8_t *b) {
    if (!gm_eq467d5cm1d5(a + 0, b + 0)) return false;
    return true;
}
inline __attribute__((aligned(4))) uint32_t gm_f_add_fn_t4t83qh(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, const void * a2) {
    struct gm_fr_add_fn_t4t83qh F;
    void * gm_l0; /* s */
    GM_STACK_CHECK(&F, 81u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        void * t3 = (void *)(((uint8_t *)t2) + 344);
        gm_tftb1odc739t5 t4 = gm_f_push_472i7vl(t3, &(*(gm_tqqhoivahirr0 *)((uint8_t *)a1)));
        if (((((uint8_t *)&t4))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t4) + 1, gm_srcmhupam4se3h5, 466, 5);
        gm_b0_1 t5 = GM_LD(gm_b0_1, (((uint8_t *)&t4) + 1));
    }
    {
        void * t6 = gm_l0;
        void * t7 = (void *)(((uint8_t *)t6) + 384);
        gm_tftb1odc739t5 t8 = gm_f_push_roqi806(t7, &(*(gm_tdgfduqjheqdm *)((uint8_t *)a2)));
        if (((((uint8_t *)&t8))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t8) + 1, gm_srcmhupam4se3h5, 467, 5);
        gm_b0_1 t9 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
    }
    {
        void * t10 = gm_l0;
        uint64_t t11 = GM_LD(uintptr_t, ((((uint8_t *)t10) + 344) + 0) + 8);
        uint64_t r12; bool o13 = __builtin_sub_overflow(t11, ((uint64_t)1ULL), &r12);
        if (o13) gm_trap("integer overflow in `-`", gm_srcmhupam4se3h5, 468, 13);
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
inline __attribute__((aligned(4))) void gm_f_bodiless_def_u9kqo09(void *gm_out, gm_t9qkh0rsooc8d a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_bodiless_def_u9kqo09 F;
    gm_t9qkh0rsooc8d gm_l0; /* kind */
    GM_STACK_CHECK_SHALLOW(&F, 42u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memset((uint8_t *)&(*(gm_tdgfduqjheqdm *)gm_out) + 1, 0, 7);
    memset((uint8_t *)&(*(gm_tdgfduqjheqdm *)gm_out) + 57, 0, 7);
    gm_t9qkh0rsooc8d t1 = gm_l0;
    gm_b16_8 t2 = gm_f_empty_slice_d914i5u();
    gm_b16_8 t3 = gm_f_empty_slice_6trg8r7();
    gm_t9805qtonbsag t4;
    memset(&t4, 0, sizeof t4);
    GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tdgfduqjheqdm *)gm_out) + 0, t1);
    GM_ST(gm_b16_8, (uint8_t *)&(*(gm_tdgfduqjheqdm *)gm_out) + 8, t2);
    GM_ST(gm_b16_8, (uint8_t *)&(*(gm_tdgfduqjheqdm *)gm_out) + 24, t3);
    GM_ST(void *, (uint8_t *)&(*(gm_tdgfduqjheqdm *)gm_out) + 40, ((void *)0));
    GM_ST(gm_t9805qtonbsag, (uint8_t *)&(*(gm_tdgfduqjheqdm *)gm_out) + 48, t4);
    GM_ST(uint8_t, (uint8_t *)&(*(gm_tdgfduqjheqdm *)gm_out) + 56, ((uint8_t)0));
    goto gm_fdone;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_effect_name_prpp0ip(void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_effect_name_prpp0ip F;
    void * gm_l0; /* t */
    uint32_t gm_l1; /* id */
    GM_STACK_CHECK_SHALLOW(&F, 96u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint32_t t1 = gm_l1;
    uint64_t t2 = (uint64_t)t1;
    uint64_t t3 = (uintptr_t)t2;
    void * t4 = gm_l0;
    uint64_t t5 = GM_LD(uintptr_t, ((((uint8_t *)t4) + 216) + 0) + 8);
    if (t3 >= (uintptr_t)(t5)) gm_trap_index(t3, t5, gm_src0ih77f43mhf2, 283, 62);
    gm_b16_8 t6 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t4) + 216) + 0)) + t3 * 16));
    F.ret = t6;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_effect_terms_bphj7th(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_effect_terms_bphj7th F;
    void * gm_l0; /* e */
    GM_STACK_CHECK(&F, 73u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_qs7fohu((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = gm_l0;
        void * t3 = (void *)((uint8_t *)&F.l1);
        gm_b0_1 t4 = gm_f_collect_terms_n8okg7m((void *)NULL, cx, NULL, t2, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_b16_8 t5 = GM_LD(gm_b16_8, (((uint8_t *)&F.l1) + 0));
        uint8_t t6 = 0;
        F.ret = t5;
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
inline __attribute__((aligned(4))) gm_tnpifv20irnph gm_f_empty_effects_r0iv3pv(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_effects_r0iv3pv F;
    GM_STACK_CHECK_SHALLOW(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_92arfe8();
    gm_b16_8 t2 = GM_LD(gm_b16_8, ((uint8_t *)&t1));
    gm_tnpifv20irnph t3;
    GM_ST(gm_b16_8, ((uint8_t *)&t3 + 0), t2);
    F.ret = t3;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_6trg8r7(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_6trg8r7 F;
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
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 40)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_92arfe8(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_92arfe8 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_d914i5u(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_d914i5u F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_fh7svmj(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_fh7svmj F;
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
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 40)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_he774nl(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_he774nl F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ito0pap(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_ito0pap F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_lp0ck0u(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_lp0ck0u F;
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
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 8)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_p6mn4us(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_p6mn4us F;
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
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 48)));
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
inline __attribute__((aligned(4))) uint32_t gm_f_fresh_effect_2msfvhg(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1) {
    struct gm_fr_fresh_effect_2msfvhg F;
    void * gm_l0; /* t */
    gm_b16_8 gm_l1; /* name */
    GM_STACK_CHECK(&F, 95u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        void * t3 = (void *)(((uint8_t *)t2) + 216);
        gm_b16_8 t4 = gm_l1;
        gm_tftb1odc739t5 t5 = gm_f_push_3ef746b(t3, t4);
        if (((((uint8_t *)&t5))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t5) + 1, gm_src0ih77f43mhf2, 279, 5);
        gm_b0_1 t6 = GM_LD(gm_b0_1, (((uint8_t *)&t5) + 1));
    }
    {
        void * t7 = gm_l0;
        uint64_t t8 = GM_LD(uintptr_t, ((((uint8_t *)t7) + 216) + 0) + 8);
        uint64_t r9; bool o10 = __builtin_sub_overflow(t8, ((uint64_t)1ULL), &r9);
        if (o10) gm_trap("integer overflow in `-`", gm_src0ih77f43mhf2, 280, 13);
        uint64_t t11 = r9;
        uint32_t t12 = (uint32_t)t11;
        uint8_t t13 = 0;
        F.ret = t12;
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_effects_kind_uutbf9u(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_effects_kind_uutbf9u F;
    void * gm_l0; /* t */
    gm_t08olq97rscn3 gm_l1; /* i */
    GM_STACK_CHECK_SHALLOW(&F, 86u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t3 = gm_l0;
        uint8_t t4 = GM_LD(uint8_t, ((((uint8_t *)t3) + 8) + 0));
        if (t4 != ((uint8_t)6ULL)) goto ie2;
        gm_t08olq97rscn3 t5 = GM_LD(gm_t08olq97rscn3, ((((uint8_t *)t3) + 8) + 8));
        gm_l1 = t5;
        {
            uint8_t t7 = (((GM_LD(uintptr_t, (((uint8_t *)&gm_l1) + 0) + 8) == 7) && memcmp(GM_LD(void *, (((uint8_t *)&gm_l1) + 0)), gm_sgbumttiou4de, 7) == 0));
            uint8_t t8 = 0;
            F.ret = t7;
            goto gm_fret;
        }
        goto ix2;
ie2:;
        {
            uint8_t t10 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
ix2:;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_site_8sr7vam(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_site_8sr7vam F;
    void * gm_l0; /* p */
    void * gm_l1; /* d */
    GM_STACK_CHECK_SHALLOW(&F, 119u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 32));
        void * t4;
        if ((GM_LD(void *, ((uint8_t *)&t3)) != 0)) {
            void * t5 = GM_LD(void *, (((uint8_t *)&t3) + 0));
            t4 = t5;
        } else {
            uint8_t t6 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        gm_l1 = t4;
    }
    {
        void * t8 = gm_l1;
        uint8_t t9 = GM_LD(uint8_t, ((((uint8_t *)t8) + 8) + 0));
        if (t9 != ((uint8_t)49ULL)) goto ie7;
        gm_tet9guhev5r4u t10 = GM_LD(gm_tet9guhev5r4u, ((((uint8_t *)t8) + 8) + 8));
        F.l2 = t10;
        {
            uint8_t t12 = (((GM_LD(uintptr_t, ((((uint8_t *)&F.l2) + 0) + 0) + 8) == 15) && memcmp(GM_LD(void *, ((((uint8_t *)&F.l2) + 0) + 0)), gm_sa9e77lq9q04p, 15) == 0));
            uint8_t t13 = 0;
            F.ret = t12;
            goto gm_fret;
        }
        goto ix7;
ie7:;
        {
            uint8_t t15 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
ix7:;
    }
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
inline __attribute__((aligned(4))) void gm_f_list_084fcd6(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_084fcd6 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_92arfe8();
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
inline __attribute__((aligned(4))) void gm_f_list_a7rbkus(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_a7rbkus F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_p6mn4us();
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
inline __attribute__((aligned(4))) void gm_f_list_af74s0q(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_af74s0q F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_he774nl();
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
inline __attribute__((aligned(4))) void gm_f_list_qs7fohu(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_qs7fohu F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_lp0ck0u();
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
inline __attribute__((aligned(4))) void gm_f_list_vmvl81b(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_vmvl81b F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_fh7svmj();
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
inline __attribute__((aligned(4))) uint32_t gm_f_new_literal_fn_oqjp4lg(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_tl874t2atnlh3 a2, uint64_t a3, gm_b16_8 a4, gm_t88rcucg1b7uk a5, gm_tnpifv20irnph a6) {
    struct gm_fr_new_literal_fn_oqjp4lg F;
    void * gm_l0; /* s */
    gm_b16_8 gm_l1; /* name */
    gm_tl874t2atnlh3 gm_l2; /* span */
    uint64_t gm_l3; /* module */
    gm_b16_8 gm_l4; /* params */
    gm_t88rcucg1b7uk gm_l5; /* ret */
    gm_tnpifv20irnph gm_l6; /* effects */
    GM_STACK_CHECK(&F, 174u);
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
        void * t2 = gm_l0;
        gm_t9qkh0rsooc8d t3;
        GM_ST(uint8_t, ((uint8_t *)&t3 + 0), ((uint8_t)1ULL));
        gm_b16_8 t4 = gm_l1;
        gm_tl874t2atnlh3 t5 = gm_l2;
        uint64_t t6 = gm_l3;
        gm_b16_8 t7 = gm_l4;
        gm_t88rcucg1b7uk t8 = gm_l5;
        gm_tnpifv20irnph t9 = gm_l6;
        uint32_t t10 = gm_f_new_nested_fn_r5u4ioo((void *)NULL, cx, NULL, t2, t3, t4, t5, t6, t7, t8, t9);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t11 = 0;
        F.ret = t10;
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_param_n8kgsdm(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1) {
    struct gm_fr_param_n8kgsdm F;
    void * gm_l0; /* t */
    uint32_t gm_l1; /* id */
    GM_STACK_CHECK(&F, 92u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    uint32_t t2 = gm_l1;
    gm_t53at80pfdus3 t3;
    memset(&t3, 0, sizeof t3);
    GM_ST(uint8_t, ((uint8_t *)&t3 + 0), ((uint8_t)19ULL));
    GM_ST(uint32_t, ((uint8_t *)&t3 + 8), t2);
    gm_t88rcucg1b7uk t4 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t1, &t3);
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
inline __attribute__((aligned(4))) void gm_f_restore_scope_edqq1v8(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_restore_scope_edqq1v8 F;
    void * gm_l0; /* s */
    gm_t66j1im9ip128 gm_l1; /* mark */
    GM_STACK_CHECK_SHALLOW(&F, 136u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        void * t3 = (void *)(((uint8_t *)t2) + 1488);
        uint64_t t4 = GM_LD(uint64_t, (((uint8_t *)a1) + 0));
        gm_f_truncate_7r782uc(t3, t4);
    }
    {
        void * t5 = gm_l0;
        void * t6 = (void *)(((uint8_t *)t5) + 1528);
        uint64_t t7 = GM_LD(uint64_t, (((uint8_t *)a1) + 8));
        gm_f_truncate_daelje0(t6, t7);
    }
    {
        void * t8 = gm_l0;
        void * t9 = (void *)(((uint8_t *)t8) + 1608);
        uint64_t t10 = GM_LD(uint64_t, (((uint8_t *)a1) + 16));
        gm_f_truncate_dsb08pk(t9, t10);
    }
    {
        void * t11 = gm_l0;
        void * t12 = (void *)(((uint8_t *)t11) + 1568);
        uint64_t t13 = GM_LD(uint64_t, (((uint8_t *)a1) + 24));
        gm_f_truncate_upeumbt(t12, t13);
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_scope_mark_i3b2mvo(void *gm_out, void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_scope_mark_i3b2mvo F;
    void * gm_l0; /* s */
    GM_STACK_CHECK_SHALLOW(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    uint64_t t2 = GM_LD(uintptr_t, ((((uint8_t *)t1) + 1488) + 0) + 8);
    void * t3 = gm_l0;
    uint64_t t4 = GM_LD(uintptr_t, ((((uint8_t *)t3) + 1528) + 0) + 8);
    void * t5 = gm_l0;
    uint64_t t6 = GM_LD(uintptr_t, ((((uint8_t *)t5) + 1608) + 0) + 8);
    void * t7 = gm_l0;
    uint64_t t8 = GM_LD(uintptr_t, ((((uint8_t *)t7) + 1568) + 0) + 8);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_t66j1im9ip128 *)gm_out) + 0, t2);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_t66j1im9ip128 *)gm_out) + 8, t4);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_t66j1im9ip128 *)gm_out) + 16, t6);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_t66j1im9ip128 *)gm_out) + 24, t8);
    goto gm_fdone;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_set_state_3j300fi(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, const void * a2) {
    struct gm_fr_set_state_3j300fi F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* id */
    GM_STACK_CHECK(&F, 496u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    memcpy(&F.l2, a2, sizeof F.l2);
    {
        {
            void * t3 = gm_l0;
            uint64_t t4 = gm_l1;
            gm_tagrkbqh054h7 t5;
            gm_f_state_a5bnsjb((void *)&t5, (void *)NULL, cx, NULL, t3, t4);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
    }
    {
        uint64_t t6 = gm_l1;
        uint64_t t7 = (uintptr_t)t6;
        void * t8 = gm_l0;
        uint64_t t9 = GM_LD(uintptr_t, ((((uint8_t *)t8) + 304) + 0) + 8);
        if (t7 >= (uintptr_t)(t9)) gm_trap_index(t7, t9, gm_srclv0dcbbfhves, 752, 5);
        gm_tagrkbqh054h7 t10 = F.l2;
        GM_ST(gm_tagrkbqh054h7, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t8) + 304) + 0)) + t7 * 216), t10);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_kmjeh8s(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_show_kmjeh8s F;
    void * gm_l0; /* t */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    gm_tmm67r2c94d5j gm_l3; /* p */
    GM_STACK_CHECK(&F, 129u);
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
        gm_b16_8 t8 = GM_LD(gm_b16_8, ((uint8_t *)&t7));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_ref_aqt1t7c(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_show_ref_aqt1t7c F;
    void * gm_l0; /* t */
    void * gm_l1; /* e */
    gm_tmm67r2c94d5j gm_l3; /* p */
    GM_STACK_CHECK(&F, 137u);
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
        void * t5 = gm_l1;
        gm_b0_1 t6 = gm_f_effect_ref_6sjtehp((void *)NULL, cx, NULL, t4, t5);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_b16_8 t7 = GM_LD(gm_b16_8, (((uint8_t *)&F.l2) + 0));
        gm_b16_8 t8 = GM_LD(gm_b16_8, ((uint8_t *)&t7));
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
inline __attribute__((aligned(4))) uint32_t gm_f_template_9o92cvv(void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_template_9o92cvv F;
    void * gm_l0; /* t */
    uint32_t gm_l1; /* id */
    GM_STACK_CHECK_SHALLOW(&F, 60u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint32_t t1 = gm_l1;
    uint64_t t2 = (uint64_t)t1;
    uint64_t t3 = (uintptr_t)t2;
    void * t4 = gm_l0;
    uint64_t t5 = GM_LD(uintptr_t, ((((uint8_t *)t4) + 176) + 0) + 8);
    if (t3 >= (uintptr_t)(t5)) gm_trap_index(t3, t5, gm_src0ih77f43mhf2, 276, 56);
    uint32_t t6 = GM_LD(uint32_t, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t4) + 176) + 0)) + t3 * 4));
    F.ret = t6;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_truncate_7r782uc(void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_truncate_7r782uc F;
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
inline __attribute__((aligned(4))) void gm_f_truncate_daelje0(void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_truncate_daelje0 F;
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
inline __attribute__((aligned(4))) void gm_f_truncate_dsb08pk(void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_truncate_dsb08pk F;
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
inline __attribute__((aligned(4))) void gm_f_truncate_upeumbt(void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_truncate_upeumbt F;
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
            GM_ST(void *, (uint8_t *)&t15, (void *)(((uint8_t *)t11 + t12 * 224)));
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_type_ty_9ph2gbr(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_type_ty_9ph2gbr F;
    void * gm_l0; /* t */
    GM_STACK_CHECK(&F, 84u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    gm_t53at80pfdus3 t2;
    memset(&t2, 0, sizeof t2);
    GM_ST(uint8_t, ((uint8_t *)&t2 + 0), ((uint8_t)6ULL));
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
extern __attribute__((aligned(4))) uint32_t gm_f_add_fn_t4t83qh(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, const void * a2);
__attribute__((aligned(4))) void gm_f_bare_effect_ref_a5ofmji(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1) {
    struct gm_fr_bare_effect_ref_a5ofmji F;
    void * gm_l0; /* s */
    uint32_t gm_l1; /* eff */
    gm_b16_8 gm_l2; /* params */
    uint8_t gm_l3; /* all_sets */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    uint32_t gm_l6; /* p */
    gm_b16_8 gm_l8; /* <tmp> */
    uint64_t gm_l9; /* <tmp> */
    GM_STACK_CHECK(&F, 553u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint32_t t2 = gm_l1;
        uint64_t t3 = (uint64_t)t2;
        uint64_t t4 = (uintptr_t)t3;
        void * t5 = gm_l0;
        uint64_t t6 = GM_LD(uintptr_t, ((((uint8_t *)t5) + 1048) + 0) + 8);
        if (t4 >= (uintptr_t)(t6)) gm_trap_index(t4, t6, gm_srcmhupam4se3h5, 370, 15);
        gm_b16_8 t7 = GM_LD(gm_b16_8, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t5) + 1048) + 0)) + t4 * 56) + 24));
        gm_l2 = t7;
    }
    {
        uint64_t t8 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        uint8_t t9 = (t8 > ((uint64_t)0ULL));
        gm_l3 = t9;
    }
    {
        {
            gm_b16_8 t11 = gm_l2;
            gm_l4 = t11;
        }
        {
            gm_l5 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t13 = gm_l5;
                uint64_t t14 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                uint8_t t15 = (t13 < t14);
                if (!t15) goto lb12;
                {
                    uint64_t t18 = gm_l5;
                    uint64_t t19 = (uintptr_t)t18;
                    uint64_t t20 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                    uint32_t t21 = GM_LD(uint32_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t19 * 4));
                    uint32_t t24 = GM_LD(uint32_t, ((uint8_t *)&t21));
                    gm_l6 = t24;
                    {
                        uint32_t t27 = gm_l6;
                        uint64_t t28 = (uint64_t)t27;
                        uint64_t t29 = (uintptr_t)t28;
                        void * t30 = gm_l0;
                        uint64_t t31 = GM_LD(uintptr_t, ((((uint8_t *)t30) + 1128) + 0) + 8);
                        if (t29 >= (uintptr_t)(t31)) gm_trap_index(t29, t31, gm_srcmhupam4se3h5, 373, 12);
                        uint8_t t32 = (!(GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t30) + 1128) + 0)) + t29 * 48) + 0) + 0) == ((uint8_t)1ULL)));
                        if (!t32) goto ie26;
                        {
                            gm_l3 = ((uint8_t)0);
                        }
                        goto ix26;
ie26:;
ix26:;
                    }
                    goto me22;
mn23:;
                    gm_trap("no match arm matched the scrutinee", gm_srcmhupam4se3h5, 372, 5);
me22:;
                }
lc12:;
                {
                    uint64_t t35 = gm_l5;
                    uint64_t r36; bool o37 = __builtin_add_overflow(t35, ((uint64_t)1ULL), &r36);
                    uint64_t t38 = r36;
                    gm_l5 = t38;
                }
            }
lb12:;
        }
    }
    {
        uint8_t t40 = gm_l3;
        uint8_t t41 = (uint8_t)!t40;
        if (!t41) goto ie39;
        {
            memset((uint8_t *)&(*(gm_tiilmfd16v3fc *)gm_out) + 4, 0, 4);
            uint32_t t43 = gm_l1;
            gm_b16_8 t44 = gm_f_empty_slice_fh7svmj();
            gm_b16_8 t45 = GM_LD(gm_b16_8, ((uint8_t *)&t44));
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tiilmfd16v3fc *)gm_out) + 0, t43);
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_tiilmfd16v3fc *)gm_out) + 8, t45);
            goto gm_fdone;
        }
        goto ix39;
ie39:;
ix39:;
    }
    {
        gm_f_list_vmvl81b((void *)&F.l7, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t47 = gm_l2;
            gm_l8 = t47;
        }
        {
            gm_l9 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t49 = gm_l9;
                uint64_t t50 = GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8);
                uint8_t t51 = (t49 < t50);
                if (!t51) goto lb48;
                {
                    uint64_t t54 = gm_l9;
                    uint64_t t55 = (uintptr_t)t54;
                    uint64_t t56 = GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8);
                    uint32_t t57 = GM_LD(uint32_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l8)) + t55 * 4));
                    {
                        void * t61 = (void *)((uint8_t *)&F.l7);
                        void * t62 = gm_l0;
                        void * t63 = (void *)(((uint8_t *)t62) + 8);
                        gm_tnpifv20irnph t64 = gm_f_empty_effects_r0iv3pv();
                        gm_t53at80pfdus3 t65;
                        memset(&t65, 0, sizeof t65);
                        GM_ST(uint8_t, ((uint8_t *)&t65 + 0), ((uint8_t)24ULL));
                        GM_ST(gm_tnpifv20irnph, ((uint8_t *)&t65 + 8), t64);
                        gm_t88rcucg1b7uk t66 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t63, &t65);
                        if (gm_unw.kind) {
                            goto gm_ll52;
                        }
                        gm_toa2vhe3l4hfr t67;
                        memset(&t67, 0, sizeof t67);
                        GM_ST(uint8_t, ((uint8_t *)&t67 + 0), ((uint8_t)0ULL));
                        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t67 + 8), t66);
                        gm_tftb1odc739t5 t68 = gm_f_push_s5vr2a2(t61, &t67);
                        if (((((uint8_t *)&t68))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t68) + 1, gm_srcmhupam4se3h5, 377, 23);
                        gm_b0_1 t69 = GM_LD(gm_b0_1, (((uint8_t *)&t68) + 1));
                    }
                    goto me58;
mn59:;
                    gm_trap("no match arm matched the scrutinee", gm_srcmhupam4se3h5, 377, 5);
me58:;
                }
lc48:;
                {
                    uint64_t t71 = gm_l9;
                    uint64_t r72; bool o73 = __builtin_add_overflow(t71, ((uint64_t)1ULL), &r72);
                    uint64_t t74 = r72;
                    gm_l9 = t74;
                }
            }
gm_ll52:;
            if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb48; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc48; }
            }
            goto gm_lad;
lb48:;
        }
    }
    {
        memset((uint8_t *)&(*(gm_tiilmfd16v3fc *)gm_out) + 4, 0, 4);
        uint32_t t75 = gm_l1;
        gm_b16_8 t76 = GM_LD(gm_b16_8, (((uint8_t *)&F.l7) + 0));
        gm_b16_8 t77 = GM_LD(gm_b16_8, ((uint8_t *)&t76));
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tiilmfd16v3fc *)gm_out) + 0, t75);
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_tiilmfd16v3fc *)gm_out) + 8, t77);
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
extern __attribute__((aligned(4))) void gm_f_bodiless_def_u9kqo09(void *gm_out, gm_t9qkh0rsooc8d a0);
__attribute__((aligned(4))) gm_tdgfduqjheqdm gm_f_bodiless_def_u9kqo09_c(gm_t9qkh0rsooc8d a0) { gm_tdgfduqjheqdm gm_r; gm_f_bodiless_def_u9kqo09(&gm_r, a0); return gm_r; }
__attribute__((aligned(4))) gm_b0_1 gm_f_check_distinct_d4svq2h(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2) {
    struct gm_fr_check_distinct_d4svq2h F;
    void * gm_l0; /* s */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_b16_8 gm_l2; /* refs */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    uint64_t gm_l5; /* i */
    gm_tiilmfd16v3fc gm_l6; /* a */
    gm_b16_8 gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    gm_tiilmfd16v3fc gm_l9; /* b */
    GM_STACK_CHECK(&F, 611u);
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
                    gm_t4f5fr60fo50r t10;
                    uint64_t t11 = gm_l4;
                    GM_ST(uint64_t, (uint8_t *)&t10 + 0, t11);
                    uint64_t t12 = gm_l4;
                    uint64_t t13 = (uintptr_t)t12;
                    uint64_t t14 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                    memcpy((uint8_t *)&t10 + 8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t13 * 24), 24);
                    uint64_t t17 = GM_LD(uint64_t, (((uint8_t *)&t10) + 0));
                    gm_l5 = t17;
                    gm_l6 = (*(gm_tiilmfd16v3fc *)(((uint8_t *)&t10) + 8));
                    {
                        {
                            uint64_t t20 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                            void * t21 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)));
                            uint64_t t22 = gm_l5;
                            uint64_t r23; bool o24 = __builtin_add_overflow(t22, ((uint64_t)1ULL), &r23);
                            if (o24) gm_trap("integer overflow in `+`", gm_srcmhupam4se3h5, 154, 23);
                            uint64_t t25 = r23;
                            uint64_t t26 = (uintptr_t)t25;
                            if (t26 > (uintptr_t)t20 || (uintptr_t)t20 > (uintptr_t)t20) gm_trap_slice(t26, (uintptr_t)t20, t20, gm_srcmhupam4se3h5, 154, 18);
                            gm_b16_8 t27;
                            GM_ST(void *, (uint8_t *)&t27, (void *)(((uint8_t *)t21 + t26 * 24)));
                            GM_ST(uintptr_t, (uint8_t *)&t27 + 8, (uintptr_t)((uintptr_t)t20 - t26));
                            gm_l7 = t27;
                        }
                        {
                            gm_l8 = ((uint64_t)0ULL);
                        }
                        {
                            for (;;) {
                                uint64_t t29 = gm_l8;
                                uint64_t t30 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                                uint8_t t31 = (t29 < t30);
                                if (!t31) goto lb28;
                                {
                                    uint64_t t34 = gm_l8;
                                    uint64_t t35 = (uintptr_t)t34;
                                    uint64_t t36 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                                    gm_tiilmfd16v3fc t39 = GM_LD(gm_tiilmfd16v3fc, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l7)) + t35 * 24));
                                    gm_l9 = t39;
                                    {
                                        void * t42 = gm_l0;
                                        void * t43 = (void *)((uint8_t *)&gm_l6);
                                        void * t44 = (void *)((uint8_t *)&gm_l9);
                                        uint8_t t45 = gm_f_may_coincide_f6v7r98(t42, t43, t44);
                                        if (!t45) goto ie41;
                                        {
                                            void * t47 = gm_l0;
                                            gm_tl874t2atnlh3 t48 = gm_l1;
                                            gm_t6j6smj7qoctu t49;
                                            void * t50 = gm_l0;
                                            void * t51 = (void *)(((uint8_t *)t50) + 8);
                                            void * t52 = (void *)((uint8_t *)&gm_l6);
                                            gm_b16_8 t53 = gm_f_show_ref_aqt1t7c((void *)NULL, cx, NULL, t51, t52);
                                            if (gm_unw.kind) {
                                                goto gm_ll32;
                                            }
                                            GM_ST(gm_b16_8, (uint8_t *)&t49 + 0, t53);
                                            void * t54 = gm_l0;
                                            void * t55 = (void *)(((uint8_t *)t54) + 8);
                                            void * t56 = (void *)((uint8_t *)&gm_l9);
                                            gm_b16_8 t57 = gm_f_show_ref_aqt1t7c((void *)NULL, cx, NULL, t55, t56);
                                            if (gm_unw.kind) {
                                                goto gm_ll32;
                                            }
                                            GM_ST(gm_b16_8, (uint8_t *)&t49 + 16, t57);
                                            gm_tabb710d9mq2e t58;
                                            gm_f_format_4e7kcl2((void *)&t58, (void *)NULL, cx, NULL, &t49);
                                            if (gm_unw.kind) {
                                                goto gm_ll32;
                                            }
                                            if (((((uint8_t *)&t58))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t58) + 8, gm_srcmhupam4se3h5, 156, 31);
                                            gm_b16_8 t59 = GM_LD(gm_b16_8, (((uint8_t *)&t58) + 8));
                                            gm_b16_8 t60 = GM_LD(gm_b16_8, ((uint8_t *)&t59));
                                            gm_b0_1 t61 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t47, t48, t60);
                                            if (gm_unw.kind) {
                                                goto gm_ll32;
                                            }
                                        }
                                        goto ix41;
ie41:;
ix41:;
                                    }
                                    goto me37;
mn38:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcmhupam4se3h5, 154, 9);
me37:;
                                }
lc28:;
                                {
                                    uint64_t t63 = gm_l8;
                                    uint64_t r64; bool o65 = __builtin_add_overflow(t63, ((uint64_t)1ULL), &r64);
                                    uint64_t t66 = r64;
                                    gm_l8 = t66;
                                }
                            }
gm_ll32:;
                            if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb28; }
                                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc28; }
                            }
                            goto gm_ll8;
lb28:;
                        }
                    }
                    goto me15;
mn16:;
                    gm_trap("no match arm matched the scrutinee", gm_srcmhupam4se3h5, 153, 5);
me15:;
                }
lc4:;
                {
                    uint64_t t68 = gm_l4;
                    uint64_t r69; bool o70 = __builtin_add_overflow(t68, ((uint64_t)1ULL), &r69);
                    uint64_t t71 = r69;
                    gm_l4 = t71;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_collect_terms_n8okg7m(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_collect_terms_n8okg7m F;
    void * gm_l0; /* e */
    void * gm_l1; /* out */
    void * gm_l2; /* lhs */
    void * gm_l3; /* rhs */
    GM_STACK_CHECK(&F, 109u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t3 = gm_l0;
        uint8_t t4 = GM_LD(uint8_t, ((((uint8_t *)t3) + 8) + 0));
        if (t4 != ((uint8_t)13ULL)) goto ie2;
        uint8_t t5 = GM_LD(uint8_t, ((((((uint8_t *)t3) + 8) + 8) + 0) + 0));
        if (t5 != ((uint8_t)0ULL)) goto ie2;
        uint8_t t6 = GM_LD(uint8_t, (((((((uint8_t *)t3) + 8) + 8) + 0) + 1) + 0));
        if (t6 != ((uint8_t)0ULL)) goto ie2;
        void * t7 = GM_LD(void *, (((((uint8_t *)t3) + 8) + 8) + 8));
        gm_l2 = t7;
        void * t8 = GM_LD(void *, (((((uint8_t *)t3) + 8) + 8) + 16));
        gm_l3 = t8;
        {
            void * t10 = gm_l2;
            void * t11 = gm_l1;
            gm_b0_1 t12 = gm_f_collect_terms_n8okg7m((void *)NULL, cx, NULL, t10, t11);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t13 = gm_l3;
            void * t14 = gm_l1;
            gm_b0_1 t15 = gm_f_collect_terms_n8okg7m((void *)NULL, cx, NULL, t13, t14);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix2;
ie2:;
        {
            void * t17 = gm_l1;
            void * t18 = gm_l0;
            gm_tftb1odc739t5 t19 = gm_f_push_57kepgg(t17, t18);
            if (((((uint8_t *)&t19))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t19) + 1, gm_srcmhupam4se3h5, 221, 9);
            gm_b0_1 t20 = GM_LD(gm_b0_1, (((uint8_t *)&t19) + 1));
        }
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
__attribute__((aligned(4))) void gm_f_declare_effect_9n12hc8(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, uint64_t a2, const void * a3, void * a4) {
    struct gm_fr_declare_effect_9n12hc8 F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* module */
    uint64_t gm_l2; /* decl */
    gm_t08olq97rscn3 gm_l3; /* name */
    void * gm_l4; /* lit */
    uint32_t gm_l5; /* eff */
    gm_b16_8 gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    uint8_t gm_l10; /* is_type */
    uint8_t gm_l11; /* is_effects */
    void * gm_l12; /* t */
    gm_t08olq97rscn3 gm_l13; /* i */
    gm_b16_8 gm_l14; /* <tmp> */
    uint64_t gm_l15; /* <tmp> */
    gm_t08olq97rscn3 gm_l16; /* n */
    uint32_t gm_l17; /* pid */
    gm_b16_8 gm_l19; /* <tmp> */
    uint64_t gm_l20; /* <tmp> */
    gm_b16_8 gm_l23; /* <tmp> */
    uint64_t gm_l24; /* <tmp> */
    gm_t88rcucg1b7uk gm_l26; /* ty */
    void * gm_l27; /* t */
    gm_b16_8 gm_l28; /* <tmp> */
    uint64_t gm_l29; /* <tmp> */
    gm_t08olq97rscn3 gm_l30; /* n */
    gm_t88rcucg1b7uk gm_l31; /* ret */
    void * gm_l32; /* r */
    GM_STACK_CHECK(&F, 2411u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l4 = a4;
    {
        void * t2 = gm_l0;
        void * t3 = (void *)(((uint8_t *)t2) + 8);
        gm_b16_8 t4 = GM_LD(gm_b16_8, (((uint8_t *)a3) + 0));
        uint32_t t5 = gm_f_fresh_effect_2msfvhg((void *)NULL, cx, NULL, t3, t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l5 = t5;
    }
    {
        gm_f_list_go2qg0b((void *)&F.l6, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            void * t7 = gm_l4;
            gm_th2lqbo77pvsc t8 = GM_LD(gm_th2lqbo77pvsc, (((uint8_t *)t7) + 0));
            gm_b16_8 t9;
            if (((((uint8_t *)&t8))[16] != 0)) {
                gm_b16_8 t10 = GM_LD(gm_b16_8, (((uint8_t *)&t8) + 0));
                t9 = t10;
            } else {
                gm_b16_8 t11 = gm_f_empty_slice_ito0pap();
                t9 = t11;
            }
            gm_l7 = t9;
        }
        {
            gm_l8 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t13 = gm_l8;
                uint64_t t14 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                uint8_t t15 = (t13 < t14);
                if (!t15) goto lb12;
                {
                    uint64_t t18 = gm_l8;
                    uint64_t t19 = (uintptr_t)t18;
                    uint64_t t20 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                    gm_tf2orqcrphmek t23 = GM_LD(gm_tf2orqcrphmek, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l7)) + t19 * 56));
                    F.l9 = t23;
                    {
                        gm_l10 = ((uint8_t)0);
                    }
                    {
                        gm_l11 = ((uint8_t)0);
                    }
                    {
                        void * t26 = GM_LD(void *, (((uint8_t *)&F.l9) + 24));
                        if (!(GM_LD(void *, ((uint8_t *)&t26)) != 0)) goto ie25;
                        void * t27 = GM_LD(void *, ((uint8_t *)&t26));
                        gm_l12 = t27;
                        {
                            void * t30 = gm_l12;
                            uint8_t t31 = GM_LD(uint8_t, ((((uint8_t *)t30) + 8) + 0));
                            if (t31 != ((uint8_t)6ULL)) goto ie29;
                            gm_t08olq97rscn3 t32 = GM_LD(gm_t08olq97rscn3, ((((uint8_t *)t30) + 8) + 8));
                            gm_l13 = t32;
                            {
                                uint8_t t34 = (((GM_LD(uintptr_t, (((uint8_t *)&gm_l13) + 0) + 8) == 4) && memcmp(GM_LD(void *, (((uint8_t *)&gm_l13) + 0)), gm_s2737i1iq57nf, 4) == 0));
                                gm_l10 = t34;
                            }
                            goto ix29;
ie29:;
ix29:;
                        }
                        {
                            void * t35 = gm_l12;
                            uint8_t t36 = gm_f_is_effects_kind_uutbf9u(t35);
                            gm_l11 = t36;
                        }
                        goto ix25;
ie25:;
ix25:;
                    }
                    {
                        uint8_t t38 = gm_l10;
                        uint8_t t39 = (uint8_t)!t38;
                        if (!t39) goto ie37;
                        uint8_t t40 = gm_l11;
                        uint8_t t41 = (uint8_t)!t40;
                        if (!t41) goto ie37;
                        {
                            void * t43 = gm_l0;
                            gm_tl874t2atnlh3 t44 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l9) + 40));
                            gm_b16_8 t45;
                            GM_ST(void *, (uint8_t *)&t45, (void *)(gm_sp929adkqo1cd));
                            GM_ST(uintptr_t, (uint8_t *)&t45 + 8, (uintptr_t)(92));
                            gm_b0_1 t46 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t43, t44, t45);
                            if (gm_unw.kind) {
                                goto gm_ll16;
                            }
                        }
                        goto ix37;
ie37:;
ix37:;
                    }
                    {
                        {
                            gm_b16_8 t48 = GM_LD(gm_b16_8, (((uint8_t *)&F.l9) + 0));
                            gm_l14 = t48;
                        }
                        {
                            gm_l15 = ((uint64_t)0ULL);
                        }
                        {
                            for (;;) {
                                uint64_t t50 = gm_l15;
                                uint64_t t51 = GM_LD(uintptr_t, ((uint8_t *)&gm_l14) + 8);
                                uint8_t t52 = (t50 < t51);
                                if (!t52) goto lb49;
                                {
                                    uint64_t t55 = gm_l15;
                                    uint64_t t56 = (uintptr_t)t55;
                                    uint64_t t57 = GM_LD(uintptr_t, ((uint8_t *)&gm_l14) + 8);
                                    gm_t08olq97rscn3 t60 = GM_LD(gm_t08olq97rscn3, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l14)) + t56 * 32));
                                    gm_l16 = t60;
                                    {
                                        void * t62 = gm_l0;
                                        gm_b16_8 t63 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l16) + 0));
                                        gm_tnnjeerolos0a t64;
                                        uint8_t t66 = gm_l11;
                                        if (!t66) goto ie65;
                                        gm_tnnjeerolos0a t68;
                                        GM_ST(uint8_t, (uint8_t *)&t68 + 1, 0);
                                        GM_ST(uint16_t, (uint8_t *)&t68 + 2, 0);
                                        GM_ST(uint32_t, (uint8_t *)&t68 + 4, 0);
                                        GM_ST(uint8_t, ((uint8_t *)&t68 + 0), ((uint8_t)1ULL));
                                        gm_tnnjeerolos0a t69 = t68;
                                        t64 = t69;
                                        goto ix65;
ie65:;
                                        gm_tnnjeerolos0a t71;
                                        GM_ST(uint8_t, (uint8_t *)&t71 + 1, 0);
                                        GM_ST(uint16_t, (uint8_t *)&t71 + 2, 0);
                                        GM_ST(uint32_t, (uint8_t *)&t71 + 4, 0);
                                        GM_ST(uint8_t, ((uint8_t *)&t71 + 0), ((uint8_t)0ULL));
                                        gm_tnnjeerolos0a t72 = t71;
                                        t64 = t72;
ix65:;
                                        uint32_t t73 = gm_f_new_param_7010dt4((void *)NULL, cx, NULL, t62, t63, t64);
                                        if (gm_unw.kind) {
                                            goto gm_ll53;
                                        }
                                        gm_l17 = t73;
                                    }
                                    {
                                        void * t74 = (void *)((uint8_t *)&F.l6);
                                        uint32_t t75 = gm_l17;
                                        gm_tftb1odc739t5 t76 = gm_f_push_im9d4ro(t74, t75);
                                        if (((((uint8_t *)&t76))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t76) + 1, gm_srcmhupam4se3h5, 419, 13);
                                        gm_b0_1 t77 = GM_LD(gm_b0_1, (((uint8_t *)&t76) + 1));
                                    }
                                    {
                                        void * t78 = gm_l0;
                                        void * t79 = (void *)(((uint8_t *)t78) + 1488);
                                        gm_tep2drsl9408b t80;
                                        memset((uint8_t *)&t80 + 20, 0, 4);
                                        gm_b16_8 t81 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l16) + 0));
                                        GM_ST(gm_b16_8, (uint8_t *)&t80 + 0, t81);
                                        void * t82 = gm_l0;
                                        void * t83 = (void *)(((uint8_t *)t82) + 8);
                                        uint32_t t84 = gm_l17;
                                        gm_t88rcucg1b7uk t85 = gm_f_param_n8kgsdm((void *)NULL, cx, NULL, t83, t84);
                                        if (gm_unw.kind) {
                                            goto gm_ll53;
                                        }
                                        GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t80 + 16, t85);
                                        gm_tftb1odc739t5 t86 = gm_f_push_hv483nm(t79, &t80);
                                        if (((((uint8_t *)&t86))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t86) + 1, gm_srcmhupam4se3h5, 420, 13);
                                        gm_b0_1 t87 = GM_LD(gm_b0_1, (((uint8_t *)&t86) + 1));
                                    }
                                    goto me58;
mn59:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcmhupam4se3h5, 417, 9);
me58:;
                                }
lc49:;
                                {
                                    uint64_t t89 = gm_l15;
                                    uint64_t r90; bool o91 = __builtin_add_overflow(t89, ((uint64_t)1ULL), &r90);
                                    uint64_t t92 = r90;
                                    gm_l15 = t92;
                                }
                            }
gm_ll53:;
                            if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb49; }
                                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc49; }
                            }
                            goto gm_ll16;
lb49:;
                        }
                    }
                    goto me21;
mn22:;
                    gm_trap("no match arm matched the scrutinee", gm_srcmhupam4se3h5, 407, 5);
me21:;
                }
lc12:;
                {
                    uint64_t t94 = gm_l8;
                    uint64_t r95; bool o96 = __builtin_add_overflow(t94, ((uint64_t)1ULL), &r95);
                    uint64_t t97 = r95;
                    gm_l8 = t97;
                }
            }
gm_ll16:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb12; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc12; }
            }
            goto gm_lad;
lb12:;
        }
    }
    {
        void * t98 = gm_l0;
        void * t99 = (void *)(((uint8_t *)t98) + 1048);
        gm_tammvh1iespi3 t100;
        gm_b16_8 t101 = GM_LD(gm_b16_8, (((uint8_t *)a3) + 0));
        GM_ST(gm_b16_8, (uint8_t *)&t100 + 0, t101);
        uint64_t t102 = gm_l1;
        GM_ST(uint64_t, (uint8_t *)&t100 + 16, t102);
        gm_b16_8 t103 = GM_LD(gm_b16_8, (((uint8_t *)&F.l6) + 0));
        GM_ST(gm_b16_8, (uint8_t *)&t100 + 24, t103);
        gm_b16_8 t104 = gm_f_empty_slice_p6mn4us();
        GM_ST(gm_b16_8, (uint8_t *)&t100 + 40, t104);
        gm_tftb1odc739t5 t105 = gm_f_push_puil3ql(t99, &t100);
        if (((((uint8_t *)&t105))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t105) + 1, gm_srcmhupam4se3h5, 424, 5);
        gm_b0_1 t106 = GM_LD(gm_b0_1, (((uint8_t *)&t105) + 1));
    }
    {
        void * t107 = gm_l0;
        uint64_t t108 = gm_l2;
        uint32_t t109 = gm_l5;
        gm_t6qooom8qa2do t110;
        memset(&t110, 0, sizeof t110);
        GM_ST(uint8_t, ((uint8_t *)&t110 + 0), ((uint8_t)2ULL));
        GM_ST(uint32_t, ((uint8_t *)&t110 + 8), t109);
        gm_tagrkbqh054h7 t111;
        GM_ST(uint8_t, (uint8_t *)&t111 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t111 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t111 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t111 + 0), ((uint8_t)2ULL));
        GM_ST(gm_t6qooom8qa2do, ((uint8_t *)&t111 + 8), t110);
        gm_b0_1 t112 = gm_f_set_state_3j300fi((void *)NULL, cx, NULL, t107, t108, &t111);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_f_list_a7rbkus((void *)&F.l18, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            void * t114 = gm_l4;
            gm_b16_8 t115 = GM_LD(gm_b16_8, (((uint8_t *)t114) + 24));
            gm_l19 = t115;
        }
        {
            gm_l20 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t117 = gm_l20;
                uint64_t t118 = GM_LD(uintptr_t, ((uint8_t *)&gm_l19) + 8);
                uint8_t t119 = (t117 < t118);
                if (!t119) goto lb116;
                {
                    uint64_t t122 = gm_l20;
                    uint64_t t123 = (uintptr_t)t122;
                    uint64_t t124 = GM_LD(uintptr_t, ((uint8_t *)&gm_l19) + 8);
                    gm_tc0sbs1tp6d7f t127 = GM_LD(gm_tc0sbs1tp6d7f, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l19)) + t123 * 80));
                    F.l21 = t127;
                    {
                        gm_f_list_af74s0q((void *)&F.l22, (void *)NULL, cx, NULL);
                        if (gm_unw.kind) {
                            goto gm_ll120;
                        }
                    }
                    {
                        {
                            gm_b16_8 t130 = GM_LD(gm_b16_8, (((uint8_t *)&F.l21) + 40));
                            gm_l23 = t130;
                        }
                        {
                            gm_l24 = ((uint64_t)0ULL);
                        }
                        {
                            for (;;) {
                                uint64_t t132 = gm_l24;
                                uint64_t t133 = GM_LD(uintptr_t, ((uint8_t *)&gm_l23) + 8);
                                uint8_t t134 = (t132 < t133);
                                if (!t134) goto lb131;
                                {
                                    uint64_t t137 = gm_l24;
                                    uint64_t t138 = (uintptr_t)t137;
                                    uint64_t t139 = GM_LD(uintptr_t, ((uint8_t *)&gm_l23) + 8);
                                    gm_tf2orqcrphmek t142 = GM_LD(gm_tf2orqcrphmek, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l23)) + t138 * 56));
                                    F.l25 = t142;
                                    {
                                        void * t144 = gm_l0;
                                        void * t145 = (void *)(((uint8_t *)t144) + 8);
                                        gm_t88rcucg1b7uk t146 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t145);
                                        if (gm_unw.kind) {
                                            goto gm_ll135;
                                        }
                                        gm_l26 = t146;
                                    }
                                    {
                                        void * t148 = GM_LD(void *, (((uint8_t *)&F.l25) + 24));
                                        if (!(GM_LD(void *, ((uint8_t *)&t148)) != 0)) goto ie147;
                                        void * t149 = GM_LD(void *, ((uint8_t *)&t148));
                                        gm_l27 = t149;
                                        {
                                            void * t151 = gm_l0;
                                            uint64_t t152 = gm_l1;
                                            void * t153 = gm_l27;
                                            gm_t88rcucg1b7uk t154 = gm_f_resolve_type_jjodbei((void *)NULL, cx, NULL, t151, t152, t153);
                                            if (gm_unw.kind) {
                                                goto gm_ll135;
                                            }
                                            gm_l26 = t154;
                                        }
                                        goto ix147;
ie147:;
                                        {
                                            void * t156 = gm_l0;
                                            gm_tl874t2atnlh3 t157 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l25) + 40));
                                            gm_b16_8 t158;
                                            GM_ST(void *, (uint8_t *)&t158, (void *)(gm_s8p6h6db1ftub));
                                            GM_ST(uintptr_t, (uint8_t *)&t158 + 8, (uintptr_t)(31));
                                            gm_b0_1 t159 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t156, t157, t158);
                                            if (gm_unw.kind) {
                                                goto gm_ll135;
                                            }
                                        }
ix147:;
                                    }
                                    {
                                        {
                                            gm_b16_8 t161 = GM_LD(gm_b16_8, (((uint8_t *)&F.l25) + 0));
                                            gm_l28 = t161;
                                        }
                                        {
                                            gm_l29 = ((uint64_t)0ULL);
                                        }
                                        {
                                            for (;;) {
                                                uint64_t t163 = gm_l29;
                                                uint64_t t164 = GM_LD(uintptr_t, ((uint8_t *)&gm_l28) + 8);
                                                uint8_t t165 = (t163 < t164);
                                                if (!t165) goto lb162;
                                                {
                                                    uint64_t t168 = gm_l29;
                                                    uint64_t t169 = (uintptr_t)t168;
                                                    uint64_t t170 = GM_LD(uintptr_t, ((uint8_t *)&gm_l28) + 8);
                                                    gm_t08olq97rscn3 t173 = GM_LD(gm_t08olq97rscn3, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l28)) + t169 * 32));
                                                    gm_l30 = t173;
                                                    {
                                                        void * t175 = (void *)((uint8_t *)&F.l22);
                                                        gm_t5v0c8ia6a5mo t176;
                                                        memset((uint8_t *)&t176 + 49, 0, 7);
                                                        gm_b16_8 t177 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l30) + 0));
                                                        GM_ST(gm_b16_8, (uint8_t *)&t176 + 0, t177);
                                                        gm_t88rcucg1b7uk t178 = gm_l26;
                                                        GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t176 + 16, t178);
                                                        gm_tl874t2atnlh3 t179 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l30) + 16));
                                                        GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t176 + 20, t179);
                                                        GM_ST(void *, (uint8_t *)&t176 + 32, ((void *)0));
                                                        gm_t9805qtonbsag t180;
                                                        memset(&t180, 0, sizeof t180);
                                                        GM_ST(gm_t9805qtonbsag, (uint8_t *)&t176 + 40, t180);
                                                        GM_ST(uint8_t, (uint8_t *)&t176 + 48, ((uint8_t)0));
                                                        gm_tftb1odc739t5 t181 = gm_f_push_o8u01sh(t175, &t176);
                                                        if (((((uint8_t *)&t181))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t181) + 1, gm_srcmhupam4se3h5, 437, 17);
                                                        gm_b0_1 t182 = GM_LD(gm_b0_1, (((uint8_t *)&t181) + 1));
                                                    }
                                                    goto me171;
mn172:;
                                                    gm_trap("no match arm matched the scrutinee", gm_srcmhupam4se3h5, 436, 13);
me171:;
                                                }
lc162:;
                                                {
                                                    uint64_t t184 = gm_l29;
                                                    uint64_t r185; bool o186 = __builtin_add_overflow(t184, ((uint64_t)1ULL), &r185);
                                                    uint64_t t187 = r185;
                                                    gm_l29 = t187;
                                                }
                                            }
lb162:;
                                        }
                                    }
                                    goto me140;
mn141:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcmhupam4se3h5, 429, 9);
me140:;
                                }
lc131:;
                                {
                                    uint64_t t189 = gm_l24;
                                    uint64_t r190; bool o191 = __builtin_add_overflow(t189, ((uint64_t)1ULL), &r190);
                                    uint64_t t192 = r190;
                                    gm_l24 = t192;
                                }
                            }
gm_ll135:;
                            if (gm_unw.frame == &F.h && gm_unw.label == 3) {
                                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb131; }
                                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc131; }
                            }
                            goto gm_ll120;
lb131:;
                        }
                    }
                    {
                        void * t193 = gm_l0;
                        void * t194 = (void *)(((uint8_t *)t193) + 8);
                        gm_t88rcucg1b7uk t195 = gm_f_unit_2age34u((void *)NULL, cx, NULL, t194);
                        if (gm_unw.kind) {
                            goto gm_ll120;
                        }
                        gm_l31 = t195;
                    }
                    {
                        void * t197 = GM_LD(void *, (((uint8_t *)&F.l21) + 56));
                        if (!(GM_LD(void *, ((uint8_t *)&t197)) != 0)) goto ie196;
                        void * t198 = GM_LD(void *, ((uint8_t *)&t197));
                        gm_l32 = t198;
                        {
                            void * t200 = gm_l0;
                            uint64_t t201 = gm_l1;
                            void * t202 = gm_l32;
                            gm_t88rcucg1b7uk t203 = gm_f_resolve_type_jjodbei((void *)NULL, cx, NULL, t200, t201, t202);
                            if (gm_unw.kind) {
                                goto gm_ll120;
                            }
                            gm_l31 = t203;
                        }
                        goto ix196;
ie196:;
ix196:;
                    }
                    {
                        uint64_t t205 = GM_LD(uintptr_t, (((uint8_t *)&F.l22) + 0) + 8);
                        uint8_t t206 = (t205 > ((uint64_t)0ULL));
                        if (!t206) goto ie204;
                        {
                            void * t208 = gm_l0;
                            uint64_t t209 = gm_l1;
                            uint64_t t210 = (uintptr_t)((uint64_t)0ULL);
                            uint64_t t211 = GM_LD(uintptr_t, (((uint8_t *)&F.l22) + 0) + 8);
                            if (t210 >= (uintptr_t)(t211)) gm_trap_index(t210, t211, gm_srcmhupam4se3h5, 442, 82);
                            gm_t88rcucg1b7uk t212 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l22) + 0)) + t210 * 56) + 16));
                            gm_b0_1 t213 = gm_f_declare_method_candidate_e20bo1i((void *)NULL, cx, NULL, t208, t209, &(*(gm_t08olq97rscn3 *)(((uint8_t *)&F.l21) + 0)), t212);
                            if (gm_unw.kind) {
                                goto gm_ll120;
                            }
                        }
                        goto ix204;
ie204:;
ix204:;
                    }
                    {
                        void * t214 = (void *)((uint8_t *)&F.l18);
                        gm_tv0k2vg3054a6 t215;
                        memset((uint8_t *)&t215 + 17, 0, 7);
                        memset((uint8_t *)&t215 + 44, 0, 4);
                        gm_b16_8 t216 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l21) + 0) + 0));
                        GM_ST(gm_b16_8, (uint8_t *)&t215 + 0, t216);
                        gm_t9qkh0rsooc8d t217 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&F.l21) + 32));
                        GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&t215 + 16, t217);
                        gm_b16_8 t218 = GM_LD(gm_b16_8, (((uint8_t *)&F.l22) + 0));
                        GM_ST(gm_b16_8, (uint8_t *)&t215 + 24, t218);
                        gm_t88rcucg1b7uk t219 = gm_l31;
                        GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t215 + 40, t219);
                        gm_tftb1odc739t5 t220 = gm_f_push_700doiq(t214, &t215);
                        if (((((uint8_t *)&t220))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t220) + 1, gm_srcmhupam4se3h5, 443, 9);
                        gm_b0_1 t221 = GM_LD(gm_b0_1, (((uint8_t *)&t220) + 1));
                    }
                    goto me125;
mn126:;
                    gm_trap("no match arm matched the scrutinee", gm_srcmhupam4se3h5, 427, 5);
me125:;
                }
lc116:;
                {
                    uint64_t t223 = gm_l20;
                    uint64_t r224; bool o225 = __builtin_add_overflow(t223, ((uint64_t)1ULL), &r224);
                    uint64_t t226 = r224;
                    gm_l20 = t226;
                }
            }
gm_ll120:;
            if (gm_unw.frame == &F.h && gm_unw.label == 2) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb116; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc116; }
            }
            goto gm_lad;
lb116:;
        }
    }
    {
        void * t227 = gm_l0;
        void * t228 = (void *)(((uint8_t *)t227) + 1488);
        void * t229 = gm_l0;
        uint64_t t230 = GM_LD(uintptr_t, ((((uint8_t *)t229) + 1488) + 0) + 8);
        uint64_t t231 = GM_LD(uintptr_t, (((uint8_t *)&F.l6) + 0) + 8);
        uint64_t r232; bool o233 = __builtin_sub_overflow(t230, t231, &r232);
        if (o233) gm_trap("integer overflow in `-`", gm_srcmhupam4se3h5, 445, 28);
        uint64_t t234 = r232;
        gm_f_truncate_7r782uc(t228, t234);
    }
    {
        uint32_t t235 = gm_l5;
        uint64_t t236 = (uint64_t)t235;
        uint64_t t237 = (uintptr_t)t236;
        void * t238 = gm_l0;
        uint64_t t239 = GM_LD(uintptr_t, ((((uint8_t *)t238) + 1048) + 0) + 8);
        if (t237 >= (uintptr_t)(t239)) gm_trap_index(t237, t239, gm_srcmhupam4se3h5, 446, 5);
        gm_b16_8 t240 = GM_LD(gm_b16_8, (((uint8_t *)&F.l18) + 0));
        GM_ST(gm_b16_8, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t238) + 1048) + 0)) + t237 * 56) + 40), t240);
    }
    {
        uint32_t t241 = gm_l5;
        memset(&(*(gm_t6qooom8qa2do *)gm_out), 0, sizeof (*(gm_t6qooom8qa2do *)gm_out));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t6qooom8qa2do *)gm_out) + 0, ((uint8_t)2ULL));
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t6qooom8qa2do *)gm_out) + 8, t241);
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
inline __attribute__((aligned(4))) void gm_f_declare_fn_6piolnr(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, const void * a2, void * a3) {
    struct gm_fr_declare_fn_6piolnr F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* module */
    gm_t08olq97rscn3 gm_l2; /* name */
    void * gm_l3; /* lit */
    gm_b16_8 gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    uint32_t gm_l8; /* id */
    GM_STACK_CHECK(&F, 985u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l3 = a3;
    {
        void * t2 = gm_l0;
        uint64_t t3 = gm_l1;
        void * t4 = gm_l3;
        void * t5 = (void *)(((uint8_t *)t4) + 0);
        void * t6 = gm_l3;
        gm_b16_8 t7 = GM_LD(gm_b16_8, (((uint8_t *)t6) + 72));
        gm_b16_8 t8 = GM_LD(gm_b16_8, ((uint8_t *)&t7));
        gm_tg93b272d11vo t9;
        gm_f_resolve_fn_sig_pamuhve((void *)&t9, (void *)NULL, cx, NULL, t2, t3, t5, ((uint8_t)1), t8);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tqqhoivahirr0 t10;
        if (((((uint8_t *)&t9))[136] != 0)) {
            t10 = (*(gm_tqqhoivahirr0 *)(((uint8_t *)&t9) + 0));
        } else {
            memset(&(*(gm_t6qooom8qa2do *)gm_out), 0, sizeof (*(gm_t6qooom8qa2do *)gm_out));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t6qooom8qa2do *)gm_out) + 0, ((uint8_t)11ULL));
            goto gm_fdone;
        }
        F.l4 = t10;
    }
    {
        {
            gm_b16_8 t12 = GM_LD(gm_b16_8, (((uint8_t *)&F.l4) + 40));
            gm_l5 = t12;
        }
        {
            gm_l6 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t14 = gm_l6;
                uint64_t t15 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                uint8_t t16 = (t14 < t15);
                if (!t16) goto lb13;
                {
                    uint64_t t19 = gm_l6;
                    uint64_t t20 = (uintptr_t)t19;
                    uint64_t t21 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                    gm_t5v0c8ia6a5mo t24 = GM_LD(gm_t5v0c8ia6a5mo, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l5)) + t20 * 56));
                    F.l7 = t24;
                    {
                        uint8_t t27;
                        if ((((((uint8_t *)&F.l7) + 40))[4] != 0)) goto mn29;
                        t27 = ((uint8_t)1);
                        goto me28;
mn29:;
                        t27 = ((uint8_t)0);
                        goto me28;
mn30:;
                        gm_trap("no match arm matched the scrutinee", gm_srcmhupam4se3h5, 7, 12);
me28:;
                        if (!t27) goto ie26;
                        {
                            void * t32 = gm_l0;
                            uint64_t t33 = gm_l1;
                            gm_t88rcucg1b7uk t34 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l7) + 16));
                            gm_b0_1 t35 = gm_f_declare_method_candidate_e20bo1i((void *)NULL, cx, NULL, t32, t33, &(*(gm_t08olq97rscn3 *)((uint8_t *)a2)), t34);
                            if (gm_unw.kind) {
                                goto gm_ll17;
                            }
                        }
                        {
                            goto lb13;
                        }
                        goto ix26;
ie26:;
ix26:;
                    }
                    goto me22;
mn23:;
                    gm_trap("no match arm matched the scrutinee", gm_srcmhupam4se3h5, 6, 5);
me22:;
                }
lc13:;
                {
                    uint64_t t37 = gm_l6;
                    uint64_t r38; bool o39 = __builtin_add_overflow(t37, ((uint64_t)1ULL), &r38);
                    uint64_t t40 = r38;
                    gm_l6 = t40;
                }
            }
gm_ll17:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb13; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc13; }
            }
            goto gm_lad;
lb13:;
        }
    }
    {
        void * t41 = gm_l0;
        gm_tqqhoivahirr0 t42;
        memset((uint8_t *)&t42 + 28, 0, 4);
        memset((uint8_t *)&t42 + 60, 0, 4);
        memset((uint8_t *)&t42 + 129, 0, 7);
        gm_b16_8 t43 = GM_LD(gm_b16_8, (((uint8_t *)a2) + 0));
        GM_ST(gm_b16_8, (uint8_t *)&t42 + 0, t43);
        gm_tl874t2atnlh3 t44 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)a2) + 16));
        GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t42 + 16, t44);
        uint64_t t45 = gm_l1;
        GM_ST(uint64_t, (uint8_t *)&t42 + 32, t45);
        gm_b16_8 t46 = GM_LD(gm_b16_8, (((uint8_t *)&F.l4) + 40));
        GM_ST(gm_b16_8, (uint8_t *)&t42 + 40, t46);
        gm_t88rcucg1b7uk t47 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l4) + 56));
        GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t42 + 56, t47);
        gm_tnpifv20irnph t48 = GM_LD(gm_tnpifv20irnph, (((uint8_t *)&F.l4) + 64));
        GM_ST(gm_tnpifv20irnph, (uint8_t *)&t42 + 64, t48);
        gm_b16_8 t49 = GM_LD(gm_b16_8, (((uint8_t *)&F.l4) + 80));
        GM_ST(gm_b16_8, (uint8_t *)&t42 + 80, t49);
        gm_tqu3m2l3n8mnc t50;
        memset(&t50, 0, sizeof t50);
        GM_ST(gm_tqu3m2l3n8mnc, (uint8_t *)&t42 + 96, t50);
        GM_ST(uint8_t, (uint8_t *)&t42 + 128, ((uint8_t)0));
        gm_t9qkh0rsooc8d t51;
        GM_ST(uint8_t, ((uint8_t *)&t51 + 0), ((uint8_t)0ULL));
        gm_tdgfduqjheqdm t52;
        gm_f_bodiless_def_u9kqo09((void *)&t52, t51);
        uint32_t t53 = gm_f_add_fn_t4t83qh((void *)NULL, cx, NULL, t41, &t42, &t52);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l8 = t53;
    }
    {
        void * t54 = gm_l0;
        void * t55 = (void *)(((uint8_t *)t54) + 424);
        gm_t5r2drn9pjqkt t56;
        memset((uint8_t *)&t56 + 4, 0, 4);
        uint32_t t57 = gm_l8;
        GM_ST(uint32_t, (uint8_t *)&t56 + 0, t57);
        uint64_t t58 = gm_l1;
        GM_ST(uint64_t, (uint8_t *)&t56 + 8, t58);
        void * t59 = gm_l3;
        GM_ST(void *, (uint8_t *)&t56 + 16, t59);
        gm_tftb1odc739t5 t60 = gm_f_push_o022ckl(t55, &t56);
        if (((((uint8_t *)&t60))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t60) + 1, gm_srcmhupam4se3h5, 13, 5);
        gm_b0_1 t61 = GM_LD(gm_b0_1, (((uint8_t *)&t60) + 1));
    }
    {
        uint32_t t62 = gm_l8;
        memset(&(*(gm_t6qooom8qa2do *)gm_out), 0, sizeof (*(gm_t6qooom8qa2do *)gm_out));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t6qooom8qa2do *)gm_out) + 0, ((uint8_t)5ULL));
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t6qooom8qa2do *)gm_out) + 8, t62);
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
inline __attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_effect_param_named_h2jeoo7(void * a0, void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_effect_param_named_h2jeoo7 F;
    void * gm_l0; /* s */
    void * gm_l1; /* e */
    gm_t08olq97rscn3 gm_l2; /* id */
    gm_t88rcucg1b7uk gm_l3; /* t */
    uint32_t gm_l4; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 320u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t3 = gm_l1;
        uint8_t t4 = GM_LD(uint8_t, ((((uint8_t *)t3) + 8) + 0));
        if (t4 != ((uint8_t)6ULL)) goto ie2;
        gm_t08olq97rscn3 t5 = GM_LD(gm_t08olq97rscn3, ((((uint8_t *)t3) + 8) + 8));
        gm_l2 = t5;
        {
            void * t7 = gm_l0;
            gm_b16_8 t8 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l2) + 0));
            gm_tkn42u7na3t81 t9 = gm_f_type_param_3av4r8i(t7, t8);
            gm_t88rcucg1b7uk t10;
            if (((((uint8_t *)&t9))[4] != 0)) {
                gm_t88rcucg1b7uk t11 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t9) + 0));
                t10 = t11;
            } else {
                gm_tkn42u7na3t81 t12;
                memset(&t12, 0, sizeof t12);
                uint8_t t13 = 0;
                F.ret = t12;
                goto gm_fret;
            }
            gm_l3 = t10;
        }
        {
            void * t15 = gm_l0;
            void * t16 = (void *)(((uint8_t *)t15) + 8);
            gm_t88rcucg1b7uk t17 = gm_l3;
            gm_t53at80pfdus3 t18;
            gm_f_kind_jcnt4hm((void *)&t18, t16, GM_CONV(gm_csguankko4lmhd, t17));
            uint8_t t19 = GM_LD(uint8_t, (((uint8_t *)&t18) + 0));
            if (t19 != ((uint8_t)19ULL)) goto ie14;
            uint32_t t20 = GM_LD(uint32_t, (((uint8_t *)&t18) + 8));
            gm_l4 = t20;
            {
                uint32_t t23 = gm_l4;
                uint64_t t24 = (uint64_t)t23;
                uint64_t t25 = (uintptr_t)t24;
                void * t26 = gm_l0;
                uint64_t t27 = GM_LD(uintptr_t, ((((uint8_t *)t26) + 1128) + 0) + 8);
                if (t25 >= (uintptr_t)(t27)) gm_trap_index(t25, t27, gm_srcmhupam4se3h5, 386, 8);
                uint8_t t28 = (!(GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t26) + 1128) + 0)) + t25 * 48) + 0) + 0) == ((uint8_t)1ULL)));
                if (!t28) goto ie22;
                {
                    gm_tkn42u7na3t81 t30;
                    memset(&t30, 0, sizeof t30);
                    uint8_t t31 = 0;
                    F.ret = t30;
                    goto gm_fret;
                }
                goto ix22;
ie22:;
ix22:;
            }
            {
                gm_t88rcucg1b7uk t32 = gm_l3;
                gm_tkn42u7na3t81 t33;
                GM_ST(uint8_t, (uint8_t *)&t33 + 5, 0);
                GM_ST(uint16_t, (uint8_t *)&t33 + 6, 0);
                GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t33), t32);
                ((uint8_t *)&t33)[4] = 1;
                uint8_t t34 = 0;
                F.ret = t33;
                goto gm_fret;
            }
            goto ix14;
ie14:;
            {
                gm_tkn42u7na3t81 t36;
                memset(&t36, 0, sizeof t36);
                uint8_t t37 = 0;
                F.ret = t36;
                goto gm_fret;
            }
ix14:;
        }
        goto ix2;
ie2:;
        {
            gm_tkn42u7na3t81 t39;
            memset(&t39, 0, sizeof t39);
            uint8_t t40 = 0;
            F.ret = t39;
            goto gm_fret;
        }
ix2:;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) gm_b16_8 gm_f_effect_terms_bphj7th(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) uint8_t gm_f_has_splice_80pgqas(gm_tnpifv20irnph a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_has_splice_80pgqas F;
    gm_tnpifv20irnph gm_l0; /* set */
    gm_b16_8 gm_l1; /* <tmp> */
    uint64_t gm_l2; /* <tmp> */
    gm_tiilmfd16v3fc gm_l3; /* e */
    GM_STACK_CHECK_SHALLOW(&F, 170u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        {
            gm_b16_8 t3 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l0) + 0));
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
                    gm_tiilmfd16v3fc t15 = GM_LD(gm_tiilmfd16v3fc, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t11 * 24));
                    gm_l3 = t15;
                    {
                        uint32_t t18 = GM_LD(uint32_t, (((uint8_t *)&gm_l3) + 0));
                        uint8_t t19 = ((t18 == ((uint32_t)0ULL)));
                        if (!t19) goto ie17;
                        {
                            uint8_t t21 = 0;
                            F.ret = ((uint8_t)1);
                            goto gm_fret;
                        }
                        goto ix17;
ie17:;
ix17:;
                    }
                    goto me13;
mn14:;
                    gm_trap("no match arm matched the scrutinee", gm_srcmhupam4se3h5, 227, 5);
me13:;
                }
lc4:;
                {
                    uint64_t t23 = gm_l2;
                    uint64_t r24; bool o25 = __builtin_add_overflow(t23, ((uint64_t)1ULL), &r24);
                    uint64_t t26 = r24;
                    gm_l2 = t26;
                }
            }
lb4:;
        }
    }
    {
        uint8_t t27 = 0;
        F.ret = ((uint8_t)0);
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) uint8_t gm_f_is_effect_sum_dd9le16(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2) {
    struct gm_fr_is_effect_sum_dd9le16 F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* module */
    void * gm_l2; /* e */
    void * gm_l3; /* first */
    void * gm_l4; /* head */
    void * gm_l5; /* callee */
    gm_tf5srh4mp4b3i gm_l6; /* d */
    gm_t08olq97rscn3 gm_l7; /* id */
    void * gm_l8; /* base */
    gm_t08olq97rscn3 gm_l9; /* name */
    uint64_t gm_l10; /* m */
    uint64_t gm_l11; /* decl */
    GM_STACK_CHECK(&F, 733u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        uint64_t t2 = (uintptr_t)((uint64_t)0ULL);
        void * t3 = gm_l2;
        gm_b16_8 t4 = gm_f_effect_terms_bphj7th((void *)NULL, cx, NULL, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        if (t2 >= (uintptr_t)(t5)) gm_trap_index(t2, t5, gm_srcmhupam4se3h5, 261, 14);
        void * t6 = GM_LD(void *, ((uint8_t *)GM_LD(void *, ((uint8_t *)&t4)) + t2 * 8));
        gm_l3 = t6;
    }
    {
        void * t7 = gm_l3;
        gm_l4 = t7;
    }
    {
        void * t9 = gm_l3;
        uint8_t t10 = GM_LD(uint8_t, ((((uint8_t *)t9) + 8) + 0));
        if (t10 != ((uint8_t)19ULL)) goto ie8;
        void * t11 = GM_LD(void *, (((((uint8_t *)t9) + 8) + 8) + 0));
        gm_l5 = t11;
        {
            void * t13 = gm_l5;
            gm_l4 = t13;
        }
        goto ix8;
ie8:;
ix8:;
    }
    {
        gm_tf5srh4mp4b3i t14;
        memset(&t14, 0, sizeof t14);
        gm_l6 = t14;
    }
    {
        void * t15 = gm_l4;
        uint8_t t21 = GM_LD(uint8_t, ((((uint8_t *)t15) + 8) + 0));
        switch (t21) { case ((uint8_t)0ULL): goto ma19; case ((uint8_t)1ULL): goto ma19; case ((uint8_t)2ULL): goto ma19; case ((uint8_t)3ULL): goto ma19; case ((uint8_t)4ULL): goto ma19; case ((uint8_t)5ULL): goto ma19; case ((uint8_t)6ULL): goto ma17; case ((uint8_t)7ULL): goto ma19; case ((uint8_t)8ULL): goto ma19; case ((uint8_t)9ULL): goto ma19; case ((uint8_t)10ULL): goto ma19; case ((uint8_t)11ULL): goto ma19; case ((uint8_t)12ULL): goto ma19; case ((uint8_t)13ULL): goto ma19; case ((uint8_t)14ULL): goto ma19; case ((uint8_t)15ULL): goto ma19; case ((uint8_t)16ULL): goto ma19; case ((uint8_t)17ULL): goto ma19; case ((uint8_t)18ULL): goto ma19; case ((uint8_t)19ULL): goto ma19; case ((uint8_t)20ULL): goto ma19; case ((uint8_t)21ULL): goto ma19; case ((uint8_t)22ULL): goto ma18; case ((uint8_t)23ULL): goto ma19; case ((uint8_t)24ULL): goto ma19; case ((uint8_t)25ULL): goto ma19; case ((uint8_t)26ULL): goto ma19; case ((uint8_t)27ULL): goto ma19; case ((uint8_t)28ULL): goto ma19; case ((uint8_t)29ULL): goto ma19; case ((uint8_t)30ULL): goto ma19; case ((uint8_t)31ULL): goto ma19; case ((uint8_t)32ULL): goto ma19; case ((uint8_t)33ULL): goto ma19; case ((uint8_t)34ULL): goto ma19; case ((uint8_t)35ULL): goto ma19; case ((uint8_t)36ULL): goto ma19; case ((uint8_t)37ULL): goto ma19; case ((uint8_t)38ULL): goto ma19; case ((uint8_t)39ULL): goto ma19; case ((uint8_t)40ULL): goto ma19; case ((uint8_t)41ULL): goto ma19; case ((uint8_t)42ULL): goto ma19; case ((uint8_t)43ULL): goto ma19; case ((uint8_t)44ULL): goto ma19; case ((uint8_t)45ULL): goto ma19; case ((uint8_t)46ULL): goto ma19; case ((uint8_t)47ULL): goto ma19; case ((uint8_t)48ULL): goto ma19; case ((uint8_t)49ULL): goto ma19; case ((uint8_t)50ULL): goto ma19; default: goto ma19; }
ma17:;
        uint8_t t23 = GM_LD(uint8_t, ((((uint8_t *)t15) + 8) + 0));
        if (t23 != ((uint8_t)6ULL)) goto mn22;
        gm_t08olq97rscn3 t24 = GM_LD(gm_t08olq97rscn3, ((((uint8_t *)t15) + 8) + 8));
        gm_l7 = t24;
        {
            void * t26 = gm_l0;
            uint64_t t27 = gm_l1;
            gm_b16_8 t28 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l7) + 0));
            gm_tf5srh4mp4b3i t29 = gm_f_lookup_module_name_cukfp7t((void *)NULL, cx, NULL, t26, t27, t28);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l6 = t29;
        }
        goto me16;
mn22:;
ma18:;
        uint8_t t31 = GM_LD(uint8_t, ((((uint8_t *)t15) + 8) + 0));
        if (t31 != ((uint8_t)22ULL)) goto mn30;
        void * t32 = GM_LD(void *, (((((uint8_t *)t15) + 8) + 8) + 0));
        gm_l8 = t32;
        gm_t08olq97rscn3 t33 = GM_LD(gm_t08olq97rscn3, (((((uint8_t *)t15) + 8) + 8) + 8));
        gm_l9 = t33;
        {
            void * t36 = gm_l0;
            uint64_t t37 = gm_l1;
            void * t38 = gm_l8;
            gm_tf5srh4mp4b3i t39 = gm_f_module_named_n1mcl3b((void *)NULL, cx, NULL, t36, t37, t38);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (!((((uint8_t *)&t39))[8] != 0)) goto ie35;
            uint64_t t40 = GM_LD(uint64_t, ((uint8_t *)&t39));
            gm_l10 = t40;
            {
                void * t42 = gm_l0;
                uint64_t t43 = gm_l10;
                uint64_t t44 = gm_l1;
                gm_tf5srh4mp4b3i t45 = gm_f_module_member_5fd2qm9((void *)NULL, cx, NULL, t42, t43, &(*(gm_t08olq97rscn3 *)((uint8_t *)&gm_l9)), t44);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l6 = t45;
            }
            goto ix35;
ie35:;
ix35:;
        }
        goto me16;
mn30:;
ma19:;
        goto me16;
mn46:;
ma20:;
        gm_trap("no match arm matched the scrutinee", gm_srcmhupam4se3h5, 265, 5);
me16:;
    }
    {
        gm_tf5srh4mp4b3i t48 = gm_l6;
        uint64_t t49;
        if (((((uint8_t *)&t48))[8] != 0)) {
            uint64_t t50 = GM_LD(uint64_t, (((uint8_t *)&t48) + 0));
            t49 = t50;
        } else {
            uint8_t t51 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        gm_l11 = t49;
    }
    {
        void * t52 = gm_l0;
        uint64_t t53 = gm_l11;
        gm_t6qooom8qa2do t54;
        gm_f_resolve_decl_pus042v((void *)&t54, (void *)NULL, cx, NULL, t52, t53);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t55;
        uint8_t t62 = GM_LD(uint8_t, (((uint8_t *)&t54) + 0));
        switch (t62) { case ((uint8_t)0ULL): goto ma60; case ((uint8_t)1ULL): goto ma60; case ((uint8_t)2ULL): goto ma57; case ((uint8_t)3ULL): goto ma58; case ((uint8_t)4ULL): goto ma59; case ((uint8_t)5ULL): goto ma60; case ((uint8_t)6ULL): goto ma60; case ((uint8_t)7ULL): goto ma60; case ((uint8_t)8ULL): goto ma60; case ((uint8_t)9ULL): goto ma60; case ((uint8_t)10ULL): goto ma60; case ((uint8_t)11ULL): goto ma60; default: goto ma60; }
ma57:;
        uint8_t t64 = GM_LD(uint8_t, (((uint8_t *)&t54) + 0));
        if (t64 != ((uint8_t)2ULL)) goto mn63;
        t55 = ((uint8_t)1);
        goto me56;
mn63:;
ma58:;
        uint8_t t66 = GM_LD(uint8_t, (((uint8_t *)&t54) + 0));
        if (t66 != ((uint8_t)3ULL)) goto mn65;
        t55 = ((uint8_t)1);
        goto me56;
mn65:;
ma59:;
        uint8_t t68 = GM_LD(uint8_t, (((uint8_t *)&t54) + 0));
        if (t68 != ((uint8_t)4ULL)) goto mn67;
        t55 = ((uint8_t)1);
        goto me56;
mn67:;
ma60:;
        t55 = ((uint8_t)0);
        goto me56;
mn69:;
ma61:;
        gm_trap("no match arm matched the scrutinee", gm_srcmhupam4se3h5, 273, 12);
me56:;
        uint8_t t70 = 0;
        F.ret = t55;
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
extern __attribute__((aligned(4))) uint8_t gm_f_is_effects_kind_uutbf9u(void * a0);
extern __attribute__((aligned(4))) uint8_t gm_f_is_site_8sr7vam(void * a0);
inline __attribute__((aligned(4))) uint8_t gm_f_may_coincide_f6v7r98(void * a0, void * a1, void * a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_may_coincide_f6v7r98 F;
    void * gm_l0; /* s */
    void * gm_l1; /* a */
    void * gm_l2; /* b */
    uint64_t gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    uint64_t gm_l5; /* i */
    gm_t88rcucg1b7uk gm_l6; /* x */
    gm_t88rcucg1b7uk gm_l7; /* y */
    GM_STACK_CHECK(&F, 288u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        uint8_t t3 = 0;
        void * t4 = gm_l1;
        uint32_t t5 = GM_LD(uint32_t, (((uint8_t *)t4) + 0));
        void * t6 = gm_l2;
        uint32_t t7 = GM_LD(uint32_t, (((uint8_t *)t6) + 0));
        uint8_t t8 = (!(t5 == t7));
        if (!t8) {
            void * t9 = gm_l1;
            void * t10 = gm_l2;
            int32_t t11 = gm_f_cmp_ref_8ntrqll(t9, t10);
            uint8_t t12 = ((t11 == ((int32_t)0LL)));
            t3 = t12;
        } else t3 = 1;
        if (!t3) goto ie2;
        {
            uint8_t t14 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        {
            gm_l4 = ((uint64_t)0ULL);
        }
        {
            void * t16 = gm_l1;
            uint64_t t17 = GM_LD(uintptr_t, (((uint8_t *)t16) + 8) + 8);
            void * t18 = gm_l2;
            uint64_t t19 = GM_LD(uintptr_t, (((uint8_t *)t18) + 8) + 8);
            uint64_t t20 = (t17 < t19 ? t17 : t19);
            gm_l3 = t20;
        }
        {
            for (;;) {
                uint64_t t22 = gm_l4;
                uint64_t t23 = gm_l3;
                uint8_t t24 = (t22 < t23);
                if (!t24) goto lb21;
                {
                    uint64_t t27 = gm_l4;
                    uint64_t t30 = GM_LD(uint64_t, ((uint8_t *)&t27));
                    gm_l5 = t30;
                    {
                        uint64_t t33 = gm_l5;
                        uint64_t t34 = (uintptr_t)t33;
                        void * t35 = gm_l1;
                        uint64_t t36 = GM_LD(uintptr_t, (((uint8_t *)t35) + 8) + 8);
                        if (t34 >= (uintptr_t)(t36)) gm_trap_index(t34, t36, gm_srcmhupam4se3h5, 165, 24);
                        uint8_t t37 = GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, (((uint8_t *)t35) + 8)) + t34 * 40) + 0));
                        if (t37 != ((uint8_t)0ULL)) goto ie32;
                        gm_t88rcucg1b7uk t38 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, (((uint8_t *)t35) + 8)) + t34 * 40) + 8));
                        gm_l6 = t38;
                        uint64_t t39 = gm_l5;
                        uint64_t t40 = (uintptr_t)t39;
                        void * t41 = gm_l2;
                        uint64_t t42 = GM_LD(uintptr_t, (((uint8_t *)t41) + 8) + 8);
                        if (t40 >= (uintptr_t)(t42)) gm_trap_index(t40, t42, gm_srcmhupam4se3h5, 165, 50);
                        uint8_t t43 = GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, (((uint8_t *)t41) + 8)) + t40 * 40) + 0));
                        if (t43 != ((uint8_t)0ULL)) goto ie32;
                        gm_t88rcucg1b7uk t44 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, (((uint8_t *)t41) + 8)) + t40 * 40) + 8));
                        gm_l7 = t44;
                        {
                            void * t47 = gm_l0;
                            gm_t88rcucg1b7uk t48 = gm_l6;
                            gm_t88rcucg1b7uk t49 = gm_l7;
                            uint8_t t50 = gm_f_provably_distinct_q4efcr7(t47, GM_CONV(gm_csguankko4lmhd, t48), GM_CONV(gm_csguankko4lmhd, t49));
                            if (!t50) goto ie46;
                            {
                                uint8_t t52 = 0;
                                F.ret = ((uint8_t)0);
                                goto gm_fret;
                            }
                            goto ix46;
ie46:;
ix46:;
                        }
                        goto ix32;
ie32:;
ix32:;
                    }
                    goto me28;
mn29:;
                    gm_trap("no match arm matched the scrutinee", gm_srcmhupam4se3h5, 164, 5);
me28:;
                }
lc21:;
                {
                    uint64_t t54 = gm_l4;
                    uint64_t r55; bool o56 = __builtin_add_overflow(t54, ((uint64_t)1ULL), &r55);
                    uint64_t t57 = r55;
                    gm_l4 = t57;
                }
            }
lb21:;
        }
    }
    {
        uint8_t t58 = 0;
        F.ret = ((uint8_t)1);
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_named_effect_set_s5gn1lr(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2) {
    struct gm_fr_named_effect_set_s5gn1lr F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* module */
    void * gm_l2; /* e */
    uint64_t gm_l3; /* d */
    gm_t08olq97rscn3 gm_l4; /* id */
    void * gm_l5; /* base */
    gm_t08olq97rscn3 gm_l6; /* name */
    uint64_t gm_l7; /* m */
    gm_tnpifv20irnph gm_l8; /* set */
    GM_STACK_CHECK(&F, 769u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        gm_l3 = ((uint64_t)0ULL);
    }
    {
        void * t2 = gm_l2;
        uint8_t t8 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        switch (t8) { case ((uint8_t)0ULL): goto ma6; case ((uint8_t)1ULL): goto ma6; case ((uint8_t)2ULL): goto ma6; case ((uint8_t)3ULL): goto ma6; case ((uint8_t)4ULL): goto ma6; case ((uint8_t)5ULL): goto ma6; case ((uint8_t)6ULL): goto ma4; case ((uint8_t)7ULL): goto ma6; case ((uint8_t)8ULL): goto ma6; case ((uint8_t)9ULL): goto ma6; case ((uint8_t)10ULL): goto ma6; case ((uint8_t)11ULL): goto ma6; case ((uint8_t)12ULL): goto ma6; case ((uint8_t)13ULL): goto ma6; case ((uint8_t)14ULL): goto ma6; case ((uint8_t)15ULL): goto ma6; case ((uint8_t)16ULL): goto ma6; case ((uint8_t)17ULL): goto ma6; case ((uint8_t)18ULL): goto ma6; case ((uint8_t)19ULL): goto ma6; case ((uint8_t)20ULL): goto ma6; case ((uint8_t)21ULL): goto ma6; case ((uint8_t)22ULL): goto ma5; case ((uint8_t)23ULL): goto ma6; case ((uint8_t)24ULL): goto ma6; case ((uint8_t)25ULL): goto ma6; case ((uint8_t)26ULL): goto ma6; case ((uint8_t)27ULL): goto ma6; case ((uint8_t)28ULL): goto ma6; case ((uint8_t)29ULL): goto ma6; case ((uint8_t)30ULL): goto ma6; case ((uint8_t)31ULL): goto ma6; case ((uint8_t)32ULL): goto ma6; case ((uint8_t)33ULL): goto ma6; case ((uint8_t)34ULL): goto ma6; case ((uint8_t)35ULL): goto ma6; case ((uint8_t)36ULL): goto ma6; case ((uint8_t)37ULL): goto ma6; case ((uint8_t)38ULL): goto ma6; case ((uint8_t)39ULL): goto ma6; case ((uint8_t)40ULL): goto ma6; case ((uint8_t)41ULL): goto ma6; case ((uint8_t)42ULL): goto ma6; case ((uint8_t)43ULL): goto ma6; case ((uint8_t)44ULL): goto ma6; case ((uint8_t)45ULL): goto ma6; case ((uint8_t)46ULL): goto ma6; case ((uint8_t)47ULL): goto ma6; case ((uint8_t)48ULL): goto ma6; case ((uint8_t)49ULL): goto ma6; case ((uint8_t)50ULL): goto ma6; default: goto ma6; }
ma4:;
        uint8_t t10 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t10 != ((uint8_t)6ULL)) goto mn9;
        gm_t08olq97rscn3 t11 = GM_LD(gm_t08olq97rscn3, ((((uint8_t *)t2) + 8) + 8));
        gm_l4 = t11;
        {
            void * t13 = gm_l0;
            uint64_t t14 = gm_l1;
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l4) + 0));
            gm_tf5srh4mp4b3i t16 = gm_f_lookup_module_name_cukfp7t((void *)NULL, cx, NULL, t13, t14, t15);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint64_t t17;
            if (((((uint8_t *)&t16))[8] != 0)) {
                uint64_t t18 = GM_LD(uint64_t, (((uint8_t *)&t16) + 0));
                t17 = t18;
            } else {
                gm_teseu4tn5o422 t19;
                memset(&t19, 0, sizeof t19);
                uint8_t t20 = 0;
                F.ret = t19;
                goto gm_fret;
            }
            gm_l3 = t17;
        }
        goto me3;
mn9:;
ma5:;
        uint8_t t22 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t22 != ((uint8_t)22ULL)) goto mn21;
        void * t23 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 0));
        gm_l5 = t23;
        gm_t08olq97rscn3 t24 = GM_LD(gm_t08olq97rscn3, (((((uint8_t *)t2) + 8) + 8) + 8));
        gm_l6 = t24;
        {
            void * t26 = gm_l0;
            uint64_t t27 = gm_l1;
            void * t28 = gm_l5;
            gm_tf5srh4mp4b3i t29 = gm_f_module_named_n1mcl3b((void *)NULL, cx, NULL, t26, t27, t28);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint64_t t30;
            if (((((uint8_t *)&t29))[8] != 0)) {
                uint64_t t31 = GM_LD(uint64_t, (((uint8_t *)&t29) + 0));
                t30 = t31;
            } else {
                gm_teseu4tn5o422 t32;
                memset(&t32, 0, sizeof t32);
                uint8_t t33 = 0;
                F.ret = t32;
                goto gm_fret;
            }
            gm_l7 = t30;
        }
        {
            void * t34 = gm_l0;
            uint64_t t35 = gm_l7;
            uint64_t t36 = gm_l1;
            gm_tf5srh4mp4b3i t37 = gm_f_module_member_5fd2qm9((void *)NULL, cx, NULL, t34, t35, &(*(gm_t08olq97rscn3 *)((uint8_t *)&gm_l6)), t36);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint64_t t38;
            if (((((uint8_t *)&t37))[8] != 0)) {
                uint64_t t39 = GM_LD(uint64_t, (((uint8_t *)&t37) + 0));
                t38 = t39;
            } else {
                gm_teseu4tn5o422 t40;
                memset(&t40, 0, sizeof t40);
                uint8_t t41 = 0;
                F.ret = t40;
                goto gm_fret;
            }
            gm_l3 = t38;
        }
        goto me3;
mn21:;
ma6:;
        gm_teseu4tn5o422 t43;
        memset(&t43, 0, sizeof t43);
        uint8_t t44 = 0;
        F.ret = t43;
        goto gm_fret;
        goto me3;
mn42:;
ma7:;
        gm_trap("no match arm matched the scrutinee", gm_srcmhupam4se3h5, 279, 5);
me3:;
    }
    {
        void * t46 = gm_l0;
        uint64_t t47 = gm_l3;
        gm_t6qooom8qa2do t48;
        gm_f_resolve_decl_pus042v((void *)&t48, (void *)NULL, cx, NULL, t46, t47);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t49 = GM_LD(uint8_t, (((uint8_t *)&t48) + 0));
        if (t49 != ((uint8_t)4ULL)) goto ie45;
        gm_tnpifv20irnph t50 = GM_LD(gm_tnpifv20irnph, (((uint8_t *)&t48) + 8));
        gm_l8 = t50;
        {
            gm_tnpifv20irnph t52 = gm_l8;
            GM_ST(uint8_t, (uint8_t *)&(*(gm_teseu4tn5o422 *)gm_out) + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_teseu4tn5o422 *)gm_out) + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_teseu4tn5o422 *)gm_out) + 20, 0);
            ((uint8_t *)&(*(gm_teseu4tn5o422 *)gm_out))[16] = 1;
            GM_ST(gm_tnpifv20irnph, (uint8_t *)&(*(gm_teseu4tn5o422 *)gm_out) + 0, t52);
            goto gm_fdone;
        }
        goto ix45;
ie45:;
        {
            gm_teseu4tn5o422 t54;
            memset(&t54, 0, sizeof t54);
            uint8_t t55 = 0;
            F.ret = t54;
            goto gm_fret;
        }
ix45:;
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
extern __attribute__((aligned(4))) uint32_t gm_f_new_literal_fn_oqjp4lg(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_tl874t2atnlh3 a2, uint64_t a3, gm_b16_8 a4, gm_t88rcucg1b7uk a5, gm_tnpifv20irnph a6);
inline __attribute__((aligned(4))) uint32_t gm_f_new_nested_fn_r5u4ioo(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t9qkh0rsooc8d a1, gm_b16_8 a2, gm_tl874t2atnlh3 a3, uint64_t a4, gm_b16_8 a5, gm_t88rcucg1b7uk a6, gm_tnpifv20irnph a7) {
    struct gm_fr_new_nested_fn_r5u4ioo F;
    void * gm_l0; /* s */
    gm_t9qkh0rsooc8d gm_l1; /* kind */
    gm_b16_8 gm_l2; /* name */
    gm_tl874t2atnlh3 gm_l3; /* span */
    uint64_t gm_l4; /* module */
    gm_b16_8 gm_l5; /* params */
    gm_t88rcucg1b7uk gm_l6; /* ret */
    gm_tnpifv20irnph gm_l7; /* effects */
    GM_STACK_CHECK(&F, 423u);
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
    gm_l7 = a7;
    {
        void * t2 = gm_l0;
        gm_tqqhoivahirr0 t3;
        memset((uint8_t *)&t3 + 28, 0, 4);
        memset((uint8_t *)&t3 + 60, 0, 4);
        memset((uint8_t *)&t3 + 129, 0, 7);
        gm_b16_8 t4 = gm_l2;
        GM_ST(gm_b16_8, (uint8_t *)&t3 + 0, t4);
        gm_tl874t2atnlh3 t5 = gm_l3;
        GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t3 + 16, t5);
        uint64_t t6 = gm_l4;
        GM_ST(uint64_t, (uint8_t *)&t3 + 32, t6);
        gm_b16_8 t7 = gm_l5;
        GM_ST(gm_b16_8, (uint8_t *)&t3 + 40, t7);
        gm_t88rcucg1b7uk t8 = gm_l6;
        GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t3 + 56, t8);
        gm_tnpifv20irnph t9 = gm_l7;
        GM_ST(gm_tnpifv20irnph, (uint8_t *)&t3 + 64, t9);
        gm_b16_8 t10 = gm_f_empty_slice_d914i5u();
        GM_ST(gm_b16_8, (uint8_t *)&t3 + 80, t10);
        gm_tqu3m2l3n8mnc t11;
        memset(&t11, 0, sizeof t11);
        GM_ST(gm_tqu3m2l3n8mnc, (uint8_t *)&t3 + 96, t11);
        GM_ST(uint8_t, (uint8_t *)&t3 + 128, ((uint8_t)0));
        gm_t9qkh0rsooc8d t12 = gm_l1;
        gm_tdgfduqjheqdm t13;
        gm_f_bodiless_def_u9kqo09((void *)&t13, t12);
        uint32_t t14 = gm_f_add_fn_t4t83qh((void *)NULL, cx, NULL, t2, &t3, &t13);
        if (gm_unw.kind) {
            goto gm_lad;
        }
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
__attribute__((aligned(4))) uint8_t gm_f_provably_distinct_q4efcr7(void * a0, gm_csguankko4lmhd a1, gm_csguankko4lmhd a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_provably_distinct_q4efcr7 F;
    void * gm_l0; /* s */
    gm_t88rcucg1b7uk gm_l1; /* a */
    gm_t88rcucg1b7uk gm_l2; /* b */
    gm_t88rcucg1b7uk gm_l5; /* x */
    gm_t88rcucg1b7uk gm_l6; /* y */
    gm_t88rcucg1b7uk gm_l7; /* x */
    gm_t88rcucg1b7uk gm_l8; /* y */
    gm_t88rcucg1b7uk gm_l9; /* x */
    gm_t88rcucg1b7uk gm_l10; /* y */
    gm_t88rcucg1b7uk gm_l11; /* x */
    gm_t88rcucg1b7uk gm_l12; /* y */
    uint32_t gm_l13; /* x */
    gm_b16_8 gm_l14; /* xs */
    uint32_t gm_l15; /* y */
    gm_b16_8 gm_l16; /* ys */
    uint64_t gm_l17; /* <tmp> */
    uint64_t gm_l18; /* <tmp> */
    uint64_t gm_l19; /* i */
    gm_t88rcucg1b7uk gm_l20; /* p */
    gm_t88rcucg1b7uk gm_l21; /* q */
    gm_b16_8 gm_l22; /* x */
    gm_b16_8 gm_l23; /* y */
    uint64_t gm_l24; /* <tmp> */
    uint64_t gm_l25; /* <tmp> */
    uint64_t gm_l26; /* i */
    GM_STACK_CHECK(&F, 925u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    gm_l2 = GM_CONV(gm_t88rcucg1b7uk, a2);
    {
        void * t3 = gm_l0;
        void * t4 = (void *)(((uint8_t *)t3) + 8);
        gm_t88rcucg1b7uk t5 = gm_l1;
        uint8_t t6 = gm_f_has_param_u9t0efs(t4, GM_CONV(gm_csguankko4lmhd, t5));
        uint8_t t7 = (uint8_t)!t6;
        if (!t7) goto ie2;
        void * t8 = gm_l0;
        void * t9 = (void *)(((uint8_t *)t8) + 8);
        gm_t88rcucg1b7uk t10 = gm_l2;
        uint8_t t11 = gm_f_has_param_u9t0efs(t9, GM_CONV(gm_csguankko4lmhd, t10));
        uint8_t t12 = (uint8_t)!t11;
        if (!t12) goto ie2;
        {
            gm_t88rcucg1b7uk t14 = gm_l1;
            gm_t88rcucg1b7uk t15 = gm_l2;
            uint8_t t16 = (!gm_eq9m6mvtmgde(((uint8_t *)&t14), ((uint8_t *)&t15)));
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
        void * t19 = (void *)(((uint8_t *)t18) + 8);
        gm_t88rcucg1b7uk t20 = gm_l1;
        gm_f_kind_jcnt4hm((void *)&F.l3, t19, GM_CONV(gm_csguankko4lmhd, t20));
    }
    {
        void * t21 = gm_l0;
        void * t22 = (void *)(((uint8_t *)t21) + 8);
        gm_t88rcucg1b7uk t23 = gm_l2;
        gm_f_kind_jcnt4hm((void *)&F.l4, t22, GM_CONV(gm_csguankko4lmhd, t23));
    }
    {
        uint8_t t25 = 0;
        uint8_t t26;
        uint8_t t29 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 0));
        if (t29 != ((uint8_t)19ULL)) goto mn28;
        t26 = ((uint8_t)1);
        goto me27;
mn28:;
        t26 = ((uint8_t)0);
        goto me27;
mn30:;
        gm_trap("no match arm matched the scrutinee", gm_srcmhupam4se3h5, 176, 8);
me27:;
        if (!t26) {
            uint8_t t31;
            uint8_t t34 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 0));
            if (t34 != ((uint8_t)19ULL)) goto mn33;
            t31 = ((uint8_t)1);
            goto me32;
mn33:;
            t31 = ((uint8_t)0);
            goto me32;
mn35:;
            gm_trap("no match arm matched the scrutinee", gm_srcmhupam4se3h5, 176, 27);
me32:;
            t25 = t31;
        } else t25 = 1;
        if (!t25) goto ie24;
        {
            uint8_t t37 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        uint8_t t47 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 0));
        switch (t47) { case ((uint8_t)0ULL): goto ma45; case ((uint8_t)1ULL): goto ma45; case ((uint8_t)2ULL): goto ma45; case ((uint8_t)3ULL): goto ma45; case ((uint8_t)4ULL): goto ma45; case ((uint8_t)5ULL): goto ma45; case ((uint8_t)6ULL): goto ma45; case ((uint8_t)7ULL): goto ma45; case ((uint8_t)8ULL): goto ma39; case ((uint8_t)9ULL): goto ma41; case ((uint8_t)10ULL): goto ma40; case ((uint8_t)11ULL): goto ma45; case ((uint8_t)12ULL): goto ma44; case ((uint8_t)13ULL): goto ma42; case ((uint8_t)14ULL): goto ma45; case ((uint8_t)15ULL): goto ma45; case ((uint8_t)16ULL): goto ma45; case ((uint8_t)17ULL): goto ma43; case ((uint8_t)18ULL): goto ma45; case ((uint8_t)19ULL): goto ma45; case ((uint8_t)20ULL): goto ma45; case ((uint8_t)21ULL): goto ma45; case ((uint8_t)22ULL): goto ma45; case ((uint8_t)23ULL): goto ma45; case ((uint8_t)24ULL): goto ma45; case ((uint8_t)25ULL): goto ma45; case ((uint8_t)26ULL): goto ma45; default: goto ma45; }
ma39:;
        uint8_t t49 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 0));
        if (t49 != ((uint8_t)8ULL)) goto mn48;
        gm_t88rcucg1b7uk t50 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&F.l3) + 8) + 4));
        gm_l5 = t50;
        uint8_t t52 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 0));
        if (t52 != ((uint8_t)8ULL)) goto ie51;
        gm_t88rcucg1b7uk t53 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&F.l4) + 8) + 4));
        gm_l6 = t53;
        {
            void * t55 = gm_l0;
            gm_t88rcucg1b7uk t56 = gm_l5;
            gm_t88rcucg1b7uk t57 = gm_l6;
            uint8_t t58 = gm_f_provably_distinct_q4efcr7(t55, GM_CONV(gm_csguankko4lmhd, t56), GM_CONV(gm_csguankko4lmhd, t57));
            uint8_t t59 = 0;
            F.ret = t58;
            goto gm_fret;
        }
        goto ix51;
ie51:;
ix51:;
        goto me38;
mn48:;
ma40:;
        uint8_t t61 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 0));
        if (t61 != ((uint8_t)10ULL)) goto mn60;
        gm_t88rcucg1b7uk t62 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&F.l3) + 8) + 4));
        gm_l7 = t62;
        uint8_t t64 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 0));
        if (t64 != ((uint8_t)10ULL)) goto ie63;
        gm_t88rcucg1b7uk t65 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&F.l4) + 8) + 4));
        gm_l8 = t65;
        {
            void * t67 = gm_l0;
            gm_t88rcucg1b7uk t68 = gm_l7;
            gm_t88rcucg1b7uk t69 = gm_l8;
            uint8_t t70 = gm_f_provably_distinct_q4efcr7(t67, GM_CONV(gm_csguankko4lmhd, t68), GM_CONV(gm_csguankko4lmhd, t69));
            uint8_t t71 = 0;
            F.ret = t70;
            goto gm_fret;
        }
        goto ix63;
ie63:;
ix63:;
        goto me38;
mn60:;
ma41:;
        uint8_t t73 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 0));
        if (t73 != ((uint8_t)9ULL)) goto mn72;
        gm_t88rcucg1b7uk t74 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&F.l3) + 8) + 4));
        gm_l9 = t74;
        uint8_t t76 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 0));
        if (t76 != ((uint8_t)9ULL)) goto ie75;
        gm_t88rcucg1b7uk t77 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&F.l4) + 8) + 4));
        gm_l10 = t77;
        {
            void * t79 = gm_l0;
            gm_t88rcucg1b7uk t80 = gm_l9;
            gm_t88rcucg1b7uk t81 = gm_l10;
            uint8_t t82 = gm_f_provably_distinct_q4efcr7(t79, GM_CONV(gm_csguankko4lmhd, t80), GM_CONV(gm_csguankko4lmhd, t81));
            uint8_t t83 = 0;
            F.ret = t82;
            goto gm_fret;
        }
        goto ix75;
ie75:;
ix75:;
        goto me38;
mn72:;
ma42:;
        uint8_t t85 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 0));
        if (t85 != ((uint8_t)13ULL)) goto mn84;
        gm_t88rcucg1b7uk t86 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l3) + 8));
        gm_l11 = t86;
        uint8_t t88 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 0));
        if (t88 != ((uint8_t)13ULL)) goto ie87;
        gm_t88rcucg1b7uk t89 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l4) + 8));
        gm_l12 = t89;
        {
            void * t91 = gm_l0;
            gm_t88rcucg1b7uk t92 = gm_l11;
            gm_t88rcucg1b7uk t93 = gm_l12;
            uint8_t t94 = gm_f_provably_distinct_q4efcr7(t91, GM_CONV(gm_csguankko4lmhd, t92), GM_CONV(gm_csguankko4lmhd, t93));
            uint8_t t95 = 0;
            F.ret = t94;
            goto gm_fret;
        }
        goto ix87;
ie87:;
ix87:;
        goto me38;
mn84:;
ma43:;
        uint8_t t97 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 0));
        if (t97 != ((uint8_t)17ULL)) goto mn96;
        uint32_t t98 = GM_LD(uint32_t, ((((uint8_t *)&F.l3) + 8) + 0));
        gm_l13 = t98;
        gm_b16_8 t99 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l3) + 8) + 8));
        gm_l14 = t99;
        uint8_t t101 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 0));
        if (t101 != ((uint8_t)17ULL)) goto ie100;
        uint32_t t102 = GM_LD(uint32_t, ((((uint8_t *)&F.l4) + 8) + 0));
        gm_l15 = t102;
        gm_b16_8 t103 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l4) + 8) + 8));
        gm_l16 = t103;
        {
            void * t106 = gm_l0;
            void * t107 = (void *)(((uint8_t *)t106) + 8);
            uint32_t t108 = gm_l13;
            uint32_t t109 = gm_f_template_9o92cvv(t107, t108);
            void * t110 = gm_l0;
            void * t111 = (void *)(((uint8_t *)t110) + 8);
            uint32_t t112 = gm_l15;
            uint32_t t113 = gm_f_template_9o92cvv(t111, t112);
            uint8_t t114 = (!(t109 == t113));
            if (!t114) goto ie105;
            {
                uint8_t t116 = 0;
                F.ret = ((uint8_t)1);
                goto gm_fret;
            }
            goto ix105;
ie105:;
ix105:;
        }
        {
            {
                gm_l18 = ((uint64_t)0ULL);
            }
            {
                uint64_t t118 = GM_LD(uintptr_t, ((uint8_t *)&gm_l14) + 8);
                uint64_t t119 = GM_LD(uintptr_t, ((uint8_t *)&gm_l16) + 8);
                uint64_t t120 = (t118 < t119 ? t118 : t119);
                gm_l17 = t120;
            }
            {
                for (;;) {
                    uint64_t t122 = gm_l18;
                    uint64_t t123 = gm_l17;
                    uint8_t t124 = (t122 < t123);
                    if (!t124) goto lb121;
                    {
                        uint64_t t127 = gm_l18;
                        uint64_t t130 = GM_LD(uint64_t, ((uint8_t *)&t127));
                        gm_l19 = t130;
                        {
                            uint64_t t133 = gm_l19;
                            uint64_t t134 = (uintptr_t)t133;
                            uint64_t t135 = GM_LD(uintptr_t, ((uint8_t *)&gm_l14) + 8);
                            if (t134 >= (uintptr_t)(t135)) gm_trap_index(t134, t135, gm_srcmhupam4se3h5, 185, 32);
                            uint8_t t136 = GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l14)) + t134 * 40) + 0));
                            if (t136 != ((uint8_t)0ULL)) goto ie132;
                            gm_t88rcucg1b7uk t137 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l14)) + t134 * 40) + 8));
                            gm_l20 = t137;
                            uint64_t t138 = gm_l19;
                            uint64_t t139 = (uintptr_t)t138;
                            uint64_t t140 = GM_LD(uintptr_t, ((uint8_t *)&gm_l16) + 8);
                            if (t139 >= (uintptr_t)(t140)) gm_trap_index(t139, t140, gm_srcmhupam4se3h5, 185, 54);
                            uint8_t t141 = GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l16)) + t139 * 40) + 0));
                            if (t141 != ((uint8_t)0ULL)) goto ie132;
                            gm_t88rcucg1b7uk t142 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l16)) + t139 * 40) + 8));
                            gm_l21 = t142;
                            {
                                void * t145 = gm_l0;
                                gm_t88rcucg1b7uk t146 = gm_l20;
                                gm_t88rcucg1b7uk t147 = gm_l21;
                                uint8_t t148 = gm_f_provably_distinct_q4efcr7(t145, GM_CONV(gm_csguankko4lmhd, t146), GM_CONV(gm_csguankko4lmhd, t147));
                                if (!t148) goto ie144;
                                {
                                    uint8_t t150 = 0;
                                    F.ret = ((uint8_t)1);
                                    goto gm_fret;
                                }
                                goto ix144;
ie144:;
ix144:;
                            }
                            goto ix132;
ie132:;
ix132:;
                        }
                        goto me128;
mn129:;
                        gm_trap("no match arm matched the scrutinee", gm_srcmhupam4se3h5, 184, 13);
me128:;
                    }
lc121:;
                    {
                        uint64_t t152 = gm_l18;
                        uint64_t r153; bool o154 = __builtin_add_overflow(t152, ((uint64_t)1ULL), &r153);
                        uint64_t t155 = r153;
                        gm_l18 = t155;
                    }
                }
lb121:;
            }
        }
        {
            uint8_t t156 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        goto ix100;
ie100:;
ix100:;
        goto me38;
mn96:;
ma44:;
        uint8_t t158 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 0));
        if (t158 != ((uint8_t)12ULL)) goto mn157;
        gm_b16_8 t159 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l3) + 8) + 0));
        gm_l22 = t159;
        uint8_t t161 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 0));
        if (t161 != ((uint8_t)12ULL)) goto ie160;
        gm_b16_8 t162 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l4) + 8) + 0));
        gm_l23 = t162;
        {
            uint64_t t165 = GM_LD(uintptr_t, ((uint8_t *)&gm_l22) + 8);
            uint64_t t166 = GM_LD(uintptr_t, ((uint8_t *)&gm_l23) + 8);
            uint8_t t167 = (!(t165 == t166));
            if (!t167) goto ie164;
            {
                uint8_t t169 = 0;
                F.ret = ((uint8_t)1);
                goto gm_fret;
            }
            goto ix164;
ie164:;
ix164:;
        }
        {
            {
                gm_l25 = ((uint64_t)0ULL);
            }
            {
                uint64_t t171 = GM_LD(uintptr_t, ((uint8_t *)&gm_l22) + 8);
                gm_l24 = t171;
            }
            {
                for (;;) {
                    uint64_t t173 = gm_l25;
                    uint64_t t174 = gm_l24;
                    uint8_t t175 = (t173 < t174);
                    if (!t175) goto lb172;
                    {
                        uint64_t t178 = gm_l25;
                        uint64_t t181 = GM_LD(uint64_t, ((uint8_t *)&t178));
                        gm_l26 = t181;
                        {
                            void * t184 = gm_l0;
                            uint64_t t185 = gm_l26;
                            uint64_t t186 = (uintptr_t)t185;
                            uint64_t t187 = GM_LD(uintptr_t, ((uint8_t *)&gm_l22) + 8);
                            if (t186 >= (uintptr_t)(t187)) gm_trap_index(t186, t187, gm_srcmhupam4se3h5, 194, 40);
                            gm_t88rcucg1b7uk t188 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l22)) + t186 * 4));
                            uint64_t t189 = gm_l26;
                            uint64_t t190 = (uintptr_t)t189;
                            uint64_t t191 = GM_LD(uintptr_t, ((uint8_t *)&gm_l23) + 8);
                            if (t190 >= (uintptr_t)(t191)) gm_trap_index(t190, t191, gm_srcmhupam4se3h5, 194, 46);
                            gm_t88rcucg1b7uk t192 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l23)) + t190 * 4));
                            uint8_t t193 = gm_f_provably_distinct_q4efcr7(t184, GM_CONV(gm_csguankko4lmhd, t188), GM_CONV(gm_csguankko4lmhd, t192));
                            if (!t193) goto ie183;
                            {
                                uint8_t t195 = 0;
                                F.ret = ((uint8_t)1);
                                goto gm_fret;
                            }
                            goto ix183;
ie183:;
ix183:;
                        }
                        goto me179;
mn180:;
                        gm_trap("no match arm matched the scrutinee", gm_srcmhupam4se3h5, 193, 13);
me179:;
                    }
lc172:;
                    {
                        uint64_t t197 = gm_l25;
                        uint64_t r198; bool o199 = __builtin_add_overflow(t197, ((uint64_t)1ULL), &r198);
                        uint64_t t200 = r198;
                        gm_l25 = t200;
                    }
                }
lb172:;
            }
        }
        {
            uint8_t t201 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        goto ix160;
ie160:;
ix160:;
        goto me38;
mn157:;
ma45:;
        goto me38;
mn202:;
ma46:;
        gm_trap("no match arm matched the scrutinee", gm_srcmhupam4se3h5, 177, 5);
me38:;
    }
    {
        gm_t53at80pfdus3 t204 = F.l3;
        uint8_t t205 = GM_LD(uint8_t, ((uint8_t *)&t204));
        gm_t53at80pfdus3 t206 = F.l4;
        uint8_t t207 = GM_LD(uint8_t, ((uint8_t *)&t206));
        uint8_t t208 = (!(t205 == t207));
        uint8_t t209 = 0;
        F.ret = t208;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_resolve_effect_arg_4h6q4qv(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2) {
    struct gm_fr_resolve_effect_arg_4h6q4qv F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* module */
    void * gm_l2; /* e */
    gm_t88rcucg1b7uk gm_l3; /* p */
    gm_tnpifv20irnph gm_l4; /* set */
    gm_tnpifv20irnph gm_l5; /* set */
    gm_tiilmfd16v3fc gm_l6; /* r */
    gm_tfe2ufaqq8re9 gm_l7; /* <temporary array> */
    GM_STACK_CHECK(&F, 741u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t3 = gm_l0;
        void * t4 = gm_l2;
        gm_tkn42u7na3t81 t5 = gm_f_effect_param_named_h2jeoo7(t3, t4);
        if (!((((uint8_t *)&t5))[4] != 0)) goto ie2;
        gm_t88rcucg1b7uk t6 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&t5));
        gm_l3 = t6;
        {
            gm_t88rcucg1b7uk t8 = gm_l3;
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
        uint64_t t12 = gm_l1;
        void * t13 = gm_l2;
        gm_teseu4tn5o422 t14;
        gm_f_named_effect_set_s5gn1lr((void *)&t14, (void *)NULL, cx, NULL, t11, t12, t13);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (!((((uint8_t *)&t14))[16] != 0)) goto ie10;
        gm_tnpifv20irnph t15 = GM_LD(gm_tnpifv20irnph, ((uint8_t *)&t14));
        gm_l4 = t15;
        {
            void * t17 = gm_l0;
            void * t18 = (void *)(((uint8_t *)t17) + 8);
            gm_tnpifv20irnph t19 = gm_l4;
            gm_t53at80pfdus3 t20;
            memset(&t20, 0, sizeof t20);
            GM_ST(uint8_t, ((uint8_t *)&t20 + 0), ((uint8_t)24ULL));
            GM_ST(gm_tnpifv20irnph, ((uint8_t *)&t20 + 8), t19);
            gm_t88rcucg1b7uk t21 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t18, &t20);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t22 = 0;
            F.ret = t21;
            goto gm_fret;
        }
        goto ix10;
ie10:;
ix10:;
    }
    {
        void * t24 = gm_l2;
        uint8_t t25;
        uint8_t t28 = GM_LD(uint8_t, ((((uint8_t *)t24) + 8) + 0));
        if (t28 != ((uint8_t)13ULL)) goto mn27;
        uint8_t t29 = GM_LD(uint8_t, ((((((uint8_t *)t24) + 8) + 8) + 0) + 0));
        if (t29 != ((uint8_t)0ULL)) goto mn27;
        uint8_t t30 = GM_LD(uint8_t, (((((((uint8_t *)t24) + 8) + 8) + 0) + 1) + 0));
        if (t30 != ((uint8_t)0ULL)) goto mn27;
        t25 = ((uint8_t)1);
        goto me26;
mn27:;
        t25 = ((uint8_t)0);
        goto me26;
mn31:;
        gm_trap("no match arm matched the scrutinee", gm_srcmhupam4se3h5, 395, 8);
me26:;
        if (!t25) goto ie23;
        {
            void * t33 = gm_l0;
            uint64_t t34 = gm_l1;
            void * t35 = gm_l2;
            gm_b16_8 t36 = gm_f_effect_terms_bphj7th((void *)NULL, cx, NULL, t35);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_b16_8 t37 = GM_LD(gm_b16_8, ((uint8_t *)&t36));
            gm_tnpifv20irnph t38 = gm_f_resolve_effect_set_o7rc5od((void *)NULL, cx, NULL, t33, t34, t37);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l5 = t38;
        }
        {
            void * t39 = gm_l0;
            void * t40 = (void *)(((uint8_t *)t39) + 8);
            gm_tnpifv20irnph t41 = gm_l5;
            gm_t53at80pfdus3 t42;
            memset(&t42, 0, sizeof t42);
            GM_ST(uint8_t, ((uint8_t *)&t42 + 0), ((uint8_t)24ULL));
            GM_ST(gm_tnpifv20irnph, ((uint8_t *)&t42 + 8), t41);
            gm_t88rcucg1b7uk t43 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t40, &t42);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t44 = 0;
            F.ret = t43;
            goto gm_fret;
        }
        goto ix23;
ie23:;
ix23:;
    }
    {
        void * t45 = gm_l0;
        uint64_t t46 = gm_l1;
        void * t47 = gm_l2;
        gm_tqu3m2l3n8mnc t48;
        gm_f_resolve_effect_ref_bggirs2((void *)&t48, (void *)NULL, cx, NULL, t45, t46, t47);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tiilmfd16v3fc t49;
        if (((((uint8_t *)&t48))[24] != 0)) {
            t49 = (*(gm_tiilmfd16v3fc *)(((uint8_t *)&t48) + 0));
        } else {
            void * t50 = gm_l0;
            void * t51 = (void *)(((uint8_t *)t50) + 8);
            gm_t88rcucg1b7uk t52 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t51);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t53 = 0;
            F.ret = t52;
            goto gm_fret;
        }
        gm_l6 = t49;
    }
    {
    }
    {
        void * t54 = gm_l0;
        void * t55 = (void *)(((uint8_t *)t54) + 8);
        {
            gm_tiilmfd16v3fc t57 = gm_l6;
            GM_ST(gm_tiilmfd16v3fc, (uint8_t *)&(*(gm_tfe2ufaqq8re9 *)((uint8_t *)&gm_l7)) + 0, t57);
        }
        void * t58 = ((uint8_t *)&gm_l7);
        if ((uintptr_t)0 > (uintptr_t)1 || (uintptr_t)1 > (uintptr_t)1) gm_trap_slice((uintptr_t)0, (uintptr_t)1, 1, gm_srcmhupam4se3h5, 400, 51);
        gm_b16_8 t59;
        GM_ST(void *, (uint8_t *)&t59, (void *)(((uint8_t *)t58 + (uintptr_t)0 * 24)));
        GM_ST(uintptr_t, (uint8_t *)&t59 + 8, (uintptr_t)((uintptr_t)1 - (uintptr_t)0));
        gm_b16_8 t60 = t59;
        gm_tnpifv20irnph t61 = gm_f_effect_set_vtcpipr((void *)NULL, cx, NULL, t60);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t53at80pfdus3 t62;
        memset(&t62, 0, sizeof t62);
        GM_ST(uint8_t, ((uint8_t *)&t62 + 0), ((uint8_t)24ULL));
        GM_ST(gm_tnpifv20irnph, ((uint8_t *)&t62 + 8), t61);
        gm_t88rcucg1b7uk t63 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t55, &t62);
        if (gm_unw.kind) {
            goto gm_lad;
        }
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
inline __attribute__((aligned(4))) gm_tnpifv20irnph gm_f_resolve_effect_list_uhjlh6o(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2) {
    struct gm_fr_resolve_effect_list_uhjlh6o F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* module */
    gm_b16_8 gm_l2; /* list_ */
    uint64_t gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    uint64_t gm_l6; /* i */
    GM_STACK_CHECK(&F, 252u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        gm_f_list_qs7fohu((void *)&F.l3, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
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
                    uint64_t t13 = GM_LD(uint64_t, ((uint8_t *)&t10));
                    gm_l6 = t13;
                    {
                        void * t15 = (void *)((uint8_t *)&F.l3);
                        uint64_t t16 = gm_l6;
                        uint64_t t17 = (uintptr_t)t16;
                        uint64_t t18 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                        if (t17 >= (uintptr_t)(t18)) gm_trap_index(t17, t18, gm_srcmhupam4se3h5, 254, 41);
                        void * t19 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t17 * 88);
                        gm_tftb1odc739t5 t20 = gm_f_push_57kepgg(t15, t19);
                        if (((((uint8_t *)&t20))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t20) + 1, gm_srcmhupam4se3h5, 254, 29);
                        gm_b0_1 t21 = GM_LD(gm_b0_1, (((uint8_t *)&t20) + 1));
                    }
                    goto me11;
mn12:;
                    gm_trap("no match arm matched the scrutinee", gm_srcmhupam4se3h5, 254, 5);
me11:;
                }
lc4:;
                {
                    uint64_t t23 = gm_l5;
                    uint64_t r24; bool o25 = __builtin_add_overflow(t23, ((uint64_t)1ULL), &r24);
                    uint64_t t26 = r24;
                    gm_l5 = t26;
                }
            }
lb4:;
        }
    }
    {
        void * t27 = gm_l0;
        uint64_t t28 = gm_l1;
        gm_b16_8 t29 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
        gm_b16_8 t30 = GM_LD(gm_b16_8, ((uint8_t *)&t29));
        gm_tnpifv20irnph t31 = gm_f_resolve_effect_set_o7rc5od((void *)NULL, cx, NULL, t27, t28, t30);
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
__attribute__((aligned(4))) void gm_f_resolve_effect_ref_bggirs2(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2) {
    struct gm_fr_resolve_effect_ref_bggirs2 F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* module */
    void * gm_l2; /* e */
    gm_t88rcucg1b7uk gm_l3; /* p */
    void * gm_l4; /* callee */
    gm_b16_8 gm_l5; /* args */
    gm_tiilmfd16v3fc gm_l6; /* base */
    gm_b16_8 gm_l7; /* params */
    uint64_t gm_l8; /* required */
    gm_b16_8 gm_l9; /* <tmp> */
    uint64_t gm_l10; /* <tmp> */
    uint64_t gm_l11; /* i */
    uint32_t gm_l12; /* p */
    gm_b16_8 gm_l14; /* <tmp> */
    uint64_t gm_l15; /* <tmp> */
    uint64_t gm_l16; /* i */
    uint32_t gm_l17; /* p */
    void * gm_l18; /* a */
    gm_t08olq97rscn3 gm_l20; /* id */
    uint64_t gm_l21; /* d */
    uint32_t gm_l22; /* eff */
    gm_tiilmfd16v3fc gm_l23; /* r */
    void * gm_l24; /* base */
    gm_t08olq97rscn3 gm_l25; /* name */
    uint64_t gm_l26; /* m */
    uint64_t gm_l27; /* d */
    uint32_t gm_l28; /* eff */
    gm_tiilmfd16v3fc gm_l29; /* r */
    GM_STACK_CHECK(&F, 3509u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t3 = gm_l0;
        void * t4 = gm_l2;
        gm_tkn42u7na3t81 t5 = gm_f_effect_param_named_h2jeoo7(t3, t4);
        if (!((((uint8_t *)&t5))[4] != 0)) goto ie2;
        gm_t88rcucg1b7uk t6 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&t5));
        gm_l3 = t6;
        {
            void * t8 = gm_l0;
            void * t9 = gm_l2;
            gm_tl874t2atnlh3 t10 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t9) + 72));
            void * t11 = gm_l0;
            gm_t88rcucg1b7uk t12 = gm_l3;
            gm_b16_8 t13 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t11, t12);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tnpifv20irnph t14;
            GM_ST(gm_b16_8, ((uint8_t *)&t14 + 0), t13);
            gm_tabb710d9mq2e t15;
            gm_f_format_uvukia4((void *)&t15, (void *)NULL, cx, NULL, t14);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t15))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t15) + 8, gm_srcmhupam4se3h5, 293, 25);
            gm_b16_8 t16 = GM_LD(gm_b16_8, (((uint8_t *)&t15) + 8));
            gm_b16_8 t17 = GM_LD(gm_b16_8, ((uint8_t *)&t16));
            gm_b0_1 t18 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t8, t10, t17);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_tqu3m2l3n8mnc t19;
            memset(&t19, 0, sizeof t19);
            uint8_t t20 = 0;
            F.ret = t19;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t22 = gm_l2;
        uint8_t t23 = GM_LD(uint8_t, ((((uint8_t *)t22) + 8) + 0));
        if (t23 != ((uint8_t)19ULL)) goto ie21;
        void * t24 = GM_LD(void *, (((((uint8_t *)t22) + 8) + 8) + 0));
        gm_l4 = t24;
        gm_b16_8 t25 = GM_LD(gm_b16_8, (((((uint8_t *)t22) + 8) + 8) + 8));
        gm_l5 = t25;
        {
            void * t27 = gm_l0;
            uint64_t t28 = gm_l1;
            void * t29 = gm_l4;
            gm_tqu3m2l3n8mnc t30;
            gm_f_resolve_effect_ref_bggirs2((void *)&t30, (void *)NULL, cx, NULL, t27, t28, t29);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tiilmfd16v3fc t31;
            if (((((uint8_t *)&t30))[24] != 0)) {
                t31 = (*(gm_tiilmfd16v3fc *)(((uint8_t *)&t30) + 0));
            } else {
                gm_tqu3m2l3n8mnc t32;
                memset(&t32, 0, sizeof t32);
                uint8_t t33 = 0;
                F.ret = t32;
                goto gm_fret;
            }
            gm_l6 = t31;
        }
        {
            uint32_t t34 = GM_LD(uint32_t, (((uint8_t *)&gm_l6) + 0));
            uint64_t t35 = (uint64_t)t34;
            uint64_t t36 = (uintptr_t)t35;
            void * t37 = gm_l0;
            uint64_t t38 = GM_LD(uintptr_t, ((((uint8_t *)t37) + 1048) + 0) + 8);
            if (t36 >= (uintptr_t)(t38)) gm_trap_index(t36, t38, gm_srcmhupam4se3h5, 299, 19);
            gm_b16_8 t39 = GM_LD(gm_b16_8, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t37) + 1048) + 0)) + t36 * 56) + 24));
            gm_l7 = t39;
        }
        {
            gm_l8 = ((uint64_t)0ULL);
        }
        {
            {
                gm_b16_8 t41 = gm_l7;
                gm_b16_8 t42 = GM_LD(gm_b16_8, ((uint8_t *)&t41));
                gm_l9 = t42;
            }
            {
                gm_l10 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t44 = gm_l10;
                    uint64_t t45 = GM_LD(uintptr_t, ((uint8_t *)&gm_l9) + 8);
                    uint8_t t46 = (t44 < t45);
                    if (!t46) goto lb43;
                    {
                        uint64_t t49 = gm_l10;
                        uint64_t t50 = gm_l10;
                        uint64_t t51 = (uintptr_t)t50;
                        uint64_t t52 = GM_LD(uintptr_t, ((uint8_t *)&gm_l9) + 8);
                        uint32_t t53 = GM_LD(uint32_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l9)) + t51 * 4));
                        gm_tn68el8d14vku t54;
                        GM_ST(uint32_t, (uint8_t *)&t54 + 12, 0);
                        GM_ST(uint64_t, ((uint8_t *)&t54 + 0), t49);
                        GM_ST(uint32_t, ((uint8_t *)&t54 + 8), t53);
                        uint64_t t57 = GM_LD(uint64_t, (((uint8_t *)&t54) + 0));
                        gm_l11 = t57;
                        uint32_t t58 = GM_LD(uint32_t, (((uint8_t *)&t54) + 8));
                        gm_l12 = t58;
                        {
                            uint32_t t61 = gm_l12;
                            uint64_t t62 = (uint64_t)t61;
                            uint64_t t63 = (uintptr_t)t62;
                            void * t64 = gm_l0;
                            uint64_t t65 = GM_LD(uintptr_t, ((((uint8_t *)t64) + 1128) + 0) + 8);
                            if (t63 >= (uintptr_t)(t65)) gm_trap_index(t63, t65, gm_srcmhupam4se3h5, 303, 16);
                            uint8_t t66 = (!(GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t64) + 1128) + 0)) + t63 * 48) + 0) + 0) == ((uint8_t)1ULL)));
                            if (!t66) goto ie60;
                            {
                                uint64_t t68 = gm_l11;
                                uint64_t r69; bool o70 = __builtin_add_overflow(t68, ((uint64_t)1ULL), &r69);
                                if (o70) gm_trap("integer overflow in `+`", gm_srcmhupam4se3h5, 303, 75);
                                uint64_t t71 = r69;
                                gm_l8 = t71;
                            }
                            goto ix60;
ie60:;
ix60:;
                        }
                        goto me55;
mn56:;
                        gm_trap("no match arm matched the scrutinee", gm_srcmhupam4se3h5, 302, 9);
me55:;
                    }
lc43:;
                    {
                        uint64_t t73 = gm_l10;
                        uint64_t r74; bool o75 = __builtin_add_overflow(t73, ((uint64_t)1ULL), &r74);
                        uint64_t t76 = r74;
                        gm_l10 = t76;
                    }
                }
lb43:;
            }
        }
        {
            uint8_t t78 = 0;
            uint64_t t79 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
            uint64_t t80 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
            uint8_t t81 = (t79 > t80);
            if (!t81) {
                uint64_t t82 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                uint64_t t83 = gm_l8;
                uint8_t t84 = (t82 < t83);
                t78 = t84;
            } else t78 = 1;
            if (!t78) goto ie77;
            {
                void * t86 = gm_l0;
                void * t87 = gm_l2;
                gm_tl874t2atnlh3 t88 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t87) + 72));
                gm_t3877pk7lv748 t89;
                void * t90 = gm_l0;
                void * t91 = (void *)(((uint8_t *)t90) + 8);
                uint32_t t92 = GM_LD(uint32_t, (((uint8_t *)&gm_l6) + 0));
                gm_b16_8 t93 = gm_f_effect_name_prpp0ip(t91, t92);
                GM_ST(gm_b16_8, (uint8_t *)&t89 + 0, t93);
                uint64_t t94 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                GM_ST(uint64_t, (uint8_t *)&t89 + 16, t94);
                gm_tabb710d9mq2e t95;
                gm_f_format_n6tblha((void *)&t95, (void *)NULL, cx, NULL, &t89);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t95))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t95) + 8, gm_srcmhupam4se3h5, 306, 29);
                gm_b16_8 t96 = GM_LD(gm_b16_8, (((uint8_t *)&t95) + 8));
                gm_b16_8 t97 = GM_LD(gm_b16_8, ((uint8_t *)&t96));
                gm_b0_1 t98 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t86, t88, t97);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                gm_tqu3m2l3n8mnc t99;
                memset(&t99, 0, sizeof t99);
                uint8_t t100 = 0;
                F.ret = t99;
                goto gm_fret;
            }
            goto ix77;
ie77:;
ix77:;
        }
        {
            gm_f_list_vmvl81b((void *)&F.l13, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            {
                gm_b16_8 t102 = gm_l7;
                gm_b16_8 t103 = GM_LD(gm_b16_8, ((uint8_t *)&t102));
                gm_l14 = t103;
            }
            {
                gm_l15 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t105 = gm_l15;
                    uint64_t t106 = GM_LD(uintptr_t, ((uint8_t *)&gm_l14) + 8);
                    uint8_t t107 = (t105 < t106);
                    if (!t107) goto lb104;
                    {
                        uint64_t t110 = gm_l15;
                        uint64_t t111 = gm_l15;
                        uint64_t t112 = (uintptr_t)t111;
                        uint64_t t113 = GM_LD(uintptr_t, ((uint8_t *)&gm_l14) + 8);
                        uint32_t t114 = GM_LD(uint32_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l14)) + t112 * 4));
                        gm_tn68el8d14vku t115;
                        GM_ST(uint32_t, (uint8_t *)&t115 + 12, 0);
                        GM_ST(uint64_t, ((uint8_t *)&t115 + 0), t110);
                        GM_ST(uint32_t, ((uint8_t *)&t115 + 8), t114);
                        uint64_t t118 = GM_LD(uint64_t, (((uint8_t *)&t115) + 0));
                        gm_l16 = t118;
                        uint32_t t119 = GM_LD(uint32_t, (((uint8_t *)&t115) + 8));
                        gm_l17 = t119;
                        {
                            uint64_t t122 = gm_l16;
                            uint64_t t123 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                            uint8_t t124 = (t122 >= t123);
                            if (!t124) goto ie121;
                            {
                                void * t126 = (void *)((uint8_t *)&F.l13);
                                void * t127 = gm_l0;
                                void * t128 = (void *)(((uint8_t *)t127) + 8);
                                gm_tnpifv20irnph t129 = gm_f_empty_effects_r0iv3pv();
                                gm_t53at80pfdus3 t130;
                                memset(&t130, 0, sizeof t130);
                                GM_ST(uint8_t, ((uint8_t *)&t130 + 0), ((uint8_t)24ULL));
                                GM_ST(gm_tnpifv20irnph, ((uint8_t *)&t130 + 8), t129);
                                gm_t88rcucg1b7uk t131 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t128, &t130);
                                if (gm_unw.kind) {
                                    goto gm_ll108;
                                }
                                gm_toa2vhe3l4hfr t132;
                                memset(&t132, 0, sizeof t132);
                                GM_ST(uint8_t, ((uint8_t *)&t132 + 0), ((uint8_t)0ULL));
                                GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t132 + 8), t131);
                                gm_tftb1odc739t5 t133 = gm_f_push_s5vr2a2(t126, &t132);
                                if (((((uint8_t *)&t133))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t133) + 1, gm_srcmhupam4se3h5, 312, 17);
                                gm_b0_1 t134 = GM_LD(gm_b0_1, (((uint8_t *)&t133) + 1));
                            }
                            {
                                goto lc104;
                            }
                            goto ix121;
ie121:;
ix121:;
                        }
                        {
                            uint64_t t135 = gm_l16;
                            uint64_t t136 = (uintptr_t)t135;
                            uint64_t t137 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                            if (t136 >= (uintptr_t)(t137)) gm_trap_index(t136, t137, gm_srcmhupam4se3h5, 315, 19);
                            void * t138 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l5)) + t136 * 144);
                            gm_l18 = t138;
                        }
                        {
                            void * t140 = gm_l18;
                            uint8_t t141 = GM_LD(uint8_t, ((((uint8_t *)t140) + 0) + 0));
                            if (t141 != ((uint8_t)0ULL)) goto ie139;
                            gm_tc3oht4ksth9d t142 = GM_LD(gm_tc3oht4ksth9d, ((((uint8_t *)t140) + 0) + 8));
                            F.l19 = t142;
                            {
                                uint32_t t145 = gm_l17;
                                uint64_t t146 = (uint64_t)t145;
                                uint64_t t147 = (uintptr_t)t146;
                                void * t148 = gm_l0;
                                uint64_t t149 = GM_LD(uintptr_t, ((((uint8_t *)t148) + 1128) + 0) + 8);
                                if (t147 >= (uintptr_t)(t149)) gm_trap_index(t147, t149, gm_srcmhupam4se3h5, 320, 16);
                                uint8_t t150 = ((GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t148) + 1128) + 0)) + t147 * 48) + 0) + 0) == ((uint8_t)1ULL)));
                                if (!t150) goto ie144;
                                {
                                    void * t152 = (void *)((uint8_t *)&F.l13);
                                    void * t153 = gm_l0;
                                    uint64_t t154 = gm_l1;
                                    void * t155 = (void *)((uint8_t *)&F.l19);
                                    gm_t88rcucg1b7uk t156 = gm_f_resolve_effect_arg_4h6q4qv((void *)NULL, cx, NULL, t153, t154, t155);
                                    if (gm_unw.kind) {
                                        goto gm_ll108;
                                    }
                                    gm_toa2vhe3l4hfr t157;
                                    memset(&t157, 0, sizeof t157);
                                    GM_ST(uint8_t, ((uint8_t *)&t157 + 0), ((uint8_t)0ULL));
                                    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t157 + 8), t156);
                                    gm_tftb1odc739t5 t158 = gm_f_push_s5vr2a2(t152, &t157);
                                    if (((((uint8_t *)&t158))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t158) + 1, gm_srcmhupam4se3h5, 321, 17);
                                    gm_b0_1 t159 = GM_LD(gm_b0_1, (((uint8_t *)&t158) + 1));
                                }
                                goto ix144;
ie144:;
                                {
                                    void * t161 = (void *)((uint8_t *)&F.l13);
                                    void * t162 = gm_l0;
                                    uint64_t t163 = gm_l1;
                                    void * t164 = (void *)((uint8_t *)&F.l19);
                                    gm_t88rcucg1b7uk t165 = gm_f_resolve_type_jjodbei((void *)NULL, cx, NULL, t162, t163, t164);
                                    if (gm_unw.kind) {
                                        goto gm_ll108;
                                    }
                                    gm_toa2vhe3l4hfr t166;
                                    memset(&t166, 0, sizeof t166);
                                    GM_ST(uint8_t, ((uint8_t *)&t166 + 0), ((uint8_t)0ULL));
                                    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t166 + 8), t165);
                                    gm_tftb1odc739t5 t167 = gm_f_push_s5vr2a2(t161, &t166);
                                    if (((((uint8_t *)&t167))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t167) + 1, gm_srcmhupam4se3h5, 323, 17);
                                    gm_b0_1 t168 = GM_LD(gm_b0_1, (((uint8_t *)&t167) + 1));
                                }
ix144:;
                            }
                            goto ix139;
ie139:;
                            {
                                {
                                    void * t171 = gm_l0;
                                    void * t172 = gm_l18;
                                    gm_tl874t2atnlh3 t173 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t172) + 128));
                                    gm_b16_8 t174;
                                    GM_ST(void *, (uint8_t *)&t174, (void *)(gm_spskd7qirpoe3));
                                    GM_ST(uintptr_t, (uint8_t *)&t174 + 8, (uintptr_t)(24));
                                    gm_b0_1 t175 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t171, t173, t174);
                                    if (gm_unw.kind) {
                                        goto gm_ll108;
                                    }
                                }
                                {
                                    gm_tqu3m2l3n8mnc t176;
                                    memset(&t176, 0, sizeof t176);
                                    uint8_t t177 = 0;
                                    F.ret = t176;
                                    goto gm_fret;
                                }
                            }
ix139:;
                        }
                        goto me116;
mn117:;
                        gm_trap("no match arm matched the scrutinee", gm_srcmhupam4se3h5, 310, 9);
me116:;
                    }
lc104:;
                    {
                        uint64_t t179 = gm_l15;
                        uint64_t r180; bool o181 = __builtin_add_overflow(t179, ((uint64_t)1ULL), &r180);
                        uint64_t t182 = r180;
                        gm_l15 = t182;
                    }
                }
gm_ll108:;
                if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb104; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc104; }
                }
                goto gm_lad;
lb104:;
            }
        }
        {
            gm_tiilmfd16v3fc t183;
            memset((uint8_t *)&t183 + 4, 0, 4);
            uint32_t t184 = GM_LD(uint32_t, (((uint8_t *)&gm_l6) + 0));
            GM_ST(uint32_t, (uint8_t *)&t183 + 0, t184);
            gm_b16_8 t185 = GM_LD(gm_b16_8, (((uint8_t *)&F.l13) + 0));
            gm_b16_8 t186 = GM_LD(gm_b16_8, ((uint8_t *)&t185));
            GM_ST(gm_b16_8, (uint8_t *)&t183 + 8, t186);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tqu3m2l3n8mnc *)gm_out) + 25, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tqu3m2l3n8mnc *)gm_out) + 26, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tqu3m2l3n8mnc *)gm_out) + 28, 0);
            ((uint8_t *)&(*(gm_tqu3m2l3n8mnc *)gm_out))[24] = 1;
            GM_ST(gm_tiilmfd16v3fc, (uint8_t *)&(*(gm_tqu3m2l3n8mnc *)gm_out) + 0, t183);
            goto gm_fdone;
        }
        goto ix21;
ie21:;
ix21:;
    }
    {
        void * t188 = gm_l2;
        uint8_t t189 = GM_LD(uint8_t, ((((uint8_t *)t188) + 8) + 0));
        if (t189 != ((uint8_t)6ULL)) goto ie187;
        gm_t08olq97rscn3 t190 = GM_LD(gm_t08olq97rscn3, ((((uint8_t *)t188) + 8) + 8));
        gm_l20 = t190;
        {
            void * t192 = gm_l0;
            uint64_t t193 = gm_l1;
            gm_b16_8 t194 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l20) + 0));
            gm_tf5srh4mp4b3i t195 = gm_f_lookup_module_name_cukfp7t((void *)NULL, cx, NULL, t192, t193, t194);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint64_t t196;
            if (((((uint8_t *)&t195))[8] != 0)) {
                uint64_t t197 = GM_LD(uint64_t, (((uint8_t *)&t195) + 0));
                t196 = t197;
            } else {
                {
                    void * t199 = gm_l0;
                    void * t200 = gm_l2;
                    gm_tl874t2atnlh3 t201 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t200) + 72));
                    gm_b16_8 t202 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l20) + 0));
                    gm_tnpifv20irnph t203;
                    GM_ST(gm_b16_8, ((uint8_t *)&t203 + 0), t202);
                    gm_tabb710d9mq2e t204;
                    gm_f_format_kjp7rmf((void *)&t204, (void *)NULL, cx, NULL, t203);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    if (((((uint8_t *)&t204))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t204) + 8, gm_srcmhupam4se3h5, 330, 29);
                    gm_b16_8 t205 = GM_LD(gm_b16_8, (((uint8_t *)&t204) + 8));
                    gm_b16_8 t206 = GM_LD(gm_b16_8, ((uint8_t *)&t205));
                    gm_b0_1 t207 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t199, t201, t206);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                gm_tqu3m2l3n8mnc t208;
                memset(&t208, 0, sizeof t208);
                uint8_t t209 = 0;
                F.ret = t208;
                goto gm_fret;
            }
            gm_l21 = t196;
        }
        {
            void * t210 = gm_l0;
            uint64_t t211 = gm_l21;
            gm_t6qooom8qa2do t212;
            gm_f_resolve_decl_pus042v((void *)&t212, (void *)NULL, cx, NULL, t210, t211);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t220 = GM_LD(uint8_t, (((uint8_t *)&t212) + 0));
            switch (t220) { case ((uint8_t)0ULL): goto ma218; case ((uint8_t)1ULL): goto ma218; case ((uint8_t)2ULL): goto ma214; case ((uint8_t)3ULL): goto ma215; case ((uint8_t)4ULL): goto ma217; case ((uint8_t)5ULL): goto ma218; case ((uint8_t)6ULL): goto ma218; case ((uint8_t)7ULL): goto ma218; case ((uint8_t)8ULL): goto ma218; case ((uint8_t)9ULL): goto ma218; case ((uint8_t)10ULL): goto ma218; case ((uint8_t)11ULL): goto ma216; default: goto ma218; }
ma214:;
            uint8_t t222 = GM_LD(uint8_t, (((uint8_t *)&t212) + 0));
            if (t222 != ((uint8_t)2ULL)) goto mn221;
            uint32_t t223 = GM_LD(uint32_t, (((uint8_t *)&t212) + 8));
            gm_l22 = t223;
            void * t224 = gm_l0;
            uint32_t t225 = gm_l22;
            gm_tiilmfd16v3fc t226;
            gm_f_bare_effect_ref_a5ofmji((void *)&t226, (void *)NULL, cx, NULL, t224, t225);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tqu3m2l3n8mnc *)gm_out) + 25, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tqu3m2l3n8mnc *)gm_out) + 26, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tqu3m2l3n8mnc *)gm_out) + 28, 0);
            ((uint8_t *)&(*(gm_tqu3m2l3n8mnc *)gm_out))[24] = 1;
            GM_ST(gm_tiilmfd16v3fc, (uint8_t *)&(*(gm_tqu3m2l3n8mnc *)gm_out) + 0, t226);
            goto gm_fdone;
            goto me213;
mn221:;
ma215:;
            uint8_t t228 = GM_LD(uint8_t, (((uint8_t *)&t212) + 0));
            if (t228 != ((uint8_t)3ULL)) goto mn227;
            gm_l23 = (*(gm_tiilmfd16v3fc *)(((uint8_t *)&t212) + 8));
            const uint8_t *t229 = ((uint8_t *)&gm_l23);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tqu3m2l3n8mnc *)gm_out) + 25, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tqu3m2l3n8mnc *)gm_out) + 26, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tqu3m2l3n8mnc *)gm_out) + 28, 0);
            ((uint8_t *)&(*(gm_tqu3m2l3n8mnc *)gm_out))[24] = 1;
            memmove((uint8_t *)&(*(gm_tqu3m2l3n8mnc *)gm_out) + 0, t229, 24);
            goto gm_fdone;
            goto me213;
mn227:;
ma216:;
            uint8_t t231 = GM_LD(uint8_t, (((uint8_t *)&t212) + 0));
            if (t231 != ((uint8_t)11ULL)) goto mn230;
            gm_tqu3m2l3n8mnc t232;
            memset(&t232, 0, sizeof t232);
            uint8_t t233 = 0;
            F.ret = t232;
            goto gm_fret;
            goto me213;
mn230:;
ma217:;
            uint8_t t235 = GM_LD(uint8_t, (((uint8_t *)&t212) + 0));
            if (t235 != ((uint8_t)4ULL)) goto mn234;
            {
                void * t237 = gm_l0;
                void * t238 = gm_l2;
                gm_tl874t2atnlh3 t239 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t238) + 72));
                gm_b16_8 t240 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l20) + 0));
                gm_tnpifv20irnph t241;
                GM_ST(gm_b16_8, ((uint8_t *)&t241 + 0), t240);
                gm_tabb710d9mq2e t242;
                gm_f_format_37dbjte((void *)&t242, (void *)NULL, cx, NULL, t241);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t242))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t242) + 8, gm_srcmhupam4se3h5, 338, 33);
                gm_b16_8 t243 = GM_LD(gm_b16_8, (((uint8_t *)&t242) + 8));
                gm_b16_8 t244 = GM_LD(gm_b16_8, ((uint8_t *)&t243));
                gm_b0_1 t245 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t237, t239, t244);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                gm_tqu3m2l3n8mnc t246;
                memset(&t246, 0, sizeof t246);
                uint8_t t247 = 0;
                F.ret = t246;
                goto gm_fret;
            }
            goto me213;
mn234:;
ma218:;
            {
                void * t250 = gm_l0;
                void * t251 = gm_l2;
                gm_tl874t2atnlh3 t252 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t251) + 72));
                gm_b16_8 t253 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l20) + 0));
                gm_tnpifv20irnph t254;
                GM_ST(gm_b16_8, ((uint8_t *)&t254 + 0), t253);
                gm_tabb710d9mq2e t255;
                gm_f_format_vilpuor((void *)&t255, (void *)NULL, cx, NULL, t254);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t255))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t255) + 8, gm_srcmhupam4se3h5, 342, 33);
                gm_b16_8 t256 = GM_LD(gm_b16_8, (((uint8_t *)&t255) + 8));
                gm_b16_8 t257 = GM_LD(gm_b16_8, ((uint8_t *)&t256));
                gm_b0_1 t258 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t250, t252, t257);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                gm_tqu3m2l3n8mnc t259;
                memset(&t259, 0, sizeof t259);
                uint8_t t260 = 0;
                F.ret = t259;
                goto gm_fret;
            }
            goto me213;
mn248:;
ma219:;
            gm_trap("no match arm matched the scrutinee", gm_srcmhupam4se3h5, 333, 9);
me213:;
        }
        goto ix187;
ie187:;
ix187:;
    }
    {
        void * t262 = gm_l2;
        uint8_t t263 = GM_LD(uint8_t, ((((uint8_t *)t262) + 8) + 0));
        if (t263 != ((uint8_t)22ULL)) goto ie261;
        void * t264 = GM_LD(void *, (((((uint8_t *)t262) + 8) + 8) + 0));
        gm_l24 = t264;
        gm_t08olq97rscn3 t265 = GM_LD(gm_t08olq97rscn3, (((((uint8_t *)t262) + 8) + 8) + 8));
        gm_l25 = t265;
        {
            void * t268 = gm_l0;
            uint64_t t269 = gm_l1;
            void * t270 = gm_l24;
            gm_tf5srh4mp4b3i t271 = gm_f_module_named_n1mcl3b((void *)NULL, cx, NULL, t268, t269, t270);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (!((((uint8_t *)&t271))[8] != 0)) goto ie267;
            uint64_t t272 = GM_LD(uint64_t, ((uint8_t *)&t271));
            gm_l26 = t272;
            {
                void * t274 = gm_l0;
                uint64_t t275 = gm_l26;
                uint64_t t276 = gm_l1;
                gm_tf5srh4mp4b3i t277 = gm_f_module_member_5fd2qm9((void *)NULL, cx, NULL, t274, t275, &(*(gm_t08olq97rscn3 *)((uint8_t *)&gm_l25)), t276);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint64_t t278;
                if (((((uint8_t *)&t277))[8] != 0)) {
                    uint64_t t279 = GM_LD(uint64_t, (((uint8_t *)&t277) + 0));
                    t278 = t279;
                } else {
                    gm_tqu3m2l3n8mnc t280;
                    memset(&t280, 0, sizeof t280);
                    uint8_t t281 = 0;
                    F.ret = t280;
                    goto gm_fret;
                }
                gm_l27 = t278;
            }
            {
                void * t282 = gm_l0;
                uint64_t t283 = gm_l27;
                gm_t6qooom8qa2do t284;
                gm_f_resolve_decl_pus042v((void *)&t284, (void *)NULL, cx, NULL, t282, t283);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t291 = GM_LD(uint8_t, (((uint8_t *)&t284) + 0));
                switch (t291) { case ((uint8_t)0ULL): goto ma289; case ((uint8_t)1ULL): goto ma289; case ((uint8_t)2ULL): goto ma286; case ((uint8_t)3ULL): goto ma287; case ((uint8_t)4ULL): goto ma289; case ((uint8_t)5ULL): goto ma289; case ((uint8_t)6ULL): goto ma289; case ((uint8_t)7ULL): goto ma289; case ((uint8_t)8ULL): goto ma289; case ((uint8_t)9ULL): goto ma289; case ((uint8_t)10ULL): goto ma289; case ((uint8_t)11ULL): goto ma288; default: goto ma289; }
ma286:;
                uint8_t t293 = GM_LD(uint8_t, (((uint8_t *)&t284) + 0));
                if (t293 != ((uint8_t)2ULL)) goto mn292;
                uint32_t t294 = GM_LD(uint32_t, (((uint8_t *)&t284) + 8));
                gm_l28 = t294;
                void * t295 = gm_l0;
                uint32_t t296 = gm_l28;
                gm_tiilmfd16v3fc t297;
                gm_f_bare_effect_ref_a5ofmji((void *)&t297, (void *)NULL, cx, NULL, t295, t296);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tqu3m2l3n8mnc *)gm_out) + 25, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_tqu3m2l3n8mnc *)gm_out) + 26, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tqu3m2l3n8mnc *)gm_out) + 28, 0);
                ((uint8_t *)&(*(gm_tqu3m2l3n8mnc *)gm_out))[24] = 1;
                GM_ST(gm_tiilmfd16v3fc, (uint8_t *)&(*(gm_tqu3m2l3n8mnc *)gm_out) + 0, t297);
                goto gm_fdone;
                goto me285;
mn292:;
ma287:;
                uint8_t t299 = GM_LD(uint8_t, (((uint8_t *)&t284) + 0));
                if (t299 != ((uint8_t)3ULL)) goto mn298;
                gm_l29 = (*(gm_tiilmfd16v3fc *)(((uint8_t *)&t284) + 8));
                const uint8_t *t300 = ((uint8_t *)&gm_l29);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tqu3m2l3n8mnc *)gm_out) + 25, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_tqu3m2l3n8mnc *)gm_out) + 26, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tqu3m2l3n8mnc *)gm_out) + 28, 0);
                ((uint8_t *)&(*(gm_tqu3m2l3n8mnc *)gm_out))[24] = 1;
                memmove((uint8_t *)&(*(gm_tqu3m2l3n8mnc *)gm_out) + 0, t300, 24);
                goto gm_fdone;
                goto me285;
mn298:;
ma288:;
                uint8_t t302 = GM_LD(uint8_t, (((uint8_t *)&t284) + 0));
                if (t302 != ((uint8_t)11ULL)) goto mn301;
                gm_tqu3m2l3n8mnc t303;
                memset(&t303, 0, sizeof t303);
                uint8_t t304 = 0;
                F.ret = t303;
                goto gm_fret;
                goto me285;
mn301:;
ma289:;
                {
                    void * t307 = gm_l0;
                    void * t308 = gm_l2;
                    gm_tl874t2atnlh3 t309 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t308) + 72));
                    gm_b16_8 t310 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l25) + 0));
                    gm_tnpifv20irnph t311;
                    GM_ST(gm_b16_8, ((uint8_t *)&t311 + 0), t310);
                    gm_tabb710d9mq2e t312;
                    gm_f_format_vilpuor((void *)&t312, (void *)NULL, cx, NULL, t311);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    if (((((uint8_t *)&t312))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t312) + 8, gm_srcmhupam4se3h5, 356, 37);
                    gm_b16_8 t313 = GM_LD(gm_b16_8, (((uint8_t *)&t312) + 8));
                    gm_b16_8 t314 = GM_LD(gm_b16_8, ((uint8_t *)&t313));
                    gm_b0_1 t315 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t307, t309, t314);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                {
                    gm_tqu3m2l3n8mnc t316;
                    memset(&t316, 0, sizeof t316);
                    uint8_t t317 = 0;
                    F.ret = t316;
                    goto gm_fret;
                }
                goto me285;
mn305:;
ma290:;
                gm_trap("no match arm matched the scrutinee", gm_srcmhupam4se3h5, 351, 13);
me285:;
            }
            goto ix267;
ie267:;
ix267:;
        }
        {
            void * t319 = gm_l0;
            uint64_t t320 = gm_l1;
            void * t321 = gm_l24;
            uint8_t t322 = gm_f_names_error_oet8t6b((void *)NULL, cx, NULL, t319, t320, t321);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (!t322) goto ie318;
            {
                gm_tqu3m2l3n8mnc t324;
                memset(&t324, 0, sizeof t324);
                uint8_t t325 = 0;
                F.ret = t324;
                goto gm_fret;
            }
            goto ix318;
ie318:;
ix318:;
        }
        goto ix261;
ie261:;
ix261:;
    }
    {
        void * t326 = gm_l0;
        void * t327 = gm_l2;
        gm_tl874t2atnlh3 t328 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t327) + 72));
        gm_b16_8 t329;
        GM_ST(void *, (uint8_t *)&t329, (void *)(gm_sq5p8t05ccn5h));
        GM_ST(uintptr_t, (uint8_t *)&t329 + 8, (uintptr_t)(23));
        gm_b0_1 t330 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t326, t328, t329);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_tqu3m2l3n8mnc t331;
        memset(&t331, 0, sizeof t331);
        uint8_t t332 = 0;
        F.ret = t331;
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
inline __attribute__((aligned(4))) gm_tnpifv20irnph gm_f_resolve_effect_set_o7rc5od(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2) {
    struct gm_fr_resolve_effect_set_o7rc5od F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* module */
    gm_b16_8 gm_l2; /* terms */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    void * gm_l6; /* e */
    gm_t88rcucg1b7uk gm_l7; /* p */
    gm_tnpifv20irnph gm_l8; /* set */
    gm_tiilmfd16v3fc gm_l9; /* r */
    GM_STACK_CHECK(&F, 500u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        gm_f_list_084fcd6((void *)&F.l3, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t3 = gm_l2;
            gm_l4 = t3;
        }
        {
            gm_l5 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t5 = gm_l5;
                uint64_t t6 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                uint8_t t7 = (t5 < t6);
                if (!t7) goto lb4;
                {
                    uint64_t t10 = gm_l5;
                    uint64_t t11 = (uintptr_t)t10;
                    uint64_t t12 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                    void * t13 = GM_LD(void *, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t11 * 8));
                    void * t16 = GM_LD(void *, ((uint8_t *)&t13));
                    gm_l6 = t16;
                    {
                        void * t19 = gm_l0;
                        void * t20 = gm_l6;
                        gm_tkn42u7na3t81 t21 = gm_f_effect_param_named_h2jeoo7(t19, t20);
                        if (!((((uint8_t *)&t21))[4] != 0)) goto ie18;
                        gm_t88rcucg1b7uk t22 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&t21));
                        gm_l7 = t22;
                        {
                            void * t24 = (void *)((uint8_t *)&F.l3);
                            gm_t88rcucg1b7uk t25 = gm_l7;
                            gm_tiilmfd16v3fc t26;
                            gm_f_splice_tj8iedl((void *)&t26, (void *)NULL, cx, NULL, t25);
                            if (gm_unw.kind) {
                                goto gm_ll8;
                            }
                            gm_tftb1odc739t5 t27 = gm_f_push_d1r4h19(t24, &t26);
                            if (((((uint8_t *)&t27))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t27) + 1, gm_srcmhupam4se3h5, 239, 13);
                            gm_b0_1 t28 = GM_LD(gm_b0_1, (((uint8_t *)&t27) + 1));
                        }
                        {
                            goto lc4;
                        }
                        goto ix18;
ie18:;
ix18:;
                    }
                    {
                        void * t30 = gm_l0;
                        uint64_t t31 = gm_l1;
                        void * t32 = gm_l6;
                        gm_teseu4tn5o422 t33;
                        gm_f_named_effect_set_s5gn1lr((void *)&t33, (void *)NULL, cx, NULL, t30, t31, t32);
                        if (gm_unw.kind) {
                            goto gm_ll8;
                        }
                        if (!((((uint8_t *)&t33))[16] != 0)) goto ie29;
                        gm_tnpifv20irnph t34 = GM_LD(gm_tnpifv20irnph, ((uint8_t *)&t33));
                        gm_l8 = t34;
                        {
                            void * t36 = (void *)((uint8_t *)&F.l3);
                            gm_b16_8 t37 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l8) + 0));
                            gm_tftb1odc739t5 t38 = gm_f_extend_74ac4hb(t36, t37);
                            if (((((uint8_t *)&t38))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t38) + 1, gm_srcmhupam4se3h5, 243, 13);
                            gm_b0_1 t39 = GM_LD(gm_b0_1, (((uint8_t *)&t38) + 1));
                        }
                        {
                            goto lc4;
                        }
                        goto ix29;
ie29:;
ix29:;
                    }
                    {
                        void * t41 = gm_l0;
                        uint64_t t42 = gm_l1;
                        void * t43 = gm_l6;
                        gm_tqu3m2l3n8mnc t44;
                        gm_f_resolve_effect_ref_bggirs2((void *)&t44, (void *)NULL, cx, NULL, t41, t42, t43);
                        if (gm_unw.kind) {
                            goto gm_ll8;
                        }
                        if (!((((uint8_t *)&t44))[24] != 0)) goto ie40;
                        gm_l9 = (*(gm_tiilmfd16v3fc *)(((uint8_t *)&t44) + 0));
                        {
                            void * t46 = (void *)((uint8_t *)&F.l3);
                            gm_tftb1odc739t5 t47 = gm_f_push_d1r4h19(t46, &(*(gm_tiilmfd16v3fc *)((uint8_t *)&gm_l9)));
                            if (((((uint8_t *)&t47))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t47) + 1, gm_srcmhupam4se3h5, 246, 51);
                            gm_b0_1 t48 = GM_LD(gm_b0_1, (((uint8_t *)&t47) + 1));
                        }
                        goto ix40;
ie40:;
ix40:;
                    }
                    goto me14;
mn15:;
                    gm_trap("no match arm matched the scrutinee", gm_srcmhupam4se3h5, 237, 5);
me14:;
                }
lc4:;
                {
                    uint64_t t50 = gm_l5;
                    uint64_t r51; bool o52 = __builtin_add_overflow(t50, ((uint64_t)1ULL), &r51);
                    uint64_t t53 = r51;
                    gm_l5 = t53;
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
        gm_b16_8 t54 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
        gm_b16_8 t55 = GM_LD(gm_b16_8, ((uint8_t *)&t54));
        gm_tnpifv20irnph t56 = gm_f_effect_set_vtcpipr((void *)NULL, cx, NULL, t55);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t57 = 0;
        F.ret = t56;
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
__attribute__((aligned(4))) void gm_f_resolve_fn_sig_pamuhve(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2, uint8_t a3, gm_b16_8 a4) {
    struct gm_fr_resolve_fn_sig_pamuhve F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* module */
    void * gm_l2; /* sig */
    uint8_t gm_l3; /* literal */
    gm_b16_8 gm_l4; /* where_ */
    uint8_t gm_l5; /* ok */
    uint8_t gm_l8; /* seen_default */
    gm_t66j1im9ip128 gm_l9; /* mark */
    gm_b16_8 gm_l10; /* <tmp> */
    uint64_t gm_l11; /* <tmp> */
    void * gm_l13; /* ty_expr */
    gm_t88rcucg1b7uk gm_l14; /* ty */
    gm_b16_8 gm_l15; /* <tmp> */
    uint64_t gm_l16; /* <tmp> */
    gm_t08olq97rscn3 gm_l17; /* n */
    uint32_t gm_l18; /* pid */
    void * gm_l19; /* ty_expr */
    uint8_t gm_l20; /* is_effects */
    uint8_t gm_l21; /* is_type */
    gm_t08olq97rscn3 gm_l22; /* i */
    gm_b16_8 gm_l23; /* <tmp> */
    uint64_t gm_l24; /* <tmp> */
    gm_t08olq97rscn3 gm_l25; /* n */
    uint32_t gm_l26; /* pid */
    gm_t88rcucg1b7uk gm_l27; /* ty */
    uint8_t gm_l28; /* rest */
    gm_b16_8 gm_l29; /* names */
    uint8_t gm_l30; /* ok_ty */
    gm_b16_8 gm_l31; /* <tmp> */
    uint64_t gm_l32; /* <tmp> */
    gm_t08olq97rscn3 gm_l33; /* n */
    gm_b16_8 gm_l34; /* <tmp> */
    uint64_t gm_l35; /* <tmp> */
    uint64_t gm_l36; /* r */
    gm_b16_8 gm_l38; /* <tmp> */
    uint64_t gm_l39; /* <tmp> */
    gm_t88rcucg1b7uk gm_l41; /* ret */
    void * gm_l42; /* r */
    gm_tnpifv20irnph gm_l43; /* effects */
    gm_b16_8 gm_l44; /* list_ */
    gm_b16_8 gm_l45; /* <tmp> */
    uint64_t gm_l46; /* <tmp> */
    GM_STACK_CHECK(&F, 3870u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    {
        gm_l5 = ((uint8_t)1);
    }
    {
        gm_f_list_af74s0q((void *)&F.l6, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_f_list_go2qg0b((void *)&F.l7, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_l8 = ((uint8_t)0);
    }
    {
        void * t2 = gm_l0;
        void * t3 = gm_l0;
        uint32_t t4 = GM_LD(uint32_t, (((uint8_t *)t3) + 1768));
        uint32_t r5; bool o6 = __builtin_add_overflow(t4, ((uint32_t)1ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_srcmhupam4se3h5, 31, 5);
        uint32_t t7 = r5;
        GM_ST(uint32_t, (((uint8_t *)t2) + 1768), t7);
    }
    {
        void * t8 = gm_l0;
        gm_f_scope_mark_i3b2mvo((void *)&gm_l9, t8);
    }
    {
        {
            void * t10 = gm_l2;
            gm_b16_8 t11 = GM_LD(gm_b16_8, (((uint8_t *)t10) + 0));
            gm_l10 = t11;
        }
        {
            gm_l11 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t13 = gm_l11;
                uint64_t t14 = GM_LD(uintptr_t, ((uint8_t *)&gm_l10) + 8);
                uint8_t t15 = (t13 < t14);
                if (!t15) goto lb12;
                {
                    uint64_t t18 = gm_l11;
                    uint64_t t19 = (uintptr_t)t18;
                    uint64_t t20 = GM_LD(uintptr_t, ((uint8_t *)&gm_l10) + 8);
                    gm_tf2orqcrphmek t23 = GM_LD(gm_tf2orqcrphmek, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l10)) + t19 * 56));
                    F.l12 = t23;
                    {
                        uint8_t t26 = GM_LD(uint8_t, (((uint8_t *)&F.l12) + 16));
                        if (!t26) goto ie25;
                        {
                            uint8_t t29 = gm_l3;
                            uint8_t t30 = (uint8_t)!t29;
                            if (!t30) goto ie28;
                            {
                                void * t32 = gm_l0;
                                gm_tl874t2atnlh3 t33 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l12) + 40));
                                gm_b16_8 t34;
                                GM_ST(void *, (uint8_t *)&t34, (void *)(gm_sshv7blujc475));
                                GM_ST(uintptr_t, (uint8_t *)&t34 + 8, (uintptr_t)(51));
                                gm_b0_1 t35 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t32, t33, t34);
                                if (gm_unw.kind) {
                                    goto gm_ll16;
                                }
                            }
                            {
                                gm_l5 = ((uint8_t)0);
                            }
                            {
                                goto lc12;
                            }
                            goto ix28;
ie28:;
ix28:;
                        }
                        {
                            void * t36 = GM_LD(void *, (((uint8_t *)&F.l12) + 24));
                            void * t37;
                            if ((GM_LD(void *, ((uint8_t *)&t36)) != 0)) {
                                void * t38 = GM_LD(void *, (((uint8_t *)&t36) + 0));
                                t37 = t38;
                            } else {
                                {
                                    void * t40 = gm_l0;
                                    gm_tl874t2atnlh3 t41 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l12) + 40));
                                    gm_b16_8 t42;
                                    GM_ST(void *, (uint8_t *)&t42, (void *)(gm_snnov1h58rig5));
                                    GM_ST(uintptr_t, (uint8_t *)&t42 + 8, (uintptr_t)(51));
                                    gm_b0_1 t43 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t40, t41, t42);
                                    if (gm_unw.kind) {
                                        goto gm_ll16;
                                    }
                                }
                                {
                                    gm_l5 = ((uint8_t)0);
                                }
                                goto lc12;
                            }
                            gm_l13 = t37;
                        }
                        {
                            void * t44 = gm_l0;
                            uint64_t t45 = gm_l1;
                            void * t46 = gm_l13;
                            gm_t88rcucg1b7uk t47 = gm_f_resolve_type_jjodbei((void *)NULL, cx, NULL, t44, t45, t46);
                            if (gm_unw.kind) {
                                goto gm_ll16;
                            }
                            gm_l14 = t47;
                        }
                        {
                            void * t49 = gm_l0;
                            void * t50 = (void *)(((uint8_t *)t49) + 8);
                            gm_t88rcucg1b7uk t51 = gm_l14;
                            gm_t53at80pfdus3 t52;
                            gm_f_kind_jcnt4hm((void *)&t52, t50, GM_CONV(gm_csguankko4lmhd, t51));
                            uint8_t t53;
                            uint8_t t60 = GM_LD(uint8_t, (((uint8_t *)&t52) + 0));
                            switch (t60) { case ((uint8_t)0ULL): goto ma58; case ((uint8_t)1ULL): goto ma58; case ((uint8_t)2ULL): goto ma58; case ((uint8_t)3ULL): goto ma58; case ((uint8_t)4ULL): goto ma58; case ((uint8_t)5ULL): goto ma58; case ((uint8_t)6ULL): goto ma57; case ((uint8_t)7ULL): goto ma58; case ((uint8_t)8ULL): goto ma58; case ((uint8_t)9ULL): goto ma58; case ((uint8_t)10ULL): goto ma58; case ((uint8_t)11ULL): goto ma58; case ((uint8_t)12ULL): goto ma58; case ((uint8_t)13ULL): goto ma58; case ((uint8_t)14ULL): goto ma58; case ((uint8_t)15ULL): goto ma55; case ((uint8_t)16ULL): goto ma58; case ((uint8_t)17ULL): goto ma58; case ((uint8_t)18ULL): goto ma58; case ((uint8_t)19ULL): goto ma58; case ((uint8_t)20ULL): goto ma58; case ((uint8_t)21ULL): goto ma58; case ((uint8_t)22ULL): goto ma58; case ((uint8_t)23ULL): goto ma58; case ((uint8_t)24ULL): goto ma58; case ((uint8_t)25ULL): goto ma56; case ((uint8_t)26ULL): goto ma58; default: goto ma58; }
ma55:;
                            uint8_t t62 = GM_LD(uint8_t, (((uint8_t *)&t52) + 0));
                            if (t62 != ((uint8_t)15ULL)) goto mn61;
                            t53 = ((uint8_t)1);
                            goto me54;
mn61:;
ma56:;
                            uint8_t t64 = GM_LD(uint8_t, (((uint8_t *)&t52) + 0));
                            if (t64 != ((uint8_t)25ULL)) goto mn63;
                            t53 = ((uint8_t)1);
                            goto me54;
mn63:;
ma57:;
                            uint8_t t66 = GM_LD(uint8_t, (((uint8_t *)&t52) + 0));
                            if (t66 != ((uint8_t)6ULL)) goto mn65;
                            t53 = ((uint8_t)1);
                            goto me54;
mn65:;
ma58:;
                            t53 = ((uint8_t)0);
                            goto me54;
mn67:;
ma59:;
                            gm_trap("no match arm matched the scrutinee", gm_srcmhupam4se3h5, 47, 16);
me54:;
                            if (!t53) goto ie48;
                            {
                                void * t69 = gm_l0;
                                void * t70 = gm_l13;
                                gm_tl874t2atnlh3 t71 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t70) + 72));
                                void * t72 = gm_l0;
                                gm_t88rcucg1b7uk t73 = gm_l14;
                                gm_b16_8 t74 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t72, t73);
                                if (gm_unw.kind) {
                                    goto gm_ll16;
                                }
                                gm_tnpifv20irnph t75;
                                GM_ST(gm_b16_8, ((uint8_t *)&t75 + 0), t74);
                                gm_tabb710d9mq2e t76;
                                gm_f_format_askmr98((void *)&t76, (void *)NULL, cx, NULL, t75);
                                if (gm_unw.kind) {
                                    goto gm_ll16;
                                }
                                if (((((uint8_t *)&t76))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t76) + 8, gm_srcmhupam4se3h5, 48, 39);
                                gm_b16_8 t77 = GM_LD(gm_b16_8, (((uint8_t *)&t76) + 8));
                                gm_b16_8 t78 = GM_LD(gm_b16_8, ((uint8_t *)&t77));
                                gm_b0_1 t79 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t69, t71, t78);
                                if (gm_unw.kind) {
                                    goto gm_ll16;
                                }
                            }
                            {
                                gm_l5 = ((uint8_t)0);
                            }
                            {
                                goto lc12;
                            }
                            goto ix48;
ie48:;
ix48:;
                        }
                        {
                            {
                                gm_b16_8 t81 = GM_LD(gm_b16_8, (((uint8_t *)&F.l12) + 0));
                                gm_l15 = t81;
                            }
                            {
                                gm_l16 = ((uint64_t)0ULL);
                            }
                            {
                                for (;;) {
                                    uint64_t t83 = gm_l16;
                                    uint64_t t84 = GM_LD(uintptr_t, ((uint8_t *)&gm_l15) + 8);
                                    uint8_t t85 = (t83 < t84);
                                    if (!t85) goto lb82;
                                    {
                                        uint64_t t88 = gm_l16;
                                        uint64_t t89 = (uintptr_t)t88;
                                        uint64_t t90 = GM_LD(uintptr_t, ((uint8_t *)&gm_l15) + 8);
                                        gm_t08olq97rscn3 t93 = GM_LD(gm_t08olq97rscn3, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l15)) + t89 * 32));
                                        gm_l17 = t93;
                                        {
                                            void * t95 = gm_l0;
                                            gm_b16_8 t96 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l17) + 0));
                                            gm_t88rcucg1b7uk t97 = gm_l14;
                                            gm_tnnjeerolos0a t98;
                                            GM_ST(uint8_t, (uint8_t *)&t98 + 1, 0);
                                            GM_ST(uint16_t, (uint8_t *)&t98 + 2, 0);
                                            GM_ST(uint8_t, ((uint8_t *)&t98 + 0), ((uint8_t)2ULL));
                                            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t98 + 4), t97);
                                            uint32_t t99 = gm_f_new_param_7010dt4((void *)NULL, cx, NULL, t95, t96, t98);
                                            if (gm_unw.kind) {
                                                goto gm_ll86;
                                            }
                                            gm_l18 = t99;
                                        }
                                        {
                                            void * t100 = (void *)((uint8_t *)&F.l7);
                                            uint32_t t101 = gm_l18;
                                            gm_tftb1odc739t5 t102 = gm_f_push_im9d4ro(t100, t101);
                                            if (((((uint8_t *)&t102))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t102) + 1, gm_srcmhupam4se3h5, 54, 17);
                                            gm_b0_1 t103 = GM_LD(gm_b0_1, (((uint8_t *)&t102) + 1));
                                        }
                                        {
                                            void * t104 = gm_l0;
                                            void * t105 = (void *)(((uint8_t *)t104) + 1528);
                                            gm_tommf4j9upomc t106;
                                            memset((uint8_t *)&t106 + 20, 0, 4);
                                            gm_b16_8 t107 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l17) + 0));
                                            GM_ST(gm_b16_8, (uint8_t *)&t106 + 0, t107);
                                            uint32_t t108 = gm_l18;
                                            GM_ST(uint32_t, (uint8_t *)&t106 + 16, t108);
                                            gm_tftb1odc739t5 t109 = gm_f_push_sp4si0f(t105, &t106);
                                            if (((((uint8_t *)&t109))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t109) + 1, gm_srcmhupam4se3h5, 55, 17);
                                            gm_b0_1 t110 = GM_LD(gm_b0_1, (((uint8_t *)&t109) + 1));
                                        }
                                        {
                                            void * t111 = (void *)((uint8_t *)&F.l6);
                                            gm_t5v0c8ia6a5mo t112;
                                            memset((uint8_t *)&t112 + 49, 0, 7);
                                            gm_b16_8 t113 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l17) + 0));
                                            GM_ST(gm_b16_8, (uint8_t *)&t112 + 0, t113);
                                            gm_t88rcucg1b7uk t114 = gm_l14;
                                            GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t112 + 16, t114);
                                            gm_tl874t2atnlh3 t115 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l17) + 16));
                                            GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t112 + 20, t115);
                                            void * t116 = GM_LD(void *, (((uint8_t *)&F.l12) + 32));
                                            GM_ST(void *, (uint8_t *)&t112 + 32, t116);
                                            uint32_t t117 = gm_l18;
                                            gm_t9805qtonbsag t118;
                                            GM_ST(uint8_t, (uint8_t *)&t118 + 5, 0);
                                            GM_ST(uint16_t, (uint8_t *)&t118 + 6, 0);
                                            GM_ST(uint32_t, ((uint8_t *)&t118), t117);
                                            ((uint8_t *)&t118)[4] = 1;
                                            GM_ST(gm_t9805qtonbsag, (uint8_t *)&t112 + 40, t118);
                                            GM_ST(uint8_t, (uint8_t *)&t112 + 48, ((uint8_t)0));
                                            gm_tftb1odc739t5 t119 = gm_f_push_o8u01sh(t111, &t112);
                                            if (((((uint8_t *)&t119))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t119) + 1, gm_srcmhupam4se3h5, 56, 17);
                                            gm_b0_1 t120 = GM_LD(gm_b0_1, (((uint8_t *)&t119) + 1));
                                        }
                                        goto me91;
mn92:;
                                        gm_trap("no match arm matched the scrutinee", gm_srcmhupam4se3h5, 52, 13);
me91:;
                                    }
lc82:;
                                    {
                                        uint64_t t122 = gm_l16;
                                        uint64_t r123; bool o124 = __builtin_add_overflow(t122, ((uint64_t)1ULL), &r123);
                                        uint64_t t125 = r123;
                                        gm_l16 = t125;
                                    }
                                }
gm_ll86:;
                                if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb82; }
                                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc82; }
                                }
                                goto gm_ll16;
lb82:;
                            }
                        }
                        {
                            goto lc12;
                        }
                        goto ix25;
ie25:;
ix25:;
                    }
                    {
                        void * t126 = GM_LD(void *, (((uint8_t *)&F.l12) + 24));
                        void * t127;
                        if ((GM_LD(void *, ((uint8_t *)&t126)) != 0)) {
                            void * t128 = GM_LD(void *, (((uint8_t *)&t126) + 0));
                            t127 = t128;
                        } else {
                            {
                                void * t130 = gm_l0;
                                gm_tl874t2atnlh3 t131 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l12) + 40));
                                gm_b16_8 t132;
                                GM_ST(void *, (uint8_t *)&t132, (void *)(gm_spfda095eoqse));
                                GM_ST(uintptr_t, (uint8_t *)&t132 + 8, (uintptr_t)(33));
                                gm_b0_1 t133 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t130, t131, t132);
                                if (gm_unw.kind) {
                                    goto gm_ll16;
                                }
                            }
                            {
                                gm_l5 = ((uint8_t)0);
                            }
                            goto lc12;
                        }
                        gm_l19 = t127;
                    }
                    {
                        void * t134 = gm_l19;
                        uint8_t t135 = gm_f_is_effects_kind_uutbf9u(t134);
                        gm_l20 = t135;
                    }
                    {
                        gm_l21 = ((uint8_t)0);
                    }
                    {
                        void * t137 = gm_l19;
                        uint8_t t138 = GM_LD(uint8_t, ((((uint8_t *)t137) + 8) + 0));
                        if (t138 != ((uint8_t)6ULL)) goto ie136;
                        gm_t08olq97rscn3 t139 = GM_LD(gm_t08olq97rscn3, ((((uint8_t *)t137) + 8) + 8));
                        gm_l22 = t139;
                        {
                            uint8_t t141 = (((GM_LD(uintptr_t, (((uint8_t *)&gm_l22) + 0) + 8) == 4) && memcmp(GM_LD(void *, (((uint8_t *)&gm_l22) + 0)), gm_s2737i1iq57nf, 4) == 0));
                            gm_l21 = t141;
                        }
                        goto ix136;
ie136:;
ix136:;
                    }
                    {
                        uint8_t t143 = 0;
                        uint8_t t144 = gm_l20;
                        if (!t144) {
                            uint8_t t145 = gm_l21;
                            t143 = t145;
                        } else t143 = 1;
                        if (!t143) goto ie142;
                        {
                            uint8_t t148 = gm_l3;
                            uint8_t t149 = (uint8_t)!t148;
                            if (!t149) goto ie147;
                            {
                                void * t151 = gm_l0;
                                void * t152 = gm_l19;
                                gm_tl874t2atnlh3 t153 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t152) + 72));
                                gm_b16_8 t154;
                                GM_ST(void *, (uint8_t *)&t154, (void *)(gm_sa7gjgqof9s56));
                                GM_ST(uintptr_t, (uint8_t *)&t154 + 8, (uintptr_t)(43));
                                gm_b0_1 t155 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t151, t153, t154);
                                if (gm_unw.kind) {
                                    goto gm_ll16;
                                }
                            }
                            {
                                gm_l5 = ((uint8_t)0);
                            }
                            {
                                goto lc12;
                            }
                            goto ix147;
ie147:;
ix147:;
                        }
                        {
                            {
                                gm_b16_8 t157 = GM_LD(gm_b16_8, (((uint8_t *)&F.l12) + 0));
                                gm_l23 = t157;
                            }
                            {
                                gm_l24 = ((uint64_t)0ULL);
                            }
                            {
                                for (;;) {
                                    uint64_t t159 = gm_l24;
                                    uint64_t t160 = GM_LD(uintptr_t, ((uint8_t *)&gm_l23) + 8);
                                    uint8_t t161 = (t159 < t160);
                                    if (!t161) goto lb158;
                                    {
                                        uint64_t t164 = gm_l24;
                                        uint64_t t165 = (uintptr_t)t164;
                                        uint64_t t166 = GM_LD(uintptr_t, ((uint8_t *)&gm_l23) + 8);
                                        gm_t08olq97rscn3 t169 = GM_LD(gm_t08olq97rscn3, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l23)) + t165 * 32));
                                        gm_l25 = t169;
                                        {
                                            void * t171 = gm_l0;
                                            gm_b16_8 t172 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l25) + 0));
                                            gm_tnnjeerolos0a t173;
                                            uint8_t t175 = gm_l20;
                                            if (!t175) goto ie174;
                                            gm_tnnjeerolos0a t177;
                                            GM_ST(uint8_t, (uint8_t *)&t177 + 1, 0);
                                            GM_ST(uint16_t, (uint8_t *)&t177 + 2, 0);
                                            GM_ST(uint32_t, (uint8_t *)&t177 + 4, 0);
                                            GM_ST(uint8_t, ((uint8_t *)&t177 + 0), ((uint8_t)1ULL));
                                            gm_tnnjeerolos0a t178 = t177;
                                            t173 = t178;
                                            goto ix174;
ie174:;
                                            gm_tnnjeerolos0a t180;
                                            GM_ST(uint8_t, (uint8_t *)&t180 + 1, 0);
                                            GM_ST(uint16_t, (uint8_t *)&t180 + 2, 0);
                                            GM_ST(uint32_t, (uint8_t *)&t180 + 4, 0);
                                            GM_ST(uint8_t, ((uint8_t *)&t180 + 0), ((uint8_t)0ULL));
                                            gm_tnnjeerolos0a t181 = t180;
                                            t173 = t181;
ix174:;
                                            uint32_t t182 = gm_f_new_param_7010dt4((void *)NULL, cx, NULL, t171, t172, t173);
                                            if (gm_unw.kind) {
                                                goto gm_ll162;
                                            }
                                            gm_l26 = t182;
                                        }
                                        {
                                            void * t183 = (void *)((uint8_t *)&F.l7);
                                            uint32_t t184 = gm_l26;
                                            gm_tftb1odc739t5 t185 = gm_f_push_im9d4ro(t183, t184);
                                            if (((((uint8_t *)&t185))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t185) + 1, gm_srcmhupam4se3h5, 78, 17);
                                            gm_b0_1 t186 = GM_LD(gm_b0_1, (((uint8_t *)&t185) + 1));
                                        }
                                        {
                                            void * t187 = gm_l0;
                                            void * t188 = (void *)(((uint8_t *)t187) + 1488);
                                            gm_tep2drsl9408b t189;
                                            memset((uint8_t *)&t189 + 20, 0, 4);
                                            gm_b16_8 t190 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l25) + 0));
                                            GM_ST(gm_b16_8, (uint8_t *)&t189 + 0, t190);
                                            void * t191 = gm_l0;
                                            void * t192 = (void *)(((uint8_t *)t191) + 8);
                                            uint32_t t193 = gm_l26;
                                            gm_t88rcucg1b7uk t194 = gm_f_param_n8kgsdm((void *)NULL, cx, NULL, t192, t193);
                                            if (gm_unw.kind) {
                                                goto gm_ll162;
                                            }
                                            GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t189 + 16, t194);
                                            gm_tftb1odc739t5 t195 = gm_f_push_hv483nm(t188, &t189);
                                            if (((((uint8_t *)&t195))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t195) + 1, gm_srcmhupam4se3h5, 79, 17);
                                            gm_b0_1 t196 = GM_LD(gm_b0_1, (((uint8_t *)&t195) + 1));
                                        }
                                        {
                                            void * t197 = (void *)((uint8_t *)&F.l6);
                                            gm_t5v0c8ia6a5mo t198;
                                            memset((uint8_t *)&t198 + 49, 0, 7);
                                            gm_b16_8 t199 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l25) + 0));
                                            GM_ST(gm_b16_8, (uint8_t *)&t198 + 0, t199);
                                            void * t200 = gm_l0;
                                            void * t201 = (void *)(((uint8_t *)t200) + 8);
                                            gm_t88rcucg1b7uk t202 = gm_f_type_ty_9ph2gbr((void *)NULL, cx, NULL, t201);
                                            if (gm_unw.kind) {
                                                goto gm_ll162;
                                            }
                                            GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t198 + 16, t202);
                                            gm_tl874t2atnlh3 t203 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l25) + 16));
                                            GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t198 + 20, t203);
                                            GM_ST(void *, (uint8_t *)&t198 + 32, ((void *)0));
                                            uint32_t t204 = gm_l26;
                                            gm_t9805qtonbsag t205;
                                            GM_ST(uint8_t, (uint8_t *)&t205 + 5, 0);
                                            GM_ST(uint16_t, (uint8_t *)&t205 + 6, 0);
                                            GM_ST(uint32_t, ((uint8_t *)&t205), t204);
                                            ((uint8_t *)&t205)[4] = 1;
                                            GM_ST(gm_t9805qtonbsag, (uint8_t *)&t198 + 40, t205);
                                            GM_ST(uint8_t, (uint8_t *)&t198 + 48, ((uint8_t)0));
                                            gm_tftb1odc739t5 t206 = gm_f_push_o8u01sh(t197, &t198);
                                            if (((((uint8_t *)&t206))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t206) + 1, gm_srcmhupam4se3h5, 80, 17);
                                            gm_b0_1 t207 = GM_LD(gm_b0_1, (((uint8_t *)&t206) + 1));
                                        }
                                        goto me167;
mn168:;
                                        gm_trap("no match arm matched the scrutinee", gm_srcmhupam4se3h5, 76, 13);
me167:;
                                    }
lc158:;
                                    {
                                        uint64_t t209 = gm_l24;
                                        uint64_t r210; bool o211 = __builtin_add_overflow(t209, ((uint64_t)1ULL), &r210);
                                        uint64_t t212 = r210;
                                        gm_l24 = t212;
                                    }
                                }
gm_ll162:;
                                if (gm_unw.frame == &F.h && gm_unw.label == 2) {
                                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb158; }
                                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc158; }
                                }
                                goto gm_ll16;
lb158:;
                            }
                        }
                        {
                            goto lc12;
                        }
                        goto ix142;
ie142:;
ix142:;
                    }
                    {
                        void * t213 = gm_l0;
                        uint64_t t214 = gm_l1;
                        void * t215 = gm_l19;
                        gm_t88rcucg1b7uk t216 = gm_f_resolve_type_jjodbei((void *)NULL, cx, NULL, t213, t214, t215);
                        if (gm_unw.kind) {
                            goto gm_ll16;
                        }
                        gm_l27 = t216;
                    }
                    {
                        void * t218 = GM_LD(void *, (((uint8_t *)&F.l12) + 32));
                        uint8_t t219;
                        if ((GM_LD(void *, ((uint8_t *)&t218)) != 0)) goto mn221;
                        t219 = ((uint8_t)0);
                        goto me220;
mn221:;
                        t219 = ((uint8_t)1);
                        goto me220;
mn222:;
                        gm_trap("no match arm matched the scrutinee", gm_srcmhupam4se3h5, 85, 12);
me220:;
                        if (!t219) goto ie217;
                        {
                            gm_l8 = ((uint8_t)1);
                        }
                        goto ix217;
ie217:;
                        uint8_t t225 = gm_l8;
                        if (!t225) goto ie224;
                        uint8_t t226 = gm_l3;
                        if (!t226) goto ie224;
                        {
                            void * t228 = gm_l0;
                            gm_tl874t2atnlh3 t229 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l12) + 40));
                            gm_b16_8 t230;
                            GM_ST(void *, (uint8_t *)&t230, (void *)(gm_s0sivi9n1gpid));
                            GM_ST(uintptr_t, (uint8_t *)&t230 + 8, (uintptr_t)(73));
                            gm_b0_1 t231 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t228, t229, t230);
                            if (gm_unw.kind) {
                                goto gm_ll16;
                            }
                        }
                        goto ix224;
ie224:;
ix224:;
ix217:;
                    }
                    {
                        uint64_t t233 = GM_LD(uintptr_t, (((uint8_t *)&F.l12) + 0) + 8);
                        uint8_t t234 = ((t233 == ((uint64_t)0ULL)));
                        if (!t234) goto ie232;
                        {
                            void * t236 = (void *)((uint8_t *)&F.l6);
                            gm_t5v0c8ia6a5mo t237;
                            memset((uint8_t *)&t237 + 49, 0, 7);
                            gm_b16_8 t238;
                            GM_ST(void *, (uint8_t *)&t238, (void *)(gm_sfdbfrv46lihd));
                            GM_ST(uintptr_t, (uint8_t *)&t238 + 8, (uintptr_t)(1));
                            GM_ST(gm_b16_8, (uint8_t *)&t237 + 0, t238);
                            gm_t88rcucg1b7uk t239 = gm_l27;
                            GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t237 + 16, t239);
                            gm_tl874t2atnlh3 t240 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l12) + 40));
                            GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t237 + 20, t240);
                            GM_ST(void *, (uint8_t *)&t237 + 32, ((void *)0));
                            gm_t9805qtonbsag t241;
                            memset(&t241, 0, sizeof t241);
                            GM_ST(gm_t9805qtonbsag, (uint8_t *)&t237 + 40, t241);
                            GM_ST(uint8_t, (uint8_t *)&t237 + 48, ((uint8_t)0));
                            gm_tftb1odc739t5 t242 = gm_f_push_o8u01sh(t236, &t237);
                            if (((((uint8_t *)&t242))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t242) + 1, gm_srcmhupam4se3h5, 91, 13);
                            gm_b0_1 t243 = GM_LD(gm_b0_1, (((uint8_t *)&t242) + 1));
                        }
                        goto ix232;
ie232:;
ix232:;
                    }
                    {
                        gm_l28 = ((uint8_t)0);
                    }
                    {
                        uint8_t t245 = GM_LD(uint8_t, (((uint8_t *)&F.l12) + 17));
                        if (!t245) goto ie244;
                        {
                            void * t247 = gm_l0;
                            void * t248 = (void *)(((uint8_t *)t247) + 8);
                            gm_t88rcucg1b7uk t249 = gm_l27;
                            gm_t53at80pfdus3 t250;
                            gm_f_kind_jcnt4hm((void *)&t250, t248, GM_CONV(gm_csguankko4lmhd, t249));
                            uint8_t t251;
                            uint8_t t257 = GM_LD(uint8_t, (((uint8_t *)&t250) + 0));
                            switch (t257) { case ((uint8_t)0ULL): goto ma255; case ((uint8_t)1ULL): goto ma255; case ((uint8_t)2ULL): goto ma255; case ((uint8_t)3ULL): goto ma255; case ((uint8_t)4ULL): goto ma255; case ((uint8_t)5ULL): goto ma255; case ((uint8_t)6ULL): goto ma255; case ((uint8_t)7ULL): goto ma255; case ((uint8_t)8ULL): goto ma255; case ((uint8_t)9ULL): goto ma255; case ((uint8_t)10ULL): goto ma255; case ((uint8_t)11ULL): goto ma255; case ((uint8_t)12ULL): goto ma253; case ((uint8_t)13ULL): goto ma255; case ((uint8_t)14ULL): goto ma255; case ((uint8_t)15ULL): goto ma255; case ((uint8_t)16ULL): goto ma255; case ((uint8_t)17ULL): goto ma255; case ((uint8_t)18ULL): goto ma255; case ((uint8_t)19ULL): goto ma254; case ((uint8_t)20ULL): goto ma255; case ((uint8_t)21ULL): goto ma255; case ((uint8_t)22ULL): goto ma255; case ((uint8_t)23ULL): goto ma255; case ((uint8_t)24ULL): goto ma255; case ((uint8_t)25ULL): goto ma255; case ((uint8_t)26ULL): goto ma254; default: goto ma255; }
ma253:;
                            uint8_t t259 = GM_LD(uint8_t, (((uint8_t *)&t250) + 0));
                            if (t259 != ((uint8_t)12ULL)) goto mn258;
                            gm_b16_8 t260 = GM_LD(gm_b16_8, ((((uint8_t *)&t250) + 8) + 16));
                            gm_l29 = t260;
                            uint64_t t261 = GM_LD(uintptr_t, ((uint8_t *)&gm_l29) + 8);
                            uint8_t t262 = ((t261 == ((uint64_t)0ULL)));
                            t251 = t262;
                            goto me252;
mn258:;
ma254:;
                            uint8_t t266 = GM_LD(uint8_t, (((uint8_t *)&t250) + 0));
                            if (t266 != ((uint8_t)19ULL)) goto pa265;
                            goto po264;
pa265:;
                            uint8_t t268 = GM_LD(uint8_t, (((uint8_t *)&t250) + 0));
                            if (t268 != ((uint8_t)26ULL)) goto pa267;
                            goto po264;
pa267:;
                            goto mn263;
po264:;
                            t251 = ((uint8_t)1);
                            goto me252;
mn263:;
ma255:;
                            t251 = ((uint8_t)0);
                            goto me252;
mn269:;
ma256:;
                            gm_trap("no match arm matched the scrutinee", gm_srcmhupam4se3h5, 96, 22);
me252:;
                            gm_l30 = t251;
                        }
                        {
                            uint8_t t271 = gm_l3;
                            uint8_t t272 = (uint8_t)!t271;
                            if (!t272) goto ie270;
                            {
                                void * t274 = gm_l0;
                                gm_tl874t2atnlh3 t275 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l12) + 40));
                                gm_b16_8 t276;
                                GM_ST(void *, (uint8_t *)&t276, (void *)(gm_sfvsgjcg86bbl));
                                GM_ST(uintptr_t, (uint8_t *)&t276 + 8, (uintptr_t)(44));
                                gm_b0_1 t277 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t274, t275, t276);
                                if (gm_unw.kind) {
                                    goto gm_ll16;
                                }
                            }
                            {
                                gm_l5 = ((uint8_t)0);
                            }
                            goto ix270;
ie270:;
                            uint8_t t279 = 0;
                            uint8_t t280 = 0;
                            uint64_t t281 = GM_LD(uintptr_t, (((uint8_t *)&F.l12) + 0) + 8);
                            uint8_t t282 = (!(t281 == ((uint64_t)1ULL)));
                            if (!t282) {
                                void * t283 = GM_LD(void *, (((uint8_t *)&F.l12) + 32));
                                uint8_t t284;
                                if ((GM_LD(void *, ((uint8_t *)&t283)) != 0)) goto mn286;
                                t284 = ((uint8_t)0);
                                goto me285;
mn286:;
                                t284 = ((uint8_t)1);
                                goto me285;
mn287:;
                                gm_trap("no match arm matched the scrutinee", gm_srcmhupam4se3h5, 104, 47);
me285:;
                                t280 = t284;
                            } else t280 = 1;
                            if (!t280) {
                                uint8_t t288 = gm_l30;
                                uint8_t t289 = (uint8_t)!t288;
                                t279 = t289;
                            } else t279 = 1;
                            if (!t279) goto ie278;
                            {
                                void * t291 = gm_l0;
                                gm_tl874t2atnlh3 t292 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l12) + 40));
                                gm_b16_8 t293;
                                GM_ST(void *, (uint8_t *)&t293, (void *)(gm_s0ad3307s8jmo));
                                GM_ST(uintptr_t, (uint8_t *)&t293 + 8, (uintptr_t)(104));
                                gm_b0_1 t294 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t291, t292, t293);
                                if (gm_unw.kind) {
                                    goto gm_ll16;
                                }
                            }
                            {
                                gm_l5 = ((uint8_t)0);
                            }
                            goto ix278;
ie278:;
                            {
                                gm_l28 = ((uint8_t)1);
                            }
ix278:;
ix270:;
                        }
                        goto ix244;
ie244:;
ix244:;
                    }
                    {
                        {
                            gm_b16_8 t297 = GM_LD(gm_b16_8, (((uint8_t *)&F.l12) + 0));
                            gm_l31 = t297;
                        }
                        {
                            gm_l32 = ((uint64_t)0ULL);
                        }
                        {
                            for (;;) {
                                uint64_t t299 = gm_l32;
                                uint64_t t300 = GM_LD(uintptr_t, ((uint8_t *)&gm_l31) + 8);
                                uint8_t t301 = (t299 < t300);
                                if (!t301) goto lb298;
                                {
                                    uint64_t t304 = gm_l32;
                                    uint64_t t305 = (uintptr_t)t304;
                                    uint64_t t306 = GM_LD(uintptr_t, ((uint8_t *)&gm_l31) + 8);
                                    gm_t08olq97rscn3 t309 = GM_LD(gm_t08olq97rscn3, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l31)) + t305 * 32));
                                    gm_l33 = t309;
                                    {
                                        void * t311 = (void *)((uint8_t *)&F.l6);
                                        gm_t5v0c8ia6a5mo t312;
                                        memset((uint8_t *)&t312 + 49, 0, 7);
                                        gm_b16_8 t313 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l33) + 0));
                                        GM_ST(gm_b16_8, (uint8_t *)&t312 + 0, t313);
                                        gm_t88rcucg1b7uk t314 = gm_l27;
                                        GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t312 + 16, t314);
                                        gm_tl874t2atnlh3 t315 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l33) + 16));
                                        GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t312 + 20, t315);
                                        void * t316 = GM_LD(void *, (((uint8_t *)&F.l12) + 32));
                                        GM_ST(void *, (uint8_t *)&t312 + 32, t316);
                                        gm_t9805qtonbsag t317;
                                        memset(&t317, 0, sizeof t317);
                                        GM_ST(gm_t9805qtonbsag, (uint8_t *)&t312 + 40, t317);
                                        uint8_t t318 = gm_l28;
                                        GM_ST(uint8_t, (uint8_t *)&t312 + 48, t318);
                                        gm_tftb1odc739t5 t319 = gm_f_push_o8u01sh(t311, &t312);
                                        if (((((uint8_t *)&t319))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t319) + 1, gm_srcmhupam4se3h5, 112, 13);
                                        gm_b0_1 t320 = GM_LD(gm_b0_1, (((uint8_t *)&t319) + 1));
                                    }
                                    goto me307;
mn308:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcmhupam4se3h5, 111, 9);
me307:;
                                }
lc298:;
                                {
                                    uint64_t t322 = gm_l32;
                                    uint64_t r323; bool o324 = __builtin_add_overflow(t322, ((uint64_t)1ULL), &r323);
                                    uint64_t t325 = r323;
                                    gm_l32 = t325;
                                }
                            }
lb298:;
                        }
                    }
                    goto me21;
mn22:;
                    gm_trap("no match arm matched the scrutinee", gm_srcmhupam4se3h5, 33, 5);
me21:;
                }
lc12:;
                {
                    uint64_t t327 = gm_l11;
                    uint64_t r328; bool o329 = __builtin_add_overflow(t327, ((uint64_t)1ULL), &r328);
                    uint64_t t330 = r328;
                    gm_l11 = t330;
                }
            }
gm_ll16:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb12; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc12; }
            }
            goto gm_lad;
lb12:;
        }
    }
    {
        {
            gm_b16_8 t332 = GM_LD(gm_b16_8, (((uint8_t *)&F.l6) + 0));
            gm_b16_8 t333 = GM_LD(gm_b16_8, ((uint8_t *)&t332));
            gm_l34 = t333;
        }
        {
            gm_l35 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t335 = gm_l35;
                uint64_t t336 = GM_LD(uintptr_t, ((uint8_t *)&gm_l34) + 8);
                uint8_t t337 = (t335 < t336);
                if (!t337) goto lb334;
                {
                    gm_tgsvmddj3e85e t340;
                    uint64_t t341 = gm_l35;
                    GM_ST(uint64_t, (uint8_t *)&t340 + 0, t341);
                    uint64_t t342 = gm_l35;
                    uint64_t t343 = (uintptr_t)t342;
                    uint64_t t344 = GM_LD(uintptr_t, ((uint8_t *)&gm_l34) + 8);
                    memcpy((uint8_t *)&t340 + 8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l34)) + t343 * 56), 56);
                    uint64_t t347 = GM_LD(uint64_t, (((uint8_t *)&t340) + 0));
                    gm_l36 = t347;
                    F.l37 = (*(gm_t5v0c8ia6a5mo *)(((uint8_t *)&t340) + 8));
                    {
                        uint8_t t350 = GM_LD(uint8_t, (((uint8_t *)&F.l37) + 48));
                        uint8_t t351 = (uint8_t)!t350;
                        if (!t351) goto ie349;
                        {
                            goto lc334;
                        }
                        goto ix349;
ie349:;
ix349:;
                    }
                    {
                        {
                            uint64_t t354 = GM_LD(uintptr_t, (((uint8_t *)&F.l6) + 0) + 8);
                            void * t355 = ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l6) + 0)));
                            uint64_t t356 = gm_l36;
                            uint64_t r357; bool o358 = __builtin_add_overflow(t356, ((uint64_t)1ULL), &r357);
                            if (o358) gm_trap("integer overflow in `+`", gm_srcmhupam4se3h5, 118, 31);
                            uint64_t t359 = r357;
                            uint64_t t360 = (uintptr_t)t359;
                            if (t360 > (uintptr_t)t354 || (uintptr_t)t354 > (uintptr_t)t354) gm_trap_slice(t360, (uintptr_t)t354, t354, gm_srcmhupam4se3h5, 118, 18);
                            gm_b16_8 t361;
                            GM_ST(void *, (uint8_t *)&t361, (void *)(((uint8_t *)t355 + t360 * 56)));
                            GM_ST(uintptr_t, (uint8_t *)&t361 + 8, (uintptr_t)((uintptr_t)t354 - t360));
                            gm_l38 = t361;
                        }
                        {
                            gm_l39 = ((uint64_t)0ULL);
                        }
                        {
                            for (;;) {
                                uint64_t t363 = gm_l39;
                                uint64_t t364 = GM_LD(uintptr_t, ((uint8_t *)&gm_l38) + 8);
                                uint8_t t365 = (t363 < t364);
                                if (!t365) goto lb362;
                                {
                                    uint64_t t368 = gm_l39;
                                    uint64_t t369 = (uintptr_t)t368;
                                    uint64_t t370 = GM_LD(uintptr_t, ((uint8_t *)&gm_l38) + 8);
                                    gm_t5v0c8ia6a5mo t373 = GM_LD(gm_t5v0c8ia6a5mo, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l38)) + t369 * 56));
                                    F.l40 = t373;
                                    {
                                        uint8_t t376 = 0;
                                        uint8_t t377 = GM_LD(uint8_t, (((uint8_t *)&F.l40) + 48));
                                        if (!t377) {
                                            void * t378 = (void *)((uint8_t *)&F.l40);
                                            uint8_t t379 = gm_f_is_site_8sr7vam(t378);
                                            uint8_t t380 = (uint8_t)!t379;
                                            t376 = t380;
                                        } else t376 = 1;
                                        if (!t376) goto ie375;
                                        {
                                            void * t382 = gm_l0;
                                            gm_tl874t2atnlh3 t383 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l37) + 20));
                                            gm_b16_8 t384;
                                            GM_ST(void *, (uint8_t *)&t384, (void *)(gm_svurqcoqldl32));
                                            GM_ST(uintptr_t, (uint8_t *)&t384 + 8, (uintptr_t)(82));
                                            gm_b0_1 t385 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t382, t383, t384);
                                            if (gm_unw.kind) {
                                                goto gm_ll366;
                                            }
                                        }
                                        {
                                            gm_l5 = ((uint8_t)0);
                                        }
                                        {
                                            goto lb362;
                                        }
                                        goto ix375;
ie375:;
ix375:;
                                    }
                                    goto me371;
mn372:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcmhupam4se3h5, 118, 9);
me371:;
                                }
lc362:;
                                {
                                    uint64_t t387 = gm_l39;
                                    uint64_t r388; bool o389 = __builtin_add_overflow(t387, ((uint64_t)1ULL), &r388);
                                    uint64_t t390 = r388;
                                    gm_l39 = t390;
                                }
                            }
gm_ll366:;
                            if (gm_unw.frame == &F.h && gm_unw.label == 5) {
                                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb362; }
                                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc362; }
                            }
                            goto gm_ll338;
lb362:;
                        }
                    }
                    {
                        goto lb334;
                    }
                    goto me345;
mn346:;
                    gm_trap("no match arm matched the scrutinee", gm_srcmhupam4se3h5, 116, 5);
me345:;
                }
lc334:;
                {
                    uint64_t t392 = gm_l35;
                    uint64_t r393; bool o394 = __builtin_add_overflow(t392, ((uint64_t)1ULL), &r393);
                    uint64_t t395 = r393;
                    gm_l35 = t395;
                }
            }
gm_ll338:;
            if (gm_unw.frame == &F.h && gm_unw.label == 4) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb334; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc334; }
            }
            goto gm_lad;
lb334:;
        }
    }
    {
        void * t397 = gm_l2;
        uint8_t t398 = GM_LD(uint8_t, (((uint8_t *)t397) + 16));
        if (!t398) goto ie396;
        void * t399 = gm_l0;
        uint8_t t400 = GM_LD(uint8_t, (((uint8_t *)t399) + 1364));
        uint8_t t401 = (uint8_t)!t400;
        if (!t401) goto ie396;
        {
            void * t403 = gm_l0;
            void * t404 = gm_l2;
            gm_tl874t2atnlh3 t405 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t404) + 56));
            gm_b16_8 t406;
            GM_ST(void *, (uint8_t *)&t406, (void *)(gm_sba4cjt08ps1i));
            GM_ST(uintptr_t, (uint8_t *)&t406 + 8, (uintptr_t)(57));
            gm_b0_1 t407 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t403, t405, t406);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_l5 = ((uint8_t)0);
        }
        goto ix396;
ie396:;
ix396:;
    }
    {
        void * t408 = gm_l0;
        void * t409 = (void *)(((uint8_t *)t408) + 8);
        gm_t88rcucg1b7uk t410 = gm_f_unit_2age34u((void *)NULL, cx, NULL, t409);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l41 = t410;
    }
    {
        void * t412 = gm_l2;
        void * t413 = GM_LD(void *, (((uint8_t *)t412) + 24));
        if (!(GM_LD(void *, ((uint8_t *)&t413)) != 0)) goto ie411;
        void * t414 = GM_LD(void *, ((uint8_t *)&t413));
        gm_l42 = t414;
        {
            void * t416 = gm_l0;
            uint64_t t417 = gm_l1;
            void * t418 = gm_l42;
            gm_t88rcucg1b7uk t419 = gm_f_resolve_type_jjodbei((void *)NULL, cx, NULL, t416, t417, t418);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l41 = t419;
        }
        {
            void * t421 = gm_l0;
            void * t422 = (void *)(((uint8_t *)t421) + 8);
            gm_t88rcucg1b7uk t423 = gm_l41;
            gm_t53at80pfdus3 t424;
            gm_f_kind_jcnt4hm((void *)&t424, t422, GM_CONV(gm_csguankko4lmhd, t423));
            uint8_t t425;
            uint8_t t428 = GM_LD(uint8_t, (((uint8_t *)&t424) + 0));
            if (t428 != ((uint8_t)15ULL)) goto mn427;
            t425 = ((uint8_t)1);
            goto me426;
mn427:;
            t425 = ((uint8_t)0);
            goto me426;
mn429:;
            gm_trap("no match arm matched the scrutinee", gm_srcmhupam4se3h5, 134, 12);
me426:;
            if (!t425) goto ie420;
            {
                void * t431 = gm_l0;
                void * t432 = gm_l42;
                gm_tl874t2atnlh3 t433 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t432) + 72));
                gm_b16_8 t434;
                GM_ST(void *, (uint8_t *)&t434, (void *)(gm_sdsd71bjtv81c));
                GM_ST(uintptr_t, (uint8_t *)&t434 + 8, (uintptr_t)(97));
                gm_b0_1 t435 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t431, t433, t434);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix420;
ie420:;
ix420:;
        }
        goto ix411;
ie411:;
ix411:;
    }
    {
        gm_tnpifv20irnph t436 = gm_f_empty_effects_r0iv3pv();
        gm_l43 = t436;
    }
    {
        void * t438 = gm_l2;
        if (!(((((uint8_t *)t438) + 32))[16] != 0)) goto ie437;
        gm_b16_8 t439 = GM_LD(gm_b16_8, (((uint8_t *)t438) + 32));
        gm_l44 = t439;
        {
            void * t441 = gm_l0;
            uint64_t t442 = gm_l1;
            gm_b16_8 t443 = gm_l44;
            gm_b16_8 t444 = GM_LD(gm_b16_8, ((uint8_t *)&t443));
            gm_tnpifv20irnph t445 = gm_f_resolve_effect_list_uhjlh6o((void *)NULL, cx, NULL, t441, t442, t444);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l43 = t445;
        }
        goto ix437;
ie437:;
ix437:;
    }
    {
        {
            gm_b16_8 t447 = gm_l4;
            gm_l45 = t447;
        }
        {
            gm_l46 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t449 = gm_l46;
                uint64_t t450 = GM_LD(uintptr_t, ((uint8_t *)&gm_l45) + 8);
                uint8_t t451 = (t449 < t450);
                if (!t451) goto lb448;
                {
                    uint64_t t454 = gm_l46;
                    uint64_t t455 = (uintptr_t)t454;
                    uint64_t t456 = GM_LD(uintptr_t, ((uint8_t *)&gm_l45) + 8);
                    gm_tpv68m88f9lcr t459 = GM_LD(gm_tpv68m88f9lcr, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l45)) + t455 * 56));
                    F.l47 = t459;
                    {
                        void * t461 = gm_l0;
                        uint64_t t462 = gm_l1;
                        void * t463 = (void *)((uint8_t *)&F.l47);
                        gm_b0_1 t464 = gm_f_declare_constraint_s1juggf((void *)NULL, cx, NULL, t461, t462, t463);
                        if (gm_unw.kind) {
                            goto gm_ll452;
                        }
                    }
                    goto me457;
mn458:;
                    gm_trap("no match arm matched the scrutinee", gm_srcmhupam4se3h5, 140, 5);
me457:;
                }
lc448:;
                {
                    uint64_t t466 = gm_l46;
                    uint64_t r467; bool o468 = __builtin_add_overflow(t466, ((uint64_t)1ULL), &r467);
                    uint64_t t469 = r467;
                    gm_l46 = t469;
                }
            }
gm_ll452:;
            if (gm_unw.frame == &F.h && gm_unw.label == 6) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb448; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc448; }
            }
            goto gm_lad;
lb448:;
        }
    }
    {
        void * t470 = gm_l0;
        gm_b0_1 t471 = gm_f_end_constraint_defer_52jt3m9((void *)NULL, cx, NULL, t470);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        uint64_t t473 = GM_LD(uintptr_t, (((uint8_t *)&F.l7) + 0) + 8);
        uint8_t t474 = (t473 > ((uint64_t)0ULL));
        if (!t474) goto ie472;
        {
            void * t476 = gm_l0;
            void * t477 = gm_l2;
            gm_tl874t2atnlh3 t478 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t477) + 56));
            gm_b16_8 t479 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l43) + 0));
            gm_b0_1 t480 = gm_f_check_distinct_d4svq2h((void *)NULL, cx, NULL, t476, t478, t479);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix472;
ie472:;
ix472:;
    }
    {
        void * t481 = gm_l0;
        gm_f_restore_scope_edqq1v8(t481, &(*(gm_t66j1im9ip128 *)((uint8_t *)&gm_l9)));
    }
    {
        uint8_t t483 = gm_l5;
        uint8_t t484 = (uint8_t)!t483;
        if (!t484) goto ie482;
        {
            gm_tg93b272d11vo t486;
            memset(&t486, 0, sizeof t486);
            uint8_t t487 = 0;
            F.ret = t486;
            goto gm_fret;
        }
        goto ix482;
ie482:;
ix482:;
    }
    {
        gm_tqqhoivahirr0 t488;
        memset((uint8_t *)&t488 + 28, 0, 4);
        memset((uint8_t *)&t488 + 60, 0, 4);
        memset((uint8_t *)&t488 + 129, 0, 7);
        gm_b16_8 t489;
        GM_ST(void *, (uint8_t *)&t489, (void *)(gm_sr4qbrk6tlgaa));
        GM_ST(uintptr_t, (uint8_t *)&t489 + 8, (uintptr_t)(0));
        GM_ST(gm_b16_8, (uint8_t *)&t488 + 0, t489);
        void * t490 = gm_l2;
        gm_tl874t2atnlh3 t491 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t490) + 56));
        GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t488 + 16, t491);
        uint64_t t492 = gm_l1;
        GM_ST(uint64_t, (uint8_t *)&t488 + 32, t492);
        gm_b16_8 t493 = GM_LD(gm_b16_8, (((uint8_t *)&F.l6) + 0));
        GM_ST(gm_b16_8, (uint8_t *)&t488 + 40, t493);
        gm_t88rcucg1b7uk t494 = gm_l41;
        GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t488 + 56, t494);
        gm_tnpifv20irnph t495 = gm_l43;
        GM_ST(gm_tnpifv20irnph, (uint8_t *)&t488 + 64, t495);
        gm_b16_8 t496 = GM_LD(gm_b16_8, (((uint8_t *)&F.l7) + 0));
        GM_ST(gm_b16_8, (uint8_t *)&t488 + 80, t496);
        gm_tqu3m2l3n8mnc t497;
        memset(&t497, 0, sizeof t497);
        GM_ST(gm_tqu3m2l3n8mnc, (uint8_t *)&t488 + 96, t497);
        GM_ST(uint8_t, (uint8_t *)&t488 + 128, ((uint8_t)0));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tg93b272d11vo *)gm_out) + 137, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tg93b272d11vo *)gm_out) + 138, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tg93b272d11vo *)gm_out) + 140, 0);
        ((uint8_t *)&(*(gm_tg93b272d11vo *)gm_out))[136] = 1;
        GM_ST(gm_tqqhoivahirr0, (uint8_t *)&(*(gm_tg93b272d11vo *)gm_out) + 0, t488);
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
