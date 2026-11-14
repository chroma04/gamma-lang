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
typedef struct gm_cseahvg8v7p0vs { uint32_t f0; uint32_t f1; uint32_t f2; } gm_cseahvg8v7p0vs;
typedef struct gm_csguankko4lmhd { uint32_t f0; } gm_csguankko4lmhd;
typedef struct { uint64_t f0; uint64_t f1; } gm_t2lqu8jsj511d;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint32_t f0; uint8_t f1; uint8_t pe[3]; } gm_t9805qtonbsag;
typedef struct { unsigned __int128 f0; uint8_t f1; uint8_t pe[15]; } gm_t9p38b040grms;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { __int128 f0; uint8_t f1; uint8_t pe[15]; } gm_ta24rd13gupia;
typedef struct { uint8_t f0; uint8_t p1[7]; void * f1; void * f2; } gm_ta612fil812pm;
typedef struct { void * f0; void * f1; void * f2; uint8_t f3; uint8_t pe[7]; } gm_tao63pk3r18v5;
typedef struct { void * f0; uint32_t f1; uint8_t pe[4]; } gm_tbl8il92l97ut;
typedef struct { uint32_t f0; uint32_t f1; } gm_tcc1rq4n1avb2;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint8_t f0; uint8_t p1[1]; uint16_t f1; } gm_tjcj4j2gbh6c8;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { void * f0; void * f1; uint8_t f2; uint8_t pe[7]; } gm_tlcl4u558l6p5;
typedef struct { void * f0; void * f1; uint32_t f2; uint32_t f3; } gm_tmm67r2c94d5j;
typedef struct { uint8_t f0; uint8_t p1[3]; union { uint32_t v0; } f1; } gm_tmn7n0n9raou5;
typedef struct { uint8_t f0; uint8_t f1; uint8_t f2; } gm_toh0ootodhjue;
typedef struct { void * f0; uint8_t f1; uint8_t pe[7]; } gm_tsplfr33dsvji;
typedef struct { uint32_t f0; uint8_t p1[4]; void * f1; } gm_ttggq7ng4kp3p;
typedef struct { uint8_t f0; uint8_t p1[3]; uint32_t f1; } gm_ttovhfee8mf28;
_Static_assert(sizeof(gm_cseahvg8v7p0vs) == 12 && _Alignof(gm_cseahvg8v7p0vs) == 4 && offsetof(gm_cseahvg8v7p0vs, f0) == 0 && offsetof(gm_cseahvg8v7p0vs, f1) == 4 && offsetof(gm_cseahvg8v7p0vs, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_csguankko4lmhd) == 4 && _Alignof(gm_csguankko4lmhd) == 4 && offsetof(gm_csguankko4lmhd, f0) == 0, "layout of `Mutex`");
typedef struct { void * f0; gm_t9805qtonbsag f1; uint32_t f2; uint8_t pe[4]; } gm_t083kd7b1kkag;
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_t08olq97rscn3;
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; gm_t9qkh0rsooc8d f2; uint8_t p3[3]; gm_tf5srh4mp4b3i f3; } gm_t0mglksrkfa5e;
_Static_assert(sizeof(gm_t2lqu8jsj511d) == 16 && _Alignof(gm_t2lqu8jsj511d) == 8 && offsetof(gm_t2lqu8jsj511d, f0) == 0 && offsetof(gm_t2lqu8jsj511d, f1) == 8, "layout of `(u64, u64)`");
typedef struct { gm_b16_8 f0; uint64_t f1; } gm_t3877pk7lv748;
typedef struct { void * f0; gm_t9805qtonbsag f1; void * f2; } gm_t3dcoc7077t1q;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; uint8_t f2; uint8_t pe[3]; } gm_t3rcjibl8kkg4;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; void * f2; void * f3; } gm_t710lsrq1m8gk;
typedef struct { __int128 f0; gm_b16_8 f1; } gm_t7a4aqr549lb5;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; void * f1; } gm_t8ghgl2jj0qha;
typedef struct { gm_tmn7n0n9raou5 f0; void * f1; } gm_t8jsc4n9vp2ib;
_Static_assert(sizeof(gm_t9805qtonbsag) == 8 && _Alignof(gm_t9805qtonbsag) == 4 && offsetof(gm_t9805qtonbsag, f0) == 0 && offsetof(gm_t9805qtonbsag, f1) == 4, "layout of `?u32`");
_Static_assert(sizeof(gm_t9p38b040grms) == 32 && _Alignof(gm_t9p38b040grms) == 16 && offsetof(gm_t9p38b040grms, f0) == 0 && offsetof(gm_t9p38b040grms, f1) == 16, "layout of `?u128`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
_Static_assert(sizeof(gm_ta24rd13gupia) == 32 && _Alignof(gm_ta24rd13gupia) == 16 && offsetof(gm_ta24rd13gupia, f0) == 0 && offsetof(gm_ta24rd13gupia, f1) == 16, "layout of `?i128`");
_Static_assert(sizeof(gm_ta612fil812pm) == 24 && _Alignof(gm_ta612fil812pm) == 8 && offsetof(gm_ta612fil812pm, f0) == 0 && offsetof(gm_ta612fil812pm, f1) == 8 && offsetof(gm_ta612fil812pm, f2) == 16, "layout of `(is_and: bool, lhs: ^Expr, rhs: ^Expr)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
_Static_assert(sizeof(gm_tao63pk3r18v5) == 32 && _Alignof(gm_tao63pk3r18v5) == 8 && offsetof(gm_tao63pk3r18v5, f0) == 0 && offsetof(gm_tao63pk3r18v5, f1) == 8 && offsetof(gm_tao63pk3r18v5, f2) == 16 && offsetof(gm_tao63pk3r18v5, f3) == 24, "layout of `SliceExpr`");
typedef struct { gm_b16_8 f0; uint32_t f1; uint32_t f2; gm_b16_8 f3; } gm_tb40qn7hk0tl9;
typedef struct { uint64_t f0; uint64_t f1; gm_b16_8 f2; } gm_tbccvikg6b96e;
typedef struct { gm_tl874t2atnlh3 f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tbgd14esf0jvi;
_Static_assert(sizeof(gm_tbl8il92l97ut) == 16 && _Alignof(gm_tbl8il92l97ut) == 8 && offsetof(gm_tbl8il92l97ut, f0) == 0 && offsetof(gm_tbl8il92l97ut, f1) == 8, "layout of `SpecCall`");
typedef struct { uint8_t f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; } gm_tbngattl1fo51;
_Static_assert(sizeof(gm_tcc1rq4n1avb2) == 8 && _Alignof(gm_tcc1rq4n1avb2) == 4 && offsetof(gm_tcc1rq4n1avb2, f0) == 0 && offsetof(gm_tcc1rq4n1avb2, f1) == 4, "layout of `LineCol`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tcmkompbl09il;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; gm_t88rcucg1b7uk f2; } gm_tcmtrmhj7ak4u;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_tcc1rq4n1avb2 v0; } f1; } gm_te9hrk0u35rev;
typedef struct { uint8_t f0; union { gm_toh0ootodhjue v0; } f1; } gm_tesaltf0mhcki;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; } gm_tgglga8ro3isc;
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
_Static_assert(sizeof(gm_tjcj4j2gbh6c8) == 4 && _Alignof(gm_tjcj4j2gbh6c8) == 2 && offsetof(gm_tjcj4j2gbh6c8, f0) == 0 && offsetof(gm_tjcj4j2gbh6c8, f1) == 2, "layout of `(signed: bool, bits: u16)`");
typedef struct { void * f0; gm_b16_8 f1; } gm_tkaapcii86udk;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t f1; uint8_t pe[3]; } gm_tkn42u7na3t81;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_tlcl4u558l6p5) == 24 && _Alignof(gm_tlcl4u558l6p5) == 8 && offsetof(gm_tlcl4u558l6p5, f0) == 0 && offsetof(gm_tlcl4u558l6p5, f1) == 8 && offsetof(gm_tlcl4u558l6p5, f2) == 16, "layout of `(lo: ^Expr, hi: ^Expr, inclusive: bool)`");
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; uint8_t pe[4]; } gm_tlcm4lvu3g1a8;
_Static_assert(sizeof(gm_tmm67r2c94d5j) == 24 && _Alignof(gm_tmm67r2c94d5j) == 8 && offsetof(gm_tmm67r2c94d5j, f0) == 0 && offsetof(gm_tmm67r2c94d5j, f1) == 8 && offsetof(gm_tmm67r2c94d5j, f2) == 16 && offsetof(gm_tmm67r2c94d5j, f3) == 20, "layout of `Printer`");
_Static_assert(sizeof(gm_tmn7n0n9raou5) == 8 && _Alignof(gm_tmn7n0n9raou5) == 4 && offsetof(gm_tmn7n0n9raou5, f0) == 0 && offsetof(gm_tmn7n0n9raou5, f1) == 4, "layout of `Coercion`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t88rcucg1b7uk v0; } f1; } gm_tnnjeerolos0a;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
_Static_assert(sizeof(gm_toh0ootodhjue) == 3 && _Alignof(gm_toh0ootodhjue) == 1 && offsetof(gm_toh0ootodhjue, f0) == 0 && offsetof(gm_toh0ootodhjue, f1) == 1 && offsetof(gm_toh0ootodhjue, f2) == 2, "layout of `Restr`");
typedef struct { gm_b16_8 f0; unsigned __int128 f1; } gm_tou0girmhri5g;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; void * v1; } f1; } gm_tq7s98ihonkju;
typedef struct { gm_b16_8 f0; void * f1; void * f2; } gm_trl2khmuphncd;
typedef struct { gm_tl874t2atnlh3 f0; gm_t88rcucg1b7uk f1; } gm_trlabqsjjp0ds;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_t9qkh0rsooc8d v3; } f1; } gm_ts6oqppjh0ba3;
_Static_assert(sizeof(gm_tsplfr33dsvji) == 16 && _Alignof(gm_tsplfr33dsvji) == 8 && offsetof(gm_tsplfr33dsvji, f0) == 0 && offsetof(gm_tsplfr33dsvji, f1) == 8, "layout of `(expr: ^Expr, is_result: bool)`");
_Static_assert(sizeof(gm_ttggq7ng4kp3p) == 16 && _Alignof(gm_ttggq7ng4kp3p) == 8 && offsetof(gm_ttggq7ng4kp3p, f0) == 0 && offsetof(gm_ttggq7ng4kp3p, f1) == 8, "layout of `(index: u32, payload: ?^Value)`");
_Static_assert(sizeof(gm_ttovhfee8mf28) == 8 && _Alignof(gm_ttovhfee8mf28) == 4 && offsetof(gm_ttovhfee8mf28, f0) == 0 && offsetof(gm_ttovhfee8mf28, f1) == 4, "layout of `(signed: bool, bits: u32)`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_tcc1rq4n1avb2 v3; uint32_t v4; uint32_t v5; } f1; } gm_tvdg94bk7aa22;
_Static_assert(sizeof(gm_t083kd7b1kkag) == 24 && _Alignof(gm_t083kd7b1kkag) == 8 && offsetof(gm_t083kd7b1kkag, f0) == 0 && offsetof(gm_t083kd7b1kkag, f1) == 8 && offsetof(gm_t083kd7b1kkag, f2) == 16, "layout of `TryExpr`");
_Static_assert(sizeof(gm_t08olq97rscn3) == 32 && _Alignof(gm_t08olq97rscn3) == 8 && offsetof(gm_t08olq97rscn3, f0) == 0 && offsetof(gm_t08olq97rscn3, f1) == 16, "layout of `File`");
_Static_assert(sizeof(gm_t0mglksrkfa5e) == 40 && _Alignof(gm_t0mglksrkfa5e) == 8 && offsetof(gm_t0mglksrkfa5e, f0) == 0 && offsetof(gm_t0mglksrkfa5e, f1) == 16 && offsetof(gm_t0mglksrkfa5e, f2) == 20 && offsetof(gm_t0mglksrkfa5e, f3) == 24, "layout of `FieldDef`");
_Static_assert(sizeof(gm_t3877pk7lv748) == 24 && _Alignof(gm_t3877pk7lv748) == 8 && offsetof(gm_t3877pk7lv748, f0) == 0 && offsetof(gm_t3877pk7lv748, f1) == 16, "layout of `WasmReader`");
_Static_assert(sizeof(gm_t3dcoc7077t1q) == 24 && _Alignof(gm_t3dcoc7077t1q) == 8 && offsetof(gm_t3dcoc7077t1q, f0) == 0 && offsetof(gm_t3dcoc7077t1q, f1) == 8 && offsetof(gm_t3dcoc7077t1q, f2) == 16, "layout of `ElseExpr`");
_Static_assert(sizeof(gm_t3rcjibl8kkg4) == 12 && _Alignof(gm_t3rcjibl8kkg4) == 4 && offsetof(gm_t3rcjibl8kkg4, f0) == 0 && offsetof(gm_t3rcjibl8kkg4, f1) == 4 && offsetof(gm_t3rcjibl8kkg4, f2) == 8, "layout of `(a: Ty, b: Ty, negated: bool)`");
typedef struct { gm_tiilmfd16v3fc f0; uint32_t f1; uint8_t pe[4]; } gm_t419mo45g7j0a;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tcmkompbl09il v0; gm_tiilmfd16v3fc v1; gm_b16_8 v2; } f1; } gm_t428ue2u3hf5g;
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_t710lsrq1m8gk) == 40 && _Alignof(gm_t710lsrq1m8gk) == 8 && offsetof(gm_t710lsrq1m8gk, f0) == 0 && offsetof(gm_t710lsrq1m8gk, f1) == 8 && offsetof(gm_t710lsrq1m8gk, f2) == 24 && offsetof(gm_t710lsrq1m8gk, f3) == 32, "layout of `WithExpr`");
_Static_assert(sizeof(gm_t7a4aqr549lb5) == 32 && _Alignof(gm_t7a4aqr549lb5) == 16 && offsetof(gm_t7a4aqr549lb5, f0) == 0 && offsetof(gm_t7a4aqr549lb5, f1) == 16, "layout of `(i128, string)`");
_Static_assert(sizeof(gm_t8ghgl2jj0qha) == 16 && _Alignof(gm_t8ghgl2jj0qha) == 8 && offsetof(gm_t8ghgl2jj0qha, f0) == 0 && offsetof(gm_t8ghgl2jj0qha, f1) == 8, "layout of `(op: UnOp, operand: ^Expr)`");
_Static_assert(sizeof(gm_t8jsc4n9vp2ib) == 16 && _Alignof(gm_t8jsc4n9vp2ib) == 8 && offsetof(gm_t8jsc4n9vp2ib, f0) == 0 && offsetof(gm_t8jsc4n9vp2ib, f1) == 8, "layout of `(kind: Coercion, expr: ^Expr)`");
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; gm_tnnjeerolos0a f2; uint8_t pe[4]; } gm_t8nckeqfenl8a;
typedef struct { gm_tkaapcii86udk f0; uint8_t f1; uint8_t pe[7]; } gm_tab260m0n3sqj;
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
typedef struct { gm_tesaltf0mhcki f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tandvbt63uk7n;
_Static_assert(sizeof(gm_tb40qn7hk0tl9) == 40 && _Alignof(gm_tb40qn7hk0tl9) == 8 && offsetof(gm_tb40qn7hk0tl9, f0) == 0 && offsetof(gm_tb40qn7hk0tl9, f1) == 16 && offsetof(gm_tb40qn7hk0tl9, f2) == 20 && offsetof(gm_tb40qn7hk0tl9, f3) == 24, "layout of `SourceLocation`");
typedef struct { gm_tlcm4lvu3g1a8 f0; uint8_t f1; uint8_t pe[7]; } gm_tb7u19loo3j87;
_Static_assert(sizeof(gm_tbccvikg6b96e) == 32 && _Alignof(gm_tbccvikg6b96e) == 8 && offsetof(gm_tbccvikg6b96e, f0) == 0 && offsetof(gm_tbccvikg6b96e, f1) == 8 && offsetof(gm_tbccvikg6b96e, f2) == 16, "layout of `Layout`");
typedef struct { uint8_t f0; uint8_t p1[7]; gm_trqfvmjtt4sij f1; } gm_tbci5ospk5nnn;
_Static_assert(sizeof(gm_tbgd14esf0jvi) == 32 && _Alignof(gm_tbgd14esf0jvi) == 8 && offsetof(gm_tbgd14esf0jvi, f0) == 0 && offsetof(gm_tbgd14esf0jvi, f1) == 16, "layout of `Edit`");
_Static_assert(sizeof(gm_tbngattl1fo51) == 8 && _Alignof(gm_tbngattl1fo51) == 4 && offsetof(gm_tbngattl1fo51, f0) == 0 && offsetof(gm_tbngattl1fo51, f1) == 4, "layout of `(is_const: bool, pointee: Ty)`");
_Static_assert(sizeof(gm_tcmkompbl09il) == 24 && _Alignof(gm_tcmkompbl09il) == 8 && offsetof(gm_tcmkompbl09il, f0) == 0 && offsetof(gm_tcmkompbl09il, f1) == 8, "layout of `(ty: Ty, bytes: string)`");
_Static_assert(sizeof(gm_tcmtrmhj7ak4u) == 12 && _Alignof(gm_tcmtrmhj7ak4u) == 4 && offsetof(gm_tcmtrmhj7ak4u, f0) == 0 && offsetof(gm_tcmtrmhj7ak4u, f1) == 4 && offsetof(gm_tcmtrmhj7ak4u, f2) == 8, "layout of `(op: LenOp, lhs: Ty, rhs: Ty)`");
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t p2[4]; gm_tnpifv20irnph f2; uint8_t f3; uint8_t pe[7]; } gm_tddtjfggdat0d;
_Static_assert(sizeof(gm_te9hrk0u35rev) == 12 && _Alignof(gm_te9hrk0u35rev) == 4 && offsetof(gm_te9hrk0u35rev, f0) == 0 && offsetof(gm_te9hrk0u35rev, f1) == 4, "layout of `Target`");
typedef struct { gm_te9hrk0u35rev f0; uint8_t p1[4]; void * f1; } gm_teo9d4mncjfdu;
_Static_assert(sizeof(gm_tesaltf0mhcki) == 4 && _Alignof(gm_tesaltf0mhcki) == 1 && offsetof(gm_tesaltf0mhcki, f0) == 0 && offsetof(gm_tesaltf0mhcki, f1) == 1, "layout of `SoaOp`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_tgglga8ro3isc) == 8 && _Alignof(gm_tgglga8ro3isc) == 4 && offsetof(gm_tgglga8ro3isc, f0) == 0 && offsetof(gm_tgglga8ro3isc, f1) == 4, "layout of `(err: Ty, ok: Ty)`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
typedef struct { gm_tvdg94bk7aa22 f0; } gm_th9nl3oso747e;
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; gm_t88rcucg1b7uk f2; uint64_t f3; void * f4; gm_tkn42u7na3t81 f5; uint64_t f6; void * f7; gm_t9qkh0rsooc8d f8; uint8_t p9[7]; void * f9; } gm_tjcptv9sagfq8;
_Static_assert(sizeof(gm_tkaapcii86udk) == 24 && _Alignof(gm_tkaapcii86udk) == 8 && offsetof(gm_tkaapcii86udk, f0) == 0 && offsetof(gm_tkaapcii86udk, f1) == 8, "layout of `Place`");
_Static_assert(sizeof(gm_tkn42u7na3t81) == 8 && _Alignof(gm_tkn42u7na3t81) == 4 && offsetof(gm_tkn42u7na3t81, f0) == 0 && offsetof(gm_tkn42u7na3t81, f1) == 4, "layout of `?Ty`");
_Static_assert(sizeof(gm_tlcm4lvu3g1a8) == 16 && _Alignof(gm_tlcm4lvu3g1a8) == 8 && offsetof(gm_tlcm4lvu3g1a8, f0) == 0 && offsetof(gm_tlcm4lvu3g1a8, f1) == 8, "layout of `(i: usize, x: Ty)`");
_Static_assert(sizeof(gm_tnnjeerolos0a) == 8 && _Alignof(gm_tnnjeerolos0a) == 4 && offsetof(gm_tnnjeerolos0a, f0) == 0 && offsetof(gm_tnnjeerolos0a, f1) == 4, "layout of `ArrayKind`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
typedef struct { gm_tkaapcii86udk f0; uint64_t f1; } gm_to6glrduves6o;
_Static_assert(sizeof(gm_tou0girmhri5g) == 32 && _Alignof(gm_tou0girmhri5g) == 16 && offsetof(gm_tou0girmhri5g, f0) == 0 && offsetof(gm_tou0girmhri5g, f1) == 16, "layout of `([]u8, u128)`");
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t p2[7]; gm_tf5srh4mp4b3i f2; gm_tkn42u7na3t81 f3; } gm_tpautr4br7h3l;
_Static_assert(sizeof(gm_tq7s98ihonkju) == 16 && _Alignof(gm_tq7s98ihonkju) == 8 && offsetof(gm_tq7s98ihonkju, f0) == 0 && offsetof(gm_tq7s98ihonkju, f1) == 8, "layout of `AllocError!^HandlerDecl`");
_Static_assert(sizeof(gm_trl2khmuphncd) == 32 && _Alignof(gm_trl2khmuphncd) == 8 && offsetof(gm_trl2khmuphncd, f0) == 0 && offsetof(gm_trl2khmuphncd, f1) == 16 && offsetof(gm_trl2khmuphncd, f2) == 24, "layout of `IfExpr`");
typedef struct { gm_ts6oqppjh0ba3 f0; uint8_t p1[6]; void * f1; void * f2; } gm_trl9d7m1k5lbd;
_Static_assert(sizeof(gm_trlabqsjjp0ds) == 16 && _Alignof(gm_trlabqsjjp0ds) == 4 && offsetof(gm_trlabqsjjp0ds, f0) == 0 && offsetof(gm_trlabqsjjp0ds, f1) == 12, "layout of `(Span, Ty)`");
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_ts6oqppjh0ba3) == 2 && _Alignof(gm_ts6oqppjh0ba3) == 1 && offsetof(gm_ts6oqppjh0ba3, f0) == 0 && offsetof(gm_ts6oqppjh0ba3, f1) == 1, "layout of `BinOp`");
typedef struct { gm_tvdg94bk7aa22 f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tt8o5kebou62r;
typedef struct { gm_t6j6smj7qoctu f0; uint8_t f1; uint8_t pe[7]; } gm_ttu09lij36f8c;
_Static_assert(sizeof(gm_tvdg94bk7aa22) == 12 && _Alignof(gm_tvdg94bk7aa22) == 4 && offsetof(gm_tvdg94bk7aa22, f0) == 0 && offsetof(gm_tvdg94bk7aa22, f1) == 4, "layout of `Builtin`");
_Static_assert(sizeof(gm_t419mo45g7j0a) == 32 && _Alignof(gm_t419mo45g7j0a) == 8 && offsetof(gm_t419mo45g7j0a, f0) == 0 && offsetof(gm_t419mo45g7j0a, f1) == 24, "layout of `(effect_: EffectRef, op: u32)`");
_Static_assert(sizeof(gm_t428ue2u3hf5g) == 32 && _Alignof(gm_t428ue2u3hf5g) == 8 && offsetof(gm_t428ue2u3hf5g, f0) == 0 && offsetof(gm_t428ue2u3hf5g, f1) == 8, "layout of `ConstKey`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tjcj4j2gbh6c8 v0; uint16_t v1; gm_tbngattl1fo51 v2; gm_tbngattl1fo51 v3; gm_tbngattl1fo51 v4; gm_t8nckeqfenl8a v5; gm_t6j6smj7qoctu v6; gm_t88rcucg1b7uk v7; gm_tgglga8ro3isc v8; gm_tddtjfggdat0d v9; gm_tddtjfggdat0d v10; gm_tiilmfd16v3fc v11; gm_t88rcucg1b7uk v12; uint32_t v13; gm_tgglga8ro3isc v14; gm_t428ue2u3hf5g v15; gm_tcmtrmhj7ak4u v16; uint32_t v17; gm_tnpifv20irnph v18; uint32_t v19; } f1; } gm_t53at80pfdus3;
typedef struct { uint8_t f0; uint8_t p1[15]; union { unsigned __int128 v0; double v1; uint8_t v2; void * v3; gm_tkaapcii86udk v4; gm_to6glrduves6o v5; void * v6; void * v7; void * v8; gm_ttggq7ng4kp3p v9; uint32_t v10; gm_ttggq7ng4kp3p v11; gm_ttggq7ng4kp3p v12; } f1; } gm_t6evimt84lndq;
_Static_assert(sizeof(gm_t8nckeqfenl8a) == 24 && _Alignof(gm_t8nckeqfenl8a) == 8 && offsetof(gm_t8nckeqfenl8a, f0) == 0 && offsetof(gm_t8nckeqfenl8a, f1) == 8 && offsetof(gm_t8nckeqfenl8a, f2) == 12, "layout of `(len: u64, elem: Ty, kind: ArrayKind)`");
_Static_assert(sizeof(gm_tab260m0n3sqj) == 32 && _Alignof(gm_tab260m0n3sqj) == 8 && offsetof(gm_tab260m0n3sqj, f0) == 0 && offsetof(gm_tab260m0n3sqj, f1) == 24, "layout of `?Place`");
_Static_assert(sizeof(gm_tandvbt63uk7n) == 24 && _Alignof(gm_tandvbt63uk7n) == 8 && offsetof(gm_tandvbt63uk7n, f0) == 0 && offsetof(gm_tandvbt63uk7n, f1) == 8, "layout of `(op: SoaOp, args: []Expr)`");
_Static_assert(sizeof(gm_tb7u19loo3j87) == 24 && _Alignof(gm_tb7u19loo3j87) == 8 && offsetof(gm_tb7u19loo3j87, f0) == 0 && offsetof(gm_tb7u19loo3j87, f1) == 16, "layout of `?(len: u64, elem: Ty)`");
_Static_assert(sizeof(gm_tbci5ospk5nnn) == 48 && _Alignof(gm_tbci5ospk5nnn) == 8 && offsetof(gm_tbci5ospk5nnn, f0) == 0 && offsetof(gm_tbci5ospk5nnn, f1) == 8, "layout of `Int`");
_Static_assert(sizeof(gm_tddtjfggdat0d) == 48 && _Alignof(gm_tddtjfggdat0d) == 8 && offsetof(gm_tddtjfggdat0d, f0) == 0 && offsetof(gm_tddtjfggdat0d, f1) == 16 && offsetof(gm_tddtjfggdat0d, f2) == 24 && offsetof(gm_tddtjfggdat0d, f3) == 40, "layout of `FnTy`");
_Static_assert(sizeof(gm_teo9d4mncjfdu) == 24 && _Alignof(gm_teo9d4mncjfdu) == 8 && offsetof(gm_teo9d4mncjfdu, f0) == 0 && offsetof(gm_teo9d4mncjfdu, f1) == 16, "layout of `BreakExpr`");
_Static_assert(sizeof(gm_th9nl3oso747e) == 12 && _Alignof(gm_th9nl3oso747e) == 4 && offsetof(gm_th9nl3oso747e, f0) == 0, "layout of `(Builtin,)`");
typedef struct { gm_tbci5ospk5nnn f0; uint8_t f1; uint8_t pe[7]; } gm_thrdpodrs8pgt;
_Static_assert(sizeof(gm_tjcptv9sagfq8) == 88 && _Alignof(gm_tjcptv9sagfq8) == 8 && offsetof(gm_tjcptv9sagfq8, f0) == 0 && offsetof(gm_tjcptv9sagfq8, f1) == 16 && offsetof(gm_tjcptv9sagfq8, f2) == 28 && offsetof(gm_tjcptv9sagfq8, f3) == 32 && offsetof(gm_tjcptv9sagfq8, f4) == 40 && offsetof(gm_tjcptv9sagfq8, f5) == 48 && offsetof(gm_tjcptv9sagfq8, f6) == 56 && offsetof(gm_tjcptv9sagfq8, f7) == 64 && offsetof(gm_tjcptv9sagfq8, f8) == 72 && offsetof(gm_tjcptv9sagfq8, f9) == 80, "layout of `ConstDef`");
typedef struct { gm_tbci5ospk5nnn f0; gm_tbci5ospk5nnn f1; } gm_tngtuiuskn938;
_Static_assert(sizeof(gm_to6glrduves6o) == 32 && _Alignof(gm_to6glrduves6o) == 8 && offsetof(gm_to6glrduves6o, f0) == 0 && offsetof(gm_to6glrduves6o, f1) == 24, "layout of `(start: Place, len: u64)`");
_Static_assert(sizeof(gm_tpautr4br7h3l) == 48 && _Alignof(gm_tpautr4br7h3l) == 8 && offsetof(gm_tpautr4br7h3l, f0) == 0 && offsetof(gm_tpautr4br7h3l, f1) == 16 && offsetof(gm_tpautr4br7h3l, f2) == 24 && offsetof(gm_tpautr4br7h3l, f3) == 40, "layout of `StructDef`");
_Static_assert(sizeof(gm_trl9d7m1k5lbd) == 24 && _Alignof(gm_trl9d7m1k5lbd) == 8 && offsetof(gm_trl9d7m1k5lbd, f0) == 0 && offsetof(gm_trl9d7m1k5lbd, f1) == 8 && offsetof(gm_trl9d7m1k5lbd, f2) == 16, "layout of `(op: BinOp, lhs: ^Expr, rhs: ^Expr)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint32_t v0; gm_t419mo45g7j0a v1; void * v2; } f1; } gm_tsth9qu1dv0g3;
_Static_assert(sizeof(gm_tt8o5kebou62r) == 32 && _Alignof(gm_tt8o5kebou62r) == 8 && offsetof(gm_tt8o5kebou62r, f0) == 0 && offsetof(gm_tt8o5kebou62r, f1) == 16, "layout of `(op: Builtin, args: []Expr)`");
_Static_assert(sizeof(gm_ttu09lij36f8c) == 40 && _Alignof(gm_ttu09lij36f8c) == 8 && offsetof(gm_ttu09lij36f8c, f0) == 0 && offsetof(gm_ttu09lij36f8c, f1) == 32, "layout of `?Split`");
typedef struct { void * f0; uint8_t p1[8]; gm_t6evimt84lndq f1; } gm_t2i13nq5toap9;
_Static_assert(sizeof(gm_t53at80pfdus3) == 56 && _Alignof(gm_t53at80pfdus3) == 8 && offsetof(gm_t53at80pfdus3, f0) == 0 && offsetof(gm_t53at80pfdus3, f1) == 8, "layout of `TyKind`");
_Static_assert(sizeof(gm_t6evimt84lndq) == 48 && _Alignof(gm_t6evimt84lndq) == 16 && offsetof(gm_t6evimt84lndq, f0) == 0 && offsetof(gm_t6evimt84lndq, f1) == 16, "layout of `Value`");
typedef struct { uint64_t f0; uint8_t p1[8]; gm_t6evimt84lndq f1; } gm_thf8as07kj3uj;
_Static_assert(sizeof(gm_thrdpodrs8pgt) == 56 && _Alignof(gm_thrdpodrs8pgt) == 8 && offsetof(gm_thrdpodrs8pgt, f0) == 0 && offsetof(gm_thrdpodrs8pgt, f1) == 48, "layout of `?Int`");
typedef struct { gm_t6evimt84lndq f0; uint8_t f1; uint8_t pe[15]; } gm_tjoi8o798ne9k;
_Static_assert(sizeof(gm_tngtuiuskn938) == 96 && _Alignof(gm_tngtuiuskn938) == 8 && offsetof(gm_tngtuiuskn938, f0) == 0 && offsetof(gm_tngtuiuskn938, f1) == 48, "layout of `Rat`");
typedef struct { gm_tsth9qu1dv0g3 f0; gm_b16_8 f1; } gm_to76jli3bqddq;
_Static_assert(sizeof(gm_tsth9qu1dv0g3) == 40 && _Alignof(gm_tsth9qu1dv0g3) == 8 && offsetof(gm_tsth9qu1dv0g3, f0) == 0 && offsetof(gm_tsth9qu1dv0g3, f1) == 8, "layout of `Callee`");
typedef struct { gm_tngtuiuskn938 f0; uint8_t f1; uint8_t pe[7]; } gm_tvegkb6ouf3m0;
_Static_assert(sizeof(gm_t2i13nq5toap9) == 64 && _Alignof(gm_t2i13nq5toap9) == 16 && offsetof(gm_t2i13nq5toap9, f0) == 0 && offsetof(gm_t2i13nq5toap9, f1) == 16, "layout of `(frame: ^Frame, value: Value)`");
typedef struct { void * f0; uint32_t f1; uint8_t p2[4]; gm_tjoi8o798ne9k f2; } gm_ta160mkacvukc;
typedef struct { uint32_t f0; uint8_t p1[12]; gm_tjoi8o798ne9k f1; } gm_tbnj1edh31eaa;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tbci5ospk5nnn v0; double v1; void * v2; void * v3; uint8_t v4; gm_b16_8 v5; gm_b16_8 v6; uint32_t v7; gm_tkn42u7na3t81 v8; gm_tkn42u7na3t81 v9; gm_t88rcucg1b7uk v10; gm_t3rcjibl8kkg4 v11; gm_t08olq97rscn3 v12; uint32_t v13; gm_tcc1rq4n1avb2 v14; gm_t3877pk7lv748 v15; void * v16; gm_tbl8il92l97ut v17; gm_tlcl4u558l6p5 v18; gm_tao63pk3r18v5 v19; void * v20; void * v21; void * v22; void * v23; uint32_t v24; uint32_t v25; uint32_t v26; gm_t8ghgl2jj0qha v27; gm_trl9d7m1k5lbd v28; gm_ta612fil812pm v29; gm_t8jsc4n9vp2ib v30; gm_tt8o5kebou62r v31; gm_to76jli3bqddq v32; gm_b16_8 v33; gm_b16_8 v34; void * v35; gm_ttggq7ng4kp3p v36; void * v37; gm_tsplfr33dsvji v38; gm_t3dcoc7077t1q v39; gm_t083kd7b1kkag v40; gm_tlcl4u558l6p5 v41; gm_tkaapcii86udk v42; gm_tlcl4u558l6p5 v43; void * v44; gm_trl2khmuphncd v45; gm_t710lsrq1m8gk v46; gm_teo9d4mncjfdu v47; gm_te9hrk0u35rev v48; gm_ttggq7ng4kp3p v49; void * v50; gm_t3mf58gv9kf3n v51; void * v52; void * v53; void * v54; gm_tandvbt63uk7n v55; gm_t710lsrq1m8gk v56; } f1; } gm_teti411eujvf4;
_Static_assert(sizeof(gm_thf8as07kj3uj) == 64 && _Alignof(gm_thf8as07kj3uj) == 16 && offsetof(gm_thf8as07kj3uj, f0) == 0 && offsetof(gm_thf8as07kj3uj, f1) == 16, "layout of `(i: usize, x: Value)`");
_Static_assert(sizeof(gm_tjoi8o798ne9k) == 64 && _Alignof(gm_tjoi8o798ne9k) == 16 && offsetof(gm_tjoi8o798ne9k, f0) == 0 && offsetof(gm_tjoi8o798ne9k, f1) == 48, "layout of `?Value`");
_Static_assert(sizeof(gm_to76jli3bqddq) == 56 && _Alignof(gm_to76jli3bqddq) == 8 && offsetof(gm_to76jli3bqddq, f0) == 0 && offsetof(gm_to76jli3bqddq, f1) == 40, "layout of `(callee: Callee, args: []Expr)`");
_Static_assert(sizeof(gm_tvegkb6ouf3m0) == 104 && _Alignof(gm_tvegkb6ouf3m0) == 8 && offsetof(gm_tvegkb6ouf3m0, f0) == 0 && offsetof(gm_tvegkb6ouf3m0, f1) == 96, "layout of `?Rat`");
typedef struct { uint8_t f0; uint8_t p1[15]; union { gm_ta160mkacvukc v0; gm_tbl8il92l97ut v1; gm_t2i13nq5toap9 v2; gm_tbnj1edh31eaa v3; void * v4; gm_tbgd14esf0jvi v5; } f1; } gm_t39uai1ajk3qv;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_teti411eujvf4 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_t69m56meqefs8;
_Static_assert(sizeof(gm_ta160mkacvukc) == 80 && _Alignof(gm_ta160mkacvukc) == 16 && offsetof(gm_ta160mkacvukc, f0) == 0 && offsetof(gm_ta160mkacvukc, f1) == 8 && offsetof(gm_ta160mkacvukc, f2) == 16, "layout of `(frame: ^Frame, label: u32, value: ?Value)`");
_Static_assert(sizeof(gm_tbnj1edh31eaa) == 80 && _Alignof(gm_tbnj1edh31eaa) == 16 && offsetof(gm_tbnj1edh31eaa, f0) == 0 && offsetof(gm_tbnj1edh31eaa, f1) == 16, "layout of `(with_id: u32, value: ?Value)`");
_Static_assert(sizeof(gm_teti411eujvf4) == 64 && _Alignof(gm_teti411eujvf4) == 8 && offsetof(gm_teti411eujvf4, f0) == 0 && offsetof(gm_teti411eujvf4, f1) == 8, "layout of `ExprKind`");
typedef struct { uint8_t f0; uint8_t p1[15]; union { gm_t39uai1ajk3qv v0; double v1; } f1; } gm_t26jhpkpc0sda;
_Static_assert(sizeof(gm_t39uai1ajk3qv) == 96 && _Alignof(gm_t39uai1ajk3qv) == 16 && offsetof(gm_t39uai1ajk3qv, f0) == 0 && offsetof(gm_t39uai1ajk3qv, f1) == 16, "layout of `Flow`");
typedef struct { uint8_t f0; uint8_t p1[15]; union { gm_t39uai1ajk3qv v0; gm_tkaapcii86udk v1; } f1; } gm_t5kq49rr2n9f8;
typedef struct { uint8_t f0; uint8_t p1[15]; union { gm_t39uai1ajk3qv v0; gm_b16_8 v1; } f1; } gm_t65n7r6ql4pgd;
_Static_assert(sizeof(gm_t69m56meqefs8) == 88 && _Alignof(gm_t69m56meqefs8) == 8 && offsetof(gm_t69m56meqefs8, f0) == 0 && offsetof(gm_t69m56meqefs8, f1) == 8 && offsetof(gm_t69m56meqefs8, f2) == 72, "layout of `Expr`");
typedef struct { uint8_t f0; uint8_t p1[15]; union { gm_t39uai1ajk3qv v0; void * v1; } f1; } gm_t7g11k265ojef;
typedef struct { uint8_t f0; uint8_t p1[15]; union { gm_t39uai1ajk3qv v0; unsigned __int128 v1; } f1; } gm_t8carr7mku484;
typedef struct { uint8_t f0; uint8_t p1[15]; union { gm_t39uai1ajk3qv v0; } f1; } gm_t9j082e6qvnjj;
typedef struct { uint8_t f0; uint8_t p1[15]; union { gm_t39uai1ajk3qv v0; uint8_t v1; } f1; } gm_tsg40glpvq01o;
typedef struct { uint8_t f0; uint8_t p1[15]; union { gm_t39uai1ajk3qv v0; gm_t6evimt84lndq v1; } f1; } gm_ttk92ufj2f6eh;
_Static_assert(sizeof(gm_t26jhpkpc0sda) == 112 && _Alignof(gm_t26jhpkpc0sda) == 16 && offsetof(gm_t26jhpkpc0sda, f0) == 0 && offsetof(gm_t26jhpkpc0sda, f1) == 16, "layout of `Flow!f64`");
_Static_assert(sizeof(gm_t5kq49rr2n9f8) == 112 && _Alignof(gm_t5kq49rr2n9f8) == 16 && offsetof(gm_t5kq49rr2n9f8, f0) == 0 && offsetof(gm_t5kq49rr2n9f8, f1) == 16, "layout of `Flow!Place`");
_Static_assert(sizeof(gm_t65n7r6ql4pgd) == 112 && _Alignof(gm_t65n7r6ql4pgd) == 16 && offsetof(gm_t65n7r6ql4pgd, f0) == 0 && offsetof(gm_t65n7r6ql4pgd, f1) == 16, "layout of `Flow![]Value`");
_Static_assert(sizeof(gm_t7g11k265ojef) == 112 && _Alignof(gm_t7g11k265ojef) == 16 && offsetof(gm_t7g11k265ojef, f0) == 0 && offsetof(gm_t7g11k265ojef, f1) == 16, "layout of `Flow!^Cell`");
_Static_assert(sizeof(gm_t8carr7mku484) == 112 && _Alignof(gm_t8carr7mku484) == 16 && offsetof(gm_t8carr7mku484, f0) == 0 && offsetof(gm_t8carr7mku484, f1) == 16, "layout of `Flow!u128`");
_Static_assert(sizeof(gm_t9j082e6qvnjj) == 112 && _Alignof(gm_t9j082e6qvnjj) == 16 && offsetof(gm_t9j082e6qvnjj, f0) == 0 && offsetof(gm_t9j082e6qvnjj, f1) == 16, "layout of `Flow!()`");
_Static_assert(sizeof(gm_tsg40glpvq01o) == 112 && _Alignof(gm_tsg40glpvq01o) == 16 && offsetof(gm_tsg40glpvq01o, f0) == 0 && offsetof(gm_tsg40glpvq01o, f1) == 16, "layout of `Flow!bool`");
_Static_assert(sizeof(gm_ttk92ufj2f6eh) == 112 && _Alignof(gm_ttk92ufj2f6eh) == 16 && offsetof(gm_ttk92ufj2f6eh, f0) == 0 && offsetof(gm_ttk92ufj2f6eh, f1) == 16, "layout of `Flow!Value`");
struct gm_fr_abs_v7cf214 {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_tbci5ospk5nnn l1; /* r */
    gm_tbci5ospk5nnn ret;
};
struct gm_fr_add_420lj6o {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_tbci5ospk5nnn l1; /* b */
    gm_tbci5ospk5nnn ret;
};
struct gm_fr_append_sajo2rq {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* out */
    gm_b16_8 ret;
};
struct gm_fr_big_float_jmiufgl {
    gm_frame h;
    gm_tbci5ospk5nnn l1; /* i */
    double ret;
};
struct gm_fr_big_of_1cq0qao {
    gm_frame h;
    gm_tbci5ospk5nnn ret;
};
struct gm_fr_bigint_i128_p6m54kv {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* i */
    gm_ta24rd13gupia ret;
};
struct gm_fr_binary_fmoblrg {
    gm_frame h;
    gm_t6evimt84lndq l2; /* a */
    gm_t6evimt84lndq l3; /* b */
    gm_trqfvmjtt4sij l9; /* out */
    gm_t6evimt84lndq l13; /* x */
    gm_t6evimt84lndq l14; /* y */
    gm_ttk92ufj2f6eh ret;
};
struct gm_fr_bit_s517clu {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_builtin_psq6ql3 {
    gm_frame h;
    gm_t6evimt84lndq l7; /* msg */
    gm_t6evimt84lndq l10; /* x */
    gm_tbci5ospk5nnn l16; /* tag */
    gm_tbci5ospk5nnn l19; /* i */
    gm_tbci5ospk5nnn l22; /* v */
    gm_tngtuiuskn938 l23; /* r */
    gm_tngtuiuskn938 l25; /* q */
    gm_tngtuiuskn938 l26; /* r */
    gm_tngtuiuskn938 l27; /* r */
    gm_tbci5ospk5nnn l28; /* v */
    gm_tngtuiuskn938 l31; /* q */
    gm_t6evimt84lndq l32; /* y */
    gm_tbci5ospk5nnn l41; /* a */
    gm_tbci5ospk5nnn l43; /* v */
    gm_tngtuiuskn938 l44; /* r */
    gm_tbci5ospk5nnn l45; /* a */
    gm_tbci5ospk5nnn l49; /* d */
    gm_tbci5ospk5nnn l53; /* exact */
    gm_tngtuiuskn938 l55; /* r */
    gm_tbci5ospk5nnn l63; /* tag */
    gm_t6evimt84lndq l75; /* v */
    gm_trqfvmjtt4sij l78; /* elems */
    gm_t6evimt84lndq l85; /* v */
    gm_t6evimt84lndq l87; /* pv */
    gm_trqfvmjtt4sij l95; /* path */
    unsigned __int128 l100; /* v */
    gm_tbci5ospk5nnn l116; /* amount */
    gm_ttk92ufj2f6eh ret;
};
struct gm_fr_byte_at_m94bj3e {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_cmp_9aseneh {
    gm_frame h;
    gm_tngtuiuskn938 l0; /* a */
    gm_tngtuiuskn938 l1; /* b */
    int32_t ret;
};
struct gm_fr_coerce_fesnqv4 {
    gm_frame h;
    gm_t6evimt84lndq l4; /* v */
    gm_ttk92ufj2f6eh ret;
};
struct gm_fr_const_cell_p4adg2s {
    gm_frame h;
    gm_tjcptv9sagfq8 l5; /* c */
    gm_t7g11k265ojef ret;
};
struct gm_fr_const_id_2mlh0og {
    gm_frame h;
    gm_t9805qtonbsag ret;
};
struct gm_fr_convert_value_v1i8v3i {
    gm_frame h;
    gm_t6evimt84lndq l1; /* v */
    gm_ttk92ufj2f6eh ret;
};
struct gm_fr_copy_pmkg67r {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_trqfvmjtt4sij l1; /* l */
    gm_tbci5ospk5nnn ret;
};
struct gm_fr_display_covn4i2 {
    gm_frame h;
    gm_t6evimt84lndq l1; /* v */
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_div_f0qrfbr {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_tbci5ospk5nnn l1; /* b */
    gm_tbci5ospk5nnn l2; /* q */
    gm_tbci5ospk5nnn ret;
};
struct gm_fr_div_floor_u3mfihk {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_tbci5ospk5nnn l1; /* b */
    gm_tbci5ospk5nnn l2; /* q */
    gm_tbci5ospk5nnn ret;
};
struct gm_fr_empty_slice_84b0b46 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_d914i5u {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_dheabhi {
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
struct gm_fr_eq_60fjn21 {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_tbci5ospk5nnn l1; /* b */
    uint8_t ret;
};
struct gm_fr_equal_hinsj77 {
    gm_frame h;
    gm_t6evimt84lndq l1; /* a */
    gm_t6evimt84lndq l2; /* b */
    gm_tsg40glpvq01o ret;
};
struct gm_fr_field_tys_foan32f {
    gm_frame h;
    gm_trqfvmjtt4sij l6; /* out */
    gm_tpautr4br7h3l l10; /* def */
    gm_trqfvmjtt4sij l11; /* out */
    gm_t0mglksrkfa5e l14; /* f */
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_fit_8hvi4bv {
    gm_frame h;
    gm_tbci5ospk5nnn l1; /* exact */
    gm_tngtuiuskn938 l6; /* r */
    gm_t8carr7mku484 ret;
};
struct gm_fr_fit_small_7o94i8v {
    gm_frame h;
    gm_t8carr7mku484 ret;
};
struct gm_fr_float_exact_1g17c6c {
    gm_frame h;
    gm_tbci5ospk5nnn l6; /* num */
    gm_tvegkb6ouf3m0 ret;
};
struct gm_fr_float_rem_24loign {
    gm_frame h;
    gm_tngtuiuskn938 l2; /* a */
    gm_tngtuiuskn938 l3; /* b */
    gm_tngtuiuskn938 l4; /* q */
    gm_tbci5ospk5nnn l5; /* t */
    gm_tngtuiuskn938 l6; /* r */
    double ret;
};
struct gm_fr_float_to_int_sf4dap7 {
    gm_frame h;
    gm_tngtuiuskn938 l4; /* q */
    gm_tbci5ospk5nnn l5; /* fl */
    gm_tngtuiuskn938 l6; /* frac */
    gm_tbci5ospk5nnn l7; /* i */
    gm_tngtuiuskn938 l9; /* half */
    gm_tngtuiuskn938 l11; /* r */
    gm_thrdpodrs8pgt ret;
};
struct gm_fr_from_i128_1n0s49q {
    gm_frame h;
    gm_tbci5ospk5nnn l1; /* r */
    gm_tbci5ospk5nnn ret;
};
struct gm_fr_from_int_ofjuoqh {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* i */
    gm_tngtuiuskn938 ret;
};
struct gm_fr_get_bytes_ql9o4k0 {
    gm_frame h;
    gm_trqfvmjtt4sij l17; /* elems */
    gm_tjoi8o798ne9k ret;
};
struct gm_fr_int_info_2r9qb5n {
    gm_frame h;
    gm_ttovhfee8mf28 ret;
};
struct gm_fr_int_op_jnafd2a {
    gm_frame h;
    gm_tbci5ospk5nnn l16; /* a */
    gm_tbci5ospk5nnn l17; /* c */
    gm_tbci5ospk5nnn l18; /* exact */
    gm_tbci5ospk5nnn l25; /* a */
    gm_tbci5ospk5nnn l26; /* c */
    gm_tbci5ospk5nnn l27; /* exact */
    gm_t8carr7mku484 ret;
};
struct gm_fr_int_range_hue3vms {
    gm_frame h;
    gm_tbci5ospk5nnn l3; /* half */
    gm_tngtuiuskn938 ret;
};
struct gm_fr_int_value_6hmh8tj {
    gm_frame h;
    __int128 ret;
};
struct gm_fr_is_even_3kgcpjs {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    uint8_t ret;
};
struct gm_fr_is_integer_8d4f9ek {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_integer_ab2ohk2 {
    gm_frame h;
    gm_tngtuiuskn938 l0; /* r */
    uint8_t ret;
};
struct gm_fr_is_negative_k87pp3n {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    uint8_t ret;
};
struct gm_fr_is_negative_rb46l9j {
    gm_frame h;
    gm_tngtuiuskn938 l0; /* r */
    uint8_t ret;
};
struct gm_fr_is_zero_5aept6u {
    gm_frame h;
    gm_tngtuiuskn938 l0; /* r */
    uint8_t ret;
};
struct gm_fr_is_zero_8tcct9o {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    uint8_t ret;
};
struct gm_fr_kind_jcnt4hm {
    gm_frame h;
    gm_t53at80pfdus3 ret;
};
struct gm_fr_list_0202sdh {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_go2qg0b {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_kl0t9m0 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_nof7vfj {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_uvfu1k1 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_make_r4pbu77 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* m */
    gm_trqfvmjtt4sij l2; /* l */
    gm_tbci5ospk5nnn ret;
};
struct gm_fr_mask_gbhl4hp {
    gm_frame h;
    unsigned __int128 ret;
};
struct gm_fr_mul_nfj5rjv {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_tbci5ospk5nnn l1; /* b */
    gm_tbci5ospk5nnn ret;
};
struct gm_fr_mul_od1e7nd {
    gm_frame h;
    gm_tngtuiuskn938 l0; /* a */
    gm_tngtuiuskn938 l1; /* b */
    gm_tngtuiuskn938 ret;
};
struct gm_fr_neg_agd5c5k {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_tbci5ospk5nnn l1; /* r */
    gm_tbci5ospk5nnn ret;
};
struct gm_fr_offsets_niooj98 {
    gm_frame h;
    gm_trqfvmjtt4sij l6; /* tys */
    gm_trqfvmjtt4sij l7; /* offs */
    gm_tpautr4br7h3l l13; /* def */
    gm_trqfvmjtt4sij l14; /* tys */
    gm_t0mglksrkfa5e l17; /* f */
    gm_ttu09lij36f8c ret;
};
struct gm_fr_one_08qg1op {
    gm_frame h;
    gm_tbci5ospk5nnn ret;
};
struct gm_fr_payload_ty_mu1ii1k {
    gm_frame h;
    gm_tkn42u7na3t81 ret;
};
struct gm_fr_printer_h7jigd7 {
    gm_frame h;
    gm_tmm67r2c94d5j ret;
};
struct gm_fr_ptr_bits_glps5i7 {
    gm_frame h;
    uint32_t ret;
};
struct gm_fr_put_bytes_1ltl1lv {
    gm_frame h;
    gm_t6evimt84lndq l1; /* v */
    gm_t6evimt84lndq l23; /* x */
    uint8_t ret;
};
struct gm_fr_reinterpret_jmr6jts {
    gm_frame h;
    gm_t6evimt84lndq l1; /* v */
    gm_trqfvmjtt4sij l6; /* bytes */
    gm_t6evimt84lndq l9; /* r */
    gm_ttk92ufj2f6eh ret;
};
struct gm_fr_rem_ib4l1vl {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_tbci5ospk5nnn l1; /* b */
    gm_tbci5ospk5nnn l2; /* r */
    gm_tbci5ospk5nnn ret;
};
struct gm_fr_round_float_saju9fu {
    gm_frame h;
    gm_tngtuiuskn938 l4; /* r */
    gm_t26jhpkpc0sda ret;
};
struct gm_fr_same_place_1r7qrqk {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_scalar_binary_bvc65kt {
    gm_frame h;
    gm_t6evimt84lndq l2; /* a */
    gm_t6evimt84lndq l3; /* b */
    gm_ttk92ufj2f6eh ret;
};
struct gm_fr_shl_qqecl81 {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_tbci5ospk5nnn ret;
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
struct gm_fr_sub_mler6q8 {
    gm_frame h;
    gm_tngtuiuskn938 l0; /* a */
    gm_tngtuiuskn938 l1; /* b */
    gm_tngtuiuskn938 ret;
};
struct gm_fr_sub_pktm5cr {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_tbci5ospk5nnn l1; /* b */
    gm_tbci5ospk5nnn ret;
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
struct gm_fr_unary_19d8qg7 {
    gm_frame h;
    gm_t6evimt84lndq l4; /* v */
    gm_trqfvmjtt4sij l7; /* out */
    gm_t6evimt84lndq l10; /* el */
    gm_ttk92ufj2f6eh ret;
};
struct gm_fr_unary_value_vi7cs6u {
    gm_frame h;
    gm_t6evimt84lndq l2; /* v */
    gm_ttk92ufj2f6eh ret;
};
struct gm_fr_unit_value_evae6mr {
    gm_frame h;
    gm_t6evimt84lndq ret;
};
struct gm_fr_unsupported_2seq49l {
    gm_frame h;
    gm_t39uai1ajk3qv ret;
};
struct gm_fr_variant_tag_4pbamin {
    gm_frame h;
    gm_thrdpodrs8pgt ret;
};
struct gm_fr_wrap_bt8r149 {
    gm_frame h;
    unsigned __int128 ret;
};
struct gm_fr_zero_s25l990 {
    gm_frame h;
    gm_tbci5ospk5nnn ret;
};
inline bool gm_eq467d5cm1d5(const uint8_t *a, const uint8_t *b);
inline bool gm_eq9m6mvtmgde(const uint8_t *a, const uint8_t *b);
inline __attribute__((aligned(4))) void gm_f_abs_v7cf214(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_add_420lj6o(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_append_sajo2rq(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_args_6f16vj1(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) double gm_f_big_float_jmiufgl(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_big_of_1cq0qao(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, unsigned __int128 a1, gm_t88rcucg1b7uk a2);
inline __attribute__((aligned(4))) void gm_f_bigint_i128_p6m54kv(void *gm_out, const void * a0);
__attribute__((aligned(4))) gm_ta24rd13gupia gm_f_bigint_i128_p6m54kv_c(gm_tbci5ospk5nnn a0);
__attribute__((aligned(4))) void gm_f_binary_fmoblrg(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_ts6oqppjh0ba3 a1, const void * a2, const void * a3, gm_t88rcucg1b7uk a4, gm_t88rcucg1b7uk a5, gm_tl874t2atnlh3 a6);
__attribute__((aligned(4))) gm_b16_8 gm_f_binop_symbol_9ijp0eo(gm_ts6oqppjh0ba3 a0);
inline __attribute__((aligned(4))) uint8_t gm_f_bit_s517clu(void *env, gm_node *cx, gm_node *inst, uint32_t a0);
__attribute__((aligned(4))) unsigned __int128 gm_f_bits128_jigkui4(const void * a0);
__attribute__((aligned(4))) uint64_t gm_f_bits_lr3of4i(const void * a0);
__attribute__((aligned(4))) void gm_f_builtin_psq6ql3(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tvdg94bk7aa22 a1, gm_b16_8 a2, void * a3);
inline __attribute__((aligned(4))) uint64_t gm_f_byte_at_m94bj3e(void * a0, uint64_t a1, uint64_t a2);
inline __attribute__((aligned(4))) int32_t gm_f_cmp_9aseneh(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) int32_t gm_f_cmp_i1knrgu(const void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_coerce_fesnqv4(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tmn7n0n9raou5 a1, void * a2, void * a3);
__attribute__((aligned(4))) void gm_f_const_cell_p4adg2s(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t88rcucg1b7uk a2, gm_tl874t2atnlh3 a3);
inline __attribute__((aligned(4))) gm_t9805qtonbsag gm_f_const_id_2mlh0og(void * a0, gm_cseahvg8v7p0vs a1, gm_csguankko4lmhd a2);
__attribute__((aligned(4))) void gm_f_convert_value_v1i8v3i(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_t88rcucg1b7uk a2, gm_t88rcucg1b7uk a3);
__attribute__((aligned(4))) void gm_f_copy_nk4jid6(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_copy_pmkg67r(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_display_covn4i2(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) void gm_f_div_d8sd4qc(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
inline __attribute__((aligned(4))) void gm_f_div_f0qrfbr(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
inline __attribute__((aligned(4))) void gm_f_div_floor_u3mfihk(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_div_mod_floor_eqv1ila(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_div_rem_sc09dcc(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_84b0b46(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_d914i5u(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_dheabhi(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_rcd2jln(void);
inline __attribute__((aligned(4))) uint8_t gm_f_eq_60fjn21(const void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_equal_hinsj77(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, const void * a2, gm_t88rcucg1b7uk a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) void gm_f_eval_2vbtp1v(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_extend_7hs67uj(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_extend_ll5819n(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_field_tys_foan32f(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, uint64_t a2);
__attribute__((aligned(4))) void gm_f_fit_8hvi4bv(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_t9qkh0rsooc8d a2, gm_t88rcucg1b7uk a3, gm_b16_8 a4, gm_tl874t2atnlh3 a5);
__attribute__((aligned(4))) void gm_f_fit_small_7o94i8v(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, __int128 a1, gm_t9qkh0rsooc8d a2, gm_t88rcucg1b7uk a3, gm_b16_8 a4, gm_tl874t2atnlh3 a5);
__attribute__((aligned(4))) void gm_f_float_exact_1g17c6c(void *gm_out, void *env, gm_node *cx, gm_node *inst, double a0);
__attribute__((aligned(4))) double gm_f_float_rem_24loign(void *env, gm_node *cx, gm_node *inst, double a0, double a1);
__attribute__((aligned(4))) void gm_f_float_to_int_sf4dap7(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, double a1, gm_t88rcucg1b7uk a2, gm_tvdg94bk7aa22 a3);
__attribute__((aligned(4))) void gm_f_format_0fuesvo(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_0sujqbp(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_9u3jvus(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t9qkh0rsooc8d a0);
__attribute__((aligned(4))) void gm_f_format_eruojoe(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_g0ibdjv(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_o6d0gj1(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_th9nl3oso747e a0);
__attribute__((aligned(4))) void gm_f_format_q8f0n70(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_u0vfg08(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_ven7kt5(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
inline __attribute__((aligned(4))) void gm_f_from_i128_1n0s49q(void *gm_out, void *env, gm_node *cx, gm_node *inst, __int128 a0);
inline __attribute__((aligned(4))) void gm_f_from_int_ofjuoqh(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_from_u128_gl5fk0t(void *gm_out, void *env, gm_node *cx, gm_node *inst, unsigned __int128 a0);
__attribute__((aligned(4))) void gm_f_from_u64_ud13q5v(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint64_t a0);
__attribute__((aligned(4))) void gm_f_get_bytes_ql9o4k0(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, uint64_t a3);
__attribute__((aligned(4))) gm_t9805qtonbsag gm_f_get_pk8ncss(void * a0, gm_trlabqsjjp0ds a1);
__attribute__((aligned(4))) gm_ttovhfee8mf28 gm_f_int_info_2r9qb5n(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void gm_f_int_op_jnafd2a(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_ts6oqppjh0ba3 a1, unsigned __int128 a2, unsigned __int128 a3, gm_t88rcucg1b7uk a4, gm_t88rcucg1b7uk a5, gm_tl874t2atnlh3 a6);
__attribute__((aligned(4))) void gm_f_int_range_hue3vms(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) __int128 gm_f_int_value_6hmh8tj(void * a0, unsigned __int128 a1, gm_csguankko4lmhd a2);
__attribute__((aligned(4))) void gm_f_into_slice_hq0te56(void *gm_out, void * a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_even_3kgcpjs(const void * a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_integer_8d4f9ek(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_integer_ab2ohk2(const void * a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_negative_k87pp3n(const void * a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_negative_rb46l9j(const void * a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_zero_5aept6u(const void * a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_zero_8tcct9o(const void * a0);
inline __attribute__((aligned(4))) void gm_f_kind_jcnt4hm(void *gm_out, void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void gm_f_layout_a5ovnrm(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) void gm_f_list_0202sdh(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_go2qg0b(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_kl0t9m0(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_nof7vfj(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_load_nf7o5kl(void *gm_out, void * a0, const void * a1);
__attribute__((aligned(4))) int32_t gm_f_mag_cmp_s39veq6(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_mag_mul_if5s38k(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_mag_shl_5df643p(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_mag_to_u128_f843dal(void *gm_out, const void * a0);
inline __attribute__((aligned(4))) void gm_f_make_r4pbu77(void *gm_out, uint8_t a0, const void * a1);
inline __attribute__((aligned(4))) unsigned __int128 gm_f_mask_gbhl4hp(uint32_t a0);
inline __attribute__((aligned(4))) void gm_f_mul_nfj5rjv(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
inline __attribute__((aligned(4))) void gm_f_mul_od1e7nd(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
inline __attribute__((aligned(4))) void gm_f_neg_agd5c5k(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_15vqrvd(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_new_2jv6hag(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_70le0lp(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_lcff781(void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0, const void * a1);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_ve8bb01_2(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_offset_nkn711s(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_offsets_niooj98(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) void gm_f_one_08qg1op(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_payload_ty_mu1ii1k(void * a0, gm_csguankko4lmhd a1, uint32_t a2);
__attribute__((aligned(4))) void gm_f_place_9889dgp(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) void gm_f_pow_mhlb5fr(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, uint32_t a1);
inline __attribute__((aligned(4))) void gm_f_printer_h7jigd7(void *gm_out, void * a0, void * a1);
inline __attribute__((aligned(4))) uint32_t gm_f_ptr_bits_glps5i7(void * a0);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_0pgacsj(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_7o6oubd(void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_k3l9nic(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_t83rg2m(void * a0, uint8_t a1);
__attribute__((aligned(4))) uint8_t gm_f_put_bytes_1ltl1lv(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3, uint64_t a4);
__attribute__((aligned(4))) void gm_f_reinterpret_jmr6jts(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_t88rcucg1b7uk a2, gm_t88rcucg1b7uk a3, gm_tl874t2atnlh3 a4);
inline __attribute__((aligned(4))) void gm_f_rem_ib4l1vl(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_round_float_saju9fu(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, double a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) uint8_t gm_f_same_place_1r7qrqk(const void * a0, const void * a1);
__attribute__((aligned(4))) uint8_t gm_f_same_place_1r7qrqk_c(gm_tkaapcii86udk a0, gm_tkaapcii86udk a1);
__attribute__((aligned(4))) void gm_f_scalar_binary_bvc65kt(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_ts6oqppjh0ba3 a1, const void * a2, const void * a3, gm_t88rcucg1b7uk a4, gm_t88rcucg1b7uk a5, gm_tl874t2atnlh3 a6);
inline __attribute__((aligned(4))) void gm_f_shl_qqecl81(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, uint64_t a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_8uijkbs(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_kmjeh8s(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_signed_add_ek0dpmo(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint8_t a0, gm_b16_8 a1, uint8_t a2, gm_b16_8 a3);
__attribute__((aligned(4))) void gm_f_store_kejn5q1(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, const void * a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) void gm_f_string_of_plna4gu(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
inline __attribute__((aligned(4))) void gm_f_sub_mler6q8(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
inline __attribute__((aligned(4))) void gm_f_sub_pktm5cr(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_to_decimal_8bshn5b(void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) double gm_f_to_f16_0pcu0pq(void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) double gm_f_to_f64_4avhrht(void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_to_i128_o7ud1ue(void *gm_out, const void * a0);
inline __attribute__((aligned(4))) void gm_f_to_u128_haka99r(void *gm_out, const void * a0);
inline __attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_to_u64_m01t1da(const void * a0);
__attribute__((aligned(4))) void gm_f_trap_jftbss6(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_trim_8rdppmk(void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_ty_vka2r8h(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_unary_19d8qg7(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t9qkh0rsooc8d a1, void * a2, void * a3);
__attribute__((aligned(4))) void gm_f_unary_value_vi7cs6u(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t9qkh0rsooc8d a1, const void * a2, gm_t88rcucg1b7uk a3, gm_tl874t2atnlh3 a4, gm_tl874t2atnlh3 a5);
inline __attribute__((aligned(4))) void gm_f_unit_value_evae6mr(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_unsupported_2seq49l(void *gm_out, void * a0, gm_cseahvg8v7p0vs a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_variant_tag_4pbamin(void *gm_out, void * a0, gm_csguankko4lmhd a1, uint32_t a2);
__attribute__((aligned(4))) gm_thrdpodrs8pgt gm_f_variant_tag_4pbamin_c(void * a0, gm_csguankko4lmhd a1, uint32_t a2);
__attribute__((aligned(4))) void gm_f_vector_parts_crrfopm(void *gm_out, void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) unsigned __int128 gm_f_wrap_bt8r149(void * a0, unsigned __int128 a1, gm_csguankko4lmhd a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_write_decimal_ke367ab(void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1);
inline __attribute__((aligned(4))) void gm_f_zero_s25l990(void *gm_out, void *env, gm_node *cx, gm_node *inst);
extern uint8_t gm_k1fl2q13fn0[40] __attribute__((aligned(8)));
extern uint8_t gm_k4nsaof9c90[40] __attribute__((aligned(8)));
extern uint8_t gm_k4tn3ii07uh[40] __attribute__((aligned(8)));
extern uint8_t gm_k7898pnkupv[40] __attribute__((aligned(8)));
extern uint8_t gm_kct6b70ce3d[40] __attribute__((aligned(8)));
extern uint8_t gm_kdra5fvcfrb[40] __attribute__((aligned(8)));
extern uint8_t gm_kequv58cbtg[40] __attribute__((aligned(8)));
extern uint8_t gm_kha5d1oq7pb[40] __attribute__((aligned(8)));
extern uint8_t gm_kinrtga7ov4[40] __attribute__((aligned(8)));
extern uint8_t gm_kjh6c1ct3mq[40] __attribute__((aligned(8)));
extern uint8_t gm_kjqc35oao72[40] __attribute__((aligned(8)));
extern uint8_t gm_knj30d2h126[40] __attribute__((aligned(8)));
extern uint8_t gm_kqu6q38jp8e[40] __attribute__((aligned(8)));
extern uint8_t gm_krchumal22f[40] __attribute__((aligned(8)));
extern uint8_t gm_ksjvoab7999[40] __attribute__((aligned(8)));
extern uint8_t gm_ksrlu8s6v53[40] __attribute__((aligned(8)));
extern uint8_t gm_ktbnd3fuoao[40] __attribute__((aligned(8)));
extern uint8_t gm_ktv7kfnnmgp[40] __attribute__((aligned(8)));
extern const uint8_t gm_s0dggtblbl7jq[30];
extern const uint8_t gm_s2kubr08sqj72[15];
extern const uint8_t gm_s3385032lmmqq[24];
extern const uint8_t gm_s8afn1h3asffl[32];
extern const uint8_t gm_s94n39ltlvmhe[36];
extern const uint8_t gm_s9o2c36nosv54[41];
extern const uint8_t gm_s9ofbo8c28rrv[16];
extern const uint8_t gm_sbjuojbqo51av[36];
extern const uint8_t gm_sbsu8rcurukea[25];
extern const uint8_t gm_sd3b7a2bnmpef[25];
extern const uint8_t gm_sdrg69bmlm0tm[30];
extern const uint8_t gm_seg4idqqmijmd[34];
extern const uint8_t gm_sjn4krvshoajd[26];
extern const uint8_t gm_skolmnfc0ccbd[2];
extern const uint8_t gm_slf2kl43ogurh[17];
extern const uint8_t gm_sllgvbfukep8p[24];
extern const uint8_t gm_smqhtg33814q9[23];
extern const uint8_t gm_smr3su985na96[16];
extern const uint8_t gm_spi8n2atkg603[17];
extern const uint8_t gm_sqe4aodms9tfs[14];
extern const uint8_t gm_sqh0568fl8oao[14];
extern const char gm_src0ih77f43mhf2[];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_src4o48ksu43djd[];
extern const char gm_src9vg8k4mbj38f[];
extern const char gm_srclv3pj5es0f8o[];
extern const char gm_srcrgc9bkquf6nr[];
extern const char gm_srcue4ionbe9lgh[];
extern const uint8_t gm_su1oqg46rlg2d[10];
extern const uint8_t gm_sv7lsha1e0i8r[13];
extern const uint8_t gm_svpofgjtij7vj[4];
extern const gm_type gm_tyo86ks9qkvt;
inline bool gm_eq467d5cm1d5(const uint8_t *a, const uint8_t *b) {
    return GM_LD(uint32_t, a) == GM_LD(uint32_t, b);
}
inline bool gm_eq9m6mvtmgde(const uint8_t *a, const uint8_t *b) {
    if (!gm_eq467d5cm1d5(a + 0, b + 0)) return false;
    return true;
}
inline __attribute__((aligned(4))) void gm_f_abs_v7cf214(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_abs_v7cf214 F;
    GM_STACK_CHECK(&F, 0u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_copy_pmkg67r((void *)&F.l1, (void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)((uint8_t *)a0)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        GM_ST(uint8_t, (((uint8_t *)&F.l1) + 0), ((uint8_t)0));
    }
    {
        memmove(gm_out, ((uint8_t *)&F.l1), 48);
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
inline __attribute__((aligned(4))) void gm_f_add_420lj6o(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1) {
    struct gm_fr_add_420lj6o F;
    GM_STACK_CHECK(&F, 66u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memcpy(&F.l0, a0, sizeof F.l0);
    uint8_t t1 = GM_LD(uint8_t, (((uint8_t *)&F.l0) + 0));
    gm_b16_8 t2 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l0) + 8) + 0));
    gm_b16_8 t3 = GM_LD(gm_b16_8, ((uint8_t *)&t2));
    uint8_t t4 = GM_LD(uint8_t, (((uint8_t *)a1) + 0));
    gm_b16_8 t5 = GM_LD(gm_b16_8, ((((uint8_t *)a1) + 8) + 0));
    gm_b16_8 t6 = GM_LD(gm_b16_8, ((uint8_t *)&t5));
    gm_f_signed_add_ek0dpmo((void *)&(*(gm_tbci5ospk5nnn *)gm_out), (void *)NULL, cx, NULL, t1, t3, t4, t6);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_append_sajo2rq(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, uint64_t a1) {
    struct gm_fr_append_sajo2rq F;
    gm_b16_8 gm_l0; /* path */
    uint64_t gm_l1; /* i */
    GM_STACK_CHECK(&F, 133u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        gm_f_list_kl0t9m0((void *)&F.l2, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l2);
        gm_b16_8 t3 = gm_l0;
        gm_tftb1odc739t5 t4 = gm_f_extend_ll5819n(t2, t3);
        if (((((uint8_t *)&t4))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t4) + 1, gm_src9vg8k4mbj38f, 547, 5);
        gm_b0_1 t5 = GM_LD(gm_b0_1, (((uint8_t *)&t4) + 1));
    }
    {
        void * t6 = (void *)((uint8_t *)&F.l2);
        uint64_t t7 = gm_l1;
        gm_tftb1odc739t5 t8 = gm_f_push_7o6oubd(t6, t7);
        if (((((uint8_t *)&t8))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t8) + 1, gm_src9vg8k4mbj38f, 548, 5);
        gm_b0_1 t9 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
    }
    {
        gm_b16_8 t10 = GM_LD(gm_b16_8, (((uint8_t *)&F.l2) + 0));
        gm_b16_8 t11 = GM_LD(gm_b16_8, ((uint8_t *)&t10));
        uint8_t t12 = 0;
        F.ret = t11;
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
inline __attribute__((aligned(4))) double gm_f_big_float_jmiufgl(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_big_float_jmiufgl F;
    void * gm_l0; /* m */
    GM_STACK_CHECK(&F, 128u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tngtuiuskn938 t1;
    gm_f_from_int_ofjuoqh((void *)&t1, (void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)((uint8_t *)a1)));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    double t2 = gm_f_to_f64_4avhrht((void *)NULL, cx, NULL, &t1);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.ret = t2;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_bigint_i128_p6m54kv(void *gm_out, const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_bigint_i128_p6m54kv F;
    GM_STACK_CHECK_SHALLOW(&F, 0u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_f_to_i128_o7ud1ue((void *)&(*(gm_ta24rd13gupia *)gm_out), &(*(gm_tbci5ospk5nnn *)((uint8_t *)a0)));
    goto gm_fdone;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) uint8_t gm_f_bit_s517clu(void *env, gm_node *cx, gm_node *inst, uint32_t a0) {
    struct gm_fr_bit_s517clu F;
    uint32_t gm_l0; /* i */
    GM_STACK_CHECK_SHALLOW(&F, 75u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    unsigned __int128 t1 = GM_LD(unsigned __int128, ((uint8_t *)&((struct gm_fr_builtin_psq6ql3 *)((&F.h)->up))->l100));
    uint32_t t2 = gm_l0;
    unsigned __int128 t3 = (unsigned __int128)t2;
    unsigned __int128 n4 = (unsigned __int128)t3;
    if (n4 >= 128) gm_trap_shift((uint64_t)n4, "u128", gm_srcue4ionbe9lgh, 594, 43);
    unsigned __int128 t5 = (unsigned __int128)(t1 >> (int)n4);
    unsigned __int128 t6 = (unsigned __int128)(t5 & ((unsigned __int128)(((unsigned __int128)0u << 64) | 1u)));
    uint8_t t7 = ((t6 == ((unsigned __int128)(((unsigned __int128)0u << 64) | 1u))));
    F.ret = t7;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint64_t gm_f_byte_at_m94bj3e(void * a0, uint64_t a1, uint64_t a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_byte_at_m94bj3e F;
    void * gm_l0; /* m */
    uint64_t gm_l1; /* i */
    uint64_t gm_l2; /* n */
    GM_STACK_CHECK_SHALLOW(&F, 129u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    uint64_t t1;
    void * t3 = gm_l0;
    void * t4 = GM_LD(void *, (((uint8_t *)t3) + 0));
    void * t5 = GM_LD(void *, (((uint8_t *)t4) + 0));
    uint8_t t6 = GM_LD(uint8_t, ((((uint8_t *)t5) + 88) + 8));
    if (!t6) goto ie2;
    uint64_t t8 = gm_l1;
    uint64_t t9 = t8;
    t1 = t9;
    goto ix2;
ie2:;
    uint64_t t11 = gm_l2;
    uint64_t r12; bool o13 = __builtin_sub_overflow(t11, ((uint64_t)1ULL), &r12);
    if (o13) gm_trap("integer overflow in `-`", gm_srcue4ionbe9lgh, 835, 98);
    uint64_t t14 = r12;
    uint64_t t15 = gm_l1;
    uint64_t r16; bool o17 = __builtin_sub_overflow(t14, t15, &r16);
    if (o17) gm_trap("integer overflow in `-`", gm_srcue4ionbe9lgh, 835, 98);
    uint64_t t18 = r16;
    uint64_t t19 = t18;
    t1 = t19;
ix2:;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) int32_t gm_f_cmp_9aseneh(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1) {
    struct gm_fr_cmp_9aseneh F;
    GM_STACK_CHECK(&F, 108u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_tbci5ospk5nnn t1;
    gm_f_mul_nfj5rjv((void *)&t1, (void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)(((uint8_t *)a0) + 0)), &(*(gm_tbci5ospk5nnn *)(((uint8_t *)a1) + 48)));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tbci5ospk5nnn t2;
    gm_f_mul_nfj5rjv((void *)&t2, (void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)(((uint8_t *)a1) + 0)), &(*(gm_tbci5ospk5nnn *)(((uint8_t *)a0) + 48)));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    int32_t t3 = gm_f_cmp_i1knrgu(&t1, &t2);
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
inline __attribute__((aligned(4))) gm_t9805qtonbsag gm_f_const_id_2mlh0og(void * a0, gm_cseahvg8v7p0vs a1, gm_csguankko4lmhd a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_const_id_2mlh0og F;
    void * gm_l0; /* s */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_t88rcucg1b7uk gm_l2; /* ty */
    GM_STACK_CHECK(&F, 96u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_tl874t2atnlh3, a1);
    gm_l2 = GM_CONV(gm_t88rcucg1b7uk, a2);
    void * t1 = gm_l0;
    void * t2 = (void *)(((uint8_t *)t1) + 2008);
    gm_tl874t2atnlh3 t3 = gm_l1;
    gm_t88rcucg1b7uk t4 = gm_l2;
    gm_trlabqsjjp0ds t5;
    GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t5 + 0), t3);
    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t5 + 12), t4);
    gm_t9805qtonbsag t6 = gm_f_get_pk8ncss(t2, t5);
    F.ret = t6;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_copy_pmkg67r(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_copy_pmkg67r F;
    GM_STACK_CHECK(&F, 43u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_go2qg0b((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3 = GM_LD(gm_b16_8, ((((uint8_t *)a0) + 8) + 0));
        gm_b16_8 t4 = GM_LD(gm_b16_8, ((uint8_t *)&t3));
        gm_tftb1odc739t5 t5 = gm_f_extend_7hs67uj(t2, t4);
        if (((((uint8_t *)&t5))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t5) + 1, gm_srclv3pj5es0f8o, 330, 5);
        gm_b0_1 t6 = GM_LD(gm_b0_1, (((uint8_t *)&t5) + 1));
    }
    {
        memset((uint8_t *)&(*(gm_tbci5ospk5nnn *)gm_out) + 1, 0, 7);
        uint8_t t7 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tbci5ospk5nnn *)gm_out) + 0, t7);
        memcpy((uint8_t *)&(*(gm_tbci5ospk5nnn *)gm_out) + 8, ((uint8_t *)&F.l1), 40);
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
inline __attribute__((aligned(4))) void gm_f_div_f0qrfbr(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1) {
    struct gm_fr_div_f0qrfbr F;
    GM_STACK_CHECK(&F, 192u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memcpy(&F.l0, a0, sizeof F.l0);
    {
        gm_tngtuiuskn938 t2;
        gm_f_div_rem_sc09dcc((void *)&t2, (void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l0)), &(*(gm_tbci5ospk5nnn *)((uint8_t *)a1)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tngtuiuskn938 t3 = t2;
        F.l2 = GM_LD(gm_tbci5ospk5nnn, (uint8_t *)&t3 + 0);
    }
    {
        memmove(gm_out, ((uint8_t *)&F.l2), 48);
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
inline __attribute__((aligned(4))) void gm_f_div_floor_u3mfihk(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1) {
    struct gm_fr_div_floor_u3mfihk F;
    GM_STACK_CHECK(&F, 192u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_tngtuiuskn938 t2;
        gm_f_div_mod_floor_eqv1ila((void *)&t2, (void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)((uint8_t *)a0)), &(*(gm_tbci5ospk5nnn *)((uint8_t *)a1)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tngtuiuskn938 t3 = t2;
        F.l2 = GM_LD(gm_tbci5ospk5nnn, (uint8_t *)&t3 + 0);
    }
    {
        memmove(gm_out, ((uint8_t *)&F.l2), 48);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_84b0b46(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_84b0b46 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_dheabhi(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_dheabhi F;
    void * gm_l0; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 105u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        void * t2 = GM_INT_TO_PTR(((uint64_t)16ULL));
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
inline __attribute__((aligned(4))) uint8_t gm_f_eq_60fjn21(const void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_eq_60fjn21 F;
    GM_STACK_CHECK_SHALLOW(&F, 75u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    uint8_t t1 = 0;
    uint8_t t2 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    uint8_t t3 = GM_LD(uint8_t, (((uint8_t *)a1) + 0));
    uint8_t t4 = ((t2 == t3));
    if (t4) {
        gm_b16_8 t5 = GM_LD(gm_b16_8, ((((uint8_t *)a0) + 8) + 0));
        gm_b16_8 t6 = GM_LD(gm_b16_8, ((uint8_t *)&t5));
        gm_b16_8 t7 = GM_LD(gm_b16_8, ((((uint8_t *)a1) + 8) + 0));
        gm_b16_8 t8 = GM_LD(gm_b16_8, ((uint8_t *)&t7));
        int32_t t9 = gm_f_mag_cmp_s39veq6(t6, t8);
        uint8_t t10 = ((t9 == ((int32_t)0LL)));
        t1 = t10;
    } else t1 = 0;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_from_i128_1n0s49q(void *gm_out, void *env, gm_node *cx, gm_node *inst, __int128 a0) {
    struct gm_fr_from_i128_1n0s49q F;
    __int128 gm_l0; /* v */
    GM_STACK_CHECK(&F, 65u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        __int128 t2 = gm_l0;
        unsigned __int128 t3 = (t2 > ((__int128)(((unsigned __int128)0u << 64) | 0u)) ? (unsigned __int128)t2 - (unsigned __int128)((__int128)(((unsigned __int128)0u << 64) | 0u)) : (unsigned __int128)((__int128)(((unsigned __int128)0u << 64) | 0u)) - (unsigned __int128)t2);
        gm_f_from_u128_gl5fk0t((void *)&F.l1, (void *)NULL, cx, NULL, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        __int128 t4 = gm_l0;
        uint8_t t5 = (t4 < ((__int128)(((unsigned __int128)0u << 64) | 0u)));
        GM_ST(uint8_t, (((uint8_t *)&F.l1) + 0), t5);
    }
    {
        memmove(gm_out, ((uint8_t *)&F.l1), 48);
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
inline __attribute__((aligned(4))) void gm_f_from_int_ofjuoqh(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_from_int_ofjuoqh F;
    GM_STACK_CHECK(&F, 96u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memcpy(&F.l0, a0, sizeof F.l0);
    gm_tbci5ospk5nnn t1 = F.l0;
    gm_tbci5ospk5nnn t2;
    gm_f_one_08qg1op((void *)&t2, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    GM_ST(gm_tbci5ospk5nnn, (uint8_t *)&(*(gm_tngtuiuskn938 *)gm_out) + 0, t1);
    GM_ST(gm_tbci5ospk5nnn, (uint8_t *)&(*(gm_tngtuiuskn938 *)gm_out) + 48, t2);
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_even_3kgcpjs(const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_even_3kgcpjs F;
    GM_STACK_CHECK_SHALLOW(&F, 37u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    uint8_t t1 = 0;
    uint64_t t2 = GM_LD(uintptr_t, ((((uint8_t *)a0) + 8) + 0) + 8);
    uint8_t t3 = ((t2 == ((uint64_t)0ULL)));
    if (!t3) {
        uint64_t t4 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t5 = GM_LD(uintptr_t, ((((uint8_t *)a0) + 8) + 0) + 8);
        if (t4 >= (uintptr_t)(t5)) gm_trap_index(t4, t5, gm_srclv3pj5es0f8o, 344, 63);
        uint32_t t6 = GM_LD(uint32_t, ((uint8_t *)GM_LD(void *, ((((uint8_t *)a0) + 8) + 0)) + t4 * 4));
        uint32_t t7 = (uint32_t)(t6 & ((uint32_t)1ULL));
        uint8_t t8 = ((t7 == ((uint32_t)0ULL)));
        t1 = t8;
    } else t1 = 1;
    F.ret = t1;
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_integer_ab2ohk2(const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_integer_ab2ohk2 F;
    GM_STACK_CHECK_SHALLOW(&F, 11u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    uint64_t t1 = gm_f_bits_lr3of4i(&(*(gm_tbci5ospk5nnn *)(((uint8_t *)a0) + 48)));
    uint8_t t2 = ((t1 == ((uint64_t)1ULL)));
    F.ret = t2;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_negative_k87pp3n(const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_negative_k87pp3n F;
    GM_STACK_CHECK_SHALLOW(&F, 3u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    uint8_t t1 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_negative_rb46l9j(const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_negative_rb46l9j F;
    GM_STACK_CHECK_SHALLOW(&F, 3u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    uint8_t t1 = gm_f_is_negative_k87pp3n(&(*(gm_tbci5ospk5nnn *)(((uint8_t *)a0) + 0)));
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_zero_5aept6u(const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_zero_5aept6u F;
    GM_STACK_CHECK_SHALLOW(&F, 3u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    uint8_t t1 = gm_f_is_zero_8tcct9o(&(*(gm_tbci5ospk5nnn *)(((uint8_t *)a0) + 0)));
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_zero_8tcct9o(const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_zero_8tcct9o F;
    GM_STACK_CHECK_SHALLOW(&F, 11u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    uint64_t t1 = GM_LD(uintptr_t, ((((uint8_t *)a0) + 8) + 0) + 8);
    uint8_t t2 = ((t1 == ((uint64_t)0ULL)));
    F.ret = t2;
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
inline __attribute__((aligned(4))) void gm_f_list_0202sdh(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_0202sdh F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_dheabhi();
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
inline __attribute__((aligned(4))) void gm_f_list_nof7vfj(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_nof7vfj F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_rcd2jln();
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
inline __attribute__((aligned(4))) void gm_f_make_r4pbu77(void *gm_out, uint8_t a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_make_r4pbu77 F;
    uint8_t gm_l0; /* negative */
    GM_STACK_CHECK_SHALLOW(&F, 100u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&F.l1, a1, sizeof F.l1);
    {
        gm_trqfvmjtt4sij t2 = F.l1;
        F.l2 = t2;
    }
    {
        void * t3 = (void *)((uint8_t *)&F.l2);
        gm_f_trim_8rdppmk(t3);
    }
    {
        memset((uint8_t *)&(*(gm_tbci5ospk5nnn *)gm_out) + 1, 0, 7);
        uint8_t t4 = 0;
        uint8_t t5 = gm_l0;
        if (t5) {
            uint64_t t6 = GM_LD(uintptr_t, (((uint8_t *)&F.l2) + 0) + 8);
            uint8_t t7 = (t6 > ((uint64_t)0ULL));
            t4 = t7;
        } else t4 = 0;
        gm_trqfvmjtt4sij t8 = F.l2;
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tbci5ospk5nnn *)gm_out) + 0, t4);
        GM_ST(gm_trqfvmjtt4sij, (uint8_t *)&(*(gm_tbci5ospk5nnn *)gm_out) + 8, t8);
        goto gm_fdone;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) unsigned __int128 gm_f_mask_gbhl4hp(uint32_t a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_mask_gbhl4hp F;
    uint32_t gm_l0; /* bits */
    GM_STACK_CHECK_SHALLOW(&F, 157u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    unsigned __int128 t1;
    uint32_t t3 = gm_l0;
    uint8_t t4 = (t3 >= ((uint32_t)128ULL));
    if (!t4) goto ie2;
    unsigned __int128 t6 = (unsigned __int128)~((unsigned __int128)(((unsigned __int128)0u << 64) | 0u));
    unsigned __int128 t7 = t6;
    t1 = t7;
    goto ix2;
ie2:;
    uint32_t t9 = gm_l0;
    unsigned __int128 t10 = (unsigned __int128)t9;
    unsigned __int128 n11 = (unsigned __int128)t10;
    if (n11 >= 128) gm_trap_shift((uint64_t)n11, "u128", gm_srcue4ionbe9lgh, 14, 70);
    unsigned __int128 r12 = (unsigned __int128)((unsigned __int128)((unsigned __int128)(((unsigned __int128)0u << 64) | 1u)) << (int)n11);
    bool o13 = (unsigned __int128)(r12 >> (int)n11) != ((unsigned __int128)(((unsigned __int128)0u << 64) | 1u));
    if (o13) gm_trap("integer overflow in `<<`", gm_srcue4ionbe9lgh, 14, 70);
    unsigned __int128 t14 = r12;
    unsigned __int128 r15; bool o16 = __builtin_sub_overflow(t14, ((unsigned __int128)(((unsigned __int128)0u << 64) | 1u)), &r15);
    if (o16) gm_trap("integer overflow in `-`", gm_srcue4ionbe9lgh, 14, 69);
    unsigned __int128 t17 = r15;
    unsigned __int128 t18 = t17;
    t1 = t18;
ix2:;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_mul_nfj5rjv(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1) {
    struct gm_fr_mul_nfj5rjv F;
    GM_STACK_CHECK(&F, 107u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memcpy(&F.l0, a0, sizeof F.l0);
    memcpy(&F.l1, a1, sizeof F.l1);
    uint8_t t1 = GM_LD(uint8_t, (((uint8_t *)&F.l0) + 0));
    uint8_t t2 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
    uint8_t t3 = (!(t1 == t2));
    gm_b16_8 t4 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l0) + 8) + 0));
    gm_b16_8 t5 = GM_LD(gm_b16_8, ((uint8_t *)&t4));
    gm_b16_8 t6 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l1) + 8) + 0));
    gm_b16_8 t7 = GM_LD(gm_b16_8, ((uint8_t *)&t6));
    gm_trqfvmjtt4sij t8;
    gm_f_mag_mul_if5s38k((void *)&t8, (void *)NULL, cx, NULL, t5, t7);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_f_make_r4pbu77((void *)&(*(gm_tbci5ospk5nnn *)gm_out), t3, &t8);
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
inline __attribute__((aligned(4))) void gm_f_mul_od1e7nd(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1) {
    struct gm_fr_mul_od1e7nd F;
    GM_STACK_CHECK(&F, 96u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_tbci5ospk5nnn t1;
    gm_f_mul_nfj5rjv((void *)&t1, (void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)(((uint8_t *)a0) + 0)), &(*(gm_tbci5ospk5nnn *)(((uint8_t *)a1) + 0)));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tbci5ospk5nnn t2;
    gm_f_mul_nfj5rjv((void *)&t2, (void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)(((uint8_t *)a0) + 48)), &(*(gm_tbci5ospk5nnn *)(((uint8_t *)a1) + 48)));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_f_new_2jv6hag((void *)&(*(gm_tngtuiuskn938 *)gm_out), (void *)NULL, cx, NULL, &t1, &t2);
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
inline __attribute__((aligned(4))) void gm_f_neg_agd5c5k(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_neg_agd5c5k F;
    GM_STACK_CHECK(&F, 12u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memcpy(&F.l0, a0, sizeof F.l0);
    {
        gm_f_copy_pmkg67r((void *)&F.l1, (void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l0)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        uint8_t t2 = 0;
        uint8_t t3 = GM_LD(uint8_t, (((uint8_t *)&F.l0) + 0));
        uint8_t t4 = (uint8_t)!t3;
        if (t4) {
            uint64_t t5 = GM_LD(uintptr_t, ((((uint8_t *)&F.l0) + 8) + 0) + 8);
            uint8_t t6 = (t5 > ((uint64_t)0ULL));
            t2 = t6;
        } else t2 = 0;
        GM_ST(uint8_t, (((uint8_t *)&F.l1) + 0), t2);
    }
    {
        memmove(gm_out, ((uint8_t *)&F.l1), 48);
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
inline __attribute__((aligned(4))) void gm_f_one_08qg1op(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_one_08qg1op F;
    GM_STACK_CHECK(&F, 0u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_f_from_u64_ud13q5v((void *)&(*(gm_tbci5ospk5nnn *)gm_out), (void *)NULL, cx, NULL, ((uint64_t)1ULL));
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
inline __attribute__((aligned(4))) uint32_t gm_f_ptr_bits_glps5i7(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_ptr_bits_glps5i7 F;
    void * gm_l0; /* t */
    GM_STACK_CHECK_SHALLOW(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    uint64_t t2 = GM_LD(uint64_t, (((uint8_t *)t1) + 0));
    uint64_t r3; bool o4 = __builtin_mul_overflow(t2, ((uint64_t)8ULL), &r3);
    if (o4) gm_trap("integer overflow in `*`", gm_src4o48ksu43djd, 91, 49);
    uint64_t t5 = r3;
    uint32_t t6 = (uint32_t)t5;
    F.ret = t6;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_rem_ib4l1vl(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1) {
    struct gm_fr_rem_ib4l1vl F;
    GM_STACK_CHECK(&F, 192u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_tngtuiuskn938 t2;
        gm_f_div_rem_sc09dcc((void *)&t2, (void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)((uint8_t *)a0)), &(*(gm_tbci5ospk5nnn *)((uint8_t *)a1)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tngtuiuskn938 t3 = t2;
        F.l2 = GM_LD(gm_tbci5ospk5nnn, (uint8_t *)&t3 + 48);
    }
    {
        memmove(gm_out, ((uint8_t *)&F.l2), 48);
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
inline __attribute__((aligned(4))) void gm_f_shl_qqecl81(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, uint64_t a1) {
    struct gm_fr_shl_qqecl81 F;
    uint64_t gm_l1; /* n */
    GM_STACK_CHECK(&F, 89u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memcpy(&F.l0, a0, sizeof F.l0);
    gm_l1 = a1;
    uint8_t t1 = GM_LD(uint8_t, (((uint8_t *)&F.l0) + 0));
    gm_b16_8 t2 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l0) + 8) + 0));
    gm_b16_8 t3 = GM_LD(gm_b16_8, ((uint8_t *)&t2));
    uint64_t t4 = gm_l1;
    gm_trqfvmjtt4sij t5;
    gm_f_mag_shl_5df643p((void *)&t5, (void *)NULL, cx, NULL, t3, t4);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_f_make_r4pbu77((void *)&(*(gm_tbci5ospk5nnn *)gm_out), t1, &t5);
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
inline __attribute__((aligned(4))) void gm_f_sub_mler6q8(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1) {
    struct gm_fr_sub_mler6q8 F;
    GM_STACK_CHECK(&F, 192u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_tbci5ospk5nnn t1;
    gm_f_mul_nfj5rjv((void *)&t1, (void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)(((uint8_t *)a0) + 0)), &(*(gm_tbci5ospk5nnn *)(((uint8_t *)a1) + 48)));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tbci5ospk5nnn t2;
    gm_f_mul_nfj5rjv((void *)&t2, (void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)(((uint8_t *)a1) + 0)), &(*(gm_tbci5ospk5nnn *)(((uint8_t *)a0) + 48)));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tbci5ospk5nnn t3;
    gm_f_sub_pktm5cr((void *)&t3, (void *)NULL, cx, NULL, &t1, &t2);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tbci5ospk5nnn t4;
    gm_f_mul_nfj5rjv((void *)&t4, (void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)(((uint8_t *)a0) + 48)), &(*(gm_tbci5ospk5nnn *)(((uint8_t *)a1) + 48)));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_f_new_2jv6hag((void *)&(*(gm_tngtuiuskn938 *)gm_out), (void *)NULL, cx, NULL, &t3, &t4);
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
inline __attribute__((aligned(4))) void gm_f_sub_pktm5cr(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1) {
    struct gm_fr_sub_pktm5cr F;
    GM_STACK_CHECK(&F, 67u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memcpy(&F.l0, a0, sizeof F.l0);
    uint8_t t1 = GM_LD(uint8_t, (((uint8_t *)&F.l0) + 0));
    gm_b16_8 t2 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l0) + 8) + 0));
    gm_b16_8 t3 = GM_LD(gm_b16_8, ((uint8_t *)&t2));
    uint8_t t4 = GM_LD(uint8_t, (((uint8_t *)a1) + 0));
    uint8_t t5 = (uint8_t)!t4;
    gm_b16_8 t6 = GM_LD(gm_b16_8, ((((uint8_t *)a1) + 8) + 0));
    gm_b16_8 t7 = GM_LD(gm_b16_8, ((uint8_t *)&t6));
    gm_f_signed_add_ek0dpmo((void *)&(*(gm_tbci5ospk5nnn *)gm_out), (void *)NULL, cx, NULL, t1, t3, t5, t7);
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
    GM_STACK_CHECK_SHALLOW(&F, 202u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_t9p38b040grms t2;
        gm_f_to_u128_haka99r((void *)&t2, &(*(gm_tbci5ospk5nnn *)((uint8_t *)a0)));
        unsigned __int128 t3;
        if (((((uint8_t *)&t2))[16] != 0)) {
            unsigned __int128 t4 = GM_LD(unsigned __int128, (((uint8_t *)&t2) + 0));
            t3 = t4;
        } else {
            gm_tf5srh4mp4b3i t5;
            memset(&t5, 0, sizeof t5);
            uint8_t t6 = 0;
            F.ret = t5;
            goto gm_fret;
        }
        gm_l1 = t3;
    }
    {
        unsigned __int128 t7 = gm_l1;
        gm_tf5srh4mp4b3i t8;
        uint64_t t9 = (uint64_t)t7;
        gm_tf5srh4mp4b3i t10;
        GM_ST(uint8_t, (uint8_t *)&t10 + 9, 0);
        GM_ST(uint16_t, (uint8_t *)&t10 + 10, 0);
        GM_ST(uint32_t, (uint8_t *)&t10 + 12, 0);
        GM_ST(uint64_t, ((uint8_t *)&t10), t9);
        ((uint8_t *)&t10)[8] = 1;
        gm_tf5srh4mp4b3i t11;
        memset(&t11, 0, sizeof t11);
        t8 = ((unsigned __int128)(t7) <= (((unsigned __int128)0ULL << 64) | 18446744073709551615ULL)) ? t10 : t11;
        uint8_t t12 = 0;
        F.ret = t8;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_unit_value_evae6mr(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_unit_value_evae6mr F;
    GM_STACK_CHECK(&F, 56u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_dheabhi();
    gm_tnpifv20irnph t2;
    GM_ST(gm_b16_8, ((uint8_t *)&t2 + 0), t1);
    gm_tq7s98ihonkju t3 = gm_f_new_lcff781((void *)NULL, cx, NULL, t2, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kequv58cbtg)));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    if (((((uint8_t *)&t3))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t3) + 8, gm_src9vg8k4mbj38f, 133, 49);
    void * t4 = GM_LD(void *, (((uint8_t *)&t3) + 8));
    memset(&(*(gm_t6evimt84lndq *)gm_out), 0, sizeof (*(gm_t6evimt84lndq *)gm_out));
    GM_ST(uint8_t, (uint8_t *)&(*(gm_t6evimt84lndq *)gm_out) + 0, ((uint8_t)3ULL));
    GM_ST(void *, (uint8_t *)&(*(gm_t6evimt84lndq *)gm_out) + 16, t4);
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
inline __attribute__((aligned(4))) void gm_f_unsupported_2seq49l(void *gm_out, void * a0, gm_cseahvg8v7p0vs a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_unsupported_2seq49l F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_b16_8 gm_l2; /* what */
    GM_STACK_CHECK_SHALLOW(&F, 96u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_tl874t2atnlh3, a1);
    gm_l2 = a2;
    gm_tbgd14esf0jvi t1;
    memset((uint8_t *)&t1 + 12, 0, 4);
    gm_tl874t2atnlh3 t2 = gm_l1;
    GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t1 + 0, t2);
    gm_b16_8 t3 = gm_l2;
    GM_ST(gm_b16_8, (uint8_t *)&t1 + 16, t3);
    memset(&(*(gm_t39uai1ajk3qv *)gm_out), 0, sizeof (*(gm_t39uai1ajk3qv *)gm_out));
    GM_ST(uint8_t, (uint8_t *)&(*(gm_t39uai1ajk3qv *)gm_out) + 0, ((uint8_t)5ULL));
    GM_ST(gm_tbgd14esf0jvi, (uint8_t *)&(*(gm_t39uai1ajk3qv *)gm_out) + 16, t1);
    goto gm_fdone;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) unsigned __int128 gm_f_wrap_bt8r149(void * a0, unsigned __int128 a1, gm_csguankko4lmhd a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_wrap_bt8r149 F;
    void * gm_l0; /* m */
    unsigned __int128 gm_l1; /* bits */
    gm_t88rcucg1b7uk gm_l2; /* t */
    GM_STACK_CHECK_SHALLOW(&F, 132u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = GM_CONV(gm_t88rcucg1b7uk, a2);
    unsigned __int128 t1 = gm_l1;
    void * t2 = gm_l0;
    gm_t88rcucg1b7uk t3 = gm_l2;
    gm_ttovhfee8mf28 t4 = gm_f_int_info_2r9qb5n(t2, GM_CONV(gm_csguankko4lmhd, t3));
    uint32_t t5 = GM_LD(uint32_t, (((uint8_t *)&t4) + 4));
    unsigned __int128 t6 = gm_f_mask_gbhl4hp(t5);
    unsigned __int128 t7 = (unsigned __int128)(t1 & t6);
    F.ret = t7;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
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
extern __attribute__((aligned(4))) double gm_f_big_float_jmiufgl(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
inline __attribute__((aligned(4))) void gm_f_big_of_1cq0qao(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, unsigned __int128 a1, gm_t88rcucg1b7uk a2) {
    struct gm_fr_big_of_1cq0qao F;
    void * gm_l0; /* m */
    unsigned __int128 gm_l1; /* bits */
    gm_t88rcucg1b7uk gm_l2; /* t */
    GM_STACK_CHECK(&F, 109u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t3 = gm_l0;
        gm_t88rcucg1b7uk t4 = gm_l2;
        gm_ttovhfee8mf28 t5 = gm_f_int_info_2r9qb5n(t3, GM_CONV(gm_csguankko4lmhd, t4));
        uint8_t t6 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (!t6) goto ie2;
        {
            void * t8 = gm_l0;
            unsigned __int128 t9 = gm_l1;
            gm_t88rcucg1b7uk t10 = gm_l2;
            __int128 t11 = gm_f_int_value_6hmh8tj(t8, t9, GM_CONV(gm_csguankko4lmhd, t10));
            gm_f_from_i128_1n0s49q((void *)&(*(gm_tbci5ospk5nnn *)gm_out), (void *)NULL, cx, NULL, t11);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        unsigned __int128 t12 = gm_l1;
        gm_f_from_u128_gl5fk0t((void *)&(*(gm_tbci5ospk5nnn *)gm_out), (void *)NULL, cx, NULL, t12);
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
extern __attribute__((aligned(4))) void gm_f_bigint_i128_p6m54kv(void *gm_out, const void * a0);
__attribute__((aligned(4))) gm_ta24rd13gupia gm_f_bigint_i128_p6m54kv_c(gm_tbci5ospk5nnn a0) { gm_ta24rd13gupia gm_r; gm_f_bigint_i128_p6m54kv(&gm_r, &a0); return gm_r; }
__attribute__((aligned(4))) void gm_f_binary_fmoblrg(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_ts6oqppjh0ba3 a1, const void * a2, const void * a3, gm_t88rcucg1b7uk a4, gm_t88rcucg1b7uk a5, gm_tl874t2atnlh3 a6) {
    struct gm_fr_binary_fmoblrg F;
    void * gm_l0; /* m */
    gm_ts6oqppjh0ba3 gm_l1; /* op */
    gm_t88rcucg1b7uk gm_l4; /* lt */
    gm_t88rcucg1b7uk gm_l5; /* rt */
    gm_tl874t2atnlh3 gm_l6; /* span */
    gm_tlcm4lvu3g1a8 gm_l7; /* lp */
    void * gm_l8; /* xa */
    gm_b16_8 gm_l10; /* <tmp> */
    uint64_t gm_l11; /* <tmp> */
    uint64_t gm_l12; /* i */
    void * gm_l15; /* ya */
    gm_t88rcucg1b7uk gm_l16; /* rlane */
    GM_STACK_CHECK(&F, 1092u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l4 = a4;
    gm_l5 = a5;
    gm_l6 = a6;
    {
        void * t3 = gm_l0;
        void * t4 = GM_LD(void *, (((uint8_t *)t3) + 0));
        void * t5 = (void *)(((uint8_t *)t4) + 8);
        gm_t88rcucg1b7uk t6 = gm_l4;
        gm_tb7u19loo3j87 t7;
        gm_f_vector_parts_crrfopm((void *)&t7, t5, GM_CONV(gm_csguankko4lmhd, t6));
        if (!((((uint8_t *)&t7))[16] != 0)) goto ie2;
        gm_tlcm4lvu3g1a8 t8 = GM_LD(gm_tlcm4lvu3g1a8, ((uint8_t *)&t7));
        gm_l7 = t8;
        {
            uint8_t t11 = GM_LD(uint8_t, (((uint8_t *)a2) + 0));
            if (t11 != ((uint8_t)3ULL)) goto ie10;
            void * t12 = GM_LD(void *, (((uint8_t *)a2) + 16));
            gm_l8 = t12;
            {
                gm_f_list_0202sdh((void *)&F.l9, (void *)NULL, cx, NULL);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                {
                    void * t15 = gm_l8;
                    gm_b16_8 t16 = GM_LD(gm_b16_8, (((uint8_t *)t15) + 0));
                    gm_b16_8 t17 = GM_LD(gm_b16_8, ((uint8_t *)&t16));
                    gm_l10 = t17;
                }
                {
                    gm_l11 = ((uint64_t)0ULL);
                }
                {
                    for (;;) {
                        uint64_t t19 = gm_l11;
                        uint64_t t20 = GM_LD(uintptr_t, ((uint8_t *)&gm_l10) + 8);
                        uint8_t t21 = (t19 < t20);
                        if (!t21) goto lb18;
                        {
                            gm_thf8as07kj3uj t24;
                            memset((uint8_t *)&t24 + 8, 0, 8);
                            uint64_t t25 = gm_l11;
                            GM_ST(uint64_t, (uint8_t *)&t24 + 0, t25);
                            uint64_t t26 = gm_l11;
                            uint64_t t27 = (uintptr_t)t26;
                            uint64_t t28 = GM_LD(uintptr_t, ((uint8_t *)&gm_l10) + 8);
                            memcpy((uint8_t *)&t24 + 16, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l10)) + t27 * 48), 48);
                            uint64_t t31 = GM_LD(uint64_t, (((uint8_t *)&t24) + 0));
                            gm_l12 = t31;
                            F.l13 = (*(gm_t6evimt84lndq *)(((uint8_t *)&t24) + 16));
                            {
                                gm_t6evimt84lndq t33 = GM_LD(gm_t6evimt84lndq, ((uint8_t *)a3));
                                F.l14 = t33;
                            }
                            {
                                uint8_t t35 = GM_LD(uint8_t, (((uint8_t *)a3) + 0));
                                if (t35 != ((uint8_t)3ULL)) goto ie34;
                                void * t36 = GM_LD(void *, (((uint8_t *)a3) + 16));
                                gm_l15 = t36;
                                {
                                    uint64_t t38 = gm_l12;
                                    uint64_t t39 = (uintptr_t)t38;
                                    void * t40 = gm_l15;
                                    uint64_t t41 = GM_LD(uintptr_t, (((uint8_t *)t40) + 0) + 8);
                                    if (t39 >= (uintptr_t)(t41)) gm_trap_index(t39, t41, gm_srcue4ionbe9lgh, 113, 36);
                                    gm_t6evimt84lndq t42 = GM_LD(gm_t6evimt84lndq, ((uint8_t *)GM_LD(void *, (((uint8_t *)t40) + 0)) + t39 * 48));
                                    F.l14 = t42;
                                }
                                goto ix34;
ie34:;
ix34:;
                            }
                            {
                                gm_t88rcucg1b7uk t43;
                                void * t45 = gm_l0;
                                void * t46 = GM_LD(void *, (((uint8_t *)t45) + 0));
                                void * t47 = (void *)(((uint8_t *)t46) + 8);
                                gm_t88rcucg1b7uk t48 = gm_l5;
                                gm_tb7u19loo3j87 t49;
                                gm_f_vector_parts_crrfopm((void *)&t49, t47, GM_CONV(gm_csguankko4lmhd, t48));
                                uint8_t t50;
                                if (((((uint8_t *)&t49))[16] != 0)) goto mn52;
                                t50 = ((uint8_t)0);
                                goto me51;
mn52:;
                                t50 = ((uint8_t)1);
                                goto me51;
mn53:;
                                gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 114, 25);
me51:;
                                if (!t50) goto ie44;
                                gm_t88rcucg1b7uk t55 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l7) + 8));
                                gm_t88rcucg1b7uk t56 = t55;
                                t43 = t56;
                                goto ix44;
ie44:;
                                gm_t88rcucg1b7uk t58 = gm_l5;
                                gm_t88rcucg1b7uk t59 = t58;
                                t43 = t59;
ix44:;
                                gm_l16 = t43;
                            }
                            {
                                void * t60 = (void *)((uint8_t *)&F.l9);
                                void * t61 = gm_l0;
                                gm_ts6oqppjh0ba3 t62 = gm_l1;
                                gm_t88rcucg1b7uk t63 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l7) + 8));
                                gm_t88rcucg1b7uk t64 = gm_l16;
                                gm_tl874t2atnlh3 t65 = gm_l6;
                                gm_ttk92ufj2f6eh t66;
                                gm_f_scalar_binary_bvc65kt((void *)&t66, (void *)NULL, cx, NULL, t61, t62, &(*(gm_t6evimt84lndq *)((uint8_t *)&F.l13)), &(*(gm_t6evimt84lndq *)((uint8_t *)&F.l14)), t63, t64, t65);
                                if (gm_unw.kind) {
                                    goto gm_ll22;
                                }
                                if (((((uint8_t *)&t66))[0] != 0)) {
                                    gm_ttk92ufj2f6eh t67;
                                    GM_ST(uint8_t, (uint8_t *)&t67 + 1, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t67 + 2, 0);
                                    GM_ST(uint32_t, (uint8_t *)&t67 + 4, 0);
                                    GM_ST(uint64_t, (uint8_t *)&t67 + 8, 0);
                                    ((uint8_t *)&t67)[0] = 1;
                                    GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t67 + 16), (*(gm_t39uai1ajk3qv *)(((uint8_t *)&t66) + 16)));
                                    F.ret = t67;
                                    goto gm_fret;
                                }
                                gm_tftb1odc739t5 t68 = gm_f_push_0pgacsj(t60, &(*(gm_t6evimt84lndq *)(((uint8_t *)&t66) + 16)));
                                if (((((uint8_t *)&t68))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t68) + 1, gm_srcue4ionbe9lgh, 115, 13);
                                gm_b0_1 t69 = GM_LD(gm_b0_1, (((uint8_t *)&t68) + 1));
                            }
                            goto me29;
mn30:;
                            gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 111, 9);
me29:;
                        }
lc18:;
                        {
                            uint64_t t71 = gm_l11;
                            uint64_t r72; bool o73 = __builtin_add_overflow(t71, ((uint64_t)1ULL), &r72);
                            uint64_t t74 = r72;
                            gm_l11 = t74;
                        }
                    }
gm_ll22:;
                    if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb18; }
                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc18; }
                    }
                    goto gm_lad;
lb18:;
                }
            }
            {
                gm_b16_8 t75 = GM_LD(gm_b16_8, (((uint8_t *)&F.l9) + 0));
                gm_tnpifv20irnph t76;
                GM_ST(gm_b16_8, ((uint8_t *)&t76 + 0), t75);
                gm_tq7s98ihonkju t77 = gm_f_new_lcff781((void *)NULL, cx, NULL, t76, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kha5d1oq7pb)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t77))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t77) + 8, gm_srcue4ionbe9lgh, 117, 21);
                void * t78 = GM_LD(void *, (((uint8_t *)&t77) + 8));
                gm_t6evimt84lndq t79;
                memset(&t79, 0, sizeof t79);
                GM_ST(uint8_t, ((uint8_t *)&t79 + 0), ((uint8_t)3ULL));
                GM_ST(void *, ((uint8_t *)&t79 + 16), t78);
                memset(&(*(gm_ttk92ufj2f6eh *)gm_out), 0, sizeof (*(gm_ttk92ufj2f6eh *)gm_out));
                ((uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out))[0] = 0;
                GM_ST(gm_t6evimt84lndq, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 16, t79);
                goto gm_fdone;
            }
            goto ix10;
ie10:;
            {
                void * t81 = gm_l0;
                gm_tl874t2atnlh3 t82 = gm_l6;
                gm_b16_8 t83;
                GM_ST(void *, (uint8_t *)&t83, (void *)(gm_s0dggtblbl7jq));
                GM_ST(uintptr_t, (uint8_t *)&t83 + 8, (uintptr_t)(29));
                gm_t39uai1ajk3qv t84;
                gm_f_unsupported_2seq49l((void *)&t84, t81, GM_CONV(gm_cseahvg8v7p0vs, t82), t83);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 8, 0);
                ((uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out))[0] = 1;
                GM_ST(gm_t39uai1ajk3qv, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 16, t84);
                goto gm_fdone;
            }
ix10:;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t85 = gm_l0;
        gm_ts6oqppjh0ba3 t86 = gm_l1;
        gm_t88rcucg1b7uk t87 = gm_l4;
        gm_t88rcucg1b7uk t88 = gm_l5;
        gm_tl874t2atnlh3 t89 = gm_l6;
        gm_f_scalar_binary_bvc65kt((void *)&(*(gm_ttk92ufj2f6eh *)gm_out), (void *)NULL, cx, NULL, t85, t86, &(*(gm_t6evimt84lndq *)((uint8_t *)a2)), &(*(gm_t6evimt84lndq *)((uint8_t *)a3)), t87, t88, t89);
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
extern __attribute__((aligned(4))) uint8_t gm_f_bit_s517clu(void *env, gm_node *cx, gm_node *inst, uint32_t a0);
__attribute__((aligned(4))) void gm_f_builtin_psq6ql3(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tvdg94bk7aa22 a1, gm_b16_8 a2, void * a3) {
    struct gm_fr_builtin_psq6ql3 F;
    void * gm_l0; /* m */
    gm_tvdg94bk7aa22 gm_l1; /* op */
    gm_b16_8 gm_l2; /* args */
    void * gm_l3; /* e */
    void * gm_l4; /* tys */
    gm_tl874t2atnlh3 gm_l5; /* span */
    uint8_t gm_l6; /* ok */
    gm_b16_8 gm_l8; /* text */
    gm_b16_8 gm_l9; /* vals */
    gm_t88rcucg1b7uk gm_l11; /* at */
    unsigned __int128 gm_l12; /* bits */
    uint8_t gm_l13; /* b */
    double gm_l14; /* f */
    uint32_t gm_l15; /* i */
    unsigned __int128 gm_l17; /* bits */
    double gm_l18; /* f */
    gm_t88rcucg1b7uk gm_l20; /* t */
    unsigned __int128 gm_l21; /* bits */
    double gm_l24; /* f */
    unsigned __int128 gm_l29; /* bits */
    double gm_l30; /* f */
    unsigned __int128 gm_l33; /* a */
    unsigned __int128 gm_l34; /* b */
    int32_t gm_l35; /* c */
    double gm_l36; /* a */
    double gm_l37; /* b */
    unsigned __int128 gm_l38; /* v */
    unsigned __int128 gm_l39; /* lo */
    unsigned __int128 gm_l40; /* hi */
    unsigned __int128 gm_l42; /* bits */
    double gm_l46; /* f */
    unsigned __int128 gm_l47; /* a */
    unsigned __int128 gm_l48; /* b */
    gm_t9qkh0rsooc8d gm_l50; /* flavor */
    unsigned __int128 gm_l51; /* base */
    unsigned __int128 gm_l52; /* n */
    gm_t88rcucg1b7uk gm_l54; /* t */
    uint8_t gm_l56; /* fits */
    gm_b16_8 gm_l57; /* text */
    double gm_l58; /* f */
    double gm_l59; /* f */
    double gm_l60; /* f */
    double gm_l61; /* f */
    uint32_t gm_l62; /* i */
    uint64_t gm_l64; /* n */
    gm_tab260m0n3sqj gm_l65; /* start */
    void * gm_l66; /* a */
    gm_to6glrduves6o gm_l67; /* sl */
    unsigned __int128 gm_l68; /* bits */
    __int128 gm_l69; /* i */
    uint64_t gm_l70; /* k */
    gm_tkaapcii86udk gm_l71; /* pl */
    gm_tkaapcii86udk gm_l72; /* pl */
    gm_tkaapcii86udk gm_l73; /* pl */
    void * gm_l74; /* a */
    gm_to6glrduves6o gm_l76; /* d */
    gm_to6glrduves6o gm_l77; /* s */
    uint64_t gm_l79; /* <tmp> */
    uint64_t gm_l80; /* <tmp> */
    uint64_t gm_l81; /* k */
    gm_b16_8 gm_l82; /* <tmp> */
    uint64_t gm_l83; /* <tmp> */
    uint64_t gm_l84; /* k */
    void * gm_l86; /* inner */
    gm_tkaapcii86udk gm_l88; /* pl */
    uint64_t gm_l89; /* elem */
    gm_t88rcucg1b7uk gm_l90; /* t */
    gm_t88rcucg1b7uk gm_l91; /* t */
    uint64_t gm_l92; /* index */
    uint64_t gm_l93; /* base */
    unsigned __int128 gm_l94; /* bits */
    double gm_l96; /* f */
    unsigned __int128 gm_l97; /* raw */
    gm_ttovhfee8mf28 gm_l98; /* info */
    uint32_t gm_l99; /* n */
    gm_closure gm_l101; /* bit */
    unsigned __int128 gm_l102; /* r */
    uint32_t gm_l103; /* c */
    uint32_t gm_l104; /* c */
    uint32_t gm_l105; /* c */
    uint32_t gm_l106; /* <tmp> */
    uint32_t gm_l107; /* <tmp> */
    uint32_t gm_l108; /* i */
    uint32_t gm_l109; /* <tmp> */
    uint32_t gm_l110; /* <tmp> */
    uint32_t gm_l111; /* i */
    uint32_t gm_l112; /* <tmp> */
    uint32_t gm_l113; /* <tmp> */
    uint32_t gm_l114; /* i */
    unsigned __int128 gm_l115; /* amount_bits */
    uint32_t gm_l117; /* k */
    uint32_t gm_l118; /* <tmp> */
    uint32_t gm_l119; /* <tmp> */
    uint32_t gm_l120; /* i */
    gm_tvdg94bk7aa22 gm_l121; /* other */
    GM_STACK_CHECK(&F, 22286u);
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
        void * t5 = gm_l3;
        gm_tl874t2atnlh3 t6 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t5) + 72));
        gm_l5 = t6;
    }
    {
        uint8_t t12 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        switch (t12) { case ((uint8_t)0ULL): goto ma10; case ((uint8_t)1ULL): goto ma10; case ((uint8_t)2ULL): goto ma10; case ((uint8_t)3ULL): goto ma10; case ((uint8_t)4ULL): goto ma10; case ((uint8_t)5ULL): goto ma10; case ((uint8_t)6ULL): goto ma10; case ((uint8_t)7ULL): goto ma10; case ((uint8_t)8ULL): goto ma10; case ((uint8_t)9ULL): goto ma10; case ((uint8_t)10ULL): goto ma10; case ((uint8_t)11ULL): goto ma10; case ((uint8_t)12ULL): goto ma10; case ((uint8_t)13ULL): goto ma10; case ((uint8_t)14ULL): goto ma10; case ((uint8_t)15ULL): goto ma10; case ((uint8_t)16ULL): goto ma10; case ((uint8_t)17ULL): goto ma10; case ((uint8_t)18ULL): goto ma10; case ((uint8_t)19ULL): goto ma10; case ((uint8_t)20ULL): goto ma10; case ((uint8_t)21ULL): goto ma10; case ((uint8_t)22ULL): goto ma10; case ((uint8_t)23ULL): goto ma10; case ((uint8_t)24ULL): goto ma10; case ((uint8_t)25ULL): goto ma10; case ((uint8_t)26ULL): goto ma10; case ((uint8_t)27ULL): goto ma10; case ((uint8_t)28ULL): goto ma10; case ((uint8_t)29ULL): goto ma10; case ((uint8_t)30ULL): goto ma10; case ((uint8_t)31ULL): goto ma10; case ((uint8_t)32ULL): goto ma10; case ((uint8_t)33ULL): goto ma10; case ((uint8_t)34ULL): goto ma10; case ((uint8_t)35ULL): goto ma10; case ((uint8_t)36ULL): goto ma10; case ((uint8_t)37ULL): goto ma10; case ((uint8_t)38ULL): goto ma10; case ((uint8_t)39ULL): goto ma10; case ((uint8_t)40ULL): goto ma10; case ((uint8_t)41ULL): goto ma10; case ((uint8_t)42ULL): goto ma10; case ((uint8_t)43ULL): goto ma10; case ((uint8_t)44ULL): goto ma10; case ((uint8_t)45ULL): goto ma10; case ((uint8_t)46ULL): goto ma10; case ((uint8_t)47ULL): goto ma10; case ((uint8_t)48ULL): goto ma10; case ((uint8_t)49ULL): goto ma10; case ((uint8_t)50ULL): goto ma10; case ((uint8_t)51ULL): goto ma10; case ((uint8_t)52ULL): goto ma10; case ((uint8_t)53ULL): goto ma10; case ((uint8_t)54ULL): goto ma10; case ((uint8_t)55ULL): goto ma10; case ((uint8_t)56ULL): goto ma10; case ((uint8_t)57ULL): goto ma10; case ((uint8_t)58ULL): goto ma10; case ((uint8_t)59ULL): goto ma10; case ((uint8_t)60ULL): goto ma10; case ((uint8_t)61ULL): goto ma9; case ((uint8_t)62ULL): goto ma10; case ((uint8_t)63ULL): goto ma8; case ((uint8_t)64ULL): goto ma10; case ((uint8_t)65ULL): goto ma10; case ((uint8_t)66ULL): goto ma10; case ((uint8_t)67ULL): goto ma10; case ((uint8_t)68ULL): goto ma10; case ((uint8_t)69ULL): goto ma10; case ((uint8_t)70ULL): goto ma10; case ((uint8_t)71ULL): goto ma10; case ((uint8_t)72ULL): goto ma10; case ((uint8_t)73ULL): goto ma10; case ((uint8_t)74ULL): goto ma10; case ((uint8_t)75ULL): goto ma10; case ((uint8_t)76ULL): goto ma10; case ((uint8_t)77ULL): goto ma10; case ((uint8_t)78ULL): goto ma10; case ((uint8_t)79ULL): goto ma10; case ((uint8_t)80ULL): goto ma10; case ((uint8_t)81ULL): goto ma10; default: goto ma10; }
ma8:;
        uint8_t t14 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t14 != ((uint8_t)63ULL)) goto mn13;
        void * t15 = gm_l0;
        gm_tl874t2atnlh3 t16 = gm_l5;
        gm_b16_8 t17;
        GM_ST(void *, (uint8_t *)&t17, (void *)(gm_sd3b7a2bnmpef));
        GM_ST(uintptr_t, (uint8_t *)&t17 + 8, (uintptr_t)(24));
        gm_t39uai1ajk3qv t18;
        gm_f_trap_jftbss6((void *)&t18, (void *)NULL, cx, NULL, t15, t16, t17);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(uint8_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 8, 0);
        ((uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out))[0] = 1;
        GM_ST(gm_t39uai1ajk3qv, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 16, t18);
        goto gm_fdone;
        goto me7;
mn13:;
ma9:;
        uint8_t t20 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t20 != ((uint8_t)61ULL)) goto mn19;
        {
            void * t23 = gm_l0;
            uint64_t t24 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t25 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
            if (t24 >= (uintptr_t)(t25)) gm_trap_index(t24, t25, gm_srcue4ionbe9lgh, 358, 39);
            void * t26 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t24 * 88);
            gm_ttk92ufj2f6eh t27;
            gm_f_eval_2vbtp1v((void *)&t27, (void *)NULL, cx, NULL, t23, t26);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t27))[0] != 0)) {
                gm_ttk92ufj2f6eh t28;
                GM_ST(uint8_t, (uint8_t *)&t28 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t28 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t28 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t28 + 8, 0);
                ((uint8_t *)&t28)[0] = 1;
                GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t28 + 16), (*(gm_t39uai1ajk3qv *)(((uint8_t *)&t27) + 16)));
                F.ret = t28;
                goto gm_fret;
            }
            uint8_t t29 = GM_LD(uint8_t, ((((uint8_t *)&t27) + 16) + 0));
            if (t29 != ((uint8_t)2ULL)) goto ie22;
            uint8_t t30 = GM_LD(uint8_t, ((((uint8_t *)&t27) + 16) + 16));
            gm_l6 = t30;
            {
                void * t32 = gm_l0;
                uint64_t t33 = (uintptr_t)((uint64_t)1ULL);
                uint64_t t34 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                if (t33 >= (uintptr_t)(t34)) gm_trap_index(t33, t34, gm_srcue4ionbe9lgh, 359, 32);
                void * t35 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t33 * 88);
                gm_ttk92ufj2f6eh t36;
                gm_f_eval_2vbtp1v((void *)&t36, (void *)NULL, cx, NULL, t32, t35);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t36))[0] != 0)) {
                    gm_ttk92ufj2f6eh t37;
                    GM_ST(uint8_t, (uint8_t *)&t37 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t37 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t37 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t37 + 8, 0);
                    ((uint8_t *)&t37)[0] = 1;
                    GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t37 + 16), (*(gm_t39uai1ajk3qv *)(((uint8_t *)&t36) + 16)));
                    F.ret = t37;
                    goto gm_fret;
                }
                F.l7 = (*(gm_t6evimt84lndq *)(((uint8_t *)&t36) + 16));
            }
            {
                uint8_t t39 = gm_l6;
                if (!t39) goto ie38;
                {
                    gm_t6evimt84lndq t41;
                    gm_f_unit_value_evae6mr((void *)&t41, (void *)NULL, cx, NULL);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    memset(&(*(gm_ttk92ufj2f6eh *)gm_out), 0, sizeof (*(gm_ttk92ufj2f6eh *)gm_out));
                    ((uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out))[0] = 0;
                    GM_ST(gm_t6evimt84lndq, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 16, t41);
                    goto gm_fdone;
                }
                goto ix38;
ie38:;
ix38:;
            }
            {
                void * t42 = gm_l0;
                gm_th2lqbo77pvsc t43;
                gm_f_string_of_plna4gu((void *)&t43, (void *)NULL, cx, NULL, t42, &(*(gm_t6evimt84lndq *)((uint8_t *)&F.l7)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_b16_8 t44;
                if (((((uint8_t *)&t43))[16] != 0)) {
                    gm_b16_8 t45 = GM_LD(gm_b16_8, (((uint8_t *)&t43) + 0));
                    t44 = t45;
                } else {
                    void * t46 = gm_l0;
                    gm_tl874t2atnlh3 t47 = gm_l5;
                    gm_b16_8 t48;
                    GM_ST(void *, (uint8_t *)&t48, (void *)(gm_smqhtg33814q9));
                    GM_ST(uintptr_t, (uint8_t *)&t48 + 8, (uintptr_t)(22));
                    gm_t39uai1ajk3qv t49;
                    gm_f_unsupported_2seq49l((void *)&t49, t46, GM_CONV(gm_cseahvg8v7p0vs, t47), t48);
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 8, 0);
                    ((uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out))[0] = 1;
                    GM_ST(gm_t39uai1ajk3qv, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 16, t49);
                    goto gm_fdone;
                }
                gm_l8 = t44;
            }
            {
                void * t50 = gm_l0;
                gm_tl874t2atnlh3 t51 = gm_l5;
                gm_b16_8 t52;
                gm_b16_8 t54 = gm_l8;
                gm_b16_8 t55 = GM_LD(gm_b16_8, ((uint8_t *)&t54));
                uint8_t t56 = (((GM_LD(uintptr_t, ((uint8_t *)&t55) + 8) == 16) && memcmp(GM_LD(void *, ((uint8_t *)&t55)), gm_spi8n2atkg603, 16) == 0));
                if (!t56) goto ie53;
                gm_b16_8 t58 = gm_l8;
                gm_b16_8 t59 = t58;
                gm_b16_8 t60 = GM_LD(gm_b16_8, ((uint8_t *)&t59));
                t52 = t60;
                goto ix53;
ie53:;
                gm_b16_8 t62 = gm_l8;
                gm_tnpifv20irnph t63;
                GM_ST(gm_b16_8, ((uint8_t *)&t63 + 0), t62);
                gm_tabb710d9mq2e t64;
                gm_f_format_u0vfg08((void *)&t64, (void *)NULL, cx, NULL, t63);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t64))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t64) + 8, gm_srcue4ionbe9lgh, 362, 80);
                gm_b16_8 t65 = GM_LD(gm_b16_8, (((uint8_t *)&t64) + 8));
                gm_b16_8 t66 = t65;
                gm_b16_8 t67 = GM_LD(gm_b16_8, ((uint8_t *)&t66));
                t52 = t67;
ix53:;
                gm_t39uai1ajk3qv t68;
                gm_f_trap_jftbss6((void *)&t68, (void *)NULL, cx, NULL, t50, t51, t52);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(uint8_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 8, 0);
                ((uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out))[0] = 1;
                GM_ST(gm_t39uai1ajk3qv, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 16, t68);
                goto gm_fdone;
            }
            goto ix22;
ie22:;
            {
                void * t70 = gm_l0;
                gm_tl874t2atnlh3 t71 = gm_l5;
                gm_b16_8 t72;
                GM_ST(void *, (uint8_t *)&t72, (void *)(gm_s2kubr08sqj72));
                GM_ST(uintptr_t, (uint8_t *)&t72 + 8, (uintptr_t)(14));
                gm_t39uai1ajk3qv t73;
                gm_f_unsupported_2seq49l((void *)&t73, t70, GM_CONV(gm_cseahvg8v7p0vs, t71), t72);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 8, 0);
                ((uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out))[0] = 1;
                GM_ST(gm_t39uai1ajk3qv, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 16, t73);
                goto gm_fdone;
            }
ix22:;
        }
        goto me7;
mn19:;
ma10:;
        goto me7;
mn74:;
ma11:;
        gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 355, 5);
me7:;
    }
    {
        void * t76 = gm_l0;
        gm_b16_8 t77 = gm_l2;
        gm_t65n7r6ql4pgd t78;
        gm_f_args_6f16vj1((void *)&t78, (void *)NULL, cx, NULL, t76, t77);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t78))[0] != 0)) {
            gm_ttk92ufj2f6eh t79;
            GM_ST(uint8_t, (uint8_t *)&t79 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t79 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t79 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t79 + 8, 0);
            ((uint8_t *)&t79)[0] = 1;
            GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t79 + 16), (*(gm_t39uai1ajk3qv *)(((uint8_t *)&t78) + 16)));
            F.ret = t79;
            goto gm_fret;
        }
        gm_b16_8 t80 = GM_LD(gm_b16_8, (((uint8_t *)&t78) + 16));
        gm_l9 = t80;
    }
    {
        gm_t6evimt84lndq t81;
        uint64_t t83 = GM_LD(uintptr_t, ((uint8_t *)&gm_l9) + 8);
        uint8_t t84 = (t83 > ((uint64_t)0ULL));
        if (!t84) goto ie82;
        uint64_t t86 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t87 = GM_LD(uintptr_t, ((uint8_t *)&gm_l9) + 8);
        if (t86 >= (uintptr_t)(t87)) gm_trap_index(t86, t87, gm_srcue4ionbe9lgh, 367, 28);
        gm_t6evimt84lndq t88 = GM_LD(gm_t6evimt84lndq, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l9)) + t86 * 48));
        gm_t6evimt84lndq t89 = t88;
        t81 = t89;
        goto ix82;
ie82:;
        gm_t6evimt84lndq t91;
        memset(&t91, 0, sizeof t91);
        GM_ST(uint8_t, ((uint8_t *)&t91 + 0), ((uint8_t)6ULL));
        gm_t6evimt84lndq t92 = t91;
        t81 = t92;
ix82:;
        F.l10 = t81;
    }
    {
        gm_t88rcucg1b7uk t93;
        uint64_t t95 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        uint8_t t96 = (t95 > ((uint64_t)0ULL));
        if (!t96) goto ie94;
        uint64_t t98 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t99 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        if (t98 >= (uintptr_t)(t99)) gm_trap_index(t98, t99, gm_srcue4ionbe9lgh, 368, 29);
        gm_t88rcucg1b7uk t100 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t98 * 88) + 0));
        gm_t88rcucg1b7uk t101 = t100;
        t93 = t101;
        goto ix94;
ie94:;
        void * t103 = gm_l3;
        gm_t88rcucg1b7uk t104 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t103) + 0));
        gm_t88rcucg1b7uk t105 = t104;
        t93 = t105;
ix94:;
        gm_l11 = t93;
    }
    {
        uint8_t t132 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        switch (t132) { case ((uint8_t)0ULL): goto ma107; case ((uint8_t)1ULL): goto ma108; case ((uint8_t)2ULL): goto ma108; case ((uint8_t)3ULL): goto ma108; case ((uint8_t)4ULL): goto ma108; case ((uint8_t)5ULL): goto ma109; case ((uint8_t)6ULL): goto ma110; case ((uint8_t)7ULL): goto ma127; case ((uint8_t)8ULL): goto ma124; case ((uint8_t)9ULL): goto ma125; case ((uint8_t)10ULL): goto ma130; case ((uint8_t)11ULL): goto ma130; case ((uint8_t)12ULL): goto ma130; case ((uint8_t)13ULL): goto ma126; case ((uint8_t)14ULL): goto ma120; case ((uint8_t)15ULL): goto ma130; case ((uint8_t)16ULL): goto ma111; case ((uint8_t)17ULL): goto ma111; case ((uint8_t)18ULL): goto ma112; case ((uint8_t)19ULL): goto ma113; case ((uint8_t)20ULL): goto ma114; case ((uint8_t)21ULL): goto ma115; case ((uint8_t)22ULL): goto ma116; case ((uint8_t)23ULL): goto ma117; case ((uint8_t)24ULL): goto ma118; case ((uint8_t)25ULL): goto ma119; case ((uint8_t)26ULL): goto ma128; case ((uint8_t)27ULL): goto ma130; case ((uint8_t)28ULL): goto ma129; case ((uint8_t)29ULL): goto ma129; case ((uint8_t)30ULL): goto ma129; case ((uint8_t)31ULL): goto ma129; case ((uint8_t)32ULL): goto ma129; case ((uint8_t)33ULL): goto ma129; case ((uint8_t)34ULL): goto ma129; case ((uint8_t)35ULL): goto ma122; case ((uint8_t)36ULL): goto ma121; case ((uint8_t)37ULL): goto ma121; case ((uint8_t)38ULL): goto ma121; case ((uint8_t)39ULL): goto ma121; case ((uint8_t)40ULL): goto ma130; case ((uint8_t)41ULL): goto ma130; case ((uint8_t)42ULL): goto ma130; case ((uint8_t)43ULL): goto ma130; case ((uint8_t)44ULL): goto ma130; case ((uint8_t)45ULL): goto ma130; case ((uint8_t)46ULL): goto ma130; case ((uint8_t)47ULL): goto ma130; case ((uint8_t)48ULL): goto ma130; case ((uint8_t)49ULL): goto ma130; case ((uint8_t)50ULL): goto ma130; case ((uint8_t)51ULL): goto ma130; case ((uint8_t)52ULL): goto ma130; case ((uint8_t)53ULL): goto ma130; case ((uint8_t)54ULL): goto ma130; case ((uint8_t)55ULL): goto ma130; case ((uint8_t)56ULL): goto ma130; case ((uint8_t)57ULL): goto ma130; case ((uint8_t)58ULL): goto ma130; case ((uint8_t)59ULL): goto ma130; case ((uint8_t)60ULL): goto ma130; case ((uint8_t)61ULL): goto ma130; case ((uint8_t)62ULL): goto ma130; case ((uint8_t)63ULL): goto ma130; case ((uint8_t)64ULL): goto ma130; case ((uint8_t)65ULL): goto ma130; case ((uint8_t)66ULL): goto ma130; case ((uint8_t)67ULL): goto ma130; case ((uint8_t)68ULL): goto ma130; case ((uint8_t)69ULL): goto ma130; case ((uint8_t)70ULL): goto ma130; case ((uint8_t)71ULL): goto ma130; case ((uint8_t)72ULL): goto ma130; case ((uint8_t)73ULL): goto ma130; case ((uint8_t)74ULL): goto ma130; case ((uint8_t)75ULL): goto ma130; case ((uint8_t)76ULL): goto ma130; case ((uint8_t)77ULL): goto ma130; case ((uint8_t)78ULL): goto ma123; case ((uint8_t)79ULL): goto ma130; case ((uint8_t)80ULL): goto ma130; case ((uint8_t)81ULL): goto ma130; default: goto ma130; }
ma107:;
        uint8_t t134 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t134 != ((uint8_t)0ULL)) goto mn133;
        uint8_t t142 = GM_LD(uint8_t, (((uint8_t *)&F.l10) + 0));
        switch (t142) { case ((uint8_t)0ULL): goto ma136; case ((uint8_t)1ULL): goto ma138; case ((uint8_t)2ULL): goto ma137; case ((uint8_t)3ULL): goto ma140; case ((uint8_t)4ULL): goto ma140; case ((uint8_t)5ULL): goto ma140; case ((uint8_t)6ULL): goto ma140; case ((uint8_t)7ULL): goto ma140; case ((uint8_t)8ULL): goto ma140; case ((uint8_t)9ULL): goto ma140; case ((uint8_t)10ULL): goto ma139; case ((uint8_t)11ULL): goto ma140; case ((uint8_t)12ULL): goto ma140; case ((uint8_t)13ULL): goto ma140; default: goto ma140; }
ma136:;
        uint8_t t144 = GM_LD(uint8_t, (((uint8_t *)&F.l10) + 0));
        if (t144 != ((uint8_t)0ULL)) goto mn143;
        unsigned __int128 t145 = GM_LD(unsigned __int128, (((uint8_t *)&F.l10) + 16));
        gm_l12 = t145;
        void * t146 = gm_l4;
        void * t147 = gm_l3;
        gm_t88rcucg1b7uk t148 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t147) + 0));
        gm_t53at80pfdus3 t149;
        gm_f_kind_jcnt4hm((void *)&t149, t146, GM_CONV(gm_csguankko4lmhd, t148));
        uint8_t t155 = GM_LD(uint8_t, (((uint8_t *)&t149) + 0));
        switch (t155) { case ((uint8_t)0ULL): goto ma152; case ((uint8_t)1ULL): goto ma152; case ((uint8_t)2ULL): goto ma152; case ((uint8_t)3ULL): goto ma151; case ((uint8_t)4ULL): goto ma153; case ((uint8_t)5ULL): goto ma153; case ((uint8_t)6ULL): goto ma153; case ((uint8_t)7ULL): goto ma153; case ((uint8_t)8ULL): goto ma153; case ((uint8_t)9ULL): goto ma153; case ((uint8_t)10ULL): goto ma153; case ((uint8_t)11ULL): goto ma153; case ((uint8_t)12ULL): goto ma153; case ((uint8_t)13ULL): goto ma153; case ((uint8_t)14ULL): goto ma153; case ((uint8_t)15ULL): goto ma153; case ((uint8_t)16ULL): goto ma153; case ((uint8_t)17ULL): goto ma153; case ((uint8_t)18ULL): goto ma153; case ((uint8_t)19ULL): goto ma153; case ((uint8_t)20ULL): goto ma153; case ((uint8_t)21ULL): goto ma153; case ((uint8_t)22ULL): goto ma153; case ((uint8_t)23ULL): goto ma153; case ((uint8_t)24ULL): goto ma153; case ((uint8_t)25ULL): goto ma153; case ((uint8_t)26ULL): goto ma153; default: goto ma153; }
ma151:;
        uint8_t t157 = GM_LD(uint8_t, (((uint8_t *)&t149) + 0));
        if (t157 != ((uint8_t)3ULL)) goto mn156;
        void * t158 = gm_l0;
        void * t159 = gm_l0;
        void * t160 = gm_l0;
        unsigned __int128 t161 = gm_l12;
        gm_t88rcucg1b7uk t162 = gm_l11;
        gm_tbci5ospk5nnn t163;
        gm_f_big_of_1cq0qao((void *)&t163, (void *)NULL, cx, NULL, t160, t161, t162);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        double t164 = gm_f_big_float_jmiufgl((void *)NULL, cx, NULL, t159, &t163);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t165 = gm_l3;
        gm_t88rcucg1b7uk t166 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t165) + 0));
        gm_t26jhpkpc0sda t167;
        gm_f_round_float_saju9fu((void *)&t167, (void *)NULL, cx, NULL, t158, t164, t166);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t167))[0] != 0)) {
            gm_ttk92ufj2f6eh t168;
            GM_ST(uint8_t, (uint8_t *)&t168 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t168 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t168 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t168 + 8, 0);
            ((uint8_t *)&t168)[0] = 1;
            GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t168 + 16), (*(gm_t39uai1ajk3qv *)(((uint8_t *)&t167) + 16)));
            F.ret = t168;
            goto gm_fret;
        }
        double t169 = GM_LD(double, (((uint8_t *)&t167) + 16));
        gm_t6evimt84lndq t170;
        memset(&t170, 0, sizeof t170);
        GM_ST(uint8_t, ((uint8_t *)&t170 + 0), ((uint8_t)1ULL));
        GM_ST(double, ((uint8_t *)&t170 + 16), t169);
        memset(&(*(gm_ttk92ufj2f6eh *)gm_out), 0, sizeof (*(gm_ttk92ufj2f6eh *)gm_out));
        ((uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out))[0] = 0;
        GM_ST(gm_t6evimt84lndq, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 16, t170);
        goto gm_fdone;
        goto me150;
mn156:;
ma152:;
        uint8_t t174 = GM_LD(uint8_t, (((uint8_t *)&t149) + 0));
        if (t174 != ((uint8_t)0ULL)) goto pa173;
        goto po172;
pa173:;
        uint8_t t176 = GM_LD(uint8_t, (((uint8_t *)&t149) + 0));
        if (t176 != ((uint8_t)1ULL)) goto pa175;
        goto po172;
pa175:;
        uint8_t t178 = GM_LD(uint8_t, (((uint8_t *)&t149) + 0));
        if (t178 != ((uint8_t)2ULL)) goto pa177;
        goto po172;
pa177:;
        goto mn171;
po172:;
        void * t179 = gm_l0;
        void * t180 = gm_l0;
        unsigned __int128 t181 = gm_l12;
        gm_t88rcucg1b7uk t182 = gm_l11;
        __int128 t183 = gm_f_int_value_6hmh8tj(t180, t181, GM_CONV(gm_csguankko4lmhd, t182));
        unsigned __int128 t184;
        memset(&t184, 0, sizeof t184); memcpy(&t184, ((uint8_t *)&t183), sizeof t184 < 16 ? sizeof t184 : 16);
        void * t185 = gm_l3;
        gm_t88rcucg1b7uk t186 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t185) + 0));
        unsigned __int128 t187 = gm_f_wrap_bt8r149(t179, t184, GM_CONV(gm_csguankko4lmhd, t186));
        gm_t6evimt84lndq t188;
        GM_ST(uint8_t, (uint8_t *)&t188 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t188 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t188 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t188 + 8, 0);
        GM_ST(uint64_t, (uint8_t *)&t188 + 32, 0);
        GM_ST(uint64_t, (uint8_t *)&t188 + 40, 0);
        GM_ST(uint8_t, ((uint8_t *)&t188 + 0), ((uint8_t)0ULL));
        GM_ST(unsigned __int128, ((uint8_t *)&t188 + 16), t187);
        memset(&(*(gm_ttk92ufj2f6eh *)gm_out), 0, sizeof (*(gm_ttk92ufj2f6eh *)gm_out));
        ((uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out))[0] = 0;
        GM_ST(gm_t6evimt84lndq, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 16, t188);
        goto gm_fdone;
        goto me150;
mn171:;
ma153:;
        void * t190 = gm_l0;
        gm_tl874t2atnlh3 t191 = gm_l5;
        gm_b16_8 t192;
        GM_ST(void *, (uint8_t *)&t192, (void *)(gm_smr3su985na96));
        GM_ST(uintptr_t, (uint8_t *)&t192 + 8, (uintptr_t)(15));
        gm_t39uai1ajk3qv t193;
        gm_f_unsupported_2seq49l((void *)&t193, t190, GM_CONV(gm_cseahvg8v7p0vs, t191), t192);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 8, 0);
        ((uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out))[0] = 1;
        GM_ST(gm_t39uai1ajk3qv, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 16, t193);
        goto gm_fdone;
        goto me150;
mn189:;
ma154:;
        gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 371, 27);
me150:;
        goto me135;
mn143:;
ma137:;
        uint8_t t195 = GM_LD(uint8_t, (((uint8_t *)&F.l10) + 0));
        if (t195 != ((uint8_t)2ULL)) goto mn194;
        uint8_t t196 = GM_LD(uint8_t, (((uint8_t *)&F.l10) + 16));
        gm_l13 = t196;
        unsigned __int128 t197;
        uint8_t t199 = gm_l13;
        if (!t199) goto ie198;
        unsigned __int128 t201 = ((unsigned __int128)(((unsigned __int128)0u << 64) | 1u));
        t197 = t201;
        goto ix198;
ie198:;
        unsigned __int128 t203 = ((unsigned __int128)(((unsigned __int128)0u << 64) | 0u));
        t197 = t203;
ix198:;
        gm_t6evimt84lndq t204;
        GM_ST(uint8_t, (uint8_t *)&t204 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t204 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t204 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t204 + 8, 0);
        GM_ST(uint64_t, (uint8_t *)&t204 + 32, 0);
        GM_ST(uint64_t, (uint8_t *)&t204 + 40, 0);
        GM_ST(uint8_t, ((uint8_t *)&t204 + 0), ((uint8_t)0ULL));
        GM_ST(unsigned __int128, ((uint8_t *)&t204 + 16), t197);
        memset(&(*(gm_ttk92ufj2f6eh *)gm_out), 0, sizeof (*(gm_ttk92ufj2f6eh *)gm_out));
        ((uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out))[0] = 0;
        GM_ST(gm_t6evimt84lndq, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 16, t204);
        goto gm_fdone;
        goto me135;
mn194:;
ma138:;
        uint8_t t206 = GM_LD(uint8_t, (((uint8_t *)&F.l10) + 0));
        if (t206 != ((uint8_t)1ULL)) goto mn205;
        double t207 = GM_LD(double, (((uint8_t *)&F.l10) + 16));
        gm_l14 = t207;
        void * t208 = gm_l0;
        double t209 = gm_l14;
        void * t210 = gm_l3;
        gm_t88rcucg1b7uk t211 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t210) + 0));
        gm_t26jhpkpc0sda t212;
        gm_f_round_float_saju9fu((void *)&t212, (void *)NULL, cx, NULL, t208, t209, t211);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t212))[0] != 0)) {
            gm_ttk92ufj2f6eh t213;
            GM_ST(uint8_t, (uint8_t *)&t213 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t213 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t213 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t213 + 8, 0);
            ((uint8_t *)&t213)[0] = 1;
            GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t213 + 16), (*(gm_t39uai1ajk3qv *)(((uint8_t *)&t212) + 16)));
            F.ret = t213;
            goto gm_fret;
        }
        double t214 = GM_LD(double, (((uint8_t *)&t212) + 16));
        gm_t6evimt84lndq t215;
        memset(&t215, 0, sizeof t215);
        GM_ST(uint8_t, ((uint8_t *)&t215 + 0), ((uint8_t)1ULL));
        GM_ST(double, ((uint8_t *)&t215 + 16), t214);
        memset(&(*(gm_ttk92ufj2f6eh *)gm_out), 0, sizeof (*(gm_ttk92ufj2f6eh *)gm_out));
        ((uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out))[0] = 0;
        GM_ST(gm_t6evimt84lndq, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 16, t215);
        goto gm_fdone;
        goto me135;
mn205:;
ma139:;
        uint8_t t217 = GM_LD(uint8_t, (((uint8_t *)&F.l10) + 0));
        if (t217 != ((uint8_t)10ULL)) goto mn216;
        uint32_t t218 = GM_LD(uint32_t, ((((uint8_t *)&F.l10) + 16) + 0));
        gm_l15 = t218;
        {
            void * t220 = gm_l0;
            gm_t88rcucg1b7uk t221 = gm_l11;
            uint32_t t222 = gm_l15;
            gm_thrdpodrs8pgt t223;
            gm_f_variant_tag_4pbamin((void *)&t223, t220, GM_CONV(gm_csguankko4lmhd, t221), t222);
            gm_tbci5ospk5nnn t224;
            if (((((uint8_t *)&t223))[48] != 0)) {
                t224 = (*(gm_tbci5ospk5nnn *)(((uint8_t *)&t223) + 0));
            } else {
                void * t225 = gm_l0;
                gm_tl874t2atnlh3 t226 = gm_l5;
                gm_b16_8 t227;
                GM_ST(void *, (uint8_t *)&t227, (void *)(gm_smr3su985na96));
                GM_ST(uintptr_t, (uint8_t *)&t227 + 8, (uintptr_t)(15));
                gm_t39uai1ajk3qv t228;
                gm_f_unsupported_2seq49l((void *)&t228, t225, GM_CONV(gm_cseahvg8v7p0vs, t226), t227);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 8, 0);
                ((uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out))[0] = 1;
                GM_ST(gm_t39uai1ajk3qv, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 16, t228);
                goto gm_fdone;
            }
            F.l16 = t224;
        }
        {
            void * t229 = gm_l0;
            unsigned __int128 t230 = gm_f_bits128_jigkui4(&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l16)));
            void * t231 = gm_l3;
            gm_t88rcucg1b7uk t232 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t231) + 0));
            unsigned __int128 t233 = gm_f_wrap_bt8r149(t229, t230, GM_CONV(gm_csguankko4lmhd, t232));
            gm_t6evimt84lndq t234;
            GM_ST(uint8_t, (uint8_t *)&t234 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t234 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t234 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t234 + 8, 0);
            GM_ST(uint64_t, (uint8_t *)&t234 + 32, 0);
            GM_ST(uint64_t, (uint8_t *)&t234 + 40, 0);
            GM_ST(uint8_t, ((uint8_t *)&t234 + 0), ((uint8_t)0ULL));
            GM_ST(unsigned __int128, ((uint8_t *)&t234 + 16), t233);
            memset(&(*(gm_ttk92ufj2f6eh *)gm_out), 0, sizeof (*(gm_ttk92ufj2f6eh *)gm_out));
            ((uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out))[0] = 0;
            GM_ST(gm_t6evimt84lndq, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 16, t234);
            goto gm_fdone;
        }
        goto me135;
mn216:;
ma140:;
        void * t236 = gm_l0;
        gm_tl874t2atnlh3 t237 = gm_l5;
        gm_b16_8 t238;
        GM_ST(void *, (uint8_t *)&t238, (void *)(gm_smr3su985na96));
        GM_ST(uintptr_t, (uint8_t *)&t238 + 8, (uintptr_t)(15));
        gm_t39uai1ajk3qv t239;
        gm_f_unsupported_2seq49l((void *)&t239, t236, GM_CONV(gm_cseahvg8v7p0vs, t237), t238);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 8, 0);
        ((uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out))[0] = 1;
        GM_ST(gm_t39uai1ajk3qv, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 16, t239);
        goto gm_fdone;
        goto me135;
mn235:;
ma141:;
        gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 370, 21);
me135:;
        goto me106;
mn133:;
ma108:;
        uint8_t t243 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t243 != ((uint8_t)1ULL)) goto pa242;
        goto po241;
pa242:;
        uint8_t t245 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t245 != ((uint8_t)2ULL)) goto pa244;
        goto po241;
pa244:;
        uint8_t t247 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t247 != ((uint8_t)3ULL)) goto pa246;
        goto po241;
pa246:;
        uint8_t t249 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t249 != ((uint8_t)4ULL)) goto pa248;
        goto po241;
pa248:;
        goto mn240;
po241:;
        uint8_t t255 = GM_LD(uint8_t, (((uint8_t *)&F.l10) + 0));
        switch (t255) { case ((uint8_t)0ULL): goto ma251; case ((uint8_t)1ULL): goto ma252; case ((uint8_t)2ULL): goto ma253; case ((uint8_t)3ULL): goto ma253; case ((uint8_t)4ULL): goto ma253; case ((uint8_t)5ULL): goto ma253; case ((uint8_t)6ULL): goto ma253; case ((uint8_t)7ULL): goto ma253; case ((uint8_t)8ULL): goto ma253; case ((uint8_t)9ULL): goto ma253; case ((uint8_t)10ULL): goto ma253; case ((uint8_t)11ULL): goto ma253; case ((uint8_t)12ULL): goto ma253; case ((uint8_t)13ULL): goto ma253; default: goto ma253; }
ma251:;
        uint8_t t257 = GM_LD(uint8_t, (((uint8_t *)&F.l10) + 0));
        if (t257 != ((uint8_t)0ULL)) goto mn256;
        unsigned __int128 t258 = GM_LD(unsigned __int128, (((uint8_t *)&F.l10) + 16));
        gm_l17 = t258;
        {
            void * t261 = gm_l4;
            void * t262 = gm_l3;
            gm_t88rcucg1b7uk t263 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t262) + 0));
            gm_t53at80pfdus3 t264;
            gm_f_kind_jcnt4hm((void *)&t264, t261, GM_CONV(gm_csguankko4lmhd, t263));
            uint8_t t265 = GM_LD(uint8_t, (((uint8_t *)&t264) + 0));
            if (t265 != ((uint8_t)3ULL)) goto ie260;
            {
                void * t267 = gm_l0;
                void * t268 = gm_l0;
                void * t269 = gm_l0;
                unsigned __int128 t270 = gm_l17;
                gm_t88rcucg1b7uk t271 = gm_l11;
                gm_tbci5ospk5nnn t272;
                gm_f_big_of_1cq0qao((void *)&t272, (void *)NULL, cx, NULL, t269, t270, t271);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                double t273 = gm_f_big_float_jmiufgl((void *)NULL, cx, NULL, t268, &t272);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                void * t274 = gm_l3;
                gm_t88rcucg1b7uk t275 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t274) + 0));
                gm_t26jhpkpc0sda t276;
                gm_f_round_float_saju9fu((void *)&t276, (void *)NULL, cx, NULL, t267, t273, t275);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t276))[0] != 0)) {
                    gm_ttk92ufj2f6eh t277;
                    GM_ST(uint8_t, (uint8_t *)&t277 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t277 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t277 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t277 + 8, 0);
                    ((uint8_t *)&t277)[0] = 1;
                    GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t277 + 16), (*(gm_t39uai1ajk3qv *)(((uint8_t *)&t276) + 16)));
                    F.ret = t277;
                    goto gm_fret;
                }
                double t278 = GM_LD(double, (((uint8_t *)&t276) + 16));
                gm_t6evimt84lndq t279;
                memset(&t279, 0, sizeof t279);
                GM_ST(uint8_t, ((uint8_t *)&t279 + 0), ((uint8_t)1ULL));
                GM_ST(double, ((uint8_t *)&t279 + 16), t278);
                memset(&(*(gm_ttk92ufj2f6eh *)gm_out), 0, sizeof (*(gm_ttk92ufj2f6eh *)gm_out));
                ((uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out))[0] = 0;
                GM_ST(gm_t6evimt84lndq, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 16, t279);
                goto gm_fdone;
            }
            goto ix260;
ie260:;
ix260:;
        }
        {
            void * t280 = gm_l0;
            unsigned __int128 t281 = gm_l17;
            void * t282 = gm_l3;
            gm_t88rcucg1b7uk t283 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t282) + 0));
            unsigned __int128 t284 = gm_f_wrap_bt8r149(t280, t281, GM_CONV(gm_csguankko4lmhd, t283));
            gm_t6evimt84lndq t285;
            GM_ST(uint8_t, (uint8_t *)&t285 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t285 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t285 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t285 + 8, 0);
            GM_ST(uint64_t, (uint8_t *)&t285 + 32, 0);
            GM_ST(uint64_t, (uint8_t *)&t285 + 40, 0);
            GM_ST(uint8_t, ((uint8_t *)&t285 + 0), ((uint8_t)0ULL));
            GM_ST(unsigned __int128, ((uint8_t *)&t285 + 16), t284);
            memset(&(*(gm_ttk92ufj2f6eh *)gm_out), 0, sizeof (*(gm_ttk92ufj2f6eh *)gm_out));
            ((uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out))[0] = 0;
            GM_ST(gm_t6evimt84lndq, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 16, t285);
            goto gm_fdone;
        }
        goto me250;
mn256:;
ma252:;
        uint8_t t287 = GM_LD(uint8_t, (((uint8_t *)&F.l10) + 0));
        if (t287 != ((uint8_t)1ULL)) goto mn286;
        double t288 = GM_LD(double, (((uint8_t *)&F.l10) + 16));
        gm_l18 = t288;
        {
            void * t291 = gm_l4;
            void * t292 = gm_l3;
            gm_t88rcucg1b7uk t293 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t292) + 0));
            gm_t53at80pfdus3 t294;
            gm_f_kind_jcnt4hm((void *)&t294, t291, GM_CONV(gm_csguankko4lmhd, t293));
            uint8_t t295 = GM_LD(uint8_t, (((uint8_t *)&t294) + 0));
            if (t295 != ((uint8_t)3ULL)) goto ie290;
            {
                void * t297 = gm_l0;
                double t298 = gm_l18;
                void * t299 = gm_l3;
                gm_t88rcucg1b7uk t300 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t299) + 0));
                gm_t26jhpkpc0sda t301;
                gm_f_round_float_saju9fu((void *)&t301, (void *)NULL, cx, NULL, t297, t298, t300);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t301))[0] != 0)) {
                    gm_ttk92ufj2f6eh t302;
                    GM_ST(uint8_t, (uint8_t *)&t302 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t302 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t302 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t302 + 8, 0);
                    ((uint8_t *)&t302)[0] = 1;
                    GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t302 + 16), (*(gm_t39uai1ajk3qv *)(((uint8_t *)&t301) + 16)));
                    F.ret = t302;
                    goto gm_fret;
                }
                double t303 = GM_LD(double, (((uint8_t *)&t301) + 16));
                gm_t6evimt84lndq t304;
                memset(&t304, 0, sizeof t304);
                GM_ST(uint8_t, ((uint8_t *)&t304 + 0), ((uint8_t)1ULL));
                GM_ST(double, ((uint8_t *)&t304 + 16), t303);
                memset(&(*(gm_ttk92ufj2f6eh *)gm_out), 0, sizeof (*(gm_ttk92ufj2f6eh *)gm_out));
                ((uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out))[0] = 0;
                GM_ST(gm_t6evimt84lndq, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 16, t304);
                goto gm_fdone;
            }
            goto ix290;
ie290:;
ix290:;
        }
        {
            void * t305 = gm_l0;
            double t306 = gm_l18;
            void * t307 = gm_l3;
            gm_t88rcucg1b7uk t308 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t307) + 0));
            gm_tvdg94bk7aa22 t309 = gm_l1;
            gm_thrdpodrs8pgt t310;
            gm_f_float_to_int_sf4dap7((void *)&t310, (void *)NULL, cx, NULL, t305, t306, t308, t309);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tbci5ospk5nnn t311;
            if (((((uint8_t *)&t310))[48] != 0)) {
                t311 = (*(gm_tbci5ospk5nnn *)(((uint8_t *)&t310) + 0));
            } else {
                void * t312 = gm_l0;
                gm_tl874t2atnlh3 t313 = gm_l5;
                gm_b16_8 t314;
                GM_ST(void *, (uint8_t *)&t314, (void *)(gm_s9o2c36nosv54));
                GM_ST(uintptr_t, (uint8_t *)&t314 + 8, (uintptr_t)(40));
                gm_t39uai1ajk3qv t315;
                gm_f_trap_jftbss6((void *)&t315, (void *)NULL, cx, NULL, t312, t313, t314);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(uint8_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 8, 0);
                ((uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out))[0] = 1;
                GM_ST(gm_t39uai1ajk3qv, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 16, t315);
                goto gm_fdone;
            }
            F.l19 = t311;
        }
        {
            void * t316 = gm_l0;
            unsigned __int128 t317 = gm_f_bits128_jigkui4(&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l19)));
            void * t318 = gm_l3;
            gm_t88rcucg1b7uk t319 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t318) + 0));
            unsigned __int128 t320 = gm_f_wrap_bt8r149(t316, t317, GM_CONV(gm_csguankko4lmhd, t319));
            gm_t6evimt84lndq t321;
            GM_ST(uint8_t, (uint8_t *)&t321 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t321 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t321 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t321 + 8, 0);
            GM_ST(uint64_t, (uint8_t *)&t321 + 32, 0);
            GM_ST(uint64_t, (uint8_t *)&t321 + 40, 0);
            GM_ST(uint8_t, ((uint8_t *)&t321 + 0), ((uint8_t)0ULL));
            GM_ST(unsigned __int128, ((uint8_t *)&t321 + 16), t320);
            memset(&(*(gm_ttk92ufj2f6eh *)gm_out), 0, sizeof (*(gm_ttk92ufj2f6eh *)gm_out));
            ((uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out))[0] = 0;
            GM_ST(gm_t6evimt84lndq, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 16, t321);
            goto gm_fdone;
        }
        goto me250;
mn286:;
ma253:;
        void * t323 = gm_l0;
        gm_tl874t2atnlh3 t324 = gm_l5;
        gm_b16_8 t325;
        GM_ST(void *, (uint8_t *)&t325, (void *)(gm_smr3su985na96));
        GM_ST(uintptr_t, (uint8_t *)&t325 + 8, (uintptr_t)(15));
        gm_t39uai1ajk3qv t326;
        gm_f_unsupported_2seq49l((void *)&t326, t323, GM_CONV(gm_cseahvg8v7p0vs, t324), t325);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 8, 0);
        ((uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out))[0] = 1;
        GM_ST(gm_t39uai1ajk3qv, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 16, t326);
        goto gm_fdone;
        goto me250;
mn322:;
ma254:;
        gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 384, 42);
me250:;
        goto me106;
mn240:;
ma109:;
        uint8_t t328 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t328 != ((uint8_t)5ULL)) goto mn327;
        {
            void * t331 = gm_l4;
            void * t332 = gm_l3;
            gm_t88rcucg1b7uk t333 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t332) + 0));
            gm_t53at80pfdus3 t334;
            gm_f_kind_jcnt4hm((void *)&t334, t331, GM_CONV(gm_csguankko4lmhd, t333));
            uint8_t t335 = GM_LD(uint8_t, (((uint8_t *)&t334) + 0));
            if (t335 != ((uint8_t)13ULL)) goto ie330;
            gm_t88rcucg1b7uk t336 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t334) + 8));
            gm_l20 = t336;
            {
                uint8_t t343 = GM_LD(uint8_t, (((uint8_t *)&F.l10) + 0));
                switch (t343) { case ((uint8_t)0ULL): goto ma339; case ((uint8_t)1ULL): goto ma340; case ((uint8_t)2ULL): goto ma341; case ((uint8_t)3ULL): goto ma341; case ((uint8_t)4ULL): goto ma341; case ((uint8_t)5ULL): goto ma341; case ((uint8_t)6ULL): goto ma341; case ((uint8_t)7ULL): goto ma341; case ((uint8_t)8ULL): goto ma341; case ((uint8_t)9ULL): goto ma341; case ((uint8_t)10ULL): goto ma341; case ((uint8_t)11ULL): goto ma341; case ((uint8_t)12ULL): goto ma341; case ((uint8_t)13ULL): goto ma341; default: goto ma341; }
ma339:;
                uint8_t t345 = GM_LD(uint8_t, (((uint8_t *)&F.l10) + 0));
                if (t345 != ((uint8_t)0ULL)) goto mn344;
                unsigned __int128 t346 = GM_LD(unsigned __int128, (((uint8_t *)&F.l10) + 16));
                gm_l21 = t346;
                {
                    void * t348 = gm_l0;
                    unsigned __int128 t349 = gm_l21;
                    gm_t88rcucg1b7uk t350 = gm_l11;
                    gm_f_big_of_1cq0qao((void *)&F.l22, (void *)NULL, cx, NULL, t348, t349, t350);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                {
                    void * t351 = gm_l0;
                    gm_t88rcucg1b7uk t352 = gm_l20;
                    gm_f_int_range_hue3vms((void *)&F.l23, (void *)NULL, cx, NULL, t351, t352);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                {
                    uint8_t t354 = 0;
                    int32_t t355 = gm_f_cmp_i1knrgu(&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l22)), &(*(gm_tbci5ospk5nnn *)(((uint8_t *)&F.l23) + 0)));
                    uint8_t t356 = (t355 < ((int32_t)0LL));
                    if (!t356) {
                        int32_t t357 = gm_f_cmp_i1knrgu(&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l22)), &(*(gm_tbci5ospk5nnn *)(((uint8_t *)&F.l23) + 48)));
                        uint8_t t358 = (t357 > ((int32_t)0LL));
                        t354 = t358;
                    } else t354 = 1;
                    if (!t354) goto ie353;
                    {
                        gm_t6evimt84lndq t360;
                        memset(&t360, 0, sizeof t360);
                        GM_ST(uint8_t, ((uint8_t *)&t360 + 0), ((uint8_t)6ULL));
                        memset(&(*(gm_ttk92ufj2f6eh *)gm_out), 0, sizeof (*(gm_ttk92ufj2f6eh *)gm_out));
                        ((uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out))[0] = 0;
                        GM_ST(gm_t6evimt84lndq, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 16, t360);
                        goto gm_fdone;
                    }
                    goto ix353;
ie353:;
ix353:;
                }
                {
                    void * t361 = gm_l0;
                    unsigned __int128 t362 = gm_f_bits128_jigkui4(&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l22)));
                    gm_t88rcucg1b7uk t363 = gm_l20;
                    unsigned __int128 t364 = gm_f_wrap_bt8r149(t361, t362, GM_CONV(gm_csguankko4lmhd, t363));
                    gm_t6evimt84lndq t365;
                    GM_ST(uint8_t, (uint8_t *)&t365 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t365 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t365 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t365 + 8, 0);
                    GM_ST(uint64_t, (uint8_t *)&t365 + 32, 0);
                    GM_ST(uint64_t, (uint8_t *)&t365 + 40, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t365 + 0), ((uint8_t)0ULL));
                    GM_ST(unsigned __int128, ((uint8_t *)&t365 + 16), t364);
                    gm_tq7s98ihonkju t366 = gm_f_new_70le0lp((void *)NULL, cx, NULL, &t365, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_k4tn3ii07uh)));
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    if (((((uint8_t *)&t366))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t366) + 8, gm_srcue4ionbe9lgh, 403, 34);
                    void * t367 = GM_LD(void *, (((uint8_t *)&t366) + 8));
                    gm_t6evimt84lndq t368;
                    memset(&t368, 0, sizeof t368);
                    GM_ST(uint8_t, ((uint8_t *)&t368 + 0), ((uint8_t)7ULL));
                    GM_ST(void *, ((uint8_t *)&t368 + 16), t367);
                    memset(&(*(gm_ttk92ufj2f6eh *)gm_out), 0, sizeof (*(gm_ttk92ufj2f6eh *)gm_out));
                    ((uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out))[0] = 0;
                    GM_ST(gm_t6evimt84lndq, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 16, t368);
                    goto gm_fdone;
                }
                goto me338;
mn344:;
ma340:;
                uint8_t t370 = GM_LD(uint8_t, (((uint8_t *)&F.l10) + 0));
                if (t370 != ((uint8_t)1ULL)) goto mn369;
                double t371 = GM_LD(double, (((uint8_t *)&F.l10) + 16));
                gm_l24 = t371;
                {
                    double t373 = gm_l24;
                    gm_tvegkb6ouf3m0 t374;
                    gm_f_float_exact_1g17c6c((void *)&t374, (void *)NULL, cx, NULL, t373);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tngtuiuskn938 t375;
                    if (((((uint8_t *)&t374))[96] != 0)) {
                        t375 = (*(gm_tngtuiuskn938 *)(((uint8_t *)&t374) + 0));
                    } else {
                        gm_t6evimt84lndq t376;
                        memset(&t376, 0, sizeof t376);
                        GM_ST(uint8_t, ((uint8_t *)&t376 + 0), ((uint8_t)6ULL));
                        memset(&(*(gm_ttk92ufj2f6eh *)gm_out), 0, sizeof (*(gm_ttk92ufj2f6eh *)gm_out));
                        ((uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out))[0] = 0;
                        GM_ST(gm_t6evimt84lndq, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 16, t376);
                        goto gm_fdone;
                    }
                    F.l25 = t375;
                }
                {
                    uint8_t t378 = gm_f_is_integer_ab2ohk2(&(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l25)));
                    uint8_t t379 = (uint8_t)!t378;
                    if (!t379) goto ie377;
                    {
                        gm_t6evimt84lndq t381;
                        memset(&t381, 0, sizeof t381);
                        GM_ST(uint8_t, ((uint8_t *)&t381 + 0), ((uint8_t)6ULL));
                        memset(&(*(gm_ttk92ufj2f6eh *)gm_out), 0, sizeof (*(gm_ttk92ufj2f6eh *)gm_out));
                        ((uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out))[0] = 0;
                        GM_ST(gm_t6evimt84lndq, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 16, t381);
                        goto gm_fdone;
                    }
                    goto ix377;
ie377:;
ix377:;
                }
                {
                    void * t382 = gm_l0;
                    gm_t88rcucg1b7uk t383 = gm_l20;
                    gm_f_int_range_hue3vms((void *)&F.l26, (void *)NULL, cx, NULL, t382, t383);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                {
                    uint8_t t385 = 0;
                    int32_t t386 = gm_f_cmp_i1knrgu(&(*(gm_tbci5ospk5nnn *)(((uint8_t *)&F.l25) + 0)), &(*(gm_tbci5ospk5nnn *)(((uint8_t *)&F.l26) + 0)));
                    uint8_t t387 = (t386 < ((int32_t)0LL));
                    if (!t387) {
                        int32_t t388 = gm_f_cmp_i1knrgu(&(*(gm_tbci5ospk5nnn *)(((uint8_t *)&F.l25) + 0)), &(*(gm_tbci5ospk5nnn *)(((uint8_t *)&F.l26) + 48)));
                        uint8_t t389 = (t388 > ((int32_t)0LL));
                        t385 = t389;
                    } else t385 = 1;
                    if (!t385) goto ie384;
                    {
                        gm_t6evimt84lndq t391;
                        memset(&t391, 0, sizeof t391);
                        GM_ST(uint8_t, ((uint8_t *)&t391 + 0), ((uint8_t)6ULL));
                        memset(&(*(gm_ttk92ufj2f6eh *)gm_out), 0, sizeof (*(gm_ttk92ufj2f6eh *)gm_out));
                        ((uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out))[0] = 0;
                        GM_ST(gm_t6evimt84lndq, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 16, t391);
                        goto gm_fdone;
                    }
                    goto ix384;
ie384:;
ix384:;
                }
                {
                    void * t392 = gm_l0;
                    unsigned __int128 t393 = gm_f_bits128_jigkui4(&(*(gm_tbci5ospk5nnn *)(((uint8_t *)&F.l25) + 0)));
                    gm_t88rcucg1b7uk t394 = gm_l20;
                    unsigned __int128 t395 = gm_f_wrap_bt8r149(t392, t393, GM_CONV(gm_csguankko4lmhd, t394));
                    gm_t6evimt84lndq t396;
                    GM_ST(uint8_t, (uint8_t *)&t396 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t396 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t396 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t396 + 8, 0);
                    GM_ST(uint64_t, (uint8_t *)&t396 + 32, 0);
                    GM_ST(uint64_t, (uint8_t *)&t396 + 40, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t396 + 0), ((uint8_t)0ULL));
                    GM_ST(unsigned __int128, ((uint8_t *)&t396 + 16), t395);
                    gm_tq7s98ihonkju t397 = gm_f_new_70le0lp((void *)NULL, cx, NULL, &t396, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_krchumal22f)));
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    if (((((uint8_t *)&t397))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t397) + 8, gm_srcue4ionbe9lgh, 410, 34);
                    void * t398 = GM_LD(void *, (((uint8_t *)&t397) + 8));
                    gm_t6evimt84lndq t399;
                    memset(&t399, 0, sizeof t399);
                    GM_ST(uint8_t, ((uint8_t *)&t399 + 0), ((uint8_t)7ULL));
                    GM_ST(void *, ((uint8_t *)&t399 + 16), t398);
                    memset(&(*(gm_ttk92ufj2f6eh *)gm_out), 0, sizeof (*(gm_ttk92ufj2f6eh *)gm_out));
                    ((uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out))[0] = 0;
                    GM_ST(gm_t6evimt84lndq, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 16, t399);
                    goto gm_fdone;
                }
                goto me338;
mn369:;
ma341:;
                void * t401 = gm_l0;
                gm_tl874t2atnlh3 t402 = gm_l5;
                gm_b16_8 t403;
                GM_ST(void *, (uint8_t *)&t403, (void *)(gm_smr3su985na96));
                GM_ST(uintptr_t, (uint8_t *)&t403 + 8, (uintptr_t)(15));
                gm_t39uai1ajk3qv t404;
                gm_f_unsupported_2seq49l((void *)&t404, t401, GM_CONV(gm_cseahvg8v7p0vs, t402), t403);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 8, 0);
                ((uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out))[0] = 1;
                GM_ST(gm_t39uai1ajk3qv, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 16, t404);
                goto gm_fdone;
                goto me338;
mn400:;
ma342:;
                gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 398, 13);
me338:;
            }
            goto ix330;
ie330:;
            {
                void * t406 = gm_l0;
                gm_tl874t2atnlh3 t407 = gm_l5;
                gm_b16_8 t408;
                GM_ST(void *, (uint8_t *)&t408, (void *)(gm_smr3su985na96));
                GM_ST(uintptr_t, (uint8_t *)&t408 + 8, (uintptr_t)(15));
                gm_t39uai1ajk3qv t409;
                gm_f_unsupported_2seq49l((void *)&t409, t406, GM_CONV(gm_cseahvg8v7p0vs, t407), t408);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 8, 0);
                ((uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out))[0] = 1;
                GM_ST(gm_t39uai1ajk3qv, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 16, t409);
                goto gm_fdone;
            }
ix330:;
        }
        goto me106;
mn327:;
ma110:;
        uint8_t t411 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t411 != ((uint8_t)6ULL)) goto mn410;
        {
            void * t413 = gm_l0;
            void * t414 = gm_l3;
            gm_t88rcucg1b7uk t415 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t414) + 0));
            gm_f_int_range_hue3vms((void *)&F.l27, (void *)NULL, cx, NULL, t413, t415);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_f_zero_s25l990((void *)&F.l28, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            uint8_t t421 = GM_LD(uint8_t, (((uint8_t *)&F.l10) + 0));
            switch (t421) { case ((uint8_t)0ULL): goto ma417; case ((uint8_t)1ULL): goto ma418; case ((uint8_t)2ULL): goto ma419; case ((uint8_t)3ULL): goto ma419; case ((uint8_t)4ULL): goto ma419; case ((uint8_t)5ULL): goto ma419; case ((uint8_t)6ULL): goto ma419; case ((uint8_t)7ULL): goto ma419; case ((uint8_t)8ULL): goto ma419; case ((uint8_t)9ULL): goto ma419; case ((uint8_t)10ULL): goto ma419; case ((uint8_t)11ULL): goto ma419; case ((uint8_t)12ULL): goto ma419; case ((uint8_t)13ULL): goto ma419; default: goto ma419; }
ma417:;
            uint8_t t423 = GM_LD(uint8_t, (((uint8_t *)&F.l10) + 0));
            if (t423 != ((uint8_t)0ULL)) goto mn422;
            unsigned __int128 t424 = GM_LD(unsigned __int128, (((uint8_t *)&F.l10) + 16));
            gm_l29 = t424;
            {
                void * t426 = gm_l0;
                unsigned __int128 t427 = gm_l29;
                gm_t88rcucg1b7uk t428 = gm_l11;
                gm_f_big_of_1cq0qao((void *)&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l28)), (void *)NULL, cx, NULL, t426, t427, t428);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto me416;
mn422:;
ma418:;
            uint8_t t430 = GM_LD(uint8_t, (((uint8_t *)&F.l10) + 0));
            if (t430 != ((uint8_t)1ULL)) goto mn429;
            double t431 = GM_LD(double, (((uint8_t *)&F.l10) + 16));
            gm_l30 = t431;
            {
                double t434 = gm_l30;
                double t435 = gm_l30;
                uint8_t t436 = (!(t434 == t435));
                if (!t436) goto ie433;
                {
                    gm_t6evimt84lndq t438;
                    GM_ST(uint8_t, (uint8_t *)&t438 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t438 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t438 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t438 + 8, 0);
                    GM_ST(uint64_t, (uint8_t *)&t438 + 32, 0);
                    GM_ST(uint64_t, (uint8_t *)&t438 + 40, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t438 + 0), ((uint8_t)0ULL));
                    GM_ST(unsigned __int128, ((uint8_t *)&t438 + 16), ((unsigned __int128)(((unsigned __int128)0u << 64) | 0u)));
                    memset(&(*(gm_ttk92ufj2f6eh *)gm_out), 0, sizeof (*(gm_ttk92ufj2f6eh *)gm_out));
                    ((uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out))[0] = 0;
                    GM_ST(gm_t6evimt84lndq, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 16, t438);
                    goto gm_fdone;
                }
                goto ix433;
ie433:;
ix433:;
            }
            {
                double t440 = gm_l30;
                gm_tvegkb6ouf3m0 t441;
                gm_f_float_exact_1g17c6c((void *)&t441, (void *)NULL, cx, NULL, t440);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (!((((uint8_t *)&t441))[96] != 0)) goto ie439;
                F.l31 = (*(gm_tngtuiuskn938 *)(((uint8_t *)&t441) + 0));
                {
                    gm_f_div_floor_u3mfihk((void *)&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l28)), (void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)(((uint8_t *)&F.l31) + 0)), &(*(gm_tbci5ospk5nnn *)(((uint8_t *)&F.l31) + 48)));
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                {
                    uint8_t t444 = gm_f_is_negative_rb46l9j(&(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l31)));
                    if (!t444) goto ie443;
                    uint8_t t445 = gm_f_is_integer_ab2ohk2(&(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l31)));
                    uint8_t t446 = (uint8_t)!t445;
                    if (!t446) goto ie443;
                    {
                        gm_tbci5ospk5nnn t448 = F.l28;
                        gm_tbci5ospk5nnn t449;
                        gm_f_one_08qg1op((void *)&t449, (void *)NULL, cx, NULL);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_f_add_420lj6o((void *)&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l28)), (void *)NULL, cx, NULL, &t448, &t449);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    goto ix443;
ie443:;
ix443:;
                }
                goto ix439;
ie439:;
                {
                    gm_tbci5ospk5nnn t451;
                    double t453 = gm_l30;
                    uint8_t t454 = (t453 < ((double)0e0));
                    if (!t454) goto ie452;
                    gm_tbci5ospk5nnn t456 = GM_LD(gm_tbci5ospk5nnn, (((uint8_t *)&F.l27) + 0));
                    gm_tbci5ospk5nnn t457 = t456;
                    t451 = t457;
                    goto ix452;
ie452:;
                    gm_tbci5ospk5nnn t459 = GM_LD(gm_tbci5ospk5nnn, (((uint8_t *)&F.l27) + 48));
                    gm_tbci5ospk5nnn t460 = t459;
                    t451 = t460;
ix452:;
                    F.l28 = t451;
                }
ix439:;
            }
            goto me416;
mn429:;
ma419:;
            void * t462 = gm_l0;
            gm_tl874t2atnlh3 t463 = gm_l5;
            gm_b16_8 t464;
            GM_ST(void *, (uint8_t *)&t464, (void *)(gm_smr3su985na96));
            GM_ST(uintptr_t, (uint8_t *)&t464 + 8, (uintptr_t)(15));
            gm_t39uai1ajk3qv t465;
            gm_f_unsupported_2seq49l((void *)&t465, t462, GM_CONV(gm_cseahvg8v7p0vs, t463), t464);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 8, 0);
            ((uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out))[0] = 1;
            GM_ST(gm_t39uai1ajk3qv, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 16, t465);
            goto gm_fdone;
            goto me416;
mn461:;
ma420:;
            gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 418, 13);
me416:;
        }
        {
            int32_t t467 = gm_f_cmp_i1knrgu(&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l28)), &(*(gm_tbci5ospk5nnn *)(((uint8_t *)&F.l27) + 0)));
            uint8_t t468 = (t467 < ((int32_t)0LL));
            if (!t468) goto ie466;
            {
                gm_tbci5ospk5nnn t470 = GM_LD(gm_tbci5ospk5nnn, (((uint8_t *)&F.l27) + 0));
                F.l28 = t470;
            }
            goto ix466;
ie466:;
ix466:;
        }
        {
            int32_t t472 = gm_f_cmp_i1knrgu(&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l28)), &(*(gm_tbci5ospk5nnn *)(((uint8_t *)&F.l27) + 48)));
            uint8_t t473 = (t472 > ((int32_t)0LL));
            if (!t473) goto ie471;
            {
                gm_tbci5ospk5nnn t475 = GM_LD(gm_tbci5ospk5nnn, (((uint8_t *)&F.l27) + 48));
                F.l28 = t475;
            }
            goto ix471;
ie471:;
ix471:;
        }
        {
            void * t476 = gm_l0;
            unsigned __int128 t477 = gm_f_bits128_jigkui4(&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l28)));
            void * t478 = gm_l3;
            gm_t88rcucg1b7uk t479 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t478) + 0));
            unsigned __int128 t480 = gm_f_wrap_bt8r149(t476, t477, GM_CONV(gm_csguankko4lmhd, t479));
            gm_t6evimt84lndq t481;
            GM_ST(uint8_t, (uint8_t *)&t481 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t481 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t481 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t481 + 8, 0);
            GM_ST(uint64_t, (uint8_t *)&t481 + 32, 0);
            GM_ST(uint64_t, (uint8_t *)&t481 + 40, 0);
            GM_ST(uint8_t, ((uint8_t *)&t481 + 0), ((uint8_t)0ULL));
            GM_ST(unsigned __int128, ((uint8_t *)&t481 + 16), t480);
            memset(&(*(gm_ttk92ufj2f6eh *)gm_out), 0, sizeof (*(gm_ttk92ufj2f6eh *)gm_out));
            ((uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out))[0] = 0;
            GM_ST(gm_t6evimt84lndq, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 16, t481);
            goto gm_fdone;
        }
        goto me106;
mn410:;
ma111:;
        uint8_t t485 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t485 != ((uint8_t)16ULL)) goto pa484;
        goto po483;
pa484:;
        uint8_t t487 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t487 != ((uint8_t)17ULL)) goto pa486;
        goto po483;
pa486:;
        goto mn482;
po483:;
        {
            uint64_t t489 = (uintptr_t)((uint64_t)1ULL);
            uint64_t t490 = GM_LD(uintptr_t, ((uint8_t *)&gm_l9) + 8);
            if (t489 >= (uintptr_t)(t490)) gm_trap_index(t489, t490, gm_srcue4ionbe9lgh, 436, 18);
            gm_t6evimt84lndq t491 = GM_LD(gm_t6evimt84lndq, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l9)) + t489 * 48));
            F.l32 = t491;
        }
        {
            uint8_t t497 = GM_LD(uint8_t, (((uint8_t *)&F.l10) + 0));
            switch (t497) { case ((uint8_t)0ULL): goto ma493; case ((uint8_t)1ULL): goto ma494; case ((uint8_t)2ULL): goto ma495; case ((uint8_t)3ULL): goto ma495; case ((uint8_t)4ULL): goto ma495; case ((uint8_t)5ULL): goto ma495; case ((uint8_t)6ULL): goto ma495; case ((uint8_t)7ULL): goto ma495; case ((uint8_t)8ULL): goto ma495; case ((uint8_t)9ULL): goto ma495; case ((uint8_t)10ULL): goto ma495; case ((uint8_t)11ULL): goto ma495; case ((uint8_t)12ULL): goto ma495; case ((uint8_t)13ULL): goto ma495; default: goto ma495; }
ma493:;
            uint8_t t499 = GM_LD(uint8_t, (((uint8_t *)&F.l10) + 0));
            if (t499 != ((uint8_t)0ULL)) goto mn498;
            unsigned __int128 t500 = GM_LD(unsigned __int128, (((uint8_t *)&F.l10) + 16));
            gm_l33 = t500;
            {
                uint8_t t503 = GM_LD(uint8_t, (((uint8_t *)&F.l32) + 0));
                if (t503 != ((uint8_t)0ULL)) goto ie502;
                unsigned __int128 t504 = GM_LD(unsigned __int128, (((uint8_t *)&F.l32) + 16));
                gm_l34 = t504;
                {
                    void * t506 = gm_l0;
                    unsigned __int128 t507 = gm_l33;
                    gm_t88rcucg1b7uk t508 = gm_l11;
                    gm_tbci5ospk5nnn t509;
                    gm_f_big_of_1cq0qao((void *)&t509, (void *)NULL, cx, NULL, t506, t507, t508);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    void * t510 = gm_l0;
                    unsigned __int128 t511 = gm_l34;
                    uint64_t t512 = (uintptr_t)((uint64_t)1ULL);
                    uint64_t t513 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                    if (t512 >= (uintptr_t)(t513)) gm_trap_index(t512, t513, gm_srcue4ionbe9lgh, 440, 58);
                    gm_t88rcucg1b7uk t514 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t512 * 88) + 0));
                    gm_tbci5ospk5nnn t515;
                    gm_f_big_of_1cq0qao((void *)&t515, (void *)NULL, cx, NULL, t510, t511, t514);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    int32_t t516 = gm_f_cmp_i1knrgu(&t509, &t515);
                    gm_l35 = t516;
                }
                {
                    gm_ttk92ufj2f6eh t517;
                    int32_t t519 = gm_l35;
                    uint8_t t520 = (t519 <= ((int32_t)0LL));
                    uint8_t t521 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l1) + 0) == ((uint8_t)16ULL)));
                    uint8_t t522 = ((t520 == t521));
                    if (!t522) goto ie518;
                    gm_t6evimt84lndq t524 = F.l10;
                    gm_t6evimt84lndq t525 = t524;
                    gm_ttk92ufj2f6eh t526;
                    memset(&t526, 0, sizeof t526);
                    ((uint8_t *)&t526)[0] = 0;
                    GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t526 + 16), t525);
                    t517 = t526;
                    goto ix518;
ie518:;
                    gm_t6evimt84lndq t528 = F.l32;
                    gm_t6evimt84lndq t529 = t528;
                    gm_ttk92ufj2f6eh t530;
                    memset(&t530, 0, sizeof t530);
                    ((uint8_t *)&t530)[0] = 0;
                    GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t530 + 16), t529);
                    t517 = t530;
ix518:;
                    uint8_t t531 = ((((uint8_t *)&t517))[0] != 0);
                    F.ret = t517;
                    goto gm_fret;
                }
                goto ix502;
ie502:;
                {
                    void * t533 = gm_l0;
                    gm_tl874t2atnlh3 t534 = gm_l5;
                    gm_b16_8 t535;
                    GM_ST(void *, (uint8_t *)&t535, (void *)(gm_sv7lsha1e0i8r));
                    GM_ST(uintptr_t, (uint8_t *)&t535 + 8, (uintptr_t)(12));
                    gm_t39uai1ajk3qv t536;
                    gm_f_unsupported_2seq49l((void *)&t536, t533, GM_CONV(gm_cseahvg8v7p0vs, t534), t535);
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 8, 0);
                    ((uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out))[0] = 1;
                    GM_ST(gm_t39uai1ajk3qv, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 16, t536);
                    goto gm_fdone;
                }
ix502:;
            }
            goto me492;
mn498:;
ma494:;
            uint8_t t538 = GM_LD(uint8_t, (((uint8_t *)&F.l10) + 0));
            if (t538 != ((uint8_t)1ULL)) goto mn537;
            double t539 = GM_LD(double, (((uint8_t *)&F.l10) + 16));
            gm_l36 = t539;
            {
                uint8_t t542 = GM_LD(uint8_t, (((uint8_t *)&F.l32) + 0));
                if (t542 != ((uint8_t)1ULL)) goto ie541;
                double t543 = GM_LD(double, (((uint8_t *)&F.l32) + 16));
                gm_l37 = t543;
                {
                    double t546 = gm_l36;
                    double t547 = gm_l36;
                    uint8_t t548 = (!(t546 == t547));
                    if (!t548) goto ie545;
                    {
                        const uint8_t *t550 = ((uint8_t *)&F.l32);
                        memset(&(*(gm_ttk92ufj2f6eh *)gm_out), 0, sizeof (*(gm_ttk92ufj2f6eh *)gm_out));
                        ((uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out))[0] = 0;
                        memmove((uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 16, t550, 48);
                        goto gm_fdone;
                    }
                    goto ix545;
ie545:;
ix545:;
                }
                {
                    double t552 = gm_l37;
                    double t553 = gm_l37;
                    uint8_t t554 = (!(t552 == t553));
                    if (!t554) goto ie551;
                    {
                        const uint8_t *t556 = ((uint8_t *)&F.l10);
                        memset(&(*(gm_ttk92ufj2f6eh *)gm_out), 0, sizeof (*(gm_ttk92ufj2f6eh *)gm_out));
                        ((uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out))[0] = 0;
                        memmove((uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 16, t556, 48);
                        goto gm_fdone;
                    }
                    goto ix551;
ie551:;
ix551:;
                }
                {
                    gm_ttk92ufj2f6eh t557;
                    double t559 = gm_l36;
                    double t560 = gm_l37;
                    uint8_t t561 = (t559 <= t560);
                    uint8_t t562 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l1) + 0) == ((uint8_t)16ULL)));
                    uint8_t t563 = ((t561 == t562));
                    if (!t563) goto ie558;
                    gm_t6evimt84lndq t565 = F.l10;
                    gm_t6evimt84lndq t566 = t565;
                    gm_ttk92ufj2f6eh t567;
                    memset(&t567, 0, sizeof t567);
                    ((uint8_t *)&t567)[0] = 0;
                    GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t567 + 16), t566);
                    t557 = t567;
                    goto ix558;
ie558:;
                    gm_t6evimt84lndq t569 = F.l32;
                    gm_t6evimt84lndq t570 = t569;
                    gm_ttk92ufj2f6eh t571;
                    memset(&t571, 0, sizeof t571);
                    ((uint8_t *)&t571)[0] = 0;
                    GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t571 + 16), t570);
                    t557 = t571;
ix558:;
                    uint8_t t572 = ((((uint8_t *)&t557))[0] != 0);
                    F.ret = t557;
                    goto gm_fret;
                }
                goto ix541;
ie541:;
                {
                    void * t574 = gm_l0;
                    gm_tl874t2atnlh3 t575 = gm_l5;
                    gm_b16_8 t576;
                    GM_ST(void *, (uint8_t *)&t576, (void *)(gm_sv7lsha1e0i8r));
                    GM_ST(uintptr_t, (uint8_t *)&t576 + 8, (uintptr_t)(12));
                    gm_t39uai1ajk3qv t577;
                    gm_f_unsupported_2seq49l((void *)&t577, t574, GM_CONV(gm_cseahvg8v7p0vs, t575), t576);
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 8, 0);
                    ((uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out))[0] = 1;
                    GM_ST(gm_t39uai1ajk3qv, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 16, t577);
                    goto gm_fdone;
                }
ix541:;
            }
            goto me492;
mn537:;
ma495:;
            void * t579 = gm_l0;
            gm_tl874t2atnlh3 t580 = gm_l5;
            gm_b16_8 t581;
            GM_ST(void *, (uint8_t *)&t581, (void *)(gm_sv7lsha1e0i8r));
            GM_ST(uintptr_t, (uint8_t *)&t581 + 8, (uintptr_t)(12));
            gm_t39uai1ajk3qv t582;
            gm_f_unsupported_2seq49l((void *)&t582, t579, GM_CONV(gm_cseahvg8v7p0vs, t580), t581);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 8, 0);
            ((uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out))[0] = 1;
            GM_ST(gm_t39uai1ajk3qv, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 16, t582);
            goto gm_fdone;
            goto me492;
mn578:;
ma496:;
            gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 437, 13);
me492:;
        }
        goto me106;
mn482:;
ma112:;
        uint8_t t584 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t584 != ((uint8_t)18ULL)) goto mn583;
        {
            uint8_t t587 = GM_LD(uint8_t, (((uint8_t *)&F.l10) + 0));
            if (t587 != ((uint8_t)0ULL)) goto ie586;
            unsigned __int128 t588 = GM_LD(unsigned __int128, (((uint8_t *)&F.l10) + 16));
            gm_l38 = t588;
            {
                uint64_t t591 = (uintptr_t)((uint64_t)1ULL);
                uint64_t t592 = GM_LD(uintptr_t, ((uint8_t *)&gm_l9) + 8);
                if (t591 >= (uintptr_t)(t592)) gm_trap_index(t591, t592, gm_srcue4ionbe9lgh, 454, 25);
                uint8_t t593 = GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l9)) + t591 * 48) + 0));
                if (t593 != ((uint8_t)0ULL)) goto ie590;
                unsigned __int128 t594 = GM_LD(unsigned __int128, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l9)) + t591 * 48) + 16));
                gm_l39 = t594;
                {
                    uint64_t t597 = (uintptr_t)((uint64_t)2ULL);
                    uint64_t t598 = GM_LD(uintptr_t, ((uint8_t *)&gm_l9) + 8);
                    if (t597 >= (uintptr_t)(t598)) gm_trap_index(t597, t598, gm_srcue4ionbe9lgh, 455, 25);
                    uint8_t t599 = GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l9)) + t597 * 48) + 0));
                    if (t599 != ((uint8_t)0ULL)) goto ie596;
                    unsigned __int128 t600 = GM_LD(unsigned __int128, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l9)) + t597 * 48) + 16));
                    gm_l40 = t600;
                    {
                        void * t602 = gm_l0;
                        unsigned __int128 t603 = gm_l38;
                        gm_t88rcucg1b7uk t604 = gm_l11;
                        gm_f_big_of_1cq0qao((void *)&F.l41, (void *)NULL, cx, NULL, t602, t603, t604);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    {
                        gm_tbci5ospk5nnn t606 = F.l41;
                        void * t607 = gm_l0;
                        unsigned __int128 t608 = gm_l39;
                        gm_t88rcucg1b7uk t609 = gm_l11;
                        gm_tbci5ospk5nnn t610;
                        gm_f_big_of_1cq0qao((void *)&t610, (void *)NULL, cx, NULL, t607, t608, t609);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        int32_t t611 = gm_f_cmp_i1knrgu(&t606, &t610);
                        uint8_t t612 = (t611 < ((int32_t)0LL));
                        if (!t612) goto ie605;
                        {
                            uint64_t t614 = (uintptr_t)((uint64_t)1ULL);
                            uint64_t t615 = GM_LD(uintptr_t, ((uint8_t *)&gm_l9) + 8);
                            if (t614 >= (uintptr_t)(t615)) gm_trap_index(t614, t615, gm_srcue4ionbe9lgh, 457, 53);
                            const uint8_t *t616 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l9)) + t614 * 48);
                            memset(&(*(gm_ttk92ufj2f6eh *)gm_out), 0, sizeof (*(gm_ttk92ufj2f6eh *)gm_out));
                            ((uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out))[0] = 0;
                            memmove((uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 16, t616, 48);
                            goto gm_fdone;
                        }
                        goto ix605;
ie605:;
ix605:;
                    }
                    {
                        gm_tbci5ospk5nnn t618 = F.l41;
                        void * t619 = gm_l0;
                        unsigned __int128 t620 = gm_l40;
                        gm_t88rcucg1b7uk t621 = gm_l11;
                        gm_tbci5ospk5nnn t622;
                        gm_f_big_of_1cq0qao((void *)&t622, (void *)NULL, cx, NULL, t619, t620, t621);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        int32_t t623 = gm_f_cmp_i1knrgu(&t618, &t622);
                        uint8_t t624 = (t623 > ((int32_t)0LL));
                        if (!t624) goto ie617;
                        {
                            uint64_t t626 = (uintptr_t)((uint64_t)2ULL);
                            uint64_t t627 = GM_LD(uintptr_t, ((uint8_t *)&gm_l9) + 8);
                            if (t626 >= (uintptr_t)(t627)) gm_trap_index(t626, t627, gm_srcue4ionbe9lgh, 458, 53);
                            const uint8_t *t628 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l9)) + t626 * 48);
                            memset(&(*(gm_ttk92ufj2f6eh *)gm_out), 0, sizeof (*(gm_ttk92ufj2f6eh *)gm_out));
                            ((uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out))[0] = 0;
                            memmove((uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 16, t628, 48);
                            goto gm_fdone;
                        }
                        goto ix617;
ie617:;
ix617:;
                    }
                    {
                        const uint8_t *t629 = ((uint8_t *)&F.l10);
                        memset(&(*(gm_ttk92ufj2f6eh *)gm_out), 0, sizeof (*(gm_ttk92ufj2f6eh *)gm_out));
                        ((uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out))[0] = 0;
                        memmove((uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 16, t629, 48);
                        goto gm_fdone;
                    }
                    goto ix596;
ie596:;
                    {
                        void * t631 = gm_l0;
                        gm_tl874t2atnlh3 t632 = gm_l5;
                        gm_b16_8 t633;
                        GM_ST(void *, (uint8_t *)&t633, (void *)(gm_sv7lsha1e0i8r));
                        GM_ST(uintptr_t, (uint8_t *)&t633 + 8, (uintptr_t)(12));
                        gm_t39uai1ajk3qv t634;
                        gm_f_unsupported_2seq49l((void *)&t634, t631, GM_CONV(gm_cseahvg8v7p0vs, t632), t633);
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 8, 0);
                        ((uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out))[0] = 1;
                        GM_ST(gm_t39uai1ajk3qv, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 16, t634);
                        goto gm_fdone;
                    }
ix596:;
                }
                goto ix590;
ie590:;
                {
                    void * t636 = gm_l0;
                    gm_tl874t2atnlh3 t637 = gm_l5;
                    gm_b16_8 t638;
                    GM_ST(void *, (uint8_t *)&t638, (void *)(gm_sv7lsha1e0i8r));
                    GM_ST(uintptr_t, (uint8_t *)&t638 + 8, (uintptr_t)(12));
                    gm_t39uai1ajk3qv t639;
                    gm_f_unsupported_2seq49l((void *)&t639, t636, GM_CONV(gm_cseahvg8v7p0vs, t637), t638);
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 8, 0);
                    ((uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out))[0] = 1;
                    GM_ST(gm_t39uai1ajk3qv, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 16, t639);
                    goto gm_fdone;
                }
ix590:;
            }
            goto ix586;
ie586:;
            {
                void * t641 = gm_l0;
                gm_tl874t2atnlh3 t642 = gm_l5;
                gm_b16_8 t643;
                GM_ST(void *, (uint8_t *)&t643, (void *)(gm_seg4idqqmijmd));
                GM_ST(uintptr_t, (uint8_t *)&t643 + 8, (uintptr_t)(33));
                gm_t39uai1ajk3qv t644;
                gm_f_unsupported_2seq49l((void *)&t644, t641, GM_CONV(gm_cseahvg8v7p0vs, t642), t643);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 8, 0);
                ((uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out))[0] = 1;
                GM_ST(gm_t39uai1ajk3qv, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 16, t644);
                goto gm_fdone;
            }
ix586:;
        }
        goto me106;
mn583:;
ma113:;
        uint8_t t646 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t646 != ((uint8_t)19ULL)) goto mn645;
        uint8_t t652 = GM_LD(uint8_t, (((uint8_t *)&F.l10) + 0));
        switch (t652) { case ((uint8_t)0ULL): goto ma648; case ((uint8_t)1ULL): goto ma649; case ((uint8_t)2ULL): goto ma650; case ((uint8_t)3ULL): goto ma650; case ((uint8_t)4ULL): goto ma650; case ((uint8_t)5ULL): goto ma650; case ((uint8_t)6ULL): goto ma650; case ((uint8_t)7ULL): goto ma650; case ((uint8_t)8ULL): goto ma650; case ((uint8_t)9ULL): goto ma650; case ((uint8_t)10ULL): goto ma650; case ((uint8_t)11ULL): goto ma650; case ((uint8_t)12ULL): goto ma650; case ((uint8_t)13ULL): goto ma650; default: goto ma650; }
ma648:;
        uint8_t t654 = GM_LD(uint8_t, (((uint8_t *)&F.l10) + 0));
        if (t654 != ((uint8_t)0ULL)) goto mn653;
        unsigned __int128 t655 = GM_LD(unsigned __int128, (((uint8_t *)&F.l10) + 16));
        gm_l42 = t655;
        {
            void * t657 = gm_l0;
            unsigned __int128 t658 = gm_l42;
            gm_t88rcucg1b7uk t659 = gm_l11;
            gm_f_big_of_1cq0qao((void *)&F.l43, (void *)NULL, cx, NULL, t657, t658, t659);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t660 = gm_l0;
            void * t661 = gm_l3;
            gm_t88rcucg1b7uk t662 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t661) + 0));
            gm_f_int_range_hue3vms((void *)&F.l44, (void *)NULL, cx, NULL, t660, t662);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_f_abs_v7cf214((void *)&F.l45, (void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l43)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            int32_t t664 = gm_f_cmp_i1knrgu(&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l45)), &(*(gm_tbci5ospk5nnn *)(((uint8_t *)&F.l44) + 48)));
            uint8_t t665 = (t664 > ((int32_t)0LL));
            if (!t665) goto ie663;
            {
                void * t667 = gm_l0;
                gm_tl874t2atnlh3 t668 = gm_l5;
                gm_b16_8 t669;
                GM_ST(void *, (uint8_t *)&t669, (void *)(gm_sjn4krvshoajd));
                GM_ST(uintptr_t, (uint8_t *)&t669 + 8, (uintptr_t)(25));
                gm_t39uai1ajk3qv t670;
                gm_f_trap_jftbss6((void *)&t670, (void *)NULL, cx, NULL, t667, t668, t669);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(uint8_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 8, 0);
                ((uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out))[0] = 1;
                GM_ST(gm_t39uai1ajk3qv, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 16, t670);
                goto gm_fdone;
            }
            goto ix663;
ie663:;
ix663:;
        }
        {
            void * t671 = gm_l0;
            unsigned __int128 t672 = gm_f_bits128_jigkui4(&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l45)));
            void * t673 = gm_l3;
            gm_t88rcucg1b7uk t674 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t673) + 0));
            unsigned __int128 t675 = gm_f_wrap_bt8r149(t671, t672, GM_CONV(gm_csguankko4lmhd, t674));
            gm_t6evimt84lndq t676;
            GM_ST(uint8_t, (uint8_t *)&t676 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t676 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t676 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t676 + 8, 0);
            GM_ST(uint64_t, (uint8_t *)&t676 + 32, 0);
            GM_ST(uint64_t, (uint8_t *)&t676 + 40, 0);
            GM_ST(uint8_t, ((uint8_t *)&t676 + 0), ((uint8_t)0ULL));
            GM_ST(unsigned __int128, ((uint8_t *)&t676 + 16), t675);
            memset(&(*(gm_ttk92ufj2f6eh *)gm_out), 0, sizeof (*(gm_ttk92ufj2f6eh *)gm_out));
            ((uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out))[0] = 0;
            GM_ST(gm_t6evimt84lndq, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 16, t676);
            goto gm_fdone;
        }
        goto me647;
mn653:;
ma649:;
        uint8_t t678 = GM_LD(uint8_t, (((uint8_t *)&F.l10) + 0));
        if (t678 != ((uint8_t)1ULL)) goto mn677;
        double t679 = GM_LD(double, (((uint8_t *)&F.l10) + 16));
        gm_l46 = t679;
        double t680;
        uint8_t t682 = 0;
        double t683 = gm_l46;
        uint8_t t684 = (t683 < ((double)0e0));
        if (!t684) {
            uint8_t t685 = 0;
            double t686 = gm_l46;
            uint8_t t687 = ((t686 == ((double)0e0)));
            if (t687) {
                double t688 = gm_l46;
                uint64_t t689;
                memset(&t689, 0, sizeof t689); memcpy(&t689, ((uint8_t *)&t688), sizeof t689 < 8 ? sizeof t689 : 8);
                unsigned __int128 n690 = (unsigned __int128)((uint64_t)63ULL);
                if (n690 >= 64) gm_trap_shift((uint64_t)n690, "u64", gm_srcue4ionbe9lgh, 469, 68);
                uint64_t t691 = (uint64_t)(t689 >> (int)n690);
                uint8_t t692 = ((t691 == ((uint64_t)1ULL)));
                t685 = t692;
            } else t685 = 0;
            t682 = t685;
        } else t682 = 1;
        if (!t682) goto ie681;
        double t694 = gm_l46;
        double t695 = -t694;
        double t696 = t695;
        t680 = t696;
        goto ix681;
ie681:;
        double t698 = gm_l46;
        double t699 = t698;
        t680 = t699;
ix681:;
        gm_t6evimt84lndq t700;
        memset(&t700, 0, sizeof t700);
        GM_ST(uint8_t, ((uint8_t *)&t700 + 0), ((uint8_t)1ULL));
        GM_ST(double, ((uint8_t *)&t700 + 16), t680);
        memset(&(*(gm_ttk92ufj2f6eh *)gm_out), 0, sizeof (*(gm_ttk92ufj2f6eh *)gm_out));
        ((uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out))[0] = 0;
        GM_ST(gm_t6evimt84lndq, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 16, t700);
        goto gm_fdone;
        goto me647;
mn677:;
ma650:;
        void * t702 = gm_l0;
        gm_tl874t2atnlh3 t703 = gm_l5;
        gm_b16_8 t704;
        GM_ST(void *, (uint8_t *)&t704, (void *)(gm_sv7lsha1e0i8r));
        GM_ST(uintptr_t, (uint8_t *)&t704 + 8, (uintptr_t)(12));
        gm_t39uai1ajk3qv t705;
        gm_f_unsupported_2seq49l((void *)&t705, t702, GM_CONV(gm_cseahvg8v7p0vs, t703), t704);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 8, 0);
        ((uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out))[0] = 1;
        GM_ST(gm_t39uai1ajk3qv, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 16, t705);
        goto gm_fdone;
        goto me647;
mn701:;
ma651:;
        gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 461, 17);
me647:;
        goto me106;
mn645:;
ma114:;
        uint8_t t707 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t707 != ((uint8_t)20ULL)) goto mn706;
        {
            uint8_t t710 = GM_LD(uint8_t, (((uint8_t *)&F.l10) + 0));
            if (t710 != ((uint8_t)0ULL)) goto ie709;
            unsigned __int128 t711 = GM_LD(unsigned __int128, (((uint8_t *)&F.l10) + 16));
            gm_l47 = t711;
            {
                uint64_t t714 = (uintptr_t)((uint64_t)1ULL);
                uint64_t t715 = GM_LD(uintptr_t, ((uint8_t *)&gm_l9) + 8);
                if (t714 >= (uintptr_t)(t715)) gm_trap_index(t714, t715, gm_srcue4ionbe9lgh, 474, 24);
                uint8_t t716 = GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l9)) + t714 * 48) + 0));
                if (t716 != ((uint8_t)0ULL)) goto ie713;
                unsigned __int128 t717 = GM_LD(unsigned __int128, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l9)) + t714 * 48) + 16));
                gm_l48 = t717;
                {
                    void * t719 = gm_l0;
                    unsigned __int128 t720 = gm_l47;
                    gm_t88rcucg1b7uk t721 = gm_l11;
                    gm_tbci5ospk5nnn t722;
                    gm_f_big_of_1cq0qao((void *)&t722, (void *)NULL, cx, NULL, t719, t720, t721);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    void * t723 = gm_l0;
                    unsigned __int128 t724 = gm_l48;
                    uint64_t t725 = (uintptr_t)((uint64_t)1ULL);
                    uint64_t t726 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                    if (t725 >= (uintptr_t)(t726)) gm_trap_index(t725, t726, gm_srcue4ionbe9lgh, 475, 50);
                    gm_t88rcucg1b7uk t727 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t725 * 88) + 0));
                    gm_tbci5ospk5nnn t728;
                    gm_f_big_of_1cq0qao((void *)&t728, (void *)NULL, cx, NULL, t723, t724, t727);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tbci5ospk5nnn t729;
                    gm_f_sub_pktm5cr((void *)&t729, (void *)NULL, cx, NULL, &t722, &t728);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_f_abs_v7cf214((void *)&F.l49, (void *)NULL, cx, NULL, &t729);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                {
                    void * t730 = gm_l0;
                    unsigned __int128 t731 = gm_f_bits128_jigkui4(&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l49)));
                    void * t732 = gm_l3;
                    gm_t88rcucg1b7uk t733 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t732) + 0));
                    unsigned __int128 t734 = gm_f_wrap_bt8r149(t730, t731, GM_CONV(gm_csguankko4lmhd, t733));
                    gm_t6evimt84lndq t735;
                    GM_ST(uint8_t, (uint8_t *)&t735 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t735 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t735 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t735 + 8, 0);
                    GM_ST(uint64_t, (uint8_t *)&t735 + 32, 0);
                    GM_ST(uint64_t, (uint8_t *)&t735 + 40, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t735 + 0), ((uint8_t)0ULL));
                    GM_ST(unsigned __int128, ((uint8_t *)&t735 + 16), t734);
                    memset(&(*(gm_ttk92ufj2f6eh *)gm_out), 0, sizeof (*(gm_ttk92ufj2f6eh *)gm_out));
                    ((uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out))[0] = 0;
                    GM_ST(gm_t6evimt84lndq, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 16, t735);
                    goto gm_fdone;
                }
                goto ix713;
ie713:;
                {
                    void * t737 = gm_l0;
                    gm_tl874t2atnlh3 t738 = gm_l5;
                    gm_b16_8 t739;
                    GM_ST(void *, (uint8_t *)&t739, (void *)(gm_sv7lsha1e0i8r));
                    GM_ST(uintptr_t, (uint8_t *)&t739 + 8, (uintptr_t)(12));
                    gm_t39uai1ajk3qv t740;
                    gm_f_unsupported_2seq49l((void *)&t740, t737, GM_CONV(gm_cseahvg8v7p0vs, t738), t739);
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 8, 0);
                    ((uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out))[0] = 1;
                    GM_ST(gm_t39uai1ajk3qv, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 16, t740);
                    goto gm_fdone;
                }
ix713:;
            }
            goto ix709;
ie709:;
            {
                void * t742 = gm_l0;
                gm_tl874t2atnlh3 t743 = gm_l5;
                gm_b16_8 t744;
                GM_ST(void *, (uint8_t *)&t744, (void *)(gm_sv7lsha1e0i8r));
                GM_ST(uintptr_t, (uint8_t *)&t744 + 8, (uintptr_t)(12));
                gm_t39uai1ajk3qv t745;
                gm_f_unsupported_2seq49l((void *)&t745, t742, GM_CONV(gm_cseahvg8v7p0vs, t743), t744);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 8, 0);
                ((uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out))[0] = 1;
                GM_ST(gm_t39uai1ajk3qv, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 16, t745);
                goto gm_fdone;
            }
ix709:;
        }
        goto me106;
mn706:;
ma115:;
        uint8_t t747 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t747 != ((uint8_t)21ULL)) goto mn746;
        gm_t9qkh0rsooc8d t748 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l1) + 4));
        gm_l50 = t748;
        {
            uint8_t t751 = GM_LD(uint8_t, (((uint8_t *)&F.l10) + 0));
            if (t751 != ((uint8_t)0ULL)) goto ie750;
            unsigned __int128 t752 = GM_LD(unsigned __int128, (((uint8_t *)&F.l10) + 16));
            gm_l51 = t752;
            {
                uint64_t t755 = (uintptr_t)((uint64_t)1ULL);
                uint64_t t756 = GM_LD(uintptr_t, ((uint8_t *)&gm_l9) + 8);
                if (t755 >= (uintptr_t)(t756)) gm_trap_index(t755, t756, gm_srcue4ionbe9lgh, 480, 24);
                uint8_t t757 = GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l9)) + t755 * 48) + 0));
                if (t757 != ((uint8_t)0ULL)) goto ie754;
                unsigned __int128 t758 = GM_LD(unsigned __int128, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l9)) + t755 * 48) + 16));
                gm_l52 = t758;
                {
                    void * t760 = gm_l0;
                    unsigned __int128 t761 = gm_l51;
                    gm_t88rcucg1b7uk t762 = gm_l11;
                    gm_tbci5ospk5nnn t763;
                    gm_f_big_of_1cq0qao((void *)&t763, (void *)NULL, cx, NULL, t760, t761, t762);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    unsigned __int128 t764 = gm_l52;
                    uint32_t t765 = (uint32_t)t764;
                    gm_f_pow_mhlb5fr((void *)&F.l53, (void *)NULL, cx, NULL, &t763, t765);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                {
                    gm_t88rcucg1b7uk t766 = gm_l11;
                    gm_l54 = t766;
                }
                {
                    void * t767 = gm_l0;
                    gm_t88rcucg1b7uk t768 = gm_l54;
                    gm_f_int_range_hue3vms((void *)&F.l55, (void *)NULL, cx, NULL, t767, t768);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                {
                    uint8_t t769 = 0;
                    int32_t t770 = gm_f_cmp_i1knrgu(&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l53)), &(*(gm_tbci5ospk5nnn *)(((uint8_t *)&F.l55) + 0)));
                    uint8_t t771 = (t770 >= ((int32_t)0LL));
                    if (t771) {
                        int32_t t772 = gm_f_cmp_i1knrgu(&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l53)), &(*(gm_tbci5ospk5nnn *)(((uint8_t *)&F.l55) + 48)));
                        uint8_t t773 = (t772 <= ((int32_t)0LL));
                        t769 = t773;
                    } else t769 = 0;
                    gm_l56 = t769;
                }
                {
                    uint8_t t779 = GM_LD(uint8_t, (((uint8_t *)&gm_l50) + 0));
                    switch (t779) { case ((uint8_t)0ULL): goto ma776; case ((uint8_t)1ULL): goto ma777; case ((uint8_t)2ULL): goto ma777; case ((uint8_t)3ULL): goto ma775; default: goto ma777; }
ma775:;
                    uint8_t t781 = GM_LD(uint8_t, (((uint8_t *)&gm_l50) + 0));
                    if (t781 != ((uint8_t)3ULL)) goto mn780;
                    gm_ttk92ufj2f6eh t782;
                    uint8_t t784 = gm_l56;
                    if (!t784) goto ie783;
                    void * t786 = gm_l0;
                    unsigned __int128 t787 = gm_f_bits128_jigkui4(&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l53)));
                    gm_t88rcucg1b7uk t788 = gm_l54;
                    unsigned __int128 t789 = gm_f_wrap_bt8r149(t786, t787, GM_CONV(gm_csguankko4lmhd, t788));
                    gm_t6evimt84lndq t790;
                    GM_ST(uint8_t, (uint8_t *)&t790 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t790 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t790 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t790 + 8, 0);
                    GM_ST(uint64_t, (uint8_t *)&t790 + 32, 0);
                    GM_ST(uint64_t, (uint8_t *)&t790 + 40, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t790 + 0), ((uint8_t)0ULL));
                    GM_ST(unsigned __int128, ((uint8_t *)&t790 + 16), t789);
                    gm_tq7s98ihonkju t791 = gm_f_new_70le0lp((void *)NULL, cx, NULL, &t790, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_ksjvoab7999)));
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    if (((((uint8_t *)&t791))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t791) + 8, gm_srcue4ionbe9lgh, 486, 53);
                    void * t792 = GM_LD(void *, (((uint8_t *)&t791) + 8));
                    gm_t6evimt84lndq t793;
                    memset(&t793, 0, sizeof t793);
                    GM_ST(uint8_t, ((uint8_t *)&t793 + 0), ((uint8_t)7ULL));
                    GM_ST(void *, ((uint8_t *)&t793 + 16), t792);
                    gm_t6evimt84lndq t794 = t793;
                    gm_ttk92ufj2f6eh t795;
                    memset(&t795, 0, sizeof t795);
                    ((uint8_t *)&t795)[0] = 0;
                    GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t795 + 16), t794);
                    t782 = t795;
                    goto ix783;
ie783:;
                    gm_t6evimt84lndq t797;
                    memset(&t797, 0, sizeof t797);
                    GM_ST(uint8_t, ((uint8_t *)&t797 + 0), ((uint8_t)6ULL));
                    gm_t6evimt84lndq t798 = t797;
                    gm_ttk92ufj2f6eh t799;
                    memset(&t799, 0, sizeof t799);
                    ((uint8_t *)&t799)[0] = 0;
                    GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t799 + 16), t798);
                    t782 = t799;
ix783:;
                    uint8_t t800 = ((((uint8_t *)&t782))[0] != 0);
                    F.ret = t782;
                    goto gm_fret;
                    goto me774;
mn780:;
ma776:;
                    uint8_t t802 = GM_LD(uint8_t, (((uint8_t *)&gm_l50) + 0));
                    if (t802 != ((uint8_t)0ULL)) goto mn801;
                    uint8_t t804 = gm_l56;
                    uint8_t t805 = (uint8_t)!t804;
                    if (!t805) goto ie803;
                    {
                        void * t807 = gm_l0;
                        unsigned __int128 t808 = gm_l51;
                        gm_t88rcucg1b7uk t809 = gm_l11;
                        gm_tbci5ospk5nnn t810;
                        gm_f_big_of_1cq0qao((void *)&t810, (void *)NULL, cx, NULL, t807, t808, t809);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_b16_8 t811 = gm_f_to_decimal_8bshn5b((void *)NULL, cx, NULL, &t810);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_l57 = t811;
                    }
                    {
                        void * t812 = gm_l0;
                        gm_tl874t2atnlh3 t813 = gm_l5;
                        gm_tou0girmhri5g t814;
                        gm_b16_8 t815 = gm_l57;
                        GM_ST(gm_b16_8, (uint8_t *)&t814 + 0, t815);
                        unsigned __int128 t816 = gm_l52;
                        GM_ST(unsigned __int128, (uint8_t *)&t814 + 16, t816);
                        gm_tabb710d9mq2e t817;
                        gm_f_format_0sujqbp((void *)&t817, (void *)NULL, cx, NULL, &t814);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        if (((((uint8_t *)&t817))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t817) + 8, gm_srcue4ionbe9lgh, 489, 42);
                        gm_b16_8 t818 = GM_LD(gm_b16_8, (((uint8_t *)&t817) + 8));
                        gm_b16_8 t819 = GM_LD(gm_b16_8, ((uint8_t *)&t818));
                        gm_t39uai1ajk3qv t820;
                        gm_f_trap_jftbss6((void *)&t820, (void *)NULL, cx, NULL, t812, t813, t819);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 8, 0);
                        ((uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out))[0] = 1;
                        GM_ST(gm_t39uai1ajk3qv, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 16, t820);
                        goto gm_fdone;
                    }
                    goto ix803;
ie803:;
ix803:;
                    goto me774;
mn801:;
ma777:;
                    goto me774;
mn821:;
ma778:;
                    gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 485, 13);
me774:;
                }
                {
                    void * t823 = gm_l0;
                    gm_t9qkh0rsooc8d t824 = gm_l50;
                    gm_t88rcucg1b7uk t825 = gm_l54;
                    gm_b16_8 t826;
                    GM_ST(void *, (uint8_t *)&t826, (void *)(gm_svpofgjtij7vj));
                    GM_ST(uintptr_t, (uint8_t *)&t826 + 8, (uintptr_t)(3));
                    gm_tl874t2atnlh3 t827 = gm_l5;
                    gm_t8carr7mku484 t828;
                    gm_f_fit_8hvi4bv((void *)&t828, (void *)NULL, cx, NULL, t823, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l53)), t824, t825, t826, t827);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    if (((((uint8_t *)&t828))[0] != 0)) {
                        gm_ttk92ufj2f6eh t829;
                        GM_ST(uint8_t, (uint8_t *)&t829 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t829 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t829 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t829 + 8, 0);
                        ((uint8_t *)&t829)[0] = 1;
                        GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t829 + 16), (*(gm_t39uai1ajk3qv *)(((uint8_t *)&t828) + 16)));
                        F.ret = t829;
                        goto gm_fret;
                    }
                    unsigned __int128 t830 = GM_LD(unsigned __int128, (((uint8_t *)&t828) + 16));
                    gm_t6evimt84lndq t831;
                    GM_ST(uint8_t, (uint8_t *)&t831 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t831 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t831 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t831 + 8, 0);
                    GM_ST(uint64_t, (uint8_t *)&t831 + 32, 0);
                    GM_ST(uint64_t, (uint8_t *)&t831 + 40, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t831 + 0), ((uint8_t)0ULL));
                    GM_ST(unsigned __int128, ((uint8_t *)&t831 + 16), t830);
                    memset(&(*(gm_ttk92ufj2f6eh *)gm_out), 0, sizeof (*(gm_ttk92ufj2f6eh *)gm_out));
                    ((uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out))[0] = 0;
                    GM_ST(gm_t6evimt84lndq, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 16, t831);
                    goto gm_fdone;
                }
                goto ix754;
ie754:;
                {
                    void * t833 = gm_l0;
                    gm_tl874t2atnlh3 t834 = gm_l5;
                    gm_b16_8 t835;
                    GM_ST(void *, (uint8_t *)&t835, (void *)(gm_sv7lsha1e0i8r));
                    GM_ST(uintptr_t, (uint8_t *)&t835 + 8, (uintptr_t)(12));
                    gm_t39uai1ajk3qv t836;
                    gm_f_unsupported_2seq49l((void *)&t836, t833, GM_CONV(gm_cseahvg8v7p0vs, t834), t835);
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 8, 0);
                    ((uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out))[0] = 1;
                    GM_ST(gm_t39uai1ajk3qv, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 16, t836);
                    goto gm_fdone;
                }
ix754:;
            }
            goto ix750;
ie750:;
            {
                void * t838 = gm_l0;
                gm_tl874t2atnlh3 t839 = gm_l5;
                gm_b16_8 t840;
                GM_ST(void *, (uint8_t *)&t840, (void *)(gm_sv7lsha1e0i8r));
                GM_ST(uintptr_t, (uint8_t *)&t840 + 8, (uintptr_t)(12));
                gm_t39uai1ajk3qv t841;
                gm_f_unsupported_2seq49l((void *)&t841, t838, GM_CONV(gm_cseahvg8v7p0vs, t839), t840);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 8, 0);
                ((uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out))[0] = 1;
                GM_ST(gm_t39uai1ajk3qv, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 16, t841);
                goto gm_fdone;
            }
ix750:;
        }
        goto me106;
mn746:;
ma116:;
        uint8_t t843 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t843 != ((uint8_t)22ULL)) goto mn842;
        {
            uint8_t t846 = GM_LD(uint8_t, (((uint8_t *)&F.l10) + 0));
            if (t846 != ((uint8_t)1ULL)) goto ie845;
            double t847 = GM_LD(double, (((uint8_t *)&F.l10) + 16));
            gm_l58 = t847;
            {
                double t849 = gm_l58;
                double t850 = gm_l58;
                uint8_t t851 = (!(t849 == t850));
                gm_t6evimt84lndq t852;
                memset(&t852, 0, sizeof t852);
                GM_ST(uint8_t, ((uint8_t *)&t852 + 0), ((uint8_t)2ULL));
                GM_ST(uint8_t, ((uint8_t *)&t852 + 16), t851);
                memset(&(*(gm_ttk92ufj2f6eh *)gm_out), 0, sizeof (*(gm_ttk92ufj2f6eh *)gm_out));
                ((uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out))[0] = 0;
                GM_ST(gm_t6evimt84lndq, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 16, t852);
                goto gm_fdone;
            }
            goto ix845;
ie845:;
            {
                void * t854 = gm_l0;
                gm_tl874t2atnlh3 t855 = gm_l5;
                gm_b16_8 t856;
                GM_ST(void *, (uint8_t *)&t856, (void *)(gm_sv7lsha1e0i8r));
                GM_ST(uintptr_t, (uint8_t *)&t856 + 8, (uintptr_t)(12));
                gm_t39uai1ajk3qv t857;
                gm_f_unsupported_2seq49l((void *)&t857, t854, GM_CONV(gm_cseahvg8v7p0vs, t855), t856);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 8, 0);
                ((uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out))[0] = 1;
                GM_ST(gm_t39uai1ajk3qv, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 16, t857);
                goto gm_fdone;
            }
ix845:;
        }
        goto me106;
mn842:;
ma117:;
        uint8_t t859 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t859 != ((uint8_t)23ULL)) goto mn858;
        {
            uint8_t t862 = GM_LD(uint8_t, (((uint8_t *)&F.l10) + 0));
            if (t862 != ((uint8_t)1ULL)) goto ie861;
            double t863 = GM_LD(double, (((uint8_t *)&F.l10) + 16));
            gm_l59 = t863;
            {
                uint8_t t865 = 0;
                double t866 = gm_l59;
                double t867 = gm_l59;
                uint8_t t868 = ((t866 == t867));
                if (t868) {
                    double t869 = gm_l59;
                    double t870 = gm_l59;
                    double t871 = (t869 - t870);
                    uint8_t t872 = (!(t871 == ((double)0e0)));
                    t865 = t872;
                } else t865 = 0;
                gm_t6evimt84lndq t873;
                memset(&t873, 0, sizeof t873);
                GM_ST(uint8_t, ((uint8_t *)&t873 + 0), ((uint8_t)2ULL));
                GM_ST(uint8_t, ((uint8_t *)&t873 + 16), t865);
                memset(&(*(gm_ttk92ufj2f6eh *)gm_out), 0, sizeof (*(gm_ttk92ufj2f6eh *)gm_out));
                ((uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out))[0] = 0;
                GM_ST(gm_t6evimt84lndq, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 16, t873);
                goto gm_fdone;
            }
            goto ix861;
ie861:;
            {
                void * t875 = gm_l0;
                gm_tl874t2atnlh3 t876 = gm_l5;
                gm_b16_8 t877;
                GM_ST(void *, (uint8_t *)&t877, (void *)(gm_sv7lsha1e0i8r));
                GM_ST(uintptr_t, (uint8_t *)&t877 + 8, (uintptr_t)(12));
                gm_t39uai1ajk3qv t878;
                gm_f_unsupported_2seq49l((void *)&t878, t875, GM_CONV(gm_cseahvg8v7p0vs, t876), t877);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 8, 0);
                ((uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out))[0] = 1;
                GM_ST(gm_t39uai1ajk3qv, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 16, t878);
                goto gm_fdone;
            }
ix861:;
        }
        goto me106;
mn858:;
ma118:;
        uint8_t t880 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t880 != ((uint8_t)24ULL)) goto mn879;
        {
            uint8_t t883 = GM_LD(uint8_t, (((uint8_t *)&F.l10) + 0));
            if (t883 != ((uint8_t)1ULL)) goto ie882;
            double t884 = GM_LD(double, (((uint8_t *)&F.l10) + 16));
            gm_l60 = t884;
            {
                double t886 = gm_l60;
                double t887 = gm_l60;
                double t888 = (t886 - t887);
                uint8_t t889 = ((t888 == ((double)0e0)));
                gm_t6evimt84lndq t890;
                memset(&t890, 0, sizeof t890);
                GM_ST(uint8_t, ((uint8_t *)&t890 + 0), ((uint8_t)2ULL));
                GM_ST(uint8_t, ((uint8_t *)&t890 + 16), t889);
                memset(&(*(gm_ttk92ufj2f6eh *)gm_out), 0, sizeof (*(gm_ttk92ufj2f6eh *)gm_out));
                ((uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out))[0] = 0;
                GM_ST(gm_t6evimt84lndq, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 16, t890);
                goto gm_fdone;
            }
            goto ix882;
ie882:;
            {
                void * t892 = gm_l0;
                gm_tl874t2atnlh3 t893 = gm_l5;
                gm_b16_8 t894;
                GM_ST(void *, (uint8_t *)&t894, (void *)(gm_sv7lsha1e0i8r));
                GM_ST(uintptr_t, (uint8_t *)&t894 + 8, (uintptr_t)(12));
                gm_t39uai1ajk3qv t895;
                gm_f_unsupported_2seq49l((void *)&t895, t892, GM_CONV(gm_cseahvg8v7p0vs, t893), t894);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 8, 0);
                ((uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out))[0] = 1;
                GM_ST(gm_t39uai1ajk3qv, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 16, t895);
                goto gm_fdone;
            }
ix882:;
        }
        goto me106;
mn879:;
ma119:;
        uint8_t t897 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t897 != ((uint8_t)25ULL)) goto mn896;
        {
            uint8_t t900 = GM_LD(uint8_t, (((uint8_t *)&F.l10) + 0));
            if (t900 != ((uint8_t)1ULL)) goto ie899;
            double t901 = GM_LD(double, (((uint8_t *)&F.l10) + 16));
            gm_l61 = t901;
            {
                double t903 = gm_l61;
                uint64_t t904;
                memset(&t904, 0, sizeof t904); memcpy(&t904, ((uint8_t *)&t903), sizeof t904 < 8 ? sizeof t904 : 8);
                unsigned __int128 n905 = (unsigned __int128)((uint64_t)63ULL);
                if (n905 >= 64) gm_trap_shift((uint64_t)n905, "u64", gm_srcue4ionbe9lgh, 509, 26);
                uint64_t t906 = (uint64_t)(t904 >> (int)n905);
                uint8_t t907 = ((t906 == ((uint64_t)1ULL)));
                gm_t6evimt84lndq t908;
                memset(&t908, 0, sizeof t908);
                GM_ST(uint8_t, ((uint8_t *)&t908 + 0), ((uint8_t)2ULL));
                GM_ST(uint8_t, ((uint8_t *)&t908 + 16), t907);
                memset(&(*(gm_ttk92ufj2f6eh *)gm_out), 0, sizeof (*(gm_ttk92ufj2f6eh *)gm_out));
                ((uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out))[0] = 0;
                GM_ST(gm_t6evimt84lndq, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 16, t908);
                goto gm_fdone;
            }
            goto ix899;
ie899:;
            {
                void * t910 = gm_l0;
                gm_tl874t2atnlh3 t911 = gm_l5;
                gm_b16_8 t912;
                GM_ST(void *, (uint8_t *)&t912, (void *)(gm_sv7lsha1e0i8r));
                GM_ST(uintptr_t, (uint8_t *)&t912 + 8, (uintptr_t)(12));
                gm_t39uai1ajk3qv t913;
                gm_f_unsupported_2seq49l((void *)&t913, t910, GM_CONV(gm_cseahvg8v7p0vs, t911), t912);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 8, 0);
                ((uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out))[0] = 1;
                GM_ST(gm_t39uai1ajk3qv, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 16, t913);
                goto gm_fdone;
            }
ix899:;
        }
        goto me106;
mn896:;
ma120:;
        uint8_t t915 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t915 != ((uint8_t)14ULL)) goto mn914;
        {
            uint8_t t918 = GM_LD(uint8_t, (((uint8_t *)&F.l10) + 0));
            if (t918 != ((uint8_t)10ULL)) goto ie917;
            uint32_t t919 = GM_LD(uint32_t, ((((uint8_t *)&F.l10) + 16) + 0));
            gm_l62 = t919;
            {
                void * t921 = gm_l0;
                gm_t88rcucg1b7uk t922 = gm_l11;
                uint32_t t923 = gm_l62;
                gm_thrdpodrs8pgt t924;
                gm_f_variant_tag_4pbamin((void *)&t924, t921, GM_CONV(gm_csguankko4lmhd, t922), t923);
                gm_tbci5ospk5nnn t925;
                if (((((uint8_t *)&t924))[48] != 0)) {
                    t925 = (*(gm_tbci5ospk5nnn *)(((uint8_t *)&t924) + 0));
                } else {
                    void * t926 = gm_l0;
                    gm_tl874t2atnlh3 t927 = gm_l5;
                    gm_b16_8 t928;
                    GM_ST(void *, (uint8_t *)&t928, (void *)(gm_sv7lsha1e0i8r));
                    GM_ST(uintptr_t, (uint8_t *)&t928 + 8, (uintptr_t)(12));
                    gm_t39uai1ajk3qv t929;
                    gm_f_unsupported_2seq49l((void *)&t929, t926, GM_CONV(gm_cseahvg8v7p0vs, t927), t928);
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 8, 0);
                    ((uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out))[0] = 1;
                    GM_ST(gm_t39uai1ajk3qv, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 16, t929);
                    goto gm_fdone;
                }
                F.l63 = t925;
            }
            {
                void * t930 = gm_l0;
                unsigned __int128 t931 = gm_f_bits128_jigkui4(&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l63)));
                void * t932 = gm_l3;
                gm_t88rcucg1b7uk t933 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t932) + 0));
                unsigned __int128 t934 = gm_f_wrap_bt8r149(t930, t931, GM_CONV(gm_csguankko4lmhd, t933));
                gm_t6evimt84lndq t935;
                GM_ST(uint8_t, (uint8_t *)&t935 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t935 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t935 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t935 + 8, 0);
                GM_ST(uint64_t, (uint8_t *)&t935 + 32, 0);
                GM_ST(uint64_t, (uint8_t *)&t935 + 40, 0);
                GM_ST(uint8_t, ((uint8_t *)&t935 + 0), ((uint8_t)0ULL));
                GM_ST(unsigned __int128, ((uint8_t *)&t935 + 16), t934);
                memset(&(*(gm_ttk92ufj2f6eh *)gm_out), 0, sizeof (*(gm_ttk92ufj2f6eh *)gm_out));
                ((uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out))[0] = 0;
                GM_ST(gm_t6evimt84lndq, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 16, t935);
                goto gm_fdone;
            }
            goto ix917;
ie917:;
            {
                void * t937 = gm_l0;
                gm_tl874t2atnlh3 t938 = gm_l5;
                gm_b16_8 t939;
                GM_ST(void *, (uint8_t *)&t939, (void *)(gm_sv7lsha1e0i8r));
                GM_ST(uintptr_t, (uint8_t *)&t939 + 8, (uintptr_t)(12));
                gm_t39uai1ajk3qv t940;
                gm_f_unsupported_2seq49l((void *)&t940, t937, GM_CONV(gm_cseahvg8v7p0vs, t938), t939);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 8, 0);
                ((uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out))[0] = 1;
                GM_ST(gm_t39uai1ajk3qv, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 16, t940);
                goto gm_fdone;
            }
ix917:;
        }
        goto me106;
mn914:;
ma121:;
        uint8_t t944 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t944 != ((uint8_t)36ULL)) goto pa943;
        goto po942;
pa943:;
        uint8_t t946 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t946 != ((uint8_t)37ULL)) goto pa945;
        goto po942;
pa945:;
        uint8_t t948 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t948 != ((uint8_t)38ULL)) goto pa947;
        goto po942;
pa947:;
        uint8_t t950 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t950 != ((uint8_t)39ULL)) goto pa949;
        goto po942;
pa949:;
        goto mn941;
po942:;
        {
            gm_l64 = ((uint64_t)0ULL);
        }
        {
            gm_tab260m0n3sqj t952;
            memset(&t952, 0, sizeof t952);
            gm_l65 = t952;
        }
        {
            uint8_t t958 = GM_LD(uint8_t, (((uint8_t *)&F.l10) + 0));
            switch (t958) { case ((uint8_t)0ULL): goto ma956; case ((uint8_t)1ULL): goto ma956; case ((uint8_t)2ULL): goto ma956; case ((uint8_t)3ULL): goto ma954; case ((uint8_t)4ULL): goto ma956; case ((uint8_t)5ULL): goto ma955; case ((uint8_t)6ULL): goto ma956; case ((uint8_t)7ULL): goto ma956; case ((uint8_t)8ULL): goto ma956; case ((uint8_t)9ULL): goto ma956; case ((uint8_t)10ULL): goto ma956; case ((uint8_t)11ULL): goto ma956; case ((uint8_t)12ULL): goto ma956; case ((uint8_t)13ULL): goto ma956; default: goto ma956; }
ma954:;
            uint8_t t960 = GM_LD(uint8_t, (((uint8_t *)&F.l10) + 0));
            if (t960 != ((uint8_t)3ULL)) goto mn959;
            void * t961 = GM_LD(void *, (((uint8_t *)&F.l10) + 16));
            gm_l66 = t961;
            {
                void * t963 = gm_l66;
                uint64_t t964 = GM_LD(uintptr_t, (((uint8_t *)t963) + 0) + 8);
                uint64_t t965 = (uint64_t)t964;
                gm_l64 = t965;
            }
            goto me953;
mn959:;
ma955:;
            uint8_t t967 = GM_LD(uint8_t, (((uint8_t *)&F.l10) + 0));
            if (t967 != ((uint8_t)5ULL)) goto mn966;
            gm_to6glrduves6o t968 = GM_LD(gm_to6glrduves6o, (((uint8_t *)&F.l10) + 16));
            gm_l67 = t968;
            {
                uint64_t t970 = GM_LD(uint64_t, (((uint8_t *)&gm_l67) + 24));
                gm_l64 = t970;
            }
            {
                const uint8_t *t971 = (((uint8_t *)&gm_l67) + 0);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tab260m0n3sqj *)((uint8_t *)&gm_l65)) + 25, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_tab260m0n3sqj *)((uint8_t *)&gm_l65)) + 26, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tab260m0n3sqj *)((uint8_t *)&gm_l65)) + 28, 0);
                ((uint8_t *)&(*(gm_tab260m0n3sqj *)((uint8_t *)&gm_l65)))[24] = 1;
                memmove((uint8_t *)&(*(gm_tab260m0n3sqj *)((uint8_t *)&gm_l65)) + 0, t971, 24);
            }
            goto me953;
mn966:;
ma956:;
            void * t973 = gm_l0;
            gm_tl874t2atnlh3 t974 = gm_l5;
            gm_b16_8 t975;
            GM_ST(void *, (uint8_t *)&t975, (void *)(gm_sv7lsha1e0i8r));
            GM_ST(uintptr_t, (uint8_t *)&t975 + 8, (uintptr_t)(12));
            gm_t39uai1ajk3qv t976;
            gm_f_unsupported_2seq49l((void *)&t976, t973, GM_CONV(gm_cseahvg8v7p0vs, t974), t975);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 8, 0);
            ((uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out))[0] = 1;
            GM_ST(gm_t39uai1ajk3qv, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 16, t976);
            goto gm_fdone;
            goto me953;
mn972:;
ma957:;
            gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 519, 13);
me953:;
        }
        {
            __int128 t977;
            uint8_t t983 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
            switch (t983) { case ((uint8_t)0ULL): goto ma981; case ((uint8_t)1ULL): goto ma981; case ((uint8_t)2ULL): goto ma981; case ((uint8_t)3ULL): goto ma981; case ((uint8_t)4ULL): goto ma981; case ((uint8_t)5ULL): goto ma981; case ((uint8_t)6ULL): goto ma981; case ((uint8_t)7ULL): goto ma981; case ((uint8_t)8ULL): goto ma981; case ((uint8_t)9ULL): goto ma981; case ((uint8_t)10ULL): goto ma981; case ((uint8_t)11ULL): goto ma981; case ((uint8_t)12ULL): goto ma981; case ((uint8_t)13ULL): goto ma981; case ((uint8_t)14ULL): goto ma981; case ((uint8_t)15ULL): goto ma981; case ((uint8_t)16ULL): goto ma981; case ((uint8_t)17ULL): goto ma981; case ((uint8_t)18ULL): goto ma981; case ((uint8_t)19ULL): goto ma981; case ((uint8_t)20ULL): goto ma981; case ((uint8_t)21ULL): goto ma981; case ((uint8_t)22ULL): goto ma981; case ((uint8_t)23ULL): goto ma981; case ((uint8_t)24ULL): goto ma981; case ((uint8_t)25ULL): goto ma981; case ((uint8_t)26ULL): goto ma981; case ((uint8_t)27ULL): goto ma981; case ((uint8_t)28ULL): goto ma981; case ((uint8_t)29ULL): goto ma981; case ((uint8_t)30ULL): goto ma981; case ((uint8_t)31ULL): goto ma981; case ((uint8_t)32ULL): goto ma981; case ((uint8_t)33ULL): goto ma981; case ((uint8_t)34ULL): goto ma981; case ((uint8_t)35ULL): goto ma981; case ((uint8_t)36ULL): goto ma981; case ((uint8_t)37ULL): goto ma981; case ((uint8_t)38ULL): goto ma979; case ((uint8_t)39ULL): goto ma980; case ((uint8_t)40ULL): goto ma981; case ((uint8_t)41ULL): goto ma981; case ((uint8_t)42ULL): goto ma981; case ((uint8_t)43ULL): goto ma981; case ((uint8_t)44ULL): goto ma981; case ((uint8_t)45ULL): goto ma981; case ((uint8_t)46ULL): goto ma981; case ((uint8_t)47ULL): goto ma981; case ((uint8_t)48ULL): goto ma981; case ((uint8_t)49ULL): goto ma981; case ((uint8_t)50ULL): goto ma981; case ((uint8_t)51ULL): goto ma981; case ((uint8_t)52ULL): goto ma981; case ((uint8_t)53ULL): goto ma981; case ((uint8_t)54ULL): goto ma981; case ((uint8_t)55ULL): goto ma981; case ((uint8_t)56ULL): goto ma981; case ((uint8_t)57ULL): goto ma981; case ((uint8_t)58ULL): goto ma981; case ((uint8_t)59ULL): goto ma981; case ((uint8_t)60ULL): goto ma981; case ((uint8_t)61ULL): goto ma981; case ((uint8_t)62ULL): goto ma981; case ((uint8_t)63ULL): goto ma981; case ((uint8_t)64ULL): goto ma981; case ((uint8_t)65ULL): goto ma981; case ((uint8_t)66ULL): goto ma981; case ((uint8_t)67ULL): goto ma981; case ((uint8_t)68ULL): goto ma981; case ((uint8_t)69ULL): goto ma981; case ((uint8_t)70ULL): goto ma981; case ((uint8_t)71ULL): goto ma981; case ((uint8_t)72ULL): goto ma981; case ((uint8_t)73ULL): goto ma981; case ((uint8_t)74ULL): goto ma981; case ((uint8_t)75ULL): goto ma981; case ((uint8_t)76ULL): goto ma981; case ((uint8_t)77ULL): goto ma981; case ((uint8_t)78ULL): goto ma981; case ((uint8_t)79ULL): goto ma981; case ((uint8_t)80ULL): goto ma981; case ((uint8_t)81ULL): goto ma981; default: goto ma981; }
ma979:;
            uint8_t t985 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
            if (t985 != ((uint8_t)38ULL)) goto mn984;
            t977 = ((__int128)(((unsigned __int128)0u << 64) | 0u));
            goto me978;
mn984:;
ma980:;
            uint8_t t987 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
            if (t987 != ((uint8_t)39ULL)) goto mn986;
            uint64_t t988 = gm_l64;
            __int128 t989 = (__int128)t988;
            __int128 r990; bool o991 = __builtin_sub_overflow(t989, ((__int128)(((unsigned __int128)0u << 64) | 1u)), &r990);
            if (o991) gm_trap("integer overflow in `-`", gm_srcue4ionbe9lgh, 529, 26);
            __int128 t992 = r990;
            t977 = t992;
            goto me978;
mn986:;
ma981:;
            __int128 t995;
            uint64_t t997 = (uintptr_t)((uint64_t)1ULL);
            uint64_t t998 = GM_LD(uintptr_t, ((uint8_t *)&gm_l9) + 8);
            if (t997 >= (uintptr_t)(t998)) gm_trap_index(t997, t998, gm_srcue4ionbe9lgh, 531, 35);
            uint8_t t999 = GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l9)) + t997 * 48) + 0));
            if (t999 != ((uint8_t)0ULL)) goto ie996;
            unsigned __int128 t1000 = GM_LD(unsigned __int128, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l9)) + t997 * 48) + 16));
            gm_l68 = t1000;
            void * t1002 = gm_l0;
            unsigned __int128 t1003 = gm_l68;
            uint64_t t1004 = (uintptr_t)((uint64_t)1ULL);
            uint64_t t1005 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
            if (t1004 >= (uintptr_t)(t1005)) gm_trap_index(t1004, t1005, gm_srcue4ionbe9lgh, 532, 39);
            gm_t88rcucg1b7uk t1006 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t1004 * 88) + 0));
            __int128 t1007 = gm_f_int_value_6hmh8tj(t1002, t1003, GM_CONV(gm_csguankko4lmhd, t1006));
            __int128 t1008 = t1007;
            t995 = t1008;
            goto ix996;
ie996:;
            void * t1010 = gm_l0;
            gm_tl874t2atnlh3 t1011 = gm_l5;
            gm_b16_8 t1012;
            GM_ST(void *, (uint8_t *)&t1012, (void *)(gm_sv7lsha1e0i8r));
            GM_ST(uintptr_t, (uint8_t *)&t1012 + 8, (uintptr_t)(12));
            gm_t39uai1ajk3qv t1013;
            gm_f_unsupported_2seq49l((void *)&t1013, t1010, GM_CONV(gm_cseahvg8v7p0vs, t1011), t1012);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 8, 0);
            ((uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out))[0] = 1;
            GM_ST(gm_t39uai1ajk3qv, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 16, t1013);
            goto gm_fdone;
            t995 = ((__int128)0);
ix996:;
            __int128 t1014 = t995;
            t977 = t1014;
            goto me978;
mn993:;
ma982:;
            gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 527, 23);
me978:;
            gm_l69 = t977;
        }
        {
            uint8_t t1016 = 0;
            __int128 t1017 = gm_l69;
            uint8_t t1018 = (t1017 < ((__int128)(((unsigned __int128)0u << 64) | 0u)));
            if (!t1018) {
                __int128 t1019 = gm_l69;
                uint64_t t1020 = gm_l64;
                __int128 t1021 = (__int128)t1020;
                uint8_t t1022 = (t1019 >= t1021);
                t1016 = t1022;
            } else t1016 = 1;
            if (!t1016) goto ie1015;
            {
                gm_t6evimt84lndq t1024;
                memset(&t1024, 0, sizeof t1024);
                GM_ST(uint8_t, ((uint8_t *)&t1024 + 0), ((uint8_t)6ULL));
                memset(&(*(gm_ttk92ufj2f6eh *)gm_out), 0, sizeof (*(gm_ttk92ufj2f6eh *)gm_out));
                ((uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out))[0] = 0;
                GM_ST(gm_t6evimt84lndq, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 16, t1024);
                goto gm_fdone;
            }
            goto ix1015;
ie1015:;
ix1015:;
        }
        {
            __int128 t1025 = gm_l69;
            uint64_t t1026 = (uint64_t)t1025;
            gm_l70 = t1026;
        }
        {
            uint8_t t1028 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l1) + 0) == ((uint8_t)37ULL)));
            if (!t1028) goto ie1027;
            {
                if (!((((uint8_t *)&gm_l65))[24] != 0)) goto ie1030;
                gm_tkaapcii86udk t1031 = GM_LD(gm_tkaapcii86udk, ((uint8_t *)&gm_l65));
                gm_l71 = t1031;
                {
                    uint64_t t1033 = gm_l70;
                    gm_tkaapcii86udk t1034;
                    gm_f_offset_nkn711s((void *)&t1034, (void *)NULL, cx, NULL, &(*(gm_tkaapcii86udk *)((uint8_t *)&gm_l71)), t1033);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_t6evimt84lndq t1035;
                    GM_ST(uint8_t, (uint8_t *)&t1035 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t1035 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t1035 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t1035 + 8, 0);
                    GM_ST(uint64_t, (uint8_t *)&t1035 + 40, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t1035 + 0), ((uint8_t)4ULL));
                    GM_ST(gm_tkaapcii86udk, ((uint8_t *)&t1035 + 16), t1034);
                    gm_tq7s98ihonkju t1036 = gm_f_new_70le0lp((void *)NULL, cx, NULL, &t1035, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_k7898pnkupv)));
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    if (((((uint8_t *)&t1036))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t1036) + 8, gm_srcue4ionbe9lgh, 538, 47);
                    void * t1037 = GM_LD(void *, (((uint8_t *)&t1036) + 8));
                    gm_t6evimt84lndq t1038;
                    memset(&t1038, 0, sizeof t1038);
                    GM_ST(uint8_t, ((uint8_t *)&t1038 + 0), ((uint8_t)7ULL));
                    GM_ST(void *, ((uint8_t *)&t1038 + 16), t1037);
                    memset(&(*(gm_ttk92ufj2f6eh *)gm_out), 0, sizeof (*(gm_ttk92ufj2f6eh *)gm_out));
                    ((uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out))[0] = 0;
                    GM_ST(gm_t6evimt84lndq, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 16, t1038);
                    goto gm_fdone;
                }
                goto ix1030;
ie1030:;
ix1030:;
            }
            {
                void * t1039 = gm_l0;
                uint64_t t1040 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t1041 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                if (t1040 >= (uintptr_t)(t1041)) gm_trap_index(t1040, t1041, gm_srcue4ionbe9lgh, 539, 36);
                void * t1042 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t1040 * 88);
                gm_t5kq49rr2n9f8 t1043;
                gm_f_place_9889dgp((void *)&t1043, (void *)NULL, cx, NULL, t1039, t1042);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t1043))[0] != 0)) {
                    gm_ttk92ufj2f6eh t1044;
                    GM_ST(uint8_t, (uint8_t *)&t1044 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t1044 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t1044 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t1044 + 8, 0);
                    ((uint8_t *)&t1044)[0] = 1;
                    GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t1044 + 16), (*(gm_t39uai1ajk3qv *)(((uint8_t *)&t1043) + 16)));
                    F.ret = t1044;
                    goto gm_fret;
                }
                gm_l72 = (*(gm_tkaapcii86udk *)(((uint8_t *)&t1043) + 16));
            }
            {
                gm_tkaapcii86udk t1045;
                void * t1046 = GM_LD(void *, (((uint8_t *)&gm_l72) + 0));
                GM_ST(void *, (uint8_t *)&t1045 + 0, t1046);
                gm_b16_8 t1047 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l72) + 8));
                uint64_t t1048 = gm_l70;
                gm_b16_8 t1049 = gm_f_append_sajo2rq((void *)NULL, cx, NULL, t1047, t1048);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(gm_b16_8, (uint8_t *)&t1045 + 8, t1049);
                gm_t6evimt84lndq t1050;
                GM_ST(uint8_t, (uint8_t *)&t1050 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t1050 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t1050 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t1050 + 8, 0);
                GM_ST(uint64_t, (uint8_t *)&t1050 + 40, 0);
                GM_ST(uint8_t, ((uint8_t *)&t1050 + 0), ((uint8_t)4ULL));
                GM_ST(gm_tkaapcii86udk, ((uint8_t *)&t1050 + 16), t1045);
                gm_tq7s98ihonkju t1051 = gm_f_new_70le0lp((void *)NULL, cx, NULL, &t1050, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_knj30d2h126)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t1051))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t1051) + 8, gm_srcue4ionbe9lgh, 540, 30);
                void * t1052 = GM_LD(void *, (((uint8_t *)&t1051) + 8));
                gm_t6evimt84lndq t1053;
                memset(&t1053, 0, sizeof t1053);
                GM_ST(uint8_t, ((uint8_t *)&t1053 + 0), ((uint8_t)7ULL));
                GM_ST(void *, ((uint8_t *)&t1053 + 16), t1052);
                memset(&(*(gm_ttk92ufj2f6eh *)gm_out), 0, sizeof (*(gm_ttk92ufj2f6eh *)gm_out));
                ((uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out))[0] = 0;
                GM_ST(gm_t6evimt84lndq, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 16, t1053);
                goto gm_fdone;
            }
            goto ix1027;
ie1027:;
ix1027:;
        }
        {
            gm_t6evimt84lndq t1054;
            if (!((((uint8_t *)&gm_l65))[24] != 0)) goto ie1055;
            gm_tkaapcii86udk t1056 = GM_LD(gm_tkaapcii86udk, ((uint8_t *)&gm_l65));
            gm_l73 = t1056;
            void * t1058 = gm_l0;
            uint64_t t1059 = gm_l70;
            gm_tkaapcii86udk t1060;
            gm_f_offset_nkn711s((void *)&t1060, (void *)NULL, cx, NULL, &(*(gm_tkaapcii86udk *)((uint8_t *)&gm_l73)), t1059);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t6evimt84lndq t1061;
            gm_f_load_nf7o5kl((void *)&t1061, t1058, &t1060);
            gm_t6evimt84lndq t1062 = t1061;
            t1054 = t1062;
            goto ix1055;
ie1055:;
            gm_t6evimt84lndq t1064;
            uint8_t t1066 = GM_LD(uint8_t, (((uint8_t *)&F.l10) + 0));
            if (t1066 != ((uint8_t)3ULL)) goto ie1065;
            void * t1067 = GM_LD(void *, (((uint8_t *)&F.l10) + 16));
            gm_l74 = t1067;
            void * t1069 = gm_l74;
            gm_b16_8 t1070 = GM_LD(gm_b16_8, (((uint8_t *)t1069) + 0));
            gm_b16_8 t1071 = t1070;
            uint64_t t1072 = gm_l70;
            uint64_t t1073 = (uint64_t)t1072;
            uint64_t t1074 = (uintptr_t)t1073;
            uint64_t t1075 = GM_LD(uintptr_t, ((uint8_t *)&t1071) + 8);
            if (t1074 >= (uintptr_t)(t1075)) gm_trap_index(t1074, t1075, gm_srcue4ionbe9lgh, 544, 17);
            gm_t6evimt84lndq t1076 = GM_LD(gm_t6evimt84lndq, ((uint8_t *)GM_LD(void *, ((uint8_t *)&t1071)) + t1074 * 48));
            gm_t6evimt84lndq t1077 = t1076;
            t1064 = t1077;
            goto ix1065;
ie1065:;
            void * t1079 = gm_l0;
            gm_tl874t2atnlh3 t1080 = gm_l5;
            gm_b16_8 t1081;
            GM_ST(void *, (uint8_t *)&t1081, (void *)(gm_sv7lsha1e0i8r));
            GM_ST(uintptr_t, (uint8_t *)&t1081 + 8, (uintptr_t)(12));
            gm_t39uai1ajk3qv t1082;
            gm_f_unsupported_2seq49l((void *)&t1082, t1079, GM_CONV(gm_cseahvg8v7p0vs, t1080), t1081);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 8, 0);
            ((uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out))[0] = 1;
            GM_ST(gm_t39uai1ajk3qv, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 16, t1082);
            goto gm_fdone;
            t1064 = GM_ZERO(gm_t6evimt84lndq);
ix1065:;
            gm_t6evimt84lndq t1083 = t1064;
            t1054 = t1083;
ix1055:;
            F.l75 = t1054;
        }
        {
            gm_t6evimt84lndq t1084;
            gm_f_copy_nk4jid6((void *)&t1084, (void *)NULL, cx, NULL, &(*(gm_t6evimt84lndq *)((uint8_t *)&F.l75)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tq7s98ihonkju t1085 = gm_f_new_70le0lp((void *)NULL, cx, NULL, &t1084, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_k4nsaof9c90)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t1085))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t1085) + 8, gm_srcue4ionbe9lgh, 546, 26);
            void * t1086 = GM_LD(void *, (((uint8_t *)&t1085) + 8));
            gm_t6evimt84lndq t1087;
            memset(&t1087, 0, sizeof t1087);
            GM_ST(uint8_t, ((uint8_t *)&t1087 + 0), ((uint8_t)7ULL));
            GM_ST(void *, ((uint8_t *)&t1087 + 16), t1086);
            memset(&(*(gm_ttk92ufj2f6eh *)gm_out), 0, sizeof (*(gm_ttk92ufj2f6eh *)gm_out));
            ((uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out))[0] = 0;
            GM_ST(gm_t6evimt84lndq, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 16, t1087);
            goto gm_fdone;
        }
        goto me106;
mn941:;
ma122:;
        uint8_t t1089 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t1089 != ((uint8_t)35ULL)) goto mn1088;
        const uint8_t *t1090 = ((uint8_t *)&F.l10);
        memset(&(*(gm_ttk92ufj2f6eh *)gm_out), 0, sizeof (*(gm_ttk92ufj2f6eh *)gm_out));
        ((uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out))[0] = 0;
        memmove((uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 16, t1090, 48);
        goto gm_fdone;
        goto me106;
mn1088:;
ma123:;
        uint8_t t1092 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t1092 != ((uint8_t)78ULL)) goto mn1091;
        {
            uint8_t t1095 = GM_LD(uint8_t, (((uint8_t *)&F.l10) + 0));
            if (t1095 != ((uint8_t)5ULL)) goto ie1094;
            gm_to6glrduves6o t1096 = GM_LD(gm_to6glrduves6o, (((uint8_t *)&F.l10) + 16));
            gm_l76 = t1096;
            {
                uint64_t t1099 = (uintptr_t)((uint64_t)1ULL);
                uint64_t t1100 = GM_LD(uintptr_t, ((uint8_t *)&gm_l9) + 8);
                if (t1099 >= (uintptr_t)(t1100)) gm_trap_index(t1099, t1100, gm_srcue4ionbe9lgh, 551, 26);
                uint8_t t1101 = GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l9)) + t1099 * 48) + 0));
                if (t1101 != ((uint8_t)5ULL)) goto ie1098;
                gm_to6glrduves6o t1102 = GM_LD(gm_to6glrduves6o, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l9)) + t1099 * 48) + 16));
                gm_l77 = t1102;
                {
                    gm_f_list_0202sdh((void *)&F.l78, (void *)NULL, cx, NULL);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                {
                    {
                        gm_l80 = ((uint64_t)0ULL);
                    }
                    {
                        uint64_t t1105 = GM_LD(uint64_t, (((uint8_t *)&gm_l77) + 24));
                        gm_l79 = t1105;
                    }
                    {
                        for (;;) {
                            uint64_t t1107 = gm_l80;
                            uint64_t t1108 = gm_l79;
                            uint8_t t1109 = (t1107 < t1108);
                            if (!t1109) goto lb1106;
                            {
                                uint64_t t1112 = gm_l80;
                                uint64_t t1115 = GM_LD(uint64_t, ((uint8_t *)&t1112));
                                gm_l81 = t1115;
                                {
                                    void * t1117 = (void *)((uint8_t *)&F.l78);
                                    void * t1118 = gm_l0;
                                    uint64_t t1119 = gm_l81;
                                    gm_tkaapcii86udk t1120;
                                    gm_f_offset_nkn711s((void *)&t1120, (void *)NULL, cx, NULL, &(*(gm_tkaapcii86udk *)(((uint8_t *)&gm_l77) + 0)), t1119);
                                    if (gm_unw.kind) {
                                        goto gm_ll1110;
                                    }
                                    gm_t6evimt84lndq t1121;
                                    gm_f_load_nf7o5kl((void *)&t1121, t1118, &t1120);
                                    gm_t6evimt84lndq t1122;
                                    gm_f_copy_nk4jid6((void *)&t1122, (void *)NULL, cx, NULL, &t1121);
                                    if (gm_unw.kind) {
                                        goto gm_ll1110;
                                    }
                                    gm_tftb1odc739t5 t1123 = gm_f_push_0pgacsj(t1117, &t1122);
                                    if (((((uint8_t *)&t1123))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t1123) + 1, gm_srcue4ionbe9lgh, 554, 33);
                                    gm_b0_1 t1124 = GM_LD(gm_b0_1, (((uint8_t *)&t1123) + 1));
                                }
                                goto me1113;
mn1114:;
                                gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 554, 13);
me1113:;
                            }
lc1106:;
                            {
                                uint64_t t1126 = gm_l80;
                                uint64_t r1127; bool o1128 = __builtin_add_overflow(t1126, ((uint64_t)1ULL), &r1127);
                                uint64_t t1129 = r1127;
                                gm_l80 = t1129;
                            }
                        }
gm_ll1110:;
                        if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb1106; }
                            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc1106; }
                        }
                        goto gm_lad;
lb1106:;
                    }
                }
                {
                    {
                        gm_b16_8 t1131 = GM_LD(gm_b16_8, (((uint8_t *)&F.l78) + 0));
                        gm_b16_8 t1132 = GM_LD(gm_b16_8, ((uint8_t *)&t1131));
                        gm_l82 = t1132;
                    }
                    {
                        gm_l83 = ((uint64_t)0ULL);
                    }
                    {
                        for (;;) {
                            uint64_t t1134 = gm_l83;
                            uint64_t t1135 = GM_LD(uintptr_t, ((uint8_t *)&gm_l82) + 8);
                            uint8_t t1136 = (t1134 < t1135);
                            if (!t1136) goto lb1133;
                            {
                                gm_thf8as07kj3uj t1139;
                                memset((uint8_t *)&t1139 + 8, 0, 8);
                                uint64_t t1140 = gm_l83;
                                GM_ST(uint64_t, (uint8_t *)&t1139 + 0, t1140);
                                uint64_t t1141 = gm_l83;
                                uint64_t t1142 = (uintptr_t)t1141;
                                uint64_t t1143 = GM_LD(uintptr_t, ((uint8_t *)&gm_l82) + 8);
                                memcpy((uint8_t *)&t1139 + 16, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l82)) + t1142 * 48), 48);
                                uint64_t t1146 = GM_LD(uint64_t, (((uint8_t *)&t1139) + 0));
                                gm_l84 = t1146;
                                F.l85 = (*(gm_t6evimt84lndq *)(((uint8_t *)&t1139) + 16));
                                {
                                    void * t1148 = gm_l0;
                                    gm_tkaapcii86udk t1149 = GM_LD(gm_tkaapcii86udk, (((uint8_t *)&gm_l76) + 0));
                                    uint64_t t1150 = gm_l84;
                                    uint64_t t1151 = (uint64_t)t1150;
                                    gm_tkaapcii86udk t1152;
                                    gm_f_offset_nkn711s((void *)&t1152, (void *)NULL, cx, NULL, &t1149, t1151);
                                    if (gm_unw.kind) {
                                        goto gm_ll1137;
                                    }
                                    gm_tl874t2atnlh3 t1153 = gm_l5;
                                    gm_t9j082e6qvnjj t1154;
                                    gm_f_store_kejn5q1((void *)&t1154, (void *)NULL, cx, NULL, t1148, &t1152, &(*(gm_t6evimt84lndq *)((uint8_t *)&F.l85)), t1153);
                                    if (gm_unw.kind) {
                                        goto gm_ll1137;
                                    }
                                    if (((((uint8_t *)&t1154))[0] != 0)) {
                                        gm_ttk92ufj2f6eh t1155;
                                        GM_ST(uint8_t, (uint8_t *)&t1155 + 1, 0);
                                        GM_ST(uint16_t, (uint8_t *)&t1155 + 2, 0);
                                        GM_ST(uint32_t, (uint8_t *)&t1155 + 4, 0);
                                        GM_ST(uint64_t, (uint8_t *)&t1155 + 8, 0);
                                        ((uint8_t *)&t1155)[0] = 1;
                                        GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t1155 + 16), (*(gm_t39uai1ajk3qv *)(((uint8_t *)&t1154) + 16)));
                                        F.ret = t1155;
                                        goto gm_fret;
                                    }
                                    gm_b0_1 t1156 = GM_LD(gm_b0_1, (((uint8_t *)&t1154) + 16));
                                }
                                goto me1144;
mn1145:;
                                gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 555, 13);
me1144:;
                            }
lc1133:;
                            {
                                uint64_t t1158 = gm_l83;
                                uint64_t r1159; bool o1160 = __builtin_add_overflow(t1158, ((uint64_t)1ULL), &r1159);
                                uint64_t t1161 = r1159;
                                gm_l83 = t1161;
                            }
                        }
gm_ll1137:;
                        if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb1133; }
                            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc1133; }
                        }
                        goto gm_lad;
lb1133:;
                    }
                }
                {
                    gm_t6evimt84lndq t1162;
                    gm_f_unit_value_evae6mr((void *)&t1162, (void *)NULL, cx, NULL);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    memset(&(*(gm_ttk92ufj2f6eh *)gm_out), 0, sizeof (*(gm_ttk92ufj2f6eh *)gm_out));
                    ((uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out))[0] = 0;
                    GM_ST(gm_t6evimt84lndq, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 16, t1162);
                    goto gm_fdone;
                }
                goto ix1098;
ie1098:;
                {
                    void * t1164 = gm_l0;
                    gm_tl874t2atnlh3 t1165 = gm_l5;
                    gm_b16_8 t1166;
                    GM_ST(void *, (uint8_t *)&t1166, (void *)(gm_su1oqg46rlg2d));
                    GM_ST(uintptr_t, (uint8_t *)&t1166 + 8, (uintptr_t)(9));
                    gm_t39uai1ajk3qv t1167;
                    gm_f_unsupported_2seq49l((void *)&t1167, t1164, GM_CONV(gm_cseahvg8v7p0vs, t1165), t1166);
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 8, 0);
                    ((uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out))[0] = 1;
                    GM_ST(gm_t39uai1ajk3qv, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 16, t1167);
                    goto gm_fdone;
                }
ix1098:;
            }
            goto ix1094;
ie1094:;
            {
                void * t1169 = gm_l0;
                gm_tl874t2atnlh3 t1170 = gm_l5;
                gm_b16_8 t1171;
                GM_ST(void *, (uint8_t *)&t1171, (void *)(gm_su1oqg46rlg2d));
                GM_ST(uintptr_t, (uint8_t *)&t1171 + 8, (uintptr_t)(9));
                gm_t39uai1ajk3qv t1172;
                gm_f_unsupported_2seq49l((void *)&t1172, t1169, GM_CONV(gm_cseahvg8v7p0vs, t1170), t1171);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 8, 0);
                ((uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out))[0] = 1;
                GM_ST(gm_t39uai1ajk3qv, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 16, t1172);
                goto gm_fdone;
            }
ix1094:;
        }
        goto me106;
mn1091:;
ma124:;
        uint8_t t1174 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t1174 != ((uint8_t)8ULL)) goto mn1173;
        const uint8_t *t1175 = ((uint8_t *)&F.l10);
        memset(&(*(gm_ttk92ufj2f6eh *)gm_out), 0, sizeof (*(gm_ttk92ufj2f6eh *)gm_out));
        ((uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out))[0] = 0;
        memmove((uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 16, t1175, 48);
        goto gm_fdone;
        goto me106;
mn1173:;
ma125:;
        uint8_t t1177 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t1177 != ((uint8_t)9ULL)) goto mn1176;
        {
            gm_t6evimt84lndq t1179;
            uint8_t t1181 = GM_LD(uint8_t, (((uint8_t *)&F.l10) + 0));
            if (t1181 != ((uint8_t)7ULL)) goto ie1180;
            void * t1182 = GM_LD(void *, (((uint8_t *)&F.l10) + 16));
            gm_l86 = t1182;
            void * t1184 = gm_l86;
            gm_t6evimt84lndq t1185 = GM_LD(gm_t6evimt84lndq, ((uint8_t *)t1184));
            gm_t6evimt84lndq t1186 = t1185;
            t1179 = t1186;
            goto ix1180;
ie1180:;
            gm_t6evimt84lndq t1188 = F.l10;
            gm_t6evimt84lndq t1189 = t1188;
            t1179 = t1189;
ix1180:;
            F.l87 = t1179;
        }
        {
            uint8_t t1191 = GM_LD(uint8_t, (((uint8_t *)&F.l87) + 0));
            if (t1191 != ((uint8_t)4ULL)) goto ie1190;
            gm_tkaapcii86udk t1192 = GM_LD(gm_tkaapcii86udk, (((uint8_t *)&F.l87) + 16));
            gm_l88 = t1192;
            {
                gm_l89 = ((uint64_t)1ULL);
            }
            {
                void * t1194 = gm_l4;
                gm_t88rcucg1b7uk t1195 = gm_l11;
                gm_t53at80pfdus3 t1196;
                gm_f_kind_jcnt4hm((void *)&t1196, t1194, GM_CONV(gm_csguankko4lmhd, t1195));
                uint8_t t1201 = GM_LD(uint8_t, (((uint8_t *)&t1196) + 0));
                if (t1201 != ((uint8_t)8ULL)) goto pa1200;
                gm_t88rcucg1b7uk t1202 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t1196) + 8) + 4));
                gm_l90 = t1202;
                goto po1199;
pa1200:;
                uint8_t t1204 = GM_LD(uint8_t, (((uint8_t *)&t1196) + 0));
                if (t1204 != ((uint8_t)9ULL)) goto pa1203;
                gm_t88rcucg1b7uk t1205 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t1196) + 8) + 4));
                gm_l90 = t1205;
                goto po1199;
pa1203:;
                goto mn1198;
po1199:;
                {
                    void * t1207 = gm_l0;
                    void * t1208 = GM_LD(void *, (((uint8_t *)t1207) + 0));
                    gm_t88rcucg1b7uk t1209 = gm_l90;
                    gm_tbccvikg6b96e t1210;
                    gm_f_layout_a5ovnrm((void *)&t1210, (void *)NULL, cx, NULL, t1208, t1209);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint64_t t1211 = GM_LD(uint64_t, (((uint8_t *)&t1210) + 0));
                    uint64_t t1212 = (t1211 > ((uint64_t)1ULL) ? t1211 : ((uint64_t)1ULL));
                    gm_l89 = t1212;
                }
                goto me1197;
mn1198:;
                goto me1197;
mn1213:;
                gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 567, 13);
me1197:;
            }
            {
                uint64_t t1215;
                uint64_t t1217 = GM_LD(uintptr_t, (((uint8_t *)&gm_l88) + 8) + 8);
                uint8_t t1218 = (t1217 > ((uint64_t)0ULL));
                if (!t1218) goto ie1216;
                uint64_t t1220 = GM_LD(uintptr_t, (((uint8_t *)&gm_l88) + 8) + 8);
                uint64_t r1221; bool o1222 = __builtin_sub_overflow(t1220, ((uint64_t)1ULL), &r1221);
                if (o1222) gm_trap("integer overflow in `-`", gm_srcue4ionbe9lgh, 571, 55);
                uint64_t t1223 = r1221;
                uint64_t t1224 = (uintptr_t)t1223;
                uint64_t t1225 = GM_LD(uintptr_t, (((uint8_t *)&gm_l88) + 8) + 8);
                if (t1224 >= (uintptr_t)(t1225)) gm_trap_index(t1224, t1225, gm_srcue4ionbe9lgh, 571, 47);
                uint64_t t1226 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)&gm_l88) + 8)) + t1224 * 8));
                uint64_t t1227 = t1226;
                t1215 = t1227;
                goto ix1216;
ie1216:;
                uint64_t t1229 = ((uint64_t)0ULL);
                t1215 = t1229;
ix1216:;
                gm_l92 = t1215;
            }
            {
                void * t1230 = GM_LD(void *, (((uint8_t *)&gm_l88) + 0));
                void * t1231 = t1230;
                uint64_t t1232 = (uintptr_t)t1231;
                uint64_t t1233 = (uint64_t)t1232;
                unsigned __int128 n1234 = (unsigned __int128)((uint64_t)12ULL);
                if (n1234 >= 64) gm_trap_shift((uint64_t)n1234, "u64", gm_srcue4ionbe9lgh, 572, 21);
                uint64_t r1235 = (uint64_t)((uint64_t)t1233 << (int)n1234);
                bool o1236 = (uint64_t)(r1235 >> (int)n1234) != t1233;
                if (o1236) gm_trap("integer overflow in `<<`", gm_srcue4ionbe9lgh, 572, 21);
                uint64_t t1237 = r1235;
                gm_l93 = t1237;
            }
            {
                void * t1238 = gm_l0;
                uint64_t t1239 = gm_l93;
                uint64_t t1240 = gm_l92;
                uint64_t t1241 = gm_l89;
                uint64_t r1242; bool o1243 = __builtin_mul_overflow(t1240, t1241, &r1242);
                uint64_t t1244 = r1242;
                uint64_t r1245; bool o1246 = __builtin_add_overflow(t1239, t1244, &r1245);
                uint64_t t1247 = r1245;
                unsigned __int128 t1248 = (unsigned __int128)t1247;
                void * t1249 = gm_l3;
                gm_t88rcucg1b7uk t1250 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t1249) + 0));
                unsigned __int128 t1251 = gm_f_wrap_bt8r149(t1238, t1248, GM_CONV(gm_csguankko4lmhd, t1250));
                gm_t6evimt84lndq t1252;
                GM_ST(uint8_t, (uint8_t *)&t1252 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t1252 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t1252 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t1252 + 8, 0);
                GM_ST(uint64_t, (uint8_t *)&t1252 + 32, 0);
                GM_ST(uint64_t, (uint8_t *)&t1252 + 40, 0);
                GM_ST(uint8_t, ((uint8_t *)&t1252 + 0), ((uint8_t)0ULL));
                GM_ST(unsigned __int128, ((uint8_t *)&t1252 + 16), t1251);
                memset(&(*(gm_ttk92ufj2f6eh *)gm_out), 0, sizeof (*(gm_ttk92ufj2f6eh *)gm_out));
                ((uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out))[0] = 0;
                GM_ST(gm_t6evimt84lndq, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 16, t1252);
                goto gm_fdone;
            }
            goto ix1190;
ie1190:;
            {
                gm_t6evimt84lndq t1254;
                GM_ST(uint8_t, (uint8_t *)&t1254 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t1254 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t1254 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t1254 + 8, 0);
                GM_ST(uint64_t, (uint8_t *)&t1254 + 32, 0);
                GM_ST(uint64_t, (uint8_t *)&t1254 + 40, 0);
                GM_ST(uint8_t, ((uint8_t *)&t1254 + 0), ((uint8_t)0ULL));
                GM_ST(unsigned __int128, ((uint8_t *)&t1254 + 16), ((unsigned __int128)(((unsigned __int128)0u << 64) | 0u)));
                memset(&(*(gm_ttk92ufj2f6eh *)gm_out), 0, sizeof (*(gm_ttk92ufj2f6eh *)gm_out));
                ((uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out))[0] = 0;
                GM_ST(gm_t6evimt84lndq, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 16, t1254);
                goto gm_fdone;
            }
ix1190:;
        }
        goto me106;
mn1176:;
ma126:;
        uint8_t t1256 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t1256 != ((uint8_t)13ULL)) goto mn1255;
        {
            uint8_t t1259 = GM_LD(uint8_t, (((uint8_t *)&F.l10) + 0));
            if (t1259 != ((uint8_t)0ULL)) goto ie1258;
            unsigned __int128 t1260 = GM_LD(unsigned __int128, (((uint8_t *)&F.l10) + 16));
            gm_l94 = t1260;
            {
                unsigned __int128 t1263 = gm_l94;
                uint8_t t1264 = ((t1263 == ((unsigned __int128)(((unsigned __int128)0u << 64) | 0u))));
                if (!t1264) goto ie1262;
                {
                    gm_t6evimt84lndq t1266;
                    memset(&t1266, 0, sizeof t1266);
                    GM_ST(uint8_t, ((uint8_t *)&t1266 + 0), ((uint8_t)6ULL));
                    memset(&(*(gm_ttk92ufj2f6eh *)gm_out), 0, sizeof (*(gm_ttk92ufj2f6eh *)gm_out));
                    ((uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out))[0] = 0;
                    GM_ST(gm_t6evimt84lndq, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 16, t1266);
                    goto gm_fdone;
                }
                goto ix1262;
ie1262:;
ix1262:;
            }
            {
                gm_f_list_kl0t9m0((void *)&F.l95, (void *)NULL, cx, NULL);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t1267 = (void *)((uint8_t *)&F.l95);
                gm_tftb1odc739t5 t1268 = gm_f_push_7o6oubd(t1267, ((uint64_t)0ULL));
                if (((((uint8_t *)&t1268))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t1268) + 1, gm_srcue4ionbe9lgh, 581, 13);
                gm_b0_1 t1269 = GM_LD(gm_b0_1, (((uint8_t *)&t1268) + 1));
            }
            {
                gm_tkaapcii86udk t1270;
                gm_tjoi8o798ne9k t1271;
                memset((uint8_t *)&t1271 + 49, 0, 15);
                gm_f_unit_value_evae6mr((void *)&(*(gm_t6evimt84lndq *)((uint8_t *)&t1271 + 0)), (void *)NULL, cx, NULL);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(uint8_t, (uint8_t *)&t1271 + 48, ((uint8_t)1));
                gm_tq7s98ihonkju t1272 = gm_f_new_15vqrvd((void *)NULL, cx, NULL, &t1271, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kqu6q38jp8e)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t1272))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t1272) + 8, gm_srcue4ionbe9lgh, 582, 52);
                void * t1273 = GM_LD(void *, (((uint8_t *)&t1272) + 8));
                GM_ST(void *, (uint8_t *)&t1270 + 0, t1273);
                gm_b16_8 t1274 = GM_LD(gm_b16_8, (((uint8_t *)&F.l95) + 0));
                gm_b16_8 t1275 = GM_LD(gm_b16_8, ((uint8_t *)&t1274));
                GM_ST(gm_b16_8, (uint8_t *)&t1270 + 8, t1275);
                gm_t6evimt84lndq t1276;
                GM_ST(uint8_t, (uint8_t *)&t1276 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t1276 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t1276 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t1276 + 8, 0);
                GM_ST(uint64_t, (uint8_t *)&t1276 + 40, 0);
                GM_ST(uint8_t, ((uint8_t *)&t1276 + 0), ((uint8_t)4ULL));
                GM_ST(gm_tkaapcii86udk, ((uint8_t *)&t1276 + 16), t1270);
                gm_tq7s98ihonkju t1277 = gm_f_new_70le0lp((void *)NULL, cx, NULL, &t1276, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kjh6c1ct3mq)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t1277))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t1277) + 8, gm_srcue4ionbe9lgh, 582, 26);
                void * t1278 = GM_LD(void *, (((uint8_t *)&t1277) + 8));
                gm_t6evimt84lndq t1279;
                memset(&t1279, 0, sizeof t1279);
                GM_ST(uint8_t, ((uint8_t *)&t1279 + 0), ((uint8_t)7ULL));
                GM_ST(void *, ((uint8_t *)&t1279 + 16), t1278);
                memset(&(*(gm_ttk92ufj2f6eh *)gm_out), 0, sizeof (*(gm_ttk92ufj2f6eh *)gm_out));
                ((uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out))[0] = 0;
                GM_ST(gm_t6evimt84lndq, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 16, t1279);
                goto gm_fdone;
            }
            goto ix1258;
ie1258:;
            {
                void * t1281 = gm_l0;
                gm_tl874t2atnlh3 t1282 = gm_l5;
                gm_b16_8 t1283;
                GM_ST(void *, (uint8_t *)&t1283, (void *)(gm_sv7lsha1e0i8r));
                GM_ST(uintptr_t, (uint8_t *)&t1283 + 8, (uintptr_t)(12));
                gm_t39uai1ajk3qv t1284;
                gm_f_unsupported_2seq49l((void *)&t1284, t1281, GM_CONV(gm_cseahvg8v7p0vs, t1282), t1283);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 8, 0);
                ((uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out))[0] = 1;
                GM_ST(gm_t39uai1ajk3qv, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 16, t1284);
                goto gm_fdone;
            }
ix1258:;
        }
        goto me106;
mn1255:;
ma127:;
        uint8_t t1286 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t1286 != ((uint8_t)7ULL)) goto mn1285;
        void * t1287 = gm_l0;
        gm_t88rcucg1b7uk t1288 = gm_l11;
        void * t1289 = gm_l3;
        gm_t88rcucg1b7uk t1290 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t1289) + 0));
        gm_tl874t2atnlh3 t1291 = gm_l5;
        gm_f_reinterpret_jmr6jts((void *)&(*(gm_ttk92ufj2f6eh *)gm_out), (void *)NULL, cx, NULL, t1287, &(*(gm_t6evimt84lndq *)((uint8_t *)&F.l10)), t1288, t1290, t1291);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto gm_fdone;
        goto me106;
mn1285:;
ma128:;
        uint8_t t1293 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t1293 != ((uint8_t)26ULL)) goto mn1292;
        {
            uint8_t t1296 = GM_LD(uint8_t, (((uint8_t *)&F.l10) + 0));
            if (t1296 != ((uint8_t)1ULL)) goto ie1295;
            double t1297 = GM_LD(double, (((uint8_t *)&F.l10) + 16));
            gm_l96 = t1297;
            {
                void * t1299 = gm_l0;
                double t1300 = gm_l96;
                double t1301 = (double)(__builtin_sqrt(t1300));
                void * t1302 = gm_l3;
                gm_t88rcucg1b7uk t1303 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t1302) + 0));
                gm_t26jhpkpc0sda t1304;
                gm_f_round_float_saju9fu((void *)&t1304, (void *)NULL, cx, NULL, t1299, t1301, t1303);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t1304))[0] != 0)) {
                    gm_ttk92ufj2f6eh t1305;
                    GM_ST(uint8_t, (uint8_t *)&t1305 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t1305 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t1305 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t1305 + 8, 0);
                    ((uint8_t *)&t1305)[0] = 1;
                    GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t1305 + 16), (*(gm_t39uai1ajk3qv *)(((uint8_t *)&t1304) + 16)));
                    F.ret = t1305;
                    goto gm_fret;
                }
                double t1306 = GM_LD(double, (((uint8_t *)&t1304) + 16));
                gm_t6evimt84lndq t1307;
                memset(&t1307, 0, sizeof t1307);
                GM_ST(uint8_t, ((uint8_t *)&t1307 + 0), ((uint8_t)1ULL));
                GM_ST(double, ((uint8_t *)&t1307 + 16), t1306);
                memset(&(*(gm_ttk92ufj2f6eh *)gm_out), 0, sizeof (*(gm_ttk92ufj2f6eh *)gm_out));
                ((uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out))[0] = 0;
                GM_ST(gm_t6evimt84lndq, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 16, t1307);
                goto gm_fdone;
            }
            goto ix1295;
ie1295:;
            {
                void * t1309 = gm_l0;
                gm_tl874t2atnlh3 t1310 = gm_l5;
                gm_b16_8 t1311;
                GM_ST(void *, (uint8_t *)&t1311, (void *)(gm_sv7lsha1e0i8r));
                GM_ST(uintptr_t, (uint8_t *)&t1311 + 8, (uintptr_t)(12));
                gm_t39uai1ajk3qv t1312;
                gm_f_unsupported_2seq49l((void *)&t1312, t1309, GM_CONV(gm_cseahvg8v7p0vs, t1310), t1311);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 8, 0);
                ((uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out))[0] = 1;
                GM_ST(gm_t39uai1ajk3qv, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 16, t1312);
                goto gm_fdone;
            }
ix1295:;
        }
        goto me106;
mn1292:;
ma129:;
        uint8_t t1316 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t1316 != ((uint8_t)28ULL)) goto pa1315;
        goto po1314;
pa1315:;
        uint8_t t1318 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t1318 != ((uint8_t)29ULL)) goto pa1317;
        goto po1314;
pa1317:;
        uint8_t t1320 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t1320 != ((uint8_t)30ULL)) goto pa1319;
        goto po1314;
pa1319:;
        uint8_t t1322 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t1322 != ((uint8_t)31ULL)) goto pa1321;
        goto po1314;
pa1321:;
        uint8_t t1324 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t1324 != ((uint8_t)34ULL)) goto pa1323;
        goto po1314;
pa1323:;
        uint8_t t1326 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t1326 != ((uint8_t)32ULL)) goto pa1325;
        goto po1314;
pa1325:;
        uint8_t t1328 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t1328 != ((uint8_t)33ULL)) goto pa1327;
        goto po1314;
pa1327:;
        goto mn1313;
po1314:;
        {
            uint8_t t1331 = GM_LD(uint8_t, (((uint8_t *)&F.l10) + 0));
            if (t1331 != ((uint8_t)0ULL)) goto ie1330;
            unsigned __int128 t1332 = GM_LD(unsigned __int128, (((uint8_t *)&F.l10) + 16));
            gm_l97 = t1332;
            {
                void * t1334 = gm_l0;
                gm_t88rcucg1b7uk t1335 = gm_l11;
                gm_ttovhfee8mf28 t1336 = gm_f_int_info_2r9qb5n(t1334, GM_CONV(gm_csguankko4lmhd, t1335));
                gm_l98 = t1336;
            }
            {
                uint32_t t1337 = GM_LD(uint32_t, (((uint8_t *)&gm_l98) + 4));
                gm_l99 = t1337;
            }
            {
                unsigned __int128 t1338 = gm_l97;
                uint32_t t1339 = gm_l99;
                unsigned __int128 t1340 = gm_f_mask_gbhl4hp(t1339);
                unsigned __int128 t1341 = (unsigned __int128)(t1338 & t1340);
                F.l100 = t1341;
            }
            {
                gm_closure t1342 = ((gm_closure){(void *)gm_f_bit_s517clu, (void *)&F.h, cx});
                gm_l101 = t1342;
            }
            {
                gm_l102 = ((unsigned __int128)(((unsigned __int128)0u << 64) | 0u));
            }
            {
                uint8_t t1351 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                switch (t1351) { case ((uint8_t)0ULL): goto ma1349; case ((uint8_t)1ULL): goto ma1349; case ((uint8_t)2ULL): goto ma1349; case ((uint8_t)3ULL): goto ma1349; case ((uint8_t)4ULL): goto ma1349; case ((uint8_t)5ULL): goto ma1349; case ((uint8_t)6ULL): goto ma1349; case ((uint8_t)7ULL): goto ma1349; case ((uint8_t)8ULL): goto ma1349; case ((uint8_t)9ULL): goto ma1349; case ((uint8_t)10ULL): goto ma1349; case ((uint8_t)11ULL): goto ma1349; case ((uint8_t)12ULL): goto ma1349; case ((uint8_t)13ULL): goto ma1349; case ((uint8_t)14ULL): goto ma1349; case ((uint8_t)15ULL): goto ma1349; case ((uint8_t)16ULL): goto ma1349; case ((uint8_t)17ULL): goto ma1349; case ((uint8_t)18ULL): goto ma1349; case ((uint8_t)19ULL): goto ma1349; case ((uint8_t)20ULL): goto ma1349; case ((uint8_t)21ULL): goto ma1349; case ((uint8_t)22ULL): goto ma1349; case ((uint8_t)23ULL): goto ma1349; case ((uint8_t)24ULL): goto ma1349; case ((uint8_t)25ULL): goto ma1349; case ((uint8_t)26ULL): goto ma1349; case ((uint8_t)27ULL): goto ma1349; case ((uint8_t)28ULL): goto ma1344; case ((uint8_t)29ULL): goto ma1345; case ((uint8_t)30ULL): goto ma1346; case ((uint8_t)31ULL): goto ma1347; case ((uint8_t)32ULL): goto ma1349; case ((uint8_t)33ULL): goto ma1349; case ((uint8_t)34ULL): goto ma1348; case ((uint8_t)35ULL): goto ma1349; case ((uint8_t)36ULL): goto ma1349; case ((uint8_t)37ULL): goto ma1349; case ((uint8_t)38ULL): goto ma1349; case ((uint8_t)39ULL): goto ma1349; case ((uint8_t)40ULL): goto ma1349; case ((uint8_t)41ULL): goto ma1349; case ((uint8_t)42ULL): goto ma1349; case ((uint8_t)43ULL): goto ma1349; case ((uint8_t)44ULL): goto ma1349; case ((uint8_t)45ULL): goto ma1349; case ((uint8_t)46ULL): goto ma1349; case ((uint8_t)47ULL): goto ma1349; case ((uint8_t)48ULL): goto ma1349; case ((uint8_t)49ULL): goto ma1349; case ((uint8_t)50ULL): goto ma1349; case ((uint8_t)51ULL): goto ma1349; case ((uint8_t)52ULL): goto ma1349; case ((uint8_t)53ULL): goto ma1349; case ((uint8_t)54ULL): goto ma1349; case ((uint8_t)55ULL): goto ma1349; case ((uint8_t)56ULL): goto ma1349; case ((uint8_t)57ULL): goto ma1349; case ((uint8_t)58ULL): goto ma1349; case ((uint8_t)59ULL): goto ma1349; case ((uint8_t)60ULL): goto ma1349; case ((uint8_t)61ULL): goto ma1349; case ((uint8_t)62ULL): goto ma1349; case ((uint8_t)63ULL): goto ma1349; case ((uint8_t)64ULL): goto ma1349; case ((uint8_t)65ULL): goto ma1349; case ((uint8_t)66ULL): goto ma1349; case ((uint8_t)67ULL): goto ma1349; case ((uint8_t)68ULL): goto ma1349; case ((uint8_t)69ULL): goto ma1349; case ((uint8_t)70ULL): goto ma1349; case ((uint8_t)71ULL): goto ma1349; case ((uint8_t)72ULL): goto ma1349; case ((uint8_t)73ULL): goto ma1349; case ((uint8_t)74ULL): goto ma1349; case ((uint8_t)75ULL): goto ma1349; case ((uint8_t)76ULL): goto ma1349; case ((uint8_t)77ULL): goto ma1349; case ((uint8_t)78ULL): goto ma1349; case ((uint8_t)79ULL): goto ma1349; case ((uint8_t)80ULL): goto ma1349; case ((uint8_t)81ULL): goto ma1349; default: goto ma1349; }
ma1344:;
                uint8_t t1353 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                if (t1353 != ((uint8_t)28ULL)) goto mn1352;
                {
                    gm_l103 = ((uint32_t)0ULL);
                }
                {
                    for (;;) {
                        uint32_t t1356 = gm_l103;
                        uint32_t t1357 = gm_l99;
                        uint8_t t1358 = (t1356 < t1357);
                        if (!t1358) goto lb1355;
                        gm_closure t1359 = gm_l101;
                        uint32_t t1360 = gm_l99;
                        uint32_t r1361; bool o1362 = __builtin_sub_overflow(t1360, ((uint32_t)1ULL), &r1361);
                        if (o1362) gm_trap("integer overflow in `-`", gm_srcue4ionbe9lgh, 599, 45);
                        uint32_t t1363 = r1361;
                        uint32_t t1364 = gm_l103;
                        uint32_t r1365; bool o1366 = __builtin_sub_overflow(t1363, t1364, &r1365);
                        if (o1366) gm_trap("integer overflow in `-`", gm_srcue4ionbe9lgh, 599, 45);
                        uint32_t t1367 = r1365;
                        gm_closure t1368 = t1359;
                        gm_node *ccx1369 = t1368.cx;
                        void * t1370 = t1368.code;
                        uint8_t t1371;
                        if (GM_PTR_TAGGED(t1370)) {
                            t1371 = ((uint8_t (*)(uint32_t))GM_PTR_UNTAG(t1370))(t1367);
                        } else {
                            t1371 = ((uint8_t (*)(void *, gm_node *, gm_node *, uint32_t))t1370)(t1368.env, ccx1369, NULL, t1367);
                        }
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t1372 = (uint8_t)!t1371;
                        if (!t1372) goto lb1355;
                        {
                            uint32_t t1375 = gm_l103;
                            uint32_t r1376; bool o1377 = __builtin_add_overflow(t1375, ((uint32_t)1ULL), &r1376);
                            if (o1377) gm_trap("integer overflow in `+`", gm_srcue4ionbe9lgh, 599, 58);
                            uint32_t t1378 = r1376;
                            gm_l103 = t1378;
                        }
lc1355:;
                    }
lb1355:;
                }
                {
                    uint32_t t1379 = gm_l103;
                    unsigned __int128 t1380 = (unsigned __int128)t1379;
                    gm_t6evimt84lndq t1381;
                    GM_ST(uint8_t, (uint8_t *)&t1381 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t1381 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t1381 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t1381 + 8, 0);
                    GM_ST(uint64_t, (uint8_t *)&t1381 + 32, 0);
                    GM_ST(uint64_t, (uint8_t *)&t1381 + 40, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t1381 + 0), ((uint8_t)0ULL));
                    GM_ST(unsigned __int128, ((uint8_t *)&t1381 + 16), t1380);
                    memset(&(*(gm_ttk92ufj2f6eh *)gm_out), 0, sizeof (*(gm_ttk92ufj2f6eh *)gm_out));
                    ((uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out))[0] = 0;
                    GM_ST(gm_t6evimt84lndq, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 16, t1381);
                    goto gm_fdone;
                }
                goto me1343;
mn1352:;
ma1345:;
                uint8_t t1383 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                if (t1383 != ((uint8_t)29ULL)) goto mn1382;
                {
                    gm_l104 = ((uint32_t)0ULL);
                }
                {
                    for (;;) {
                        uint32_t t1386 = gm_l104;
                        uint32_t t1387 = gm_l99;
                        uint8_t t1388 = (t1386 < t1387);
                        if (!t1388) goto lb1385;
                        gm_closure t1389 = gm_l101;
                        uint32_t t1390 = gm_l104;
                        gm_closure t1391 = t1389;
                        gm_node *ccx1392 = t1391.cx;
                        void * t1393 = t1391.code;
                        uint8_t t1394;
                        if (GM_PTR_TAGGED(t1393)) {
                            t1394 = ((uint8_t (*)(uint32_t))GM_PTR_UNTAG(t1393))(t1390);
                        } else {
                            t1394 = ((uint8_t (*)(void *, gm_node *, gm_node *, uint32_t))t1393)(t1391.env, ccx1392, NULL, t1390);
                        }
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t1395 = (uint8_t)!t1394;
                        if (!t1395) goto lb1385;
                        {
                            uint32_t t1398 = gm_l104;
                            uint32_t r1399; bool o1400 = __builtin_add_overflow(t1398, ((uint32_t)1ULL), &r1399);
                            if (o1400) gm_trap("integer overflow in `+`", gm_srcue4ionbe9lgh, 604, 50);
                            uint32_t t1401 = r1399;
                            gm_l104 = t1401;
                        }
lc1385:;
                    }
lb1385:;
                }
                {
                    uint32_t t1402 = gm_l104;
                    unsigned __int128 t1403 = (unsigned __int128)t1402;
                    gm_t6evimt84lndq t1404;
                    GM_ST(uint8_t, (uint8_t *)&t1404 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t1404 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t1404 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t1404 + 8, 0);
                    GM_ST(uint64_t, (uint8_t *)&t1404 + 32, 0);
                    GM_ST(uint64_t, (uint8_t *)&t1404 + 40, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t1404 + 0), ((uint8_t)0ULL));
                    GM_ST(unsigned __int128, ((uint8_t *)&t1404 + 16), t1403);
                    memset(&(*(gm_ttk92ufj2f6eh *)gm_out), 0, sizeof (*(gm_ttk92ufj2f6eh *)gm_out));
                    ((uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out))[0] = 0;
                    GM_ST(gm_t6evimt84lndq, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 16, t1404);
                    goto gm_fdone;
                }
                goto me1343;
mn1382:;
ma1346:;
                uint8_t t1406 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                if (t1406 != ((uint8_t)30ULL)) goto mn1405;
                {
                    gm_l105 = ((uint32_t)0ULL);
                }
                {
                    {
                        gm_l107 = ((uint32_t)0ULL);
                    }
                    {
                        uint32_t t1409 = gm_l99;
                        gm_l106 = t1409;
                    }
                    {
                        for (;;) {
                            uint32_t t1411 = gm_l107;
                            uint32_t t1412 = gm_l106;
                            uint8_t t1413 = (t1411 < t1412);
                            if (!t1413) goto lb1410;
                            {
                                uint32_t t1416 = gm_l107;
                                uint32_t t1419 = GM_LD(uint32_t, ((uint8_t *)&t1416));
                                gm_l108 = t1419;
                                {
                                    gm_closure t1422 = gm_l101;
                                    uint32_t t1423 = gm_l108;
                                    gm_closure t1424 = t1422;
                                    gm_node *ccx1425 = t1424.cx;
                                    void * t1426 = t1424.code;
                                    uint8_t t1427;
                                    if (GM_PTR_TAGGED(t1426)) {
                                        t1427 = ((uint8_t (*)(uint32_t))GM_PTR_UNTAG(t1426))(t1423);
                                    } else {
                                        t1427 = ((uint8_t (*)(void *, gm_node *, gm_node *, uint32_t))t1426)(t1424.env, ccx1425, NULL, t1423);
                                    }
                                    if (gm_unw.kind) {
                                        goto gm_ll1414;
                                    }
                                    if (!t1427) goto ie1421;
                                    {
                                        uint32_t t1429 = gm_l105;
                                        uint32_t r1430; bool o1431 = __builtin_add_overflow(t1429, ((uint32_t)1ULL), &r1430);
                                        if (o1431) gm_trap("integer overflow in `+`", gm_srcue4ionbe9lgh, 610, 37);
                                        uint32_t t1432 = r1430;
                                        gm_l105 = t1432;
                                    }
                                    goto ix1421;
ie1421:;
ix1421:;
                                }
                                goto me1417;
mn1418:;
                                gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 609, 21);
me1417:;
                            }
lc1410:;
                            {
                                uint32_t t1434 = gm_l107;
                                uint32_t r1435; bool o1436 = __builtin_add_overflow(t1434, ((uint32_t)1ULL), &r1435);
                                uint32_t t1437 = r1435;
                                gm_l107 = t1437;
                            }
                        }
gm_ll1414:;
                        if (gm_unw.frame == &F.h && gm_unw.label == 4) {
                            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb1410; }
                            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc1410; }
                        }
                        goto gm_lad;
lb1410:;
                    }
                }
                {
                    uint32_t t1438 = gm_l105;
                    unsigned __int128 t1439 = (unsigned __int128)t1438;
                    gm_t6evimt84lndq t1440;
                    GM_ST(uint8_t, (uint8_t *)&t1440 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t1440 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t1440 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t1440 + 8, 0);
                    GM_ST(uint64_t, (uint8_t *)&t1440 + 32, 0);
                    GM_ST(uint64_t, (uint8_t *)&t1440 + 40, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t1440 + 0), ((uint8_t)0ULL));
                    GM_ST(unsigned __int128, ((uint8_t *)&t1440 + 16), t1439);
                    memset(&(*(gm_ttk92ufj2f6eh *)gm_out), 0, sizeof (*(gm_ttk92ufj2f6eh *)gm_out));
                    ((uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out))[0] = 0;
                    GM_ST(gm_t6evimt84lndq, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 16, t1440);
                    goto gm_fdone;
                }
                goto me1343;
mn1405:;
ma1347:;
                uint8_t t1442 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                if (t1442 != ((uint8_t)31ULL)) goto mn1441;
                {
                    uint32_t t1445 = gm_l99;
                    if (((uint32_t)8ULL) == 0) gm_trap("division by zero", gm_srcue4ionbe9lgh, 615, 24);
                    uint32_t t1446 = (uint32_t)(t1445 % ((uint32_t)8ULL));
                    uint8_t t1447 = (!(t1446 == ((uint32_t)0ULL)));
                    if (!t1447) goto ie1444;
                    {
                        void * t1449 = gm_l0;
                        gm_tl874t2atnlh3 t1450 = gm_l5;
                        uint32_t t1451 = gm_l99;
                        gm_t88rcucg1b7uk t1452;
                        GM_ST(uint32_t, ((uint8_t *)&t1452 + 0), t1451);
                        gm_tabb710d9mq2e t1453;
                        gm_f_format_ven7kt5((void *)&t1453, (void *)NULL, cx, NULL, t1452);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        if (((((uint8_t *)&t1453))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t1453) + 8, gm_srcue4ionbe9lgh, 615, 62);
                        gm_b16_8 t1454 = GM_LD(gm_b16_8, (((uint8_t *)&t1453) + 8));
                        gm_b16_8 t1455 = GM_LD(gm_b16_8, ((uint8_t *)&t1454));
                        gm_t39uai1ajk3qv t1456;
                        gm_f_trap_jftbss6((void *)&t1456, (void *)NULL, cx, NULL, t1449, t1450, t1455);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 8, 0);
                        ((uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out))[0] = 1;
                        GM_ST(gm_t39uai1ajk3qv, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 16, t1456);
                        goto gm_fdone;
                    }
                    goto ix1444;
ie1444:;
ix1444:;
                }
                {
                    {
                        gm_l110 = ((uint32_t)0ULL);
                    }
                    {
                        uint32_t t1458 = gm_l99;
                        if (((uint32_t)8ULL) == 0) gm_trap("division by zero", gm_srcue4ionbe9lgh, 616, 33);
                        uint32_t t1459 = (uint32_t)(t1458 / ((uint32_t)8ULL));
                        gm_l109 = t1459;
                    }
                    {
                        for (;;) {
                            uint32_t t1461 = gm_l110;
                            uint32_t t1462 = gm_l109;
                            uint8_t t1463 = (t1461 < t1462);
                            if (!t1463) goto lb1460;
                            {
                                uint32_t t1466 = gm_l110;
                                uint32_t t1469 = GM_LD(uint32_t, ((uint8_t *)&t1466));
                                gm_l111 = t1469;
                                {
                                    unsigned __int128 t1471 = gm_l102;
                                    unsigned __int128 t1472 = F.l100;
                                    uint32_t t1473 = gm_l111;
                                    uint32_t r1474; bool o1475 = __builtin_mul_overflow(t1473, ((uint32_t)8ULL), &r1474);
                                    if (o1475) gm_trap("integer overflow in `*`", gm_srcue4ionbe9lgh, 616, 58);
                                    uint32_t t1476 = r1474;
                                    unsigned __int128 t1477 = (unsigned __int128)t1476;
                                    unsigned __int128 n1478 = (unsigned __int128)t1477;
                                    if (n1478 >= 128) gm_trap_shift((uint64_t)n1478, "u128", gm_srcue4ionbe9lgh, 616, 48);
                                    unsigned __int128 t1479 = (unsigned __int128)(t1472 >> (int)n1478);
                                    unsigned __int128 t1480 = (unsigned __int128)(t1479 & ((unsigned __int128)(((unsigned __int128)0u << 64) | 255u)));
                                    uint32_t t1481 = gm_l99;
                                    if (((uint32_t)8ULL) == 0) gm_trap("division by zero", gm_srcue4ionbe9lgh, 616, 83);
                                    uint32_t t1482 = (uint32_t)(t1481 / ((uint32_t)8ULL));
                                    uint32_t r1483; bool o1484 = __builtin_sub_overflow(t1482, ((uint32_t)1ULL), &r1483);
                                    if (o1484) gm_trap("integer overflow in `-`", gm_srcue4ionbe9lgh, 616, 83);
                                    uint32_t t1485 = r1483;
                                    uint32_t t1486 = gm_l111;
                                    uint32_t r1487; bool o1488 = __builtin_sub_overflow(t1485, t1486, &r1487);
                                    if (o1488) gm_trap("integer overflow in `-`", gm_srcue4ionbe9lgh, 616, 83);
                                    uint32_t t1489 = r1487;
                                    uint32_t r1490; bool o1491 = __builtin_mul_overflow(t1489, ((uint32_t)8ULL), &r1490);
                                    if (o1491) gm_trap("integer overflow in `*`", gm_srcue4ionbe9lgh, 616, 82);
                                    uint32_t t1492 = r1490;
                                    unsigned __int128 t1493 = (unsigned __int128)t1492;
                                    unsigned __int128 n1494 = (unsigned __int128)t1493;
                                    if (n1494 >= 128) gm_trap_shift((uint64_t)n1494, "u128", gm_srcue4ionbe9lgh, 616, 46);
                                    unsigned __int128 r1495 = (unsigned __int128)((unsigned __int128)t1480 << (int)n1494);
                                    bool o1496 = (unsigned __int128)(r1495 >> (int)n1494) != t1480;
                                    if (o1496) gm_trap("integer overflow in `<<`", gm_srcue4ionbe9lgh, 616, 46);
                                    unsigned __int128 t1497 = r1495;
                                    unsigned __int128 t1498 = (unsigned __int128)(t1471 | t1497);
                                    gm_l102 = t1498;
                                }
                                goto me1467;
mn1468:;
                                gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 616, 21);
me1467:;
                            }
lc1460:;
                            {
                                uint32_t t1500 = gm_l110;
                                uint32_t r1501; bool o1502 = __builtin_add_overflow(t1500, ((uint32_t)1ULL), &r1501);
                                uint32_t t1503 = r1501;
                                gm_l110 = t1503;
                            }
                        }
lb1460:;
                    }
                }
                goto me1343;
mn1441:;
ma1348:;
                uint8_t t1505 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                if (t1505 != ((uint8_t)34ULL)) goto mn1504;
                {
                    gm_l113 = ((uint32_t)0ULL);
                }
                {
                    uint32_t t1507 = gm_l99;
                    gm_l112 = t1507;
                }
                {
                    for (;;) {
                        uint32_t t1509 = gm_l113;
                        uint32_t t1510 = gm_l112;
                        uint8_t t1511 = (t1509 < t1510);
                        if (!t1511) goto lb1508;
                        {
                            uint32_t t1514 = gm_l113;
                            uint32_t t1517 = GM_LD(uint32_t, ((uint8_t *)&t1514));
                            gm_l114 = t1517;
                            {
                                gm_closure t1520 = gm_l101;
                                uint32_t t1521 = gm_l114;
                                gm_closure t1522 = t1520;
                                gm_node *ccx1523 = t1522.cx;
                                void * t1524 = t1522.code;
                                uint8_t t1525;
                                if (GM_PTR_TAGGED(t1524)) {
                                    t1525 = ((uint8_t (*)(uint32_t))GM_PTR_UNTAG(t1524))(t1521);
                                } else {
                                    t1525 = ((uint8_t (*)(void *, gm_node *, gm_node *, uint32_t))t1524)(t1522.env, ccx1523, NULL, t1521);
                                }
                                if (gm_unw.kind) {
                                    goto gm_ll1512;
                                }
                                if (!t1525) goto ie1519;
                                {
                                    unsigned __int128 t1527 = gm_l102;
                                    uint32_t t1528 = gm_l99;
                                    uint32_t r1529; bool o1530 = __builtin_sub_overflow(t1528, ((uint32_t)1ULL), &r1529);
                                    if (o1530) gm_trap("integer overflow in `-`", gm_srcue4ionbe9lgh, 619, 54);
                                    uint32_t t1531 = r1529;
                                    uint32_t t1532 = gm_l114;
                                    uint32_t r1533; bool o1534 = __builtin_sub_overflow(t1531, t1532, &r1533);
                                    if (o1534) gm_trap("integer overflow in `-`", gm_srcue4ionbe9lgh, 619, 54);
                                    uint32_t t1535 = r1533;
                                    unsigned __int128 t1536 = (unsigned __int128)t1535;
                                    unsigned __int128 n1537 = (unsigned __int128)t1536;
                                    if (n1537 >= 128) gm_trap_shift((uint64_t)n1537, "u128", gm_srcue4ionbe9lgh, 619, 38);
                                    unsigned __int128 r1538 = (unsigned __int128)((unsigned __int128)((unsigned __int128)(((unsigned __int128)0u << 64) | 1u)) << (int)n1537);
                                    bool o1539 = (unsigned __int128)(r1538 >> (int)n1537) != ((unsigned __int128)(((unsigned __int128)0u << 64) | 1u));
                                    if (o1539) gm_trap("integer overflow in `<<`", gm_srcue4ionbe9lgh, 619, 38);
                                    unsigned __int128 t1540 = r1538;
                                    unsigned __int128 t1541 = (unsigned __int128)(t1527 | t1540);
                                    gm_l102 = t1541;
                                }
                                goto ix1519;
ie1519:;
ix1519:;
                            }
                            goto me1515;
mn1516:;
                            gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 618, 32);
me1515:;
                        }
lc1508:;
                        {
                            uint32_t t1543 = gm_l113;
                            uint32_t r1544; bool o1545 = __builtin_add_overflow(t1543, ((uint32_t)1ULL), &r1544);
                            uint32_t t1546 = r1544;
                            gm_l113 = t1546;
                        }
                    }
gm_ll1512:;
                    if (gm_unw.frame == &F.h && gm_unw.label == 6) {
                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb1508; }
                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc1508; }
                    }
                    goto gm_lad;
lb1508:;
                }
                goto me1343;
mn1504:;
ma1349:;
                {
                    uint64_t t1550 = (uintptr_t)((uint64_t)1ULL);
                    uint64_t t1551 = GM_LD(uintptr_t, ((uint8_t *)&gm_l9) + 8);
                    if (t1550 >= (uintptr_t)(t1551)) gm_trap_index(t1550, t1551, gm_srcue4ionbe9lgh, 622, 42);
                    uint8_t t1552 = GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l9)) + t1550 * 48) + 0));
                    if (t1552 != ((uint8_t)0ULL)) goto ie1549;
                    unsigned __int128 t1553 = GM_LD(unsigned __int128, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l9)) + t1550 * 48) + 16));
                    gm_l115 = t1553;
                    {
                        void * t1555 = gm_l0;
                        unsigned __int128 t1556 = gm_l115;
                        uint64_t t1557 = (uintptr_t)((uint64_t)1ULL);
                        uint64_t t1558 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                        if (t1557 >= (uintptr_t)(t1558)) gm_trap_index(t1557, t1558, gm_srcue4ionbe9lgh, 623, 53);
                        gm_t88rcucg1b7uk t1559 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t1557 * 88) + 0));
                        gm_f_big_of_1cq0qao((void *)&F.l116, (void *)NULL, cx, NULL, t1555, t1556, t1559);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    {
                        gm_l117 = ((uint32_t)0ULL);
                    }
                    {
                        uint8_t t1561 = gm_f_is_negative_k87pp3n(&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l116)));
                        uint8_t t1562 = (uint8_t)!t1561;
                        if (!t1562) goto ie1560;
                        {
                            gm_tbci5ospk5nnn t1564 = F.l116;
                            uint32_t t1565 = gm_l99;
                            uint64_t t1566 = (uint64_t)t1565;
                            gm_tbci5ospk5nnn t1567;
                            gm_f_from_u64_ud13q5v((void *)&t1567, (void *)NULL, cx, NULL, t1566);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_tbci5ospk5nnn t1568;
                            gm_f_rem_ib4l1vl((void *)&t1568, (void *)NULL, cx, NULL, &t1564, &t1567);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_tf5srh4mp4b3i t1569 = gm_f_to_u64_m01t1da(&t1568);
                            uint64_t t1570;
                            if (((((uint8_t *)&t1569))[8] != 0)) {
                                uint64_t t1571 = GM_LD(uint64_t, (((uint8_t *)&t1569) + 0));
                                t1570 = t1571;
                            } else {
                                t1570 = ((uint64_t)0ULL);
                            }
                            uint32_t t1572 = (uint32_t)t1570;
                            gm_l117 = t1572;
                        }
                        goto ix1560;
ie1560:;
ix1560:;
                    }
                    {
                        uint8_t t1574 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l1) + 0) == ((uint8_t)33ULL)));
                        if (!t1574) goto ie1573;
                        {
                            uint32_t t1576 = gm_l99;
                            uint32_t t1577 = gm_l117;
                            uint32_t r1578; bool o1579 = __builtin_sub_overflow(t1576, t1577, &r1578);
                            if (o1579) gm_trap("integer overflow in `-`", gm_srcue4ionbe9lgh, 626, 43);
                            uint32_t t1580 = r1578;
                            uint32_t t1581 = gm_l99;
                            if (t1581 == 0) gm_trap("division by zero", gm_srcue4ionbe9lgh, 626, 42);
                            uint32_t t1582 = (uint32_t)(t1580 % t1581);
                            gm_l117 = t1582;
                        }
                        goto ix1573;
ie1573:;
ix1573:;
                    }
                    {
                        {
                            gm_l119 = ((uint32_t)0ULL);
                        }
                        {
                            uint32_t t1584 = gm_l99;
                            gm_l118 = t1584;
                        }
                        {
                            for (;;) {
                                uint32_t t1586 = gm_l119;
                                uint32_t t1587 = gm_l118;
                                uint8_t t1588 = (t1586 < t1587);
                                if (!t1588) goto lb1585;
                                {
                                    uint32_t t1591 = gm_l119;
                                    uint32_t t1594 = GM_LD(uint32_t, ((uint8_t *)&t1591));
                                    gm_l120 = t1594;
                                    {
                                        gm_closure t1597 = gm_l101;
                                        uint32_t t1598 = gm_l120;
                                        gm_closure t1599 = t1597;
                                        gm_node *ccx1600 = t1599.cx;
                                        void * t1601 = t1599.code;
                                        uint8_t t1602;
                                        if (GM_PTR_TAGGED(t1601)) {
                                            t1602 = ((uint8_t (*)(uint32_t))GM_PTR_UNTAG(t1601))(t1598);
                                        } else {
                                            t1602 = ((uint8_t (*)(void *, gm_node *, gm_node *, uint32_t))t1601)(t1599.env, ccx1600, NULL, t1598);
                                        }
                                        if (gm_unw.kind) {
                                            goto gm_ll1589;
                                        }
                                        if (!t1602) goto ie1596;
                                        {
                                            unsigned __int128 t1604 = gm_l102;
                                            uint32_t t1605 = gm_l120;
                                            uint32_t t1606 = gm_l117;
                                            uint32_t r1607; bool o1608 = __builtin_add_overflow(t1605, t1606, &r1607);
                                            if (o1608) gm_trap("integer overflow in `+`", gm_srcue4ionbe9lgh, 629, 59);
                                            uint32_t t1609 = r1607;
                                            uint32_t t1610 = gm_l99;
                                            if (t1610 == 0) gm_trap("division by zero", gm_srcue4ionbe9lgh, 629, 58);
                                            uint32_t t1611 = (uint32_t)(t1609 % t1610);
                                            unsigned __int128 t1612 = (unsigned __int128)t1611;
                                            unsigned __int128 n1613 = (unsigned __int128)t1612;
                                            if (n1613 >= 128) gm_trap_shift((uint64_t)n1613, "u128", gm_srcue4ionbe9lgh, 629, 42);
                                            unsigned __int128 r1614 = (unsigned __int128)((unsigned __int128)((unsigned __int128)(((unsigned __int128)0u << 64) | 1u)) << (int)n1613);
                                            bool o1615 = (unsigned __int128)(r1614 >> (int)n1613) != ((unsigned __int128)(((unsigned __int128)0u << 64) | 1u));
                                            if (o1615) gm_trap("integer overflow in `<<`", gm_srcue4ionbe9lgh, 629, 42);
                                            unsigned __int128 t1616 = r1614;
                                            unsigned __int128 t1617 = (unsigned __int128)(t1604 | t1616);
                                            gm_l102 = t1617;
                                        }
                                        goto ix1596;
ie1596:;
ix1596:;
                                    }
                                    goto me1592;
mn1593:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 628, 21);
me1592:;
                                }
lc1585:;
                                {
                                    uint32_t t1619 = gm_l119;
                                    uint32_t r1620; bool o1621 = __builtin_add_overflow(t1619, ((uint32_t)1ULL), &r1620);
                                    uint32_t t1622 = r1620;
                                    gm_l119 = t1622;
                                }
                            }
gm_ll1589:;
                            if (gm_unw.frame == &F.h && gm_unw.label == 7) {
                                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb1585; }
                                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc1585; }
                            }
                            goto gm_lad;
lb1585:;
                        }
                    }
                    goto ix1549;
ie1549:;
                    {
                        void * t1624 = gm_l0;
                        gm_tl874t2atnlh3 t1625 = gm_l5;
                        gm_b16_8 t1626;
                        GM_ST(void *, (uint8_t *)&t1626, (void *)(gm_sv7lsha1e0i8r));
                        GM_ST(uintptr_t, (uint8_t *)&t1626 + 8, (uintptr_t)(12));
                        gm_t39uai1ajk3qv t1627;
                        gm_f_unsupported_2seq49l((void *)&t1627, t1624, GM_CONV(gm_cseahvg8v7p0vs, t1625), t1626);
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 8, 0);
                        ((uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out))[0] = 1;
                        GM_ST(gm_t39uai1ajk3qv, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 16, t1627);
                        goto gm_fdone;
                    }
ix1549:;
                }
                goto me1343;
mn1547:;
ma1350:;
                gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 596, 13);
me1343:;
            }
            {
                void * t1628 = gm_l0;
                unsigned __int128 t1629 = gm_l102;
                gm_t88rcucg1b7uk t1630 = gm_l11;
                unsigned __int128 t1631 = gm_f_wrap_bt8r149(t1628, t1629, GM_CONV(gm_csguankko4lmhd, t1630));
                gm_t6evimt84lndq t1632;
                GM_ST(uint8_t, (uint8_t *)&t1632 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t1632 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t1632 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t1632 + 8, 0);
                GM_ST(uint64_t, (uint8_t *)&t1632 + 32, 0);
                GM_ST(uint64_t, (uint8_t *)&t1632 + 40, 0);
                GM_ST(uint8_t, ((uint8_t *)&t1632 + 0), ((uint8_t)0ULL));
                GM_ST(unsigned __int128, ((uint8_t *)&t1632 + 16), t1631);
                memset(&(*(gm_ttk92ufj2f6eh *)gm_out), 0, sizeof (*(gm_ttk92ufj2f6eh *)gm_out));
                ((uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out))[0] = 0;
                GM_ST(gm_t6evimt84lndq, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 16, t1632);
                goto gm_fdone;
            }
            goto ix1330;
ie1330:;
            {
                void * t1634 = gm_l0;
                gm_tl874t2atnlh3 t1635 = gm_l5;
                gm_b16_8 t1636;
                GM_ST(void *, (uint8_t *)&t1636, (void *)(gm_sv7lsha1e0i8r));
                GM_ST(uintptr_t, (uint8_t *)&t1636 + 8, (uintptr_t)(12));
                gm_t39uai1ajk3qv t1637;
                gm_f_unsupported_2seq49l((void *)&t1637, t1634, GM_CONV(gm_cseahvg8v7p0vs, t1635), t1636);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 8, 0);
                ((uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out))[0] = 1;
                GM_ST(gm_t39uai1ajk3qv, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 16, t1637);
                goto gm_fdone;
            }
ix1330:;
        }
        goto me106;
mn1313:;
ma130:;
        gm_tvdg94bk7aa22 t1639 = GM_LD(gm_tvdg94bk7aa22, ((uint8_t *)&gm_l1));
        gm_l121 = t1639;
        void * t1640 = gm_l0;
        gm_tl874t2atnlh3 t1641 = gm_l5;
        gm_tvdg94bk7aa22 t1642 = gm_l121;
        gm_th9nl3oso747e t1643;
        GM_ST(gm_tvdg94bk7aa22, ((uint8_t *)&t1643 + 0), t1642);
        gm_tabb710d9mq2e t1644;
        gm_f_format_o6d0gj1((void *)&t1644, (void *)NULL, cx, NULL, t1643);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t1644))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t1644) + 8, gm_srcue4ionbe9lgh, 635, 46);
        gm_b16_8 t1645 = GM_LD(gm_b16_8, (((uint8_t *)&t1644) + 8));
        gm_b16_8 t1646 = GM_LD(gm_b16_8, ((uint8_t *)&t1645));
        gm_t39uai1ajk3qv t1647;
        gm_f_unsupported_2seq49l((void *)&t1647, t1640, GM_CONV(gm_cseahvg8v7p0vs, t1641), t1646);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 8, 0);
        ((uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out))[0] = 1;
        GM_ST(gm_t39uai1ajk3qv, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 16, t1647);
        goto gm_fdone;
        goto me106;
mn1638:;
ma131:;
        gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 369, 5);
me106:;
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
extern __attribute__((aligned(4))) uint64_t gm_f_byte_at_m94bj3e(void * a0, uint64_t a1, uint64_t a2);
__attribute__((aligned(4))) void gm_f_coerce_fesnqv4(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tmn7n0n9raou5 a1, void * a2, void * a3) {
    struct gm_fr_coerce_fesnqv4 F;
    void * gm_l0; /* m */
    gm_tmn7n0n9raou5 gm_l1; /* kind */
    void * gm_l2; /* x */
    void * gm_l3; /* e */
    unsigned __int128 gm_l5; /* bits */
    uint32_t gm_l6; /* i */
    void * gm_l7; /* p */
    gm_tkaapcii86udk gm_l8; /* pl */
    gm_t88rcucg1b7uk gm_l9; /* pointee */
    uint64_t gm_l10; /* n */
    void * gm_l11; /* p */
    gm_t88rcucg1b7uk gm_l12; /* inner */
    gm_t88rcucg1b7uk gm_l13; /* from */
    GM_STACK_CHECK(&F, 2318u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        void * t2 = gm_l0;
        void * t3 = gm_l2;
        gm_ttk92ufj2f6eh t4;
        gm_f_eval_2vbtp1v((void *)&t4, (void *)NULL, cx, NULL, t2, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t4))[0] != 0)) {
            gm_ttk92ufj2f6eh t5;
            GM_ST(uint8_t, (uint8_t *)&t5 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t5 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t5 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t5 + 8, 0);
            ((uint8_t *)&t5)[0] = 1;
            GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t5 + 16), (*(gm_t39uai1ajk3qv *)(((uint8_t *)&t4) + 16)));
            F.ret = t5;
            goto gm_fret;
        }
        F.l4 = (*(gm_t6evimt84lndq *)(((uint8_t *)&t4) + 16));
    }
    {
        uint8_t t17 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        switch (t17) { case ((uint8_t)0ULL): goto ma7; case ((uint8_t)1ULL): goto ma8; case ((uint8_t)2ULL): goto ma9; case ((uint8_t)3ULL): goto ma10; case ((uint8_t)4ULL): goto ma11; case ((uint8_t)5ULL): goto ma14; case ((uint8_t)6ULL): goto ma15; case ((uint8_t)7ULL): goto ma12; case ((uint8_t)8ULL): goto ma15; case ((uint8_t)9ULL): goto ma15; case ((uint8_t)10ULL): goto ma15; case ((uint8_t)11ULL): goto ma13; case ((uint8_t)12ULL): goto ma14; default: goto ma15; }
ma7:;
        uint8_t t19 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t19 != ((uint8_t)0ULL)) goto mn18;
        {
            uint8_t t22 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 0));
            if (t22 != ((uint8_t)0ULL)) goto ie21;
            unsigned __int128 t23 = GM_LD(unsigned __int128, (((uint8_t *)&F.l4) + 16));
            gm_l5 = t23;
            {
                void * t25 = gm_l0;
                void * t26 = gm_l0;
                unsigned __int128 t27 = gm_l5;
                void * t28 = gm_l2;
                gm_t88rcucg1b7uk t29 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t28) + 0));
                __int128 t30 = gm_f_int_value_6hmh8tj(t26, t27, GM_CONV(gm_csguankko4lmhd, t29));
                unsigned __int128 t31;
                memset(&t31, 0, sizeof t31); memcpy(&t31, ((uint8_t *)&t30), sizeof t31 < 16 ? sizeof t31 : 16);
                void * t32 = gm_l3;
                gm_t88rcucg1b7uk t33 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t32) + 0));
                unsigned __int128 t34 = gm_f_wrap_bt8r149(t25, t31, GM_CONV(gm_csguankko4lmhd, t33));
                gm_t6evimt84lndq t35;
                GM_ST(uint8_t, (uint8_t *)&t35 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t35 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t35 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t35 + 8, 0);
                GM_ST(uint64_t, (uint8_t *)&t35 + 32, 0);
                GM_ST(uint64_t, (uint8_t *)&t35 + 40, 0);
                GM_ST(uint8_t, ((uint8_t *)&t35 + 0), ((uint8_t)0ULL));
                GM_ST(unsigned __int128, ((uint8_t *)&t35 + 16), t34);
                memset(&(*(gm_ttk92ufj2f6eh *)gm_out), 0, sizeof (*(gm_ttk92ufj2f6eh *)gm_out));
                ((uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out))[0] = 0;
                GM_ST(gm_t6evimt84lndq, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 16, t35);
                goto gm_fdone;
            }
            goto ix21;
ie21:;
            {
                const uint8_t *t37 = ((uint8_t *)&F.l4);
                memset(&(*(gm_ttk92ufj2f6eh *)gm_out), 0, sizeof (*(gm_ttk92ufj2f6eh *)gm_out));
                ((uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out))[0] = 0;
                memmove((uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 16, t37, 48);
                goto gm_fdone;
            }
ix21:;
        }
        goto me6;
mn18:;
ma8:;
        uint8_t t39 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t39 != ((uint8_t)1ULL)) goto mn38;
        const uint8_t *t40 = ((uint8_t *)&F.l4);
        memset(&(*(gm_ttk92ufj2f6eh *)gm_out), 0, sizeof (*(gm_ttk92ufj2f6eh *)gm_out));
        ((uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out))[0] = 0;
        memmove((uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 16, t40, 48);
        goto gm_fdone;
        goto me6;
mn38:;
ma9:;
        uint8_t t42 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t42 != ((uint8_t)2ULL)) goto mn41;
        gm_tq7s98ihonkju t43 = gm_f_new_70le0lp((void *)NULL, cx, NULL, &(*(gm_t6evimt84lndq *)((uint8_t *)&F.l4)), &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kjqc35oao72)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t43))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t43) + 8, gm_srcue4ionbe9lgh, 264, 39);
        void * t44 = GM_LD(void *, (((uint8_t *)&t43) + 8));
        gm_t6evimt84lndq t45;
        memset(&t45, 0, sizeof t45);
        GM_ST(uint8_t, ((uint8_t *)&t45 + 0), ((uint8_t)7ULL));
        GM_ST(void *, ((uint8_t *)&t45 + 16), t44);
        memset(&(*(gm_ttk92ufj2f6eh *)gm_out), 0, sizeof (*(gm_ttk92ufj2f6eh *)gm_out));
        ((uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out))[0] = 0;
        GM_ST(gm_t6evimt84lndq, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 16, t45);
        goto gm_fdone;
        goto me6;
mn41:;
ma10:;
        uint8_t t47 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t47 != ((uint8_t)3ULL)) goto mn46;
        gm_tq7s98ihonkju t48 = gm_f_new_70le0lp((void *)NULL, cx, NULL, &(*(gm_t6evimt84lndq *)((uint8_t *)&F.l4)), &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kct6b70ce3d)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t48))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t48) + 8, gm_srcue4ionbe9lgh, 265, 31);
        void * t49 = GM_LD(void *, (((uint8_t *)&t48) + 8));
        gm_t6evimt84lndq t50;
        memset(&t50, 0, sizeof t50);
        GM_ST(uint8_t, ((uint8_t *)&t50 + 0), ((uint8_t)8ULL));
        GM_ST(void *, ((uint8_t *)&t50 + 16), t49);
        memset(&(*(gm_ttk92ufj2f6eh *)gm_out), 0, sizeof (*(gm_ttk92ufj2f6eh *)gm_out));
        ((uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out))[0] = 0;
        GM_ST(gm_t6evimt84lndq, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 16, t50);
        goto gm_fdone;
        goto me6;
mn46:;
ma11:;
        uint8_t t52 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t52 != ((uint8_t)4ULL)) goto mn51;
        uint32_t t53 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 4));
        gm_l6 = t53;
        uint8_t t56 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 0));
        if (t56 != ((uint8_t)9ULL)) goto mn55;
        void * t57 = GM_LD(void *, (((uint8_t *)&F.l4) + 16));
        gm_l7 = t57;
        uint32_t t58 = gm_l6;
        void * t59 = gm_l7;
        gm_ttggq7ng4kp3p t60;
        GM_ST(uint32_t, (uint8_t *)&t60 + 4, 0);
        GM_ST(uint32_t, ((uint8_t *)&t60 + 0), t58);
        GM_ST(void *, ((uint8_t *)&t60 + 8), t59);
        gm_t6evimt84lndq t61;
        GM_ST(uint8_t, (uint8_t *)&t61 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t61 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t61 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t61 + 8, 0);
        GM_ST(uint64_t, (uint8_t *)&t61 + 32, 0);
        GM_ST(uint64_t, (uint8_t *)&t61 + 40, 0);
        GM_ST(uint8_t, ((uint8_t *)&t61 + 0), ((uint8_t)10ULL));
        GM_ST(gm_ttggq7ng4kp3p, ((uint8_t *)&t61 + 16), t60);
        gm_tq7s98ihonkju t62 = gm_f_new_70le0lp((void *)NULL, cx, NULL, &t61, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_ktv7kfnnmgp)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t62))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t62) + 8, gm_srcue4ionbe9lgh, 267, 36);
        void * t63 = GM_LD(void *, (((uint8_t *)&t62) + 8));
        gm_t6evimt84lndq t64;
        memset(&t64, 0, sizeof t64);
        GM_ST(uint8_t, ((uint8_t *)&t64 + 0), ((uint8_t)9ULL));
        GM_ST(void *, ((uint8_t *)&t64 + 16), t63);
        memset(&(*(gm_ttk92ufj2f6eh *)gm_out), 0, sizeof (*(gm_ttk92ufj2f6eh *)gm_out));
        ((uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out))[0] = 0;
        GM_ST(gm_t6evimt84lndq, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 16, t64);
        goto gm_fdone;
        goto me54;
mn55:;
        const uint8_t *t66 = ((uint8_t *)&F.l4);
        memset(&(*(gm_ttk92ufj2f6eh *)gm_out), 0, sizeof (*(gm_ttk92ufj2f6eh *)gm_out));
        ((uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out))[0] = 0;
        memmove((uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 16, t66, 48);
        goto gm_fdone;
        goto me54;
mn65:;
        gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 266, 24);
me54:;
        goto me6;
mn51:;
ma12:;
        uint8_t t68 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t68 != ((uint8_t)7ULL)) goto mn67;
        {
            uint8_t t71 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 0));
            if (t71 != ((uint8_t)4ULL)) goto ie70;
            gm_tkaapcii86udk t72 = GM_LD(gm_tkaapcii86udk, (((uint8_t *)&F.l4) + 16));
            gm_l8 = t72;
            {
                void * t75 = gm_l0;
                void * t76 = GM_LD(void *, (((uint8_t *)t75) + 0));
                void * t77 = (void *)(((uint8_t *)t76) + 8);
                void * t78 = gm_l2;
                gm_t88rcucg1b7uk t79 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t78) + 0));
                gm_t53at80pfdus3 t80;
                gm_f_kind_jcnt4hm((void *)&t80, t77, GM_CONV(gm_csguankko4lmhd, t79));
                uint8_t t81 = GM_LD(uint8_t, (((uint8_t *)&t80) + 0));
                if (t81 != ((uint8_t)8ULL)) goto ie74;
                gm_t88rcucg1b7uk t82 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t80) + 8) + 4));
                gm_l9 = t82;
                {
                    void * t85 = gm_l0;
                    void * t86 = GM_LD(void *, (((uint8_t *)t85) + 0));
                    void * t87 = (void *)(((uint8_t *)t86) + 8);
                    gm_t88rcucg1b7uk t88 = gm_l9;
                    gm_t53at80pfdus3 t89;
                    gm_f_kind_jcnt4hm((void *)&t89, t87, GM_CONV(gm_csguankko4lmhd, t88));
                    uint8_t t90 = GM_LD(uint8_t, (((uint8_t *)&t89) + 0));
                    if (t90 != ((uint8_t)11ULL)) goto ie84;
                    uint64_t t91 = GM_LD(uint64_t, ((((uint8_t *)&t89) + 8) + 0));
                    gm_l10 = t91;
                    {
                        gm_to6glrduves6o t93;
                        void * t94 = GM_LD(void *, (((uint8_t *)&gm_l8) + 0));
                        GM_ST(void *, (uint8_t *)&(*(gm_tkaapcii86udk *)((uint8_t *)&t93 + 0)) + 0, t94);
                        gm_b16_8 t95 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l8) + 8));
                        gm_b16_8 t96 = gm_f_append_sajo2rq((void *)NULL, cx, NULL, t95, ((uint64_t)0ULL));
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_tkaapcii86udk *)((uint8_t *)&t93 + 0)) + 8, t96);
                        uint64_t t97 = gm_l10;
                        GM_ST(uint64_t, (uint8_t *)&t93 + 24, t97);
                        gm_t6evimt84lndq t98;
                        GM_ST(uint8_t, (uint8_t *)&t98 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t98 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t98 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t98 + 8, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t98 + 0), ((uint8_t)5ULL));
                        GM_ST(gm_to6glrduves6o, ((uint8_t *)&t98 + 16), t93);
                        memset(&(*(gm_ttk92ufj2f6eh *)gm_out), 0, sizeof (*(gm_ttk92ufj2f6eh *)gm_out));
                        ((uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out))[0] = 0;
                        GM_ST(gm_t6evimt84lndq, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 16, t98);
                        goto gm_fdone;
                    }
                    goto ix84;
ie84:;
                    {
                        void * t100 = gm_l0;
                        void * t101 = gm_l3;
                        gm_tl874t2atnlh3 t102 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t101) + 72));
                        gm_b16_8 t103;
                        GM_ST(void *, (uint8_t *)&t103, (void *)(gm_sqh0568fl8oao));
                        GM_ST(uintptr_t, (uint8_t *)&t103 + 8, (uintptr_t)(13));
                        gm_t39uai1ajk3qv t104;
                        gm_f_unsupported_2seq49l((void *)&t104, t100, GM_CONV(gm_cseahvg8v7p0vs, t102), t103);
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 8, 0);
                        ((uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out))[0] = 1;
                        GM_ST(gm_t39uai1ajk3qv, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 16, t104);
                        goto gm_fdone;
                    }
ix84:;
                }
                goto ix74;
ie74:;
                {
                    void * t106 = gm_l0;
                    void * t107 = gm_l3;
                    gm_tl874t2atnlh3 t108 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t107) + 72));
                    gm_b16_8 t109;
                    GM_ST(void *, (uint8_t *)&t109, (void *)(gm_sqh0568fl8oao));
                    GM_ST(uintptr_t, (uint8_t *)&t109 + 8, (uintptr_t)(13));
                    gm_t39uai1ajk3qv t110;
                    gm_f_unsupported_2seq49l((void *)&t110, t106, GM_CONV(gm_cseahvg8v7p0vs, t108), t109);
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 8, 0);
                    ((uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out))[0] = 1;
                    GM_ST(gm_t39uai1ajk3qv, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 16, t110);
                    goto gm_fdone;
                }
ix74:;
            }
            goto ix70;
ie70:;
            {
                void * t112 = gm_l0;
                void * t113 = gm_l3;
                gm_tl874t2atnlh3 t114 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t113) + 72));
                gm_b16_8 t115;
                GM_ST(void *, (uint8_t *)&t115, (void *)(gm_sqh0568fl8oao));
                GM_ST(uintptr_t, (uint8_t *)&t115 + 8, (uintptr_t)(13));
                gm_t39uai1ajk3qv t116;
                gm_f_unsupported_2seq49l((void *)&t116, t112, GM_CONV(gm_cseahvg8v7p0vs, t114), t115);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 8, 0);
                ((uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out))[0] = 1;
                GM_ST(gm_t39uai1ajk3qv, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 16, t116);
                goto gm_fdone;
            }
ix70:;
        }
        goto me6;
mn67:;
ma13:;
        uint8_t t118 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t118 != ((uint8_t)11ULL)) goto mn117;
        uint8_t t121 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 0));
        if (t121 != ((uint8_t)7ULL)) goto mn120;
        void * t122 = GM_LD(void *, (((uint8_t *)&F.l4) + 16));
        gm_l11 = t122;
        {
            void * t125 = gm_l0;
            void * t126 = GM_LD(void *, (((uint8_t *)t125) + 0));
            void * t127 = (void *)(((uint8_t *)t126) + 8);
            void * t128 = gm_l3;
            gm_t88rcucg1b7uk t129 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t128) + 0));
            gm_t53at80pfdus3 t130;
            gm_f_kind_jcnt4hm((void *)&t130, t127, GM_CONV(gm_csguankko4lmhd, t129));
            uint8_t t131 = GM_LD(uint8_t, (((uint8_t *)&t130) + 0));
            if (t131 != ((uint8_t)13ULL)) goto ie124;
            gm_t88rcucg1b7uk t132 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t130) + 8));
            gm_l12 = t132;
            {
                void * t135 = gm_l0;
                void * t136 = GM_LD(void *, (((uint8_t *)t135) + 0));
                void * t137 = (void *)(((uint8_t *)t136) + 8);
                void * t138 = gm_l2;
                gm_t88rcucg1b7uk t139 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t138) + 0));
                gm_t53at80pfdus3 t140;
                gm_f_kind_jcnt4hm((void *)&t140, t137, GM_CONV(gm_csguankko4lmhd, t139));
                uint8_t t141 = GM_LD(uint8_t, (((uint8_t *)&t140) + 0));
                if (t141 != ((uint8_t)13ULL)) goto ie134;
                gm_t88rcucg1b7uk t142 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t140) + 8));
                gm_l13 = t142;
                {
                    void * t144 = gm_l0;
                    void * t145 = gm_l11;
                    gm_t88rcucg1b7uk t146 = gm_l13;
                    gm_t88rcucg1b7uk t147 = gm_l12;
                    gm_ttk92ufj2f6eh t148;
                    gm_f_convert_value_v1i8v3i((void *)&t148, (void *)NULL, cx, NULL, t144, &(*(gm_t6evimt84lndq *)((uint8_t *)t145)), t146, t147);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    if (((((uint8_t *)&t148))[0] != 0)) {
                        gm_ttk92ufj2f6eh t149;
                        GM_ST(uint8_t, (uint8_t *)&t149 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t149 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t149 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t149 + 8, 0);
                        ((uint8_t *)&t149)[0] = 1;
                        GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t149 + 16), (*(gm_t39uai1ajk3qv *)(((uint8_t *)&t148) + 16)));
                        F.ret = t149;
                        goto gm_fret;
                    }
                    gm_tq7s98ihonkju t150 = gm_f_new_70le0lp((void *)NULL, cx, NULL, &(*(gm_t6evimt84lndq *)(((uint8_t *)&t148) + 16)), &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_k1fl2q13fn0)));
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    if (((((uint8_t *)&t150))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t150) + 8, gm_srcue4ionbe9lgh, 280, 30);
                    void * t151 = GM_LD(void *, (((uint8_t *)&t150) + 8));
                    gm_t6evimt84lndq t152;
                    memset(&t152, 0, sizeof t152);
                    GM_ST(uint8_t, ((uint8_t *)&t152 + 0), ((uint8_t)7ULL));
                    GM_ST(void *, ((uint8_t *)&t152 + 16), t151);
                    memset(&(*(gm_ttk92ufj2f6eh *)gm_out), 0, sizeof (*(gm_ttk92ufj2f6eh *)gm_out));
                    ((uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out))[0] = 0;
                    GM_ST(gm_t6evimt84lndq, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 16, t152);
                    goto gm_fdone;
                }
                goto ix134;
ie134:;
                {
                    const uint8_t *t154 = ((uint8_t *)&F.l4);
                    memset(&(*(gm_ttk92ufj2f6eh *)gm_out), 0, sizeof (*(gm_ttk92ufj2f6eh *)gm_out));
                    ((uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out))[0] = 0;
                    memmove((uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 16, t154, 48);
                    goto gm_fdone;
                }
ix134:;
            }
            goto ix124;
ie124:;
            {
                const uint8_t *t156 = ((uint8_t *)&F.l4);
                memset(&(*(gm_ttk92ufj2f6eh *)gm_out), 0, sizeof (*(gm_ttk92ufj2f6eh *)gm_out));
                ((uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out))[0] = 0;
                memmove((uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 16, t156, 48);
                goto gm_fdone;
            }
ix124:;
        }
        goto me119;
mn120:;
        const uint8_t *t158 = ((uint8_t *)&F.l4);
        memset(&(*(gm_ttk92ufj2f6eh *)gm_out), 0, sizeof (*(gm_ttk92ufj2f6eh *)gm_out));
        ((uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out))[0] = 0;
        memmove((uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 16, t158, 48);
        goto gm_fdone;
        goto me119;
mn157:;
        gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 276, 24);
me119:;
        goto me6;
mn117:;
ma14:;
        uint8_t t162 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t162 != ((uint8_t)12ULL)) goto pa161;
        goto po160;
pa161:;
        uint8_t t164 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t164 != ((uint8_t)5ULL)) goto pa163;
        goto po160;
pa163:;
        goto mn159;
po160:;
        void * t165 = gm_l0;
        void * t166 = gm_l3;
        gm_tl874t2atnlh3 t167 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t166) + 72));
        gm_b16_8 t168;
        GM_ST(void *, (uint8_t *)&t168, (void *)(gm_sqh0568fl8oao));
        GM_ST(uintptr_t, (uint8_t *)&t168 + 8, (uintptr_t)(13));
        gm_t39uai1ajk3qv t169;
        gm_f_unsupported_2seq49l((void *)&t169, t165, GM_CONV(gm_cseahvg8v7p0vs, t167), t168);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 8, 0);
        ((uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out))[0] = 1;
        GM_ST(gm_t39uai1ajk3qv, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 16, t169);
        goto gm_fdone;
        goto me6;
mn159:;
ma15:;
        const uint8_t *t171 = ((uint8_t *)&F.l4);
        memset(&(*(gm_ttk92ufj2f6eh *)gm_out), 0, sizeof (*(gm_ttk92ufj2f6eh *)gm_out));
        ((uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out))[0] = 0;
        memmove((uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 16, t171, 48);
        goto gm_fdone;
        goto me6;
mn170:;
ma16:;
        gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 258, 5);
me6:;
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
inline __attribute__((aligned(4))) void gm_f_const_cell_p4adg2s(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t88rcucg1b7uk a2, gm_tl874t2atnlh3 a3) {
    struct gm_fr_const_cell_p4adg2s F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_t88rcucg1b7uk gm_l2; /* ty */
    gm_tl874t2atnlh3 gm_l3; /* use_span */
    uint32_t gm_l4; /* id */
    GM_STACK_CHECK(&F, 804u);
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
        gm_tl874t2atnlh3 t4 = gm_l1;
        gm_t88rcucg1b7uk t5 = gm_l2;
        gm_t9805qtonbsag t6 = gm_f_const_id_2mlh0og(t3, GM_CONV(gm_cseahvg8v7p0vs, t4), GM_CONV(gm_csguankko4lmhd, t5));
        uint32_t t7;
        if (((((uint8_t *)&t6))[4] != 0)) {
            uint32_t t8 = GM_LD(uint32_t, (((uint8_t *)&t6) + 0));
            t7 = t8;
        } else {
            void * t9 = gm_l0;
            gm_tl874t2atnlh3 t10 = gm_l3;
            gm_b16_8 t11;
            GM_ST(void *, (uint8_t *)&t11, (void *)(gm_sdrg69bmlm0tm));
            GM_ST(uintptr_t, (uint8_t *)&t11 + 8, (uintptr_t)(29));
            gm_t39uai1ajk3qv t12;
            gm_f_unsupported_2seq49l((void *)&t12, t9, GM_CONV(gm_cseahvg8v7p0vs, t10), t11);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t7g11k265ojef *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t7g11k265ojef *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t7g11k265ojef *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t7g11k265ojef *)gm_out) + 8, 0);
            ((uint8_t *)&(*(gm_t7g11k265ojef *)gm_out))[0] = 1;
            GM_ST(gm_t39uai1ajk3qv, (uint8_t *)&(*(gm_t7g11k265ojef *)gm_out) + 16, t12);
            goto gm_fdone;
        }
        gm_l4 = t7;
    }
    {
        uint32_t t13 = gm_l4;
        uint64_t t14 = (uint64_t)t13;
        uint64_t t15 = (uintptr_t)t14;
        void * t16 = gm_l0;
        void * t17 = GM_LD(void *, (((uint8_t *)t16) + 0));
        uint64_t t18 = GM_LD(uintptr_t, ((((uint8_t *)t17) + 1968) + 0) + 8);
        if (t15 >= (uintptr_t)(t18)) gm_trap_index(t15, t18, gm_srcue4ionbe9lgh, 783, 10);
        gm_tjcptv9sagfq8 t19 = GM_LD(gm_tjcptv9sagfq8, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t17) + 1968) + 0)) + t15 * 88));
        F.l5 = t19;
    }
    {
        uint8_t t26 = GM_LD(uint8_t, ((((uint8_t *)&F.l5) + 72) + 0));
        switch (t26) { case ((uint8_t)0ULL): goto ma23; case ((uint8_t)1ULL): goto ma22; case ((uint8_t)2ULL): goto ma21; case ((uint8_t)3ULL): goto ma24; case ((uint8_t)4ULL): goto ma24; default: goto ma24; }
ma21:;
        uint8_t t28 = GM_LD(uint8_t, ((((uint8_t *)&F.l5) + 72) + 0));
        if (t28 != ((uint8_t)2ULL)) goto mn27;
        void * t29 = GM_LD(void *, (((uint8_t *)&F.l5) + 80));
        if (!(GM_LD(void *, ((uint8_t *)&t29)) != 0)) gm_trap("unwrap of null", gm_srcue4ionbe9lgh, 785, 25);
        void * t30 = GM_LD(void *, ((uint8_t *)&t29));
        memset(&(*(gm_t7g11k265ojef *)gm_out), 0, sizeof (*(gm_t7g11k265ojef *)gm_out));
        ((uint8_t *)&(*(gm_t7g11k265ojef *)gm_out))[0] = 0;
        GM_ST(void *, (uint8_t *)&(*(gm_t7g11k265ojef *)gm_out) + 16, t30);
        goto gm_fdone;
        goto me20;
mn27:;
ma22:;
        uint8_t t32 = GM_LD(uint8_t, ((((uint8_t *)&F.l5) + 72) + 0));
        if (t32 != ((uint8_t)1ULL)) goto mn31;
        void * t33 = gm_l0;
        gm_tl874t2atnlh3 t34 = gm_l3;
        gm_b16_8 t35 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 0));
        gm_tnpifv20irnph t36;
        GM_ST(gm_b16_8, ((uint8_t *)&t36 + 0), t35);
        gm_tabb710d9mq2e t37;
        gm_f_format_q8f0n70((void *)&t37, (void *)NULL, cx, NULL, t36);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t37))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t37) + 8, gm_srcue4ionbe9lgh, 786, 49);
        gm_b16_8 t38 = GM_LD(gm_b16_8, (((uint8_t *)&t37) + 8));
        gm_b16_8 t39 = GM_LD(gm_b16_8, ((uint8_t *)&t38));
        gm_t39uai1ajk3qv t40;
        gm_f_trap_jftbss6((void *)&t40, (void *)NULL, cx, NULL, t33, t34, t39);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t7g11k265ojef *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t7g11k265ojef *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t7g11k265ojef *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t7g11k265ojef *)gm_out) + 8, 0);
        ((uint8_t *)&(*(gm_t7g11k265ojef *)gm_out))[0] = 1;
        GM_ST(gm_t39uai1ajk3qv, (uint8_t *)&(*(gm_t7g11k265ojef *)gm_out) + 16, t40);
        goto gm_fdone;
        goto me20;
mn31:;
ma23:;
        uint8_t t42 = GM_LD(uint8_t, ((((uint8_t *)&F.l5) + 72) + 0));
        if (t42 != ((uint8_t)0ULL)) goto mn41;
        void * t43 = gm_l0;
        gm_tl874t2atnlh3 t44 = gm_l3;
        gm_b16_8 t45;
        GM_ST(void *, (uint8_t *)&t45, (void *)(gm_sdrg69bmlm0tm));
        GM_ST(uintptr_t, (uint8_t *)&t45 + 8, (uintptr_t)(29));
        gm_t39uai1ajk3qv t46;
        gm_f_unsupported_2seq49l((void *)&t46, t43, GM_CONV(gm_cseahvg8v7p0vs, t44), t45);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t7g11k265ojef *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t7g11k265ojef *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t7g11k265ojef *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t7g11k265ojef *)gm_out) + 8, 0);
        ((uint8_t *)&(*(gm_t7g11k265ojef *)gm_out))[0] = 1;
        GM_ST(gm_t39uai1ajk3qv, (uint8_t *)&(*(gm_t7g11k265ojef *)gm_out) + 16, t46);
        goto gm_fdone;
        goto me20;
mn41:;
ma24:;
        gm_t39uai1ajk3qv t48;
        memset(&t48, 0, sizeof t48);
        GM_ST(uint8_t, ((uint8_t *)&t48 + 0), ((uint8_t)6ULL));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t7g11k265ojef *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t7g11k265ojef *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t7g11k265ojef *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t7g11k265ojef *)gm_out) + 8, 0);
        ((uint8_t *)&(*(gm_t7g11k265ojef *)gm_out))[0] = 1;
        GM_ST(gm_t39uai1ajk3qv, (uint8_t *)&(*(gm_t7g11k265ojef *)gm_out) + 16, t48);
        goto gm_fdone;
        goto me20;
mn47:;
ma25:;
        gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 784, 5);
me20:;
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
inline __attribute__((aligned(4))) void gm_f_convert_value_v1i8v3i(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_t88rcucg1b7uk a2, gm_t88rcucg1b7uk a3) {
    struct gm_fr_convert_value_v1i8v3i F;
    void * gm_l0; /* m */
    gm_t88rcucg1b7uk gm_l2; /* from */
    gm_t88rcucg1b7uk gm_l3; /* to */
    unsigned __int128 gm_l4; /* bits */
    gm_tkaapcii86udk gm_l5; /* pl */
    gm_t88rcucg1b7uk gm_l6; /* pointee */
    uint64_t gm_l7; /* n */
    GM_STACK_CHECK(&F, 683u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&F.l1, a1, sizeof F.l1);
    gm_l2 = a2;
    gm_l3 = a3;
    {
        gm_t88rcucg1b7uk t3 = gm_l2;
        gm_t88rcucg1b7uk t4 = gm_l3;
        uint8_t t5 = (gm_eq9m6mvtmgde(((uint8_t *)&t3), ((uint8_t *)&t4)));
        if (!t5) goto ie2;
        {
            const uint8_t *t7 = ((uint8_t *)&F.l1);
            memset(&(*(gm_ttk92ufj2f6eh *)gm_out), 0, sizeof (*(gm_ttk92ufj2f6eh *)gm_out));
            ((uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out))[0] = 0;
            memmove((uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 16, t7, 48);
            goto gm_fdone;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        uint8_t t13 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
        switch (t13) { case ((uint8_t)0ULL): goto ma9; case ((uint8_t)1ULL): goto ma11; case ((uint8_t)2ULL): goto ma11; case ((uint8_t)3ULL): goto ma11; case ((uint8_t)4ULL): goto ma10; case ((uint8_t)5ULL): goto ma11; case ((uint8_t)6ULL): goto ma11; case ((uint8_t)7ULL): goto ma11; case ((uint8_t)8ULL): goto ma11; case ((uint8_t)9ULL): goto ma11; case ((uint8_t)10ULL): goto ma11; case ((uint8_t)11ULL): goto ma11; case ((uint8_t)12ULL): goto ma11; case ((uint8_t)13ULL): goto ma11; default: goto ma11; }
ma9:;
        uint8_t t15 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
        if (t15 != ((uint8_t)0ULL)) goto mn14;
        unsigned __int128 t16 = GM_LD(unsigned __int128, (((uint8_t *)&F.l1) + 16));
        gm_l4 = t16;
        void * t17 = gm_l0;
        void * t18 = gm_l0;
        unsigned __int128 t19 = gm_l4;
        gm_t88rcucg1b7uk t20 = gm_l2;
        __int128 t21 = gm_f_int_value_6hmh8tj(t18, t19, GM_CONV(gm_csguankko4lmhd, t20));
        unsigned __int128 t22;
        memset(&t22, 0, sizeof t22); memcpy(&t22, ((uint8_t *)&t21), sizeof t22 < 16 ? sizeof t22 : 16);
        gm_t88rcucg1b7uk t23 = gm_l3;
        unsigned __int128 t24 = gm_f_wrap_bt8r149(t17, t22, GM_CONV(gm_csguankko4lmhd, t23));
        gm_t6evimt84lndq t25;
        GM_ST(uint8_t, (uint8_t *)&t25 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t25 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t25 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t25 + 8, 0);
        GM_ST(uint64_t, (uint8_t *)&t25 + 32, 0);
        GM_ST(uint64_t, (uint8_t *)&t25 + 40, 0);
        GM_ST(uint8_t, ((uint8_t *)&t25 + 0), ((uint8_t)0ULL));
        GM_ST(unsigned __int128, ((uint8_t *)&t25 + 16), t24);
        memset(&(*(gm_ttk92ufj2f6eh *)gm_out), 0, sizeof (*(gm_ttk92ufj2f6eh *)gm_out));
        ((uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out))[0] = 0;
        GM_ST(gm_t6evimt84lndq, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 16, t25);
        goto gm_fdone;
        goto me8;
mn14:;
ma10:;
        uint8_t t27 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
        if (t27 != ((uint8_t)4ULL)) goto mn26;
        gm_tkaapcii86udk t28 = GM_LD(gm_tkaapcii86udk, (((uint8_t *)&F.l1) + 16));
        gm_l5 = t28;
        {
            void * t31 = gm_l0;
            void * t32 = GM_LD(void *, (((uint8_t *)t31) + 0));
            void * t33 = (void *)(((uint8_t *)t32) + 8);
            gm_t88rcucg1b7uk t34 = gm_l2;
            gm_t53at80pfdus3 t35;
            gm_f_kind_jcnt4hm((void *)&t35, t33, GM_CONV(gm_csguankko4lmhd, t34));
            uint8_t t36 = GM_LD(uint8_t, (((uint8_t *)&t35) + 0));
            if (t36 != ((uint8_t)8ULL)) goto ie30;
            gm_t88rcucg1b7uk t37 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t35) + 8) + 4));
            gm_l6 = t37;
            {
                void * t40 = gm_l0;
                void * t41 = GM_LD(void *, (((uint8_t *)t40) + 0));
                void * t42 = (void *)(((uint8_t *)t41) + 8);
                gm_t88rcucg1b7uk t43 = gm_l3;
                gm_t53at80pfdus3 t44;
                gm_f_kind_jcnt4hm((void *)&t44, t42, GM_CONV(gm_csguankko4lmhd, t43));
                uint8_t t45 = GM_LD(uint8_t, (((uint8_t *)&t44) + 0));
                if (t45 != ((uint8_t)10ULL)) goto ie39;
                void * t46 = gm_l0;
                void * t47 = GM_LD(void *, (((uint8_t *)t46) + 0));
                void * t48 = (void *)(((uint8_t *)t47) + 8);
                gm_t88rcucg1b7uk t49 = gm_l6;
                gm_t53at80pfdus3 t50;
                gm_f_kind_jcnt4hm((void *)&t50, t48, GM_CONV(gm_csguankko4lmhd, t49));
                uint8_t t51 = GM_LD(uint8_t, (((uint8_t *)&t50) + 0));
                if (t51 != ((uint8_t)11ULL)) goto ie39;
                uint64_t t52 = GM_LD(uint64_t, ((((uint8_t *)&t50) + 8) + 0));
                gm_l7 = t52;
                {
                    gm_to6glrduves6o t54;
                    void * t55 = GM_LD(void *, (((uint8_t *)&gm_l5) + 0));
                    GM_ST(void *, (uint8_t *)&(*(gm_tkaapcii86udk *)((uint8_t *)&t54 + 0)) + 0, t55);
                    gm_b16_8 t56 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l5) + 8));
                    gm_b16_8 t57 = gm_f_append_sajo2rq((void *)NULL, cx, NULL, t56, ((uint64_t)0ULL));
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    GM_ST(gm_b16_8, (uint8_t *)&(*(gm_tkaapcii86udk *)((uint8_t *)&t54 + 0)) + 8, t57);
                    uint64_t t58 = gm_l7;
                    GM_ST(uint64_t, (uint8_t *)&t54 + 24, t58);
                    gm_t6evimt84lndq t59;
                    GM_ST(uint8_t, (uint8_t *)&t59 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t59 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t59 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t59 + 8, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t59 + 0), ((uint8_t)5ULL));
                    GM_ST(gm_to6glrduves6o, ((uint8_t *)&t59 + 16), t54);
                    memset(&(*(gm_ttk92ufj2f6eh *)gm_out), 0, sizeof (*(gm_ttk92ufj2f6eh *)gm_out));
                    ((uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out))[0] = 0;
                    GM_ST(gm_t6evimt84lndq, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 16, t59);
                    goto gm_fdone;
                }
                goto ix39;
ie39:;
ix39:;
            }
            {
                const uint8_t *t60 = ((uint8_t *)&F.l1);
                memset(&(*(gm_ttk92ufj2f6eh *)gm_out), 0, sizeof (*(gm_ttk92ufj2f6eh *)gm_out));
                ((uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out))[0] = 0;
                memmove((uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 16, t60, 48);
                goto gm_fdone;
            }
            goto ix30;
ie30:;
            {
                const uint8_t *t62 = ((uint8_t *)&F.l1);
                memset(&(*(gm_ttk92ufj2f6eh *)gm_out), 0, sizeof (*(gm_ttk92ufj2f6eh *)gm_out));
                ((uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out))[0] = 0;
                memmove((uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 16, t62, 48);
                goto gm_fdone;
            }
ix30:;
        }
        goto me8;
mn26:;
ma11:;
        const uint8_t *t64 = ((uint8_t *)&F.l1);
        memset(&(*(gm_ttk92ufj2f6eh *)gm_out), 0, sizeof (*(gm_ttk92ufj2f6eh *)gm_out));
        ((uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out))[0] = 0;
        memmove((uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 16, t64, 48);
        goto gm_fdone;
        goto me8;
mn63:;
ma12:;
        gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 292, 5);
me8:;
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
__attribute__((aligned(4))) void gm_f_display_covn4i2(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_t88rcucg1b7uk a2) {
    struct gm_fr_display_covn4i2 F;
    void * gm_l0; /* m */
    gm_t88rcucg1b7uk gm_l2; /* t */
    unsigned __int128 gm_l3; /* bits */
    uint8_t gm_l4; /* b */
    uint32_t gm_l5; /* index */
    uint32_t gm_l6; /* id */
    gm_b16_8 gm_l7; /* variants */
    GM_STACK_CHECK(&F, 566u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&F.l1, a1, sizeof F.l1);
    gm_l2 = a2;
    {
        uint8_t t8 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
        switch (t8) { case ((uint8_t)0ULL): goto ma3; case ((uint8_t)1ULL): goto ma6; case ((uint8_t)2ULL): goto ma4; case ((uint8_t)3ULL): goto ma6; case ((uint8_t)4ULL): goto ma6; case ((uint8_t)5ULL): goto ma6; case ((uint8_t)6ULL): goto ma6; case ((uint8_t)7ULL): goto ma6; case ((uint8_t)8ULL): goto ma6; case ((uint8_t)9ULL): goto ma6; case ((uint8_t)10ULL): goto ma5; case ((uint8_t)11ULL): goto ma6; case ((uint8_t)12ULL): goto ma6; case ((uint8_t)13ULL): goto ma6; default: goto ma6; }
ma3:;
        uint8_t t10 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
        if (t10 != ((uint8_t)0ULL)) goto mn9;
        unsigned __int128 t11 = GM_LD(unsigned __int128, (((uint8_t *)&F.l1) + 16));
        gm_l3 = t11;
        void * t13 = gm_l0;
        void * t14 = GM_LD(void *, (((uint8_t *)t13) + 0));
        void * t15 = (void *)(((uint8_t *)t14) + 8);
        gm_t88rcucg1b7uk t16 = gm_l2;
        uint8_t t17 = gm_f_is_integer_8d4f9ek(t15, GM_CONV(gm_csguankko4lmhd, t16));
        if (!t17) goto ie12;
        {
            void * t19 = gm_l0;
            unsigned __int128 t20 = gm_l3;
            gm_t88rcucg1b7uk t21 = gm_l2;
            gm_tbci5ospk5nnn t22;
            gm_f_big_of_1cq0qao((void *)&t22, (void *)NULL, cx, NULL, t19, t20, t21);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_b16_8 t23 = gm_f_to_decimal_8bshn5b((void *)NULL, cx, NULL, &t22);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 20, 0);
            ((uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out))[16] = 1;
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 0, t23);
            goto gm_fdone;
        }
        goto ix12;
ie12:;
ix12:;
        goto me2;
mn9:;
ma4:;
        uint8_t t25 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
        if (t25 != ((uint8_t)2ULL)) goto mn24;
        uint8_t t26 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 16));
        gm_l4 = t26;
        uint8_t t27 = gm_l4;
        gm_t9qkh0rsooc8d t28;
        GM_ST(uint8_t, ((uint8_t *)&t28 + 0), t27);
        gm_tabb710d9mq2e t29;
        gm_f_format_9u3jvus((void *)&t29, (void *)NULL, cx, NULL, t28);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t29))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t29) + 8, gm_srcue4ionbe9lgh, 764, 28);
        gm_b16_8 t30 = GM_LD(gm_b16_8, (((uint8_t *)&t29) + 8));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 20, 0);
        ((uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out))[16] = 1;
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 0, t30);
        goto gm_fdone;
        goto me2;
mn24:;
ma5:;
        uint8_t t32 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
        if (t32 != ((uint8_t)10ULL)) goto mn31;
        uint32_t t33 = GM_LD(uint32_t, ((((uint8_t *)&F.l1) + 16) + 0));
        gm_l5 = t33;
        if ((GM_LD(void *, ((((uint8_t *)&F.l1) + 16) + 8)) != 0)) goto mn31;
        {
            void * t36 = gm_l0;
            void * t37 = GM_LD(void *, (((uint8_t *)t36) + 0));
            void * t38 = (void *)(((uint8_t *)t37) + 8);
            gm_t88rcucg1b7uk t39 = gm_l2;
            gm_t53at80pfdus3 t40;
            gm_f_kind_jcnt4hm((void *)&t40, t38, GM_CONV(gm_csguankko4lmhd, t39));
            uint8_t t41 = GM_LD(uint8_t, (((uint8_t *)&t40) + 0));
            if (t41 != ((uint8_t)17ULL)) goto ie35;
            uint32_t t42 = GM_LD(uint32_t, ((((uint8_t *)&t40) + 8) + 0));
            gm_l6 = t42;
            {
                uint32_t t45 = gm_l6;
                uint64_t t46 = (uint64_t)t45;
                uint64_t t47 = (uintptr_t)t46;
                void * t48 = gm_l0;
                void * t49 = GM_LD(void *, (((uint8_t *)t48) + 0));
                uint64_t t50 = GM_LD(uintptr_t, ((((uint8_t *)t49) + 1088) + 0) + 8);
                if (t47 >= (uintptr_t)(t50)) gm_trap_index(t47, t50, gm_srcue4ionbe9lgh, 767, 36);
                uint8_t t51 = GM_LD(uint8_t, ((((uint8_t *)GM_LD(void *, ((((uint8_t *)t49) + 1088) + 0)) + t47 * 184) + 24) + 0));
                if (t51 != ((uint8_t)1ULL)) goto ie44;
                gm_b16_8 t52 = GM_LD(gm_b16_8, (((((uint8_t *)GM_LD(void *, ((((uint8_t *)t49) + 1088) + 0)) + t47 * 184) + 24) + 8) + 8));
                gm_l7 = t52;
                {
                    uint32_t t54 = gm_l5;
                    uint64_t t55 = (uint64_t)t54;
                    uint64_t t56 = (uintptr_t)t55;
                    uint64_t t57 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                    if (t56 >= (uintptr_t)(t57)) gm_trap_index(t56, t57, gm_srcue4ionbe9lgh, 768, 38);
                    gm_b16_8 t58 = GM_LD(gm_b16_8, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l7)) + t56 * 72) + 0));
                    gm_tnpifv20irnph t59;
                    GM_ST(gm_b16_8, ((uint8_t *)&t59 + 0), t58);
                    gm_tabb710d9mq2e t60;
                    gm_f_format_g0ibdjv((void *)&t60, (void *)NULL, cx, NULL, t59);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    if (((((uint8_t *)&t60))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t60) + 8, gm_srcue4ionbe9lgh, 768, 20);
                    gm_b16_8 t61 = GM_LD(gm_b16_8, (((uint8_t *)&t60) + 8));
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 17, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 18, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 20, 0);
                    ((uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out))[16] = 1;
                    GM_ST(gm_b16_8, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 0, t61);
                    goto gm_fdone;
                }
                goto ix44;
ie44:;
                {
                    gm_th2lqbo77pvsc t63;
                    memset(&t63, 0, sizeof t63);
                    uint8_t t64 = 0;
                    F.ret = t63;
                    goto gm_fret;
                }
ix44:;
            }
            goto ix35;
ie35:;
            {
                gm_th2lqbo77pvsc t66;
                memset(&t66, 0, sizeof t66);
                uint8_t t67 = 0;
                F.ret = t66;
                goto gm_fret;
            }
ix35:;
        }
        goto me2;
mn31:;
ma6:;
        goto me2;
mn68:;
ma7:;
        gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 762, 5);
me2:;
    }
    {
        gm_th2lqbo77pvsc t70;
        memset(&t70, 0, sizeof t70);
        uint8_t t71 = 0;
        F.ret = t70;
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
__attribute__((aligned(4))) void gm_f_equal_hinsj77(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, const void * a2, gm_t88rcucg1b7uk a3, gm_tl874t2atnlh3 a4) {
    struct gm_fr_equal_hinsj77 F;
    void * gm_l0; /* m */
    gm_t88rcucg1b7uk gm_l3; /* t */
    gm_tl874t2atnlh3 gm_l4; /* span */
    void * gm_l5; /* tys */
    unsigned __int128 gm_l6; /* x */
    unsigned __int128 gm_l7; /* y */
    double gm_l8; /* x */
    double gm_l9; /* y */
    uint8_t gm_l10; /* x */
    uint8_t gm_l11; /* y */
    uint32_t gm_l12; /* f */
    uint32_t gm_l13; /* g */
    void * gm_l14; /* x */
    void * gm_l15; /* y */
    gm_t88rcucg1b7uk gm_l16; /* inner */
    void * gm_l17; /* x */
    void * gm_l18; /* y */
    gm_t88rcucg1b7uk gm_l19; /* ok */
    void * gm_l20; /* x */
    void * gm_l21; /* y */
    gm_t88rcucg1b7uk gm_l22; /* err */
    uint32_t gm_l23; /* index */
    void * gm_l24; /* pa */
    uint32_t gm_l25; /* index_b */
    void * gm_l26; /* pb */
    void * gm_l27; /* x */
    void * gm_l28; /* y */
    gm_t88rcucg1b7uk gm_l29; /* pt */
    void * gm_l30; /* x */
    void * gm_l31; /* y */
    gm_b16_8 gm_l32; /* elems */
    uint64_t gm_l33; /* <tmp> */
    uint64_t gm_l34; /* <tmp> */
    uint64_t gm_l35; /* i */
    gm_to6glrduves6o gm_l36; /* x */
    gm_to6glrduves6o gm_l37; /* y */
    gm_t88rcucg1b7uk gm_l38; /* et */
    uint64_t gm_l39; /* <tmp> */
    uint64_t gm_l40; /* <tmp> */
    uint64_t gm_l41; /* i */
    gm_tkaapcii86udk gm_l42; /* x */
    gm_tkaapcii86udk gm_l43; /* y */
    GM_STACK_CHECK(&F, 3245u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&F.l1, a1, sizeof F.l1);
    memcpy(&F.l2, a2, sizeof F.l2);
    gm_l3 = a3;
    gm_l4 = a4;
    {
        void * t2 = gm_l0;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 0));
        void * t4 = (void *)(((uint8_t *)t3) + 8);
        gm_l5 = t4;
    }
    {
        uint8_t t20 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
        switch (t20) { case ((uint8_t)0ULL): goto ma6; case ((uint8_t)1ULL): goto ma7; case ((uint8_t)2ULL): goto ma8; case ((uint8_t)3ULL): goto ma15; case ((uint8_t)4ULL): goto ma17; case ((uint8_t)5ULL): goto ma16; case ((uint8_t)6ULL): goto ma9; case ((uint8_t)7ULL): goto ma11; case ((uint8_t)8ULL): goto ma12; case ((uint8_t)9ULL): goto ma13; case ((uint8_t)10ULL): goto ma14; case ((uint8_t)11ULL): goto ma10; case ((uint8_t)12ULL): goto ma18; case ((uint8_t)13ULL): goto ma18; default: goto ma18; }
ma6:;
        uint8_t t22 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
        if (t22 != ((uint8_t)0ULL)) goto mn21;
        unsigned __int128 t23 = GM_LD(unsigned __int128, (((uint8_t *)&F.l1) + 16));
        gm_l6 = t23;
        {
            uint8_t t26 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 0));
            if (t26 != ((uint8_t)0ULL)) goto ie25;
            unsigned __int128 t27 = GM_LD(unsigned __int128, (((uint8_t *)&F.l2) + 16));
            gm_l7 = t27;
            {
                unsigned __int128 t29 = gm_l6;
                unsigned __int128 t30 = gm_l7;
                uint8_t t31 = ((t29 == t30));
                memset(&(*(gm_tsg40glpvq01o *)gm_out), 0, sizeof (*(gm_tsg40glpvq01o *)gm_out));
                ((uint8_t *)&(*(gm_tsg40glpvq01o *)gm_out))[0] = 0;
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tsg40glpvq01o *)gm_out) + 16, t31);
                goto gm_fdone;
            }
            goto ix25;
ie25:;
            {
                memset(&(*(gm_tsg40glpvq01o *)gm_out), 0, sizeof (*(gm_tsg40glpvq01o *)gm_out));
                ((uint8_t *)&(*(gm_tsg40glpvq01o *)gm_out))[0] = 0;
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tsg40glpvq01o *)gm_out) + 16, ((uint8_t)0));
                goto gm_fdone;
            }
ix25:;
        }
        goto me5;
mn21:;
ma7:;
        uint8_t t34 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
        if (t34 != ((uint8_t)1ULL)) goto mn33;
        double t35 = GM_LD(double, (((uint8_t *)&F.l1) + 16));
        gm_l8 = t35;
        {
            uint8_t t38 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 0));
            if (t38 != ((uint8_t)1ULL)) goto ie37;
            double t39 = GM_LD(double, (((uint8_t *)&F.l2) + 16));
            gm_l9 = t39;
            {
                double t41 = gm_l8;
                double t42 = gm_l9;
                uint8_t t43 = ((t41 == t42));
                memset(&(*(gm_tsg40glpvq01o *)gm_out), 0, sizeof (*(gm_tsg40glpvq01o *)gm_out));
                ((uint8_t *)&(*(gm_tsg40glpvq01o *)gm_out))[0] = 0;
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tsg40glpvq01o *)gm_out) + 16, t43);
                goto gm_fdone;
            }
            goto ix37;
ie37:;
            {
                memset(&(*(gm_tsg40glpvq01o *)gm_out), 0, sizeof (*(gm_tsg40glpvq01o *)gm_out));
                ((uint8_t *)&(*(gm_tsg40glpvq01o *)gm_out))[0] = 0;
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tsg40glpvq01o *)gm_out) + 16, ((uint8_t)0));
                goto gm_fdone;
            }
ix37:;
        }
        goto me5;
mn33:;
ma8:;
        uint8_t t46 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
        if (t46 != ((uint8_t)2ULL)) goto mn45;
        uint8_t t47 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 16));
        gm_l10 = t47;
        {
            uint8_t t50 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 0));
            if (t50 != ((uint8_t)2ULL)) goto ie49;
            uint8_t t51 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 16));
            gm_l11 = t51;
            {
                uint8_t t53 = gm_l10;
                uint8_t t54 = gm_l11;
                uint8_t t55 = ((t53 == t54));
                memset(&(*(gm_tsg40glpvq01o *)gm_out), 0, sizeof (*(gm_tsg40glpvq01o *)gm_out));
                ((uint8_t *)&(*(gm_tsg40glpvq01o *)gm_out))[0] = 0;
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tsg40glpvq01o *)gm_out) + 16, t55);
                goto gm_fdone;
            }
            goto ix49;
ie49:;
            {
                memset(&(*(gm_tsg40glpvq01o *)gm_out), 0, sizeof (*(gm_tsg40glpvq01o *)gm_out));
                ((uint8_t *)&(*(gm_tsg40glpvq01o *)gm_out))[0] = 0;
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tsg40glpvq01o *)gm_out) + 16, ((uint8_t)0));
                goto gm_fdone;
            }
ix49:;
        }
        goto me5;
mn45:;
ma9:;
        uint8_t t58 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
        if (t58 != ((uint8_t)6ULL)) goto mn57;
        uint8_t t59 = ((GM_LD(uint8_t, ((uint8_t *)&F.l2) + 0) == ((uint8_t)6ULL)));
        memset(&(*(gm_tsg40glpvq01o *)gm_out), 0, sizeof (*(gm_tsg40glpvq01o *)gm_out));
        ((uint8_t *)&(*(gm_tsg40glpvq01o *)gm_out))[0] = 0;
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tsg40glpvq01o *)gm_out) + 16, t59);
        goto gm_fdone;
        goto me5;
mn57:;
ma10:;
        uint8_t t61 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
        if (t61 != ((uint8_t)11ULL)) goto mn60;
        uint32_t t62 = GM_LD(uint32_t, (((uint8_t *)&F.l1) + 16));
        gm_l12 = t62;
        {
            uint8_t t65 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 0));
            if (t65 != ((uint8_t)11ULL)) goto ie64;
            uint32_t t66 = GM_LD(uint32_t, (((uint8_t *)&F.l2) + 16));
            gm_l13 = t66;
            {
                uint32_t t68 = gm_l12;
                uint32_t t69 = gm_l13;
                uint8_t t70 = ((t68 == t69));
                memset(&(*(gm_tsg40glpvq01o *)gm_out), 0, sizeof (*(gm_tsg40glpvq01o *)gm_out));
                ((uint8_t *)&(*(gm_tsg40glpvq01o *)gm_out))[0] = 0;
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tsg40glpvq01o *)gm_out) + 16, t70);
                goto gm_fdone;
            }
            goto ix64;
ie64:;
            {
                memset(&(*(gm_tsg40glpvq01o *)gm_out), 0, sizeof (*(gm_tsg40glpvq01o *)gm_out));
                ((uint8_t *)&(*(gm_tsg40glpvq01o *)gm_out))[0] = 0;
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tsg40glpvq01o *)gm_out) + 16, ((uint8_t)0));
                goto gm_fdone;
            }
ix64:;
        }
        goto me5;
mn60:;
ma11:;
        uint8_t t73 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
        if (t73 != ((uint8_t)7ULL)) goto mn72;
        void * t74 = GM_LD(void *, (((uint8_t *)&F.l1) + 16));
        gm_l14 = t74;
        {
            uint8_t t77 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 0));
            if (t77 != ((uint8_t)7ULL)) goto ie76;
            void * t78 = GM_LD(void *, (((uint8_t *)&F.l2) + 16));
            gm_l15 = t78;
            {
                void * t81 = gm_l5;
                gm_t88rcucg1b7uk t82 = gm_l3;
                gm_t53at80pfdus3 t83;
                gm_f_kind_jcnt4hm((void *)&t83, t81, GM_CONV(gm_csguankko4lmhd, t82));
                uint8_t t84 = GM_LD(uint8_t, (((uint8_t *)&t83) + 0));
                if (t84 != ((uint8_t)13ULL)) goto ie80;
                gm_t88rcucg1b7uk t85 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t83) + 8));
                gm_l16 = t85;
                {
                    void * t87 = gm_l0;
                    void * t88 = gm_l14;
                    void * t89 = gm_l15;
                    gm_t88rcucg1b7uk t90 = gm_l16;
                    gm_tl874t2atnlh3 t91 = gm_l4;
                    gm_f_equal_hinsj77((void *)&(*(gm_tsg40glpvq01o *)gm_out), (void *)NULL, cx, NULL, t87, &(*(gm_t6evimt84lndq *)((uint8_t *)t88)), &(*(gm_t6evimt84lndq *)((uint8_t *)t89)), t90, t91);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_fdone;
                }
                goto ix80;
ie80:;
                {
                    void * t93 = gm_l0;
                    gm_tl874t2atnlh3 t94 = gm_l4;
                    gm_b16_8 t95;
                    GM_ST(void *, (uint8_t *)&t95, (void *)(gm_s9ofbo8c28rrv));
                    GM_ST(uintptr_t, (uint8_t *)&t95 + 8, (uintptr_t)(15));
                    gm_t39uai1ajk3qv t96;
                    gm_f_unsupported_2seq49l((void *)&t96, t93, GM_CONV(gm_cseahvg8v7p0vs, t94), t95);
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tsg40glpvq01o *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tsg40glpvq01o *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tsg40glpvq01o *)gm_out) + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tsg40glpvq01o *)gm_out) + 8, 0);
                    ((uint8_t *)&(*(gm_tsg40glpvq01o *)gm_out))[0] = 1;
                    GM_ST(gm_t39uai1ajk3qv, (uint8_t *)&(*(gm_tsg40glpvq01o *)gm_out) + 16, t96);
                    goto gm_fdone;
                }
ix80:;
            }
            goto ix76;
ie76:;
            {
                memset(&(*(gm_tsg40glpvq01o *)gm_out), 0, sizeof (*(gm_tsg40glpvq01o *)gm_out));
                ((uint8_t *)&(*(gm_tsg40glpvq01o *)gm_out))[0] = 0;
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tsg40glpvq01o *)gm_out) + 16, ((uint8_t)0));
                goto gm_fdone;
            }
ix76:;
        }
        goto me5;
mn72:;
ma12:;
        uint8_t t99 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
        if (t99 != ((uint8_t)8ULL)) goto mn98;
        void * t100 = GM_LD(void *, (((uint8_t *)&F.l1) + 16));
        gm_l17 = t100;
        {
            uint8_t t103 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 0));
            if (t103 != ((uint8_t)8ULL)) goto ie102;
            void * t104 = GM_LD(void *, (((uint8_t *)&F.l2) + 16));
            gm_l18 = t104;
            {
                void * t107 = gm_l5;
                gm_t88rcucg1b7uk t108 = gm_l3;
                gm_t53at80pfdus3 t109;
                gm_f_kind_jcnt4hm((void *)&t109, t107, GM_CONV(gm_csguankko4lmhd, t108));
                uint8_t t110 = GM_LD(uint8_t, (((uint8_t *)&t109) + 0));
                if (t110 != ((uint8_t)14ULL)) goto ie106;
                gm_t88rcucg1b7uk t111 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t109) + 8) + 4));
                gm_l19 = t111;
                {
                    void * t113 = gm_l0;
                    void * t114 = gm_l17;
                    void * t115 = gm_l18;
                    gm_t88rcucg1b7uk t116 = gm_l19;
                    gm_tl874t2atnlh3 t117 = gm_l4;
                    gm_f_equal_hinsj77((void *)&(*(gm_tsg40glpvq01o *)gm_out), (void *)NULL, cx, NULL, t113, &(*(gm_t6evimt84lndq *)((uint8_t *)t114)), &(*(gm_t6evimt84lndq *)((uint8_t *)t115)), t116, t117);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_fdone;
                }
                goto ix106;
ie106:;
                {
                    void * t119 = gm_l0;
                    gm_tl874t2atnlh3 t120 = gm_l4;
                    gm_b16_8 t121;
                    GM_ST(void *, (uint8_t *)&t121, (void *)(gm_s9ofbo8c28rrv));
                    GM_ST(uintptr_t, (uint8_t *)&t121 + 8, (uintptr_t)(15));
                    gm_t39uai1ajk3qv t122;
                    gm_f_unsupported_2seq49l((void *)&t122, t119, GM_CONV(gm_cseahvg8v7p0vs, t120), t121);
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tsg40glpvq01o *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tsg40glpvq01o *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tsg40glpvq01o *)gm_out) + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tsg40glpvq01o *)gm_out) + 8, 0);
                    ((uint8_t *)&(*(gm_tsg40glpvq01o *)gm_out))[0] = 1;
                    GM_ST(gm_t39uai1ajk3qv, (uint8_t *)&(*(gm_tsg40glpvq01o *)gm_out) + 16, t122);
                    goto gm_fdone;
                }
ix106:;
            }
            goto ix102;
ie102:;
            {
                memset(&(*(gm_tsg40glpvq01o *)gm_out), 0, sizeof (*(gm_tsg40glpvq01o *)gm_out));
                ((uint8_t *)&(*(gm_tsg40glpvq01o *)gm_out))[0] = 0;
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tsg40glpvq01o *)gm_out) + 16, ((uint8_t)0));
                goto gm_fdone;
            }
ix102:;
        }
        goto me5;
mn98:;
ma13:;
        uint8_t t125 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
        if (t125 != ((uint8_t)9ULL)) goto mn124;
        void * t126 = GM_LD(void *, (((uint8_t *)&F.l1) + 16));
        gm_l20 = t126;
        {
            uint8_t t129 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 0));
            if (t129 != ((uint8_t)9ULL)) goto ie128;
            void * t130 = GM_LD(void *, (((uint8_t *)&F.l2) + 16));
            gm_l21 = t130;
            {
                void * t133 = gm_l5;
                gm_t88rcucg1b7uk t134 = gm_l3;
                gm_t53at80pfdus3 t135;
                gm_f_kind_jcnt4hm((void *)&t135, t133, GM_CONV(gm_csguankko4lmhd, t134));
                uint8_t t136 = GM_LD(uint8_t, (((uint8_t *)&t135) + 0));
                if (t136 != ((uint8_t)14ULL)) goto ie132;
                gm_t88rcucg1b7uk t137 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t135) + 8) + 0));
                gm_l22 = t137;
                {
                    void * t139 = gm_l0;
                    void * t140 = gm_l20;
                    void * t141 = gm_l21;
                    gm_t88rcucg1b7uk t142 = gm_l22;
                    gm_tl874t2atnlh3 t143 = gm_l4;
                    gm_f_equal_hinsj77((void *)&(*(gm_tsg40glpvq01o *)gm_out), (void *)NULL, cx, NULL, t139, &(*(gm_t6evimt84lndq *)((uint8_t *)t140)), &(*(gm_t6evimt84lndq *)((uint8_t *)t141)), t142, t143);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_fdone;
                }
                goto ix132;
ie132:;
                {
                    void * t145 = gm_l0;
                    gm_tl874t2atnlh3 t146 = gm_l4;
                    gm_b16_8 t147;
                    GM_ST(void *, (uint8_t *)&t147, (void *)(gm_s9ofbo8c28rrv));
                    GM_ST(uintptr_t, (uint8_t *)&t147 + 8, (uintptr_t)(15));
                    gm_t39uai1ajk3qv t148;
                    gm_f_unsupported_2seq49l((void *)&t148, t145, GM_CONV(gm_cseahvg8v7p0vs, t146), t147);
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tsg40glpvq01o *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tsg40glpvq01o *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tsg40glpvq01o *)gm_out) + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tsg40glpvq01o *)gm_out) + 8, 0);
                    ((uint8_t *)&(*(gm_tsg40glpvq01o *)gm_out))[0] = 1;
                    GM_ST(gm_t39uai1ajk3qv, (uint8_t *)&(*(gm_tsg40glpvq01o *)gm_out) + 16, t148);
                    goto gm_fdone;
                }
ix132:;
            }
            goto ix128;
ie128:;
            {
                memset(&(*(gm_tsg40glpvq01o *)gm_out), 0, sizeof (*(gm_tsg40glpvq01o *)gm_out));
                ((uint8_t *)&(*(gm_tsg40glpvq01o *)gm_out))[0] = 0;
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tsg40glpvq01o *)gm_out) + 16, ((uint8_t)0));
                goto gm_fdone;
            }
ix128:;
        }
        goto me5;
mn124:;
ma14:;
        uint8_t t151 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
        if (t151 != ((uint8_t)10ULL)) goto mn150;
        uint32_t t152 = GM_LD(uint32_t, ((((uint8_t *)&F.l1) + 16) + 0));
        gm_l23 = t152;
        void * t153 = GM_LD(void *, ((((uint8_t *)&F.l1) + 16) + 8));
        gm_l24 = t153;
        {
            uint8_t t156 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 0));
            if (t156 != ((uint8_t)10ULL)) goto ie155;
            uint32_t t157 = GM_LD(uint32_t, ((((uint8_t *)&F.l2) + 16) + 0));
            gm_l25 = t157;
            void * t158 = GM_LD(void *, ((((uint8_t *)&F.l2) + 16) + 8));
            gm_l26 = t158;
            {
                uint32_t t161 = gm_l23;
                uint32_t t162 = gm_l25;
                uint8_t t163 = (!(t161 == t162));
                if (!t163) goto ie160;
                {
                    memset(&(*(gm_tsg40glpvq01o *)gm_out), 0, sizeof (*(gm_tsg40glpvq01o *)gm_out));
                    ((uint8_t *)&(*(gm_tsg40glpvq01o *)gm_out))[0] = 0;
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tsg40glpvq01o *)gm_out) + 16, ((uint8_t)0));
                    goto gm_fdone;
                }
                goto ix160;
ie160:;
ix160:;
            }
            {
                void * t165 = gm_l24;
                void * t166;
                if ((GM_LD(void *, ((uint8_t *)&t165)) != 0)) {
                    void * t167 = GM_LD(void *, (((uint8_t *)&t165) + 0));
                    t166 = t167;
                } else {
                    memset(&(*(gm_tsg40glpvq01o *)gm_out), 0, sizeof (*(gm_tsg40glpvq01o *)gm_out));
                    ((uint8_t *)&(*(gm_tsg40glpvq01o *)gm_out))[0] = 0;
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tsg40glpvq01o *)gm_out) + 16, ((uint8_t)1));
                    goto gm_fdone;
                }
                gm_l27 = t166;
            }
            {
                void * t168 = gm_l26;
                void * t169;
                if ((GM_LD(void *, ((uint8_t *)&t168)) != 0)) {
                    void * t170 = GM_LD(void *, (((uint8_t *)&t168) + 0));
                    t169 = t170;
                } else {
                    memset(&(*(gm_tsg40glpvq01o *)gm_out), 0, sizeof (*(gm_tsg40glpvq01o *)gm_out));
                    ((uint8_t *)&(*(gm_tsg40glpvq01o *)gm_out))[0] = 0;
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tsg40glpvq01o *)gm_out) + 16, ((uint8_t)1));
                    goto gm_fdone;
                }
                gm_l28 = t169;
            }
            {
                void * t171 = gm_l0;
                gm_t88rcucg1b7uk t172 = gm_l3;
                uint32_t t173 = gm_l23;
                gm_tkn42u7na3t81 t174 = gm_f_payload_ty_mu1ii1k(t171, GM_CONV(gm_csguankko4lmhd, t172), t173);
                gm_t88rcucg1b7uk t175;
                if (((((uint8_t *)&t174))[4] != 0)) {
                    gm_t88rcucg1b7uk t176 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t174) + 0));
                    t175 = t176;
                } else {
                    void * t177 = gm_l0;
                    gm_tl874t2atnlh3 t178 = gm_l4;
                    gm_b16_8 t179;
                    GM_ST(void *, (uint8_t *)&t179, (void *)(gm_s9ofbo8c28rrv));
                    GM_ST(uintptr_t, (uint8_t *)&t179 + 8, (uintptr_t)(15));
                    gm_t39uai1ajk3qv t180;
                    gm_f_unsupported_2seq49l((void *)&t180, t177, GM_CONV(gm_cseahvg8v7p0vs, t178), t179);
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tsg40glpvq01o *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tsg40glpvq01o *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tsg40glpvq01o *)gm_out) + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tsg40glpvq01o *)gm_out) + 8, 0);
                    ((uint8_t *)&(*(gm_tsg40glpvq01o *)gm_out))[0] = 1;
                    GM_ST(gm_t39uai1ajk3qv, (uint8_t *)&(*(gm_tsg40glpvq01o *)gm_out) + 16, t180);
                    goto gm_fdone;
                }
                gm_l29 = t175;
            }
            {
                void * t181 = gm_l0;
                void * t182 = gm_l27;
                void * t183 = gm_l28;
                gm_t88rcucg1b7uk t184 = gm_l29;
                gm_tl874t2atnlh3 t185 = gm_l4;
                gm_f_equal_hinsj77((void *)&(*(gm_tsg40glpvq01o *)gm_out), (void *)NULL, cx, NULL, t181, &(*(gm_t6evimt84lndq *)((uint8_t *)t182)), &(*(gm_t6evimt84lndq *)((uint8_t *)t183)), t184, t185);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                goto gm_fdone;
            }
            goto ix155;
ie155:;
            {
                memset(&(*(gm_tsg40glpvq01o *)gm_out), 0, sizeof (*(gm_tsg40glpvq01o *)gm_out));
                ((uint8_t *)&(*(gm_tsg40glpvq01o *)gm_out))[0] = 0;
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tsg40glpvq01o *)gm_out) + 16, ((uint8_t)0));
                goto gm_fdone;
            }
ix155:;
        }
        goto me5;
mn150:;
ma15:;
        uint8_t t188 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
        if (t188 != ((uint8_t)3ULL)) goto mn187;
        void * t189 = GM_LD(void *, (((uint8_t *)&F.l1) + 16));
        gm_l30 = t189;
        {
            uint8_t t192 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 0));
            if (t192 != ((uint8_t)3ULL)) goto ie191;
            void * t193 = GM_LD(void *, (((uint8_t *)&F.l2) + 16));
            gm_l31 = t193;
            {
                void * t196 = gm_l30;
                uint64_t t197 = GM_LD(uintptr_t, (((uint8_t *)t196) + 0) + 8);
                void * t198 = gm_l31;
                uint64_t t199 = GM_LD(uintptr_t, (((uint8_t *)t198) + 0) + 8);
                uint8_t t200 = (!(t197 == t199));
                if (!t200) goto ie195;
                {
                    memset(&(*(gm_tsg40glpvq01o *)gm_out), 0, sizeof (*(gm_tsg40glpvq01o *)gm_out));
                    ((uint8_t *)&(*(gm_tsg40glpvq01o *)gm_out))[0] = 0;
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tsg40glpvq01o *)gm_out) + 16, ((uint8_t)0));
                    goto gm_fdone;
                }
                goto ix195;
ie195:;
ix195:;
            }
            {
                void * t202 = gm_l0;
                gm_t88rcucg1b7uk t203 = gm_l3;
                void * t204 = gm_l30;
                uint64_t t205 = GM_LD(uintptr_t, (((uint8_t *)t204) + 0) + 8);
                gm_th2lqbo77pvsc t206;
                gm_f_field_tys_foan32f((void *)&t206, (void *)NULL, cx, NULL, t202, t203, t205);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_b16_8 t207;
                if (((((uint8_t *)&t206))[16] != 0)) {
                    gm_b16_8 t208 = GM_LD(gm_b16_8, (((uint8_t *)&t206) + 0));
                    t207 = t208;
                } else {
                    void * t209 = gm_l0;
                    gm_tl874t2atnlh3 t210 = gm_l4;
                    gm_b16_8 t211;
                    GM_ST(void *, (uint8_t *)&t211, (void *)(gm_s9ofbo8c28rrv));
                    GM_ST(uintptr_t, (uint8_t *)&t211 + 8, (uintptr_t)(15));
                    gm_t39uai1ajk3qv t212;
                    gm_f_unsupported_2seq49l((void *)&t212, t209, GM_CONV(gm_cseahvg8v7p0vs, t210), t211);
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tsg40glpvq01o *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tsg40glpvq01o *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tsg40glpvq01o *)gm_out) + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tsg40glpvq01o *)gm_out) + 8, 0);
                    ((uint8_t *)&(*(gm_tsg40glpvq01o *)gm_out))[0] = 1;
                    GM_ST(gm_t39uai1ajk3qv, (uint8_t *)&(*(gm_tsg40glpvq01o *)gm_out) + 16, t212);
                    goto gm_fdone;
                }
                gm_l32 = t207;
            }
            {
                {
                    gm_l34 = ((uint64_t)0ULL);
                }
                {
                    void * t214 = gm_l30;
                    uint64_t t215 = GM_LD(uintptr_t, (((uint8_t *)t214) + 0) + 8);
                    gm_l33 = t215;
                }
                {
                    for (;;) {
                        uint64_t t217 = gm_l34;
                        uint64_t t218 = gm_l33;
                        uint8_t t219 = (t217 < t218);
                        if (!t219) goto lb216;
                        {
                            uint64_t t222 = gm_l34;
                            uint64_t t225 = GM_LD(uint64_t, ((uint8_t *)&t222));
                            gm_l35 = t225;
                            {
                                void * t228 = gm_l0;
                                uint64_t t229 = gm_l35;
                                uint64_t t230 = (uintptr_t)t229;
                                void * t231 = gm_l30;
                                uint64_t t232 = GM_LD(uintptr_t, (((uint8_t *)t231) + 0) + 8);
                                if (t230 >= (uintptr_t)(t232)) gm_trap_index(t230, t232, gm_srcue4ionbe9lgh, 703, 36);
                                uint64_t t233 = gm_l35;
                                uint64_t t234 = (uintptr_t)t233;
                                void * t235 = gm_l31;
                                uint64_t t236 = GM_LD(uintptr_t, (((uint8_t *)t235) + 0) + 8);
                                if (t234 >= (uintptr_t)(t236)) gm_trap_index(t234, t236, gm_srcue4ionbe9lgh, 703, 48);
                                uint64_t t237 = gm_l35;
                                uint64_t t238 = (uintptr_t)t237;
                                uint64_t t239 = GM_LD(uintptr_t, ((uint8_t *)&gm_l32) + 8);
                                if (t238 >= (uintptr_t)(t239)) gm_trap_index(t238, t239, gm_srcue4ionbe9lgh, 703, 60);
                                gm_t88rcucg1b7uk t240 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l32)) + t238 * 4));
                                gm_tl874t2atnlh3 t241 = gm_l4;
                                gm_tsg40glpvq01o t242;
                                gm_f_equal_hinsj77((void *)&t242, (void *)NULL, cx, NULL, t228, &(*(gm_t6evimt84lndq *)((uint8_t *)GM_LD(void *, (((uint8_t *)t231) + 0)) + t230 * 48)), &(*(gm_t6evimt84lndq *)((uint8_t *)GM_LD(void *, (((uint8_t *)t235) + 0)) + t234 * 48)), t240, t241);
                                if (gm_unw.kind) {
                                    goto gm_ll220;
                                }
                                if (((((uint8_t *)&t242))[0] != 0)) {
                                    gm_tsg40glpvq01o t243;
                                    GM_ST(uint8_t, (uint8_t *)&t243 + 1, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t243 + 2, 0);
                                    GM_ST(uint32_t, (uint8_t *)&t243 + 4, 0);
                                    GM_ST(uint64_t, (uint8_t *)&t243 + 8, 0);
                                    ((uint8_t *)&t243)[0] = 1;
                                    GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t243 + 16), (*(gm_t39uai1ajk3qv *)(((uint8_t *)&t242) + 16)));
                                    F.ret = t243;
                                    goto gm_fret;
                                }
                                uint8_t t244 = GM_LD(uint8_t, (((uint8_t *)&t242) + 16));
                                uint8_t t245 = (uint8_t)!t244;
                                if (!t245) goto ie227;
                                {
                                    memset(&(*(gm_tsg40glpvq01o *)gm_out), 0, sizeof (*(gm_tsg40glpvq01o *)gm_out));
                                    ((uint8_t *)&(*(gm_tsg40glpvq01o *)gm_out))[0] = 0;
                                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tsg40glpvq01o *)gm_out) + 16, ((uint8_t)0));
                                    goto gm_fdone;
                                }
                                goto ix227;
ie227:;
ix227:;
                            }
                            goto me223;
mn224:;
                            gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 702, 13);
me223:;
                        }
lc216:;
                        {
                            uint64_t t248 = gm_l34;
                            uint64_t r249; bool o250 = __builtin_add_overflow(t248, ((uint64_t)1ULL), &r249);
                            uint64_t t251 = r249;
                            gm_l34 = t251;
                        }
                    }
gm_ll220:;
                    if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb216; }
                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc216; }
                    }
                    goto gm_lad;
lb216:;
                }
            }
            {
                memset(&(*(gm_tsg40glpvq01o *)gm_out), 0, sizeof (*(gm_tsg40glpvq01o *)gm_out));
                ((uint8_t *)&(*(gm_tsg40glpvq01o *)gm_out))[0] = 0;
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tsg40glpvq01o *)gm_out) + 16, ((uint8_t)1));
                goto gm_fdone;
            }
            goto ix191;
ie191:;
            {
                memset(&(*(gm_tsg40glpvq01o *)gm_out), 0, sizeof (*(gm_tsg40glpvq01o *)gm_out));
                ((uint8_t *)&(*(gm_tsg40glpvq01o *)gm_out))[0] = 0;
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tsg40glpvq01o *)gm_out) + 16, ((uint8_t)0));
                goto gm_fdone;
            }
ix191:;
        }
        goto me5;
mn187:;
ma16:;
        uint8_t t254 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
        if (t254 != ((uint8_t)5ULL)) goto mn253;
        gm_to6glrduves6o t255 = GM_LD(gm_to6glrduves6o, (((uint8_t *)&F.l1) + 16));
        gm_l36 = t255;
        {
            uint8_t t258 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 0));
            if (t258 != ((uint8_t)5ULL)) goto ie257;
            gm_to6glrduves6o t259 = GM_LD(gm_to6glrduves6o, (((uint8_t *)&F.l2) + 16));
            gm_l37 = t259;
            {
                void * t262 = gm_l5;
                gm_t88rcucg1b7uk t263 = gm_l3;
                gm_t53at80pfdus3 t264;
                gm_f_kind_jcnt4hm((void *)&t264, t262, GM_CONV(gm_csguankko4lmhd, t263));
                uint8_t t265 = GM_LD(uint8_t, (((uint8_t *)&t264) + 0));
                if (t265 != ((uint8_t)10ULL)) goto ie261;
                gm_t88rcucg1b7uk t266 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t264) + 8) + 4));
                gm_l38 = t266;
                {
                    uint64_t t269 = GM_LD(uint64_t, (((uint8_t *)&gm_l36) + 24));
                    uint64_t t270 = GM_LD(uint64_t, (((uint8_t *)&gm_l37) + 24));
                    uint8_t t271 = (!(t269 == t270));
                    if (!t271) goto ie268;
                    {
                        memset(&(*(gm_tsg40glpvq01o *)gm_out), 0, sizeof (*(gm_tsg40glpvq01o *)gm_out));
                        ((uint8_t *)&(*(gm_tsg40glpvq01o *)gm_out))[0] = 0;
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tsg40glpvq01o *)gm_out) + 16, ((uint8_t)0));
                        goto gm_fdone;
                    }
                    goto ix268;
ie268:;
ix268:;
                }
                {
                    {
                        gm_l40 = ((uint64_t)0ULL);
                    }
                    {
                        uint64_t t274 = GM_LD(uint64_t, (((uint8_t *)&gm_l36) + 24));
                        gm_l39 = t274;
                    }
                    {
                        for (;;) {
                            uint64_t t276 = gm_l40;
                            uint64_t t277 = gm_l39;
                            uint8_t t278 = (t276 < t277);
                            if (!t278) goto lb275;
                            {
                                uint64_t t281 = gm_l40;
                                uint64_t t284 = GM_LD(uint64_t, ((uint8_t *)&t281));
                                gm_l41 = t284;
                                {
                                    void * t287 = gm_l0;
                                    void * t288 = gm_l0;
                                    uint64_t t289 = gm_l41;
                                    gm_tkaapcii86udk t290;
                                    gm_f_offset_nkn711s((void *)&t290, (void *)NULL, cx, NULL, &(*(gm_tkaapcii86udk *)(((uint8_t *)&gm_l36) + 0)), t289);
                                    if (gm_unw.kind) {
                                        goto gm_ll279;
                                    }
                                    gm_t6evimt84lndq t291;
                                    gm_f_load_nf7o5kl((void *)&t291, t288, &t290);
                                    void * t292 = gm_l0;
                                    uint64_t t293 = gm_l41;
                                    gm_tkaapcii86udk t294;
                                    gm_f_offset_nkn711s((void *)&t294, (void *)NULL, cx, NULL, &(*(gm_tkaapcii86udk *)(((uint8_t *)&gm_l37) + 0)), t293);
                                    if (gm_unw.kind) {
                                        goto gm_ll279;
                                    }
                                    gm_t6evimt84lndq t295;
                                    gm_f_load_nf7o5kl((void *)&t295, t292, &t294);
                                    gm_t88rcucg1b7uk t296 = gm_l38;
                                    gm_tl874t2atnlh3 t297 = gm_l4;
                                    gm_tsg40glpvq01o t298;
                                    gm_f_equal_hinsj77((void *)&t298, (void *)NULL, cx, NULL, t287, &t291, &t295, t296, t297);
                                    if (gm_unw.kind) {
                                        goto gm_ll279;
                                    }
                                    if (((((uint8_t *)&t298))[0] != 0)) {
                                        gm_tsg40glpvq01o t299;
                                        GM_ST(uint8_t, (uint8_t *)&t299 + 1, 0);
                                        GM_ST(uint16_t, (uint8_t *)&t299 + 2, 0);
                                        GM_ST(uint32_t, (uint8_t *)&t299 + 4, 0);
                                        GM_ST(uint64_t, (uint8_t *)&t299 + 8, 0);
                                        ((uint8_t *)&t299)[0] = 1;
                                        GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t299 + 16), (*(gm_t39uai1ajk3qv *)(((uint8_t *)&t298) + 16)));
                                        F.ret = t299;
                                        goto gm_fret;
                                    }
                                    uint8_t t300 = GM_LD(uint8_t, (((uint8_t *)&t298) + 16));
                                    uint8_t t301 = (uint8_t)!t300;
                                    if (!t301) goto ie286;
                                    {
                                        memset(&(*(gm_tsg40glpvq01o *)gm_out), 0, sizeof (*(gm_tsg40glpvq01o *)gm_out));
                                        ((uint8_t *)&(*(gm_tsg40glpvq01o *)gm_out))[0] = 0;
                                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tsg40glpvq01o *)gm_out) + 16, ((uint8_t)0));
                                        goto gm_fdone;
                                    }
                                    goto ix286;
ie286:;
ix286:;
                                }
                                goto me282;
mn283:;
                                gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 711, 13);
me282:;
                            }
lc275:;
                            {
                                uint64_t t304 = gm_l40;
                                uint64_t r305; bool o306 = __builtin_add_overflow(t304, ((uint64_t)1ULL), &r305);
                                uint64_t t307 = r305;
                                gm_l40 = t307;
                            }
                        }
gm_ll279:;
                        if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb275; }
                            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc275; }
                        }
                        goto gm_lad;
lb275:;
                    }
                }
                {
                    memset(&(*(gm_tsg40glpvq01o *)gm_out), 0, sizeof (*(gm_tsg40glpvq01o *)gm_out));
                    ((uint8_t *)&(*(gm_tsg40glpvq01o *)gm_out))[0] = 0;
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tsg40glpvq01o *)gm_out) + 16, ((uint8_t)1));
                    goto gm_fdone;
                }
                goto ix261;
ie261:;
                {
                    void * t309 = gm_l0;
                    gm_tl874t2atnlh3 t310 = gm_l4;
                    gm_b16_8 t311;
                    GM_ST(void *, (uint8_t *)&t311, (void *)(gm_s9ofbo8c28rrv));
                    GM_ST(uintptr_t, (uint8_t *)&t311 + 8, (uintptr_t)(15));
                    gm_t39uai1ajk3qv t312;
                    gm_f_unsupported_2seq49l((void *)&t312, t309, GM_CONV(gm_cseahvg8v7p0vs, t310), t311);
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tsg40glpvq01o *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tsg40glpvq01o *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tsg40glpvq01o *)gm_out) + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tsg40glpvq01o *)gm_out) + 8, 0);
                    ((uint8_t *)&(*(gm_tsg40glpvq01o *)gm_out))[0] = 1;
                    GM_ST(gm_t39uai1ajk3qv, (uint8_t *)&(*(gm_tsg40glpvq01o *)gm_out) + 16, t312);
                    goto gm_fdone;
                }
ix261:;
            }
            goto ix257;
ie257:;
            {
                memset(&(*(gm_tsg40glpvq01o *)gm_out), 0, sizeof (*(gm_tsg40glpvq01o *)gm_out));
                ((uint8_t *)&(*(gm_tsg40glpvq01o *)gm_out))[0] = 0;
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tsg40glpvq01o *)gm_out) + 16, ((uint8_t)0));
                goto gm_fdone;
            }
ix257:;
        }
        goto me5;
mn253:;
ma17:;
        uint8_t t315 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
        if (t315 != ((uint8_t)4ULL)) goto mn314;
        gm_tkaapcii86udk t316 = GM_LD(gm_tkaapcii86udk, (((uint8_t *)&F.l1) + 16));
        gm_l42 = t316;
        {
            uint8_t t319 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 0));
            if (t319 != ((uint8_t)4ULL)) goto ie318;
            gm_tkaapcii86udk t320 = GM_LD(gm_tkaapcii86udk, (((uint8_t *)&F.l2) + 16));
            gm_l43 = t320;
            {
                uint8_t t322 = gm_f_same_place_1r7qrqk(&(*(gm_tkaapcii86udk *)((uint8_t *)&gm_l42)), &(*(gm_tkaapcii86udk *)((uint8_t *)&gm_l43)));
                memset(&(*(gm_tsg40glpvq01o *)gm_out), 0, sizeof (*(gm_tsg40glpvq01o *)gm_out));
                ((uint8_t *)&(*(gm_tsg40glpvq01o *)gm_out))[0] = 0;
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tsg40glpvq01o *)gm_out) + 16, t322);
                goto gm_fdone;
            }
            goto ix318;
ie318:;
            {
                memset(&(*(gm_tsg40glpvq01o *)gm_out), 0, sizeof (*(gm_tsg40glpvq01o *)gm_out));
                ((uint8_t *)&(*(gm_tsg40glpvq01o *)gm_out))[0] = 0;
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tsg40glpvq01o *)gm_out) + 16, ((uint8_t)0));
                goto gm_fdone;
            }
ix318:;
        }
        goto me5;
mn314:;
ma18:;
        void * t325 = gm_l0;
        gm_tl874t2atnlh3 t326 = gm_l4;
        gm_b16_8 t327;
        GM_ST(void *, (uint8_t *)&t327, (void *)(gm_s8afn1h3asffl));
        GM_ST(uintptr_t, (uint8_t *)&t327 + 8, (uintptr_t)(31));
        gm_t39uai1ajk3qv t328;
        gm_f_unsupported_2seq49l((void *)&t328, t325, GM_CONV(gm_cseahvg8v7p0vs, t326), t327);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tsg40glpvq01o *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tsg40glpvq01o *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tsg40glpvq01o *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_tsg40glpvq01o *)gm_out) + 8, 0);
        ((uint8_t *)&(*(gm_tsg40glpvq01o *)gm_out))[0] = 1;
        GM_ST(gm_t39uai1ajk3qv, (uint8_t *)&(*(gm_tsg40glpvq01o *)gm_out) + 16, t328);
        goto gm_fdone;
        goto me5;
mn324:;
ma19:;
        gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 657, 5);
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
__attribute__((aligned(4))) void gm_f_field_tys_foan32f(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, uint64_t a2) {
    struct gm_fr_field_tys_foan32f F;
    void * gm_l0; /* m */
    gm_t88rcucg1b7uk gm_l1; /* t */
    uint64_t gm_l2; /* n */
    void * gm_l3; /* tys */
    gm_b16_8 gm_l4; /* elems */
    gm_t88rcucg1b7uk gm_l5; /* elem */
    uint64_t gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    uint32_t gm_l9; /* id */
    gm_b16_8 gm_l12; /* <tmp> */
    uint64_t gm_l13; /* <tmp> */
    GM_STACK_CHECK(&F, 625u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 0));
        void * t4 = (void *)(((uint8_t *)t3) + 8);
        gm_l3 = t4;
    }
    {
        void * t5 = gm_l3;
        gm_t88rcucg1b7uk t6 = gm_l1;
        gm_t53at80pfdus3 t7;
        gm_f_kind_jcnt4hm((void *)&t7, t5, GM_CONV(gm_csguankko4lmhd, t6));
        uint8_t t14 = GM_LD(uint8_t, (((uint8_t *)&t7) + 0));
        switch (t14) { case ((uint8_t)0ULL): goto ma12; case ((uint8_t)1ULL): goto ma12; case ((uint8_t)2ULL): goto ma12; case ((uint8_t)3ULL): goto ma12; case ((uint8_t)4ULL): goto ma12; case ((uint8_t)5ULL): goto ma12; case ((uint8_t)6ULL): goto ma12; case ((uint8_t)7ULL): goto ma12; case ((uint8_t)8ULL): goto ma12; case ((uint8_t)9ULL): goto ma12; case ((uint8_t)10ULL): goto ma12; case ((uint8_t)11ULL): goto ma10; case ((uint8_t)12ULL): goto ma9; case ((uint8_t)13ULL): goto ma12; case ((uint8_t)14ULL): goto ma12; case ((uint8_t)15ULL): goto ma12; case ((uint8_t)16ULL): goto ma12; case ((uint8_t)17ULL): goto ma11; case ((uint8_t)18ULL): goto ma12; case ((uint8_t)19ULL): goto ma12; case ((uint8_t)20ULL): goto ma12; case ((uint8_t)21ULL): goto ma12; case ((uint8_t)22ULL): goto ma12; case ((uint8_t)23ULL): goto ma12; case ((uint8_t)24ULL): goto ma12; case ((uint8_t)25ULL): goto ma12; case ((uint8_t)26ULL): goto ma12; default: goto ma12; }
ma9:;
        uint8_t t16 = GM_LD(uint8_t, (((uint8_t *)&t7) + 0));
        if (t16 != ((uint8_t)12ULL)) goto mn15;
        gm_b16_8 t17 = GM_LD(gm_b16_8, ((((uint8_t *)&t7) + 8) + 0));
        gm_l4 = t17;
        gm_b16_8 t18 = gm_l4;
        GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 20, 0);
        ((uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out))[16] = 1;
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 0, t18);
        goto gm_fdone;
        goto me8;
mn15:;
ma10:;
        uint8_t t20 = GM_LD(uint8_t, (((uint8_t *)&t7) + 0));
        if (t20 != ((uint8_t)11ULL)) goto mn19;
        gm_t88rcucg1b7uk t21 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t7) + 8) + 8));
        gm_l5 = t21;
        {
            gm_f_list_nof7vfj((void *)&F.l6, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            {
                gm_l8 = ((uint64_t)0ULL);
            }
            {
                uint64_t t24 = gm_l2;
                gm_l7 = t24;
            }
            {
                for (;;) {
                    uint64_t t26 = gm_l8;
                    uint64_t t27 = gm_l7;
                    uint8_t t28 = (t26 < t27);
                    if (!t28) goto lb25;
                    {
                        uint64_t t31 = gm_l8;
                        {
                            void * t35 = (void *)((uint8_t *)&F.l6);
                            gm_t88rcucg1b7uk t36 = gm_l5;
                            gm_tftb1odc739t5 t37 = gm_f_push_k3l9nic(t35, GM_CONV(gm_csguankko4lmhd, t36));
                            if (((((uint8_t *)&t37))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t37) + 1, gm_srcue4ionbe9lgh, 739, 29);
                            gm_b0_1 t38 = GM_LD(gm_b0_1, (((uint8_t *)&t37) + 1));
                        }
                        goto me32;
mn33:;
                        gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 739, 13);
me32:;
                    }
lc25:;
                    {
                        uint64_t t40 = gm_l8;
                        uint64_t r41; bool o42 = __builtin_add_overflow(t40, ((uint64_t)1ULL), &r41);
                        uint64_t t43 = r41;
                        gm_l8 = t43;
                    }
                }
lb25:;
            }
        }
        {
            gm_b16_8 t44 = GM_LD(gm_b16_8, (((uint8_t *)&F.l6) + 0));
            gm_b16_8 t45 = GM_LD(gm_b16_8, ((uint8_t *)&t44));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 20, 0);
            ((uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out))[16] = 1;
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 0, t45);
            goto gm_fdone;
        }
        goto me8;
mn19:;
ma11:;
        uint8_t t47 = GM_LD(uint8_t, (((uint8_t *)&t7) + 0));
        if (t47 != ((uint8_t)17ULL)) goto mn46;
        uint32_t t48 = GM_LD(uint32_t, ((((uint8_t *)&t7) + 8) + 0));
        gm_l9 = t48;
        {
            uint32_t t51 = gm_l9;
            uint64_t t52 = (uint64_t)t51;
            uint64_t t53 = (uintptr_t)t52;
            void * t54 = gm_l0;
            void * t55 = GM_LD(void *, (((uint8_t *)t54) + 0));
            uint64_t t56 = GM_LD(uintptr_t, ((((uint8_t *)t55) + 1088) + 0) + 8);
            if (t53 >= (uintptr_t)(t56)) gm_trap_index(t53, t56, gm_srcue4ionbe9lgh, 743, 29);
            uint8_t t57 = GM_LD(uint8_t, ((((uint8_t *)GM_LD(void *, ((((uint8_t *)t55) + 1088) + 0)) + t53 * 184) + 24) + 0));
            if (t57 != ((uint8_t)0ULL)) goto ie50;
            gm_tpautr4br7h3l t58 = GM_LD(gm_tpautr4br7h3l, ((((uint8_t *)GM_LD(void *, ((((uint8_t *)t55) + 1088) + 0)) + t53 * 184) + 24) + 8));
            F.l10 = t58;
            {
                gm_f_list_nof7vfj((void *)&F.l11, (void *)NULL, cx, NULL);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                {
                    gm_b16_8 t61 = GM_LD(gm_b16_8, (((uint8_t *)&F.l10) + 0));
                    gm_l12 = t61;
                }
                {
                    gm_l13 = ((uint64_t)0ULL);
                }
                {
                    for (;;) {
                        uint64_t t63 = gm_l13;
                        uint64_t t64 = GM_LD(uintptr_t, ((uint8_t *)&gm_l12) + 8);
                        uint8_t t65 = (t63 < t64);
                        if (!t65) goto lb62;
                        {
                            uint64_t t68 = gm_l13;
                            uint64_t t69 = (uintptr_t)t68;
                            uint64_t t70 = GM_LD(uintptr_t, ((uint8_t *)&gm_l12) + 8);
                            gm_t0mglksrkfa5e t73 = GM_LD(gm_t0mglksrkfa5e, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l12)) + t69 * 40));
                            F.l14 = t73;
                            {
                                void * t75 = (void *)((uint8_t *)&F.l11);
                                gm_t88rcucg1b7uk t76 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l14) + 16));
                                gm_tftb1odc739t5 t77 = gm_f_push_k3l9nic(t75, GM_CONV(gm_csguankko4lmhd, t76));
                                if (((((uint8_t *)&t77))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t77) + 1, gm_srcue4ionbe9lgh, 745, 35);
                                gm_b0_1 t78 = GM_LD(gm_b0_1, (((uint8_t *)&t77) + 1));
                            }
                            goto me71;
mn72:;
                            gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 745, 13);
me71:;
                        }
lc62:;
                        {
                            uint64_t t80 = gm_l13;
                            uint64_t r81; bool o82 = __builtin_add_overflow(t80, ((uint64_t)1ULL), &r81);
                            uint64_t t83 = r81;
                            gm_l13 = t83;
                        }
                    }
lb62:;
                }
            }
            {
                gm_b16_8 t84 = GM_LD(gm_b16_8, (((uint8_t *)&F.l11) + 0));
                gm_b16_8 t85 = GM_LD(gm_b16_8, ((uint8_t *)&t84));
                GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 17, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 18, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 20, 0);
                ((uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out))[16] = 1;
                GM_ST(gm_b16_8, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 0, t85);
                goto gm_fdone;
            }
            goto ix50;
ie50:;
            {
                gm_th2lqbo77pvsc t87;
                memset(&t87, 0, sizeof t87);
                uint8_t t88 = 0;
                F.ret = t87;
                goto gm_fret;
            }
ix50:;
        }
        goto me8;
mn46:;
ma12:;
        gm_th2lqbo77pvsc t90;
        memset(&t90, 0, sizeof t90);
        uint8_t t91 = 0;
        F.ret = t90;
        goto gm_fret;
        goto me8;
mn89:;
ma13:;
        gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 735, 5);
me8:;
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
inline __attribute__((aligned(4))) void gm_f_fit_8hvi4bv(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_t9qkh0rsooc8d a2, gm_t88rcucg1b7uk a3, gm_b16_8 a4, gm_tl874t2atnlh3 a5) {
    struct gm_fr_fit_8hvi4bv F;
    void * gm_l0; /* m */
    gm_t9qkh0rsooc8d gm_l2; /* flavor */
    gm_t88rcucg1b7uk gm_l3; /* t */
    gm_b16_8 gm_l4; /* sym */
    gm_tl874t2atnlh3 gm_l5; /* span */
    GM_STACK_CHECK(&F, 643u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_l5 = a5;
    {
        void * t2 = gm_l0;
        gm_t88rcucg1b7uk t3 = gm_l3;
        gm_f_int_range_hue3vms((void *)&F.l6, (void *)NULL, cx, NULL, t2, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        int32_t t5 = gm_f_cmp_i1knrgu(&(*(gm_tbci5ospk5nnn *)((uint8_t *)a1)), &(*(gm_tbci5ospk5nnn *)(((uint8_t *)&F.l6) + 0)));
        uint8_t t6 = (t5 >= ((int32_t)0LL));
        if (!t6) goto ie4;
        int32_t t7 = gm_f_cmp_i1knrgu(&(*(gm_tbci5ospk5nnn *)((uint8_t *)a1)), &(*(gm_tbci5ospk5nnn *)(((uint8_t *)&F.l6) + 48)));
        uint8_t t8 = (t7 <= ((int32_t)0LL));
        if (!t8) goto ie4;
        {
            void * t10 = gm_l0;
            unsigned __int128 t11 = gm_f_bits128_jigkui4(&(*(gm_tbci5ospk5nnn *)((uint8_t *)a1)));
            gm_t88rcucg1b7uk t12 = gm_l3;
            unsigned __int128 t13 = gm_f_wrap_bt8r149(t10, t11, GM_CONV(gm_csguankko4lmhd, t12));
            memset(&(*(gm_t8carr7mku484 *)gm_out), 0, sizeof (*(gm_t8carr7mku484 *)gm_out));
            ((uint8_t *)&(*(gm_t8carr7mku484 *)gm_out))[0] = 0;
            GM_ST(unsigned __int128, (uint8_t *)&(*(gm_t8carr7mku484 *)gm_out) + 16, t13);
            goto gm_fdone;
        }
        goto ix4;
ie4:;
ix4:;
    }
    {
        uint8_t t19 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        switch (t19) { case ((uint8_t)0ULL): goto ma17; case ((uint8_t)1ULL): goto ma15; case ((uint8_t)2ULL): goto ma16; case ((uint8_t)3ULL): goto ma17; default: goto ma17; }
ma15:;
        uint8_t t21 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t21 != ((uint8_t)1ULL)) goto mn20;
        void * t22 = gm_l0;
        unsigned __int128 t23 = gm_f_bits128_jigkui4(&(*(gm_tbci5ospk5nnn *)((uint8_t *)a1)));
        gm_t88rcucg1b7uk t24 = gm_l3;
        unsigned __int128 t25 = gm_f_wrap_bt8r149(t22, t23, GM_CONV(gm_csguankko4lmhd, t24));
        memset(&(*(gm_t8carr7mku484 *)gm_out), 0, sizeof (*(gm_t8carr7mku484 *)gm_out));
        ((uint8_t *)&(*(gm_t8carr7mku484 *)gm_out))[0] = 0;
        GM_ST(unsigned __int128, (uint8_t *)&(*(gm_t8carr7mku484 *)gm_out) + 16, t25);
        goto gm_fdone;
        goto me14;
mn20:;
ma16:;
        uint8_t t27 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t27 != ((uint8_t)2ULL)) goto mn26;
        void * t28 = gm_l0;
        gm_tbci5ospk5nnn t29;
        uint8_t t31 = gm_f_is_negative_k87pp3n(&(*(gm_tbci5ospk5nnn *)((uint8_t *)a1)));
        if (!t31) goto ie30;
        gm_tbci5ospk5nnn t33 = GM_LD(gm_tbci5ospk5nnn, (((uint8_t *)&F.l6) + 0));
        gm_tbci5ospk5nnn t34 = t33;
        t29 = t34;
        goto ix30;
ie30:;
        gm_tbci5ospk5nnn t36 = GM_LD(gm_tbci5ospk5nnn, (((uint8_t *)&F.l6) + 48));
        gm_tbci5ospk5nnn t37 = t36;
        t29 = t37;
ix30:;
        unsigned __int128 t38 = gm_f_bits128_jigkui4(&t29);
        gm_t88rcucg1b7uk t39 = gm_l3;
        unsigned __int128 t40 = gm_f_wrap_bt8r149(t28, t38, GM_CONV(gm_csguankko4lmhd, t39));
        memset(&(*(gm_t8carr7mku484 *)gm_out), 0, sizeof (*(gm_t8carr7mku484 *)gm_out));
        ((uint8_t *)&(*(gm_t8carr7mku484 *)gm_out))[0] = 0;
        GM_ST(unsigned __int128, (uint8_t *)&(*(gm_t8carr7mku484 *)gm_out) + 16, t40);
        goto gm_fdone;
        goto me14;
mn26:;
ma17:;
        void * t42 = gm_l0;
        gm_tl874t2atnlh3 t43 = gm_l5;
        gm_b16_8 t44 = gm_l4;
        gm_tnpifv20irnph t45;
        GM_ST(gm_b16_8, ((uint8_t *)&t45 + 0), t44);
        gm_tabb710d9mq2e t46;
        gm_f_format_eruojoe((void *)&t46, (void *)NULL, cx, NULL, t45);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t46))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t46) + 8, gm_srcue4ionbe9lgh, 53, 35);
        gm_b16_8 t47 = GM_LD(gm_b16_8, (((uint8_t *)&t46) + 8));
        gm_b16_8 t48 = GM_LD(gm_b16_8, ((uint8_t *)&t47));
        gm_t39uai1ajk3qv t49;
        gm_f_trap_jftbss6((void *)&t49, (void *)NULL, cx, NULL, t42, t43, t48);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t8carr7mku484 *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t8carr7mku484 *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t8carr7mku484 *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t8carr7mku484 *)gm_out) + 8, 0);
        ((uint8_t *)&(*(gm_t8carr7mku484 *)gm_out))[0] = 1;
        GM_ST(gm_t39uai1ajk3qv, (uint8_t *)&(*(gm_t8carr7mku484 *)gm_out) + 16, t49);
        goto gm_fdone;
        goto me14;
mn41:;
ma18:;
        gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 50, 5);
me14:;
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
__attribute__((aligned(4))) void gm_f_fit_small_7o94i8v(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, __int128 a1, gm_t9qkh0rsooc8d a2, gm_t88rcucg1b7uk a3, gm_b16_8 a4, gm_tl874t2atnlh3 a5) {
    struct gm_fr_fit_small_7o94i8v F;
    void * gm_l0; /* m */
    __int128 gm_l1; /* exact */
    gm_t9qkh0rsooc8d gm_l2; /* flavor */
    gm_t88rcucg1b7uk gm_l3; /* t */
    gm_b16_8 gm_l4; /* sym */
    gm_tl874t2atnlh3 gm_l5; /* span */
    gm_ttovhfee8mf28 gm_l6; /* info */
    __int128 gm_l7; /* lo */
    __int128 gm_l8; /* hi */
    GM_STACK_CHECK(&F, 865u);
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
        gm_t88rcucg1b7uk t3 = gm_l3;
        gm_ttovhfee8mf28 t4 = gm_f_int_info_2r9qb5n(t2, GM_CONV(gm_csguankko4lmhd, t3));
        gm_l6 = t4;
    }
    {
        __int128 t5;
        uint8_t t7 = GM_LD(uint8_t, (((uint8_t *)&gm_l6) + 0));
        if (!t7) goto ie6;
        uint32_t t9 = GM_LD(uint32_t, (((uint8_t *)&gm_l6) + 4));
        uint32_t r10; bool o11 = __builtin_sub_overflow(t9, ((uint32_t)1ULL), &r10);
        if (o11) gm_trap("integer overflow in `-`", gm_srcue4ionbe9lgh, 60, 47);
        uint32_t t12 = r10;
        unsigned __int128 n13 = (unsigned __int128)t12;
        if (n13 >= 128) gm_trap_shift((uint64_t)n13, "i128", gm_srcue4ionbe9lgh, 60, 35);
        __int128 r14 = (__int128)((unsigned __int128)((__int128)(((unsigned __int128)0u << 64) | 1u)) << (int)n13);
        bool o15 = (__int128)(r14 >> (int)n13) != ((__int128)(((unsigned __int128)0u << 64) | 1u));
        if (o15) gm_trap("integer overflow in `<<`", gm_srcue4ionbe9lgh, 60, 35);
        __int128 t16 = r14;
        if (t16 == ((__int128)(((unsigned __int128)9223372036854775808u << 64) | 0u))) gm_trap("integer overflow in `-`", gm_srcue4ionbe9lgh, 60, 33);
        __int128 t17 = (__int128)((unsigned __int128)0 - (unsigned __int128)t16);
        __int128 t18 = t17;
        t5 = t18;
        goto ix6;
ie6:;
        __int128 t20 = ((__int128)(((unsigned __int128)0u << 64) | 0u));
        t5 = t20;
ix6:;
        gm_l7 = t5;
    }
    {
        __int128 t21;
        uint8_t t23 = GM_LD(uint8_t, (((uint8_t *)&gm_l6) + 0));
        if (!t23) goto ie22;
        uint32_t t25 = GM_LD(uint32_t, (((uint8_t *)&gm_l6) + 4));
        uint32_t r26; bool o27 = __builtin_sub_overflow(t25, ((uint32_t)1ULL), &r26);
        if (o27) gm_trap("integer overflow in `-`", gm_srcue4ionbe9lgh, 61, 46);
        uint32_t t28 = r26;
        unsigned __int128 n29 = (unsigned __int128)t28;
        if (n29 >= 128) gm_trap_shift((uint64_t)n29, "i128", gm_srcue4ionbe9lgh, 61, 34);
        __int128 r30 = (__int128)((unsigned __int128)((__int128)(((unsigned __int128)0u << 64) | 1u)) << (int)n29);
        bool o31 = (__int128)(r30 >> (int)n29) != ((__int128)(((unsigned __int128)0u << 64) | 1u));
        if (o31) gm_trap("integer overflow in `<<`", gm_srcue4ionbe9lgh, 61, 34);
        __int128 t32 = r30;
        __int128 r33; bool o34 = __builtin_sub_overflow(t32, ((__int128)(((unsigned __int128)0u << 64) | 1u)), &r33);
        if (o34) gm_trap("integer overflow in `-`", gm_srcue4ionbe9lgh, 61, 33);
        __int128 t35 = r33;
        __int128 t36 = t35;
        t21 = t36;
        goto ix22;
ie22:;
        uint32_t t38 = GM_LD(uint32_t, (((uint8_t *)&gm_l6) + 4));
        unsigned __int128 n39 = (unsigned __int128)t38;
        if (n39 >= 128) gm_trap_shift((uint64_t)n39, "i128", gm_srcue4ionbe9lgh, 61, 76);
        __int128 r40 = (__int128)((unsigned __int128)((__int128)(((unsigned __int128)0u << 64) | 1u)) << (int)n39);
        bool o41 = (__int128)(r40 >> (int)n39) != ((__int128)(((unsigned __int128)0u << 64) | 1u));
        if (o41) gm_trap("integer overflow in `<<`", gm_srcue4ionbe9lgh, 61, 76);
        __int128 t42 = r40;
        __int128 r43; bool o44 = __builtin_sub_overflow(t42, ((__int128)(((unsigned __int128)0u << 64) | 1u)), &r43);
        if (o44) gm_trap("integer overflow in `-`", gm_srcue4ionbe9lgh, 61, 75);
        __int128 t45 = r43;
        __int128 t46 = t45;
        t21 = t46;
ix22:;
        gm_l8 = t21;
    }
    {
        __int128 t48 = gm_l1;
        __int128 t49 = gm_l7;
        uint8_t t50 = (t48 >= t49);
        if (!t50) goto ie47;
        __int128 t51 = gm_l1;
        __int128 t52 = gm_l8;
        uint8_t t53 = (t51 <= t52);
        if (!t53) goto ie47;
        {
            void * t55 = gm_l0;
            __int128 t56 = gm_l1;
            unsigned __int128 t57;
            memset(&t57, 0, sizeof t57); memcpy(&t57, ((uint8_t *)&t56), sizeof t57 < 16 ? sizeof t57 : 16);
            gm_t88rcucg1b7uk t58 = gm_l3;
            unsigned __int128 t59 = gm_f_wrap_bt8r149(t55, t57, GM_CONV(gm_csguankko4lmhd, t58));
            memset(&(*(gm_t8carr7mku484 *)gm_out), 0, sizeof (*(gm_t8carr7mku484 *)gm_out));
            ((uint8_t *)&(*(gm_t8carr7mku484 *)gm_out))[0] = 0;
            GM_ST(unsigned __int128, (uint8_t *)&(*(gm_t8carr7mku484 *)gm_out) + 16, t59);
            goto gm_fdone;
        }
        goto ix47;
ie47:;
ix47:;
    }
    {
        uint8_t t65 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        switch (t65) { case ((uint8_t)0ULL): goto ma63; case ((uint8_t)1ULL): goto ma61; case ((uint8_t)2ULL): goto ma62; case ((uint8_t)3ULL): goto ma63; default: goto ma63; }
ma61:;
        uint8_t t67 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t67 != ((uint8_t)1ULL)) goto mn66;
        void * t68 = gm_l0;
        __int128 t69 = gm_l1;
        unsigned __int128 t70;
        memset(&t70, 0, sizeof t70); memcpy(&t70, ((uint8_t *)&t69), sizeof t70 < 16 ? sizeof t70 : 16);
        gm_t88rcucg1b7uk t71 = gm_l3;
        unsigned __int128 t72 = gm_f_wrap_bt8r149(t68, t70, GM_CONV(gm_csguankko4lmhd, t71));
        memset(&(*(gm_t8carr7mku484 *)gm_out), 0, sizeof (*(gm_t8carr7mku484 *)gm_out));
        ((uint8_t *)&(*(gm_t8carr7mku484 *)gm_out))[0] = 0;
        GM_ST(unsigned __int128, (uint8_t *)&(*(gm_t8carr7mku484 *)gm_out) + 16, t72);
        goto gm_fdone;
        goto me60;
mn66:;
ma62:;
        uint8_t t74 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t74 != ((uint8_t)2ULL)) goto mn73;
        void * t75 = gm_l0;
        __int128 t76;
        __int128 t78 = gm_l1;
        uint8_t t79 = (t78 < ((__int128)(((unsigned __int128)0u << 64) | 0u)));
        if (!t79) goto ie77;
        __int128 t81 = gm_l7;
        __int128 t82 = t81;
        t76 = t82;
        goto ix77;
ie77:;
        __int128 t84 = gm_l8;
        __int128 t85 = t84;
        t76 = t85;
ix77:;
        unsigned __int128 t86;
        memset(&t86, 0, sizeof t86); memcpy(&t86, ((uint8_t *)&t76), sizeof t86 < 16 ? sizeof t86 : 16);
        gm_t88rcucg1b7uk t87 = gm_l3;
        unsigned __int128 t88 = gm_f_wrap_bt8r149(t75, t86, GM_CONV(gm_csguankko4lmhd, t87));
        memset(&(*(gm_t8carr7mku484 *)gm_out), 0, sizeof (*(gm_t8carr7mku484 *)gm_out));
        ((uint8_t *)&(*(gm_t8carr7mku484 *)gm_out))[0] = 0;
        GM_ST(unsigned __int128, (uint8_t *)&(*(gm_t8carr7mku484 *)gm_out) + 16, t88);
        goto gm_fdone;
        goto me60;
mn73:;
ma63:;
        void * t90 = gm_l0;
        gm_tl874t2atnlh3 t91 = gm_l5;
        gm_b16_8 t92 = gm_l4;
        gm_tnpifv20irnph t93;
        GM_ST(gm_b16_8, ((uint8_t *)&t93 + 0), t92);
        gm_tabb710d9mq2e t94;
        gm_f_format_eruojoe((void *)&t94, (void *)NULL, cx, NULL, t93);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t94))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t94) + 8, gm_srcue4ionbe9lgh, 66, 35);
        gm_b16_8 t95 = GM_LD(gm_b16_8, (((uint8_t *)&t94) + 8));
        gm_b16_8 t96 = GM_LD(gm_b16_8, ((uint8_t *)&t95));
        gm_t39uai1ajk3qv t97;
        gm_f_trap_jftbss6((void *)&t97, (void *)NULL, cx, NULL, t90, t91, t96);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t8carr7mku484 *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t8carr7mku484 *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t8carr7mku484 *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t8carr7mku484 *)gm_out) + 8, 0);
        ((uint8_t *)&(*(gm_t8carr7mku484 *)gm_out))[0] = 1;
        GM_ST(gm_t39uai1ajk3qv, (uint8_t *)&(*(gm_t8carr7mku484 *)gm_out) + 16, t97);
        goto gm_fdone;
        goto me60;
mn89:;
ma64:;
        gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 63, 5);
me60:;
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
inline __attribute__((aligned(4))) void gm_f_float_exact_1g17c6c(void *gm_out, void *env, gm_node *cx, gm_node *inst, double a0) {
    struct gm_fr_float_exact_1g17c6c F;
    double gm_l0; /* x */
    uint64_t gm_l1; /* raw */
    uint8_t gm_l2; /* negative */
    int64_t gm_l3; /* exp */
    uint64_t gm_l4; /* mant */
    int64_t gm_l5; /* e */
    GM_STACK_CHECK(&F, 791u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        double t3 = gm_l0;
        double t4 = gm_l0;
        double t5 = (t3 - t4);
        uint8_t t6 = (!(t5 == ((double)0e0)));
        if (!t6) goto ie2;
        {
            gm_tvegkb6ouf3m0 t8;
            memset(&t8, 0, sizeof t8);
            uint8_t t9 = 0;
            F.ret = t8;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        double t10 = gm_l0;
        uint64_t t11;
        memset(&t11, 0, sizeof t11); memcpy(&t11, ((uint8_t *)&t10), sizeof t11 < 8 ? sizeof t11 : 8);
        gm_l1 = t11;
    }
    {
        uint64_t t12 = gm_l1;
        unsigned __int128 n13 = (unsigned __int128)((uint64_t)63ULL);
        if (n13 >= 64) gm_trap_shift((uint64_t)n13, "u64", gm_srcue4ionbe9lgh, 307, 17);
        uint64_t t14 = (uint64_t)(t12 >> (int)n13);
        uint8_t t15 = ((t14 == ((uint64_t)1ULL)));
        gm_l2 = t15;
    }
    {
        uint64_t t16 = gm_l1;
        unsigned __int128 n17 = (unsigned __int128)((uint64_t)52ULL);
        if (n17 >= 64) gm_trap_shift((uint64_t)n17, "u64", gm_srcue4ionbe9lgh, 308, 14);
        uint64_t t18 = (uint64_t)(t16 >> (int)n17);
        uint64_t t19 = (uint64_t)(t18 & ((uint64_t)2047ULL));
        int64_t t20;
        memset(&t20, 0, sizeof t20); memcpy(&t20, ((uint8_t *)&t19), sizeof t20 < 8 ? sizeof t20 : 8);
        gm_l3 = t20;
    }
    {
        uint64_t t21 = gm_l1;
        uint64_t t22 = (uint64_t)(t21 & ((uint64_t)4503599627370495ULL));
        gm_l4 = t22;
    }
    {
        int64_t t24 = gm_l3;
        uint8_t t25 = ((t24 == ((int64_t)0LL)));
        if (!t25) goto ie23;
        {
            gm_l3 = ((int64_t)1LL);
        }
        goto ix23;
ie23:;
        {
            uint64_t t28 = gm_l4;
            unsigned __int128 n29 = (unsigned __int128)((uint64_t)52ULL);
            if (n29 >= 64) gm_trap_shift((uint64_t)n29, "u64", gm_srcue4ionbe9lgh, 313, 17);
            uint64_t r30 = (uint64_t)((uint64_t)((uint64_t)1ULL) << (int)n29);
            bool o31 = (uint64_t)(r30 >> (int)n29) != ((uint64_t)1ULL);
            if (o31) gm_trap("integer overflow in `<<`", gm_srcue4ionbe9lgh, 313, 17);
            uint64_t t32 = r30;
            uint64_t t33 = (uint64_t)(t28 | t32);
            gm_l4 = t33;
        }
ix23:;
    }
    {
        int64_t t34 = gm_l3;
        int64_t r35; bool o36 = __builtin_sub_overflow(t34, ((int64_t)1075LL), &r35);
        if (o36) gm_trap("integer overflow in `-`", gm_srcue4ionbe9lgh, 315, 10);
        int64_t t37 = r35;
        gm_l5 = t37;
    }
    {
        uint64_t t38 = gm_l4;
        gm_f_from_u64_ud13q5v((void *)&F.l6, (void *)NULL, cx, NULL, t38);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        uint8_t t40 = gm_l2;
        if (!t40) goto ie39;
        {
            gm_f_neg_agd5c5k((void *)&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l6)), (void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l6)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix39;
ie39:;
ix39:;
    }
    {
        int64_t t43 = gm_l5;
        uint8_t t44 = (t43 >= ((int64_t)0LL));
        if (!t44) goto ie42;
        {
            gm_tbci5ospk5nnn t46 = F.l6;
            int64_t t47 = gm_l5;
            uint64_t t48;
            memset(&t48, 0, sizeof t48); memcpy(&t48, ((uint8_t *)&t47), sizeof t48 < 8 ? sizeof t48 : 8);
            gm_tbci5ospk5nnn t49;
            gm_f_shl_qqecl81((void *)&t49, (void *)NULL, cx, NULL, &t46, t48);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tngtuiuskn938 t50;
            gm_f_from_int_ofjuoqh((void *)&t50, (void *)NULL, cx, NULL, &t49);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tvegkb6ouf3m0 *)gm_out) + 97, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tvegkb6ouf3m0 *)gm_out) + 98, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tvegkb6ouf3m0 *)gm_out) + 100, 0);
            ((uint8_t *)&(*(gm_tvegkb6ouf3m0 *)gm_out))[96] = 1;
            GM_ST(gm_tngtuiuskn938, (uint8_t *)&(*(gm_tvegkb6ouf3m0 *)gm_out) + 0, t50);
            goto gm_fdone;
        }
        goto ix42;
ie42:;
ix42:;
    }
    {
        gm_tbci5ospk5nnn t51 = F.l6;
        gm_tbci5ospk5nnn t52;
        gm_f_one_08qg1op((void *)&t52, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        int64_t t53 = gm_l5;
        if (t53 == ((int64_t)(-9223372036854775807LL - 1))) gm_trap("integer overflow in `-`", gm_srcue4ionbe9lgh, 319, 45);
        int64_t t54 = (int64_t)((uint64_t)0 - (uint64_t)t53);
        uint64_t t55;
        memset(&t55, 0, sizeof t55); memcpy(&t55, ((uint8_t *)&t54), sizeof t55 < 8 ? sizeof t55 : 8);
        gm_tbci5ospk5nnn t56;
        gm_f_shl_qqecl81((void *)&t56, (void *)NULL, cx, NULL, &t52, t55);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tngtuiuskn938 t57;
        gm_f_new_2jv6hag((void *)&t57, (void *)NULL, cx, NULL, &t51, &t56);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tvegkb6ouf3m0 *)gm_out) + 97, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tvegkb6ouf3m0 *)gm_out) + 98, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tvegkb6ouf3m0 *)gm_out) + 100, 0);
        ((uint8_t *)&(*(gm_tvegkb6ouf3m0 *)gm_out))[96] = 1;
        GM_ST(gm_tngtuiuskn938, (uint8_t *)&(*(gm_tvegkb6ouf3m0 *)gm_out) + 0, t57);
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
inline __attribute__((aligned(4))) double gm_f_float_rem_24loign(void *env, gm_node *cx, gm_node *inst, double a0, double a1) {
    struct gm_fr_float_rem_24loign F;
    double gm_l0; /* x */
    double gm_l1; /* y */
    GM_STACK_CHECK(&F, 998u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint8_t t3 = 0;
        uint8_t t4 = 0;
        double t5 = gm_l0;
        double t6 = gm_l0;
        double t7 = (t5 - t6);
        uint8_t t8 = (!(t7 == ((double)0e0)));
        if (!t8) {
            double t9 = gm_l1;
            uint8_t t10 = ((t9 == ((double)0e0)));
            t4 = t10;
        } else t4 = 1;
        if (!t4) {
            double t11 = gm_l1;
            double t12 = gm_l1;
            uint8_t t13 = (!(t11 == t12));
            t3 = t13;
        } else t3 = 1;
        if (!t3) goto ie2;
        {
            double t15 = (((double)0e0) / ((double)0e0));
            uint8_t t16 = 0;
            F.ret = t15;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        double t18 = gm_l1;
        double t19 = gm_l1;
        double t20 = (t18 - t19);
        uint8_t t21 = (!(t20 == ((double)0e0)));
        if (!t21) goto ie17;
        {
            double t23 = gm_l0;
            uint8_t t24 = 0;
            F.ret = t23;
            goto gm_fret;
        }
        goto ix17;
ie17:;
ix17:;
    }
    {
        double t25 = gm_l0;
        gm_tvegkb6ouf3m0 t26;
        gm_f_float_exact_1g17c6c((void *)&t26, (void *)NULL, cx, NULL, t25);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tngtuiuskn938 t27;
        if (((((uint8_t *)&t26))[96] != 0)) {
            t27 = (*(gm_tngtuiuskn938 *)(((uint8_t *)&t26) + 0));
        } else {
            double t28 = (((double)0e0) / ((double)0e0));
            uint8_t t29 = 0;
            F.ret = t28;
            goto gm_fret;
        }
        F.l2 = t27;
    }
    {
        double t30 = gm_l1;
        gm_tvegkb6ouf3m0 t31;
        gm_f_float_exact_1g17c6c((void *)&t31, (void *)NULL, cx, NULL, t30);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tngtuiuskn938 t32;
        if (((((uint8_t *)&t31))[96] != 0)) {
            t32 = (*(gm_tngtuiuskn938 *)(((uint8_t *)&t31) + 0));
        } else {
            double t33 = (((double)0e0) / ((double)0e0));
            uint8_t t34 = 0;
            F.ret = t33;
            goto gm_fret;
        }
        F.l3 = t32;
    }
    {
        gm_f_div_d8sd4qc((void *)&F.l4, (void *)NULL, cx, NULL, &(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l2)), &(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l3)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_f_div_f0qrfbr((void *)&F.l5, (void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)(((uint8_t *)&F.l4) + 0)), &(*(gm_tbci5ospk5nnn *)(((uint8_t *)&F.l4) + 48)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_tngtuiuskn938 t35 = F.l2;
        gm_tngtuiuskn938 t36 = F.l3;
        gm_tngtuiuskn938 t37;
        gm_f_from_int_ofjuoqh((void *)&t37, (void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l5)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tngtuiuskn938 t38;
        gm_f_mul_od1e7nd((void *)&t38, (void *)NULL, cx, NULL, &t36, &t37);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_sub_mler6q8((void *)&F.l6, (void *)NULL, cx, NULL, &t35, &t38);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        uint8_t t40 = gm_f_is_zero_5aept6u(&(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l6)));
        if (!t40) goto ie39;
        {
            double t42;
            double t44 = gm_l0;
            uint8_t t45 = (t44 < ((double)0e0));
            if (!t45) goto ie43;
            double t47 = -((double)0e0);
            double t48 = t47;
            t42 = t48;
            goto ix43;
ie43:;
            double t50 = ((double)0e0);
            t42 = t50;
ix43:;
            uint8_t t51 = 0;
            F.ret = t42;
            goto gm_fret;
        }
        goto ix39;
ie39:;
ix39:;
    }
    {
        double t52 = gm_f_to_f64_4avhrht((void *)NULL, cx, NULL, &(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l6)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t53 = 0;
        F.ret = t52;
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
__attribute__((aligned(4))) void gm_f_float_to_int_sf4dap7(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, double a1, gm_t88rcucg1b7uk a2, gm_tvdg94bk7aa22 a3) {
    struct gm_fr_float_to_int_sf4dap7 F;
    void * gm_l0; /* m */
    double gm_l1; /* x */
    gm_t88rcucg1b7uk gm_l2; /* t */
    gm_tvdg94bk7aa22 gm_l3; /* mode */
    uint8_t gm_l8; /* exact */
    int32_t gm_l10; /* c */
    GM_STACK_CHECK(&F, 1044u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        double t2 = gm_l1;
        gm_tvegkb6ouf3m0 t3;
        gm_f_float_exact_1g17c6c((void *)&t3, (void *)NULL, cx, NULL, t2);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tngtuiuskn938 t4;
        if (((((uint8_t *)&t3))[96] != 0)) {
            t4 = (*(gm_tngtuiuskn938 *)(((uint8_t *)&t3) + 0));
        } else {
            gm_thrdpodrs8pgt t5;
            memset(&t5, 0, sizeof t5);
            uint8_t t6 = 0;
            F.ret = t5;
            goto gm_fret;
        }
        F.l4 = t4;
    }
    {
        gm_f_div_floor_u3mfihk((void *)&F.l5, (void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)(((uint8_t *)&F.l4) + 0)), &(*(gm_tbci5ospk5nnn *)(((uint8_t *)&F.l4) + 48)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_tngtuiuskn938 t7 = F.l4;
        gm_tngtuiuskn938 t8;
        gm_f_from_int_ofjuoqh((void *)&t8, (void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l5)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_sub_mler6q8((void *)&F.l6, (void *)NULL, cx, NULL, &t7, &t8);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_tbci5ospk5nnn t9 = F.l5;
        F.l7 = t9;
    }
    {
        uint8_t t10 = gm_f_is_zero_5aept6u(&(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l6)));
        gm_l8 = t10;
    }
    {
        uint8_t t17 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
        switch (t17) { case ((uint8_t)0ULL): goto ma15; case ((uint8_t)1ULL): goto ma12; case ((uint8_t)2ULL): goto ma14; case ((uint8_t)3ULL): goto ma15; case ((uint8_t)4ULL): goto ma13; case ((uint8_t)5ULL): goto ma15; case ((uint8_t)6ULL): goto ma15; case ((uint8_t)7ULL): goto ma15; case ((uint8_t)8ULL): goto ma15; case ((uint8_t)9ULL): goto ma15; case ((uint8_t)10ULL): goto ma15; case ((uint8_t)11ULL): goto ma15; case ((uint8_t)12ULL): goto ma15; case ((uint8_t)13ULL): goto ma15; case ((uint8_t)14ULL): goto ma15; case ((uint8_t)15ULL): goto ma15; case ((uint8_t)16ULL): goto ma15; case ((uint8_t)17ULL): goto ma15; case ((uint8_t)18ULL): goto ma15; case ((uint8_t)19ULL): goto ma15; case ((uint8_t)20ULL): goto ma15; case ((uint8_t)21ULL): goto ma15; case ((uint8_t)22ULL): goto ma15; case ((uint8_t)23ULL): goto ma15; case ((uint8_t)24ULL): goto ma15; case ((uint8_t)25ULL): goto ma15; case ((uint8_t)26ULL): goto ma15; case ((uint8_t)27ULL): goto ma15; case ((uint8_t)28ULL): goto ma15; case ((uint8_t)29ULL): goto ma15; case ((uint8_t)30ULL): goto ma15; case ((uint8_t)31ULL): goto ma15; case ((uint8_t)32ULL): goto ma15; case ((uint8_t)33ULL): goto ma15; case ((uint8_t)34ULL): goto ma15; case ((uint8_t)35ULL): goto ma15; case ((uint8_t)36ULL): goto ma15; case ((uint8_t)37ULL): goto ma15; case ((uint8_t)38ULL): goto ma15; case ((uint8_t)39ULL): goto ma15; case ((uint8_t)40ULL): goto ma15; case ((uint8_t)41ULL): goto ma15; case ((uint8_t)42ULL): goto ma15; case ((uint8_t)43ULL): goto ma15; case ((uint8_t)44ULL): goto ma15; case ((uint8_t)45ULL): goto ma15; case ((uint8_t)46ULL): goto ma15; case ((uint8_t)47ULL): goto ma15; case ((uint8_t)48ULL): goto ma15; case ((uint8_t)49ULL): goto ma15; case ((uint8_t)50ULL): goto ma15; case ((uint8_t)51ULL): goto ma15; case ((uint8_t)52ULL): goto ma15; case ((uint8_t)53ULL): goto ma15; case ((uint8_t)54ULL): goto ma15; case ((uint8_t)55ULL): goto ma15; case ((uint8_t)56ULL): goto ma15; case ((uint8_t)57ULL): goto ma15; case ((uint8_t)58ULL): goto ma15; case ((uint8_t)59ULL): goto ma15; case ((uint8_t)60ULL): goto ma15; case ((uint8_t)61ULL): goto ma15; case ((uint8_t)62ULL): goto ma15; case ((uint8_t)63ULL): goto ma15; case ((uint8_t)64ULL): goto ma15; case ((uint8_t)65ULL): goto ma15; case ((uint8_t)66ULL): goto ma15; case ((uint8_t)67ULL): goto ma15; case ((uint8_t)68ULL): goto ma15; case ((uint8_t)69ULL): goto ma15; case ((uint8_t)70ULL): goto ma15; case ((uint8_t)71ULL): goto ma15; case ((uint8_t)72ULL): goto ma15; case ((uint8_t)73ULL): goto ma15; case ((uint8_t)74ULL): goto ma15; case ((uint8_t)75ULL): goto ma15; case ((uint8_t)76ULL): goto ma15; case ((uint8_t)77ULL): goto ma15; case ((uint8_t)78ULL): goto ma15; case ((uint8_t)79ULL): goto ma15; case ((uint8_t)80ULL): goto ma15; case ((uint8_t)81ULL): goto ma15; default: goto ma15; }
ma12:;
        uint8_t t19 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
        if (t19 != ((uint8_t)1ULL)) goto mn18;
        uint8_t t21 = gm_l8;
        uint8_t t22 = (uint8_t)!t21;
        if (!t22) goto ie20;
        uint8_t t23 = gm_f_is_negative_rb46l9j(&(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l4)));
        if (!t23) goto ie20;
        {
            gm_tbci5ospk5nnn t25 = F.l5;
            gm_tbci5ospk5nnn t26;
            gm_f_one_08qg1op((void *)&t26, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_f_add_420lj6o((void *)&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l7)), (void *)NULL, cx, NULL, &t25, &t26);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix20;
ie20:;
ix20:;
        goto me11;
mn18:;
ma13:;
        uint8_t t28 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
        if (t28 != ((uint8_t)4ULL)) goto mn27;
        uint8_t t30 = gm_l8;
        uint8_t t31 = (uint8_t)!t30;
        if (!t31) goto ie29;
        {
            gm_tbci5ospk5nnn t33 = F.l5;
            gm_tbci5ospk5nnn t34;
            gm_f_one_08qg1op((void *)&t34, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_f_add_420lj6o((void *)&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l7)), (void *)NULL, cx, NULL, &t33, &t34);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix29;
ie29:;
ix29:;
        goto me11;
mn27:;
ma14:;
        uint8_t t36 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
        if (t36 != ((uint8_t)2ULL)) goto mn35;
        {
            uint8_t t39 = gm_l8;
            uint8_t t40 = (uint8_t)!t39;
            if (!t40) goto ie38;
            {
                gm_tbci5ospk5nnn t42;
                gm_f_one_08qg1op((void *)&t42, (void *)NULL, cx, NULL);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tbci5ospk5nnn t43;
                gm_f_from_u64_ud13q5v((void *)&t43, (void *)NULL, cx, NULL, ((uint64_t)2ULL));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_f_new_2jv6hag((void *)&F.l9, (void *)NULL, cx, NULL, &t42, &t43);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                int32_t t44 = gm_f_cmp_9aseneh((void *)NULL, cx, NULL, &(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l6)), &(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l9)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l10 = t44;
            }
            {
                uint8_t t46 = 0;
                int32_t t47 = gm_l10;
                uint8_t t48 = (t47 > ((int32_t)0LL));
                if (!t48) {
                    uint8_t t49 = 0;
                    int32_t t50 = gm_l10;
                    uint8_t t51 = ((t50 == ((int32_t)0LL)));
                    if (t51) {
                        uint8_t t52 = gm_f_is_even_3kgcpjs(&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l5)));
                        uint8_t t53 = (uint8_t)!t52;
                        t49 = t53;
                    } else t49 = 0;
                    t46 = t49;
                } else t46 = 1;
                if (!t46) goto ie45;
                {
                    gm_tbci5ospk5nnn t55 = F.l5;
                    gm_tbci5ospk5nnn t56;
                    gm_f_one_08qg1op((void *)&t56, (void *)NULL, cx, NULL);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_f_add_420lj6o((void *)&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l7)), (void *)NULL, cx, NULL, &t55, &t56);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                goto ix45;
ie45:;
ix45:;
            }
            goto ix38;
ie38:;
ix38:;
        }
        goto me11;
mn35:;
ma15:;
        goto me11;
mn57:;
ma16:;
        gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 331, 5);
me11:;
    }
    {
        void * t59 = gm_l0;
        gm_t88rcucg1b7uk t60 = gm_l2;
        gm_f_int_range_hue3vms((void *)&F.l11, (void *)NULL, cx, NULL, t59, t60);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        uint8_t t62 = 0;
        int32_t t63 = gm_f_cmp_i1knrgu(&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l7)), &(*(gm_tbci5ospk5nnn *)(((uint8_t *)&F.l11) + 0)));
        uint8_t t64 = (t63 < ((int32_t)0LL));
        if (!t64) {
            int32_t t65 = gm_f_cmp_i1knrgu(&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l7)), &(*(gm_tbci5ospk5nnn *)(((uint8_t *)&F.l11) + 48)));
            uint8_t t66 = (t65 > ((int32_t)0LL));
            t62 = t66;
        } else t62 = 1;
        if (!t62) goto ie61;
        {
            gm_thrdpodrs8pgt t68;
            memset(&t68, 0, sizeof t68);
            uint8_t t69 = 0;
            F.ret = t68;
            goto gm_fret;
        }
        goto ix61;
ie61:;
ix61:;
    }
    {
        const uint8_t *t70 = ((uint8_t *)&F.l7);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_thrdpodrs8pgt *)gm_out) + 49, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_thrdpodrs8pgt *)gm_out) + 50, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_thrdpodrs8pgt *)gm_out) + 52, 0);
        ((uint8_t *)&(*(gm_thrdpodrs8pgt *)gm_out))[48] = 1;
        memmove((uint8_t *)&(*(gm_thrdpodrs8pgt *)gm_out) + 0, t70, 48);
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
__attribute__((aligned(4))) void gm_f_get_bytes_ql9o4k0(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, uint64_t a3) {
    struct gm_fr_get_bytes_ql9o4k0 F;
    void * gm_l0; /* m */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_b16_8 gm_l2; /* b */
    uint64_t gm_l3; /* at */
    void * gm_l4; /* s */
    uint64_t gm_l5; /* size */
    unsigned __int128 gm_l6; /* v */
    uint64_t gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    uint64_t gm_l9; /* i */
    uint16_t gm_l10; /* w */
    uint64_t gm_l11; /* n */
    uint64_t gm_l12; /* raw */
    uint64_t gm_l13; /* <tmp> */
    uint64_t gm_l14; /* <tmp> */
    uint64_t gm_l15; /* i */
    gm_t6j6smj7qoctu gm_l16; /* o */
    gm_b16_8 gm_l18; /* <tmp> */
    uint64_t gm_l19; /* <tmp> */
    uint64_t gm_l20; /* i */
    gm_t88rcucg1b7uk gm_l21; /* et */
    GM_STACK_CHECK(&F, 1728u);
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
        gm_l4 = t3;
    }
    {
        void * t4 = gm_l4;
        void * t5 = (void *)(((uint8_t *)t4) + 8);
        gm_t88rcucg1b7uk t6 = gm_l1;
        gm_t53at80pfdus3 t7;
        gm_f_kind_jcnt4hm((void *)&t7, t5, GM_CONV(gm_csguankko4lmhd, t6));
        uint8_t t13 = GM_LD(uint8_t, (((uint8_t *)&t7) + 0));
        switch (t13) { case ((uint8_t)0ULL): goto ma9; case ((uint8_t)1ULL): goto ma9; case ((uint8_t)2ULL): goto ma9; case ((uint8_t)3ULL): goto ma10; case ((uint8_t)4ULL): goto ma11; case ((uint8_t)5ULL): goto ma11; case ((uint8_t)6ULL): goto ma11; case ((uint8_t)7ULL): goto ma11; case ((uint8_t)8ULL): goto ma11; case ((uint8_t)9ULL): goto ma11; case ((uint8_t)10ULL): goto ma11; case ((uint8_t)11ULL): goto ma11; case ((uint8_t)12ULL): goto ma11; case ((uint8_t)13ULL): goto ma11; case ((uint8_t)14ULL): goto ma11; case ((uint8_t)15ULL): goto ma11; case ((uint8_t)16ULL): goto ma11; case ((uint8_t)17ULL): goto ma11; case ((uint8_t)18ULL): goto ma11; case ((uint8_t)19ULL): goto ma11; case ((uint8_t)20ULL): goto ma11; case ((uint8_t)21ULL): goto ma11; case ((uint8_t)22ULL): goto ma11; case ((uint8_t)23ULL): goto ma11; case ((uint8_t)24ULL): goto ma11; case ((uint8_t)25ULL): goto ma11; case ((uint8_t)26ULL): goto ma11; default: goto ma11; }
ma9:;
        uint8_t t17 = GM_LD(uint8_t, (((uint8_t *)&t7) + 0));
        if (t17 != ((uint8_t)0ULL)) goto pa16;
        goto po15;
pa16:;
        uint8_t t19 = GM_LD(uint8_t, (((uint8_t *)&t7) + 0));
        if (t19 != ((uint8_t)1ULL)) goto pa18;
        goto po15;
pa18:;
        uint8_t t21 = GM_LD(uint8_t, (((uint8_t *)&t7) + 0));
        if (t21 != ((uint8_t)2ULL)) goto pa20;
        goto po15;
pa20:;
        goto mn14;
po15:;
        {
            void * t23 = gm_l4;
            gm_t88rcucg1b7uk t24 = gm_l1;
            gm_tbccvikg6b96e t25;
            gm_f_layout_a5ovnrm((void *)&t25, (void *)NULL, cx, NULL, t23, t24);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint64_t t26 = GM_LD(uint64_t, (((uint8_t *)&t25) + 0));
            gm_l5 = t26;
        }
        {
            gm_l6 = ((unsigned __int128)(((unsigned __int128)0u << 64) | 0u));
        }
        {
            {
                gm_l8 = ((uint64_t)0ULL);
            }
            {
                uint64_t t28 = gm_l5;
                gm_l7 = t28;
            }
            {
                for (;;) {
                    uint64_t t30 = gm_l8;
                    uint64_t t31 = gm_l7;
                    uint8_t t32 = (t30 < t31);
                    if (!t32) goto lb29;
                    {
                        uint64_t t35 = gm_l8;
                        uint64_t t38 = GM_LD(uint64_t, ((uint8_t *)&t35));
                        gm_l9 = t38;
                        {
                            unsigned __int128 t40 = gm_l6;
                            gm_b16_8 t41 = gm_l2;
                            gm_b16_8 t42 = t41;
                            uint64_t t43 = gm_l3;
                            void * t44 = gm_l0;
                            uint64_t t45 = gm_l9;
                            uint64_t t46 = gm_l5;
                            uint64_t t47 = gm_f_byte_at_m94bj3e(t44, t45, t46);
                            uint64_t r48; bool o49 = __builtin_add_overflow(t43, t47, &r48);
                            if (o49) gm_trap("integer overflow in `+`", gm_srcue4ionbe9lgh, 883, 45);
                            uint64_t t50 = r48;
                            uint64_t t51 = (uint64_t)t50;
                            uint64_t t52 = (uintptr_t)t51;
                            uint64_t t53 = GM_LD(uintptr_t, ((uint8_t *)&t42) + 8);
                            if (t52 >= (uintptr_t)(t53)) gm_trap_index(t52, t53, gm_srcue4ionbe9lgh, 883, 42);
                            uint8_t t54 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&t42)) + t52 * 1));
                            unsigned __int128 t55 = (unsigned __int128)t54;
                            uint64_t t56 = gm_l9;
                            uint64_t r57; bool o58 = __builtin_mul_overflow(t56, ((uint64_t)8ULL), &r57);
                            if (o58) gm_trap("integer overflow in `*`", gm_srcue4ionbe9lgh, 883, 93);
                            uint64_t t59 = r57;
                            unsigned __int128 t60 = (unsigned __int128)t59;
                            unsigned __int128 n61 = (unsigned __int128)t60;
                            if (n61 >= 128) gm_trap_shift((uint64_t)n61, "u128", gm_srcue4ionbe9lgh, 883, 37);
                            unsigned __int128 r62 = (unsigned __int128)((unsigned __int128)t55 << (int)n61);
                            bool o63 = (unsigned __int128)(r62 >> (int)n61) != t55;
                            if (o63) gm_trap("integer overflow in `<<`", gm_srcue4ionbe9lgh, 883, 37);
                            unsigned __int128 t64 = r62;
                            unsigned __int128 t65 = (unsigned __int128)(t40 | t64);
                            gm_l6 = t65;
                        }
                        goto me36;
mn37:;
                        gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 883, 13);
me36:;
                    }
lc29:;
                    {
                        uint64_t t67 = gm_l8;
                        uint64_t r68; bool o69 = __builtin_add_overflow(t67, ((uint64_t)1ULL), &r68);
                        uint64_t t70 = r68;
                        gm_l8 = t70;
                    }
                }
lb29:;
            }
        }
        {
            void * t71 = gm_l0;
            unsigned __int128 t72 = gm_l6;
            gm_t88rcucg1b7uk t73 = gm_l1;
            unsigned __int128 t74 = gm_f_wrap_bt8r149(t71, t72, GM_CONV(gm_csguankko4lmhd, t73));
            gm_t6evimt84lndq t75;
            GM_ST(uint8_t, (uint8_t *)&t75 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t75 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t75 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t75 + 8, 0);
            GM_ST(uint64_t, (uint8_t *)&t75 + 32, 0);
            GM_ST(uint64_t, (uint8_t *)&t75 + 40, 0);
            GM_ST(uint8_t, ((uint8_t *)&t75 + 0), ((uint8_t)0ULL));
            GM_ST(unsigned __int128, ((uint8_t *)&t75 + 16), t74);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tjoi8o798ne9k *)gm_out) + 49, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tjoi8o798ne9k *)gm_out) + 50, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tjoi8o798ne9k *)gm_out) + 52, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tjoi8o798ne9k *)gm_out) + 56, 0);
            ((uint8_t *)&(*(gm_tjoi8o798ne9k *)gm_out))[48] = 1;
            GM_ST(gm_t6evimt84lndq, (uint8_t *)&(*(gm_tjoi8o798ne9k *)gm_out) + 0, t75);
            goto gm_fdone;
        }
        goto me8;
mn14:;
ma10:;
        uint8_t t77 = GM_LD(uint8_t, (((uint8_t *)&t7) + 0));
        if (t77 != ((uint8_t)3ULL)) goto mn76;
        uint16_t t78 = GM_LD(uint16_t, (((uint8_t *)&t7) + 8));
        gm_l10 = t78;
        {
            uint64_t t80;
            uint16_t t82 = gm_l10;
            uint8_t t83 = ((t82 == ((uint16_t)64ULL)));
            if (!t83) goto ie81;
            uint64_t t85 = ((uint64_t)8ULL);
            t80 = t85;
            goto ix81;
ie81:;
            uint64_t t86;
            uint16_t t88 = gm_l10;
            uint8_t t89 = ((t88 == ((uint16_t)32ULL)));
            if (!t89) goto ie87;
            uint64_t t91 = ((uint64_t)4ULL);
            t86 = t91;
            goto ix87;
ie87:;
            gm_tjoi8o798ne9k t93;
            memset(&t93, 0, sizeof t93);
            uint8_t t94 = 0;
            F.ret = t93;
            goto gm_fret;
            t86 = ((uint64_t)0);
ix87:;
            t80 = t86;
ix81:;
            gm_l11 = t80;
        }
        {
            gm_l12 = ((uint64_t)0ULL);
        }
        {
            {
                gm_l14 = ((uint64_t)0ULL);
            }
            {
                uint64_t t96 = gm_l11;
                gm_l13 = t96;
            }
            {
                for (;;) {
                    uint64_t t98 = gm_l14;
                    uint64_t t99 = gm_l13;
                    uint8_t t100 = (t98 < t99);
                    if (!t100) goto lb97;
                    {
                        uint64_t t103 = gm_l14;
                        uint64_t t106 = GM_LD(uint64_t, ((uint8_t *)&t103));
                        gm_l15 = t106;
                        {
                            uint64_t t108 = gm_l12;
                            gm_b16_8 t109 = gm_l2;
                            gm_b16_8 t110 = t109;
                            uint64_t t111 = gm_l3;
                            void * t112 = gm_l0;
                            uint64_t t113 = gm_l15;
                            uint64_t t114 = gm_l11;
                            uint64_t t115 = gm_f_byte_at_m94bj3e(t112, t113, t114);
                            uint64_t r116; bool o117 = __builtin_add_overflow(t111, t115, &r116);
                            if (o117) gm_trap("integer overflow in `+`", gm_srcue4ionbe9lgh, 889, 43);
                            uint64_t t118 = r116;
                            uint64_t t119 = (uint64_t)t118;
                            uint64_t t120 = (uintptr_t)t119;
                            uint64_t t121 = GM_LD(uintptr_t, ((uint8_t *)&t110) + 8);
                            if (t120 >= (uintptr_t)(t121)) gm_trap_index(t120, t121, gm_srcue4ionbe9lgh, 889, 40);
                            uint8_t t122 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&t110)) + t120 * 1));
                            uint64_t t123 = (uint64_t)t122;
                            uint64_t t124 = gm_l15;
                            uint64_t r125; bool o126 = __builtin_mul_overflow(t124, ((uint64_t)8ULL), &r125);
                            if (o126) gm_trap("integer overflow in `*`", gm_srcue4ionbe9lgh, 889, 84);
                            uint64_t t127 = r125;
                            unsigned __int128 n128 = (unsigned __int128)t127;
                            if (n128 >= 64) gm_trap_shift((uint64_t)n128, "u64", gm_srcue4ionbe9lgh, 889, 36);
                            uint64_t r129 = (uint64_t)((uint64_t)t123 << (int)n128);
                            bool o130 = (uint64_t)(r129 >> (int)n128) != t123;
                            if (o130) gm_trap("integer overflow in `<<`", gm_srcue4ionbe9lgh, 889, 36);
                            uint64_t t131 = r129;
                            uint64_t t132 = (uint64_t)(t108 | t131);
                            gm_l12 = t132;
                        }
                        goto me104;
mn105:;
                        gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 889, 13);
me104:;
                    }
lc97:;
                    {
                        uint64_t t134 = gm_l14;
                        uint64_t r135; bool o136 = __builtin_add_overflow(t134, ((uint64_t)1ULL), &r135);
                        uint64_t t137 = r135;
                        gm_l14 = t137;
                    }
                }
lb97:;
            }
        }
        {
            double t138;
            uint16_t t140 = gm_l10;
            uint8_t t141 = ((t140 == ((uint16_t)64ULL)));
            if (!t141) goto ie139;
            uint64_t t143 = gm_l12;
            double t144;
            memset(&t144, 0, sizeof t144); memcpy(&t144, ((uint8_t *)&t143), sizeof t144 < 8 ? sizeof t144 : 8);
            double t145 = t144;
            t138 = t145;
            goto ix139;
ie139:;
            uint64_t t147 = gm_l12;
            uint32_t t148 = (uint32_t)t147;
            float t149;
            memset(&t149, 0, sizeof t149); memcpy(&t149, ((uint8_t *)&t148), sizeof t149 < 4 ? sizeof t149 : 4);
            double t150 = (double)t149;
            double t151 = t150;
            t138 = t151;
ix139:;
            gm_t6evimt84lndq t152;
            memset(&t152, 0, sizeof t152);
            GM_ST(uint8_t, ((uint8_t *)&t152 + 0), ((uint8_t)1ULL));
            GM_ST(double, ((uint8_t *)&t152 + 16), t138);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tjoi8o798ne9k *)gm_out) + 49, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tjoi8o798ne9k *)gm_out) + 50, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tjoi8o798ne9k *)gm_out) + 52, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tjoi8o798ne9k *)gm_out) + 56, 0);
            ((uint8_t *)&(*(gm_tjoi8o798ne9k *)gm_out))[48] = 1;
            GM_ST(gm_t6evimt84lndq, (uint8_t *)&(*(gm_tjoi8o798ne9k *)gm_out) + 0, t152);
            goto gm_fdone;
        }
        goto me8;
mn76:;
ma11:;
        {
            void * t155 = gm_l0;
            gm_t88rcucg1b7uk t156 = gm_l1;
            gm_ttu09lij36f8c t157;
            gm_f_offsets_niooj98((void *)&t157, (void *)NULL, cx, NULL, t155, t156);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t6j6smj7qoctu t158;
            if (((((uint8_t *)&t157))[32] != 0)) {
                t158 = (*(gm_t6j6smj7qoctu *)(((uint8_t *)&t157) + 0));
            } else {
                gm_tjoi8o798ne9k t159;
                memset(&t159, 0, sizeof t159);
                uint8_t t160 = 0;
                F.ret = t159;
                goto gm_fret;
            }
            gm_l16 = t158;
        }
        {
            gm_f_list_0202sdh((void *)&F.l17, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            {
                gm_b16_8 t162 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l16) + 0));
                gm_l18 = t162;
            }
            {
                gm_l19 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t164 = gm_l19;
                    uint64_t t165 = GM_LD(uintptr_t, ((uint8_t *)&gm_l18) + 8);
                    uint8_t t166 = (t164 < t165);
                    if (!t166) goto lb163;
                    {
                        uint64_t t169 = gm_l19;
                        uint64_t t170 = gm_l19;
                        uint64_t t171 = (uintptr_t)t170;
                        uint64_t t172 = GM_LD(uintptr_t, ((uint8_t *)&gm_l18) + 8);
                        gm_t88rcucg1b7uk t173 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l18)) + t171 * 4));
                        gm_tlcm4lvu3g1a8 t174;
                        GM_ST(uint32_t, (uint8_t *)&t174 + 12, 0);
                        GM_ST(uint64_t, ((uint8_t *)&t174 + 0), t169);
                        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t174 + 8), t173);
                        uint64_t t177 = GM_LD(uint64_t, (((uint8_t *)&t174) + 0));
                        gm_l20 = t177;
                        gm_t88rcucg1b7uk t178 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t174) + 8));
                        gm_l21 = t178;
                        {
                            void * t180 = (void *)((uint8_t *)&F.l17);
                            void * t181 = gm_l0;
                            gm_t88rcucg1b7uk t182 = gm_l21;
                            gm_b16_8 t183 = gm_l2;
                            uint64_t t184 = gm_l3;
                            uint64_t t185 = gm_l20;
                            uint64_t t186 = (uintptr_t)t185;
                            uint64_t t187 = GM_LD(uintptr_t, (((uint8_t *)&gm_l16) + 16) + 8);
                            if (t186 >= (uintptr_t)(t187)) gm_trap_index(t186, t187, gm_srcue4ionbe9lgh, 895, 80);
                            uint64_t t188 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)&gm_l16) + 16)) + t186 * 8));
                            uint64_t r189; bool o190 = __builtin_add_overflow(t184, t188, &r189);
                            if (o190) gm_trap("integer overflow in `+`", gm_srcue4ionbe9lgh, 895, 75);
                            uint64_t t191 = r189;
                            gm_tjoi8o798ne9k t192;
                            gm_f_get_bytes_ql9o4k0((void *)&t192, (void *)NULL, cx, NULL, t181, t182, t183, t191);
                            if (gm_unw.kind) {
                                goto gm_ll167;
                            }
                            gm_t6evimt84lndq t193;
                            if (((((uint8_t *)&t192))[48] != 0)) {
                                t193 = (*(gm_t6evimt84lndq *)(((uint8_t *)&t192) + 0));
                            } else {
                                gm_tjoi8o798ne9k t194;
                                memset(&t194, 0, sizeof t194);
                                uint8_t t195 = 0;
                                F.ret = t194;
                                goto gm_fret;
                            }
                            gm_tftb1odc739t5 t196 = gm_f_push_0pgacsj(t180, &t193);
                            if (((((uint8_t *)&t196))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t196) + 1, gm_srcue4ionbe9lgh, 895, 45);
                            gm_b0_1 t197 = GM_LD(gm_b0_1, (((uint8_t *)&t196) + 1));
                        }
                        goto me175;
mn176:;
                        gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 895, 13);
me175:;
                    }
lc163:;
                    {
                        uint64_t t199 = gm_l19;
                        uint64_t r200; bool o201 = __builtin_add_overflow(t199, ((uint64_t)1ULL), &r200);
                        uint64_t t202 = r200;
                        gm_l19 = t202;
                    }
                }
gm_ll167:;
                if (gm_unw.frame == &F.h && gm_unw.label == 2) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb163; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc163; }
                }
                goto gm_lad;
lb163:;
            }
        }
        {
            gm_b16_8 t203 = GM_LD(gm_b16_8, (((uint8_t *)&F.l17) + 0));
            gm_tnpifv20irnph t204;
            GM_ST(gm_b16_8, ((uint8_t *)&t204 + 0), t203);
            gm_tq7s98ihonkju t205 = gm_f_new_lcff781((void *)NULL, cx, NULL, t204, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kdra5fvcfrb)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t205))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t205) + 8, gm_srcue4ionbe9lgh, 896, 25);
            void * t206 = GM_LD(void *, (((uint8_t *)&t205) + 8));
            gm_t6evimt84lndq t207;
            memset(&t207, 0, sizeof t207);
            GM_ST(uint8_t, ((uint8_t *)&t207 + 0), ((uint8_t)3ULL));
            GM_ST(void *, ((uint8_t *)&t207 + 16), t206);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tjoi8o798ne9k *)gm_out) + 49, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tjoi8o798ne9k *)gm_out) + 50, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tjoi8o798ne9k *)gm_out) + 52, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tjoi8o798ne9k *)gm_out) + 56, 0);
            ((uint8_t *)&(*(gm_tjoi8o798ne9k *)gm_out))[48] = 1;
            GM_ST(gm_t6evimt84lndq, (uint8_t *)&(*(gm_tjoi8o798ne9k *)gm_out) + 0, t207);
            goto gm_fdone;
        }
        goto me8;
mn153:;
ma12:;
        gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 879, 5);
me8:;
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
inline __attribute__((aligned(4))) gm_ttovhfee8mf28 gm_f_int_info_2r9qb5n(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_int_info_2r9qb5n F;
    void * gm_l0; /* m */
    gm_t88rcucg1b7uk gm_l1; /* t */
    uint8_t gm_l2; /* signed */
    uint16_t gm_l3; /* bits */
    GM_STACK_CHECK_SHALLOW(&F, 241u);
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
    gm_ttovhfee8mf28 t6;
    uint8_t t13 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
    switch (t13) { case ((uint8_t)0ULL): goto ma8; case ((uint8_t)1ULL): goto ma9; case ((uint8_t)2ULL): goto ma10; case ((uint8_t)3ULL): goto ma11; case ((uint8_t)4ULL): goto ma11; case ((uint8_t)5ULL): goto ma11; case ((uint8_t)6ULL): goto ma11; case ((uint8_t)7ULL): goto ma11; case ((uint8_t)8ULL): goto ma11; case ((uint8_t)9ULL): goto ma11; case ((uint8_t)10ULL): goto ma11; case ((uint8_t)11ULL): goto ma11; case ((uint8_t)12ULL): goto ma11; case ((uint8_t)13ULL): goto ma11; case ((uint8_t)14ULL): goto ma11; case ((uint8_t)15ULL): goto ma11; case ((uint8_t)16ULL): goto ma11; case ((uint8_t)17ULL): goto ma11; case ((uint8_t)18ULL): goto ma11; case ((uint8_t)19ULL): goto ma11; case ((uint8_t)20ULL): goto ma11; case ((uint8_t)21ULL): goto ma11; case ((uint8_t)22ULL): goto ma11; case ((uint8_t)23ULL): goto ma11; case ((uint8_t)24ULL): goto ma11; case ((uint8_t)25ULL): goto ma11; case ((uint8_t)26ULL): goto ma11; default: goto ma11; }
ma8:;
    uint8_t t15 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
    if (t15 != ((uint8_t)0ULL)) goto mn14;
    uint8_t t16 = GM_LD(uint8_t, ((((uint8_t *)&t5) + 8) + 0));
    gm_l2 = t16;
    uint16_t t17 = GM_LD(uint16_t, ((((uint8_t *)&t5) + 8) + 2));
    gm_l3 = t17;
    uint8_t t18 = gm_l2;
    uint16_t t19 = gm_l3;
    uint32_t t20 = (uint32_t)t19;
    gm_ttovhfee8mf28 t21;
    GM_ST(uint8_t, (uint8_t *)&t21 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t21 + 2, 0);
    GM_ST(uint8_t, ((uint8_t *)&t21 + 0), t18);
    GM_ST(uint32_t, ((uint8_t *)&t21 + 4), t20);
    t6 = t21;
    goto me7;
mn14:;
ma9:;
    uint8_t t23 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
    if (t23 != ((uint8_t)1ULL)) goto mn22;
    void * t24 = gm_l0;
    void * t25 = GM_LD(void *, (((uint8_t *)t24) + 0));
    void * t26 = GM_LD(void *, (((uint8_t *)t25) + 0));
    void * t27 = (void *)(((uint8_t *)t26) + 88);
    uint32_t t28 = gm_f_ptr_bits_glps5i7(t27);
    gm_ttovhfee8mf28 t29;
    GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
    GM_ST(uint8_t, ((uint8_t *)&t29 + 0), ((uint8_t)0));
    GM_ST(uint32_t, ((uint8_t *)&t29 + 4), t28);
    t6 = t29;
    goto me7;
mn22:;
ma10:;
    uint8_t t31 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
    if (t31 != ((uint8_t)2ULL)) goto mn30;
    void * t32 = gm_l0;
    void * t33 = GM_LD(void *, (((uint8_t *)t32) + 0));
    void * t34 = GM_LD(void *, (((uint8_t *)t33) + 0));
    void * t35 = (void *)(((uint8_t *)t34) + 88);
    uint32_t t36 = gm_f_ptr_bits_glps5i7(t35);
    gm_ttovhfee8mf28 t37;
    GM_ST(uint8_t, (uint8_t *)&t37 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t37 + 2, 0);
    GM_ST(uint8_t, ((uint8_t *)&t37 + 0), ((uint8_t)1));
    GM_ST(uint32_t, ((uint8_t *)&t37 + 4), t36);
    t6 = t37;
    goto me7;
mn30:;
ma11:;
    gm_ttovhfee8mf28 t39;
    GM_ST(uint8_t, (uint8_t *)&t39 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t39 + 2, 0);
    GM_ST(uint8_t, ((uint8_t *)&t39 + 0), ((uint8_t)0));
    GM_ST(uint32_t, ((uint8_t *)&t39 + 4), ((uint32_t)128ULL));
    t6 = t39;
    goto me7;
mn38:;
ma12:;
    gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 6, 74);
me7:;
    F.ret = t6;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) void gm_f_int_op_jnafd2a(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_ts6oqppjh0ba3 a1, unsigned __int128 a2, unsigned __int128 a3, gm_t88rcucg1b7uk a4, gm_t88rcucg1b7uk a5, gm_tl874t2atnlh3 a6) {
    struct gm_fr_int_op_jnafd2a F;
    void * gm_l0; /* m */
    gm_ts6oqppjh0ba3 gm_l1; /* op */
    unsigned __int128 gm_l2; /* x */
    unsigned __int128 gm_l3; /* y */
    gm_t88rcucg1b7uk gm_l4; /* lt */
    gm_t88rcucg1b7uk gm_l5; /* rt */
    gm_tl874t2atnlh3 gm_l6; /* span */
    gm_ttovhfee8mf28 gm_l7; /* info */
    gm_b16_8 gm_l8; /* sym */
    __int128 gm_l9; /* amount */
    uint32_t gm_l10; /* n */
    gm_t9qkh0rsooc8d gm_l11; /* flavor */
    uint8_t gm_l12; /* is_div */
    __int128 gm_l13; /* a */
    __int128 gm_l14; /* c */
    __int128 gm_l15; /* exact */
    gm_t9qkh0rsooc8d gm_l19; /* flavor */
    gm_t9qkh0rsooc8d gm_l20; /* flavor */
    gm_t9qkh0rsooc8d gm_l21; /* flavor */
    __int128 gm_l22; /* a */
    __int128 gm_l23; /* c */
    __int128 gm_l24; /* exact */
    GM_STACK_CHECK(&F, 3038u);
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
        gm_t88rcucg1b7uk t3 = gm_l4;
        gm_ttovhfee8mf28 t4 = gm_f_int_info_2r9qb5n(t2, GM_CONV(gm_csguankko4lmhd, t3));
        gm_l7 = t4;
    }
    {
        gm_ts6oqppjh0ba3 t5 = gm_l1;
        gm_b16_8 t6 = gm_f_binop_symbol_9ijp0eo(t5);
        gm_l8 = t6;
    }
    {
        uint8_t t13 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        switch (t13) { case ((uint8_t)0ULL): goto ma10; case ((uint8_t)1ULL): goto ma10; case ((uint8_t)2ULL): goto ma10; case ((uint8_t)3ULL): goto ma8; case ((uint8_t)4ULL): goto ma9; case ((uint8_t)5ULL): goto ma9; case ((uint8_t)6ULL): goto ma8; case ((uint8_t)7ULL): goto ma11; case ((uint8_t)8ULL): goto ma11; case ((uint8_t)9ULL): goto ma11; case ((uint8_t)10ULL): goto ma11; case ((uint8_t)11ULL): goto ma11; case ((uint8_t)12ULL): goto ma11; case ((uint8_t)13ULL): goto ma11; default: goto ma11; }
ma8:;
        uint8_t t17 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t17 != ((uint8_t)3ULL)) goto pa16;
        goto po15;
pa16:;
        uint8_t t19 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t19 != ((uint8_t)6ULL)) goto pa18;
        goto po15;
pa18:;
        goto mn14;
po15:;
        {
            void * t21 = gm_l0;
            unsigned __int128 t22 = gm_l3;
            gm_t88rcucg1b7uk t23 = gm_l5;
            __int128 t24 = gm_f_int_value_6hmh8tj(t21, t22, GM_CONV(gm_csguankko4lmhd, t23));
            gm_l9 = t24;
        }
        {
            uint8_t t26 = 0;
            __int128 t27 = gm_l9;
            uint8_t t28 = (t27 < ((__int128)(((unsigned __int128)0u << 64) | 0u)));
            if (!t28) {
                __int128 t29 = gm_l9;
                uint32_t t30 = GM_LD(uint32_t, (((uint8_t *)&gm_l7) + 4));
                __int128 t31 = (__int128)t30;
                uint8_t t32 = (t29 >= t31);
                t26 = t32;
            } else t26 = 1;
            if (!t26) goto ie25;
            {
                void * t34 = gm_l0;
                gm_tl874t2atnlh3 t35 = gm_l6;
                gm_t7a4aqr549lb5 t36;
                __int128 t37 = gm_l9;
                GM_ST(__int128, (uint8_t *)&t36 + 0, t37);
                void * t38 = gm_l0;
                void * t39 = GM_LD(void *, (((uint8_t *)t38) + 0));
                gm_t88rcucg1b7uk t40 = gm_l4;
                gm_b16_8 t41 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t39, t40);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(gm_b16_8, (uint8_t *)&t36 + 16, t41);
                gm_tabb710d9mq2e t42;
                gm_f_format_0fuesvo((void *)&t42, (void *)NULL, cx, NULL, &t36);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t42))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t42) + 8, gm_srcue4ionbe9lgh, 204, 79);
                gm_b16_8 t43 = GM_LD(gm_b16_8, (((uint8_t *)&t42) + 8));
                gm_b16_8 t44 = GM_LD(gm_b16_8, ((uint8_t *)&t43));
                gm_t39uai1ajk3qv t45;
                gm_f_trap_jftbss6((void *)&t45, (void *)NULL, cx, NULL, t34, t35, t44);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t8carr7mku484 *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_t8carr7mku484 *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_t8carr7mku484 *)gm_out) + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_t8carr7mku484 *)gm_out) + 8, 0);
                ((uint8_t *)&(*(gm_t8carr7mku484 *)gm_out))[0] = 1;
                GM_ST(gm_t39uai1ajk3qv, (uint8_t *)&(*(gm_t8carr7mku484 *)gm_out) + 16, t45);
                goto gm_fdone;
            }
            goto ix25;
ie25:;
ix25:;
        }
        {
            __int128 t46 = gm_l9;
            uint32_t t47 = (uint32_t)t46;
            gm_l10 = t47;
        }
        {
            uint8_t t49 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
            if (t49 != ((uint8_t)3ULL)) goto ie48;
            gm_t9qkh0rsooc8d t50 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l1) + 1));
            gm_l11 = t50;
            {
                uint32_t t53 = GM_LD(uint32_t, (((uint8_t *)&gm_l7) + 4));
                uint8_t t54 = (t53 <= ((uint32_t)63ULL));
                if (!t54) goto ie52;
                {
                    void * t56 = gm_l0;
                    void * t57 = gm_l0;
                    unsigned __int128 t58 = gm_l2;
                    gm_t88rcucg1b7uk t59 = gm_l4;
                    __int128 t60 = gm_f_int_value_6hmh8tj(t57, t58, GM_CONV(gm_csguankko4lmhd, t59));
                    uint32_t t61 = gm_l10;
                    unsigned __int128 n62 = (unsigned __int128)t61;
                    if (n62 >= 128) gm_trap_shift((uint64_t)n62, "i128", gm_srcue4ionbe9lgh, 207, 57);
                    __int128 r63 = (__int128)((unsigned __int128)t60 << (int)n62);
                    bool o64 = (__int128)(r63 >> (int)n62) != t60;
                    if (o64) gm_trap("integer overflow in `<<`", gm_srcue4ionbe9lgh, 207, 57);
                    __int128 t65 = r63;
                    gm_t9qkh0rsooc8d t66 = gm_l11;
                    gm_t88rcucg1b7uk t67 = gm_l4;
                    gm_b16_8 t68 = gm_l8;
                    gm_tl874t2atnlh3 t69 = gm_l6;
                    gm_f_fit_small_7o94i8v((void *)&(*(gm_t8carr7mku484 *)gm_out), (void *)NULL, cx, NULL, t56, t65, t66, t67, t68, t69);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_fdone;
                }
                goto ix52;
ie52:;
ix52:;
            }
            {
                void * t70 = gm_l0;
                void * t71 = gm_l0;
                unsigned __int128 t72 = gm_l2;
                gm_t88rcucg1b7uk t73 = gm_l4;
                gm_tbci5ospk5nnn t74;
                gm_f_big_of_1cq0qao((void *)&t74, (void *)NULL, cx, NULL, t71, t72, t73);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint32_t t75 = gm_l10;
                uint64_t t76 = (uint64_t)t75;
                gm_tbci5ospk5nnn t77;
                gm_f_shl_qqecl81((void *)&t77, (void *)NULL, cx, NULL, &t74, t76);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_t9qkh0rsooc8d t78 = gm_l11;
                gm_t88rcucg1b7uk t79 = gm_l4;
                gm_b16_8 t80 = gm_l8;
                gm_tl874t2atnlh3 t81 = gm_l6;
                gm_f_fit_8hvi4bv((void *)&(*(gm_t8carr7mku484 *)gm_out), (void *)NULL, cx, NULL, t70, &t77, t78, t79, t80, t81);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                goto gm_fdone;
            }
            goto ix48;
ie48:;
ix48:;
        }
        {
            uint8_t t83 = GM_LD(uint8_t, (((uint8_t *)&gm_l7) + 0));
            if (!t83) goto ie82;
            {
                void * t85 = gm_l0;
                void * t86 = gm_l0;
                unsigned __int128 t87 = gm_l2;
                gm_t88rcucg1b7uk t88 = gm_l4;
                __int128 t89 = gm_f_int_value_6hmh8tj(t86, t87, GM_CONV(gm_csguankko4lmhd, t88));
                uint32_t t90 = gm_l10;
                unsigned __int128 n91 = (unsigned __int128)t90;
                if (n91 >= 128) gm_trap_shift((uint64_t)n91, "i128", gm_srcue4ionbe9lgh, 210, 45);
                __int128 t92 = (__int128)(t89 >> (int)n91);
                unsigned __int128 t93;
                memset(&t93, 0, sizeof t93); memcpy(&t93, ((uint8_t *)&t92), sizeof t93 < 16 ? sizeof t93 : 16);
                gm_t88rcucg1b7uk t94 = gm_l4;
                unsigned __int128 t95 = gm_f_wrap_bt8r149(t85, t93, GM_CONV(gm_csguankko4lmhd, t94));
                memset(&(*(gm_t8carr7mku484 *)gm_out), 0, sizeof (*(gm_t8carr7mku484 *)gm_out));
                ((uint8_t *)&(*(gm_t8carr7mku484 *)gm_out))[0] = 0;
                GM_ST(unsigned __int128, (uint8_t *)&(*(gm_t8carr7mku484 *)gm_out) + 16, t95);
                goto gm_fdone;
            }
            goto ix82;
ie82:;
ix82:;
        }
        {
            unsigned __int128 t96 = gm_l2;
            uint32_t t97 = gm_l10;
            unsigned __int128 t98 = (unsigned __int128)t97;
            unsigned __int128 n99 = (unsigned __int128)t98;
            if (n99 >= 128) gm_trap_shift((uint64_t)n99, "u128", gm_srcue4ionbe9lgh, 211, 20);
            unsigned __int128 t100 = (unsigned __int128)(t96 >> (int)n99);
            memset(&(*(gm_t8carr7mku484 *)gm_out), 0, sizeof (*(gm_t8carr7mku484 *)gm_out));
            ((uint8_t *)&(*(gm_t8carr7mku484 *)gm_out))[0] = 0;
            GM_ST(unsigned __int128, (uint8_t *)&(*(gm_t8carr7mku484 *)gm_out) + 16, t100);
            goto gm_fdone;
        }
        goto me7;
mn14:;
ma9:;
        uint8_t t104 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t104 != ((uint8_t)4ULL)) goto pa103;
        goto po102;
pa103:;
        uint8_t t106 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t106 != ((uint8_t)5ULL)) goto pa105;
        goto po102;
pa105:;
        goto mn101;
po102:;
        {
            unsigned __int128 t109 = gm_l3;
            uint8_t t110 = ((t109 == ((unsigned __int128)(((unsigned __int128)0u << 64) | 0u))));
            if (!t110) goto ie108;
            {
                void * t112 = gm_l0;
                gm_tl874t2atnlh3 t113 = gm_l6;
                gm_b16_8 t114;
                GM_ST(void *, (uint8_t *)&t114, (void *)(gm_slf2kl43ogurh));
                GM_ST(uintptr_t, (uint8_t *)&t114 + 8, (uintptr_t)(16));
                gm_t39uai1ajk3qv t115;
                gm_f_trap_jftbss6((void *)&t115, (void *)NULL, cx, NULL, t112, t113, t114);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t8carr7mku484 *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_t8carr7mku484 *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_t8carr7mku484 *)gm_out) + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_t8carr7mku484 *)gm_out) + 8, 0);
                ((uint8_t *)&(*(gm_t8carr7mku484 *)gm_out))[0] = 1;
                GM_ST(gm_t39uai1ajk3qv, (uint8_t *)&(*(gm_t8carr7mku484 *)gm_out) + 16, t115);
                goto gm_fdone;
            }
            goto ix108;
ie108:;
ix108:;
        }
        {
            uint8_t t116 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l1) + 0) == ((uint8_t)4ULL)));
            gm_l12 = t116;
        }
        {
            uint32_t t118 = GM_LD(uint32_t, (((uint8_t *)&gm_l7) + 4));
            uint8_t t119 = (t118 <= ((uint32_t)63ULL));
            if (!t119) goto ie117;
            {
                void * t121 = gm_l0;
                unsigned __int128 t122 = gm_l2;
                gm_t88rcucg1b7uk t123 = gm_l4;
                __int128 t124 = gm_f_int_value_6hmh8tj(t121, t122, GM_CONV(gm_csguankko4lmhd, t123));
                gm_l13 = t124;
            }
            {
                void * t125 = gm_l0;
                unsigned __int128 t126 = gm_l3;
                gm_t88rcucg1b7uk t127 = gm_l5;
                __int128 t128 = gm_f_int_value_6hmh8tj(t125, t126, GM_CONV(gm_csguankko4lmhd, t127));
                gm_l14 = t128;
            }
            {
                __int128 t129;
                uint8_t t131 = gm_l12;
                if (!t131) goto ie130;
                __int128 t133 = gm_l13;
                __int128 t134 = gm_l14;
                if (t134 == 0) gm_trap("division by zero", gm_srcue4ionbe9lgh, 219, 38);
                if (t133 == ((__int128)(((unsigned __int128)9223372036854775808u << 64) | 0u)) && t134 == (__int128)-1) gm_trap("integer overflow in `/`", gm_srcue4ionbe9lgh, 219, 38);
                __int128 t135 = (__int128)(t133 / t134);
                __int128 t136 = t135;
                t129 = t136;
                goto ix130;
ie130:;
                __int128 t138 = gm_l13;
                __int128 t139 = gm_l14;
                if (t139 == 0) gm_trap("division by zero", gm_srcue4ionbe9lgh, 219, 53);
                if (t138 == ((__int128)(((unsigned __int128)9223372036854775808u << 64) | 0u)) && t139 == (__int128)-1) gm_trap("integer overflow in `%`", gm_srcue4ionbe9lgh, 219, 53);
                __int128 t140 = (__int128)(t138 % t139);
                __int128 t141 = t140;
                t129 = t141;
ix130:;
                gm_l15 = t129;
            }
            {
                void * t142 = gm_l0;
                __int128 t143 = gm_l15;
                gm_t9qkh0rsooc8d t144;
                GM_ST(uint8_t, ((uint8_t *)&t144 + 0), ((uint8_t)0ULL));
                gm_t88rcucg1b7uk t145 = gm_l4;
                gm_b16_8 t146 = gm_l8;
                gm_tl874t2atnlh3 t147 = gm_l6;
                gm_f_fit_small_7o94i8v((void *)&(*(gm_t8carr7mku484 *)gm_out), (void *)NULL, cx, NULL, t142, t143, t144, t145, t146, t147);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                goto gm_fdone;
            }
            goto ix117;
ie117:;
ix117:;
        }
        {
            void * t148 = gm_l0;
            unsigned __int128 t149 = gm_l2;
            gm_t88rcucg1b7uk t150 = gm_l4;
            gm_f_big_of_1cq0qao((void *)&F.l16, (void *)NULL, cx, NULL, t148, t149, t150);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t151 = gm_l0;
            unsigned __int128 t152 = gm_l3;
            gm_t88rcucg1b7uk t153 = gm_l5;
            gm_f_big_of_1cq0qao((void *)&F.l17, (void *)NULL, cx, NULL, t151, t152, t153);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            uint8_t t155 = GM_LD(uint8_t, (((uint8_t *)&gm_l7) + 0));
            if (!t155) goto ie154;
            uint8_t t156 = gm_l12;
            uint8_t t157 = (uint8_t)!t156;
            if (!t157) goto ie154;
            gm_tbci5ospk5nnn t158 = F.l17;
            gm_tbci5ospk5nnn t159;
            gm_f_one_08qg1op((void *)&t159, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tbci5ospk5nnn t160;
            gm_f_neg_agd5c5k((void *)&t160, (void *)NULL, cx, NULL, &t159);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t161 = gm_f_eq_60fjn21(&t158, &t160);
            if (!t161) goto ie154;
            gm_tbci5ospk5nnn t162 = F.l16;
            void * t163 = gm_l0;
            gm_t88rcucg1b7uk t164 = gm_l4;
            gm_tngtuiuskn938 t165;
            gm_f_int_range_hue3vms((void *)&t165, (void *)NULL, cx, NULL, t163, t164);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t166 = gm_f_eq_60fjn21(&t162, &(*(gm_tbci5ospk5nnn *)(((uint8_t *)&t165) + 0)));
            if (!t166) goto ie154;
            {
                void * t168 = gm_l0;
                gm_tl874t2atnlh3 t169 = gm_l6;
                gm_b16_8 t170;
                GM_ST(void *, (uint8_t *)&t170, (void *)(gm_s3385032lmmqq));
                GM_ST(uintptr_t, (uint8_t *)&t170 + 8, (uintptr_t)(23));
                gm_t39uai1ajk3qv t171;
                gm_f_trap_jftbss6((void *)&t171, (void *)NULL, cx, NULL, t168, t169, t170);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t8carr7mku484 *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_t8carr7mku484 *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_t8carr7mku484 *)gm_out) + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_t8carr7mku484 *)gm_out) + 8, 0);
                ((uint8_t *)&(*(gm_t8carr7mku484 *)gm_out))[0] = 1;
                GM_ST(gm_t39uai1ajk3qv, (uint8_t *)&(*(gm_t8carr7mku484 *)gm_out) + 16, t171);
                goto gm_fdone;
            }
            goto ix154;
ie154:;
ix154:;
        }
        {
            gm_tbci5ospk5nnn t172;
            uint8_t t174 = gm_l12;
            if (!t174) goto ie173;
            gm_tbci5ospk5nnn t176;
            gm_f_div_f0qrfbr((void *)&t176, (void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l16)), &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l17)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tbci5ospk5nnn t177 = t176;
            t172 = t177;
            goto ix173;
ie173:;
            gm_tbci5ospk5nnn t179;
            gm_f_rem_ib4l1vl((void *)&t179, (void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l16)), &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l17)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tbci5ospk5nnn t180 = t179;
            t172 = t180;
ix173:;
            F.l18 = t172;
        }
        {
            void * t181 = gm_l0;
            gm_t9qkh0rsooc8d t182;
            GM_ST(uint8_t, ((uint8_t *)&t182 + 0), ((uint8_t)0ULL));
            gm_t88rcucg1b7uk t183 = gm_l4;
            gm_b16_8 t184 = gm_l8;
            gm_tl874t2atnlh3 t185 = gm_l6;
            gm_f_fit_8hvi4bv((void *)&(*(gm_t8carr7mku484 *)gm_out), (void *)NULL, cx, NULL, t181, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l18)), t182, t183, t184, t185);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
        }
        goto me7;
mn101:;
ma10:;
        uint8_t t189 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t189 != ((uint8_t)0ULL)) goto pa188;
        gm_t9qkh0rsooc8d t190 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l1) + 1));
        gm_l19 = t190;
        goto po187;
pa188:;
        uint8_t t192 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t192 != ((uint8_t)1ULL)) goto pa191;
        gm_t9qkh0rsooc8d t193 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l1) + 1));
        gm_l19 = t193;
        goto po187;
pa191:;
        uint8_t t195 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t195 != ((uint8_t)2ULL)) goto pa194;
        gm_t9qkh0rsooc8d t196 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l1) + 1));
        gm_l19 = t196;
        goto po187;
pa194:;
        goto mn186;
po187:;
        {
            uint32_t t199 = GM_LD(uint32_t, (((uint8_t *)&gm_l7) + 4));
            uint8_t t200 = (t199 <= ((uint32_t)63ULL));
            if (!t200) goto ie198;
            {
                void * t202 = gm_l0;
                unsigned __int128 t203 = gm_l2;
                gm_t88rcucg1b7uk t204 = gm_l4;
                __int128 t205 = gm_f_int_value_6hmh8tj(t202, t203, GM_CONV(gm_csguankko4lmhd, t204));
                gm_l22 = t205;
            }
            {
                void * t206 = gm_l0;
                unsigned __int128 t207 = gm_l3;
                gm_t88rcucg1b7uk t208 = gm_l5;
                __int128 t209 = gm_f_int_value_6hmh8tj(t206, t207, GM_CONV(gm_csguankko4lmhd, t208));
                gm_l23 = t209;
            }
            {
                __int128 t210;
                uint8_t t216 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                switch (t216) { case ((uint8_t)0ULL): goto ma212; case ((uint8_t)1ULL): goto ma213; case ((uint8_t)2ULL): goto ma214; case ((uint8_t)3ULL): goto ma214; case ((uint8_t)4ULL): goto ma214; case ((uint8_t)5ULL): goto ma214; case ((uint8_t)6ULL): goto ma214; case ((uint8_t)7ULL): goto ma214; case ((uint8_t)8ULL): goto ma214; case ((uint8_t)9ULL): goto ma214; case ((uint8_t)10ULL): goto ma214; case ((uint8_t)11ULL): goto ma214; case ((uint8_t)12ULL): goto ma214; case ((uint8_t)13ULL): goto ma214; default: goto ma214; }
ma212:;
                uint8_t t218 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                if (t218 != ((uint8_t)0ULL)) goto mn217;
                __int128 t219 = gm_l22;
                __int128 t220 = gm_l23;
                __int128 r221; bool o222 = __builtin_add_overflow(t219, t220, &r221);
                if (o222) gm_trap("integer overflow in `+`", gm_srcue4ionbe9lgh, 233, 32);
                __int128 t223 = r221;
                t210 = t223;
                goto me211;
mn217:;
ma213:;
                uint8_t t225 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                if (t225 != ((uint8_t)1ULL)) goto mn224;
                __int128 t226 = gm_l22;
                __int128 t227 = gm_l23;
                __int128 r228; bool o229 = __builtin_sub_overflow(t226, t227, &r228);
                if (o229) gm_trap("integer overflow in `-`", gm_srcue4ionbe9lgh, 234, 32);
                __int128 t230 = r228;
                t210 = t230;
                goto me211;
mn224:;
ma214:;
                __int128 t232 = gm_l22;
                __int128 t233 = gm_l23;
                __int128 r234; bool o235 = __builtin_mul_overflow(t232, t233, &r234);
                if (o235) gm_trap("integer overflow in `*`", gm_srcue4ionbe9lgh, 235, 26);
                __int128 t236 = r234;
                t210 = t236;
                goto me211;
mn231:;
ma215:;
                gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 232, 26);
me211:;
                gm_l24 = t210;
            }
            {
                void * t237 = gm_l0;
                __int128 t238 = gm_l24;
                gm_t9qkh0rsooc8d t239 = gm_l19;
                gm_t88rcucg1b7uk t240 = gm_l4;
                gm_b16_8 t241 = gm_l8;
                gm_tl874t2atnlh3 t242 = gm_l6;
                gm_f_fit_small_7o94i8v((void *)&(*(gm_t8carr7mku484 *)gm_out), (void *)NULL, cx, NULL, t237, t238, t239, t240, t241, t242);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                goto gm_fdone;
            }
            goto ix198;
ie198:;
ix198:;
        }
        {
            void * t243 = gm_l0;
            unsigned __int128 t244 = gm_l2;
            gm_t88rcucg1b7uk t245 = gm_l4;
            gm_f_big_of_1cq0qao((void *)&F.l25, (void *)NULL, cx, NULL, t243, t244, t245);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t246 = gm_l0;
            unsigned __int128 t247 = gm_l3;
            gm_t88rcucg1b7uk t248 = gm_l5;
            gm_f_big_of_1cq0qao((void *)&F.l26, (void *)NULL, cx, NULL, t246, t247, t248);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_tbci5ospk5nnn t249;
            uint8_t t255 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
            switch (t255) { case ((uint8_t)0ULL): goto ma251; case ((uint8_t)1ULL): goto ma252; case ((uint8_t)2ULL): goto ma253; case ((uint8_t)3ULL): goto ma253; case ((uint8_t)4ULL): goto ma253; case ((uint8_t)5ULL): goto ma253; case ((uint8_t)6ULL): goto ma253; case ((uint8_t)7ULL): goto ma253; case ((uint8_t)8ULL): goto ma253; case ((uint8_t)9ULL): goto ma253; case ((uint8_t)10ULL): goto ma253; case ((uint8_t)11ULL): goto ma253; case ((uint8_t)12ULL): goto ma253; case ((uint8_t)13ULL): goto ma253; default: goto ma253; }
ma251:;
            uint8_t t257 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
            if (t257 != ((uint8_t)0ULL)) goto mn256;
            gm_tbci5ospk5nnn t258;
            gm_f_add_420lj6o((void *)&t258, (void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l25)), &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l26)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            t249 = t258;
            goto me250;
mn256:;
ma252:;
            uint8_t t260 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
            if (t260 != ((uint8_t)1ULL)) goto mn259;
            gm_tbci5ospk5nnn t261;
            gm_f_sub_pktm5cr((void *)&t261, (void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l25)), &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l26)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            t249 = t261;
            goto me250;
mn259:;
ma253:;
            gm_tbci5ospk5nnn t263;
            gm_f_mul_nfj5rjv((void *)&t263, (void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l25)), &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l26)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            t249 = t263;
            goto me250;
mn262:;
ma254:;
            gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 241, 22);
me250:;
            F.l27 = t249;
        }
        {
            void * t264 = gm_l0;
            gm_t9qkh0rsooc8d t265 = gm_l19;
            gm_t88rcucg1b7uk t266 = gm_l4;
            gm_b16_8 t267 = gm_l8;
            gm_tl874t2atnlh3 t268 = gm_l6;
            gm_f_fit_8hvi4bv((void *)&(*(gm_t8carr7mku484 *)gm_out), (void *)NULL, cx, NULL, t264, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l27)), t265, t266, t267, t268);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
        }
        goto me7;
mn186:;
ma11:;
        void * t270 = gm_l0;
        gm_tl874t2atnlh3 t271 = gm_l6;
        gm_b16_8 t272;
        GM_ST(void *, (uint8_t *)&t272, (void *)(gm_sqe4aodms9tfs));
        GM_ST(uintptr_t, (uint8_t *)&t272 + 8, (uintptr_t)(13));
        gm_t39uai1ajk3qv t273;
        gm_f_unsupported_2seq49l((void *)&t273, t270, GM_CONV(gm_cseahvg8v7p0vs, t271), t272);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t8carr7mku484 *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t8carr7mku484 *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t8carr7mku484 *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t8carr7mku484 *)gm_out) + 8, 0);
        ((uint8_t *)&(*(gm_t8carr7mku484 *)gm_out))[0] = 1;
        GM_ST(gm_t39uai1ajk3qv, (uint8_t *)&(*(gm_t8carr7mku484 *)gm_out) + 16, t273);
        goto gm_fdone;
        goto me7;
mn269:;
ma12:;
        gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 201, 5);
me7:;
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
inline __attribute__((aligned(4))) void gm_f_int_range_hue3vms(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_int_range_hue3vms F;
    void * gm_l0; /* m */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_ttovhfee8mf28 gm_l2; /* info */
    GM_STACK_CHECK(&F, 549u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        gm_t88rcucg1b7uk t3 = gm_l1;
        gm_ttovhfee8mf28 t4 = gm_f_int_info_2r9qb5n(t2, GM_CONV(gm_csguankko4lmhd, t3));
        gm_l2 = t4;
    }
    {
        uint8_t t6 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (!t6) goto ie5;
        {
            gm_tbci5ospk5nnn t8;
            gm_f_one_08qg1op((void *)&t8, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint32_t t9 = GM_LD(uint32_t, (((uint8_t *)&gm_l2) + 4));
            uint32_t r10; bool o11 = __builtin_sub_overflow(t9, ((uint32_t)1ULL), &r10);
            if (o11) gm_trap("integer overflow in `-`", gm_srcue4ionbe9lgh, 39, 38);
            uint32_t t12 = r10;
            uint64_t t13 = (uint64_t)t12;
            gm_f_shl_qqecl81((void *)&F.l3, (void *)NULL, cx, NULL, &t8, t13);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_tbci5ospk5nnn t14;
            gm_f_neg_agd5c5k((void *)&t14, (void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l3)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tbci5ospk5nnn t15 = F.l3;
            gm_tbci5ospk5nnn t16;
            gm_f_one_08qg1op((void *)&t16, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tbci5ospk5nnn t17;
            gm_f_sub_pktm5cr((void *)&t17, (void *)NULL, cx, NULL, &t15, &t16);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(gm_tbci5ospk5nnn, (uint8_t *)&(*(gm_tngtuiuskn938 *)gm_out) + 0, t14);
            GM_ST(gm_tbci5ospk5nnn, (uint8_t *)&(*(gm_tngtuiuskn938 *)gm_out) + 48, t17);
            goto gm_fdone;
        }
        goto ix5;
ie5:;
ix5:;
    }
    {
        gm_tbci5ospk5nnn t18;
        gm_f_zero_s25l990((void *)&t18, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tbci5ospk5nnn t19;
        gm_f_one_08qg1op((void *)&t19, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint32_t t20 = GM_LD(uint32_t, (((uint8_t *)&gm_l2) + 4));
        uint64_t t21 = (uint64_t)t20;
        gm_tbci5ospk5nnn t22;
        gm_f_shl_qqecl81((void *)&t22, (void *)NULL, cx, NULL, &t19, t21);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tbci5ospk5nnn t23;
        gm_f_one_08qg1op((void *)&t23, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tbci5ospk5nnn t24;
        gm_f_sub_pktm5cr((void *)&t24, (void *)NULL, cx, NULL, &t22, &t23);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_tbci5ospk5nnn, (uint8_t *)&(*(gm_tngtuiuskn938 *)gm_out) + 0, t18);
        GM_ST(gm_tbci5ospk5nnn, (uint8_t *)&(*(gm_tngtuiuskn938 *)gm_out) + 48, t24);
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
inline __attribute__((aligned(4))) __int128 gm_f_int_value_6hmh8tj(void * a0, unsigned __int128 a1, gm_csguankko4lmhd a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_int_value_6hmh8tj F;
    void * gm_l0; /* m */
    unsigned __int128 gm_l1; /* bits */
    gm_t88rcucg1b7uk gm_l2; /* t */
    gm_ttovhfee8mf28 gm_l3; /* info */
    GM_STACK_CHECK_SHALLOW(&F, 285u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = GM_CONV(gm_t88rcucg1b7uk, a2);
    {
        void * t2 = gm_l0;
        gm_t88rcucg1b7uk t3 = gm_l2;
        gm_ttovhfee8mf28 t4 = gm_f_int_info_2r9qb5n(t2, GM_CONV(gm_csguankko4lmhd, t3));
        gm_l3 = t4;
    }
    {
        uint8_t t6 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
        if (!t6) goto ie5;
        uint32_t t7 = GM_LD(uint32_t, (((uint8_t *)&gm_l3) + 4));
        uint8_t t8 = (t7 < ((uint32_t)128ULL));
        if (!t8) goto ie5;
        unsigned __int128 t9 = gm_l1;
        uint32_t t10 = GM_LD(uint32_t, (((uint8_t *)&gm_l3) + 4));
        uint32_t r11; bool o12 = __builtin_sub_overflow(t10, ((uint32_t)1ULL), &r11);
        if (o12) gm_trap("integer overflow in `-`", gm_srcue4ionbe9lgh, 23, 58);
        uint32_t t13 = r11;
        unsigned __int128 t14 = (unsigned __int128)t13;
        unsigned __int128 n15 = (unsigned __int128)t14;
        if (n15 >= 128) gm_trap_shift((uint64_t)n15, "u128", gm_srcue4ionbe9lgh, 23, 45);
        unsigned __int128 t16 = (unsigned __int128)(t9 >> (int)n15);
        unsigned __int128 t17 = (unsigned __int128)(t16 & ((unsigned __int128)(((unsigned __int128)0u << 64) | 1u)));
        uint8_t t18 = ((t17 == ((unsigned __int128)(((unsigned __int128)0u << 64) | 1u))));
        if (!t18) goto ie5;
        {
            unsigned __int128 t20 = gm_l1;
            uint32_t t21 = GM_LD(uint32_t, (((uint8_t *)&gm_l3) + 4));
            unsigned __int128 t22 = gm_f_mask_gbhl4hp(t21);
            unsigned __int128 t23 = (unsigned __int128)~t22;
            unsigned __int128 t24 = (unsigned __int128)(t20 | t23);
            __int128 t25;
            memset(&t25, 0, sizeof t25); memcpy(&t25, ((uint8_t *)&t24), sizeof t25 < 16 ? sizeof t25 : 16);
            uint8_t t26 = 0;
            F.ret = t25;
            goto gm_fret;
        }
        goto ix5;
ie5:;
ix5:;
    }
    {
        unsigned __int128 t27 = gm_l1;
        __int128 t28;
        memset(&t28, 0, sizeof t28); memcpy(&t28, ((uint8_t *)&t27), sizeof t28 < 16 ? sizeof t28 : 16);
        uint8_t t29 = 0;
        F.ret = t28;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) unsigned __int128 gm_f_mask_gbhl4hp(uint32_t a0);
__attribute__((aligned(4))) void gm_f_offsets_niooj98(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_offsets_niooj98 F;
    void * gm_l0; /* m */
    gm_t88rcucg1b7uk gm_l1; /* t */
    void * gm_l2; /* s */
    uint64_t gm_l3; /* len */
    gm_t88rcucg1b7uk gm_l4; /* elem */
    uint64_t gm_l5; /* size */
    uint64_t gm_l8; /* <tmp> */
    uint64_t gm_l9; /* <tmp> */
    uint64_t gm_l10; /* i */
    gm_b16_8 gm_l11; /* elems */
    uint32_t gm_l12; /* id */
    gm_b16_8 gm_l15; /* <tmp> */
    uint64_t gm_l16; /* <tmp> */
    GM_STACK_CHECK(&F, 1057u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 0));
        gm_l2 = t3;
    }
    {
        void * t4 = gm_l2;
        void * t5 = (void *)(((uint8_t *)t4) + 8);
        gm_t88rcucg1b7uk t6 = gm_l1;
        gm_t53at80pfdus3 t7;
        gm_f_kind_jcnt4hm((void *)&t7, t5, GM_CONV(gm_csguankko4lmhd, t6));
        uint8_t t14 = GM_LD(uint8_t, (((uint8_t *)&t7) + 0));
        switch (t14) { case ((uint8_t)0ULL): goto ma12; case ((uint8_t)1ULL): goto ma12; case ((uint8_t)2ULL): goto ma12; case ((uint8_t)3ULL): goto ma12; case ((uint8_t)4ULL): goto ma12; case ((uint8_t)5ULL): goto ma12; case ((uint8_t)6ULL): goto ma12; case ((uint8_t)7ULL): goto ma12; case ((uint8_t)8ULL): goto ma12; case ((uint8_t)9ULL): goto ma12; case ((uint8_t)10ULL): goto ma12; case ((uint8_t)11ULL): goto ma9; case ((uint8_t)12ULL): goto ma10; case ((uint8_t)13ULL): goto ma12; case ((uint8_t)14ULL): goto ma12; case ((uint8_t)15ULL): goto ma12; case ((uint8_t)16ULL): goto ma12; case ((uint8_t)17ULL): goto ma11; case ((uint8_t)18ULL): goto ma12; case ((uint8_t)19ULL): goto ma12; case ((uint8_t)20ULL): goto ma12; case ((uint8_t)21ULL): goto ma12; case ((uint8_t)22ULL): goto ma12; case ((uint8_t)23ULL): goto ma12; case ((uint8_t)24ULL): goto ma12; case ((uint8_t)25ULL): goto ma12; case ((uint8_t)26ULL): goto ma12; default: goto ma12; }
ma9:;
        uint8_t t16 = GM_LD(uint8_t, (((uint8_t *)&t7) + 0));
        if (t16 != ((uint8_t)11ULL)) goto mn15;
        uint64_t t17 = GM_LD(uint64_t, ((((uint8_t *)&t7) + 8) + 0));
        gm_l3 = t17;
        gm_t88rcucg1b7uk t18 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t7) + 8) + 8));
        gm_l4 = t18;
        {
            void * t20 = gm_l2;
            gm_t88rcucg1b7uk t21 = gm_l4;
            gm_tbccvikg6b96e t22;
            gm_f_layout_a5ovnrm((void *)&t22, (void *)NULL, cx, NULL, t20, t21);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint64_t t23 = GM_LD(uint64_t, (((uint8_t *)&t22) + 0));
            gm_l5 = t23;
        }
        {
            gm_f_list_nof7vfj((void *)&F.l6, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_f_list_kl0t9m0((void *)&F.l7, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            {
                gm_l9 = ((uint64_t)0ULL);
            }
            {
                uint64_t t25 = gm_l3;
                gm_l8 = t25;
            }
            {
                for (;;) {
                    uint64_t t27 = gm_l9;
                    uint64_t t28 = gm_l8;
                    uint8_t t29 = (t27 < t28);
                    if (!t29) goto lb26;
                    {
                        uint64_t t32 = gm_l9;
                        uint64_t t35 = GM_LD(uint64_t, ((uint8_t *)&t32));
                        gm_l10 = t35;
                        {
                            void * t37 = (void *)((uint8_t *)&F.l6);
                            gm_t88rcucg1b7uk t38 = gm_l4;
                            gm_tftb1odc739t5 t39 = gm_f_push_k3l9nic(t37, GM_CONV(gm_csguankko4lmhd, t38));
                            if (((((uint8_t *)&t39))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t39) + 1, gm_srcue4ionbe9lgh, 816, 17);
                            gm_b0_1 t40 = GM_LD(gm_b0_1, (((uint8_t *)&t39) + 1));
                        }
                        {
                            void * t41 = (void *)((uint8_t *)&F.l7);
                            uint64_t t42 = gm_l10;
                            uint64_t t43 = gm_l5;
                            uint64_t r44; bool o45 = __builtin_mul_overflow(t42, t43, &r44);
                            if (o45) gm_trap("integer overflow in `*`", gm_srcue4ionbe9lgh, 817, 27);
                            uint64_t t46 = r44;
                            gm_tftb1odc739t5 t47 = gm_f_push_7o6oubd(t41, t46);
                            if (((((uint8_t *)&t47))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t47) + 1, gm_srcue4ionbe9lgh, 817, 17);
                            gm_b0_1 t48 = GM_LD(gm_b0_1, (((uint8_t *)&t47) + 1));
                        }
                        goto me33;
mn34:;
                        gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 815, 13);
me33:;
                    }
lc26:;
                    {
                        uint64_t t50 = gm_l9;
                        uint64_t r51; bool o52 = __builtin_add_overflow(t50, ((uint64_t)1ULL), &r51);
                        uint64_t t53 = r51;
                        gm_l9 = t53;
                    }
                }
lb26:;
            }
        }
        {
            gm_t6j6smj7qoctu t54;
            gm_b16_8 t55 = GM_LD(gm_b16_8, (((uint8_t *)&F.l6) + 0));
            gm_b16_8 t56 = GM_LD(gm_b16_8, ((uint8_t *)&t55));
            GM_ST(gm_b16_8, (uint8_t *)&t54 + 0, t56);
            gm_b16_8 t57 = GM_LD(gm_b16_8, (((uint8_t *)&F.l7) + 0));
            gm_b16_8 t58 = GM_LD(gm_b16_8, ((uint8_t *)&t57));
            GM_ST(gm_b16_8, (uint8_t *)&t54 + 16, t58);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_ttu09lij36f8c *)gm_out) + 33, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_ttu09lij36f8c *)gm_out) + 34, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_ttu09lij36f8c *)gm_out) + 36, 0);
            ((uint8_t *)&(*(gm_ttu09lij36f8c *)gm_out))[32] = 1;
            GM_ST(gm_t6j6smj7qoctu, (uint8_t *)&(*(gm_ttu09lij36f8c *)gm_out) + 0, t54);
            goto gm_fdone;
        }
        goto me8;
mn15:;
ma10:;
        uint8_t t60 = GM_LD(uint8_t, (((uint8_t *)&t7) + 0));
        if (t60 != ((uint8_t)12ULL)) goto mn59;
        gm_b16_8 t61 = GM_LD(gm_b16_8, ((((uint8_t *)&t7) + 8) + 0));
        gm_l11 = t61;
        gm_t6j6smj7qoctu t62;
        gm_b16_8 t63 = gm_l11;
        GM_ST(gm_b16_8, (uint8_t *)&t62 + 0, t63);
        void * t64 = gm_l2;
        gm_t88rcucg1b7uk t65 = gm_l1;
        gm_tbccvikg6b96e t66;
        gm_f_layout_a5ovnrm((void *)&t66, (void *)NULL, cx, NULL, t64, t65);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b16_8 t67 = GM_LD(gm_b16_8, (((uint8_t *)&t66) + 16));
        GM_ST(gm_b16_8, (uint8_t *)&t62 + 16, t67);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_ttu09lij36f8c *)gm_out) + 33, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_ttu09lij36f8c *)gm_out) + 34, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_ttu09lij36f8c *)gm_out) + 36, 0);
        ((uint8_t *)&(*(gm_ttu09lij36f8c *)gm_out))[32] = 1;
        GM_ST(gm_t6j6smj7qoctu, (uint8_t *)&(*(gm_ttu09lij36f8c *)gm_out) + 0, t62);
        goto gm_fdone;
        goto me8;
mn59:;
ma11:;
        uint8_t t69 = GM_LD(uint8_t, (((uint8_t *)&t7) + 0));
        if (t69 != ((uint8_t)17ULL)) goto mn68;
        uint32_t t70 = GM_LD(uint32_t, ((((uint8_t *)&t7) + 8) + 0));
        gm_l12 = t70;
        {
            uint32_t t73 = gm_l12;
            uint64_t t74 = (uint64_t)t73;
            uint64_t t75 = (uintptr_t)t74;
            void * t76 = gm_l2;
            uint64_t t77 = GM_LD(uintptr_t, ((((uint8_t *)t76) + 1088) + 0) + 8);
            if (t75 >= (uintptr_t)(t77)) gm_trap_index(t75, t77, gm_srcue4ionbe9lgh, 823, 29);
            uint8_t t78 = GM_LD(uint8_t, ((((uint8_t *)GM_LD(void *, ((((uint8_t *)t76) + 1088) + 0)) + t75 * 184) + 24) + 0));
            if (t78 != ((uint8_t)0ULL)) goto ie72;
            gm_tpautr4br7h3l t79 = GM_LD(gm_tpautr4br7h3l, ((((uint8_t *)GM_LD(void *, ((((uint8_t *)t76) + 1088) + 0)) + t75 * 184) + 24) + 8));
            F.l13 = t79;
            {
                uint8_t t82;
                if ((((((uint8_t *)&F.l13) + 40))[4] != 0)) goto mn84;
                t82 = ((uint8_t)0);
                goto me83;
mn84:;
                t82 = ((uint8_t)1);
                goto me83;
mn85:;
                gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 824, 16);
me83:;
                if (!t82) goto ie81;
                {
                    gm_ttu09lij36f8c t87;
                    memset(&t87, 0, sizeof t87);
                    uint8_t t88 = 0;
                    F.ret = t87;
                    goto gm_fret;
                }
                goto ix81;
ie81:;
ix81:;
            }
            {
                gm_f_list_nof7vfj((void *)&F.l14, (void *)NULL, cx, NULL);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                {
                    gm_b16_8 t90 = GM_LD(gm_b16_8, (((uint8_t *)&F.l13) + 0));
                    gm_l15 = t90;
                }
                {
                    gm_l16 = ((uint64_t)0ULL);
                }
                {
                    for (;;) {
                        uint64_t t92 = gm_l16;
                        uint64_t t93 = GM_LD(uintptr_t, ((uint8_t *)&gm_l15) + 8);
                        uint8_t t94 = (t92 < t93);
                        if (!t94) goto lb91;
                        {
                            uint64_t t97 = gm_l16;
                            uint64_t t98 = (uintptr_t)t97;
                            uint64_t t99 = GM_LD(uintptr_t, ((uint8_t *)&gm_l15) + 8);
                            gm_t0mglksrkfa5e t102 = GM_LD(gm_t0mglksrkfa5e, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l15)) + t98 * 40));
                            F.l17 = t102;
                            {
                                void * t104 = (void *)((uint8_t *)&F.l14);
                                gm_t88rcucg1b7uk t105 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l17) + 16));
                                gm_tftb1odc739t5 t106 = gm_f_push_k3l9nic(t104, GM_CONV(gm_csguankko4lmhd, t105));
                                if (((((uint8_t *)&t106))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t106) + 1, gm_srcue4ionbe9lgh, 826, 35);
                                gm_b0_1 t107 = GM_LD(gm_b0_1, (((uint8_t *)&t106) + 1));
                            }
                            goto me100;
mn101:;
                            gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 826, 13);
me100:;
                        }
lc91:;
                        {
                            uint64_t t109 = gm_l16;
                            uint64_t r110; bool o111 = __builtin_add_overflow(t109, ((uint64_t)1ULL), &r110);
                            uint64_t t112 = r110;
                            gm_l16 = t112;
                        }
                    }
lb91:;
                }
            }
            {
                gm_t6j6smj7qoctu t113;
                gm_b16_8 t114 = GM_LD(gm_b16_8, (((uint8_t *)&F.l14) + 0));
                gm_b16_8 t115 = GM_LD(gm_b16_8, ((uint8_t *)&t114));
                GM_ST(gm_b16_8, (uint8_t *)&t113 + 0, t115);
                void * t116 = gm_l2;
                gm_t88rcucg1b7uk t117 = gm_l1;
                gm_tbccvikg6b96e t118;
                gm_f_layout_a5ovnrm((void *)&t118, (void *)NULL, cx, NULL, t116, t117);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_b16_8 t119 = GM_LD(gm_b16_8, (((uint8_t *)&t118) + 16));
                GM_ST(gm_b16_8, (uint8_t *)&t113 + 16, t119);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_ttu09lij36f8c *)gm_out) + 33, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_ttu09lij36f8c *)gm_out) + 34, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_ttu09lij36f8c *)gm_out) + 36, 0);
                ((uint8_t *)&(*(gm_ttu09lij36f8c *)gm_out))[32] = 1;
                GM_ST(gm_t6j6smj7qoctu, (uint8_t *)&(*(gm_ttu09lij36f8c *)gm_out) + 0, t113);
                goto gm_fdone;
            }
            goto ix72;
ie72:;
            {
                gm_ttu09lij36f8c t121;
                memset(&t121, 0, sizeof t121);
                uint8_t t122 = 0;
                F.ret = t121;
                goto gm_fret;
            }
ix72:;
        }
        goto me8;
mn68:;
ma12:;
        gm_ttu09lij36f8c t124;
        memset(&t124, 0, sizeof t124);
        uint8_t t125 = 0;
        F.ret = t124;
        goto gm_fret;
        goto me8;
mn123:;
ma13:;
        gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 810, 5);
me8:;
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
inline __attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_payload_ty_mu1ii1k(void * a0, gm_csguankko4lmhd a1, uint32_t a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_payload_ty_mu1ii1k F;
    void * gm_l0; /* m */
    gm_t88rcucg1b7uk gm_l1; /* t */
    uint32_t gm_l2; /* i */
    uint32_t gm_l3; /* id */
    gm_b16_8 gm_l4; /* variants */
    GM_STACK_CHECK_SHALLOW(&F, 287u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    gm_l2 = a2;
    {
        void * t3 = gm_l0;
        void * t4 = GM_LD(void *, (((uint8_t *)t3) + 0));
        void * t5 = (void *)(((uint8_t *)t4) + 8);
        gm_t88rcucg1b7uk t6 = gm_l1;
        gm_t53at80pfdus3 t7;
        gm_f_kind_jcnt4hm((void *)&t7, t5, GM_CONV(gm_csguankko4lmhd, t6));
        uint8_t t8 = GM_LD(uint8_t, (((uint8_t *)&t7) + 0));
        if (t8 != ((uint8_t)17ULL)) goto ie2;
        uint32_t t9 = GM_LD(uint32_t, ((((uint8_t *)&t7) + 8) + 0));
        gm_l3 = t9;
        {
            uint32_t t12 = gm_l3;
            uint64_t t13 = (uint64_t)t12;
            uint64_t t14 = (uintptr_t)t13;
            void * t15 = gm_l0;
            void * t16 = GM_LD(void *, (((uint8_t *)t15) + 0));
            uint64_t t17 = GM_LD(uintptr_t, ((((uint8_t *)t16) + 1088) + 0) + 8);
            if (t14 >= (uintptr_t)(t17)) gm_trap_index(t14, t17, gm_srcue4ionbe9lgh, 755, 28);
            uint8_t t18 = GM_LD(uint8_t, ((((uint8_t *)GM_LD(void *, ((((uint8_t *)t16) + 1088) + 0)) + t14 * 184) + 24) + 0));
            if (t18 != ((uint8_t)1ULL)) goto ie11;
            gm_b16_8 t19 = GM_LD(gm_b16_8, (((((uint8_t *)GM_LD(void *, ((((uint8_t *)t16) + 1088) + 0)) + t14 * 184) + 24) + 8) + 8));
            gm_l4 = t19;
            {
                uint32_t t22 = gm_l2;
                uint64_t t23 = (uint64_t)t22;
                uint64_t t24 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                uint8_t t25 = (t23 >= t24);
                if (!t25) goto ie21;
                {
                    gm_tkn42u7na3t81 t27;
                    memset(&t27, 0, sizeof t27);
                    uint8_t t28 = 0;
                    F.ret = t27;
                    goto gm_fret;
                }
                goto ix21;
ie21:;
ix21:;
            }
            {
                uint32_t t29 = gm_l2;
                uint64_t t30 = (uint64_t)t29;
                uint64_t t31 = (uintptr_t)t30;
                uint64_t t32 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                if (t31 >= (uintptr_t)(t32)) gm_trap_index(t31, t32, gm_srcue4ionbe9lgh, 757, 12);
                gm_tkn42u7na3t81 t33 = GM_LD(gm_tkn42u7na3t81, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t31 * 72) + 16));
                uint8_t t34 = 0;
                F.ret = t33;
                goto gm_fret;
            }
            goto ix11;
ie11:;
            {
                gm_tkn42u7na3t81 t36;
                memset(&t36, 0, sizeof t36);
                uint8_t t37 = 0;
                F.ret = t36;
                goto gm_fret;
            }
ix11:;
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
__attribute__((aligned(4))) uint8_t gm_f_put_bytes_1ltl1lv(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3, uint64_t a4) {
    struct gm_fr_put_bytes_1ltl1lv F;
    void * gm_l0; /* m */
    gm_t88rcucg1b7uk gm_l2; /* t */
    gm_b16_8 gm_l3; /* out */
    uint64_t gm_l4; /* at */
    unsigned __int128 gm_l5; /* bits */
    uint64_t gm_l6; /* size */
    uint64_t gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    uint64_t gm_l9; /* i */
    uint8_t gm_l10; /* b */
    double gm_l11; /* x */
    uint16_t gm_l12; /* w */
    uint64_t gm_l13; /* raw */
    uint64_t gm_l14; /* n */
    uint64_t gm_l15; /* <tmp> */
    uint64_t gm_l16; /* <tmp> */
    uint64_t gm_l17; /* i */
    void * gm_l18; /* a */
    gm_t6j6smj7qoctu gm_l19; /* o */
    gm_b16_8 gm_l20; /* <tmp> */
    uint64_t gm_l21; /* <tmp> */
    uint64_t gm_l22; /* i */
    GM_STACK_CHECK(&F, 1260u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    {
        uint8_t t9 = GM_LD(uint8_t, (((uint8_t *)a1) + 0));
        switch (t9) { case ((uint8_t)0ULL): goto ma3; case ((uint8_t)1ULL): goto ma5; case ((uint8_t)2ULL): goto ma4; case ((uint8_t)3ULL): goto ma6; case ((uint8_t)4ULL): goto ma7; case ((uint8_t)5ULL): goto ma7; case ((uint8_t)6ULL): goto ma7; case ((uint8_t)7ULL): goto ma7; case ((uint8_t)8ULL): goto ma7; case ((uint8_t)9ULL): goto ma7; case ((uint8_t)10ULL): goto ma7; case ((uint8_t)11ULL): goto ma7; case ((uint8_t)12ULL): goto ma7; case ((uint8_t)13ULL): goto ma7; default: goto ma7; }
ma3:;
        uint8_t t11 = GM_LD(uint8_t, (((uint8_t *)a1) + 0));
        if (t11 != ((uint8_t)0ULL)) goto mn10;
        unsigned __int128 t12 = GM_LD(unsigned __int128, (((uint8_t *)a1) + 16));
        gm_l5 = t12;
        {
            void * t14 = gm_l0;
            void * t15 = GM_LD(void *, (((uint8_t *)t14) + 0));
            gm_t88rcucg1b7uk t16 = gm_l2;
            gm_tbccvikg6b96e t17;
            gm_f_layout_a5ovnrm((void *)&t17, (void *)NULL, cx, NULL, t15, t16);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint64_t t18 = GM_LD(uint64_t, (((uint8_t *)&t17) + 0));
            gm_l6 = t18;
        }
        {
            {
                gm_l8 = ((uint64_t)0ULL);
            }
            {
                uint64_t t20 = gm_l6;
                gm_l7 = t20;
            }
            {
                for (;;) {
                    uint64_t t22 = gm_l8;
                    uint64_t t23 = gm_l7;
                    uint8_t t24 = (t22 < t23);
                    if (!t24) goto lb21;
                    {
                        uint64_t t27 = gm_l8;
                        uint64_t t30 = GM_LD(uint64_t, ((uint8_t *)&t27));
                        gm_l9 = t30;
                        {
                            gm_b16_8 t32 = gm_l3;
                            gm_b16_8 t33 = t32;
                            uint64_t t34 = gm_l4;
                            void * t35 = gm_l0;
                            uint64_t t36 = gm_l9;
                            uint64_t t37 = gm_l6;
                            uint64_t t38 = gm_f_byte_at_m94bj3e(t35, t36, t37);
                            uint64_t r39; bool o40 = __builtin_add_overflow(t34, t38, &r39);
                            if (o40) gm_trap("integer overflow in `+`", gm_srcue4ionbe9lgh, 843, 37);
                            uint64_t t41 = r39;
                            uint64_t t42 = (uint64_t)t41;
                            uint64_t t43 = (uintptr_t)t42;
                            uint64_t t44 = GM_LD(uintptr_t, ((uint8_t *)&t33) + 8);
                            if (t43 >= (uintptr_t)(t44)) gm_trap_index(t43, t44, gm_srcue4ionbe9lgh, 843, 32);
                            unsigned __int128 t45 = gm_l5;
                            uint64_t t46 = gm_l9;
                            uint64_t r47; bool o48 = __builtin_mul_overflow(t46, ((uint64_t)8ULL), &r47);
                            if (o48) gm_trap("integer overflow in `*`", gm_srcue4ionbe9lgh, 843, 92);
                            uint64_t t49 = r47;
                            unsigned __int128 t50 = (unsigned __int128)t49;
                            unsigned __int128 n51 = (unsigned __int128)t50;
                            if (n51 >= 128) gm_trap_shift((uint64_t)n51, "u128", gm_srcue4ionbe9lgh, 843, 79);
                            unsigned __int128 t52 = (unsigned __int128)(t45 >> (int)n51);
                            uint8_t t53 = (uint8_t)t52;
                            GM_ST(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&t33)) + t43 * 1), t53);
                        }
                        goto me28;
mn29:;
                        gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 843, 13);
me28:;
                    }
lc21:;
                    {
                        uint64_t t55 = gm_l8;
                        uint64_t r56; bool o57 = __builtin_add_overflow(t55, ((uint64_t)1ULL), &r56);
                        uint64_t t58 = r56;
                        gm_l8 = t58;
                    }
                }
lb21:;
            }
        }
        {
            uint8_t t59 = 0;
            F.ret = ((uint8_t)1);
            goto gm_fret;
        }
        goto me2;
mn10:;
ma4:;
        uint8_t t61 = GM_LD(uint8_t, (((uint8_t *)a1) + 0));
        if (t61 != ((uint8_t)2ULL)) goto mn60;
        uint8_t t62 = GM_LD(uint8_t, (((uint8_t *)a1) + 16));
        gm_l10 = t62;
        {
            gm_b16_8 t64 = gm_l3;
            gm_b16_8 t65 = t64;
            uint64_t t66 = gm_l4;
            uint64_t t67 = (uint64_t)t66;
            uint64_t t68 = (uintptr_t)t67;
            uint64_t t69 = GM_LD(uintptr_t, ((uint8_t *)&t65) + 8);
            if (t68 >= (uintptr_t)(t69)) gm_trap_index(t68, t69, gm_srcue4ionbe9lgh, 847, 13);
            uint8_t *t70 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t65)) + t68 * 1);
            uint8_t t71;
            uint8_t t73 = gm_l10;
            if (!t73) goto ie72;
            uint8_t t75 = ((uint8_t)1ULL);
            t71 = t75;
            goto ix72;
ie72:;
            uint8_t t77 = ((uint8_t)0ULL);
            t71 = t77;
ix72:;
            GM_ST(uint8_t, t70, t71);
        }
        {
            uint8_t t78 = 0;
            F.ret = ((uint8_t)1);
            goto gm_fret;
        }
        goto me2;
mn60:;
ma5:;
        uint8_t t80 = GM_LD(uint8_t, (((uint8_t *)a1) + 0));
        if (t80 != ((uint8_t)1ULL)) goto mn79;
        double t81 = GM_LD(double, (((uint8_t *)a1) + 16));
        gm_l11 = t81;
        {
            void * t84 = gm_l0;
            void * t85 = GM_LD(void *, (((uint8_t *)t84) + 0));
            void * t86 = (void *)(((uint8_t *)t85) + 8);
            gm_t88rcucg1b7uk t87 = gm_l2;
            gm_t53at80pfdus3 t88;
            gm_f_kind_jcnt4hm((void *)&t88, t86, GM_CONV(gm_csguankko4lmhd, t87));
            uint8_t t89 = GM_LD(uint8_t, (((uint8_t *)&t88) + 0));
            if (t89 != ((uint8_t)3ULL)) goto ie83;
            uint16_t t90 = GM_LD(uint16_t, (((uint8_t *)&t88) + 8));
            gm_l12 = t90;
            {
                gm_l13 = ((uint64_t)0ULL);
            }
            {
                gm_l14 = ((uint64_t)8ULL);
            }
            {
                uint16_t t92 = gm_l12;
                uint16_t t95 = GM_LD(uint16_t, ((uint8_t *)&t92));
                if (!(t95 == ((uint16_t)64ULL))) goto mn94;
                {
                    double t97 = gm_l11;
                    uint64_t t98;
                    memset(&t98, 0, sizeof t98); memcpy(&t98, ((uint8_t *)&t97), sizeof t98 < 8 ? sizeof t98 : 8);
                    gm_l13 = t98;
                }
                goto me93;
mn94:;
                uint16_t t100 = GM_LD(uint16_t, ((uint8_t *)&t92));
                if (!(t100 == ((uint16_t)32ULL))) goto mn99;
                {
                    double t102 = gm_l11;
                    float t103 = (float)((double)t102);
                    uint32_t t104;
                    memset(&t104, 0, sizeof t104); memcpy(&t104, ((uint8_t *)&t103), sizeof t104 < 4 ? sizeof t104 : 4);
                    uint64_t t105 = (uint64_t)t104;
                    gm_l13 = t105;
                }
                {
                    gm_l14 = ((uint64_t)4ULL);
                }
                goto me93;
mn99:;
                uint8_t t107 = 0;
                F.ret = ((uint8_t)0);
                goto gm_fret;
                goto me93;
mn106:;
                gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 854, 13);
me93:;
            }
            {
                {
                    gm_l16 = ((uint64_t)0ULL);
                }
                {
                    uint64_t t109 = gm_l14;
                    gm_l15 = t109;
                }
                {
                    for (;;) {
                        uint64_t t111 = gm_l16;
                        uint64_t t112 = gm_l15;
                        uint8_t t113 = (t111 < t112);
                        if (!t113) goto lb110;
                        {
                            uint64_t t116 = gm_l16;
                            uint64_t t119 = GM_LD(uint64_t, ((uint8_t *)&t116));
                            gm_l17 = t119;
                            {
                                gm_b16_8 t121 = gm_l3;
                                gm_b16_8 t122 = t121;
                                uint64_t t123 = gm_l4;
                                void * t124 = gm_l0;
                                uint64_t t125 = gm_l17;
                                uint64_t t126 = gm_l14;
                                uint64_t t127 = gm_f_byte_at_m94bj3e(t124, t125, t126);
                                uint64_t r128; bool o129 = __builtin_add_overflow(t123, t127, &r128);
                                if (o129) gm_trap("integer overflow in `+`", gm_srcue4ionbe9lgh, 862, 34);
                                uint64_t t130 = r128;
                                uint64_t t131 = (uint64_t)t130;
                                uint64_t t132 = (uintptr_t)t131;
                                uint64_t t133 = GM_LD(uintptr_t, ((uint8_t *)&t122) + 8);
                                if (t132 >= (uintptr_t)(t133)) gm_trap_index(t132, t133, gm_srcue4ionbe9lgh, 862, 29);
                                uint64_t t134 = gm_l13;
                                uint64_t t135 = gm_l17;
                                uint64_t r136; bool o137 = __builtin_mul_overflow(t135, ((uint64_t)8ULL), &r136);
                                if (o137) gm_trap("integer overflow in `*`", gm_srcue4ionbe9lgh, 862, 81);
                                uint64_t t138 = r136;
                                unsigned __int128 n139 = (unsigned __int128)t138;
                                if (n139 >= 64) gm_trap_shift((uint64_t)n139, "u64", gm_srcue4ionbe9lgh, 862, 73);
                                uint64_t t140 = (uint64_t)(t134 >> (int)n139);
                                uint8_t t141 = (uint8_t)t140;
                                GM_ST(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&t122)) + t132 * 1), t141);
                            }
                            goto me117;
mn118:;
                            gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 862, 13);
me117:;
                        }
lc110:;
                        {
                            uint64_t t143 = gm_l16;
                            uint64_t r144; bool o145 = __builtin_add_overflow(t143, ((uint64_t)1ULL), &r144);
                            uint64_t t146 = r144;
                            gm_l16 = t146;
                        }
                    }
lb110:;
                }
            }
            {
                uint8_t t147 = 0;
                F.ret = ((uint8_t)1);
                goto gm_fret;
            }
            goto ix83;
ie83:;
            {
                uint8_t t149 = 0;
                F.ret = ((uint8_t)0);
                goto gm_fret;
            }
ix83:;
        }
        goto me2;
mn79:;
ma6:;
        uint8_t t151 = GM_LD(uint8_t, (((uint8_t *)a1) + 0));
        if (t151 != ((uint8_t)3ULL)) goto mn150;
        void * t152 = GM_LD(void *, (((uint8_t *)a1) + 16));
        gm_l18 = t152;
        {
            void * t154 = gm_l0;
            gm_t88rcucg1b7uk t155 = gm_l2;
            gm_ttu09lij36f8c t156;
            gm_f_offsets_niooj98((void *)&t156, (void *)NULL, cx, NULL, t154, t155);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t6j6smj7qoctu t157;
            if (((((uint8_t *)&t156))[32] != 0)) {
                t157 = (*(gm_t6j6smj7qoctu *)(((uint8_t *)&t156) + 0));
            } else {
                uint8_t t158 = 0;
                F.ret = ((uint8_t)0);
                goto gm_fret;
            }
            gm_l19 = t157;
        }
        {
            {
                void * t160 = gm_l18;
                gm_b16_8 t161 = GM_LD(gm_b16_8, (((uint8_t *)t160) + 0));
                gm_b16_8 t162 = GM_LD(gm_b16_8, ((uint8_t *)&t161));
                gm_l20 = t162;
            }
            {
                gm_l21 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t164 = gm_l21;
                    uint64_t t165 = GM_LD(uintptr_t, ((uint8_t *)&gm_l20) + 8);
                    uint8_t t166 = (t164 < t165);
                    if (!t166) goto lb163;
                    {
                        gm_thf8as07kj3uj t169;
                        memset((uint8_t *)&t169 + 8, 0, 8);
                        uint64_t t170 = gm_l21;
                        GM_ST(uint64_t, (uint8_t *)&t169 + 0, t170);
                        uint64_t t171 = gm_l21;
                        uint64_t t172 = (uintptr_t)t171;
                        uint64_t t173 = GM_LD(uintptr_t, ((uint8_t *)&gm_l20) + 8);
                        memcpy((uint8_t *)&t169 + 16, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l20)) + t172 * 48), 48);
                        uint64_t t176 = GM_LD(uint64_t, (((uint8_t *)&t169) + 0));
                        gm_l22 = t176;
                        F.l23 = (*(gm_t6evimt84lndq *)(((uint8_t *)&t169) + 16));
                        {
                            void * t179 = gm_l0;
                            uint64_t t180 = gm_l22;
                            uint64_t t181 = (uintptr_t)t180;
                            uint64_t t182 = GM_LD(uintptr_t, (((uint8_t *)&gm_l19) + 0) + 8);
                            if (t181 >= (uintptr_t)(t182)) gm_trap_index(t181, t182, gm_srcue4ionbe9lgh, 868, 39);
                            gm_t88rcucg1b7uk t183 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, (((uint8_t *)&gm_l19) + 0)) + t181 * 4));
                            gm_b16_8 t184 = gm_l3;
                            uint64_t t185 = gm_l4;
                            uint64_t t186 = gm_l22;
                            uint64_t t187 = (uintptr_t)t186;
                            uint64_t t188 = GM_LD(uintptr_t, (((uint8_t *)&gm_l19) + 16) + 8);
                            if (t187 >= (uintptr_t)(t188)) gm_trap_index(t187, t188, gm_srcue4ionbe9lgh, 868, 59);
                            uint64_t t189 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)&gm_l19) + 16)) + t187 * 8));
                            uint64_t r190; bool o191 = __builtin_add_overflow(t185, t189, &r190);
                            if (o191) gm_trap("integer overflow in `+`", gm_srcue4ionbe9lgh, 868, 54);
                            uint64_t t192 = r190;
                            uint8_t t193 = gm_f_put_bytes_1ltl1lv((void *)NULL, cx, NULL, t179, &(*(gm_t6evimt84lndq *)((uint8_t *)&F.l23)), t183, t184, t192);
                            if (gm_unw.kind) {
                                goto gm_ll167;
                            }
                            uint8_t t194 = (uint8_t)!t193;
                            if (!t194) goto ie178;
                            {
                                uint8_t t196 = 0;
                                F.ret = ((uint8_t)0);
                                goto gm_fret;
                            }
                            goto ix178;
ie178:;
ix178:;
                        }
                        goto me174;
mn175:;
                        gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 867, 13);
me174:;
                    }
lc163:;
                    {
                        uint64_t t198 = gm_l21;
                        uint64_t r199; bool o200 = __builtin_add_overflow(t198, ((uint64_t)1ULL), &r199);
                        uint64_t t201 = r199;
                        gm_l21 = t201;
                    }
                }
gm_ll167:;
                if (gm_unw.frame == &F.h && gm_unw.label == 2) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb163; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc163; }
                }
                goto gm_lad;
lb163:;
            }
        }
        {
            uint8_t t202 = 0;
            F.ret = ((uint8_t)1);
            goto gm_fret;
        }
        goto me2;
mn150:;
ma7:;
        uint8_t t204 = 0;
        F.ret = ((uint8_t)0);
        goto gm_fret;
        goto me2;
mn203:;
ma8:;
        gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 840, 5);
me2:;
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
inline __attribute__((aligned(4))) void gm_f_reinterpret_jmr6jts(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_t88rcucg1b7uk a2, gm_t88rcucg1b7uk a3, gm_tl874t2atnlh3 a4) {
    struct gm_fr_reinterpret_jmr6jts F;
    void * gm_l0; /* m */
    gm_t88rcucg1b7uk gm_l2; /* from */
    gm_t88rcucg1b7uk gm_l3; /* to */
    gm_tl874t2atnlh3 gm_l4; /* span */
    uint64_t gm_l5; /* size */
    uint64_t gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    GM_STACK_CHECK(&F, 629u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    {
        void * t2 = gm_l0;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 0));
        gm_t88rcucg1b7uk t4 = gm_l2;
        gm_tbccvikg6b96e t5;
        gm_f_layout_a5ovnrm((void *)&t5, (void *)NULL, cx, NULL, t3, t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)&t5) + 0));
        gm_l5 = t6;
    }
    {
        gm_f_list_uvfu1k1((void *)&F.l6, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_l8 = ((uint64_t)0ULL);
        }
        {
            uint64_t t8 = gm_l5;
            gm_l7 = t8;
        }
        {
            for (;;) {
                uint64_t t10 = gm_l8;
                uint64_t t11 = gm_l7;
                uint8_t t12 = (t10 < t11);
                if (!t12) goto lb9;
                {
                    uint64_t t15 = gm_l8;
                    {
                        void * t19 = (void *)((uint8_t *)&F.l6);
                        gm_tftb1odc739t5 t20 = gm_f_push_t83rg2m(t19, ((uint8_t)0ULL));
                        if (((((uint8_t *)&t20))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t20) + 1, gm_srcue4ionbe9lgh, 801, 24);
                        gm_b0_1 t21 = GM_LD(gm_b0_1, (((uint8_t *)&t20) + 1));
                    }
                    goto me16;
mn17:;
                    gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 801, 5);
me16:;
                }
lc9:;
                {
                    uint64_t t23 = gm_l8;
                    uint64_t r24; bool o25 = __builtin_add_overflow(t23, ((uint64_t)1ULL), &r24);
                    uint64_t t26 = r24;
                    gm_l8 = t26;
                }
            }
lb9:;
        }
    }
    {
        void * t28 = gm_l0;
        gm_t88rcucg1b7uk t29 = gm_l2;
        gm_b16_8 t30 = GM_LD(gm_b16_8, (((uint8_t *)&F.l6) + 0));
        uint8_t t31 = gm_f_put_bytes_1ltl1lv((void *)NULL, cx, NULL, t28, &(*(gm_t6evimt84lndq *)((uint8_t *)a1)), t29, t30, ((uint64_t)0ULL));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t32 = (uint8_t)!t31;
        if (!t32) goto ie27;
        {
            void * t34 = gm_l0;
            gm_tl874t2atnlh3 t35 = gm_l4;
            gm_b16_8 t36;
            GM_ST(void *, (uint8_t *)&t36, (void *)(gm_s94n39ltlvmhe));
            GM_ST(uintptr_t, (uint8_t *)&t36 + 8, (uintptr_t)(35));
            gm_t39uai1ajk3qv t37;
            gm_f_unsupported_2seq49l((void *)&t37, t34, GM_CONV(gm_cseahvg8v7p0vs, t35), t36);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 8, 0);
            ((uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out))[0] = 1;
            GM_ST(gm_t39uai1ajk3qv, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 16, t37);
            goto gm_fdone;
        }
        goto ix27;
ie27:;
ix27:;
    }
    {
        void * t38 = gm_l0;
        gm_t88rcucg1b7uk t39 = gm_l3;
        gm_b16_8 t40 = GM_LD(gm_b16_8, (((uint8_t *)&F.l6) + 0));
        gm_b16_8 t41 = GM_LD(gm_b16_8, ((uint8_t *)&t40));
        gm_tjoi8o798ne9k t42;
        gm_f_get_bytes_ql9o4k0((void *)&t42, (void *)NULL, cx, NULL, t38, t39, t41, ((uint64_t)0ULL));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t6evimt84lndq t43;
        if (((((uint8_t *)&t42))[48] != 0)) {
            t43 = (*(gm_t6evimt84lndq *)(((uint8_t *)&t42) + 0));
        } else {
            void * t44 = gm_l0;
            gm_tl874t2atnlh3 t45 = gm_l4;
            gm_b16_8 t46;
            GM_ST(void *, (uint8_t *)&t46, (void *)(gm_sbjuojbqo51av));
            GM_ST(uintptr_t, (uint8_t *)&t46 + 8, (uintptr_t)(35));
            gm_t39uai1ajk3qv t47;
            gm_f_unsupported_2seq49l((void *)&t47, t44, GM_CONV(gm_cseahvg8v7p0vs, t45), t46);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 8, 0);
            ((uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out))[0] = 1;
            GM_ST(gm_t39uai1ajk3qv, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 16, t47);
            goto gm_fdone;
        }
        F.l9 = t43;
    }
    {
        const uint8_t *t48 = ((uint8_t *)&F.l9);
        memset(&(*(gm_ttk92ufj2f6eh *)gm_out), 0, sizeof (*(gm_ttk92ufj2f6eh *)gm_out));
        ((uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out))[0] = 0;
        memmove((uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 16, t48, 48);
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
inline __attribute__((aligned(4))) void gm_f_round_float_saju9fu(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, double a1, gm_t88rcucg1b7uk a2) {
    struct gm_fr_round_float_saju9fu F;
    void * gm_l0; /* m */
    double gm_l1; /* x */
    gm_t88rcucg1b7uk gm_l2; /* t */
    uint16_t gm_l3; /* bits */
    GM_STACK_CHECK(&F, 408u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t3 = gm_l0;
        void * t4 = GM_LD(void *, (((uint8_t *)t3) + 0));
        void * t5 = (void *)(((uint8_t *)t4) + 8);
        gm_t88rcucg1b7uk t6 = gm_l2;
        gm_t53at80pfdus3 t7;
        gm_f_kind_jcnt4hm((void *)&t7, t5, GM_CONV(gm_csguankko4lmhd, t6));
        uint8_t t8 = GM_LD(uint8_t, (((uint8_t *)&t7) + 0));
        if (t8 != ((uint8_t)3ULL)) goto ie2;
        uint16_t t9 = GM_LD(uint16_t, (((uint8_t *)&t7) + 8));
        gm_l3 = t9;
        {
            uint16_t t12 = gm_l3;
            uint8_t t13 = ((t12 == ((uint16_t)32ULL)));
            if (!t13) goto ie11;
            {
                double t15 = gm_l1;
                float t16 = (float)((double)t15);
                double t17 = (double)t16;
                memset(&(*(gm_t26jhpkpc0sda *)gm_out), 0, sizeof (*(gm_t26jhpkpc0sda *)gm_out));
                ((uint8_t *)&(*(gm_t26jhpkpc0sda *)gm_out))[0] = 0;
                GM_ST(double, (uint8_t *)&(*(gm_t26jhpkpc0sda *)gm_out) + 16, t17);
                goto gm_fdone;
            }
            goto ix11;
ie11:;
ix11:;
        }
        {
            uint16_t t19 = gm_l3;
            uint8_t t20 = ((t19 == ((uint16_t)16ULL)));
            if (!t20) goto ie18;
            {
                double t23 = gm_l1;
                double t24 = gm_l1;
                double t25 = (t23 - t24);
                uint8_t t26 = (!(t25 == ((double)0e0)));
                if (!t26) goto ie22;
                {
                    double t28 = gm_l1;
                    memset(&(*(gm_t26jhpkpc0sda *)gm_out), 0, sizeof (*(gm_t26jhpkpc0sda *)gm_out));
                    ((uint8_t *)&(*(gm_t26jhpkpc0sda *)gm_out))[0] = 0;
                    GM_ST(double, (uint8_t *)&(*(gm_t26jhpkpc0sda *)gm_out) + 16, t28);
                    goto gm_fdone;
                }
                goto ix22;
ie22:;
ix22:;
            }
            {
                double t29 = gm_l1;
                gm_tvegkb6ouf3m0 t30;
                gm_f_float_exact_1g17c6c((void *)&t30, (void *)NULL, cx, NULL, t29);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tngtuiuskn938 t31;
                if (((((uint8_t *)&t30))[96] != 0)) {
                    t31 = (*(gm_tngtuiuskn938 *)(((uint8_t *)&t30) + 0));
                } else {
                    double t32 = gm_l1;
                    memset(&(*(gm_t26jhpkpc0sda *)gm_out), 0, sizeof (*(gm_t26jhpkpc0sda *)gm_out));
                    ((uint8_t *)&(*(gm_t26jhpkpc0sda *)gm_out))[0] = 0;
                    GM_ST(double, (uint8_t *)&(*(gm_t26jhpkpc0sda *)gm_out) + 16, t32);
                    goto gm_fdone;
                }
                F.l4 = t31;
            }
            {
                double t33 = gm_f_to_f16_0pcu0pq((void *)NULL, cx, NULL, &(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l4)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                memset(&(*(gm_t26jhpkpc0sda *)gm_out), 0, sizeof (*(gm_t26jhpkpc0sda *)gm_out));
                ((uint8_t *)&(*(gm_t26jhpkpc0sda *)gm_out))[0] = 0;
                GM_ST(double, (uint8_t *)&(*(gm_t26jhpkpc0sda *)gm_out) + 16, t33);
                goto gm_fdone;
            }
            goto ix18;
ie18:;
ix18:;
        }
        {
            double t34 = gm_l1;
            memset(&(*(gm_t26jhpkpc0sda *)gm_out), 0, sizeof (*(gm_t26jhpkpc0sda *)gm_out));
            ((uint8_t *)&(*(gm_t26jhpkpc0sda *)gm_out))[0] = 0;
            GM_ST(double, (uint8_t *)&(*(gm_t26jhpkpc0sda *)gm_out) + 16, t34);
            goto gm_fdone;
        }
        goto ix2;
ie2:;
        {
            double t36 = gm_l1;
            memset(&(*(gm_t26jhpkpc0sda *)gm_out), 0, sizeof (*(gm_t26jhpkpc0sda *)gm_out));
            ((uint8_t *)&(*(gm_t26jhpkpc0sda *)gm_out))[0] = 0;
            GM_ST(double, (uint8_t *)&(*(gm_t26jhpkpc0sda *)gm_out) + 16, t36);
            goto gm_fdone;
        }
ix2:;
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
inline __attribute__((aligned(4))) uint8_t gm_f_same_place_1r7qrqk(const void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_same_place_1r7qrqk F;
    gm_tkaapcii86udk gm_l0; /* a */
    gm_tkaapcii86udk gm_l1; /* b */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    uint64_t gm_l4; /* i */
    uint64_t gm_l5; /* x */
    GM_STACK_CHECK_SHALLOW(&F, 290u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        uint8_t t3 = 0;
        void * t4 = GM_LD(void *, (((uint8_t *)a0) + 0));
        void * t5 = GM_LD(void *, (((uint8_t *)a1) + 0));
        uint8_t t6 = (!(t4 == t5));
        if (!t6) {
            uint64_t t7 = GM_LD(uintptr_t, (((uint8_t *)a0) + 8) + 8);
            uint64_t t8 = GM_LD(uintptr_t, (((uint8_t *)a1) + 8) + 8);
            uint8_t t9 = (!(t7 == t8));
            t3 = t9;
        } else t3 = 1;
        if (!t3) goto ie2;
        {
            uint8_t t11 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        {
            gm_b16_8 t13 = GM_LD(gm_b16_8, (((uint8_t *)a0) + 8));
            gm_l2 = t13;
        }
        {
            gm_l3 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t15 = gm_l3;
                uint64_t t16 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                uint8_t t17 = (t15 < t16);
                if (!t17) goto lb14;
                {
                    uint64_t t20 = gm_l3;
                    uint64_t t21 = gm_l3;
                    uint64_t t22 = (uintptr_t)t21;
                    uint64_t t23 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                    uint64_t t24 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t22 * 8));
                    gm_t2lqu8jsj511d t25;
                    GM_ST(uint64_t, ((uint8_t *)&t25 + 0), t20);
                    GM_ST(uint64_t, ((uint8_t *)&t25 + 8), t24);
                    uint64_t t28 = GM_LD(uint64_t, (((uint8_t *)&t25) + 0));
                    gm_l4 = t28;
                    uint64_t t29 = GM_LD(uint64_t, (((uint8_t *)&t25) + 8));
                    gm_l5 = t29;
                    {
                        uint64_t t32 = gm_l5;
                        uint64_t t33 = gm_l4;
                        uint64_t t34 = (uintptr_t)t33;
                        uint64_t t35 = GM_LD(uintptr_t, (((uint8_t *)a1) + 8) + 8);
                        if (t34 >= (uintptr_t)(t35)) gm_trap_index(t34, t35, gm_srcue4ionbe9lgh, 727, 21);
                        uint64_t t36 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)a1) + 8)) + t34 * 8));
                        uint8_t t37 = (!(t32 == t36));
                        if (!t37) goto ie31;
                        {
                            uint8_t t39 = 0;
                            F.ret = ((uint8_t)0);
                            goto gm_fret;
                        }
                        goto ix31;
ie31:;
ix31:;
                    }
                    goto me26;
mn27:;
                    gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 726, 5);
me26:;
                }
lc14:;
                {
                    uint64_t t41 = gm_l3;
                    uint64_t r42; bool o43 = __builtin_add_overflow(t41, ((uint64_t)1ULL), &r42);
                    uint64_t t44 = r42;
                    gm_l3 = t44;
                }
            }
lb14:;
        }
    }
    {
        uint8_t t45 = 0;
        F.ret = ((uint8_t)1);
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) uint8_t gm_f_same_place_1r7qrqk_c(gm_tkaapcii86udk a0, gm_tkaapcii86udk a1) { return gm_f_same_place_1r7qrqk(&a0, &a1); }
__attribute__((aligned(4))) void gm_f_scalar_binary_bvc65kt(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_ts6oqppjh0ba3 a1, const void * a2, const void * a3, gm_t88rcucg1b7uk a4, gm_t88rcucg1b7uk a5, gm_tl874t2atnlh3 a6) {
    struct gm_fr_scalar_binary_bvc65kt F;
    void * gm_l0; /* m */
    gm_ts6oqppjh0ba3 gm_l1; /* op */
    gm_t88rcucg1b7uk gm_l4; /* lt */
    gm_t88rcucg1b7uk gm_l5; /* rt */
    gm_tl874t2atnlh3 gm_l6; /* span */
    unsigned __int128 gm_l7; /* x */
    unsigned __int128 gm_l8; /* y */
    int32_t gm_l9; /* c */
    double gm_l10; /* x */
    double gm_l11; /* y */
    double gm_l12; /* r */
    uint8_t gm_l13; /* x */
    uint8_t gm_l14; /* y */
    GM_STACK_CHECK(&F, 2671u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    memcpy(&F.l3, a3, sizeof F.l3);
    gm_l4 = a4;
    gm_l5 = a5;
    gm_l6 = a6;
    {
        uint8_t t8 = GM_LD(uint8_t, (((uint8_t *)a2) + 0));
        switch (t8) { case ((uint8_t)0ULL): goto ma3; case ((uint8_t)1ULL): goto ma4; case ((uint8_t)2ULL): goto ma5; case ((uint8_t)3ULL): goto ma6; case ((uint8_t)4ULL): goto ma6; case ((uint8_t)5ULL): goto ma6; case ((uint8_t)6ULL): goto ma6; case ((uint8_t)7ULL): goto ma6; case ((uint8_t)8ULL): goto ma6; case ((uint8_t)9ULL): goto ma6; case ((uint8_t)10ULL): goto ma6; case ((uint8_t)11ULL): goto ma6; case ((uint8_t)12ULL): goto ma6; case ((uint8_t)13ULL): goto ma6; default: goto ma6; }
ma3:;
        uint8_t t10 = GM_LD(uint8_t, (((uint8_t *)a2) + 0));
        if (t10 != ((uint8_t)0ULL)) goto mn9;
        unsigned __int128 t11 = GM_LD(unsigned __int128, (((uint8_t *)a2) + 16));
        gm_l7 = t11;
        {
            uint8_t t14 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 0));
            if (t14 != ((uint8_t)0ULL)) goto ie13;
            unsigned __int128 t15 = GM_LD(unsigned __int128, (((uint8_t *)&F.l3) + 16));
            gm_l8 = t15;
            {
                uint8_t t24 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                switch (t24) { case ((uint8_t)0ULL): goto ma22; case ((uint8_t)1ULL): goto ma22; case ((uint8_t)2ULL): goto ma22; case ((uint8_t)3ULL): goto ma22; case ((uint8_t)4ULL): goto ma22; case ((uint8_t)5ULL): goto ma22; case ((uint8_t)6ULL): goto ma22; case ((uint8_t)7ULL): goto ma19; case ((uint8_t)8ULL): goto ma20; case ((uint8_t)9ULL): goto ma21; case ((uint8_t)10ULL): goto ma18; case ((uint8_t)11ULL): goto ma18; case ((uint8_t)12ULL): goto ma18; case ((uint8_t)13ULL): goto ma18; default: goto ma22; }
ma18:;
                uint8_t t28 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                if (t28 != ((uint8_t)10ULL)) goto pa27;
                goto po26;
pa27:;
                uint8_t t30 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                if (t30 != ((uint8_t)11ULL)) goto pa29;
                goto po26;
pa29:;
                uint8_t t32 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                if (t32 != ((uint8_t)12ULL)) goto pa31;
                goto po26;
pa31:;
                uint8_t t34 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                if (t34 != ((uint8_t)13ULL)) goto pa33;
                goto po26;
pa33:;
                goto mn25;
po26:;
                {
                    void * t36 = gm_l0;
                    unsigned __int128 t37 = gm_l7;
                    gm_t88rcucg1b7uk t38 = gm_l4;
                    gm_tbci5ospk5nnn t39;
                    gm_f_big_of_1cq0qao((void *)&t39, (void *)NULL, cx, NULL, t36, t37, t38);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    void * t40 = gm_l0;
                    unsigned __int128 t41 = gm_l8;
                    gm_t88rcucg1b7uk t42 = gm_l5;
                    gm_tbci5ospk5nnn t43;
                    gm_f_big_of_1cq0qao((void *)&t43, (void *)NULL, cx, NULL, t40, t41, t42);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    int32_t t44 = gm_f_cmp_i1knrgu(&t39, &t43);
                    gm_l9 = t44;
                }
                {
                    uint8_t t45;
                    uint8_t t52 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                    switch (t52) { case ((uint8_t)0ULL): goto ma50; case ((uint8_t)1ULL): goto ma50; case ((uint8_t)2ULL): goto ma50; case ((uint8_t)3ULL): goto ma50; case ((uint8_t)4ULL): goto ma50; case ((uint8_t)5ULL): goto ma50; case ((uint8_t)6ULL): goto ma50; case ((uint8_t)7ULL): goto ma50; case ((uint8_t)8ULL): goto ma50; case ((uint8_t)9ULL): goto ma50; case ((uint8_t)10ULL): goto ma47; case ((uint8_t)11ULL): goto ma48; case ((uint8_t)12ULL): goto ma49; case ((uint8_t)13ULL): goto ma50; default: goto ma50; }
ma47:;
                    uint8_t t54 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                    if (t54 != ((uint8_t)10ULL)) goto mn53;
                    int32_t t55 = gm_l9;
                    uint8_t t56 = (t55 < ((int32_t)0LL));
                    t45 = t56;
                    goto me46;
mn53:;
ma48:;
                    uint8_t t58 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                    if (t58 != ((uint8_t)11ULL)) goto mn57;
                    int32_t t59 = gm_l9;
                    uint8_t t60 = (t59 <= ((int32_t)0LL));
                    t45 = t60;
                    goto me46;
mn57:;
ma49:;
                    uint8_t t62 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                    if (t62 != ((uint8_t)12ULL)) goto mn61;
                    int32_t t63 = gm_l9;
                    uint8_t t64 = (t63 > ((int32_t)0LL));
                    t45 = t64;
                    goto me46;
mn61:;
ma50:;
                    int32_t t66 = gm_l9;
                    uint8_t t67 = (t66 >= ((int32_t)0LL));
                    t45 = t67;
                    goto me46;
mn65:;
ma51:;
                    gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 129, 34);
me46:;
                    gm_t6evimt84lndq t68;
                    memset(&t68, 0, sizeof t68);
                    GM_ST(uint8_t, ((uint8_t *)&t68 + 0), ((uint8_t)2ULL));
                    GM_ST(uint8_t, ((uint8_t *)&t68 + 16), t45);
                    memset(&(*(gm_ttk92ufj2f6eh *)gm_out), 0, sizeof (*(gm_ttk92ufj2f6eh *)gm_out));
                    ((uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out))[0] = 0;
                    GM_ST(gm_t6evimt84lndq, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 16, t68);
                    goto gm_fdone;
                }
                goto me17;
mn25:;
ma19:;
                uint8_t t70 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                if (t70 != ((uint8_t)7ULL)) goto mn69;
                unsigned __int128 t71 = gm_l7;
                unsigned __int128 t72 = gm_l8;
                unsigned __int128 t73 = (unsigned __int128)(t71 & t72);
                gm_t6evimt84lndq t74;
                GM_ST(uint8_t, (uint8_t *)&t74 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t74 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t74 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t74 + 8, 0);
                GM_ST(uint64_t, (uint8_t *)&t74 + 32, 0);
                GM_ST(uint64_t, (uint8_t *)&t74 + 40, 0);
                GM_ST(uint8_t, ((uint8_t *)&t74 + 0), ((uint8_t)0ULL));
                GM_ST(unsigned __int128, ((uint8_t *)&t74 + 16), t73);
                memset(&(*(gm_ttk92ufj2f6eh *)gm_out), 0, sizeof (*(gm_ttk92ufj2f6eh *)gm_out));
                ((uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out))[0] = 0;
                GM_ST(gm_t6evimt84lndq, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 16, t74);
                goto gm_fdone;
                goto me17;
mn69:;
ma20:;
                uint8_t t76 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                if (t76 != ((uint8_t)8ULL)) goto mn75;
                unsigned __int128 t77 = gm_l7;
                unsigned __int128 t78 = gm_l8;
                unsigned __int128 t79 = (unsigned __int128)(t77 | t78);
                gm_t6evimt84lndq t80;
                GM_ST(uint8_t, (uint8_t *)&t80 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t80 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t80 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t80 + 8, 0);
                GM_ST(uint64_t, (uint8_t *)&t80 + 32, 0);
                GM_ST(uint64_t, (uint8_t *)&t80 + 40, 0);
                GM_ST(uint8_t, ((uint8_t *)&t80 + 0), ((uint8_t)0ULL));
                GM_ST(unsigned __int128, ((uint8_t *)&t80 + 16), t79);
                memset(&(*(gm_ttk92ufj2f6eh *)gm_out), 0, sizeof (*(gm_ttk92ufj2f6eh *)gm_out));
                ((uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out))[0] = 0;
                GM_ST(gm_t6evimt84lndq, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 16, t80);
                goto gm_fdone;
                goto me17;
mn75:;
ma21:;
                uint8_t t82 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                if (t82 != ((uint8_t)9ULL)) goto mn81;
                unsigned __int128 t83 = gm_l7;
                unsigned __int128 t84 = gm_l8;
                unsigned __int128 t85 = (unsigned __int128)(t83 ^ t84);
                gm_t6evimt84lndq t86;
                GM_ST(uint8_t, (uint8_t *)&t86 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t86 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t86 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t86 + 8, 0);
                GM_ST(uint64_t, (uint8_t *)&t86 + 32, 0);
                GM_ST(uint64_t, (uint8_t *)&t86 + 40, 0);
                GM_ST(uint8_t, ((uint8_t *)&t86 + 0), ((uint8_t)0ULL));
                GM_ST(unsigned __int128, ((uint8_t *)&t86 + 16), t85);
                memset(&(*(gm_ttk92ufj2f6eh *)gm_out), 0, sizeof (*(gm_ttk92ufj2f6eh *)gm_out));
                ((uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out))[0] = 0;
                GM_ST(gm_t6evimt84lndq, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 16, t86);
                goto gm_fdone;
                goto me17;
mn81:;
ma22:;
                void * t88 = gm_l0;
                gm_ts6oqppjh0ba3 t89 = gm_l1;
                unsigned __int128 t90 = gm_l7;
                unsigned __int128 t91 = gm_l8;
                gm_t88rcucg1b7uk t92 = gm_l4;
                gm_t88rcucg1b7uk t93 = gm_l5;
                gm_tl874t2atnlh3 t94 = gm_l6;
                gm_t8carr7mku484 t95;
                gm_f_int_op_jnafd2a((void *)&t95, (void *)NULL, cx, NULL, t88, t89, t90, t91, t92, t93, t94);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t95))[0] != 0)) {
                    gm_ttk92ufj2f6eh t96;
                    GM_ST(uint8_t, (uint8_t *)&t96 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t96 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t96 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t96 + 8, 0);
                    ((uint8_t *)&t96)[0] = 1;
                    GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t96 + 16), (*(gm_t39uai1ajk3qv *)(((uint8_t *)&t95) + 16)));
                    F.ret = t96;
                    goto gm_fret;
                }
                unsigned __int128 t97 = GM_LD(unsigned __int128, (((uint8_t *)&t95) + 16));
                gm_t6evimt84lndq t98;
                GM_ST(uint8_t, (uint8_t *)&t98 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t98 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t98 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t98 + 8, 0);
                GM_ST(uint64_t, (uint8_t *)&t98 + 32, 0);
                GM_ST(uint64_t, (uint8_t *)&t98 + 40, 0);
                GM_ST(uint8_t, ((uint8_t *)&t98 + 0), ((uint8_t)0ULL));
                GM_ST(unsigned __int128, ((uint8_t *)&t98 + 16), t97);
                memset(&(*(gm_ttk92ufj2f6eh *)gm_out), 0, sizeof (*(gm_ttk92ufj2f6eh *)gm_out));
                ((uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out))[0] = 0;
                GM_ST(gm_t6evimt84lndq, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 16, t98);
                goto gm_fdone;
                goto me17;
mn87:;
ma23:;
                gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 126, 13);
me17:;
            }
            goto ix13;
ie13:;
            {
                void * t100 = gm_l0;
                gm_tl874t2atnlh3 t101 = gm_l6;
                gm_b16_8 t102;
                GM_ST(void *, (uint8_t *)&t102, (void *)(gm_sqe4aodms9tfs));
                GM_ST(uintptr_t, (uint8_t *)&t102 + 8, (uintptr_t)(13));
                gm_t39uai1ajk3qv t103;
                gm_f_unsupported_2seq49l((void *)&t103, t100, GM_CONV(gm_cseahvg8v7p0vs, t101), t102);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 8, 0);
                ((uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out))[0] = 1;
                GM_ST(gm_t39uai1ajk3qv, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 16, t103);
                goto gm_fdone;
            }
ix13:;
        }
        goto me2;
mn9:;
ma4:;
        uint8_t t105 = GM_LD(uint8_t, (((uint8_t *)a2) + 0));
        if (t105 != ((uint8_t)1ULL)) goto mn104;
        double t106 = GM_LD(double, (((uint8_t *)a2) + 16));
        gm_l10 = t106;
        {
            uint8_t t109 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 0));
            if (t109 != ((uint8_t)1ULL)) goto ie108;
            double t110 = GM_LD(double, (((uint8_t *)&F.l3) + 16));
            gm_l11 = t110;
            {
                double t112;
                uint8_t t125 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                switch (t125) { case ((uint8_t)0ULL): goto ma114; case ((uint8_t)1ULL): goto ma115; case ((uint8_t)2ULL): goto ma116; case ((uint8_t)3ULL): goto ma123; case ((uint8_t)4ULL): goto ma117; case ((uint8_t)5ULL): goto ma118; case ((uint8_t)6ULL): goto ma123; case ((uint8_t)7ULL): goto ma123; case ((uint8_t)8ULL): goto ma123; case ((uint8_t)9ULL): goto ma123; case ((uint8_t)10ULL): goto ma119; case ((uint8_t)11ULL): goto ma120; case ((uint8_t)12ULL): goto ma121; case ((uint8_t)13ULL): goto ma122; default: goto ma123; }
ma114:;
                uint8_t t127 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                if (t127 != ((uint8_t)0ULL)) goto mn126;
                double t128 = gm_l10;
                double t129 = gm_l11;
                double t130 = (t128 + t129);
                t112 = t130;
                goto me113;
mn126:;
ma115:;
                uint8_t t132 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                if (t132 != ((uint8_t)1ULL)) goto mn131;
                double t133 = gm_l10;
                double t134 = gm_l11;
                double t135 = (t133 - t134);
                t112 = t135;
                goto me113;
mn131:;
ma116:;
                uint8_t t137 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                if (t137 != ((uint8_t)2ULL)) goto mn136;
                double t138 = gm_l10;
                double t139 = gm_l11;
                double t140 = (t138 * t139);
                t112 = t140;
                goto me113;
mn136:;
ma117:;
                uint8_t t142 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                if (t142 != ((uint8_t)4ULL)) goto mn141;
                double t143 = gm_l10;
                double t144 = gm_l11;
                double t145 = (t143 / t144);
                t112 = t145;
                goto me113;
mn141:;
ma118:;
                uint8_t t147 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                if (t147 != ((uint8_t)5ULL)) goto mn146;
                double t148 = gm_l10;
                double t149 = gm_l11;
                double t150 = gm_f_float_rem_24loign((void *)NULL, cx, NULL, t148, t149);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                t112 = t150;
                goto me113;
mn146:;
ma119:;
                uint8_t t152 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                if (t152 != ((uint8_t)10ULL)) goto mn151;
                double t153 = gm_l10;
                double t154 = gm_l11;
                uint8_t t155 = (t153 < t154);
                gm_t6evimt84lndq t156;
                memset(&t156, 0, sizeof t156);
                GM_ST(uint8_t, ((uint8_t *)&t156 + 0), ((uint8_t)2ULL));
                GM_ST(uint8_t, ((uint8_t *)&t156 + 16), t155);
                memset(&(*(gm_ttk92ufj2f6eh *)gm_out), 0, sizeof (*(gm_ttk92ufj2f6eh *)gm_out));
                ((uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out))[0] = 0;
                GM_ST(gm_t6evimt84lndq, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 16, t156);
                goto gm_fdone;
                goto me113;
mn151:;
ma120:;
                uint8_t t158 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                if (t158 != ((uint8_t)11ULL)) goto mn157;
                double t159 = gm_l10;
                double t160 = gm_l11;
                uint8_t t161 = (t159 <= t160);
                gm_t6evimt84lndq t162;
                memset(&t162, 0, sizeof t162);
                GM_ST(uint8_t, ((uint8_t *)&t162 + 0), ((uint8_t)2ULL));
                GM_ST(uint8_t, ((uint8_t *)&t162 + 16), t161);
                memset(&(*(gm_ttk92ufj2f6eh *)gm_out), 0, sizeof (*(gm_ttk92ufj2f6eh *)gm_out));
                ((uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out))[0] = 0;
                GM_ST(gm_t6evimt84lndq, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 16, t162);
                goto gm_fdone;
                goto me113;
mn157:;
ma121:;
                uint8_t t164 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                if (t164 != ((uint8_t)12ULL)) goto mn163;
                double t165 = gm_l10;
                double t166 = gm_l11;
                uint8_t t167 = (t165 > t166);
                gm_t6evimt84lndq t168;
                memset(&t168, 0, sizeof t168);
                GM_ST(uint8_t, ((uint8_t *)&t168 + 0), ((uint8_t)2ULL));
                GM_ST(uint8_t, ((uint8_t *)&t168 + 16), t167);
                memset(&(*(gm_ttk92ufj2f6eh *)gm_out), 0, sizeof (*(gm_ttk92ufj2f6eh *)gm_out));
                ((uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out))[0] = 0;
                GM_ST(gm_t6evimt84lndq, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 16, t168);
                goto gm_fdone;
                goto me113;
mn163:;
ma122:;
                uint8_t t170 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                if (t170 != ((uint8_t)13ULL)) goto mn169;
                double t171 = gm_l10;
                double t172 = gm_l11;
                uint8_t t173 = (t171 >= t172);
                gm_t6evimt84lndq t174;
                memset(&t174, 0, sizeof t174);
                GM_ST(uint8_t, ((uint8_t *)&t174 + 0), ((uint8_t)2ULL));
                GM_ST(uint8_t, ((uint8_t *)&t174 + 16), t173);
                memset(&(*(gm_ttk92ufj2f6eh *)gm_out), 0, sizeof (*(gm_ttk92ufj2f6eh *)gm_out));
                ((uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out))[0] = 0;
                GM_ST(gm_t6evimt84lndq, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 16, t174);
                goto gm_fdone;
                goto me113;
mn169:;
ma123:;
                void * t176 = gm_l0;
                gm_tl874t2atnlh3 t177 = gm_l6;
                gm_b16_8 t178;
                GM_ST(void *, (uint8_t *)&t178, (void *)(gm_sllgvbfukep8p));
                GM_ST(uintptr_t, (uint8_t *)&t178 + 8, (uintptr_t)(23));
                gm_t39uai1ajk3qv t179;
                gm_f_unsupported_2seq49l((void *)&t179, t176, GM_CONV(gm_cseahvg8v7p0vs, t177), t178);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 8, 0);
                ((uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out))[0] = 1;
                GM_ST(gm_t39uai1ajk3qv, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 16, t179);
                goto gm_fdone;
                goto me113;
mn175:;
ma124:;
                gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 144, 18);
me113:;
                gm_l12 = t112;
            }
            {
                void * t180 = gm_l0;
                double t181 = gm_l12;
                gm_t88rcucg1b7uk t182 = gm_l4;
                gm_t26jhpkpc0sda t183;
                gm_f_round_float_saju9fu((void *)&t183, (void *)NULL, cx, NULL, t180, t181, t182);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t183))[0] != 0)) {
                    gm_ttk92ufj2f6eh t184;
                    GM_ST(uint8_t, (uint8_t *)&t184 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t184 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t184 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t184 + 8, 0);
                    ((uint8_t *)&t184)[0] = 1;
                    GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t184 + 16), (*(gm_t39uai1ajk3qv *)(((uint8_t *)&t183) + 16)));
                    F.ret = t184;
                    goto gm_fret;
                }
                double t185 = GM_LD(double, (((uint8_t *)&t183) + 16));
                gm_t6evimt84lndq t186;
                memset(&t186, 0, sizeof t186);
                GM_ST(uint8_t, ((uint8_t *)&t186 + 0), ((uint8_t)1ULL));
                GM_ST(double, ((uint8_t *)&t186 + 16), t185);
                memset(&(*(gm_ttk92ufj2f6eh *)gm_out), 0, sizeof (*(gm_ttk92ufj2f6eh *)gm_out));
                ((uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out))[0] = 0;
                GM_ST(gm_t6evimt84lndq, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 16, t186);
                goto gm_fdone;
            }
            goto ix108;
ie108:;
            {
                void * t188 = gm_l0;
                gm_tl874t2atnlh3 t189 = gm_l6;
                gm_b16_8 t190;
                GM_ST(void *, (uint8_t *)&t190, (void *)(gm_sqe4aodms9tfs));
                GM_ST(uintptr_t, (uint8_t *)&t190 + 8, (uintptr_t)(13));
                gm_t39uai1ajk3qv t191;
                gm_f_unsupported_2seq49l((void *)&t191, t188, GM_CONV(gm_cseahvg8v7p0vs, t189), t190);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 8, 0);
                ((uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out))[0] = 1;
                GM_ST(gm_t39uai1ajk3qv, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 16, t191);
                goto gm_fdone;
            }
ix108:;
        }
        goto me2;
mn104:;
ma5:;
        uint8_t t193 = GM_LD(uint8_t, (((uint8_t *)a2) + 0));
        if (t193 != ((uint8_t)2ULL)) goto mn192;
        uint8_t t194 = GM_LD(uint8_t, (((uint8_t *)a2) + 16));
        gm_l13 = t194;
        {
            uint8_t t197 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 0));
            if (t197 != ((uint8_t)2ULL)) goto ie196;
            uint8_t t198 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 16));
            gm_l14 = t198;
            {
                uint8_t t206 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                switch (t206) { case ((uint8_t)0ULL): goto ma204; case ((uint8_t)1ULL): goto ma204; case ((uint8_t)2ULL): goto ma204; case ((uint8_t)3ULL): goto ma204; case ((uint8_t)4ULL): goto ma204; case ((uint8_t)5ULL): goto ma204; case ((uint8_t)6ULL): goto ma204; case ((uint8_t)7ULL): goto ma201; case ((uint8_t)8ULL): goto ma202; case ((uint8_t)9ULL): goto ma203; case ((uint8_t)10ULL): goto ma204; case ((uint8_t)11ULL): goto ma204; case ((uint8_t)12ULL): goto ma204; case ((uint8_t)13ULL): goto ma204; default: goto ma204; }
ma201:;
                uint8_t t208 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                if (t208 != ((uint8_t)7ULL)) goto mn207;
                uint8_t t209 = 0;
                uint8_t t210 = gm_l13;
                if (t210) {
                    uint8_t t211 = gm_l14;
                    t209 = t211;
                } else t209 = 0;
                gm_t6evimt84lndq t212;
                memset(&t212, 0, sizeof t212);
                GM_ST(uint8_t, ((uint8_t *)&t212 + 0), ((uint8_t)2ULL));
                GM_ST(uint8_t, ((uint8_t *)&t212 + 16), t209);
                memset(&(*(gm_ttk92ufj2f6eh *)gm_out), 0, sizeof (*(gm_ttk92ufj2f6eh *)gm_out));
                ((uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out))[0] = 0;
                GM_ST(gm_t6evimt84lndq, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 16, t212);
                goto gm_fdone;
                goto me200;
mn207:;
ma202:;
                uint8_t t214 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                if (t214 != ((uint8_t)8ULL)) goto mn213;
                uint8_t t215 = 0;
                uint8_t t216 = gm_l13;
                if (!t216) {
                    uint8_t t217 = gm_l14;
                    t215 = t217;
                } else t215 = 1;
                gm_t6evimt84lndq t218;
                memset(&t218, 0, sizeof t218);
                GM_ST(uint8_t, ((uint8_t *)&t218 + 0), ((uint8_t)2ULL));
                GM_ST(uint8_t, ((uint8_t *)&t218 + 16), t215);
                memset(&(*(gm_ttk92ufj2f6eh *)gm_out), 0, sizeof (*(gm_ttk92ufj2f6eh *)gm_out));
                ((uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out))[0] = 0;
                GM_ST(gm_t6evimt84lndq, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 16, t218);
                goto gm_fdone;
                goto me200;
mn213:;
ma203:;
                uint8_t t220 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                if (t220 != ((uint8_t)9ULL)) goto mn219;
                uint8_t t221 = gm_l13;
                uint8_t t222 = gm_l14;
                uint8_t t223 = (!(t221 == t222));
                gm_t6evimt84lndq t224;
                memset(&t224, 0, sizeof t224);
                GM_ST(uint8_t, ((uint8_t *)&t224 + 0), ((uint8_t)2ULL));
                GM_ST(uint8_t, ((uint8_t *)&t224 + 16), t223);
                memset(&(*(gm_ttk92ufj2f6eh *)gm_out), 0, sizeof (*(gm_ttk92ufj2f6eh *)gm_out));
                ((uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out))[0] = 0;
                GM_ST(gm_t6evimt84lndq, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 16, t224);
                goto gm_fdone;
                goto me200;
mn219:;
ma204:;
                void * t226 = gm_l0;
                gm_tl874t2atnlh3 t227 = gm_l6;
                gm_b16_8 t228;
                GM_ST(void *, (uint8_t *)&t228, (void *)(gm_sbsu8rcurukea));
                GM_ST(uintptr_t, (uint8_t *)&t228 + 8, (uintptr_t)(24));
                gm_t39uai1ajk3qv t229;
                gm_f_unsupported_2seq49l((void *)&t229, t226, GM_CONV(gm_cseahvg8v7p0vs, t227), t228);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 8, 0);
                ((uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out))[0] = 1;
                GM_ST(gm_t39uai1ajk3qv, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 16, t229);
                goto gm_fdone;
                goto me200;
mn225:;
ma205:;
                gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 160, 13);
me200:;
            }
            goto ix196;
ie196:;
            {
                void * t231 = gm_l0;
                gm_tl874t2atnlh3 t232 = gm_l6;
                gm_b16_8 t233;
                GM_ST(void *, (uint8_t *)&t233, (void *)(gm_sqe4aodms9tfs));
                GM_ST(uintptr_t, (uint8_t *)&t233 + 8, (uintptr_t)(13));
                gm_t39uai1ajk3qv t234;
                gm_f_unsupported_2seq49l((void *)&t234, t231, GM_CONV(gm_cseahvg8v7p0vs, t232), t233);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 8, 0);
                ((uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out))[0] = 1;
                GM_ST(gm_t39uai1ajk3qv, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 16, t234);
                goto gm_fdone;
            }
ix196:;
        }
        goto me2;
mn192:;
ma6:;
        void * t236 = gm_l0;
        gm_tl874t2atnlh3 t237 = gm_l6;
        gm_b16_8 t238;
        GM_ST(void *, (uint8_t *)&t238, (void *)(gm_s0dggtblbl7jq));
        GM_ST(uintptr_t, (uint8_t *)&t238 + 8, (uintptr_t)(29));
        gm_t39uai1ajk3qv t239;
        gm_f_unsupported_2seq49l((void *)&t239, t236, GM_CONV(gm_cseahvg8v7p0vs, t237), t238);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 8, 0);
        ((uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out))[0] = 1;
        GM_ST(gm_t39uai1ajk3qv, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 16, t239);
        goto gm_fdone;
        goto me2;
mn235:;
ma7:;
        gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 123, 5);
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
inline __attribute__((aligned(4))) void gm_f_unary_19d8qg7(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t9qkh0rsooc8d a1, void * a2, void * a3) {
    struct gm_fr_unary_19d8qg7 F;
    void * gm_l0; /* m */
    gm_t9qkh0rsooc8d gm_l1; /* op */
    void * gm_l2; /* x */
    void * gm_l3; /* e */
    void * gm_l5; /* a */
    gm_t88rcucg1b7uk gm_l6; /* lane */
    gm_b16_8 gm_l8; /* <tmp> */
    uint64_t gm_l9; /* <tmp> */
    GM_STACK_CHECK(&F, 971u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        void * t2 = gm_l0;
        void * t3 = gm_l2;
        gm_ttk92ufj2f6eh t4;
        gm_f_eval_2vbtp1v((void *)&t4, (void *)NULL, cx, NULL, t2, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t4))[0] != 0)) {
            gm_ttk92ufj2f6eh t5;
            GM_ST(uint8_t, (uint8_t *)&t5 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t5 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t5 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t5 + 8, 0);
            ((uint8_t *)&t5)[0] = 1;
            GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t5 + 16), (*(gm_t39uai1ajk3qv *)(((uint8_t *)&t4) + 16)));
            F.ret = t5;
            goto gm_fret;
        }
        F.l4 = (*(gm_t6evimt84lndq *)(((uint8_t *)&t4) + 16));
    }
    {
        uint8_t t7 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 0));
        if (t7 != ((uint8_t)3ULL)) goto ie6;
        void * t8 = GM_LD(void *, (((uint8_t *)&F.l4) + 16));
        gm_l5 = t8;
        {
            void * t10 = gm_l0;
            void * t11 = GM_LD(void *, (((uint8_t *)t10) + 0));
            void * t12 = (void *)(((uint8_t *)t11) + 8);
            void * t13 = gm_l3;
            gm_t88rcucg1b7uk t14 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t13) + 0));
            gm_tb7u19loo3j87 t15;
            gm_f_vector_parts_crrfopm((void *)&t15, t12, GM_CONV(gm_csguankko4lmhd, t14));
            if (!((((uint8_t *)&t15))[16] != 0)) gm_trap("unwrap of null", gm_srcue4ionbe9lgh, 74, 17);
            gm_tlcm4lvu3g1a8 t16 = GM_LD(gm_tlcm4lvu3g1a8, ((uint8_t *)&t15));
            gm_t88rcucg1b7uk t17 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t16) + 8));
            gm_l6 = t17;
        }
        {
            gm_f_list_0202sdh((void *)&F.l7, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            {
                void * t19 = gm_l5;
                gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)t19) + 0));
                gm_l8 = t20;
            }
            {
                gm_l9 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t22 = gm_l9;
                    uint64_t t23 = GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8);
                    uint8_t t24 = (t22 < t23);
                    if (!t24) goto lb21;
                    {
                        uint64_t t27 = gm_l9;
                        uint64_t t28 = (uintptr_t)t27;
                        uint64_t t29 = GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8);
                        gm_t6evimt84lndq t32 = GM_LD(gm_t6evimt84lndq, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l8)) + t28 * 48));
                        F.l10 = t32;
                        {
                            void * t34 = (void *)((uint8_t *)&F.l7);
                            void * t35 = gm_l0;
                            gm_t9qkh0rsooc8d t36 = gm_l1;
                            gm_t88rcucg1b7uk t37 = gm_l6;
                            void * t38 = gm_l2;
                            gm_tl874t2atnlh3 t39 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t38) + 72));
                            void * t40 = gm_l3;
                            gm_tl874t2atnlh3 t41 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t40) + 72));
                            gm_ttk92ufj2f6eh t42;
                            gm_f_unary_value_vi7cs6u((void *)&t42, (void *)NULL, cx, NULL, t35, t36, &(*(gm_t6evimt84lndq *)((uint8_t *)&F.l10)), t37, t39, t41);
                            if (gm_unw.kind) {
                                goto gm_ll25;
                            }
                            if (((((uint8_t *)&t42))[0] != 0)) {
                                gm_ttk92ufj2f6eh t43;
                                GM_ST(uint8_t, (uint8_t *)&t43 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t43 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t43 + 4, 0);
                                GM_ST(uint64_t, (uint8_t *)&t43 + 8, 0);
                                ((uint8_t *)&t43)[0] = 1;
                                GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t43 + 16), (*(gm_t39uai1ajk3qv *)(((uint8_t *)&t42) + 16)));
                                F.ret = t43;
                                goto gm_fret;
                            }
                            gm_tftb1odc739t5 t44 = gm_f_push_0pgacsj(t34, &(*(gm_t6evimt84lndq *)(((uint8_t *)&t42) + 16)));
                            if (((((uint8_t *)&t44))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t44) + 1, gm_srcue4ionbe9lgh, 76, 29);
                            gm_b0_1 t45 = GM_LD(gm_b0_1, (((uint8_t *)&t44) + 1));
                        }
                        goto me30;
mn31:;
                        gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 76, 9);
me30:;
                    }
lc21:;
                    {
                        uint64_t t47 = gm_l9;
                        uint64_t r48; bool o49 = __builtin_add_overflow(t47, ((uint64_t)1ULL), &r48);
                        uint64_t t50 = r48;
                        gm_l9 = t50;
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
            gm_b16_8 t51 = GM_LD(gm_b16_8, (((uint8_t *)&F.l7) + 0));
            gm_tnpifv20irnph t52;
            GM_ST(gm_b16_8, ((uint8_t *)&t52 + 0), t51);
            gm_tq7s98ihonkju t53 = gm_f_new_lcff781((void *)NULL, cx, NULL, t52, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_ktbnd3fuoao)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t53))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t53) + 8, gm_srcue4ionbe9lgh, 77, 21);
            void * t54 = GM_LD(void *, (((uint8_t *)&t53) + 8));
            gm_t6evimt84lndq t55;
            memset(&t55, 0, sizeof t55);
            GM_ST(uint8_t, ((uint8_t *)&t55 + 0), ((uint8_t)3ULL));
            GM_ST(void *, ((uint8_t *)&t55 + 16), t54);
            memset(&(*(gm_ttk92ufj2f6eh *)gm_out), 0, sizeof (*(gm_ttk92ufj2f6eh *)gm_out));
            ((uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out))[0] = 0;
            GM_ST(gm_t6evimt84lndq, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 16, t55);
            goto gm_fdone;
        }
        goto ix6;
ie6:;
ix6:;
    }
    {
        void * t56 = gm_l0;
        gm_t9qkh0rsooc8d t57 = gm_l1;
        void * t58 = gm_l2;
        gm_t88rcucg1b7uk t59 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t58) + 0));
        void * t60 = gm_l2;
        gm_tl874t2atnlh3 t61 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t60) + 72));
        void * t62 = gm_l3;
        gm_tl874t2atnlh3 t63 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t62) + 72));
        gm_f_unary_value_vi7cs6u((void *)&(*(gm_ttk92ufj2f6eh *)gm_out), (void *)NULL, cx, NULL, t56, t57, &(*(gm_t6evimt84lndq *)((uint8_t *)&F.l4)), t59, t61, t63);
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
__attribute__((aligned(4))) void gm_f_unary_value_vi7cs6u(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t9qkh0rsooc8d a1, const void * a2, gm_t88rcucg1b7uk a3, gm_tl874t2atnlh3 a4, gm_tl874t2atnlh3 a5) {
    struct gm_fr_unary_value_vi7cs6u F;
    void * gm_l0; /* m */
    gm_t9qkh0rsooc8d gm_l1; /* op */
    gm_t88rcucg1b7uk gm_l3; /* xt */
    gm_tl874t2atnlh3 gm_l4; /* xspan */
    gm_tl874t2atnlh3 gm_l5; /* span */
    void * gm_l6; /* x */
    void * gm_l7; /* e */
    uint8_t gm_l8; /* b */
    unsigned __int128 gm_l9; /* bits */
    double gm_l10; /* f */
    unsigned __int128 gm_l11; /* bits */
    GM_STACK_CHECK(&F, 2101u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_l5 = a5;
    {
        gm_t69m56meqefs8 t2;
        memset((uint8_t *)&t2 + 4, 0, 4);
        memset((uint8_t *)&t2 + 84, 0, 4);
        gm_t88rcucg1b7uk t3 = gm_l3;
        GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t2 + 0, t3);
        gm_teti411eujvf4 t4;
        memset(&t4, 0, sizeof t4);
        GM_ST(uint8_t, ((uint8_t *)&t4 + 0), ((uint8_t)7ULL));
        GM_ST(gm_teti411eujvf4, (uint8_t *)&t2 + 8, t4);
        gm_tl874t2atnlh3 t5 = gm_l4;
        GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t2 + 72, t5);
        gm_tq7s98ihonkju t6 = gm_f_new_ve8bb01_2((void *)NULL, cx, NULL, &t2, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kinrtga7ov4)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t6))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t6) + 8, gm_srcue4ionbe9lgh, 84, 10);
        void * t7 = GM_LD(void *, (((uint8_t *)&t6) + 8));
        gm_l6 = t7;
    }
    {
        gm_t69m56meqefs8 t8;
        memset((uint8_t *)&t8 + 4, 0, 4);
        memset((uint8_t *)&t8 + 84, 0, 4);
        gm_t88rcucg1b7uk t9 = gm_l3;
        GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t8 + 0, t9);
        gm_teti411eujvf4 t10;
        memset(&t10, 0, sizeof t10);
        GM_ST(uint8_t, ((uint8_t *)&t10 + 0), ((uint8_t)7ULL));
        GM_ST(gm_teti411eujvf4, (uint8_t *)&t8 + 8, t10);
        gm_tl874t2atnlh3 t11 = gm_l5;
        GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t8 + 72, t11);
        gm_tq7s98ihonkju t12 = gm_f_new_ve8bb01_2((void *)NULL, cx, NULL, &t8, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_ksrlu8s6v53)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t12))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t12) + 8, gm_srcue4ionbe9lgh, 85, 10);
        void * t13 = GM_LD(void *, (((uint8_t *)&t12) + 8));
        gm_l7 = t13;
    }
    {
        uint8_t t19 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        switch (t19) { case ((uint8_t)0ULL): goto ma17; case ((uint8_t)1ULL): goto ma16; case ((uint8_t)2ULL): goto ma15; default: goto ma18; }
ma15:;
        uint8_t t21 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t21 != ((uint8_t)2ULL)) goto mn20;
        {
            uint8_t t24 = GM_LD(uint8_t, (((uint8_t *)a2) + 0));
            if (t24 != ((uint8_t)2ULL)) goto ie23;
            uint8_t t25 = GM_LD(uint8_t, (((uint8_t *)a2) + 16));
            gm_l8 = t25;
            {
                uint8_t t27 = gm_l8;
                uint8_t t28 = (uint8_t)!t27;
                gm_t6evimt84lndq t29;
                memset(&t29, 0, sizeof t29);
                GM_ST(uint8_t, ((uint8_t *)&t29 + 0), ((uint8_t)2ULL));
                GM_ST(uint8_t, ((uint8_t *)&t29 + 16), t28);
                memset(&(*(gm_ttk92ufj2f6eh *)gm_out), 0, sizeof (*(gm_ttk92ufj2f6eh *)gm_out));
                ((uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out))[0] = 0;
                GM_ST(gm_t6evimt84lndq, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 16, t29);
                goto gm_fdone;
            }
            goto ix23;
ie23:;
            {
                void * t31 = gm_l0;
                void * t32 = gm_l7;
                gm_tl874t2atnlh3 t33 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t32) + 72));
                gm_b16_8 t34;
                GM_ST(void *, (uint8_t *)&t34, (void *)(gm_sqe4aodms9tfs));
                GM_ST(uintptr_t, (uint8_t *)&t34 + 8, (uintptr_t)(13));
                gm_t39uai1ajk3qv t35;
                gm_f_unsupported_2seq49l((void *)&t35, t31, GM_CONV(gm_cseahvg8v7p0vs, t33), t34);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 8, 0);
                ((uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out))[0] = 1;
                GM_ST(gm_t39uai1ajk3qv, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 16, t35);
                goto gm_fdone;
            }
ix23:;
        }
        goto me14;
mn20:;
ma16:;
        uint8_t t37 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t37 != ((uint8_t)1ULL)) goto mn36;
        {
            uint8_t t40 = GM_LD(uint8_t, (((uint8_t *)a2) + 0));
            if (t40 != ((uint8_t)0ULL)) goto ie39;
            unsigned __int128 t41 = GM_LD(unsigned __int128, (((uint8_t *)a2) + 16));
            gm_l9 = t41;
            {
                void * t43 = gm_l0;
                unsigned __int128 t44 = gm_l9;
                unsigned __int128 t45 = (unsigned __int128)~t44;
                void * t46 = gm_l7;
                gm_t88rcucg1b7uk t47 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t46) + 0));
                unsigned __int128 t48 = gm_f_wrap_bt8r149(t43, t45, GM_CONV(gm_csguankko4lmhd, t47));
                gm_t6evimt84lndq t49;
                GM_ST(uint8_t, (uint8_t *)&t49 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t49 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t49 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t49 + 8, 0);
                GM_ST(uint64_t, (uint8_t *)&t49 + 32, 0);
                GM_ST(uint64_t, (uint8_t *)&t49 + 40, 0);
                GM_ST(uint8_t, ((uint8_t *)&t49 + 0), ((uint8_t)0ULL));
                GM_ST(unsigned __int128, ((uint8_t *)&t49 + 16), t48);
                memset(&(*(gm_ttk92ufj2f6eh *)gm_out), 0, sizeof (*(gm_ttk92ufj2f6eh *)gm_out));
                ((uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out))[0] = 0;
                GM_ST(gm_t6evimt84lndq, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 16, t49);
                goto gm_fdone;
            }
            goto ix39;
ie39:;
            {
                void * t51 = gm_l0;
                void * t52 = gm_l7;
                gm_tl874t2atnlh3 t53 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t52) + 72));
                gm_b16_8 t54;
                GM_ST(void *, (uint8_t *)&t54, (void *)(gm_sqe4aodms9tfs));
                GM_ST(uintptr_t, (uint8_t *)&t54 + 8, (uintptr_t)(13));
                gm_t39uai1ajk3qv t55;
                gm_f_unsupported_2seq49l((void *)&t55, t51, GM_CONV(gm_cseahvg8v7p0vs, t53), t54);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 8, 0);
                ((uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out))[0] = 1;
                GM_ST(gm_t39uai1ajk3qv, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 16, t55);
                goto gm_fdone;
            }
ix39:;
        }
        goto me14;
mn36:;
ma17:;
        uint8_t t57 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t57 != ((uint8_t)0ULL)) goto mn56;
        uint8_t t63 = GM_LD(uint8_t, (((uint8_t *)a2) + 0));
        switch (t63) { case ((uint8_t)0ULL): goto ma60; case ((uint8_t)1ULL): goto ma59; case ((uint8_t)2ULL): goto ma61; case ((uint8_t)3ULL): goto ma61; case ((uint8_t)4ULL): goto ma61; case ((uint8_t)5ULL): goto ma61; case ((uint8_t)6ULL): goto ma61; case ((uint8_t)7ULL): goto ma61; case ((uint8_t)8ULL): goto ma61; case ((uint8_t)9ULL): goto ma61; case ((uint8_t)10ULL): goto ma61; case ((uint8_t)11ULL): goto ma61; case ((uint8_t)12ULL): goto ma61; case ((uint8_t)13ULL): goto ma61; default: goto ma61; }
ma59:;
        uint8_t t65 = GM_LD(uint8_t, (((uint8_t *)a2) + 0));
        if (t65 != ((uint8_t)1ULL)) goto mn64;
        double t66 = GM_LD(double, (((uint8_t *)a2) + 16));
        gm_l10 = t66;
        double t67 = gm_l10;
        double t68 = -t67;
        gm_t6evimt84lndq t69;
        memset(&t69, 0, sizeof t69);
        GM_ST(uint8_t, ((uint8_t *)&t69 + 0), ((uint8_t)1ULL));
        GM_ST(double, ((uint8_t *)&t69 + 16), t68);
        memset(&(*(gm_ttk92ufj2f6eh *)gm_out), 0, sizeof (*(gm_ttk92ufj2f6eh *)gm_out));
        ((uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out))[0] = 0;
        GM_ST(gm_t6evimt84lndq, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 16, t69);
        goto gm_fdone;
        goto me58;
mn64:;
ma60:;
        uint8_t t71 = GM_LD(uint8_t, (((uint8_t *)a2) + 0));
        if (t71 != ((uint8_t)0ULL)) goto mn70;
        unsigned __int128 t72 = GM_LD(unsigned __int128, (((uint8_t *)a2) + 16));
        gm_l11 = t72;
        {
            void * t75 = gm_l0;
            void * t76 = gm_l7;
            gm_t88rcucg1b7uk t77 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t76) + 0));
            gm_ttovhfee8mf28 t78 = gm_f_int_info_2r9qb5n(t75, GM_CONV(gm_csguankko4lmhd, t77));
            uint32_t t79 = GM_LD(uint32_t, (((uint8_t *)&t78) + 4));
            uint8_t t80 = (t79 <= ((uint32_t)63ULL));
            if (!t80) goto ie74;
            {
                void * t82 = gm_l0;
                void * t83 = gm_l0;
                unsigned __int128 t84 = gm_l11;
                void * t85 = gm_l6;
                gm_t88rcucg1b7uk t86 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t85) + 0));
                __int128 t87 = gm_f_int_value_6hmh8tj(t83, t84, GM_CONV(gm_csguankko4lmhd, t86));
                if (t87 == ((__int128)(((unsigned __int128)9223372036854775808u << 64) | 0u))) gm_trap("integer overflow in `-`", gm_srcue4ionbe9lgh, 98, 78);
                __int128 t88 = (__int128)((unsigned __int128)0 - (unsigned __int128)t87);
                gm_t9qkh0rsooc8d t89;
                GM_ST(uint8_t, ((uint8_t *)&t89 + 0), ((uint8_t)0ULL));
                void * t90 = gm_l7;
                gm_t88rcucg1b7uk t91 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t90) + 0));
                gm_b16_8 t92;
                GM_ST(void *, (uint8_t *)&t92, (void *)(gm_skolmnfc0ccbd));
                GM_ST(uintptr_t, (uint8_t *)&t92 + 8, (uintptr_t)(1));
                void * t93 = gm_l7;
                gm_tl874t2atnlh3 t94 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t93) + 72));
                gm_t8carr7mku484 t95;
                gm_f_fit_small_7o94i8v((void *)&t95, (void *)NULL, cx, NULL, t82, t88, t89, t91, t92, t94);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t95))[0] != 0)) {
                    gm_ttk92ufj2f6eh t96;
                    GM_ST(uint8_t, (uint8_t *)&t96 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t96 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t96 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t96 + 8, 0);
                    ((uint8_t *)&t96)[0] = 1;
                    GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t96 + 16), (*(gm_t39uai1ajk3qv *)(((uint8_t *)&t95) + 16)));
                    F.ret = t96;
                    goto gm_fret;
                }
                unsigned __int128 t97 = GM_LD(unsigned __int128, (((uint8_t *)&t95) + 16));
                gm_t6evimt84lndq t98;
                GM_ST(uint8_t, (uint8_t *)&t98 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t98 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t98 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t98 + 8, 0);
                GM_ST(uint64_t, (uint8_t *)&t98 + 32, 0);
                GM_ST(uint64_t, (uint8_t *)&t98 + 40, 0);
                GM_ST(uint8_t, ((uint8_t *)&t98 + 0), ((uint8_t)0ULL));
                GM_ST(unsigned __int128, ((uint8_t *)&t98 + 16), t97);
                memset(&(*(gm_ttk92ufj2f6eh *)gm_out), 0, sizeof (*(gm_ttk92ufj2f6eh *)gm_out));
                ((uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out))[0] = 0;
                GM_ST(gm_t6evimt84lndq, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 16, t98);
                goto gm_fdone;
            }
            goto ix74;
ie74:;
ix74:;
        }
        {
            void * t99 = gm_l0;
            void * t100 = gm_l0;
            unsigned __int128 t101 = gm_l11;
            void * t102 = gm_l6;
            gm_t88rcucg1b7uk t103 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t102) + 0));
            gm_tbci5ospk5nnn t104;
            gm_f_big_of_1cq0qao((void *)&t104, (void *)NULL, cx, NULL, t100, t101, t103);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tbci5ospk5nnn t105;
            gm_f_neg_agd5c5k((void *)&t105, (void *)NULL, cx, NULL, &t104);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t9qkh0rsooc8d t106;
            GM_ST(uint8_t, ((uint8_t *)&t106 + 0), ((uint8_t)0ULL));
            void * t107 = gm_l7;
            gm_t88rcucg1b7uk t108 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t107) + 0));
            gm_b16_8 t109;
            GM_ST(void *, (uint8_t *)&t109, (void *)(gm_skolmnfc0ccbd));
            GM_ST(uintptr_t, (uint8_t *)&t109 + 8, (uintptr_t)(1));
            void * t110 = gm_l7;
            gm_tl874t2atnlh3 t111 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t110) + 72));
            gm_t8carr7mku484 t112;
            gm_f_fit_8hvi4bv((void *)&t112, (void *)NULL, cx, NULL, t99, &t105, t106, t108, t109, t111);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t112))[0] != 0)) {
                gm_ttk92ufj2f6eh t113;
                GM_ST(uint8_t, (uint8_t *)&t113 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t113 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t113 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t113 + 8, 0);
                ((uint8_t *)&t113)[0] = 1;
                GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t113 + 16), (*(gm_t39uai1ajk3qv *)(((uint8_t *)&t112) + 16)));
                F.ret = t113;
                goto gm_fret;
            }
            unsigned __int128 t114 = GM_LD(unsigned __int128, (((uint8_t *)&t112) + 16));
            gm_t6evimt84lndq t115;
            GM_ST(uint8_t, (uint8_t *)&t115 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t115 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t115 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t115 + 8, 0);
            GM_ST(uint64_t, (uint8_t *)&t115 + 32, 0);
            GM_ST(uint64_t, (uint8_t *)&t115 + 40, 0);
            GM_ST(uint8_t, ((uint8_t *)&t115 + 0), ((uint8_t)0ULL));
            GM_ST(unsigned __int128, ((uint8_t *)&t115 + 16), t114);
            memset(&(*(gm_ttk92ufj2f6eh *)gm_out), 0, sizeof (*(gm_ttk92ufj2f6eh *)gm_out));
            ((uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out))[0] = 0;
            GM_ST(gm_t6evimt84lndq, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 16, t115);
            goto gm_fdone;
        }
        goto me58;
mn70:;
ma61:;
        void * t117 = gm_l0;
        void * t118 = gm_l7;
        gm_tl874t2atnlh3 t119 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t118) + 72));
        gm_b16_8 t120;
        GM_ST(void *, (uint8_t *)&t120, (void *)(gm_sqe4aodms9tfs));
        GM_ST(uintptr_t, (uint8_t *)&t120 + 8, (uintptr_t)(13));
        gm_t39uai1ajk3qv t121;
        gm_f_unsupported_2seq49l((void *)&t121, t117, GM_CONV(gm_cseahvg8v7p0vs, t119), t120);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 8, 0);
        ((uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out))[0] = 1;
        GM_ST(gm_t39uai1ajk3qv, (uint8_t *)&(*(gm_ttk92ufj2f6eh *)gm_out) + 16, t121);
        goto gm_fdone;
        goto me58;
mn116:;
ma62:;
        gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 95, 17);
me58:;
        goto me14;
mn56:;
ma18:;
        gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 86, 5);
me14:;
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
inline __attribute__((aligned(4))) void gm_f_variant_tag_4pbamin(void *gm_out, void * a0, gm_csguankko4lmhd a1, uint32_t a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_variant_tag_4pbamin F;
    void * gm_l0; /* m */
    gm_t88rcucg1b7uk gm_l1; /* t */
    uint32_t gm_l2; /* i */
    uint32_t gm_l3; /* id */
    gm_b16_8 gm_l4; /* variants */
    GM_STACK_CHECK_SHALLOW(&F, 414u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    gm_l2 = a2;
    {
        void * t3 = gm_l0;
        void * t4 = GM_LD(void *, (((uint8_t *)t3) + 0));
        void * t5 = (void *)(((uint8_t *)t4) + 8);
        gm_t88rcucg1b7uk t6 = gm_l1;
        gm_t53at80pfdus3 t7;
        gm_f_kind_jcnt4hm((void *)&t7, t5, GM_CONV(gm_csguankko4lmhd, t6));
        uint8_t t8 = GM_LD(uint8_t, (((uint8_t *)&t7) + 0));
        if (t8 != ((uint8_t)17ULL)) goto ie2;
        uint32_t t9 = GM_LD(uint32_t, ((((uint8_t *)&t7) + 8) + 0));
        gm_l3 = t9;
        {
            uint32_t t12 = gm_l3;
            uint64_t t13 = (uint64_t)t12;
            uint64_t t14 = (uintptr_t)t13;
            void * t15 = gm_l0;
            void * t16 = GM_LD(void *, (((uint8_t *)t15) + 0));
            uint64_t t17 = GM_LD(uintptr_t, ((((uint8_t *)t16) + 1088) + 0) + 8);
            if (t14 >= (uintptr_t)(t17)) gm_trap_index(t14, t17, gm_srcue4ionbe9lgh, 645, 28);
            uint8_t t18 = GM_LD(uint8_t, ((((uint8_t *)GM_LD(void *, ((((uint8_t *)t16) + 1088) + 0)) + t14 * 184) + 24) + 0));
            if (t18 != ((uint8_t)1ULL)) goto ie11;
            gm_b16_8 t19 = GM_LD(gm_b16_8, (((((uint8_t *)GM_LD(void *, ((((uint8_t *)t16) + 1088) + 0)) + t14 * 184) + 24) + 8) + 8));
            gm_l4 = t19;
            {
                uint32_t t22 = gm_l2;
                uint64_t t23 = (uint64_t)t22;
                uint64_t t24 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                uint8_t t25 = (t23 >= t24);
                if (!t25) goto ie21;
                {
                    gm_thrdpodrs8pgt t27;
                    memset(&t27, 0, sizeof t27);
                    uint8_t t28 = 0;
                    F.ret = t27;
                    goto gm_fret;
                }
                goto ix21;
ie21:;
ix21:;
            }
            {
                uint32_t t29 = gm_l2;
                uint64_t t30 = (uint64_t)t29;
                uint64_t t31 = (uintptr_t)t30;
                uint64_t t32 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                if (t31 >= (uintptr_t)(t32)) gm_trap_index(t31, t32, gm_srcue4ionbe9lgh, 647, 12);
                const uint8_t *t33 = (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t31 * 72) + 24);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_thrdpodrs8pgt *)gm_out) + 49, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_thrdpodrs8pgt *)gm_out) + 50, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_thrdpodrs8pgt *)gm_out) + 52, 0);
                ((uint8_t *)&(*(gm_thrdpodrs8pgt *)gm_out))[48] = 1;
                memmove((uint8_t *)&(*(gm_thrdpodrs8pgt *)gm_out) + 0, t33, 48);
                goto gm_fdone;
            }
            goto ix11;
ie11:;
            {
                gm_thrdpodrs8pgt t35;
                memset(&t35, 0, sizeof t35);
                uint8_t t36 = 0;
                F.ret = t35;
                goto gm_fret;
            }
ix11:;
        }
        goto ix2;
ie2:;
        {
            gm_thrdpodrs8pgt t38;
            memset(&t38, 0, sizeof t38);
            uint8_t t39 = 0;
            F.ret = t38;
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
__attribute__((aligned(4))) gm_thrdpodrs8pgt gm_f_variant_tag_4pbamin_c(void * a0, gm_csguankko4lmhd a1, uint32_t a2) { gm_thrdpodrs8pgt gm_r; gm_f_variant_tag_4pbamin(&gm_r, a0, a1, a2); return gm_r; }
extern __attribute__((aligned(4))) unsigned __int128 gm_f_wrap_bt8r149(void * a0, unsigned __int128 a1, gm_csguankko4lmhd a2);
