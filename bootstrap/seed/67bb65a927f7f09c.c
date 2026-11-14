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
typedef struct gm_cs6mnftt383dnt { void * f0; void * f1; } gm_cs6mnftt383dnt;
typedef struct gm_cs9ufftihtbpo2 { uint32_t f0; uint32_t f1; } gm_cs9ufftihtbpo2;
typedef struct gm_csfq64m1m1rt6m { int64_t f0; } gm_csfq64m1m1rt6m;
typedef struct gm_csguankko4lmhd { uint32_t f0; } gm_csguankko4lmhd;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint64_t f0; } gm_t4ce0jd3tdro4;
typedef struct { uint8_t f0; uint8_t f1; uint8_t f2; uint8_t f3; uint8_t f4; uint8_t f5; } gm_t4q2b5ibsrdik;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint32_t f0; uint8_t f1; uint8_t pe[3]; } gm_t9805qtonbsag;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint32_t f0; uint32_t f1; } gm_tcc1rq4n1avb2;
typedef struct { uint32_t a[1]; } gm_tdvq40fmif5j0;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { double f0; uint8_t f1; uint8_t pe[7]; } gm_th2bqc58ss467;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { int64_t f0; } gm_tm98ap4112hee;
typedef struct { uint8_t f0; uint8_t p1[3]; union { int32_t v0; } f1; } gm_toe5fmr3vvf4d;
typedef struct { uint8_t a[4096]; } gm_tpbv7nqggnvqr;
typedef struct { uint8_t a[4]; } gm_tpvg3mjn3dp06;
typedef struct { uint8_t f0; uint8_t f1; } gm_tqs5p4n2tq19v;
typedef struct { uint8_t f0; uint8_t p1[3]; union { uint32_t v0; int32_t v1; } f1; } gm_tsoiri44d0i6q;
typedef struct { uint8_t f1; } gm_ttsm01t0d8srg;
_Static_assert(sizeof(gm_cs6mnftt383dnt) == 16 && _Alignof(gm_cs6mnftt383dnt) == 8 && offsetof(gm_cs6mnftt383dnt, f0) == 0 && offsetof(gm_cs6mnftt383dnt, f1) == 8, "layout of `Allocator`");
_Static_assert(sizeof(gm_cs9ufftihtbpo2) == 8 && _Alignof(gm_cs9ufftihtbpo2) == 4 && offsetof(gm_cs9ufftihtbpo2, f0) == 0 && offsetof(gm_cs9ufftihtbpo2, f1) == 4, "layout of `LineCol`");
_Static_assert(sizeof(gm_csfq64m1m1rt6m) == 8 && _Alignof(gm_csfq64m1m1rt6m) == 8 && offsetof(gm_csfq64m1m1rt6m, f0) == 0, "layout of `Dir`");
_Static_assert(sizeof(gm_csguankko4lmhd) == 4 && _Alignof(gm_csguankko4lmhd) == 4 && offsetof(gm_csguankko4lmhd, f0) == 0, "layout of `Mutex`");
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_t08olq97rscn3;
typedef struct { uint64_t f0; uint8_t f1; uint8_t p2[7]; uint64_t f2; gm_b16_8 f3; gm_b16_8 f4; uint8_t f5; uint8_t f6; uint8_t pe[6]; } gm_t0rfe1ou14h21;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; } gm_t0uuhaljsf410;
typedef struct { gm_b16_8 a[9]; } gm_t142fel6vb03l;
typedef struct { uint64_t f0; uint8_t f1; uint8_t p2[7]; uint64_t f2; gm_b16_8 f3; gm_b16_8 f4; uint8_t f5; uint8_t f6; uint8_t p7[6]; gm_b16_8 f7; } gm_t2t9klu4dfekp;
typedef struct { gm_b16_8 f0; uint64_t f1; int64_t f2; } gm_t2u4qamcbbgu0;
typedef struct { gm_b16_8 f0; uint64_t f1; } gm_t3877pk7lv748;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
_Static_assert(sizeof(gm_t4ce0jd3tdro4) == 8 && _Alignof(gm_t4ce0jd3tdro4) == 8 && offsetof(gm_t4ce0jd3tdro4, f0) == 0, "layout of `Instant`");
_Static_assert(sizeof(gm_t4q2b5ibsrdik) == 6 && _Alignof(gm_t4q2b5ibsrdik) == 1 && offsetof(gm_t4q2b5ibsrdik, f0) == 0 && offsetof(gm_t4q2b5ibsrdik, f1) == 1 && offsetof(gm_t4q2b5ibsrdik, f2) == 2 && offsetof(gm_t4q2b5ibsrdik, f3) == 3 && offsetof(gm_t4q2b5ibsrdik, f4) == 4 && offsetof(gm_t4q2b5ibsrdik, f5) == 5, "layout of `OpenOptions`");
typedef struct { gm_b16_8 f0; uint32_t f1; uint32_t f2; } gm_t5hdnnftkup6m;
typedef struct { uint64_t f0; gm_t9qkh0rsooc8d f1; uint8_t p2[7]; int64_t f2; uint32_t f3; uint8_t pe[4]; } gm_t5j14m8ru132p;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_toe5fmr3vvf4d v0; gm_b16_8 v1; } f1; } gm_t5uv522f0p612;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_toe5fmr3vvf4d v0; gm_tm98ap4112hee v1; } f1; } gm_t62ebbi2i5rkd;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
_Static_assert(sizeof(gm_t9805qtonbsag) == 8 && _Alignof(gm_t9805qtonbsag) == 4 && offsetof(gm_t9805qtonbsag, f0) == 0 && offsetof(gm_t9805qtonbsag, f1) == 4, "layout of `?u32`");
typedef struct { gm_b16_8 a[3]; } gm_t9bmh80edou9c;
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
_Static_assert(sizeof(gm_tcc1rq4n1avb2) == 8 && _Alignof(gm_tcc1rq4n1avb2) == 4 && offsetof(gm_tcc1rq4n1avb2, f0) == 0 && offsetof(gm_tcc1rq4n1avb2, f1) == 4, "layout of `LineCol`");
typedef struct { gm_b16_8 a[4]; } gm_tdmi4fk9nhqa7;
_Static_assert(sizeof(gm_tdvq40fmif5j0) == 4 && _Alignof(gm_tdvq40fmif5j0) == 4 && offsetof(gm_tdvq40fmif5j0, a) == 0, "layout of `[1]u32`");
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; uint64_t f1; } gm_te0mgnjdphs16;
typedef struct { uint64_t f0; gm_b16_8 f1; } gm_tf1bjg81u0glv;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; } gm_tfeab279c16rr;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { uint8_t f0; uint8_t f1; uint8_t f2; uint8_t p3[5]; gm_b16_8 f3; } gm_tftmsj5vtqvej;
_Static_assert(sizeof(gm_th2bqc58ss467) == 16 && _Alignof(gm_th2bqc58ss467) == 8 && offsetof(gm_th2bqc58ss467, f0) == 0 && offsetof(gm_th2bqc58ss467, f1) == 8, "layout of `?f64`");
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_toe5fmr3vvf4d v0; uint64_t v1; } f1; } gm_thf5kr0is9u9i;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_toe5fmr3vvf4d v0; gm_t9qkh0rsooc8d v1; } f1; } gm_tj5jn79kq9pnm;
typedef struct { gm_b16_8 a[5]; } gm_tjgkb0mi112kg;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; gm_b16_8 f4; gm_b16_8 f5; gm_b16_8 f6; gm_b16_8 f7; gm_b16_8 f8; gm_b16_8 f9; gm_b16_8 f10; } gm_tk5njge13tk1d;
typedef struct { gm_t9805qtonbsag f0; gm_t9805qtonbsag f1; gm_t9805qtonbsag f2; gm_t9805qtonbsag f3; } gm_tk8ebsa8coi4p;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; } f1; } gm_tl572d7579hns;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_toe5fmr3vvf4d v0; gm_tsoiri44d0i6q v1; } f1; } gm_tlmjd3nndlf3r;
_Static_assert(sizeof(gm_tm98ap4112hee) == 8 && _Alignof(gm_tm98ap4112hee) == 8 && offsetof(gm_tm98ap4112hee, f0) == 0, "layout of `File`");
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
_Static_assert(sizeof(gm_toe5fmr3vvf4d) == 8 && _Alignof(gm_toe5fmr3vvf4d) == 4 && offsetof(gm_toe5fmr3vvf4d, f0) == 0 && offsetof(gm_toe5fmr3vvf4d, f1) == 4, "layout of `IoError`");
_Static_assert(sizeof(gm_tpbv7nqggnvqr) == 4096 && _Alignof(gm_tpbv7nqggnvqr) == 1 && offsetof(gm_tpbv7nqggnvqr, a) == 0, "layout of `[4096]u8`");
_Static_assert(sizeof(gm_tpvg3mjn3dp06) == 4 && _Alignof(gm_tpvg3mjn3dp06) == 1 && offsetof(gm_tpvg3mjn3dp06, a) == 0, "layout of `[4]u8`");
typedef struct { gm_b16_8 f0; uint64_t f1; gm_b16_8 f2; } gm_tq3m5qgvkpje8;
_Static_assert(sizeof(gm_tqs5p4n2tq19v) == 2 && _Alignof(gm_tqs5p4n2tq19v) == 1 && offsetof(gm_tqs5p4n2tq19v, f0) == 0 && offsetof(gm_tqs5p4n2tq19v, f1) == 1, "layout of `(u8, u8)`");
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
_Static_assert(sizeof(gm_tsoiri44d0i6q) == 8 && _Alignof(gm_tsoiri44d0i6q) == 4 && offsetof(gm_tsoiri44d0i6q, f0) == 0 && offsetof(gm_tsoiri44d0i6q, f1) == 4, "layout of `ExitStatus`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t4ce0jd3tdro4 v0; } f1; } gm_ttiiv5vhiuvvr;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_toe5fmr3vvf4d v0; } f1; } gm_ttln0d0hd70en;
_Static_assert(sizeof(gm_ttsm01t0d8srg) == 1 && _Alignof(gm_ttsm01t0d8srg) == 1 && offsetof(gm_ttsm01t0d8srg, f1) == 0, "layout of `?()`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; gm_b16_8 f4; } gm_tud7oovfvnf43;
_Static_assert(sizeof(gm_t08olq97rscn3) == 32 && _Alignof(gm_t08olq97rscn3) == 8 && offsetof(gm_t08olq97rscn3, f0) == 0 && offsetof(gm_t08olq97rscn3, f1) == 16, "layout of `File`");
_Static_assert(sizeof(gm_t0rfe1ou14h21) == 64 && _Alignof(gm_t0rfe1ou14h21) == 8 && offsetof(gm_t0rfe1ou14h21, f0) == 0 && offsetof(gm_t0rfe1ou14h21, f1) == 8 && offsetof(gm_t0rfe1ou14h21, f2) == 16 && offsetof(gm_t0rfe1ou14h21, f3) == 24 && offsetof(gm_t0rfe1ou14h21, f4) == 40 && offsetof(gm_t0rfe1ou14h21, f5) == 56 && offsetof(gm_t0rfe1ou14h21, f6) == 57, "layout of `Target`");
typedef struct { gm_b16_8 f0; gm_tk5njge13tk1d f1; gm_b16_8 f2; uint8_t f3; uint8_t f4; uint8_t pe[6]; } gm_t0u6dopi4f21f;
_Static_assert(sizeof(gm_t0uuhaljsf410) == 64 && _Alignof(gm_t0uuhaljsf410) == 8 && offsetof(gm_t0uuhaljsf410, f0) == 0 && offsetof(gm_t0uuhaljsf410, f1) == 16 && offsetof(gm_t0uuhaljsf410, f2) == 32 && offsetof(gm_t0uuhaljsf410, f3) == 48, "layout of `(string, string, string, string)`");
_Static_assert(sizeof(gm_t142fel6vb03l) == 144 && _Alignof(gm_t142fel6vb03l) == 8 && offsetof(gm_t142fel6vb03l, a) == 0, "layout of `[9]string`");
typedef struct { gm_trqfvmjtt4sij f0; gm_b16_8 f1; gm_b16_8 f2; } gm_t1rs7l0f3pc4f;
_Static_assert(sizeof(gm_t2t9klu4dfekp) == 80 && _Alignof(gm_t2t9klu4dfekp) == 8 && offsetof(gm_t2t9klu4dfekp, f0) == 0 && offsetof(gm_t2t9klu4dfekp, f1) == 8 && offsetof(gm_t2t9klu4dfekp, f2) == 16 && offsetof(gm_t2t9klu4dfekp, f3) == 24 && offsetof(gm_t2t9klu4dfekp, f4) == 40 && offsetof(gm_t2t9klu4dfekp, f5) == 56 && offsetof(gm_t2t9klu4dfekp, f6) == 57 && offsetof(gm_t2t9klu4dfekp, f7) == 64, "layout of `(u64, bool, u64, string, string, bool, bool, string)`");
_Static_assert(sizeof(gm_t2u4qamcbbgu0) == 32 && _Alignof(gm_t2u4qamcbbgu0) == 8 && offsetof(gm_t2u4qamcbbgu0, f0) == 0 && offsetof(gm_t2u4qamcbbgu0, f1) == 16 && offsetof(gm_t2u4qamcbbgu0, f2) == 24, "layout of `(string, u64, i64)`");
typedef struct { gm_t9qkh0rsooc8d f0; gm_t9qkh0rsooc8d f1; gm_t9qkh0rsooc8d f2; uint8_t p3[5]; gm_th2lqbo77pvsc f3; gm_b16_8 f4; uint8_t f5; uint8_t f6; uint8_t p7[6]; gm_th2lqbo77pvsc f7; } gm_t32dckm4jfarl;
_Static_assert(sizeof(gm_t3877pk7lv748) == 24 && _Alignof(gm_t3877pk7lv748) == 8 && offsetof(gm_t3877pk7lv748, f0) == 0 && offsetof(gm_t3877pk7lv748, f1) == 16, "layout of `WasmReader`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; gm_trqfvmjtt4sij v1; } f1; } gm_t470aqkia5212;
_Static_assert(sizeof(gm_t5hdnnftkup6m) == 24 && _Alignof(gm_t5hdnnftkup6m) == 8 && offsetof(gm_t5hdnnftkup6m, f0) == 0 && offsetof(gm_t5hdnnftkup6m, f1) == 16 && offsetof(gm_t5hdnnftkup6m, f2) == 20, "layout of `(string, u32, u32)`");
_Static_assert(sizeof(gm_t5j14m8ru132p) == 32 && _Alignof(gm_t5j14m8ru132p) == 8 && offsetof(gm_t5j14m8ru132p, f0) == 0 && offsetof(gm_t5j14m8ru132p, f1) == 8 && offsetof(gm_t5j14m8ru132p, f2) == 16 && offsetof(gm_t5j14m8ru132p, f3) == 24, "layout of `FileInfo`");
_Static_assert(sizeof(gm_t5uv522f0p612) == 24 && _Alignof(gm_t5uv522f0p612) == 8 && offsetof(gm_t5uv522f0p612, f0) == 0 && offsetof(gm_t5uv522f0p612, f1) == 8, "layout of `IoError!string`");
_Static_assert(sizeof(gm_t62ebbi2i5rkd) == 16 && _Alignof(gm_t62ebbi2i5rkd) == 8 && offsetof(gm_t62ebbi2i5rkd, f0) == 0 && offsetof(gm_t62ebbi2i5rkd, f1) == 8, "layout of `IoError!File`");
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_t9bmh80edou9c) == 48 && _Alignof(gm_t9bmh80edou9c) == 8 && offsetof(gm_t9bmh80edou9c, a) == 0, "layout of `[3]string`");
typedef struct { void * f0; gm_trqfvmjtt4sij f1; gm_tk8ebsa8coi4p f2; gm_t0rfe1ou14h21 f3; } gm_ta4jakbus8leg;
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
typedef struct { uint8_t f0; uint8_t p1[7]; gm_trqfvmjtt4sij f1; } gm_tbci5ospk5nnn;
typedef struct { gm_tfeab279c16rr f0; uint8_t f1; uint8_t pe[7]; } gm_td6svc7gs468a;
_Static_assert(sizeof(gm_tdmi4fk9nhqa7) == 64 && _Alignof(gm_tdmi4fk9nhqa7) == 8 && offsetof(gm_tdmi4fk9nhqa7, a) == 0, "layout of `[4]string`");
_Static_assert(sizeof(gm_te0mgnjdphs16) == 16 && _Alignof(gm_te0mgnjdphs16) == 8 && offsetof(gm_te0mgnjdphs16, f0) == 0 && offsetof(gm_te0mgnjdphs16, f1) == 8, "layout of `Chunk`");
typedef struct { gm_b16_8 f0; uint64_t f1; gm_trqfvmjtt4sij f2; gm_t88rcucg1b7uk f3; uint8_t pe[4]; } gm_teg5ofd5qf10s;
_Static_assert(sizeof(gm_tf1bjg81u0glv) == 24 && _Alignof(gm_tf1bjg81u0glv) == 8 && offsetof(gm_tf1bjg81u0glv, f0) == 0 && offsetof(gm_tf1bjg81u0glv, f1) == 8, "layout of `(i: usize, x: string)`");
_Static_assert(sizeof(gm_tfeab279c16rr) == 48 && _Alignof(gm_tfeab279c16rr) == 8 && offsetof(gm_tfeab279c16rr, f0) == 0 && offsetof(gm_tfeab279c16rr, f1) == 16 && offsetof(gm_tfeab279c16rr, f2) == 32, "layout of `Toolchain`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_tftmsj5vtqvej) == 24 && _Alignof(gm_tftmsj5vtqvej) == 8 && offsetof(gm_tftmsj5vtqvej, f0) == 0 && offsetof(gm_tftmsj5vtqvej, f1) == 1 && offsetof(gm_tftmsj5vtqvej, f2) == 2 && offsetof(gm_tftmsj5vtqvej, f3) == 8, "layout of `Options`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tj5jn79kq9pnm v0; gm_b16_8 v1; } f1; } gm_tgvh15qshf643;
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
typedef struct { gm_te0mgnjdphs16 f0; uint8_t f1; uint8_t pe[7]; } gm_thdrvp9q0u7b5;
_Static_assert(sizeof(gm_thf5kr0is9u9i) == 16 && _Alignof(gm_thf5kr0is9u9i) == 8 && offsetof(gm_thf5kr0is9u9i, f0) == 0 && offsetof(gm_thf5kr0is9u9i, f1) == 8, "layout of `IoError!usize`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_toe5fmr3vvf4d v0; gm_t5j14m8ru132p v1; } f1; } gm_tih6h4avv5nrb;
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
_Static_assert(sizeof(gm_tj5jn79kq9pnm) == 12 && _Alignof(gm_tj5jn79kq9pnm) == 4 && offsetof(gm_tj5jn79kq9pnm, f0) == 0 && offsetof(gm_tj5jn79kq9pnm, f1) == 4, "layout of `ReadError`");
_Static_assert(sizeof(gm_tjgkb0mi112kg) == 80 && _Alignof(gm_tjgkb0mi112kg) == 8 && offsetof(gm_tjgkb0mi112kg, a) == 0, "layout of `[5]string`");
typedef struct { gm_tsoiri44d0i6q f0; gm_trqfvmjtt4sij f1; gm_trqfvmjtt4sij f2; uint8_t f3; uint8_t pe[7]; } gm_tjm4rm50v5mle;
_Static_assert(sizeof(gm_tk5njge13tk1d) == 176 && _Alignof(gm_tk5njge13tk1d) == 8 && offsetof(gm_tk5njge13tk1d, f0) == 0 && offsetof(gm_tk5njge13tk1d, f1) == 16 && offsetof(gm_tk5njge13tk1d, f2) == 32 && offsetof(gm_tk5njge13tk1d, f3) == 48 && offsetof(gm_tk5njge13tk1d, f4) == 64 && offsetof(gm_tk5njge13tk1d, f5) == 80 && offsetof(gm_tk5njge13tk1d, f6) == 96 && offsetof(gm_tk5njge13tk1d, f7) == 112 && offsetof(gm_tk5njge13tk1d, f8) == 128 && offsetof(gm_tk5njge13tk1d, f9) == 144 && offsetof(gm_tk5njge13tk1d, f10) == 160, "layout of `Parts`");
_Static_assert(sizeof(gm_tk8ebsa8coi4p) == 32 && _Alignof(gm_tk8ebsa8coi4p) == 4 && offsetof(gm_tk8ebsa8coi4p, f0) == 0 && offsetof(gm_tk8ebsa8coi4p, f1) == 8 && offsetof(gm_tk8ebsa8coi4p, f2) == 16 && offsetof(gm_tk8ebsa8coi4p, f3) == 24, "layout of `Ambient`");
_Static_assert(sizeof(gm_tl572d7579hns) == 24 && _Alignof(gm_tl572d7579hns) == 8 && offsetof(gm_tl572d7579hns, f0) == 0 && offsetof(gm_tl572d7579hns, f1) == 8, "layout of `string!()`");
typedef struct { gm_b16_8 f0; gm_trqfvmjtt4sij f1; } gm_tl7hpf5nlilqq;
_Static_assert(sizeof(gm_tlmjd3nndlf3r) == 12 && _Alignof(gm_tlmjd3nndlf3r) == 4 && offsetof(gm_tlmjd3nndlf3r, f0) == 0 && offsetof(gm_tlmjd3nndlf3r, f1) == 4, "layout of `IoError!ExitStatus`");
typedef struct { uint8_t f0; uint8_t f1; uint8_t f2; uint8_t p3[5]; gm_trqfvmjtt4sij f3; } gm_tm4vhr17lpr3p;
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
_Static_assert(sizeof(gm_tq3m5qgvkpje8) == 40 && _Alignof(gm_tq3m5qgvkpje8) == 8 && offsetof(gm_tq3m5qgvkpje8, f0) == 0 && offsetof(gm_tq3m5qgvkpje8, f1) == 16 && offsetof(gm_tq3m5qgvkpje8, f2) == 24, "layout of `(string, usize, string)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t08olq97rscn3 v0; } f1; } gm_tqgtd5gtgj65s;
typedef struct { gm_trqfvmjtt4sij f0; gm_trqfvmjtt4sij f1; gm_trqfvmjtt4sij f2; } gm_tr8dvlu9ik4gj;
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_ttiiv5vhiuvvr) == 16 && _Alignof(gm_ttiiv5vhiuvvr) == 8 && offsetof(gm_ttiiv5vhiuvvr, f0) == 0 && offsetof(gm_ttiiv5vhiuvvr, f1) == 8, "layout of `ParseError`");
_Static_assert(sizeof(gm_ttln0d0hd70en) == 12 && _Alignof(gm_ttln0d0hd70en) == 4 && offsetof(gm_ttln0d0hd70en, f0) == 0 && offsetof(gm_ttln0d0hd70en, f1) == 4, "layout of `IoError!()`");
typedef struct { gm_t6j6smj7qoctu f0; uint8_t f1; uint8_t pe[7]; } gm_ttu09lij36f8c;
_Static_assert(sizeof(gm_tud7oovfvnf43) == 80 && _Alignof(gm_tud7oovfvnf43) == 8 && offsetof(gm_tud7oovfvnf43, f0) == 0 && offsetof(gm_tud7oovfvnf43, f1) == 16 && offsetof(gm_tud7oovfvnf43, f2) == 32 && offsetof(gm_tud7oovfvnf43, f3) == 48 && offsetof(gm_tud7oovfvnf43, f4) == 64, "layout of `(string, string, string, string, string)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_ttiiv5vhiuvvr v0; uint64_t v1; } f1; } gm_tutnubt6u4hoq;
_Static_assert(sizeof(gm_t0u6dopi4f21f) == 216 && _Alignof(gm_t0u6dopi4f21f) == 8 && offsetof(gm_t0u6dopi4f21f, f0) == 0 && offsetof(gm_t0u6dopi4f21f, f1) == 16 && offsetof(gm_t0u6dopi4f21f, f2) == 192 && offsetof(gm_t0u6dopi4f21f, f3) == 208 && offsetof(gm_t0u6dopi4f21f, f4) == 209, "layout of `Generated`");
_Static_assert(sizeof(gm_t1rs7l0f3pc4f) == 72 && _Alignof(gm_t1rs7l0f3pc4f) == 8 && offsetof(gm_t1rs7l0f3pc4f, f0) == 0 && offsetof(gm_t1rs7l0f3pc4f, f1) == 40 && offsetof(gm_t1rs7l0f3pc4f, f2) == 56, "layout of `Link`");
_Static_assert(sizeof(gm_t32dckm4jfarl) == 80 && _Alignof(gm_t32dckm4jfarl) == 8 && offsetof(gm_t32dckm4jfarl, f0) == 0 && offsetof(gm_t32dckm4jfarl, f1) == 1 && offsetof(gm_t32dckm4jfarl, f2) == 2 && offsetof(gm_t32dckm4jfarl, f3) == 8 && offsetof(gm_t32dckm4jfarl, f4) == 32 && offsetof(gm_t32dckm4jfarl, f5) == 48 && offsetof(gm_t32dckm4jfarl, f6) == 49 && offsetof(gm_t32dckm4jfarl, f7) == 56, "layout of `SpawnOptions`");
_Static_assert(sizeof(gm_t470aqkia5212) == 48 && _Alignof(gm_t470aqkia5212) == 8 && offsetof(gm_t470aqkia5212, f0) == 0 && offsetof(gm_t470aqkia5212, f1) == 8, "layout of `string!List(string)`");
_Static_assert(sizeof(gm_ta4jakbus8leg) == 144 && _Alignof(gm_ta4jakbus8leg) == 8 && offsetof(gm_ta4jakbus8leg, f0) == 0 && offsetof(gm_ta4jakbus8leg, f1) == 8 && offsetof(gm_ta4jakbus8leg, f2) == 48 && offsetof(gm_ta4jakbus8leg, f3) == 80, "layout of `Prog`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tj5jn79kq9pnm v0; gm_tjm4rm50v5mle v1; } f1; } gm_tat31u3lvqjk4;
_Static_assert(sizeof(gm_tbci5ospk5nnn) == 48 && _Alignof(gm_tbci5ospk5nnn) == 8 && offsetof(gm_tbci5ospk5nnn, f0) == 0 && offsetof(gm_tbci5ospk5nnn, f1) == 8, "layout of `Int`");
typedef struct { gm_tr8dvlu9ik4gj f0; uint8_t f1; uint8_t pe[7]; } gm_tctputthhrk9k;
_Static_assert(sizeof(gm_td6svc7gs468a) == 56 && _Alignof(gm_td6svc7gs468a) == 8 && offsetof(gm_td6svc7gs468a, f0) == 0 && offsetof(gm_td6svc7gs468a, f1) == 48, "layout of `?Toolchain`");
_Static_assert(sizeof(gm_teg5ofd5qf10s) == 72 && _Alignof(gm_teg5ofd5qf10s) == 8 && offsetof(gm_teg5ofd5qf10s, f0) == 0 && offsetof(gm_teg5ofd5qf10s, f1) == 16 && offsetof(gm_teg5ofd5qf10s, f2) == 24 && offsetof(gm_teg5ofd5qf10s, f3) == 64, "layout of `CompilePool`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t08olq97rscn3 v0; gm_t0u6dopi4f21f v1; } f1; } gm_tetdkgoreqmn1;
_Static_assert(sizeof(gm_tgvh15qshf643) == 24 && _Alignof(gm_tgvh15qshf643) == 8 && offsetof(gm_tgvh15qshf643, f0) == 0 && offsetof(gm_tgvh15qshf643, f1) == 8, "layout of `ReadError![]u8`");
_Static_assert(sizeof(gm_thdrvp9q0u7b5) == 24 && _Alignof(gm_thdrvp9q0u7b5) == 8 && offsetof(gm_thdrvp9q0u7b5, f0) == 0 && offsetof(gm_thdrvp9q0u7b5, f1) == 16, "layout of `?Chunk`");
_Static_assert(sizeof(gm_tih6h4avv5nrb) == 40 && _Alignof(gm_tih6h4avv5nrb) == 8 && offsetof(gm_tih6h4avv5nrb, f0) == 0 && offsetof(gm_tih6h4avv5nrb, f1) == 8, "layout of `IoError!FileInfo`");
_Static_assert(sizeof(gm_tjm4rm50v5mle) == 96 && _Alignof(gm_tjm4rm50v5mle) == 8 && offsetof(gm_tjm4rm50v5mle, f0) == 0 && offsetof(gm_tjm4rm50v5mle, f1) == 8 && offsetof(gm_tjm4rm50v5mle, f2) == 48 && offsetof(gm_tjm4rm50v5mle, f3) == 88, "layout of `Output`");
_Static_assert(sizeof(gm_tl7hpf5nlilqq) == 56 && _Alignof(gm_tl7hpf5nlilqq) == 8 && offsetof(gm_tl7hpf5nlilqq, f0) == 0 && offsetof(gm_tl7hpf5nlilqq, f1) == 16, "layout of `Fix`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_toe5fmr3vvf4d v0; gm_thdrvp9q0u7b5 v1; } f1; } gm_tlbmih0s72in3;
_Static_assert(sizeof(gm_tm4vhr17lpr3p) == 48 && _Alignof(gm_tm4vhr17lpr3p) == 8 && offsetof(gm_tm4vhr17lpr3p, f0) == 0 && offsetof(gm_tm4vhr17lpr3p, f1) == 1 && offsetof(gm_tm4vhr17lpr3p, f2) == 2 && offsetof(gm_tm4vhr17lpr3p, f3) == 8, "layout of `Options`");
typedef struct { gm_tbci5ospk5nnn f0; gm_tbci5ospk5nnn f1; } gm_tngtuiuskn938;
_Static_assert(sizeof(gm_tqgtd5gtgj65s) == 40 && _Alignof(gm_tqgtd5gtgj65s) == 8 && offsetof(gm_tqgtd5gtgj65s, f0) == 0 && offsetof(gm_tqgtd5gtgj65s, f1) == 8, "layout of `Unsup!()`");
_Static_assert(sizeof(gm_tr8dvlu9ik4gj) == 120 && _Alignof(gm_tr8dvlu9ik4gj) == 8 && offsetof(gm_tr8dvlu9ik4gj, f0) == 0 && offsetof(gm_tr8dvlu9ik4gj, f1) == 40 && offsetof(gm_tr8dvlu9ik4gj, f2) == 80, "layout of `Extras`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; gm_t1rs7l0f3pc4f v1; } f1; } gm_tsghr07tq93d8;
typedef struct { gm_t0u6dopi4f21f f0; gm_b16_8 f1; gm_b16_8 f2; } gm_tt1457on428g4;
_Static_assert(sizeof(gm_ttu09lij36f8c) == 40 && _Alignof(gm_ttu09lij36f8c) == 8 && offsetof(gm_ttu09lij36f8c, f0) == 0 && offsetof(gm_ttu09lij36f8c, f1) == 32, "layout of `?Split`");
_Static_assert(sizeof(gm_tutnubt6u4hoq) == 24 && _Alignof(gm_tutnubt6u4hoq) == 8 && offsetof(gm_tutnubt6u4hoq, f0) == 0 && offsetof(gm_tutnubt6u4hoq, f1) == 8, "layout of `ParseError!usize`");
typedef struct { gm_tt1457on428g4 f0; uint8_t f1; uint8_t pe[7]; } gm_t5i6e9e007icp;
_Static_assert(sizeof(gm_tat31u3lvqjk4) == 104 && _Alignof(gm_tat31u3lvqjk4) == 8 && offsetof(gm_tat31u3lvqjk4, f0) == 0 && offsetof(gm_tat31u3lvqjk4, f1) == 8, "layout of `RunError!Output`");
_Static_assert(sizeof(gm_tctputthhrk9k) == 128 && _Alignof(gm_tctputthhrk9k) == 8 && offsetof(gm_tctputthhrk9k, f0) == 0 && offsetof(gm_tctputthhrk9k, f1) == 120, "layout of `?Extras`");
typedef struct { gm_t9qkh0rsooc8d f0; gm_t9qkh0rsooc8d f1; uint8_t f2; uint8_t f3; gm_t9qkh0rsooc8d f4; uint8_t p5[3]; gm_th2lqbo77pvsc f5; uint8_t f6; uint8_t f7; uint8_t f8; uint8_t p9[5]; gm_b16_8 f9; uint8_t f10; uint8_t f11; uint8_t p12[6]; gm_th2lqbo77pvsc f12; uint8_t f13; uint8_t f14; uint8_t p15[6]; uint64_t f15; gm_th2bqc58ss467 f16; uint8_t f17; uint8_t f18; uint8_t p19[6]; gm_th2lqbo77pvsc f19; gm_th2lqbo77pvsc f20; uint8_t f21; uint8_t f22; uint8_t p23[6]; gm_th2lqbo77pvsc f23; uint8_t f24; uint8_t p25[7]; gm_b16_8 f25; gm_b16_8 f26; gm_tctputthhrk9k f27; gm_th2lqbo77pvsc f28; gm_t0rfe1ou14h21 f29; gm_td6svc7gs468a f30; gm_th2lqbo77pvsc f31; gm_tm4vhr17lpr3p f32; } gm_tdkfkbgm73fql;
_Static_assert(sizeof(gm_tetdkgoreqmn1) == 224 && _Alignof(gm_tetdkgoreqmn1) == 8 && offsetof(gm_tetdkgoreqmn1, f0) == 0 && offsetof(gm_tetdkgoreqmn1, f1) == 8, "layout of `Unsup!Generated`");
_Static_assert(sizeof(gm_tlbmih0s72in3) == 32 && _Alignof(gm_tlbmih0s72in3) == 8 && offsetof(gm_tlbmih0s72in3, f0) == 0 && offsetof(gm_tlbmih0s72in3, f1) == 8, "layout of `IoError!?Chunk`");
_Static_assert(sizeof(gm_tngtuiuskn938) == 96 && _Alignof(gm_tngtuiuskn938) == 8 && offsetof(gm_tngtuiuskn938, f0) == 0 && offsetof(gm_tngtuiuskn938, f1) == 48, "layout of `Rat`");
_Static_assert(sizeof(gm_tsghr07tq93d8) == 80 && _Alignof(gm_tsghr07tq93d8) == 8 && offsetof(gm_tsghr07tq93d8, f0) == 0 && offsetof(gm_tsghr07tq93d8, f1) == 8, "layout of `string!Link`");
_Static_assert(sizeof(gm_tt1457on428g4) == 248 && _Alignof(gm_tt1457on428g4) == 8 && offsetof(gm_tt1457on428g4, f0) == 0 && offsetof(gm_tt1457on428g4, f1) == 216 && offsetof(gm_tt1457on428g4, f2) == 232, "layout of `Built`");
typedef struct { gm_tngtuiuskn938 f0; gm_tngtuiuskn938 f1; } gm_tu15p0hi2qlv6;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tbci5ospk5nnn v0; gm_tngtuiuskn938 v1; uint8_t v2; gm_tu15p0hi2qlv6 v3; } f1; } gm_t0ialelq3ql1k;
_Static_assert(sizeof(gm_t5i6e9e007icp) == 256 && _Alignof(gm_t5i6e9e007icp) == 8 && offsetof(gm_t5i6e9e007icp, f0) == 0 && offsetof(gm_t5i6e9e007icp, f1) == 248, "layout of `?Built`");
_Static_assert(sizeof(gm_tdkfkbgm73fql) == 592 && _Alignof(gm_tdkfkbgm73fql) == 8 && offsetof(gm_tdkfkbgm73fql, f0) == 0 && offsetof(gm_tdkfkbgm73fql, f1) == 1 && offsetof(gm_tdkfkbgm73fql, f2) == 2 && offsetof(gm_tdkfkbgm73fql, f3) == 3 && offsetof(gm_tdkfkbgm73fql, f4) == 4 && offsetof(gm_tdkfkbgm73fql, f5) == 8 && offsetof(gm_tdkfkbgm73fql, f6) == 32 && offsetof(gm_tdkfkbgm73fql, f7) == 33 && offsetof(gm_tdkfkbgm73fql, f8) == 34 && offsetof(gm_tdkfkbgm73fql, f9) == 40 && offsetof(gm_tdkfkbgm73fql, f10) == 56 && offsetof(gm_tdkfkbgm73fql, f11) == 57 && offsetof(gm_tdkfkbgm73fql, f12) == 64 && offsetof(gm_tdkfkbgm73fql, f13) == 88 && offsetof(gm_tdkfkbgm73fql, f14) == 89 && offsetof(gm_tdkfkbgm73fql, f15) == 96 && offsetof(gm_tdkfkbgm73fql, f16) == 104 && offsetof(gm_tdkfkbgm73fql, f17) == 120 && offsetof(gm_tdkfkbgm73fql, f18) == 121 && offsetof(gm_tdkfkbgm73fql, f19) == 128 && offsetof(gm_tdkfkbgm73fql, f20) == 152 && offsetof(gm_tdkfkbgm73fql, f21) == 176 && offsetof(gm_tdkfkbgm73fql, f22) == 177 && offsetof(gm_tdkfkbgm73fql, f23) == 184 && offsetof(gm_tdkfkbgm73fql, f24) == 208 && offsetof(gm_tdkfkbgm73fql, f25) == 216 && offsetof(gm_tdkfkbgm73fql, f26) == 232 && offsetof(gm_tdkfkbgm73fql, f27) == 248 && offsetof(gm_tdkfkbgm73fql, f28) == 376 && offsetof(gm_tdkfkbgm73fql, f29) == 400 && offsetof(gm_tdkfkbgm73fql, f30) == 464 && offsetof(gm_tdkfkbgm73fql, f31) == 520 && offsetof(gm_tdkfkbgm73fql, f32) == 544, "layout of `Options`");
_Static_assert(sizeof(gm_tu15p0hi2qlv6) == 192 && _Alignof(gm_tu15p0hi2qlv6) == 8 && offsetof(gm_tu15p0hi2qlv6, f0) == 0 && offsetof(gm_tu15p0hi2qlv6, f1) == 96, "layout of `(re: Rat, im: Rat)`");
_Static_assert(sizeof(gm_t0ialelq3ql1k) == 200 && _Alignof(gm_t0ialelq3ql1k) == 8 && offsetof(gm_t0ialelq3ql1k, f0) == 0 && offsetof(gm_t0ialelq3ql1k, f1) == 8, "layout of `Untyped`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t88rcucg1b7uk v0; uint32_t v1; uint32_t v2; gm_tiilmfd16v3fc v3; gm_tnpifv20irnph v4; uint32_t v5; gm_tcc1rq4n1avb2 v6; gm_t88rcucg1b7uk v7; gm_t0ialelq3ql1k v8; uint64_t v9; gm_t88rcucg1b7uk v10; } f1; } gm_t6qooom8qa2do;
_Static_assert(sizeof(gm_t6qooom8qa2do) == 208 && _Alignof(gm_t6qooom8qa2do) == 8 && offsetof(gm_t6qooom8qa2do, f0) == 0 && offsetof(gm_t6qooom8qa2do, f1) == 8, "layout of `DeclValue`");
struct gm_fr_base_cflags_irg71ui {
    gm_frame h;
    gm_t0rfe1ou14h21 l0; /* target */
    gm_trqfvmjtt4sij l1; /* flags */
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_build_flags_sn2j41p {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* flags */
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_build_object_r4906f3 {
    gm_frame h;
    gm_tdkfkbgm73fql l2; /* opts */
    gm_tfeab279c16rr l5; /* tc */
    gm_trqfvmjtt4sij l6; /* roots */
    gm_tt1457on428g4 l7; /* b */
    uint8_t ret;
};
struct gm_fr_build_vkmrjt3 {
    gm_frame h;
    gm_tdkfkbgm73fql l3; /* opts */
    uint8_t ret;
};
struct gm_fr_cached_program_5afqj27 {
    gm_frame h;
    gm_tdkfkbgm73fql l1; /* opts */
    gm_tr8dvlu9ik4gj l2; /* extras */
    gm_tfeab279c16rr l3; /* tc */
    gm_trqfvmjtt4sij l8; /* parts */
    gm_t142fel6vb03l l9; /* <temporary array> */
    gm_tdmi4fk9nhqa7 l10; /* <temporary array> */
    gm_tl572d7579hns ret;
};
struct gm_fr_clear_dvdem7p {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_compile_built_8o9nmfu {
    gm_frame h;
    gm_tdkfkbgm73fql l2; /* opts */
    gm_tfeab279c16rr l8; /* tc */
    gm_tr8dvlu9ik4gj l9; /* extras */
    gm_t1rs7l0f3pc4f l10; /* link */
    uint8_t ret;
};
struct gm_fr_compile_c_bkb1gkm {
    gm_frame h;
    gm_tdkfkbgm73fql l4; /* opts */
    gm_trqfvmjtt4sij l15; /* flags */
    gm_trqfvmjtt4sij l16; /* sources */
    gm_trqfvmjtt4sij l19; /* uflags */
    gm_trqfvmjtt4sij l27; /* c_files */
    gm_trqfvmjtt4sij l34; /* flags */
    gm_trqfvmjtt4sij l36; /* inputs */
    gm_trqfvmjtt4sij l37; /* objects */
    gm_trqfvmjtt4sij l41; /* unit_flags */
    gm_trqfvmjtt4sij l46; /* link_flags */
    gm_trqfvmjtt4sij l50; /* tail */
    gm_trqfvmjtt4sij l53; /* argv */
    uint8_t ret;
};
struct gm_fr_compile_cdm3hj7 {
    gm_frame h;
    gm_tdkfkbgm73fql l3; /* opts */
    uint8_t ret;
};
struct gm_fr_compile_entries_l9qlniu {
    gm_frame h;
    gm_tdkfkbgm73fql l4; /* opts */
    gm_tt1457on428g4 l5; /* b */
    uint8_t ret;
};
struct gm_fr_compile_object_k7kbtpr {
    gm_frame h;
    gm_tdkfkbgm73fql l3; /* opts */
    gm_trqfvmjtt4sij l8; /* flags */
    gm_tdmi4fk9nhqa7 l9; /* tail */
    gm_trqfvmjtt4sij l10; /* argv */
    uint8_t ret;
};
struct gm_fr_compile_objects_mu5secm {
    gm_frame h;
    gm_trqfvmjtt4sij l5; /* argvs */
    gm_tdmi4fk9nhqa7 l10; /* tail */
    gm_teg5ofd5qf10s l11; /* pool */
    gm_trqfvmjtt4sij l14; /* threads */
    uint8_t ret;
};
struct gm_fr_compile_one_4djvgo4 {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* err */
    gm_tpbv7nqggnvqr l3; /* buf */
    gm_tl572d7579hns ret;
};
struct gm_fr_compile_worker_5cv5pj9 {
    gm_frame h;
    gm_b0_1 ret;
    gm_b0_1 w0;
};
struct gm_fr_contains_gkki4kq {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_cpu_count_0o5882g {
    gm_frame h;
    uint32_t ret;
};
struct gm_fr_debug_flags_gv15c46 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_default_exe_194tkju {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_display_30kc1i9 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_emitted_units_7f3nie0 {
    gm_frame h;
    gm_ttu09lij36f8c ret;
};
struct gm_fr_empty_slice_4e376v7 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_5nn7ph0 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_d914i5u {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_fklmfmo {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ijtp9lo {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_iv39hj3 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ju26jbp {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_exit_code_dv2nn7j {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_extras_joquqpb {
    gm_frame h;
    gm_tr8dvlu9ik4gj ret;
};
struct gm_fr_f16c_flag_vpo7tss {
    gm_frame h;
    gm_t0rfe1ou14h21 l0; /* target */
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_file_name_em7v8lv {
    gm_frame h;
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_file_o15fqku {
    gm_frame h;
    void * ret;
};
struct gm_fr_generate_object_rgfvem4 {
    gm_frame h;
    gm_tetdkgoreqmn1 ret;
};
struct gm_fr_generate_program_9mr9pv8 {
    gm_frame h;
    gm_tdkfkbgm73fql l4; /* opts */
    gm_trqfvmjtt4sij l5; /* roots */
    gm_ta4jakbus8leg l9; /* p */
    gm_tetdkgoreqmn1 l11; /* result */
    gm_t0u6dopi4f21f l16; /* generated */
    gm_t5i6e9e007icp ret;
};
struct gm_fr_generate_tests_462i0us {
    gm_frame h;
    gm_tetdkgoreqmn1 ret;
};
struct gm_fr_has_extension_sdfv93u {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_heap_allocator_gihki37 {
    gm_frame h;
    gm_t3mf58gv9kf3n ret;
};
struct gm_fr_io_message_p7jbrea {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_is_empty_8l7niee {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_file_tcuv9rv {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_gcc_epb17a6 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_join_r8j2dp1 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_library_stamps_mjeqafa {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* dirs */
    gm_trqfvmjtt4sij l7; /* stamps */
    gm_tfeab279c16rr l14; /* <tmp> */
    gm_t9bmh80edou9c l15; /* <tmp> */
    gm_b16_8 ret;
};
struct gm_fr_list_242hv68 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_7h11c55 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_7vplt15 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_epvducg {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_fnnocfv {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
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
struct gm_fr_mutex_t7if00c {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_no_link_a2212mt {
    gm_frame h;
    gm_t1rs7l0f3pc4f ret;
};
struct gm_fr_opt_level_gnm5dcm {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_pid_token_c444nkv {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_report_unsupported_9rg6nq1 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_rt_flags_oeql34c {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* flags */
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_run_compiler_ulaqcb1 {
    gm_frame h;
    gm_tjm4rm50v5mle l3; /* result */
    uint8_t ret;
};
struct gm_fr_run_dg4eogs {
    gm_frame h;
    gm_tdkfkbgm73fql l2; /* opts */
    gm_tt1457on428g4 l6; /* b */
    gm_tpbv7nqggnvqr l10; /* buf */
    gm_trqfvmjtt4sij l12; /* runner */
    gm_trqfvmjtt4sij l13; /* argv */
    uint8_t ret;
};
struct gm_fr_run_error_gs1dinp {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_runtime_source_skilk06 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_runtime_text_37culso {
    gm_frame h;
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_stem_of_t9qmlhi {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_success_q3lb6vt {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_system_io_mcjcl9m {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_target_text_s4sugoj {
    gm_frame h;
    gm_t0rfe1ou14h21 l0; /* t */
    gm_b16_8 ret;
};
struct gm_fr_unit_count_dk0d2e4 {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_unit_path_3tdv4sg {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_unit_sources_329det7 {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* sources */
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_unlock_9f1vcl6 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_wasi_target_lp6g335 {
    gm_frame h;
    gm_t0rfe1ou14h21 l0; /* target */
    uint8_t ret;
};
struct gm_fr_windows_libs_hmrgs9s {
    gm_frame h;
    gm_t0rfe1ou14h21 l0; /* target */
    gm_trqfvmjtt4sij l1; /* libs */
    gm_tud7oovfvnf43 l2; /* <tmp> */
    gm_tjgkb0mi112kg l3; /* <tmp> */
    gm_b16_8 ret;
};
struct gm_fr_write_generated_c_kb09043 {
    gm_frame h;
    gm_tpbv7nqggnvqr l3; /* buf */
    uint8_t ret;
};
struct gm_fr_write_units_hc5dqp8 {
    gm_frame h;
    uint8_t ret;
};
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b);
inline bool gm_eq467d5cm1d5(const uint8_t *a, const uint8_t *b);
inline bool gm_eqff85ac10o3(const uint8_t *a, const uint8_t *b);
__attribute__((aligned(4))) gm_b16_8 gm_f_absolute_6pqhsaj(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_asyncify_bs28ebq(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1, uint8_t a2);
__attribute__((aligned(4))) void gm_f_base_cflags_irg71ui(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_build_flags_sn2j41p(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) uint8_t gm_f_build_object_r4906f3(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, const void * a2);
__attribute__((aligned(4))) uint8_t gm_f_build_vkmrjt3(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2, const void * a3);
__attribute__((aligned(4))) void gm_f_cache_dir_81h90qu(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_b16_8 gm_f_cache_key_tf6ebrd(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_cached_program_5afqj27(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_check_export_names_mirc4me(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_clear_dvdem7p(void * a0);
__attribute__((aligned(4))) void gm_f_command_b2u33fl(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2);
__attribute__((aligned(4))) uint8_t gm_f_compile_built_8o9nmfu(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, const void * a2);
__attribute__((aligned(4))) uint8_t gm_f_compile_c_bkb1gkm(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2, gm_b16_8 a3, const void * a4);
__attribute__((aligned(4))) uint8_t gm_f_compile_cdm3hj7(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2, const void * a3);
__attribute__((aligned(4))) uint8_t gm_f_compile_entries_l9qlniu(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, uint8_t a2, gm_b16_8 a3, const void * a4);
__attribute__((aligned(4))) uint8_t gm_f_compile_object_k7kbtpr(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_b16_8 a2, const void * a3);
__attribute__((aligned(4))) uint8_t gm_f_compile_objects_mu5secm(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2, gm_b16_8 a3, uint64_t a4);
__attribute__((aligned(4))) void gm_f_compile_one_4djvgo4(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_compile_worker_5cv5pj9(void * a0);
__attribute__((aligned(4))) void gm_f_compiler_version_hj63jgf(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) uint8_t gm_f_contains_gkki4kq(gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) uint32_t gm_f_cpu_count_0o5882g(void);
__attribute__((aligned(4))) gm_b16_8 gm_f_debug_flags_gv15c46(void * a0, const void * a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_debug_flags_gv15c46_c(void * a0, gm_th2lqbo77pvsc a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_default_exe_194tkju(gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_display_30kc1i9(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_emitted_units_7f3nie0(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_4e376v7(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_5nn7ph0(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_d914i5u(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_fklmfmo(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_iv39hj3(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ju26jbp(void);
__attribute__((aligned(4))) uint8_t gm_f_ends_with_biriu5l(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) uint32_t gm_f_entry_point_n2gu79u(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
__attribute__((aligned(4))) uint8_t gm_f_exit_code_dv2nn7j(gm_tsoiri44d0i6q a0);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_extend_0cs85gs(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_extend_7hs67uj(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_extend_vogsd72(void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) void gm_f_extras_joquqpb(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_f16c_flag_vpo7tss(void *gm_out, const void * a0);
__attribute__((aligned(4))) gm_th2lqbo77pvsc gm_f_f16c_flag_vpo7tss_c(gm_t0rfe1ou14h21 a0);
inline __attribute__((aligned(4))) void gm_f_file_name_em7v8lv(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) void * gm_f_file_o15fqku(void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_find_nab29co(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_format_09mcfes(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_12q2s8v(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_359meqs(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_4hckftk(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_5029eea(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_6gakl8s(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_7os4s22(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_a1j85ht(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_cncadt9(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_cv7qh05(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_ddl06ea(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_f2pdud5(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_i9dlnsg(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_jicque0(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_kdp6nh1(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_pf7dqps(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_q8i7gsb(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_rjqt2eo(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_generate_6no5tn1(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, const void * a2);
__attribute__((aligned(4))) void gm_f_generate_entries_br1bd7f(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, uint8_t a2, const void * a3);
inline __attribute__((aligned(4))) void gm_f_generate_object_rgfvem4(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_generate_program_9mr9pv8(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2, uint8_t a3, const void * a4);
inline __attribute__((aligned(4))) void gm_f_generate_tests_462i0us(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, const void * a2);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_get_espvf96(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) uint8_t gm_f_has_extension_sdfv93u(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_cs6mnftt383dnt gm_f_heap_allocator_gihki37(void);
__attribute__((aligned(4))) gm_b16_8 gm_f_hex_euih242(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_io_message_p7jbrea(void *env, gm_node *cx, gm_node *inst, gm_toe5fmr3vvf4d a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_empty_8l7niee(void * a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_file_tcuv9rv(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_gcc_epb17a6(const void * a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_join_qveh5b2(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) void gm_f_join_r8j2dp1(gm_csfq64m1m1rt6m a0);
__attribute__((aligned(4))) void gm_f_join_sfknljb(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_library_args_lijr5kp(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_library_stamps_mjeqafa(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_cs9ufftihtbpo2 gm_f_line_col_9vn24mu(void * a0, uint32_t a1);
__attribute__((aligned(4))) void gm_f_link_inputs_1dv03h3(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_b16_8 a2, uint8_t a3, uint8_t a4);
inline __attribute__((aligned(4))) void gm_f_list_242hv68(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_7h11c55(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_7vplt15(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_epvducg(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_fnnocfv(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_go2qg0b(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_lock_u5lks01(void * a0);
inline __attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_lookup_59juqoo(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_make_dirs_gbp8nfr(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_make_temp_dir_vi8f88d(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b16_8 gm_f_mode_name_vpt4nrc(gm_t9qkh0rsooc8d a0);
inline __attribute__((aligned(4))) gm_csguankko4lmhd gm_f_mutex_t7if00c(void);
__attribute__((aligned(4))) gm_b16_8 gm_f_native_exe_name_rqchv3n(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) void gm_f_no_link_a2212mt(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_b16_8 gm_f_opt_level_gnm5dcm(void * a0);
__attribute__((aligned(4))) uint64_t gm_f_optimized_unit_count_5mur81l(void * a0, uint64_t a1, gm_tf5srh4mp4b3i a2);
__attribute__((aligned(4))) gm_b16_8 gm_f_os_error_text_319eh6h(void *env, gm_node *cx, gm_node *inst, gm_toe5fmr3vvf4d a0);
__attribute__((aligned(4))) void gm_f_output_mg6eie5(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, const void * a1);
__attribute__((aligned(4))) void gm_f_parse_int_3ce02np(void *gm_out, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_pid_token_c444nkv(void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) uint8_t gm_f_prepare_codegen_4hnlio1(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_private_name_u7i0rip(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_prog_61arbs1(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_prune_eu93ghe(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_0hkhfv5(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_3ef746b(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_6ct7n6b(void * a0, gm_csfq64m1m1rt6m a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_fp59o9c(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_im9d4ro(void * a0, uint32_t a1);
__attribute__((aligned(4))) void gm_f_read_all_r9rh9qd(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_ttln0d0hd70en gm_f_remove_all_h0k1qkh(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_remove_objects_1vq49q8(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_ttsm01t0d8srg gm_f_rename_or_remove_l80dbfs(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_report_unsupported_9rg6nq1(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_resolve_decl_pus042v(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_resolve_lines_h4bf30f(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_rfind_ai5s2js(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_rt_flags_oeql34c(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) uint8_t gm_f_run_compiler_ulaqcb1(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) uint8_t gm_f_run_dg4eogs(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, const void * a2);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_run_error_gs1dinp(void *env, gm_node *cx, gm_node *inst, gm_tj5jn79kq9pnm a0);
__attribute__((aligned(4))) void gm_f_runner_for_79o4h4m(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_runtime_dir_qd07iff(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_runtime_object_oihv3ss(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2, gm_b16_8 a3, const void * a4);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_runtime_source_skilk06(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_runtime_text_37culso(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_t62ebbi2i5rkd gm_f_spawn_et0s5ea(void * a0, void * a1);
__attribute__((aligned(4))) void gm_f_split_by_file_hgjqgsp(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_split_cbk4ks8(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_split_jendjdm(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
__attribute__((aligned(4))) uint8_t gm_f_starts_with_6vvt01e(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tlmjd3nndlf3r gm_f_status_20hgo8k(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, const void * a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_std_root_e0juohb(void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_b16_8 gm_f_stem_of_t9qmlhi(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) uint8_t gm_f_success_q3lb6vt(gm_tsoiri44d0i6q a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_system_io_mcjcl9m(void);
__attribute__((aligned(4))) gm_b16_8 gm_f_target_text_s4sugoj(void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_temp_dir_i6apmsd(void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_b0_1 gm_f_touch_frggs6u(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) uint64_t gm_f_unit_count_dk0d2e4(void * a0, uint64_t a1, gm_tf5srh4mp4b3i a2);
__attribute__((aligned(4))) void gm_f_unit_objects_3f31tcd(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2, const void * a3, uint64_t a4, uint8_t a5);
__attribute__((aligned(4))) gm_b16_8 gm_f_unit_path_3tdv4sg(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_unit_sources_329det7(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
inline __attribute__((aligned(4))) void gm_f_unlock_9f1vcl6(void * a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_wasi_link_flags_6mu198c(void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) uint8_t gm_f_wasi_target_lp6g335(const void * a0);
__attribute__((aligned(4))) uint8_t gm_f_wasi_target_lp6g335_c(gm_t0rfe1ou14h21 a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_wasm_opt_version_hapilm6(void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_b16_8 gm_f_windows_libs_hmrgs9s(void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) gm_ttln0d0hd70en gm_f_write_all_o8ucaji(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) uint8_t gm_f_write_generated_c_kb09043(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) uint8_t gm_f_write_lldb_formatters_m0rjk7q(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) uint8_t gm_f_write_units_hc5dqp8(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_b16_8 a2);
GM_NOINLINE bool gm_pf454qfrpt6r(gm_node *cx, const void *d, uintptr_t n, const char *file, uint32_t line, uint32_t col);
GM_NOINLINE bool gm_pfu8dq5bikbh(gm_node *cx, const gm_type *d, const void *v, const char *file, uint32_t line, uint32_t col);
extern uint8_t gm_c1a92ugfgjv[112] __attribute__((aligned(8)));
extern uint8_t gm_c69snm9duhi[16] __attribute__((aligned(8)));
extern uint8_t gm_c7mc3uaor1r[80] __attribute__((aligned(8)));
extern uint8_t gm_c7mc3uaor1r_2[80] __attribute__((aligned(8)));
extern uint8_t gm_cc7b06pnl1q[1] __attribute__((aligned(1)));
extern uint8_t gm_cejn1dj0054[12] __attribute__((aligned(4)));
extern uint8_t gm_cfa8lmn3344[48] __attribute__((aligned(8)));
extern uint8_t gm_cgd1khc5sal[24] __attribute__((aligned(8)));
extern uint8_t gm_cgsmfcq937e[16] __attribute__((aligned(8)));
extern uint8_t gm_cl1ove1i7cb[16] __attribute__((aligned(8)));
extern uint8_t gm_clslitrb510[16] __attribute__((aligned(8)));
extern void *const gm_hd3g3ujqvah4[];
extern void *const gm_hd3t1u951rul[];
extern void *const gm_hdaqemfrshlp[];
extern void *const gm_hdf4dspmusp5[];
extern void *const gm_hdffkeesmui4[];
extern void *const gm_hdu8v6n5i2qd[];
extern void *const gm_hdvslis4o40u[];
extern const uint8_t gm_s048hmhpmbi7c[8];
extern const uint8_t gm_s06m3met1mb9v[3];
extern const uint8_t gm_s0pi0emdqd3vb[2];
extern const uint8_t gm_s0ucactj6hj0a[4];
extern const uint8_t gm_s1ctr86inn5iu[24];
extern const uint8_t gm_s29cvp3di8ou1[4];
extern const uint8_t gm_s2cgbvfh1b7rp[6];
extern const uint8_t gm_s3p0rbs9hc3lg[2];
extern const uint8_t gm_s3vboaodpmi9a[4];
extern const uint8_t gm_s463po27vtmnn[15];
extern const uint8_t gm_s4lj2bemc87jp[3];
extern const uint8_t gm_s4vd6e3mkiiso[3];
extern const uint8_t gm_s55g9qp4arkn7[8];
extern const uint8_t gm_s5g0cpgo64peg[104];
extern const uint8_t gm_s5hode9408atm[9];
extern const uint8_t gm_s5lqhiu2bn6of[8];
extern const uint8_t gm_s6077qnlc299h[4];
extern const uint8_t gm_s622mvvjraklk[43];
extern const uint8_t gm_s65qt8qo64cje[3];
extern const uint8_t gm_s6vtoqhgjb0nd[6];
extern const uint8_t gm_s7qmtc95nc11i[24];
extern const uint8_t gm_s9o66vhtid5af[4];
extern const uint8_t gm_sad95m298up5o[44];
extern const uint8_t gm_sbkvlhvo2a3ic[7];
extern const uint8_t gm_scq91tgm9lph4[6];
extern const uint8_t gm_sdahvc1997d01[14];
extern const uint8_t gm_sehokd57s045g[11];
extern const uint8_t gm_setv7ejg35cg9[43];
extern const uint8_t gm_sfss9c30cdmun[8];
extern const uint8_t gm_sftkp1r7uetfv[10];
extern const uint8_t gm_sg5khilhaakst[16];
extern const uint8_t gm_sg87p4r5apham[36];
extern const uint8_t gm_sguhoqhco0rfk[9];
extern const uint8_t gm_shgi3g6gdv99o[4];
extern const uint8_t gm_sidpdvffdra02[14];
extern const uint8_t gm_sir0litoa3fk0[18];
extern const uint8_t gm_sjcgs2v314rl7[19];
extern const uint8_t gm_sje4rbc88csdu[7];
extern const uint8_t gm_sk4cv563hhed7[2];
extern const uint8_t gm_skbmmrtbskpao[8];
extern const uint8_t gm_skm48m2qkkghv[116];
extern const uint8_t gm_skolmnfc0ccbd[2];
extern const uint8_t gm_skp6qpvju2t5u[19];
extern const uint8_t gm_sl3q52ep6aa53[2];
extern const uint8_t gm_slgjpcgk90nuf[5];
extern const uint8_t gm_slgmqgkioa4u6[3];
extern const uint8_t gm_sm8m7jldttgpj[38];
extern const uint8_t gm_smi2s23d2qqne[6];
extern const uint8_t gm_sn0peadng5i31[3];
extern const uint8_t gm_sn2furlv6e7jl[4];
extern const uint8_t gm_sn3ltks10rrv7[8];
extern const uint8_t gm_sn6vmked7l7pf[7];
extern const uint8_t gm_snekrjln06tfl[14];
extern const uint8_t gm_snenkhfev166s[3];
extern const uint8_t gm_so514oj5nbhcu[2];
extern const uint8_t gm_so78814tnt5g5[2];
extern const uint8_t gm_sog4rksrmsqd2[5];
extern const uint8_t gm_soo4rtckloi4d[13];
extern const uint8_t gm_sqd102436v4lo[9];
extern const uint8_t gm_sqdfr4k3vea5p[4];
extern const uint8_t gm_sr2df1mc0vbea[4];
extern const uint8_t gm_sr3et5tffndla[28];
extern const uint8_t gm_sr4qbrk6tlgaa[1];
extern const uint8_t gm_sr98hl2rjopi2[8];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_src3d0v42kavhv7[];
extern const char gm_src7u0d314efpdm[];
extern const char gm_srcc2qutubarto1[];
extern const char gm_srccfhjr4r9mdef[];
extern const char gm_srcmm27gepldtcm[];
extern const char gm_srcqr4ch56kv6bv[];
extern const char gm_srcrgc9bkquf6nr[];
extern const char gm_srct4i4cmesm57g[];
extern const char gm_srcv1nri2247d2r[];
extern const uint8_t gm_ssmr7jgj9e42t[25];
extern const uint8_t gm_st3ja0nqi37bv[2];
extern const uint8_t gm_st9f93lco88fg[3];
extern const uint8_t gm_svaibtfuu0gf2[48];
extern const uint8_t gm_svl13be5ca8rt[5];
extern const uint8_t gm_svtktff6b3snn[3];
extern const gm_type gm_ty0iqiubfm7e;
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
inline bool gm_eqff85ac10o3(const uint8_t *a, const uint8_t *b) {
    if (!gm_eq467d5cm1d5(a + 0, b + 0)) return false;
    if (!gm_eq467d5cm1d5(a + 4, b + 4)) return false;
    if (!gm_eq467d5cm1d5(a + 8, b + 8)) return false;
    return true;
}
inline __attribute__((aligned(4))) void gm_f_clear_dvdem7p(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_clear_dvdem7p F;
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
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 16)));
        GM_ST(uintptr_t, (uint8_t *)&t8 + 8, (uintptr_t)(t7 - t6));
        GM_ST(gm_b16_8, (((uint8_t *)t2) + 0), t8);
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
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
inline __attribute__((aligned(4))) uint32_t gm_f_cpu_count_0o5882g(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_cpu_count_0o5882g F;
    GM_STACK_CHECK(&F, 12u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    uint32_t t1 = gm_cpu_count();
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_display_30kc1i9(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_display_30kc1i9 F;
    void * gm_l0; /* tc */
    GM_STACK_CHECK(&F, 176u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    gm_b16_8 t2 = GM_LD(gm_b16_8, (((uint8_t *)t1) + 0));
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s3p0rbs9hc3lg));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(1));
    gm_tabb710d9mq2e t4;
    gm_f_join_sfknljb((void *)&t4, (void *)NULL, cx, NULL, t2, t3);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tabb710d9mq2e t5 = t4;
    if (((((uint8_t *)&t5))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t5) + 8, gm_src7u0d314efpdm, 175, 66);
    gm_b16_8 t6 = GM_LD(gm_b16_8, (((uint8_t *)&t5) + 8));
    gm_b16_8 t7 = t6;
    gm_b16_8 t8 = GM_LD(gm_b16_8, ((uint8_t *)&t7));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_4e376v7(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_4e376v7 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_fklmfmo(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_fklmfmo F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_iv39hj3(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_iv39hj3 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ju26jbp(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_ju26jbp F;
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
inline __attribute__((aligned(4))) void gm_f_extras_joquqpb(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_extras_joquqpb F;
    GM_STACK_CHECK(&F, 120u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_trqfvmjtt4sij t1;
    gm_f_list_7h11c55((void *)&t1, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_trqfvmjtt4sij t2;
    gm_f_list_epvducg((void *)&t2, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_trqfvmjtt4sij t3;
    gm_f_list_epvducg((void *)&t3, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    GM_ST(gm_trqfvmjtt4sij, (uint8_t *)&(*(gm_tr8dvlu9ik4gj *)gm_out) + 0, t1);
    GM_ST(gm_trqfvmjtt4sij, (uint8_t *)&(*(gm_tr8dvlu9ik4gj *)gm_out) + 40, t2);
    GM_ST(gm_trqfvmjtt4sij, (uint8_t *)&(*(gm_tr8dvlu9ik4gj *)gm_out) + 80, t3);
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
inline __attribute__((aligned(4))) void gm_f_f16c_flag_vpo7tss(void *gm_out, const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_f16c_flag_vpo7tss F;
    GM_STACK_CHECK_SHALLOW(&F, 130u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_th2lqbo77pvsc t1;
    uint8_t t3 = GM_LD(uint8_t, (((uint8_t *)a0) + 56));
    if (!t3) goto ie2;
    uint8_t t4 = (((GM_LD(uintptr_t, (((uint8_t *)a0) + 40) + 8) == 6) && memcmp(GM_LD(void *, (((uint8_t *)a0) + 40)), gm_sje4rbc88csdu, 6) == 0));
    if (!t4) goto ie2;
    gm_b16_8 t6;
    GM_ST(void *, (uint8_t *)&t6, (void *)(gm_sn6vmked7l7pf));
    GM_ST(uintptr_t, (uint8_t *)&t6 + 8, (uintptr_t)(6));
    gm_b16_8 t7 = t6;
    gm_th2lqbo77pvsc t8;
    GM_ST(uint8_t, (uint8_t *)&t8 + 17, 0);
    GM_ST(uint16_t, (uint8_t *)&t8 + 18, 0);
    GM_ST(uint32_t, (uint8_t *)&t8 + 20, 0);
    GM_ST(gm_b16_8, ((uint8_t *)&t8), t7);
    ((uint8_t *)&t8)[16] = 1;
    t1 = t8;
    goto ix2;
ie2:;
    gm_th2lqbo77pvsc t10;
    memset(&t10, 0, sizeof t10);
    gm_th2lqbo77pvsc t11 = t10;
    t1 = t11;
ix2:;
    F.ret = t1;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_file_name_em7v8lv(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_file_name_em7v8lv F;
    gm_b16_8 gm_l0; /* p */
    GM_STACK_CHECK(&F, 288u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t1 = gm_l0;
    gm_tl7hpf5nlilqq t2;
    gm_f_split_cbk4ks8((void *)&t2, (void *)NULL, cx, NULL, t1);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tl7hpf5nlilqq t3 = t2;
    gm_b16_8 t4 = GM_LD(gm_b16_8, ((((uint8_t *)&t3) + 16) + 0));
    gm_b16_8 t5 = t4;
    void * t6 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t5)));
    uint64_t t7 = (uintptr_t)GM_LD(uintptr_t, ((uint8_t *)&t5) + 8);
    uint64_t t8 = (t7 - 1);
    gm_th2lqbo77pvsc t9;
    if (t8 < t7) {
    gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)t6 + t8 * 16));
    gm_th2lqbo77pvsc t11;
    GM_ST(uint8_t, (uint8_t *)&t11 + 17, 0);
    GM_ST(uint16_t, (uint8_t *)&t11 + 18, 0);
    GM_ST(uint32_t, (uint8_t *)&t11 + 20, 0);
    GM_ST(gm_b16_8, ((uint8_t *)&t11), t10);
    ((uint8_t *)&t11)[16] = 1;
        t9 = t11;
    } else {
    gm_th2lqbo77pvsc t12;
    memset(&t12, 0, sizeof t12);
        t9 = t12;
    }
    F.ret = t9;
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
inline __attribute__((aligned(4))) void gm_f_generate_object_rgfvem4(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_generate_object_rgfvem4 F;
    void * gm_l0; /* p */
    gm_tftmsj5vtqvej gm_l1; /* opts */
    GM_STACK_CHECK(&F, 88u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&gm_l1, a1, sizeof gm_l1);
    {
        void * t2 = gm_l0;
        gm_b16_8 t3 = gm_f_empty_slice_d914i5u();
        gm_b16_8 t4 = t3;
        gm_b16_8 t5 = GM_LD(gm_b16_8, ((uint8_t *)&t4));
        gm_f_generate_entries_br1bd7f((void *)&(*(gm_tetdkgoreqmn1 *)gm_out), (void *)NULL, cx, NULL, t2, t5, ((uint8_t)0), &(*(gm_tftmsj5vtqvej *)((uint8_t *)&gm_l1)));
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
inline __attribute__((aligned(4))) void gm_f_generate_tests_462i0us(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, const void * a2) {
    struct gm_fr_generate_tests_462i0us F;
    void * gm_l0; /* p */
    gm_b16_8 gm_l1; /* entries */
    gm_tftmsj5vtqvej gm_l2; /* opts */
    GM_STACK_CHECK(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    memcpy(&gm_l2, a2, sizeof gm_l2);
    {
        void * t2 = gm_l0;
        gm_b16_8 t3 = gm_l1;
        gm_f_generate_entries_br1bd7f((void *)&(*(gm_tetdkgoreqmn1 *)gm_out), (void *)NULL, cx, NULL, t2, t3, ((uint8_t)1), &(*(gm_tftmsj5vtqvej *)((uint8_t *)&gm_l2)));
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
inline __attribute__((aligned(4))) gm_cs6mnftt383dnt gm_f_heap_allocator_gihki37(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_heap_allocator_gihki37 F;
    GM_STACK_CHECK_SHALLOW(&F, 64u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    void * t1 = (void *)((uint8_t *)gm_cgd1khc5sal);
    void * t2 = (void *)((uint8_t *)gm_cgd1khc5sal);
    gm_t3mf58gv9kf3n t3;
    GM_ST(void *, ((uint8_t *)&t3 + 0), t1);
    GM_ST(void *, ((uint8_t *)&t3 + 8), t2);
    F.ret = t3;
gm_fret:
    return GM_CONV(gm_cs6mnftt383dnt, F.ret);
gm_fprop:
    return GM_CONV(gm_cs6mnftt383dnt, F.ret);
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_io_message_p7jbrea(void *env, gm_node *cx, gm_node *inst, gm_toe5fmr3vvf4d a0) {
    struct gm_fr_io_message_p7jbrea F;
    gm_toe5fmr3vvf4d gm_l0; /* e */
    GM_STACK_CHECK(&F, 64u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_toe5fmr3vvf4d t1 = gm_l0;
    gm_b16_8 t2 = gm_f_os_error_text_319eh6h((void *)NULL, cx, NULL, t1);
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_empty_8l7niee(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_empty_8l7niee F;
    void * gm_l0; /* e */
    GM_STACK_CHECK_SHALLOW(&F, 63u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t t1 = 0;
    uint8_t t2 = 0;
    void * t3 = gm_l0;
    uint64_t t4 = GM_LD(uintptr_t, ((((uint8_t *)t3) + 0) + 0) + 8);
    uint8_t t5 = ((t4 == ((uint64_t)0ULL)));
    if (t5) {
        void * t6 = gm_l0;
        uint64_t t7 = GM_LD(uintptr_t, ((((uint8_t *)t6) + 40) + 0) + 8);
        uint8_t t8 = ((t7 == ((uint64_t)0ULL)));
        t2 = t8;
    } else t2 = 0;
    if (t2) {
        void * t9 = gm_l0;
        uint64_t t10 = GM_LD(uintptr_t, ((((uint8_t *)t9) + 80) + 0) + 8);
        uint8_t t11 = ((t10 == ((uint64_t)0ULL)));
        t1 = t11;
    } else t1 = 0;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_file_tcuv9rv(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_is_file_tcuv9rv F;
    gm_b16_8 gm_l0; /* p */
    gm_t5j14m8ru132p gm_l1; /* info */
    GM_STACK_CHECK(&F, 213u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_b16_8 t2 = gm_l0;
        gm_node *pn3 = gm_find(cx, 2126215853);
        if (!pn3) gm_trap("no handler installed for effect `Fs`", gm_srcc2qutubarto1, 234, 13);
        gm_tih6h4avv5nrb t4;
        ((void (*)(void *, void *, gm_node *, gm_node *, gm_b16_8))pn3->clauses[5])((void *)&t4, pn3->env, pn3->hctx, pn3, t2);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tih6h4avv5nrb t5 = t4;
        gm_t5j14m8ru132p t6;
        if (!((((uint8_t *)&t5))[0] != 0)) {
            gm_t5j14m8ru132p t7 = GM_LD(gm_t5j14m8ru132p, (((uint8_t *)&t5) + 8));
            t6 = t7;
        } else {
            uint8_t t8 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        gm_l1 = t6;
    }
    {
        uint8_t t9 = ((GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 8) + 0) == ((uint8_t)0ULL)));
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_gcc_epb17a6(const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_gcc_epb17a6 F;
    gm_th2lqbo77pvsc gm_l0; /* version */
    gm_b16_8 gm_l1; /* v */
    GM_STACK_CHECK_SHALLOW(&F, 192u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_th2lqbo77pvsc t2 = GM_LD(gm_th2lqbo77pvsc, ((uint8_t *)a0));
        gm_th2lqbo77pvsc t3 = t2;
        gm_b16_8 t4;
        if (((((uint8_t *)&t3))[16] != 0)) {
            gm_b16_8 t5 = GM_LD(gm_b16_8, (((uint8_t *)&t3) + 0));
            t4 = t5;
        } else {
            uint8_t t6 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        gm_l1 = t4;
    }
    {
        uint8_t t7 = 0;
        gm_b16_8 t8 = gm_l1;
        gm_b16_8 t9;
        GM_ST(void *, (uint8_t *)&t9, (void *)(gm_ssmr7jgj9e42t));
        GM_ST(uintptr_t, (uint8_t *)&t9 + 8, (uintptr_t)(24));
        uint8_t t10 = gm_f_contains_gkki4kq(t8, t9);
        if (t10) {
            gm_b16_8 t11 = gm_l1;
            gm_b16_8 t12;
            GM_ST(void *, (uint8_t *)&t12, (void *)(gm_s2cgbvfh1b7rp));
            GM_ST(uintptr_t, (uint8_t *)&t12 + 8, (uintptr_t)(5));
            uint8_t t13 = gm_f_contains_gkki4kq(t11, t12);
            uint8_t t14 = (uint8_t)!t13;
            t7 = t14;
        } else t7 = 0;
        uint8_t t15 = 0;
        F.ret = t7;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_join_r8j2dp1(gm_csfq64m1m1rt6m a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_join_r8j2dp1 F;
    gm_tm98ap4112hee gm_l0; /* t */
    GM_STACK_CHECK(&F, 24u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = GM_CONV(gm_tm98ap4112hee, a0);
    {
        {
            int64_t t3 = GM_LD(int64_t, (((uint8_t *)&gm_l0) + 0));
            int64_t t4 = gm_thread_join(t3);
        }
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_list_242hv68(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_242hv68 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_ju26jbp();
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
inline __attribute__((aligned(4))) void gm_f_list_7h11c55(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_7h11c55 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_fklmfmo();
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
inline __attribute__((aligned(4))) void gm_f_list_7vplt15(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_7vplt15 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_iv39hj3();
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
inline __attribute__((aligned(4))) void gm_f_list_fnnocfv(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_fnnocfv F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_4e376v7();
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
inline __attribute__((aligned(4))) gm_csguankko4lmhd gm_f_mutex_t7if00c(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_mutex_t7if00c F;
    GM_STACK_CHECK_SHALLOW(&F, 12u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_t88rcucg1b7uk t1;
    GM_ST(uint32_t, ((uint8_t *)&t1 + 0), ((uint32_t)0ULL));
    F.ret = t1;
gm_fret:
    return GM_CONV(gm_csguankko4lmhd, F.ret);
gm_fprop:
    return GM_CONV(gm_csguankko4lmhd, F.ret);
}
inline __attribute__((aligned(4))) void gm_f_no_link_a2212mt(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_no_link_a2212mt F;
    GM_STACK_CHECK(&F, 136u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_trqfvmjtt4sij t1;
    gm_f_list_epvducg((void *)&t1, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_b16_8 t2 = gm_f_empty_slice_5nn7ph0();
    gm_b16_8 t3 = t2;
    gm_b16_8 t4 = GM_LD(gm_b16_8, ((uint8_t *)&t3));
    gm_b16_8 t5 = gm_f_empty_slice_5nn7ph0();
    gm_b16_8 t6 = t5;
    gm_b16_8 t7 = GM_LD(gm_b16_8, ((uint8_t *)&t6));
    GM_ST(gm_trqfvmjtt4sij, (uint8_t *)&(*(gm_t1rs7l0f3pc4f *)gm_out) + 0, t1);
    GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t1rs7l0f3pc4f *)gm_out) + 40, t4);
    GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t1rs7l0f3pc4f *)gm_out) + 56, t7);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_run_error_gs1dinp(void *env, gm_node *cx, gm_node *inst, gm_tj5jn79kq9pnm a0) {
    struct gm_fr_run_error_gs1dinp F;
    gm_tj5jn79kq9pnm gm_l0; /* e */
    gm_toe5fmr3vvf4d gm_l1; /* io */
    GM_STACK_CHECK(&F, 118u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t1;
    uint8_t t4 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t4 != ((uint8_t)0ULL)) goto mn3;
    gm_toe5fmr3vvf4d t5 = GM_LD(gm_toe5fmr3vvf4d, (((uint8_t *)&gm_l0) + 4));
    gm_l1 = t5;
    gm_toe5fmr3vvf4d t6 = gm_l1;
    gm_b16_8 t7 = gm_f_io_message_p7jbrea((void *)NULL, cx, NULL, t6);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    t1 = t7;
    goto me2;
mn3:;
    uint8_t t9 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t9 != ((uint8_t)1ULL)) goto mn8;
    gm_b16_8 t10;
    GM_ST(void *, (uint8_t *)&t10, (void *)(gm_sidpdvffdra02));
    GM_ST(uintptr_t, (uint8_t *)&t10 + 8, (uintptr_t)(13));
    t1 = t10;
    goto me2;
mn8:;
    gm_trap("no match arm matched the scrutinee", gm_srcmm27gepldtcm, 211, 67);
me2:;
    F.ret = t1;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_runtime_source_skilk06(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_runtime_source_skilk06 F;
    void * gm_l0; /* b */
    GM_STACK_CHECK(&F, 216u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_t6j6smj7qoctu t1;
    void * t2 = gm_l0;
    gm_b16_8 t3 = GM_LD(gm_b16_8, (((uint8_t *)t2) + 216));
    GM_ST(gm_b16_8, (uint8_t *)&t1 + 0, t3);
    void * t4 = gm_l0;
    gm_b16_8 t5 = GM_LD(gm_b16_8, (((uint8_t *)t4) + 232));
    GM_ST(gm_b16_8, (uint8_t *)&t1 + 16, t5);
    gm_tabb710d9mq2e t6;
    gm_f_format_12q2s8v((void *)&t6, (void *)NULL, cx, NULL, &t1);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tabb710d9mq2e t7 = t6;
    if (((((uint8_t *)&t7))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t7) + 8, gm_srcqr4ch56kv6bv, 229, 64);
    gm_b16_8 t8 = GM_LD(gm_b16_8, (((uint8_t *)&t7) + 8));
    gm_b16_8 t9 = t8;
    gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
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
inline __attribute__((aligned(4))) uint8_t gm_f_success_q3lb6vt(gm_tsoiri44d0i6q a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_success_q3lb6vt F;
    gm_tsoiri44d0i6q gm_l0; /* s */
    uint32_t gm_l1; /* code */
    GM_STACK_CHECK_SHALLOW(&F, 27u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t2;
        uint8_t t5 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
        if (t5 != ((uint8_t)0ULL)) goto mn4;
        uint32_t t6 = GM_LD(uint32_t, (((uint8_t *)&gm_l0) + 4));
        gm_l1 = t6;
        uint32_t t7 = gm_l1;
        uint8_t t8 = ((t7 == ((uint32_t)0ULL)));
        t2 = t8;
        goto me3;
mn4:;
        uint8_t t10 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
        if (t10 != ((uint8_t)1ULL)) goto mn9;
        t2 = ((uint8_t)0);
        goto me3;
mn9:;
        gm_trap("no match arm matched the scrutinee", gm_srct4i4cmesm57g, 65, 12);
me3:;
        uint8_t t11 = 0;
        F.ret = t2;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_system_io_mcjcl9m(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_system_io_mcjcl9m F;
    GM_STACK_CHECK_SHALLOW(&F, 0u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b0_1 t1;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint64_t gm_f_unit_count_dk0d2e4(void * a0, uint64_t a1, gm_tf5srh4mp4b3i a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_unit_count_dk0d2e4 F;
    void * gm_l0; /* parts */
    uint64_t gm_l1; /* cpus */
    gm_tf5srh4mp4b3i gm_l2; /* forced */
    uint64_t gm_l3; /* n */
    GM_STACK_CHECK_SHALLOW(&F, 130u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        if (!((((uint8_t *)&gm_l2))[8] != 0)) goto ie2;
        uint64_t t3 = GM_LD(uint64_t, ((uint8_t *)&gm_l2));
        gm_l3 = t3;
        {
            uint64_t t5 = gm_l3;
            uint64_t t6 = (t5 < ((uint64_t)1ULL) ? ((uint64_t)1ULL) : (t5 > ((uint64_t)8ULL) ? ((uint64_t)8ULL) : t5));
            uint8_t t7 = 0;
            F.ret = t6;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t8 = gm_l0;
        uint64_t t9 = GM_LD(uintptr_t, (((uint8_t *)t8) + 64) + 8);
        if (((uint64_t)98304ULL) == 0) gm_trap("division by zero", gm_srccfhjr4r9mdef, 82, 13);
        uint64_t t10 = (uint64_t)(t9 / ((uint64_t)98304ULL));
        uint64_t t11 = gm_l1;
        uint64_t t12 = (t11 < ((uint64_t)1ULL) ? ((uint64_t)1ULL) : (t11 > ((uint64_t)8ULL) ? ((uint64_t)8ULL) : t11));
        uint64_t t13 = (t10 < ((uint64_t)1ULL) ? ((uint64_t)1ULL) : (t10 > t12 ? t12 : t10));
        uint8_t t14 = 0;
        F.ret = t13;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_unlock_9f1vcl6(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_unlock_9f1vcl6 F;
    void * gm_l0; /* m */
    GM_STACK_CHECK(&F, 55u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t3 = gm_l0;
        void * t4 = (void *)(((uint8_t *)t3) + 0);
        gm_t9qkh0rsooc8d t5;
        GM_ST(uint8_t, ((uint8_t *)&t5 + 0), ((uint8_t)2ULL));
        gm_t9qkh0rsooc8d t6 = t5;
        uint32_t t7 = __atomic_exchange_n(((uint32_t *)t4), ((uint32_t)0ULL), gm_ord[GM_LD(uint8_t, ((uint8_t *)&t6))]);
        uint8_t t8 = ((t7 == ((uint32_t)2ULL)));
        if (!t8) goto ie2;
        {
            {
                void * t11 = gm_l0;
                void * t12 = (void *)(((uint8_t *)t11) + 0);
                int64_t t13 = gm_futex_wake(t12, ((uint32_t)1ULL));
            }
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
inline __attribute__((aligned(4))) uint8_t gm_f_wasi_target_lp6g335(const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_wasi_target_lp6g335 F;
    GM_STACK_CHECK_SHALLOW(&F, 3u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    uint8_t t1 = (((GM_LD(uintptr_t, (((uint8_t *)a0) + 24) + 8) == 4) && memcmp(GM_LD(void *, (((uint8_t *)a0) + 24)), gm_svl13be5ca8rt, 4) == 0));
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_base_cflags_irg71ui(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_base_cflags_irg71ui F;
    GM_STACK_CHECK(&F, 66u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memcpy(&F.l0, a0, sizeof F.l0);
    {
        gm_f_list_epvducg((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        void * t3 = ((uint8_t *)gm_c1a92ugfgjv);
        if ((uintptr_t)0 > (uintptr_t)7 || (uintptr_t)7 > (uintptr_t)7) gm_trap_slice((uintptr_t)0, (uintptr_t)7, 7, gm_srcqr4ch56kv6bv, 260, 18);
        gm_b16_8 t4;
        GM_ST(void *, (uint8_t *)&t4, (void *)(((uint8_t *)t3 + (uintptr_t)0 * 16)));
        GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)((uintptr_t)7 - (uintptr_t)0));
        gm_tftb1odc739t5 t5 = gm_f_extend_vogsd72(t2, t4);
        gm_tftb1odc739t5 t6 = t5;
        if (((((uint8_t *)&t6))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t6) + 1, gm_srcqr4ch56kv6bv, 260, 5);
        gm_b0_1 t7 = GM_LD(gm_b0_1, (((uint8_t *)&t6) + 1));
    }
    {
        uint8_t t9 = (!((GM_LD(uintptr_t, (((uint8_t *)&F.l0) + 24) + 8) == 7) && memcmp(GM_LD(void *, (((uint8_t *)&F.l0) + 24)), gm_s048hmhpmbi7c, 7) == 0));
        if (!t9) goto ie8;
        uint8_t t10 = (!((GM_LD(uintptr_t, (((uint8_t *)&F.l0) + 24) + 8) == 4) && memcmp(GM_LD(void *, (((uint8_t *)&F.l0) + 24)), gm_svl13be5ca8rt, 4) == 0));
        if (!t10) goto ie8;
        {
            void * t12 = (void *)((uint8_t *)&F.l1);
            gm_b16_8 t13;
            GM_ST(void *, (uint8_t *)&t13, (void *)(gm_sguhoqhco0rfk));
            GM_ST(uintptr_t, (uint8_t *)&t13 + 8, (uintptr_t)(8));
            gm_tftb1odc739t5 t14 = gm_f_push_3ef746b(t12, t13);
            gm_tftb1odc739t5 t15 = t14;
            if (((((uint8_t *)&t15))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t15) + 1, gm_srcqr4ch56kv6bv, 261, 65);
            gm_b0_1 t16 = GM_LD(gm_b0_1, (((uint8_t *)&t15) + 1));
        }
        goto ix8;
ie8:;
ix8:;
    }
    {
        memmove(gm_out, ((uint8_t *)&F.l1), 40);
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
inline __attribute__((aligned(4))) void gm_f_build_flags_sn2j41p(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_build_flags_sn2j41p F;
    void * gm_l0; /* opts */
    gm_th2lqbo77pvsc gm_l1; /* version */
    gm_b16_8 gm_l3; /* f */
    GM_STACK_CHECK(&F, 265u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        gm_f_base_cflags_irg71ui((void *)&F.l2, (void *)NULL, cx, NULL, &(*(gm_t0rfe1ou14h21 *)(((uint8_t *)t2) + 400)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t3 = (void *)((uint8_t *)&F.l2);
        void * t4 = gm_l0;
        gm_b16_8 t5 = gm_f_opt_level_gnm5dcm(t4);
        gm_tftb1odc739t5 t6 = gm_f_push_3ef746b(t3, t5);
        gm_tftb1odc739t5 t7 = t6;
        if (((((uint8_t *)&t7))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t7) + 1, gm_srcqr4ch56kv6bv, 428, 5);
        gm_b0_1 t8 = GM_LD(gm_b0_1, (((uint8_t *)&t7) + 1));
    }
    {
        void * t9 = (void *)((uint8_t *)&F.l2);
        void * t10 = gm_l0;
        gm_b16_8 t11 = gm_f_debug_flags_gv15c46(t10, &(*(gm_th2lqbo77pvsc *)((uint8_t *)a1)));
        gm_tftb1odc739t5 t12 = gm_f_extend_vogsd72(t9, t11);
        gm_tftb1odc739t5 t13 = t12;
        if (((((uint8_t *)&t13))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t13) + 1, gm_srcqr4ch56kv6bv, 429, 5);
        gm_b0_1 t14 = GM_LD(gm_b0_1, (((uint8_t *)&t13) + 1));
    }
    {
        void * t16 = gm_l0;
        uint8_t t17 = GM_LD(uint8_t, (((uint8_t *)t16) + 33));
        if (!t17) goto ie15;
        {
            void * t19 = (void *)((uint8_t *)&F.l2);
            gm_b16_8 t20;
            GM_ST(void *, (uint8_t *)&t20, (void *)(gm_sn0peadng5i31));
            GM_ST(uintptr_t, (uint8_t *)&t20 + 8, (uintptr_t)(2));
            gm_tftb1odc739t5 t21 = gm_f_push_3ef746b(t19, t20);
            gm_tftb1odc739t5 t22 = t21;
            if (((((uint8_t *)&t22))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t22) + 1, gm_srcqr4ch56kv6bv, 430, 26);
            gm_b0_1 t23 = GM_LD(gm_b0_1, (((uint8_t *)&t22) + 1));
        }
        goto ix15;
ie15:;
ix15:;
    }
    {
        void * t25 = gm_l0;
        gm_th2lqbo77pvsc t26;
        gm_f_f16c_flag_vpo7tss((void *)&t26, &(*(gm_t0rfe1ou14h21 *)(((uint8_t *)t25) + 400)));
        gm_th2lqbo77pvsc t27 = t26;
        if (!((((uint8_t *)&t27))[16] != 0)) goto ie24;
        gm_b16_8 t28 = GM_LD(gm_b16_8, ((uint8_t *)&t27));
        gm_l3 = t28;
        {
            void * t30 = (void *)((uint8_t *)&F.l2);
            gm_b16_8 t31 = gm_l3;
            gm_tftb1odc739t5 t32 = gm_f_push_3ef746b(t30, t31);
            gm_tftb1odc739t5 t33 = t32;
            if (((((uint8_t *)&t33))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t33) + 1, gm_srcqr4ch56kv6bv, 431, 38);
            gm_b0_1 t34 = GM_LD(gm_b0_1, (((uint8_t *)&t33) + 1));
        }
        goto ix24;
ie24:;
ix24:;
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
inline __attribute__((aligned(4))) uint8_t gm_f_build_object_r4906f3(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, const void * a2) {
    struct gm_fr_build_object_r4906f3 F;
    void * gm_l0; /* s */
    gm_b16_8 gm_l1; /* path */
    gm_b16_8 gm_l3; /* o */
    gm_b16_8 gm_l4; /* obj */
    GM_STACK_CHECK(&F, 1836u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    memcpy(&F.l2, a2, sizeof F.l2);
    {
        gm_b16_8 t2;
        if (!(((((uint8_t *)&F.l2) + 8))[16] != 0)) goto ie3;
        gm_b16_8 t4 = GM_LD(gm_b16_8, (((uint8_t *)&F.l2) + 8));
        gm_l3 = t4;
        gm_b16_8 t6 = gm_l3;
        gm_b16_8 t7 = t6;
        t2 = t7;
        goto ix3;
ie3:;
        gm_b16_8 t9 = gm_l1;
        gm_b16_8 t10 = gm_f_stem_of_t9qmlhi((void *)NULL, cx, NULL, t9);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tnpifv20irnph t11;
        GM_ST(gm_b16_8, ((uint8_t *)&t11 + 0), t10);
        gm_tabb710d9mq2e t12;
        gm_f_format_pf7dqps((void *)&t12, (void *)NULL, cx, NULL, t11);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t13 = t12;
        if (((((uint8_t *)&t13))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t13) + 8, gm_srcqr4ch56kv6bv, 49, 49);
        gm_b16_8 t14 = GM_LD(gm_b16_8, (((uint8_t *)&t13) + 8));
        gm_b16_8 t15 = t14;
        gm_b16_8 t16 = t15;
        gm_b16_8 t17 = GM_LD(gm_b16_8, ((uint8_t *)&t16));
        t2 = t17;
ix3:;
        gm_l4 = t2;
    }
    {
        gm_td6svc7gs468a t18 = GM_LD(gm_td6svc7gs468a, (((uint8_t *)&F.l2) + 464));
        gm_td6svc7gs468a t19 = t18;
        gm_tfeab279c16rr t20;
        if (((((uint8_t *)&t19))[48] != 0)) {
            gm_tfeab279c16rr t21 = GM_LD(gm_tfeab279c16rr, (((uint8_t *)&t19) + 0));
            t20 = t21;
        } else {
            {
                gm_th2lqbo77pvsc t23 = GM_LD(gm_th2lqbo77pvsc, (((uint8_t *)&F.l2) + 520));
                gm_th2lqbo77pvsc t24 = t23;
                gm_b16_8 t25;
                if (((((uint8_t *)&t24))[16] != 0)) {
                    gm_b16_8 t26 = GM_LD(gm_b16_8, (((uint8_t *)&t24) + 0));
                    t25 = t26;
                } else {
                    gm_b16_8 t27;
                    GM_ST(void *, (uint8_t *)&t27, (void *)(gm_sr4qbrk6tlgaa));
                    GM_ST(uintptr_t, (uint8_t *)&t27 + 8, (uintptr_t)(0));
                    t25 = t27;
                }
                gm_b16_8 t28 = t25;
                if (gm_pf454qfrpt6r(cx, gm_s5lqhiu2bn6of, 7, gm_srcqr4ch56kv6bv, 51, 9)) {
                    goto gm_lad;
                }
                if (gm_pfu8dq5bikbh(cx, &gm_ty0iqiubfm7e, ((uint8_t *)&t28), gm_srcqr4ch56kv6bv, 51, 9)) {
                    goto gm_lad;
                }
                if (gm_pf454qfrpt6r(cx, gm_so514oj5nbhcu, 1, gm_srcqr4ch56kv6bv, 51, 9)) {
                    goto gm_lad;
                }
            }
            uint8_t t29 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        F.l5 = t20;
    }
    {
        void * t31 = gm_l0;
        uint64_t t32 = GM_LD(uintptr_t, ((((uint8_t *)t31) + 880) + 0) + 8);
        uint8_t t33 = ((t32 == ((uint64_t)0ULL)));
        if (!t33) goto ie30;
        {
            if (gm_pf454qfrpt6r(cx, gm_skm48m2qkkghv, 115, gm_srcqr4ch56kv6bv, 55, 9)) {
                goto gm_lad;
            }
        }
        {
            uint8_t t35 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        goto ix30;
ie30:;
ix30:;
    }
    {
        gm_f_list_go2qg0b((void *)&F.l6, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t36 = (void *)((uint8_t *)&F.l6);
        void * t37 = gm_l0;
        gm_b16_8 t38 = GM_LD(gm_b16_8, ((((uint8_t *)t37) + 920) + 0));
        gm_b16_8 t39 = t38;
        gm_b16_8 t40 = GM_LD(gm_b16_8, ((uint8_t *)&t39));
        gm_tftb1odc739t5 t41 = gm_f_extend_7hs67uj(t36, t40);
        gm_tftb1odc739t5 t42 = t41;
        if (((((uint8_t *)&t42))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t42) + 1, gm_srcqr4ch56kv6bv, 59, 5);
        gm_b0_1 t43 = GM_LD(gm_b0_1, (((uint8_t *)&t42) + 1));
    }
    {
        void * t44 = gm_l0;
        gm_b16_8 t45 = GM_LD(gm_b16_8, (((uint8_t *)&F.l6) + 0));
        gm_b16_8 t46 = t45;
        gm_b16_8 t47 = GM_LD(gm_b16_8, ((uint8_t *)&t46));
        gm_b16_8 t48 = gm_f_empty_slice_d914i5u();
        gm_b16_8 t49 = t48;
        gm_b16_8 t50 = GM_LD(gm_b16_8, ((uint8_t *)&t49));
        gm_t5i6e9e007icp t51;
        gm_f_generate_program_9mr9pv8((void *)&t51, (void *)NULL, cx, NULL, t44, t47, t50, ((uint8_t)0), &(*(gm_tdkfkbgm73fql *)((uint8_t *)&F.l2)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t5i6e9e007icp t52 = t51;
        gm_tt1457on428g4 t53;
        if (((((uint8_t *)&t52))[248] != 0)) {
            gm_tt1457on428g4 t54 = GM_LD(gm_tt1457on428g4, (((uint8_t *)&t52) + 0));
            t53 = t54;
        } else {
            uint8_t t55 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        F.l7 = t53;
    }
    {
        void * t56 = (void *)((uint8_t *)&F.l7);
        void * t57 = (void *)((uint8_t *)&F.l5);
        gm_b16_8 t58 = gm_l4;
        uint8_t t59 = gm_f_compile_object_k7kbtpr((void *)NULL, cx, NULL, t56, t57, t58, &(*(gm_tdkfkbgm73fql *)((uint8_t *)&F.l2)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t60 = 0;
        F.ret = t59;
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
inline __attribute__((aligned(4))) uint8_t gm_f_build_vkmrjt3(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2, const void * a3) {
    struct gm_fr_build_vkmrjt3 F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* root */
    gm_b16_8 gm_l2; /* path */
    gm_b16_8 gm_l4; /* o */
    gm_b16_8 gm_l5; /* exe */
    gm_b16_8 gm_l6; /* printers */
    GM_STACK_CHECK(&F, 520u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    memcpy(&F.l3, a3, sizeof F.l3);
    {
        uint8_t t3 = (((GM_LD(uintptr_t, ((((uint8_t *)&F.l3) + 400) + 24) + 8) == 12) && memcmp(GM_LD(void *, ((((uint8_t *)&F.l3) + 400) + 24)), gm_soo4rtckloi4d, 12) == 0));
        if (!t3) goto ie2;
        {
            void * t5 = gm_l0;
            gm_b16_8 t6 = gm_l2;
            uint8_t t7 = gm_f_build_object_r4906f3((void *)NULL, cx, NULL, t5, t6, &(*(gm_tdkfkbgm73fql *)((uint8_t *)&F.l3)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t8 = 0;
            F.ret = t7;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        gm_b16_8 t9;
        if (!(((((uint8_t *)&F.l3) + 8))[16] != 0)) goto ie10;
        gm_b16_8 t11 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 8));
        gm_l4 = t11;
        gm_b16_8 t13;
        gm_b16_8 t15 = gm_l4;
        uint8_t t16 = gm_f_has_extension_sdfv93u((void *)NULL, cx, NULL, t15);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (!t16) goto ie14;
        gm_b16_8 t18 = gm_l4;
        gm_b16_8 t19 = t18;
        t13 = t19;
        goto ix14;
ie14:;
        void * t21 = (void *)((uint8_t *)&F.l3);
        gm_b16_8 t22 = gm_l4;
        gm_b16_8 t23 = gm_f_native_exe_name_rqchv3n((void *)NULL, cx, NULL, t21, t22);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b16_8 t24 = t23;
        t13 = t24;
ix14:;
        gm_b16_8 t25 = t13;
        t9 = t25;
        goto ix10;
ie10:;
        void * t27 = (void *)((uint8_t *)&F.l3);
        gm_b16_8 t28 = gm_l2;
        gm_b16_8 t29 = gm_f_default_exe_194tkju(t28);
        gm_b16_8 t30 = gm_f_native_exe_name_rqchv3n((void *)NULL, cx, NULL, t27, t29);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b16_8 t31 = t30;
        t9 = t31;
ix10:;
        gm_l5 = t9;
    }
    {
        void * t33 = gm_l0;
        uint64_t t34 = gm_l1;
        gm_b16_8 t35 = gm_l5;
        uint8_t t36 = gm_f_compile_cdm3hj7((void *)NULL, cx, NULL, t33, t34, t35, &(*(gm_tdkfkbgm73fql *)((uint8_t *)&F.l3)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t37 = (uint8_t)!t36;
        if (!t37) goto ie32;
        {
            uint8_t t39 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        goto ix32;
ie32:;
ix32:;
    }
    {
        uint8_t t41 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 33));
        if (!t41) goto ie40;
        uint8_t t42 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 32));
        uint8_t t43 = (uint8_t)!t42;
        if (!t43) goto ie40;
        {
            gm_b16_8 t45;
            GM_ST(void *, (uint8_t *)&t45, (void *)(gm_skp6qpvju2t5u));
            GM_ST(uintptr_t, (uint8_t *)&t45 + 8, (uintptr_t)(18));
            gm_th2lqbo77pvsc t46;
            gm_f_runtime_text_37culso((void *)&t46, (void *)NULL, cx, NULL, t45);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_th2lqbo77pvsc t47 = t46;
            gm_b16_8 t48;
            if (((((uint8_t *)&t47))[16] != 0)) {
                gm_b16_8 t49 = GM_LD(gm_b16_8, (((uint8_t *)&t47) + 0));
                t48 = t49;
            } else {
                uint8_t t50 = 0;
                F.ret = ((uint8_t)0);
                goto gm_fret;
            }
            gm_l6 = t48;
        }
        {
            gm_b16_8 t51 = gm_l5;
            gm_b16_8 t52 = gm_l6;
            uint8_t t53 = gm_f_write_lldb_formatters_m0rjk7q((void *)NULL, cx, NULL, t51, t52);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t54 = 0;
            F.ret = t53;
            goto gm_fret;
        }
        goto ix40;
ie40:;
ix40:;
    }
    {
        uint8_t t55 = 0;
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
__attribute__((aligned(4))) void gm_f_cached_program_5afqj27(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_cached_program_5afqj27 F;
    void * gm_l0; /* b */
    gm_b16_8 gm_l4; /* cache */
    gm_b16_8 gm_l5; /* dir */
    gm_b16_8 gm_l6; /* version */
    gm_b16_8 gm_l7; /* libs */
    gm_b16_8 gm_l11; /* stem */
    gm_b16_8 gm_l12; /* exe */
    gm_b16_8 gm_l13; /* tmp */
    uint8_t gm_l14; /* built */
    uint64_t gm_l15; /* <tmp> */
    uint64_t gm_l16; /* <tmp> */
    uint64_t gm_l17; /* i */
    GM_STACK_CHECK(&F, 3296u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&F.l1, a1, sizeof F.l1);
    {
        gm_tctputthhrk9k t2 = GM_LD(gm_tctputthhrk9k, (((uint8_t *)&F.l1) + 248));
        gm_tctputthhrk9k t3 = t2;
        gm_tr8dvlu9ik4gj t4;
        if (((((uint8_t *)&t3))[120] != 0)) {
            gm_tr8dvlu9ik4gj t5 = GM_LD(gm_tr8dvlu9ik4gj, (((uint8_t *)&t3) + 0));
            t4 = t5;
        } else {
            gm_tr8dvlu9ik4gj t6;
            gm_f_extras_joquqpb((void *)&t6, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            t4 = t6;
        }
        F.l2 = t4;
    }
    {
        uint8_t t8 = 0;
        void * t9 = (void *)((uint8_t *)&F.l2);
        uint8_t t10 = gm_f_is_empty_8l7niee(t9);
        uint8_t t11 = (uint8_t)!t10;
        if (!t11) {
            uint8_t t12 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 32));
            t8 = t12;
        } else t8 = 1;
        if (!t8) goto ie7;
        {
            gm_tl572d7579hns t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 8, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            GM_ST(uint8_t, ((uint8_t *)&t14 + 0), ((uint8_t)1ULL));
            uint8_t t15 = 0;
            F.ret = t14;
            goto gm_fret;
        }
        goto ix7;
ie7:;
ix7:;
    }
    {
        gm_td6svc7gs468a t16 = GM_LD(gm_td6svc7gs468a, (((uint8_t *)&F.l1) + 464));
        gm_td6svc7gs468a t17 = t16;
        gm_tfeab279c16rr t18;
        if (((((uint8_t *)&t17))[48] != 0)) {
            gm_tfeab279c16rr t19 = GM_LD(gm_tfeab279c16rr, (((uint8_t *)&t17) + 0));
            t18 = t19;
        } else {
            gm_tl572d7579hns t20;
            GM_ST(uint8_t, (uint8_t *)&t20 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t20 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t20 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t20 + 8, 0);
            GM_ST(uint64_t, (uint8_t *)&t20 + 16, 0);
            GM_ST(uint8_t, ((uint8_t *)&t20 + 0), ((uint8_t)1ULL));
            uint8_t t21 = 0;
            F.ret = t20;
            goto gm_fret;
        }
        F.l3 = t18;
    }
    {
        gm_th2lqbo77pvsc t22;
        gm_f_cache_dir_81h90qu((void *)&t22, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_th2lqbo77pvsc t23 = t22;
        gm_b16_8 t24;
        if (((((uint8_t *)&t23))[16] != 0)) {
            gm_b16_8 t25 = GM_LD(gm_b16_8, (((uint8_t *)&t23) + 0));
            t24 = t25;
        } else {
            gm_tl572d7579hns t26;
            GM_ST(uint8_t, (uint8_t *)&t26 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t26 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t26 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t26 + 8, 0);
            GM_ST(uint64_t, (uint8_t *)&t26 + 16, 0);
            GM_ST(uint8_t, ((uint8_t *)&t26 + 0), ((uint8_t)1ULL));
            uint8_t t27 = 0;
            F.ret = t26;
            goto gm_fret;
        }
        gm_l4 = t24;
    }
    {
        gm_b16_8 t28 = gm_l4;
        gm_b16_8 t29;
        GM_ST(void *, (uint8_t *)&t29, (void *)(gm_sn2furlv6e7jl));
        GM_ST(uintptr_t, (uint8_t *)&t29 + 8, (uintptr_t)(3));
        gm_b16_8 t30 = gm_f_join_qveh5b2((void *)NULL, cx, NULL, t28, t29);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l5 = t30;
    }
    {
        void * t31 = (void *)((uint8_t *)&F.l3);
        gm_th2lqbo77pvsc t32;
        gm_f_compiler_version_hj63jgf((void *)&t32, (void *)NULL, cx, NULL, t31);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_th2lqbo77pvsc t33 = t32;
        gm_b16_8 t34;
        if (((((uint8_t *)&t33))[16] != 0)) {
            gm_b16_8 t35 = GM_LD(gm_b16_8, (((uint8_t *)&t33) + 0));
            t34 = t35;
        } else {
            gm_tl572d7579hns t36;
            GM_ST(uint8_t, (uint8_t *)&t36 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t36 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t36 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t36 + 8, 0);
            GM_ST(uint64_t, (uint8_t *)&t36 + 16, 0);
            GM_ST(uint8_t, ((uint8_t *)&t36 + 0), ((uint8_t)1ULL));
            uint8_t t37 = 0;
            F.ret = t36;
            goto gm_fret;
        }
        gm_l6 = t34;
    }
    {
        void * t38 = gm_l0;
        gm_b16_8 t39 = GM_LD(gm_b16_8, ((((uint8_t *)t38) + 0) + 192));
        gm_b16_8 t40;
        GM_ST(void *, (uint8_t *)&t40, (void *)(gm_s0pi0emdqd3vb));
        GM_ST(uintptr_t, (uint8_t *)&t40 + 8, (uintptr_t)(1));
        gm_tabb710d9mq2e t41;
        gm_f_join_sfknljb((void *)&t41, (void *)NULL, cx, NULL, t39, t40);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t42 = t41;
        if (((((uint8_t *)&t42))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t42) + 8, gm_srcqr4ch56kv6bv, 661, 13);
        gm_b16_8 t43 = GM_LD(gm_b16_8, (((uint8_t *)&t42) + 8));
        gm_l7 = t43;
    }
    {
        gm_f_list_epvducg((void *)&F.l8, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
    }
    {
        void * t44 = (void *)((uint8_t *)&F.l8);
        {
            gm_b16_8 t46;
            GM_ST(void *, (uint8_t *)&t46, (void *)(gm_sbkvlhvo2a3ic));
            GM_ST(uintptr_t, (uint8_t *)&t46 + 8, (uintptr_t)(6));
            void * t47 = gm_l0;
            gm_b16_8 t48 = GM_LD(gm_b16_8, (((uint8_t *)t47) + 216));
            void * t49 = gm_l0;
            gm_b16_8 t50 = GM_LD(gm_b16_8, (((uint8_t *)t49) + 232));
            void * t51 = gm_l0;
            gm_b16_8 t52 = GM_LD(gm_b16_8, ((((uint8_t *)t51) + 0) + 0));
            gm_b16_8 t53 = gm_l7;
            gm_b16_8 t54 = t53;
            gm_b16_8 t55 = GM_LD(gm_b16_8, ((uint8_t *)&t54));
            gm_t9qkh0rsooc8d t56 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&F.l1) + 4));
            gm_b16_8 t57 = gm_f_mode_name_vpt4nrc(t56);
            gm_b16_8 t58;
            uint8_t t60 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 33));
            if (!t60) goto ie59;
            gm_b16_8 t62;
            GM_ST(void *, (uint8_t *)&t62, (void *)(gm_sk4cv563hhed7));
            GM_ST(uintptr_t, (uint8_t *)&t62 + 8, (uintptr_t)(1));
            gm_b16_8 t63 = t62;
            t58 = t63;
            goto ix59;
ie59:;
            gm_b16_8 t65;
            GM_ST(void *, (uint8_t *)&t65, (void *)(gm_skolmnfc0ccbd));
            GM_ST(uintptr_t, (uint8_t *)&t65 + 8, (uintptr_t)(1));
            gm_b16_8 t66 = t65;
            t58 = t66;
ix59:;
            gm_b16_8 t67 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
            gm_b16_8 t68;
            GM_ST(void *, (uint8_t *)&t68, (void *)(gm_s0pi0emdqd3vb));
            GM_ST(uintptr_t, (uint8_t *)&t68 + 8, (uintptr_t)(1));
            gm_tabb710d9mq2e t69;
            gm_f_join_sfknljb((void *)&t69, (void *)NULL, cx, NULL, t67, t68);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t70 = t69;
            if (((((uint8_t *)&t70))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t70) + 8, gm_srcqr4ch56kv6bv, 663, 141);
            gm_b16_8 t71 = GM_LD(gm_b16_8, (((uint8_t *)&t70) + 8));
            gm_b16_8 t72 = t71;
            gm_b16_8 t73 = GM_LD(gm_b16_8, ((uint8_t *)&t72));
            gm_b16_8 t74 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 16));
            gm_b16_8 t75;
            GM_ST(void *, (uint8_t *)&t75, (void *)(gm_s0pi0emdqd3vb));
            GM_ST(uintptr_t, (uint8_t *)&t75 + 8, (uintptr_t)(1));
            gm_tabb710d9mq2e t76;
            gm_f_join_sfknljb((void *)&t76, (void *)NULL, cx, NULL, t74, t75);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t77 = t76;
            if (((((uint8_t *)&t77))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t77) + 8, gm_srcqr4ch56kv6bv, 663, 165);
            gm_b16_8 t78 = GM_LD(gm_b16_8, (((uint8_t *)&t77) + 8));
            gm_b16_8 t79 = t78;
            gm_b16_8 t80 = GM_LD(gm_b16_8, ((uint8_t *)&t79));
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t142fel6vb03l *)((uint8_t *)&F.l9)) + 0, t46);
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t142fel6vb03l *)((uint8_t *)&F.l9)) + 16, t48);
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t142fel6vb03l *)((uint8_t *)&F.l9)) + 32, t50);
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t142fel6vb03l *)((uint8_t *)&F.l9)) + 48, t52);
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t142fel6vb03l *)((uint8_t *)&F.l9)) + 64, t55);
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t142fel6vb03l *)((uint8_t *)&F.l9)) + 80, t57);
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t142fel6vb03l *)((uint8_t *)&F.l9)) + 96, t58);
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t142fel6vb03l *)((uint8_t *)&F.l9)) + 112, t73);
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t142fel6vb03l *)((uint8_t *)&F.l9)) + 128, t80);
        }
        void * t81 = ((uint8_t *)&F.l9);
        if ((uintptr_t)0 > (uintptr_t)9 || (uintptr_t)9 > (uintptr_t)9) gm_trap_slice((uintptr_t)0, (uintptr_t)9, 9, gm_srcqr4ch56kv6bv, 663, 18);
        gm_b16_8 t82;
        GM_ST(void *, (uint8_t *)&t82, (void *)(((uint8_t *)t81 + (uintptr_t)0 * 16)));
        GM_ST(uintptr_t, (uint8_t *)&t82 + 8, (uintptr_t)((uintptr_t)9 - (uintptr_t)0));
        gm_b16_8 t83 = t82;
        gm_tftb1odc739t5 t84 = gm_f_extend_vogsd72(t44, t83);
        gm_tftb1odc739t5 t85 = t84;
        if (((((uint8_t *)&t85))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t85) + 1, gm_srcqr4ch56kv6bv, 663, 5);
        gm_b0_1 t86 = GM_LD(gm_b0_1, (((uint8_t *)&t85) + 1));
    }
    {
    }
    {
        void * t87 = (void *)((uint8_t *)&F.l8);
        {
            gm_b16_8 t89 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 32));
            gm_b16_8 t90;
            GM_ST(void *, (uint8_t *)&t90, (void *)(gm_s0pi0emdqd3vb));
            GM_ST(uintptr_t, (uint8_t *)&t90 + 8, (uintptr_t)(1));
            gm_tabb710d9mq2e t91;
            gm_f_join_sfknljb((void *)&t91, (void *)NULL, cx, NULL, t89, t90);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t92 = t91;
            if (((((uint8_t *)&t92))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t92) + 8, gm_srcqr4ch56kv6bv, 664, 28);
            gm_b16_8 t93 = GM_LD(gm_b16_8, (((uint8_t *)&t92) + 8));
            gm_b16_8 t94 = t93;
            gm_b16_8 t95 = GM_LD(gm_b16_8, ((uint8_t *)&t94));
            gm_b16_8 t96 = gm_l6;
            gm_b16_8 t97 = gm_f_target_text_s4sugoj((void *)NULL, cx, NULL, &(*(gm_t0rfe1ou14h21 *)(((uint8_t *)&F.l1) + 400)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            void * t98 = gm_l0;
            gm_b16_8 t99 = GM_LD(gm_b16_8, ((((uint8_t *)t98) + 0) + 192));
            gm_b16_8 t100 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 32));
            gm_b16_8 t101 = gm_f_library_stamps_mjeqafa((void *)NULL, cx, NULL, t99, t100);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_tdmi4fk9nhqa7 *)((uint8_t *)&F.l10)) + 0, t95);
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_tdmi4fk9nhqa7 *)((uint8_t *)&F.l10)) + 16, t96);
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_tdmi4fk9nhqa7 *)((uint8_t *)&F.l10)) + 32, t97);
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_tdmi4fk9nhqa7 *)((uint8_t *)&F.l10)) + 48, t101);
        }
        void * t102 = ((uint8_t *)&F.l10);
        if ((uintptr_t)0 > (uintptr_t)4 || (uintptr_t)4 > (uintptr_t)4) gm_trap_slice((uintptr_t)0, (uintptr_t)4, 4, gm_srcqr4ch56kv6bv, 664, 18);
        gm_b16_8 t103;
        GM_ST(void *, (uint8_t *)&t103, (void *)(((uint8_t *)t102 + (uintptr_t)0 * 16)));
        GM_ST(uintptr_t, (uint8_t *)&t103 + 8, (uintptr_t)((uintptr_t)4 - (uintptr_t)0));
        gm_b16_8 t104 = t103;
        gm_tftb1odc739t5 t105 = gm_f_extend_vogsd72(t87, t104);
        gm_tftb1odc739t5 t106 = t105;
        if (((((uint8_t *)&t106))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t106) + 1, gm_srcqr4ch56kv6bv, 664, 5);
        gm_b0_1 t107 = GM_LD(gm_b0_1, (((uint8_t *)&t106) + 1));
    }
    {
        uint8_t t109 = (((GM_LD(uintptr_t, ((((uint8_t *)&F.l1) + 400) + 24) + 8) == 4) && memcmp(GM_LD(void *, ((((uint8_t *)&F.l1) + 400) + 24)), gm_svl13be5ca8rt, 4) == 0));
        if (!t109) goto ie108;
        {
            void * t111 = (void *)((uint8_t *)&F.l8);
            gm_b16_8 t112 = gm_f_wasm_opt_version_hapilm6((void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tftb1odc739t5 t113 = gm_f_push_3ef746b(t111, t112);
            gm_tftb1odc739t5 t114 = t113;
            if (((((uint8_t *)&t114))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t114) + 1, gm_srcqr4ch56kv6bv, 665, 35);
            gm_b0_1 t115 = GM_LD(gm_b0_1, (((uint8_t *)&t114) + 1));
        }
        goto ix108;
ie108:;
ix108:;
    }
    {
        gm_b16_8 t116 = GM_LD(gm_b16_8, (((uint8_t *)&F.l8) + 0));
        gm_b16_8 t117 = t116;
        gm_b16_8 t118 = GM_LD(gm_b16_8, ((uint8_t *)&t117));
        gm_b16_8 t119 = gm_f_cache_key_tf6ebrd((void *)NULL, cx, NULL, t118);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tnpifv20irnph t120;
        GM_ST(gm_b16_8, ((uint8_t *)&t120 + 0), t119);
        gm_tabb710d9mq2e t121;
        gm_f_format_jicque0((void *)&t121, (void *)NULL, cx, NULL, t120);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t122 = t121;
        if (((((uint8_t *)&t122))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t122) + 8, gm_srcqr4ch56kv6bv, 666, 13);
        gm_b16_8 t123 = GM_LD(gm_b16_8, (((uint8_t *)&t122) + 8));
        gm_l11 = t123;
    }
    {
        gm_b16_8 t124 = gm_l5;
        void * t125 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t126 = gm_l11;
        gm_b16_8 t127 = t126;
        gm_b16_8 t128 = GM_LD(gm_b16_8, ((uint8_t *)&t127));
        gm_b16_8 t129 = gm_f_native_exe_name_rqchv3n((void *)NULL, cx, NULL, t125, t128);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b16_8 t130 = gm_f_join_qveh5b2((void *)NULL, cx, NULL, t124, t129);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l12 = t130;
    }
    {
        gm_b16_8 t132 = gm_l12;
        uint8_t t133 = gm_f_is_file_tcuv9rv((void *)NULL, cx, NULL, t132);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (!t133) goto ie131;
        {
            gm_b16_8 t135 = gm_l12;
            gm_b0_1 t136 = gm_f_touch_frggs6u((void *)NULL, cx, NULL, t135);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_b16_8 t137 = gm_l12;
            gm_tl572d7579hns t138;
            GM_ST(uint8_t, (uint8_t *)&t138 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t138 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t138 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t138 + 0), ((uint8_t)0ULL));
            GM_ST(gm_b16_8, ((uint8_t *)&t138 + 8), t137);
            uint8_t t139 = 0;
            F.ret = t138;
            goto gm_fret;
        }
        goto ix131;
ie131:;
ix131:;
    }
    {
        gm_b16_8 t140 = gm_l5;
        gm_tl572d7579hns t141;
        gm_f_make_dirs_gbp8nfr((void *)&t141, (void *)NULL, cx, NULL, t140);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tl572d7579hns t142 = t141;
        gm_b0_1 t143;
        if (!((((uint8_t *)&t142))[0] != 0)) {
            gm_b0_1 t144 = GM_LD(gm_b0_1, (((uint8_t *)&t142) + 8));
            t143 = t144;
        } else {
            gm_tl572d7579hns t145;
            GM_ST(uint8_t, (uint8_t *)&t145 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t145 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t145 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t145 + 8, 0);
            GM_ST(uint64_t, (uint8_t *)&t145 + 16, 0);
            GM_ST(uint8_t, ((uint8_t *)&t145 + 0), ((uint8_t)1ULL));
            uint8_t t146 = 0;
            F.ret = t145;
            goto gm_fret;
        }
    }
    {
        gm_b16_8 t147 = gm_l5;
        gm_b16_8 t148 = gm_l11;
        gm_b16_8 t149 = t148;
        gm_b16_8 t150 = GM_LD(gm_b16_8, ((uint8_t *)&t149));
        gm_b16_8 t151;
        GM_ST(void *, (uint8_t *)&t151, (void *)(gm_s9o66vhtid5af));
        GM_ST(uintptr_t, (uint8_t *)&t151 + 8, (uintptr_t)(3));
        gm_b16_8 t152 = gm_f_private_name_u7i0rip((void *)NULL, cx, NULL, t147, t150, t151);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l13 = t152;
    }
    {
        void * t153 = gm_l0;
        gm_b16_8 t154 = gm_l13;
        uint8_t t155 = gm_f_compile_built_8o9nmfu((void *)NULL, cx, NULL, t153, t154, &(*(gm_tdkfkbgm73fql *)((uint8_t *)&F.l1)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l14 = t155;
    }
    {
        {
            gm_b16_8 t157 = gm_l13;
            gm_tnpifv20irnph t158;
            GM_ST(gm_b16_8, ((uint8_t *)&t158 + 0), t157);
            gm_tabb710d9mq2e t159;
            gm_f_format_cncadt9((void *)&t159, (void *)NULL, cx, NULL, t158);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t160 = t159;
            if (((((uint8_t *)&t160))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t160) + 8, gm_srcqr4ch56kv6bv, 676, 23);
            gm_b16_8 t161 = GM_LD(gm_b16_8, (((uint8_t *)&t160) + 8));
            gm_b16_8 t162 = t161;
            gm_b16_8 t163 = GM_LD(gm_b16_8, ((uint8_t *)&t162));
            gm_ttln0d0hd70en t164 = gm_f_remove_all_h0k1qkh((void *)NULL, cx, NULL, t163);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
    }
    {
        {
            gm_l16 = ((uint64_t)1ULL);
        }
        {
            gm_l15 = ((uint64_t)8ULL);
        }
        {
            for (;;) {
                uint64_t t167 = gm_l16;
                uint64_t t168 = gm_l15;
                uint8_t t169 = (t167 < t168);
                if (!t169) goto lb166;
                {
                    uint64_t t172 = gm_l16;
                    uint64_t t173 = t172;
                    uint64_t t176 = GM_LD(uint64_t, ((uint8_t *)&t173));
                    gm_l17 = t176;
                    {
                        {
                            gm_t3877pk7lv748 t179;
                            gm_b16_8 t180 = gm_l13;
                            GM_ST(gm_b16_8, (uint8_t *)&t179 + 0, t180);
                            uint64_t t181 = gm_l17;
                            GM_ST(uint64_t, (uint8_t *)&t179 + 16, t181);
                            gm_tabb710d9mq2e t182;
                            gm_f_format_4hckftk((void *)&t182, (void *)NULL, cx, NULL, &t179);
                            if (gm_unw.kind) {
                                goto gm_ll170;
                            }
                            gm_tabb710d9mq2e t183 = t182;
                            if (((((uint8_t *)&t183))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t183) + 8, gm_srcqr4ch56kv6bv, 677, 39);
                            gm_b16_8 t184 = GM_LD(gm_b16_8, (((uint8_t *)&t183) + 8));
                            gm_b16_8 t185 = t184;
                            gm_b16_8 t186 = GM_LD(gm_b16_8, ((uint8_t *)&t185));
                            gm_ttln0d0hd70en t187 = gm_f_remove_all_h0k1qkh((void *)NULL, cx, NULL, t186);
                            if (gm_unw.kind) {
                                goto gm_ll170;
                            }
                        }
                    }
                    goto me174;
mn175:;
                    gm_trap("no match arm matched the scrutinee", gm_srcqr4ch56kv6bv, 677, 5);
me174:;
                }
lc166:;
                {
                    uint64_t t189 = gm_l16;
                    uint64_t r190; bool o191 = __builtin_add_overflow(t189, ((uint64_t)1ULL), &r190);
                    uint64_t t192 = r190;
                    gm_l16 = t192;
                }
            }
gm_ll170:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb166; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc166; }
            }
            goto gm_lad;
lb166:;
        }
    }
    {
        uint8_t t194 = gm_l14;
        uint8_t t195 = (uint8_t)!t194;
        if (!t195) goto ie193;
        {
            {
                gm_b16_8 t198 = gm_l13;
                gm_ttln0d0hd70en t199 = gm_f_remove_all_h0k1qkh((void *)NULL, cx, NULL, t198);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
        }
        {
            gm_tl572d7579hns t200;
            GM_ST(uint8_t, (uint8_t *)&t200 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t200 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t200 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t200 + 8, 0);
            GM_ST(uint64_t, (uint8_t *)&t200 + 16, 0);
            GM_ST(uint8_t, ((uint8_t *)&t200 + 0), ((uint8_t)2ULL));
            uint8_t t201 = 0;
            F.ret = t200;
            goto gm_fret;
        }
        goto ix193;
ie193:;
ix193:;
    }
    {
        gm_b16_8 t202 = gm_l13;
        gm_b16_8 t203 = gm_l12;
        gm_ttsm01t0d8srg t204 = gm_f_rename_or_remove_l80dbfs((void *)NULL, cx, NULL, t202, t203);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_ttsm01t0d8srg t205 = t204;
        gm_b0_1 t206;
        if (((((uint8_t *)&t205))[0] != 0)) {
            gm_b0_1 t207 = GM_LD(gm_b0_1, (((uint8_t *)&t205) + 0));
            t206 = t207;
        } else {
            gm_tl572d7579hns t208;
            GM_ST(uint8_t, (uint8_t *)&t208 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t208 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t208 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t208 + 8, 0);
            GM_ST(uint64_t, (uint8_t *)&t208 + 16, 0);
            GM_ST(uint8_t, ((uint8_t *)&t208 + 0), ((uint8_t)1ULL));
            uint8_t t209 = 0;
            F.ret = t208;
            goto gm_fret;
        }
    }
    {
        gm_b16_8 t210 = gm_l5;
        gm_b0_1 t211 = gm_f_prune_eu93ghe((void *)NULL, cx, NULL, t210);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_b16_8 t212 = gm_l12;
        gm_tl572d7579hns t213;
        GM_ST(uint8_t, (uint8_t *)&t213 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t213 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t213 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t213 + 0), ((uint8_t)0ULL));
        GM_ST(gm_b16_8, ((uint8_t *)&t213 + 8), t212);
        uint8_t t214 = 0;
        F.ret = t213;
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
__attribute__((aligned(4))) uint8_t gm_f_compile_built_8o9nmfu(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, const void * a2) {
    struct gm_fr_compile_built_8o9nmfu F;
    void * gm_l0; /* b */
    gm_b16_8 gm_l1; /* exe */
    void * gm_l3; /* generated */
    gm_b16_8 gm_l4; /* c_file */
    gm_t6j6smj7qoctu gm_l5; /* split */
    gm_b16_8 gm_l6; /* code */
    gm_b16_8 gm_l7; /* text */
    gm_b16_8 gm_l11; /* e */
    GM_STACK_CHECK(&F, 2265u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    memcpy(&F.l2, a2, sizeof F.l2);
    {
        void * t2 = gm_l0;
        void * t3 = (void *)(((uint8_t *)t2) + 0);
        gm_l3 = t3;
    }
    {
        gm_b16_8 t4 = gm_l1;
        gm_tnpifv20irnph t5;
        GM_ST(gm_b16_8, ((uint8_t *)&t5 + 0), t4);
        gm_tabb710d9mq2e t6;
        gm_f_format_cncadt9((void *)&t6, (void *)NULL, cx, NULL, t5);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t7 = t6;
        if (((((uint8_t *)&t7))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t7) + 8, gm_srcqr4ch56kv6bv, 182, 15);
        gm_b16_8 t8 = GM_LD(gm_b16_8, (((uint8_t *)&t7) + 8));
        gm_l4 = t8;
    }
    {
        uint8_t t10 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 32));
        if (!t10) goto ie9;
        {
            void * t13 = gm_l0;
            uint8_t t14 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 33));
            gm_ttu09lij36f8c t15;
            gm_f_emitted_units_7f3nie0((void *)&t15, (void *)NULL, cx, NULL, t13, t14);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_ttu09lij36f8c t16 = t15;
            if (!((((uint8_t *)&t16))[32] != 0)) goto ie12;
            gm_t6j6smj7qoctu t17 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)&t16));
            gm_l5 = t17;
            {
                void * t19 = gm_l0;
                gm_b16_8 t20 = gm_l1;
                uint8_t t21 = gm_f_write_units_hc5dqp8((void *)NULL, cx, NULL, t19, &(*(gm_t6j6smj7qoctu *)((uint8_t *)&gm_l5)), t20);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t22 = 0;
                F.ret = t21;
                goto gm_fret;
            }
            goto ix12;
ie12:;
ix12:;
        }
        {
            gm_tfeab279c16rr t23;
            void * t24 = gm_l0;
            gm_b16_8 t25 = GM_LD(gm_b16_8, (((uint8_t *)t24) + 216));
            GM_ST(gm_b16_8, (uint8_t *)&t23 + 0, t25);
            void * t26 = gm_l0;
            gm_b16_8 t27 = GM_LD(gm_b16_8, (((uint8_t *)t26) + 232));
            GM_ST(gm_b16_8, (uint8_t *)&t23 + 16, t27);
            void * t28 = gm_l3;
            gm_b16_8 t29 = GM_LD(gm_b16_8, (((uint8_t *)t28) + 0));
            GM_ST(gm_b16_8, (uint8_t *)&t23 + 32, t29);
            gm_tabb710d9mq2e t30;
            gm_f_format_i9dlnsg((void *)&t30, (void *)NULL, cx, NULL, &t23);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t31 = t30;
            if (((((uint8_t *)&t31))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t31) + 8, gm_srcqr4ch56kv6bv, 186, 17);
            gm_b16_8 t32 = GM_LD(gm_b16_8, (((uint8_t *)&t31) + 8));
            gm_l6 = t32;
        }
        {
            gm_b16_8 t33;
            uint8_t t35 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 33));
            if (!t35) goto ie34;
            gm_b16_8 t37 = gm_l6;
            gm_b16_8 t38 = t37;
            gm_b16_8 t39 = GM_LD(gm_b16_8, ((uint8_t *)&t38));
            gm_b16_8 t40 = gm_l4;
            gm_b16_8 t41 = t40;
            gm_b16_8 t42 = GM_LD(gm_b16_8, ((uint8_t *)&t41));
            gm_b16_8 t43 = gm_f_absolute_6pqhsaj((void *)NULL, cx, NULL, t42);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_b16_8 t44 = gm_f_resolve_lines_h4bf30f((void *)NULL, cx, NULL, t39, t43);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_b16_8 t45 = t44;
            t33 = t45;
            goto ix34;
ie34:;
            gm_b16_8 t47 = gm_l6;
            gm_b16_8 t48 = t47;
            gm_b16_8 t49 = t48;
            gm_b16_8 t50 = GM_LD(gm_b16_8, ((uint8_t *)&t49));
            t33 = t50;
ix34:;
            gm_l7 = t33;
        }
        {
            gm_b16_8 t51 = gm_l4;
            gm_b16_8 t52 = t51;
            gm_b16_8 t53 = GM_LD(gm_b16_8, ((uint8_t *)&t52));
            gm_b16_8 t54 = gm_l7;
            uint8_t t55 = gm_f_write_generated_c_kb09043((void *)NULL, cx, NULL, t53, t54);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t56 = 0;
            F.ret = t55;
            goto gm_fret;
        }
        goto ix9;
ie9:;
ix9:;
    }
    {
        gm_td6svc7gs468a t57 = GM_LD(gm_td6svc7gs468a, (((uint8_t *)&F.l2) + 464));
        gm_td6svc7gs468a t58 = t57;
        gm_tfeab279c16rr t59;
        if (((((uint8_t *)&t58))[48] != 0)) {
            gm_tfeab279c16rr t60 = GM_LD(gm_tfeab279c16rr, (((uint8_t *)&t58) + 0));
            t59 = t60;
        } else {
            {
                gm_th2lqbo77pvsc t62 = GM_LD(gm_th2lqbo77pvsc, (((uint8_t *)&F.l2) + 520));
                gm_th2lqbo77pvsc t63 = t62;
                gm_b16_8 t64;
                if (((((uint8_t *)&t63))[16] != 0)) {
                    gm_b16_8 t65 = GM_LD(gm_b16_8, (((uint8_t *)&t63) + 0));
                    t64 = t65;
                } else {
                    gm_b16_8 t66;
                    GM_ST(void *, (uint8_t *)&t66, (void *)(gm_sr4qbrk6tlgaa));
                    GM_ST(uintptr_t, (uint8_t *)&t66 + 8, (uintptr_t)(0));
                    t64 = t66;
                }
                gm_b16_8 t67 = t64;
                if (gm_pf454qfrpt6r(cx, gm_s5lqhiu2bn6of, 7, gm_srcqr4ch56kv6bv, 191, 9)) {
                    goto gm_lad;
                }
                if (gm_pfu8dq5bikbh(cx, &gm_ty0iqiubfm7e, ((uint8_t *)&t67), gm_srcqr4ch56kv6bv, 191, 9)) {
                    goto gm_lad;
                }
                if (gm_pf454qfrpt6r(cx, gm_so514oj5nbhcu, 1, gm_srcqr4ch56kv6bv, 191, 9)) {
                    goto gm_lad;
                }
            }
            uint8_t t68 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        F.l8 = t59;
    }
    {
        gm_tctputthhrk9k t69 = GM_LD(gm_tctputthhrk9k, (((uint8_t *)&F.l2) + 248));
        gm_tctputthhrk9k t70 = t69;
        gm_tr8dvlu9ik4gj t71;
        if (((((uint8_t *)&t70))[120] != 0)) {
            gm_tr8dvlu9ik4gj t72 = GM_LD(gm_tr8dvlu9ik4gj, (((uint8_t *)&t70) + 0));
            t71 = t72;
        } else {
            gm_tr8dvlu9ik4gj t73;
            gm_f_extras_joquqpb((void *)&t73, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            t71 = t73;
        }
        F.l9 = t71;
    }
    {
        gm_f_no_link_a2212mt((void *)&F.l10, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t75 = (void *)((uint8_t *)&F.l9);
        uint8_t t76 = gm_f_is_empty_8l7niee(t75);
        uint8_t t77 = (uint8_t)!t76;
        if (!t77) goto ie74;
        {
            void * t79 = (void *)((uint8_t *)&F.l9);
            void * t80 = (void *)((uint8_t *)&F.l8);
            gm_b16_8 t81 = gm_l1;
            uint8_t t82 = ((GM_LD(uint8_t, (((uint8_t *)&F.l2) + 4) + 0) == ((uint8_t)0ULL)));
            uint8_t t83 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 33));
            gm_tsghr07tq93d8 t84;
            gm_f_link_inputs_1dv03h3((void *)&t84, (void *)NULL, cx, NULL, t79, t80, t81, t82, t83);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tsghr07tq93d8 t85 = t84;
            gm_t1rs7l0f3pc4f t86;
            if (!((((uint8_t *)&t85))[0] != 0)) {
                gm_t1rs7l0f3pc4f t87 = GM_LD(gm_t1rs7l0f3pc4f, (((uint8_t *)&t85) + 8));
                t86 = t87;
            } else {
                gm_b16_8 t88 = GM_LD(gm_b16_8, (((uint8_t *)&t85) + 8));
                gm_l11 = t88;
                {
                    gm_b16_8 t90 = gm_l11;
                    gm_b16_8 t91 = t90;
                    if (gm_pfu8dq5bikbh(cx, &gm_ty0iqiubfm7e, ((uint8_t *)&t91), gm_srcqr4ch56kv6bv, 198, 13)) {
                        goto gm_lad;
                    }
                    if (gm_pf454qfrpt6r(cx, gm_so514oj5nbhcu, 1, gm_srcqr4ch56kv6bv, 198, 13)) {
                        goto gm_lad;
                    }
                }
                uint8_t t92 = 0;
                F.ret = ((uint8_t)0);
                goto gm_fret;
            }
            F.l10 = t86;
        }
        goto ix74;
ie74:;
ix74:;
    }
    {
    }
    {
        void * t95 = gm_l0;
        void * t96 = (void *)((uint8_t *)&F.l8);
        void * t97 = (void *)((uint8_t *)&F.l10);
        gm_b16_8 t98 = gm_l1;
        uint8_t t99 = gm_f_compile_c_bkb1gkm((void *)NULL, cx, NULL, t95, t96, t97, t98, &(*(gm_tdkfkbgm73fql *)((uint8_t *)&F.l2)));
        if (gm_unw.kind) {
            goto gm_ld1_1;
        }
        uint8_t t100 = 0;
        F.ret = t99;
        gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = t100;
        goto gm_ld1_1;
    }
    {
        void * t101 = (void *)((uint8_t *)&F.l10);
        gm_b0_1 t102 = gm_f_remove_objects_1vq49q8((void *)NULL, cx, NULL, t101);
    }
    goto gm_ls103;
gm_ld1_1:;
    {
        gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
        void * t104 = (void *)((uint8_t *)&F.l10);
        gm_b0_1 t105 = gm_f_remove_objects_1vq49q8((void *)NULL, cx, NULL, t104);
        gm_unw = gm_saved;
    }
    goto gm_lad;
gm_ls103:;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) uint8_t gm_f_compile_c_bkb1gkm(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2, gm_b16_8 a3, const void * a4) {
    struct gm_fr_compile_c_bkb1gkm F;
    void * gm_l0; /* b */
    void * gm_l1; /* tc */
    void * gm_l2; /* link */
    gm_b16_8 gm_l3; /* exe */
    void * gm_l5; /* generated */
    gm_b16_8 gm_l6; /* opt */
    uint8_t gm_l7; /* debug */
    gm_tf5srh4mp4b3i gm_l8; /* forced */
    gm_b16_8 gm_l9; /* v */
    uint64_t gm_l10; /* cpus */
    uint64_t gm_l11; /* n */
    uint8_t gm_l12; /* separate */
    gm_th2lqbo77pvsc gm_l13; /* version */
    gm_th2lqbo77pvsc gm_l14; /* runtime */
    gm_th2lqbo77pvsc gm_l17; /* cached */
    gm_t6j6smj7qoctu gm_l18; /* split */
    gm_b16_8 gm_l20; /* objs */
    gm_b16_8 gm_l21; /* prelude */
    gm_th2lqbo77pvsc gm_l22; /* split_units */
    gm_b16_8 gm_l23; /* units */
    gm_b16_8 gm_l24; /* <tmp> */
    uint64_t gm_l25; /* <tmp> */
    gm_b16_8 gm_l26; /* u */
    gm_b16_8 gm_l28; /* <tmp> */
    uint64_t gm_l29; /* <tmp> */
    uint64_t gm_l30; /* i */
    gm_b16_8 gm_l31; /* src */
    gm_b16_8 gm_l32; /* path */
    gm_b16_8 gm_l33; /* text */
    uint8_t gm_l35; /* wasi */
    uint64_t gm_l38; /* <tmp> */
    uint64_t gm_l39; /* <tmp> */
    uint64_t gm_l40; /* i */
    uint8_t gm_l42; /* ok */
    gm_b16_8 gm_l43; /* <tmp> */
    uint64_t gm_l44; /* <tmp> */
    gm_b16_8 gm_l45; /* o */
    gm_b16_8 gm_l47; /* <tmp> */
    uint64_t gm_l48; /* <tmp> */
    gm_b16_8 gm_l49; /* f */
    gm_b16_8 gm_l51; /* objs */
    gm_b16_8 gm_l52; /* r */
    uint8_t gm_l54; /* ok */
    gm_b16_8 gm_l55; /* <tmp> */
    uint64_t gm_l56; /* <tmp> */
    gm_b16_8 gm_l57; /* o */
    gm_b16_8 gm_l58; /* e */
    GM_STACK_CHECK(&F, 5153u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    memcpy(&F.l4, a4, sizeof F.l4);
    {
        void * t2 = gm_l0;
        void * t3 = (void *)(((uint8_t *)t2) + 0);
        gm_l5 = t3;
    }
    {
        void * t5 = gm_l5;
        uint8_t t6 = GM_LD(uint8_t, (((uint8_t *)t5) + 208));
        if (!t6) goto ie4;
        {
            void * t8 = gm_l0;
            void * t9 = gm_l1;
            gm_b16_8 t10 = gm_l3;
            uint8_t t11 = gm_f_compile_object_k7kbtpr((void *)NULL, cx, NULL, t8, t9, t10, &(*(gm_tdkfkbgm73fql *)((uint8_t *)&F.l4)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t12 = 0;
            F.ret = t11;
            goto gm_fret;
        }
        goto ix4;
ie4:;
ix4:;
    }
    {
        void * t13 = (void *)((uint8_t *)&F.l4);
        gm_b16_8 t14 = gm_f_opt_level_gnm5dcm(t13);
        gm_l6 = t14;
    }
    {
        uint8_t t15 = ((GM_LD(uint8_t, (((uint8_t *)&F.l4) + 4) + 0) == ((uint8_t)0ULL)));
        gm_l7 = t15;
    }
    {
        gm_tf5srh4mp4b3i t16;
        memset(&t16, 0, sizeof t16);
        gm_l8 = t16;
    }
    {
        gm_b16_8 t18;
        GM_ST(void *, (uint8_t *)&t18, (void *)(gm_sdahvc1997d01));
        GM_ST(uintptr_t, (uint8_t *)&t18 + 8, (uintptr_t)(13));
        gm_node *pn19 = gm_find(cx, 621690230);
        if (!pn19) gm_trap("no handler installed for effect `Env`", gm_srcqr4ch56kv6bv, 311, 13);
        gm_th2lqbo77pvsc t20;
        if (pn19->kind == GM_NATIVE) {
            const uint8_t *ev; size_t evn;
            bool evok = gm_env_var(GM_LD(void *, &t18), GM_LD(uintptr_t, (uint8_t *)&t18 + 8), &ev, &evn);
            gm_b16_8 t21;
            GM_ST(void *, (uint8_t *)&t21, (void *)(ev));
            GM_ST(uintptr_t, (uint8_t *)&t21 + 8, (uintptr_t)(evn));
            gm_th2lqbo77pvsc t22;
            GM_ST(uint8_t, (uint8_t *)&t22 + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&t22 + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&t22 + 20, 0);
            GM_ST(gm_b16_8, ((uint8_t *)&t22), t21);
            ((uint8_t *)&t22)[16] = 1;
            gm_th2lqbo77pvsc t23;
            memset(&t23, 0, sizeof t23);
            t20 = evok ? t22 : t23;
        } else {
            ((void (*)(void *, void *, gm_node *, gm_node *, gm_b16_8))pn19->clauses[1])((void *)&t20, pn19->env, pn19->hctx, pn19, t18);
        }
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_th2lqbo77pvsc t24 = t20;
        if (!((((uint8_t *)&t24))[16] != 0)) goto ie17;
        gm_b16_8 t25 = GM_LD(gm_b16_8, ((uint8_t *)&t24));
        gm_l9 = t25;
        {
            gm_b16_8 t27 = gm_l9;
            gm_tutnubt6u4hoq t28;
            gm_f_parse_int_3ce02np((void *)&t28, t27);
            gm_tutnubt6u4hoq t29 = t28;
            gm_tf5srh4mp4b3i t30;
            if (!((((uint8_t *)&t29))[0] != 0)) {
                uint64_t t31 = GM_LD(uint64_t, (((uint8_t *)&t29) + 8));
                gm_tf5srh4mp4b3i t32;
                GM_ST(uint8_t, (uint8_t *)&t32 + 9, 0);
                GM_ST(uint16_t, (uint8_t *)&t32 + 10, 0);
                GM_ST(uint32_t, (uint8_t *)&t32 + 12, 0);
                GM_ST(uint64_t, ((uint8_t *)&t32), t31);
                ((uint8_t *)&t32)[8] = 1;
                t30 = t32;
            } else {
                gm_tf5srh4mp4b3i t33;
                memset(&t33, 0, sizeof t33);
                t30 = t33;
            }
            gm_l8 = t30;
        }
        goto ix17;
ie17:;
ix17:;
    }
    {
        uint32_t t34 = gm_f_cpu_count_0o5882g();
        uint64_t t35 = (uint64_t)t34;
        gm_l10 = t35;
    }
    {
        gm_l11 = ((uint64_t)1ULL);
    }
    {
        uint8_t t37 = gm_l7;
        if (!t37) goto ie36;
        {
            void * t39 = gm_l5;
            void * t40 = (void *)(((uint8_t *)t39) + 16);
            uint64_t t41 = gm_l10;
            gm_tf5srh4mp4b3i t42 = gm_l8;
            uint64_t t43 = gm_f_unit_count_dk0d2e4(t40, t41, t42);
            gm_l11 = t43;
        }
        goto ix36;
ie36:;
        uint8_t t45 = GM_LD(uint8_t, ((((uint8_t *)&F.l4) + 400) + 56));
        uint8_t t46 = (uint8_t)!t45;
        if (!t46) goto ie44;
        {
            void * t48 = gm_l5;
            void * t49 = (void *)(((uint8_t *)t48) + 16);
            uint64_t t50 = gm_l10;
            gm_tf5srh4mp4b3i t51 = gm_l8;
            uint64_t t52 = gm_f_optimized_unit_count_5mur81l(t49, t50, t51);
            gm_l11 = t52;
        }
        goto ix44;
ie44:;
ix44:;
ix36:;
    }
    {
        uint8_t t53 = 0;
        uint8_t t54 = 0;
        uint8_t t55 = gm_l7;
        if (!t55) {
            uint8_t t56 = GM_LD(uint8_t, ((((uint8_t *)&F.l4) + 400) + 56));
            t54 = t56;
        } else t54 = 1;
        if (!t54) {
            uint64_t t57 = gm_l11;
            uint8_t t58 = (t57 > ((uint64_t)1ULL));
            t53 = t58;
        } else t53 = 1;
        gm_l12 = t53;
    }
    {
        gm_th2lqbo77pvsc t59;
        uint8_t t61 = gm_l12;
        if (!t61) goto ie60;
        void * t63 = gm_l1;
        gm_th2lqbo77pvsc t64;
        gm_f_compiler_version_hj63jgf((void *)&t64, (void *)NULL, cx, NULL, t63);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_th2lqbo77pvsc t65 = t64;
        t59 = t65;
        goto ix60;
ie60:;
        gm_th2lqbo77pvsc t67;
        memset(&t67, 0, sizeof t67);
        gm_th2lqbo77pvsc t68 = t67;
        t59 = t68;
ix60:;
        gm_l13 = t59;
    }
    {
        gm_th2lqbo77pvsc t69;
        memset(&t69, 0, sizeof t69);
        gm_l14 = t69;
    }
    {
        uint8_t t71 = gm_l12;
        if (!t71) goto ie70;
        {
            void * t73 = (void *)((uint8_t *)&F.l4);
            gm_f_rt_flags_oeql34c((void *)&F.l15, (void *)NULL, cx, NULL, t73);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t74 = gm_l1;
            gm_b16_8 t75 = GM_LD(gm_b16_8, (((uint8_t *)&F.l15) + 0));
            gm_b16_8 t76 = t75;
            gm_b16_8 t77 = GM_LD(gm_b16_8, ((uint8_t *)&t76));
            void * t78 = gm_l0;
            gm_b16_8 t79 = gm_f_runtime_source_skilk06((void *)NULL, cx, NULL, t78);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_b16_8 t80;
            GM_ST(void *, (uint8_t *)&t80, (void *)(gm_smi2s23d2qqne));
            GM_ST(uintptr_t, (uint8_t *)&t80 + 8, (uintptr_t)(5));
            gm_f_runtime_object_oihv3ss((void *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l14)), (void *)NULL, cx, NULL, t74, t77, t79, t80, &(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l13)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix70;
ie70:;
ix70:;
    }
    {
        gm_f_list_epvducg((void *)&F.l16, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_th2lqbo77pvsc t81;
        memset(&t81, 0, sizeof t81);
        gm_l17 = t81;
    }
    {
        uint8_t t83;
        if (((((uint8_t *)&gm_l14))[16] != 0)) goto mn85;
        t83 = ((uint8_t)0);
        goto me84;
mn85:;
        t83 = ((uint8_t)1);
        goto me84;
mn86:;
        gm_trap("no match arm matched the scrutinee", gm_srcqr4ch56kv6bv, 334, 8);
me84:;
        if (!t83) goto ie82;
        uint64_t t87 = gm_l11;
        uint8_t t88 = (t87 > ((uint64_t)1ULL));
        if (!t88) goto ie82;
        {
            void * t91 = gm_l5;
            void * t92 = (void *)(((uint8_t *)t91) + 16);
            gm_ttu09lij36f8c t93;
            gm_f_split_by_file_hgjqgsp((void *)&t93, (void *)NULL, cx, NULL, t92);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_ttu09lij36f8c t94 = t93;
            if (!((((uint8_t *)&t94))[32] != 0)) goto ie90;
            gm_t6j6smj7qoctu t95 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)&t94));
            gm_l18 = t95;
            {
                void * t97 = gm_l0;
                void * t98 = (void *)((uint8_t *)&gm_l18);
                gm_f_unit_sources_329det7((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l16)), (void *)NULL, cx, NULL, t97, t98);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t99 = (void *)((uint8_t *)&F.l4);
                gm_f_build_flags_sn2j41p((void *)&F.l19, (void *)NULL, cx, NULL, t99, &(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l13)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t100 = (void *)((uint8_t *)&F.l19);
                gm_b16_8 t101;
                GM_ST(void *, (uint8_t *)&t101, (void *)(gm_sm8m7jldttgpj));
                GM_ST(uintptr_t, (uint8_t *)&t101 + 8, (uintptr_t)(37));
                gm_tftb1odc739t5 t102 = gm_f_push_3ef746b(t100, t101);
                gm_tftb1odc739t5 t103 = t102;
                if (((((uint8_t *)&t103))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t103) + 1, gm_srcqr4ch56kv6bv, 338, 13);
                gm_b0_1 t104 = GM_LD(gm_b0_1, (((uint8_t *)&t103) + 1));
            }
            {
                void * t105 = gm_l1;
                gm_b16_8 t106 = GM_LD(gm_b16_8, (((uint8_t *)&F.l19) + 0));
                gm_b16_8 t107 = t106;
                gm_b16_8 t108 = GM_LD(gm_b16_8, ((uint8_t *)&t107));
                gm_b16_8 t109 = GM_LD(gm_b16_8, (((uint8_t *)&F.l16) + 0));
                gm_b16_8 t110 = t109;
                gm_b16_8 t111 = GM_LD(gm_b16_8, ((uint8_t *)&t110));
                uint64_t t112 = gm_l10;
                uint8_t t113 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 33));
                gm_tl572d7579hns t114;
                gm_f_unit_objects_3f31tcd((void *)&t114, (void *)NULL, cx, NULL, t105, t108, t111, &(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l13)), t112, t113);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tl572d7579hns t115 = t114;
                uint8_t t121 = GM_LD(uint8_t, (((uint8_t *)&t115) + 0));
                switch (t121) { case ((uint8_t)0ULL): goto ma117; case ((uint8_t)1ULL): goto ma119; case ((uint8_t)2ULL): goto ma118; default: goto ma120; }
ma117:;
                uint8_t t123 = GM_LD(uint8_t, (((uint8_t *)&t115) + 0));
                if (t123 != ((uint8_t)0ULL)) goto mn122;
                gm_b16_8 t124 = GM_LD(gm_b16_8, (((uint8_t *)&t115) + 8));
                gm_l20 = t124;
                {
                    gm_b16_8 t126 = gm_l20;
                    gm_th2lqbo77pvsc t127;
                    GM_ST(uint8_t, (uint8_t *)&t127 + 17, 0);
                    GM_ST(uint16_t, (uint8_t *)&t127 + 18, 0);
                    GM_ST(uint32_t, (uint8_t *)&t127 + 20, 0);
                    GM_ST(gm_b16_8, ((uint8_t *)&t127), t126);
                    ((uint8_t *)&t127)[16] = 1;
                    gm_l17 = t127;
                }
                goto me116;
mn122:;
ma118:;
                uint8_t t129 = GM_LD(uint8_t, (((uint8_t *)&t115) + 0));
                if (t129 != ((uint8_t)2ULL)) goto mn128;
                uint8_t t130 = 0;
                F.ret = ((uint8_t)0);
                goto gm_fret;
                goto me116;
mn128:;
ma119:;
                uint8_t t132 = GM_LD(uint8_t, (((uint8_t *)&t115) + 0));
                if (t132 != ((uint8_t)1ULL)) goto mn131;
                goto me116;
mn131:;
ma120:;
                gm_trap("no match arm matched the scrutinee", gm_srcqr4ch56kv6bv, 339, 13);
me116:;
            }
            goto ix90;
ie90:;
ix90:;
        }
        goto ix82;
ie82:;
ix82:;
    }
    {
        uint8_t t135 = 0;
        uint8_t t136;
        if (((((uint8_t *)&gm_l17))[16] != 0)) goto mn138;
        t136 = ((uint8_t)0);
        goto me137;
mn138:;
        t136 = ((uint8_t)1);
        goto me137;
mn139:;
        gm_trap("no match arm matched the scrutinee", gm_srcqr4ch56kv6bv, 346, 8);
me137:;
        if (!t136) {
            uint64_t t140 = GM_LD(uintptr_t, (((uint8_t *)&F.l16) + 0) + 8);
            uint8_t t141 = (t140 > ((uint64_t)0ULL));
            t135 = t141;
        } else t135 = 1;
        if (!t135) goto ie134;
        goto ix134;
ie134:;
        uint8_t t144;
        if (((((uint8_t *)&gm_l14))[16] != 0)) goto mn146;
        t144 = ((uint8_t)0);
        goto me145;
mn146:;
        t144 = ((uint8_t)1);
        goto me145;
mn147:;
        gm_trap("no match arm matched the scrutinee", gm_srcqr4ch56kv6bv, 348, 15);
me145:;
        if (!t144) goto ie143;
        {
            gm_t6j6smj7qoctu t149;
            gm_b16_8 t150 = GM_LD(gm_b16_8, gm_c69snm9duhi);
            GM_ST(gm_b16_8, (uint8_t *)&t149 + 0, t150);
            void * t151 = gm_l0;
            gm_b16_8 t152 = GM_LD(gm_b16_8, (((uint8_t *)t151) + 216));
            GM_ST(gm_b16_8, (uint8_t *)&t149 + 16, t152);
            gm_tabb710d9mq2e t153;
            gm_f_format_q8i7gsb((void *)&t153, (void *)NULL, cx, NULL, &t149);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t154 = t153;
            if (((((uint8_t *)&t154))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t154) + 8, gm_srcqr4ch56kv6bv, 349, 20);
            gm_b16_8 t155 = GM_LD(gm_b16_8, (((uint8_t *)&t154) + 8));
            gm_l21 = t155;
        }
        {
            gm_th2lqbo77pvsc t156;
            uint64_t t158 = gm_l11;
            uint8_t t159 = (t158 > ((uint64_t)1ULL));
            if (!t159) goto ie157;
            void * t161 = gm_l5;
            void * t162 = (void *)(((uint8_t *)t161) + 16);
            uint64_t t163 = gm_l11;
            gm_th2lqbo77pvsc t164;
            gm_f_split_jendjdm((void *)&t164, (void *)NULL, cx, NULL, t162, t163);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_th2lqbo77pvsc t165 = t164;
            t156 = t165;
            goto ix157;
ie157:;
            gm_th2lqbo77pvsc t167;
            memset(&t167, 0, sizeof t167);
            gm_th2lqbo77pvsc t168 = t167;
            t156 = t168;
ix157:;
            gm_l22 = t156;
        }
        {
            if (!((((uint8_t *)&gm_l22))[16] != 0)) goto ie169;
            gm_b16_8 t170 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l22));
            gm_l23 = t170;
            {
                {
                    gm_b16_8 t173 = gm_l23;
                    gm_l24 = t173;
                }
                {
                    gm_l25 = ((uint64_t)0ULL);
                }
                {
                    for (;;) {
                        uint64_t t175 = gm_l25;
                        uint64_t t176 = GM_LD(uintptr_t, ((uint8_t *)&gm_l24) + 8);
                        uint8_t t177 = (t175 < t176);
                        if (!t177) goto lb174;
                        {
                            uint64_t t180 = gm_l25;
                            uint64_t t181 = (uintptr_t)t180;
                            uint64_t t182 = GM_LD(uintptr_t, ((uint8_t *)&gm_l24) + 8);
                            gm_b16_8 t185 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l24)) + t181 * 16));
                            gm_l26 = t185;
                            {
                                void * t187 = (void *)((uint8_t *)&F.l16);
                                gm_t6j6smj7qoctu t188;
                                gm_b16_8 t189 = gm_l21;
                                GM_ST(gm_b16_8, (uint8_t *)&t188 + 0, t189);
                                gm_b16_8 t190 = gm_l26;
                                GM_ST(gm_b16_8, (uint8_t *)&t188 + 16, t190);
                                gm_tabb710d9mq2e t191;
                                gm_f_format_5029eea((void *)&t191, (void *)NULL, cx, NULL, &t188);
                                if (gm_unw.kind) {
                                    goto gm_ll178;
                                }
                                gm_tabb710d9mq2e t192 = t191;
                                if (((((uint8_t *)&t192))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t192) + 8, gm_srcqr4ch56kv6bv, 352, 43);
                                gm_b16_8 t193 = GM_LD(gm_b16_8, (((uint8_t *)&t192) + 8));
                                gm_b16_8 t194 = t193;
                                gm_b16_8 t195 = GM_LD(gm_b16_8, ((uint8_t *)&t194));
                                gm_tftb1odc739t5 t196 = gm_f_push_3ef746b(t187, t195);
                                gm_tftb1odc739t5 t197 = t196;
                                if (((((uint8_t *)&t197))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t197) + 1, gm_srcqr4ch56kv6bv, 352, 30);
                                gm_b0_1 t198 = GM_LD(gm_b0_1, (((uint8_t *)&t197) + 1));
                            }
                            goto me183;
mn184:;
                            gm_trap("no match arm matched the scrutinee", gm_srcqr4ch56kv6bv, 352, 13);
me183:;
                        }
lc174:;
                        {
                            uint64_t t200 = gm_l25;
                            uint64_t r201; bool o202 = __builtin_add_overflow(t200, ((uint64_t)1ULL), &r201);
                            uint64_t t203 = r201;
                            gm_l25 = t203;
                        }
                    }
gm_ll178:;
                    if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb174; }
                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc174; }
                    }
                    goto gm_lad;
lb174:;
                }
            }
            goto ix169;
ie169:;
            {
                uint64_t t206 = gm_l11;
                uint8_t t207 = (t206 > ((uint64_t)1ULL));
                if (!t207) goto ie205;
                uint8_t t208;
                if (((((uint8_t *)&gm_l8))[8] != 0)) goto mn210;
                t208 = ((uint8_t)0);
                goto me209;
mn210:;
                t208 = ((uint8_t)1);
                goto me209;
mn211:;
                gm_trap("no match arm matched the scrutinee", gm_srcqr4ch56kv6bv, 354, 26);
me209:;
                if (!t208) goto ie205;
                {
                    if (gm_pf454qfrpt6r(cx, gm_sad95m298up5o, 43, gm_srcqr4ch56kv6bv, 355, 17)) {
                        goto gm_lad;
                    }
                }
                {
                    uint8_t t213 = 0;
                    F.ret = ((uint8_t)0);
                    goto gm_fret;
                }
                goto ix205;
ie205:;
ix205:;
            }
            {
                void * t214 = (void *)((uint8_t *)&F.l16);
                gm_t6j6smj7qoctu t215;
                gm_b16_8 t216 = gm_l21;
                GM_ST(gm_b16_8, (uint8_t *)&t215 + 0, t216);
                void * t217 = gm_l5;
                gm_b16_8 t218 = GM_LD(gm_b16_8, (((uint8_t *)t217) + 0));
                GM_ST(gm_b16_8, (uint8_t *)&t215 + 16, t218);
                gm_tabb710d9mq2e t219;
                gm_f_format_5029eea((void *)&t219, (void *)NULL, cx, NULL, &t215);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t220 = t219;
                if (((((uint8_t *)&t220))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t220) + 8, gm_srcqr4ch56kv6bv, 358, 26);
                gm_b16_8 t221 = GM_LD(gm_b16_8, (((uint8_t *)&t220) + 8));
                gm_b16_8 t222 = t221;
                gm_b16_8 t223 = GM_LD(gm_b16_8, ((uint8_t *)&t222));
                gm_tftb1odc739t5 t224 = gm_f_push_3ef746b(t214, t223);
                gm_tftb1odc739t5 t225 = t224;
                if (((((uint8_t *)&t225))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t225) + 1, gm_srcqr4ch56kv6bv, 358, 13);
                gm_b0_1 t226 = GM_LD(gm_b0_1, (((uint8_t *)&t225) + 1));
            }
ix169:;
        }
        goto ix143;
ie143:;
        {
            void * t228 = (void *)((uint8_t *)&F.l16);
            gm_tfeab279c16rr t229;
            void * t230 = gm_l0;
            gm_b16_8 t231 = GM_LD(gm_b16_8, (((uint8_t *)t230) + 216));
            GM_ST(gm_b16_8, (uint8_t *)&t229 + 0, t231);
            void * t232 = gm_l0;
            gm_b16_8 t233 = GM_LD(gm_b16_8, (((uint8_t *)t232) + 232));
            GM_ST(gm_b16_8, (uint8_t *)&t229 + 16, t233);
            void * t234 = gm_l5;
            gm_b16_8 t235 = GM_LD(gm_b16_8, (((uint8_t *)t234) + 0));
            GM_ST(gm_b16_8, (uint8_t *)&t229 + 32, t235);
            gm_tabb710d9mq2e t236;
            gm_f_format_i9dlnsg((void *)&t236, (void *)NULL, cx, NULL, &t229);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t237 = t236;
            if (((((uint8_t *)&t237))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t237) + 8, gm_srcqr4ch56kv6bv, 361, 22);
            gm_b16_8 t238 = GM_LD(gm_b16_8, (((uint8_t *)&t237) + 8));
            gm_b16_8 t239 = t238;
            gm_b16_8 t240 = GM_LD(gm_b16_8, ((uint8_t *)&t239));
            gm_tftb1odc739t5 t241 = gm_f_push_3ef746b(t228, t240);
            gm_tftb1odc739t5 t242 = t241;
            if (((((uint8_t *)&t242))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t242) + 1, gm_srcqr4ch56kv6bv, 361, 9);
            gm_b0_1 t243 = GM_LD(gm_b0_1, (((uint8_t *)&t242) + 1));
        }
ix143:;
ix134:;
    }
    {
        gm_f_list_epvducg((void *)&F.l27, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        uint8_t t245;
        if (((((uint8_t *)&gm_l17))[16] != 0)) goto mn247;
        t245 = ((uint8_t)0);
        goto me246;
mn247:;
        t245 = ((uint8_t)1);
        goto me246;
mn248:;
        gm_trap("no match arm matched the scrutinee", gm_srcqr4ch56kv6bv, 364, 8);
me246:;
        if (!t245) goto ie244;
        {
            void * t250 = (void *)((uint8_t *)&F.l16);
            gm_f_clear_dvdem7p(t250);
        }
        goto ix244;
ie244:;
ix244:;
    }
    {
        {
            gm_b16_8 t252 = GM_LD(gm_b16_8, (((uint8_t *)&F.l16) + 0));
            gm_b16_8 t253 = t252;
            gm_b16_8 t254 = GM_LD(gm_b16_8, ((uint8_t *)&t253));
            gm_l28 = t254;
        }
        {
            gm_l29 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t256 = gm_l29;
                uint64_t t257 = GM_LD(uintptr_t, ((uint8_t *)&gm_l28) + 8);
                uint8_t t258 = (t256 < t257);
                if (!t258) goto lb255;
                {
                    gm_tf1bjg81u0glv t261;
                    uint64_t t262 = gm_l29;
                    GM_ST(uint64_t, (uint8_t *)&t261 + 0, t262);
                    uint64_t t263 = gm_l29;
                    uint64_t t264 = (uintptr_t)t263;
                    uint64_t t265 = GM_LD(uintptr_t, ((uint8_t *)&gm_l28) + 8);
                    gm_b16_8 t266 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l28)) + t264 * 16));
                    GM_ST(gm_b16_8, (uint8_t *)&t261 + 8, t266);
                    gm_tf1bjg81u0glv t267 = t261;
                    uint64_t t270 = GM_LD(uint64_t, (((uint8_t *)&t267) + 0));
                    gm_l30 = t270;
                    gm_b16_8 t271 = GM_LD(gm_b16_8, (((uint8_t *)&t267) + 8));
                    gm_l31 = t271;
                    {
                        gm_b16_8 t273 = gm_l3;
                        uint64_t t274 = gm_l30;
                        gm_b16_8 t275 = gm_f_unit_path_3tdv4sg((void *)NULL, cx, NULL, t273, t274);
                        if (gm_unw.kind) {
                            goto gm_ll259;
                        }
                        gm_l32 = t275;
                    }
                    {
                        gm_b16_8 t276;
                        uint8_t t278 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 33));
                        if (!t278) goto ie277;
                        gm_b16_8 t280 = gm_l31;
                        gm_b16_8 t281 = gm_l32;
                        gm_b16_8 t282 = gm_f_absolute_6pqhsaj((void *)NULL, cx, NULL, t281);
                        if (gm_unw.kind) {
                            goto gm_ll259;
                        }
                        gm_b16_8 t283 = gm_f_resolve_lines_h4bf30f((void *)NULL, cx, NULL, t280, t282);
                        if (gm_unw.kind) {
                            goto gm_ll259;
                        }
                        gm_b16_8 t284 = t283;
                        t276 = t284;
                        goto ix277;
ie277:;
                        gm_b16_8 t286 = gm_l31;
                        gm_b16_8 t287 = t286;
                        t276 = t287;
ix277:;
                        gm_l33 = t276;
                    }
                    {
                        gm_b16_8 t289 = gm_l32;
                        gm_b16_8 t290 = gm_l33;
                        uint8_t t291 = gm_f_write_generated_c_kb09043((void *)NULL, cx, NULL, t289, t290);
                        if (gm_unw.kind) {
                            goto gm_ll259;
                        }
                        uint8_t t292 = (uint8_t)!t291;
                        if (!t292) goto ie288;
                        {
                            uint8_t t294 = 0;
                            F.ret = ((uint8_t)0);
                            goto gm_fret;
                        }
                        goto ix288;
ie288:;
ix288:;
                    }
                    {
                        void * t295 = (void *)((uint8_t *)&F.l27);
                        gm_b16_8 t296 = gm_l32;
                        gm_tftb1odc739t5 t297 = gm_f_push_3ef746b(t295, t296);
                        gm_tftb1odc739t5 t298 = t297;
                        if (((((uint8_t *)&t298))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t298) + 1, gm_srcqr4ch56kv6bv, 370, 9);
                        gm_b0_1 t299 = GM_LD(gm_b0_1, (((uint8_t *)&t298) + 1));
                    }
                    goto me268;
mn269:;
                    gm_trap("no match arm matched the scrutinee", gm_srcqr4ch56kv6bv, 365, 5);
me268:;
                }
lc255:;
                {
                    uint64_t t301 = gm_l29;
                    uint64_t r302; bool o303 = __builtin_add_overflow(t301, ((uint64_t)1ULL), &r302);
                    uint64_t t304 = r302;
                    gm_l29 = t304;
                }
            }
gm_ll259:;
            if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb255; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc255; }
            }
            goto gm_lad;
lb255:;
        }
    }
    {
        void * t305 = (void *)((uint8_t *)&F.l4);
        gm_f_build_flags_sn2j41p((void *)&F.l34, (void *)NULL, cx, NULL, t305, &(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l13)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        uint8_t t306 = gm_f_wasi_target_lp6g335(&(*(gm_t0rfe1ou14h21 *)(((uint8_t *)&F.l4) + 400)));
        gm_l35 = t306;
    }
    {
        gm_f_list_epvducg((void *)&F.l36, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_f_list_epvducg((void *)&F.l37, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        uint8_t t308 = 0;
        uint64_t t309 = GM_LD(uintptr_t, (((uint8_t *)&F.l27) + 0) + 8);
        uint8_t t310 = (t309 > ((uint64_t)1ULL));
        if (!t310) {
            uint8_t t311 = gm_l35;
            t308 = t311;
        } else t308 = 1;
        if (!t308) goto ie307;
        {
            {
                gm_l39 = ((uint64_t)0ULL);
            }
            {
                uint64_t t314 = GM_LD(uintptr_t, (((uint8_t *)&F.l27) + 0) + 8);
                gm_l38 = t314;
            }
            {
                for (;;) {
                    uint64_t t316 = gm_l39;
                    uint64_t t317 = gm_l38;
                    uint8_t t318 = (t316 < t317);
                    if (!t318) goto lb315;
                    {
                        uint64_t t321 = gm_l39;
                        uint64_t t322 = t321;
                        uint64_t t325 = GM_LD(uint64_t, ((uint8_t *)&t322));
                        gm_l40 = t325;
                        {
                            void * t327 = (void *)((uint8_t *)&F.l37);
                            gm_tq3m5qgvkpje8 t328;
                            gm_b16_8 t329 = gm_l3;
                            GM_ST(gm_b16_8, (uint8_t *)&t328 + 0, t329);
                            uint64_t t330 = gm_l40;
                            GM_ST(uint64_t, (uint8_t *)&t328 + 16, t330);
                            gm_b16_8 t331 = gm_f_pid_token_c444nkv((void *)NULL, cx, NULL);
                            if (gm_unw.kind) {
                                goto gm_ll319;
                            }
                            GM_ST(gm_b16_8, (uint8_t *)&t328 + 24, t331);
                            gm_tabb710d9mq2e t332;
                            gm_f_format_kdp6nh1((void *)&t332, (void *)NULL, cx, NULL, &t328);
                            if (gm_unw.kind) {
                                goto gm_ll319;
                            }
                            gm_tabb710d9mq2e t333 = t332;
                            if (((((uint8_t *)&t333))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t333) + 8, gm_srcqr4ch56kv6bv, 380, 54);
                            gm_b16_8 t334 = GM_LD(gm_b16_8, (((uint8_t *)&t333) + 8));
                            gm_b16_8 t335 = t334;
                            gm_b16_8 t336 = GM_LD(gm_b16_8, ((uint8_t *)&t335));
                            gm_tftb1odc739t5 t337 = gm_f_push_3ef746b(t327, t336);
                            gm_tftb1odc739t5 t338 = t337;
                            if (((((uint8_t *)&t338))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t338) + 1, gm_srcqr4ch56kv6bv, 380, 41);
                            gm_b0_1 t339 = GM_LD(gm_b0_1, (((uint8_t *)&t338) + 1));
                        }
                        goto me323;
mn324:;
                        gm_trap("no match arm matched the scrutinee", gm_srcqr4ch56kv6bv, 380, 9);
me323:;
                    }
lc315:;
                    {
                        uint64_t t341 = gm_l39;
                        uint64_t r342; bool o343 = __builtin_add_overflow(t341, ((uint64_t)1ULL), &r342);
                        uint64_t t344 = r342;
                        gm_l39 = t344;
                    }
                }
gm_ll319:;
                if (gm_unw.frame == &F.h && gm_unw.label == 2) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb315; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc315; }
                }
                goto gm_lad;
lb315:;
            }
        }
        {
            gm_f_list_epvducg((void *)&F.l41, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t345 = (void *)((uint8_t *)&F.l41);
            gm_b16_8 t346 = GM_LD(gm_b16_8, (((uint8_t *)&F.l34) + 0));
            gm_b16_8 t347 = t346;
            gm_b16_8 t348 = GM_LD(gm_b16_8, ((uint8_t *)&t347));
            gm_tftb1odc739t5 t349 = gm_f_extend_vogsd72(t345, t348);
            gm_tftb1odc739t5 t350 = t349;
            if (((((uint8_t *)&t350))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t350) + 1, gm_srcqr4ch56kv6bv, 384, 9);
            gm_b0_1 t351 = GM_LD(gm_b0_1, (((uint8_t *)&t350) + 1));
        }
        {
            void * t352 = (void *)((uint8_t *)&F.l41);
            gm_b16_8 t353;
            GM_ST(void *, (uint8_t *)&t353, (void *)(gm_sm8m7jldttgpj));
            GM_ST(uintptr_t, (uint8_t *)&t353 + 8, (uintptr_t)(37));
            gm_tftb1odc739t5 t354 = gm_f_push_3ef746b(t352, t353);
            gm_tftb1odc739t5 t355 = t354;
            if (((((uint8_t *)&t355))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t355) + 1, gm_srcqr4ch56kv6bv, 385, 9);
            gm_b0_1 t356 = GM_LD(gm_b0_1, (((uint8_t *)&t355) + 1));
        }
        {
            void * t357 = gm_l1;
            gm_b16_8 t358 = GM_LD(gm_b16_8, (((uint8_t *)&F.l41) + 0));
            gm_b16_8 t359 = t358;
            gm_b16_8 t360 = GM_LD(gm_b16_8, ((uint8_t *)&t359));
            gm_b16_8 t361 = GM_LD(gm_b16_8, (((uint8_t *)&F.l27) + 0));
            gm_b16_8 t362 = t361;
            gm_b16_8 t363 = GM_LD(gm_b16_8, ((uint8_t *)&t362));
            gm_b16_8 t364 = GM_LD(gm_b16_8, (((uint8_t *)&F.l37) + 0));
            gm_b16_8 t365 = t364;
            gm_b16_8 t366 = GM_LD(gm_b16_8, ((uint8_t *)&t365));
            uint64_t t367 = gm_l10;
            uint8_t t368 = gm_f_compile_objects_mu5secm((void *)NULL, cx, NULL, t357, t360, t363, t366, t367);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l42 = t368;
        }
        {
            uint8_t t370 = gm_l42;
            uint8_t t371 = (uint8_t)!t370;
            if (!t371) goto ie369;
            {
                {
                    gm_b16_8 t374 = GM_LD(gm_b16_8, (((uint8_t *)&F.l37) + 0));
                    gm_l43 = t374;
                }
                {
                    gm_l44 = ((uint64_t)0ULL);
                }
                {
                    for (;;) {
                        uint64_t t376 = gm_l44;
                        uint64_t t377 = GM_LD(uintptr_t, ((uint8_t *)&gm_l43) + 8);
                        uint8_t t378 = (t376 < t377);
                        if (!t378) goto lb375;
                        {
                            uint64_t t381 = gm_l44;
                            uint64_t t382 = (uintptr_t)t381;
                            uint64_t t383 = GM_LD(uintptr_t, ((uint8_t *)&gm_l43) + 8);
                            gm_b16_8 t386 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l43)) + t382 * 16));
                            gm_l45 = t386;
                            {
                                {
                                    gm_b16_8 t389 = gm_l45;
                                    gm_ttln0d0hd70en t390 = gm_f_remove_all_h0k1qkh((void *)NULL, cx, NULL, t389);
                                    if (gm_unw.kind) {
                                        goto gm_ll379;
                                    }
                                }
                            }
                            goto me384;
mn385:;
                            gm_trap("no match arm matched the scrutinee", gm_srcqr4ch56kv6bv, 388, 13);
me384:;
                        }
lc375:;
                        {
                            uint64_t t392 = gm_l44;
                            uint64_t r393; bool o394 = __builtin_add_overflow(t392, ((uint64_t)1ULL), &r393);
                            uint64_t t395 = r393;
                            gm_l44 = t395;
                        }
                    }
gm_ll379:;
                    if (gm_unw.frame == &F.h && gm_unw.label == 3) {
                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb375; }
                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc375; }
                    }
                    goto gm_lad;
lb375:;
                }
            }
            {
                uint8_t t396 = 0;
                F.ret = ((uint8_t)0);
                goto gm_fret;
            }
            goto ix369;
ie369:;
ix369:;
        }
        {
            void * t397 = (void *)((uint8_t *)&F.l36);
            gm_b16_8 t398 = GM_LD(gm_b16_8, (((uint8_t *)&F.l37) + 0));
            gm_b16_8 t399 = t398;
            gm_b16_8 t400 = GM_LD(gm_b16_8, ((uint8_t *)&t399));
            gm_tftb1odc739t5 t401 = gm_f_extend_vogsd72(t397, t400);
            gm_tftb1odc739t5 t402 = t401;
            if (((((uint8_t *)&t402))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t402) + 1, gm_srcqr4ch56kv6bv, 391, 9);
            gm_b0_1 t403 = GM_LD(gm_b0_1, (((uint8_t *)&t402) + 1));
        }
        goto ix307;
ie307:;
        {
            void * t405 = (void *)((uint8_t *)&F.l36);
            gm_b16_8 t406 = GM_LD(gm_b16_8, (((uint8_t *)&F.l27) + 0));
            gm_b16_8 t407 = t406;
            gm_b16_8 t408 = GM_LD(gm_b16_8, ((uint8_t *)&t407));
            gm_tftb1odc739t5 t409 = gm_f_extend_vogsd72(t405, t408);
            gm_tftb1odc739t5 t410 = t409;
            if (((((uint8_t *)&t410))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t410) + 1, gm_srcqr4ch56kv6bv, 393, 9);
            gm_b0_1 t411 = GM_LD(gm_b0_1, (((uint8_t *)&t410) + 1));
        }
ix307:;
    }
    {
        gm_f_list_epvducg((void *)&F.l46, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t413 = GM_LD(gm_b16_8, (((uint8_t *)&F.l34) + 0));
            gm_l47 = t413;
        }
        {
            gm_l48 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t415 = gm_l48;
                uint64_t t416 = GM_LD(uintptr_t, ((uint8_t *)&gm_l47) + 8);
                uint8_t t417 = (t415 < t416);
                if (!t417) goto lb414;
                {
                    uint64_t t420 = gm_l48;
                    uint64_t t421 = (uintptr_t)t420;
                    uint64_t t422 = GM_LD(uintptr_t, ((uint8_t *)&gm_l47) + 8);
                    gm_b16_8 t425 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l47)) + t421 * 16));
                    gm_l49 = t425;
                    {
                        uint8_t t428 = 0;
                        uint8_t t429 = gm_l35;
                        if (t429) {
                            gm_b16_8 t430 = gm_l49;
                            gm_b16_8 t431 = gm_l6;
                            gm_b16_8 t432 = t430;
                            gm_b16_8 t433 = t431;
                            uint8_t t434 = (gm_eq0iqiubfm7e(((uint8_t *)&t432), ((uint8_t *)&t433)));
                            t428 = t434;
                        } else t428 = 0;
                        uint8_t t435 = (uint8_t)!t428;
                        if (!t435) goto ie427;
                        {
                            void * t437 = (void *)((uint8_t *)&F.l46);
                            gm_b16_8 t438 = gm_l49;
                            gm_tftb1odc739t5 t439 = gm_f_push_3ef746b(t437, t438);
                            gm_tftb1odc739t5 t440 = t439;
                            if (((((uint8_t *)&t440))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t440) + 1, gm_srcqr4ch56kv6bv, 397, 38);
                            gm_b0_1 t441 = GM_LD(gm_b0_1, (((uint8_t *)&t440) + 1));
                        }
                        goto ix427;
ie427:;
ix427:;
                    }
                    goto me423;
mn424:;
                    gm_trap("no match arm matched the scrutinee", gm_srcqr4ch56kv6bv, 396, 5);
me423:;
                }
lc414:;
                {
                    uint64_t t443 = gm_l48;
                    uint64_t r444; bool o445 = __builtin_add_overflow(t443, ((uint64_t)1ULL), &r444);
                    uint64_t t446 = r444;
                    gm_l48 = t446;
                }
            }
lb414:;
        }
    }
    {
        uint8_t t448 = gm_l35;
        if (!t448) goto ie447;
        {
            void * t450 = (void *)((uint8_t *)&F.l46);
            gm_b16_8 t451 = gm_f_wasi_link_flags_6mu198c((void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tftb1odc739t5 t452 = gm_f_extend_vogsd72(t450, t451);
            gm_tftb1odc739t5 t453 = t452;
            if (((((uint8_t *)&t453))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t453) + 1, gm_srcqr4ch56kv6bv, 399, 15);
            gm_b0_1 t454 = GM_LD(gm_b0_1, (((uint8_t *)&t453) + 1));
        }
        goto ix447;
ie447:;
ix447:;
    }
    {
        gm_f_list_epvducg((void *)&F.l50, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t455 = (void *)((uint8_t *)&F.l50);
        gm_b16_8 t456;
        GM_ST(void *, (uint8_t *)&t456, (void *)(gm_s65qt8qo64cje));
        GM_ST(uintptr_t, (uint8_t *)&t456 + 8, (uintptr_t)(2));
        gm_tftb1odc739t5 t457 = gm_f_push_3ef746b(t455, t456);
        gm_tftb1odc739t5 t458 = t457;
        if (((((uint8_t *)&t458))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t458) + 1, gm_srcqr4ch56kv6bv, 401, 5);
        gm_b0_1 t459 = GM_LD(gm_b0_1, (((uint8_t *)&t458) + 1));
    }
    {
        void * t460 = (void *)((uint8_t *)&F.l50);
        gm_b16_8 t461 = gm_l3;
        gm_tftb1odc739t5 t462 = gm_f_push_3ef746b(t460, t461);
        gm_tftb1odc739t5 t463 = t462;
        if (((((uint8_t *)&t463))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t463) + 1, gm_srcqr4ch56kv6bv, 402, 5);
        gm_b0_1 t464 = GM_LD(gm_b0_1, (((uint8_t *)&t463) + 1));
    }
    {
        void * t465 = (void *)((uint8_t *)&F.l50);
        gm_b16_8 t466 = GM_LD(gm_b16_8, (((uint8_t *)&F.l36) + 0));
        gm_b16_8 t467 = t466;
        gm_b16_8 t468 = GM_LD(gm_b16_8, ((uint8_t *)&t467));
        gm_tftb1odc739t5 t469 = gm_f_extend_vogsd72(t465, t468);
        gm_tftb1odc739t5 t470 = t469;
        if (((((uint8_t *)&t470))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t470) + 1, gm_srcqr4ch56kv6bv, 403, 5);
        gm_b0_1 t471 = GM_LD(gm_b0_1, (((uint8_t *)&t470) + 1));
    }
    {
        if (!((((uint8_t *)&gm_l17))[16] != 0)) goto ie472;
        gm_b16_8 t473 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l17));
        gm_l51 = t473;
        {
            void * t475 = (void *)((uint8_t *)&F.l50);
            gm_b16_8 t476 = gm_l51;
            gm_tftb1odc739t5 t477 = gm_f_extend_vogsd72(t475, t476);
            gm_tftb1odc739t5 t478 = t477;
            if (((((uint8_t *)&t478))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t478) + 1, gm_srcqr4ch56kv6bv, 404, 25);
            gm_b0_1 t479 = GM_LD(gm_b0_1, (((uint8_t *)&t478) + 1));
        }
        goto ix472;
ie472:;
ix472:;
    }
    {
        if (!((((uint8_t *)&gm_l14))[16] != 0)) goto ie480;
        gm_b16_8 t481 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l14));
        gm_l52 = t481;
        {
            void * t483 = (void *)((uint8_t *)&F.l50);
            gm_b16_8 t484 = gm_l52;
            gm_tftb1odc739t5 t485 = gm_f_push_3ef746b(t483, t484);
            gm_tftb1odc739t5 t486 = t485;
            if (((((uint8_t *)&t486))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t486) + 1, gm_srcqr4ch56kv6bv, 405, 23);
            gm_b0_1 t487 = GM_LD(gm_b0_1, (((uint8_t *)&t486) + 1));
        }
        goto ix480;
ie480:;
ix480:;
    }
    {
        void * t488 = (void *)((uint8_t *)&F.l50);
        void * t489 = gm_l2;
        gm_b16_8 t490 = GM_LD(gm_b16_8, ((((uint8_t *)t489) + 0) + 0));
        gm_b16_8 t491 = t490;
        gm_b16_8 t492 = GM_LD(gm_b16_8, ((uint8_t *)&t491));
        gm_tftb1odc739t5 t493 = gm_f_extend_vogsd72(t488, t492);
        gm_tftb1odc739t5 t494 = t493;
        if (((((uint8_t *)&t494))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t494) + 1, gm_srcqr4ch56kv6bv, 406, 5);
        gm_b0_1 t495 = GM_LD(gm_b0_1, (((uint8_t *)&t494) + 1));
    }
    {
        void * t496 = (void *)((uint8_t *)&F.l50);
        void * t497 = gm_l1;
        gm_b16_8 t498 = GM_LD(gm_b16_8, (((uint8_t *)t497) + 32));
        gm_tftb1odc739t5 t499 = gm_f_extend_vogsd72(t496, t498);
        gm_tftb1odc739t5 t500 = t499;
        if (((((uint8_t *)&t500))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t500) + 1, gm_srcqr4ch56kv6bv, 407, 5);
        gm_b0_1 t501 = GM_LD(gm_b0_1, (((uint8_t *)&t500) + 1));
    }
    {
        void * t502 = (void *)((uint8_t *)&F.l50);
        void * t503 = gm_l2;
        void * t504 = gm_l5;
        gm_b16_8 t505 = GM_LD(gm_b16_8, (((uint8_t *)t504) + 192));
        gm_trqfvmjtt4sij t506;
        gm_f_library_args_lijr5kp((void *)&t506, (void *)NULL, cx, NULL, t503, t505);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_trqfvmjtt4sij t507 = t506;
        gm_b16_8 t508 = GM_LD(gm_b16_8, (((uint8_t *)&t507) + 0));
        gm_b16_8 t509 = t508;
        gm_b16_8 t510 = GM_LD(gm_b16_8, ((uint8_t *)&t509));
        gm_tftb1odc739t5 t511 = gm_f_extend_vogsd72(t502, t510);
        gm_tftb1odc739t5 t512 = t511;
        if (((((uint8_t *)&t512))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t512) + 1, gm_srcqr4ch56kv6bv, 408, 5);
        gm_b0_1 t513 = GM_LD(gm_b0_1, (((uint8_t *)&t512) + 1));
    }
    {
        void * t514 = (void *)((uint8_t *)&F.l50);
        gm_b16_8 t515;
        GM_ST(void *, (uint8_t *)&t515, (void *)(gm_s6077qnlc299h));
        GM_ST(uintptr_t, (uint8_t *)&t515 + 8, (uintptr_t)(3));
        gm_tftb1odc739t5 t516 = gm_f_push_3ef746b(t514, t515);
        gm_tftb1odc739t5 t517 = t516;
        if (((((uint8_t *)&t517))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t517) + 1, gm_srcqr4ch56kv6bv, 409, 5);
        gm_b0_1 t518 = GM_LD(gm_b0_1, (((uint8_t *)&t517) + 1));
    }
    {
        void * t519 = (void *)((uint8_t *)&F.l50);
        gm_b16_8 t520 = gm_f_windows_libs_hmrgs9s((void *)NULL, cx, NULL, &(*(gm_t0rfe1ou14h21 *)(((uint8_t *)&F.l4) + 400)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tftb1odc739t5 t521 = gm_f_extend_vogsd72(t519, t520);
        gm_tftb1odc739t5 t522 = t521;
        if (((((uint8_t *)&t522))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t522) + 1, gm_srcqr4ch56kv6bv, 410, 5);
        gm_b0_1 t523 = GM_LD(gm_b0_1, (((uint8_t *)&t522) + 1));
    }
    {
        void * t524 = gm_l1;
        gm_b16_8 t525 = GM_LD(gm_b16_8, (((uint8_t *)&F.l46) + 0));
        gm_b16_8 t526 = t525;
        gm_b16_8 t527 = GM_LD(gm_b16_8, ((uint8_t *)&t526));
        gm_b16_8 t528 = GM_LD(gm_b16_8, (((uint8_t *)&F.l50) + 0));
        gm_b16_8 t529 = t528;
        gm_b16_8 t530 = GM_LD(gm_b16_8, ((uint8_t *)&t529));
        gm_f_command_b2u33fl((void *)&F.l53, (void *)NULL, cx, NULL, t524, t527, t530);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t531 = gm_l1;
        gm_b16_8 t532 = GM_LD(gm_b16_8, (((uint8_t *)&F.l53) + 0));
        gm_b16_8 t533 = t532;
        gm_b16_8 t534 = GM_LD(gm_b16_8, ((uint8_t *)&t533));
        uint8_t t535 = gm_f_run_compiler_ulaqcb1((void *)NULL, cx, NULL, t531, t534);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l54 = t535;
    }
    {
        {
            gm_b16_8 t537 = GM_LD(gm_b16_8, (((uint8_t *)&F.l37) + 0));
            gm_l55 = t537;
        }
        {
            gm_l56 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t539 = gm_l56;
                uint64_t t540 = GM_LD(uintptr_t, ((uint8_t *)&gm_l55) + 8);
                uint8_t t541 = (t539 < t540);
                if (!t541) goto lb538;
                {
                    uint64_t t544 = gm_l56;
                    uint64_t t545 = (uintptr_t)t544;
                    uint64_t t546 = GM_LD(uintptr_t, ((uint8_t *)&gm_l55) + 8);
                    gm_b16_8 t549 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l55)) + t545 * 16));
                    gm_l57 = t549;
                    {
                        {
                            gm_b16_8 t552 = gm_l57;
                            gm_ttln0d0hd70en t553 = gm_f_remove_all_h0k1qkh((void *)NULL, cx, NULL, t552);
                            if (gm_unw.kind) {
                                goto gm_ll542;
                            }
                        }
                    }
                    goto me547;
mn548:;
                    gm_trap("no match arm matched the scrutinee", gm_srcqr4ch56kv6bv, 413, 5);
me547:;
                }
lc538:;
                {
                    uint64_t t555 = gm_l56;
                    uint64_t r556; bool o557 = __builtin_add_overflow(t555, ((uint64_t)1ULL), &r556);
                    uint64_t t558 = r556;
                    gm_l56 = t558;
                }
            }
gm_ll542:;
            if (gm_unw.frame == &F.h && gm_unw.label == 5) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb538; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc538; }
            }
            goto gm_lad;
lb538:;
        }
    }
    {
        uint8_t t560 = gm_l54;
        uint8_t t561 = (uint8_t)!t560;
        if (!t561) goto ie559;
        {
            uint8_t t563 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        goto ix559;
ie559:;
ix559:;
    }
    {
        uint8_t t565 = gm_l35;
        if (!t565) goto ie564;
        {
            gm_b16_8 t567 = gm_l3;
            gm_b16_8 t568 = gm_l6;
            uint8_t t569 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 33));
            gm_tl572d7579hns t570;
            gm_f_asyncify_bs28ebq((void *)&t570, (void *)NULL, cx, NULL, t567, t568, t569);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tl572d7579hns t571 = t570;
            gm_b0_1 t572;
            if (!((((uint8_t *)&t571))[0] != 0)) {
                gm_b0_1 t573 = GM_LD(gm_b0_1, (((uint8_t *)&t571) + 8));
                t572 = t573;
            } else {
                gm_b16_8 t574 = GM_LD(gm_b16_8, (((uint8_t *)&t571) + 8));
                gm_l58 = t574;
                {
                    gm_b16_8 t576 = gm_l58;
                    gm_b16_8 t577 = t576;
                    if (gm_pfu8dq5bikbh(cx, &gm_ty0iqiubfm7e, ((uint8_t *)&t577), gm_srcqr4ch56kv6bv, 417, 13)) {
                        goto gm_lad;
                    }
                    if (gm_pf454qfrpt6r(cx, gm_so514oj5nbhcu, 1, gm_srcqr4ch56kv6bv, 417, 13)) {
                        goto gm_lad;
                    }
                }
                uint8_t t578 = 0;
                F.ret = ((uint8_t)0);
                goto gm_fret;
            }
        }
        goto ix564;
ie564:;
ix564:;
    }
    {
        uint8_t t579 = 0;
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
inline __attribute__((aligned(4))) uint8_t gm_f_compile_cdm3hj7(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2, const void * a3) {
    struct gm_fr_compile_cdm3hj7 F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* root */
    gm_b16_8 gm_l2; /* exe */
    uint64_t gm_l4; /* d */
    uint32_t gm_l5; /* main */
    gm_tdvq40fmif5j0 gm_l6; /* entries */
    GM_STACK_CHECK(&F, 679u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    memcpy(&F.l3, a3, sizeof F.l3);
    {
        void * t2 = gm_l0;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 0));
        uint64_t t4 = gm_l1;
        gm_b16_8 t5;
        GM_ST(void *, (uint8_t *)&t5, (void *)(gm_sog4rksrmsqd2));
        GM_ST(uintptr_t, (uint8_t *)&t5 + 8, (uintptr_t)(4));
        gm_tf5srh4mp4b3i t6 = gm_f_lookup_59juqoo(t3, t4, t5);
        gm_tf5srh4mp4b3i t7 = t6;
        uint64_t t8;
        if (((((uint8_t *)&t7))[8] != 0)) {
            uint64_t t9 = GM_LD(uint64_t, (((uint8_t *)&t7) + 0));
            t8 = t9;
        } else {
            {
                if (gm_pf454qfrpt6r(cx, gm_setv7ejg35cg9, 42, gm_srcqr4ch56kv6bv, 85, 9)) {
                    goto gm_lad;
                }
            }
            uint8_t t11 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        gm_l4 = t8;
    }
    {
        void * t13 = gm_l0;
        uint64_t t14 = gm_l4;
        gm_t6qooom8qa2do t15;
        gm_f_resolve_decl_pus042v((void *)&t15, (void *)NULL, cx, NULL, t13, t14);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t6qooom8qa2do t16 = t15;
        uint8_t t17 = GM_LD(uint8_t, (((uint8_t *)&t16) + 0));
        if (t17 != ((uint8_t)5ULL)) goto ie12;
        uint32_t t18 = GM_LD(uint32_t, (((uint8_t *)&t16) + 8));
        gm_l5 = t18;
        {
            void * t20 = gm_l0;
            uint32_t t21 = gm_l5;
            uint32_t t22 = gm_f_entry_point_n2gu79u((void *)NULL, cx, NULL, t20, t21);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tdvq40fmif5j0 t23;
            GM_ST(uint32_t, ((uint8_t *)&t23 + 0), t22);
            gm_l6 = t23;
        }
        {
            void * t24 = gm_l0;
            void * t25 = ((uint8_t *)&gm_l6);
            if ((uintptr_t)0 > (uintptr_t)1 || (uintptr_t)1 > (uintptr_t)1) gm_trap_slice((uintptr_t)0, (uintptr_t)1, 1, gm_srcqr4ch56kv6bv, 90, 31);
            gm_b16_8 t26;
            GM_ST(void *, (uint8_t *)&t26, (void *)(((uint8_t *)t25 + (uintptr_t)0 * 4)));
            GM_ST(uintptr_t, (uint8_t *)&t26 + 8, (uintptr_t)((uintptr_t)1 - (uintptr_t)0));
            gm_b16_8 t27 = t26;
            gm_b16_8 t28 = GM_LD(gm_b16_8, ((uint8_t *)&t27));
            gm_b16_8 t29 = gm_l2;
            uint8_t t30 = gm_f_compile_entries_l9qlniu((void *)NULL, cx, NULL, t24, t28, ((uint8_t)0), t29, &(*(gm_tdkfkbgm73fql *)((uint8_t *)&F.l3)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t31 = 0;
            F.ret = t30;
            goto gm_fret;
        }
        goto ix12;
ie12:;
        {
            uint8_t t33 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
ix12:;
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
inline __attribute__((aligned(4))) uint8_t gm_f_compile_entries_l9qlniu(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, uint8_t a2, gm_b16_8 a3, const void * a4) {
    struct gm_fr_compile_entries_l9qlniu F;
    void * gm_l0; /* s */
    gm_b16_8 gm_l1; /* entries */
    uint8_t gm_l2; /* select */
    gm_b16_8 gm_l3; /* exe */
    GM_STACK_CHECK(&F, 1119u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    memcpy(&F.l4, a4, sizeof F.l4);
    {
        void * t2 = gm_l0;
        gm_b16_8 t3 = gm_l1;
        gm_b16_8 t4 = gm_l1;
        uint8_t t5 = gm_l2;
        gm_t5i6e9e007icp t6;
        gm_f_generate_program_9mr9pv8((void *)&t6, (void *)NULL, cx, NULL, t2, t3, t4, t5, &(*(gm_tdkfkbgm73fql *)((uint8_t *)&F.l4)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t5i6e9e007icp t7 = t6;
        gm_tt1457on428g4 t8;
        if (((((uint8_t *)&t7))[248] != 0)) {
            gm_tt1457on428g4 t9 = GM_LD(gm_tt1457on428g4, (((uint8_t *)&t7) + 0));
            t8 = t9;
        } else {
            uint8_t t10 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        F.l5 = t8;
    }
    {
        void * t11 = (void *)((uint8_t *)&F.l5);
        gm_b16_8 t12 = gm_l3;
        uint8_t t13 = gm_f_compile_built_8o9nmfu((void *)NULL, cx, NULL, t11, t12, &(*(gm_tdkfkbgm73fql *)((uint8_t *)&F.l4)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
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
__attribute__((aligned(4))) uint8_t gm_f_compile_object_k7kbtpr(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_b16_8 a2, const void * a3) {
    struct gm_fr_compile_object_k7kbtpr F;
    void * gm_l0; /* b */
    void * gm_l1; /* tc */
    gm_b16_8 gm_l2; /* obj */
    gm_b16_8 gm_l4; /* c_file */
    gm_b16_8 gm_l5; /* opt */
    gm_b16_8 gm_l6; /* prelude */
    gm_b16_8 gm_l7; /* code */
    GM_STACK_CHECK(&F, 1257u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    memcpy(&F.l3, a3, sizeof F.l3);
    {
        gm_b16_8 t2 = gm_l2;
        gm_tnpifv20irnph t3;
        GM_ST(gm_b16_8, ((uint8_t *)&t3 + 0), t2);
        gm_tabb710d9mq2e t4;
        gm_f_format_cncadt9((void *)&t4, (void *)NULL, cx, NULL, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t5 = t4;
        if (((((uint8_t *)&t5))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t5) + 8, gm_srcqr4ch56kv6bv, 456, 15);
        gm_b16_8 t6 = GM_LD(gm_b16_8, (((uint8_t *)&t5) + 8));
        gm_l4 = t6;
    }
    {
        void * t7 = (void *)((uint8_t *)&F.l3);
        gm_b16_8 t8 = gm_f_opt_level_gnm5dcm(t7);
        gm_l5 = t8;
    }
    {
        gm_b16_8 t9;
        void * t11 = gm_l0;
        uint8_t t12 = GM_LD(uint8_t, ((((uint8_t *)t11) + 0) + 209));
        if (!t12) goto ie10;
        gm_b16_8 t14;
        GM_ST(void *, (uint8_t *)&t14, (void *)(gm_s7qmtc95nc11i));
        GM_ST(uintptr_t, (uint8_t *)&t14 + 8, (uintptr_t)(23));
        gm_b16_8 t15 = t14;
        t9 = t15;
        goto ix10;
ie10:;
        gm_b16_8 t17;
        GM_ST(void *, (uint8_t *)&t17, (void *)(gm_sr4qbrk6tlgaa));
        GM_ST(uintptr_t, (uint8_t *)&t17 + 8, (uintptr_t)(0));
        gm_b16_8 t18 = t17;
        t9 = t18;
ix10:;
        gm_tnpifv20irnph t19;
        GM_ST(gm_b16_8, ((uint8_t *)&t19 + 0), t9);
        gm_tabb710d9mq2e t20;
        gm_f_format_6gakl8s((void *)&t20, (void *)NULL, cx, NULL, t19);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t21 = t20;
        if (((((uint8_t *)&t21))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t21) + 8, gm_srcqr4ch56kv6bv, 458, 16);
        gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)&t21) + 8));
        gm_l6 = t22;
    }
    {
        gm_t0uuhaljsf410 t23;
        gm_b16_8 t24 = gm_l6;
        GM_ST(gm_b16_8, (uint8_t *)&t23 + 0, t24);
        void * t25 = gm_l0;
        gm_b16_8 t26 = GM_LD(gm_b16_8, (((uint8_t *)t25) + 216));
        GM_ST(gm_b16_8, (uint8_t *)&t23 + 16, t26);
        void * t27 = gm_l0;
        gm_b16_8 t28 = GM_LD(gm_b16_8, (((uint8_t *)t27) + 232));
        GM_ST(gm_b16_8, (uint8_t *)&t23 + 32, t28);
        void * t29 = gm_l0;
        gm_b16_8 t30 = GM_LD(gm_b16_8, ((((uint8_t *)t29) + 0) + 0));
        GM_ST(gm_b16_8, (uint8_t *)&t23 + 48, t30);
        gm_tabb710d9mq2e t31;
        gm_f_format_f2pdud5((void *)&t31, (void *)NULL, cx, NULL, &t23);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t32 = t31;
        if (((((uint8_t *)&t32))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t32) + 8, gm_srcqr4ch56kv6bv, 459, 20);
        gm_b16_8 t33 = GM_LD(gm_b16_8, (((uint8_t *)&t32) + 8));
        gm_b16_8 t34 = t33;
        gm_b16_8 t35 = GM_LD(gm_b16_8, ((uint8_t *)&t34));
        gm_l7 = t35;
    }
    {
        uint8_t t37 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 33));
        if (!t37) goto ie36;
        {
            gm_b16_8 t39 = gm_l7;
            gm_b16_8 t40 = gm_l4;
            gm_b16_8 t41 = t40;
            gm_b16_8 t42 = GM_LD(gm_b16_8, ((uint8_t *)&t41));
            gm_b16_8 t43 = gm_f_absolute_6pqhsaj((void *)NULL, cx, NULL, t42);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_b16_8 t44 = gm_f_resolve_lines_h4bf30f((void *)NULL, cx, NULL, t39, t43);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l7 = t44;
        }
        goto ix36;
ie36:;
ix36:;
    }
    {
        gm_b16_8 t46 = gm_l4;
        gm_b16_8 t47 = t46;
        gm_b16_8 t48 = GM_LD(gm_b16_8, ((uint8_t *)&t47));
        gm_b16_8 t49 = gm_l7;
        uint8_t t50 = gm_f_write_generated_c_kb09043((void *)NULL, cx, NULL, t48, t49);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t51 = (uint8_t)!t50;
        if (!t51) goto ie45;
        {
            uint8_t t53 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        goto ix45;
ie45:;
ix45:;
    }
    {
        gm_f_list_epvducg((void *)&F.l8, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t54 = (void *)((uint8_t *)&F.l8);
        void * t55 = ((uint8_t *)gm_c1a92ugfgjv);
        if ((uintptr_t)0 > (uintptr_t)7 || (uintptr_t)7 > (uintptr_t)7) gm_trap_slice((uintptr_t)0, (uintptr_t)7, 7, gm_srcqr4ch56kv6bv, 463, 18);
        gm_b16_8 t56;
        GM_ST(void *, (uint8_t *)&t56, (void *)(((uint8_t *)t55 + (uintptr_t)0 * 16)));
        GM_ST(uintptr_t, (uint8_t *)&t56 + 8, (uintptr_t)((uintptr_t)7 - (uintptr_t)0));
        gm_tftb1odc739t5 t57 = gm_f_extend_vogsd72(t54, t56);
        gm_tftb1odc739t5 t58 = t57;
        if (((((uint8_t *)&t58))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t58) + 1, gm_srcqr4ch56kv6bv, 463, 5);
        gm_b0_1 t59 = GM_LD(gm_b0_1, (((uint8_t *)&t58) + 1));
    }
    {
        void * t60 = (void *)((uint8_t *)&F.l8);
        gm_b16_8 t61;
        GM_ST(void *, (uint8_t *)&t61, (void *)(gm_s463po27vtmnn));
        GM_ST(uintptr_t, (uint8_t *)&t61 + 8, (uintptr_t)(14));
        gm_tftb1odc739t5 t62 = gm_f_push_3ef746b(t60, t61);
        gm_tftb1odc739t5 t63 = t62;
        if (((((uint8_t *)&t63))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t63) + 1, gm_srcqr4ch56kv6bv, 464, 5);
        gm_b0_1 t64 = GM_LD(gm_b0_1, (((uint8_t *)&t63) + 1));
    }
    {
        void * t65 = (void *)((uint8_t *)&F.l8);
        gm_b16_8 t66 = gm_l5;
        gm_tftb1odc739t5 t67 = gm_f_push_3ef746b(t65, t66);
        gm_tftb1odc739t5 t68 = t67;
        if (((((uint8_t *)&t68))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t68) + 1, gm_srcqr4ch56kv6bv, 465, 5);
        gm_b0_1 t69 = GM_LD(gm_b0_1, (((uint8_t *)&t68) + 1));
    }
    {
        uint8_t t71 = ((GM_LD(uint8_t, (((uint8_t *)&F.l3) + 4) + 0) == ((uint8_t)0ULL)));
        if (!t71) goto ie70;
        uint8_t t72 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 33));
        uint8_t t73 = (uint8_t)!t72;
        if (!t73) goto ie70;
        {
            void * t75 = (void *)((uint8_t *)&F.l8);
            void * t76 = (void *)((uint8_t *)&F.l3);
            void * t77 = gm_l1;
            gm_th2lqbo77pvsc t78;
            gm_f_compiler_version_hj63jgf((void *)&t78, (void *)NULL, cx, NULL, t77);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_b16_8 t79 = gm_f_debug_flags_gv15c46(t76, &t78);
            gm_tftb1odc739t5 t80 = gm_f_extend_vogsd72(t75, t79);
            gm_tftb1odc739t5 t81 = t80;
            if (((((uint8_t *)&t81))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t81) + 1, gm_srcqr4ch56kv6bv, 466, 54);
            gm_b0_1 t82 = GM_LD(gm_b0_1, (((uint8_t *)&t81) + 1));
        }
        goto ix70;
ie70:;
ix70:;
    }
    {
        uint8_t t84 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 33));
        if (!t84) goto ie83;
        {
            void * t86 = (void *)((uint8_t *)&F.l8);
            gm_b16_8 t87;
            GM_ST(void *, (uint8_t *)&t87, (void *)(gm_sn0peadng5i31));
            GM_ST(uintptr_t, (uint8_t *)&t87 + 8, (uintptr_t)(2));
            gm_tftb1odc739t5 t88 = gm_f_push_3ef746b(t86, t87);
            gm_tftb1odc739t5 t89 = t88;
            if (((((uint8_t *)&t89))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t89) + 1, gm_srcqr4ch56kv6bv, 467, 26);
            gm_b0_1 t90 = GM_LD(gm_b0_1, (((uint8_t *)&t89) + 1));
        }
        goto ix83;
ie83:;
ix83:;
    }
    {
        gm_b16_8 t91;
        GM_ST(void *, (uint8_t *)&t91, (void *)(gm_s4vd6e3mkiiso));
        GM_ST(uintptr_t, (uint8_t *)&t91 + 8, (uintptr_t)(2));
        GM_ST(gm_b16_8, (uint8_t *)&F.l9 + 0, t91);
        gm_b16_8 t92;
        GM_ST(void *, (uint8_t *)&t92, (void *)(gm_s65qt8qo64cje));
        GM_ST(uintptr_t, (uint8_t *)&t92 + 8, (uintptr_t)(2));
        GM_ST(gm_b16_8, (uint8_t *)&F.l9 + 16, t92);
        gm_b16_8 t93 = gm_l2;
        GM_ST(gm_b16_8, (uint8_t *)&F.l9 + 32, t93);
        gm_b16_8 t94 = gm_l4;
        gm_b16_8 t95 = t94;
        gm_b16_8 t96 = GM_LD(gm_b16_8, ((uint8_t *)&t95));
        GM_ST(gm_b16_8, (uint8_t *)&F.l9 + 48, t96);
    }
    {
        void * t97 = gm_l1;
        gm_b16_8 t98 = GM_LD(gm_b16_8, (((uint8_t *)&F.l8) + 0));
        gm_b16_8 t99 = t98;
        gm_b16_8 t100 = GM_LD(gm_b16_8, ((uint8_t *)&t99));
        void * t101 = ((uint8_t *)&F.l9);
        if ((uintptr_t)0 > (uintptr_t)4 || (uintptr_t)4 > (uintptr_t)4) gm_trap_slice((uintptr_t)0, (uintptr_t)4, 4, gm_srcqr4ch56kv6bv, 469, 42);
        gm_b16_8 t102;
        GM_ST(void *, (uint8_t *)&t102, (void *)(((uint8_t *)t101 + (uintptr_t)0 * 16)));
        GM_ST(uintptr_t, (uint8_t *)&t102 + 8, (uintptr_t)((uintptr_t)4 - (uintptr_t)0));
        gm_b16_8 t103 = t102;
        gm_b16_8 t104 = GM_LD(gm_b16_8, ((uint8_t *)&t103));
        gm_f_command_b2u33fl((void *)&F.l10, (void *)NULL, cx, NULL, t97, t100, t104);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t105 = gm_l1;
        gm_b16_8 t106 = GM_LD(gm_b16_8, (((uint8_t *)&F.l10) + 0));
        gm_b16_8 t107 = t106;
        gm_b16_8 t108 = GM_LD(gm_b16_8, ((uint8_t *)&t107));
        uint8_t t109 = gm_f_run_compiler_ulaqcb1((void *)NULL, cx, NULL, t105, t108);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t110 = 0;
        F.ret = t109;
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
__attribute__((aligned(4))) uint8_t gm_f_compile_objects_mu5secm(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2, gm_b16_8 a3, uint64_t a4) {
    struct gm_fr_compile_objects_mu5secm F;
    void * gm_l0; /* tc */
    gm_b16_8 gm_l1; /* flags */
    gm_b16_8 gm_l2; /* c_files */
    gm_b16_8 gm_l3; /* objects */
    uint64_t gm_l4; /* jobs */
    gm_b16_8 gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    uint64_t gm_l8; /* i */
    gm_b16_8 gm_l9; /* c */
    gm_b16_8 gm_l12; /* <tmp> */
    uint64_t gm_l13; /* <tmp> */
    uint64_t gm_l15; /* <tmp> */
    uint64_t gm_l16; /* <tmp> */
    gm_tm98ap4112hee gm_l17; /* t */
    gm_b16_8 gm_l18; /* <tmp> */
    uint64_t gm_l19; /* <tmp> */
    gm_tm98ap4112hee gm_l20; /* t */
    gm_b16_8 gm_l21; /* <tmp> */
    uint64_t gm_l22; /* <tmp> */
    gm_tl572d7579hns gm_l23; /* r */
    gm_b16_8 gm_l24; /* err */
    GM_STACK_CHECK(&F, 1351u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    {
        gm_f_list_242hv68((void *)&F.l5, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t3 = gm_l2;
            gm_l6 = t3;
        }
        {
            gm_l7 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t5 = gm_l7;
                uint64_t t6 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                uint8_t t7 = (t5 < t6);
                if (!t7) goto lb4;
                {
                    gm_tf1bjg81u0glv t10;
                    uint64_t t11 = gm_l7;
                    GM_ST(uint64_t, (uint8_t *)&t10 + 0, t11);
                    uint64_t t12 = gm_l7;
                    uint64_t t13 = (uintptr_t)t12;
                    uint64_t t14 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                    gm_b16_8 t15 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t13 * 16));
                    GM_ST(gm_b16_8, (uint8_t *)&t10 + 8, t15);
                    gm_tf1bjg81u0glv t16 = t10;
                    uint64_t t19 = GM_LD(uint64_t, (((uint8_t *)&t16) + 0));
                    gm_l8 = t19;
                    gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&t16) + 8));
                    gm_l9 = t20;
                    {
                        gm_b16_8 t22;
                        GM_ST(void *, (uint8_t *)&t22, (void *)(gm_s4vd6e3mkiiso));
                        GM_ST(uintptr_t, (uint8_t *)&t22 + 8, (uintptr_t)(2));
                        GM_ST(gm_b16_8, (uint8_t *)&F.l10 + 0, t22);
                        gm_b16_8 t23;
                        GM_ST(void *, (uint8_t *)&t23, (void *)(gm_s65qt8qo64cje));
                        GM_ST(uintptr_t, (uint8_t *)&t23 + 8, (uintptr_t)(2));
                        GM_ST(gm_b16_8, (uint8_t *)&F.l10 + 16, t23);
                        uint64_t t24 = gm_l8;
                        uint64_t t25 = (uintptr_t)t24;
                        uint64_t t26 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                        if (t25 >= (uintptr_t)(t26)) gm_trap_index(t25, t26, gm_srcqr4ch56kv6bv, 498, 39);
                        gm_b16_8 t27 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t25 * 16));
                        GM_ST(gm_b16_8, (uint8_t *)&F.l10 + 32, t27);
                        gm_b16_8 t28 = gm_l9;
                        GM_ST(gm_b16_8, (uint8_t *)&F.l10 + 48, t28);
                    }
                    {
                        void * t29 = (void *)((uint8_t *)&F.l5);
                        void * t30 = gm_l0;
                        gm_b16_8 t31 = gm_l1;
                        void * t32 = ((uint8_t *)&F.l10);
                        if ((uintptr_t)0 > (uintptr_t)4 || (uintptr_t)4 > (uintptr_t)4) gm_trap_slice((uintptr_t)0, (uintptr_t)4, 4, gm_srcqr4ch56kv6bv, 499, 43);
                        gm_b16_8 t33;
                        GM_ST(void *, (uint8_t *)&t33, (void *)(((uint8_t *)t32 + (uintptr_t)0 * 16)));
                        GM_ST(uintptr_t, (uint8_t *)&t33 + 8, (uintptr_t)((uintptr_t)4 - (uintptr_t)0));
                        gm_b16_8 t34 = t33;
                        gm_b16_8 t35 = GM_LD(gm_b16_8, ((uint8_t *)&t34));
                        gm_trqfvmjtt4sij t36;
                        gm_f_command_b2u33fl((void *)&t36, (void *)NULL, cx, NULL, t30, t31, t35);
                        if (gm_unw.kind) {
                            goto gm_ll8;
                        }
                        gm_trqfvmjtt4sij t37 = t36;
                        gm_b16_8 t38 = GM_LD(gm_b16_8, (((uint8_t *)&t37) + 0));
                        gm_b16_8 t39 = t38;
                        gm_b16_8 t40 = GM_LD(gm_b16_8, ((uint8_t *)&t39));
                        gm_tftb1odc739t5 t41 = gm_f_push_0hkhfv5(t29, t40);
                        gm_tftb1odc739t5 t42 = t41;
                        if (((((uint8_t *)&t42))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t42) + 1, gm_srcqr4ch56kv6bv, 499, 9);
                        gm_b0_1 t43 = GM_LD(gm_b0_1, (((uint8_t *)&t42) + 1));
                    }
                    goto me17;
mn18:;
                    gm_trap("no match arm matched the scrutinee", gm_srcqr4ch56kv6bv, 497, 5);
me17:;
                }
lc4:;
                {
                    uint64_t t45 = gm_l7;
                    uint64_t r46; bool o47 = __builtin_add_overflow(t45, ((uint64_t)1ULL), &r46);
                    uint64_t t48 = r46;
                    gm_l7 = t48;
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
        memset((uint8_t *)&F.l11 + 68, 0, 4);
        gm_b16_8 t49 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 0));
        gm_b16_8 t50 = t49;
        gm_b16_8 t51 = GM_LD(gm_b16_8, ((uint8_t *)&t50));
        GM_ST(gm_b16_8, (uint8_t *)&F.l11 + 0, t51);
        GM_ST(uint64_t, (uint8_t *)&F.l11 + 16, ((uint64_t)0ULL));
        gm_f_list_fnnocfv((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l11 + 24)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t88rcucg1b7uk t52 = GM_CONV(gm_t88rcucg1b7uk, gm_f_mutex_t7if00c());
        GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&F.l11 + 64, t52);
    }
    {
        {
            gm_b16_8 t54 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 0));
            gm_l12 = t54;
        }
        {
            gm_l13 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t56 = gm_l13;
                uint64_t t57 = GM_LD(uintptr_t, ((uint8_t *)&gm_l12) + 8);
                uint8_t t58 = (t56 < t57);
                if (!t58) goto lb55;
                {
                    uint64_t t61 = gm_l13;
                    uint64_t t62 = (uintptr_t)t61;
                    uint64_t t63 = GM_LD(uintptr_t, ((uint8_t *)&gm_l12) + 8);
                    {
                        void * t67 = (void *)(((uint8_t *)&F.l11) + 24);
                        gm_tl572d7579hns t68;
                        GM_ST(uint8_t, (uint8_t *)&t68 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t68 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t68 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t68 + 8, 0);
                        GM_ST(uint64_t, (uint8_t *)&t68 + 16, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t68 + 0), ((uint8_t)0ULL));
                        gm_tftb1odc739t5 t69 = gm_f_push_fp59o9c(t67, &t68);
                        gm_tftb1odc739t5 t70 = t69;
                        if (((((uint8_t *)&t70))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t70) + 1, gm_srcqr4ch56kv6bv, 502, 28);
                        gm_b0_1 t71 = GM_LD(gm_b0_1, (((uint8_t *)&t70) + 1));
                    }
                    goto me64;
mn65:;
                    gm_trap("no match arm matched the scrutinee", gm_srcqr4ch56kv6bv, 502, 5);
me64:;
                }
lc55:;
                {
                    uint64_t t73 = gm_l13;
                    uint64_t r74; bool o75 = __builtin_add_overflow(t73, ((uint64_t)1ULL), &r74);
                    uint64_t t76 = r74;
                    gm_l13 = t76;
                }
            }
lb55:;
        }
    }
    {
        gm_f_list_7vplt15((void *)&F.l14, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_l16 = ((uint64_t)0ULL);
        }
        {
            uint64_t t78 = gm_l4;
            uint64_t t79 = GM_LD(uintptr_t, (((uint8_t *)&F.l5) + 0) + 8);
            uint64_t t80 = (t79 > ((uint64_t)1ULL) ? t79 : ((uint64_t)1ULL));
            uint64_t t81 = (t78 < ((uint64_t)1ULL) ? ((uint64_t)1ULL) : (t78 > t80 ? t80 : t78));
            gm_l15 = t81;
        }
        {
            for (;;) {
                uint64_t t83 = gm_l16;
                uint64_t t84 = gm_l15;
                uint8_t t85 = (t83 < t84);
                if (!t85) goto lb82;
                {
                    uint64_t t88 = gm_l16;
                    uint64_t t89 = t88;
                    {
                        void * t93 = (void *)((uint8_t *)&F.l11);
                        gm_t62ebbi2i5rkd t94 = gm_f_spawn_et0s5ea(((void *)gm_f_compile_worker_5cv5pj9), t93);
                        gm_t62ebbi2i5rkd t95 = t94;
                        gm_tm98ap4112hee t96;
                        if (!((((uint8_t *)&t95))[0] != 0)) {
                            gm_tm98ap4112hee t97 = GM_LD(gm_tm98ap4112hee, (((uint8_t *)&t95) + 8));
                            t96 = t97;
                        } else {
                            goto lb82;
                        }
                        gm_l17 = t96;
                    }
                    {
                        void * t98 = (void *)((uint8_t *)&F.l14);
                        gm_tm98ap4112hee t99 = gm_l17;
                        gm_tftb1odc739t5 t100 = gm_f_push_6ct7n6b(t98, GM_CONV(gm_csfq64m1m1rt6m, t99));
                        gm_tftb1odc739t5 t101 = t100;
                        if (((((uint8_t *)&t101))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t101) + 1, gm_srcqr4ch56kv6bv, 506, 9);
                        gm_b0_1 t102 = GM_LD(gm_b0_1, (((uint8_t *)&t101) + 1));
                    }
                    goto me90;
mn91:;
                    gm_trap("no match arm matched the scrutinee", gm_srcqr4ch56kv6bv, 504, 5);
me90:;
                }
lc82:;
                {
                    uint64_t t104 = gm_l16;
                    uint64_t r105; bool o106 = __builtin_add_overflow(t104, ((uint64_t)1ULL), &r105);
                    uint64_t t107 = r105;
                    gm_l16 = t107;
                }
            }
lb82:;
        }
    }
    {
        uint64_t t109 = GM_LD(uintptr_t, (((uint8_t *)&F.l14) + 0) + 8);
        uint8_t t110 = ((t109 == ((uint64_t)0ULL)));
        if (!t110) goto ie108;
        {
            void * t112 = (void *)((uint8_t *)&F.l11);
            gm_f_compile_worker_5cv5pj9(t112);
        }
        goto ix108;
ie108:;
ix108:;
    }
    {
        {
            gm_b16_8 t114 = GM_LD(gm_b16_8, (((uint8_t *)&F.l14) + 0));
            gm_l18 = t114;
        }
        {
            gm_l19 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t116 = gm_l19;
                uint64_t t117 = GM_LD(uintptr_t, ((uint8_t *)&gm_l18) + 8);
                uint8_t t118 = (t116 < t117);
                if (!t118) goto lb115;
                {
                    uint64_t t121 = gm_l19;
                    uint64_t t122 = (uintptr_t)t121;
                    uint64_t t123 = GM_LD(uintptr_t, ((uint8_t *)&gm_l18) + 8);
                    gm_tm98ap4112hee t126 = GM_LD(gm_tm98ap4112hee, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l18)) + t122 * 8));
                    gm_l20 = t126;
                    {
                        gm_tm98ap4112hee t128 = gm_l20;
                        gm_f_join_r8j2dp1(GM_CONV(gm_csfq64m1m1rt6m, t128));
                    }
                    goto me124;
mn125:;
                    gm_trap("no match arm matched the scrutinee", gm_srcqr4ch56kv6bv, 509, 5);
me124:;
                }
lc115:;
                {
                    uint64_t t130 = gm_l19;
                    uint64_t r131; bool o132 = __builtin_add_overflow(t130, ((uint64_t)1ULL), &r131);
                    uint64_t t133 = r131;
                    gm_l19 = t133;
                }
            }
lb115:;
        }
    }
    {
        {
            gm_b16_8 t135 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l11) + 24) + 0));
            gm_l21 = t135;
        }
        {
            gm_l22 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t137 = gm_l22;
                uint64_t t138 = GM_LD(uintptr_t, ((uint8_t *)&gm_l21) + 8);
                uint8_t t139 = (t137 < t138);
                if (!t139) goto lb136;
                {
                    uint64_t t142 = gm_l22;
                    uint64_t t143 = (uintptr_t)t142;
                    uint64_t t144 = GM_LD(uintptr_t, ((uint8_t *)&gm_l21) + 8);
                    gm_tl572d7579hns t147 = GM_LD(gm_tl572d7579hns, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l21)) + t143 * 24));
                    gm_l23 = t147;
                    {
                        uint8_t t154 = GM_LD(uint8_t, (((uint8_t *)&gm_l23) + 0));
                        switch (t154) { case ((uint8_t)0ULL): goto ma150; case ((uint8_t)1ULL): goto ma151; case ((uint8_t)2ULL): goto ma152; default: goto ma153; }
ma150:;
                        uint8_t t156 = GM_LD(uint8_t, (((uint8_t *)&gm_l23) + 0));
                        if (t156 != ((uint8_t)0ULL)) goto mn155;
                        goto me149;
mn155:;
ma151:;
                        uint8_t t159 = GM_LD(uint8_t, (((uint8_t *)&gm_l23) + 0));
                        if (t159 != ((uint8_t)1ULL)) goto mn158;
                        {
                            void * t161 = gm_l0;
                            gm_b16_8 t162 = gm_f_display_30kc1i9((void *)NULL, cx, NULL, t161);
                            if (gm_unw.kind) {
                                goto gm_ll140;
                            }
                            gm_b16_8 t163 = t162;
                            if (gm_pf454qfrpt6r(cx, gm_sr3et5tffndla, 27, gm_srcqr4ch56kv6bv, 514, 17)) {
                                goto gm_ll140;
                            }
                            if (gm_pfu8dq5bikbh(cx, &gm_ty0iqiubfm7e, ((uint8_t *)&t163), gm_srcqr4ch56kv6bv, 514, 17)) {
                                goto gm_ll140;
                            }
                            if (gm_pf454qfrpt6r(cx, gm_s622mvvjraklk, 42, gm_srcqr4ch56kv6bv, 514, 17)) {
                                goto gm_ll140;
                            }
                        }
                        {
                            uint8_t t164 = 0;
                            F.ret = ((uint8_t)0);
                            goto gm_fret;
                        }
                        goto me149;
mn158:;
ma152:;
                        uint8_t t166 = GM_LD(uint8_t, (((uint8_t *)&gm_l23) + 0));
                        if (t166 != ((uint8_t)2ULL)) goto mn165;
                        gm_b16_8 t167 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l23) + 8));
                        gm_l24 = t167;
                        {
                            gm_b16_8 t169 = gm_l24;
                            gm_b16_8 t170 = t169;
                            if (gm_pf454qfrpt6r(cx, gm_s1ctr86inn5iu, 23, gm_srcqr4ch56kv6bv, 518, 17)) {
                                goto gm_ll140;
                            }
                            if (gm_pfu8dq5bikbh(cx, &gm_ty0iqiubfm7e, ((uint8_t *)&t170), gm_srcqr4ch56kv6bv, 518, 17)) {
                                goto gm_ll140;
                            }
                        }
                        {
                            uint8_t t171 = 0;
                            F.ret = ((uint8_t)0);
                            goto gm_fret;
                        }
                        goto me149;
mn165:;
ma153:;
                        gm_trap("no match arm matched the scrutinee", gm_srcqr4ch56kv6bv, 511, 9);
me149:;
                    }
                    goto me145;
mn146:;
                    gm_trap("no match arm matched the scrutinee", gm_srcqr4ch56kv6bv, 510, 5);
me145:;
                }
lc136:;
                {
                    uint64_t t173 = gm_l22;
                    uint64_t r174; bool o175 = __builtin_add_overflow(t173, ((uint64_t)1ULL), &r174);
                    uint64_t t176 = r174;
                    gm_l22 = t176;
                }
            }
gm_ll140:;
            if (gm_unw.frame == &F.h && gm_unw.label == 4) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb136; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc136; }
            }
            goto gm_lad;
lb136:;
        }
    }
    {
        uint8_t t177 = 0;
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
inline __attribute__((aligned(4))) void gm_f_compile_one_4djvgo4(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_compile_one_4djvgo4 F;
    gm_b16_8 gm_l0; /* argv */
    gm_tm98ap4112hee gm_l1; /* c */
    gm_te0mgnjdphs16 gm_l4; /* chunk */
    gm_tsoiri44d0i6q gm_l5; /* status */
    GM_STACK_CHECK(&F, 739u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_b16_8 t2 = gm_l0;
        gm_t32dckm4jfarl t3;
        memset((uint8_t *)&t3 + 3, 0, 5);
        memset((uint8_t *)&t3 + 50, 0, 6);
        gm_t9qkh0rsooc8d t4 = GM_LD(gm_t9qkh0rsooc8d, gm_cc7b06pnl1q);
        GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&t3 + 0, t4);
        gm_t9qkh0rsooc8d t5;
        GM_ST(uint8_t, ((uint8_t *)&t5 + 0), ((uint8_t)1ULL));
        GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&t3 + 1, t5);
        gm_t9qkh0rsooc8d t6;
        GM_ST(uint8_t, ((uint8_t *)&t6 + 0), ((uint8_t)2ULL));
        GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&t3 + 2, t6);
        gm_th2lqbo77pvsc t7;
        memset(&t7, 0, sizeof t7);
        GM_ST(gm_th2lqbo77pvsc, (uint8_t *)&t3 + 8, t7);
        gm_b16_8 t8 = GM_LD(gm_b16_8, gm_clslitrb510);
        GM_ST(gm_b16_8, (uint8_t *)&t3 + 32, t8);
        GM_ST(uint8_t, (uint8_t *)&t3 + 48, ((uint8_t)0));
        GM_ST(uint8_t, (uint8_t *)&t3 + 49, ((uint8_t)0));
        gm_th2lqbo77pvsc t9;
        memset(&t9, 0, sizeof t9);
        GM_ST(gm_th2lqbo77pvsc, (uint8_t *)&t3 + 56, t9);
        gm_node *pn10 = gm_find(cx, 2119230);
        if (!pn10) gm_trap("no handler installed for effect `Proc`", gm_srcqr4ch56kv6bv, 558, 10);
        gm_t62ebbi2i5rkd t11;
        t11 = ((gm_t62ebbi2i5rkd (*)(void *, gm_node *, gm_node *, gm_b16_8, const void *))pn10->clauses[0])(pn10->env, pn10->hctx, pn10, t2, &t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t62ebbi2i5rkd t12 = t11;
        gm_tm98ap4112hee t13;
        if (!((((uint8_t *)&t12))[0] != 0)) {
            gm_tm98ap4112hee t14 = GM_LD(gm_tm98ap4112hee, (((uint8_t *)&t12) + 8));
            t13 = t14;
        } else {
            gm_tl572d7579hns t15;
            GM_ST(uint8_t, (uint8_t *)&t15 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t15 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t15 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t15 + 8, 0);
            GM_ST(uint64_t, (uint8_t *)&t15 + 16, 0);
            GM_ST(uint8_t, ((uint8_t *)&t15 + 0), ((uint8_t)1ULL));
            uint8_t t16 = 0;
            F.ret = t15;
            goto gm_fret;
        }
        gm_l1 = t13;
    }
    {
        gm_f_list_uvfu1k1((void *)&F.l2, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
    }
    {
        for (;;) {
            if (!((uint8_t)1)) goto lb17;
            {
                gm_tm98ap4112hee t20 = gm_l1;
                void * t21 = ((uint8_t *)&F.l3);
                if ((uintptr_t)0 > (uintptr_t)4096 || (uintptr_t)4096 > (uintptr_t)4096) gm_trap_slice((uintptr_t)0, (uintptr_t)4096, 4096, gm_srcqr4ch56kv6bv, 562, 26);
                gm_b16_8 t22;
                GM_ST(void *, (uint8_t *)&t22, (void *)(((uint8_t *)t21 + (uintptr_t)0 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t22 + 8, (uintptr_t)((uintptr_t)4096 - (uintptr_t)0));
                gm_node *pn23 = gm_find(cx, 2119230);
                if (!pn23) gm_trap("no handler installed for effect `Proc`", gm_srcqr4ch56kv6bv, 562, 19);
                gm_tlbmih0s72in3 t24;
                ((void (*)(void *, void *, gm_node *, gm_node *, gm_tm98ap4112hee, gm_b16_8))pn23->clauses[3])((void *)&t24, pn23->env, pn23->hctx, pn23, t20, t22);
                if (gm_unw.kind) {
                    goto gm_ll18;
                }
                gm_tlbmih0s72in3 t25 = t24;
                gm_thdrvp9q0u7b5 t26;
                if (!((((uint8_t *)&t25))[0] != 0)) {
                    gm_thdrvp9q0u7b5 t27 = GM_LD(gm_thdrvp9q0u7b5, (((uint8_t *)&t25) + 8));
                    t26 = t27;
                } else {
                    goto lb17;
                }
                gm_thdrvp9q0u7b5 t28 = t26;
                gm_te0mgnjdphs16 t29;
                if (((((uint8_t *)&t28))[16] != 0)) {
                    gm_te0mgnjdphs16 t30 = GM_LD(gm_te0mgnjdphs16, (((uint8_t *)&t28) + 0));
                    t29 = t30;
                } else {
                    goto lb17;
                }
                gm_l4 = t29;
            }
            {
                void * t31 = (void *)((uint8_t *)&F.l2);
                void * t32 = ((uint8_t *)&F.l3);
                uint64_t t33 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t34 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                uint64_t t35 = (uintptr_t)t34;
                if (t33 > t35 || t35 > (uintptr_t)4096) gm_trap_slice(t33, t35, 4096, gm_srcqr4ch56kv6bv, 563, 20);
                gm_b16_8 t36;
                GM_ST(void *, (uint8_t *)&t36, (void *)(((uint8_t *)t32 + t33 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t36 + 8, (uintptr_t)(t35 - t33));
                gm_b16_8 t37 = t36;
                gm_b16_8 t38 = GM_LD(gm_b16_8, ((uint8_t *)&t37));
                gm_tftb1odc739t5 t39 = gm_f_extend_0cs85gs(t31, t38);
                gm_tftb1odc739t5 t40 = t39;
                if (((((uint8_t *)&t40))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t40) + 1, gm_srcqr4ch56kv6bv, 563, 9);
                gm_b0_1 t41 = GM_LD(gm_b0_1, (((uint8_t *)&t40) + 1));
            }
lc17:;
        }
gm_ll18:;
        if (gm_unw.frame == &F.h && gm_unw.label == 0) {
            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb17; }
            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc17; }
        }
        goto gm_lad;
lb17:;
    }
    {
        gm_tm98ap4112hee t42 = gm_l1;
        gm_node *pn43 = gm_find(cx, 2119230);
        if (!pn43) gm_trap("no handler installed for effect `Proc`", gm_srcqr4ch56kv6bv, 565, 15);
        gm_tlmjd3nndlf3r t44;
        t44 = ((gm_tlmjd3nndlf3r (*)(void *, gm_node *, gm_node *, gm_tm98ap4112hee))pn43->clauses[4])(pn43->env, pn43->hctx, pn43, t42);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tlmjd3nndlf3r t45 = t44;
        gm_tsoiri44d0i6q t46;
        if (!((((uint8_t *)&t45))[0] != 0)) {
            gm_tsoiri44d0i6q t47 = GM_LD(gm_tsoiri44d0i6q, (((uint8_t *)&t45) + 4));
            t46 = t47;
        } else {
            gm_tsoiri44d0i6q t48;
            GM_ST(uint8_t, (uint8_t *)&t48 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t48 + 2, 0);
            GM_ST(uint8_t, ((uint8_t *)&t48 + 0), ((uint8_t)0ULL));
            GM_ST(uint32_t, ((uint8_t *)&t48 + 4), ((uint32_t)1ULL));
            t46 = t48;
        }
        gm_l5 = t46;
    }
    {
        gm_tsoiri44d0i6q t50 = gm_l5;
        uint8_t t51 = gm_f_success_q3lb6vt(t50);
        if (!t51) goto ie49;
        {
            gm_tl572d7579hns t53;
            GM_ST(uint8_t, (uint8_t *)&t53 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t53 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t53 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t53 + 8, 0);
            GM_ST(uint64_t, (uint8_t *)&t53 + 16, 0);
            GM_ST(uint8_t, ((uint8_t *)&t53 + 0), ((uint8_t)0ULL));
            uint8_t t54 = 0;
            F.ret = t53;
            goto gm_fret;
        }
        goto ix49;
ie49:;
ix49:;
    }
    {
        gm_b16_8 t55 = GM_LD(gm_b16_8, (((uint8_t *)&F.l2) + 0));
        gm_b16_8 t56 = t55;
        gm_b16_8 t57 = GM_LD(gm_b16_8, ((uint8_t *)&t56));
        gm_tl572d7579hns t58;
        GM_ST(uint8_t, (uint8_t *)&t58 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t58 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t58 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t58 + 0), ((uint8_t)2ULL));
        GM_ST(gm_b16_8, ((uint8_t *)&t58 + 8), t57);
        uint8_t t59 = 0;
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
inline __attribute__((aligned(4))) void gm_f_compile_worker_5cv5pj9(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_compile_worker_5cv5pj9 F;
    void * gm_l0; /* p */
    uint64_t gm_l1; /* j */
    GM_STACK_CHECK(&F, 241u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_t3mf58gv9kf3n t2 = GM_CONV(gm_t3mf58gv9kf3n, gm_f_heap_allocator_gihki37());
        gm_t3mf58gv9kf3n t3 = t2;
        gm_node wn4 = {.eff = 587034433, .kind = GM_DECL, .clauses = gm_hd3t1u951rul, .env = (void *)&t3, .hctx = cx, .with_frame = &F.h, .with_label = 0, .result = (void *)&F.w0, .fw = NULL, .next = cx};
        gm_b0_1 t5 = gm_f_system_io_mcjcl9m();
        gm_b0_1 t6 = t5;
        gm_node wn7 = {.eff = 1018230276, .kind = GM_DECL, .clauses = gm_hd3g3ujqvah4, .env = (void *)&t6, .hctx = (&wn4), .with_frame = &F.h, .with_label = 0, .result = (void *)&F.w0, .fw = NULL, .next = (&wn4)};
        gm_node wn8 = {.eff = 1579066750, .kind = GM_DECL, .clauses = gm_hdf4dspmusp5, .env = (void *)&t6, .hctx = (&wn4), .with_frame = &F.h, .with_label = 0, .result = (void *)&F.w0, .fw = NULL, .next = (&wn7)};
        gm_node wn9 = {.eff = 1087599154, .kind = GM_DECL, .clauses = gm_hdvslis4o40u, .env = (void *)&t6, .hctx = (&wn4), .with_frame = &F.h, .with_label = 0, .result = (void *)&F.w0, .fw = NULL, .next = (&wn8)};
        gm_node wn10 = {.eff = 2126215853, .kind = GM_DECL, .clauses = gm_hdu8v6n5i2qd, .env = (void *)&t6, .hctx = (&wn4), .with_frame = &F.h, .with_label = 0, .result = (void *)&F.w0, .fw = NULL, .next = (&wn9)};
        gm_node wn11 = {.eff = 163658498, .kind = GM_DECL, .clauses = gm_hdffkeesmui4, .env = (void *)&t6, .hctx = (&wn4), .with_frame = &F.h, .with_label = 0, .result = (void *)&F.w0, .fw = NULL, .next = (&wn10)};
        gm_node wn12 = {.eff = 2119230, .kind = GM_DECL, .clauses = gm_hdaqemfrshlp, .env = (void *)&t6, .hctx = (&wn4), .with_frame = &F.h, .with_label = 0, .result = (void *)&F.w0, .fw = NULL, .next = (&wn11)};
        {
            for (;;) {
                if (!((uint8_t)1)) goto lb16;
                {
                    void * t19 = gm_l0;
                    void * t20 = (void *)(((uint8_t *)t19) + 64);
                    gm_f_lock_u5lks01(t20);
                }
                {
                    void * t21 = gm_l0;
                    uint64_t t22 = GM_LD(uint64_t, (((uint8_t *)t21) + 16));
                    gm_l1 = t22;
                }
                {
                    void * t23 = gm_l0;
                    void * t24 = gm_l0;
                    uint64_t t25 = GM_LD(uint64_t, (((uint8_t *)t24) + 16));
                    uint64_t r26; bool o27 = __builtin_add_overflow(t25, ((uint64_t)1ULL), &r26);
                    if (o27) gm_trap("integer overflow in `+`", gm_srcqr4ch56kv6bv, 548, 13);
                    uint64_t t28 = r26;
                    GM_ST(uint64_t, (((uint8_t *)t23) + 16), t28);
                }
                {
                    void * t29 = gm_l0;
                    void * t30 = (void *)(((uint8_t *)t29) + 64);
                    gm_f_unlock_9f1vcl6(t30);
                }
                {
                    uint64_t t32 = gm_l1;
                    void * t33 = gm_l0;
                    uint64_t t34 = GM_LD(uintptr_t, (((uint8_t *)t33) + 0) + 8);
                    uint8_t t35 = (t32 >= t34);
                    if (!t35) goto ie31;
                    {
                        goto lb16;
                    }
                    goto ix31;
ie31:;
ix31:;
                }
                {
                    uint64_t t37 = gm_l1;
                    uint64_t t38 = (uintptr_t)t37;
                    void * t39 = gm_l0;
                    uint64_t t40 = GM_LD(uintptr_t, ((((uint8_t *)t39) + 24) + 0) + 8);
                    if (t38 >= (uintptr_t)(t40)) gm_trap_index(t38, t40, gm_srcqr4ch56kv6bv, 551, 13);
                    uint8_t *t41 = ((uint8_t *)GM_LD(void *, ((((uint8_t *)t39) + 24) + 0)) + t38 * 24);
                    uint64_t t42 = gm_l1;
                    uint64_t t43 = (uintptr_t)t42;
                    void * t44 = gm_l0;
                    uint64_t t45 = GM_LD(uintptr_t, (((uint8_t *)t44) + 0) + 8);
                    if (t43 >= (uintptr_t)(t45)) gm_trap_index(t43, t45, gm_srcqr4ch56kv6bv, 551, 46);
                    gm_b16_8 t46 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, (((uint8_t *)t44) + 0)) + t43 * 16));
                    gm_f_compile_one_4djvgo4((void *)&(*(gm_tl572d7579hns *)t41), (void *)NULL, (&wn12), NULL, t46);
                    if (gm_unw.kind) {
                        goto gm_ll17;
                    }
                }
lc16:;
            }
gm_ll17:;
            if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb16; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc16; }
            }
            goto gm_ll14;
lb16:;
        }
        F.w0 = GM_ZERO(gm_b0_1);
        goto gm_ls47;
gm_ll14:;
        if (gm_unw.kind == GM_BREAK && gm_unw.frame == &F.h && gm_unw.label == 0) { gm_unw.kind = GM_NONE; goto we13; }
        goto gm_lad;
gm_ls47:;
we13:;
        gm_b0_1 t48 = F.w0;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_debug_flags_gv15c46(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_debug_flags_gv15c46 F;
    void * gm_l0; /* opts */
    gm_th2lqbo77pvsc gm_l1; /* version */
    GM_STACK_CHECK(&F, 158u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t3 = gm_l0;
        uint8_t t4 = ((GM_LD(uint8_t, (((uint8_t *)t3) + 4) + 0) == ((uint8_t)0ULL)));
        if (!t4) goto ie2;
        void * t5 = gm_l0;
        uint8_t t6 = GM_LD(uint8_t, (((uint8_t *)t5) + 33));
        uint8_t t7 = (uint8_t)!t6;
        if (!t7) goto ie2;
        uint8_t t8 = gm_f_is_gcc_epb17a6(&(*(gm_th2lqbo77pvsc *)((uint8_t *)a1)));
        if (!t8) goto ie2;
        {
            void * t10 = ((uint8_t *)gm_cfa8lmn3344);
            if ((uintptr_t)0 > (uintptr_t)3 || (uintptr_t)3 > (uintptr_t)3) gm_trap_slice((uintptr_t)0, (uintptr_t)3, 3, gm_srcqr4ch56kv6bv, 173, 81);
            gm_b16_8 t11;
            GM_ST(void *, (uint8_t *)&t11, (void *)(((uint8_t *)t10 + (uintptr_t)0 * 16)));
            GM_ST(uintptr_t, (uint8_t *)&t11 + 8, (uintptr_t)((uintptr_t)3 - (uintptr_t)0));
            uint8_t t12 = 0;
            F.ret = t11;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        gm_b16_8 t13 = gm_f_empty_slice_5nn7ph0();
        gm_b16_8 t14 = t13;
        gm_b16_8 t15 = GM_LD(gm_b16_8, ((uint8_t *)&t14));
        uint8_t t16 = 0;
        F.ret = t15;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_b16_8 gm_f_debug_flags_gv15c46_c(void * a0, gm_th2lqbo77pvsc a1) { return gm_f_debug_flags_gv15c46(a0, &a1); }
inline __attribute__((aligned(4))) gm_b16_8 gm_f_default_exe_194tkju(gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_default_exe_194tkju F;
    gm_b16_8 gm_l0; /* root */
    gm_b16_8 gm_l1; /* trimmed */
    GM_STACK_CHECK_SHALLOW(&F, 555u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_b16_8 t3 = gm_l0;
        gm_b16_8 t4;
        GM_ST(void *, (uint8_t *)&t4, (void *)(gm_s0ucactj6hj0a));
        GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(3));
        uint8_t t5 = gm_f_ends_with_biriu5l(t3, t4);
        if (!t5) goto ie2;
        {
            uint64_t t7 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            void * t8 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)));
            uint64_t t9 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t10 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            uint64_t r11; bool o12 = __builtin_sub_overflow(t10, ((uint64_t)3ULL), &r11);
            if (o12) gm_trap("integer overflow in `-`", gm_srcqr4ch56kv6bv, 585, 47);
            uint64_t t13 = r11;
            uint64_t t14 = (uintptr_t)t13;
            if (t9 > t14 || t14 > (uintptr_t)t7) gm_trap_slice(t9, t14, t7, gm_srcqr4ch56kv6bv, 585, 39);
            gm_b16_8 t15;
            GM_ST(void *, (uint8_t *)&t15, (void *)(((uint8_t *)t8 + t9 * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t15 + 8, (uintptr_t)(t14 - t9));
            uint8_t t16 = 0;
            F.ret = t15;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        gm_b16_8 t17;
        gm_b16_8 t19 = gm_l0;
        gm_b16_8 t20;
        GM_ST(void *, (uint8_t *)&t20, (void *)(gm_st3ja0nqi37bv));
        GM_ST(uintptr_t, (uint8_t *)&t20 + 8, (uintptr_t)(1));
        uint8_t t21 = gm_f_ends_with_biriu5l(t19, t20);
        if (!t21) goto ie18;
        uint64_t t23 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        void * t24 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)));
        uint64_t t25 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t26 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        uint64_t r27; bool o28 = __builtin_sub_overflow(t26, ((uint64_t)1ULL), &r27);
        if (o28) gm_trap("integer overflow in `-`", gm_srcqr4ch56kv6bv, 586, 49);
        uint64_t t29 = r27;
        uint64_t t30 = (uintptr_t)t29;
        if (t25 > t30 || t30 > (uintptr_t)t23) gm_trap_slice(t25, t30, t23, gm_srcqr4ch56kv6bv, 586, 41);
        gm_b16_8 t31;
        GM_ST(void *, (uint8_t *)&t31, (void *)(((uint8_t *)t24 + t25 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t31 + 8, (uintptr_t)(t30 - t25));
        gm_b16_8 t32 = t31;
        t17 = t32;
        goto ix18;
ie18:;
        gm_b16_8 t34 = gm_l0;
        gm_b16_8 t35 = t34;
        t17 = t35;
ix18:;
        gm_l1 = t17;
    }
    {
        uint8_t t37 = 0;
        uint8_t t38 = 0;
        gm_b16_8 t39 = gm_l1;
        gm_t6j6smj7qoctu t40;
        gm_b16_8 t41;
        GM_ST(void *, (uint8_t *)&t41, (void *)(gm_sl3q52ep6aa53));
        GM_ST(uintptr_t, (uint8_t *)&t41 + 8, (uintptr_t)(1));
        GM_ST(gm_b16_8, (uint8_t *)&t40 + 0, t41);
        gm_b16_8 t42;
        GM_ST(void *, (uint8_t *)&t42, (void *)(gm_snenkhfev166s));
        GM_ST(uintptr_t, (uint8_t *)&t42 + 8, (uintptr_t)(2));
        GM_ST(gm_b16_8, (uint8_t *)&t40 + 16, t42);
        gm_t6j6smj7qoctu t43 = t40;
        uint8_t t44 = 0;
        gm_b16_8 t45 = t39;
        if (gm_eq0iqiubfm7e(((uint8_t *)&t43) + 0, ((uint8_t *)&t45))) t44 = 1;
        if (gm_eq0iqiubfm7e(((uint8_t *)&t43) + 16, ((uint8_t *)&t45))) t44 = 1;
        uint8_t t46 = (uint8_t)(t44);
        if (!t46) {
            gm_b16_8 t47 = gm_l1;
            gm_b16_8 t48;
            GM_ST(void *, (uint8_t *)&t48, (void *)(gm_st9f93lco88fg));
            GM_ST(uintptr_t, (uint8_t *)&t48 + 8, (uintptr_t)(2));
            uint8_t t49 = gm_f_ends_with_biriu5l(t47, t48);
            t38 = t49;
        } else t38 = 1;
        if (!t38) {
            gm_b16_8 t50 = gm_l1;
            gm_b16_8 t51;
            GM_ST(void *, (uint8_t *)&t51, (void *)(gm_s29cvp3di8ou1));
            GM_ST(uintptr_t, (uint8_t *)&t51 + 8, (uintptr_t)(3));
            uint8_t t52 = gm_f_ends_with_biriu5l(t50, t51);
            t37 = t52;
        } else t37 = 1;
        if (!t37) goto ie36;
        {
            gm_b16_8 t54;
            GM_ST(void *, (uint8_t *)&t54, (void *)(gm_sog4rksrmsqd2));
            GM_ST(uintptr_t, (uint8_t *)&t54 + 8, (uintptr_t)(4));
            uint8_t t55 = 0;
            F.ret = t54;
            goto gm_fret;
        }
        goto ix36;
ie36:;
ix36:;
    }
    {
        gm_b16_8 t56 = gm_l1;
        uint8_t t57 = 0;
        F.ret = t56;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_emitted_units_7f3nie0(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1) {
    struct gm_fr_emitted_units_7f3nie0 F;
    void * gm_l0; /* b */
    uint8_t gm_l1; /* debug_info */
    gm_b16_8 gm_l2; /* v */
    uint64_t gm_l3; /* n */
    GM_STACK_CHECK(&F, 463u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint8_t t3 = gm_l1;
        if (!t3) goto ie2;
        {
            gm_ttu09lij36f8c t5;
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
        gm_b16_8 t7;
        GM_ST(void *, (uint8_t *)&t7, (void *)(gm_sdahvc1997d01));
        GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)(13));
        gm_node *pn8 = gm_find(cx, 621690230);
        if (!pn8) gm_trap("no handler installed for effect `Env`", gm_srcqr4ch56kv6bv, 210, 10);
        gm_th2lqbo77pvsc t9;
        if (pn8->kind == GM_NATIVE) {
            const uint8_t *ev; size_t evn;
            bool evok = gm_env_var(GM_LD(void *, &t7), GM_LD(uintptr_t, (uint8_t *)&t7 + 8), &ev, &evn);
            gm_b16_8 t10;
            GM_ST(void *, (uint8_t *)&t10, (void *)(ev));
            GM_ST(uintptr_t, (uint8_t *)&t10 + 8, (uintptr_t)(evn));
            gm_th2lqbo77pvsc t11;
            GM_ST(uint8_t, (uint8_t *)&t11 + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&t11 + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&t11 + 20, 0);
            GM_ST(gm_b16_8, ((uint8_t *)&t11), t10);
            ((uint8_t *)&t11)[16] = 1;
            gm_th2lqbo77pvsc t12;
            memset(&t12, 0, sizeof t12);
            t9 = evok ? t11 : t12;
        } else {
            ((void (*)(void *, void *, gm_node *, gm_node *, gm_b16_8))pn8->clauses[1])((void *)&t9, pn8->env, pn8->hctx, pn8, t7);
        }
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_th2lqbo77pvsc t13 = t9;
        gm_b16_8 t14;
        if (((((uint8_t *)&t13))[16] != 0)) {
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&t13) + 0));
            t14 = t15;
        } else {
            gm_ttu09lij36f8c t16;
            memset(&t16, 0, sizeof t16);
            uint8_t t17 = 0;
            F.ret = t16;
            goto gm_fret;
        }
        gm_l2 = t14;
    }
    {
        gm_b16_8 t18 = gm_l2;
        gm_tutnubt6u4hoq t19;
        gm_f_parse_int_3ce02np((void *)&t19, t18);
        gm_tutnubt6u4hoq t20 = t19;
        uint64_t t21;
        if (!((((uint8_t *)&t20))[0] != 0)) {
            uint64_t t22 = GM_LD(uint64_t, (((uint8_t *)&t20) + 8));
            t21 = t22;
        } else {
            gm_ttu09lij36f8c t23;
            memset(&t23, 0, sizeof t23);
            uint8_t t24 = 0;
            F.ret = t23;
            goto gm_fret;
        }
        gm_l3 = t21;
    }
    {
        uint64_t t26 = gm_l3;
        uint8_t t27 = (t26 < ((uint64_t)2ULL));
        if (!t27) goto ie25;
        {
            gm_ttu09lij36f8c t29;
            memset(&t29, 0, sizeof t29);
            uint8_t t30 = 0;
            F.ret = t29;
            goto gm_fret;
        }
        goto ix25;
ie25:;
ix25:;
    }
    {
        void * t31 = gm_l0;
        void * t32 = (void *)((((uint8_t *)t31) + 0) + 16);
        gm_f_split_by_file_hgjqgsp((void *)&(*(gm_ttu09lij36f8c *)gm_out), (void *)NULL, cx, NULL, t32);
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
inline __attribute__((aligned(4))) uint8_t gm_f_exit_code_dv2nn7j(gm_tsoiri44d0i6q a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_exit_code_dv2nn7j F;
    gm_tsoiri44d0i6q gm_l0; /* status */
    uint32_t gm_l1; /* c */
    int32_t gm_l2; /* sig */
    GM_STACK_CHECK_SHALLOW(&F, 54u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t t1;
    uint8_t t7 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    switch (t7) { case ((uint8_t)0ULL): goto ma3; case ((uint8_t)1ULL): goto ma4; default: goto ma6; }
ma3:;
    uint8_t t9 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t9 != ((uint8_t)0ULL)) goto mn8;
    uint32_t t10 = GM_LD(uint32_t, (((uint8_t *)&gm_l0) + 4));
    gm_l1 = t10;
    uint32_t t11 = gm_l1;
    uint8_t t12 = (uint8_t)t11;
    t1 = t12;
    goto me2;
mn8:;
ma4:;
    uint8_t t16 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t16 != ((uint8_t)1ULL)) goto pa15;
    int32_t t17 = GM_LD(int32_t, (((uint8_t *)&gm_l0) + 4));
    if (!(t17 == ((int32_t)4LL))) goto pa15;
    goto po14;
pa15:;
    uint8_t t19 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t19 != ((uint8_t)1ULL)) goto pa18;
    int32_t t20 = GM_LD(int32_t, (((uint8_t *)&gm_l0) + 4));
    if (!(t20 == ((int32_t)5LL))) goto pa18;
    goto po14;
pa18:;
    goto mn13;
po14:;
    t1 = ((uint8_t)134ULL);
    goto me2;
mn13:;
ma5:;
    uint8_t t22 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t22 != ((uint8_t)1ULL)) goto mn21;
    int32_t t23 = GM_LD(int32_t, (((uint8_t *)&gm_l0) + 4));
    gm_l2 = t23;
    int32_t t24 = gm_l2;
    int32_t r25; bool o26 = __builtin_add_overflow(((int32_t)128LL), t24, &r25);
    if (o26) gm_trap("integer overflow in `+`", gm_srcqr4ch56kv6bv, 722, 24);
    int32_t t27 = r25;
    uint8_t t28 = (uint8_t)t27;
    t1 = t28;
    goto me2;
mn21:;
ma6:;
    gm_trap("no match arm matched the scrutinee", gm_srcqr4ch56kv6bv, 719, 54);
me2:;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) void gm_f_f16c_flag_vpo7tss(void *gm_out, const void * a0);
__attribute__((aligned(4))) gm_th2lqbo77pvsc gm_f_f16c_flag_vpo7tss_c(gm_t0rfe1ou14h21 a0) { gm_th2lqbo77pvsc gm_r; gm_f_f16c_flag_vpo7tss(&gm_r, &a0); return gm_r; }
__attribute__((aligned(4))) void gm_f_generate_program_9mr9pv8(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2, uint8_t a3, const void * a4) {
    struct gm_fr_generate_program_9mr9pv8 F;
    void * gm_l0; /* s */
    gm_b16_8 gm_l1; /* roots_in */
    gm_b16_8 gm_l2; /* entries */
    uint8_t gm_l3; /* select */
    gm_b16_8 gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    uint32_t gm_l8; /* f */
    gm_tftmsj5vtqvej gm_l10; /* copts */
    gm_b16_8 gm_l12; /* rt */
    gm_b16_8 gm_l13; /* rt_h */
    gm_b16_8 gm_l14; /* rt_c */
    gm_t08olq97rscn3 gm_l15; /* u */
    gm_t08olq97rscn3 gm_l17; /* u */
    GM_STACK_CHECK(&F, 6412u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    memcpy(&F.l4, a4, sizeof F.l4);
    {
        gm_f_list_go2qg0b((void *)&F.l5, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l5);
        gm_b16_8 t3 = gm_l1;
        gm_tftb1odc739t5 t4 = gm_f_extend_7hs67uj(t2, t3);
        gm_tftb1odc739t5 t5 = t4;
        if (((((uint8_t *)&t5))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t5) + 1, gm_srcqr4ch56kv6bv, 113, 5);
        gm_b0_1 t6 = GM_LD(gm_b0_1, (((uint8_t *)&t5) + 1));
    }
    {
        {
            void * t8 = gm_l0;
            gm_b16_8 t9 = GM_LD(gm_b16_8, ((((uint8_t *)t8) + 920) + 0));
            gm_l6 = t9;
        }
        {
            gm_l7 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t11 = gm_l7;
                uint64_t t12 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                uint8_t t13 = (t11 < t12);
                if (!t13) goto lb10;
                {
                    uint64_t t16 = gm_l7;
                    uint64_t t17 = (uintptr_t)t16;
                    uint64_t t18 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                    uint32_t t19 = GM_LD(uint32_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t17 * 4));
                    uint32_t t20 = t19;
                    uint32_t t23 = GM_LD(uint32_t, ((uint8_t *)&t20));
                    gm_l8 = t23;
                    {
                        uint32_t t26 = gm_l8;
                        gm_b16_8 t27 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 0));
                        gm_b16_8 t28 = t27;
                        uint8_t t29 = 0;
                        uint32_t t30 = t26;
                        for (uintptr_t i = 0; i < (uintptr_t)(GM_LD(uintptr_t, ((uint8_t *)&t28) + 8)); i++) if (gm_eq467d5cm1d5(((uint8_t *)GM_LD(void *, ((uint8_t *)&t28))) + i * 4, ((uint8_t *)&t30))) { t29 = 1; break; }
                        uint8_t t31 = (uint8_t)(!t29);
                        if (!t31) goto ie25;
                        {
                            void * t33 = (void *)((uint8_t *)&F.l5);
                            uint32_t t34 = gm_l8;
                            gm_tftb1odc739t5 t35 = gm_f_push_im9d4ro(t33, t34);
                            gm_tftb1odc739t5 t36 = t35;
                            if (((((uint8_t *)&t36))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t36) + 1, gm_srcqr4ch56kv6bv, 114, 61);
                            gm_b0_1 t37 = GM_LD(gm_b0_1, (((uint8_t *)&t36) + 1));
                        }
                        goto ix25;
ie25:;
ix25:;
                    }
                    goto me21;
mn22:;
                    gm_trap("no match arm matched the scrutinee", gm_srcqr4ch56kv6bv, 114, 5);
me21:;
                }
lc10:;
                {
                    uint64_t t39 = gm_l7;
                    uint64_t r40; bool o41 = __builtin_add_overflow(t39, ((uint64_t)1ULL), &r40);
                    uint64_t t42 = r40;
                    gm_l7 = t42;
                }
            }
lb10:;
        }
    }
    {
        void * t44 = gm_l0;
        gm_b16_8 t45 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 0));
        gm_b16_8 t46 = t45;
        gm_b16_8 t47 = GM_LD(gm_b16_8, ((uint8_t *)&t46));
        uint8_t t48 = gm_f_prepare_codegen_4hnlio1((void *)NULL, cx, NULL, t44, t47);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t49 = (uint8_t)!t48;
        if (!t49) goto ie43;
        {
            gm_t5i6e9e007icp t51;
            memset(&t51, 0, sizeof t51);
            uint8_t t52 = 0;
            F.ret = t51;
            goto gm_fret;
        }
        goto ix43;
ie43:;
ix43:;
    }
    {
        void * t53 = gm_l0;
        gm_f_prog_61arbs1((void *)&F.l9, (void *)NULL, cx, NULL, t53);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_tftmsj5vtqvej t54;
        uint8_t t60 = GM_LD(uint8_t, ((((uint8_t *)&F.l4) + 4) + 0));
        switch (t60) { case ((uint8_t)0ULL): goto ma56; case ((uint8_t)1ULL): goto ma57; case ((uint8_t)2ULL): goto ma57; case ((uint8_t)3ULL): goto ma58; default: goto ma59; }
ma56:;
        uint8_t t62 = GM_LD(uint8_t, ((((uint8_t *)&F.l4) + 4) + 0));
        if (t62 != ((uint8_t)0ULL)) goto mn61;
        gm_tftmsj5vtqvej t63;
        memset((uint8_t *)&t63 + 3, 0, 5);
        GM_ST(uint8_t, (uint8_t *)&t63 + 0, ((uint8_t)1));
        GM_ST(uint8_t, (uint8_t *)&t63 + 1, ((uint8_t)1));
        uint8_t t64 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 33));
        GM_ST(uint8_t, (uint8_t *)&t63 + 2, t64);
        gm_b16_8 t65 = GM_LD(gm_b16_8, gm_cgsmfcq937e);
        GM_ST(gm_b16_8, (uint8_t *)&t63 + 8, t65);
        t54 = t63;
        goto me55;
mn61:;
ma57:;
        uint8_t t69 = GM_LD(uint8_t, ((((uint8_t *)&F.l4) + 4) + 0));
        if (t69 != ((uint8_t)1ULL)) goto pa68;
        goto po67;
pa68:;
        uint8_t t71 = GM_LD(uint8_t, ((((uint8_t *)&F.l4) + 4) + 0));
        if (t71 != ((uint8_t)2ULL)) goto pa70;
        goto po67;
pa70:;
        goto mn66;
po67:;
        gm_tftmsj5vtqvej t72;
        memset((uint8_t *)&t72 + 3, 0, 5);
        GM_ST(uint8_t, (uint8_t *)&t72 + 0, ((uint8_t)1));
        GM_ST(uint8_t, (uint8_t *)&t72 + 1, ((uint8_t)0));
        uint8_t t73 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 33));
        GM_ST(uint8_t, (uint8_t *)&t72 + 2, t73);
        gm_b16_8 t74 = GM_LD(gm_b16_8, gm_cgsmfcq937e);
        GM_ST(gm_b16_8, (uint8_t *)&t72 + 8, t74);
        t54 = t72;
        goto me55;
mn66:;
ma58:;
        uint8_t t76 = GM_LD(uint8_t, ((((uint8_t *)&F.l4) + 4) + 0));
        if (t76 != ((uint8_t)3ULL)) goto mn75;
        gm_tftmsj5vtqvej t77;
        memset((uint8_t *)&t77 + 3, 0, 5);
        GM_ST(uint8_t, (uint8_t *)&t77 + 0, ((uint8_t)0));
        GM_ST(uint8_t, (uint8_t *)&t77 + 1, ((uint8_t)0));
        uint8_t t78 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 33));
        GM_ST(uint8_t, (uint8_t *)&t77 + 2, t78);
        gm_b16_8 t79 = GM_LD(gm_b16_8, gm_cgsmfcq937e);
        GM_ST(gm_b16_8, (uint8_t *)&t77 + 8, t79);
        t54 = t77;
        goto me55;
mn75:;
ma59:;
        gm_trap("no match arm matched the scrutinee", gm_srcqr4ch56kv6bv, 117, 14);
me55:;
        gm_l10 = t54;
    }
    {
        uint8_t t81 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 33));
        if (!t81) goto ie80;
        gm_b16_8 t82 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l9) + 80) + 24));
        gm_t6j6smj7qoctu t83;
        gm_b16_8 t84;
        GM_ST(void *, (uint8_t *)&t84, (void *)(gm_scq91tgm9lph4));
        GM_ST(uintptr_t, (uint8_t *)&t84 + 8, (uintptr_t)(5));
        GM_ST(gm_b16_8, (uint8_t *)&t83 + 0, t84);
        gm_b16_8 t85;
        GM_ST(void *, (uint8_t *)&t85, (void *)(gm_sn3ltks10rrv7));
        GM_ST(uintptr_t, (uint8_t *)&t85 + 8, (uintptr_t)(7));
        GM_ST(gm_b16_8, (uint8_t *)&t83 + 16, t85);
        gm_t6j6smj7qoctu t86 = t83;
        uint8_t t87 = 0;
        gm_b16_8 t88 = t82;
        if (gm_eq0iqiubfm7e(((uint8_t *)&t86) + 0, ((uint8_t *)&t88))) t87 = 1;
        if (gm_eq0iqiubfm7e(((uint8_t *)&t86) + 16, ((uint8_t *)&t88))) t87 = 1;
        uint8_t t89 = (uint8_t)(t87);
        if (!t89) goto ie80;
        uint8_t t90 = GM_LD(uint8_t, ((((uint8_t *)&F.l9) + 80) + 56));
        uint8_t t91 = (uint8_t)!t90;
        if (!t91) goto ie80;
        {
            gm_b16_8 t93;
            GM_ST(void *, (uint8_t *)&t93, (void *)(gm_sg5khilhaakst));
            GM_ST(uintptr_t, (uint8_t *)&t93 + 8, (uintptr_t)(15));
            gm_th2lqbo77pvsc t94;
            gm_f_runtime_text_37culso((void *)&t94, (void *)NULL, cx, NULL, t93);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_th2lqbo77pvsc t95 = t94;
            gm_b16_8 t96;
            if (((((uint8_t *)&t95))[16] != 0)) {
                gm_b16_8 t97 = GM_LD(gm_b16_8, (((uint8_t *)&t95) + 0));
                t96 = t97;
            } else {
                gm_t5i6e9e007icp t98;
                memset(&t98, 0, sizeof t98);
                uint8_t t99 = 0;
                F.ret = t98;
                goto gm_fret;
            }
            GM_ST(gm_b16_8, (((uint8_t *)&gm_l10) + 8), t96);
        }
        goto ix80;
ie80:;
ix80:;
    }
    {
        gm_tetdkgoreqmn1 t100;
        uint8_t t102 = gm_l3;
        if (!t102) goto ie101;
        void * t104 = (void *)((uint8_t *)&F.l9);
        gm_b16_8 t105 = gm_l2;
        gm_tetdkgoreqmn1 t106;
        gm_f_generate_tests_462i0us((void *)&t106, (void *)NULL, cx, NULL, t104, t105, &(*(gm_tftmsj5vtqvej *)((uint8_t *)&gm_l10)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tetdkgoreqmn1 t107 = t106;
        t100 = t107;
        goto ix101;
ie101:;
        gm_tetdkgoreqmn1 t108;
        uint64_t t110 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        uint8_t t111 = ((t110 == ((uint64_t)0ULL)));
        if (!t111) goto ie109;
        void * t113 = (void *)((uint8_t *)&F.l9);
        gm_tetdkgoreqmn1 t114;
        gm_f_generate_object_rgfvem4((void *)&t114, (void *)NULL, cx, NULL, t113, &(*(gm_tftmsj5vtqvej *)((uint8_t *)&gm_l10)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tetdkgoreqmn1 t115 = t114;
        t108 = t115;
        goto ix109;
ie109:;
        void * t117 = (void *)((uint8_t *)&F.l9);
        uint64_t t118 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t119 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        if (t118 >= (uintptr_t)(t119)) gm_trap_index(t118, t119, gm_srcqr4ch56kv6bv, 127, 155);
        uint32_t t120 = GM_LD(uint32_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t118 * 4));
        gm_tetdkgoreqmn1 t121;
        gm_f_generate_6no5tn1((void *)&t121, (void *)NULL, cx, NULL, t117, t120, &(*(gm_tftmsj5vtqvej *)((uint8_t *)&gm_l10)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tetdkgoreqmn1 t122 = t121;
        t108 = t122;
ix109:;
        t100 = t108;
ix101:;
        F.l11 = t100;
    }
    {
        gm_b16_8 t123 = gm_f_std_root_e0juohb((void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b16_8 t124 = gm_f_runtime_dir_qd07iff((void *)NULL, cx, NULL, t123);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l12 = t124;
    }
    {
        gm_b16_8 t125 = gm_l12;
        gm_b16_8 t126;
        GM_ST(void *, (uint8_t *)&t126, (void *)(gm_s55g9qp4arkn7));
        GM_ST(uintptr_t, (uint8_t *)&t126 + 8, (uintptr_t)(7));
        gm_b16_8 t127 = gm_f_join_qveh5b2((void *)NULL, cx, NULL, t125, t126);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tgvh15qshf643 t128;
        gm_f_read_all_r9rh9qd((void *)&t128, (void *)NULL, cx, NULL, t127);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tgvh15qshf643 t129 = t128;
        gm_b16_8 t130;
        if (!((((uint8_t *)&t129))[0] != 0)) {
            gm_b16_8 t131 = GM_LD(gm_b16_8, (((uint8_t *)&t129) + 8));
            t130 = t131;
        } else {
            {
                gm_b16_8 t133 = gm_l12;
                gm_b16_8 t134 = t133;
                if (gm_pf454qfrpt6r(cx, gm_sg87p4r5apham, 35, gm_srcqr4ch56kv6bv, 130, 9)) {
                    goto gm_lad;
                }
                if (gm_pfu8dq5bikbh(cx, &gm_ty0iqiubfm7e, ((uint8_t *)&t134), gm_srcqr4ch56kv6bv, 130, 9)) {
                    goto gm_lad;
                }
                if (gm_pf454qfrpt6r(cx, gm_s06m3met1mb9v, 2, gm_srcqr4ch56kv6bv, 130, 9)) {
                    goto gm_lad;
                }
            }
            gm_t5i6e9e007icp t135;
            memset(&t135, 0, sizeof t135);
            uint8_t t136 = 0;
            F.ret = t135;
            goto gm_fret;
        }
        gm_l13 = t130;
    }
    {
        gm_b16_8 t137 = gm_l12;
        gm_b16_8 t138;
        GM_ST(void *, (uint8_t *)&t138, (void *)(gm_sr98hl2rjopi2));
        GM_ST(uintptr_t, (uint8_t *)&t138 + 8, (uintptr_t)(7));
        gm_b16_8 t139 = gm_f_join_qveh5b2((void *)NULL, cx, NULL, t137, t138);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tgvh15qshf643 t140;
        gm_f_read_all_r9rh9qd((void *)&t140, (void *)NULL, cx, NULL, t139);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tgvh15qshf643 t141 = t140;
        gm_b16_8 t142;
        if (!((((uint8_t *)&t141))[0] != 0)) {
            gm_b16_8 t143 = GM_LD(gm_b16_8, (((uint8_t *)&t141) + 8));
            t142 = t143;
        } else {
            {
                gm_b16_8 t145 = gm_l12;
                gm_b16_8 t146 = t145;
                if (gm_pf454qfrpt6r(cx, gm_sg87p4r5apham, 35, gm_srcqr4ch56kv6bv, 134, 9)) {
                    goto gm_lad;
                }
                if (gm_pfu8dq5bikbh(cx, &gm_ty0iqiubfm7e, ((uint8_t *)&t146), gm_srcqr4ch56kv6bv, 134, 9)) {
                    goto gm_lad;
                }
                if (gm_pf454qfrpt6r(cx, gm_s06m3met1mb9v, 2, gm_srcqr4ch56kv6bv, 134, 9)) {
                    goto gm_lad;
                }
            }
            gm_t5i6e9e007icp t147;
            memset(&t147, 0, sizeof t147);
            uint8_t t148 = 0;
            F.ret = t147;
            goto gm_fret;
        }
        gm_l14 = t142;
    }
    {
        gm_tetdkgoreqmn1 t149 = F.l11;
        gm_tetdkgoreqmn1 t150 = t149;
        gm_t0u6dopi4f21f t151;
        if (!((((uint8_t *)&t150))[0] != 0)) {
            gm_t0u6dopi4f21f t152 = GM_LD(gm_t0u6dopi4f21f, (((uint8_t *)&t150) + 8));
            t151 = t152;
        } else {
            gm_t08olq97rscn3 t153 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&t150) + 8));
            gm_l15 = t153;
            {
                void * t155 = gm_l0;
                gm_b0_1 t156 = gm_f_report_unsupported_9rg6nq1((void *)NULL, cx, NULL, t155, &(*(gm_t08olq97rscn3 *)((uint8_t *)&gm_l15)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_t5i6e9e007icp t157;
            memset(&t157, 0, sizeof t157);
            uint8_t t158 = 0;
            F.ret = t157;
            goto gm_fret;
        }
        F.l16 = t151;
    }
    {
        void * t159 = (void *)((uint8_t *)&F.l9);
        gm_t6j6smj7qoctu t160;
        gm_b16_8 t161 = gm_l13;
        GM_ST(gm_b16_8, (uint8_t *)&t160 + 0, t161);
        gm_b16_8 t162 = gm_l14;
        GM_ST(gm_b16_8, (uint8_t *)&t160 + 16, t162);
        gm_tabb710d9mq2e t163;
        gm_f_format_09mcfes((void *)&t163, (void *)NULL, cx, NULL, &t160);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t164 = t163;
        if (((((uint8_t *)&t164))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t164) + 8, gm_srcqr4ch56kv6bv, 141, 33);
        gm_b16_8 t165 = GM_LD(gm_b16_8, (((uint8_t *)&t164) + 8));
        gm_b16_8 t166 = t165;
        gm_b16_8 t167 = GM_LD(gm_b16_8, ((uint8_t *)&t166));
        gm_b16_8 t168 = GM_LD(gm_b16_8, (((uint8_t *)&F.l16) + 0));
        gm_tqgtd5gtgj65s t169;
        gm_f_check_export_names_mirc4me((void *)&t169, (void *)NULL, cx, NULL, t159, t167, t168);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tqgtd5gtgj65s t170 = t169;
        gm_b0_1 t171;
        if (!((((uint8_t *)&t170))[0] != 0)) {
            gm_b0_1 t172 = GM_LD(gm_b0_1, (((uint8_t *)&t170) + 8));
            t171 = t172;
        } else {
            gm_t08olq97rscn3 t173 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&t170) + 8));
            gm_l17 = t173;
            {
                void * t175 = gm_l0;
                gm_b0_1 t176 = gm_f_report_unsupported_9rg6nq1((void *)NULL, cx, NULL, t175, &(*(gm_t08olq97rscn3 *)((uint8_t *)&gm_l17)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_t5i6e9e007icp t177;
            memset(&t177, 0, sizeof t177);
            uint8_t t178 = 0;
            F.ret = t177;
            goto gm_fret;
        }
    }
    {
        gm_tt1457on428g4 t179;
        memcpy((uint8_t *)&t179 + 0, ((uint8_t *)&F.l16), 216);
        gm_b16_8 t180 = gm_l13;
        gm_b16_8 t181 = t180;
        gm_b16_8 t182 = GM_LD(gm_b16_8, ((uint8_t *)&t181));
        GM_ST(gm_b16_8, (uint8_t *)&t179 + 216, t182);
        gm_b16_8 t183 = gm_l14;
        gm_b16_8 t184 = t183;
        gm_b16_8 t185 = GM_LD(gm_b16_8, ((uint8_t *)&t184));
        GM_ST(gm_b16_8, (uint8_t *)&t179 + 232, t185);
        gm_t5i6e9e007icp t186;
        GM_ST(uint8_t, (uint8_t *)&t186 + 249, 0);
        GM_ST(uint16_t, (uint8_t *)&t186 + 250, 0);
        GM_ST(uint32_t, (uint8_t *)&t186 + 252, 0);
        GM_ST(gm_tt1457on428g4, ((uint8_t *)&t186), t179);
        ((uint8_t *)&t186)[248] = 1;
        uint8_t t187 = 0;
        F.ret = t186;
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
inline __attribute__((aligned(4))) uint8_t gm_f_has_extension_sdfv93u(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_has_extension_sdfv93u F;
    gm_b16_8 gm_l0; /* p */
    gm_b16_8 gm_l1; /* name */
    uint64_t gm_l2; /* dot */
    GM_STACK_CHECK(&F, 230u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_b16_8 t2 = gm_l0;
        gm_th2lqbo77pvsc t3;
        gm_f_file_name_em7v8lv((void *)&t3, (void *)NULL, cx, NULL, t2);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_th2lqbo77pvsc t4 = t3;
        gm_b16_8 t5;
        if (((((uint8_t *)&t4))[16] != 0)) {
            gm_b16_8 t6 = GM_LD(gm_b16_8, (((uint8_t *)&t4) + 0));
            t5 = t6;
        } else {
            uint8_t t7 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        gm_l1 = t5;
    }
    {
        gm_b16_8 t8 = gm_l1;
        gm_b16_8 t9;
        GM_ST(void *, (uint8_t *)&t9, (void *)(gm_sl3q52ep6aa53));
        GM_ST(uintptr_t, (uint8_t *)&t9 + 8, (uintptr_t)(1));
        gm_tf5srh4mp4b3i t10 = gm_f_rfind_ai5s2js(t8, t9);
        gm_tf5srh4mp4b3i t11 = t10;
        uint64_t t12;
        if (((((uint8_t *)&t11))[8] != 0)) {
            uint64_t t13 = GM_LD(uint64_t, (((uint8_t *)&t11) + 0));
            t12 = t13;
        } else {
            uint8_t t14 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        gm_l2 = t12;
    }
    {
        uint64_t t15 = gm_l2;
        uint8_t t16 = (t15 > ((uint64_t)0ULL));
        uint8_t t17 = 0;
        F.ret = t16;
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
__attribute__((aligned(4))) gm_b16_8 gm_f_library_stamps_mjeqafa(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1) {
    struct gm_fr_library_stamps_mjeqafa F;
    gm_b16_8 gm_l0; /* libs */
    gm_b16_8 gm_l1; /* ldflags */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    uint64_t gm_l5; /* i */
    gm_b16_8 gm_l6; /* f */
    gm_b16_8 gm_l8; /* <tmp> */
    uint64_t gm_l9; /* <tmp> */
    gm_b16_8 gm_l10; /* lib */
    gm_b16_8 gm_l11; /* <tmp> */
    uint64_t gm_l12; /* <tmp> */
    gm_b16_8 gm_l13; /* d */
    gm_b16_8 gm_l16; /* <tmp> */
    uint64_t gm_l17; /* <tmp> */
    gm_b16_8 gm_l18; /* ext */
    gm_b16_8 gm_l19; /* path */
    gm_t5j14m8ru132p gm_l20; /* info */
    GM_STACK_CHECK(&F, 1660u);
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
            gm_b16_8 t3 = gm_l1;
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
                    gm_tf1bjg81u0glv t10;
                    uint64_t t11 = gm_l4;
                    GM_ST(uint64_t, (uint8_t *)&t10 + 0, t11);
                    uint64_t t12 = gm_l4;
                    uint64_t t13 = (uintptr_t)t12;
                    uint64_t t14 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                    gm_b16_8 t15 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t13 * 16));
                    GM_ST(gm_b16_8, (uint8_t *)&t10 + 8, t15);
                    gm_tf1bjg81u0glv t16 = t10;
                    uint64_t t19 = GM_LD(uint64_t, (((uint8_t *)&t16) + 0));
                    gm_l5 = t19;
                    gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&t16) + 8));
                    gm_l6 = t20;
                    {
                        uint8_t t23 = (((GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8) == 2) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l6)), gm_s4lj2bemc87jp, 2) == 0));
                        if (!t23) goto ie22;
                        uint64_t t24 = gm_l5;
                        uint64_t r25; bool o26 = __builtin_add_overflow(t24, ((uint64_t)1ULL), &r25);
                        if (o26) gm_trap("integer overflow in `+`", gm_srcqr4ch56kv6bv, 693, 26);
                        uint64_t t27 = r25;
                        uint64_t t28 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                        uint8_t t29 = (t27 < t28);
                        if (!t29) goto ie22;
                        {
                            void * t31 = (void *)((uint8_t *)&F.l2);
                            uint64_t t32 = gm_l5;
                            uint64_t r33; bool o34 = __builtin_add_overflow(t32, ((uint64_t)1ULL), &r33);
                            if (o34) gm_trap("integer overflow in `+`", gm_srcqr4ch56kv6bv, 694, 31);
                            uint64_t t35 = r33;
                            uint64_t t36 = (uintptr_t)t35;
                            uint64_t t37 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                            if (t36 >= (uintptr_t)(t37)) gm_trap_index(t36, t37, gm_srcqr4ch56kv6bv, 694, 23);
                            gm_b16_8 t38 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t36 * 16));
                            gm_tftb1odc739t5 t39 = gm_f_push_3ef746b(t31, t38);
                            gm_tftb1odc739t5 t40 = t39;
                            if (((((uint8_t *)&t40))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t40) + 1, gm_srcqr4ch56kv6bv, 694, 13);
                            gm_b0_1 t41 = GM_LD(gm_b0_1, (((uint8_t *)&t40) + 1));
                        }
                        goto ix22;
ie22:;
                        gm_b16_8 t43 = gm_l6;
                        gm_b16_8 t44;
                        GM_ST(void *, (uint8_t *)&t44, (void *)(gm_s4lj2bemc87jp));
                        GM_ST(uintptr_t, (uint8_t *)&t44 + 8, (uintptr_t)(2));
                        uint8_t t45 = gm_f_starts_with_6vvt01e(t43, t44);
                        if (!t45) goto ie42;
                        {
                            void * t47 = (void *)((uint8_t *)&F.l2);
                            uint64_t t48 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                            void * t49 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)));
                            uint64_t t50 = (uintptr_t)((uint64_t)2ULL);
                            if (t50 > (uintptr_t)t48 || (uintptr_t)t48 > (uintptr_t)t48) gm_trap_slice(t50, (uintptr_t)t48, t48, gm_srcqr4ch56kv6bv, 696, 23);
                            gm_b16_8 t51;
                            GM_ST(void *, (uint8_t *)&t51, (void *)(((uint8_t *)t49 + t50 * 1)));
                            GM_ST(uintptr_t, (uint8_t *)&t51 + 8, (uintptr_t)((uintptr_t)t48 - t50));
                            gm_tftb1odc739t5 t52 = gm_f_push_3ef746b(t47, t51);
                            gm_tftb1odc739t5 t53 = t52;
                            if (((((uint8_t *)&t53))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t53) + 1, gm_srcqr4ch56kv6bv, 696, 13);
                            gm_b0_1 t54 = GM_LD(gm_b0_1, (((uint8_t *)&t53) + 1));
                        }
                        goto ix42;
ie42:;
ix42:;
ix22:;
                    }
                    goto me17;
mn18:;
                    gm_trap("no match arm matched the scrutinee", gm_srcqr4ch56kv6bv, 692, 5);
me17:;
                }
lc4:;
                {
                    uint64_t t56 = gm_l4;
                    uint64_t r57; bool o58 = __builtin_add_overflow(t56, ((uint64_t)1ULL), &r57);
                    uint64_t t59 = r57;
                    gm_l4 = t59;
                }
            }
lb4:;
        }
    }
    {
        gm_f_list_epvducg((void *)&F.l7, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t61 = gm_l0;
            gm_l8 = t61;
        }
        {
            gm_l9 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t63 = gm_l9;
                uint64_t t64 = GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8);
                uint8_t t65 = (t63 < t64);
                if (!t65) goto lb62;
                {
                    uint64_t t68 = gm_l9;
                    uint64_t t69 = (uintptr_t)t68;
                    uint64_t t70 = GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8);
                    gm_b16_8 t73 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l8)) + t69 * 16));
                    gm_l10 = t73;
                    {
                        {
                            gm_b16_8 t76 = GM_LD(gm_b16_8, (((uint8_t *)&F.l2) + 0));
                            gm_l11 = t76;
                        }
                        {
                            gm_l12 = ((uint64_t)0ULL);
                        }
                        {
                            for (;;) {
                                uint64_t t78 = gm_l12;
                                uint64_t t79 = GM_LD(uintptr_t, ((uint8_t *)&gm_l11) + 8);
                                uint8_t t80 = (t78 < t79);
                                if (!t80) goto lb77;
                                {
                                    uint64_t t83 = gm_l12;
                                    uint64_t t84 = (uintptr_t)t83;
                                    uint64_t t85 = GM_LD(uintptr_t, ((uint8_t *)&gm_l11) + 8);
                                    gm_b16_8 t88 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l11)) + t84 * 16));
                                    gm_l13 = t88;
                                    {
                                        {
                                            gm_b16_8 t91;
                                            GM_ST(void *, (uint8_t *)&t91, (void *)(gm_so78814tnt5g5));
                                            GM_ST(uintptr_t, (uint8_t *)&t91 + 8, (uintptr_t)(1));
                                            GM_ST(gm_b16_8, (uint8_t *)&F.l14 + 0, t91);
                                            gm_b16_8 t92;
                                            GM_ST(void *, (uint8_t *)&t92, (void *)(gm_svtktff6b3snn));
                                            GM_ST(uintptr_t, (uint8_t *)&t92 + 8, (uintptr_t)(2));
                                            GM_ST(gm_b16_8, (uint8_t *)&F.l14 + 16, t92);
                                            gm_b16_8 t93;
                                            GM_ST(void *, (uint8_t *)&t93, (void *)(gm_s6vtoqhgjb0nd));
                                            GM_ST(uintptr_t, (uint8_t *)&t93 + 8, (uintptr_t)(5));
                                            GM_ST(gm_b16_8, (uint8_t *)&F.l14 + 32, t93);
                                        }
                                        {
                                            {
                                                {
                                                    gm_b16_8 t96 = GM_LD(gm_b16_8, (((uint8_t *)&F.l14) + 0));
                                                    GM_ST(gm_b16_8, (uint8_t *)&F.l15 + 0, t96);
                                                    gm_b16_8 t97 = GM_LD(gm_b16_8, (((uint8_t *)&F.l14) + 16));
                                                    GM_ST(gm_b16_8, (uint8_t *)&F.l15 + 16, t97);
                                                    gm_b16_8 t98 = GM_LD(gm_b16_8, (((uint8_t *)&F.l14) + 32));
                                                    GM_ST(gm_b16_8, (uint8_t *)&F.l15 + 32, t98);
                                                }
                                                void * t99 = ((uint8_t *)&F.l15);
                                                if ((uintptr_t)0 > (uintptr_t)3 || (uintptr_t)3 > (uintptr_t)3) gm_trap_slice((uintptr_t)0, (uintptr_t)3, 3, gm_srcqr4ch56kv6bv, 702, 13);
                                                gm_b16_8 t100;
                                                GM_ST(void *, (uint8_t *)&t100, (void *)(((uint8_t *)t99 + (uintptr_t)0 * 16)));
                                                GM_ST(uintptr_t, (uint8_t *)&t100 + 8, (uintptr_t)((uintptr_t)3 - (uintptr_t)0));
                                                gm_b16_8 t101 = t100;
                                                gm_l16 = t101;
                                            }
                                            {
                                                gm_l17 = ((uint64_t)0ULL);
                                            }
                                            {
                                                for (;;) {
                                                    uint64_t t103 = gm_l17;
                                                    uint64_t t104 = GM_LD(uintptr_t, ((uint8_t *)&gm_l16) + 8);
                                                    uint8_t t105 = (t103 < t104);
                                                    if (!t105) goto lb102;
                                                    {
                                                        uint64_t t108 = gm_l17;
                                                        uint64_t t109 = (uintptr_t)t108;
                                                        uint64_t t110 = GM_LD(uintptr_t, ((uint8_t *)&gm_l16) + 8);
                                                        gm_b16_8 t113 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l16)) + t109 * 16));
                                                        gm_l18 = t113;
                                                        {
                                                            gm_b16_8 t115 = gm_l13;
                                                            gm_t6j6smj7qoctu t116;
                                                            gm_b16_8 t117 = gm_l10;
                                                            GM_ST(gm_b16_8, (uint8_t *)&t116 + 0, t117);
                                                            gm_b16_8 t118 = gm_l18;
                                                            GM_ST(gm_b16_8, (uint8_t *)&t116 + 16, t118);
                                                            gm_tabb710d9mq2e t119;
                                                            gm_f_format_rjqt2eo((void *)&t119, (void *)NULL, cx, NULL, &t116);
                                                            if (gm_unw.kind) {
                                                                goto gm_ll106;
                                                            }
                                                            gm_tabb710d9mq2e t120 = t119;
                                                            if (((((uint8_t *)&t120))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t120) + 8, gm_srcqr4ch56kv6bv, 703, 38);
                                                            gm_b16_8 t121 = GM_LD(gm_b16_8, (((uint8_t *)&t120) + 8));
                                                            gm_b16_8 t122 = t121;
                                                            gm_b16_8 t123 = GM_LD(gm_b16_8, ((uint8_t *)&t122));
                                                            gm_b16_8 t124 = gm_f_join_qveh5b2((void *)NULL, cx, NULL, t115, t123);
                                                            if (gm_unw.kind) {
                                                                goto gm_ll106;
                                                            }
                                                            gm_l19 = t124;
                                                        }
                                                        {
                                                            gm_b16_8 t126 = gm_l19;
                                                            gm_node *pn127 = gm_find(cx, 2126215853);
                                                            if (!pn127) gm_trap("no handler installed for effect `Fs`", gm_srcqr4ch56kv6bv, 704, 28);
                                                            gm_tih6h4avv5nrb t128;
                                                            ((void (*)(void *, void *, gm_node *, gm_node *, gm_b16_8))pn127->clauses[5])((void *)&t128, pn127->env, pn127->hctx, pn127, t126);
                                                            if (gm_unw.kind) {
                                                                goto gm_ll106;
                                                            }
                                                            gm_tih6h4avv5nrb t129 = t128;
                                                            if (((((uint8_t *)&t129))[0] != 0)) goto ie125;
                                                            gm_t5j14m8ru132p t130 = GM_LD(gm_t5j14m8ru132p, (((uint8_t *)&t129) + 8));
                                                            gm_l20 = t130;
                                                            {
                                                                void * t132 = (void *)((uint8_t *)&F.l7);
                                                                gm_t2u4qamcbbgu0 t133;
                                                                gm_b16_8 t134 = gm_l19;
                                                                GM_ST(gm_b16_8, (uint8_t *)&t133 + 0, t134);
                                                                uint64_t t135 = GM_LD(uint64_t, (((uint8_t *)&gm_l20) + 0));
                                                                GM_ST(uint64_t, (uint8_t *)&t133 + 16, t135);
                                                                int64_t t136 = GM_LD(int64_t, (((uint8_t *)&gm_l20) + 16));
                                                                GM_ST(int64_t, (uint8_t *)&t133 + 24, t136);
                                                                gm_tabb710d9mq2e t137;
                                                                gm_f_format_a1j85ht((void *)&t137, (void *)NULL, cx, NULL, &t133);
                                                                if (gm_unw.kind) {
                                                                    goto gm_ll106;
                                                                }
                                                                gm_tabb710d9mq2e t138 = t137;
                                                                if (((((uint8_t *)&t138))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t138) + 8, gm_srcqr4ch56kv6bv, 705, 33);
                                                                gm_b16_8 t139 = GM_LD(gm_b16_8, (((uint8_t *)&t138) + 8));
                                                                gm_b16_8 t140 = t139;
                                                                gm_b16_8 t141 = GM_LD(gm_b16_8, ((uint8_t *)&t140));
                                                                gm_tftb1odc739t5 t142 = gm_f_push_3ef746b(t132, t141);
                                                                gm_tftb1odc739t5 t143 = t142;
                                                                if (((((uint8_t *)&t143))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t143) + 1, gm_srcqr4ch56kv6bv, 705, 21);
                                                                gm_b0_1 t144 = GM_LD(gm_b0_1, (((uint8_t *)&t143) + 1));
                                                            }
                                                            goto ix125;
ie125:;
ix125:;
                                                        }
                                                        goto me111;
mn112:;
                                                        gm_trap("no match arm matched the scrutinee", gm_srcqr4ch56kv6bv, 702, 13);
me111:;
                                                    }
lc102:;
                                                    {
                                                        uint64_t t146 = gm_l17;
                                                        uint64_t r147; bool o148 = __builtin_add_overflow(t146, ((uint64_t)1ULL), &r147);
                                                        uint64_t t149 = r147;
                                                        gm_l17 = t149;
                                                    }
                                                }
gm_ll106:;
                                                if (gm_unw.frame == &F.h && gm_unw.label == 3) {
                                                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb102; }
                                                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc102; }
                                                }
                                                goto gm_ll81;
lb102:;
                                            }
                                        }
                                    }
                                    goto me86;
mn87:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcqr4ch56kv6bv, 701, 9);
me86:;
                                }
lc77:;
                                {
                                    uint64_t t151 = gm_l12;
                                    uint64_t r152; bool o153 = __builtin_add_overflow(t151, ((uint64_t)1ULL), &r152);
                                    uint64_t t154 = r152;
                                    gm_l12 = t154;
                                }
                            }
gm_ll81:;
                            if (gm_unw.frame == &F.h && gm_unw.label == 2) {
                                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb77; }
                                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc77; }
                            }
                            goto gm_ll66;
lb77:;
                        }
                    }
                    goto me71;
mn72:;
                    gm_trap("no match arm matched the scrutinee", gm_srcqr4ch56kv6bv, 700, 5);
me71:;
                }
lc62:;
                {
                    uint64_t t156 = gm_l9;
                    uint64_t r157; bool o158 = __builtin_add_overflow(t156, ((uint64_t)1ULL), &r157);
                    uint64_t t159 = r157;
                    gm_l9 = t159;
                }
            }
gm_ll66:;
            if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb62; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc62; }
            }
            goto gm_lad;
lb62:;
        }
    }
    {
        gm_b16_8 t160 = GM_LD(gm_b16_8, (((uint8_t *)&F.l7) + 0));
        gm_b16_8 t161 = t160;
        gm_b16_8 t162 = GM_LD(gm_b16_8, ((uint8_t *)&t161));
        gm_b16_8 t163;
        GM_ST(void *, (uint8_t *)&t163, (void *)(gm_s0pi0emdqd3vb));
        GM_ST(uintptr_t, (uint8_t *)&t163 + 8, (uintptr_t)(1));
        gm_tabb710d9mq2e t164;
        gm_f_join_sfknljb((void *)&t164, (void *)NULL, cx, NULL, t162, t163);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t165 = t164;
        if (((((uint8_t *)&t165))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t165) + 8, gm_srcqr4ch56kv6bv, 710, 12);
        gm_b16_8 t166 = GM_LD(gm_b16_8, (((uint8_t *)&t165) + 8));
        gm_b16_8 t167 = t166;
        gm_b16_8 t168 = GM_LD(gm_b16_8, ((uint8_t *)&t167));
        uint8_t t169 = 0;
        F.ret = t168;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_opt_level_gnm5dcm(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_opt_level_gnm5dcm F;
    void * gm_l0; /* opts */
    GM_STACK_CHECK_SHALLOW(&F, 131u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    uint8_t t2 = ((GM_LD(uint8_t, (((uint8_t *)t1) + 4) + 0) == ((uint8_t)0ULL)));
    void * t3 = gm_l0;
    uint8_t t4 = GM_LD(uint8_t, (((uint8_t *)t3) + 33));
    gm_tqs5p4n2tq19v t5;
    GM_ST(uint8_t, ((uint8_t *)&t5 + 0), t2);
    GM_ST(uint8_t, ((uint8_t *)&t5 + 1), t4);
    gm_tqs5p4n2tq19v t6 = t5;
    gm_b16_8 t7;
    uint8_t t10 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
    if (!(t10 == ((uint8_t)1))) goto mn9;
    uint8_t t11 = GM_LD(uint8_t, (((uint8_t *)&t6) + 1));
    if (!(t11 == ((uint8_t)1))) goto mn9;
    gm_b16_8 t12;
    GM_ST(void *, (uint8_t *)&t12, (void *)(gm_s3vboaodpmi9a));
    GM_ST(uintptr_t, (uint8_t *)&t12 + 8, (uintptr_t)(3));
    t7 = t12;
    goto me8;
mn9:;
    uint8_t t14 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
    if (!(t14 == ((uint8_t)1))) goto mn13;
    uint8_t t15 = GM_LD(uint8_t, (((uint8_t *)&t6) + 1));
    if (!(t15 == ((uint8_t)0))) goto mn13;
    gm_b16_8 t16;
    GM_ST(void *, (uint8_t *)&t16, (void *)(gm_shgi3g6gdv99o));
    GM_ST(uintptr_t, (uint8_t *)&t16 + 8, (uintptr_t)(3));
    t7 = t16;
    goto me8;
mn13:;
    uint8_t t18 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
    if (!(t18 == ((uint8_t)0))) goto mn17;
    gm_b16_8 t19;
    GM_ST(void *, (uint8_t *)&t19, (void *)(gm_sr2df1mc0vbea));
    GM_ST(uintptr_t, (uint8_t *)&t19 + 8, (uintptr_t)(3));
    t7 = t19;
    goto me8;
mn17:;
    gm_trap("no match arm matched the scrutinee", gm_srcqr4ch56kv6bv, 159, 52);
me8:;
    F.ret = t7;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_pid_token_c444nkv(void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_pid_token_c444nkv F;
    gm_tpvg3mjn3dp06 gm_l0; /* r */
    GM_STACK_CHECK(&F, 133u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
    }
    {
        void * t2 = ((uint8_t *)&gm_l0);
        if ((uintptr_t)0 > (uintptr_t)4 || (uintptr_t)4 > (uintptr_t)4) gm_trap_slice((uintptr_t)0, (uintptr_t)4, 4, gm_srcqr4ch56kv6bv, 441, 15);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(((uint8_t *)t2 + (uintptr_t)0 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)((uintptr_t)4 - (uintptr_t)0));
        gm_node *pn4 = gm_find(cx, 1087599154);
        if (!pn4) gm_trap("no handler installed for effect `Rand`", gm_srcqr4ch56kv6bv, 441, 5);
        gm_b0_1 t5;
        t5 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, gm_b16_8))pn4->clauses[0])(pn4->env, pn4->hctx, pn4, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t6 = ((uint8_t *)&gm_l0);
        if ((uintptr_t)0 > (uintptr_t)4 || (uintptr_t)4 > (uintptr_t)4) gm_trap_slice((uintptr_t)0, (uintptr_t)4, 4, gm_srcqr4ch56kv6bv, 442, 20);
        gm_b16_8 t7;
        GM_ST(void *, (uint8_t *)&t7, (void *)(((uint8_t *)t6 + (uintptr_t)0 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)((uintptr_t)4 - (uintptr_t)0));
        gm_b16_8 t8 = t7;
        gm_b16_8 t9 = GM_LD(gm_b16_8, ((uint8_t *)&t8));
        gm_b16_8 t10 = gm_f_hex_euih242((void *)NULL, cx, NULL, t9);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_report_unsupported_9rg6nq1(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_report_unsupported_9rg6nq1 F;
    void * gm_l0; /* s */
    gm_t08olq97rscn3 gm_l1; /* u */
    void * gm_l2; /* file */
    gm_tcc1rq4n1avb2 gm_l3; /* lc */
    gm_b16_8 gm_l4; /* place */
    GM_STACK_CHECK(&F, 457u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_b16_8 t2;
        gm_tl874t2atnlh3 t4 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)a1) + 16));
        gm_tl874t2atnlh3 t5 = GM_LD(gm_tl874t2atnlh3, gm_cejn1dj0054);
        gm_tl874t2atnlh3 t6 = t4;
        gm_tl874t2atnlh3 t7 = t5;
        uint8_t t8 = (gm_eqff85ac10o3(((uint8_t *)&t6), ((uint8_t *)&t7)));
        if (!t8) goto ie3;
        gm_b16_8 t10;
        GM_ST(void *, (uint8_t *)&t10, (void *)(gm_sr4qbrk6tlgaa));
        GM_ST(uintptr_t, (uint8_t *)&t10 + 8, (uintptr_t)(0));
        gm_b16_8 t11 = t10;
        t2 = t11;
        goto ix3;
ie3:;
        {
            void * t13 = gm_l0;
            void * t14 = GM_LD(void *, (((uint8_t *)t13) + 0));
            void * t15 = GM_LD(void *, (((uint8_t *)t14) + 0));
            uint32_t t16 = GM_LD(uint32_t, ((((uint8_t *)a1) + 16) + 0));
            void * t17 = gm_f_file_o15fqku(t15, t16);
            gm_l2 = t17;
        }
        {
            void * t18 = gm_l2;
            uint32_t t19 = GM_LD(uint32_t, ((((uint8_t *)a1) + 16) + 4));
            gm_tcc1rq4n1avb2 t20 = GM_CONV(gm_tcc1rq4n1avb2, gm_f_line_col_9vn24mu(t18, t19));
            gm_l3 = t20;
        }
        gm_t5hdnnftkup6m t21;
        void * t22 = gm_l2;
        gm_b16_8 t23 = GM_LD(gm_b16_8, (((uint8_t *)t22) + 0));
        GM_ST(gm_b16_8, (uint8_t *)&t21 + 0, t23);
        uint32_t t24 = GM_LD(uint32_t, (((uint8_t *)&gm_l3) + 0));
        GM_ST(uint32_t, (uint8_t *)&t21 + 16, t24);
        uint32_t t25 = GM_LD(uint32_t, (((uint8_t *)&gm_l3) + 4));
        GM_ST(uint32_t, (uint8_t *)&t21 + 20, t25);
        gm_tabb710d9mq2e t26;
        gm_f_format_cv7qh05((void *)&t26, (void *)NULL, cx, NULL, &t21);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t27 = t26;
        if (((((uint8_t *)&t27))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t27) + 8, gm_srcqr4ch56kv6bv, 153, 9);
        gm_b16_8 t28 = GM_LD(gm_b16_8, (((uint8_t *)&t27) + 8));
        gm_b16_8 t29 = t28;
        gm_b16_8 t30 = t29;
        gm_b16_8 t31 = GM_LD(gm_b16_8, ((uint8_t *)&t30));
        t2 = t31;
ix3:;
        gm_l4 = t2;
    }
    {
        gm_b16_8 t32 = gm_l4;
        gm_b16_8 t33 = t32;
        gm_b16_8 t34 = GM_LD(gm_b16_8, (((uint8_t *)a1) + 0));
        gm_b16_8 t35 = t34;
        if (gm_pfu8dq5bikbh(cx, &gm_ty0iqiubfm7e, ((uint8_t *)&t33), gm_srcqr4ch56kv6bv, 155, 5)) {
            goto gm_lad;
        }
        if (gm_pf454qfrpt6r(cx, gm_sfss9c30cdmun, 7, gm_srcqr4ch56kv6bv, 155, 5)) {
            goto gm_lad;
        }
        if (gm_pfu8dq5bikbh(cx, &gm_ty0iqiubfm7e, ((uint8_t *)&t35), gm_srcqr4ch56kv6bv, 155, 5)) {
            goto gm_lad;
        }
        if (gm_pf454qfrpt6r(cx, gm_so514oj5nbhcu, 1, gm_srcqr4ch56kv6bv, 155, 5)) {
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
inline __attribute__((aligned(4))) void gm_f_rt_flags_oeql34c(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_rt_flags_oeql34c F;
    void * gm_l0; /* opts */
    gm_b16_8 gm_l2; /* f */
    GM_STACK_CHECK(&F, 197u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        gm_f_base_cflags_irg71ui((void *)&F.l1, (void *)NULL, cx, NULL, &(*(gm_t0rfe1ou14h21 *)(((uint8_t *)t2) + 400)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t3 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t4;
        GM_ST(void *, (uint8_t *)&t4, (void *)(gm_sr2df1mc0vbea));
        GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(3));
        gm_tftb1odc739t5 t5 = gm_f_push_3ef746b(t3, t4);
        gm_tftb1odc739t5 t6 = t5;
        if (((((uint8_t *)&t6))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t6) + 1, gm_srcqr4ch56kv6bv, 273, 5);
        gm_b0_1 t7 = GM_LD(gm_b0_1, (((uint8_t *)&t6) + 1));
    }
    {
        void * t9 = gm_l0;
        gm_th2lqbo77pvsc t10;
        gm_f_f16c_flag_vpo7tss((void *)&t10, &(*(gm_t0rfe1ou14h21 *)(((uint8_t *)t9) + 400)));
        gm_th2lqbo77pvsc t11 = t10;
        if (!((((uint8_t *)&t11))[16] != 0)) goto ie8;
        gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
        gm_l2 = t12;
        {
            void * t14 = (void *)((uint8_t *)&F.l1);
            gm_b16_8 t15 = gm_l2;
            gm_tftb1odc739t5 t16 = gm_f_push_3ef746b(t14, t15);
            gm_tftb1odc739t5 t17 = t16;
            if (((((uint8_t *)&t17))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t17) + 1, gm_srcqr4ch56kv6bv, 274, 38);
            gm_b0_1 t18 = GM_LD(gm_b0_1, (((uint8_t *)&t17) + 1));
        }
        goto ix8;
ie8:;
ix8:;
    }
    {
        void * t20 = gm_l0;
        uint8_t t21 = GM_LD(uint8_t, (((uint8_t *)t20) + 33));
        if (!t21) goto ie19;
        {
            void * t23 = (void *)((uint8_t *)&F.l1);
            gm_b16_8 t24;
            GM_ST(void *, (uint8_t *)&t24, (void *)(gm_sn0peadng5i31));
            GM_ST(uintptr_t, (uint8_t *)&t24 + 8, (uintptr_t)(2));
            gm_tftb1odc739t5 t25 = gm_f_push_3ef746b(t23, t24);
            gm_tftb1odc739t5 t26 = t25;
            if (((((uint8_t *)&t26))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t26) + 1, gm_srcqr4ch56kv6bv, 275, 26);
            gm_b0_1 t27 = GM_LD(gm_b0_1, (((uint8_t *)&t26) + 1));
        }
        goto ix19;
ie19:;
ix19:;
    }
    {
        memmove(gm_out, ((uint8_t *)&F.l1), 40);
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
inline __attribute__((aligned(4))) uint8_t gm_f_run_compiler_ulaqcb1(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1) {
    struct gm_fr_run_compiler_ulaqcb1 F;
    void * gm_l0; /* tc */
    gm_b16_8 gm_l1; /* argv */
    gm_tj5jn79kq9pnm gm_l2; /* e */
    GM_STACK_CHECK(&F, 707u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        gm_b16_8 t2 = gm_l1;
        gm_t32dckm4jfarl t3 = GM_LD(gm_t32dckm4jfarl, gm_c7mc3uaor1r_2);
        gm_tat31u3lvqjk4 t4;
        gm_f_output_mg6eie5((void *)&t4, (void *)NULL, cx, NULL, t2, &t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tat31u3lvqjk4 t5 = t4;
        gm_tjm4rm50v5mle t6;
        if (!((((uint8_t *)&t5))[0] != 0)) {
            gm_tjm4rm50v5mle t7 = GM_LD(gm_tjm4rm50v5mle, (((uint8_t *)&t5) + 8));
            t6 = t7;
        } else {
            gm_tj5jn79kq9pnm t8 = GM_LD(gm_tj5jn79kq9pnm, (((uint8_t *)&t5) + 8));
            gm_l2 = t8;
            {
                void * t10 = gm_l0;
                gm_b16_8 t11 = gm_f_display_30kc1i9((void *)NULL, cx, NULL, t10);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_b16_8 t12 = t11;
                gm_tj5jn79kq9pnm t13 = gm_l2;
                gm_b16_8 t14 = gm_f_run_error_gs1dinp((void *)NULL, cx, NULL, t13);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_b16_8 t15 = t14;
                if (gm_pf454qfrpt6r(cx, gm_sr3et5tffndla, 27, gm_srcqr4ch56kv6bv, 573, 9)) {
                    goto gm_lad;
                }
                if (gm_pfu8dq5bikbh(cx, &gm_ty0iqiubfm7e, ((uint8_t *)&t12), gm_srcqr4ch56kv6bv, 573, 9)) {
                    goto gm_lad;
                }
                if (gm_pf454qfrpt6r(cx, gm_sqdfr4k3vea5p, 3, gm_srcqr4ch56kv6bv, 573, 9)) {
                    goto gm_lad;
                }
                if (gm_pfu8dq5bikbh(cx, &gm_ty0iqiubfm7e, ((uint8_t *)&t15), gm_srcqr4ch56kv6bv, 573, 9)) {
                    goto gm_lad;
                }
                if (gm_pf454qfrpt6r(cx, gm_so514oj5nbhcu, 1, gm_srcqr4ch56kv6bv, 573, 9)) {
                    goto gm_lad;
                }
            }
            uint8_t t16 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        F.l3 = t6;
    }
    {
        gm_tsoiri44d0i6q t18 = GM_LD(gm_tsoiri44d0i6q, (((uint8_t *)&F.l3) + 0));
        uint8_t t19 = gm_f_success_q3lb6vt(t18);
        uint8_t t20 = (uint8_t)!t19;
        if (!t20) goto ie17;
        {
            gm_b16_8 t22 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l3) + 48) + 0));
            gm_b16_8 t23 = t22;
            gm_b16_8 t24 = GM_LD(gm_b16_8, ((uint8_t *)&t23));
            gm_b16_8 t25 = t24;
            if (gm_pf454qfrpt6r(cx, gm_s1ctr86inn5iu, 23, gm_srcqr4ch56kv6bv, 577, 9)) {
                goto gm_lad;
            }
            if (gm_pfu8dq5bikbh(cx, &gm_ty0iqiubfm7e, ((uint8_t *)&t25), gm_srcqr4ch56kv6bv, 577, 9)) {
                goto gm_lad;
            }
        }
        {
            uint8_t t26 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        goto ix17;
ie17:;
ix17:;
    }
    {
        uint8_t t27 = 0;
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
__attribute__((aligned(4))) uint8_t gm_f_run_dg4eogs(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, const void * a2) {
    struct gm_fr_run_dg4eogs F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* root */
    uint64_t gm_l3; /* d */
    uint32_t gm_l4; /* main */
    gm_tdvq40fmif5j0 gm_l5; /* entries */
    gm_b16_8 gm_l7; /* exe */
    gm_b16_8 gm_l8; /* dir */
    gm_b16_8 gm_l9; /* path */
    gm_b16_8 gm_l11; /* e */
    gm_toe5fmr3vvf4d gm_l14; /* e */
    gm_tsoiri44d0i6q gm_l15; /* status */
    GM_STACK_CHECK(&F, 2938u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    memcpy(&F.l2, a2, sizeof F.l2);
    {
        void * t2 = gm_l0;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 0));
        uint64_t t4 = gm_l1;
        gm_b16_8 t5;
        GM_ST(void *, (uint8_t *)&t5, (void *)(gm_sog4rksrmsqd2));
        GM_ST(uintptr_t, (uint8_t *)&t5 + 8, (uintptr_t)(4));
        gm_tf5srh4mp4b3i t6 = gm_f_lookup_59juqoo(t3, t4, t5);
        gm_tf5srh4mp4b3i t7 = t6;
        uint64_t t8;
        if (((((uint8_t *)&t7))[8] != 0)) {
            uint64_t t9 = GM_LD(uint64_t, (((uint8_t *)&t7) + 0));
            t8 = t9;
        } else {
            {
                if (gm_pf454qfrpt6r(cx, gm_setv7ejg35cg9, 42, gm_srcqr4ch56kv6bv, 598, 9)) {
                    goto gm_lad;
                }
            }
            uint8_t t11 = 0;
            F.ret = ((uint8_t)1ULL);
            goto gm_fret;
        }
        gm_l3 = t8;
    }
    {
        void * t13 = gm_l0;
        uint64_t t14 = gm_l3;
        gm_t6qooom8qa2do t15;
        gm_f_resolve_decl_pus042v((void *)&t15, (void *)NULL, cx, NULL, t13, t14);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t6qooom8qa2do t16 = t15;
        uint8_t t17 = GM_LD(uint8_t, (((uint8_t *)&t16) + 0));
        if (t17 != ((uint8_t)5ULL)) goto ie12;
        uint32_t t18 = GM_LD(uint32_t, (((uint8_t *)&t16) + 8));
        gm_l4 = t18;
        {
            void * t20 = gm_l0;
            uint32_t t21 = gm_l4;
            uint32_t t22 = gm_f_entry_point_n2gu79u((void *)NULL, cx, NULL, t20, t21);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tdvq40fmif5j0 t23;
            GM_ST(uint32_t, ((uint8_t *)&t23 + 0), t22);
            gm_l5 = t23;
        }
        {
            void * t24 = gm_l0;
            void * t25 = ((uint8_t *)&gm_l5);
            if ((uintptr_t)0 > (uintptr_t)1 || (uintptr_t)1 > (uintptr_t)1) gm_trap_slice((uintptr_t)0, (uintptr_t)1, 1, gm_srcqr4ch56kv6bv, 603, 30);
            gm_b16_8 t26;
            GM_ST(void *, (uint8_t *)&t26, (void *)(((uint8_t *)t25 + (uintptr_t)0 * 4)));
            GM_ST(uintptr_t, (uint8_t *)&t26 + 8, (uintptr_t)((uintptr_t)1 - (uintptr_t)0));
            gm_b16_8 t27 = t26;
            gm_b16_8 t28 = GM_LD(gm_b16_8, ((uint8_t *)&t27));
            void * t29 = ((uint8_t *)&gm_l5);
            if ((uintptr_t)0 > (uintptr_t)1 || (uintptr_t)1 > (uintptr_t)1) gm_trap_slice((uintptr_t)0, (uintptr_t)1, 1, gm_srcqr4ch56kv6bv, 603, 43);
            gm_b16_8 t30;
            GM_ST(void *, (uint8_t *)&t30, (void *)(((uint8_t *)t29 + (uintptr_t)0 * 4)));
            GM_ST(uintptr_t, (uint8_t *)&t30 + 8, (uintptr_t)((uintptr_t)1 - (uintptr_t)0));
            gm_b16_8 t31 = t30;
            gm_b16_8 t32 = GM_LD(gm_b16_8, ((uint8_t *)&t31));
            gm_t5i6e9e007icp t33;
            gm_f_generate_program_9mr9pv8((void *)&t33, (void *)NULL, cx, NULL, t24, t28, t32, ((uint8_t)0), &(*(gm_tdkfkbgm73fql *)((uint8_t *)&F.l2)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t5i6e9e007icp t34 = t33;
            gm_tt1457on428g4 t35;
            if (((((uint8_t *)&t34))[248] != 0)) {
                gm_tt1457on428g4 t36 = GM_LD(gm_tt1457on428g4, (((uint8_t *)&t34) + 0));
                t35 = t36;
            } else {
                uint8_t t37 = 0;
                F.ret = ((uint8_t)1ULL);
                goto gm_fret;
            }
            F.l6 = t35;
        }
        {
            gm_b16_8 t38;
            GM_ST(void *, (uint8_t *)&t38, (void *)(gm_sr4qbrk6tlgaa));
            GM_ST(uintptr_t, (uint8_t *)&t38 + 8, (uintptr_t)(0));
            gm_l7 = t38;
        }
        {
            gm_b16_8 t39;
            GM_ST(void *, (uint8_t *)&t39, (void *)(gm_sr4qbrk6tlgaa));
            GM_ST(uintptr_t, (uint8_t *)&t39 + 8, (uintptr_t)(0));
            gm_l8 = t39;
        }
        {
            void * t40 = (void *)((uint8_t *)&F.l6);
            gm_tl572d7579hns t41;
            gm_f_cached_program_5afqj27((void *)&t41, (void *)NULL, cx, NULL, t40, &(*(gm_tdkfkbgm73fql *)((uint8_t *)&F.l2)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tl572d7579hns t42 = t41;
            uint8_t t48 = GM_LD(uint8_t, (((uint8_t *)&t42) + 0));
            switch (t48) { case ((uint8_t)0ULL): goto ma44; case ((uint8_t)1ULL): goto ma46; case ((uint8_t)2ULL): goto ma45; default: goto ma47; }
ma44:;
            uint8_t t50 = GM_LD(uint8_t, (((uint8_t *)&t42) + 0));
            if (t50 != ((uint8_t)0ULL)) goto mn49;
            gm_b16_8 t51 = GM_LD(gm_b16_8, (((uint8_t *)&t42) + 8));
            gm_l9 = t51;
            {
                gm_b16_8 t53 = gm_l9;
                gm_l7 = t53;
            }
            goto me43;
mn49:;
ma45:;
            uint8_t t55 = GM_LD(uint8_t, (((uint8_t *)&t42) + 0));
            if (t55 != ((uint8_t)2ULL)) goto mn54;
            uint8_t t56 = 0;
            F.ret = ((uint8_t)1ULL);
            goto gm_fret;
            goto me43;
mn54:;
ma46:;
            uint8_t t58 = GM_LD(uint8_t, (((uint8_t *)&t42) + 0));
            if (t58 != ((uint8_t)1ULL)) goto mn57;
            {
            }
            {
                gm_b16_8 t60 = gm_f_temp_dir_i6apmsd((void *)NULL, cx, NULL);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_b16_8 t61;
                GM_ST(void *, (uint8_t *)&t61, (void *)(gm_sehokd57s045g));
                GM_ST(uintptr_t, (uint8_t *)&t61 + 8, (uintptr_t)(10));
                void * t62 = ((uint8_t *)&F.l10);
                if ((uintptr_t)0 > (uintptr_t)4096 || (uintptr_t)4096 > (uintptr_t)4096) gm_trap_slice((uintptr_t)0, (uintptr_t)4096, 4096, gm_srcqr4ch56kv6bv, 612, 65);
                gm_b16_8 t63;
                GM_ST(void *, (uint8_t *)&t63, (void *)(((uint8_t *)t62 + (uintptr_t)0 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t63 + 8, (uintptr_t)((uintptr_t)4096 - (uintptr_t)0));
                gm_t5uv522f0p612 t64;
                gm_f_make_temp_dir_vi8f88d((void *)&t64, (void *)NULL, cx, NULL, t60, t61, t63);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_t5uv522f0p612 t65 = t64;
                gm_b16_8 t66;
                if (!((((uint8_t *)&t65))[0] != 0)) {
                    gm_b16_8 t67 = GM_LD(gm_b16_8, (((uint8_t *)&t65) + 8));
                    t66 = t67;
                } else {
                    {
                        gm_b16_8 t69 = gm_f_temp_dir_i6apmsd((void *)NULL, cx, NULL);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_b16_8 t70 = t69;
                        if (gm_pf454qfrpt6r(cx, gm_svaibtfuu0gf2, 47, gm_srcqr4ch56kv6bv, 613, 17)) {
                            goto gm_lad;
                        }
                        if (gm_pfu8dq5bikbh(cx, &gm_ty0iqiubfm7e, ((uint8_t *)&t70), gm_srcqr4ch56kv6bv, 613, 17)) {
                            goto gm_lad;
                        }
                        if (gm_pf454qfrpt6r(cx, gm_s06m3met1mb9v, 2, gm_srcqr4ch56kv6bv, 613, 17)) {
                            goto gm_lad;
                        }
                    }
                    uint8_t t71 = 0;
                    F.ret = ((uint8_t)1ULL);
                    goto gm_fret;
                }
                gm_l8 = t66;
            }
            {
                gm_b16_8 t72 = gm_l8;
                void * t73 = (void *)((uint8_t *)&F.l2);
                gm_b16_8 t74;
                GM_ST(void *, (uint8_t *)&t74, (void *)(gm_slgjpcgk90nuf));
                GM_ST(uintptr_t, (uint8_t *)&t74 + 8, (uintptr_t)(4));
                gm_b16_8 t75 = gm_f_native_exe_name_rqchv3n((void *)NULL, cx, NULL, t73, t74);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_b16_8 t76 = gm_f_join_qveh5b2((void *)NULL, cx, NULL, t72, t75);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l7 = t76;
            }
            {
                void * t78 = (void *)((uint8_t *)&F.l6);
                gm_b16_8 t79 = gm_l7;
                uint8_t t80 = gm_f_compile_built_8o9nmfu((void *)NULL, cx, NULL, t78, t79, &(*(gm_tdkfkbgm73fql *)((uint8_t *)&F.l2)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t81 = (uint8_t)!t80;
                if (!t81) goto ie77;
                {
                    {
                        gm_b16_8 t84 = gm_l8;
                        gm_ttln0d0hd70en t85 = gm_f_remove_all_h0k1qkh((void *)NULL, cx, NULL, t84);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                }
                {
                    uint8_t t86 = 0;
                    F.ret = ((uint8_t)1ULL);
                    goto gm_fret;
                }
                goto ix77;
ie77:;
ix77:;
            }
            goto me43;
mn57:;
ma47:;
            gm_trap("no match arm matched the scrutinee", gm_srcqr4ch56kv6bv, 607, 5);
me43:;
        }
        {
        }
        {
            gm_b16_8 t94 = gm_l7;
            gm_t470aqkia5212 t95;
            gm_f_runner_for_79o4h4m((void *)&t95, (void *)NULL, cx, NULL, t94);
            if (gm_unw.kind) {
                goto gm_ld19_1;
            }
            gm_t470aqkia5212 t96 = t95;
            gm_trqfvmjtt4sij t97;
            if (!((((uint8_t *)&t96))[0] != 0)) {
                gm_trqfvmjtt4sij t98 = GM_LD(gm_trqfvmjtt4sij, (((uint8_t *)&t96) + 8));
                t97 = t98;
            } else {
                gm_b16_8 t99 = GM_LD(gm_b16_8, (((uint8_t *)&t96) + 8));
                gm_l11 = t99;
                {
                    gm_b16_8 t101 = gm_l11;
                    gm_b16_8 t102 = t101;
                    if (gm_pf454qfrpt6r(cx, gm_s5lqhiu2bn6of, 7, gm_srcqr4ch56kv6bv, 625, 9)) {
                        goto gm_ld19_1;
                    }
                    if (gm_pfu8dq5bikbh(cx, &gm_ty0iqiubfm7e, ((uint8_t *)&t102), gm_srcqr4ch56kv6bv, 625, 9)) {
                        goto gm_ld19_1;
                    }
                    if (gm_pf454qfrpt6r(cx, gm_so514oj5nbhcu, 1, gm_srcqr4ch56kv6bv, 625, 9)) {
                        goto gm_ld19_1;
                    }
                }
                uint8_t t103 = 0;
                F.ret = ((uint8_t)2ULL);
                gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = t103;
                goto gm_ld19_1;
            }
            F.l12 = t97;
        }
        {
            gm_f_list_epvducg((void *)&F.l13, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_ld19_1;
            }
        }
        {
            void * t104 = (void *)((uint8_t *)&F.l13);
            gm_b16_8 t105 = GM_LD(gm_b16_8, (((uint8_t *)&F.l12) + 0));
            gm_b16_8 t106 = t105;
            gm_b16_8 t107 = GM_LD(gm_b16_8, ((uint8_t *)&t106));
            gm_tftb1odc739t5 t108 = gm_f_extend_vogsd72(t104, t107);
            gm_tftb1odc739t5 t109 = t108;
            if (((((uint8_t *)&t109))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t109) + 1, gm_srcqr4ch56kv6bv, 629, 5);
            gm_b0_1 t110 = GM_LD(gm_b0_1, (((uint8_t *)&t109) + 1));
        }
        {
            void * t111 = (void *)((uint8_t *)&F.l13);
            gm_b16_8 t112 = gm_l7;
            gm_tftb1odc739t5 t113 = gm_f_push_3ef746b(t111, t112);
            gm_tftb1odc739t5 t114 = t113;
            if (((((uint8_t *)&t114))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t114) + 1, gm_srcqr4ch56kv6bv, 630, 5);
            gm_b0_1 t115 = GM_LD(gm_b0_1, (((uint8_t *)&t114) + 1));
        }
        {
            void * t116 = (void *)((uint8_t *)&F.l13);
            gm_b16_8 t117 = GM_LD(gm_b16_8, (((uint8_t *)&F.l2) + 40));
            gm_tftb1odc739t5 t118 = gm_f_extend_vogsd72(t116, t117);
            gm_tftb1odc739t5 t119 = t118;
            if (((((uint8_t *)&t119))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t119) + 1, gm_srcqr4ch56kv6bv, 631, 5);
            gm_b0_1 t120 = GM_LD(gm_b0_1, (((uint8_t *)&t119) + 1));
        }
        {
            gm_b16_8 t121 = GM_LD(gm_b16_8, (((uint8_t *)&F.l13) + 0));
            gm_b16_8 t122 = t121;
            gm_b16_8 t123 = GM_LD(gm_b16_8, ((uint8_t *)&t122));
            gm_t32dckm4jfarl t124 = GM_LD(gm_t32dckm4jfarl, gm_c7mc3uaor1r);
            gm_tlmjd3nndlf3r t125 = gm_f_status_20hgo8k((void *)NULL, cx, NULL, t123, &t124);
            if (gm_unw.kind) {
                goto gm_ld19_1;
            }
            gm_tlmjd3nndlf3r t126 = t125;
            gm_tsoiri44d0i6q t127;
            if (!((((uint8_t *)&t126))[0] != 0)) {
                gm_tsoiri44d0i6q t128 = GM_LD(gm_tsoiri44d0i6q, (((uint8_t *)&t126) + 4));
                t127 = t128;
            } else {
                gm_toe5fmr3vvf4d t129 = GM_LD(gm_toe5fmr3vvf4d, (((uint8_t *)&t126) + 4));
                gm_l14 = t129;
                {
                    gm_b16_8 t131 = gm_l7;
                    gm_b16_8 t132 = t131;
                    gm_toe5fmr3vvf4d t133 = gm_l14;
                    gm_b16_8 t134 = gm_f_io_message_p7jbrea((void *)NULL, cx, NULL, t133);
                    if (gm_unw.kind) {
                        goto gm_ld19_1;
                    }
                    gm_b16_8 t135 = t134;
                    if (gm_pf454qfrpt6r(cx, gm_sjcgs2v314rl7, 18, gm_srcqr4ch56kv6bv, 633, 9)) {
                        goto gm_ld19_1;
                    }
                    if (gm_pfu8dq5bikbh(cx, &gm_ty0iqiubfm7e, ((uint8_t *)&t132), gm_srcqr4ch56kv6bv, 633, 9)) {
                        goto gm_ld19_1;
                    }
                    if (gm_pf454qfrpt6r(cx, gm_slgmqgkioa4u6, 2, gm_srcqr4ch56kv6bv, 633, 9)) {
                        goto gm_ld19_1;
                    }
                    if (gm_pfu8dq5bikbh(cx, &gm_ty0iqiubfm7e, ((uint8_t *)&t135), gm_srcqr4ch56kv6bv, 633, 9)) {
                        goto gm_ld19_1;
                    }
                    if (gm_pf454qfrpt6r(cx, gm_so514oj5nbhcu, 1, gm_srcqr4ch56kv6bv, 633, 9)) {
                        goto gm_ld19_1;
                    }
                }
                uint8_t t136 = 0;
                F.ret = ((uint8_t)1ULL);
                gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = t136;
                goto gm_ld19_1;
            }
            gm_l15 = t127;
        }
        {
            gm_tsoiri44d0i6q t137 = gm_l15;
            uint8_t t138 = gm_f_exit_code_dv2nn7j(t137);
            uint8_t t139 = 0;
            F.ret = t138;
            gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = t139;
            goto gm_ld19_1;
        }
        {
            uint64_t t141 = GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8);
            uint8_t t142 = (t141 > ((uint64_t)0ULL));
            if (!t142) goto ie140;
            {
                {
                    gm_b16_8 t145 = gm_l8;
                    gm_ttln0d0hd70en t146 = gm_f_remove_all_h0k1qkh((void *)NULL, cx, NULL, t145);
                }
            }
            goto ix140;
ie140:;
ix140:;
        }
        goto gm_ls147;
gm_ld19_1:;
        {
            gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
            uint64_t t149 = GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8);
            uint8_t t150 = (t149 > ((uint64_t)0ULL));
            if (!t150) goto ie148;
            {
                {
                    gm_b16_8 t153 = gm_l8;
                    gm_ttln0d0hd70en t154 = gm_f_remove_all_h0k1qkh((void *)NULL, cx, NULL, t153);
                }
            }
            goto ix148;
ie148:;
ix148:;
            gm_unw = gm_saved;
        }
        goto gm_lad;
gm_ls147:;
        goto ix12;
ie12:;
        {
            uint8_t t156 = 0;
            F.ret = ((uint8_t)1ULL);
            goto gm_fret;
        }
ix12:;
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
extern __attribute__((aligned(4))) gm_b16_8 gm_f_runtime_source_skilk06(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) void gm_f_runtime_text_37culso(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_runtime_text_37culso F;
    gm_b16_8 gm_l0; /* name */
    gm_b16_8 gm_l1; /* rt */
    GM_STACK_CHECK(&F, 306u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_b16_8 t2 = gm_f_std_root_e0juohb((void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b16_8 t3 = gm_f_runtime_dir_qd07iff((void *)NULL, cx, NULL, t2);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l1 = t3;
    }
    {
        gm_b16_8 t4 = gm_l1;
        gm_b16_8 t5 = gm_l0;
        gm_b16_8 t6 = gm_f_join_qveh5b2((void *)NULL, cx, NULL, t4, t5);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tgvh15qshf643 t7;
        gm_f_read_all_r9rh9qd((void *)&t7, (void *)NULL, cx, NULL, t6);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tgvh15qshf643 t8 = t7;
        gm_b16_8 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b16_8 t10 = GM_LD(gm_b16_8, (((uint8_t *)&t8) + 8));
            t9 = t10;
        } else {
            {
                gm_b16_8 t12 = gm_l1;
                gm_b16_8 t13 = t12;
                if (gm_pf454qfrpt6r(cx, gm_sg87p4r5apham, 35, gm_srcqr4ch56kv6bv, 77, 9)) {
                    goto gm_lad;
                }
                if (gm_pfu8dq5bikbh(cx, &gm_ty0iqiubfm7e, ((uint8_t *)&t13), gm_srcqr4ch56kv6bv, 77, 9)) {
                    goto gm_lad;
                }
                if (gm_pf454qfrpt6r(cx, gm_s06m3met1mb9v, 2, gm_srcqr4ch56kv6bv, 77, 9)) {
                    goto gm_lad;
                }
            }
            gm_th2lqbo77pvsc t14;
            memset(&t14, 0, sizeof t14);
            uint8_t t15 = 0;
            F.ret = t14;
            goto gm_fret;
        }
        gm_b16_8 t16 = t9;
        gm_b16_8 t17 = GM_LD(gm_b16_8, ((uint8_t *)&t16));
        gm_th2lqbo77pvsc t18;
        GM_ST(uint8_t, (uint8_t *)&t18 + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&t18 + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&t18 + 20, 0);
        GM_ST(gm_b16_8, ((uint8_t *)&t18), t17);
        ((uint8_t *)&t18)[16] = 1;
        uint8_t t19 = 0;
        F.ret = t18;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_stem_of_t9qmlhi(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_stem_of_t9qmlhi F;
    gm_b16_8 gm_l0; /* path */
    gm_b16_8 gm_l1; /* name */
    gm_b16_8 gm_l2; /* base */
    GM_STACK_CHECK(&F, 315u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_b16_8 t2 = gm_l0;
        gm_b16_8 t3 = gm_f_default_exe_194tkju(t2);
        gm_l1 = t3;
    }
    {
        gm_b16_8 t4 = gm_l1;
        gm_th2lqbo77pvsc t5;
        gm_f_file_name_em7v8lv((void *)&t5, (void *)NULL, cx, NULL, t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_th2lqbo77pvsc t6 = t5;
        gm_b16_8 t7;
        if (((((uint8_t *)&t6))[16] != 0)) {
            gm_b16_8 t8 = GM_LD(gm_b16_8, (((uint8_t *)&t6) + 0));
            t7 = t8;
        } else {
            gm_b16_8 t9;
            GM_ST(void *, (uint8_t *)&t9, (void *)(gm_sog4rksrmsqd2));
            GM_ST(uintptr_t, (uint8_t *)&t9 + 8, (uintptr_t)(4));
            uint8_t t10 = 0;
            F.ret = t9;
            goto gm_fret;
        }
        gm_l2 = t7;
    }
    {
        gm_b16_8 t11;
        uint64_t t13 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        uint8_t t14 = ((t13 == ((uint64_t)0ULL)));
        if (!t14) goto ie12;
        gm_b16_8 t16;
        GM_ST(void *, (uint8_t *)&t16, (void *)(gm_sog4rksrmsqd2));
        GM_ST(uintptr_t, (uint8_t *)&t16 + 8, (uintptr_t)(4));
        gm_b16_8 t17 = t16;
        t11 = t17;
        goto ix12;
ie12:;
        gm_b16_8 t19 = gm_l2;
        gm_b16_8 t20 = t19;
        t11 = t20;
ix12:;
        uint8_t t21 = 0;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_target_text_s4sugoj(void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_target_text_s4sugoj F;
    GM_STACK_CHECK(&F, 275u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memcpy(&F.l0, a0, sizeof F.l0);
    gm_t2t9klu4dfekp t1;
    memset((uint8_t *)&t1 + 9, 0, 7);
    memset((uint8_t *)&t1 + 58, 0, 6);
    uint64_t t2 = GM_LD(uint64_t, (((uint8_t *)&F.l0) + 0));
    GM_ST(uint64_t, (uint8_t *)&t1 + 0, t2);
    uint8_t t3 = GM_LD(uint8_t, (((uint8_t *)&F.l0) + 8));
    GM_ST(uint8_t, (uint8_t *)&t1 + 8, t3);
    uint64_t t4 = GM_LD(uint64_t, (((uint8_t *)&F.l0) + 16));
    GM_ST(uint64_t, (uint8_t *)&t1 + 16, t4);
    gm_b16_8 t5 = GM_LD(gm_b16_8, (((uint8_t *)&F.l0) + 24));
    GM_ST(gm_b16_8, (uint8_t *)&t1 + 24, t5);
    gm_b16_8 t6 = GM_LD(gm_b16_8, (((uint8_t *)&F.l0) + 40));
    GM_ST(gm_b16_8, (uint8_t *)&t1 + 40, t6);
    uint8_t t7 = GM_LD(uint8_t, (((uint8_t *)&F.l0) + 56));
    GM_ST(uint8_t, (uint8_t *)&t1 + 56, t7);
    uint8_t t8 = GM_LD(uint8_t, (((uint8_t *)&F.l0) + 57));
    GM_ST(uint8_t, (uint8_t *)&t1 + 57, t8);
    gm_b16_8 t9;
    GM_ST(void *, (uint8_t *)&t9, (void *)(gm_sr4qbrk6tlgaa));
    GM_ST(uintptr_t, (uint8_t *)&t9 + 8, (uintptr_t)(0));
    GM_ST(gm_b16_8, (uint8_t *)&t1 + 64, t9);
    gm_tabb710d9mq2e t10;
    gm_f_format_ddl06ea((void *)&t10, (void *)NULL, cx, NULL, &t1);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tabb710d9mq2e t11 = t10;
    if (((((uint8_t *)&t11))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t11) + 8, gm_srcqr4ch56kv6bv, 714, 60);
    gm_b16_8 t12 = GM_LD(gm_b16_8, (((uint8_t *)&t11) + 8));
    gm_b16_8 t13 = t12;
    gm_b16_8 t14 = GM_LD(gm_b16_8, ((uint8_t *)&t13));
    F.ret = t14;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_unit_path_3tdv4sg(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, uint64_t a1) {
    struct gm_fr_unit_path_3tdv4sg F;
    gm_b16_8 gm_l0; /* exe */
    uint64_t gm_l1; /* i */
    GM_STACK_CHECK(&F, 385u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_b16_8 t1;
    uint64_t t3 = gm_l1;
    uint8_t t4 = ((t3 == ((uint64_t)0ULL)));
    if (!t4) goto ie2;
    gm_b16_8 t6 = gm_l0;
    gm_tnpifv20irnph t7;
    GM_ST(gm_b16_8, ((uint8_t *)&t7 + 0), t6);
    gm_tabb710d9mq2e t8;
    gm_f_format_cncadt9((void *)&t8, (void *)NULL, cx, NULL, t7);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tabb710d9mq2e t9 = t8;
    if (((((uint8_t *)&t9))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t9) + 8, gm_srcqr4ch56kv6bv, 446, 77);
    gm_b16_8 t10 = GM_LD(gm_b16_8, (((uint8_t *)&t9) + 8));
    gm_b16_8 t11 = t10;
    gm_b16_8 t12 = t11;
    gm_b16_8 t13 = GM_LD(gm_b16_8, ((uint8_t *)&t12));
    t1 = t13;
    goto ix2;
ie2:;
    gm_t3877pk7lv748 t15;
    gm_b16_8 t16 = gm_l0;
    GM_ST(gm_b16_8, (uint8_t *)&t15 + 0, t16);
    uint64_t t17 = gm_l1;
    GM_ST(uint64_t, (uint8_t *)&t15 + 16, t17);
    gm_tabb710d9mq2e t18;
    gm_f_format_4hckftk((void *)&t18, (void *)NULL, cx, NULL, &t15);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tabb710d9mq2e t19 = t18;
    if (((((uint8_t *)&t19))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t19) + 8, gm_srcqr4ch56kv6bv, 446, 111);
    gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&t19) + 8));
    gm_b16_8 t21 = t20;
    gm_b16_8 t22 = t21;
    gm_b16_8 t23 = GM_LD(gm_b16_8, ((uint8_t *)&t22));
    t1 = t23;
ix2:;
    F.ret = t1;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_unit_sources_329det7(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_unit_sources_329det7 F;
    void * gm_l0; /* b */
    void * gm_l1; /* split */
    gm_b16_8 gm_l3; /* prelude */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    gm_b16_8 gm_l6; /* u */
    GM_STACK_CHECK(&F, 517u);
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
        gm_tfeab279c16rr t2;
        gm_b16_8 t3 = GM_LD(gm_b16_8, gm_c69snm9duhi);
        GM_ST(gm_b16_8, (uint8_t *)&t2 + 0, t3);
        void * t4 = gm_l0;
        gm_b16_8 t5 = GM_LD(gm_b16_8, (((uint8_t *)t4) + 216));
        GM_ST(gm_b16_8, (uint8_t *)&t2 + 16, t5);
        void * t6 = gm_l1;
        gm_b16_8 t7 = GM_LD(gm_b16_8, (((uint8_t *)t6) + 0));
        GM_ST(gm_b16_8, (uint8_t *)&t2 + 32, t7);
        gm_tabb710d9mq2e t8;
        gm_f_format_359meqs((void *)&t8, (void *)NULL, cx, NULL, &t2);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t9 = t8;
        if (((((uint8_t *)&t9))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t9) + 8, gm_srcqr4ch56kv6bv, 235, 16);
        gm_b16_8 t10 = GM_LD(gm_b16_8, (((uint8_t *)&t9) + 8));
        gm_l3 = t10;
    }
    {
        {
            void * t12 = gm_l1;
            gm_b16_8 t13 = GM_LD(gm_b16_8, (((uint8_t *)t12) + 16));
            gm_l4 = t13;
        }
        {
            gm_l5 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t15 = gm_l5;
                uint64_t t16 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                uint8_t t17 = (t15 < t16);
                if (!t17) goto lb14;
                {
                    uint64_t t20 = gm_l5;
                    uint64_t t21 = (uintptr_t)t20;
                    uint64_t t22 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                    gm_b16_8 t25 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t21 * 16));
                    gm_l6 = t25;
                    {
                        void * t27 = (void *)((uint8_t *)&F.l2);
                        gm_t6j6smj7qoctu t28;
                        gm_b16_8 t29 = gm_l3;
                        GM_ST(gm_b16_8, (uint8_t *)&t28 + 0, t29);
                        gm_b16_8 t30 = gm_l6;
                        GM_ST(gm_b16_8, (uint8_t *)&t28 + 16, t30);
                        gm_tabb710d9mq2e t31;
                        gm_f_format_5029eea((void *)&t31, (void *)NULL, cx, NULL, &t28);
                        if (gm_unw.kind) {
                            goto gm_ll18;
                        }
                        gm_tabb710d9mq2e t32 = t31;
                        if (((((uint8_t *)&t32))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t32) + 8, gm_srcqr4ch56kv6bv, 236, 41);
                        gm_b16_8 t33 = GM_LD(gm_b16_8, (((uint8_t *)&t32) + 8));
                        gm_b16_8 t34 = t33;
                        gm_b16_8 t35 = GM_LD(gm_b16_8, ((uint8_t *)&t34));
                        gm_tftb1odc739t5 t36 = gm_f_push_3ef746b(t27, t35);
                        gm_tftb1odc739t5 t37 = t36;
                        if (((((uint8_t *)&t37))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t37) + 1, gm_srcqr4ch56kv6bv, 236, 28);
                        gm_b0_1 t38 = GM_LD(gm_b0_1, (((uint8_t *)&t37) + 1));
                    }
                    goto me23;
mn24:;
                    gm_trap("no match arm matched the scrutinee", gm_srcqr4ch56kv6bv, 236, 5);
me23:;
                }
lc14:;
                {
                    uint64_t t40 = gm_l5;
                    uint64_t r41; bool o42 = __builtin_add_overflow(t40, ((uint64_t)1ULL), &r41);
                    uint64_t t43 = r41;
                    gm_l5 = t43;
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
extern __attribute__((aligned(4))) uint8_t gm_f_wasi_target_lp6g335(const void * a0);
__attribute__((aligned(4))) uint8_t gm_f_wasi_target_lp6g335_c(gm_t0rfe1ou14h21 a0) { return gm_f_wasi_target_lp6g335(&a0); }
inline __attribute__((aligned(4))) gm_b16_8 gm_f_windows_libs_hmrgs9s(void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_windows_libs_hmrgs9s F;
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    gm_b16_8 gm_l6; /* l */
    GM_STACK_CHECK(&F, 472u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memcpy(&F.l0, a0, sizeof F.l0);
    {
        uint8_t t3 = (!((GM_LD(uintptr_t, (((uint8_t *)&F.l0) + 24) + 8) == 7) && memcmp(GM_LD(void *, (((uint8_t *)&F.l0) + 24)), gm_s048hmhpmbi7c, 7) == 0));
        if (!t3) goto ie2;
        {
            gm_b16_8 t5 = gm_f_empty_slice_5nn7ph0();
            gm_b16_8 t6 = t5;
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
        gm_f_list_epvducg((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t10;
            GM_ST(void *, (uint8_t *)&t10, (void *)(gm_skbmmrtbskpao));
            GM_ST(uintptr_t, (uint8_t *)&t10 + 8, (uintptr_t)(7));
            GM_ST(gm_b16_8, (uint8_t *)&F.l2 + 0, t10);
            gm_b16_8 t11;
            GM_ST(void *, (uint8_t *)&t11, (void *)(gm_s5hode9408atm));
            GM_ST(uintptr_t, (uint8_t *)&t11 + 8, (uintptr_t)(8));
            GM_ST(gm_b16_8, (uint8_t *)&F.l2 + 16, t11);
            gm_b16_8 t12;
            GM_ST(void *, (uint8_t *)&t12, (void *)(gm_sir0litoa3fk0));
            GM_ST(uintptr_t, (uint8_t *)&t12 + 8, (uintptr_t)(17));
            GM_ST(gm_b16_8, (uint8_t *)&F.l2 + 32, t12);
            gm_b16_8 t13;
            GM_ST(void *, (uint8_t *)&t13, (void *)(gm_sftkp1r7uetfv));
            GM_ST(uintptr_t, (uint8_t *)&t13 + 8, (uintptr_t)(9));
            GM_ST(gm_b16_8, (uint8_t *)&F.l2 + 48, t13);
            gm_b16_8 t14;
            GM_ST(void *, (uint8_t *)&t14, (void *)(gm_sqd102436v4lo));
            GM_ST(uintptr_t, (uint8_t *)&t14 + 8, (uintptr_t)(8));
            GM_ST(gm_b16_8, (uint8_t *)&F.l2 + 64, t14);
        }
        {
            {
                {
                    gm_b16_8 t17 = GM_LD(gm_b16_8, (((uint8_t *)&F.l2) + 0));
                    GM_ST(gm_b16_8, (uint8_t *)&F.l3 + 0, t17);
                    gm_b16_8 t18 = GM_LD(gm_b16_8, (((uint8_t *)&F.l2) + 16));
                    GM_ST(gm_b16_8, (uint8_t *)&F.l3 + 16, t18);
                    gm_b16_8 t19 = GM_LD(gm_b16_8, (((uint8_t *)&F.l2) + 32));
                    GM_ST(gm_b16_8, (uint8_t *)&F.l3 + 32, t19);
                    gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&F.l2) + 48));
                    GM_ST(gm_b16_8, (uint8_t *)&F.l3 + 48, t20);
                    gm_b16_8 t21 = GM_LD(gm_b16_8, (((uint8_t *)&F.l2) + 64));
                    GM_ST(gm_b16_8, (uint8_t *)&F.l3 + 64, t21);
                }
                void * t22 = ((uint8_t *)&F.l3);
                if ((uintptr_t)0 > (uintptr_t)5 || (uintptr_t)5 > (uintptr_t)5) gm_trap_slice((uintptr_t)0, (uintptr_t)5, 5, gm_srcqr4ch56kv6bv, 285, 5);
                gm_b16_8 t23;
                GM_ST(void *, (uint8_t *)&t23, (void *)(((uint8_t *)t22 + (uintptr_t)0 * 16)));
                GM_ST(uintptr_t, (uint8_t *)&t23 + 8, (uintptr_t)((uintptr_t)5 - (uintptr_t)0));
                gm_b16_8 t24 = t23;
                gm_l4 = t24;
            }
            {
                gm_l5 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t26 = gm_l5;
                    uint64_t t27 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                    uint8_t t28 = (t26 < t27);
                    if (!t28) goto lb25;
                    {
                        uint64_t t31 = gm_l5;
                        uint64_t t32 = (uintptr_t)t31;
                        uint64_t t33 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                        gm_b16_8 t36 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t32 * 16));
                        gm_l6 = t36;
                        {
                            void * t38 = (void *)((uint8_t *)&F.l1);
                            gm_b16_8 t39 = gm_l6;
                            gm_tftb1odc739t5 t40 = gm_f_push_3ef746b(t38, t39);
                            gm_tftb1odc739t5 t41 = t40;
                            if (((((uint8_t *)&t41))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t41) + 1, gm_srcqr4ch56kv6bv, 285, 86);
                            gm_b0_1 t42 = GM_LD(gm_b0_1, (((uint8_t *)&t41) + 1));
                        }
                        goto me34;
mn35:;
                        gm_trap("no match arm matched the scrutinee", gm_srcqr4ch56kv6bv, 285, 5);
me34:;
                    }
lc25:;
                    {
                        uint64_t t44 = gm_l5;
                        uint64_t r45; bool o46 = __builtin_add_overflow(t44, ((uint64_t)1ULL), &r45);
                        uint64_t t47 = r45;
                        gm_l5 = t47;
                    }
                }
lb25:;
            }
        }
    }
    {
        gm_b16_8 t48 = GM_LD(gm_b16_8, (((uint8_t *)&F.l1) + 0));
        gm_b16_8 t49 = t48;
        gm_b16_8 t50 = GM_LD(gm_b16_8, ((uint8_t *)&t49));
        uint8_t t51 = 0;
        F.ret = t50;
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
inline __attribute__((aligned(4))) uint8_t gm_f_write_generated_c_kb09043(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1) {
    struct gm_fr_write_generated_c_kb09043 F;
    gm_b16_8 gm_l0; /* path */
    gm_b16_8 gm_l1; /* code */
    gm_tm98ap4112hee gm_l2; /* f */
    uint64_t gm_l4; /* n */
    GM_STACK_CHECK(&F, 430u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        gm_b16_8 t3 = gm_l0;
        gm_t4q2b5ibsrdik t4;
        GM_ST(uint8_t, ((uint8_t *)&t4 + 0), ((uint8_t)1));
        GM_ST(uint8_t, ((uint8_t *)&t4 + 1), ((uint8_t)0));
        GM_ST(uint8_t, ((uint8_t *)&t4 + 2), ((uint8_t)0));
        GM_ST(uint8_t, ((uint8_t *)&t4 + 3), ((uint8_t)0));
        GM_ST(uint8_t, ((uint8_t *)&t4 + 4), ((uint8_t)0));
        GM_ST(uint8_t, ((uint8_t *)&t4 + 5), ((uint8_t)0));
        gm_node *pn5 = gm_find(cx, 2126215853);
        if (!pn5) gm_trap("no handler installed for effect `Fs`", gm_srcqr4ch56kv6bv, 477, 13);
        gm_t62ebbi2i5rkd t6;
        t6 = ((gm_t62ebbi2i5rkd (*)(void *, gm_node *, gm_node *, gm_b16_8, gm_t4q2b5ibsrdik))pn5->clauses[0])(pn5->env, pn5->hctx, pn5, t3, t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t62ebbi2i5rkd t7 = t6;
        if (((((uint8_t *)&t7))[0] != 0)) goto ie2;
        gm_tm98ap4112hee t8 = GM_LD(gm_tm98ap4112hee, (((uint8_t *)&t7) + 8));
        gm_l2 = t8;
        {
        }
        {
            gm_tm98ap4112hee t10 = gm_l2;
            void * t11 = ((uint8_t *)&F.l3);
            if ((uintptr_t)0 > (uintptr_t)4096 || (uintptr_t)4096 > (uintptr_t)4096) gm_trap_slice((uintptr_t)0, (uintptr_t)4096, 4096, gm_srcqr4ch56kv6bv, 479, 25);
            gm_b16_8 t12;
            GM_ST(void *, (uint8_t *)&t12, (void *)(((uint8_t *)t11 + (uintptr_t)0 * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t12 + 8, (uintptr_t)((uintptr_t)4096 - (uintptr_t)0));
            gm_node *pn13 = gm_find(cx, 2126215853);
            if (!pn13) gm_trap("no handler installed for effect `Fs`", gm_srcqr4ch56kv6bv, 479, 14);
            gm_thf5kr0is9u9i t14;
            t14 = ((gm_thf5kr0is9u9i (*)(void *, gm_node *, gm_node *, gm_tm98ap4112hee, gm_b16_8))pn13->clauses[2])(pn13->env, pn13->hctx, pn13, t10, t12);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_thf5kr0is9u9i t15 = t14;
            uint64_t t16;
            if (!((((uint8_t *)&t15))[0] != 0)) {
                uint64_t t17 = GM_LD(uint64_t, (((uint8_t *)&t15) + 8));
                t16 = t17;
            } else {
                t16 = ((uint64_t)0ULL);
            }
            gm_l4 = t16;
        }
        {
            gm_tm98ap4112hee t18 = gm_l2;
            gm_node *pn19 = gm_find(cx, 2126215853);
            if (!pn19) gm_trap("no handler installed for effect `Fs`", gm_srcqr4ch56kv6bv, 480, 9);
            gm_b0_1 t20;
            t20 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, gm_tm98ap4112hee))pn19->clauses[1])(pn19->env, pn19->hctx, pn19, t18);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            uint64_t t22 = gm_l4;
            uint8_t t23 = (t22 > ((uint64_t)0ULL));
            if (!t23) goto ie21;
            void * t24 = ((uint8_t *)&F.l3);
            uint64_t t25 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t26 = gm_l4;
            uint64_t t27 = (uintptr_t)t26;
            if (t25 > t27 || t27 > (uintptr_t)4096) gm_trap_slice(t25, t27, 4096, gm_srcqr4ch56kv6bv, 481, 46);
            gm_b16_8 t28;
            GM_ST(void *, (uint8_t *)&t28, (void *)(((uint8_t *)t24 + t25 * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(t27 - t25));
            gm_b16_8 t29 = t28;
            gm_b16_8 t30 = GM_LD(gm_b16_8, ((uint8_t *)&t29));
            gm_b16_8 t31 = GM_LD(gm_b16_8, gm_cl1ove1i7cb);
            uint8_t t32 = gm_f_contains_gkki4kq(t30, t31);
            uint8_t t33 = (uint8_t)!t32;
            if (!t33) goto ie21;
            {
                gm_b16_8 t35 = gm_l0;
                gm_b16_8 t36 = t35;
                if (gm_pfu8dq5bikbh(cx, &gm_ty0iqiubfm7e, ((uint8_t *)&t36), gm_srcqr4ch56kv6bv, 482, 13)) {
                    goto gm_lad;
                }
                if (gm_pf454qfrpt6r(cx, gm_s5g0cpgo64peg, 103, gm_srcqr4ch56kv6bv, 482, 13)) {
                    goto gm_lad;
                }
            }
            {
                uint8_t t37 = 0;
                F.ret = ((uint8_t)0);
                goto gm_fret;
            }
            goto ix21;
ie21:;
ix21:;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        gm_b16_8 t38 = gm_l0;
        gm_b16_8 t39 = gm_l1;
        gm_ttln0d0hd70en t40 = gm_f_write_all_o8ucaji((void *)NULL, cx, NULL, t38, t39);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_ttln0d0hd70en t41 = t40;
        gm_b0_1 t42;
        if (!((((uint8_t *)&t41))[0] != 0)) {
            gm_b0_1 t43 = GM_LD(gm_b0_1, (((uint8_t *)&t41) + 4));
            t42 = t43;
        } else {
            {
                gm_b16_8 t45 = gm_l0;
                gm_b16_8 t46 = t45;
                if (gm_pf454qfrpt6r(cx, gm_snekrjln06tfl, 13, gm_srcqr4ch56kv6bv, 487, 9)) {
                    goto gm_lad;
                }
                if (gm_pfu8dq5bikbh(cx, &gm_ty0iqiubfm7e, ((uint8_t *)&t46), gm_srcqr4ch56kv6bv, 487, 9)) {
                    goto gm_lad;
                }
                if (gm_pf454qfrpt6r(cx, gm_so514oj5nbhcu, 1, gm_srcqr4ch56kv6bv, 487, 9)) {
                    goto gm_lad;
                }
            }
            uint8_t t47 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
    }
    {
        uint8_t t48 = 0;
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
inline __attribute__((aligned(4))) uint8_t gm_f_write_units_hc5dqp8(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_b16_8 a2) {
    struct gm_fr_write_units_hc5dqp8 F;
    void * gm_l0; /* b */
    gm_t6j6smj7qoctu gm_l1; /* split */
    gm_b16_8 gm_l2; /* exe */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    uint64_t gm_l5; /* i */
    gm_b16_8 gm_l6; /* u */
    GM_STACK_CHECK(&F, 618u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&gm_l1, a1, sizeof gm_l1);
    gm_l2 = a2;
    {
        gm_b16_8 t3 = gm_l2;
        gm_tnpifv20irnph t4;
        GM_ST(gm_b16_8, ((uint8_t *)&t4 + 0), t3);
        gm_tabb710d9mq2e t5;
        gm_f_format_7os4s22((void *)&t5, (void *)NULL, cx, NULL, t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t6 = t5;
        if (((((uint8_t *)&t6))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t6) + 8, gm_srcqr4ch56kv6bv, 221, 30);
        gm_b16_8 t7 = GM_LD(gm_b16_8, (((uint8_t *)&t6) + 8));
        gm_b16_8 t8 = t7;
        gm_b16_8 t9 = GM_LD(gm_b16_8, ((uint8_t *)&t8));
        void * t10 = gm_l0;
        gm_b16_8 t11 = gm_f_runtime_source_skilk06((void *)NULL, cx, NULL, t10);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t12 = gm_f_write_generated_c_kb09043((void *)NULL, cx, NULL, t9, t11);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t13 = (uint8_t)!t12;
        if (!t13) goto ie2;
        {
            uint8_t t15 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        {
            void * t17 = gm_l0;
            void * t18 = (void *)((uint8_t *)&gm_l1);
            gm_trqfvmjtt4sij t19;
            gm_f_unit_sources_329det7((void *)&t19, (void *)NULL, cx, NULL, t17, t18);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_trqfvmjtt4sij t20 = t19;
            gm_b16_8 t21 = GM_LD(gm_b16_8, (((uint8_t *)&t20) + 0));
            gm_b16_8 t22 = t21;
            gm_b16_8 t23 = GM_LD(gm_b16_8, ((uint8_t *)&t22));
            gm_l3 = t23;
        }
        {
            gm_l4 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t25 = gm_l4;
                uint64_t t26 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                uint8_t t27 = (t25 < t26);
                if (!t27) goto lb24;
                {
                    gm_tf1bjg81u0glv t30;
                    uint64_t t31 = gm_l4;
                    GM_ST(uint64_t, (uint8_t *)&t30 + 0, t31);
                    uint64_t t32 = gm_l4;
                    uint64_t t33 = (uintptr_t)t32;
                    uint64_t t34 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                    gm_b16_8 t35 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t33 * 16));
                    GM_ST(gm_b16_8, (uint8_t *)&t30 + 8, t35);
                    gm_tf1bjg81u0glv t36 = t30;
                    uint64_t t39 = GM_LD(uint64_t, (((uint8_t *)&t36) + 0));
                    gm_l5 = t39;
                    gm_b16_8 t40 = GM_LD(gm_b16_8, (((uint8_t *)&t36) + 8));
                    gm_l6 = t40;
                    {
                        gm_b16_8 t43 = gm_l2;
                        uint64_t t44 = gm_l5;
                        gm_b16_8 t45 = gm_f_unit_path_3tdv4sg((void *)NULL, cx, NULL, t43, t44);
                        if (gm_unw.kind) {
                            goto gm_ll28;
                        }
                        gm_b16_8 t46 = gm_l6;
                        uint8_t t47 = gm_f_write_generated_c_kb09043((void *)NULL, cx, NULL, t45, t46);
                        if (gm_unw.kind) {
                            goto gm_ll28;
                        }
                        uint8_t t48 = (uint8_t)!t47;
                        if (!t48) goto ie42;
                        {
                            uint8_t t50 = 0;
                            F.ret = ((uint8_t)0);
                            goto gm_fret;
                        }
                        goto ix42;
ie42:;
ix42:;
                    }
                    goto me37;
mn38:;
                    gm_trap("no match arm matched the scrutinee", gm_srcqr4ch56kv6bv, 222, 5);
me37:;
                }
lc24:;
                {
                    uint64_t t52 = gm_l4;
                    uint64_t r53; bool o54 = __builtin_add_overflow(t52, ((uint64_t)1ULL), &r53);
                    uint64_t t55 = r53;
                    gm_l4 = t55;
                }
            }
gm_ll28:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb24; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc24; }
            }
            goto gm_lad;
lb24:;
        }
    }
    {
        uint8_t t56 = 0;
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
