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
   record at the top of the frame (x86-64, Clang on AArch64), and below them where it
   keeps it at the bottom (GCC on AArch64 without a stack protector): there the frame is
   above it already. The comparison with the frame `p` tells which, and folds away. */
#define GM_STACK_CHECK(p, n) do { \
    uintptr_t gm_fa = (uintptr_t)__builtin_frame_address(0); \
    if (gm_fa < gm_stack_limit + (gm_fa > (uintptr_t)(p) ? (uintptr_t)(n) + sizeof(*(p)) : 0)) gm_stack_overflow(); \
} while (0)
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
typedef struct { __int128 f0; uint8_t f1; uint8_t pe[15]; } gm_ta24rd13gupia;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint8_t f0; uint8_t p1[1]; uint16_t f1; } gm_tjcj4j2gbh6c8;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { void * f0; void * f1; uint32_t f2; uint32_t f3; } gm_tmm67r2c94d5j;
typedef struct { uint32_t f0; uint8_t p1[4]; void * f1; } gm_ttggq7ng4kp3p;
_Static_assert(sizeof(gm_cseahvg8v7p0vs) == 12 && _Alignof(gm_cseahvg8v7p0vs) == 4 && offsetof(gm_cseahvg8v7p0vs, f0) == 0 && offsetof(gm_cseahvg8v7p0vs, f1) == 4 && offsetof(gm_cseahvg8v7p0vs, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_csguankko4lmhd) == 4 && _Alignof(gm_csguankko4lmhd) == 4 && offsetof(gm_csguankko4lmhd, f0) == 0, "layout of `Mutex`");
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_t08olq97rscn3;
typedef struct { gm_b16_8 f0; uint64_t f1; } gm_t3877pk7lv748;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
_Static_assert(sizeof(gm_t9805qtonbsag) == 8 && _Alignof(gm_t9805qtonbsag) == 4 && offsetof(gm_t9805qtonbsag, f0) == 0 && offsetof(gm_t9805qtonbsag, f1) == 4, "layout of `?u32`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
_Static_assert(sizeof(gm_ta24rd13gupia) == 32 && _Alignof(gm_ta24rd13gupia) == 16 && offsetof(gm_ta24rd13gupia, f0) == 0 && offsetof(gm_ta24rd13gupia, f1) == 16, "layout of `?i128`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
typedef struct { uint64_t f0; uint64_t f1; gm_b16_8 f2; } gm_tbccvikg6b96e;
typedef struct { uint8_t f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; } gm_tbngattl1fo51;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tcmkompbl09il;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; gm_t88rcucg1b7uk f2; } gm_tcmtrmhj7ak4u;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; uint64_t f2; uint64_t f3; } gm_teqsi7sd08g9f;
typedef struct { uint64_t f0; gm_b16_8 f1; } gm_tf1bjg81u0glv;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; } gm_tfeab279c16rr;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; uint64_t f2; } gm_tftksr4op114e;
typedef struct { gm_b16_8 f0; uint64_t f1; uint64_t f2; } gm_tgbgti82mf879;
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; } gm_tgglga8ro3isc;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; gm_b16_8 f4; gm_b16_8 f5; gm_b16_8 f6; } gm_th2d9c7aju8hp;
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_b16_8 f2; uint64_t f3; } gm_thddbqb6snabm;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
_Static_assert(sizeof(gm_tjcj4j2gbh6c8) == 4 && _Alignof(gm_tjcj4j2gbh6c8) == 2 && offsetof(gm_tjcj4j2gbh6c8, f0) == 0 && offsetof(gm_tjcj4j2gbh6c8, f1) == 2, "layout of `(signed: bool, bits: u16)`");
typedef struct { void * f0; gm_b16_8 f1; } gm_tkaapcii86udk;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t f1; uint8_t pe[3]; } gm_tkn42u7na3t81;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_tmm67r2c94d5j) == 24 && _Alignof(gm_tmm67r2c94d5j) == 8 && offsetof(gm_tmm67r2c94d5j, f0) == 0 && offsetof(gm_tmm67r2c94d5j, f1) == 8 && offsetof(gm_tmm67r2c94d5j, f2) == 16 && offsetof(gm_tmm67r2c94d5j, f3) == 20, "layout of `Printer`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t88rcucg1b7uk v0; } f1; } gm_tnnjeerolos0a;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
typedef struct { void * f0; gm_t88rcucg1b7uk f1; uint8_t p2[4]; gm_b16_8 f2; } gm_to3r6jqo24gs1;
typedef struct { uint64_t f0; gm_b16_8 f1; uint64_t f2; } gm_tob1snuii5bme;
typedef struct { uint32_t f0; uint32_t f1; gm_t88rcucg1b7uk f2; } gm_tov4mtrfp5e7v;
typedef struct { gm_tl874t2atnlh3 f0; gm_t88rcucg1b7uk f1; } gm_trlabqsjjp0ds;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
_Static_assert(sizeof(gm_ttggq7ng4kp3p) == 16 && _Alignof(gm_ttggq7ng4kp3p) == 8 && offsetof(gm_ttggq7ng4kp3p, f0) == 0 && offsetof(gm_ttggq7ng4kp3p, f1) == 8, "layout of `(index: u32, payload: ?^Value)`");
_Static_assert(sizeof(gm_t08olq97rscn3) == 32 && _Alignof(gm_t08olq97rscn3) == 8 && offsetof(gm_t08olq97rscn3, f0) == 0 && offsetof(gm_t08olq97rscn3, f1) == 16, "layout of `File`");
_Static_assert(sizeof(gm_t3877pk7lv748) == 24 && _Alignof(gm_t3877pk7lv748) == 8 && offsetof(gm_t3877pk7lv748, f0) == 0 && offsetof(gm_t3877pk7lv748, f1) == 16, "layout of `WasmReader`");
typedef struct { uint8_t f0; uint8_t p1[15]; union { gm_t08olq97rscn3 v0; unsigned __int128 v1; } f1; } gm_t3d480iurj7vi;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tcmkompbl09il v0; gm_tiilmfd16v3fc v1; gm_b16_8 v2; } f1; } gm_t428ue2u3hf5g;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_trqfvmjtt4sij f1; gm_trqfvmjtt4sij f2; gm_b16_8 f3; } gm_t4rau9abddl2u;
typedef struct { gm_tcmkompbl09il f0; uint8_t f1; uint8_t pe[7]; } gm_t57euh612ligr;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t08olq97rscn3 v0; gm_b16_8 v1; } f1; } gm_t5o0tpv5jka2l;
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; gm_tnnjeerolos0a f2; uint8_t pe[4]; } gm_t8nckeqfenl8a;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t08olq97rscn3 v0; gm_t6j6smj7qoctu v1; } f1; } gm_t9t4u07j77scv;
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
_Static_assert(sizeof(gm_tbccvikg6b96e) == 32 && _Alignof(gm_tbccvikg6b96e) == 8 && offsetof(gm_tbccvikg6b96e, f0) == 0 && offsetof(gm_tbccvikg6b96e, f1) == 8 && offsetof(gm_tbccvikg6b96e, f2) == 16, "layout of `Layout`");
typedef struct { uint8_t f0; uint8_t p1[7]; gm_trqfvmjtt4sij f1; } gm_tbci5ospk5nnn;
_Static_assert(sizeof(gm_tbngattl1fo51) == 8 && _Alignof(gm_tbngattl1fo51) == 4 && offsetof(gm_tbngattl1fo51, f0) == 0 && offsetof(gm_tbngattl1fo51, f1) == 4, "layout of `(is_const: bool, pointee: Ty)`");
_Static_assert(sizeof(gm_tcmkompbl09il) == 24 && _Alignof(gm_tcmkompbl09il) == 8 && offsetof(gm_tcmkompbl09il, f0) == 0 && offsetof(gm_tcmkompbl09il, f1) == 8, "layout of `(ty: Ty, bytes: string)`");
_Static_assert(sizeof(gm_tcmtrmhj7ak4u) == 12 && _Alignof(gm_tcmtrmhj7ak4u) == 4 && offsetof(gm_tcmtrmhj7ak4u, f0) == 0 && offsetof(gm_tcmtrmhj7ak4u, f1) == 4 && offsetof(gm_tcmtrmhj7ak4u, f2) == 8, "layout of `(op: LenOp, lhs: Ty, rhs: Ty)`");
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t p2[4]; gm_tnpifv20irnph f2; uint8_t f3; uint8_t pe[7]; } gm_tddtjfggdat0d;
_Static_assert(sizeof(gm_teqsi7sd08g9f) == 48 && _Alignof(gm_teqsi7sd08g9f) == 8 && offsetof(gm_teqsi7sd08g9f, f0) == 0 && offsetof(gm_teqsi7sd08g9f, f1) == 16 && offsetof(gm_teqsi7sd08g9f, f2) == 32 && offsetof(gm_teqsi7sd08g9f, f3) == 40, "layout of `(string, string, u64, u64)`");
_Static_assert(sizeof(gm_tf1bjg81u0glv) == 24 && _Alignof(gm_tf1bjg81u0glv) == 8 && offsetof(gm_tf1bjg81u0glv, f0) == 0 && offsetof(gm_tf1bjg81u0glv, f1) == 8, "layout of `(i: usize, x: string)`");
_Static_assert(sizeof(gm_tfeab279c16rr) == 48 && _Alignof(gm_tfeab279c16rr) == 8 && offsetof(gm_tfeab279c16rr, f0) == 0 && offsetof(gm_tfeab279c16rr, f1) == 16 && offsetof(gm_tfeab279c16rr, f2) == 32, "layout of `Toolchain`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_tftksr4op114e) == 40 && _Alignof(gm_tftksr4op114e) == 8 && offsetof(gm_tftksr4op114e, f0) == 0 && offsetof(gm_tftksr4op114e, f1) == 16 && offsetof(gm_tftksr4op114e, f2) == 32, "layout of `(string, string, usize)`");
_Static_assert(sizeof(gm_tgbgti82mf879) == 32 && _Alignof(gm_tgbgti82mf879) == 8 && offsetof(gm_tgbgti82mf879, f0) == 0 && offsetof(gm_tgbgti82mf879, f1) == 16 && offsetof(gm_tgbgti82mf879, f2) == 24, "layout of `PayloadUnion`");
_Static_assert(sizeof(gm_tgglga8ro3isc) == 8 && _Alignof(gm_tgglga8ro3isc) == 4 && offsetof(gm_tgglga8ro3isc, f0) == 0 && offsetof(gm_tgglga8ro3isc, f1) == 4, "layout of `(err: Ty, ok: Ty)`");
_Static_assert(sizeof(gm_th2d9c7aju8hp) == 112 && _Alignof(gm_th2d9c7aju8hp) == 8 && offsetof(gm_th2d9c7aju8hp, f0) == 0 && offsetof(gm_th2d9c7aju8hp, f1) == 16 && offsetof(gm_th2d9c7aju8hp, f2) == 32 && offsetof(gm_th2d9c7aju8hp, f3) == 48 && offsetof(gm_th2d9c7aju8hp, f4) == 64 && offsetof(gm_th2d9c7aju8hp, f5) == 80 && offsetof(gm_th2d9c7aju8hp, f6) == 96, "layout of `(string, string, string, string, string, string, string)`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
_Static_assert(sizeof(gm_thddbqb6snabm) == 48 && _Alignof(gm_thddbqb6snabm) == 8 && offsetof(gm_thddbqb6snabm, f0) == 0 && offsetof(gm_thddbqb6snabm, f1) == 16 && offsetof(gm_thddbqb6snabm, f2) == 24 && offsetof(gm_thddbqb6snabm, f3) == 40, "layout of `(string, u64, string, u64)`");
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
_Static_assert(sizeof(gm_tkaapcii86udk) == 24 && _Alignof(gm_tkaapcii86udk) == 8 && offsetof(gm_tkaapcii86udk, f0) == 0 && offsetof(gm_tkaapcii86udk, f1) == 8, "layout of `Place`");
_Static_assert(sizeof(gm_tkn42u7na3t81) == 8 && _Alignof(gm_tkn42u7na3t81) == 4 && offsetof(gm_tkn42u7na3t81, f0) == 0 && offsetof(gm_tkn42u7na3t81, f1) == 4, "layout of `?Ty`");
_Static_assert(sizeof(gm_tnnjeerolos0a) == 8 && _Alignof(gm_tnnjeerolos0a) == 4 && offsetof(gm_tnnjeerolos0a, f0) == 0 && offsetof(gm_tnnjeerolos0a, f1) == 4, "layout of `ArrayKind`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
_Static_assert(sizeof(gm_to3r6jqo24gs1) == 32 && _Alignof(gm_to3r6jqo24gs1) == 8 && offsetof(gm_to3r6jqo24gs1, f0) == 0 && offsetof(gm_to3r6jqo24gs1, f1) == 8 && offsetof(gm_to3r6jqo24gs1, f2) == 16, "layout of `StaticCell`");
typedef struct { gm_tkaapcii86udk f0; uint64_t f1; } gm_to6glrduves6o;
_Static_assert(sizeof(gm_tob1snuii5bme) == 32 && _Alignof(gm_tob1snuii5bme) == 8 && offsetof(gm_tob1snuii5bme, f0) == 0 && offsetof(gm_tob1snuii5bme, f1) == 8 && offsetof(gm_tob1snuii5bme, f2) == 24, "layout of `Reloc`");
_Static_assert(sizeof(gm_tov4mtrfp5e7v) == 12 && _Alignof(gm_tov4mtrfp5e7v) == 4 && offsetof(gm_tov4mtrfp5e7v, f0) == 0 && offsetof(gm_tov4mtrfp5e7v, f1) == 4 && offsetof(gm_tov4mtrfp5e7v, f2) == 8, "layout of `BitField`");
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t p2[7]; gm_tf5srh4mp4b3i f2; gm_tkn42u7na3t81 f3; } gm_tpautr4br7h3l;
typedef struct { uint64_t f0; gm_b16_8 f1; gm_t88rcucg1b7uk f2; uint8_t p3[4]; uint64_t f3; void * f4; uint8_t f5; uint8_t p6[7]; gm_th2lqbo77pvsc f6; gm_tf5srh4mp4b3i f7; void * f8; } gm_tq7qjpkmqmc5s;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t08olq97rscn3 v0; } f1; } gm_tqgtd5gtgj65s;
_Static_assert(sizeof(gm_trlabqsjjp0ds) == 16 && _Alignof(gm_trlabqsjjp0ds) == 4 && offsetof(gm_trlabqsjjp0ds, f0) == 0 && offsetof(gm_trlabqsjjp0ds, f1) == 12, "layout of `(Span, Ty)`");
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t08olq97rscn3 v0; gm_tob1snuii5bme v1; } f1; } gm_tt9fudfifskca;
_Static_assert(sizeof(gm_t3d480iurj7vi) == 48 && _Alignof(gm_t3d480iurj7vi) == 16 && offsetof(gm_t3d480iurj7vi, f0) == 0 && offsetof(gm_t3d480iurj7vi, f1) == 16, "layout of `Unsup!u128`");
_Static_assert(sizeof(gm_t428ue2u3hf5g) == 32 && _Alignof(gm_t428ue2u3hf5g) == 8 && offsetof(gm_t428ue2u3hf5g, f0) == 0 && offsetof(gm_t428ue2u3hf5g, f1) == 8, "layout of `ConstKey`");
_Static_assert(sizeof(gm_t4rau9abddl2u) == 104 && _Alignof(gm_t4rau9abddl2u) == 8 && offsetof(gm_t4rau9abddl2u, f0) == 0 && offsetof(gm_t4rau9abddl2u, f1) == 8 && offsetof(gm_t4rau9abddl2u, f2) == 48 && offsetof(gm_t4rau9abddl2u, f3) == 88, "layout of `SiteConst`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tjcj4j2gbh6c8 v0; uint16_t v1; gm_tbngattl1fo51 v2; gm_tbngattl1fo51 v3; gm_tbngattl1fo51 v4; gm_t8nckeqfenl8a v5; gm_t6j6smj7qoctu v6; gm_t88rcucg1b7uk v7; gm_tgglga8ro3isc v8; gm_tddtjfggdat0d v9; gm_tddtjfggdat0d v10; gm_tiilmfd16v3fc v11; gm_t88rcucg1b7uk v12; uint32_t v13; gm_tgglga8ro3isc v14; gm_t428ue2u3hf5g v15; gm_tcmtrmhj7ak4u v16; uint32_t v17; gm_tnpifv20irnph v18; uint32_t v19; } f1; } gm_t53at80pfdus3;
_Static_assert(sizeof(gm_t57euh612ligr) == 32 && _Alignof(gm_t57euh612ligr) == 8 && offsetof(gm_t57euh612ligr, f0) == 0 && offsetof(gm_t57euh612ligr, f1) == 24, "layout of `?BitsLayout`");
_Static_assert(sizeof(gm_t5o0tpv5jka2l) == 40 && _Alignof(gm_t5o0tpv5jka2l) == 8 && offsetof(gm_t5o0tpv5jka2l, f0) == 0 && offsetof(gm_t5o0tpv5jka2l, f1) == 8, "layout of `Unsup!string`");
typedef struct { uint8_t f0; uint8_t p1[15]; union { unsigned __int128 v0; double v1; uint8_t v2; void * v3; gm_tkaapcii86udk v4; gm_to6glrduves6o v5; void * v6; void * v7; void * v8; gm_ttggq7ng4kp3p v9; uint32_t v10; gm_ttggq7ng4kp3p v11; gm_ttggq7ng4kp3p v12; } f1; } gm_t6evimt84lndq;
_Static_assert(sizeof(gm_t8nckeqfenl8a) == 24 && _Alignof(gm_t8nckeqfenl8a) == 8 && offsetof(gm_t8nckeqfenl8a, f0) == 0 && offsetof(gm_t8nckeqfenl8a, f1) == 8 && offsetof(gm_t8nckeqfenl8a, f2) == 12, "layout of `(len: u64, elem: Ty, kind: ArrayKind)`");
typedef struct { uint64_t f0; gm_tq7qjpkmqmc5s f1; } gm_t964gsbl2irde;
_Static_assert(sizeof(gm_t9t4u07j77scv) == 40 && _Alignof(gm_t9t4u07j77scv) == 8 && offsetof(gm_t9t4u07j77scv, f0) == 0 && offsetof(gm_t9t4u07j77scv, f1) == 8, "layout of `Unsup!ConstsText`");
_Static_assert(sizeof(gm_tbci5ospk5nnn) == 48 && _Alignof(gm_tbci5ospk5nnn) == 8 && offsetof(gm_tbci5ospk5nnn, f0) == 0 && offsetof(gm_tbci5ospk5nnn, f1) == 8, "layout of `Int`");
typedef struct { gm_b16_8 f0; gm_tkn42u7na3t81 f1; gm_tbci5ospk5nnn f2; } gm_tckdobunaulee;
_Static_assert(sizeof(gm_tddtjfggdat0d) == 48 && _Alignof(gm_tddtjfggdat0d) == 8 && offsetof(gm_tddtjfggdat0d, f0) == 0 && offsetof(gm_tddtjfggdat0d, f1) == 16 && offsetof(gm_tddtjfggdat0d, f2) == 24 && offsetof(gm_tddtjfggdat0d, f3) == 40, "layout of `FnTy`");
_Static_assert(sizeof(gm_to6glrduves6o) == 32 && _Alignof(gm_to6glrduves6o) == 8 && offsetof(gm_to6glrduves6o, f0) == 0 && offsetof(gm_to6glrduves6o, f1) == 24, "layout of `(start: Place, len: u64)`");
_Static_assert(sizeof(gm_tpautr4br7h3l) == 48 && _Alignof(gm_tpautr4br7h3l) == 8 && offsetof(gm_tpautr4br7h3l, f0) == 0 && offsetof(gm_tpautr4br7h3l, f1) == 16 && offsetof(gm_tpautr4br7h3l, f2) == 24 && offsetof(gm_tpautr4br7h3l, f3) == 40, "layout of `StructDef`");
_Static_assert(sizeof(gm_tq7qjpkmqmc5s) == 104 && _Alignof(gm_tq7qjpkmqmc5s) == 8 && offsetof(gm_tq7qjpkmqmc5s, f0) == 0 && offsetof(gm_tq7qjpkmqmc5s, f1) == 8 && offsetof(gm_tq7qjpkmqmc5s, f2) == 24 && offsetof(gm_tq7qjpkmqmc5s, f3) == 32 && offsetof(gm_tq7qjpkmqmc5s, f4) == 40 && offsetof(gm_tq7qjpkmqmc5s, f5) == 48 && offsetof(gm_tq7qjpkmqmc5s, f6) == 56 && offsetof(gm_tq7qjpkmqmc5s, f7) == 80 && offsetof(gm_tq7qjpkmqmc5s, f8) == 96, "layout of `GlobalInfo`");
_Static_assert(sizeof(gm_tqgtd5gtgj65s) == 40 && _Alignof(gm_tqgtd5gtgj65s) == 8 && offsetof(gm_tqgtd5gtgj65s, f0) == 0 && offsetof(gm_tqgtd5gtgj65s, f1) == 8, "layout of `Unsup!()`");
_Static_assert(sizeof(gm_tt9fudfifskca) == 40 && _Alignof(gm_tt9fudfifskca) == 8 && offsetof(gm_tt9fudfifskca, f0) == 0 && offsetof(gm_tt9fudfifskca, f1) == 8, "layout of `Unsup!Reloc`");
_Static_assert(sizeof(gm_t53at80pfdus3) == 56 && _Alignof(gm_t53at80pfdus3) == 8 && offsetof(gm_t53at80pfdus3, f0) == 0 && offsetof(gm_t53at80pfdus3, f1) == 8, "layout of `TyKind`");
_Static_assert(sizeof(gm_t6evimt84lndq) == 48 && _Alignof(gm_t6evimt84lndq) == 16 && offsetof(gm_t6evimt84lndq, f0) == 0 && offsetof(gm_t6evimt84lndq, f1) == 16, "layout of `Value`");
_Static_assert(sizeof(gm_t964gsbl2irde) == 112 && _Alignof(gm_t964gsbl2irde) == 8 && offsetof(gm_t964gsbl2irde, f0) == 0 && offsetof(gm_t964gsbl2irde, f1) == 8, "layout of `(i: usize, x: GlobalInfo)`");
_Static_assert(sizeof(gm_tckdobunaulee) == 72 && _Alignof(gm_tckdobunaulee) == 8 && offsetof(gm_tckdobunaulee, f0) == 0 && offsetof(gm_tckdobunaulee, f1) == 16 && offsetof(gm_tckdobunaulee, f2) == 24, "layout of `VariantDef`");
typedef struct { uint64_t f0; uint8_t p1[8]; gm_t6evimt84lndq f1; } gm_thf8as07kj3uj;
_Static_assert(sizeof(gm_thf8as07kj3uj) == 64 && _Alignof(gm_thf8as07kj3uj) == 16 && offsetof(gm_thf8as07kj3uj, f0) == 0 && offsetof(gm_thf8as07kj3uj, f1) == 16, "layout of `(i: usize, x: Value)`");
struct gm_fr_adt_mpqv8qe {
    gm_frame h;
    void * ret;
};
struct gm_fr_align_of_k6eqqrn {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_align_uu2harl {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_bit_width_u86m8ir {
    gm_frame h;
    uint32_t ret;
};
struct gm_fr_bits_layout_23rect9 {
    gm_frame h;
    gm_t57euh612ligr ret;
};
struct gm_fr_bits_of_ilgn83m {
    gm_frame h;
    gm_t6evimt84lndq l1; /* v */
    gm_t6evimt84lndq l17; /* x */
    gm_t3d480iurj7vi ret;
};
struct gm_fr_cell_storage_1rtp280 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_cell_type_0jgf3ue {
    gm_frame h;
    gm_tkn42u7na3t81 ret;
};
struct gm_fr_const_id_2mlh0og {
    gm_frame h;
    gm_t9805qtonbsag ret;
};
struct gm_fr_const_storage_0d7nj5d {
    gm_frame h;
    gm_t5o0tpv5jka2l ret;
};
struct gm_fr_empty_slice_8rccll4 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ijtp9lo {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_gen_consts_6ibghrc {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* decls */
    gm_trqfvmjtt4sij l2; /* init */
    gm_trqfvmjtt4sij l8; /* bytes */
    gm_trqfvmjtt4sij l11; /* relocs */
    gm_tq7qjpkmqmc5s l19; /* gl */
    gm_trqfvmjtt4sij l25; /* tls_init */
    gm_trqfvmjtt4sij l28; /* bytes */
    gm_trqfvmjtt4sij l31; /* relocs */
    gm_trqfvmjtt4sij l39; /* bytes */
    gm_trqfvmjtt4sij l42; /* relocs */
    gm_t4rau9abddl2u l49; /* sc */
    gm_t9t4u07j77scv ret;
};
struct gm_fr_keyed_h72lrop {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_kind_ghvav22 {
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
struct gm_fr_list_opmir2c {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_uvfu1k1 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_nullable_pointer_7qfmn6o {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_offsets_eh6vfle {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_place_reloc_blfij5d {
    gm_frame h;
    gm_tpautr4br7h3l l16; /* sd */
    gm_tt9fudfifskca ret;
};
struct gm_fr_printer_h7jigd7 {
    gm_frame h;
    gm_tmm67r2c94d5j ret;
};
struct gm_fr_show_5qbi623 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_show_kmjeh8s {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* out */
    gm_b16_8 ret;
};
struct gm_fr_size_dmhtkib {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_size_of_k0fluj2 {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_store_int_mfkaqk5 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_unsupported_531q51r {
    gm_frame h;
    gm_t08olq97rscn3 ret;
};
struct gm_fr_value_bytes_qm8egmj {
    gm_frame h;
    gm_t6evimt84lndq l1; /* v */
    gm_tckdobunaulee l27; /* vd */
    gm_t6evimt84lndq l38; /* x */
    gm_t6evimt84lndq l44; /* x */
    gm_tpautr4br7h3l l46; /* sd */
    gm_t6evimt84lndq l53; /* x */
    gm_tqgtd5gtgj65s ret;
};
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b);
inline bool gm_eq0ogca82g1f(const uint8_t *a, const uint8_t *b);
__attribute__((aligned(4))) gm_b16_8 gm_f_addr_name_g5lrlt3(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
inline __attribute__((aligned(4))) void * gm_f_adt_mpqv8qe(void * a0, uint32_t a1);
inline __attribute__((aligned(4))) uint64_t gm_f_align_of_k6eqqrn(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) uint64_t gm_f_align_uu2harl(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_array_th8sdi3(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_t88rcucg1b7uk a2);
inline __attribute__((aligned(4))) uint32_t gm_f_bit_width_u86m8ir(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) void gm_f_bits_layout_23rect9(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_bits_layout_u6kkccp(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_bits_of_ilgn83m(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) gm_b16_8 gm_f_c_string_n7h27b3(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_cell_storage_1rtp280(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3);
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_cell_type_0jgf3ue(void * a0, void * a1);
inline __attribute__((aligned(4))) gm_t9805qtonbsag gm_f_const_id_2mlh0og(void * a0, gm_cseahvg8v7p0vs a1, gm_csguankko4lmhd a2);
__attribute__((aligned(4))) void gm_f_const_storage_0d7nj5d(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t88rcucg1b7uk a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) void gm_f_drain_leoibma(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_8rccll4(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_extend_0cs85gs(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) uint32_t gm_f_field_bits_2ktfm1k(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_format_0o96lei(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_17eulka(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_317bju7(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_8n626pi(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_be3fee1(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_fsfutor(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_gen_consts_6ibghrc(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_t9805qtonbsag gm_f_get_5ktivom(void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_t9805qtonbsag gm_f_get_pk8ncss(void * a0, gm_trlabqsjjp0ds a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_global_tag_gs7s3p6(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_hash_text_4aekv10(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, uint64_t a1);
__attribute__((aligned(4))) uint8_t gm_f_is_nullable_pointer_nkie06s(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_keyed_h72lrop(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_kind_ghvav22(void *gm_out, void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void gm_f_kind_jcnt4hm(void *gm_out, void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void gm_f_layout_a5ovnrm(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) void gm_f_layout_of_e6hfj1r(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) void gm_f_list_opmir2c(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_b16_8 gm_f_module_key_m4hm4f5(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
inline __attribute__((aligned(4))) uint8_t gm_f_nullable_pointer_7qfmn6o(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_offsets_eh6vfle(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_place_reloc_blfij5d(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_t88rcucg1b7uk a2, uint64_t a3);
inline __attribute__((aligned(4))) void gm_f_printer_h7jigd7(void *gm_out, void * a0, void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_dffnss0(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_t83rg2m(void * a0, uint8_t a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_vgtfcjf(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_uldb5pe(void * a0, uint64_t a1, uint32_t a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_queue_cd51r8c(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_5qbi623(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_kmjeh8s(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) uint64_t gm_f_size_dmhtkib(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) uint64_t gm_f_size_of_k0fluj2(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_static_bytes_u0uvco3(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_store_int_mfkaqk5(void * a0, gm_b16_8 a1, uint64_t a2, unsigned __int128 a3, uint64_t a4);
__attribute__((aligned(4))) void gm_f_to_i128_o7ud1ue(void *gm_out, const void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_ty_vka2r8h(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_unique_fba8sj3(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) void gm_f_unsupported_531q51r(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_value_bytes_qm8egmj(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3, uint64_t a4, void * a5);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_60kg76i(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_80k6mh9(void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_dhp21l9(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_lket48d(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_qbs7gku(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_snsbmln(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_t09mk5p(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_th5g0ba(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_u9o0juu(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_ufh35j8(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_vlrefbu(void * a0, const void * a1);
extern uint8_t gm_cejn1dj0054[12] __attribute__((aligned(4)));
extern const uint8_t gm_s0ggo8ek7kfrt[34];
extern const uint8_t gm_s2uimijqjekbd[67];
extern const uint8_t gm_s31vj4bjbj0gj[36];
extern const uint8_t gm_s3js9g3l46oiv[40];
extern const uint8_t gm_s67fd8s6hf84e[24];
extern const uint8_t gm_sa6mes8ijqmmk[30];
extern const uint8_t gm_sb8jvk2vbau9p[25];
extern const uint8_t gm_sbbavs945cjhp[32];
extern const uint8_t gm_sbe61ep7tock5[5];
extern const uint8_t gm_sbe7f0bkvca75[43];
extern const uint8_t gm_scc54gv4a7k04[6];
extern const uint8_t gm_sfali8nl85hc9[25];
extern const uint8_t gm_sg1uf2uigo01q[27];
extern const uint8_t gm_sgiqpj781s1a9[36];
extern const uint8_t gm_sikpqtja8pqpv[3];
extern const uint8_t gm_skff6pclobvnu[40];
extern const uint8_t gm_slpqve1dqssq0[8];
extern const uint8_t gm_sqrl3kgomorsv[39];
extern const uint8_t gm_sr4qbrk6tlgaa[1];
extern const char gm_src0ih77f43mhf2[];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_srcd5arorrf8jkg[];
extern const char gm_srcht195tfrfuqo[];
extern const char gm_srcrgc9bkquf6nr[];
extern const char gm_srcuj9anpi7p1mq[];
extern const char gm_srcuro7l7pd15c2[];
extern const uint8_t gm_svlcdvcv2bp94[24];
extern const gm_type gm_tyo86ks9qkvt;
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b) {
    uintptr_t n = GM_LD(uintptr_t, a + 8);
    if (n != GM_LD(uintptr_t, b + 8)) return false;
    const uint8_t *pa = GM_LD(void *, a), *pb = GM_LD(void *, b);
    return n == 0 || memcmp(pa, pb, n * 1) == 0;
}
inline bool gm_eq0ogca82g1f(const uint8_t *a, const uint8_t *b) {
    if (a[16] != b[16]) return false;
    return !a[16] || gm_eq0iqiubfm7e(a, b);
}
inline __attribute__((aligned(4))) void * gm_f_adt_mpqv8qe(void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_adt_mpqv8qe F;
    void * gm_l0; /* p */
    uint32_t gm_l1; /* id */
    GM_STACK_CHECK_SHALLOW(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint32_t t1 = gm_l1;
    uint64_t t2 = (uint64_t)t1;
    uint64_t t3 = (uintptr_t)t2;
    void * t4 = gm_l0;
    void * t5 = GM_LD(void *, (((uint8_t *)t4) + 0));
    uint64_t t6 = GM_LD(uintptr_t, ((((uint8_t *)t5) + 1088) + 0) + 8);
    if (t3 >= (uintptr_t)(t6)) gm_trap_index(t3, t6, gm_srcuj9anpi7p1mq, 135, 63);
    void * t7 = (void *)((uint8_t *)GM_LD(void *, ((((uint8_t *)t5) + 1088) + 0)) + t3 * 184);
    F.ret = t7;
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
inline __attribute__((aligned(4))) uint64_t gm_f_align_uu2harl(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_align_uu2harl F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 56u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    void * t2 = (void *)(((uint8_t *)t1) + 8);
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
inline __attribute__((aligned(4))) uint32_t gm_f_bit_width_u86m8ir(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_bit_width_u86m8ir F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    void * t2 = GM_LD(void *, (((uint8_t *)t1) + 0));
    gm_t88rcucg1b7uk t3 = gm_l1;
    uint32_t t4 = gm_f_field_bits_2ktfm1k((void *)NULL, cx, NULL, t2, t3);
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
inline __attribute__((aligned(4))) void gm_f_bits_layout_23rect9(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_bits_layout_23rect9 F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    void * t2 = GM_LD(void *, (((uint8_t *)t1) + 0));
    gm_t88rcucg1b7uk t3 = gm_l1;
    gm_f_bits_layout_u6kkccp((void *)&(*(gm_t57euh612ligr *)gm_out), (void *)NULL, cx, NULL, t2, t3);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_8rccll4(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_8rccll4 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_keyed_h72lrop(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2) {
    struct gm_fr_keyed_h72lrop F;
    void * gm_l0; /* n */
    gm_b16_8 gm_l1; /* prefix */
    gm_b16_8 gm_l2; /* key */
    GM_STACK_CHECK(&F, 272u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t1 = gm_l0;
    gm_t6j6smj7qoctu t2;
    gm_b16_8 t3 = gm_l1;
    GM_ST(gm_b16_8, (uint8_t *)&t2 + 0, t3);
    gm_b16_8 t4 = gm_l2;
    gm_b16_8 t5 = gm_f_hash_text_4aekv10((void *)NULL, cx, NULL, t4, ((uint64_t)10ULL));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    GM_ST(gm_b16_8, (uint8_t *)&t2 + 16, t5);
    gm_tabb710d9mq2e t6;
    gm_f_format_8n626pi((void *)&t6, (void *)NULL, cx, NULL, &t2);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tabb710d9mq2e t7 = t6;
    if (((((uint8_t *)&t7))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t7) + 8, gm_srcht195tfrfuqo, 80, 91);
    gm_b16_8 t8 = GM_LD(gm_b16_8, (((uint8_t *)&t7) + 8));
    gm_b16_8 t9 = t8;
    gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
    gm_b16_8 t11 = gm_f_unique_fba8sj3((void *)NULL, cx, NULL, t1, t10);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.ret = t11;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
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
inline __attribute__((aligned(4))) void gm_f_list_opmir2c(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_opmir2c F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_8rccll4();
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
inline __attribute__((aligned(4))) uint8_t gm_f_nullable_pointer_7qfmn6o(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_nullable_pointer_7qfmn6o F;
    void * gm_l0; /* s */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK_SHALLOW(&F, 27u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    void * t1 = gm_l0;
    gm_t88rcucg1b7uk t2 = gm_l1;
    uint8_t t3 = gm_f_is_nullable_pointer_nkie06s(t1, GM_CONV(gm_csguankko4lmhd, t2));
    F.ret = t3;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_offsets_eh6vfle(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_offsets_eh6vfle F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 144u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    void * t2 = (void *)(((uint8_t *)t1) + 8);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_5qbi623(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_show_5qbi623 F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 88u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    void * t2 = GM_LD(void *, (((uint8_t *)t1) + 0));
    void * t3 = (void *)(((uint8_t *)t2) + 8);
    gm_t88rcucg1b7uk t4 = gm_l1;
    gm_b16_8 t5 = gm_f_show_kmjeh8s((void *)NULL, cx, NULL, t3, t4);
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
inline __attribute__((aligned(4))) uint64_t gm_f_size_dmhtkib(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_size_dmhtkib F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 56u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    void * t2 = (void *)(((uint8_t *)t1) + 8);
    gm_t88rcucg1b7uk t3 = gm_l1;
    uint64_t t4 = gm_f_size_of_k0fluj2((void *)NULL, cx, NULL, t2, t3);
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
inline __attribute__((aligned(4))) uint64_t gm_f_size_of_k0fluj2(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_size_of_k0fluj2 F;
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
    uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)&t5) + 0));
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
inline __attribute__((aligned(4))) void gm_f_unsupported_531q51r(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0, gm_b16_8 a1) {
    struct gm_fr_unsupported_531q51r F;
    gm_tl874t2atnlh3 gm_l0; /* span */
    gm_b16_8 gm_l1; /* what */
    GM_STACK_CHECK(&F, 168u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        memset((uint8_t *)&(*(gm_t08olq97rscn3 *)gm_out) + 28, 0, 4);
        gm_b16_8 t2 = gm_l1;
        gm_tnpifv20irnph t3;
        GM_ST(gm_b16_8, ((uint8_t *)&t3 + 0), t2);
        gm_tabb710d9mq2e t4;
        gm_f_format_fsfutor((void *)&t4, (void *)NULL, cx, NULL, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t5 = t4;
        if (((((uint8_t *)&t5))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t5) + 8, gm_srcd5arorrf8jkg, 35, 27);
        gm_b16_8 t6 = GM_LD(gm_b16_8, (((uint8_t *)&t5) + 8));
        gm_b16_8 t7 = t6;
        gm_b16_8 t8 = GM_LD(gm_b16_8, ((uint8_t *)&t7));
        gm_tl874t2atnlh3 t9 = gm_l0;
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t08olq97rscn3 *)gm_out) + 0, t8);
        GM_ST(gm_tl874t2atnlh3, (uint8_t *)&(*(gm_t08olq97rscn3 *)gm_out) + 16, t9);
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
__attribute__((aligned(4))) void gm_f_bits_of_ilgn83m(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_t88rcucg1b7uk a2) {
    struct gm_fr_bits_of_ilgn83m F;
    void * gm_l0; /* g */
    gm_t88rcucg1b7uk gm_l2; /* t */
    uint8_t gm_l3; /* b */
    unsigned __int128 gm_l4; /* bits */
    uint32_t gm_l5; /* w */
    uint32_t gm_l6; /* index */
    uint32_t gm_l7; /* id */
    gm_t88rcucg1b7uk gm_l8; /* tag_ty */
    gm_b16_8 gm_l9; /* variants */
    __int128 gm_l10; /* tag */
    void * gm_l11; /* a */
    gm_tcmkompbl09il gm_l12; /* bl */
    unsigned __int128 gm_l13; /* raw */
    gm_b16_8 gm_l14; /* <tmp> */
    uint64_t gm_l15; /* <tmp> */
    uint64_t gm_l16; /* k */
    gm_tov4mtrfp5e7v gm_l18; /* f */
    GM_STACK_CHECK(&F, 2671u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&F.l1, a1, sizeof F.l1);
    gm_l2 = a2;
    {
        uint8_t t9 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
        switch (t9) { case ((uint8_t)0ULL): goto ma4; case ((uint8_t)1ULL): goto ma7; case ((uint8_t)2ULL): goto ma3; case ((uint8_t)3ULL): goto ma6; case ((uint8_t)4ULL): goto ma7; case ((uint8_t)5ULL): goto ma7; case ((uint8_t)6ULL): goto ma7; case ((uint8_t)7ULL): goto ma7; case ((uint8_t)8ULL): goto ma7; case ((uint8_t)9ULL): goto ma7; case ((uint8_t)10ULL): goto ma5; case ((uint8_t)11ULL): goto ma7; case ((uint8_t)12ULL): goto ma7; case ((uint8_t)13ULL): goto ma7; default: goto ma7; }
ma3:;
        uint8_t t11 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
        if (t11 != ((uint8_t)2ULL)) goto mn10;
        uint8_t t12 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 16));
        gm_l3 = t12;
        gm_t3d480iurj7vi t13;
        uint8_t t15 = gm_l3;
        if (!t15) goto ie14;
        unsigned __int128 t17 = ((unsigned __int128)(((unsigned __int128)0u << 64) | 1u));
        gm_t3d480iurj7vi t18;
        GM_ST(uint8_t, (uint8_t *)&t18 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t18 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t18 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t18 + 8, 0);
        GM_ST(uint64_t, (uint8_t *)&t18 + 32, 0);
        GM_ST(uint64_t, (uint8_t *)&t18 + 40, 0);
        ((uint8_t *)&t18)[0] = 0;
        GM_ST(unsigned __int128, ((uint8_t *)&t18 + 16), t17);
        t13 = t18;
        goto ix14;
ie14:;
        unsigned __int128 t20 = ((unsigned __int128)(((unsigned __int128)0u << 64) | 0u));
        gm_t3d480iurj7vi t21;
        GM_ST(uint8_t, (uint8_t *)&t21 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t21 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t21 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t21 + 8, 0);
        GM_ST(uint64_t, (uint8_t *)&t21 + 32, 0);
        GM_ST(uint64_t, (uint8_t *)&t21 + 40, 0);
        ((uint8_t *)&t21)[0] = 0;
        GM_ST(unsigned __int128, ((uint8_t *)&t21 + 16), t20);
        t13 = t21;
ix14:;
        gm_t3d480iurj7vi t22 = t13;
        uint8_t t23 = ((((uint8_t *)&t22))[0] != 0);
        F.ret = t13;
        goto gm_fret;
        goto me2;
mn10:;
ma4:;
        uint8_t t25 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
        if (t25 != ((uint8_t)0ULL)) goto mn24;
        unsigned __int128 t26 = GM_LD(unsigned __int128, (((uint8_t *)&F.l1) + 16));
        gm_l4 = t26;
        {
            void * t28 = gm_l0;
            void * t29 = (void *)(((uint8_t *)t28) + 8);
            gm_t88rcucg1b7uk t30 = gm_l2;
            uint32_t t31 = gm_f_bit_width_u86m8ir((void *)NULL, cx, NULL, t29, t30);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l5 = t31;
        }
        {
            gm_t3d480iurj7vi t32;
            uint32_t t34 = gm_l5;
            uint8_t t35 = (t34 >= ((uint32_t)128ULL));
            if (!t35) goto ie33;
            unsigned __int128 t37 = gm_l4;
            unsigned __int128 t38 = t37;
            gm_t3d480iurj7vi t39;
            GM_ST(uint8_t, (uint8_t *)&t39 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t39 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t39 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t39 + 8, 0);
            GM_ST(uint64_t, (uint8_t *)&t39 + 32, 0);
            GM_ST(uint64_t, (uint8_t *)&t39 + 40, 0);
            ((uint8_t *)&t39)[0] = 0;
            GM_ST(unsigned __int128, ((uint8_t *)&t39 + 16), t38);
            t32 = t39;
            goto ix33;
ie33:;
            unsigned __int128 t41 = gm_l4;
            uint32_t t42 = gm_l5;
            unsigned __int128 t43 = (unsigned __int128)t42;
            unsigned __int128 n44 = (unsigned __int128)t43;
            if (n44 >= 128) gm_trap_shift((uint64_t)n44, "u128", gm_srcuro7l7pd15c2, 323, 57);
            unsigned __int128 r45 = (unsigned __int128)((unsigned __int128)((unsigned __int128)(((unsigned __int128)0u << 64) | 1u)) << (int)n44);
            bool o46 = (unsigned __int128)(r45 >> (int)n44) != ((unsigned __int128)(((unsigned __int128)0u << 64) | 1u));
            if (o46) gm_trap("integer overflow in `<<`", gm_srcuro7l7pd15c2, 323, 57);
            unsigned __int128 t47 = r45;
            unsigned __int128 r48; bool o49 = __builtin_sub_overflow(t47, ((unsigned __int128)(((unsigned __int128)0u << 64) | 1u)), &r48);
            if (o49) gm_trap("integer overflow in `-`", gm_srcuro7l7pd15c2, 323, 56);
            unsigned __int128 t50 = r48;
            unsigned __int128 t51 = (unsigned __int128)(t41 & t50);
            unsigned __int128 t52 = t51;
            gm_t3d480iurj7vi t53;
            GM_ST(uint8_t, (uint8_t *)&t53 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t53 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t53 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t53 + 8, 0);
            GM_ST(uint64_t, (uint8_t *)&t53 + 32, 0);
            GM_ST(uint64_t, (uint8_t *)&t53 + 40, 0);
            ((uint8_t *)&t53)[0] = 0;
            GM_ST(unsigned __int128, ((uint8_t *)&t53 + 16), t52);
            t32 = t53;
ix33:;
            gm_t3d480iurj7vi t54 = t32;
            uint8_t t55 = ((((uint8_t *)&t54))[0] != 0);
            F.ret = t32;
            goto gm_fret;
        }
        goto me2;
mn24:;
ma5:;
        uint8_t t57 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
        if (t57 != ((uint8_t)10ULL)) goto mn56;
        uint32_t t58 = GM_LD(uint32_t, ((((uint8_t *)&F.l1) + 16) + 0));
        gm_l6 = t58;
        {
            void * t61 = gm_l0;
            void * t62 = (void *)(((uint8_t *)t61) + 8);
            gm_t88rcucg1b7uk t63 = gm_l2;
            gm_t53at80pfdus3 t64;
            gm_f_kind_ghvav22((void *)&t64, t62, GM_CONV(gm_csguankko4lmhd, t63));
            gm_t53at80pfdus3 t65 = t64;
            uint8_t t66 = GM_LD(uint8_t, (((uint8_t *)&t65) + 0));
            if (t66 != ((uint8_t)17ULL)) goto ie60;
            uint32_t t67 = GM_LD(uint32_t, ((((uint8_t *)&t65) + 8) + 0));
            gm_l7 = t67;
            {
                void * t70 = gm_l0;
                void * t71 = GM_LD(void *, (((uint8_t *)t70) + 0));
                uint32_t t72 = gm_l7;
                void * t73 = gm_f_adt_mpqv8qe(t71, t72);
                uint8_t t74 = GM_LD(uint8_t, ((((uint8_t *)t73) + 24) + 0));
                if (t74 != ((uint8_t)1ULL)) goto ie69;
                gm_t88rcucg1b7uk t75 = GM_LD(gm_t88rcucg1b7uk, (((((uint8_t *)t73) + 24) + 8) + 0));
                gm_l8 = t75;
                gm_b16_8 t76 = GM_LD(gm_b16_8, (((((uint8_t *)t73) + 24) + 8) + 8));
                gm_l9 = t76;
                {
                    uint32_t t78 = gm_l6;
                    uint64_t t79 = (uint64_t)t78;
                    uint64_t t80 = (uintptr_t)t79;
                    uint64_t t81 = GM_LD(uintptr_t, ((uint8_t *)&gm_l9) + 8);
                    if (t80 >= (uintptr_t)(t81)) gm_trap_index(t80, t81, gm_srcuro7l7pd15c2, 328, 35);
                    gm_ta24rd13gupia t82;
                    gm_f_to_i128_o7ud1ue((void *)&t82, &(*(gm_tbci5ospk5nnn *)(((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l9)) + t80 * 72) + 24)));
                    gm_ta24rd13gupia t83 = t82;
                    __int128 t84;
                    if (((((uint8_t *)&t83))[16] != 0)) {
                        __int128 t85 = GM_LD(__int128, (((uint8_t *)&t83) + 0));
                        t84 = t85;
                    } else {
                        t84 = ((__int128)(((unsigned __int128)0u << 64) | 0u));
                    }
                    gm_l10 = t84;
                }
                {
                    void * t86 = gm_l0;
                    __int128 t87 = gm_l10;
                    unsigned __int128 t88 = (unsigned __int128)t87;
                    gm_t6evimt84lndq t89;
                    GM_ST(uint8_t, (uint8_t *)&t89 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t89 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t89 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t89 + 8, 0);
                    GM_ST(uint64_t, (uint8_t *)&t89 + 32, 0);
                    GM_ST(uint64_t, (uint8_t *)&t89 + 40, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t89 + 0), ((uint8_t)0ULL));
                    GM_ST(unsigned __int128, ((uint8_t *)&t89 + 16), t88);
                    gm_t88rcucg1b7uk t90 = gm_l8;
                    gm_f_bits_of_ilgn83m((void *)&(*(gm_t3d480iurj7vi *)gm_out), (void *)NULL, cx, NULL, t86, &t89, t90);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_fdone;
                }
                goto ix69;
ie69:;
                {
                    gm_tl874t2atnlh3 t92 = GM_LD(gm_tl874t2atnlh3, gm_cejn1dj0054);
                    gm_b16_8 t93;
                    GM_ST(void *, (uint8_t *)&t93, (void *)(gm_sfali8nl85hc9));
                    GM_ST(uintptr_t, (uint8_t *)&t93 + 8, (uintptr_t)(24));
                    gm_t08olq97rscn3 t94;
                    gm_f_unsupported_531q51r((void *)&t94, (void *)NULL, cx, NULL, t92, t93);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_t3d480iurj7vi t95;
                    GM_ST(uint8_t, (uint8_t *)&t95 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t95 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t95 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t95 + 8, 0);
                    ((uint8_t *)&t95)[0] = 1;
                    GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t95 + 16), t94);
                    gm_t3d480iurj7vi t96 = t95;
                    uint8_t t97 = ((((uint8_t *)&t96))[0] != 0);
                    F.ret = t95;
                    goto gm_fret;
                }
ix69:;
            }
            goto ix60;
ie60:;
            {
                gm_tl874t2atnlh3 t99 = GM_LD(gm_tl874t2atnlh3, gm_cejn1dj0054);
                gm_b16_8 t100;
                GM_ST(void *, (uint8_t *)&t100, (void *)(gm_sfali8nl85hc9));
                GM_ST(uintptr_t, (uint8_t *)&t100 + 8, (uintptr_t)(24));
                gm_t08olq97rscn3 t101;
                gm_f_unsupported_531q51r((void *)&t101, (void *)NULL, cx, NULL, t99, t100);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_t3d480iurj7vi t102;
                GM_ST(uint8_t, (uint8_t *)&t102 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t102 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t102 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t102 + 8, 0);
                ((uint8_t *)&t102)[0] = 1;
                GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t102 + 16), t101);
                gm_t3d480iurj7vi t103 = t102;
                uint8_t t104 = ((((uint8_t *)&t103))[0] != 0);
                F.ret = t102;
                goto gm_fret;
            }
ix60:;
        }
        goto me2;
mn56:;
ma6:;
        uint8_t t106 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
        if (t106 != ((uint8_t)3ULL)) goto mn105;
        void * t107 = GM_LD(void *, (((uint8_t *)&F.l1) + 16));
        gm_l11 = t107;
        {
            void * t109 = gm_l0;
            void * t110 = (void *)(((uint8_t *)t109) + 8);
            gm_t88rcucg1b7uk t111 = gm_l2;
            gm_t57euh612ligr t112;
            gm_f_bits_layout_23rect9((void *)&t112, (void *)NULL, cx, NULL, t110, t111);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t57euh612ligr t113 = t112;
            gm_tcmkompbl09il t114;
            if (((((uint8_t *)&t113))[24] != 0)) {
                gm_tcmkompbl09il t115 = GM_LD(gm_tcmkompbl09il, (((uint8_t *)&t113) + 0));
                t114 = t115;
            } else {
                gm_tl874t2atnlh3 t116 = GM_LD(gm_tl874t2atnlh3, gm_cejn1dj0054);
                gm_b16_8 t117;
                GM_ST(void *, (uint8_t *)&t117, (void *)(gm_sfali8nl85hc9));
                GM_ST(uintptr_t, (uint8_t *)&t117 + 8, (uintptr_t)(24));
                gm_t08olq97rscn3 t118;
                gm_f_unsupported_531q51r((void *)&t118, (void *)NULL, cx, NULL, t116, t117);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_t3d480iurj7vi t119;
                GM_ST(uint8_t, (uint8_t *)&t119 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t119 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t119 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t119 + 8, 0);
                ((uint8_t *)&t119)[0] = 1;
                GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t119 + 16), t118);
                gm_t3d480iurj7vi t120 = t119;
                uint8_t t121 = ((((uint8_t *)&t120))[0] != 0);
                F.ret = t119;
                goto gm_fret;
            }
            gm_l12 = t114;
        }
        {
            gm_l13 = ((unsigned __int128)(((unsigned __int128)0u << 64) | 0u));
        }
        {
            {
                void * t123 = gm_l11;
                gm_b16_8 t124 = GM_LD(gm_b16_8, (((uint8_t *)t123) + 0));
                gm_b16_8 t125 = t124;
                gm_b16_8 t126 = GM_LD(gm_b16_8, ((uint8_t *)&t125));
                gm_l14 = t126;
            }
            {
                gm_l15 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t128 = gm_l15;
                    uint64_t t129 = GM_LD(uintptr_t, ((uint8_t *)&gm_l14) + 8);
                    uint8_t t130 = (t128 < t129);
                    if (!t130) goto lb127;
                    {
                        gm_thf8as07kj3uj t133;
                        memset((uint8_t *)&t133 + 8, 0, 8);
                        uint64_t t134 = gm_l15;
                        GM_ST(uint64_t, (uint8_t *)&t133 + 0, t134);
                        uint64_t t135 = gm_l15;
                        uint64_t t136 = (uintptr_t)t135;
                        uint64_t t137 = GM_LD(uintptr_t, ((uint8_t *)&gm_l14) + 8);
                        memcpy((uint8_t *)&t133 + 16, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l14)) + t136 * 48), 48);
                        gm_thf8as07kj3uj t138 = t133;
                        uint64_t t141 = GM_LD(uint64_t, (((uint8_t *)&t138) + 0));
                        gm_l16 = t141;
                        gm_t6evimt84lndq t142 = GM_LD(gm_t6evimt84lndq, (((uint8_t *)&t138) + 16));
                        F.l17 = t142;
                        {
                            uint64_t t144 = gm_l16;
                            uint64_t t145 = (uintptr_t)t144;
                            uint64_t t146 = GM_LD(uintptr_t, (((uint8_t *)&gm_l12) + 8) + 8);
                            if (t145 >= (uintptr_t)(t146)) gm_trap_index(t145, t146, gm_srcuro7l7pd15c2, 335, 22);
                            gm_tov4mtrfp5e7v t147 = GM_LD(gm_tov4mtrfp5e7v, ((uint8_t *)GM_LD(void *, (((uint8_t *)&gm_l12) + 8)) + t145 * 12));
                            gm_l18 = t147;
                        }
                        {
                            unsigned __int128 t148 = gm_l13;
                            void * t149 = gm_l0;
                            gm_t88rcucg1b7uk t150 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l18) + 8));
                            gm_t3d480iurj7vi t151;
                            gm_f_bits_of_ilgn83m((void *)&t151, (void *)NULL, cx, NULL, t149, &(*(gm_t6evimt84lndq *)((uint8_t *)&F.l17)), t150);
                            if (gm_unw.kind) {
                                goto gm_ll131;
                            }
                            gm_t3d480iurj7vi t152 = t151;
                            if (((((uint8_t *)&t152))[0] != 0)) {
                                gm_t08olq97rscn3 t153 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&t152) + 16));
                                gm_t3d480iurj7vi t154;
                                GM_ST(uint8_t, (uint8_t *)&t154 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t154 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t154 + 4, 0);
                                GM_ST(uint64_t, (uint8_t *)&t154 + 8, 0);
                                ((uint8_t *)&t154)[0] = 1;
                                GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t154 + 16), t153);
                                F.ret = t154;
                                goto gm_fret;
                            }
                            unsigned __int128 t155 = GM_LD(unsigned __int128, (((uint8_t *)&t152) + 16));
                            uint32_t t156 = GM_LD(uint32_t, (((uint8_t *)&gm_l18) + 0));
                            unsigned __int128 t157 = (unsigned __int128)t156;
                            unsigned __int128 n158 = (unsigned __int128)t157;
                            if (n158 >= 128) gm_trap_shift((uint64_t)n158, "u128", gm_srcuro7l7pd15c2, 336, 24);
                            unsigned __int128 r159 = (unsigned __int128)((unsigned __int128)t155 << (int)n158);
                            bool o160 = (unsigned __int128)(r159 >> (int)n158) != t155;
                            if (o160) gm_trap("integer overflow in `<<`", gm_srcuro7l7pd15c2, 336, 24);
                            unsigned __int128 t161 = r159;
                            unsigned __int128 t162 = (unsigned __int128)(t148 | t161);
                            gm_l13 = t162;
                        }
                        goto me139;
mn140:;
                        gm_trap("no match arm matched the scrutinee", gm_srcuro7l7pd15c2, 334, 13);
me139:;
                    }
lc127:;
                    {
                        uint64_t t164 = gm_l15;
                        uint64_t r165; bool o166 = __builtin_add_overflow(t164, ((uint64_t)1ULL), &r165);
                        uint64_t t167 = r165;
                        gm_l15 = t167;
                    }
                }
gm_ll131:;
                if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb127; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc127; }
                }
                goto gm_lad;
lb127:;
            }
        }
        {
            unsigned __int128 t168 = gm_l13;
            gm_t3d480iurj7vi t169;
            GM_ST(uint8_t, (uint8_t *)&t169 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t169 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t169 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t169 + 8, 0);
            GM_ST(uint64_t, (uint8_t *)&t169 + 32, 0);
            GM_ST(uint64_t, (uint8_t *)&t169 + 40, 0);
            ((uint8_t *)&t169)[0] = 0;
            GM_ST(unsigned __int128, ((uint8_t *)&t169 + 16), t168);
            gm_t3d480iurj7vi t170 = t169;
            uint8_t t171 = ((((uint8_t *)&t170))[0] != 0);
            F.ret = t169;
            goto gm_fret;
        }
        goto me2;
mn105:;
ma7:;
        gm_tl874t2atnlh3 t173 = GM_LD(gm_tl874t2atnlh3, gm_cejn1dj0054);
        gm_b16_8 t174;
        GM_ST(void *, (uint8_t *)&t174, (void *)(gm_sfali8nl85hc9));
        GM_ST(uintptr_t, (uint8_t *)&t174 + 8, (uintptr_t)(24));
        gm_t08olq97rscn3 t175;
        gm_f_unsupported_531q51r((void *)&t175, (void *)NULL, cx, NULL, t173, t174);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t3d480iurj7vi t176;
        GM_ST(uint8_t, (uint8_t *)&t176 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t176 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t176 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t176 + 8, 0);
        ((uint8_t *)&t176)[0] = 1;
        GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t176 + 16), t175);
        gm_t3d480iurj7vi t177 = t176;
        uint8_t t178 = ((((uint8_t *)&t177))[0] != 0);
        F.ret = t176;
        goto gm_fret;
        goto me2;
mn172:;
ma8:;
        gm_trap("no match arm matched the scrutinee", gm_srcuro7l7pd15c2, 319, 5);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_cell_storage_1rtp280(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3) {
    struct gm_fr_cell_storage_1rtp280 F;
    void * gm_l0; /* g */
    void * gm_l1; /* cell */
    gm_t88rcucg1b7uk gm_l2; /* t */
    gm_b16_8 gm_l3; /* key */
    uint64_t gm_l4; /* addr */
    uint32_t gm_l5; /* i */
    uint32_t gm_l6; /* i */
    gm_b16_8 gm_l7; /* name */
    GM_STACK_CHECK(&F, 426u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        void * t2 = gm_l1;
        uint64_t t3 = (uintptr_t)t2;
        gm_l4 = t3;
    }
    {
        void * t5 = gm_l0;
        void * t6 = (void *)(((uint8_t *)t5) + 3528);
        uint64_t t7 = gm_l4;
        gm_t9805qtonbsag t8 = gm_f_get_5ktivom(t6, t7);
        gm_t9805qtonbsag t9 = t8;
        if (!((((uint8_t *)&t9))[4] != 0)) goto ie4;
        uint32_t t10 = GM_LD(uint32_t, ((uint8_t *)&t9));
        gm_l5 = t10;
        {
            uint32_t t12 = gm_l5;
            uint64_t t13 = (uint64_t)t12;
            uint64_t t14 = (uintptr_t)t13;
            void * t15 = gm_l0;
            uint64_t t16 = GM_LD(uintptr_t, ((((uint8_t *)t15) + 3488) + 0) + 8);
            if (t14 >= (uintptr_t)(t16)) gm_trap_index(t14, t16, gm_srcuro7l7pd15c2, 41, 43);
            gm_b16_8 t17 = GM_LD(gm_b16_8, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t15) + 3488) + 0)) + t14 * 32) + 16));
            uint8_t t18 = 0;
            F.ret = t17;
            goto gm_fret;
        }
        goto ix4;
ie4:;
ix4:;
    }
    {
        void * t19 = gm_l0;
        uint64_t t20 = GM_LD(uintptr_t, ((((uint8_t *)t19) + 3488) + 0) + 8);
        uint32_t t21 = (uint32_t)t20;
        gm_l6 = t21;
    }
    {
        void * t22 = gm_l0;
        void * t23 = (void *)(((uint8_t *)t22) + 1272);
        gm_b16_8 t24;
        GM_ST(void *, (uint8_t *)&t24, (void *)(gm_sbe61ep7tock5));
        GM_ST(uintptr_t, (uint8_t *)&t24 + 8, (uintptr_t)(4));
        gm_b16_8 t25 = gm_l3;
        gm_b16_8 t26 = gm_f_keyed_h72lrop((void *)NULL, cx, NULL, t23, t24, t25);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l7 = t26;
    }
    {
        void * t27 = gm_l0;
        void * t28 = (void *)(((uint8_t *)t27) + 3488);
        gm_to3r6jqo24gs1 t29;
        memset((uint8_t *)&t29 + 12, 0, 4);
        void * t30 = gm_l1;
        GM_ST(void *, (uint8_t *)&t29 + 0, t30);
        gm_t88rcucg1b7uk t31 = gm_l2;
        GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t29 + 8, t31);
        gm_b16_8 t32 = gm_l7;
        GM_ST(gm_b16_8, (uint8_t *)&t29 + 16, t32);
        gm_tftb1odc739t5 t33 = gm_f_push_dffnss0(t28, &t29);
        gm_tftb1odc739t5 t34 = t33;
        if (((((uint8_t *)&t34))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t34) + 1, gm_srcuro7l7pd15c2, 44, 5);
        gm_b0_1 t35 = GM_LD(gm_b0_1, (((uint8_t *)&t34) + 1));
    }
    {
        void * t36 = gm_l0;
        void * t37 = (void *)(((uint8_t *)t36) + 3528);
        uint64_t t38 = gm_l4;
        uint32_t t39 = gm_l6;
        gm_tftb1odc739t5 t40 = gm_f_put_uldb5pe(t37, t38, t39);
        gm_tftb1odc739t5 t41 = t40;
        if (((((uint8_t *)&t41))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t41) + 1, gm_srcuro7l7pd15c2, 45, 5);
        gm_b0_1 t42 = GM_LD(gm_b0_1, (((uint8_t *)&t41) + 1));
    }
    {
        gm_b16_8 t43 = gm_l7;
        uint8_t t44 = 0;
        F.ret = t43;
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
inline __attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_cell_type_0jgf3ue(void * a0, void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_cell_type_0jgf3ue F;
    void * gm_l0; /* g */
    void * gm_l1; /* cell */
    uint32_t gm_l2; /* i */
    GM_STACK_CHECK(&F, 182u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        void * t3 = (void *)(((uint8_t *)t2) + 3528);
        void * t4 = gm_l1;
        uint64_t t5 = (uintptr_t)t4;
        gm_t9805qtonbsag t6 = gm_f_get_5ktivom(t3, t5);
        gm_t9805qtonbsag t7 = t6;
        uint32_t t8;
        if (((((uint8_t *)&t7))[4] != 0)) {
            uint32_t t9 = GM_LD(uint32_t, (((uint8_t *)&t7) + 0));
            t8 = t9;
        } else {
            gm_tkn42u7na3t81 t10;
            memset(&t10, 0, sizeof t10);
            uint8_t t11 = 0;
            F.ret = t10;
            goto gm_fret;
        }
        gm_l2 = t8;
    }
    {
        void * t12 = gm_l0;
        gm_b16_8 t13 = GM_LD(gm_b16_8, ((((uint8_t *)t12) + 3488) + 0));
        gm_b16_8 t14 = t13;
        uint32_t t15 = gm_l2;
        uint64_t t16 = (uint64_t)t15;
        uint64_t t17 = (uintptr_t)t16;
        gm_b16_8 t18 = t14;
        uint64_t t19 = GM_LD(uintptr_t, ((uint8_t *)&t18) + 8);
        if (t17 >= (uintptr_t)(t19)) gm_trap_index(t17, t19, gm_srcuro7l7pd15c2, 305, 12);
        gm_t88rcucg1b7uk t20 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, ((uint8_t *)&t18)) + t17 * 32) + 8));
        gm_tkn42u7na3t81 t21;
        GM_ST(uint8_t, (uint8_t *)&t21 + 5, 0);
        GM_ST(uint16_t, (uint8_t *)&t21 + 6, 0);
        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t21), t20);
        ((uint8_t *)&t21)[4] = 1;
        uint8_t t22 = 0;
        F.ret = t21;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_const_storage_0d7nj5d(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t88rcucg1b7uk a2, gm_tl874t2atnlh3 a3) {
    struct gm_fr_const_storage_0d7nj5d F;
    void * gm_l0; /* g */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_t88rcucg1b7uk gm_l2; /* t */
    gm_tl874t2atnlh3 gm_l3; /* at */
    void * gm_l4; /* s */
    uint32_t gm_l5; /* id */
    void * gm_l6; /* def */
    void * gm_l7; /* cell */
    GM_STACK_CHECK(&F, 895u);
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
        void * t4 = GM_LD(void *, (((uint8_t *)t3) + 0));
        gm_l4 = t4;
    }
    {
        void * t5 = gm_l4;
        gm_tl874t2atnlh3 t6 = gm_l1;
        gm_t88rcucg1b7uk t7 = gm_l2;
        gm_t9805qtonbsag t8 = gm_f_const_id_2mlh0og(t5, GM_CONV(gm_cseahvg8v7p0vs, t6), GM_CONV(gm_csguankko4lmhd, t7));
        gm_t9805qtonbsag t9 = t8;
        uint32_t t10;
        if (((((uint8_t *)&t9))[4] != 0)) {
            uint32_t t11 = GM_LD(uint32_t, (((uint8_t *)&t9) + 0));
            t10 = t11;
        } else {
            gm_tl874t2atnlh3 t12 = gm_l3;
            gm_b16_8 t13;
            GM_ST(void *, (uint8_t *)&t13, (void *)(gm_s0ggo8ek7kfrt));
            GM_ST(uintptr_t, (uint8_t *)&t13 + 8, (uintptr_t)(33));
            gm_t08olq97rscn3 t14;
            gm_f_unsupported_531q51r((void *)&t14, (void *)NULL, cx, NULL, t12, t13);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t5o0tpv5jka2l t15;
            GM_ST(uint8_t, (uint8_t *)&t15 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t15 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t15 + 4, 0);
            ((uint8_t *)&t15)[0] = 1;
            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t15 + 8), t14);
            gm_t5o0tpv5jka2l t16 = t15;
            uint8_t t17 = ((((uint8_t *)&t16))[0] != 0);
            F.ret = t15;
            goto gm_fret;
        }
        gm_l5 = t10;
    }
    {
        void * t18 = gm_l4;
        gm_b16_8 t19 = GM_LD(gm_b16_8, ((((uint8_t *)t18) + 1968) + 0));
        gm_b16_8 t20 = t19;
        uint32_t t21 = gm_l5;
        uint64_t t22 = (uint64_t)t21;
        uint64_t t23 = (uintptr_t)t22;
        gm_b16_8 t24 = t20;
        uint64_t t25 = GM_LD(uintptr_t, ((uint8_t *)&t24) + 8);
        if (t23 >= (uintptr_t)(t25)) gm_trap_index(t23, t25, gm_srcuro7l7pd15c2, 33, 13);
        void * t26 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&t24)) + t23 * 88);
        gm_l6 = t26;
    }
    {
        void * t27 = gm_l6;
        void * t28 = GM_LD(void *, (((uint8_t *)t27) + 80));
        void * t29 = t28;
        void * t30;
        if ((GM_LD(void *, ((uint8_t *)&t29)) != 0)) {
            void * t31 = GM_LD(void *, (((uint8_t *)&t29) + 0));
            t30 = t31;
        } else {
            gm_tl874t2atnlh3 t32 = gm_l3;
            gm_b16_8 t33;
            GM_ST(void *, (uint8_t *)&t33, (void *)(gm_s3js9g3l46oiv));
            GM_ST(uintptr_t, (uint8_t *)&t33 + 8, (uintptr_t)(39));
            gm_t08olq97rscn3 t34;
            gm_f_unsupported_531q51r((void *)&t34, (void *)NULL, cx, NULL, t32, t33);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t5o0tpv5jka2l t35;
            GM_ST(uint8_t, (uint8_t *)&t35 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t35 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t35 + 4, 0);
            ((uint8_t *)&t35)[0] = 1;
            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t35 + 8), t34);
            gm_t5o0tpv5jka2l t36 = t35;
            uint8_t t37 = ((((uint8_t *)&t36))[0] != 0);
            F.ret = t35;
            goto gm_fret;
        }
        gm_l7 = t30;
    }
    {
        void * t38 = gm_l0;
        void * t39 = gm_l7;
        gm_t88rcucg1b7uk t40 = gm_l2;
        gm_tfeab279c16rr t41;
        void * t42 = gm_l0;
        void * t43 = gm_l6;
        uint64_t t44 = GM_LD(uint64_t, (((uint8_t *)t43) + 32));
        gm_b16_8 t45 = gm_f_module_key_m4hm4f5((void *)NULL, cx, NULL, t42, t44);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_b16_8, (uint8_t *)&t41 + 0, t45);
        void * t46 = gm_l6;
        gm_b16_8 t47 = GM_LD(gm_b16_8, (((uint8_t *)t46) + 0));
        GM_ST(gm_b16_8, (uint8_t *)&t41 + 16, t47);
        void * t48 = gm_l0;
        void * t49 = (void *)(((uint8_t *)t48) + 8);
        gm_t88rcucg1b7uk t50 = gm_l2;
        gm_b16_8 t51 = gm_f_show_5qbi623((void *)NULL, cx, NULL, t49, t50);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_b16_8, (uint8_t *)&t41 + 32, t51);
        gm_tabb710d9mq2e t52;
        gm_f_format_17eulka((void *)&t52, (void *)NULL, cx, NULL, &t41);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t53 = t52;
        if (((((uint8_t *)&t53))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t53) + 8, gm_srcuro7l7pd15c2, 35, 36);
        gm_b16_8 t54 = GM_LD(gm_b16_8, (((uint8_t *)&t53) + 8));
        gm_b16_8 t55 = t54;
        gm_b16_8 t56 = GM_LD(gm_b16_8, ((uint8_t *)&t55));
        gm_b16_8 t57 = gm_f_cell_storage_1rtp280((void *)NULL, cx, NULL, t38, t39, t40, t56);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t5o0tpv5jka2l t58;
        GM_ST(uint8_t, (uint8_t *)&t58 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t58 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t58 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t58 + 24, 0);
        GM_ST(uint64_t, (uint8_t *)&t58 + 32, 0);
        ((uint8_t *)&t58)[0] = 0;
        GM_ST(gm_b16_8, ((uint8_t *)&t58 + 8), t57);
        gm_t5o0tpv5jka2l t59 = t58;
        uint8_t t60 = ((((uint8_t *)&t59))[0] != 0);
        F.ret = t58;
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
__attribute__((aligned(4))) void gm_f_gen_consts_6ibghrc(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_gen_consts_6ibghrc F;
    void * gm_l0; /* g */
    uint64_t gm_l3; /* i */
    gm_to3r6jqo24gs1 gm_l4; /* sc */
    gm_b16_8 gm_l5; /* name */
    uint64_t gm_l6; /* size */
    uint64_t gm_l7; /* al */
    uint64_t gm_l9; /* <tmp> */
    uint64_t gm_l10; /* <tmp> */
    gm_b16_8 gm_l12; /* sb */
    gm_b16_8 gm_l13; /* <tmp> */
    uint64_t gm_l14; /* <tmp> */
    gm_tob1snuii5bme gm_l15; /* r */
    gm_b16_8 gm_l16; /* <tmp> */
    uint64_t gm_l17; /* <tmp> */
    uint64_t gm_l18; /* gi */
    gm_b16_8 gm_l20; /* t */
    uint64_t gm_l21; /* size */
    uint64_t gm_l22; /* al */
    gm_b16_8 gm_l23; /* link */
    gm_b16_8 gm_l24; /* tls */
    void * gm_l26; /* target */
    void * gm_l27; /* cell */
    uint64_t gm_l29; /* <tmp> */
    uint64_t gm_l30; /* <tmp> */
    gm_b16_8 gm_l32; /* sb */
    gm_b16_8 gm_l33; /* <tmp> */
    uint64_t gm_l34; /* <tmp> */
    gm_tob1snuii5bme gm_l35; /* r */
    gm_to3r6jqo24gs1 gm_l36; /* sc */
    gm_b16_8 gm_l37; /* name */
    uint64_t gm_l38; /* size */
    uint64_t gm_l40; /* <tmp> */
    uint64_t gm_l41; /* <tmp> */
    gm_b16_8 gm_l43; /* sb */
    gm_b16_8 gm_l44; /* <tmp> */
    uint64_t gm_l45; /* <tmp> */
    gm_tob1snuii5bme gm_l46; /* r */
    gm_b16_8 gm_l47; /* <tmp> */
    uint64_t gm_l48; /* <tmp> */
    gm_b16_8 gm_l50; /* name */
    gm_b16_8 gm_l51; /* sb */
    gm_b16_8 gm_l52; /* <tmp> */
    uint64_t gm_l53; /* <tmp> */
    gm_tf1bjg81u0glv gm_l54; /* r */
    gm_b16_8 gm_l55; /* target */
    GM_STACK_CHECK(&F, 6291u);
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
        gm_f_list_uvfu1k1((void *)&F.l2, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l2);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s31vj4bjbj0gj));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(35));
        gm_tftb1odc739t5 t4 = gm_f_extend_0cs85gs(t2, t3);
        gm_tftb1odc739t5 t5 = t4;
        if (((((uint8_t *)&t5))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t5) + 1, gm_srcuro7l7pd15c2, 53, 5);
        gm_b0_1 t6 = GM_LD(gm_b0_1, (((uint8_t *)&t5) + 1));
    }
    {
        gm_l3 = ((uint64_t)0ULL);
    }
    {
        for (;;) {
            if (!((uint8_t)1)) goto lb7;
            {
                for (;;) {
                    uint64_t t11 = gm_l3;
                    void * t12 = gm_l0;
                    uint64_t t13 = GM_LD(uintptr_t, ((((uint8_t *)t12) + 3488) + 0) + 8);
                    uint8_t t14 = (t11 < t13);
                    if (!t14) goto lb10;
                    {
                        uint64_t t17 = gm_l3;
                        uint64_t t18 = (uintptr_t)t17;
                        void * t19 = gm_l0;
                        uint64_t t20 = GM_LD(uintptr_t, ((((uint8_t *)t19) + 3488) + 0) + 8);
                        if (t18 >= (uintptr_t)(t20)) gm_trap_index(t18, t20, gm_srcuro7l7pd15c2, 59, 19);
                        gm_to3r6jqo24gs1 t21 = GM_LD(gm_to3r6jqo24gs1, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t19) + 3488) + 0)) + t18 * 32));
                        gm_l4 = t21;
                    }
                    {
                        gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l4) + 16));
                        gm_l5 = t22;
                    }
                    {
                        void * t23 = gm_l0;
                        gm_b16_8 t24 = gm_l5;
                        GM_ST(gm_b16_8, (((uint8_t *)t23) + 3584), t24);
                    }
                    {
                        uint64_t t25 = gm_l3;
                        uint64_t r26; bool o27 = __builtin_add_overflow(t25, ((uint64_t)1ULL), &r26);
                        if (o27) gm_trap("integer overflow in `+`", gm_srcuro7l7pd15c2, 62, 13);
                        uint64_t t28 = r26;
                        gm_l3 = t28;
                    }
                    {
                        void * t29 = gm_l0;
                        void * t30 = (void *)(((uint8_t *)t29) + 8);
                        gm_t88rcucg1b7uk t31 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l4) + 8));
                        uint64_t t32 = gm_f_size_dmhtkib((void *)NULL, cx, NULL, t30, t31);
                        if (gm_unw.kind) {
                            goto gm_ll15;
                        }
                        gm_l6 = t32;
                    }
                    {
                        void * t33 = gm_l0;
                        void * t34 = (void *)(((uint8_t *)t33) + 8);
                        gm_t88rcucg1b7uk t35 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l4) + 8));
                        uint64_t t36 = gm_f_align_uu2harl((void *)NULL, cx, NULL, t34, t35);
                        if (gm_unw.kind) {
                            goto gm_ll15;
                        }
                        uint64_t t37 = (t36 > ((uint64_t)1ULL) ? t36 : ((uint64_t)1ULL));
                        gm_l7 = t37;
                    }
                    {
                        void * t38 = (void *)((uint8_t *)&F.l1);
                        gm_tgbgti82mf879 t39;
                        gm_b16_8 t40 = gm_l5;
                        GM_ST(gm_b16_8, (uint8_t *)&t39 + 0, t40);
                        uint64_t t41 = gm_l6;
                        uint64_t t42 = (t41 > ((uint64_t)1ULL) ? t41 : ((uint64_t)1ULL));
                        GM_ST(uint64_t, (uint8_t *)&t39 + 16, t42);
                        uint64_t t43 = gm_l7;
                        GM_ST(uint64_t, (uint8_t *)&t39 + 24, t43);
                        gm_tftb1odc739t5 t44 = gm_f_write_60kg76i(t38, &t39);
                        gm_tftb1odc739t5 t45 = t44;
                        if (((((uint8_t *)&t45))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t45) + 1, gm_srcuro7l7pd15c2, 65, 13);
                        gm_b0_1 t46 = GM_LD(gm_b0_1, (((uint8_t *)&t45) + 1));
                    }
                    {
                        gm_f_list_uvfu1k1((void *)&F.l8, (void *)NULL, cx, NULL);
                        if (gm_unw.kind) {
                            goto gm_ll15;
                        }
                    }
                    {
                        {
                            gm_l10 = ((uint64_t)0ULL);
                        }
                        {
                            uint64_t t48 = gm_l6;
                            gm_l9 = t48;
                        }
                        {
                            for (;;) {
                                uint64_t t50 = gm_l10;
                                uint64_t t51 = gm_l9;
                                uint8_t t52 = (t50 < t51);
                                if (!t52) goto lb49;
                                {
                                    uint64_t t55 = gm_l10;
                                    uint64_t t56 = t55;
                                    {
                                        void * t60 = (void *)((uint8_t *)&F.l8);
                                        gm_tftb1odc739t5 t61 = gm_f_push_t83rg2m(t60, ((uint8_t)0ULL));
                                        gm_tftb1odc739t5 t62 = t61;
                                        if (((((uint8_t *)&t62))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t62) + 1, gm_srcuro7l7pd15c2, 67, 32);
                                        gm_b0_1 t63 = GM_LD(gm_b0_1, (((uint8_t *)&t62) + 1));
                                    }
                                    goto me57;
mn58:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcuro7l7pd15c2, 67, 13);
me57:;
                                }
lc49:;
                                {
                                    uint64_t t65 = gm_l10;
                                    uint64_t r66; bool o67 = __builtin_add_overflow(t65, ((uint64_t)1ULL), &r66);
                                    uint64_t t68 = r66;
                                    gm_l10 = t68;
                                }
                            }
lb49:;
                        }
                    }
                    {
                        gm_f_list_opmir2c((void *)&F.l11, (void *)NULL, cx, NULL);
                        if (gm_unw.kind) {
                            goto gm_ll15;
                        }
                    }
                    {
                        void * t69 = gm_l0;
                        void * t70 = GM_LD(void *, (((uint8_t *)&gm_l4) + 0));
                        gm_t88rcucg1b7uk t71 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l4) + 8));
                        gm_b16_8 t72 = GM_LD(gm_b16_8, (((uint8_t *)&F.l8) + 0));
                        void * t73 = (void *)((uint8_t *)&F.l11);
                        gm_tqgtd5gtgj65s t74;
                        gm_f_value_bytes_qm8egmj((void *)&t74, (void *)NULL, cx, NULL, t69, &(*(gm_t6evimt84lndq *)(((uint8_t *)t70) + 0)), t71, t72, ((uint64_t)0ULL), t73);
                        if (gm_unw.kind) {
                            goto gm_ll15;
                        }
                        gm_tqgtd5gtgj65s t75 = t74;
                        if (((((uint8_t *)&t75))[0] != 0)) {
                            gm_t08olq97rscn3 t76 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&t75) + 8));
                            gm_t9t4u07j77scv t77;
                            GM_ST(uint8_t, (uint8_t *)&t77 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t77 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t77 + 4, 0);
                            ((uint8_t *)&t77)[0] = 1;
                            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t77 + 8), t76);
                            F.ret = t77;
                            goto gm_fret;
                        }
                        gm_b0_1 t78 = GM_LD(gm_b0_1, (((uint8_t *)&t75) + 8));
                    }
                    {
                        uint64_t t80 = GM_LD(uintptr_t, (((uint8_t *)&F.l8) + 0) + 8);
                        uint8_t t81 = (t80 > ((uint64_t)0ULL));
                        if (!t81) goto ie79;
                        {
                            void * t83 = gm_l0;
                            gm_b16_8 t84 = GM_LD(gm_b16_8, (((uint8_t *)&F.l8) + 0));
                            gm_b16_8 t85 = t84;
                            gm_b16_8 t86 = GM_LD(gm_b16_8, ((uint8_t *)&t85));
                            gm_b16_8 t87 = gm_f_static_bytes_u0uvco3((void *)NULL, cx, NULL, t83, t86);
                            if (gm_unw.kind) {
                                goto gm_ll15;
                            }
                            gm_l12 = t87;
                        }
                        {
                            void * t88 = (void *)((uint8_t *)&F.l2);
                            gm_tftksr4op114e t89;
                            gm_b16_8 t90 = gm_l5;
                            GM_ST(gm_b16_8, (uint8_t *)&t89 + 0, t90);
                            gm_b16_8 t91 = gm_l12;
                            GM_ST(gm_b16_8, (uint8_t *)&t89 + 16, t91);
                            uint64_t t92 = GM_LD(uintptr_t, (((uint8_t *)&F.l8) + 0) + 8);
                            GM_ST(uint64_t, (uint8_t *)&t89 + 32, t92);
                            gm_tftb1odc739t5 t93 = gm_f_write_snsbmln(t88, &t89);
                            gm_tftb1odc739t5 t94 = t93;
                            if (((((uint8_t *)&t94))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t94) + 1, gm_srcuro7l7pd15c2, 72, 17);
                            gm_b0_1 t95 = GM_LD(gm_b0_1, (((uint8_t *)&t94) + 1));
                        }
                        goto ix79;
ie79:;
ix79:;
                    }
                    {
                        {
                            gm_b16_8 t97 = GM_LD(gm_b16_8, (((uint8_t *)&F.l11) + 0));
                            gm_l13 = t97;
                        }
                        {
                            gm_l14 = ((uint64_t)0ULL);
                        }
                        {
                            for (;;) {
                                uint64_t t99 = gm_l14;
                                uint64_t t100 = GM_LD(uintptr_t, ((uint8_t *)&gm_l13) + 8);
                                uint8_t t101 = (t99 < t100);
                                if (!t101) goto lb98;
                                {
                                    uint64_t t104 = gm_l14;
                                    uint64_t t105 = (uintptr_t)t104;
                                    uint64_t t106 = GM_LD(uintptr_t, ((uint8_t *)&gm_l13) + 8);
                                    gm_tob1snuii5bme t109 = GM_LD(gm_tob1snuii5bme, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l13)) + t105 * 32));
                                    gm_l15 = t109;
                                    {
                                        void * t111 = (void *)((uint8_t *)&F.l2);
                                        gm_thddbqb6snabm t112;
                                        gm_b16_8 t113 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l15) + 8));
                                        GM_ST(gm_b16_8, (uint8_t *)&t112 + 0, t113);
                                        uint64_t t114 = GM_LD(uint64_t, (((uint8_t *)&gm_l15) + 24));
                                        GM_ST(uint64_t, (uint8_t *)&t112 + 16, t114);
                                        gm_b16_8 t115 = gm_l5;
                                        GM_ST(gm_b16_8, (uint8_t *)&t112 + 24, t115);
                                        uint64_t t116 = GM_LD(uint64_t, (((uint8_t *)&gm_l15) + 0));
                                        GM_ST(uint64_t, (uint8_t *)&t112 + 40, t116);
                                        gm_tftb1odc739t5 t117 = gm_f_write_th5g0ba(t111, &t112);
                                        gm_tftb1odc739t5 t118 = t117;
                                        if (((((uint8_t *)&t118))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t118) + 1, gm_srcuro7l7pd15c2, 75, 17);
                                        gm_b0_1 t119 = GM_LD(gm_b0_1, (((uint8_t *)&t118) + 1));
                                    }
                                    goto me107;
mn108:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcuro7l7pd15c2, 74, 13);
me107:;
                                }
lc98:;
                                {
                                    uint64_t t121 = gm_l14;
                                    uint64_t r122; bool o123 = __builtin_add_overflow(t121, ((uint64_t)1ULL), &r122);
                                    uint64_t t124 = r122;
                                    gm_l14 = t124;
                                }
                            }
lb98:;
                        }
                    }
lc10:;
                }
gm_ll15:;
                if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb10; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc10; }
                }
                goto gm_ll8;
lb10:;
            }
            {
                void * t125 = gm_l0;
                gm_tqgtd5gtgj65s t126;
                gm_f_drain_leoibma((void *)&t126, (void *)NULL, cx, NULL, t125);
                if (gm_unw.kind) {
                    goto gm_ll8;
                }
                gm_tqgtd5gtgj65s t127 = t126;
                if (((((uint8_t *)&t127))[0] != 0)) {
                    gm_t08olq97rscn3 t128 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&t127) + 8));
                    gm_t9t4u07j77scv t129;
                    GM_ST(uint8_t, (uint8_t *)&t129 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t129 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t129 + 4, 0);
                    ((uint8_t *)&t129)[0] = 1;
                    GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t129 + 8), t128);
                    F.ret = t129;
                    goto gm_fret;
                }
                gm_b0_1 t130 = GM_LD(gm_b0_1, (((uint8_t *)&t127) + 8));
            }
            {
                uint64_t t132 = gm_l3;
                void * t133 = gm_l0;
                uint64_t t134 = GM_LD(uintptr_t, ((((uint8_t *)t133) + 3488) + 0) + 8);
                uint8_t t135 = ((t132 == t134));
                if (!t135) goto ie131;
                {
                    goto lb7;
                }
                goto ix131;
ie131:;
ix131:;
            }
lc7:;
        }
gm_ll8:;
        if (gm_unw.frame == &F.h && gm_unw.label == 0) {
            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb7; }
            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc7; }
        }
        goto gm_lad;
lb7:;
    }
    {
        {
            void * t138 = gm_l0;
            void * t139 = GM_LD(void *, (((uint8_t *)t138) + 0));
            void * t140 = GM_LD(void *, (((uint8_t *)t139) + 0));
            gm_b16_8 t141 = GM_LD(gm_b16_8, ((((uint8_t *)t140) + 1448) + 0));
            gm_b16_8 t142 = t141;
            gm_b16_8 t143 = GM_LD(gm_b16_8, ((uint8_t *)&t142));
            gm_l16 = t143;
        }
        {
            gm_l17 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t145 = gm_l17;
                uint64_t t146 = GM_LD(uintptr_t, ((uint8_t *)&gm_l16) + 8);
                uint8_t t147 = (t145 < t146);
                if (!t147) goto lb144;
                {
                    gm_t964gsbl2irde t150;
                    uint64_t t151 = gm_l17;
                    GM_ST(uint64_t, (uint8_t *)&t150 + 0, t151);
                    uint64_t t152 = gm_l17;
                    uint64_t t153 = (uintptr_t)t152;
                    uint64_t t154 = GM_LD(uintptr_t, ((uint8_t *)&gm_l16) + 8);
                    memcpy((uint8_t *)&t150 + 8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l16)) + t153 * 104), 104);
                    gm_t964gsbl2irde t155 = t150;
                    uint64_t t158 = GM_LD(uint64_t, (((uint8_t *)&t155) + 0));
                    gm_l18 = t158;
                    gm_tq7qjpkmqmc5s t159 = GM_LD(gm_tq7qjpkmqmc5s, (((uint8_t *)&t155) + 8));
                    F.l19 = t159;
                    {
                        void * t161 = gm_l0;
                        uint64_t t162 = gm_l18;
                        gm_b16_8 t163 = gm_f_global_tag_gs7s3p6((void *)NULL, cx, NULL, t161, t162);
                        if (gm_unw.kind) {
                            goto gm_ll148;
                        }
                        gm_l20 = t163;
                    }
                    {
                        void * t164 = gm_l0;
                        uint8_t *t165 = (((uint8_t *)t164) + 3584);
                        gm_b16_8 t166 = gm_l20;
                        gm_tnpifv20irnph t167;
                        GM_ST(gm_b16_8, ((uint8_t *)&t167 + 0), t166);
                        gm_tabb710d9mq2e t168;
                        gm_f_format_0o96lei((void *)&t168, (void *)NULL, cx, NULL, t167);
                        if (gm_unw.kind) {
                            goto gm_ll148;
                        }
                        gm_tabb710d9mq2e t169 = t168;
                        if (((((uint8_t *)&t169))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t169) + 8, gm_srcuro7l7pd15c2, 84, 27);
                        gm_b16_8 t170 = GM_LD(gm_b16_8, (((uint8_t *)&t169) + 8));
                        gm_b16_8 t171 = t170;
                        gm_b16_8 t172 = GM_LD(gm_b16_8, ((uint8_t *)&t171));
                        GM_ST(gm_b16_8, t165, t172);
                    }
                    {
                        void * t173 = gm_l0;
                        void * t174 = (void *)(((uint8_t *)t173) + 8);
                        gm_t88rcucg1b7uk t175 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l19) + 24));
                        uint64_t t176 = gm_f_size_dmhtkib((void *)NULL, cx, NULL, t174, t175);
                        if (gm_unw.kind) {
                            goto gm_ll148;
                        }
                        gm_l21 = t176;
                    }
                    {
                        void * t177 = gm_l0;
                        void * t178 = (void *)(((uint8_t *)t177) + 8);
                        gm_t88rcucg1b7uk t179 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l19) + 24));
                        uint64_t t180 = gm_f_align_uu2harl((void *)NULL, cx, NULL, t178, t179);
                        if (gm_unw.kind) {
                            goto gm_ll148;
                        }
                        gm_tf5srh4mp4b3i t181 = GM_LD(gm_tf5srh4mp4b3i, (((uint8_t *)&F.l19) + 80));
                        gm_tf5srh4mp4b3i t182 = t181;
                        uint64_t t183;
                        if (((((uint8_t *)&t182))[8] != 0)) {
                            uint64_t t184 = GM_LD(uint64_t, (((uint8_t *)&t182) + 0));
                            t183 = t184;
                        } else {
                            t183 = ((uint64_t)1ULL);
                        }
                        uint64_t t185 = (t180 > t183 ? t180 : t183);
                        uint64_t t186 = (t185 > ((uint64_t)1ULL) ? t185 : ((uint64_t)1ULL));
                        gm_l22 = t186;
                    }
                    {
                        gm_th2lqbo77pvsc t188 = GM_LD(gm_th2lqbo77pvsc, (((uint8_t *)&F.l19) + 56));
                        gm_b16_8 t189;
                        GM_ST(void *, (uint8_t *)&t189, (void *)(gm_scc54gv4a7k04));
                        GM_ST(uintptr_t, (uint8_t *)&t189 + 8, (uintptr_t)(5));
                        gm_th2lqbo77pvsc t190;
                        GM_ST(uint8_t, (uint8_t *)&t190 + 17, 0);
                        GM_ST(uint16_t, (uint8_t *)&t190 + 18, 0);
                        GM_ST(uint32_t, (uint8_t *)&t190 + 20, 0);
                        GM_ST(gm_b16_8, ((uint8_t *)&t190), t189);
                        ((uint8_t *)&t190)[16] = 1;
                        gm_th2lqbo77pvsc t191 = t188;
                        gm_th2lqbo77pvsc t192 = t190;
                        uint8_t t193 = (gm_eq0ogca82g1f(((uint8_t *)&t191), ((uint8_t *)&t192)));
                        if (!t193) goto ie187;
                        {
                            void * t195 = (void *)((uint8_t *)&F.l1);
                            gm_b16_8 t196 = gm_l20;
                            gm_tnpifv20irnph t197;
                            GM_ST(gm_b16_8, ((uint8_t *)&t197 + 0), t196);
                            gm_tftb1odc739t5 t198 = gm_f_write_80k6mh9(t195, t197);
                            gm_tftb1odc739t5 t199 = t198;
                            if (((((uint8_t *)&t199))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t199) + 1, gm_srcuro7l7pd15c2, 90, 13);
                            gm_b0_1 t200 = GM_LD(gm_b0_1, (((uint8_t *)&t199) + 1));
                        }
                        {
                            goto lc144;
                        }
                        goto ix187;
ie187:;
ix187:;
                    }
                    {
                        if (!(((((uint8_t *)&F.l19) + 56))[16] != 0)) goto ie201;
                        gm_b16_8 t202 = GM_LD(gm_b16_8, (((uint8_t *)&F.l19) + 56));
                        gm_l23 = t202;
                        {
                            void * t204 = (void *)((uint8_t *)&F.l1);
                            gm_t6j6smj7qoctu t205;
                            gm_b16_8 t206 = gm_l20;
                            GM_ST(gm_b16_8, (uint8_t *)&t205 + 0, t206);
                            gm_b16_8 t207 = gm_l23;
                            gm_b16_8 t208 = gm_f_c_string_n7h27b3((void *)NULL, cx, NULL, t207);
                            if (gm_unw.kind) {
                                goto gm_ll148;
                            }
                            GM_ST(gm_b16_8, (uint8_t *)&t205 + 16, t208);
                            gm_tftb1odc739t5 t209 = gm_f_write_u9o0juu(t204, &t205);
                            gm_tftb1odc739t5 t210 = t209;
                            if (((((uint8_t *)&t210))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t210) + 1, gm_srcuro7l7pd15c2, 94, 13);
                            gm_b0_1 t211 = GM_LD(gm_b0_1, (((uint8_t *)&t210) + 1));
                        }
                        {
                            goto lc144;
                        }
                        goto ix201;
ie201:;
ix201:;
                    }
                    {
                        gm_b16_8 t212;
                        uint8_t t214 = GM_LD(uint8_t, (((uint8_t *)&F.l19) + 48));
                        if (!t214) goto ie213;
                        gm_b16_8 t216;
                        GM_ST(void *, (uint8_t *)&t216, (void *)(gm_slpqve1dqssq0));
                        GM_ST(uintptr_t, (uint8_t *)&t216 + 8, (uintptr_t)(7));
                        gm_b16_8 t217 = t216;
                        t212 = t217;
                        goto ix213;
ie213:;
                        gm_b16_8 t219;
                        GM_ST(void *, (uint8_t *)&t219, (void *)(gm_sr4qbrk6tlgaa));
                        GM_ST(uintptr_t, (uint8_t *)&t219 + 8, (uintptr_t)(0));
                        gm_b16_8 t220 = t219;
                        t212 = t220;
ix213:;
                        gm_l24 = t212;
                    }
                    {
                        void * t221 = (void *)((uint8_t *)&F.l1);
                        gm_teqsi7sd08g9f t222;
                        gm_b16_8 t223 = gm_l24;
                        GM_ST(gm_b16_8, (uint8_t *)&t222 + 0, t223);
                        gm_b16_8 t224 = gm_l20;
                        GM_ST(gm_b16_8, (uint8_t *)&t222 + 16, t224);
                        uint64_t t225 = gm_l21;
                        uint64_t t226 = (t225 > ((uint64_t)1ULL) ? t225 : ((uint64_t)1ULL));
                        GM_ST(uint64_t, (uint8_t *)&t222 + 32, t226);
                        uint64_t t227 = gm_l22;
                        GM_ST(uint64_t, (uint8_t *)&t222 + 40, t227);
                        gm_tftb1odc739t5 t228 = gm_f_write_lket48d(t221, &t222);
                        gm_tftb1odc739t5 t229 = t228;
                        if (((((uint8_t *)&t229))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t229) + 1, gm_srcuro7l7pd15c2, 98, 9);
                        gm_b0_1 t230 = GM_LD(gm_b0_1, (((uint8_t *)&t229) + 1));
                    }
                    {
                        gm_f_list_uvfu1k1((void *)&F.l25, (void *)NULL, cx, NULL);
                        if (gm_unw.kind) {
                            goto gm_ll148;
                        }
                    }
                    {
                        void * t231;
                        uint8_t t233 = GM_LD(uint8_t, (((uint8_t *)&F.l19) + 48));
                        if (!t233) goto ie232;
                        void * t235 = (void *)((uint8_t *)&F.l25);
                        void * t236 = t235;
                        t231 = t236;
                        goto ix232;
ie232:;
                        void * t238 = (void *)((uint8_t *)&F.l2);
                        void * t239 = t238;
                        t231 = t239;
ix232:;
                        gm_l26 = t231;
                    }
                    {
                        void * t241 = GM_LD(void *, (((uint8_t *)&F.l19) + 96));
                        void * t242 = t241;
                        if (!(GM_LD(void *, ((uint8_t *)&t242)) != 0)) goto ie240;
                        void * t243 = GM_LD(void *, ((uint8_t *)&t242));
                        gm_l27 = t243;
                        {
                            gm_f_list_uvfu1k1((void *)&F.l28, (void *)NULL, cx, NULL);
                            if (gm_unw.kind) {
                                goto gm_ll148;
                            }
                        }
                        {
                            {
                                gm_l30 = ((uint64_t)0ULL);
                            }
                            {
                                uint64_t t246 = gm_l21;
                                gm_l29 = t246;
                            }
                            {
                                for (;;) {
                                    uint64_t t248 = gm_l30;
                                    uint64_t t249 = gm_l29;
                                    uint8_t t250 = (t248 < t249);
                                    if (!t250) goto lb247;
                                    {
                                        uint64_t t253 = gm_l30;
                                        uint64_t t254 = t253;
                                        {
                                            void * t258 = (void *)((uint8_t *)&F.l28);
                                            gm_tftb1odc739t5 t259 = gm_f_push_t83rg2m(t258, ((uint8_t)0ULL));
                                            gm_tftb1odc739t5 t260 = t259;
                                            if (((((uint8_t *)&t260))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t260) + 1, gm_srcuro7l7pd15c2, 105, 32);
                                            gm_b0_1 t261 = GM_LD(gm_b0_1, (((uint8_t *)&t260) + 1));
                                        }
                                        goto me255;
mn256:;
                                        gm_trap("no match arm matched the scrutinee", gm_srcuro7l7pd15c2, 105, 13);
me255:;
                                    }
lc247:;
                                    {
                                        uint64_t t263 = gm_l30;
                                        uint64_t r264; bool o265 = __builtin_add_overflow(t263, ((uint64_t)1ULL), &r264);
                                        uint64_t t266 = r264;
                                        gm_l30 = t266;
                                    }
                                }
lb247:;
                            }
                        }
                        {
                            gm_f_list_opmir2c((void *)&F.l31, (void *)NULL, cx, NULL);
                            if (gm_unw.kind) {
                                goto gm_ll148;
                            }
                        }
                        {
                            void * t267 = gm_l0;
                            void * t268 = gm_l27;
                            gm_t88rcucg1b7uk t269 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l19) + 24));
                            gm_b16_8 t270 = GM_LD(gm_b16_8, (((uint8_t *)&F.l28) + 0));
                            void * t271 = (void *)((uint8_t *)&F.l31);
                            gm_tqgtd5gtgj65s t272;
                            gm_f_value_bytes_qm8egmj((void *)&t272, (void *)NULL, cx, NULL, t267, &(*(gm_t6evimt84lndq *)(((uint8_t *)t268) + 0)), t269, t270, ((uint64_t)0ULL), t271);
                            if (gm_unw.kind) {
                                goto gm_ll148;
                            }
                            gm_tqgtd5gtgj65s t273 = t272;
                            if (((((uint8_t *)&t273))[0] != 0)) {
                                gm_t08olq97rscn3 t274 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&t273) + 8));
                                gm_t9t4u07j77scv t275;
                                GM_ST(uint8_t, (uint8_t *)&t275 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t275 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t275 + 4, 0);
                                ((uint8_t *)&t275)[0] = 1;
                                GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t275 + 8), t274);
                                F.ret = t275;
                                goto gm_fret;
                            }
                            gm_b0_1 t276 = GM_LD(gm_b0_1, (((uint8_t *)&t273) + 8));
                        }
                        {
                            uint64_t t278 = GM_LD(uintptr_t, (((uint8_t *)&F.l28) + 0) + 8);
                            uint8_t t279 = (t278 > ((uint64_t)0ULL));
                            if (!t279) goto ie277;
                            {
                                void * t281 = gm_l0;
                                gm_b16_8 t282 = GM_LD(gm_b16_8, (((uint8_t *)&F.l28) + 0));
                                gm_b16_8 t283 = t282;
                                gm_b16_8 t284 = GM_LD(gm_b16_8, ((uint8_t *)&t283));
                                gm_b16_8 t285 = gm_f_static_bytes_u0uvco3((void *)NULL, cx, NULL, t281, t284);
                                if (gm_unw.kind) {
                                    goto gm_ll148;
                                }
                                gm_l32 = t285;
                            }
                            {
                                void * t286 = gm_l26;
                                gm_tftksr4op114e t287;
                                gm_b16_8 t288 = gm_l20;
                                GM_ST(gm_b16_8, (uint8_t *)&t287 + 0, t288);
                                gm_b16_8 t289 = gm_l32;
                                GM_ST(gm_b16_8, (uint8_t *)&t287 + 16, t289);
                                uint64_t t290 = GM_LD(uintptr_t, (((uint8_t *)&F.l28) + 0) + 8);
                                GM_ST(uint64_t, (uint8_t *)&t287 + 32, t290);
                                gm_tftb1odc739t5 t291 = gm_f_write_dhp21l9(t286, &t287);
                                gm_tftb1odc739t5 t292 = t291;
                                if (((((uint8_t *)&t292))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t292) + 1, gm_srcuro7l7pd15c2, 110, 17);
                                gm_b0_1 t293 = GM_LD(gm_b0_1, (((uint8_t *)&t292) + 1));
                            }
                            goto ix277;
ie277:;
ix277:;
                        }
                        {
                            {
                                gm_b16_8 t295 = GM_LD(gm_b16_8, (((uint8_t *)&F.l31) + 0));
                                gm_l33 = t295;
                            }
                            {
                                gm_l34 = ((uint64_t)0ULL);
                            }
                            {
                                for (;;) {
                                    uint64_t t297 = gm_l34;
                                    uint64_t t298 = GM_LD(uintptr_t, ((uint8_t *)&gm_l33) + 8);
                                    uint8_t t299 = (t297 < t298);
                                    if (!t299) goto lb296;
                                    {
                                        uint64_t t302 = gm_l34;
                                        uint64_t t303 = (uintptr_t)t302;
                                        uint64_t t304 = GM_LD(uintptr_t, ((uint8_t *)&gm_l33) + 8);
                                        gm_tob1snuii5bme t307 = GM_LD(gm_tob1snuii5bme, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l33)) + t303 * 32));
                                        gm_l35 = t307;
                                        {
                                            void * t309 = gm_l26;
                                            gm_thddbqb6snabm t310;
                                            gm_b16_8 t311 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l35) + 8));
                                            GM_ST(gm_b16_8, (uint8_t *)&t310 + 0, t311);
                                            uint64_t t312 = GM_LD(uint64_t, (((uint8_t *)&gm_l35) + 24));
                                            GM_ST(uint64_t, (uint8_t *)&t310 + 16, t312);
                                            gm_b16_8 t313 = gm_l20;
                                            GM_ST(gm_b16_8, (uint8_t *)&t310 + 24, t313);
                                            uint64_t t314 = GM_LD(uint64_t, (((uint8_t *)&gm_l35) + 0));
                                            GM_ST(uint64_t, (uint8_t *)&t310 + 40, t314);
                                            gm_tftb1odc739t5 t315 = gm_f_write_vlrefbu(t309, &t310);
                                            gm_tftb1odc739t5 t316 = t315;
                                            if (((((uint8_t *)&t316))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t316) + 1, gm_srcuro7l7pd15c2, 113, 17);
                                            gm_b0_1 t317 = GM_LD(gm_b0_1, (((uint8_t *)&t316) + 1));
                                        }
                                        goto me305;
mn306:;
                                        gm_trap("no match arm matched the scrutinee", gm_srcuro7l7pd15c2, 112, 13);
me305:;
                                    }
lc296:;
                                    {
                                        uint64_t t319 = gm_l34;
                                        uint64_t r320; bool o321 = __builtin_add_overflow(t319, ((uint64_t)1ULL), &r320);
                                        uint64_t t322 = r320;
                                        gm_l34 = t322;
                                    }
                                }
lb296:;
                            }
                        }
                        goto ix240;
ie240:;
ix240:;
                    }
                    {
                        uint8_t t324 = GM_LD(uint8_t, (((uint8_t *)&F.l19) + 48));
                        if (!t324) goto ie323;
                        {
                            void * t326 = (void *)((uint8_t *)&F.l1);
                            gm_th2d9c7aju8hp t327;
                            gm_b16_8 t328 = gm_l20;
                            GM_ST(gm_b16_8, (uint8_t *)&t327 + 0, t328);
                            gm_b16_8 t329 = gm_l20;
                            GM_ST(gm_b16_8, (uint8_t *)&t327 + 16, t329);
                            gm_b16_8 t330 = gm_l20;
                            GM_ST(gm_b16_8, (uint8_t *)&t327 + 32, t330);
                            gm_b16_8 t331 = gm_l20;
                            GM_ST(gm_b16_8, (uint8_t *)&t327 + 48, t331);
                            gm_b16_8 t332 = gm_l20;
                            GM_ST(gm_b16_8, (uint8_t *)&t327 + 64, t332);
                            gm_b16_8 t333 = gm_l20;
                            GM_ST(gm_b16_8, (uint8_t *)&t327 + 80, t333);
                            gm_b16_8 t334 = gm_l20;
                            GM_ST(gm_b16_8, (uint8_t *)&t327 + 96, t334);
                            gm_tftb1odc739t5 t335 = gm_f_write_qbs7gku(t326, &t327);
                            gm_tftb1odc739t5 t336 = t335;
                            if (((((uint8_t *)&t336))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t336) + 1, gm_srcuro7l7pd15c2, 117, 13);
                            gm_b0_1 t337 = GM_LD(gm_b0_1, (((uint8_t *)&t336) + 1));
                        }
                        {
                            void * t338 = gm_l0;
                            void * t339 = (void *)(((uint8_t *)t338) + 1928);
                            gm_t6j6smj7qoctu t340;
                            gm_b16_8 t341 = gm_l20;
                            GM_ST(gm_b16_8, (uint8_t *)&t340 + 0, t341);
                            gm_b16_8 t342 = GM_LD(gm_b16_8, (((uint8_t *)&F.l25) + 0));
                            GM_ST(gm_b16_8, (uint8_t *)&t340 + 16, t342);
                            gm_tftb1odc739t5 t343 = gm_f_write_ufh35j8(t339, &t340);
                            gm_tftb1odc739t5 t344 = t343;
                            if (((((uint8_t *)&t344))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t344) + 1, gm_srcuro7l7pd15c2, 118, 13);
                            gm_b0_1 t345 = GM_LD(gm_b0_1, (((uint8_t *)&t344) + 1));
                        }
                        goto ix323;
ie323:;
ix323:;
                    }
                    goto me156;
mn157:;
                    gm_trap("no match arm matched the scrutinee", gm_srcuro7l7pd15c2, 82, 5);
me156:;
                }
lc144:;
                {
                    uint64_t t347 = gm_l17;
                    uint64_t r348; bool o349 = __builtin_add_overflow(t347, ((uint64_t)1ULL), &r348);
                    uint64_t t350 = r348;
                    gm_l17 = t350;
                }
            }
gm_ll148:;
            if (gm_unw.frame == &F.h && gm_unw.label == 4) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb144; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc144; }
            }
            goto gm_lad;
lb144:;
        }
    }
    {
        for (;;) {
            uint64_t t352 = gm_l3;
            void * t353 = gm_l0;
            uint64_t t354 = GM_LD(uintptr_t, ((((uint8_t *)t353) + 3488) + 0) + 8);
            uint8_t t355 = (t352 < t354);
            if (!t355) goto lb351;
            {
                uint64_t t358 = gm_l3;
                uint64_t t359 = (uintptr_t)t358;
                void * t360 = gm_l0;
                uint64_t t361 = GM_LD(uintptr_t, ((((uint8_t *)t360) + 3488) + 0) + 8);
                if (t359 >= (uintptr_t)(t361)) gm_trap_index(t359, t361, gm_srcuro7l7pd15c2, 123, 15);
                gm_to3r6jqo24gs1 t362 = GM_LD(gm_to3r6jqo24gs1, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t360) + 3488) + 0)) + t359 * 32));
                gm_l36 = t362;
            }
            {
                gm_b16_8 t363 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l36) + 16));
                gm_l37 = t363;
            }
            {
                void * t364 = gm_l0;
                gm_b16_8 t365 = gm_l37;
                GM_ST(gm_b16_8, (((uint8_t *)t364) + 3584), t365);
            }
            {
                uint64_t t366 = gm_l3;
                uint64_t r367; bool o368 = __builtin_add_overflow(t366, ((uint64_t)1ULL), &r367);
                if (o368) gm_trap("integer overflow in `+`", gm_srcuro7l7pd15c2, 126, 9);
                uint64_t t369 = r367;
                gm_l3 = t369;
            }
            {
                void * t370 = gm_l0;
                void * t371 = (void *)(((uint8_t *)t370) + 8);
                gm_t88rcucg1b7uk t372 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l36) + 8));
                uint64_t t373 = gm_f_size_dmhtkib((void *)NULL, cx, NULL, t371, t372);
                if (gm_unw.kind) {
                    goto gm_ll356;
                }
                gm_l38 = t373;
            }
            {
                void * t374 = (void *)((uint8_t *)&F.l1);
                gm_tgbgti82mf879 t375;
                gm_b16_8 t376 = gm_l37;
                GM_ST(gm_b16_8, (uint8_t *)&t375 + 0, t376);
                uint64_t t377 = gm_l38;
                uint64_t t378 = (t377 > ((uint64_t)1ULL) ? t377 : ((uint64_t)1ULL));
                GM_ST(uint64_t, (uint8_t *)&t375 + 16, t378);
                void * t379 = gm_l0;
                void * t380 = (void *)(((uint8_t *)t379) + 8);
                gm_t88rcucg1b7uk t381 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l36) + 8));
                uint64_t t382 = gm_f_align_uu2harl((void *)NULL, cx, NULL, t380, t381);
                if (gm_unw.kind) {
                    goto gm_ll356;
                }
                uint64_t t383 = (t382 > ((uint64_t)1ULL) ? t382 : ((uint64_t)1ULL));
                GM_ST(uint64_t, (uint8_t *)&t375 + 24, t383);
                gm_tftb1odc739t5 t384 = gm_f_write_60kg76i(t374, &t375);
                gm_tftb1odc739t5 t385 = t384;
                if (((((uint8_t *)&t385))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t385) + 1, gm_srcuro7l7pd15c2, 128, 9);
                gm_b0_1 t386 = GM_LD(gm_b0_1, (((uint8_t *)&t385) + 1));
            }
            {
                gm_f_list_uvfu1k1((void *)&F.l39, (void *)NULL, cx, NULL);
                if (gm_unw.kind) {
                    goto gm_ll356;
                }
            }
            {
                {
                    gm_l41 = ((uint64_t)0ULL);
                }
                {
                    uint64_t t388 = gm_l38;
                    gm_l40 = t388;
                }
                {
                    for (;;) {
                        uint64_t t390 = gm_l41;
                        uint64_t t391 = gm_l40;
                        uint8_t t392 = (t390 < t391);
                        if (!t392) goto lb389;
                        {
                            uint64_t t395 = gm_l41;
                            uint64_t t396 = t395;
                            {
                                void * t400 = (void *)((uint8_t *)&F.l39);
                                gm_tftb1odc739t5 t401 = gm_f_push_t83rg2m(t400, ((uint8_t)0ULL));
                                gm_tftb1odc739t5 t402 = t401;
                                if (((((uint8_t *)&t402))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t402) + 1, gm_srcuro7l7pd15c2, 130, 28);
                                gm_b0_1 t403 = GM_LD(gm_b0_1, (((uint8_t *)&t402) + 1));
                            }
                            goto me397;
mn398:;
                            gm_trap("no match arm matched the scrutinee", gm_srcuro7l7pd15c2, 130, 9);
me397:;
                        }
lc389:;
                        {
                            uint64_t t405 = gm_l41;
                            uint64_t r406; bool o407 = __builtin_add_overflow(t405, ((uint64_t)1ULL), &r406);
                            uint64_t t408 = r406;
                            gm_l41 = t408;
                        }
                    }
lb389:;
                }
            }
            {
                gm_f_list_opmir2c((void *)&F.l42, (void *)NULL, cx, NULL);
                if (gm_unw.kind) {
                    goto gm_ll356;
                }
            }
            {
                void * t409 = gm_l0;
                void * t410 = GM_LD(void *, (((uint8_t *)&gm_l36) + 0));
                gm_t88rcucg1b7uk t411 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l36) + 8));
                gm_b16_8 t412 = GM_LD(gm_b16_8, (((uint8_t *)&F.l39) + 0));
                void * t413 = (void *)((uint8_t *)&F.l42);
                gm_tqgtd5gtgj65s t414;
                gm_f_value_bytes_qm8egmj((void *)&t414, (void *)NULL, cx, NULL, t409, &(*(gm_t6evimt84lndq *)(((uint8_t *)t410) + 0)), t411, t412, ((uint64_t)0ULL), t413);
                if (gm_unw.kind) {
                    goto gm_ll356;
                }
                gm_tqgtd5gtgj65s t415 = t414;
                if (((((uint8_t *)&t415))[0] != 0)) {
                    gm_t08olq97rscn3 t416 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&t415) + 8));
                    gm_t9t4u07j77scv t417;
                    GM_ST(uint8_t, (uint8_t *)&t417 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t417 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t417 + 4, 0);
                    ((uint8_t *)&t417)[0] = 1;
                    GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t417 + 8), t416);
                    F.ret = t417;
                    goto gm_fret;
                }
                gm_b0_1 t418 = GM_LD(gm_b0_1, (((uint8_t *)&t415) + 8));
            }
            {
                uint64_t t420 = GM_LD(uintptr_t, (((uint8_t *)&F.l39) + 0) + 8);
                uint8_t t421 = (t420 > ((uint64_t)0ULL));
                if (!t421) goto ie419;
                {
                    void * t423 = gm_l0;
                    gm_b16_8 t424 = GM_LD(gm_b16_8, (((uint8_t *)&F.l39) + 0));
                    gm_b16_8 t425 = t424;
                    gm_b16_8 t426 = GM_LD(gm_b16_8, ((uint8_t *)&t425));
                    gm_b16_8 t427 = gm_f_static_bytes_u0uvco3((void *)NULL, cx, NULL, t423, t426);
                    if (gm_unw.kind) {
                        goto gm_ll356;
                    }
                    gm_l43 = t427;
                }
                {
                    void * t428 = (void *)((uint8_t *)&F.l2);
                    gm_tftksr4op114e t429;
                    gm_b16_8 t430 = gm_l37;
                    GM_ST(gm_b16_8, (uint8_t *)&t429 + 0, t430);
                    gm_b16_8 t431 = gm_l43;
                    GM_ST(gm_b16_8, (uint8_t *)&t429 + 16, t431);
                    uint64_t t432 = GM_LD(uintptr_t, (((uint8_t *)&F.l39) + 0) + 8);
                    GM_ST(uint64_t, (uint8_t *)&t429 + 32, t432);
                    gm_tftb1odc739t5 t433 = gm_f_write_snsbmln(t428, &t429);
                    gm_tftb1odc739t5 t434 = t433;
                    if (((((uint8_t *)&t434))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t434) + 1, gm_srcuro7l7pd15c2, 135, 13);
                    gm_b0_1 t435 = GM_LD(gm_b0_1, (((uint8_t *)&t434) + 1));
                }
                goto ix419;
ie419:;
ix419:;
            }
            {
                {
                    gm_b16_8 t437 = GM_LD(gm_b16_8, (((uint8_t *)&F.l42) + 0));
                    gm_l44 = t437;
                }
                {
                    gm_l45 = ((uint64_t)0ULL);
                }
                {
                    for (;;) {
                        uint64_t t439 = gm_l45;
                        uint64_t t440 = GM_LD(uintptr_t, ((uint8_t *)&gm_l44) + 8);
                        uint8_t t441 = (t439 < t440);
                        if (!t441) goto lb438;
                        {
                            uint64_t t444 = gm_l45;
                            uint64_t t445 = (uintptr_t)t444;
                            uint64_t t446 = GM_LD(uintptr_t, ((uint8_t *)&gm_l44) + 8);
                            gm_tob1snuii5bme t449 = GM_LD(gm_tob1snuii5bme, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l44)) + t445 * 32));
                            gm_l46 = t449;
                            {
                                void * t451 = (void *)((uint8_t *)&F.l2);
                                gm_thddbqb6snabm t452;
                                gm_b16_8 t453 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l46) + 8));
                                GM_ST(gm_b16_8, (uint8_t *)&t452 + 0, t453);
                                uint64_t t454 = GM_LD(uint64_t, (((uint8_t *)&gm_l46) + 24));
                                GM_ST(uint64_t, (uint8_t *)&t452 + 16, t454);
                                gm_b16_8 t455 = gm_l37;
                                GM_ST(gm_b16_8, (uint8_t *)&t452 + 24, t455);
                                uint64_t t456 = GM_LD(uint64_t, (((uint8_t *)&gm_l46) + 0));
                                GM_ST(uint64_t, (uint8_t *)&t452 + 40, t456);
                                gm_tftb1odc739t5 t457 = gm_f_write_th5g0ba(t451, &t452);
                                gm_tftb1odc739t5 t458 = t457;
                                if (((((uint8_t *)&t458))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t458) + 1, gm_srcuro7l7pd15c2, 138, 13);
                                gm_b0_1 t459 = GM_LD(gm_b0_1, (((uint8_t *)&t458) + 1));
                            }
                            goto me447;
mn448:;
                            gm_trap("no match arm matched the scrutinee", gm_srcuro7l7pd15c2, 137, 9);
me447:;
                        }
lc438:;
                        {
                            uint64_t t461 = gm_l45;
                            uint64_t r462; bool o463 = __builtin_add_overflow(t461, ((uint64_t)1ULL), &r462);
                            uint64_t t464 = r462;
                            gm_l45 = t464;
                        }
                    }
lb438:;
                }
            }
lc351:;
        }
gm_ll356:;
        if (gm_unw.frame == &F.h && gm_unw.label == 7) {
            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb351; }
            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc351; }
        }
        goto gm_lad;
lb351:;
    }
    {
        void * t465 = gm_l0;
        gm_tqgtd5gtgj65s t466;
        gm_f_drain_leoibma((void *)&t466, (void *)NULL, cx, NULL, t465);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tqgtd5gtgj65s t467 = t466;
        if (((((uint8_t *)&t467))[0] != 0)) {
            gm_t08olq97rscn3 t468 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&t467) + 8));
            gm_t9t4u07j77scv t469;
            GM_ST(uint8_t, (uint8_t *)&t469 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t469 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t469 + 4, 0);
            ((uint8_t *)&t469)[0] = 1;
            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t469 + 8), t468);
            F.ret = t469;
            goto gm_fret;
        }
        gm_b0_1 t470 = GM_LD(gm_b0_1, (((uint8_t *)&t467) + 8));
    }
    {
        uint64_t t472 = gm_l3;
        void * t473 = gm_l0;
        uint64_t t474 = GM_LD(uintptr_t, ((((uint8_t *)t473) + 3488) + 0) + 8);
        uint8_t t475 = (!(t472 == t474));
        if (!t475) goto ie471;
        {
            gm_tl874t2atnlh3 t477 = GM_LD(gm_tl874t2atnlh3, gm_cejn1dj0054);
            gm_b16_8 t478;
            GM_ST(void *, (uint8_t *)&t478, (void *)(gm_s2uimijqjekbd));
            GM_ST(uintptr_t, (uint8_t *)&t478 + 8, (uintptr_t)(66));
            gm_t08olq97rscn3 t479;
            gm_f_unsupported_531q51r((void *)&t479, (void *)NULL, cx, NULL, t477, t478);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t9t4u07j77scv t480;
            GM_ST(uint8_t, (uint8_t *)&t480 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t480 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t480 + 4, 0);
            ((uint8_t *)&t480)[0] = 1;
            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t480 + 8), t479);
            gm_t9t4u07j77scv t481 = t480;
            uint8_t t482 = ((((uint8_t *)&t481))[0] != 0);
            F.ret = t480;
            goto gm_fret;
        }
        goto ix471;
ie471:;
ix471:;
    }
    {
        {
            void * t484 = gm_l0;
            gm_b16_8 t485 = GM_LD(gm_b16_8, ((((uint8_t *)t484) + 2552) + 0));
            gm_l47 = t485;
        }
        {
            gm_l48 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t487 = gm_l48;
                uint64_t t488 = GM_LD(uintptr_t, ((uint8_t *)&gm_l47) + 8);
                uint8_t t489 = (t487 < t488);
                if (!t489) goto lb486;
                {
                    uint64_t t492 = gm_l48;
                    uint64_t t493 = (uintptr_t)t492;
                    uint64_t t494 = GM_LD(uintptr_t, ((uint8_t *)&gm_l47) + 8);
                    gm_t4rau9abddl2u t497 = GM_LD(gm_t4rau9abddl2u, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l47)) + t493 * 104));
                    F.l49 = t497;
                    {
                        gm_b16_8 t499 = GM_LD(gm_b16_8, (((uint8_t *)&F.l49) + 88));
                        gm_l50 = t499;
                    }
                    {
                        void * t500 = (void *)((uint8_t *)&F.l1);
                        gm_tgbgti82mf879 t501;
                        gm_b16_8 t502 = gm_l50;
                        GM_ST(gm_b16_8, (uint8_t *)&t501 + 0, t502);
                        void * t503 = gm_l0;
                        void * t504 = (void *)(((uint8_t *)t503) + 8);
                        gm_t88rcucg1b7uk t505 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l49) + 0));
                        uint64_t t506 = gm_f_size_dmhtkib((void *)NULL, cx, NULL, t504, t505);
                        if (gm_unw.kind) {
                            goto gm_ll490;
                        }
                        uint64_t t507 = (t506 > ((uint64_t)1ULL) ? t506 : ((uint64_t)1ULL));
                        GM_ST(uint64_t, (uint8_t *)&t501 + 16, t507);
                        void * t508 = gm_l0;
                        void * t509 = (void *)(((uint8_t *)t508) + 8);
                        gm_t88rcucg1b7uk t510 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l49) + 0));
                        uint64_t t511 = gm_f_align_uu2harl((void *)NULL, cx, NULL, t509, t510);
                        if (gm_unw.kind) {
                            goto gm_ll490;
                        }
                        uint64_t t512 = (t511 > ((uint64_t)1ULL) ? t511 : ((uint64_t)1ULL));
                        GM_ST(uint64_t, (uint8_t *)&t501 + 24, t512);
                        gm_tftb1odc739t5 t513 = gm_f_write_60kg76i(t500, &t501);
                        gm_tftb1odc739t5 t514 = t513;
                        if (((((uint8_t *)&t514))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t514) + 1, gm_srcuro7l7pd15c2, 146, 9);
                        gm_b0_1 t515 = GM_LD(gm_b0_1, (((uint8_t *)&t514) + 1));
                    }
                    {
                        uint64_t t517 = GM_LD(uintptr_t, ((((uint8_t *)&F.l49) + 8) + 0) + 8);
                        uint8_t t518 = (t517 > ((uint64_t)0ULL));
                        if (!t518) goto ie516;
                        {
                            void * t520 = gm_l0;
                            gm_b16_8 t521 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l49) + 8) + 0));
                            gm_b16_8 t522 = t521;
                            gm_b16_8 t523 = GM_LD(gm_b16_8, ((uint8_t *)&t522));
                            gm_b16_8 t524 = gm_f_static_bytes_u0uvco3((void *)NULL, cx, NULL, t520, t523);
                            if (gm_unw.kind) {
                                goto gm_ll490;
                            }
                            gm_l51 = t524;
                        }
                        {
                            void * t525 = (void *)((uint8_t *)&F.l2);
                            gm_tftksr4op114e t526;
                            gm_b16_8 t527 = gm_l50;
                            GM_ST(gm_b16_8, (uint8_t *)&t526 + 0, t527);
                            gm_b16_8 t528 = gm_l51;
                            GM_ST(gm_b16_8, (uint8_t *)&t526 + 16, t528);
                            uint64_t t529 = GM_LD(uintptr_t, ((((uint8_t *)&F.l49) + 8) + 0) + 8);
                            GM_ST(uint64_t, (uint8_t *)&t526 + 32, t529);
                            gm_tftb1odc739t5 t530 = gm_f_write_snsbmln(t525, &t526);
                            gm_tftb1odc739t5 t531 = t530;
                            if (((((uint8_t *)&t531))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t531) + 1, gm_srcuro7l7pd15c2, 149, 13);
                            gm_b0_1 t532 = GM_LD(gm_b0_1, (((uint8_t *)&t531) + 1));
                        }
                        goto ix516;
ie516:;
ix516:;
                    }
                    {
                        {
                            gm_b16_8 t534 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l49) + 48) + 0));
                            gm_l52 = t534;
                        }
                        {
                            gm_l53 = ((uint64_t)0ULL);
                        }
                        {
                            for (;;) {
                                uint64_t t536 = gm_l53;
                                uint64_t t537 = GM_LD(uintptr_t, ((uint8_t *)&gm_l52) + 8);
                                uint8_t t538 = (t536 < t537);
                                if (!t538) goto lb535;
                                {
                                    uint64_t t541 = gm_l53;
                                    uint64_t t542 = (uintptr_t)t541;
                                    uint64_t t543 = GM_LD(uintptr_t, ((uint8_t *)&gm_l52) + 8);
                                    gm_tf1bjg81u0glv t546 = GM_LD(gm_tf1bjg81u0glv, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l52)) + t542 * 24));
                                    gm_l54 = t546;
                                    {
                                        void * t548 = gm_l0;
                                        gm_b16_8 t549 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l54) + 8));
                                        gm_b16_8 t550 = gm_f_static_bytes_u0uvco3((void *)NULL, cx, NULL, t548, t549);
                                        if (gm_unw.kind) {
                                            goto gm_ll539;
                                        }
                                        gm_l55 = t550;
                                    }
                                    {
                                        void * t551 = (void *)((uint8_t *)&F.l2);
                                        gm_tftksr4op114e t552;
                                        gm_b16_8 t553 = gm_l55;
                                        GM_ST(gm_b16_8, (uint8_t *)&t552 + 0, t553);
                                        gm_b16_8 t554 = gm_l50;
                                        GM_ST(gm_b16_8, (uint8_t *)&t552 + 16, t554);
                                        uint64_t t555 = GM_LD(uint64_t, (((uint8_t *)&gm_l54) + 0));
                                        GM_ST(uint64_t, (uint8_t *)&t552 + 32, t555);
                                        gm_tftb1odc739t5 t556 = gm_f_write_t09mk5p(t551, &t552);
                                        gm_tftb1odc739t5 t557 = t556;
                                        if (((((uint8_t *)&t557))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t557) + 1, gm_srcuro7l7pd15c2, 153, 13);
                                        gm_b0_1 t558 = GM_LD(gm_b0_1, (((uint8_t *)&t557) + 1));
                                    }
                                    goto me544;
mn545:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcuro7l7pd15c2, 151, 9);
me544:;
                                }
lc535:;
                                {
                                    uint64_t t560 = gm_l53;
                                    uint64_t r561; bool o562 = __builtin_add_overflow(t560, ((uint64_t)1ULL), &r561);
                                    uint64_t t563 = r561;
                                    gm_l53 = t563;
                                }
                            }
gm_ll539:;
                            if (gm_unw.frame == &F.h && gm_unw.label == 11) {
                                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb535; }
                                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc535; }
                            }
                            goto gm_ll490;
lb535:;
                        }
                    }
                    goto me495;
mn496:;
                    gm_trap("no match arm matched the scrutinee", gm_srcuro7l7pd15c2, 144, 5);
me495:;
                }
lc486:;
                {
                    uint64_t t565 = gm_l48;
                    uint64_t r566; bool o567 = __builtin_add_overflow(t565, ((uint64_t)1ULL), &r566);
                    uint64_t t568 = r566;
                    gm_l48 = t568;
                }
            }
gm_ll490:;
            if (gm_unw.frame == &F.h && gm_unw.label == 10) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb486; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc486; }
            }
            goto gm_lad;
lb486:;
        }
    }
    {
        void * t569 = (void *)((uint8_t *)&F.l2);
        gm_b16_8 t570;
        GM_ST(void *, (uint8_t *)&t570, (void *)(gm_sikpqtja8pqpv));
        GM_ST(uintptr_t, (uint8_t *)&t570 + 8, (uintptr_t)(2));
        gm_tftb1odc739t5 t571 = gm_f_extend_0cs85gs(t569, t570);
        gm_tftb1odc739t5 t572 = t571;
        if (((((uint8_t *)&t572))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t572) + 1, gm_srcuro7l7pd15c2, 156, 5);
        gm_b0_1 t573 = GM_LD(gm_b0_1, (((uint8_t *)&t572) + 1));
    }
    {
        gm_t6j6smj7qoctu t574;
        gm_b16_8 t575 = GM_LD(gm_b16_8, (((uint8_t *)&F.l1) + 0));
        gm_b16_8 t576 = t575;
        gm_b16_8 t577 = GM_LD(gm_b16_8, ((uint8_t *)&t576));
        GM_ST(gm_b16_8, (uint8_t *)&t574 + 0, t577);
        gm_b16_8 t578 = GM_LD(gm_b16_8, (((uint8_t *)&F.l2) + 0));
        gm_b16_8 t579 = t578;
        gm_b16_8 t580 = GM_LD(gm_b16_8, ((uint8_t *)&t579));
        GM_ST(gm_b16_8, (uint8_t *)&t574 + 16, t580);
        gm_t9t4u07j77scv t581;
        GM_ST(uint8_t, (uint8_t *)&t581 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t581 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t581 + 4, 0);
        ((uint8_t *)&t581)[0] = 0;
        GM_ST(gm_t6j6smj7qoctu, ((uint8_t *)&t581 + 8), t574);
        gm_t9t4u07j77scv t582 = t581;
        uint8_t t583 = ((((uint8_t *)&t582))[0] != 0);
        F.ret = t581;
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
__attribute__((aligned(4))) void gm_f_place_reloc_blfij5d(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_t88rcucg1b7uk a2, uint64_t a3) {
    struct gm_fr_place_reloc_blfij5d F;
    void * gm_l0; /* g */
    gm_tkaapcii86udk gm_l1; /* pl */
    gm_t88rcucg1b7uk gm_l2; /* pointee */
    uint64_t gm_l3; /* off */
    gm_t88rcucg1b7uk gm_l4; /* t */
    void * gm_l5; /* a */
    gm_t88rcucg1b7uk gm_l6; /* root */
    uint64_t gm_l7; /* addend */
    gm_t88rcucg1b7uk gm_l8; /* cur */
    gm_b16_8 gm_l9; /* <tmp> */
    uint64_t gm_l10; /* <tmp> */
    uint64_t gm_l11; /* i */
    uint64_t gm_l12; /* len */
    gm_t88rcucg1b7uk gm_l13; /* elem */
    gm_b16_8 gm_l14; /* elems */
    uint32_t gm_l15; /* id */
    GM_STACK_CHECK(&F, 1822u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        gm_t88rcucg1b7uk t2;
        void * t4 = gm_l0;
        void * t5 = GM_LD(void *, (((uint8_t *)a1) + 0));
        gm_tkn42u7na3t81 t6 = gm_f_cell_type_0jgf3ue(t4, t5);
        gm_tkn42u7na3t81 t7 = t6;
        if (!((((uint8_t *)&t7))[4] != 0)) goto ie3;
        gm_t88rcucg1b7uk t8 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&t7));
        gm_l4 = t8;
        gm_t88rcucg1b7uk t10 = gm_l4;
        gm_t88rcucg1b7uk t11 = t10;
        t2 = t11;
        goto ix3;
ie3:;
        gm_t88rcucg1b7uk t12;
        uint64_t t14 = GM_LD(uintptr_t, (((uint8_t *)a1) + 8) + 8);
        uint8_t t15 = ((t14 == ((uint64_t)0ULL)));
        if (!t15) goto ie13;
        gm_t88rcucg1b7uk t17 = gm_l2;
        gm_t88rcucg1b7uk t18 = t17;
        t12 = t18;
        goto ix13;
ie13:;
        gm_t88rcucg1b7uk t19;
        uint64_t t21 = GM_LD(uintptr_t, (((uint8_t *)a1) + 8) + 8);
        uint8_t t22 = ((t21 == ((uint64_t)1ULL)));
        if (!t22) goto ie20;
        void * t23 = GM_LD(void *, (((uint8_t *)a1) + 0));
        uint8_t t24 = GM_LD(uint8_t, ((((uint8_t *)t23) + 0) + 0));
        if (t24 != ((uint8_t)3ULL)) goto ie20;
        void * t25 = GM_LD(void *, ((((uint8_t *)t23) + 0) + 16));
        gm_l5 = t25;
        void * t27 = gm_l0;
        void * t28 = GM_LD(void *, (((uint8_t *)t27) + 0));
        void * t29 = GM_LD(void *, (((uint8_t *)t28) + 0));
        void * t30 = (void *)(((uint8_t *)t29) + 8);
        void * t31 = gm_l5;
        uint64_t t32 = GM_LD(uintptr_t, (((uint8_t *)t31) + 0) + 8);
        uint64_t t33 = (uint64_t)t32;
        gm_t88rcucg1b7uk t34 = gm_l2;
        gm_t88rcucg1b7uk t35 = gm_f_array_th8sdi3((void *)NULL, cx, NULL, t30, t33, t34);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t88rcucg1b7uk t36 = t35;
        t19 = t36;
        goto ix20;
ie20:;
        gm_tl874t2atnlh3 t38 = GM_LD(gm_tl874t2atnlh3, gm_cejn1dj0054);
        gm_b16_8 t39;
        GM_ST(void *, (uint8_t *)&t39, (void *)(gm_sqrl3kgomorsv));
        GM_ST(uintptr_t, (uint8_t *)&t39 + 8, (uintptr_t)(38));
        gm_t08olq97rscn3 t40;
        gm_f_unsupported_531q51r((void *)&t40, (void *)NULL, cx, NULL, t38, t39);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tt9fudfifskca t41;
        GM_ST(uint8_t, (uint8_t *)&t41 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t41 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t41 + 4, 0);
        ((uint8_t *)&t41)[0] = 1;
        GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t41 + 8), t40);
        gm_tt9fudfifskca t42 = t41;
        uint8_t t43 = ((((uint8_t *)&t42))[0] != 0);
        F.ret = t41;
        goto gm_fret;
        t19 = GM_ZERO(gm_t88rcucg1b7uk);
ix20:;
        t12 = t19;
ix13:;
        t2 = t12;
ix3:;
        gm_l6 = t2;
    }
    {
        gm_l7 = ((uint64_t)0ULL);
    }
    {
        gm_t88rcucg1b7uk t44 = gm_l6;
        gm_l8 = t44;
    }
    {
        {
            gm_b16_8 t46 = GM_LD(gm_b16_8, (((uint8_t *)a1) + 8));
            gm_l9 = t46;
        }
        {
            gm_l10 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t48 = gm_l10;
                uint64_t t49 = GM_LD(uintptr_t, ((uint8_t *)&gm_l9) + 8);
                uint8_t t50 = (t48 < t49);
                if (!t50) goto lb47;
                {
                    uint64_t t53 = gm_l10;
                    uint64_t t54 = (uintptr_t)t53;
                    uint64_t t55 = GM_LD(uintptr_t, ((uint8_t *)&gm_l9) + 8);
                    uint64_t t56 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l9)) + t54 * 8));
                    uint64_t t57 = t56;
                    uint64_t t60 = GM_LD(uint64_t, ((uint8_t *)&t57));
                    gm_l11 = t60;
                    {
                        void * t62 = gm_l0;
                        void * t63 = (void *)(((uint8_t *)t62) + 8);
                        gm_t88rcucg1b7uk t64 = gm_l8;
                        gm_t53at80pfdus3 t65;
                        gm_f_kind_ghvav22((void *)&t65, t63, GM_CONV(gm_csguankko4lmhd, t64));
                        gm_t53at80pfdus3 t66 = t65;
                        uint8_t t73 = GM_LD(uint8_t, (((uint8_t *)&t66) + 0));
                        switch (t73) { case ((uint8_t)0ULL): goto ma71; case ((uint8_t)1ULL): goto ma71; case ((uint8_t)2ULL): goto ma71; case ((uint8_t)3ULL): goto ma71; case ((uint8_t)4ULL): goto ma71; case ((uint8_t)5ULL): goto ma71; case ((uint8_t)6ULL): goto ma71; case ((uint8_t)7ULL): goto ma71; case ((uint8_t)8ULL): goto ma71; case ((uint8_t)9ULL): goto ma71; case ((uint8_t)10ULL): goto ma71; case ((uint8_t)11ULL): goto ma68; case ((uint8_t)12ULL): goto ma69; case ((uint8_t)13ULL): goto ma71; case ((uint8_t)14ULL): goto ma71; case ((uint8_t)15ULL): goto ma71; case ((uint8_t)16ULL): goto ma71; case ((uint8_t)17ULL): goto ma70; case ((uint8_t)18ULL): goto ma71; case ((uint8_t)19ULL): goto ma71; case ((uint8_t)20ULL): goto ma71; case ((uint8_t)21ULL): goto ma71; case ((uint8_t)22ULL): goto ma71; case ((uint8_t)23ULL): goto ma71; case ((uint8_t)24ULL): goto ma71; case ((uint8_t)25ULL): goto ma71; case ((uint8_t)26ULL): goto ma71; default: goto ma71; }
ma68:;
                        uint8_t t75 = GM_LD(uint8_t, (((uint8_t *)&t66) + 0));
                        if (t75 != ((uint8_t)11ULL)) goto mn74;
                        uint64_t t76 = GM_LD(uint64_t, ((((uint8_t *)&t66) + 8) + 0));
                        gm_l12 = t76;
                        gm_t88rcucg1b7uk t77 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t66) + 8) + 8));
                        gm_l13 = t77;
                        {
                            uint64_t t79 = gm_l7;
                            uint64_t t80 = gm_l11;
                            void * t81 = gm_l0;
                            void * t82 = (void *)(((uint8_t *)t81) + 8);
                            gm_t88rcucg1b7uk t83 = gm_l13;
                            uint64_t t84 = gm_f_size_dmhtkib((void *)NULL, cx, NULL, t82, t83);
                            if (gm_unw.kind) {
                                goto gm_ll51;
                            }
                            uint64_t r85; bool o86 = __builtin_mul_overflow(t80, t84, &r85);
                            if (o86) gm_trap("integer overflow in `*`", gm_srcuro7l7pd15c2, 284, 27);
                            uint64_t t87 = r85;
                            uint64_t r88; bool o89 = __builtin_add_overflow(t79, t87, &r88);
                            if (o89) gm_trap("integer overflow in `+`", gm_srcuro7l7pd15c2, 284, 17);
                            uint64_t t90 = r88;
                            gm_l7 = t90;
                        }
                        {
                            uint64_t t92 = gm_l11;
                            uint64_t t93 = gm_l12;
                            uint8_t t94 = (t92 < t93);
                            if (!t94) goto ie91;
                            {
                                gm_t88rcucg1b7uk t96 = gm_l13;
                                gm_l8 = t96;
                            }
                            goto ix91;
ie91:;
ix91:;
                        }
                        goto me67;
mn74:;
ma69:;
                        uint8_t t98 = GM_LD(uint8_t, (((uint8_t *)&t66) + 0));
                        if (t98 != ((uint8_t)12ULL)) goto mn97;
                        gm_b16_8 t99 = GM_LD(gm_b16_8, ((((uint8_t *)&t66) + 8) + 0));
                        gm_l14 = t99;
                        {
                            uint64_t t101 = gm_l7;
                            void * t102 = gm_l0;
                            void * t103 = (void *)(((uint8_t *)t102) + 8);
                            gm_t88rcucg1b7uk t104 = gm_l8;
                            gm_b16_8 t105 = gm_f_offsets_eh6vfle((void *)NULL, cx, NULL, t103, t104);
                            if (gm_unw.kind) {
                                goto gm_ll51;
                            }
                            gm_b16_8 t106 = t105;
                            uint64_t t107 = gm_l11;
                            uint64_t t108 = (uint64_t)t107;
                            uint64_t t109 = (uintptr_t)t108;
                            gm_b16_8 t110 = t106;
                            uint64_t t111 = GM_LD(uintptr_t, ((uint8_t *)&t110) + 8);
                            if (t109 >= (uintptr_t)(t111)) gm_trap_index(t109, t111, gm_srcuro7l7pd15c2, 288, 27);
                            uint64_t t112 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&t110)) + t109 * 8));
                            uint64_t r113; bool o114 = __builtin_add_overflow(t101, t112, &r113);
                            if (o114) gm_trap("integer overflow in `+`", gm_srcuro7l7pd15c2, 288, 17);
                            uint64_t t115 = r113;
                            gm_l7 = t115;
                        }
                        {
                            gm_b16_8 t116 = gm_l14;
                            gm_b16_8 t117 = t116;
                            uint64_t t118 = gm_l11;
                            uint64_t t119 = (uint64_t)t118;
                            uint64_t t120 = (uintptr_t)t119;
                            gm_b16_8 t121 = t117;
                            uint64_t t122 = GM_LD(uintptr_t, ((uint8_t *)&t121) + 8);
                            if (t120 >= (uintptr_t)(t122)) gm_trap_index(t120, t122, gm_srcuro7l7pd15c2, 289, 23);
                            gm_t88rcucg1b7uk t123 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&t121)) + t120 * 4));
                            gm_l8 = t123;
                        }
                        goto me67;
mn97:;
ma70:;
                        uint8_t t125 = GM_LD(uint8_t, (((uint8_t *)&t66) + 0));
                        if (t125 != ((uint8_t)17ULL)) goto mn124;
                        uint32_t t126 = GM_LD(uint32_t, ((((uint8_t *)&t66) + 8) + 0));
                        gm_l15 = t126;
                        {
                            void * t129 = gm_l0;
                            void * t130 = GM_LD(void *, (((uint8_t *)t129) + 0));
                            uint32_t t131 = gm_l15;
                            void * t132 = gm_f_adt_mpqv8qe(t130, t131);
                            uint8_t t133 = GM_LD(uint8_t, ((((uint8_t *)t132) + 24) + 0));
                            if (t133 != ((uint8_t)0ULL)) goto ie128;
                            gm_tpautr4br7h3l t134 = GM_LD(gm_tpautr4br7h3l, ((((uint8_t *)t132) + 24) + 8));
                            F.l16 = t134;
                            {
                                uint64_t t136 = gm_l7;
                                void * t137 = gm_l0;
                                void * t138 = (void *)(((uint8_t *)t137) + 8);
                                gm_t88rcucg1b7uk t139 = gm_l8;
                                gm_b16_8 t140 = gm_f_offsets_eh6vfle((void *)NULL, cx, NULL, t138, t139);
                                if (gm_unw.kind) {
                                    goto gm_ll51;
                                }
                                gm_b16_8 t141 = t140;
                                uint64_t t142 = gm_l11;
                                uint64_t t143 = (uint64_t)t142;
                                uint64_t t144 = (uintptr_t)t143;
                                gm_b16_8 t145 = t141;
                                uint64_t t146 = GM_LD(uintptr_t, ((uint8_t *)&t145) + 8);
                                if (t144 >= (uintptr_t)(t146)) gm_trap_index(t144, t146, gm_srcuro7l7pd15c2, 293, 27);
                                uint64_t t147 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&t145)) + t144 * 8));
                                uint64_t r148; bool o149 = __builtin_add_overflow(t136, t147, &r148);
                                if (o149) gm_trap("integer overflow in `+`", gm_srcuro7l7pd15c2, 293, 17);
                                uint64_t t150 = r148;
                                gm_l7 = t150;
                            }
                            {
                                gm_b16_8 t151 = GM_LD(gm_b16_8, (((uint8_t *)&F.l16) + 0));
                                gm_b16_8 t152 = t151;
                                uint64_t t153 = gm_l11;
                                uint64_t t154 = (uint64_t)t153;
                                uint64_t t155 = (uintptr_t)t154;
                                gm_b16_8 t156 = t152;
                                uint64_t t157 = GM_LD(uintptr_t, ((uint8_t *)&t156) + 8);
                                if (t155 >= (uintptr_t)(t157)) gm_trap_index(t155, t157, gm_srcuro7l7pd15c2, 294, 23);
                                gm_t88rcucg1b7uk t158 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, ((uint8_t *)&t156)) + t155 * 40) + 16));
                                gm_l8 = t158;
                            }
                            goto ix128;
ie128:;
                            {
                                gm_tl874t2atnlh3 t160 = GM_LD(gm_tl874t2atnlh3, gm_cejn1dj0054);
                                gm_b16_8 t161;
                                GM_ST(void *, (uint8_t *)&t161, (void *)(gm_sqrl3kgomorsv));
                                GM_ST(uintptr_t, (uint8_t *)&t161 + 8, (uintptr_t)(38));
                                gm_t08olq97rscn3 t162;
                                gm_f_unsupported_531q51r((void *)&t162, (void *)NULL, cx, NULL, t160, t161);
                                if (gm_unw.kind) {
                                    goto gm_ll51;
                                }
                                gm_tt9fudfifskca t163;
                                GM_ST(uint8_t, (uint8_t *)&t163 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t163 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t163 + 4, 0);
                                ((uint8_t *)&t163)[0] = 1;
                                GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t163 + 8), t162);
                                gm_tt9fudfifskca t164 = t163;
                                uint8_t t165 = ((((uint8_t *)&t164))[0] != 0);
                                F.ret = t163;
                                goto gm_fret;
                            }
ix128:;
                        }
                        goto me67;
mn124:;
ma71:;
                        gm_tl874t2atnlh3 t167 = GM_LD(gm_tl874t2atnlh3, gm_cejn1dj0054);
                        gm_b16_8 t168;
                        GM_ST(void *, (uint8_t *)&t168, (void *)(gm_sqrl3kgomorsv));
                        GM_ST(uintptr_t, (uint8_t *)&t168 + 8, (uintptr_t)(38));
                        gm_t08olq97rscn3 t169;
                        gm_f_unsupported_531q51r((void *)&t169, (void *)NULL, cx, NULL, t167, t168);
                        if (gm_unw.kind) {
                            goto gm_ll51;
                        }
                        gm_tt9fudfifskca t170;
                        GM_ST(uint8_t, (uint8_t *)&t170 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t170 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t170 + 4, 0);
                        ((uint8_t *)&t170)[0] = 1;
                        GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t170 + 8), t169);
                        gm_tt9fudfifskca t171 = t170;
                        uint8_t t172 = ((((uint8_t *)&t171))[0] != 0);
                        F.ret = t170;
                        goto gm_fret;
                        goto me67;
mn166:;
ma72:;
                        gm_trap("no match arm matched the scrutinee", gm_srcuro7l7pd15c2, 282, 9);
me67:;
                    }
                    goto me58;
mn59:;
                    gm_trap("no match arm matched the scrutinee", gm_srcuro7l7pd15c2, 281, 5);
me58:;
                }
lc47:;
                {
                    uint64_t t174 = gm_l10;
                    uint64_t r175; bool o176 = __builtin_add_overflow(t174, ((uint64_t)1ULL), &r175);
                    uint64_t t177 = r175;
                    gm_l10 = t177;
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
    }
    {
        gm_tob1snuii5bme t178;
        uint64_t t179 = gm_l3;
        GM_ST(uint64_t, (uint8_t *)&t178 + 0, t179);
        void * t180 = gm_l0;
        void * t181 = GM_LD(void *, (((uint8_t *)a1) + 0));
        gm_t88rcucg1b7uk t182 = gm_l6;
        gm_t3877pk7lv748 t183;
        void * t184 = gm_l0;
        gm_b16_8 t185 = GM_LD(gm_b16_8, (((uint8_t *)t184) + 3584));
        GM_ST(gm_b16_8, (uint8_t *)&t183 + 0, t185);
        uint64_t t186 = gm_l3;
        GM_ST(uint64_t, (uint8_t *)&t183 + 16, t186);
        gm_tabb710d9mq2e t187;
        gm_f_format_317bju7((void *)&t187, (void *)NULL, cx, NULL, &t183);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t188 = t187;
        if (((((uint8_t *)&t188))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t188) + 8, gm_srcuro7l7pd15c2, 299, 69);
        gm_b16_8 t189 = GM_LD(gm_b16_8, (((uint8_t *)&t188) + 8));
        gm_b16_8 t190 = t189;
        gm_b16_8 t191 = GM_LD(gm_b16_8, ((uint8_t *)&t190));
        gm_b16_8 t192 = gm_f_cell_storage_1rtp280((void *)NULL, cx, NULL, t180, t181, t182, t191);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_b16_8, (uint8_t *)&t178 + 8, t192);
        uint64_t t193 = gm_l7;
        GM_ST(uint64_t, (uint8_t *)&t178 + 24, t193);
        gm_tt9fudfifskca t194;
        GM_ST(uint8_t, (uint8_t *)&t194 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t194 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t194 + 4, 0);
        ((uint8_t *)&t194)[0] = 0;
        GM_ST(gm_tob1snuii5bme, ((uint8_t *)&t194 + 8), t178);
        gm_tt9fudfifskca t195 = t194;
        uint8_t t196 = ((((uint8_t *)&t195))[0] != 0);
        F.ret = t194;
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
inline __attribute__((aligned(4))) void gm_f_store_int_mfkaqk5(void * a0, gm_b16_8 a1, uint64_t a2, unsigned __int128 a3, uint64_t a4) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_store_int_mfkaqk5 F;
    void * gm_l0; /* g */
    gm_b16_8 gm_l1; /* out */
    uint64_t gm_l2; /* off */
    unsigned __int128 gm_l3; /* v */
    uint64_t gm_l4; /* n */
    uint64_t gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    uint64_t gm_l7; /* k */
    uint64_t gm_l8; /* at */
    GM_STACK_CHECK_SHALLOW(&F, 371u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    {
        {
            gm_l6 = ((uint64_t)0ULL);
        }
        {
            uint64_t t3 = gm_l4;
            gm_l5 = t3;
        }
        {
            for (;;) {
                uint64_t t5 = gm_l6;
                uint64_t t6 = gm_l5;
                uint8_t t7 = (t5 < t6);
                if (!t7) goto lb4;
                {
                    uint64_t t10 = gm_l6;
                    uint64_t t11 = t10;
                    uint64_t t14 = GM_LD(uint64_t, ((uint8_t *)&t11));
                    gm_l7 = t14;
                    {
                        uint64_t t16;
                        void * t18 = gm_l0;
                        uint8_t t19 = GM_LD(uint8_t, ((((((uint8_t *)t18) + 8) + 8) + 0) + 8));
                        if (!t19) goto ie17;
                        uint64_t t21 = gm_l7;
                        uint64_t t22 = t21;
                        t16 = t22;
                        goto ix17;
ie17:;
                        uint64_t t24 = gm_l4;
                        uint64_t r25; bool o26 = __builtin_sub_overflow(t24, ((uint64_t)1ULL), &r25);
                        if (o26) gm_trap("integer overflow in `-`", gm_srcuro7l7pd15c2, 311, 61);
                        uint64_t t27 = r25;
                        uint64_t t28 = gm_l7;
                        uint64_t r29; bool o30 = __builtin_sub_overflow(t27, t28, &r29);
                        if (o30) gm_trap("integer overflow in `-`", gm_srcuro7l7pd15c2, 311, 61);
                        uint64_t t31 = r29;
                        uint64_t t32 = t31;
                        t16 = t32;
ix17:;
                        gm_l8 = t16;
                    }
                    {
                        gm_b16_8 t33 = gm_l1;
                        gm_b16_8 t34 = t33;
                        uint64_t t35 = gm_l2;
                        uint64_t t36 = gm_l8;
                        uint64_t r37; bool o38 = __builtin_add_overflow(t35, t36, &r37);
                        if (o38) gm_trap("integer overflow in `+`", gm_srcuro7l7pd15c2, 312, 14);
                        uint64_t t39 = r37;
                        uint64_t t40 = (uint64_t)t39;
                        uint64_t t41 = (uintptr_t)t40;
                        gm_b16_8 t42 = t34;
                        uint64_t t43 = GM_LD(uintptr_t, ((uint8_t *)&t42) + 8);
                        if (t41 >= (uintptr_t)(t43)) gm_trap_index(t41, t43, gm_srcuro7l7pd15c2, 312, 9);
                        unsigned __int128 t44 = gm_l3;
                        uint64_t t45 = gm_l7;
                        uint64_t r46; bool o47 = __builtin_mul_overflow(((uint64_t)8ULL), t45, &r46);
                        if (o47) gm_trap("integer overflow in `*`", gm_srcuro7l7pd15c2, 312, 51);
                        uint64_t t48 = r46;
                        unsigned __int128 t49 = (unsigned __int128)t48;
                        unsigned __int128 n50 = (unsigned __int128)t49;
                        if (n50 >= 128) gm_trap_shift((uint64_t)n50, "u128", gm_srcuro7l7pd15c2, 312, 41);
                        unsigned __int128 t51 = (unsigned __int128)(t44 >> (int)n50);
                        uint8_t t52 = (uint8_t)t51;
                        GM_ST(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&t42)) + t41 * 1), t52);
                    }
                    goto me12;
mn13:;
                    gm_trap("no match arm matched the scrutinee", gm_srcuro7l7pd15c2, 310, 5);
me12:;
                }
lc4:;
                {
                    uint64_t t54 = gm_l6;
                    uint64_t r55; bool o56 = __builtin_add_overflow(t54, ((uint64_t)1ULL), &r55);
                    uint64_t t57 = r55;
                    gm_l6 = t57;
                }
            }
lb4:;
        }
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) void gm_f_value_bytes_qm8egmj(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3, uint64_t a4, void * a5) {
    struct gm_fr_value_bytes_qm8egmj F;
    void * gm_l0; /* g */
    gm_t88rcucg1b7uk gm_l2; /* t */
    gm_b16_8 gm_l3; /* out */
    uint64_t gm_l4; /* off */
    void * gm_l5; /* relocs */
    unsigned __int128 gm_l6; /* bits */
    uint64_t gm_l7; /* n */
    uint64_t gm_l8; /* <tmp> */
    uint64_t gm_l9; /* <tmp> */
    uint64_t gm_l10; /* k */
    uint64_t gm_l11; /* at */
    double gm_l12; /* x */
    uint8_t gm_l13; /* b */
    void * gm_l14; /* x */
    gm_t88rcucg1b7uk gm_l15; /* inner */
    void * gm_l16; /* x */
    gm_t88rcucg1b7uk gm_l17; /* ok */
    gm_b16_8 gm_l18; /* offs */
    void * gm_l19; /* x */
    gm_t88rcucg1b7uk gm_l20; /* err */
    gm_b16_8 gm_l21; /* offs */
    uint32_t gm_l22; /* index */
    void * gm_l23; /* payload */
    uint32_t gm_l24; /* id */
    gm_t88rcucg1b7uk gm_l25; /* tag_ty */
    gm_b16_8 gm_l26; /* variants */
    gm_b16_8 gm_l28; /* offs */
    __int128 gm_l29; /* tag */
    void * gm_l30; /* x */
    gm_t88rcucg1b7uk gm_l31; /* pt */
    void * gm_l32; /* a */
    gm_t88rcucg1b7uk gm_l33; /* elem */
    uint64_t gm_l34; /* esz */
    gm_b16_8 gm_l35; /* <tmp> */
    uint64_t gm_l36; /* <tmp> */
    uint64_t gm_l37; /* k */
    gm_b16_8 gm_l39; /* elems */
    gm_b16_8 gm_l40; /* offs */
    gm_b16_8 gm_l41; /* <tmp> */
    uint64_t gm_l42; /* <tmp> */
    uint64_t gm_l43; /* k */
    uint32_t gm_l45; /* id */
    gm_t88rcucg1b7uk gm_l47; /* u */
    unsigned __int128 gm_l48; /* raw */
    gm_b16_8 gm_l49; /* offs */
    gm_b16_8 gm_l50; /* <tmp> */
    uint64_t gm_l51; /* <tmp> */
    uint64_t gm_l52; /* k */
    gm_tkaapcii86udk gm_l54; /* pl */
    gm_t88rcucg1b7uk gm_l55; /* p */
    gm_t88rcucg1b7uk gm_l56; /* p */
    gm_t88rcucg1b7uk gm_l57; /* inner */
    gm_t88rcucg1b7uk gm_l58; /* p */
    gm_t88rcucg1b7uk gm_l59; /* p */
    gm_t88rcucg1b7uk gm_l60; /* pointee */
    gm_tob1snuii5bme gm_l61; /* r */
    gm_tkaapcii86udk gm_l62; /* start */
    uint64_t gm_l63; /* len */
    gm_t88rcucg1b7uk gm_l64; /* elem */
    uint64_t gm_l65; /* p */
    gm_tob1snuii5bme gm_l66; /* r */
    uint32_t gm_l67; /* f */
    GM_STACK_CHECK(&F, 9838u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&F.l1, a1, sizeof F.l1);
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_l5 = a5;
    {
        {
            uint8_t t18 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
            switch (t18) { case ((uint8_t)0ULL): goto ma4; case ((uint8_t)1ULL): goto ma5; case ((uint8_t)2ULL): goto ma6; case ((uint8_t)3ULL): goto ma12; case ((uint8_t)4ULL): goto ma13; case ((uint8_t)5ULL): goto ma14; case ((uint8_t)6ULL): goto ma7; case ((uint8_t)7ULL): goto ma8; case ((uint8_t)8ULL): goto ma9; case ((uint8_t)9ULL): goto ma10; case ((uint8_t)10ULL): goto ma11; case ((uint8_t)11ULL): goto ma15; case ((uint8_t)12ULL): goto ma16; case ((uint8_t)13ULL): goto ma16; default: goto ma17; }
ma4:;
            uint8_t t20 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
            if (t20 != ((uint8_t)0ULL)) goto mn19;
            unsigned __int128 t21 = GM_LD(unsigned __int128, (((uint8_t *)&F.l1) + 16));
            gm_l6 = t21;
            {
                void * t23 = gm_l0;
                void * t24 = (void *)(((uint8_t *)t23) + 8);
                gm_t88rcucg1b7uk t25 = gm_l2;
                uint64_t t26 = gm_f_size_dmhtkib((void *)NULL, cx, NULL, t24, t25);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l7 = t26;
            }
            {
                {
                    gm_l9 = ((uint64_t)0ULL);
                }
                {
                    uint64_t t28 = gm_l7;
                    gm_l8 = t28;
                }
                {
                    for (;;) {
                        uint64_t t30 = gm_l9;
                        uint64_t t31 = gm_l8;
                        uint8_t t32 = (t30 < t31);
                        if (!t32) goto lb29;
                        {
                            uint64_t t35 = gm_l9;
                            uint64_t t36 = t35;
                            uint64_t t39 = GM_LD(uint64_t, ((uint8_t *)&t36));
                            gm_l10 = t39;
                            {
                                uint64_t t41;
                                void * t43 = gm_l0;
                                uint8_t t44 = GM_LD(uint8_t, ((((((uint8_t *)t43) + 8) + 8) + 0) + 8));
                                if (!t44) goto ie42;
                                uint64_t t46 = gm_l10;
                                uint64_t t47 = t46;
                                t41 = t47;
                                goto ix42;
ie42:;
                                uint64_t t49 = gm_l7;
                                uint64_t r50; bool o51 = __builtin_sub_overflow(t49, ((uint64_t)1ULL), &r50);
                                if (o51) gm_trap("integer overflow in `-`", gm_srcuro7l7pd15c2, 166, 69);
                                uint64_t t52 = r50;
                                uint64_t t53 = gm_l10;
                                uint64_t r54; bool o55 = __builtin_sub_overflow(t52, t53, &r54);
                                if (o55) gm_trap("integer overflow in `-`", gm_srcuro7l7pd15c2, 166, 69);
                                uint64_t t56 = r54;
                                uint64_t t57 = t56;
                                t41 = t57;
ix42:;
                                gm_l11 = t41;
                            }
                            {
                                gm_b16_8 t58 = gm_l3;
                                gm_b16_8 t59 = t58;
                                uint64_t t60 = gm_l4;
                                uint64_t t61 = gm_l11;
                                uint64_t r62; bool o63 = __builtin_add_overflow(t60, t61, &r62);
                                if (o63) gm_trap("integer overflow in `+`", gm_srcuro7l7pd15c2, 167, 22);
                                uint64_t t64 = r62;
                                uint64_t t65 = (uint64_t)t64;
                                uint64_t t66 = (uintptr_t)t65;
                                gm_b16_8 t67 = t59;
                                uint64_t t68 = GM_LD(uintptr_t, ((uint8_t *)&t67) + 8);
                                if (t66 >= (uintptr_t)(t68)) gm_trap_index(t66, t68, gm_srcuro7l7pd15c2, 167, 17);
                                unsigned __int128 t69 = gm_l6;
                                uint64_t t70 = gm_l10;
                                uint64_t r71; bool o72 = __builtin_mul_overflow(((uint64_t)8ULL), t70, &r71);
                                if (o72) gm_trap("integer overflow in `*`", gm_srcuro7l7pd15c2, 167, 62);
                                uint64_t t73 = r71;
                                unsigned __int128 t74 = (unsigned __int128)t73;
                                unsigned __int128 n75 = (unsigned __int128)t74;
                                if (n75 >= 128) gm_trap_shift((uint64_t)n75, "u128", gm_srcuro7l7pd15c2, 167, 49);
                                unsigned __int128 t76 = (unsigned __int128)(t69 >> (int)n75);
                                uint8_t t77 = (uint8_t)t76;
                                GM_ST(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&t67)) + t66 * 1), t77);
                            }
                            goto me37;
mn38:;
                            gm_trap("no match arm matched the scrutinee", gm_srcuro7l7pd15c2, 165, 13);
me37:;
                        }
lc29:;
                        {
                            uint64_t t79 = gm_l9;
                            uint64_t r80; bool o81 = __builtin_add_overflow(t79, ((uint64_t)1ULL), &r80);
                            uint64_t t82 = r80;
                            gm_l9 = t82;
                        }
                    }
lb29:;
                }
            }
            goto me3;
mn19:;
ma5:;
            uint8_t t84 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
            if (t84 != ((uint8_t)1ULL)) goto mn83;
            double t85 = GM_LD(double, (((uint8_t *)&F.l1) + 16));
            gm_l12 = t85;
            {
                void * t87 = gm_l0;
                void * t88 = (void *)(((uint8_t *)t87) + 8);
                gm_t88rcucg1b7uk t89 = gm_l2;
                gm_t53at80pfdus3 t90;
                gm_f_kind_ghvav22((void *)&t90, t88, GM_CONV(gm_csguankko4lmhd, t89));
                gm_t53at80pfdus3 t91 = t90;
                uint8_t t97 = GM_LD(uint8_t, (((uint8_t *)&t91) + 0));
                switch (t97) { case ((uint8_t)0ULL): goto ma95; case ((uint8_t)1ULL): goto ma95; case ((uint8_t)2ULL): goto ma95; case ((uint8_t)3ULL): goto ma93; case ((uint8_t)4ULL): goto ma95; case ((uint8_t)5ULL): goto ma95; case ((uint8_t)6ULL): goto ma95; case ((uint8_t)7ULL): goto ma95; case ((uint8_t)8ULL): goto ma95; case ((uint8_t)9ULL): goto ma95; case ((uint8_t)10ULL): goto ma95; case ((uint8_t)11ULL): goto ma95; case ((uint8_t)12ULL): goto ma95; case ((uint8_t)13ULL): goto ma95; case ((uint8_t)14ULL): goto ma95; case ((uint8_t)15ULL): goto ma95; case ((uint8_t)16ULL): goto ma95; case ((uint8_t)17ULL): goto ma95; case ((uint8_t)18ULL): goto ma95; case ((uint8_t)19ULL): goto ma95; case ((uint8_t)20ULL): goto ma95; case ((uint8_t)21ULL): goto ma95; case ((uint8_t)22ULL): goto ma95; case ((uint8_t)23ULL): goto ma95; case ((uint8_t)24ULL): goto ma95; case ((uint8_t)25ULL): goto ma95; case ((uint8_t)26ULL): goto ma95; default: goto ma95; }
ma93:;
                uint8_t t99 = GM_LD(uint8_t, (((uint8_t *)&t91) + 0));
                if (t99 != ((uint8_t)3ULL)) goto mn98;
                uint16_t t100 = GM_LD(uint16_t, (((uint8_t *)&t91) + 8));
                if (!(t100 == ((uint16_t)64ULL))) goto mn98;
                void * t101 = gm_l0;
                gm_b16_8 t102 = gm_l3;
                uint64_t t103 = gm_l4;
                double t104 = gm_l12;
                double t105 = t104;
                uint64_t t106;
                memset(&t106, 0, sizeof t106); memcpy(&t106, ((uint8_t *)&t105), sizeof t106 < 8 ? sizeof t106 : 8);
                unsigned __int128 t107 = (unsigned __int128)t106;
                gm_f_store_int_mfkaqk5(t101, t102, t103, t107, ((uint64_t)8ULL));
                goto me92;
mn98:;
ma94:;
                uint8_t t109 = GM_LD(uint8_t, (((uint8_t *)&t91) + 0));
                if (t109 != ((uint8_t)3ULL)) goto mn108;
                uint16_t t110 = GM_LD(uint16_t, (((uint8_t *)&t91) + 8));
                if (!(t110 == ((uint16_t)32ULL))) goto mn108;
                void * t111 = gm_l0;
                gm_b16_8 t112 = gm_l3;
                uint64_t t113 = gm_l4;
                double t114 = gm_l12;
                float t115 = (float)((double)t114);
                float t116 = t115;
                uint32_t t117;
                memset(&t117, 0, sizeof t117); memcpy(&t117, ((uint8_t *)&t116), sizeof t117 < 4 ? sizeof t117 : 4);
                unsigned __int128 t118 = (unsigned __int128)t117;
                gm_f_store_int_mfkaqk5(t111, t112, t113, t118, ((uint64_t)4ULL));
                goto me92;
mn108:;
ma95:;
                gm_tl874t2atnlh3 t120 = GM_LD(gm_tl874t2atnlh3, gm_cejn1dj0054);
                gm_b16_8 t121;
                GM_ST(void *, (uint8_t *)&t121, (void *)(gm_skff6pclobvnu));
                GM_ST(uintptr_t, (uint8_t *)&t121 + 8, (uintptr_t)(39));
                gm_t08olq97rscn3 t122;
                gm_f_unsupported_531q51r((void *)&t122, (void *)NULL, cx, NULL, t120, t121);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tqgtd5gtgj65s t123;
                GM_ST(uint8_t, (uint8_t *)&t123 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t123 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t123 + 4, 0);
                ((uint8_t *)&t123)[0] = 1;
                GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t123 + 8), t122);
                gm_tqgtd5gtgj65s t124 = t123;
                uint8_t t125 = ((((uint8_t *)&t124))[0] != 0);
                F.ret = t123;
                goto gm_fret;
                goto me92;
mn119:;
ma96:;
                gm_trap("no match arm matched the scrutinee", gm_srcuro7l7pd15c2, 171, 13);
me92:;
            }
            goto me3;
mn83:;
ma6:;
            uint8_t t127 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
            if (t127 != ((uint8_t)2ULL)) goto mn126;
            uint8_t t128 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 16));
            gm_l13 = t128;
            {
                gm_b16_8 t130 = gm_l3;
                gm_b16_8 t131 = t130;
                uint64_t t132 = gm_l4;
                uint64_t t133 = (uint64_t)t132;
                uint64_t t134 = (uintptr_t)t133;
                gm_b16_8 t135 = t131;
                uint64_t t136 = GM_LD(uintptr_t, ((uint8_t *)&t135) + 8);
                if (t134 >= (uintptr_t)(t136)) gm_trap_index(t134, t136, gm_srcuro7l7pd15c2, 177, 23);
                uint8_t *t137 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t135)) + t134 * 1);
                uint8_t t138;
                uint8_t t140 = gm_l13;
                if (!t140) goto ie139;
                uint8_t t142 = ((uint8_t)1ULL);
                t138 = t142;
                goto ix139;
ie139:;
                uint8_t t144 = ((uint8_t)0ULL);
                t138 = t144;
ix139:;
                GM_ST(uint8_t, t137, t138);
            }
            goto me3;
mn126:;
ma7:;
            uint8_t t146 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
            if (t146 != ((uint8_t)6ULL)) goto mn145;
            goto me3;
mn145:;
ma8:;
            uint8_t t149 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
            if (t149 != ((uint8_t)7ULL)) goto mn148;
            void * t150 = GM_LD(void *, (((uint8_t *)&F.l1) + 16));
            gm_l14 = t150;
            {
                void * t153 = gm_l0;
                void * t154 = (void *)(((uint8_t *)t153) + 8);
                gm_t88rcucg1b7uk t155 = gm_l2;
                gm_t53at80pfdus3 t156;
                gm_f_kind_ghvav22((void *)&t156, t154, GM_CONV(gm_csguankko4lmhd, t155));
                gm_t53at80pfdus3 t157 = t156;
                uint8_t t158 = GM_LD(uint8_t, (((uint8_t *)&t157) + 0));
                if (t158 != ((uint8_t)13ULL)) goto ie152;
                gm_t88rcucg1b7uk t159 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t157) + 8));
                gm_l15 = t159;
                {
                    void * t161 = gm_l0;
                    void * t162 = gm_l14;
                    gm_t88rcucg1b7uk t163 = gm_l15;
                    gm_b16_8 t164 = gm_l3;
                    uint64_t t165 = gm_l4;
                    void * t166 = gm_l5;
                    gm_tqgtd5gtgj65s t167;
                    gm_f_value_bytes_qm8egmj((void *)&t167, (void *)NULL, cx, NULL, t161, &(*(gm_t6evimt84lndq *)((uint8_t *)t162)), t163, t164, t165, t166);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tqgtd5gtgj65s t168 = t167;
                    if (((((uint8_t *)&t168))[0] != 0)) {
                        gm_t08olq97rscn3 t169 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&t168) + 8));
                        gm_tqgtd5gtgj65s t170;
                        GM_ST(uint8_t, (uint8_t *)&t170 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t170 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t170 + 4, 0);
                        ((uint8_t *)&t170)[0] = 1;
                        GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t170 + 8), t169);
                        F.ret = t170;
                        goto gm_fret;
                    }
                    gm_b0_1 t171 = GM_LD(gm_b0_1, (((uint8_t *)&t168) + 8));
                }
                {
                    void * t173 = gm_l0;
                    void * t174 = GM_LD(void *, (((uint8_t *)t173) + 0));
                    void * t175 = GM_LD(void *, (((uint8_t *)t174) + 0));
                    gm_t88rcucg1b7uk t176 = gm_l15;
                    uint8_t t177 = gm_f_nullable_pointer_7qfmn6o(t175, GM_CONV(gm_csguankko4lmhd, t176));
                    uint8_t t178 = (uint8_t)!t177;
                    if (!t178) goto ie172;
                    {
                        gm_b16_8 t180 = gm_l3;
                        gm_b16_8 t181 = t180;
                        uint64_t t182 = gm_l4;
                        uint64_t t183 = (uintptr_t)((uint64_t)1ULL);
                        void * t184 = gm_l0;
                        void * t185 = (void *)(((uint8_t *)t184) + 8);
                        gm_t88rcucg1b7uk t186 = gm_l2;
                        gm_b16_8 t187 = gm_f_offsets_eh6vfle((void *)NULL, cx, NULL, t185, t186);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_b16_8 t188 = t187;
                        uint64_t t189 = GM_LD(uintptr_t, ((uint8_t *)&t188) + 8);
                        if (t183 >= (uintptr_t)(t189)) gm_trap_index(t183, t189, gm_srcuro7l7pd15c2, 182, 67);
                        uint64_t t190 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&t188)) + t183 * 8));
                        uint64_t r191; bool o192 = __builtin_add_overflow(t182, t190, &r191);
                        if (o192) gm_trap("integer overflow in `+`", gm_srcuro7l7pd15c2, 182, 61);
                        uint64_t t193 = r191;
                        uint64_t t194 = (uint64_t)t193;
                        uint64_t t195 = (uintptr_t)t194;
                        gm_b16_8 t196 = t181;
                        uint64_t t197 = GM_LD(uintptr_t, ((uint8_t *)&t196) + 8);
                        if (t195 >= (uintptr_t)(t197)) gm_trap_index(t195, t197, gm_srcuro7l7pd15c2, 182, 56);
                        GM_ST(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&t196)) + t195 * 1), ((uint8_t)1ULL));
                    }
                    goto ix172;
ie172:;
ix172:;
                }
                goto ix152;
ie152:;
                {
                    gm_tl874t2atnlh3 t199 = GM_LD(gm_tl874t2atnlh3, gm_cejn1dj0054);
                    gm_b16_8 t200;
                    GM_ST(void *, (uint8_t *)&t200, (void *)(gm_sg1uf2uigo01q));
                    GM_ST(uintptr_t, (uint8_t *)&t200 + 8, (uintptr_t)(26));
                    gm_t08olq97rscn3 t201;
                    gm_f_unsupported_531q51r((void *)&t201, (void *)NULL, cx, NULL, t199, t200);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tqgtd5gtgj65s t202;
                    GM_ST(uint8_t, (uint8_t *)&t202 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t202 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t202 + 4, 0);
                    ((uint8_t *)&t202)[0] = 1;
                    GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t202 + 8), t201);
                    gm_tqgtd5gtgj65s t203 = t202;
                    uint8_t t204 = ((((uint8_t *)&t203))[0] != 0);
                    F.ret = t202;
                    goto gm_fret;
                }
ix152:;
            }
            goto me3;
mn148:;
ma9:;
            uint8_t t206 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
            if (t206 != ((uint8_t)8ULL)) goto mn205;
            void * t207 = GM_LD(void *, (((uint8_t *)&F.l1) + 16));
            gm_l16 = t207;
            {
                void * t210 = gm_l0;
                void * t211 = (void *)(((uint8_t *)t210) + 8);
                gm_t88rcucg1b7uk t212 = gm_l2;
                gm_t53at80pfdus3 t213;
                gm_f_kind_ghvav22((void *)&t213, t211, GM_CONV(gm_csguankko4lmhd, t212));
                gm_t53at80pfdus3 t214 = t213;
                uint8_t t215 = GM_LD(uint8_t, (((uint8_t *)&t214) + 0));
                if (t215 != ((uint8_t)14ULL)) goto ie209;
                gm_t88rcucg1b7uk t216 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t214) + 8) + 4));
                gm_l17 = t216;
                {
                    void * t218 = gm_l0;
                    void * t219 = (void *)(((uint8_t *)t218) + 8);
                    gm_t88rcucg1b7uk t220 = gm_l2;
                    gm_b16_8 t221 = gm_f_offsets_eh6vfle((void *)NULL, cx, NULL, t219, t220);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l18 = t221;
                }
                {
                    void * t222 = gm_l0;
                    void * t223 = gm_l16;
                    gm_t88rcucg1b7uk t224 = gm_l17;
                    gm_b16_8 t225 = gm_l3;
                    uint64_t t226 = gm_l4;
                    uint64_t t227 = (uintptr_t)((uint64_t)1ULL);
                    uint64_t t228 = GM_LD(uintptr_t, ((uint8_t *)&gm_l18) + 8);
                    if (t227 >= (uintptr_t)(t228)) gm_trap_index(t227, t228, gm_srcuro7l7pd15c2, 187, 50);
                    uint64_t t229 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l18)) + t227 * 8));
                    uint64_t r230; bool o231 = __builtin_add_overflow(t226, t229, &r230);
                    if (o231) gm_trap("integer overflow in `+`", gm_srcuro7l7pd15c2, 187, 44);
                    uint64_t t232 = r230;
                    void * t233 = gm_l5;
                    gm_tqgtd5gtgj65s t234;
                    gm_f_value_bytes_qm8egmj((void *)&t234, (void *)NULL, cx, NULL, t222, &(*(gm_t6evimt84lndq *)((uint8_t *)t223)), t224, t225, t232, t233);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tqgtd5gtgj65s t235 = t234;
                    if (((((uint8_t *)&t235))[0] != 0)) {
                        gm_t08olq97rscn3 t236 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&t235) + 8));
                        gm_tqgtd5gtgj65s t237;
                        GM_ST(uint8_t, (uint8_t *)&t237 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t237 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t237 + 4, 0);
                        ((uint8_t *)&t237)[0] = 1;
                        GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t237 + 8), t236);
                        F.ret = t237;
                        goto gm_fret;
                    }
                    gm_b0_1 t238 = GM_LD(gm_b0_1, (((uint8_t *)&t235) + 8));
                }
                goto ix209;
ie209:;
                {
                    gm_tl874t2atnlh3 t240 = GM_LD(gm_tl874t2atnlh3, gm_cejn1dj0054);
                    gm_b16_8 t241;
                    GM_ST(void *, (uint8_t *)&t241, (void *)(gm_sb8jvk2vbau9p));
                    GM_ST(uintptr_t, (uint8_t *)&t241 + 8, (uintptr_t)(24));
                    gm_t08olq97rscn3 t242;
                    gm_f_unsupported_531q51r((void *)&t242, (void *)NULL, cx, NULL, t240, t241);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tqgtd5gtgj65s t243;
                    GM_ST(uint8_t, (uint8_t *)&t243 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t243 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t243 + 4, 0);
                    ((uint8_t *)&t243)[0] = 1;
                    GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t243 + 8), t242);
                    gm_tqgtd5gtgj65s t244 = t243;
                    uint8_t t245 = ((((uint8_t *)&t244))[0] != 0);
                    F.ret = t243;
                    goto gm_fret;
                }
ix209:;
            }
            goto me3;
mn205:;
ma10:;
            uint8_t t247 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
            if (t247 != ((uint8_t)9ULL)) goto mn246;
            void * t248 = GM_LD(void *, (((uint8_t *)&F.l1) + 16));
            gm_l19 = t248;
            {
                void * t251 = gm_l0;
                void * t252 = (void *)(((uint8_t *)t251) + 8);
                gm_t88rcucg1b7uk t253 = gm_l2;
                gm_t53at80pfdus3 t254;
                gm_f_kind_ghvav22((void *)&t254, t252, GM_CONV(gm_csguankko4lmhd, t253));
                gm_t53at80pfdus3 t255 = t254;
                uint8_t t256 = GM_LD(uint8_t, (((uint8_t *)&t255) + 0));
                if (t256 != ((uint8_t)14ULL)) goto ie250;
                gm_t88rcucg1b7uk t257 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t255) + 8) + 0));
                gm_l20 = t257;
                {
                    void * t259 = gm_l0;
                    void * t260 = (void *)(((uint8_t *)t259) + 8);
                    gm_t88rcucg1b7uk t261 = gm_l2;
                    gm_b16_8 t262 = gm_f_offsets_eh6vfle((void *)NULL, cx, NULL, t260, t261);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l21 = t262;
                }
                {
                    gm_b16_8 t263 = gm_l3;
                    gm_b16_8 t264 = t263;
                    uint64_t t265 = gm_l4;
                    uint64_t t266 = (uintptr_t)((uint64_t)0ULL);
                    uint64_t t267 = GM_LD(uintptr_t, ((uint8_t *)&gm_l21) + 8);
                    if (t266 >= (uintptr_t)(t267)) gm_trap_index(t266, t267, gm_srcuro7l7pd15c2, 192, 24);
                    uint64_t t268 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l21)) + t266 * 8));
                    uint64_t r269; bool o270 = __builtin_add_overflow(t265, t268, &r269);
                    if (o270) gm_trap("integer overflow in `+`", gm_srcuro7l7pd15c2, 192, 18);
                    uint64_t t271 = r269;
                    uint64_t t272 = (uint64_t)t271;
                    uint64_t t273 = (uintptr_t)t272;
                    gm_b16_8 t274 = t264;
                    uint64_t t275 = GM_LD(uintptr_t, ((uint8_t *)&t274) + 8);
                    if (t273 >= (uintptr_t)(t275)) gm_trap_index(t273, t275, gm_srcuro7l7pd15c2, 192, 13);
                    GM_ST(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&t274)) + t273 * 1), ((uint8_t)1ULL));
                }
                {
                    void * t276 = gm_l0;
                    void * t277 = gm_l19;
                    gm_t88rcucg1b7uk t278 = gm_l20;
                    gm_b16_8 t279 = gm_l3;
                    uint64_t t280 = gm_l4;
                    uint64_t t281 = (uintptr_t)((uint64_t)1ULL);
                    uint64_t t282 = GM_LD(uintptr_t, ((uint8_t *)&gm_l21) + 8);
                    if (t281 >= (uintptr_t)(t282)) gm_trap_index(t281, t282, gm_srcuro7l7pd15c2, 193, 51);
                    uint64_t t283 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l21)) + t281 * 8));
                    uint64_t r284; bool o285 = __builtin_add_overflow(t280, t283, &r284);
                    if (o285) gm_trap("integer overflow in `+`", gm_srcuro7l7pd15c2, 193, 45);
                    uint64_t t286 = r284;
                    void * t287 = gm_l5;
                    gm_tqgtd5gtgj65s t288;
                    gm_f_value_bytes_qm8egmj((void *)&t288, (void *)NULL, cx, NULL, t276, &(*(gm_t6evimt84lndq *)((uint8_t *)t277)), t278, t279, t286, t287);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tqgtd5gtgj65s t289 = t288;
                    if (((((uint8_t *)&t289))[0] != 0)) {
                        gm_t08olq97rscn3 t290 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&t289) + 8));
                        gm_tqgtd5gtgj65s t291;
                        GM_ST(uint8_t, (uint8_t *)&t291 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t291 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t291 + 4, 0);
                        ((uint8_t *)&t291)[0] = 1;
                        GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t291 + 8), t290);
                        F.ret = t291;
                        goto gm_fret;
                    }
                    gm_b0_1 t292 = GM_LD(gm_b0_1, (((uint8_t *)&t289) + 8));
                }
                goto ix250;
ie250:;
                {
                    gm_tl874t2atnlh3 t294 = GM_LD(gm_tl874t2atnlh3, gm_cejn1dj0054);
                    gm_b16_8 t295;
                    GM_ST(void *, (uint8_t *)&t295, (void *)(gm_sb8jvk2vbau9p));
                    GM_ST(uintptr_t, (uint8_t *)&t295 + 8, (uintptr_t)(24));
                    gm_t08olq97rscn3 t296;
                    gm_f_unsupported_531q51r((void *)&t296, (void *)NULL, cx, NULL, t294, t295);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tqgtd5gtgj65s t297;
                    GM_ST(uint8_t, (uint8_t *)&t297 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t297 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t297 + 4, 0);
                    ((uint8_t *)&t297)[0] = 1;
                    GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t297 + 8), t296);
                    gm_tqgtd5gtgj65s t298 = t297;
                    uint8_t t299 = ((((uint8_t *)&t298))[0] != 0);
                    F.ret = t297;
                    goto gm_fret;
                }
ix250:;
            }
            goto me3;
mn246:;
ma11:;
            uint8_t t301 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
            if (t301 != ((uint8_t)10ULL)) goto mn300;
            uint32_t t302 = GM_LD(uint32_t, ((((uint8_t *)&F.l1) + 16) + 0));
            gm_l22 = t302;
            void * t303 = GM_LD(void *, ((((uint8_t *)&F.l1) + 16) + 8));
            gm_l23 = t303;
            {
                void * t306 = gm_l0;
                void * t307 = (void *)(((uint8_t *)t306) + 8);
                gm_t88rcucg1b7uk t308 = gm_l2;
                gm_t53at80pfdus3 t309;
                gm_f_kind_ghvav22((void *)&t309, t307, GM_CONV(gm_csguankko4lmhd, t308));
                gm_t53at80pfdus3 t310 = t309;
                uint8_t t311 = GM_LD(uint8_t, (((uint8_t *)&t310) + 0));
                if (t311 != ((uint8_t)17ULL)) goto ie305;
                uint32_t t312 = GM_LD(uint32_t, ((((uint8_t *)&t310) + 8) + 0));
                gm_l24 = t312;
                {
                    void * t315 = gm_l0;
                    void * t316 = GM_LD(void *, (((uint8_t *)t315) + 0));
                    uint32_t t317 = gm_l24;
                    void * t318 = gm_f_adt_mpqv8qe(t316, t317);
                    uint8_t t319 = GM_LD(uint8_t, ((((uint8_t *)t318) + 24) + 0));
                    if (t319 != ((uint8_t)1ULL)) goto ie314;
                    gm_t88rcucg1b7uk t320 = GM_LD(gm_t88rcucg1b7uk, (((((uint8_t *)t318) + 24) + 8) + 0));
                    gm_l25 = t320;
                    gm_b16_8 t321 = GM_LD(gm_b16_8, (((((uint8_t *)t318) + 24) + 8) + 8));
                    gm_l26 = t321;
                    {
                        gm_b16_8 t323 = gm_l26;
                        gm_b16_8 t324 = t323;
                        uint32_t t325 = gm_l22;
                        uint64_t t326 = (uint64_t)t325;
                        uint64_t t327 = (uintptr_t)t326;
                        gm_b16_8 t328 = t324;
                        uint64_t t329 = GM_LD(uintptr_t, ((uint8_t *)&t328) + 8);
                        if (t327 >= (uintptr_t)(t329)) gm_trap_index(t327, t329, gm_srcuro7l7pd15c2, 198, 19);
                        gm_tckdobunaulee t330 = GM_LD(gm_tckdobunaulee, ((uint8_t *)GM_LD(void *, ((uint8_t *)&t328)) + t327 * 72));
                        F.l27 = t330;
                    }
                    {
                        void * t331 = gm_l0;
                        void * t332 = (void *)(((uint8_t *)t331) + 8);
                        gm_t88rcucg1b7uk t333 = gm_l2;
                        gm_b16_8 t334 = gm_f_offsets_eh6vfle((void *)NULL, cx, NULL, t332, t333);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_l28 = t334;
                    }
                    {
                        gm_ta24rd13gupia t335;
                        gm_f_to_i128_o7ud1ue((void *)&t335, &(*(gm_tbci5ospk5nnn *)(((uint8_t *)&F.l27) + 24)));
                        gm_ta24rd13gupia t336 = t335;
                        __int128 t337;
                        if (((((uint8_t *)&t336))[16] != 0)) {
                            __int128 t338 = GM_LD(__int128, (((uint8_t *)&t336) + 0));
                            t337 = t338;
                        } else {
                            t337 = ((__int128)(((unsigned __int128)0u << 64) | 0u));
                        }
                        gm_l29 = t337;
                    }
                    {
                        void * t339 = gm_l0;
                        __int128 t340 = gm_l29;
                        unsigned __int128 t341 = (unsigned __int128)t340;
                        gm_t6evimt84lndq t342;
                        GM_ST(uint8_t, (uint8_t *)&t342 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t342 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t342 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t342 + 8, 0);
                        GM_ST(uint64_t, (uint8_t *)&t342 + 32, 0);
                        GM_ST(uint64_t, (uint8_t *)&t342 + 40, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t342 + 0), ((uint8_t)0ULL));
                        GM_ST(unsigned __int128, ((uint8_t *)&t342 + 16), t341);
                        gm_t88rcucg1b7uk t343 = gm_l25;
                        gm_b16_8 t344 = gm_l3;
                        uint64_t t345 = gm_l4;
                        uint64_t t346 = (uintptr_t)((uint64_t)0ULL);
                        uint64_t t347 = GM_LD(uintptr_t, ((uint8_t *)&gm_l28) + 8);
                        if (t346 >= (uintptr_t)(t347)) gm_trap_index(t346, t347, gm_srcuro7l7pd15c2, 201, 79);
                        uint64_t t348 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l28)) + t346 * 8));
                        uint64_t r349; bool o350 = __builtin_add_overflow(t345, t348, &r349);
                        if (o350) gm_trap("integer overflow in `+`", gm_srcuro7l7pd15c2, 201, 73);
                        uint64_t t351 = r349;
                        void * t352 = gm_l5;
                        gm_tqgtd5gtgj65s t353;
                        gm_f_value_bytes_qm8egmj((void *)&t353, (void *)NULL, cx, NULL, t339, &t342, t343, t344, t351, t352);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_tqgtd5gtgj65s t354 = t353;
                        if (((((uint8_t *)&t354))[0] != 0)) {
                            gm_t08olq97rscn3 t355 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&t354) + 8));
                            gm_tqgtd5gtgj65s t356;
                            GM_ST(uint8_t, (uint8_t *)&t356 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t356 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t356 + 4, 0);
                            ((uint8_t *)&t356)[0] = 1;
                            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t356 + 8), t355);
                            F.ret = t356;
                            goto gm_fret;
                        }
                        gm_b0_1 t357 = GM_LD(gm_b0_1, (((uint8_t *)&t354) + 8));
                    }
                    {
                        void * t359 = gm_l23;
                        void * t360 = t359;
                        if (!(GM_LD(void *, ((uint8_t *)&t360)) != 0)) goto ie358;
                        void * t361 = GM_LD(void *, ((uint8_t *)&t360));
                        gm_l30 = t361;
                        {
                            gm_tkn42u7na3t81 t363 = GM_LD(gm_tkn42u7na3t81, (((uint8_t *)&F.l27) + 16));
                            gm_tkn42u7na3t81 t364 = t363;
                            gm_t88rcucg1b7uk t365;
                            if (((((uint8_t *)&t364))[4] != 0)) {
                                gm_t88rcucg1b7uk t366 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t364) + 0));
                                t365 = t366;
                            } else {
                                gm_tl874t2atnlh3 t367 = GM_LD(gm_tl874t2atnlh3, gm_cejn1dj0054);
                                gm_b16_8 t368;
                                GM_ST(void *, (uint8_t *)&t368, (void *)(gm_sa6mes8ijqmmk));
                                GM_ST(uintptr_t, (uint8_t *)&t368 + 8, (uintptr_t)(29));
                                gm_t08olq97rscn3 t369;
                                gm_f_unsupported_531q51r((void *)&t369, (void *)NULL, cx, NULL, t367, t368);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_tqgtd5gtgj65s t370;
                                GM_ST(uint8_t, (uint8_t *)&t370 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t370 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t370 + 4, 0);
                                ((uint8_t *)&t370)[0] = 1;
                                GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t370 + 8), t369);
                                gm_tqgtd5gtgj65s t371 = t370;
                                uint8_t t372 = ((((uint8_t *)&t371))[0] != 0);
                                F.ret = t370;
                                goto gm_fret;
                            }
                            gm_l31 = t365;
                        }
                        {
                            void * t373 = gm_l0;
                            void * t374 = gm_l30;
                            gm_t88rcucg1b7uk t375 = gm_l31;
                            gm_b16_8 t376 = gm_l3;
                            uint64_t t377 = gm_l4;
                            uint64_t t378 = (uintptr_t)((uint64_t)1ULL);
                            uint64_t t379 = GM_LD(uintptr_t, ((uint8_t *)&gm_l28) + 8);
                            if (t378 >= (uintptr_t)(t379)) gm_trap_index(t378, t379, gm_srcuro7l7pd15c2, 204, 54);
                            uint64_t t380 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l28)) + t378 * 8));
                            uint64_t r381; bool o382 = __builtin_add_overflow(t377, t380, &r381);
                            if (o382) gm_trap("integer overflow in `+`", gm_srcuro7l7pd15c2, 204, 48);
                            uint64_t t383 = r381;
                            void * t384 = gm_l5;
                            gm_tqgtd5gtgj65s t385;
                            gm_f_value_bytes_qm8egmj((void *)&t385, (void *)NULL, cx, NULL, t373, &(*(gm_t6evimt84lndq *)((uint8_t *)t374)), t375, t376, t383, t384);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_tqgtd5gtgj65s t386 = t385;
                            if (((((uint8_t *)&t386))[0] != 0)) {
                                gm_t08olq97rscn3 t387 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&t386) + 8));
                                gm_tqgtd5gtgj65s t388;
                                GM_ST(uint8_t, (uint8_t *)&t388 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t388 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t388 + 4, 0);
                                ((uint8_t *)&t388)[0] = 1;
                                GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t388 + 8), t387);
                                F.ret = t388;
                                goto gm_fret;
                            }
                            gm_b0_1 t389 = GM_LD(gm_b0_1, (((uint8_t *)&t386) + 8));
                        }
                        goto ix358;
ie358:;
ix358:;
                    }
                    goto ix314;
ie314:;
                    {
                        gm_tl874t2atnlh3 t391 = GM_LD(gm_tl874t2atnlh3, gm_cejn1dj0054);
                        gm_b16_8 t392;
                        GM_ST(void *, (uint8_t *)&t392, (void *)(gm_sa6mes8ijqmmk));
                        GM_ST(uintptr_t, (uint8_t *)&t392 + 8, (uintptr_t)(29));
                        gm_t08olq97rscn3 t393;
                        gm_f_unsupported_531q51r((void *)&t393, (void *)NULL, cx, NULL, t391, t392);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_tqgtd5gtgj65s t394;
                        GM_ST(uint8_t, (uint8_t *)&t394 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t394 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t394 + 4, 0);
                        ((uint8_t *)&t394)[0] = 1;
                        GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t394 + 8), t393);
                        gm_tqgtd5gtgj65s t395 = t394;
                        uint8_t t396 = ((((uint8_t *)&t395))[0] != 0);
                        F.ret = t394;
                        goto gm_fret;
                    }
ix314:;
                }
                goto ix305;
ie305:;
                {
                    gm_tl874t2atnlh3 t398 = GM_LD(gm_tl874t2atnlh3, gm_cejn1dj0054);
                    gm_b16_8 t399;
                    GM_ST(void *, (uint8_t *)&t399, (void *)(gm_sa6mes8ijqmmk));
                    GM_ST(uintptr_t, (uint8_t *)&t399 + 8, (uintptr_t)(29));
                    gm_t08olq97rscn3 t400;
                    gm_f_unsupported_531q51r((void *)&t400, (void *)NULL, cx, NULL, t398, t399);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tqgtd5gtgj65s t401;
                    GM_ST(uint8_t, (uint8_t *)&t401 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t401 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t401 + 4, 0);
                    ((uint8_t *)&t401)[0] = 1;
                    GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t401 + 8), t400);
                    gm_tqgtd5gtgj65s t402 = t401;
                    uint8_t t403 = ((((uint8_t *)&t402))[0] != 0);
                    F.ret = t401;
                    goto gm_fret;
                }
ix305:;
            }
            goto me3;
mn300:;
ma12:;
            uint8_t t405 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
            if (t405 != ((uint8_t)3ULL)) goto mn404;
            void * t406 = GM_LD(void *, (((uint8_t *)&F.l1) + 16));
            gm_l32 = t406;
            {
                void * t408 = gm_l0;
                void * t409 = (void *)(((uint8_t *)t408) + 8);
                gm_t88rcucg1b7uk t410 = gm_l2;
                gm_t53at80pfdus3 t411;
                gm_f_kind_ghvav22((void *)&t411, t409, GM_CONV(gm_csguankko4lmhd, t410));
                gm_t53at80pfdus3 t412 = t411;
                uint8_t t419 = GM_LD(uint8_t, (((uint8_t *)&t412) + 0));
                switch (t419) { case ((uint8_t)0ULL): goto ma417; case ((uint8_t)1ULL): goto ma417; case ((uint8_t)2ULL): goto ma417; case ((uint8_t)3ULL): goto ma417; case ((uint8_t)4ULL): goto ma417; case ((uint8_t)5ULL): goto ma417; case ((uint8_t)6ULL): goto ma417; case ((uint8_t)7ULL): goto ma417; case ((uint8_t)8ULL): goto ma417; case ((uint8_t)9ULL): goto ma417; case ((uint8_t)10ULL): goto ma417; case ((uint8_t)11ULL): goto ma414; case ((uint8_t)12ULL): goto ma415; case ((uint8_t)13ULL): goto ma417; case ((uint8_t)14ULL): goto ma417; case ((uint8_t)15ULL): goto ma417; case ((uint8_t)16ULL): goto ma417; case ((uint8_t)17ULL): goto ma416; case ((uint8_t)18ULL): goto ma417; case ((uint8_t)19ULL): goto ma417; case ((uint8_t)20ULL): goto ma417; case ((uint8_t)21ULL): goto ma417; case ((uint8_t)22ULL): goto ma417; case ((uint8_t)23ULL): goto ma417; case ((uint8_t)24ULL): goto ma417; case ((uint8_t)25ULL): goto ma417; case ((uint8_t)26ULL): goto ma417; default: goto ma417; }
ma414:;
                uint8_t t421 = GM_LD(uint8_t, (((uint8_t *)&t412) + 0));
                if (t421 != ((uint8_t)11ULL)) goto mn420;
                gm_t88rcucg1b7uk t422 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t412) + 8) + 8));
                gm_l33 = t422;
                {
                    void * t424 = gm_l0;
                    void * t425 = (void *)(((uint8_t *)t424) + 8);
                    gm_t88rcucg1b7uk t426 = gm_l33;
                    uint64_t t427 = gm_f_size_dmhtkib((void *)NULL, cx, NULL, t425, t426);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l34 = t427;
                }
                {
                    {
                        void * t429 = gm_l32;
                        gm_b16_8 t430 = GM_LD(gm_b16_8, (((uint8_t *)t429) + 0));
                        gm_b16_8 t431 = t430;
                        gm_b16_8 t432 = GM_LD(gm_b16_8, ((uint8_t *)&t431));
                        gm_l35 = t432;
                    }
                    {
                        gm_l36 = ((uint64_t)0ULL);
                    }
                    {
                        for (;;) {
                            uint64_t t434 = gm_l36;
                            uint64_t t435 = GM_LD(uintptr_t, ((uint8_t *)&gm_l35) + 8);
                            uint8_t t436 = (t434 < t435);
                            if (!t436) goto lb433;
                            {
                                gm_thf8as07kj3uj t439;
                                memset((uint8_t *)&t439 + 8, 0, 8);
                                uint64_t t440 = gm_l36;
                                GM_ST(uint64_t, (uint8_t *)&t439 + 0, t440);
                                uint64_t t441 = gm_l36;
                                uint64_t t442 = (uintptr_t)t441;
                                uint64_t t443 = GM_LD(uintptr_t, ((uint8_t *)&gm_l35) + 8);
                                memcpy((uint8_t *)&t439 + 16, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l35)) + t442 * 48), 48);
                                gm_thf8as07kj3uj t444 = t439;
                                uint64_t t447 = GM_LD(uint64_t, (((uint8_t *)&t444) + 0));
                                gm_l37 = t447;
                                gm_t6evimt84lndq t448 = GM_LD(gm_t6evimt84lndq, (((uint8_t *)&t444) + 16));
                                F.l38 = t448;
                                {
                                    void * t450 = gm_l0;
                                    gm_t6evimt84lndq t451 = F.l38;
                                    gm_t88rcucg1b7uk t452 = gm_l33;
                                    gm_b16_8 t453 = gm_l3;
                                    uint64_t t454 = gm_l4;
                                    uint64_t t455 = gm_l37;
                                    uint64_t t456 = (uint64_t)t455;
                                    uint64_t t457 = gm_l34;
                                    uint64_t r458; bool o459 = __builtin_mul_overflow(t456, t457, &r458);
                                    if (o459) gm_trap("integer overflow in `*`", gm_srcuro7l7pd15c2, 211, 92);
                                    uint64_t t460 = r458;
                                    uint64_t r461; bool o462 = __builtin_add_overflow(t454, t460, &r461);
                                    if (o462) gm_trap("integer overflow in `+`", gm_srcuro7l7pd15c2, 211, 86);
                                    uint64_t t463 = r461;
                                    void * t464 = gm_l5;
                                    gm_tqgtd5gtgj65s t465;
                                    gm_f_value_bytes_qm8egmj((void *)&t465, (void *)NULL, cx, NULL, t450, &t451, t452, t453, t463, t464);
                                    if (gm_unw.kind) {
                                        goto gm_ll437;
                                    }
                                    gm_tqgtd5gtgj65s t466 = t465;
                                    if (((((uint8_t *)&t466))[0] != 0)) {
                                        gm_t08olq97rscn3 t467 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&t466) + 8));
                                        gm_tqgtd5gtgj65s t468;
                                        GM_ST(uint8_t, (uint8_t *)&t468 + 1, 0);
                                        GM_ST(uint16_t, (uint8_t *)&t468 + 2, 0);
                                        GM_ST(uint32_t, (uint8_t *)&t468 + 4, 0);
                                        ((uint8_t *)&t468)[0] = 1;
                                        GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t468 + 8), t467);
                                        F.ret = t468;
                                        goto gm_fret;
                                    }
                                    gm_b0_1 t469 = GM_LD(gm_b0_1, (((uint8_t *)&t466) + 8));
                                }
                                goto me445;
mn446:;
                                gm_trap("no match arm matched the scrutinee", gm_srcuro7l7pd15c2, 211, 21);
me445:;
                            }
lc433:;
                            {
                                uint64_t t471 = gm_l36;
                                uint64_t r472; bool o473 = __builtin_add_overflow(t471, ((uint64_t)1ULL), &r472);
                                uint64_t t474 = r472;
                                gm_l36 = t474;
                            }
                        }
gm_ll437:;
                        if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb433; }
                            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc433; }
                        }
                        goto gm_lad;
lb433:;
                    }
                }
                goto me413;
mn420:;
ma415:;
                uint8_t t476 = GM_LD(uint8_t, (((uint8_t *)&t412) + 0));
                if (t476 != ((uint8_t)12ULL)) goto mn475;
                gm_b16_8 t477 = GM_LD(gm_b16_8, ((((uint8_t *)&t412) + 8) + 0));
                gm_l39 = t477;
                {
                    void * t479 = gm_l0;
                    void * t480 = (void *)(((uint8_t *)t479) + 8);
                    gm_t88rcucg1b7uk t481 = gm_l2;
                    gm_b16_8 t482 = gm_f_offsets_eh6vfle((void *)NULL, cx, NULL, t480, t481);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l40 = t482;
                }
                {
                    {
                        void * t484 = gm_l32;
                        gm_b16_8 t485 = GM_LD(gm_b16_8, (((uint8_t *)t484) + 0));
                        gm_b16_8 t486 = t485;
                        gm_b16_8 t487 = GM_LD(gm_b16_8, ((uint8_t *)&t486));
                        gm_l41 = t487;
                    }
                    {
                        gm_l42 = ((uint64_t)0ULL);
                    }
                    {
                        for (;;) {
                            uint64_t t489 = gm_l42;
                            uint64_t t490 = GM_LD(uintptr_t, ((uint8_t *)&gm_l41) + 8);
                            uint8_t t491 = (t489 < t490);
                            if (!t491) goto lb488;
                            {
                                gm_thf8as07kj3uj t494;
                                memset((uint8_t *)&t494 + 8, 0, 8);
                                uint64_t t495 = gm_l42;
                                GM_ST(uint64_t, (uint8_t *)&t494 + 0, t495);
                                uint64_t t496 = gm_l42;
                                uint64_t t497 = (uintptr_t)t496;
                                uint64_t t498 = GM_LD(uintptr_t, ((uint8_t *)&gm_l41) + 8);
                                memcpy((uint8_t *)&t494 + 16, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l41)) + t497 * 48), 48);
                                gm_thf8as07kj3uj t499 = t494;
                                uint64_t t502 = GM_LD(uint64_t, (((uint8_t *)&t499) + 0));
                                gm_l43 = t502;
                                gm_t6evimt84lndq t503 = GM_LD(gm_t6evimt84lndq, (((uint8_t *)&t499) + 16));
                                F.l44 = t503;
                                {
                                    void * t505 = gm_l0;
                                    uint64_t t506 = gm_l43;
                                    uint64_t t507 = (uintptr_t)t506;
                                    uint64_t t508 = GM_LD(uintptr_t, ((uint8_t *)&gm_l39) + 8);
                                    if (t507 >= (uintptr_t)(t508)) gm_trap_index(t507, t508, gm_srcuro7l7pd15c2, 215, 75);
                                    gm_t88rcucg1b7uk t509 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l39)) + t507 * 4));
                                    gm_b16_8 t510 = gm_l3;
                                    uint64_t t511 = gm_l4;
                                    uint64_t t512 = gm_l43;
                                    uint64_t t513 = (uintptr_t)t512;
                                    uint64_t t514 = GM_LD(uintptr_t, ((uint8_t *)&gm_l40) + 8);
                                    if (t513 >= (uintptr_t)(t514)) gm_trap_index(t513, t514, gm_srcuro7l7pd15c2, 215, 96);
                                    uint64_t t515 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l40)) + t513 * 8));
                                    uint64_t r516; bool o517 = __builtin_add_overflow(t511, t515, &r516);
                                    if (o517) gm_trap("integer overflow in `+`", gm_srcuro7l7pd15c2, 215, 90);
                                    uint64_t t518 = r516;
                                    void * t519 = gm_l5;
                                    gm_tqgtd5gtgj65s t520;
                                    gm_f_value_bytes_qm8egmj((void *)&t520, (void *)NULL, cx, NULL, t505, &(*(gm_t6evimt84lndq *)((uint8_t *)&F.l44)), t509, t510, t518, t519);
                                    if (gm_unw.kind) {
                                        goto gm_ll492;
                                    }
                                    gm_tqgtd5gtgj65s t521 = t520;
                                    if (((((uint8_t *)&t521))[0] != 0)) {
                                        gm_t08olq97rscn3 t522 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&t521) + 8));
                                        gm_tqgtd5gtgj65s t523;
                                        GM_ST(uint8_t, (uint8_t *)&t523 + 1, 0);
                                        GM_ST(uint16_t, (uint8_t *)&t523 + 2, 0);
                                        GM_ST(uint32_t, (uint8_t *)&t523 + 4, 0);
                                        ((uint8_t *)&t523)[0] = 1;
                                        GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t523 + 8), t522);
                                        F.ret = t523;
                                        goto gm_fret;
                                    }
                                    gm_b0_1 t524 = GM_LD(gm_b0_1, (((uint8_t *)&t521) + 8));
                                }
                                goto me500;
mn501:;
                                gm_trap("no match arm matched the scrutinee", gm_srcuro7l7pd15c2, 215, 21);
me500:;
                            }
lc488:;
                            {
                                uint64_t t526 = gm_l42;
                                uint64_t r527; bool o528 = __builtin_add_overflow(t526, ((uint64_t)1ULL), &r527);
                                uint64_t t529 = r527;
                                gm_l42 = t529;
                            }
                        }
gm_ll492:;
                        if (gm_unw.frame == &F.h && gm_unw.label == 2) {
                            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb488; }
                            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc488; }
                        }
                        goto gm_lad;
lb488:;
                    }
                }
                goto me413;
mn475:;
ma416:;
                uint8_t t531 = GM_LD(uint8_t, (((uint8_t *)&t412) + 0));
                if (t531 != ((uint8_t)17ULL)) goto mn530;
                uint32_t t532 = GM_LD(uint32_t, ((((uint8_t *)&t412) + 8) + 0));
                gm_l45 = t532;
                void * t533 = gm_l0;
                void * t534 = GM_LD(void *, (((uint8_t *)t533) + 0));
                uint32_t t535 = gm_l45;
                void * t536 = gm_f_adt_mpqv8qe(t534, t535);
                uint8_t t539 = GM_LD(uint8_t, ((((uint8_t *)t536) + 24) + 0));
                if (t539 != ((uint8_t)0ULL)) goto mn538;
                gm_tpautr4br7h3l t540 = GM_LD(gm_tpautr4br7h3l, ((((uint8_t *)t536) + 24) + 8));
                F.l46 = t540;
                {
                    if (!(((((uint8_t *)&F.l46) + 40))[4] != 0)) goto ie542;
                    gm_t88rcucg1b7uk t543 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l46) + 40));
                    gm_l47 = t543;
                    {
                        void * t545 = gm_l0;
                        gm_t88rcucg1b7uk t546 = gm_l2;
                        gm_t3d480iurj7vi t547;
                        gm_f_bits_of_ilgn83m((void *)&t547, (void *)NULL, cx, NULL, t545, &(*(gm_t6evimt84lndq *)((uint8_t *)&F.l1)), t546);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_t3d480iurj7vi t548 = t547;
                        if (((((uint8_t *)&t548))[0] != 0)) {
                            gm_t08olq97rscn3 t549 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&t548) + 16));
                            gm_tqgtd5gtgj65s t550;
                            GM_ST(uint8_t, (uint8_t *)&t550 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t550 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t550 + 4, 0);
                            ((uint8_t *)&t550)[0] = 1;
                            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t550 + 8), t549);
                            F.ret = t550;
                            goto gm_fret;
                        }
                        unsigned __int128 t551 = GM_LD(unsigned __int128, (((uint8_t *)&t548) + 16));
                        gm_l48 = t551;
                    }
                    {
                        void * t552 = gm_l0;
                        unsigned __int128 t553 = gm_l48;
                        gm_t6evimt84lndq t554;
                        GM_ST(uint8_t, (uint8_t *)&t554 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t554 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t554 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t554 + 8, 0);
                        GM_ST(uint64_t, (uint8_t *)&t554 + 32, 0);
                        GM_ST(uint64_t, (uint8_t *)&t554 + 40, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t554 + 0), ((uint8_t)0ULL));
                        GM_ST(unsigned __int128, ((uint8_t *)&t554 + 16), t553);
                        gm_t88rcucg1b7uk t555 = gm_l47;
                        gm_b16_8 t556 = gm_l3;
                        uint64_t t557 = gm_l4;
                        void * t558 = gm_l5;
                        gm_f_value_bytes_qm8egmj((void *)&(*(gm_tqgtd5gtgj65s *)gm_out), (void *)NULL, cx, NULL, t552, &t554, t555, t556, t557, t558);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        goto gm_fdone;
                    }
                    goto ix542;
ie542:;
ix542:;
                }
                {
                    void * t559 = gm_l0;
                    void * t560 = (void *)(((uint8_t *)t559) + 8);
                    gm_t88rcucg1b7uk t561 = gm_l2;
                    gm_b16_8 t562 = gm_f_offsets_eh6vfle((void *)NULL, cx, NULL, t560, t561);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l49 = t562;
                }
                {
                    {
                        void * t564 = gm_l32;
                        gm_b16_8 t565 = GM_LD(gm_b16_8, (((uint8_t *)t564) + 0));
                        gm_b16_8 t566 = t565;
                        gm_b16_8 t567 = GM_LD(gm_b16_8, ((uint8_t *)&t566));
                        gm_l50 = t567;
                    }
                    {
                        gm_l51 = ((uint64_t)0ULL);
                    }
                    {
                        for (;;) {
                            uint64_t t569 = gm_l51;
                            uint64_t t570 = GM_LD(uintptr_t, ((uint8_t *)&gm_l50) + 8);
                            uint8_t t571 = (t569 < t570);
                            if (!t571) goto lb568;
                            {
                                gm_thf8as07kj3uj t574;
                                memset((uint8_t *)&t574 + 8, 0, 8);
                                uint64_t t575 = gm_l51;
                                GM_ST(uint64_t, (uint8_t *)&t574 + 0, t575);
                                uint64_t t576 = gm_l51;
                                uint64_t t577 = (uintptr_t)t576;
                                uint64_t t578 = GM_LD(uintptr_t, ((uint8_t *)&gm_l50) + 8);
                                memcpy((uint8_t *)&t574 + 16, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l50)) + t577 * 48), 48);
                                gm_thf8as07kj3uj t579 = t574;
                                uint64_t t582 = GM_LD(uint64_t, (((uint8_t *)&t579) + 0));
                                gm_l52 = t582;
                                gm_t6evimt84lndq t583 = GM_LD(gm_t6evimt84lndq, (((uint8_t *)&t579) + 16));
                                F.l53 = t583;
                                {
                                    void * t585 = gm_l0;
                                    uint64_t t586 = gm_l52;
                                    uint64_t t587 = (uintptr_t)t586;
                                    uint64_t t588 = GM_LD(uintptr_t, (((uint8_t *)&F.l46) + 0) + 8);
                                    if (t587 >= (uintptr_t)(t588)) gm_trap_index(t587, t588, gm_srcuro7l7pd15c2, 224, 79);
                                    gm_t88rcucg1b7uk t589 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l46) + 0)) + t587 * 40) + 16));
                                    gm_b16_8 t590 = gm_l3;
                                    uint64_t t591 = gm_l4;
                                    uint64_t t592 = gm_l52;
                                    uint64_t t593 = (uintptr_t)t592;
                                    uint64_t t594 = GM_LD(uintptr_t, ((uint8_t *)&gm_l49) + 8);
                                    if (t593 >= (uintptr_t)(t594)) gm_trap_index(t593, t594, gm_srcuro7l7pd15c2, 224, 107);
                                    uint64_t t595 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l49)) + t593 * 8));
                                    uint64_t r596; bool o597 = __builtin_add_overflow(t591, t595, &r596);
                                    if (o597) gm_trap("integer overflow in `+`", gm_srcuro7l7pd15c2, 224, 101);
                                    uint64_t t598 = r596;
                                    void * t599 = gm_l5;
                                    gm_tqgtd5gtgj65s t600;
                                    gm_f_value_bytes_qm8egmj((void *)&t600, (void *)NULL, cx, NULL, t585, &(*(gm_t6evimt84lndq *)((uint8_t *)&F.l53)), t589, t590, t598, t599);
                                    if (gm_unw.kind) {
                                        goto gm_ll572;
                                    }
                                    gm_tqgtd5gtgj65s t601 = t600;
                                    if (((((uint8_t *)&t601))[0] != 0)) {
                                        gm_t08olq97rscn3 t602 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&t601) + 8));
                                        gm_tqgtd5gtgj65s t603;
                                        GM_ST(uint8_t, (uint8_t *)&t603 + 1, 0);
                                        GM_ST(uint16_t, (uint8_t *)&t603 + 2, 0);
                                        GM_ST(uint32_t, (uint8_t *)&t603 + 4, 0);
                                        ((uint8_t *)&t603)[0] = 1;
                                        GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t603 + 8), t602);
                                        F.ret = t603;
                                        goto gm_fret;
                                    }
                                    gm_b0_1 t604 = GM_LD(gm_b0_1, (((uint8_t *)&t601) + 8));
                                }
                                goto me580;
mn581:;
                                gm_trap("no match arm matched the scrutinee", gm_srcuro7l7pd15c2, 224, 25);
me580:;
                            }
lc568:;
                            {
                                uint64_t t606 = gm_l51;
                                uint64_t r607; bool o608 = __builtin_add_overflow(t606, ((uint64_t)1ULL), &r607);
                                uint64_t t609 = r607;
                                gm_l51 = t609;
                            }
                        }
gm_ll572:;
                        if (gm_unw.frame == &F.h && gm_unw.label == 3) {
                            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb568; }
                            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc568; }
                        }
                        goto gm_lad;
lb568:;
                    }
                }
                goto me537;
mn538:;
                gm_tl874t2atnlh3 t611 = GM_LD(gm_tl874t2atnlh3, gm_cejn1dj0054);
                gm_b16_8 t612;
                GM_ST(void *, (uint8_t *)&t612, (void *)(gm_s67fd8s6hf84e));
                GM_ST(uintptr_t, (uint8_t *)&t612 + 8, (uintptr_t)(23));
                gm_t08olq97rscn3 t613;
                gm_f_unsupported_531q51r((void *)&t613, (void *)NULL, cx, NULL, t611, t612);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tqgtd5gtgj65s t614;
                GM_ST(uint8_t, (uint8_t *)&t614 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t614 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t614 + 4, 0);
                ((uint8_t *)&t614)[0] = 1;
                GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t614 + 8), t613);
                gm_tqgtd5gtgj65s t615 = t614;
                uint8_t t616 = ((((uint8_t *)&t615))[0] != 0);
                F.ret = t614;
                goto gm_fret;
                goto me537;
mn610:;
                gm_trap("no match arm matched the scrutinee", gm_srcuro7l7pd15c2, 217, 32);
me537:;
                goto me413;
mn530:;
ma417:;
                gm_tl874t2atnlh3 t618 = GM_LD(gm_tl874t2atnlh3, gm_cejn1dj0054);
                gm_b16_8 t619;
                GM_ST(void *, (uint8_t *)&t619, (void *)(gm_s67fd8s6hf84e));
                GM_ST(uintptr_t, (uint8_t *)&t619 + 8, (uintptr_t)(23));
                gm_t08olq97rscn3 t620;
                gm_f_unsupported_531q51r((void *)&t620, (void *)NULL, cx, NULL, t618, t619);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tqgtd5gtgj65s t621;
                GM_ST(uint8_t, (uint8_t *)&t621 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t621 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t621 + 4, 0);
                ((uint8_t *)&t621)[0] = 1;
                GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t621 + 8), t620);
                gm_tqgtd5gtgj65s t622 = t621;
                uint8_t t623 = ((((uint8_t *)&t622))[0] != 0);
                F.ret = t621;
                goto gm_fret;
                goto me413;
mn617:;
ma418:;
                gm_trap("no match arm matched the scrutinee", gm_srcuro7l7pd15c2, 208, 13);
me413:;
            }
            goto me3;
mn404:;
ma13:;
            uint8_t t625 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
            if (t625 != ((uint8_t)4ULL)) goto mn624;
            gm_tkaapcii86udk t626 = GM_LD(gm_tkaapcii86udk, (((uint8_t *)&F.l1) + 16));
            gm_l54 = t626;
            {
                void * t628 = gm_l0;
                void * t629 = (void *)(((uint8_t *)t628) + 8);
                gm_t88rcucg1b7uk t630 = gm_l2;
                gm_t53at80pfdus3 t631;
                gm_f_kind_ghvav22((void *)&t631, t629, GM_CONV(gm_csguankko4lmhd, t630));
                gm_t53at80pfdus3 t632 = t631;
                gm_t88rcucg1b7uk t633;
                uint8_t t640 = GM_LD(uint8_t, (((uint8_t *)&t632) + 0));
                switch (t640) { case ((uint8_t)0ULL): goto ma638; case ((uint8_t)1ULL): goto ma638; case ((uint8_t)2ULL): goto ma638; case ((uint8_t)3ULL): goto ma638; case ((uint8_t)4ULL): goto ma638; case ((uint8_t)5ULL): goto ma638; case ((uint8_t)6ULL): goto ma638; case ((uint8_t)7ULL): goto ma638; case ((uint8_t)8ULL): goto ma635; case ((uint8_t)9ULL): goto ma636; case ((uint8_t)10ULL): goto ma638; case ((uint8_t)11ULL): goto ma638; case ((uint8_t)12ULL): goto ma638; case ((uint8_t)13ULL): goto ma637; case ((uint8_t)14ULL): goto ma638; case ((uint8_t)15ULL): goto ma638; case ((uint8_t)16ULL): goto ma638; case ((uint8_t)17ULL): goto ma638; case ((uint8_t)18ULL): goto ma638; case ((uint8_t)19ULL): goto ma638; case ((uint8_t)20ULL): goto ma638; case ((uint8_t)21ULL): goto ma638; case ((uint8_t)22ULL): goto ma638; case ((uint8_t)23ULL): goto ma638; case ((uint8_t)24ULL): goto ma638; case ((uint8_t)25ULL): goto ma638; case ((uint8_t)26ULL): goto ma638; default: goto ma638; }
ma635:;
                uint8_t t642 = GM_LD(uint8_t, (((uint8_t *)&t632) + 0));
                if (t642 != ((uint8_t)8ULL)) goto mn641;
                gm_t88rcucg1b7uk t643 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t632) + 8) + 4));
                gm_l55 = t643;
                gm_t88rcucg1b7uk t644 = gm_l55;
                t633 = t644;
                goto me634;
mn641:;
ma636:;
                uint8_t t646 = GM_LD(uint8_t, (((uint8_t *)&t632) + 0));
                if (t646 != ((uint8_t)9ULL)) goto mn645;
                gm_t88rcucg1b7uk t647 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t632) + 8) + 4));
                gm_l56 = t647;
                gm_t88rcucg1b7uk t648 = gm_l56;
                t633 = t648;
                goto me634;
mn645:;
ma637:;
                uint8_t t650 = GM_LD(uint8_t, (((uint8_t *)&t632) + 0));
                if (t650 != ((uint8_t)13ULL)) goto mn649;
                gm_t88rcucg1b7uk t651 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t632) + 8));
                gm_l57 = t651;
                void * t652 = gm_l0;
                void * t653 = (void *)(((uint8_t *)t652) + 8);
                gm_t88rcucg1b7uk t654 = gm_l57;
                gm_t53at80pfdus3 t655;
                gm_f_kind_ghvav22((void *)&t655, t653, GM_CONV(gm_csguankko4lmhd, t654));
                gm_t53at80pfdus3 t656 = t655;
                gm_t88rcucg1b7uk t657;
                uint8_t t663 = GM_LD(uint8_t, (((uint8_t *)&t656) + 0));
                switch (t663) { case ((uint8_t)0ULL): goto ma661; case ((uint8_t)1ULL): goto ma661; case ((uint8_t)2ULL): goto ma661; case ((uint8_t)3ULL): goto ma661; case ((uint8_t)4ULL): goto ma661; case ((uint8_t)5ULL): goto ma661; case ((uint8_t)6ULL): goto ma661; case ((uint8_t)7ULL): goto ma661; case ((uint8_t)8ULL): goto ma659; case ((uint8_t)9ULL): goto ma660; case ((uint8_t)10ULL): goto ma661; case ((uint8_t)11ULL): goto ma661; case ((uint8_t)12ULL): goto ma661; case ((uint8_t)13ULL): goto ma661; case ((uint8_t)14ULL): goto ma661; case ((uint8_t)15ULL): goto ma661; case ((uint8_t)16ULL): goto ma661; case ((uint8_t)17ULL): goto ma661; case ((uint8_t)18ULL): goto ma661; case ((uint8_t)19ULL): goto ma661; case ((uint8_t)20ULL): goto ma661; case ((uint8_t)21ULL): goto ma661; case ((uint8_t)22ULL): goto ma661; case ((uint8_t)23ULL): goto ma661; case ((uint8_t)24ULL): goto ma661; case ((uint8_t)25ULL): goto ma661; case ((uint8_t)26ULL): goto ma661; default: goto ma661; }
ma659:;
                uint8_t t665 = GM_LD(uint8_t, (((uint8_t *)&t656) + 0));
                if (t665 != ((uint8_t)8ULL)) goto mn664;
                gm_t88rcucg1b7uk t666 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t656) + 8) + 4));
                gm_l58 = t666;
                gm_t88rcucg1b7uk t667 = gm_l58;
                t657 = t667;
                goto me658;
mn664:;
ma660:;
                uint8_t t669 = GM_LD(uint8_t, (((uint8_t *)&t656) + 0));
                if (t669 != ((uint8_t)9ULL)) goto mn668;
                gm_t88rcucg1b7uk t670 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t656) + 8) + 4));
                gm_l59 = t670;
                gm_t88rcucg1b7uk t671 = gm_l59;
                t657 = t671;
                goto me658;
mn668:;
ma661:;
                gm_tl874t2atnlh3 t673 = GM_LD(gm_tl874t2atnlh3, gm_cejn1dj0054);
                gm_b16_8 t674;
                GM_ST(void *, (uint8_t *)&t674, (void *)(gm_sgiqpj781s1a9));
                GM_ST(uintptr_t, (uint8_t *)&t674 + 8, (uintptr_t)(35));
                gm_t08olq97rscn3 t675;
                gm_f_unsupported_531q51r((void *)&t675, (void *)NULL, cx, NULL, t673, t674);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tqgtd5gtgj65s t676;
                GM_ST(uint8_t, (uint8_t *)&t676 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t676 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t676 + 4, 0);
                ((uint8_t *)&t676)[0] = 1;
                GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t676 + 8), t675);
                gm_tqgtd5gtgj65s t677 = t676;
                uint8_t t678 = ((((uint8_t *)&t677))[0] != 0);
                F.ret = t676;
                goto gm_fret;
                goto me658;
mn672:;
ma662:;
                gm_trap("no match arm matched the scrutinee", gm_srcuro7l7pd15c2, 235, 37);
me658:;
                t633 = t657;
                goto me634;
mn649:;
ma638:;
                gm_tl874t2atnlh3 t680 = GM_LD(gm_tl874t2atnlh3, gm_cejn1dj0054);
                gm_b16_8 t681;
                GM_ST(void *, (uint8_t *)&t681, (void *)(gm_sgiqpj781s1a9));
                GM_ST(uintptr_t, (uint8_t *)&t681 + 8, (uintptr_t)(35));
                gm_t08olq97rscn3 t682;
                gm_f_unsupported_531q51r((void *)&t682, (void *)NULL, cx, NULL, t680, t681);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tqgtd5gtgj65s t683;
                GM_ST(uint8_t, (uint8_t *)&t683 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t683 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t683 + 4, 0);
                ((uint8_t *)&t683)[0] = 1;
                GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t683 + 8), t682);
                gm_tqgtd5gtgj65s t684 = t683;
                uint8_t t685 = ((((uint8_t *)&t684))[0] != 0);
                F.ret = t683;
                goto gm_fret;
                goto me634;
mn679:;
ma639:;
                gm_trap("no match arm matched the scrutinee", gm_srcuro7l7pd15c2, 232, 27);
me634:;
                gm_l60 = t633;
            }
            {
                void * t686 = gm_l0;
                gm_t88rcucg1b7uk t687 = gm_l60;
                uint64_t t688 = gm_l4;
                gm_tt9fudfifskca t689;
                gm_f_place_reloc_blfij5d((void *)&t689, (void *)NULL, cx, NULL, t686, &(*(gm_tkaapcii86udk *)((uint8_t *)&gm_l54)), t687, t688);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tt9fudfifskca t690 = t689;
                if (((((uint8_t *)&t690))[0] != 0)) {
                    gm_t08olq97rscn3 t691 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&t690) + 8));
                    gm_tqgtd5gtgj65s t692;
                    GM_ST(uint8_t, (uint8_t *)&t692 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t692 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t692 + 4, 0);
                    ((uint8_t *)&t692)[0] = 1;
                    GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t692 + 8), t691);
                    F.ret = t692;
                    goto gm_fret;
                }
                gm_tob1snuii5bme t693 = GM_LD(gm_tob1snuii5bme, (((uint8_t *)&t690) + 8));
                gm_l61 = t693;
            }
            {
                void * t694 = gm_l5;
                gm_tftb1odc739t5 t695 = gm_f_push_vgtfcjf(t694, &(*(gm_tob1snuii5bme *)((uint8_t *)&gm_l61)));
                gm_tftb1odc739t5 t696 = t695;
                if (((((uint8_t *)&t696))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t696) + 1, gm_srcuro7l7pd15c2, 243, 13);
                gm_b0_1 t697 = GM_LD(gm_b0_1, (((uint8_t *)&t696) + 1));
            }
            goto me3;
mn624:;
ma14:;
            uint8_t t699 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
            if (t699 != ((uint8_t)5ULL)) goto mn698;
            gm_tkaapcii86udk t700 = GM_LD(gm_tkaapcii86udk, ((((uint8_t *)&F.l1) + 16) + 0));
            gm_l62 = t700;
            uint64_t t701 = GM_LD(uint64_t, ((((uint8_t *)&F.l1) + 16) + 24));
            gm_l63 = t701;
            {
                void * t704 = gm_l0;
                void * t705 = (void *)(((uint8_t *)t704) + 8);
                gm_t88rcucg1b7uk t706 = gm_l2;
                gm_t53at80pfdus3 t707;
                gm_f_kind_ghvav22((void *)&t707, t705, GM_CONV(gm_csguankko4lmhd, t706));
                gm_t53at80pfdus3 t708 = t707;
                uint8_t t709 = GM_LD(uint8_t, (((uint8_t *)&t708) + 0));
                if (t709 != ((uint8_t)10ULL)) goto ie703;
                gm_t88rcucg1b7uk t710 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t708) + 8) + 4));
                gm_l64 = t710;
                {
                    void * t712 = gm_l0;
                    uint64_t t713 = GM_LD(uint64_t, ((((((uint8_t *)t712) + 8) + 8) + 0) + 0));
                    gm_l65 = t713;
                }
                {
                    uint8_t t715 = 0;
                    uint64_t t716 = gm_l63;
                    uint8_t t717 = (t716 > ((uint64_t)0ULL));
                    if (!t717) {
                        uint64_t t718 = GM_LD(uintptr_t, (((uint8_t *)&gm_l62) + 8) + 8);
                        uint8_t t719 = (t718 > ((uint64_t)0ULL));
                        t715 = t719;
                    } else t715 = 1;
                    if (!t715) goto ie714;
                    {
                        void * t721 = gm_l0;
                        gm_t88rcucg1b7uk t722 = gm_l64;
                        uint64_t t723 = gm_l4;
                        gm_tt9fudfifskca t724;
                        gm_f_place_reloc_blfij5d((void *)&t724, (void *)NULL, cx, NULL, t721, &(*(gm_tkaapcii86udk *)((uint8_t *)&gm_l62)), t722, t723);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_tt9fudfifskca t725 = t724;
                        if (((((uint8_t *)&t725))[0] != 0)) {
                            gm_t08olq97rscn3 t726 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&t725) + 8));
                            gm_tqgtd5gtgj65s t727;
                            GM_ST(uint8_t, (uint8_t *)&t727 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t727 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t727 + 4, 0);
                            ((uint8_t *)&t727)[0] = 1;
                            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t727 + 8), t726);
                            F.ret = t727;
                            goto gm_fret;
                        }
                        gm_tob1snuii5bme t728 = GM_LD(gm_tob1snuii5bme, (((uint8_t *)&t725) + 8));
                        gm_l66 = t728;
                    }
                    {
                        void * t729 = gm_l5;
                        gm_tftb1odc739t5 t730 = gm_f_push_vgtfcjf(t729, &(*(gm_tob1snuii5bme *)((uint8_t *)&gm_l66)));
                        gm_tftb1odc739t5 t731 = t730;
                        if (((((uint8_t *)&t731))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t731) + 1, gm_srcuro7l7pd15c2, 250, 17);
                        gm_b0_1 t732 = GM_LD(gm_b0_1, (((uint8_t *)&t731) + 1));
                    }
                    goto ix714;
ie714:;
ix714:;
                }
                {
                    void * t733 = gm_l0;
                    gm_b16_8 t734 = gm_l3;
                    uint64_t t735 = gm_l4;
                    uint64_t t736 = gm_l65;
                    uint64_t r737; bool o738 = __builtin_add_overflow(t735, t736, &r737);
                    if (o738) gm_trap("integer overflow in `+`", gm_srcuro7l7pd15c2, 252, 30);
                    uint64_t t739 = r737;
                    uint64_t t740 = gm_l63;
                    unsigned __int128 t741 = (unsigned __int128)t740;
                    uint64_t t742 = gm_l65;
                    gm_f_store_int_mfkaqk5(t733, t734, t739, t741, t742);
                }
                goto ix703;
ie703:;
                {
                    gm_tl874t2atnlh3 t744 = GM_LD(gm_tl874t2atnlh3, gm_cejn1dj0054);
                    gm_b16_8 t745;
                    GM_ST(void *, (uint8_t *)&t745, (void *)(gm_svlcdvcv2bp94));
                    GM_ST(uintptr_t, (uint8_t *)&t745 + 8, (uintptr_t)(23));
                    gm_t08olq97rscn3 t746;
                    gm_f_unsupported_531q51r((void *)&t746, (void *)NULL, cx, NULL, t744, t745);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tqgtd5gtgj65s t747;
                    GM_ST(uint8_t, (uint8_t *)&t747 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t747 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t747 + 4, 0);
                    ((uint8_t *)&t747)[0] = 1;
                    GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t747 + 8), t746);
                    gm_tqgtd5gtgj65s t748 = t747;
                    uint8_t t749 = ((((uint8_t *)&t748))[0] != 0);
                    F.ret = t747;
                    goto gm_fret;
                }
ix703:;
            }
            goto me3;
mn698:;
ma15:;
            uint8_t t751 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
            if (t751 != ((uint8_t)11ULL)) goto mn750;
            uint32_t t752 = GM_LD(uint32_t, (((uint8_t *)&F.l1) + 16));
            gm_l67 = t752;
            {
                void * t754 = gm_l0;
                uint32_t t755 = gm_l67;
                gm_b0_1 t756 = gm_f_queue_cd51r8c((void *)NULL, cx, NULL, t754, t755);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t757 = gm_l0;
                void * t758 = (void *)(((uint8_t *)t757) + 8);
                gm_t88rcucg1b7uk t759 = gm_l2;
                gm_t53at80pfdus3 t760;
                gm_f_kind_ghvav22((void *)&t760, t758, GM_CONV(gm_csguankko4lmhd, t759));
                gm_t53at80pfdus3 t761 = t760;
                uint8_t t767 = GM_LD(uint8_t, (((uint8_t *)&t761) + 0));
                switch (t767) { case ((uint8_t)0ULL): goto ma765; case ((uint8_t)1ULL): goto ma765; case ((uint8_t)2ULL): goto ma765; case ((uint8_t)3ULL): goto ma765; case ((uint8_t)4ULL): goto ma765; case ((uint8_t)5ULL): goto ma765; case ((uint8_t)6ULL): goto ma765; case ((uint8_t)7ULL): goto ma765; case ((uint8_t)8ULL): goto ma765; case ((uint8_t)9ULL): goto ma765; case ((uint8_t)10ULL): goto ma765; case ((uint8_t)11ULL): goto ma765; case ((uint8_t)12ULL): goto ma765; case ((uint8_t)13ULL): goto ma764; case ((uint8_t)14ULL): goto ma765; case ((uint8_t)15ULL): goto ma765; case ((uint8_t)16ULL): goto ma763; case ((uint8_t)17ULL): goto ma765; case ((uint8_t)18ULL): goto ma765; case ((uint8_t)19ULL): goto ma765; case ((uint8_t)20ULL): goto ma765; case ((uint8_t)21ULL): goto ma765; case ((uint8_t)22ULL): goto ma765; case ((uint8_t)23ULL): goto ma765; case ((uint8_t)24ULL): goto ma765; case ((uint8_t)25ULL): goto ma765; case ((uint8_t)26ULL): goto ma765; default: goto ma765; }
ma763:;
                uint8_t t769 = GM_LD(uint8_t, (((uint8_t *)&t761) + 0));
                if (t769 != ((uint8_t)16ULL)) goto mn768;
                void * t770 = gm_l5;
                gm_tob1snuii5bme t771;
                uint64_t t772 = gm_l4;
                GM_ST(uint64_t, (uint8_t *)&t771 + 0, t772);
                void * t773 = gm_l0;
                uint32_t t774 = gm_l67;
                gm_b16_8 t775 = gm_f_addr_name_g5lrlt3((void *)NULL, cx, NULL, t773, t774);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tnpifv20irnph t776;
                GM_ST(gm_b16_8, ((uint8_t *)&t776 + 0), t775);
                gm_tabb710d9mq2e t777;
                gm_f_format_be3fee1((void *)&t777, (void *)NULL, cx, NULL, t776);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t778 = t777;
                if (((((uint8_t *)&t778))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t778) + 8, gm_srcuro7l7pd15c2, 257, 69);
                gm_b16_8 t779 = GM_LD(gm_b16_8, (((uint8_t *)&t778) + 8));
                gm_b16_8 t780 = t779;
                gm_b16_8 t781 = GM_LD(gm_b16_8, ((uint8_t *)&t780));
                GM_ST(gm_b16_8, (uint8_t *)&t771 + 8, t781);
                GM_ST(uint64_t, (uint8_t *)&t771 + 24, ((uint64_t)0ULL));
                gm_tftb1odc739t5 t782 = gm_f_push_vgtfcjf(t770, &t771);
                gm_tftb1odc739t5 t783 = t782;
                if (((((uint8_t *)&t783))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t783) + 1, gm_srcuro7l7pd15c2, 257, 30);
                gm_b0_1 t784 = GM_LD(gm_b0_1, (((uint8_t *)&t783) + 1));
                goto me762;
mn768:;
ma764:;
                uint8_t t786 = GM_LD(uint8_t, (((uint8_t *)&t761) + 0));
                if (t786 != ((uint8_t)13ULL)) goto mn785;
                void * t787 = gm_l5;
                gm_tob1snuii5bme t788;
                uint64_t t789 = gm_l4;
                GM_ST(uint64_t, (uint8_t *)&t788 + 0, t789);
                void * t790 = gm_l0;
                uint32_t t791 = gm_l67;
                gm_b16_8 t792 = gm_f_addr_name_g5lrlt3((void *)NULL, cx, NULL, t790, t791);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tnpifv20irnph t793;
                GM_ST(gm_b16_8, ((uint8_t *)&t793 + 0), t792);
                gm_tabb710d9mq2e t794;
                gm_f_format_be3fee1((void *)&t794, (void *)NULL, cx, NULL, t793);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t795 = t794;
                if (((((uint8_t *)&t795))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t795) + 8, gm_srcuro7l7pd15c2, 258, 72);
                gm_b16_8 t796 = GM_LD(gm_b16_8, (((uint8_t *)&t795) + 8));
                gm_b16_8 t797 = t796;
                gm_b16_8 t798 = GM_LD(gm_b16_8, ((uint8_t *)&t797));
                GM_ST(gm_b16_8, (uint8_t *)&t788 + 8, t798);
                GM_ST(uint64_t, (uint8_t *)&t788 + 24, ((uint64_t)0ULL));
                gm_tftb1odc739t5 t799 = gm_f_push_vgtfcjf(t787, &t788);
                gm_tftb1odc739t5 t800 = t799;
                if (((((uint8_t *)&t800))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t800) + 1, gm_srcuro7l7pd15c2, 258, 33);
                gm_b0_1 t801 = GM_LD(gm_b0_1, (((uint8_t *)&t800) + 1));
                goto me762;
mn785:;
ma765:;
                gm_tl874t2atnlh3 t803 = GM_LD(gm_tl874t2atnlh3, gm_cejn1dj0054);
                gm_b16_8 t804;
                GM_ST(void *, (uint8_t *)&t804, (void *)(gm_sbe7f0bkvca75));
                GM_ST(uintptr_t, (uint8_t *)&t804 + 8, (uintptr_t)(42));
                gm_t08olq97rscn3 t805;
                gm_f_unsupported_531q51r((void *)&t805, (void *)NULL, cx, NULL, t803, t804);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tqgtd5gtgj65s t806;
                GM_ST(uint8_t, (uint8_t *)&t806 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t806 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t806 + 4, 0);
                ((uint8_t *)&t806)[0] = 1;
                GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t806 + 8), t805);
                gm_tqgtd5gtgj65s t807 = t806;
                uint8_t t808 = ((((uint8_t *)&t807))[0] != 0);
                F.ret = t806;
                goto gm_fret;
                goto me762;
mn802:;
ma766:;
                gm_trap("no match arm matched the scrutinee", gm_srcuro7l7pd15c2, 256, 13);
me762:;
            }
            goto me3;
mn750:;
ma16:;
            uint8_t t812 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
            if (t812 != ((uint8_t)12ULL)) goto pa811;
            goto po810;
pa811:;
            uint8_t t814 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
            if (t814 != ((uint8_t)13ULL)) goto pa813;
            goto po810;
pa813:;
            goto mn809;
po810:;
            gm_tl874t2atnlh3 t815 = GM_LD(gm_tl874t2atnlh3, gm_cejn1dj0054);
            gm_b16_8 t816;
            GM_ST(void *, (uint8_t *)&t816, (void *)(gm_sbbavs945cjhp));
            GM_ST(uintptr_t, (uint8_t *)&t816 + 8, (uintptr_t)(31));
            gm_t08olq97rscn3 t817;
            gm_f_unsupported_531q51r((void *)&t817, (void *)NULL, cx, NULL, t815, t816);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tqgtd5gtgj65s t818;
            GM_ST(uint8_t, (uint8_t *)&t818 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t818 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t818 + 4, 0);
            ((uint8_t *)&t818)[0] = 1;
            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t818 + 8), t817);
            gm_tqgtd5gtgj65s t819 = t818;
            uint8_t t820 = ((((uint8_t *)&t819))[0] != 0);
            F.ret = t818;
            goto gm_fret;
            goto me3;
mn809:;
ma17:;
            gm_trap("no match arm matched the scrutinee", gm_srcuro7l7pd15c2, 162, 5);
me3:;
        }
    }
    gm_b0_1 t821;
    gm_tqgtd5gtgj65s t822;
    memset(&t822, 0, sizeof t822);
    ((uint8_t *)&t822)[0] = 0;
    GM_ST(gm_b0_1, ((uint8_t *)&t822 + 8), t821);
    gm_tqgtd5gtgj65s t823 = t822;
    F.ret = t823;
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
