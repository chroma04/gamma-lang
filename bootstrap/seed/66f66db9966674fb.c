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
typedef struct { int32_t f0; } gm_t19enj90vg8bd;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint64_t f0; } gm_t4ce0jd3tdro4;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { unsigned __int128 f0; uint8_t f1; uint8_t pe[15]; } gm_t9p38b040grms;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint32_t f0; uint32_t f1; } gm_tcc1rq4n1avb2;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_t08olq97rscn3;
_Static_assert(sizeof(gm_t19enj90vg8bd) == 4 && _Alignof(gm_t19enj90vg8bd) == 4 && offsetof(gm_t19enj90vg8bd, f0) == 0, "layout of `(i32,)`");
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
_Static_assert(sizeof(gm_t4ce0jd3tdro4) == 8 && _Alignof(gm_t4ce0jd3tdro4) == 8 && offsetof(gm_t4ce0jd3tdro4, f0) == 0, "layout of `Instant`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
_Static_assert(sizeof(gm_t9p38b040grms) == 32 && _Alignof(gm_t9p38b040grms) == 16 && offsetof(gm_t9p38b040grms, f0) == 0 && offsetof(gm_t9p38b040grms, f1) == 16, "layout of `?u128`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
_Static_assert(sizeof(gm_tcc1rq4n1avb2) == 8 && _Alignof(gm_tcc1rq4n1avb2) == 4 && offsetof(gm_tcc1rq4n1avb2, f0) == 0 && offsetof(gm_tcc1rq4n1avb2, f1) == 4, "layout of `LineCol`");
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_t9qkh0rsooc8d v3; } f1; } gm_ts6oqppjh0ba3;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_ts78ca2amthgu;
_Static_assert(sizeof(gm_t08olq97rscn3) == 32 && _Alignof(gm_t08olq97rscn3) == 8 && offsetof(gm_t08olq97rscn3, f0) == 0 && offsetof(gm_t08olq97rscn3, f1) == 16, "layout of `File`");
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t p3[4]; gm_trqfvmjtt4sij f3; gm_trqfvmjtt4sij f4; gm_trqfvmjtt4sij f5; gm_th2lqbo77pvsc f6; } gm_t4gut2c9fj1kt;
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
typedef struct { uint8_t f0; uint8_t p1[7]; gm_trqfvmjtt4sij f1; } gm_tbci5ospk5nnn;
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_ts6oqppjh0ba3) == 2 && _Alignof(gm_ts6oqppjh0ba3) == 1 && offsetof(gm_ts6oqppjh0ba3, f0) == 0 && offsetof(gm_ts6oqppjh0ba3, f1) == 1, "layout of `BinOp`");
_Static_assert(sizeof(gm_ts78ca2amthgu) == 40 && _Alignof(gm_ts78ca2amthgu) == 8 && offsetof(gm_ts78ca2amthgu, f0) == 0 && offsetof(gm_ts78ca2amthgu, f1) == 8 && offsetof(gm_ts78ca2amthgu, f2) == 24, "layout of `(ty: Ty, name: string, span: Span)`");
typedef struct { gm_ts6oqppjh0ba3 f0; gm_ts6oqppjh0ba3 f1; gm_ts6oqppjh0ba3 f2; gm_ts6oqppjh0ba3 f3; gm_ts6oqppjh0ba3 f4; gm_ts6oqppjh0ba3 f5; } gm_tvqq0vnt1mu0q;
_Static_assert(sizeof(gm_t4gut2c9fj1kt) == 184 && _Alignof(gm_t4gut2c9fj1kt) == 8 && offsetof(gm_t4gut2c9fj1kt, f0) == 0 && offsetof(gm_t4gut2c9fj1kt, f1) == 8 && offsetof(gm_t4gut2c9fj1kt, f2) == 24 && offsetof(gm_t4gut2c9fj1kt, f3) == 40 && offsetof(gm_t4gut2c9fj1kt, f4) == 80 && offsetof(gm_t4gut2c9fj1kt, f5) == 120 && offsetof(gm_t4gut2c9fj1kt, f6) == 160, "layout of `Diagnostic`");
_Static_assert(sizeof(gm_tbci5ospk5nnn) == 48 && _Alignof(gm_tbci5ospk5nnn) == 8 && offsetof(gm_tbci5ospk5nnn, f0) == 0 && offsetof(gm_tbci5ospk5nnn, f1) == 8, "layout of `Int`");
typedef struct { gm_tbci5ospk5nnn f0; gm_tbci5ospk5nnn f1; } gm_tngtuiuskn938;
_Static_assert(sizeof(gm_tvqq0vnt1mu0q) == 12 && _Alignof(gm_tvqq0vnt1mu0q) == 1 && offsetof(gm_tvqq0vnt1mu0q, f0) == 0 && offsetof(gm_tvqq0vnt1mu0q, f1) == 2 && offsetof(gm_tvqq0vnt1mu0q, f2) == 4 && offsetof(gm_tvqq0vnt1mu0q, f3) == 6 && offsetof(gm_tvqq0vnt1mu0q, f4) == 8 && offsetof(gm_tvqq0vnt1mu0q, f5) == 10, "layout of `(BinOp, BinOp, BinOp, BinOp, BinOp, BinOp)`");
_Static_assert(sizeof(gm_tngtuiuskn938) == 96 && _Alignof(gm_tngtuiuskn938) == 8 && offsetof(gm_tngtuiuskn938, f0) == 0 && offsetof(gm_tngtuiuskn938, f1) == 48, "layout of `Rat`");
typedef struct { gm_tngtuiuskn938 f0; gm_tngtuiuskn938 f1; } gm_tu15p0hi2qlv6;
typedef struct { gm_tngtuiuskn938 f0; uint8_t f1; uint8_t pe[7]; } gm_tvegkb6ouf3m0;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tbci5ospk5nnn v0; gm_tngtuiuskn938 v1; uint8_t v2; gm_tu15p0hi2qlv6 v3; } f1; } gm_t0ialelq3ql1k;
typedef struct { gm_tu15p0hi2qlv6 f0; uint8_t f1; uint8_t pe[7]; } gm_tk3f7nn6jq07c;
_Static_assert(sizeof(gm_tu15p0hi2qlv6) == 192 && _Alignof(gm_tu15p0hi2qlv6) == 8 && offsetof(gm_tu15p0hi2qlv6, f0) == 0 && offsetof(gm_tu15p0hi2qlv6, f1) == 96, "layout of `(re: Rat, im: Rat)`");
_Static_assert(sizeof(gm_tvegkb6ouf3m0) == 104 && _Alignof(gm_tvegkb6ouf3m0) == 8 && offsetof(gm_tvegkb6ouf3m0, f0) == 0 && offsetof(gm_tvegkb6ouf3m0, f1) == 96, "layout of `?Rat`");
_Static_assert(sizeof(gm_t0ialelq3ql1k) == 200 && _Alignof(gm_t0ialelq3ql1k) == 8 && offsetof(gm_t0ialelq3ql1k, f0) == 0 && offsetof(gm_t0ialelq3ql1k, f1) == 8, "layout of `Untyped`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t0ialelq3ql1k v0; gm_ts78ca2amthgu v1; } f1; } gm_t1hofbajv6dns;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t0ialelq3ql1k v0; gm_b16_8 v1; } f1; } gm_t4t568o7qnv02;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t0ialelq3ql1k v0; } f1; } gm_t5vbkdq81qfs7;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t88rcucg1b7uk v0; uint32_t v1; uint32_t v2; gm_tiilmfd16v3fc v3; gm_tnpifv20irnph v4; uint32_t v5; gm_tcc1rq4n1avb2 v6; gm_t88rcucg1b7uk v7; gm_t0ialelq3ql1k v8; uint64_t v9; gm_t88rcucg1b7uk v10; } f1; } gm_t6qooom8qa2do;
_Static_assert(sizeof(gm_tk3f7nn6jq07c) == 200 && _Alignof(gm_tk3f7nn6jq07c) == 8 && offsetof(gm_tk3f7nn6jq07c, f0) == 0 && offsetof(gm_tk3f7nn6jq07c, f1) == 192, "layout of `?(re: Rat, im: Rat)`");
_Static_assert(sizeof(gm_t1hofbajv6dns) == 208 && _Alignof(gm_t1hofbajv6dns) == 8 && offsetof(gm_t1hofbajv6dns, f0) == 0 && offsetof(gm_t1hofbajv6dns, f1) == 8, "layout of `LocalConstKind`");
_Static_assert(sizeof(gm_t4t568o7qnv02) == 208 && _Alignof(gm_t4t568o7qnv02) == 8 && offsetof(gm_t4t568o7qnv02, f0) == 0 && offsetof(gm_t4t568o7qnv02, f1) == 8, "layout of `UntypedOr`");
_Static_assert(sizeof(gm_t5vbkdq81qfs7) == 208 && _Alignof(gm_t5vbkdq81qfs7) == 8 && offsetof(gm_t5vbkdq81qfs7, f0) == 0 && offsetof(gm_t5vbkdq81qfs7, f1) == 8, "layout of `UntypedResult`");
_Static_assert(sizeof(gm_t6qooom8qa2do) == 208 && _Alignof(gm_t6qooom8qa2do) == 8 && offsetof(gm_t6qooom8qa2do, f0) == 0 && offsetof(gm_t6qooom8qa2do, f1) == 8, "layout of `DeclValue`");
typedef struct { gm_b16_8 f0; gm_t1hofbajv6dns f1; } gm_tb07pp6njg0bk;
typedef struct { gm_t5vbkdq81qfs7 f0; uint8_t f1; uint8_t pe[7]; } gm_tkv1vap90vht2;
_Static_assert(sizeof(gm_tb07pp6njg0bk) == 224 && _Alignof(gm_tb07pp6njg0bk) == 8 && offsetof(gm_tb07pp6njg0bk, f0) == 0 && offsetof(gm_tb07pp6njg0bk, f1) == 16, "layout of `LocalConst`");
typedef struct { gm_tb07pp6njg0bk f0; uint8_t f1; uint8_t pe[7]; } gm_tk1gen3hgkm51;
_Static_assert(sizeof(gm_tkv1vap90vht2) == 216 && _Alignof(gm_tkv1vap90vht2) == 8 && offsetof(gm_tkv1vap90vht2, f0) == 0 && offsetof(gm_tkv1vap90vht2, f1) == 208, "layout of `?UntypedResult`");
_Static_assert(sizeof(gm_tk1gen3hgkm51) == 232 && _Alignof(gm_tk1gen3hgkm51) == 8 && offsetof(gm_tk1gen3hgkm51, f0) == 0 && offsetof(gm_tk1gen3hgkm51, f1) == 224, "layout of `?LocalConst`");
struct gm_fr_add_420lj6o {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_tbci5ospk5nnn l1; /* b */
    gm_tbci5ospk5nnn ret;
};
struct gm_fr_add_837b4po {
    gm_frame h;
    gm_tngtuiuskn938 l0; /* a */
    gm_tngtuiuskn938 l1; /* b */
    gm_tngtuiuskn938 ret;
};
struct gm_fr_as_complex_7e8vejf {
    gm_frame h;
    gm_tngtuiuskn938 l1; /* re */
    gm_tngtuiuskn938 l2; /* im */
    gm_tngtuiuskn938 l3; /* r */
    gm_tk3f7nn6jq07c ret;
};
struct gm_fr_as_rational_dj3nve5 {
    gm_frame h;
    gm_tbci5ospk5nnn l1; /* i */
    gm_tngtuiuskn938 l2; /* r */
    gm_tvegkb6ouf3m0 ret;
};
struct gm_fr_binary_9sb7899 {
    gm_frame h;
    gm_t4t568o7qnv02 l3; /* r */
    gm_t0ialelq3ql1k l4; /* v */
    gm_tbci5ospk5nnn l5; /* i */
    gm_tngtuiuskn938 l6; /* q */
    gm_tngtuiuskn938 l7; /* re */
    gm_tngtuiuskn938 l8; /* im */
    gm_t4t568o7qnv02 ret;
};
struct gm_fr_binary_exact_8smjl0k {
    gm_frame h;
    gm_tngtuiuskn938 l5; /* x */
    gm_tngtuiuskn938 l6; /* y */
    gm_tbci5ospk5nnn l8; /* x */
    gm_tbci5ospk5nnn l9; /* y */
    gm_tngtuiuskn938 l10; /* x */
    gm_tngtuiuskn938 l11; /* y */
    gm_tngtuiuskn938 l12; /* r */
    gm_tbci5ospk5nnn l13; /* x */
    gm_tbci5ospk5nnn l14; /* y */
    gm_t4t568o7qnv02 ret;
};
struct gm_fr_bit_and_3tgam8m {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_tbci5ospk5nnn l1; /* b */
    gm_tbci5ospk5nnn ret;
};
struct gm_fr_bit_or_j1tdrhl {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_tbci5ospk5nnn l1; /* b */
    gm_tbci5ospk5nnn ret;
};
struct gm_fr_bit_xor_tmvdnad {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_tbci5ospk5nnn l1; /* b */
    gm_tbci5ospk5nnn ret;
};
struct gm_fr_cmp_9aseneh {
    gm_frame h;
    gm_tngtuiuskn938 l0; /* a */
    gm_tngtuiuskn938 l1; /* b */
    int32_t ret;
};
struct gm_fr_complex_binary_fggi5t8 {
    gm_frame h;
    gm_tu15p0hi2qlv6 l3; /* x */
    gm_tu15p0hi2qlv6 l4; /* y */
    gm_tngtuiuskn938 l5; /* p */
    gm_tngtuiuskn938 l6; /* q */
    gm_tngtuiuskn938 l7; /* r */
    gm_tngtuiuskn938 l8; /* t */
    gm_tngtuiuskn938 l9; /* d */
    gm_t4t568o7qnv02 ret;
};
struct gm_fr_copy_pmkg67r {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_trqfvmjtt4sij l1; /* l */
    gm_tbci5ospk5nnn ret;
};
struct gm_fr_decl_visible_bd168ti {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_describe_bm129vh {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_div_f0qrfbr {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_tbci5ospk5nnn l1; /* b */
    gm_tbci5ospk5nnn l2; /* q */
    gm_tbci5ospk5nnn ret;
};
struct gm_fr_dump_untyped_o8mt4q8 {
    gm_frame h;
    gm_tbci5ospk5nnn l1; /* i */
    gm_tngtuiuskn938 l2; /* q */
    gm_tngtuiuskn938 l4; /* re */
    gm_tngtuiuskn938 l5; /* im */
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_d914i5u {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ijtp9lo {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_eq_60fjn21 {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_tbci5ospk5nnn l1; /* b */
    uint8_t ret;
};
struct gm_fr_eq_ceufbo4 {
    gm_frame h;
    gm_tngtuiuskn938 l0; /* a */
    gm_tngtuiuskn938 l1; /* b */
    uint8_t ret;
};
struct gm_fr_error_89glhjr {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_extend_7hs67uj {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_float_result_csnia2t {
    gm_frame h;
    gm_tngtuiuskn938 l3; /* r */
    gm_t0ialelq3ql1k ret;
};
struct gm_fr_from_int_ofjuoqh {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* i */
    gm_tngtuiuskn938 ret;
};
struct gm_fr_is_integer_ab2ohk2 {
    gm_frame h;
    gm_tngtuiuskn938 l0; /* r */
    uint8_t ret;
};
struct gm_fr_is_neg_q1tmt2n {
    gm_frame h;
    gm_tbci5ospk5nnn l1; /* i */
    gm_tngtuiuskn938 l2; /* r */
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
struct gm_fr_is_zero_f3o1s37 {
    gm_frame h;
    gm_tngtuiuskn938 l1; /* r */
    uint8_t ret;
};
struct gm_fr_list_go2qg0b {
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
struct gm_fr_make_r4pbu77 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* m */
    gm_trqfvmjtt4sij l2; /* l */
    gm_tbci5ospk5nnn ret;
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
struct gm_fr_neg_0u9uqg6 {
    gm_frame h;
    gm_tngtuiuskn938 l0; /* r */
    gm_tngtuiuskn938 ret;
};
struct gm_fr_neg_agd5c5k {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_tbci5ospk5nnn l1; /* r */
    gm_tbci5ospk5nnn ret;
};
struct gm_fr_negate_fohael7 {
    gm_frame h;
    gm_tbci5ospk5nnn l1; /* i */
    gm_tngtuiuskn938 l2; /* r */
    gm_tngtuiuskn938 l3; /* re */
    gm_tngtuiuskn938 l4; /* im */
    gm_t4t568o7qnv02 ret;
};
struct gm_fr_one_08qg1op {
    gm_frame h;
    gm_tbci5ospk5nnn ret;
};
struct gm_fr_rational_bits_u60jbai {
    gm_frame h;
    gm_tngtuiuskn938 l0; /* q */
    uint64_t ret;
};
struct gm_fr_regrow_2onqfca {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_rem_ib4l1vl {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_tbci5ospk5nnn l1; /* b */
    gm_tbci5ospk5nnn l2; /* r */
    gm_tbci5ospk5nnn ret;
};
struct gm_fr_report_eardf07 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_shl_qqecl81 {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_tbci5ospk5nnn ret;
};
struct gm_fr_show_int_ub6npub {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* v */
    gm_b16_8 ret;
};
struct gm_fr_show_rational_00mlc2h {
    gm_frame h;
    gm_tngtuiuskn938 l0; /* q */
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
struct gm_fr_try_untyped_fj0h8i5 {
    gm_frame h;
    gm_tngtuiuskn938 l5; /* v */
    gm_tb07pp6njg0bk l8; /* c */
    gm_t0ialelq3ql1k l9; /* v */
    gm_t0ialelq3ql1k l11; /* v */
    gm_t0ialelq3ql1k l16; /* v */
    gm_t5vbkdq81qfs7 l18; /* r */
    gm_t0ialelq3ql1k l19; /* v */
    gm_t0ialelq3ql1k l20; /* x */
    gm_t5vbkdq81qfs7 l29; /* a */
    gm_t5vbkdq81qfs7 l30; /* b */
    gm_t0ialelq3ql1k l31; /* x */
    gm_t0ialelq3ql1k l32; /* y */
    gm_t0ialelq3ql1k l33; /* v */
    gm_tkv1vap90vht2 ret;
};
struct gm_fr_visible_ll07dr9 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_zero_9r6o7sj {
    gm_frame h;
    gm_tngtuiuskn938 ret;
};
struct gm_fr_zero_s25l990 {
    gm_frame h;
    gm_tbci5ospk5nnn ret;
};
inline bool gm_eqbqb4gb1d92(const uint8_t *a, const uint8_t *b);
inline bool gm_eqehhdtpeti1(const uint8_t *a, const uint8_t *b);
inline __attribute__((aligned(4))) void gm_f_add_420lj6o(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
inline __attribute__((aligned(4))) void gm_f_add_837b4po(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_as_complex_7e8vejf(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_as_rational_dj3nve5(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_binary_9sb7899(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_ts6oqppjh0ba3 a0, void * a1, void * a2);
__attribute__((aligned(4))) void gm_f_binary_exact_8smjl0k(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_ts6oqppjh0ba3 a0, void * a1, void * a2);
__attribute__((aligned(4))) gm_b16_8 gm_f_binop_text_0ljfc0r(gm_ts6oqppjh0ba3 a0);
inline __attribute__((aligned(4))) void gm_f_bit_and_3tgam8m(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
inline __attribute__((aligned(4))) void gm_f_bit_or_j1tdrhl(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
inline __attribute__((aligned(4))) void gm_f_bit_xor_tmvdnad(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) uint64_t gm_f_bits_lr3of4i(const void * a0);
__attribute__((aligned(4))) void gm_f_bitwise_f0f970e(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t9qkh0rsooc8d a0, const void * a1, const void * a2);
inline __attribute__((aligned(4))) int32_t gm_f_cmp_9aseneh(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) int32_t gm_f_cmp_i1knrgu(const void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_complex_binary_fggi5t8(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_ts6oqppjh0ba3 a0, void * a1, void * a2);
inline __attribute__((aligned(4))) void gm_f_copy_pmkg67r(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) uint8_t gm_f_decl_visible_bd168ti(void * a0, uint64_t a1, uint64_t a2);
__attribute__((aligned(4))) gm_b16_8 gm_f_describe_bm129vh(void * a0);
__attribute__((aligned(4))) void gm_f_div_d8sd4qc(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
inline __attribute__((aligned(4))) void gm_f_div_f0qrfbr(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_div_rem_sc09dcc(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_dump_untyped_o8mt4q8(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_d914i5u(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
inline __attribute__((aligned(4))) uint8_t gm_f_eq_60fjn21(const void * a0, const void * a1);
inline __attribute__((aligned(4))) uint8_t gm_f_eq_ceufbo4(const void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_error_89glhjr(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_error_s6hn871(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0, gm_b16_8 a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_extend_7hs67uj(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_float_result_csnia2t(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_ts6oqppjh0ba3 a0, void * a1, void * a2, const void * a3);
__attribute__((aligned(4))) void gm_f_format_5k1v0hr(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
__attribute__((aligned(4))) void gm_f_format_9453blg(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_as0b1t3(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t9qkh0rsooc8d a0);
__attribute__((aligned(4))) void gm_f_format_d801g52(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_djdpqv2(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_ejli3qf(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_mmf09ti(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t19enj90vg8bd a0);
__attribute__((aligned(4))) void gm_f_format_o65p921(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_obkf19v(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_thddtj7(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_from_int_ofjuoqh(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_from_u64_ud13q5v(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint64_t a0);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_get_espvf96(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_into_slice_hq0te56(void *gm_out, void * a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_integer_ab2ohk2(const void * a0);
__attribute__((aligned(4))) uint8_t gm_f_is_neg_q1tmt2n(void * a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_negative_k87pp3n(const void * a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_negative_rb46l9j(const void * a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_zero_5aept6u(const void * a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_zero_8tcct9o(const void * a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_zero_f3o1s37(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) void gm_f_list_go2qg0b(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_local_const_idpokjh(void *gm_out, void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_lookup_59juqoo(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_lookup_module_name_cukfp7t(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) int32_t gm_f_mag_cmp_s39veq6(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_mag_mul_if5s38k(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_mag_shl_5df643p(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_mag_to_u128_f843dal(void *gm_out, const void * a0);
inline __attribute__((aligned(4))) void gm_f_make_r4pbu77(void *gm_out, uint8_t a0, const void * a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_module_named_n1mcl3b(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
inline __attribute__((aligned(4))) void gm_f_mul_nfj5rjv(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
inline __attribute__((aligned(4))) void gm_f_mul_od1e7nd(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
inline __attribute__((aligned(4))) void gm_f_neg_0u9uqg6(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_neg_agd5c5k(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_negate_fohael7(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_new_2jv6hag(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_note_use_from_2lmujsq(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, uint64_t a2);
inline __attribute__((aligned(4))) void gm_f_one_08qg1op(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_push_o4ckdr1(void * a0, const void * a1);
inline __attribute__((aligned(4))) uint64_t gm_f_rational_bits_u60jbai(const void * a0);
__attribute__((aligned(4))) uint64_t gm_f_rational_bits_u60jbai_c(gm_tngtuiuskn938 a0);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_2onqfca(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_raw_4ag1s1i(void * a0, uint64_t a1, void * a2, uint64_t a3, uint64_t a4, uint64_t a5);
inline __attribute__((aligned(4))) void gm_f_rem_ib4l1vl(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_report_eardf07(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_resolve_decl_pus042v(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
__attribute__((aligned(4))) uint8_t gm_f_same_package_vmps24r(void * a0, uint64_t a1, uint64_t a2);
inline __attribute__((aligned(4))) void gm_f_shl_qqecl81(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_show_int_ub6npub(void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_show_rational_00mlc2h(void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_shr_4jp99a9(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_signed_add_ek0dpmo(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint8_t a0, gm_b16_8 a1, uint8_t a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) void gm_f_sub_mler6q8(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
inline __attribute__((aligned(4))) void gm_f_sub_pktm5cr(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_to_decimal_8bshn5b(void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_to_u128_haka99r(void *gm_out, const void * a0);
inline __attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_to_u64_m01t1da(const void * a0);
__attribute__((aligned(4))) void gm_f_trim_8rdppmk(void * a0);
__attribute__((aligned(4))) void gm_f_try_untyped_fj0h8i5(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
inline __attribute__((aligned(4))) uint8_t gm_f_visible_ll07dr9(void * a0, gm_t9qkh0rsooc8d a1, uint64_t a2, uint64_t a3);
__attribute__((aligned(4))) gm_b0_1 gm_f_write_decimal_ke367ab(void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1);
inline __attribute__((aligned(4))) void gm_f_zero_9r6o7sj(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_zero_s25l990(void *gm_out, void *env, gm_node *cx, gm_node *inst);
extern uint8_t gm_cii3u4ppm6k[16] __attribute__((aligned(8)));
extern const uint8_t gm_s27qdg2qrqsr9[25];
extern const uint8_t gm_s73ofu7ms28r6[23];
extern const uint8_t gm_s8qs8rpj34te9[28];
extern const uint8_t gm_s9c60h0qendav[9];
extern const uint8_t gm_sc003f3fvnij4[17];
extern const uint8_t gm_scaduriptfv6s[34];
extern const uint8_t gm_sek3nvre3su54[17];
extern const uint8_t gm_snod68sv8lqq6[45];
extern const uint8_t gm_splm7v4oko2tv[15];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_src6lm8esmv16p1[];
extern const char gm_src7h8dni243k2v[];
extern const char gm_srcc2qutubarto1[];
extern const char gm_srclv3pj5es0f8o[];
extern const char gm_srcrgc9bkquf6nr[];
extern const uint8_t gm_stpkldc5dmf71[33];
extern const uint8_t gm_sv56kn0notafb[17];
extern const gm_type gm_tyo86ks9qkvt;
inline bool gm_eqbqb4gb1d92(const uint8_t *a, const uint8_t *b) {
    uint8_t ta = GM_LD(uint8_t, a + 0), tb = GM_LD(uint8_t, b + 0);
    if (ta != tb) return false;
    if (ta == (uint8_t)0) return gm_eqehhdtpeti1(a + 1, b + 1);
    if (ta == (uint8_t)1) return gm_eqehhdtpeti1(a + 1, b + 1);
    if (ta == (uint8_t)2) return gm_eqehhdtpeti1(a + 1, b + 1);
    if (ta == (uint8_t)3) return gm_eqehhdtpeti1(a + 1, b + 1);
    return true;
}
inline bool gm_eqehhdtpeti1(const uint8_t *a, const uint8_t *b) {
    uint8_t ta = GM_LD(uint8_t, a + 0), tb = GM_LD(uint8_t, b + 0);
    if (ta != tb) return false;
    return true;
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
inline __attribute__((aligned(4))) void gm_f_add_837b4po(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1) {
    struct gm_fr_add_837b4po F;
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
    gm_f_add_420lj6o((void *)&t3, (void *)NULL, cx, NULL, &t1, &t2);
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
inline __attribute__((aligned(4))) void gm_f_bit_and_3tgam8m(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1) {
    struct gm_fr_bit_and_3tgam8m F;
    GM_STACK_CHECK(&F, 1u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memcpy(&F.l0, a0, sizeof F.l0);
    gm_t9qkh0rsooc8d t1;
    GM_ST(uint8_t, ((uint8_t *)&t1 + 0), ((uint8_t)0ULL));
    gm_f_bitwise_f0f970e((void *)&(*(gm_tbci5ospk5nnn *)gm_out), (void *)NULL, cx, NULL, t1, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l0)), &(*(gm_tbci5ospk5nnn *)((uint8_t *)a1)));
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
inline __attribute__((aligned(4))) void gm_f_bit_or_j1tdrhl(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1) {
    struct gm_fr_bit_or_j1tdrhl F;
    GM_STACK_CHECK(&F, 1u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memcpy(&F.l0, a0, sizeof F.l0);
    gm_t9qkh0rsooc8d t1;
    GM_ST(uint8_t, ((uint8_t *)&t1 + 0), ((uint8_t)1ULL));
    gm_f_bitwise_f0f970e((void *)&(*(gm_tbci5ospk5nnn *)gm_out), (void *)NULL, cx, NULL, t1, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l0)), &(*(gm_tbci5ospk5nnn *)((uint8_t *)a1)));
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
inline __attribute__((aligned(4))) void gm_f_bit_xor_tmvdnad(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1) {
    struct gm_fr_bit_xor_tmvdnad F;
    GM_STACK_CHECK(&F, 1u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_t9qkh0rsooc8d t1;
    GM_ST(uint8_t, ((uint8_t *)&t1 + 0), ((uint8_t)2ULL));
    gm_f_bitwise_f0f970e((void *)&(*(gm_tbci5ospk5nnn *)gm_out), (void *)NULL, cx, NULL, t1, &(*(gm_tbci5ospk5nnn *)((uint8_t *)a0)), &(*(gm_tbci5ospk5nnn *)((uint8_t *)a1)));
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
inline __attribute__((aligned(4))) uint8_t gm_f_decl_visible_bd168ti(void * a0, uint64_t a1, uint64_t a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_decl_visible_bd168ti F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* d */
    uint64_t gm_l2; /* from */
    void * gm_l3; /* decl */
    GM_STACK_CHECK_SHALLOW(&F, 125u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        uint64_t t2 = gm_l1;
        uint64_t t3 = (uintptr_t)t2;
        void * t4 = gm_l0;
        void * t5 = GM_LD(void *, (((uint8_t *)t4) + 0));
        uint64_t t6 = GM_LD(uintptr_t, ((((uint8_t *)t5) + 240) + 0) + 8);
        if (t3 >= (uintptr_t)(t6)) gm_trap_index(t3, t6, gm_src6lm8esmv16p1, 176, 14);
        void * t7 = (void *)((uint8_t *)GM_LD(void *, ((((uint8_t *)t5) + 240) + 0)) + t3 * 216);
        gm_l3 = t7;
    }
    {
        void * t8 = gm_l0;
        void * t9 = gm_l3;
        gm_t9qkh0rsooc8d t10 = GM_LD(gm_t9qkh0rsooc8d, ((((uint8_t *)t9) + 40) + 56));
        void * t11 = gm_l3;
        uint64_t t12 = GM_LD(uint64_t, (((uint8_t *)t11) + 32));
        uint64_t t13 = gm_l2;
        uint8_t t14 = gm_f_visible_ll07dr9(t8, t10, t12, t13);
        uint8_t t15 = 0;
        F.ret = t14;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
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
inline __attribute__((aligned(4))) uint8_t gm_f_eq_ceufbo4(const void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_eq_ceufbo4 F;
    GM_STACK_CHECK_SHALLOW(&F, 5u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    uint8_t t1 = 0;
    uint8_t t2 = gm_f_eq_60fjn21(&(*(gm_tbci5ospk5nnn *)(((uint8_t *)a0) + 0)), &(*(gm_tbci5ospk5nnn *)(((uint8_t *)a1) + 0)));
    if (t2) {
        uint8_t t3 = gm_f_eq_60fjn21(&(*(gm_tbci5ospk5nnn *)(((uint8_t *)a0) + 48)), &(*(gm_tbci5ospk5nnn *)(((uint8_t *)a1) + 48)));
        t1 = t3;
    } else t1 = 0;
    F.ret = t1;
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_extend_7hs67uj(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_extend_7hs67uj F;
    void * gm_l0; /* l */
    gm_b16_8 gm_l1; /* xs */
    uint64_t gm_l2; /* n */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        uint64_t t3 = GM_LD(uintptr_t, (((uint8_t *)t2) + 0) + 8);
        gm_l2 = t3;
    }
    {
        void * t5 = gm_l0;
        uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)t5) + 16));
        uint64_t t7 = gm_l2;
        uint64_t r8; bool o9 = __builtin_sub_overflow(t6, t7, &r8);
        if (o9) gm_trap("integer overflow in `-`", gm_srcrgc9bkquf6nr, 80, 8);
        uint64_t t10 = r8;
        uint64_t t11 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint8_t t12 = (t10 < t11);
        if (!t12) goto ie4;
        {
            void * t14 = gm_l0;
            uint64_t t15 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            gm_b16_8 t16 = gm_l1;
            gm_tftb1odc739t5 t17 = gm_f_regrow_2onqfca(t14, t15, t16);
            if (((((uint8_t *)&t17))[0] != 0)) {
                gm_t9qkh0rsooc8d t18 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&t17) + 1));
                gm_tftb1odc739t5 t19;
                ((uint8_t *)&t19)[0] = 1;
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t19 + 1), t18);
                F.ret = t19;
                goto gm_fret;
            }
            gm_b0_1 t20 = GM_LD(gm_b0_1, (((uint8_t *)&t17) + 1));
        }
        goto ix4;
ie4:;
        {
            void * t22 = gm_l0;
            void * t23 = GM_LD(void *, (((uint8_t *)t22) + 0));
            void * t24 = ((uint8_t *)t23);
            uint64_t t25 = gm_l2;
            uint64_t t26 = (uintptr_t)t25;
            uint64_t t27 = gm_l2;
            uint64_t t28 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            uint64_t r29; bool o30 = __builtin_add_overflow(t27, t28, &r29);
            if (o30) gm_trap("integer overflow in `+`", gm_srcrgc9bkquf6nr, 84, 36);
            uint64_t t31 = r29;
            uint64_t t32 = (uintptr_t)t31;
            gm_b16_8 t33;
            GM_ST(void *, (uint8_t *)&t33, (void *)(((uint8_t *)t24 + t26 * 4)));
            GM_ST(uintptr_t, (uint8_t *)&t33 + 8, (uintptr_t)(t32 - t26));
            gm_b16_8 t34 = gm_l1;
            if (GM_LD(uintptr_t, ((uint8_t *)&t33) + 8) != 0) memmove(GM_LD(void *, ((uint8_t *)&t33)), GM_LD(void *, ((uint8_t *)&t34)), GM_LD(uintptr_t, ((uint8_t *)&t33) + 8) * 4);
        }
ix4:;
    }
    {
        void * t35 = gm_l0;
        void * t36 = gm_l0;
        void * t37 = GM_LD(void *, (((uint8_t *)t36) + 0));
        void * t38 = ((uint8_t *)t37);
        uint64_t t39 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t40 = gm_l2;
        uint64_t t41 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint64_t r42; bool o43 = __builtin_add_overflow(t40, t41, &r42);
        if (o43) gm_trap("integer overflow in `+`", gm_srcrgc9bkquf6nr, 86, 30);
        uint64_t t44 = r42;
        uint64_t t45 = (uintptr_t)t44;
        gm_b16_8 t46;
        GM_ST(void *, (uint8_t *)&t46, (void *)(((uint8_t *)t38 + t39 * 4)));
        GM_ST(uintptr_t, (uint8_t *)&t46 + 8, (uintptr_t)(t45 - t39));
        GM_ST(gm_b16_8, (((uint8_t *)t35) + 0), t46);
    }
    {
        gm_b0_1 t47;
        gm_tftb1odc739t5 t48;
        GM_ST(uint8_t, (uint8_t *)&t48 + 1, 0);
        ((uint8_t *)&t48)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t48 + 1), t47);
        uint8_t t49 = ((((uint8_t *)&t48))[0] != 0);
        F.ret = t48;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_zero_f3o1s37(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_is_zero_f3o1s37 F;
    void * gm_l0; /* v */
    GM_STACK_CHECK(&F, 221u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        gm_tvegkb6ouf3m0 t3;
        gm_f_as_rational_dj3nve5((void *)&t3, (void *)NULL, cx, NULL, t2);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tngtuiuskn938 t4;
        if (((((uint8_t *)&t3))[96] != 0)) {
            t4 = (*(gm_tngtuiuskn938 *)(((uint8_t *)&t3) + 0));
        } else {
            uint8_t t5 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        F.l1 = t4;
    }
    {
        uint8_t t6 = gm_f_is_zero_5aept6u(&(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l1)));
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
inline __attribute__((aligned(4))) void gm_f_neg_0u9uqg6(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_neg_0u9uqg6 F;
    GM_STACK_CHECK(&F, 96u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memcpy(&F.l0, a0, sizeof F.l0);
    gm_tbci5ospk5nnn t1;
    gm_f_neg_agd5c5k((void *)&t1, (void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)(((uint8_t *)&F.l0) + 0)));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tbci5ospk5nnn t2 = GM_LD(gm_tbci5ospk5nnn, (((uint8_t *)&F.l0) + 48));
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
inline __attribute__((aligned(4))) uint64_t gm_f_rational_bits_u60jbai(const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_rational_bits_u60jbai F;
    GM_STACK_CHECK_SHALLOW(&F, 40u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    uint64_t t1 = gm_f_bits_lr3of4i(&(*(gm_tbci5ospk5nnn *)(((uint8_t *)a0) + 0)));
    uint64_t t2 = gm_f_bits_lr3of4i(&(*(gm_tbci5ospk5nnn *)(((uint8_t *)a0) + 48)));
    uint64_t t3 = (t1 > t2 ? t1 : t2);
    F.ret = t3;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_2onqfca(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_2onqfca F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* extra */
    gm_b16_8 gm_l2; /* tail */
    GM_STACK_CHECK(&F, 87u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        void * t3 = t2;
        uint64_t t4 = gm_l1;
        void * t5 = GM_LD(void *, ((uint8_t *)&gm_l2));
        void * t6 = t5;
        uint64_t t7 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)4ULL), ((uint64_t)4ULL));
        uint8_t t9 = ((((uint8_t *)&t8))[0] != 0);
        F.ret = t8;
        goto gm_fret;
    }
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
inline __attribute__((aligned(4))) uint8_t gm_f_visible_ll07dr9(void * a0, gm_t9qkh0rsooc8d a1, uint64_t a2, uint64_t a3) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_visible_ll07dr9 F;
    void * gm_l0; /* s */
    gm_t9qkh0rsooc8d gm_l1; /* vis */
    uint64_t gm_l2; /* owner */
    uint64_t gm_l3; /* from */
    GM_STACK_CHECK_SHALLOW(&F, 74u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    uint8_t t1;
    uint8_t t7 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
    switch (t7) { case ((uint8_t)0ULL): goto ma5; case ((uint8_t)1ULL): goto ma4; case ((uint8_t)2ULL): goto ma3; default: goto ma6; }
ma3:;
    uint8_t t9 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
    if (t9 != ((uint8_t)2ULL)) goto mn8;
    t1 = ((uint8_t)1);
    goto me2;
mn8:;
ma4:;
    uint8_t t11 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
    if (t11 != ((uint8_t)1ULL)) goto mn10;
    void * t12 = gm_l0;
    uint64_t t13 = gm_l2;
    uint64_t t14 = gm_l3;
    uint8_t t15 = gm_f_same_package_vmps24r(t12, t13, t14);
    t1 = t15;
    goto me2;
mn10:;
ma5:;
    uint8_t t17 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
    if (t17 != ((uint8_t)0ULL)) goto mn16;
    uint64_t t18 = gm_l2;
    uint64_t t19 = gm_l3;
    uint8_t t20 = ((t18 == t19));
    t1 = t20;
    goto me2;
mn16:;
ma6:;
    gm_trap("no match arm matched the scrutinee", gm_src6lm8esmv16p1, 168, 103);
me2:;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_zero_9r6o7sj(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_zero_9r6o7sj F;
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_tbci5ospk5nnn t1;
    gm_f_zero_s25l990((void *)&t1, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_f_from_int_ofjuoqh((void *)&(*(gm_tngtuiuskn938 *)gm_out), (void *)NULL, cx, NULL, &t1);
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
inline __attribute__((aligned(4))) void gm_f_as_complex_7e8vejf(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_as_complex_7e8vejf F;
    void * gm_l0; /* v */
    GM_STACK_CHECK(&F, 1002u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t3 = gm_l0;
        uint8_t t4 = GM_LD(uint8_t, (((uint8_t *)t3) + 0));
        if (t4 != ((uint8_t)4ULL)) goto ie2;
        gm_tngtuiuskn938 t5 = GM_LD(gm_tngtuiuskn938, ((((uint8_t *)t3) + 8) + 0));
        F.l1 = t5;
        gm_tngtuiuskn938 t6 = GM_LD(gm_tngtuiuskn938, ((((uint8_t *)t3) + 8) + 96));
        F.l2 = t6;
        {
            gm_tu15p0hi2qlv6 t8;
            memcpy((uint8_t *)&t8 + 0, ((uint8_t *)&F.l1), 96);
            memcpy((uint8_t *)&t8 + 96, ((uint8_t *)&F.l2), 96);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tk3f7nn6jq07c *)gm_out) + 193, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tk3f7nn6jq07c *)gm_out) + 194, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tk3f7nn6jq07c *)gm_out) + 196, 0);
            ((uint8_t *)&(*(gm_tk3f7nn6jq07c *)gm_out))[192] = 1;
            GM_ST(gm_tu15p0hi2qlv6, (uint8_t *)&(*(gm_tk3f7nn6jq07c *)gm_out) + 0, t8);
            goto gm_fdone;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t9 = gm_l0;
        gm_tvegkb6ouf3m0 t10;
        gm_f_as_rational_dj3nve5((void *)&t10, (void *)NULL, cx, NULL, t9);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tngtuiuskn938 t11;
        if (((((uint8_t *)&t10))[96] != 0)) {
            t11 = (*(gm_tngtuiuskn938 *)(((uint8_t *)&t10) + 0));
        } else {
            gm_tk3f7nn6jq07c t12;
            memset(&t12, 0, sizeof t12);
            uint8_t t13 = 0;
            F.ret = t12;
            goto gm_fret;
        }
        F.l3 = t11;
    }
    {
        gm_tu15p0hi2qlv6 t14;
        memcpy((uint8_t *)&t14 + 0, ((uint8_t *)&F.l3), 96);
        gm_f_zero_9r6o7sj((void *)&(*(gm_tngtuiuskn938 *)((uint8_t *)&t14 + 96)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tk3f7nn6jq07c *)gm_out) + 193, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tk3f7nn6jq07c *)gm_out) + 194, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tk3f7nn6jq07c *)gm_out) + 196, 0);
        ((uint8_t *)&(*(gm_tk3f7nn6jq07c *)gm_out))[192] = 1;
        GM_ST(gm_tu15p0hi2qlv6, (uint8_t *)&(*(gm_tk3f7nn6jq07c *)gm_out) + 0, t14);
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
inline __attribute__((aligned(4))) void gm_f_as_rational_dj3nve5(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_as_rational_dj3nve5 F;
    void * gm_l0; /* v */
    GM_STACK_CHECK(&F, 972u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    gm_tvegkb6ouf3m0 t2;
    uint8_t t9 = GM_LD(uint8_t, (((uint8_t *)t1) + 0));
    switch (t9) { case ((uint8_t)0ULL): goto ma4; case ((uint8_t)1ULL): goto ma5; case ((uint8_t)2ULL): goto ma6; case ((uint8_t)3ULL): goto ma7; case ((uint8_t)4ULL): goto ma7; default: goto ma7; }
ma4:;
    uint8_t t11 = GM_LD(uint8_t, (((uint8_t *)t1) + 0));
    if (t11 != ((uint8_t)0ULL)) goto mn10;
    gm_tbci5ospk5nnn t12 = GM_LD(gm_tbci5ospk5nnn, (((uint8_t *)t1) + 8));
    F.l1 = t12;
    gm_tngtuiuskn938 t13;
    gm_f_from_int_ofjuoqh((void *)&t13, (void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l1)));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tvegkb6ouf3m0 t14;
    GM_ST(uint8_t, (uint8_t *)&t14 + 97, 0);
    GM_ST(uint16_t, (uint8_t *)&t14 + 98, 0);
    GM_ST(uint32_t, (uint8_t *)&t14 + 100, 0);
    GM_ST(gm_tngtuiuskn938, ((uint8_t *)&t14), t13);
    ((uint8_t *)&t14)[96] = 1;
    t2 = t14;
    goto me3;
mn10:;
ma5:;
    uint8_t t16 = GM_LD(uint8_t, (((uint8_t *)t1) + 0));
    if (t16 != ((uint8_t)1ULL)) goto mn15;
    gm_tngtuiuskn938 t17 = GM_LD(gm_tngtuiuskn938, (((uint8_t *)t1) + 8));
    F.l2 = t17;
    gm_tngtuiuskn938 t18 = F.l2;
    gm_tvegkb6ouf3m0 t19;
    GM_ST(uint8_t, (uint8_t *)&t19 + 97, 0);
    GM_ST(uint16_t, (uint8_t *)&t19 + 98, 0);
    GM_ST(uint32_t, (uint8_t *)&t19 + 100, 0);
    GM_ST(gm_tngtuiuskn938, ((uint8_t *)&t19), t18);
    ((uint8_t *)&t19)[96] = 1;
    t2 = t19;
    goto me3;
mn15:;
ma6:;
    uint8_t t21 = GM_LD(uint8_t, (((uint8_t *)t1) + 0));
    if (t21 != ((uint8_t)2ULL)) goto mn20;
    gm_tngtuiuskn938 t22;
    gm_f_zero_9r6o7sj((void *)&t22, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tvegkb6ouf3m0 t23;
    GM_ST(uint8_t, (uint8_t *)&t23 + 97, 0);
    GM_ST(uint16_t, (uint8_t *)&t23 + 98, 0);
    GM_ST(uint32_t, (uint8_t *)&t23 + 100, 0);
    GM_ST(gm_tngtuiuskn938, ((uint8_t *)&t23), t22);
    ((uint8_t *)&t23)[96] = 1;
    t2 = t23;
    goto me3;
mn20:;
ma7:;
    gm_tvegkb6ouf3m0 t25;
    memset(&t25, 0, sizeof t25);
    t2 = t25;
    goto me3;
mn24:;
ma8:;
    gm_trap("no match arm matched the scrutinee", gm_src7h8dni243k2v, 37, 61);
me3:;
    F.ret = t2;
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
inline __attribute__((aligned(4))) void gm_f_binary_9sb7899(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_ts6oqppjh0ba3 a0, void * a1, void * a2) {
    struct gm_fr_binary_9sb7899 F;
    gm_ts6oqppjh0ba3 gm_l0; /* op */
    void * gm_l1; /* a */
    void * gm_l2; /* b */
    uint64_t gm_l9; /* bits */
    GM_STACK_CHECK(&F, 702u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        gm_ts6oqppjh0ba3 t2 = gm_l0;
        void * t3 = gm_l1;
        void * t4 = gm_l2;
        gm_f_binary_exact_8smjl0k((void *)&F.l3, (void *)NULL, cx, NULL, t2, t3, t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        uint8_t t6 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 0));
        if (t6 != ((uint8_t)0ULL)) goto ie5;
        gm_t0ialelq3ql1k t7 = GM_LD(gm_t0ialelq3ql1k, (((uint8_t *)&F.l3) + 8));
        F.l4 = t7;
        {
            uint64_t t9;
            uint8_t t16 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 0));
            switch (t16) { case ((uint8_t)0ULL): goto ma11; case ((uint8_t)1ULL): goto ma12; case ((uint8_t)2ULL): goto ma14; case ((uint8_t)3ULL): goto ma14; case ((uint8_t)4ULL): goto ma13; default: goto ma14; }
ma11:;
            uint8_t t18 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 0));
            if (t18 != ((uint8_t)0ULL)) goto mn17;
            gm_tbci5ospk5nnn t19 = GM_LD(gm_tbci5ospk5nnn, (((uint8_t *)&F.l4) + 8));
            F.l5 = t19;
            uint64_t t20 = gm_f_bits_lr3of4i(&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l5)));
            t9 = t20;
            goto me10;
mn17:;
ma12:;
            uint8_t t22 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 0));
            if (t22 != ((uint8_t)1ULL)) goto mn21;
            gm_tngtuiuskn938 t23 = GM_LD(gm_tngtuiuskn938, (((uint8_t *)&F.l4) + 8));
            F.l6 = t23;
            uint64_t t24 = gm_f_rational_bits_u60jbai(&(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l6)));
            t9 = t24;
            goto me10;
mn21:;
ma13:;
            uint8_t t26 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 0));
            if (t26 != ((uint8_t)4ULL)) goto mn25;
            gm_tngtuiuskn938 t27 = GM_LD(gm_tngtuiuskn938, ((((uint8_t *)&F.l4) + 8) + 0));
            F.l7 = t27;
            gm_tngtuiuskn938 t28 = GM_LD(gm_tngtuiuskn938, ((((uint8_t *)&F.l4) + 8) + 96));
            F.l8 = t28;
            uint64_t t29 = gm_f_rational_bits_u60jbai(&(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l7)));
            uint64_t t30 = gm_f_rational_bits_u60jbai(&(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l8)));
            uint64_t t31 = (t29 > t30 ? t29 : t30);
            t9 = t31;
            goto me10;
mn25:;
ma14:;
            t9 = ((uint64_t)0ULL);
            goto me10;
mn32:;
ma15:;
            gm_trap("no match arm matched the scrutinee", gm_src7h8dni243k2v, 75, 17);
me10:;
            gm_l9 = t9;
        }
        {
            uint64_t t34 = gm_l9;
            uint8_t t35 = (t34 > ((uint64_t)4194304ULL));
            if (!t35) goto ie33;
            {
                gm_t19enj90vg8bd t37;
                GM_ST(int32_t, ((uint8_t *)&t37 + 0), ((int32_t)4194304LL));
                gm_tabb710d9mq2e t38;
                gm_f_format_mmf09ti((void *)&t38, (void *)NULL, cx, NULL, t37);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t38))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t38) + 8, gm_src7h8dni243k2v, 82, 21);
                gm_b16_8 t39 = GM_LD(gm_b16_8, (((uint8_t *)&t38) + 8));
                gm_b16_8 t40 = GM_LD(gm_b16_8, ((uint8_t *)&t39));
                memset(&(*(gm_t4t568o7qnv02 *)gm_out), 0, sizeof (*(gm_t4t568o7qnv02 *)gm_out));
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t4t568o7qnv02 *)gm_out) + 0, ((uint8_t)1ULL));
                GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t4t568o7qnv02 *)gm_out) + 8, t40);
                goto gm_fdone;
            }
            goto ix33;
ie33:;
ix33:;
        }
        {
            memmove(gm_out, ((uint8_t *)&F.l3), 208);
            goto gm_fdone;
        }
        goto ix5;
ie5:;
        {
            memmove(gm_out, ((uint8_t *)&F.l3), 208);
            goto gm_fdone;
        }
ix5:;
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
__attribute__((aligned(4))) void gm_f_binary_exact_8smjl0k(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_ts6oqppjh0ba3 a0, void * a1, void * a2) {
    struct gm_fr_binary_exact_8smjl0k F;
    gm_ts6oqppjh0ba3 gm_l0; /* op */
    void * gm_l1; /* a */
    void * gm_l2; /* b */
    uint8_t gm_l3; /* x */
    uint8_t gm_l4; /* y */
    int32_t gm_l7; /* c */
    uint64_t gm_l15; /* n */
    GM_STACK_CHECK(&F, 5238u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        uint8_t t3 = 0;
        void * t4 = gm_l1;
        uint8_t t5;
        uint8_t t8 = GM_LD(uint8_t, (((uint8_t *)t4) + 0));
        if (t8 != ((uint8_t)4ULL)) goto mn7;
        t5 = ((uint8_t)1);
        goto me6;
mn7:;
        t5 = ((uint8_t)0);
        goto me6;
mn9:;
        gm_trap("no match arm matched the scrutinee", gm_src7h8dni243k2v, 92, 8);
me6:;
        if (!t5) {
            void * t10 = gm_l2;
            uint8_t t11;
            uint8_t t14 = GM_LD(uint8_t, (((uint8_t *)t10) + 0));
            if (t14 != ((uint8_t)4ULL)) goto mn13;
            t11 = ((uint8_t)1);
            goto me12;
mn13:;
            t11 = ((uint8_t)0);
            goto me12;
mn15:;
            gm_trap("no match arm matched the scrutinee", gm_src7h8dni243k2v, 92, 29);
me12:;
            t3 = t11;
        } else t3 = 1;
        if (!t3) goto ie2;
        {
            gm_ts6oqppjh0ba3 t17 = gm_l0;
            void * t18 = gm_l1;
            void * t19 = gm_l2;
            gm_f_complex_binary_fggi5t8((void *)&(*(gm_t4t568o7qnv02 *)gm_out), (void *)NULL, cx, NULL, t17, t18, t19);
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
        uint8_t t26 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
        switch (t26) { case ((uint8_t)0ULL): goto ma22; case ((uint8_t)1ULL): goto ma22; case ((uint8_t)2ULL): goto ma22; case ((uint8_t)3ULL): goto ma23; case ((uint8_t)4ULL): goto ma22; case ((uint8_t)5ULL): goto ma23; case ((uint8_t)6ULL): goto ma23; case ((uint8_t)7ULL): goto ma23; case ((uint8_t)8ULL): goto ma23; case ((uint8_t)9ULL): goto ma23; case ((uint8_t)10ULL): goto ma24; case ((uint8_t)11ULL): goto ma24; case ((uint8_t)12ULL): goto ma21; case ((uint8_t)13ULL): goto ma21; case ((uint8_t)14ULL): goto ma24; case ((uint8_t)15ULL): goto ma24; case ((uint8_t)16ULL): goto ma21; case ((uint8_t)17ULL): goto ma21; case ((uint8_t)18ULL): goto ma21; case ((uint8_t)19ULL): goto ma21; case ((uint8_t)20ULL): goto ma24; case ((uint8_t)21ULL): goto ma24; default: goto ma24; }
ma21:;
        uint8_t t30 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
        if (t30 != ((uint8_t)16ULL)) goto pa29;
        goto po28;
pa29:;
        uint8_t t32 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
        if (t32 != ((uint8_t)17ULL)) goto pa31;
        goto po28;
pa31:;
        uint8_t t34 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
        if (t34 != ((uint8_t)18ULL)) goto pa33;
        goto po28;
pa33:;
        uint8_t t36 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
        if (t36 != ((uint8_t)19ULL)) goto pa35;
        goto po28;
pa35:;
        uint8_t t38 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
        if (t38 != ((uint8_t)12ULL)) goto pa37;
        goto po28;
pa37:;
        uint8_t t40 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
        if (t40 != ((uint8_t)13ULL)) goto pa39;
        goto po28;
pa39:;
        goto mn27;
po28:;
        {
            void * t43 = gm_l1;
            uint8_t t44 = GM_LD(uint8_t, (((uint8_t *)t43) + 0));
            if (t44 != ((uint8_t)3ULL)) goto ie42;
            uint8_t t45 = GM_LD(uint8_t, (((uint8_t *)t43) + 8));
            gm_l3 = t45;
            void * t46 = gm_l2;
            uint8_t t47 = GM_LD(uint8_t, (((uint8_t *)t46) + 0));
            if (t47 != ((uint8_t)3ULL)) goto ie42;
            uint8_t t48 = GM_LD(uint8_t, (((uint8_t *)t46) + 8));
            gm_l4 = t48;
            {
                gm_t4t568o7qnv02 t50;
                uint8_t t56 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                switch (t56) { case ((uint8_t)0ULL): goto ma54; case ((uint8_t)1ULL): goto ma54; case ((uint8_t)2ULL): goto ma54; case ((uint8_t)3ULL): goto ma54; case ((uint8_t)4ULL): goto ma54; case ((uint8_t)5ULL): goto ma54; case ((uint8_t)6ULL): goto ma54; case ((uint8_t)7ULL): goto ma54; case ((uint8_t)8ULL): goto ma54; case ((uint8_t)9ULL): goto ma54; case ((uint8_t)10ULL): goto ma54; case ((uint8_t)11ULL): goto ma54; case ((uint8_t)12ULL): goto ma52; case ((uint8_t)13ULL): goto ma53; case ((uint8_t)14ULL): goto ma54; case ((uint8_t)15ULL): goto ma54; case ((uint8_t)16ULL): goto ma54; case ((uint8_t)17ULL): goto ma54; case ((uint8_t)18ULL): goto ma54; case ((uint8_t)19ULL): goto ma54; case ((uint8_t)20ULL): goto ma54; case ((uint8_t)21ULL): goto ma54; default: goto ma54; }
ma52:;
                uint8_t t58 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                if (t58 != ((uint8_t)12ULL)) goto mn57;
                uint8_t t59 = gm_l3;
                uint8_t t60 = gm_l4;
                uint8_t t61 = ((t59 == t60));
                gm_t0ialelq3ql1k t62;
                memset(&t62, 0, sizeof t62);
                GM_ST(uint8_t, ((uint8_t *)&t62 + 0), ((uint8_t)3ULL));
                GM_ST(uint8_t, ((uint8_t *)&t62 + 8), t61);
                gm_t4t568o7qnv02 t63;
                GM_ST(uint8_t, (uint8_t *)&t63 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t63 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t63 + 4, 0);
                GM_ST(uint8_t, ((uint8_t *)&t63 + 0), ((uint8_t)0ULL));
                GM_ST(gm_t0ialelq3ql1k, ((uint8_t *)&t63 + 8), t62);
                t50 = t63;
                goto me51;
mn57:;
ma53:;
                uint8_t t65 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                if (t65 != ((uint8_t)13ULL)) goto mn64;
                uint8_t t66 = gm_l3;
                uint8_t t67 = gm_l4;
                uint8_t t68 = (!(t66 == t67));
                gm_t0ialelq3ql1k t69;
                memset(&t69, 0, sizeof t69);
                GM_ST(uint8_t, ((uint8_t *)&t69 + 0), ((uint8_t)3ULL));
                GM_ST(uint8_t, ((uint8_t *)&t69 + 8), t68);
                gm_t4t568o7qnv02 t70;
                GM_ST(uint8_t, (uint8_t *)&t70 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t70 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t70 + 4, 0);
                GM_ST(uint8_t, ((uint8_t *)&t70 + 0), ((uint8_t)0ULL));
                GM_ST(gm_t0ialelq3ql1k, ((uint8_t *)&t70 + 8), t69);
                t50 = t70;
                goto me51;
mn64:;
ma54:;
                gm_b16_8 t72;
                GM_ST(void *, (uint8_t *)&t72, (void *)(gm_s27qdg2qrqsr9));
                GM_ST(uintptr_t, (uint8_t *)&t72 + 8, (uintptr_t)(24));
                gm_t4t568o7qnv02 t73;
                memset(&t73, 0, sizeof t73);
                GM_ST(uint8_t, ((uint8_t *)&t73 + 0), ((uint8_t)1ULL));
                GM_ST(gm_b16_8, ((uint8_t *)&t73 + 8), t72);
                t50 = t73;
                goto me51;
mn71:;
ma55:;
                gm_trap("no match arm matched the scrutinee", gm_src7h8dni243k2v, 96, 24);
me51:;
                uint8_t t74 = 0;
                F.ret = t50;
                goto gm_fret;
            }
            goto ix42;
ie42:;
ix42:;
        }
        {
            void * t75 = gm_l1;
            gm_tvegkb6ouf3m0 t76;
            gm_f_as_rational_dj3nve5((void *)&t76, (void *)NULL, cx, NULL, t75);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tngtuiuskn938 t77;
            if (((((uint8_t *)&t76))[96] != 0)) {
                t77 = (*(gm_tngtuiuskn938 *)(((uint8_t *)&t76) + 0));
            } else {
                gm_b16_8 t78;
                GM_ST(void *, (uint8_t *)&t78, (void *)(gm_s8qs8rpj34te9));
                GM_ST(uintptr_t, (uint8_t *)&t78 + 8, (uintptr_t)(27));
                memset(&(*(gm_t4t568o7qnv02 *)gm_out), 0, sizeof (*(gm_t4t568o7qnv02 *)gm_out));
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t4t568o7qnv02 *)gm_out) + 0, ((uint8_t)1ULL));
                GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t4t568o7qnv02 *)gm_out) + 8, t78);
                goto gm_fdone;
            }
            F.l5 = t77;
        }
        {
            void * t79 = gm_l2;
            gm_tvegkb6ouf3m0 t80;
            gm_f_as_rational_dj3nve5((void *)&t80, (void *)NULL, cx, NULL, t79);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tngtuiuskn938 t81;
            if (((((uint8_t *)&t80))[96] != 0)) {
                t81 = (*(gm_tngtuiuskn938 *)(((uint8_t *)&t80) + 0));
            } else {
                gm_b16_8 t82;
                GM_ST(void *, (uint8_t *)&t82, (void *)(gm_s8qs8rpj34te9));
                GM_ST(uintptr_t, (uint8_t *)&t82 + 8, (uintptr_t)(27));
                memset(&(*(gm_t4t568o7qnv02 *)gm_out), 0, sizeof (*(gm_t4t568o7qnv02 *)gm_out));
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t4t568o7qnv02 *)gm_out) + 0, ((uint8_t)1ULL));
                GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t4t568o7qnv02 *)gm_out) + 8, t82);
                goto gm_fdone;
            }
            F.l6 = t81;
        }
        {
            int32_t t83 = gm_f_cmp_9aseneh((void *)NULL, cx, NULL, &(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l5)), &(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l6)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l7 = t83;
        }
        {
            uint8_t t84;
            uint8_t t93 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            switch (t93) { case ((uint8_t)0ULL): goto ma91; case ((uint8_t)1ULL): goto ma91; case ((uint8_t)2ULL): goto ma91; case ((uint8_t)3ULL): goto ma91; case ((uint8_t)4ULL): goto ma91; case ((uint8_t)5ULL): goto ma91; case ((uint8_t)6ULL): goto ma91; case ((uint8_t)7ULL): goto ma91; case ((uint8_t)8ULL): goto ma91; case ((uint8_t)9ULL): goto ma91; case ((uint8_t)10ULL): goto ma91; case ((uint8_t)11ULL): goto ma91; case ((uint8_t)12ULL): goto ma90; case ((uint8_t)13ULL): goto ma91; case ((uint8_t)14ULL): goto ma91; case ((uint8_t)15ULL): goto ma91; case ((uint8_t)16ULL): goto ma86; case ((uint8_t)17ULL): goto ma87; case ((uint8_t)18ULL): goto ma88; case ((uint8_t)19ULL): goto ma89; case ((uint8_t)20ULL): goto ma91; case ((uint8_t)21ULL): goto ma91; default: goto ma91; }
ma86:;
            uint8_t t95 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            if (t95 != ((uint8_t)16ULL)) goto mn94;
            int32_t t96 = gm_l7;
            uint8_t t97 = (t96 < ((int32_t)0LL));
            t84 = t97;
            goto me85;
mn94:;
ma87:;
            uint8_t t99 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            if (t99 != ((uint8_t)17ULL)) goto mn98;
            int32_t t100 = gm_l7;
            uint8_t t101 = (t100 <= ((int32_t)0LL));
            t84 = t101;
            goto me85;
mn98:;
ma88:;
            uint8_t t103 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            if (t103 != ((uint8_t)18ULL)) goto mn102;
            int32_t t104 = gm_l7;
            uint8_t t105 = (t104 > ((int32_t)0LL));
            t84 = t105;
            goto me85;
mn102:;
ma89:;
            uint8_t t107 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            if (t107 != ((uint8_t)19ULL)) goto mn106;
            int32_t t108 = gm_l7;
            uint8_t t109 = (t108 >= ((int32_t)0LL));
            t84 = t109;
            goto me85;
mn106:;
ma90:;
            uint8_t t111 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            if (t111 != ((uint8_t)12ULL)) goto mn110;
            int32_t t112 = gm_l7;
            uint8_t t113 = ((t112 == ((int32_t)0LL)));
            t84 = t113;
            goto me85;
mn110:;
ma91:;
            int32_t t115 = gm_l7;
            uint8_t t116 = (!(t115 == ((int32_t)0LL)));
            t84 = t116;
            goto me85;
mn114:;
ma92:;
            gm_trap("no match arm matched the scrutinee", gm_src7h8dni243k2v, 105, 30);
me85:;
            gm_t0ialelq3ql1k t117;
            memset(&t117, 0, sizeof t117);
            GM_ST(uint8_t, ((uint8_t *)&t117 + 0), ((uint8_t)3ULL));
            GM_ST(uint8_t, ((uint8_t *)&t117 + 8), t84);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t4t568o7qnv02 *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t4t568o7qnv02 *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t4t568o7qnv02 *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t4t568o7qnv02 *)gm_out) + 0, ((uint8_t)0ULL));
            GM_ST(gm_t0ialelq3ql1k, (uint8_t *)&(*(gm_t4t568o7qnv02 *)gm_out) + 8, t117);
            goto gm_fdone;
        }
        goto me20;
mn27:;
ma22:;
        uint8_t t121 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
        if (t121 != ((uint8_t)0ULL)) goto pa120;
        goto po119;
pa120:;
        uint8_t t123 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
        if (t123 != ((uint8_t)1ULL)) goto pa122;
        goto po119;
pa122:;
        uint8_t t125 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
        if (t125 != ((uint8_t)2ULL)) goto pa124;
        goto po119;
pa124:;
        uint8_t t127 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
        if (t127 != ((uint8_t)4ULL)) goto pa126;
        goto po119;
pa126:;
        goto mn118;
po119:;
        {
            void * t130 = gm_l1;
            uint8_t t131 = GM_LD(uint8_t, (((uint8_t *)t130) + 0));
            if (t131 != ((uint8_t)0ULL)) goto ie129;
            gm_tbci5ospk5nnn t132 = GM_LD(gm_tbci5ospk5nnn, (((uint8_t *)t130) + 8));
            F.l8 = t132;
            void * t133 = gm_l2;
            uint8_t t134 = GM_LD(uint8_t, (((uint8_t *)t133) + 0));
            if (t134 != ((uint8_t)0ULL)) goto ie129;
            gm_tbci5ospk5nnn t135 = GM_LD(gm_tbci5ospk5nnn, (((uint8_t *)t133) + 8));
            F.l9 = t135;
            {
                gm_tbci5ospk5nnn t137;
                uint8_t t144 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                switch (t144) { case ((uint8_t)0ULL): goto ma139; case ((uint8_t)1ULL): goto ma140; case ((uint8_t)2ULL): goto ma141; case ((uint8_t)3ULL): goto ma142; case ((uint8_t)4ULL): goto ma142; case ((uint8_t)5ULL): goto ma142; case ((uint8_t)6ULL): goto ma142; case ((uint8_t)7ULL): goto ma142; case ((uint8_t)8ULL): goto ma142; case ((uint8_t)9ULL): goto ma142; case ((uint8_t)10ULL): goto ma142; case ((uint8_t)11ULL): goto ma142; case ((uint8_t)12ULL): goto ma142; case ((uint8_t)13ULL): goto ma142; case ((uint8_t)14ULL): goto ma142; case ((uint8_t)15ULL): goto ma142; case ((uint8_t)16ULL): goto ma142; case ((uint8_t)17ULL): goto ma142; case ((uint8_t)18ULL): goto ma142; case ((uint8_t)19ULL): goto ma142; case ((uint8_t)20ULL): goto ma142; case ((uint8_t)21ULL): goto ma142; default: goto ma142; }
ma139:;
                uint8_t t146 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                if (t146 != ((uint8_t)0ULL)) goto mn145;
                gm_tbci5ospk5nnn t147;
                gm_f_add_420lj6o((void *)&t147, (void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l8)), &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l9)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                t137 = t147;
                goto me138;
mn145:;
ma140:;
                uint8_t t149 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                if (t149 != ((uint8_t)1ULL)) goto mn148;
                gm_tbci5ospk5nnn t150;
                gm_f_sub_pktm5cr((void *)&t150, (void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l8)), &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l9)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                t137 = t150;
                goto me138;
mn148:;
ma141:;
                uint8_t t152 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                if (t152 != ((uint8_t)2ULL)) goto mn151;
                gm_tbci5ospk5nnn t153;
                gm_f_mul_nfj5rjv((void *)&t153, (void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l8)), &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l9)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                t137 = t153;
                goto me138;
mn151:;
ma142:;
                {
                    uint8_t t157 = gm_f_is_zero_8tcct9o(&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l9)));
                    if (!t157) goto ie156;
                    {
                        gm_b16_8 t159 = GM_LD(gm_b16_8, gm_cii3u4ppm6k);
                        memset(&(*(gm_t4t568o7qnv02 *)gm_out), 0, sizeof (*(gm_t4t568o7qnv02 *)gm_out));
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_t4t568o7qnv02 *)gm_out) + 0, ((uint8_t)1ULL));
                        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t4t568o7qnv02 *)gm_out) + 8, t159);
                        goto gm_fdone;
                    }
                    goto ix156;
ie156:;
ix156:;
                }
                gm_tbci5ospk5nnn t160;
                gm_f_div_f0qrfbr((void *)&t160, (void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l8)), &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l9)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tbci5ospk5nnn t161 = t160;
                t137 = t161;
                goto me138;
mn154:;
ma143:;
                gm_trap("no match arm matched the scrutinee", gm_src7h8dni243k2v, 116, 33);
me138:;
                gm_t0ialelq3ql1k t162;
                memset(&t162, 0, sizeof t162);
                GM_ST(uint8_t, ((uint8_t *)&t162 + 0), ((uint8_t)0ULL));
                GM_ST(gm_tbci5ospk5nnn, ((uint8_t *)&t162 + 8), t137);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t4t568o7qnv02 *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_t4t568o7qnv02 *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_t4t568o7qnv02 *)gm_out) + 4, 0);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t4t568o7qnv02 *)gm_out) + 0, ((uint8_t)0ULL));
                GM_ST(gm_t0ialelq3ql1k, (uint8_t *)&(*(gm_t4t568o7qnv02 *)gm_out) + 8, t162);
                goto gm_fdone;
            }
            goto ix129;
ie129:;
ix129:;
        }
        {
            void * t163 = gm_l1;
            gm_tvegkb6ouf3m0 t164;
            gm_f_as_rational_dj3nve5((void *)&t164, (void *)NULL, cx, NULL, t163);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tngtuiuskn938 t165;
            if (((((uint8_t *)&t164))[96] != 0)) {
                t165 = (*(gm_tngtuiuskn938 *)(((uint8_t *)&t164) + 0));
            } else {
                gm_b16_8 t166;
                GM_ST(void *, (uint8_t *)&t166, (void *)(gm_s8qs8rpj34te9));
                GM_ST(uintptr_t, (uint8_t *)&t166 + 8, (uintptr_t)(27));
                memset(&(*(gm_t4t568o7qnv02 *)gm_out), 0, sizeof (*(gm_t4t568o7qnv02 *)gm_out));
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t4t568o7qnv02 *)gm_out) + 0, ((uint8_t)1ULL));
                GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t4t568o7qnv02 *)gm_out) + 8, t166);
                goto gm_fdone;
            }
            F.l10 = t165;
        }
        {
            void * t167 = gm_l2;
            gm_tvegkb6ouf3m0 t168;
            gm_f_as_rational_dj3nve5((void *)&t168, (void *)NULL, cx, NULL, t167);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tngtuiuskn938 t169;
            if (((((uint8_t *)&t168))[96] != 0)) {
                t169 = (*(gm_tngtuiuskn938 *)(((uint8_t *)&t168) + 0));
            } else {
                gm_b16_8 t170;
                GM_ST(void *, (uint8_t *)&t170, (void *)(gm_s8qs8rpj34te9));
                GM_ST(uintptr_t, (uint8_t *)&t170 + 8, (uintptr_t)(27));
                memset(&(*(gm_t4t568o7qnv02 *)gm_out), 0, sizeof (*(gm_t4t568o7qnv02 *)gm_out));
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t4t568o7qnv02 *)gm_out) + 0, ((uint8_t)1ULL));
                GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t4t568o7qnv02 *)gm_out) + 8, t170);
                goto gm_fdone;
            }
            F.l11 = t169;
        }
        {
            gm_tngtuiuskn938 t171;
            uint8_t t178 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            switch (t178) { case ((uint8_t)0ULL): goto ma173; case ((uint8_t)1ULL): goto ma174; case ((uint8_t)2ULL): goto ma175; case ((uint8_t)3ULL): goto ma176; case ((uint8_t)4ULL): goto ma176; case ((uint8_t)5ULL): goto ma176; case ((uint8_t)6ULL): goto ma176; case ((uint8_t)7ULL): goto ma176; case ((uint8_t)8ULL): goto ma176; case ((uint8_t)9ULL): goto ma176; case ((uint8_t)10ULL): goto ma176; case ((uint8_t)11ULL): goto ma176; case ((uint8_t)12ULL): goto ma176; case ((uint8_t)13ULL): goto ma176; case ((uint8_t)14ULL): goto ma176; case ((uint8_t)15ULL): goto ma176; case ((uint8_t)16ULL): goto ma176; case ((uint8_t)17ULL): goto ma176; case ((uint8_t)18ULL): goto ma176; case ((uint8_t)19ULL): goto ma176; case ((uint8_t)20ULL): goto ma176; case ((uint8_t)21ULL): goto ma176; default: goto ma176; }
ma173:;
            uint8_t t180 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            if (t180 != ((uint8_t)0ULL)) goto mn179;
            gm_tngtuiuskn938 t181;
            gm_f_add_837b4po((void *)&t181, (void *)NULL, cx, NULL, &(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l10)), &(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l11)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            t171 = t181;
            goto me172;
mn179:;
ma174:;
            uint8_t t183 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            if (t183 != ((uint8_t)1ULL)) goto mn182;
            gm_tngtuiuskn938 t184;
            gm_f_sub_mler6q8((void *)&t184, (void *)NULL, cx, NULL, &(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l10)), &(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l11)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            t171 = t184;
            goto me172;
mn182:;
ma175:;
            uint8_t t186 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            if (t186 != ((uint8_t)2ULL)) goto mn185;
            gm_tngtuiuskn938 t187;
            gm_f_mul_od1e7nd((void *)&t187, (void *)NULL, cx, NULL, &(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l10)), &(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l11)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            t171 = t187;
            goto me172;
mn185:;
ma176:;
            {
                uint8_t t191 = gm_f_is_zero_5aept6u(&(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l11)));
                if (!t191) goto ie190;
                {
                    gm_b16_8 t193 = GM_LD(gm_b16_8, gm_cii3u4ppm6k);
                    memset(&(*(gm_t4t568o7qnv02 *)gm_out), 0, sizeof (*(gm_t4t568o7qnv02 *)gm_out));
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_t4t568o7qnv02 *)gm_out) + 0, ((uint8_t)1ULL));
                    GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t4t568o7qnv02 *)gm_out) + 8, t193);
                    goto gm_fdone;
                }
                goto ix190;
ie190:;
ix190:;
            }
            gm_tngtuiuskn938 t194;
            gm_f_div_d8sd4qc((void *)&t194, (void *)NULL, cx, NULL, &(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l10)), &(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l11)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tngtuiuskn938 t195 = t194;
            t171 = t195;
            goto me172;
mn188:;
ma177:;
            gm_trap("no match arm matched the scrutinee", gm_src7h8dni243k2v, 128, 18);
me172:;
            F.l12 = t171;
        }
        {
            gm_ts6oqppjh0ba3 t196 = gm_l0;
            void * t197 = gm_l1;
            void * t198 = gm_l2;
            gm_t0ialelq3ql1k t199;
            gm_f_float_result_csnia2t((void *)&t199, (void *)NULL, cx, NULL, t196, t197, t198, &(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l12)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t4t568o7qnv02 *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t4t568o7qnv02 *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t4t568o7qnv02 *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t4t568o7qnv02 *)gm_out) + 0, ((uint8_t)0ULL));
            GM_ST(gm_t0ialelq3ql1k, (uint8_t *)&(*(gm_t4t568o7qnv02 *)gm_out) + 8, t199);
            goto gm_fdone;
        }
        goto me20;
mn118:;
ma23:;
        uint8_t t203 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
        if (t203 != ((uint8_t)5ULL)) goto pa202;
        goto po201;
pa202:;
        uint8_t t205 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
        if (t205 != ((uint8_t)7ULL)) goto pa204;
        goto po201;
pa204:;
        uint8_t t207 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
        if (t207 != ((uint8_t)8ULL)) goto pa206;
        goto po201;
pa206:;
        uint8_t t209 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
        if (t209 != ((uint8_t)9ULL)) goto pa208;
        goto po201;
pa208:;
        uint8_t t211 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
        if (t211 != ((uint8_t)3ULL)) goto pa210;
        goto po201;
pa210:;
        uint8_t t213 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
        if (t213 != ((uint8_t)6ULL)) goto pa212;
        goto po201;
pa212:;
        goto mn200;
po201:;
        {
            void * t216 = gm_l1;
            uint8_t t217 = GM_LD(uint8_t, (((uint8_t *)t216) + 0));
            if (t217 != ((uint8_t)0ULL)) goto ie215;
            gm_tbci5ospk5nnn t218 = GM_LD(gm_tbci5ospk5nnn, (((uint8_t *)t216) + 8));
            F.l13 = t218;
            {
                void * t221 = gm_l2;
                uint8_t t222 = GM_LD(uint8_t, (((uint8_t *)t221) + 0));
                if (t222 != ((uint8_t)0ULL)) goto ie220;
                gm_tbci5ospk5nnn t223 = GM_LD(gm_tbci5ospk5nnn, (((uint8_t *)t221) + 8));
                F.l14 = t223;
                {
                    gm_tbci5ospk5nnn t225;
                    uint8_t t233 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    switch (t233) { case ((uint8_t)0ULL): goto ma231; case ((uint8_t)1ULL): goto ma231; case ((uint8_t)2ULL): goto ma231; case ((uint8_t)3ULL): goto ma231; case ((uint8_t)4ULL): goto ma231; case ((uint8_t)5ULL): goto ma227; case ((uint8_t)6ULL): goto ma231; case ((uint8_t)7ULL): goto ma228; case ((uint8_t)8ULL): goto ma229; case ((uint8_t)9ULL): goto ma230; case ((uint8_t)10ULL): goto ma231; case ((uint8_t)11ULL): goto ma231; case ((uint8_t)12ULL): goto ma231; case ((uint8_t)13ULL): goto ma231; case ((uint8_t)14ULL): goto ma231; case ((uint8_t)15ULL): goto ma231; case ((uint8_t)16ULL): goto ma231; case ((uint8_t)17ULL): goto ma231; case ((uint8_t)18ULL): goto ma231; case ((uint8_t)19ULL): goto ma231; case ((uint8_t)20ULL): goto ma231; case ((uint8_t)21ULL): goto ma231; default: goto ma231; }
ma227:;
                    uint8_t t235 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    if (t235 != ((uint8_t)5ULL)) goto mn234;
                    {
                        uint8_t t238 = gm_f_is_zero_8tcct9o(&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l14)));
                        if (!t238) goto ie237;
                        {
                            gm_b16_8 t240 = GM_LD(gm_b16_8, gm_cii3u4ppm6k);
                            memset(&(*(gm_t4t568o7qnv02 *)gm_out), 0, sizeof (*(gm_t4t568o7qnv02 *)gm_out));
                            GM_ST(uint8_t, (uint8_t *)&(*(gm_t4t568o7qnv02 *)gm_out) + 0, ((uint8_t)1ULL));
                            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t4t568o7qnv02 *)gm_out) + 8, t240);
                            goto gm_fdone;
                        }
                        goto ix237;
ie237:;
ix237:;
                    }
                    gm_tbci5ospk5nnn t241;
                    gm_f_rem_ib4l1vl((void *)&t241, (void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l13)), &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l14)));
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tbci5ospk5nnn t242 = t241;
                    t225 = t242;
                    goto me226;
mn234:;
ma228:;
                    uint8_t t244 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    if (t244 != ((uint8_t)7ULL)) goto mn243;
                    gm_tbci5ospk5nnn t245;
                    gm_f_bit_and_3tgam8m((void *)&t245, (void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l13)), &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l14)));
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    t225 = t245;
                    goto me226;
mn243:;
ma229:;
                    uint8_t t247 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    if (t247 != ((uint8_t)8ULL)) goto mn246;
                    gm_tbci5ospk5nnn t248;
                    gm_f_bit_or_j1tdrhl((void *)&t248, (void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l13)), &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l14)));
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    t225 = t248;
                    goto me226;
mn246:;
ma230:;
                    uint8_t t250 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    if (t250 != ((uint8_t)9ULL)) goto mn249;
                    gm_tbci5ospk5nnn t251;
                    gm_f_bit_xor_tmvdnad((void *)&t251, (void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l13)), &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l14)));
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    t225 = t251;
                    goto me226;
mn249:;
ma231:;
                    {
                        uint8_t t255 = gm_f_is_negative_k87pp3n(&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l14)));
                        if (!t255) goto ie254;
                        {
                            gm_b16_8 t257;
                            GM_ST(void *, (uint8_t *)&t257, (void *)(gm_snod68sv8lqq6));
                            GM_ST(uintptr_t, (uint8_t *)&t257 + 8, (uintptr_t)(44));
                            memset(&(*(gm_t4t568o7qnv02 *)gm_out), 0, sizeof (*(gm_t4t568o7qnv02 *)gm_out));
                            GM_ST(uint8_t, (uint8_t *)&(*(gm_t4t568o7qnv02 *)gm_out) + 0, ((uint8_t)1ULL));
                            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t4t568o7qnv02 *)gm_out) + 8, t257);
                            goto gm_fdone;
                        }
                        goto ix254;
ie254:;
ix254:;
                    }
                    {
                        gm_tf5srh4mp4b3i t258 = gm_f_to_u64_m01t1da(&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l14)));
                        uint64_t t259;
                        if (((((uint8_t *)&t258))[8] != 0)) {
                            uint64_t t260 = GM_LD(uint64_t, (((uint8_t *)&t258) + 0));
                            t259 = t260;
                        } else {
                            gm_b16_8 t261;
                            GM_ST(void *, (uint8_t *)&t261, (void *)(gm_s73ofu7ms28r6));
                            GM_ST(uintptr_t, (uint8_t *)&t261 + 8, (uintptr_t)(22));
                            memset(&(*(gm_t4t568o7qnv02 *)gm_out), 0, sizeof (*(gm_t4t568o7qnv02 *)gm_out));
                            GM_ST(uint8_t, (uint8_t *)&(*(gm_t4t568o7qnv02 *)gm_out) + 0, ((uint8_t)1ULL));
                            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t4t568o7qnv02 *)gm_out) + 8, t261);
                            goto gm_fdone;
                        }
                        gm_l15 = t259;
                    }
                    {
                        uint64_t t263 = gm_l15;
                        unsigned __int128 n264 = (unsigned __int128)((uint64_t)20ULL);
                        if (n264 >= 64) gm_trap_shift((uint64_t)n264, "u64", gm_src7h8dni243k2v, 153, 28);
                        uint64_t r265 = (uint64_t)((uint64_t)((uint64_t)1ULL) << (int)n264);
                        bool o266 = (uint64_t)(r265 >> (int)n264) != ((uint64_t)1ULL);
                        if (o266) gm_trap("integer overflow in `<<`", gm_src7h8dni243k2v, 153, 28);
                        uint64_t t267 = r265;
                        uint8_t t268 = (t263 > t267);
                        if (!t268) goto ie262;
                        {
                            gm_b16_8 t270;
                            GM_ST(void *, (uint8_t *)&t270, (void *)(gm_s73ofu7ms28r6));
                            GM_ST(uintptr_t, (uint8_t *)&t270 + 8, (uintptr_t)(22));
                            memset(&(*(gm_t4t568o7qnv02 *)gm_out), 0, sizeof (*(gm_t4t568o7qnv02 *)gm_out));
                            GM_ST(uint8_t, (uint8_t *)&(*(gm_t4t568o7qnv02 *)gm_out) + 0, ((uint8_t)1ULL));
                            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t4t568o7qnv02 *)gm_out) + 8, t270);
                            goto gm_fdone;
                        }
                        goto ix262;
ie262:;
ix262:;
                    }
                    gm_tbci5ospk5nnn t271;
                    uint8_t t273 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l0) + 0) == ((uint8_t)6ULL)));
                    if (!t273) goto ie272;
                    uint64_t t275 = gm_l15;
                    gm_tbci5ospk5nnn t276;
                    gm_f_shr_4jp99a9((void *)&t276, (void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l13)), t275);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tbci5ospk5nnn t277 = t276;
                    t271 = t277;
                    goto ix272;
ie272:;
                    uint64_t t279 = gm_l15;
                    gm_tbci5ospk5nnn t280;
                    gm_f_shl_qqecl81((void *)&t280, (void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l13)), t279);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tbci5ospk5nnn t281 = t280;
                    t271 = t281;
ix272:;
                    gm_tbci5ospk5nnn t282 = t271;
                    t225 = t282;
                    goto me226;
mn252:;
ma232:;
                    gm_trap("no match arm matched the scrutinee", gm_src7h8dni243k2v, 142, 29);
me226:;
                    gm_t0ialelq3ql1k t283;
                    memset(&t283, 0, sizeof t283);
                    GM_ST(uint8_t, ((uint8_t *)&t283 + 0), ((uint8_t)0ULL));
                    GM_ST(gm_tbci5ospk5nnn, ((uint8_t *)&t283 + 8), t225);
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_t4t568o7qnv02 *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_t4t568o7qnv02 *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_t4t568o7qnv02 *)gm_out) + 4, 0);
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_t4t568o7qnv02 *)gm_out) + 0, ((uint8_t)0ULL));
                    GM_ST(gm_t0ialelq3ql1k, (uint8_t *)&(*(gm_t4t568o7qnv02 *)gm_out) + 8, t283);
                    goto gm_fdone;
                }
                goto ix220;
ie220:;
                {
                    gm_ts6oqppjh0ba3 t285 = gm_l0;
                    gm_b16_8 t286 = gm_f_binop_text_0ljfc0r(t285);
                    gm_tnpifv20irnph t287;
                    GM_ST(gm_b16_8, ((uint8_t *)&t287 + 0), t286);
                    gm_tabb710d9mq2e t288;
                    gm_f_format_obkf19v((void *)&t288, (void *)NULL, cx, NULL, t287);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    if (((((uint8_t *)&t288))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t288) + 8, gm_src7h8dni243k2v, 141, 44);
                    gm_b16_8 t289 = GM_LD(gm_b16_8, (((uint8_t *)&t288) + 8));
                    gm_b16_8 t290 = GM_LD(gm_b16_8, ((uint8_t *)&t289));
                    memset(&(*(gm_t4t568o7qnv02 *)gm_out), 0, sizeof (*(gm_t4t568o7qnv02 *)gm_out));
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_t4t568o7qnv02 *)gm_out) + 0, ((uint8_t)1ULL));
                    GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t4t568o7qnv02 *)gm_out) + 8, t290);
                    goto gm_fdone;
                }
ix220:;
            }
            goto ix215;
ie215:;
            {
                gm_ts6oqppjh0ba3 t292 = gm_l0;
                gm_b16_8 t293 = gm_f_binop_text_0ljfc0r(t292);
                gm_tnpifv20irnph t294;
                GM_ST(gm_b16_8, ((uint8_t *)&t294 + 0), t293);
                gm_tabb710d9mq2e t295;
                gm_f_format_obkf19v((void *)&t295, (void *)NULL, cx, NULL, t294);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t295))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t295) + 8, gm_src7h8dni243k2v, 140, 44);
                gm_b16_8 t296 = GM_LD(gm_b16_8, (((uint8_t *)&t295) + 8));
                gm_b16_8 t297 = GM_LD(gm_b16_8, ((uint8_t *)&t296));
                memset(&(*(gm_t4t568o7qnv02 *)gm_out), 0, sizeof (*(gm_t4t568o7qnv02 *)gm_out));
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t4t568o7qnv02 *)gm_out) + 0, ((uint8_t)1ULL));
                GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t4t568o7qnv02 *)gm_out) + 8, t297);
                goto gm_fdone;
            }
ix215:;
        }
        goto me20;
mn200:;
ma24:;
        gm_ts6oqppjh0ba3 t299 = gm_l0;
        gm_b16_8 t300 = gm_f_binop_text_0ljfc0r(t299);
        gm_tnpifv20irnph t301;
        GM_ST(gm_b16_8, ((uint8_t *)&t301 + 0), t300);
        gm_tabb710d9mq2e t302;
        gm_f_format_9453blg((void *)&t302, (void *)NULL, cx, NULL, t301);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t302))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t302) + 8, gm_src7h8dni243k2v, 158, 26);
        gm_b16_8 t303 = GM_LD(gm_b16_8, (((uint8_t *)&t302) + 8));
        gm_b16_8 t304 = GM_LD(gm_b16_8, ((uint8_t *)&t303));
        memset(&(*(gm_t4t568o7qnv02 *)gm_out), 0, sizeof (*(gm_t4t568o7qnv02 *)gm_out));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t4t568o7qnv02 *)gm_out) + 0, ((uint8_t)1ULL));
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t4t568o7qnv02 *)gm_out) + 8, t304);
        goto gm_fdone;
        goto me20;
mn298:;
ma25:;
        gm_trap("no match arm matched the scrutinee", gm_src7h8dni243k2v, 93, 5);
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
__attribute__((aligned(4))) void gm_f_complex_binary_fggi5t8(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_ts6oqppjh0ba3 a0, void * a1, void * a2) {
    struct gm_fr_complex_binary_fggi5t8 F;
    gm_ts6oqppjh0ba3 gm_l0; /* op */
    void * gm_l1; /* a */
    void * gm_l2; /* b */
    GM_STACK_CHECK(&F, 4496u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l1;
        gm_tk3f7nn6jq07c t3;
        gm_f_as_complex_7e8vejf((void *)&t3, (void *)NULL, cx, NULL, t2);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tu15p0hi2qlv6 t4;
        if (((((uint8_t *)&t3))[192] != 0)) {
            t4 = (*(gm_tu15p0hi2qlv6 *)(((uint8_t *)&t3) + 0));
        } else {
            gm_b16_8 t5;
            GM_ST(void *, (uint8_t *)&t5, (void *)(gm_s8qs8rpj34te9));
            GM_ST(uintptr_t, (uint8_t *)&t5 + 8, (uintptr_t)(27));
            memset(&(*(gm_t4t568o7qnv02 *)gm_out), 0, sizeof (*(gm_t4t568o7qnv02 *)gm_out));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t4t568o7qnv02 *)gm_out) + 0, ((uint8_t)1ULL));
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t4t568o7qnv02 *)gm_out) + 8, t5);
            goto gm_fdone;
        }
        F.l3 = t4;
    }
    {
        void * t6 = gm_l2;
        gm_tk3f7nn6jq07c t7;
        gm_f_as_complex_7e8vejf((void *)&t7, (void *)NULL, cx, NULL, t6);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tu15p0hi2qlv6 t8;
        if (((((uint8_t *)&t7))[192] != 0)) {
            t8 = (*(gm_tu15p0hi2qlv6 *)(((uint8_t *)&t7) + 0));
        } else {
            gm_b16_8 t9;
            GM_ST(void *, (uint8_t *)&t9, (void *)(gm_s8qs8rpj34te9));
            GM_ST(uintptr_t, (uint8_t *)&t9 + 8, (uintptr_t)(27));
            memset(&(*(gm_t4t568o7qnv02 *)gm_out), 0, sizeof (*(gm_t4t568o7qnv02 *)gm_out));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t4t568o7qnv02 *)gm_out) + 0, ((uint8_t)1ULL));
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t4t568o7qnv02 *)gm_out) + 8, t9);
            goto gm_fdone;
        }
        F.l4 = t8;
    }
    {
        gm_tngtuiuskn938 t10 = GM_LD(gm_tngtuiuskn938, (((uint8_t *)&F.l3) + 0));
        F.l5 = t10;
    }
    {
        gm_tngtuiuskn938 t11 = GM_LD(gm_tngtuiuskn938, (((uint8_t *)&F.l3) + 96));
        F.l6 = t11;
    }
    {
        gm_tngtuiuskn938 t12 = GM_LD(gm_tngtuiuskn938, (((uint8_t *)&F.l4) + 0));
        F.l7 = t12;
    }
    {
        gm_tngtuiuskn938 t13 = GM_LD(gm_tngtuiuskn938, (((uint8_t *)&F.l4) + 96));
        F.l8 = t13;
    }
    {
        uint8_t t24 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
        switch (t24) { case ((uint8_t)0ULL): goto ma15; case ((uint8_t)1ULL): goto ma16; case ((uint8_t)2ULL): goto ma17; case ((uint8_t)3ULL): goto ma22; case ((uint8_t)4ULL): goto ma18; case ((uint8_t)5ULL): goto ma22; case ((uint8_t)6ULL): goto ma22; case ((uint8_t)7ULL): goto ma22; case ((uint8_t)8ULL): goto ma22; case ((uint8_t)9ULL): goto ma22; case ((uint8_t)10ULL): goto ma22; case ((uint8_t)11ULL): goto ma22; case ((uint8_t)12ULL): goto ma19; case ((uint8_t)13ULL): goto ma20; case ((uint8_t)14ULL): goto ma22; case ((uint8_t)15ULL): goto ma22; case ((uint8_t)16ULL): goto ma21; case ((uint8_t)17ULL): goto ma21; case ((uint8_t)18ULL): goto ma21; case ((uint8_t)19ULL): goto ma21; case ((uint8_t)20ULL): goto ma22; case ((uint8_t)21ULL): goto ma22; default: goto ma22; }
ma15:;
        uint8_t t26 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
        if (t26 != ((uint8_t)0ULL)) goto mn25;
        gm_tu15p0hi2qlv6 t27;
        gm_f_add_837b4po((void *)&(*(gm_tngtuiuskn938 *)((uint8_t *)&t27 + 0)), (void *)NULL, cx, NULL, &(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l5)), &(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l7)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_add_837b4po((void *)&(*(gm_tngtuiuskn938 *)((uint8_t *)&t27 + 96)), (void *)NULL, cx, NULL, &(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l6)), &(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l8)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t0ialelq3ql1k t28;
        GM_ST(uint8_t, (uint8_t *)&t28 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t28 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t28 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)4ULL));
        GM_ST(gm_tu15p0hi2qlv6, ((uint8_t *)&t28 + 8), t27);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t4t568o7qnv02 *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t4t568o7qnv02 *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t4t568o7qnv02 *)gm_out) + 4, 0);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t4t568o7qnv02 *)gm_out) + 0, ((uint8_t)0ULL));
        GM_ST(gm_t0ialelq3ql1k, (uint8_t *)&(*(gm_t4t568o7qnv02 *)gm_out) + 8, t28);
        goto gm_fdone;
        goto me14;
mn25:;
ma16:;
        uint8_t t30 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
        if (t30 != ((uint8_t)1ULL)) goto mn29;
        gm_tu15p0hi2qlv6 t31;
        gm_f_sub_mler6q8((void *)&(*(gm_tngtuiuskn938 *)((uint8_t *)&t31 + 0)), (void *)NULL, cx, NULL, &(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l5)), &(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l7)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_sub_mler6q8((void *)&(*(gm_tngtuiuskn938 *)((uint8_t *)&t31 + 96)), (void *)NULL, cx, NULL, &(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l6)), &(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l8)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t0ialelq3ql1k t32;
        GM_ST(uint8_t, (uint8_t *)&t32 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t32 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t32 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t32 + 0), ((uint8_t)4ULL));
        GM_ST(gm_tu15p0hi2qlv6, ((uint8_t *)&t32 + 8), t31);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t4t568o7qnv02 *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t4t568o7qnv02 *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t4t568o7qnv02 *)gm_out) + 4, 0);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t4t568o7qnv02 *)gm_out) + 0, ((uint8_t)0ULL));
        GM_ST(gm_t0ialelq3ql1k, (uint8_t *)&(*(gm_t4t568o7qnv02 *)gm_out) + 8, t32);
        goto gm_fdone;
        goto me14;
mn29:;
ma17:;
        uint8_t t34 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
        if (t34 != ((uint8_t)2ULL)) goto mn33;
        gm_tu15p0hi2qlv6 t35;
        gm_tngtuiuskn938 t36;
        gm_f_mul_od1e7nd((void *)&t36, (void *)NULL, cx, NULL, &(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l5)), &(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l7)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tngtuiuskn938 t37;
        gm_f_mul_od1e7nd((void *)&t37, (void *)NULL, cx, NULL, &(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l6)), &(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l8)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_sub_mler6q8((void *)&(*(gm_tngtuiuskn938 *)((uint8_t *)&t35 + 0)), (void *)NULL, cx, NULL, &t36, &t37);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tngtuiuskn938 t38;
        gm_f_mul_od1e7nd((void *)&t38, (void *)NULL, cx, NULL, &(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l5)), &(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l8)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tngtuiuskn938 t39;
        gm_f_mul_od1e7nd((void *)&t39, (void *)NULL, cx, NULL, &(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l6)), &(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l7)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_add_837b4po((void *)&(*(gm_tngtuiuskn938 *)((uint8_t *)&t35 + 96)), (void *)NULL, cx, NULL, &t38, &t39);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t0ialelq3ql1k t40;
        GM_ST(uint8_t, (uint8_t *)&t40 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t40 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t40 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t40 + 0), ((uint8_t)4ULL));
        GM_ST(gm_tu15p0hi2qlv6, ((uint8_t *)&t40 + 8), t35);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t4t568o7qnv02 *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t4t568o7qnv02 *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t4t568o7qnv02 *)gm_out) + 4, 0);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t4t568o7qnv02 *)gm_out) + 0, ((uint8_t)0ULL));
        GM_ST(gm_t0ialelq3ql1k, (uint8_t *)&(*(gm_t4t568o7qnv02 *)gm_out) + 8, t40);
        goto gm_fdone;
        goto me14;
mn33:;
ma18:;
        uint8_t t42 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
        if (t42 != ((uint8_t)4ULL)) goto mn41;
        {
            gm_tngtuiuskn938 t44;
            gm_f_mul_od1e7nd((void *)&t44, (void *)NULL, cx, NULL, &(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l7)), &(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l7)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tngtuiuskn938 t45;
            gm_f_mul_od1e7nd((void *)&t45, (void *)NULL, cx, NULL, &(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l8)), &(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l8)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_f_add_837b4po((void *)&F.l9, (void *)NULL, cx, NULL, &t44, &t45);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            uint8_t t47 = gm_f_is_zero_5aept6u(&(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l9)));
            if (!t47) goto ie46;
            {
                gm_b16_8 t49 = GM_LD(gm_b16_8, gm_cii3u4ppm6k);
                memset(&(*(gm_t4t568o7qnv02 *)gm_out), 0, sizeof (*(gm_t4t568o7qnv02 *)gm_out));
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t4t568o7qnv02 *)gm_out) + 0, ((uint8_t)1ULL));
                GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t4t568o7qnv02 *)gm_out) + 8, t49);
                goto gm_fdone;
            }
            goto ix46;
ie46:;
ix46:;
        }
        {
            gm_tu15p0hi2qlv6 t50;
            gm_tngtuiuskn938 t51;
            gm_f_mul_od1e7nd((void *)&t51, (void *)NULL, cx, NULL, &(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l5)), &(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l7)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tngtuiuskn938 t52;
            gm_f_mul_od1e7nd((void *)&t52, (void *)NULL, cx, NULL, &(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l6)), &(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l8)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tngtuiuskn938 t53;
            gm_f_add_837b4po((void *)&t53, (void *)NULL, cx, NULL, &t51, &t52);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_f_div_d8sd4qc((void *)&(*(gm_tngtuiuskn938 *)((uint8_t *)&t50 + 0)), (void *)NULL, cx, NULL, &t53, &(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l9)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tngtuiuskn938 t54;
            gm_f_mul_od1e7nd((void *)&t54, (void *)NULL, cx, NULL, &(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l6)), &(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l7)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tngtuiuskn938 t55;
            gm_f_mul_od1e7nd((void *)&t55, (void *)NULL, cx, NULL, &(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l5)), &(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l8)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tngtuiuskn938 t56;
            gm_f_sub_mler6q8((void *)&t56, (void *)NULL, cx, NULL, &t54, &t55);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_f_div_d8sd4qc((void *)&(*(gm_tngtuiuskn938 *)((uint8_t *)&t50 + 96)), (void *)NULL, cx, NULL, &t56, &(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l9)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t0ialelq3ql1k t57;
            GM_ST(uint8_t, (uint8_t *)&t57 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t57 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t57 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t57 + 0), ((uint8_t)4ULL));
            GM_ST(gm_tu15p0hi2qlv6, ((uint8_t *)&t57 + 8), t50);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t4t568o7qnv02 *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t4t568o7qnv02 *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t4t568o7qnv02 *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t4t568o7qnv02 *)gm_out) + 0, ((uint8_t)0ULL));
            GM_ST(gm_t0ialelq3ql1k, (uint8_t *)&(*(gm_t4t568o7qnv02 *)gm_out) + 8, t57);
            goto gm_fdone;
        }
        goto me14;
mn41:;
ma19:;
        uint8_t t59 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
        if (t59 != ((uint8_t)12ULL)) goto mn58;
        uint8_t t60 = 0;
        uint8_t t61 = gm_f_eq_ceufbo4(&(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l5)), &(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l7)));
        if (t61) {
            uint8_t t62 = gm_f_eq_ceufbo4(&(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l6)), &(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l8)));
            t60 = t62;
        } else t60 = 0;
        gm_t0ialelq3ql1k t63;
        memset(&t63, 0, sizeof t63);
        GM_ST(uint8_t, ((uint8_t *)&t63 + 0), ((uint8_t)3ULL));
        GM_ST(uint8_t, ((uint8_t *)&t63 + 8), t60);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t4t568o7qnv02 *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t4t568o7qnv02 *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t4t568o7qnv02 *)gm_out) + 4, 0);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t4t568o7qnv02 *)gm_out) + 0, ((uint8_t)0ULL));
        GM_ST(gm_t0ialelq3ql1k, (uint8_t *)&(*(gm_t4t568o7qnv02 *)gm_out) + 8, t63);
        goto gm_fdone;
        goto me14;
mn58:;
ma20:;
        uint8_t t65 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
        if (t65 != ((uint8_t)13ULL)) goto mn64;
        uint8_t t66 = 0;
        uint8_t t67 = gm_f_eq_ceufbo4(&(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l5)), &(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l7)));
        uint8_t t68 = (uint8_t)!t67;
        if (!t68) {
            uint8_t t69 = gm_f_eq_ceufbo4(&(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l6)), &(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l8)));
            uint8_t t70 = (uint8_t)!t69;
            t66 = t70;
        } else t66 = 1;
        gm_t0ialelq3ql1k t71;
        memset(&t71, 0, sizeof t71);
        GM_ST(uint8_t, ((uint8_t *)&t71 + 0), ((uint8_t)3ULL));
        GM_ST(uint8_t, ((uint8_t *)&t71 + 8), t66);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t4t568o7qnv02 *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t4t568o7qnv02 *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t4t568o7qnv02 *)gm_out) + 4, 0);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t4t568o7qnv02 *)gm_out) + 0, ((uint8_t)0ULL));
        GM_ST(gm_t0ialelq3ql1k, (uint8_t *)&(*(gm_t4t568o7qnv02 *)gm_out) + 8, t71);
        goto gm_fdone;
        goto me14;
mn64:;
ma21:;
        uint8_t t75 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
        if (t75 != ((uint8_t)16ULL)) goto pa74;
        goto po73;
pa74:;
        uint8_t t77 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
        if (t77 != ((uint8_t)17ULL)) goto pa76;
        goto po73;
pa76:;
        uint8_t t79 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
        if (t79 != ((uint8_t)18ULL)) goto pa78;
        goto po73;
pa78:;
        uint8_t t81 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
        if (t81 != ((uint8_t)19ULL)) goto pa80;
        goto po73;
pa80:;
        goto mn72;
po73:;
        gm_b16_8 t82;
        GM_ST(void *, (uint8_t *)&t82, (void *)(gm_scaduriptfv6s));
        GM_ST(uintptr_t, (uint8_t *)&t82 + 8, (uintptr_t)(33));
        memset(&(*(gm_t4t568o7qnv02 *)gm_out), 0, sizeof (*(gm_t4t568o7qnv02 *)gm_out));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t4t568o7qnv02 *)gm_out) + 0, ((uint8_t)1ULL));
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t4t568o7qnv02 *)gm_out) + 8, t82);
        goto gm_fdone;
        goto me14;
mn72:;
ma22:;
        gm_ts6oqppjh0ba3 t84 = gm_l0;
        gm_b16_8 t85 = gm_f_binop_text_0ljfc0r(t84);
        gm_tnpifv20irnph t86;
        GM_ST(gm_b16_8, ((uint8_t *)&t86 + 0), t85);
        gm_tabb710d9mq2e t87;
        gm_f_format_thddtj7((void *)&t87, (void *)NULL, cx, NULL, t86);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t87))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t87) + 8, gm_src7h8dni243k2v, 183, 26);
        gm_b16_8 t88 = GM_LD(gm_b16_8, (((uint8_t *)&t87) + 8));
        gm_b16_8 t89 = GM_LD(gm_b16_8, ((uint8_t *)&t88));
        memset(&(*(gm_t4t568o7qnv02 *)gm_out), 0, sizeof (*(gm_t4t568o7qnv02 *)gm_out));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t4t568o7qnv02 *)gm_out) + 0, ((uint8_t)1ULL));
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t4t568o7qnv02 *)gm_out) + 8, t89);
        goto gm_fdone;
        goto me14;
mn83:;
ma23:;
        gm_trap("no match arm matched the scrutinee", gm_src7h8dni243k2v, 171, 5);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_describe_bm129vh(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_describe_bm129vh F;
    void * gm_l0; /* v */
    GM_STACK_CHECK_SHALLOW(&F, 134u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    gm_b16_8 t2;
    uint8_t t9 = GM_LD(uint8_t, (((uint8_t *)t1) + 0));
    switch (t9) { case ((uint8_t)0ULL): goto ma4; case ((uint8_t)1ULL): goto ma5; case ((uint8_t)2ULL): goto ma5; case ((uint8_t)3ULL): goto ma6; case ((uint8_t)4ULL): goto ma7; default: goto ma8; }
ma4:;
    uint8_t t11 = GM_LD(uint8_t, (((uint8_t *)t1) + 0));
    if (t11 != ((uint8_t)0ULL)) goto mn10;
    gm_b16_8 t12;
    GM_ST(void *, (uint8_t *)&t12, (void *)(gm_sc003f3fvnij4));
    GM_ST(uintptr_t, (uint8_t *)&t12 + 8, (uintptr_t)(16));
    t2 = t12;
    goto me3;
mn10:;
ma5:;
    uint8_t t16 = GM_LD(uint8_t, (((uint8_t *)t1) + 0));
    if (t16 != ((uint8_t)1ULL)) goto pa15;
    goto po14;
pa15:;
    uint8_t t18 = GM_LD(uint8_t, (((uint8_t *)t1) + 0));
    if (t18 != ((uint8_t)2ULL)) goto pa17;
    goto po14;
pa17:;
    goto mn13;
po14:;
    gm_b16_8 t19;
    GM_ST(void *, (uint8_t *)&t19, (void *)(gm_splm7v4oko2tv));
    GM_ST(uintptr_t, (uint8_t *)&t19 + 8, (uintptr_t)(14));
    t2 = t19;
    goto me3;
mn13:;
ma6:;
    uint8_t t21 = GM_LD(uint8_t, (((uint8_t *)t1) + 0));
    if (t21 != ((uint8_t)3ULL)) goto mn20;
    gm_b16_8 t22;
    GM_ST(void *, (uint8_t *)&t22, (void *)(gm_sek3nvre3su54));
    GM_ST(uintptr_t, (uint8_t *)&t22 + 8, (uintptr_t)(16));
    t2 = t22;
    goto me3;
mn20:;
ma7:;
    uint8_t t24 = GM_LD(uint8_t, (((uint8_t *)t1) + 0));
    if (t24 != ((uint8_t)4ULL)) goto mn23;
    gm_b16_8 t25;
    GM_ST(void *, (uint8_t *)&t25, (void *)(gm_sv56kn0notafb));
    GM_ST(uintptr_t, (uint8_t *)&t25 + 8, (uintptr_t)(16));
    t2 = t25;
    goto me3;
mn23:;
ma8:;
    gm_trap("no match arm matched the scrutinee", gm_src7h8dni243k2v, 29, 48);
me3:;
    F.ret = t2;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_dump_untyped_o8mt4q8(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_dump_untyped_o8mt4q8 F;
    void * gm_l0; /* v */
    uint8_t gm_l3; /* b */
    GM_STACK_CHECK(&F, 778u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    gm_b16_8 t2;
    uint8_t t10 = GM_LD(uint8_t, (((uint8_t *)t1) + 0));
    switch (t10) { case ((uint8_t)0ULL): goto ma4; case ((uint8_t)1ULL): goto ma5; case ((uint8_t)2ULL): goto ma6; case ((uint8_t)3ULL): goto ma7; case ((uint8_t)4ULL): goto ma8; default: goto ma9; }
ma4:;
    uint8_t t12 = GM_LD(uint8_t, (((uint8_t *)t1) + 0));
    if (t12 != ((uint8_t)0ULL)) goto mn11;
    gm_tbci5ospk5nnn t13 = GM_LD(gm_tbci5ospk5nnn, (((uint8_t *)t1) + 8));
    F.l1 = t13;
    gm_b16_8 t14 = gm_f_show_int_ub6npub((void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l1)));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tnpifv20irnph t15;
    GM_ST(gm_b16_8, ((uint8_t *)&t15 + 0), t14);
    gm_tabb710d9mq2e t16;
    gm_f_format_ejli3qf((void *)&t16, (void *)NULL, cx, NULL, t15);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    if (((((uint8_t *)&t16))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t16) + 8, gm_src7h8dni243k2v, 214, 16);
    gm_b16_8 t17 = GM_LD(gm_b16_8, (((uint8_t *)&t16) + 8));
    gm_b16_8 t18 = GM_LD(gm_b16_8, ((uint8_t *)&t17));
    t2 = t18;
    goto me3;
mn11:;
ma5:;
    uint8_t t20 = GM_LD(uint8_t, (((uint8_t *)t1) + 0));
    if (t20 != ((uint8_t)1ULL)) goto mn19;
    gm_tngtuiuskn938 t21 = GM_LD(gm_tngtuiuskn938, (((uint8_t *)t1) + 8));
    F.l2 = t21;
    gm_b16_8 t22 = gm_f_show_rational_00mlc2h((void *)NULL, cx, NULL, &(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l2)));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tnpifv20irnph t23;
    GM_ST(gm_b16_8, ((uint8_t *)&t23 + 0), t22);
    gm_tabb710d9mq2e t24;
    gm_f_format_o65p921((void *)&t24, (void *)NULL, cx, NULL, t23);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    if (((((uint8_t *)&t24))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t24) + 8, gm_src7h8dni243k2v, 215, 18);
    gm_b16_8 t25 = GM_LD(gm_b16_8, (((uint8_t *)&t24) + 8));
    gm_b16_8 t26 = GM_LD(gm_b16_8, ((uint8_t *)&t25));
    t2 = t26;
    goto me3;
mn19:;
ma6:;
    uint8_t t28 = GM_LD(uint8_t, (((uint8_t *)t1) + 0));
    if (t28 != ((uint8_t)2ULL)) goto mn27;
    gm_b16_8 t29;
    GM_ST(void *, (uint8_t *)&t29, (void *)(gm_s9c60h0qendav));
    GM_ST(uintptr_t, (uint8_t *)&t29 + 8, (uintptr_t)(8));
    t2 = t29;
    goto me3;
mn27:;
ma7:;
    uint8_t t31 = GM_LD(uint8_t, (((uint8_t *)t1) + 0));
    if (t31 != ((uint8_t)3ULL)) goto mn30;
    uint8_t t32 = GM_LD(uint8_t, (((uint8_t *)t1) + 8));
    gm_l3 = t32;
    uint8_t t33 = gm_l3;
    gm_t9qkh0rsooc8d t34;
    GM_ST(uint8_t, ((uint8_t *)&t34 + 0), t33);
    gm_tabb710d9mq2e t35;
    gm_f_format_as0b1t3((void *)&t35, (void *)NULL, cx, NULL, t34);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    if (((((uint8_t *)&t35))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t35) + 8, gm_src7h8dni243k2v, 217, 17);
    gm_b16_8 t36 = GM_LD(gm_b16_8, (((uint8_t *)&t35) + 8));
    gm_b16_8 t37 = GM_LD(gm_b16_8, ((uint8_t *)&t36));
    t2 = t37;
    goto me3;
mn30:;
ma8:;
    uint8_t t39 = GM_LD(uint8_t, (((uint8_t *)t1) + 0));
    if (t39 != ((uint8_t)4ULL)) goto mn38;
    gm_tngtuiuskn938 t40 = GM_LD(gm_tngtuiuskn938, ((((uint8_t *)t1) + 8) + 0));
    F.l4 = t40;
    gm_tngtuiuskn938 t41 = GM_LD(gm_tngtuiuskn938, ((((uint8_t *)t1) + 8) + 96));
    F.l5 = t41;
    gm_t6j6smj7qoctu t42;
    gm_b16_8 t43 = gm_f_show_rational_00mlc2h((void *)NULL, cx, NULL, &(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l4)));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    GM_ST(gm_b16_8, (uint8_t *)&t42 + 0, t43);
    gm_b16_8 t44 = gm_f_show_rational_00mlc2h((void *)NULL, cx, NULL, &(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l5)));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    GM_ST(gm_b16_8, (uint8_t *)&t42 + 16, t44);
    gm_tabb710d9mq2e t45;
    gm_f_format_djdpqv2((void *)&t45, (void *)NULL, cx, NULL, &t42);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    if (((((uint8_t *)&t45))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t45) + 8, gm_src7h8dni243k2v, 218, 25);
    gm_b16_8 t46 = GM_LD(gm_b16_8, (((uint8_t *)&t45) + 8));
    gm_b16_8 t47 = GM_LD(gm_b16_8, ((uint8_t *)&t46));
    t2 = t47;
    goto me3;
mn38:;
ma9:;
    gm_trap("no match arm matched the scrutinee", gm_src7h8dni243k2v, 213, 64);
me3:;
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
inline __attribute__((aligned(4))) void gm_f_float_result_csnia2t(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_ts6oqppjh0ba3 a0, void * a1, void * a2, const void * a3) {
    struct gm_fr_float_result_csnia2t F;
    gm_ts6oqppjh0ba3 gm_l0; /* op */
    void * gm_l1; /* a */
    void * gm_l2; /* b */
    uint8_t gm_l4; /* negative */
    GM_STACK_CHECK(&F, 1198u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        uint8_t t3 = gm_f_is_zero_5aept6u(&(*(gm_tngtuiuskn938 *)((uint8_t *)a3)));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            const uint8_t *t6 = ((uint8_t *)a3);
            memset(&(*(gm_t0ialelq3ql1k *)gm_out), 0, sizeof (*(gm_t0ialelq3ql1k *)gm_out));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t0ialelq3ql1k *)gm_out) + 0, ((uint8_t)1ULL));
            memmove((uint8_t *)&(*(gm_t0ialelq3ql1k *)gm_out) + 8, t6, 96);
            goto gm_fdone;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        uint8_t t7;
        uint8_t t13 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
        switch (t13) { case ((uint8_t)0ULL): goto ma9; case ((uint8_t)1ULL): goto ma10; case ((uint8_t)2ULL): goto ma11; case ((uint8_t)3ULL): goto ma11; case ((uint8_t)4ULL): goto ma11; case ((uint8_t)5ULL): goto ma11; case ((uint8_t)6ULL): goto ma11; case ((uint8_t)7ULL): goto ma11; case ((uint8_t)8ULL): goto ma11; case ((uint8_t)9ULL): goto ma11; case ((uint8_t)10ULL): goto ma11; case ((uint8_t)11ULL): goto ma11; case ((uint8_t)12ULL): goto ma11; case ((uint8_t)13ULL): goto ma11; case ((uint8_t)14ULL): goto ma11; case ((uint8_t)15ULL): goto ma11; case ((uint8_t)16ULL): goto ma11; case ((uint8_t)17ULL): goto ma11; case ((uint8_t)18ULL): goto ma11; case ((uint8_t)19ULL): goto ma11; case ((uint8_t)20ULL): goto ma11; case ((uint8_t)21ULL): goto ma11; default: goto ma11; }
ma9:;
        uint8_t t15 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
        if (t15 != ((uint8_t)0ULL)) goto mn14;
        uint8_t t16 = 0;
        void * t17 = gm_l1;
        uint8_t t18 = ((GM_LD(uint8_t, ((uint8_t *)t17) + 0) == ((uint8_t)2ULL)));
        if (t18) {
            void * t19 = gm_l2;
            uint8_t t20 = ((GM_LD(uint8_t, ((uint8_t *)t19) + 0) == ((uint8_t)2ULL)));
            t16 = t20;
        } else t16 = 0;
        t7 = t16;
        goto me8;
mn14:;
ma10:;
        uint8_t t22 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
        if (t22 != ((uint8_t)1ULL)) goto mn21;
        uint8_t t23 = 0;
        uint8_t t24 = 0;
        void * t25 = gm_l1;
        uint8_t t26 = ((GM_LD(uint8_t, ((uint8_t *)t25) + 0) == ((uint8_t)2ULL)));
        if (t26) {
            void * t27 = gm_l2;
            uint8_t t28 = (!(GM_LD(uint8_t, ((uint8_t *)t27) + 0) == ((uint8_t)2ULL)));
            t24 = t28;
        } else t24 = 0;
        if (t24) {
            void * t29 = gm_l2;
            uint8_t t30 = gm_f_is_zero_f3o1s37((void *)NULL, cx, NULL, t29);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            t23 = t30;
        } else t23 = 0;
        t7 = t23;
        goto me8;
mn21:;
ma11:;
        void * t32 = gm_l1;
        uint8_t t33 = gm_f_is_neg_q1tmt2n(t32);
        void * t34 = gm_l2;
        uint8_t t35 = gm_f_is_neg_q1tmt2n(t34);
        uint8_t t36 = (!(t33 == t35));
        t7 = t36;
        goto me8;
mn31:;
ma12:;
        gm_trap("no match arm matched the scrutinee", gm_src7h8dni243k2v, 190, 17);
me8:;
        gm_l4 = t7;
    }
    {
        gm_t0ialelq3ql1k t37;
        uint8_t t39 = gm_l4;
        if (!t39) goto ie38;
        gm_t0ialelq3ql1k t41;
        memset(&t41, 0, sizeof t41);
        GM_ST(uint8_t, ((uint8_t *)&t41 + 0), ((uint8_t)2ULL));
        gm_t0ialelq3ql1k t42 = t41;
        t37 = t42;
        goto ix38;
ie38:;
        gm_tngtuiuskn938 t44 = GM_LD(gm_tngtuiuskn938, ((uint8_t *)a3));
        gm_t0ialelq3ql1k t45;
        memset(&t45, 0, sizeof t45);
        GM_ST(uint8_t, ((uint8_t *)&t45 + 0), ((uint8_t)1ULL));
        GM_ST(gm_tngtuiuskn938, ((uint8_t *)&t45 + 8), t44);
        gm_t0ialelq3ql1k t46 = t45;
        t37 = t46;
ix38:;
        uint8_t t47 = 0;
        F.ret = t37;
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_neg_q1tmt2n(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_neg_q1tmt2n F;
    void * gm_l0; /* v */
    GM_STACK_CHECK_SHALLOW(&F, 169u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    uint8_t t2;
    uint8_t t9 = GM_LD(uint8_t, (((uint8_t *)t1) + 0));
    switch (t9) { case ((uint8_t)0ULL): goto ma5; case ((uint8_t)1ULL): goto ma6; case ((uint8_t)2ULL): goto ma4; case ((uint8_t)3ULL): goto ma7; case ((uint8_t)4ULL): goto ma7; default: goto ma7; }
ma4:;
    uint8_t t11 = GM_LD(uint8_t, (((uint8_t *)t1) + 0));
    if (t11 != ((uint8_t)2ULL)) goto mn10;
    t2 = ((uint8_t)1);
    goto me3;
mn10:;
ma5:;
    uint8_t t13 = GM_LD(uint8_t, (((uint8_t *)t1) + 0));
    if (t13 != ((uint8_t)0ULL)) goto mn12;
    gm_tbci5ospk5nnn t14 = GM_LD(gm_tbci5ospk5nnn, (((uint8_t *)t1) + 8));
    F.l1 = t14;
    uint8_t t15 = gm_f_is_negative_k87pp3n(&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l1)));
    t2 = t15;
    goto me3;
mn12:;
ma6:;
    uint8_t t17 = GM_LD(uint8_t, (((uint8_t *)t1) + 0));
    if (t17 != ((uint8_t)1ULL)) goto mn16;
    gm_tngtuiuskn938 t18 = GM_LD(gm_tngtuiuskn938, (((uint8_t *)t1) + 8));
    F.l2 = t18;
    uint8_t t19 = gm_f_is_negative_rb46l9j(&(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l2)));
    t2 = t19;
    goto me3;
mn16:;
ma7:;
    t2 = ((uint8_t)0);
    goto me3;
mn20:;
ma8:;
    gm_trap("no match arm matched the scrutinee", gm_src7h8dni243k2v, 199, 44);
me3:;
    F.ret = t2;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) uint8_t gm_f_is_zero_f3o1s37(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) void gm_f_negate_fohael7(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_negate_fohael7 F;
    void * gm_l0; /* v */
    GM_STACK_CHECK(&F, 3655u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    gm_t4t568o7qnv02 t2;
    uint8_t t10 = GM_LD(uint8_t, (((uint8_t *)t1) + 0));
    switch (t10) { case ((uint8_t)0ULL): goto ma4; case ((uint8_t)1ULL): goto ma5; case ((uint8_t)2ULL): goto ma6; case ((uint8_t)3ULL): goto ma7; case ((uint8_t)4ULL): goto ma8; default: goto ma9; }
ma4:;
    uint8_t t12 = GM_LD(uint8_t, (((uint8_t *)t1) + 0));
    if (t12 != ((uint8_t)0ULL)) goto mn11;
    gm_tbci5ospk5nnn t13 = GM_LD(gm_tbci5ospk5nnn, (((uint8_t *)t1) + 8));
    F.l1 = t13;
    gm_tbci5ospk5nnn t14;
    gm_f_neg_agd5c5k((void *)&t14, (void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l1)));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_t0ialelq3ql1k t15;
    memset(&t15, 0, sizeof t15);
    GM_ST(uint8_t, ((uint8_t *)&t15 + 0), ((uint8_t)0ULL));
    GM_ST(gm_tbci5ospk5nnn, ((uint8_t *)&t15 + 8), t14);
    gm_t4t568o7qnv02 t16;
    GM_ST(uint8_t, (uint8_t *)&t16 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t16 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t16 + 4, 0);
    GM_ST(uint8_t, ((uint8_t *)&t16 + 0), ((uint8_t)0ULL));
    GM_ST(gm_t0ialelq3ql1k, ((uint8_t *)&t16 + 8), t15);
    t2 = t16;
    goto me3;
mn11:;
ma5:;
    uint8_t t18 = GM_LD(uint8_t, (((uint8_t *)t1) + 0));
    if (t18 != ((uint8_t)1ULL)) goto mn17;
    gm_tngtuiuskn938 t19 = GM_LD(gm_tngtuiuskn938, (((uint8_t *)t1) + 8));
    F.l2 = t19;
    gm_t0ialelq3ql1k t20;
    uint8_t t22 = gm_f_is_zero_5aept6u(&(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l2)));
    if (!t22) goto ie21;
    gm_t0ialelq3ql1k t24;
    memset(&t24, 0, sizeof t24);
    GM_ST(uint8_t, ((uint8_t *)&t24 + 0), ((uint8_t)2ULL));
    gm_t0ialelq3ql1k t25 = t24;
    t20 = t25;
    goto ix21;
ie21:;
    gm_tngtuiuskn938 t27;
    gm_f_neg_0u9uqg6((void *)&t27, (void *)NULL, cx, NULL, &(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l2)));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_t0ialelq3ql1k t28;
    memset(&t28, 0, sizeof t28);
    GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)1ULL));
    GM_ST(gm_tngtuiuskn938, ((uint8_t *)&t28 + 8), t27);
    gm_t0ialelq3ql1k t29 = t28;
    t20 = t29;
ix21:;
    gm_t4t568o7qnv02 t30;
    GM_ST(uint8_t, (uint8_t *)&t30 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t30 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t30 + 4, 0);
    GM_ST(uint8_t, ((uint8_t *)&t30 + 0), ((uint8_t)0ULL));
    GM_ST(gm_t0ialelq3ql1k, ((uint8_t *)&t30 + 8), t20);
    t2 = t30;
    goto me3;
mn17:;
ma6:;
    uint8_t t32 = GM_LD(uint8_t, (((uint8_t *)t1) + 0));
    if (t32 != ((uint8_t)2ULL)) goto mn31;
    gm_tngtuiuskn938 t33;
    gm_f_zero_9r6o7sj((void *)&t33, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_t0ialelq3ql1k t34;
    memset(&t34, 0, sizeof t34);
    GM_ST(uint8_t, ((uint8_t *)&t34 + 0), ((uint8_t)1ULL));
    GM_ST(gm_tngtuiuskn938, ((uint8_t *)&t34 + 8), t33);
    gm_t4t568o7qnv02 t35;
    GM_ST(uint8_t, (uint8_t *)&t35 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t35 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t35 + 4, 0);
    GM_ST(uint8_t, ((uint8_t *)&t35 + 0), ((uint8_t)0ULL));
    GM_ST(gm_t0ialelq3ql1k, ((uint8_t *)&t35 + 8), t34);
    t2 = t35;
    goto me3;
mn31:;
ma7:;
    uint8_t t37 = GM_LD(uint8_t, (((uint8_t *)t1) + 0));
    if (t37 != ((uint8_t)3ULL)) goto mn36;
    gm_b16_8 t38;
    GM_ST(void *, (uint8_t *)&t38, (void *)(gm_stpkldc5dmf71));
    GM_ST(uintptr_t, (uint8_t *)&t38 + 8, (uintptr_t)(32));
    gm_t4t568o7qnv02 t39;
    memset(&t39, 0, sizeof t39);
    GM_ST(uint8_t, ((uint8_t *)&t39 + 0), ((uint8_t)1ULL));
    GM_ST(gm_b16_8, ((uint8_t *)&t39 + 8), t38);
    t2 = t39;
    goto me3;
mn36:;
ma8:;
    uint8_t t41 = GM_LD(uint8_t, (((uint8_t *)t1) + 0));
    if (t41 != ((uint8_t)4ULL)) goto mn40;
    gm_tngtuiuskn938 t42 = GM_LD(gm_tngtuiuskn938, ((((uint8_t *)t1) + 8) + 0));
    F.l3 = t42;
    gm_tngtuiuskn938 t43 = GM_LD(gm_tngtuiuskn938, ((((uint8_t *)t1) + 8) + 96));
    F.l4 = t43;
    gm_tu15p0hi2qlv6 t44;
    gm_f_neg_0u9uqg6((void *)&(*(gm_tngtuiuskn938 *)((uint8_t *)&t44 + 0)), (void *)NULL, cx, NULL, &(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l3)));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_f_neg_0u9uqg6((void *)&(*(gm_tngtuiuskn938 *)((uint8_t *)&t44 + 96)), (void *)NULL, cx, NULL, &(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l4)));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_t0ialelq3ql1k t45;
    GM_ST(uint8_t, (uint8_t *)&t45 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t45 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t45 + 4, 0);
    GM_ST(uint8_t, ((uint8_t *)&t45 + 0), ((uint8_t)4ULL));
    GM_ST(gm_tu15p0hi2qlv6, ((uint8_t *)&t45 + 8), t44);
    gm_t4t568o7qnv02 t46;
    GM_ST(uint8_t, (uint8_t *)&t46 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t46 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t46 + 4, 0);
    GM_ST(uint8_t, ((uint8_t *)&t46 + 0), ((uint8_t)0ULL));
    GM_ST(gm_t0ialelq3ql1k, ((uint8_t *)&t46 + 8), t45);
    t2 = t46;
    goto me3;
mn40:;
ma9:;
    gm_trap("no match arm matched the scrutinee", gm_src7h8dni243k2v, 51, 65);
me3:;
    F.ret = t2;
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
extern __attribute__((aligned(4))) uint64_t gm_f_rational_bits_u60jbai(const void * a0);
__attribute__((aligned(4))) uint64_t gm_f_rational_bits_u60jbai_c(gm_tngtuiuskn938 a0) { return gm_f_rational_bits_u60jbai(&a0); }
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_int_ub6npub(void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_show_int_ub6npub F;
    GM_STACK_CHECK(&F, 147u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memcpy(&F.l0, a0, sizeof F.l0);
    {
        uint64_t t3 = gm_f_bits_lr3of4i(&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l0)));
        uint8_t t4 = (t3 <= ((uint64_t)512ULL));
        if (!t4) goto ie2;
        {
            gm_b16_8 t6 = gm_f_to_decimal_8bshn5b((void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l0)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_b16_8 t7 = GM_LD(gm_b16_8, ((uint8_t *)&t6));
            uint8_t t8 = 0;
            F.ret = t7;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        uint64_t t9 = gm_f_bits_lr3of4i(&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l0)));
        gm_t4ce0jd3tdro4 t10;
        GM_ST(uint64_t, ((uint8_t *)&t10 + 0), t9);
        gm_tabb710d9mq2e t11;
        gm_f_format_5k1v0hr((void *)&t11, (void *)NULL, cx, NULL, t10);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t11))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t11) + 8, gm_src7h8dni243k2v, 68, 12);
        gm_b16_8 t12 = GM_LD(gm_b16_8, (((uint8_t *)&t11) + 8));
        gm_b16_8 t13 = GM_LD(gm_b16_8, ((uint8_t *)&t12));
        uint8_t t14 = 0;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_rational_00mlc2h(void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_show_rational_00mlc2h F;
    GM_STACK_CHECK(&F, 171u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        uint8_t t3 = gm_f_is_integer_ab2ohk2(&(*(gm_tngtuiuskn938 *)((uint8_t *)a0)));
        if (!t3) goto ie2;
        {
            gm_b16_8 t5 = gm_f_show_int_ub6npub((void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)(((uint8_t *)a0) + 0)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t6 = 0;
            F.ret = t5;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        gm_t6j6smj7qoctu t7;
        gm_b16_8 t8 = gm_f_show_int_ub6npub((void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)(((uint8_t *)a0) + 0)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_b16_8, (uint8_t *)&t7 + 0, t8);
        gm_b16_8 t9 = gm_f_show_int_ub6npub((void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)(((uint8_t *)a0) + 48)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_b16_8, (uint8_t *)&t7 + 16, t9);
        gm_tabb710d9mq2e t10;
        gm_f_format_d801g52((void *)&t10, (void *)NULL, cx, NULL, &t7);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t10))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t10) + 8, gm_src7h8dni243k2v, 223, 12);
        gm_b16_8 t11 = GM_LD(gm_b16_8, (((uint8_t *)&t10) + 8));
        gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
        uint8_t t13 = 0;
        F.ret = t12;
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
__attribute__((aligned(4))) void gm_f_try_untyped_fj0h8i5(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2) {
    struct gm_fr_try_untyped_fj0h8i5 F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* module */
    void * gm_l2; /* e */
    void * gm_l3; /* lit */
    void * gm_l4; /* lit */
    void * gm_l6; /* inner */
    gm_t08olq97rscn3 gm_l7; /* id */
    uint64_t gm_l10; /* d */
    void * gm_l12; /* base */
    gm_t08olq97rscn3 gm_l13; /* name */
    uint64_t gm_l14; /* m */
    uint64_t gm_l15; /* d */
    void * gm_l17; /* operand */
    gm_b16_8 gm_l21; /* m */
    gm_ts6oqppjh0ba3 gm_l22; /* op */
    void * gm_l23; /* lhs */
    void * gm_l24; /* rhs */
    gm_t9qkh0rsooc8d gm_l25; /* f */
    gm_t9qkh0rsooc8d gm_l26; /* f */
    gm_t9qkh0rsooc8d gm_l27; /* f */
    gm_t9qkh0rsooc8d gm_l28; /* f */
    gm_b16_8 gm_l34; /* m */
    GM_STACK_CHECK(&F, 12122u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l2;
        uint8_t t13 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        switch (t13) { case ((uint8_t)0ULL): goto ma4; case ((uint8_t)1ULL): goto ma5; case ((uint8_t)2ULL): goto ma11; case ((uint8_t)3ULL): goto ma11; case ((uint8_t)4ULL): goto ma11; case ((uint8_t)5ULL): goto ma11; case ((uint8_t)6ULL): goto ma7; case ((uint8_t)7ULL): goto ma11; case ((uint8_t)8ULL): goto ma11; case ((uint8_t)9ULL): goto ma6; case ((uint8_t)10ULL): goto ma11; case ((uint8_t)11ULL): goto ma11; case ((uint8_t)12ULL): goto ma9; case ((uint8_t)13ULL): goto ma10; case ((uint8_t)14ULL): goto ma11; case ((uint8_t)15ULL): goto ma11; case ((uint8_t)16ULL): goto ma11; case ((uint8_t)17ULL): goto ma11; case ((uint8_t)18ULL): goto ma11; case ((uint8_t)19ULL): goto ma11; case ((uint8_t)20ULL): goto ma11; case ((uint8_t)21ULL): goto ma11; case ((uint8_t)22ULL): goto ma8; case ((uint8_t)23ULL): goto ma11; case ((uint8_t)24ULL): goto ma11; case ((uint8_t)25ULL): goto ma11; case ((uint8_t)26ULL): goto ma11; case ((uint8_t)27ULL): goto ma11; case ((uint8_t)28ULL): goto ma11; case ((uint8_t)29ULL): goto ma11; case ((uint8_t)30ULL): goto ma11; case ((uint8_t)31ULL): goto ma11; case ((uint8_t)32ULL): goto ma11; case ((uint8_t)33ULL): goto ma11; case ((uint8_t)34ULL): goto ma11; case ((uint8_t)35ULL): goto ma11; case ((uint8_t)36ULL): goto ma11; case ((uint8_t)37ULL): goto ma11; case ((uint8_t)38ULL): goto ma11; case ((uint8_t)39ULL): goto ma11; case ((uint8_t)40ULL): goto ma11; case ((uint8_t)41ULL): goto ma11; case ((uint8_t)42ULL): goto ma11; case ((uint8_t)43ULL): goto ma11; case ((uint8_t)44ULL): goto ma11; case ((uint8_t)45ULL): goto ma11; case ((uint8_t)46ULL): goto ma11; case ((uint8_t)47ULL): goto ma11; case ((uint8_t)48ULL): goto ma11; case ((uint8_t)49ULL): goto ma11; case ((uint8_t)50ULL): goto ma11; default: goto ma11; }
ma4:;
        uint8_t t15 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t15 != ((uint8_t)0ULL)) goto mn14;
        void * t16 = GM_LD(void *, ((((uint8_t *)t2) + 8) + 8));
        gm_l3 = t16;
        {
            void * t19 = gm_l3;
            uint8_t t20;
            if ((((((uint8_t *)t19) + 48))[8] != 0)) goto mn22;
            t20 = ((uint8_t)0);
            goto me21;
mn22:;
            t20 = ((uint8_t)1);
            goto me21;
mn23:;
            gm_trap("no match arm matched the scrutinee", gm_src7h8dni243k2v, 233, 16);
me21:;
            if (!t20) goto ie18;
            {
                gm_tkv1vap90vht2 t25;
                memset(&t25, 0, sizeof t25);
                uint8_t t26 = 0;
                F.ret = t25;
                goto gm_fret;
            }
            goto ix18;
ie18:;
ix18:;
        }
        {
            void * t27 = gm_l3;
            gm_tbci5ospk5nnn t28 = GM_LD(gm_tbci5ospk5nnn, (((uint8_t *)t27) + 0));
            gm_t0ialelq3ql1k t29;
            memset(&t29, 0, sizeof t29);
            GM_ST(uint8_t, ((uint8_t *)&t29 + 0), ((uint8_t)0ULL));
            GM_ST(gm_tbci5ospk5nnn, ((uint8_t *)&t29 + 8), t28);
            gm_t5vbkdq81qfs7 t30;
            GM_ST(uint8_t, (uint8_t *)&t30 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t30 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t30 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t30 + 0), ((uint8_t)0ULL));
            GM_ST(gm_t0ialelq3ql1k, ((uint8_t *)&t30 + 8), t29);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tkv1vap90vht2 *)gm_out) + 209, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tkv1vap90vht2 *)gm_out) + 210, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tkv1vap90vht2 *)gm_out) + 212, 0);
            ((uint8_t *)&(*(gm_tkv1vap90vht2 *)gm_out))[208] = 1;
            GM_ST(gm_t5vbkdq81qfs7, (uint8_t *)&(*(gm_tkv1vap90vht2 *)gm_out) + 0, t30);
            goto gm_fdone;
        }
        goto me3;
mn14:;
ma5:;
        uint8_t t32 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t32 != ((uint8_t)1ULL)) goto mn31;
        void * t33 = GM_LD(void *, ((((uint8_t *)t2) + 8) + 8));
        gm_l4 = t33;
        {
            void * t36 = gm_l4;
            uint8_t t37;
            if ((((((uint8_t *)t36) + 96))[1] != 0)) goto mn39;
            t37 = ((uint8_t)0);
            goto me38;
mn39:;
            t37 = ((uint8_t)1);
            goto me38;
mn40:;
            gm_trap("no match arm matched the scrutinee", gm_src7h8dni243k2v, 237, 16);
me38:;
            if (!t37) goto ie35;
            {
                gm_tkv1vap90vht2 t42;
                memset(&t42, 0, sizeof t42);
                uint8_t t43 = 0;
                F.ret = t42;
                goto gm_fret;
            }
            goto ix35;
ie35:;
ix35:;
        }
        {
            void * t44 = gm_l4;
            gm_tngtuiuskn938 t45 = GM_LD(gm_tngtuiuskn938, (((uint8_t *)t44) + 0));
            F.l5 = t45;
        }
        {
            void * t47 = gm_l4;
            uint8_t t48 = GM_LD(uint8_t, (((uint8_t *)t47) + 98));
            if (!t48) goto ie46;
            {
                gm_tu15p0hi2qlv6 t50;
                gm_f_zero_9r6o7sj((void *)&(*(gm_tngtuiuskn938 *)((uint8_t *)&t50 + 0)), (void *)NULL, cx, NULL);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                memcpy((uint8_t *)&t50 + 96, ((uint8_t *)&F.l5), 96);
                gm_t0ialelq3ql1k t51;
                GM_ST(uint8_t, (uint8_t *)&t51 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t51 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t51 + 4, 0);
                GM_ST(uint8_t, ((uint8_t *)&t51 + 0), ((uint8_t)4ULL));
                GM_ST(gm_tu15p0hi2qlv6, ((uint8_t *)&t51 + 8), t50);
                gm_t5vbkdq81qfs7 t52;
                GM_ST(uint8_t, (uint8_t *)&t52 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t52 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t52 + 4, 0);
                GM_ST(uint8_t, ((uint8_t *)&t52 + 0), ((uint8_t)0ULL));
                GM_ST(gm_t0ialelq3ql1k, ((uint8_t *)&t52 + 8), t51);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tkv1vap90vht2 *)gm_out) + 209, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_tkv1vap90vht2 *)gm_out) + 210, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tkv1vap90vht2 *)gm_out) + 212, 0);
                ((uint8_t *)&(*(gm_tkv1vap90vht2 *)gm_out))[208] = 1;
                GM_ST(gm_t5vbkdq81qfs7, (uint8_t *)&(*(gm_tkv1vap90vht2 *)gm_out) + 0, t52);
                goto gm_fdone;
            }
            goto ix46;
ie46:;
ix46:;
        }
        {
            gm_tngtuiuskn938 t53 = F.l5;
            gm_t0ialelq3ql1k t54;
            memset(&t54, 0, sizeof t54);
            GM_ST(uint8_t, ((uint8_t *)&t54 + 0), ((uint8_t)1ULL));
            GM_ST(gm_tngtuiuskn938, ((uint8_t *)&t54 + 8), t53);
            gm_t5vbkdq81qfs7 t55;
            GM_ST(uint8_t, (uint8_t *)&t55 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t55 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t55 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t55 + 0), ((uint8_t)0ULL));
            GM_ST(gm_t0ialelq3ql1k, ((uint8_t *)&t55 + 8), t54);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tkv1vap90vht2 *)gm_out) + 209, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tkv1vap90vht2 *)gm_out) + 210, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tkv1vap90vht2 *)gm_out) + 212, 0);
            ((uint8_t *)&(*(gm_tkv1vap90vht2 *)gm_out))[208] = 1;
            GM_ST(gm_t5vbkdq81qfs7, (uint8_t *)&(*(gm_tkv1vap90vht2 *)gm_out) + 0, t55);
            goto gm_fdone;
        }
        goto me3;
mn31:;
ma6:;
        uint8_t t57 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t57 != ((uint8_t)9ULL)) goto mn56;
        void * t58 = GM_LD(void *, ((((uint8_t *)t2) + 8) + 8));
        gm_l6 = t58;
        void * t59 = gm_l0;
        uint64_t t60 = gm_l1;
        void * t61 = gm_l6;
        gm_f_try_untyped_fj0h8i5((void *)&(*(gm_tkv1vap90vht2 *)gm_out), (void *)NULL, cx, NULL, t59, t60, t61);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto gm_fdone;
        goto me3;
mn56:;
ma7:;
        uint8_t t63 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t63 != ((uint8_t)6ULL)) goto mn62;
        gm_t08olq97rscn3 t64 = GM_LD(gm_t08olq97rscn3, ((((uint8_t *)t2) + 8) + 8));
        gm_l7 = t64;
        {
            void * t67 = gm_l0;
            gm_b16_8 t68 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l7) + 0));
            gm_tk1gen3hgkm51 t69;
            gm_f_local_const_idpokjh((void *)&t69, t67, t68);
            if (!((((uint8_t *)&t69))[224] != 0)) goto ie66;
            F.l8 = (*(gm_tb07pp6njg0bk *)(((uint8_t *)&t69) + 0));
            {
                uint8_t t72 = GM_LD(uint8_t, ((((uint8_t *)&F.l8) + 16) + 0));
                if (t72 != ((uint8_t)0ULL)) goto ie71;
                gm_t0ialelq3ql1k t73 = GM_LD(gm_t0ialelq3ql1k, ((((uint8_t *)&F.l8) + 16) + 8));
                F.l9 = t73;
                {
                    gm_t0ialelq3ql1k t75 = F.l9;
                    gm_t5vbkdq81qfs7 t76;
                    GM_ST(uint8_t, (uint8_t *)&t76 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t76 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t76 + 4, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t76 + 0), ((uint8_t)0ULL));
                    GM_ST(gm_t0ialelq3ql1k, ((uint8_t *)&t76 + 8), t75);
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tkv1vap90vht2 *)gm_out) + 209, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tkv1vap90vht2 *)gm_out) + 210, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tkv1vap90vht2 *)gm_out) + 212, 0);
                    ((uint8_t *)&(*(gm_tkv1vap90vht2 *)gm_out))[208] = 1;
                    GM_ST(gm_t5vbkdq81qfs7, (uint8_t *)&(*(gm_tkv1vap90vht2 *)gm_out) + 0, t76);
                    goto gm_fdone;
                }
                goto ix71;
ie71:;
                {
                    gm_tkv1vap90vht2 t78;
                    memset(&t78, 0, sizeof t78);
                    uint8_t t79 = 0;
                    F.ret = t78;
                    goto gm_fret;
                }
ix71:;
            }
            goto ix66;
ie66:;
ix66:;
        }
        {
            void * t80 = gm_l0;
            uint64_t t81 = gm_l1;
            gm_b16_8 t82 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l7) + 0));
            gm_tf5srh4mp4b3i t83 = gm_f_lookup_module_name_cukfp7t((void *)NULL, cx, NULL, t80, t81, t82);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint64_t t84;
            if (((((uint8_t *)&t83))[8] != 0)) {
                uint64_t t85 = GM_LD(uint64_t, (((uint8_t *)&t83) + 0));
                t84 = t85;
            } else {
                gm_tkv1vap90vht2 t86;
                memset(&t86, 0, sizeof t86);
                uint8_t t87 = 0;
                F.ret = t86;
                goto gm_fret;
            }
            gm_l10 = t84;
        }
        {
            void * t89 = gm_l0;
            uint64_t t90 = gm_l10;
            gm_t6qooom8qa2do t91;
            gm_f_resolve_decl_pus042v((void *)&t91, (void *)NULL, cx, NULL, t89, t90);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t92 = GM_LD(uint8_t, (((uint8_t *)&t91) + 0));
            if (t92 != ((uint8_t)8ULL)) goto ie88;
            F.l11 = (*(gm_t0ialelq3ql1k *)(((uint8_t *)&t91) + 8));
            {
                gm_t0ialelq3ql1k t94 = F.l11;
                gm_t5vbkdq81qfs7 t95;
                GM_ST(uint8_t, (uint8_t *)&t95 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t95 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t95 + 4, 0);
                GM_ST(uint8_t, ((uint8_t *)&t95 + 0), ((uint8_t)0ULL));
                GM_ST(gm_t0ialelq3ql1k, ((uint8_t *)&t95 + 8), t94);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tkv1vap90vht2 *)gm_out) + 209, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_tkv1vap90vht2 *)gm_out) + 210, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tkv1vap90vht2 *)gm_out) + 212, 0);
                ((uint8_t *)&(*(gm_tkv1vap90vht2 *)gm_out))[208] = 1;
                GM_ST(gm_t5vbkdq81qfs7, (uint8_t *)&(*(gm_tkv1vap90vht2 *)gm_out) + 0, t95);
                goto gm_fdone;
            }
            goto ix88;
ie88:;
            {
                gm_tkv1vap90vht2 t97;
                memset(&t97, 0, sizeof t97);
                uint8_t t98 = 0;
                F.ret = t97;
                goto gm_fret;
            }
ix88:;
        }
        goto me3;
mn62:;
ma8:;
        uint8_t t100 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t100 != ((uint8_t)22ULL)) goto mn99;
        void * t101 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 0));
        gm_l12 = t101;
        gm_t08olq97rscn3 t102 = GM_LD(gm_t08olq97rscn3, (((((uint8_t *)t2) + 8) + 8) + 8));
        gm_l13 = t102;
        {
            void * t104 = gm_l0;
            uint64_t t105 = gm_l1;
            void * t106 = gm_l12;
            gm_tf5srh4mp4b3i t107 = gm_f_module_named_n1mcl3b((void *)NULL, cx, NULL, t104, t105, t106);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint64_t t108;
            if (((((uint8_t *)&t107))[8] != 0)) {
                uint64_t t109 = GM_LD(uint64_t, (((uint8_t *)&t107) + 0));
                t108 = t109;
            } else {
                gm_tkv1vap90vht2 t110;
                memset(&t110, 0, sizeof t110);
                uint8_t t111 = 0;
                F.ret = t110;
                goto gm_fret;
            }
            gm_l14 = t108;
        }
        {
            void * t112 = gm_l0;
            void * t113 = GM_LD(void *, (((uint8_t *)t112) + 0));
            uint64_t t114 = gm_l14;
            gm_b16_8 t115 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l13) + 0));
            gm_tf5srh4mp4b3i t116 = gm_f_lookup_59juqoo(t113, t114, t115);
            uint64_t t117;
            if (((((uint8_t *)&t116))[8] != 0)) {
                uint64_t t118 = GM_LD(uint64_t, (((uint8_t *)&t116) + 0));
                t117 = t118;
            } else {
                gm_tkv1vap90vht2 t119;
                memset(&t119, 0, sizeof t119);
                uint8_t t120 = 0;
                F.ret = t119;
                goto gm_fret;
            }
            gm_l15 = t117;
        }
        {
            void * t122 = gm_l0;
            uint64_t t123 = gm_l15;
            uint64_t t124 = gm_l1;
            uint8_t t125 = gm_f_decl_visible_bd168ti(t122, t123, t124);
            uint8_t t126 = (uint8_t)!t125;
            if (!t126) goto ie121;
            {
                gm_tkv1vap90vht2 t128;
                memset(&t128, 0, sizeof t128);
                uint8_t t129 = 0;
                F.ret = t128;
                goto gm_fret;
            }
            goto ix121;
ie121:;
ix121:;
        }
        {
            void * t130 = gm_l0;
            uint64_t t131 = gm_l15;
            uint64_t t132 = gm_l1;
            gm_b0_1 t133 = gm_f_note_use_from_2lmujsq((void *)NULL, cx, NULL, t130, t131, t132);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t135 = gm_l0;
            uint64_t t136 = gm_l15;
            gm_t6qooom8qa2do t137;
            gm_f_resolve_decl_pus042v((void *)&t137, (void *)NULL, cx, NULL, t135, t136);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t138 = GM_LD(uint8_t, (((uint8_t *)&t137) + 0));
            if (t138 != ((uint8_t)8ULL)) goto ie134;
            F.l16 = (*(gm_t0ialelq3ql1k *)(((uint8_t *)&t137) + 8));
            {
                gm_t0ialelq3ql1k t140 = F.l16;
                gm_t5vbkdq81qfs7 t141;
                GM_ST(uint8_t, (uint8_t *)&t141 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t141 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t141 + 4, 0);
                GM_ST(uint8_t, ((uint8_t *)&t141 + 0), ((uint8_t)0ULL));
                GM_ST(gm_t0ialelq3ql1k, ((uint8_t *)&t141 + 8), t140);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tkv1vap90vht2 *)gm_out) + 209, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_tkv1vap90vht2 *)gm_out) + 210, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tkv1vap90vht2 *)gm_out) + 212, 0);
                ((uint8_t *)&(*(gm_tkv1vap90vht2 *)gm_out))[208] = 1;
                GM_ST(gm_t5vbkdq81qfs7, (uint8_t *)&(*(gm_tkv1vap90vht2 *)gm_out) + 0, t141);
                goto gm_fdone;
            }
            goto ix134;
ie134:;
            {
                gm_tkv1vap90vht2 t143;
                memset(&t143, 0, sizeof t143);
                uint8_t t144 = 0;
                F.ret = t143;
                goto gm_fret;
            }
ix134:;
        }
        goto me3;
mn99:;
ma9:;
        uint8_t t146 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t146 != ((uint8_t)12ULL)) goto mn145;
        uint8_t t147 = GM_LD(uint8_t, ((((((uint8_t *)t2) + 8) + 8) + 0) + 0));
        if (t147 != ((uint8_t)0ULL)) goto mn145;
        void * t148 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 8));
        gm_l17 = t148;
        {
            void * t150 = gm_l0;
            uint64_t t151 = gm_l1;
            void * t152 = gm_l17;
            gm_tkv1vap90vht2 t153;
            gm_f_try_untyped_fj0h8i5((void *)&t153, (void *)NULL, cx, NULL, t150, t151, t152);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t5vbkdq81qfs7 t154;
            if (((((uint8_t *)&t153))[208] != 0)) {
                t154 = (*(gm_t5vbkdq81qfs7 *)(((uint8_t *)&t153) + 0));
            } else {
                gm_tkv1vap90vht2 t155;
                memset(&t155, 0, sizeof t155);
                uint8_t t156 = 0;
                F.ret = t155;
                goto gm_fret;
            }
            F.l18 = t154;
        }
        {
            uint8_t t158 = GM_LD(uint8_t, (((uint8_t *)&F.l18) + 0));
            if (t158 != ((uint8_t)0ULL)) goto ie157;
            gm_t0ialelq3ql1k t159 = GM_LD(gm_t0ialelq3ql1k, (((uint8_t *)&F.l18) + 8));
            F.l19 = t159;
            {
                void * t161 = (void *)((uint8_t *)&F.l19);
                gm_t4t568o7qnv02 t162;
                gm_f_negate_fohael7((void *)&t162, (void *)NULL, cx, NULL, t161);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t165 = GM_LD(uint8_t, (((uint8_t *)&t162) + 0));
                if (t165 != ((uint8_t)0ULL)) goto mn164;
                F.l20 = (*(gm_t0ialelq3ql1k *)(((uint8_t *)&t162) + 8));
                gm_t0ialelq3ql1k t166 = F.l20;
                gm_t5vbkdq81qfs7 t167;
                GM_ST(uint8_t, (uint8_t *)&t167 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t167 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t167 + 4, 0);
                GM_ST(uint8_t, ((uint8_t *)&t167 + 0), ((uint8_t)0ULL));
                GM_ST(gm_t0ialelq3ql1k, ((uint8_t *)&t167 + 8), t166);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tkv1vap90vht2 *)gm_out) + 209, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_tkv1vap90vht2 *)gm_out) + 210, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tkv1vap90vht2 *)gm_out) + 212, 0);
                ((uint8_t *)&(*(gm_tkv1vap90vht2 *)gm_out))[208] = 1;
                GM_ST(gm_t5vbkdq81qfs7, (uint8_t *)&(*(gm_tkv1vap90vht2 *)gm_out) + 0, t167);
                goto gm_fdone;
                goto me163;
mn164:;
                uint8_t t169 = GM_LD(uint8_t, (((uint8_t *)&t162) + 0));
                if (t169 != ((uint8_t)1ULL)) goto mn168;
                gm_b16_8 t170 = GM_LD(gm_b16_8, (((uint8_t *)&t162) + 8));
                gm_l21 = t170;
                {
                    void * t173 = gm_l0;
                    uint8_t t174 = GM_LD(uint8_t, (((uint8_t *)t173) + 2536));
                    uint8_t t175 = (uint8_t)!t174;
                    if (!t175) goto ie172;
                    {
                        void * t177 = gm_l0;
                        void * t178 = gm_l2;
                        gm_tl874t2atnlh3 t179 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t178) + 72));
                        gm_b16_8 t180 = gm_l21;
                        gm_b0_1 t181 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t177, t179, t180);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    goto ix172;
ie172:;
ix172:;
                }
                {
                    gm_t5vbkdq81qfs7 t182;
                    memset(&t182, 0, sizeof t182);
                    GM_ST(uint8_t, ((uint8_t *)&t182 + 0), ((uint8_t)1ULL));
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tkv1vap90vht2 *)gm_out) + 209, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tkv1vap90vht2 *)gm_out) + 210, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tkv1vap90vht2 *)gm_out) + 212, 0);
                    ((uint8_t *)&(*(gm_tkv1vap90vht2 *)gm_out))[208] = 1;
                    GM_ST(gm_t5vbkdq81qfs7, (uint8_t *)&(*(gm_tkv1vap90vht2 *)gm_out) + 0, t182);
                    goto gm_fdone;
                }
                goto me163;
mn168:;
                gm_trap("no match arm matched the scrutinee", gm_src7h8dni243k2v, 264, 13);
me163:;
            }
            goto ix157;
ie157:;
            {
                gm_t5vbkdq81qfs7 t184;
                memset(&t184, 0, sizeof t184);
                GM_ST(uint8_t, ((uint8_t *)&t184 + 0), ((uint8_t)1ULL));
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tkv1vap90vht2 *)gm_out) + 209, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_tkv1vap90vht2 *)gm_out) + 210, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tkv1vap90vht2 *)gm_out) + 212, 0);
                ((uint8_t *)&(*(gm_tkv1vap90vht2 *)gm_out))[208] = 1;
                GM_ST(gm_t5vbkdq81qfs7, (uint8_t *)&(*(gm_tkv1vap90vht2 *)gm_out) + 0, t184);
                goto gm_fdone;
            }
ix157:;
        }
        goto me3;
mn145:;
ma10:;
        uint8_t t186 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t186 != ((uint8_t)13ULL)) goto mn185;
        gm_ts6oqppjh0ba3 t187 = GM_LD(gm_ts6oqppjh0ba3, (((((uint8_t *)t2) + 8) + 8) + 0));
        gm_l22 = t187;
        void * t188 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 8));
        gm_l23 = t188;
        void * t189 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 16));
        gm_l24 = t189;
        {
            gm_ts6oqppjh0ba3 t192 = gm_l22;
            gm_ts6oqppjh0ba3 t193;
            GM_ST(uint8_t, (uint8_t *)&t193 + 1, 0);
            GM_ST(uint8_t, ((uint8_t *)&t193 + 0), ((uint8_t)20ULL));
            gm_ts6oqppjh0ba3 t194;
            GM_ST(uint8_t, (uint8_t *)&t194 + 1, 0);
            GM_ST(uint8_t, ((uint8_t *)&t194 + 0), ((uint8_t)21ULL));
            gm_ts6oqppjh0ba3 t195;
            GM_ST(uint8_t, (uint8_t *)&t195 + 1, 0);
            GM_ST(uint8_t, ((uint8_t *)&t195 + 0), ((uint8_t)14ULL));
            gm_ts6oqppjh0ba3 t196;
            GM_ST(uint8_t, (uint8_t *)&t196 + 1, 0);
            GM_ST(uint8_t, ((uint8_t *)&t196 + 0), ((uint8_t)15ULL));
            gm_ts6oqppjh0ba3 t197;
            GM_ST(uint8_t, (uint8_t *)&t197 + 1, 0);
            GM_ST(uint8_t, ((uint8_t *)&t197 + 0), ((uint8_t)10ULL));
            gm_ts6oqppjh0ba3 t198;
            GM_ST(uint8_t, (uint8_t *)&t198 + 1, 0);
            GM_ST(uint8_t, ((uint8_t *)&t198 + 0), ((uint8_t)11ULL));
            gm_tvqq0vnt1mu0q t199;
            GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t199 + 0), t193);
            GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t199 + 2), t194);
            GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t199 + 4), t195);
            GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t199 + 6), t196);
            GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t199 + 8), t197);
            GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t199 + 10), t198);
            uint8_t t200 = 0;
            if (gm_eqbqb4gb1d92(((uint8_t *)&t199) + 0, ((uint8_t *)&t192))) t200 = 1;
            if (gm_eqbqb4gb1d92(((uint8_t *)&t199) + 2, ((uint8_t *)&t192))) t200 = 1;
            if (gm_eqbqb4gb1d92(((uint8_t *)&t199) + 4, ((uint8_t *)&t192))) t200 = 1;
            if (gm_eqbqb4gb1d92(((uint8_t *)&t199) + 6, ((uint8_t *)&t192))) t200 = 1;
            if (gm_eqbqb4gb1d92(((uint8_t *)&t199) + 8, ((uint8_t *)&t192))) t200 = 1;
            if (gm_eqbqb4gb1d92(((uint8_t *)&t199) + 10, ((uint8_t *)&t192))) t200 = 1;
            uint8_t t201 = (uint8_t)(t200);
            if (!t201) goto ie191;
            {
                gm_tkv1vap90vht2 t203;
                memset(&t203, 0, sizeof t203);
                uint8_t t204 = 0;
                F.ret = t203;
                goto gm_fret;
            }
            goto ix191;
ie191:;
ix191:;
        }
        {
            uint8_t t209 = GM_LD(uint8_t, (((uint8_t *)&gm_l22) + 0));
            if (t209 != ((uint8_t)0ULL)) goto pa208;
            gm_t9qkh0rsooc8d t210 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l22) + 1));
            gm_l25 = t210;
            goto po207;
pa208:;
            uint8_t t212 = GM_LD(uint8_t, (((uint8_t *)&gm_l22) + 0));
            if (t212 != ((uint8_t)1ULL)) goto pa211;
            gm_t9qkh0rsooc8d t213 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l22) + 1));
            gm_l25 = t213;
            goto po207;
pa211:;
            uint8_t t215 = GM_LD(uint8_t, (((uint8_t *)&gm_l22) + 0));
            if (t215 != ((uint8_t)2ULL)) goto pa214;
            gm_t9qkh0rsooc8d t216 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l22) + 1));
            gm_l25 = t216;
            goto po207;
pa214:;
            uint8_t t218 = GM_LD(uint8_t, (((uint8_t *)&gm_l22) + 0));
            if (t218 != ((uint8_t)3ULL)) goto pa217;
            gm_t9qkh0rsooc8d t219 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l22) + 1));
            gm_l25 = t219;
            goto po207;
pa217:;
            goto mn206;
po207:;
            uint8_t t221 = (!(GM_LD(uint8_t, ((uint8_t *)&gm_l25) + 0) == ((uint8_t)0ULL)));
            if (!t221) goto ie220;
            {
                gm_tkv1vap90vht2 t223;
                memset(&t223, 0, sizeof t223);
                uint8_t t224 = 0;
                F.ret = t223;
                goto gm_fret;
            }
            goto ix220;
ie220:;
ix220:;
            goto me205;
mn206:;
            goto me205;
mn225:;
            gm_trap("no match arm matched the scrutinee", gm_src7h8dni243k2v, 275, 13);
me205:;
        }
        {
            void * t227 = gm_l0;
            uint64_t t228 = gm_l1;
            void * t229 = gm_l23;
            gm_tkv1vap90vht2 t230;
            gm_f_try_untyped_fj0h8i5((void *)&t230, (void *)NULL, cx, NULL, t227, t228, t229);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t5vbkdq81qfs7 t231;
            if (((((uint8_t *)&t230))[208] != 0)) {
                t231 = (*(gm_t5vbkdq81qfs7 *)(((uint8_t *)&t230) + 0));
            } else {
                gm_tkv1vap90vht2 t232;
                memset(&t232, 0, sizeof t232);
                uint8_t t233 = 0;
                F.ret = t232;
                goto gm_fret;
            }
            F.l29 = t231;
        }
        {
            void * t234 = gm_l0;
            uint64_t t235 = gm_l1;
            void * t236 = gm_l24;
            gm_tkv1vap90vht2 t237;
            gm_f_try_untyped_fj0h8i5((void *)&t237, (void *)NULL, cx, NULL, t234, t235, t236);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t5vbkdq81qfs7 t238;
            if (((((uint8_t *)&t237))[208] != 0)) {
                t238 = (*(gm_t5vbkdq81qfs7 *)(((uint8_t *)&t237) + 0));
            } else {
                gm_tkv1vap90vht2 t239;
                memset(&t239, 0, sizeof t239);
                uint8_t t240 = 0;
                F.ret = t239;
                goto gm_fret;
            }
            F.l30 = t238;
        }
        {
            uint8_t t242 = GM_LD(uint8_t, (((uint8_t *)&F.l29) + 0));
            if (t242 != ((uint8_t)0ULL)) goto ie241;
            gm_t0ialelq3ql1k t243 = GM_LD(gm_t0ialelq3ql1k, (((uint8_t *)&F.l29) + 8));
            F.l31 = t243;
            {
                uint8_t t246 = GM_LD(uint8_t, (((uint8_t *)&F.l30) + 0));
                if (t246 != ((uint8_t)0ULL)) goto ie245;
                gm_t0ialelq3ql1k t247 = GM_LD(gm_t0ialelq3ql1k, (((uint8_t *)&F.l30) + 8));
                F.l32 = t247;
                {
                    gm_ts6oqppjh0ba3 t249 = gm_l22;
                    void * t250 = (void *)((uint8_t *)&F.l31);
                    void * t251 = (void *)((uint8_t *)&F.l32);
                    gm_t4t568o7qnv02 t252;
                    gm_f_binary_9sb7899((void *)&t252, (void *)NULL, cx, NULL, t249, t250, t251);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t255 = GM_LD(uint8_t, (((uint8_t *)&t252) + 0));
                    if (t255 != ((uint8_t)0ULL)) goto mn254;
                    F.l33 = (*(gm_t0ialelq3ql1k *)(((uint8_t *)&t252) + 8));
                    gm_t0ialelq3ql1k t256 = F.l33;
                    gm_t5vbkdq81qfs7 t257;
                    GM_ST(uint8_t, (uint8_t *)&t257 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t257 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t257 + 4, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t257 + 0), ((uint8_t)0ULL));
                    GM_ST(gm_t0ialelq3ql1k, ((uint8_t *)&t257 + 8), t256);
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tkv1vap90vht2 *)gm_out) + 209, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tkv1vap90vht2 *)gm_out) + 210, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tkv1vap90vht2 *)gm_out) + 212, 0);
                    ((uint8_t *)&(*(gm_tkv1vap90vht2 *)gm_out))[208] = 1;
                    GM_ST(gm_t5vbkdq81qfs7, (uint8_t *)&(*(gm_tkv1vap90vht2 *)gm_out) + 0, t257);
                    goto gm_fdone;
                    goto me253;
mn254:;
                    uint8_t t259 = GM_LD(uint8_t, (((uint8_t *)&t252) + 0));
                    if (t259 != ((uint8_t)1ULL)) goto mn258;
                    gm_b16_8 t260 = GM_LD(gm_b16_8, (((uint8_t *)&t252) + 8));
                    gm_l34 = t260;
                    {
                        void * t263 = gm_l0;
                        uint8_t t264 = GM_LD(uint8_t, (((uint8_t *)t263) + 2536));
                        uint8_t t265 = (uint8_t)!t264;
                        if (!t265) goto ie262;
                        {
                            void * t267 = gm_l0;
                            void * t268 = gm_l2;
                            gm_tl874t2atnlh3 t269 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t268) + 72));
                            gm_b16_8 t270 = gm_l34;
                            gm_b0_1 t271 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t267, t269, t270);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                        }
                        goto ix262;
ie262:;
ix262:;
                    }
                    {
                        gm_t5vbkdq81qfs7 t272;
                        memset(&t272, 0, sizeof t272);
                        GM_ST(uint8_t, ((uint8_t *)&t272 + 0), ((uint8_t)1ULL));
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tkv1vap90vht2 *)gm_out) + 209, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tkv1vap90vht2 *)gm_out) + 210, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tkv1vap90vht2 *)gm_out) + 212, 0);
                        ((uint8_t *)&(*(gm_tkv1vap90vht2 *)gm_out))[208] = 1;
                        GM_ST(gm_t5vbkdq81qfs7, (uint8_t *)&(*(gm_tkv1vap90vht2 *)gm_out) + 0, t272);
                        goto gm_fdone;
                    }
                    goto me253;
mn258:;
                    gm_trap("no match arm matched the scrutinee", gm_src7h8dni243k2v, 283, 13);
me253:;
                }
                goto ix245;
ie245:;
                {
                    gm_t5vbkdq81qfs7 t274;
                    memset(&t274, 0, sizeof t274);
                    GM_ST(uint8_t, ((uint8_t *)&t274 + 0), ((uint8_t)1ULL));
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tkv1vap90vht2 *)gm_out) + 209, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tkv1vap90vht2 *)gm_out) + 210, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tkv1vap90vht2 *)gm_out) + 212, 0);
                    ((uint8_t *)&(*(gm_tkv1vap90vht2 *)gm_out))[208] = 1;
                    GM_ST(gm_t5vbkdq81qfs7, (uint8_t *)&(*(gm_tkv1vap90vht2 *)gm_out) + 0, t274);
                    goto gm_fdone;
                }
ix245:;
            }
            goto ix241;
ie241:;
            {
                gm_t5vbkdq81qfs7 t276;
                memset(&t276, 0, sizeof t276);
                GM_ST(uint8_t, ((uint8_t *)&t276 + 0), ((uint8_t)1ULL));
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tkv1vap90vht2 *)gm_out) + 209, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_tkv1vap90vht2 *)gm_out) + 210, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tkv1vap90vht2 *)gm_out) + 212, 0);
                ((uint8_t *)&(*(gm_tkv1vap90vht2 *)gm_out))[208] = 1;
                GM_ST(gm_t5vbkdq81qfs7, (uint8_t *)&(*(gm_tkv1vap90vht2 *)gm_out) + 0, t276);
                goto gm_fdone;
            }
ix241:;
        }
        goto me3;
mn185:;
ma11:;
        gm_tkv1vap90vht2 t278;
        memset(&t278, 0, sizeof t278);
        uint8_t t279 = 0;
        F.ret = t278;
        goto gm_fret;
        goto me3;
mn277:;
ma12:;
        gm_trap("no match arm matched the scrutinee", gm_src7h8dni243k2v, 231, 5);
me3:;
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
