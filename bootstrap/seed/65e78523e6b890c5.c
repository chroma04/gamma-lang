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
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint32_t f0; uint8_t f1; uint8_t pe[3]; } gm_t9805qtonbsag;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { void * f0; void * f1; void * f2; uint8_t f3; uint8_t pe[7]; } gm_tao63pk3r18v5;
typedef struct { uint32_t f0; uint8_t p1[4]; uint64_t f1; } gm_tb31k5l794t7p;
typedef struct { uint32_t f0; uint32_t f1; } gm_tcc1rq4n1avb2;
typedef struct { uint16_t f0; uint16_t f1; uint16_t f2; uint16_t f3; uint16_t f4; } gm_tdbjst0cng0lo;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint8_t f0; uint8_t p1[7]; void * f1; } gm_tgtub0v94d5u5;
typedef struct { uint8_t f0; uint8_t p1[1]; uint16_t f1; } gm_tjcj4j2gbh6c8;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { void * f0; void * f1; uint32_t f2; uint32_t f3; } gm_tmm67r2c94d5j;
typedef struct { uint8_t f0; uint8_t f1; } gm_tqs5p4n2tq19v;
_Static_assert(sizeof(gm_cseahvg8v7p0vs) == 12 && _Alignof(gm_cseahvg8v7p0vs) == 4 && offsetof(gm_cseahvg8v7p0vs, f0) == 0 && offsetof(gm_cseahvg8v7p0vs, f1) == 4 && offsetof(gm_cseahvg8v7p0vs, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_csguankko4lmhd) == 4 && _Alignof(gm_csguankko4lmhd) == 4 && offsetof(gm_csguankko4lmhd, f0) == 0, "layout of `Mutex`");
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_t08olq97rscn3;
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; gm_t9qkh0rsooc8d f2; uint8_t p3[3]; gm_tf5srh4mp4b3i f3; } gm_t0mglksrkfa5e;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; gm_tl874t2atnlh3 f2; void * f3; gm_t9805qtonbsag f4; uint8_t f5; uint8_t pe[7]; } gm_t5v0c8ia6a5mo;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_t79rbdv4rafe5;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; void * f1; } gm_t8ghgl2jj0qha;
_Static_assert(sizeof(gm_t9805qtonbsag) == 8 && _Alignof(gm_t9805qtonbsag) == 4 && offsetof(gm_t9805qtonbsag, f0) == 0 && offsetof(gm_t9805qtonbsag, f1) == 4, "layout of `?u32`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
_Static_assert(sizeof(gm_tao63pk3r18v5) == 32 && _Alignof(gm_tao63pk3r18v5) == 8 && offsetof(gm_tao63pk3r18v5, f0) == 0 && offsetof(gm_tao63pk3r18v5, f1) == 8 && offsetof(gm_tao63pk3r18v5, f2) == 16 && offsetof(gm_tao63pk3r18v5, f3) == 24, "layout of `SliceExpr`");
_Static_assert(sizeof(gm_tb31k5l794t7p) == 16 && _Alignof(gm_tb31k5l794t7p) == 8 && offsetof(gm_tb31k5l794t7p, f0) == 0 && offsetof(gm_tb31k5l794t7p, f1) == 8, "layout of `Char`");
typedef struct { gm_b16_8 f0; uint32_t f1; uint32_t f2; gm_b16_8 f3; } gm_tb40qn7hk0tl9;
typedef struct { uint8_t f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; } gm_tbngattl1fo51;
typedef struct { uint8_t f0; uint8_t p1[3]; gm_tl874t2atnlh3 f1; } gm_tbvif938ns15l;
_Static_assert(sizeof(gm_tcc1rq4n1avb2) == 8 && _Alignof(gm_tcc1rq4n1avb2) == 4 && offsetof(gm_tcc1rq4n1avb2, f0) == 0 && offsetof(gm_tcc1rq4n1avb2, f1) == 4, "layout of `LineCol`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tcmkompbl09il;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; gm_t88rcucg1b7uk f2; } gm_tcmtrmhj7ak4u;
_Static_assert(sizeof(gm_tdbjst0cng0lo) == 10 && _Alignof(gm_tdbjst0cng0lo) == 2 && offsetof(gm_tdbjst0cng0lo, f0) == 0 && offsetof(gm_tdbjst0cng0lo, f1) == 2 && offsetof(gm_tdbjst0cng0lo, f2) == 4 && offsetof(gm_tdbjst0cng0lo, f3) == 6 && offsetof(gm_tdbjst0cng0lo, f4) == 8, "layout of `(u16, u16, u16, u16, u16)`");
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; gm_b16_8 f1; gm_b16_8 f2; void * f3; gm_t9805qtonbsag f4; uint8_t f5; uint8_t pe[7]; } gm_tdgfduqjheqdm;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; } gm_tgglga8ro3isc;
typedef struct { gm_tb31k5l794t7p f0; uint8_t f1; uint8_t pe[7]; } gm_tgmjda3f8g6hg;
_Static_assert(sizeof(gm_tgtub0v94d5u5) == 16 && _Alignof(gm_tgtub0v94d5u5) == 8 && offsetof(gm_tgtub0v94d5u5, f0) == 0 && offsetof(gm_tgtub0v94d5u5, f1) == 8, "layout of `(is_const: bool, inner: ^Expr)`");
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; gm_b16_8 f2; } gm_thognv9odov03;
typedef struct { gm_b16_8 f0; gm_t9qkh0rsooc8d f1; uint8_t pe[7]; } gm_tibupt4dmcqai;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
_Static_assert(sizeof(gm_tjcj4j2gbh6c8) == 4 && _Alignof(gm_tjcj4j2gbh6c8) == 2 && offsetof(gm_tjcj4j2gbh6c8, f0) == 0 && offsetof(gm_tjcj4j2gbh6c8, f1) == 2, "layout of `(signed: bool, bits: u16)`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_tf5srh4mp4b3i f1; uint8_t f2; uint8_t pe[7]; } gm_tjpqvfd72o62u;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t f1; uint8_t pe[3]; } gm_tkn42u7na3t81;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
typedef struct { void * f0; gm_b16_8 f1; uint8_t f2; uint8_t pe[7]; } gm_tlljg61a2ctgq;
_Static_assert(sizeof(gm_tmm67r2c94d5j) == 24 && _Alignof(gm_tmm67r2c94d5j) == 8 && offsetof(gm_tmm67r2c94d5j, f0) == 0 && offsetof(gm_tmm67r2c94d5j, f1) == 8 && offsetof(gm_tmm67r2c94d5j, f2) == 16 && offsetof(gm_tmm67r2c94d5j, f3) == 20, "layout of `Printer`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t88rcucg1b7uk v0; } f1; } gm_tnnjeerolos0a;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
typedef struct { gm_b16_8 f0; void * f1; } gm_tpe1hv3fekn05;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; void * v1; } f1; } gm_tq7s98ihonkju;
_Static_assert(sizeof(gm_tqs5p4n2tq19v) == 2 && _Alignof(gm_tqs5p4n2tq19v) == 1 && offsetof(gm_tqs5p4n2tq19v, f0) == 0 && offsetof(gm_tqs5p4n2tq19v, f1) == 1, "layout of `(u8, u8)`");
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_t9qkh0rsooc8d v3; } f1; } gm_ts6oqppjh0ba3;
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t f2; uint8_t p3[3]; gm_tl874t2atnlh3 f3; uint8_t pe[4]; } gm_tsoie1ppgdvcg;
typedef struct { void * f0; uint32_t f1; gm_tl874t2atnlh3 f2; } gm_tupk62cr6lbdu;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; void * f2; gm_t9qkh0rsooc8d f3; uint8_t p4[7]; void * f4; gm_tl874t2atnlh3 f5; uint8_t pe[4]; } gm_tv2uf4leil5hq;
_Static_assert(sizeof(gm_t08olq97rscn3) == 32 && _Alignof(gm_t08olq97rscn3) == 8 && offsetof(gm_t08olq97rscn3, f0) == 0 && offsetof(gm_t08olq97rscn3, f1) == 16, "layout of `File`");
_Static_assert(sizeof(gm_t0mglksrkfa5e) == 40 && _Alignof(gm_t0mglksrkfa5e) == 8 && offsetof(gm_t0mglksrkfa5e, f0) == 0 && offsetof(gm_t0mglksrkfa5e, f1) == 16 && offsetof(gm_t0mglksrkfa5e, f2) == 20 && offsetof(gm_t0mglksrkfa5e, f3) == 24, "layout of `FieldDef`");
typedef struct { gm_t08olq97rscn3 f0; uint8_t f1; uint8_t pe[7]; } gm_t1s75i08qtptd;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tcmkompbl09il v0; gm_tiilmfd16v3fc v1; gm_b16_8 v2; } f1; } gm_t428ue2u3hf5g;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t p3[4]; gm_trqfvmjtt4sij f3; gm_trqfvmjtt4sij f4; gm_trqfvmjtt4sij f5; gm_th2lqbo77pvsc f6; } gm_t4gut2c9fj1kt;
_Static_assert(sizeof(gm_t5v0c8ia6a5mo) == 56 && _Alignof(gm_t5v0c8ia6a5mo) == 8 && offsetof(gm_t5v0c8ia6a5mo, f0) == 0 && offsetof(gm_t5v0c8ia6a5mo, f1) == 16 && offsetof(gm_t5v0c8ia6a5mo, f2) == 20 && offsetof(gm_t5v0c8ia6a5mo, f3) == 32 && offsetof(gm_t5v0c8ia6a5mo, f4) == 40 && offsetof(gm_t5v0c8ia6a5mo, f5) == 48, "layout of `ParamInfo`");
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_t79rbdv4rafe5) == 40 && _Alignof(gm_t79rbdv4rafe5) == 8 && offsetof(gm_t79rbdv4rafe5, f0) == 0 && offsetof(gm_t79rbdv4rafe5, f1) == 8 && offsetof(gm_t79rbdv4rafe5, f2) == 24, "layout of `Block`");
typedef struct { void * f0; gm_b16_8 f1; gm_th2lqbo77pvsc f2; gm_tl874t2atnlh3 f3; uint8_t pe[4]; } gm_t7bjbmukv26e7;
typedef struct { gm_tbvif938ns15l f0; uint8_t f1; uint8_t pe[3]; } gm_t84dv0cg48dut;
typedef struct { gm_tjpqvfd72o62u f0; uint8_t f1; uint8_t pe[7]; } gm_t8cc1mtbrhlpt;
_Static_assert(sizeof(gm_t8ghgl2jj0qha) == 16 && _Alignof(gm_t8ghgl2jj0qha) == 8 && offsetof(gm_t8ghgl2jj0qha, f0) == 0 && offsetof(gm_t8ghgl2jj0qha, f1) == 8, "layout of `(op: UnOp, operand: ^Expr)`");
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; gm_tnnjeerolos0a f2; uint8_t pe[4]; } gm_t8nckeqfenl8a;
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
_Static_assert(sizeof(gm_tb40qn7hk0tl9) == 40 && _Alignof(gm_tb40qn7hk0tl9) == 8 && offsetof(gm_tb40qn7hk0tl9, f0) == 0 && offsetof(gm_tb40qn7hk0tl9, f1) == 16 && offsetof(gm_tb40qn7hk0tl9, f2) == 20 && offsetof(gm_tb40qn7hk0tl9, f3) == 24, "layout of `SourceLocation`");
typedef struct { uint8_t f0; uint8_t p1[7]; gm_trqfvmjtt4sij f1; } gm_tbci5ospk5nnn;
_Static_assert(sizeof(gm_tbngattl1fo51) == 8 && _Alignof(gm_tbngattl1fo51) == 4 && offsetof(gm_tbngattl1fo51, f0) == 0 && offsetof(gm_tbngattl1fo51, f1) == 4, "layout of `(is_const: bool, pointee: Ty)`");
_Static_assert(sizeof(gm_tbvif938ns15l) == 16 && _Alignof(gm_tbvif938ns15l) == 4 && offsetof(gm_tbvif938ns15l, f0) == 0 && offsetof(gm_tbvif938ns15l, f1) == 4, "layout of `(win64: bool, span: Span)`");
typedef struct { gm_tibupt4dmcqai f0; gm_tl874t2atnlh3 f1; uint8_t p2[4]; gm_t79rbdv4rafe5 f2; uint8_t f3; uint8_t pe[7]; } gm_tcfiotghd1s5k;
_Static_assert(sizeof(gm_tcmkompbl09il) == 24 && _Alignof(gm_tcmkompbl09il) == 8 && offsetof(gm_tcmkompbl09il, f0) == 0 && offsetof(gm_tcmkompbl09il, f1) == 8, "layout of `(ty: Ty, bytes: string)`");
_Static_assert(sizeof(gm_tcmtrmhj7ak4u) == 12 && _Alignof(gm_tcmtrmhj7ak4u) == 4 && offsetof(gm_tcmtrmhj7ak4u, f0) == 0 && offsetof(gm_tcmtrmhj7ak4u, f1) == 4 && offsetof(gm_tcmtrmhj7ak4u, f2) == 8, "layout of `(op: LenOp, lhs: Ty, rhs: Ty)`");
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t p2[4]; gm_tnpifv20irnph f2; uint8_t f3; uint8_t pe[7]; } gm_tddtjfggdat0d;
_Static_assert(sizeof(gm_tdgfduqjheqdm) == 64 && _Alignof(gm_tdgfduqjheqdm) == 8 && offsetof(gm_tdgfduqjheqdm, f0) == 0 && offsetof(gm_tdgfduqjheqdm, f1) == 8 && offsetof(gm_tdgfduqjheqdm, f2) == 24 && offsetof(gm_tdgfduqjheqdm, f3) == 40 && offsetof(gm_tdgfduqjheqdm, f4) == 48 && offsetof(gm_tdgfduqjheqdm, f5) == 56, "layout of `FnDef`");
typedef struct { gm_t08olq97rscn3 f0; gm_th2lqbo77pvsc f1; } gm_tet9guhev5r4u;
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_tgglga8ro3isc) == 8 && _Alignof(gm_tgglga8ro3isc) == 4 && offsetof(gm_tgglga8ro3isc, f0) == 0 && offsetof(gm_tgglga8ro3isc, f1) == 4, "layout of `(err: Ty, ok: Ty)`");
_Static_assert(sizeof(gm_tgmjda3f8g6hg) == 24 && _Alignof(gm_tgmjda3f8g6hg) == 8 && offsetof(gm_tgmjda3f8g6hg, f0) == 0 && offsetof(gm_tgmjda3f8g6hg, f1) == 16, "layout of `?(rune: u32, len: usize)`");
typedef struct { uint64_t f0; gm_t5v0c8ia6a5mo f1; } gm_tgsvmddj3e85e;
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
_Static_assert(sizeof(gm_thognv9odov03) == 40 && _Alignof(gm_thognv9odov03) == 8 && offsetof(gm_thognv9odov03, f0) == 0 && offsetof(gm_thognv9odov03, f1) == 8 && offsetof(gm_thognv9odov03, f2) == 24, "layout of `ForeignFn`");
_Static_assert(sizeof(gm_tibupt4dmcqai) == 24 && _Alignof(gm_tibupt4dmcqai) == 8 && offsetof(gm_tibupt4dmcqai, f0) == 0 && offsetof(gm_tibupt4dmcqai, f1) == 16, "layout of `StrLit`");
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
typedef struct { void * f0; gm_t08olq97rscn3 f1; } gm_tioie2ju3q4ul;
_Static_assert(sizeof(gm_tjpqvfd72o62u) == 32 && _Alignof(gm_tjpqvfd72o62u) == 8 && offsetof(gm_tjpqvfd72o62u, f0) == 0 && offsetof(gm_tjpqvfd72o62u, f1) == 8 && offsetof(gm_tjpqvfd72o62u, f2) == 24, "layout of `SoaInfo`");
typedef struct { gm_t08olq97rscn3 f0; gm_th2lqbo77pvsc f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_tkacitepg5fk0;
_Static_assert(sizeof(gm_tkn42u7na3t81) == 8 && _Alignof(gm_tkn42u7na3t81) == 4 && offsetof(gm_tkn42u7na3t81, f0) == 0 && offsetof(gm_tkn42u7na3t81, f1) == 4, "layout of `?Ty`");
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t p2[7]; void * f2; gm_th2lqbo77pvsc f3; gm_tl874t2atnlh3 f4; uint8_t pe[4]; } gm_tlivgedjs5ps3;
_Static_assert(sizeof(gm_tlljg61a2ctgq) == 32 && _Alignof(gm_tlljg61a2ctgq) == 8 && offsetof(gm_tlljg61a2ctgq, f0) == 0 && offsetof(gm_tlljg61a2ctgq, f1) == 8 && offsetof(gm_tlljg61a2ctgq, f2) == 24, "layout of `(callee: ^Expr, args: []Arg, trailing_comma: bool)`");
_Static_assert(sizeof(gm_tnnjeerolos0a) == 8 && _Alignof(gm_tnnjeerolos0a) == 4 && offsetof(gm_tnnjeerolos0a, f0) == 0 && offsetof(gm_tnnjeerolos0a, f1) == 4, "layout of `ArrayKind`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t p2[7]; gm_tf5srh4mp4b3i f2; gm_tkn42u7na3t81 f3; } gm_tpautr4br7h3l;
_Static_assert(sizeof(gm_tpe1hv3fekn05) == 24 && _Alignof(gm_tpe1hv3fekn05) == 8 && offsetof(gm_tpe1hv3fekn05, f0) == 0 && offsetof(gm_tpe1hv3fekn05, f1) == 16, "layout of `(attrs: []Attribute, inner: ^Expr)`");
typedef struct { uint64_t f0; gm_b16_8 f1; gm_t88rcucg1b7uk f2; uint8_t p3[4]; uint64_t f3; void * f4; uint8_t f5; uint8_t p6[7]; gm_th2lqbo77pvsc f6; gm_tf5srh4mp4b3i f7; void * f8; } gm_tq7qjpkmqmc5s;
_Static_assert(sizeof(gm_tq7s98ihonkju) == 16 && _Alignof(gm_tq7s98ihonkju) == 8 && offsetof(gm_tq7s98ihonkju, f0) == 0 && offsetof(gm_tq7s98ihonkju, f1) == 8, "layout of `AllocError!^HandlerDecl`");
typedef struct { gm_tiilmfd16v3fc f0; uint8_t f1; uint8_t pe[7]; } gm_tqu3m2l3n8mnc;
typedef struct { gm_ts6oqppjh0ba3 f0; uint8_t p1[6]; void * f1; void * f2; } gm_trl9d7m1k5lbd;
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
typedef struct { gm_t08olq97rscn3 f0; void * f1; } gm_ts686ckv8ehm2;
_Static_assert(sizeof(gm_ts6oqppjh0ba3) == 2 && _Alignof(gm_ts6oqppjh0ba3) == 1 && offsetof(gm_ts6oqppjh0ba3, f0) == 0 && offsetof(gm_ts6oqppjh0ba3, f1) == 1, "layout of `BinOp`");
_Static_assert(sizeof(gm_tsoie1ppgdvcg) == 40 && _Alignof(gm_tsoie1ppgdvcg) == 8 && offsetof(gm_tsoie1ppgdvcg, f0) == 0 && offsetof(gm_tsoie1ppgdvcg, f1) == 16 && offsetof(gm_tsoie1ppgdvcg, f2) == 20 && offsetof(gm_tsoie1ppgdvcg, f3) == 24, "layout of `LocalDecl`");
_Static_assert(sizeof(gm_tupk62cr6lbdu) == 24 && _Alignof(gm_tupk62cr6lbdu) == 8 && offsetof(gm_tupk62cr6lbdu, f0) == 0 && offsetof(gm_tupk62cr6lbdu, f1) == 8 && offsetof(gm_tupk62cr6lbdu, f2) == 12, "layout of `(base: ^Expr, index: u32, index_span: Span)`");
_Static_assert(sizeof(gm_tv2uf4leil5hq) == 64 && _Alignof(gm_tv2uf4leil5hq) == 8 && offsetof(gm_tv2uf4leil5hq, f0) == 0 && offsetof(gm_tv2uf4leil5hq, f1) == 8 && offsetof(gm_tv2uf4leil5hq, f2) == 24 && offsetof(gm_tv2uf4leil5hq, f3) == 32 && offsetof(gm_tv2uf4leil5hq, f4) == 40 && offsetof(gm_tv2uf4leil5hq, f5) == 48, "layout of `Binding`");
_Static_assert(sizeof(gm_t1s75i08qtptd) == 40 && _Alignof(gm_t1s75i08qtptd) == 8 && offsetof(gm_t1s75i08qtptd, f0) == 0 && offsetof(gm_t1s75i08qtptd, f1) == 32, "layout of `?Ident`");
_Static_assert(sizeof(gm_t428ue2u3hf5g) == 32 && _Alignof(gm_t428ue2u3hf5g) == 8 && offsetof(gm_t428ue2u3hf5g, f0) == 0 && offsetof(gm_t428ue2u3hf5g, f1) == 8, "layout of `ConstKey`");
_Static_assert(sizeof(gm_t4gut2c9fj1kt) == 184 && _Alignof(gm_t4gut2c9fj1kt) == 8 && offsetof(gm_t4gut2c9fj1kt, f0) == 0 && offsetof(gm_t4gut2c9fj1kt, f1) == 8 && offsetof(gm_t4gut2c9fj1kt, f2) == 24 && offsetof(gm_t4gut2c9fj1kt, f3) == 40 && offsetof(gm_t4gut2c9fj1kt, f4) == 80 && offsetof(gm_t4gut2c9fj1kt, f5) == 120 && offsetof(gm_t4gut2c9fj1kt, f6) == 160, "layout of `Diagnostic`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tjcj4j2gbh6c8 v0; uint16_t v1; gm_tbngattl1fo51 v2; gm_tbngattl1fo51 v3; gm_tbngattl1fo51 v4; gm_t8nckeqfenl8a v5; gm_t6j6smj7qoctu v6; gm_t88rcucg1b7uk v7; gm_tgglga8ro3isc v8; gm_tddtjfggdat0d v9; gm_tddtjfggdat0d v10; gm_tiilmfd16v3fc v11; gm_t88rcucg1b7uk v12; uint32_t v13; gm_tgglga8ro3isc v14; gm_t428ue2u3hf5g v15; gm_tcmtrmhj7ak4u v16; uint32_t v17; gm_tnpifv20irnph v18; uint32_t v19; } f1; } gm_t53at80pfdus3;
typedef struct { gm_t08olq97rscn3 f0; gm_tlivgedjs5ps3 f1; } gm_t70h2c775g4n7;
_Static_assert(sizeof(gm_t7bjbmukv26e7) == 64 && _Alignof(gm_t7bjbmukv26e7) == 8 && offsetof(gm_t7bjbmukv26e7, f0) == 0 && offsetof(gm_t7bjbmukv26e7, f1) == 8 && offsetof(gm_t7bjbmukv26e7, f2) == 24 && offsetof(gm_t7bjbmukv26e7, f3) == 48, "layout of `WhenItems`");
_Static_assert(sizeof(gm_t84dv0cg48dut) == 20 && _Alignof(gm_t84dv0cg48dut) == 4 && offsetof(gm_t84dv0cg48dut, f0) == 0 && offsetof(gm_t84dv0cg48dut, f1) == 16, "layout of `?(win64: bool, span: Span)`");
_Static_assert(sizeof(gm_t8cc1mtbrhlpt) == 40 && _Alignof(gm_t8cc1mtbrhlpt) == 8 && offsetof(gm_t8cc1mtbrhlpt, f0) == 0 && offsetof(gm_t8cc1mtbrhlpt, f1) == 32, "layout of `?SoaInfo`");
_Static_assert(sizeof(gm_t8nckeqfenl8a) == 24 && _Alignof(gm_t8nckeqfenl8a) == 8 && offsetof(gm_t8nckeqfenl8a, f0) == 0 && offsetof(gm_t8nckeqfenl8a, f1) == 8 && offsetof(gm_t8nckeqfenl8a, f2) == 12, "layout of `(len: u64, elem: Ty, kind: ArrayKind)`");
_Static_assert(sizeof(gm_tbci5ospk5nnn) == 48 && _Alignof(gm_tbci5ospk5nnn) == 8 && offsetof(gm_tbci5ospk5nnn, f0) == 0 && offsetof(gm_tbci5ospk5nnn, f1) == 8, "layout of `Int`");
_Static_assert(sizeof(gm_tcfiotghd1s5k) == 88 && _Alignof(gm_tcfiotghd1s5k) == 8 && offsetof(gm_tcfiotghd1s5k, f0) == 0 && offsetof(gm_tcfiotghd1s5k, f1) == 24 && offsetof(gm_tcfiotghd1s5k, f2) == 40 && offsetof(gm_tcfiotghd1s5k, f3) == 80, "layout of `TestDecl`");
typedef struct { gm_t1s75i08qtptd f0; void * f1; } gm_tchr52g0ema53;
_Static_assert(sizeof(gm_tddtjfggdat0d) == 48 && _Alignof(gm_tddtjfggdat0d) == 8 && offsetof(gm_tddtjfggdat0d, f0) == 0 && offsetof(gm_tddtjfggdat0d, f1) == 16 && offsetof(gm_tddtjfggdat0d, f2) == 24 && offsetof(gm_tddtjfggdat0d, f3) == 40, "layout of `FnTy`");
_Static_assert(sizeof(gm_tet9guhev5r4u) == 56 && _Alignof(gm_tet9guhev5r4u) == 8 && offsetof(gm_tet9guhev5r4u, f0) == 0 && offsetof(gm_tet9guhev5r4u, f1) == 32, "layout of `IntrinsicExpr`");
_Static_assert(sizeof(gm_tgsvmddj3e85e) == 64 && _Alignof(gm_tgsvmddj3e85e) == 8 && offsetof(gm_tgsvmddj3e85e, f0) == 0 && offsetof(gm_tgsvmddj3e85e, f1) == 8, "layout of `(i: usize, x: ParamInfo)`");
typedef struct { void * f0; gm_t1s75i08qtptd f1; void * f2; } gm_tioi3jh9v22g5;
_Static_assert(sizeof(gm_tioie2ju3q4ul) == 40 && _Alignof(gm_tioie2ju3q4ul) == 8 && offsetof(gm_tioie2ju3q4ul, f0) == 0 && offsetof(gm_tioie2ju3q4ul, f1) == 8, "layout of `(base: ^Expr, name: Ident)`");
_Static_assert(sizeof(gm_tkacitepg5fk0) == 72 && _Alignof(gm_tkacitepg5fk0) == 8 && offsetof(gm_tkacitepg5fk0, f0) == 0 && offsetof(gm_tkacitepg5fk0, f1) == 32 && offsetof(gm_tkacitepg5fk0, f2) == 56, "layout of `Attribute`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tv2uf4leil5hq v0; gm_t7bjbmukv26e7 v1; void * v2; gm_tcfiotghd1s5k v3; void * v4; } f1; } gm_tkoope2qgegd9;
_Static_assert(sizeof(gm_tlivgedjs5ps3) == 72 && _Alignof(gm_tlivgedjs5ps3) == 8 && offsetof(gm_tlivgedjs5ps3, f0) == 0 && offsetof(gm_tlivgedjs5ps3, f1) == 16 && offsetof(gm_tlivgedjs5ps3, f2) == 24 && offsetof(gm_tlivgedjs5ps3, f3) == 32 && offsetof(gm_tlivgedjs5ps3, f4) == 56, "layout of `FnSig`");
typedef struct { gm_tbci5ospk5nnn f0; gm_tbci5ospk5nnn f1; } gm_tngtuiuskn938;
_Static_assert(sizeof(gm_tpautr4br7h3l) == 48 && _Alignof(gm_tpautr4br7h3l) == 8 && offsetof(gm_tpautr4br7h3l, f0) == 0 && offsetof(gm_tpautr4br7h3l, f1) == 16 && offsetof(gm_tpautr4br7h3l, f2) == 24 && offsetof(gm_tpautr4br7h3l, f3) == 40, "layout of `StructDef`");
_Static_assert(sizeof(gm_tq7qjpkmqmc5s) == 104 && _Alignof(gm_tq7qjpkmqmc5s) == 8 && offsetof(gm_tq7qjpkmqmc5s, f0) == 0 && offsetof(gm_tq7qjpkmqmc5s, f1) == 8 && offsetof(gm_tq7qjpkmqmc5s, f2) == 24 && offsetof(gm_tq7qjpkmqmc5s, f3) == 32 && offsetof(gm_tq7qjpkmqmc5s, f4) == 40 && offsetof(gm_tq7qjpkmqmc5s, f5) == 48 && offsetof(gm_tq7qjpkmqmc5s, f6) == 56 && offsetof(gm_tq7qjpkmqmc5s, f7) == 80 && offsetof(gm_tq7qjpkmqmc5s, f8) == 96, "layout of `GlobalInfo`");
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t p2[4]; uint64_t f2; gm_b16_8 f3; gm_t88rcucg1b7uk f4; uint8_t p5[4]; gm_tnpifv20irnph f5; gm_b16_8 f6; gm_tqu3m2l3n8mnc f7; uint8_t f8; uint8_t pe[7]; } gm_tqqhoivahirr0;
_Static_assert(sizeof(gm_tqu3m2l3n8mnc) == 32 && _Alignof(gm_tqu3m2l3n8mnc) == 8 && offsetof(gm_tqu3m2l3n8mnc, f0) == 0 && offsetof(gm_tqu3m2l3n8mnc, f1) == 24, "layout of `?FnInstance`");
_Static_assert(sizeof(gm_trl9d7m1k5lbd) == 24 && _Alignof(gm_trl9d7m1k5lbd) == 8 && offsetof(gm_trl9d7m1k5lbd, f0) == 0 && offsetof(gm_trl9d7m1k5lbd, f1) == 8 && offsetof(gm_trl9d7m1k5lbd, f2) == 16, "layout of `(op: BinOp, lhs: ^Expr, rhs: ^Expr)`");
_Static_assert(sizeof(gm_ts686ckv8ehm2) == 40 && _Alignof(gm_ts686ckv8ehm2) == 8 && offsetof(gm_ts686ckv8ehm2, f0) == 0 && offsetof(gm_ts686ckv8ehm2, f1) == 32, "layout of `(name: Ident, ty: ^Expr)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { void * v0; void * v1; gm_tibupt4dmcqai v2; uint8_t v3; gm_t08olq97rscn3 v4; gm_t08olq97rscn3 v5; void * v6; gm_b16_8 v7; void * v8; gm_t8ghgl2jj0qha v9; gm_trl9d7m1k5lbd v10; gm_tioi3jh9v22g5 v11; void * v12; void * v13; gm_t3mf58gv9kf3n v14; void * v15; gm_tlljg61a2ctgq v16; gm_t3mf58gv9kf3n v17; gm_tao63pk3r18v5 v18; gm_tioie2ju3q4ul v19; gm_tupk62cr6lbdu v20; void * v21; gm_tgtub0v94d5u5 v22; gm_tgtub0v94d5u5 v23; gm_tgtub0v94d5u5 v24; gm_t3mf58gv9kf3n v25; gm_t3mf58gv9kf3n v26; void * v27; void * v28; void * v29; void * v30; void * v31; gm_tpe1hv3fekn05 v32; void * v33; void * v34; gm_tibupt4dmcqai v35; void * v36; void * v37; void * v38; void * v39; void * v40; gm_t79rbdv4rafe5 v41; void * v42; gm_tchr52g0ema53 v43; gm_t1s75i08qtptd v44; gm_t3mf58gv9kf3n v45; gm_tet9guhev5r4u v46; } f1; } gm_t2gtbh39ptekd;
_Static_assert(sizeof(gm_t53at80pfdus3) == 56 && _Alignof(gm_t53at80pfdus3) == 8 && offsetof(gm_t53at80pfdus3, f0) == 0 && offsetof(gm_t53at80pfdus3, f1) == 8, "layout of `TyKind`");
_Static_assert(sizeof(gm_t70h2c775g4n7) == 104 && _Alignof(gm_t70h2c775g4n7) == 8 && offsetof(gm_t70h2c775g4n7, f0) == 0 && offsetof(gm_t70h2c775g4n7, f1) == 32, "layout of `(name: Ident, sig: FnSig)`");
_Static_assert(sizeof(gm_tchr52g0ema53) == 48 && _Alignof(gm_tchr52g0ema53) == 8 && offsetof(gm_tchr52g0ema53, f0) == 0 && offsetof(gm_tchr52g0ema53, f1) == 40, "layout of `BreakExpr`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t70h2c775g4n7 v0; gm_ts686ckv8ehm2 v1; } f1; } gm_tg69e9d4krfe7;
typedef struct { gm_tqqhoivahirr0 f0; uint8_t f1; uint8_t pe[7]; } gm_tg93b272d11vo;
_Static_assert(sizeof(gm_tioi3jh9v22g5) == 56 && _Alignof(gm_tioi3jh9v22g5) == 8 && offsetof(gm_tioi3jh9v22g5, f0) == 0 && offsetof(gm_tioi3jh9v22g5, f1) == 8 && offsetof(gm_tioi3jh9v22g5, f2) == 48, "layout of `ElseExpr`");
_Static_assert(sizeof(gm_tkoope2qgegd9) == 96 && _Alignof(gm_tkoope2qgegd9) == 8 && offsetof(gm_tkoope2qgegd9, f0) == 0 && offsetof(gm_tkoope2qgegd9, f1) == 8, "layout of `ItemKind`");
_Static_assert(sizeof(gm_tngtuiuskn938) == 96 && _Alignof(gm_tngtuiuskn938) == 8 && offsetof(gm_tngtuiuskn938, f0) == 0 && offsetof(gm_tngtuiuskn938, f1) == 48, "layout of `Rat`");
_Static_assert(sizeof(gm_tqqhoivahirr0) == 136 && _Alignof(gm_tqqhoivahirr0) == 8 && offsetof(gm_tqqhoivahirr0, f0) == 0 && offsetof(gm_tqqhoivahirr0, f1) == 16 && offsetof(gm_tqqhoivahirr0, f2) == 32 && offsetof(gm_tqqhoivahirr0, f3) == 40 && offsetof(gm_tqqhoivahirr0, f4) == 56 && offsetof(gm_tqqhoivahirr0, f5) == 64 && offsetof(gm_tqqhoivahirr0, f6) == 80 && offsetof(gm_tqqhoivahirr0, f7) == 96 && offsetof(gm_tqqhoivahirr0, f8) == 128, "layout of `FnSig`");
typedef struct { gm_tngtuiuskn938 f0; gm_tngtuiuskn938 f1; } gm_tu15p0hi2qlv6;
typedef struct { uint32_t f0; gm_tl874t2atnlh3 f1; gm_th2lqbo77pvsc f2; gm_b16_8 f3; gm_t9qkh0rsooc8d f4; uint8_t p5[7]; gm_tkoope2qgegd9 f5; } gm_tvpnktv5fo8nl;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tbci5ospk5nnn v0; gm_tngtuiuskn938 v1; uint8_t v2; gm_tu15p0hi2qlv6 v3; } f1; } gm_t0ialelq3ql1k;
_Static_assert(sizeof(gm_t2gtbh39ptekd) == 64 && _Alignof(gm_t2gtbh39ptekd) == 8 && offsetof(gm_t2gtbh39ptekd, f0) == 0 && offsetof(gm_t2gtbh39ptekd, f1) == 8, "layout of `ExprKind`");
typedef struct { gm_b16_8 f0; gm_tg69e9d4krfe7 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_t7rh4gj19r55v;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_t2gtbh39ptekd f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_tc3oht4ksth9d;
_Static_assert(sizeof(gm_tg69e9d4krfe7) == 112 && _Alignof(gm_tg69e9d4krfe7) == 8 && offsetof(gm_tg69e9d4krfe7, f0) == 0 && offsetof(gm_tg69e9d4krfe7, f1) == 8, "layout of `ForeignMemberKind`");
_Static_assert(sizeof(gm_tg93b272d11vo) == 144 && _Alignof(gm_tg93b272d11vo) == 8 && offsetof(gm_tg93b272d11vo, f0) == 0 && offsetof(gm_tg93b272d11vo, f1) == 136, "layout of `?FnSig`");
_Static_assert(sizeof(gm_tu15p0hi2qlv6) == 192 && _Alignof(gm_tu15p0hi2qlv6) == 8 && offsetof(gm_tu15p0hi2qlv6, f0) == 0 && offsetof(gm_tu15p0hi2qlv6, f1) == 96, "layout of `(re: Rat, im: Rat)`");
_Static_assert(sizeof(gm_tvpnktv5fo8nl) == 160 && _Alignof(gm_tvpnktv5fo8nl) == 8 && offsetof(gm_tvpnktv5fo8nl, f0) == 0 && offsetof(gm_tvpnktv5fo8nl, f1) == 4 && offsetof(gm_tvpnktv5fo8nl, f2) == 16 && offsetof(gm_tvpnktv5fo8nl, f3) == 40 && offsetof(gm_tvpnktv5fo8nl, f4) == 56 && offsetof(gm_tvpnktv5fo8nl, f5) == 64, "layout of `Item`");
_Static_assert(sizeof(gm_t0ialelq3ql1k) == 200 && _Alignof(gm_t0ialelq3ql1k) == 8 && offsetof(gm_t0ialelq3ql1k, f0) == 0 && offsetof(gm_t0ialelq3ql1k, f1) == 8, "layout of `Untyped`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t88rcucg1b7uk v0; uint32_t v1; uint32_t v2; gm_tiilmfd16v3fc v3; gm_tnpifv20irnph v4; uint32_t v5; gm_tcc1rq4n1avb2 v6; gm_t88rcucg1b7uk v7; gm_t0ialelq3ql1k v8; uint64_t v9; gm_t88rcucg1b7uk v10; } f1; } gm_t6qooom8qa2do;
_Static_assert(sizeof(gm_t7rh4gj19r55v) == 144 && _Alignof(gm_t7rh4gj19r55v) == 8 && offsetof(gm_t7rh4gj19r55v, f0) == 0 && offsetof(gm_t7rh4gj19r55v, f1) == 16 && offsetof(gm_t7rh4gj19r55v, f2) == 128, "layout of `ForeignMember`");
_Static_assert(sizeof(gm_tc3oht4ksth9d) == 88 && _Alignof(gm_tc3oht4ksth9d) == 8 && offsetof(gm_tc3oht4ksth9d, f0) == 0 && offsetof(gm_tc3oht4ksth9d, f1) == 8 && offsetof(gm_tc3oht4ksth9d, f2) == 72, "layout of `Expr`");
_Static_assert(sizeof(gm_t6qooom8qa2do) == 208 && _Alignof(gm_t6qooom8qa2do) == 8 && offsetof(gm_t6qooom8qa2do, f0) == 0 && offsetof(gm_t6qooom8qa2do, f1) == 8, "layout of `DeclValue`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t6qooom8qa2do v0; } f1; } gm_tagrkbqh054h7;
_Static_assert(sizeof(gm_tagrkbqh054h7) == 216 && _Alignof(gm_tagrkbqh054h7) == 8 && offsetof(gm_tagrkbqh054h7, f0) == 0 && offsetof(gm_tagrkbqh054h7, f1) == 8, "layout of `DeclState`");
struct gm_fr_apply_callconv_197l87p {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_callconv_attr_dd08ovc {
    gm_frame h;
    gm_tqs5p4n2tq19v ret;
};
struct gm_fr_callconv_fn_ptr_l9kavc3 {
    gm_frame h;
    gm_tkacitepg5fk0 l8; /* a */
    gm_tddtjfggdat0d l10; /* sig */
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_declare_foreign_6s86jje {
    gm_frame h;
    gm_t7rh4gj19r55v l12; /* member */
    gm_tkacitepg5fk0 l17; /* a */
    gm_t6qooom8qa2do l32; /* value */
    gm_tlivgedjs5ps3 l34; /* sig */
    gm_tvpnktv5fo8nl l40; /* item */
    gm_t6qooom8qa2do ret;
};
struct gm_fr_declare_foreign_fn_u3dekca {
    gm_frame h;
    gm_tg93b272d11vo l4; /* resolved */
    gm_tqqhoivahirr0 l5; /* r */
    gm_t5v0c8ia6a5mo l9; /* p */
    gm_trqfvmjtt4sij l11; /* params */
    gm_trqfvmjtt4sij l12; /* locals */
    gm_t5v0c8ia6a5mo l16; /* p */
    gm_t9805qtonbsag ret;
};
struct gm_fr_declare_global_729nttf {
    gm_frame h;
    gm_t6qooom8qa2do ret;
};
struct gm_fr_empty_effects_r0iv3pv {
    gm_frame h;
    gm_tnpifv20irnph ret;
};
struct gm_fr_empty_slice_3jt8vgk {
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
struct gm_fr_empty_slice_atlcs4j {
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
struct gm_fr_empty_slice_g3tddrb {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ijtp9lo {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_uphdt5g {
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
struct gm_fr_escape_debug_lu2p5se {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* out */
    gm_b16_8 ret;
};
struct gm_fr_export_fn_8hcuasc {
    gm_frame h;
    gm_tqqhoivahirr0 l4; /* sig */
    gm_t5v0c8ia6a5mo l11; /* p */
    gm_b0_1 ret;
};
struct gm_fr_foreign_info_e0kd6uc {
    gm_frame h;
    gm_thognv9odov03 l4; /* x */
    gm_thognv9odov03 ret;
};
struct gm_fr_has_param_u9t0efs {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_int_ci6airg {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_is_alnum_n81ervr {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_alpha_12215ga {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_c_compatible_2hi3vi8 {
    gm_frame h;
    gm_tddtjfggdat0d l5; /* sig */
    gm_tpautr4br7h3l l13; /* def */
    gm_t0mglksrkfa5e l17; /* f */
    gm_t0mglksrkfa5e l21; /* f */
    uint8_t ret;
};
struct gm_fr_is_digit_2hoggnj {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_error_8k27gd1 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_lower_4ebokoh {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_unit_n83ttnb {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_upper_iekrftp {
    gm_frame h;
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
struct gm_fr_list_go2qg0b {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_p9s2t99 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_uvfu1k1 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_lookup_59juqoo {
    gm_frame h;
    gm_tf5srh4mp4b3i ret;
};
struct gm_fr_printer_h7jigd7 {
    gm_frame h;
    gm_tmm67r2c94d5j ret;
};
struct gm_fr_report_eardf07 {
    gm_frame h;
    gm_b0_1 ret;
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
struct gm_fr_slice_5dt9121 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_source_location_ty_slllard {
    gm_frame h;
    gm_tkn42u7na3t81 ret;
};
struct gm_fr_string_ty_j74ff0o {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_sys_effect_97o96a1 {
    gm_frame h;
    gm_t9805qtonbsag ret;
};
struct gm_fr_sys_set_pqe8mto {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* one */
    gm_tnpifv20irnph ret;
};
struct gm_fr_trap_hook_fn_5c0a4ds {
    gm_frame h;
    gm_tqqhoivahirr0 l3; /* sig */
    gm_b0_1 ret;
};
struct gm_fr_u8_ty_afkvj25 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_unsupported_t7lgvp9 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_unsupported_value_sufn4da {
    gm_frame h;
    gm_t6qooom8qa2do ret;
};
struct gm_fr_within_sys_otp5qp6 {
    gm_frame h;
    uint8_t ret;
};
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b);
inline bool gm_eq467d5cm1d5(const uint8_t *a, const uint8_t *b);
inline bool gm_eq9m6mvtmgde(const uint8_t *a, const uint8_t *b);
inline bool gm_eq9oqmmiu8qc(const uint8_t *a, const uint8_t *b);
inline bool gm_eqjmccqdb90f(const uint8_t *a, const uint8_t *b);
inline bool gm_eqkricceou4u(const uint8_t *a, const uint8_t *b);
__attribute__((aligned(4))) uint32_t gm_f_add_fn_t4t83qh(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, const void * a2);
__attribute__((aligned(4))) uint64_t gm_f_add_member_0jvs7ug(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, const void * a2, const void * a3);
__attribute__((aligned(4))) uint64_t gm_f_add_module_3cgsegf(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_apply_callconv_197l87p(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, uint8_t a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) gm_tqs5p4n2tq19v gm_f_callconv_attr_dd08ovc(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_callconv_fn_ptr_l9kavc3(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2, void * a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_const_check_d48mnu7(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2, gm_tkn42u7na3t81 a3);
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_const_type_7rijs95(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2, gm_tkn42u7na3t81 a3);
__attribute__((aligned(4))) void gm_f_declare_foreign_6s86jje(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, const void * a2, void * a3);
__attribute__((aligned(4))) gm_t9805qtonbsag gm_f_declare_foreign_fn_u3dekca(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, const void * a2, void * a3);
__attribute__((aligned(4))) void gm_f_declare_global_729nttf(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, const void * a2, void * a3);
__attribute__((aligned(4))) void gm_f_decode_rune_ugbib8e(void *gm_out, gm_b16_8 a0);
__attribute__((aligned(4))) gm_tnpifv20irnph gm_f_effect_set_vtcpipr(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_tnpifv20irnph gm_f_empty_effects_r0iv3pv(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_3jt8vgk(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_6trg8r7(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_92arfe8(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_atlcs4j(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_d914i5u(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_fh7svmj(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_g3tddrb(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_uphdt5g(void);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_error_89glhjr(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_error_ki8i3k6(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_error_s6hn871(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_escape_debug_lu2p5se(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_export_fn_8hcuasc(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_b16_8 a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_extend_0cs85gs(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_foreign_info_e0kd6uc(void *gm_out, void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_thognv9odov03 gm_f_foreign_info_e0kd6uc_c(void * a0, uint32_t a1);
__attribute__((aligned(4))) void gm_f_format_2ng6k0e(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_3mrt8ba(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_69lunt5(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_a14ppi0(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_du76tsh(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_e79cvtl(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_fea89f6(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_g6mj4ou(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_k3kbc4n(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_mh1raea(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_mprr1lp(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_oq9akg7(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_qds4t66(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_rb3ud4d(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_v2s58vm(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_get_espvf96(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_get_nt3g9ko(void *gm_out, void * a0, uint32_t a1);
inline __attribute__((aligned(4))) uint8_t gm_f_has_param_u9t0efs(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_int_ci6airg(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, uint16_t a2);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_intern_ia19mhl(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_alnum_n81ervr(uint8_t a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_alpha_12215ga(uint8_t a0);
__attribute__((aligned(4))) uint8_t gm_f_is_c_compatible_2hi3vi8(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, uint8_t a2);
inline __attribute__((aligned(4))) uint8_t gm_f_is_digit_2hoggnj(uint8_t a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_error_8k27gd1(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_lower_4ebokoh(uint8_t a0);
__attribute__((aligned(4))) uint8_t gm_f_is_nullable_pointer_nkie06s(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) uint8_t gm_f_is_runtime_symbol_rj1sp7s(gm_b16_8 a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_unit_n83ttnb(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_upper_iekrftp(uint8_t a0);
inline __attribute__((aligned(4))) void gm_f_kind_jcnt4hm(void *gm_out, void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void gm_f_list_084fcd6(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_go2qg0b(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_p9s2t99(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_lookup_59juqoo(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_ve8bb01(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
inline __attribute__((aligned(4))) void gm_f_printer_h7jigd7(void *gm_out, void * a0, void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_3ef746b(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_4p3ivmb(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_am7bo0v(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_d1r4h19(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_im9d4ro(void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_krl2rii(void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_push_o4ckdr1(void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_report_eardf07(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_resolve_decl_pus042v(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_resolve_fn_sig_pamuhve(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2, uint8_t a3, gm_b16_8 a4);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_resolve_type_jjodbei(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_set_state_3j300fi(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, const void * a2);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_8uijkbs(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_kmjeh8s(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_slice_5dt9121(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_source_location_ty_slllard(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_state_a5bnsjb(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_string_ty_j74ff0o(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_struct_fields_chqu3it(void *gm_out, void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) gm_t9805qtonbsag gm_f_sys_effect_97o96a1(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_tnpifv20irnph gm_f_sys_set_pqe8mto(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_trap_hook_fn_5c0a4ds(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_ty_vka2r8h(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_u8_ty_afkvj25(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_unsupported_t7lgvp9(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_unsupported_value_sufn4da(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) uint8_t gm_f_utf8_valid_a8oc1t8(gm_b16_8 a0);
__attribute__((aligned(4))) uint8_t gm_f_within_sys_otp5qp6(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_146u7aa(void * a0, gm_t88rcucg1b7uk a1);
extern uint8_t gm_c3t36dlh4g5[16] __attribute__((aligned(8)));
extern uint8_t gm_kp923qoaivn[40] __attribute__((aligned(8)));
extern const uint8_t gm_s1de3v91lp97g[106];
extern const uint8_t gm_s22g482qk7a40[3];
extern const uint8_t gm_s2gnpj7k4l22n[2];
extern const uint8_t gm_s2v5amvfkfoll[3];
extern const uint8_t gm_s310nl8k9dnl2[59];
extern const uint8_t gm_s3p8gug7bvl1p[3];
extern const uint8_t gm_s43ls0s4m665j[57];
extern const uint8_t gm_s4nl1jq95dg65[111];
extern const uint8_t gm_s5nig6ebl26qo[61];
extern const uint8_t gm_s6aprmc3mqp0t[2];
extern const uint8_t gm_s6gt8b7hsisgu[3];
extern const uint8_t gm_s6ket4hq2umi0[51];
extern const uint8_t gm_s7v0kgsq3q7vf[57];
extern const uint8_t gm_s80p0ld1j6rfg[10];
extern const uint8_t gm_s81csu1l86dmv[52];
extern const uint8_t gm_s9cb6j5ok7bdk[3];
extern const uint8_t gm_sa291memmphhk[15];
extern const uint8_t gm_sa902c1tbtg0i[4];
extern const uint8_t gm_sbaogg9p69n9f[38];
extern const uint8_t gm_sbd6ket0ql5ab[9];
extern const uint8_t gm_scj8qp34gs3m8[58];
extern const uint8_t gm_sge2cgjaklnf2[5];
extern const uint8_t gm_shud0j6jb8ucc[37];
extern const uint8_t gm_si3qm04ntebij[6];
extern const uint8_t gm_siqa65grpcsk9[99];
extern const uint8_t gm_sj2jft9n0jm50[3];
extern const uint8_t gm_sj2ngf6ro6oja[65];
extern const uint8_t gm_sje4rbc88csdu[7];
extern const uint8_t gm_sjmngec2on365[5];
extern const uint8_t gm_smee1janbl2t9[3];
extern const uint8_t gm_sog4rksrmsqd2[5];
extern const uint8_t gm_soo4rtckloi4d[13];
extern const uint8_t gm_sottk452c14qm[56];
extern const uint8_t gm_sovqp4emsh0q8[5];
extern const uint8_t gm_spvg0rt0dloah[9];
extern const char gm_src0ih77f43mhf2[];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_srcc2qutubarto1[];
extern const char gm_srccvj6up3gbolp[];
extern const char gm_srclv0dcbbfhves[];
extern const char gm_srcrgc9bkquf6nr[];
extern const uint8_t gm_st4bliqvgaqre[127];
extern const uint8_t gm_str26682vod5l[38];
extern const uint8_t gm_su274du674a98[38];
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
inline bool gm_eq9oqmmiu8qc(const uint8_t *a, const uint8_t *b) {
    return GM_LD(uint16_t, a) == GM_LD(uint16_t, b);
}
inline bool gm_eqjmccqdb90f(const uint8_t *a, const uint8_t *b) {
    return GM_LD(uint8_t, a) == GM_LD(uint8_t, b);
}
inline bool gm_eqkricceou4u(const uint8_t *a, const uint8_t *b) {
    if (a[4] != b[4]) return false;
    return !a[4] || gm_eq467d5cm1d5(a, b);
}
inline __attribute__((aligned(4))) gm_tnpifv20irnph gm_f_empty_effects_r0iv3pv(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_effects_r0iv3pv F;
    GM_STACK_CHECK_SHALLOW(&F, 96u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_92arfe8();
    gm_b16_8 t2 = t1;
    gm_b16_8 t3 = GM_LD(gm_b16_8, ((uint8_t *)&t2));
    gm_tnpifv20irnph t4;
    GM_ST(gm_b16_8, ((uint8_t *)&t4 + 0), t3);
    F.ret = t4;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_3jt8vgk(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_3jt8vgk F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_6trg8r7(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_6trg8r7 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_92arfe8(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_92arfe8 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_atlcs4j(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_atlcs4j F;
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
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 72)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_fh7svmj(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_fh7svmj F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_g3tddrb(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_g3tddrb F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_uphdt5g(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_uphdt5g F;
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_int_ci6airg(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, uint16_t a2) {
    struct gm_fr_int_ci6airg F;
    void * gm_l0; /* t */
    uint8_t gm_l1; /* signed */
    uint16_t gm_l2; /* bits */
    GM_STACK_CHECK(&F, 94u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t1 = gm_l0;
    uint8_t t2 = gm_l1;
    uint16_t t3 = gm_l2;
    gm_tjcj4j2gbh6c8 t4;
    GM_ST(uint8_t, (uint8_t *)&t4 + 1, 0);
    GM_ST(uint8_t, ((uint8_t *)&t4 + 0), t2);
    GM_ST(uint16_t, ((uint8_t *)&t4 + 2), t3);
    gm_t53at80pfdus3 t5;
    memset(&t5, 0, sizeof t5);
    GM_ST(uint8_t, ((uint8_t *)&t5 + 0), ((uint8_t)0ULL));
    GM_ST(gm_tjcj4j2gbh6c8, ((uint8_t *)&t5 + 8), t4);
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_alnum_n81ervr(uint8_t a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_alnum_n81ervr F;
    uint8_t gm_l0; /* c */
    GM_STACK_CHECK_SHALLOW(&F, 8u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t t1 = 0;
    uint8_t t2 = gm_l0;
    uint8_t t3 = gm_f_is_alpha_12215ga(t2);
    if (!t3) {
        uint8_t t4 = gm_l0;
        uint8_t t5 = gm_f_is_digit_2hoggnj(t4);
        t1 = t5;
    } else t1 = 1;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_alpha_12215ga(uint8_t a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_alpha_12215ga F;
    uint8_t gm_l0; /* c */
    GM_STACK_CHECK_SHALLOW(&F, 8u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t t1 = 0;
    uint8_t t2 = gm_l0;
    uint8_t t3 = gm_f_is_upper_iekrftp(t2);
    if (!t3) {
        uint8_t t4 = gm_l0;
        uint8_t t5 = gm_f_is_lower_4ebokoh(t4);
        t1 = t5;
    } else t1 = 1;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_lower_4ebokoh(uint8_t a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_lower_4ebokoh F;
    uint8_t gm_l0; /* c */
    GM_STACK_CHECK_SHALLOW(&F, 8u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t t1 = 0;
    uint8_t t2 = gm_l0;
    uint8_t t3 = (t2 >= ((uint8_t)97ULL));
    if (t3) {
        uint8_t t4 = gm_l0;
        uint8_t t5 = (t4 <= ((uint8_t)122ULL));
        t1 = t5;
    } else t1 = 0;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_unit_n83ttnb(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_unit_n83ttnb F;
    void * gm_l0; /* t */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    gm_b16_8 gm_l2; /* elems */
    GM_STACK_CHECK_SHALLOW(&F, 182u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    {
        void * t3 = gm_l0;
        gm_t88rcucg1b7uk t4 = gm_l1;
        gm_t53at80pfdus3 t5;
        gm_f_kind_jcnt4hm((void *)&t5, t3, GM_CONV(gm_csguankko4lmhd, t4));
        gm_t53at80pfdus3 t6 = t5;
        uint8_t t7 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
        if (t7 != ((uint8_t)12ULL)) goto ie2;
        gm_b16_8 t8 = GM_LD(gm_b16_8, ((((uint8_t *)&t6) + 8) + 0));
        gm_l2 = t8;
        {
            uint64_t t10 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
            uint8_t t11 = ((t10 == ((uint64_t)0ULL)));
            uint8_t t12 = 0;
            F.ret = t11;
            goto gm_fret;
        }
        goto ix2;
ie2:;
        {
            uint8_t t14 = 0;
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_upper_iekrftp(uint8_t a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_upper_iekrftp F;
    uint8_t gm_l0; /* c */
    GM_STACK_CHECK_SHALLOW(&F, 8u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t t1 = 0;
    uint8_t t2 = gm_l0;
    uint8_t t3 = (t2 >= ((uint8_t)65ULL));
    if (t3) {
        uint8_t t4 = gm_l0;
        uint8_t t5 = (t4 <= ((uint8_t)90ULL));
        t1 = t5;
    } else t1 = 0;
    F.ret = t1;
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
inline __attribute__((aligned(4))) void gm_f_list_p9s2t99(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_p9s2t99 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_6trg8r7();
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_string_ty_j74ff0o(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_string_ty_j74ff0o F;
    void * gm_l0; /* t */
    GM_STACK_CHECK(&F, 40u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    void * t2 = gm_l0;
    gm_t88rcucg1b7uk t3 = gm_f_u8_ty_afkvj25((void *)NULL, cx, NULL, t2);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_t88rcucg1b7uk t4 = gm_f_slice_5dt9121((void *)NULL, cx, NULL, t1, ((uint8_t)1), t3);
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_u8_ty_afkvj25(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_u8_ty_afkvj25 F;
    void * gm_l0; /* t */
    GM_STACK_CHECK(&F, 28u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    gm_t88rcucg1b7uk t2 = gm_f_int_ci6airg((void *)NULL, cx, NULL, t1, ((uint8_t)0), ((uint16_t)8ULL));
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_unsupported_t7lgvp9(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2) {
    struct gm_fr_unsupported_t7lgvp9 F;
    void * gm_l0; /* s */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_b16_8 gm_l2; /* what */
    GM_STACK_CHECK(&F, 216u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        gm_tl874t2atnlh3 t3 = gm_l1;
        gm_t6j6smj7qoctu t4;
        gm_b16_8 t5 = GM_LD(gm_b16_8, gm_c3t36dlh4g5);
        GM_ST(gm_b16_8, (uint8_t *)&t4 + 0, t5);
        gm_b16_8 t6 = gm_l2;
        GM_ST(gm_b16_8, (uint8_t *)&t4 + 16, t6);
        gm_tabb710d9mq2e t7;
        gm_f_format_e79cvtl((void *)&t7, (void *)NULL, cx, NULL, &t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t8 = t7;
        if (((((uint8_t *)&t8))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t8) + 8, gm_srclv0dcbbfhves, 635, 19);
        gm_b16_8 t9 = GM_LD(gm_b16_8, (((uint8_t *)&t8) + 8));
        gm_b16_8 t10 = t9;
        gm_b16_8 t11 = GM_LD(gm_b16_8, ((uint8_t *)&t10));
        gm_b0_1 t12 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t2, t3, t11);
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
inline __attribute__((aligned(4))) void gm_f_unsupported_value_sufn4da(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2) {
    struct gm_fr_unsupported_value_sufn4da F;
    void * gm_l0; /* s */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_b16_8 gm_l2; /* what */
    GM_STACK_CHECK(&F, 281u);
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
        gm_b0_1 t5 = gm_f_unsupported_t7lgvp9((void *)NULL, cx, NULL, t2, t3, t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_t6qooom8qa2do t6;
        memset(&t6, 0, sizeof t6);
        GM_ST(uint8_t, ((uint8_t *)&t6 + 0), ((uint8_t)11ULL));
        uint8_t t7 = 0;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_apply_callconv_197l87p(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, uint8_t a2, gm_tl874t2atnlh3 a3) {
    struct gm_fr_apply_callconv_197l87p F;
    void * gm_l0; /* s */
    uint32_t gm_l1; /* f */
    uint8_t gm_l2; /* win64 */
    gm_tl874t2atnlh3 gm_l3; /* span */
    gm_tnpifv20irnph gm_l4; /* effects */
    GM_STACK_CHECK(&F, 267u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        uint8_t t3 = gm_l2;
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            gm_b0_1 t6;
            uint8_t t7 = 0;
            F.ret = t6;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        uint32_t t8 = gm_l1;
        uint64_t t9 = (uint64_t)t8;
        uint64_t t10 = (uintptr_t)t9;
        void * t11 = gm_l0;
        uint64_t t12 = GM_LD(uintptr_t, ((((uint8_t *)t11) + 344) + 0) + 8);
        if (t10 >= (uintptr_t)(t12)) gm_trap_index(t10, t12, gm_srccvj6up3gbolp, 244, 16);
        gm_tnpifv20irnph t13 = GM_LD(gm_tnpifv20irnph, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t11) + 344) + 0)) + t10 * 136) + 64));
        gm_l4 = t13;
    }
    {
        uint32_t t15 = gm_l1;
        uint64_t t16 = (uint64_t)t15;
        uint64_t t17 = (uintptr_t)t16;
        void * t18 = gm_l0;
        uint64_t t19 = GM_LD(uintptr_t, ((((uint8_t *)t18) + 344) + 0) + 8);
        if (t17 >= (uintptr_t)(t19)) gm_trap_index(t17, t19, gm_srccvj6up3gbolp, 245, 8);
        uint64_t t20 = GM_LD(uintptr_t, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t18) + 344) + 0)) + t17 * 136) + 80) + 8);
        uint8_t t21 = (t20 > ((uint64_t)0ULL));
        if (!t21) goto ie14;
        {
            void * t23 = gm_l0;
            gm_tl874t2atnlh3 t24 = gm_l3;
            gm_b16_8 t25;
            GM_ST(void *, (uint8_t *)&t25, (void *)(gm_sj2ngf6ro6oja));
            GM_ST(uintptr_t, (uint8_t *)&t25 + 8, (uintptr_t)(64));
            gm_b0_1 t26 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t23, t24, t25);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix14;
ie14:;
        void * t28 = gm_l0;
        void * t29 = (void *)((uint8_t *)&gm_l4);
        uint8_t t30 = gm_f_within_sys_otp5qp6((void *)NULL, cx, NULL, t28, t29);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t31 = (uint8_t)!t30;
        if (!t31) goto ie27;
        {
            void * t33 = gm_l0;
            gm_tl874t2atnlh3 t34 = gm_l3;
            gm_b16_8 t35;
            GM_ST(void *, (uint8_t *)&t35, (void *)(gm_st4bliqvgaqre));
            GM_ST(uintptr_t, (uint8_t *)&t35 + 8, (uintptr_t)(126));
            gm_b0_1 t36 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t33, t34, t35);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix27;
ie27:;
        {
            uint32_t t38 = gm_l1;
            uint64_t t39 = (uint64_t)t38;
            uint64_t t40 = (uintptr_t)t39;
            void * t41 = gm_l0;
            uint64_t t42 = GM_LD(uintptr_t, ((((uint8_t *)t41) + 344) + 0) + 8);
            if (t40 >= (uintptr_t)(t42)) gm_trap_index(t40, t42, gm_srccvj6up3gbolp, 250, 9);
            GM_ST(uint8_t, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t41) + 344) + 0)) + t40 * 136) + 128), ((uint8_t)1));
        }
ix27:;
ix14:;
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
__attribute__((aligned(4))) gm_tqs5p4n2tq19v gm_f_callconv_attr_dd08ovc(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_callconv_attr_dd08ovc F;
    void * gm_l0; /* s */
    void * gm_l1; /* a */
    gm_b16_8 gm_l2; /* args */
    gm_t08olq97rscn3 gm_l3; /* id */
    uint8_t gm_l4; /* x86_64 */
    gm_b16_8 gm_l5; /* other */
    GM_STACK_CHECK(&F, 676u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l1;
        gm_th2lqbo77pvsc t3 = GM_LD(gm_th2lqbo77pvsc, (((uint8_t *)t2) + 32));
        gm_th2lqbo77pvsc t4 = t3;
        gm_b16_8 t5;
        if (((((uint8_t *)&t4))[16] != 0)) {
            gm_b16_8 t6 = GM_LD(gm_b16_8, (((uint8_t *)&t4) + 0));
            t5 = t6;
        } else {
            gm_b16_8 t7 = gm_f_empty_slice_g3tddrb();
            t5 = t7;
        }
        gm_l2 = t5;
    }
    {
        uint8_t t9 = 0;
        uint64_t t10 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        uint8_t t11 = (!(t10 == ((uint64_t)1ULL)));
        if (!t11) {
            uint64_t t12 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t13 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
            if (t12 >= (uintptr_t)(t13)) gm_trap_index(t12, t13, gm_srccvj6up3gbolp, 216, 29);
            uint8_t t14;
            uint8_t t17 = GM_LD(uint8_t, ((((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t12 * 88) + 8) + 0));
            if (t17 != ((uint8_t)8ULL)) goto mn16;
            t14 = ((uint8_t)0);
            goto me15;
mn16:;
            t14 = ((uint8_t)1);
            goto me15;
mn18:;
            gm_trap("no match arm matched the scrutinee", gm_srccvj6up3gbolp, 216, 29);
me15:;
            t9 = t14;
        } else t9 = 1;
        if (!t9) goto ie8;
        {
            void * t20 = gm_l0;
            void * t21 = gm_l1;
            gm_tl874t2atnlh3 t22 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t21) + 56));
            gm_b16_8 t23;
            GM_ST(void *, (uint8_t *)&t23, (void *)(gm_s7v0kgsq3q7vf));
            GM_ST(uintptr_t, (uint8_t *)&t23 + 8, (uintptr_t)(56));
            gm_b0_1 t24 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t20, t22, t23);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_tqs5p4n2tq19v t25;
            memset(&t25, 0, sizeof t25);
            uint8_t t26 = 0;
            F.ret = t25;
            goto gm_fret;
        }
        goto ix8;
ie8:;
ix8:;
    }
    {
        uint64_t t28 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t29 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        if (t28 >= (uintptr_t)(t29)) gm_trap_index(t28, t29, gm_srccvj6up3gbolp, 220, 22);
        uint8_t t30 = GM_LD(uint8_t, ((((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t28 * 88) + 8) + 0));
        if (t30 != ((uint8_t)8ULL)) goto ie27;
        gm_t08olq97rscn3 t31 = GM_LD(gm_t08olq97rscn3, ((((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t28 * 88) + 8) + 8));
        gm_l3 = t31;
        {
            void * t33 = gm_l0;
            void * t34 = GM_LD(void *, (((uint8_t *)t33) + 0));
            uint8_t t35 = (((GM_LD(uintptr_t, ((((uint8_t *)t34) + 88) + 40) + 8) == 6) && memcmp(GM_LD(void *, ((((uint8_t *)t34) + 88) + 40)), gm_sje4rbc88csdu, 6) == 0));
            gm_l4 = t35;
        }
        {
            if (!((GM_LD(uintptr_t, (((uint8_t *)&gm_l3) + 0) + 8) == 1) && memcmp(GM_LD(void *, (((uint8_t *)&gm_l3) + 0)), gm_s6aprmc3mqp0t, 1) == 0)) goto mn37;
            gm_tqs5p4n2tq19v t38;
            GM_ST(uint8_t, ((uint8_t *)&t38), ((uint8_t)0));
            ((uint8_t *)&t38)[1] = 1;
            uint8_t t39 = 0;
            F.ret = t38;
            goto gm_fret;
            goto me36;
mn37:;
            if (!((GM_LD(uintptr_t, (((uint8_t *)&gm_l3) + 0) + 8) == 4) && memcmp(GM_LD(void *, (((uint8_t *)&gm_l3) + 0)), gm_sjmngec2on365, 4) == 0)) goto pa42;
            goto po41;
pa42:;
            if (!((GM_LD(uintptr_t, (((uint8_t *)&gm_l3) + 0) + 8) == 5) && memcmp(GM_LD(void *, (((uint8_t *)&gm_l3) + 0)), gm_si3qm04ntebij, 5) == 0)) goto pa43;
            goto po41;
pa43:;
            goto mn40;
po41:;
            {
                uint8_t t46 = gm_l4;
                uint8_t t47 = (uint8_t)!t46;
                if (!t47) goto ie45;
                {
                    void * t49 = gm_l0;
                    void * t50 = gm_l1;
                    gm_tl874t2atnlh3 t51 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t50) + 56));
                    gm_b16_8 t52 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
                    gm_tnpifv20irnph t53;
                    GM_ST(gm_b16_8, ((uint8_t *)&t53 + 0), t52);
                    gm_tabb710d9mq2e t54;
                    gm_f_format_v2s58vm((void *)&t54, (void *)NULL, cx, NULL, t53);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tabb710d9mq2e t55 = t54;
                    if (((((uint8_t *)&t55))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t55) + 8, gm_srccvj6up3gbolp, 226, 33);
                    gm_b16_8 t56 = GM_LD(gm_b16_8, (((uint8_t *)&t55) + 8));
                    gm_b16_8 t57 = t56;
                    gm_b16_8 t58 = GM_LD(gm_b16_8, ((uint8_t *)&t57));
                    gm_b0_1 t59 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t49, t51, t58);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                {
                    gm_tqs5p4n2tq19v t60;
                    memset(&t60, 0, sizeof t60);
                    uint8_t t61 = 0;
                    F.ret = t60;
                    goto gm_fret;
                }
                goto ix45;
ie45:;
ix45:;
            }
            {
                uint8_t t62 = (((GM_LD(uintptr_t, (((uint8_t *)&gm_l3) + 0) + 8) == 5) && memcmp(GM_LD(void *, (((uint8_t *)&gm_l3) + 0)), gm_si3qm04ntebij, 5) == 0));
                gm_tqs5p4n2tq19v t63;
                GM_ST(uint8_t, ((uint8_t *)&t63), t62);
                ((uint8_t *)&t63)[1] = 1;
                uint8_t t64 = 0;
                F.ret = t63;
                goto gm_fret;
            }
            goto me36;
mn40:;
            gm_b16_8 t66 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_l5 = t66;
            {
                void * t68 = gm_l0;
                void * t69 = gm_l1;
                gm_tl874t2atnlh3 t70 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t69) + 56));
                gm_b16_8 t71 = gm_l5;
                gm_tnpifv20irnph t72;
                GM_ST(gm_b16_8, ((uint8_t *)&t72 + 0), t71);
                gm_tabb710d9mq2e t73;
                gm_f_format_g6mj4ou((void *)&t73, (void *)NULL, cx, NULL, t72);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t74 = t73;
                if (((((uint8_t *)&t74))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t74) + 8, gm_srccvj6up3gbolp, 232, 29);
                gm_b16_8 t75 = GM_LD(gm_b16_8, (((uint8_t *)&t74) + 8));
                gm_b16_8 t76 = t75;
                gm_b16_8 t77 = GM_LD(gm_b16_8, ((uint8_t *)&t76));
                gm_b0_1 t78 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t68, t70, t77);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                gm_tqs5p4n2tq19v t79;
                memset(&t79, 0, sizeof t79);
                uint8_t t80 = 0;
                F.ret = t79;
                goto gm_fret;
            }
            goto me36;
mn65:;
            gm_trap("no match arm matched the scrutinee", gm_srccvj6up3gbolp, 222, 5);
me36:;
        }
        goto ix27;
ie27:;
        {
            gm_tqs5p4n2tq19v t82;
            memset(&t82, 0, sizeof t82);
            uint8_t t83 = 0;
            F.ret = t82;
            goto gm_fret;
        }
ix27:;
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
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_callconv_fn_ptr_l9kavc3(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2, void * a3, gm_tl874t2atnlh3 a4) {
    struct gm_fr_callconv_fn_ptr_l9kavc3 F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* module */
    gm_b16_8 gm_l2; /* attrs */
    void * gm_l3; /* inner */
    gm_tl874t2atnlh3 gm_l4; /* span */
    gm_tqs5p4n2tq19v gm_l5; /* win64 */
    gm_b16_8 gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    gm_t88rcucg1b7uk gm_l9; /* ty */
    uint8_t gm_l11; /* w */
    GM_STACK_CHECK(&F, 910u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    {
        gm_tqs5p4n2tq19v t2;
        memset(&t2, 0, sizeof t2);
        gm_l5 = t2;
    }
    {
        {
            gm_b16_8 t4 = gm_l2;
            gm_l6 = t4;
        }
        {
            gm_l7 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t6 = gm_l7;
                uint64_t t7 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                uint8_t t8 = (t6 < t7);
                if (!t8) goto lb5;
                {
                    uint64_t t11 = gm_l7;
                    uint64_t t12 = (uintptr_t)t11;
                    uint64_t t13 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                    gm_tkacitepg5fk0 t16 = GM_LD(gm_tkacitepg5fk0, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t12 * 72));
                    F.l8 = t16;
                    {
                        uint8_t t19 = (!((GM_LD(uintptr_t, ((((uint8_t *)&F.l8) + 0) + 0) + 8) == 8) && memcmp(GM_LD(void *, ((((uint8_t *)&F.l8) + 0) + 0)), gm_spvg0rt0dloah, 8) == 0));
                        if (!t19) goto ie18;
                        {
                            void * t21 = gm_l0;
                            gm_tl874t2atnlh3 t22 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l8) + 56));
                            gm_b16_8 t23 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l8) + 0) + 0));
                            gm_tnpifv20irnph t24;
                            GM_ST(gm_b16_8, ((uint8_t *)&t24 + 0), t23);
                            gm_tabb710d9mq2e t25;
                            gm_f_format_a14ppi0((void *)&t25, (void *)NULL, cx, NULL, t24);
                            if (gm_unw.kind) {
                                goto gm_ll9;
                            }
                            gm_tabb710d9mq2e t26 = t25;
                            if (((((uint8_t *)&t26))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t26) + 8, gm_srccvj6up3gbolp, 260, 29);
                            gm_b16_8 t27 = GM_LD(gm_b16_8, (((uint8_t *)&t26) + 8));
                            gm_b16_8 t28 = t27;
                            gm_b16_8 t29 = GM_LD(gm_b16_8, ((uint8_t *)&t28));
                            gm_b0_1 t30 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t21, t22, t29);
                            if (gm_unw.kind) {
                                goto gm_ll9;
                            }
                        }
                        goto ix18;
ie18:;
                        uint8_t t32;
                        if (((((uint8_t *)&gm_l5))[1] != 0)) goto mn34;
                        t32 = ((uint8_t)0);
                        goto me33;
mn34:;
                        t32 = ((uint8_t)1);
                        goto me33;
mn35:;
                        gm_trap("no match arm matched the scrutinee", gm_srccvj6up3gbolp, 261, 19);
me33:;
                        if (!t32) goto ie31;
                        {
                            void * t37 = gm_l0;
                            gm_tl874t2atnlh3 t38 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l8) + 56));
                            gm_b16_8 t39;
                            GM_ST(void *, (uint8_t *)&t39, (void *)(gm_s6ket4hq2umi0));
                            GM_ST(uintptr_t, (uint8_t *)&t39 + 8, (uintptr_t)(50));
                            gm_b0_1 t40 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t37, t38, t39);
                            if (gm_unw.kind) {
                                goto gm_ll9;
                            }
                        }
                        goto ix31;
ie31:;
                        {
                            void * t42 = gm_l0;
                            void * t43 = (void *)((uint8_t *)&F.l8);
                            gm_tqs5p4n2tq19v t44 = gm_f_callconv_attr_dd08ovc((void *)NULL, cx, NULL, t42, t43);
                            if (gm_unw.kind) {
                                goto gm_ll9;
                            }
                            gm_l5 = t44;
                        }
ix31:;
ix18:;
                    }
                    goto me14;
mn15:;
                    gm_trap("no match arm matched the scrutinee", gm_srccvj6up3gbolp, 258, 5);
me14:;
                }
lc5:;
                {
                    uint64_t t46 = gm_l7;
                    uint64_t r47; bool o48 = __builtin_add_overflow(t46, ((uint64_t)1ULL), &r47);
                    uint64_t t49 = r47;
                    gm_l7 = t49;
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
        void * t50 = gm_l0;
        uint64_t t51 = gm_l1;
        void * t52 = gm_l3;
        gm_t88rcucg1b7uk t53 = gm_f_resolve_type_jjodbei((void *)NULL, cx, NULL, t50, t51, t52);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l9 = t53;
    }
    {
        void * t55 = gm_l0;
        void * t56 = (void *)(((uint8_t *)t55) + 8);
        gm_t88rcucg1b7uk t57 = gm_l9;
        gm_t53at80pfdus3 t58;
        gm_f_kind_jcnt4hm((void *)&t58, t56, GM_CONV(gm_csguankko4lmhd, t57));
        gm_t53at80pfdus3 t59 = t58;
        uint8_t t60 = GM_LD(uint8_t, (((uint8_t *)&t59) + 0));
        if (t60 != ((uint8_t)16ULL)) goto ie54;
        gm_tddtjfggdat0d t61 = GM_LD(gm_tddtjfggdat0d, (((uint8_t *)&t59) + 8));
        F.l10 = t61;
        {
            gm_tqs5p4n2tq19v t63 = gm_l5;
            gm_tqs5p4n2tq19v t64 = t63;
            uint8_t t65;
            if (((((uint8_t *)&t64))[1] != 0)) {
                uint8_t t66 = GM_LD(uint8_t, (((uint8_t *)&t64) + 0));
                t65 = t66;
            } else {
                gm_t88rcucg1b7uk t67 = gm_l9;
                uint8_t t68 = 0;
                F.ret = t67;
                goto gm_fret;
            }
            gm_l11 = t65;
        }
        {
            uint8_t t70 = gm_l11;
            uint8_t t71 = (uint8_t)!t70;
            if (!t71) goto ie69;
            {
                gm_t88rcucg1b7uk t73 = gm_l9;
                uint8_t t74 = 0;
                F.ret = t73;
                goto gm_fret;
            }
            goto ix69;
ie69:;
ix69:;
        }
        {
            void * t76 = gm_l0;
            void * t77 = (void *)(((uint8_t *)&F.l10) + 24);
            uint8_t t78 = gm_f_within_sys_otp5qp6((void *)NULL, cx, NULL, t76, t77);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t79 = (uint8_t)!t78;
            if (!t79) goto ie75;
            {
                void * t81 = gm_l0;
                gm_tl874t2atnlh3 t82 = gm_l4;
                gm_b16_8 t83;
                GM_ST(void *, (uint8_t *)&t83, (void *)(gm_s1de3v91lp97g));
                GM_ST(uintptr_t, (uint8_t *)&t83 + 8, (uintptr_t)(105));
                gm_b0_1 t84 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t81, t82, t83);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t85 = gm_l0;
                void * t86 = (void *)(((uint8_t *)t85) + 8);
                gm_t88rcucg1b7uk t87 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t86);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t88 = 0;
                F.ret = t87;
                goto gm_fret;
            }
            goto ix75;
ie75:;
ix75:;
        }
        {
            void * t89 = gm_l0;
            void * t90 = (void *)(((uint8_t *)t89) + 8);
            gm_tddtjfggdat0d t91;
            memset((uint8_t *)&t91 + 20, 0, 4);
            memset((uint8_t *)&t91 + 41, 0, 7);
            gm_b16_8 t92 = GM_LD(gm_b16_8, (((uint8_t *)&F.l10) + 0));
            GM_ST(gm_b16_8, (uint8_t *)&t91 + 0, t92);
            gm_t88rcucg1b7uk t93 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l10) + 16));
            GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t91 + 16, t93);
            gm_tnpifv20irnph t94 = GM_LD(gm_tnpifv20irnph, (((uint8_t *)&F.l10) + 24));
            GM_ST(gm_tnpifv20irnph, (uint8_t *)&t91 + 24, t94);
            GM_ST(uint8_t, (uint8_t *)&t91 + 40, ((uint8_t)1));
            gm_t53at80pfdus3 t95;
            GM_ST(uint8_t, (uint8_t *)&t95 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t95 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t95 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t95 + 0), ((uint8_t)16ULL));
            GM_ST(gm_tddtjfggdat0d, ((uint8_t *)&t95 + 8), t91);
            gm_t88rcucg1b7uk t96 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t90, &t95);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t97 = 0;
            F.ret = t96;
            goto gm_fret;
        }
        goto ix54;
ie54:;
        {
            gm_t88rcucg1b7uk t99 = gm_l9;
            uint8_t t100 = 0;
            F.ret = t99;
            goto gm_fret;
        }
ix54:;
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
__attribute__((aligned(4))) void gm_f_declare_foreign_6s86jje(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, const void * a2, void * a3) {
    struct gm_fr_declare_foreign_6s86jje F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* module */
    gm_t08olq97rscn3 gm_l2; /* name */
    void * gm_l3; /* block */
    gm_b16_8 gm_l4; /* lib */
    uint8_t gm_l5; /* lib_ok */
    gm_b16_8 gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    uint8_t gm_l8; /* c */
    uint64_t gm_l9; /* m */
    gm_b16_8 gm_l10; /* <tmp> */
    uint64_t gm_l11; /* <tmp> */
    gm_th2lqbo77pvsc gm_l13; /* link_name */
    gm_t84dv0cg48dut gm_l14; /* callconv */
    gm_b16_8 gm_l15; /* <tmp> */
    uint64_t gm_l16; /* <tmp> */
    uint8_t gm_l18; /* ok */
    gm_b16_8 gm_l19; /* args */
    gm_tibupt4dmcqai gm_l20; /* lit */
    gm_b16_8 gm_l21; /* sym */
    uint8_t gm_l22; /* good */
    gm_b16_8 gm_l23; /* <tmp> */
    uint64_t gm_l24; /* <tmp> */
    uint8_t gm_l25; /* c */
    uint8_t gm_l26; /* w */
    gm_b16_8 gm_l27; /* other */
    gm_t08olq97rscn3 gm_l28; /* n */
    gm_t08olq97rscn3 gm_l29; /* n */
    gm_t08olq97rscn3 gm_l30; /* own_name */
    gm_b16_8 gm_l31; /* sym */
    gm_t08olq97rscn3 gm_l33; /* n */
    uint32_t gm_l35; /* f */
    gm_tbvif938ns15l gm_l36; /* c */
    gm_t08olq97rscn3 gm_l37; /* n */
    void * gm_l38; /* ty_expr */
    gm_t88rcucg1b7uk gm_l39; /* t */
    uint64_t gm_l41; /* d */
    gm_t88rcucg1b7uk gm_l42; /* t */
    GM_STACK_CHECK(&F, 5747u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l3 = a3;
    {
        void * t2 = gm_l3;
        gm_b16_8 t3 = GM_LD(gm_b16_8, ((((uint8_t *)t2) + 0) + 0));
        gm_l4 = t3;
    }
    {
        uint8_t t4 = 0;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
        uint8_t t6 = (t5 > ((uint64_t)0ULL));
        if (t6) {
            uint64_t t7 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t8 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
            if (t7 >= (uintptr_t)(t8)) gm_trap_index(t7, t8, gm_srccvj6up3gbolp, 55, 31);
            uint8_t t9 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t7 * 1));
            uint8_t t10 = (!(t9 == ((uint8_t)45ULL)));
            t4 = t10;
        } else t4 = 0;
        gm_l5 = t4;
    }
    {
        {
            gm_b16_8 t12 = gm_l4;
            gm_l6 = t12;
        }
        {
            gm_l7 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t14 = gm_l7;
                uint64_t t15 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                uint8_t t16 = (t14 < t15);
                if (!t16) goto lb13;
                {
                    uint64_t t19 = gm_l7;
                    uint64_t t20 = (uintptr_t)t19;
                    uint64_t t21 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                    uint8_t t22 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t20 * 1));
                    uint8_t t23 = t22;
                    uint8_t t26 = GM_LD(uint8_t, ((uint8_t *)&t23));
                    gm_l8 = t26;
                    {
                        uint8_t t29 = 0;
                        uint8_t t30 = gm_l8;
                        uint8_t t31 = gm_f_is_alnum_n81ervr(t30);
                        if (!t31) {
                            uint8_t t32 = gm_l8;
                            gm_b16_8 t33;
                            GM_ST(void *, (uint8_t *)&t33, (void *)(gm_sovqp4emsh0q8));
                            GM_ST(uintptr_t, (uint8_t *)&t33 + 8, (uintptr_t)(4));
                            gm_b16_8 t34 = t33;
                            uint8_t t35 = 0;
                            uint8_t t36 = t32;
                            for (uintptr_t i = 0; i < (uintptr_t)(GM_LD(uintptr_t, ((uint8_t *)&t34) + 8)); i++) if (gm_eqjmccqdb90f(((uint8_t *)GM_LD(void *, ((uint8_t *)&t34))) + i * 1, ((uint8_t *)&t36))) { t35 = 1; break; }
                            uint8_t t37 = (uint8_t)(t35);
                            t29 = t37;
                        } else t29 = 1;
                        uint8_t t38 = (uint8_t)!t29;
                        if (!t38) goto ie28;
                        {
                            gm_l5 = ((uint8_t)0);
                        }
                        goto ix28;
ie28:;
ix28:;
                    }
                    goto me24;
mn25:;
                    gm_trap("no match arm matched the scrutinee", gm_srccvj6up3gbolp, 56, 5);
me24:;
                }
lc13:;
                {
                    uint64_t t41 = gm_l7;
                    uint64_t r42; bool o43 = __builtin_add_overflow(t41, ((uint64_t)1ULL), &r42);
                    uint64_t t44 = r42;
                    gm_l7 = t44;
                }
            }
lb13:;
        }
    }
    {
        uint8_t t46 = gm_l5;
        uint8_t t47 = (uint8_t)!t46;
        if (!t47) goto ie45;
        {
            gm_b16_8 t50 = gm_l4;
            gm_b16_8 t51 = t50;
            gm_b16_8 t52 = GM_LD(gm_b16_8, ((uint8_t *)&t51));
            uint8_t t53 = gm_f_utf8_valid_a8oc1t8(t52);
            uint8_t t54 = (uint8_t)!t53;
            if (!t54) goto ie49;
            {
                void * t56 = gm_l0;
                gm_tl874t2atnlh3 t57 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)a2) + 16));
                gm_b16_8 t58;
                GM_ST(void *, (uint8_t *)&t58, (void *)(gm_su274du674a98));
                GM_ST(uintptr_t, (uint8_t *)&t58 + 8, (uintptr_t)(37));
                gm_f_unsupported_value_sufn4da((void *)&(*(gm_t6qooom8qa2do *)gm_out), (void *)NULL, cx, NULL, t56, t57, t58);
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
            void * t59 = gm_l0;
            gm_tl874t2atnlh3 t60 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)a2) + 16));
            gm_b16_8 t61 = gm_l4;
            gm_b16_8 t62 = t61;
            gm_b16_8 t63 = GM_LD(gm_b16_8, ((uint8_t *)&t62));
            gm_b16_8 t64 = gm_f_escape_debug_lu2p5se((void *)NULL, cx, NULL, t63);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tnpifv20irnph t65;
            GM_ST(gm_b16_8, ((uint8_t *)&t65 + 0), t64);
            gm_tabb710d9mq2e t66;
            gm_f_format_du76tsh((void *)&t66, (void *)NULL, cx, NULL, t65);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t67 = t66;
            if (((((uint8_t *)&t67))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t67) + 8, gm_srccvj6up3gbolp, 61, 28);
            gm_b16_8 t68 = GM_LD(gm_b16_8, (((uint8_t *)&t67) + 8));
            gm_b16_8 t69 = t68;
            gm_b16_8 t70 = GM_LD(gm_b16_8, ((uint8_t *)&t69));
            gm_b0_1 t71 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t59, t60, t70);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix45;
ie45:;
ix45:;
    }
    {
        void * t72 = gm_l0;
        void * t73 = GM_LD(void *, (((uint8_t *)t72) + 0));
        gm_b16_8 t74 = GM_LD(gm_b16_8, (((uint8_t *)a2) + 0));
        gm_b16_8 t75 = gm_f_empty_slice_uphdt5g();
        gm_b16_8 t76 = t75;
        gm_b16_8 t77 = GM_LD(gm_b16_8, ((uint8_t *)&t76));
        uint64_t t78 = gm_f_add_module_3cgsegf((void *)NULL, cx, NULL, t73, t74, t77);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l9 = t78;
    }
    {
        uint64_t t79 = gm_l9;
        uint64_t t80 = (uintptr_t)t79;
        void * t81 = gm_l0;
        void * t82 = GM_LD(void *, (((uint8_t *)t81) + 0));
        uint64_t t83 = GM_LD(uintptr_t, ((((uint8_t *)t82) + 200) + 0) + 8);
        if (t80 >= (uintptr_t)(t83)) gm_trap_index(t80, t83, gm_srccvj6up3gbolp, 64, 5);
        uint64_t t84 = gm_l1;
        uint64_t t85 = (uintptr_t)t84;
        void * t86 = gm_l0;
        void * t87 = GM_LD(void *, (((uint8_t *)t86) + 0));
        uint64_t t88 = GM_LD(uintptr_t, ((((uint8_t *)t87) + 200) + 0) + 8);
        if (t85 >= (uintptr_t)(t88)) gm_trap_index(t85, t88, gm_srccvj6up3gbolp, 64, 32);
        gm_b16_8 t89 = GM_LD(gm_b16_8, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t87) + 200) + 0)) + t85 * 184) + 120));
        GM_ST(gm_b16_8, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t82) + 200) + 0)) + t80 * 184) + 120), t89);
    }
    {
        uint64_t t90 = gm_l9;
        uint64_t t91 = (uintptr_t)t90;
        void * t92 = gm_l0;
        void * t93 = GM_LD(void *, (((uint8_t *)t92) + 0));
        uint64_t t94 = GM_LD(uintptr_t, ((((uint8_t *)t93) + 200) + 0) + 8);
        if (t91 >= (uintptr_t)(t94)) gm_trap_index(t91, t94, gm_srccvj6up3gbolp, 65, 5);
        uint64_t t95 = gm_l1;
        uint64_t t96 = (uintptr_t)t95;
        void * t97 = gm_l0;
        void * t98 = GM_LD(void *, (((uint8_t *)t97) + 0));
        uint64_t t99 = GM_LD(uintptr_t, ((((uint8_t *)t98) + 200) + 0) + 8);
        if (t96 >= (uintptr_t)(t99)) gm_trap_index(t96, t99, gm_srccvj6up3gbolp, 65, 36);
        uint64_t t100 = GM_LD(uint64_t, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t98) + 200) + 0)) + t96 * 184) + 136));
        GM_ST(uint64_t, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t93) + 200) + 0)) + t91 * 184) + 136), t100);
    }
    {
        {
            void * t102 = gm_l3;
            gm_b16_8 t103 = GM_LD(gm_b16_8, (((uint8_t *)t102) + 24));
            gm_l10 = t103;
        }
        {
            gm_l11 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t105 = gm_l11;
                uint64_t t106 = GM_LD(uintptr_t, ((uint8_t *)&gm_l10) + 8);
                uint8_t t107 = (t105 < t106);
                if (!t107) goto lb104;
                {
                    uint64_t t110 = gm_l11;
                    uint64_t t111 = (uintptr_t)t110;
                    uint64_t t112 = GM_LD(uintptr_t, ((uint8_t *)&gm_l10) + 8);
                    gm_t7rh4gj19r55v t115 = GM_LD(gm_t7rh4gj19r55v, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l10)) + t111 * 144));
                    F.l12 = t115;
                    {
                        gm_th2lqbo77pvsc t117;
                        memset(&t117, 0, sizeof t117);
                        gm_l13 = t117;
                    }
                    {
                        gm_t84dv0cg48dut t118;
                        memset(&t118, 0, sizeof t118);
                        gm_l14 = t118;
                    }
                    {
                        {
                            gm_b16_8 t120 = GM_LD(gm_b16_8, (((uint8_t *)&F.l12) + 0));
                            gm_l15 = t120;
                        }
                        {
                            gm_l16 = ((uint64_t)0ULL);
                        }
                        {
                            for (;;) {
                                uint64_t t122 = gm_l16;
                                uint64_t t123 = GM_LD(uintptr_t, ((uint8_t *)&gm_l15) + 8);
                                uint8_t t124 = (t122 < t123);
                                if (!t124) goto lb121;
                                {
                                    uint64_t t127 = gm_l16;
                                    uint64_t t128 = (uintptr_t)t127;
                                    uint64_t t129 = GM_LD(uintptr_t, ((uint8_t *)&gm_l15) + 8);
                                    gm_tkacitepg5fk0 t132 = GM_LD(gm_tkacitepg5fk0, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l15)) + t128 * 72));
                                    F.l17 = t132;
                                    {
                                        if (!((GM_LD(uintptr_t, ((((uint8_t *)&F.l17) + 0) + 0) + 8) == 9) && memcmp(GM_LD(void *, ((((uint8_t *)&F.l17) + 0) + 0)), gm_s80p0ld1j6rfg, 9) == 0)) goto mn135;
                                        {
                                            gm_l18 = ((uint8_t)0);
                                        }
                                        {
                                            if (!(((((uint8_t *)&F.l17) + 32))[16] != 0)) goto ie137;
                                            gm_b16_8 t138 = GM_LD(gm_b16_8, (((uint8_t *)&F.l17) + 32));
                                            gm_l19 = t138;
                                            uint64_t t139 = GM_LD(uintptr_t, ((uint8_t *)&gm_l19) + 8);
                                            uint8_t t140 = ((t139 == ((uint64_t)1ULL)));
                                            if (!t140) goto ie137;
                                            uint64_t t141 = (uintptr_t)((uint64_t)0ULL);
                                            uint64_t t142 = GM_LD(uintptr_t, ((uint8_t *)&gm_l19) + 8);
                                            if (t141 >= (uintptr_t)(t142)) gm_trap_index(t141, t142, gm_srccvj6up3gbolp, 73, 74);
                                            uint8_t t143 = GM_LD(uint8_t, ((((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l19)) + t141 * 88) + 8) + 0));
                                            if (t143 != ((uint8_t)2ULL)) goto ie137;
                                            gm_tibupt4dmcqai t144 = GM_LD(gm_tibupt4dmcqai, ((((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l19)) + t141 * 88) + 8) + 8));
                                            gm_l20 = t144;
                                            {
                                                gm_l18 = ((uint8_t)1);
                                            }
                                            {
                                                gm_b16_8 t146 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l20) + 0));
                                                gm_l21 = t146;
                                            }
                                            {
                                                uint64_t t147 = GM_LD(uintptr_t, ((uint8_t *)&gm_l21) + 8);
                                                uint8_t t148 = (t147 > ((uint64_t)0ULL));
                                                gm_l22 = t148;
                                            }
                                            {
                                                {
                                                    gm_b16_8 t150 = gm_l21;
                                                    gm_l23 = t150;
                                                }
                                                {
                                                    gm_l24 = ((uint64_t)0ULL);
                                                }
                                                {
                                                    for (;;) {
                                                        uint64_t t152 = gm_l24;
                                                        uint64_t t153 = GM_LD(uintptr_t, ((uint8_t *)&gm_l23) + 8);
                                                        uint8_t t154 = (t152 < t153);
                                                        if (!t154) goto lb151;
                                                        {
                                                            uint64_t t157 = gm_l24;
                                                            uint64_t t158 = (uintptr_t)t157;
                                                            uint64_t t159 = GM_LD(uintptr_t, ((uint8_t *)&gm_l23) + 8);
                                                            uint8_t t160 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l23)) + t158 * 1));
                                                            uint8_t t161 = t160;
                                                            uint8_t t164 = GM_LD(uint8_t, ((uint8_t *)&t161));
                                                            gm_l25 = t164;
                                                            {
                                                                uint8_t t167 = 0;
                                                                uint8_t t168 = gm_l25;
                                                                uint8_t t169 = gm_f_is_alnum_n81ervr(t168);
                                                                if (!t169) {
                                                                    uint8_t t170 = gm_l25;
                                                                    gm_b16_8 t171;
                                                                    GM_ST(void *, (uint8_t *)&t171, (void *)(gm_sge2cgjaklnf2));
                                                                    GM_ST(uintptr_t, (uint8_t *)&t171 + 8, (uintptr_t)(4));
                                                                    gm_b16_8 t172 = t171;
                                                                    uint8_t t173 = 0;
                                                                    uint8_t t174 = t170;
                                                                    for (uintptr_t i = 0; i < (uintptr_t)(GM_LD(uintptr_t, ((uint8_t *)&t172) + 8)); i++) if (gm_eqjmccqdb90f(((uint8_t *)GM_LD(void *, ((uint8_t *)&t172))) + i * 1, ((uint8_t *)&t174))) { t173 = 1; break; }
                                                                    uint8_t t175 = (uint8_t)(t173);
                                                                    t167 = t175;
                                                                } else t167 = 1;
                                                                uint8_t t176 = (uint8_t)!t167;
                                                                if (!t176) goto ie166;
                                                                {
                                                                    gm_l22 = ((uint8_t)0);
                                                                }
                                                                goto ix166;
ie166:;
ix166:;
                                                            }
                                                            goto me162;
mn163:;
                                                            gm_trap("no match arm matched the scrutinee", gm_srccvj6up3gbolp, 77, 25);
me162:;
                                                        }
lc151:;
                                                        {
                                                            uint64_t t179 = gm_l24;
                                                            uint64_t r180; bool o181 = __builtin_add_overflow(t179, ((uint64_t)1ULL), &r180);
                                                            uint64_t t182 = r180;
                                                            gm_l24 = t182;
                                                        }
                                                    }
lb151:;
                                                }
                                            }
                                            {
                                                uint8_t t184 = gm_l22;
                                                if (!t184) goto ie183;
                                                {
                                                    gm_b16_8 t186 = gm_l21;
                                                    gm_b16_8 t187 = t186;
                                                    gm_b16_8 t188 = GM_LD(gm_b16_8, ((uint8_t *)&t187));
                                                    gm_th2lqbo77pvsc t189;
                                                    GM_ST(uint8_t, (uint8_t *)&t189 + 17, 0);
                                                    GM_ST(uint16_t, (uint8_t *)&t189 + 18, 0);
                                                    GM_ST(uint32_t, (uint8_t *)&t189 + 20, 0);
                                                    GM_ST(gm_b16_8, ((uint8_t *)&t189), t188);
                                                    ((uint8_t *)&t189)[16] = 1;
                                                    gm_l13 = t189;
                                                }
                                                goto ix183;
ie183:;
                                                {
                                                    gm_b16_8 t192 = gm_l21;
                                                    gm_b16_8 t193 = t192;
                                                    gm_b16_8 t194 = GM_LD(gm_b16_8, ((uint8_t *)&t193));
                                                    uint8_t t195 = gm_f_utf8_valid_a8oc1t8(t194);
                                                    uint8_t t196 = (uint8_t)!t195;
                                                    if (!t196) goto ie191;
                                                    {
                                                        void * t198 = gm_l0;
                                                        uint64_t t199 = (uintptr_t)((uint64_t)0ULL);
                                                        uint64_t t200 = GM_LD(uintptr_t, ((uint8_t *)&gm_l19) + 8);
                                                        if (t199 >= (uintptr_t)(t200)) gm_trap_index(t199, t200, gm_srccvj6up3gbolp, 83, 82);
                                                        gm_tl874t2atnlh3 t201 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l19)) + t199 * 88) + 72));
                                                        gm_b16_8 t202;
                                                        GM_ST(void *, (uint8_t *)&t202, (void *)(gm_shud0j6jb8ucc));
                                                        GM_ST(uintptr_t, (uint8_t *)&t202 + 8, (uintptr_t)(36));
                                                        gm_f_unsupported_value_sufn4da((void *)&(*(gm_t6qooom8qa2do *)gm_out), (void *)NULL, cx, NULL, t198, t201, t202);
                                                        if (gm_unw.kind) {
                                                            goto gm_ll125;
                                                        }
                                                        goto gm_fdone;
                                                    }
                                                    goto ix191;
ie191:;
ix191:;
                                                }
                                                {
                                                    void * t203 = gm_l0;
                                                    uint64_t t204 = (uintptr_t)((uint64_t)0ULL);
                                                    uint64_t t205 = GM_LD(uintptr_t, ((uint8_t *)&gm_l19) + 8);
                                                    if (t204 >= (uintptr_t)(t205)) gm_trap_index(t204, t205, gm_srccvj6up3gbolp, 84, 37);
                                                    gm_tl874t2atnlh3 t206 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l19)) + t204 * 88) + 72));
                                                    gm_b16_8 t207 = gm_l21;
                                                    gm_b16_8 t208 = t207;
                                                    gm_b16_8 t209 = GM_LD(gm_b16_8, ((uint8_t *)&t208));
                                                    gm_b16_8 t210 = gm_f_escape_debug_lu2p5se((void *)NULL, cx, NULL, t209);
                                                    if (gm_unw.kind) {
                                                        goto gm_ll125;
                                                    }
                                                    gm_tnpifv20irnph t211;
                                                    GM_ST(gm_b16_8, ((uint8_t *)&t211 + 0), t210);
                                                    gm_tabb710d9mq2e t212;
                                                    gm_f_format_3mrt8ba((void *)&t212, (void *)NULL, cx, NULL, t211);
                                                    if (gm_unw.kind) {
                                                        goto gm_ll125;
                                                    }
                                                    gm_tabb710d9mq2e t213 = t212;
                                                    if (((((uint8_t *)&t213))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t213) + 8, gm_srccvj6up3gbolp, 84, 51);
                                                    gm_b16_8 t214 = GM_LD(gm_b16_8, (((uint8_t *)&t213) + 8));
                                                    gm_b16_8 t215 = t214;
                                                    gm_b16_8 t216 = GM_LD(gm_b16_8, ((uint8_t *)&t215));
                                                    gm_b0_1 t217 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t203, t206, t216);
                                                    if (gm_unw.kind) {
                                                        goto gm_ll125;
                                                    }
                                                }
ix183:;
                                            }
                                            goto ix137;
ie137:;
ix137:;
                                        }
                                        {
                                            uint8_t t219 = gm_l18;
                                            uint8_t t220 = (uint8_t)!t219;
                                            if (!t220) goto ie218;
                                            {
                                                void * t222 = gm_l0;
                                                gm_tl874t2atnlh3 t223 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l17) + 56));
                                                gm_b16_8 t224;
                                                GM_ST(void *, (uint8_t *)&t224, (void *)(gm_sottk452c14qm));
                                                GM_ST(uintptr_t, (uint8_t *)&t224 + 8, (uintptr_t)(55));
                                                gm_b0_1 t225 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t222, t223, t224);
                                                if (gm_unw.kind) {
                                                    goto gm_ll125;
                                                }
                                            }
                                            goto ix218;
ie218:;
ix218:;
                                        }
                                        goto me134;
mn135:;
                                        if (!((GM_LD(uintptr_t, ((((uint8_t *)&F.l17) + 0) + 0) + 8) == 8) && memcmp(GM_LD(void *, ((((uint8_t *)&F.l17) + 0) + 0)), gm_spvg0rt0dloah, 8) == 0)) goto mn226;
                                        {
                                            uint8_t t229;
                                            uint8_t t232 = GM_LD(uint8_t, ((((uint8_t *)&F.l12) + 16) + 0));
                                            if (t232 != ((uint8_t)0ULL)) goto mn231;
                                            t229 = ((uint8_t)1);
                                            goto me230;
mn231:;
                                            t229 = ((uint8_t)0);
                                            goto me230;
mn233:;
                                            gm_trap("no match arm matched the scrutinee", gm_srccvj6up3gbolp, 90, 24);
me230:;
                                            if (!t229) goto ie228;
                                            {
                                                uint8_t t236;
                                                if (((((uint8_t *)&gm_l14))[16] != 0)) goto mn238;
                                                t236 = ((uint8_t)0);
                                                goto me237;
mn238:;
                                                t236 = ((uint8_t)1);
                                                goto me237;
mn239:;
                                                gm_trap("no match arm matched the scrutinee", gm_srccvj6up3gbolp, 91, 28);
me237:;
                                                if (!t236) goto ie235;
                                                {
                                                    void * t241 = gm_l0;
                                                    gm_tl874t2atnlh3 t242 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l17) + 56));
                                                    gm_b16_8 t243;
                                                    GM_ST(void *, (uint8_t *)&t243, (void *)(gm_str26682vod5l));
                                                    GM_ST(uintptr_t, (uint8_t *)&t243 + 8, (uintptr_t)(37));
                                                    gm_b0_1 t244 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t241, t242, t243);
                                                    if (gm_unw.kind) {
                                                        goto gm_ll125;
                                                    }
                                                }
                                                goto ix235;
ie235:;
ix235:;
                                            }
                                            {
                                                gm_t84dv0cg48dut t245;
                                                memset(&t245, 0, sizeof t245);
                                                gm_l14 = t245;
                                            }
                                            {
                                                void * t247 = gm_l0;
                                                void * t248 = (void *)((uint8_t *)&F.l17);
                                                gm_tqs5p4n2tq19v t249 = gm_f_callconv_attr_dd08ovc((void *)NULL, cx, NULL, t247, t248);
                                                if (gm_unw.kind) {
                                                    goto gm_ll125;
                                                }
                                                gm_tqs5p4n2tq19v t250 = t249;
                                                if (!((((uint8_t *)&t250))[1] != 0)) goto ie246;
                                                uint8_t t251 = GM_LD(uint8_t, ((uint8_t *)&t250));
                                                gm_l26 = t251;
                                                {
                                                    uint8_t t253 = gm_l26;
                                                    gm_tl874t2atnlh3 t254 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l17) + 56));
                                                    gm_tbvif938ns15l t255;
                                                    GM_ST(uint8_t, (uint8_t *)&t255 + 1, 0);
                                                    GM_ST(uint16_t, (uint8_t *)&t255 + 2, 0);
                                                    GM_ST(uint8_t, ((uint8_t *)&t255 + 0), t253);
                                                    GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t255 + 4), t254);
                                                    gm_t84dv0cg48dut t256;
                                                    GM_ST(uint8_t, (uint8_t *)&t256 + 17, 0);
                                                    GM_ST(uint16_t, (uint8_t *)&t256 + 18, 0);
                                                    GM_ST(gm_tbvif938ns15l, ((uint8_t *)&t256), t255);
                                                    ((uint8_t *)&t256)[16] = 1;
                                                    gm_l14 = t256;
                                                }
                                                goto ix246;
ie246:;
ix246:;
                                            }
                                            goto ix228;
ie228:;
                                            {
                                                void * t258 = gm_l0;
                                                gm_tl874t2atnlh3 t259 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l17) + 56));
                                                gm_b16_8 t260;
                                                GM_ST(void *, (uint8_t *)&t260, (void *)(gm_s43ls0s4m665j));
                                                GM_ST(uintptr_t, (uint8_t *)&t260 + 8, (uintptr_t)(56));
                                                gm_b0_1 t261 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t258, t259, t260);
                                                if (gm_unw.kind) {
                                                    goto gm_ll125;
                                                }
                                            }
ix228:;
                                        }
                                        goto me134;
mn226:;
                                        gm_b16_8 t263 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l17) + 0) + 0));
                                        gm_l27 = t263;
                                        void * t264 = gm_l0;
                                        gm_tl874t2atnlh3 t265 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l17) + 56));
                                        gm_b16_8 t266 = gm_l27;
                                        gm_tnpifv20irnph t267;
                                        GM_ST(gm_b16_8, ((uint8_t *)&t267 + 0), t266);
                                        gm_tabb710d9mq2e t268;
                                        gm_f_format_2ng6k0e((void *)&t268, (void *)NULL, cx, NULL, t267);
                                        if (gm_unw.kind) {
                                            goto gm_ll125;
                                        }
                                        gm_tabb710d9mq2e t269 = t268;
                                        if (((((uint8_t *)&t269))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t269) + 8, gm_srccvj6up3gbolp, 98, 42);
                                        gm_b16_8 t270 = GM_LD(gm_b16_8, (((uint8_t *)&t269) + 8));
                                        gm_b16_8 t271 = t270;
                                        gm_b16_8 t272 = GM_LD(gm_b16_8, ((uint8_t *)&t271));
                                        gm_b0_1 t273 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t264, t265, t272);
                                        if (gm_unw.kind) {
                                            goto gm_ll125;
                                        }
                                        goto me134;
mn262:;
                                        gm_trap("no match arm matched the scrutinee", gm_srccvj6up3gbolp, 70, 13);
me134:;
                                    }
                                    goto me130;
mn131:;
                                    gm_trap("no match arm matched the scrutinee", gm_srccvj6up3gbolp, 69, 9);
me130:;
                                }
lc121:;
                                {
                                    uint64_t t275 = gm_l16;
                                    uint64_t r276; bool o277 = __builtin_add_overflow(t275, ((uint64_t)1ULL), &r276);
                                    uint64_t t278 = r276;
                                    gm_l16 = t278;
                                }
                            }
gm_ll125:;
                            if (gm_unw.frame == &F.h && gm_unw.label == 2) {
                                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb121; }
                                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc121; }
                            }
                            goto gm_ll108;
lb121:;
                        }
                    }
                    {
                        gm_t08olq97rscn3 t279;
                        uint8_t t282 = GM_LD(uint8_t, ((((uint8_t *)&F.l12) + 16) + 0));
                        if (t282 != ((uint8_t)0ULL)) goto mn281;
                        gm_t08olq97rscn3 t283 = GM_LD(gm_t08olq97rscn3, (((((uint8_t *)&F.l12) + 16) + 8) + 0));
                        gm_l28 = t283;
                        gm_t08olq97rscn3 t284 = gm_l28;
                        t279 = t284;
                        goto me280;
mn281:;
                        uint8_t t286 = GM_LD(uint8_t, ((((uint8_t *)&F.l12) + 16) + 0));
                        if (t286 != ((uint8_t)1ULL)) goto mn285;
                        gm_t08olq97rscn3 t287 = GM_LD(gm_t08olq97rscn3, (((((uint8_t *)&F.l12) + 16) + 8) + 0));
                        gm_l29 = t287;
                        gm_t08olq97rscn3 t288 = gm_l29;
                        t279 = t288;
                        goto me280;
mn285:;
                        gm_trap("no match arm matched the scrutinee", gm_srccvj6up3gbolp, 101, 21);
me280:;
                        gm_l30 = t279;
                    }
                    {
                        gm_th2lqbo77pvsc t289 = gm_l13;
                        gm_th2lqbo77pvsc t290 = t289;
                        gm_b16_8 t291;
                        if (((((uint8_t *)&t290))[16] != 0)) {
                            gm_b16_8 t292 = GM_LD(gm_b16_8, (((uint8_t *)&t290) + 0));
                            t291 = t292;
                        } else {
                            gm_b16_8 t293 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l30) + 0));
                            t291 = t293;
                        }
                        gm_l31 = t291;
                    }
                    {
                        gm_b16_8 t295 = gm_l31;
                        uint8_t t296 = gm_f_is_runtime_symbol_rj1sp7s(t295);
                        if (!t296) goto ie294;
                        {
                            void * t298 = gm_l0;
                            gm_tl874t2atnlh3 t299 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l30) + 16));
                            gm_b16_8 t300 = gm_l31;
                            gm_tnpifv20irnph t301;
                            GM_ST(gm_b16_8, ((uint8_t *)&t301 + 0), t300);
                            gm_tabb710d9mq2e t302;
                            gm_f_format_k3kbc4n((void *)&t302, (void *)NULL, cx, NULL, t301);
                            if (gm_unw.kind) {
                                goto gm_ll108;
                            }
                            gm_tabb710d9mq2e t303 = t302;
                            if (((((uint8_t *)&t303))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t303) + 8, gm_srccvj6up3gbolp, 108, 36);
                            gm_b16_8 t304 = GM_LD(gm_b16_8, (((uint8_t *)&t303) + 8));
                            gm_b16_8 t305 = t304;
                            gm_b16_8 t306 = GM_LD(gm_b16_8, ((uint8_t *)&t305));
                            gm_b0_1 t307 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t298, t299, t306);
                            if (gm_unw.kind) {
                                goto gm_ll108;
                            }
                        }
                        goto ix294;
ie294:;
ix294:;
                    }
                    {
                        gm_t6qooom8qa2do t308;
                        memset(&t308, 0, sizeof t308);
                        GM_ST(uint8_t, ((uint8_t *)&t308 + 0), ((uint8_t)11ULL));
                        F.l32 = t308;
                    }
                    {
                        uint8_t t311 = GM_LD(uint8_t, ((((uint8_t *)&F.l12) + 16) + 0));
                        if (t311 != ((uint8_t)0ULL)) goto mn310;
                        gm_t08olq97rscn3 t312 = GM_LD(gm_t08olq97rscn3, (((((uint8_t *)&F.l12) + 16) + 8) + 0));
                        gm_l33 = t312;
                        gm_tlivgedjs5ps3 t313 = GM_LD(gm_tlivgedjs5ps3, (((((uint8_t *)&F.l12) + 16) + 8) + 32));
                        F.l34 = t313;
                        {
                            void * t316 = gm_l0;
                            uint64_t t317 = gm_l1;
                            void * t318 = (void *)((uint8_t *)&F.l34);
                            gm_t9805qtonbsag t319 = gm_f_declare_foreign_fn_u3dekca((void *)NULL, cx, NULL, t316, t317, &(*(gm_t08olq97rscn3 *)((uint8_t *)&gm_l33)), t318);
                            if (gm_unw.kind) {
                                goto gm_ll108;
                            }
                            gm_t9805qtonbsag t320 = t319;
                            if (!((((uint8_t *)&t320))[4] != 0)) goto ie315;
                            uint32_t t321 = GM_LD(uint32_t, ((uint8_t *)&t320));
                            gm_l35 = t321;
                            {
                                if (!((((uint8_t *)&gm_l14))[16] != 0)) goto ie323;
                                gm_tbvif938ns15l t324 = GM_LD(gm_tbvif938ns15l, ((uint8_t *)&gm_l14));
                                gm_l36 = t324;
                                {
                                    void * t326 = gm_l0;
                                    uint32_t t327 = gm_l35;
                                    uint8_t t328 = GM_LD(uint8_t, (((uint8_t *)&gm_l36) + 0));
                                    gm_tl874t2atnlh3 t329 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l36) + 4));
                                    gm_b0_1 t330 = gm_f_apply_callconv_197l87p((void *)NULL, cx, NULL, t326, t327, t328, t329);
                                    if (gm_unw.kind) {
                                        goto gm_ll108;
                                    }
                                }
                                goto ix323;
ie323:;
ix323:;
                            }
                            {
                                void * t331 = gm_l0;
                                void * t332 = (void *)(((uint8_t *)t331) + 1408);
                                gm_thognv9odov03 t333;
                                memset((uint8_t *)&t333 + 4, 0, 4);
                                uint32_t t334 = gm_l35;
                                GM_ST(uint32_t, (uint8_t *)&t333 + 0, t334);
                                gm_b16_8 t335 = gm_l4;
                                gm_b16_8 t336 = t335;
                                gm_b16_8 t337 = GM_LD(gm_b16_8, ((uint8_t *)&t336));
                                GM_ST(gm_b16_8, (uint8_t *)&t333 + 8, t337);
                                gm_b16_8 t338 = gm_l31;
                                GM_ST(gm_b16_8, (uint8_t *)&t333 + 24, t338);
                                gm_tftb1odc739t5 t339 = gm_f_push_krl2rii(t332, &t333);
                                gm_tftb1odc739t5 t340 = t339;
                                if (((((uint8_t *)&t340))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t340) + 1, gm_srccvj6up3gbolp, 115, 21);
                                gm_b0_1 t341 = GM_LD(gm_b0_1, (((uint8_t *)&t340) + 1));
                            }
                            {
                                uint32_t t342 = gm_l35;
                                gm_t6qooom8qa2do t343;
                                memset(&t343, 0, sizeof t343);
                                GM_ST(uint8_t, ((uint8_t *)&t343 + 0), ((uint8_t)5ULL));
                                GM_ST(uint32_t, ((uint8_t *)&t343 + 8), t342);
                                F.l32 = t343;
                            }
                            goto ix315;
ie315:;
ix315:;
                        }
                        goto me309;
mn310:;
                        uint8_t t345 = GM_LD(uint8_t, ((((uint8_t *)&F.l12) + 16) + 0));
                        if (t345 != ((uint8_t)1ULL)) goto mn344;
                        gm_t08olq97rscn3 t346 = GM_LD(gm_t08olq97rscn3, (((((uint8_t *)&F.l12) + 16) + 8) + 0));
                        gm_l37 = t346;
                        void * t347 = GM_LD(void *, (((((uint8_t *)&F.l12) + 16) + 8) + 32));
                        gm_l38 = t347;
                        {
                            void * t349 = gm_l0;
                            uint64_t t350 = gm_l1;
                            void * t351 = gm_l38;
                            gm_t88rcucg1b7uk t352 = gm_f_resolve_type_jjodbei((void *)NULL, cx, NULL, t349, t350, t351);
                            if (gm_unw.kind) {
                                goto gm_ll108;
                            }
                            gm_l39 = t352;
                        }
                        {
                            void * t354 = gm_l0;
                            void * t355 = (void *)(((uint8_t *)t354) + 8);
                            gm_t88rcucg1b7uk t356 = gm_l39;
                            uint8_t t357 = gm_f_is_error_8k27gd1(t355, GM_CONV(gm_csguankko4lmhd, t356));
                            uint8_t t358 = (uint8_t)!t357;
                            if (!t358) goto ie353;
                            void * t359 = gm_l0;
                            gm_t88rcucg1b7uk t360 = gm_l39;
                            uint8_t t361 = gm_f_is_c_compatible_2hi3vi8((void *)NULL, cx, NULL, t359, t360, ((uint8_t)0));
                            if (gm_unw.kind) {
                                goto gm_ll108;
                            }
                            uint8_t t362 = (uint8_t)!t361;
                            if (!t362) goto ie353;
                            {
                                void * t364 = gm_l0;
                                void * t365 = gm_l38;
                                gm_tl874t2atnlh3 t366 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t365) + 72));
                                void * t367 = gm_l0;
                                gm_t88rcucg1b7uk t368 = gm_l39;
                                gm_b16_8 t369 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t367, t368);
                                if (gm_unw.kind) {
                                    goto gm_ll108;
                                }
                                gm_tnpifv20irnph t370;
                                GM_ST(gm_b16_8, ((uint8_t *)&t370 + 0), t369);
                                gm_tabb710d9mq2e t371;
                                gm_f_format_oq9akg7((void *)&t371, (void *)NULL, cx, NULL, t370);
                                if (gm_unw.kind) {
                                    goto gm_ll108;
                                }
                                gm_tabb710d9mq2e t372 = t371;
                                if (((((uint8_t *)&t372))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t372) + 8, gm_srccvj6up3gbolp, 121, 102);
                                gm_b16_8 t373 = GM_LD(gm_b16_8, (((uint8_t *)&t372) + 8));
                                gm_b16_8 t374 = t373;
                                gm_b16_8 t375 = GM_LD(gm_b16_8, ((uint8_t *)&t374));
                                gm_b0_1 t376 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t364, t366, t375);
                                if (gm_unw.kind) {
                                    goto gm_ll108;
                                }
                            }
                            goto ix353;
ie353:;
ix353:;
                        }
                        {
                            gm_t88rcucg1b7uk t377 = gm_l39;
                            gm_t6qooom8qa2do t378;
                            memset(&t378, 0, sizeof t378);
                            GM_ST(uint8_t, ((uint8_t *)&t378 + 0), ((uint8_t)10ULL));
                            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t378 + 8), t377);
                            F.l32 = t378;
                        }
                        goto me309;
mn344:;
                        gm_trap("no match arm matched the scrutinee", gm_srccvj6up3gbolp, 111, 9);
me309:;
                    }
                    {
                        void * t380 = gm_l0;
                        void * t381 = GM_LD(void *, (((uint8_t *)t380) + 0));
                        uint64_t t382 = gm_l9;
                        gm_b16_8 t383 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l30) + 0));
                        gm_tf5srh4mp4b3i t384 = gm_f_lookup_59juqoo(t381, t382, t383);
                        gm_tf5srh4mp4b3i t385 = t384;
                        uint8_t t386;
                        if (((((uint8_t *)&t385))[8] != 0)) goto mn388;
                        t386 = ((uint8_t)0);
                        goto me387;
mn388:;
                        t386 = ((uint8_t)1);
                        goto me387;
mn389:;
                        gm_trap("no match arm matched the scrutinee", gm_srccvj6up3gbolp, 125, 12);
me387:;
                        if (!t386) goto ie379;
                        {
                            void * t391 = gm_l0;
                            gm_tl874t2atnlh3 t392 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l30) + 16));
                            gm_b16_8 t393 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l30) + 0));
                            gm_tnpifv20irnph t394;
                            GM_ST(gm_b16_8, ((uint8_t *)&t394 + 0), t393);
                            gm_tabb710d9mq2e t395;
                            gm_f_format_qds4t66((void *)&t395, (void *)NULL, cx, NULL, t394);
                            if (gm_unw.kind) {
                                goto gm_ll108;
                            }
                            gm_tabb710d9mq2e t396 = t395;
                            if (((((uint8_t *)&t396))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t396) + 8, gm_srccvj6up3gbolp, 126, 36);
                            gm_b16_8 t397 = GM_LD(gm_b16_8, (((uint8_t *)&t396) + 8));
                            gm_b16_8 t398 = t397;
                            gm_b16_8 t399 = GM_LD(gm_b16_8, ((uint8_t *)&t398));
                            gm_b0_1 t400 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t391, t392, t399);
                            if (gm_unw.kind) {
                                goto gm_ll108;
                            }
                        }
                        {
                            goto lc104;
                        }
                        goto ix379;
ie379:;
ix379:;
                    }
                    {
                        memset((uint8_t *)&F.l40 + 57, 0, 7);
                        GM_ST(uint32_t, (uint8_t *)&F.l40 + 0, ((uint32_t)4294967295ULL));
                        gm_tl874t2atnlh3 t401 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l12) + 128));
                        GM_ST(gm_tl874t2atnlh3, (uint8_t *)&F.l40 + 4, t401);
                        gm_th2lqbo77pvsc t402;
                        memset(&t402, 0, sizeof t402);
                        GM_ST(gm_th2lqbo77pvsc, (uint8_t *)&F.l40 + 16, t402);
                        gm_b16_8 t403 = gm_f_empty_slice_atlcs4j();
                        GM_ST(gm_b16_8, (uint8_t *)&F.l40 + 40, t403);
                        gm_t9qkh0rsooc8d t404;
                        GM_ST(uint8_t, ((uint8_t *)&t404 + 0), ((uint8_t)2ULL));
                        GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&F.l40 + 56, t404);
                        gm_tc3oht4ksth9d t405;
                        memset((uint8_t *)&t405 + 4, 0, 4);
                        memset((uint8_t *)&t405 + 84, 0, 4);
                        GM_ST(uint32_t, (uint8_t *)&t405 + 0, ((uint32_t)4294967295ULL));
                        gm_t2gtbh39ptekd t406;
                        memset(&t406, 0, sizeof t406);
                        GM_ST(uint8_t, ((uint8_t *)&t406 + 0), ((uint8_t)7ULL));
                        GM_ST(gm_t2gtbh39ptekd, (uint8_t *)&t405 + 8, t406);
                        gm_tl874t2atnlh3 t407 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l12) + 128));
                        GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t405 + 72, t407);
                        gm_tq7s98ihonkju t408 = gm_f_new_ve8bb01((void *)NULL, cx, NULL, &t405, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kp923qoaivn)));
                        if (gm_unw.kind) {
                            goto gm_ll108;
                        }
                        gm_tq7s98ihonkju t409 = t408;
                        if (((((uint8_t *)&t409))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t409) + 8, gm_srccvj6up3gbolp, 129, 139);
                        void * t410 = GM_LD(void *, (((uint8_t *)&t409) + 8));
                        gm_tkoope2qgegd9 t411;
                        memset(&t411, 0, sizeof t411);
                        GM_ST(uint8_t, ((uint8_t *)&t411 + 0), ((uint8_t)4ULL));
                        GM_ST(void *, ((uint8_t *)&t411 + 8), t410);
                        GM_ST(gm_tkoope2qgegd9, (uint8_t *)&F.l40 + 64, t411);
                    }
                    {
                        void * t412 = gm_l0;
                        void * t413 = GM_LD(void *, (((uint8_t *)t412) + 0));
                        uint64_t t414 = gm_l9;
                        uint64_t t415 = gm_f_add_member_0jvs7ug((void *)NULL, cx, NULL, t413, t414, &(*(gm_t08olq97rscn3 *)((uint8_t *)&gm_l30)), &(*(gm_tvpnktv5fo8nl *)((uint8_t *)&F.l40)));
                        if (gm_unw.kind) {
                            goto gm_ll108;
                        }
                        gm_l41 = t415;
                    }
                    {
                        void * t416 = gm_l0;
                        uint64_t t417 = gm_l41;
                        gm_t6qooom8qa2do t418 = F.l32;
                        gm_tagrkbqh054h7 t419;
                        GM_ST(uint8_t, (uint8_t *)&t419 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t419 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t419 + 4, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t419 + 0), ((uint8_t)2ULL));
                        GM_ST(gm_t6qooom8qa2do, ((uint8_t *)&t419 + 8), t418);
                        gm_b0_1 t420 = gm_f_set_state_3j300fi((void *)NULL, cx, NULL, t416, t417, &t419);
                        if (gm_unw.kind) {
                            goto gm_ll108;
                        }
                    }
                    {
                        uint8_t t422 = GM_LD(uint8_t, (((uint8_t *)&F.l32) + 0));
                        if (t422 != ((uint8_t)10ULL)) goto ie421;
                        gm_t88rcucg1b7uk t423 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l32) + 8));
                        gm_l42 = t423;
                        {
                            void * t425 = gm_l0;
                            void * t426 = (void *)(((uint8_t *)t425) + 1448);
                            gm_tq7qjpkmqmc5s t427;
                            memset((uint8_t *)&t427 + 28, 0, 4);
                            memset((uint8_t *)&t427 + 49, 0, 7);
                            uint64_t t428 = gm_l41;
                            GM_ST(uint64_t, (uint8_t *)&t427 + 0, t428);
                            gm_b16_8 t429 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l30) + 0));
                            GM_ST(gm_b16_8, (uint8_t *)&t427 + 8, t429);
                            gm_t88rcucg1b7uk t430 = gm_l42;
                            GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t427 + 24, t430);
                            uint64_t t431 = gm_l1;
                            GM_ST(uint64_t, (uint8_t *)&t427 + 32, t431);
                            GM_ST(void *, (uint8_t *)&t427 + 40, ((void *)0));
                            GM_ST(uint8_t, (uint8_t *)&t427 + 48, ((uint8_t)0));
                            gm_b16_8 t432 = gm_l31;
                            gm_th2lqbo77pvsc t433;
                            GM_ST(uint8_t, (uint8_t *)&t433 + 17, 0);
                            GM_ST(uint16_t, (uint8_t *)&t433 + 18, 0);
                            GM_ST(uint32_t, (uint8_t *)&t433 + 20, 0);
                            GM_ST(gm_b16_8, ((uint8_t *)&t433), t432);
                            ((uint8_t *)&t433)[16] = 1;
                            GM_ST(gm_th2lqbo77pvsc, (uint8_t *)&t427 + 56, t433);
                            gm_tf5srh4mp4b3i t434;
                            memset(&t434, 0, sizeof t434);
                            GM_ST(gm_tf5srh4mp4b3i, (uint8_t *)&t427 + 80, t434);
                            GM_ST(void *, (uint8_t *)&t427 + 96, ((void *)0));
                            gm_tftb1odc739t5 t435 = gm_f_push_4p3ivmb(t426, &t427);
                            gm_tftb1odc739t5 t436 = t435;
                            if (((((uint8_t *)&t436))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t436) + 1, gm_srccvj6up3gbolp, 133, 13);
                            gm_b0_1 t437 = GM_LD(gm_b0_1, (((uint8_t *)&t436) + 1));
                        }
                        goto ix421;
ie421:;
ix421:;
                    }
                    goto me113;
mn114:;
                    gm_trap("no match arm matched the scrutinee", gm_srccvj6up3gbolp, 66, 5);
me113:;
                }
lc104:;
                {
                    uint64_t t439 = gm_l11;
                    uint64_t r440; bool o441 = __builtin_add_overflow(t439, ((uint64_t)1ULL), &r440);
                    uint64_t t442 = r440;
                    gm_l11 = t442;
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
        uint64_t t443 = gm_l9;
        gm_t6qooom8qa2do t444;
        memset(&t444, 0, sizeof t444);
        GM_ST(uint8_t, ((uint8_t *)&t444 + 0), ((uint8_t)9ULL));
        GM_ST(uint64_t, ((uint8_t *)&t444 + 8), t443);
        uint8_t t445 = 0;
        F.ret = t444;
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
__attribute__((aligned(4))) gm_t9805qtonbsag gm_f_declare_foreign_fn_u3dekca(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, const void * a2, void * a3) {
    struct gm_fr_declare_foreign_fn_u3dekca F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* module */
    gm_t08olq97rscn3 gm_l2; /* name */
    void * gm_l3; /* sig */
    gm_tnpifv20irnph gm_l6; /* effects */
    gm_b16_8 gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    gm_t88rcucg1b7uk gm_l10; /* ret */
    gm_b16_8 gm_l13; /* <tmp> */
    uint64_t gm_l14; /* <tmp> */
    uint64_t gm_l15; /* i */
    uint32_t gm_l17; /* f */
    GM_STACK_CHECK(&F, 2370u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l3 = a3;
    {
        void * t2 = gm_l0;
        GM_ST(uint8_t, (((uint8_t *)t2) + 1364), ((uint8_t)1));
    }
    {
        void * t3 = gm_l0;
        uint64_t t4 = gm_l1;
        void * t5 = gm_l3;
        gm_b16_8 t6 = gm_f_empty_slice_3jt8vgk();
        gm_b16_8 t7 = t6;
        gm_b16_8 t8 = GM_LD(gm_b16_8, ((uint8_t *)&t7));
        gm_f_resolve_fn_sig_pamuhve((void *)&F.l4, (void *)NULL, cx, NULL, t3, t4, t5, ((uint8_t)0), t8);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t9 = gm_l0;
        GM_ST(uint8_t, (((uint8_t *)t9) + 1364), ((uint8_t)0));
    }
    {
        gm_tg93b272d11vo t10 = F.l4;
        gm_tg93b272d11vo t11 = t10;
        gm_tqqhoivahirr0 t12;
        if (((((uint8_t *)&t11))[136] != 0)) {
            gm_tqqhoivahirr0 t13 = GM_LD(gm_tqqhoivahirr0, (((uint8_t *)&t11) + 0));
            t12 = t13;
        } else {
            gm_t9805qtonbsag t14;
            memset(&t14, 0, sizeof t14);
            uint8_t t15 = 0;
            F.ret = t14;
            goto gm_fret;
        }
        F.l5 = t12;
    }
    {
        gm_tnpifv20irnph t16 = GM_LD(gm_tnpifv20irnph, (((uint8_t *)&F.l5) + 64));
        gm_l6 = t16;
    }
    {
        void * t18 = gm_l3;
        uint8_t t19;
        if ((((((uint8_t *)t18) + 32))[16] != 0)) goto mn21;
        t19 = ((uint8_t)1);
        goto me20;
mn21:;
        t19 = ((uint8_t)0);
        goto me20;
mn22:;
        gm_trap("no match arm matched the scrutinee", gm_srccvj6up3gbolp, 188, 8);
me20:;
        if (!t19) goto ie17;
        {
            void * t24 = gm_l0;
            gm_tnpifv20irnph t25 = gm_f_sys_set_pqe8mto((void *)NULL, cx, NULL, t24);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l6 = t25;
        }
        goto ix17;
ie17:;
ix17:;
    }
    {
        {
            gm_b16_8 t27 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 40));
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
                    gm_t5v0c8ia6a5mo t39 = GM_LD(gm_t5v0c8ia6a5mo, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l7)) + t35 * 56));
                    F.l9 = t39;
                    {
                        void * t42 = gm_l0;
                        void * t43 = (void *)(((uint8_t *)t42) + 8);
                        gm_t88rcucg1b7uk t44 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l9) + 16));
                        uint8_t t45 = gm_f_is_error_8k27gd1(t43, GM_CONV(gm_csguankko4lmhd, t44));
                        uint8_t t46 = (uint8_t)!t45;
                        if (!t46) goto ie41;
                        void * t47 = gm_l0;
                        gm_t88rcucg1b7uk t48 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l9) + 16));
                        uint8_t t49 = gm_f_is_c_compatible_2hi3vi8((void *)NULL, cx, NULL, t47, t48, ((uint8_t)0));
                        if (gm_unw.kind) {
                            goto gm_ll32;
                        }
                        uint8_t t50 = (uint8_t)!t49;
                        if (!t50) goto ie41;
                        {
                            void * t52 = gm_l0;
                            gm_tl874t2atnlh3 t53 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l9) + 20));
                            void * t54 = gm_l0;
                            gm_t88rcucg1b7uk t55 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l9) + 16));
                            gm_b16_8 t56 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t54, t55);
                            if (gm_unw.kind) {
                                goto gm_ll32;
                            }
                            gm_tnpifv20irnph t57;
                            GM_ST(gm_b16_8, ((uint8_t *)&t57 + 0), t56);
                            gm_tabb710d9mq2e t58;
                            gm_f_format_fea89f6((void *)&t58, (void *)NULL, cx, NULL, t57);
                            if (gm_unw.kind) {
                                goto gm_ll32;
                            }
                            gm_tabb710d9mq2e t59 = t58;
                            if (((((uint8_t *)&t59))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t59) + 8, gm_srccvj6up3gbolp, 190, 94);
                            gm_b16_8 t60 = GM_LD(gm_b16_8, (((uint8_t *)&t59) + 8));
                            gm_b16_8 t61 = t60;
                            gm_b16_8 t62 = GM_LD(gm_b16_8, ((uint8_t *)&t61));
                            gm_b0_1 t63 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t52, t53, t62);
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
                    gm_trap("no match arm matched the scrutinee", gm_srccvj6up3gbolp, 189, 5);
me37:;
                }
lc28:;
                {
                    uint64_t t65 = gm_l8;
                    uint64_t r66; bool o67 = __builtin_add_overflow(t65, ((uint64_t)1ULL), &r66);
                    uint64_t t68 = r66;
                    gm_l8 = t68;
                }
            }
gm_ll32:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb28; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc28; }
            }
            goto gm_lad;
lb28:;
        }
    }
    {
        gm_t88rcucg1b7uk t69 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l5) + 56));
        gm_l10 = t69;
    }
    {
        void * t71 = gm_l0;
        void * t72 = (void *)(((uint8_t *)t71) + 8);
        gm_t88rcucg1b7uk t73 = gm_l10;
        uint8_t t74 = gm_f_is_error_8k27gd1(t72, GM_CONV(gm_csguankko4lmhd, t73));
        uint8_t t75 = (uint8_t)!t74;
        if (!t75) goto ie70;
        void * t76 = gm_l0;
        void * t77 = (void *)(((uint8_t *)t76) + 8);
        gm_t88rcucg1b7uk t78 = gm_l10;
        uint8_t t79 = gm_f_is_unit_n83ttnb(t77, GM_CONV(gm_csguankko4lmhd, t78));
        uint8_t t80 = (uint8_t)!t79;
        if (!t80) goto ie70;
        void * t81 = gm_l0;
        void * t82 = (void *)(((uint8_t *)t81) + 8);
        gm_t88rcucg1b7uk t83 = gm_l10;
        gm_t53at80pfdus3 t84;
        gm_f_kind_jcnt4hm((void *)&t84, t82, GM_CONV(gm_csguankko4lmhd, t83));
        gm_t53at80pfdus3 t85 = t84;
        uint8_t t86 = (!(GM_LD(uint8_t, ((uint8_t *)&t85) + 0) == ((uint8_t)5ULL)));
        if (!t86) goto ie70;
        void * t87 = gm_l0;
        gm_t88rcucg1b7uk t88 = gm_l10;
        uint8_t t89 = gm_f_is_c_compatible_2hi3vi8((void *)NULL, cx, NULL, t87, t88, ((uint8_t)0));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t90 = (uint8_t)!t89;
        if (!t90) goto ie70;
        {
            void * t92 = gm_l0;
            gm_tl874t2atnlh3 t93 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)a2) + 16));
            void * t94 = gm_l0;
            gm_t88rcucg1b7uk t95 = gm_l10;
            gm_b16_8 t96 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t94, t95);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tnpifv20irnph t97;
            GM_ST(gm_b16_8, ((uint8_t *)&t97 + 0), t96);
            gm_tabb710d9mq2e t98;
            gm_f_format_mprr1lp((void *)&t98, (void *)NULL, cx, NULL, t97);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t99 = t98;
            if (((((uint8_t *)&t99))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t99) + 8, gm_srccvj6up3gbolp, 195, 28);
            gm_b16_8 t100 = GM_LD(gm_b16_8, (((uint8_t *)&t99) + 8));
            gm_b16_8 t101 = t100;
            gm_b16_8 t102 = GM_LD(gm_b16_8, ((uint8_t *)&t101));
            gm_b0_1 t103 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t92, t93, t102);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix70;
ie70:;
ix70:;
    }
    {
        gm_f_list_go2qg0b((void *)&F.l11, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_f_list_p9s2t99((void *)&F.l12, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t105 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 40));
            gm_b16_8 t106 = t105;
            gm_b16_8 t107 = GM_LD(gm_b16_8, ((uint8_t *)&t106));
            gm_l13 = t107;
        }
        {
            gm_l14 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t109 = gm_l14;
                uint64_t t110 = GM_LD(uintptr_t, ((uint8_t *)&gm_l13) + 8);
                uint8_t t111 = (t109 < t110);
                if (!t111) goto lb108;
                {
                    gm_tgsvmddj3e85e t114;
                    uint64_t t115 = gm_l14;
                    GM_ST(uint64_t, (uint8_t *)&t114 + 0, t115);
                    uint64_t t116 = gm_l14;
                    uint64_t t117 = (uintptr_t)t116;
                    uint64_t t118 = GM_LD(uintptr_t, ((uint8_t *)&gm_l13) + 8);
                    memcpy((uint8_t *)&t114 + 8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l13)) + t117 * 56), 56);
                    gm_tgsvmddj3e85e t119 = t114;
                    uint64_t t122 = GM_LD(uint64_t, (((uint8_t *)&t119) + 0));
                    gm_l15 = t122;
                    gm_t5v0c8ia6a5mo t123 = GM_LD(gm_t5v0c8ia6a5mo, (((uint8_t *)&t119) + 8));
                    F.l16 = t123;
                    {
                        void * t125 = (void *)((uint8_t *)&F.l12);
                        gm_tsoie1ppgdvcg t126;
                        memset((uint8_t *)&t126 + 21, 0, 3);
                        memset((uint8_t *)&t126 + 36, 0, 4);
                        gm_b16_8 t127 = GM_LD(gm_b16_8, (((uint8_t *)&F.l16) + 0));
                        GM_ST(gm_b16_8, (uint8_t *)&t126 + 0, t127);
                        gm_t88rcucg1b7uk t128 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l16) + 16));
                        GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t126 + 16, t128);
                        GM_ST(uint8_t, (uint8_t *)&t126 + 20, ((uint8_t)0));
                        gm_tl874t2atnlh3 t129 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l16) + 20));
                        GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t126 + 24, t129);
                        gm_tftb1odc739t5 t130 = gm_f_push_am7bo0v(t125, &t126);
                        gm_tftb1odc739t5 t131 = t130;
                        if (((((uint8_t *)&t131))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t131) + 1, gm_srccvj6up3gbolp, 200, 9);
                        gm_b0_1 t132 = GM_LD(gm_b0_1, (((uint8_t *)&t131) + 1));
                    }
                    {
                        void * t133 = (void *)((uint8_t *)&F.l11);
                        uint64_t t134 = gm_l15;
                        uint32_t t135 = (uint32_t)t134;
                        gm_tftb1odc739t5 t136 = gm_f_push_im9d4ro(t133, t135);
                        gm_tftb1odc739t5 t137 = t136;
                        if (((((uint8_t *)&t137))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t137) + 1, gm_srccvj6up3gbolp, 201, 9);
                        gm_b0_1 t138 = GM_LD(gm_b0_1, (((uint8_t *)&t137) + 1));
                    }
                    goto me120;
mn121:;
                    gm_trap("no match arm matched the scrutinee", gm_srccvj6up3gbolp, 199, 5);
me120:;
                }
lc108:;
                {
                    uint64_t t140 = gm_l14;
                    uint64_t r141; bool o142 = __builtin_add_overflow(t140, ((uint64_t)1ULL), &r141);
                    uint64_t t143 = r141;
                    gm_l14 = t143;
                }
            }
lb108:;
        }
    }
    {
        void * t144 = gm_l0;
        gm_tqqhoivahirr0 t145;
        memset((uint8_t *)&t145 + 28, 0, 4);
        memset((uint8_t *)&t145 + 60, 0, 4);
        memset((uint8_t *)&t145 + 129, 0, 7);
        gm_b16_8 t146 = GM_LD(gm_b16_8, (((uint8_t *)a2) + 0));
        GM_ST(gm_b16_8, (uint8_t *)&t145 + 0, t146);
        gm_tl874t2atnlh3 t147 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)a2) + 16));
        GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t145 + 16, t147);
        uint64_t t148 = gm_l1;
        GM_ST(uint64_t, (uint8_t *)&t145 + 32, t148);
        gm_b16_8 t149 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 40));
        GM_ST(gm_b16_8, (uint8_t *)&t145 + 40, t149);
        gm_t88rcucg1b7uk t150 = gm_l10;
        GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t145 + 56, t150);
        gm_tnpifv20irnph t151 = gm_l6;
        GM_ST(gm_tnpifv20irnph, (uint8_t *)&t145 + 64, t151);
        gm_b16_8 t152 = gm_f_empty_slice_d914i5u();
        GM_ST(gm_b16_8, (uint8_t *)&t145 + 80, t152);
        gm_tqu3m2l3n8mnc t153;
        memset(&t153, 0, sizeof t153);
        GM_ST(gm_tqu3m2l3n8mnc, (uint8_t *)&t145 + 96, t153);
        GM_ST(uint8_t, (uint8_t *)&t145 + 128, ((uint8_t)0));
        gm_tdgfduqjheqdm t154;
        memset((uint8_t *)&t154 + 1, 0, 7);
        memset((uint8_t *)&t154 + 57, 0, 7);
        gm_t9qkh0rsooc8d t155;
        GM_ST(uint8_t, ((uint8_t *)&t155 + 0), ((uint8_t)3ULL));
        GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&t154 + 0, t155);
        gm_b16_8 t156 = GM_LD(gm_b16_8, (((uint8_t *)&F.l11) + 0));
        GM_ST(gm_b16_8, (uint8_t *)&t154 + 8, t156);
        gm_b16_8 t157 = GM_LD(gm_b16_8, (((uint8_t *)&F.l12) + 0));
        GM_ST(gm_b16_8, (uint8_t *)&t154 + 24, t157);
        GM_ST(void *, (uint8_t *)&t154 + 40, ((void *)0));
        gm_t9805qtonbsag t158;
        memset(&t158, 0, sizeof t158);
        GM_ST(gm_t9805qtonbsag, (uint8_t *)&t154 + 48, t158);
        GM_ST(uint8_t, (uint8_t *)&t154 + 56, ((uint8_t)0));
        uint32_t t159 = gm_f_add_fn_t4t83qh((void *)NULL, cx, NULL, t144, &t145, &t154);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l17 = t159;
    }
    {
        void * t161 = gm_l3;
        uint8_t t162 = GM_LD(uint8_t, (((uint8_t *)t161) + 16));
        if (!t162) goto ie160;
        {
            void * t164 = gm_l0;
            void * t165 = (void *)(((uint8_t *)t164) + 1368);
            uint32_t t166 = gm_l17;
            gm_tftb1odc739t5 t167 = gm_f_push_im9d4ro(t165, t166);
            gm_tftb1odc739t5 t168 = t167;
            if (((((uint8_t *)&t168))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t168) + 1, gm_srccvj6up3gbolp, 207, 23);
            gm_b0_1 t169 = GM_LD(gm_b0_1, (((uint8_t *)&t168) + 1));
        }
        goto ix160;
ie160:;
ix160:;
    }
    {
        uint32_t t170 = gm_l17;
        gm_t9805qtonbsag t171;
        GM_ST(uint8_t, (uint8_t *)&t171 + 5, 0);
        GM_ST(uint16_t, (uint8_t *)&t171 + 6, 0);
        GM_ST(uint32_t, ((uint8_t *)&t171), t170);
        ((uint8_t *)&t171)[4] = 1;
        uint8_t t172 = 0;
        F.ret = t171;
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
__attribute__((aligned(4))) void gm_f_declare_global_729nttf(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, const void * a2, void * a3) {
    struct gm_fr_declare_global_729nttf F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* module */
    gm_t08olq97rscn3 gm_l2; /* name */
    void * gm_l3; /* b */
    void * gm_l4; /* ty_expr */
    gm_t88rcucg1b7uk gm_l5; /* ty */
    void * gm_l6; /* hook */
    GM_STACK_CHECK(&F, 1987u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l3 = a3;
    {
        void * t2 = gm_l3;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 24));
        void * t4 = t3;
        void * t5;
        if ((GM_LD(void *, ((uint8_t *)&t4)) != 0)) {
            void * t6 = GM_LD(void *, (((uint8_t *)&t4) + 0));
            t5 = t6;
        } else {
            {
                void * t8 = gm_l0;
                gm_tl874t2atnlh3 t9 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)a2) + 16));
                gm_b16_8 t10;
                GM_ST(void *, (uint8_t *)&t10, (void *)(gm_s81csu1l86dmv));
                GM_ST(uintptr_t, (uint8_t *)&t10 + 8, (uintptr_t)(51));
                gm_b0_1 t11 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t8, t9, t10);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_t6qooom8qa2do t12;
            memset(&t12, 0, sizeof t12);
            GM_ST(uint8_t, ((uint8_t *)&t12 + 0), ((uint8_t)11ULL));
            uint8_t t13 = 0;
            F.ret = t12;
            goto gm_fret;
        }
        gm_l4 = t5;
    }
    {
        void * t14 = gm_l0;
        uint64_t t15 = gm_l1;
        void * t16 = gm_l4;
        gm_t88rcucg1b7uk t17 = gm_f_resolve_type_jjodbei((void *)NULL, cx, NULL, t14, t15, t16);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l5 = t17;
    }
    {
        void * t19 = gm_l0;
        void * t20 = (void *)(((uint8_t *)t19) + 8);
        gm_t88rcucg1b7uk t21 = gm_l5;
        uint8_t t22 = gm_f_is_error_8k27gd1(t20, GM_CONV(gm_csguankko4lmhd, t21));
        if (!t22) goto ie18;
        {
            gm_t6qooom8qa2do t24;
            memset(&t24, 0, sizeof t24);
            GM_ST(uint8_t, ((uint8_t *)&t24 + 0), ((uint8_t)11ULL));
            uint8_t t25 = 0;
            F.ret = t24;
            goto gm_fret;
        }
        goto ix18;
ie18:;
ix18:;
    }
    {
        void * t27 = gm_l0;
        uint64_t t28 = gm_l1;
        void * t29 = gm_l3;
        void * t30 = GM_LD(void *, (((uint8_t *)t29) + 40));
        gm_t88rcucg1b7uk t31 = gm_l5;
        gm_tkn42u7na3t81 t32;
        GM_ST(uint8_t, (uint8_t *)&t32 + 5, 0);
        GM_ST(uint16_t, (uint8_t *)&t32 + 6, 0);
        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t32), t31);
        ((uint8_t *)&t32)[4] = 1;
        gm_tkn42u7na3t81 t33 = gm_f_const_check_d48mnu7((void *)NULL, cx, NULL, t27, t28, t30, t32);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tkn42u7na3t81 t34 = t33;
        uint8_t t35;
        if (((((uint8_t *)&t34))[4] != 0)) goto mn37;
        t35 = ((uint8_t)0);
        goto me36;
mn37:;
        t35 = ((uint8_t)1);
        goto me36;
mn38:;
        gm_trap("no match arm matched the scrutinee", gm_srccvj6up3gbolp, 40, 8);
me36:;
        if (!t35) goto ie26;
        {
            gm_t88rcucg1b7uk t40 = gm_l5;
            gm_t6qooom8qa2do t41;
            memset(&t41, 0, sizeof t41);
            GM_ST(uint8_t, ((uint8_t *)&t41 + 0), ((uint8_t)10ULL));
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t41 + 8), t40);
            uint8_t t42 = 0;
            F.ret = t41;
            goto gm_fret;
        }
        goto ix26;
ie26:;
ix26:;
    }
    {
        void * t44 = gm_l0;
        void * t45 = GM_LD(void *, (((uint8_t *)t44) + 2168));
        void * t46 = t45;
        if (!(GM_LD(void *, ((uint8_t *)&t46)) != 0)) goto ie43;
        void * t47 = GM_LD(void *, ((uint8_t *)&t46));
        gm_l6 = t47;
        void * t48 = gm_l0;
        void * t49 = (void *)(((uint8_t *)t48) + 8);
        gm_t88rcucg1b7uk t50 = gm_l5;
        uint8_t t51 = gm_f_has_param_u9t0efs(t49, GM_CONV(gm_csguankko4lmhd, t50));
        uint8_t t52 = (uint8_t)!t51;
        if (!t52) goto ie43;
        {
            {
                void * t55 = gm_l6;
                void * t56 = gm_l0;
                uint64_t t57 = gm_l1;
                void * t58 = gm_l3;
                void * t59 = GM_LD(void *, (((uint8_t *)t58) + 40));
                gm_t88rcucg1b7uk t60 = gm_l5;
                gm_tkn42u7na3t81 t61;
                GM_ST(uint8_t, (uint8_t *)&t61 + 5, 0);
                GM_ST(uint16_t, (uint8_t *)&t61 + 6, 0);
                GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t61), t60);
                ((uint8_t *)&t61)[4] = 1;
                gm_b16_8 t62;
                GM_ST(void *, (uint8_t *)&t62, (void *)(gm_sbd6ket0ql5ab));
                GM_ST(uintptr_t, (uint8_t *)&t62 + 8, (uintptr_t)(8));
                void * t63 = gm_l3;
                void * t64 = GM_LD(void *, (((uint8_t *)t63) + 40));
                gm_tl874t2atnlh3 t65 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t64) + 72));
                void * t66 = t55;
                gm_t9805qtonbsag t67;
                if (GM_PTR_TAGGED(t66)) {
                    t67 = ((gm_t9805qtonbsag (*)(void *, uint64_t, void *, gm_tkn42u7na3t81, gm_b16_8, gm_cseahvg8v7p0vs))GM_PTR_UNTAG(t66))(t56, t57, t59, t61, t62, GM_CONV(gm_cseahvg8v7p0vs, t65));
                } else {
                    t67 = ((gm_t9805qtonbsag (*)(void *, gm_node *, gm_node *, void *, uint64_t, void *, gm_tkn42u7na3t81, gm_b16_8, gm_tl874t2atnlh3))t66)(NULL, cx, NULL, t56, t57, t59, t61, t62, t65);
                }
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_t9805qtonbsag t68 = t67;
                uint32_t t69;
                if (((((uint8_t *)&t68))[4] != 0)) {
                    uint32_t t70 = GM_LD(uint32_t, (((uint8_t *)&t68) + 0));
                    t69 = t70;
                } else {
                    gm_t6qooom8qa2do t71;
                    memset(&t71, 0, sizeof t71);
                    GM_ST(uint8_t, ((uint8_t *)&t71 + 0), ((uint8_t)11ULL));
                    uint8_t t72 = 0;
                    F.ret = t71;
                    goto gm_fret;
                }
            }
        }
        {
            gm_t88rcucg1b7uk t73 = gm_l5;
            gm_t6qooom8qa2do t74;
            memset(&t74, 0, sizeof t74);
            GM_ST(uint8_t, ((uint8_t *)&t74 + 0), ((uint8_t)10ULL));
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t74 + 8), t73);
            uint8_t t75 = 0;
            F.ret = t74;
            goto gm_fret;
        }
        goto ix43;
ie43:;
ix43:;
    }
    {
        {
            void * t77 = gm_l0;
            uint64_t t78 = gm_l1;
            void * t79 = gm_l3;
            void * t80 = GM_LD(void *, (((uint8_t *)t79) + 40));
            gm_t88rcucg1b7uk t81 = gm_l5;
            gm_tkn42u7na3t81 t82;
            GM_ST(uint8_t, (uint8_t *)&t82 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t82 + 6, 0);
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t82), t81);
            ((uint8_t *)&t82)[4] = 1;
            gm_tkn42u7na3t81 t83 = gm_f_const_type_7rijs95((void *)NULL, cx, NULL, t77, t78, t80, t82);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tkn42u7na3t81 t84 = t83;
            gm_t88rcucg1b7uk t85;
            if (((((uint8_t *)&t84))[4] != 0)) {
                gm_t88rcucg1b7uk t86 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t84) + 0));
                t85 = t86;
            } else {
                gm_t6qooom8qa2do t87;
                memset(&t87, 0, sizeof t87);
                GM_ST(uint8_t, ((uint8_t *)&t87 + 0), ((uint8_t)11ULL));
                uint8_t t88 = 0;
                F.ret = t87;
                goto gm_fret;
            }
        }
    }
    {
        gm_t88rcucg1b7uk t89 = gm_l5;
        gm_t6qooom8qa2do t90;
        memset(&t90, 0, sizeof t90);
        GM_ST(uint8_t, ((uint8_t *)&t90 + 0), ((uint8_t)10ULL));
        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t90 + 8), t89);
        uint8_t t91 = 0;
        F.ret = t90;
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
__attribute__((aligned(4))) gm_b16_8 gm_f_escape_debug_lu2p5se(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_escape_debug_lu2p5se F;
    gm_b16_8 gm_l0; /* name */
    gm_b16_8 gm_l2; /* rest */
    gm_tb31k5l794t7p gm_l3; /* r */
    uint32_t gm_l4; /* c */
    GM_STACK_CHECK(&F, 635u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_b16_8 t2 = gm_l0;
        gm_l2 = t2;
    }
    {
        for (;;) {
            uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
            uint8_t t5 = (t4 > ((uint64_t)0ULL));
            if (!t5) goto lb3;
            {
                gm_b16_8 t8 = gm_l2;
                gm_tgmjda3f8g6hg t9;
                gm_f_decode_rune_ugbib8e((void *)&t9, t8);
                gm_tgmjda3f8g6hg t10 = t9;
                gm_tb31k5l794t7p t11;
                if (((((uint8_t *)&t10))[16] != 0)) {
                    gm_tb31k5l794t7p t12 = GM_LD(gm_tb31k5l794t7p, (((uint8_t *)&t10) + 0));
                    t11 = t12;
                } else {
                    goto lb3;
                }
                gm_l3 = t11;
            }
            {
                uint32_t t13 = GM_LD(uint32_t, (((uint8_t *)&gm_l3) + 0));
                uint32_t t14 = t13;
                uint32_t t17 = GM_LD(uint32_t, ((uint8_t *)&t14));
                if (!(t17 == ((uint32_t)34ULL))) goto mn16;
                void * t18 = (void *)((uint8_t *)&F.l1);
                gm_b16_8 t19;
                GM_ST(void *, (uint8_t *)&t19, (void *)(gm_smee1janbl2t9));
                GM_ST(uintptr_t, (uint8_t *)&t19 + 8, (uintptr_t)(2));
                gm_tftb1odc739t5 t20 = gm_f_extend_0cs85gs(t18, t19);
                gm_tftb1odc739t5 t21 = t20;
                if (((((uint8_t *)&t21))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t21) + 1, gm_srccvj6up3gbolp, 147, 20);
                gm_b0_1 t22 = GM_LD(gm_b0_1, (((uint8_t *)&t21) + 1));
                goto me15;
mn16:;
                uint32_t t24 = GM_LD(uint32_t, ((uint8_t *)&t14));
                if (!(t24 == ((uint32_t)39ULL))) goto mn23;
                void * t25 = (void *)((uint8_t *)&F.l1);
                gm_b16_8 t26;
                GM_ST(void *, (uint8_t *)&t26, (void *)(gm_sj2jft9n0jm50));
                GM_ST(uintptr_t, (uint8_t *)&t26 + 8, (uintptr_t)(2));
                gm_tftb1odc739t5 t27 = gm_f_extend_0cs85gs(t25, t26);
                gm_tftb1odc739t5 t28 = t27;
                if (((((uint8_t *)&t28))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t28) + 1, gm_srccvj6up3gbolp, 148, 21);
                gm_b0_1 t29 = GM_LD(gm_b0_1, (((uint8_t *)&t28) + 1));
                goto me15;
mn23:;
                uint32_t t31 = GM_LD(uint32_t, ((uint8_t *)&t14));
                if (!(t31 == ((uint32_t)92ULL))) goto mn30;
                void * t32 = (void *)((uint8_t *)&F.l1);
                gm_b16_8 t33;
                GM_ST(void *, (uint8_t *)&t33, (void *)(gm_s3p8gug7bvl1p));
                GM_ST(uintptr_t, (uint8_t *)&t33 + 8, (uintptr_t)(2));
                gm_tftb1odc739t5 t34 = gm_f_extend_0cs85gs(t32, t33);
                gm_tftb1odc739t5 t35 = t34;
                if (((((uint8_t *)&t35))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t35) + 1, gm_srccvj6up3gbolp, 149, 21);
                gm_b0_1 t36 = GM_LD(gm_b0_1, (((uint8_t *)&t35) + 1));
                goto me15;
mn30:;
                uint32_t t38 = GM_LD(uint32_t, ((uint8_t *)&t14));
                if (!(t38 == ((uint32_t)10ULL))) goto mn37;
                void * t39 = (void *)((uint8_t *)&F.l1);
                gm_b16_8 t40;
                GM_ST(void *, (uint8_t *)&t40, (void *)(gm_s6gt8b7hsisgu));
                GM_ST(uintptr_t, (uint8_t *)&t40 + 8, (uintptr_t)(2));
                gm_tftb1odc739t5 t41 = gm_f_extend_0cs85gs(t39, t40);
                gm_tftb1odc739t5 t42 = t41;
                if (((((uint8_t *)&t42))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t42) + 1, gm_srccvj6up3gbolp, 150, 21);
                gm_b0_1 t43 = GM_LD(gm_b0_1, (((uint8_t *)&t42) + 1));
                goto me15;
mn37:;
                uint32_t t45 = GM_LD(uint32_t, ((uint8_t *)&t14));
                if (!(t45 == ((uint32_t)13ULL))) goto mn44;
                void * t46 = (void *)((uint8_t *)&F.l1);
                gm_b16_8 t47;
                GM_ST(void *, (uint8_t *)&t47, (void *)(gm_s9cb6j5ok7bdk));
                GM_ST(uintptr_t, (uint8_t *)&t47 + 8, (uintptr_t)(2));
                gm_tftb1odc739t5 t48 = gm_f_extend_0cs85gs(t46, t47);
                gm_tftb1odc739t5 t49 = t48;
                if (((((uint8_t *)&t49))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t49) + 1, gm_srccvj6up3gbolp, 151, 21);
                gm_b0_1 t50 = GM_LD(gm_b0_1, (((uint8_t *)&t49) + 1));
                goto me15;
mn44:;
                uint32_t t52 = GM_LD(uint32_t, ((uint8_t *)&t14));
                if (!(t52 == ((uint32_t)9ULL))) goto mn51;
                void * t53 = (void *)((uint8_t *)&F.l1);
                gm_b16_8 t54;
                GM_ST(void *, (uint8_t *)&t54, (void *)(gm_s22g482qk7a40));
                GM_ST(uintptr_t, (uint8_t *)&t54 + 8, (uintptr_t)(2));
                gm_tftb1odc739t5 t55 = gm_f_extend_0cs85gs(t53, t54);
                gm_tftb1odc739t5 t56 = t55;
                if (((((uint8_t *)&t56))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t56) + 1, gm_srccvj6up3gbolp, 152, 21);
                gm_b0_1 t57 = GM_LD(gm_b0_1, (((uint8_t *)&t56) + 1));
                goto me15;
mn51:;
                uint32_t t59 = GM_LD(uint32_t, ((uint8_t *)&t14));
                if (!(t59 == ((uint32_t)0ULL))) goto mn58;
                void * t60 = (void *)((uint8_t *)&F.l1);
                gm_b16_8 t61;
                GM_ST(void *, (uint8_t *)&t61, (void *)(gm_s2v5amvfkfoll));
                GM_ST(uintptr_t, (uint8_t *)&t61 + 8, (uintptr_t)(2));
                gm_tftb1odc739t5 t62 = gm_f_extend_0cs85gs(t60, t61);
                gm_tftb1odc739t5 t63 = t62;
                if (((((uint8_t *)&t63))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t63) + 1, gm_srccvj6up3gbolp, 153, 18);
                gm_b0_1 t64 = GM_LD(gm_b0_1, (((uint8_t *)&t63) + 1));
                goto me15;
mn58:;
                uint32_t t66 = GM_LD(uint32_t, ((uint8_t *)&t14));
                gm_l4 = t66;
                {
                    uint8_t t69 = 0;
                    uint32_t t70 = gm_l4;
                    uint8_t t71 = (t70 < ((uint32_t)32ULL));
                    if (!t71) {
                        uint8_t t72 = 0;
                        uint32_t t73 = gm_l4;
                        uint8_t t74 = (t73 >= ((uint32_t)127ULL));
                        if (t74) {
                            uint32_t t75 = gm_l4;
                            uint8_t t76 = (t75 <= ((uint32_t)159ULL));
                            t72 = t76;
                        } else t72 = 0;
                        t69 = t72;
                    } else t69 = 1;
                    if (!t69) goto ie68;
                    {
                        void * t78 = (void *)((uint8_t *)&F.l1);
                        uint32_t t79 = gm_l4;
                        gm_t88rcucg1b7uk t80;
                        GM_ST(uint32_t, ((uint8_t *)&t80 + 0), t79);
                        gm_tftb1odc739t5 t81 = gm_f_write_146u7aa(t78, t80);
                        gm_tftb1odc739t5 t82 = t81;
                        if (((((uint8_t *)&t82))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t82) + 1, gm_srccvj6up3gbolp, 156, 21);
                        gm_b0_1 t83 = GM_LD(gm_b0_1, (((uint8_t *)&t82) + 1));
                    }
                    goto ix68;
ie68:;
                    {
                        void * t85 = (void *)((uint8_t *)&F.l1);
                        uint64_t t86 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                        void * t87 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)));
                        uint64_t t88 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                        uint64_t t89 = (uintptr_t)t88;
                        if ((uintptr_t)0 > t89 || t89 > (uintptr_t)t86) gm_trap_slice((uintptr_t)0, t89, t86, gm_srccvj6up3gbolp, 158, 32);
                        gm_b16_8 t90;
                        GM_ST(void *, (uint8_t *)&t90, (void *)(((uint8_t *)t87 + (uintptr_t)0 * 1)));
                        GM_ST(uintptr_t, (uint8_t *)&t90 + 8, (uintptr_t)(t89 - (uintptr_t)0));
                        gm_tftb1odc739t5 t91 = gm_f_extend_0cs85gs(t85, t90);
                        gm_tftb1odc739t5 t92 = t91;
                        if (((((uint8_t *)&t92))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t92) + 1, gm_srccvj6up3gbolp, 158, 21);
                        gm_b0_1 t93 = GM_LD(gm_b0_1, (((uint8_t *)&t92) + 1));
                    }
ix68:;
                }
                goto me15;
mn65:;
                gm_trap("no match arm matched the scrutinee", gm_srccvj6up3gbolp, 146, 9);
me15:;
            }
            {
                uint64_t t94 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                void * t95 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)));
                uint64_t t96 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                uint64_t t97 = (uintptr_t)t96;
                if (t97 > (uintptr_t)t94 || (uintptr_t)t94 > (uintptr_t)t94) gm_trap_slice(t97, (uintptr_t)t94, t94, gm_srccvj6up3gbolp, 162, 16);
                gm_b16_8 t98;
                GM_ST(void *, (uint8_t *)&t98, (void *)(((uint8_t *)t95 + t97 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t98 + 8, (uintptr_t)((uintptr_t)t94 - t97));
                gm_l2 = t98;
            }
lc3:;
        }
lb3:;
    }
    {
        gm_b16_8 t99 = GM_LD(gm_b16_8, (((uint8_t *)&F.l1) + 0));
        gm_b16_8 t100 = t99;
        gm_b16_8 t101 = GM_LD(gm_b16_8, ((uint8_t *)&t100));
        uint8_t t102 = 0;
        F.ret = t101;
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
__attribute__((aligned(4))) gm_b0_1 gm_f_export_fn_8hcuasc(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_b16_8 a2, gm_tl874t2atnlh3 a3) {
    struct gm_fr_export_fn_8hcuasc F;
    void * gm_l0; /* s */
    uint32_t gm_l1; /* f */
    gm_b16_8 gm_l2; /* sym */
    gm_tl874t2atnlh3 gm_l3; /* span */
    uint8_t gm_l5; /* ident */
    gm_b16_8 gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    uint8_t gm_l8; /* c */
    gm_b16_8 gm_l9; /* <tmp> */
    uint64_t gm_l10; /* <tmp> */
    GM_STACK_CHECK(&F, 1628u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        uint32_t t2 = gm_l1;
        uint64_t t3 = (uint64_t)t2;
        uint64_t t4 = (uintptr_t)t3;
        void * t5 = gm_l0;
        uint64_t t6 = GM_LD(uintptr_t, ((((uint8_t *)t5) + 344) + 0) + 8);
        if (t4 >= (uintptr_t)(t6)) gm_trap_index(t4, t6, gm_srccvj6up3gbolp, 281, 12);
        gm_tqqhoivahirr0 t7 = GM_LD(gm_tqqhoivahirr0, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t5) + 344) + 0)) + t4 * 136));
        F.l4 = t7;
    }
    {
        uint8_t t8 = 0;
        uint64_t t9 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        uint8_t t10 = (t9 > ((uint64_t)0ULL));
        if (t10) {
            uint8_t t11 = 0;
            uint64_t t12 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t13 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
            if (t12 >= (uintptr_t)(t13)) gm_trap_index(t12, t13, gm_srccvj6up3gbolp, 284, 46);
            uint8_t t14 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t12 * 1));
            uint8_t t15 = gm_f_is_alpha_12215ga(t14);
            if (!t15) {
                uint64_t t16 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t17 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                if (t16 >= (uintptr_t)(t17)) gm_trap_index(t16, t17, gm_srccvj6up3gbolp, 284, 57);
                uint8_t t18 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t16 * 1));
                uint8_t t19 = ((t18 == ((uint8_t)95ULL)));
                t11 = t19;
            } else t11 = 1;
            t8 = t11;
        } else t8 = 0;
        gm_l5 = t8;
    }
    {
        {
            gm_b16_8 t21 = gm_l2;
            gm_l6 = t21;
        }
        {
            gm_l7 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t23 = gm_l7;
                uint64_t t24 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                uint8_t t25 = (t23 < t24);
                if (!t25) goto lb22;
                {
                    uint64_t t28 = gm_l7;
                    uint64_t t29 = (uintptr_t)t28;
                    uint64_t t30 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                    uint8_t t31 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t29 * 1));
                    uint8_t t32 = t31;
                    uint8_t t35 = GM_LD(uint8_t, ((uint8_t *)&t32));
                    gm_l8 = t35;
                    {
                        uint8_t t38 = 0;
                        uint8_t t39 = gm_l8;
                        uint8_t t40 = gm_f_is_alnum_n81ervr(t39);
                        if (!t40) {
                            uint8_t t41 = gm_l8;
                            uint8_t t42 = ((t41 == ((uint8_t)95ULL)));
                            t38 = t42;
                        } else t38 = 1;
                        uint8_t t43 = (uint8_t)!t38;
                        if (!t43) goto ie37;
                        {
                            gm_l5 = ((uint8_t)0);
                        }
                        goto ix37;
ie37:;
ix37:;
                    }
                    goto me33;
mn34:;
                    gm_trap("no match arm matched the scrutinee", gm_srccvj6up3gbolp, 285, 5);
me33:;
                }
lc22:;
                {
                    uint64_t t46 = gm_l7;
                    uint64_t r47; bool o48 = __builtin_add_overflow(t46, ((uint64_t)1ULL), &r47);
                    uint64_t t49 = r47;
                    gm_l7 = t49;
                }
            }
lb22:;
        }
    }
    {
        uint8_t t51 = gm_l5;
        uint8_t t52 = (uint8_t)!t51;
        if (!t52) goto ie50;
        {
            void * t54 = gm_l0;
            gm_tl874t2atnlh3 t55 = gm_l3;
            gm_b16_8 t56 = gm_l2;
            gm_tnpifv20irnph t57;
            GM_ST(gm_b16_8, ((uint8_t *)&t57 + 0), t56);
            gm_tabb710d9mq2e t58;
            gm_f_format_mh1raea((void *)&t58, (void *)NULL, cx, NULL, t57);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t59 = t58;
            if (((((uint8_t *)&t59))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t59) + 8, gm_srccvj6up3gbolp, 289, 23);
            gm_b16_8 t60 = GM_LD(gm_b16_8, (((uint8_t *)&t59) + 8));
            gm_b16_8 t61 = t60;
            gm_b16_8 t62 = GM_LD(gm_b16_8, ((uint8_t *)&t61));
            gm_b0_1 t63 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t54, t55, t62);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_b0_1 t64;
            uint8_t t65 = 0;
            F.ret = t64;
            goto gm_fret;
        }
        goto ix50;
ie50:;
ix50:;
    }
    {
        uint8_t t67 = (((GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8) == 4) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l2)), gm_sog4rksrmsqd2, 4) == 0));
        if (!t67) goto ie66;
        {
            void * t69 = gm_l0;
            gm_tl874t2atnlh3 t70 = gm_l3;
            gm_b16_8 t71;
            GM_ST(void *, (uint8_t *)&t71, (void *)(gm_s310nl8k9dnl2));
            GM_ST(uintptr_t, (uint8_t *)&t71 + 8, (uintptr_t)(58));
            gm_b0_1 t72 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t69, t70, t71);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_b0_1 t73;
            uint8_t t74 = 0;
            F.ret = t73;
            goto gm_fret;
        }
        goto ix66;
ie66:;
ix66:;
    }
    {
        gm_b16_8 t76 = gm_l2;
        uint8_t t77 = gm_f_is_runtime_symbol_rj1sp7s(t76);
        if (!t77) goto ie75;
        {
            void * t79 = gm_l0;
            gm_tl874t2atnlh3 t80 = gm_l3;
            gm_b16_8 t81 = gm_l2;
            gm_tnpifv20irnph t82;
            GM_ST(gm_b16_8, ((uint8_t *)&t82 + 0), t81);
            gm_tabb710d9mq2e t83;
            gm_f_format_rb3ud4d((void *)&t83, (void *)NULL, cx, NULL, t82);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t84 = t83;
            if (((((uint8_t *)&t84))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t84) + 8, gm_srccvj6up3gbolp, 297, 23);
            gm_b16_8 t85 = GM_LD(gm_b16_8, (((uint8_t *)&t84) + 8));
            gm_b16_8 t86 = t85;
            gm_b16_8 t87 = GM_LD(gm_b16_8, ((uint8_t *)&t86));
            gm_b0_1 t88 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t79, t80, t87);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_b0_1 t89;
            uint8_t t90 = 0;
            F.ret = t89;
            goto gm_fret;
        }
        goto ix75;
ie75:;
ix75:;
    }
    {
        gm_b16_8 t92 = gm_l2;
        void * t93 = gm_l0;
        gm_b16_8 t94 = GM_LD(gm_b16_8, ((((uint8_t *)t93) + 880) + 0));
        gm_b16_8 t95 = t94;
        uint8_t t96 = 0;
        gm_b16_8 t97 = t92;
        for (uintptr_t i = 0; i < (uintptr_t)(GM_LD(uintptr_t, ((uint8_t *)&t95) + 8)); i++) if (gm_eq0iqiubfm7e(((uint8_t *)GM_LD(void *, ((uint8_t *)&t95))) + i * 16, ((uint8_t *)&t97))) { t96 = 1; break; }
        uint8_t t98 = (uint8_t)(t96);
        if (!t98) goto ie91;
        {
            void * t100 = gm_l0;
            gm_tl874t2atnlh3 t101 = gm_l3;
            gm_b16_8 t102 = gm_l2;
            gm_tnpifv20irnph t103;
            GM_ST(gm_b16_8, ((uint8_t *)&t103 + 0), t102);
            gm_tabb710d9mq2e t104;
            gm_f_format_69lunt5((void *)&t104, (void *)NULL, cx, NULL, t103);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t105 = t104;
            if (((((uint8_t *)&t105))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t105) + 8, gm_srccvj6up3gbolp, 301, 23);
            gm_b16_8 t106 = GM_LD(gm_b16_8, (((uint8_t *)&t105) + 8));
            gm_b16_8 t107 = t106;
            gm_b16_8 t108 = GM_LD(gm_b16_8, ((uint8_t *)&t107));
            gm_b0_1 t109 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t100, t101, t108);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_b0_1 t110;
            uint8_t t111 = 0;
            F.ret = t110;
            goto gm_fret;
        }
        goto ix91;
ie91:;
ix91:;
    }
    {
        uint64_t t113 = GM_LD(uintptr_t, (((uint8_t *)&F.l4) + 80) + 8);
        uint8_t t114 = (t113 > ((uint64_t)0ULL));
        if (!t114) goto ie112;
        {
            void * t116 = gm_l0;
            gm_tl874t2atnlh3 t117 = gm_l3;
            gm_b16_8 t118;
            GM_ST(void *, (uint8_t *)&t118, (void *)(gm_sbaogg9p69n9f));
            GM_ST(uintptr_t, (uint8_t *)&t118 + 8, (uintptr_t)(37));
            gm_b0_1 t119 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t116, t117, t118);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_b0_1 t120;
            uint8_t t121 = 0;
            F.ret = t120;
            goto gm_fret;
        }
        goto ix112;
ie112:;
ix112:;
    }
    {
        void * t123 = gm_l0;
        void * t124 = (void *)(((uint8_t *)&F.l4) + 64);
        uint8_t t125 = gm_f_within_sys_otp5qp6((void *)NULL, cx, NULL, t123, t124);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t126 = (uint8_t)!t125;
        if (!t126) goto ie122;
        {
            void * t128 = gm_l0;
            gm_tl874t2atnlh3 t129 = gm_l3;
            gm_b16_8 t130;
            GM_ST(void *, (uint8_t *)&t130, (void *)(gm_siqa65grpcsk9));
            GM_ST(uintptr_t, (uint8_t *)&t130 + 8, (uintptr_t)(98));
            gm_b0_1 t131 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t128, t129, t130);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix122;
ie122:;
ix122:;
    }
    {
        {
            gm_b16_8 t133 = GM_LD(gm_b16_8, (((uint8_t *)&F.l4) + 40));
            gm_l9 = t133;
        }
        {
            gm_l10 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t135 = gm_l10;
                uint64_t t136 = GM_LD(uintptr_t, ((uint8_t *)&gm_l9) + 8);
                uint8_t t137 = (t135 < t136);
                if (!t137) goto lb134;
                {
                    uint64_t t140 = gm_l10;
                    uint64_t t141 = (uintptr_t)t140;
                    uint64_t t142 = GM_LD(uintptr_t, ((uint8_t *)&gm_l9) + 8);
                    gm_t5v0c8ia6a5mo t145 = GM_LD(gm_t5v0c8ia6a5mo, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l9)) + t141 * 56));
                    F.l11 = t145;
                    {
                        void * t148 = gm_l0;
                        gm_t88rcucg1b7uk t149 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l11) + 16));
                        uint8_t t150 = gm_f_is_c_compatible_2hi3vi8((void *)NULL, cx, NULL, t148, t149, ((uint8_t)0));
                        if (gm_unw.kind) {
                            goto gm_ll138;
                        }
                        uint8_t t151 = (uint8_t)!t150;
                        if (!t151) goto ie147;
                        {
                            void * t153 = gm_l0;
                            gm_tl874t2atnlh3 t154 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l11) + 20));
                            void * t155 = gm_l0;
                            gm_t88rcucg1b7uk t156 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l11) + 16));
                            gm_b16_8 t157 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t155, t156);
                            if (gm_unw.kind) {
                                goto gm_ll138;
                            }
                            gm_tnpifv20irnph t158;
                            GM_ST(gm_b16_8, ((uint8_t *)&t158 + 0), t157);
                            gm_tabb710d9mq2e t159;
                            gm_f_format_fea89f6((void *)&t159, (void *)NULL, cx, NULL, t158);
                            if (gm_unw.kind) {
                                goto gm_ll138;
                            }
                            gm_tabb710d9mq2e t160 = t159;
                            if (((((uint8_t *)&t160))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t160) + 8, gm_srccvj6up3gbolp, 312, 65);
                            gm_b16_8 t161 = GM_LD(gm_b16_8, (((uint8_t *)&t160) + 8));
                            gm_b16_8 t162 = t161;
                            gm_b16_8 t163 = GM_LD(gm_b16_8, ((uint8_t *)&t162));
                            gm_b0_1 t164 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t153, t154, t163);
                            if (gm_unw.kind) {
                                goto gm_ll138;
                            }
                        }
                        goto ix147;
ie147:;
ix147:;
                    }
                    goto me143;
mn144:;
                    gm_trap("no match arm matched the scrutinee", gm_srccvj6up3gbolp, 311, 5);
me143:;
                }
lc134:;
                {
                    uint64_t t166 = gm_l10;
                    uint64_t r167; bool o168 = __builtin_add_overflow(t166, ((uint64_t)1ULL), &r167);
                    uint64_t t169 = r167;
                    gm_l10 = t169;
                }
            }
gm_ll138:;
            if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb134; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc134; }
            }
            goto gm_lad;
lb134:;
        }
    }
    {
        void * t171 = gm_l0;
        void * t172 = (void *)(((uint8_t *)t171) + 8);
        gm_t88rcucg1b7uk t173 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l4) + 56));
        uint8_t t174 = gm_f_is_unit_n83ttnb(t172, GM_CONV(gm_csguankko4lmhd, t173));
        uint8_t t175 = (uint8_t)!t174;
        if (!t175) goto ie170;
        void * t176 = gm_l0;
        gm_t88rcucg1b7uk t177 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l4) + 56));
        uint8_t t178 = gm_f_is_c_compatible_2hi3vi8((void *)NULL, cx, NULL, t176, t177, ((uint8_t)0));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t179 = (uint8_t)!t178;
        if (!t179) goto ie170;
        {
            void * t181 = gm_l0;
            gm_tl874t2atnlh3 t182 = gm_l3;
            void * t183 = gm_l0;
            gm_t88rcucg1b7uk t184 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l4) + 56));
            gm_b16_8 t185 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t183, t184);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tnpifv20irnph t186;
            GM_ST(gm_b16_8, ((uint8_t *)&t186 + 0), t185);
            gm_tabb710d9mq2e t187;
            gm_f_format_mprr1lp((void *)&t187, (void *)NULL, cx, NULL, t186);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t188 = t187;
            if (((((uint8_t *)&t188))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t188) + 8, gm_srccvj6up3gbolp, 315, 23);
            gm_b16_8 t189 = GM_LD(gm_b16_8, (((uint8_t *)&t188) + 8));
            gm_b16_8 t190 = t189;
            gm_b16_8 t191 = GM_LD(gm_b16_8, ((uint8_t *)&t190));
            gm_b0_1 t192 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t181, t182, t191);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix170;
ie170:;
ix170:;
    }
    {
        void * t193 = gm_l0;
        void * t194 = (void *)(((uint8_t *)t193) + 880);
        gm_b16_8 t195 = gm_l2;
        gm_tftb1odc739t5 t196 = gm_f_push_3ef746b(t194, t195);
        gm_tftb1odc739t5 t197 = t196;
        if (((((uint8_t *)&t197))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t197) + 1, gm_srccvj6up3gbolp, 317, 5);
        gm_b0_1 t198 = GM_LD(gm_b0_1, (((uint8_t *)&t197) + 1));
    }
    {
        void * t199 = gm_l0;
        void * t200 = (void *)(((uint8_t *)t199) + 920);
        uint32_t t201 = gm_l1;
        gm_tftb1odc739t5 t202 = gm_f_push_im9d4ro(t200, t201);
        gm_tftb1odc739t5 t203 = t202;
        if (((((uint8_t *)&t203))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t203) + 1, gm_srccvj6up3gbolp, 318, 5);
        gm_b0_1 t204 = GM_LD(gm_b0_1, (((uint8_t *)&t203) + 1));
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
inline __attribute__((aligned(4))) void gm_f_foreign_info_e0kd6uc(void *gm_out, void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_foreign_info_e0kd6uc F;
    void * gm_l0; /* s */
    uint32_t gm_l1; /* f */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    GM_STACK_CHECK_SHALLOW(&F, 238u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        {
            void * t3 = gm_l0;
            gm_b16_8 t4 = GM_LD(gm_b16_8, ((((uint8_t *)t3) + 1408) + 0));
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
                    gm_thognv9odov03 t16 = GM_LD(gm_thognv9odov03, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t12 * 40));
                    F.l4 = t16;
                    {
                        uint32_t t19 = GM_LD(uint32_t, (((uint8_t *)&F.l4) + 0));
                        uint32_t t20 = gm_l1;
                        uint8_t t21 = ((t19 == t20));
                        if (!t21) goto ie18;
                        {
                            memmove(gm_out, ((uint8_t *)&F.l4), 40);
                            goto gm_fdone;
                        }
                        goto ix18;
ie18:;
ix18:;
                    }
                    goto me14;
mn15:;
                    gm_trap("no match arm matched the scrutinee", gm_srccvj6up3gbolp, 169, 5);
me14:;
                }
lc5:;
                {
                    uint64_t t24 = gm_l3;
                    uint64_t r25; bool o26 = __builtin_add_overflow(t24, ((uint64_t)1ULL), &r25);
                    uint64_t t27 = r25;
                    gm_l3 = t27;
                }
            }
lb5:;
        }
    }
    {
        memset((uint8_t *)&(*(gm_thognv9odov03 *)gm_out) + 4, 0, 4);
        uint32_t t28 = gm_l1;
        gm_b16_8 t29;
        GM_ST(void *, (uint8_t *)&t29, (void *)(gm_s2gnpj7k4l22n));
        GM_ST(uintptr_t, (uint8_t *)&t29 + 8, (uintptr_t)(1));
        uint32_t t30 = gm_l1;
        uint64_t t31 = (uint64_t)t30;
        uint64_t t32 = (uintptr_t)t31;
        void * t33 = gm_l0;
        uint64_t t34 = GM_LD(uintptr_t, ((((uint8_t *)t33) + 344) + 0) + 8);
        if (t32 >= (uintptr_t)(t34)) gm_trap_index(t32, t34, gm_srccvj6up3gbolp, 172, 53);
        gm_b16_8 t35 = GM_LD(gm_b16_8, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t33) + 344) + 0)) + t32 * 136) + 0));
        GM_ST(uint32_t, (uint8_t *)&(*(gm_thognv9odov03 *)gm_out) + 0, t28);
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_thognv9odov03 *)gm_out) + 8, t29);
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_thognv9odov03 *)gm_out) + 24, t35);
        goto gm_fdone;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_thognv9odov03 gm_f_foreign_info_e0kd6uc_c(void * a0, uint32_t a1) { gm_thognv9odov03 gm_r; gm_f_foreign_info_e0kd6uc(&gm_r, a0, a1); return gm_r; }
__attribute__((aligned(4))) uint8_t gm_f_is_c_compatible_2hi3vi8(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, uint8_t a2) {
    struct gm_fr_is_c_compatible_2hi3vi8 F;
    void * gm_l0; /* s */
    gm_t88rcucg1b7uk gm_l1; /* t */
    uint8_t gm_l2; /* in_struct */
    uint16_t gm_l3; /* bits */
    gm_t88rcucg1b7uk gm_l4; /* inner */
    gm_b16_8 gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    gm_t88rcucg1b7uk gm_l8; /* p */
    gm_t88rcucg1b7uk gm_l9; /* elem */
    gm_tnnjeerolos0a gm_l10; /* kind */
    uint32_t gm_l11; /* id */
    gm_tjpqvfd72o62u gm_l12; /* info */
    gm_b16_8 gm_l14; /* fs */
    gm_b16_8 gm_l15; /* <tmp> */
    uint64_t gm_l16; /* <tmp> */
    gm_b16_8 gm_l18; /* fields */
    gm_b16_8 gm_l19; /* <tmp> */
    uint64_t gm_l20; /* <tmp> */
    GM_STACK_CHECK(&F, 1206u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        void * t3 = (void *)(((uint8_t *)t2) + 8);
        gm_t88rcucg1b7uk t4 = gm_l1;
        gm_t53at80pfdus3 t5;
        gm_f_kind_jcnt4hm((void *)&t5, t3, GM_CONV(gm_csguankko4lmhd, t4));
        gm_t53at80pfdus3 t6 = t5;
        uint8_t t16 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
        switch (t16) { case ((uint8_t)0ULL): goto ma8; case ((uint8_t)1ULL): goto ma9; case ((uint8_t)2ULL): goto ma9; case ((uint8_t)3ULL): goto ma9; case ((uint8_t)4ULL): goto ma9; case ((uint8_t)5ULL): goto ma14; case ((uint8_t)6ULL): goto ma14; case ((uint8_t)7ULL): goto ma9; case ((uint8_t)8ULL): goto ma9; case ((uint8_t)9ULL): goto ma9; case ((uint8_t)10ULL): goto ma14; case ((uint8_t)11ULL): goto ma12; case ((uint8_t)12ULL): goto ma14; case ((uint8_t)13ULL): goto ma10; case ((uint8_t)14ULL): goto ma14; case ((uint8_t)15ULL): goto ma14; case ((uint8_t)16ULL): goto ma11; case ((uint8_t)17ULL): goto ma13; case ((uint8_t)18ULL): goto ma14; case ((uint8_t)19ULL): goto ma14; case ((uint8_t)20ULL): goto ma14; case ((uint8_t)21ULL): goto ma14; case ((uint8_t)22ULL): goto ma14; case ((uint8_t)23ULL): goto ma14; case ((uint8_t)24ULL): goto ma14; case ((uint8_t)25ULL): goto ma14; case ((uint8_t)26ULL): goto ma9; default: goto ma14; }
ma8:;
        uint8_t t18 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
        if (t18 != ((uint8_t)0ULL)) goto mn17;
        uint16_t t19 = GM_LD(uint16_t, ((((uint8_t *)&t6) + 8) + 2));
        gm_l3 = t19;
        uint16_t t20 = gm_l3;
        gm_tdbjst0cng0lo t21;
        GM_ST(uint16_t, ((uint8_t *)&t21 + 0), ((uint16_t)8ULL));
        GM_ST(uint16_t, ((uint8_t *)&t21 + 2), ((uint16_t)16ULL));
        GM_ST(uint16_t, ((uint8_t *)&t21 + 4), ((uint16_t)32ULL));
        GM_ST(uint16_t, ((uint8_t *)&t21 + 6), ((uint16_t)64ULL));
        GM_ST(uint16_t, ((uint8_t *)&t21 + 8), ((uint16_t)128ULL));
        gm_tdbjst0cng0lo t22 = t21;
        uint8_t t23 = 0;
        uint16_t t24 = t20;
        if (gm_eq9oqmmiu8qc(((uint8_t *)&t22) + 0, ((uint8_t *)&t24))) t23 = 1;
        if (gm_eq9oqmmiu8qc(((uint8_t *)&t22) + 2, ((uint8_t *)&t24))) t23 = 1;
        if (gm_eq9oqmmiu8qc(((uint8_t *)&t22) + 4, ((uint8_t *)&t24))) t23 = 1;
        if (gm_eq9oqmmiu8qc(((uint8_t *)&t22) + 6, ((uint8_t *)&t24))) t23 = 1;
        if (gm_eq9oqmmiu8qc(((uint8_t *)&t22) + 8, ((uint8_t *)&t24))) t23 = 1;
        uint8_t t25 = (uint8_t)(t23);
        uint8_t t26 = 0;
        F.ret = t25;
        goto gm_fret;
        goto me7;
mn17:;
ma9:;
        uint8_t t30 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
        if (t30 != ((uint8_t)1ULL)) goto pa29;
        goto po28;
pa29:;
        uint8_t t32 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
        if (t32 != ((uint8_t)2ULL)) goto pa31;
        goto po28;
pa31:;
        uint8_t t34 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
        if (t34 != ((uint8_t)3ULL)) goto pa33;
        goto po28;
pa33:;
        uint8_t t36 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
        if (t36 != ((uint8_t)4ULL)) goto pa35;
        goto po28;
pa35:;
        uint8_t t38 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
        if (t38 != ((uint8_t)7ULL)) goto pa37;
        goto po28;
pa37:;
        uint8_t t40 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
        if (t40 != ((uint8_t)8ULL)) goto pa39;
        goto po28;
pa39:;
        uint8_t t42 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
        if (t42 != ((uint8_t)9ULL)) goto pa41;
        goto po28;
pa41:;
        uint8_t t44 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
        if (t44 != ((uint8_t)26ULL)) goto pa43;
        goto po28;
pa43:;
        goto mn27;
po28:;
        uint8_t t45 = 0;
        F.ret = ((uint8_t)1);
        goto gm_fret;
        goto me7;
mn27:;
ma10:;
        uint8_t t47 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
        if (t47 != ((uint8_t)13ULL)) goto mn46;
        gm_t88rcucg1b7uk t48 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t6) + 8));
        gm_l4 = t48;
        uint8_t t49 = 0;
        void * t50 = gm_l0;
        gm_t88rcucg1b7uk t51 = gm_l4;
        uint8_t t52 = gm_f_is_nullable_pointer_nkie06s(t50, GM_CONV(gm_csguankko4lmhd, t51));
        if (t52) {
            void * t53 = gm_l0;
            gm_t88rcucg1b7uk t54 = gm_l4;
            uint8_t t55 = gm_f_is_c_compatible_2hi3vi8((void *)NULL, cx, NULL, t53, t54, ((uint8_t)0));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            t49 = t55;
        } else t49 = 0;
        uint8_t t56 = 0;
        F.ret = t49;
        goto gm_fret;
        goto me7;
mn46:;
ma11:;
        uint8_t t58 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
        if (t58 != ((uint8_t)16ULL)) goto mn57;
        gm_tddtjfggdat0d t59 = GM_LD(gm_tddtjfggdat0d, (((uint8_t *)&t6) + 8));
        F.l5 = t59;
        {
            void * t62 = gm_l0;
            void * t63 = (void *)(((uint8_t *)&F.l5) + 24);
            uint8_t t64 = gm_f_within_sys_otp5qp6((void *)NULL, cx, NULL, t62, t63);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t65 = (uint8_t)!t64;
            if (!t65) goto ie61;
            {
                uint8_t t67 = 0;
                F.ret = ((uint8_t)0);
                goto gm_fret;
            }
            goto ix61;
ie61:;
ix61:;
        }
        {
            {
                gm_b16_8 t69 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 0));
                gm_l6 = t69;
            }
            {
                gm_l7 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t71 = gm_l7;
                    uint64_t t72 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                    uint8_t t73 = (t71 < t72);
                    if (!t73) goto lb70;
                    {
                        uint64_t t76 = gm_l7;
                        uint64_t t77 = (uintptr_t)t76;
                        uint64_t t78 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                        gm_t88rcucg1b7uk t81 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t77 * 4));
                        gm_l8 = t81;
                        {
                            void * t84 = gm_l0;
                            gm_t88rcucg1b7uk t85 = gm_l8;
                            uint8_t t86 = gm_f_is_c_compatible_2hi3vi8((void *)NULL, cx, NULL, t84, t85, ((uint8_t)0));
                            if (gm_unw.kind) {
                                goto gm_ll74;
                            }
                            uint8_t t87 = (uint8_t)!t86;
                            if (!t87) goto ie83;
                            {
                                uint8_t t89 = 0;
                                F.ret = ((uint8_t)0);
                                goto gm_fret;
                            }
                            goto ix83;
ie83:;
ix83:;
                        }
                        goto me79;
mn80:;
                        gm_trap("no match arm matched the scrutinee", gm_srccvj6up3gbolp, 364, 13);
me79:;
                    }
lc70:;
                    {
                        uint64_t t91 = gm_l7;
                        uint64_t r92; bool o93 = __builtin_add_overflow(t91, ((uint64_t)1ULL), &r92);
                        uint64_t t94 = r92;
                        gm_l7 = t94;
                    }
                }
gm_ll74:;
                if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb70; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc70; }
                }
                goto gm_lad;
lb70:;
            }
        }
        {
            uint8_t t95 = 0;
            void * t96 = gm_l0;
            void * t97 = (void *)(((uint8_t *)t96) + 8);
            gm_t88rcucg1b7uk t98 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l5) + 16));
            uint8_t t99 = gm_f_is_unit_n83ttnb(t97, GM_CONV(gm_csguankko4lmhd, t98));
            if (!t99) {
                void * t100 = gm_l0;
                gm_t88rcucg1b7uk t101 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l5) + 16));
                uint8_t t102 = gm_f_is_c_compatible_2hi3vi8((void *)NULL, cx, NULL, t100, t101, ((uint8_t)0));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                t95 = t102;
            } else t95 = 1;
            uint8_t t103 = 0;
            F.ret = t95;
            goto gm_fret;
        }
        goto me7;
mn57:;
ma12:;
        uint8_t t105 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
        if (t105 != ((uint8_t)11ULL)) goto mn104;
        gm_t88rcucg1b7uk t106 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t6) + 8) + 8));
        gm_l9 = t106;
        gm_tnnjeerolos0a t107 = GM_LD(gm_tnnjeerolos0a, ((((uint8_t *)&t6) + 8) + 12));
        gm_l10 = t107;
        uint8_t t108 = 0;
        uint8_t t109 = 0;
        uint8_t t110 = gm_l2;
        if (t110) {
            uint8_t t111 = (!(GM_LD(uint8_t, ((uint8_t *)&gm_l10) + 0) == ((uint8_t)2ULL)));
            t109 = t111;
        } else t109 = 0;
        if (t109) {
            void * t112 = gm_l0;
            gm_t88rcucg1b7uk t113 = gm_l9;
            uint8_t t114 = gm_f_is_c_compatible_2hi3vi8((void *)NULL, cx, NULL, t112, t113, ((uint8_t)1));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            t108 = t114;
        } else t108 = 0;
        uint8_t t115 = 0;
        F.ret = t108;
        goto gm_fret;
        goto me7;
mn104:;
ma13:;
        uint8_t t117 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
        if (t117 != ((uint8_t)17ULL)) goto mn116;
        uint32_t t118 = GM_LD(uint32_t, ((((uint8_t *)&t6) + 8) + 0));
        gm_l11 = t118;
        {
            void * t121 = gm_l0;
            void * t122 = (void *)(((uint8_t *)t121) + 2320);
            uint32_t t123 = gm_l11;
            gm_t8cc1mtbrhlpt t124;
            gm_f_get_nt3g9ko((void *)&t124, t122, t123);
            gm_t8cc1mtbrhlpt t125 = t124;
            if (!((((uint8_t *)&t125))[32] != 0)) goto ie120;
            gm_tjpqvfd72o62u t126 = GM_LD(gm_tjpqvfd72o62u, ((uint8_t *)&t125));
            gm_l12 = t126;
            {
                uint8_t t129;
                if ((((((uint8_t *)&gm_l12) + 8))[8] != 0)) goto mn131;
                t129 = ((uint8_t)1);
                goto me130;
mn131:;
                t129 = ((uint8_t)0);
                goto me130;
mn132:;
                gm_trap("no match arm matched the scrutinee", gm_srccvj6up3gbolp, 373, 20);
me130:;
                if (!t129) goto ie128;
                {
                    uint8_t t134 = 0;
                    F.ret = ((uint8_t)0);
                    goto gm_fret;
                }
                goto ix128;
ie128:;
ix128:;
            }
            goto ix120;
ie120:;
ix120:;
        }
        {
            uint32_t t135 = gm_l11;
            uint64_t t136 = (uint64_t)t135;
            uint64_t t137 = (uintptr_t)t136;
            void * t138 = gm_l0;
            uint64_t t139 = GM_LD(uintptr_t, ((((uint8_t *)t138) + 1088) + 0) + 8);
            if (t137 >= (uintptr_t)(t139)) gm_trap_index(t137, t139, gm_srccvj6up3gbolp, 375, 19);
            uint8_t t145 = GM_LD(uint8_t, ((((uint8_t *)GM_LD(void *, ((((uint8_t *)t138) + 1088) + 0)) + t137 * 184) + 24) + 0));
            switch (t145) { case ((uint8_t)0ULL): goto ma141; case ((uint8_t)1ULL): goto ma143; case ((uint8_t)2ULL): goto ma142; default: goto ma143; }
ma141:;
            uint8_t t147 = GM_LD(uint8_t, ((((uint8_t *)GM_LD(void *, ((((uint8_t *)t138) + 1088) + 0)) + t137 * 184) + 24) + 0));
            if (t147 != ((uint8_t)0ULL)) goto mn146;
            gm_tpautr4br7h3l t148 = GM_LD(gm_tpautr4br7h3l, ((((uint8_t *)GM_LD(void *, ((((uint8_t *)t138) + 1088) + 0)) + t137 * 184) + 24) + 8));
            F.l13 = t148;
            {
                uint8_t t151;
                if ((((((uint8_t *)&F.l13) + 40))[4] != 0)) goto mn153;
                t151 = ((uint8_t)0);
                goto me152;
mn153:;
                t151 = ((uint8_t)1);
                goto me152;
mn154:;
                gm_trap("no match arm matched the scrutinee", gm_srccvj6up3gbolp, 377, 24);
me152:;
                if (!t151) goto ie150;
                {
                    uint8_t t156 = 0;
                    F.ret = ((uint8_t)1);
                    goto gm_fret;
                }
                goto ix150;
ie150:;
ix150:;
            }
            {
                void * t157 = gm_l0;
                gm_t88rcucg1b7uk t158 = gm_l1;
                gm_th2lqbo77pvsc t159;
                gm_f_struct_fields_chqu3it((void *)&t159, t157, GM_CONV(gm_csguankko4lmhd, t158));
                gm_th2lqbo77pvsc t160 = t159;
                gm_b16_8 t161;
                if (((((uint8_t *)&t160))[16] != 0)) {
                    gm_b16_8 t162 = GM_LD(gm_b16_8, (((uint8_t *)&t160) + 0));
                    t161 = t162;
                } else {
                    uint8_t t163 = 0;
                    F.ret = ((uint8_t)0);
                    goto gm_fret;
                }
                gm_l14 = t161;
            }
            {
                {
                    gm_b16_8 t165 = gm_l14;
                    gm_l15 = t165;
                }
                {
                    gm_l16 = ((uint64_t)0ULL);
                }
                {
                    for (;;) {
                        uint64_t t167 = gm_l16;
                        uint64_t t168 = GM_LD(uintptr_t, ((uint8_t *)&gm_l15) + 8);
                        uint8_t t169 = (t167 < t168);
                        if (!t169) goto lb166;
                        {
                            uint64_t t172 = gm_l16;
                            uint64_t t173 = (uintptr_t)t172;
                            uint64_t t174 = GM_LD(uintptr_t, ((uint8_t *)&gm_l15) + 8);
                            gm_t0mglksrkfa5e t177 = GM_LD(gm_t0mglksrkfa5e, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l15)) + t173 * 40));
                            F.l17 = t177;
                            {
                                void * t180 = gm_l0;
                                gm_t88rcucg1b7uk t181 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l17) + 16));
                                uint8_t t182 = gm_f_is_c_compatible_2hi3vi8((void *)NULL, cx, NULL, t180, t181, ((uint8_t)1));
                                if (gm_unw.kind) {
                                    goto gm_ll170;
                                }
                                uint8_t t183 = (uint8_t)!t182;
                                if (!t183) goto ie179;
                                {
                                    uint8_t t185 = 0;
                                    F.ret = ((uint8_t)0);
                                    goto gm_fret;
                                }
                                goto ix179;
ie179:;
ix179:;
                            }
                            goto me175;
mn176:;
                            gm_trap("no match arm matched the scrutinee", gm_srccvj6up3gbolp, 379, 21);
me175:;
                        }
lc166:;
                        {
                            uint64_t t187 = gm_l16;
                            uint64_t r188; bool o189 = __builtin_add_overflow(t187, ((uint64_t)1ULL), &r188);
                            uint64_t t190 = r188;
                            gm_l16 = t190;
                        }
                    }
gm_ll170:;
                    if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb166; }
                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc166; }
                    }
                    goto gm_lad;
lb166:;
                }
            }
            {
                uint8_t t191 = 0;
                F.ret = ((uint8_t)1);
                goto gm_fret;
            }
            goto me140;
mn146:;
ma142:;
            uint8_t t193 = GM_LD(uint8_t, ((((uint8_t *)GM_LD(void *, ((((uint8_t *)t138) + 1088) + 0)) + t137 * 184) + 24) + 0));
            if (t193 != ((uint8_t)2ULL)) goto mn192;
            gm_b16_8 t194 = GM_LD(gm_b16_8, ((((uint8_t *)GM_LD(void *, ((((uint8_t *)t138) + 1088) + 0)) + t137 * 184) + 24) + 8));
            gm_l18 = t194;
            {
                {
                    gm_b16_8 t197 = gm_l18;
                    gm_l19 = t197;
                }
                {
                    gm_l20 = ((uint64_t)0ULL);
                }
                {
                    for (;;) {
                        uint64_t t199 = gm_l20;
                        uint64_t t200 = GM_LD(uintptr_t, ((uint8_t *)&gm_l19) + 8);
                        uint8_t t201 = (t199 < t200);
                        if (!t201) goto lb198;
                        {
                            uint64_t t204 = gm_l20;
                            uint64_t t205 = (uintptr_t)t204;
                            uint64_t t206 = GM_LD(uintptr_t, ((uint8_t *)&gm_l19) + 8);
                            gm_t0mglksrkfa5e t209 = GM_LD(gm_t0mglksrkfa5e, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l19)) + t205 * 40));
                            F.l21 = t209;
                            {
                                void * t212 = gm_l0;
                                gm_t88rcucg1b7uk t213 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l21) + 16));
                                uint8_t t214 = gm_f_is_c_compatible_2hi3vi8((void *)NULL, cx, NULL, t212, t213, ((uint8_t)1));
                                if (gm_unw.kind) {
                                    goto gm_ll202;
                                }
                                uint8_t t215 = (uint8_t)!t214;
                                if (!t215) goto ie211;
                                {
                                    uint8_t t217 = 0;
                                    F.ret = ((uint8_t)0);
                                    goto gm_fret;
                                }
                                goto ix211;
ie211:;
ix211:;
                            }
                            goto me207;
mn208:;
                            gm_trap("no match arm matched the scrutinee", gm_srccvj6up3gbolp, 385, 21);
me207:;
                        }
lc198:;
                        {
                            uint64_t t219 = gm_l20;
                            uint64_t r220; bool o221 = __builtin_add_overflow(t219, ((uint64_t)1ULL), &r220);
                            uint64_t t222 = r220;
                            gm_l20 = t222;
                        }
                    }
gm_ll202:;
                    if (gm_unw.frame == &F.h && gm_unw.label == 2) {
                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb198; }
                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc198; }
                    }
                    goto gm_lad;
lb198:;
                }
            }
            {
                uint8_t t223 = 0;
                F.ret = ((uint8_t)1);
                goto gm_fret;
            }
            goto me140;
mn192:;
ma143:;
            uint8_t t225 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
            goto me140;
mn224:;
ma144:;
            gm_trap("no match arm matched the scrutinee", gm_srccvj6up3gbolp, 375, 13);
me140:;
        }
        goto me7;
mn116:;
ma14:;
        uint8_t t227 = 0;
        F.ret = ((uint8_t)0);
        goto gm_fret;
        goto me7;
mn226:;
ma15:;
        gm_trap("no match arm matched the scrutinee", gm_srccvj6up3gbolp, 358, 5);
me7:;
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
inline __attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_source_location_ty_slllard(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_source_location_ty_slllard F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* prelude */
    uint64_t gm_l2; /* d */
    gm_t88rcucg1b7uk gm_l3; /* t */
    GM_STACK_CHECK(&F, 673u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 0));
        gm_tf5srh4mp4b3i t4 = GM_LD(gm_tf5srh4mp4b3i, (((uint8_t *)t3) + 320));
        gm_tf5srh4mp4b3i t5 = t4;
        uint64_t t6;
        if (((((uint8_t *)&t5))[8] != 0)) {
            uint64_t t7 = GM_LD(uint64_t, (((uint8_t *)&t5) + 0));
            t6 = t7;
        } else {
            gm_tkn42u7na3t81 t8;
            memset(&t8, 0, sizeof t8);
            uint8_t t9 = 0;
            F.ret = t8;
            goto gm_fret;
        }
        gm_l1 = t6;
    }
    {
        void * t10 = gm_l0;
        void * t11 = GM_LD(void *, (((uint8_t *)t10) + 0));
        uint64_t t12 = gm_l1;
        gm_b16_8 t13;
        GM_ST(void *, (uint8_t *)&t13, (void *)(gm_sa291memmphhk));
        GM_ST(uintptr_t, (uint8_t *)&t13 + 8, (uintptr_t)(14));
        gm_tf5srh4mp4b3i t14 = gm_f_lookup_59juqoo(t11, t12, t13);
        gm_tf5srh4mp4b3i t15 = t14;
        uint64_t t16;
        if (((((uint8_t *)&t15))[8] != 0)) {
            uint64_t t17 = GM_LD(uint64_t, (((uint8_t *)&t15) + 0));
            t16 = t17;
        } else {
            gm_tkn42u7na3t81 t18;
            memset(&t18, 0, sizeof t18);
            uint8_t t19 = 0;
            F.ret = t18;
            goto gm_fret;
        }
        gm_l2 = t16;
    }
    {
        void * t21 = gm_l0;
        uint64_t t22 = gm_l2;
        gm_t6qooom8qa2do t23;
        gm_f_resolve_decl_pus042v((void *)&t23, (void *)NULL, cx, NULL, t21, t22);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t6qooom8qa2do t24 = t23;
        uint8_t t25 = GM_LD(uint8_t, (((uint8_t *)&t24) + 0));
        if (t25 != ((uint8_t)0ULL)) goto ie20;
        gm_t88rcucg1b7uk t26 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t24) + 8));
        gm_l3 = t26;
        {
            gm_t88rcucg1b7uk t28 = gm_l3;
            gm_tkn42u7na3t81 t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 6, 0);
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t29), t28);
            ((uint8_t *)&t29)[4] = 1;
            uint8_t t30 = 0;
            F.ret = t29;
            goto gm_fret;
        }
        goto ix20;
ie20:;
        {
            gm_tkn42u7na3t81 t32;
            memset(&t32, 0, sizeof t32);
            uint8_t t33 = 0;
            F.ret = t32;
            goto gm_fret;
        }
ix20:;
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
inline __attribute__((aligned(4))) gm_t9805qtonbsag gm_f_sys_effect_97o96a1(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_sys_effect_97o96a1 F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* prelude */
    uint64_t gm_l2; /* d */
    uint32_t gm_l3; /* e */
    GM_STACK_CHECK(&F, 673u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 0));
        gm_tf5srh4mp4b3i t4 = GM_LD(gm_tf5srh4mp4b3i, (((uint8_t *)t3) + 320));
        gm_tf5srh4mp4b3i t5 = t4;
        uint64_t t6;
        if (((((uint8_t *)&t5))[8] != 0)) {
            uint64_t t7 = GM_LD(uint64_t, (((uint8_t *)&t5) + 0));
            t6 = t7;
        } else {
            gm_t9805qtonbsag t8;
            memset(&t8, 0, sizeof t8);
            uint8_t t9 = 0;
            F.ret = t8;
            goto gm_fret;
        }
        gm_l1 = t6;
    }
    {
        void * t10 = gm_l0;
        void * t11 = GM_LD(void *, (((uint8_t *)t10) + 0));
        uint64_t t12 = gm_l1;
        gm_b16_8 t13;
        GM_ST(void *, (uint8_t *)&t13, (void *)(gm_sa902c1tbtg0i));
        GM_ST(uintptr_t, (uint8_t *)&t13 + 8, (uintptr_t)(3));
        gm_tf5srh4mp4b3i t14 = gm_f_lookup_59juqoo(t11, t12, t13);
        gm_tf5srh4mp4b3i t15 = t14;
        uint64_t t16;
        if (((((uint8_t *)&t15))[8] != 0)) {
            uint64_t t17 = GM_LD(uint64_t, (((uint8_t *)&t15) + 0));
            t16 = t17;
        } else {
            gm_t9805qtonbsag t18;
            memset(&t18, 0, sizeof t18);
            uint8_t t19 = 0;
            F.ret = t18;
            goto gm_fret;
        }
        gm_l2 = t16;
    }
    {
        void * t21 = gm_l0;
        uint64_t t22 = gm_l2;
        gm_t6qooom8qa2do t23;
        gm_f_resolve_decl_pus042v((void *)&t23, (void *)NULL, cx, NULL, t21, t22);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t6qooom8qa2do t24 = t23;
        uint8_t t25 = GM_LD(uint8_t, (((uint8_t *)&t24) + 0));
        if (t25 != ((uint8_t)2ULL)) goto ie20;
        uint32_t t26 = GM_LD(uint32_t, (((uint8_t *)&t24) + 8));
        gm_l3 = t26;
        {
            uint32_t t28 = gm_l3;
            gm_t9805qtonbsag t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 6, 0);
            GM_ST(uint32_t, ((uint8_t *)&t29), t28);
            ((uint8_t *)&t29)[4] = 1;
            uint8_t t30 = 0;
            F.ret = t29;
            goto gm_fret;
        }
        goto ix20;
ie20:;
        {
            gm_t9805qtonbsag t32;
            memset(&t32, 0, sizeof t32);
            uint8_t t33 = 0;
            F.ret = t32;
            goto gm_fret;
        }
ix20:;
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
inline __attribute__((aligned(4))) gm_tnpifv20irnph gm_f_sys_set_pqe8mto(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_sys_set_pqe8mto F;
    void * gm_l0; /* s */
    uint32_t gm_l1; /* sys */
    GM_STACK_CHECK(&F, 246u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        gm_t9805qtonbsag t3 = gm_f_sys_effect_97o96a1((void *)NULL, cx, NULL, t2);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t9805qtonbsag t4 = t3;
        uint32_t t5;
        if (((((uint8_t *)&t4))[4] != 0)) {
            uint32_t t6 = GM_LD(uint32_t, (((uint8_t *)&t4) + 0));
            t5 = t6;
        } else {
            gm_tnpifv20irnph t7 = gm_f_empty_effects_r0iv3pv();
            uint8_t t8 = 0;
            F.ret = t7;
            goto gm_fret;
        }
        gm_l1 = t5;
    }
    {
        gm_f_list_084fcd6((void *)&F.l2, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t9 = (void *)((uint8_t *)&F.l2);
        gm_tiilmfd16v3fc t10;
        memset((uint8_t *)&t10 + 4, 0, 4);
        uint32_t t11 = gm_l1;
        GM_ST(uint32_t, (uint8_t *)&t10 + 0, t11);
        gm_b16_8 t12 = gm_f_empty_slice_fh7svmj();
        gm_b16_8 t13 = t12;
        gm_b16_8 t14 = GM_LD(gm_b16_8, ((uint8_t *)&t13));
        GM_ST(gm_b16_8, (uint8_t *)&t10 + 8, t14);
        gm_tftb1odc739t5 t15 = gm_f_push_d1r4h19(t9, &t10);
        gm_tftb1odc739t5 t16 = t15;
        if (((((uint8_t *)&t16))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t16) + 1, gm_srccvj6up3gbolp, 11, 5);
        gm_b0_1 t17 = GM_LD(gm_b0_1, (((uint8_t *)&t16) + 1));
    }
    {
        gm_b16_8 t18 = GM_LD(gm_b16_8, (((uint8_t *)&F.l2) + 0));
        gm_b16_8 t19 = t18;
        gm_b16_8 t20 = GM_LD(gm_b16_8, ((uint8_t *)&t19));
        gm_tnpifv20irnph t21 = gm_f_effect_set_vtcpipr((void *)NULL, cx, NULL, t20);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t22 = 0;
        F.ret = t21;
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
__attribute__((aligned(4))) gm_b0_1 gm_f_trap_hook_fn_5c0a4ds(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_tl874t2atnlh3 a2) {
    struct gm_fr_trap_hook_fn_5c0a4ds F;
    void * gm_l0; /* s */
    uint32_t gm_l1; /* f */
    gm_tl874t2atnlh3 gm_l2; /* span */
    gm_tkn42u7na3t81 gm_l4; /* loc */
    uint8_t gm_l5; /* params_ok */
    gm_t88rcucg1b7uk gm_l6; /* l */
    uint32_t gm_l7; /* h */
    GM_STACK_CHECK(&F, 660u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t3 = gm_l0;
        void * t4 = GM_LD(void *, (((uint8_t *)t3) + 0));
        uint8_t t5 = (!((GM_LD(uintptr_t, ((((uint8_t *)t4) + 88) + 24) + 8) == 12) && memcmp(GM_LD(void *, ((((uint8_t *)t4) + 88) + 24)), gm_soo4rtckloi4d, 12) == 0));
        if (!t5) goto ie2;
        {
            void * t7 = gm_l0;
            gm_tl874t2atnlh3 t8 = gm_l2;
            gm_b16_8 t9;
            GM_ST(void *, (uint8_t *)&t9, (void *)(gm_s5nig6ebl26qo));
            GM_ST(uintptr_t, (uint8_t *)&t9 + 8, (uintptr_t)(60));
            gm_b0_1 t10 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t7, t8, t9);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_b0_1 t11;
            uint8_t t12 = 0;
            F.ret = t11;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        uint32_t t13 = gm_l1;
        uint64_t t14 = (uint64_t)t13;
        uint64_t t15 = (uintptr_t)t14;
        void * t16 = gm_l0;
        uint64_t t17 = GM_LD(uintptr_t, ((((uint8_t *)t16) + 344) + 0) + 8);
        if (t15 >= (uintptr_t)(t17)) gm_trap_index(t15, t17, gm_srccvj6up3gbolp, 328, 12);
        gm_tqqhoivahirr0 t18 = GM_LD(gm_tqqhoivahirr0, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t16) + 344) + 0)) + t15 * 136));
        F.l3 = t18;
    }
    {
        void * t19 = gm_l0;
        gm_tkn42u7na3t81 t20 = gm_f_source_location_ty_slllard((void *)NULL, cx, NULL, t19);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l4 = t20;
    }
    {
        uint8_t t21 = 0;
        uint8_t t22 = 0;
        uint64_t t23 = GM_LD(uintptr_t, (((uint8_t *)&F.l3) + 40) + 8);
        uint8_t t24 = ((t23 == ((uint64_t)2ULL)));
        if (t24) {
            uint64_t t25 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t26 = GM_LD(uintptr_t, (((uint8_t *)&F.l3) + 40) + 8);
            if (t25 >= (uintptr_t)(t26)) gm_trap_index(t25, t26, gm_srccvj6up3gbolp, 330, 42);
            gm_t88rcucg1b7uk t27 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l3) + 40)) + t25 * 56) + 16));
            void * t28 = gm_l0;
            void * t29 = (void *)(((uint8_t *)t28) + 8);
            gm_t88rcucg1b7uk t30 = gm_f_string_ty_j74ff0o((void *)NULL, cx, NULL, t29);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t88rcucg1b7uk t31 = t27;
            gm_t88rcucg1b7uk t32 = t30;
            uint8_t t33 = (gm_eq9m6mvtmgde(((uint8_t *)&t31), ((uint8_t *)&t32)));
            t22 = t33;
        } else t22 = 0;
        if (t22) {
            uint64_t t34 = GM_LD(uintptr_t, (((uint8_t *)&F.l3) + 80) + 8);
            uint8_t t35 = ((t34 == ((uint64_t)0ULL)));
            t21 = t35;
        } else t21 = 0;
        gm_l5 = t21;
    }
    {
        uint8_t t37 = gm_l5;
        if (!t37) goto ie36;
        {
            gm_l5 = ((uint8_t)0);
        }
        {
            if (!((((uint8_t *)&gm_l4))[4] != 0)) goto ie39;
            gm_t88rcucg1b7uk t40 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l4));
            gm_l6 = t40;
            {
                uint64_t t42 = (uintptr_t)((uint64_t)1ULL);
                uint64_t t43 = GM_LD(uintptr_t, (((uint8_t *)&F.l3) + 40) + 8);
                if (t42 >= (uintptr_t)(t43)) gm_trap_index(t42, t43, gm_srccvj6up3gbolp, 333, 35);
                gm_t88rcucg1b7uk t44 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l3) + 40)) + t42 * 56) + 16));
                gm_t88rcucg1b7uk t45 = gm_l6;
                gm_t88rcucg1b7uk t46 = t44;
                gm_t88rcucg1b7uk t47 = t45;
                uint8_t t48 = (gm_eq9m6mvtmgde(((uint8_t *)&t46), ((uint8_t *)&t47)));
                gm_l5 = t48;
            }
            goto ix39;
ie39:;
ix39:;
        }
        goto ix36;
ie36:;
ix36:;
    }
    {
        uint8_t t50 = 0;
        uint8_t t51 = 0;
        uint8_t t52 = gm_l5;
        uint8_t t53 = (uint8_t)!t52;
        if (!t53) {
            void * t54 = gm_l0;
            void * t55 = (void *)(((uint8_t *)t54) + 8);
            gm_t88rcucg1b7uk t56 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l3) + 56));
            gm_t53at80pfdus3 t57;
            gm_f_kind_jcnt4hm((void *)&t57, t55, GM_CONV(gm_csguankko4lmhd, t56));
            gm_t53at80pfdus3 t58 = t57;
            uint8_t t59 = (!(GM_LD(uint8_t, ((uint8_t *)&t58) + 0) == ((uint8_t)5ULL)));
            t51 = t59;
        } else t51 = 1;
        if (!t51) {
            void * t60 = gm_l0;
            void * t61 = (void *)(((uint8_t *)&F.l3) + 64);
            uint8_t t62 = gm_f_within_sys_otp5qp6((void *)NULL, cx, NULL, t60, t61);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t63 = (uint8_t)!t62;
            t50 = t63;
        } else t50 = 1;
        if (!t50) goto ie49;
        {
            void * t65 = gm_l0;
            gm_tl874t2atnlh3 t66 = gm_l2;
            gm_b16_8 t67;
            GM_ST(void *, (uint8_t *)&t67, (void *)(gm_s4nl1jq95dg65));
            GM_ST(uintptr_t, (uint8_t *)&t67 + 8, (uintptr_t)(110));
            gm_b0_1 t68 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t65, t66, t67);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_b0_1 t69;
            uint8_t t70 = 0;
            F.ret = t69;
            goto gm_fret;
        }
        goto ix49;
ie49:;
ix49:;
    }
    {
        void * t72 = gm_l0;
        if (!(((((uint8_t *)t72) + 960))[4] != 0)) goto ie71;
        uint32_t t73 = GM_LD(uint32_t, (((uint8_t *)t72) + 960));
        gm_l7 = t73;
        {
            uint32_t t76 = gm_l7;
            uint32_t t77 = gm_l1;
            uint8_t t78 = (!(t76 == t77));
            if (!t78) goto ie75;
            {
                void * t80 = gm_l0;
                gm_tl874t2atnlh3 t81 = gm_l2;
                gm_b16_8 t82;
                GM_ST(void *, (uint8_t *)&t82, (void *)(gm_scj8qp34gs3m8));
                GM_ST(uintptr_t, (uint8_t *)&t82 + 8, (uintptr_t)(57));
                gm_b0_1 t83 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t80, t81, t82);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                gm_b0_1 t84;
                uint8_t t85 = 0;
                F.ret = t84;
                goto gm_fret;
            }
            goto ix75;
ie75:;
ix75:;
        }
        goto ix71;
ie71:;
ix71:;
    }
    {
        void * t86 = gm_l0;
        uint32_t t87 = gm_l1;
        gm_t9805qtonbsag t88;
        GM_ST(uint8_t, (uint8_t *)&t88 + 5, 0);
        GM_ST(uint16_t, (uint8_t *)&t88 + 6, 0);
        GM_ST(uint32_t, ((uint8_t *)&t88), t87);
        ((uint8_t *)&t88)[4] = 1;
        GM_ST(gm_t9805qtonbsag, (((uint8_t *)t86) + 960), t88);
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
extern __attribute__((aligned(4))) void gm_f_unsupported_value_sufn4da(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) uint8_t gm_f_within_sys_otp5qp6(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_within_sys_otp5qp6 F;
    void * gm_l0; /* s */
    void * gm_l1; /* set */
    gm_t9805qtonbsag gm_l2; /* sys */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    gm_tiilmfd16v3fc gm_l5; /* e */
    GM_STACK_CHECK(&F, 234u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        gm_t9805qtonbsag t3 = gm_f_sys_effect_97o96a1((void *)NULL, cx, NULL, t2);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l2 = t3;
    }
    {
        {
            void * t5 = gm_l1;
            gm_b16_8 t6 = GM_LD(gm_b16_8, (((uint8_t *)t5) + 0));
            gm_l3 = t6;
        }
        {
            gm_l4 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t8 = gm_l4;
                uint64_t t9 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                uint8_t t10 = (t8 < t9);
                if (!t10) goto lb7;
                {
                    uint64_t t13 = gm_l4;
                    uint64_t t14 = (uintptr_t)t13;
                    uint64_t t15 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                    gm_tiilmfd16v3fc t18 = GM_LD(gm_tiilmfd16v3fc, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t14 * 24));
                    gm_l5 = t18;
                    {
                        gm_t9805qtonbsag t21 = gm_l2;
                        uint32_t t22 = GM_LD(uint32_t, (((uint8_t *)&gm_l5) + 0));
                        gm_t9805qtonbsag t23;
                        GM_ST(uint8_t, (uint8_t *)&t23 + 5, 0);
                        GM_ST(uint16_t, (uint8_t *)&t23 + 6, 0);
                        GM_ST(uint32_t, ((uint8_t *)&t23), t22);
                        ((uint8_t *)&t23)[4] = 1;
                        gm_t9805qtonbsag t24 = t21;
                        gm_t9805qtonbsag t25 = t23;
                        uint8_t t26 = (!gm_eqkricceou4u(((uint8_t *)&t24), ((uint8_t *)&t25)));
                        if (!t26) goto ie20;
                        {
                            uint8_t t28 = 0;
                            F.ret = ((uint8_t)0);
                            goto gm_fret;
                        }
                        goto ix20;
ie20:;
ix20:;
                    }
                    goto me16;
mn17:;
                    gm_trap("no match arm matched the scrutinee", gm_srccvj6up3gbolp, 26, 5);
me16:;
                }
lc7:;
                {
                    uint64_t t30 = gm_l4;
                    uint64_t r31; bool o32 = __builtin_add_overflow(t30, ((uint64_t)1ULL), &r31);
                    uint64_t t33 = r31;
                    gm_l4 = t33;
                }
            }
lb7:;
        }
    }
    {
        uint8_t t34 = 0;
        F.ret = ((uint8_t)1);
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
