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
typedef struct gm_cskjv6t053tkje { uint32_t f0; uint64_t f1; } gm_cskjv6t053tkje;
typedef struct { uint64_t f0; uint64_t f1; } gm_t2lqu8jsj511d;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint64_t f0; } gm_t4ce0jd3tdro4;
typedef struct { uint8_t f0; uint8_t f1; uint8_t f2; uint8_t f3; uint8_t f4; uint8_t f5; } gm_t4q2b5ibsrdik;
typedef struct { int64_t f0; uint8_t f1; uint8_t pe[7]; } gm_t913bb735pvt5;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint32_t f0; uint8_t p1[4]; uint64_t f1; } gm_tb31k5l794t7p;
typedef struct { uint32_t f0; uint32_t f1; } gm_tcc1rq4n1avb2;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint8_t f0; uint8_t p1[3]; union { uint32_t v0; uint32_t v1; } f1; } gm_tj2m9dbh1ekv6;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { int64_t f0; } gm_tm98ap4112hee;
typedef struct { uint8_t f0; uint8_t f1; } gm_tqs5p4n2tq19v;
typedef struct { uint8_t f0; uint8_t p1[3]; union { uint32_t v0; uint8_t v1; } f1; } gm_tum2strs1ckr2;
_Static_assert(sizeof(gm_cseahvg8v7p0vs) == 12 && _Alignof(gm_cseahvg8v7p0vs) == 4 && offsetof(gm_cseahvg8v7p0vs, f0) == 0 && offsetof(gm_cseahvg8v7p0vs, f1) == 4 && offsetof(gm_cseahvg8v7p0vs, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_cskjv6t053tkje) == 16 && _Alignof(gm_cskjv6t053tkje) == 8 && offsetof(gm_cskjv6t053tkje, f0) == 0 && offsetof(gm_cskjv6t053tkje, f1) == 8, "layout of `Char`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; } gm_t0uuhaljsf410;
_Static_assert(sizeof(gm_t2lqu8jsj511d) == 16 && _Alignof(gm_t2lqu8jsj511d) == 8 && offsetof(gm_t2lqu8jsj511d, f0) == 0 && offsetof(gm_t2lqu8jsj511d, f1) == 8, "layout of `(u64, u64)`");
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { gm_tj2m9dbh1ekv6 f0; uint8_t f1; uint8_t pe[3]; } gm_t46lmulm8gaj3;
_Static_assert(sizeof(gm_t4ce0jd3tdro4) == 8 && _Alignof(gm_t4ce0jd3tdro4) == 8 && offsetof(gm_t4ce0jd3tdro4, f0) == 0, "layout of `Instant`");
_Static_assert(sizeof(gm_t4q2b5ibsrdik) == 6 && _Alignof(gm_t4q2b5ibsrdik) == 1 && offsetof(gm_t4q2b5ibsrdik, f0) == 0 && offsetof(gm_t4q2b5ibsrdik, f1) == 1 && offsetof(gm_t4q2b5ibsrdik, f2) == 2 && offsetof(gm_t4q2b5ibsrdik, f3) == 3 && offsetof(gm_t4q2b5ibsrdik, f4) == 4 && offsetof(gm_t4q2b5ibsrdik, f5) == 5, "layout of `OpenOptions`");
typedef struct { gm_tqs5p4n2tq19v f0; gm_tqs5p4n2tq19v f1; } gm_t5r6k0fdcfsb3;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
_Static_assert(sizeof(gm_t913bb735pvt5) == 16 && _Alignof(gm_t913bb735pvt5) == 8 && offsetof(gm_t913bb735pvt5, f0) == 0 && offsetof(gm_t913bb735pvt5, f1) == 8, "layout of `?i64`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t f1; } gm_taari1s0m51a7;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
_Static_assert(sizeof(gm_tb31k5l794t7p) == 16 && _Alignof(gm_tb31k5l794t7p) == 8 && offsetof(gm_tb31k5l794t7p, f0) == 0 && offsetof(gm_tb31k5l794t7p, f1) == 8, "layout of `Char`");
typedef struct { gm_b16_8 f0; uint32_t f1; uint32_t f2; gm_b16_8 f3; } gm_tb40qn7hk0tl9;
typedef struct { gm_t2lqu8jsj511d f0; uint8_t f1; uint8_t pe[7]; } gm_tc70a9dbbmgkg;
_Static_assert(sizeof(gm_tcc1rq4n1avb2) == 8 && _Alignof(gm_tcc1rq4n1avb2) == 4 && offsetof(gm_tcc1rq4n1avb2, f0) == 0 && offsetof(gm_tcc1rq4n1avb2, f1) == 4, "layout of `LineCol`");
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { gm_b16_8 f0; gm_t9qkh0rsooc8d f1; uint8_t pe[7]; } gm_tibupt4dmcqai;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; gm_tj2m9dbh1ekv6 v1; } f1; } gm_tij245bdr7emd;
_Static_assert(sizeof(gm_tj2m9dbh1ekv6) == 8 && _Alignof(gm_tj2m9dbh1ekv6) == 4 && offsetof(gm_tj2m9dbh1ekv6, f0) == 0 && offsetof(gm_tj2m9dbh1ekv6, f1) == 4, "layout of `IntSuffix`");
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_tm98ap4112hee) == 8 && _Alignof(gm_tm98ap4112hee) == 8 && offsetof(gm_tm98ap4112hee, f0) == 0, "layout of `File`");
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; void * v1; } f1; } gm_tq7s98ihonkju;
_Static_assert(sizeof(gm_tqs5p4n2tq19v) == 2 && _Alignof(gm_tqs5p4n2tq19v) == 1 && offsetof(gm_tqs5p4n2tq19v, f0) == 0 && offsetof(gm_tqs5p4n2tq19v, f1) == 1, "layout of `(u8, u8)`");
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { gm_tum2strs1ckr2 f0; uint8_t f1; uint8_t pe[3]; } gm_ts06tb4mldv43;
_Static_assert(sizeof(gm_tum2strs1ckr2) == 8 && _Alignof(gm_tum2strs1ckr2) == 4 && offsetof(gm_tum2strs1ckr2, f0) == 0 && offsetof(gm_tum2strs1ckr2, f1) == 4, "layout of `Escape`");
_Static_assert(sizeof(gm_t0uuhaljsf410) == 64 && _Alignof(gm_t0uuhaljsf410) == 8 && offsetof(gm_t0uuhaljsf410, f0) == 0 && offsetof(gm_t0uuhaljsf410, f1) == 16 && offsetof(gm_t0uuhaljsf410, f2) == 32 && offsetof(gm_t0uuhaljsf410, f3) == 48, "layout of `(string, string, string, string)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; gm_t9qkh0rsooc8d v1; void * v2; void * v3; gm_tibupt4dmcqai v4; gm_b16_8 v5; gm_b16_8 v6; gm_t9qkh0rsooc8d v7; } f1; } gm_t3fcbuehh3bs2;
_Static_assert(sizeof(gm_t46lmulm8gaj3) == 12 && _Alignof(gm_t46lmulm8gaj3) == 4 && offsetof(gm_t46lmulm8gaj3, f0) == 0 && offsetof(gm_t46lmulm8gaj3, f1) == 8, "layout of `?IntSuffix`");
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t p3[4]; gm_trqfvmjtt4sij f3; gm_trqfvmjtt4sij f4; gm_trqfvmjtt4sij f5; gm_th2lqbo77pvsc f6; } gm_t4gut2c9fj1kt;
_Static_assert(sizeof(gm_t5r6k0fdcfsb3) == 4 && _Alignof(gm_t5r6k0fdcfsb3) == 1 && offsetof(gm_t5r6k0fdcfsb3, f0) == 0 && offsetof(gm_t5r6k0fdcfsb3, f1) == 2, "layout of `(?u8, ?u8)`");
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_taari1s0m51a7) == 2 && _Alignof(gm_taari1s0m51a7) == 1 && offsetof(gm_taari1s0m51a7, f0) == 0 && offsetof(gm_taari1s0m51a7, f1) == 1, "layout of `?UnOp`");
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
_Static_assert(sizeof(gm_tb40qn7hk0tl9) == 40 && _Alignof(gm_tb40qn7hk0tl9) == 8 && offsetof(gm_tb40qn7hk0tl9, f0) == 0 && offsetof(gm_tb40qn7hk0tl9, f1) == 16 && offsetof(gm_tb40qn7hk0tl9, f2) == 20 && offsetof(gm_tb40qn7hk0tl9, f3) == 24, "layout of `SourceLocation`");
typedef struct { uint8_t f0; uint8_t p1[7]; gm_trqfvmjtt4sij f1; } gm_tbci5ospk5nnn;
_Static_assert(sizeof(gm_tc70a9dbbmgkg) == 24 && _Alignof(gm_tc70a9dbbmgkg) == 8 && offsetof(gm_tc70a9dbbmgkg, f0) == 0 && offsetof(gm_tc70a9dbbmgkg, f1) == 16, "layout of `?(u64, u64)`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
_Static_assert(sizeof(gm_tibupt4dmcqai) == 24 && _Alignof(gm_tibupt4dmcqai) == 8 && offsetof(gm_tibupt4dmcqai, f0) == 0 && offsetof(gm_tibupt4dmcqai, f1) == 16, "layout of `StrLit`");
_Static_assert(sizeof(gm_tij245bdr7emd) == 24 && _Alignof(gm_tij245bdr7emd) == 8 && offsetof(gm_tij245bdr7emd, f0) == 0 && offsetof(gm_tij245bdr7emd, f1) == 8, "layout of `string!IntSuffix`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
_Static_assert(sizeof(gm_tq7s98ihonkju) == 16 && _Alignof(gm_tq7s98ihonkju) == 8 && offsetof(gm_tq7s98ihonkju, f0) == 0 && offsetof(gm_tq7s98ihonkju, f1) == 8, "layout of `AllocError!^HandlerDecl`");
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_ts06tb4mldv43) == 12 && _Alignof(gm_ts06tb4mldv43) == 4 && offsetof(gm_ts06tb4mldv43, f0) == 0 && offsetof(gm_ts06tb4mldv43, f1) == 8, "layout of `?Escape`");
_Static_assert(sizeof(gm_t3fcbuehh3bs2) == 32 && _Alignof(gm_t3fcbuehh3bs2) == 8 && offsetof(gm_t3fcbuehh3bs2, f0) == 0 && offsetof(gm_t3fcbuehh3bs2, f1) == 8, "layout of `TokenKind`");
_Static_assert(sizeof(gm_t4gut2c9fj1kt) == 184 && _Alignof(gm_t4gut2c9fj1kt) == 8 && offsetof(gm_t4gut2c9fj1kt, f0) == 0 && offsetof(gm_t4gut2c9fj1kt, f1) == 8 && offsetof(gm_t4gut2c9fj1kt, f2) == 24 && offsetof(gm_t4gut2c9fj1kt, f3) == 40 && offsetof(gm_t4gut2c9fj1kt, f4) == 80 && offsetof(gm_t4gut2c9fj1kt, f5) == 120 && offsetof(gm_t4gut2c9fj1kt, f6) == 160, "layout of `Diagnostic`");
typedef struct { gm_tbci5ospk5nnn f0; gm_t46lmulm8gaj3 f1; uint8_t f2; uint8_t pe[3]; } gm_ta42iqot1auae;
_Static_assert(sizeof(gm_tbci5ospk5nnn) == 48 && _Alignof(gm_tbci5ospk5nnn) == 8 && offsetof(gm_tbci5ospk5nnn, f0) == 0 && offsetof(gm_tbci5ospk5nnn, f1) == 8, "layout of `Int`");
typedef struct { gm_tbci5ospk5nnn f0; gm_tbci5ospk5nnn f1; } gm_tngtuiuskn938;
_Static_assert(sizeof(gm_ta42iqot1auae) == 64 && _Alignof(gm_ta42iqot1auae) == 8 && offsetof(gm_ta42iqot1auae, f0) == 0 && offsetof(gm_ta42iqot1auae, f1) == 48 && offsetof(gm_ta42iqot1auae, f2) == 60, "layout of `IntLit`");
typedef struct { gm_tngtuiuskn938 f0; gm_taari1s0m51a7 f1; uint8_t f2; uint8_t pe[5]; } gm_tghmb53v8qi1e;
_Static_assert(sizeof(gm_tngtuiuskn938) == 96 && _Alignof(gm_tngtuiuskn938) == 8 && offsetof(gm_tngtuiuskn938, f0) == 0 && offsetof(gm_tngtuiuskn938, f1) == 48, "layout of `Rat`");
_Static_assert(sizeof(gm_tghmb53v8qi1e) == 104 && _Alignof(gm_tghmb53v8qi1e) == 8 && offsetof(gm_tghmb53v8qi1e, f0) == 0 && offsetof(gm_tghmb53v8qi1e, f1) == 96 && offsetof(gm_tghmb53v8qi1e, f2) == 98, "layout of `FloatLit`");
struct gm_fr_add_note_46v502s {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_all_digits_e77c2k1 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_at_exponent_qp4713f {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_byte_b4v8vul {
    gm_frame h;
    gm_tqs5p4n2tq19v ret;
};
struct gm_fr_contains_gkki4kq {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_current_char_ctktsl2 {
    gm_frame h;
    gm_tb31k5l794t7p ret;
};
struct gm_fr_emit_char_1ips8a1 {
    gm_frame h;
    gm_ta42iqot1auae l3; /* lit */
    gm_b0_1 ret;
};
struct gm_fr_empty_slice_d914i5u {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ijtp9lo {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_error_p5r0d56 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_float_suffix_lh8qbg0 {
    gm_frame h;
    gm_taari1s0m51a7 ret;
};
struct gm_fr_float_value_ip18r0p {
    gm_frame h;
    gm_trqfvmjtt4sij l11; /* digits */
    gm_tbci5ospk5nnn l18; /* mantissa */
    gm_tngtuiuskn938 ret;
};
struct gm_fr_fraction_follows_ivbj04h {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_imag_width_9gtosab {
    gm_frame h;
    gm_taari1s0m51a7 ret;
};
struct gm_fr_int_suffix_j594l00 {
    gm_frame h;
    gm_tij245bdr7emd ret;
};
struct gm_fr_int_value_cnmrbr4 {
    gm_frame h;
    gm_tbci5ospk5nnn ret;
};
struct gm_fr_is_digit_2hoggnj {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_hex_digit_9lads51 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_imag_suffix_46gu23m {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_lex_char_4qk4vjp {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_lex_escape_7pnp2v0 {
    gm_frame h;
    gm_ts06tb4mldv43 ret;
};
struct gm_fr_lex_field_index_pf905g6 {
    gm_frame h;
    gm_tbci5ospk5nnn l2; /* value */
    gm_b0_1 ret;
};
struct gm_fr_lex_number_9j9sdfi {
    gm_frame h;
    gm_t4gut2c9fj1kt l22; /* d */
    gm_tngtuiuskn938 l24; /* value */
    gm_tbci5ospk5nnn l25; /* value */
    gm_b0_1 ret;
};
struct gm_fr_lex_raw_string_uf6bd4g {
    gm_frame h;
    gm_trqfvmjtt4sij l3; /* bytes */
    gm_t4gut2c9fj1kt l4; /* d */
    gm_b0_1 ret;
};
struct gm_fr_lex_string_bcdbdjm {
    gm_frame h;
    gm_trqfvmjtt4sij l3; /* bytes */
    gm_b0_1 ret;
};
struct gm_fr_lex_unicode_escape_ns81oou {
    gm_frame h;
    gm_ts06tb4mldv43 ret;
};
struct gm_fr_list_go2qg0b {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_uvfu1k1 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_lone_cr_ktb0jvm {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_parse_valid_9fjef3j {
    gm_frame h;
    gm_tbci5ospk5nnn ret;
};
struct gm_fr_peek_72bostn {
    gm_frame h;
    gm_tqs5p4n2tq19v ret;
};
struct gm_fr_peek_at_74p8lkq {
    gm_frame h;
    gm_tqs5p4n2tq19v ret;
};
struct gm_fr_push_utf8_bjq6ubs {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_radix_name_5kts8t0 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_report_eardf07 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_scan_digits_jhpsv6j {
    gm_frame h;
    gm_t2lqu8jsj511d ret;
};
struct gm_fr_scan_exponent_20o8k8o {
    gm_frame h;
    int64_t ret;
};
struct gm_fr_skip_to_closing_quote_qmb6ii3 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_span_64igq5o {
    gm_frame h;
    gm_tl874t2atnlh3 ret;
};
struct gm_fr_span_tg7cev6 {
    gm_frame h;
    gm_tl874t2atnlh3 ret;
};
struct gm_fr_starts_with_6vvt01e {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_unterminated_at_line_end_v8hbf1f {
    gm_frame h;
    gm_t4gut2c9fj1kt l2; /* d */
    gm_b0_1 ret;
};
struct gm_fr_zero_s25l990 {
    gm_frame h;
    gm_tbci5ospk5nnn ret;
};
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b);
inline bool gm_eq467d5cm1d5(const uint8_t *a, const uint8_t *b);
inline bool gm_eqb44cb3cf1n(const uint8_t *a, const uint8_t *b);
inline bool gm_eqjmccqdb90f(const uint8_t *a, const uint8_t *b);
inline bool gm_eqv3tj1pu43g(const uint8_t *a, const uint8_t *b);
__attribute__((aligned(4))) gm_b0_1 gm_f_add_fix_0gkcr4c(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_tl874t2atnlh3 a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) void gm_f_add_note_46v502s(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) uint8_t gm_f_all_digits_e77c2k1(gm_b16_8 a0);
__attribute__((aligned(4))) uint8_t gm_f_at_exponent_qp4713f(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) uint64_t gm_f_backtick_run_l95fb09(gm_b16_8 a0, uint64_t a1);
inline __attribute__((aligned(4))) gm_tqs5p4n2tq19v gm_f_byte_b4v8vul(void * a0, uint64_t a1);
inline __attribute__((aligned(4))) uint8_t gm_f_contains_gkki4kq(gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_cskjv6t053tkje gm_f_current_char_ctktsl2(void * a0);
__attribute__((aligned(4))) gm_cskjv6t053tkje gm_f_decode_jrk6b03(gm_b16_8 a0, uint64_t a1);
__attribute__((aligned(4))) uint32_t gm_f_digit_value_8ogbrqp(uint8_t a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_emit_char_1ips8a1(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, uint64_t a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_emit_ekvv703(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, uint64_t a2, uint64_t a3);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_d914i5u(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
__attribute__((aligned(4))) uint8_t gm_f_ends_with_biriu5l(gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_error_p5r0d56(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, uint64_t a2, gm_b16_8 a3);
__attribute__((aligned(4))) void gm_f_error_s6hn871(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_find_nab29co(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_taari1s0m51a7 gm_f_float_suffix_lh8qbg0(gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_float_value_ip18r0p(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_t2lqu8jsj511d a2, const void * a3, int64_t a4);
__attribute__((aligned(4))) void gm_f_format_16f92bp(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_1kivsu4(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_3rm05av(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_51h4888(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_78j0ecp(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_b6aggpq(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_convcvp(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_ek7crui(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_eue6enu(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_g6sh9k2(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_ikusttp(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
__attribute__((aligned(4))) void gm_f_format_ooibcq2(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_ufd866t(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tm98ap4112hee a0);
__attribute__((aligned(4))) void gm_f_format_v5s6luj(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) uint8_t gm_f_fraction_follows_ivbj04h(void * a0, uint32_t a1, uint64_t a2);
__attribute__((aligned(4))) void gm_f_from_u64_ud13q5v(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint64_t a0);
__attribute__((aligned(4))) gm_taari1s0m51a7 gm_f_imag_width_9gtosab(gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_int_suffix_j594l00(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) void gm_f_int_value_cnmrbr4(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t2lqu8jsj511d a1, uint32_t a2);
inline __attribute__((aligned(4))) uint8_t gm_f_is_digit_2hoggnj(uint8_t a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_hex_digit_9lads51(uint8_t a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_imag_suffix_46gu23m(gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_lex_char_4qk4vjp(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_ts06tb4mldv43 gm_f_lex_escape_7pnp2v0(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_lex_field_index_pf905g6(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_lex_number_9j9sdfi(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_lex_raw_string_uf6bd4g(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_lex_string_bcdbdjm(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_t9qkh0rsooc8d a2);
__attribute__((aligned(4))) gm_ts06tb4mldv43 gm_f_lex_unicode_escape_ns81oou(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
inline __attribute__((aligned(4))) void gm_f_list_go2qg0b(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_lone_cr_ktb0jvm(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_j2spmhj(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_q1fs5ps(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_parse_fbta82r(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, uint32_t a1);
__attribute__((aligned(4))) void gm_f_parse_valid_9fjef3j(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, uint32_t a1);
inline __attribute__((aligned(4))) gm_tqs5p4n2tq19v gm_f_peek_72bostn(void * a0);
inline __attribute__((aligned(4))) gm_tqs5p4n2tq19v gm_f_peek_at_74p8lkq(void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_3ef746b(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_push_o4ckdr1(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_t83rg2m(void * a0, uint8_t a1);
__attribute__((aligned(4))) void gm_f_push_utf8_bjq6ubs(void * a0, uint32_t a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_radix_name_5kts8t0(uint32_t a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_report_eardf07(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_scaled_ufcgnm9(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, uint32_t a1, int64_t a2);
__attribute__((aligned(4))) gm_t2lqu8jsj511d gm_f_scan_digits_jhpsv6j(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, void * a2);
__attribute__((aligned(4))) int64_t gm_f_scan_exponent_20o8k8o(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_scan_ident_h1b2n4u(void * a0);
__attribute__((aligned(4))) uint8_t gm_f_skip_to_closing_quote_qmb6ii3(void * a0);
inline __attribute__((aligned(4))) gm_cseahvg8v7p0vs gm_f_span_64igq5o(uint32_t a0, uint64_t a1, uint64_t a2);
inline __attribute__((aligned(4))) gm_cseahvg8v7p0vs gm_f_span_tg7cev6(void * a0, uint64_t a1, uint64_t a2);
inline __attribute__((aligned(4))) uint8_t gm_f_starts_with_6vvt01e(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_unterminated_at_line_end_v8hbf1f(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
inline __attribute__((aligned(4))) void gm_f_zero_s25l990(void *gm_out, void *env, gm_node *cx, gm_node *inst);
extern uint8_t gm_c644mb9o5bq[16] __attribute__((aligned(8)));
extern uint8_t gm_cobksqr67t4[8] __attribute__((aligned(8)));
extern uint8_t gm_k2kjji37v2m[40] __attribute__((aligned(8)));
extern uint8_t gm_kdmv1ea52os[40] __attribute__((aligned(8)));
extern uint8_t gm_khsmm7kmq9o[40] __attribute__((aligned(8)));
extern uint8_t gm_knasprnpdnf[40] __attribute__((aligned(8)));
extern const uint8_t gm_s0i3i86nphpsh[6];
extern const uint8_t gm_s15p9kmrsfogp[2];
extern const uint8_t gm_s32vm0u5vgiu5[18];
extern const uint8_t gm_s4hf5j4eu72mv[66];
extern const uint8_t gm_s50pmptf3idds[2];
extern const uint8_t gm_s5frmv5tebja4[50];
extern const uint8_t gm_s5k9ll19r7m4g[4];
extern const uint8_t gm_s6groq37ram3h[6];
extern const uint8_t gm_s8e72gdumihb1[7];
extern const uint8_t gm_sae23ckujbpp9[107];
extern const uint8_t gm_safnb563bt6ue[12];
extern const uint8_t gm_sajhc6og5bv8v[59];
extern const uint8_t gm_saoa4m7dh2vl3[55];
extern const uint8_t gm_sb6jpc12sa7uj[4];
extern const uint8_t gm_sc80lo6tf645h[31];
extern const uint8_t gm_se0f2jka437t4[2];
extern const uint8_t gm_se4629k8oa1uo[2];
extern const uint8_t gm_sebojpbfjfgaf[3];
extern const uint8_t gm_sfd839bbgsa7s[63];
extern const uint8_t gm_sfdbfrv46lihd[2];
extern const uint8_t gm_sg550i4fp569j[55];
extern const uint8_t gm_sh6hkhojsmti9[48];
extern const uint8_t gm_shnig5tqtdedi[4];
extern const uint8_t gm_shq8kvmdjhmfg[4];
extern const uint8_t gm_sikg061j3im7q[2];
extern const uint8_t gm_sil0do0eri7f7[44];
extern const uint8_t gm_sjblck9bpb6r8[28];
extern const uint8_t gm_skmai4uvseje8[24];
extern const uint8_t gm_sm9e81djuncnu[4];
extern const uint8_t gm_smit2phoauonm[61];
extern const uint8_t gm_sovc43d45he5a[21];
extern const uint8_t gm_sq9ngpqr0m7ek[31];
extern const uint8_t gm_sqhghq789nvm0[8];
extern const uint8_t gm_sqj9769nr8pm0[48];
extern const uint8_t gm_sqqajgod2bb4o[4];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_src3d0v42kavhv7[];
extern const char gm_srcc24pnihhgkug[];
extern const char gm_srcgfglnv8p059r[];
extern const char gm_srcrgc9bkquf6nr[];
extern const char gm_srct4c97p4hek1p[];
extern const char gm_srcvmmksi0829pa[];
extern const uint8_t gm_srnee0q49ttns[32];
extern const uint8_t gm_su30pqbigkn5m[6];
extern const uint8_t gm_su7ft5rc766n4[3];
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
inline bool gm_eqb44cb3cf1n(const uint8_t *a, const uint8_t *b) {
    if (a[1] != b[1]) return false;
    return !a[1] || gm_eqjmccqdb90f(a, b);
}
inline bool gm_eqjmccqdb90f(const uint8_t *a, const uint8_t *b) {
    return GM_LD(uint8_t, a) == GM_LD(uint8_t, b);
}
inline bool gm_eqv3tj1pu43g(const uint8_t *a, const uint8_t *b) {
    uint8_t ta = GM_LD(uint8_t, a + 0), tb = GM_LD(uint8_t, b + 0);
    if (ta != tb) return false;
    return true;
}
inline __attribute__((aligned(4))) void gm_f_add_note_46v502s(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_add_note_46v502s F;
    void * gm_l0; /* d */
    gm_b16_8 gm_l1; /* note */
    GM_STACK_CHECK(&F, 58u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        void * t3 = (void *)(((uint8_t *)t2) + 80);
        gm_b16_8 t4 = gm_l1;
        gm_tftb1odc739t5 t5 = gm_f_push_3ef746b(t3, t4);
        if (((((uint8_t *)&t5))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t5) + 1, gm_srcgfglnv8p059r, 96, 5);
        gm_b0_1 t6 = GM_LD(gm_b0_1, (((uint8_t *)&t5) + 1));
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) gm_tqs5p4n2tq19v gm_f_byte_b4v8vul(void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_byte_b4v8vul F;
    void * gm_l0; /* lx */
    uint64_t gm_l1; /* i */
    GM_STACK_CHECK_SHALLOW(&F, 83u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    gm_b16_8 t2 = GM_LD(gm_b16_8, (((uint8_t *)t1) + 8));
    void * t3 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t2)));
    uint64_t t4 = (uintptr_t)GM_LD(uintptr_t, ((uint8_t *)&t2) + 8);
    uint64_t t5 = gm_l1;
    uint64_t t6 = (uintptr_t)t5;
    gm_tqs5p4n2tq19v t7;
    if (t6 < t4) {
    uint8_t t8 = GM_LD(uint8_t, ((uint8_t *)t3 + t6 * 1));
    gm_tqs5p4n2tq19v t9;
    GM_ST(uint8_t, ((uint8_t *)&t9), t8);
    ((uint8_t *)&t9)[1] = 1;
        t7 = t9;
    } else {
    gm_tqs5p4n2tq19v t10;
    memset(&t10, 0, sizeof t10);
        t7 = t10;
    }
    F.ret = t7;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_contains_gkki4kq(gm_b16_8 a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_contains_gkki4kq F;
    gm_b16_8 gm_l0; /* s */
    gm_b16_8 gm_l1; /* needle */
    GM_STACK_CHECK_SHALLOW(&F, 83u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_b16_8 t1 = gm_l0;
    gm_b16_8 t2 = gm_l1;
    gm_tf5srh4mp4b3i t3 = gm_f_find_nab29co(t1, t2);
    uint8_t t4;
    if (((((uint8_t *)&t3))[8] != 0)) goto mn6;
    t4 = ((uint8_t)0);
    goto me5;
mn6:;
    t4 = ((uint8_t)1);
    goto me5;
mn7:;
    gm_trap("no match arm matched the scrutinee", gm_src3d0v42kavhv7, 84, 50);
me5:;
    F.ret = t4;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_cskjv6t053tkje gm_f_current_char_ctktsl2(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_current_char_ctktsl2 F;
    void * gm_l0; /* lx */
    GM_STACK_CHECK_SHALLOW(&F, 96u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    gm_b16_8 t2 = GM_LD(gm_b16_8, (((uint8_t *)t1) + 8));
    void * t3 = gm_l0;
    uint64_t t4 = GM_LD(uint64_t, (((uint8_t *)t3) + 24));
    gm_tb31k5l794t7p t5 = GM_CONV(gm_tb31k5l794t7p, gm_f_decode_jrk6b03(t2, t4));
    F.ret = t5;
gm_fret:
    return GM_CONV(gm_cskjv6t053tkje, F.ret);
gm_fprop:
    return GM_CONV(gm_cskjv6t053tkje, F.ret);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_error_p5r0d56(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, uint64_t a2, gm_b16_8 a3) {
    struct gm_fr_error_p5r0d56 F;
    void * gm_l0; /* lx */
    uint64_t gm_l1; /* lo */
    uint64_t gm_l2; /* hi */
    gm_b16_8 gm_l3; /* message */
    GM_STACK_CHECK(&F, 108u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        void * t2 = gm_l0;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 32));
        void * t4 = gm_l0;
        uint64_t t5 = gm_l1;
        uint64_t t6 = gm_l2;
        gm_tl874t2atnlh3 t7 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_tg7cev6(t4, t5, t6));
        gm_b16_8 t8 = gm_l3;
        gm_b0_1 t9 = gm_f_report_eardf07((void *)NULL, cx, NULL, t3, t7, t8);
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
inline __attribute__((aligned(4))) void gm_f_int_value_cnmrbr4(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t2lqu8jsj511d a1, uint32_t a2) {
    struct gm_fr_int_value_cnmrbr4 F;
    void * gm_l0; /* lx */
    gm_t2lqu8jsj511d gm_l1; /* digits */
    uint32_t gm_l2; /* radix */
    GM_STACK_CHECK(&F, 104u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        uint64_t t3 = GM_LD(uintptr_t, (((uint8_t *)t2) + 8) + 8);
        void * t4 = ((uint8_t *)GM_LD(void *, (((uint8_t *)t2) + 8)));
        uint64_t t5 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 0));
        uint64_t t6 = (uintptr_t)t5;
        uint64_t t7 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
        uint64_t t8 = (uintptr_t)t7;
        if (t6 > t8 || t8 > (uintptr_t)t3) gm_trap_slice(t6, t8, t3, gm_srct4c97p4hek1p, 226, 24);
        gm_b16_8 t9;
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t4 + t6 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t9 + 8, (uintptr_t)(t8 - t6));
        uint32_t t10 = gm_l2;
        gm_f_parse_valid_9fjef3j((void *)&(*(gm_tbci5ospk5nnn *)gm_out), (void *)NULL, cx, NULL, t9, t10);
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_hex_digit_9lads51(uint8_t a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_hex_digit_9lads51 F;
    uint8_t gm_l0; /* c */
    GM_STACK_CHECK_SHALLOW(&F, 17u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t t1 = 0;
    uint8_t t2 = 0;
    uint8_t t3 = gm_l0;
    uint8_t t4 = gm_f_is_digit_2hoggnj(t3);
    if (!t4) {
        uint8_t t5 = 0;
        uint8_t t6 = gm_l0;
        uint8_t t7 = (t6 >= ((uint8_t)97ULL));
        if (t7) {
            uint8_t t8 = gm_l0;
            uint8_t t9 = (t8 <= ((uint8_t)102ULL));
            t5 = t9;
        } else t5 = 0;
        t2 = t5;
    } else t2 = 1;
    if (!t2) {
        uint8_t t10 = 0;
        uint8_t t11 = gm_l0;
        uint8_t t12 = (t11 >= ((uint8_t)65ULL));
        if (t12) {
            uint8_t t13 = gm_l0;
            uint8_t t14 = (t13 <= ((uint8_t)70ULL));
            t10 = t14;
        } else t10 = 0;
        t1 = t10;
    } else t1 = 1;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_imag_suffix_46gu23m(gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_imag_suffix_46gu23m F;
    gm_b16_8 gm_l0; /* s */
    GM_STACK_CHECK_SHALLOW(&F, 164u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t1 = gm_l0;
    gm_t0uuhaljsf410 t2;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sikg061j3im7q));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(1));
    GM_ST(gm_b16_8, (uint8_t *)&t2 + 0, t3);
    gm_b16_8 t4;
    GM_ST(void *, (uint8_t *)&t4, (void *)(gm_sb6jpc12sa7uj));
    GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(3));
    GM_ST(gm_b16_8, (uint8_t *)&t2 + 16, t4);
    gm_b16_8 t5;
    GM_ST(void *, (uint8_t *)&t5, (void *)(gm_shq8kvmdjhmfg));
    GM_ST(uintptr_t, (uint8_t *)&t5 + 8, (uintptr_t)(3));
    GM_ST(gm_b16_8, (uint8_t *)&t2 + 32, t5);
    gm_b16_8 t6;
    GM_ST(void *, (uint8_t *)&t6, (void *)(gm_s5k9ll19r7m4g));
    GM_ST(uintptr_t, (uint8_t *)&t6 + 8, (uintptr_t)(3));
    GM_ST(gm_b16_8, (uint8_t *)&t2 + 48, t6);
    uint8_t t7 = 0;
    if (gm_eq0iqiubfm7e(((uint8_t *)&t2) + 0, ((uint8_t *)&t1))) t7 = 1;
    if (gm_eq0iqiubfm7e(((uint8_t *)&t2) + 16, ((uint8_t *)&t1))) t7 = 1;
    if (gm_eq0iqiubfm7e(((uint8_t *)&t2) + 32, ((uint8_t *)&t1))) t7 = 1;
    if (gm_eq0iqiubfm7e(((uint8_t *)&t2) + 48, ((uint8_t *)&t1))) t7 = 1;
    uint8_t t8 = (uint8_t)(t7);
    F.ret = t8;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_lone_cr_ktb0jvm(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_lone_cr_ktb0jvm F;
    void * gm_l0; /* lx */
    GM_STACK_CHECK(&F, 104u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        void * t3 = gm_l0;
        uint64_t t4 = GM_LD(uint64_t, (((uint8_t *)t3) + 24));
        void * t5 = gm_l0;
        uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)t5) + 24));
        uint64_t r7; bool o8 = __builtin_add_overflow(t6, ((uint64_t)1ULL), &r7);
        if (o8) gm_trap("integer overflow in `+`", gm_srcvmmksi0829pa, 198, 22);
        uint64_t t9 = r7;
        gm_b16_8 t10 = GM_LD(gm_b16_8, gm_c644mb9o5bq);
        gm_b0_1 t11 = gm_f_error_p5r0d56((void *)NULL, cx, NULL, t2, t4, t9, t10);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t12 = gm_l0;
        void * t13 = gm_l0;
        uint64_t t14 = GM_LD(uint64_t, (((uint8_t *)t13) + 24));
        uint64_t r15; bool o16 = __builtin_add_overflow(t14, ((uint64_t)1ULL), &r15);
        if (o16) gm_trap("integer overflow in `+`", gm_srcvmmksi0829pa, 199, 5);
        uint64_t t17 = r15;
        GM_ST(uint64_t, (((uint8_t *)t12) + 24), t17);
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
inline __attribute__((aligned(4))) gm_tqs5p4n2tq19v gm_f_peek_72bostn(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_peek_72bostn F;
    void * gm_l0; /* lx */
    GM_STACK_CHECK_SHALLOW(&F, 83u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    gm_b16_8 t2 = GM_LD(gm_b16_8, (((uint8_t *)t1) + 8));
    void * t3 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t2)));
    uint64_t t4 = (uintptr_t)GM_LD(uintptr_t, ((uint8_t *)&t2) + 8);
    void * t5 = gm_l0;
    uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)t5) + 24));
    uint64_t t7 = (uintptr_t)t6;
    gm_tqs5p4n2tq19v t8;
    if (t7 < t4) {
    uint8_t t9 = GM_LD(uint8_t, ((uint8_t *)t3 + t7 * 1));
    gm_tqs5p4n2tq19v t10;
    GM_ST(uint8_t, ((uint8_t *)&t10), t9);
    ((uint8_t *)&t10)[1] = 1;
        t8 = t10;
    } else {
    gm_tqs5p4n2tq19v t11;
    memset(&t11, 0, sizeof t11);
        t8 = t11;
    }
    F.ret = t8;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tqs5p4n2tq19v gm_f_peek_at_74p8lkq(void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_peek_at_74p8lkq F;
    void * gm_l0; /* lx */
    uint64_t gm_l1; /* n */
    GM_STACK_CHECK_SHALLOW(&F, 107u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    gm_b16_8 t2 = GM_LD(gm_b16_8, (((uint8_t *)t1) + 8));
    void * t3 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t2)));
    uint64_t t4 = (uintptr_t)GM_LD(uintptr_t, ((uint8_t *)&t2) + 8);
    void * t5 = gm_l0;
    uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)t5) + 24));
    uint64_t t7 = gm_l1;
    uint64_t r8; bool o9 = __builtin_add_overflow(t6, t7, &r8);
    if (o9) gm_trap("integer overflow in `+`", gm_srcvmmksi0829pa, 153, 64);
    uint64_t t10 = r8;
    uint64_t t11 = (uintptr_t)t10;
    gm_tqs5p4n2tq19v t12;
    if (t11 < t4) {
    uint8_t t13 = GM_LD(uint8_t, ((uint8_t *)t3 + t11 * 1));
    gm_tqs5p4n2tq19v t14;
    GM_ST(uint8_t, ((uint8_t *)&t14), t13);
    ((uint8_t *)&t14)[1] = 1;
        t12 = t14;
    } else {
    gm_tqs5p4n2tq19v t15;
    memset(&t15, 0, sizeof t15);
        t12 = t15;
    }
    F.ret = t12;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_radix_name_5kts8t0(uint32_t a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_radix_name_5kts8t0 F;
    uint32_t gm_l0; /* radix */
    GM_STACK_CHECK_SHALLOW(&F, 132u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint32_t t1 = gm_l0;
    gm_b16_8 t2;
    uint32_t t5 = GM_LD(uint32_t, ((uint8_t *)&t1));
    if (!(t5 == ((uint32_t)2ULL))) goto mn4;
    gm_b16_8 t6;
    GM_ST(void *, (uint8_t *)&t6, (void *)(gm_s8e72gdumihb1));
    GM_ST(uintptr_t, (uint8_t *)&t6 + 8, (uintptr_t)(6));
    t2 = t6;
    goto me3;
mn4:;
    uint32_t t8 = GM_LD(uint32_t, ((uint8_t *)&t1));
    if (!(t8 == ((uint32_t)8ULL))) goto mn7;
    gm_b16_8 t9;
    GM_ST(void *, (uint8_t *)&t9, (void *)(gm_su30pqbigkn5m));
    GM_ST(uintptr_t, (uint8_t *)&t9 + 8, (uintptr_t)(5));
    t2 = t9;
    goto me3;
mn7:;
    uint32_t t11 = GM_LD(uint32_t, ((uint8_t *)&t1));
    if (!(t11 == ((uint32_t)10ULL))) goto mn10;
    gm_b16_8 t12;
    GM_ST(void *, (uint8_t *)&t12, (void *)(gm_sqhghq789nvm0));
    GM_ST(uintptr_t, (uint8_t *)&t12 + 8, (uintptr_t)(7));
    t2 = t12;
    goto me3;
mn10:;
    gm_b16_8 t14;
    GM_ST(void *, (uint8_t *)&t14, (void *)(gm_safnb563bt6ue));
    GM_ST(uintptr_t, (uint8_t *)&t14 + 8, (uintptr_t)(11));
    t2 = t14;
    goto me3;
mn13:;
    gm_trap("no match arm matched the scrutinee", gm_srct4c97p4hek1p, 507, 43);
me3:;
    F.ret = t2;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
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
inline __attribute__((aligned(4))) gm_cseahvg8v7p0vs gm_f_span_64igq5o(uint32_t a0, uint64_t a1, uint64_t a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_span_64igq5o F;
    uint32_t gm_l0; /* file */
    uint64_t gm_l1; /* lo */
    uint64_t gm_l2; /* hi */
    GM_STACK_CHECK_SHALLOW(&F, 84u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    uint32_t t1 = gm_l0;
    uint64_t t2 = gm_l1;
    uint32_t t3 = (uint32_t)t2;
    uint64_t t4 = gm_l2;
    uint32_t t5 = (uint32_t)t4;
    gm_tl874t2atnlh3 t6;
    GM_ST(uint32_t, ((uint8_t *)&t6 + 0), t1);
    GM_ST(uint32_t, ((uint8_t *)&t6 + 4), t3);
    GM_ST(uint32_t, ((uint8_t *)&t6 + 8), t5);
    F.ret = t6;
gm_fret:
    return GM_CONV(gm_cseahvg8v7p0vs, F.ret);
gm_fprop:
    return GM_CONV(gm_cseahvg8v7p0vs, F.ret);
}
inline __attribute__((aligned(4))) gm_cseahvg8v7p0vs gm_f_span_tg7cev6(void * a0, uint64_t a1, uint64_t a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_span_tg7cev6 F;
    void * gm_l0; /* lx */
    uint64_t gm_l1; /* lo */
    uint64_t gm_l2; /* hi */
    GM_STACK_CHECK_SHALLOW(&F, 88u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t1 = gm_l0;
    uint32_t t2 = GM_LD(uint32_t, (((uint8_t *)t1) + 0));
    uint64_t t3 = gm_l1;
    uint64_t t4 = gm_l2;
    gm_tl874t2atnlh3 t5 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_64igq5o(t2, t3, t4));
    F.ret = t5;
gm_fret:
    return GM_CONV(gm_cseahvg8v7p0vs, F.ret);
gm_fprop:
    return GM_CONV(gm_cseahvg8v7p0vs, F.ret);
}
inline __attribute__((aligned(4))) uint8_t gm_f_starts_with_6vvt01e(gm_b16_8 a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_starts_with_6vvt01e F;
    gm_b16_8 gm_l0; /* s */
    gm_b16_8 gm_l1; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 126u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint64_t t3 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        uint8_t t5 = (t3 > t4);
        if (!t5) goto ie2;
        {
            uint8_t t7 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        uint64_t t8 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        void * t9 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)));
        uint64_t t10 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t11 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint64_t t12 = (uintptr_t)t11;
        if (t10 > t12 || t12 > (uintptr_t)t8) gm_trap_slice(t10, t12, t8, gm_srcc24pnihhgkug, 416, 12);
        gm_b16_8 t13;
        GM_ST(void *, (uint8_t *)&t13, (void *)(((uint8_t *)t9 + t10 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t13 + 8, (uintptr_t)(t12 - t10));
        gm_b16_8 t14 = gm_l1;
        uint8_t t15 = (gm_eq0iqiubfm7e(((uint8_t *)&t13), ((uint8_t *)&t14)));
        uint8_t t16 = 0;
        F.ret = t15;
        goto gm_fret;
    }
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
inline __attribute__((aligned(4))) uint8_t gm_f_all_digits_e77c2k1(gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_all_digits_e77c2k1 F;
    gm_b16_8 gm_l0; /* s */
    gm_b16_8 gm_l1; /* <tmp> */
    uint64_t gm_l2; /* <tmp> */
    uint8_t gm_l3; /* c */
    GM_STACK_CHECK_SHALLOW(&F, 123u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        {
            gm_b16_8 t3 = gm_l0;
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
                    uint8_t t13 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t11 * 1));
                    uint8_t t16 = GM_LD(uint8_t, ((uint8_t *)&t13));
                    gm_l3 = t16;
                    {
                        uint8_t t19 = gm_l3;
                        uint8_t t20 = gm_f_is_digit_2hoggnj(t19);
                        uint8_t t21 = (uint8_t)!t20;
                        if (!t21) goto ie18;
                        {
                            uint8_t t23 = 0;
                            F.ret = ((uint8_t)0);
                            goto gm_fret;
                        }
                        goto ix18;
ie18:;
ix18:;
                    }
                    goto me14;
mn15:;
                    gm_trap("no match arm matched the scrutinee", gm_srct4c97p4hek1p, 515, 5);
me14:;
                }
lc4:;
                {
                    uint64_t t25 = gm_l2;
                    uint64_t r26; bool o27 = __builtin_add_overflow(t25, ((uint64_t)1ULL), &r26);
                    uint64_t t28 = r26;
                    gm_l2 = t28;
                }
            }
lb4:;
        }
    }
    {
        uint8_t t29 = 0;
        F.ret = ((uint8_t)1);
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_at_exponent_qp4713f(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_at_exponent_qp4713f F;
    void * gm_l0; /* lx */
    gm_b16_8 gm_l1; /* markers */
    uint8_t gm_l2; /* m */
    uint64_t gm_l3; /* i */
    uint8_t gm_l4; /* c */
    GM_STACK_CHECK_SHALLOW(&F, 137u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        gm_tqs5p4n2tq19v t3 = gm_f_peek_72bostn(t2);
        uint8_t t4;
        if (((((uint8_t *)&t3))[1] != 0)) {
            uint8_t t5 = GM_LD(uint8_t, (((uint8_t *)&t3) + 0));
            t4 = t5;
        } else {
            uint8_t t6 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        gm_l2 = t4;
    }
    {
        uint8_t t8 = gm_l2;
        gm_b16_8 t9 = gm_l1;
        uint8_t t10 = 0;
        for (uintptr_t i = 0; i < (uintptr_t)(GM_LD(uintptr_t, ((uint8_t *)&t9) + 8)); i++) if (gm_eqjmccqdb90f(((uint8_t *)GM_LD(void *, ((uint8_t *)&t9))) + i * 1, ((uint8_t *)&t8))) { t10 = 1; break; }
        uint8_t t11 = (uint8_t)(!t10);
        if (!t11) goto ie7;
        {
            uint8_t t13 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        goto ix7;
ie7:;
ix7:;
    }
    {
        gm_l3 = ((uint64_t)1ULL);
    }
    {
        void * t15 = gm_l0;
        uint64_t t16 = gm_l3;
        gm_tqs5p4n2tq19v t17 = gm_f_peek_at_74p8lkq(t15, t16);
        gm_tqs5p4n2tq19v t18;
        GM_ST(uint8_t, ((uint8_t *)&t18), ((uint8_t)43ULL));
        ((uint8_t *)&t18)[1] = 1;
        gm_tqs5p4n2tq19v t19;
        GM_ST(uint8_t, ((uint8_t *)&t19), ((uint8_t)45ULL));
        ((uint8_t *)&t19)[1] = 1;
        gm_t5r6k0fdcfsb3 t20;
        GM_ST(gm_tqs5p4n2tq19v, ((uint8_t *)&t20 + 0), t18);
        GM_ST(gm_tqs5p4n2tq19v, ((uint8_t *)&t20 + 2), t19);
        uint8_t t21 = 0;
        if (gm_eqb44cb3cf1n(((uint8_t *)&t20) + 0, ((uint8_t *)&t17))) t21 = 1;
        if (gm_eqb44cb3cf1n(((uint8_t *)&t20) + 2, ((uint8_t *)&t17))) t21 = 1;
        uint8_t t22 = (uint8_t)(t21);
        if (!t22) goto ie14;
        {
            uint64_t t24 = gm_l3;
            uint64_t r25; bool o26 = __builtin_add_overflow(t24, ((uint64_t)1ULL), &r25);
            if (o26) gm_trap("integer overflow in `+`", gm_srct4c97p4hek1p, 192, 38);
            uint64_t t27 = r25;
            gm_l3 = t27;
        }
        goto ix14;
ie14:;
ix14:;
    }
    {
        void * t28 = gm_l0;
        uint64_t t29 = gm_l3;
        gm_tqs5p4n2tq19v t30 = gm_f_peek_at_74p8lkq(t28, t29);
        uint8_t t31;
        if (((((uint8_t *)&t30))[1] != 0)) {
            uint8_t t32 = GM_LD(uint8_t, (((uint8_t *)&t30) + 0));
            t31 = t32;
        } else {
            uint8_t t33 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        gm_l4 = t31;
    }
    {
        uint8_t t34 = gm_l4;
        uint8_t t35 = gm_f_is_digit_2hoggnj(t34);
        uint8_t t36 = 0;
        F.ret = t35;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_emit_char_1ips8a1(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, uint64_t a2) {
    struct gm_fr_emit_char_1ips8a1 F;
    void * gm_l0; /* lx */
    uint32_t gm_l1; /* value */
    uint64_t gm_l2; /* lo */
    GM_STACK_CHECK(&F, 132u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        memset((uint8_t *)&F.l3 + 61, 0, 3);
        uint32_t t2 = gm_l1;
        uint64_t t3 = (uint64_t)t2;
        gm_f_from_u64_ud13q5v((void *)&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l3 + 0)), (void *)NULL, cx, NULL, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t46lmulm8gaj3 t4;
        memset(&t4, 0, sizeof t4);
        GM_ST(gm_t46lmulm8gaj3, (uint8_t *)&F.l3 + 48, t4);
        GM_ST(uint8_t, (uint8_t *)&F.l3 + 60, ((uint8_t)1));
    }
    {
        void * t5 = gm_l0;
        gm_tq7s98ihonkju t6 = gm_f_new_q1fs5ps((void *)NULL, cx, NULL, &(*(gm_ta42iqot1auae *)((uint8_t *)&F.l3)), &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kdmv1ea52os)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t6))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t6) + 8, gm_srct4c97p4hek1p, 321, 18);
        void * t7 = GM_LD(void *, (((uint8_t *)&t6) + 8));
        gm_t3fcbuehh3bs2 t8;
        GM_ST(uint8_t, (uint8_t *)&t8 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t8 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t8 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t8 + 16, 0);
        GM_ST(uint64_t, (uint8_t *)&t8 + 24, 0);
        GM_ST(uint8_t, ((uint8_t *)&t8 + 0), ((uint8_t)3ULL));
        GM_ST(void *, ((uint8_t *)&t8 + 8), t7);
        uint64_t t9 = gm_l2;
        void * t10 = gm_l0;
        uint64_t t11 = GM_LD(uint64_t, (((uint8_t *)t10) + 24));
        gm_b0_1 t12 = gm_f_emit_ekvv703((void *)NULL, cx, NULL, t5, &t8, t9, t11);
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
inline __attribute__((aligned(4))) gm_taari1s0m51a7 gm_f_float_suffix_lh8qbg0(gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_float_suffix_lh8qbg0 F;
    gm_b16_8 gm_l0; /* s */
    GM_STACK_CHECK_SHALLOW(&F, 33u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_taari1s0m51a7 t1;
    if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8) == 3) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l0)), gm_shnig5tqtdedi, 3) == 0)) goto mn3;
    gm_t9qkh0rsooc8d t4;
    GM_ST(uint8_t, ((uint8_t *)&t4 + 0), ((uint8_t)0ULL));
    gm_taari1s0m51a7 t5;
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t5), t4);
    ((uint8_t *)&t5)[1] = 1;
    t1 = t5;
    goto me2;
mn3:;
    if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8) == 3) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l0)), gm_sm9e81djuncnu, 3) == 0)) goto mn6;
    gm_t9qkh0rsooc8d t7;
    GM_ST(uint8_t, ((uint8_t *)&t7 + 0), ((uint8_t)1ULL));
    gm_taari1s0m51a7 t8;
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t8), t7);
    ((uint8_t *)&t8)[1] = 1;
    t1 = t8;
    goto me2;
mn6:;
    if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8) == 3) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l0)), gm_sqqajgod2bb4o, 3) == 0)) goto mn9;
    gm_t9qkh0rsooc8d t10;
    GM_ST(uint8_t, ((uint8_t *)&t10 + 0), ((uint8_t)2ULL));
    gm_taari1s0m51a7 t11;
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t11), t10);
    ((uint8_t *)&t11)[1] = 1;
    t1 = t11;
    goto me2;
mn9:;
    gm_taari1s0m51a7 t13;
    memset(&t13, 0, sizeof t13);
    t1 = t13;
    goto me2;
mn12:;
    gm_trap("no match arm matched the scrutinee", gm_srct4c97p4hek1p, 556, 50);
me2:;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) void gm_f_float_value_ip18r0p(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_t2lqu8jsj511d a2, const void * a3, int64_t a4) {
    struct gm_fr_float_value_ip18r0p F;
    void * gm_l0; /* lx */
    uint32_t gm_l1; /* radix */
    gm_t2lqu8jsj511d gm_l2; /* int_part */
    gm_tc70a9dbbmgkg gm_l3; /* frac_part */
    int64_t gm_l4; /* exp */
    gm_t2lqu8jsj511d gm_l5; /* frac */
    gm_b16_8 gm_l6; /* frac_text */
    int64_t gm_l7; /* frac_digits */
    gm_b16_8 gm_l8; /* <tmp> */
    uint64_t gm_l9; /* <tmp> */
    uint8_t gm_l10; /* c */
    gm_b16_8 gm_l12; /* <tmp> */
    uint64_t gm_l13; /* <tmp> */
    uint8_t gm_l14; /* c */
    gm_b16_8 gm_l15; /* <tmp> */
    uint64_t gm_l16; /* <tmp> */
    uint8_t gm_l17; /* c */
    GM_STACK_CHECK(&F, 741u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    memcpy(&gm_l3, a3, sizeof gm_l3);
    gm_l4 = a4;
    {
        gm_tc70a9dbbmgkg t2 = gm_l3;
        gm_t2lqu8jsj511d t3;
        if (((((uint8_t *)&t2))[16] != 0)) {
            gm_t2lqu8jsj511d t4 = GM_LD(gm_t2lqu8jsj511d, (((uint8_t *)&t2) + 0));
            t3 = t4;
        } else {
            gm_t2lqu8jsj511d t5;
            GM_ST(uint64_t, ((uint8_t *)&t5 + 0), ((uint64_t)0ULL));
            GM_ST(uint64_t, ((uint8_t *)&t5 + 8), ((uint64_t)0ULL));
            t3 = t5;
        }
        gm_l5 = t3;
    }
    {
        void * t6 = gm_l0;
        uint64_t t7 = GM_LD(uintptr_t, (((uint8_t *)t6) + 8) + 8);
        void * t8 = ((uint8_t *)GM_LD(void *, (((uint8_t *)t6) + 8)));
        uint64_t t9 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 0));
        uint64_t t10 = (uintptr_t)t9;
        uint64_t t11 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
        uint64_t t12 = (uintptr_t)t11;
        if (t10 > t12 || t12 > (uintptr_t)t7) gm_trap_slice(t10, t12, t7, gm_srct4c97p4hek1p, 238, 18);
        gm_b16_8 t13;
        GM_ST(void *, (uint8_t *)&t13, (void *)(((uint8_t *)t8 + t10 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t13 + 8, (uintptr_t)(t12 - t10));
        gm_l6 = t13;
    }
    {
        gm_l7 = ((int64_t)0LL);
    }
    {
        {
            gm_b16_8 t15 = gm_l6;
            gm_l8 = t15;
        }
        {
            gm_l9 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t17 = gm_l9;
                uint64_t t18 = GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8);
                uint8_t t19 = (t17 < t18);
                if (!t19) goto lb16;
                {
                    uint64_t t22 = gm_l9;
                    uint64_t t23 = (uintptr_t)t22;
                    uint64_t t24 = GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8);
                    uint8_t t25 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l8)) + t23 * 1));
                    uint8_t t28 = GM_LD(uint8_t, ((uint8_t *)&t25));
                    gm_l10 = t28;
                    {
                        uint8_t t31 = gm_l10;
                        uint8_t t32 = (!(t31 == ((uint8_t)95ULL)));
                        if (!t32) goto ie30;
                        {
                            int64_t t34 = gm_l7;
                            int64_t r35; bool o36 = __builtin_add_overflow(t34, ((int64_t)1LL), &r35);
                            if (o36) gm_trap("integer overflow in `+`", gm_srct4c97p4hek1p, 241, 27);
                            int64_t t37 = r35;
                            gm_l7 = t37;
                        }
                        goto ix30;
ie30:;
ix30:;
                    }
                    goto me26;
mn27:;
                    gm_trap("no match arm matched the scrutinee", gm_srct4c97p4hek1p, 240, 5);
me26:;
                }
lc16:;
                {
                    uint64_t t39 = gm_l9;
                    uint64_t r40; bool o41 = __builtin_add_overflow(t39, ((uint64_t)1ULL), &r40);
                    uint64_t t42 = r40;
                    gm_l9 = t42;
                }
            }
lb16:;
        }
    }
    {
        gm_f_list_uvfu1k1((void *)&F.l11, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            void * t44 = gm_l0;
            uint64_t t45 = GM_LD(uintptr_t, (((uint8_t *)t44) + 8) + 8);
            void * t46 = ((uint8_t *)GM_LD(void *, (((uint8_t *)t44) + 8)));
            uint64_t t47 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 0));
            uint64_t t48 = (uintptr_t)t47;
            uint64_t t49 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
            uint64_t t50 = (uintptr_t)t49;
            if (t48 > t50 || t50 > (uintptr_t)t45) gm_trap_slice(t48, t50, t45, gm_srct4c97p4hek1p, 244, 14);
            gm_b16_8 t51;
            GM_ST(void *, (uint8_t *)&t51, (void *)(((uint8_t *)t46 + t48 * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t51 + 8, (uintptr_t)(t50 - t48));
            gm_l12 = t51;
        }
        {
            gm_l13 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t53 = gm_l13;
                uint64_t t54 = GM_LD(uintptr_t, ((uint8_t *)&gm_l12) + 8);
                uint8_t t55 = (t53 < t54);
                if (!t55) goto lb52;
                {
                    uint64_t t58 = gm_l13;
                    uint64_t t59 = (uintptr_t)t58;
                    uint64_t t60 = GM_LD(uintptr_t, ((uint8_t *)&gm_l12) + 8);
                    uint8_t t61 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l12)) + t59 * 1));
                    uint8_t t64 = GM_LD(uint8_t, ((uint8_t *)&t61));
                    gm_l14 = t64;
                    {
                        uint8_t t67 = gm_l14;
                        uint8_t t68 = (!(t67 == ((uint8_t)95ULL)));
                        if (!t68) goto ie66;
                        {
                            void * t70 = (void *)((uint8_t *)&F.l11);
                            uint8_t t71 = gm_l14;
                            gm_tftb1odc739t5 t72 = gm_f_push_t83rg2m(t70, t71);
                            if (((((uint8_t *)&t72))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t72) + 1, gm_srct4c97p4hek1p, 245, 27);
                            gm_b0_1 t73 = GM_LD(gm_b0_1, (((uint8_t *)&t72) + 1));
                        }
                        goto ix66;
ie66:;
ix66:;
                    }
                    goto me62;
mn63:;
                    gm_trap("no match arm matched the scrutinee", gm_srct4c97p4hek1p, 244, 5);
me62:;
                }
lc52:;
                {
                    uint64_t t75 = gm_l13;
                    uint64_t r76; bool o77 = __builtin_add_overflow(t75, ((uint64_t)1ULL), &r76);
                    uint64_t t78 = r76;
                    gm_l13 = t78;
                }
            }
lb52:;
        }
    }
    {
        {
            gm_b16_8 t80 = gm_l6;
            gm_l15 = t80;
        }
        {
            gm_l16 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t82 = gm_l16;
                uint64_t t83 = GM_LD(uintptr_t, ((uint8_t *)&gm_l15) + 8);
                uint8_t t84 = (t82 < t83);
                if (!t84) goto lb81;
                {
                    uint64_t t87 = gm_l16;
                    uint64_t t88 = (uintptr_t)t87;
                    uint64_t t89 = GM_LD(uintptr_t, ((uint8_t *)&gm_l15) + 8);
                    uint8_t t90 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l15)) + t88 * 1));
                    uint8_t t93 = GM_LD(uint8_t, ((uint8_t *)&t90));
                    gm_l17 = t93;
                    {
                        uint8_t t96 = gm_l17;
                        uint8_t t97 = (!(t96 == ((uint8_t)95ULL)));
                        if (!t97) goto ie95;
                        {
                            void * t99 = (void *)((uint8_t *)&F.l11);
                            uint8_t t100 = gm_l17;
                            gm_tftb1odc739t5 t101 = gm_f_push_t83rg2m(t99, t100);
                            if (((((uint8_t *)&t101))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t101) + 1, gm_srct4c97p4hek1p, 248, 27);
                            gm_b0_1 t102 = GM_LD(gm_b0_1, (((uint8_t *)&t101) + 1));
                        }
                        goto ix95;
ie95:;
ix95:;
                    }
                    goto me91;
mn92:;
                    gm_trap("no match arm matched the scrutinee", gm_srct4c97p4hek1p, 247, 5);
me91:;
                }
lc81:;
                {
                    uint64_t t104 = gm_l16;
                    uint64_t r105; bool o106 = __builtin_add_overflow(t104, ((uint64_t)1ULL), &r105);
                    uint64_t t107 = r105;
                    gm_l16 = t107;
                }
            }
lb81:;
        }
    }
    {
        gm_b16_8 t108 = GM_LD(gm_b16_8, (((uint8_t *)&F.l11) + 0));
        gm_b16_8 t109 = GM_LD(gm_b16_8, ((uint8_t *)&t108));
        uint32_t t110 = gm_l1;
        gm_f_parse_valid_9fjef3j((void *)&F.l18, (void *)NULL, cx, NULL, t109, t110);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        uint32_t t112 = gm_l1;
        uint8_t t113 = ((t112 == ((uint32_t)16ULL)));
        if (!t113) goto ie111;
        {
            int64_t t115 = gm_l4;
            int64_t t116 = gm_l7;
            int64_t r117; bool o118 = __builtin_mul_overflow(((int64_t)4LL), t116, &r117);
            if (o118) gm_trap("integer overflow in `*`", gm_srct4c97p4hek1p, 252, 61);
            int64_t t119 = r117;
            int64_t r120; bool o121 = __builtin_sub_overflow(t115, t119, &r120);
            if (o121) gm_trap("integer overflow in `-`", gm_srct4c97p4hek1p, 252, 55);
            int64_t t122 = r120;
            gm_f_scaled_ufcgnm9((void *)&(*(gm_tngtuiuskn938 *)gm_out), (void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l18)), ((uint32_t)2ULL), t122);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
        }
        goto ix111;
ie111:;
ix111:;
    }
    {
        int64_t t123 = gm_l4;
        int64_t t124 = gm_l7;
        int64_t r125; bool o126 = __builtin_sub_overflow(t123, t124, &r125);
        if (o126) gm_trap("integer overflow in `-`", gm_srct4c97p4hek1p, 253, 39);
        int64_t t127 = r125;
        gm_f_scaled_ufcgnm9((void *)&(*(gm_tngtuiuskn938 *)gm_out), (void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l18)), ((uint32_t)10ULL), t127);
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
inline __attribute__((aligned(4))) uint8_t gm_f_fraction_follows_ivbj04h(void * a0, uint32_t a1, uint64_t a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_fraction_follows_ivbj04h F;
    void * gm_l0; /* lx */
    uint32_t gm_l1; /* radix */
    uint64_t gm_l2; /* i */
    uint8_t gm_l3; /* c */
    uint8_t gm_l4; /* first_is_digit */
    uint64_t gm_l5; /* j */
    GM_STACK_CHECK_SHALLOW(&F, 205u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        uint8_t t2;
        void * t4 = gm_l0;
        uint64_t t5 = gm_l2;
        gm_tqs5p4n2tq19v t6 = gm_f_byte_b4v8vul(t4, t5);
        if (!((((uint8_t *)&t6))[1] != 0)) goto ie3;
        uint8_t t7 = GM_LD(uint8_t, ((uint8_t *)&t6));
        gm_l3 = t7;
        uint8_t t9 = gm_l3;
        uint8_t t10 = gm_f_is_digit_2hoggnj(t9);
        uint8_t t11 = t10;
        t2 = t11;
        goto ix3;
ie3:;
        uint8_t t13 = ((uint8_t)0);
        t2 = t13;
ix3:;
        gm_l4 = t2;
    }
    {
        uint32_t t15 = gm_l1;
        uint8_t t16 = ((t15 == ((uint32_t)10ULL)));
        if (!t16) goto ie14;
        {
            uint8_t t18 = gm_l4;
            uint8_t t19 = 0;
            F.ret = t18;
            goto gm_fret;
        }
        goto ix14;
ie14:;
ix14:;
    }
    {
        uint64_t t20 = gm_l2;
        gm_l5 = t20;
    }
    {
        for (;;) {
            uint64_t t22 = gm_l5;
            void * t23 = gm_l0;
            uint64_t t24 = GM_LD(uintptr_t, (((uint8_t *)t23) + 8) + 8);
            uint8_t t25 = (t22 < t24);
            if (!t25) goto lb21;
            uint8_t t26 = 0;
            uint64_t t27 = gm_l5;
            uint64_t t28 = (uintptr_t)t27;
            void * t29 = gm_l0;
            uint64_t t30 = GM_LD(uintptr_t, (((uint8_t *)t29) + 8) + 8);
            if (t28 >= (uintptr_t)(t30)) gm_trap_index(t28, t30, gm_srct4c97p4hek1p, 148, 50);
            uint8_t t31 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)t29) + 8)) + t28 * 1));
            uint8_t t32 = gm_f_is_hex_digit_9lads51(t31);
            if (!t32) {
                uint64_t t33 = gm_l5;
                uint64_t t34 = (uintptr_t)t33;
                void * t35 = gm_l0;
                uint64_t t36 = GM_LD(uintptr_t, (((uint8_t *)t35) + 8) + 8);
                if (t34 >= (uintptr_t)(t36)) gm_trap_index(t34, t36, gm_srct4c97p4hek1p, 148, 64);
                uint8_t t37 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)t35) + 8)) + t34 * 1));
                uint8_t t38 = ((t37 == ((uint8_t)95ULL)));
                t26 = t38;
            } else t26 = 1;
            if (!t26) goto lb21;
            {
                uint64_t t41 = gm_l5;
                uint64_t r42; bool o43 = __builtin_add_overflow(t41, ((uint64_t)1ULL), &r42);
                if (o43) gm_trap("integer overflow in `+`", gm_srct4c97p4hek1p, 148, 84);
                uint64_t t44 = r42;
                gm_l5 = t44;
            }
lc21:;
        }
lb21:;
    }
    {
        uint8_t t45 = 0;
        uint8_t t46 = gm_l4;
        if (!t46) {
            void * t47 = gm_l0;
            uint64_t t48 = gm_l5;
            gm_tqs5p4n2tq19v t49 = gm_f_byte_b4v8vul(t47, t48);
            gm_tqs5p4n2tq19v t50;
            GM_ST(uint8_t, ((uint8_t *)&t50), ((uint8_t)112ULL));
            ((uint8_t *)&t50)[1] = 1;
            uint8_t t51 = (gm_eqb44cb3cf1n(((uint8_t *)&t49), ((uint8_t *)&t50)));
            t45 = t51;
        } else t45 = 1;
        uint8_t t52 = 0;
        F.ret = t45;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_taari1s0m51a7 gm_f_imag_width_9gtosab(gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_imag_width_9gtosab F;
    gm_b16_8 gm_l0; /* s */
    GM_STACK_CHECK_SHALLOW(&F, 33u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_taari1s0m51a7 t1;
    if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8) == 3) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l0)), gm_sb6jpc12sa7uj, 3) == 0)) goto mn3;
    gm_t9qkh0rsooc8d t4;
    GM_ST(uint8_t, ((uint8_t *)&t4 + 0), ((uint8_t)0ULL));
    gm_taari1s0m51a7 t5;
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t5), t4);
    ((uint8_t *)&t5)[1] = 1;
    t1 = t5;
    goto me2;
mn3:;
    if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8) == 3) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l0)), gm_shq8kvmdjhmfg, 3) == 0)) goto mn6;
    gm_t9qkh0rsooc8d t7;
    GM_ST(uint8_t, ((uint8_t *)&t7 + 0), ((uint8_t)1ULL));
    gm_taari1s0m51a7 t8;
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t8), t7);
    ((uint8_t *)&t8)[1] = 1;
    t1 = t8;
    goto me2;
mn6:;
    if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8) == 3) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l0)), gm_s5k9ll19r7m4g, 3) == 0)) goto mn9;
    gm_t9qkh0rsooc8d t10;
    GM_ST(uint8_t, ((uint8_t *)&t10 + 0), ((uint8_t)2ULL));
    gm_taari1s0m51a7 t11;
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t11), t10);
    ((uint8_t *)&t11)[1] = 1;
    t1 = t11;
    goto me2;
mn9:;
    gm_taari1s0m51a7 t13;
    memset(&t13, 0, sizeof t13);
    t1 = t13;
    goto me2;
mn12:;
    gm_trap("no match arm matched the scrutinee", gm_srct4c97p4hek1p, 549, 48);
me2:;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) void gm_f_int_suffix_j594l00(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_int_suffix_j594l00 F;
    gm_b16_8 gm_l0; /* s */
    gm_b16_8 gm_l1; /* digits */
    uint32_t gm_l2; /* width */
    uint8_t gm_l3; /* valid */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    uint8_t gm_l6; /* c */
    uint32_t gm_l7; /* <tmp> */
    GM_STACK_CHECK(&F, 636u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = (((GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8) == 5) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l0)), gm_s6groq37ram3h, 5) == 0));
        if (!t3) goto ie2;
        {
            gm_tj2m9dbh1ekv6 t5;
            GM_ST(uint8_t, (uint8_t *)&t5 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t5 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t5 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t5 + 0), ((uint8_t)2ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tij245bdr7emd *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tij245bdr7emd *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tij245bdr7emd *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tij245bdr7emd *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tij245bdr7emd *)gm_out))[0] = 0;
            GM_ST(gm_tj2m9dbh1ekv6, (uint8_t *)&(*(gm_tij245bdr7emd *)gm_out) + 8, t5);
            goto gm_fdone;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        uint8_t t7 = (((GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8) == 5) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l0)), gm_s0i3i86nphpsh, 5) == 0));
        if (!t7) goto ie6;
        {
            gm_tj2m9dbh1ekv6 t9;
            GM_ST(uint8_t, (uint8_t *)&t9 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t9 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t9 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t9 + 0), ((uint8_t)3ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tij245bdr7emd *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tij245bdr7emd *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tij245bdr7emd *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tij245bdr7emd *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tij245bdr7emd *)gm_out))[0] = 0;
            GM_ST(gm_tj2m9dbh1ekv6, (uint8_t *)&(*(gm_tij245bdr7emd *)gm_out) + 8, t9);
            goto gm_fdone;
        }
        goto ix6;
ie6:;
ix6:;
    }
    {
        uint64_t t10 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        void * t11 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)));
        uint64_t t12 = (uintptr_t)((uint64_t)1ULL);
        if (t12 > (uintptr_t)t10 || (uintptr_t)t10 > (uintptr_t)t10) gm_trap_slice(t12, (uintptr_t)t10, t10, gm_srct4c97p4hek1p, 525, 15);
        gm_b16_8 t13;
        GM_ST(void *, (uint8_t *)&t13, (void *)(((uint8_t *)t11 + t12 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t13 + 8, (uintptr_t)((uintptr_t)t10 - t12));
        gm_l1 = t13;
    }
    {
        uint64_t t15 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t16 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        if (t15 >= (uintptr_t)(t16)) gm_trap_index(t15, t16, gm_srct4c97p4hek1p, 526, 8);
        uint8_t t17 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t15 * 1));
        gm_tqs5p4n2tq19v t18;
        GM_ST(uint8_t, ((uint8_t *)&t18 + 0), ((uint8_t)117ULL));
        GM_ST(uint8_t, ((uint8_t *)&t18 + 1), ((uint8_t)105ULL));
        uint8_t t19 = 0;
        if (gm_eqjmccqdb90f(((uint8_t *)&t18) + 0, ((uint8_t *)&t17))) t19 = 1;
        if (gm_eqjmccqdb90f(((uint8_t *)&t18) + 1, ((uint8_t *)&t17))) t19 = 1;
        uint8_t t20 = (uint8_t)(t19);
        if (!t20) goto ie14;
        uint64_t t21 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint8_t t22 = (t21 > ((uint64_t)0ULL));
        if (!t22) goto ie14;
        gm_b16_8 t23 = gm_l1;
        uint8_t t24 = gm_f_all_digits_e77c2k1(t23);
        if (!t24) goto ie14;
        {
            gm_l2 = ((uint32_t)0ULL);
        }
        {
            uint8_t t26 = 0;
            uint64_t t27 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            uint8_t t28 = (t27 <= ((uint64_t)3ULL));
            if (t28) {
                uint64_t t29 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t30 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t29 >= (uintptr_t)(t30)) gm_trap_index(t29, t30, gm_srct4c97p4hek1p, 528, 38);
                uint8_t t31 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t29 * 1));
                uint8_t t32 = (!(t31 == ((uint8_t)48ULL)));
                t26 = t32;
            } else t26 = 0;
            gm_l3 = t26;
        }
        {
            uint8_t t34 = gm_l3;
            if (!t34) goto ie33;
            {
                {
                    gm_b16_8 t37 = gm_l1;
                    gm_l4 = t37;
                }
                {
                    gm_l5 = ((uint64_t)0ULL);
                }
                {
                    for (;;) {
                        uint64_t t39 = gm_l5;
                        uint64_t t40 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                        uint8_t t41 = (t39 < t40);
                        if (!t41) goto lb38;
                        {
                            uint64_t t44 = gm_l5;
                            uint64_t t45 = (uintptr_t)t44;
                            uint64_t t46 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                            uint8_t t47 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t45 * 1));
                            uint8_t t50 = GM_LD(uint8_t, ((uint8_t *)&t47));
                            gm_l6 = t50;
                            {
                                uint32_t t52 = gm_l2;
                                uint32_t r53; bool o54 = __builtin_mul_overflow(t52, ((uint32_t)10ULL), &r53);
                                if (o54) gm_trap("integer overflow in `*`", gm_srct4c97p4hek1p, 530, 39);
                                uint32_t t55 = r53;
                                uint8_t t56 = gm_l6;
                                uint8_t r57; bool o58 = __builtin_sub_overflow(t56, ((uint8_t)48ULL), &r57);
                                if (o58) gm_trap("integer overflow in `-`", gm_srct4c97p4hek1p, 530, 56);
                                uint8_t t59 = r57;
                                uint32_t t60 = (uint32_t)t59;
                                uint32_t r61; bool o62 = __builtin_add_overflow(t55, t60, &r61);
                                if (o62) gm_trap("integer overflow in `+`", gm_srct4c97p4hek1p, 530, 39);
                                uint32_t t63 = r61;
                                gm_l2 = t63;
                            }
                            goto me48;
mn49:;
                            gm_trap("no match arm matched the scrutinee", gm_srct4c97p4hek1p, 530, 13);
me48:;
                        }
lc38:;
                        {
                            uint64_t t65 = gm_l5;
                            uint64_t r66; bool o67 = __builtin_add_overflow(t65, ((uint64_t)1ULL), &r66);
                            uint64_t t68 = r66;
                            gm_l5 = t68;
                        }
                    }
lb38:;
                }
            }
            {
                {
                    uint32_t t70 = gm_l2;
                    gm_l7 = t70;
                }
                uint8_t t71 = 0;
                uint32_t t72 = gm_l7;
                uint8_t t73 = (((uint32_t)1ULL) <= t72);
                if (t73) {
                    uint32_t t74 = gm_l7;
                    uint8_t t75 = (t74 <= ((uint32_t)128ULL));
                    t71 = t75;
                } else t71 = 0;
                uint8_t t76 = t71;
                gm_l3 = t76;
            }
            goto ix33;
ie33:;
ix33:;
        }
        {
            uint8_t t78 = gm_l3;
            uint8_t t79 = (uint8_t)!t78;
            if (!t79) goto ie77;
            {
                gm_b16_8 t81 = gm_l0;
                gm_tnpifv20irnph t82;
                GM_ST(gm_b16_8, ((uint8_t *)&t82 + 0), t81);
                gm_tabb710d9mq2e t83;
                gm_f_format_1kivsu4((void *)&t83, (void *)NULL, cx, NULL, t82);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t83))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t83) + 8, gm_srct4c97p4hek1p, 534, 21);
                gm_b16_8 t84 = GM_LD(gm_b16_8, (((uint8_t *)&t83) + 8));
                gm_b16_8 t85 = GM_LD(gm_b16_8, ((uint8_t *)&t84));
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tij245bdr7emd *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_tij245bdr7emd *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tij245bdr7emd *)gm_out) + 4, 0);
                ((uint8_t *)&(*(gm_tij245bdr7emd *)gm_out))[0] = 1;
                GM_ST(gm_b16_8, (uint8_t *)&(*(gm_tij245bdr7emd *)gm_out) + 8, t85);
                goto gm_fdone;
            }
            goto ix77;
ie77:;
ix77:;
        }
        {
            uint64_t t87 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t88 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            if (t87 >= (uintptr_t)(t88)) gm_trap_index(t87, t88, gm_srct4c97p4hek1p, 536, 12);
            uint8_t t89 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t87 * 1));
            uint8_t t90 = ((t89 == ((uint8_t)117ULL)));
            if (!t90) goto ie86;
            {
                uint32_t t92 = gm_l2;
                gm_tj2m9dbh1ekv6 t93;
                GM_ST(uint8_t, (uint8_t *)&t93 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t93 + 2, 0);
                GM_ST(uint8_t, ((uint8_t *)&t93 + 0), ((uint8_t)0ULL));
                GM_ST(uint32_t, ((uint8_t *)&t93 + 4), t92);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tij245bdr7emd *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_tij245bdr7emd *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tij245bdr7emd *)gm_out) + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_tij245bdr7emd *)gm_out) + 16, 0);
                ((uint8_t *)&(*(gm_tij245bdr7emd *)gm_out))[0] = 0;
                GM_ST(gm_tj2m9dbh1ekv6, (uint8_t *)&(*(gm_tij245bdr7emd *)gm_out) + 8, t93);
                goto gm_fdone;
            }
            goto ix86;
ie86:;
ix86:;
        }
        {
            uint32_t t94 = gm_l2;
            gm_tj2m9dbh1ekv6 t95;
            GM_ST(uint8_t, (uint8_t *)&t95 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t95 + 2, 0);
            GM_ST(uint8_t, ((uint8_t *)&t95 + 0), ((uint8_t)1ULL));
            GM_ST(uint32_t, ((uint8_t *)&t95 + 4), t94);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tij245bdr7emd *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tij245bdr7emd *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tij245bdr7emd *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tij245bdr7emd *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tij245bdr7emd *)gm_out))[0] = 0;
            GM_ST(gm_tj2m9dbh1ekv6, (uint8_t *)&(*(gm_tij245bdr7emd *)gm_out) + 8, t95);
            goto gm_fdone;
        }
        goto ix14;
ie14:;
ix14:;
    }
    {
        gm_b16_8 t97 = gm_l0;
        gm_taari1s0m51a7 t98 = gm_f_float_suffix_lh8qbg0(t97);
        uint8_t t99;
        if (((((uint8_t *)&t98))[1] != 0)) goto mn101;
        t99 = ((uint8_t)0);
        goto me100;
mn101:;
        t99 = ((uint8_t)1);
        goto me100;
mn102:;
        gm_trap("no match arm matched the scrutinee", gm_srct4c97p4hek1p, 539, 8);
me100:;
        if (!t99) goto ie96;
        {
            gm_b16_8 t104 = gm_l0;
            gm_tnpifv20irnph t105;
            GM_ST(gm_b16_8, ((uint8_t *)&t105 + 0), t104);
            gm_tabb710d9mq2e t106;
            gm_f_format_3rm05av((void *)&t106, (void *)NULL, cx, NULL, t105);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t106))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t106) + 8, gm_srct4c97p4hek1p, 540, 17);
            gm_b16_8 t107 = GM_LD(gm_b16_8, (((uint8_t *)&t106) + 8));
            gm_b16_8 t108 = GM_LD(gm_b16_8, ((uint8_t *)&t107));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tij245bdr7emd *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tij245bdr7emd *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tij245bdr7emd *)gm_out) + 4, 0);
            ((uint8_t *)&(*(gm_tij245bdr7emd *)gm_out))[0] = 1;
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_tij245bdr7emd *)gm_out) + 8, t108);
            goto gm_fdone;
        }
        goto ix96;
ie96:;
ix96:;
    }
    {
        gm_b16_8 t109 = gm_l0;
        gm_tnpifv20irnph t110;
        GM_ST(gm_b16_8, ((uint8_t *)&t110 + 0), t109);
        gm_tabb710d9mq2e t111;
        gm_f_format_16f92bp((void *)&t111, (void *)NULL, cx, NULL, t110);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t111))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t111) + 8, gm_srct4c97p4hek1p, 542, 13);
        gm_b16_8 t112 = GM_LD(gm_b16_8, (((uint8_t *)&t111) + 8));
        gm_b16_8 t113 = GM_LD(gm_b16_8, ((uint8_t *)&t112));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tij245bdr7emd *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tij245bdr7emd *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tij245bdr7emd *)gm_out) + 4, 0);
        ((uint8_t *)&(*(gm_tij245bdr7emd *)gm_out))[0] = 1;
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_tij245bdr7emd *)gm_out) + 8, t113);
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
extern __attribute__((aligned(4))) void gm_f_int_value_cnmrbr4(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t2lqu8jsj511d a1, uint32_t a2);
extern __attribute__((aligned(4))) uint8_t gm_f_is_imag_suffix_46gu23m(gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_lex_char_4qk4vjp(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_lex_char_4qk4vjp F;
    void * gm_l0; /* lx */
    uint64_t gm_l1; /* lo */
    uint32_t gm_l2; /* value */
    uint8_t gm_l3; /* b */
    uint32_t gm_l4; /* c */
    uint8_t gm_l5; /* x */
    gm_tb31k5l794t7p gm_l6; /* c */
    GM_STACK_CHECK(&F, 638u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        uint64_t t3 = GM_LD(uint64_t, (((uint8_t *)t2) + 24));
        gm_l1 = t3;
    }
    {
        void * t4 = gm_l0;
        void * t5 = gm_l0;
        uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)t5) + 24));
        uint64_t r7; bool o8 = __builtin_add_overflow(t6, ((uint64_t)1ULL), &r7);
        if (o8) gm_trap("integer overflow in `+`", gm_srct4c97p4hek1p, 261, 5);
        uint64_t t9 = r7;
        GM_ST(uint64_t, (((uint8_t *)t4) + 24), t9);
    }
    {
        gm_l2 = ((uint32_t)0ULL);
    }
    {
        void * t10 = gm_l0;
        gm_tqs5p4n2tq19v t11 = gm_f_peek_72bostn(t10);
        uint8_t t12;
        if (((((uint8_t *)&t11))[1] != 0)) {
            uint8_t t13 = GM_LD(uint8_t, (((uint8_t *)&t11) + 0));
            t12 = t13;
        } else {
            {
                void * t15 = gm_l0;
                uint64_t t16 = gm_l1;
                void * t17 = gm_l0;
                uint64_t t18 = GM_LD(uint64_t, (((uint8_t *)t17) + 24));
                gm_b16_8 t19;
                GM_ST(void *, (uint8_t *)&t19, (void *)(gm_sq9ngpqr0m7ek));
                GM_ST(uintptr_t, (uint8_t *)&t19 + 8, (uintptr_t)(30));
                gm_b0_1 t20 = gm_f_error_p5r0d56((void *)NULL, cx, NULL, t15, t16, t18, t19);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t21 = gm_l0;
                uint64_t t22 = gm_l1;
                gm_b0_1 t23 = gm_f_emit_char_1ips8a1((void *)NULL, cx, NULL, t21, ((uint32_t)0ULL), t22);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b0_1 t24;
            uint8_t t25 = 0;
            F.ret = t24;
            goto gm_fret;
        }
        gm_l3 = t12;
    }
    {
        uint8_t t27 = gm_l3;
        gm_tqs5p4n2tq19v t28;
        GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)10ULL));
        GM_ST(uint8_t, ((uint8_t *)&t28 + 1), ((uint8_t)13ULL));
        uint8_t t29 = 0;
        if (gm_eqjmccqdb90f(((uint8_t *)&t28) + 0, ((uint8_t *)&t27))) t29 = 1;
        if (gm_eqjmccqdb90f(((uint8_t *)&t28) + 1, ((uint8_t *)&t27))) t29 = 1;
        uint8_t t30 = (uint8_t)(t29);
        if (!t30) goto ie26;
        {
            void * t32 = gm_l0;
            uint64_t t33 = gm_l1;
            void * t34 = gm_l0;
            uint64_t t35 = GM_LD(uint64_t, (((uint8_t *)t34) + 24));
            gm_b16_8 t36;
            GM_ST(void *, (uint8_t *)&t36, (void *)(gm_sq9ngpqr0m7ek));
            GM_ST(uintptr_t, (uint8_t *)&t36 + 8, (uintptr_t)(30));
            gm_b0_1 t37 = gm_f_error_p5r0d56((void *)NULL, cx, NULL, t32, t33, t35, t36);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t38 = gm_l0;
            uint64_t t39 = gm_l1;
            gm_b0_1 t40 = gm_f_emit_char_1ips8a1((void *)NULL, cx, NULL, t38, ((uint32_t)0ULL), t39);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_b0_1 t41;
            uint8_t t42 = 0;
            F.ret = t41;
            goto gm_fret;
        }
        goto ix26;
ie26:;
ix26:;
    }
    {
        uint8_t t44 = gm_l3;
        uint8_t t45 = ((t44 == ((uint8_t)39ULL)));
        if (!t45) goto ie43;
        {
            void * t47 = gm_l0;
            void * t48 = gm_l0;
            uint64_t t49 = GM_LD(uint64_t, (((uint8_t *)t48) + 24));
            uint64_t r50; bool o51 = __builtin_add_overflow(t49, ((uint64_t)1ULL), &r50);
            if (o51) gm_trap("integer overflow in `+`", gm_srct4c97p4hek1p, 274, 9);
            uint64_t t52 = r50;
            GM_ST(uint64_t, (((uint8_t *)t47) + 24), t52);
        }
        {
            void * t53 = gm_l0;
            uint64_t t54 = gm_l1;
            void * t55 = gm_l0;
            uint64_t t56 = GM_LD(uint64_t, (((uint8_t *)t55) + 24));
            gm_b16_8 t57;
            GM_ST(void *, (uint8_t *)&t57, (void *)(gm_skmai4uvseje8));
            GM_ST(uintptr_t, (uint8_t *)&t57 + 8, (uintptr_t)(23));
            gm_b0_1 t58 = gm_f_error_p5r0d56((void *)NULL, cx, NULL, t53, t54, t56, t57);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t59 = gm_l0;
            uint64_t t60 = gm_l1;
            gm_b0_1 t61 = gm_f_emit_char_1ips8a1((void *)NULL, cx, NULL, t59, ((uint32_t)0ULL), t60);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_b0_1 t62;
            uint8_t t63 = 0;
            F.ret = t62;
            goto gm_fret;
        }
        goto ix43;
ie43:;
ix43:;
    }
    {
        uint8_t t65 = gm_l3;
        uint8_t t66 = ((t65 == ((uint8_t)92ULL)));
        if (!t66) goto ie64;
        {
            void * t68 = gm_l0;
            gm_ts06tb4mldv43 t69 = gm_f_lex_escape_7pnp2v0((void *)NULL, cx, NULL, t68);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (!((((uint8_t *)&t69))[8] != 0)) goto mn71;
            uint8_t t72 = GM_LD(uint8_t, (((uint8_t *)&t69) + 0));
            if (t72 != ((uint8_t)0ULL)) goto mn71;
            uint32_t t73 = GM_LD(uint32_t, (((uint8_t *)&t69) + 4));
            gm_l4 = t73;
            {
                uint32_t t75 = gm_l4;
                gm_l2 = t75;
            }
            goto me70;
mn71:;
            if (!((((uint8_t *)&t69))[8] != 0)) goto mn76;
            uint8_t t77 = GM_LD(uint8_t, (((uint8_t *)&t69) + 0));
            if (t77 != ((uint8_t)1ULL)) goto mn76;
            uint8_t t78 = GM_LD(uint8_t, (((uint8_t *)&t69) + 4));
            gm_l5 = t78;
            {
                uint8_t t80 = gm_l5;
                uint32_t t81 = (uint32_t)t80;
                gm_l2 = t81;
            }
            goto me70;
mn76:;
            if (((((uint8_t *)&t69))[8] != 0)) goto mn82;
            {
                {
                    void * t85 = gm_l0;
                    uint8_t t86 = gm_f_skip_to_closing_quote_qmb6ii3(t85);
                }
            }
            {
                void * t87 = gm_l0;
                uint64_t t88 = gm_l1;
                gm_b0_1 t89 = gm_f_emit_char_1ips8a1((void *)NULL, cx, NULL, t87, ((uint32_t)0ULL), t88);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                gm_b0_1 t90;
                uint8_t t91 = 0;
                F.ret = t90;
                goto gm_fret;
            }
            goto me70;
mn82:;
            gm_trap("no match arm matched the scrutinee", gm_srct4c97p4hek1p, 280, 9);
me70:;
        }
        goto ix64;
ie64:;
        {
            void * t93 = gm_l0;
            gm_tb31k5l794t7p t94 = GM_CONV(gm_tb31k5l794t7p, gm_f_current_char_ctktsl2(t93));
            gm_l6 = t94;
        }
        {
            void * t95 = gm_l0;
            void * t96 = gm_l0;
            uint64_t t97 = GM_LD(uint64_t, (((uint8_t *)t96) + 24));
            uint64_t t98 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
            uint64_t r99; bool o100 = __builtin_add_overflow(t97, t98, &r99);
            if (o100) gm_trap("integer overflow in `+`", gm_srct4c97p4hek1p, 291, 9);
            uint64_t t101 = r99;
            GM_ST(uint64_t, (((uint8_t *)t95) + 24), t101);
        }
        {
            uint32_t t102 = GM_LD(uint32_t, (((uint8_t *)&gm_l6) + 0));
            gm_l2 = t102;
        }
ix64:;
    }
    {
        void * t104 = gm_l0;
        gm_tqs5p4n2tq19v t105 = gm_f_peek_72bostn(t104);
        gm_tqs5p4n2tq19v t106;
        GM_ST(uint8_t, ((uint8_t *)&t106), ((uint8_t)39ULL));
        ((uint8_t *)&t106)[1] = 1;
        uint8_t t107 = (gm_eqb44cb3cf1n(((uint8_t *)&t105), ((uint8_t *)&t106)));
        if (!t107) goto ie103;
        {
            void * t109 = gm_l0;
            void * t110 = gm_l0;
            uint64_t t111 = GM_LD(uint64_t, (((uint8_t *)t110) + 24));
            uint64_t r112; bool o113 = __builtin_add_overflow(t111, ((uint64_t)1ULL), &r112);
            if (o113) gm_trap("integer overflow in `+`", gm_srct4c97p4hek1p, 295, 9);
            uint64_t t114 = r112;
            GM_ST(uint64_t, (((uint8_t *)t109) + 24), t114);
        }
        goto ix103;
ie103:;
        void * t116 = gm_l0;
        uint8_t t117 = gm_f_skip_to_closing_quote_qmb6ii3(t116);
        if (!t117) goto ie115;
        {
            void * t119 = gm_l0;
            uint64_t t120 = gm_l1;
            void * t121 = gm_l0;
            uint64_t t122 = GM_LD(uint64_t, (((uint8_t *)t121) + 24));
            gm_b16_8 t123;
            GM_ST(void *, (uint8_t *)&t123, (void *)(gm_saoa4m7dh2vl3));
            GM_ST(uintptr_t, (uint8_t *)&t123 + 8, (uintptr_t)(54));
            gm_b0_1 t124 = gm_f_error_p5r0d56((void *)NULL, cx, NULL, t119, t120, t122, t123);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix115;
ie115:;
        {
            void * t126 = gm_l0;
            uint64_t t127 = gm_l1;
            void * t128 = gm_l0;
            uint64_t t129 = GM_LD(uint64_t, (((uint8_t *)t128) + 24));
            gm_b16_8 t130;
            GM_ST(void *, (uint8_t *)&t130, (void *)(gm_sq9ngpqr0m7ek));
            GM_ST(uintptr_t, (uint8_t *)&t130 + 8, (uintptr_t)(30));
            gm_b0_1 t131 = gm_f_error_p5r0d56((void *)NULL, cx, NULL, t126, t127, t129, t130);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
ix115:;
ix103:;
    }
    {
        void * t132 = gm_l0;
        uint32_t t133 = gm_l2;
        uint64_t t134 = gm_l1;
        gm_b0_1 t135 = gm_f_emit_char_1ips8a1((void *)NULL, cx, NULL, t132, t133, t134);
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
__attribute__((aligned(4))) gm_ts06tb4mldv43 gm_f_lex_escape_7pnp2v0(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_lex_escape_7pnp2v0 F;
    void * gm_l0; /* lx */
    uint64_t gm_l1; /* lo */
    uint8_t gm_l2; /* b */
    gm_tqs5p4n2tq19v gm_l3; /* simple */
    uint8_t gm_l4; /* c */
    uint8_t gm_l5; /* hi */
    uint8_t gm_l6; /* low */
    gm_tb31k5l794t7p gm_l7; /* c */
    GM_STACK_CHECK(&F, 898u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        uint64_t t3 = GM_LD(uint64_t, (((uint8_t *)t2) + 24));
        gm_l1 = t3;
    }
    {
        void * t4 = gm_l0;
        void * t5 = gm_l0;
        uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)t5) + 24));
        uint64_t r7; bool o8 = __builtin_add_overflow(t6, ((uint64_t)1ULL), &r7);
        if (o8) gm_trap("integer overflow in `+`", gm_srct4c97p4hek1p, 331, 5);
        uint64_t t9 = r7;
        GM_ST(uint64_t, (((uint8_t *)t4) + 24), t9);
    }
    {
        void * t10 = gm_l0;
        gm_tqs5p4n2tq19v t11 = gm_f_peek_72bostn(t10);
        uint8_t t12;
        if (((((uint8_t *)&t11))[1] != 0)) {
            uint8_t t13 = GM_LD(uint8_t, (((uint8_t *)&t11) + 0));
            t12 = t13;
        } else {
            gm_ts06tb4mldv43 t14;
            memset(&t14, 0, sizeof t14);
            uint8_t t15 = 0;
            F.ret = t14;
            goto gm_fret;
        }
        gm_l2 = t12;
    }
    {
        uint8_t t16 = gm_l2;
        gm_tqs5p4n2tq19v t17;
        uint8_t t20 = GM_LD(uint8_t, ((uint8_t *)&t16));
        if (!(t20 == ((uint8_t)110ULL))) goto mn19;
        gm_tqs5p4n2tq19v t21;
        GM_ST(uint8_t, ((uint8_t *)&t21), ((uint8_t)10ULL));
        ((uint8_t *)&t21)[1] = 1;
        t17 = t21;
        goto me18;
mn19:;
        uint8_t t23 = GM_LD(uint8_t, ((uint8_t *)&t16));
        if (!(t23 == ((uint8_t)114ULL))) goto mn22;
        gm_tqs5p4n2tq19v t24;
        GM_ST(uint8_t, ((uint8_t *)&t24), ((uint8_t)13ULL));
        ((uint8_t *)&t24)[1] = 1;
        t17 = t24;
        goto me18;
mn22:;
        uint8_t t26 = GM_LD(uint8_t, ((uint8_t *)&t16));
        if (!(t26 == ((uint8_t)116ULL))) goto mn25;
        gm_tqs5p4n2tq19v t27;
        GM_ST(uint8_t, ((uint8_t *)&t27), ((uint8_t)9ULL));
        ((uint8_t *)&t27)[1] = 1;
        t17 = t27;
        goto me18;
mn25:;
        uint8_t t29 = GM_LD(uint8_t, ((uint8_t *)&t16));
        if (!(t29 == ((uint8_t)48ULL))) goto mn28;
        gm_tqs5p4n2tq19v t30;
        GM_ST(uint8_t, ((uint8_t *)&t30), ((uint8_t)0ULL));
        ((uint8_t *)&t30)[1] = 1;
        t17 = t30;
        goto me18;
mn28:;
        uint8_t t34 = GM_LD(uint8_t, ((uint8_t *)&t16));
        if (!(t34 == ((uint8_t)92ULL))) goto pa33;
        goto po32;
pa33:;
        uint8_t t36 = GM_LD(uint8_t, ((uint8_t *)&t16));
        if (!(t36 == ((uint8_t)34ULL))) goto pa35;
        goto po32;
pa35:;
        uint8_t t38 = GM_LD(uint8_t, ((uint8_t *)&t16));
        if (!(t38 == ((uint8_t)39ULL))) goto pa37;
        goto po32;
pa37:;
        goto mn31;
po32:;
        uint8_t t39 = gm_l2;
        gm_tqs5p4n2tq19v t40;
        GM_ST(uint8_t, ((uint8_t *)&t40), t39);
        ((uint8_t *)&t40)[1] = 1;
        t17 = t40;
        goto me18;
mn31:;
        gm_tqs5p4n2tq19v t42;
        memset(&t42, 0, sizeof t42);
        t17 = t42;
        goto me18;
mn41:;
        gm_trap("no match arm matched the scrutinee", gm_srct4c97p4hek1p, 333, 19);
me18:;
        gm_l3 = t17;
    }
    {
        if (!((((uint8_t *)&gm_l3))[1] != 0)) goto ie43;
        uint8_t t44 = GM_LD(uint8_t, ((uint8_t *)&gm_l3));
        gm_l4 = t44;
        {
            void * t46 = gm_l0;
            void * t47 = gm_l0;
            uint64_t t48 = GM_LD(uint64_t, (((uint8_t *)t47) + 24));
            uint64_t r49; bool o50 = __builtin_add_overflow(t48, ((uint64_t)1ULL), &r49);
            if (o50) gm_trap("integer overflow in `+`", gm_srct4c97p4hek1p, 342, 9);
            uint64_t t51 = r49;
            GM_ST(uint64_t, (((uint8_t *)t46) + 24), t51);
        }
        {
            uint8_t t52 = gm_l4;
            uint32_t t53 = (uint32_t)t52;
            gm_tum2strs1ckr2 t54;
            GM_ST(uint8_t, (uint8_t *)&t54 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t54 + 2, 0);
            GM_ST(uint8_t, ((uint8_t *)&t54 + 0), ((uint8_t)0ULL));
            GM_ST(uint32_t, ((uint8_t *)&t54 + 4), t53);
            gm_ts06tb4mldv43 t55;
            GM_ST(uint8_t, (uint8_t *)&t55 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t55 + 10, 0);
            GM_ST(gm_tum2strs1ckr2, ((uint8_t *)&t55), t54);
            ((uint8_t *)&t55)[8] = 1;
            uint8_t t56 = 0;
            F.ret = t55;
            goto gm_fret;
        }
        goto ix43;
ie43:;
ix43:;
    }
    {
        uint8_t t57 = gm_l2;
        uint8_t t60 = GM_LD(uint8_t, ((uint8_t *)&t57));
        if (!(t60 == ((uint8_t)120ULL))) goto mn59;
        {
            void * t62 = gm_l0;
            void * t63 = gm_l0;
            uint64_t t64 = GM_LD(uint64_t, (((uint8_t *)t63) + 24));
            uint64_t r65; bool o66 = __builtin_add_overflow(t64, ((uint64_t)1ULL), &r65);
            if (o66) gm_trap("integer overflow in `+`", gm_srct4c97p4hek1p, 347, 13);
            uint64_t t67 = r65;
            GM_ST(uint64_t, (((uint8_t *)t62) + 24), t67);
        }
        {
            void * t68 = gm_l0;
            gm_tqs5p4n2tq19v t69 = gm_f_peek_72bostn(t68);
            uint8_t t70;
            if (((((uint8_t *)&t69))[1] != 0)) {
                uint8_t t71 = GM_LD(uint8_t, (((uint8_t *)&t69) + 0));
                t70 = t71;
            } else {
                t70 = ((uint8_t)0ULL);
            }
            gm_l5 = t70;
        }
        {
            void * t72 = gm_l0;
            gm_tqs5p4n2tq19v t73 = gm_f_peek_at_74p8lkq(t72, ((uint64_t)1ULL));
            uint8_t t74;
            if (((((uint8_t *)&t73))[1] != 0)) {
                uint8_t t75 = GM_LD(uint8_t, (((uint8_t *)&t73) + 0));
                t74 = t75;
            } else {
                t74 = ((uint8_t)0ULL);
            }
            gm_l6 = t74;
        }
        {
            uint8_t t77 = gm_l5;
            uint8_t t78 = gm_f_is_hex_digit_9lads51(t77);
            if (!t78) goto ie76;
            uint8_t t79 = gm_l6;
            uint8_t t80 = gm_f_is_hex_digit_9lads51(t79);
            if (!t80) goto ie76;
            {
                void * t82 = gm_l0;
                void * t83 = gm_l0;
                uint64_t t84 = GM_LD(uint64_t, (((uint8_t *)t83) + 24));
                uint64_t r85; bool o86 = __builtin_add_overflow(t84, ((uint64_t)2ULL), &r85);
                if (o86) gm_trap("integer overflow in `+`", gm_srct4c97p4hek1p, 351, 17);
                uint64_t t87 = r85;
                GM_ST(uint64_t, (((uint8_t *)t82) + 24), t87);
            }
            {
                uint8_t t88 = gm_l5;
                uint32_t t89 = gm_f_digit_value_8ogbrqp(t88);
                uint32_t r90; bool o91 = __builtin_mul_overflow(t89, ((uint32_t)16ULL), &r90);
                if (o91) gm_trap("integer overflow in `*`", gm_srct4c97p4hek1p, 352, 31);
                uint32_t t92 = r90;
                uint8_t t93 = gm_l6;
                uint32_t t94 = gm_f_digit_value_8ogbrqp(t93);
                uint32_t r95; bool o96 = __builtin_add_overflow(t92, t94, &r95);
                if (o96) gm_trap("integer overflow in `+`", gm_srct4c97p4hek1p, 352, 31);
                uint32_t t97 = r95;
                uint8_t t98 = (uint8_t)t97;
                gm_tum2strs1ckr2 t99;
                GM_ST(uint8_t, (uint8_t *)&t99 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t99 + 2, 0);
                GM_ST(uint8_t, (uint8_t *)&t99 + 5, 0);
                GM_ST(uint16_t, (uint8_t *)&t99 + 6, 0);
                GM_ST(uint8_t, ((uint8_t *)&t99 + 0), ((uint8_t)1ULL));
                GM_ST(uint8_t, ((uint8_t *)&t99 + 4), t98);
                gm_ts06tb4mldv43 t100;
                GM_ST(uint8_t, (uint8_t *)&t100 + 9, 0);
                GM_ST(uint16_t, (uint8_t *)&t100 + 10, 0);
                GM_ST(gm_tum2strs1ckr2, ((uint8_t *)&t100), t99);
                ((uint8_t *)&t100)[8] = 1;
                uint8_t t101 = 0;
                F.ret = t100;
                goto gm_fret;
            }
            goto ix76;
ie76:;
ix76:;
        }
        {
            for (;;) {
                void * t103 = gm_l0;
                uint64_t t104 = GM_LD(uint64_t, (((uint8_t *)t103) + 24));
                uint64_t t105 = gm_l1;
                uint64_t r106; bool o107 = __builtin_add_overflow(t105, ((uint64_t)4ULL), &r106);
                if (o107) gm_trap("integer overflow in `+`", gm_srct4c97p4hek1p, 354, 28);
                uint64_t t108 = r106;
                uint8_t t109 = (t104 < t108);
                if (!t109) goto lb102;
                void * t110 = gm_l0;
                uint64_t t111 = GM_LD(uint64_t, (((uint8_t *)t110) + 24));
                void * t112 = gm_l0;
                uint64_t t113 = GM_LD(uintptr_t, (((uint8_t *)t112) + 8) + 8);
                uint8_t t114 = (t111 < t113);
                if (!t114) goto lb102;
                void * t115 = gm_l0;
                uint64_t t116 = GM_LD(uint64_t, (((uint8_t *)t115) + 24));
                uint64_t t117 = (uintptr_t)t116;
                void * t118 = gm_l0;
                uint64_t t119 = GM_LD(uintptr_t, (((uint8_t *)t118) + 8) + 8);
                if (t117 >= (uintptr_t)(t119)) gm_trap_index(t117, t119, gm_srct4c97p4hek1p, 354, 82);
                uint8_t t120 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)t118) + 8)) + t117 * 1));
                uint8_t t121 = gm_f_is_hex_digit_9lads51(t120);
                if (!t121) goto lb102;
                {
                    void * t124 = gm_l0;
                    void * t125 = gm_l0;
                    uint64_t t126 = GM_LD(uint64_t, (((uint8_t *)t125) + 24));
                    uint64_t r127; bool o128 = __builtin_add_overflow(t126, ((uint64_t)1ULL), &r127);
                    if (o128) gm_trap("integer overflow in `+`", gm_srct4c97p4hek1p, 354, 100);
                    uint64_t t129 = r127;
                    GM_ST(uint64_t, (((uint8_t *)t124) + 24), t129);
                }
lc102:;
            }
lb102:;
        }
        {
            void * t130 = gm_l0;
            uint64_t t131 = gm_l1;
            void * t132 = gm_l0;
            uint64_t t133 = GM_LD(uint64_t, (((uint8_t *)t132) + 24));
            gm_b16_8 t134;
            GM_ST(void *, (uint8_t *)&t134, (void *)(gm_smit2phoauonm));
            GM_ST(uintptr_t, (uint8_t *)&t134 + 8, (uintptr_t)(60));
            gm_b0_1 t135 = gm_f_error_p5r0d56((void *)NULL, cx, NULL, t130, t131, t133, t134);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_ts06tb4mldv43 t136;
            memset(&t136, 0, sizeof t136);
            uint8_t t137 = 0;
            F.ret = t136;
            goto gm_fret;
        }
        goto me58;
mn59:;
        uint8_t t139 = GM_LD(uint8_t, ((uint8_t *)&t57));
        if (!(t139 == ((uint8_t)117ULL))) goto mn138;
        {
            void * t141 = gm_l0;
            void * t142 = gm_l0;
            uint64_t t143 = GM_LD(uint64_t, (((uint8_t *)t142) + 24));
            uint64_t r144; bool o145 = __builtin_add_overflow(t143, ((uint64_t)1ULL), &r144);
            if (o145) gm_trap("integer overflow in `+`", gm_srct4c97p4hek1p, 359, 13);
            uint64_t t146 = r144;
            GM_ST(uint64_t, (((uint8_t *)t141) + 24), t146);
        }
        {
            void * t147 = gm_l0;
            uint64_t t148 = gm_l1;
            gm_ts06tb4mldv43 t149 = gm_f_lex_unicode_escape_ns81oou((void *)NULL, cx, NULL, t147, t148);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t150 = 0;
            F.ret = t149;
            goto gm_fret;
        }
        goto me58;
mn138:;
        uint8_t t154 = GM_LD(uint8_t, ((uint8_t *)&t57));
        if (!(t154 == ((uint8_t)10ULL))) goto pa153;
        goto po152;
pa153:;
        uint8_t t156 = GM_LD(uint8_t, ((uint8_t *)&t57));
        if (!(t156 == ((uint8_t)13ULL))) goto pa155;
        goto po152;
pa155:;
        goto mn151;
po152:;
        gm_ts06tb4mldv43 t157;
        memset(&t157, 0, sizeof t157);
        uint8_t t158 = 0;
        F.ret = t157;
        goto gm_fret;
        goto me58;
mn151:;
        {
            void * t161 = gm_l0;
            gm_tb31k5l794t7p t162 = GM_CONV(gm_tb31k5l794t7p, gm_f_current_char_ctktsl2(t161));
            gm_l7 = t162;
        }
        {
            void * t163 = gm_l0;
            void * t164 = gm_l0;
            uint64_t t165 = GM_LD(uint64_t, (((uint8_t *)t164) + 24));
            uint64_t t166 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
            uint64_t r167; bool o168 = __builtin_add_overflow(t165, t166, &r167);
            if (o168) gm_trap("integer overflow in `+`", gm_srct4c97p4hek1p, 365, 13);
            uint64_t t169 = r167;
            GM_ST(uint64_t, (((uint8_t *)t163) + 24), t169);
        }
        {
            void * t170 = gm_l0;
            uint64_t t171 = gm_l1;
            void * t172 = gm_l0;
            uint64_t t173 = GM_LD(uint64_t, (((uint8_t *)t172) + 24));
            void * t174 = gm_l0;
            uint64_t t175 = GM_LD(uintptr_t, (((uint8_t *)t174) + 8) + 8);
            void * t176 = ((uint8_t *)GM_LD(void *, (((uint8_t *)t174) + 8)));
            uint64_t t177 = gm_l1;
            uint64_t r178; bool o179 = __builtin_add_overflow(t177, ((uint64_t)1ULL), &r178);
            if (o179) gm_trap("integer overflow in `+`", gm_srct4c97p4hek1p, 366, 86);
            uint64_t t180 = r178;
            uint64_t t181 = (uintptr_t)t180;
            void * t182 = gm_l0;
            uint64_t t183 = GM_LD(uint64_t, (((uint8_t *)t182) + 24));
            uint64_t t184 = (uintptr_t)t183;
            if (t181 > t184 || t184 > (uintptr_t)t175) gm_trap_slice(t181, t184, t175, gm_srct4c97p4hek1p, 366, 79);
            gm_b16_8 t185;
            GM_ST(void *, (uint8_t *)&t185, (void *)(((uint8_t *)t176 + t181 * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t185 + 8, (uintptr_t)(t184 - t181));
            gm_tnpifv20irnph t186;
            GM_ST(gm_b16_8, ((uint8_t *)&t186 + 0), t185);
            gm_tabb710d9mq2e t187;
            gm_f_format_eue6enu((void *)&t187, (void *)NULL, cx, NULL, t186);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t187))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t187) + 8, gm_srct4c97p4hek1p, 366, 34);
            gm_b16_8 t188 = GM_LD(gm_b16_8, (((uint8_t *)&t187) + 8));
            gm_b16_8 t189 = GM_LD(gm_b16_8, ((uint8_t *)&t188));
            gm_b0_1 t190 = gm_f_error_p5r0d56((void *)NULL, cx, NULL, t170, t171, t173, t189);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_ts06tb4mldv43 t191;
            memset(&t191, 0, sizeof t191);
            uint8_t t192 = 0;
            F.ret = t191;
            goto gm_fret;
        }
        goto me58;
mn159:;
        gm_trap("no match arm matched the scrutinee", gm_srct4c97p4hek1p, 345, 5);
me58:;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_lex_field_index_pf905g6(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1) {
    struct gm_fr_lex_field_index_pf905g6 F;
    void * gm_l0; /* lx */
    uint64_t gm_l1; /* lo */
    GM_STACK_CHECK(&F, 450u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        for (;;) {
            void * t3 = gm_l0;
            uint64_t t4 = GM_LD(uint64_t, (((uint8_t *)t3) + 24));
            void * t5 = gm_l0;
            uint64_t t6 = GM_LD(uintptr_t, (((uint8_t *)t5) + 8) + 8);
            uint8_t t7 = (t4 < t6);
            if (!t7) goto lb2;
            void * t8 = gm_l0;
            uint64_t t9 = GM_LD(uint64_t, (((uint8_t *)t8) + 24));
            uint64_t t10 = (uintptr_t)t9;
            void * t11 = gm_l0;
            uint64_t t12 = GM_LD(uintptr_t, (((uint8_t *)t11) + 8) + 8);
            if (t10 >= (uintptr_t)(t12)) gm_trap_index(t10, t12, gm_srct4c97p4hek1p, 154, 50);
            uint8_t t13 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)t11) + 8)) + t10 * 1));
            uint8_t t14 = gm_f_is_digit_2hoggnj(t13);
            if (!t14) goto lb2;
            {
                void * t17 = gm_l0;
                void * t18 = gm_l0;
                uint64_t t19 = GM_LD(uint64_t, (((uint8_t *)t18) + 24));
                uint64_t r20; bool o21 = __builtin_add_overflow(t19, ((uint64_t)1ULL), &r20);
                if (o21) gm_trap("integer overflow in `+`", gm_srct4c97p4hek1p, 154, 68);
                uint64_t t22 = r20;
                GM_ST(uint64_t, (((uint8_t *)t17) + 24), t22);
            }
lc2:;
        }
lb2:;
    }
    {
        void * t24 = gm_l0;
        uint64_t t25 = GM_LD(uint64_t, (((uint8_t *)t24) + 24));
        uint64_t t26 = gm_l1;
        uint64_t r27; bool o28 = __builtin_sub_overflow(t25, t26, &r27);
        if (o28) gm_trap("integer overflow in `-`", gm_srct4c97p4hek1p, 155, 8);
        uint64_t t29 = r27;
        uint8_t t30 = (t29 > ((uint64_t)1ULL));
        if (!t30) goto ie23;
        uint64_t t31 = gm_l1;
        uint64_t t32 = (uintptr_t)t31;
        void * t33 = gm_l0;
        uint64_t t34 = GM_LD(uintptr_t, (((uint8_t *)t33) + 8) + 8);
        if (t32 >= (uintptr_t)(t34)) gm_trap_index(t32, t34, gm_srct4c97p4hek1p, 155, 28);
        uint8_t t35 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)t33) + 8)) + t32 * 1));
        uint8_t t36 = ((t35 == ((uint8_t)48ULL)));
        if (!t36) goto ie23;
        {
            void * t38 = gm_l0;
            uint64_t t39 = gm_l1;
            void * t40 = gm_l0;
            uint64_t t41 = GM_LD(uint64_t, (((uint8_t *)t40) + 24));
            gm_b16_8 t42;
            GM_ST(void *, (uint8_t *)&t42, (void *)(gm_sqj9769nr8pm0));
            GM_ST(uintptr_t, (uint8_t *)&t42 + 8, (uintptr_t)(47));
            gm_b0_1 t43 = gm_f_error_p5r0d56((void *)NULL, cx, NULL, t38, t39, t41, t42);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix23;
ie23:;
ix23:;
    }
    {
        void * t44 = gm_l0;
        uint64_t t45 = gm_l1;
        void * t46 = gm_l0;
        uint64_t t47 = GM_LD(uint64_t, (((uint8_t *)t46) + 24));
        gm_t2lqu8jsj511d t48;
        GM_ST(uint64_t, ((uint8_t *)&t48 + 0), t45);
        GM_ST(uint64_t, ((uint8_t *)&t48 + 8), t47);
        gm_f_int_value_cnmrbr4((void *)&F.l2, (void *)NULL, cx, NULL, t44, t48, ((uint32_t)10ULL));
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t49 = gm_l0;
        gm_ta42iqot1auae t50;
        memset((uint8_t *)&t50 + 61, 0, 3);
        memcpy((uint8_t *)&t50 + 0, ((uint8_t *)&F.l2), 48);
        gm_t46lmulm8gaj3 t51;
        memset(&t51, 0, sizeof t51);
        GM_ST(gm_t46lmulm8gaj3, (uint8_t *)&t50 + 48, t51);
        GM_ST(uint8_t, (uint8_t *)&t50 + 60, ((uint8_t)0));
        gm_tq7s98ihonkju t52 = gm_f_new_q1fs5ps((void *)NULL, cx, NULL, &t50, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_khsmm7kmq9o)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t52))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t52) + 8, gm_srct4c97p4hek1p, 159, 18);
        void * t53 = GM_LD(void *, (((uint8_t *)&t52) + 8));
        gm_t3fcbuehh3bs2 t54;
        GM_ST(uint8_t, (uint8_t *)&t54 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t54 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t54 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t54 + 16, 0);
        GM_ST(uint64_t, (uint8_t *)&t54 + 24, 0);
        GM_ST(uint8_t, ((uint8_t *)&t54 + 0), ((uint8_t)3ULL));
        GM_ST(void *, ((uint8_t *)&t54 + 8), t53);
        uint64_t t55 = gm_l1;
        void * t56 = gm_l0;
        uint64_t t57 = GM_LD(uint64_t, (((uint8_t *)t56) + 24));
        gm_b0_1 t58 = gm_f_emit_ekvv703((void *)NULL, cx, NULL, t49, &t54, t55, t57);
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
__attribute__((aligned(4))) gm_b0_1 gm_f_lex_number_9j9sdfi(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_lex_number_9j9sdfi F;
    void * gm_l0; /* lx */
    uint64_t gm_l1; /* lo */
    uint64_t gm_l2; /* n */
    gm_th2lqbo77pvsc gm_l3; /* err */
    gm_th2lqbo77pvsc gm_l4; /* prefix_fix */
    uint32_t gm_l5; /* radix */
    uint8_t gm_l6; /* c */
    uint8_t gm_l7; /* lower */
    gm_b16_8 gm_l8; /* letter */
    gm_t2lqu8jsj511d gm_l9; /* int_part */
    gm_tc70a9dbbmgkg gm_l10; /* frac_part */
    gm_t913bb735pvt5 gm_l11; /* exp */
    uint8_t gm_l12; /* float_by_suffix */
    uint64_t gm_l13; /* at */
    uint8_t gm_l14; /* is_float */
    gm_b16_8 gm_l15; /* suffix */
    uint8_t gm_l16; /* imag */
    gm_t46lmulm8gaj3 gm_l17; /* int_sfx */
    gm_taari1s0m51a7 gm_l18; /* float_sfx */
    gm_b16_8 gm_l19; /* message */
    gm_tj2m9dbh1ekv6 gm_l20; /* s */
    gm_b16_8 gm_l21; /* message */
    gm_b16_8 gm_l23; /* p */
    GM_STACK_CHECK(&F, 2711u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        uint64_t t3 = GM_LD(uint64_t, (((uint8_t *)t2) + 24));
        gm_l1 = t3;
    }
    {
        void * t4 = gm_l0;
        uint64_t t5 = GM_LD(uintptr_t, ((((uint8_t *)t4) + 40) + 0) + 8);
        gm_l2 = t5;
    }
    {
        uint64_t t7 = gm_l2;
        uint8_t t8 = (t7 > ((uint64_t)0ULL));
        if (!t8) goto ie6;
        uint64_t t9 = gm_l2;
        uint64_t r10; bool o11 = __builtin_sub_overflow(t9, ((uint64_t)1ULL), &r10);
        if (o11) gm_trap("integer overflow in `-`", gm_srct4c97p4hek1p, 33, 34);
        uint64_t t12 = r10;
        uint64_t t13 = (uintptr_t)t12;
        void * t14 = gm_l0;
        uint64_t t15 = GM_LD(uintptr_t, ((((uint8_t *)t14) + 40) + 0) + 8);
        if (t13 >= (uintptr_t)(t15)) gm_trap_index(t13, t15, gm_srct4c97p4hek1p, 33, 18);
        gm_t3fcbuehh3bs2 t16 = GM_LD(gm_t3fcbuehh3bs2, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t14) + 40) + 0)) + t13 * 48) + 0));
        gm_t9qkh0rsooc8d t17;
        GM_ST(uint8_t, ((uint8_t *)&t17 + 0), ((uint8_t)58ULL));
        uint8_t t18 = (((GM_LD(uint8_t, ((uint8_t *)&t16) + 0) == ((uint8_t)8ULL)) && gm_eqv3tj1pu43g((((uint8_t *)&t16) + 8), ((uint8_t *)&t17))));
        if (!t18) goto ie6;
        {
            void * t20 = gm_l0;
            uint64_t t21 = gm_l1;
            gm_b0_1 t22 = gm_f_lex_field_index_pf905g6((void *)NULL, cx, NULL, t20, t21);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_b0_1 t23;
            uint8_t t24 = 0;
            F.ret = t23;
            goto gm_fret;
        }
        goto ix6;
ie6:;
ix6:;
    }
    {
        gm_th2lqbo77pvsc t25;
        memset(&t25, 0, sizeof t25);
        gm_l3 = t25;
    }
    {
        gm_th2lqbo77pvsc t26;
        memset(&t26, 0, sizeof t26);
        gm_l4 = t26;
    }
    {
        gm_l5 = ((uint32_t)10ULL);
    }
    {
        uint64_t t28 = gm_l1;
        uint64_t t29 = (uintptr_t)t28;
        void * t30 = gm_l0;
        uint64_t t31 = GM_LD(uintptr_t, (((uint8_t *)t30) + 8) + 8);
        if (t29 >= (uintptr_t)(t31)) gm_trap_index(t29, t31, gm_srct4c97p4hek1p, 42, 8);
        uint8_t t32 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)t30) + 8)) + t29 * 1));
        uint8_t t33 = ((t32 == ((uint8_t)48ULL)));
        if (!t33) goto ie27;
        {
            void * t36 = gm_l0;
            gm_tqs5p4n2tq19v t37 = gm_f_peek_at_74p8lkq(t36, ((uint64_t)1ULL));
            if (!((((uint8_t *)&t37))[1] != 0)) goto ie35;
            uint8_t t38 = GM_LD(uint8_t, ((uint8_t *)&t37));
            gm_l6 = t38;
            {
                uint8_t t41 = gm_l6;
                gm_t4q2b5ibsrdik t42;
                GM_ST(uint8_t, ((uint8_t *)&t42 + 0), ((uint8_t)120ULL));
                GM_ST(uint8_t, ((uint8_t *)&t42 + 1), ((uint8_t)111ULL));
                GM_ST(uint8_t, ((uint8_t *)&t42 + 2), ((uint8_t)98ULL));
                GM_ST(uint8_t, ((uint8_t *)&t42 + 3), ((uint8_t)88ULL));
                GM_ST(uint8_t, ((uint8_t *)&t42 + 4), ((uint8_t)79ULL));
                GM_ST(uint8_t, ((uint8_t *)&t42 + 5), ((uint8_t)66ULL));
                uint8_t t43 = 0;
                if (gm_eqjmccqdb90f(((uint8_t *)&t42) + 0, ((uint8_t *)&t41))) t43 = 1;
                if (gm_eqjmccqdb90f(((uint8_t *)&t42) + 1, ((uint8_t *)&t41))) t43 = 1;
                if (gm_eqjmccqdb90f(((uint8_t *)&t42) + 2, ((uint8_t *)&t41))) t43 = 1;
                if (gm_eqjmccqdb90f(((uint8_t *)&t42) + 3, ((uint8_t *)&t41))) t43 = 1;
                if (gm_eqjmccqdb90f(((uint8_t *)&t42) + 4, ((uint8_t *)&t41))) t43 = 1;
                if (gm_eqjmccqdb90f(((uint8_t *)&t42) + 5, ((uint8_t *)&t41))) t43 = 1;
                uint8_t t44 = (uint8_t)(t43);
                if (!t44) goto ie40;
                {
                    uint8_t t46 = gm_l6;
                    uint8_t t47 = (uint8_t)(t46 | ((uint8_t)32ULL));
                    gm_l7 = t47;
                }
                {
                    uint8_t t49 = gm_l6;
                    uint8_t t50 = (t49 < ((uint8_t)97ULL));
                    if (!t50) goto ie48;
                    {
                        uint8_t t52 = gm_l7;
                        gm_b16_8 t53;
                        uint8_t t56 = GM_LD(uint8_t, ((uint8_t *)&t52));
                        if (!(t56 == ((uint8_t)120ULL))) goto mn55;
                        gm_b16_8 t57;
                        GM_ST(void *, (uint8_t *)&t57, (void *)(gm_se4629k8oa1uo));
                        GM_ST(uintptr_t, (uint8_t *)&t57 + 8, (uintptr_t)(1));
                        t53 = t57;
                        goto me54;
mn55:;
                        uint8_t t59 = GM_LD(uint8_t, ((uint8_t *)&t52));
                        if (!(t59 == ((uint8_t)111ULL))) goto mn58;
                        gm_b16_8 t60;
                        GM_ST(void *, (uint8_t *)&t60, (void *)(gm_se0f2jka437t4));
                        GM_ST(uintptr_t, (uint8_t *)&t60 + 8, (uintptr_t)(1));
                        t53 = t60;
                        goto me54;
mn58:;
                        gm_b16_8 t62;
                        GM_ST(void *, (uint8_t *)&t62, (void *)(gm_s50pmptf3idds));
                        GM_ST(uintptr_t, (uint8_t *)&t62 + 8, (uintptr_t)(1));
                        t53 = t62;
                        goto me54;
mn61:;
                        gm_trap("no match arm matched the scrutinee", gm_srct4c97p4hek1p, 47, 31);
me54:;
                        gm_l8 = t53;
                    }
                    {
                        gm_b16_8 t63 = gm_l8;
                        gm_tnpifv20irnph t64;
                        GM_ST(gm_b16_8, ((uint8_t *)&t64 + 0), t63);
                        gm_tabb710d9mq2e t65;
                        gm_f_format_g6sh9k2((void *)&t65, (void *)NULL, cx, NULL, t64);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        if (((((uint8_t *)&t65))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t65) + 8, gm_srct4c97p4hek1p, 52, 27);
                        gm_b16_8 t66 = GM_LD(gm_b16_8, (((uint8_t *)&t65) + 8));
                        gm_b16_8 t67 = GM_LD(gm_b16_8, ((uint8_t *)&t66));
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l3)) + 17, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l3)) + 18, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l3)) + 20, 0);
                        ((uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l3)))[16] = 1;
                        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l3)) + 0, t67);
                    }
                    {
                        gm_b16_8 t68 = gm_l8;
                        gm_tnpifv20irnph t69;
                        GM_ST(gm_b16_8, ((uint8_t *)&t69 + 0), t68);
                        gm_tabb710d9mq2e t70;
                        gm_f_format_78j0ecp((void *)&t70, (void *)NULL, cx, NULL, t69);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        if (((((uint8_t *)&t70))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t70) + 8, gm_srct4c97p4hek1p, 53, 34);
                        gm_b16_8 t71 = GM_LD(gm_b16_8, (((uint8_t *)&t70) + 8));
                        gm_b16_8 t72 = GM_LD(gm_b16_8, ((uint8_t *)&t71));
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l4)) + 17, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l4)) + 18, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l4)) + 20, 0);
                        ((uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l4)))[16] = 1;
                        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l4)) + 0, t72);
                    }
                    goto ix48;
ie48:;
ix48:;
                }
                {
                    void * t73 = gm_l0;
                    void * t74 = gm_l0;
                    uint64_t t75 = GM_LD(uint64_t, (((uint8_t *)t74) + 24));
                    uint64_t r76; bool o77 = __builtin_add_overflow(t75, ((uint64_t)2ULL), &r76);
                    if (o77) gm_trap("integer overflow in `+`", gm_srct4c97p4hek1p, 55, 17);
                    uint64_t t78 = r76;
                    GM_ST(uint64_t, (((uint8_t *)t73) + 24), t78);
                }
                {
                    uint8_t t79 = gm_l7;
                    uint32_t t80;
                    uint8_t t83 = GM_LD(uint8_t, ((uint8_t *)&t79));
                    if (!(t83 == ((uint8_t)120ULL))) goto mn82;
                    t80 = ((uint32_t)16ULL);
                    goto me81;
mn82:;
                    uint8_t t85 = GM_LD(uint8_t, ((uint8_t *)&t79));
                    if (!(t85 == ((uint8_t)111ULL))) goto mn84;
                    t80 = ((uint32_t)8ULL);
                    goto me81;
mn84:;
                    t80 = ((uint32_t)2ULL);
                    goto me81;
mn86:;
                    gm_trap("no match arm matched the scrutinee", gm_srct4c97p4hek1p, 56, 25);
me81:;
                    gm_l5 = t80;
                }
                goto ix40;
ie40:;
ix40:;
            }
            goto ix35;
ie35:;
ix35:;
        }
        goto ix27;
ie27:;
ix27:;
    }
    {
        void * t87 = gm_l0;
        uint32_t t88 = gm_l5;
        void * t89 = (void *)((uint8_t *)&gm_l3);
        gm_t2lqu8jsj511d t90 = gm_f_scan_digits_jhpsv6j((void *)NULL, cx, NULL, t87, t88, t89);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l9 = t90;
    }
    {
        uint64_t t92 = GM_LD(uint64_t, (((uint8_t *)&gm_l9) + 8));
        uint64_t t93 = GM_LD(uint64_t, (((uint8_t *)&gm_l9) + 0));
        uint8_t t94 = ((t92 == t93));
        if (!t94) goto ie91;
        uint8_t t95;
        if (((((uint8_t *)&gm_l3))[16] != 0)) goto mn97;
        t95 = ((uint8_t)1);
        goto me96;
mn97:;
        t95 = ((uint8_t)0);
        goto me96;
mn98:;
        gm_trap("no match arm matched the scrutinee", gm_srct4c97p4hek1p, 66, 39);
me96:;
        if (!t95) goto ie91;
        {
            gm_t6j6smj7qoctu t100;
            uint32_t t101 = gm_l5;
            gm_b16_8 t102 = gm_f_radix_name_5kts8t0(t101);
            GM_ST(gm_b16_8, (uint8_t *)&t100 + 0, t102);
            void * t103 = gm_l0;
            uint64_t t104 = GM_LD(uintptr_t, (((uint8_t *)t103) + 8) + 8);
            void * t105 = ((uint8_t *)GM_LD(void *, (((uint8_t *)t103) + 8)));
            uint64_t t106 = gm_l1;
            uint64_t t107 = (uintptr_t)t106;
            void * t108 = gm_l0;
            uint64_t t109 = GM_LD(uint64_t, (((uint8_t *)t108) + 24));
            uint64_t t110 = (uintptr_t)t109;
            if (t107 > t110 || t110 > (uintptr_t)t104) gm_trap_slice(t107, t110, t104, gm_srct4c97p4hek1p, 67, 78);
            gm_b16_8 t111;
            GM_ST(void *, (uint8_t *)&t111, (void *)(((uint8_t *)t105 + t107 * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t111 + 8, (uintptr_t)(t110 - t107));
            GM_ST(gm_b16_8, (uint8_t *)&t100 + 16, t111);
            gm_tabb710d9mq2e t112;
            gm_f_format_b6aggpq((void *)&t112, (void *)NULL, cx, NULL, &t100);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t112))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t112) + 8, gm_srct4c97p4hek1p, 67, 15);
            gm_b16_8 t113 = GM_LD(gm_b16_8, (((uint8_t *)&t112) + 8));
            gm_b16_8 t114 = GM_LD(gm_b16_8, ((uint8_t *)&t113));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l3)) + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l3)) + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l3)) + 20, 0);
            ((uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l3)))[16] = 1;
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l3)) + 0, t114);
        }
        goto ix91;
ie91:;
ix91:;
    }
    {
        uint32_t t116 = gm_l5;
        uint8_t t117 = ((t116 == ((uint32_t)10ULL)));
        if (!t117) goto ie115;
        uint64_t t118 = GM_LD(uint64_t, (((uint8_t *)&gm_l9) + 8));
        uint64_t t119 = GM_LD(uint64_t, (((uint8_t *)&gm_l9) + 0));
        uint64_t r120; bool o121 = __builtin_sub_overflow(t118, t119, &r120);
        if (o121) gm_trap("integer overflow in `-`", gm_srct4c97p4hek1p, 69, 24);
        uint64_t t122 = r120;
        uint8_t t123 = (t122 > ((uint64_t)1ULL));
        if (!t123) goto ie115;
        uint64_t t124 = GM_LD(uint64_t, (((uint8_t *)&gm_l9) + 0));
        uint64_t t125 = (uintptr_t)t124;
        void * t126 = gm_l0;
        uint64_t t127 = GM_LD(uintptr_t, (((uint8_t *)t126) + 8) + 8);
        if (t125 >= (uintptr_t)(t127)) gm_trap_index(t125, t127, gm_srct4c97p4hek1p, 69, 58);
        uint8_t t128 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)t126) + 8)) + t125 * 1));
        uint8_t t129 = ((t128 == ((uint8_t)48ULL)));
        if (!t129) goto ie115;
        uint8_t t130;
        if (((((uint8_t *)&gm_l3))[16] != 0)) goto mn132;
        t130 = ((uint8_t)1);
        goto me131;
mn132:;
        t130 = ((uint8_t)0);
        goto me131;
mn133:;
        gm_trap("no match arm matched the scrutinee", gm_srct4c97p4hek1p, 69, 89);
me131:;
        if (!t130) goto ie115;
        {
            gm_b16_8 t135;
            GM_ST(void *, (uint8_t *)&t135, (void *)(gm_sfd839bbgsa7s));
            GM_ST(uintptr_t, (uint8_t *)&t135 + 8, (uintptr_t)(62));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l3)) + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l3)) + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l3)) + 20, 0);
            ((uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l3)))[16] = 1;
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l3)) + 0, t135);
        }
        goto ix115;
ie115:;
ix115:;
    }
    {
        gm_tc70a9dbbmgkg t136;
        memset(&t136, 0, sizeof t136);
        gm_l10 = t136;
    }
    {
        gm_t913bb735pvt5 t137;
        memset(&t137, 0, sizeof t137);
        gm_l11 = t137;
    }
    {
        uint32_t t139 = gm_l5;
        gm_tcc1rq4n1avb2 t140;
        GM_ST(uint32_t, ((uint8_t *)&t140 + 0), ((uint32_t)10ULL));
        GM_ST(uint32_t, ((uint8_t *)&t140 + 4), ((uint32_t)16ULL));
        uint8_t t141 = 0;
        if (gm_eq467d5cm1d5(((uint8_t *)&t140) + 0, ((uint8_t *)&t139))) t141 = 1;
        if (gm_eq467d5cm1d5(((uint8_t *)&t140) + 4, ((uint8_t *)&t139))) t141 = 1;
        uint8_t t142 = (uint8_t)(t141);
        if (!t142) goto ie138;
        void * t143 = gm_l0;
        gm_tqs5p4n2tq19v t144 = gm_f_peek_72bostn(t143);
        gm_tqs5p4n2tq19v t145;
        GM_ST(uint8_t, ((uint8_t *)&t145), ((uint8_t)46ULL));
        ((uint8_t *)&t145)[1] = 1;
        uint8_t t146 = (gm_eqb44cb3cf1n(((uint8_t *)&t144), ((uint8_t *)&t145)));
        if (!t146) goto ie138;
        void * t147 = gm_l0;
        uint32_t t148 = gm_l5;
        void * t149 = gm_l0;
        uint64_t t150 = GM_LD(uint64_t, (((uint8_t *)t149) + 24));
        uint64_t r151; bool o152 = __builtin_add_overflow(t150, ((uint64_t)1ULL), &r151);
        if (o152) gm_trap("integer overflow in `+`", gm_srct4c97p4hek1p, 76, 78);
        uint64_t t153 = r151;
        uint8_t t154 = gm_f_fraction_follows_ivbj04h(t147, t148, t153);
        if (!t154) goto ie138;
        {
            void * t156 = gm_l0;
            void * t157 = gm_l0;
            uint64_t t158 = GM_LD(uint64_t, (((uint8_t *)t157) + 24));
            uint64_t r159; bool o160 = __builtin_add_overflow(t158, ((uint64_t)1ULL), &r159);
            if (o160) gm_trap("integer overflow in `+`", gm_srct4c97p4hek1p, 77, 9);
            uint64_t t161 = r159;
            GM_ST(uint64_t, (((uint8_t *)t156) + 24), t161);
        }
        {
            void * t162 = gm_l0;
            uint32_t t163 = gm_l5;
            void * t164 = (void *)((uint8_t *)&gm_l3);
            gm_t2lqu8jsj511d t165 = gm_f_scan_digits_jhpsv6j((void *)NULL, cx, NULL, t162, t163, t164);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tc70a9dbbmgkg *)((uint8_t *)&gm_l10)) + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tc70a9dbbmgkg *)((uint8_t *)&gm_l10)) + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tc70a9dbbmgkg *)((uint8_t *)&gm_l10)) + 20, 0);
            ((uint8_t *)&(*(gm_tc70a9dbbmgkg *)((uint8_t *)&gm_l10)))[16] = 1;
            GM_ST(gm_t2lqu8jsj511d, (uint8_t *)&(*(gm_tc70a9dbbmgkg *)((uint8_t *)&gm_l10)) + 0, t165);
        }
        goto ix138;
ie138:;
ix138:;
    }
    {
        uint32_t t167 = gm_l5;
        uint8_t t168 = ((t167 == ((uint32_t)10ULL)));
        if (!t168) goto ie166;
        {
            void * t171 = gm_l0;
            gm_b16_8 t172;
            GM_ST(void *, (uint8_t *)&t172, (void *)(gm_su7ft5rc766n4));
            GM_ST(uintptr_t, (uint8_t *)&t172 + 8, (uintptr_t)(2));
            uint8_t t173 = gm_f_at_exponent_qp4713f(t171, t172);
            if (!t173) goto ie170;
            {
                void * t175 = gm_l0;
                void * t176 = (void *)((uint8_t *)&gm_l3);
                int64_t t177 = gm_f_scan_exponent_20o8k8o((void *)NULL, cx, NULL, t175, t176);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_t913bb735pvt5 t178;
                GM_ST(uint8_t, (uint8_t *)&t178 + 9, 0);
                GM_ST(uint16_t, (uint8_t *)&t178 + 10, 0);
                GM_ST(uint32_t, (uint8_t *)&t178 + 12, 0);
                GM_ST(int64_t, ((uint8_t *)&t178), t177);
                ((uint8_t *)&t178)[8] = 1;
                gm_l11 = t178;
            }
            goto ix170;
ie170:;
ix170:;
        }
        goto ix166;
ie166:;
        uint32_t t180 = gm_l5;
        uint8_t t181 = ((t180 == ((uint32_t)16ULL)));
        if (!t181) goto ie179;
        {
            void * t184 = gm_l0;
            gm_b16_8 t185;
            GM_ST(void *, (uint8_t *)&t185, (void *)(gm_s15p9kmrsfogp));
            GM_ST(uintptr_t, (uint8_t *)&t185 + 8, (uintptr_t)(1));
            uint8_t t186 = gm_f_at_exponent_qp4713f(t184, t185);
            if (!t186) goto ie183;
            {
                void * t188 = gm_l0;
                void * t189 = (void *)((uint8_t *)&gm_l3);
                int64_t t190 = gm_f_scan_exponent_20o8k8o((void *)NULL, cx, NULL, t188, t189);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_t913bb735pvt5 t191;
                GM_ST(uint8_t, (uint8_t *)&t191 + 9, 0);
                GM_ST(uint16_t, (uint8_t *)&t191 + 10, 0);
                GM_ST(uint32_t, (uint8_t *)&t191 + 12, 0);
                GM_ST(int64_t, ((uint8_t *)&t191), t190);
                ((uint8_t *)&t191)[8] = 1;
                gm_l11 = t191;
            }
            goto ix183;
ie183:;
            uint8_t t193;
            if (((((uint8_t *)&gm_l10))[16] != 0)) goto mn195;
            t193 = ((uint8_t)0);
            goto me194;
mn195:;
            t193 = ((uint8_t)1);
            goto me194;
mn196:;
            gm_trap("no match arm matched the scrutinee", gm_srct4c97p4hek1p, 85, 19);
me194:;
            if (!t193) goto ie192;
            uint8_t t197;
            if (((((uint8_t *)&gm_l3))[16] != 0)) goto mn199;
            t197 = ((uint8_t)1);
            goto me198;
mn199:;
            t197 = ((uint8_t)0);
            goto me198;
mn200:;
            gm_trap("no match arm matched the scrutinee", gm_srct4c97p4hek1p, 85, 45);
me198:;
            if (!t197) goto ie192;
            {
                gm_b16_8 t202;
                GM_ST(void *, (uint8_t *)&t202, (void *)(gm_s5frmv5tebja4));
                GM_ST(uintptr_t, (uint8_t *)&t202 + 8, (uintptr_t)(49));
                GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l3)) + 17, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l3)) + 18, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l3)) + 20, 0);
                ((uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l3)))[16] = 1;
                GM_ST(gm_b16_8, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l3)) + 0, t202);
            }
            goto ix192;
ie192:;
ix192:;
ix183:;
        }
        goto ix179;
ie179:;
ix179:;
ix166:;
    }
    {
        gm_l12 = ((uint8_t)0);
    }
    {
        uint32_t t204 = gm_l5;
        uint8_t t205 = ((t204 == ((uint32_t)10ULL)));
        if (!t205) goto ie203;
        uint8_t t206;
        if (((((uint8_t *)&gm_l10))[16] != 0)) goto mn208;
        t206 = ((uint8_t)1);
        goto me207;
mn208:;
        t206 = ((uint8_t)0);
        goto me207;
mn209:;
        gm_trap("no match arm matched the scrutinee", gm_srct4c97p4hek1p, 91, 24);
me207:;
        if (!t206) goto ie203;
        uint8_t t210;
        if (((((uint8_t *)&gm_l11))[8] != 0)) goto mn212;
        t210 = ((uint8_t)1);
        goto me211;
mn212:;
        t210 = ((uint8_t)0);
        goto me211;
mn213:;
        gm_trap("no match arm matched the scrutinee", gm_srct4c97p4hek1p, 91, 46);
me211:;
        if (!t210) goto ie203;
        {
            void * t215 = gm_l0;
            uint64_t t216 = GM_LD(uint64_t, (((uint8_t *)t215) + 24));
            gm_l13 = t216;
        }
        {
            void * t217 = gm_l0;
            gm_b16_8 t218 = gm_f_scan_ident_h1b2n4u(t217);
            gm_taari1s0m51a7 t219 = gm_f_float_suffix_lh8qbg0(t218);
            uint8_t t220;
            if (((((uint8_t *)&t219))[1] != 0)) goto mn222;
            t220 = ((uint8_t)0);
            goto me221;
mn222:;
            t220 = ((uint8_t)1);
            goto me221;
mn223:;
            gm_trap("no match arm matched the scrutinee", gm_srct4c97p4hek1p, 93, 27);
me221:;
            gm_l12 = t220;
        }
        {
            void * t224 = gm_l0;
            uint64_t t225 = gm_l13;
            GM_ST(uint64_t, (((uint8_t *)t224) + 24), t225);
        }
        goto ix203;
ie203:;
ix203:;
    }
    {
        uint8_t t226 = 0;
        uint8_t t227 = 0;
        uint8_t t228;
        if (((((uint8_t *)&gm_l10))[16] != 0)) goto mn230;
        t228 = ((uint8_t)0);
        goto me229;
mn230:;
        t228 = ((uint8_t)1);
        goto me229;
mn231:;
        gm_trap("no match arm matched the scrutinee", gm_srct4c97p4hek1p, 96, 17);
me229:;
        if (!t228) {
            uint8_t t232;
            if (((((uint8_t *)&gm_l11))[8] != 0)) goto mn234;
            t232 = ((uint8_t)0);
            goto me233;
mn234:;
            t232 = ((uint8_t)1);
            goto me233;
mn235:;
            gm_trap("no match arm matched the scrutinee", gm_srct4c97p4hek1p, 96, 42);
me233:;
            t227 = t232;
        } else t227 = 1;
        if (!t227) {
            uint8_t t236 = gm_l12;
            t226 = t236;
        } else t226 = 1;
        gm_l14 = t226;
    }
    {
        void * t237 = gm_l0;
        gm_b16_8 t238 = gm_f_scan_ident_h1b2n4u(t237);
        gm_l15 = t238;
    }
    {
        gm_b16_8 t239 = gm_l15;
        uint8_t t240 = gm_f_is_imag_suffix_46gu23m(t239);
        gm_l16 = t240;
    }
    {
        uint8_t t242 = gm_l16;
        if (!t242) goto ie241;
        {
            uint32_t t245 = gm_l5;
            uint8_t t246 = (!(t245 == ((uint32_t)10ULL)));
            if (!t246) goto ie244;
            uint8_t t247;
            if (((((uint8_t *)&gm_l3))[16] != 0)) goto mn249;
            t247 = ((uint8_t)1);
            goto me248;
mn249:;
            t247 = ((uint8_t)0);
            goto me248;
mn250:;
            gm_trap("no match arm matched the scrutinee", gm_srct4c97p4hek1p, 103, 32);
me248:;
            if (!t247) goto ie244;
            {
                gm_b16_8 t252;
                GM_ST(void *, (uint8_t *)&t252, (void *)(gm_sc80lo6tf645h));
                GM_ST(uintptr_t, (uint8_t *)&t252 + 8, (uintptr_t)(30));
                GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l3)) + 17, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l3)) + 18, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l3)) + 20, 0);
                ((uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l3)))[16] = 1;
                GM_ST(gm_b16_8, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l3)) + 0, t252);
            }
            goto ix244;
ie244:;
ix244:;
        }
        {
            gm_l14 = ((uint8_t)1);
        }
        goto ix241;
ie241:;
        gm_b16_8 t254 = gm_l15;
        gm_b16_8 t255;
        GM_ST(void *, (uint8_t *)&t255, (void *)(gm_sikg061j3im7q));
        GM_ST(uintptr_t, (uint8_t *)&t255 + 8, (uintptr_t)(1));
        uint8_t t256 = gm_f_starts_with_6vvt01e(t254, t255);
        if (!t256) goto ie253;
        uint64_t t257 = GM_LD(uintptr_t, ((uint8_t *)&gm_l15) + 8);
        void * t258 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l15)));
        uint64_t t259 = (uintptr_t)((uint64_t)1ULL);
        if (t259 > (uintptr_t)t257 || (uintptr_t)t257 > (uintptr_t)t257) gm_trap_slice(t259, (uintptr_t)t257, t257, gm_srct4c97p4hek1p, 105, 54);
        gm_b16_8 t260;
        GM_ST(void *, (uint8_t *)&t260, (void *)(((uint8_t *)t258 + t259 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t260 + 8, (uintptr_t)((uintptr_t)t257 - t259));
        uint8_t t261 = gm_f_all_digits_e77c2k1(t260);
        if (!t261) goto ie253;
        uint32_t t262 = gm_l5;
        uint8_t t263 = ((t262 == ((uint32_t)10ULL)));
        if (!t263) goto ie253;
        uint8_t t264;
        if (((((uint8_t *)&gm_l3))[16] != 0)) goto mn266;
        t264 = ((uint8_t)1);
        goto me265;
mn266:;
        t264 = ((uint8_t)0);
        goto me265;
mn267:;
        gm_trap("no match arm matched the scrutinee", gm_srct4c97p4hek1p, 105, 87);
me265:;
        if (!t264) goto ie253;
        {
            gm_b16_8 t269 = gm_l15;
            gm_tnpifv20irnph t270;
            GM_ST(gm_b16_8, ((uint8_t *)&t270 + 0), t269);
            gm_tabb710d9mq2e t271;
            gm_f_format_v5s6luj((void *)&t271, (void *)NULL, cx, NULL, t270);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t271))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t271) + 8, gm_srct4c97p4hek1p, 106, 15);
            gm_b16_8 t272 = GM_LD(gm_b16_8, (((uint8_t *)&t271) + 8));
            gm_b16_8 t273 = GM_LD(gm_b16_8, ((uint8_t *)&t272));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l3)) + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l3)) + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l3)) + 20, 0);
            ((uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l3)))[16] = 1;
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l3)) + 0, t273);
        }
        goto ix253;
ie253:;
ix253:;
ix241:;
    }
    {
        gm_t46lmulm8gaj3 t274;
        memset(&t274, 0, sizeof t274);
        gm_l17 = t274;
    }
    {
        gm_taari1s0m51a7 t275;
        memset(&t275, 0, sizeof t275);
        gm_l18 = t275;
    }
    {
        uint64_t t277 = GM_LD(uintptr_t, ((uint8_t *)&gm_l15) + 8);
        uint8_t t278 = ((t277 == ((uint64_t)0ULL)));
        if (!t278) goto ie276;
        goto ix276;
ie276:;
        uint8_t t281 = gm_l16;
        if (!t281) goto ie280;
        {
            gm_b16_8 t283 = gm_l15;
            gm_taari1s0m51a7 t284 = gm_f_imag_width_9gtosab(t283);
            gm_l18 = t284;
        }
        goto ix280;
ie280:;
        uint8_t t286 = gm_l14;
        if (!t286) goto ie285;
        {
            gm_b16_8 t288 = gm_l15;
            gm_taari1s0m51a7 t289 = gm_f_float_suffix_lh8qbg0(t288);
            gm_l18 = t289;
        }
        {
            uint8_t t291;
            if (((((uint8_t *)&gm_l18))[1] != 0)) goto mn293;
            t291 = ((uint8_t)1);
            goto me292;
mn293:;
            t291 = ((uint8_t)0);
            goto me292;
mn294:;
            gm_trap("no match arm matched the scrutinee", gm_srct4c97p4hek1p, 116, 12);
me292:;
            if (!t291) goto ie290;
            uint8_t t295;
            if (((((uint8_t *)&gm_l3))[16] != 0)) goto mn297;
            t295 = ((uint8_t)1);
            goto me296;
mn297:;
            t295 = ((uint8_t)0);
            goto me296;
mn298:;
            gm_trap("no match arm matched the scrutinee", gm_srct4c97p4hek1p, 116, 34);
me296:;
            if (!t295) goto ie290;
            {
                gm_b16_8 t300 = gm_l15;
                gm_tnpifv20irnph t301;
                GM_ST(gm_b16_8, ((uint8_t *)&t301 + 0), t300);
                gm_tabb710d9mq2e t302;
                gm_f_format_convcvp((void *)&t302, (void *)NULL, cx, NULL, t301);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t302))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t302) + 8, gm_srct4c97p4hek1p, 117, 19);
                gm_b16_8 t303 = GM_LD(gm_b16_8, (((uint8_t *)&t302) + 8));
                gm_b16_8 t304 = GM_LD(gm_b16_8, ((uint8_t *)&t303));
                GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l3)) + 17, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l3)) + 18, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l3)) + 20, 0);
                ((uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l3)))[16] = 1;
                GM_ST(gm_b16_8, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l3)) + 0, t304);
            }
            goto ix290;
ie290:;
ix290:;
        }
        goto ix285;
ie285:;
        {
            gm_b16_8 t306 = gm_l15;
            gm_tij245bdr7emd t307;
            gm_f_int_suffix_j594l00((void *)&t307, (void *)NULL, cx, NULL, t306);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (!((((uint8_t *)&t307))[0] != 0)) goto mn309;
            gm_b16_8 t310 = GM_LD(gm_b16_8, (((uint8_t *)&t307) + 8));
            gm_l19 = t310;
            {
                uint8_t t313;
                if (((((uint8_t *)&gm_l3))[16] != 0)) goto mn315;
                t313 = ((uint8_t)1);
                goto me314;
mn315:;
                t313 = ((uint8_t)0);
                goto me314;
mn316:;
                gm_trap("no match arm matched the scrutinee", gm_srct4c97p4hek1p, 122, 20);
me314:;
                if (!t313) goto ie312;
                {
                    gm_b16_8 t318 = gm_l19;
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l3)) + 17, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l3)) + 18, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l3)) + 20, 0);
                    ((uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l3)))[16] = 1;
                    GM_ST(gm_b16_8, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l3)) + 0, t318);
                }
                goto ix312;
ie312:;
ix312:;
            }
            goto me308;
mn309:;
            if (((((uint8_t *)&t307))[0] != 0)) goto mn319;
            gm_tj2m9dbh1ekv6 t320 = GM_LD(gm_tj2m9dbh1ekv6, (((uint8_t *)&t307) + 8));
            gm_l20 = t320;
            {
                gm_tj2m9dbh1ekv6 t322 = gm_l20;
                gm_t46lmulm8gaj3 t323;
                GM_ST(uint8_t, (uint8_t *)&t323 + 9, 0);
                GM_ST(uint16_t, (uint8_t *)&t323 + 10, 0);
                GM_ST(gm_tj2m9dbh1ekv6, ((uint8_t *)&t323), t322);
                ((uint8_t *)&t323)[8] = 1;
                gm_l17 = t323;
            }
            goto me308;
mn319:;
            gm_trap("no match arm matched the scrutinee", gm_srct4c97p4hek1p, 120, 9);
me308:;
        }
ix285:;
ix280:;
ix276:;
    }
    {
        if (!((((uint8_t *)&gm_l3))[16] != 0)) goto ie324;
        gm_b16_8 t325 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l3));
        gm_l21 = t325;
        {
            void * t327 = gm_l0;
            uint64_t t328 = gm_l1;
            void * t329 = gm_l0;
            uint64_t t330 = GM_LD(uint64_t, (((uint8_t *)t329) + 24));
            gm_tl874t2atnlh3 t331 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_tg7cev6(t327, t328, t330));
            gm_b16_8 t332 = gm_l21;
            gm_f_error_s6hn871((void *)&F.l22, (void *)NULL, cx, NULL, t331, t332);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            if (!((((uint8_t *)&gm_l4))[16] != 0)) goto ie333;
            gm_b16_8 t334 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l4));
            gm_l23 = t334;
            {
                void * t336 = (void *)((uint8_t *)&F.l22);
                gm_b16_8 t337 = gm_l23;
                gm_tnpifv20irnph t338;
                GM_ST(gm_b16_8, ((uint8_t *)&t338 + 0), t337);
                gm_tabb710d9mq2e t339;
                gm_f_format_ooibcq2((void *)&t339, (void *)NULL, cx, NULL, t338);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t339))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t339) + 8, gm_srct4c97p4hek1p, 130, 40);
                gm_b16_8 t340 = GM_LD(gm_b16_8, (((uint8_t *)&t339) + 8));
                gm_b16_8 t341 = GM_LD(gm_b16_8, ((uint8_t *)&t340));
                void * t342 = gm_l0;
                uint64_t t343 = gm_l1;
                uint64_t t344 = gm_l1;
                uint64_t r345; bool o346 = __builtin_add_overflow(t344, ((uint64_t)2ULL), &r345);
                if (o346) gm_trap("integer overflow in `+`", gm_srct4c97p4hek1p, 130, 82);
                uint64_t t347 = r345;
                gm_tl874t2atnlh3 t348 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_tg7cev6(t342, t343, t347));
                gm_b16_8 t349 = gm_l23;
                gm_b0_1 t350 = gm_f_add_fix_0gkcr4c((void *)NULL, cx, NULL, t336, t341, t348, t349);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix333;
ie333:;
ix333:;
        }
        {
            void * t351 = gm_l0;
            void * t352 = GM_LD(void *, (((uint8_t *)t351) + 32));
            gm_f_push_o4ckdr1(t352, &(*(gm_t4gut2c9fj1kt *)((uint8_t *)&F.l22)));
        }
        goto ix324;
ie324:;
ix324:;
    }
    {
        uint8_t t354 = gm_l14;
        if (!t354) goto ie353;
        {
            void * t356 = gm_l0;
            uint32_t t357 = gm_l5;
            gm_t2lqu8jsj511d t358 = gm_l9;
            gm_tc70a9dbbmgkg t359 = gm_l10;
            gm_t913bb735pvt5 t360 = gm_l11;
            int64_t t361;
            if (((((uint8_t *)&t360))[8] != 0)) {
                int64_t t362 = GM_LD(int64_t, (((uint8_t *)&t360) + 0));
                t361 = t362;
            } else {
                t361 = ((int64_t)0LL);
            }
            gm_f_float_value_ip18r0p((void *)&F.l24, (void *)NULL, cx, NULL, t356, t357, t358, &t359, t361);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t363 = gm_l0;
            gm_tghmb53v8qi1e t364;
            memset((uint8_t *)&t364 + 99, 0, 5);
            memcpy((uint8_t *)&t364 + 0, ((uint8_t *)&F.l24), 96);
            gm_taari1s0m51a7 t365 = gm_l18;
            GM_ST(gm_taari1s0m51a7, (uint8_t *)&t364 + 96, t365);
            uint8_t t366 = gm_l16;
            GM_ST(uint8_t, (uint8_t *)&t364 + 98, t366);
            gm_tq7s98ihonkju t367 = gm_f_new_j2spmhj((void *)NULL, cx, NULL, &t364, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_knasprnpdnf)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t367))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t367) + 8, gm_srct4c97p4hek1p, 135, 24);
            void * t368 = GM_LD(void *, (((uint8_t *)&t367) + 8));
            gm_t3fcbuehh3bs2 t369;
            GM_ST(uint8_t, (uint8_t *)&t369 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t369 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t369 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t369 + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&t369 + 24, 0);
            GM_ST(uint8_t, ((uint8_t *)&t369 + 0), ((uint8_t)4ULL));
            GM_ST(void *, ((uint8_t *)&t369 + 8), t368);
            uint64_t t370 = gm_l1;
            void * t371 = gm_l0;
            uint64_t t372 = GM_LD(uint64_t, (((uint8_t *)t371) + 24));
            gm_b0_1 t373 = gm_f_emit_ekvv703((void *)NULL, cx, NULL, t363, &t369, t370, t372);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix353;
ie353:;
        {
            void * t375 = gm_l0;
            gm_t2lqu8jsj511d t376 = gm_l9;
            uint32_t t377 = gm_l5;
            gm_f_int_value_cnmrbr4((void *)&F.l25, (void *)NULL, cx, NULL, t375, t376, t377);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t378 = gm_l0;
            gm_ta42iqot1auae t379;
            memset((uint8_t *)&t379 + 61, 0, 3);
            memcpy((uint8_t *)&t379 + 0, ((uint8_t *)&F.l25), 48);
            gm_t46lmulm8gaj3 t380 = gm_l17;
            GM_ST(gm_t46lmulm8gaj3, (uint8_t *)&t379 + 48, t380);
            GM_ST(uint8_t, (uint8_t *)&t379 + 60, ((uint8_t)0));
            gm_tq7s98ihonkju t381 = gm_f_new_q1fs5ps((void *)NULL, cx, NULL, &t379, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_k2kjji37v2m)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t381))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t381) + 8, gm_srct4c97p4hek1p, 138, 22);
            void * t382 = GM_LD(void *, (((uint8_t *)&t381) + 8));
            gm_t3fcbuehh3bs2 t383;
            GM_ST(uint8_t, (uint8_t *)&t383 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t383 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t383 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t383 + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&t383 + 24, 0);
            GM_ST(uint8_t, ((uint8_t *)&t383 + 0), ((uint8_t)3ULL));
            GM_ST(void *, ((uint8_t *)&t383 + 8), t382);
            uint64_t t384 = gm_l1;
            void * t385 = gm_l0;
            uint64_t t386 = GM_LD(uint64_t, (((uint8_t *)t385) + 24));
            gm_b0_1 t387 = gm_f_emit_ekvv703((void *)NULL, cx, NULL, t378, &t383, t384, t386);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
ix353:;
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
__attribute__((aligned(4))) gm_b0_1 gm_f_lex_raw_string_uf6bd4g(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_lex_raw_string_uf6bd4g F;
    void * gm_l0; /* lx */
    uint64_t gm_l1; /* lo */
    uint64_t gm_l2; /* delim */
    uint8_t gm_l5; /* b */
    uint64_t gm_l6; /* run */
    uint64_t gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    GM_STACK_CHECK(&F, 811u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        uint64_t t3 = GM_LD(uint64_t, (((uint8_t *)t2) + 24));
        gm_l1 = t3;
    }
    {
        void * t4 = gm_l0;
        gm_b16_8 t5 = GM_LD(gm_b16_8, (((uint8_t *)t4) + 8));
        void * t6 = gm_l0;
        uint64_t t7 = GM_LD(uint64_t, (((uint8_t *)t6) + 24));
        uint64_t t8 = gm_f_backtick_run_l95fb09(t5, t7);
        gm_l2 = t8;
    }
    {
        void * t9 = gm_l0;
        void * t10 = gm_l0;
        uint64_t t11 = GM_LD(uint64_t, (((uint8_t *)t10) + 24));
        uint64_t t12 = gm_l2;
        uint64_t r13; bool o14 = __builtin_add_overflow(t11, t12, &r13);
        if (o14) gm_trap("integer overflow in `+`", gm_srct4c97p4hek1p, 473, 5);
        uint64_t t15 = r13;
        GM_ST(uint64_t, (((uint8_t *)t9) + 24), t15);
    }
    {
        void * t17 = gm_l0;
        gm_tqs5p4n2tq19v t18 = gm_f_peek_72bostn(t17);
        gm_tqs5p4n2tq19v t19;
        GM_ST(uint8_t, ((uint8_t *)&t19), ((uint8_t)10ULL));
        ((uint8_t *)&t19)[1] = 1;
        uint8_t t20 = (gm_eqb44cb3cf1n(((uint8_t *)&t18), ((uint8_t *)&t19)));
        if (!t20) goto ie16;
        {
            void * t22 = gm_l0;
            void * t23 = gm_l0;
            uint64_t t24 = GM_LD(uint64_t, (((uint8_t *)t23) + 24));
            uint64_t r25; bool o26 = __builtin_add_overflow(t24, ((uint64_t)1ULL), &r25);
            if (o26) gm_trap("integer overflow in `+`", gm_srct4c97p4hek1p, 475, 9);
            uint64_t t27 = r25;
            GM_ST(uint64_t, (((uint8_t *)t22) + 24), t27);
        }
        goto ix16;
ie16:;
        void * t29 = gm_l0;
        gm_tqs5p4n2tq19v t30 = gm_f_peek_72bostn(t29);
        gm_tqs5p4n2tq19v t31;
        GM_ST(uint8_t, ((uint8_t *)&t31), ((uint8_t)13ULL));
        ((uint8_t *)&t31)[1] = 1;
        uint8_t t32 = (gm_eqb44cb3cf1n(((uint8_t *)&t30), ((uint8_t *)&t31)));
        if (!t32) goto ie28;
        void * t33 = gm_l0;
        gm_tqs5p4n2tq19v t34 = gm_f_peek_at_74p8lkq(t33, ((uint64_t)1ULL));
        gm_tqs5p4n2tq19v t35;
        GM_ST(uint8_t, ((uint8_t *)&t35), ((uint8_t)10ULL));
        ((uint8_t *)&t35)[1] = 1;
        uint8_t t36 = (gm_eqb44cb3cf1n(((uint8_t *)&t34), ((uint8_t *)&t35)));
        if (!t36) goto ie28;
        {
            void * t38 = gm_l0;
            void * t39 = gm_l0;
            uint64_t t40 = GM_LD(uint64_t, (((uint8_t *)t39) + 24));
            uint64_t r41; bool o42 = __builtin_add_overflow(t40, ((uint64_t)2ULL), &r41);
            if (o42) gm_trap("integer overflow in `+`", gm_srct4c97p4hek1p, 477, 9);
            uint64_t t43 = r41;
            GM_ST(uint64_t, (((uint8_t *)t38) + 24), t43);
        }
        goto ix28;
ie28:;
ix28:;
ix16:;
    }
    {
        gm_f_list_uvfu1k1((void *)&F.l3, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        for (;;) {
            if (!((uint8_t)1)) goto lb44;
            {
                void * t47 = gm_l0;
                gm_tqs5p4n2tq19v t48 = gm_f_peek_72bostn(t47);
                uint8_t t49;
                if (((((uint8_t *)&t48))[1] != 0)) {
                    uint8_t t50 = GM_LD(uint8_t, (((uint8_t *)&t48) + 0));
                    t49 = t50;
                } else {
                    {
                        void * t52 = gm_l0;
                        uint64_t t53 = gm_l1;
                        uint64_t t54 = gm_l1;
                        uint64_t t55 = gm_l2;
                        uint64_t r56; bool o57 = __builtin_add_overflow(t54, t55, &r56);
                        if (o57) gm_trap("integer overflow in `+`", gm_srct4c97p4hek1p, 482, 41);
                        uint64_t t58 = r56;
                        gm_tl874t2atnlh3 t59 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_tg7cev6(t52, t53, t58));
                        gm_b16_8 t60;
                        GM_ST(void *, (uint8_t *)&t60, (void *)(gm_srnee0q49ttns));
                        GM_ST(uintptr_t, (uint8_t *)&t60 + 8, (uintptr_t)(31));
                        gm_f_error_s6hn871((void *)&F.l4, (void *)NULL, cx, NULL, t59, t60);
                        if (gm_unw.kind) {
                            goto gm_ll45;
                        }
                    }
                    {
                        void * t61 = (void *)((uint8_t *)&F.l4);
                        uint64_t t62 = gm_l2;
                        gm_t4ce0jd3tdro4 t63;
                        GM_ST(uint64_t, ((uint8_t *)&t63 + 0), t62);
                        gm_tabb710d9mq2e t64;
                        gm_f_format_ikusttp((void *)&t64, (void *)NULL, cx, NULL, t63);
                        if (gm_unw.kind) {
                            goto gm_ll45;
                        }
                        if (((((uint8_t *)&t64))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t64) + 8, gm_srct4c97p4hek1p, 483, 24);
                        gm_b16_8 t65 = GM_LD(gm_b16_8, (((uint8_t *)&t64) + 8));
                        gm_b16_8 t66 = GM_LD(gm_b16_8, ((uint8_t *)&t65));
                        gm_f_add_note_46v502s(t61, t66);
                    }
                    {
                        void * t67 = gm_l0;
                        void * t68 = GM_LD(void *, (((uint8_t *)t67) + 32));
                        gm_f_push_o4ckdr1(t68, &(*(gm_t4gut2c9fj1kt *)((uint8_t *)&F.l4)));
                    }
                    goto lb44;
                }
                gm_l5 = t49;
            }
            {
                uint8_t t70 = gm_l5;
                uint8_t t71 = ((t70 == ((uint8_t)96ULL)));
                if (!t71) goto ie69;
                {
                    void * t73 = gm_l0;
                    gm_b16_8 t74 = GM_LD(gm_b16_8, (((uint8_t *)t73) + 8));
                    void * t75 = gm_l0;
                    uint64_t t76 = GM_LD(uint64_t, (((uint8_t *)t75) + 24));
                    uint64_t t77 = gm_f_backtick_run_l95fb09(t74, t76);
                    gm_l6 = t77;
                }
                {
                    void * t78 = gm_l0;
                    void * t79 = gm_l0;
                    uint64_t t80 = GM_LD(uint64_t, (((uint8_t *)t79) + 24));
                    uint64_t t81 = gm_l6;
                    uint64_t r82; bool o83 = __builtin_add_overflow(t80, t81, &r82);
                    if (o83) gm_trap("integer overflow in `+`", gm_srct4c97p4hek1p, 489, 13);
                    uint64_t t84 = r82;
                    GM_ST(uint64_t, (((uint8_t *)t78) + 24), t84);
                }
                {
                    uint64_t t86 = gm_l6;
                    uint64_t t87 = gm_l2;
                    uint8_t t88 = ((t86 == t87));
                    if (!t88) goto ie85;
                    {
                        goto lb44;
                    }
                    goto ix85;
ie85:;
ix85:;
                }
                {
                    {
                        gm_l8 = ((uint64_t)0ULL);
                    }
                    {
                        uint64_t t91 = gm_l6;
                        gm_l7 = t91;
                    }
                    {
                        for (;;) {
                            uint64_t t93 = gm_l8;
                            uint64_t t94 = gm_l7;
                            uint8_t t95 = (t93 < t94);
                            if (!t95) goto lb92;
                            {
                                uint64_t t98 = gm_l8;
                                {
                                    void * t102 = (void *)((uint8_t *)&F.l3);
                                    gm_tftb1odc739t5 t103 = gm_f_push_t83rg2m(t102, ((uint8_t)96ULL));
                                    if (((((uint8_t *)&t103))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t103) + 1, gm_srct4c97p4hek1p, 491, 31);
                                    gm_b0_1 t104 = GM_LD(gm_b0_1, (((uint8_t *)&t103) + 1));
                                }
                                goto me99;
mn100:;
                                gm_trap("no match arm matched the scrutinee", gm_srct4c97p4hek1p, 491, 13);
me99:;
                            }
lc92:;
                            {
                                uint64_t t106 = gm_l8;
                                uint64_t r107; bool o108 = __builtin_add_overflow(t106, ((uint64_t)1ULL), &r107);
                                uint64_t t109 = r107;
                                gm_l8 = t109;
                            }
                        }
lb92:;
                    }
                }
                goto ix69;
ie69:;
                uint8_t t111 = gm_l5;
                uint8_t t112 = ((t111 == ((uint8_t)13ULL)));
                if (!t112) goto ie110;
                {
                    void * t115 = gm_l0;
                    gm_tqs5p4n2tq19v t116 = gm_f_peek_at_74p8lkq(t115, ((uint64_t)1ULL));
                    gm_tqs5p4n2tq19v t117;
                    GM_ST(uint8_t, ((uint8_t *)&t117), ((uint8_t)10ULL));
                    ((uint8_t *)&t117)[1] = 1;
                    uint8_t t118 = (gm_eqb44cb3cf1n(((uint8_t *)&t116), ((uint8_t *)&t117)));
                    if (!t118) goto ie114;
                    {
                        void * t120 = (void *)((uint8_t *)&F.l3);
                        gm_tftb1odc739t5 t121 = gm_f_push_t83rg2m(t120, ((uint8_t)10ULL));
                        if (((((uint8_t *)&t121))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t121) + 1, gm_srct4c97p4hek1p, 494, 17);
                        gm_b0_1 t122 = GM_LD(gm_b0_1, (((uint8_t *)&t121) + 1));
                    }
                    {
                        void * t123 = gm_l0;
                        void * t124 = gm_l0;
                        uint64_t t125 = GM_LD(uint64_t, (((uint8_t *)t124) + 24));
                        uint64_t r126; bool o127 = __builtin_add_overflow(t125, ((uint64_t)2ULL), &r126);
                        if (o127) gm_trap("integer overflow in `+`", gm_srct4c97p4hek1p, 495, 17);
                        uint64_t t128 = r126;
                        GM_ST(uint64_t, (((uint8_t *)t123) + 24), t128);
                    }
                    goto ix114;
ie114:;
                    {
                        void * t130 = gm_l0;
                        gm_b0_1 t131 = gm_f_lone_cr_ktb0jvm((void *)NULL, cx, NULL, t130);
                        if (gm_unw.kind) {
                            goto gm_ll45;
                        }
                    }
ix114:;
                }
                goto ix110;
ie110:;
                {
                    void * t133 = (void *)((uint8_t *)&F.l3);
                    uint8_t t134 = gm_l5;
                    gm_tftb1odc739t5 t135 = gm_f_push_t83rg2m(t133, t134);
                    if (((((uint8_t *)&t135))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t135) + 1, gm_srct4c97p4hek1p, 500, 13);
                    gm_b0_1 t136 = GM_LD(gm_b0_1, (((uint8_t *)&t135) + 1));
                }
                {
                    void * t137 = gm_l0;
                    void * t138 = gm_l0;
                    uint64_t t139 = GM_LD(uint64_t, (((uint8_t *)t138) + 24));
                    uint64_t r140; bool o141 = __builtin_add_overflow(t139, ((uint64_t)1ULL), &r140);
                    if (o141) gm_trap("integer overflow in `+`", gm_srct4c97p4hek1p, 501, 13);
                    uint64_t t142 = r140;
                    GM_ST(uint64_t, (((uint8_t *)t137) + 24), t142);
                }
ix110:;
ix69:;
            }
lc44:;
        }
gm_ll45:;
        if (gm_unw.frame == &F.h && gm_unw.label == 0) {
            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb44; }
            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc44; }
        }
        goto gm_lad;
lb44:;
    }
    {
        void * t143 = gm_l0;
        gm_tibupt4dmcqai t144;
        memset((uint8_t *)&t144 + 17, 0, 7);
        gm_b16_8 t145 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
        GM_ST(gm_b16_8, (uint8_t *)&t144 + 0, t145);
        gm_t9qkh0rsooc8d t146;
        GM_ST(uint8_t, ((uint8_t *)&t146 + 0), ((uint8_t)1ULL));
        GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&t144 + 16, t146);
        gm_t3fcbuehh3bs2 t147;
        GM_ST(uint8_t, (uint8_t *)&t147 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t147 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t147 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t147 + 0), ((uint8_t)5ULL));
        GM_ST(gm_tibupt4dmcqai, ((uint8_t *)&t147 + 8), t144);
        uint64_t t148 = gm_l1;
        void * t149 = gm_l0;
        uint64_t t150 = GM_LD(uint64_t, (((uint8_t *)t149) + 24));
        gm_b0_1 t151 = gm_f_emit_ekvv703((void *)NULL, cx, NULL, t143, &t147, t148, t150);
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
__attribute__((aligned(4))) gm_b0_1 gm_f_lex_string_bcdbdjm(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_t9qkh0rsooc8d a2) {
    struct gm_fr_lex_string_bcdbdjm F;
    void * gm_l0; /* lx */
    uint64_t gm_l1; /* lo */
    gm_t9qkh0rsooc8d gm_l2; /* kind */
    uint8_t gm_l4; /* b */
    uint32_t gm_l5; /* c */
    uint8_t gm_l6; /* x */
    GM_STACK_CHECK(&F, 474u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        void * t3 = gm_l0;
        uint64_t t4 = GM_LD(uint64_t, (((uint8_t *)t3) + 24));
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)1ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_srct4c97p4hek1p, 425, 5);
        uint64_t t7 = r5;
        GM_ST(uint64_t, (((uint8_t *)t2) + 24), t7);
    }
    {
        gm_f_list_uvfu1k1((void *)&F.l3, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        for (;;) {
            if (!((uint8_t)1)) goto lb8;
            {
                void * t11 = gm_l0;
                gm_tqs5p4n2tq19v t12 = gm_f_peek_72bostn(t11);
                uint8_t t13;
                if (((((uint8_t *)&t12))[1] != 0)) {
                    uint8_t t14 = GM_LD(uint8_t, (((uint8_t *)&t12) + 0));
                    t13 = t14;
                } else {
                    {
                        void * t16 = gm_l0;
                        uint64_t t17 = gm_l1;
                        void * t18 = gm_l0;
                        uint64_t t19 = GM_LD(uint64_t, (((uint8_t *)t18) + 24));
                        gm_b16_8 t20;
                        GM_ST(void *, (uint8_t *)&t20, (void *)(gm_sjblck9bpb6r8));
                        GM_ST(uintptr_t, (uint8_t *)&t20 + 8, (uintptr_t)(27));
                        gm_b0_1 t21 = gm_f_error_p5r0d56((void *)NULL, cx, NULL, t16, t17, t19, t20);
                        if (gm_unw.kind) {
                            goto gm_ll9;
                        }
                    }
                    goto lb8;
                }
                gm_l4 = t13;
            }
            {
                uint8_t t23 = gm_l4;
                uint8_t t24 = ((t23 == ((uint8_t)34ULL)));
                if (!t24) goto ie22;
                {
                    void * t26 = gm_l0;
                    void * t27 = gm_l0;
                    uint64_t t28 = GM_LD(uint64_t, (((uint8_t *)t27) + 24));
                    uint64_t r29; bool o30 = __builtin_add_overflow(t28, ((uint64_t)1ULL), &r29);
                    if (o30) gm_trap("integer overflow in `+`", gm_srct4c97p4hek1p, 433, 13);
                    uint64_t t31 = r29;
                    GM_ST(uint64_t, (((uint8_t *)t26) + 24), t31);
                }
                {
                    goto lb8;
                }
                goto ix22;
ie22:;
ix22:;
            }
            {
                uint8_t t33 = gm_l4;
                uint8_t t34 = ((t33 == ((uint8_t)10ULL)));
                if (!t34) goto ie32;
                {
                    void * t36 = gm_l0;
                    uint64_t t37 = gm_l1;
                    gm_b0_1 t38 = gm_f_unterminated_at_line_end_v8hbf1f((void *)NULL, cx, NULL, t36, t37);
                    if (gm_unw.kind) {
                        goto gm_ll9;
                    }
                }
                {
                    goto lb8;
                }
                goto ix32;
ie32:;
ix32:;
            }
            {
                uint8_t t40 = gm_l4;
                uint8_t t41 = ((t40 == ((uint8_t)13ULL)));
                if (!t41) goto ie39;
                {
                    void * t44 = gm_l0;
                    gm_tqs5p4n2tq19v t45 = gm_f_peek_at_74p8lkq(t44, ((uint64_t)1ULL));
                    gm_tqs5p4n2tq19v t46;
                    GM_ST(uint8_t, ((uint8_t *)&t46), ((uint8_t)10ULL));
                    ((uint8_t *)&t46)[1] = 1;
                    uint8_t t47 = (gm_eqb44cb3cf1n(((uint8_t *)&t45), ((uint8_t *)&t46)));
                    if (!t47) goto ie43;
                    {
                        void * t49 = gm_l0;
                        uint64_t t50 = gm_l1;
                        gm_b0_1 t51 = gm_f_unterminated_at_line_end_v8hbf1f((void *)NULL, cx, NULL, t49, t50);
                        if (gm_unw.kind) {
                            goto gm_ll9;
                        }
                    }
                    {
                        goto lb8;
                    }
                    goto ix43;
ie43:;
ix43:;
                }
                {
                    void * t52 = gm_l0;
                    gm_b0_1 t53 = gm_f_lone_cr_ktb0jvm((void *)NULL, cx, NULL, t52);
                    if (gm_unw.kind) {
                        goto gm_ll9;
                    }
                }
                goto ix39;
ie39:;
                uint8_t t55 = gm_l4;
                uint8_t t56 = ((t55 == ((uint8_t)92ULL)));
                if (!t56) goto ie54;
                {
                    void * t58 = gm_l0;
                    gm_ts06tb4mldv43 t59 = gm_f_lex_escape_7pnp2v0((void *)NULL, cx, NULL, t58);
                    if (gm_unw.kind) {
                        goto gm_ll9;
                    }
                    if (!((((uint8_t *)&t59))[8] != 0)) goto mn61;
                    uint8_t t62 = GM_LD(uint8_t, (((uint8_t *)&t59) + 0));
                    if (t62 != ((uint8_t)0ULL)) goto mn61;
                    uint32_t t63 = GM_LD(uint32_t, (((uint8_t *)&t59) + 4));
                    gm_l5 = t63;
                    void * t64 = (void *)((uint8_t *)&F.l3);
                    uint32_t t65 = gm_l5;
                    gm_f_push_utf8_bjq6ubs(t64, t65);
                    goto me60;
mn61:;
                    if (!((((uint8_t *)&t59))[8] != 0)) goto mn66;
                    uint8_t t67 = GM_LD(uint8_t, (((uint8_t *)&t59) + 0));
                    if (t67 != ((uint8_t)1ULL)) goto mn66;
                    uint8_t t68 = GM_LD(uint8_t, (((uint8_t *)&t59) + 4));
                    gm_l6 = t68;
                    void * t69 = (void *)((uint8_t *)&F.l3);
                    uint8_t t70 = gm_l6;
                    gm_tftb1odc739t5 t71 = gm_f_push_t83rg2m(t69, t70);
                    if (((((uint8_t *)&t71))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t71) + 1, gm_srct4c97p4hek1p, 449, 29);
                    gm_b0_1 t72 = GM_LD(gm_b0_1, (((uint8_t *)&t71) + 1));
                    goto me60;
mn66:;
                    if (((((uint8_t *)&t59))[8] != 0)) goto mn73;
                    goto me60;
mn73:;
                    gm_trap("no match arm matched the scrutinee", gm_srct4c97p4hek1p, 447, 13);
me60:;
                }
                goto ix54;
ie54:;
                {
                    void * t76 = (void *)((uint8_t *)&F.l3);
                    uint8_t t77 = gm_l4;
                    gm_tftb1odc739t5 t78 = gm_f_push_t83rg2m(t76, t77);
                    if (((((uint8_t *)&t78))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t78) + 1, gm_srct4c97p4hek1p, 453, 13);
                    gm_b0_1 t79 = GM_LD(gm_b0_1, (((uint8_t *)&t78) + 1));
                }
                {
                    void * t80 = gm_l0;
                    void * t81 = gm_l0;
                    uint64_t t82 = GM_LD(uint64_t, (((uint8_t *)t81) + 24));
                    uint64_t r83; bool o84 = __builtin_add_overflow(t82, ((uint64_t)1ULL), &r83);
                    if (o84) gm_trap("integer overflow in `+`", gm_srct4c97p4hek1p, 454, 13);
                    uint64_t t85 = r83;
                    GM_ST(uint64_t, (((uint8_t *)t80) + 24), t85);
                }
ix54:;
ix39:;
            }
lc8:;
        }
gm_ll9:;
        if (gm_unw.frame == &F.h && gm_unw.label == 0) {
            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb8; }
            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc8; }
        }
        goto gm_lad;
lb8:;
    }
    {
        uint8_t t87 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l2) + 0) == ((uint8_t)2ULL)));
        if (!t87) goto ie86;
        gm_b16_8 t88 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
        uint8_t t89 = 0;
        uint8_t t90 = ((uint8_t)0ULL);
        for (uintptr_t i = 0; i < (uintptr_t)(GM_LD(uintptr_t, ((uint8_t *)&t88) + 8)); i++) if (gm_eqjmccqdb90f(((uint8_t *)GM_LD(void *, ((uint8_t *)&t88))) + i * 1, ((uint8_t *)&t90))) { t89 = 1; break; }
        uint8_t t91 = (uint8_t)(t89);
        if (!t91) goto ie86;
        {
            void * t93 = gm_l0;
            uint64_t t94 = gm_l1;
            void * t95 = gm_l0;
            uint64_t t96 = GM_LD(uint64_t, (((uint8_t *)t95) + 24));
            gm_b16_8 t97;
            GM_ST(void *, (uint8_t *)&t97, (void *)(gm_sil0do0eri7f7));
            GM_ST(uintptr_t, (uint8_t *)&t97 + 8, (uintptr_t)(43));
            gm_b0_1 t98 = gm_f_error_p5r0d56((void *)NULL, cx, NULL, t93, t94, t96, t97);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix86;
ie86:;
ix86:;
    }
    {
        void * t99 = gm_l0;
        gm_tibupt4dmcqai t100;
        memset((uint8_t *)&t100 + 17, 0, 7);
        gm_b16_8 t101 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
        GM_ST(gm_b16_8, (uint8_t *)&t100 + 0, t101);
        gm_t9qkh0rsooc8d t102 = gm_l2;
        GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&t100 + 16, t102);
        gm_t3fcbuehh3bs2 t103;
        GM_ST(uint8_t, (uint8_t *)&t103 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t103 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t103 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t103 + 0), ((uint8_t)5ULL));
        GM_ST(gm_tibupt4dmcqai, ((uint8_t *)&t103 + 8), t100);
        uint64_t t104 = gm_l1;
        void * t105 = gm_l0;
        uint64_t t106 = GM_LD(uint64_t, (((uint8_t *)t105) + 24));
        gm_b0_1 t107 = gm_f_emit_ekvv703((void *)NULL, cx, NULL, t99, &t103, t104, t106);
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
__attribute__((aligned(4))) gm_ts06tb4mldv43 gm_f_lex_unicode_escape_ns81oou(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1) {
    struct gm_fr_lex_unicode_escape_ns81oou F;
    void * gm_l0; /* lx */
    uint64_t gm_l1; /* lo */
    uint64_t gm_l2; /* start */
    gm_b16_8 gm_l3; /* digits */
    uint32_t gm_l4; /* value */
    gm_b16_8 gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    uint8_t gm_l7; /* c */
    uint32_t gm_l8; /* <tmp> */
    gm_b16_8 gm_l9; /* why */
    GM_STACK_CHECK(&F, 997u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t3 = gm_l0;
        gm_tqs5p4n2tq19v t4 = gm_f_peek_72bostn(t3);
        gm_tqs5p4n2tq19v t5;
        GM_ST(uint8_t, ((uint8_t *)&t5), ((uint8_t)123ULL));
        ((uint8_t *)&t5)[1] = 1;
        uint8_t t6 = (!gm_eqb44cb3cf1n(((uint8_t *)&t4), ((uint8_t *)&t5)));
        if (!t6) goto ie2;
        {
            void * t8 = gm_l0;
            uint64_t t9 = gm_l1;
            void * t10 = gm_l0;
            uint64_t t11 = GM_LD(uint64_t, (((uint8_t *)t10) + 24));
            gm_b16_8 t12;
            GM_ST(void *, (uint8_t *)&t12, (void *)(gm_sh6hkhojsmti9));
            GM_ST(uintptr_t, (uint8_t *)&t12 + 8, (uintptr_t)(47));
            gm_b0_1 t13 = gm_f_error_p5r0d56((void *)NULL, cx, NULL, t8, t9, t11, t12);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_ts06tb4mldv43 t14;
            memset(&t14, 0, sizeof t14);
            uint8_t t15 = 0;
            F.ret = t14;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t16 = gm_l0;
        void * t17 = gm_l0;
        uint64_t t18 = GM_LD(uint64_t, (((uint8_t *)t17) + 24));
        uint64_t r19; bool o20 = __builtin_add_overflow(t18, ((uint64_t)1ULL), &r19);
        if (o20) gm_trap("integer overflow in `+`", gm_srct4c97p4hek1p, 378, 5);
        uint64_t t21 = r19;
        GM_ST(uint64_t, (((uint8_t *)t16) + 24), t21);
    }
    {
        void * t22 = gm_l0;
        uint64_t t23 = GM_LD(uint64_t, (((uint8_t *)t22) + 24));
        gm_l2 = t23;
    }
    {
        for (;;) {
            void * t25 = gm_l0;
            uint64_t t26 = GM_LD(uint64_t, (((uint8_t *)t25) + 24));
            void * t27 = gm_l0;
            uint64_t t28 = GM_LD(uintptr_t, (((uint8_t *)t27) + 8) + 8);
            uint8_t t29 = (t26 < t28);
            if (!t29) goto lb24;
            void * t30 = gm_l0;
            uint64_t t31 = GM_LD(uint64_t, (((uint8_t *)t30) + 24));
            uint64_t t32 = (uintptr_t)t31;
            void * t33 = gm_l0;
            uint64_t t34 = GM_LD(uintptr_t, (((uint8_t *)t33) + 8) + 8);
            if (t32 >= (uintptr_t)(t34)) gm_trap_index(t32, t34, gm_srct4c97p4hek1p, 380, 54);
            uint8_t t35 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)t33) + 8)) + t32 * 1));
            uint8_t t36 = gm_f_is_hex_digit_9lads51(t35);
            if (!t36) goto lb24;
            {
                void * t39 = gm_l0;
                void * t40 = gm_l0;
                uint64_t t41 = GM_LD(uint64_t, (((uint8_t *)t40) + 24));
                uint64_t r42; bool o43 = __builtin_add_overflow(t41, ((uint64_t)1ULL), &r42);
                if (o43) gm_trap("integer overflow in `+`", gm_srct4c97p4hek1p, 380, 72);
                uint64_t t44 = r42;
                GM_ST(uint64_t, (((uint8_t *)t39) + 24), t44);
            }
lc24:;
        }
lb24:;
    }
    {
        void * t45 = gm_l0;
        uint64_t t46 = GM_LD(uintptr_t, (((uint8_t *)t45) + 8) + 8);
        void * t47 = ((uint8_t *)GM_LD(void *, (((uint8_t *)t45) + 8)));
        uint64_t t48 = gm_l2;
        uint64_t t49 = (uintptr_t)t48;
        void * t50 = gm_l0;
        uint64_t t51 = GM_LD(uint64_t, (((uint8_t *)t50) + 24));
        uint64_t t52 = (uintptr_t)t51;
        if (t49 > t52 || t52 > (uintptr_t)t46) gm_trap_slice(t49, t52, t46, gm_srct4c97p4hek1p, 381, 15);
        gm_b16_8 t53;
        GM_ST(void *, (uint8_t *)&t53, (void *)(((uint8_t *)t47 + t49 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t53 + 8, (uintptr_t)(t52 - t49));
        gm_l3 = t53;
    }
    {
        void * t55 = gm_l0;
        gm_tqs5p4n2tq19v t56 = gm_f_peek_72bostn(t55);
        gm_tqs5p4n2tq19v t57;
        GM_ST(uint8_t, ((uint8_t *)&t57), ((uint8_t)125ULL));
        ((uint8_t *)&t57)[1] = 1;
        uint8_t t58 = (!gm_eqb44cb3cf1n(((uint8_t *)&t56), ((uint8_t *)&t57)));
        if (!t58) goto ie54;
        {
            void * t60 = gm_l0;
            uint64_t t61 = gm_l1;
            void * t62 = gm_l0;
            uint64_t t63 = GM_LD(uint64_t, (((uint8_t *)t62) + 24));
            gm_b16_8 t64;
            GM_ST(void *, (uint8_t *)&t64, (void *)(gm_s4hf5j4eu72mv));
            GM_ST(uintptr_t, (uint8_t *)&t64 + 8, (uintptr_t)(65));
            gm_b0_1 t65 = gm_f_error_p5r0d56((void *)NULL, cx, NULL, t60, t61, t63, t64);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_ts06tb4mldv43 t66;
            memset(&t66, 0, sizeof t66);
            uint8_t t67 = 0;
            F.ret = t66;
            goto gm_fret;
        }
        goto ix54;
ie54:;
ix54:;
    }
    {
        void * t68 = gm_l0;
        void * t69 = gm_l0;
        uint64_t t70 = GM_LD(uint64_t, (((uint8_t *)t69) + 24));
        uint64_t r71; bool o72 = __builtin_add_overflow(t70, ((uint64_t)1ULL), &r71);
        if (o72) gm_trap("integer overflow in `+`", gm_srct4c97p4hek1p, 386, 5);
        uint64_t t73 = r71;
        GM_ST(uint64_t, (((uint8_t *)t68) + 24), t73);
    }
    {
        uint8_t t75 = 0;
        uint64_t t76 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
        uint8_t t77 = ((t76 == ((uint64_t)0ULL)));
        if (!t77) {
            uint64_t t78 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
            uint8_t t79 = (t78 > ((uint64_t)6ULL));
            t75 = t79;
        } else t75 = 1;
        if (!t75) goto ie74;
        {
            void * t81 = gm_l0;
            uint64_t t82 = gm_l1;
            void * t83 = gm_l0;
            uint64_t t84 = GM_LD(uint64_t, (((uint8_t *)t83) + 24));
            gm_b16_8 t85;
            GM_ST(void *, (uint8_t *)&t85, (void *)(gm_sajhc6og5bv8v));
            GM_ST(uintptr_t, (uint8_t *)&t85 + 8, (uintptr_t)(58));
            gm_b0_1 t86 = gm_f_error_p5r0d56((void *)NULL, cx, NULL, t81, t82, t84, t85);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_ts06tb4mldv43 t87;
            memset(&t87, 0, sizeof t87);
            uint8_t t88 = 0;
            F.ret = t87;
            goto gm_fret;
        }
        goto ix74;
ie74:;
ix74:;
    }
    {
        gm_l4 = ((uint32_t)0ULL);
    }
    {
        {
            gm_b16_8 t90 = gm_l3;
            gm_l5 = t90;
        }
        {
            gm_l6 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t92 = gm_l6;
                uint64_t t93 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                uint8_t t94 = (t92 < t93);
                if (!t94) goto lb91;
                {
                    uint64_t t97 = gm_l6;
                    uint64_t t98 = (uintptr_t)t97;
                    uint64_t t99 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                    uint8_t t100 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l5)) + t98 * 1));
                    uint8_t t103 = GM_LD(uint8_t, ((uint8_t *)&t100));
                    gm_l7 = t103;
                    {
                        uint32_t t105 = gm_l4;
                        uint32_t r106; bool o107 = __builtin_mul_overflow(t105, ((uint32_t)16ULL), &r106);
                        if (o107) gm_trap("integer overflow in `*`", gm_srct4c97p4hek1p, 392, 31);
                        uint32_t t108 = r106;
                        uint8_t t109 = gm_l7;
                        uint32_t t110 = gm_f_digit_value_8ogbrqp(t109);
                        uint32_t r111; bool o112 = __builtin_add_overflow(t108, t110, &r111);
                        if (o112) gm_trap("integer overflow in `+`", gm_srct4c97p4hek1p, 392, 31);
                        uint32_t t113 = r111;
                        gm_l4 = t113;
                    }
                    goto me101;
mn102:;
                    gm_trap("no match arm matched the scrutinee", gm_srct4c97p4hek1p, 392, 5);
me101:;
                }
lc91:;
                {
                    uint64_t t115 = gm_l6;
                    uint64_t r116; bool o117 = __builtin_add_overflow(t115, ((uint64_t)1ULL), &r116);
                    uint64_t t118 = r116;
                    gm_l6 = t118;
                }
            }
lb91:;
        }
    }
    {
        uint8_t t120 = 0;
        uint32_t t121 = gm_l4;
        uint8_t t122 = (t121 > ((uint32_t)1114111ULL));
        if (!t122) {
            {
                uint32_t t124 = gm_l4;
                gm_l8 = t124;
            }
            uint8_t t125 = 0;
            uint32_t t126 = gm_l8;
            uint8_t t127 = (((uint32_t)55296ULL) <= t126);
            if (t127) {
                uint32_t t128 = gm_l8;
                uint8_t t129 = (t128 <= ((uint32_t)57343ULL));
                t125 = t129;
            } else t125 = 0;
            uint8_t t130 = t125;
            t120 = t130;
        } else t120 = 1;
        if (!t120) goto ie119;
        {
            gm_b16_8 t132;
            uint32_t t134 = gm_l4;
            uint8_t t135 = (t134 > ((uint32_t)1114111ULL));
            if (!t135) goto ie133;
            gm_b16_8 t137;
            GM_ST(void *, (uint8_t *)&t137, (void *)(gm_sovc43d45he5a));
            GM_ST(uintptr_t, (uint8_t *)&t137 + 8, (uintptr_t)(20));
            gm_b16_8 t138 = t137;
            t132 = t138;
            goto ix133;
ie133:;
            gm_b16_8 t140;
            GM_ST(void *, (uint8_t *)&t140, (void *)(gm_s32vm0u5vgiu5));
            GM_ST(uintptr_t, (uint8_t *)&t140 + 8, (uintptr_t)(17));
            gm_b16_8 t141 = t140;
            t132 = t141;
ix133:;
            gm_l9 = t132;
        }
        {
            void * t142 = gm_l0;
            uint64_t t143 = gm_l1;
            void * t144 = gm_l0;
            uint64_t t145 = GM_LD(uint64_t, (((uint8_t *)t144) + 24));
            gm_t6j6smj7qoctu t146;
            gm_b16_8 t147 = gm_l3;
            GM_ST(gm_b16_8, (uint8_t *)&t146 + 0, t147);
            gm_b16_8 t148 = gm_l9;
            GM_ST(gm_b16_8, (uint8_t *)&t146 + 16, t148);
            gm_tabb710d9mq2e t149;
            gm_f_format_ek7crui((void *)&t149, (void *)NULL, cx, NULL, &t146);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t149))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t149) + 8, gm_srct4c97p4hek1p, 395, 30);
            gm_b16_8 t150 = GM_LD(gm_b16_8, (((uint8_t *)&t149) + 8));
            gm_b16_8 t151 = GM_LD(gm_b16_8, ((uint8_t *)&t150));
            gm_b0_1 t152 = gm_f_error_p5r0d56((void *)NULL, cx, NULL, t142, t143, t145, t151);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_ts06tb4mldv43 t153;
            memset(&t153, 0, sizeof t153);
            uint8_t t154 = 0;
            F.ret = t153;
            goto gm_fret;
        }
        goto ix119;
ie119:;
ix119:;
    }
    {
        uint32_t t155 = gm_l4;
        gm_tum2strs1ckr2 t156;
        GM_ST(uint8_t, (uint8_t *)&t156 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t156 + 2, 0);
        GM_ST(uint8_t, ((uint8_t *)&t156 + 0), ((uint8_t)0ULL));
        GM_ST(uint32_t, ((uint8_t *)&t156 + 4), t155);
        gm_ts06tb4mldv43 t157;
        GM_ST(uint8_t, (uint8_t *)&t157 + 9, 0);
        GM_ST(uint16_t, (uint8_t *)&t157 + 10, 0);
        GM_ST(gm_tum2strs1ckr2, ((uint8_t *)&t157), t156);
        ((uint8_t *)&t157)[8] = 1;
        uint8_t t158 = 0;
        F.ret = t157;
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
inline __attribute__((aligned(4))) void gm_f_parse_valid_9fjef3j(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, uint32_t a1) {
    struct gm_fr_parse_valid_9fjef3j F;
    gm_b16_8 gm_l0; /* text */
    uint32_t gm_l1; /* radix */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    uint8_t gm_l4; /* c */
    GM_STACK_CHECK(&F, 152u);
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
                    uint8_t t13 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t11 * 1));
                    uint8_t t16 = GM_LD(uint8_t, ((uint8_t *)&t13));
                    gm_l4 = t16;
                    {
                        uint8_t t19 = gm_l4;
                        uint8_t t20 = (!(t19 == ((uint8_t)95ULL)));
                        if (!t20) goto ie18;
                        uint8_t t21 = gm_l4;
                        uint32_t t22 = gm_f_digit_value_8ogbrqp(t21);
                        uint32_t t23 = gm_l1;
                        uint8_t t24 = (t22 >= t23);
                        if (!t24) goto ie18;
                        {
                            gm_f_zero_s25l990((void *)&(*(gm_tbci5ospk5nnn *)gm_out), (void *)NULL, cx, NULL);
                            if (gm_unw.kind) {
                                goto gm_ll8;
                            }
                            goto gm_fdone;
                        }
                        goto ix18;
ie18:;
ix18:;
                    }
                    goto me14;
mn15:;
                    gm_trap("no match arm matched the scrutinee", gm_srct4c97p4hek1p, 230, 5);
me14:;
                }
lc4:;
                {
                    uint64_t t27 = gm_l3;
                    uint64_t r28; bool o29 = __builtin_add_overflow(t27, ((uint64_t)1ULL), &r28);
                    uint64_t t30 = r28;
                    gm_l3 = t30;
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
        gm_b16_8 t31 = gm_l0;
        uint32_t t32 = gm_l1;
        gm_f_parse_fbta82r((void *)&(*(gm_tbci5ospk5nnn *)gm_out), (void *)NULL, cx, NULL, t31, t32);
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
__attribute__((aligned(4))) void gm_f_push_utf8_bjq6ubs(void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_utf8_bjq6ubs F;
    void * gm_l0; /* out */
    uint32_t gm_l1; /* c */
    GM_STACK_CHECK(&F, 261u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint32_t t3 = gm_l1;
        uint8_t t4 = (t3 < ((uint32_t)128ULL));
        if (!t4) goto ie2;
        {
            void * t6 = gm_l0;
            uint32_t t7 = gm_l1;
            uint8_t t8 = (uint8_t)t7;
            gm_tftb1odc739t5 t9 = gm_f_push_t83rg2m(t6, t8);
            if (((((uint8_t *)&t9))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t9) + 1, gm_srct4c97p4hek1p, 404, 9);
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
        }
        goto ix2;
ie2:;
        uint32_t t12 = gm_l1;
        uint8_t t13 = (t12 < ((uint32_t)2048ULL));
        if (!t13) goto ie11;
        {
            void * t15 = gm_l0;
            uint32_t t16 = gm_l1;
            unsigned __int128 n17 = (unsigned __int128)((uint64_t)6ULL);
            if (n17 >= 32) gm_trap_shift((uint64_t)n17, "u32", gm_srct4c97p4hek1p, 406, 27);
            uint32_t t18 = (uint32_t)(t16 >> (int)n17);
            uint32_t t19 = (uint32_t)(((uint32_t)192ULL) | t18);
            uint8_t t20 = (uint8_t)t19;
            gm_tftb1odc739t5 t21 = gm_f_push_t83rg2m(t15, t20);
            if (((((uint8_t *)&t21))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t21) + 1, gm_srct4c97p4hek1p, 406, 9);
            gm_b0_1 t22 = GM_LD(gm_b0_1, (((uint8_t *)&t21) + 1));
        }
        {
            void * t23 = gm_l0;
            uint32_t t24 = gm_l1;
            uint32_t t25 = (uint32_t)(t24 & ((uint32_t)63ULL));
            uint32_t t26 = (uint32_t)(((uint32_t)128ULL) | t25);
            uint8_t t27 = (uint8_t)t26;
            gm_tftb1odc739t5 t28 = gm_f_push_t83rg2m(t23, t27);
            if (((((uint8_t *)&t28))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t28) + 1, gm_srct4c97p4hek1p, 407, 9);
            gm_b0_1 t29 = GM_LD(gm_b0_1, (((uint8_t *)&t28) + 1));
        }
        goto ix11;
ie11:;
        uint32_t t31 = gm_l1;
        uint8_t t32 = (t31 < ((uint32_t)65536ULL));
        if (!t32) goto ie30;
        {
            void * t34 = gm_l0;
            uint32_t t35 = gm_l1;
            unsigned __int128 n36 = (unsigned __int128)((uint64_t)12ULL);
            if (n36 >= 32) gm_trap_shift((uint64_t)n36, "u32", gm_srct4c97p4hek1p, 409, 27);
            uint32_t t37 = (uint32_t)(t35 >> (int)n36);
            uint32_t t38 = (uint32_t)(((uint32_t)224ULL) | t37);
            uint8_t t39 = (uint8_t)t38;
            gm_tftb1odc739t5 t40 = gm_f_push_t83rg2m(t34, t39);
            if (((((uint8_t *)&t40))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t40) + 1, gm_srct4c97p4hek1p, 409, 9);
            gm_b0_1 t41 = GM_LD(gm_b0_1, (((uint8_t *)&t40) + 1));
        }
        {
            void * t42 = gm_l0;
            uint32_t t43 = gm_l1;
            unsigned __int128 n44 = (unsigned __int128)((uint64_t)6ULL);
            if (n44 >= 32) gm_trap_shift((uint64_t)n44, "u32", gm_srct4c97p4hek1p, 410, 28);
            uint32_t t45 = (uint32_t)(t43 >> (int)n44);
            uint32_t t46 = (uint32_t)(t45 & ((uint32_t)63ULL));
            uint32_t t47 = (uint32_t)(((uint32_t)128ULL) | t46);
            uint8_t t48 = (uint8_t)t47;
            gm_tftb1odc739t5 t49 = gm_f_push_t83rg2m(t42, t48);
            if (((((uint8_t *)&t49))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t49) + 1, gm_srct4c97p4hek1p, 410, 9);
            gm_b0_1 t50 = GM_LD(gm_b0_1, (((uint8_t *)&t49) + 1));
        }
        {
            void * t51 = gm_l0;
            uint32_t t52 = gm_l1;
            uint32_t t53 = (uint32_t)(t52 & ((uint32_t)63ULL));
            uint32_t t54 = (uint32_t)(((uint32_t)128ULL) | t53);
            uint8_t t55 = (uint8_t)t54;
            gm_tftb1odc739t5 t56 = gm_f_push_t83rg2m(t51, t55);
            if (((((uint8_t *)&t56))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t56) + 1, gm_srct4c97p4hek1p, 411, 9);
            gm_b0_1 t57 = GM_LD(gm_b0_1, (((uint8_t *)&t56) + 1));
        }
        goto ix30;
ie30:;
        {
            void * t59 = gm_l0;
            uint32_t t60 = gm_l1;
            unsigned __int128 n61 = (unsigned __int128)((uint64_t)18ULL);
            if (n61 >= 32) gm_trap_shift((uint64_t)n61, "u32", gm_srct4c97p4hek1p, 413, 27);
            uint32_t t62 = (uint32_t)(t60 >> (int)n61);
            uint32_t t63 = (uint32_t)(((uint32_t)240ULL) | t62);
            uint8_t t64 = (uint8_t)t63;
            gm_tftb1odc739t5 t65 = gm_f_push_t83rg2m(t59, t64);
            if (((((uint8_t *)&t65))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t65) + 1, gm_srct4c97p4hek1p, 413, 9);
            gm_b0_1 t66 = GM_LD(gm_b0_1, (((uint8_t *)&t65) + 1));
        }
        {
            void * t67 = gm_l0;
            uint32_t t68 = gm_l1;
            unsigned __int128 n69 = (unsigned __int128)((uint64_t)12ULL);
            if (n69 >= 32) gm_trap_shift((uint64_t)n69, "u32", gm_srct4c97p4hek1p, 414, 28);
            uint32_t t70 = (uint32_t)(t68 >> (int)n69);
            uint32_t t71 = (uint32_t)(t70 & ((uint32_t)63ULL));
            uint32_t t72 = (uint32_t)(((uint32_t)128ULL) | t71);
            uint8_t t73 = (uint8_t)t72;
            gm_tftb1odc739t5 t74 = gm_f_push_t83rg2m(t67, t73);
            if (((((uint8_t *)&t74))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t74) + 1, gm_srct4c97p4hek1p, 414, 9);
            gm_b0_1 t75 = GM_LD(gm_b0_1, (((uint8_t *)&t74) + 1));
        }
        {
            void * t76 = gm_l0;
            uint32_t t77 = gm_l1;
            unsigned __int128 n78 = (unsigned __int128)((uint64_t)6ULL);
            if (n78 >= 32) gm_trap_shift((uint64_t)n78, "u32", gm_srct4c97p4hek1p, 415, 28);
            uint32_t t79 = (uint32_t)(t77 >> (int)n78);
            uint32_t t80 = (uint32_t)(t79 & ((uint32_t)63ULL));
            uint32_t t81 = (uint32_t)(((uint32_t)128ULL) | t80);
            uint8_t t82 = (uint8_t)t81;
            gm_tftb1odc739t5 t83 = gm_f_push_t83rg2m(t76, t82);
            if (((((uint8_t *)&t83))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t83) + 1, gm_srct4c97p4hek1p, 415, 9);
            gm_b0_1 t84 = GM_LD(gm_b0_1, (((uint8_t *)&t83) + 1));
        }
        {
            void * t85 = gm_l0;
            uint32_t t86 = gm_l1;
            uint32_t t87 = (uint32_t)(t86 & ((uint32_t)63ULL));
            uint32_t t88 = (uint32_t)(((uint32_t)128ULL) | t87);
            uint8_t t89 = (uint8_t)t88;
            gm_tftb1odc739t5 t90 = gm_f_push_t83rg2m(t85, t89);
            if (((((uint8_t *)&t90))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t90) + 1, gm_srct4c97p4hek1p, 416, 9);
            gm_b0_1 t91 = GM_LD(gm_b0_1, (((uint8_t *)&t90) + 1));
        }
ix30:;
ix11:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
extern __attribute__((aligned(4))) gm_b16_8 gm_f_radix_name_5kts8t0(uint32_t a0);
__attribute__((aligned(4))) gm_t2lqu8jsj511d gm_f_scan_digits_jhpsv6j(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, void * a2) {
    struct gm_fr_scan_digits_jhpsv6j F;
    void * gm_l0; /* lx */
    uint32_t gm_l1; /* radix */
    void * gm_l2; /* err */
    uint64_t gm_l3; /* start */
    uint8_t gm_l4; /* c */
    uint8_t gm_l5; /* ok */
    gm_b16_8 gm_l6; /* text */
    gm_b16_8 gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    uint64_t gm_l9; /* i */
    uint8_t gm_l10; /* c */
    GM_STACK_CHECK(&F, 766u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        uint64_t t3 = GM_LD(uint64_t, (((uint8_t *)t2) + 24));
        gm_l3 = t3;
    }
    {
        for (;;) {
            void * t5 = gm_l0;
            gm_tqs5p4n2tq19v t6 = gm_f_peek_72bostn(t5);
            if (!((((uint8_t *)&t6))[1] != 0)) goto lb4;
            uint8_t t7 = GM_LD(uint8_t, ((uint8_t *)&t6));
            gm_l4 = t7;
            {
                uint8_t t10;
                uint32_t t12 = gm_l1;
                uint8_t t13 = ((t12 == ((uint32_t)16ULL)));
                if (!t13) goto ie11;
                uint8_t t15 = gm_l4;
                uint8_t t16 = gm_f_is_hex_digit_9lads51(t15);
                uint8_t t17 = t16;
                t10 = t17;
                goto ix11;
ie11:;
                uint8_t t19 = gm_l4;
                uint8_t t20 = gm_f_is_digit_2hoggnj(t19);
                uint8_t t21 = t20;
                t10 = t21;
ix11:;
                gm_l5 = t10;
            }
            {
                uint8_t t23 = 0;
                uint8_t t24 = gm_l5;
                if (!t24) {
                    uint8_t t25 = gm_l4;
                    uint8_t t26 = ((t25 == ((uint8_t)95ULL)));
                    t23 = t26;
                } else t23 = 1;
                uint8_t t27 = (uint8_t)!t23;
                if (!t27) goto ie22;
                {
                    goto lb4;
                }
                goto ix22;
ie22:;
ix22:;
            }
            {
                void * t29 = gm_l0;
                void * t30 = gm_l0;
                uint64_t t31 = GM_LD(uint64_t, (((uint8_t *)t30) + 24));
                uint64_t r32; bool o33 = __builtin_add_overflow(t31, ((uint64_t)1ULL), &r32);
                if (o33) gm_trap("integer overflow in `+`", gm_srct4c97p4hek1p, 170, 9);
                uint64_t t34 = r32;
                GM_ST(uint64_t, (((uint8_t *)t29) + 24), t34);
            }
lc4:;
        }
lb4:;
    }
    {
        void * t35 = gm_l0;
        uint64_t t36 = GM_LD(uintptr_t, (((uint8_t *)t35) + 8) + 8);
        void * t37 = ((uint8_t *)GM_LD(void *, (((uint8_t *)t35) + 8)));
        uint64_t t38 = gm_l3;
        uint64_t t39 = (uintptr_t)t38;
        void * t40 = gm_l0;
        uint64_t t41 = GM_LD(uint64_t, (((uint8_t *)t40) + 24));
        uint64_t t42 = (uintptr_t)t41;
        if (t39 > t42 || t42 > (uintptr_t)t36) gm_trap_slice(t39, t42, t36, gm_srct4c97p4hek1p, 172, 13);
        gm_b16_8 t43;
        GM_ST(void *, (uint8_t *)&t43, (void *)(((uint8_t *)t37 + t39 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t43 + 8, (uintptr_t)(t42 - t39));
        gm_l6 = t43;
    }
    {
        void * t45 = gm_l2;
        uint8_t t46;
        if (((((uint8_t *)t45))[16] != 0)) goto mn48;
        t46 = ((uint8_t)1);
        goto me47;
mn48:;
        t46 = ((uint8_t)0);
        goto me47;
mn49:;
        gm_trap("no match arm matched the scrutinee", gm_srct4c97p4hek1p, 173, 8);
me47:;
        if (!t46) goto ie44;
        uint8_t t50 = 0;
        uint8_t t51 = 0;
        gm_b16_8 t52 = gm_l6;
        gm_b16_8 t53;
        GM_ST(void *, (uint8_t *)&t53, (void *)(gm_sfdbfrv46lihd));
        GM_ST(uintptr_t, (uint8_t *)&t53 + 8, (uintptr_t)(1));
        uint8_t t54 = gm_f_starts_with_6vvt01e(t52, t53);
        if (!t54) {
            gm_b16_8 t55 = gm_l6;
            gm_b16_8 t56;
            GM_ST(void *, (uint8_t *)&t56, (void *)(gm_sfdbfrv46lihd));
            GM_ST(uintptr_t, (uint8_t *)&t56 + 8, (uintptr_t)(1));
            uint8_t t57 = gm_f_ends_with_biriu5l(t55, t56);
            t51 = t57;
        } else t51 = 1;
        if (!t51) {
            gm_b16_8 t58 = gm_l6;
            gm_b16_8 t59;
            GM_ST(void *, (uint8_t *)&t59, (void *)(gm_sebojpbfjfgaf));
            GM_ST(uintptr_t, (uint8_t *)&t59 + 8, (uintptr_t)(2));
            uint8_t t60 = gm_f_contains_gkki4kq(t58, t59);
            t50 = t60;
        } else t50 = 1;
        if (!t50) goto ie44;
        {
            void * t62 = gm_l2;
            gm_b16_8 t63;
            GM_ST(void *, (uint8_t *)&t63, (void *)(gm_sg550i4fp569j));
            GM_ST(uintptr_t, (uint8_t *)&t63 + 8, (uintptr_t)(54));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)t62)) + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)t62)) + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)t62)) + 20, 0);
            ((uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)t62)))[16] = 1;
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)t62)) + 0, t63);
        }
        goto ix44;
ie44:;
ix44:;
    }
    {
        void * t65 = gm_l2;
        uint8_t t66;
        if (((((uint8_t *)t65))[16] != 0)) goto mn68;
        t66 = ((uint8_t)1);
        goto me67;
mn68:;
        t66 = ((uint8_t)0);
        goto me67;
mn69:;
        gm_trap("no match arm matched the scrutinee", gm_srct4c97p4hek1p, 176, 8);
me67:;
        if (!t66) goto ie64;
        {
            {
                gm_b16_8 t72 = gm_l6;
                gm_l7 = t72;
            }
            {
                gm_l8 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t74 = gm_l8;
                    uint64_t t75 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                    uint8_t t76 = (t74 < t75);
                    if (!t76) goto lb73;
                    {
                        uint64_t t79 = gm_l8;
                        uint64_t t80 = gm_l8;
                        uint64_t t81 = (uintptr_t)t80;
                        uint64_t t82 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                        uint8_t t83 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l7)) + t81 * 1));
                        gm_tf5srh4mp4b3i t84;
                        GM_ST(uint8_t, (uint8_t *)&t84 + 9, 0);
                        GM_ST(uint16_t, (uint8_t *)&t84 + 10, 0);
                        GM_ST(uint32_t, (uint8_t *)&t84 + 12, 0);
                        GM_ST(uint64_t, ((uint8_t *)&t84 + 0), t79);
                        GM_ST(uint8_t, ((uint8_t *)&t84 + 8), t83);
                        uint64_t t87 = GM_LD(uint64_t, (((uint8_t *)&t84) + 0));
                        gm_l9 = t87;
                        uint8_t t88 = GM_LD(uint8_t, (((uint8_t *)&t84) + 8));
                        gm_l10 = t88;
                        {
                            uint8_t t91 = gm_l10;
                            uint8_t t92 = (!(t91 == ((uint8_t)95ULL)));
                            if (!t92) goto ie90;
                            uint8_t t93 = gm_l10;
                            uint32_t t94 = gm_f_digit_value_8ogbrqp(t93);
                            uint32_t t95 = gm_l1;
                            uint8_t t96 = (t94 >= t95);
                            if (!t96) goto ie90;
                            {
                                void * t98 = gm_l2;
                                uint8_t *t99 = ((uint8_t *)t98);
                                gm_t6j6smj7qoctu t100;
                                uint64_t t101 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                                void * t102 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)));
                                uint64_t t103 = gm_l9;
                                uint64_t t104 = (uintptr_t)t103;
                                uint64_t t105 = gm_l9;
                                uint64_t r106; bool o107 = __builtin_add_overflow(t105, ((uint64_t)1ULL), &r106);
                                if (o107) gm_trap("integer overflow in `+`", gm_srct4c97p4hek1p, 179, 79);
                                uint64_t t108 = r106;
                                uint64_t t109 = (uintptr_t)t108;
                                if (t104 > t109 || t109 > (uintptr_t)t101) gm_trap_slice(t104, t109, t101, gm_srct4c97p4hek1p, 179, 71);
                                gm_b16_8 t110;
                                GM_ST(void *, (uint8_t *)&t110, (void *)(((uint8_t *)t102 + t104 * 1)));
                                GM_ST(uintptr_t, (uint8_t *)&t110 + 8, (uintptr_t)(t109 - t104));
                                GM_ST(gm_b16_8, (uint8_t *)&t100 + 0, t110);
                                uint32_t t111 = gm_l1;
                                gm_b16_8 t112 = gm_f_radix_name_5kts8t0(t111);
                                GM_ST(gm_b16_8, (uint8_t *)&t100 + 16, t112);
                                gm_tabb710d9mq2e t113;
                                gm_f_format_51h4888((void *)&t113, (void *)NULL, cx, NULL, &t100);
                                if (gm_unw.kind) {
                                    goto gm_ll77;
                                }
                                if (((((uint8_t *)&t113))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t113) + 8, gm_srct4c97p4hek1p, 179, 24);
                                gm_b16_8 t114 = GM_LD(gm_b16_8, (((uint8_t *)&t113) + 8));
                                gm_b16_8 t115 = GM_LD(gm_b16_8, ((uint8_t *)&t114));
                                GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)t99) + 17, 0);
                                GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)t99) + 18, 0);
                                GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)t99) + 20, 0);
                                ((uint8_t *)&(*(gm_th2lqbo77pvsc *)t99))[16] = 1;
                                GM_ST(gm_b16_8, (uint8_t *)&(*(gm_th2lqbo77pvsc *)t99) + 0, t115);
                            }
                            {
                                goto lb73;
                            }
                            goto ix90;
ie90:;
ix90:;
                        }
                        goto me85;
mn86:;
                        gm_trap("no match arm matched the scrutinee", gm_srct4c97p4hek1p, 177, 9);
me85:;
                    }
lc73:;
                    {
                        uint64_t t117 = gm_l8;
                        uint64_t r118; bool o119 = __builtin_add_overflow(t117, ((uint64_t)1ULL), &r118);
                        uint64_t t120 = r118;
                        gm_l8 = t120;
                    }
                }
gm_ll77:;
                if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb73; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc73; }
                }
                goto gm_lad;
lb73:;
            }
        }
        goto ix64;
ie64:;
ix64:;
    }
    {
        uint64_t t121 = gm_l3;
        void * t122 = gm_l0;
        uint64_t t123 = GM_LD(uint64_t, (((uint8_t *)t122) + 24));
        gm_t2lqu8jsj511d t124;
        GM_ST(uint64_t, ((uint8_t *)&t124 + 0), t121);
        GM_ST(uint64_t, ((uint8_t *)&t124 + 8), t123);
        uint8_t t125 = 0;
        F.ret = t124;
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
__attribute__((aligned(4))) int64_t gm_f_scan_exponent_20o8k8o(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_scan_exponent_20o8k8o F;
    void * gm_l0; /* lx */
    void * gm_l1; /* err */
    uint8_t gm_l2; /* negative */
    gm_t2lqu8jsj511d gm_l3; /* digits */
    int64_t gm_l4; /* value */
    gm_b16_8 gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    uint8_t gm_l7; /* c */
    GM_STACK_CHECK(&F, 591u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        void * t3 = gm_l0;
        uint64_t t4 = GM_LD(uint64_t, (((uint8_t *)t3) + 24));
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)1ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_srct4c97p4hek1p, 199, 5);
        uint64_t t7 = r5;
        GM_ST(uint64_t, (((uint8_t *)t2) + 24), t7);
    }
    {
        gm_l2 = ((uint8_t)0);
    }
    {
        void * t9 = gm_l0;
        gm_tqs5p4n2tq19v t10 = gm_f_peek_72bostn(t9);
        gm_tqs5p4n2tq19v t11;
        GM_ST(uint8_t, ((uint8_t *)&t11), ((uint8_t)45ULL));
        ((uint8_t *)&t11)[1] = 1;
        uint8_t t12 = (gm_eqb44cb3cf1n(((uint8_t *)&t10), ((uint8_t *)&t11)));
        if (!t12) goto ie8;
        {
            void * t14 = gm_l0;
            void * t15 = gm_l0;
            uint64_t t16 = GM_LD(uint64_t, (((uint8_t *)t15) + 24));
            uint64_t r17; bool o18 = __builtin_add_overflow(t16, ((uint64_t)1ULL), &r17);
            if (o18) gm_trap("integer overflow in `+`", gm_srct4c97p4hek1p, 202, 9);
            uint64_t t19 = r17;
            GM_ST(uint64_t, (((uint8_t *)t14) + 24), t19);
        }
        {
            gm_l2 = ((uint8_t)1);
        }
        goto ix8;
ie8:;
        void * t21 = gm_l0;
        gm_tqs5p4n2tq19v t22 = gm_f_peek_72bostn(t21);
        gm_tqs5p4n2tq19v t23;
        GM_ST(uint8_t, ((uint8_t *)&t23), ((uint8_t)43ULL));
        ((uint8_t *)&t23)[1] = 1;
        uint8_t t24 = (gm_eqb44cb3cf1n(((uint8_t *)&t22), ((uint8_t *)&t23)));
        if (!t24) goto ie20;
        {
            void * t26 = gm_l0;
            void * t27 = gm_l0;
            uint64_t t28 = GM_LD(uint64_t, (((uint8_t *)t27) + 24));
            uint64_t r29; bool o30 = __builtin_add_overflow(t28, ((uint64_t)1ULL), &r29);
            if (o30) gm_trap("integer overflow in `+`", gm_srct4c97p4hek1p, 205, 9);
            uint64_t t31 = r29;
            GM_ST(uint64_t, (((uint8_t *)t26) + 24), t31);
        }
        goto ix20;
ie20:;
ix20:;
ix8:;
    }
    {
        void * t32 = gm_l0;
        void * t33 = gm_l1;
        gm_t2lqu8jsj511d t34 = gm_f_scan_digits_jhpsv6j((void *)NULL, cx, NULL, t32, ((uint32_t)10ULL), t33);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l3 = t34;
    }
    {
        gm_l4 = ((int64_t)0LL);
    }
    {
        {
            void * t36 = gm_l0;
            uint64_t t37 = GM_LD(uintptr_t, (((uint8_t *)t36) + 8) + 8);
            void * t38 = ((uint8_t *)GM_LD(void *, (((uint8_t *)t36) + 8)));
            uint64_t t39 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 0));
            uint64_t t40 = (uintptr_t)t39;
            uint64_t t41 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
            uint64_t t42 = (uintptr_t)t41;
            if (t40 > t42 || t42 > (uintptr_t)t37) gm_trap_slice(t40, t42, t37, gm_srct4c97p4hek1p, 209, 14);
            gm_b16_8 t43;
            GM_ST(void *, (uint8_t *)&t43, (void *)(((uint8_t *)t38 + t40 * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t43 + 8, (uintptr_t)(t42 - t40));
            gm_l5 = t43;
        }
        {
            gm_l6 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t45 = gm_l6;
                uint64_t t46 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                uint8_t t47 = (t45 < t46);
                if (!t47) goto lb44;
                {
                    uint64_t t50 = gm_l6;
                    uint64_t t51 = (uintptr_t)t50;
                    uint64_t t52 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                    uint8_t t53 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l5)) + t51 * 1));
                    uint8_t t56 = GM_LD(uint8_t, ((uint8_t *)&t53));
                    gm_l7 = t56;
                    {
                        uint8_t t59 = gm_l7;
                        uint8_t t60 = (!(t59 == ((uint8_t)95ULL)));
                        if (!t60) goto ie58;
                        {
                            int64_t t62 = gm_l4;
                            int64_t r63; bool o64 = __builtin_mul_overflow(t62, ((int64_t)10LL), &r63);
                            int64_t t65 = o64 ? (((t62 < 0) == (((int64_t)10LL) < 0)) ? ((int64_t)9223372036854775807LL) : ((int64_t)(-9223372036854775807LL - 1))) : r63;
                            uint8_t t66 = gm_l7;
                            uint8_t r67; bool o68 = __builtin_sub_overflow(t66, ((uint8_t)48ULL), &r67);
                            if (o68) gm_trap("integer overflow in `-`", gm_srct4c97p4hek1p, 211, 41);
                            uint8_t t69 = r67;
                            int64_t t70 = (int64_t)t69;
                            int64_t r71; bool o72 = __builtin_add_overflow(t65, t70, &r71);
                            int64_t t73 = o72 ? ((t70 > 0) ? ((int64_t)9223372036854775807LL) : ((int64_t)(-9223372036854775807LL - 1))) : r71;
                            int64_t t74 = GM_LD(int64_t, gm_cobksqr67t4);
                            int64_t r75; bool o76 = __builtin_add_overflow(t74, ((int64_t)1LL), &r75);
                            if (o76) gm_trap("integer overflow in `+`", gm_srct4c97p4hek1p, 211, 55);
                            int64_t t77 = r75;
                            int64_t t78 = (t73 < t77 ? t73 : t77);
                            gm_l4 = t78;
                        }
                        goto ix58;
ie58:;
ix58:;
                    }
                    goto me54;
mn55:;
                    gm_trap("no match arm matched the scrutinee", gm_srct4c97p4hek1p, 209, 5);
me54:;
                }
lc44:;
                {
                    uint64_t t80 = gm_l6;
                    uint64_t r81; bool o82 = __builtin_add_overflow(t80, ((uint64_t)1ULL), &r81);
                    uint64_t t83 = r81;
                    gm_l6 = t83;
                }
            }
lb44:;
        }
    }
    {
        int64_t t85 = gm_l4;
        int64_t t86 = GM_LD(int64_t, gm_cobksqr67t4);
        uint8_t t87 = (t85 > t86);
        if (!t87) goto ie84;
        {
            void * t90 = gm_l1;
            uint8_t t91;
            if (((((uint8_t *)t90))[16] != 0)) goto mn93;
            t91 = ((uint8_t)1);
            goto me92;
mn93:;
            t91 = ((uint8_t)0);
            goto me92;
mn94:;
            gm_trap("no match arm matched the scrutinee", gm_srct4c97p4hek1p, 215, 12);
me92:;
            if (!t91) goto ie89;
            {
                void * t96 = gm_l1;
                uint8_t *t97 = ((uint8_t *)t96);
                int64_t t98 = GM_LD(int64_t, gm_cobksqr67t4);
                gm_tm98ap4112hee t99;
                GM_ST(int64_t, ((uint8_t *)&t99 + 0), t98);
                gm_tabb710d9mq2e t100;
                gm_f_format_ufd866t((void *)&t100, (void *)NULL, cx, NULL, t99);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t100))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t100) + 8, gm_srct4c97p4hek1p, 216, 20);
                gm_b16_8 t101 = GM_LD(gm_b16_8, (((uint8_t *)&t100) + 8));
                gm_b16_8 t102 = GM_LD(gm_b16_8, ((uint8_t *)&t101));
                GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)t97) + 17, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)t97) + 18, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)t97) + 20, 0);
                ((uint8_t *)&(*(gm_th2lqbo77pvsc *)t97))[16] = 1;
                GM_ST(gm_b16_8, (uint8_t *)&(*(gm_th2lqbo77pvsc *)t97) + 0, t102);
            }
            goto ix89;
ie89:;
ix89:;
        }
        {
            gm_l4 = ((int64_t)0LL);
        }
        goto ix84;
ie84:;
ix84:;
    }
    {
        int64_t t103;
        uint8_t t105 = gm_l2;
        if (!t105) goto ie104;
        int64_t t107 = gm_l4;
        if (t107 == ((int64_t)(-9223372036854775807LL - 1))) gm_trap("integer overflow in `-`", gm_srct4c97p4hek1p, 220, 26);
        int64_t t108 = (int64_t)((uint64_t)0 - (uint64_t)t107);
        int64_t t109 = t108;
        t103 = t109;
        goto ix104;
ie104:;
        int64_t t111 = gm_l4;
        int64_t t112 = t111;
        t103 = t112;
ix104:;
        uint8_t t113 = 0;
        F.ret = t103;
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
inline __attribute__((aligned(4))) uint8_t gm_f_skip_to_closing_quote_qmb6ii3(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_skip_to_closing_quote_qmb6ii3 F;
    void * gm_l0; /* lx */
    uint64_t gm_l1; /* i */
    uint8_t gm_l2; /* b */
    GM_STACK_CHECK_SHALLOW(&F, 103u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        uint64_t t3 = GM_LD(uint64_t, (((uint8_t *)t2) + 24));
        gm_l1 = t3;
    }
    {
        for (;;) {
            void * t5 = gm_l0;
            uint64_t t6 = gm_l1;
            gm_tqs5p4n2tq19v t7 = gm_f_byte_b4v8vul(t5, t6);
            if (!((((uint8_t *)&t7))[1] != 0)) goto lb4;
            uint8_t t8 = GM_LD(uint8_t, ((uint8_t *)&t7));
            gm_l2 = t8;
            {
                uint8_t t12 = gm_l2;
                uint8_t t13 = ((t12 == ((uint8_t)39ULL)));
                if (!t13) goto ie11;
                {
                    void * t15 = gm_l0;
                    uint64_t t16 = gm_l1;
                    uint64_t r17; bool o18 = __builtin_add_overflow(t16, ((uint64_t)1ULL), &r17);
                    if (o18) gm_trap("integer overflow in `+`", gm_srct4c97p4hek1p, 310, 22);
                    uint64_t t19 = r17;
                    GM_ST(uint64_t, (((uint8_t *)t15) + 24), t19);
                }
                {
                    uint8_t t20 = 0;
                    F.ret = ((uint8_t)1);
                    goto gm_fret;
                }
                goto ix11;
ie11:;
ix11:;
            }
            {
                uint8_t t22 = gm_l2;
                gm_tqs5p4n2tq19v t23;
                GM_ST(uint8_t, ((uint8_t *)&t23 + 0), ((uint8_t)10ULL));
                GM_ST(uint8_t, ((uint8_t *)&t23 + 1), ((uint8_t)13ULL));
                uint8_t t24 = 0;
                if (gm_eqjmccqdb90f(((uint8_t *)&t23) + 0, ((uint8_t *)&t22))) t24 = 1;
                if (gm_eqjmccqdb90f(((uint8_t *)&t23) + 1, ((uint8_t *)&t22))) t24 = 1;
                uint8_t t25 = (uint8_t)(t24);
                if (!t25) goto ie21;
                {
                    goto lb4;
                }
                goto ix21;
ie21:;
ix21:;
            }
            {
                uint64_t t27 = gm_l1;
                uint64_t r28; bool o29 = __builtin_add_overflow(t27, ((uint64_t)1ULL), &r28);
                if (o29) gm_trap("integer overflow in `+`", gm_srct4c97p4hek1p, 314, 9);
                uint64_t t30 = r28;
                gm_l1 = t30;
            }
lc4:;
        }
lb4:;
    }
    {
        uint8_t t31 = 0;
        F.ret = ((uint8_t)0);
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_unterminated_at_line_end_v8hbf1f(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1) {
    struct gm_fr_unterminated_at_line_end_v8hbf1f F;
    void * gm_l0; /* lx */
    uint64_t gm_l1; /* lo */
    GM_STACK_CHECK(&F, 116u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        uint64_t t3 = gm_l1;
        void * t4 = gm_l0;
        uint64_t t5 = GM_LD(uint64_t, (((uint8_t *)t4) + 24));
        gm_tl874t2atnlh3 t6 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_tg7cev6(t2, t3, t5));
        gm_b16_8 t7;
        GM_ST(void *, (uint8_t *)&t7, (void *)(gm_sjblck9bpb6r8));
        GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)(27));
        gm_f_error_s6hn871((void *)&F.l2, (void *)NULL, cx, NULL, t6, t7);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t8 = (void *)((uint8_t *)&F.l2);
        gm_b16_8 t9;
        GM_ST(void *, (uint8_t *)&t9, (void *)(gm_sae23ckujbpp9));
        GM_ST(uintptr_t, (uint8_t *)&t9 + 8, (uintptr_t)(106));
        gm_f_add_note_46v502s(t8, t9);
    }
    {
        void * t10 = gm_l0;
        void * t11 = GM_LD(void *, (((uint8_t *)t10) + 32));
        gm_f_push_o4ckdr1(t11, &(*(gm_t4gut2c9fj1kt *)((uint8_t *)&F.l2)));
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
