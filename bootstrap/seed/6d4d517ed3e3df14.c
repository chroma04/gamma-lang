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
#define GM_STACK_CHECK(p, n) do { if ((uintptr_t)__builtin_frame_address(0) < gm_stack_limit + (uintptr_t)(n) + sizeof(*(p))) gm_stack_overflow(); } while (0)
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
typedef struct gm_csfq64m1m1rt6m { int64_t f0; } gm_csfq64m1m1rt6m;
typedef struct gm_csguankko4lmhd { uint32_t f0; } gm_csguankko4lmhd;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint64_t f0; } gm_t4ce0jd3tdro4;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { double f0; uint8_t f1; uint8_t pe[7]; } gm_th2bqc58ss467;
typedef struct { int64_t f0; } gm_tm98ap4112hee;
typedef struct { uint8_t f0; uint8_t p1[3]; union { int32_t v0; } f1; } gm_toe5fmr3vvf4d;
typedef struct { uint8_t a[4096]; } gm_tpbv7nqggnvqr;
typedef struct { uint8_t f1; } gm_ttsm01t0d8srg;
_Static_assert(sizeof(gm_cs6mnftt383dnt) == 16 && _Alignof(gm_cs6mnftt383dnt) == 8 && offsetof(gm_cs6mnftt383dnt, f0) == 0 && offsetof(gm_cs6mnftt383dnt, f1) == 8, "layout of `Allocator`");
_Static_assert(sizeof(gm_csfq64m1m1rt6m) == 8 && _Alignof(gm_csfq64m1m1rt6m) == 8 && offsetof(gm_csfq64m1m1rt6m, f0) == 0, "layout of `Dir`");
_Static_assert(sizeof(gm_csguankko4lmhd) == 4 && _Alignof(gm_csguankko4lmhd) == 4 && offsetof(gm_csguankko4lmhd, f0) == 0, "layout of `Mutex`");
typedef struct { uint64_t f0; uint8_t f1; uint8_t p2[7]; uint64_t f2; gm_b16_8 f3; gm_b16_8 f4; uint8_t f5; uint8_t f6; uint8_t pe[6]; } gm_t0rfe1ou14h21;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
_Static_assert(sizeof(gm_t4ce0jd3tdro4) == 8 && _Alignof(gm_t4ce0jd3tdro4) == 8 && offsetof(gm_t4ce0jd3tdro4, f0) == 0, "layout of `Instant`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_toe5fmr3vvf4d v0; gm_b16_8 v1; } f1; } gm_t5uv522f0p612;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_toe5fmr3vvf4d v0; gm_tm98ap4112hee v1; } f1; } gm_t62ebbi2i5rkd;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; } gm_tfeab279c16rr;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
_Static_assert(sizeof(gm_th2bqc58ss467) == 16 && _Alignof(gm_th2bqc58ss467) == 8 && offsetof(gm_th2bqc58ss467, f0) == 0 && offsetof(gm_th2bqc58ss467, f1) == 8, "layout of `?f64`");
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_toe5fmr3vvf4d v0; gm_t9qkh0rsooc8d v1; } f1; } gm_tj5jn79kq9pnm;
_Static_assert(sizeof(gm_tm98ap4112hee) == 8 && _Alignof(gm_tm98ap4112hee) == 8 && offsetof(gm_tm98ap4112hee, f0) == 0, "layout of `File`");
_Static_assert(sizeof(gm_toe5fmr3vvf4d) == 8 && _Alignof(gm_toe5fmr3vvf4d) == 4 && offsetof(gm_toe5fmr3vvf4d, f0) == 0 && offsetof(gm_toe5fmr3vvf4d, f1) == 4, "layout of `IoError`");
_Static_assert(sizeof(gm_tpbv7nqggnvqr) == 4096 && _Alignof(gm_tpbv7nqggnvqr) == 1 && offsetof(gm_tpbv7nqggnvqr, a) == 0, "layout of `[4096]u8`");
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_toe5fmr3vvf4d v0; } f1; } gm_ttln0d0hd70en;
_Static_assert(sizeof(gm_ttsm01t0d8srg) == 1 && _Alignof(gm_ttsm01t0d8srg) == 1 && offsetof(gm_ttsm01t0d8srg, f1) == 0, "layout of `?()`");
typedef struct { uint8_t f0; uint8_t p1[7]; uint64_t f1; uint64_t f2; gm_tf5srh4mp4b3i f3; uint8_t f4; uint8_t f5; uint8_t p6[6]; uint64_t f6; } gm_tutaanatuv9ov;
_Static_assert(sizeof(gm_t0rfe1ou14h21) == 64 && _Alignof(gm_t0rfe1ou14h21) == 8 && offsetof(gm_t0rfe1ou14h21, f0) == 0 && offsetof(gm_t0rfe1ou14h21, f1) == 8 && offsetof(gm_t0rfe1ou14h21, f2) == 16 && offsetof(gm_t0rfe1ou14h21, f3) == 24 && offsetof(gm_t0rfe1ou14h21, f4) == 40 && offsetof(gm_t0rfe1ou14h21, f5) == 56 && offsetof(gm_t0rfe1ou14h21, f6) == 57, "layout of `Target`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; gm_trqfvmjtt4sij v1; } f1; } gm_t470aqkia5212;
_Static_assert(sizeof(gm_t5uv522f0p612) == 24 && _Alignof(gm_t5uv522f0p612) == 8 && offsetof(gm_t5uv522f0p612, f0) == 0 && offsetof(gm_t5uv522f0p612, f1) == 8, "layout of `IoError!string`");
_Static_assert(sizeof(gm_t62ebbi2i5rkd) == 16 && _Alignof(gm_t62ebbi2i5rkd) == 8 && offsetof(gm_t62ebbi2i5rkd, f0) == 0 && offsetof(gm_t62ebbi2i5rkd, f1) == 8, "layout of `IoError!File`");
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
typedef struct { gm_b16_8 f0; gm_th2lqbo77pvsc f1; uint32_t f2; uint8_t pe[4]; } gm_tbah3mk9eqfec;
typedef struct { gm_tfeab279c16rr f0; uint8_t f1; uint8_t pe[7]; } gm_td6svc7gs468a;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_th2bqc58ss467 f2; gm_th2lqbo77pvsc f3; uint64_t f4; uint64_t f5; gm_trqfvmjtt4sij f6; gm_t88rcucg1b7uk f7; gm_t88rcucg1b7uk f8; } gm_tdg3pnmn22f83;
_Static_assert(sizeof(gm_tfeab279c16rr) == 48 && _Alignof(gm_tfeab279c16rr) == 8 && offsetof(gm_tfeab279c16rr, f0) == 0 && offsetof(gm_tfeab279c16rr, f1) == 16 && offsetof(gm_tfeab279c16rr, f2) == 32, "layout of `Toolchain`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tj5jn79kq9pnm v0; gm_b16_8 v1; } f1; } gm_tgvh15qshf643;
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
typedef struct { gm_trqfvmjtt4sij f0; } gm_tin97tt4ud0s1;
_Static_assert(sizeof(gm_tj5jn79kq9pnm) == 12 && _Alignof(gm_tj5jn79kq9pnm) == 4 && offsetof(gm_tj5jn79kq9pnm, f0) == 0 && offsetof(gm_tj5jn79kq9pnm, f1) == 4, "layout of `ReadError`");
typedef struct { uint8_t f0; uint8_t f1; uint8_t f2; uint8_t p3[5]; gm_trqfvmjtt4sij f3; } gm_tm4vhr17lpr3p;
typedef struct { gm_th2lqbo77pvsc f0; gm_b16_8 f1; uint64_t f2; } gm_tp3p9lj444cp7;
typedef struct { gm_trqfvmjtt4sij f0; gm_trqfvmjtt4sij f1; gm_trqfvmjtt4sij f2; } gm_tr8dvlu9ik4gj;
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_ttln0d0hd70en) == 12 && _Alignof(gm_ttln0d0hd70en) == 4 && offsetof(gm_ttln0d0hd70en, f0) == 0 && offsetof(gm_ttln0d0hd70en, f1) == 4, "layout of `IoError!()`");
_Static_assert(sizeof(gm_tutaanatuv9ov) == 56 && _Alignof(gm_tutaanatuv9ov) == 8 && offsetof(gm_tutaanatuv9ov, f0) == 0 && offsetof(gm_tutaanatuv9ov, f1) == 8 && offsetof(gm_tutaanatuv9ov, f2) == 16 && offsetof(gm_tutaanatuv9ov, f3) == 24 && offsetof(gm_tutaanatuv9ov, f4) == 40 && offsetof(gm_tutaanatuv9ov, f5) == 41 && offsetof(gm_tutaanatuv9ov, f6) == 48, "layout of `Report`");
typedef struct { gm_t6j6smj7qoctu f0; gm_tin97tt4ud0s1 f1; gm_trqfvmjtt4sij f2; } gm_t31ebhnaiodh3;
typedef struct { gm_tp3p9lj444cp7 f0; uint8_t f1; uint8_t pe[7]; } gm_t33ki4t2edp58;
_Static_assert(sizeof(gm_t470aqkia5212) == 48 && _Alignof(gm_t470aqkia5212) == 8 && offsetof(gm_t470aqkia5212, f0) == 0 && offsetof(gm_t470aqkia5212, f1) == 8, "layout of `string!List(string)`");
_Static_assert(sizeof(gm_tbah3mk9eqfec) == 48 && _Alignof(gm_tbah3mk9eqfec) == 8 && offsetof(gm_tbah3mk9eqfec, f0) == 0 && offsetof(gm_tbah3mk9eqfec, f1) == 16 && offsetof(gm_tbah3mk9eqfec, f2) == 40, "layout of `TestCase`");
typedef struct { gm_tr8dvlu9ik4gj f0; uint8_t f1; uint8_t pe[7]; } gm_tctputthhrk9k;
_Static_assert(sizeof(gm_td6svc7gs468a) == 56 && _Alignof(gm_td6svc7gs468a) == 8 && offsetof(gm_td6svc7gs468a, f0) == 0 && offsetof(gm_td6svc7gs468a, f1) == 48, "layout of `?Toolchain`");
_Static_assert(sizeof(gm_tdg3pnmn22f83) == 136 && _Alignof(gm_tdg3pnmn22f83) == 8 && offsetof(gm_tdg3pnmn22f83, f0) == 0 && offsetof(gm_tdg3pnmn22f83, f1) == 16 && offsetof(gm_tdg3pnmn22f83, f2) == 32 && offsetof(gm_tdg3pnmn22f83, f3) == 48 && offsetof(gm_tdg3pnmn22f83, f4) == 72 && offsetof(gm_tdg3pnmn22f83, f5) == 80 && offsetof(gm_tdg3pnmn22f83, f6) == 88 && offsetof(gm_tdg3pnmn22f83, f7) == 128 && offsetof(gm_tdg3pnmn22f83, f8) == 132, "layout of `Pool`");
_Static_assert(sizeof(gm_tgvh15qshf643) == 24 && _Alignof(gm_tgvh15qshf643) == 8 && offsetof(gm_tgvh15qshf643, f0) == 0 && offsetof(gm_tgvh15qshf643, f1) == 8, "layout of `ReadError![]u8`");
_Static_assert(sizeof(gm_tin97tt4ud0s1) == 40 && _Alignof(gm_tin97tt4ud0s1) == 8 && offsetof(gm_tin97tt4ud0s1, f0) == 0, "layout of `SourceMap`");
_Static_assert(sizeof(gm_tm4vhr17lpr3p) == 48 && _Alignof(gm_tm4vhr17lpr3p) == 8 && offsetof(gm_tm4vhr17lpr3p, f0) == 0 && offsetof(gm_tm4vhr17lpr3p, f1) == 1 && offsetof(gm_tm4vhr17lpr3p, f2) == 2 && offsetof(gm_tm4vhr17lpr3p, f3) == 8, "layout of `Options`");
_Static_assert(sizeof(gm_tp3p9lj444cp7) == 48 && _Alignof(gm_tp3p9lj444cp7) == 8 && offsetof(gm_tp3p9lj444cp7, f0) == 0 && offsetof(gm_tp3p9lj444cp7, f1) == 24 && offsetof(gm_tp3p9lj444cp7, f2) == 40, "layout of `Finished`");
_Static_assert(sizeof(gm_tr8dvlu9ik4gj) == 120 && _Alignof(gm_tr8dvlu9ik4gj) == 8 && offsetof(gm_tr8dvlu9ik4gj, f0) == 0 && offsetof(gm_tr8dvlu9ik4gj, f1) == 40 && offsetof(gm_tr8dvlu9ik4gj, f2) == 80, "layout of `Extras`");
typedef struct { uint64_t f0; gm_tbah3mk9eqfec f1; } gm_tu5ilovikalsj;
_Static_assert(sizeof(gm_t31ebhnaiodh3) == 112 && _Alignof(gm_t31ebhnaiodh3) == 8 && offsetof(gm_t31ebhnaiodh3, f0) == 0 && offsetof(gm_t31ebhnaiodh3, f1) == 32 && offsetof(gm_t31ebhnaiodh3, f2) == 72, "layout of `Cover`");
_Static_assert(sizeof(gm_t33ki4t2edp58) == 56 && _Alignof(gm_t33ki4t2edp58) == 8 && offsetof(gm_t33ki4t2edp58, f0) == 0 && offsetof(gm_t33ki4t2edp58, f1) == 48, "layout of `?Finished`");
_Static_assert(sizeof(gm_tctputthhrk9k) == 128 && _Alignof(gm_tctputthhrk9k) == 8 && offsetof(gm_tctputthhrk9k, f0) == 0 && offsetof(gm_tctputthhrk9k, f1) == 120, "layout of `?Extras`");
typedef struct { gm_t9qkh0rsooc8d f0; gm_t9qkh0rsooc8d f1; uint8_t f2; uint8_t f3; gm_t9qkh0rsooc8d f4; uint8_t p5[3]; gm_th2lqbo77pvsc f5; uint8_t f6; uint8_t f7; uint8_t f8; uint8_t p9[5]; gm_b16_8 f9; uint8_t f10; uint8_t f11; uint8_t p12[6]; gm_th2lqbo77pvsc f12; uint8_t f13; uint8_t f14; uint8_t p15[6]; uint64_t f15; gm_th2bqc58ss467 f16; uint8_t f17; uint8_t f18; uint8_t p19[6]; gm_th2lqbo77pvsc f19; gm_th2lqbo77pvsc f20; uint8_t f21; uint8_t f22; uint8_t p23[6]; gm_th2lqbo77pvsc f23; uint8_t f24; uint8_t p25[7]; gm_b16_8 f25; gm_b16_8 f26; gm_tctputthhrk9k f27; gm_th2lqbo77pvsc f28; gm_t0rfe1ou14h21 f29; gm_td6svc7gs468a f30; gm_th2lqbo77pvsc f31; gm_tm4vhr17lpr3p f32; } gm_tdkfkbgm73fql;
_Static_assert(sizeof(gm_tu5ilovikalsj) == 56 && _Alignof(gm_tu5ilovikalsj) == 8 && offsetof(gm_tu5ilovikalsj, f0) == 0 && offsetof(gm_tu5ilovikalsj, f1) == 8, "layout of `(i: usize, x: TestCase)`");
_Static_assert(sizeof(gm_tdkfkbgm73fql) == 592 && _Alignof(gm_tdkfkbgm73fql) == 8 && offsetof(gm_tdkfkbgm73fql, f0) == 0 && offsetof(gm_tdkfkbgm73fql, f1) == 1 && offsetof(gm_tdkfkbgm73fql, f2) == 2 && offsetof(gm_tdkfkbgm73fql, f3) == 3 && offsetof(gm_tdkfkbgm73fql, f4) == 4 && offsetof(gm_tdkfkbgm73fql, f5) == 8 && offsetof(gm_tdkfkbgm73fql, f6) == 32 && offsetof(gm_tdkfkbgm73fql, f7) == 33 && offsetof(gm_tdkfkbgm73fql, f8) == 34 && offsetof(gm_tdkfkbgm73fql, f9) == 40 && offsetof(gm_tdkfkbgm73fql, f10) == 56 && offsetof(gm_tdkfkbgm73fql, f11) == 57 && offsetof(gm_tdkfkbgm73fql, f12) == 64 && offsetof(gm_tdkfkbgm73fql, f13) == 88 && offsetof(gm_tdkfkbgm73fql, f14) == 89 && offsetof(gm_tdkfkbgm73fql, f15) == 96 && offsetof(gm_tdkfkbgm73fql, f16) == 104 && offsetof(gm_tdkfkbgm73fql, f17) == 120 && offsetof(gm_tdkfkbgm73fql, f18) == 121 && offsetof(gm_tdkfkbgm73fql, f19) == 128 && offsetof(gm_tdkfkbgm73fql, f20) == 152 && offsetof(gm_tdkfkbgm73fql, f21) == 176 && offsetof(gm_tdkfkbgm73fql, f22) == 177 && offsetof(gm_tdkfkbgm73fql, f23) == 184 && offsetof(gm_tdkfkbgm73fql, f24) == 208 && offsetof(gm_tdkfkbgm73fql, f25) == 216 && offsetof(gm_tdkfkbgm73fql, f26) == 232 && offsetof(gm_tdkfkbgm73fql, f27) == 248 && offsetof(gm_tdkfkbgm73fql, f28) == 376 && offsetof(gm_tdkfkbgm73fql, f29) == 400 && offsetof(gm_tdkfkbgm73fql, f30) == 464 && offsetof(gm_tdkfkbgm73fql, f31) == 520 && offsetof(gm_tdkfkbgm73fql, f32) == 544, "layout of `Options`");
struct gm_fr_add_counts_hprtdd9 {
    gm_frame h;
    void * l0; /* counts */
    uint64_t l2; /* i */
    gm_b0_1 ret;
    gm_b0_1 w0;
};
struct gm_fr_broadcast_bfak2oh {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_condvar_rp5e3qr {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_contains_gkki4kq {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_cover_file_bhhubi9 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_73v4ttb {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_d914i5u {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_iv39hj3 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_j0iglrj {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_n0agqu3 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_heap_allocator_gihki37 {
    gm_frame h;
    gm_t3mf58gv9kf3n ret;
};
struct gm_fr_join_r8j2dp1 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_list_1qnmubu {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_5dhgg4i {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_7vplt15 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_86kk1bu {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_go2qg0b {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_mutex_t7if00c {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_run_6mplkoc {
    gm_frame h;
    gm_tdkfkbgm73fql l2; /* opts */
    gm_trqfvmjtt4sij l5; /* chosen */
    gm_tbah3mk9eqfec l9; /* c */
    gm_tutaanatuv9ov l11; /* report */
    gm_t31ebhnaiodh3 l12; /* cover */
    uint8_t ret;
};
struct gm_fr_run_all_fj42v4f {
    gm_frame h;
    gm_trqfvmjtt4sij l9; /* threads */
    gm_tdg3pnmn22f83 l10; /* pool */
    gm_tp3p9lj444cp7 l22; /* f */
    gm_b0_1 ret;
};
struct gm_fr_run_native_ije4jng {
    gm_frame h;
    gm_tdkfkbgm73fql l1; /* opts */
    gm_trqfvmjtt4sij l7; /* entries */
    gm_tpbv7nqggnvqr l12; /* buf */
    gm_trqfvmjtt4sij l15; /* roots */
    gm_trqfvmjtt4sij l21; /* runner */
    gm_ttsm01t0d8srg ret;
};
struct gm_fr_selected_8kh2cc4 {
    gm_frame h;
    gm_tbah3mk9eqfec l0; /* t */
    uint8_t ret;
};
struct gm_fr_system_io_mcjcl9m {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_system_output_eg72lom {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_unlock_9f1vcl6 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_wait_k1r8nlm {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_worker_s2irjfh {
    gm_frame h;
    gm_tp3p9lj444cp7 l2; /* f */
    gm_b0_1 ret;
    gm_b0_1 w0;
};
inline bool gm_eq467d5cm1d5(const uint8_t *a, const uint8_t *b);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_add_counts_hprtdd9(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) void gm_f_broadcast_bfak2oh(void * a0);
__attribute__((aligned(4))) uint8_t gm_f_compile_entries_l9qlniu(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, uint8_t a2, gm_b16_8 a3, const void * a4);
inline __attribute__((aligned(4))) gm_csguankko4lmhd gm_f_condvar_rp5e3qr(void);
inline __attribute__((aligned(4))) uint8_t gm_f_contains_gkki4kq(gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_cover_file_bhhubi9(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, uint64_t a1);
__attribute__((aligned(4))) uint8_t gm_f_coverage_report_hrhbf22(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2, gm_b16_8 a3, const void * a4);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_73v4ttb(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_d914i5u(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_iv39hj3(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_j0iglrj(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_n0agqu3(void);
__attribute__((aligned(4))) uint32_t gm_f_entry_point_n2gu79u(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_extend_7hs67uj(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_find_nab29co(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) uint8_t gm_f_finish_2t60fs7(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_format_map1fa6(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
inline __attribute__((aligned(4))) gm_cs6mnftt383dnt gm_f_heap_allocator_gihki37(void);
__attribute__((aligned(4))) void gm_f_instrument_mk8clvl(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_join_qveh5b2(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) void gm_f_join_r8j2dp1(gm_csfq64m1m1rt6m a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_lines_9uns0k2_sqdm19s(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) void gm_f_list_1qnmubu(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_5dhgg4i(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_7vplt15(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_86kk1bu(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_go2qg0b(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_lock_u5lks01(void * a0);
__attribute__((aligned(4))) void gm_f_make_temp_dir_vi8f88d(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_csguankko4lmhd gm_f_mutex_t7if00c(void);
__attribute__((aligned(4))) gm_b16_8 gm_f_native_exe_name_rqchv3n(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) uint8_t gm_f_prepare_codegen_4hnlio1(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_6ct7n6b(void * a0, gm_csfq64m1m1rt6m a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_im9d4ro(void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_snfdb36(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_ucgmlo2(void * a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_read_all_r9rh9qd(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_record_abrqivi(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, const void * a2);
__attribute__((aligned(4))) gm_ttln0d0hd70en gm_f_remove_all_h0k1qkh(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) uint8_t gm_f_run_6mplkoc(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, const void * a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_run_all_fj42v4f(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1, gm_b16_8 a2, gm_b16_8 a3, uint64_t a4, gm_th2bqc58ss467 a5, const void * a6, void * a7);
__attribute__((aligned(4))) void gm_f_run_exe_4182ncf(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1, uint64_t a2, gm_th2bqc58ss467 a3, const void * a4);
__attribute__((aligned(4))) gm_ttsm01t0d8srg gm_f_run_native_ije4jng(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_b16_8 a2, gm_b16_8 a3, uint8_t a4, void * a5, void * a6);
__attribute__((aligned(4))) void gm_f_runner_for_79o4h4m(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_scope_4n0q0tq(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) uint8_t gm_f_selected_8kh2cc4(const void * a0, const void * a1);
__attribute__((aligned(4))) uint8_t gm_f_selected_8kh2cc4_c(gm_tbah3mk9eqfec a0, gm_th2lqbo77pvsc a1);
__attribute__((aligned(4))) gm_t62ebbi2i5rkd gm_f_spawn_534m93a(void * a0, void * a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_system_io_mcjcl9m(void);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_system_output_eg72lom(void);
__attribute__((aligned(4))) gm_b16_8 gm_f_temp_dir_i6apmsd(void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_unlock_9f1vcl6(void * a0);
inline __attribute__((aligned(4))) void gm_f_wait_k1r8nlm(void * a0, void * a1);
__attribute__((aligned(4))) void gm_f_worker_s2irjfh(void * a0);
GM_NOINLINE bool gm_pf454qfrpt6r(gm_node *cx, const void *d, uintptr_t n, const char *file, uint32_t line, uint32_t col);
GM_NOINLINE bool gm_pfu8dq5bikbh(gm_node *cx, const gm_type *d, const void *v, const char *file, uint32_t line, uint32_t col);
extern uint8_t gm_cgd1khc5sal[24] __attribute__((aligned(8)));
extern void *const gm_hd3g3ujqvah4[];
extern void *const gm_hd3t1u951rul[];
extern void *const gm_hdaqemfrshlp[];
extern void *const gm_hddfeetqn95b[];
extern void *const gm_hdf4dspmusp5[];
extern void *const gm_hdffkeesmui4[];
extern void *const gm_hdu50j4i95ke[];
extern void *const gm_hdu8v6n5i2qd[];
extern void *const gm_hdvslis4o40u[];
extern void *const gm_hl9hta9cqrqu[];
extern const uint8_t gm_s06m3met1mb9v[3];
extern const uint8_t gm_s5lqhiu2bn6of[8];
extern const uint8_t gm_s63emi69ethib[50];
extern const uint8_t gm_s79el5a3kii78[12];
extern const uint8_t gm_shg1pn1r3sjgi[13];
extern const uint8_t gm_sjbhqu0o8ebub[6];
extern const uint8_t gm_so514oj5nbhcu[2];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_src3d0v42kavhv7[];
extern const char gm_srcrgc9bkquf6nr[];
extern const char gm_srcrq1gl2e1lsg5[];
extern const uint8_t gm_svaibtfuu0gf2[48];
extern const gm_type gm_ty0iqiubfm7e;
extern const gm_type gm_tyo86ks9qkvt;
inline bool gm_eq467d5cm1d5(const uint8_t *a, const uint8_t *b) {
    return GM_LD(uint32_t, a) == GM_LD(uint32_t, b);
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_add_counts_hprtdd9(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1) {
    struct gm_fr_add_counts_hprtdd9 F;
    gm_b16_8 gm_l1; /* text */
    gm_b16_8 gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    gm_l1 = a1;
    {
        F.l2 = ((uint64_t)0ULL);
    }
    {
        {
            gm_b16_8 t3 = gm_l1;
            gm_l3 = t3;
        }
        gm_hval t4 = ((gm_hval){(void *)&F.h, cx});
        gm_node wn5 = {.eff = 993381376, .kind = GM_LIT, .clauses = gm_hl9hta9cqrqu, .env = t4.env, .hctx = t4.cx, .with_frame = &F.h, .with_label = 0, .result = (void *)&F.w0, .fw = NULL, .next = cx};
        gm_b16_8 t8 = gm_l3;
        gm_b0_1 t9 = gm_f_lines_9uns0k2_sqdm19s((void *)NULL, (&wn5), NULL, t8);
        if (gm_unw.kind) {
            goto gm_ll7;
        }
        F.w0 = t9;
        goto gm_ls10;
gm_ll7:;
        if (gm_unw.kind == GM_BREAK && gm_unw.frame == &F.h && gm_unw.label == 0) { gm_unw.kind = GM_NONE; goto we6; }
        goto gm_lad;
gm_ls10:;
we6:;
        gm_b0_1 t11 = F.w0;
        gm_b0_1 t12 = t11;
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
inline __attribute__((aligned(4))) void gm_f_broadcast_bfak2oh(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_broadcast_bfak2oh F;
    void * gm_l0; /* c */
    GM_STACK_CHECK(&F, 54u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        {
            void * t3 = gm_l0;
            void * t4 = (void *)(((uint8_t *)t3) + 0);
            gm_t9qkh0rsooc8d t5;
            GM_ST(uint8_t, ((uint8_t *)&t5 + 0), ((uint8_t)2ULL));
            gm_t9qkh0rsooc8d t6 = t5;
            uint32_t t7 = __atomic_fetch_add(((uint32_t *)t4), ((uint32_t)1ULL), gm_ord[GM_LD(uint8_t, ((uint8_t *)&t6))]);
        }
    }
    {
        {
            void * t9 = gm_l0;
            void * t10 = (void *)(((uint8_t *)t9) + 0);
            int64_t t11 = gm_futex_wake(t10, ((uint32_t)4294967295ULL));
        }
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) gm_csguankko4lmhd gm_f_condvar_rp5e3qr(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_condvar_rp5e3qr F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_cover_file_bhhubi9(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, uint64_t a1) {
    struct gm_fr_cover_file_bhhubi9 F;
    gm_b16_8 gm_l0; /* dir */
    uint64_t gm_l1; /* j */
    GM_STACK_CHECK(&F, 200u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_b16_8 t1 = gm_l0;
    uint64_t t2 = gm_l1;
    gm_t4ce0jd3tdro4 t3;
    GM_ST(uint64_t, ((uint8_t *)&t3 + 0), t2);
    gm_tabb710d9mq2e t4;
    gm_f_format_map1fa6((void *)&t4, (void *)NULL, cx, NULL, t3);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tabb710d9mq2e t5 = t4;
    if (((((uint8_t *)&t5))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t5) + 8, gm_srcrq1gl2e1lsg5, 114, 81);
    gm_b16_8 t6 = GM_LD(gm_b16_8, (((uint8_t *)&t5) + 8));
    gm_b16_8 t7 = t6;
    gm_b16_8 t8 = GM_LD(gm_b16_8, ((uint8_t *)&t7));
    gm_b16_8 t9 = gm_f_join_qveh5b2((void *)NULL, cx, NULL, t1, t8);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.ret = t9;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_73v4ttb(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_73v4ttb F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_j0iglrj(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_j0iglrj F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_n0agqu3(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_n0agqu3 F;
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
inline __attribute__((aligned(4))) void gm_f_list_1qnmubu(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_1qnmubu F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_n0agqu3();
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
inline __attribute__((aligned(4))) void gm_f_list_5dhgg4i(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_5dhgg4i F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_73v4ttb();
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
inline __attribute__((aligned(4))) void gm_f_list_86kk1bu(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_86kk1bu F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_j0iglrj();
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_system_output_eg72lom(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_system_output_eg72lom F;
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
inline __attribute__((aligned(4))) void gm_f_wait_k1r8nlm(void * a0, void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_wait_k1r8nlm F;
    void * gm_l0; /* c */
    void * gm_l1; /* m */
    uint32_t gm_l2; /* seen */
    GM_STACK_CHECK(&F, 86u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        void * t3 = (void *)(((uint8_t *)t2) + 0);
        gm_t9qkh0rsooc8d t4;
        GM_ST(uint8_t, ((uint8_t *)&t4 + 0), ((uint8_t)1ULL));
        gm_t9qkh0rsooc8d t5 = t4;
        uint32_t t6 = __atomic_load_n(((uint32_t *)t3), gm_ord[GM_LD(uint8_t, ((uint8_t *)&t5))]);
        gm_l2 = t6;
    }
    {
        void * t7 = gm_l1;
        gm_f_unlock_9f1vcl6(t7);
    }
    {
        {
            void * t9 = gm_l0;
            void * t10 = (void *)(((uint8_t *)t9) + 0);
            uint32_t t11 = gm_l2;
            int64_t t12 = gm_futex_wait(t10, t11, ((int64_t)-1LL));
        }
    }
    {
        void * t13 = gm_l1;
        gm_f_lock_u5lks01(t13);
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
extern __attribute__((aligned(4))) gm_b16_8 gm_f_cover_file_bhhubi9(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, uint64_t a1);
__attribute__((aligned(4))) uint8_t gm_f_run_6mplkoc(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, const void * a2) {
    struct gm_fr_run_6mplkoc F;
    void * gm_l0; /* s */
    gm_b16_8 gm_l1; /* input */
    uint8_t gm_l3; /* bench */
    gm_b16_8 gm_l4; /* cases */
    gm_b16_8 gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    uint64_t gm_l8; /* i */
    gm_tf5srh4mp4b3i gm_l10; /* filtered_out */
    uint8_t gm_l13; /* status */
    GM_STACK_CHECK(&F, 949u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    memcpy(&F.l2, a2, sizeof F.l2);
    {
        uint8_t t2 = ((GM_LD(uint8_t, (((uint8_t *)&F.l2) + 0) + 0) == ((uint8_t)11ULL)));
        gm_l3 = t2;
    }
    {
        uint8_t t4 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 121));
        if (!t4) goto ie3;
        uint8_t t5 = gm_l3;
        if (!t5) goto ie3;
        {
            if (gm_pf454qfrpt6r(cx, gm_s63emi69ethib, 49, gm_srcrq1gl2e1lsg5, 36, 9)) {
                goto gm_lad;
            }
        }
        {
            uint8_t t7 = 0;
            F.ret = ((uint8_t)2ULL);
            goto gm_fret;
        }
        goto ix3;
ie3:;
ix3:;
    }
    {
        gm_b16_8 t8;
        uint8_t t10 = gm_l3;
        if (!t10) goto ie9;
        void * t12 = gm_l0;
        gm_b16_8 t13 = GM_LD(gm_b16_8, ((((uint8_t *)t12) + 1008) + 0));
        gm_b16_8 t14 = t13;
        t8 = t14;
        goto ix9;
ie9:;
        void * t16 = gm_l0;
        gm_b16_8 t17 = GM_LD(gm_b16_8, ((((uint8_t *)t16) + 968) + 0));
        gm_b16_8 t18 = t17;
        t8 = t18;
ix9:;
        gm_l4 = t8;
    }
    {
        gm_f_list_5dhgg4i((void *)&F.l5, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t20 = gm_l4;
            gm_b16_8 t21 = t20;
            gm_b16_8 t22 = GM_LD(gm_b16_8, ((uint8_t *)&t21));
            gm_l6 = t22;
        }
        {
            gm_l7 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t24 = gm_l7;
                uint64_t t25 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                uint8_t t26 = (t24 < t25);
                if (!t26) goto lb23;
                {
                    gm_tu5ilovikalsj t29;
                    uint64_t t30 = gm_l7;
                    GM_ST(uint64_t, (uint8_t *)&t29 + 0, t30);
                    uint64_t t31 = gm_l7;
                    uint64_t t32 = (uintptr_t)t31;
                    uint64_t t33 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                    memcpy((uint8_t *)&t29 + 8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t32 * 48), 48);
                    gm_tu5ilovikalsj t34 = t29;
                    uint64_t t37 = GM_LD(uint64_t, (((uint8_t *)&t34) + 0));
                    gm_l8 = t37;
                    gm_tbah3mk9eqfec t38 = GM_LD(gm_tbah3mk9eqfec, (((uint8_t *)&t34) + 8));
                    F.l9 = t38;
                    {
                        uint8_t t41 = gm_f_selected_8kh2cc4(&(*(gm_tbah3mk9eqfec *)((uint8_t *)&F.l9)), &(*(gm_th2lqbo77pvsc *)(((uint8_t *)&F.l2) + 64)));
                        if (!t41) goto ie40;
                        {
                            void * t43 = (void *)((uint8_t *)&F.l5);
                            uint64_t t44 = gm_l8;
                            gm_tftb1odc739t5 t45 = gm_f_push_ucgmlo2(t43, t44);
                            gm_tftb1odc739t5 t46 = t45;
                            if (((((uint8_t *)&t46))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t46) + 1, gm_srcrq1gl2e1lsg5, 42, 39);
                            gm_b0_1 t47 = GM_LD(gm_b0_1, (((uint8_t *)&t46) + 1));
                        }
                        goto ix40;
ie40:;
ix40:;
                    }
                    goto me35;
mn36:;
                    gm_trap("no match arm matched the scrutinee", gm_srcrq1gl2e1lsg5, 41, 5);
me35:;
                }
lc23:;
                {
                    uint64_t t49 = gm_l7;
                    uint64_t r50; bool o51 = __builtin_add_overflow(t49, ((uint64_t)1ULL), &r50);
                    uint64_t t52 = r50;
                    gm_l7 = t52;
                }
            }
lb23:;
        }
    }
    {
        gm_tf5srh4mp4b3i t53;
        memset(&t53, 0, sizeof t53);
        gm_l10 = t53;
    }
    {
        uint8_t t55;
        if ((((((uint8_t *)&F.l2) + 64))[16] != 0)) goto mn57;
        t55 = ((uint8_t)0);
        goto me56;
mn57:;
        t55 = ((uint8_t)1);
        goto me56;
mn58:;
        gm_trap("no match arm matched the scrutinee", gm_srcrq1gl2e1lsg5, 45, 8);
me56:;
        if (!t55) goto ie54;
        {
            uint64_t t60 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
            uint64_t t61 = GM_LD(uintptr_t, (((uint8_t *)&F.l5) + 0) + 8);
            uint64_t r62; bool o63 = __builtin_sub_overflow(t60, t61, &r62);
            if (o63) gm_trap("integer overflow in `-`", gm_srcrq1gl2e1lsg5, 45, 49);
            uint64_t t64 = r62;
            gm_tf5srh4mp4b3i t65;
            GM_ST(uint8_t, (uint8_t *)&t65 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t65 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t65 + 12, 0);
            GM_ST(uint64_t, ((uint8_t *)&t65), t64);
            ((uint8_t *)&t65)[8] = 1;
            gm_l10 = t65;
        }
        goto ix54;
ie54:;
ix54:;
    }
    {
        memset((uint8_t *)&F.l11 + 1, 0, 7);
        memset((uint8_t *)&F.l11 + 42, 0, 6);
        uint8_t t66 = gm_l3;
        GM_ST(uint8_t, (uint8_t *)&F.l11 + 0, t66);
        GM_ST(uint64_t, (uint8_t *)&F.l11 + 8, ((uint64_t)0ULL));
        GM_ST(uint64_t, (uint8_t *)&F.l11 + 16, ((uint64_t)0ULL));
        gm_tf5srh4mp4b3i t67 = gm_l10;
        GM_ST(gm_tf5srh4mp4b3i, (uint8_t *)&F.l11 + 24, t67);
        uint8_t t68 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 89));
        GM_ST(uint8_t, (uint8_t *)&F.l11 + 40, t68);
        uint8_t t69 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 120));
        GM_ST(uint8_t, (uint8_t *)&F.l11 + 41, t69);
        gm_node *pn70 = gm_find(cx, 1579066750);
        if (!pn70) gm_trap("no handler installed for effect `Clock`", gm_srcrq1gl2e1lsg5, 46, 121);
        gm_t4ce0jd3tdro4 t71;
        t71 = ((gm_t4ce0jd3tdro4 (*)(void *, gm_node *, gm_node *))pn70->clauses[0])(pn70->env, pn70->hctx, pn70);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t4ce0jd3tdro4 t72 = t71;
        uint64_t t73 = GM_LD(uint64_t, (((uint8_t *)&t72) + 0));
        GM_ST(uint64_t, (uint8_t *)&F.l11 + 48, t73);
    }
    {
        uint8_t t75 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 121));
        uint8_t t76 = (uint8_t)!t75;
        if (!t76) goto ie74;
        {
            void * t78 = gm_l0;
            gm_b16_8 t79 = gm_l4;
            gm_b16_8 t80 = t79;
            gm_b16_8 t81 = GM_LD(gm_b16_8, ((uint8_t *)&t80));
            gm_b16_8 t82 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 0));
            gm_b16_8 t83 = t82;
            gm_b16_8 t84 = GM_LD(gm_b16_8, ((uint8_t *)&t83));
            uint8_t t85 = gm_l3;
            void * t86 = (void *)((uint8_t *)&F.l11);
            gm_ttsm01t0d8srg t87 = gm_f_run_native_ije4jng((void *)NULL, cx, NULL, t78, &(*(gm_tdkfkbgm73fql *)((uint8_t *)&F.l2)), t81, t84, t85, t86, ((void *)0));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_ttsm01t0d8srg t88 = t87;
            gm_b0_1 t89;
            if (((((uint8_t *)&t88))[0] != 0)) {
                gm_b0_1 t90 = GM_LD(gm_b0_1, (((uint8_t *)&t88) + 0));
                t89 = t90;
            } else {
                uint8_t t91 = 0;
                F.ret = ((uint8_t)1ULL);
                goto gm_fret;
            }
        }
        {
            void * t92 = (void *)((uint8_t *)&F.l11);
            uint8_t t93 = gm_f_finish_2t60fs7((void *)NULL, cx, NULL, t92);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t94 = 0;
            F.ret = t93;
            goto gm_fret;
        }
        goto ix74;
ie74:;
ix74:;
    }
    {
        gm_b16_8 t95 = gm_l1;
        gm_f_scope_4n0q0tq((void *)&(*(gm_t6j6smj7qoctu *)((uint8_t *)&F.l12 + 0)), (void *)NULL, cx, NULL, t95);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_list_1qnmubu((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&(*(gm_tin97tt4ud0s1 *)((uint8_t *)&F.l12 + 32)) + 0)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_list_go2qg0b((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l12 + 72)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_l13 = ((uint8_t)1ULL);
    }
    {
        void * t97 = gm_l0;
        gm_b16_8 t98 = gm_l4;
        gm_b16_8 t99 = t98;
        gm_b16_8 t100 = GM_LD(gm_b16_8, ((uint8_t *)&t99));
        gm_b16_8 t101 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 0));
        gm_b16_8 t102 = t101;
        gm_b16_8 t103 = GM_LD(gm_b16_8, ((uint8_t *)&t102));
        uint8_t t104 = gm_l3;
        void * t105 = (void *)((uint8_t *)&F.l11);
        void * t106 = (void *)((uint8_t *)&F.l12);
        gm_ttsm01t0d8srg t107 = gm_f_run_native_ije4jng((void *)NULL, cx, NULL, t97, &(*(gm_tdkfkbgm73fql *)((uint8_t *)&F.l2)), t100, t103, t104, t105, t106);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_ttsm01t0d8srg t108 = t107;
        uint8_t t109;
        if (((((uint8_t *)&t108))[0] != 0)) goto mn111;
        t109 = ((uint8_t)0);
        goto me110;
mn111:;
        t109 = ((uint8_t)1);
        goto me110;
mn112:;
        gm_trap("no match arm matched the scrutinee", gm_srcrq1gl2e1lsg5, 53, 8);
me110:;
        if (!t109) goto ie96;
        {
            void * t114 = (void *)((uint8_t *)&F.l11);
            uint8_t t115 = gm_f_finish_2t60fs7((void *)NULL, cx, NULL, t114);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l13 = t115;
        }
        goto ix96;
ie96:;
ix96:;
    }
    {
        void * t117 = gm_l0;
        void * t118 = (void *)(((uint8_t *)&F.l12) + 0);
        void * t119 = (void *)(((uint8_t *)&F.l12) + 32);
        gm_b16_8 t120 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l12) + 72) + 0));
        gm_b16_8 t121 = t120;
        gm_b16_8 t122 = GM_LD(gm_b16_8, ((uint8_t *)&t121));
        uint8_t t123 = gm_f_coverage_report_hrhbf22((void *)NULL, cx, NULL, t117, t118, t119, t122, &(*(gm_th2lqbo77pvsc *)(((uint8_t *)&F.l2) + 128)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t124 = (uint8_t)!t123;
        if (!t124) goto ie116;
        {
            uint8_t t126 = 0;
            F.ret = ((uint8_t)1ULL);
            goto gm_fret;
        }
        goto ix116;
ie116:;
ix116:;
    }
    {
        uint8_t t127 = gm_l13;
        uint8_t t128 = 0;
        F.ret = t127;
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
__attribute__((aligned(4))) gm_b0_1 gm_f_run_all_fj42v4f(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1, gm_b16_8 a2, gm_b16_8 a3, uint64_t a4, gm_th2bqc58ss467 a5, const void * a6, void * a7) {
    struct gm_fr_run_all_fj42v4f F;
    gm_b16_8 gm_l0; /* runner */
    gm_b16_8 gm_l1; /* exe */
    gm_b16_8 gm_l2; /* cases */
    gm_b16_8 gm_l3; /* chosen */
    uint64_t gm_l4; /* jobs */
    gm_th2bqc58ss467 gm_l5; /* timeout */
    gm_th2lqbo77pvsc gm_l6; /* cover_dir */
    void * gm_l7; /* report */
    uint64_t gm_l8; /* n */
    uint64_t gm_l11; /* <tmp> */
    uint64_t gm_l12; /* <tmp> */
    uint64_t gm_l13; /* <tmp> */
    uint64_t gm_l14; /* <tmp> */
    gm_tm98ap4112hee gm_l15; /* t */
    uint64_t gm_l16; /* <tmp> */
    uint64_t gm_l17; /* <tmp> */
    uint64_t gm_l18; /* j */
    uint64_t gm_l19; /* <tmp> */
    uint64_t gm_l20; /* <tmp> */
    uint64_t gm_l21; /* j */
    gm_b16_8 gm_l23; /* <tmp> */
    uint64_t gm_l24; /* <tmp> */
    gm_tm98ap4112hee gm_l25; /* t */
    GM_STACK_CHECK(&F, 1427u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_l5 = a5;
    gm_l7 = a7;
    {
        uint64_t t2 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
        gm_l8 = t2;
    }
    {
        gm_f_list_7vplt15((void *)&F.l9, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_b16_8 t3 = gm_l0;
        GM_ST(gm_b16_8, (uint8_t *)&F.l10 + 0, t3);
        gm_b16_8 t4 = gm_l1;
        GM_ST(gm_b16_8, (uint8_t *)&F.l10 + 16, t4);
        gm_th2bqc58ss467 t5 = gm_l5;
        GM_ST(gm_th2bqc58ss467, (uint8_t *)&F.l10 + 32, t5);
        memcpy((uint8_t *)&F.l10 + 48, ((uint8_t *)a6), 24);
        uint64_t t6 = gm_l8;
        GM_ST(uint64_t, (uint8_t *)&F.l10 + 72, t6);
        GM_ST(uint64_t, (uint8_t *)&F.l10 + 80, ((uint64_t)0ULL));
        gm_f_list_86kk1bu((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l10 + 88)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t88rcucg1b7uk t7 = GM_CONV(gm_t88rcucg1b7uk, gm_f_mutex_t7if00c());
        GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&F.l10 + 128, t7);
        gm_t88rcucg1b7uk t8 = GM_CONV(gm_t88rcucg1b7uk, gm_f_condvar_rp5e3qr());
        GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&F.l10 + 132, t8);
    }
    {
        uint64_t t10 = gm_l4;
        uint8_t t11 = (t10 > ((uint64_t)1ULL));
        if (!t11) goto ie9;
        uint64_t t12 = gm_l8;
        uint8_t t13 = (t12 > ((uint64_t)1ULL));
        if (!t13) goto ie9;
        {
            {
                gm_l12 = ((uint64_t)0ULL);
            }
            {
                uint64_t t16 = gm_l8;
                gm_l11 = t16;
            }
            {
                for (;;) {
                    uint64_t t18 = gm_l12;
                    uint64_t t19 = gm_l11;
                    uint8_t t20 = (t18 < t19);
                    if (!t20) goto lb17;
                    {
                        uint64_t t23 = gm_l12;
                        uint64_t t24 = t23;
                        {
                            void * t28 = (void *)(((uint8_t *)&F.l10) + 88);
                            gm_t33ki4t2edp58 t29;
                            memset(&t29, 0, sizeof t29);
                            gm_tftb1odc739t5 t30 = gm_f_push_snfdb36(t28, &t29);
                            gm_tftb1odc739t5 t31 = t30;
                            if (((((uint8_t *)&t31))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t31) + 1, gm_srcrq1gl2e1lsg5, 138, 25);
                            gm_b0_1 t32 = GM_LD(gm_b0_1, (((uint8_t *)&t31) + 1));
                        }
                        goto me25;
mn26:;
                        gm_trap("no match arm matched the scrutinee", gm_srcrq1gl2e1lsg5, 138, 9);
me25:;
                    }
lc17:;
                    {
                        uint64_t t34 = gm_l12;
                        uint64_t r35; bool o36 = __builtin_add_overflow(t34, ((uint64_t)1ULL), &r35);
                        uint64_t t37 = r35;
                        gm_l12 = t37;
                    }
                }
lb17:;
            }
        }
        {
            {
                gm_l14 = ((uint64_t)0ULL);
            }
            {
                uint64_t t39;
                uint64_t t41 = gm_l4;
                uint64_t t42 = gm_l8;
                uint8_t t43 = (t41 < t42);
                if (!t43) goto ie40;
                uint64_t t45 = gm_l4;
                uint64_t t46 = t45;
                t39 = t46;
                goto ix40;
ie40:;
                uint64_t t48 = gm_l8;
                uint64_t t49 = t48;
                t39 = t49;
ix40:;
                gm_l13 = t39;
            }
            {
                for (;;) {
                    uint64_t t51 = gm_l14;
                    uint64_t t52 = gm_l13;
                    uint8_t t53 = (t51 < t52);
                    if (!t53) goto lb50;
                    {
                        uint64_t t56 = gm_l14;
                        uint64_t t57 = t56;
                        {
                            void * t61 = (void *)((uint8_t *)&F.l10);
                            gm_t62ebbi2i5rkd t62 = gm_f_spawn_534m93a(((void *)gm_f_worker_s2irjfh), t61);
                            gm_t62ebbi2i5rkd t63 = t62;
                            gm_tm98ap4112hee t64;
                            if (!((((uint8_t *)&t63))[0] != 0)) {
                                gm_tm98ap4112hee t65 = GM_LD(gm_tm98ap4112hee, (((uint8_t *)&t63) + 8));
                                t64 = t65;
                            } else {
                                goto lb50;
                            }
                            gm_l15 = t64;
                        }
                        {
                            void * t66 = (void *)((uint8_t *)&F.l9);
                            gm_tm98ap4112hee t67 = gm_l15;
                            gm_tftb1odc739t5 t68 = gm_f_push_6ct7n6b(t66, GM_CONV(gm_csfq64m1m1rt6m, t67));
                            gm_tftb1odc739t5 t69 = t68;
                            if (((((uint8_t *)&t69))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t69) + 1, gm_srcrq1gl2e1lsg5, 141, 13);
                            gm_b0_1 t70 = GM_LD(gm_b0_1, (((uint8_t *)&t69) + 1));
                        }
                        goto me58;
mn59:;
                        gm_trap("no match arm matched the scrutinee", gm_srcrq1gl2e1lsg5, 139, 9);
me58:;
                    }
lc50:;
                    {
                        uint64_t t72 = gm_l14;
                        uint64_t r73; bool o74 = __builtin_add_overflow(t72, ((uint64_t)1ULL), &r73);
                        uint64_t t75 = r73;
                        gm_l14 = t75;
                    }
                }
lb50:;
            }
        }
        goto ix9;
ie9:;
ix9:;
    }
    {
        uint64_t t77 = GM_LD(uintptr_t, (((uint8_t *)&F.l9) + 0) + 8);
        uint8_t t78 = ((t77 == ((uint64_t)0ULL)));
        if (!t78) goto ie76;
        {
            {
                gm_l17 = ((uint64_t)0ULL);
            }
            {
                uint64_t t81 = gm_l8;
                gm_l16 = t81;
            }
            {
                for (;;) {
                    uint64_t t83 = gm_l17;
                    uint64_t t84 = gm_l16;
                    uint8_t t85 = (t83 < t84);
                    if (!t85) goto lb82;
                    {
                        uint64_t t88 = gm_l17;
                        uint64_t t89 = t88;
                        uint64_t t92 = GM_LD(uint64_t, ((uint8_t *)&t89));
                        gm_l18 = t92;
                        {
                            void * t94 = gm_l7;
                            uint64_t t95 = gm_l18;
                            uint64_t t96 = (uintptr_t)t95;
                            uint64_t t97 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                            if (t96 >= (uintptr_t)(t97)) gm_trap_index(t96, t97, gm_srcrq1gl2e1lsg5, 145, 45);
                            uint64_t t98 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t96 * 8));
                            uint64_t t99 = (uintptr_t)t98;
                            uint64_t t100 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                            if (t99 >= (uintptr_t)(t100)) gm_trap_index(t99, t100, gm_srcrq1gl2e1lsg5, 145, 39);
                            gm_tbah3mk9eqfec t101 = GM_LD(gm_tbah3mk9eqfec, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t99 * 48));
                            gm_b16_8 t102 = gm_l0;
                            gm_b16_8 t103 = gm_l1;
                            uint64_t t104 = gm_l18;
                            gm_th2bqc58ss467 t105 = gm_l5;
                            gm_tp3p9lj444cp7 t106;
                            gm_f_run_exe_4182ncf((void *)&t106, (void *)NULL, cx, NULL, t102, t103, t104, t105, &(*(gm_th2lqbo77pvsc *)((uint8_t *)a6)));
                            if (gm_unw.kind) {
                                goto gm_ll86;
                            }
                            gm_b0_1 t107 = gm_f_record_abrqivi((void *)NULL, cx, NULL, t94, &t101, &t106);
                            if (gm_unw.kind) {
                                goto gm_ll86;
                            }
                        }
                        goto me90;
mn91:;
                        gm_trap("no match arm matched the scrutinee", gm_srcrq1gl2e1lsg5, 145, 9);
me90:;
                    }
lc82:;
                    {
                        uint64_t t109 = gm_l17;
                        uint64_t r110; bool o111 = __builtin_add_overflow(t109, ((uint64_t)1ULL), &r110);
                        uint64_t t112 = r110;
                        gm_l17 = t112;
                    }
                }
gm_ll86:;
                if (gm_unw.frame == &F.h && gm_unw.label == 2) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb82; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc82; }
                }
                goto gm_lad;
lb82:;
            }
        }
        {
            gm_b0_1 t113;
            uint8_t t114 = 0;
            F.ret = t113;
            goto gm_fret;
        }
        goto ix76;
ie76:;
ix76:;
    }
    {
        {
            gm_l20 = ((uint64_t)0ULL);
        }
        {
            uint64_t t116 = gm_l8;
            gm_l19 = t116;
        }
        {
            for (;;) {
                uint64_t t118 = gm_l20;
                uint64_t t119 = gm_l19;
                uint8_t t120 = (t118 < t119);
                if (!t120) goto lb117;
                {
                    uint64_t t123 = gm_l20;
                    uint64_t t124 = t123;
                    uint64_t t127 = GM_LD(uint64_t, ((uint8_t *)&t124));
                    gm_l21 = t127;
                    {
                        void * t129 = (void *)(((uint8_t *)&F.l10) + 128);
                        gm_f_lock_u5lks01(t129);
                    }
                    {
                        for (;;) {
                            uint64_t t131 = gm_l21;
                            uint64_t t132 = (uintptr_t)t131;
                            uint64_t t133 = GM_LD(uintptr_t, ((((uint8_t *)&F.l10) + 88) + 0) + 8);
                            if (t132 >= (uintptr_t)(t133)) gm_trap_index(t132, t133, gm_srcrq1gl2e1lsg5, 150, 15);
                            uint8_t t134;
                            if (((((uint8_t *)GM_LD(void *, ((((uint8_t *)&F.l10) + 88) + 0)) + t132 * 56))[48] != 0)) goto mn136;
                            t134 = ((uint8_t)1);
                            goto me135;
mn136:;
                            t134 = ((uint8_t)0);
                            goto me135;
mn137:;
                            gm_trap("no match arm matched the scrutinee", gm_srcrq1gl2e1lsg5, 150, 15);
me135:;
                            if (!t134) goto lb130;
                            {
                                void * t140 = (void *)(((uint8_t *)&F.l10) + 132);
                                void * t141 = (void *)(((uint8_t *)&F.l10) + 128);
                                gm_f_wait_k1r8nlm(t140, t141);
                            }
lc130:;
                        }
lb130:;
                    }
                    {
                        uint64_t t142 = gm_l21;
                        uint64_t t143 = (uintptr_t)t142;
                        uint64_t t144 = GM_LD(uintptr_t, ((((uint8_t *)&F.l10) + 88) + 0) + 8);
                        if (t143 >= (uintptr_t)(t144)) gm_trap_index(t143, t144, gm_srcrq1gl2e1lsg5, 151, 14);
                        gm_t33ki4t2edp58 t145 = GM_LD(gm_t33ki4t2edp58, ((uint8_t *)GM_LD(void *, ((((uint8_t *)&F.l10) + 88) + 0)) + t143 * 56));
                        gm_t33ki4t2edp58 t146 = t145;
                        gm_tp3p9lj444cp7 t147;
                        if (((((uint8_t *)&t146))[48] != 0)) {
                            gm_tp3p9lj444cp7 t148 = GM_LD(gm_tp3p9lj444cp7, (((uint8_t *)&t146) + 0));
                            t147 = t148;
                        } else {
                            gm_trap("reached `@unreachable()`", gm_srcrq1gl2e1lsg5, 151, 41);
                        }
                        F.l22 = t147;
                    }
                    {
                        void * t149 = (void *)(((uint8_t *)&F.l10) + 128);
                        gm_f_unlock_9f1vcl6(t149);
                    }
                    {
                        void * t150 = gm_l7;
                        uint64_t t151 = gm_l21;
                        uint64_t t152 = (uintptr_t)t151;
                        uint64_t t153 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                        if (t152 >= (uintptr_t)(t153)) gm_trap_index(t152, t153, gm_srcrq1gl2e1lsg5, 153, 29);
                        uint64_t t154 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t152 * 8));
                        uint64_t t155 = (uintptr_t)t154;
                        uint64_t t156 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                        if (t155 >= (uintptr_t)(t156)) gm_trap_index(t155, t156, gm_srcrq1gl2e1lsg5, 153, 23);
                        gm_b0_1 t157 = gm_f_record_abrqivi((void *)NULL, cx, NULL, t150, &(*(gm_tbah3mk9eqfec *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t155 * 48)), &(*(gm_tp3p9lj444cp7 *)((uint8_t *)&F.l22)));
                        if (gm_unw.kind) {
                            goto gm_ll121;
                        }
                    }
                    goto me125;
mn126:;
                    gm_trap("no match arm matched the scrutinee", gm_srcrq1gl2e1lsg5, 148, 5);
me125:;
                }
lc117:;
                {
                    uint64_t t159 = gm_l20;
                    uint64_t r160; bool o161 = __builtin_add_overflow(t159, ((uint64_t)1ULL), &r160);
                    uint64_t t162 = r160;
                    gm_l20 = t162;
                }
            }
gm_ll121:;
            if (gm_unw.frame == &F.h && gm_unw.label == 3) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb117; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc117; }
            }
            goto gm_lad;
lb117:;
        }
    }
    {
        {
            gm_b16_8 t164 = GM_LD(gm_b16_8, (((uint8_t *)&F.l9) + 0));
            gm_l23 = t164;
        }
        {
            gm_l24 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t166 = gm_l24;
                uint64_t t167 = GM_LD(uintptr_t, ((uint8_t *)&gm_l23) + 8);
                uint8_t t168 = (t166 < t167);
                if (!t168) goto lb165;
                {
                    uint64_t t171 = gm_l24;
                    uint64_t t172 = (uintptr_t)t171;
                    uint64_t t173 = GM_LD(uintptr_t, ((uint8_t *)&gm_l23) + 8);
                    gm_tm98ap4112hee t176 = GM_LD(gm_tm98ap4112hee, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l23)) + t172 * 8));
                    gm_l25 = t176;
                    {
                        gm_tm98ap4112hee t178 = gm_l25;
                        gm_f_join_r8j2dp1(GM_CONV(gm_csfq64m1m1rt6m, t178));
                    }
                    goto me174;
mn175:;
                    gm_trap("no match arm matched the scrutinee", gm_srcrq1gl2e1lsg5, 155, 5);
me174:;
                }
lc165:;
                {
                    uint64_t t180 = gm_l24;
                    uint64_t r181; bool o182 = __builtin_add_overflow(t180, ((uint64_t)1ULL), &r181);
                    uint64_t t183 = r181;
                    gm_l24 = t183;
                }
            }
lb165:;
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
__attribute__((aligned(4))) gm_ttsm01t0d8srg gm_f_run_native_ije4jng(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_b16_8 a2, gm_b16_8 a3, uint8_t a4, void * a5, void * a6) {
    struct gm_fr_run_native_ije4jng F;
    void * gm_l0; /* s */
    gm_b16_8 gm_l2; /* cases */
    gm_b16_8 gm_l3; /* chosen */
    uint8_t gm_l4; /* bench */
    void * gm_l5; /* report */
    void * gm_l6; /* cover */
    gm_b16_8 gm_l8; /* <tmp> */
    uint64_t gm_l9; /* <tmp> */
    uint64_t gm_l10; /* i */
    uint32_t gm_l11; /* entry */
    gm_b16_8 gm_l13; /* dir */
    void * gm_l14; /* c */
    gm_b16_8 gm_l16; /* <tmp> */
    uint64_t gm_l17; /* <tmp> */
    uint32_t gm_l18; /* f */
    gm_b16_8 gm_l19; /* exe */
    gm_b16_8 gm_l20; /* e */
    uint64_t gm_l22; /* jobs */
    gm_th2lqbo77pvsc gm_l23; /* cover_dir */
    void * gm_l24; /* c */
    uint64_t gm_l25; /* <tmp> */
    uint64_t gm_l26; /* <tmp> */
    uint64_t gm_l27; /* j */
    gm_b16_8 gm_l28; /* text */
    GM_STACK_CHECK(&F, 2052u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&F.l1, a1, sizeof F.l1);
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_l5 = a5;
    gm_l6 = a6;
    {
        gm_f_list_go2qg0b((void *)&F.l7, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t3 = gm_l3;
            gm_l8 = t3;
        }
        {
            gm_l9 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t5 = gm_l9;
                uint64_t t6 = GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8);
                uint8_t t7 = (t5 < t6);
                if (!t7) goto lb4;
                {
                    uint64_t t10 = gm_l9;
                    uint64_t t11 = (uintptr_t)t10;
                    uint64_t t12 = GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8);
                    uint64_t t13 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l8)) + t11 * 8));
                    uint64_t t14 = t13;
                    uint64_t t17 = GM_LD(uint64_t, ((uint8_t *)&t14));
                    gm_l10 = t17;
                    {
                        uint64_t t19 = gm_l10;
                        uint64_t t20 = (uintptr_t)t19;
                        uint64_t t21 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                        if (t20 >= (uintptr_t)(t21)) gm_trap_index(t20, t21, gm_srcrq1gl2e1lsg5, 74, 18);
                        uint32_t t22 = GM_LD(uint32_t, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t20 * 48) + 40));
                        gm_l11 = t22;
                    }
                    {
                        void * t23 = (void *)((uint8_t *)&F.l7);
                        uint32_t t24;
                        uint8_t t26 = gm_l4;
                        if (!t26) goto ie25;
                        void * t28 = gm_l0;
                        uint32_t t29 = gm_l11;
                        uint32_t t30 = gm_f_entry_point_n2gu79u((void *)NULL, cx, NULL, t28, t29);
                        if (gm_unw.kind) {
                            goto gm_ll8;
                        }
                        uint32_t t31 = t30;
                        t24 = t31;
                        goto ix25;
ie25:;
                        uint32_t t33 = gm_l11;
                        uint32_t t34 = t33;
                        t24 = t34;
ix25:;
                        gm_tftb1odc739t5 t35 = gm_f_push_im9d4ro(t23, t24);
                        gm_tftb1odc739t5 t36 = t35;
                        if (((((uint8_t *)&t36))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t36) + 1, gm_srcrq1gl2e1lsg5, 75, 9);
                        gm_b0_1 t37 = GM_LD(gm_b0_1, (((uint8_t *)&t36) + 1));
                    }
                    goto me15;
mn16:;
                    gm_trap("no match arm matched the scrutinee", gm_srcrq1gl2e1lsg5, 72, 5);
me15:;
                }
lc4:;
                {
                    uint64_t t39 = gm_l9;
                    uint64_t r40; bool o41 = __builtin_add_overflow(t39, ((uint64_t)1ULL), &r40);
                    uint64_t t42 = r40;
                    gm_l9 = t42;
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
    }
    {
        gm_b16_8 t43 = gm_f_temp_dir_i6apmsd((void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b16_8 t44;
        uint8_t t46 = gm_l4;
        if (!t46) goto ie45;
        gm_b16_8 t48;
        GM_ST(void *, (uint8_t *)&t48, (void *)(gm_shg1pn1r3sjgi));
        GM_ST(uintptr_t, (uint8_t *)&t48 + 8, (uintptr_t)(12));
        gm_b16_8 t49 = t48;
        t44 = t49;
        goto ix45;
ie45:;
        gm_b16_8 t51;
        GM_ST(void *, (uint8_t *)&t51, (void *)(gm_s79el5a3kii78));
        GM_ST(uintptr_t, (uint8_t *)&t51 + 8, (uintptr_t)(11));
        gm_b16_8 t52 = t51;
        t44 = t52;
ix45:;
        void * t53 = ((uint8_t *)&F.l12);
        if ((uintptr_t)0 > (uintptr_t)4096 || (uintptr_t)4096 > (uintptr_t)4096) gm_trap_slice((uintptr_t)0, (uintptr_t)4096, 4096, gm_srcrq1gl2e1lsg5, 78, 96);
        gm_b16_8 t54;
        GM_ST(void *, (uint8_t *)&t54, (void *)(((uint8_t *)t53 + (uintptr_t)0 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t54 + 8, (uintptr_t)((uintptr_t)4096 - (uintptr_t)0));
        gm_t5uv522f0p612 t55;
        gm_f_make_temp_dir_vi8f88d((void *)&t55, (void *)NULL, cx, NULL, t43, t44, t54);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t5uv522f0p612 t56 = t55;
        gm_b16_8 t57;
        if (!((((uint8_t *)&t56))[0] != 0)) {
            gm_b16_8 t58 = GM_LD(gm_b16_8, (((uint8_t *)&t56) + 8));
            t57 = t58;
        } else {
            {
                gm_b16_8 t60 = gm_f_temp_dir_i6apmsd((void *)NULL, cx, NULL);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_b16_8 t61 = t60;
                if (gm_pf454qfrpt6r(cx, gm_svaibtfuu0gf2, 47, gm_srcrq1gl2e1lsg5, 79, 9)) {
                    goto gm_lad;
                }
                if (gm_pfu8dq5bikbh(cx, &gm_ty0iqiubfm7e, ((uint8_t *)&t61), gm_srcrq1gl2e1lsg5, 79, 9)) {
                    goto gm_lad;
                }
                if (gm_pf454qfrpt6r(cx, gm_s06m3met1mb9v, 2, gm_srcrq1gl2e1lsg5, 79, 9)) {
                    goto gm_lad;
                }
            }
            gm_ttsm01t0d8srg t62;
            memset(&t62, 0, sizeof t62);
            uint8_t t63 = 0;
            F.ret = t62;
            goto gm_fret;
        }
        gm_l13 = t57;
    }
    {
    }
    {
        void * t68 = gm_l6;
        void * t69 = t68;
        if (!(GM_LD(void *, ((uint8_t *)&t69)) != 0)) goto ie67;
        void * t70 = GM_LD(void *, ((uint8_t *)&t69));
        gm_l14 = t70;
        {
            gm_f_list_go2qg0b((void *)&F.l15, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_ld1_1;
            }
        }
        {
            void * t72 = (void *)((uint8_t *)&F.l15);
            gm_b16_8 t73 = GM_LD(gm_b16_8, (((uint8_t *)&F.l7) + 0));
            gm_b16_8 t74 = t73;
            gm_b16_8 t75 = GM_LD(gm_b16_8, ((uint8_t *)&t74));
            gm_tftb1odc739t5 t76 = gm_f_extend_7hs67uj(t72, t75);
            gm_tftb1odc739t5 t77 = t76;
            if (((((uint8_t *)&t77))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t77) + 1, gm_srcrq1gl2e1lsg5, 86, 9);
            gm_b0_1 t78 = GM_LD(gm_b0_1, (((uint8_t *)&t77) + 1));
        }
        {
            {
                void * t80 = gm_l0;
                gm_b16_8 t81 = GM_LD(gm_b16_8, ((((uint8_t *)t80) + 920) + 0));
                gm_l16 = t81;
            }
            {
                gm_l17 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t83 = gm_l17;
                    uint64_t t84 = GM_LD(uintptr_t, ((uint8_t *)&gm_l16) + 8);
                    uint8_t t85 = (t83 < t84);
                    if (!t85) goto lb82;
                    {
                        uint64_t t88 = gm_l17;
                        uint64_t t89 = (uintptr_t)t88;
                        uint64_t t90 = GM_LD(uintptr_t, ((uint8_t *)&gm_l16) + 8);
                        uint32_t t91 = GM_LD(uint32_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l16)) + t89 * 4));
                        uint32_t t92 = t91;
                        uint32_t t95 = GM_LD(uint32_t, ((uint8_t *)&t92));
                        gm_l18 = t95;
                        {
                            uint32_t t98 = gm_l18;
                            gm_b16_8 t99 = GM_LD(gm_b16_8, (((uint8_t *)&F.l15) + 0));
                            gm_b16_8 t100 = t99;
                            uint8_t t101 = 0;
                            uint32_t t102 = t98;
                            for (uintptr_t i = 0; i < (uintptr_t)(GM_LD(uintptr_t, ((uint8_t *)&t100) + 8)); i++) if (gm_eq467d5cm1d5(((uint8_t *)GM_LD(void *, ((uint8_t *)&t100))) + i * 4, ((uint8_t *)&t102))) { t101 = 1; break; }
                            uint8_t t103 = (uint8_t)(!t101);
                            if (!t103) goto ie97;
                            {
                                void * t105 = (void *)((uint8_t *)&F.l15);
                                uint32_t t106 = gm_l18;
                                gm_tftb1odc739t5 t107 = gm_f_push_im9d4ro(t105, t106);
                                gm_tftb1odc739t5 t108 = t107;
                                if (((((uint8_t *)&t108))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t108) + 1, gm_srcrq1gl2e1lsg5, 87, 65);
                                gm_b0_1 t109 = GM_LD(gm_b0_1, (((uint8_t *)&t108) + 1));
                            }
                            goto ix97;
ie97:;
ix97:;
                        }
                        goto me93;
mn94:;
                        gm_trap("no match arm matched the scrutinee", gm_srcrq1gl2e1lsg5, 87, 9);
me93:;
                    }
lc82:;
                    {
                        uint64_t t111 = gm_l17;
                        uint64_t r112; bool o113 = __builtin_add_overflow(t111, ((uint64_t)1ULL), &r112);
                        uint64_t t114 = r112;
                        gm_l17 = t114;
                    }
                }
lb82:;
            }
        }
        {
            void * t116 = gm_l0;
            gm_b16_8 t117 = GM_LD(gm_b16_8, (((uint8_t *)&F.l15) + 0));
            gm_b16_8 t118 = t117;
            gm_b16_8 t119 = GM_LD(gm_b16_8, ((uint8_t *)&t118));
            uint8_t t120 = gm_f_prepare_codegen_4hnlio1((void *)NULL, cx, NULL, t116, t119);
            if (gm_unw.kind) {
                goto gm_ld1_1;
            }
            uint8_t t121 = (uint8_t)!t120;
            if (!t121) goto ie115;
            {
                gm_ttsm01t0d8srg t123;
                memset(&t123, 0, sizeof t123);
                uint8_t t124 = 0;
                F.ret = t123;
                gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = t124;
                goto gm_ld1_1;
            }
            goto ix115;
ie115:;
ix115:;
        }
        {
            void * t125 = gm_l14;
            uint8_t *t126 = (((uint8_t *)t125) + 32);
            void * t127 = gm_l0;
            void * t128 = gm_l14;
            void * t129 = (void *)(((uint8_t *)t128) + 0);
            gm_f_instrument_mk8clvl((void *)&(*(gm_tin97tt4ud0s1 *)t126), (void *)NULL, cx, NULL, t127, t129);
            if (gm_unw.kind) {
                goto gm_ld1_1;
            }
        }
        goto ix67;
ie67:;
ix67:;
    }
    {
        gm_b16_8 t130 = gm_l13;
        void * t131 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t132;
        GM_ST(void *, (uint8_t *)&t132, (void *)(gm_sjbhqu0o8ebub));
        GM_ST(uintptr_t, (uint8_t *)&t132 + 8, (uintptr_t)(5));
        gm_b16_8 t133 = gm_f_native_exe_name_rqchv3n((void *)NULL, cx, NULL, t131, t132);
        if (gm_unw.kind) {
            goto gm_ld1_1;
        }
        gm_b16_8 t134 = gm_f_join_qveh5b2((void *)NULL, cx, NULL, t130, t133);
        if (gm_unw.kind) {
            goto gm_ld1_1;
        }
        gm_l19 = t134;
    }
    {
        void * t136 = gm_l0;
        gm_b16_8 t137 = GM_LD(gm_b16_8, (((uint8_t *)&F.l7) + 0));
        gm_b16_8 t138 = t137;
        gm_b16_8 t139 = GM_LD(gm_b16_8, ((uint8_t *)&t138));
        gm_b16_8 t140 = gm_l19;
        uint8_t t141 = gm_f_compile_entries_l9qlniu((void *)NULL, cx, NULL, t136, t139, ((uint8_t)1), t140, &(*(gm_tdkfkbgm73fql *)((uint8_t *)&F.l1)));
        if (gm_unw.kind) {
            goto gm_ld1_1;
        }
        uint8_t t142 = (uint8_t)!t141;
        if (!t142) goto ie135;
        {
            gm_ttsm01t0d8srg t144;
            memset(&t144, 0, sizeof t144);
            uint8_t t145 = 0;
            F.ret = t144;
            gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = t145;
            goto gm_ld1_1;
        }
        goto ix135;
ie135:;
ix135:;
    }
    {
        gm_b16_8 t146 = gm_l19;
        gm_t470aqkia5212 t147;
        gm_f_runner_for_79o4h4m((void *)&t147, (void *)NULL, cx, NULL, t146);
        if (gm_unw.kind) {
            goto gm_ld1_1;
        }
        gm_t470aqkia5212 t148 = t147;
        gm_trqfvmjtt4sij t149;
        if (!((((uint8_t *)&t148))[0] != 0)) {
            gm_trqfvmjtt4sij t150 = GM_LD(gm_trqfvmjtt4sij, (((uint8_t *)&t148) + 8));
            t149 = t150;
        } else {
            gm_b16_8 t151 = GM_LD(gm_b16_8, (((uint8_t *)&t148) + 8));
            gm_l20 = t151;
            {
                gm_b16_8 t153 = gm_l20;
                gm_b16_8 t154 = t153;
                if (gm_pf454qfrpt6r(cx, gm_s5lqhiu2bn6of, 7, gm_srcrq1gl2e1lsg5, 96, 9)) {
                    goto gm_ld1_1;
                }
                if (gm_pfu8dq5bikbh(cx, &gm_ty0iqiubfm7e, ((uint8_t *)&t154), gm_srcrq1gl2e1lsg5, 96, 9)) {
                    goto gm_ld1_1;
                }
                if (gm_pf454qfrpt6r(cx, gm_so514oj5nbhcu, 1, gm_srcrq1gl2e1lsg5, 96, 9)) {
                    goto gm_ld1_1;
                }
            }
            gm_node *pn155 = gm_find(cx, 621690230);
            if (!pn155) gm_trap("no handler installed for effect `Env`", gm_srcrq1gl2e1lsg5, 97, 9);
            if (pn155->kind == GM_NATIVE) {
                gm_unw.code = (uint8_t)((uint8_t)2ULL);
                gm_unw.kind = GM_EXIT; gm_unw.frame = NULL; gm_unw.label = 0; gm_unw.is_err = 1;
                goto gm_ld1_1;
            } else {
                ((gm_b0_1 (*)(void *, gm_node *, gm_node *, uint8_t))pn155->clauses[2])(pn155->env, pn155->hctx, pn155, ((uint8_t)2ULL));
            }
            if (gm_unw.kind) {
                goto gm_ld1_1;
            }
        }
        F.l21 = t149;
    }
    {
        uint64_t t156;
        uint8_t t158 = gm_l4;
        if (!t158) goto ie157;
        uint64_t t160 = ((uint64_t)1ULL);
        t156 = t160;
        goto ix157;
ie157:;
        uint64_t t162 = GM_LD(uint64_t, (((uint8_t *)&F.l1) + 96));
        uint64_t t163 = t162;
        t156 = t163;
ix157:;
        gm_l22 = t156;
    }
    {
        gm_th2lqbo77pvsc t164;
        void * t166 = gm_l6;
        void * t167 = t166;
        uint8_t t168;
        if ((GM_LD(void *, ((uint8_t *)&t167)) != 0)) goto mn170;
        t168 = ((uint8_t)0);
        goto me169;
mn170:;
        t168 = ((uint8_t)1);
        goto me169;
mn171:;
        gm_trap("no match arm matched the scrutinee", gm_srcrq1gl2e1lsg5, 102, 29);
me169:;
        if (!t168) goto ie165;
        gm_b16_8 t173 = gm_l13;
        gm_b16_8 t174 = t173;
        gm_th2lqbo77pvsc t175;
        GM_ST(uint8_t, (uint8_t *)&t175 + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&t175 + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&t175 + 20, 0);
        GM_ST(gm_b16_8, ((uint8_t *)&t175), t174);
        ((uint8_t *)&t175)[16] = 1;
        t164 = t175;
        goto ix165;
ie165:;
        gm_th2lqbo77pvsc t177;
        memset(&t177, 0, sizeof t177);
        gm_th2lqbo77pvsc t178 = t177;
        t164 = t178;
ix165:;
        gm_l23 = t164;
    }
    {
        gm_b16_8 t179 = GM_LD(gm_b16_8, (((uint8_t *)&F.l21) + 0));
        gm_b16_8 t180 = t179;
        gm_b16_8 t181 = GM_LD(gm_b16_8, ((uint8_t *)&t180));
        gm_b16_8 t182 = gm_l19;
        gm_b16_8 t183 = gm_l2;
        gm_b16_8 t184 = gm_l3;
        uint64_t t185 = gm_l22;
        gm_th2bqc58ss467 t186 = GM_LD(gm_th2bqc58ss467, (((uint8_t *)&F.l1) + 104));
        void * t187 = gm_l5;
        gm_b0_1 t188 = gm_f_run_all_fj42v4f((void *)NULL, cx, NULL, t181, t182, t183, t184, t185, t186, &(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l23)), t187);
        if (gm_unw.kind) {
            goto gm_ld1_1;
        }
    }
    {
        void * t190 = gm_l6;
        void * t191 = t190;
        if (!(GM_LD(void *, ((uint8_t *)&t191)) != 0)) goto ie189;
        void * t192 = GM_LD(void *, ((uint8_t *)&t191));
        gm_l24 = t192;
        {
            {
                gm_l26 = ((uint64_t)0ULL);
            }
            {
                uint64_t t195 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                gm_l25 = t195;
            }
            {
                for (;;) {
                    uint64_t t197 = gm_l26;
                    uint64_t t198 = gm_l25;
                    uint8_t t199 = (t197 < t198);
                    if (!t199) goto lb196;
                    {
                        uint64_t t202 = gm_l26;
                        uint64_t t203 = t202;
                        uint64_t t206 = GM_LD(uint64_t, ((uint8_t *)&t203));
                        gm_l27 = t206;
                        {
                            gm_b16_8 t208 = gm_l13;
                            uint64_t t209 = gm_l27;
                            gm_b16_8 t210 = gm_f_cover_file_bhhubi9((void *)NULL, cx, NULL, t208, t209);
                            if (gm_unw.kind) {
                                goto gm_ll200;
                            }
                            gm_tgvh15qshf643 t211;
                            gm_f_read_all_r9rh9qd((void *)&t211, (void *)NULL, cx, NULL, t210);
                            if (gm_unw.kind) {
                                goto gm_ll200;
                            }
                            gm_tgvh15qshf643 t212 = t211;
                            gm_b16_8 t213;
                            if (!((((uint8_t *)&t212))[0] != 0)) {
                                gm_b16_8 t214 = GM_LD(gm_b16_8, (((uint8_t *)&t212) + 8));
                                t213 = t214;
                            } else {
                                goto lc196;
                            }
                            gm_l28 = t213;
                        }
                        {
                            void * t215 = gm_l24;
                            void * t216 = (void *)(((uint8_t *)t215) + 72);
                            gm_b16_8 t217 = gm_l28;
                            gm_b16_8 t218 = t217;
                            gm_b16_8 t219 = GM_LD(gm_b16_8, ((uint8_t *)&t218));
                            gm_b0_1 t220 = gm_f_add_counts_hprtdd9((void *)NULL, cx, NULL, t216, t219);
                            if (gm_unw.kind) {
                                goto gm_ll200;
                            }
                        }
                        goto me204;
mn205:;
                        gm_trap("no match arm matched the scrutinee", gm_srcrq1gl2e1lsg5, 105, 9);
me204:;
                    }
lc196:;
                    {
                        uint64_t t222 = gm_l26;
                        uint64_t r223; bool o224 = __builtin_add_overflow(t222, ((uint64_t)1ULL), &r223);
                        uint64_t t225 = r223;
                        gm_l26 = t225;
                    }
                }
gm_ll200:;
                if (gm_unw.frame == &F.h && gm_unw.label == 2) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb196; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc196; }
                }
                goto gm_ld1_1;
lb196:;
            }
        }
        goto ix189;
ie189:;
ix189:;
    }
    {
        gm_b0_1 t226;
        gm_ttsm01t0d8srg t227;
        GM_ST(gm_b0_1, ((uint8_t *)&t227), t226);
        ((uint8_t *)&t227)[0] = 1;
        uint8_t t228 = 0;
        F.ret = t227;
        gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = t228;
        goto gm_ld1_1;
    }
    {
        {
            gm_b16_8 t230 = gm_l13;
            gm_ttln0d0hd70en t231 = gm_f_remove_all_h0k1qkh((void *)NULL, cx, NULL, t230);
        }
    }
    goto gm_ls232;
gm_ld1_1:;
    {
        gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
        {
            gm_b16_8 t234 = gm_l13;
            gm_ttln0d0hd70en t235 = gm_f_remove_all_h0k1qkh((void *)NULL, cx, NULL, t234);
        }
        gm_unw = gm_saved;
    }
    goto gm_lad;
gm_ls232:;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_selected_8kh2cc4(const void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_selected_8kh2cc4 F;
    gm_th2lqbo77pvsc gm_l1; /* filter */
    gm_b16_8 gm_l2; /* f */
    gm_b16_8 gm_l3; /* m */
    GM_STACK_CHECK_SHALLOW(&F, 288u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_th2lqbo77pvsc t2 = GM_LD(gm_th2lqbo77pvsc, ((uint8_t *)a1));
        gm_th2lqbo77pvsc t3 = t2;
        gm_b16_8 t4;
        if (((((uint8_t *)&t3))[16] != 0)) {
            gm_b16_8 t5 = GM_LD(gm_b16_8, (((uint8_t *)&t3) + 0));
            t4 = t5;
        } else {
            uint8_t t6 = 0;
            F.ret = ((uint8_t)1);
            goto gm_fret;
        }
        gm_l2 = t4;
    }
    {
        gm_b16_8 t8 = GM_LD(gm_b16_8, (((uint8_t *)a0) + 0));
        gm_b16_8 t9 = gm_l2;
        uint8_t t10 = gm_f_contains_gkki4kq(t8, t9);
        if (!t10) goto ie7;
        {
            uint8_t t12 = 0;
            F.ret = ((uint8_t)1);
            goto gm_fret;
        }
        goto ix7;
ie7:;
ix7:;
    }
    {
        gm_th2lqbo77pvsc t13 = GM_LD(gm_th2lqbo77pvsc, (((uint8_t *)a0) + 16));
        gm_th2lqbo77pvsc t14 = t13;
        gm_b16_8 t15;
        if (((((uint8_t *)&t14))[16] != 0)) {
            gm_b16_8 t16 = GM_LD(gm_b16_8, (((uint8_t *)&t14) + 0));
            t15 = t16;
        } else {
            uint8_t t17 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        gm_l3 = t15;
    }
    {
        gm_b16_8 t18 = gm_l3;
        gm_b16_8 t19 = gm_l2;
        uint8_t t20 = gm_f_contains_gkki4kq(t18, t19);
        uint8_t t21 = 0;
        F.ret = t20;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) uint8_t gm_f_selected_8kh2cc4_c(gm_tbah3mk9eqfec a0, gm_th2lqbo77pvsc a1) { return gm_f_selected_8kh2cc4(&a0, &a1); }
inline __attribute__((aligned(4))) void gm_f_worker_s2irjfh(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_worker_s2irjfh F;
    void * gm_l0; /* p */
    uint64_t gm_l1; /* j */
    GM_STACK_CHECK(&F, 425u);
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
        gm_b0_1 t13 = gm_f_system_output_eg72lom();
        gm_b0_1 t14 = t13;
        gm_node wn15 = {.eff = 515083575, .kind = GM_DECL, .clauses = gm_hdu50j4i95ke, .env = (void *)&t14, .hctx = (&wn12), .with_frame = &F.h, .with_label = 0, .result = (void *)&F.w0, .fw = NULL, .next = (&wn12)};
        gm_node wn16 = {.eff = 2063121243, .kind = GM_DECL, .clauses = gm_hddfeetqn95b, .env = (void *)&t14, .hctx = (&wn12), .with_frame = &F.h, .with_label = 0, .result = (void *)&F.w0, .fw = NULL, .next = (&wn15)};
        {
            for (;;) {
                if (!((uint8_t)1)) goto lb20;
                {
                    void * t23 = gm_l0;
                    void * t24 = (void *)(((uint8_t *)t23) + 128);
                    gm_f_lock_u5lks01(t24);
                }
                {
                    void * t25 = gm_l0;
                    uint64_t t26 = GM_LD(uint64_t, (((uint8_t *)t25) + 80));
                    gm_l1 = t26;
                }
                {
                    void * t27 = gm_l0;
                    void * t28 = gm_l0;
                    uint64_t t29 = GM_LD(uint64_t, (((uint8_t *)t28) + 80));
                    uint64_t r30; bool o31 = __builtin_add_overflow(t29, ((uint64_t)1ULL), &r30);
                    if (o31) gm_trap("integer overflow in `+`", gm_srcrq1gl2e1lsg5, 164, 13);
                    uint64_t t32 = r30;
                    GM_ST(uint64_t, (((uint8_t *)t27) + 80), t32);
                }
                {
                    void * t33 = gm_l0;
                    void * t34 = (void *)(((uint8_t *)t33) + 128);
                    gm_f_unlock_9f1vcl6(t34);
                }
                {
                    uint64_t t36 = gm_l1;
                    void * t37 = gm_l0;
                    uint64_t t38 = GM_LD(uint64_t, (((uint8_t *)t37) + 72));
                    uint8_t t39 = (t36 >= t38);
                    if (!t39) goto ie35;
                    {
                        goto lb20;
                    }
                    goto ix35;
ie35:;
ix35:;
                }
                {
                    void * t41 = gm_l0;
                    gm_b16_8 t42 = GM_LD(gm_b16_8, (((uint8_t *)t41) + 0));
                    void * t43 = gm_l0;
                    gm_b16_8 t44 = GM_LD(gm_b16_8, (((uint8_t *)t43) + 16));
                    uint64_t t45 = gm_l1;
                    void * t46 = gm_l0;
                    gm_th2bqc58ss467 t47 = GM_LD(gm_th2bqc58ss467, (((uint8_t *)t46) + 32));
                    void * t48 = gm_l0;
                    gm_f_run_exe_4182ncf((void *)&F.l2, (void *)NULL, (&wn16), NULL, t42, t44, t45, t47, &(*(gm_th2lqbo77pvsc *)(((uint8_t *)t48) + 48)));
                    if (gm_unw.kind) {
                        goto gm_ll21;
                    }
                }
                {
                    void * t49 = gm_l0;
                    void * t50 = (void *)(((uint8_t *)t49) + 128);
                    gm_f_lock_u5lks01(t50);
                }
                {
                    uint64_t t51 = gm_l1;
                    uint64_t t52 = (uintptr_t)t51;
                    void * t53 = gm_l0;
                    uint64_t t54 = GM_LD(uintptr_t, ((((uint8_t *)t53) + 88) + 0) + 8);
                    if (t52 >= (uintptr_t)(t54)) gm_trap_index(t52, t54, gm_srcrq1gl2e1lsg5, 169, 13);
                    gm_tp3p9lj444cp7 t55 = F.l2;
                    gm_t33ki4t2edp58 t56;
                    GM_ST(uint8_t, (uint8_t *)&t56 + 49, 0);
                    GM_ST(uint16_t, (uint8_t *)&t56 + 50, 0);
                    GM_ST(uint32_t, (uint8_t *)&t56 + 52, 0);
                    GM_ST(gm_tp3p9lj444cp7, ((uint8_t *)&t56), t55);
                    ((uint8_t *)&t56)[48] = 1;
                    GM_ST(gm_t33ki4t2edp58, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t53) + 88) + 0)) + t52 * 56), t56);
                }
                {
                    void * t57 = gm_l0;
                    void * t58 = (void *)(((uint8_t *)t57) + 132);
                    gm_f_broadcast_bfak2oh(t58);
                }
                {
                    void * t59 = gm_l0;
                    void * t60 = (void *)(((uint8_t *)t59) + 128);
                    gm_f_unlock_9f1vcl6(t60);
                }
lc20:;
            }
gm_ll21:;
            if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb20; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc20; }
            }
            goto gm_ll18;
lb20:;
        }
        F.w0 = GM_ZERO(gm_b0_1);
        goto gm_ls61;
gm_ll18:;
        if (gm_unw.kind == GM_BREAK && gm_unw.frame == &F.h && gm_unw.label == 0) { gm_unw.kind = GM_NONE; goto we17; }
        goto gm_lad;
gm_ls61:;
we17:;
        gm_b0_1 t62 = F.w0;
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
