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
typedef struct { uint8_t a[8]; } gm_t2doi72kmmlil;
typedef struct { uint64_t f0; uint64_t f1; } gm_t2lqu8jsj511d;
typedef struct { uint64_t f0; } gm_t4ce0jd3tdro4;
typedef struct { uint64_t f0; uint64_t f1; uint64_t f2; uint64_t f3; } gm_t66j1im9ip128;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint32_t f0; uint32_t f1; } gm_tcc1rq4n1avb2;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint8_t f0; uint8_t p1[1]; uint16_t f1; } gm_tjcj4j2gbh6c8;
typedef struct { uint64_t f0; uint64_t f1; uint64_t f2; } gm_tkpb4kplftgaa;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { uint64_t f0; uint32_t f1; uint8_t pe[4]; } gm_tn68el8d14vku;
typedef struct { uint8_t f0; uint8_t p1[3]; uint32_t f1; } gm_ttovhfee8mf28;
_Static_assert(sizeof(gm_csguankko4lmhd) == 4 && _Alignof(gm_csguankko4lmhd) == 4 && offsetof(gm_csguankko4lmhd, f0) == 0, "layout of `Mutex`");
_Static_assert(sizeof(gm_t2doi72kmmlil) == 8 && _Alignof(gm_t2doi72kmmlil) == 1 && offsetof(gm_t2doi72kmmlil, a) == 0, "layout of `[8]u8`");
_Static_assert(sizeof(gm_t2lqu8jsj511d) == 16 && _Alignof(gm_t2lqu8jsj511d) == 8 && offsetof(gm_t2lqu8jsj511d, f0) == 0 && offsetof(gm_t2lqu8jsj511d, f1) == 8, "layout of `(u64, u64)`");
_Static_assert(sizeof(gm_t4ce0jd3tdro4) == 8 && _Alignof(gm_t4ce0jd3tdro4) == 8 && offsetof(gm_t4ce0jd3tdro4, f0) == 0, "layout of `Instant`");
typedef struct { uint64_t f0; gm_t9qkh0rsooc8d f1; uint8_t pe[7]; } gm_t617vr72bd7b3;
_Static_assert(sizeof(gm_t66j1im9ip128) == 32 && _Alignof(gm_t66j1im9ip128) == 8 && offsetof(gm_t66j1im9ip128, f0) == 0 && offsetof(gm_t66j1im9ip128, f1) == 8 && offsetof(gm_t66j1im9ip128, f2) == 16 && offsetof(gm_t66j1im9ip128, f3) == 24, "layout of `(types: usize, values: usize, fns: usize, consts: usize)`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tn68el8d14vku v0; gm_tn68el8d14vku v1; gm_tf5srh4mp4b3i v2; uint8_t v3; void * v4; uint32_t v5; } f1; } gm_t76123gfet73g;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
typedef struct { uint64_t f0; uint64_t f1; gm_b16_8 f2; } gm_tbccvikg6b96e;
typedef struct { uint8_t f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; } gm_tbngattl1fo51;
_Static_assert(sizeof(gm_tcc1rq4n1avb2) == 8 && _Alignof(gm_tcc1rq4n1avb2) == 4 && offsetof(gm_tcc1rq4n1avb2, f0) == 0 && offsetof(gm_tcc1rq4n1avb2, f1) == 4, "layout of `LineCol`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tcmkompbl09il;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; gm_t88rcucg1b7uk f2; } gm_tcmtrmhj7ak4u;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_ttovhfee8mf28 f0; uint8_t f1; uint8_t pe[3]; } gm_tg5lql6itufk7;
typedef struct { gm_b16_8 f0; uint64_t f1; uint64_t f2; } gm_tgbgti82mf879;
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; } gm_tgglga8ro3isc;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
typedef struct { uint64_t f0; uint64_t f1; gm_t9qkh0rsooc8d f2; uint8_t f3; uint8_t pe[6]; } gm_tilak255t5m34;
_Static_assert(sizeof(gm_tjcj4j2gbh6c8) == 4 && _Alignof(gm_tjcj4j2gbh6c8) == 2 && offsetof(gm_tjcj4j2gbh6c8, f0) == 0 && offsetof(gm_tjcj4j2gbh6c8, f1) == 2, "layout of `(signed: bool, bits: u16)`");
_Static_assert(sizeof(gm_tkpb4kplftgaa) == 24 && _Alignof(gm_tkpb4kplftgaa) == 8 && offsetof(gm_tkpb4kplftgaa, f0) == 0 && offsetof(gm_tkpb4kplftgaa, f1) == 8 && offsetof(gm_tkpb4kplftgaa, f2) == 16, "layout of `(u64, u64, u64)`");
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_tn68el8d14vku) == 16 && _Alignof(gm_tn68el8d14vku) == 8 && offsetof(gm_tn68el8d14vku, f0) == 0 && offsetof(gm_tn68el8d14vku, f1) == 8, "layout of `(i: usize, x: u32)`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t88rcucg1b7uk v0; } f1; } gm_tnnjeerolos0a;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
_Static_assert(sizeof(gm_ttovhfee8mf28) == 8 && _Alignof(gm_ttovhfee8mf28) == 4 && offsetof(gm_ttovhfee8mf28, f0) == 0 && offsetof(gm_ttovhfee8mf28, f1) == 4, "layout of `(signed: bool, bits: u32)`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_tcc1rq4n1avb2 v3; uint32_t v4; uint32_t v5; } f1; } gm_tvdg94bk7aa22;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t76123gfet73g v0; uint8_t v1; } f1; } gm_t1ec8br9ddkav;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tcmkompbl09il v0; gm_tiilmfd16v3fc v1; gm_b16_8 v2; } f1; } gm_t428ue2u3hf5g;
_Static_assert(sizeof(gm_t617vr72bd7b3) == 16 && _Alignof(gm_t617vr72bd7b3) == 8 && offsetof(gm_t617vr72bd7b3, f0) == 0 && offsetof(gm_t617vr72bd7b3, f1) == 8, "layout of `DirEntry`");
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_t76123gfet73g) == 24 && _Alignof(gm_t76123gfet73g) == 8 && offsetof(gm_t76123gfet73g, f0) == 0 && offsetof(gm_t76123gfet73g, f1) == 8, "layout of `Flow`");
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; gm_tnnjeerolos0a f2; uint8_t pe[4]; } gm_t8nckeqfenl8a;
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
_Static_assert(sizeof(gm_tbccvikg6b96e) == 32 && _Alignof(gm_tbccvikg6b96e) == 8 && offsetof(gm_tbccvikg6b96e, f0) == 0 && offsetof(gm_tbccvikg6b96e, f1) == 8 && offsetof(gm_tbccvikg6b96e, f2) == 16, "layout of `Layout`");
_Static_assert(sizeof(gm_tbngattl1fo51) == 8 && _Alignof(gm_tbngattl1fo51) == 4 && offsetof(gm_tbngattl1fo51, f0) == 0 && offsetof(gm_tbngattl1fo51, f1) == 4, "layout of `(is_const: bool, pointee: Ty)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t76123gfet73g v0; } f1; } gm_tcfa6l7reoosn;
_Static_assert(sizeof(gm_tcmkompbl09il) == 24 && _Alignof(gm_tcmkompbl09il) == 8 && offsetof(gm_tcmkompbl09il, f0) == 0 && offsetof(gm_tcmkompbl09il, f1) == 8, "layout of `(ty: Ty, bytes: string)`");
_Static_assert(sizeof(gm_tcmtrmhj7ak4u) == 12 && _Alignof(gm_tcmtrmhj7ak4u) == 4 && offsetof(gm_tcmtrmhj7ak4u, f0) == 0 && offsetof(gm_tcmtrmhj7ak4u, f1) == 4 && offsetof(gm_tcmtrmhj7ak4u, f2) == 8, "layout of `(op: LenOp, lhs: Ty, rhs: Ty)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t76123gfet73g v0; gm_b16_8 v1; } f1; } gm_td0ept12e7q8b;
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t p2[4]; gm_tnpifv20irnph f2; uint8_t f3; uint8_t pe[7]; } gm_tddtjfggdat0d;
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_tg5lql6itufk7) == 12 && _Alignof(gm_tg5lql6itufk7) == 4 && offsetof(gm_tg5lql6itufk7, f0) == 0 && offsetof(gm_tg5lql6itufk7, f1) == 8, "layout of `?(signed: bool, bits: u32)`");
_Static_assert(sizeof(gm_tgbgti82mf879) == 32 && _Alignof(gm_tgbgti82mf879) == 8 && offsetof(gm_tgbgti82mf879, f0) == 0 && offsetof(gm_tgbgti82mf879, f1) == 16 && offsetof(gm_tgbgti82mf879, f2) == 24, "layout of `PayloadUnion`");
_Static_assert(sizeof(gm_tgglga8ro3isc) == 8 && _Alignof(gm_tgglga8ro3isc) == 4 && offsetof(gm_tgglga8ro3isc, f0) == 0 && offsetof(gm_tgglga8ro3isc, f1) == 4, "layout of `(err: Ty, ok: Ty)`");
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
_Static_assert(sizeof(gm_tilak255t5m34) == 24 && _Alignof(gm_tilak255t5m34) == 8 && offsetof(gm_tilak255t5m34, f0) == 0 && offsetof(gm_tilak255t5m34, f1) == 8 && offsetof(gm_tilak255t5m34, f2) == 16 && offsetof(gm_tilak255t5m34, f3) == 17, "layout of `(base: u64, size: u64, kind: AllocKind, live: bool)`");
_Static_assert(sizeof(gm_tnnjeerolos0a) == 8 && _Alignof(gm_tnnjeerolos0a) == 4 && offsetof(gm_tnnjeerolos0a, f0) == 0 && offsetof(gm_tnnjeerolos0a, f1) == 4, "layout of `ArrayKind`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t2lqu8jsj511d v0; gm_t617vr72bd7b3 v1; gm_t66j1im9ip128 v2; gm_t2lqu8jsj511d v3; uint64_t v4; gm_t617vr72bd7b3 v5; } f1; } gm_tpekhfs32op0f;
typedef struct { gm_tilak255t5m34 f0; uint8_t f1; uint8_t pe[7]; } gm_tqvpud5umqqp1;
typedef struct { gm_tbccvikg6b96e f0; uint8_t f1; uint8_t pe[7]; } gm_ttfi3b3ccbpes;
_Static_assert(sizeof(gm_tvdg94bk7aa22) == 12 && _Alignof(gm_tvdg94bk7aa22) == 4 && offsetof(gm_tvdg94bk7aa22, f0) == 0 && offsetof(gm_tvdg94bk7aa22, f1) == 4, "layout of `Builtin`");
_Static_assert(sizeof(gm_t1ec8br9ddkav) == 32 && _Alignof(gm_t1ec8br9ddkav) == 8 && offsetof(gm_t1ec8br9ddkav, f0) == 0 && offsetof(gm_t1ec8br9ddkav, f1) == 8, "layout of `Flow!bool`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tpekhfs32op0f v0; uint64_t v1; } f1; } gm_t3j6egd6cfnk5;
_Static_assert(sizeof(gm_t428ue2u3hf5g) == 32 && _Alignof(gm_t428ue2u3hf5g) == 8 && offsetof(gm_t428ue2u3hf5g, f0) == 0 && offsetof(gm_t428ue2u3hf5g, f1) == 8, "layout of `ConstKey`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tjcj4j2gbh6c8 v0; uint16_t v1; gm_tbngattl1fo51 v2; gm_tbngattl1fo51 v3; gm_tbngattl1fo51 v4; gm_t8nckeqfenl8a v5; gm_t6j6smj7qoctu v6; gm_t88rcucg1b7uk v7; gm_tgglga8ro3isc v8; gm_tddtjfggdat0d v9; gm_tddtjfggdat0d v10; gm_tiilmfd16v3fc v11; gm_t88rcucg1b7uk v12; uint32_t v13; gm_tgglga8ro3isc v14; gm_t428ue2u3hf5g v15; gm_tcmtrmhj7ak4u v16; uint32_t v17; gm_tnpifv20irnph v18; uint32_t v19; } f1; } gm_t53at80pfdus3;
_Static_assert(sizeof(gm_t8nckeqfenl8a) == 24 && _Alignof(gm_t8nckeqfenl8a) == 8 && offsetof(gm_t8nckeqfenl8a, f0) == 0 && offsetof(gm_t8nckeqfenl8a, f1) == 8 && offsetof(gm_t8nckeqfenl8a, f2) == 12, "layout of `(len: u64, elem: Ty, kind: ArrayKind)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tpekhfs32op0f v0; gm_b16_8 v1; } f1; } gm_t934k1j7u70ct;
_Static_assert(sizeof(gm_tcfa6l7reoosn) == 32 && _Alignof(gm_tcfa6l7reoosn) == 8 && offsetof(gm_tcfa6l7reoosn, f0) == 0 && offsetof(gm_tcfa6l7reoosn, f1) == 8, "layout of `Flow!()`");
_Static_assert(sizeof(gm_td0ept12e7q8b) == 32 && _Alignof(gm_td0ept12e7q8b) == 8 && offsetof(gm_td0ept12e7q8b, f0) == 0 && offsetof(gm_td0ept12e7q8b, f1) == 8, "layout of `Flow!string`");
_Static_assert(sizeof(gm_tddtjfggdat0d) == 48 && _Alignof(gm_tddtjfggdat0d) == 8 && offsetof(gm_tddtjfggdat0d, f0) == 0 && offsetof(gm_tddtjfggdat0d, f1) == 16 && offsetof(gm_tddtjfggdat0d, f2) == 24 && offsetof(gm_tddtjfggdat0d, f3) == 40, "layout of `FnTy`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tpekhfs32op0f v0; } f1; } gm_tgfdnufetdnfc;
_Static_assert(sizeof(gm_tpekhfs32op0f) == 40 && _Alignof(gm_tpekhfs32op0f) == 8 && offsetof(gm_tpekhfs32op0f, f0) == 0 && offsetof(gm_tpekhfs32op0f, f1) == 8, "layout of `MemError`");
_Static_assert(sizeof(gm_tqvpud5umqqp1) == 32 && _Alignof(gm_tqvpud5umqqp1) == 8 && offsetof(gm_tqvpud5umqqp1, f0) == 0 && offsetof(gm_tqvpud5umqqp1, f1) == 24, "layout of `?(base: u64, size: u64, kind: AllocKind, live: bool)`");
_Static_assert(sizeof(gm_ttfi3b3ccbpes) == 40 && _Alignof(gm_ttfi3b3ccbpes) == 8 && offsetof(gm_ttfi3b3ccbpes, f0) == 0 && offsetof(gm_ttfi3b3ccbpes, f1) == 32, "layout of `?AllocTag`");
_Static_assert(sizeof(gm_t3j6egd6cfnk5) == 48 && _Alignof(gm_t3j6egd6cfnk5) == 8 && offsetof(gm_t3j6egd6cfnk5, f0) == 0 && offsetof(gm_t3j6egd6cfnk5, f1) == 8, "layout of `MemError!u64`");
_Static_assert(sizeof(gm_t53at80pfdus3) == 56 && _Alignof(gm_t53at80pfdus3) == 8 && offsetof(gm_t53at80pfdus3, f0) == 0 && offsetof(gm_t53at80pfdus3, f1) == 8, "layout of `TyKind`");
_Static_assert(sizeof(gm_t934k1j7u70ct) == 48 && _Alignof(gm_t934k1j7u70ct) == 8 && offsetof(gm_t934k1j7u70ct, f0) == 0 && offsetof(gm_t934k1j7u70ct, f1) == 8, "layout of `MemError![]u8`");
_Static_assert(sizeof(gm_tgfdnufetdnfc) == 48 && _Alignof(gm_tgfdnufetdnfc) == 8 && offsetof(gm_tgfdnufetdnfc, f0) == 0 && offsetof(gm_tgfdnufetdnfc, f1) == 8, "layout of `MemError!()`");
struct gm_fr_align_15fjgq1 {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_align_of_k6eqqrn {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_aligned_q6sor4h {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_copy_from_f7j6p05 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_empty_slice_ijtp9lo {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_fast_23em7sv {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_fill_abmf5g5 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_get_u64_tgpb2vh {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_kind_htv6093 {
    gm_frame h;
    gm_t53at80pfdus3 ret;
};
struct gm_fr_kind_jcnt4hm {
    gm_frame h;
    gm_t53at80pfdus3 ret;
};
struct gm_fr_mem_trap_ecogko0 {
    gm_frame h;
    gm_tpekhfs32op0f l2; /* e */
    gm_t76123gfet73g ret;
};
struct gm_fr_memory_builtin_9s47dk8 {
    gm_frame h;
    gm_tpekhfs32op0f l36; /* e */
    gm_tpekhfs32op0f l39; /* e */
    gm_tpekhfs32op0f l42; /* e */
    gm_tcfa6l7reoosn ret;
};
struct gm_fr_ptr_of_b77b4c9 {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_put_ptr_188p3mm {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_put_u64_irp77r2 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_rd_word_8flcq6o {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_read_u6r4vns {
    gm_frame h;
    gm_t934k1j7u70ct ret;
};
struct gm_fr_tmp_8k6u9c9 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_us_cuv01fp {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_view_qalkbv1 {
    gm_frame h;
    void * ret;
};
struct gm_fr_wr_word_npte9fs {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_write_view_pebit6h {
    gm_frame h;
    gm_t934k1j7u70ct ret;
};
inline __attribute__((aligned(4))) uint64_t gm_f_align_15fjgq1(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) uint64_t gm_f_align_of_k6eqqrn(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline uint8_t gm_f_aligned_q6sor4h(uint64_t a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_alloc_bfo8187(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, uint64_t a2, gm_t9qkh0rsooc8d a3, uint8_t a4);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_copy_from_f7j6p05(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_describe_t62jhnt(void *gm_out, void * a0, uint64_t a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
__attribute__((aligned(4))) void gm_f_equal_e5vf82h(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3, gm_b16_8 a4);
inline __attribute__((aligned(4))) uint8_t gm_f_fast_23em7sv(void * a0);
__attribute__((aligned(4))) void gm_f_fault_u31b92l(void *gm_out, void * a0, uint64_t a1, uint64_t a2, uint64_t a3, uint8_t a4);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_fill_abmf5g5(gm_b16_8 a0, uint8_t a1);
__attribute__((aligned(4))) void gm_f_format_37g0gks(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_bv13gdl(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_d613p2h(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_e7344u2(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t2lqu8jsj511d a0);
__attribute__((aligned(4))) void gm_f_format_jc3ftg7(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
__attribute__((aligned(4))) void gm_f_format_p0ej7hu(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_u3ioj90(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
__attribute__((aligned(4))) void gm_f_get_hd4d6j0(void *gm_out, void * a0, uint64_t a1);
__attribute__((aligned(4))) unsigned __int128 gm_f_get_int_pg7p4gc(void * a0, gm_csguankko4lmhd a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) uint64_t gm_f_get_u64_tgpb2vh(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) unsigned __int128 gm_f_get_uint_bq8c4b7(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tg5lql6itufk7 gm_f_int_info_ulcevdv(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) uint8_t gm_f_kill_3kcavfd(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
inline __attribute__((aligned(4))) void gm_f_kind_htv6093(void *gm_out, void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void gm_f_kind_jcnt4hm(void *gm_out, void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void gm_f_layout_a5ovnrm(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_load_at_6u81jcc(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, uint64_t a2, uint64_t a3, gm_b16_8 a4);
__attribute__((aligned(4))) gm_b0_1 gm_f_make_some_tvb9ubk(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, gm_b16_8 a3);
__attribute__((aligned(4))) gm_b16_8 gm_f_mem_message_e0go901(void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_mem_trap_ecogko0(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, const void * a2);
__attribute__((aligned(4))) void gm_f_memory_builtin_9s47dk8(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_tvdg94bk7aa22 a2, gm_b16_8 a3, gm_b16_8 a4, gm_t88rcucg1b7uk a5, gm_b16_8 a6);
inline __attribute__((aligned(4))) uint64_t gm_f_ptr_of_b77b4c9(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_64ej4om(void * a0, uint64_t a1, const void * a2);
inline __attribute__((aligned(4))) void gm_f_put_ptr_188p3mm(void * a0, gm_b16_8 a1, uint64_t a2);
inline __attribute__((aligned(4))) void gm_f_put_u64_irp77r2(void * a0, gm_b16_8 a1, uint64_t a2);
__attribute__((aligned(4))) void gm_f_put_uint_cddin99(void * a0, gm_b16_8 a1, unsigned __int128 a2);
__attribute__((aligned(4))) __attribute__((always_inline)) inline uint64_t gm_f_rd_word_8flcq6o(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_read_string_ttuk4gr(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_read_u6r4vns(void *gm_out, void * a0, uint64_t a1, uint64_t a2, uint64_t a3);
__attribute__((aligned(4))) void gm_f_remove_gjirbtf(void *gm_out, void * a0, uint64_t a1);
__attribute__((aligned(4))) uint64_t gm_f_size_hr4b76n(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_t2lqu8jsj511d gm_f_slice_parts_1se5hu4(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_store_at_ktabiji(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, uint64_t a2, uint64_t a3, gm_b16_8 a4);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_b16_8 gm_f_tmp_8k6u9c9(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
__attribute__((aligned(4))) __attribute__((noinline)) gm_b16_8 gm_f_tmp_next_dalgo1f(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_trap_qh84l9d(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) __attribute__((always_inline)) inline uint64_t gm_f_us_cuv01fp(uint64_t a0);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void * gm_f_view_qalkbv1(void * a0, uint64_t a1, uint64_t a2, uint8_t a3);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_wr_word_npte9fs(void * a0, gm_b16_8 a1, uint64_t a2);
__attribute__((aligned(4))) void gm_f_write_5gub0ul(void *gm_out, void * a0, uint64_t a1, gm_b16_8 a2, uint64_t a3);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_write_view_pebit6h(void *gm_out, void * a0, uint64_t a1, uint64_t a2, uint64_t a3);
extern uint8_t gm_c6p4chcutca[8] __attribute__((aligned(8)));
extern uint8_t gm_ckh5e429gka[8] __attribute__((aligned(8)));
extern const uint8_t gm_s0ugop4l95o1j[6];
extern const uint8_t gm_s1gjt02j7um1i[37];
extern const uint8_t gm_s2qp8mqi0c2le[48];
extern const uint8_t gm_s8pst1ovjhtbk[71];
extern const uint8_t gm_sp0ag7curvl4o[42];
extern const uint8_t gm_sqfrv3g5v9m2n[5];
extern const uint8_t gm_sqofr58g480ri[39];
extern const char gm_src0ih77f43mhf2[];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_srcc24pnihhgkug[];
extern const char gm_srccp2mbkjs1vn4[];
extern const char gm_srcnqtpfma6qe4a[];
extern const char gm_srcpit8mdn0781q[];
extern const char gm_srcq7ohh8h77bn7[];
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
    GM_STACK_CHECK(&F, 88u);
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
    uint64_t t5 = GM_LD(uint64_t, (((uint8_t *)&t4) + 8));
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline uint8_t gm_f_aligned_q6sor4h(uint64_t a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_aligned_q6sor4h F;
    uint64_t gm_l0; /* addr */
    uint64_t gm_l1; /* align */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint64_t t3 = gm_l1;
        uint8_t t4 = (t3 <= ((uint64_t)1ULL));
        if (!t4) goto ie2;
        {
            uint8_t t6 = 0;
            F.ret = ((uint8_t)1);
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        uint64_t t8 = gm_l1;
        uint64_t t9 = gm_l1;
        uint64_t r10; bool o11 = __builtin_sub_overflow(t9, ((uint64_t)1ULL), &r10);
        if (o11) gm_trap("integer overflow in `-`", gm_srcq7ohh8h77bn7, 197, 17);
        uint64_t t12 = r10;
        uint64_t t13 = (uint64_t)(t8 & t12);
        uint8_t t14 = ((t13 == ((uint64_t)0ULL)));
        if (!t14) goto ie7;
        {
            uint64_t t16 = gm_l0;
            uint64_t t17 = gm_l1;
            uint64_t r18; bool o19 = __builtin_sub_overflow(t17, ((uint64_t)1ULL), &r18);
            if (o19) gm_trap("integer overflow in `-`", gm_srcq7ohh8h77bn7, 197, 50);
            uint64_t t20 = r18;
            uint64_t t21 = (uint64_t)(t16 & t20);
            uint8_t t22 = ((t21 == ((uint64_t)0ULL)));
            uint8_t t23 = 0;
            F.ret = t22;
            goto gm_fret;
        }
        goto ix7;
ie7:;
ix7:;
    }
    {
        uint64_t t24 = gm_l0;
        uint64_t t25 = gm_l1;
        if (t25 == 0) gm_trap("division by zero", gm_srcq7ohh8h77bn7, 198, 12);
        uint64_t t26 = (uint64_t)(t24 % t25);
        uint8_t t27 = ((t26 == ((uint64_t)0ULL)));
        uint8_t t28 = 0;
        F.ret = t27;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_copy_from_f7j6p05(gm_b16_8 a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_copy_from_f7j6p05 F;
    gm_b16_8 gm_l0; /* s */
    gm_b16_8 gm_l1; /* src */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint64_t t2 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        uint64_t t3 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint8_t t4 = ((t2 == t3));
        if (!t4) {
            gm_b16_8 t5;
            GM_ST(void *, (uint8_t *)&t5, (void *)(gm_sqofr58g480ri));
            GM_ST(uintptr_t, (uint8_t *)&t5 + 8, (uintptr_t)(38));
            gm_buf mb = {0}; size_t ml = GM_LD(uintptr_t, ((uint8_t *)&t5) + 8); const uint8_t *mp = GM_LD(void *, ((uint8_t *)&t5)); if (ml == 16 && memcmp(mp, "assertion failed", 16) == 0) gm_buf_push(&mb, mp, ml); else { gm_buf_push(&mb, "assertion failed: ", 18); gm_buf_push(&mb, mp, ml); } gm_trap_bytes(mb.data, mb.len, gm_srcc24pnihhgkug, 24, 5);
        }
    }
    {
        gm_b16_8 t6 = gm_l0;
        gm_b16_8 t7 = gm_l1;
        if (GM_LD(uintptr_t, ((uint8_t *)&t6) + 8) != 0) memmove(GM_LD(void *, ((uint8_t *)&t6)), GM_LD(void *, ((uint8_t *)&t7)), GM_LD(uintptr_t, ((uint8_t *)&t6) + 8) * 1);
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
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
inline __attribute__((aligned(4))) uint8_t gm_f_fast_23em7sv(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_fast_23em7sv F;
    void * gm_l0; /* m */
    GM_STACK_CHECK_SHALLOW(&F, 19u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    uint8_t t2 = ((GM_LD(uint8_t, (((uint8_t *)t1) + 88) + 0) == ((uint8_t)3ULL)));
    F.ret = t2;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_fill_abmf5g5(gm_b16_8 a0, uint8_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_fill_abmf5g5 F;
    gm_b16_8 gm_l0; /* s */
    uint8_t gm_l1; /* x */
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
                    uint64_t t14 = GM_LD(uint64_t, ((uint8_t *)&t11));
                    gm_l5 = t14;
                    {
                        uint64_t t16 = gm_l5;
                        uint64_t t17 = (uintptr_t)t16;
                        void * t18 = gm_l2;
                        uint8_t t19 = gm_l1;
                        GM_ST(uint8_t, ((uint8_t *)t18 + t17 * 1), t19);
                    }
                    goto me12;
mn13:;
                    gm_trap("no match arm matched the scrutinee", gm_srcc24pnihhgkug, 18, 5);
me12:;
                }
lc5:;
                {
                    uint64_t t21 = gm_l4;
                    uint64_t r22; bool o23 = __builtin_add_overflow(t21, ((uint64_t)1ULL), &r22);
                    uint64_t t24 = r22;
                    gm_l4 = t24;
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
inline __attribute__((aligned(4))) uint64_t gm_f_get_u64_tgpb2vh(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_get_u64_tgpb2vh F;
    void * gm_l0; /* m */
    gm_b16_8 gm_l1; /* b */
    GM_STACK_CHECK_SHALLOW(&F, 153u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint64_t t1;
    uint64_t t3 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
    uint8_t t4 = (t3 <= ((uint64_t)8ULL));
    if (!t4) goto ie2;
    void * t6 = gm_l0;
    gm_b16_8 t7 = gm_l1;
    uint64_t t8 = gm_f_rd_word_8flcq6o(t6, t7);
    uint64_t t9 = t8;
    t1 = t9;
    goto ix2;
ie2:;
    void * t11 = gm_l0;
    gm_b16_8 t12 = gm_l1;
    unsigned __int128 t13 = gm_f_get_uint_bq8c4b7(t11, t12);
    uint64_t t14 = (uint64_t)t13;
    uint64_t t15 = t14;
    t1 = t15;
ix2:;
    F.ret = t1;
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
inline __attribute__((aligned(4))) void gm_f_mem_trap_ecogko0(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, const void * a2) {
    struct gm_fr_mem_trap_ecogko0 F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    GM_STACK_CHECK(&F, 56u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    gm_tl874t2atnlh3 t2 = gm_l1;
    gm_b16_8 t3 = gm_f_mem_message_e0go901((void *)NULL, cx, NULL, &(*(gm_tpekhfs32op0f *)((uint8_t *)a2)));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_f_trap_qh84l9d((void *)&(*(gm_t76123gfet73g *)gm_out), (void *)NULL, cx, NULL, t1, t2, t3);
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
inline __attribute__((aligned(4))) uint64_t gm_f_ptr_of_b77b4c9(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_ptr_of_b77b4c9 F;
    void * gm_l0; /* m */
    gm_b16_8 gm_l1; /* b */
    GM_STACK_CHECK(&F, 144u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    gm_b16_8 t2 = gm_l1;
    uint64_t t3 = GM_LD(uintptr_t, ((uint8_t *)&t2) + 8);
    void * t4 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t2)));
    uint64_t t5 = (uintptr_t)((uint64_t)0ULL);
    void * t6 = gm_l0;
    uint64_t t7 = GM_LD(uint64_t, (((uint8_t *)t6) + 176));
    uint64_t t8 = gm_f_us_cuv01fp(t7);
    uint64_t t9 = (uintptr_t)t8;
    if (t5 > t9 || t9 > (uintptr_t)t3) gm_trap_slice(t5, t9, t3, gm_srcnqtpfma6qe4a, 104, 68);
    gm_b16_8 t10;
    GM_ST(void *, (uint8_t *)&t10, (void *)(((uint8_t *)t4 + t5 * 1)));
    GM_ST(uintptr_t, (uint8_t *)&t10 + 8, (uintptr_t)(t9 - t5));
    uint64_t t11 = gm_f_get_u64_tgpb2vh(t1, t10);
    F.ret = t11;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_put_ptr_188p3mm(void * a0, gm_b16_8 a1, uint64_t a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_put_ptr_188p3mm F;
    void * gm_l0; /* m */
    gm_b16_8 gm_l1; /* b */
    uint64_t gm_l2; /* addr */
    GM_STACK_CHECK(&F, 136u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t1 = gm_l0;
    gm_b16_8 t2 = gm_l1;
    uint64_t t3 = GM_LD(uintptr_t, ((uint8_t *)&t2) + 8);
    void * t4 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t2)));
    uint64_t t5 = (uintptr_t)((uint64_t)0ULL);
    void * t6 = gm_l0;
    uint64_t t7 = GM_LD(uint64_t, (((uint8_t *)t6) + 176));
    uint64_t t8 = gm_f_us_cuv01fp(t7);
    uint64_t t9 = (uintptr_t)t8;
    if (t5 > t9 || t9 > (uintptr_t)t3) gm_trap_slice(t5, t9, t3, gm_srcnqtpfma6qe4a, 106, 67);
    gm_b16_8 t10;
    GM_ST(void *, (uint8_t *)&t10, (void *)(((uint8_t *)t4 + t5 * 1)));
    GM_ST(uintptr_t, (uint8_t *)&t10 + 8, (uintptr_t)(t9 - t5));
    uint64_t t11 = gm_l2;
    gm_f_put_u64_irp77r2(t1, t10, t11);
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_put_u64_irp77r2(void * a0, gm_b16_8 a1, uint64_t a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_put_u64_irp77r2 F;
    void * gm_l0; /* m */
    gm_b16_8 gm_l1; /* b */
    uint64_t gm_l2; /* v */
    GM_STACK_CHECK_SHALLOW(&F, 121u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        uint64_t t3 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint8_t t4 = (t3 <= ((uint64_t)8ULL));
        if (!t4) goto ie2;
        {
            void * t6 = gm_l0;
            gm_b16_8 t7 = gm_l1;
            uint64_t t8 = gm_l2;
            gm_f_wr_word_npte9fs(t6, t7, t8);
        }
        goto ix2;
ie2:;
        {
            void * t10 = gm_l0;
            gm_b16_8 t11 = gm_l1;
            uint64_t t12 = gm_l2;
            unsigned __int128 t13 = (unsigned __int128)t12;
            gm_f_put_uint_cddin99(t10, t11, t13);
        }
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline uint64_t gm_f_rd_word_8flcq6o(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_rd_word_8flcq6o F;
    void * gm_l0; /* m */
    gm_b16_8 gm_l1; /* b */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t3 = gm_l0;
        uint8_t t4 = GM_LD(uint8_t, (((uint8_t *)t3) + 89));
        if (!t4) goto ie2;
        {
            uint64_t t7 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            uint8_t t8 = ((t7 == ((uint64_t)8ULL)));
            if (!t8) goto ie6;
            {
                uint64_t t10 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t11 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t10 >= (uintptr_t)(t11)) gm_trap_index(t10, t11, gm_srcnqtpfma6qe4a, 316, 36);
                uint8_t t12 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t10 * 1));
                uint64_t t13 = (uint64_t)t12;
                uint64_t t14 = (uintptr_t)((uint64_t)1ULL);
                uint64_t t15 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t14 >= (uintptr_t)(t15)) gm_trap_index(t14, t15, gm_srcnqtpfma6qe4a, 316, 49);
                uint8_t t16 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t14 * 1));
                uint64_t t17 = (uint64_t)t16;
                unsigned __int128 n18 = (unsigned __int128)((uint64_t)8ULL);
                if (n18 >= 64) gm_trap_shift((uint64_t)n18, "u64", gm_srcnqtpfma6qe4a, 316, 45);
                uint64_t r19 = (uint64_t)((uint64_t)t17 << (int)n18);
                bool o20 = (uint64_t)(r19 >> (int)n18) != t17;
                if (o20) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 316, 45);
                uint64_t t21 = r19;
                uint64_t t22 = (uint64_t)(t13 | t21);
                uint64_t t23 = (uintptr_t)((uint64_t)2ULL);
                uint64_t t24 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t23 >= (uintptr_t)(t24)) gm_trap_index(t23, t24, gm_srcnqtpfma6qe4a, 316, 68);
                uint8_t t25 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t23 * 1));
                uint64_t t26 = (uint64_t)t25;
                unsigned __int128 n27 = (unsigned __int128)((uint64_t)16ULL);
                if (n27 >= 64) gm_trap_shift((uint64_t)n27, "u64", gm_srcnqtpfma6qe4a, 316, 64);
                uint64_t r28 = (uint64_t)((uint64_t)t26 << (int)n27);
                bool o29 = (uint64_t)(r28 >> (int)n27) != t26;
                if (o29) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 316, 64);
                uint64_t t30 = r28;
                uint64_t t31 = (uint64_t)(t22 | t30);
                uint64_t t32 = (uintptr_t)((uint64_t)3ULL);
                uint64_t t33 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t32 >= (uintptr_t)(t33)) gm_trap_index(t32, t33, gm_srcnqtpfma6qe4a, 316, 88);
                uint8_t t34 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t32 * 1));
                uint64_t t35 = (uint64_t)t34;
                unsigned __int128 n36 = (unsigned __int128)((uint64_t)24ULL);
                if (n36 >= 64) gm_trap_shift((uint64_t)n36, "u64", gm_srcnqtpfma6qe4a, 316, 84);
                uint64_t r37 = (uint64_t)((uint64_t)t35 << (int)n36);
                bool o38 = (uint64_t)(r37 >> (int)n36) != t35;
                if (o38) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 316, 84);
                uint64_t t39 = r37;
                uint64_t t40 = (uint64_t)(t31 | t39);
                uint64_t t41 = (uintptr_t)((uint64_t)4ULL);
                uint64_t t42 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t41 >= (uintptr_t)(t42)) gm_trap_index(t41, t42, gm_srcnqtpfma6qe4a, 316, 108);
                uint8_t t43 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t41 * 1));
                uint64_t t44 = (uint64_t)t43;
                unsigned __int128 n45 = (unsigned __int128)((uint64_t)32ULL);
                if (n45 >= 64) gm_trap_shift((uint64_t)n45, "u64", gm_srcnqtpfma6qe4a, 316, 104);
                uint64_t r46 = (uint64_t)((uint64_t)t44 << (int)n45);
                bool o47 = (uint64_t)(r46 >> (int)n45) != t44;
                if (o47) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 316, 104);
                uint64_t t48 = r46;
                uint64_t t49 = (uint64_t)(t40 | t48);
                uint64_t t50 = (uintptr_t)((uint64_t)5ULL);
                uint64_t t51 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t50 >= (uintptr_t)(t51)) gm_trap_index(t50, t51, gm_srcnqtpfma6qe4a, 316, 128);
                uint8_t t52 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t50 * 1));
                uint64_t t53 = (uint64_t)t52;
                unsigned __int128 n54 = (unsigned __int128)((uint64_t)40ULL);
                if (n54 >= 64) gm_trap_shift((uint64_t)n54, "u64", gm_srcnqtpfma6qe4a, 316, 124);
                uint64_t r55 = (uint64_t)((uint64_t)t53 << (int)n54);
                bool o56 = (uint64_t)(r55 >> (int)n54) != t53;
                if (o56) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 316, 124);
                uint64_t t57 = r55;
                uint64_t t58 = (uint64_t)(t49 | t57);
                uint64_t t59 = (uintptr_t)((uint64_t)6ULL);
                uint64_t t60 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t59 >= (uintptr_t)(t60)) gm_trap_index(t59, t60, gm_srcnqtpfma6qe4a, 316, 148);
                uint8_t t61 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t59 * 1));
                uint64_t t62 = (uint64_t)t61;
                unsigned __int128 n63 = (unsigned __int128)((uint64_t)48ULL);
                if (n63 >= 64) gm_trap_shift((uint64_t)n63, "u64", gm_srcnqtpfma6qe4a, 316, 144);
                uint64_t r64 = (uint64_t)((uint64_t)t62 << (int)n63);
                bool o65 = (uint64_t)(r64 >> (int)n63) != t62;
                if (o65) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 316, 144);
                uint64_t t66 = r64;
                uint64_t t67 = (uint64_t)(t58 | t66);
                uint64_t t68 = (uintptr_t)((uint64_t)7ULL);
                uint64_t t69 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t68 >= (uintptr_t)(t69)) gm_trap_index(t68, t69, gm_srcnqtpfma6qe4a, 316, 168);
                uint8_t t70 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t68 * 1));
                uint64_t t71 = (uint64_t)t70;
                unsigned __int128 n72 = (unsigned __int128)((uint64_t)56ULL);
                if (n72 >= 64) gm_trap_shift((uint64_t)n72, "u64", gm_srcnqtpfma6qe4a, 316, 164);
                uint64_t r73 = (uint64_t)((uint64_t)t71 << (int)n72);
                bool o74 = (uint64_t)(r73 >> (int)n72) != t71;
                if (o74) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 316, 164);
                uint64_t t75 = r73;
                uint64_t t76 = (uint64_t)(t67 | t75);
                uint8_t t77 = 0;
                F.ret = t76;
                goto gm_fret;
            }
            goto ix6;
ie6:;
ix6:;
        }
        {
            uint64_t t79 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            uint8_t t80 = ((t79 == ((uint64_t)4ULL)));
            if (!t80) goto ie78;
            {
                uint64_t t82 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t83 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t82 >= (uintptr_t)(t83)) gm_trap_index(t82, t83, gm_srcnqtpfma6qe4a, 317, 36);
                uint8_t t84 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t82 * 1));
                uint64_t t85 = (uint64_t)t84;
                uint64_t t86 = (uintptr_t)((uint64_t)1ULL);
                uint64_t t87 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t86 >= (uintptr_t)(t87)) gm_trap_index(t86, t87, gm_srcnqtpfma6qe4a, 317, 49);
                uint8_t t88 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t86 * 1));
                uint64_t t89 = (uint64_t)t88;
                unsigned __int128 n90 = (unsigned __int128)((uint64_t)8ULL);
                if (n90 >= 64) gm_trap_shift((uint64_t)n90, "u64", gm_srcnqtpfma6qe4a, 317, 45);
                uint64_t r91 = (uint64_t)((uint64_t)t89 << (int)n90);
                bool o92 = (uint64_t)(r91 >> (int)n90) != t89;
                if (o92) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 317, 45);
                uint64_t t93 = r91;
                uint64_t t94 = (uint64_t)(t85 | t93);
                uint64_t t95 = (uintptr_t)((uint64_t)2ULL);
                uint64_t t96 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t95 >= (uintptr_t)(t96)) gm_trap_index(t95, t96, gm_srcnqtpfma6qe4a, 317, 68);
                uint8_t t97 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t95 * 1));
                uint64_t t98 = (uint64_t)t97;
                unsigned __int128 n99 = (unsigned __int128)((uint64_t)16ULL);
                if (n99 >= 64) gm_trap_shift((uint64_t)n99, "u64", gm_srcnqtpfma6qe4a, 317, 64);
                uint64_t r100 = (uint64_t)((uint64_t)t98 << (int)n99);
                bool o101 = (uint64_t)(r100 >> (int)n99) != t98;
                if (o101) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 317, 64);
                uint64_t t102 = r100;
                uint64_t t103 = (uint64_t)(t94 | t102);
                uint64_t t104 = (uintptr_t)((uint64_t)3ULL);
                uint64_t t105 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t104 >= (uintptr_t)(t105)) gm_trap_index(t104, t105, gm_srcnqtpfma6qe4a, 317, 88);
                uint8_t t106 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t104 * 1));
                uint64_t t107 = (uint64_t)t106;
                unsigned __int128 n108 = (unsigned __int128)((uint64_t)24ULL);
                if (n108 >= 64) gm_trap_shift((uint64_t)n108, "u64", gm_srcnqtpfma6qe4a, 317, 84);
                uint64_t r109 = (uint64_t)((uint64_t)t107 << (int)n108);
                bool o110 = (uint64_t)(r109 >> (int)n108) != t107;
                if (o110) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 317, 84);
                uint64_t t111 = r109;
                uint64_t t112 = (uint64_t)(t103 | t111);
                uint8_t t113 = 0;
                F.ret = t112;
                goto gm_fret;
            }
            goto ix78;
ie78:;
ix78:;
        }
        {
            uint64_t t115 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            uint8_t t116 = ((t115 == ((uint64_t)1ULL)));
            if (!t116) goto ie114;
            {
                uint64_t t118 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t119 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t118 >= (uintptr_t)(t119)) gm_trap_index(t118, t119, gm_srcnqtpfma6qe4a, 318, 36);
                uint8_t t120 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t118 * 1));
                uint64_t t121 = (uint64_t)t120;
                uint8_t t122 = 0;
                F.ret = t121;
                goto gm_fret;
            }
            goto ix114;
ie114:;
ix114:;
        }
        {
            uint64_t t124 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            uint8_t t125 = ((t124 == ((uint64_t)2ULL)));
            if (!t125) goto ie123;
            {
                uint64_t t127 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t128 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t127 >= (uintptr_t)(t128)) gm_trap_index(t127, t128, gm_srcnqtpfma6qe4a, 319, 36);
                uint8_t t129 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t127 * 1));
                uint64_t t130 = (uint64_t)t129;
                uint64_t t131 = (uintptr_t)((uint64_t)1ULL);
                uint64_t t132 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t131 >= (uintptr_t)(t132)) gm_trap_index(t131, t132, gm_srcnqtpfma6qe4a, 319, 49);
                uint8_t t133 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t131 * 1));
                uint64_t t134 = (uint64_t)t133;
                unsigned __int128 n135 = (unsigned __int128)((uint64_t)8ULL);
                if (n135 >= 64) gm_trap_shift((uint64_t)n135, "u64", gm_srcnqtpfma6qe4a, 319, 45);
                uint64_t r136 = (uint64_t)((uint64_t)t134 << (int)n135);
                bool o137 = (uint64_t)(r136 >> (int)n135) != t134;
                if (o137) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 319, 45);
                uint64_t t138 = r136;
                uint64_t t139 = (uint64_t)(t130 | t138);
                uint8_t t140 = 0;
                F.ret = t139;
                goto gm_fret;
            }
            goto ix123;
ie123:;
ix123:;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t141 = gm_l0;
        gm_b16_8 t142 = gm_l1;
        unsigned __int128 t143 = gm_f_get_uint_bq8c4b7(t141, t142);
        uint64_t t144 = (uint64_t)t143;
        uint8_t t145 = 0;
        F.ret = t144;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_read_u6r4vns(void *gm_out, void * a0, uint64_t a1, uint64_t a2, uint64_t a3) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_read_u6r4vns F;
    void * gm_l0; /* m */
    uint64_t gm_l1; /* addr */
    uint64_t gm_l2; /* len */
    uint64_t gm_l3; /* align */
    void * gm_l4; /* p */
    GM_STACK_CHECK(&F, 267u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        uint64_t t3 = gm_l2;
        uint8_t t4 = ((t3 == ((uint64_t)0ULL)));
        if (!t4) goto ie2;
        uint64_t t5 = gm_l3;
        uint8_t t6 = (t5 <= ((uint64_t)1ULL));
        if (!t6) goto ie2;
        {
            gm_b16_8 t8 = gm_f_empty_slice_ijtp9lo();
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 32, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 40, 0);
            ((uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out))[0] = 0;
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 8, t8);
            goto gm_fdone;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        uint64_t t10 = gm_l1;
        uint64_t t11 = gm_l3;
        uint8_t t12 = gm_f_aligned_q6sor4h(t10, t11);
        if (!t12) goto ie9;
        {
            void * t15 = gm_l0;
            uint64_t t16 = gm_l1;
            uint64_t t17 = gm_l2;
            void * t18 = gm_f_view_qalkbv1(t15, t16, t17, ((uint8_t)0));
            if (!(GM_LD(void *, ((uint8_t *)&t18)) != 0)) goto ie14;
            void * t19 = GM_LD(void *, ((uint8_t *)&t18));
            gm_l4 = t19;
            {
                void * t21 = gm_l4;
                void * t22 = ((uint8_t *)t21);
                uint64_t t23 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t24 = gm_l2;
                uint64_t t25 = gm_f_us_cuv01fp(t24);
                uint64_t t26 = (uintptr_t)t25;
                gm_b16_8 t27;
                GM_ST(void *, (uint8_t *)&t27, (void *)(((uint8_t *)t22 + t23 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t27 + 8, (uintptr_t)(t26 - t23));
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 24, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 32, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 40, 0);
                ((uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out))[0] = 0;
                GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 8, t27);
                goto gm_fdone;
            }
            goto ix14;
ie14:;
ix14:;
        }
        goto ix9;
ie9:;
ix9:;
    }
    {
        void * t28 = gm_l0;
        uint64_t t29 = gm_l1;
        uint64_t t30 = gm_l2;
        uint64_t t31 = gm_l3;
        gm_tpekhfs32op0f t32;
        gm_f_fault_u31b92l((void *)&t32, t28, t29, t30, t31, ((uint8_t)0));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 4, 0);
        ((uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out))[0] = 1;
        GM_ST(gm_tpekhfs32op0f, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 8, t32);
        goto gm_fdone;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_b16_8 gm_f_tmp_8k6u9c9(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1) {
    struct gm_fr_tmp_8k6u9c9 F;
    void * gm_l0; /* m */
    uint64_t gm_l1; /* n */
    void * gm_l2; /* sc */
    gm_b16_8 gm_l3; /* chunk */
    uint64_t gm_l4; /* base */
    uint64_t gm_l5; /* top */
    uint64_t gm_l6; /* size */
    gm_b16_8 gm_l7; /* b */
    GM_STACK_CHECK(&F, 517u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        void * t3 = (void *)(((uint8_t *)t2) + 360);
        gm_l2 = t3;
    }
    {
        void * t4 = gm_l2;
        uint64_t t5 = GM_LD(uint64_t, (((uint8_t *)t4) + 40));
        uint64_t t6 = (uintptr_t)t5;
        void * t7 = gm_l2;
        uint64_t t8 = GM_LD(uintptr_t, ((((uint8_t *)t7) + 0) + 0) + 8);
        if (t6 >= (uintptr_t)(t8)) gm_trap_index(t6, t8, gm_srcpit8mdn0781q, 386, 14);
        gm_b16_8 t9 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t7) + 0) + 0)) + t6 * 16));
        gm_l3 = t9;
    }
    {
        void * t10 = GM_LD(void *, ((uint8_t *)&gm_l3));
        uint64_t t11 = (uintptr_t)t10;
        gm_l4 = t11;
    }
    {
        uint64_t t12 = gm_l4;
        void * t13 = gm_l2;
        uint64_t t14 = GM_LD(uint64_t, (((uint8_t *)t13) + 48));
        uint64_t r15; bool o16 = __builtin_add_overflow(t12, t14, &r15);
        if (o16) gm_trap("integer overflow in `+`", gm_srcpit8mdn0781q, 390, 14);
        uint64_t t17 = r15;
        uint64_t r18; bool o19 = __builtin_add_overflow(t17, ((uint64_t)7ULL), &r18);
        if (o19) gm_trap("integer overflow in `+`", gm_srcpit8mdn0781q, 390, 14);
        uint64_t t20 = r18;
        uint64_t t21 = gm_f_us_cuv01fp(((uint64_t)7ULL));
        uint64_t t22 = (uint64_t)~t21;
        uint64_t t23 = (uint64_t)(t20 & t22);
        uint64_t t24 = gm_l4;
        uint64_t r25; bool o26 = __builtin_sub_overflow(t23, t24, &r25);
        if (o26) gm_trap("integer overflow in `-`", gm_srcpit8mdn0781q, 390, 12);
        uint64_t t27 = r25;
        gm_l5 = t27;
    }
    {
        uint64_t t28 = gm_l1;
        uint64_t t29 = gm_f_us_cuv01fp(t28);
        gm_l6 = t29;
    }
    {
        uint8_t t31 = 0;
        uint64_t t32 = gm_l1;
        uint8_t t33 = ((t32 == ((uint64_t)0ULL)));
        if (!t33) {
            uint64_t t34 = gm_l5;
            uint64_t t35 = gm_l6;
            uint64_t r36; bool o37 = __builtin_add_overflow(t34, t35, &r36);
            if (o37) gm_trap("integer overflow in `+`", gm_srcpit8mdn0781q, 392, 18);
            uint64_t t38 = r36;
            uint64_t t39 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
            uint8_t t40 = (t38 > t39);
            t31 = t40;
        } else t31 = 1;
        if (!t31) goto ie30;
        {
            void * t42 = gm_l0;
            uint64_t t43 = gm_l1;
            gm_b16_8 t44 = gm_f_tmp_next_dalgo1f((void *)NULL, cx, NULL, t42, t43);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t45 = 0;
            F.ret = t44;
            goto gm_fret;
        }
        goto ix30;
ie30:;
ix30:;
    }
    {
        uint64_t t46 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
        void * t47 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)));
        uint64_t t48 = gm_l5;
        uint64_t t49 = (uintptr_t)t48;
        uint64_t t50 = gm_l5;
        uint64_t t51 = gm_l6;
        uint64_t r52; bool o53 = __builtin_add_overflow(t50, t51, &r52);
        if (o53) gm_trap("integer overflow in `+`", gm_srcpit8mdn0781q, 393, 21);
        uint64_t t54 = r52;
        uint64_t t55 = (uintptr_t)t54;
        if (t49 > t55 || t55 > (uintptr_t)t46) gm_trap_slice(t49, t55, t46, gm_srcpit8mdn0781q, 393, 10);
        gm_b16_8 t56;
        GM_ST(void *, (uint8_t *)&t56, (void *)(((uint8_t *)t47 + t49 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t56 + 8, (uintptr_t)(t55 - t49));
        gm_l7 = t56;
    }
    {
        void * t57 = gm_l2;
        uint64_t t58 = gm_l5;
        uint64_t t59 = gm_l6;
        uint64_t r60; bool o61 = __builtin_add_overflow(t58, t59, &r60);
        if (o61) gm_trap("integer overflow in `+`", gm_srcpit8mdn0781q, 394, 14);
        uint64_t t62 = r60;
        GM_ST(uint64_t, (((uint8_t *)t57) + 48), t62);
    }
    {
        gm_b16_8 t63 = gm_l7;
        gm_f_fill_abmf5g5(t63, ((uint8_t)0ULL));
    }
    {
        gm_b16_8 t64 = gm_l7;
        uint8_t t65 = 0;
        F.ret = t64;
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline void * gm_f_view_qalkbv1(void * a0, uint64_t a1, uint64_t a2, uint8_t a3) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_view_qalkbv1 F;
    void * gm_l0; /* m */
    uint64_t gm_l1; /* addr */
    uint64_t gm_l2; /* len */
    uint8_t gm_l3; /* write */
    uint64_t gm_l4; /* p */
    void * gm_l5; /* pg */
    void * gm_l6; /* ss */
    uint64_t gm_l7; /* rel */
    uint64_t gm_l8; /* i */
    void * gm_l9; /* s */
    uint64_t gm_l10; /* off */
    GM_STACK_CHECK_SHALLOW(&F, 561u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        uint64_t t2 = gm_l1;
        void * t3 = gm_l0;
        uint64_t t4 = GM_LD(uint64_t, (((uint8_t *)t3) + 40));
        uint64_t r5; bool o6 = __builtin_sub_overflow(t2, t4, &r5);
        uint64_t t7 = r5;
        uint64_t t8 = GM_LD(uint64_t, gm_c6p4chcutca);
        unsigned __int128 n9 = (unsigned __int128)t8;
        if (n9 >= 64) gm_trap_shift((uint64_t)n9, "u64", gm_srcq7ohh8h77bn7, 206, 10);
        uint64_t t10 = (uint64_t)(t7 >> (int)n9);
        gm_l4 = t10;
    }
    {
        uint64_t t12 = gm_l4;
        void * t13 = gm_l0;
        uint64_t t14 = GM_LD(uintptr_t, ((((uint8_t *)t13) + 0) + 0) + 8);
        uint64_t t15 = (uint64_t)t14;
        uint8_t t16 = (t12 >= t15);
        if (!t16) goto ie11;
        {
            uint8_t t18 = 0;
            F.ret = ((void *)0);
            goto gm_fret;
        }
        goto ix11;
ie11:;
ix11:;
    }
    {
        void * t19 = gm_l0;
        void * t20 = GM_LD(void *, ((((uint8_t *)t19) + 0) + 0));
        void * t21 = t20;
        uint64_t t22 = gm_l4;
        uint64_t t23 = gm_f_us_cuv01fp(t22);
        uint64_t t24 = (uintptr_t)t23;
        void * t25 = (void *)((uint8_t *)t21 + t24 * 32);
        gm_l5 = t25;
    }
    {
        void * t26 = gm_l5;
        void * t27 = GM_LD(void *, (((uint8_t *)t26) + 0));
        void * t28;
        if ((GM_LD(void *, ((uint8_t *)&t27)) != 0)) {
            void * t29 = GM_LD(void *, (((uint8_t *)&t27) + 0));
            t28 = t29;
        } else {
            uint8_t t30 = 0;
            F.ret = ((void *)0);
            goto gm_fret;
        }
        gm_l6 = t28;
    }
    {
        uint64_t t31 = gm_l1;
        void * t32 = gm_l5;
        uint64_t t33 = GM_LD(uint64_t, (((uint8_t *)t32) + 8));
        uint64_t r34; bool o35 = __builtin_sub_overflow(t31, t33, &r34);
        if (o35) gm_trap("integer overflow in `-`", gm_srcq7ohh8h77bn7, 210, 12);
        uint64_t t36 = r34;
        gm_l7 = t36;
    }
    {
        uint64_t t37 = gm_l7;
        void * t38 = gm_l5;
        uint64_t t39 = GM_LD(uint64_t, (((uint8_t *)t38) + 16));
        unsigned __int128 n40 = (unsigned __int128)t39;
        if (n40 >= 64) gm_trap_shift((uint64_t)n40, "u64", gm_srcq7ohh8h77bn7, 211, 10);
        uint64_t t41 = (uint64_t)(t37 >> (int)n40);
        gm_l8 = t41;
    }
    {
        void * t42 = gm_l6;
        void * t43 = t42;
        uint64_t t44 = gm_l8;
        uint64_t t45 = gm_f_us_cuv01fp(t44);
        uint64_t t46 = (uintptr_t)t45;
        void * t47 = (void *)((uint8_t *)t43 + t46 * 24);
        gm_l9 = t47;
    }
    {
        uint64_t t48 = gm_l7;
        uint64_t t49 = gm_l8;
        void * t50 = gm_l5;
        uint64_t t51 = GM_LD(uint64_t, (((uint8_t *)t50) + 16));
        unsigned __int128 n52 = (unsigned __int128)t51;
        if (n52 >= 64) gm_trap_shift((uint64_t)n52, "u64", gm_srcq7ohh8h77bn7, 213, 19);
        uint64_t r53 = (uint64_t)((uint64_t)t49 << (int)n52);
        bool o54 = (uint64_t)(r53 >> (int)n52) != t49;
        if (o54) gm_trap("integer overflow in `<<`", gm_srcq7ohh8h77bn7, 213, 19);
        uint64_t t55 = r53;
        uint64_t r56; bool o57 = __builtin_sub_overflow(t48, t55, &r56);
        if (o57) gm_trap("integer overflow in `-`", gm_srcq7ohh8h77bn7, 213, 12);
        uint64_t t58 = r56;
        gm_l10 = t58;
    }
    {
        uint8_t t60 = 0;
        uint8_t t61 = 0;
        uint8_t t62 = 0;
        void * t63 = gm_l9;
        uint8_t t64 = GM_LD(uint8_t, (((uint8_t *)t63) + 17));
        uint8_t t65 = (uint8_t)!t64;
        if (!t65) {
            uint64_t t66 = gm_l2;
            void * t67 = gm_l9;
            uint64_t t68 = GM_LD(uint64_t, (((uint8_t *)t67) + 8));
            uint8_t t69 = (t66 > t68);
            t62 = t69;
        } else t62 = 1;
        if (!t62) {
            uint64_t t70 = gm_l10;
            void * t71 = gm_l9;
            uint64_t t72 = GM_LD(uint64_t, (((uint8_t *)t71) + 8));
            uint64_t t73 = gm_l2;
            uint64_t r74; bool o75 = __builtin_sub_overflow(t72, t73, &r74);
            if (o75) gm_trap("integer overflow in `-`", gm_srcq7ohh8h77bn7, 214, 44);
            uint64_t t76 = r74;
            uint8_t t77 = (t70 > t76);
            t61 = t77;
        } else t61 = 1;
        if (!t61) {
            uint8_t t78 = 0;
            uint8_t t79 = gm_l3;
            if (t79) {
                void * t80 = gm_l9;
                uint8_t t81 = GM_LD(uint8_t, (((uint8_t *)t80) + 18));
                uint8_t t82 = (uint8_t)!t81;
                t78 = t82;
            } else t78 = 0;
            t60 = t78;
        } else t60 = 1;
        if (!t60) goto ie59;
        {
            uint8_t t84 = 0;
            F.ret = ((void *)0);
            goto gm_fret;
        }
        goto ix59;
ie59:;
ix59:;
    }
    {
        void * t85 = gm_l9;
        void * t86 = GM_LD(void *, (((uint8_t *)t85) + 0));
        void * t87 = t86;
        uint64_t t88 = gm_l10;
        uint64_t t89 = gm_f_us_cuv01fp(t88);
        uint64_t t90 = (uintptr_t)t89;
        void * t91 = (void *)((uint8_t *)t87 + t90 * 1);
        void * t92 = t91;
        uint8_t t93 = 0;
        F.ret = t92;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_wr_word_npte9fs(void * a0, gm_b16_8 a1, uint64_t a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_wr_word_npte9fs F;
    void * gm_l0; /* m */
    gm_b16_8 gm_l1; /* b */
    uint64_t gm_l2; /* v */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t3 = gm_l0;
        uint8_t t4 = GM_LD(uint8_t, (((uint8_t *)t3) + 89));
        if (!t4) goto ie2;
        {
            uint64_t t7 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            uint8_t t8 = ((t7 == ((uint64_t)8ULL)));
            if (!t8) goto ie6;
            {
                uint64_t t10 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t11 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t10 >= (uintptr_t)(t11)) gm_trap_index(t10, t11, gm_srcnqtpfma6qe4a, 329, 13);
                uint64_t t12 = gm_l2;
                uint8_t t13 = (uint8_t)t12;
                GM_ST(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t10 * 1), t13);
            }
            {
                uint64_t t14 = (uintptr_t)((uint64_t)1ULL);
                uint64_t t15 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t14 >= (uintptr_t)(t15)) gm_trap_index(t14, t15, gm_srcnqtpfma6qe4a, 330, 13);
                uint64_t t16 = gm_l2;
                unsigned __int128 n17 = (unsigned __int128)((uint64_t)8ULL);
                if (n17 >= 64) gm_trap_shift((uint64_t)n17, "u64", gm_srcnqtpfma6qe4a, 330, 21);
                uint64_t t18 = (uint64_t)(t16 >> (int)n17);
                uint8_t t19 = (uint8_t)t18;
                GM_ST(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t14 * 1), t19);
            }
            {
                uint64_t t20 = (uintptr_t)((uint64_t)2ULL);
                uint64_t t21 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t20 >= (uintptr_t)(t21)) gm_trap_index(t20, t21, gm_srcnqtpfma6qe4a, 331, 13);
                uint64_t t22 = gm_l2;
                unsigned __int128 n23 = (unsigned __int128)((uint64_t)16ULL);
                if (n23 >= 64) gm_trap_shift((uint64_t)n23, "u64", gm_srcnqtpfma6qe4a, 331, 21);
                uint64_t t24 = (uint64_t)(t22 >> (int)n23);
                uint8_t t25 = (uint8_t)t24;
                GM_ST(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t20 * 1), t25);
            }
            {
                uint64_t t26 = (uintptr_t)((uint64_t)3ULL);
                uint64_t t27 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t26 >= (uintptr_t)(t27)) gm_trap_index(t26, t27, gm_srcnqtpfma6qe4a, 332, 13);
                uint64_t t28 = gm_l2;
                unsigned __int128 n29 = (unsigned __int128)((uint64_t)24ULL);
                if (n29 >= 64) gm_trap_shift((uint64_t)n29, "u64", gm_srcnqtpfma6qe4a, 332, 21);
                uint64_t t30 = (uint64_t)(t28 >> (int)n29);
                uint8_t t31 = (uint8_t)t30;
                GM_ST(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t26 * 1), t31);
            }
            {
                uint64_t t32 = (uintptr_t)((uint64_t)4ULL);
                uint64_t t33 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t32 >= (uintptr_t)(t33)) gm_trap_index(t32, t33, gm_srcnqtpfma6qe4a, 333, 13);
                uint64_t t34 = gm_l2;
                unsigned __int128 n35 = (unsigned __int128)((uint64_t)32ULL);
                if (n35 >= 64) gm_trap_shift((uint64_t)n35, "u64", gm_srcnqtpfma6qe4a, 333, 21);
                uint64_t t36 = (uint64_t)(t34 >> (int)n35);
                uint8_t t37 = (uint8_t)t36;
                GM_ST(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t32 * 1), t37);
            }
            {
                uint64_t t38 = (uintptr_t)((uint64_t)5ULL);
                uint64_t t39 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t38 >= (uintptr_t)(t39)) gm_trap_index(t38, t39, gm_srcnqtpfma6qe4a, 334, 13);
                uint64_t t40 = gm_l2;
                unsigned __int128 n41 = (unsigned __int128)((uint64_t)40ULL);
                if (n41 >= 64) gm_trap_shift((uint64_t)n41, "u64", gm_srcnqtpfma6qe4a, 334, 21);
                uint64_t t42 = (uint64_t)(t40 >> (int)n41);
                uint8_t t43 = (uint8_t)t42;
                GM_ST(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t38 * 1), t43);
            }
            {
                uint64_t t44 = (uintptr_t)((uint64_t)6ULL);
                uint64_t t45 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t44 >= (uintptr_t)(t45)) gm_trap_index(t44, t45, gm_srcnqtpfma6qe4a, 335, 13);
                uint64_t t46 = gm_l2;
                unsigned __int128 n47 = (unsigned __int128)((uint64_t)48ULL);
                if (n47 >= 64) gm_trap_shift((uint64_t)n47, "u64", gm_srcnqtpfma6qe4a, 335, 21);
                uint64_t t48 = (uint64_t)(t46 >> (int)n47);
                uint8_t t49 = (uint8_t)t48;
                GM_ST(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t44 * 1), t49);
            }
            {
                uint64_t t50 = (uintptr_t)((uint64_t)7ULL);
                uint64_t t51 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t50 >= (uintptr_t)(t51)) gm_trap_index(t50, t51, gm_srcnqtpfma6qe4a, 336, 13);
                uint64_t t52 = gm_l2;
                unsigned __int128 n53 = (unsigned __int128)((uint64_t)56ULL);
                if (n53 >= 64) gm_trap_shift((uint64_t)n53, "u64", gm_srcnqtpfma6qe4a, 336, 21);
                uint64_t t54 = (uint64_t)(t52 >> (int)n53);
                uint8_t t55 = (uint8_t)t54;
                GM_ST(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t50 * 1), t55);
            }
            {
                gm_b0_1 t56;
                uint8_t t57 = 0;
                F.ret = t56;
                goto gm_fret;
            }
            goto ix6;
ie6:;
ix6:;
        }
        {
            uint64_t t59 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            uint8_t t60 = ((t59 == ((uint64_t)1ULL)));
            if (!t60) goto ie58;
            {
                uint64_t t62 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t63 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t62 >= (uintptr_t)(t63)) gm_trap_index(t62, t63, gm_srcnqtpfma6qe4a, 340, 13);
                uint64_t t64 = gm_l2;
                uint8_t t65 = (uint8_t)t64;
                GM_ST(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t62 * 1), t65);
            }
            {
                gm_b0_1 t66;
                uint8_t t67 = 0;
                F.ret = t66;
                goto gm_fret;
            }
            goto ix58;
ie58:;
ix58:;
        }
        {
            uint64_t t69 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            uint8_t t70 = ((t69 == ((uint64_t)4ULL)));
            if (!t70) goto ie68;
            {
                uint64_t t72 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t73 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t72 >= (uintptr_t)(t73)) gm_trap_index(t72, t73, gm_srcnqtpfma6qe4a, 344, 13);
                uint64_t t74 = gm_l2;
                uint8_t t75 = (uint8_t)t74;
                GM_ST(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t72 * 1), t75);
            }
            {
                uint64_t t76 = (uintptr_t)((uint64_t)1ULL);
                uint64_t t77 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t76 >= (uintptr_t)(t77)) gm_trap_index(t76, t77, gm_srcnqtpfma6qe4a, 345, 13);
                uint64_t t78 = gm_l2;
                unsigned __int128 n79 = (unsigned __int128)((uint64_t)8ULL);
                if (n79 >= 64) gm_trap_shift((uint64_t)n79, "u64", gm_srcnqtpfma6qe4a, 345, 21);
                uint64_t t80 = (uint64_t)(t78 >> (int)n79);
                uint8_t t81 = (uint8_t)t80;
                GM_ST(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t76 * 1), t81);
            }
            {
                uint64_t t82 = (uintptr_t)((uint64_t)2ULL);
                uint64_t t83 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t82 >= (uintptr_t)(t83)) gm_trap_index(t82, t83, gm_srcnqtpfma6qe4a, 346, 13);
                uint64_t t84 = gm_l2;
                unsigned __int128 n85 = (unsigned __int128)((uint64_t)16ULL);
                if (n85 >= 64) gm_trap_shift((uint64_t)n85, "u64", gm_srcnqtpfma6qe4a, 346, 21);
                uint64_t t86 = (uint64_t)(t84 >> (int)n85);
                uint8_t t87 = (uint8_t)t86;
                GM_ST(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t82 * 1), t87);
            }
            {
                uint64_t t88 = (uintptr_t)((uint64_t)3ULL);
                uint64_t t89 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t88 >= (uintptr_t)(t89)) gm_trap_index(t88, t89, gm_srcnqtpfma6qe4a, 347, 13);
                uint64_t t90 = gm_l2;
                unsigned __int128 n91 = (unsigned __int128)((uint64_t)24ULL);
                if (n91 >= 64) gm_trap_shift((uint64_t)n91, "u64", gm_srcnqtpfma6qe4a, 347, 21);
                uint64_t t92 = (uint64_t)(t90 >> (int)n91);
                uint8_t t93 = (uint8_t)t92;
                GM_ST(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t88 * 1), t93);
            }
            {
                gm_b0_1 t94;
                uint8_t t95 = 0;
                F.ret = t94;
                goto gm_fret;
            }
            goto ix68;
ie68:;
ix68:;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t96 = gm_l0;
        gm_b16_8 t97 = gm_l1;
        uint64_t t98 = gm_l2;
        unsigned __int128 t99 = (unsigned __int128)t98;
        gm_f_put_uint_cddin99(t96, t97, t99);
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_write_view_pebit6h(void *gm_out, void * a0, uint64_t a1, uint64_t a2, uint64_t a3) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_view_pebit6h F;
    void * gm_l0; /* m */
    uint64_t gm_l1; /* addr */
    uint64_t gm_l2; /* len */
    uint64_t gm_l3; /* align */
    void * gm_l4; /* p */
    GM_STACK_CHECK(&F, 267u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        uint64_t t3 = gm_l2;
        uint8_t t4 = ((t3 == ((uint64_t)0ULL)));
        if (!t4) goto ie2;
        uint64_t t5 = gm_l3;
        uint8_t t6 = (t5 <= ((uint64_t)1ULL));
        if (!t6) goto ie2;
        {
            gm_b16_8 t8 = gm_f_empty_slice_ijtp9lo();
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 32, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 40, 0);
            ((uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out))[0] = 0;
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 8, t8);
            goto gm_fdone;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        uint64_t t10 = gm_l1;
        uint64_t t11 = gm_l3;
        uint8_t t12 = gm_f_aligned_q6sor4h(t10, t11);
        if (!t12) goto ie9;
        {
            void * t15 = gm_l0;
            uint64_t t16 = gm_l1;
            uint64_t t17 = gm_l2;
            void * t18 = gm_f_view_qalkbv1(t15, t16, t17, ((uint8_t)1));
            if (!(GM_LD(void *, ((uint8_t *)&t18)) != 0)) goto ie14;
            void * t19 = GM_LD(void *, ((uint8_t *)&t18));
            gm_l4 = t19;
            {
                void * t21 = gm_l4;
                void * t22 = ((uint8_t *)t21);
                uint64_t t23 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t24 = gm_l2;
                uint64_t t25 = gm_f_us_cuv01fp(t24);
                uint64_t t26 = (uintptr_t)t25;
                gm_b16_8 t27;
                GM_ST(void *, (uint8_t *)&t27, (void *)(((uint8_t *)t22 + t23 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t27 + 8, (uintptr_t)(t26 - t23));
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 24, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 32, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 40, 0);
                ((uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out))[0] = 0;
                GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 8, t27);
                goto gm_fdone;
            }
            goto ix14;
ie14:;
ix14:;
        }
        goto ix9;
ie9:;
ix9:;
    }
    {
        void * t28 = gm_l0;
        uint64_t t29 = gm_l1;
        uint64_t t30 = gm_l2;
        uint64_t t31 = gm_l3;
        gm_tpekhfs32op0f t32;
        gm_f_fault_u31b92l((void *)&t32, t28, t29, t30, t31, ((uint8_t)1));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 4, 0);
        ((uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out))[0] = 1;
        GM_ST(gm_tpekhfs32op0f, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 8, t32);
        goto gm_fdone;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) void gm_f_memory_builtin_9s47dk8(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_tvdg94bk7aa22 a2, gm_b16_8 a3, gm_b16_8 a4, gm_t88rcucg1b7uk a5, gm_b16_8 a6) {
    struct gm_fr_memory_builtin_9s47dk8 F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_tvdg94bk7aa22 gm_l2; /* op */
    gm_b16_8 gm_l3; /* args */
    gm_b16_8 gm_l4; /* v */
    gm_t88rcucg1b7uk gm_l5; /* res_ty */
    gm_b16_8 gm_l6; /* dst */
    uint64_t gm_l7; /* size */
    uint64_t gm_l8; /* al */
    uint64_t gm_l9; /* addr */
    gm_b16_8 gm_l10; /* d */
    gm_t2doi72kmmlil gm_l11; /* pb */
    gm_b16_8 gm_l12; /* p */
    uint64_t gm_l13; /* p */
    uint64_t gm_l14; /* size */
    gm_tilak255t5m34 gm_l15; /* d */
    uint64_t gm_l16; /* p */
    uint64_t gm_l17; /* owner */
    uint64_t gm_l18; /* vt */
    gm_b16_8 gm_l19; /* file */
    gm_b16_8 gm_l20; /* site */
    gm_tbccvikg6b96e gm_l21; /* tag */
    uint64_t gm_l22; /* addr */
    gm_t88rcucg1b7uk gm_l23; /* ty */
    uint64_t gm_l24; /* al */
    gm_b16_8 gm_l25; /* old */
    gm_t9qkh0rsooc8d gm_l26; /* rmw */
    unsigned __int128 gm_l27; /* x */
    unsigned __int128 gm_l28; /* y */
    uint8_t gm_l29; /* signed */
    unsigned __int128 gm_l30; /* r */
    gm_b16_8 gm_l31; /* nb */
    gm_t88rcucg1b7uk gm_l32; /* elem */
    gm_t2lqu8jsj511d gm_l33; /* d */
    gm_t2lqu8jsj511d gm_l34; /* s */
    uint64_t gm_l35; /* n */
    gm_b16_8 gm_l37; /* src */
    gm_b16_8 gm_l38; /* tmp */
    uint64_t gm_l40; /* p */
    uint64_t gm_l41; /* n */
    gm_b16_8 gm_l43; /* b */
    gm_t88rcucg1b7uk gm_l44; /* vt */
    gm_t2lqu8jsj511d gm_l45; /* sp */
    uint64_t gm_l46; /* n */
    gm_t88rcucg1b7uk gm_l47; /* lane */
    gm_b16_8 gm_l48; /* what */
    uint64_t gm_l49; /* al */
    GM_STACK_CHECK(&F, 7389u);
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
        {
            uint8_t t14 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            switch (t14) { case ((uint8_t)0ULL): goto ma12; case ((uint8_t)1ULL): goto ma12; case ((uint8_t)2ULL): goto ma12; case ((uint8_t)3ULL): goto ma12; case ((uint8_t)4ULL): goto ma12; case ((uint8_t)5ULL): goto ma12; case ((uint8_t)6ULL): goto ma12; case ((uint8_t)7ULL): goto ma12; case ((uint8_t)8ULL): goto ma12; case ((uint8_t)9ULL): goto ma12; case ((uint8_t)10ULL): goto ma12; case ((uint8_t)11ULL): goto ma12; case ((uint8_t)12ULL): goto ma12; case ((uint8_t)13ULL): goto ma12; case ((uint8_t)14ULL): goto ma12; case ((uint8_t)15ULL): goto ma12; case ((uint8_t)16ULL): goto ma12; case ((uint8_t)17ULL): goto ma12; case ((uint8_t)18ULL): goto ma12; case ((uint8_t)19ULL): goto ma12; case ((uint8_t)20ULL): goto ma12; case ((uint8_t)21ULL): goto ma12; case ((uint8_t)22ULL): goto ma12; case ((uint8_t)23ULL): goto ma12; case ((uint8_t)24ULL): goto ma12; case ((uint8_t)25ULL): goto ma12; case ((uint8_t)26ULL): goto ma12; case ((uint8_t)27ULL): goto ma12; case ((uint8_t)28ULL): goto ma12; case ((uint8_t)29ULL): goto ma12; case ((uint8_t)30ULL): goto ma12; case ((uint8_t)31ULL): goto ma12; case ((uint8_t)32ULL): goto ma12; case ((uint8_t)33ULL): goto ma12; case ((uint8_t)34ULL): goto ma12; case ((uint8_t)35ULL): goto ma12; case ((uint8_t)36ULL): goto ma12; case ((uint8_t)37ULL): goto ma12; case ((uint8_t)38ULL): goto ma12; case ((uint8_t)39ULL): goto ma12; case ((uint8_t)40ULL): goto ma12; case ((uint8_t)41ULL): goto ma12; case ((uint8_t)42ULL): goto ma12; case ((uint8_t)43ULL): goto ma12; case ((uint8_t)44ULL): goto ma12; case ((uint8_t)45ULL): goto ma12; case ((uint8_t)46ULL): goto ma12; case ((uint8_t)47ULL): goto ma12; case ((uint8_t)48ULL): goto ma12; case ((uint8_t)49ULL): goto ma12; case ((uint8_t)50ULL): goto ma12; case ((uint8_t)51ULL): goto ma12; case ((uint8_t)52ULL): goto ma12; case ((uint8_t)53ULL): goto ma12; case ((uint8_t)54ULL): goto ma12; case ((uint8_t)55ULL): goto ma12; case ((uint8_t)56ULL): goto ma12; case ((uint8_t)57ULL): goto ma12; case ((uint8_t)58ULL): goto ma11; case ((uint8_t)59ULL): goto ma11; case ((uint8_t)60ULL): goto ma10; case ((uint8_t)61ULL): goto ma12; case ((uint8_t)62ULL): goto ma12; case ((uint8_t)63ULL): goto ma12; case ((uint8_t)64ULL): goto ma7; case ((uint8_t)65ULL): goto ma7; case ((uint8_t)66ULL): goto ma7; case ((uint8_t)67ULL): goto ma7; case ((uint8_t)68ULL): goto ma7; case ((uint8_t)69ULL): goto ma8; case ((uint8_t)70ULL): goto ma7; case ((uint8_t)71ULL): goto ma7; case ((uint8_t)72ULL): goto ma12; case ((uint8_t)73ULL): goto ma12; case ((uint8_t)74ULL): goto ma4; case ((uint8_t)75ULL): goto ma5; case ((uint8_t)76ULL): goto ma6; case ((uint8_t)77ULL): goto ma6; case ((uint8_t)78ULL): goto ma9; case ((uint8_t)79ULL): goto ma12; case ((uint8_t)80ULL): goto ma12; case ((uint8_t)81ULL): goto ma12; default: goto ma12; }
ma4:;
            uint8_t t16 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t16 != ((uint8_t)74ULL)) goto mn15;
            {
                void * t18 = gm_l0;
                uint64_t t19 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t20 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                if (t19 >= (uintptr_t)(t20)) gm_trap_index(t19, t20, gm_srccp2mbkjs1vn4, 12, 31);
                gm_b16_8 t21 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t19 * 16));
                gm_b16_8 t22 = GM_LD(gm_b16_8, ((uint8_t *)&t21));
                uint64_t t23 = gm_f_get_u64_tgpb2vh(t18, t22);
                gm_l7 = t23;
            }
            {
                void * t24 = gm_l0;
                uint64_t t25 = (uintptr_t)((uint64_t)1ULL);
                uint64_t t26 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                if (t25 >= (uintptr_t)(t26)) gm_trap_index(t25, t26, gm_srccp2mbkjs1vn4, 13, 29);
                gm_b16_8 t27 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t25 * 16));
                gm_b16_8 t28 = GM_LD(gm_b16_8, ((uint8_t *)&t27));
                uint64_t t29 = gm_f_get_u64_tgpb2vh(t24, t28);
                uint64_t t30 = (t29 > ((uint64_t)1ULL) ? t29 : ((uint64_t)1ULL));
                gm_l8 = t30;
            }
            {
                uint64_t t32 = gm_l8;
                uint64_t t33 = gm_l8;
                uint64_t r34; bool o35 = __builtin_sub_overflow(t33, ((uint64_t)1ULL), &r34);
                if (o35) gm_trap("integer overflow in `-`", gm_srccp2mbkjs1vn4, 14, 22);
                uint64_t t36 = r34;
                uint64_t t37 = (uint64_t)(t32 & t36);
                uint8_t t38 = (!(t37 == ((uint64_t)0ULL)));
                if (!t38) goto ie31;
                {
                    void * t40 = gm_l0;
                    gm_tl874t2atnlh3 t41 = gm_l1;
                    uint64_t t42 = gm_l8;
                    gm_t4ce0jd3tdro4 t43;
                    GM_ST(uint64_t, ((uint8_t *)&t43 + 0), t42);
                    gm_tabb710d9mq2e t44;
                    gm_f_format_jc3ftg7((void *)&t44, (void *)NULL, cx, NULL, t43);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    if (((((uint8_t *)&t44))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t44) + 8, gm_srccp2mbkjs1vn4, 14, 62);
                    gm_b16_8 t45 = GM_LD(gm_b16_8, (((uint8_t *)&t44) + 8));
                    gm_b16_8 t46 = GM_LD(gm_b16_8, ((uint8_t *)&t45));
                    gm_t76123gfet73g t47;
                    gm_f_trap_qh84l9d((void *)&t47, (void *)NULL, cx, NULL, t40, t41, t46);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                    ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                    GM_ST(gm_t76123gfet73g, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t47);
                    goto gm_fdone;
                }
                goto ix31;
ie31:;
ix31:;
            }
            {
                uint64_t t49 = gm_l8;
                uint64_t t50 = GM_LD(uint64_t, gm_ckh5e429gka);
                uint8_t t51 = (t49 > t50);
                if (!t51) goto ie48;
                {
                    gm_b16_8 t53 = gm_l6;
                    gm_f_fill_abmf5g5(t53, ((uint8_t)0ULL));
                }
                {
                    gm_b0_1 t54;
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 16, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 24, 0);
                    ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 0;
                    GM_ST(gm_b0_1, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t54);
                    goto gm_fdone;
                }
                goto ix48;
ie48:;
ix48:;
            }
            {
                void * t55 = gm_l0;
                void * t56 = (void *)(((uint8_t *)t55) + 184);
                uint64_t t57 = gm_l7;
                uint64_t t58 = gm_l8;
                gm_t9qkh0rsooc8d t59;
                GM_ST(uint8_t, ((uint8_t *)&t59 + 0), ((uint8_t)2ULL));
                gm_t3j6egd6cfnk5 t60;
                gm_f_alloc_bfo8187((void *)&t60, (void *)NULL, cx, NULL, t56, t57, t58, t59, ((uint8_t)1));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint64_t t61;
                if (!((((uint8_t *)&t60))[0] != 0)) {
                    uint64_t t62 = GM_LD(uint64_t, (((uint8_t *)&t60) + 8));
                    t61 = t62;
                } else {
                    {
                        gm_b16_8 t64 = gm_l6;
                        gm_f_fill_abmf5g5(t64, ((uint8_t)0ULL));
                    }
                    gm_b0_1 t65;
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 16, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 24, 0);
                    ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 0;
                    GM_ST(gm_b0_1, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t65);
                    goto gm_fdone;
                }
                gm_l9 = t61;
            }
            {
                void * t67 = gm_l0;
                uint8_t t68 = ((GM_LD(uint8_t, (((uint8_t *)t67) + 88) + 0) == ((uint8_t)0ULL)));
                if (!t68) goto ie66;
                uint64_t t69 = gm_l7;
                uint8_t t70 = (t69 > ((uint64_t)0ULL));
                if (!t70) goto ie66;
                {
                    void * t72 = gm_l0;
                    void * t73 = (void *)(((uint8_t *)t72) + 184);
                    uint64_t t74 = gm_l9;
                    uint64_t t75 = gm_l7;
                    gm_t934k1j7u70ct t76;
                    gm_f_write_view_pebit6h((void *)&t76, t73, t74, t75, ((uint64_t)1ULL));
                    gm_b16_8 t77;
                    if (!((((uint8_t *)&t76))[0] != 0)) {
                        gm_b16_8 t78 = GM_LD(gm_b16_8, (((uint8_t *)&t76) + 8));
                        t77 = t78;
                    } else {
                        gm_b0_1 t79;
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 16, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 24, 0);
                        ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 0;
                        GM_ST(gm_b0_1, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t79);
                        goto gm_fdone;
                    }
                    gm_l10 = t77;
                }
                {
                    gm_b16_8 t80 = gm_l10;
                    gm_f_fill_abmf5g5(t80, ((uint8_t)170ULL));
                }
                goto ix66;
ie66:;
ix66:;
            }
            {
                gm_t2doi72kmmlil t81;
                GM_ST(uint8_t, ((uint8_t *)&t81 + 0), ((uint8_t)0ULL));
                GM_ST(uint8_t, ((uint8_t *)&t81 + 1), ((uint8_t)0ULL));
                GM_ST(uint8_t, ((uint8_t *)&t81 + 2), ((uint8_t)0ULL));
                GM_ST(uint8_t, ((uint8_t *)&t81 + 3), ((uint8_t)0ULL));
                GM_ST(uint8_t, ((uint8_t *)&t81 + 4), ((uint8_t)0ULL));
                GM_ST(uint8_t, ((uint8_t *)&t81 + 5), ((uint8_t)0ULL));
                GM_ST(uint8_t, ((uint8_t *)&t81 + 6), ((uint8_t)0ULL));
                GM_ST(uint8_t, ((uint8_t *)&t81 + 7), ((uint8_t)0ULL));
                gm_l11 = t81;
            }
            {
                void * t82 = ((uint8_t *)&gm_l11);
                uint64_t t83 = (uintptr_t)((uint64_t)0ULL);
                void * t84 = gm_l0;
                uint64_t t85 = GM_LD(uint64_t, (((uint8_t *)t84) + 176));
                uint64_t t86 = gm_f_us_cuv01fp(t85);
                uint64_t t87 = (uintptr_t)t86;
                if (t83 > t87 || t87 > (uintptr_t)8) gm_trap_slice(t83, t87, 8, gm_srccp2mbkjs1vn4, 32, 18);
                gm_b16_8 t88;
                GM_ST(void *, (uint8_t *)&t88, (void *)(((uint8_t *)t82 + t83 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t88 + 8, (uintptr_t)(t87 - t83));
                gm_l12 = t88;
            }
            {
                void * t89 = gm_l0;
                gm_b16_8 t90 = gm_l12;
                uint64_t t91 = gm_l9;
                gm_f_put_ptr_188p3mm(t89, t90, t91);
            }
            {
                void * t92 = gm_l0;
                gm_t88rcucg1b7uk t93 = gm_l5;
                gm_b16_8 t94 = gm_l6;
                gm_b16_8 t95 = gm_l12;
                gm_b16_8 t96 = GM_LD(gm_b16_8, ((uint8_t *)&t95));
                gm_b0_1 t97 = gm_f_make_some_tvb9ubk((void *)NULL, cx, NULL, t92, t93, t94, t96);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto me3;
mn15:;
ma5:;
            uint8_t t99 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t99 != ((uint8_t)75ULL)) goto mn98;
            {
                void * t101 = gm_l0;
                uint64_t t102 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t103 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                if (t102 >= (uintptr_t)(t103)) gm_trap_index(t102, t103, gm_srccp2mbkjs1vn4, 37, 27);
                gm_b16_8 t104 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t102 * 16));
                gm_b16_8 t105 = GM_LD(gm_b16_8, ((uint8_t *)&t104));
                uint64_t t106 = gm_f_ptr_of_b77b4c9(t101, t105);
                gm_l13 = t106;
            }
            {
                void * t107 = gm_l0;
                uint64_t t108 = (uintptr_t)((uint64_t)1ULL);
                uint64_t t109 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                if (t108 >= (uintptr_t)(t109)) gm_trap_index(t108, t109, gm_srccp2mbkjs1vn4, 38, 31);
                gm_b16_8 t110 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t108 * 16));
                gm_b16_8 t111 = GM_LD(gm_b16_8, ((uint8_t *)&t110));
                uint64_t t112 = gm_f_get_u64_tgpb2vh(t107, t111);
                gm_l14 = t112;
            }
            {
                void * t113 = gm_l0;
                void * t114 = (void *)(((uint8_t *)t113) + 184);
                uint64_t t115 = gm_l13;
                gm_tqvpud5umqqp1 t116;
                gm_f_describe_t62jhnt((void *)&t116, t114, t115);
                gm_tilak255t5m34 t117;
                if (((((uint8_t *)&t116))[24] != 0)) {
                    t117 = (*(gm_tilak255t5m34 *)(((uint8_t *)&t116) + 0));
                } else {
                    void * t118 = gm_l0;
                    gm_tl874t2atnlh3 t119 = gm_l1;
                    uint64_t t120 = gm_l13;
                    uint64_t t121 = gm_l14;
                    gm_t2lqu8jsj511d t122;
                    GM_ST(uint64_t, ((uint8_t *)&t122 + 0), t120);
                    GM_ST(uint64_t, ((uint8_t *)&t122 + 8), t121);
                    gm_tabb710d9mq2e t123;
                    gm_f_format_e7344u2((void *)&t123, (void *)NULL, cx, NULL, t122);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    if (((((uint8_t *)&t123))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t123) + 8, gm_srccp2mbkjs1vn4, 39, 62);
                    gm_b16_8 t124 = GM_LD(gm_b16_8, (((uint8_t *)&t123) + 8));
                    gm_b16_8 t125 = GM_LD(gm_b16_8, ((uint8_t *)&t124));
                    gm_t76123gfet73g t126;
                    gm_f_trap_qh84l9d((void *)&t126, (void *)NULL, cx, NULL, t118, t119, t125);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                    ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                    GM_ST(gm_t76123gfet73g, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t126);
                    goto gm_fdone;
                }
                gm_l15 = t117;
            }
            {
                uint8_t t128 = ((GM_LD(uint8_t, (((uint8_t *)&gm_l15) + 16) + 0) == ((uint8_t)2ULL)));
                if (!t128) goto ie127;
                uint8_t t129 = GM_LD(uint8_t, (((uint8_t *)&gm_l15) + 17));
                if (!t129) goto ie127;
                uint64_t t130 = GM_LD(uint64_t, (((uint8_t *)&gm_l15) + 0));
                uint64_t t131 = gm_l13;
                uint8_t t132 = ((t130 == t131));
                if (!t132) goto ie127;
                uint64_t t133 = GM_LD(uint64_t, (((uint8_t *)&gm_l15) + 8));
                uint64_t t134 = gm_l14;
                uint8_t t135 = ((t133 == t134));
                if (!t135) goto ie127;
                {
                    {
                        void * t138 = gm_l0;
                        void * t139 = (void *)(((uint8_t *)t138) + 184);
                        uint64_t t140 = gm_l13;
                        uint8_t t141 = gm_f_kill_3kcavfd((void *)NULL, cx, NULL, t139, t140);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                }
                {
                    gm_b0_1 t142;
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 16, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 24, 0);
                    ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 0;
                    GM_ST(gm_b0_1, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t142);
                    goto gm_fdone;
                }
                goto ix127;
ie127:;
ix127:;
            }
            {
                uint8_t t144 = ((GM_LD(uint8_t, (((uint8_t *)&gm_l15) + 16) + 0) == ((uint8_t)2ULL)));
                if (!t144) goto ie143;
                uint8_t t145 = GM_LD(uint8_t, (((uint8_t *)&gm_l15) + 17));
                uint8_t t146 = (uint8_t)!t145;
                if (!t146) goto ie143;
                {
                    void * t148 = gm_l0;
                    gm_tl874t2atnlh3 t149 = gm_l1;
                    uint64_t t150 = gm_l13;
                    gm_t4ce0jd3tdro4 t151;
                    GM_ST(uint64_t, ((uint8_t *)&t151 + 0), t150);
                    gm_tabb710d9mq2e t152;
                    gm_f_format_u3ioj90((void *)&t152, (void *)NULL, cx, NULL, t151);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    if (((((uint8_t *)&t152))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t152) + 8, gm_srccp2mbkjs1vn4, 44, 70);
                    gm_b16_8 t153 = GM_LD(gm_b16_8, (((uint8_t *)&t152) + 8));
                    gm_b16_8 t154 = GM_LD(gm_b16_8, ((uint8_t *)&t153));
                    gm_t76123gfet73g t155;
                    gm_f_trap_qh84l9d((void *)&t155, (void *)NULL, cx, NULL, t148, t149, t154);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                    ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                    GM_ST(gm_t76123gfet73g, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t155);
                    goto gm_fdone;
                }
                goto ix143;
ie143:;
ix143:;
            }
            {
                uint8_t t157 = ((GM_LD(uint8_t, (((uint8_t *)&gm_l15) + 16) + 0) == ((uint8_t)2ULL)));
                if (!t157) goto ie156;
                uint64_t t158 = GM_LD(uint64_t, (((uint8_t *)&gm_l15) + 0));
                uint64_t t159 = gm_l13;
                uint8_t t160 = ((t158 == t159));
                if (!t160) goto ie156;
                {
                    void * t162 = gm_l0;
                    gm_tl874t2atnlh3 t163 = gm_l1;
                    gm_tkpb4kplftgaa t164;
                    uint64_t t165 = gm_l13;
                    GM_ST(uint64_t, (uint8_t *)&t164 + 0, t165);
                    uint64_t t166 = GM_LD(uint64_t, (((uint8_t *)&gm_l15) + 8));
                    GM_ST(uint64_t, (uint8_t *)&t164 + 8, t166);
                    uint64_t t167 = gm_l14;
                    GM_ST(uint64_t, (uint8_t *)&t164 + 16, t167);
                    gm_tabb710d9mq2e t168;
                    gm_f_format_p0ej7hu((void *)&t168, (void *)NULL, cx, NULL, &t164);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    if (((((uint8_t *)&t168))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t168) + 8, gm_srccp2mbkjs1vn4, 46, 71);
                    gm_b16_8 t169 = GM_LD(gm_b16_8, (((uint8_t *)&t168) + 8));
                    gm_b16_8 t170 = GM_LD(gm_b16_8, ((uint8_t *)&t169));
                    gm_t76123gfet73g t171;
                    gm_f_trap_qh84l9d((void *)&t171, (void *)NULL, cx, NULL, t162, t163, t170);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                    ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                    GM_ST(gm_t76123gfet73g, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t171);
                    goto gm_fdone;
                }
                goto ix156;
ie156:;
ix156:;
            }
            {
                void * t172 = gm_l0;
                gm_tl874t2atnlh3 t173 = gm_l1;
                uint64_t t174 = gm_l13;
                uint64_t t175 = gm_l14;
                gm_t2lqu8jsj511d t176;
                GM_ST(uint64_t, ((uint8_t *)&t176 + 0), t174);
                GM_ST(uint64_t, ((uint8_t *)&t176 + 8), t175);
                gm_tabb710d9mq2e t177;
                gm_f_format_e7344u2((void *)&t177, (void *)NULL, cx, NULL, t176);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t177))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t177) + 8, gm_srccp2mbkjs1vn4, 47, 34);
                gm_b16_8 t178 = GM_LD(gm_b16_8, (((uint8_t *)&t177) + 8));
                gm_b16_8 t179 = GM_LD(gm_b16_8, ((uint8_t *)&t178));
                gm_t76123gfet73g t180;
                gm_f_trap_qh84l9d((void *)&t180, (void *)NULL, cx, NULL, t172, t173, t179);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                GM_ST(gm_t76123gfet73g, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t180);
                goto gm_fdone;
            }
            goto me3;
mn98:;
ma6:;
            uint8_t t184 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t184 != ((uint8_t)76ULL)) goto pa183;
            goto po182;
pa183:;
            uint8_t t186 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t186 != ((uint8_t)77ULL)) goto pa185;
            goto po182;
pa185:;
            goto mn181;
po182:;
            {
                void * t189 = gm_l0;
                uint8_t t190 = (!(GM_LD(uint8_t, (((uint8_t *)t189) + 88) + 0) == ((uint8_t)0ULL)));
                if (!t190) goto ie188;
                {
                    gm_b0_1 t192;
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 16, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 24, 0);
                    ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 0;
                    GM_ST(gm_b0_1, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t192);
                    goto gm_fdone;
                }
                goto ix188;
ie188:;
ix188:;
            }
            {
                void * t193 = gm_l0;
                uint64_t t194 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t195 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                if (t194 >= (uintptr_t)(t195)) gm_trap_index(t194, t195, gm_srccp2mbkjs1vn4, 51, 27);
                gm_b16_8 t196 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t194 * 16));
                gm_b16_8 t197 = GM_LD(gm_b16_8, ((uint8_t *)&t196));
                uint64_t t198 = gm_f_ptr_of_b77b4c9(t193, t197);
                gm_l16 = t198;
            }
            {
                void * t199 = gm_l0;
                uint64_t t200 = (uintptr_t)((uint64_t)1ULL);
                uint64_t t201 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                if (t200 >= (uintptr_t)(t201)) gm_trap_index(t200, t201, gm_srccp2mbkjs1vn4, 52, 31);
                gm_b16_8 t202 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t200 * 16));
                gm_b16_8 t203 = GM_LD(gm_b16_8, ((uint8_t *)&t202));
                uint64_t t204 = gm_f_ptr_of_b77b4c9(t199, t203);
                gm_l17 = t204;
            }
            {
                void * t205 = gm_l0;
                uint64_t t206 = (uintptr_t)((uint64_t)2ULL);
                uint64_t t207 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                if (t206 >= (uintptr_t)(t207)) gm_trap_index(t206, t207, gm_srccp2mbkjs1vn4, 53, 28);
                gm_b16_8 t208 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t206 * 16));
                gm_b16_8 t209 = GM_LD(gm_b16_8, ((uint8_t *)&t208));
                uint64_t t210 = gm_f_ptr_of_b77b4c9(t205, t209);
                gm_l18 = t210;
            }
            {
                void * t211 = gm_l0;
                gm_tl874t2atnlh3 t212 = gm_l1;
                uint64_t t213 = (uintptr_t)((uint64_t)3ULL);
                uint64_t t214 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                if (t213 >= (uintptr_t)(t214)) gm_trap_index(t213, t214, gm_srccp2mbkjs1vn4, 54, 45);
                gm_b16_8 t215 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t213 * 16));
                gm_b16_8 t216 = GM_LD(gm_b16_8, ((uint8_t *)&t215));
                gm_td0ept12e7q8b t217;
                gm_f_read_string_ttuk4gr((void *)&t217, (void *)NULL, cx, NULL, t211, t212, t216);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t217))[0] != 0)) {
                    gm_tcfa6l7reoosn t218;
                    GM_ST(uint8_t, (uint8_t *)&t218 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t218 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t218 + 4, 0);
                    ((uint8_t *)&t218)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t218 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t217) + 8)));
                    F.ret = t218;
                    goto gm_fret;
                }
                gm_b16_8 t219 = GM_LD(gm_b16_8, (((uint8_t *)&t217) + 8));
                gm_l19 = t219;
            }
            {
                gm_tgbgti82mf879 t220;
                gm_b16_8 t221 = gm_l19;
                GM_ST(gm_b16_8, (uint8_t *)&t220 + 0, t221);
                void * t222 = gm_l0;
                uint64_t t223 = (uintptr_t)((uint64_t)4ULL);
                uint64_t t224 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                if (t223 >= (uintptr_t)(t224)) gm_trap_index(t223, t224, gm_srccp2mbkjs1vn4, 55, 60);
                gm_b16_8 t225 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t223 * 16));
                gm_b16_8 t226 = GM_LD(gm_b16_8, ((uint8_t *)&t225));
                uint64_t t227 = gm_f_get_u64_tgpb2vh(t222, t226);
                GM_ST(uint64_t, (uint8_t *)&t220 + 16, t227);
                void * t228 = gm_l0;
                uint64_t t229 = (uintptr_t)((uint64_t)5ULL);
                uint64_t t230 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                if (t229 >= (uintptr_t)(t230)) gm_trap_index(t229, t230, gm_srccp2mbkjs1vn4, 55, 77);
                gm_b16_8 t231 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t229 * 16));
                gm_b16_8 t232 = GM_LD(gm_b16_8, ((uint8_t *)&t231));
                uint64_t t233 = gm_f_get_u64_tgpb2vh(t228, t232);
                GM_ST(uint64_t, (uint8_t *)&t220 + 24, t233);
                gm_tabb710d9mq2e t234;
                gm_f_format_bv13gdl((void *)&t234, (void *)NULL, cx, NULL, &t220);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t234))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t234) + 8, gm_srccp2mbkjs1vn4, 55, 21);
                gm_b16_8 t235 = GM_LD(gm_b16_8, (((uint8_t *)&t234) + 8));
                gm_l20 = t235;
            }
            {
                uint8_t t237 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l2) + 0) == ((uint8_t)76ULL)));
                if (!t237) goto ie236;
                {
                    void * t239 = gm_l0;
                    void * t240 = (void *)(((uint8_t *)t239) + 1296);
                    uint64_t t241 = gm_l16;
                    gm_tbccvikg6b96e t242;
                    uint64_t t243 = gm_l17;
                    GM_ST(uint64_t, (uint8_t *)&t242 + 0, t243);
                    uint64_t t244 = gm_l18;
                    GM_ST(uint64_t, (uint8_t *)&t242 + 8, t244);
                    gm_b16_8 t245 = gm_l20;
                    gm_b16_8 t246 = GM_LD(gm_b16_8, ((uint8_t *)&t245));
                    GM_ST(gm_b16_8, (uint8_t *)&t242 + 16, t246);
                    gm_tftb1odc739t5 t247 = gm_f_put_64ej4om(t240, t241, &t242);
                    if (((((uint8_t *)&t247))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t247) + 1, gm_srccp2mbkjs1vn4, 57, 17);
                    gm_b0_1 t248 = GM_LD(gm_b0_1, (((uint8_t *)&t247) + 1));
                }
                {
                    gm_b0_1 t249;
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 16, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 24, 0);
                    ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 0;
                    GM_ST(gm_b0_1, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t249);
                    goto gm_fdone;
                }
                goto ix236;
ie236:;
ix236:;
            }
            {
                void * t250 = gm_l0;
                void * t251 = (void *)(((uint8_t *)t250) + 1296);
                uint64_t t252 = gm_l16;
                gm_ttfi3b3ccbpes t253;
                gm_f_get_hd4d6j0((void *)&t253, t251, t252);
                gm_tbccvikg6b96e t254;
                if (((((uint8_t *)&t253))[32] != 0)) {
                    t254 = (*(gm_tbccvikg6b96e *)(((uint8_t *)&t253) + 0));
                } else {
                    gm_b0_1 t255;
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 16, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 24, 0);
                    ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 0;
                    GM_ST(gm_b0_1, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t255);
                    goto gm_fdone;
                }
                gm_l21 = t254;
            }
            {
                {
                    void * t257 = gm_l0;
                    void * t258 = (void *)(((uint8_t *)t257) + 1296);
                    uint64_t t259 = gm_l16;
                    gm_ttfi3b3ccbpes t260;
                    gm_f_remove_gjirbtf((void *)&t260, t258, t259);
                }
            }
            {
                uint8_t t262 = 0;
                uint64_t t263 = GM_LD(uint64_t, (((uint8_t *)&gm_l21) + 0));
                uint64_t t264 = gm_l17;
                uint8_t t265 = (!(t263 == t264));
                if (!t265) {
                    uint64_t t266 = GM_LD(uint64_t, (((uint8_t *)&gm_l21) + 8));
                    uint64_t t267 = gm_l18;
                    uint8_t t268 = (!(t266 == t267));
                    t262 = t268;
                } else t262 = 1;
                if (!t262) goto ie261;
                {
                    void * t270 = gm_l0;
                    gm_tl874t2atnlh3 t271 = gm_l1;
                    gm_t6j6smj7qoctu t272;
                    gm_b16_8 t273 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l21) + 16));
                    GM_ST(gm_b16_8, (uint8_t *)&t272 + 0, t273);
                    gm_b16_8 t274 = gm_l20;
                    GM_ST(gm_b16_8, (uint8_t *)&t272 + 16, t274);
                    gm_tabb710d9mq2e t275;
                    gm_f_format_d613p2h((void *)&t275, (void *)NULL, cx, NULL, &t272);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    if (((((uint8_t *)&t275))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t275) + 8, gm_srccp2mbkjs1vn4, 62, 82);
                    gm_b16_8 t276 = GM_LD(gm_b16_8, (((uint8_t *)&t275) + 8));
                    gm_b16_8 t277 = GM_LD(gm_b16_8, ((uint8_t *)&t276));
                    gm_t76123gfet73g t278;
                    gm_f_trap_qh84l9d((void *)&t278, (void *)NULL, cx, NULL, t270, t271, t277);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                    ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                    GM_ST(gm_t76123gfet73g, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t278);
                    goto gm_fdone;
                }
                goto ix261;
ie261:;
ix261:;
            }
            goto me3;
mn181:;
ma7:;
            uint8_t t282 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t282 != ((uint8_t)64ULL)) goto pa281;
            goto po280;
pa281:;
            uint8_t t284 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t284 != ((uint8_t)65ULL)) goto pa283;
            goto po280;
pa283:;
            uint8_t t286 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t286 != ((uint8_t)66ULL)) goto pa285;
            goto po280;
pa285:;
            uint8_t t288 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t288 != ((uint8_t)67ULL)) goto pa287;
            goto po280;
pa287:;
            uint8_t t290 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t290 != ((uint8_t)68ULL)) goto pa289;
            goto po280;
pa289:;
            uint8_t t292 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t292 != ((uint8_t)70ULL)) goto pa291;
            goto po280;
pa291:;
            uint8_t t294 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t294 != ((uint8_t)71ULL)) goto pa293;
            goto po280;
pa293:;
            goto mn279;
po280:;
            {
                void * t296 = gm_l0;
                uint64_t t297 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t298 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                if (t297 >= (uintptr_t)(t298)) gm_trap_index(t297, t298, gm_srccp2mbkjs1vn4, 65, 30);
                gm_b16_8 t299 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t297 * 16));
                gm_b16_8 t300 = GM_LD(gm_b16_8, ((uint8_t *)&t299));
                uint64_t t301 = gm_f_ptr_of_b77b4c9(t296, t300);
                gm_l22 = t301;
            }
            {
                void * t303 = gm_l0;
                uint64_t t304 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t305 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                if (t304 >= (uintptr_t)(t305)) gm_trap_index(t304, t305, gm_srccp2mbkjs1vn4, 66, 35);
                void * t306 = GM_LD(void *, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t304 * 8));
                gm_t88rcucg1b7uk t307 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t306) + 96));
                gm_t53at80pfdus3 t308;
                gm_f_kind_htv6093((void *)&t308, t303, GM_CONV(gm_csguankko4lmhd, t307));
                uint8_t t309 = GM_LD(uint8_t, (((uint8_t *)&t308) + 0));
                if (t309 != ((uint8_t)8ULL)) goto ie302;
                gm_t88rcucg1b7uk t310 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t308) + 8) + 4));
                gm_l23 = t310;
                {
                    void * t312 = gm_l0;
                    gm_t88rcucg1b7uk t313 = gm_l23;
                    uint64_t t314 = gm_f_align_15fjgq1((void *)NULL, cx, NULL, t312, t313);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l24 = t314;
                }
                {
                    void * t315 = gm_l0;
                    void * t316 = gm_l0;
                    gm_t88rcucg1b7uk t317 = gm_l23;
                    uint64_t t318 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t316, t317);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_b16_8 t319 = gm_f_tmp_8k6u9c9((void *)NULL, cx, NULL, t315, t318);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l25 = t319;
                }
                {
                    void * t320 = gm_l0;
                    gm_tl874t2atnlh3 t321 = gm_l1;
                    uint64_t t322 = gm_l22;
                    uint64_t t323 = gm_l24;
                    gm_b16_8 t324 = gm_l25;
                    gm_tcfa6l7reoosn t325;
                    gm_f_load_at_6u81jcc((void *)&t325, (void *)NULL, cx, NULL, t320, t321, t322, t323, t324);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    if (((((uint8_t *)&t325))[0] != 0)) {
                        gm_tcfa6l7reoosn t326;
                        GM_ST(uint8_t, (uint8_t *)&t326 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t326 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t326 + 4, 0);
                        ((uint8_t *)&t326)[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t326 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t325) + 8)));
                        F.ret = t326;
                        goto gm_fret;
                    }
                    gm_b0_1 t327 = GM_LD(gm_b0_1, (((uint8_t *)&t325) + 8));
                }
                {
                    uint8_t t336 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    switch (t336) { case ((uint8_t)0ULL): goto ma334; case ((uint8_t)1ULL): goto ma334; case ((uint8_t)2ULL): goto ma334; case ((uint8_t)3ULL): goto ma334; case ((uint8_t)4ULL): goto ma334; case ((uint8_t)5ULL): goto ma334; case ((uint8_t)6ULL): goto ma334; case ((uint8_t)7ULL): goto ma334; case ((uint8_t)8ULL): goto ma334; case ((uint8_t)9ULL): goto ma334; case ((uint8_t)10ULL): goto ma334; case ((uint8_t)11ULL): goto ma334; case ((uint8_t)12ULL): goto ma334; case ((uint8_t)13ULL): goto ma334; case ((uint8_t)14ULL): goto ma334; case ((uint8_t)15ULL): goto ma334; case ((uint8_t)16ULL): goto ma334; case ((uint8_t)17ULL): goto ma334; case ((uint8_t)18ULL): goto ma334; case ((uint8_t)19ULL): goto ma334; case ((uint8_t)20ULL): goto ma334; case ((uint8_t)21ULL): goto ma334; case ((uint8_t)22ULL): goto ma334; case ((uint8_t)23ULL): goto ma334; case ((uint8_t)24ULL): goto ma334; case ((uint8_t)25ULL): goto ma334; case ((uint8_t)26ULL): goto ma334; case ((uint8_t)27ULL): goto ma334; case ((uint8_t)28ULL): goto ma334; case ((uint8_t)29ULL): goto ma334; case ((uint8_t)30ULL): goto ma334; case ((uint8_t)31ULL): goto ma334; case ((uint8_t)32ULL): goto ma334; case ((uint8_t)33ULL): goto ma334; case ((uint8_t)34ULL): goto ma334; case ((uint8_t)35ULL): goto ma334; case ((uint8_t)36ULL): goto ma334; case ((uint8_t)37ULL): goto ma334; case ((uint8_t)38ULL): goto ma334; case ((uint8_t)39ULL): goto ma334; case ((uint8_t)40ULL): goto ma334; case ((uint8_t)41ULL): goto ma334; case ((uint8_t)42ULL): goto ma334; case ((uint8_t)43ULL): goto ma334; case ((uint8_t)44ULL): goto ma334; case ((uint8_t)45ULL): goto ma334; case ((uint8_t)46ULL): goto ma334; case ((uint8_t)47ULL): goto ma334; case ((uint8_t)48ULL): goto ma334; case ((uint8_t)49ULL): goto ma334; case ((uint8_t)50ULL): goto ma334; case ((uint8_t)51ULL): goto ma334; case ((uint8_t)52ULL): goto ma334; case ((uint8_t)53ULL): goto ma334; case ((uint8_t)54ULL): goto ma334; case ((uint8_t)55ULL): goto ma334; case ((uint8_t)56ULL): goto ma334; case ((uint8_t)57ULL): goto ma334; case ((uint8_t)58ULL): goto ma334; case ((uint8_t)59ULL): goto ma334; case ((uint8_t)60ULL): goto ma334; case ((uint8_t)61ULL): goto ma334; case ((uint8_t)62ULL): goto ma334; case ((uint8_t)63ULL): goto ma334; case ((uint8_t)64ULL): goto ma329; case ((uint8_t)65ULL): goto ma330; case ((uint8_t)66ULL): goto ma331; case ((uint8_t)67ULL): goto ma332; case ((uint8_t)68ULL): goto ma333; case ((uint8_t)69ULL): goto ma334; case ((uint8_t)70ULL): goto ma329; case ((uint8_t)71ULL): goto ma330; case ((uint8_t)72ULL): goto ma334; case ((uint8_t)73ULL): goto ma334; case ((uint8_t)74ULL): goto ma334; case ((uint8_t)75ULL): goto ma334; case ((uint8_t)76ULL): goto ma334; case ((uint8_t)77ULL): goto ma334; case ((uint8_t)78ULL): goto ma334; case ((uint8_t)79ULL): goto ma334; case ((uint8_t)80ULL): goto ma334; case ((uint8_t)81ULL): goto ma334; default: goto ma334; }
ma329:;
                    uint8_t t340 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    if (t340 != ((uint8_t)64ULL)) goto pa339;
                    goto po338;
pa339:;
                    uint8_t t342 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    if (t342 != ((uint8_t)70ULL)) goto pa341;
                    goto po338;
pa341:;
                    goto mn337;
po338:;
                    gm_b16_8 t343 = gm_l6;
                    gm_b16_8 t344 = gm_l25;
                    gm_b16_8 t345 = GM_LD(gm_b16_8, ((uint8_t *)&t344));
                    gm_f_copy_from_f7j6p05(t343, t345);
                    goto me328;
mn337:;
ma330:;
                    uint8_t t349 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    if (t349 != ((uint8_t)65ULL)) goto pa348;
                    goto po347;
pa348:;
                    uint8_t t351 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    if (t351 != ((uint8_t)71ULL)) goto pa350;
                    goto po347;
pa350:;
                    goto mn346;
po347:;
                    void * t352 = gm_l0;
                    gm_tl874t2atnlh3 t353 = gm_l1;
                    uint64_t t354 = gm_l22;
                    uint64_t t355 = gm_l24;
                    uint64_t t356 = (uintptr_t)((uint64_t)1ULL);
                    uint64_t t357 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                    if (t356 >= (uintptr_t)(t357)) gm_trap_index(t356, t357, gm_srccp2mbkjs1vn4, 72, 80);
                    gm_b16_8 t358 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t356 * 16));
                    gm_b16_8 t359 = GM_LD(gm_b16_8, ((uint8_t *)&t358));
                    gm_tcfa6l7reoosn t360;
                    gm_f_store_at_ktabiji((void *)&t360, (void *)NULL, cx, NULL, t352, t353, t354, t355, t359);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    if (((((uint8_t *)&t360))[0] != 0)) {
                        gm_tcfa6l7reoosn t361;
                        GM_ST(uint8_t, (uint8_t *)&t361 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t361 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t361 + 4, 0);
                        ((uint8_t *)&t361)[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t361 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t360) + 8)));
                        F.ret = t361;
                        goto gm_fret;
                    }
                    gm_b0_1 t362 = GM_LD(gm_b0_1, (((uint8_t *)&t360) + 8));
                    goto me328;
mn346:;
ma331:;
                    uint8_t t364 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    if (t364 != ((uint8_t)66ULL)) goto mn363;
                    {
                        void * t366 = gm_l0;
                        gm_tl874t2atnlh3 t367 = gm_l1;
                        uint64_t t368 = gm_l22;
                        uint64_t t369 = gm_l24;
                        uint64_t t370 = (uintptr_t)((uint64_t)1ULL);
                        uint64_t t371 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                        if (t370 >= (uintptr_t)(t371)) gm_trap_index(t370, t371, gm_srccp2mbkjs1vn4, 74, 52);
                        gm_b16_8 t372 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t370 * 16));
                        gm_b16_8 t373 = GM_LD(gm_b16_8, ((uint8_t *)&t372));
                        gm_tcfa6l7reoosn t374;
                        gm_f_store_at_ktabiji((void *)&t374, (void *)NULL, cx, NULL, t366, t367, t368, t369, t373);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        if (((((uint8_t *)&t374))[0] != 0)) {
                            gm_tcfa6l7reoosn t375;
                            GM_ST(uint8_t, (uint8_t *)&t375 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t375 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t375 + 4, 0);
                            ((uint8_t *)&t375)[0] = 1;
                            GM_ST(gm_t76123gfet73g, ((uint8_t *)&t375 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t374) + 8)));
                            F.ret = t375;
                            goto gm_fret;
                        }
                        gm_b0_1 t376 = GM_LD(gm_b0_1, (((uint8_t *)&t374) + 8));
                    }
                    {
                        gm_b16_8 t377 = gm_l6;
                        gm_b16_8 t378 = gm_l25;
                        gm_b16_8 t379 = GM_LD(gm_b16_8, ((uint8_t *)&t378));
                        gm_f_copy_from_f7j6p05(t377, t379);
                    }
                    goto me328;
mn363:;
ma332:;
                    uint8_t t381 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    if (t381 != ((uint8_t)67ULL)) goto mn380;
                    {
                        void * t384 = gm_l0;
                        gm_tl874t2atnlh3 t385 = gm_l1;
                        gm_t88rcucg1b7uk t386 = gm_l23;
                        gm_b16_8 t387 = gm_l25;
                        uint64_t t388 = (uintptr_t)((uint64_t)1ULL);
                        uint64_t t389 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                        if (t388 >= (uintptr_t)(t389)) gm_trap_index(t388, t389, gm_srccp2mbkjs1vn4, 78, 51);
                        gm_b16_8 t390 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t388 * 16));
                        gm_t1ec8br9ddkav t391;
                        gm_f_equal_e5vf82h((void *)&t391, (void *)NULL, cx, NULL, t384, t385, t386, t387, t390);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        if (((((uint8_t *)&t391))[0] != 0)) {
                            gm_tcfa6l7reoosn t392;
                            GM_ST(uint8_t, (uint8_t *)&t392 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t392 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t392 + 4, 0);
                            ((uint8_t *)&t392)[0] = 1;
                            GM_ST(gm_t76123gfet73g, ((uint8_t *)&t392 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t391) + 8)));
                            F.ret = t392;
                            goto gm_fret;
                        }
                        uint8_t t393 = GM_LD(uint8_t, (((uint8_t *)&t391) + 8));
                        if (!t393) goto ie383;
                        {
                            void * t395 = gm_l0;
                            gm_tl874t2atnlh3 t396 = gm_l1;
                            uint64_t t397 = gm_l22;
                            uint64_t t398 = gm_l24;
                            uint64_t t399 = (uintptr_t)((uint64_t)2ULL);
                            uint64_t t400 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                            if (t399 >= (uintptr_t)(t400)) gm_trap_index(t399, t400, gm_srccp2mbkjs1vn4, 79, 56);
                            gm_b16_8 t401 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t399 * 16));
                            gm_b16_8 t402 = GM_LD(gm_b16_8, ((uint8_t *)&t401));
                            gm_tcfa6l7reoosn t403;
                            gm_f_store_at_ktabiji((void *)&t403, (void *)NULL, cx, NULL, t395, t396, t397, t398, t402);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            if (((((uint8_t *)&t403))[0] != 0)) {
                                gm_tcfa6l7reoosn t404;
                                GM_ST(uint8_t, (uint8_t *)&t404 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t404 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t404 + 4, 0);
                                ((uint8_t *)&t404)[0] = 1;
                                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t404 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t403) + 8)));
                                F.ret = t404;
                                goto gm_fret;
                            }
                            gm_b0_1 t405 = GM_LD(gm_b0_1, (((uint8_t *)&t403) + 8));
                        }
                        {
                            gm_b16_8 t406 = gm_l6;
                            gm_f_fill_abmf5g5(t406, ((uint8_t)0ULL));
                        }
                        goto ix383;
ie383:;
                        {
                            void * t408 = gm_l0;
                            gm_t88rcucg1b7uk t409 = gm_l5;
                            gm_b16_8 t410 = gm_l6;
                            gm_b16_8 t411 = gm_l25;
                            gm_b16_8 t412 = GM_LD(gm_b16_8, ((uint8_t *)&t411));
                            gm_b0_1 t413 = gm_f_make_some_tvb9ubk((void *)NULL, cx, NULL, t408, t409, t410, t412);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                        }
ix383:;
                    }
                    goto me328;
mn380:;
ma333:;
                    uint8_t t415 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    if (t415 != ((uint8_t)68ULL)) goto mn414;
                    gm_t9qkh0rsooc8d t416 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l2) + 4));
                    gm_l26 = t416;
                    {
                        void * t418 = gm_l0;
                        gm_t88rcucg1b7uk t419 = gm_l23;
                        gm_b16_8 t420 = gm_l25;
                        gm_b16_8 t421 = GM_LD(gm_b16_8, ((uint8_t *)&t420));
                        unsigned __int128 t422 = gm_f_get_int_pg7p4gc(t418, GM_CONV(gm_csguankko4lmhd, t419), t421);
                        gm_l27 = t422;
                    }
                    {
                        void * t423 = gm_l0;
                        gm_t88rcucg1b7uk t424 = gm_l23;
                        uint64_t t425 = (uintptr_t)((uint64_t)1ULL);
                        uint64_t t426 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                        if (t425 >= (uintptr_t)(t426)) gm_trap_index(t425, t426, gm_srccp2mbkjs1vn4, 87, 40);
                        gm_b16_8 t427 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t425 * 16));
                        gm_b16_8 t428 = GM_LD(gm_b16_8, ((uint8_t *)&t427));
                        unsigned __int128 t429 = gm_f_get_int_pg7p4gc(t423, GM_CONV(gm_csguankko4lmhd, t424), t428);
                        gm_l28 = t429;
                    }
                    {
                        void * t430 = gm_l0;
                        gm_t88rcucg1b7uk t431 = gm_l23;
                        gm_tg5lql6itufk7 t432 = gm_f_int_info_ulcevdv(t430, GM_CONV(gm_csguankko4lmhd, t431));
                        gm_ttovhfee8mf28 t433;
                        if (((((uint8_t *)&t432))[8] != 0)) {
                            gm_ttovhfee8mf28 t434 = GM_LD(gm_ttovhfee8mf28, (((uint8_t *)&t432) + 0));
                            t433 = t434;
                        } else {
                            gm_ttovhfee8mf28 t435;
                            GM_ST(uint8_t, (uint8_t *)&t435 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t435 + 2, 0);
                            GM_ST(uint8_t, ((uint8_t *)&t435 + 0), ((uint8_t)0));
                            GM_ST(uint32_t, ((uint8_t *)&t435 + 4), ((uint32_t)0ULL));
                            t433 = t435;
                        }
                        uint8_t t436 = GM_LD(uint8_t, (((uint8_t *)&t433) + 0));
                        gm_l29 = t436;
                    }
                    {
                        unsigned __int128 t437;
                        uint8_t t447 = GM_LD(uint8_t, (((uint8_t *)&gm_l26) + 0));
                        switch (t447) { case ((uint8_t)0ULL): goto ma439; case ((uint8_t)1ULL): goto ma440; case ((uint8_t)2ULL): goto ma441; case ((uint8_t)3ULL): goto ma442; case ((uint8_t)4ULL): goto ma443; case ((uint8_t)5ULL): goto ma444; case ((uint8_t)6ULL): goto ma445; default: goto ma446; }
ma439:;
                        uint8_t t449 = GM_LD(uint8_t, (((uint8_t *)&gm_l26) + 0));
                        if (t449 != ((uint8_t)0ULL)) goto mn448;
                        unsigned __int128 t450 = gm_l27;
                        unsigned __int128 t451 = gm_l28;
                        unsigned __int128 r452; bool o453 = __builtin_add_overflow(t450, t451, &r452);
                        unsigned __int128 t454 = r452;
                        t437 = t454;
                        goto me438;
mn448:;
ma440:;
                        uint8_t t456 = GM_LD(uint8_t, (((uint8_t *)&gm_l26) + 0));
                        if (t456 != ((uint8_t)1ULL)) goto mn455;
                        unsigned __int128 t457 = gm_l27;
                        unsigned __int128 t458 = gm_l28;
                        unsigned __int128 r459; bool o460 = __builtin_sub_overflow(t457, t458, &r459);
                        unsigned __int128 t461 = r459;
                        t437 = t461;
                        goto me438;
mn455:;
ma441:;
                        uint8_t t463 = GM_LD(uint8_t, (((uint8_t *)&gm_l26) + 0));
                        if (t463 != ((uint8_t)2ULL)) goto mn462;
                        unsigned __int128 t464 = gm_l27;
                        unsigned __int128 t465 = gm_l28;
                        unsigned __int128 t466 = (unsigned __int128)(t464 & t465);
                        t437 = t466;
                        goto me438;
mn462:;
ma442:;
                        uint8_t t468 = GM_LD(uint8_t, (((uint8_t *)&gm_l26) + 0));
                        if (t468 != ((uint8_t)3ULL)) goto mn467;
                        unsigned __int128 t469 = gm_l27;
                        unsigned __int128 t470 = gm_l28;
                        unsigned __int128 t471 = (unsigned __int128)(t469 | t470);
                        t437 = t471;
                        goto me438;
mn467:;
ma443:;
                        uint8_t t473 = GM_LD(uint8_t, (((uint8_t *)&gm_l26) + 0));
                        if (t473 != ((uint8_t)4ULL)) goto mn472;
                        unsigned __int128 t474 = gm_l27;
                        unsigned __int128 t475 = gm_l28;
                        unsigned __int128 t476 = (unsigned __int128)(t474 ^ t475);
                        t437 = t476;
                        goto me438;
mn472:;
ma444:;
                        uint8_t t478 = GM_LD(uint8_t, (((uint8_t *)&gm_l26) + 0));
                        if (t478 != ((uint8_t)5ULL)) goto mn477;
                        unsigned __int128 t479;
                        uint8_t t481 = 0;
                        uint8_t t482 = 0;
                        uint8_t t483 = gm_l29;
                        if (t483) {
                            unsigned __int128 t484 = gm_l28;
                            __int128 t485;
                            memset(&t485, 0, sizeof t485); memcpy(&t485, ((uint8_t *)&t484), sizeof t485 < 16 ? sizeof t485 : 16);
                            unsigned __int128 t486 = gm_l27;
                            __int128 t487;
                            memset(&t487, 0, sizeof t487); memcpy(&t487, ((uint8_t *)&t486), sizeof t487 < 16 ? sizeof t487 : 16);
                            uint8_t t488 = (t485 > t487);
                            t482 = t488;
                        } else t482 = 0;
                        if (!t482) {
                            uint8_t t489 = 0;
                            uint8_t t490 = gm_l29;
                            uint8_t t491 = (uint8_t)!t490;
                            if (t491) {
                                unsigned __int128 t492 = gm_l28;
                                unsigned __int128 t493 = gm_l27;
                                uint8_t t494 = (t492 > t493);
                                t489 = t494;
                            } else t489 = 0;
                            t481 = t489;
                        } else t481 = 1;
                        if (!t481) goto ie480;
                        unsigned __int128 t496 = gm_l28;
                        unsigned __int128 t497 = t496;
                        t479 = t497;
                        goto ix480;
ie480:;
                        unsigned __int128 t499 = gm_l27;
                        unsigned __int128 t500 = t499;
                        t479 = t500;
ix480:;
                        t437 = t479;
                        goto me438;
mn477:;
ma445:;
                        uint8_t t502 = GM_LD(uint8_t, (((uint8_t *)&gm_l26) + 0));
                        if (t502 != ((uint8_t)6ULL)) goto mn501;
                        unsigned __int128 t503;
                        uint8_t t505 = 0;
                        uint8_t t506 = 0;
                        uint8_t t507 = gm_l29;
                        if (t507) {
                            unsigned __int128 t508 = gm_l28;
                            __int128 t509;
                            memset(&t509, 0, sizeof t509); memcpy(&t509, ((uint8_t *)&t508), sizeof t509 < 16 ? sizeof t509 : 16);
                            unsigned __int128 t510 = gm_l27;
                            __int128 t511;
                            memset(&t511, 0, sizeof t511); memcpy(&t511, ((uint8_t *)&t510), sizeof t511 < 16 ? sizeof t511 : 16);
                            uint8_t t512 = (t509 < t511);
                            t506 = t512;
                        } else t506 = 0;
                        if (!t506) {
                            uint8_t t513 = 0;
                            uint8_t t514 = gm_l29;
                            uint8_t t515 = (uint8_t)!t514;
                            if (t515) {
                                unsigned __int128 t516 = gm_l28;
                                unsigned __int128 t517 = gm_l27;
                                uint8_t t518 = (t516 < t517);
                                t513 = t518;
                            } else t513 = 0;
                            t505 = t513;
                        } else t505 = 1;
                        if (!t505) goto ie504;
                        unsigned __int128 t520 = gm_l28;
                        unsigned __int128 t521 = t520;
                        t503 = t521;
                        goto ix504;
ie504:;
                        unsigned __int128 t523 = gm_l27;
                        unsigned __int128 t524 = t523;
                        t503 = t524;
ix504:;
                        t437 = t503;
                        goto me438;
mn501:;
ma446:;
                        gm_trap("no match arm matched the scrutinee", gm_srccp2mbkjs1vn4, 89, 31);
me438:;
                        gm_l30 = t437;
                    }
                    {
                        void * t525 = gm_l0;
                        void * t526 = gm_l0;
                        gm_t88rcucg1b7uk t527 = gm_l23;
                        uint64_t t528 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t526, t527);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_b16_8 t529 = gm_f_tmp_8k6u9c9((void *)NULL, cx, NULL, t525, t528);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_l31 = t529;
                    }
                    {
                        void * t530 = gm_l0;
                        gm_b16_8 t531 = gm_l31;
                        unsigned __int128 t532 = gm_l30;
                        gm_f_put_uint_cddin99(t530, t531, t532);
                    }
                    {
                        void * t533 = gm_l0;
                        gm_tl874t2atnlh3 t534 = gm_l1;
                        uint64_t t535 = gm_l22;
                        uint64_t t536 = gm_l24;
                        gm_b16_8 t537 = gm_l31;
                        gm_b16_8 t538 = GM_LD(gm_b16_8, ((uint8_t *)&t537));
                        gm_tcfa6l7reoosn t539;
                        gm_f_store_at_ktabiji((void *)&t539, (void *)NULL, cx, NULL, t533, t534, t535, t536, t538);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        if (((((uint8_t *)&t539))[0] != 0)) {
                            gm_tcfa6l7reoosn t540;
                            GM_ST(uint8_t, (uint8_t *)&t540 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t540 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t540 + 4, 0);
                            ((uint8_t *)&t540)[0] = 1;
                            GM_ST(gm_t76123gfet73g, ((uint8_t *)&t540 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t539) + 8)));
                            F.ret = t540;
                            goto gm_fret;
                        }
                        gm_b0_1 t541 = GM_LD(gm_b0_1, (((uint8_t *)&t539) + 8));
                    }
                    {
                        gm_b16_8 t542 = gm_l6;
                        gm_b16_8 t543 = gm_l25;
                        gm_b16_8 t544 = GM_LD(gm_b16_8, ((uint8_t *)&t543));
                        gm_f_copy_from_f7j6p05(t542, t544);
                    }
                    goto me328;
mn414:;
ma334:;
                    goto me328;
mn545:;
ma335:;
                    gm_trap("no match arm matched the scrutinee", gm_srccp2mbkjs1vn4, 70, 13);
me328:;
                }
                goto ix302;
ie302:;
                {
                    void * t548 = gm_l0;
                    gm_tl874t2atnlh3 t549 = gm_l1;
                    gm_b16_8 t550;
                    GM_ST(void *, (uint8_t *)&t550, (void *)(gm_s8pst1ovjhtbk));
                    GM_ST(uintptr_t, (uint8_t *)&t550 + 8, (uintptr_t)(70));
                    gm_t76123gfet73g t551;
                    gm_f_trap_qh84l9d((void *)&t551, (void *)NULL, cx, NULL, t548, t549, t550);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                    ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                    GM_ST(gm_t76123gfet73g, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t551);
                    goto gm_fdone;
                }
ix302:;
            }
            goto me3;
mn279:;
ma8:;
            uint8_t t553 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t553 != ((uint8_t)69ULL)) goto mn552;
            goto me3;
mn552:;
ma9:;
            uint8_t t556 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t556 != ((uint8_t)78ULL)) goto mn555;
            {
                void * t559 = gm_l0;
                uint64_t t560 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t561 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                if (t560 >= (uintptr_t)(t561)) gm_trap_index(t560, t561, gm_srccp2mbkjs1vn4, 108, 39);
                void * t562 = GM_LD(void *, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t560 * 8));
                gm_t88rcucg1b7uk t563 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t562) + 96));
                gm_t53at80pfdus3 t564;
                gm_f_kind_htv6093((void *)&t564, t559, GM_CONV(gm_csguankko4lmhd, t563));
                uint8_t t565 = GM_LD(uint8_t, (((uint8_t *)&t564) + 0));
                if (t565 != ((uint8_t)10ULL)) goto ie558;
                gm_t88rcucg1b7uk t566 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t564) + 8) + 4));
                gm_l32 = t566;
                {
                    void * t568 = gm_l0;
                    uint64_t t569 = (uintptr_t)((uint64_t)0ULL);
                    uint64_t t570 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                    if (t569 >= (uintptr_t)(t570)) gm_trap_index(t569, t570, gm_srccp2mbkjs1vn4, 109, 32);
                    gm_b16_8 t571 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t569 * 16));
                    gm_b16_8 t572 = GM_LD(gm_b16_8, ((uint8_t *)&t571));
                    gm_t2lqu8jsj511d t573 = gm_f_slice_parts_1se5hu4(t568, t572);
                    gm_l33 = t573;
                }
                {
                    void * t574 = gm_l0;
                    uint64_t t575 = (uintptr_t)((uint64_t)1ULL);
                    uint64_t t576 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                    if (t575 >= (uintptr_t)(t576)) gm_trap_index(t575, t576, gm_srccp2mbkjs1vn4, 110, 32);
                    gm_b16_8 t577 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t575 * 16));
                    gm_b16_8 t578 = GM_LD(gm_b16_8, ((uint8_t *)&t577));
                    gm_t2lqu8jsj511d t579 = gm_f_slice_parts_1se5hu4(t574, t578);
                    gm_l34 = t579;
                }
                {
                    uint64_t t580 = GM_LD(uint64_t, (((uint8_t *)&gm_l33) + 8));
                    void * t581 = gm_l0;
                    gm_t88rcucg1b7uk t582 = gm_l32;
                    uint64_t t583 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t581, t582);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint64_t r584; bool o585 = __builtin_mul_overflow(t580, t583, &r584);
                    if (o585) gm_trap("integer overflow in `*`", gm_srccp2mbkjs1vn4, 111, 18);
                    uint64_t t586 = r584;
                    gm_l35 = t586;
                }
                {
                    uint64_t t588 = gm_l35;
                    uint8_t t589 = ((t588 == ((uint64_t)0ULL)));
                    if (!t589) goto ie587;
                    {
                        gm_b0_1 t591;
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 16, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 24, 0);
                        ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 0;
                        GM_ST(gm_b0_1, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t591);
                        goto gm_fdone;
                    }
                    goto ix587;
ie587:;
ix587:;
                }
                {
                    void * t592 = gm_l0;
                    void * t593 = (void *)(((uint8_t *)t592) + 184);
                    uint64_t t594 = GM_LD(uint64_t, (((uint8_t *)&gm_l34) + 0));
                    uint64_t t595 = gm_l35;
                    gm_t934k1j7u70ct t596;
                    gm_f_read_u6r4vns((void *)&t596, t593, t594, t595, ((uint64_t)1ULL));
                    gm_b16_8 t597;
                    if (!((((uint8_t *)&t596))[0] != 0)) {
                        gm_b16_8 t598 = GM_LD(gm_b16_8, (((uint8_t *)&t596) + 8));
                        t597 = t598;
                    } else {
                        F.l36 = (*(gm_tpekhfs32op0f *)(((uint8_t *)&t596) + 8));
                        void * t599 = gm_l0;
                        gm_tl874t2atnlh3 t600 = gm_l1;
                        gm_t76123gfet73g t601;
                        gm_f_mem_trap_ecogko0((void *)&t601, (void *)NULL, cx, NULL, t599, t600, &(*(gm_tpekhfs32op0f *)((uint8_t *)&F.l36)));
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                        ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                        GM_ST(gm_t76123gfet73g, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t601);
                        goto gm_fdone;
                    }
                    gm_l37 = t597;
                }
                {
                    void * t602 = gm_l0;
                    uint64_t t603 = gm_l35;
                    gm_b16_8 t604 = gm_f_tmp_8k6u9c9((void *)NULL, cx, NULL, t602, t603);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l38 = t604;
                }
                {
                    gm_b16_8 t605 = gm_l38;
                    gm_b16_8 t606 = gm_l37;
                    gm_b16_8 t607 = GM_LD(gm_b16_8, ((uint8_t *)&t606));
                    gm_f_copy_from_f7j6p05(t605, t607);
                }
                {
                    void * t608 = gm_l0;
                    void * t609 = (void *)(((uint8_t *)t608) + 184);
                    uint64_t t610 = GM_LD(uint64_t, (((uint8_t *)&gm_l33) + 0));
                    gm_b16_8 t611 = gm_l38;
                    gm_b16_8 t612 = GM_LD(gm_b16_8, ((uint8_t *)&t611));
                    gm_tgfdnufetdnfc t613;
                    gm_f_write_5gub0ul((void *)&t613, t609, t610, t612, ((uint64_t)1ULL));
                    gm_b0_1 t614;
                    if (!((((uint8_t *)&t613))[0] != 0)) {
                        gm_b0_1 t615 = GM_LD(gm_b0_1, (((uint8_t *)&t613) + 8));
                        t614 = t615;
                    } else {
                        F.l39 = (*(gm_tpekhfs32op0f *)(((uint8_t *)&t613) + 8));
                        void * t616 = gm_l0;
                        gm_tl874t2atnlh3 t617 = gm_l1;
                        gm_t76123gfet73g t618;
                        gm_f_mem_trap_ecogko0((void *)&t618, (void *)NULL, cx, NULL, t616, t617, &(*(gm_tpekhfs32op0f *)((uint8_t *)&F.l39)));
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                        ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                        GM_ST(gm_t76123gfet73g, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t618);
                        goto gm_fdone;
                    }
                }
                goto ix558;
ie558:;
                {
                    void * t620 = gm_l0;
                    gm_tl874t2atnlh3 t621 = gm_l1;
                    gm_b16_8 t622;
                    GM_ST(void *, (uint8_t *)&t622, (void *)(gm_sp0ag7curvl4o));
                    GM_ST(uintptr_t, (uint8_t *)&t622 + 8, (uintptr_t)(41));
                    gm_t76123gfet73g t623;
                    gm_f_trap_qh84l9d((void *)&t623, (void *)NULL, cx, NULL, t620, t621, t622);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                    ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                    GM_ST(gm_t76123gfet73g, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t623);
                    goto gm_fdone;
                }
ix558:;
            }
            goto me3;
mn555:;
ma10:;
            uint8_t t625 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t625 != ((uint8_t)60ULL)) goto mn624;
            {
                void * t627 = gm_l0;
                uint64_t t628 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t629 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                if (t628 >= (uintptr_t)(t629)) gm_trap_index(t628, t629, gm_srccp2mbkjs1vn4, 120, 27);
                gm_b16_8 t630 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t628 * 16));
                gm_b16_8 t631 = GM_LD(gm_b16_8, ((uint8_t *)&t630));
                uint64_t t632 = gm_f_ptr_of_b77b4c9(t627, t631);
                gm_l40 = t632;
            }
            {
                gm_l41 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    if (!((uint8_t)1)) goto lb633;
                    {
                        void * t636 = gm_l0;
                        void * t637 = (void *)(((uint8_t *)t636) + 184);
                        uint64_t t638 = gm_l40;
                        uint64_t t639 = gm_l41;
                        uint64_t r640; bool o641 = __builtin_add_overflow(t638, t639, &r640);
                        if (o641) gm_trap("integer overflow in `+`", gm_srccp2mbkjs1vn4, 123, 33);
                        uint64_t t642 = r640;
                        gm_t934k1j7u70ct t643;
                        gm_f_read_u6r4vns((void *)&t643, t637, t642, ((uint64_t)1ULL), ((uint64_t)1ULL));
                        gm_b16_8 t644;
                        if (!((((uint8_t *)&t643))[0] != 0)) {
                            gm_b16_8 t645 = GM_LD(gm_b16_8, (((uint8_t *)&t643) + 8));
                            t644 = t645;
                        } else {
                            F.l42 = (*(gm_tpekhfs32op0f *)(((uint8_t *)&t643) + 8));
                            void * t646 = gm_l0;
                            gm_tl874t2atnlh3 t647 = gm_l1;
                            gm_t76123gfet73g t648;
                            gm_f_mem_trap_ecogko0((void *)&t648, (void *)NULL, cx, NULL, t646, t647, &(*(gm_tpekhfs32op0f *)((uint8_t *)&F.l42)));
                            if (gm_unw.kind) {
                                goto gm_ll634;
                            }
                            GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                            ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                            GM_ST(gm_t76123gfet73g, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t648);
                            goto gm_fdone;
                        }
                        gm_l43 = t644;
                    }
                    {
                        uint64_t t650 = (uintptr_t)((uint64_t)0ULL);
                        uint64_t t651 = GM_LD(uintptr_t, ((uint8_t *)&gm_l43) + 8);
                        if (t650 >= (uintptr_t)(t651)) gm_trap_index(t650, t651, gm_srccp2mbkjs1vn4, 124, 20);
                        uint8_t t652 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l43)) + t650 * 1));
                        uint8_t t653 = ((t652 == ((uint8_t)0ULL)));
                        if (!t653) goto ie649;
                        {
                            goto lb633;
                        }
                        goto ix649;
ie649:;
ix649:;
                    }
                    {
                        uint64_t t655 = gm_l41;
                        uint64_t r656; bool o657 = __builtin_add_overflow(t655, ((uint64_t)1ULL), &r656);
                        if (o657) gm_trap("integer overflow in `+`", gm_srccp2mbkjs1vn4, 125, 17);
                        uint64_t t658 = r656;
                        gm_l41 = t658;
                    }
lc633:;
                }
gm_ll634:;
                if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb633; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc633; }
                }
                goto gm_lad;
lb633:;
            }
            {
                void * t659 = gm_l0;
                gm_b16_8 t660 = gm_l6;
                uint64_t t661 = gm_l41;
                gm_f_put_u64_irp77r2(t659, t660, t661);
            }
            goto me3;
mn624:;
ma11:;
            uint8_t t665 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t665 != ((uint8_t)58ULL)) goto pa664;
            goto po663;
pa664:;
            uint8_t t667 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t667 != ((uint8_t)59ULL)) goto pa666;
            goto po663;
pa666:;
            goto mn662;
po663:;
            {
                gm_t88rcucg1b7uk t669;
                uint8_t t671 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l2) + 0) == ((uint8_t)58ULL)));
                if (!t671) goto ie670;
                gm_t88rcucg1b7uk t673 = gm_l5;
                gm_t88rcucg1b7uk t674 = t673;
                t669 = t674;
                goto ix670;
ie670:;
                uint64_t t676 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t677 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                if (t676 >= (uintptr_t)(t677)) gm_trap_index(t676, t677, gm_srccp2mbkjs1vn4, 130, 55);
                void * t678 = GM_LD(void *, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t676 * 8));
                gm_t88rcucg1b7uk t679 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t678) + 96));
                gm_t88rcucg1b7uk t680 = t679;
                t669 = t680;
ix670:;
                gm_l44 = t669;
            }
            {
                void * t681 = gm_l0;
                gm_b16_8 t682;
                uint8_t t684 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l2) + 0) == ((uint8_t)58ULL)));
                if (!t684) goto ie683;
                uint64_t t686 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t687 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                if (t686 >= (uintptr_t)(t687)) gm_trap_index(t686, t687, gm_srccp2mbkjs1vn4, 131, 53);
                gm_b16_8 t688 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t686 * 16));
                gm_b16_8 t689 = t688;
                gm_b16_8 t690 = GM_LD(gm_b16_8, ((uint8_t *)&t689));
                t682 = t690;
                goto ix683;
ie683:;
                uint64_t t692 = (uintptr_t)((uint64_t)1ULL);
                uint64_t t693 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                if (t692 >= (uintptr_t)(t693)) gm_trap_index(t692, t693, gm_srccp2mbkjs1vn4, 131, 67);
                gm_b16_8 t694 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t692 * 16));
                gm_b16_8 t695 = t694;
                gm_b16_8 t696 = GM_LD(gm_b16_8, ((uint8_t *)&t695));
                t682 = t696;
ix683:;
                gm_t2lqu8jsj511d t697 = gm_f_slice_parts_1se5hu4(t681, t682);
                gm_l45 = t697;
            }
            {
                void * t699 = gm_l0;
                gm_t88rcucg1b7uk t700 = gm_l44;
                gm_t53at80pfdus3 t701;
                gm_f_kind_htv6093((void *)&t701, t699, GM_CONV(gm_csguankko4lmhd, t700));
                uint8_t t702 = GM_LD(uint8_t, (((uint8_t *)&t701) + 0));
                if (t702 != ((uint8_t)11ULL)) goto ie698;
                uint64_t t703 = GM_LD(uint64_t, ((((uint8_t *)&t701) + 8) + 0));
                gm_l46 = t703;
                gm_t88rcucg1b7uk t704 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t701) + 8) + 8));
                gm_l47 = t704;
                {
                    uint64_t t707 = GM_LD(uint64_t, (((uint8_t *)&gm_l45) + 8));
                    uint64_t t708 = gm_l46;
                    uint8_t t709 = (t707 < t708);
                    if (!t709) goto ie706;
                    void * t710 = gm_l0;
                    uint8_t t711 = gm_f_fast_23em7sv(t710);
                    uint8_t t712 = (uint8_t)!t711;
                    if (!t712) goto ie706;
                    {
                        gm_b16_8 t714;
                        uint8_t t716 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l2) + 0) == ((uint8_t)58ULL)));
                        if (!t716) goto ie715;
                        gm_b16_8 t718;
                        GM_ST(void *, (uint8_t *)&t718, (void *)(gm_sqfrv3g5v9m2n));
                        GM_ST(uintptr_t, (uint8_t *)&t718 + 8, (uintptr_t)(4));
                        gm_b16_8 t719 = t718;
                        t714 = t719;
                        goto ix715;
ie715:;
                        gm_b16_8 t721;
                        GM_ST(void *, (uint8_t *)&t721, (void *)(gm_s0ugop4l95o1j));
                        GM_ST(uintptr_t, (uint8_t *)&t721 + 8, (uintptr_t)(5));
                        gm_b16_8 t722 = t721;
                        t714 = t722;
ix715:;
                        gm_l48 = t714;
                    }
                    {
                        void * t723 = gm_l0;
                        gm_tl874t2atnlh3 t724 = gm_l1;
                        gm_tgbgti82mf879 t725;
                        gm_b16_8 t726 = gm_l48;
                        GM_ST(gm_b16_8, (uint8_t *)&t725 + 0, t726);
                        uint64_t t727 = gm_l46;
                        GM_ST(uint64_t, (uint8_t *)&t725 + 16, t727);
                        uint64_t t728 = GM_LD(uint64_t, (((uint8_t *)&gm_l45) + 8));
                        GM_ST(uint64_t, (uint8_t *)&t725 + 24, t728);
                        gm_tabb710d9mq2e t729;
                        gm_f_format_37g0gks((void *)&t729, (void *)NULL, cx, NULL, &t725);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        if (((((uint8_t *)&t729))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t729) + 8, gm_srccp2mbkjs1vn4, 135, 38);
                        gm_b16_8 t730 = GM_LD(gm_b16_8, (((uint8_t *)&t729) + 8));
                        gm_b16_8 t731 = GM_LD(gm_b16_8, ((uint8_t *)&t730));
                        gm_t76123gfet73g t732;
                        gm_f_trap_qh84l9d((void *)&t732, (void *)NULL, cx, NULL, t723, t724, t731);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                        ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                        GM_ST(gm_t76123gfet73g, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t732);
                        goto gm_fdone;
                    }
                    goto ix706;
ie706:;
ix706:;
                }
                {
                    void * t733 = gm_l0;
                    gm_t88rcucg1b7uk t734 = gm_l47;
                    uint64_t t735 = gm_f_align_15fjgq1((void *)NULL, cx, NULL, t733, t734);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l49 = t735;
                }
                {
                    uint8_t t737 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l2) + 0) == ((uint8_t)58ULL)));
                    if (!t737) goto ie736;
                    {
                        void * t739 = gm_l0;
                        gm_tl874t2atnlh3 t740 = gm_l1;
                        uint64_t t741 = GM_LD(uint64_t, (((uint8_t *)&gm_l45) + 0));
                        uint64_t t742 = gm_l49;
                        gm_b16_8 t743 = gm_l6;
                        gm_tcfa6l7reoosn t744;
                        gm_f_load_at_6u81jcc((void *)&t744, (void *)NULL, cx, NULL, t739, t740, t741, t742, t743);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        if (((((uint8_t *)&t744))[0] != 0)) {
                            gm_tcfa6l7reoosn t745;
                            GM_ST(uint8_t, (uint8_t *)&t745 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t745 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t745 + 4, 0);
                            ((uint8_t *)&t745)[0] = 1;
                            GM_ST(gm_t76123gfet73g, ((uint8_t *)&t745 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t744) + 8)));
                            F.ret = t745;
                            goto gm_fret;
                        }
                        gm_b0_1 t746 = GM_LD(gm_b0_1, (((uint8_t *)&t744) + 8));
                    }
                    goto ix736;
ie736:;
                    {
                        void * t748 = gm_l0;
                        gm_tl874t2atnlh3 t749 = gm_l1;
                        uint64_t t750 = GM_LD(uint64_t, (((uint8_t *)&gm_l45) + 0));
                        uint64_t t751 = gm_l49;
                        uint64_t t752 = (uintptr_t)((uint64_t)0ULL);
                        uint64_t t753 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                        if (t752 >= (uintptr_t)(t753)) gm_trap_index(t752, t753, gm_srccp2mbkjs1vn4, 138, 112);
                        gm_b16_8 t754 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t752 * 16));
                        gm_b16_8 t755 = GM_LD(gm_b16_8, ((uint8_t *)&t754));
                        gm_tcfa6l7reoosn t756;
                        gm_f_store_at_ktabiji((void *)&t756, (void *)NULL, cx, NULL, t748, t749, t750, t751, t755);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        if (((((uint8_t *)&t756))[0] != 0)) {
                            gm_tcfa6l7reoosn t757;
                            GM_ST(uint8_t, (uint8_t *)&t757 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t757 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t757 + 4, 0);
                            ((uint8_t *)&t757)[0] = 1;
                            GM_ST(gm_t76123gfet73g, ((uint8_t *)&t757 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t756) + 8)));
                            F.ret = t757;
                            goto gm_fret;
                        }
                        gm_b0_1 t758 = GM_LD(gm_b0_1, (((uint8_t *)&t756) + 8));
                    }
ix736:;
                }
                goto ix698;
ie698:;
                {
                    void * t760 = gm_l0;
                    gm_tl874t2atnlh3 t761 = gm_l1;
                    gm_b16_8 t762;
                    GM_ST(void *, (uint8_t *)&t762, (void *)(gm_s2qp8mqi0c2le));
                    GM_ST(uintptr_t, (uint8_t *)&t762 + 8, (uintptr_t)(47));
                    gm_t76123gfet73g t763;
                    gm_f_trap_qh84l9d((void *)&t763, (void *)NULL, cx, NULL, t760, t761, t762);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                    ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                    GM_ST(gm_t76123gfet73g, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t763);
                    goto gm_fdone;
                }
ix698:;
            }
            goto me3;
mn662:;
ma12:;
            void * t765 = gm_l0;
            gm_tl874t2atnlh3 t766 = gm_l1;
            gm_b16_8 t767;
            GM_ST(void *, (uint8_t *)&t767, (void *)(gm_s1gjt02j7um1i));
            GM_ST(uintptr_t, (uint8_t *)&t767 + 8, (uintptr_t)(36));
            gm_t76123gfet73g t768;
            gm_f_trap_qh84l9d((void *)&t768, (void *)NULL, cx, NULL, t765, t766, t767);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
            ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
            GM_ST(gm_t76123gfet73g, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t768);
            goto gm_fdone;
            goto me3;
mn764:;
ma13:;
            gm_trap("no match arm matched the scrutinee", gm_srccp2mbkjs1vn4, 10, 5);
me3:;
        }
    }
    gm_b0_1 t769;
    GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, 0);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 16, 0);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 24, 0);
    ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 0;
    GM_ST(gm_b0_1, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t769);
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
