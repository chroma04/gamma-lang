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
typedef struct { uint64_t f0; uint64_t f1; } gm_t2lqu8jsj511d;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint64_t f0; } gm_t4ce0jd3tdro4;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { double f0; uint8_t f1; uint8_t pe[7]; } gm_th2bqc58ss467;
typedef struct { double f0; } gm_tidu2er0d7h95;
typedef struct { uint64_t f0; uint64_t f1; uint64_t f2; } gm_tkpb4kplftgaa;
_Static_assert(sizeof(gm_t2lqu8jsj511d) == 16 && _Alignof(gm_t2lqu8jsj511d) == 8 && offsetof(gm_t2lqu8jsj511d, f0) == 0 && offsetof(gm_t2lqu8jsj511d, f1) == 8, "layout of `(u64, u64)`");
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
_Static_assert(sizeof(gm_t4ce0jd3tdro4) == 8 && _Alignof(gm_t4ce0jd3tdro4) == 8 && offsetof(gm_t4ce0jd3tdro4, f0) == 0, "layout of `Instant`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
typedef struct { gm_b16_8 f0; double f1; uint64_t f2; uint64_t f3; } gm_tcir0vf4307lp;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
_Static_assert(sizeof(gm_th2bqc58ss467) == 16 && _Alignof(gm_th2bqc58ss467) == 8 && offsetof(gm_th2bqc58ss467, f0) == 0 && offsetof(gm_th2bqc58ss467, f1) == 8, "layout of `?f64`");
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
_Static_assert(sizeof(gm_tidu2er0d7h95) == 8 && _Alignof(gm_tidu2er0d7h95) == 8 && offsetof(gm_tidu2er0d7h95, f0) == 0, "layout of `(f64,)`");
_Static_assert(sizeof(gm_tkpb4kplftgaa) == 24 && _Alignof(gm_tkpb4kplftgaa) == 8 && offsetof(gm_tkpb4kplftgaa, f0) == 0 && offsetof(gm_tkpb4kplftgaa, f1) == 8 && offsetof(gm_tkpb4kplftgaa, f2) == 16, "layout of `(u64, u64, u64)`");
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t4ce0jd3tdro4 v0; } f1; } gm_ttiiv5vhiuvvr;
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
typedef struct { gm_b16_8 f0; gm_th2lqbo77pvsc f1; uint32_t f2; uint8_t pe[4]; } gm_tbah3mk9eqfec;
_Static_assert(sizeof(gm_tcir0vf4307lp) == 40 && _Alignof(gm_tcir0vf4307lp) == 8 && offsetof(gm_tcir0vf4307lp, f0) == 0 && offsetof(gm_tcir0vf4307lp, f1) == 16 && offsetof(gm_tcir0vf4307lp, f2) == 24 && offsetof(gm_tcir0vf4307lp, f3) == 32, "layout of `(string, f64, usize, u64)`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
typedef struct { gm_th2lqbo77pvsc f0; gm_b16_8 f1; uint64_t f2; } gm_tp3p9lj444cp7;
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_ttiiv5vhiuvvr v0; double v1; } f1; } gm_tspt3vn9ljcnd;
_Static_assert(sizeof(gm_ttiiv5vhiuvvr) == 16 && _Alignof(gm_ttiiv5vhiuvvr) == 8 && offsetof(gm_ttiiv5vhiuvvr, f0) == 0 && offsetof(gm_ttiiv5vhiuvvr, f1) == 8, "layout of `ParseError`");
typedef struct { gm_t6j6smj7qoctu f0; uint8_t f1; uint8_t pe[7]; } gm_ttu09lij36f8c;
_Static_assert(sizeof(gm_tbah3mk9eqfec) == 48 && _Alignof(gm_tbah3mk9eqfec) == 8 && offsetof(gm_tbah3mk9eqfec, f0) == 0 && offsetof(gm_tbah3mk9eqfec, f1) == 16 && offsetof(gm_tbah3mk9eqfec, f2) == 40, "layout of `TestCase`");
_Static_assert(sizeof(gm_tp3p9lj444cp7) == 48 && _Alignof(gm_tp3p9lj444cp7) == 8 && offsetof(gm_tp3p9lj444cp7, f0) == 0 && offsetof(gm_tp3p9lj444cp7, f1) == 24 && offsetof(gm_tp3p9lj444cp7, f2) == 40, "layout of `Finished`");
_Static_assert(sizeof(gm_tspt3vn9ljcnd) == 24 && _Alignof(gm_tspt3vn9ljcnd) == 8 && offsetof(gm_tspt3vn9ljcnd, f0) == 0 && offsetof(gm_tspt3vn9ljcnd, f1) == 8, "layout of `ParseError!f64`");
_Static_assert(sizeof(gm_ttu09lij36f8c) == 40 && _Alignof(gm_ttu09lij36f8c) == 8 && offsetof(gm_ttu09lij36f8c, f0) == 0 && offsetof(gm_ttu09lij36f8c, f1) == 32, "layout of `?Split`");
struct gm_fr_abs_n2h5t0g {
    gm_frame h;
    double ret;
};
struct gm_fr_contains_gkki4kq {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_copysign_qgltnuv {
    gm_frame h;
    double ret;
};
struct gm_fr_empty_slice_5g066b3 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_failed_to_run_3e08soa {
    gm_frame h;
    gm_tp3p9lj444cp7 ret;
};
struct gm_fr_finish_2t60fs7 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_fmt_duration_qh0lr6g {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_fmt_per_run_159rum2 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_for_body_hrv26t6 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_for_body_ui39jfe {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_list_3po0a3n {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_parse_number_hb0i8jm {
    gm_frame h;
    gm_th2bqc58ss467 ret;
};
struct gm_fr_record_abrqivi {
    gm_frame h;
    gm_tbah3mk9eqfec l1; /* t */
    gm_tp3p9lj444cp7 l2; /* f */
    gm_b0_1 ret;
    gm_b0_1 w0;
};
struct gm_fr_round_ocas0nv {
    gm_frame h;
    double ret;
};
struct gm_fr_sqrt_mar88n3 {
    gm_frame h;
    double ret;
};
struct gm_fr_summarize_kafmpn5 {
    gm_frame h;
    gm_th2bqc58ss467 l5; /* iters */
    gm_trqfvmjtt4sij l6; /* per_run */
    uint64_t l7; /* samples */
    gm_ttu09lij36f8c ret;
    gm_b0_1 w1;
};
inline __attribute__((aligned(4))) double gm_f_abs_n2h5t0g(double a0);
inline __attribute__((aligned(4))) uint8_t gm_f_contains_gkki4kq(gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) double gm_f_copysign_qgltnuv(double a0, double a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_5g066b3(void);
inline __attribute__((aligned(4))) void gm_f_failed_to_run_3e08soa(void *gm_out, gm_b16_8 a0);
__attribute__((aligned(4))) gm_tp3p9lj444cp7 gm_f_failed_to_run_3e08soa_c(gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_fields_ojgi3et_sfcmm6t(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_find_nab29co(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) uint8_t gm_f_finish_2t60fs7(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_fmt_duration_qh0lr6g(void *env, gm_node *cx, gm_node *inst, uint64_t a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_fmt_per_run_159rum2(void *env, gm_node *cx, gm_node *inst, double a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_hrv26t6(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_for_body_ui39jfe(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_format_2kpt646(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tidu2er0d7h95 a0);
__attribute__((aligned(4))) void gm_f_format_4otle6l(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_729oo46(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t2lqu8jsj511d a0);
__attribute__((aligned(4))) void gm_f_format_88h5fhl(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tidu2er0d7h95 a0);
__attribute__((aligned(4))) void gm_f_format_9t9dukb(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
__attribute__((aligned(4))) void gm_f_format_ahg4b8c(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_bia01cc(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tidu2er0d7h95 a0);
__attribute__((aligned(4))) void gm_f_format_bkdctpk(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_edua06s(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tidu2er0d7h95 a0);
__attribute__((aligned(4))) void gm_f_format_ia4ff4d(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tidu2er0d7h95 a0);
__attribute__((aligned(4))) void gm_f_format_iq1do3n(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_k7lpr9c(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_o5mv7j4(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_tjld97n(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_tttig7s(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tidu2er0d7h95 a0);
__attribute__((aligned(4))) void gm_f_format_v3lvmh8(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t2lqu8jsj511d a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_lines_9uns0k2_scfak7n(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) void gm_f_list_3po0a3n(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_parse_float_17hgksa(void *gm_out, gm_b16_8 a0);
__attribute__((aligned(4))) gm_th2bqc58ss467 gm_f_parse_number_hb0i8jm(gm_b16_8 a0);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_n9ksguc(void * a0, double a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_record_abrqivi(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, const void * a2);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_rfind_ai5s2js(gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) double gm_f_round_ocas0nv(double a0);
inline __attribute__((aligned(4))) double gm_f_sqrt_mar88n3(double a0);
__attribute__((aligned(4))) void gm_f_summarize_kafmpn5(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) double gm_f_trunc_oao3g20(double a0);
GM_NOINLINE bool gm_pf1m4b8q4e78(gm_node *cx, const void *d, uintptr_t n, const char *file, uint32_t line, uint32_t col);
GM_NOINLINE bool gm_pf70j0q3sgt1(gm_node *cx, const gm_type *d, const void *v, const char *file, uint32_t line, uint32_t col);
extern uint8_t gm_c2t15afgpnn[8] __attribute__((aligned(8)));
extern uint8_t gm_c6d5506jf7q[8] __attribute__((aligned(8)));
extern uint8_t gm_co3qlejdntu[16] __attribute__((aligned(8)));
extern void *const gm_hlob66lsuf8h[];
extern void *const gm_hluov08dhrrb[];
extern const uint8_t gm_s3p0rbs9hc3lg[2];
extern const uint8_t gm_s5f956mm1l828[6];
extern const uint8_t gm_s9r64t1166q1f[5];
extern const uint8_t gm_sfdbfrv46lihd[2];
extern const uint8_t gm_so514oj5nbhcu[2];
extern const uint8_t gm_soim4k70n6bc7[3];
extern const uint8_t gm_som0q53jqvjh3[5];
extern const uint8_t gm_sp77et3djoa12[12];
extern const uint8_t gm_sr4qbrk6tlgaa[1];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_src3d0v42kavhv7[];
extern const char gm_src4mnsbede6fos[];
extern const char gm_srcrgc9bkquf6nr[];
extern const uint8_t gm_srfagufvviad7[6];
extern const gm_type gm_ty0iqiubfm7e;
extern const gm_type gm_tybihgc69jv3;
extern const gm_type gm_tyo86ks9qkvt;
inline __attribute__((aligned(4))) double gm_f_abs_n2h5t0g(double a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_abs_n2h5t0g F;
    double gm_l0; /* x */
    GM_STACK_CHECK_SHALLOW(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    double t1 = gm_l0;
    double t2 = t1;
    uint64_t t3;
    memset(&t3, 0, sizeof t3); memcpy(&t3, ((uint8_t *)&t2), sizeof t3 < 8 ? sizeof t3 : 8);
    uint64_t t4 = GM_LD(uint64_t, gm_c2t15afgpnn);
    uint64_t t5 = (uint64_t)(t3 & t4);
    uint64_t t6 = t5;
    double t7;
    memset(&t7, 0, sizeof t7); memcpy(&t7, ((uint8_t *)&t6), sizeof t7 < 8 ? sizeof t7 : 8);
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
inline __attribute__((aligned(4))) double gm_f_copysign_qgltnuv(double a0, double a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_copysign_qgltnuv F;
    double gm_l0; /* x */
    double gm_l1; /* y */
    GM_STACK_CHECK_SHALLOW(&F, 136u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    double t1 = gm_l0;
    double t2 = t1;
    uint64_t t3;
    memset(&t3, 0, sizeof t3); memcpy(&t3, ((uint8_t *)&t2), sizeof t3 < 8 ? sizeof t3 : 8);
    uint64_t t4 = GM_LD(uint64_t, gm_c2t15afgpnn);
    uint64_t t5 = (uint64_t)(t3 & t4);
    double t6 = gm_l1;
    double t7 = t6;
    uint64_t t8;
    memset(&t8, 0, sizeof t8); memcpy(&t8, ((uint8_t *)&t7), sizeof t8 < 8 ? sizeof t8 : 8);
    uint64_t t9 = GM_LD(uint64_t, gm_c6d5506jf7q);
    uint64_t t10 = (uint64_t)(t8 & t9);
    uint64_t t11 = (uint64_t)(t5 | t10);
    uint64_t t12 = t11;
    double t13;
    memset(&t13, 0, sizeof t13); memcpy(&t13, ((uint8_t *)&t12), sizeof t13 < 8 ? sizeof t13 : 8);
    F.ret = t13;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_5g066b3(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_5g066b3 F;
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
inline __attribute__((aligned(4))) void gm_f_failed_to_run_3e08soa(void *gm_out, gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_failed_to_run_3e08soa F;
    gm_b16_8 gm_l0; /* detail */
    GM_STACK_CHECK_SHALLOW(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t1 = gm_l0;
    gm_th2lqbo77pvsc t2;
    GM_ST(uint8_t, (uint8_t *)&t2 + 17, 0);
    GM_ST(uint16_t, (uint8_t *)&t2 + 18, 0);
    GM_ST(uint32_t, (uint8_t *)&t2 + 20, 0);
    GM_ST(gm_b16_8, ((uint8_t *)&t2), t1);
    ((uint8_t *)&t2)[16] = 1;
    gm_b16_8 t3 = gm_l0;
    GM_ST(gm_th2lqbo77pvsc, (uint8_t *)&(*(gm_tp3p9lj444cp7 *)gm_out) + 0, t2);
    GM_ST(gm_b16_8, (uint8_t *)&(*(gm_tp3p9lj444cp7 *)gm_out) + 24, t3);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_tp3p9lj444cp7 *)gm_out) + 40, ((uint64_t)0ULL));
    goto gm_fdone;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_hrv26t6(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_for_body_hrv26t6 F;
    gm_b16_8 gm_l0; /* <item> */
    gm_b16_8 gm_l1; /* line */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t3 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l0));
    gm_l1 = t3;
    {
        gm_b16_8 t5 = gm_l1;
        gm_b16_8 t6 = t5;
        if (gm_pf1m4b8q4e78(cx, gm_som0q53jqvjh3, 4, gm_src4mnsbede6fos, 57, 36)) {
            goto gm_lad;
        }
        if (gm_pf70j0q3sgt1(cx, &gm_ty0iqiubfm7e, ((uint8_t *)&t6), gm_src4mnsbede6fos, 57, 36)) {
            goto gm_lad;
        }
        if (gm_pf1m4b8q4e78(cx, gm_so514oj5nbhcu, 1, gm_src4mnsbede6fos, 57, 36)) {
            goto gm_lad;
        }
    }
    goto me1;
mn2:;
    gm_trap("no match arm matched the scrutinee", gm_src4mnsbede6fos, 57, 5);
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
inline __attribute__((aligned(4))) void gm_f_list_3po0a3n(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_3po0a3n F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_5g066b3();
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
inline __attribute__((aligned(4))) double gm_f_round_ocas0nv(double a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_round_ocas0nv F;
    double gm_l0; /* x */
    double gm_l1; /* t */
    GM_STACK_CHECK_SHALLOW(&F, 123u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        double t2 = gm_l0;
        double t3 = gm_f_trunc_oao3g20(t2);
        gm_l1 = t3;
    }
    {
        double t5 = gm_l0;
        double t6 = gm_l1;
        double t7 = (t5 - t6);
        double t8 = gm_f_abs_n2h5t0g(t7);
        uint8_t t9 = (t8 >= ((double)5e-1));
        if (!t9) goto ie4;
        {
            double t11 = gm_l1;
            double t12 = gm_l0;
            double t13 = gm_f_copysign_qgltnuv(((double)1e0), t12);
            double t14 = (t11 + t13);
            uint8_t t15 = 0;
            F.ret = t14;
            goto gm_fret;
        }
        goto ix4;
ie4:;
ix4:;
    }
    {
        double t16 = gm_l1;
        uint8_t t17 = 0;
        F.ret = t16;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) double gm_f_sqrt_mar88n3(double a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_sqrt_mar88n3 F;
    double gm_l0; /* x */
    GM_STACK_CHECK_SHALLOW(&F, 40u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    double t1 = gm_l0;
    double t2 = (double)(__builtin_sqrt(t1));
    F.ret = t2;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) void gm_f_failed_to_run_3e08soa(void *gm_out, gm_b16_8 a0);
__attribute__((aligned(4))) gm_tp3p9lj444cp7 gm_f_failed_to_run_3e08soa_c(gm_b16_8 a0) { gm_tp3p9lj444cp7 gm_r; gm_f_failed_to_run_3e08soa(&gm_r, a0); return gm_r; }
__attribute__((aligned(4))) uint8_t gm_f_finish_2t60fs7(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_finish_2t60fs7 F;
    void * gm_l0; /* r */
    uint64_t gm_l1; /* n */
    uint64_t gm_l2; /* n */
    gm_b16_8 gm_l3; /* line */
    GM_STACK_CHECK(&F, 929u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_b16_8 t2;
        void * t4 = gm_l0;
        uint8_t t5 = GM_LD(uint8_t, (((uint8_t *)t4) + 0));
        if (!t5) goto ie3;
        gm_b16_8 t7;
        void * t9 = gm_l0;
        if (!(((((uint8_t *)t9) + 24))[8] != 0)) goto ie8;
        uint64_t t10 = GM_LD(uint64_t, (((uint8_t *)t9) + 24));
        gm_l1 = t10;
        gm_tkpb4kplftgaa t12;
        void * t13 = gm_l0;
        uint64_t t14 = GM_LD(uint64_t, (((uint8_t *)t13) + 8));
        GM_ST(uint64_t, (uint8_t *)&t12 + 0, t14);
        void * t15 = gm_l0;
        uint64_t t16 = GM_LD(uint64_t, (((uint8_t *)t15) + 16));
        GM_ST(uint64_t, (uint8_t *)&t12 + 8, t16);
        uint64_t t17 = gm_l1;
        GM_ST(uint64_t, (uint8_t *)&t12 + 16, t17);
        gm_tabb710d9mq2e t18;
        gm_f_format_iq1do3n((void *)&t18, (void *)NULL, cx, NULL, &t12);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t19 = t18;
        if (((((uint8_t *)&t19))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t19) + 8, gm_src4mnsbede6fos, 64, 13);
        gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&t19) + 8));
        gm_b16_8 t21 = t20;
        t7 = t21;
        goto ix8;
ie8:;
        void * t23 = gm_l0;
        uint64_t t24 = GM_LD(uint64_t, (((uint8_t *)t23) + 8));
        void * t25 = gm_l0;
        uint64_t t26 = GM_LD(uint64_t, (((uint8_t *)t25) + 16));
        gm_t2lqu8jsj511d t27;
        GM_ST(uint64_t, ((uint8_t *)&t27 + 0), t24);
        GM_ST(uint64_t, ((uint8_t *)&t27 + 8), t26);
        gm_tabb710d9mq2e t28;
        gm_f_format_v3lvmh8((void *)&t28, (void *)NULL, cx, NULL, t27);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t29 = t28;
        if (((((uint8_t *)&t29))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t29) + 8, gm_src4mnsbede6fos, 66, 13);
        gm_b16_8 t30 = GM_LD(gm_b16_8, (((uint8_t *)&t29) + 8));
        gm_b16_8 t31 = t30;
        t7 = t31;
ix8:;
        gm_b16_8 t32 = t7;
        t2 = t32;
        goto ix3;
ie3:;
        gm_b16_8 t33;
        void * t35 = gm_l0;
        if (!(((((uint8_t *)t35) + 24))[8] != 0)) goto ie34;
        uint64_t t36 = GM_LD(uint64_t, (((uint8_t *)t35) + 24));
        gm_l2 = t36;
        gm_tkpb4kplftgaa t38;
        void * t39 = gm_l0;
        uint64_t t40 = GM_LD(uint64_t, (((uint8_t *)t39) + 8));
        GM_ST(uint64_t, (uint8_t *)&t38 + 0, t40);
        void * t41 = gm_l0;
        uint64_t t42 = GM_LD(uint64_t, (((uint8_t *)t41) + 16));
        GM_ST(uint64_t, (uint8_t *)&t38 + 8, t42);
        uint64_t t43 = gm_l2;
        GM_ST(uint64_t, (uint8_t *)&t38 + 16, t43);
        gm_tabb710d9mq2e t44;
        gm_f_format_4otle6l((void *)&t44, (void *)NULL, cx, NULL, &t38);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t45 = t44;
        if (((((uint8_t *)&t45))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t45) + 8, gm_src4mnsbede6fos, 69, 9);
        gm_b16_8 t46 = GM_LD(gm_b16_8, (((uint8_t *)&t45) + 8));
        gm_b16_8 t47 = t46;
        t33 = t47;
        goto ix34;
ie34:;
        void * t49 = gm_l0;
        uint64_t t50 = GM_LD(uint64_t, (((uint8_t *)t49) + 8));
        void * t51 = gm_l0;
        uint64_t t52 = GM_LD(uint64_t, (((uint8_t *)t51) + 16));
        gm_t2lqu8jsj511d t53;
        GM_ST(uint64_t, ((uint8_t *)&t53 + 0), t50);
        GM_ST(uint64_t, ((uint8_t *)&t53 + 8), t52);
        gm_tabb710d9mq2e t54;
        gm_f_format_729oo46((void *)&t54, (void *)NULL, cx, NULL, t53);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t55 = t54;
        if (((((uint8_t *)&t55))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t55) + 8, gm_src4mnsbede6fos, 71, 9);
        gm_b16_8 t56 = GM_LD(gm_b16_8, (((uint8_t *)&t55) + 8));
        gm_b16_8 t57 = t56;
        t33 = t57;
ix34:;
        t2 = t33;
ix3:;
        gm_l3 = t2;
    }
    {
        void * t59 = gm_l0;
        uint8_t t60 = GM_LD(uint8_t, (((uint8_t *)t59) + 41));
        if (!t60) goto ie58;
        {
            gm_t6j6smj7qoctu t62;
            gm_b16_8 t63 = gm_l3;
            GM_ST(gm_b16_8, (uint8_t *)&t62 + 0, t63);
            gm_node *pn64 = gm_find(cx, 1579066750);
            if (!pn64) gm_trap("no handler installed for effect `Clock`", gm_src4mnsbede6fos, 73, 77);
            gm_t4ce0jd3tdro4 t65;
            t65 = ((gm_t4ce0jd3tdro4 (*)(void *, gm_node *, gm_node *))pn64->clauses[0])(pn64->env, pn64->hctx, pn64);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t4ce0jd3tdro4 t66 = t65;
            uint64_t t67 = GM_LD(uint64_t, (((uint8_t *)&t66) + 0));
            void * t68 = gm_l0;
            uint64_t t69 = GM_LD(uint64_t, (((uint8_t *)t68) + 48));
            uint64_t r70; bool o71 = __builtin_sub_overflow(t67, t69, &r70);
            if (o71) gm_trap("integer overflow in `-`", gm_src4mnsbede6fos, 73, 77);
            uint64_t t72 = r70;
            gm_b16_8 t73 = gm_f_fmt_duration_qh0lr6g((void *)NULL, cx, NULL, t72);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(gm_b16_8, (uint8_t *)&t62 + 16, t73);
            gm_tabb710d9mq2e t74;
            gm_f_format_o5mv7j4((void *)&t74, (void *)NULL, cx, NULL, &t62);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t75 = t74;
            if (((((uint8_t *)&t75))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t75) + 8, gm_src4mnsbede6fos, 73, 24);
            gm_b16_8 t76 = GM_LD(gm_b16_8, (((uint8_t *)&t75) + 8));
            gm_l3 = t76;
        }
        goto ix58;
ie58:;
ix58:;
    }
    {
        gm_b16_8 t77 = gm_l3;
        gm_b16_8 t78 = t77;
        if (gm_pf70j0q3sgt1(cx, &gm_tybihgc69jv3, ((uint8_t *)&t78), gm_src4mnsbede6fos, 74, 5)) {
            goto gm_lad;
        }
        if (gm_pf1m4b8q4e78(cx, gm_so514oj5nbhcu, 1, gm_src4mnsbede6fos, 74, 5)) {
            goto gm_lad;
        }
    }
    {
        uint8_t t79;
        void * t81 = gm_l0;
        uint64_t t82 = GM_LD(uint64_t, (((uint8_t *)t81) + 16));
        uint8_t t83 = ((t82 == ((uint64_t)0ULL)));
        if (!t83) goto ie80;
        uint8_t t85 = ((uint8_t)0ULL);
        t79 = t85;
        goto ix80;
ie80:;
        uint8_t t87 = ((uint8_t)1ULL);
        t79 = t87;
ix80:;
        uint8_t t88 = 0;
        F.ret = t79;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_fmt_duration_qh0lr6g(void *env, gm_node *cx, gm_node *inst, uint64_t a0) {
    struct gm_fr_fmt_duration_qh0lr6g F;
    uint64_t gm_l0; /* ns */
    double gm_l1; /* us */
    GM_STACK_CHECK(&F, 477u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint64_t t2 = gm_l0;
        double t3 = (double)(t2);
        double t4 = (t3 / ((double)1e3));
        gm_l1 = t4;
    }
    {
        double t6 = gm_l1;
        uint8_t t7 = (t6 < ((double)1e3));
        if (!t7) goto ie5;
        {
            double t9 = gm_l1;
            double t10 = gm_f_round_ocas0nv(t9);
            double t11 = (double)t10;
            double t12 = __builtin_trunc(t11);
            uint64_t t13 = __builtin_isnan(t11) ? (uint64_t)0 : (t12 < 0e0 ? ((uint64_t)0ULL) : (t12 >= 1.8446744073709552e19 ? ((uint64_t)18446744073709551615ULL) : (uint64_t)t12));
            gm_t4ce0jd3tdro4 t14;
            GM_ST(uint64_t, ((uint8_t *)&t14 + 0), t13);
            gm_tabb710d9mq2e t15;
            gm_f_format_9t9dukb((void *)&t15, (void *)NULL, cx, NULL, t14);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t16 = t15;
            if (((((uint8_t *)&t16))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t16) + 8, gm_src4mnsbede6fos, 81, 29);
            gm_b16_8 t17 = GM_LD(gm_b16_8, (((uint8_t *)&t16) + 8));
            gm_b16_8 t18 = t17;
            gm_b16_8 t19 = GM_LD(gm_b16_8, ((uint8_t *)&t18));
            uint8_t t20 = 0;
            F.ret = t19;
            goto gm_fret;
        }
        goto ix5;
ie5:;
ix5:;
    }
    {
        double t22 = gm_l1;
        uint8_t t23 = (t22 < ((double)1e6));
        if (!t23) goto ie21;
        {
            double t25 = gm_l1;
            double t26 = (t25 / ((double)1e3));
            gm_tidu2er0d7h95 t27;
            GM_ST(double, ((uint8_t *)&t27 + 0), t26);
            gm_tabb710d9mq2e t28;
            gm_f_format_2kpt646((void *)&t28, (void *)NULL, cx, NULL, t27);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t29 = t28;
            if (((((uint8_t *)&t29))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t29) + 8, gm_src4mnsbede6fos, 82, 26);
            gm_b16_8 t30 = GM_LD(gm_b16_8, (((uint8_t *)&t29) + 8));
            gm_b16_8 t31 = t30;
            gm_b16_8 t32 = GM_LD(gm_b16_8, ((uint8_t *)&t31));
            uint8_t t33 = 0;
            F.ret = t32;
            goto gm_fret;
        }
        goto ix21;
ie21:;
ix21:;
    }
    {
        double t34 = gm_l1;
        double t35 = (t34 / ((double)1e6));
        gm_tidu2er0d7h95 t36;
        GM_ST(double, ((uint8_t *)&t36 + 0), t35);
        gm_tabb710d9mq2e t37;
        gm_f_format_bia01cc((void *)&t37, (void *)NULL, cx, NULL, t36);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t38 = t37;
        if (((((uint8_t *)&t38))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t38) + 8, gm_src4mnsbede6fos, 83, 12);
        gm_b16_8 t39 = GM_LD(gm_b16_8, (((uint8_t *)&t38) + 8));
        gm_b16_8 t40 = t39;
        gm_b16_8 t41 = GM_LD(gm_b16_8, ((uint8_t *)&t40));
        uint8_t t42 = 0;
        F.ret = t41;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_fmt_per_run_159rum2(void *env, gm_node *cx, gm_node *inst, double a0) {
    struct gm_fr_fmt_per_run_159rum2 F;
    double gm_l0; /* ns */
    GM_STACK_CHECK(&F, 543u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        double t3 = gm_l0;
        uint8_t t4 = (t3 < ((double)1e3));
        if (!t4) goto ie2;
        {
            double t6 = gm_l0;
            gm_tidu2er0d7h95 t7;
            GM_ST(double, ((uint8_t *)&t7 + 0), t6);
            gm_tabb710d9mq2e t8;
            gm_f_format_edua06s((void *)&t8, (void *)NULL, cx, NULL, t7);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t9 = t8;
            if (((((uint8_t *)&t9))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t9) + 8, gm_src4mnsbede6fos, 88, 26);
            gm_b16_8 t10 = GM_LD(gm_b16_8, (((uint8_t *)&t9) + 8));
            gm_b16_8 t11 = t10;
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
            uint8_t t13 = 0;
            F.ret = t12;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        double t15 = gm_l0;
        uint8_t t16 = (t15 < ((double)1e6));
        if (!t16) goto ie14;
        {
            double t18 = gm_l0;
            double t19 = (t18 / ((double)1e3));
            gm_tidu2er0d7h95 t20;
            GM_ST(double, ((uint8_t *)&t20 + 0), t19);
            gm_tabb710d9mq2e t21;
            gm_f_format_tttig7s((void *)&t21, (void *)NULL, cx, NULL, t20);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t22 = t21;
            if (((((uint8_t *)&t22))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t22) + 8, gm_src4mnsbede6fos, 89, 26);
            gm_b16_8 t23 = GM_LD(gm_b16_8, (((uint8_t *)&t22) + 8));
            gm_b16_8 t24 = t23;
            gm_b16_8 t25 = GM_LD(gm_b16_8, ((uint8_t *)&t24));
            uint8_t t26 = 0;
            F.ret = t25;
            goto gm_fret;
        }
        goto ix14;
ie14:;
ix14:;
    }
    {
        double t28 = gm_l0;
        uint8_t t29 = (t28 < ((double)1e9));
        if (!t29) goto ie27;
        {
            double t31 = gm_l0;
            double t32 = (t31 / ((double)1e6));
            gm_tidu2er0d7h95 t33;
            GM_ST(double, ((uint8_t *)&t33 + 0), t32);
            gm_tabb710d9mq2e t34;
            gm_f_format_88h5fhl((void *)&t34, (void *)NULL, cx, NULL, t33);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t35 = t34;
            if (((((uint8_t *)&t35))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t35) + 8, gm_src4mnsbede6fos, 90, 26);
            gm_b16_8 t36 = GM_LD(gm_b16_8, (((uint8_t *)&t35) + 8));
            gm_b16_8 t37 = t36;
            gm_b16_8 t38 = GM_LD(gm_b16_8, ((uint8_t *)&t37));
            uint8_t t39 = 0;
            F.ret = t38;
            goto gm_fret;
        }
        goto ix27;
ie27:;
ix27:;
    }
    {
        double t40 = gm_l0;
        double t41 = (t40 / ((double)1e9));
        gm_tidu2er0d7h95 t42;
        GM_ST(double, ((uint8_t *)&t42 + 0), t41);
        gm_tabb710d9mq2e t43;
        gm_f_format_ia4ff4d((void *)&t43, (void *)NULL, cx, NULL, t42);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t44 = t43;
        if (((((uint8_t *)&t44))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t44) + 8, gm_src4mnsbede6fos, 91, 12);
        gm_b16_8 t45 = GM_LD(gm_b16_8, (((uint8_t *)&t44) + 8));
        gm_b16_8 t46 = t45;
        gm_b16_8 t47 = GM_LD(gm_b16_8, ((uint8_t *)&t46));
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
extern __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_hrv26t6(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_ui39jfe(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_for_body_ui39jfe F;
    gm_b16_8 gm_l0; /* <item> */
    gm_b16_8 gm_l1; /* word */
    double gm_l2; /* x */
    GM_STACK_CHECK(&F, 222u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t3 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l0));
    gm_l1 = t3;
    {
        gm_b16_8 t5 = gm_l1;
        gm_th2bqc58ss467 t6 = gm_f_parse_number_hb0i8jm(t5);
        gm_th2bqc58ss467 t7 = t6;
        double t8;
        if (((((uint8_t *)&t7))[8] != 0)) {
            double t9 = GM_LD(double, (((uint8_t *)&t7) + 0));
            t8 = t9;
        } else {
            gm_ttu09lij36f8c t10;
            memset(&t10, 0, sizeof t10);
            uint8_t t11 = 0;
            ((struct gm_fr_summarize_kafmpn5 *)((&F.h)->up))->ret = t10;
            gm_unw.kind = GM_RETURN; gm_unw.frame = ((&F.h)->up); gm_unw.label = 0; gm_unw.is_err = t11;
            goto gm_lad;
        }
        gm_l2 = t8;
    }
    {
        uint8_t t13;
        if (((((uint8_t *)&((struct gm_fr_summarize_kafmpn5 *)((&F.h)->up))->l5))[8] != 0)) goto mn15;
        t13 = ((uint8_t)1);
        goto me14;
mn15:;
        t13 = ((uint8_t)0);
        goto me14;
mn16:;
        gm_trap("no match arm matched the scrutinee", gm_src4mnsbede6fos, 117, 12);
me14:;
        if (!t13) goto ie12;
        {
            double t18 = gm_l2;
            gm_th2bqc58ss467 t19;
            GM_ST(uint8_t, (uint8_t *)&t19 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t19 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t19 + 12, 0);
            GM_ST(double, ((uint8_t *)&t19), t18);
            ((uint8_t *)&t19)[8] = 1;
            GM_ST(gm_th2bqc58ss467, ((uint8_t *)&((struct gm_fr_summarize_kafmpn5 *)((&F.h)->up))->l5), t19);
        }
        goto ix12;
ie12:;
        {
            void * t21 = (void *)((uint8_t *)&((struct gm_fr_summarize_kafmpn5 *)((&F.h)->up))->l6);
            double t22 = gm_l2;
            gm_tftb1odc739t5 t23 = gm_f_push_n9ksguc(t21, t22);
            gm_tftb1odc739t5 t24 = t23;
            if (((((uint8_t *)&t24))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t24) + 1, gm_src4mnsbede6fos, 120, 13);
            gm_b0_1 t25 = GM_LD(gm_b0_1, (((uint8_t *)&t24) + 1));
        }
        {
            uint64_t t26 = GM_LD(uint64_t, ((uint8_t *)&((struct gm_fr_summarize_kafmpn5 *)((&F.h)->up))->l7));
            uint64_t r27; bool o28 = __builtin_add_overflow(t26, ((uint64_t)1ULL), &r27);
            if (o28) gm_trap("integer overflow in `+`", gm_src4mnsbede6fos, 121, 13);
            uint64_t t29 = r27;
            GM_ST(uint64_t, ((uint8_t *)&((struct gm_fr_summarize_kafmpn5 *)((&F.h)->up))->l7), t29);
        }
ix12:;
    }
    goto me1;
mn2:;
    gm_trap("no match arm matched the scrutinee", gm_src4mnsbede6fos, 115, 5);
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
inline __attribute__((aligned(4))) gm_th2bqc58ss467 gm_f_parse_number_hb0i8jm(gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_parse_number_hb0i8jm F;
    gm_b16_8 gm_l0; /* text */
    GM_STACK_CHECK(&F, 219u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_b16_8 t3 = gm_l0;
        gm_b16_8 t4;
        GM_ST(void *, (uint8_t *)&t4, (void *)(gm_sfdbfrv46lihd));
        GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(1));
        uint8_t t5 = gm_f_contains_gkki4kq(t3, t4);
        if (!t5) goto ie2;
        {
            gm_th2bqc58ss467 t7;
            memset(&t7, 0, sizeof t7);
            uint8_t t8 = 0;
            F.ret = t7;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        gm_b16_8 t9 = gm_l0;
        gm_tspt3vn9ljcnd t10;
        gm_f_parse_float_17hgksa((void *)&t10, t9);
        gm_tspt3vn9ljcnd t11 = t10;
        gm_th2bqc58ss467 t12;
        if (!((((uint8_t *)&t11))[0] != 0)) {
            double t13 = GM_LD(double, (((uint8_t *)&t11) + 8));
            gm_th2bqc58ss467 t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(double, ((uint8_t *)&t14), t13);
            ((uint8_t *)&t14)[8] = 1;
            t12 = t14;
        } else {
            gm_th2bqc58ss467 t15;
            memset(&t15, 0, sizeof t15);
            t12 = t15;
        }
        uint8_t t16 = 0;
        F.ret = t12;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_b0_1 gm_f_record_abrqivi(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, const void * a2) {
    struct gm_fr_record_abrqivi F;
    void * gm_l0; /* r */
    gm_b16_8 gm_l3; /* word */
    gm_b16_8 gm_l4; /* name */
    gm_b16_8 gm_l5; /* m */
    gm_b16_8 gm_l6; /* took */
    gm_b16_8 gm_l7; /* shown */
    gm_b16_8 gm_l8; /* detail */
    gm_b16_8 gm_l9; /* output */
    gm_b16_8 gm_l10; /* result */
    gm_t6j6smj7qoctu gm_l11; /* summary */
    gm_b16_8 gm_l12; /* <tmp> */
    GM_STACK_CHECK(&F, 1342u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&F.l1, a1, sizeof F.l1);
    {
        gm_b16_8 t2;
        void * t4 = gm_l0;
        uint8_t t5 = GM_LD(uint8_t, (((uint8_t *)t4) + 0));
        if (!t5) goto ie3;
        gm_b16_8 t7;
        GM_ST(void *, (uint8_t *)&t7, (void *)(gm_srfagufvviad7));
        GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)(5));
        gm_b16_8 t8 = t7;
        t2 = t8;
        goto ix3;
ie3:;
        gm_b16_8 t10;
        GM_ST(void *, (uint8_t *)&t10, (void *)(gm_s9r64t1166q1f));
        GM_ST(uintptr_t, (uint8_t *)&t10 + 8, (uintptr_t)(4));
        gm_b16_8 t11 = t10;
        t2 = t11;
ix3:;
        gm_l3 = t2;
    }
    {
        gm_b16_8 t12 = GM_LD(gm_b16_8, (((uint8_t *)&F.l1) + 0));
        gm_tnpifv20irnph t13;
        GM_ST(gm_b16_8, ((uint8_t *)&t13 + 0), t12);
        gm_tabb710d9mq2e t14;
        gm_f_format_bkdctpk((void *)&t14, (void *)NULL, cx, NULL, t13);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t15 = t14;
        if (((((uint8_t *)&t15))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t15) + 8, gm_src4mnsbede6fos, 36, 13);
        gm_b16_8 t16 = GM_LD(gm_b16_8, (((uint8_t *)&t15) + 8));
        gm_l4 = t16;
    }
    {
        if (!(((((uint8_t *)&F.l1) + 16))[16] != 0)) goto ie17;
        gm_b16_8 t18 = GM_LD(gm_b16_8, (((uint8_t *)&F.l1) + 16));
        gm_l5 = t18;
        {
            gm_t6j6smj7qoctu t20;
            gm_b16_8 t21 = GM_LD(gm_b16_8, (((uint8_t *)&F.l1) + 0));
            GM_ST(gm_b16_8, (uint8_t *)&t20 + 0, t21);
            gm_b16_8 t22 = gm_l5;
            GM_ST(gm_b16_8, (uint8_t *)&t20 + 16, t22);
            gm_tabb710d9mq2e t23;
            gm_f_format_ahg4b8c((void *)&t23, (void *)NULL, cx, NULL, &t20);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t24 = t23;
            if (((((uint8_t *)&t24))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t24) + 8, gm_src4mnsbede6fos, 37, 31);
            gm_b16_8 t25 = GM_LD(gm_b16_8, (((uint8_t *)&t24) + 8));
            gm_l4 = t25;
        }
        goto ix17;
ie17:;
ix17:;
    }
    {
        gm_b16_8 t26;
        void * t28 = gm_l0;
        uint8_t t29 = GM_LD(uint8_t, (((uint8_t *)t28) + 41));
        if (!t29) goto ie27;
        void * t30 = gm_l0;
        uint8_t t31 = GM_LD(uint8_t, (((uint8_t *)t30) + 0));
        uint8_t t32 = (uint8_t)!t31;
        if (!t32) goto ie27;
        uint64_t t34 = GM_LD(uint64_t, (((uint8_t *)a2) + 40));
        gm_b16_8 t35 = gm_f_fmt_duration_qh0lr6g((void *)NULL, cx, NULL, t34);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tnpifv20irnph t36;
        GM_ST(gm_b16_8, ((uint8_t *)&t36 + 0), t35);
        gm_tabb710d9mq2e t37;
        gm_f_format_k7lpr9c((void *)&t37, (void *)NULL, cx, NULL, t36);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t38 = t37;
        if (((((uint8_t *)&t38))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t38) + 8, gm_src4mnsbede6fos, 38, 48);
        gm_b16_8 t39 = GM_LD(gm_b16_8, (((uint8_t *)&t38) + 8));
        gm_b16_8 t40 = t39;
        gm_b16_8 t41 = t40;
        gm_b16_8 t42 = GM_LD(gm_b16_8, ((uint8_t *)&t41));
        t26 = t42;
        goto ix27;
ie27:;
        gm_b16_8 t44;
        GM_ST(void *, (uint8_t *)&t44, (void *)(gm_sr4qbrk6tlgaa));
        GM_ST(uintptr_t, (uint8_t *)&t44 + 8, (uintptr_t)(0));
        gm_b16_8 t45 = t44;
        t26 = t45;
ix27:;
        gm_l6 = t26;
    }
    {
        gm_b16_8 t46;
        GM_ST(void *, (uint8_t *)&t46, (void *)(gm_sr4qbrk6tlgaa));
        GM_ST(uintptr_t, (uint8_t *)&t46 + 8, (uintptr_t)(0));
        gm_l7 = t46;
    }
    {
        if (!(((((uint8_t *)a2) + 0))[16] != 0)) goto ie47;
        gm_b16_8 t48 = GM_LD(gm_b16_8, (((uint8_t *)a2) + 0));
        gm_l8 = t48;
        {
            void * t50 = gm_l0;
            void * t51 = gm_l0;
            uint64_t t52 = GM_LD(uint64_t, (((uint8_t *)t51) + 16));
            uint64_t r53; bool o54 = __builtin_add_overflow(t52, ((uint64_t)1ULL), &r53);
            if (o54) gm_trap("integer overflow in `+`", gm_src4mnsbede6fos, 41, 9);
            uint64_t t55 = r53;
            GM_ST(uint64_t, (((uint8_t *)t50) + 16), t55);
        }
        {
            gm_b16_8 t56 = gm_l3;
            gm_b16_8 t57 = t56;
            gm_b16_8 t58 = gm_l4;
            gm_b16_8 t59 = t58;
            gm_b16_8 t60 = gm_l6;
            gm_b16_8 t61 = t60;
            if (gm_pf70j0q3sgt1(cx, &gm_ty0iqiubfm7e, ((uint8_t *)&t57), gm_src4mnsbede6fos, 42, 9)) {
                goto gm_lad;
            }
            if (gm_pf1m4b8q4e78(cx, gm_s3p0rbs9hc3lg, 1, gm_src4mnsbede6fos, 42, 9)) {
                goto gm_lad;
            }
            if (gm_pf70j0q3sgt1(cx, &gm_tybihgc69jv3, ((uint8_t *)&t59), gm_src4mnsbede6fos, 42, 9)) {
                goto gm_lad;
            }
            if (gm_pf1m4b8q4e78(cx, gm_sp77et3djoa12, 11, gm_src4mnsbede6fos, 42, 9)) {
                goto gm_lad;
            }
            if (gm_pf70j0q3sgt1(cx, &gm_ty0iqiubfm7e, ((uint8_t *)&t61), gm_src4mnsbede6fos, 42, 9)) {
                goto gm_lad;
            }
            if (gm_pf1m4b8q4e78(cx, gm_so514oj5nbhcu, 1, gm_src4mnsbede6fos, 42, 9)) {
                goto gm_lad;
            }
        }
        {
            gm_b16_8 t62 = gm_l8;
            gm_l7 = t62;
        }
        goto ix47;
ie47:;
        {
            void * t64 = gm_l0;
            void * t65 = gm_l0;
            uint64_t t66 = GM_LD(uint64_t, (((uint8_t *)t65) + 8));
            uint64_t r67; bool o68 = __builtin_add_overflow(t66, ((uint64_t)1ULL), &r67);
            if (o68) gm_trap("integer overflow in `+`", gm_src4mnsbede6fos, 45, 9);
            uint64_t t69 = r67;
            GM_ST(uint64_t, (((uint8_t *)t64) + 8), t69);
        }
        {
            gm_b16_8 t70 = GM_LD(gm_b16_8, (((uint8_t *)a2) + 24));
            gm_l9 = t70;
        }
        {
            gm_b16_8 t71;
            GM_ST(void *, (uint8_t *)&t71, (void *)(gm_soim4k70n6bc7));
            GM_ST(uintptr_t, (uint8_t *)&t71 + 8, (uintptr_t)(2));
            gm_l10 = t71;
        }
        {
            void * t73 = gm_l0;
            uint8_t t74 = GM_LD(uint8_t, (((uint8_t *)t73) + 0));
            if (!t74) goto ie72;
            {
                gm_b16_8 t77 = gm_l9;
                gm_ttu09lij36f8c t78;
                gm_f_summarize_kafmpn5((void *)&t78, (void *)NULL, cx, NULL, t77);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_ttu09lij36f8c t79 = t78;
                if (!((((uint8_t *)&t79))[32] != 0)) goto ie76;
                gm_t6j6smj7qoctu t80 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)&t79));
                gm_l11 = t80;
                {
                    gm_b16_8 t82 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l11) + 0));
                    gm_l10 = t82;
                }
                {
                    gm_b16_8 t83 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l11) + 16));
                    gm_l9 = t83;
                }
                goto ix76;
ie76:;
ix76:;
            }
            goto ix72;
ie72:;
ix72:;
        }
        {
            gm_b16_8 t84 = gm_l3;
            gm_b16_8 t85 = t84;
            gm_b16_8 t86 = gm_l4;
            gm_b16_8 t87 = t86;
            gm_b16_8 t88 = gm_l10;
            gm_b16_8 t89 = t88;
            gm_b16_8 t90 = gm_l6;
            gm_b16_8 t91 = t90;
            if (gm_pf70j0q3sgt1(cx, &gm_ty0iqiubfm7e, ((uint8_t *)&t85), gm_src4mnsbede6fos, 54, 9)) {
                goto gm_lad;
            }
            if (gm_pf1m4b8q4e78(cx, gm_s3p0rbs9hc3lg, 1, gm_src4mnsbede6fos, 54, 9)) {
                goto gm_lad;
            }
            if (gm_pf70j0q3sgt1(cx, &gm_tybihgc69jv3, ((uint8_t *)&t87), gm_src4mnsbede6fos, 54, 9)) {
                goto gm_lad;
            }
            if (gm_pf1m4b8q4e78(cx, gm_s5f956mm1l828, 5, gm_src4mnsbede6fos, 54, 9)) {
                goto gm_lad;
            }
            if (gm_pf70j0q3sgt1(cx, &gm_ty0iqiubfm7e, ((uint8_t *)&t89), gm_src4mnsbede6fos, 54, 9)) {
                goto gm_lad;
            }
            if (gm_pf70j0q3sgt1(cx, &gm_ty0iqiubfm7e, ((uint8_t *)&t91), gm_src4mnsbede6fos, 54, 9)) {
                goto gm_lad;
            }
            if (gm_pf1m4b8q4e78(cx, gm_so514oj5nbhcu, 1, gm_src4mnsbede6fos, 54, 9)) {
                goto gm_lad;
            }
        }
        {
            void * t93 = gm_l0;
            uint8_t t94 = GM_LD(uint8_t, (((uint8_t *)t93) + 40));
            if (!t94) goto ie92;
            {
                gm_b16_8 t96 = gm_l9;
                gm_l7 = t96;
            }
            goto ix92;
ie92:;
ix92:;
        }
ix47:;
    }
    {
        {
            gm_b16_8 t98 = gm_l7;
            gm_l12 = t98;
        }
        gm_hval t99 = ((gm_hval){(void *)&F.h, cx});
        gm_node wn100 = {.eff = 993381376, .kind = GM_LIT, .clauses = gm_hlob66lsuf8h, .env = t99.env, .hctx = t99.cx, .with_frame = &F.h, .with_label = 0, .result = (void *)&F.w0, .fw = NULL, .next = cx};
        gm_b16_8 t103 = gm_l12;
        gm_b0_1 t104 = gm_f_lines_9uns0k2_scfak7n((void *)NULL, (&wn100), NULL, t103);
        if (gm_unw.kind) {
            goto gm_ll102;
        }
        F.w0 = t104;
        goto gm_ls105;
gm_ll102:;
        if (gm_unw.kind == GM_BREAK && gm_unw.frame == &F.h && gm_unw.label == 0) { gm_unw.kind = GM_NONE; goto we101; }
        goto gm_lad;
gm_ls105:;
we101:;
        gm_b0_1 t106 = F.w0;
        gm_b0_1 t107 = t106;
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
__attribute__((aligned(4))) void gm_f_summarize_kafmpn5(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_summarize_kafmpn5 F;
    gm_b16_8 gm_l0; /* output */
    uint64_t gm_l1; /* at */
    uint64_t gm_l2; /* end */
    uint64_t gm_l3; /* line_end */
    gm_b16_8 gm_l4; /* line */
    gm_b16_8 gm_l8; /* <tmp> */
    double gm_l9; /* n_iters */
    uint64_t gm_l10; /* <tmp> */
    uint64_t gm_l11; /* <tmp> */
    uint64_t gm_l12; /* i */
    double gm_l13; /* n */
    double gm_l14; /* total */
    gm_b16_8 gm_l15; /* <tmp> */
    uint64_t gm_l16; /* <tmp> */
    double gm_l17; /* x */
    double gm_l18; /* mean */
    double gm_l19; /* squares */
    gm_b16_8 gm_l20; /* <tmp> */
    uint64_t gm_l21; /* <tmp> */
    double gm_l22; /* x */
    double gm_l23; /* var */
    double gm_l24; /* rel */
    gm_b16_8 gm_l25; /* summary */
    gm_b16_8 gm_l26; /* rest */
    gm_b16_8 gm_l27; /* before */
    uint64_t gm_l28; /* skip */
    GM_STACK_CHECK(&F, 2146u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_b16_8 t2 = gm_l0;
        gm_b16_8 t3 = GM_LD(gm_b16_8, gm_co3qlejdntu);
        gm_tf5srh4mp4b3i t4 = gm_f_rfind_ai5s2js(t2, t3);
        gm_tf5srh4mp4b3i t5 = t4;
        uint64_t t6;
        if (((((uint8_t *)&t5))[8] != 0)) {
            uint64_t t7 = GM_LD(uint64_t, (((uint8_t *)&t5) + 0));
            t6 = t7;
        } else {
            gm_ttu09lij36f8c t8;
            memset(&t8, 0, sizeof t8);
            uint8_t t9 = 0;
            F.ret = t8;
            goto gm_fret;
        }
        gm_l1 = t6;
    }
    {
        uint64_t t11 = gm_l1;
        uint8_t t12 = (!(t11 == ((uint64_t)0ULL)));
        if (!t12) goto ie10;
        uint64_t t13 = gm_l1;
        uint64_t r14; bool o15 = __builtin_sub_overflow(t13, ((uint64_t)1ULL), &r14);
        if (o15) gm_trap("integer overflow in `-`", gm_src4mnsbede6fos, 105, 31);
        uint64_t t16 = r14;
        uint64_t t17 = (uintptr_t)t16;
        uint64_t t18 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        if (t17 >= (uintptr_t)(t18)) gm_trap_index(t17, t18, gm_src4mnsbede6fos, 105, 24);
        uint8_t t19 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t17 * 1));
        uint8_t t20 = (!(t19 == ((uint8_t)10ULL)));
        if (!t20) goto ie10;
        {
            gm_ttu09lij36f8c t22;
            memset(&t22, 0, sizeof t22);
            uint8_t t23 = 0;
            F.ret = t22;
            goto gm_fret;
        }
        goto ix10;
ie10:;
ix10:;
    }
    {
        uint64_t t24 = gm_l1;
        gm_l2 = t24;
    }
    {
        for (;;) {
            uint64_t t26 = gm_l2;
            uint64_t t27 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            uint8_t t28 = (t26 < t27);
            if (!t28) goto lb25;
            uint64_t t29 = gm_l2;
            uint64_t t30 = (uintptr_t)t29;
            uint64_t t31 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            if (t30 >= (uintptr_t)(t31)) gm_trap_index(t30, t31, gm_src4mnsbede6fos, 108, 32);
            uint8_t t32 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t30 * 1));
            uint8_t t33 = (!(t32 == ((uint8_t)10ULL)));
            if (!t33) goto lb25;
            {
                uint64_t t36 = gm_l2;
                uint64_t r37; bool o38 = __builtin_add_overflow(t36, ((uint64_t)1ULL), &r37);
                if (o38) gm_trap("integer overflow in `+`", gm_src4mnsbede6fos, 108, 58);
                uint64_t t39 = r37;
                gm_l2 = t39;
            }
lc25:;
        }
lb25:;
    }
    {
        uint64_t t40 = gm_l2;
        gm_l3 = t40;
    }
    {
        uint64_t t42 = gm_l3;
        uint64_t t43 = gm_l1;
        uint8_t t44 = (t42 > t43);
        if (!t44) goto ie41;
        uint64_t t45 = gm_l3;
        uint64_t r46; bool o47 = __builtin_sub_overflow(t45, ((uint64_t)1ULL), &r46);
        if (o47) gm_trap("integer overflow in `-`", gm_src4mnsbede6fos, 110, 33);
        uint64_t t48 = r46;
        uint64_t t49 = (uintptr_t)t48;
        uint64_t t50 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        if (t49 >= (uintptr_t)(t50)) gm_trap_index(t49, t50, gm_src4mnsbede6fos, 110, 26);
        uint8_t t51 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t49 * 1));
        uint8_t t52 = ((t51 == ((uint8_t)13ULL)));
        if (!t52) goto ie41;
        {
            uint64_t t54 = gm_l3;
            uint64_t r55; bool o56 = __builtin_sub_overflow(t54, ((uint64_t)1ULL), &r55);
            if (o56) gm_trap("integer overflow in `-`", gm_src4mnsbede6fos, 110, 57);
            uint64_t t57 = r55;
            gm_l3 = t57;
        }
        goto ix41;
ie41:;
ix41:;
    }
    {
        uint64_t t58 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        void * t59 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)));
        uint64_t t60 = gm_l1;
        uint64_t t61 = (uintptr_t)t60;
        uint64_t t62 = gm_l3;
        uint64_t t63 = (uintptr_t)t62;
        if (t61 > t63 || t63 > (uintptr_t)t58) gm_trap_slice(t61, t63, t58, gm_src4mnsbede6fos, 111, 13);
        gm_b16_8 t64;
        GM_ST(void *, (uint8_t *)&t64, (void *)(((uint8_t *)t59 + t61 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t64 + 8, (uintptr_t)(t63 - t61));
        gm_l4 = t64;
    }
    {
        gm_th2bqc58ss467 t65;
        memset(&t65, 0, sizeof t65);
        F.l5 = t65;
    }
    {
        gm_f_list_3po0a3n((void *)&F.l6, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        F.l7 = ((uint64_t)0ULL);
    }
    {
        {
            uint64_t t67 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
            void * t68 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)));
            uint64_t t69 = GM_LD(uintptr_t, ((uint8_t *)gm_co3qlejdntu) + 8);
            uint64_t t70 = (uintptr_t)t69;
            if (t70 > (uintptr_t)t67 || (uintptr_t)t67 > (uintptr_t)t67) gm_trap_slice(t70, (uintptr_t)t67, t67, gm_src4mnsbede6fos, 115, 28);
            gm_b16_8 t71;
            GM_ST(void *, (uint8_t *)&t71, (void *)(((uint8_t *)t68 + t70 * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t71 + 8, (uintptr_t)((uintptr_t)t67 - t70));
            gm_l8 = t71;
        }
        gm_hval t72 = ((gm_hval){(void *)&F.h, cx});
        gm_node wn73 = {.eff = 993381376, .kind = GM_LIT, .clauses = gm_hluov08dhrrb, .env = t72.env, .hctx = t72.cx, .with_frame = &F.h, .with_label = 1, .result = (void *)&F.w1, .fw = NULL, .next = cx};
        gm_b16_8 t76 = gm_l8;
        gm_b0_1 t77 = gm_f_fields_ojgi3et_sfcmm6t((void *)NULL, (&wn73), NULL, t76);
        if (gm_unw.kind) {
            goto gm_ll75;
        }
        F.w1 = t77;
        goto gm_ls78;
gm_ll75:;
        if (gm_unw.kind == GM_BREAK && gm_unw.frame == &F.h && gm_unw.label == 1) { gm_unw.kind = GM_NONE; goto we74; }
        goto gm_lad;
gm_ls78:;
we74:;
        gm_b0_1 t79 = F.w1;
        gm_b0_1 t80 = t79;
    }
    {
        gm_th2bqc58ss467 t81 = F.l5;
        gm_th2bqc58ss467 t82 = t81;
        double t83;
        if (((((uint8_t *)&t82))[8] != 0)) {
            double t84 = GM_LD(double, (((uint8_t *)&t82) + 0));
            t83 = t84;
        } else {
            gm_ttu09lij36f8c t85;
            memset(&t85, 0, sizeof t85);
            uint8_t t86 = 0;
            F.ret = t85;
            goto gm_fret;
        }
        gm_l9 = t83;
    }
    {
        uint8_t t88 = 0;
        uint64_t t89 = F.l7;
        uint8_t t90 = ((t89 == ((uint64_t)0ULL)));
        if (!t90) {
            double t91 = gm_l9;
            uint8_t t92 = (t91 <= ((double)0e0));
            t88 = t92;
        } else t88 = 1;
        if (!t88) goto ie87;
        {
            gm_ttu09lij36f8c t94;
            memset(&t94, 0, sizeof t94);
            uint8_t t95 = 0;
            F.ret = t94;
            goto gm_fret;
        }
        goto ix87;
ie87:;
ix87:;
    }
    {
        {
            gm_l11 = ((uint64_t)0ULL);
        }
        {
            uint64_t t97 = GM_LD(uintptr_t, (((uint8_t *)&F.l6) + 0) + 8);
            gm_l10 = t97;
        }
        {
            for (;;) {
                uint64_t t99 = gm_l11;
                uint64_t t100 = gm_l10;
                uint8_t t101 = (t99 < t100);
                if (!t101) goto lb98;
                {
                    uint64_t t104 = gm_l11;
                    uint64_t t105 = t104;
                    uint64_t t108 = GM_LD(uint64_t, ((uint8_t *)&t105));
                    gm_l12 = t108;
                    {
                        uint64_t t110 = gm_l12;
                        uint64_t t111 = (uintptr_t)t110;
                        uint64_t t112 = GM_LD(uintptr_t, (((uint8_t *)&F.l6) + 0) + 8);
                        if (t111 >= (uintptr_t)(t112)) gm_trap_index(t111, t112, gm_src4mnsbede6fos, 126, 37);
                        uint64_t t113 = gm_l12;
                        uint64_t t114 = (uintptr_t)t113;
                        uint64_t t115 = GM_LD(uintptr_t, (((uint8_t *)&F.l6) + 0) + 8);
                        if (t114 >= (uintptr_t)(t115)) gm_trap_index(t114, t115, gm_src4mnsbede6fos, 126, 56);
                        double t116 = GM_LD(double, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l6) + 0)) + t114 * 8));
                        double t117 = gm_l9;
                        double t118 = (t116 / t117);
                        GM_ST(double, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l6) + 0)) + t111 * 8), t118);
                    }
                    goto me106;
mn107:;
                    gm_trap("no match arm matched the scrutinee", gm_src4mnsbede6fos, 126, 5);
me106:;
                }
lc98:;
                {
                    uint64_t t120 = gm_l11;
                    uint64_t r121; bool o122 = __builtin_add_overflow(t120, ((uint64_t)1ULL), &r121);
                    uint64_t t123 = r121;
                    gm_l11 = t123;
                }
            }
lb98:;
        }
    }
    {
        uint64_t t124 = F.l7;
        double t125 = (double)(t124);
        gm_l13 = t125;
    }
    {
        gm_l14 = ((double)0e0);
    }
    {
        {
            gm_b16_8 t127 = GM_LD(gm_b16_8, (((uint8_t *)&F.l6) + 0));
            gm_l15 = t127;
        }
        {
            gm_l16 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t129 = gm_l16;
                uint64_t t130 = GM_LD(uintptr_t, ((uint8_t *)&gm_l15) + 8);
                uint8_t t131 = (t129 < t130);
                if (!t131) goto lb128;
                {
                    uint64_t t134 = gm_l16;
                    uint64_t t135 = (uintptr_t)t134;
                    uint64_t t136 = GM_LD(uintptr_t, ((uint8_t *)&gm_l15) + 8);
                    double t137 = GM_LD(double, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l15)) + t135 * 8));
                    double t138 = t137;
                    double t141 = GM_LD(double, ((uint8_t *)&t138));
                    gm_l17 = t141;
                    {
                        double t143 = gm_l14;
                        double t144 = gm_l17;
                        double t145 = (t143 + t144);
                        gm_l14 = t145;
                    }
                    goto me139;
mn140:;
                    gm_trap("no match arm matched the scrutinee", gm_src4mnsbede6fos, 129, 5);
me139:;
                }
lc128:;
                {
                    uint64_t t147 = gm_l16;
                    uint64_t r148; bool o149 = __builtin_add_overflow(t147, ((uint64_t)1ULL), &r148);
                    uint64_t t150 = r148;
                    gm_l16 = t150;
                }
            }
lb128:;
        }
    }
    {
        double t151 = gm_l14;
        double t152 = gm_l13;
        double t153 = (t151 / t152);
        gm_l18 = t153;
    }
    {
        gm_l19 = ((double)0e0);
    }
    {
        {
            gm_b16_8 t155 = GM_LD(gm_b16_8, (((uint8_t *)&F.l6) + 0));
            gm_l20 = t155;
        }
        {
            gm_l21 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t157 = gm_l21;
                uint64_t t158 = GM_LD(uintptr_t, ((uint8_t *)&gm_l20) + 8);
                uint8_t t159 = (t157 < t158);
                if (!t159) goto lb156;
                {
                    uint64_t t162 = gm_l21;
                    uint64_t t163 = (uintptr_t)t162;
                    uint64_t t164 = GM_LD(uintptr_t, ((uint8_t *)&gm_l20) + 8);
                    double t165 = GM_LD(double, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l20)) + t163 * 8));
                    double t166 = t165;
                    double t169 = GM_LD(double, ((uint8_t *)&t166));
                    gm_l22 = t169;
                    {
                        double t171 = gm_l19;
                        double t172 = gm_l22;
                        double t173 = gm_l18;
                        double t174 = (t172 - t173);
                        double t175 = gm_l22;
                        double t176 = gm_l18;
                        double t177 = (t175 - t176);
                        double t178 = (t174 * t177);
                        double t179 = (t171 + t178);
                        gm_l19 = t179;
                    }
                    goto me167;
mn168:;
                    gm_trap("no match arm matched the scrutinee", gm_src4mnsbede6fos, 132, 5);
me167:;
                }
lc156:;
                {
                    uint64_t t181 = gm_l21;
                    uint64_t r182; bool o183 = __builtin_add_overflow(t181, ((uint64_t)1ULL), &r182);
                    uint64_t t184 = r182;
                    gm_l21 = t184;
                }
            }
lb156:;
        }
    }
    {
        double t185 = gm_l19;
        double t186 = gm_l13;
        double t187 = (t185 / t186);
        gm_l23 = t187;
    }
    {
        double t188;
        double t190 = gm_l18;
        uint8_t t191 = (t190 > ((double)0e0));
        if (!t191) goto ie189;
        double t193 = gm_l23;
        double t194 = gm_f_sqrt_mar88n3(t193);
        double t195 = gm_l18;
        double t196 = (t194 / t195);
        double t197 = (t196 * ((double)1e2));
        double t198 = t197;
        t188 = t198;
        goto ix189;
ie189:;
        double t200 = ((double)0e0);
        t188 = t200;
ix189:;
        gm_l24 = t188;
    }
    {
        gm_tcir0vf4307lp t201;
        double t202 = gm_l18;
        gm_b16_8 t203 = gm_f_fmt_per_run_159rum2((void *)NULL, cx, NULL, t202);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_b16_8, (uint8_t *)&t201 + 0, t203);
        double t204 = gm_l24;
        GM_ST(double, (uint8_t *)&t201 + 16, t204);
        uint64_t t205 = F.l7;
        GM_ST(uint64_t, (uint8_t *)&t201 + 24, t205);
        double t206 = gm_l9;
        double t207 = (double)t206;
        double t208 = __builtin_trunc(t207);
        uint64_t t209 = __builtin_isnan(t207) ? (uint64_t)0 : (t208 < 0e0 ? ((uint64_t)0ULL) : (t208 >= 1.8446744073709552e19 ? ((uint64_t)18446744073709551615ULL) : (uint64_t)t208));
        GM_ST(uint64_t, (uint8_t *)&t201 + 32, t209);
        gm_tabb710d9mq2e t210;
        gm_f_format_tjld97n((void *)&t210, (void *)NULL, cx, NULL, &t201);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t211 = t210;
        if (((((uint8_t *)&t211))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t211) + 8, gm_src4mnsbede6fos, 135, 16);
        gm_b16_8 t212 = GM_LD(gm_b16_8, (((uint8_t *)&t211) + 8));
        gm_l25 = t212;
    }
    {
        uint64_t t213 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        void * t214 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)));
        uint64_t t215 = gm_l3;
        uint64_t t216 = (uintptr_t)t215;
        if (t216 > (uintptr_t)t213 || (uintptr_t)t213 > (uintptr_t)t213) gm_trap_slice(t216, (uintptr_t)t213, t213, gm_src4mnsbede6fos, 137, 13);
        gm_b16_8 t217;
        GM_ST(void *, (uint8_t *)&t217, (void *)(((uint8_t *)t214 + t216 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t217 + 8, (uintptr_t)((uintptr_t)t213 - t216));
        gm_l26 = t217;
    }
    {
        uint64_t t218 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        void * t219 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)));
        uint64_t t220 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t221 = gm_l1;
        uint64_t t222 = (uintptr_t)t221;
        if (t220 > t222 || t222 > (uintptr_t)t218) gm_trap_slice(t220, t222, t218, gm_src4mnsbede6fos, 138, 15);
        gm_b16_8 t223;
        GM_ST(void *, (uint8_t *)&t223, (void *)(((uint8_t *)t219 + t220 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t223 + 8, (uintptr_t)(t222 - t220));
        gm_l27 = t223;
    }
    {
        uint64_t t225 = GM_LD(uintptr_t, ((uint8_t *)&gm_l27) + 8);
        uint8_t t226 = (t225 > ((uint64_t)0ULL));
        if (!t226) goto ie224;
        {
            gm_t6j6smj7qoctu t228;
            gm_b16_8 t229 = gm_l25;
            gm_b16_8 t230 = t229;
            gm_b16_8 t231 = GM_LD(gm_b16_8, ((uint8_t *)&t230));
            GM_ST(gm_b16_8, (uint8_t *)&t228 + 0, t231);
            gm_b16_8 t232 = gm_l27;
            GM_ST(gm_b16_8, (uint8_t *)&t228 + 16, t232);
            gm_ttu09lij36f8c t233;
            GM_ST(uint8_t, (uint8_t *)&t233 + 33, 0);
            GM_ST(uint16_t, (uint8_t *)&t233 + 34, 0);
            GM_ST(uint32_t, (uint8_t *)&t233 + 36, 0);
            GM_ST(gm_t6j6smj7qoctu, ((uint8_t *)&t233), t228);
            ((uint8_t *)&t233)[32] = 1;
            uint8_t t234 = 0;
            F.ret = t233;
            goto gm_fret;
        }
        goto ix224;
ie224:;
ix224:;
    }
    {
        gm_l28 = ((uint64_t)0ULL);
    }
    {
        for (;;) {
            uint64_t t236 = gm_l28;
            uint64_t t237 = GM_LD(uintptr_t, ((uint8_t *)&gm_l26) + 8);
            uint8_t t238 = (t236 < t237);
            if (!t238) goto lb235;
            uint64_t t239 = gm_l28;
            uint64_t t240 = (uintptr_t)t239;
            uint64_t t241 = GM_LD(uintptr_t, ((uint8_t *)&gm_l26) + 8);
            if (t240 >= (uintptr_t)(t241)) gm_trap_index(t240, t241, gm_src4mnsbede6fos, 141, 31);
            uint8_t t242 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l26)) + t240 * 1));
            uint8_t t243 = ((t242 == ((uint8_t)10ULL)));
            if (!t243) goto lb235;
            {
                uint64_t t246 = gm_l28;
                uint64_t r247; bool o248 = __builtin_add_overflow(t246, ((uint64_t)1ULL), &r247);
                if (o248) gm_trap("integer overflow in `+`", gm_src4mnsbede6fos, 141, 52);
                uint64_t t249 = r247;
                gm_l28 = t249;
            }
lc235:;
        }
lb235:;
    }
    {
        gm_t6j6smj7qoctu t250;
        gm_b16_8 t251 = gm_l25;
        gm_b16_8 t252 = t251;
        gm_b16_8 t253 = GM_LD(gm_b16_8, ((uint8_t *)&t252));
        GM_ST(gm_b16_8, (uint8_t *)&t250 + 0, t253);
        uint64_t t254 = GM_LD(uintptr_t, ((uint8_t *)&gm_l26) + 8);
        void * t255 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l26)));
        uint64_t t256 = gm_l28;
        uint64_t t257 = (uintptr_t)t256;
        if (t257 > (uintptr_t)t254 || (uintptr_t)t254 > (uintptr_t)t254) gm_trap_slice(t257, (uintptr_t)t254, t254, gm_src4mnsbede6fos, 142, 34);
        gm_b16_8 t258;
        GM_ST(void *, (uint8_t *)&t258, (void *)(((uint8_t *)t255 + t257 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t258 + 8, (uintptr_t)((uintptr_t)t254 - t257));
        GM_ST(gm_b16_8, (uint8_t *)&t250 + 16, t258);
        gm_ttu09lij36f8c t259;
        GM_ST(uint8_t, (uint8_t *)&t259 + 33, 0);
        GM_ST(uint16_t, (uint8_t *)&t259 + 34, 0);
        GM_ST(uint32_t, (uint8_t *)&t259 + 36, 0);
        GM_ST(gm_t6j6smj7qoctu, ((uint8_t *)&t259), t250);
        ((uint8_t *)&t259)[32] = 1;
        uint8_t t260 = 0;
        F.ret = t259;
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
