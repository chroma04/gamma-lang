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
typedef struct { uint64_t f0; uint64_t f1; } gm_t2lqu8jsj511d;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint8_t f0; uint8_t f1; } gm_tqs5p4n2tq19v;
_Static_assert(sizeof(gm_t2lqu8jsj511d) == 16 && _Alignof(gm_t2lqu8jsj511d) == 8 && offsetof(gm_t2lqu8jsj511d, f0) == 0 && offsetof(gm_t2lqu8jsj511d, f1) == 8, "layout of `(u64, u64)`");
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; gm_b16_8 v1; } f1; } gm_t3ujhu6qph7de;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
typedef struct { gm_t2lqu8jsj511d f0; uint8_t f1; uint8_t pe[7]; } gm_tc70a9dbbmgkg;
typedef struct { uint64_t f0; gm_b16_8 f1; } gm_tf1bjg81u0glv;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
typedef struct { uint64_t f0; gm_b16_8 f1; uint64_t f2; } gm_tob1snuii5bme;
_Static_assert(sizeof(gm_tqs5p4n2tq19v) == 2 && _Alignof(gm_tqs5p4n2tq19v) == 1 && offsetof(gm_tqs5p4n2tq19v, f0) == 0 && offsetof(gm_tqs5p4n2tq19v, f1) == 1, "layout of `(u8, u8)`");
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
_Static_assert(sizeof(gm_t3ujhu6qph7de) == 24 && _Alignof(gm_t3ujhu6qph7de) == 8 && offsetof(gm_t3ujhu6qph7de, f0) == 0 && offsetof(gm_t3ujhu6qph7de, f1) == 8, "layout of `string!string`");
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
_Static_assert(sizeof(gm_tc70a9dbbmgkg) == 24 && _Alignof(gm_tc70a9dbbmgkg) == 8 && offsetof(gm_tc70a9dbbmgkg, f0) == 0 && offsetof(gm_tc70a9dbbmgkg, f1) == 16, "layout of `?(u64, u64)`");
_Static_assert(sizeof(gm_tf1bjg81u0glv) == 24 && _Alignof(gm_tf1bjg81u0glv) == 8 && offsetof(gm_tf1bjg81u0glv, f0) == 0 && offsetof(gm_tf1bjg81u0glv, f1) == 8, "layout of `(i: usize, x: string)`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; int64_t v1; uint8_t v2; gm_trqfvmjtt4sij v3; void * v4; gm_b16_8 v5; } f1; } gm_tktr4rmbindgv;
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
_Static_assert(sizeof(gm_tob1snuii5bme) == 32 && _Alignof(gm_tob1snuii5bme) == 8 && offsetof(gm_tob1snuii5bme, f0) == 0 && offsetof(gm_tob1snuii5bme, f1) == 8 && offsetof(gm_tob1snuii5bme, f2) == 24, "layout of `Reloc`");
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tob1snuii5bme v0; void * v1; } f1; } gm_trssmtuv8jr0v;
typedef struct { gm_b16_8 f0; gm_th2lqbo77pvsc f1; gm_th2lqbo77pvsc f2; } gm_tt008mgut8glb;
typedef struct { gm_t6j6smj7qoctu f0; uint8_t f1; uint8_t pe[7]; } gm_ttu09lij36f8c;
typedef struct { gm_b16_8 f0; gm_ttu09lij36f8c f1; gm_th2lqbo77pvsc f2; } gm_t03ccdabio1fj;
_Static_assert(sizeof(gm_tktr4rmbindgv) == 48 && _Alignof(gm_tktr4rmbindgv) == 8 && offsetof(gm_tktr4rmbindgv, f0) == 0 && offsetof(gm_tktr4rmbindgv, f1) == 8, "layout of `Value`");
_Static_assert(sizeof(gm_trssmtuv8jr0v) == 40 && _Alignof(gm_trssmtuv8jr0v) == 8 && offsetof(gm_trssmtuv8jr0v, f0) == 0 && offsetof(gm_trssmtuv8jr0v, f1) == 8, "layout of `TomlError!^Table`");
_Static_assert(sizeof(gm_tt008mgut8glb) == 64 && _Alignof(gm_tt008mgut8glb) == 8 && offsetof(gm_tt008mgut8glb, f0) == 0 && offsetof(gm_tt008mgut8glb, f1) == 16 && offsetof(gm_tt008mgut8glb, f2) == 40, "layout of `UrlSource`");
_Static_assert(sizeof(gm_ttu09lij36f8c) == 40 && _Alignof(gm_ttu09lij36f8c) == 8 && offsetof(gm_ttu09lij36f8c, f0) == 0 && offsetof(gm_ttu09lij36f8c, f1) == 32, "layout of `?Split`");
_Static_assert(sizeof(gm_t03ccdabio1fj) == 80 && _Alignof(gm_t03ccdabio1fj) == 8 && offsetof(gm_t03ccdabio1fj, f0) == 0 && offsetof(gm_t03ccdabio1fj, f1) == 16 && offsetof(gm_t03ccdabio1fj, f2) == 56, "layout of `(url: string, reference: ?(key: string, value: string), subdir: ?string)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; gm_t03ccdabio1fj v1; gm_tt008mgut8glb v2; } f1; } gm_t2jeshnsn30ep;
_Static_assert(sizeof(gm_t2jeshnsn30ep) == 88 && _Alignof(gm_t2jeshnsn30ep) == 8 && offsetof(gm_t2jeshnsn30ep, f0) == 0 && offsetof(gm_t2jeshnsn30ep, f1) == 8, "layout of `NewDep`");
struct gm_fr_add_dependency_jmjffrp {
    gm_frame h;
    gm_t2jeshnsn30ep l2; /* dep */
    gm_trqfvmjtt4sij l9; /* lines */
    gm_trqfvmjtt4sij l10; /* out */
    gm_t3ujhu6qph7de ret;
};
struct gm_fr_as_table_4f7n31g {
    gm_frame h;
    gm_tktr4rmbindgv l0; /* v */
    void * ret;
};
struct gm_fr_empty_slice_5nn7ph0 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ijtp9lo {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_entry_lines_3jhl7a4 {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_header_of_2u6atoc {
    gm_frame h;
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_inline_value_0impjcn {
    gm_frame h;
    gm_t2jeshnsn30ep l0; /* dep */
    gm_trqfvmjtt4sij l1; /* out */
    gm_tt008mgut8glb l3; /* u */
    gm_t03ccdabio1fj l6; /* g */
    gm_b16_8 ret;
};
struct gm_fr_is_blank_or_comment_lubpopi {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_header_uakq83c {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_join_without_ej3mkbe {
    gm_frame h;
    gm_trqfvmjtt4sij l3; /* out */
    gm_b16_8 ret;
};
struct gm_fr_key_of_99iu4dv {
    gm_frame h;
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_list_epvducg {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_uvfu1k1 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_normalize_newlines_183t7r2 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_pop_vr08sii {
    gm_frame h;
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_remove_dependency_4ju7b0h {
    gm_frame h;
    gm_trqfvmjtt4sij l3; /* lines */
    gm_trqfvmjtt4sij l6; /* keep */
    gm_t3ujhu6qph7de ret;
};
struct gm_fr_split_lines_jecth9m {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* lines */
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_starts_with_6vvt01e {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_table_range_09bngqh {
    gm_frame h;
    gm_tc70a9dbbmgkg ret;
};
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b);
inline bool gm_eqjmccqdb90f(const uint8_t *a, const uint8_t *b);
__attribute__((aligned(4))) void gm_f_add_dependency_jmjffrp(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1, const void * a2);
inline __attribute__((aligned(4))) void * gm_f_as_table_4f7n31g(const void * a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_5nn7ph0(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
__attribute__((aligned(4))) uint8_t gm_f_ends_with_biriu5l(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) uint64_t gm_f_entry_lines_3jhl7a4(gm_b16_8 a0, uint64_t a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_extend_0cs85gs(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_find_nab29co(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_format_7plp021(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_f1281l5(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_fqec6j2(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_m89t2jt(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_rkrg0f2(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void * gm_f_get_9rme0gb(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_header_of_2u6atoc(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_inline_value_0impjcn(void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_blank_or_comment_lubpopi(gm_b16_8 a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_header_uakq83c(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_join_without_ej3mkbe(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, uint64_t a1, uint64_t a2);
__attribute__((aligned(4))) void gm_f_key_of_99iu4dv(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) void gm_f_list_epvducg(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_b16_8 gm_f_normalize_newlines_183t7r2(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_parse_toml_gjtmf79(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_pop_vr08sii(void *gm_out, void * a0);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_3ef746b(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_t83rg2m(void * a0, uint8_t a1);
__attribute__((aligned(4))) void gm_f_remove_dependency_4ju7b0h(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_replace_alloc_t7hndlq(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_split_lines_jecth9m(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) uint8_t gm_f_starts_with_6vvt01e(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_table_range_09bngqh(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_toml_string_cagbrmp(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_trim_ascii_c9j36d1(gm_b16_8 a0);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_5jambfs(void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_ctdpla1(void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_gle9guv(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_ivc38em(void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_r83e4t6(void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_rbbkc88(void * a0, gm_tnpifv20irnph a1);
extern const uint8_t gm_s2053hhvf16fm[2];
extern const uint8_t gm_s5m9qimbv7e8k[3];
extern const uint8_t gm_s7mfvroqmll1f[3];
extern const uint8_t gm_sajfarek3i4jp[44];
extern const uint8_t gm_seccpktgmrevt[2];
extern const uint8_t gm_sh0j2ggs1vvoa[2];
extern const uint8_t gm_shv5ofgc65v68[3];
extern const uint8_t gm_sk754mj3p1el6[2];
extern const uint8_t gm_skmt2kt2p7v0u[3];
extern const uint8_t gm_skof48dte1s1o[13];
extern const uint8_t gm_sl3q52ep6aa53[2];
extern const uint8_t gm_snomh93h7tumv[14];
extern const uint8_t gm_so514oj5nbhcu[2];
extern const uint8_t gm_sr4qbrk6tlgaa[1];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_src3nt8pf0qk5gn[];
extern const char gm_srcc24pnihhgkug[];
extern const char gm_srclt70oog8j9io[];
extern const char gm_srcrgc9bkquf6nr[];
extern const uint8_t gm_sv9j89ufns35b[16];
extern const gm_type gm_tyo86ks9qkvt;
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b) {
    uintptr_t n = GM_LD(uintptr_t, a + 8);
    if (n != GM_LD(uintptr_t, b + 8)) return false;
    const uint8_t *pa = GM_LD(void *, a), *pb = GM_LD(void *, b);
    return n == 0 || memcmp(pa, pb, n * 1) == 0;
}
inline bool gm_eqjmccqdb90f(const uint8_t *a, const uint8_t *b) {
    return GM_LD(uint8_t, a) == GM_LD(uint8_t, b);
}
inline __attribute__((aligned(4))) void * gm_f_as_table_4f7n31g(const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_as_table_4f7n31g F;
    void * gm_l1; /* t */
    GM_STACK_CHECK_SHALLOW(&F, 49u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    void * t1;
    uint8_t t4 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t4 != ((uint8_t)4ULL)) goto mn3;
    void * t5 = GM_LD(void *, (((uint8_t *)a0) + 8));
    gm_l1 = t5;
    void * t6 = gm_l1;
    t1 = t6;
    goto me2;
mn3:;
    t1 = ((void *)0);
    goto me2;
mn7:;
    gm_trap("no match arm matched the scrutinee", gm_src3nt8pf0qk5gn, 56, 53);
me2:;
    F.ret = t1;
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_blank_or_comment_lubpopi(gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_blank_or_comment_lubpopi F;
    gm_b16_8 gm_l0; /* line */
    gm_b16_8 gm_l1; /* t */
    GM_STACK_CHECK_SHALLOW(&F, 110u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_b16_8 t2 = gm_l0;
        gm_b16_8 t3 = gm_f_trim_ascii_c9j36d1(t2);
        gm_l1 = t3;
    }
    {
        uint8_t t4 = 0;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint8_t t6 = ((t5 == ((uint64_t)0ULL)));
        if (!t6) {
            gm_b16_8 t7 = gm_l1;
            gm_b16_8 t8;
            GM_ST(void *, (uint8_t *)&t8, (void *)(gm_seccpktgmrevt));
            GM_ST(uintptr_t, (uint8_t *)&t8 + 8, (uintptr_t)(1));
            uint8_t t9 = gm_f_starts_with_6vvt01e(t7, t8);
            t4 = t9;
        } else t4 = 1;
        uint8_t t10 = 0;
        F.ret = t4;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_header_uakq83c(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_is_header_uakq83c F;
    gm_b16_8 gm_l0; /* line */
    gm_b16_8 gm_l1; /* t */
    GM_STACK_CHECK_SHALLOW(&F, 100u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_b16_8 t2 = gm_l0;
        gm_b16_8 t3 = gm_f_trim_ascii_c9j36d1(t2);
        gm_l1 = t3;
    }
    {
        gm_b16_8 t4 = gm_l1;
        gm_b16_8 t5;
        GM_ST(void *, (uint8_t *)&t5, (void *)(gm_sk754mj3p1el6));
        GM_ST(uintptr_t, (uint8_t *)&t5 + 8, (uintptr_t)(1));
        uint8_t t6 = gm_f_starts_with_6vvt01e(t4, t5);
        uint8_t t7 = 0;
        F.ret = t6;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_pop_vr08sii(void *gm_out, void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_pop_vr08sii F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* n */
    gm_b16_8 gm_l2; /* x */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        uint64_t t3 = GM_LD(uintptr_t, (((uint8_t *)t2) + 0) + 8);
        gm_l1 = t3;
    }
    {
        uint64_t t5 = gm_l1;
        uint8_t t6 = ((t5 == ((uint64_t)0ULL)));
        if (!t6) goto ie4;
        {
            gm_th2lqbo77pvsc t8;
            memset(&t8, 0, sizeof t8);
            uint8_t t9 = 0;
            F.ret = t8;
            goto gm_fret;
        }
        goto ix4;
ie4:;
ix4:;
    }
    {
        uint64_t t10 = gm_l1;
        uint64_t r11; bool o12 = __builtin_sub_overflow(t10, ((uint64_t)1ULL), &r11);
        if (o12) gm_trap("integer overflow in `-`", gm_srcrgc9bkquf6nr, 130, 18);
        uint64_t t13 = r11;
        uint64_t t14 = (uintptr_t)t13;
        void * t15 = gm_l0;
        uint64_t t16 = GM_LD(uintptr_t, (((uint8_t *)t15) + 0) + 8);
        if (t14 >= (uintptr_t)(t16)) gm_trap_index(t14, t16, gm_srcrgc9bkquf6nr, 130, 10);
        gm_b16_8 t17 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, (((uint8_t *)t15) + 0)) + t14 * 16));
        gm_l2 = t17;
    }
    {
        void * t18 = gm_l0;
        void * t19 = gm_l0;
        uint64_t t20 = GM_LD(uintptr_t, (((uint8_t *)t19) + 0) + 8);
        void * t21 = ((uint8_t *)GM_LD(void *, (((uint8_t *)t19) + 0)));
        uint64_t t22 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t23 = gm_l1;
        uint64_t r24; bool o25 = __builtin_sub_overflow(t23, ((uint64_t)1ULL), &r24);
        if (o25) gm_trap("integer overflow in `-`", gm_srcrgc9bkquf6nr, 131, 26);
        uint64_t t26 = r24;
        uint64_t t27 = (uintptr_t)t26;
        if (t22 > t27 || t27 > (uintptr_t)t20) gm_trap_slice(t22, t27, t20, gm_srcrgc9bkquf6nr, 131, 15);
        gm_b16_8 t28;
        GM_ST(void *, (uint8_t *)&t28, (void *)(((uint8_t *)t21 + t22 * 16)));
        GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(t27 - t22));
        GM_ST(gm_b16_8, (((uint8_t *)t18) + 0), t28);
    }
    {
        gm_b16_8 t29 = gm_l2;
        GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 20, 0);
        ((uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out))[16] = 1;
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 0, t29);
        goto gm_fdone;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
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
__attribute__((aligned(4))) void gm_f_add_dependency_jmjffrp(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1, const void * a2) {
    struct gm_fr_add_dependency_jmjffrp F;
    gm_b16_8 gm_l0; /* original */
    gm_b16_8 gm_l1; /* name */
    gm_b16_8 gm_l3; /* text */
    gm_tob1snuii5bme gm_l4; /* e */
    void * gm_l5; /* doc */
    void * gm_l6; /* d */
    void * gm_l7; /* t */
    gm_b16_8 gm_l8; /* entry */
    gm_t2lqu8jsj511d gm_l11; /* r */
    uint64_t gm_l12; /* last */
    gm_b16_8 gm_l13; /* <tmp> */
    uint64_t gm_l14; /* <tmp> */
    gm_b16_8 gm_l15; /* l */
    gm_b16_8 gm_l16; /* <tmp> */
    uint64_t gm_l17; /* <tmp> */
    gm_b16_8 gm_l18; /* l */
    gm_b16_8 gm_l19; /* <tmp> */
    uint64_t gm_l20; /* <tmp> */
    uint64_t gm_l21; /* i */
    gm_b16_8 gm_l22; /* l */
    gm_b16_8 gm_l23; /* h */
    gm_b16_8 gm_l24; /* <tmp> */
    uint64_t gm_l25; /* <tmp> */
    gm_b16_8 gm_l26; /* x */
    gm_b16_8 gm_l27; /* <tmp> */
    uint64_t gm_l28; /* <tmp> */
    gm_b16_8 gm_l29; /* x */
    GM_STACK_CHECK(&F, 2446u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    memcpy(&F.l2, a2, sizeof F.l2);
    {
        gm_b16_8 t2 = gm_l0;
        gm_b16_8 t3 = gm_f_normalize_newlines_183t7r2((void *)NULL, cx, NULL, t2);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l3 = t3;
    }
    {
        gm_b16_8 t4 = gm_l3;
        gm_trssmtuv8jr0v t5;
        gm_f_parse_toml_gjtmf79((void *)&t5, (void *)NULL, cx, NULL, t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t6;
        if (!((((uint8_t *)&t5))[0] != 0)) {
            void * t7 = GM_LD(void *, (((uint8_t *)&t5) + 8));
            t6 = t7;
        } else {
            gm_l4 = (*(gm_tob1snuii5bme *)(((uint8_t *)&t5) + 8));
            gm_b16_8 t9 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l4) + 8));
            gm_tnpifv20irnph t10;
            GM_ST(gm_b16_8, ((uint8_t *)&t10 + 0), t9);
            gm_tabb710d9mq2e t11;
            gm_f_format_f1281l5((void *)&t11, (void *)NULL, cx, NULL, t10);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t11))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t11) + 8, gm_srclt70oog8j9io, 138, 54);
            gm_b16_8 t12 = GM_LD(gm_b16_8, (((uint8_t *)&t11) + 8));
            gm_b16_8 t13 = GM_LD(gm_b16_8, ((uint8_t *)&t12));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out) + 4, 0);
            ((uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out))[0] = 1;
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out) + 8, t13);
            goto gm_fdone;
        }
        gm_l5 = t6;
    }
    {
        void * t15 = gm_l5;
        gm_b16_8 t16;
        GM_ST(void *, (uint8_t *)&t16, (void *)(gm_skof48dte1s1o));
        GM_ST(uintptr_t, (uint8_t *)&t16 + 8, (uintptr_t)(12));
        void * t17 = gm_f_get_9rme0gb(t15, t16);
        if (!(GM_LD(void *, ((uint8_t *)&t17)) != 0)) goto ie14;
        void * t18 = GM_LD(void *, ((uint8_t *)&t17));
        gm_l6 = t18;
        {
            void * t20 = gm_l6;
            void * t21 = gm_f_as_table_4f7n31g(&(*(gm_tktr4rmbindgv *)(((uint8_t *)t20) + 16)));
            void * t22;
            if ((GM_LD(void *, ((uint8_t *)&t21)) != 0)) {
                void * t23 = GM_LD(void *, (((uint8_t *)&t21) + 0));
                t22 = t23;
            } else {
                gm_b16_8 t24;
                GM_ST(void *, (uint8_t *)&t24, (void *)(gm_sajfarek3i4jp));
                GM_ST(uintptr_t, (uint8_t *)&t24 + 8, (uintptr_t)(43));
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out) + 4, 0);
                ((uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out))[0] = 1;
                GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out) + 8, t24);
                goto gm_fdone;
            }
            gm_l7 = t22;
        }
        {
            void * t26 = gm_l7;
            gm_b16_8 t27 = gm_l1;
            void * t28 = gm_f_get_9rme0gb(t26, t27);
            uint8_t t29;
            if ((GM_LD(void *, ((uint8_t *)&t28)) != 0)) goto mn31;
            t29 = ((uint8_t)0);
            goto me30;
mn31:;
            t29 = ((uint8_t)1);
            goto me30;
mn32:;
            gm_trap("no match arm matched the scrutinee", gm_srclt70oog8j9io, 141, 12);
me30:;
            if (!t29) goto ie25;
            {
                gm_b16_8 t34 = gm_l1;
                gm_tnpifv20irnph t35;
                GM_ST(gm_b16_8, ((uint8_t *)&t35 + 0), t34);
                gm_tabb710d9mq2e t36;
                gm_f_format_m89t2jt((void *)&t36, (void *)NULL, cx, NULL, t35);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t36))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t36) + 8, gm_srclt70oog8j9io, 141, 46);
                gm_b16_8 t37 = GM_LD(gm_b16_8, (((uint8_t *)&t36) + 8));
                gm_b16_8 t38 = GM_LD(gm_b16_8, ((uint8_t *)&t37));
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out) + 4, 0);
                ((uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out))[0] = 1;
                GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out) + 8, t38);
                goto gm_fdone;
            }
            goto ix25;
ie25:;
ix25:;
        }
        goto ix14;
ie14:;
ix14:;
    }
    {
        gm_t6j6smj7qoctu t39;
        gm_b16_8 t40 = gm_l1;
        GM_ST(gm_b16_8, (uint8_t *)&t39 + 0, t40);
        gm_b16_8 t41 = gm_f_inline_value_0impjcn((void *)NULL, cx, NULL, &(*(gm_t2jeshnsn30ep *)((uint8_t *)&F.l2)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_b16_8, (uint8_t *)&t39 + 16, t41);
        gm_tabb710d9mq2e t42;
        gm_f_format_fqec6j2((void *)&t42, (void *)NULL, cx, NULL, &t39);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t42))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t42) + 8, gm_srclt70oog8j9io, 143, 14);
        gm_b16_8 t43 = GM_LD(gm_b16_8, (((uint8_t *)&t42) + 8));
        gm_l8 = t43;
    }
    {
        gm_b16_8 t44 = gm_l3;
        gm_f_split_lines_jecth9m((void *)&F.l9, (void *)NULL, cx, NULL, t44);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_f_list_uvfu1k1((void *)&F.l10, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_b16_8 t46 = GM_LD(gm_b16_8, (((uint8_t *)&F.l9) + 0));
        gm_b16_8 t47 = GM_LD(gm_b16_8, ((uint8_t *)&t46));
        gm_b16_8 t48;
        GM_ST(void *, (uint8_t *)&t48, (void *)(gm_skof48dte1s1o));
        GM_ST(uintptr_t, (uint8_t *)&t48 + 8, (uintptr_t)(12));
        gm_tc70a9dbbmgkg t49;
        gm_f_table_range_09bngqh((void *)&t49, (void *)NULL, cx, NULL, t47, t48);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (!((((uint8_t *)&t49))[16] != 0)) goto ie45;
        gm_t2lqu8jsj511d t50 = GM_LD(gm_t2lqu8jsj511d, ((uint8_t *)&t49));
        gm_l11 = t50;
        {
            uint64_t t52 = GM_LD(uint64_t, (((uint8_t *)&gm_l11) + 8));
            gm_l12 = t52;
        }
        {
            for (;;) {
                uint64_t t54 = gm_l12;
                uint64_t t55 = GM_LD(uint64_t, (((uint8_t *)&gm_l11) + 0));
                uint64_t r56; bool o57 = __builtin_add_overflow(t55, ((uint64_t)1ULL), &r56);
                if (o57) gm_trap("integer overflow in `+`", gm_srclt70oog8j9io, 149, 22);
                uint64_t t58 = r56;
                uint8_t t59 = (t54 > t58);
                if (!t59) goto lb53;
                uint64_t t60 = gm_l12;
                uint64_t r61; bool o62 = __builtin_sub_overflow(t60, ((uint64_t)1ULL), &r61);
                if (o62) gm_trap("integer overflow in `-`", gm_srclt70oog8j9io, 149, 71);
                uint64_t t63 = r61;
                uint64_t t64 = (uintptr_t)t63;
                uint64_t t65 = GM_LD(uintptr_t, (((uint8_t *)&F.l9) + 0) + 8);
                if (t64 >= (uintptr_t)(t65)) gm_trap_index(t64, t65, gm_srclt70oog8j9io, 149, 59);
                gm_b16_8 t66 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l9) + 0)) + t64 * 16));
                uint8_t t67 = gm_f_is_blank_or_comment_lubpopi(t66);
                if (!t67) goto lb53;
                {
                    uint64_t t70 = gm_l12;
                    uint64_t r71; bool o72 = __builtin_sub_overflow(t70, ((uint64_t)1ULL), &r71);
                    if (o72) gm_trap("integer overflow in `-`", gm_srclt70oog8j9io, 149, 84);
                    uint64_t t73 = r71;
                    gm_l12 = t73;
                }
lc53:;
            }
lb53:;
        }
        {
            {
                uint64_t t75 = GM_LD(uintptr_t, (((uint8_t *)&F.l9) + 0) + 8);
                void * t76 = ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l9) + 0)));
                uint64_t t77 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t78 = gm_l12;
                uint64_t t79 = (uintptr_t)t78;
                if (t77 > t79 || t79 > (uintptr_t)t75) gm_trap_slice(t77, t79, t75, gm_srclt70oog8j9io, 150, 18);
                gm_b16_8 t80;
                GM_ST(void *, (uint8_t *)&t80, (void *)(((uint8_t *)t76 + t77 * 16)));
                GM_ST(uintptr_t, (uint8_t *)&t80 + 8, (uintptr_t)(t79 - t77));
                gm_l13 = t80;
            }
            {
                gm_l14 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t82 = gm_l14;
                    uint64_t t83 = GM_LD(uintptr_t, ((uint8_t *)&gm_l13) + 8);
                    uint8_t t84 = (t82 < t83);
                    if (!t84) goto lb81;
                    {
                        uint64_t t87 = gm_l14;
                        uint64_t t88 = (uintptr_t)t87;
                        uint64_t t89 = GM_LD(uintptr_t, ((uint8_t *)&gm_l13) + 8);
                        gm_b16_8 t92 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l13)) + t88 * 16));
                        gm_l15 = t92;
                        {
                            void * t94 = (void *)((uint8_t *)&F.l10);
                            gm_b16_8 t95 = gm_l15;
                            gm_tftb1odc739t5 t96 = gm_f_extend_0cs85gs(t94, t95);
                            if (((((uint8_t *)&t96))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t96) + 1, gm_srclt70oog8j9io, 150, 41);
                            gm_b0_1 t97 = GM_LD(gm_b0_1, (((uint8_t *)&t96) + 1));
                        }
                        goto me90;
mn91:;
                        gm_trap("no match arm matched the scrutinee", gm_srclt70oog8j9io, 150, 9);
me90:;
                    }
lc81:;
                    {
                        uint64_t t99 = gm_l14;
                        uint64_t r100; bool o101 = __builtin_add_overflow(t99, ((uint64_t)1ULL), &r100);
                        uint64_t t102 = r100;
                        gm_l14 = t102;
                    }
                }
lb81:;
            }
        }
        {
            uint64_t t104 = gm_l12;
            uint8_t t105 = (t104 > ((uint64_t)0ULL));
            if (!t105) goto ie103;
            uint64_t t106 = gm_l12;
            uint64_t r107; bool o108 = __builtin_sub_overflow(t106, ((uint64_t)1ULL), &r107);
            if (o108) gm_trap("integer overflow in `-`", gm_srclt70oog8j9io, 151, 41);
            uint64_t t109 = r107;
            uint64_t t110 = (uintptr_t)t109;
            uint64_t t111 = GM_LD(uintptr_t, (((uint8_t *)&F.l9) + 0) + 8);
            if (t110 >= (uintptr_t)(t111)) gm_trap_index(t110, t111, gm_srclt70oog8j9io, 151, 29);
            gm_b16_8 t112 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l9) + 0)) + t110 * 16));
            gm_b16_8 t113;
            GM_ST(void *, (uint8_t *)&t113, (void *)(gm_so514oj5nbhcu));
            GM_ST(uintptr_t, (uint8_t *)&t113 + 8, (uintptr_t)(1));
            uint8_t t114 = gm_f_ends_with_biriu5l(t112, t113);
            uint8_t t115 = (uint8_t)!t114;
            if (!t115) goto ie103;
            {
                void * t117 = (void *)((uint8_t *)&F.l10);
                gm_tftb1odc739t5 t118 = gm_f_push_t83rg2m(t117, ((uint8_t)10ULL));
                if (((((uint8_t *)&t118))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t118) + 1, gm_srclt70oog8j9io, 151, 69);
                gm_b0_1 t119 = GM_LD(gm_b0_1, (((uint8_t *)&t118) + 1));
            }
            goto ix103;
ie103:;
ix103:;
        }
        {
            void * t120 = (void *)((uint8_t *)&F.l10);
            gm_b16_8 t121 = gm_l8;
            gm_b16_8 t122 = GM_LD(gm_b16_8, ((uint8_t *)&t121));
            gm_tftb1odc739t5 t123 = gm_f_extend_0cs85gs(t120, t122);
            if (((((uint8_t *)&t123))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t123) + 1, gm_srclt70oog8j9io, 152, 9);
            gm_b0_1 t124 = GM_LD(gm_b0_1, (((uint8_t *)&t123) + 1));
        }
        {
            {
                uint64_t t126 = GM_LD(uintptr_t, (((uint8_t *)&F.l9) + 0) + 8);
                void * t127 = ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l9) + 0)));
                uint64_t t128 = gm_l12;
                uint64_t t129 = (uintptr_t)t128;
                if (t129 > (uintptr_t)t126 || (uintptr_t)t126 > (uintptr_t)t126) gm_trap_slice(t129, (uintptr_t)t126, t126, gm_srclt70oog8j9io, 153, 18);
                gm_b16_8 t130;
                GM_ST(void *, (uint8_t *)&t130, (void *)(((uint8_t *)t127 + t129 * 16)));
                GM_ST(uintptr_t, (uint8_t *)&t130 + 8, (uintptr_t)((uintptr_t)t126 - t129));
                gm_l16 = t130;
            }
            {
                gm_l17 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t132 = gm_l17;
                    uint64_t t133 = GM_LD(uintptr_t, ((uint8_t *)&gm_l16) + 8);
                    uint8_t t134 = (t132 < t133);
                    if (!t134) goto lb131;
                    {
                        uint64_t t137 = gm_l17;
                        uint64_t t138 = (uintptr_t)t137;
                        uint64_t t139 = GM_LD(uintptr_t, ((uint8_t *)&gm_l16) + 8);
                        gm_b16_8 t142 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l16)) + t138 * 16));
                        gm_l18 = t142;
                        {
                            void * t144 = (void *)((uint8_t *)&F.l10);
                            gm_b16_8 t145 = gm_l18;
                            gm_tftb1odc739t5 t146 = gm_f_extend_0cs85gs(t144, t145);
                            if (((((uint8_t *)&t146))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t146) + 1, gm_srclt70oog8j9io, 153, 40);
                            gm_b0_1 t147 = GM_LD(gm_b0_1, (((uint8_t *)&t146) + 1));
                        }
                        goto me140;
mn141:;
                        gm_trap("no match arm matched the scrutinee", gm_srclt70oog8j9io, 153, 9);
me140:;
                    }
lc131:;
                    {
                        uint64_t t149 = gm_l17;
                        uint64_t r150; bool o151 = __builtin_add_overflow(t149, ((uint64_t)1ULL), &r150);
                        uint64_t t152 = r150;
                        gm_l17 = t152;
                    }
                }
lb131:;
            }
        }
        {
            gm_b16_8 t153 = GM_LD(gm_b16_8, (((uint8_t *)&F.l10) + 0));
            gm_b16_8 t154 = GM_LD(gm_b16_8, ((uint8_t *)&t153));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out) + 4, 0);
            ((uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out))[0] = 0;
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out) + 8, t154);
            goto gm_fdone;
        }
        goto ix45;
ie45:;
ix45:;
    }
    {
        {
            gm_b16_8 t156 = GM_LD(gm_b16_8, (((uint8_t *)&F.l9) + 0));
            gm_b16_8 t157 = GM_LD(gm_b16_8, ((uint8_t *)&t156));
            gm_l19 = t157;
        }
        {
            gm_l20 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t159 = gm_l20;
                uint64_t t160 = GM_LD(uintptr_t, ((uint8_t *)&gm_l19) + 8);
                uint8_t t161 = (t159 < t160);
                if (!t161) goto lb158;
                {
                    gm_tf1bjg81u0glv t164;
                    uint64_t t165 = gm_l20;
                    GM_ST(uint64_t, (uint8_t *)&t164 + 0, t165);
                    uint64_t t166 = gm_l20;
                    uint64_t t167 = (uintptr_t)t166;
                    uint64_t t168 = GM_LD(uintptr_t, ((uint8_t *)&gm_l19) + 8);
                    gm_b16_8 t169 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l19)) + t167 * 16));
                    GM_ST(gm_b16_8, (uint8_t *)&t164 + 8, t169);
                    uint64_t t172 = GM_LD(uint64_t, (((uint8_t *)&t164) + 0));
                    gm_l21 = t172;
                    gm_b16_8 t173 = GM_LD(gm_b16_8, (((uint8_t *)&t164) + 8));
                    gm_l22 = t173;
                    {
                        gm_b16_8 t176 = gm_l22;
                        gm_th2lqbo77pvsc t177;
                        gm_f_header_of_2u6atoc((void *)&t177, (void *)NULL, cx, NULL, t176);
                        if (gm_unw.kind) {
                            goto gm_ll162;
                        }
                        if (!((((uint8_t *)&t177))[16] != 0)) goto ie175;
                        gm_b16_8 t178 = GM_LD(gm_b16_8, ((uint8_t *)&t177));
                        gm_l23 = t178;
                        {
                            gm_b16_8 t181 = gm_l23;
                            gm_b16_8 t182;
                            GM_ST(void *, (uint8_t *)&t182, (void *)(gm_snomh93h7tumv));
                            GM_ST(uintptr_t, (uint8_t *)&t182 + 8, (uintptr_t)(13));
                            uint8_t t183 = gm_f_starts_with_6vvt01e(t181, t182);
                            if (!t183) goto ie180;
                            {
                                {
                                    uint64_t t186 = GM_LD(uintptr_t, (((uint8_t *)&F.l9) + 0) + 8);
                                    void * t187 = ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l9) + 0)));
                                    uint64_t t188 = (uintptr_t)((uint64_t)0ULL);
                                    uint64_t t189 = gm_l21;
                                    uint64_t t190 = (uintptr_t)t189;
                                    if (t188 > t190 || t190 > (uintptr_t)t186) gm_trap_slice(t188, t190, t186, gm_srclt70oog8j9io, 160, 26);
                                    gm_b16_8 t191;
                                    GM_ST(void *, (uint8_t *)&t191, (void *)(((uint8_t *)t187 + t188 * 16)));
                                    GM_ST(uintptr_t, (uint8_t *)&t191 + 8, (uintptr_t)(t190 - t188));
                                    gm_l24 = t191;
                                }
                                {
                                    gm_l25 = ((uint64_t)0ULL);
                                }
                                {
                                    for (;;) {
                                        uint64_t t193 = gm_l25;
                                        uint64_t t194 = GM_LD(uintptr_t, ((uint8_t *)&gm_l24) + 8);
                                        uint8_t t195 = (t193 < t194);
                                        if (!t195) goto lb192;
                                        {
                                            uint64_t t198 = gm_l25;
                                            uint64_t t199 = (uintptr_t)t198;
                                            uint64_t t200 = GM_LD(uintptr_t, ((uint8_t *)&gm_l24) + 8);
                                            gm_b16_8 t203 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l24)) + t199 * 16));
                                            gm_l26 = t203;
                                            {
                                                void * t205 = (void *)((uint8_t *)&F.l10);
                                                gm_b16_8 t206 = gm_l26;
                                                gm_tftb1odc739t5 t207 = gm_f_extend_0cs85gs(t205, t206);
                                                if (((((uint8_t *)&t207))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t207) + 1, gm_srclt70oog8j9io, 160, 46);
                                                gm_b0_1 t208 = GM_LD(gm_b0_1, (((uint8_t *)&t207) + 1));
                                            }
                                            goto me201;
mn202:;
                                            gm_trap("no match arm matched the scrutinee", gm_srclt70oog8j9io, 160, 17);
me201:;
                                        }
lc192:;
                                        {
                                            uint64_t t210 = gm_l25;
                                            uint64_t r211; bool o212 = __builtin_add_overflow(t210, ((uint64_t)1ULL), &r211);
                                            uint64_t t213 = r211;
                                            gm_l25 = t213;
                                        }
                                    }
lb192:;
                                }
                            }
                            {
                                uint64_t t215 = gm_l21;
                                uint8_t t216 = (t215 > ((uint64_t)0ULL));
                                if (!t216) goto ie214;
                                uint64_t t217 = gm_l21;
                                uint64_t r218; bool o219 = __builtin_sub_overflow(t217, ((uint64_t)1ULL), &r218);
                                if (o219) gm_trap("integer overflow in `-`", gm_srclt70oog8j9io, 161, 66);
                                uint64_t t220 = r218;
                                uint64_t t221 = (uintptr_t)t220;
                                uint64_t t222 = GM_LD(uintptr_t, (((uint8_t *)&F.l9) + 0) + 8);
                                if (t221 >= (uintptr_t)(t222)) gm_trap_index(t221, t222, gm_srclt70oog8j9io, 161, 54);
                                gm_b16_8 t223 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l9) + 0)) + t221 * 16));
                                uint8_t t224 = gm_f_is_blank_or_comment_lubpopi(t223);
                                uint8_t t225 = (uint8_t)!t224;
                                if (!t225) goto ie214;
                                {
                                    void * t227 = (void *)((uint8_t *)&F.l10);
                                    gm_tftb1odc739t5 t228 = gm_f_push_t83rg2m(t227, ((uint8_t)10ULL));
                                    if (((((uint8_t *)&t228))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t228) + 1, gm_srclt70oog8j9io, 161, 76);
                                    gm_b0_1 t229 = GM_LD(gm_b0_1, (((uint8_t *)&t228) + 1));
                                }
                                goto ix214;
ie214:;
ix214:;
                            }
                            {
                                void * t230 = (void *)((uint8_t *)&F.l10);
                                gm_b16_8 t231;
                                GM_ST(void *, (uint8_t *)&t231, (void *)(gm_sv9j89ufns35b));
                                GM_ST(uintptr_t, (uint8_t *)&t231 + 8, (uintptr_t)(15));
                                gm_tftb1odc739t5 t232 = gm_f_extend_0cs85gs(t230, t231);
                                if (((((uint8_t *)&t232))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t232) + 1, gm_srclt70oog8j9io, 162, 17);
                                gm_b0_1 t233 = GM_LD(gm_b0_1, (((uint8_t *)&t232) + 1));
                            }
                            {
                                void * t234 = (void *)((uint8_t *)&F.l10);
                                gm_b16_8 t235 = gm_l8;
                                gm_b16_8 t236 = GM_LD(gm_b16_8, ((uint8_t *)&t235));
                                gm_tftb1odc739t5 t237 = gm_f_extend_0cs85gs(t234, t236);
                                if (((((uint8_t *)&t237))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t237) + 1, gm_srclt70oog8j9io, 163, 17);
                                gm_b0_1 t238 = GM_LD(gm_b0_1, (((uint8_t *)&t237) + 1));
                            }
                            {
                                {
                                    uint64_t t240 = GM_LD(uintptr_t, (((uint8_t *)&F.l9) + 0) + 8);
                                    void * t241 = ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l9) + 0)));
                                    uint64_t t242 = gm_l21;
                                    uint64_t t243 = (uintptr_t)t242;
                                    if (t243 > (uintptr_t)t240 || (uintptr_t)t240 > (uintptr_t)t240) gm_trap_slice(t243, (uintptr_t)t240, t240, gm_srclt70oog8j9io, 164, 26);
                                    gm_b16_8 t244;
                                    GM_ST(void *, (uint8_t *)&t244, (void *)(((uint8_t *)t241 + t243 * 16)));
                                    GM_ST(uintptr_t, (uint8_t *)&t244 + 8, (uintptr_t)((uintptr_t)t240 - t243));
                                    gm_l27 = t244;
                                }
                                {
                                    gm_l28 = ((uint64_t)0ULL);
                                }
                                {
                                    for (;;) {
                                        uint64_t t246 = gm_l28;
                                        uint64_t t247 = GM_LD(uintptr_t, ((uint8_t *)&gm_l27) + 8);
                                        uint8_t t248 = (t246 < t247);
                                        if (!t248) goto lb245;
                                        {
                                            uint64_t t251 = gm_l28;
                                            uint64_t t252 = (uintptr_t)t251;
                                            uint64_t t253 = GM_LD(uintptr_t, ((uint8_t *)&gm_l27) + 8);
                                            gm_b16_8 t256 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l27)) + t252 * 16));
                                            gm_l29 = t256;
                                            {
                                                void * t258 = (void *)((uint8_t *)&F.l10);
                                                gm_b16_8 t259 = gm_l29;
                                                gm_tftb1odc739t5 t260 = gm_f_extend_0cs85gs(t258, t259);
                                                if (((((uint8_t *)&t260))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t260) + 1, gm_srclt70oog8j9io, 164, 45);
                                                gm_b0_1 t261 = GM_LD(gm_b0_1, (((uint8_t *)&t260) + 1));
                                            }
                                            goto me254;
mn255:;
                                            gm_trap("no match arm matched the scrutinee", gm_srclt70oog8j9io, 164, 17);
me254:;
                                        }
lc245:;
                                        {
                                            uint64_t t263 = gm_l28;
                                            uint64_t r264; bool o265 = __builtin_add_overflow(t263, ((uint64_t)1ULL), &r264);
                                            uint64_t t266 = r264;
                                            gm_l28 = t266;
                                        }
                                    }
lb245:;
                                }
                            }
                            {
                                gm_b16_8 t267 = GM_LD(gm_b16_8, (((uint8_t *)&F.l10) + 0));
                                gm_b16_8 t268 = GM_LD(gm_b16_8, ((uint8_t *)&t267));
                                GM_ST(uint8_t, (uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out) + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out) + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out) + 4, 0);
                                ((uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out))[0] = 0;
                                GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out) + 8, t268);
                                goto gm_fdone;
                            }
                            goto ix180;
ie180:;
ix180:;
                        }
                        goto ix175;
ie175:;
ix175:;
                    }
                    goto me170;
mn171:;
                    gm_trap("no match arm matched the scrutinee", gm_srclt70oog8j9io, 157, 5);
me170:;
                }
lc158:;
                {
                    uint64_t t270 = gm_l20;
                    uint64_t r271; bool o272 = __builtin_add_overflow(t270, ((uint64_t)1ULL), &r271);
                    uint64_t t273 = r271;
                    gm_l20 = t273;
                }
            }
gm_ll162:;
            if (gm_unw.frame == &F.h && gm_unw.label == 3) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb158; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc158; }
            }
            goto gm_lad;
lb158:;
        }
    }
    {
        void * t274 = (void *)((uint8_t *)&F.l10);
        gm_b16_8 t275 = gm_l3;
        gm_tftb1odc739t5 t276 = gm_f_extend_0cs85gs(t274, t275);
        if (((((uint8_t *)&t276))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t276) + 1, gm_srclt70oog8j9io, 169, 5);
        gm_b0_1 t277 = GM_LD(gm_b0_1, (((uint8_t *)&t276) + 1));
    }
    {
        uint64_t t279 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
        uint8_t t280 = (t279 > ((uint64_t)0ULL));
        if (!t280) goto ie278;
        {
            gm_b16_8 t283 = gm_l3;
            gm_b16_8 t284;
            GM_ST(void *, (uint8_t *)&t284, (void *)(gm_so514oj5nbhcu));
            GM_ST(uintptr_t, (uint8_t *)&t284 + 8, (uintptr_t)(1));
            uint8_t t285 = gm_f_ends_with_biriu5l(t283, t284);
            uint8_t t286 = (uint8_t)!t285;
            if (!t286) goto ie282;
            {
                void * t288 = (void *)((uint8_t *)&F.l10);
                gm_tftb1odc739t5 t289 = gm_f_push_t83rg2m(t288, ((uint8_t)10ULL));
                if (((((uint8_t *)&t289))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t289) + 1, gm_srclt70oog8j9io, 171, 39);
                gm_b0_1 t290 = GM_LD(gm_b0_1, (((uint8_t *)&t289) + 1));
            }
            goto ix282;
ie282:;
ix282:;
        }
        {
            void * t291 = (void *)((uint8_t *)&F.l10);
            gm_tftb1odc739t5 t292 = gm_f_push_t83rg2m(t291, ((uint8_t)10ULL));
            if (((((uint8_t *)&t292))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t292) + 1, gm_srclt70oog8j9io, 172, 9);
            gm_b0_1 t293 = GM_LD(gm_b0_1, (((uint8_t *)&t292) + 1));
        }
        goto ix278;
ie278:;
ix278:;
    }
    {
        void * t294 = (void *)((uint8_t *)&F.l10);
        gm_b16_8 t295;
        GM_ST(void *, (uint8_t *)&t295, (void *)(gm_sv9j89ufns35b));
        GM_ST(uintptr_t, (uint8_t *)&t295 + 8, (uintptr_t)(15));
        gm_tftb1odc739t5 t296 = gm_f_extend_0cs85gs(t294, t295);
        if (((((uint8_t *)&t296))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t296) + 1, gm_srclt70oog8j9io, 174, 5);
        gm_b0_1 t297 = GM_LD(gm_b0_1, (((uint8_t *)&t296) + 1));
    }
    {
        void * t298 = (void *)((uint8_t *)&F.l10);
        gm_b16_8 t299 = gm_l8;
        gm_b16_8 t300 = GM_LD(gm_b16_8, ((uint8_t *)&t299));
        gm_tftb1odc739t5 t301 = gm_f_extend_0cs85gs(t298, t300);
        if (((((uint8_t *)&t301))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t301) + 1, gm_srclt70oog8j9io, 175, 5);
        gm_b0_1 t302 = GM_LD(gm_b0_1, (((uint8_t *)&t301) + 1));
    }
    {
        gm_b16_8 t303 = GM_LD(gm_b16_8, (((uint8_t *)&F.l10) + 0));
        gm_b16_8 t304 = GM_LD(gm_b16_8, ((uint8_t *)&t303));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out) + 4, 0);
        ((uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out))[0] = 0;
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out) + 8, t304);
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
__attribute__((aligned(4))) uint64_t gm_f_entry_lines_3jhl7a4(gm_b16_8 a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_entry_lines_3jhl7a4 F;
    gm_b16_8 gm_l0; /* lines */
    uint64_t gm_l1; /* at */
    int32_t gm_l2; /* depth */
    uint8_t gm_l3; /* quote */
    uint64_t gm_l4; /* n */
    uint64_t gm_l5; /* i */
    gm_b16_8 gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    uint8_t gm_l8; /* c */
    GM_STACK_CHECK_SHALLOW(&F, 295u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        gm_l2 = ((int32_t)0LL);
    }
    {
        gm_l3 = ((uint8_t)0ULL);
    }
    {
        gm_l4 = ((uint64_t)0ULL);
    }
    {
        uint64_t t2 = gm_l1;
        gm_l5 = t2;
    }
    {
        for (;;) {
            uint64_t t4 = gm_l5;
            uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            uint8_t t6 = (t4 < t5);
            if (!t6) goto lb3;
            {
                {
                    uint64_t t10 = gm_l5;
                    uint64_t t11 = (uintptr_t)t10;
                    uint64_t t12 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                    if (t11 >= (uintptr_t)(t12)) gm_trap_index(t11, t12, gm_srclt70oog8j9io, 79, 18);
                    gm_b16_8 t13 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t11 * 16));
                    gm_l6 = t13;
                }
                {
                    gm_l7 = ((uint64_t)0ULL);
                }
                {
                    for (;;) {
                        uint64_t t15 = gm_l7;
                        uint64_t t16 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                        uint8_t t17 = (t15 < t16);
                        if (!t17) goto lb14;
                        {
                            uint64_t t20 = gm_l7;
                            uint64_t t21 = (uintptr_t)t20;
                            uint64_t t22 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                            uint8_t t23 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t21 * 1));
                            uint8_t t26 = GM_LD(uint8_t, ((uint8_t *)&t23));
                            gm_l8 = t26;
                            {
                                uint8_t t29 = gm_l3;
                                uint8_t t30 = (!(t29 == ((uint8_t)0ULL)));
                                if (!t30) goto ie28;
                                {
                                    uint8_t t33 = gm_l8;
                                    uint8_t t34 = gm_l3;
                                    uint8_t t35 = ((t33 == t34));
                                    if (!t35) goto ie32;
                                    {
                                        gm_l3 = ((uint8_t)0ULL);
                                    }
                                    goto ix32;
ie32:;
ix32:;
                                }
                                goto ix28;
ie28:;
                                uint8_t t38 = 0;
                                uint8_t t39 = gm_l8;
                                uint8_t t40 = ((t39 == ((uint8_t)34ULL)));
                                if (!t40) {
                                    uint8_t t41 = gm_l8;
                                    uint8_t t42 = ((t41 == ((uint8_t)39ULL)));
                                    t38 = t42;
                                } else t38 = 1;
                                if (!t38) goto ie37;
                                {
                                    uint8_t t44 = gm_l8;
                                    gm_l3 = t44;
                                }
                                goto ix37;
ie37:;
                                uint8_t t46 = gm_l8;
                                uint8_t t47 = ((t46 == ((uint8_t)35ULL)));
                                if (!t47) goto ie45;
                                {
                                    goto lb14;
                                }
                                goto ix45;
ie45:;
                                uint8_t t50 = gm_l8;
                                gm_tqs5p4n2tq19v t51;
                                GM_ST(uint8_t, ((uint8_t *)&t51 + 0), ((uint8_t)91ULL));
                                GM_ST(uint8_t, ((uint8_t *)&t51 + 1), ((uint8_t)123ULL));
                                uint8_t t52 = 0;
                                if (gm_eqjmccqdb90f(((uint8_t *)&t51) + 0, ((uint8_t *)&t50))) t52 = 1;
                                if (gm_eqjmccqdb90f(((uint8_t *)&t51) + 1, ((uint8_t *)&t50))) t52 = 1;
                                uint8_t t53 = (uint8_t)(t52);
                                if (!t53) goto ie49;
                                {
                                    int32_t t55 = gm_l2;
                                    int32_t r56; bool o57 = __builtin_add_overflow(t55, ((int32_t)1LL), &r56);
                                    if (o57) gm_trap("integer overflow in `+`", gm_srclt70oog8j9io, 87, 17);
                                    int32_t t58 = r56;
                                    gm_l2 = t58;
                                }
                                goto ix49;
ie49:;
                                uint8_t t60 = gm_l8;
                                gm_tqs5p4n2tq19v t61;
                                GM_ST(uint8_t, ((uint8_t *)&t61 + 0), ((uint8_t)93ULL));
                                GM_ST(uint8_t, ((uint8_t *)&t61 + 1), ((uint8_t)125ULL));
                                uint8_t t62 = 0;
                                if (gm_eqjmccqdb90f(((uint8_t *)&t61) + 0, ((uint8_t *)&t60))) t62 = 1;
                                if (gm_eqjmccqdb90f(((uint8_t *)&t61) + 1, ((uint8_t *)&t60))) t62 = 1;
                                uint8_t t63 = (uint8_t)(t62);
                                if (!t63) goto ie59;
                                {
                                    int32_t t65 = gm_l2;
                                    int32_t r66; bool o67 = __builtin_sub_overflow(t65, ((int32_t)1LL), &r66);
                                    if (o67) gm_trap("integer overflow in `-`", gm_srclt70oog8j9io, 89, 17);
                                    int32_t t68 = r66;
                                    gm_l2 = t68;
                                }
                                goto ix59;
ie59:;
ix59:;
ix49:;
ix45:;
ix37:;
ix28:;
                            }
                            goto me24;
mn25:;
                            gm_trap("no match arm matched the scrutinee", gm_srclt70oog8j9io, 79, 9);
me24:;
                        }
lc14:;
                        {
                            uint64_t t70 = gm_l7;
                            uint64_t r71; bool o72 = __builtin_add_overflow(t70, ((uint64_t)1ULL), &r71);
                            uint64_t t73 = r71;
                            gm_l7 = t73;
                        }
                    }
lb14:;
                }
            }
            {
                gm_l3 = ((uint8_t)0ULL);
            }
            {
                uint64_t t74 = gm_l4;
                uint64_t r75; bool o76 = __builtin_add_overflow(t74, ((uint64_t)1ULL), &r75);
                if (o76) gm_trap("integer overflow in `+`", gm_srclt70oog8j9io, 93, 9);
                uint64_t t77 = r75;
                gm_l4 = t77;
            }
            {
                uint64_t t78 = gm_l5;
                uint64_t r79; bool o80 = __builtin_add_overflow(t78, ((uint64_t)1ULL), &r79);
                if (o80) gm_trap("integer overflow in `+`", gm_srclt70oog8j9io, 94, 9);
                uint64_t t81 = r79;
                gm_l5 = t81;
            }
            {
                int32_t t83 = gm_l2;
                uint8_t t84 = (t83 <= ((int32_t)0LL));
                if (!t84) goto ie82;
                {
                    goto lb3;
                }
                goto ix82;
ie82:;
ix82:;
            }
lc3:;
        }
lb3:;
    }
    {
        uint64_t t86 = gm_l4;
        uint8_t t87 = 0;
        F.ret = t86;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_header_of_2u6atoc(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_header_of_2u6atoc F;
    gm_b16_8 gm_l0; /* line */
    gm_b16_8 gm_l1; /* t */
    uint64_t gm_l2; /* end */
    gm_b16_8 gm_l3; /* inner */
    GM_STACK_CHECK_SHALLOW(&F, 519u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_b16_8 t2 = gm_l0;
        gm_b16_8 t3 = gm_f_trim_ascii_c9j36d1(t2);
        gm_l1 = t3;
    }
    {
        uint8_t t5 = 0;
        gm_b16_8 t6 = gm_l1;
        gm_b16_8 t7;
        GM_ST(void *, (uint8_t *)&t7, (void *)(gm_sk754mj3p1el6));
        GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)(1));
        uint8_t t8 = gm_f_starts_with_6vvt01e(t6, t7);
        uint8_t t9 = (uint8_t)!t8;
        if (!t9) {
            gm_b16_8 t10 = gm_l1;
            gm_b16_8 t11;
            GM_ST(void *, (uint8_t *)&t11, (void *)(gm_shv5ofgc65v68));
            GM_ST(uintptr_t, (uint8_t *)&t11 + 8, (uintptr_t)(2));
            uint8_t t12 = gm_f_starts_with_6vvt01e(t10, t11);
            t5 = t12;
        } else t5 = 1;
        if (!t5) goto ie4;
        {
            gm_th2lqbo77pvsc t14;
            memset(&t14, 0, sizeof t14);
            uint8_t t15 = 0;
            F.ret = t14;
            goto gm_fret;
        }
        goto ix4;
ie4:;
ix4:;
    }
    {
        gm_b16_8 t16 = gm_l1;
        gm_b16_8 t17;
        GM_ST(void *, (uint8_t *)&t17, (void *)(gm_s2053hhvf16fm));
        GM_ST(uintptr_t, (uint8_t *)&t17 + 8, (uintptr_t)(1));
        gm_tf5srh4mp4b3i t18 = gm_f_find_nab29co(t16, t17);
        uint64_t t19;
        if (((((uint8_t *)&t18))[8] != 0)) {
            uint64_t t20 = GM_LD(uint64_t, (((uint8_t *)&t18) + 0));
            t19 = t20;
        } else {
            gm_th2lqbo77pvsc t21;
            memset(&t21, 0, sizeof t21);
            uint8_t t22 = 0;
            F.ret = t21;
            goto gm_fret;
        }
        gm_l2 = t19;
    }
    {
        uint64_t t23 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        void * t24 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)));
        uint64_t t25 = (uintptr_t)((uint64_t)1ULL);
        uint64_t t26 = gm_l2;
        uint64_t t27 = (uintptr_t)t26;
        if (t25 > t27 || t27 > (uintptr_t)t23) gm_trap_slice(t25, t27, t23, gm_srclt70oog8j9io, 40, 30);
        gm_b16_8 t28;
        GM_ST(void *, (uint8_t *)&t28, (void *)(((uint8_t *)t24 + t25 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(t27 - t25));
        gm_b16_8 t29 = gm_f_trim_ascii_c9j36d1(t28);
        gm_l3 = t29;
    }
    {
        uint64_t t31 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
        uint8_t t32 = (t31 >= ((uint64_t)2ULL));
        if (!t32) goto ie30;
        uint8_t t33 = 0;
        uint64_t t34 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t35 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
        if (t34 >= (uintptr_t)(t35)) gm_trap_index(t34, t35, gm_srclt70oog8j9io, 41, 28);
        uint8_t t36 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t34 * 1));
        uint8_t t37 = ((t36 == ((uint8_t)34ULL)));
        if (!t37) {
            uint64_t t38 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t39 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
            if (t38 >= (uintptr_t)(t39)) gm_trap_index(t38, t39, gm_srclt70oog8j9io, 41, 47);
            uint8_t t40 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t38 * 1));
            uint8_t t41 = ((t40 == ((uint8_t)39ULL)));
            t33 = t41;
        } else t33 = 1;
        if (!t33) goto ie30;
        uint64_t t42 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
        uint64_t r43; bool o44 = __builtin_sub_overflow(t42, ((uint64_t)1ULL), &r43);
        if (o44) gm_trap("integer overflow in `-`", gm_srclt70oog8j9io, 41, 75);
        uint64_t t45 = r43;
        uint64_t t46 = (uintptr_t)t45;
        uint64_t t47 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
        if (t46 >= (uintptr_t)(t47)) gm_trap_index(t46, t47, gm_srclt70oog8j9io, 41, 69);
        uint8_t t48 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t46 * 1));
        uint64_t t49 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t50 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
        if (t49 >= (uintptr_t)(t50)) gm_trap_index(t49, t50, gm_srclt70oog8j9io, 41, 93);
        uint8_t t51 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t49 * 1));
        uint8_t t52 = ((t48 == t51));
        if (!t52) goto ie30;
        {
            uint64_t t54 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
            void * t55 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)));
            uint64_t t56 = (uintptr_t)((uint64_t)1ULL);
            uint64_t t57 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
            uint64_t r58; bool o59 = __builtin_sub_overflow(t57, ((uint64_t)1ULL), &r58);
            if (o59) gm_trap("integer overflow in `-`", gm_srclt70oog8j9io, 42, 25);
            uint64_t t60 = r58;
            uint64_t t61 = (uintptr_t)t60;
            if (t56 > t61 || t61 > (uintptr_t)t54) gm_trap_slice(t56, t61, t54, gm_srclt70oog8j9io, 42, 16);
            gm_b16_8 t62;
            GM_ST(void *, (uint8_t *)&t62, (void *)(((uint8_t *)t55 + t56 * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t62 + 8, (uintptr_t)(t61 - t56));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 20, 0);
            ((uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out))[16] = 1;
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 0, t62);
            goto gm_fdone;
        }
        goto ix30;
ie30:;
ix30:;
    }
    {
        gm_b16_8 t63 = gm_l3;
        GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 20, 0);
        ((uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out))[16] = 1;
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 0, t63);
        goto gm_fdone;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_b16_8 gm_f_inline_value_0impjcn(void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_inline_value_0impjcn F;
    gm_b16_8 gm_l2; /* p */
    gm_b16_8 gm_l4; /* h */
    gm_b16_8 gm_l5; /* s */
    gm_t6j6smj7qoctu gm_l7; /* r */
    gm_b16_8 gm_l8; /* s */
    GM_STACK_CHECK(&F, 895u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s7mfvroqmll1f));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(2));
        gm_tftb1odc739t5 t4 = gm_f_extend_0cs85gs(t2, t3);
        if (((((uint8_t *)&t4))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t4) + 1, gm_srclt70oog8j9io, 117, 5);
        gm_b0_1 t5 = GM_LD(gm_b0_1, (((uint8_t *)&t4) + 1));
    }
    {
        uint8_t t11 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
        switch (t11) { case ((uint8_t)0ULL): goto ma7; case ((uint8_t)1ULL): goto ma9; case ((uint8_t)2ULL): goto ma8; default: goto ma10; }
ma7:;
        uint8_t t13 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
        if (t13 != ((uint8_t)0ULL)) goto mn12;
        gm_b16_8 t14 = GM_LD(gm_b16_8, (((uint8_t *)a0) + 8));
        gm_l2 = t14;
        {
            void * t16 = (void *)((uint8_t *)&F.l1);
            gm_b16_8 t17 = gm_l2;
            gm_b16_8 t18 = gm_f_toml_string_cagbrmp((void *)NULL, cx, NULL, t17);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tnpifv20irnph t19;
            GM_ST(gm_b16_8, ((uint8_t *)&t19 + 0), t18);
            gm_tftb1odc739t5 t20 = gm_f_write_r83e4t6(t16, t19);
            if (((((uint8_t *)&t20))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t20) + 1, gm_srclt70oog8j9io, 119, 23);
            gm_b0_1 t21 = GM_LD(gm_b0_1, (((uint8_t *)&t20) + 1));
        }
        goto me6;
mn12:;
ma8:;
        uint8_t t23 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
        if (t23 != ((uint8_t)2ULL)) goto mn22;
        gm_tt008mgut8glb t24 = GM_LD(gm_tt008mgut8glb, (((uint8_t *)a0) + 8));
        F.l3 = t24;
        {
            void * t26 = (void *)((uint8_t *)&F.l1);
            gm_b16_8 t27 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
            gm_b16_8 t28 = gm_f_toml_string_cagbrmp((void *)NULL, cx, NULL, t27);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tnpifv20irnph t29;
            GM_ST(gm_b16_8, ((uint8_t *)&t29 + 0), t28);
            gm_tftb1odc739t5 t30 = gm_f_write_ctdpla1(t26, t29);
            if (((((uint8_t *)&t30))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t30) + 1, gm_srclt70oog8j9io, 121, 13);
            gm_b0_1 t31 = GM_LD(gm_b0_1, (((uint8_t *)&t30) + 1));
        }
        {
            if (!(((((uint8_t *)&F.l3) + 16))[16] != 0)) goto ie32;
            gm_b16_8 t33 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 16));
            gm_l4 = t33;
            {
                void * t35 = (void *)((uint8_t *)&F.l1);
                gm_b16_8 t36 = gm_l4;
                gm_b16_8 t37 = gm_f_toml_string_cagbrmp((void *)NULL, cx, NULL, t36);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tnpifv20irnph t38;
                GM_ST(gm_b16_8, ((uint8_t *)&t38 + 0), t37);
                gm_tftb1odc739t5 t39 = gm_f_write_rbbkc88(t35, t38);
                if (((((uint8_t *)&t39))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t39) + 1, gm_srclt70oog8j9io, 122, 32);
                gm_b0_1 t40 = GM_LD(gm_b0_1, (((uint8_t *)&t39) + 1));
            }
            goto ix32;
ie32:;
ix32:;
        }
        {
            if (!(((((uint8_t *)&F.l3) + 40))[16] != 0)) goto ie41;
            gm_b16_8 t42 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 40));
            gm_l5 = t42;
            {
                void * t44 = (void *)((uint8_t *)&F.l1);
                gm_b16_8 t45 = gm_l5;
                gm_b16_8 t46 = gm_f_toml_string_cagbrmp((void *)NULL, cx, NULL, t45);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tnpifv20irnph t47;
                GM_ST(gm_b16_8, ((uint8_t *)&t47 + 0), t46);
                gm_tftb1odc739t5 t48 = gm_f_write_5jambfs(t44, t47);
                if (((((uint8_t *)&t48))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t48) + 1, gm_srclt70oog8j9io, 123, 32);
                gm_b0_1 t49 = GM_LD(gm_b0_1, (((uint8_t *)&t48) + 1));
            }
            goto ix41;
ie41:;
ix41:;
        }
        goto me6;
mn22:;
ma9:;
        uint8_t t51 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
        if (t51 != ((uint8_t)1ULL)) goto mn50;
        gm_t03ccdabio1fj t52 = GM_LD(gm_t03ccdabio1fj, (((uint8_t *)a0) + 8));
        F.l6 = t52;
        {
            void * t54 = (void *)((uint8_t *)&F.l1);
            gm_b16_8 t55 = GM_LD(gm_b16_8, (((uint8_t *)&F.l6) + 0));
            gm_b16_8 t56 = gm_f_toml_string_cagbrmp((void *)NULL, cx, NULL, t55);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tnpifv20irnph t57;
            GM_ST(gm_b16_8, ((uint8_t *)&t57 + 0), t56);
            gm_tftb1odc739t5 t58 = gm_f_write_ivc38em(t54, t57);
            if (((((uint8_t *)&t58))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t58) + 1, gm_srclt70oog8j9io, 126, 13);
            gm_b0_1 t59 = GM_LD(gm_b0_1, (((uint8_t *)&t58) + 1));
        }
        {
            if (!(((((uint8_t *)&F.l6) + 16))[32] != 0)) goto ie60;
            gm_t6j6smj7qoctu t61 = GM_LD(gm_t6j6smj7qoctu, (((uint8_t *)&F.l6) + 16));
            gm_l7 = t61;
            {
                void * t63 = (void *)((uint8_t *)&F.l1);
                gm_t6j6smj7qoctu t64;
                gm_b16_8 t65 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l7) + 0));
                GM_ST(gm_b16_8, (uint8_t *)&t64 + 0, t65);
                gm_b16_8 t66 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l7) + 16));
                gm_b16_8 t67 = gm_f_toml_string_cagbrmp((void *)NULL, cx, NULL, t66);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(gm_b16_8, (uint8_t *)&t64 + 16, t67);
                gm_tftb1odc739t5 t68 = gm_f_write_gle9guv(t63, &t64);
                if (((((uint8_t *)&t68))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t68) + 1, gm_srclt70oog8j9io, 127, 35);
                gm_b0_1 t69 = GM_LD(gm_b0_1, (((uint8_t *)&t68) + 1));
            }
            goto ix60;
ie60:;
ix60:;
        }
        {
            if (!(((((uint8_t *)&F.l6) + 56))[16] != 0)) goto ie70;
            gm_b16_8 t71 = GM_LD(gm_b16_8, (((uint8_t *)&F.l6) + 56));
            gm_l8 = t71;
            {
                void * t73 = (void *)((uint8_t *)&F.l1);
                gm_b16_8 t74 = gm_l8;
                gm_b16_8 t75 = gm_f_toml_string_cagbrmp((void *)NULL, cx, NULL, t74);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tnpifv20irnph t76;
                GM_ST(gm_b16_8, ((uint8_t *)&t76 + 0), t75);
                gm_tftb1odc739t5 t77 = gm_f_write_5jambfs(t73, t76);
                if (((((uint8_t *)&t77))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t77) + 1, gm_srclt70oog8j9io, 128, 32);
                gm_b0_1 t78 = GM_LD(gm_b0_1, (((uint8_t *)&t77) + 1));
            }
            goto ix70;
ie70:;
ix70:;
        }
        goto me6;
mn50:;
ma10:;
        gm_trap("no match arm matched the scrutinee", gm_srclt70oog8j9io, 118, 5);
me6:;
    }
    {
        void * t79 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t80;
        GM_ST(void *, (uint8_t *)&t80, (void *)(gm_s5m9qimbv7e8k));
        GM_ST(uintptr_t, (uint8_t *)&t80 + 8, (uintptr_t)(2));
        gm_tftb1odc739t5 t81 = gm_f_extend_0cs85gs(t79, t80);
        if (((((uint8_t *)&t81))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t81) + 1, gm_srclt70oog8j9io, 131, 5);
        gm_b0_1 t82 = GM_LD(gm_b0_1, (((uint8_t *)&t81) + 1));
    }
    {
        gm_b16_8 t83 = GM_LD(gm_b16_8, (((uint8_t *)&F.l1) + 0));
        gm_b16_8 t84 = GM_LD(gm_b16_8, ((uint8_t *)&t83));
        uint8_t t85 = 0;
        F.ret = t84;
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
extern __attribute__((aligned(4))) uint8_t gm_f_is_blank_or_comment_lubpopi(gm_b16_8 a0);
extern __attribute__((aligned(4))) uint8_t gm_f_is_header_uakq83c(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_join_without_ej3mkbe(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, uint64_t a1, uint64_t a2) {
    struct gm_fr_join_without_ej3mkbe F;
    gm_b16_8 gm_l0; /* lines */
    uint64_t gm_l1; /* from */
    uint64_t gm_l2; /* to */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    gm_b16_8 gm_l6; /* l */
    gm_b16_8 gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    gm_b16_8 gm_l9; /* l */
    GM_STACK_CHECK(&F, 479u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        gm_f_list_uvfu1k1((void *)&F.l3, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            uint64_t t3 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            void * t4 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)));
            uint64_t t5 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t6 = gm_l1;
            uint64_t t7 = (uintptr_t)t6;
            if (t5 > t7 || t7 > (uintptr_t)t3) gm_trap_slice(t5, t7, t3, gm_srclt70oog8j9io, 221, 14);
            gm_b16_8 t8;
            GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t4 + t5 * 16)));
            GM_ST(uintptr_t, (uint8_t *)&t8 + 8, (uintptr_t)(t7 - t5));
            gm_l4 = t8;
        }
        {
            gm_l5 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t10 = gm_l5;
                uint64_t t11 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                uint8_t t12 = (t10 < t11);
                if (!t12) goto lb9;
                {
                    uint64_t t15 = gm_l5;
                    uint64_t t16 = (uintptr_t)t15;
                    uint64_t t17 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                    gm_b16_8 t20 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t16 * 16));
                    gm_l6 = t20;
                    {
                        void * t22 = (void *)((uint8_t *)&F.l3);
                        gm_b16_8 t23 = gm_l6;
                        gm_tftb1odc739t5 t24 = gm_f_extend_0cs85gs(t22, t23);
                        if (((((uint8_t *)&t24))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t24) + 1, gm_srclt70oog8j9io, 221, 31);
                        gm_b0_1 t25 = GM_LD(gm_b0_1, (((uint8_t *)&t24) + 1));
                    }
                    goto me18;
mn19:;
                    gm_trap("no match arm matched the scrutinee", gm_srclt70oog8j9io, 221, 5);
me18:;
                }
lc9:;
                {
                    uint64_t t27 = gm_l5;
                    uint64_t r28; bool o29 = __builtin_add_overflow(t27, ((uint64_t)1ULL), &r28);
                    uint64_t t30 = r28;
                    gm_l5 = t30;
                }
            }
lb9:;
        }
    }
    {
        {
            uint64_t t32 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            void * t33 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)));
            uint64_t t34 = gm_l2;
            uint64_t t35 = (uintptr_t)t34;
            if (t35 > (uintptr_t)t32 || (uintptr_t)t32 > (uintptr_t)t32) gm_trap_slice(t35, (uintptr_t)t32, t32, gm_srclt70oog8j9io, 222, 14);
            gm_b16_8 t36;
            GM_ST(void *, (uint8_t *)&t36, (void *)(((uint8_t *)t33 + t35 * 16)));
            GM_ST(uintptr_t, (uint8_t *)&t36 + 8, (uintptr_t)((uintptr_t)t32 - t35));
            gm_l7 = t36;
        }
        {
            gm_l8 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t38 = gm_l8;
                uint64_t t39 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                uint8_t t40 = (t38 < t39);
                if (!t40) goto lb37;
                {
                    uint64_t t43 = gm_l8;
                    uint64_t t44 = (uintptr_t)t43;
                    uint64_t t45 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                    gm_b16_8 t48 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l7)) + t44 * 16));
                    gm_l9 = t48;
                    {
                        void * t50 = (void *)((uint8_t *)&F.l3);
                        gm_b16_8 t51 = gm_l9;
                        gm_tftb1odc739t5 t52 = gm_f_extend_0cs85gs(t50, t51);
                        if (((((uint8_t *)&t52))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t52) + 1, gm_srclt70oog8j9io, 222, 28);
                        gm_b0_1 t53 = GM_LD(gm_b0_1, (((uint8_t *)&t52) + 1));
                    }
                    goto me46;
mn47:;
                    gm_trap("no match arm matched the scrutinee", gm_srclt70oog8j9io, 222, 5);
me46:;
                }
lc37:;
                {
                    uint64_t t55 = gm_l8;
                    uint64_t r56; bool o57 = __builtin_add_overflow(t55, ((uint64_t)1ULL), &r56);
                    uint64_t t58 = r56;
                    gm_l8 = t58;
                }
            }
lb37:;
        }
    }
    {
        gm_b16_8 t59 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
        gm_b16_8 t60 = GM_LD(gm_b16_8, ((uint8_t *)&t59));
        uint8_t t61 = 0;
        F.ret = t60;
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
__attribute__((aligned(4))) void gm_f_key_of_99iu4dv(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_key_of_99iu4dv F;
    gm_b16_8 gm_l0; /* line */
    gm_b16_8 gm_l1; /* t */
    uint64_t gm_l2; /* eq */
    gm_b16_8 gm_l3; /* k */
    uint64_t gm_l4; /* dot */
    GM_STACK_CHECK_SHALLOW(&F, 607u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_b16_8 t2 = gm_l0;
        gm_b16_8 t3 = gm_f_trim_ascii_c9j36d1(t2);
        gm_l1 = t3;
    }
    {
        gm_b16_8 t4 = gm_l1;
        gm_b16_8 t5;
        GM_ST(void *, (uint8_t *)&t5, (void *)(gm_sh0j2ggs1vvoa));
        GM_ST(uintptr_t, (uint8_t *)&t5 + 8, (uintptr_t)(1));
        gm_tf5srh4mp4b3i t6 = gm_f_find_nab29co(t4, t5);
        uint64_t t7;
        if (((((uint8_t *)&t6))[8] != 0)) {
            uint64_t t8 = GM_LD(uint64_t, (((uint8_t *)&t6) + 0));
            t7 = t8;
        } else {
            gm_th2lqbo77pvsc t9;
            memset(&t9, 0, sizeof t9);
            uint8_t t10 = 0;
            F.ret = t9;
            goto gm_fret;
        }
        gm_l2 = t7;
    }
    {
        uint64_t t11 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        void * t12 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)));
        uint64_t t13 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t14 = gm_l2;
        uint64_t t15 = (uintptr_t)t14;
        if (t13 > t15 || t15 > (uintptr_t)t11) gm_trap_slice(t13, t15, t11, gm_srclt70oog8j9io, 62, 26);
        gm_b16_8 t16;
        GM_ST(void *, (uint8_t *)&t16, (void *)(((uint8_t *)t12 + t13 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t16 + 8, (uintptr_t)(t15 - t13));
        gm_b16_8 t17 = gm_f_trim_ascii_c9j36d1(t16);
        gm_l3 = t17;
    }
    {
        uint64_t t19 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
        uint8_t t20 = (t19 >= ((uint64_t)2ULL));
        if (!t20) goto ie18;
        uint8_t t21 = 0;
        uint64_t t22 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t23 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
        if (t22 >= (uintptr_t)(t23)) gm_trap_index(t22, t23, gm_srclt70oog8j9io, 63, 24);
        uint8_t t24 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t22 * 1));
        uint8_t t25 = ((t24 == ((uint8_t)34ULL)));
        if (!t25) {
            uint64_t t26 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t27 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
            if (t26 >= (uintptr_t)(t27)) gm_trap_index(t26, t27, gm_srclt70oog8j9io, 63, 39);
            uint8_t t28 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t26 * 1));
            uint8_t t29 = ((t28 == ((uint8_t)39ULL)));
            t21 = t29;
        } else t21 = 1;
        if (!t21) goto ie18;
        uint64_t t30 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
        uint64_t r31; bool o32 = __builtin_sub_overflow(t30, ((uint64_t)1ULL), &r31);
        if (o32) gm_trap("integer overflow in `-`", gm_srclt70oog8j9io, 63, 59);
        uint64_t t33 = r31;
        uint64_t t34 = (uintptr_t)t33;
        uint64_t t35 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
        if (t34 >= (uintptr_t)(t35)) gm_trap_index(t34, t35, gm_srclt70oog8j9io, 63, 57);
        uint8_t t36 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t34 * 1));
        uint64_t t37 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t38 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
        if (t37 >= (uintptr_t)(t38)) gm_trap_index(t37, t38, gm_srclt70oog8j9io, 63, 73);
        uint8_t t39 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t37 * 1));
        uint8_t t40 = ((t36 == t39));
        if (!t40) goto ie18;
        {
            uint64_t t42 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
            void * t43 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)));
            uint64_t t44 = (uintptr_t)((uint64_t)1ULL);
            uint64_t t45 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
            uint64_t r46; bool o47 = __builtin_sub_overflow(t45, ((uint64_t)1ULL), &r46);
            if (o47) gm_trap("integer overflow in `-`", gm_srclt70oog8j9io, 63, 92);
            uint64_t t48 = r46;
            uint64_t t49 = (uintptr_t)t48;
            if (t44 > t49 || t49 > (uintptr_t)t42) gm_trap_slice(t44, t49, t42, gm_srclt70oog8j9io, 63, 87);
            gm_b16_8 t50;
            GM_ST(void *, (uint8_t *)&t50, (void *)(((uint8_t *)t43 + t44 * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t50 + 8, (uintptr_t)(t49 - t44));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 20, 0);
            ((uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out))[16] = 1;
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 0, t50);
            goto gm_fdone;
        }
        goto ix18;
ie18:;
ix18:;
    }
    {
        uint64_t t52 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
        uint8_t t53 = (t52 > ((uint64_t)0ULL));
        if (!t53) goto ie51;
        uint64_t t54 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t55 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
        if (t54 >= (uintptr_t)(t55)) gm_trap_index(t54, t55, gm_srclt70oog8j9io, 65, 22);
        uint8_t t56 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t54 * 1));
        uint8_t t57 = (!(t56 == ((uint8_t)34ULL)));
        if (!t57) goto ie51;
        uint64_t t58 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t59 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
        if (t58 >= (uintptr_t)(t59)) gm_trap_index(t58, t59, gm_srclt70oog8j9io, 65, 42);
        uint8_t t60 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t58 * 1));
        uint8_t t61 = (!(t60 == ((uint8_t)39ULL)));
        if (!t61) goto ie51;
        {
            gm_b16_8 t64 = gm_l3;
            gm_b16_8 t65;
            GM_ST(void *, (uint8_t *)&t65, (void *)(gm_sl3q52ep6aa53));
            GM_ST(uintptr_t, (uint8_t *)&t65 + 8, (uintptr_t)(1));
            gm_tf5srh4mp4b3i t66 = gm_f_find_nab29co(t64, t65);
            if (!((((uint8_t *)&t66))[8] != 0)) goto ie63;
            uint64_t t67 = GM_LD(uint64_t, ((uint8_t *)&t66));
            gm_l4 = t67;
            {
                uint64_t t69 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                void * t70 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)));
                uint64_t t71 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t72 = gm_l4;
                uint64_t t73 = (uintptr_t)t72;
                if (t71 > t73 || t73 > (uintptr_t)t69) gm_trap_slice(t71, t73, t69, gm_srclt70oog8j9io, 66, 61);
                gm_b16_8 t74;
                GM_ST(void *, (uint8_t *)&t74, (void *)(((uint8_t *)t70 + t71 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t74 + 8, (uintptr_t)(t73 - t71));
                gm_b16_8 t75 = gm_f_trim_ascii_c9j36d1(t74);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 17, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 18, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 20, 0);
                ((uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out))[16] = 1;
                GM_ST(gm_b16_8, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 0, t75);
                goto gm_fdone;
            }
            goto ix63;
ie63:;
ix63:;
        }
        goto ix51;
ie51:;
ix51:;
    }
    {
        gm_b16_8 t76 = gm_l3;
        GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 20, 0);
        ((uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out))[16] = 1;
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 0, t76);
        goto gm_fdone;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_normalize_newlines_183t7r2(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_normalize_newlines_183t7r2 F;
    gm_b16_8 gm_l0; /* text */
    GM_STACK_CHECK(&F, 219u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_b16_8 t3 = gm_l0;
        gm_b16_8 t4;
        GM_ST(void *, (uint8_t *)&t4, (void *)(gm_skmt2kt2p7v0u));
        GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(2));
        gm_tf5srh4mp4b3i t5 = gm_f_find_nab29co(t3, t4);
        uint8_t t6;
        if (((((uint8_t *)&t5))[8] != 0)) goto mn8;
        t6 = ((uint8_t)1);
        goto me7;
mn8:;
        t6 = ((uint8_t)0);
        goto me7;
mn9:;
        gm_trap("no match arm matched the scrutinee", gm_srclt70oog8j9io, 16, 8);
me7:;
        if (!t6) goto ie2;
        {
            gm_b16_8 t11 = gm_l0;
            uint8_t t12 = 0;
            F.ret = t11;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        gm_b16_8 t13 = gm_l0;
        gm_b16_8 t14;
        GM_ST(void *, (uint8_t *)&t14, (void *)(gm_skmt2kt2p7v0u));
        GM_ST(uintptr_t, (uint8_t *)&t14 + 8, (uintptr_t)(2));
        gm_b16_8 t15;
        GM_ST(void *, (uint8_t *)&t15, (void *)(gm_so514oj5nbhcu));
        GM_ST(uintptr_t, (uint8_t *)&t15 + 8, (uintptr_t)(1));
        gm_tabb710d9mq2e t16;
        gm_f_replace_alloc_t7hndlq((void *)&t16, (void *)NULL, cx, NULL, t13, t14, t15);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t16))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t16) + 8, gm_srclt70oog8j9io, 17, 12);
        gm_b16_8 t17 = GM_LD(gm_b16_8, (((uint8_t *)&t16) + 8));
        gm_b16_8 t18 = GM_LD(gm_b16_8, ((uint8_t *)&t17));
        uint8_t t19 = 0;
        F.ret = t18;
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
__attribute__((aligned(4))) void gm_f_remove_dependency_4ju7b0h(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1) {
    struct gm_fr_remove_dependency_4ju7b0h F;
    gm_b16_8 gm_l0; /* original */
    gm_b16_8 gm_l1; /* name */
    gm_b16_8 gm_l2; /* text */
    gm_b16_8 gm_l4; /* missing */
    gm_t2lqu8jsj511d gm_l5; /* r */
    uint8_t gm_l7; /* found */
    uint64_t gm_l8; /* i */
    uint64_t gm_l9; /* n */
    gm_b16_8 gm_l10; /* k */
    uint64_t gm_l11; /* <tmp> */
    uint64_t gm_l12; /* <tmp> */
    uint64_t gm_l13; /* j */
    gm_b16_8 gm_l14; /* wanted */
    gm_t2lqu8jsj511d gm_l15; /* r */
    uint64_t gm_l16; /* first */
    GM_STACK_CHECK(&F, 1417u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        gm_b16_8 t2 = gm_l0;
        gm_b16_8 t3 = gm_f_normalize_newlines_183t7r2((void *)NULL, cx, NULL, t2);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l2 = t3;
    }
    {
        gm_b16_8 t4 = gm_l2;
        gm_f_split_lines_jecth9m((void *)&F.l3, (void *)NULL, cx, NULL, t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_b16_8 t5 = gm_l1;
        gm_tnpifv20irnph t6;
        GM_ST(gm_b16_8, ((uint8_t *)&t6 + 0), t5);
        gm_tabb710d9mq2e t7;
        gm_f_format_rkrg0f2((void *)&t7, (void *)NULL, cx, NULL, t6);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t7))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t7) + 8, gm_srclt70oog8j9io, 183, 16);
        gm_b16_8 t8 = GM_LD(gm_b16_8, (((uint8_t *)&t7) + 8));
        gm_l4 = t8;
    }
    {
        gm_b16_8 t10 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
        gm_b16_8 t11 = GM_LD(gm_b16_8, ((uint8_t *)&t10));
        gm_b16_8 t12;
        GM_ST(void *, (uint8_t *)&t12, (void *)(gm_skof48dte1s1o));
        GM_ST(uintptr_t, (uint8_t *)&t12 + 8, (uintptr_t)(12));
        gm_tc70a9dbbmgkg t13;
        gm_f_table_range_09bngqh((void *)&t13, (void *)NULL, cx, NULL, t11, t12);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (!((((uint8_t *)&t13))[16] != 0)) goto ie9;
        gm_t2lqu8jsj511d t14 = GM_LD(gm_t2lqu8jsj511d, ((uint8_t *)&t13));
        gm_l5 = t14;
        {
            gm_f_list_epvducg((void *)&F.l6, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_l7 = ((uint8_t)0);
        }
        {
            gm_l8 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t17 = gm_l8;
                uint64_t t18 = GM_LD(uintptr_t, (((uint8_t *)&F.l3) + 0) + 8);
                uint8_t t19 = (t17 < t18);
                if (!t19) goto lb16;
                {
                    uint64_t t23 = gm_l8;
                    uint64_t t24 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 0));
                    uint8_t t25 = (t23 > t24);
                    if (!t25) goto ie22;
                    uint64_t t26 = gm_l8;
                    uint64_t t27 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
                    uint8_t t28 = (t26 < t27);
                    if (!t28) goto ie22;
                    uint64_t t29 = gm_l8;
                    uint64_t t30 = (uintptr_t)t29;
                    uint64_t t31 = GM_LD(uintptr_t, (((uint8_t *)&F.l3) + 0) + 8);
                    if (t30 >= (uintptr_t)(t31)) gm_trap_index(t30, t31, gm_srclt70oog8j9io, 190, 71);
                    gm_b16_8 t32 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l3) + 0)) + t30 * 16));
                    uint8_t t33 = gm_f_is_blank_or_comment_lubpopi(t32);
                    uint8_t t34 = (uint8_t)!t33;
                    if (!t34) goto ie22;
                    {
                        gm_b16_8 t36 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
                        gm_b16_8 t37 = GM_LD(gm_b16_8, ((uint8_t *)&t36));
                        uint64_t t38 = gm_l8;
                        uint64_t t39 = gm_f_entry_lines_3jhl7a4(t37, t38);
                        gm_l9 = t39;
                    }
                    {
                        uint64_t t40 = gm_l8;
                        uint64_t t41 = (uintptr_t)t40;
                        uint64_t t42 = GM_LD(uintptr_t, (((uint8_t *)&F.l3) + 0) + 8);
                        if (t41 >= (uintptr_t)(t42)) gm_trap_index(t41, t42, gm_srclt70oog8j9io, 192, 29);
                        gm_b16_8 t43 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l3) + 0)) + t41 * 16));
                        gm_th2lqbo77pvsc t44;
                        gm_f_key_of_99iu4dv((void *)&t44, (void *)NULL, cx, NULL, t43);
                        if (gm_unw.kind) {
                            goto gm_ll20;
                        }
                        gm_b16_8 t45;
                        if (((((uint8_t *)&t44))[16] != 0)) {
                            gm_b16_8 t46 = GM_LD(gm_b16_8, (((uint8_t *)&t44) + 0));
                            t45 = t46;
                        } else {
                            gm_b16_8 t47;
                            GM_ST(void *, (uint8_t *)&t47, (void *)(gm_sr4qbrk6tlgaa));
                            GM_ST(uintptr_t, (uint8_t *)&t47 + 8, (uintptr_t)(0));
                            t45 = t47;
                        }
                        gm_l10 = t45;
                    }
                    {
                        gm_b16_8 t49 = gm_l10;
                        gm_b16_8 t50 = gm_l1;
                        uint8_t t51 = (gm_eq0iqiubfm7e(((uint8_t *)&t49), ((uint8_t *)&t50)));
                        if (!t51) goto ie48;
                        {
                            gm_l7 = ((uint8_t)1);
                        }
                        {
                            for (;;) {
                                uint64_t t54 = GM_LD(uintptr_t, (((uint8_t *)&F.l6) + 0) + 8);
                                uint64_t t55 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 0));
                                uint64_t r56; bool o57 = __builtin_add_overflow(t55, ((uint64_t)1ULL), &r56);
                                if (o57) gm_trap("integer overflow in `+`", gm_srclt70oog8j9io, 196, 44);
                                uint64_t t58 = r56;
                                uint8_t t59 = (t54 > t58);
                                if (!t59) goto lb53;
                                uint64_t t60 = GM_LD(uintptr_t, (((uint8_t *)&F.l6) + 0) + 8);
                                uint64_t r61; bool o62 = __builtin_sub_overflow(t60, ((uint64_t)1ULL), &r61);
                                if (o62) gm_trap("integer overflow in `-`", gm_srclt70oog8j9io, 196, 92);
                                uint64_t t63 = r61;
                                uint64_t t64 = (uintptr_t)t63;
                                uint64_t t65 = GM_LD(uintptr_t, (((uint8_t *)&F.l6) + 0) + 8);
                                if (t64 >= (uintptr_t)(t65)) gm_trap_index(t64, t65, gm_srclt70oog8j9io, 196, 81);
                                gm_b16_8 t66 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l6) + 0)) + t64 * 16));
                                uint8_t t67 = gm_f_is_blank_or_comment_lubpopi(t66);
                                if (!t67) goto lb53;
                                {
                                    {
                                        void * t71 = (void *)((uint8_t *)&F.l6);
                                        gm_th2lqbo77pvsc t72;
                                        gm_f_pop_vr08sii((void *)&t72, t71);
                                    }
                                }
lc53:;
                            }
lb53:;
                        }
                        {
                            uint64_t t73 = gm_l8;
                            uint64_t t74 = gm_l9;
                            uint64_t r75; bool o76 = __builtin_add_overflow(t73, t74, &r75);
                            if (o76) gm_trap("integer overflow in `+`", gm_srclt70oog8j9io, 197, 21);
                            uint64_t t77 = r75;
                            gm_l8 = t77;
                        }
                        {
                            goto lc16;
                        }
                        goto ix48;
ie48:;
ix48:;
                    }
                    {
                        {
                            gm_l12 = ((uint64_t)0ULL);
                        }
                        {
                            uint64_t t79 = gm_l9;
                            gm_l11 = t79;
                        }
                        {
                            for (;;) {
                                uint64_t t81 = gm_l12;
                                uint64_t t82 = gm_l11;
                                uint8_t t83 = (t81 < t82);
                                if (!t83) goto lb80;
                                {
                                    uint64_t t86 = gm_l12;
                                    uint64_t t89 = GM_LD(uint64_t, ((uint8_t *)&t86));
                                    gm_l13 = t89;
                                    {
                                        void * t91 = (void *)((uint8_t *)&F.l6);
                                        uint64_t t92 = gm_l8;
                                        uint64_t t93 = gm_l13;
                                        uint64_t r94; bool o95 = __builtin_add_overflow(t92, t93, &r94);
                                        if (o95) gm_trap("integer overflow in `+`", gm_srclt70oog8j9io, 200, 55);
                                        uint64_t t96 = r94;
                                        uint64_t t97 = (uintptr_t)t96;
                                        uint64_t t98 = GM_LD(uintptr_t, (((uint8_t *)&F.l3) + 0) + 8);
                                        if (t97 >= (uintptr_t)(t98)) gm_trap_index(t97, t98, gm_srclt70oog8j9io, 200, 43);
                                        gm_b16_8 t99 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l3) + 0)) + t97 * 16));
                                        gm_tftb1odc739t5 t100 = gm_f_push_3ef746b(t91, t99);
                                        if (((((uint8_t *)&t100))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t100) + 1, gm_srclt70oog8j9io, 200, 33);
                                        gm_b0_1 t101 = GM_LD(gm_b0_1, (((uint8_t *)&t100) + 1));
                                    }
                                    goto me87;
mn88:;
                                    gm_trap("no match arm matched the scrutinee", gm_srclt70oog8j9io, 200, 17);
me87:;
                                }
lc80:;
                                {
                                    uint64_t t103 = gm_l12;
                                    uint64_t r104; bool o105 = __builtin_add_overflow(t103, ((uint64_t)1ULL), &r104);
                                    uint64_t t106 = r104;
                                    gm_l12 = t106;
                                }
                            }
lb80:;
                        }
                    }
                    {
                        uint64_t t107 = gm_l8;
                        uint64_t t108 = gm_l9;
                        uint64_t r109; bool o110 = __builtin_add_overflow(t107, t108, &r109);
                        if (o110) gm_trap("integer overflow in `+`", gm_srclt70oog8j9io, 201, 17);
                        uint64_t t111 = r109;
                        gm_l8 = t111;
                    }
                    {
                        goto lc16;
                    }
                    goto ix22;
ie22:;
ix22:;
                }
                {
                    void * t112 = (void *)((uint8_t *)&F.l6);
                    uint64_t t113 = gm_l8;
                    uint64_t t114 = (uintptr_t)t113;
                    uint64_t t115 = GM_LD(uintptr_t, (((uint8_t *)&F.l3) + 0) + 8);
                    if (t114 >= (uintptr_t)(t115)) gm_trap_index(t114, t115, gm_srclt70oog8j9io, 204, 23);
                    gm_b16_8 t116 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l3) + 0)) + t114 * 16));
                    gm_tftb1odc739t5 t117 = gm_f_push_3ef746b(t112, t116);
                    if (((((uint8_t *)&t117))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t117) + 1, gm_srclt70oog8j9io, 204, 13);
                    gm_b0_1 t118 = GM_LD(gm_b0_1, (((uint8_t *)&t117) + 1));
                }
                {
                    uint64_t t119 = gm_l8;
                    uint64_t r120; bool o121 = __builtin_add_overflow(t119, ((uint64_t)1ULL), &r120);
                    if (o121) gm_trap("integer overflow in `+`", gm_srclt70oog8j9io, 205, 13);
                    uint64_t t122 = r120;
                    gm_l8 = t122;
                }
lc16:;
            }
gm_ll20:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb16; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc16; }
            }
            goto gm_lad;
lb16:;
        }
        {
            uint8_t t124 = gm_l7;
            if (!t124) goto ie123;
            {
                gm_b16_8 t126 = GM_LD(gm_b16_8, (((uint8_t *)&F.l6) + 0));
                gm_b16_8 t127 = GM_LD(gm_b16_8, ((uint8_t *)&t126));
                gm_b16_8 t128 = gm_f_join_without_ej3mkbe((void *)NULL, cx, NULL, t127, ((uint64_t)0ULL), ((uint64_t)0ULL));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out) + 4, 0);
                ((uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out))[0] = 0;
                GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out) + 8, t128);
                goto gm_fdone;
            }
            goto ix123;
ie123:;
ix123:;
        }
        goto ix9;
ie9:;
ix9:;
    }
    {
        gm_b16_8 t129 = gm_l1;
        gm_tnpifv20irnph t130;
        GM_ST(gm_b16_8, ((uint8_t *)&t130 + 0), t129);
        gm_tabb710d9mq2e t131;
        gm_f_format_7plp021((void *)&t131, (void *)NULL, cx, NULL, t130);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t131))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t131) + 8, gm_srclt70oog8j9io, 210, 15);
        gm_b16_8 t132 = GM_LD(gm_b16_8, (((uint8_t *)&t131) + 8));
        gm_l14 = t132;
    }
    {
        gm_b16_8 t134 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
        gm_b16_8 t135 = GM_LD(gm_b16_8, ((uint8_t *)&t134));
        gm_b16_8 t136 = gm_l14;
        gm_b16_8 t137 = GM_LD(gm_b16_8, ((uint8_t *)&t136));
        gm_tc70a9dbbmgkg t138;
        gm_f_table_range_09bngqh((void *)&t138, (void *)NULL, cx, NULL, t135, t137);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (!((((uint8_t *)&t138))[16] != 0)) goto ie133;
        gm_t2lqu8jsj511d t139 = GM_LD(gm_t2lqu8jsj511d, ((uint8_t *)&t138));
        gm_l15 = t139;
        {
            uint64_t t141 = GM_LD(uint64_t, (((uint8_t *)&gm_l15) + 0));
            gm_l16 = t141;
        }
        {
            for (;;) {
                uint64_t t143 = gm_l16;
                uint8_t t144 = (t143 > ((uint64_t)0ULL));
                if (!t144) goto lb142;
                uint64_t t145 = gm_l16;
                uint64_t r146; bool o147 = __builtin_sub_overflow(t145, ((uint64_t)1ULL), &r146);
                if (o147) gm_trap("integer overflow in `-`", gm_srclt70oog8j9io, 213, 61);
                uint64_t t148 = r146;
                uint64_t t149 = (uintptr_t)t148;
                uint64_t t150 = GM_LD(uintptr_t, (((uint8_t *)&F.l3) + 0) + 8);
                if (t149 >= (uintptr_t)(t150)) gm_trap_index(t149, t150, gm_srclt70oog8j9io, 213, 49);
                gm_b16_8 t151 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l3) + 0)) + t149 * 16));
                uint8_t t152 = gm_f_is_blank_or_comment_lubpopi(t151);
                if (!t152) goto lb142;
                {
                    uint64_t t155 = gm_l16;
                    uint64_t r156; bool o157 = __builtin_sub_overflow(t155, ((uint64_t)1ULL), &r156);
                    if (o157) gm_trap("integer overflow in `-`", gm_srclt70oog8j9io, 213, 75);
                    uint64_t t158 = r156;
                    gm_l16 = t158;
                }
lc142:;
            }
lb142:;
        }
        {
            gm_b16_8 t159 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
            gm_b16_8 t160 = GM_LD(gm_b16_8, ((uint8_t *)&t159));
            uint64_t t161 = gm_l16;
            uint64_t t162 = GM_LD(uint64_t, (((uint8_t *)&gm_l15) + 8));
            gm_b16_8 t163 = gm_f_join_without_ej3mkbe((void *)NULL, cx, NULL, t160, t161, t162);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out) + 4, 0);
            ((uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out))[0] = 0;
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out) + 8, t163);
            goto gm_fdone;
        }
        goto ix133;
ie133:;
ix133:;
    }
    {
        gm_b16_8 t164 = gm_l4;
        gm_b16_8 t165 = GM_LD(gm_b16_8, ((uint8_t *)&t164));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out) + 4, 0);
        ((uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out))[0] = 1;
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out) + 8, t165);
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
inline __attribute__((aligned(4))) void gm_f_split_lines_jecth9m(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_split_lines_jecth9m F;
    gm_b16_8 gm_l0; /* text */
    uint64_t gm_l2; /* start */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    uint64_t gm_l5; /* i */
    uint8_t gm_l6; /* c */
    GM_STACK_CHECK(&F, 339u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_epvducg((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_l2 = ((uint64_t)0ULL);
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
                    uint64_t t11 = gm_l4;
                    uint64_t t12 = (uintptr_t)t11;
                    uint64_t t13 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                    uint8_t t14 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t12 * 1));
                    gm_tf5srh4mp4b3i t15;
                    GM_ST(uint8_t, (uint8_t *)&t15 + 9, 0);
                    GM_ST(uint16_t, (uint8_t *)&t15 + 10, 0);
                    GM_ST(uint32_t, (uint8_t *)&t15 + 12, 0);
                    GM_ST(uint64_t, ((uint8_t *)&t15 + 0), t10);
                    GM_ST(uint8_t, ((uint8_t *)&t15 + 8), t14);
                    uint64_t t18 = GM_LD(uint64_t, (((uint8_t *)&t15) + 0));
                    gm_l5 = t18;
                    uint8_t t19 = GM_LD(uint8_t, (((uint8_t *)&t15) + 8));
                    gm_l6 = t19;
                    {
                        uint8_t t22 = gm_l6;
                        uint8_t t23 = ((t22 == ((uint8_t)10ULL)));
                        if (!t23) goto ie21;
                        {
                            void * t25 = (void *)((uint8_t *)&F.l1);
                            uint64_t t26 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                            void * t27 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)));
                            uint64_t t28 = gm_l2;
                            uint64_t t29 = (uintptr_t)t28;
                            uint64_t t30 = gm_l5;
                            uint64_t r31; bool o32 = __builtin_add_overflow(t30, ((uint64_t)1ULL), &r31);
                            if (o32) gm_trap("integer overflow in `+`", gm_srclt70oog8j9io, 26, 36);
                            uint64_t t33 = r31;
                            uint64_t t34 = (uintptr_t)t33;
                            if (t29 > t34 || t34 > (uintptr_t)t26) gm_trap_slice(t29, t34, t26, gm_srclt70oog8j9io, 26, 24);
                            gm_b16_8 t35;
                            GM_ST(void *, (uint8_t *)&t35, (void *)(((uint8_t *)t27 + t29 * 1)));
                            GM_ST(uintptr_t, (uint8_t *)&t35 + 8, (uintptr_t)(t34 - t29));
                            gm_tftb1odc739t5 t36 = gm_f_push_3ef746b(t25, t35);
                            if (((((uint8_t *)&t36))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t36) + 1, gm_srclt70oog8j9io, 26, 13);
                            gm_b0_1 t37 = GM_LD(gm_b0_1, (((uint8_t *)&t36) + 1));
                        }
                        {
                            uint64_t t38 = gm_l5;
                            uint64_t r39; bool o40 = __builtin_add_overflow(t38, ((uint64_t)1ULL), &r39);
                            if (o40) gm_trap("integer overflow in `+`", gm_srclt70oog8j9io, 27, 21);
                            uint64_t t41 = r39;
                            gm_l2 = t41;
                        }
                        goto ix21;
ie21:;
ix21:;
                    }
                    goto me16;
mn17:;
                    gm_trap("no match arm matched the scrutinee", gm_srclt70oog8j9io, 24, 5);
me16:;
                }
lc4:;
                {
                    uint64_t t43 = gm_l4;
                    uint64_t r44; bool o45 = __builtin_add_overflow(t43, ((uint64_t)1ULL), &r44);
                    uint64_t t46 = r44;
                    gm_l4 = t46;
                }
            }
lb4:;
        }
    }
    {
        uint64_t t48 = gm_l2;
        uint64_t t49 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        uint8_t t50 = (t48 < t49);
        if (!t50) goto ie47;
        {
            void * t52 = (void *)((uint8_t *)&F.l1);
            uint64_t t53 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            void * t54 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)));
            uint64_t t55 = gm_l2;
            uint64_t t56 = (uintptr_t)t55;
            if (t56 > (uintptr_t)t53 || (uintptr_t)t53 > (uintptr_t)t53) gm_trap_slice(t56, (uintptr_t)t53, t53, gm_srclt70oog8j9io, 30, 38);
            gm_b16_8 t57;
            GM_ST(void *, (uint8_t *)&t57, (void *)(((uint8_t *)t54 + t56 * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t57 + 8, (uintptr_t)((uintptr_t)t53 - t56));
            gm_tftb1odc739t5 t58 = gm_f_push_3ef746b(t52, t57);
            if (((((uint8_t *)&t58))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t58) + 1, gm_srclt70oog8j9io, 30, 27);
            gm_b0_1 t59 = GM_LD(gm_b0_1, (((uint8_t *)&t58) + 1));
        }
        goto ix47;
ie47:;
ix47:;
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
inline __attribute__((aligned(4))) void gm_f_table_range_09bngqh(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1) {
    struct gm_fr_table_range_09bngqh F;
    gm_b16_8 gm_l0; /* lines */
    gm_b16_8 gm_l1; /* name */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    uint64_t gm_l4; /* i */
    gm_b16_8 gm_l5; /* l */
    gm_b16_8 gm_l6; /* h */
    uint64_t gm_l7; /* end */
    GM_STACK_CHECK_SHALLOW(&F, 486u);
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
                    gm_tf1bjg81u0glv t10;
                    uint64_t t11 = gm_l3;
                    GM_ST(uint64_t, (uint8_t *)&t10 + 0, t11);
                    uint64_t t12 = gm_l3;
                    uint64_t t13 = (uintptr_t)t12;
                    uint64_t t14 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                    gm_b16_8 t15 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t13 * 16));
                    GM_ST(gm_b16_8, (uint8_t *)&t10 + 8, t15);
                    uint64_t t18 = GM_LD(uint64_t, (((uint8_t *)&t10) + 0));
                    gm_l4 = t18;
                    gm_b16_8 t19 = GM_LD(gm_b16_8, (((uint8_t *)&t10) + 8));
                    gm_l5 = t19;
                    {
                        gm_b16_8 t22 = gm_l5;
                        gm_th2lqbo77pvsc t23;
                        gm_f_header_of_2u6atoc((void *)&t23, (void *)NULL, cx, NULL, t22);
                        if (gm_unw.kind) {
                            goto gm_ll8;
                        }
                        if (!((((uint8_t *)&t23))[16] != 0)) goto ie21;
                        gm_b16_8 t24 = GM_LD(gm_b16_8, ((uint8_t *)&t23));
                        gm_l6 = t24;
                        {
                            gm_b16_8 t27 = gm_l6;
                            gm_b16_8 t28 = gm_l1;
                            uint8_t t29 = (gm_eq0iqiubfm7e(((uint8_t *)&t27), ((uint8_t *)&t28)));
                            if (!t29) goto ie26;
                            {
                                uint64_t t31 = gm_l4;
                                uint64_t r32; bool o33 = __builtin_add_overflow(t31, ((uint64_t)1ULL), &r32);
                                if (o33) gm_trap("integer overflow in `+`", gm_srclt70oog8j9io, 106, 24);
                                uint64_t t34 = r32;
                                gm_l7 = t34;
                            }
                            {
                                for (;;) {
                                    uint64_t t36 = gm_l7;
                                    uint64_t t37 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                                    uint8_t t38 = (t36 < t37);
                                    if (!t38) goto lb35;
                                    uint64_t t39 = gm_l7;
                                    uint64_t t40 = (uintptr_t)t39;
                                    uint64_t t41 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                                    if (t40 >= (uintptr_t)(t41)) gm_trap_index(t40, t41, gm_srclt70oog8j9io, 107, 57);
                                    gm_b16_8 t42 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t40 * 16));
                                    uint8_t t43 = gm_f_is_header_uakq83c((void *)NULL, cx, NULL, t42);
                                    if (gm_unw.kind) {
                                        goto gm_ll8;
                                    }
                                    uint8_t t44 = (uint8_t)!t43;
                                    if (!t44) goto lb35;
                                    {
                                        uint64_t t47 = gm_l7;
                                        uint64_t r48; bool o49 = __builtin_add_overflow(t47, ((uint64_t)1ULL), &r48);
                                        if (o49) gm_trap("integer overflow in `+`", gm_srclt70oog8j9io, 107, 71);
                                        uint64_t t50 = r48;
                                        gm_l7 = t50;
                                    }
lc35:;
                                }
lb35:;
                            }
                            {
                                uint64_t t51 = gm_l4;
                                uint64_t t52 = gm_l7;
                                gm_t2lqu8jsj511d t53;
                                GM_ST(uint64_t, ((uint8_t *)&t53 + 0), t51);
                                GM_ST(uint64_t, ((uint8_t *)&t53 + 8), t52);
                                GM_ST(uint8_t, (uint8_t *)&(*(gm_tc70a9dbbmgkg *)gm_out) + 17, 0);
                                GM_ST(uint16_t, (uint8_t *)&(*(gm_tc70a9dbbmgkg *)gm_out) + 18, 0);
                                GM_ST(uint32_t, (uint8_t *)&(*(gm_tc70a9dbbmgkg *)gm_out) + 20, 0);
                                ((uint8_t *)&(*(gm_tc70a9dbbmgkg *)gm_out))[16] = 1;
                                GM_ST(gm_t2lqu8jsj511d, (uint8_t *)&(*(gm_tc70a9dbbmgkg *)gm_out) + 0, t53);
                                goto gm_fdone;
                            }
                            goto ix26;
ie26:;
ix26:;
                        }
                        goto ix21;
ie21:;
ix21:;
                    }
                    goto me16;
mn17:;
                    gm_trap("no match arm matched the scrutinee", gm_srclt70oog8j9io, 103, 5);
me16:;
                }
lc4:;
                {
                    uint64_t t55 = gm_l3;
                    uint64_t r56; bool o57 = __builtin_add_overflow(t55, ((uint64_t)1ULL), &r56);
                    uint64_t t58 = r56;
                    gm_l3 = t58;
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
        gm_tc70a9dbbmgkg t59;
        memset(&t59, 0, sizeof t59);
        uint8_t t60 = 0;
        F.ret = t59;
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
