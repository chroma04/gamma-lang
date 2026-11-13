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
static uint64_t gm_mix(uint64_t h, const void *p, size_t n) {
    const uint8_t *b = p;
    for (size_t i = 0; i < n; i++) { h ^= b[i]; h *= 0x100000001b3ULL; }
    return h;
}
/* Mixes the eight little-endian bytes of x, whatever the byte order of the machine. */
static uint64_t gm_mix64(uint64_t h, uint64_t x) {
    for (int i = 0; i < 8; i++) { h ^= (uint8_t)(x >> (8 * i)); h *= 0x100000001b3ULL; }
    return h;
}
/* Mixes the minimal two's-complement little-endian bytes of v (as BigInt::to_signed_bytes_le). */
static uint64_t gm_hash_int(uint64_t h, __int128 v) {
    for (;;) {
        uint8_t byte = (uint8_t)v; v >>= 8;
        h ^= byte; h *= 0x100000001b3ULL;
        if ((v == 0 && !(byte & 0x80)) || (v == -1 && (byte & 0x80))) break;
    }
    return h;
}
static uint64_t gm_hash_uint(uint64_t h, unsigned __int128 v) {
    for (;;) {
        uint8_t byte = (uint8_t)v; v >>= 8;
        h ^= byte; h *= 0x100000001b3ULL;
        if (v == 0 && !(byte & 0x80)) break;
        if (v == 0) { h *= 0x100000001b3ULL; break; }
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
typedef struct gm_csc7lc6jlkqkuq { uint64_t f0; uint64_t f1; } gm_csc7lc6jlkqkuq;
typedef struct gm_csguankko4lmhd { uint32_t f0; } gm_csguankko4lmhd;
typedef struct gm_csia7oevr2u0fk { _Bool f0; unsigned __int128 f1; } gm_csia7oevr2u0fk;
typedef struct { uint64_t f0; uint64_t f1; } gm_t2lqu8jsj511d;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint8_t a[16]; } gm_t51b2en4qp02s;
typedef struct { uint8_t f0; uint8_t p1[15]; unsigned __int128 f1; } gm_t86h6hhm6d76n;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint32_t f0; uint8_t f1; uint8_t pe[3]; } gm_t9805qtonbsag;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint8_t a[17]; } gm_tb1c3uf711glh;
typedef struct { uint32_t f0; uint32_t f1; } gm_tcc1rq4n1avb2;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint8_t f0; uint8_t p1[1]; uint16_t f1; } gm_tjcj4j2gbh6c8;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { uint8_t f0; uint8_t p1[3]; union { uint32_t v0; } f1; } gm_tmn7n0n9raou5;
typedef struct { uint64_t f0; uint32_t f1; uint8_t pe[4]; } gm_tn68el8d14vku;
typedef struct { __int128 f0; } gm_tpl3q6e8s9qo1;
typedef struct { unsigned __int128 f0; } gm_tsn2omssg6cpf;
typedef struct { uint8_t f0; uint8_t p1[3]; uint32_t f1; } gm_ttovhfee8mf28;
_Static_assert(sizeof(gm_csc7lc6jlkqkuq) == 16 && _Alignof(gm_csc7lc6jlkqkuq) == 8 && offsetof(gm_csc7lc6jlkqkuq, f0) == 0 && offsetof(gm_csc7lc6jlkqkuq, f1) == 8, "layout of `BlobPart`");
_Static_assert(sizeof(gm_csguankko4lmhd) == 4 && _Alignof(gm_csguankko4lmhd) == 4 && offsetof(gm_csguankko4lmhd, f0) == 0, "layout of `Mutex`");
_Static_assert(sizeof(gm_csia7oevr2u0fk) == 32 && _Alignof(gm_csia7oevr2u0fk) == 16 && offsetof(gm_csia7oevr2u0fk, f0) == 0 && offsetof(gm_csia7oevr2u0fk, f1) == 16, "layout of `NumVal`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; uint64_t f1; uint64_t f2; gm_b16_8 f3; } gm_t2jeg3bg7sfqa;
_Static_assert(sizeof(gm_t2lqu8jsj511d) == 16 && _Alignof(gm_t2lqu8jsj511d) == 8 && offsetof(gm_t2lqu8jsj511d, f0) == 0 && offsetof(gm_t2lqu8jsj511d, f1) == 8, "layout of `(u64, u64)`");
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
_Static_assert(sizeof(gm_t51b2en4qp02s) == 16 && _Alignof(gm_t51b2en4qp02s) == 1 && offsetof(gm_t51b2en4qp02s, a) == 0, "layout of `[16]u8`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tn68el8d14vku v0; gm_tn68el8d14vku v1; gm_tf5srh4mp4b3i v2; uint8_t v3; void * v4; uint32_t v5; } f1; } gm_t76123gfet73g;
_Static_assert(sizeof(gm_t86h6hhm6d76n) == 32 && _Alignof(gm_t86h6hhm6d76n) == 16 && offsetof(gm_t86h6hhm6d76n, f0) == 0 && offsetof(gm_t86h6hhm6d76n, f1) == 16, "layout of `NumVal`");
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
_Static_assert(sizeof(gm_t9805qtonbsag) == 8 && _Alignof(gm_t9805qtonbsag) == 4 && offsetof(gm_t9805qtonbsag, f0) == 0 && offsetof(gm_t9805qtonbsag, f1) == 4, "layout of `?u32`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
_Static_assert(sizeof(gm_tb1c3uf711glh) == 17 && _Alignof(gm_tb1c3uf711glh) == 1 && offsetof(gm_tb1c3uf711glh, a) == 0, "layout of `[17]u8`");
typedef struct { uint8_t f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; } gm_tbngattl1fo51;
_Static_assert(sizeof(gm_tcc1rq4n1avb2) == 8 && _Alignof(gm_tcc1rq4n1avb2) == 4 && offsetof(gm_tcc1rq4n1avb2, f0) == 0 && offsetof(gm_tcc1rq4n1avb2, f1) == 4, "layout of `LineCol`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tcmkompbl09il;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; gm_t88rcucg1b7uk f2; } gm_tcmtrmhj7ak4u;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; uint64_t f1; } gm_tfbnciphf3261;
typedef struct { gm_ttovhfee8mf28 f0; uint8_t f1; uint8_t pe[3]; } gm_tg5lql6itufk7;
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; } gm_tgglga8ro3isc;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
_Static_assert(sizeof(gm_tjcj4j2gbh6c8) == 4 && _Alignof(gm_tjcj4j2gbh6c8) == 2 && offsetof(gm_tjcj4j2gbh6c8, f0) == 0 && offsetof(gm_tjcj4j2gbh6c8, f1) == 2, "layout of `(signed: bool, bits: u16)`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t f1; uint8_t pe[3]; } gm_tkn42u7na3t81;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_tmn7n0n9raou5) == 8 && _Alignof(gm_tmn7n0n9raou5) == 4 && offsetof(gm_tmn7n0n9raou5, f0) == 0 && offsetof(gm_tmn7n0n9raou5, f1) == 4, "layout of `Coercion`");
_Static_assert(sizeof(gm_tn68el8d14vku) == 16 && _Alignof(gm_tn68el8d14vku) == 8 && offsetof(gm_tn68el8d14vku, f0) == 0 && offsetof(gm_tn68el8d14vku, f1) == 8, "layout of `(i: usize, x: u32)`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t88rcucg1b7uk v0; } f1; } gm_tnnjeerolos0a;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
_Static_assert(sizeof(gm_tpl3q6e8s9qo1) == 16 && _Alignof(gm_tpl3q6e8s9qo1) == 16 && offsetof(gm_tpl3q6e8s9qo1, f0) == 0, "layout of `(i128,)`");
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
_Static_assert(sizeof(gm_tsn2omssg6cpf) == 16 && _Alignof(gm_tsn2omssg6cpf) == 16 && offsetof(gm_tsn2omssg6cpf, f0) == 0, "layout of `(u128,)`");
typedef struct { gm_t86h6hhm6d76n f0; uint8_t f1; uint8_t pe[15]; } gm_tsp9ul70rnkg6;
_Static_assert(sizeof(gm_ttovhfee8mf28) == 8 && _Alignof(gm_ttovhfee8mf28) == 4 && offsetof(gm_ttovhfee8mf28, f0) == 0 && offsetof(gm_ttovhfee8mf28, f1) == 4, "layout of `(signed: bool, bits: u32)`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_tcc1rq4n1avb2 v3; uint32_t v4; uint32_t v5; } f1; } gm_tvdg94bk7aa22;
_Static_assert(sizeof(gm_t2jeg3bg7sfqa) == 40 && _Alignof(gm_t2jeg3bg7sfqa) == 8 && offsetof(gm_t2jeg3bg7sfqa, f0) == 0 && offsetof(gm_t2jeg3bg7sfqa, f1) == 8 && offsetof(gm_t2jeg3bg7sfqa, f2) == 16 && offsetof(gm_t2jeg3bg7sfqa, f3) == 24, "layout of `Seq`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tcmkompbl09il v0; gm_tiilmfd16v3fc v1; gm_b16_8 v2; } f1; } gm_t428ue2u3hf5g;
typedef struct { gm_tcmkompbl09il f0; uint8_t f1; uint8_t pe[7]; } gm_t57euh612ligr;
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_t76123gfet73g) == 24 && _Alignof(gm_t76123gfet73g) == 8 && offsetof(gm_t76123gfet73g, f0) == 0 && offsetof(gm_t76123gfet73g, f1) == 8, "layout of `Flow`");
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; gm_tnnjeerolos0a f2; uint8_t pe[4]; } gm_t8nckeqfenl8a;
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
typedef struct { uint8_t f0; uint8_t p1[7]; gm_trqfvmjtt4sij f1; } gm_tbci5ospk5nnn;
_Static_assert(sizeof(gm_tbngattl1fo51) == 8 && _Alignof(gm_tbngattl1fo51) == 4 && offsetof(gm_tbngattl1fo51, f0) == 0 && offsetof(gm_tbngattl1fo51, f1) == 4, "layout of `(is_const: bool, pointee: Ty)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t76123gfet73g v0; } f1; } gm_tcfa6l7reoosn;
_Static_assert(sizeof(gm_tcmkompbl09il) == 24 && _Alignof(gm_tcmkompbl09il) == 8 && offsetof(gm_tcmkompbl09il, f0) == 0 && offsetof(gm_tcmkompbl09il, f1) == 8, "layout of `(ty: Ty, bytes: string)`");
_Static_assert(sizeof(gm_tcmtrmhj7ak4u) == 12 && _Alignof(gm_tcmtrmhj7ak4u) == 4 && offsetof(gm_tcmtrmhj7ak4u, f0) == 0 && offsetof(gm_tcmtrmhj7ak4u, f1) == 4 && offsetof(gm_tcmtrmhj7ak4u, f2) == 8, "layout of `(op: LenOp, lhs: Ty, rhs: Ty)`");
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t p2[4]; gm_tnpifv20irnph f2; uint8_t f3; uint8_t pe[7]; } gm_tddtjfggdat0d;
_Static_assert(sizeof(gm_tfbnciphf3261) == 16 && _Alignof(gm_tfbnciphf3261) == 8 && offsetof(gm_tfbnciphf3261, f0) == 0 && offsetof(gm_tfbnciphf3261, f1) == 8, "layout of `(ty: Ty, off: u64)`");
_Static_assert(sizeof(gm_tg5lql6itufk7) == 12 && _Alignof(gm_tg5lql6itufk7) == 4 && offsetof(gm_tg5lql6itufk7, f0) == 0 && offsetof(gm_tg5lql6itufk7, f1) == 8, "layout of `?(signed: bool, bits: u32)`");
_Static_assert(sizeof(gm_tgglga8ro3isc) == 8 && _Alignof(gm_tgglga8ro3isc) == 4 && offsetof(gm_tgglga8ro3isc, f0) == 0 && offsetof(gm_tgglga8ro3isc, f1) == 4, "layout of `(err: Ty, ok: Ty)`");
typedef struct { gm_tvdg94bk7aa22 f0; } gm_th9nl3oso747e;
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
_Static_assert(sizeof(gm_tkn42u7na3t81) == 8 && _Alignof(gm_tkn42u7na3t81) == 4 && offsetof(gm_tkn42u7na3t81, f0) == 0 && offsetof(gm_tkn42u7na3t81, f1) == 4, "layout of `?Ty`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t76123gfet73g v0; gm_t2jeg3bg7sfqa v1; } f1; } gm_tmc91gv7n9c0i;
_Static_assert(sizeof(gm_tnnjeerolos0a) == 8 && _Alignof(gm_tnnjeerolos0a) == 4 && offsetof(gm_tnnjeerolos0a, f0) == 0 && offsetof(gm_tnnjeerolos0a, f1) == 4, "layout of `ArrayKind`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t p2[7]; gm_tf5srh4mp4b3i f2; gm_tkn42u7na3t81 f3; } gm_tpautr4br7h3l;
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_tsp9ul70rnkg6) == 48 && _Alignof(gm_tsp9ul70rnkg6) == 16 && offsetof(gm_tsp9ul70rnkg6, f0) == 0 && offsetof(gm_tsp9ul70rnkg6, f1) == 32, "layout of `?NumVal`");
_Static_assert(sizeof(gm_tvdg94bk7aa22) == 12 && _Alignof(gm_tvdg94bk7aa22) == 4 && offsetof(gm_tvdg94bk7aa22, f0) == 0 && offsetof(gm_tvdg94bk7aa22, f1) == 4, "layout of `Builtin`");
_Static_assert(sizeof(gm_t428ue2u3hf5g) == 32 && _Alignof(gm_t428ue2u3hf5g) == 8 && offsetof(gm_t428ue2u3hf5g, f0) == 0 && offsetof(gm_t428ue2u3hf5g, f1) == 8, "layout of `ConstKey`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tjcj4j2gbh6c8 v0; uint16_t v1; gm_tbngattl1fo51 v2; gm_tbngattl1fo51 v3; gm_tbngattl1fo51 v4; gm_t8nckeqfenl8a v5; gm_t6j6smj7qoctu v6; gm_t88rcucg1b7uk v7; gm_tgglga8ro3isc v8; gm_tddtjfggdat0d v9; gm_tddtjfggdat0d v10; gm_tiilmfd16v3fc v11; gm_t88rcucg1b7uk v12; uint32_t v13; gm_tgglga8ro3isc v14; gm_t428ue2u3hf5g v15; gm_tcmtrmhj7ak4u v16; uint32_t v17; gm_tnpifv20irnph v18; uint32_t v19; } f1; } gm_t53at80pfdus3;
_Static_assert(sizeof(gm_t57euh612ligr) == 32 && _Alignof(gm_t57euh612ligr) == 8 && offsetof(gm_t57euh612ligr, f0) == 0 && offsetof(gm_t57euh612ligr, f1) == 24, "layout of `?BitsLayout`");
_Static_assert(sizeof(gm_t8nckeqfenl8a) == 24 && _Alignof(gm_t8nckeqfenl8a) == 8 && offsetof(gm_t8nckeqfenl8a, f0) == 0 && offsetof(gm_t8nckeqfenl8a, f1) == 8 && offsetof(gm_t8nckeqfenl8a, f2) == 12, "layout of `(len: u64, elem: Ty, kind: ArrayKind)`");
_Static_assert(sizeof(gm_tbci5ospk5nnn) == 48 && _Alignof(gm_tbci5ospk5nnn) == 8 && offsetof(gm_tbci5ospk5nnn, f0) == 0 && offsetof(gm_tbci5ospk5nnn, f1) == 8, "layout of `Int`");
_Static_assert(sizeof(gm_tcfa6l7reoosn) == 32 && _Alignof(gm_tcfa6l7reoosn) == 8 && offsetof(gm_tcfa6l7reoosn, f0) == 0 && offsetof(gm_tcfa6l7reoosn, f1) == 8, "layout of `Flow!()`");
typedef struct { gm_b16_8 f0; gm_tkn42u7na3t81 f1; gm_tbci5ospk5nnn f2; } gm_tckdobunaulee;
_Static_assert(sizeof(gm_tddtjfggdat0d) == 48 && _Alignof(gm_tddtjfggdat0d) == 8 && offsetof(gm_tddtjfggdat0d, f0) == 0 && offsetof(gm_tddtjfggdat0d, f1) == 16 && offsetof(gm_tddtjfggdat0d, f2) == 24 && offsetof(gm_tddtjfggdat0d, f3) == 40, "layout of `FnTy`");
_Static_assert(sizeof(gm_th9nl3oso747e) == 12 && _Alignof(gm_th9nl3oso747e) == 4 && offsetof(gm_th9nl3oso747e, f0) == 0, "layout of `(Builtin,)`");
_Static_assert(sizeof(gm_tmc91gv7n9c0i) == 48 && _Alignof(gm_tmc91gv7n9c0i) == 8 && offsetof(gm_tmc91gv7n9c0i, f0) == 0 && offsetof(gm_tmc91gv7n9c0i, f1) == 8, "layout of `Flow!Seq`");
_Static_assert(sizeof(gm_tpautr4br7h3l) == 48 && _Alignof(gm_tpautr4br7h3l) == 8 && offsetof(gm_tpautr4br7h3l, f0) == 0 && offsetof(gm_tpautr4br7h3l, f1) == 16 && offsetof(gm_tpautr4br7h3l, f2) == 24 && offsetof(gm_tpautr4br7h3l, f3) == 40, "layout of `StructDef`");
typedef struct { uint64_t f0; gm_tckdobunaulee f1; } gm_t3l8idb2m6c1r;
_Static_assert(sizeof(gm_t53at80pfdus3) == 56 && _Alignof(gm_t53at80pfdus3) == 8 && offsetof(gm_t53at80pfdus3, f0) == 0 && offsetof(gm_t53at80pfdus3, f1) == 8, "layout of `TyKind`");
_Static_assert(sizeof(gm_tckdobunaulee) == 72 && _Alignof(gm_tckdobunaulee) == 8 && offsetof(gm_tckdobunaulee, f0) == 0 && offsetof(gm_tckdobunaulee, f1) == 16 && offsetof(gm_tckdobunaulee, f2) == 24, "layout of `VariantDef`");
_Static_assert(sizeof(gm_t3l8idb2m6c1r) == 80 && _Alignof(gm_t3l8idb2m6c1r) == 8 && offsetof(gm_t3l8idb2m6c1r, f0) == 0 && offsetof(gm_t3l8idb2m6c1r, f1) == 8, "layout of `(i: usize, x: VariantDef)`");
struct gm_fr_apply_builtin_41dffti {
    gm_frame h;
    gm_tckdobunaulee l32; /* d */
    gm_tcfa6l7reoosn ret;
};
struct gm_fr_array_ptr_to_slice_7dg3m1v {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_coerce_bytes_9kf3u24 {
    gm_frame h;
    gm_tcfa6l7reoosn ret;
};
struct gm_fr_convert_is6an2k {
    gm_frame h;
    gm_tcfa6l7reoosn ret;
};
struct gm_fr_copy_from_f7j6p05 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fill_abmf5g5 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_hash_int_26mf5j0 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_hash_mix_1abfg0l {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_hash_u64_32bripn {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_hash_u64_byte_nd76h12 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_hash_val_nr2e99r {
    gm_frame h;
    gm_t2jeg3bg7sfqa l17; /* s */
    gm_tpautr4br7h3l l31; /* def */
    gm_tcfa6l7reoosn ret;
};
struct gm_fr_int_text_vrjnuu2 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_mark_kpppm1j {
    gm_frame h;
    gm_t2lqu8jsj511d ret;
};
struct gm_fr_ptr_align_lavuopj {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_release_jn5kqdi {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_tmp_8k6u9c9 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_us_cuv01fp {
    gm_frame h;
    uint64_t ret;
};
__attribute__((aligned(4))) void * gm_f_adt_of_8k5vqud(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) uint64_t gm_f_align_15fjgq1(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_apply_builtin_41dffti(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_tvdg94bk7aa22 a2, gm_t88rcucg1b7uk a3, gm_t88rcucg1b7uk a4, gm_t88rcucg1b7uk a5, gm_b16_8 a6, gm_b16_8 a7);
__attribute__((aligned(4))) gm_b0_1 gm_f_array_ptr_to_slice_7dg3m1v(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, gm_b16_8 a3);
__attribute__((aligned(4))) unsigned __int128 gm_f_bits128_jigkui4(const void * a0);
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_bits_struct_ty_22ue20s(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void gm_f_checked_overflow_ma1bm1g(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_coerce_bytes_9kf3u24(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_tmn7n0n9raou5 a2, gm_t88rcucg1b7uk a3, gm_t88rcucg1b7uk a4, gm_b16_8 a5, gm_b16_8 a6);
__attribute__((aligned(4))) void gm_f_convert_is6an2k(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_tvdg94bk7aa22 a2, gm_t88rcucg1b7uk a3, gm_t88rcucg1b7uk a4, gm_b16_8 a5, gm_b16_8 a6);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_copy_from_f7j6p05(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
__attribute__((aligned(4))) uint8_t gm_f_fast_23em7sv(void * a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_field_bytes_bhivr4s(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, uint32_t a3);
__attribute__((aligned(4))) gm_tfbnciphf3261 gm_f_field_layout_h34salb(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, uint32_t a2);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_fill_abmf5g5(gm_b16_8 a0, uint8_t a1);
__attribute__((aligned(4))) void gm_f_format_8k5eh95(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_985clkf(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_th9nl3oso747e a0);
__attribute__((aligned(4))) void gm_f_format_9qvncot(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tpl3q6e8s9qo1 a0);
__attribute__((aligned(4))) void gm_f_format_fv5tfj6(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_h8e7ps1(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_kj15nqp(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_l7c15sv(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t2lqu8jsj511d a0);
__attribute__((aligned(4))) void gm_f_format_pe0ujmq(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_ra7pont(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tsn2omssg6cpf a0);
__attribute__((aligned(4))) void gm_f_format_ven7kt5(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
__attribute__((aligned(4))) double gm_f_get_float_grmhlm4(void * a0, gm_csguankko4lmhd a1, gm_b16_8 a2);
__attribute__((aligned(4))) unsigned __int128 gm_f_get_int_pg7p4gc(void * a0, gm_csguankko4lmhd a1, gm_b16_8 a2);
__attribute__((aligned(4))) unsigned __int128 gm_f_get_uint_bq8c4b7(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_hash_int_26mf5j0(void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_hash_int_26mf5j0_c(void * a0, gm_csia7oevr2u0fk a1);
__attribute__((aligned(4))) void gm_f_hash_mix_1abfg0l(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_hash_u64_32bripn(void * a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_hash_u64_byte_nd76h12(void * a0, uint8_t a1);
__attribute__((aligned(4))) void gm_f_hash_val_nr2e99r(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3, void * a4);
__attribute__((aligned(4))) int32_t gm_f_int_cmp_kb3sm6u(uint8_t a0, unsigned __int128 a1, unsigned __int128 a2);
__attribute__((aligned(4))) gm_tg5lql6itufk7 gm_f_int_info_ulcevdv(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_int_text_vrjnuu2(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_invalid_tag_1gif354(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3);
__attribute__((aligned(4))) void gm_f_kind_htv6093(void *gm_out, void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_make_res_085nq6f(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, uint8_t a3, gm_b16_8 a4);
__attribute__((aligned(4))) gm_b0_1 gm_f_make_some_tvb9ubk(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, gm_b16_8 a3);
__attribute__((aligned(4))) gm_b0_1 gm_f_make_variant_bn3d74n(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, uint32_t a3, gm_b16_8 a4);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_csc7lc6jlkqkuq gm_f_mark_kpppm1j(void * a0);
__attribute__((aligned(4))) uint8_t gm_f_nullable_pointer_7qfmn6o(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) unsigned __int128 gm_f_num_bits_onl28vp(const void * a0);
__attribute__((aligned(4))) unsigned __int128 gm_f_num_ext_348kmbu(uint8_t a0, uint32_t a1, unsigned __int128 a2);
__attribute__((aligned(4))) uint8_t gm_f_num_fits_1n7pcsh(const void * a0, uint8_t a1, uint32_t a2);
__attribute__((aligned(4))) void gm_f_num_float_int_ba5kc1t(void *gm_out, double a0, gm_tvdg94bk7aa22 a1);
__attribute__((aligned(4))) double gm_f_num_fmax_0qau2jp(double a0, double a1);
__attribute__((aligned(4))) double gm_f_num_fmin_1k5uqp0(double a0, double a1);
__attribute__((aligned(4))) double gm_f_num_int_to_float_4hrvis6(const void * a0, uint16_t a1);
__attribute__((aligned(4))) uint8_t gm_f_num_integral_qbts4a6(double a0);
__attribute__((aligned(4))) uint8_t gm_f_num_is_nan_b0ma7mo(double a0);
__attribute__((aligned(4))) unsigned __int128 gm_f_num_mask_do3naah(uint32_t a0);
__attribute__((aligned(4))) double gm_f_num_round_2pna7m3(double a0, uint16_t a1);
__attribute__((aligned(4))) void gm_f_num_sat_66dqcpl(void *gm_out, const void * a0, uint8_t a1, uint32_t a2);
__attribute__((aligned(4))) void gm_f_num_val_i3upeur(void *gm_out, uint8_t a0, unsigned __int128 a1);
__attribute__((aligned(4))) gm_csguankko4lmhd gm_f_opt_inner_q4ga84u(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_opt_payload_1pbnobb(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
__attribute__((aligned(4))) uint8_t gm_f_opt_present_lb4i2ip(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
__attribute__((aligned(4))) uint64_t gm_f_payload_offset_la0c8ft(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_pow_7je2uvi(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t9qkh0rsooc8d a2, gm_t88rcucg1b7uk a3, gm_t88rcucg1b7uk a4, gm_t88rcucg1b7uk a5, gm_b16_8 a6, gm_b16_8 a7, gm_b16_8 a8);
__attribute__((aligned(4))) uint64_t gm_f_ptr_align_lavuopj(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) uint64_t gm_f_ptr_of_b77b4c9(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_put_float_fiad72d(void * a0, gm_csguankko4lmhd a1, double a2, gm_b16_8 a3);
__attribute__((aligned(4))) void gm_f_put_slice_ko14loi(void * a0, gm_b16_8 a1, uint64_t a2, uint64_t a3);
__attribute__((aligned(4))) void gm_f_put_uint_cddin99(void * a0, gm_b16_8 a1, unsigned __int128 a2);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_release_jn5kqdi(void * a0, gm_csc7lc6jlkqkuq a1);
__attribute__((aligned(4))) uint8_t gm_f_res_is_err_8jbgndp(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b16_8 gm_f_res_payload_pb2hcmh(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tgglga8ro3isc gm_f_res_types_pg7rvck(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void gm_f_seq_of_ifahtjs(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3);
__attribute__((aligned(4))) gm_b16_8 gm_f_show_8uijkbs(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) uint64_t gm_f_size_hr4b76n(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_b16_8 gm_f_tmp_8k6u9c9(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
__attribute__((aligned(4))) __attribute__((noinline)) gm_b16_8 gm_f_tmp_next_dalgo1f(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_trap_qh84l9d(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_union_def_h8mvf4t(void *gm_out, void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void gm_f_unsupported_70vvk1e(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) __attribute__((always_inline)) inline uint64_t gm_f_us_cuv01fp(uint64_t a0);
__attribute__((aligned(4))) gm_t9805qtonbsag gm_f_variant_index_s2p6fs4(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_vector_builtin_3fn933h(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_tvdg94bk7aa22 a2, gm_t88rcucg1b7uk a3, gm_t88rcucg1b7uk a4, gm_t88rcucg1b7uk a5, gm_b16_8 a6, gm_b16_8 a7);
extern const uint8_t gm_s064htvgs8b59[69];
extern const uint8_t gm_s26c80h2pm6a7[26];
extern const uint8_t gm_s3ergh2il1tjf[43];
extern const uint8_t gm_s3gnqv0up712m[61];
extern const uint8_t gm_s9h8lp3gkedth[29];
extern const uint8_t gm_s9o2c36nosv54[41];
extern const uint8_t gm_sartclcojin4f[52];
extern const uint8_t gm_sb9352gc5i144[21];
extern const uint8_t gm_sbel6qookv0gl[54];
extern const uint8_t gm_sbjj41cb5lph3[60];
extern const uint8_t gm_sbl3pbs9195ho[4];
extern const uint8_t gm_sd4ibvv64si9n[45];
extern const uint8_t gm_sge745dj3cnrb[73];
extern const uint8_t gm_sh01rb1u2jpfs[19];
extern const uint8_t gm_spqf88ftq0gqk[29];
extern const uint8_t gm_sqofr58g480ri[39];
extern const uint8_t gm_sr4qbrk6tlgaa[1];
extern const char gm_srcc24pnihhgkug[];
extern const char gm_srcl8pn2pjtmj6k[];
extern const char gm_srcpit8mdn0781q[];
extern const uint8_t gm_sti4tkgslrkgo[26];
extern const uint8_t gm_stk678hojgu6q[24];
extern const gm_type gm_tyo86ks9qkvt;
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
            gm_b16_8 t6 = t5;
            gm_buf mb = {0}; size_t ml = GM_LD(uintptr_t, ((uint8_t *)&t6) + 8); const uint8_t *mp = GM_LD(void *, ((uint8_t *)&t6)); if (ml == 16 && memcmp(mp, "assertion failed", 16) == 0) gm_buf_push(&mb, mp, ml); else { gm_buf_push(&mb, "assertion failed: ", 18); gm_buf_push(&mb, mp, ml); } gm_trap_bytes(mb.data, mb.len, gm_srcc24pnihhgkug, 24, 5);
        }
    }
    {
        gm_b16_8 t7 = gm_l0;
        gm_b16_8 t8 = t7;
        gm_b16_8 t9 = gm_l1;
        gm_b16_8 t10 = t9;
        if (GM_LD(uintptr_t, ((uint8_t *)&t8) + 8) != 0) memmove(GM_LD(void *, ((uint8_t *)&t8)), GM_LD(void *, ((uint8_t *)&t10)), GM_LD(uintptr_t, ((uint8_t *)&t8) + 8) * 1);
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
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
                    uint64_t t12 = t11;
                    uint64_t t15 = GM_LD(uint64_t, ((uint8_t *)&t12));
                    gm_l5 = t15;
                    {
                        uint64_t t17 = gm_l5;
                        uint64_t t18 = (uintptr_t)t17;
                        void * t19 = gm_l2;
                        uint8_t t20 = gm_l1;
                        GM_ST(uint8_t, ((uint8_t *)t19 + t18 * 1), t20);
                    }
                    goto me13;
mn14:;
                    gm_trap("no match arm matched the scrutinee", gm_srcc24pnihhgkug, 18, 5);
me13:;
                }
lc5:;
                {
                    uint64_t t22 = gm_l4;
                    uint64_t r23; bool o24 = __builtin_add_overflow(t22, ((uint64_t)1ULL), &r23);
                    uint64_t t25 = r23;
                    gm_l4 = t25;
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_csc7lc6jlkqkuq gm_f_mark_kpppm1j(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_mark_kpppm1j F;
    void * gm_l0; /* m */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    uint64_t t2 = GM_LD(uint64_t, ((((uint8_t *)t1) + 360) + 40));
    void * t3 = gm_l0;
    uint64_t t4 = GM_LD(uint64_t, ((((uint8_t *)t3) + 360) + 48));
    gm_t2lqu8jsj511d t5;
    GM_ST(uint64_t, ((uint8_t *)&t5 + 0), t2);
    GM_ST(uint64_t, ((uint8_t *)&t5 + 8), t4);
    F.ret = t5;
gm_fret:
    return GM_CONV(gm_csc7lc6jlkqkuq, F.ret);
gm_fprop:
    return GM_CONV(gm_csc7lc6jlkqkuq, F.ret);
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_release_jn5kqdi(void * a0, gm_csc7lc6jlkqkuq a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_release_jn5kqdi F;
    void * gm_l0; /* m */
    gm_t2lqu8jsj511d gm_l1; /* mk */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t2lqu8jsj511d, a1);
    {
        void * t2 = gm_l0;
        uint64_t t3 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 0));
        GM_ST(uint64_t, ((((uint8_t *)t2) + 360) + 40), t3);
    }
    {
        void * t4 = gm_l0;
        uint64_t t5 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
        GM_ST(uint64_t, ((((uint8_t *)t4) + 360) + 48), t5);
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
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
        if (t6 >= (uintptr_t)(t8)) gm_trap_index(t6, t8, gm_srcpit8mdn0781q, 381, 14);
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
        if (o16) gm_trap("integer overflow in `+`", gm_srcpit8mdn0781q, 385, 14);
        uint64_t t17 = r15;
        uint64_t r18; bool o19 = __builtin_add_overflow(t17, ((uint64_t)7ULL), &r18);
        if (o19) gm_trap("integer overflow in `+`", gm_srcpit8mdn0781q, 385, 14);
        uint64_t t20 = r18;
        uint64_t t21 = gm_f_us_cuv01fp(((uint64_t)7ULL));
        uint64_t t22 = (uint64_t)~t21;
        uint64_t t23 = (uint64_t)(t20 & t22);
        uint64_t t24 = gm_l4;
        uint64_t r25; bool o26 = __builtin_sub_overflow(t23, t24, &r25);
        if (o26) gm_trap("integer overflow in `-`", gm_srcpit8mdn0781q, 385, 12);
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
            if (o37) gm_trap("integer overflow in `+`", gm_srcpit8mdn0781q, 387, 18);
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
        if (o53) gm_trap("integer overflow in `+`", gm_srcpit8mdn0781q, 388, 21);
        uint64_t t54 = r52;
        uint64_t t55 = (uintptr_t)t54;
        if (t49 > t55 || t55 > (uintptr_t)t46) gm_trap_slice(t49, t55, t46, gm_srcpit8mdn0781q, 388, 10);
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
        if (o61) gm_trap("integer overflow in `+`", gm_srcpit8mdn0781q, 389, 14);
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
__attribute__((aligned(4))) void gm_f_apply_builtin_41dffti(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_tvdg94bk7aa22 a2, gm_t88rcucg1b7uk a3, gm_t88rcucg1b7uk a4, gm_t88rcucg1b7uk a5, gm_b16_8 a6, gm_b16_8 a7) {
    struct gm_fr_apply_builtin_41dffti F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_tvdg94bk7aa22 gm_l2; /* op */
    gm_t88rcucg1b7uk gm_l3; /* res_ty */
    gm_t88rcucg1b7uk gm_l4; /* t0 */
    gm_t88rcucg1b7uk gm_l5; /* t1 */
    gm_b16_8 gm_l6; /* v */
    gm_b16_8 gm_l7; /* dst */
    uint64_t gm_l8; /* n */
    uint64_t gm_l9; /* addr */
    uint64_t gm_l10; /* al */
    gm_t88rcucg1b7uk gm_l11; /* elem */
    __int128 gm_l12; /* stride */
    __int128 gm_l13; /* p */
    uint64_t gm_l14; /* pbits */
    uint64_t gm_l15; /* mask */
    int64_t gm_l16; /* d */
    int64_t gm_l17; /* q */
    gm_ttovhfee8mf28 gm_l18; /* info */
    unsigned __int128 gm_l19; /* raw */
    __int128 gm_l20; /* n */
    __int128 gm_l21; /* off */
    __int128 gm_l22; /* r */
    gm_tcmkompbl09il gm_l23; /* u */
    uint64_t gm_l24; /* n */
    gm_t88rcucg1b7uk gm_l25; /* u */
    gm_tcmkompbl09il gm_l26; /* def */
    gm_ttovhfee8mf28 gm_l27; /* info */
    gm_t86h6hhm6d76n gm_l28; /* x */
    gm_b16_8 gm_l29; /* <tmp> */
    uint64_t gm_l30; /* <tmp> */
    uint64_t gm_l31; /* i */
    unsigned __int128 gm_l33; /* tb */
    unsigned __int128 gm_l34; /* mag */
    gm_t2lqu8jsj511d gm_l35; /* mk */
    gm_b16_8 gm_l36; /* vb */
    double gm_l37; /* x */
    double gm_l38; /* y */
    double gm_l39; /* r */
    gm_ttovhfee8mf28 gm_l40; /* info */
    unsigned __int128 gm_l41; /* x */
    unsigned __int128 gm_l42; /* y */
    uint64_t gm_l43; /* pick */
    unsigned __int128 gm_l44; /* hi */
    gm_ttovhfee8mf28 gm_l45; /* info */
    unsigned __int128 gm_l46; /* x */
    unsigned __int128 gm_l47; /* r */
    gm_ttovhfee8mf28 gm_l48; /* info */
    unsigned __int128 gm_l49; /* x */
    unsigned __int128 gm_l50; /* y */
    unsigned __int128 gm_l51; /* d */
    gm_ttovhfee8mf28 gm_l52; /* r */
    gm_t9qkh0rsooc8d gm_l53; /* fl */
    double gm_l54; /* x */
    uint64_t gm_l55; /* raw */
    uint64_t gm_l56; /* exp */
    uint16_t gm_l57; /* w */
    double gm_l58; /* x */
    double gm_l59; /* y */
    double gm_l60; /* z */
    gm_ttovhfee8mf28 gm_l61; /* info */
    gm_ttovhfee8mf28 gm_l62; /* ainfo */
    uint32_t gm_l63; /* n */
    unsigned __int128 gm_l64; /* x */
    unsigned __int128 gm_l65; /* araw */
    uint32_t gm_l66; /* k */
    unsigned __int128 gm_l67; /* r */
    gm_ttovhfee8mf28 gm_l68; /* info */
    uint32_t gm_l69; /* n */
    unsigned __int128 gm_l70; /* x */
    unsigned __int128 gm_l71; /* r */
    uint32_t gm_l72; /* <tmp> */
    uint32_t gm_l73; /* <tmp> */
    uint32_t gm_l74; /* i */
    uint32_t gm_l75; /* <tmp> */
    uint32_t gm_l76; /* <tmp> */
    uint32_t gm_l77; /* i */
    gm_ttovhfee8mf28 gm_l78; /* info */
    gm_t86h6hhm6d76n gm_l79; /* seed */
    uint64_t gm_l80; /* h */
    GM_STACK_CHECK(&F, 13270u);
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
    gm_l7 = a7;
    {
        {
            uint8_t t23 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            switch (t23) { case ((uint8_t)0ULL): goto ma4; case ((uint8_t)1ULL): goto ma4; case ((uint8_t)2ULL): goto ma4; case ((uint8_t)3ULL): goto ma4; case ((uint8_t)4ULL): goto ma4; case ((uint8_t)5ULL): goto ma4; case ((uint8_t)6ULL): goto ma4; case ((uint8_t)7ULL): goto ma5; case ((uint8_t)8ULL): goto ma6; case ((uint8_t)9ULL): goto ma7; case ((uint8_t)10ULL): goto ma8; case ((uint8_t)11ULL): goto ma8; case ((uint8_t)12ULL): goto ma8; case ((uint8_t)13ULL): goto ma7; case ((uint8_t)14ULL): goto ma9; case ((uint8_t)15ULL): goto ma10; case ((uint8_t)16ULL): goto ma11; case ((uint8_t)17ULL): goto ma11; case ((uint8_t)18ULL): goto ma11; case ((uint8_t)19ULL): goto ma12; case ((uint8_t)20ULL): goto ma13; case ((uint8_t)21ULL): goto ma14; case ((uint8_t)22ULL): goto ma15; case ((uint8_t)23ULL): goto ma15; case ((uint8_t)24ULL): goto ma15; case ((uint8_t)25ULL): goto ma15; case ((uint8_t)26ULL): goto ma15; case ((uint8_t)27ULL): goto ma16; case ((uint8_t)28ULL): goto ma18; case ((uint8_t)29ULL): goto ma18; case ((uint8_t)30ULL): goto ma18; case ((uint8_t)31ULL): goto ma18; case ((uint8_t)32ULL): goto ma17; case ((uint8_t)33ULL): goto ma17; case ((uint8_t)34ULL): goto ma18; case ((uint8_t)35ULL): goto ma7; case ((uint8_t)36ULL): goto ma21; case ((uint8_t)37ULL): goto ma21; case ((uint8_t)38ULL): goto ma21; case ((uint8_t)39ULL): goto ma21; case ((uint8_t)40ULL): goto ma20; case ((uint8_t)41ULL): goto ma20; case ((uint8_t)42ULL): goto ma20; case ((uint8_t)43ULL): goto ma20; case ((uint8_t)44ULL): goto ma20; case ((uint8_t)45ULL): goto ma20; case ((uint8_t)46ULL): goto ma20; case ((uint8_t)47ULL): goto ma20; case ((uint8_t)48ULL): goto ma20; case ((uint8_t)49ULL): goto ma20; case ((uint8_t)50ULL): goto ma20; case ((uint8_t)51ULL): goto ma20; case ((uint8_t)52ULL): goto ma20; case ((uint8_t)53ULL): goto ma20; case ((uint8_t)54ULL): goto ma20; case ((uint8_t)55ULL): goto ma20; case ((uint8_t)56ULL): goto ma20; case ((uint8_t)57ULL): goto ma20; case ((uint8_t)58ULL): goto ma21; case ((uint8_t)59ULL): goto ma21; case ((uint8_t)60ULL): goto ma21; case ((uint8_t)61ULL): goto ma21; case ((uint8_t)62ULL): goto ma19; case ((uint8_t)63ULL): goto ma21; case ((uint8_t)64ULL): goto ma21; case ((uint8_t)65ULL): goto ma21; case ((uint8_t)66ULL): goto ma21; case ((uint8_t)67ULL): goto ma21; case ((uint8_t)68ULL): goto ma21; case ((uint8_t)69ULL): goto ma21; case ((uint8_t)70ULL): goto ma21; case ((uint8_t)71ULL): goto ma21; case ((uint8_t)72ULL): goto ma21; case ((uint8_t)73ULL): goto ma21; case ((uint8_t)74ULL): goto ma21; case ((uint8_t)75ULL): goto ma21; case ((uint8_t)76ULL): goto ma21; case ((uint8_t)77ULL): goto ma21; case ((uint8_t)78ULL): goto ma21; case ((uint8_t)79ULL): goto ma21; case ((uint8_t)80ULL): goto ma21; case ((uint8_t)81ULL): goto ma21; default: goto ma21; }
ma4:;
            uint8_t t27 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t27 != ((uint8_t)0ULL)) goto pa26;
            goto po25;
pa26:;
            uint8_t t29 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t29 != ((uint8_t)1ULL)) goto pa28;
            goto po25;
pa28:;
            uint8_t t31 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t31 != ((uint8_t)2ULL)) goto pa30;
            goto po25;
pa30:;
            uint8_t t33 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t33 != ((uint8_t)3ULL)) goto pa32;
            goto po25;
pa32:;
            uint8_t t35 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t35 != ((uint8_t)4ULL)) goto pa34;
            goto po25;
pa34:;
            uint8_t t37 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t37 != ((uint8_t)5ULL)) goto pa36;
            goto po25;
pa36:;
            uint8_t t39 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t39 != ((uint8_t)6ULL)) goto pa38;
            goto po25;
pa38:;
            goto mn24;
po25:;
            void * t40 = gm_l0;
            gm_tl874t2atnlh3 t41 = gm_l1;
            gm_tvdg94bk7aa22 t42 = gm_l2;
            gm_t88rcucg1b7uk t43 = gm_l4;
            gm_t88rcucg1b7uk t44 = gm_l3;
            uint64_t t45 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t46 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
            if (t45 >= (uintptr_t)(t46)) gm_trap_index(t45, t46, gm_srcl8pn2pjtmj6k, 192, 107);
            gm_b16_8 t47 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t45 * 16));
            gm_b16_8 t48 = t47;
            gm_b16_8 t49 = GM_LD(gm_b16_8, ((uint8_t *)&t48));
            gm_b16_8 t50 = gm_l7;
            gm_f_convert_is6an2k((void *)&(*(gm_tcfa6l7reoosn *)gm_out), (void *)NULL, cx, NULL, t40, t41, t42, t43, t44, t49, t50);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
            goto me3;
mn24:;
ma5:;
            uint8_t t52 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t52 != ((uint8_t)7ULL)) goto mn51;
            {
                gm_b16_8 t54 = gm_l7;
                gm_f_fill_abmf5g5(t54, ((uint8_t)0ULL));
            }
            {
                uint64_t t55 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                uint64_t t56 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t57 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                if (t56 >= (uintptr_t)(t57)) gm_trap_index(t56, t57, gm_srcl8pn2pjtmj6k, 195, 30);
                uint64_t t58 = GM_LD(uintptr_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t56 * 16) + 8);
                uint64_t t59 = (t55 < t58 ? t55 : t58);
                gm_l8 = t59;
            }
            {
                uint64_t t60 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                void * t61 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l7)));
                uint64_t t62 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t63 = gm_l8;
                uint64_t t64 = (uintptr_t)t63;
                if (t62 > t64 || t64 > (uintptr_t)t60) gm_trap_slice(t62, t64, t60, gm_srcl8pn2pjtmj6k, 196, 13);
                gm_b16_8 t65;
                GM_ST(void *, (uint8_t *)&t65, (void *)(((uint8_t *)t61 + t62 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t65 + 8, (uintptr_t)(t64 - t62));
                uint64_t t66 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t67 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                if (t66 >= (uintptr_t)(t67)) gm_trap_index(t66, t67, gm_srcl8pn2pjtmj6k, 196, 33);
                uint64_t t68 = GM_LD(uintptr_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t66 * 16) + 8);
                void * t69 = ((uint8_t *)GM_LD(void *, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t66 * 16)));
                uint64_t t70 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t71 = gm_l8;
                uint64_t t72 = (uintptr_t)t71;
                if (t70 > t72 || t72 > (uintptr_t)t68) gm_trap_slice(t70, t72, t68, gm_srcl8pn2pjtmj6k, 196, 33);
                gm_b16_8 t73;
                GM_ST(void *, (uint8_t *)&t73, (void *)(((uint8_t *)t69 + t70 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t73 + 8, (uintptr_t)(t72 - t70));
                gm_b16_8 t74 = t73;
                gm_b16_8 t75 = GM_LD(gm_b16_8, ((uint8_t *)&t74));
                gm_f_copy_from_f7j6p05(t65, t75);
            }
            goto me3;
mn51:;
ma6:;
            uint8_t t77 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t77 != ((uint8_t)8ULL)) goto mn76;
            {
                void * t79 = gm_l0;
                uint64_t t80 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t81 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                if (t80 >= (uintptr_t)(t81)) gm_trap_index(t80, t81, gm_srcl8pn2pjtmj6k, 199, 30);
                gm_b16_8 t82 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t80 * 16));
                gm_b16_8 t83 = t82;
                gm_b16_8 t84 = GM_LD(gm_b16_8, ((uint8_t *)&t83));
                uint64_t t85 = gm_f_ptr_of_b77b4c9(t79, t84);
                gm_l9 = t85;
            }
            {
                void * t86 = gm_l0;
                gm_t88rcucg1b7uk t87 = gm_l3;
                uint64_t t88 = gm_f_ptr_align_lavuopj((void *)NULL, cx, NULL, t86, t87);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l10 = t88;
            }
            {
                uint64_t t90 = gm_l9;
                uint8_t t91 = (!(t90 == ((uint64_t)0ULL)));
                if (!t91) goto ie89;
                uint64_t t92 = gm_l10;
                uint8_t t93 = (t92 > ((uint64_t)1ULL));
                if (!t93) goto ie89;
                uint64_t t94 = gm_l9;
                uint64_t t95 = gm_l10;
                if (t95 == 0) gm_trap("division by zero", gm_srcl8pn2pjtmj6k, 201, 45);
                uint64_t t96 = (uint64_t)(t94 % t95);
                uint8_t t97 = (!(t96 == ((uint64_t)0ULL)));
                if (!t97) goto ie89;
                void * t98 = gm_l0;
                uint8_t t99 = gm_f_fast_23em7sv(t98);
                uint8_t t100 = (uint8_t)!t99;
                if (!t100) goto ie89;
                {
                    void * t102 = gm_l0;
                    gm_tl874t2atnlh3 t103 = gm_l1;
                    uint64_t t104 = gm_l9;
                    uint64_t t105 = gm_l10;
                    gm_t2lqu8jsj511d t106;
                    GM_ST(uint64_t, ((uint8_t *)&t106 + 0), t104);
                    GM_ST(uint64_t, ((uint8_t *)&t106 + 8), t105);
                    gm_tabb710d9mq2e t107;
                    gm_f_format_l7c15sv((void *)&t107, (void *)NULL, cx, NULL, t106);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tabb710d9mq2e t108 = t107;
                    if (((((uint8_t *)&t108))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t108) + 8, gm_srcl8pn2pjtmj6k, 202, 38);
                    gm_b16_8 t109 = GM_LD(gm_b16_8, (((uint8_t *)&t108) + 8));
                    gm_b16_8 t110 = t109;
                    gm_b16_8 t111 = GM_LD(gm_b16_8, ((uint8_t *)&t110));
                    gm_t76123gfet73g t112;
                    gm_f_trap_qh84l9d((void *)&t112, (void *)NULL, cx, NULL, t102, t103, t111);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tcfa6l7reoosn t113;
                    GM_ST(uint8_t, (uint8_t *)&t113 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t113 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t113 + 4, 0);
                    ((uint8_t *)&t113)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t113 + 8), t112);
                    gm_tcfa6l7reoosn t114 = t113;
                    uint8_t t115 = ((((uint8_t *)&t114))[0] != 0);
                    F.ret = t113;
                    goto gm_fret;
                }
                goto ix89;
ie89:;
ix89:;
            }
            {
                gm_b16_8 t116 = gm_l7;
                uint64_t t117 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t118 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                if (t117 >= (uintptr_t)(t118)) gm_trap_index(t117, t118, gm_srcl8pn2pjtmj6k, 204, 27);
                gm_b16_8 t119 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t117 * 16));
                gm_b16_8 t120 = t119;
                gm_b16_8 t121 = GM_LD(gm_b16_8, ((uint8_t *)&t120));
                gm_f_copy_from_f7j6p05(t116, t121);
            }
            goto me3;
mn76:;
ma7:;
            uint8_t t125 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t125 != ((uint8_t)9ULL)) goto pa124;
            goto po123;
pa124:;
            uint8_t t127 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t127 != ((uint8_t)13ULL)) goto pa126;
            goto po123;
pa126:;
            uint8_t t129 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t129 != ((uint8_t)35ULL)) goto pa128;
            goto po123;
pa128:;
            goto mn122;
po123:;
            gm_b16_8 t130 = gm_l7;
            uint64_t t131 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t132 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
            if (t131 >= (uintptr_t)(t132)) gm_trap_index(t131, t132, gm_srcl8pn2pjtmj6k, 206, 51);
            gm_b16_8 t133 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t131 * 16));
            gm_b16_8 t134 = t133;
            gm_b16_8 t135 = GM_LD(gm_b16_8, ((uint8_t *)&t134));
            gm_f_copy_from_f7j6p05(t130, t135);
            goto me3;
mn122:;
ma8:;
            uint8_t t139 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t139 != ((uint8_t)10ULL)) goto pa138;
            goto po137;
pa138:;
            uint8_t t141 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t141 != ((uint8_t)11ULL)) goto pa140;
            goto po137;
pa140:;
            uint8_t t143 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t143 != ((uint8_t)12ULL)) goto pa142;
            goto po137;
pa142:;
            goto mn136;
po137:;
            {
                void * t146 = gm_l0;
                gm_t88rcucg1b7uk t147 = gm_l4;
                gm_t53at80pfdus3 t148;
                gm_f_kind_htv6093((void *)&t148, t146, GM_CONV(gm_csguankko4lmhd, t147));
                gm_t53at80pfdus3 t149 = t148;
                uint8_t t150 = GM_LD(uint8_t, (((uint8_t *)&t149) + 0));
                if (t150 != ((uint8_t)9ULL)) goto ie145;
                gm_t88rcucg1b7uk t151 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t149) + 8) + 4));
                gm_l11 = t151;
                {
                    void * t153 = gm_l0;
                    gm_t88rcucg1b7uk t154 = gm_l11;
                    uint64_t t155 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t153, t154);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    __int128 t156 = (__int128)t155;
                    gm_l12 = t156;
                }
                {
                    void * t157 = gm_l0;
                    uint64_t t158 = (uintptr_t)((uint64_t)0ULL);
                    uint64_t t159 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                    if (t158 >= (uintptr_t)(t159)) gm_trap_index(t158, t159, gm_srcl8pn2pjtmj6k, 210, 32);
                    gm_b16_8 t160 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t158 * 16));
                    gm_b16_8 t161 = t160;
                    gm_b16_8 t162 = GM_LD(gm_b16_8, ((uint8_t *)&t161));
                    uint64_t t163 = gm_f_ptr_of_b77b4c9(t157, t162);
                    __int128 t164 = (__int128)t163;
                    gm_l13 = t164;
                }
                {
                    void * t165 = gm_l0;
                    uint64_t t166 = GM_LD(uint64_t, (((uint8_t *)t165) + 176));
                    uint64_t r167; bool o168 = __builtin_mul_overflow(t166, ((uint64_t)8ULL), &r167);
                    if (o168) gm_trap("integer overflow in `*`", gm_srcl8pn2pjtmj6k, 211, 22);
                    uint64_t t169 = r167;
                    gm_l14 = t169;
                }
                {
                    uint64_t t170;
                    uint64_t t172 = gm_l14;
                    uint8_t t173 = (t172 >= ((uint64_t)64ULL));
                    if (!t173) goto ie171;
                    uint64_t t175 = (uint64_t)~((uint64_t)0ULL);
                    uint64_t t176 = t175;
                    t170 = t176;
                    goto ix171;
ie171:;
                    uint64_t t178 = gm_l14;
                    unsigned __int128 n179 = (unsigned __int128)t178;
                    if (n179 >= 64) gm_trap_shift((uint64_t)n179, "u64", gm_srcl8pn2pjtmj6k, 212, 60);
                    uint64_t r180 = (uint64_t)((uint64_t)((uint64_t)1ULL) << (int)n179);
                    bool o181 = (uint64_t)(r180 >> (int)n179) != ((uint64_t)1ULL);
                    if (o181) gm_trap("integer overflow in `<<`", gm_srcl8pn2pjtmj6k, 212, 60);
                    uint64_t t182 = r180;
                    uint64_t r183; bool o184 = __builtin_sub_overflow(t182, ((uint64_t)1ULL), &r183);
                    if (o184) gm_trap("integer overflow in `-`", gm_srcl8pn2pjtmj6k, 212, 59);
                    uint64_t t185 = r183;
                    uint64_t t186 = t185;
                    t170 = t186;
ix171:;
                    gm_l15 = t170;
                }
                {
                    uint8_t t188 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l2) + 0) == ((uint8_t)12ULL)));
                    if (!t188) goto ie187;
                    {
                        __int128 t190 = gm_l13;
                        void * t191 = gm_l0;
                        uint64_t t192 = (uintptr_t)((uint64_t)1ULL);
                        uint64_t t193 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                        if (t192 >= (uintptr_t)(t193)) gm_trap_index(t192, t193, gm_srcl8pn2pjtmj6k, 214, 41);
                        gm_b16_8 t194 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t192 * 16));
                        gm_b16_8 t195 = t194;
                        gm_b16_8 t196 = GM_LD(gm_b16_8, ((uint8_t *)&t195));
                        uint64_t t197 = gm_f_ptr_of_b77b4c9(t191, t196);
                        __int128 t198 = (__int128)t197;
                        __int128 r199; bool o200 = __builtin_sub_overflow(t190, t198, &r199);
                        if (o200) gm_trap("integer overflow in `-`", gm_srcl8pn2pjtmj6k, 214, 23);
                        __int128 t201 = r199;
                        int64_t t202 = (int64_t)t201;
                        gm_l16 = t202;
                    }
                    {
                        int64_t t203;
                        __int128 t205 = gm_l12;
                        uint8_t t206 = ((t205 == ((__int128)(((unsigned __int128)0u << 64) | 0u))));
                        if (!t206) goto ie204;
                        int64_t t208 = ((int64_t)0LL);
                        t203 = t208;
                        goto ix204;
ie204:;
                        int64_t t210 = gm_l16;
                        __int128 t211 = gm_l12;
                        int64_t t212 = (int64_t)t211;
                        if (t212 == 0) gm_trap("division by zero", gm_srcl8pn2pjtmj6k, 215, 54);
                        if (t210 == ((int64_t)(-9223372036854775807LL - 1)) && t212 == (int64_t)-1) gm_trap("integer overflow in `/`", gm_srcl8pn2pjtmj6k, 215, 54);
                        int64_t t213 = (int64_t)(t210 / t212);
                        int64_t t214 = t213;
                        t203 = t214;
ix204:;
                        gm_l17 = t203;
                    }
                    {
                        void * t215 = gm_l0;
                        gm_b16_8 t216 = gm_l7;
                        int64_t t217 = gm_l17;
                        unsigned __int128 t218 = (unsigned __int128)t217;
                        gm_f_put_uint_cddin99(t215, t216, t218);
                    }
                    {
                        gm_b0_1 t219;
                        gm_tcfa6l7reoosn t220;
                        GM_ST(uint8_t, (uint8_t *)&t220 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t220 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t220 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t220 + 8, 0);
                        GM_ST(uint64_t, (uint8_t *)&t220 + 16, 0);
                        GM_ST(uint64_t, (uint8_t *)&t220 + 24, 0);
                        ((uint8_t *)&t220)[0] = 0;
                        GM_ST(gm_b0_1, ((uint8_t *)&t220 + 8), t219);
                        gm_tcfa6l7reoosn t221 = t220;
                        uint8_t t222 = ((((uint8_t *)&t221))[0] != 0);
                        F.ret = t220;
                        goto gm_fret;
                    }
                    goto ix187;
ie187:;
ix187:;
                }
                {
                    void * t223 = gm_l0;
                    gm_t88rcucg1b7uk t224 = gm_l5;
                    gm_tg5lql6itufk7 t225 = gm_f_int_info_ulcevdv(t223, GM_CONV(gm_csguankko4lmhd, t224));
                    gm_tg5lql6itufk7 t226 = t225;
                    gm_ttovhfee8mf28 t227;
                    if (((((uint8_t *)&t226))[8] != 0)) {
                        gm_ttovhfee8mf28 t228 = GM_LD(gm_ttovhfee8mf28, (((uint8_t *)&t226) + 0));
                        t227 = t228;
                    } else {
                        void * t229 = gm_l0;
                        gm_tl874t2atnlh3 t230 = gm_l1;
                        gm_b16_8 t231;
                        GM_ST(void *, (uint8_t *)&t231, (void *)(gm_s064htvgs8b59));
                        GM_ST(uintptr_t, (uint8_t *)&t231 + 8, (uintptr_t)(68));
                        gm_t76123gfet73g t232;
                        gm_f_trap_qh84l9d((void *)&t232, (void *)NULL, cx, NULL, t229, t230, t231);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_tcfa6l7reoosn t233;
                        GM_ST(uint8_t, (uint8_t *)&t233 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t233 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t233 + 4, 0);
                        ((uint8_t *)&t233)[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t233 + 8), t232);
                        gm_tcfa6l7reoosn t234 = t233;
                        uint8_t t235 = ((((uint8_t *)&t234))[0] != 0);
                        F.ret = t233;
                        goto gm_fret;
                    }
                    gm_l18 = t227;
                }
                {
                    void * t236 = gm_l0;
                    gm_t88rcucg1b7uk t237 = gm_l5;
                    uint64_t t238 = (uintptr_t)((uint64_t)1ULL);
                    uint64_t t239 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                    if (t238 >= (uintptr_t)(t239)) gm_trap_index(t238, t239, gm_srcl8pn2pjtmj6k, 220, 34);
                    gm_b16_8 t240 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t238 * 16));
                    gm_b16_8 t241 = t240;
                    gm_b16_8 t242 = GM_LD(gm_b16_8, ((uint8_t *)&t241));
                    unsigned __int128 t243 = gm_f_get_int_pg7p4gc(t236, GM_CONV(gm_csguankko4lmhd, t237), t242);
                    gm_l19 = t243;
                }
                {
                    gm_l20 = ((__int128)(((unsigned __int128)0u << 64) | 0u));
                }
                {
                    uint8_t t245 = GM_LD(uint8_t, (((uint8_t *)&gm_l18) + 0));
                    if (!t245) goto ie244;
                    {
                        unsigned __int128 t247 = gm_l19;
                        unsigned __int128 t248 = t247;
                        __int128 t249;
                        memset(&t249, 0, sizeof t249); memcpy(&t249, ((uint8_t *)&t248), sizeof t249 < 16 ? sizeof t249 : 16);
                        gm_l20 = t249;
                    }
                    goto ix244;
ie244:;
                    unsigned __int128 t251 = gm_l19;
                    unsigned __int128 t252 = (unsigned __int128)~((unsigned __int128)(((unsigned __int128)0u << 64) | 0u));
                    unsigned __int128 n253 = (unsigned __int128)((uint64_t)1ULL);
                    if (n253 >= 128) gm_trap_shift((uint64_t)n253, "u128", gm_srcl8pn2pjtmj6k, 224, 31);
                    unsigned __int128 t254 = (unsigned __int128)(t252 >> (int)n253);
                    uint8_t t255 = (t251 <= t254);
                    if (!t255) goto ie250;
                    {
                        unsigned __int128 t257 = gm_l19;
                        unsigned __int128 t258 = t257;
                        __int128 t259;
                        memset(&t259, 0, sizeof t259); memcpy(&t259, ((uint8_t *)&t258), sizeof t259 < 16 ? sizeof t259 : 16);
                        gm_l20 = t259;
                    }
                    goto ix250;
ie250:;
ix250:;
ix244:;
                }
                {
                    __int128 t260 = gm_l20;
                    __int128 t261 = gm_l12;
                    __int128 r262; bool o263 = __builtin_mul_overflow(t260, t261, &r262);
                    __int128 t264 = r262;
                    gm_l21 = t264;
                }
                {
                    __int128 t265;
                    uint8_t t267 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l2) + 0) == ((uint8_t)10ULL)));
                    if (!t267) goto ie266;
                    __int128 t269 = gm_l13;
                    __int128 t270 = gm_l21;
                    __int128 r271; bool o272 = __builtin_add_overflow(t269, t270, &r271);
                    __int128 t273 = r271;
                    __int128 t274 = t273;
                    t265 = t274;
                    goto ix266;
ie266:;
                    __int128 t276 = gm_l13;
                    __int128 t277 = gm_l21;
                    __int128 r278; bool o279 = __builtin_sub_overflow(t276, t277, &r278);
                    __int128 t280 = r278;
                    __int128 t281 = t280;
                    t265 = t281;
ix266:;
                    gm_l22 = t265;
                }
                {
                    void * t282 = gm_l0;
                    gm_b16_8 t283 = gm_l7;
                    __int128 t284 = gm_l22;
                    uint64_t t285 = (uint64_t)t284;
                    uint64_t t286 = gm_l15;
                    uint64_t t287 = (uint64_t)(t285 & t286);
                    unsigned __int128 t288 = (unsigned __int128)t287;
                    gm_f_put_uint_cddin99(t282, t283, t288);
                }
                goto ix145;
ie145:;
                {
                    void * t290 = gm_l0;
                    gm_tl874t2atnlh3 t291 = gm_l1;
                    gm_b16_8 t292;
                    GM_ST(void *, (uint8_t *)&t292, (void *)(gm_sge745dj3cnrb));
                    GM_ST(uintptr_t, (uint8_t *)&t292 + 8, (uintptr_t)(72));
                    gm_t76123gfet73g t293;
                    gm_f_trap_qh84l9d((void *)&t293, (void *)NULL, cx, NULL, t290, t291, t292);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tcfa6l7reoosn t294;
                    GM_ST(uint8_t, (uint8_t *)&t294 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t294 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t294 + 4, 0);
                    ((uint8_t *)&t294)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t294 + 8), t293);
                    gm_tcfa6l7reoosn t295 = t294;
                    uint8_t t296 = ((((uint8_t *)&t295))[0] != 0);
                    F.ret = t294;
                    goto gm_fret;
                }
ix145:;
            }
            goto me3;
mn136:;
ma9:;
            uint8_t t298 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t298 != ((uint8_t)14ULL)) goto mn297;
            {
                void * t300 = gm_l0;
                gm_t88rcucg1b7uk t301 = gm_l4;
                gm_t57euh612ligr t302;
                gm_f_union_def_h8mvf4t((void *)&t302, t300, GM_CONV(gm_csguankko4lmhd, t301));
                gm_t57euh612ligr t303 = t302;
                gm_tcmkompbl09il t304;
                if (((((uint8_t *)&t303))[24] != 0)) {
                    gm_tcmkompbl09il t305 = GM_LD(gm_tcmkompbl09il, (((uint8_t *)&t303) + 0));
                    t304 = t305;
                } else {
                    void * t306 = gm_l0;
                    gm_tl874t2atnlh3 t307 = gm_l1;
                    gm_b16_8 t308;
                    GM_ST(void *, (uint8_t *)&t308, (void *)(gm_sb9352gc5i144));
                    GM_ST(uintptr_t, (uint8_t *)&t308 + 8, (uintptr_t)(20));
                    gm_t76123gfet73g t309;
                    gm_f_trap_qh84l9d((void *)&t309, (void *)NULL, cx, NULL, t306, t307, t308);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tcfa6l7reoosn t310;
                    GM_ST(uint8_t, (uint8_t *)&t310 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t310 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t310 + 4, 0);
                    ((uint8_t *)&t310)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t310 + 8), t309);
                    gm_tcfa6l7reoosn t311 = t310;
                    uint8_t t312 = ((((uint8_t *)&t311))[0] != 0);
                    F.ret = t310;
                    goto gm_fret;
                }
                gm_l23 = t304;
            }
            {
                void * t313 = gm_l0;
                gm_t88rcucg1b7uk t314 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l23) + 0));
                uint64_t t315 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t313, t314);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint64_t t316 = gm_f_us_cuv01fp(t315);
                gm_l24 = t316;
            }
            {
                gm_b16_8 t317 = gm_l7;
                gm_f_fill_abmf5g5(t317, ((uint8_t)0ULL));
            }
            {
                uint64_t t318 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                void * t319 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l7)));
                uint64_t t320 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t321 = gm_l24;
                uint64_t t322 = (uintptr_t)t321;
                if (t320 > t322 || t322 > (uintptr_t)t318) gm_trap_slice(t320, t322, t318, gm_srcl8pn2pjtmj6k, 235, 13);
                gm_b16_8 t323;
                GM_ST(void *, (uint8_t *)&t323, (void *)(((uint8_t *)t319 + t320 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t323 + 8, (uintptr_t)(t322 - t320));
                uint64_t t324 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t325 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                if (t324 >= (uintptr_t)(t325)) gm_trap_index(t324, t325, gm_srcl8pn2pjtmj6k, 235, 33);
                uint64_t t326 = GM_LD(uintptr_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t324 * 16) + 8);
                void * t327 = ((uint8_t *)GM_LD(void *, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t324 * 16)));
                uint64_t t328 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t329 = gm_l24;
                uint64_t t330 = (uintptr_t)t329;
                if (t328 > t330 || t330 > (uintptr_t)t326) gm_trap_slice(t328, t330, t326, gm_srcl8pn2pjtmj6k, 235, 33);
                gm_b16_8 t331;
                GM_ST(void *, (uint8_t *)&t331, (void *)(((uint8_t *)t327 + t328 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t331 + 8, (uintptr_t)(t330 - t328));
                gm_b16_8 t332 = t331;
                gm_b16_8 t333 = GM_LD(gm_b16_8, ((uint8_t *)&t332));
                gm_f_copy_from_f7j6p05(t323, t333);
            }
            goto me3;
mn297:;
ma10:;
            uint8_t t335 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t335 != ((uint8_t)15ULL)) goto mn334;
            {
                void * t337 = gm_l0;
                gm_t88rcucg1b7uk t338 = gm_l3;
                gm_t88rcucg1b7uk t339 = GM_CONV(gm_t88rcucg1b7uk, gm_f_opt_inner_q4ga84u(t337, GM_CONV(gm_csguankko4lmhd, t338)));
                gm_l25 = t339;
            }
            {
                void * t340 = gm_l0;
                gm_t88rcucg1b7uk t341 = gm_l25;
                gm_t57euh612ligr t342;
                gm_f_union_def_h8mvf4t((void *)&t342, t340, GM_CONV(gm_csguankko4lmhd, t341));
                gm_t57euh612ligr t343 = t342;
                gm_tcmkompbl09il t344;
                if (((((uint8_t *)&t343))[24] != 0)) {
                    gm_tcmkompbl09il t345 = GM_LD(gm_tcmkompbl09il, (((uint8_t *)&t343) + 0));
                    t344 = t345;
                } else {
                    void * t346 = gm_l0;
                    gm_tl874t2atnlh3 t347 = gm_l1;
                    gm_b16_8 t348;
                    GM_ST(void *, (uint8_t *)&t348, (void *)(gm_stk678hojgu6q));
                    GM_ST(uintptr_t, (uint8_t *)&t348 + 8, (uintptr_t)(23));
                    gm_t76123gfet73g t349;
                    gm_f_trap_qh84l9d((void *)&t349, (void *)NULL, cx, NULL, t346, t347, t348);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tcfa6l7reoosn t350;
                    GM_ST(uint8_t, (uint8_t *)&t350 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t350 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t350 + 4, 0);
                    ((uint8_t *)&t350)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t350 + 8), t349);
                    gm_tcfa6l7reoosn t351 = t350;
                    uint8_t t352 = ((((uint8_t *)&t351))[0] != 0);
                    F.ret = t350;
                    goto gm_fret;
                }
                gm_l26 = t344;
            }
            {
                void * t353 = gm_l0;
                gm_t88rcucg1b7uk t354 = gm_l4;
                gm_tg5lql6itufk7 t355 = gm_f_int_info_ulcevdv(t353, GM_CONV(gm_csguankko4lmhd, t354));
                gm_tg5lql6itufk7 t356 = t355;
                gm_ttovhfee8mf28 t357;
                if (((((uint8_t *)&t356))[8] != 0)) {
                    gm_ttovhfee8mf28 t358 = GM_LD(gm_ttovhfee8mf28, (((uint8_t *)&t356) + 0));
                    t357 = t358;
                } else {
                    void * t359 = gm_l0;
                    gm_tl874t2atnlh3 t360 = gm_l1;
                    gm_b16_8 t361;
                    GM_ST(void *, (uint8_t *)&t361, (void *)(gm_s3ergh2il1tjf));
                    GM_ST(uintptr_t, (uint8_t *)&t361 + 8, (uintptr_t)(42));
                    gm_t76123gfet73g t362;
                    gm_f_trap_qh84l9d((void *)&t362, (void *)NULL, cx, NULL, t359, t360, t361);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tcfa6l7reoosn t363;
                    GM_ST(uint8_t, (uint8_t *)&t363 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t363 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t363 + 4, 0);
                    ((uint8_t *)&t363)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t363 + 8), t362);
                    gm_tcfa6l7reoosn t364 = t363;
                    uint8_t t365 = ((((uint8_t *)&t364))[0] != 0);
                    F.ret = t363;
                    goto gm_fret;
                }
                gm_l27 = t357;
            }
            {
                uint8_t t366 = GM_LD(uint8_t, (((uint8_t *)&gm_l27) + 0));
                void * t367 = gm_l0;
                gm_t88rcucg1b7uk t368 = gm_l4;
                uint64_t t369 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t370 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                if (t369 >= (uintptr_t)(t370)) gm_trap_index(t369, t370, gm_srcl8pn2pjtmj6k, 241, 53);
                gm_b16_8 t371 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t369 * 16));
                gm_b16_8 t372 = t371;
                gm_b16_8 t373 = GM_LD(gm_b16_8, ((uint8_t *)&t372));
                unsigned __int128 t374 = gm_f_get_int_pg7p4gc(t367, GM_CONV(gm_csguankko4lmhd, t368), t373);
                gm_f_num_val_i3upeur((void *)&gm_l28, t366, t374);
            }
            {
                {
                    gm_b16_8 t376 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l26) + 8));
                    gm_b16_8 t377 = t376;
                    gm_b16_8 t378 = GM_LD(gm_b16_8, ((uint8_t *)&t377));
                    gm_l29 = t378;
                }
                {
                    gm_l30 = ((uint64_t)0ULL);
                }
                {
                    for (;;) {
                        uint64_t t380 = gm_l30;
                        uint64_t t381 = GM_LD(uintptr_t, ((uint8_t *)&gm_l29) + 8);
                        uint8_t t382 = (t380 < t381);
                        if (!t382) goto lb379;
                        {
                            gm_t3l8idb2m6c1r t385;
                            uint64_t t386 = gm_l30;
                            GM_ST(uint64_t, (uint8_t *)&t385 + 0, t386);
                            uint64_t t387 = gm_l30;
                            uint64_t t388 = (uintptr_t)t387;
                            uint64_t t389 = GM_LD(uintptr_t, ((uint8_t *)&gm_l29) + 8);
                            memcpy((uint8_t *)&t385 + 8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l29)) + t388 * 72), 72);
                            gm_t3l8idb2m6c1r t390 = t385;
                            uint64_t t393 = GM_LD(uint64_t, (((uint8_t *)&t390) + 0));
                            gm_l31 = t393;
                            gm_tckdobunaulee t394 = GM_LD(gm_tckdobunaulee, (((uint8_t *)&t390) + 8));
                            F.l32 = t394;
                            {
                                uint8_t t397;
                                if ((((((uint8_t *)&F.l32) + 16))[4] != 0)) goto mn399;
                                t397 = ((uint8_t)1);
                                goto me398;
mn399:;
                                t397 = ((uint8_t)0);
                                goto me398;
mn400:;
                                gm_trap("no match arm matched the scrutinee", gm_srcl8pn2pjtmj6k, 243, 20);
me398:;
                                if (!t397) goto ie396;
                                uint8_t t401 = GM_LD(uint8_t, ((((uint8_t *)&F.l32) + 24) + 0));
                                uint8_t t402 = GM_LD(uint8_t, (((uint8_t *)&gm_l28) + 0));
                                uint8_t t403 = ((t401 == t402));
                                if (!t403) goto ie396;
                                {
                                    unsigned __int128 t405 = gm_f_bits128_jigkui4(&(*(gm_tbci5ospk5nnn *)(((uint8_t *)&F.l32) + 24)));
                                    gm_l33 = t405;
                                }
                                {
                                    unsigned __int128 t406;
                                    uint8_t t408 = GM_LD(uint8_t, ((((uint8_t *)&F.l32) + 24) + 0));
                                    if (!t408) goto ie407;
                                    unsigned __int128 t410 = gm_l33;
                                    unsigned __int128 r411; bool o412 = __builtin_sub_overflow(((unsigned __int128)(((unsigned __int128)0u << 64) | 0u)), t410, &r411);
                                    unsigned __int128 t413 = r411;
                                    unsigned __int128 t414 = t413;
                                    t406 = t414;
                                    goto ix407;
ie407:;
                                    unsigned __int128 t416 = gm_l33;
                                    unsigned __int128 t417 = t416;
                                    t406 = t417;
ix407:;
                                    gm_l34 = t406;
                                }
                                {
                                    unsigned __int128 t419 = gm_l34;
                                    unsigned __int128 t420 = GM_LD(unsigned __int128, (((uint8_t *)&gm_l28) + 16));
                                    uint8_t t421 = ((t419 == t420));
                                    if (!t421) goto ie418;
                                    {
                                        void * t423 = gm_l0;
                                        gm_t2lqu8jsj511d t424 = GM_CONV(gm_t2lqu8jsj511d, gm_f_mark_kpppm1j(t423));
                                        gm_l35 = t424;
                                    }
                                    {
                                        void * t425 = gm_l0;
                                        void * t426 = gm_l0;
                                        gm_t88rcucg1b7uk t427 = gm_l25;
                                        uint64_t t428 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t426, t427);
                                        if (gm_unw.kind) {
                                            goto gm_ll383;
                                        }
                                        gm_b16_8 t429 = gm_f_tmp_8k6u9c9((void *)NULL, cx, NULL, t425, t428);
                                        if (gm_unw.kind) {
                                            goto gm_ll383;
                                        }
                                        gm_l36 = t429;
                                    }
                                    {
                                        void * t430 = gm_l0;
                                        gm_t88rcucg1b7uk t431 = gm_l25;
                                        gm_b16_8 t432 = gm_l36;
                                        uint64_t t433 = gm_l31;
                                        uint32_t t434 = (uint32_t)t433;
                                        gm_b16_8 t435 = gm_f_empty_slice_ijtp9lo();
                                        gm_b16_8 t436 = t435;
                                        gm_b16_8 t437 = GM_LD(gm_b16_8, ((uint8_t *)&t436));
                                        gm_b0_1 t438 = gm_f_make_variant_bn3d74n((void *)NULL, cx, NULL, t430, t431, t432, t434, t437);
                                        if (gm_unw.kind) {
                                            goto gm_ll383;
                                        }
                                    }
                                    {
                                        void * t439 = gm_l0;
                                        gm_t88rcucg1b7uk t440 = gm_l3;
                                        gm_b16_8 t441 = gm_l7;
                                        gm_b16_8 t442 = gm_l36;
                                        gm_b16_8 t443 = t442;
                                        gm_b16_8 t444 = GM_LD(gm_b16_8, ((uint8_t *)&t443));
                                        gm_b0_1 t445 = gm_f_make_some_tvb9ubk((void *)NULL, cx, NULL, t439, t440, t441, t444);
                                        if (gm_unw.kind) {
                                            goto gm_ll383;
                                        }
                                    }
                                    {
                                        void * t446 = gm_l0;
                                        gm_t2lqu8jsj511d t447 = gm_l35;
                                        gm_f_release_jn5kqdi(t446, GM_CONV(gm_csc7lc6jlkqkuq, t447));
                                    }
                                    {
                                        gm_b0_1 t448;
                                        gm_tcfa6l7reoosn t449;
                                        GM_ST(uint8_t, (uint8_t *)&t449 + 1, 0);
                                        GM_ST(uint16_t, (uint8_t *)&t449 + 2, 0);
                                        GM_ST(uint32_t, (uint8_t *)&t449 + 4, 0);
                                        GM_ST(uint64_t, (uint8_t *)&t449 + 8, 0);
                                        GM_ST(uint64_t, (uint8_t *)&t449 + 16, 0);
                                        GM_ST(uint64_t, (uint8_t *)&t449 + 24, 0);
                                        ((uint8_t *)&t449)[0] = 0;
                                        GM_ST(gm_b0_1, ((uint8_t *)&t449 + 8), t448);
                                        gm_tcfa6l7reoosn t450 = t449;
                                        uint8_t t451 = ((((uint8_t *)&t450))[0] != 0);
                                        F.ret = t449;
                                        goto gm_fret;
                                    }
                                    goto ix418;
ie418:;
ix418:;
                                }
                                goto ix396;
ie396:;
ix396:;
                            }
                            goto me391;
mn392:;
                            gm_trap("no match arm matched the scrutinee", gm_srcl8pn2pjtmj6k, 242, 13);
me391:;
                        }
lc379:;
                        {
                            uint64_t t453 = gm_l30;
                            uint64_t r454; bool o455 = __builtin_add_overflow(t453, ((uint64_t)1ULL), &r454);
                            uint64_t t456 = r454;
                            gm_l30 = t456;
                        }
                    }
gm_ll383:;
                    if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb379; }
                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc379; }
                    }
                    goto gm_lad;
lb379:;
                }
            }
            {
                gm_b16_8 t457 = gm_l7;
                gm_f_fill_abmf5g5(t457, ((uint8_t)0ULL));
            }
            goto me3;
mn334:;
ma11:;
            uint8_t t461 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t461 != ((uint8_t)16ULL)) goto pa460;
            goto po459;
pa460:;
            uint8_t t463 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t463 != ((uint8_t)17ULL)) goto pa462;
            goto po459;
pa462:;
            uint8_t t465 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t465 != ((uint8_t)18ULL)) goto pa464;
            goto po459;
pa464:;
            goto mn458;
po459:;
            {
                void * t468 = gm_l0;
                gm_t88rcucg1b7uk t469 = gm_l4;
                gm_t53at80pfdus3 t470;
                gm_f_kind_htv6093((void *)&t470, t468, GM_CONV(gm_csguankko4lmhd, t469));
                gm_t53at80pfdus3 t471 = t470;
                uint8_t t472 = GM_LD(uint8_t, (((uint8_t *)&t471) + 0));
                if (t472 != ((uint8_t)3ULL)) goto ie467;
                {
                    void * t474 = gm_l0;
                    gm_t88rcucg1b7uk t475 = gm_l4;
                    uint64_t t476 = (uintptr_t)((uint64_t)0ULL);
                    uint64_t t477 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                    if (t476 >= (uintptr_t)(t477)) gm_trap_index(t476, t477, gm_srcl8pn2pjtmj6k, 260, 38);
                    gm_b16_8 t478 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t476 * 16));
                    gm_b16_8 t479 = t478;
                    gm_b16_8 t480 = GM_LD(gm_b16_8, ((uint8_t *)&t479));
                    double t481 = gm_f_get_float_grmhlm4(t474, GM_CONV(gm_csguankko4lmhd, t475), t480);
                    gm_l37 = t481;
                }
                {
                    void * t482 = gm_l0;
                    gm_t88rcucg1b7uk t483 = gm_l4;
                    uint64_t t484 = (uintptr_t)((uint64_t)1ULL);
                    uint64_t t485 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                    if (t484 >= (uintptr_t)(t485)) gm_trap_index(t484, t485, gm_srcl8pn2pjtmj6k, 261, 38);
                    gm_b16_8 t486 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t484 * 16));
                    gm_b16_8 t487 = t486;
                    gm_b16_8 t488 = GM_LD(gm_b16_8, ((uint8_t *)&t487));
                    double t489 = gm_f_get_float_grmhlm4(t482, GM_CONV(gm_csguankko4lmhd, t483), t488);
                    gm_l38 = t489;
                }
                {
                    double t490;
                    uint8_t t496 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    switch (t496) { case ((uint8_t)0ULL): goto ma494; case ((uint8_t)1ULL): goto ma494; case ((uint8_t)2ULL): goto ma494; case ((uint8_t)3ULL): goto ma494; case ((uint8_t)4ULL): goto ma494; case ((uint8_t)5ULL): goto ma494; case ((uint8_t)6ULL): goto ma494; case ((uint8_t)7ULL): goto ma494; case ((uint8_t)8ULL): goto ma494; case ((uint8_t)9ULL): goto ma494; case ((uint8_t)10ULL): goto ma494; case ((uint8_t)11ULL): goto ma494; case ((uint8_t)12ULL): goto ma494; case ((uint8_t)13ULL): goto ma494; case ((uint8_t)14ULL): goto ma494; case ((uint8_t)15ULL): goto ma494; case ((uint8_t)16ULL): goto ma492; case ((uint8_t)17ULL): goto ma493; case ((uint8_t)18ULL): goto ma494; case ((uint8_t)19ULL): goto ma494; case ((uint8_t)20ULL): goto ma494; case ((uint8_t)21ULL): goto ma494; case ((uint8_t)22ULL): goto ma494; case ((uint8_t)23ULL): goto ma494; case ((uint8_t)24ULL): goto ma494; case ((uint8_t)25ULL): goto ma494; case ((uint8_t)26ULL): goto ma494; case ((uint8_t)27ULL): goto ma494; case ((uint8_t)28ULL): goto ma494; case ((uint8_t)29ULL): goto ma494; case ((uint8_t)30ULL): goto ma494; case ((uint8_t)31ULL): goto ma494; case ((uint8_t)32ULL): goto ma494; case ((uint8_t)33ULL): goto ma494; case ((uint8_t)34ULL): goto ma494; case ((uint8_t)35ULL): goto ma494; case ((uint8_t)36ULL): goto ma494; case ((uint8_t)37ULL): goto ma494; case ((uint8_t)38ULL): goto ma494; case ((uint8_t)39ULL): goto ma494; case ((uint8_t)40ULL): goto ma494; case ((uint8_t)41ULL): goto ma494; case ((uint8_t)42ULL): goto ma494; case ((uint8_t)43ULL): goto ma494; case ((uint8_t)44ULL): goto ma494; case ((uint8_t)45ULL): goto ma494; case ((uint8_t)46ULL): goto ma494; case ((uint8_t)47ULL): goto ma494; case ((uint8_t)48ULL): goto ma494; case ((uint8_t)49ULL): goto ma494; case ((uint8_t)50ULL): goto ma494; case ((uint8_t)51ULL): goto ma494; case ((uint8_t)52ULL): goto ma494; case ((uint8_t)53ULL): goto ma494; case ((uint8_t)54ULL): goto ma494; case ((uint8_t)55ULL): goto ma494; case ((uint8_t)56ULL): goto ma494; case ((uint8_t)57ULL): goto ma494; case ((uint8_t)58ULL): goto ma494; case ((uint8_t)59ULL): goto ma494; case ((uint8_t)60ULL): goto ma494; case ((uint8_t)61ULL): goto ma494; case ((uint8_t)62ULL): goto ma494; case ((uint8_t)63ULL): goto ma494; case ((uint8_t)64ULL): goto ma494; case ((uint8_t)65ULL): goto ma494; case ((uint8_t)66ULL): goto ma494; case ((uint8_t)67ULL): goto ma494; case ((uint8_t)68ULL): goto ma494; case ((uint8_t)69ULL): goto ma494; case ((uint8_t)70ULL): goto ma494; case ((uint8_t)71ULL): goto ma494; case ((uint8_t)72ULL): goto ma494; case ((uint8_t)73ULL): goto ma494; case ((uint8_t)74ULL): goto ma494; case ((uint8_t)75ULL): goto ma494; case ((uint8_t)76ULL): goto ma494; case ((uint8_t)77ULL): goto ma494; case ((uint8_t)78ULL): goto ma494; case ((uint8_t)79ULL): goto ma494; case ((uint8_t)80ULL): goto ma494; case ((uint8_t)81ULL): goto ma494; default: goto ma494; }
ma492:;
                    uint8_t t498 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    if (t498 != ((uint8_t)16ULL)) goto mn497;
                    double t499 = gm_l37;
                    double t500 = gm_l38;
                    double t501 = gm_f_num_fmin_1k5uqp0(t499, t500);
                    t490 = t501;
                    goto me491;
mn497:;
ma493:;
                    uint8_t t503 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    if (t503 != ((uint8_t)17ULL)) goto mn502;
                    double t504 = gm_l37;
                    double t505 = gm_l38;
                    double t506 = gm_f_num_fmax_0qau2jp(t504, t505);
                    t490 = t506;
                    goto me491;
mn502:;
ma494:;
                    double t508 = gm_l37;
                    double t509 = gm_l38;
                    double t510 = gm_f_num_fmax_0qau2jp(t508, t509);
                    void * t511 = gm_l0;
                    gm_t88rcucg1b7uk t512 = gm_l4;
                    uint64_t t513 = (uintptr_t)((uint64_t)2ULL);
                    uint64_t t514 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                    if (t513 >= (uintptr_t)(t514)) gm_trap_index(t513, t514, gm_srcl8pn2pjtmj6k, 265, 67);
                    gm_b16_8 t515 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t513 * 16));
                    gm_b16_8 t516 = t515;
                    gm_b16_8 t517 = GM_LD(gm_b16_8, ((uint8_t *)&t516));
                    double t518 = gm_f_get_float_grmhlm4(t511, GM_CONV(gm_csguankko4lmhd, t512), t517);
                    double t519 = gm_f_num_fmin_1k5uqp0(t510, t518);
                    t490 = t519;
                    goto me491;
mn507:;
ma495:;
                    gm_trap("no match arm matched the scrutinee", gm_srcl8pn2pjtmj6k, 262, 22);
me491:;
                    gm_l39 = t490;
                }
                {
                    void * t520 = gm_l0;
                    gm_t88rcucg1b7uk t521 = gm_l4;
                    double t522 = gm_l39;
                    gm_b16_8 t523 = gm_l7;
                    gm_f_put_float_fiad72d(t520, GM_CONV(gm_csguankko4lmhd, t521), t522, t523);
                }
                {
                    gm_b0_1 t524;
                    gm_tcfa6l7reoosn t525;
                    GM_ST(uint8_t, (uint8_t *)&t525 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t525 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t525 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t525 + 8, 0);
                    GM_ST(uint64_t, (uint8_t *)&t525 + 16, 0);
                    GM_ST(uint64_t, (uint8_t *)&t525 + 24, 0);
                    ((uint8_t *)&t525)[0] = 0;
                    GM_ST(gm_b0_1, ((uint8_t *)&t525 + 8), t524);
                    gm_tcfa6l7reoosn t526 = t525;
                    uint8_t t527 = ((((uint8_t *)&t526))[0] != 0);
                    F.ret = t525;
                    goto gm_fret;
                }
                goto ix467;
ie467:;
ix467:;
            }
            {
                void * t528 = gm_l0;
                gm_t88rcucg1b7uk t529 = gm_l4;
                gm_tg5lql6itufk7 t530 = gm_f_int_info_ulcevdv(t528, GM_CONV(gm_csguankko4lmhd, t529));
                gm_tg5lql6itufk7 t531 = t530;
                gm_ttovhfee8mf28 t532;
                if (((((uint8_t *)&t531))[8] != 0)) {
                    gm_ttovhfee8mf28 t533 = GM_LD(gm_ttovhfee8mf28, (((uint8_t *)&t531) + 0));
                    t532 = t533;
                } else {
                    void * t534 = gm_l0;
                    gm_tl874t2atnlh3 t535 = gm_l1;
                    void * t536 = gm_l0;
                    void * t537 = GM_LD(void *, (((uint8_t *)t536) + 8));
                    gm_t88rcucg1b7uk t538 = gm_l4;
                    gm_b16_8 t539 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t537, t538);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tnpifv20irnph t540;
                    GM_ST(gm_b16_8, ((uint8_t *)&t540 + 0), t539);
                    gm_tabb710d9mq2e t541;
                    gm_f_format_kj15nqp((void *)&t541, (void *)NULL, cx, NULL, t540);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tabb710d9mq2e t542 = t541;
                    if (((((uint8_t *)&t542))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t542) + 8, gm_srcl8pn2pjtmj6k, 270, 62);
                    gm_b16_8 t543 = GM_LD(gm_b16_8, (((uint8_t *)&t542) + 8));
                    gm_b16_8 t544 = t543;
                    gm_b16_8 t545 = GM_LD(gm_b16_8, ((uint8_t *)&t544));
                    gm_t76123gfet73g t546;
                    gm_f_trap_qh84l9d((void *)&t546, (void *)NULL, cx, NULL, t534, t535, t545);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tcfa6l7reoosn t547;
                    GM_ST(uint8_t, (uint8_t *)&t547 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t547 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t547 + 4, 0);
                    ((uint8_t *)&t547)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t547 + 8), t546);
                    gm_tcfa6l7reoosn t548 = t547;
                    uint8_t t549 = ((((uint8_t *)&t548))[0] != 0);
                    F.ret = t547;
                    goto gm_fret;
                }
                gm_l40 = t532;
            }
            {
                void * t550 = gm_l0;
                gm_t88rcucg1b7uk t551 = gm_l4;
                uint64_t t552 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t553 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                if (t552 >= (uintptr_t)(t553)) gm_trap_index(t552, t553, gm_srcl8pn2pjtmj6k, 271, 32);
                gm_b16_8 t554 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t552 * 16));
                gm_b16_8 t555 = t554;
                gm_b16_8 t556 = GM_LD(gm_b16_8, ((uint8_t *)&t555));
                unsigned __int128 t557 = gm_f_get_int_pg7p4gc(t550, GM_CONV(gm_csguankko4lmhd, t551), t556);
                gm_l41 = t557;
            }
            {
                void * t558 = gm_l0;
                gm_t88rcucg1b7uk t559 = gm_l4;
                uint64_t t560 = (uintptr_t)((uint64_t)1ULL);
                uint64_t t561 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                if (t560 >= (uintptr_t)(t561)) gm_trap_index(t560, t561, gm_srcl8pn2pjtmj6k, 272, 32);
                gm_b16_8 t562 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t560 * 16));
                gm_b16_8 t563 = t562;
                gm_b16_8 t564 = GM_LD(gm_b16_8, ((uint8_t *)&t563));
                unsigned __int128 t565 = gm_f_get_int_pg7p4gc(t558, GM_CONV(gm_csguankko4lmhd, t559), t564);
                gm_l42 = t565;
            }
            {
                gm_l43 = ((uint64_t)0ULL);
            }
            {
                uint8_t t571 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                switch (t571) { case ((uint8_t)0ULL): goto ma569; case ((uint8_t)1ULL): goto ma569; case ((uint8_t)2ULL): goto ma569; case ((uint8_t)3ULL): goto ma569; case ((uint8_t)4ULL): goto ma569; case ((uint8_t)5ULL): goto ma569; case ((uint8_t)6ULL): goto ma569; case ((uint8_t)7ULL): goto ma569; case ((uint8_t)8ULL): goto ma569; case ((uint8_t)9ULL): goto ma569; case ((uint8_t)10ULL): goto ma569; case ((uint8_t)11ULL): goto ma569; case ((uint8_t)12ULL): goto ma569; case ((uint8_t)13ULL): goto ma569; case ((uint8_t)14ULL): goto ma569; case ((uint8_t)15ULL): goto ma569; case ((uint8_t)16ULL): goto ma567; case ((uint8_t)17ULL): goto ma568; case ((uint8_t)18ULL): goto ma569; case ((uint8_t)19ULL): goto ma569; case ((uint8_t)20ULL): goto ma569; case ((uint8_t)21ULL): goto ma569; case ((uint8_t)22ULL): goto ma569; case ((uint8_t)23ULL): goto ma569; case ((uint8_t)24ULL): goto ma569; case ((uint8_t)25ULL): goto ma569; case ((uint8_t)26ULL): goto ma569; case ((uint8_t)27ULL): goto ma569; case ((uint8_t)28ULL): goto ma569; case ((uint8_t)29ULL): goto ma569; case ((uint8_t)30ULL): goto ma569; case ((uint8_t)31ULL): goto ma569; case ((uint8_t)32ULL): goto ma569; case ((uint8_t)33ULL): goto ma569; case ((uint8_t)34ULL): goto ma569; case ((uint8_t)35ULL): goto ma569; case ((uint8_t)36ULL): goto ma569; case ((uint8_t)37ULL): goto ma569; case ((uint8_t)38ULL): goto ma569; case ((uint8_t)39ULL): goto ma569; case ((uint8_t)40ULL): goto ma569; case ((uint8_t)41ULL): goto ma569; case ((uint8_t)42ULL): goto ma569; case ((uint8_t)43ULL): goto ma569; case ((uint8_t)44ULL): goto ma569; case ((uint8_t)45ULL): goto ma569; case ((uint8_t)46ULL): goto ma569; case ((uint8_t)47ULL): goto ma569; case ((uint8_t)48ULL): goto ma569; case ((uint8_t)49ULL): goto ma569; case ((uint8_t)50ULL): goto ma569; case ((uint8_t)51ULL): goto ma569; case ((uint8_t)52ULL): goto ma569; case ((uint8_t)53ULL): goto ma569; case ((uint8_t)54ULL): goto ma569; case ((uint8_t)55ULL): goto ma569; case ((uint8_t)56ULL): goto ma569; case ((uint8_t)57ULL): goto ma569; case ((uint8_t)58ULL): goto ma569; case ((uint8_t)59ULL): goto ma569; case ((uint8_t)60ULL): goto ma569; case ((uint8_t)61ULL): goto ma569; case ((uint8_t)62ULL): goto ma569; case ((uint8_t)63ULL): goto ma569; case ((uint8_t)64ULL): goto ma569; case ((uint8_t)65ULL): goto ma569; case ((uint8_t)66ULL): goto ma569; case ((uint8_t)67ULL): goto ma569; case ((uint8_t)68ULL): goto ma569; case ((uint8_t)69ULL): goto ma569; case ((uint8_t)70ULL): goto ma569; case ((uint8_t)71ULL): goto ma569; case ((uint8_t)72ULL): goto ma569; case ((uint8_t)73ULL): goto ma569; case ((uint8_t)74ULL): goto ma569; case ((uint8_t)75ULL): goto ma569; case ((uint8_t)76ULL): goto ma569; case ((uint8_t)77ULL): goto ma569; case ((uint8_t)78ULL): goto ma569; case ((uint8_t)79ULL): goto ma569; case ((uint8_t)80ULL): goto ma569; case ((uint8_t)81ULL): goto ma569; default: goto ma569; }
ma567:;
                uint8_t t573 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                if (t573 != ((uint8_t)16ULL)) goto mn572;
                {
                    uint64_t t575;
                    uint8_t t577 = GM_LD(uint8_t, (((uint8_t *)&gm_l40) + 0));
                    unsigned __int128 t578 = gm_l41;
                    unsigned __int128 t579 = gm_l42;
                    int32_t t580 = gm_f_int_cmp_kb3sm6u(t577, t578, t579);
                    uint8_t t581 = (t580 <= ((int32_t)0LL));
                    if (!t581) goto ie576;
                    uint64_t t583 = ((uint64_t)0ULL);
                    t575 = t583;
                    goto ix576;
ie576:;
                    uint64_t t585 = ((uint64_t)1ULL);
                    t575 = t585;
ix576:;
                    gm_l43 = t575;
                }
                goto me566;
mn572:;
ma568:;
                uint8_t t587 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                if (t587 != ((uint8_t)17ULL)) goto mn586;
                {
                    uint64_t t589;
                    uint8_t t591 = GM_LD(uint8_t, (((uint8_t *)&gm_l40) + 0));
                    unsigned __int128 t592 = gm_l41;
                    unsigned __int128 t593 = gm_l42;
                    int32_t t594 = gm_f_int_cmp_kb3sm6u(t591, t592, t593);
                    uint8_t t595 = (t594 >= ((int32_t)0LL));
                    if (!t595) goto ie590;
                    uint64_t t597 = ((uint64_t)0ULL);
                    t589 = t597;
                    goto ix590;
ie590:;
                    uint64_t t599 = ((uint64_t)1ULL);
                    t589 = t599;
ix590:;
                    gm_l43 = t589;
                }
                goto me566;
mn586:;
ma569:;
                {
                    void * t602 = gm_l0;
                    gm_t88rcucg1b7uk t603 = gm_l4;
                    uint64_t t604 = (uintptr_t)((uint64_t)2ULL);
                    uint64_t t605 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                    if (t604 >= (uintptr_t)(t605)) gm_trap_index(t604, t605, gm_srcl8pn2pjtmj6k, 278, 41);
                    gm_b16_8 t606 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t604 * 16));
                    gm_b16_8 t607 = t606;
                    gm_b16_8 t608 = GM_LD(gm_b16_8, ((uint8_t *)&t607));
                    unsigned __int128 t609 = gm_f_get_int_pg7p4gc(t602, GM_CONV(gm_csguankko4lmhd, t603), t608);
                    gm_l44 = t609;
                }
                {
                    uint8_t t611 = GM_LD(uint8_t, (((uint8_t *)&gm_l40) + 0));
                    unsigned __int128 t612 = gm_l41;
                    unsigned __int128 t613 = gm_l42;
                    int32_t t614 = gm_f_int_cmp_kb3sm6u(t611, t612, t613);
                    uint8_t t615 = (t614 < ((int32_t)0LL));
                    if (!t615) goto ie610;
                    {
                        gm_l43 = ((uint64_t)1ULL);
                    }
                    goto ix610;
ie610:;
                    uint8_t t618 = GM_LD(uint8_t, (((uint8_t *)&gm_l40) + 0));
                    unsigned __int128 t619 = gm_l41;
                    unsigned __int128 t620 = gm_l44;
                    int32_t t621 = gm_f_int_cmp_kb3sm6u(t618, t619, t620);
                    uint8_t t622 = (t621 > ((int32_t)0LL));
                    if (!t622) goto ie617;
                    {
                        gm_l43 = ((uint64_t)2ULL);
                    }
                    goto ix617;
ie617:;
ix617:;
ix610:;
                }
                goto me566;
mn600:;
ma570:;
                gm_trap("no match arm matched the scrutinee", gm_srcl8pn2pjtmj6k, 274, 13);
me566:;
            }
            {
                gm_b16_8 t624 = gm_l7;
                uint64_t t625 = gm_l43;
                uint64_t t626 = (uintptr_t)t625;
                uint64_t t627 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                if (t626 >= (uintptr_t)(t627)) gm_trap_index(t626, t627, gm_srcl8pn2pjtmj6k, 286, 27);
                gm_b16_8 t628 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t626 * 16));
                gm_b16_8 t629 = t628;
                gm_b16_8 t630 = GM_LD(gm_b16_8, ((uint8_t *)&t629));
                gm_f_copy_from_f7j6p05(t624, t630);
            }
            goto me3;
mn458:;
ma12:;
            uint8_t t632 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t632 != ((uint8_t)19ULL)) goto mn631;
            {
                void * t635 = gm_l0;
                gm_t88rcucg1b7uk t636 = gm_l4;
                gm_t53at80pfdus3 t637;
                gm_f_kind_htv6093((void *)&t637, t635, GM_CONV(gm_csguankko4lmhd, t636));
                gm_t53at80pfdus3 t638 = t637;
                uint8_t t639 = GM_LD(uint8_t, (((uint8_t *)&t638) + 0));
                if (t639 != ((uint8_t)3ULL)) goto ie634;
                {
                    void * t641 = gm_l0;
                    gm_t88rcucg1b7uk t642 = gm_l4;
                    void * t643 = gm_l0;
                    gm_t88rcucg1b7uk t644 = gm_l4;
                    uint64_t t645 = (uintptr_t)((uint64_t)0ULL);
                    uint64_t t646 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                    if (t645 >= (uintptr_t)(t646)) gm_trap_index(t645, t646, gm_srcl8pn2pjtmj6k, 290, 50);
                    gm_b16_8 t647 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t645 * 16));
                    gm_b16_8 t648 = t647;
                    gm_b16_8 t649 = GM_LD(gm_b16_8, ((uint8_t *)&t648));
                    double t650 = gm_f_get_float_grmhlm4(t643, GM_CONV(gm_csguankko4lmhd, t644), t649);
                    double t651 = t650;
                    uint64_t t652;
                    memset(&t652, 0, sizeof t652); memcpy(&t652, ((uint8_t *)&t651), sizeof t652 < 8 ? sizeof t652 : 8);
                    uint64_t t653 = (uint64_t)(t652 & ((uint64_t)9223372036854775807ULL));
                    uint64_t t654 = t653;
                    double t655;
                    memset(&t655, 0, sizeof t655); memcpy(&t655, ((uint8_t *)&t654), sizeof t655 < 8 ? sizeof t655 : 8);
                    gm_b16_8 t656 = gm_l7;
                    gm_f_put_float_fiad72d(t641, GM_CONV(gm_csguankko4lmhd, t642), t655, t656);
                }
                {
                    gm_b0_1 t657;
                    gm_tcfa6l7reoosn t658;
                    GM_ST(uint8_t, (uint8_t *)&t658 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t658 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t658 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t658 + 8, 0);
                    GM_ST(uint64_t, (uint8_t *)&t658 + 16, 0);
                    GM_ST(uint64_t, (uint8_t *)&t658 + 24, 0);
                    ((uint8_t *)&t658)[0] = 0;
                    GM_ST(gm_b0_1, ((uint8_t *)&t658 + 8), t657);
                    gm_tcfa6l7reoosn t659 = t658;
                    uint8_t t660 = ((((uint8_t *)&t659))[0] != 0);
                    F.ret = t658;
                    goto gm_fret;
                }
                goto ix634;
ie634:;
ix634:;
            }
            {
                void * t661 = gm_l0;
                gm_t88rcucg1b7uk t662 = gm_l4;
                gm_tg5lql6itufk7 t663 = gm_f_int_info_ulcevdv(t661, GM_CONV(gm_csguankko4lmhd, t662));
                gm_tg5lql6itufk7 t664 = t663;
                gm_ttovhfee8mf28 t665;
                if (((((uint8_t *)&t664))[8] != 0)) {
                    gm_ttovhfee8mf28 t666 = GM_LD(gm_ttovhfee8mf28, (((uint8_t *)&t664) + 0));
                    t665 = t666;
                } else {
                    void * t667 = gm_l0;
                    gm_tl874t2atnlh3 t668 = gm_l1;
                    gm_b16_8 t669;
                    GM_ST(void *, (uint8_t *)&t669, (void *)(gm_sbel6qookv0gl));
                    GM_ST(uintptr_t, (uint8_t *)&t669 + 8, (uintptr_t)(53));
                    gm_t76123gfet73g t670;
                    gm_f_trap_qh84l9d((void *)&t670, (void *)NULL, cx, NULL, t667, t668, t669);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tcfa6l7reoosn t671;
                    GM_ST(uint8_t, (uint8_t *)&t671 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t671 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t671 + 4, 0);
                    ((uint8_t *)&t671)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t671 + 8), t670);
                    gm_tcfa6l7reoosn t672 = t671;
                    uint8_t t673 = ((((uint8_t *)&t672))[0] != 0);
                    F.ret = t671;
                    goto gm_fret;
                }
                gm_l45 = t665;
            }
            {
                void * t674 = gm_l0;
                gm_t88rcucg1b7uk t675 = gm_l4;
                uint64_t t676 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t677 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                if (t676 >= (uintptr_t)(t677)) gm_trap_index(t676, t677, gm_srcl8pn2pjtmj6k, 294, 32);
                gm_b16_8 t678 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t676 * 16));
                gm_b16_8 t679 = t678;
                gm_b16_8 t680 = GM_LD(gm_b16_8, ((uint8_t *)&t679));
                unsigned __int128 t681 = gm_f_get_int_pg7p4gc(t674, GM_CONV(gm_csguankko4lmhd, t675), t680);
                gm_l46 = t681;
            }
            {
                uint8_t t683 = 0;
                uint8_t t684 = GM_LD(uint8_t, (((uint8_t *)&gm_l45) + 0));
                uint8_t t685 = (uint8_t)!t684;
                if (!t685) {
                    unsigned __int128 t686 = gm_l46;
                    unsigned __int128 n687 = (unsigned __int128)((uint64_t)127ULL);
                    if (n687 >= 128) gm_trap_shift((uint64_t)n687, "u128", gm_srcl8pn2pjtmj6k, 295, 36);
                    unsigned __int128 t688 = (unsigned __int128)(t686 >> (int)n687);
                    uint8_t t689 = ((t688 == ((unsigned __int128)(((unsigned __int128)0u << 64) | 0u))));
                    t683 = t689;
                } else t683 = 1;
                if (!t683) goto ie682;
                {
                    void * t691 = gm_l0;
                    gm_b16_8 t692 = gm_l7;
                    unsigned __int128 t693 = gm_l46;
                    gm_f_put_uint_cddin99(t691, t692, t693);
                }
                {
                    gm_b0_1 t694;
                    gm_tcfa6l7reoosn t695;
                    GM_ST(uint8_t, (uint8_t *)&t695 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t695 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t695 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t695 + 8, 0);
                    GM_ST(uint64_t, (uint8_t *)&t695 + 16, 0);
                    GM_ST(uint64_t, (uint8_t *)&t695 + 24, 0);
                    ((uint8_t *)&t695)[0] = 0;
                    GM_ST(gm_b0_1, ((uint8_t *)&t695 + 8), t694);
                    gm_tcfa6l7reoosn t696 = t695;
                    uint8_t t697 = ((((uint8_t *)&t696))[0] != 0);
                    F.ret = t695;
                    goto gm_fret;
                }
                goto ix682;
ie682:;
ix682:;
            }
            {
                unsigned __int128 t698 = gm_l46;
                unsigned __int128 r699; bool o700 = __builtin_sub_overflow(((unsigned __int128)(((unsigned __int128)0u << 64) | 0u)), t698, &r699);
                unsigned __int128 t701 = r699;
                gm_l47 = t701;
            }
            {
                unsigned __int128 t703 = gm_l47;
                uint32_t t704 = GM_LD(uint32_t, (((uint8_t *)&gm_l45) + 4));
                uint32_t r705; bool o706 = __builtin_sub_overflow(t704, ((uint32_t)1ULL), &r705);
                if (o706) gm_trap("integer overflow in `-`", gm_srcl8pn2pjtmj6k, 300, 34);
                uint32_t t707 = r705;
                unsigned __int128 n708 = (unsigned __int128)t707;
                if (n708 >= 128) gm_trap_shift((uint64_t)n708, "u128", gm_srcl8pn2pjtmj6k, 300, 22);
                unsigned __int128 r709 = (unsigned __int128)((unsigned __int128)((unsigned __int128)(((unsigned __int128)0u << 64) | 1u)) << (int)n708);
                bool o710 = (unsigned __int128)(r709 >> (int)n708) != ((unsigned __int128)(((unsigned __int128)0u << 64) | 1u));
                if (o710) gm_trap("integer overflow in `<<`", gm_srcl8pn2pjtmj6k, 300, 22);
                unsigned __int128 t711 = r709;
                uint8_t t712 = ((t703 == t711));
                if (!t712) goto ie702;
                {
                    void * t714 = gm_l0;
                    gm_tl874t2atnlh3 t715 = gm_l1;
                    gm_b16_8 t716;
                    GM_ST(void *, (uint8_t *)&t716, (void *)(gm_sbl3pbs9195ho));
                    GM_ST(uintptr_t, (uint8_t *)&t716 + 8, (uintptr_t)(3));
                    gm_tcfa6l7reoosn t717;
                    gm_f_checked_overflow_ma1bm1g((void *)&t717, (void *)NULL, cx, NULL, t714, t715, t716);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tcfa6l7reoosn t718 = t717;
                    if (((((uint8_t *)&t718))[0] != 0)) {
                        gm_t76123gfet73g t719 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t718) + 8));
                        gm_tcfa6l7reoosn t720;
                        GM_ST(uint8_t, (uint8_t *)&t720 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t720 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t720 + 4, 0);
                        ((uint8_t *)&t720)[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t720 + 8), t719);
                        F.ret = t720;
                        goto gm_fret;
                    }
                    gm_b0_1 t721 = GM_LD(gm_b0_1, (((uint8_t *)&t718) + 8));
                }
                {
                    void * t722 = gm_l0;
                    gm_b16_8 t723 = gm_l7;
                    unsigned __int128 t724 = gm_l46;
                    gm_f_put_uint_cddin99(t722, t723, t724);
                }
                {
                    gm_b0_1 t725;
                    gm_tcfa6l7reoosn t726;
                    GM_ST(uint8_t, (uint8_t *)&t726 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t726 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t726 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t726 + 8, 0);
                    GM_ST(uint64_t, (uint8_t *)&t726 + 16, 0);
                    GM_ST(uint64_t, (uint8_t *)&t726 + 24, 0);
                    ((uint8_t *)&t726)[0] = 0;
                    GM_ST(gm_b0_1, ((uint8_t *)&t726 + 8), t725);
                    gm_tcfa6l7reoosn t727 = t726;
                    uint8_t t728 = ((((uint8_t *)&t727))[0] != 0);
                    F.ret = t726;
                    goto gm_fret;
                }
                goto ix702;
ie702:;
ix702:;
            }
            {
                void * t729 = gm_l0;
                gm_b16_8 t730 = gm_l7;
                unsigned __int128 t731 = gm_l47;
                gm_f_put_uint_cddin99(t729, t730, t731);
            }
            goto me3;
mn631:;
ma13:;
            uint8_t t733 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t733 != ((uint8_t)20ULL)) goto mn732;
            {
                void * t735 = gm_l0;
                gm_t88rcucg1b7uk t736 = gm_l4;
                gm_tg5lql6itufk7 t737 = gm_f_int_info_ulcevdv(t735, GM_CONV(gm_csguankko4lmhd, t736));
                gm_tg5lql6itufk7 t738 = t737;
                gm_ttovhfee8mf28 t739;
                if (((((uint8_t *)&t738))[8] != 0)) {
                    gm_ttovhfee8mf28 t740 = GM_LD(gm_ttovhfee8mf28, (((uint8_t *)&t738) + 0));
                    t739 = t740;
                } else {
                    void * t741 = gm_l0;
                    gm_tl874t2atnlh3 t742 = gm_l1;
                    gm_b16_8 t743;
                    GM_ST(void *, (uint8_t *)&t743, (void *)(gm_s3gnqv0up712m));
                    GM_ST(uintptr_t, (uint8_t *)&t743 + 8, (uintptr_t)(60));
                    gm_t76123gfet73g t744;
                    gm_f_trap_qh84l9d((void *)&t744, (void *)NULL, cx, NULL, t741, t742, t743);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tcfa6l7reoosn t745;
                    GM_ST(uint8_t, (uint8_t *)&t745 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t745 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t745 + 4, 0);
                    ((uint8_t *)&t745)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t745 + 8), t744);
                    gm_tcfa6l7reoosn t746 = t745;
                    uint8_t t747 = ((((uint8_t *)&t746))[0] != 0);
                    F.ret = t745;
                    goto gm_fret;
                }
                gm_l48 = t739;
            }
            {
                void * t748 = gm_l0;
                gm_t88rcucg1b7uk t749 = gm_l4;
                uint64_t t750 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t751 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                if (t750 >= (uintptr_t)(t751)) gm_trap_index(t750, t751, gm_srcl8pn2pjtmj6k, 309, 32);
                gm_b16_8 t752 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t750 * 16));
                gm_b16_8 t753 = t752;
                gm_b16_8 t754 = GM_LD(gm_b16_8, ((uint8_t *)&t753));
                unsigned __int128 t755 = gm_f_get_int_pg7p4gc(t748, GM_CONV(gm_csguankko4lmhd, t749), t754);
                gm_l49 = t755;
            }
            {
                void * t756 = gm_l0;
                gm_t88rcucg1b7uk t757 = gm_l5;
                uint64_t t758 = (uintptr_t)((uint64_t)1ULL);
                uint64_t t759 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                if (t758 >= (uintptr_t)(t759)) gm_trap_index(t758, t759, gm_srcl8pn2pjtmj6k, 310, 32);
                gm_b16_8 t760 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t758 * 16));
                gm_b16_8 t761 = t760;
                gm_b16_8 t762 = GM_LD(gm_b16_8, ((uint8_t *)&t761));
                unsigned __int128 t763 = gm_f_get_int_pg7p4gc(t756, GM_CONV(gm_csguankko4lmhd, t757), t762);
                gm_l50 = t763;
            }
            {
                gm_l51 = ((unsigned __int128)(((unsigned __int128)0u << 64) | 0u));
            }
            {
                uint8_t t765 = GM_LD(uint8_t, (((uint8_t *)&gm_l48) + 0));
                unsigned __int128 t766 = gm_l49;
                unsigned __int128 t767 = gm_l50;
                int32_t t768 = gm_f_int_cmp_kb3sm6u(t765, t766, t767);
                uint8_t t769 = (t768 >= ((int32_t)0LL));
                if (!t769) goto ie764;
                {
                    unsigned __int128 t771 = gm_l49;
                    unsigned __int128 t772 = gm_l50;
                    unsigned __int128 r773; bool o774 = __builtin_sub_overflow(t771, t772, &r773);
                    unsigned __int128 t775 = r773;
                    gm_l51 = t775;
                }
                goto ix764;
ie764:;
                {
                    unsigned __int128 t777 = gm_l50;
                    unsigned __int128 t778 = gm_l49;
                    unsigned __int128 r779; bool o780 = __builtin_sub_overflow(t777, t778, &r779);
                    unsigned __int128 t781 = r779;
                    gm_l51 = t781;
                }
ix764:;
            }
            {
                void * t783 = gm_l0;
                gm_t88rcucg1b7uk t784 = gm_l3;
                gm_tg5lql6itufk7 t785 = gm_f_int_info_ulcevdv(t783, GM_CONV(gm_csguankko4lmhd, t784));
                gm_tg5lql6itufk7 t786 = t785;
                if (!((((uint8_t *)&t786))[8] != 0)) goto ie782;
                gm_ttovhfee8mf28 t787 = GM_LD(gm_ttovhfee8mf28, ((uint8_t *)&t786));
                gm_l52 = t787;
                {
                    uint8_t t789 = GM_LD(uint8_t, (((uint8_t *)&gm_l52) + 0));
                    uint32_t t790 = GM_LD(uint32_t, (((uint8_t *)&gm_l52) + 4));
                    unsigned __int128 t791 = gm_l51;
                    unsigned __int128 t792 = gm_f_num_ext_348kmbu(t789, t790, t791);
                    gm_l51 = t792;
                }
                goto ix782;
ie782:;
ix782:;
            }
            {
                void * t793 = gm_l0;
                gm_b16_8 t794 = gm_l7;
                unsigned __int128 t795 = gm_l51;
                gm_f_put_uint_cddin99(t793, t794, t795);
            }
            goto me3;
mn732:;
ma14:;
            uint8_t t797 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t797 != ((uint8_t)21ULL)) goto mn796;
            gm_t9qkh0rsooc8d t798 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l2) + 4));
            gm_l53 = t798;
            void * t799 = gm_l0;
            gm_tl874t2atnlh3 t800 = gm_l1;
            gm_t9qkh0rsooc8d t801 = gm_l53;
            gm_t88rcucg1b7uk t802 = gm_l4;
            gm_t88rcucg1b7uk t803 = gm_l3;
            gm_t88rcucg1b7uk t804 = gm_l5;
            uint64_t t805 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t806 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
            if (t805 >= (uintptr_t)(t806)) gm_trap_index(t805, t806, gm_srcl8pn2pjtmj6k, 320, 60);
            gm_b16_8 t807 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t805 * 16));
            gm_b16_8 t808 = t807;
            gm_b16_8 t809 = GM_LD(gm_b16_8, ((uint8_t *)&t808));
            uint64_t t810 = (uintptr_t)((uint64_t)1ULL);
            uint64_t t811 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
            if (t810 >= (uintptr_t)(t811)) gm_trap_index(t810, t811, gm_srcl8pn2pjtmj6k, 320, 66);
            gm_b16_8 t812 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t810 * 16));
            gm_b16_8 t813 = t812;
            gm_b16_8 t814 = GM_LD(gm_b16_8, ((uint8_t *)&t813));
            gm_b16_8 t815 = gm_l7;
            gm_f_pow_7je2uvi((void *)&(*(gm_tcfa6l7reoosn *)gm_out), (void *)NULL, cx, NULL, t799, t800, t801, t802, t803, t804, t809, t814, t815);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
            goto me3;
mn796:;
ma15:;
            uint8_t t819 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t819 != ((uint8_t)22ULL)) goto pa818;
            goto po817;
pa818:;
            uint8_t t821 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t821 != ((uint8_t)23ULL)) goto pa820;
            goto po817;
pa820:;
            uint8_t t823 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t823 != ((uint8_t)24ULL)) goto pa822;
            goto po817;
pa822:;
            uint8_t t825 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t825 != ((uint8_t)25ULL)) goto pa824;
            goto po817;
pa824:;
            uint8_t t827 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t827 != ((uint8_t)26ULL)) goto pa826;
            goto po817;
pa826:;
            goto mn816;
po817:;
            {
                void * t830 = gm_l0;
                gm_t88rcucg1b7uk t831 = gm_l4;
                gm_t53at80pfdus3 t832;
                gm_f_kind_htv6093((void *)&t832, t830, GM_CONV(gm_csguankko4lmhd, t831));
                gm_t53at80pfdus3 t833 = t832;
                uint8_t t834;
                uint8_t t837 = GM_LD(uint8_t, (((uint8_t *)&t833) + 0));
                if (t837 != ((uint8_t)3ULL)) goto mn836;
                t834 = ((uint8_t)1);
                goto me835;
mn836:;
                t834 = ((uint8_t)0);
                goto me835;
mn838:;
                gm_trap("no match arm matched the scrutinee", gm_srcl8pn2pjtmj6k, 322, 21);
me835:;
                uint8_t t839 = (uint8_t)!t834;
                if (!t839) goto ie829;
                {
                    void * t841 = gm_l0;
                    gm_tl874t2atnlh3 t842 = gm_l1;
                    gm_b16_8 t843;
                    GM_ST(void *, (uint8_t *)&t843, (void *)(gm_s9h8lp3gkedth));
                    GM_ST(uintptr_t, (uint8_t *)&t843 + 8, (uintptr_t)(28));
                    gm_t76123gfet73g t844;
                    gm_f_trap_qh84l9d((void *)&t844, (void *)NULL, cx, NULL, t841, t842, t843);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tcfa6l7reoosn t845;
                    GM_ST(uint8_t, (uint8_t *)&t845 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t845 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t845 + 4, 0);
                    ((uint8_t *)&t845)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t845 + 8), t844);
                    gm_tcfa6l7reoosn t846 = t845;
                    uint8_t t847 = ((((uint8_t *)&t846))[0] != 0);
                    F.ret = t845;
                    goto gm_fret;
                }
                goto ix829;
ie829:;
ix829:;
            }
            {
                void * t848 = gm_l0;
                gm_t88rcucg1b7uk t849 = gm_l4;
                uint64_t t850 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t851 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                if (t850 >= (uintptr_t)(t851)) gm_trap_index(t850, t851, gm_srcl8pn2pjtmj6k, 323, 34);
                gm_b16_8 t852 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t850 * 16));
                gm_b16_8 t853 = t852;
                gm_b16_8 t854 = GM_LD(gm_b16_8, ((uint8_t *)&t853));
                double t855 = gm_f_get_float_grmhlm4(t848, GM_CONV(gm_csguankko4lmhd, t849), t854);
                gm_l54 = t855;
            }
            {
                double t856 = gm_l54;
                double t857 = t856;
                uint64_t t858;
                memset(&t858, 0, sizeof t858); memcpy(&t858, ((uint8_t *)&t857), sizeof t858 < 8 ? sizeof t858 : 8);
                gm_l55 = t858;
            }
            {
                uint64_t t859 = gm_l55;
                unsigned __int128 n860 = (unsigned __int128)((uint64_t)52ULL);
                if (n860 >= 64) gm_trap_shift((uint64_t)n860, "u64", gm_srcl8pn2pjtmj6k, 325, 21);
                uint64_t t861 = (uint64_t)(t859 >> (int)n860);
                uint64_t t862 = (uint64_t)(t861 & ((uint64_t)2047ULL));
                gm_l56 = t862;
            }
            {
                uint8_t t870 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                switch (t870) { case ((uint8_t)0ULL): goto ma868; case ((uint8_t)1ULL): goto ma868; case ((uint8_t)2ULL): goto ma868; case ((uint8_t)3ULL): goto ma868; case ((uint8_t)4ULL): goto ma868; case ((uint8_t)5ULL): goto ma868; case ((uint8_t)6ULL): goto ma868; case ((uint8_t)7ULL): goto ma868; case ((uint8_t)8ULL): goto ma868; case ((uint8_t)9ULL): goto ma868; case ((uint8_t)10ULL): goto ma868; case ((uint8_t)11ULL): goto ma868; case ((uint8_t)12ULL): goto ma868; case ((uint8_t)13ULL): goto ma868; case ((uint8_t)14ULL): goto ma868; case ((uint8_t)15ULL): goto ma868; case ((uint8_t)16ULL): goto ma868; case ((uint8_t)17ULL): goto ma868; case ((uint8_t)18ULL): goto ma868; case ((uint8_t)19ULL): goto ma868; case ((uint8_t)20ULL): goto ma868; case ((uint8_t)21ULL): goto ma868; case ((uint8_t)22ULL): goto ma864; case ((uint8_t)23ULL): goto ma865; case ((uint8_t)24ULL): goto ma866; case ((uint8_t)25ULL): goto ma867; case ((uint8_t)26ULL): goto ma868; case ((uint8_t)27ULL): goto ma868; case ((uint8_t)28ULL): goto ma868; case ((uint8_t)29ULL): goto ma868; case ((uint8_t)30ULL): goto ma868; case ((uint8_t)31ULL): goto ma868; case ((uint8_t)32ULL): goto ma868; case ((uint8_t)33ULL): goto ma868; case ((uint8_t)34ULL): goto ma868; case ((uint8_t)35ULL): goto ma868; case ((uint8_t)36ULL): goto ma868; case ((uint8_t)37ULL): goto ma868; case ((uint8_t)38ULL): goto ma868; case ((uint8_t)39ULL): goto ma868; case ((uint8_t)40ULL): goto ma868; case ((uint8_t)41ULL): goto ma868; case ((uint8_t)42ULL): goto ma868; case ((uint8_t)43ULL): goto ma868; case ((uint8_t)44ULL): goto ma868; case ((uint8_t)45ULL): goto ma868; case ((uint8_t)46ULL): goto ma868; case ((uint8_t)47ULL): goto ma868; case ((uint8_t)48ULL): goto ma868; case ((uint8_t)49ULL): goto ma868; case ((uint8_t)50ULL): goto ma868; case ((uint8_t)51ULL): goto ma868; case ((uint8_t)52ULL): goto ma868; case ((uint8_t)53ULL): goto ma868; case ((uint8_t)54ULL): goto ma868; case ((uint8_t)55ULL): goto ma868; case ((uint8_t)56ULL): goto ma868; case ((uint8_t)57ULL): goto ma868; case ((uint8_t)58ULL): goto ma868; case ((uint8_t)59ULL): goto ma868; case ((uint8_t)60ULL): goto ma868; case ((uint8_t)61ULL): goto ma868; case ((uint8_t)62ULL): goto ma868; case ((uint8_t)63ULL): goto ma868; case ((uint8_t)64ULL): goto ma868; case ((uint8_t)65ULL): goto ma868; case ((uint8_t)66ULL): goto ma868; case ((uint8_t)67ULL): goto ma868; case ((uint8_t)68ULL): goto ma868; case ((uint8_t)69ULL): goto ma868; case ((uint8_t)70ULL): goto ma868; case ((uint8_t)71ULL): goto ma868; case ((uint8_t)72ULL): goto ma868; case ((uint8_t)73ULL): goto ma868; case ((uint8_t)74ULL): goto ma868; case ((uint8_t)75ULL): goto ma868; case ((uint8_t)76ULL): goto ma868; case ((uint8_t)77ULL): goto ma868; case ((uint8_t)78ULL): goto ma868; case ((uint8_t)79ULL): goto ma868; case ((uint8_t)80ULL): goto ma868; case ((uint8_t)81ULL): goto ma868; default: goto ma868; }
ma864:;
                uint8_t t872 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                if (t872 != ((uint8_t)22ULL)) goto mn871;
                {
                    uint64_t t874 = (uintptr_t)((uint64_t)0ULL);
                    uint64_t t875 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                    if (t874 >= (uintptr_t)(t875)) gm_trap_index(t874, t875, gm_srcl8pn2pjtmj6k, 327, 29);
                    uint8_t *t876 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l7)) + t874 * 1);
                    uint8_t t877;
                    uint64_t t879 = gm_l56;
                    uint8_t t880 = ((t879 == ((uint64_t)2047ULL)));
                    if (!t880) goto ie878;
                    uint64_t t881 = gm_l55;
                    uint64_t t882 = (uint64_t)(t881 & ((uint64_t)4503599627370495ULL));
                    uint8_t t883 = (!(t882 == ((uint64_t)0ULL)));
                    if (!t883) goto ie878;
                    uint8_t t885 = ((uint8_t)1ULL);
                    t877 = t885;
                    goto ix878;
ie878:;
                    uint8_t t887 = ((uint8_t)0ULL);
                    t877 = t887;
ix878:;
                    GM_ST(uint8_t, t876, t877);
                }
                goto me863;
mn871:;
ma865:;
                uint8_t t889 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                if (t889 != ((uint8_t)23ULL)) goto mn888;
                {
                    uint64_t t891 = (uintptr_t)((uint64_t)0ULL);
                    uint64_t t892 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                    if (t891 >= (uintptr_t)(t892)) gm_trap_index(t891, t892, gm_srcl8pn2pjtmj6k, 328, 29);
                    uint8_t *t893 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l7)) + t891 * 1);
                    uint8_t t894;
                    uint64_t t896 = gm_l56;
                    uint8_t t897 = ((t896 == ((uint64_t)2047ULL)));
                    if (!t897) goto ie895;
                    uint64_t t898 = gm_l55;
                    uint64_t t899 = (uint64_t)(t898 & ((uint64_t)4503599627370495ULL));
                    uint8_t t900 = ((t899 == ((uint64_t)0ULL)));
                    if (!t900) goto ie895;
                    uint8_t t902 = ((uint8_t)1ULL);
                    t894 = t902;
                    goto ix895;
ie895:;
                    uint8_t t904 = ((uint8_t)0ULL);
                    t894 = t904;
ix895:;
                    GM_ST(uint8_t, t893, t894);
                }
                goto me863;
mn888:;
ma866:;
                uint8_t t906 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                if (t906 != ((uint8_t)24ULL)) goto mn905;
                {
                    uint64_t t908 = (uintptr_t)((uint64_t)0ULL);
                    uint64_t t909 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                    if (t908 >= (uintptr_t)(t909)) gm_trap_index(t908, t909, gm_srcl8pn2pjtmj6k, 329, 32);
                    uint8_t *t910 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l7)) + t908 * 1);
                    uint8_t t911;
                    uint64_t t913 = gm_l56;
                    uint8_t t914 = ((t913 == ((uint64_t)2047ULL)));
                    if (!t914) goto ie912;
                    uint8_t t916 = ((uint8_t)0ULL);
                    t911 = t916;
                    goto ix912;
ie912:;
                    uint8_t t918 = ((uint8_t)1ULL);
                    t911 = t918;
ix912:;
                    GM_ST(uint8_t, t910, t911);
                }
                goto me863;
mn905:;
ma867:;
                uint8_t t920 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                if (t920 != ((uint8_t)25ULL)) goto mn919;
                {
                    uint64_t t922 = (uintptr_t)((uint64_t)0ULL);
                    uint64_t t923 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                    if (t922 >= (uintptr_t)(t923)) gm_trap_index(t922, t923, gm_srcl8pn2pjtmj6k, 330, 31);
                    uint8_t *t924 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l7)) + t922 * 1);
                    uint8_t t925;
                    uint64_t t927 = gm_l55;
                    unsigned __int128 n928 = (unsigned __int128)((uint64_t)63ULL);
                    if (n928 >= 64) gm_trap_shift((uint64_t)n928, "u64", gm_srcl8pn2pjtmj6k, 330, 44);
                    uint64_t t929 = (uint64_t)(t927 >> (int)n928);
                    uint8_t t930 = ((t929 == ((uint64_t)1ULL)));
                    if (!t930) goto ie926;
                    uint8_t t932 = ((uint8_t)1ULL);
                    t925 = t932;
                    goto ix926;
ie926:;
                    uint8_t t934 = ((uint8_t)0ULL);
                    t925 = t934;
ix926:;
                    GM_ST(uint8_t, t924, t925);
                }
                goto me863;
mn919:;
ma868:;
                void * t936 = gm_l0;
                gm_t88rcucg1b7uk t937 = gm_l4;
                double t938 = gm_l54;
                double t939 = (double)(__builtin_sqrt(t938));
                gm_b16_8 t940 = gm_l7;
                gm_f_put_float_fiad72d(t936, GM_CONV(gm_csguankko4lmhd, t937), t939, t940);
                goto me863;
mn935:;
ma869:;
                gm_trap("no match arm matched the scrutinee", gm_srcl8pn2pjtmj6k, 326, 13);
me863:;
            }
            goto me3;
mn816:;
ma16:;
            uint8_t t942 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t942 != ((uint8_t)27ULL)) goto mn941;
            {
                void * t945 = gm_l0;
                gm_t88rcucg1b7uk t946 = gm_l4;
                gm_t53at80pfdus3 t947;
                gm_f_kind_htv6093((void *)&t947, t945, GM_CONV(gm_csguankko4lmhd, t946));
                gm_t53at80pfdus3 t948 = t947;
                uint8_t t949 = GM_LD(uint8_t, (((uint8_t *)&t948) + 0));
                if (t949 != ((uint8_t)3ULL)) goto ie944;
                uint16_t t950 = GM_LD(uint16_t, (((uint8_t *)&t948) + 8));
                gm_l57 = t950;
                {
                    void * t952 = gm_l0;
                    gm_t88rcucg1b7uk t953 = gm_l4;
                    uint64_t t954 = (uintptr_t)((uint64_t)0ULL);
                    uint64_t t955 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                    if (t954 >= (uintptr_t)(t955)) gm_trap_index(t954, t955, gm_srcl8pn2pjtmj6k, 336, 34);
                    gm_b16_8 t956 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t954 * 16));
                    gm_b16_8 t957 = t956;
                    gm_b16_8 t958 = GM_LD(gm_b16_8, ((uint8_t *)&t957));
                    double t959 = gm_f_get_float_grmhlm4(t952, GM_CONV(gm_csguankko4lmhd, t953), t958);
                    gm_l58 = t959;
                }
                {
                    void * t960 = gm_l0;
                    gm_t88rcucg1b7uk t961 = gm_l4;
                    uint64_t t962 = (uintptr_t)((uint64_t)1ULL);
                    uint64_t t963 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                    if (t962 >= (uintptr_t)(t963)) gm_trap_index(t962, t963, gm_srcl8pn2pjtmj6k, 337, 34);
                    gm_b16_8 t964 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t962 * 16));
                    gm_b16_8 t965 = t964;
                    gm_b16_8 t966 = GM_LD(gm_b16_8, ((uint8_t *)&t965));
                    double t967 = gm_f_get_float_grmhlm4(t960, GM_CONV(gm_csguankko4lmhd, t961), t966);
                    gm_l59 = t967;
                }
                {
                    void * t968 = gm_l0;
                    gm_t88rcucg1b7uk t969 = gm_l4;
                    uint64_t t970 = (uintptr_t)((uint64_t)2ULL);
                    uint64_t t971 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                    if (t970 >= (uintptr_t)(t971)) gm_trap_index(t970, t971, gm_srcl8pn2pjtmj6k, 338, 34);
                    gm_b16_8 t972 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t970 * 16));
                    gm_b16_8 t973 = t972;
                    gm_b16_8 t974 = GM_LD(gm_b16_8, ((uint8_t *)&t973));
                    double t975 = gm_f_get_float_grmhlm4(t968, GM_CONV(gm_csguankko4lmhd, t969), t974);
                    gm_l60 = t975;
                }
                {
                    uint16_t t977 = gm_l57;
                    uint8_t t978 = ((t977 == ((uint16_t)32ULL)));
                    if (!t978) goto ie976;
                    {
                        void * t980 = gm_l0;
                        gm_t88rcucg1b7uk t981 = gm_l4;
                        double t982 = gm_l58;
                        float t983 = (float)((double)t982);
                        double t984 = gm_l59;
                        float t985 = (float)((double)t984);
                        double t986 = gm_l60;
                        float t987 = (float)((double)t986);
                        float t988 = (float)(__builtin_fmaf(t983, t985, t987));
                        double t989 = (double)t988;
                        gm_b16_8 t990 = gm_l7;
                        gm_f_put_float_fiad72d(t980, GM_CONV(gm_csguankko4lmhd, t981), t989, t990);
                    }
                    goto ix976;
ie976:;
                    {
                        void * t992 = gm_l0;
                        gm_t88rcucg1b7uk t993 = gm_l4;
                        double t994 = gm_l58;
                        double t995 = gm_l59;
                        double t996 = gm_l60;
                        double t997 = (double)(__builtin_fma(t994, t995, t996));
                        gm_b16_8 t998 = gm_l7;
                        gm_f_put_float_fiad72d(t992, GM_CONV(gm_csguankko4lmhd, t993), t997, t998);
                    }
ix976:;
                }
                goto ix944;
ie944:;
                {
                    void * t1000 = gm_l0;
                    gm_tl874t2atnlh3 t1001 = gm_l1;
                    gm_b16_8 t1002;
                    GM_ST(void *, (uint8_t *)&t1002, (void *)(gm_sh01rb1u2jpfs));
                    GM_ST(uintptr_t, (uint8_t *)&t1002 + 8, (uintptr_t)(18));
                    gm_t76123gfet73g t1003;
                    gm_f_trap_qh84l9d((void *)&t1003, (void *)NULL, cx, NULL, t1000, t1001, t1002);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tcfa6l7reoosn t1004;
                    GM_ST(uint8_t, (uint8_t *)&t1004 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t1004 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t1004 + 4, 0);
                    ((uint8_t *)&t1004)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t1004 + 8), t1003);
                    gm_tcfa6l7reoosn t1005 = t1004;
                    uint8_t t1006 = ((((uint8_t *)&t1005))[0] != 0);
                    F.ret = t1004;
                    goto gm_fret;
                }
ix944:;
            }
            goto me3;
mn941:;
ma17:;
            uint8_t t1010 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t1010 != ((uint8_t)32ULL)) goto pa1009;
            goto po1008;
pa1009:;
            uint8_t t1012 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t1012 != ((uint8_t)33ULL)) goto pa1011;
            goto po1008;
pa1011:;
            goto mn1007;
po1008:;
            {
                void * t1014 = gm_l0;
                gm_t88rcucg1b7uk t1015 = gm_l4;
                gm_tg5lql6itufk7 t1016 = gm_f_int_info_ulcevdv(t1014, GM_CONV(gm_csguankko4lmhd, t1015));
                gm_tg5lql6itufk7 t1017 = t1016;
                gm_ttovhfee8mf28 t1018;
                if (((((uint8_t *)&t1017))[8] != 0)) {
                    gm_ttovhfee8mf28 t1019 = GM_LD(gm_ttovhfee8mf28, (((uint8_t *)&t1017) + 0));
                    t1018 = t1019;
                } else {
                    void * t1020 = gm_l0;
                    gm_tl874t2atnlh3 t1021 = gm_l1;
                    gm_b16_8 t1022;
                    GM_ST(void *, (uint8_t *)&t1022, (void *)(gm_s26c80h2pm6a7));
                    GM_ST(uintptr_t, (uint8_t *)&t1022 + 8, (uintptr_t)(25));
                    gm_t76123gfet73g t1023;
                    gm_f_trap_qh84l9d((void *)&t1023, (void *)NULL, cx, NULL, t1020, t1021, t1022);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tcfa6l7reoosn t1024;
                    GM_ST(uint8_t, (uint8_t *)&t1024 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t1024 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t1024 + 4, 0);
                    ((uint8_t *)&t1024)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t1024 + 8), t1023);
                    gm_tcfa6l7reoosn t1025 = t1024;
                    uint8_t t1026 = ((((uint8_t *)&t1025))[0] != 0);
                    F.ret = t1024;
                    goto gm_fret;
                }
                gm_l61 = t1018;
            }
            {
                void * t1027 = gm_l0;
                gm_t88rcucg1b7uk t1028 = gm_l5;
                gm_tg5lql6itufk7 t1029 = gm_f_int_info_ulcevdv(t1027, GM_CONV(gm_csguankko4lmhd, t1028));
                gm_tg5lql6itufk7 t1030 = t1029;
                gm_ttovhfee8mf28 t1031;
                if (((((uint8_t *)&t1030))[8] != 0)) {
                    gm_ttovhfee8mf28 t1032 = GM_LD(gm_ttovhfee8mf28, (((uint8_t *)&t1030) + 0));
                    t1031 = t1032;
                } else {
                    void * t1033 = gm_l0;
                    gm_tl874t2atnlh3 t1034 = gm_l1;
                    gm_b16_8 t1035;
                    GM_ST(void *, (uint8_t *)&t1035, (void *)(gm_sti4tkgslrkgo));
                    GM_ST(uintptr_t, (uint8_t *)&t1035 + 8, (uintptr_t)(25));
                    gm_t76123gfet73g t1036;
                    gm_f_trap_qh84l9d((void *)&t1036, (void *)NULL, cx, NULL, t1033, t1034, t1035);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tcfa6l7reoosn t1037;
                    GM_ST(uint8_t, (uint8_t *)&t1037 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t1037 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t1037 + 4, 0);
                    ((uint8_t *)&t1037)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t1037 + 8), t1036);
                    gm_tcfa6l7reoosn t1038 = t1037;
                    uint8_t t1039 = ((((uint8_t *)&t1038))[0] != 0);
                    F.ret = t1037;
                    goto gm_fret;
                }
                gm_l62 = t1031;
            }
            {
                uint32_t t1040 = GM_LD(uint32_t, (((uint8_t *)&gm_l61) + 4));
                gm_l63 = t1040;
            }
            {
                void * t1041 = gm_l0;
                uint64_t t1042 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t1043 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                if (t1042 >= (uintptr_t)(t1043)) gm_trap_index(t1042, t1043, gm_srcl8pn2pjtmj6k, 349, 29);
                gm_b16_8 t1044 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t1042 * 16));
                gm_b16_8 t1045 = t1044;
                uint64_t t1046 = GM_LD(uintptr_t, ((uint8_t *)&t1045) + 8);
                void * t1047 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t1045)));
                uint64_t t1048 = (uintptr_t)((uint64_t)0ULL);
                void * t1049 = gm_l0;
                gm_t88rcucg1b7uk t1050 = gm_l4;
                uint64_t t1051 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t1049, t1050);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint64_t t1052 = gm_f_us_cuv01fp(t1051);
                uint64_t t1053 = (uintptr_t)t1052;
                if (t1048 > t1053 || t1053 > (uintptr_t)t1046) gm_trap_slice(t1048, t1053, t1046, gm_srcl8pn2pjtmj6k, 349, 29);
                gm_b16_8 t1054;
                GM_ST(void *, (uint8_t *)&t1054, (void *)(((uint8_t *)t1047 + t1048 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t1054 + 8, (uintptr_t)(t1053 - t1048));
                gm_b16_8 t1055 = t1054;
                gm_b16_8 t1056 = GM_LD(gm_b16_8, ((uint8_t *)&t1055));
                unsigned __int128 t1057 = gm_f_get_uint_bq8c4b7(t1041, t1056);
                uint32_t t1058 = gm_l63;
                unsigned __int128 t1059 = gm_f_num_mask_do3naah(t1058);
                unsigned __int128 t1060 = (unsigned __int128)(t1057 & t1059);
                gm_l64 = t1060;
            }
            {
                void * t1061 = gm_l0;
                gm_t88rcucg1b7uk t1062 = gm_l5;
                uint64_t t1063 = (uintptr_t)((uint64_t)1ULL);
                uint64_t t1064 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                if (t1063 >= (uintptr_t)(t1064)) gm_trap_index(t1063, t1064, gm_srcl8pn2pjtmj6k, 350, 35);
                gm_b16_8 t1065 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t1063 * 16));
                gm_b16_8 t1066 = t1065;
                gm_b16_8 t1067 = GM_LD(gm_b16_8, ((uint8_t *)&t1066));
                unsigned __int128 t1068 = gm_f_get_int_pg7p4gc(t1061, GM_CONV(gm_csguankko4lmhd, t1062), t1067);
                gm_l65 = t1068;
            }
            {
                gm_l66 = ((uint32_t)0ULL);
            }
            {
                uint8_t t1070 = 0;
                uint8_t t1071 = GM_LD(uint8_t, (((uint8_t *)&gm_l62) + 0));
                if (t1071) {
                    unsigned __int128 t1072 = gm_l65;
                    unsigned __int128 n1073 = (unsigned __int128)((uint64_t)127ULL);
                    if (n1073 >= 128) gm_trap_shift((uint64_t)n1073, "u128", gm_srcl8pn2pjtmj6k, 352, 39);
                    unsigned __int128 t1074 = (unsigned __int128)(t1072 >> (int)n1073);
                    uint8_t t1075 = ((t1074 == ((unsigned __int128)(((unsigned __int128)0u << 64) | 1u))));
                    t1070 = t1075;
                } else t1070 = 0;
                uint8_t t1076 = (uint8_t)!t1070;
                if (!t1076) goto ie1069;
                {
                    unsigned __int128 t1078 = gm_l65;
                    uint32_t t1079 = gm_l63;
                    unsigned __int128 t1080 = (unsigned __int128)t1079;
                    if (t1080 == 0) gm_trap("division by zero", gm_srcl8pn2pjtmj6k, 352, 65);
                    unsigned __int128 t1081 = (unsigned __int128)(t1078 % t1080);
                    uint32_t t1082 = (uint32_t)t1081;
                    gm_l66 = t1082;
                }
                goto ix1069;
ie1069:;
ix1069:;
            }
            {
                uint8_t t1084 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l2) + 0) == ((uint8_t)33ULL)));
                if (!t1084) goto ie1083;
                {
                    uint32_t t1086 = gm_l63;
                    uint32_t t1087 = gm_l66;
                    uint32_t r1088; bool o1089 = __builtin_sub_overflow(t1086, t1087, &r1088);
                    if (o1089) gm_trap("integer overflow in `-`", gm_srcl8pn2pjtmj6k, 353, 35);
                    uint32_t t1090 = r1088;
                    uint32_t t1091 = gm_l63;
                    if (t1091 == 0) gm_trap("division by zero", gm_srcl8pn2pjtmj6k, 353, 34);
                    uint32_t t1092 = (uint32_t)(t1090 % t1091);
                    gm_l66 = t1092;
                }
                goto ix1083;
ie1083:;
ix1083:;
            }
            {
                unsigned __int128 t1093;
                uint32_t t1095 = gm_l66;
                uint8_t t1096 = ((t1095 == ((uint32_t)0ULL)));
                if (!t1096) goto ie1094;
                unsigned __int128 t1098 = gm_l64;
                unsigned __int128 t1099 = t1098;
                t1093 = t1099;
                goto ix1094;
ie1094:;
                unsigned __int128 t1101 = gm_l64;
                uint32_t t1102 = gm_l66;
                unsigned __int128 t1103 = (unsigned __int128)t1102;
                unsigned __int128 n1104 = (unsigned __int128)t1103;
                if (n1104 >= 128) gm_trap_shift((uint64_t)n1104, "u128", gm_srcl8pn2pjtmj6k, 354, 43);
                unsigned __int128 r1105 = (unsigned __int128)((unsigned __int128)t1101 << (int)n1104);
                bool o1106 = (unsigned __int128)(r1105 >> (int)n1104) != t1101;
                unsigned __int128 t1107 = r1105;
                unsigned __int128 t1108 = gm_l64;
                uint32_t t1109 = gm_l63;
                uint32_t t1110 = gm_l66;
                uint32_t r1111; bool o1112 = __builtin_sub_overflow(t1109, t1110, &r1111);
                if (o1112) gm_trap("integer overflow in `-`", gm_srcl8pn2pjtmj6k, 354, 71);
                uint32_t t1113 = r1111;
                unsigned __int128 t1114 = (unsigned __int128)t1113;
                unsigned __int128 n1115 = (unsigned __int128)t1114;
                if (n1115 >= 128) gm_trap_shift((uint64_t)n1115, "u128", gm_srcl8pn2pjtmj6k, 354, 61);
                unsigned __int128 t1116 = (unsigned __int128)(t1108 >> (int)n1115);
                unsigned __int128 t1117 = (unsigned __int128)(t1107 | t1116);
                uint32_t t1118 = gm_l63;
                unsigned __int128 t1119 = gm_f_num_mask_do3naah(t1118);
                unsigned __int128 t1120 = (unsigned __int128)(t1117 & t1119);
                unsigned __int128 t1121 = t1120;
                t1093 = t1121;
ix1094:;
                gm_l67 = t1093;
            }
            {
                void * t1122 = gm_l0;
                gm_b16_8 t1123 = gm_l7;
                uint8_t t1124 = GM_LD(uint8_t, (((uint8_t *)&gm_l61) + 0));
                uint32_t t1125 = gm_l63;
                unsigned __int128 t1126 = gm_l67;
                unsigned __int128 t1127 = gm_f_num_ext_348kmbu(t1124, t1125, t1126);
                gm_f_put_uint_cddin99(t1122, t1123, t1127);
            }
            goto me3;
mn1007:;
ma18:;
            uint8_t t1131 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t1131 != ((uint8_t)28ULL)) goto pa1130;
            goto po1129;
pa1130:;
            uint8_t t1133 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t1133 != ((uint8_t)29ULL)) goto pa1132;
            goto po1129;
pa1132:;
            uint8_t t1135 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t1135 != ((uint8_t)30ULL)) goto pa1134;
            goto po1129;
pa1134:;
            uint8_t t1137 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t1137 != ((uint8_t)31ULL)) goto pa1136;
            goto po1129;
pa1136:;
            uint8_t t1139 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t1139 != ((uint8_t)34ULL)) goto pa1138;
            goto po1129;
pa1138:;
            goto mn1128;
po1129:;
            {
                void * t1141 = gm_l0;
                gm_t88rcucg1b7uk t1142 = gm_l4;
                gm_tg5lql6itufk7 t1143 = gm_f_int_info_ulcevdv(t1141, GM_CONV(gm_csguankko4lmhd, t1142));
                gm_tg5lql6itufk7 t1144 = t1143;
                gm_ttovhfee8mf28 t1145;
                if (((((uint8_t *)&t1144))[8] != 0)) {
                    gm_ttovhfee8mf28 t1146 = GM_LD(gm_ttovhfee8mf28, (((uint8_t *)&t1144) + 0));
                    t1145 = t1146;
                } else {
                    void * t1147 = gm_l0;
                    gm_tl874t2atnlh3 t1148 = gm_l1;
                    gm_b16_8 t1149;
                    GM_ST(void *, (uint8_t *)&t1149, (void *)(gm_spqf88ftq0gqk));
                    GM_ST(uintptr_t, (uint8_t *)&t1149 + 8, (uintptr_t)(28));
                    gm_t76123gfet73g t1150;
                    gm_f_trap_qh84l9d((void *)&t1150, (void *)NULL, cx, NULL, t1147, t1148, t1149);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tcfa6l7reoosn t1151;
                    GM_ST(uint8_t, (uint8_t *)&t1151 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t1151 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t1151 + 4, 0);
                    ((uint8_t *)&t1151)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t1151 + 8), t1150);
                    gm_tcfa6l7reoosn t1152 = t1151;
                    uint8_t t1153 = ((((uint8_t *)&t1152))[0] != 0);
                    F.ret = t1151;
                    goto gm_fret;
                }
                gm_l68 = t1145;
            }
            {
                uint32_t t1154 = GM_LD(uint32_t, (((uint8_t *)&gm_l68) + 4));
                gm_l69 = t1154;
            }
            {
                void * t1155 = gm_l0;
                uint64_t t1156 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t1157 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                if (t1156 >= (uintptr_t)(t1157)) gm_trap_index(t1156, t1157, gm_srcl8pn2pjtmj6k, 360, 29);
                gm_b16_8 t1158 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t1156 * 16));
                gm_b16_8 t1159 = t1158;
                uint64_t t1160 = GM_LD(uintptr_t, ((uint8_t *)&t1159) + 8);
                void * t1161 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t1159)));
                uint64_t t1162 = (uintptr_t)((uint64_t)0ULL);
                void * t1163 = gm_l0;
                gm_t88rcucg1b7uk t1164 = gm_l4;
                uint64_t t1165 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t1163, t1164);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint64_t t1166 = gm_f_us_cuv01fp(t1165);
                uint64_t t1167 = (uintptr_t)t1166;
                if (t1162 > t1167 || t1167 > (uintptr_t)t1160) gm_trap_slice(t1162, t1167, t1160, gm_srcl8pn2pjtmj6k, 360, 29);
                gm_b16_8 t1168;
                GM_ST(void *, (uint8_t *)&t1168, (void *)(((uint8_t *)t1161 + t1162 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t1168 + 8, (uintptr_t)(t1167 - t1162));
                gm_b16_8 t1169 = t1168;
                gm_b16_8 t1170 = GM_LD(gm_b16_8, ((uint8_t *)&t1169));
                unsigned __int128 t1171 = gm_f_get_uint_bq8c4b7(t1155, t1170);
                uint32_t t1172 = gm_l69;
                unsigned __int128 t1173 = gm_f_num_mask_do3naah(t1172);
                unsigned __int128 t1174 = (unsigned __int128)(t1171 & t1173);
                gm_l70 = t1174;
            }
            {
                gm_l71 = ((unsigned __int128)(((unsigned __int128)0u << 64) | 0u));
            }
            {
                uint8_t t1182 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                switch (t1182) { case ((uint8_t)0ULL): goto ma1180; case ((uint8_t)1ULL): goto ma1180; case ((uint8_t)2ULL): goto ma1180; case ((uint8_t)3ULL): goto ma1180; case ((uint8_t)4ULL): goto ma1180; case ((uint8_t)5ULL): goto ma1180; case ((uint8_t)6ULL): goto ma1180; case ((uint8_t)7ULL): goto ma1180; case ((uint8_t)8ULL): goto ma1180; case ((uint8_t)9ULL): goto ma1180; case ((uint8_t)10ULL): goto ma1180; case ((uint8_t)11ULL): goto ma1180; case ((uint8_t)12ULL): goto ma1180; case ((uint8_t)13ULL): goto ma1180; case ((uint8_t)14ULL): goto ma1180; case ((uint8_t)15ULL): goto ma1180; case ((uint8_t)16ULL): goto ma1180; case ((uint8_t)17ULL): goto ma1180; case ((uint8_t)18ULL): goto ma1180; case ((uint8_t)19ULL): goto ma1180; case ((uint8_t)20ULL): goto ma1180; case ((uint8_t)21ULL): goto ma1180; case ((uint8_t)22ULL): goto ma1180; case ((uint8_t)23ULL): goto ma1180; case ((uint8_t)24ULL): goto ma1180; case ((uint8_t)25ULL): goto ma1180; case ((uint8_t)26ULL): goto ma1180; case ((uint8_t)27ULL): goto ma1180; case ((uint8_t)28ULL): goto ma1176; case ((uint8_t)29ULL): goto ma1177; case ((uint8_t)30ULL): goto ma1178; case ((uint8_t)31ULL): goto ma1179; case ((uint8_t)32ULL): goto ma1180; case ((uint8_t)33ULL): goto ma1180; case ((uint8_t)34ULL): goto ma1180; case ((uint8_t)35ULL): goto ma1180; case ((uint8_t)36ULL): goto ma1180; case ((uint8_t)37ULL): goto ma1180; case ((uint8_t)38ULL): goto ma1180; case ((uint8_t)39ULL): goto ma1180; case ((uint8_t)40ULL): goto ma1180; case ((uint8_t)41ULL): goto ma1180; case ((uint8_t)42ULL): goto ma1180; case ((uint8_t)43ULL): goto ma1180; case ((uint8_t)44ULL): goto ma1180; case ((uint8_t)45ULL): goto ma1180; case ((uint8_t)46ULL): goto ma1180; case ((uint8_t)47ULL): goto ma1180; case ((uint8_t)48ULL): goto ma1180; case ((uint8_t)49ULL): goto ma1180; case ((uint8_t)50ULL): goto ma1180; case ((uint8_t)51ULL): goto ma1180; case ((uint8_t)52ULL): goto ma1180; case ((uint8_t)53ULL): goto ma1180; case ((uint8_t)54ULL): goto ma1180; case ((uint8_t)55ULL): goto ma1180; case ((uint8_t)56ULL): goto ma1180; case ((uint8_t)57ULL): goto ma1180; case ((uint8_t)58ULL): goto ma1180; case ((uint8_t)59ULL): goto ma1180; case ((uint8_t)60ULL): goto ma1180; case ((uint8_t)61ULL): goto ma1180; case ((uint8_t)62ULL): goto ma1180; case ((uint8_t)63ULL): goto ma1180; case ((uint8_t)64ULL): goto ma1180; case ((uint8_t)65ULL): goto ma1180; case ((uint8_t)66ULL): goto ma1180; case ((uint8_t)67ULL): goto ma1180; case ((uint8_t)68ULL): goto ma1180; case ((uint8_t)69ULL): goto ma1180; case ((uint8_t)70ULL): goto ma1180; case ((uint8_t)71ULL): goto ma1180; case ((uint8_t)72ULL): goto ma1180; case ((uint8_t)73ULL): goto ma1180; case ((uint8_t)74ULL): goto ma1180; case ((uint8_t)75ULL): goto ma1180; case ((uint8_t)76ULL): goto ma1180; case ((uint8_t)77ULL): goto ma1180; case ((uint8_t)78ULL): goto ma1180; case ((uint8_t)79ULL): goto ma1180; case ((uint8_t)80ULL): goto ma1180; case ((uint8_t)81ULL): goto ma1180; default: goto ma1180; }
ma1176:;
                uint8_t t1184 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                if (t1184 != ((uint8_t)28ULL)) goto mn1183;
                {
                    void * t1186 = gm_l0;
                    gm_b16_8 t1187 = gm_l7;
                    unsigned __int128 t1188 = gm_l70;
                    uint32_t t1189 = (uint32_t)gm_clz((unsigned __int128)(unsigned __int128)(t1188), 128);
                    uint32_t t1190 = gm_l69;
                    uint32_t r1191; bool o1192 = __builtin_sub_overflow(((uint32_t)128ULL), t1190, &r1191);
                    if (o1192) gm_trap("integer overflow in `-`", gm_srcl8pn2pjtmj6k, 364, 53);
                    uint32_t t1193 = r1191;
                    uint32_t r1194; bool o1195 = __builtin_sub_overflow(t1189, t1193, &r1194);
                    if (o1195) gm_trap("integer overflow in `-`", gm_srcl8pn2pjtmj6k, 364, 42);
                    uint32_t t1196 = r1194;
                    unsigned __int128 t1197 = (unsigned __int128)t1196;
                    gm_f_put_uint_cddin99(t1186, t1187, t1197);
                }
                {
                    gm_b0_1 t1198;
                    gm_tcfa6l7reoosn t1199;
                    GM_ST(uint8_t, (uint8_t *)&t1199 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t1199 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t1199 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t1199 + 8, 0);
                    GM_ST(uint64_t, (uint8_t *)&t1199 + 16, 0);
                    GM_ST(uint64_t, (uint8_t *)&t1199 + 24, 0);
                    ((uint8_t *)&t1199)[0] = 0;
                    GM_ST(gm_b0_1, ((uint8_t *)&t1199 + 8), t1198);
                    gm_tcfa6l7reoosn t1200 = t1199;
                    uint8_t t1201 = ((((uint8_t *)&t1200))[0] != 0);
                    F.ret = t1199;
                    goto gm_fret;
                }
                goto me1175;
mn1183:;
ma1177:;
                uint8_t t1203 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                if (t1203 != ((uint8_t)29ULL)) goto mn1202;
                {
                    void * t1205 = gm_l0;
                    gm_b16_8 t1206 = gm_l7;
                    unsigned __int128 t1207;
                    unsigned __int128 t1209 = gm_l70;
                    uint8_t t1210 = ((t1209 == ((unsigned __int128)(((unsigned __int128)0u << 64) | 0u))));
                    if (!t1210) goto ie1208;
                    uint32_t t1212 = gm_l69;
                    unsigned __int128 t1213 = (unsigned __int128)t1212;
                    unsigned __int128 t1214 = t1213;
                    t1207 = t1214;
                    goto ix1208;
ie1208:;
                    unsigned __int128 t1216 = gm_l70;
                    uint32_t t1217 = (uint32_t)gm_ctz((unsigned __int128)(unsigned __int128)(t1216), 128);
                    unsigned __int128 t1218 = (unsigned __int128)t1217;
                    unsigned __int128 t1219 = t1218;
                    t1207 = t1219;
ix1208:;
                    gm_f_put_uint_cddin99(t1205, t1206, t1207);
                }
                {
                    gm_b0_1 t1220;
                    gm_tcfa6l7reoosn t1221;
                    GM_ST(uint8_t, (uint8_t *)&t1221 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t1221 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t1221 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t1221 + 8, 0);
                    GM_ST(uint64_t, (uint8_t *)&t1221 + 16, 0);
                    GM_ST(uint64_t, (uint8_t *)&t1221 + 24, 0);
                    ((uint8_t *)&t1221)[0] = 0;
                    GM_ST(gm_b0_1, ((uint8_t *)&t1221 + 8), t1220);
                    gm_tcfa6l7reoosn t1222 = t1221;
                    uint8_t t1223 = ((((uint8_t *)&t1222))[0] != 0);
                    F.ret = t1221;
                    goto gm_fret;
                }
                goto me1175;
mn1202:;
ma1178:;
                uint8_t t1225 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                if (t1225 != ((uint8_t)30ULL)) goto mn1224;
                {
                    void * t1227 = gm_l0;
                    gm_b16_8 t1228 = gm_l7;
                    unsigned __int128 t1229 = gm_l70;
                    uint32_t t1230 = (uint32_t)gm_popcount((unsigned __int128)(unsigned __int128)(t1229));
                    unsigned __int128 t1231 = (unsigned __int128)t1230;
                    gm_f_put_uint_cddin99(t1227, t1228, t1231);
                }
                {
                    gm_b0_1 t1232;
                    gm_tcfa6l7reoosn t1233;
                    GM_ST(uint8_t, (uint8_t *)&t1233 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t1233 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t1233 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t1233 + 8, 0);
                    GM_ST(uint64_t, (uint8_t *)&t1233 + 16, 0);
                    GM_ST(uint64_t, (uint8_t *)&t1233 + 24, 0);
                    ((uint8_t *)&t1233)[0] = 0;
                    GM_ST(gm_b0_1, ((uint8_t *)&t1233 + 8), t1232);
                    gm_tcfa6l7reoosn t1234 = t1233;
                    uint8_t t1235 = ((((uint8_t *)&t1234))[0] != 0);
                    F.ret = t1233;
                    goto gm_fret;
                }
                goto me1175;
mn1224:;
ma1179:;
                uint8_t t1237 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                if (t1237 != ((uint8_t)31ULL)) goto mn1236;
                {
                    uint32_t t1240 = gm_l69;
                    if (((uint32_t)8ULL) == 0) gm_trap("division by zero", gm_srcl8pn2pjtmj6k, 376, 24);
                    uint32_t t1241 = (uint32_t)(t1240 % ((uint32_t)8ULL));
                    uint8_t t1242 = (!(t1241 == ((uint32_t)0ULL)));
                    if (!t1242) goto ie1239;
                    {
                        void * t1244 = gm_l0;
                        gm_tl874t2atnlh3 t1245 = gm_l1;
                        uint32_t t1246 = gm_l69;
                        gm_t88rcucg1b7uk t1247;
                        GM_ST(uint32_t, ((uint8_t *)&t1247 + 0), t1246);
                        gm_tabb710d9mq2e t1248;
                        gm_f_format_ven7kt5((void *)&t1248, (void *)NULL, cx, NULL, t1247);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_tabb710d9mq2e t1249 = t1248;
                        if (((((uint8_t *)&t1249))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t1249) + 8, gm_srcl8pn2pjtmj6k, 376, 62);
                        gm_b16_8 t1250 = GM_LD(gm_b16_8, (((uint8_t *)&t1249) + 8));
                        gm_b16_8 t1251 = t1250;
                        gm_b16_8 t1252 = GM_LD(gm_b16_8, ((uint8_t *)&t1251));
                        gm_t76123gfet73g t1253;
                        gm_f_trap_qh84l9d((void *)&t1253, (void *)NULL, cx, NULL, t1244, t1245, t1252);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_tcfa6l7reoosn t1254;
                        GM_ST(uint8_t, (uint8_t *)&t1254 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t1254 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t1254 + 4, 0);
                        ((uint8_t *)&t1254)[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t1254 + 8), t1253);
                        gm_tcfa6l7reoosn t1255 = t1254;
                        uint8_t t1256 = ((((uint8_t *)&t1255))[0] != 0);
                        F.ret = t1254;
                        goto gm_fret;
                    }
                    goto ix1239;
ie1239:;
ix1239:;
                }
                {
                    {
                        gm_l73 = ((uint32_t)0ULL);
                    }
                    {
                        uint32_t t1258 = gm_l69;
                        if (((uint32_t)8ULL) == 0) gm_trap("division by zero", gm_srcl8pn2pjtmj6k, 377, 33);
                        uint32_t t1259 = (uint32_t)(t1258 / ((uint32_t)8ULL));
                        gm_l72 = t1259;
                    }
                    {
                        for (;;) {
                            uint32_t t1261 = gm_l73;
                            uint32_t t1262 = gm_l72;
                            uint8_t t1263 = (t1261 < t1262);
                            if (!t1263) goto lb1260;
                            {
                                uint32_t t1266 = gm_l73;
                                uint32_t t1267 = t1266;
                                uint32_t t1270 = GM_LD(uint32_t, ((uint8_t *)&t1267));
                                gm_l74 = t1270;
                                {
                                    unsigned __int128 t1272 = gm_l71;
                                    unsigned __int128 t1273 = gm_l70;
                                    uint32_t t1274 = gm_l74;
                                    uint32_t r1275; bool o1276 = __builtin_mul_overflow(t1274, ((uint32_t)8ULL), &r1275);
                                    if (o1276) gm_trap("integer overflow in `*`", gm_srcl8pn2pjtmj6k, 377, 58);
                                    uint32_t t1277 = r1275;
                                    unsigned __int128 t1278 = (unsigned __int128)t1277;
                                    unsigned __int128 n1279 = (unsigned __int128)t1278;
                                    if (n1279 >= 128) gm_trap_shift((uint64_t)n1279, "u128", gm_srcl8pn2pjtmj6k, 377, 48);
                                    unsigned __int128 t1280 = (unsigned __int128)(t1273 >> (int)n1279);
                                    unsigned __int128 t1281 = (unsigned __int128)(t1280 & ((unsigned __int128)(((unsigned __int128)0u << 64) | 255u)));
                                    uint32_t t1282 = gm_l69;
                                    if (((uint32_t)8ULL) == 0) gm_trap("division by zero", gm_srcl8pn2pjtmj6k, 377, 83);
                                    uint32_t t1283 = (uint32_t)(t1282 / ((uint32_t)8ULL));
                                    uint32_t r1284; bool o1285 = __builtin_sub_overflow(t1283, ((uint32_t)1ULL), &r1284);
                                    if (o1285) gm_trap("integer overflow in `-`", gm_srcl8pn2pjtmj6k, 377, 83);
                                    uint32_t t1286 = r1284;
                                    uint32_t t1287 = gm_l74;
                                    uint32_t r1288; bool o1289 = __builtin_sub_overflow(t1286, t1287, &r1288);
                                    if (o1289) gm_trap("integer overflow in `-`", gm_srcl8pn2pjtmj6k, 377, 83);
                                    uint32_t t1290 = r1288;
                                    uint32_t r1291; bool o1292 = __builtin_mul_overflow(t1290, ((uint32_t)8ULL), &r1291);
                                    if (o1292) gm_trap("integer overflow in `*`", gm_srcl8pn2pjtmj6k, 377, 82);
                                    uint32_t t1293 = r1291;
                                    unsigned __int128 t1294 = (unsigned __int128)t1293;
                                    unsigned __int128 n1295 = (unsigned __int128)t1294;
                                    if (n1295 >= 128) gm_trap_shift((uint64_t)n1295, "u128", gm_srcl8pn2pjtmj6k, 377, 46);
                                    unsigned __int128 r1296 = (unsigned __int128)((unsigned __int128)t1281 << (int)n1295);
                                    bool o1297 = (unsigned __int128)(r1296 >> (int)n1295) != t1281;
                                    if (o1297) gm_trap("integer overflow in `<<`", gm_srcl8pn2pjtmj6k, 377, 46);
                                    unsigned __int128 t1298 = r1296;
                                    unsigned __int128 t1299 = (unsigned __int128)(t1272 | t1298);
                                    gm_l71 = t1299;
                                }
                                goto me1268;
mn1269:;
                                gm_trap("no match arm matched the scrutinee", gm_srcl8pn2pjtmj6k, 377, 21);
me1268:;
                            }
lc1260:;
                            {
                                uint32_t t1301 = gm_l73;
                                uint32_t r1302; bool o1303 = __builtin_add_overflow(t1301, ((uint32_t)1ULL), &r1302);
                                uint32_t t1304 = r1302;
                                gm_l73 = t1304;
                            }
                        }
lb1260:;
                    }
                }
                goto me1175;
mn1236:;
ma1180:;
                {
                    {
                        gm_l76 = ((uint32_t)0ULL);
                    }
                    {
                        uint32_t t1308 = gm_l69;
                        gm_l75 = t1308;
                    }
                    {
                        for (;;) {
                            uint32_t t1310 = gm_l76;
                            uint32_t t1311 = gm_l75;
                            uint8_t t1312 = (t1310 < t1311);
                            if (!t1312) goto lb1309;
                            {
                                uint32_t t1315 = gm_l76;
                                uint32_t t1316 = t1315;
                                uint32_t t1319 = GM_LD(uint32_t, ((uint8_t *)&t1316));
                                gm_l77 = t1319;
                                {
                                    unsigned __int128 t1322 = gm_l70;
                                    uint32_t t1323 = gm_l77;
                                    unsigned __int128 t1324 = (unsigned __int128)t1323;
                                    unsigned __int128 n1325 = (unsigned __int128)t1324;
                                    if (n1325 >= 128) gm_trap_shift((uint64_t)n1325, "u128", gm_srcl8pn2pjtmj6k, 381, 29);
                                    unsigned __int128 t1326 = (unsigned __int128)(t1322 >> (int)n1325);
                                    unsigned __int128 t1327 = (unsigned __int128)(t1326 & ((unsigned __int128)(((unsigned __int128)0u << 64) | 1u)));
                                    uint8_t t1328 = ((t1327 == ((unsigned __int128)(((unsigned __int128)0u << 64) | 1u))));
                                    if (!t1328) goto ie1321;
                                    {
                                        unsigned __int128 t1330 = gm_l71;
                                        uint32_t t1331 = gm_l69;
                                        uint32_t r1332; bool o1333 = __builtin_sub_overflow(t1331, ((uint32_t)1ULL), &r1332);
                                        if (o1333) gm_trap("integer overflow in `-`", gm_srcl8pn2pjtmj6k, 381, 75);
                                        uint32_t t1334 = r1332;
                                        uint32_t t1335 = gm_l77;
                                        uint32_t r1336; bool o1337 = __builtin_sub_overflow(t1334, t1335, &r1336);
                                        if (o1337) gm_trap("integer overflow in `-`", gm_srcl8pn2pjtmj6k, 381, 75);
                                        uint32_t t1338 = r1336;
                                        unsigned __int128 t1339 = (unsigned __int128)t1338;
                                        unsigned __int128 n1340 = (unsigned __int128)t1339;
                                        if (n1340 >= 128) gm_trap_shift((uint64_t)n1340, "u128", gm_srcl8pn2pjtmj6k, 381, 59);
                                        unsigned __int128 r1341 = (unsigned __int128)((unsigned __int128)((unsigned __int128)(((unsigned __int128)0u << 64) | 1u)) << (int)n1340);
                                        bool o1342 = (unsigned __int128)(r1341 >> (int)n1340) != ((unsigned __int128)(((unsigned __int128)0u << 64) | 1u));
                                        if (o1342) gm_trap("integer overflow in `<<`", gm_srcl8pn2pjtmj6k, 381, 59);
                                        unsigned __int128 t1343 = r1341;
                                        unsigned __int128 t1344 = (unsigned __int128)(t1330 | t1343);
                                        gm_l71 = t1344;
                                    }
                                    goto ix1321;
ie1321:;
ix1321:;
                                }
                                goto me1317;
mn1318:;
                                gm_trap("no match arm matched the scrutinee", gm_srcl8pn2pjtmj6k, 380, 21);
me1317:;
                            }
lc1309:;
                            {
                                uint32_t t1346 = gm_l76;
                                uint32_t r1347; bool o1348 = __builtin_add_overflow(t1346, ((uint32_t)1ULL), &r1347);
                                uint32_t t1349 = r1347;
                                gm_l76 = t1349;
                            }
                        }
lb1309:;
                    }
                }
                goto me1175;
mn1305:;
ma1181:;
                gm_trap("no match arm matched the scrutinee", gm_srcl8pn2pjtmj6k, 362, 13);
me1175:;
            }
            {
                void * t1350 = gm_l0;
                gm_b16_8 t1351 = gm_l7;
                uint8_t t1352 = GM_LD(uint8_t, (((uint8_t *)&gm_l68) + 0));
                uint32_t t1353 = gm_l69;
                unsigned __int128 t1354 = gm_l71;
                unsigned __int128 t1355 = gm_f_num_ext_348kmbu(t1352, t1353, t1354);
                gm_f_put_uint_cddin99(t1350, t1351, t1355);
            }
            goto me3;
mn1128:;
ma19:;
            uint8_t t1357 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t1357 != ((uint8_t)62ULL)) goto mn1356;
            {
                void * t1359 = gm_l0;
                gm_t88rcucg1b7uk t1360 = gm_l5;
                gm_tg5lql6itufk7 t1361 = gm_f_int_info_ulcevdv(t1359, GM_CONV(gm_csguankko4lmhd, t1360));
                gm_tg5lql6itufk7 t1362 = t1361;
                gm_ttovhfee8mf28 t1363;
                if (((((uint8_t *)&t1362))[8] != 0)) {
                    gm_ttovhfee8mf28 t1364 = GM_LD(gm_ttovhfee8mf28, (((uint8_t *)&t1362) + 0));
                    t1363 = t1364;
                } else {
                    void * t1365 = gm_l0;
                    gm_tl874t2atnlh3 t1366 = gm_l1;
                    gm_b16_8 t1367;
                    GM_ST(void *, (uint8_t *)&t1367, (void *)(gm_sartclcojin4f));
                    GM_ST(uintptr_t, (uint8_t *)&t1367 + 8, (uintptr_t)(51));
                    gm_t76123gfet73g t1368;
                    gm_f_trap_qh84l9d((void *)&t1368, (void *)NULL, cx, NULL, t1365, t1366, t1367);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tcfa6l7reoosn t1369;
                    GM_ST(uint8_t, (uint8_t *)&t1369 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t1369 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t1369 + 4, 0);
                    ((uint8_t *)&t1369)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t1369 + 8), t1368);
                    gm_tcfa6l7reoosn t1370 = t1369;
                    uint8_t t1371 = ((((uint8_t *)&t1370))[0] != 0);
                    F.ret = t1369;
                    goto gm_fret;
                }
                gm_l78 = t1363;
            }
            {
                uint8_t t1372 = GM_LD(uint8_t, (((uint8_t *)&gm_l78) + 0));
                void * t1373 = gm_l0;
                gm_t88rcucg1b7uk t1374 = gm_l5;
                uint64_t t1375 = (uintptr_t)((uint64_t)1ULL);
                uint64_t t1376 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                if (t1375 >= (uintptr_t)(t1376)) gm_trap_index(t1375, t1376, gm_srcl8pn2pjtmj6k, 389, 56);
                gm_b16_8 t1377 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t1375 * 16));
                gm_b16_8 t1378 = t1377;
                gm_b16_8 t1379 = GM_LD(gm_b16_8, ((uint8_t *)&t1378));
                unsigned __int128 t1380 = gm_f_get_int_pg7p4gc(t1373, GM_CONV(gm_csguankko4lmhd, t1374), t1379);
                gm_f_num_val_i3upeur((void *)&gm_l79, t1372, t1380);
            }
            {
                unsigned __int128 t1381 = GM_LD(unsigned __int128, (((uint8_t *)&gm_l79) + 16));
                uint64_t t1382 = (uint64_t)t1381;
                uint64_t t1383 = (uint64_t)(((uint64_t)14695981039346656037ULL) ^ t1382);
                gm_l80 = t1383;
            }
            {
                void * t1384 = gm_l0;
                gm_tl874t2atnlh3 t1385 = gm_l1;
                gm_t88rcucg1b7uk t1386 = gm_l4;
                uint64_t t1387 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t1388 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                if (t1387 >= (uintptr_t)(t1388)) gm_trap_index(t1387, t1388, gm_srcl8pn2pjtmj6k, 391, 38);
                gm_b16_8 t1389 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t1387 * 16));
                void * t1390 = (void *)((uint8_t *)&gm_l80);
                gm_tcfa6l7reoosn t1391;
                gm_f_hash_val_nr2e99r((void *)&t1391, (void *)NULL, cx, NULL, t1384, t1385, t1386, t1389, t1390);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tcfa6l7reoosn t1392 = t1391;
                if (((((uint8_t *)&t1392))[0] != 0)) {
                    gm_t76123gfet73g t1393 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t1392) + 8));
                    gm_tcfa6l7reoosn t1394;
                    GM_ST(uint8_t, (uint8_t *)&t1394 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t1394 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t1394 + 4, 0);
                    ((uint8_t *)&t1394)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t1394 + 8), t1393);
                    F.ret = t1394;
                    goto gm_fret;
                }
                gm_b0_1 t1395 = GM_LD(gm_b0_1, (((uint8_t *)&t1392) + 8));
            }
            {
                void * t1396 = gm_l0;
                gm_b16_8 t1397 = gm_l7;
                uint64_t t1398 = gm_l80;
                unsigned __int128 t1399 = (unsigned __int128)t1398;
                gm_f_put_uint_cddin99(t1396, t1397, t1399);
            }
            goto me3;
mn1356:;
ma20:;
            uint8_t t1403 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t1403 != ((uint8_t)40ULL)) goto pa1402;
            goto po1401;
pa1402:;
            uint8_t t1405 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t1405 != ((uint8_t)41ULL)) goto pa1404;
            goto po1401;
pa1404:;
            uint8_t t1407 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t1407 != ((uint8_t)42ULL)) goto pa1406;
            goto po1401;
pa1406:;
            uint8_t t1409 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t1409 != ((uint8_t)43ULL)) goto pa1408;
            goto po1401;
pa1408:;
            uint8_t t1411 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t1411 != ((uint8_t)44ULL)) goto pa1410;
            goto po1401;
pa1410:;
            uint8_t t1413 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t1413 != ((uint8_t)45ULL)) goto pa1412;
            goto po1401;
pa1412:;
            uint8_t t1415 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t1415 != ((uint8_t)46ULL)) goto pa1414;
            goto po1401;
pa1414:;
            uint8_t t1417 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t1417 != ((uint8_t)47ULL)) goto pa1416;
            goto po1401;
pa1416:;
            uint8_t t1419 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t1419 != ((uint8_t)48ULL)) goto pa1418;
            goto po1401;
pa1418:;
            uint8_t t1421 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t1421 != ((uint8_t)49ULL)) goto pa1420;
            goto po1401;
pa1420:;
            uint8_t t1423 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t1423 != ((uint8_t)50ULL)) goto pa1422;
            goto po1401;
pa1422:;
            uint8_t t1425 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t1425 != ((uint8_t)51ULL)) goto pa1424;
            goto po1401;
pa1424:;
            uint8_t t1427 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t1427 != ((uint8_t)52ULL)) goto pa1426;
            goto po1401;
pa1426:;
            uint8_t t1429 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t1429 != ((uint8_t)53ULL)) goto pa1428;
            goto po1401;
pa1428:;
            uint8_t t1431 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t1431 != ((uint8_t)54ULL)) goto pa1430;
            goto po1401;
pa1430:;
            uint8_t t1433 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t1433 != ((uint8_t)55ULL)) goto pa1432;
            goto po1401;
pa1432:;
            uint8_t t1435 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t1435 != ((uint8_t)56ULL)) goto pa1434;
            goto po1401;
pa1434:;
            uint8_t t1437 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t1437 != ((uint8_t)57ULL)) goto pa1436;
            goto po1401;
pa1436:;
            goto mn1400;
po1401:;
            void * t1438 = gm_l0;
            gm_tl874t2atnlh3 t1439 = gm_l1;
            gm_tvdg94bk7aa22 t1440 = gm_l2;
            gm_t88rcucg1b7uk t1441 = gm_l3;
            gm_t88rcucg1b7uk t1442 = gm_l4;
            gm_t88rcucg1b7uk t1443 = gm_l5;
            gm_b16_8 t1444 = gm_l6;
            gm_b16_8 t1445 = gm_l7;
            gm_f_vector_builtin_3fn933h((void *)&(*(gm_tcfa6l7reoosn *)gm_out), (void *)NULL, cx, NULL, t1438, t1439, t1440, t1441, t1442, t1443, t1444, t1445);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
            goto me3;
mn1400:;
ma21:;
            void * t1447 = gm_l0;
            gm_tl874t2atnlh3 t1448 = gm_l1;
            gm_tvdg94bk7aa22 t1449 = gm_l2;
            gm_th9nl3oso747e t1450;
            GM_ST(gm_tvdg94bk7aa22, ((uint8_t *)&t1450 + 0), t1449);
            gm_tabb710d9mq2e t1451;
            gm_f_format_985clkf((void *)&t1451, (void *)NULL, cx, NULL, t1450);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t1452 = t1451;
            if (((((uint8_t *)&t1452))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t1452) + 8, gm_srcl8pn2pjtmj6k, 395, 42);
            gm_b16_8 t1453 = GM_LD(gm_b16_8, (((uint8_t *)&t1452) + 8));
            gm_b16_8 t1454 = t1453;
            gm_b16_8 t1455 = GM_LD(gm_b16_8, ((uint8_t *)&t1454));
            gm_t76123gfet73g t1456;
            gm_f_unsupported_70vvk1e((void *)&t1456, (void *)NULL, cx, NULL, t1447, t1448, t1455);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tcfa6l7reoosn t1457;
            GM_ST(uint8_t, (uint8_t *)&t1457 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t1457 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t1457 + 4, 0);
            ((uint8_t *)&t1457)[0] = 1;
            GM_ST(gm_t76123gfet73g, ((uint8_t *)&t1457 + 8), t1456);
            gm_tcfa6l7reoosn t1458 = t1457;
            uint8_t t1459 = ((((uint8_t *)&t1458))[0] != 0);
            F.ret = t1457;
            goto gm_fret;
            goto me3;
mn1446:;
ma22:;
            gm_trap("no match arm matched the scrutinee", gm_srcl8pn2pjtmj6k, 191, 5);
me3:;
        }
    }
    gm_b0_1 t1460;
    gm_tcfa6l7reoosn t1461;
    GM_ST(uint8_t, (uint8_t *)&t1461 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t1461 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t1461 + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&t1461 + 8, 0);
    GM_ST(uint64_t, (uint8_t *)&t1461 + 16, 0);
    GM_ST(uint64_t, (uint8_t *)&t1461 + 24, 0);
    ((uint8_t *)&t1461)[0] = 0;
    GM_ST(gm_b0_1, ((uint8_t *)&t1461 + 8), t1460);
    gm_tcfa6l7reoosn t1462 = t1461;
    F.ret = t1462;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_array_ptr_to_slice_7dg3m1v(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, gm_b16_8 a3) {
    struct gm_fr_array_ptr_to_slice_7dg3m1v F;
    void * gm_l0; /* m */
    gm_t88rcucg1b7uk gm_l1; /* ptr_ty */
    gm_b16_8 gm_l2; /* b */
    gm_b16_8 gm_l3; /* dst */
    uint64_t gm_l4; /* len */
    gm_t88rcucg1b7uk gm_l5; /* pointee */
    uint64_t gm_l6; /* n */
    GM_STACK_CHECK(&F, 398u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        gm_l4 = ((uint64_t)0ULL);
    }
    {
        void * t3 = gm_l0;
        gm_t88rcucg1b7uk t4 = gm_l1;
        gm_t53at80pfdus3 t5;
        gm_f_kind_htv6093((void *)&t5, t3, GM_CONV(gm_csguankko4lmhd, t4));
        gm_t53at80pfdus3 t6 = t5;
        uint8_t t7 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
        if (t7 != ((uint8_t)8ULL)) goto ie2;
        gm_t88rcucg1b7uk t8 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t6) + 8) + 4));
        gm_l5 = t8;
        void * t9 = gm_l0;
        gm_t88rcucg1b7uk t10 = gm_l5;
        gm_t53at80pfdus3 t11;
        gm_f_kind_htv6093((void *)&t11, t9, GM_CONV(gm_csguankko4lmhd, t10));
        gm_t53at80pfdus3 t12 = t11;
        uint8_t t13 = GM_LD(uint8_t, (((uint8_t *)&t12) + 0));
        if (t13 != ((uint8_t)11ULL)) goto ie2;
        uint64_t t14 = GM_LD(uint64_t, ((((uint8_t *)&t12) + 8) + 0));
        gm_l6 = t14;
        {
            uint64_t t16 = gm_l6;
            gm_l4 = t16;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t17 = gm_l0;
        gm_b16_8 t18 = gm_l3;
        void * t19 = gm_l0;
        gm_b16_8 t20 = gm_l2;
        uint64_t t21 = gm_f_ptr_of_b77b4c9(t19, t20);
        uint64_t t22 = gm_l4;
        gm_f_put_slice_ko14loi(t17, t18, t21, t22);
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) void gm_f_coerce_bytes_9kf3u24(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_tmn7n0n9raou5 a2, gm_t88rcucg1b7uk a3, gm_t88rcucg1b7uk a4, gm_b16_8 a5, gm_b16_8 a6) {
    struct gm_fr_coerce_bytes_9kf3u24 F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_tmn7n0n9raou5 gm_l2; /* kind */
    gm_t88rcucg1b7uk gm_l3; /* from */
    gm_t88rcucg1b7uk gm_l4; /* to */
    gm_b16_8 gm_l5; /* b */
    gm_b16_8 gm_l6; /* dst */
    gm_ttovhfee8mf28 gm_l7; /* info */
    uint32_t gm_l8; /* variant */
    gm_t88rcucg1b7uk gm_l9; /* derr */
    gm_b16_8 gm_l10; /* lifted */
    gm_t88rcucg1b7uk gm_l11; /* src_in */
    gm_t88rcucg1b7uk gm_l12; /* dst_in */
    gm_b16_8 gm_l13; /* payload */
    gm_t88rcucg1b7uk gm_l14; /* pointee */
    gm_b16_8 gm_l15; /* sl */
    GM_STACK_CHECK(&F, 2681u);
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
            switch (t14) { case ((uint8_t)0ULL): goto ma4; case ((uint8_t)1ULL): goto ma5; case ((uint8_t)2ULL): goto ma6; case ((uint8_t)3ULL): goto ma7; case ((uint8_t)4ULL): goto ma8; case ((uint8_t)5ULL): goto ma12; case ((uint8_t)6ULL): goto ma11; case ((uint8_t)7ULL): goto ma9; case ((uint8_t)8ULL): goto ma11; case ((uint8_t)9ULL): goto ma12; case ((uint8_t)10ULL): goto ma11; case ((uint8_t)11ULL): goto ma10; case ((uint8_t)12ULL): goto ma12; default: goto ma13; }
ma4:;
            uint8_t t16 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t16 != ((uint8_t)0ULL)) goto mn15;
            {
                void * t19 = gm_l0;
                gm_t88rcucg1b7uk t20 = gm_l4;
                gm_tg5lql6itufk7 t21 = gm_f_int_info_ulcevdv(t19, GM_CONV(gm_csguankko4lmhd, t20));
                gm_tg5lql6itufk7 t22 = t21;
                if (!((((uint8_t *)&t22))[8] != 0)) goto ie18;
                gm_ttovhfee8mf28 t23 = GM_LD(gm_ttovhfee8mf28, ((uint8_t *)&t22));
                gm_l7 = t23;
                {
                    void * t25 = gm_l0;
                    gm_b16_8 t26 = gm_l6;
                    uint8_t t27 = GM_LD(uint8_t, (((uint8_t *)&gm_l7) + 0));
                    uint32_t t28 = GM_LD(uint32_t, (((uint8_t *)&gm_l7) + 4));
                    void * t29 = gm_l0;
                    gm_t88rcucg1b7uk t30 = gm_l3;
                    gm_b16_8 t31 = gm_l5;
                    gm_b16_8 t32 = t31;
                    gm_b16_8 t33 = GM_LD(gm_b16_8, ((uint8_t *)&t32));
                    unsigned __int128 t34 = gm_f_get_int_pg7p4gc(t29, GM_CONV(gm_csguankko4lmhd, t30), t33);
                    unsigned __int128 t35 = gm_f_num_ext_348kmbu(t27, t28, t34);
                    gm_f_put_uint_cddin99(t25, t26, t35);
                }
                goto ix18;
ie18:;
                {
                    void * t37 = gm_l0;
                    gm_b16_8 t38 = gm_l6;
                    void * t39 = gm_l0;
                    gm_t88rcucg1b7uk t40 = gm_l3;
                    gm_b16_8 t41 = gm_l5;
                    gm_b16_8 t42 = t41;
                    gm_b16_8 t43 = GM_LD(gm_b16_8, ((uint8_t *)&t42));
                    unsigned __int128 t44 = gm_f_get_int_pg7p4gc(t39, GM_CONV(gm_csguankko4lmhd, t40), t43);
                    gm_f_put_uint_cddin99(t37, t38, t44);
                }
ix18:;
            }
            goto me3;
mn15:;
ma5:;
            uint8_t t46 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t46 != ((uint8_t)1ULL)) goto mn45;
            {
                uint8_t t49 = 0;
                void * t50 = gm_l0;
                gm_t88rcucg1b7uk t51 = gm_l3;
                gm_t53at80pfdus3 t52;
                gm_f_kind_htv6093((void *)&t52, t50, GM_CONV(gm_csguankko4lmhd, t51));
                gm_t53at80pfdus3 t53 = t52;
                uint8_t t54;
                uint8_t t57 = GM_LD(uint8_t, (((uint8_t *)&t53) + 0));
                if (t57 != ((uint8_t)3ULL)) goto mn56;
                t54 = ((uint8_t)1);
                goto me55;
mn56:;
                t54 = ((uint8_t)0);
                goto me55;
mn58:;
                gm_trap("no match arm matched the scrutinee", gm_srcl8pn2pjtmj6k, 18, 21);
me55:;
                uint8_t t59 = (uint8_t)!t54;
                if (!t59) {
                    void * t60 = gm_l0;
                    gm_t88rcucg1b7uk t61 = gm_l4;
                    gm_t53at80pfdus3 t62;
                    gm_f_kind_htv6093((void *)&t62, t60, GM_CONV(gm_csguankko4lmhd, t61));
                    gm_t53at80pfdus3 t63 = t62;
                    uint8_t t64;
                    uint8_t t67 = GM_LD(uint8_t, (((uint8_t *)&t63) + 0));
                    if (t67 != ((uint8_t)3ULL)) goto mn66;
                    t64 = ((uint8_t)1);
                    goto me65;
mn66:;
                    t64 = ((uint8_t)0);
                    goto me65;
mn68:;
                    gm_trap("no match arm matched the scrutinee", gm_srcl8pn2pjtmj6k, 18, 56);
me65:;
                    uint8_t t69 = (uint8_t)!t64;
                    t49 = t69;
                } else t49 = 1;
                if (!t49) goto ie48;
                {
                    void * t71 = gm_l0;
                    gm_tl874t2atnlh3 t72 = gm_l1;
                    gm_b16_8 t73;
                    GM_ST(void *, (uint8_t *)&t73, (void *)(gm_sd4ibvv64si9n));
                    GM_ST(uintptr_t, (uint8_t *)&t73 + 8, (uintptr_t)(44));
                    gm_t76123gfet73g t74;
                    gm_f_trap_qh84l9d((void *)&t74, (void *)NULL, cx, NULL, t71, t72, t73);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tcfa6l7reoosn t75;
                    GM_ST(uint8_t, (uint8_t *)&t75 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t75 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t75 + 4, 0);
                    ((uint8_t *)&t75)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t75 + 8), t74);
                    gm_tcfa6l7reoosn t76 = t75;
                    uint8_t t77 = ((((uint8_t *)&t76))[0] != 0);
                    F.ret = t75;
                    goto gm_fret;
                }
                goto ix48;
ie48:;
ix48:;
            }
            {
                void * t78 = gm_l0;
                gm_t88rcucg1b7uk t79 = gm_l4;
                void * t80 = gm_l0;
                gm_t88rcucg1b7uk t81 = gm_l3;
                gm_b16_8 t82 = gm_l5;
                gm_b16_8 t83 = t82;
                gm_b16_8 t84 = GM_LD(gm_b16_8, ((uint8_t *)&t83));
                double t85 = gm_f_get_float_grmhlm4(t80, GM_CONV(gm_csguankko4lmhd, t81), t84);
                gm_b16_8 t86 = gm_l6;
                gm_f_put_float_fiad72d(t78, GM_CONV(gm_csguankko4lmhd, t79), t85, t86);
            }
            goto me3;
mn45:;
ma6:;
            uint8_t t88 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t88 != ((uint8_t)2ULL)) goto mn87;
            void * t89 = gm_l0;
            gm_t88rcucg1b7uk t90 = gm_l4;
            gm_b16_8 t91 = gm_l6;
            gm_b16_8 t92 = gm_l5;
            gm_b16_8 t93 = t92;
            gm_b16_8 t94 = GM_LD(gm_b16_8, ((uint8_t *)&t93));
            gm_b0_1 t95 = gm_f_make_some_tvb9ubk((void *)NULL, cx, NULL, t89, t90, t91, t94);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto me3;
mn87:;
ma7:;
            uint8_t t97 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t97 != ((uint8_t)3ULL)) goto mn96;
            void * t98 = gm_l0;
            gm_t88rcucg1b7uk t99 = gm_l4;
            gm_b16_8 t100 = gm_l6;
            gm_b16_8 t101 = gm_l5;
            gm_b16_8 t102 = t101;
            gm_b16_8 t103 = GM_LD(gm_b16_8, ((uint8_t *)&t102));
            gm_b0_1 t104 = gm_f_make_res_085nq6f((void *)NULL, cx, NULL, t98, t99, t100, ((uint8_t)0), t103);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto me3;
mn96:;
ma8:;
            uint8_t t106 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t106 != ((uint8_t)4ULL)) goto mn105;
            uint32_t t107 = GM_LD(uint32_t, (((uint8_t *)&gm_l2) + 4));
            gm_l8 = t107;
            {
                void * t110 = gm_l0;
                gm_t88rcucg1b7uk t111 = gm_l3;
                gm_b16_8 t112 = gm_l5;
                gm_b16_8 t113 = t112;
                gm_b16_8 t114 = GM_LD(gm_b16_8, ((uint8_t *)&t113));
                uint8_t t115 = gm_f_res_is_err_8jbgndp((void *)NULL, cx, NULL, t110, t111, t114);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (!t115) goto ie109;
                {
                    void * t117 = gm_l0;
                    gm_t88rcucg1b7uk t118 = gm_l4;
                    gm_tgglga8ro3isc t119 = gm_f_res_types_pg7rvck(t117, GM_CONV(gm_csguankko4lmhd, t118));
                    gm_tgglga8ro3isc t120 = t119;
                    gm_t88rcucg1b7uk t121 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t120) + 0));
                    gm_l9 = t121;
                }
                {
                    void * t122 = gm_l0;
                    void * t123 = gm_l0;
                    gm_t88rcucg1b7uk t124 = gm_l9;
                    uint64_t t125 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t123, t124);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_b16_8 t126 = gm_f_tmp_8k6u9c9((void *)NULL, cx, NULL, t122, t125);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l10 = t126;
                }
                {
                    void * t127 = gm_l0;
                    gm_t88rcucg1b7uk t128 = gm_l9;
                    gm_b16_8 t129 = gm_l10;
                    uint32_t t130 = gm_l8;
                    void * t131 = gm_l0;
                    gm_t88rcucg1b7uk t132 = gm_l3;
                    gm_b16_8 t133 = gm_l5;
                    gm_b16_8 t134 = gm_f_res_payload_pb2hcmh((void *)NULL, cx, NULL, t131, t132, t133);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_b16_8 t135 = t134;
                    gm_b16_8 t136 = GM_LD(gm_b16_8, ((uint8_t *)&t135));
                    gm_b0_1 t137 = gm_f_make_variant_bn3d74n((void *)NULL, cx, NULL, t127, t128, t129, t130, t136);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                {
                    void * t138 = gm_l0;
                    gm_t88rcucg1b7uk t139 = gm_l4;
                    gm_b16_8 t140 = gm_l6;
                    gm_b16_8 t141 = gm_l10;
                    gm_b16_8 t142 = t141;
                    gm_b16_8 t143 = GM_LD(gm_b16_8, ((uint8_t *)&t142));
                    gm_b0_1 t144 = gm_f_make_res_085nq6f((void *)NULL, cx, NULL, t138, t139, t140, ((uint8_t)1), t143);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                goto ix109;
ie109:;
                {
                    void * t146 = gm_l0;
                    gm_t88rcucg1b7uk t147 = gm_l4;
                    gm_b16_8 t148 = gm_l6;
                    void * t149 = gm_l0;
                    gm_t88rcucg1b7uk t150 = gm_l3;
                    gm_b16_8 t151 = gm_l5;
                    gm_b16_8 t152 = gm_f_res_payload_pb2hcmh((void *)NULL, cx, NULL, t149, t150, t151);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_b16_8 t153 = t152;
                    gm_b16_8 t154 = GM_LD(gm_b16_8, ((uint8_t *)&t153));
                    gm_b0_1 t155 = gm_f_make_res_085nq6f((void *)NULL, cx, NULL, t146, t147, t148, ((uint8_t)0), t154);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
ix109:;
            }
            goto me3;
mn105:;
ma9:;
            uint8_t t157 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t157 != ((uint8_t)7ULL)) goto mn156;
            void * t158 = gm_l0;
            gm_t88rcucg1b7uk t159 = gm_l3;
            gm_b16_8 t160 = gm_l5;
            gm_b16_8 t161 = t160;
            gm_b16_8 t162 = GM_LD(gm_b16_8, ((uint8_t *)&t161));
            gm_b16_8 t163 = gm_l6;
            gm_b0_1 t164 = gm_f_array_ptr_to_slice_7dg3m1v((void *)NULL, cx, NULL, t158, t159, t162, t163);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto me3;
mn156:;
ma10:;
            uint8_t t166 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t166 != ((uint8_t)11ULL)) goto mn165;
            {
                void * t169 = gm_l0;
                gm_t88rcucg1b7uk t170 = gm_l3;
                gm_b16_8 t171 = gm_l5;
                gm_b16_8 t172 = t171;
                gm_b16_8 t173 = GM_LD(gm_b16_8, ((uint8_t *)&t172));
                uint8_t t174 = gm_f_opt_present_lb4i2ip((void *)NULL, cx, NULL, t169, t170, t173);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t175 = (uint8_t)!t174;
                if (!t175) goto ie168;
                {
                    gm_b16_8 t177 = gm_l6;
                    gm_f_fill_abmf5g5(t177, ((uint8_t)0ULL));
                }
                {
                    gm_b0_1 t178;
                    gm_tcfa6l7reoosn t179;
                    GM_ST(uint8_t, (uint8_t *)&t179 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t179 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t179 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t179 + 8, 0);
                    GM_ST(uint64_t, (uint8_t *)&t179 + 16, 0);
                    GM_ST(uint64_t, (uint8_t *)&t179 + 24, 0);
                    ((uint8_t *)&t179)[0] = 0;
                    GM_ST(gm_b0_1, ((uint8_t *)&t179 + 8), t178);
                    gm_tcfa6l7reoosn t180 = t179;
                    uint8_t t181 = ((((uint8_t *)&t180))[0] != 0);
                    F.ret = t179;
                    goto gm_fret;
                }
                goto ix168;
ie168:;
ix168:;
            }
            {
                void * t182 = gm_l0;
                gm_t88rcucg1b7uk t183 = gm_l3;
                gm_t88rcucg1b7uk t184 = GM_CONV(gm_t88rcucg1b7uk, gm_f_opt_inner_q4ga84u(t182, GM_CONV(gm_csguankko4lmhd, t183)));
                gm_l11 = t184;
            }
            {
                void * t185 = gm_l0;
                gm_t88rcucg1b7uk t186 = gm_l4;
                gm_t88rcucg1b7uk t187 = GM_CONV(gm_t88rcucg1b7uk, gm_f_opt_inner_q4ga84u(t185, GM_CONV(gm_csguankko4lmhd, t186)));
                gm_l12 = t187;
            }
            {
                void * t188 = gm_l0;
                gm_t88rcucg1b7uk t189 = gm_l3;
                gm_b16_8 t190 = gm_l5;
                gm_b16_8 t191 = gm_f_opt_payload_1pbnobb((void *)NULL, cx, NULL, t188, t189, t190);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l13 = t191;
            }
            {
                void * t193 = gm_l0;
                gm_t88rcucg1b7uk t194 = gm_l11;
                gm_t53at80pfdus3 t195;
                gm_f_kind_htv6093((void *)&t195, t193, GM_CONV(gm_csguankko4lmhd, t194));
                gm_t53at80pfdus3 t196 = t195;
                uint8_t t197 = GM_LD(uint8_t, (((uint8_t *)&t196) + 0));
                if (t197 != ((uint8_t)8ULL)) goto ie192;
                gm_t88rcucg1b7uk t198 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t196) + 8) + 4));
                gm_l14 = t198;
                void * t199 = gm_l0;
                gm_t88rcucg1b7uk t200 = gm_l12;
                gm_t53at80pfdus3 t201;
                gm_f_kind_htv6093((void *)&t201, t199, GM_CONV(gm_csguankko4lmhd, t200));
                gm_t53at80pfdus3 t202 = t201;
                uint8_t t203;
                uint8_t t206 = GM_LD(uint8_t, (((uint8_t *)&t202) + 0));
                if (t206 != ((uint8_t)10ULL)) goto mn205;
                t203 = ((uint8_t)1);
                goto me204;
mn205:;
                t203 = ((uint8_t)0);
                goto me204;
mn207:;
                gm_trap("no match arm matched the scrutinee", gm_srcl8pn2pjtmj6k, 42, 55);
me204:;
                if (!t203) goto ie192;
                void * t208 = gm_l0;
                gm_t88rcucg1b7uk t209 = gm_l14;
                gm_t53at80pfdus3 t210;
                gm_f_kind_htv6093((void *)&t210, t208, GM_CONV(gm_csguankko4lmhd, t209));
                gm_t53at80pfdus3 t211 = t210;
                uint8_t t212;
                uint8_t t215 = GM_LD(uint8_t, (((uint8_t *)&t211) + 0));
                if (t215 != ((uint8_t)11ULL)) goto mn214;
                t212 = ((uint8_t)1);
                goto me213;
mn214:;
                t212 = ((uint8_t)0);
                goto me213;
mn216:;
                gm_trap("no match arm matched the scrutinee", gm_srcl8pn2pjtmj6k, 42, 90);
me213:;
                if (!t212) goto ie192;
                {
                    void * t218 = gm_l0;
                    void * t219 = gm_l0;
                    gm_t88rcucg1b7uk t220 = gm_l12;
                    uint64_t t221 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t219, t220);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_b16_8 t222 = gm_f_tmp_8k6u9c9((void *)NULL, cx, NULL, t218, t221);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l15 = t222;
                }
                {
                    void * t223 = gm_l0;
                    gm_t88rcucg1b7uk t224 = gm_l11;
                    gm_b16_8 t225 = gm_l13;
                    gm_b16_8 t226 = t225;
                    gm_b16_8 t227 = GM_LD(gm_b16_8, ((uint8_t *)&t226));
                    gm_b16_8 t228 = gm_l15;
                    gm_b0_1 t229 = gm_f_array_ptr_to_slice_7dg3m1v((void *)NULL, cx, NULL, t223, t224, t227, t228);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                {
                    void * t230 = gm_l0;
                    gm_t88rcucg1b7uk t231 = gm_l4;
                    gm_b16_8 t232 = gm_l6;
                    gm_b16_8 t233 = gm_l15;
                    gm_b16_8 t234 = t233;
                    gm_b16_8 t235 = GM_LD(gm_b16_8, ((uint8_t *)&t234));
                    gm_b0_1 t236 = gm_f_make_some_tvb9ubk((void *)NULL, cx, NULL, t230, t231, t232, t235);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                goto ix192;
ie192:;
                {
                    void * t238 = gm_l0;
                    gm_t88rcucg1b7uk t239 = gm_l4;
                    gm_b16_8 t240 = gm_l6;
                    gm_b16_8 t241 = gm_l13;
                    gm_b16_8 t242 = t241;
                    gm_b16_8 t243 = GM_LD(gm_b16_8, ((uint8_t *)&t242));
                    gm_b0_1 t244 = gm_f_make_some_tvb9ubk((void *)NULL, cx, NULL, t238, t239, t240, t243);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
ix192:;
            }
            goto me3;
mn165:;
ma11:;
            uint8_t t248 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t248 != ((uint8_t)6ULL)) goto pa247;
            goto po246;
pa247:;
            uint8_t t250 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t250 != ((uint8_t)8ULL)) goto pa249;
            goto po246;
pa249:;
            uint8_t t252 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t252 != ((uint8_t)10ULL)) goto pa251;
            goto po246;
pa251:;
            goto mn245;
po246:;
            gm_b16_8 t253 = gm_l6;
            gm_b16_8 t254 = gm_l5;
            gm_b16_8 t255 = t254;
            gm_b16_8 t256 = GM_LD(gm_b16_8, ((uint8_t *)&t255));
            gm_f_copy_from_f7j6p05(t253, t256);
            goto me3;
mn245:;
ma12:;
            uint8_t t260 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t260 != ((uint8_t)5ULL)) goto pa259;
            goto po258;
pa259:;
            uint8_t t262 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t262 != ((uint8_t)9ULL)) goto pa261;
            goto po258;
pa261:;
            uint8_t t264 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t264 != ((uint8_t)12ULL)) goto pa263;
            goto po258;
pa263:;
            goto mn257;
po258:;
            void * t265 = gm_l0;
            gm_tl874t2atnlh3 t266 = gm_l1;
            gm_b16_8 t267;
            GM_ST(void *, (uint8_t *)&t267, (void *)(gm_sbjj41cb5lph3));
            GM_ST(uintptr_t, (uint8_t *)&t267 + 8, (uintptr_t)(59));
            gm_t76123gfet73g t268;
            gm_f_trap_qh84l9d((void *)&t268, (void *)NULL, cx, NULL, t265, t266, t267);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tcfa6l7reoosn t269;
            GM_ST(uint8_t, (uint8_t *)&t269 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t269 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t269 + 4, 0);
            ((uint8_t *)&t269)[0] = 1;
            GM_ST(gm_t76123gfet73g, ((uint8_t *)&t269 + 8), t268);
            gm_tcfa6l7reoosn t270 = t269;
            uint8_t t271 = ((((uint8_t *)&t270))[0] != 0);
            F.ret = t269;
            goto gm_fret;
            goto me3;
mn257:;
ma13:;
            gm_trap("no match arm matched the scrutinee", gm_srcl8pn2pjtmj6k, 9, 5);
me3:;
        }
    }
    gm_b0_1 t272;
    gm_tcfa6l7reoosn t273;
    GM_ST(uint8_t, (uint8_t *)&t273 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t273 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t273 + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&t273 + 8, 0);
    GM_ST(uint64_t, (uint8_t *)&t273 + 16, 0);
    GM_ST(uint64_t, (uint8_t *)&t273 + 24, 0);
    ((uint8_t *)&t273)[0] = 0;
    GM_ST(gm_b0_1, ((uint8_t *)&t273 + 8), t272);
    gm_tcfa6l7reoosn t274 = t273;
    F.ret = t274;
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
__attribute__((aligned(4))) void gm_f_convert_is6an2k(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_tvdg94bk7aa22 a2, gm_t88rcucg1b7uk a3, gm_t88rcucg1b7uk a4, gm_b16_8 a5, gm_b16_8 a6) {
    struct gm_fr_convert_is6an2k F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_tvdg94bk7aa22 gm_l2; /* op */
    gm_t88rcucg1b7uk gm_l3; /* src_ty */
    gm_t88rcucg1b7uk gm_l4; /* dst_ty */
    gm_b16_8 gm_l5; /* b */
    gm_b16_8 gm_l6; /* dst */
    gm_t88rcucg1b7uk gm_l7; /* target */
    uint8_t gm_l8; /* is_float */
    gm_t86h6hhm6d76n gm_l9; /* sv */
    double gm_l10; /* fv */
    gm_ttovhfee8mf28 gm_l11; /* info */
    gm_tcmkompbl09il gm_l12; /* u */
    gm_ttovhfee8mf28 gm_l13; /* info */
    gm_t88rcucg1b7uk gm_l14; /* u */
    gm_ttovhfee8mf28 gm_l15; /* info */
    gm_ttovhfee8mf28 gm_l16; /* tinfo */
    uint8_t gm_l17; /* signed */
    uint32_t gm_l18; /* bits */
    gm_t51b2en4qp02s gm_l19; /* pb */
    uint64_t gm_l20; /* n */
    uint8_t gm_l21; /* finite */
    gm_t86h6hhm6d76n gm_l22; /* n */
    gm_t51b2en4qp02s gm_l23; /* pb */
    uint64_t gm_l24; /* sz */
    uint8_t gm_l25; /* neg */
    gm_t86h6hhm6d76n gm_l26; /* n */
    gm_t86h6hhm6d76n gm_l27; /* n */
    uint16_t gm_l28; /* w */
    double gm_l29; /* x */
    gm_t88rcucg1b7uk gm_l30; /* u */
    gm_ttovhfee8mf28 gm_l31; /* info */
    GM_STACK_CHECK(&F, 4757u);
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
        gm_t88rcucg1b7uk t2;
        uint8_t t4 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l2) + 0) == ((uint8_t)5ULL)));
        if (!t4) goto ie3;
        void * t6 = gm_l0;
        gm_t88rcucg1b7uk t7 = gm_l4;
        gm_t88rcucg1b7uk t8 = GM_CONV(gm_t88rcucg1b7uk, gm_f_opt_inner_q4ga84u(t6, GM_CONV(gm_csguankko4lmhd, t7)));
        gm_t88rcucg1b7uk t9 = t8;
        t2 = t9;
        goto ix3;
ie3:;
        gm_t88rcucg1b7uk t11 = gm_l4;
        gm_t88rcucg1b7uk t12 = t11;
        t2 = t12;
ix3:;
        gm_l7 = t2;
    }
    {
        gm_l8 = ((uint8_t)0);
    }
    {
        memset((uint8_t *)&gm_l9 + 1, 0, 15);
        GM_ST(uint8_t, (uint8_t *)&gm_l9 + 0, ((uint8_t)0));
        GM_ST(unsigned __int128, (uint8_t *)&gm_l9 + 16, ((unsigned __int128)(((unsigned __int128)0u << 64) | 0u)));
    }
    {
        gm_l10 = ((double)0e0);
    }
    {
        void * t13 = gm_l0;
        gm_t88rcucg1b7uk t14 = gm_l3;
        gm_t53at80pfdus3 t15;
        gm_f_kind_htv6093((void *)&t15, t13, GM_CONV(gm_csguankko4lmhd, t14));
        gm_t53at80pfdus3 t16 = t15;
        uint8_t t24 = GM_LD(uint8_t, (((uint8_t *)&t16) + 0));
        switch (t24) { case ((uint8_t)0ULL): goto ma18; case ((uint8_t)1ULL): goto ma18; case ((uint8_t)2ULL): goto ma18; case ((uint8_t)3ULL): goto ma19; case ((uint8_t)4ULL): goto ma20; case ((uint8_t)5ULL): goto ma22; case ((uint8_t)6ULL): goto ma22; case ((uint8_t)7ULL): goto ma22; case ((uint8_t)8ULL): goto ma22; case ((uint8_t)9ULL): goto ma22; case ((uint8_t)10ULL): goto ma22; case ((uint8_t)11ULL): goto ma22; case ((uint8_t)12ULL): goto ma22; case ((uint8_t)13ULL): goto ma22; case ((uint8_t)14ULL): goto ma22; case ((uint8_t)15ULL): goto ma22; case ((uint8_t)16ULL): goto ma22; case ((uint8_t)17ULL): goto ma21; case ((uint8_t)18ULL): goto ma22; case ((uint8_t)19ULL): goto ma22; case ((uint8_t)20ULL): goto ma22; case ((uint8_t)21ULL): goto ma22; case ((uint8_t)22ULL): goto ma22; case ((uint8_t)23ULL): goto ma22; case ((uint8_t)24ULL): goto ma22; case ((uint8_t)25ULL): goto ma22; case ((uint8_t)26ULL): goto ma22; default: goto ma22; }
ma18:;
        uint8_t t28 = GM_LD(uint8_t, (((uint8_t *)&t16) + 0));
        if (t28 != ((uint8_t)0ULL)) goto pa27;
        goto po26;
pa27:;
        uint8_t t30 = GM_LD(uint8_t, (((uint8_t *)&t16) + 0));
        if (t30 != ((uint8_t)1ULL)) goto pa29;
        goto po26;
pa29:;
        uint8_t t32 = GM_LD(uint8_t, (((uint8_t *)&t16) + 0));
        if (t32 != ((uint8_t)2ULL)) goto pa31;
        goto po26;
pa31:;
        goto mn25;
po26:;
        {
            void * t34 = gm_l0;
            gm_t88rcucg1b7uk t35 = gm_l3;
            gm_tg5lql6itufk7 t36 = gm_f_int_info_ulcevdv(t34, GM_CONV(gm_csguankko4lmhd, t35));
            gm_tg5lql6itufk7 t37 = t36;
            if (!((((uint8_t *)&t37))[8] != 0)) gm_trap("unwrap of null", gm_srcl8pn2pjtmj6k, 75, 21);
            gm_ttovhfee8mf28 t38 = GM_LD(gm_ttovhfee8mf28, ((uint8_t *)&t37));
            gm_l11 = t38;
        }
        {
            uint8_t t39 = GM_LD(uint8_t, (((uint8_t *)&gm_l11) + 0));
            void * t40 = gm_l0;
            gm_t88rcucg1b7uk t41 = gm_l3;
            gm_b16_8 t42 = gm_l5;
            unsigned __int128 t43 = gm_f_get_int_pg7p4gc(t40, GM_CONV(gm_csguankko4lmhd, t41), t42);
            gm_f_num_val_i3upeur((void *)&(*(gm_t86h6hhm6d76n *)((uint8_t *)&gm_l9)), t39, t43);
        }
        goto me17;
mn25:;
ma19:;
        uint8_t t45 = GM_LD(uint8_t, (((uint8_t *)&t16) + 0));
        if (t45 != ((uint8_t)3ULL)) goto mn44;
        {
            gm_l8 = ((uint8_t)1);
        }
        {
            void * t47 = gm_l0;
            gm_t88rcucg1b7uk t48 = gm_l3;
            gm_b16_8 t49 = gm_l5;
            double t50 = gm_f_get_float_grmhlm4(t47, GM_CONV(gm_csguankko4lmhd, t48), t49);
            gm_l10 = t50;
        }
        goto me17;
mn44:;
ma20:;
        uint8_t t52 = GM_LD(uint8_t, (((uint8_t *)&t16) + 0));
        if (t52 != ((uint8_t)4ULL)) goto mn51;
        {
            unsigned __int128 t54;
            uint64_t t56 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t57 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
            if (t56 >= (uintptr_t)(t57)) gm_trap_index(t56, t57, gm_srcl8pn2pjtmj6k, 82, 32);
            uint8_t t58 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l5)) + t56 * 1));
            uint8_t t59 = ((t58 == ((uint8_t)0ULL)));
            if (!t59) goto ie55;
            unsigned __int128 t61 = ((unsigned __int128)(((unsigned __int128)0u << 64) | 0u));
            t54 = t61;
            goto ix55;
ie55:;
            unsigned __int128 t63 = ((unsigned __int128)(((unsigned __int128)0u << 64) | 1u));
            t54 = t63;
ix55:;
            GM_ST(unsigned __int128, (((uint8_t *)&gm_l9) + 16), t54);
        }
        goto me17;
mn51:;
ma21:;
        uint8_t t65 = GM_LD(uint8_t, (((uint8_t *)&t16) + 0));
        if (t65 != ((uint8_t)17ULL)) goto mn64;
        {
            void * t68 = gm_l0;
            gm_t88rcucg1b7uk t69 = gm_l3;
            gm_t57euh612ligr t70;
            gm_f_union_def_h8mvf4t((void *)&t70, t68, GM_CONV(gm_csguankko4lmhd, t69));
            gm_t57euh612ligr t71 = t70;
            if (!((((uint8_t *)&t71))[24] != 0)) goto ie67;
            gm_tcmkompbl09il t72 = GM_LD(gm_tcmkompbl09il, ((uint8_t *)&t71));
            gm_l12 = t72;
            {
                void * t74 = gm_l0;
                gm_t88rcucg1b7uk t75 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l12) + 0));
                gm_tg5lql6itufk7 t76 = gm_f_int_info_ulcevdv(t74, GM_CONV(gm_csguankko4lmhd, t75));
                gm_tg5lql6itufk7 t77 = t76;
                if (!((((uint8_t *)&t77))[8] != 0)) gm_trap("unwrap of null", gm_srcl8pn2pjtmj6k, 85, 25);
                gm_ttovhfee8mf28 t78 = GM_LD(gm_ttovhfee8mf28, ((uint8_t *)&t77));
                gm_l13 = t78;
            }
            {
                uint8_t t79 = GM_LD(uint8_t, (((uint8_t *)&gm_l13) + 0));
                void * t80 = gm_l0;
                gm_t88rcucg1b7uk t81 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l12) + 0));
                gm_b16_8 t82 = gm_l5;
                gm_b16_8 t83 = t82;
                uint64_t t84 = GM_LD(uintptr_t, ((uint8_t *)&t83) + 8);
                void * t85 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t83)));
                uint64_t t86 = (uintptr_t)((uint64_t)0ULL);
                void * t87 = gm_l0;
                gm_t88rcucg1b7uk t88 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l12) + 0));
                uint64_t t89 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t87, t88);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint64_t t90 = gm_f_us_cuv01fp(t89);
                uint64_t t91 = (uintptr_t)t90;
                if (t86 > t91 || t91 > (uintptr_t)t84) gm_trap_slice(t86, t91, t84, gm_srcl8pn2pjtmj6k, 86, 60);
                gm_b16_8 t92;
                GM_ST(void *, (uint8_t *)&t92, (void *)(((uint8_t *)t85 + t86 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t92 + 8, (uintptr_t)(t91 - t86));
                unsigned __int128 t93 = gm_f_get_int_pg7p4gc(t80, GM_CONV(gm_csguankko4lmhd, t81), t92);
                gm_f_num_val_i3upeur((void *)&(*(gm_t86h6hhm6d76n *)((uint8_t *)&gm_l9)), t79, t93);
            }
            goto ix67;
ie67:;
            void * t95 = gm_l0;
            gm_t88rcucg1b7uk t96 = gm_l3;
            gm_tkn42u7na3t81 t97 = gm_f_bits_struct_ty_22ue20s(t95, GM_CONV(gm_csguankko4lmhd, t96));
            gm_tkn42u7na3t81 t98 = t97;
            if (!((((uint8_t *)&t98))[4] != 0)) goto ie94;
            gm_t88rcucg1b7uk t99 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&t98));
            gm_l14 = t99;
            {
                void * t101 = gm_l0;
                gm_t88rcucg1b7uk t102 = gm_l14;
                gm_tg5lql6itufk7 t103 = gm_f_int_info_ulcevdv(t101, GM_CONV(gm_csguankko4lmhd, t102));
                gm_tg5lql6itufk7 t104 = t103;
                if (!((((uint8_t *)&t104))[8] != 0)) gm_trap("unwrap of null", gm_srcl8pn2pjtmj6k, 88, 25);
                gm_ttovhfee8mf28 t105 = GM_LD(gm_ttovhfee8mf28, ((uint8_t *)&t104));
                gm_l15 = t105;
            }
            {
                memset((uint8_t *)&(*(gm_t86h6hhm6d76n *)((uint8_t *)&gm_l9)) + 1, 0, 15);
                void * t106 = gm_l0;
                gm_b16_8 t107 = gm_l5;
                gm_b16_8 t108 = t107;
                uint64_t t109 = GM_LD(uintptr_t, ((uint8_t *)&t108) + 8);
                void * t110 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t108)));
                uint64_t t111 = (uintptr_t)((uint64_t)0ULL);
                void * t112 = gm_l0;
                gm_t88rcucg1b7uk t113 = gm_l14;
                uint64_t t114 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t112, t113);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint64_t t115 = gm_f_us_cuv01fp(t114);
                uint64_t t116 = (uintptr_t)t115;
                if (t111 > t116 || t116 > (uintptr_t)t109) gm_trap_slice(t111, t116, t109, gm_srcl8pn2pjtmj6k, 89, 57);
                gm_b16_8 t117;
                GM_ST(void *, (uint8_t *)&t117, (void *)(((uint8_t *)t110 + t111 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t117 + 8, (uintptr_t)(t116 - t111));
                unsigned __int128 t118 = gm_f_get_uint_bq8c4b7(t106, t117);
                uint32_t t119 = GM_LD(uint32_t, (((uint8_t *)&gm_l15) + 4));
                unsigned __int128 t120 = gm_f_num_mask_do3naah(t119);
                unsigned __int128 t121 = (unsigned __int128)(t118 & t120);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t86h6hhm6d76n *)((uint8_t *)&gm_l9)) + 0, ((uint8_t)0));
                GM_ST(unsigned __int128, (uint8_t *)&(*(gm_t86h6hhm6d76n *)((uint8_t *)&gm_l9)) + 16, t121);
            }
            goto ix94;
ie94:;
            {
                void * t123 = gm_l0;
                gm_tl874t2atnlh3 t124 = gm_l1;
                void * t125 = gm_l0;
                void * t126 = GM_LD(void *, (((uint8_t *)t125) + 8));
                gm_t88rcucg1b7uk t127 = gm_l3;
                gm_b16_8 t128 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t126, t127);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tnpifv20irnph t129;
                GM_ST(gm_b16_8, ((uint8_t *)&t129 + 0), t128);
                gm_tabb710d9mq2e t130;
                gm_f_format_pe0ujmq((void *)&t130, (void *)NULL, cx, NULL, t129);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t131 = t130;
                if (((((uint8_t *)&t131))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t131) + 8, gm_srcl8pn2pjtmj6k, 91, 38);
                gm_b16_8 t132 = GM_LD(gm_b16_8, (((uint8_t *)&t131) + 8));
                gm_b16_8 t133 = t132;
                gm_b16_8 t134 = GM_LD(gm_b16_8, ((uint8_t *)&t133));
                gm_t76123gfet73g t135;
                gm_f_trap_qh84l9d((void *)&t135, (void *)NULL, cx, NULL, t123, t124, t134);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tcfa6l7reoosn t136;
                GM_ST(uint8_t, (uint8_t *)&t136 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t136 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t136 + 4, 0);
                ((uint8_t *)&t136)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t136 + 8), t135);
                gm_tcfa6l7reoosn t137 = t136;
                uint8_t t138 = ((((uint8_t *)&t137))[0] != 0);
                F.ret = t136;
                goto gm_fret;
            }
ix94:;
ix67:;
        }
        goto me17;
mn64:;
ma22:;
        void * t140 = gm_l0;
        gm_tl874t2atnlh3 t141 = gm_l1;
        void * t142 = gm_l0;
        void * t143 = GM_LD(void *, (((uint8_t *)t142) + 8));
        gm_t88rcucg1b7uk t144 = gm_l3;
        gm_b16_8 t145 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t143, t144);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tnpifv20irnph t146;
        GM_ST(gm_b16_8, ((uint8_t *)&t146 + 0), t145);
        gm_tabb710d9mq2e t147;
        gm_f_format_pe0ujmq((void *)&t147, (void *)NULL, cx, NULL, t146);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t148 = t147;
        if (((((uint8_t *)&t148))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t148) + 8, gm_srcl8pn2pjtmj6k, 94, 35);
        gm_b16_8 t149 = GM_LD(gm_b16_8, (((uint8_t *)&t148) + 8));
        gm_b16_8 t150 = t149;
        gm_b16_8 t151 = GM_LD(gm_b16_8, ((uint8_t *)&t150));
        gm_t76123gfet73g t152;
        gm_f_trap_qh84l9d((void *)&t152, (void *)NULL, cx, NULL, t140, t141, t151);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tcfa6l7reoosn t153;
        GM_ST(uint8_t, (uint8_t *)&t153 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t153 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t153 + 4, 0);
        ((uint8_t *)&t153)[0] = 1;
        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t153 + 8), t152);
        gm_tcfa6l7reoosn t154 = t153;
        uint8_t t155 = ((((uint8_t *)&t154))[0] != 0);
        F.ret = t153;
        goto gm_fret;
        goto me17;
mn139:;
ma23:;
        gm_trap("no match arm matched the scrutinee", gm_srcl8pn2pjtmj6k, 73, 5);
me17:;
    }
    {
        void * t157 = gm_l0;
        gm_t88rcucg1b7uk t158 = gm_l7;
        gm_tg5lql6itufk7 t159 = gm_f_int_info_ulcevdv(t157, GM_CONV(gm_csguankko4lmhd, t158));
        gm_tg5lql6itufk7 t160 = t159;
        if (!((((uint8_t *)&t160))[8] != 0)) goto ie156;
        gm_ttovhfee8mf28 t161 = GM_LD(gm_ttovhfee8mf28, ((uint8_t *)&t160));
        gm_l16 = t161;
        {
            uint8_t t163 = GM_LD(uint8_t, (((uint8_t *)&gm_l16) + 0));
            gm_l17 = t163;
        }
        {
            uint32_t t164 = GM_LD(uint32_t, (((uint8_t *)&gm_l16) + 4));
            gm_l18 = t164;
        }
        {
            uint8_t t166 = gm_l8;
            uint8_t t167 = (uint8_t)!t166;
            if (!t167) goto ie165;
            {
                uint8_t t170 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l2) + 0) == ((uint8_t)5ULL)));
                if (!t170) goto ie169;
                {
                    uint8_t t173 = gm_l17;
                    uint32_t t174 = gm_l18;
                    uint8_t t175 = gm_f_num_fits_1n7pcsh(&(*(gm_t86h6hhm6d76n *)((uint8_t *)&gm_l9)), t173, t174);
                    if (!t175) goto ie172;
                    {
                    }
                    {
                        void * t177 = gm_l0;
                        gm_t88rcucg1b7uk t178 = gm_l7;
                        uint64_t t179 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t177, t178);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint64_t t180 = gm_f_us_cuv01fp(t179);
                        gm_l20 = t180;
                    }
                    {
                        void * t181 = gm_l0;
                        void * t182 = ((uint8_t *)&gm_l19);
                        uint64_t t183 = (uintptr_t)((uint64_t)0ULL);
                        uint64_t t184 = gm_l20;
                        uint64_t t185 = (uintptr_t)t184;
                        if (t183 > t185 || t185 > (uintptr_t)16) gm_trap_slice(t183, t185, 16, gm_srcl8pn2pjtmj6k, 104, 32);
                        gm_b16_8 t186;
                        GM_ST(void *, (uint8_t *)&t186, (void *)(((uint8_t *)t182 + t183 * 1)));
                        GM_ST(uintptr_t, (uint8_t *)&t186 + 8, (uintptr_t)(t185 - t183));
                        uint8_t t187 = gm_l17;
                        uint32_t t188 = gm_l18;
                        unsigned __int128 t189 = gm_f_num_bits_onl28vp(&(*(gm_t86h6hhm6d76n *)((uint8_t *)&gm_l9)));
                        unsigned __int128 t190 = gm_f_num_ext_348kmbu(t187, t188, t189);
                        gm_f_put_uint_cddin99(t181, t186, t190);
                    }
                    {
                        void * t191 = gm_l0;
                        gm_t88rcucg1b7uk t192 = gm_l4;
                        gm_b16_8 t193 = gm_l6;
                        void * t194 = ((uint8_t *)&gm_l19);
                        uint64_t t195 = (uintptr_t)((uint64_t)0ULL);
                        uint64_t t196 = gm_l20;
                        uint64_t t197 = (uintptr_t)t196;
                        if (t195 > t197 || t197 > (uintptr_t)16) gm_trap_slice(t195, t197, 16, gm_srcl8pn2pjtmj6k, 105, 46);
                        gm_b16_8 t198;
                        GM_ST(void *, (uint8_t *)&t198, (void *)(((uint8_t *)t194 + t195 * 1)));
                        GM_ST(uintptr_t, (uint8_t *)&t198 + 8, (uintptr_t)(t197 - t195));
                        gm_b16_8 t199 = t198;
                        gm_b16_8 t200 = GM_LD(gm_b16_8, ((uint8_t *)&t199));
                        gm_b0_1 t201 = gm_f_make_some_tvb9ubk((void *)NULL, cx, NULL, t191, t192, t193, t200);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    goto ix172;
ie172:;
                    {
                        gm_b16_8 t203 = gm_l6;
                        gm_f_fill_abmf5g5(t203, ((uint8_t)0ULL));
                    }
ix172:;
                }
                goto ix169;
ie169:;
                uint8_t t205 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l2) + 0) == ((uint8_t)6ULL)));
                if (!t205) goto ie204;
                {
                    void * t207 = gm_l0;
                    gm_b16_8 t208 = gm_l6;
                    uint8_t t209 = gm_l17;
                    uint32_t t210 = gm_l18;
                    uint8_t t211 = gm_l17;
                    uint32_t t212 = gm_l18;
                    gm_t86h6hhm6d76n t213;
                    gm_f_num_sat_66dqcpl((void *)&t213, &(*(gm_t86h6hhm6d76n *)((uint8_t *)&gm_l9)), t211, t212);
                    unsigned __int128 t214 = gm_f_num_bits_onl28vp(&t213);
                    unsigned __int128 t215 = gm_f_num_ext_348kmbu(t209, t210, t214);
                    gm_f_put_uint_cddin99(t207, t208, t215);
                }
                goto ix204;
ie204:;
                {
                    void * t217 = gm_l0;
                    gm_b16_8 t218 = gm_l6;
                    uint8_t t219 = gm_l17;
                    uint32_t t220 = gm_l18;
                    unsigned __int128 t221 = gm_f_num_bits_onl28vp(&(*(gm_t86h6hhm6d76n *)((uint8_t *)&gm_l9)));
                    unsigned __int128 t222 = gm_f_num_ext_348kmbu(t219, t220, t221);
                    gm_f_put_uint_cddin99(t217, t218, t222);
                }
ix204:;
ix169:;
            }
            {
                gm_b0_1 t223;
                gm_tcfa6l7reoosn t224;
                GM_ST(uint8_t, (uint8_t *)&t224 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t224 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t224 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t224 + 8, 0);
                GM_ST(uint64_t, (uint8_t *)&t224 + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&t224 + 24, 0);
                ((uint8_t *)&t224)[0] = 0;
                GM_ST(gm_b0_1, ((uint8_t *)&t224 + 8), t223);
                gm_tcfa6l7reoosn t225 = t224;
                uint8_t t226 = ((((uint8_t *)&t225))[0] != 0);
                F.ret = t224;
                goto gm_fret;
            }
            goto ix165;
ie165:;
ix165:;
        }
        {
            double t227 = gm_l10;
            double t228 = gm_l10;
            double t229 = (t227 - t228);
            uint8_t t230 = ((t229 == ((double)0e0)));
            gm_l21 = t230;
        }
        {
            uint8_t t232 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l2) + 0) == ((uint8_t)5ULL)));
            if (!t232) goto ie231;
            {
                gm_b16_8 t234 = gm_l6;
                gm_f_fill_abmf5g5(t234, ((uint8_t)0ULL));
            }
            {
                uint8_t t236 = 0;
                uint8_t t237 = gm_l21;
                uint8_t t238 = (uint8_t)!t237;
                if (!t238) {
                    double t239 = gm_l10;
                    uint8_t t240 = gm_f_num_integral_qbts4a6(t239);
                    uint8_t t241 = (uint8_t)!t240;
                    t236 = t241;
                } else t236 = 1;
                if (!t236) goto ie235;
                {
                    gm_b0_1 t243;
                    gm_tcfa6l7reoosn t244;
                    GM_ST(uint8_t, (uint8_t *)&t244 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t244 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t244 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t244 + 8, 0);
                    GM_ST(uint64_t, (uint8_t *)&t244 + 16, 0);
                    GM_ST(uint64_t, (uint8_t *)&t244 + 24, 0);
                    ((uint8_t *)&t244)[0] = 0;
                    GM_ST(gm_b0_1, ((uint8_t *)&t244 + 8), t243);
                    gm_tcfa6l7reoosn t245 = t244;
                    uint8_t t246 = ((((uint8_t *)&t245))[0] != 0);
                    F.ret = t244;
                    goto gm_fret;
                }
                goto ix235;
ie235:;
ix235:;
            }
            {
                double t247 = gm_l10;
                gm_tvdg94bk7aa22 t248;
                GM_ST(uint8_t, (uint8_t *)&t248 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t248 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t248 + 4, 0);
                GM_ST(uint32_t, (uint8_t *)&t248 + 8, 0);
                GM_ST(uint8_t, ((uint8_t *)&t248 + 0), ((uint8_t)1ULL));
                gm_tsp9ul70rnkg6 t249;
                gm_f_num_float_int_ba5kc1t((void *)&t249, t247, t248);
                gm_tsp9ul70rnkg6 t250 = t249;
                gm_t86h6hhm6d76n t251;
                if (((((uint8_t *)&t250))[32] != 0)) {
                    gm_t86h6hhm6d76n t252 = GM_LD(gm_t86h6hhm6d76n, (((uint8_t *)&t250) + 0));
                    t251 = t252;
                } else {
                    gm_b0_1 t253;
                    gm_tcfa6l7reoosn t254;
                    GM_ST(uint8_t, (uint8_t *)&t254 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t254 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t254 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t254 + 8, 0);
                    GM_ST(uint64_t, (uint8_t *)&t254 + 16, 0);
                    GM_ST(uint64_t, (uint8_t *)&t254 + 24, 0);
                    ((uint8_t *)&t254)[0] = 0;
                    GM_ST(gm_b0_1, ((uint8_t *)&t254 + 8), t253);
                    gm_tcfa6l7reoosn t255 = t254;
                    uint8_t t256 = ((((uint8_t *)&t255))[0] != 0);
                    F.ret = t254;
                    goto gm_fret;
                }
                gm_l22 = t251;
            }
            {
                uint8_t t258 = gm_l17;
                uint32_t t259 = gm_l18;
                uint8_t t260 = gm_f_num_fits_1n7pcsh(&(*(gm_t86h6hhm6d76n *)((uint8_t *)&gm_l22)), t258, t259);
                uint8_t t261 = (uint8_t)!t260;
                if (!t261) goto ie257;
                {
                    gm_b0_1 t263;
                    gm_tcfa6l7reoosn t264;
                    GM_ST(uint8_t, (uint8_t *)&t264 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t264 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t264 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t264 + 8, 0);
                    GM_ST(uint64_t, (uint8_t *)&t264 + 16, 0);
                    GM_ST(uint64_t, (uint8_t *)&t264 + 24, 0);
                    ((uint8_t *)&t264)[0] = 0;
                    GM_ST(gm_b0_1, ((uint8_t *)&t264 + 8), t263);
                    gm_tcfa6l7reoosn t265 = t264;
                    uint8_t t266 = ((((uint8_t *)&t265))[0] != 0);
                    F.ret = t264;
                    goto gm_fret;
                }
                goto ix257;
ie257:;
ix257:;
            }
            {
            }
            {
                void * t267 = gm_l0;
                gm_t88rcucg1b7uk t268 = gm_l7;
                uint64_t t269 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t267, t268);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint64_t t270 = gm_f_us_cuv01fp(t269);
                gm_l24 = t270;
            }
            {
                void * t271 = gm_l0;
                void * t272 = ((uint8_t *)&gm_l23);
                uint64_t t273 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t274 = gm_l24;
                uint64_t t275 = (uintptr_t)t274;
                if (t273 > t275 || t275 > (uintptr_t)16) gm_trap_slice(t273, t275, 16, gm_srcl8pn2pjtmj6k, 124, 24);
                gm_b16_8 t276;
                GM_ST(void *, (uint8_t *)&t276, (void *)(((uint8_t *)t272 + t273 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t276 + 8, (uintptr_t)(t275 - t273));
                uint8_t t277 = gm_l17;
                uint32_t t278 = gm_l18;
                unsigned __int128 t279 = gm_f_num_bits_onl28vp(&(*(gm_t86h6hhm6d76n *)((uint8_t *)&gm_l22)));
                unsigned __int128 t280 = gm_f_num_ext_348kmbu(t277, t278, t279);
                gm_f_put_uint_cddin99(t271, t276, t280);
            }
            {
                void * t281 = gm_l0;
                gm_t88rcucg1b7uk t282 = gm_l4;
                gm_b16_8 t283 = gm_l6;
                void * t284 = ((uint8_t *)&gm_l23);
                uint64_t t285 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t286 = gm_l24;
                uint64_t t287 = (uintptr_t)t286;
                if (t285 > t287 || t287 > (uintptr_t)16) gm_trap_slice(t285, t287, 16, gm_srcl8pn2pjtmj6k, 125, 38);
                gm_b16_8 t288;
                GM_ST(void *, (uint8_t *)&t288, (void *)(((uint8_t *)t284 + t285 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t288 + 8, (uintptr_t)(t287 - t285));
                gm_b16_8 t289 = t288;
                gm_b16_8 t290 = GM_LD(gm_b16_8, ((uint8_t *)&t289));
                gm_b0_1 t291 = gm_f_make_some_tvb9ubk((void *)NULL, cx, NULL, t281, t282, t283, t290);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                gm_b0_1 t292;
                gm_tcfa6l7reoosn t293;
                GM_ST(uint8_t, (uint8_t *)&t293 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t293 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t293 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t293 + 8, 0);
                GM_ST(uint64_t, (uint8_t *)&t293 + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&t293 + 24, 0);
                ((uint8_t *)&t293)[0] = 0;
                GM_ST(gm_b0_1, ((uint8_t *)&t293 + 8), t292);
                gm_tcfa6l7reoosn t294 = t293;
                uint8_t t295 = ((((uint8_t *)&t294))[0] != 0);
                F.ret = t293;
                goto gm_fret;
            }
            goto ix231;
ie231:;
ix231:;
        }
        {
            uint8_t t297 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l2) + 0) == ((uint8_t)6ULL)));
            if (!t297) goto ie296;
            {
                double t300 = gm_l10;
                uint8_t t301 = gm_f_num_is_nan_b0ma7mo(t300);
                if (!t301) goto ie299;
                {
                    gm_b16_8 t303 = gm_l6;
                    gm_f_fill_abmf5g5(t303, ((uint8_t)0ULL));
                }
                {
                    gm_b0_1 t304;
                    gm_tcfa6l7reoosn t305;
                    GM_ST(uint8_t, (uint8_t *)&t305 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t305 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t305 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t305 + 8, 0);
                    GM_ST(uint64_t, (uint8_t *)&t305 + 16, 0);
                    GM_ST(uint64_t, (uint8_t *)&t305 + 24, 0);
                    ((uint8_t *)&t305)[0] = 0;
                    GM_ST(gm_b0_1, ((uint8_t *)&t305 + 8), t304);
                    gm_tcfa6l7reoosn t306 = t305;
                    uint8_t t307 = ((((uint8_t *)&t306))[0] != 0);
                    F.ret = t305;
                    goto gm_fret;
                }
                goto ix299;
ie299:;
ix299:;
            }
            {
                double t308 = gm_l10;
                double t309 = t308;
                uint64_t t310;
                memset(&t310, 0, sizeof t310); memcpy(&t310, ((uint8_t *)&t309), sizeof t310 < 8 ? sizeof t310 : 8);
                unsigned __int128 n311 = (unsigned __int128)((uint64_t)63ULL);
                if (n311 >= 64) gm_trap_shift((uint64_t)n311, "u64", gm_srcl8pn2pjtmj6k, 133, 21);
                uint64_t t312 = (uint64_t)(t310 >> (int)n311);
                uint8_t t313 = ((t312 == ((uint64_t)1ULL)));
                gm_l25 = t313;
            }
            {
                memset((uint8_t *)&gm_l26 + 1, 0, 15);
                uint8_t t314 = gm_l25;
                GM_ST(uint8_t, (uint8_t *)&gm_l26 + 0, t314);
                unsigned __int128 t315 = (unsigned __int128)~((unsigned __int128)(((unsigned __int128)0u << 64) | 0u));
                GM_ST(unsigned __int128, (uint8_t *)&gm_l26 + 16, t315);
            }
            {
                uint8_t t317 = gm_l21;
                if (!t317) goto ie316;
                {
                    double t319 = gm_l10;
                    gm_tvdg94bk7aa22 t320;
                    GM_ST(uint8_t, (uint8_t *)&t320 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t320 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t320 + 4, 0);
                    GM_ST(uint32_t, (uint8_t *)&t320 + 8, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t320 + 0), ((uint8_t)1ULL));
                    gm_tsp9ul70rnkg6 t321;
                    gm_f_num_float_int_ba5kc1t((void *)&t321, t319, t320);
                    gm_tsp9ul70rnkg6 t322 = t321;
                    gm_t86h6hhm6d76n t323;
                    if (((((uint8_t *)&t322))[32] != 0)) {
                        gm_t86h6hhm6d76n t324 = GM_LD(gm_t86h6hhm6d76n, (((uint8_t *)&t322) + 0));
                        t323 = t324;
                    } else {
                        gm_t86h6hhm6d76n t325 = gm_l26;
                        t323 = t325;
                    }
                    gm_l26 = t323;
                }
                goto ix316;
ie316:;
ix316:;
            }
            {
                void * t326 = gm_l0;
                gm_b16_8 t327 = gm_l6;
                uint8_t t328 = gm_l17;
                uint32_t t329 = gm_l18;
                uint8_t t330 = gm_l17;
                uint32_t t331 = gm_l18;
                gm_t86h6hhm6d76n t332;
                gm_f_num_sat_66dqcpl((void *)&t332, &(*(gm_t86h6hhm6d76n *)((uint8_t *)&gm_l26)), t330, t331);
                unsigned __int128 t333 = gm_f_num_bits_onl28vp(&t332);
                unsigned __int128 t334 = gm_f_num_ext_348kmbu(t328, t329, t333);
                gm_f_put_uint_cddin99(t326, t327, t334);
            }
            {
                gm_b0_1 t335;
                gm_tcfa6l7reoosn t336;
                GM_ST(uint8_t, (uint8_t *)&t336 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t336 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t336 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t336 + 8, 0);
                GM_ST(uint64_t, (uint8_t *)&t336 + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&t336 + 24, 0);
                ((uint8_t *)&t336)[0] = 0;
                GM_ST(gm_b0_1, ((uint8_t *)&t336 + 8), t335);
                gm_tcfa6l7reoosn t337 = t336;
                uint8_t t338 = ((((uint8_t *)&t337))[0] != 0);
                F.ret = t336;
                goto gm_fret;
            }
            goto ix296;
ie296:;
ix296:;
        }
        {
            uint8_t t340 = gm_l21;
            if (!t340) goto ie339;
            {
                double t343 = gm_l10;
                gm_tvdg94bk7aa22 t344 = gm_l2;
                gm_tsp9ul70rnkg6 t345;
                gm_f_num_float_int_ba5kc1t((void *)&t345, t343, t344);
                gm_tsp9ul70rnkg6 t346 = t345;
                if (!((((uint8_t *)&t346))[32] != 0)) goto ie342;
                gm_t86h6hhm6d76n t347 = GM_LD(gm_t86h6hhm6d76n, ((uint8_t *)&t346));
                gm_l27 = t347;
                {
                    uint8_t t350 = gm_l17;
                    uint32_t t351 = gm_l18;
                    uint8_t t352 = gm_f_num_fits_1n7pcsh(&(*(gm_t86h6hhm6d76n *)((uint8_t *)&gm_l27)), t350, t351);
                    if (!t352) goto ie349;
                    {
                        void * t354 = gm_l0;
                        gm_b16_8 t355 = gm_l6;
                        uint8_t t356 = gm_l17;
                        uint32_t t357 = gm_l18;
                        unsigned __int128 t358 = gm_f_num_bits_onl28vp(&(*(gm_t86h6hhm6d76n *)((uint8_t *)&gm_l27)));
                        unsigned __int128 t359 = gm_f_num_ext_348kmbu(t356, t357, t358);
                        gm_f_put_uint_cddin99(t354, t355, t359);
                    }
                    {
                        gm_b0_1 t360;
                        gm_tcfa6l7reoosn t361;
                        GM_ST(uint8_t, (uint8_t *)&t361 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t361 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t361 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t361 + 8, 0);
                        GM_ST(uint64_t, (uint8_t *)&t361 + 16, 0);
                        GM_ST(uint64_t, (uint8_t *)&t361 + 24, 0);
                        ((uint8_t *)&t361)[0] = 0;
                        GM_ST(gm_b0_1, ((uint8_t *)&t361 + 8), t360);
                        gm_tcfa6l7reoosn t362 = t361;
                        uint8_t t363 = ((((uint8_t *)&t362))[0] != 0);
                        F.ret = t361;
                        goto gm_fret;
                    }
                    goto ix349;
ie349:;
ix349:;
                }
                goto ix342;
ie342:;
ix342:;
            }
            goto ix339;
ie339:;
ix339:;
        }
        {
            void * t365 = gm_l0;
            uint8_t t366 = gm_f_fast_23em7sv(t365);
            if (!t366) goto ie364;
            {
                gm_b16_8 t368 = gm_l6;
                gm_f_fill_abmf5g5(t368, ((uint8_t)0ULL));
            }
            {
                gm_b0_1 t369;
                gm_tcfa6l7reoosn t370;
                GM_ST(uint8_t, (uint8_t *)&t370 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t370 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t370 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t370 + 8, 0);
                GM_ST(uint64_t, (uint8_t *)&t370 + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&t370 + 24, 0);
                ((uint8_t *)&t370)[0] = 0;
                GM_ST(gm_b0_1, ((uint8_t *)&t370 + 8), t369);
                gm_tcfa6l7reoosn t371 = t370;
                uint8_t t372 = ((((uint8_t *)&t371))[0] != 0);
                F.ret = t370;
                goto gm_fret;
            }
            goto ix364;
ie364:;
ix364:;
        }
        {
            void * t373 = gm_l0;
            gm_tl874t2atnlh3 t374 = gm_l1;
            gm_b16_8 t375;
            GM_ST(void *, (uint8_t *)&t375, (void *)(gm_s9o2c36nosv54));
            GM_ST(uintptr_t, (uint8_t *)&t375 + 8, (uintptr_t)(40));
            gm_t76123gfet73g t376;
            gm_f_trap_qh84l9d((void *)&t376, (void *)NULL, cx, NULL, t373, t374, t375);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tcfa6l7reoosn t377;
            GM_ST(uint8_t, (uint8_t *)&t377 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t377 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t377 + 4, 0);
            ((uint8_t *)&t377)[0] = 1;
            GM_ST(gm_t76123gfet73g, ((uint8_t *)&t377 + 8), t376);
            gm_tcfa6l7reoosn t378 = t377;
            uint8_t t379 = ((((uint8_t *)&t378))[0] != 0);
            F.ret = t377;
            goto gm_fret;
        }
        goto ix156;
ie156:;
ix156:;
    }
    {
        void * t381 = gm_l0;
        gm_t88rcucg1b7uk t382 = gm_l7;
        gm_t53at80pfdus3 t383;
        gm_f_kind_htv6093((void *)&t383, t381, GM_CONV(gm_csguankko4lmhd, t382));
        gm_t53at80pfdus3 t384 = t383;
        uint8_t t385 = GM_LD(uint8_t, (((uint8_t *)&t384) + 0));
        if (t385 != ((uint8_t)3ULL)) goto ie380;
        uint16_t t386 = GM_LD(uint16_t, (((uint8_t *)&t384) + 8));
        gm_l28 = t386;
        {
            double t388;
            uint8_t t390 = gm_l8;
            if (!t390) goto ie389;
            double t392 = gm_l10;
            uint16_t t393 = gm_l28;
            double t394 = gm_f_num_round_2pna7m3(t392, t393);
            double t395 = t394;
            t388 = t395;
            goto ix389;
ie389:;
            uint16_t t397 = gm_l28;
            double t398 = gm_f_num_int_to_float_4hrvis6(&(*(gm_t86h6hhm6d76n *)((uint8_t *)&gm_l9)), t397);
            double t399 = t398;
            t388 = t399;
ix389:;
            gm_l29 = t388;
        }
        {
            void * t400 = gm_l0;
            gm_t88rcucg1b7uk t401 = gm_l7;
            double t402 = gm_l29;
            gm_b16_8 t403 = gm_l6;
            gm_f_put_float_fiad72d(t400, GM_CONV(gm_csguankko4lmhd, t401), t402, t403);
        }
        {
            gm_b0_1 t404;
            gm_tcfa6l7reoosn t405;
            GM_ST(uint8_t, (uint8_t *)&t405 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t405 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t405 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t405 + 8, 0);
            GM_ST(uint64_t, (uint8_t *)&t405 + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&t405 + 24, 0);
            ((uint8_t *)&t405)[0] = 0;
            GM_ST(gm_b0_1, ((uint8_t *)&t405 + 8), t404);
            gm_tcfa6l7reoosn t406 = t405;
            uint8_t t407 = ((((uint8_t *)&t406))[0] != 0);
            F.ret = t405;
            goto gm_fret;
        }
        goto ix380;
ie380:;
ix380:;
    }
    {
        void * t409 = gm_l0;
        gm_t88rcucg1b7uk t410 = gm_l7;
        gm_tkn42u7na3t81 t411 = gm_f_bits_struct_ty_22ue20s(t409, GM_CONV(gm_csguankko4lmhd, t410));
        gm_tkn42u7na3t81 t412 = t411;
        if (!((((uint8_t *)&t412))[4] != 0)) goto ie408;
        gm_t88rcucg1b7uk t413 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&t412));
        gm_l30 = t413;
        {
            uint8_t t416 = gm_l8;
            uint8_t t417 = (uint8_t)!t416;
            if (!t417) goto ie415;
            {
                void * t419 = gm_l0;
                gm_t88rcucg1b7uk t420 = gm_l30;
                gm_tg5lql6itufk7 t421 = gm_f_int_info_ulcevdv(t419, GM_CONV(gm_csguankko4lmhd, t420));
                gm_tg5lql6itufk7 t422 = t421;
                if (!((((uint8_t *)&t422))[8] != 0)) gm_trap("unwrap of null", gm_srcl8pn2pjtmj6k, 160, 21);
                gm_ttovhfee8mf28 t423 = GM_LD(gm_ttovhfee8mf28, ((uint8_t *)&t422));
                gm_l31 = t423;
            }
            {
                void * t424 = gm_l0;
                gm_b16_8 t425 = gm_l6;
                uint32_t t426 = GM_LD(uint32_t, (((uint8_t *)&gm_l31) + 4));
                unsigned __int128 t427 = gm_f_num_bits_onl28vp(&(*(gm_t86h6hhm6d76n *)((uint8_t *)&gm_l9)));
                unsigned __int128 t428 = gm_f_num_ext_348kmbu(((uint8_t)0), t426, t427);
                gm_f_put_uint_cddin99(t424, t425, t428);
            }
            {
                gm_b0_1 t429;
                gm_tcfa6l7reoosn t430;
                GM_ST(uint8_t, (uint8_t *)&t430 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t430 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t430 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t430 + 8, 0);
                GM_ST(uint64_t, (uint8_t *)&t430 + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&t430 + 24, 0);
                ((uint8_t *)&t430)[0] = 0;
                GM_ST(gm_b0_1, ((uint8_t *)&t430 + 8), t429);
                gm_tcfa6l7reoosn t431 = t430;
                uint8_t t432 = ((((uint8_t *)&t431))[0] != 0);
                F.ret = t430;
                goto gm_fret;
            }
            goto ix415;
ie415:;
ix415:;
        }
        goto ix408;
ie408:;
ix408:;
    }
    {
        void * t433 = gm_l0;
        gm_tl874t2atnlh3 t434 = gm_l1;
        void * t435 = gm_l0;
        void * t436 = GM_LD(void *, (((uint8_t *)t435) + 8));
        gm_t88rcucg1b7uk t437 = gm_l7;
        gm_b16_8 t438 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t436, t437);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tnpifv20irnph t439;
        GM_ST(gm_b16_8, ((uint8_t *)&t439 + 0), t438);
        gm_tabb710d9mq2e t440;
        gm_f_format_fv5tfj6((void *)&t440, (void *)NULL, cx, NULL, t439);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t441 = t440;
        if (((((uint8_t *)&t441))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t441) + 8, gm_srcl8pn2pjtmj6k, 165, 26);
        gm_b16_8 t442 = GM_LD(gm_b16_8, (((uint8_t *)&t441) + 8));
        gm_b16_8 t443 = t442;
        gm_b16_8 t444 = GM_LD(gm_b16_8, ((uint8_t *)&t443));
        gm_t76123gfet73g t445;
        gm_f_trap_qh84l9d((void *)&t445, (void *)NULL, cx, NULL, t433, t434, t444);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tcfa6l7reoosn t446;
        GM_ST(uint8_t, (uint8_t *)&t446 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t446 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t446 + 4, 0);
        ((uint8_t *)&t446)[0] = 1;
        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t446 + 8), t445);
        gm_tcfa6l7reoosn t447 = t446;
        uint8_t t448 = ((((uint8_t *)&t447))[0] != 0);
        F.ret = t446;
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
__attribute__((aligned(4))) void gm_f_hash_int_26mf5j0(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_hash_int_26mf5j0 F;
    void * gm_l0; /* h */
    gm_t86h6hhm6d76n gm_l1; /* x */
    gm_tb1c3uf711glh gm_l2; /* buf */
    unsigned __int128 gm_l3; /* low */
    unsigned __int128 gm_l4; /* <tmp> */
    unsigned __int128 gm_l5; /* <tmp> */
    unsigned __int128 gm_l6; /* i */
    uint64_t gm_l7; /* n */
    GM_STACK_CHECK(&F, 544u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
    }
    {
        unsigned __int128 t2 = gm_f_num_bits_onl28vp(&(*(gm_t86h6hhm6d76n *)((uint8_t *)a1)));
        gm_l3 = t2;
    }
    {
        {
            gm_l5 = ((unsigned __int128)(((unsigned __int128)0u << 64) | 0u));
        }
        {
            gm_l4 = ((unsigned __int128)(((unsigned __int128)0u << 64) | 16u));
        }
        {
            for (;;) {
                unsigned __int128 t5 = gm_l5;
                unsigned __int128 t6 = gm_l4;
                uint8_t t7 = (t5 < t6);
                if (!t7) goto lb4;
                {
                    unsigned __int128 t10 = gm_l5;
                    unsigned __int128 t11 = t10;
                    unsigned __int128 t14 = GM_LD(unsigned __int128, ((uint8_t *)&t11));
                    gm_l6 = t14;
                    {
                        unsigned __int128 t16 = gm_l6;
                        uint64_t t17 = (((unsigned __int128)(t16) > (unsigned __int128)UINTPTR_MAX) ? UINTPTR_MAX : (uintptr_t)(t16));
                        if (t17 >= (uintptr_t)(17)) gm_trap_index(t17, 17, gm_srcl8pn2pjtmj6k, 417, 22);
                        unsigned __int128 t18 = gm_l3;
                        unsigned __int128 t19 = gm_l6;
                        unsigned __int128 r20; bool o21 = __builtin_mul_overflow(t19, ((unsigned __int128)(((unsigned __int128)0u << 64) | 8u)), &r20);
                        if (o21) gm_trap("integer overflow in `*`", gm_srcl8pn2pjtmj6k, 417, 44);
                        unsigned __int128 t22 = r20;
                        unsigned __int128 n23 = (unsigned __int128)t22;
                        if (n23 >= 128) gm_trap_shift((uint64_t)n23, "u128", gm_srcl8pn2pjtmj6k, 417, 32);
                        unsigned __int128 t24 = (unsigned __int128)(t18 >> (int)n23);
                        uint8_t t25 = (uint8_t)t24;
                        GM_ST(uint8_t, (((uint8_t *)&gm_l2) + t17 * 1), t25);
                    }
                    goto me12;
mn13:;
                    gm_trap("no match arm matched the scrutinee", gm_srcl8pn2pjtmj6k, 417, 5);
me12:;
                }
lc4:;
                {
                    unsigned __int128 t27 = gm_l5;
                    unsigned __int128 r28; bool o29 = __builtin_add_overflow(t27, ((unsigned __int128)(((unsigned __int128)0u << 64) | 1u)), &r28);
                    unsigned __int128 t30 = r28;
                    gm_l5 = t30;
                }
            }
lb4:;
        }
    }
    {
        uint64_t t31 = (uintptr_t)((uint64_t)16ULL);
        if (t31 >= (uintptr_t)(17)) gm_trap_index(t31, 17, gm_srcl8pn2pjtmj6k, 418, 5);
        uint8_t *t32 = (((uint8_t *)&gm_l2) + t31 * 1);
        uint8_t t33;
        uint8_t t35 = GM_LD(uint8_t, (((uint8_t *)a1) + 0));
        if (!t35) goto ie34;
        uint8_t t37 = ((uint8_t)255ULL);
        t33 = t37;
        goto ix34;
ie34:;
        uint8_t t39 = ((uint8_t)0ULL);
        t33 = t39;
ix34:;
        GM_ST(uint8_t, t32, t33);
    }
    {
        gm_l7 = ((uint64_t)17ULL);
    }
    {
        for (;;) {
            uint64_t t41 = gm_l7;
            uint8_t t42 = (t41 > ((uint64_t)1ULL));
            if (!t42) goto lb40;
            uint64_t t43 = gm_l7;
            uint64_t r44; bool o45 = __builtin_sub_overflow(t43, ((uint64_t)1ULL), &r44);
            if (o45) gm_trap("integer overflow in `-`", gm_srcl8pn2pjtmj6k, 420, 25);
            uint64_t t46 = r44;
            uint64_t t47 = (uintptr_t)t46;
            if (t47 >= (uintptr_t)(17)) gm_trap_index(t47, 17, gm_srcl8pn2pjtmj6k, 420, 21);
            uint8_t t48 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + t47 * 1));
            uint64_t t49 = (uintptr_t)((uint64_t)16ULL);
            if (t49 >= (uintptr_t)(17)) gm_trap_index(t49, 17, gm_srcl8pn2pjtmj6k, 420, 35);
            uint8_t t50 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + t49 * 1));
            uint8_t t51 = ((t48 == t50));
            if (!t51) goto lb40;
            uint64_t t52 = gm_l7;
            uint64_t r53; bool o54 = __builtin_sub_overflow(t52, ((uint64_t)2ULL), &r53);
            if (o54) gm_trap("integer overflow in `-`", gm_srcl8pn2pjtmj6k, 420, 53);
            uint64_t t55 = r53;
            uint64_t t56 = (uintptr_t)t55;
            if (t56 >= (uintptr_t)(17)) gm_trap_index(t56, 17, gm_srcl8pn2pjtmj6k, 420, 49);
            uint8_t t57 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + t56 * 1));
            unsigned __int128 n58 = (unsigned __int128)((uint64_t)7ULL);
            if (n58 >= 8) gm_trap_shift((uint64_t)n58, "u8", gm_srcl8pn2pjtmj6k, 420, 49);
            uint8_t t59 = (uint8_t)(t57 >> (int)n58);
            uint8_t t60 = ((t59 == ((uint8_t)1ULL)));
            uint8_t t61 = GM_LD(uint8_t, (((uint8_t *)a1) + 0));
            uint8_t t62 = ((t60 == t61));
            if (!t62) goto lb40;
            {
                uint64_t t65 = gm_l7;
                uint64_t r66; bool o67 = __builtin_sub_overflow(t65, ((uint64_t)1ULL), &r66);
                if (o67) gm_trap("integer overflow in `-`", gm_srcl8pn2pjtmj6k, 420, 83);
                uint64_t t68 = r66;
                gm_l7 = t68;
            }
lc40:;
        }
lb40:;
    }
    {
        void * t69 = gm_l0;
        void * t70 = ((uint8_t *)&gm_l2);
        uint64_t t71 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t72 = gm_l7;
        uint64_t t73 = (uintptr_t)t72;
        if (t71 > t73 || t73 > (uintptr_t)17) gm_trap_slice(t71, t73, 17, gm_srcl8pn2pjtmj6k, 421, 17);
        gm_b16_8 t74;
        GM_ST(void *, (uint8_t *)&t74, (void *)(((uint8_t *)t70 + t71 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t74 + 8, (uintptr_t)(t73 - t71));
        gm_b16_8 t75 = t74;
        gm_b16_8 t76 = GM_LD(gm_b16_8, ((uint8_t *)&t75));
        gm_f_hash_mix_1abfg0l(t69, t76);
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) void gm_f_hash_int_26mf5j0_c(void * a0, gm_csia7oevr2u0fk a1) { gm_f_hash_int_26mf5j0(a0, &a1); }
inline __attribute__((aligned(4))) void gm_f_hash_mix_1abfg0l(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_hash_mix_1abfg0l F;
    void * gm_l0; /* h */
    gm_b16_8 gm_l1; /* bytes */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    uint8_t gm_l4; /* x */
    GM_STACK_CHECK(&F, 174u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        {
            gm_b16_8 t3 = gm_l1;
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
                    uint8_t t14 = t13;
                    uint8_t t17 = GM_LD(uint8_t, ((uint8_t *)&t14));
                    gm_l4 = t17;
                    {
                        void * t19 = gm_l0;
                        void * t20 = gm_l0;
                        uint64_t t21 = GM_LD(uint64_t, ((uint8_t *)t20));
                        uint8_t t22 = gm_l4;
                        uint64_t t23 = (uint64_t)t22;
                        uint64_t t24 = (uint64_t)(t21 ^ t23);
                        uint64_t r25; bool o26 = __builtin_mul_overflow(t24, ((uint64_t)1099511628211ULL), &r25);
                        uint64_t t27 = r25;
                        GM_ST(uint64_t, ((uint8_t *)t19), t27);
                    }
                    goto me15;
mn16:;
                    gm_trap("no match arm matched the scrutinee", gm_srcl8pn2pjtmj6k, 405, 5);
me15:;
                }
lc4:;
                {
                    uint64_t t29 = gm_l3;
                    uint64_t r30; bool o31 = __builtin_add_overflow(t29, ((uint64_t)1ULL), &r30);
                    uint64_t t32 = r30;
                    gm_l3 = t32;
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
inline __attribute__((aligned(4))) void gm_f_hash_u64_32bripn(void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_hash_u64_32bripn F;
    void * gm_l0; /* h */
    uint64_t gm_l1; /* x */
    uint64_t gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    uint64_t gm_l4; /* i */
    GM_STACK_CHECK(&F, 177u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        {
            gm_l3 = ((uint64_t)0ULL);
        }
        {
            gm_l2 = ((uint64_t)8ULL);
        }
        {
            for (;;) {
                uint64_t t4 = gm_l3;
                uint64_t t5 = gm_l2;
                uint8_t t6 = (t4 < t5);
                if (!t6) goto lb3;
                {
                    uint64_t t9 = gm_l3;
                    uint64_t t10 = t9;
                    uint64_t t13 = GM_LD(uint64_t, ((uint8_t *)&t10));
                    gm_l4 = t13;
                    {
                        void * t15 = gm_l0;
                        void * t16 = gm_l0;
                        uint64_t t17 = GM_LD(uint64_t, ((uint8_t *)t16));
                        uint64_t t18 = gm_l1;
                        uint64_t t19 = gm_l4;
                        uint64_t r20; bool o21 = __builtin_mul_overflow(t19, ((uint64_t)8ULL), &r20);
                        if (o21) gm_trap("integer overflow in `*`", gm_srcl8pn2pjtmj6k, 410, 43);
                        uint64_t t22 = r20;
                        unsigned __int128 n23 = (unsigned __int128)t22;
                        if (n23 >= 64) gm_trap_shift((uint64_t)n23, "u64", gm_srcl8pn2pjtmj6k, 410, 34);
                        uint64_t t24 = (uint64_t)(t18 >> (int)n23);
                        uint64_t t25 = (uint64_t)(t24 & ((uint64_t)255ULL));
                        uint64_t t26 = (uint64_t)(t17 ^ t25);
                        uint64_t r27; bool o28 = __builtin_mul_overflow(t26, ((uint64_t)1099511628211ULL), &r27);
                        uint64_t t29 = r27;
                        GM_ST(uint64_t, ((uint8_t *)t15), t29);
                    }
                    goto me11;
mn12:;
                    gm_trap("no match arm matched the scrutinee", gm_srcl8pn2pjtmj6k, 410, 5);
me11:;
                }
lc3:;
                {
                    uint64_t t31 = gm_l3;
                    uint64_t r32; bool o33 = __builtin_add_overflow(t31, ((uint64_t)1ULL), &r32);
                    uint64_t t34 = r32;
                    gm_l3 = t34;
                }
            }
lb3:;
        }
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_hash_u64_byte_nd76h12(void * a0, uint8_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_hash_u64_byte_nd76h12 F;
    void * gm_l0; /* h */
    uint8_t gm_l1; /* x */
    GM_STACK_CHECK(&F, 58u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        void * t3 = gm_l0;
        uint64_t t4 = GM_LD(uint64_t, ((uint8_t *)t3));
        uint8_t t5 = gm_l1;
        uint64_t t6 = (uint64_t)t5;
        uint64_t t7 = (uint64_t)(t4 ^ t6);
        uint64_t r8; bool o9 = __builtin_mul_overflow(t7, ((uint64_t)1099511628211ULL), &r8);
        uint64_t t10 = r8;
        GM_ST(uint64_t, ((uint8_t *)t2), t10);
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) void gm_f_hash_val_nr2e99r(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3, void * a4) {
    struct gm_fr_hash_val_nr2e99r F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_t88rcucg1b7uk gm_l2; /* t */
    gm_b16_8 gm_l3; /* b */
    void * gm_l4; /* h */
    gm_ttovhfee8mf28 gm_l5; /* info */
    double gm_l6; /* x */
    gm_b16_8 gm_l7; /* elems */
    uint64_t gm_l8; /* <tmp> */
    uint64_t gm_l9; /* <tmp> */
    uint64_t gm_l10; /* i */
    uint64_t gm_l11; /* n */
    gm_t88rcucg1b7uk gm_l12; /* elem */
    uint64_t gm_l13; /* esz */
    uint64_t gm_l14; /* <tmp> */
    uint64_t gm_l15; /* <tmp> */
    uint64_t gm_l16; /* i */
    uint64_t gm_l18; /* <tmp> */
    uint64_t gm_l19; /* <tmp> */
    uint64_t gm_l20; /* i */
    gm_t88rcucg1b7uk gm_l21; /* inner */
    gm_t88rcucg1b7uk gm_l22; /* err */
    gm_t88rcucg1b7uk gm_l23; /* ok */
    uint8_t gm_l24; /* is_err */
    gm_t88rcucg1b7uk gm_l25; /* u */
    gm_tcmkompbl09il gm_l26; /* u */
    uint32_t gm_l27; /* vi */
    gm_t88rcucg1b7uk gm_l28; /* pt */
    uint64_t gm_l29; /* off */
    void * gm_l30; /* a */
    uint64_t gm_l32; /* <tmp> */
    uint64_t gm_l33; /* <tmp> */
    uint64_t gm_l34; /* i */
    gm_tfbnciphf3261 gm_l35; /* fl */
    uint64_t gm_l36; /* off */
    GM_STACK_CHECK(&F, 5140u);
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
            void * t3 = gm_l0;
            gm_t88rcucg1b7uk t4 = gm_l2;
            gm_t53at80pfdus3 t5;
            gm_f_kind_htv6093((void *)&t5, t3, GM_CONV(gm_csguankko4lmhd, t4));
            gm_t53at80pfdus3 t6 = t5;
            uint8_t t21 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
            switch (t21) { case ((uint8_t)0ULL): goto ma8; case ((uint8_t)1ULL): goto ma8; case ((uint8_t)2ULL): goto ma8; case ((uint8_t)3ULL): goto ma9; case ((uint8_t)4ULL): goto ma10; case ((uint8_t)5ULL): goto ma12; case ((uint8_t)6ULL): goto ma19; case ((uint8_t)7ULL): goto ma11; case ((uint8_t)8ULL): goto ma11; case ((uint8_t)9ULL): goto ma11; case ((uint8_t)10ULL): goto ma15; case ((uint8_t)11ULL): goto ma14; case ((uint8_t)12ULL): goto ma13; case ((uint8_t)13ULL): goto ma16; case ((uint8_t)14ULL): goto ma17; case ((uint8_t)15ULL): goto ma19; case ((uint8_t)16ULL): goto ma11; case ((uint8_t)17ULL): goto ma18; case ((uint8_t)18ULL): goto ma19; case ((uint8_t)19ULL): goto ma19; case ((uint8_t)20ULL): goto ma19; case ((uint8_t)21ULL): goto ma19; case ((uint8_t)22ULL): goto ma19; case ((uint8_t)23ULL): goto ma19; case ((uint8_t)24ULL): goto ma19; case ((uint8_t)25ULL): goto ma19; case ((uint8_t)26ULL): goto ma19; default: goto ma19; }
ma8:;
            uint8_t t25 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
            if (t25 != ((uint8_t)0ULL)) goto pa24;
            goto po23;
pa24:;
            uint8_t t27 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
            if (t27 != ((uint8_t)1ULL)) goto pa26;
            goto po23;
pa26:;
            uint8_t t29 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
            if (t29 != ((uint8_t)2ULL)) goto pa28;
            goto po23;
pa28:;
            goto mn22;
po23:;
            {
                void * t31 = gm_l0;
                gm_t88rcucg1b7uk t32 = gm_l2;
                gm_tg5lql6itufk7 t33 = gm_f_int_info_ulcevdv(t31, GM_CONV(gm_csguankko4lmhd, t32));
                gm_tg5lql6itufk7 t34 = t33;
                if (!((((uint8_t *)&t34))[8] != 0)) gm_trap("unwrap of null", gm_srcl8pn2pjtmj6k, 428, 21);
                gm_ttovhfee8mf28 t35 = GM_LD(gm_ttovhfee8mf28, ((uint8_t *)&t34));
                gm_l5 = t35;
            }
            {
                void * t36 = gm_l4;
                uint8_t t37 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
                void * t38 = gm_l0;
                gm_t88rcucg1b7uk t39 = gm_l2;
                gm_b16_8 t40 = gm_l3;
                gm_b16_8 t41 = t40;
                gm_b16_8 t42 = GM_LD(gm_b16_8, ((uint8_t *)&t41));
                unsigned __int128 t43 = gm_f_get_int_pg7p4gc(t38, GM_CONV(gm_csguankko4lmhd, t39), t42);
                gm_t86h6hhm6d76n t44;
                gm_f_num_val_i3upeur((void *)&t44, t37, t43);
                gm_f_hash_int_26mf5j0(t36, &t44);
            }
            goto me7;
mn22:;
ma9:;
            uint8_t t46 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
            if (t46 != ((uint8_t)3ULL)) goto mn45;
            {
                void * t48 = gm_l0;
                gm_t88rcucg1b7uk t49 = gm_l2;
                gm_b16_8 t50 = gm_l3;
                gm_b16_8 t51 = t50;
                gm_b16_8 t52 = GM_LD(gm_b16_8, ((uint8_t *)&t51));
                double t53 = gm_f_get_float_grmhlm4(t48, GM_CONV(gm_csguankko4lmhd, t49), t52);
                gm_l6 = t53;
            }
            {
                double t55 = gm_l6;
                uint8_t t56 = ((t55 == ((double)0e0)));
                if (!t56) goto ie54;
                {
                    gm_l6 = ((double)0e0);
                }
                goto ix54;
ie54:;
ix54:;
            }
            {
                void * t58 = gm_l4;
                double t59 = gm_l6;
                double t60 = t59;
                uint64_t t61;
                memset(&t61, 0, sizeof t61); memcpy(&t61, ((uint8_t *)&t60), sizeof t61 < 8 ? sizeof t61 : 8);
                gm_f_hash_u64_32bripn(t58, t61);
            }
            goto me7;
mn45:;
ma10:;
            uint8_t t63 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
            if (t63 != ((uint8_t)4ULL)) goto mn62;
            void * t64 = gm_l4;
            uint8_t t65;
            uint64_t t67 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t68 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
            if (t67 >= (uintptr_t)(t68)) gm_trap_index(t67, t68, gm_srcl8pn2pjtmj6k, 436, 38);
            uint8_t t69 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t67 * 1));
            uint8_t t70 = ((t69 == ((uint8_t)0ULL)));
            if (!t70) goto ie66;
            uint8_t t72 = ((uint8_t)0ULL);
            t65 = t72;
            goto ix66;
ie66:;
            uint8_t t74 = ((uint8_t)1ULL);
            t65 = t74;
ix66:;
            gm_f_hash_u64_byte_nd76h12(t64, t65);
            goto me7;
mn62:;
ma11:;
            uint8_t t78 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
            if (t78 != ((uint8_t)8ULL)) goto pa77;
            goto po76;
pa77:;
            uint8_t t80 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
            if (t80 != ((uint8_t)9ULL)) goto pa79;
            goto po76;
pa79:;
            uint8_t t82 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
            if (t82 != ((uint8_t)7ULL)) goto pa81;
            goto po76;
pa81:;
            uint8_t t84 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
            if (t84 != ((uint8_t)16ULL)) goto pa83;
            goto po76;
pa83:;
            goto mn75;
po76:;
            void * t85 = gm_l4;
            void * t86 = gm_l0;
            gm_b16_8 t87 = gm_l3;
            gm_b16_8 t88 = t87;
            gm_b16_8 t89 = GM_LD(gm_b16_8, ((uint8_t *)&t88));
            uint64_t t90 = gm_f_ptr_of_b77b4c9(t86, t89);
            gm_f_hash_u64_32bripn(t85, t90);
            goto me7;
mn75:;
ma12:;
            uint8_t t92 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
            if (t92 != ((uint8_t)5ULL)) goto mn91;
            goto me7;
mn91:;
ma13:;
            uint8_t t95 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
            if (t95 != ((uint8_t)12ULL)) goto mn94;
            gm_b16_8 t96 = GM_LD(gm_b16_8, ((((uint8_t *)&t6) + 8) + 0));
            gm_l7 = t96;
            {
                {
                    gm_l9 = ((uint64_t)0ULL);
                }
                {
                    uint64_t t99 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                    gm_l8 = t99;
                }
                {
                    for (;;) {
                        uint64_t t101 = gm_l9;
                        uint64_t t102 = gm_l8;
                        uint8_t t103 = (t101 < t102);
                        if (!t103) goto lb100;
                        {
                            uint64_t t106 = gm_l9;
                            uint64_t t107 = t106;
                            uint64_t t110 = GM_LD(uint64_t, ((uint8_t *)&t107));
                            gm_l10 = t110;
                            {
                                void * t112 = gm_l0;
                                gm_tl874t2atnlh3 t113 = gm_l1;
                                uint64_t t114 = gm_l10;
                                uint64_t t115 = (uintptr_t)t114;
                                uint64_t t116 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                                if (t115 >= (uintptr_t)(t116)) gm_trap_index(t115, t116, gm_srcl8pn2pjtmj6k, 440, 58);
                                gm_t88rcucg1b7uk t117 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l7)) + t115 * 4));
                                void * t118 = gm_l0;
                                gm_t88rcucg1b7uk t119 = gm_l2;
                                gm_b16_8 t120 = gm_l3;
                                uint64_t t121 = gm_l10;
                                uint32_t t122 = (uint32_t)t121;
                                gm_b16_8 t123 = gm_f_field_bytes_bhivr4s((void *)NULL, cx, NULL, t118, t119, t120, t122);
                                if (gm_unw.kind) {
                                    goto gm_ll104;
                                }
                                void * t124 = gm_l4;
                                gm_tcfa6l7reoosn t125;
                                gm_f_hash_val_nr2e99r((void *)&t125, (void *)NULL, cx, NULL, t112, t113, t117, t123, t124);
                                if (gm_unw.kind) {
                                    goto gm_ll104;
                                }
                                gm_tcfa6l7reoosn t126 = t125;
                                if (((((uint8_t *)&t126))[0] != 0)) {
                                    gm_t76123gfet73g t127 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t126) + 8));
                                    gm_tcfa6l7reoosn t128;
                                    GM_ST(uint8_t, (uint8_t *)&t128 + 1, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t128 + 2, 0);
                                    GM_ST(uint32_t, (uint8_t *)&t128 + 4, 0);
                                    ((uint8_t *)&t128)[0] = 1;
                                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t128 + 8), t127);
                                    F.ret = t128;
                                    goto gm_fret;
                                }
                                gm_b0_1 t129 = GM_LD(gm_b0_1, (((uint8_t *)&t126) + 8));
                            }
                            goto me108;
mn109:;
                            gm_trap("no match arm matched the scrutinee", gm_srcl8pn2pjtmj6k, 440, 13);
me108:;
                        }
lc100:;
                        {
                            uint64_t t131 = gm_l9;
                            uint64_t r132; bool o133 = __builtin_add_overflow(t131, ((uint64_t)1ULL), &r132);
                            uint64_t t134 = r132;
                            gm_l9 = t134;
                        }
                    }
gm_ll104:;
                    if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb100; }
                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc100; }
                    }
                    goto gm_lad;
lb100:;
                }
            }
            goto me7;
mn94:;
ma14:;
            uint8_t t136 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
            if (t136 != ((uint8_t)11ULL)) goto mn135;
            uint64_t t137 = GM_LD(uint64_t, ((((uint8_t *)&t6) + 8) + 0));
            gm_l11 = t137;
            gm_t88rcucg1b7uk t138 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t6) + 8) + 8));
            gm_l12 = t138;
            {
                void * t140 = gm_l0;
                gm_t88rcucg1b7uk t141 = gm_l12;
                uint64_t t142 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t140, t141);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint64_t t143 = gm_f_us_cuv01fp(t142);
                gm_l13 = t143;
            }
            {
                {
                    gm_l15 = ((uint64_t)0ULL);
                }
                {
                    uint64_t t145 = gm_l11;
                    uint64_t t146 = gm_f_us_cuv01fp(t145);
                    gm_l14 = t146;
                }
                {
                    for (;;) {
                        uint64_t t148 = gm_l15;
                        uint64_t t149 = gm_l14;
                        uint8_t t150 = (t148 < t149);
                        if (!t150) goto lb147;
                        {
                            uint64_t t153 = gm_l15;
                            uint64_t t154 = t153;
                            uint64_t t157 = GM_LD(uint64_t, ((uint8_t *)&t154));
                            gm_l16 = t157;
                            {
                                void * t159 = gm_l0;
                                gm_tl874t2atnlh3 t160 = gm_l1;
                                gm_t88rcucg1b7uk t161 = gm_l12;
                                uint64_t t162 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                                void * t163 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)));
                                uint64_t t164 = gm_l16;
                                uint64_t t165 = gm_l13;
                                uint64_t r166; bool o167 = __builtin_mul_overflow(t164, t165, &r166);
                                if (o167) gm_trap("integer overflow in `*`", gm_srcl8pn2pjtmj6k, 444, 62);
                                uint64_t t168 = r166;
                                uint64_t t169 = (uintptr_t)t168;
                                uint64_t t170 = gm_l16;
                                uint64_t r171; bool o172 = __builtin_add_overflow(t170, ((uint64_t)1ULL), &r171);
                                if (o172) gm_trap("integer overflow in `+`", gm_srcl8pn2pjtmj6k, 444, 72);
                                uint64_t t173 = r171;
                                uint64_t t174 = gm_l13;
                                uint64_t r175; bool o176 = __builtin_mul_overflow(t173, t174, &r175);
                                if (o176) gm_trap("integer overflow in `*`", gm_srcl8pn2pjtmj6k, 444, 71);
                                uint64_t t177 = r175;
                                uint64_t t178 = (uintptr_t)t177;
                                if (t169 > t178 || t178 > (uintptr_t)t162) gm_trap_slice(t169, t178, t162, gm_srcl8pn2pjtmj6k, 444, 60);
                                gm_b16_8 t179;
                                GM_ST(void *, (uint8_t *)&t179, (void *)(((uint8_t *)t163 + t169 * 1)));
                                GM_ST(uintptr_t, (uint8_t *)&t179 + 8, (uintptr_t)(t178 - t169));
                                void * t180 = gm_l4;
                                gm_tcfa6l7reoosn t181;
                                gm_f_hash_val_nr2e99r((void *)&t181, (void *)NULL, cx, NULL, t159, t160, t161, t179, t180);
                                if (gm_unw.kind) {
                                    goto gm_ll151;
                                }
                                gm_tcfa6l7reoosn t182 = t181;
                                if (((((uint8_t *)&t182))[0] != 0)) {
                                    gm_t76123gfet73g t183 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t182) + 8));
                                    gm_tcfa6l7reoosn t184;
                                    GM_ST(uint8_t, (uint8_t *)&t184 + 1, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t184 + 2, 0);
                                    GM_ST(uint32_t, (uint8_t *)&t184 + 4, 0);
                                    ((uint8_t *)&t184)[0] = 1;
                                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t184 + 8), t183);
                                    F.ret = t184;
                                    goto gm_fret;
                                }
                                gm_b0_1 t185 = GM_LD(gm_b0_1, (((uint8_t *)&t182) + 8));
                            }
                            goto me155;
mn156:;
                            gm_trap("no match arm matched the scrutinee", gm_srcl8pn2pjtmj6k, 444, 13);
me155:;
                        }
lc147:;
                        {
                            uint64_t t187 = gm_l15;
                            uint64_t r188; bool o189 = __builtin_add_overflow(t187, ((uint64_t)1ULL), &r188);
                            uint64_t t190 = r188;
                            gm_l15 = t190;
                        }
                    }
gm_ll151:;
                    if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb147; }
                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc147; }
                    }
                    goto gm_lad;
lb147:;
                }
            }
            goto me7;
mn135:;
ma15:;
            uint8_t t192 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
            if (t192 != ((uint8_t)10ULL)) goto mn191;
            {
                void * t194 = gm_l0;
                gm_tl874t2atnlh3 t195 = gm_l1;
                gm_t88rcucg1b7uk t196 = gm_l2;
                gm_b16_8 t197 = gm_l3;
                gm_tmc91gv7n9c0i t198;
                gm_f_seq_of_ifahtjs((void *)&t198, (void *)NULL, cx, NULL, t194, t195, t196, t197);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tmc91gv7n9c0i t199 = t198;
                if (((((uint8_t *)&t199))[0] != 0)) {
                    gm_t76123gfet73g t200 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t199) + 8));
                    gm_tcfa6l7reoosn t201;
                    GM_ST(uint8_t, (uint8_t *)&t201 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t201 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t201 + 4, 0);
                    ((uint8_t *)&t201)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t201 + 8), t200);
                    F.ret = t201;
                    goto gm_fret;
                }
                gm_t2jeg3bg7sfqa t202 = GM_LD(gm_t2jeg3bg7sfqa, (((uint8_t *)&t199) + 8));
                F.l17 = t202;
            }
            {
                void * t203 = gm_l4;
                uint64_t t204 = GM_LD(uint64_t, (((uint8_t *)&F.l17) + 16));
                gm_f_hash_u64_32bripn(t203, t204);
            }
            {
                {
                    gm_l19 = ((uint64_t)0ULL);
                }
                {
                    uint64_t t206 = GM_LD(uint64_t, (((uint8_t *)&F.l17) + 16));
                    uint64_t t207 = gm_f_us_cuv01fp(t206);
                    gm_l18 = t207;
                }
                {
                    for (;;) {
                        uint64_t t209 = gm_l19;
                        uint64_t t210 = gm_l18;
                        uint8_t t211 = (t209 < t210);
                        if (!t211) goto lb208;
                        {
                            uint64_t t214 = gm_l19;
                            uint64_t t215 = t214;
                            uint64_t t218 = GM_LD(uint64_t, ((uint8_t *)&t215));
                            gm_l20 = t218;
                            {
                                void * t220 = gm_l0;
                                gm_tl874t2atnlh3 t221 = gm_l1;
                                gm_t88rcucg1b7uk t222 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l17) + 0));
                                uint64_t t223 = GM_LD(uintptr_t, (((uint8_t *)&F.l17) + 24) + 8);
                                void * t224 = ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l17) + 24)));
                                uint64_t t225 = gm_l20;
                                uint64_t t226 = GM_LD(uint64_t, (((uint8_t *)&F.l17) + 8));
                                uint64_t r227; bool o228 = __builtin_mul_overflow(t225, t226, &r227);
                                if (o228) gm_trap("integer overflow in `*`", gm_srcl8pn2pjtmj6k, 449, 71);
                                uint64_t t229 = r227;
                                uint64_t t230 = (uintptr_t)t229;
                                uint64_t t231 = gm_l20;
                                uint64_t r232; bool o233 = __builtin_add_overflow(t231, ((uint64_t)1ULL), &r232);
                                if (o233) gm_trap("integer overflow in `+`", gm_srcl8pn2pjtmj6k, 449, 83);
                                uint64_t t234 = r232;
                                uint64_t t235 = GM_LD(uint64_t, (((uint8_t *)&F.l17) + 8));
                                uint64_t r236; bool o237 = __builtin_mul_overflow(t234, t235, &r236);
                                if (o237) gm_trap("integer overflow in `*`", gm_srcl8pn2pjtmj6k, 449, 82);
                                uint64_t t238 = r236;
                                uint64_t t239 = (uintptr_t)t238;
                                if (t230 > t239 || t239 > (uintptr_t)t223) gm_trap_slice(t230, t239, t223, gm_srcl8pn2pjtmj6k, 449, 64);
                                gm_b16_8 t240;
                                GM_ST(void *, (uint8_t *)&t240, (void *)(((uint8_t *)t224 + t230 * 1)));
                                GM_ST(uintptr_t, (uint8_t *)&t240 + 8, (uintptr_t)(t239 - t230));
                                void * t241 = gm_l4;
                                gm_tcfa6l7reoosn t242;
                                gm_f_hash_val_nr2e99r((void *)&t242, (void *)NULL, cx, NULL, t220, t221, t222, t240, t241);
                                if (gm_unw.kind) {
                                    goto gm_ll212;
                                }
                                gm_tcfa6l7reoosn t243 = t242;
                                if (((((uint8_t *)&t243))[0] != 0)) {
                                    gm_t76123gfet73g t244 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t243) + 8));
                                    gm_tcfa6l7reoosn t245;
                                    GM_ST(uint8_t, (uint8_t *)&t245 + 1, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t245 + 2, 0);
                                    GM_ST(uint32_t, (uint8_t *)&t245 + 4, 0);
                                    ((uint8_t *)&t245)[0] = 1;
                                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t245 + 8), t244);
                                    F.ret = t245;
                                    goto gm_fret;
                                }
                                gm_b0_1 t246 = GM_LD(gm_b0_1, (((uint8_t *)&t243) + 8));
                            }
                            goto me216;
mn217:;
                            gm_trap("no match arm matched the scrutinee", gm_srcl8pn2pjtmj6k, 449, 13);
me216:;
                        }
lc208:;
                        {
                            uint64_t t248 = gm_l19;
                            uint64_t r249; bool o250 = __builtin_add_overflow(t248, ((uint64_t)1ULL), &r249);
                            uint64_t t251 = r249;
                            gm_l19 = t251;
                        }
                    }
gm_ll212:;
                    if (gm_unw.frame == &F.h && gm_unw.label == 2) {
                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb208; }
                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc208; }
                    }
                    goto gm_lad;
lb208:;
                }
            }
            goto me7;
mn191:;
ma16:;
            uint8_t t253 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
            if (t253 != ((uint8_t)13ULL)) goto mn252;
            gm_t88rcucg1b7uk t254 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t6) + 8));
            gm_l21 = t254;
            {
                void * t257 = gm_l0;
                void * t258 = GM_LD(void *, (((uint8_t *)t257) + 8));
                gm_t88rcucg1b7uk t259 = gm_l21;
                uint8_t t260 = gm_f_nullable_pointer_7qfmn6o(t258, GM_CONV(gm_csguankko4lmhd, t259));
                if (!t260) goto ie256;
                {
                    void * t262 = gm_l4;
                    void * t263 = gm_l0;
                    gm_b16_8 t264 = gm_l3;
                    gm_b16_8 t265 = t264;
                    gm_b16_8 t266 = GM_LD(gm_b16_8, ((uint8_t *)&t265));
                    uint64_t t267 = gm_f_ptr_of_b77b4c9(t263, t266);
                    gm_f_hash_u64_32bripn(t262, t267);
                }
                goto ix256;
ie256:;
                void * t269 = gm_l0;
                gm_t88rcucg1b7uk t270 = gm_l2;
                gm_b16_8 t271 = gm_l3;
                gm_b16_8 t272 = t271;
                gm_b16_8 t273 = GM_LD(gm_b16_8, ((uint8_t *)&t272));
                uint8_t t274 = gm_f_opt_present_lb4i2ip((void *)NULL, cx, NULL, t269, t270, t273);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (!t274) goto ie268;
                {
                    void * t276 = gm_l4;
                    gm_f_hash_u64_byte_nd76h12(t276, ((uint8_t)1ULL));
                }
                {
                    void * t277 = gm_l0;
                    gm_tl874t2atnlh3 t278 = gm_l1;
                    gm_t88rcucg1b7uk t279 = gm_l21;
                    void * t280 = gm_l0;
                    gm_t88rcucg1b7uk t281 = gm_l2;
                    gm_b16_8 t282 = gm_l3;
                    gm_b16_8 t283 = gm_f_opt_payload_1pbnobb((void *)NULL, cx, NULL, t280, t281, t282);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    void * t284 = gm_l4;
                    gm_tcfa6l7reoosn t285;
                    gm_f_hash_val_nr2e99r((void *)&t285, (void *)NULL, cx, NULL, t277, t278, t279, t283, t284);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tcfa6l7reoosn t286 = t285;
                    if (((((uint8_t *)&t286))[0] != 0)) {
                        gm_t76123gfet73g t287 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t286) + 8));
                        gm_tcfa6l7reoosn t288;
                        GM_ST(uint8_t, (uint8_t *)&t288 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t288 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t288 + 4, 0);
                        ((uint8_t *)&t288)[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t288 + 8), t287);
                        F.ret = t288;
                        goto gm_fret;
                    }
                    gm_b0_1 t289 = GM_LD(gm_b0_1, (((uint8_t *)&t286) + 8));
                }
                goto ix268;
ie268:;
                {
                    void * t291 = gm_l4;
                    gm_f_hash_u64_byte_nd76h12(t291, ((uint8_t)0ULL));
                }
ix268:;
ix256:;
            }
            goto me7;
mn252:;
ma17:;
            uint8_t t293 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
            if (t293 != ((uint8_t)14ULL)) goto mn292;
            gm_t88rcucg1b7uk t294 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t6) + 8) + 0));
            gm_l22 = t294;
            gm_t88rcucg1b7uk t295 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t6) + 8) + 4));
            gm_l23 = t295;
            {
                void * t297 = gm_l0;
                gm_t88rcucg1b7uk t298 = gm_l2;
                gm_b16_8 t299 = gm_l3;
                gm_b16_8 t300 = t299;
                gm_b16_8 t301 = GM_LD(gm_b16_8, ((uint8_t *)&t300));
                uint8_t t302 = gm_f_res_is_err_8jbgndp((void *)NULL, cx, NULL, t297, t298, t301);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l24 = t302;
            }
            {
                void * t303 = gm_l4;
                uint8_t t304;
                uint8_t t306 = gm_l24;
                if (!t306) goto ie305;
                uint8_t t308 = ((uint8_t)1ULL);
                t304 = t308;
                goto ix305;
ie305:;
                uint8_t t310 = ((uint8_t)0ULL);
                t304 = t310;
ix305:;
                gm_f_hash_u64_byte_nd76h12(t303, t304);
            }
            {
                void * t311 = gm_l0;
                gm_tl874t2atnlh3 t312 = gm_l1;
                gm_t88rcucg1b7uk t313;
                uint8_t t315 = gm_l24;
                if (!t315) goto ie314;
                gm_t88rcucg1b7uk t317 = gm_l22;
                gm_t88rcucg1b7uk t318 = t317;
                t313 = t318;
                goto ix314;
ie314:;
                gm_t88rcucg1b7uk t320 = gm_l23;
                gm_t88rcucg1b7uk t321 = t320;
                t313 = t321;
ix314:;
                void * t322 = gm_l0;
                gm_t88rcucg1b7uk t323 = gm_l2;
                gm_b16_8 t324 = gm_l3;
                gm_b16_8 t325 = gm_f_res_payload_pb2hcmh((void *)NULL, cx, NULL, t322, t323, t324);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                void * t326 = gm_l4;
                gm_tcfa6l7reoosn t327;
                gm_f_hash_val_nr2e99r((void *)&t327, (void *)NULL, cx, NULL, t311, t312, t313, t325, t326);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tcfa6l7reoosn t328 = t327;
                if (((((uint8_t *)&t328))[0] != 0)) {
                    gm_t76123gfet73g t329 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t328) + 8));
                    gm_tcfa6l7reoosn t330;
                    GM_ST(uint8_t, (uint8_t *)&t330 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t330 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t330 + 4, 0);
                    ((uint8_t *)&t330)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t330 + 8), t329);
                    F.ret = t330;
                    goto gm_fret;
                }
                gm_b0_1 t331 = GM_LD(gm_b0_1, (((uint8_t *)&t328) + 8));
            }
            goto me7;
mn292:;
ma18:;
            uint8_t t333 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
            if (t333 != ((uint8_t)17ULL)) goto mn332;
            {
                void * t336 = gm_l0;
                gm_t88rcucg1b7uk t337 = gm_l2;
                gm_tkn42u7na3t81 t338 = gm_f_bits_struct_ty_22ue20s(t336, GM_CONV(gm_csguankko4lmhd, t337));
                gm_tkn42u7na3t81 t339 = t338;
                if (!((((uint8_t *)&t339))[4] != 0)) goto ie335;
                gm_t88rcucg1b7uk t340 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&t339));
                gm_l25 = t340;
                {
                    void * t342 = gm_l0;
                    gm_tl874t2atnlh3 t343 = gm_l1;
                    gm_t88rcucg1b7uk t344 = gm_l25;
                    gm_b16_8 t345 = gm_l3;
                    void * t346 = gm_l4;
                    gm_tcfa6l7reoosn t347;
                    gm_f_hash_val_nr2e99r((void *)&t347, (void *)NULL, cx, NULL, t342, t343, t344, t345, t346);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tcfa6l7reoosn t348 = t347;
                    if (((((uint8_t *)&t348))[0] != 0)) {
                        gm_t76123gfet73g t349 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t348) + 8));
                        gm_tcfa6l7reoosn t350;
                        GM_ST(uint8_t, (uint8_t *)&t350 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t350 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t350 + 4, 0);
                        ((uint8_t *)&t350)[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t350 + 8), t349);
                        F.ret = t350;
                        goto gm_fret;
                    }
                    gm_b0_1 t351 = GM_LD(gm_b0_1, (((uint8_t *)&t348) + 8));
                }
                {
                    gm_b0_1 t352;
                    gm_tcfa6l7reoosn t353;
                    GM_ST(uint8_t, (uint8_t *)&t353 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t353 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t353 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t353 + 8, 0);
                    GM_ST(uint64_t, (uint8_t *)&t353 + 16, 0);
                    GM_ST(uint64_t, (uint8_t *)&t353 + 24, 0);
                    ((uint8_t *)&t353)[0] = 0;
                    GM_ST(gm_b0_1, ((uint8_t *)&t353 + 8), t352);
                    gm_tcfa6l7reoosn t354 = t353;
                    uint8_t t355 = ((((uint8_t *)&t354))[0] != 0);
                    F.ret = t353;
                    goto gm_fret;
                }
                goto ix335;
ie335:;
ix335:;
            }
            {
                void * t357 = gm_l0;
                gm_t88rcucg1b7uk t358 = gm_l2;
                gm_t57euh612ligr t359;
                gm_f_union_def_h8mvf4t((void *)&t359, t357, GM_CONV(gm_csguankko4lmhd, t358));
                gm_t57euh612ligr t360 = t359;
                if (!((((uint8_t *)&t360))[24] != 0)) goto ie356;
                gm_tcmkompbl09il t361 = GM_LD(gm_tcmkompbl09il, ((uint8_t *)&t360));
                gm_l26 = t361;
                {
                    void * t363 = gm_l0;
                    gm_t88rcucg1b7uk t364 = gm_l2;
                    gm_b16_8 t365 = gm_l3;
                    gm_b16_8 t366 = t365;
                    gm_b16_8 t367 = GM_LD(gm_b16_8, ((uint8_t *)&t366));
                    gm_t9805qtonbsag t368 = gm_f_variant_index_s2p6fs4((void *)NULL, cx, NULL, t363, t364, t367);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_t9805qtonbsag t369 = t368;
                    uint32_t t370;
                    if (((((uint8_t *)&t369))[4] != 0)) {
                        uint32_t t371 = GM_LD(uint32_t, (((uint8_t *)&t369) + 0));
                        t370 = t371;
                    } else {
                        void * t372 = gm_l0;
                        gm_tl874t2atnlh3 t373 = gm_l1;
                        gm_t88rcucg1b7uk t374 = gm_l2;
                        gm_b16_8 t375 = gm_l3;
                        gm_b16_8 t376 = t375;
                        gm_b16_8 t377 = GM_LD(gm_b16_8, ((uint8_t *)&t376));
                        gm_t76123gfet73g t378;
                        gm_f_invalid_tag_1gif354((void *)&t378, (void *)NULL, cx, NULL, t372, t373, t374, t377);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_tcfa6l7reoosn t379;
                        GM_ST(uint8_t, (uint8_t *)&t379 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t379 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t379 + 4, 0);
                        ((uint8_t *)&t379)[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t379 + 8), t378);
                        gm_tcfa6l7reoosn t380 = t379;
                        uint8_t t381 = ((((uint8_t *)&t380))[0] != 0);
                        F.ret = t379;
                        goto gm_fret;
                    }
                    gm_l27 = t370;
                }
                {
                    void * t382 = gm_l4;
                    uint32_t t383 = gm_l27;
                    uint64_t t384 = (uint64_t)t383;
                    gm_f_hash_u64_32bripn(t382, t384);
                }
                {
                    gm_b16_8 t386 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l26) + 8));
                    gm_b16_8 t387 = t386;
                    uint32_t t388 = gm_l27;
                    uint64_t t389 = (uint64_t)t388;
                    uint64_t t390 = gm_f_us_cuv01fp(t389);
                    uint64_t t391 = (uintptr_t)t390;
                    gm_b16_8 t392 = t387;
                    uint64_t t393 = GM_LD(uintptr_t, ((uint8_t *)&t392) + 8);
                    if (t391 >= (uintptr_t)(t393)) gm_trap_index(t391, t393, gm_srcl8pn2pjtmj6k, 474, 26);
                    if (!(((((uint8_t *)GM_LD(void *, ((uint8_t *)&t392)) + t391 * 72) + 16))[4] != 0)) goto ie385;
                    gm_t88rcucg1b7uk t394 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, ((uint8_t *)&t392)) + t391 * 72) + 16));
                    gm_l28 = t394;
                    {
                        void * t396 = gm_l0;
                        gm_t88rcucg1b7uk t397 = gm_l2;
                        uint64_t t398 = gm_f_payload_offset_la0c8ft((void *)NULL, cx, NULL, t396, t397);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint64_t t399 = gm_f_us_cuv01fp(t398);
                        gm_l29 = t399;
                    }
                    {
                        void * t400 = gm_l0;
                        gm_tl874t2atnlh3 t401 = gm_l1;
                        gm_t88rcucg1b7uk t402 = gm_l28;
                        gm_b16_8 t403 = gm_l3;
                        gm_b16_8 t404 = t403;
                        uint64_t t405 = GM_LD(uintptr_t, ((uint8_t *)&t404) + 8);
                        void * t406 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t404)));
                        uint64_t t407 = gm_l29;
                        uint64_t t408 = (uintptr_t)t407;
                        uint64_t t409 = gm_l29;
                        void * t410 = gm_l0;
                        gm_t88rcucg1b7uk t411 = gm_l28;
                        uint64_t t412 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t410, t411);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint64_t t413 = gm_f_us_cuv01fp(t412);
                        uint64_t r414; bool o415 = __builtin_add_overflow(t409, t413, &r414);
                        if (o415) gm_trap("integer overflow in `+`", gm_srcl8pn2pjtmj6k, 476, 53);
                        uint64_t t416 = r414;
                        uint64_t t417 = (uintptr_t)t416;
                        if (t408 > t417 || t417 > (uintptr_t)t405) gm_trap_slice(t408, t417, t405, gm_srcl8pn2pjtmj6k, 476, 46);
                        gm_b16_8 t418;
                        GM_ST(void *, (uint8_t *)&t418, (void *)(((uint8_t *)t406 + t408 * 1)));
                        GM_ST(uintptr_t, (uint8_t *)&t418 + 8, (uintptr_t)(t417 - t408));
                        void * t419 = gm_l4;
                        gm_tcfa6l7reoosn t420;
                        gm_f_hash_val_nr2e99r((void *)&t420, (void *)NULL, cx, NULL, t400, t401, t402, t418, t419);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_tcfa6l7reoosn t421 = t420;
                        if (((((uint8_t *)&t421))[0] != 0)) {
                            gm_t76123gfet73g t422 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t421) + 8));
                            gm_tcfa6l7reoosn t423;
                            GM_ST(uint8_t, (uint8_t *)&t423 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t423 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t423 + 4, 0);
                            ((uint8_t *)&t423)[0] = 1;
                            GM_ST(gm_t76123gfet73g, ((uint8_t *)&t423 + 8), t422);
                            F.ret = t423;
                            goto gm_fret;
                        }
                        gm_b0_1 t424 = GM_LD(gm_b0_1, (((uint8_t *)&t421) + 8));
                    }
                    goto ix385;
ie385:;
ix385:;
                }
                {
                    gm_b0_1 t425;
                    gm_tcfa6l7reoosn t426;
                    GM_ST(uint8_t, (uint8_t *)&t426 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t426 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t426 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t426 + 8, 0);
                    GM_ST(uint64_t, (uint8_t *)&t426 + 16, 0);
                    GM_ST(uint64_t, (uint8_t *)&t426 + 24, 0);
                    ((uint8_t *)&t426)[0] = 0;
                    GM_ST(gm_b0_1, ((uint8_t *)&t426 + 8), t425);
                    gm_tcfa6l7reoosn t427 = t426;
                    uint8_t t428 = ((((uint8_t *)&t427))[0] != 0);
                    F.ret = t426;
                    goto gm_fret;
                }
                goto ix356;
ie356:;
ix356:;
            }
            {
                void * t429 = gm_l0;
                gm_t88rcucg1b7uk t430 = gm_l2;
                void * t431 = gm_f_adt_of_8k5vqud(t429, GM_CONV(gm_csguankko4lmhd, t430));
                void * t432 = t431;
                void * t433;
                if ((GM_LD(void *, ((uint8_t *)&t432)) != 0)) {
                    void * t434 = GM_LD(void *, (((uint8_t *)&t432) + 0));
                    t433 = t434;
                } else {
                    void * t435 = gm_l0;
                    gm_tl874t2atnlh3 t436 = gm_l1;
                    void * t437 = gm_l0;
                    void * t438 = GM_LD(void *, (((uint8_t *)t437) + 8));
                    gm_t88rcucg1b7uk t439 = gm_l2;
                    gm_b16_8 t440 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t438, t439);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tnpifv20irnph t441;
                    GM_ST(gm_b16_8, ((uint8_t *)&t441 + 0), t440);
                    gm_tabb710d9mq2e t442;
                    gm_f_format_h8e7ps1((void *)&t442, (void *)NULL, cx, NULL, t441);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tabb710d9mq2e t443 = t442;
                    if (((((uint8_t *)&t443))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t443) + 8, gm_srcl8pn2pjtmj6k, 480, 56);
                    gm_b16_8 t444 = GM_LD(gm_b16_8, (((uint8_t *)&t443) + 8));
                    gm_b16_8 t445 = t444;
                    gm_b16_8 t446 = GM_LD(gm_b16_8, ((uint8_t *)&t445));
                    gm_t76123gfet73g t447;
                    gm_f_trap_qh84l9d((void *)&t447, (void *)NULL, cx, NULL, t435, t436, t446);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tcfa6l7reoosn t448;
                    GM_ST(uint8_t, (uint8_t *)&t448 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t448 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t448 + 4, 0);
                    ((uint8_t *)&t448)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t448 + 8), t447);
                    gm_tcfa6l7reoosn t449 = t448;
                    uint8_t t450 = ((((uint8_t *)&t449))[0] != 0);
                    F.ret = t448;
                    goto gm_fret;
                }
                gm_l30 = t433;
            }
            {
                void * t452 = gm_l30;
                uint8_t t453 = GM_LD(uint8_t, ((((uint8_t *)t452) + 24) + 0));
                if (t453 != ((uint8_t)0ULL)) goto ie451;
                gm_tpautr4br7h3l t454 = GM_LD(gm_tpautr4br7h3l, ((((uint8_t *)t452) + 24) + 8));
                F.l31 = t454;
                {
                    {
                        gm_l33 = ((uint64_t)0ULL);
                    }
                    {
                        uint64_t t457 = GM_LD(uintptr_t, (((uint8_t *)&F.l31) + 0) + 8);
                        gm_l32 = t457;
                    }
                    {
                        for (;;) {
                            uint64_t t459 = gm_l33;
                            uint64_t t460 = gm_l32;
                            uint8_t t461 = (t459 < t460);
                            if (!t461) goto lb458;
                            {
                                uint64_t t464 = gm_l33;
                                uint64_t t465 = t464;
                                uint64_t t468 = GM_LD(uint64_t, ((uint8_t *)&t465));
                                gm_l34 = t468;
                                {
                                    void * t470 = gm_l0;
                                    gm_t88rcucg1b7uk t471 = gm_l2;
                                    uint64_t t472 = gm_l34;
                                    uint32_t t473 = (uint32_t)t472;
                                    gm_tfbnciphf3261 t474 = gm_f_field_layout_h34salb((void *)NULL, cx, NULL, t470, t471, t473);
                                    if (gm_unw.kind) {
                                        goto gm_ll462;
                                    }
                                    gm_l35 = t474;
                                }
                                {
                                    uint64_t t475 = GM_LD(uint64_t, (((uint8_t *)&gm_l35) + 8));
                                    uint64_t t476 = gm_f_us_cuv01fp(t475);
                                    gm_l36 = t476;
                                }
                                {
                                    void * t477 = gm_l0;
                                    gm_tl874t2atnlh3 t478 = gm_l1;
                                    gm_t88rcucg1b7uk t479 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l35) + 0));
                                    gm_b16_8 t480 = gm_l3;
                                    gm_b16_8 t481 = t480;
                                    uint64_t t482 = GM_LD(uintptr_t, ((uint8_t *)&t481) + 8);
                                    void * t483 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t481)));
                                    uint64_t t484 = gm_l36;
                                    uint64_t t485 = (uintptr_t)t484;
                                    uint64_t t486 = gm_l36;
                                    void * t487 = gm_l0;
                                    gm_t88rcucg1b7uk t488 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l35) + 0));
                                    uint64_t t489 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t487, t488);
                                    if (gm_unw.kind) {
                                        goto gm_ll462;
                                    }
                                    uint64_t t490 = gm_f_us_cuv01fp(t489);
                                    uint64_t r491; bool o492 = __builtin_add_overflow(t486, t490, &r491);
                                    if (o492) gm_trap("integer overflow in `+`", gm_srcl8pn2pjtmj6k, 485, 52);
                                    uint64_t t493 = r491;
                                    uint64_t t494 = (uintptr_t)t493;
                                    if (t485 > t494 || t494 > (uintptr_t)t482) gm_trap_slice(t485, t494, t482, gm_srcl8pn2pjtmj6k, 485, 45);
                                    gm_b16_8 t495;
                                    GM_ST(void *, (uint8_t *)&t495, (void *)(((uint8_t *)t483 + t485 * 1)));
                                    GM_ST(uintptr_t, (uint8_t *)&t495 + 8, (uintptr_t)(t494 - t485));
                                    void * t496 = gm_l4;
                                    gm_tcfa6l7reoosn t497;
                                    gm_f_hash_val_nr2e99r((void *)&t497, (void *)NULL, cx, NULL, t477, t478, t479, t495, t496);
                                    if (gm_unw.kind) {
                                        goto gm_ll462;
                                    }
                                    gm_tcfa6l7reoosn t498 = t497;
                                    if (((((uint8_t *)&t498))[0] != 0)) {
                                        gm_t76123gfet73g t499 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t498) + 8));
                                        gm_tcfa6l7reoosn t500;
                                        GM_ST(uint8_t, (uint8_t *)&t500 + 1, 0);
                                        GM_ST(uint16_t, (uint8_t *)&t500 + 2, 0);
                                        GM_ST(uint32_t, (uint8_t *)&t500 + 4, 0);
                                        ((uint8_t *)&t500)[0] = 1;
                                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t500 + 8), t499);
                                        F.ret = t500;
                                        goto gm_fret;
                                    }
                                    gm_b0_1 t501 = GM_LD(gm_b0_1, (((uint8_t *)&t498) + 8));
                                }
                                goto me466;
mn467:;
                                gm_trap("no match arm matched the scrutinee", gm_srcl8pn2pjtmj6k, 482, 13);
me466:;
                            }
lc458:;
                            {
                                uint64_t t503 = gm_l33;
                                uint64_t r504; bool o505 = __builtin_add_overflow(t503, ((uint64_t)1ULL), &r504);
                                uint64_t t506 = r504;
                                gm_l33 = t506;
                            }
                        }
gm_ll462:;
                        if (gm_unw.frame == &F.h && gm_unw.label == 3) {
                            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb458; }
                            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc458; }
                        }
                        goto gm_lad;
lb458:;
                    }
                }
                goto ix451;
ie451:;
                {
                    void * t508 = gm_l0;
                    gm_tl874t2atnlh3 t509 = gm_l1;
                    void * t510 = gm_l0;
                    void * t511 = GM_LD(void *, (((uint8_t *)t510) + 8));
                    gm_t88rcucg1b7uk t512 = gm_l2;
                    gm_b16_8 t513 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t511, t512);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tnpifv20irnph t514;
                    GM_ST(gm_b16_8, ((uint8_t *)&t514 + 0), t513);
                    gm_tabb710d9mq2e t515;
                    gm_f_format_8k5eh95((void *)&t515, (void *)NULL, cx, NULL, t514);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tabb710d9mq2e t516 = t515;
                    if (((((uint8_t *)&t516))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t516) + 8, gm_srcl8pn2pjtmj6k, 481, 62);
                    gm_b16_8 t517 = GM_LD(gm_b16_8, (((uint8_t *)&t516) + 8));
                    gm_b16_8 t518 = t517;
                    gm_b16_8 t519 = GM_LD(gm_b16_8, ((uint8_t *)&t518));
                    gm_t76123gfet73g t520;
                    gm_f_trap_qh84l9d((void *)&t520, (void *)NULL, cx, NULL, t508, t509, t519);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tcfa6l7reoosn t521;
                    GM_ST(uint8_t, (uint8_t *)&t521 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t521 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t521 + 4, 0);
                    ((uint8_t *)&t521)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t521 + 8), t520);
                    gm_tcfa6l7reoosn t522 = t521;
                    uint8_t t523 = ((((uint8_t *)&t522))[0] != 0);
                    F.ret = t521;
                    goto gm_fret;
                }
ix451:;
            }
            goto me7;
mn332:;
ma19:;
            void * t525 = gm_l0;
            gm_tl874t2atnlh3 t526 = gm_l1;
            void * t527 = gm_l0;
            void * t528 = GM_LD(void *, (((uint8_t *)t527) + 8));
            gm_t88rcucg1b7uk t529 = gm_l2;
            gm_b16_8 t530 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t528, t529);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tnpifv20irnph t531;
            GM_ST(gm_b16_8, ((uint8_t *)&t531 + 0), t530);
            gm_tabb710d9mq2e t532;
            gm_f_format_8k5eh95((void *)&t532, (void *)NULL, cx, NULL, t531);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t533 = t532;
            if (((((uint8_t *)&t533))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t533) + 8, gm_srcl8pn2pjtmj6k, 488, 35);
            gm_b16_8 t534 = GM_LD(gm_b16_8, (((uint8_t *)&t533) + 8));
            gm_b16_8 t535 = t534;
            gm_b16_8 t536 = GM_LD(gm_b16_8, ((uint8_t *)&t535));
            gm_t76123gfet73g t537;
            gm_f_trap_qh84l9d((void *)&t537, (void *)NULL, cx, NULL, t525, t526, t536);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tcfa6l7reoosn t538;
            GM_ST(uint8_t, (uint8_t *)&t538 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t538 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t538 + 4, 0);
            ((uint8_t *)&t538)[0] = 1;
            GM_ST(gm_t76123gfet73g, ((uint8_t *)&t538 + 8), t537);
            gm_tcfa6l7reoosn t539 = t538;
            uint8_t t540 = ((((uint8_t *)&t539))[0] != 0);
            F.ret = t538;
            goto gm_fret;
            goto me7;
mn524:;
ma20:;
            gm_trap("no match arm matched the scrutinee", gm_srcl8pn2pjtmj6k, 426, 5);
me7:;
        }
    }
    gm_b0_1 t541;
    gm_tcfa6l7reoosn t542;
    GM_ST(uint8_t, (uint8_t *)&t542 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t542 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t542 + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&t542 + 8, 0);
    GM_ST(uint64_t, (uint8_t *)&t542 + 16, 0);
    GM_ST(uint64_t, (uint8_t *)&t542 + 24, 0);
    ((uint8_t *)&t542)[0] = 0;
    GM_ST(gm_b0_1, ((uint8_t *)&t542 + 8), t541);
    gm_tcfa6l7reoosn t543 = t542;
    F.ret = t543;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_int_text_vrjnuu2(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2) {
    struct gm_fr_int_text_vrjnuu2 F;
    void * gm_l0; /* m */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_b16_8 gm_l2; /* b */
    unsigned __int128 gm_l3; /* v */
    gm_ttovhfee8mf28 gm_l4; /* info */
    GM_STACK_CHECK(&F, 535u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        gm_t88rcucg1b7uk t3 = gm_l1;
        gm_b16_8 t4 = gm_l2;
        unsigned __int128 t5 = gm_f_get_int_pg7p4gc(t2, GM_CONV(gm_csguankko4lmhd, t3), t4);
        gm_l3 = t5;
    }
    {
        void * t6 = gm_l0;
        gm_t88rcucg1b7uk t7 = gm_l1;
        gm_tg5lql6itufk7 t8 = gm_f_int_info_ulcevdv(t6, GM_CONV(gm_csguankko4lmhd, t7));
        gm_tg5lql6itufk7 t9 = t8;
        gm_ttovhfee8mf28 t10;
        if (((((uint8_t *)&t9))[8] != 0)) {
            gm_ttovhfee8mf28 t11 = GM_LD(gm_ttovhfee8mf28, (((uint8_t *)&t9) + 0));
            t10 = t11;
        } else {
            gm_b16_8 t12;
            GM_ST(void *, (uint8_t *)&t12, (void *)(gm_sr4qbrk6tlgaa));
            GM_ST(uintptr_t, (uint8_t *)&t12 + 8, (uintptr_t)(0));
            uint8_t t13 = 0;
            F.ret = t12;
            goto gm_fret;
        }
        gm_l4 = t10;
    }
    {
        gm_b16_8 t14;
        uint8_t t16 = GM_LD(uint8_t, (((uint8_t *)&gm_l4) + 0));
        if (!t16) goto ie15;
        unsigned __int128 t18 = gm_l3;
        unsigned __int128 t19 = t18;
        __int128 t20;
        memset(&t20, 0, sizeof t20); memcpy(&t20, ((uint8_t *)&t19), sizeof t20 < 16 ? sizeof t20 : 16);
        gm_tpl3q6e8s9qo1 t21;
        GM_ST(__int128, ((uint8_t *)&t21 + 0), t20);
        gm_tabb710d9mq2e t22;
        gm_f_format_9qvncot((void *)&t22, (void *)NULL, cx, NULL, t21);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t23 = t22;
        if (((((uint8_t *)&t23))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t23) + 8, gm_srcl8pn2pjtmj6k, 185, 29);
        gm_b16_8 t24 = GM_LD(gm_b16_8, (((uint8_t *)&t23) + 8));
        gm_b16_8 t25 = t24;
        gm_b16_8 t26 = t25;
        gm_b16_8 t27 = GM_LD(gm_b16_8, ((uint8_t *)&t26));
        t14 = t27;
        goto ix15;
ie15:;
        unsigned __int128 t29 = gm_l3;
        gm_tsn2omssg6cpf t30;
        GM_ST(unsigned __int128, ((uint8_t *)&t30 + 0), t29);
        gm_tabb710d9mq2e t31;
        gm_f_format_ra7pont((void *)&t31, (void *)NULL, cx, NULL, t30);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t32 = t31;
        if (((((uint8_t *)&t32))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t32) + 8, gm_srcl8pn2pjtmj6k, 185, 70);
        gm_b16_8 t33 = GM_LD(gm_b16_8, (((uint8_t *)&t32) + 8));
        gm_b16_8 t34 = t33;
        gm_b16_8 t35 = t34;
        gm_b16_8 t36 = GM_LD(gm_b16_8, ((uint8_t *)&t35));
        t14 = t36;
ix15:;
        uint8_t t37 = 0;
        F.ret = t14;
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
inline __attribute__((aligned(4))) uint64_t gm_f_ptr_align_lavuopj(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_ptr_align_lavuopj F;
    void * gm_l0; /* m */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_t88rcucg1b7uk gm_l2; /* p */
    gm_t88rcucg1b7uk gm_l3; /* p */
    gm_t88rcucg1b7uk gm_l4; /* inner */
    GM_STACK_CHECK(&F, 268u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        gm_t88rcucg1b7uk t3 = gm_l1;
        gm_t53at80pfdus3 t4;
        gm_f_kind_htv6093((void *)&t4, t2, GM_CONV(gm_csguankko4lmhd, t3));
        gm_t53at80pfdus3 t5 = t4;
        uint8_t t11 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        switch (t11) { case ((uint8_t)0ULL): goto ma9; case ((uint8_t)1ULL): goto ma9; case ((uint8_t)2ULL): goto ma9; case ((uint8_t)3ULL): goto ma9; case ((uint8_t)4ULL): goto ma9; case ((uint8_t)5ULL): goto ma9; case ((uint8_t)6ULL): goto ma9; case ((uint8_t)7ULL): goto ma9; case ((uint8_t)8ULL): goto ma7; case ((uint8_t)9ULL): goto ma7; case ((uint8_t)10ULL): goto ma9; case ((uint8_t)11ULL): goto ma9; case ((uint8_t)12ULL): goto ma9; case ((uint8_t)13ULL): goto ma8; case ((uint8_t)14ULL): goto ma9; case ((uint8_t)15ULL): goto ma9; case ((uint8_t)16ULL): goto ma9; case ((uint8_t)17ULL): goto ma9; case ((uint8_t)18ULL): goto ma9; case ((uint8_t)19ULL): goto ma9; case ((uint8_t)20ULL): goto ma9; case ((uint8_t)21ULL): goto ma9; case ((uint8_t)22ULL): goto ma9; case ((uint8_t)23ULL): goto ma9; case ((uint8_t)24ULL): goto ma9; case ((uint8_t)25ULL): goto ma9; case ((uint8_t)26ULL): goto ma9; default: goto ma9; }
ma7:;
        uint8_t t15 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t15 != ((uint8_t)8ULL)) goto pa14;
        gm_t88rcucg1b7uk t16 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t5) + 8) + 4));
        gm_l2 = t16;
        goto po13;
pa14:;
        uint8_t t18 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t18 != ((uint8_t)9ULL)) goto pa17;
        gm_t88rcucg1b7uk t19 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t5) + 8) + 4));
        gm_l2 = t19;
        goto po13;
pa17:;
        goto mn12;
po13:;
        uint64_t t20;
        void * t22 = gm_l0;
        gm_t88rcucg1b7uk t23 = gm_l2;
        uint64_t t24 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t22, t23);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t25 = ((t24 == ((uint64_t)0ULL)));
        if (!t25) goto ie21;
        uint64_t t27 = ((uint64_t)1ULL);
        t20 = t27;
        goto ix21;
ie21:;
        void * t29 = gm_l0;
        gm_t88rcucg1b7uk t30 = gm_l2;
        uint64_t t31 = gm_f_align_15fjgq1((void *)NULL, cx, NULL, t29, t30);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint64_t t32 = t31;
        t20 = t32;
ix21:;
        uint8_t t33 = 0;
        F.ret = t20;
        goto gm_fret;
        goto me6;
mn12:;
ma8:;
        uint8_t t35 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t35 != ((uint8_t)13ULL)) goto mn34;
        gm_t88rcucg1b7uk t36 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t5) + 8));
        gm_l4 = t36;
        void * t37 = gm_l0;
        gm_t88rcucg1b7uk t38 = gm_l4;
        uint64_t t39 = gm_f_ptr_align_lavuopj((void *)NULL, cx, NULL, t37, t38);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t40 = 0;
        F.ret = t39;
        goto gm_fret;
        goto me6;
mn34:;
ma9:;
        uint8_t t42 = 0;
        F.ret = ((uint64_t)1ULL);
        goto gm_fret;
        goto me6;
mn41:;
ma10:;
        gm_trap("no match arm matched the scrutinee", gm_srcl8pn2pjtmj6k, 174, 5);
me6:;
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
