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
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { void * a[2]; } gm_t5fru0lp11n7t;
typedef struct { uint64_t f0; uint64_t f1; uint64_t f2; uint64_t f3; } gm_t66j1im9ip128;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint32_t f0; uint8_t f1; uint8_t pe[3]; } gm_t9805qtonbsag;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint8_t f0; uint8_t p1[7]; void * f1; void * f2; } gm_ta612fil812pm;
typedef struct { void * f0; void * f1; void * f2; uint8_t f3; uint8_t pe[7]; } gm_tao63pk3r18v5;
typedef struct { void * f0; void * f1; void * f2; } gm_tbbqefuhv2r8h;
typedef struct { void * f0; uint32_t f1; uint8_t pe[4]; } gm_tbl8il92l97ut;
typedef struct { uint32_t f0; uint32_t f1; } gm_tcc1rq4n1avb2;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { void * f0; void * f1; void * f2; void * f3; } gm_tgqj9p927bnhp;
typedef struct { uint8_t f0; uint8_t p1[7]; void * f1; } gm_tgtub0v94d5u5;
typedef struct { uint8_t f0; uint8_t p1[3]; union { uint32_t v0; uint32_t v1; } f1; } gm_tj2m9dbh1ekv6;
typedef struct { uint8_t f0; uint8_t p1[1]; uint16_t f1; } gm_tjcj4j2gbh6c8;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { void * f0; void * f1; uint8_t f2; uint8_t pe[7]; } gm_tlcl4u558l6p5;
typedef struct { void * f0; void * f1; uint32_t f2; uint32_t f3; } gm_tmm67r2c94d5j;
typedef struct { uint8_t f0; uint8_t p1[3]; union { uint32_t v0; } f1; } gm_tmn7n0n9raou5;
typedef struct { uint8_t f0; uint8_t f1; uint8_t f2; } gm_toh0ootodhjue;
typedef struct { uint8_t f0; uint8_t f1; } gm_tqs5p4n2tq19v;
typedef struct { void * f0; uint8_t f1; uint8_t pe[7]; } gm_tsplfr33dsvji;
typedef struct { uint32_t f0; uint8_t p1[4]; void * f1; } gm_ttggq7ng4kp3p;
_Static_assert(sizeof(gm_csguankko4lmhd) == 4 && _Alignof(gm_csguankko4lmhd) == 4 && offsetof(gm_csguankko4lmhd, f0) == 0, "layout of `Mutex`");
typedef struct { void * f0; gm_t9805qtonbsag f1; uint32_t f2; uint8_t pe[4]; } gm_t083kd7b1kkag;
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_t08olq97rscn3;
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; gm_t9qkh0rsooc8d f2; uint8_t p3[3]; gm_tf5srh4mp4b3i f3; } gm_t0mglksrkfa5e;
typedef struct { gm_t88rcucg1b7uk f0; gm_tl874t2atnlh3 f1; } gm_t2i8ne5ibd7v4;
typedef struct { gm_b16_8 f0; uint64_t f1; } gm_t3877pk7lv748;
typedef struct { void * f0; gm_t9805qtonbsag f1; void * f2; } gm_t3dcoc7077t1q;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; uint8_t f2; uint8_t pe[3]; } gm_t3rcjibl8kkg4;
_Static_assert(sizeof(gm_t5fru0lp11n7t) == 16 && _Alignof(gm_t5fru0lp11n7t) == 8 && offsetof(gm_t5fru0lp11n7t, a) == 0, "layout of `[2]?^Expr`");
_Static_assert(sizeof(gm_t66j1im9ip128) == 32 && _Alignof(gm_t66j1im9ip128) == 8 && offsetof(gm_t66j1im9ip128, f0) == 0 && offsetof(gm_t66j1im9ip128, f1) == 8 && offsetof(gm_t66j1im9ip128, f2) == 16 && offsetof(gm_t66j1im9ip128, f3) == 24, "layout of `(types: usize, values: usize, fns: usize, consts: usize)`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; void * f2; void * f3; } gm_t710lsrq1m8gk;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_t79rbdv4rafe5;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; void * f1; } gm_t8ghgl2jj0qha;
typedef struct { gm_tmn7n0n9raou5 f0; void * f1; } gm_t8jsc4n9vp2ib;
typedef struct { gm_t88rcucg1b7uk f0; gm_tl874t2atnlh3 f1; gm_b16_8 f2; uint8_t f3; uint8_t f4; uint8_t pe[6]; } gm_t91vm1g0k2rpm;
_Static_assert(sizeof(gm_t9805qtonbsag) == 8 && _Alignof(gm_t9805qtonbsag) == 4 && offsetof(gm_t9805qtonbsag, f0) == 0 && offsetof(gm_t9805qtonbsag, f1) == 4, "layout of `?u32`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
_Static_assert(sizeof(gm_ta612fil812pm) == 24 && _Alignof(gm_ta612fil812pm) == 8 && offsetof(gm_ta612fil812pm, f0) == 0 && offsetof(gm_ta612fil812pm, f1) == 8 && offsetof(gm_ta612fil812pm, f2) == 16, "layout of `(is_and: bool, lhs: ^Expr, rhs: ^Expr)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
_Static_assert(sizeof(gm_tao63pk3r18v5) == 32 && _Alignof(gm_tao63pk3r18v5) == 8 && offsetof(gm_tao63pk3r18v5, f0) == 0 && offsetof(gm_tao63pk3r18v5, f1) == 8 && offsetof(gm_tao63pk3r18v5, f2) == 16 && offsetof(gm_tao63pk3r18v5, f3) == 24, "layout of `SliceExpr`");
typedef struct { gm_b16_8 f0; uint32_t f1; uint32_t f2; gm_b16_8 f3; } gm_tb40qn7hk0tl9;
_Static_assert(sizeof(gm_tbbqefuhv2r8h) == 24 && _Alignof(gm_tbbqefuhv2r8h) == 8 && offsetof(gm_tbbqefuhv2r8h, f0) == 0 && offsetof(gm_tbbqefuhv2r8h, f1) == 8 && offsetof(gm_tbbqefuhv2r8h, f2) == 16, "layout of `Arm`");
_Static_assert(sizeof(gm_tbl8il92l97ut) == 16 && _Alignof(gm_tbl8il92l97ut) == 8 && offsetof(gm_tbl8il92l97ut, f0) == 0 && offsetof(gm_tbl8il92l97ut, f1) == 8, "layout of `SpecCall`");
typedef struct { uint8_t f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; } gm_tbngattl1fo51;
_Static_assert(sizeof(gm_tcc1rq4n1avb2) == 8 && _Alignof(gm_tcc1rq4n1avb2) == 4 && offsetof(gm_tcc1rq4n1avb2, f0) == 0 && offsetof(gm_tcc1rq4n1avb2, f1) == 4, "layout of `LineCol`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tcmkompbl09il;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; gm_t88rcucg1b7uk f2; } gm_tcmtrmhj7ak4u;
typedef struct { gm_t66j1im9ip128 f0; uint8_t f1; uint8_t pe[7]; } gm_td8hfgklo8dil;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_tcc1rq4n1avb2 v0; } f1; } gm_te9hrk0u35rev;
typedef struct { uint8_t f0; union { gm_toh0ootodhjue v0; } f1; } gm_tesaltf0mhcki;
typedef struct { uint64_t f0; gm_b16_8 f1; } gm_tf1bjg81u0glv;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; } gm_tfeab279c16rr;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; } gm_tgglga8ro3isc;
_Static_assert(sizeof(gm_tgqj9p927bnhp) == 32 && _Alignof(gm_tgqj9p927bnhp) == 8 && offsetof(gm_tgqj9p927bnhp, f0) == 0 && offsetof(gm_tgqj9p927bnhp, f1) == 8 && offsetof(gm_tgqj9p927bnhp, f2) == 16 && offsetof(gm_tgqj9p927bnhp, f3) == 24, "layout of `Captured`");
_Static_assert(sizeof(gm_tgtub0v94d5u5) == 16 && _Alignof(gm_tgtub0v94d5u5) == 8 && offsetof(gm_tgtub0v94d5u5, f0) == 0 && offsetof(gm_tgtub0v94d5u5, f1) == 8, "layout of `(is_const: bool, inner: ^Expr)`");
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; gm_tl874t2atnlh3 f2; } gm_thsh8t5u55gps;
typedef struct { gm_b16_8 f0; gm_t9qkh0rsooc8d f1; uint8_t pe[7]; } gm_tibupt4dmcqai;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
_Static_assert(sizeof(gm_tj2m9dbh1ekv6) == 8 && _Alignof(gm_tj2m9dbh1ekv6) == 4 && offsetof(gm_tj2m9dbh1ekv6, f0) == 0 && offsetof(gm_tj2m9dbh1ekv6, f1) == 4, "layout of `IntSuffix`");
_Static_assert(sizeof(gm_tjcj4j2gbh6c8) == 4 && _Alignof(gm_tjcj4j2gbh6c8) == 2 && offsetof(gm_tjcj4j2gbh6c8, f0) == 0 && offsetof(gm_tjcj4j2gbh6c8, f1) == 2, "layout of `(signed: bool, bits: u16)`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_tf5srh4mp4b3i f1; uint8_t f2; uint8_t pe[7]; } gm_tjpqvfd72o62u;
typedef struct { void * f0; gm_b16_8 f1; } gm_tkaapcii86udk;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t f1; uint8_t pe[3]; } gm_tkn42u7na3t81;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_tlcl4u558l6p5) == 24 && _Alignof(gm_tlcl4u558l6p5) == 8 && offsetof(gm_tlcl4u558l6p5, f0) == 0 && offsetof(gm_tlcl4u558l6p5, f1) == 8 && offsetof(gm_tlcl4u558l6p5, f2) == 16, "layout of `(lo: ^Expr, hi: ^Expr, inclusive: bool)`");
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; uint8_t pe[4]; } gm_tlcm4lvu3g1a8;
typedef struct { void * f0; gm_b16_8 f1; uint8_t f2; uint8_t pe[7]; } gm_tlljg61a2ctgq;
typedef struct { uint8_t f0; uint8_t p1[7]; union { void * v0; gm_t3mf58gv9kf3n v1; } f1; } gm_tlr9dp305htm7;
_Static_assert(sizeof(gm_tmm67r2c94d5j) == 24 && _Alignof(gm_tmm67r2c94d5j) == 8 && offsetof(gm_tmm67r2c94d5j, f0) == 0 && offsetof(gm_tmm67r2c94d5j, f1) == 8 && offsetof(gm_tmm67r2c94d5j, f2) == 16 && offsetof(gm_tmm67r2c94d5j, f3) == 20, "layout of `Printer`");
_Static_assert(sizeof(gm_tmn7n0n9raou5) == 8 && _Alignof(gm_tmn7n0n9raou5) == 4 && offsetof(gm_tmn7n0n9raou5, f0) == 0 && offsetof(gm_tmn7n0n9raou5, f1) == 4, "layout of `Coercion`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t88rcucg1b7uk v0; } f1; } gm_tnnjeerolos0a;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
typedef struct { gm_t88rcucg1b7uk a[1]; } gm_tnqk2qt7j42o9;
_Static_assert(sizeof(gm_toh0ootodhjue) == 3 && _Alignof(gm_toh0ootodhjue) == 1 && offsetof(gm_toh0ootodhjue, f0) == 0 && offsetof(gm_toh0ootodhjue, f1) == 1 && offsetof(gm_toh0ootodhjue, f2) == 2, "layout of `Restr`");
typedef struct { gm_b16_8 f0; uint32_t f1; uint8_t pe[4]; } gm_tommf4j9upomc;
typedef struct { gm_b16_8 f0; void * f1; } gm_tpe1hv3fekn05;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; void * v1; } f1; } gm_tq7s98ihonkju;
_Static_assert(sizeof(gm_tqs5p4n2tq19v) == 2 && _Alignof(gm_tqs5p4n2tq19v) == 1 && offsetof(gm_tqs5p4n2tq19v, f0) == 0 && offsetof(gm_tqs5p4n2tq19v, f1) == 1, "layout of `(u8, u8)`");
typedef struct { gm_t88rcucg1b7uk a[2]; } gm_tqsk8dt6ib3qq;
typedef struct { gm_b16_8 f0; void * f1; void * f2; } gm_trl2khmuphncd;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_t9qkh0rsooc8d v3; } f1; } gm_ts6oqppjh0ba3;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_ts78ca2amthgu;
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t f2; uint8_t p3[3]; gm_tl874t2atnlh3 f3; uint8_t pe[4]; } gm_tsoie1ppgdvcg;
_Static_assert(sizeof(gm_tsplfr33dsvji) == 16 && _Alignof(gm_tsplfr33dsvji) == 8 && offsetof(gm_tsplfr33dsvji, f0) == 0 && offsetof(gm_tsplfr33dsvji, f1) == 8, "layout of `(expr: ^Expr, is_result: bool)`");
typedef struct { gm_tgqj9p927bnhp f0; uint8_t f1; uint8_t pe[7]; } gm_tsprp1cqsr44h;
_Static_assert(sizeof(gm_ttggq7ng4kp3p) == 16 && _Alignof(gm_ttggq7ng4kp3p) == 8 && offsetof(gm_ttggq7ng4kp3p, f0) == 0 && offsetof(gm_ttggq7ng4kp3p, f1) == 8, "layout of `(index: u32, payload: ?^Value)`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; gm_b16_8 f4; } gm_tud7oovfvnf43;
typedef struct { void * f0; uint32_t f1; gm_tl874t2atnlh3 f2; } gm_tupk62cr6lbdu;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_tcc1rq4n1avb2 v3; uint32_t v4; uint32_t v5; } f1; } gm_tvdg94bk7aa22;
_Static_assert(sizeof(gm_t083kd7b1kkag) == 24 && _Alignof(gm_t083kd7b1kkag) == 8 && offsetof(gm_t083kd7b1kkag, f0) == 0 && offsetof(gm_t083kd7b1kkag, f1) == 8 && offsetof(gm_t083kd7b1kkag, f2) == 16, "layout of `TryExpr`");
_Static_assert(sizeof(gm_t08olq97rscn3) == 32 && _Alignof(gm_t08olq97rscn3) == 8 && offsetof(gm_t08olq97rscn3, f0) == 0 && offsetof(gm_t08olq97rscn3, f1) == 16, "layout of `File`");
_Static_assert(sizeof(gm_t0mglksrkfa5e) == 40 && _Alignof(gm_t0mglksrkfa5e) == 8 && offsetof(gm_t0mglksrkfa5e, f0) == 0 && offsetof(gm_t0mglksrkfa5e, f1) == 16 && offsetof(gm_t0mglksrkfa5e, f2) == 20 && offsetof(gm_t0mglksrkfa5e, f3) == 24, "layout of `FieldDef`");
typedef struct { gm_t08olq97rscn3 f0; uint8_t f1; uint8_t pe[7]; } gm_t1s75i08qtptd;
_Static_assert(sizeof(gm_t2i8ne5ibd7v4) == 16 && _Alignof(gm_t2i8ne5ibd7v4) == 4 && offsetof(gm_t2i8ne5ibd7v4, f0) == 0 && offsetof(gm_t2i8ne5ibd7v4, f1) == 4, "layout of `(ty: Ty, span: Span)`");
_Static_assert(sizeof(gm_t3877pk7lv748) == 24 && _Alignof(gm_t3877pk7lv748) == 8 && offsetof(gm_t3877pk7lv748, f0) == 0 && offsetof(gm_t3877pk7lv748, f1) == 16, "layout of `WasmReader`");
_Static_assert(sizeof(gm_t3dcoc7077t1q) == 24 && _Alignof(gm_t3dcoc7077t1q) == 8 && offsetof(gm_t3dcoc7077t1q, f0) == 0 && offsetof(gm_t3dcoc7077t1q, f1) == 8 && offsetof(gm_t3dcoc7077t1q, f2) == 16, "layout of `ElseExpr`");
_Static_assert(sizeof(gm_t3rcjibl8kkg4) == 12 && _Alignof(gm_t3rcjibl8kkg4) == 4 && offsetof(gm_t3rcjibl8kkg4, f0) == 0 && offsetof(gm_t3rcjibl8kkg4, f1) == 4 && offsetof(gm_t3rcjibl8kkg4, f2) == 8, "layout of `(a: Ty, b: Ty, negated: bool)`");
typedef struct { gm_tiilmfd16v3fc f0; uint32_t f1; uint8_t pe[4]; } gm_t419mo45g7j0a;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tcmkompbl09il v0; gm_tiilmfd16v3fc v1; gm_b16_8 v2; } f1; } gm_t428ue2u3hf5g;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t p3[4]; gm_trqfvmjtt4sij f3; gm_trqfvmjtt4sij f4; gm_trqfvmjtt4sij f5; gm_th2lqbo77pvsc f6; } gm_t4gut2c9fj1kt;
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_t710lsrq1m8gk) == 40 && _Alignof(gm_t710lsrq1m8gk) == 8 && offsetof(gm_t710lsrq1m8gk, f0) == 0 && offsetof(gm_t710lsrq1m8gk, f1) == 8 && offsetof(gm_t710lsrq1m8gk, f2) == 24 && offsetof(gm_t710lsrq1m8gk, f3) == 32, "layout of `WithExpr`");
_Static_assert(sizeof(gm_t79rbdv4rafe5) == 40 && _Alignof(gm_t79rbdv4rafe5) == 8 && offsetof(gm_t79rbdv4rafe5, f0) == 0 && offsetof(gm_t79rbdv4rafe5, f1) == 8 && offsetof(gm_t79rbdv4rafe5, f2) == 24, "layout of `Block`");
typedef struct { gm_tjpqvfd72o62u f0; uint8_t f1; uint8_t pe[7]; } gm_t8cc1mtbrhlpt;
_Static_assert(sizeof(gm_t8ghgl2jj0qha) == 16 && _Alignof(gm_t8ghgl2jj0qha) == 8 && offsetof(gm_t8ghgl2jj0qha, f0) == 0 && offsetof(gm_t8ghgl2jj0qha, f1) == 8, "layout of `(op: UnOp, operand: ^Expr)`");
_Static_assert(sizeof(gm_t8jsc4n9vp2ib) == 16 && _Alignof(gm_t8jsc4n9vp2ib) == 8 && offsetof(gm_t8jsc4n9vp2ib, f0) == 0 && offsetof(gm_t8jsc4n9vp2ib, f1) == 8, "layout of `(kind: Coercion, expr: ^Expr)`");
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; gm_tnnjeerolos0a f2; uint8_t pe[4]; } gm_t8nckeqfenl8a;
typedef struct { uint64_t f0; gm_t0mglksrkfa5e f1; } gm_t8ta482bl6be0;
_Static_assert(sizeof(gm_t91vm1g0k2rpm) == 40 && _Alignof(gm_t91vm1g0k2rpm) == 8 && offsetof(gm_t91vm1g0k2rpm, f0) == 0 && offsetof(gm_t91vm1g0k2rpm, f1) == 4 && offsetof(gm_t91vm1g0k2rpm, f2) == 16 && offsetof(gm_t91vm1g0k2rpm, f3) == 32 && offsetof(gm_t91vm1g0k2rpm, f4) == 33, "layout of `NumericObligation`");
typedef struct { gm_ts6oqppjh0ba3 f0; gm_ts6oqppjh0ba3 f1; } gm_t9ldr1q5hn9fq;
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
typedef struct { void * f0; gm_th2lqbo77pvsc f1; } gm_tagc77imt2chm;
typedef struct { gm_tesaltf0mhcki f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tandvbt63uk7n;
_Static_assert(sizeof(gm_tb40qn7hk0tl9) == 40 && _Alignof(gm_tb40qn7hk0tl9) == 8 && offsetof(gm_tb40qn7hk0tl9, f0) == 0 && offsetof(gm_tb40qn7hk0tl9, f1) == 16 && offsetof(gm_tb40qn7hk0tl9, f2) == 20 && offsetof(gm_tb40qn7hk0tl9, f3) == 24, "layout of `SourceLocation`");
typedef struct { gm_tlcm4lvu3g1a8 f0; uint8_t f1; uint8_t pe[7]; } gm_tb7u19loo3j87;
typedef struct { uint8_t f0; uint8_t p1[7]; gm_trqfvmjtt4sij f1; } gm_tbci5ospk5nnn;
_Static_assert(sizeof(gm_tbngattl1fo51) == 8 && _Alignof(gm_tbngattl1fo51) == 4 && offsetof(gm_tbngattl1fo51, f0) == 0 && offsetof(gm_tbngattl1fo51, f1) == 4, "layout of `(is_const: bool, pointee: Ty)`");
_Static_assert(sizeof(gm_tcmkompbl09il) == 24 && _Alignof(gm_tcmkompbl09il) == 8 && offsetof(gm_tcmkompbl09il, f0) == 0 && offsetof(gm_tcmkompbl09il, f1) == 8, "layout of `(ty: Ty, bytes: string)`");
_Static_assert(sizeof(gm_tcmtrmhj7ak4u) == 12 && _Alignof(gm_tcmtrmhj7ak4u) == 4 && offsetof(gm_tcmtrmhj7ak4u, f0) == 0 && offsetof(gm_tcmtrmhj7ak4u, f1) == 4 && offsetof(gm_tcmtrmhj7ak4u, f2) == 8, "layout of `(op: LenOp, lhs: Ty, rhs: Ty)`");
typedef struct { gm_t08olq97rscn3 f0; gm_t79rbdv4rafe5 f1; } gm_tcuumjkg5eni6;
_Static_assert(sizeof(gm_td8hfgklo8dil) == 40 && _Alignof(gm_td8hfgklo8dil) == 8 && offsetof(gm_td8hfgklo8dil, f0) == 0 && offsetof(gm_td8hfgklo8dil, f1) == 32, "layout of `?(types: usize, values: usize, fns: usize, consts: usize)`");
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t p2[4]; gm_tnpifv20irnph f2; uint8_t f3; uint8_t pe[7]; } gm_tddtjfggdat0d;
_Static_assert(sizeof(gm_te9hrk0u35rev) == 12 && _Alignof(gm_te9hrk0u35rev) == 4 && offsetof(gm_te9hrk0u35rev, f0) == 0 && offsetof(gm_te9hrk0u35rev, f1) == 4, "layout of `Target`");
typedef struct { gm_te9hrk0u35rev f0; uint8_t p1[4]; void * f1; } gm_teo9d4mncjfdu;
_Static_assert(sizeof(gm_tesaltf0mhcki) == 4 && _Alignof(gm_tesaltf0mhcki) == 1 && offsetof(gm_tesaltf0mhcki, f0) == 0 && offsetof(gm_tesaltf0mhcki, f1) == 1, "layout of `SoaOp`");
typedef struct { gm_t08olq97rscn3 f0; gm_th2lqbo77pvsc f1; } gm_tet9guhev5r4u;
_Static_assert(sizeof(gm_tf1bjg81u0glv) == 24 && _Alignof(gm_tf1bjg81u0glv) == 8 && offsetof(gm_tf1bjg81u0glv, f0) == 0 && offsetof(gm_tf1bjg81u0glv, f1) == 8, "layout of `(i: usize, x: string)`");
_Static_assert(sizeof(gm_tfeab279c16rr) == 48 && _Alignof(gm_tfeab279c16rr) == 8 && offsetof(gm_tfeab279c16rr, f0) == 0 && offsetof(gm_tfeab279c16rr, f1) == 16 && offsetof(gm_tfeab279c16rr, f2) == 32, "layout of `Toolchain`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_tgglga8ro3isc) == 8 && _Alignof(gm_tgglga8ro3isc) == 4 && offsetof(gm_tgglga8ro3isc, f0) == 0 && offsetof(gm_tgglga8ro3isc, f1) == 4, "layout of `(err: Ty, ok: Ty)`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
typedef struct { gm_tbngattl1fo51 f0; uint8_t f1; uint8_t pe[3]; } gm_thetuhtouve7f;
_Static_assert(sizeof(gm_thsh8t5u55gps) == 20 && _Alignof(gm_thsh8t5u55gps) == 4 && offsetof(gm_thsh8t5u55gps, f0) == 0 && offsetof(gm_thsh8t5u55gps, f1) == 4 && offsetof(gm_thsh8t5u55gps, f2) == 8, "layout of `(from: Ty, to: Ty, span: Span)`");
_Static_assert(sizeof(gm_tibupt4dmcqai) == 24 && _Alignof(gm_tibupt4dmcqai) == 8 && offsetof(gm_tibupt4dmcqai, f0) == 0 && offsetof(gm_tibupt4dmcqai, f1) == 16, "layout of `StrLit`");
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
typedef struct { void * f0; gm_t08olq97rscn3 f1; } gm_tioie2ju3q4ul;
_Static_assert(sizeof(gm_tjpqvfd72o62u) == 32 && _Alignof(gm_tjpqvfd72o62u) == 8 && offsetof(gm_tjpqvfd72o62u, f0) == 0 && offsetof(gm_tjpqvfd72o62u, f1) == 8 && offsetof(gm_tjpqvfd72o62u, f2) == 24, "layout of `SoaInfo`");
_Static_assert(sizeof(gm_tkaapcii86udk) == 24 && _Alignof(gm_tkaapcii86udk) == 8 && offsetof(gm_tkaapcii86udk, f0) == 0 && offsetof(gm_tkaapcii86udk, f1) == 8, "layout of `Place`");
_Static_assert(sizeof(gm_tkn42u7na3t81) == 8 && _Alignof(gm_tkn42u7na3t81) == 4 && offsetof(gm_tkn42u7na3t81, f0) == 0 && offsetof(gm_tkn42u7na3t81, f1) == 4, "layout of `?Ty`");
typedef struct { gm_ts6oqppjh0ba3 f0; gm_ts6oqppjh0ba3 f1; gm_ts6oqppjh0ba3 f2; } gm_tl2e7spmb4osd;
_Static_assert(sizeof(gm_tlcm4lvu3g1a8) == 16 && _Alignof(gm_tlcm4lvu3g1a8) == 8 && offsetof(gm_tlcm4lvu3g1a8, f0) == 0 && offsetof(gm_tlcm4lvu3g1a8, f1) == 8, "layout of `(i: usize, x: Ty)`");
_Static_assert(sizeof(gm_tlljg61a2ctgq) == 32 && _Alignof(gm_tlljg61a2ctgq) == 8 && offsetof(gm_tlljg61a2ctgq, f0) == 0 && offsetof(gm_tlljg61a2ctgq, f1) == 8 && offsetof(gm_tlljg61a2ctgq, f2) == 24, "layout of `(callee: ^Expr, args: []Arg, trailing_comma: bool)`");
_Static_assert(sizeof(gm_tlr9dp305htm7) == 24 && _Alignof(gm_tlr9dp305htm7) == 8 && offsetof(gm_tlr9dp305htm7, f0) == 0 && offsetof(gm_tlr9dp305htm7, f1) == 8, "layout of `CondPart`");
typedef struct { gm_th2lqbo77pvsc f0; void * f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_tm7a6rksvs6n5;
_Static_assert(sizeof(gm_tnnjeerolos0a) == 8 && _Alignof(gm_tnnjeerolos0a) == 4 && offsetof(gm_tnnjeerolos0a, f0) == 0 && offsetof(gm_tnnjeerolos0a, f1) == 4, "layout of `ArrayKind`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
_Static_assert(sizeof(gm_tnqk2qt7j42o9) == 4 && _Alignof(gm_tnqk2qt7j42o9) == 4 && offsetof(gm_tnqk2qt7j42o9, a) == 0, "layout of `[1]Ty`");
_Static_assert(sizeof(gm_tommf4j9upomc) == 24 && _Alignof(gm_tommf4j9upomc) == 8 && offsetof(gm_tommf4j9upomc, f0) == 0 && offsetof(gm_tommf4j9upomc, f1) == 16, "layout of `(string, u32)`");
_Static_assert(sizeof(gm_tpe1hv3fekn05) == 24 && _Alignof(gm_tpe1hv3fekn05) == 8 && offsetof(gm_tpe1hv3fekn05, f0) == 0 && offsetof(gm_tpe1hv3fekn05, f1) == 16, "layout of `(attrs: []Attribute, inner: ^Expr)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t2i8ne5ibd7v4 v0; gm_thsh8t5u55gps v1; gm_t2i8ne5ibd7v4 v2; gm_t91vm1g0k2rpm v3; } f1; } gm_tpm220j5qhjke;
typedef struct { gm_th2lqbo77pvsc f0; void * f1; } gm_tpouv9r7lrri2;
_Static_assert(sizeof(gm_tq7s98ihonkju) == 16 && _Alignof(gm_tq7s98ihonkju) == 8 && offsetof(gm_tq7s98ihonkju, f0) == 0 && offsetof(gm_tq7s98ihonkju, f1) == 8, "layout of `AllocError!^HandlerDecl`");
typedef struct { gm_trqfvmjtt4sij f0; uint8_t f1; uint8_t pe[7]; } gm_tqetrgo0q24b5;
_Static_assert(sizeof(gm_tqsk8dt6ib3qq) == 8 && _Alignof(gm_tqsk8dt6ib3qq) == 4 && offsetof(gm_tqsk8dt6ib3qq, a) == 0, "layout of `[2]Ty`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_ttggq7ng4kp3p v0; gm_tpe1hv3fekn05 v1; gm_t3mf58gv9kf3n v2; gm_tpe1hv3fekn05 v3; void * v4; gm_tgtub0v94d5u5 v5; } f1; } gm_trcmmd12cefb1;
_Static_assert(sizeof(gm_trl2khmuphncd) == 32 && _Alignof(gm_trl2khmuphncd) == 8 && offsetof(gm_trl2khmuphncd, f0) == 0 && offsetof(gm_trl2khmuphncd, f1) == 16 && offsetof(gm_trl2khmuphncd, f2) == 24, "layout of `IfExpr`");
typedef struct { gm_ts6oqppjh0ba3 f0; uint8_t p1[6]; void * f1; void * f2; } gm_trl9d7m1k5lbd;
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_ts6oqppjh0ba3) == 2 && _Alignof(gm_ts6oqppjh0ba3) == 1 && offsetof(gm_ts6oqppjh0ba3, f0) == 0 && offsetof(gm_ts6oqppjh0ba3, f1) == 1, "layout of `BinOp`");
_Static_assert(sizeof(gm_ts78ca2amthgu) == 40 && _Alignof(gm_ts78ca2amthgu) == 8 && offsetof(gm_ts78ca2amthgu, f0) == 0 && offsetof(gm_ts78ca2amthgu, f1) == 8 && offsetof(gm_ts78ca2amthgu, f2) == 24, "layout of `(ty: Ty, name: string, span: Span)`");
_Static_assert(sizeof(gm_tsoie1ppgdvcg) == 40 && _Alignof(gm_tsoie1ppgdvcg) == 8 && offsetof(gm_tsoie1ppgdvcg, f0) == 0 && offsetof(gm_tsoie1ppgdvcg, f1) == 16 && offsetof(gm_tsoie1ppgdvcg, f2) == 20 && offsetof(gm_tsoie1ppgdvcg, f3) == 24, "layout of `LocalDecl`");
_Static_assert(sizeof(gm_tsprp1cqsr44h) == 40 && _Alignof(gm_tsprp1cqsr44h) == 8 && offsetof(gm_tsprp1cqsr44h, f0) == 0 && offsetof(gm_tsprp1cqsr44h, f1) == 32, "layout of `?Captured`");
typedef struct { gm_tvdg94bk7aa22 f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tt8o5kebou62r;
typedef struct { gm_t6j6smj7qoctu f0; uint8_t f1; uint8_t pe[7]; } gm_ttu09lij36f8c;
_Static_assert(sizeof(gm_tud7oovfvnf43) == 80 && _Alignof(gm_tud7oovfvnf43) == 8 && offsetof(gm_tud7oovfvnf43, f0) == 0 && offsetof(gm_tud7oovfvnf43, f1) == 16 && offsetof(gm_tud7oovfvnf43, f2) == 32 && offsetof(gm_tud7oovfvnf43, f3) == 48 && offsetof(gm_tud7oovfvnf43, f4) == 64, "layout of `(string, string, string, string, string)`");
_Static_assert(sizeof(gm_tupk62cr6lbdu) == 24 && _Alignof(gm_tupk62cr6lbdu) == 8 && offsetof(gm_tupk62cr6lbdu, f0) == 0 && offsetof(gm_tupk62cr6lbdu, f1) == 8 && offsetof(gm_tupk62cr6lbdu, f2) == 12, "layout of `(base: ^Expr, index: u32, index_span: Span)`");
typedef struct { gm_ts6oqppjh0ba3 f0; gm_ts6oqppjh0ba3 f1; gm_ts6oqppjh0ba3 f2; gm_ts6oqppjh0ba3 f3; gm_ts6oqppjh0ba3 f4; gm_ts6oqppjh0ba3 f5; gm_ts6oqppjh0ba3 f6; gm_ts6oqppjh0ba3 f7; } gm_tv78bma7gi894;
_Static_assert(sizeof(gm_tvdg94bk7aa22) == 12 && _Alignof(gm_tvdg94bk7aa22) == 4 && offsetof(gm_tvdg94bk7aa22, f0) == 0 && offsetof(gm_tvdg94bk7aa22, f1) == 4, "layout of `Builtin`");
_Static_assert(sizeof(gm_t1s75i08qtptd) == 40 && _Alignof(gm_t1s75i08qtptd) == 8 && offsetof(gm_t1s75i08qtptd, f0) == 0 && offsetof(gm_t1s75i08qtptd, f1) == 32, "layout of `?Ident`");
typedef struct { uint64_t f0; gm_tm7a6rksvs6n5 f1; } gm_t3gcf8sa6b1tu;
_Static_assert(sizeof(gm_t419mo45g7j0a) == 32 && _Alignof(gm_t419mo45g7j0a) == 8 && offsetof(gm_t419mo45g7j0a, f0) == 0 && offsetof(gm_t419mo45g7j0a, f1) == 24, "layout of `(effect_: EffectRef, op: u32)`");
_Static_assert(sizeof(gm_t428ue2u3hf5g) == 32 && _Alignof(gm_t428ue2u3hf5g) == 8 && offsetof(gm_t428ue2u3hf5g, f0) == 0 && offsetof(gm_t428ue2u3hf5g, f1) == 8, "layout of `ConstKey`");
_Static_assert(sizeof(gm_t4gut2c9fj1kt) == 184 && _Alignof(gm_t4gut2c9fj1kt) == 8 && offsetof(gm_t4gut2c9fj1kt, f0) == 0 && offsetof(gm_t4gut2c9fj1kt, f1) == 8 && offsetof(gm_t4gut2c9fj1kt, f2) == 24 && offsetof(gm_t4gut2c9fj1kt, f3) == 40 && offsetof(gm_t4gut2c9fj1kt, f4) == 80 && offsetof(gm_t4gut2c9fj1kt, f5) == 120 && offsetof(gm_t4gut2c9fj1kt, f6) == 160, "layout of `Diagnostic`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tjcj4j2gbh6c8 v0; uint16_t v1; gm_tbngattl1fo51 v2; gm_tbngattl1fo51 v3; gm_tbngattl1fo51 v4; gm_t8nckeqfenl8a v5; gm_t6j6smj7qoctu v6; gm_t88rcucg1b7uk v7; gm_tgglga8ro3isc v8; gm_tddtjfggdat0d v9; gm_tddtjfggdat0d v10; gm_tiilmfd16v3fc v11; gm_t88rcucg1b7uk v12; uint32_t v13; gm_tgglga8ro3isc v14; gm_t428ue2u3hf5g v15; gm_tcmtrmhj7ak4u v16; uint32_t v17; gm_tnpifv20irnph v18; uint32_t v19; } f1; } gm_t53at80pfdus3;
typedef struct { gm_tbci5ospk5nnn f0; gm_tbci5ospk5nnn f1; uint8_t f2; uint8_t pe[7]; } gm_t59juu943t8sl;
_Static_assert(sizeof(gm_t8cc1mtbrhlpt) == 40 && _Alignof(gm_t8cc1mtbrhlpt) == 8 && offsetof(gm_t8cc1mtbrhlpt, f0) == 0 && offsetof(gm_t8cc1mtbrhlpt, f1) == 32, "layout of `?SoaInfo`");
_Static_assert(sizeof(gm_t8nckeqfenl8a) == 24 && _Alignof(gm_t8nckeqfenl8a) == 8 && offsetof(gm_t8nckeqfenl8a, f0) == 0 && offsetof(gm_t8nckeqfenl8a, f1) == 8 && offsetof(gm_t8nckeqfenl8a, f2) == 12, "layout of `(len: u64, elem: Ty, kind: ArrayKind)`");
_Static_assert(sizeof(gm_t8ta482bl6be0) == 48 && _Alignof(gm_t8ta482bl6be0) == 8 && offsetof(gm_t8ta482bl6be0, f0) == 0 && offsetof(gm_t8ta482bl6be0, f1) == 8, "layout of `(i: usize, x: FieldDef)`");
_Static_assert(sizeof(gm_t9ldr1q5hn9fq) == 4 && _Alignof(gm_t9ldr1q5hn9fq) == 1 && offsetof(gm_t9ldr1q5hn9fq, f0) == 0 && offsetof(gm_t9ldr1q5hn9fq, f1) == 2, "layout of `(BinOp, BinOp)`");
_Static_assert(sizeof(gm_tagc77imt2chm) == 32 && _Alignof(gm_tagc77imt2chm) == 8 && offsetof(gm_tagc77imt2chm, f0) == 0 && offsetof(gm_tagc77imt2chm, f1) == 8, "layout of `PathPattern`");
_Static_assert(sizeof(gm_tandvbt63uk7n) == 24 && _Alignof(gm_tandvbt63uk7n) == 8 && offsetof(gm_tandvbt63uk7n, f0) == 0 && offsetof(gm_tandvbt63uk7n, f1) == 8, "layout of `(op: SoaOp, args: []Expr)`");
_Static_assert(sizeof(gm_tb7u19loo3j87) == 24 && _Alignof(gm_tb7u19loo3j87) == 8 && offsetof(gm_tb7u19loo3j87, f0) == 0 && offsetof(gm_tb7u19loo3j87, f1) == 16, "layout of `?(len: u64, elem: Ty)`");
_Static_assert(sizeof(gm_tbci5ospk5nnn) == 48 && _Alignof(gm_tbci5ospk5nnn) == 8 && offsetof(gm_tbci5ospk5nnn, f0) == 0 && offsetof(gm_tbci5ospk5nnn, f1) == 8, "layout of `Int`");
typedef struct { gm_t1s75i08qtptd f0; void * f1; } gm_tchr52g0ema53;
_Static_assert(sizeof(gm_tcuumjkg5eni6) == 72 && _Alignof(gm_tcuumjkg5eni6) == 8 && offsetof(gm_tcuumjkg5eni6, f0) == 0 && offsetof(gm_tcuumjkg5eni6, f1) == 32, "layout of `IfBranch`");
_Static_assert(sizeof(gm_tddtjfggdat0d) == 48 && _Alignof(gm_tddtjfggdat0d) == 8 && offsetof(gm_tddtjfggdat0d, f0) == 0 && offsetof(gm_tddtjfggdat0d, f1) == 16 && offsetof(gm_tddtjfggdat0d, f2) == 24 && offsetof(gm_tddtjfggdat0d, f3) == 40, "layout of `FnTy`");
_Static_assert(sizeof(gm_teo9d4mncjfdu) == 24 && _Alignof(gm_teo9d4mncjfdu) == 8 && offsetof(gm_teo9d4mncjfdu, f0) == 0 && offsetof(gm_teo9d4mncjfdu, f1) == 16, "layout of `BreakExpr`");
_Static_assert(sizeof(gm_tet9guhev5r4u) == 56 && _Alignof(gm_tet9guhev5r4u) == 8 && offsetof(gm_tet9guhev5r4u, f0) == 0 && offsetof(gm_tet9guhev5r4u, f1) == 32, "layout of `IntrinsicExpr`");
typedef struct { uint64_t f0; gm_tcuumjkg5eni6 f1; } gm_tf9j5enq7gf67;
_Static_assert(sizeof(gm_thetuhtouve7f) == 12 && _Alignof(gm_thetuhtouve7f) == 4 && offsetof(gm_thetuhtouve7f, f0) == 0 && offsetof(gm_thetuhtouve7f, f1) == 8, "layout of `?(inclusive: bool, elem: Ty)`");
typedef struct { gm_tbci5ospk5nnn f0; uint8_t f1; uint8_t pe[7]; } gm_thrdpodrs8pgt;
typedef struct { void * f0; gm_t1s75i08qtptd f1; void * f2; } gm_tioi3jh9v22g5;
_Static_assert(sizeof(gm_tioie2ju3q4ul) == 40 && _Alignof(gm_tioie2ju3q4ul) == 8 && offsetof(gm_tioie2ju3q4ul, f0) == 0 && offsetof(gm_tioie2ju3q4ul, f1) == 8, "layout of `(base: ^Expr, name: Ident)`");
_Static_assert(sizeof(gm_tl2e7spmb4osd) == 6 && _Alignof(gm_tl2e7spmb4osd) == 1 && offsetof(gm_tl2e7spmb4osd, f0) == 0 && offsetof(gm_tl2e7spmb4osd, f1) == 2 && offsetof(gm_tl2e7spmb4osd, f2) == 4, "layout of `(BinOp, BinOp, BinOp)`");
_Static_assert(sizeof(gm_tm7a6rksvs6n5) == 48 && _Alignof(gm_tm7a6rksvs6n5) == 8 && offsetof(gm_tm7a6rksvs6n5, f0) == 0 && offsetof(gm_tm7a6rksvs6n5, f1) == 24 && offsetof(gm_tm7a6rksvs6n5, f2) == 32, "layout of `TuplePart`");
typedef struct { gm_tbci5ospk5nnn f0; gm_tbci5ospk5nnn f1; } gm_tngtuiuskn938;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t08olq97rscn3 v0; gm_t08olq97rscn3 v1; void * v2; gm_tlcl4u558l6p5 v3; void * v4; gm_tagc77imt2chm v5; gm_b16_8 v6; } f1; } gm_to361lgknsu69;
_Static_assert(sizeof(gm_tpm220j5qhjke) == 48 && _Alignof(gm_tpm220j5qhjke) == 8 && offsetof(gm_tpm220j5qhjke, f0) == 0 && offsetof(gm_tpm220j5qhjke, f1) == 8, "layout of `Obligation`");
_Static_assert(sizeof(gm_tpouv9r7lrri2) == 32 && _Alignof(gm_tpouv9r7lrri2) == 8 && offsetof(gm_tpouv9r7lrri2, f0) == 0 && offsetof(gm_tpouv9r7lrri2, f1) == 24, "layout of `SpreadPart`");
_Static_assert(sizeof(gm_tqetrgo0q24b5) == 48 && _Alignof(gm_tqetrgo0q24b5) == 8 && offsetof(gm_tqetrgo0q24b5, f0) == 0 && offsetof(gm_tqetrgo0q24b5, f1) == 40, "layout of `?List(usize)`");
_Static_assert(sizeof(gm_trcmmd12cefb1) == 32 && _Alignof(gm_trcmmd12cefb1) == 8 && offsetof(gm_trcmmd12cefb1, f0) == 0 && offsetof(gm_trcmmd12cefb1, f1) == 8, "layout of `Stmt`");
_Static_assert(sizeof(gm_trl9d7m1k5lbd) == 24 && _Alignof(gm_trl9d7m1k5lbd) == 8 && offsetof(gm_trl9d7m1k5lbd, f0) == 0 && offsetof(gm_trl9d7m1k5lbd, f1) == 8 && offsetof(gm_trl9d7m1k5lbd, f2) == 16, "layout of `(op: BinOp, lhs: ^Expr, rhs: ^Expr)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint32_t v0; gm_t419mo45g7j0a v1; void * v2; } f1; } gm_tsth9qu1dv0g3;
_Static_assert(sizeof(gm_tt8o5kebou62r) == 32 && _Alignof(gm_tt8o5kebou62r) == 8 && offsetof(gm_tt8o5kebou62r, f0) == 0 && offsetof(gm_tt8o5kebou62r, f1) == 16, "layout of `(op: Builtin, args: []Expr)`");
_Static_assert(sizeof(gm_ttu09lij36f8c) == 40 && _Alignof(gm_ttu09lij36f8c) == 8 && offsetof(gm_ttu09lij36f8c, f0) == 0 && offsetof(gm_ttu09lij36f8c, f1) == 32, "layout of `?Split`");
_Static_assert(sizeof(gm_tv78bma7gi894) == 16 && _Alignof(gm_tv78bma7gi894) == 1 && offsetof(gm_tv78bma7gi894, f0) == 0 && offsetof(gm_tv78bma7gi894, f1) == 2 && offsetof(gm_tv78bma7gi894, f2) == 4 && offsetof(gm_tv78bma7gi894, f3) == 6 && offsetof(gm_tv78bma7gi894, f4) == 8 && offsetof(gm_tv78bma7gi894, f5) == 10 && offsetof(gm_tv78bma7gi894, f6) == 12 && offsetof(gm_tv78bma7gi894, f7) == 14, "layout of `(BinOp, BinOp, BinOp, BinOp, BinOp, BinOp, BinOp, BinOp)`");
typedef struct { uint32_t f0; uint8_t p1[4]; gm_to361lgknsu69 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_t0d02142k4tu8;
typedef struct { uint8_t f0; uint8_t p1[7]; union { void * v0; void * v1; gm_tibupt4dmcqai v2; uint8_t v3; gm_t08olq97rscn3 v4; gm_t08olq97rscn3 v5; void * v6; gm_b16_8 v7; void * v8; gm_t8ghgl2jj0qha v9; gm_trl9d7m1k5lbd v10; gm_tioi3jh9v22g5 v11; void * v12; void * v13; gm_t3mf58gv9kf3n v14; void * v15; gm_tlljg61a2ctgq v16; gm_t3mf58gv9kf3n v17; gm_tao63pk3r18v5 v18; gm_tioie2ju3q4ul v19; gm_tupk62cr6lbdu v20; void * v21; gm_tgtub0v94d5u5 v22; gm_tgtub0v94d5u5 v23; gm_tgtub0v94d5u5 v24; gm_t3mf58gv9kf3n v25; gm_t3mf58gv9kf3n v26; void * v27; void * v28; void * v29; void * v30; void * v31; gm_tpe1hv3fekn05 v32; void * v33; void * v34; gm_tibupt4dmcqai v35; void * v36; void * v37; void * v38; void * v39; void * v40; gm_t79rbdv4rafe5 v41; void * v42; gm_tchr52g0ema53 v43; gm_t1s75i08qtptd v44; gm_t3mf58gv9kf3n v45; gm_tet9guhev5r4u v46; } f1; } gm_t2gtbh39ptekd;
_Static_assert(sizeof(gm_t3gcf8sa6b1tu) == 56 && _Alignof(gm_t3gcf8sa6b1tu) == 8 && offsetof(gm_t3gcf8sa6b1tu, f0) == 0 && offsetof(gm_t3gcf8sa6b1tu, f1) == 8, "layout of `(i: usize, x: TuplePart)`");
_Static_assert(sizeof(gm_t53at80pfdus3) == 56 && _Alignof(gm_t53at80pfdus3) == 8 && offsetof(gm_t53at80pfdus3, f0) == 0 && offsetof(gm_t53at80pfdus3, f1) == 8, "layout of `TyKind`");
_Static_assert(sizeof(gm_t59juu943t8sl) == 104 && _Alignof(gm_t59juu943t8sl) == 8 && offsetof(gm_t59juu943t8sl, f0) == 0 && offsetof(gm_t59juu943t8sl, f1) == 48 && offsetof(gm_t59juu943t8sl, f2) == 96, "layout of `(lo: Int, hi: Int, inclusive: bool)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint32_t v0; void * v1; gm_t59juu943t8sl v2; void * v3; void * v4; void * v5; gm_ttggq7ng4kp3p v6; gm_b16_8 v7; gm_b16_8 v8; } f1; } gm_tb38v5s9j1r32;
_Static_assert(sizeof(gm_tchr52g0ema53) == 48 && _Alignof(gm_tchr52g0ema53) == 8 && offsetof(gm_tchr52g0ema53, f0) == 0 && offsetof(gm_tchr52g0ema53, f1) == 40, "layout of `BreakExpr`");
_Static_assert(sizeof(gm_tf9j5enq7gf67) == 80 && _Alignof(gm_tf9j5enq7gf67) == 8 && offsetof(gm_tf9j5enq7gf67, f0) == 0 && offsetof(gm_tf9j5enq7gf67, f1) == 8, "layout of `(i: usize, x: IfBranch)`");
_Static_assert(sizeof(gm_thrdpodrs8pgt) == 56 && _Alignof(gm_thrdpodrs8pgt) == 8 && offsetof(gm_thrdpodrs8pgt, f0) == 0 && offsetof(gm_thrdpodrs8pgt, f1) == 48, "layout of `?Int`");
_Static_assert(sizeof(gm_tioi3jh9v22g5) == 56 && _Alignof(gm_tioi3jh9v22g5) == 8 && offsetof(gm_tioi3jh9v22g5, f0) == 0 && offsetof(gm_tioi3jh9v22g5, f1) == 8 && offsetof(gm_tioi3jh9v22g5, f2) == 48, "layout of `ElseExpr`");
_Static_assert(sizeof(gm_tngtuiuskn938) == 96 && _Alignof(gm_tngtuiuskn938) == 8 && offsetof(gm_tngtuiuskn938, f0) == 0 && offsetof(gm_tngtuiuskn938, f1) == 48, "layout of `Rat`");
_Static_assert(sizeof(gm_to361lgknsu69) == 40 && _Alignof(gm_to361lgknsu69) == 8 && offsetof(gm_to361lgknsu69, f0) == 0 && offsetof(gm_to361lgknsu69, f1) == 8, "layout of `PatternKind`");
typedef struct { gm_tsth9qu1dv0g3 f0; gm_b16_8 f1; } gm_to76jli3bqddq;
_Static_assert(sizeof(gm_tsth9qu1dv0g3) == 40 && _Alignof(gm_tsth9qu1dv0g3) == 8 && offsetof(gm_tsth9qu1dv0g3, f0) == 0 && offsetof(gm_tsth9qu1dv0g3, f1) == 8, "layout of `Callee`");
typedef struct { gm_tngtuiuskn938 f0; gm_tngtuiuskn938 f1; } gm_tu15p0hi2qlv6;
_Static_assert(sizeof(gm_t0d02142k4tu8) == 64 && _Alignof(gm_t0d02142k4tu8) == 8 && offsetof(gm_t0d02142k4tu8, f0) == 0 && offsetof(gm_t0d02142k4tu8, f1) == 8 && offsetof(gm_t0d02142k4tu8, f2) == 48, "layout of `Pattern`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tbci5ospk5nnn v0; gm_tngtuiuskn938 v1; uint8_t v2; gm_tu15p0hi2qlv6 v3; } f1; } gm_t0ialelq3ql1k;
_Static_assert(sizeof(gm_t2gtbh39ptekd) == 64 && _Alignof(gm_t2gtbh39ptekd) == 8 && offsetof(gm_t2gtbh39ptekd, f0) == 0 && offsetof(gm_t2gtbh39ptekd, f1) == 8, "layout of `ExprKind`");
_Static_assert(sizeof(gm_tb38v5s9j1r32) == 112 && _Alignof(gm_tb38v5s9j1r32) == 8 && offsetof(gm_tb38v5s9j1r32, f0) == 0 && offsetof(gm_tb38v5s9j1r32, f1) == 8, "layout of `PatKind`");
typedef struct { uint32_t f0; uint8_t p1[4]; gm_t2gtbh39ptekd f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_tc3oht4ksth9d;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_tb38v5s9j1r32 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_tc9qv69frb08e;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tbci5ospk5nnn v0; double v1; void * v2; void * v3; uint8_t v4; gm_b16_8 v5; gm_b16_8 v6; uint32_t v7; gm_tkn42u7na3t81 v8; gm_tkn42u7na3t81 v9; gm_t88rcucg1b7uk v10; gm_t3rcjibl8kkg4 v11; gm_t08olq97rscn3 v12; uint32_t v13; gm_tcc1rq4n1avb2 v14; gm_t3877pk7lv748 v15; void * v16; gm_tbl8il92l97ut v17; gm_tlcl4u558l6p5 v18; gm_tao63pk3r18v5 v19; void * v20; void * v21; void * v22; void * v23; uint32_t v24; uint32_t v25; uint32_t v26; gm_t8ghgl2jj0qha v27; gm_trl9d7m1k5lbd v28; gm_ta612fil812pm v29; gm_t8jsc4n9vp2ib v30; gm_tt8o5kebou62r v31; gm_to76jli3bqddq v32; gm_b16_8 v33; gm_b16_8 v34; void * v35; gm_ttggq7ng4kp3p v36; void * v37; gm_tsplfr33dsvji v38; gm_t3dcoc7077t1q v39; gm_t083kd7b1kkag v40; gm_tlcl4u558l6p5 v41; gm_tkaapcii86udk v42; gm_tlcl4u558l6p5 v43; void * v44; gm_trl2khmuphncd v45; gm_t710lsrq1m8gk v46; gm_teo9d4mncjfdu v47; gm_te9hrk0u35rev v48; gm_ttggq7ng4kp3p v49; void * v50; gm_t3mf58gv9kf3n v51; void * v52; void * v53; void * v54; gm_tandvbt63uk7n v55; gm_t710lsrq1m8gk v56; } f1; } gm_teti411eujvf4;
_Static_assert(sizeof(gm_to76jli3bqddq) == 56 && _Alignof(gm_to76jli3bqddq) == 8 && offsetof(gm_to76jli3bqddq, f0) == 0 && offsetof(gm_to76jli3bqddq, f1) == 40, "layout of `(callee: Callee, args: []Expr)`");
typedef struct { gm_t0d02142k4tu8 f0; uint8_t f1; uint8_t pe[7]; } gm_toqf51949ppls;
_Static_assert(sizeof(gm_tu15p0hi2qlv6) == 192 && _Alignof(gm_tu15p0hi2qlv6) == 8 && offsetof(gm_tu15p0hi2qlv6, f0) == 0 && offsetof(gm_tu15p0hi2qlv6, f1) == 96, "layout of `(re: Rat, im: Rat)`");
_Static_assert(sizeof(gm_t0ialelq3ql1k) == 200 && _Alignof(gm_t0ialelq3ql1k) == 8 && offsetof(gm_t0ialelq3ql1k, f0) == 0 && offsetof(gm_t0ialelq3ql1k, f1) == 8, "layout of `Untyped`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t0ialelq3ql1k v0; gm_ts78ca2amthgu v1; } f1; } gm_t1hofbajv6dns;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t0ialelq3ql1k v0; } f1; } gm_t5vbkdq81qfs7;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_teti411eujvf4 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_t69m56meqefs8;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t88rcucg1b7uk v0; uint32_t v1; uint32_t v2; gm_tiilmfd16v3fc v3; gm_tnpifv20irnph v4; uint32_t v5; gm_tcc1rq4n1avb2 v6; gm_t88rcucg1b7uk v7; gm_t0ialelq3ql1k v8; uint64_t v9; gm_t88rcucg1b7uk v10; } f1; } gm_t6qooom8qa2do;
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint32_t v0; gm_t0ialelq3ql1k v1; uint32_t v2; gm_ts78ca2amthgu v3; gm_t88rcucg1b7uk v4; } f1; } gm_t930q5l38hdap;
_Static_assert(sizeof(gm_tc3oht4ksth9d) == 88 && _Alignof(gm_tc3oht4ksth9d) == 8 && offsetof(gm_tc3oht4ksth9d, f0) == 0 && offsetof(gm_tc3oht4ksth9d, f1) == 8 && offsetof(gm_tc3oht4ksth9d, f2) == 72, "layout of `Expr`");
_Static_assert(sizeof(gm_tc9qv69frb08e) == 136 && _Alignof(gm_tc9qv69frb08e) == 8 && offsetof(gm_tc9qv69frb08e, f0) == 0 && offsetof(gm_tc9qv69frb08e, f1) == 8 && offsetof(gm_tc9qv69frb08e, f2) == 120, "layout of `Pat`");
_Static_assert(sizeof(gm_teti411eujvf4) == 64 && _Alignof(gm_teti411eujvf4) == 8 && offsetof(gm_teti411eujvf4, f0) == 0 && offsetof(gm_teti411eujvf4, f1) == 8, "layout of `ExprKind`");
_Static_assert(sizeof(gm_toqf51949ppls) == 72 && _Alignof(gm_toqf51949ppls) == 8 && offsetof(gm_toqf51949ppls, f0) == 0 && offsetof(gm_toqf51949ppls, f1) == 64, "layout of `?Pattern`");
typedef struct { gm_t1s75i08qtptd f0; gm_tc3oht4ksth9d f1; uint8_t f2; uint8_t pe[7]; } gm_tv1o1j44ii4db;
_Static_assert(sizeof(gm_t1hofbajv6dns) == 208 && _Alignof(gm_t1hofbajv6dns) == 8 && offsetof(gm_t1hofbajv6dns, f0) == 0 && offsetof(gm_t1hofbajv6dns, f1) == 8, "layout of `LocalConstKind`");
typedef struct { uint64_t f0; gm_tv1o1j44ii4db f1; } gm_t2cmlgt3383cp;
_Static_assert(sizeof(gm_t5vbkdq81qfs7) == 208 && _Alignof(gm_t5vbkdq81qfs7) == 8 && offsetof(gm_t5vbkdq81qfs7, f0) == 0 && offsetof(gm_t5vbkdq81qfs7, f1) == 8, "layout of `UntypedResult`");
_Static_assert(sizeof(gm_t69m56meqefs8) == 88 && _Alignof(gm_t69m56meqefs8) == 8 && offsetof(gm_t69m56meqefs8, f0) == 0 && offsetof(gm_t69m56meqefs8, f1) == 8 && offsetof(gm_t69m56meqefs8, f2) == 72, "layout of `Expr`");
_Static_assert(sizeof(gm_t6qooom8qa2do) == 208 && _Alignof(gm_t6qooom8qa2do) == 8 && offsetof(gm_t6qooom8qa2do, f0) == 0 && offsetof(gm_t6qooom8qa2do, f1) == 8, "layout of `DeclValue`");
_Static_assert(sizeof(gm_t930q5l38hdap) == 208 && _Alignof(gm_t930q5l38hdap) == 8 && offsetof(gm_t930q5l38hdap, f0) == 0 && offsetof(gm_t930q5l38hdap, f1) == 8, "layout of `LocalBinding`");
typedef struct { gm_b16_8 f0; gm_t1hofbajv6dns f1; } gm_tb07pp6njg0bk;
typedef struct { gm_t930q5l38hdap f0; uint32_t f1; uint8_t pe[4]; } gm_tfj8gdjm726n4;
typedef struct { gm_t5vbkdq81qfs7 f0; uint8_t f1; uint8_t pe[7]; } gm_tkv1vap90vht2;
typedef struct { gm_t930q5l38hdap f0; uint8_t f1; uint8_t pe[7]; } gm_tqvu78rpi6q1h;
_Static_assert(sizeof(gm_tv1o1j44ii4db) == 136 && _Alignof(gm_tv1o1j44ii4db) == 8 && offsetof(gm_tv1o1j44ii4db, f0) == 0 && offsetof(gm_tv1o1j44ii4db, f1) == 40 && offsetof(gm_tv1o1j44ii4db, f2) == 128, "layout of `TupleElem`");
_Static_assert(sizeof(gm_t2cmlgt3383cp) == 144 && _Alignof(gm_t2cmlgt3383cp) == 8 && offsetof(gm_t2cmlgt3383cp, f0) == 0 && offsetof(gm_t2cmlgt3383cp, f1) == 8, "layout of `(i: usize, x: TupleElem)`");
_Static_assert(sizeof(gm_tb07pp6njg0bk) == 224 && _Alignof(gm_tb07pp6njg0bk) == 8 && offsetof(gm_tb07pp6njg0bk, f0) == 0 && offsetof(gm_tb07pp6njg0bk, f1) == 16, "layout of `LocalConst`");
_Static_assert(sizeof(gm_tfj8gdjm726n4) == 216 && _Alignof(gm_tfj8gdjm726n4) == 8 && offsetof(gm_tfj8gdjm726n4, f0) == 0 && offsetof(gm_tfj8gdjm726n4, f1) == 208, "layout of `(binding: LocalBinding, depth: u32)`");
typedef struct { gm_tb07pp6njg0bk f0; uint8_t f1; uint8_t pe[7]; } gm_tk1gen3hgkm51;
_Static_assert(sizeof(gm_tkv1vap90vht2) == 216 && _Alignof(gm_tkv1vap90vht2) == 8 && offsetof(gm_tkv1vap90vht2, f0) == 0 && offsetof(gm_tkv1vap90vht2, f1) == 208, "layout of `?UntypedResult`");
_Static_assert(sizeof(gm_tqvu78rpi6q1h) == 216 && _Alignof(gm_tqvu78rpi6q1h) == 8 && offsetof(gm_tqvu78rpi6q1h, f0) == 0 && offsetof(gm_tqvu78rpi6q1h, f1) == 208, "layout of `?LocalBinding`");
typedef struct { gm_tfj8gdjm726n4 f0; uint8_t f1; uint8_t pe[7]; } gm_tsf6kp4ohnuqs;
_Static_assert(sizeof(gm_tk1gen3hgkm51) == 232 && _Alignof(gm_tk1gen3hgkm51) == 8 && offsetof(gm_tk1gen3hgkm51, f0) == 0 && offsetof(gm_tk1gen3hgkm51, f1) == 224, "layout of `?LocalConst`");
_Static_assert(sizeof(gm_tsf6kp4ohnuqs) == 224 && _Alignof(gm_tsf6kp4ohnuqs) == 8 && offsetof(gm_tsf6kp4ohnuqs, f0) == 0 && offsetof(gm_tsf6kp4ohnuqs, f1) == 216, "layout of `?(binding: LocalBinding, depth: u32)`");
struct gm_fr_add_note_46v502s {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_addr_of_expr_ffg7r6j {
    gm_frame h;
    void * ret;
};
struct gm_fr_arith_op_hsstk43 {
    gm_frame h;
    void * ret;
};
struct gm_fr_array_elem_join_clnoo81 {
    gm_frame h;
    gm_tkn42u7na3t81 ret;
};
struct gm_fr_auto_deref_vdb1hdn {
    gm_frame h;
    void * ret;
};
struct gm_fr_binary_op_7ocdl4v {
    gm_frame h;
    gm_t0d02142k4tu8 l10; /* p */
    void * ret;
};
struct gm_fr_bit_or_j1tdrhl {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_tbci5ospk5nnn l1; /* b */
    gm_tbci5ospk5nnn ret;
};
struct gm_fr_bits_convert_8rcef5o {
    gm_frame h;
    void * ret;
};
struct gm_fr_bits_operand_d1ppvrt {
    gm_frame h;
    gm_tkn42u7na3t81 ret;
};
struct gm_fr_bits_padding_mask_d4bk9a4 {
    gm_frame h;
    gm_t0mglksrkfa5e l6; /* f */
    gm_tbci5ospk5nnn l7; /* mask */
    gm_t0mglksrkfa5e l11; /* f */
    gm_thrdpodrs8pgt ret;
};
struct gm_fr_block_of_2gjl2i1 {
    gm_frame h;
    void * ret;
};
struct gm_fr_bool_ty_pn2p277 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_chained_opt_op_nnt3ijn {
    gm_frame h;
    gm_trqfvmjtt4sij l6; /* stmts */
    gm_trqfvmjtt4sij l7; /* cond */
    gm_trqfvmjtt4sij l8; /* operands */
    void * ret;
};
struct gm_fr_coerce_element_9ue7d7u {
    gm_frame h;
    void * ret;
};
struct gm_fr_coercion_steps_ibqvhta {
    gm_frame h;
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_complex_of_kh32tib {
    gm_frame h;
    gm_tkn42u7na3t81 ret;
};
struct gm_fr_copy_pmkg67r {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_trqfvmjtt4sij l1; /* l */
    gm_tbci5ospk5nnn ret;
};
struct gm_fr_decl_expr_lv2h8vf {
    gm_frame h;
    gm_t0ialelq3ql1k l7; /* v */
    void * ret;
};
struct gm_fr_decl_visible_bd168ti {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_depth_fr7pn6n {
    gm_frame h;
    uint32_t ret;
};
struct gm_fr_deref_expr_resldhc {
    gm_frame h;
    void * ret;
};
struct gm_fr_embeds_2jpqm45 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_emit_dph5v1e {
    gm_frame h;
    gm_t4gut2c9fj1kt l1; /* d */
    gm_b0_1 ret;
};
struct gm_fr_empty_slice_3v2gjd4_3 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_5nn7ph0 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_b8btuen {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_d914i5u {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_fv6i5s8 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_g3tddrb_2 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ijtp9lo {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_l8m0eod {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_lmbdkhb_2 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_qm5tp1g {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_rcd2jln {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_s1lbe5s {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_vj3ckdj_2 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_eq_allowed_d841u9b {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_equalize_ni1r5ra {
    gm_frame h;
    void * l4; /* tys */
    gm_t3mf58gv9kf3n ret;
};
struct gm_fr_error_89glhjr {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_error_9huj44a {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_error_expr_o6qt7e2 {
    gm_frame h;
    void * ret;
};
struct gm_fr_error_ki8i3k6 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_expr_kind_397ptdj {
    gm_frame h;
    gm_tioi3jh9v22g5 l25; /* x */
    gm_t79rbdv4rafe5 l40; /* b */
    gm_tchr52g0ema53 l42; /* b */
    gm_tet9guhev5r4u l53; /* i */
    void * ret;
};
struct gm_fr_expr_kind_text_6rlftr8 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_expr_vmp704u {
    gm_frame h;
    gm_t5vbkdq81qfs7 l4; /* r */
    gm_t0ialelq3ql1k l5; /* v */
    void * ret;
};
struct gm_fr_extend_7hs67uj {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_field_jlden9b {
    gm_frame h;
    void * ret;
};
struct gm_fr_field_of_1bfelct {
    gm_frame h;
    gm_t4gut2c9fj1kt l19; /* d */
    void * ret;
};
struct gm_fr_field_visible_here_20vnoda {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_fn_ref_aqs2fl4 {
    gm_frame h;
    void * ret;
};
struct gm_fr_from_int_ofjuoqh {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* i */
    gm_tngtuiuskn938 ret;
};
struct gm_fr_has_eq_iba53hs {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* seen */
    uint8_t ret;
};
struct gm_fr_has_infer_3o82a0b {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_has_optional_operand_279ro7i {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_has_param_u9t0efs {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_hidden_field_message_b0pdek7 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_hidden_field_u7uao2s {
    gm_frame h;
    gm_t0mglksrkfa5e l5; /* f */
    gm_ttu09lij36f8c ret;
};
struct gm_fr_hoist_prefix_r1mevpl {
    gm_frame h;
    void * ret;
};
struct gm_fr_if_expr_tiak47t {
    gm_frame h;
    gm_trqfvmjtt4sij l6; /* conds */
    gm_trqfvmjtt4sij l7; /* thens */
    gm_tsprp1cqsr44h l8; /* captured */
    gm_tcuumjkg5eni6 l13; /* br */
    gm_t0d02142k4tu8 l16; /* pat */
    gm_trqfvmjtt4sij l27; /* stmts */
    gm_t79rbdv4rafe5 l31; /* b */
    gm_trqfvmjtt4sij l34; /* branches */
    gm_trqfvmjtt4sij l53; /* arms */
    void * ret;
};
struct gm_fr_index_of_part_7kb7erm {
    gm_frame h;
    gm_tm7a6rksvs6n5 l5; /* p */
    gm_tf5srh4mp4b3i ret;
};
struct gm_fr_index_uh6hfri {
    gm_frame h;
    void * ret;
};
struct gm_fr_int_ci6airg {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_is_error_8k27gd1 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_literal_tif53t0 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_vector_gkq6b88 {
    gm_frame h;
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
struct gm_fr_join_branch_anseitf {
    gm_frame h;
    void * ret;
};
struct gm_fr_join_type_cck70vp {
    gm_frame h;
    gm_trqfvmjtt4sij l3; /* live */
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_kind_jcnt4hm {
    gm_frame h;
    gm_t53at80pfdus3 ret;
};
struct gm_fr_len_value_t6b3fd0 {
    gm_frame h;
    gm_tbci5ospk5nnn l7; /* n */
    void * ret;
};
struct gm_fr_list_58nr42v_3 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_5kthfl8 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_epvducg {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_go2qg0b {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_ir0h3tc_2 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_nof7vfj {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_s1kpkpr {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_s83jn2q {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_uahmact {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_uvfu1k1 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_v5d1o9b_2 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_vp1djv1_2 {
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
struct gm_fr_many_ptr_l4v11f8 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_mk_9bc97fd {
    gm_frame h;
    gm_teti411eujvf4 l1; /* kind */
    void * ret;
};
struct gm_fr_mk_pat_hr2l6c4 {
    gm_frame h;
    gm_tb38v5s9j1r32 l1; /* kind */
    void * ret;
};
struct gm_fr_module_of_n0d5f6n {
    gm_frame h;
    gm_tf5srh4mp4b3i ret;
};
struct gm_fr_module_path_lpneg5l {
    gm_frame h;
    gm_tf5srh4mp4b3i ret;
};
struct gm_fr_name_blqdq2r {
    gm_frame h;
    gm_tfj8gdjm726n4 l4; /* found */
    gm_tsoie1ppgdvcg l6; /* local */
    gm_t0ialelq3ql1k l9; /* v */
    gm_ts78ca2amthgu l10; /* c */
    gm_tb07pp6njg0bk l13; /* c */
    gm_t0ialelq3ql1k l14; /* v */
    gm_ts78ca2amthgu l15; /* k */
    gm_t4gut2c9fj1kt l16; /* d */
    void * ret;
};
struct gm_fr_named_fn_value_b79g18s {
    gm_frame h;
    void * ret;
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
struct gm_fr_neg_expr_p72q1ni {
    gm_frame h;
    gm_tbci5ospk5nnn l6; /* i */
    void * ret;
};
struct gm_fr_never_ty_b949mgo {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_null_value_ruu6332 {
    gm_frame h;
    void * ret;
};
struct gm_fr_one_08qg1op {
    gm_frame h;
    gm_tbci5ospk5nnn ret;
};
struct gm_fr_one_expr_q6kn0hn {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_b16_8 ret;
};
struct gm_fr_optional_rh6okj3 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_param_kind_a0aikmb {
    gm_frame h;
    gm_tnnjeerolos0a ret;
};
struct gm_fr_pointer_arith_abqsrkl {
    gm_frame h;
    void * ret;
};
struct gm_fr_pop_570g79n {
    gm_frame h;
    gm_t9805qtonbsag ret;
};
struct gm_fr_pop_8sodrk9 {
    gm_frame h;
    gm_td8hfgklo8dil ret;
};
struct gm_fr_pop_dt2bd2s {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* x */
    gm_tqetrgo0q24b5 ret;
};
struct gm_fr_pop_scope_g5j7o4e {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_prefix_on_call_f7od3q2 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_printer_h7jigd7 {
    gm_frame h;
    gm_tmm67r2c94d5j ret;
};
struct gm_fr_ptr_t1h2m2d {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_push_1voul2a {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_3ef746b {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_7koe9lk {
    gm_frame h;
    gm_tpm220j5qhjke l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_9a0n4ld {
    gm_frame h;
    gm_tm7a6rksvs6n5 l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_bpovr2k_3 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_d6c76ud_2 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_d943vgp_2 {
    gm_frame h;
    gm_t69m56meqefs8 l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_eb7kbhp_2 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_k3l9nic {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_pf02nv6 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_vd1290p {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_range_template_dbt7218 {
    gm_frame h;
    gm_t9805qtonbsag ret;
};
struct gm_fr_range_value_b02g2v9 {
    gm_frame h;
    gm_trqfvmjtt4sij l18; /* parts */
    void * ret;
};
struct gm_fr_recursive_without_ret_48l31rm {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_regrow_021udu6 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_0jmnnpk {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_2onqfca {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_4bta8ne_2 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_52486fh {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_5719f1b {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_61fb705_2 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_emnjbt3 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_h08har5_2 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_h9tbucc_3 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_hkmvuvc {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_j4iu2lk {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_reject_second_class_c561fm5 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_report_eardf07 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_resolve_ty_uk6c1lp {
    gm_frame h;
    gm_tet9guhev5r4u l2; /* i */
    gm_tc3oht4ksth9d l4; /* x */
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_restore_scope_edqq1v8 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_shl_qqecl81 {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_tbci5ospk5nnn ret;
};
struct gm_fr_show_8uijkbs {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_show_fsnl6fa {
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
struct gm_fr_spread_tuple_6180uqs {
    gm_frame h;
    gm_trqfvmjtt4sij l10; /* stmts */
    gm_trqfvmjtt4sij l11; /* parts */
    gm_trqfvmjtt4sij l12; /* spread */
    gm_tv1o1j44ii4db l15; /* el */
    gm_tm7a6rksvs6n5 l34; /* p */
    gm_tm7a6rksvs6n5 l39; /* p */
    gm_trqfvmjtt4sij l40; /* values */
    gm_trqfvmjtt4sij l41; /* ts */
    gm_trqfvmjtt4sij l42; /* names */
    gm_tm7a6rksvs6n5 l46; /* p */
    void * ret;
};
struct gm_fr_string_ty_j74ff0o {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_struct_field_cc6n63f {
    gm_frame h;
    gm_t0mglksrkfa5e l10; /* f */
    void * ret;
};
struct gm_fr_sub_pktm5cr {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_tbci5ospk5nnn l1; /* b */
    gm_tbci5ospk5nnn ret;
};
struct gm_fr_temp_ilbvj8q {
    gm_frame h;
    uint32_t ret;
};
struct gm_fr_truncate_7r782uc {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_truncate_daelje0 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_truncate_dsb08pk {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_truncate_ua7l21h {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_truncate_upeumbt {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_tuple_field_dmnnit2 {
    gm_frame h;
    void * ret;
};
struct gm_fr_tuple_qkn0cor {
    gm_frame h;
    gm_tv1o1j44ii4db l16; /* el */
    gm_trqfvmjtt4sij l17; /* vals */
    gm_tv1o1j44ii4db l21; /* el */
    gm_trqfvmjtt4sij l25; /* names */
    gm_tv1o1j44ii4db l29; /* el */
    gm_trqfvmjtt4sij l31; /* ts */
    gm_t69m56meqefs8 l34; /* v */
    void * ret;
};
struct gm_fr_type_is_683ln2e {
    gm_frame h;
    void * ret;
};
struct gm_fr_type_of_name_oqtujpn {
    gm_frame h;
    gm_t930q5l38hdap l15; /* b */
    gm_tkn42u7na3t81 ret;
};
struct gm_fr_type_operand_ta2mhln {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_u8_ty_afkvj25 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_unary_asf0eot {
    gm_frame h;
    gm_tbci5ospk5nnn l10; /* mask */
    void * ret;
};
struct gm_fr_unify_operands_5sp7j1j {
    gm_frame h;
    gm_t4gut2c9fj1kt l7; /* d */
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_unit_2age34u {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_unit_ohk62mc {
    gm_frame h;
    void * ret;
};
struct gm_fr_unsupported_r3r7qdh {
    gm_frame h;
    void * ret;
};
struct gm_fr_unsupported_t7lgvp9 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_untyped_complex_20agrmu {
    gm_frame h;
    gm_tngtuiuskn938 l0; /* re */
    gm_tngtuiuskn938 l1; /* im */
    gm_teti411eujvf4 ret;
};
struct gm_fr_untyped_float_m6lbr0g {
    gm_frame h;
    gm_tngtuiuskn938 l0; /* r */
    gm_teti411eujvf4 ret;
};
struct gm_fr_untyped_literal_pfu1ft4 {
    gm_frame h;
    gm_tbci5ospk5nnn l4; /* i */
    gm_tngtuiuskn938 l5; /* r */
    gm_tu15p0hi2qlv6 l8; /* c */
    void * ret;
};
struct gm_fr_usize_ty_qnrd30f {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_value_param_ty_qrv22u4 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_visible_ll07dr9 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_wraps_mh8ot70 {
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
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b);
inline bool gm_eq0ogca82g1f(const uint8_t *a, const uint8_t *b);
inline bool gm_eq467d5cm1d5(const uint8_t *a, const uint8_t *b);
inline bool gm_eq9m6mvtmgde(const uint8_t *a, const uint8_t *b);
inline bool gm_eqbqb4gb1d92(const uint8_t *a, const uint8_t *b);
inline bool gm_eqehhdtpeti1(const uint8_t *a, const uint8_t *b);
inline bool gm_eqff85ac10o3(const uint8_t *a, const uint8_t *b);
inline __attribute__((aligned(4))) void gm_f_add_note_46v502s(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void * gm_f_addr_of_expr_ffg7r6j(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_tkn42u7na3t81 a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) void * gm_f_arith_op_hsstk43(void *env, gm_node *cx, gm_node *inst, void * a0, gm_ts6oqppjh0ba3 a1, void * a2, void * a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_array_elem_join_clnoo81(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void * gm_f_array_type_construction_vgjg706(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2, void * a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) void * gm_f_auto_deref_vdb1hdn(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) void * gm_f_binary_op_7ocdl4v(void *env, gm_node *cx, gm_node *inst, void * a0, gm_ts6oqppjh0ba3 a1, void * a2, void * a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) void * gm_f_bind_temp_sa5rcbr(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2);
__attribute__((aligned(4))) void gm_f_binding_cond_v86td9t(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2);
__attribute__((aligned(4))) gm_b16_8 gm_f_binop_text_0ljfc0r(gm_ts6oqppjh0ba3 a0);
inline __attribute__((aligned(4))) void gm_f_bit_or_j1tdrhl(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_bit_width_b7ofh2h(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void * gm_f_bits_convert_8rcef5o(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_bits_operand_d1ppvrt(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) void gm_f_bits_padding_mask_d4bk9a4(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_bits_struct_ty_l1kjtap(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void gm_f_bitwise_f0f970e(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t9qkh0rsooc8d a0, const void * a1, const void * a2);
__attribute__((aligned(4))) void * gm_f_block_expected_bv4ukm2(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint8_t a2, gm_tkn42u7na3t81 a3);
inline __attribute__((aligned(4))) void * gm_f_block_of_2gjl2i1(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) void * gm_f_block_typed_e7svg7n(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0, gm_b16_8 a1, void * a2, gm_tl874t2atnlh3 a3);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_bool_ty_pn2p277(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void * gm_f_break_expr_l653dlv(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, void * a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) void * gm_f_builtin_a7cjd7d(void *env, gm_node *cx, gm_node *inst, gm_tvdg94bk7aa22 a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_builtin_type_64h714c(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_c_union_fields_btdvc8b(void *gm_out, void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void * gm_f_call_l10ebge(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_b16_8 a2, gm_tkn42u7na3t81 a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) void * gm_f_chained_opt_op_nnt3ijn(void *env, gm_node *cx, gm_node *inst, void * a0, gm_ts6oqppjh0ba3 a1, void * a2, void * a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) gm_b0_1 gm_f_check_deprecated_j3dq26t(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) void * gm_f_coerce_element_9ue7d7u(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_tkn42u7na3t81 a2);
__attribute__((aligned(4))) void * gm_f_coerce_sudc6c1(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2);
inline __attribute__((aligned(4))) void gm_f_coercion_steps_ibqvhta(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_t88rcucg1b7uk a2);
inline __attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_complex_of_kh32tib(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void * gm_f_compound_number_binary_p7eerjs(void *env, gm_node *cx, gm_node *inst, void * a0, gm_ts6oqppjh0ba3 a1, void * a2, void * a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) void * gm_f_compound_number_unary_4h478fs(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t9qkh0rsooc8d a1, void * a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) gm_b16_8 gm_f_cond_99ajlhk(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) void * gm_f_construct_optional_cffo1ol(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) void * gm_f_construct_variant_o6is81c(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, const void * a2, const void * a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) void * gm_f_construction_call_ktqtrpo(void * a0);
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_construction_elem_type_qm1ncub(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) void * gm_f_continue_expr_53f04kf(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_tl874t2atnlh3 a2);
inline __attribute__((aligned(4))) void gm_f_copy_pmkg67r(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void * gm_f_decl_expr_lv2h8vf(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2, gm_tkn42u7na3t81 a3, gm_tl874t2atnlh3 a4);
inline __attribute__((aligned(4))) uint8_t gm_f_decl_visible_bd168ti(void * a0, uint64_t a1, uint64_t a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_declare_3l5mhoj(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, const void * a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) uint8_t gm_f_denotes_type_jgfet92(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
inline __attribute__((aligned(4))) uint32_t gm_f_depth_fr7pn6n(void * a0);
__attribute__((aligned(4))) void * gm_f_deref_expr_resldhc(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) void * gm_f_elements_tuple_gdm5n6q(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) void * gm_f_else_expr_5lgn2qp(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, const void * a2, void * a3, gm_tkn42u7na3t81 a4, gm_tl874t2atnlh3 a5);
__attribute__((aligned(4))) uint8_t gm_f_embeds_2jpqm45(void * a0, gm_csguankko4lmhd a1, gm_csguankko4lmhd a2);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_emit_dph5v1e(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_emit_with_continuation_n_lv5ofqt(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_tl874t2atnlh3 a2, gm_tl874t2atnlh3 a3);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_3v2gjd4_3(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_5nn7ph0(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_b8btuen(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_d914i5u(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_fv6i5s8(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_g3tddrb_2(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_l8m0eod(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_lmbdkhb_2(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_qm5tp1g(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_rcd2jln(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_s1lbe5s(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_vj3ckdj_2(void);
__attribute__((aligned(4))) uint8_t gm_f_eq_allowed_d841u9b(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_t3mf58gv9kf3n gm_f_equalize_ni1r5ra(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2, gm_tl874t2atnlh3 a3);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_error_89glhjr(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_error_9huj44a(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void * gm_f_error_expr_o6qt7e2(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_error_ki8i3k6(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_error_s6hn871(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void * gm_f_expect_bool_cdo5g1q(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) void * gm_f_expr_kind_397ptdj(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_tkn42u7na3t81 a2);
__attribute__((aligned(4))) gm_b16_8 gm_f_expr_kind_text_6rlftr8(void * a0);
__attribute__((aligned(4))) void * gm_f_expr_vmp704u(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_tkn42u7na3t81 a2);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_extend_7hs67uj(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void * gm_f_field_jlden9b(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, const void * a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) void * gm_f_field_of_1bfelct(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, const void * a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) uint8_t gm_f_field_visible_here_20vnoda(void * a0, gm_t9qkh0rsooc8d a1, gm_csguankko4lmhd a2);
__attribute__((aligned(4))) void * gm_f_fn_literal_fiu78lb(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_tkn42u7na3t81 a2, const void * a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) void * gm_f_fn_ref_aqs2fl4(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) void gm_f_fn_ty_qaih7ts(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) void * gm_f_for_loop_b6hjsbq(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) void gm_f_format_00gguat(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_1p8frtu(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_32qgu0c(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_32r56ja(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_33edc64(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_535vc4d(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_54ifjd1(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_6nmb1bf(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_8oppoek(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_9hpfisf(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_a612q5e(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_aovijlt(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_aqpcar1(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_bjrmbam(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_bvitrsu(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_c0n18m1(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_c8qq9n9(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_ducddq0(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_dukk0t3(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_e79cvtl(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_eadlofj(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_htqqv8t(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_io9sb4j(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_ko5t8rr(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_md1u4fd(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_pdashts(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_r6aoskm(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_r8e1h45(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_sfq90nb(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_ta7nl7k(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_vau3iu8(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_from_int_ofjuoqh(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_from_u64_ud13q5v(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint64_t a0);
__attribute__((aligned(4))) void * gm_f_generic_fn_from_expected_ok2k7go(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_tkn42u7na3t81 a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_get_espvf96(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void * gm_f_handler_literal_avosuha(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_tkn42u7na3t81 a2, gm_tl874t2atnlh3 a3);
inline __attribute__((aligned(4))) uint8_t gm_f_has_eq_iba53hs(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) uint8_t gm_f_has_eq_in_5a59avv(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, void * a2);
inline __attribute__((aligned(4))) uint8_t gm_f_has_infer_3o82a0b(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) uint8_t gm_f_has_optional_operand_279ro7i(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2);
inline __attribute__((aligned(4))) uint8_t gm_f_has_param_u9t0efs(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) uint8_t gm_f_has_spread_bndtpd6(gm_b16_8 a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_hidden_field_message_b0pdek7(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_t88rcucg1b7uk a2, gm_t9qkh0rsooc8d a3);
__attribute__((aligned(4))) void gm_f_hidden_field_u7uao2s(void *gm_out, void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) gm_ttu09lij36f8c gm_f_hidden_field_u7uao2s_c(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void * gm_f_hoist_prefix_r1mevpl(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) uint8_t gm_f_hoists_13k2a2s(void * a0);
__attribute__((aligned(4))) void * gm_f_if_expr_tiak47t(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint8_t a2, gm_tkn42u7na3t81 a3);
__attribute__((aligned(4))) void * gm_f_imaginary_literal_6d41gfv(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) void * gm_f_in_expr_su8ofmp(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2, uint8_t a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_index_of_o9r57ha(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_index_of_part_7kb7erm(gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) void * gm_f_index_uh6hfri(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) void * gm_f_index_value_ufcur8j(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_instantiate_adt_lbjcui1(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_int_ci6airg(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, uint16_t a2);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_intern_ia19mhl(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) void * gm_f_intrinsic_usmsf3f(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) uint8_t gm_f_irrefutable_lpkmeii(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) uint8_t gm_f_is_builtin_type_name_v5ttoka(gm_b16_8 a0);
__attribute__((aligned(4))) uint8_t gm_f_is_compound_number_kbipjl5(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_error_8k27gd1(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) uint8_t gm_f_is_literal_tif53t0(void * a0);
__attribute__((aligned(4))) void gm_f_is_pattern_operand_p51tban(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) void * gm_f_is_pattern_test_kdd7cfh(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2, uint8_t a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) uint8_t gm_f_is_plain_n60iihm(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) uint8_t gm_f_is_positional_array_6sufsk8(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_vector_gkq6b88(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_zero_5aept6u(const void * a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_zero_8tcct9o(const void * a0);
inline __attribute__((aligned(4))) void * gm_f_join_branch_anseitf(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_join_type_cck70vp(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_key_int_pqscuvn(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
inline __attribute__((aligned(4))) void gm_f_kind_jcnt4hm(void *gm_out, void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void * gm_f_len_value_t6b3fd0(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_tl874t2atnlh3 a2);
inline __attribute__((aligned(4))) void gm_f_list_58nr42v_3(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_5kthfl8(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_epvducg(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_go2qg0b(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_ir0h3tc_2(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_nof7vfj(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_s1kpkpr(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_s83jn2q(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_uahmact(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_v5d1o9b_2(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_vp1djv1_2(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_literal_var_3sb5sti(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t9qkh0rsooc8d a1);
__attribute__((aligned(4))) void gm_f_local_at_p388mm4(void *gm_out, void * a0, uint32_t a1, uint32_t a2);
__attribute__((aligned(4))) void gm_f_local_const_idpokjh(void *gm_out, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_t9805qtonbsag gm_f_local_fn_aji8qnd(void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_lookup_59juqoo(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_lookup_local_6f38t7p(void *gm_out, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_lookup_local_at_ntm4scs(void *gm_out, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_lookup_module_name_cukfp7t(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_mag_shl_5df643p(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, uint64_t a1);
__attribute__((aligned(4))) void * gm_f_make_err_expr_6arm2q7(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_tkn42u7na3t81 a2, gm_tl874t2atnlh3 a3);
inline __attribute__((aligned(4))) void gm_f_make_r4pbu77(void *gm_out, uint8_t a0, const void * a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_many_ptr_l4v11f8(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) void * gm_f_match_expr_npcoo60(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint8_t a2, gm_tkn42u7na3t81 a3);
__attribute__((aligned(4))) uint8_t gm_f_mentions_complex_constan_lrgkqac(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
inline __attribute__((aligned(4))) void * gm_f_mk_9bc97fd(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0, const void * a1, gm_tl874t2atnlh3 a2);
inline __attribute__((aligned(4))) void * gm_f_mk_pat_hr2l6c4(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0, const void * a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_module_member_5fd2qm9(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, const void * a2, uint64_t a3);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_module_of_n0d5f6n(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_module_path_lpneg5l(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) void * gm_f_name_blqdq2r(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_tkn42u7na3t81 a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) void * gm_f_named_fn_value_b79g18s(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_b16_8 a2, gm_tkn42u7na3t81 a3, gm_tl874t2atnlh3 a4);
inline __attribute__((aligned(4))) void gm_f_neg_0u9uqg6(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_neg_agd5c5k(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void * gm_f_neg_expr_p72q1ni(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_tkn42u7na3t81 a2, gm_tl874t2atnlh3 a3);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_never_ty_b949mgo(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_1b2khgi(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_fnepb5f(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) uint32_t gm_f_new_local_003h9es(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_t88rcucg1b7uk a2, uint8_t a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_u1gvj7k(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_new_var_up2m1nq(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t9qkh0rsooc8d a2);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_ve8bb01_2(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_note_capture_0p0af7r(void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_note_local_use_h4lf1jh(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) void * gm_f_null_value_ruu6332(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tkn42u7na3t81 a1, gm_tl874t2atnlh3 a2);
inline __attribute__((aligned(4))) void gm_f_one_08qg1op(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_one_expr_q6kn0hn(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void * gm_f_opt_unwrap_3qt46b5(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_tl874t2atnlh3 a2);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_optional_rh6okj3(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_tnnjeerolos0a gm_f_param_kind_a0aikmb(void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_tqs5p4n2tq19v gm_f_place_mutability_0tlm4g8(void * a0, void * a1);
__attribute__((aligned(4))) void * gm_f_pointer_arith_abqsrkl(void *env, gm_node *cx, gm_node *inst, void * a0, gm_ts6oqppjh0ba3 a1, void * a2, void * a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_t9805qtonbsag gm_f_pop_570g79n(void * a0);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_pop_8sodrk9(void *gm_out, void * a0);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_pop_dt2bd2s(void *gm_out, void * a0);
inline __attribute__((aligned(4))) void gm_f_pop_scope_g5j7o4e(void * a0);
__attribute__((aligned(4))) uint8_t gm_f_prefix_on_call_f7od3q2(void * a0);
__attribute__((aligned(4))) void * gm_f_prefix_operand_3n5jchh(void * a0);
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_prelude_instance_arg_kh32dmf(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_t9805qtonbsag gm_f_prelude_template_7s9nsp0(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) void gm_f_printer_h7jigd7(void *gm_out, void * a0, void * a1);
__attribute__((aligned(4))) void * gm_f_promote_real_constant_v0nhili(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, gm_t88rcucg1b7uk a3);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_ptr_t1h2m2d(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_public_module_member_e2i0at7(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2, uint64_t a3);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_1voul2a(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_3ef746b(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_7koe9lk(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_9a0n4ld(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_bpovr2k_3(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_d6c76ud_2(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_d943vgp_2(void * a0, const void * a1);
__attribute__((aligned(4))) void * gm_f_push_down_hge098p(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_eb7kbhp_2(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_k3l9nic(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void gm_f_push_o4ckdr1(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_pf02nv6(void * a0, void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_push_scope_mu7j3lg(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_vd1290p(void * a0, uint8_t a1);
__attribute__((aligned(4))) gm_thetuhtouve7f gm_f_range_of_dmmo768(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_t9805qtonbsag gm_f_range_template_dbt7218(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1);
__attribute__((aligned(4))) void * gm_f_range_value_b02g2v9(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, void * a2, void * a3, gm_tkn42u7na3t81 a4, gm_tl874t2atnlh3 a5);
__attribute__((aligned(4))) gm_b0_1 gm_f_record_effects_0eel5vu(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_tl874t2atnlh3 a2);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_recursive_without_ret_48l31rm(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_tl874t2atnlh3 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_021udu6(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_0jmnnpk(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_2onqfca(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_4bta8ne_2(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_52486fh(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_5719f1b(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_61fb705_2(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_emnjbt3(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_h08har5_2(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_h9tbucc_3(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_hkmvuvc(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_j4iu2lk(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_raw_4ag1s1i(void * a0, uint64_t a1, void * a2, uint64_t a3, uint64_t a4, uint64_t a5);
__attribute__((aligned(4))) uint8_t gm_f_reject_second_class_c561fm5(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_report_eardf07(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) uint8_t gm_f_require_unsigned_0fhlknt(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, gm_t9qkh0rsooc8d a3);
__attribute__((aligned(4))) void * gm_f_res_unwrap_e6id52i(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) void gm_f_resolve_decl_pus042v(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_resolve_t2kal61(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_resolve_ty_uk6c1lp(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_resolve_type_jjodbei(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
inline __attribute__((aligned(4))) void gm_f_restore_scope_edqq1v8(void * a0, const void * a1);
__attribute__((aligned(4))) void * gm_f_resume_expr_l33thg3(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) void * gm_f_return_expr_0qs023i(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) uint8_t gm_f_same_package_vmps24r(void * a0, uint64_t a1, uint64_t a2);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_settle_h9ikvs5(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_shallow_8ur803a(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2);
inline __attribute__((aligned(4))) void gm_f_shl_qqecl81(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, uint64_t a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_8uijkbs(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_fsnl6fa(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_kmjeh8s(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_signed_add_ek0dpmo(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint8_t a0, gm_b16_8 a1, uint8_t a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_slice_5dt9121(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) void * gm_f_slice_expr_iamf9el(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2, void * a3, uint8_t a4, gm_tl874t2atnlh3 a5);
__attribute__((aligned(4))) void * gm_f_soa_deref_uiefo9t(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) void * gm_f_soa_element_field_lo71q0l(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, const void * a2, void * a3, const void * a4, gm_tl874t2atnlh3 a5);
__attribute__((aligned(4))) void * gm_f_soa_eq_ho8ji9t(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2, const void * a3, uint8_t a4, gm_tl874t2atnlh3 a5);
__attribute__((aligned(4))) void * gm_f_soa_member_vkrr5kf(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, const void * a2, const void * a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) void gm_f_soa_of_jufdcj2(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) void gm_f_spread_parts_e62q2l2(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2);
__attribute__((aligned(4))) void * gm_f_spread_tuple_6180uqs(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_tkn42u7na3t81 a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) void gm_f_steps_for_768gb3b(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_t88rcucg1b7uk a2, uint8_t a3);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_string_ty_j74ff0o(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_csguankko4lmhd gm_f_strip_wrappers_e5thpib(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void * gm_f_struct_field_cc6n63f(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_b16_8 a2, gm_tl874t2atnlh3 a3, uint32_t a4);
__attribute__((aligned(4))) void gm_f_struct_fields_chqu3it(void *gm_out, void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void gm_f_sub_pktm5cr(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_suffix_type_icfvn23(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tj2m9dbh1ekv6 a1);
__attribute__((aligned(4))) gm_tnpifv20irnph gm_f_sys_set_pqe8mto(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) uint32_t gm_f_temp_ilbvj8q(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) void gm_f_trim_8rdppmk(void * a0);
inline __attribute__((aligned(4))) void gm_f_truncate_7r782uc(void * a0, uint64_t a1);
inline __attribute__((aligned(4))) void gm_f_truncate_daelje0(void * a0, uint64_t a1);
inline __attribute__((aligned(4))) void gm_f_truncate_dsb08pk(void * a0, uint64_t a1);
inline __attribute__((aligned(4))) void gm_f_truncate_ua7l21h(void * a0, uint64_t a1);
inline __attribute__((aligned(4))) void gm_f_truncate_upeumbt(void * a0, uint64_t a1);
__attribute__((aligned(4))) void * gm_f_try_expr_ntgm5mj(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) void gm_f_try_untyped_8j8up35(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) void * gm_f_tuple_field_dmnnit2(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint32_t a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) void * gm_f_tuple_qkn0cor(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_tkn42u7na3t81 a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) gm_b0_1 gm_f_ty_vka2r8h(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void * gm_f_type_is_683ln2e(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, void * a2, void * a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_type_of_name_oqtujpn(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
inline __attribute__((aligned(4))) uint8_t gm_f_type_operand_ta2mhln(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_type_param_3av4r8i(void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_u8_ty_afkvj25(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void * gm_f_unary_asf0eot(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t9qkh0rsooc8d a1, void * a2, gm_tkn42u7na3t81 a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_unify_operands_5sp7j1j(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_t88rcucg1b7uk a2, gm_tl874t2atnlh3 a3, gm_b16_8 a4);
__attribute__((aligned(4))) gm_t9qkh0rsooc8d gm_f_unify_t2qjv2p(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, gm_t88rcucg1b7uk a3);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_unit_2age34u(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) void * gm_f_unit_ohk62mc(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_unknown_name_dmegum5(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2, gm_tl874t2atnlh3 a3);
inline __attribute__((aligned(4))) void * gm_f_unsupported_r3r7qdh(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_unsupported_t7lgvp9(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_untyped_complex_20agrmu(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
inline __attribute__((aligned(4))) void gm_f_untyped_float_m6lbr0g(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void * gm_f_untyped_literal_pfu1ft4(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_tl874t2atnlh3 a2);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_usize_ty_qnrd30f(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_t9805qtonbsag gm_f_value_param_tof1h6t(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_value_param_ty_qrv22u4(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
__attribute__((aligned(4))) void * gm_f_vector_binary_bicu7u0(void *env, gm_node *cx, gm_node *inst, void * a0, gm_ts6oqppjh0ba3 a1, void * a2, void * a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) void * gm_f_vector_construction_fhm2mm5(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, void * a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) void gm_f_vector_parts_crrfopm(void *gm_out, void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void * gm_f_vector_unary_2mbe97t(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t9qkh0rsooc8d a1, void * a2, gm_tl874t2atnlh3 a3);
inline __attribute__((aligned(4))) uint8_t gm_f_visible_ll07dr9(void * a0, gm_t9qkh0rsooc8d a1, uint64_t a2, uint64_t a3);
__attribute__((aligned(4))) void * gm_f_with_expr_vi78dtj(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint8_t a2, gm_tkn42u7na3t81 a3);
__attribute__((aligned(4))) uint8_t gm_f_wraps_mh8ot70(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
inline __attribute__((aligned(4))) void gm_f_zero_9r6o7sj(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_zero_s25l990(void *gm_out, void *env, gm_node *cx, gm_node *inst);
extern uint8_t gm_c3t36dlh4g5[16] __attribute__((aligned(8)));
extern uint8_t gm_k3tttsr3hp1[40] __attribute__((aligned(8)));
extern uint8_t gm_k652m2ksgeg[40] __attribute__((aligned(8)));
extern uint8_t gm_kf1lpoo3h6o[40] __attribute__((aligned(8)));
extern uint8_t gm_ktdkih9htpv[40] __attribute__((aligned(8)));
extern const uint8_t gm_s01kpv06m7g4n[66];
extern const uint8_t gm_s0l72j5pkqi6s[9];
extern const uint8_t gm_s0pm20lshv21u[21];
extern const uint8_t gm_s0rphlcbno91v[8];
extern const uint8_t gm_s1c0ll7cvuidm[12];
extern const uint8_t gm_s1leke9tn7lrm[8];
extern const uint8_t gm_s2r7keqsravo0[6];
extern const uint8_t gm_s2svafu2na27p[17];
extern const uint8_t gm_s3231a310mran[10];
extern const uint8_t gm_s3nknbqjesra1[19];
extern const uint8_t gm_s43cq4krigs2b[10];
extern const uint8_t gm_s4o70d89183c2[4];
extern const uint8_t gm_s4srjt2tusqhn[30];
extern const uint8_t gm_s55kqbt4f7gau[4];
extern const uint8_t gm_s6i1ull1350kv[5];
extern const uint8_t gm_s6j4ug98j8dce[39];
extern const uint8_t gm_s6n6v50inu7p0[8];
extern const uint8_t gm_s6tc15kuvnm0c[26];
extern const uint8_t gm_s709tdhiijp4r[78];
extern const uint8_t gm_s759q7smc97t0[75];
extern const uint8_t gm_s7i2mni64durc[18];
extern const uint8_t gm_s8952afsouaih[21];
extern const uint8_t gm_s9ur7sj9r0g63[7];
extern const uint8_t gm_sa7q2bqmn8lgb[9];
extern const uint8_t gm_sa831sfid0hf3[2];
extern const uint8_t gm_safll42cn0mu7[31];
extern const uint8_t gm_safvj03783ie0[65];
extern const uint8_t gm_sbgl5dgur8udg[7];
extern const uint8_t gm_sbqhd9c69ol7k[17];
extern const uint8_t gm_scfl995gsjrop[58];
extern const uint8_t gm_scuaf7ud4smlt[123];
extern const uint8_t gm_sdpig9sk6ngdh[7];
extern const uint8_t gm_serasf77ce9g3[52];
extern const uint8_t gm_seue7mq07v0oc[6];
extern const uint8_t gm_sfdbfrv46lihd[2];
extern const uint8_t gm_sfeo7g7huf2q1[17];
extern const uint8_t gm_sfpopfqbubj89[35];
extern const uint8_t gm_sgfmudgd82uhr[24];
extern const uint8_t gm_sgjci31f2rs96[49];
extern const uint8_t gm_shnmjdmsj431f[17];
extern const uint8_t gm_shv1k5m05egbe[6];
extern const uint8_t gm_sie1pq1m4mo8b[29];
extern const uint8_t gm_sinmpfqtbo9in[5];
extern const uint8_t gm_sj4o6222mnbmq[16];
extern const uint8_t gm_sje5p1r4d76uk[57];
extern const uint8_t gm_sk531tvbsm11t[44];
extern const uint8_t gm_skolmnfc0ccbd[2];
extern const uint8_t gm_slbgk65sat4bm[8];
extern const uint8_t gm_slj1im0lsqj9h[32];
extern const uint8_t gm_sm2k5tp7dtg07[4];
extern const uint8_t gm_sn05fk3conlo9[18];
extern const uint8_t gm_sncbtqrmp1734[14];
extern const uint8_t gm_snqvo5bgc2fch[8];
extern const uint8_t gm_so4gnucc3fl4v[7];
extern const uint8_t gm_sp6ona76fn0di[13];
extern const uint8_t gm_sqo8h5kg9id12[86];
extern const uint8_t gm_sra3ncr12fqek[56];
extern const char gm_src0ih77f43mhf2[];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_src6lm8esmv16p1[];
extern const char gm_srcc2qutubarto1[];
extern const char gm_srcg82mujgottmf[];
extern const char gm_srcgfglnv8p059r[];
extern const char gm_srch2l5nl06vsci[];
extern const char gm_srck5orfpjti60c[];
extern const char gm_srckg5f2c0p31ur[];
extern const char gm_srcl43af23eher3[];
extern const char gm_srclv0dcbbfhves[];
extern const char gm_srclv3pj5es0f8o[];
extern const char gm_srcod98slk3vnra[];
extern const char gm_srcrgc9bkquf6nr[];
extern const uint8_t gm_ss4rn3b9npsij[14];
extern const uint8_t gm_ss9l5l0jp0vfb[14];
extern const uint8_t gm_ssfg7m66qbmvk[65];
extern const uint8_t gm_su30fl1m1ebg6[5];
extern const uint8_t gm_suj5r301h9vv4[9];
extern const uint8_t gm_svm0rfaqk3b2q[4];
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
inline bool gm_eq467d5cm1d5(const uint8_t *a, const uint8_t *b) {
    return GM_LD(uint32_t, a) == GM_LD(uint32_t, b);
}
inline bool gm_eq9m6mvtmgde(const uint8_t *a, const uint8_t *b) {
    if (!gm_eq467d5cm1d5(a + 0, b + 0)) return false;
    return true;
}
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
inline bool gm_eqff85ac10o3(const uint8_t *a, const uint8_t *b) {
    if (!gm_eq467d5cm1d5(a + 0, b + 0)) return false;
    if (!gm_eq467d5cm1d5(a + 4, b + 4)) return false;
    if (!gm_eq467d5cm1d5(a + 8, b + 8)) return false;
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
inline __attribute__((aligned(4))) void * gm_f_block_of_2gjl2i1(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_tl874t2atnlh3 a2) {
    struct gm_fr_block_of_2gjl2i1 F;
    void * gm_l0; /* s */
    gm_b16_8 gm_l1; /* stmts */
    gm_tl874t2atnlh3 gm_l2; /* span */
    GM_STACK_CHECK(&F, 109u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        void * t3 = (void *)(((uint8_t *)t2) + 8);
        gm_t88rcucg1b7uk t4 = gm_f_unit_2age34u((void *)NULL, cx, NULL, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b16_8 t5 = gm_l1;
        gm_tl874t2atnlh3 t6 = gm_l2;
        void * t7 = gm_f_block_typed_e7svg7n((void *)NULL, cx, NULL, t4, t5, ((void *)0), t6);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t8 = 0;
        F.ret = t7;
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_bool_ty_pn2p277(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_bool_ty_pn2p277 F;
    void * gm_l0; /* t */
    GM_STACK_CHECK(&F, 84u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    gm_t53at80pfdus3 t2;
    memset(&t2, 0, sizeof t2);
    GM_ST(uint8_t, ((uint8_t *)&t2 + 0), ((uint8_t)4ULL));
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
inline __attribute__((aligned(4))) void gm_f_coercion_steps_ibqvhta(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_t88rcucg1b7uk a2) {
    struct gm_fr_coercion_steps_ibqvhta F;
    void * gm_l0; /* cx */
    gm_t88rcucg1b7uk gm_l1; /* src */
    gm_t88rcucg1b7uk gm_l2; /* target */
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t1 = gm_l0;
    gm_t88rcucg1b7uk t2 = gm_l1;
    gm_t88rcucg1b7uk t3 = gm_l2;
    gm_f_steps_for_768gb3b((void *)&(*(gm_th2lqbo77pvsc *)gm_out), (void *)NULL, cx, NULL, t1, t2, t3, ((uint8_t)1));
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
inline __attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_complex_of_kh32tib(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_complex_of_kh32tib F;
    void * gm_l0; /* s */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 64u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    gm_t88rcucg1b7uk t2 = gm_l1;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s0rphlcbno91v));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(7));
    gm_tkn42u7na3t81 t4 = gm_f_prelude_instance_arg_kh32dmf((void *)NULL, cx, NULL, t1, t2, t3);
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
inline __attribute__((aligned(4))) uint32_t gm_f_depth_fr7pn6n(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_depth_fr7pn6n F;
    void * gm_l0; /* cx */
    GM_STACK_CHECK_SHALLOW(&F, 36u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    uint64_t t2 = GM_LD(uintptr_t, ((((uint8_t *)t1) + 576) + 0) + 8);
    uint32_t t3 = (uint32_t)t2;
    F.ret = t3;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_emit_dph5v1e(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_emit_dph5v1e F;
    void * gm_l0; /* s */
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&F.l1, a1, sizeof F.l1);
    {
        void * t2 = gm_l0;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 0));
        void * t4 = GM_LD(void *, (((uint8_t *)t3) + 8));
        gm_f_push_o4ckdr1(t4, &(*(gm_t4gut2c9fj1kt *)((uint8_t *)&F.l1)));
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_3v2gjd4_3(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_3v2gjd4_3 F;
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
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 24)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_b8btuen(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_b8btuen F;
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
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 48)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_fv6i5s8(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_fv6i5s8 F;
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
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 8)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_g3tddrb_2(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_g3tddrb_2 F;
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
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 88)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_l8m0eod(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_l8m0eod F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_lmbdkhb_2(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_lmbdkhb_2 F;
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
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 24)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_qm5tp1g(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_qm5tp1g F;
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
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 48)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_rcd2jln(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_rcd2jln F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_s1lbe5s(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_s1lbe5s F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_vj3ckdj_2(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_vj3ckdj_2 F;
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
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 32)));
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_error_9huj44a(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2) {
    struct gm_fr_error_9huj44a F;
    void * gm_l0; /* cx */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_b16_8 gm_l2; /* msg */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 0));
        gm_tl874t2atnlh3 t4 = gm_l1;
        gm_b16_8 t5 = gm_l2;
        gm_b0_1 t6 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t3, t4, t5);
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
inline __attribute__((aligned(4))) void * gm_f_error_expr_o6qt7e2(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1) {
    struct gm_fr_error_expr_o6qt7e2 F;
    void * gm_l0; /* s */
    gm_tl874t2atnlh3 gm_l1; /* span */
    GM_STACK_CHECK(&F, 156u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    void * t2 = (void *)(((uint8_t *)t1) + 8);
    gm_t88rcucg1b7uk t3 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t2);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_b16_8 t4 = gm_f_empty_slice_g3tddrb_2();
    gm_teti411eujvf4 t5;
    memset(&t5, 0, sizeof t5);
    GM_ST(uint8_t, ((uint8_t *)&t5 + 0), ((uint8_t)35ULL));
    GM_ST(gm_b16_8, ((uint8_t *)&t5 + 8), t4);
    gm_tl874t2atnlh3 t6 = gm_l1;
    void * t7 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t3, &t5, t6);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.ret = t7;
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
inline __attribute__((aligned(4))) uint8_t gm_f_has_eq_iba53hs(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_has_eq_iba53hs F;
    void * gm_l0; /* s */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 36u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        gm_f_list_nof7vfj((void *)&F.l2, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = gm_l0;
        gm_t88rcucg1b7uk t3 = gm_l1;
        void * t4 = (void *)((uint8_t *)&F.l2);
        uint8_t t5 = gm_f_has_eq_in_5a59avv((void *)NULL, cx, NULL, t2, t3, t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t6 = 0;
        F.ret = t5;
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
inline __attribute__((aligned(4))) uint8_t gm_f_has_infer_3o82a0b(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_has_infer_3o82a0b F;
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
    if (t3 >= (uintptr_t)(t5)) gm_trap_index(t3, t5, gm_src0ih77f43mhf2, 252, 57);
    uint8_t t6 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t4) + 40) + 0)) + t3 * 1));
    uint8_t t7 = (uint8_t)(t6 & ((uint8_t)2ULL));
    uint8_t t8 = (!(t7 == ((uint8_t)0ULL)));
    F.ret = t8;
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
inline __attribute__((aligned(4))) void * gm_f_hoist_prefix_r1mevpl(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_hoist_prefix_r1mevpl F;
    void * gm_l0; /* e */
    GM_STACK_CHECK(&F, 68u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t3 = gm_l0;
        uint8_t t4 = gm_f_hoists_13k2a2s(t3);
        uint8_t t5 = (uint8_t)!t4;
        if (!t5) goto ie2;
        {
            uint8_t t7 = 0;
            F.ret = ((void *)0);
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t8 = gm_l0;
        void * t9 = gm_l0;
        void * t10 = gm_f_prefix_operand_3n5jchh(t9);
        void * t11 = gm_f_push_down_hge098p((void *)NULL, cx, NULL, t8, t10);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t12 = 0;
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
inline __attribute__((aligned(4))) void * gm_f_index_uh6hfri(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2, gm_tl874t2atnlh3 a3) {
    struct gm_fr_index_uh6hfri F;
    void * gm_l0; /* cx */
    void * gm_l1; /* base */
    void * gm_l2; /* i */
    gm_tl874t2atnlh3 gm_l3; /* span */
    void * gm_l4; /* b */
    GM_STACK_CHECK(&F, 137u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        void * t2 = gm_l0;
        void * t3 = gm_l1;
        gm_tkn42u7na3t81 t4;
        memset(&t4, 0, sizeof t4);
        void * t5 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t2, t3, t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l4 = t5;
    }
    {
        void * t6 = gm_l0;
        void * t7 = gm_l4;
        void * t8 = gm_l2;
        gm_tl874t2atnlh3 t9 = gm_l3;
        void * t10 = gm_f_index_value_ufcur8j((void *)NULL, cx, NULL, t6, t7, t8, t9);
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_error_8k27gd1(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_error_8k27gd1 F;
    void * gm_l0; /* t */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    GM_STACK_CHECK_SHALLOW(&F, 83u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    void * t1 = gm_l0;
    gm_t88rcucg1b7uk t2 = gm_l1;
    gm_t53at80pfdus3 t3;
    gm_f_kind_jcnt4hm((void *)&t3, t1, GM_CONV(gm_csguankko4lmhd, t2));
    uint8_t t4 = ((GM_LD(uint8_t, ((uint8_t *)&t3) + 0) == ((uint8_t)26ULL)));
    F.ret = t4;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_vector_gkq6b88(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_vector_gkq6b88 F;
    void * gm_l0; /* t */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    GM_STACK_CHECK_SHALLOW(&F, 51u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    void * t1 = gm_l0;
    gm_t88rcucg1b7uk t2 = gm_l1;
    gm_tb7u19loo3j87 t3;
    gm_f_vector_parts_crrfopm((void *)&t3, t1, GM_CONV(gm_csguankko4lmhd, t2));
    uint8_t t4;
    if (((((uint8_t *)&t3))[16] != 0)) goto mn6;
    t4 = ((uint8_t)0);
    goto me5;
mn6:;
    t4 = ((uint8_t)1);
    goto me5;
mn7:;
    gm_trap("no match arm matched the scrutinee", gm_srck5orfpjti60c, 25, 57);
me5:;
    F.ret = t4;
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
inline __attribute__((aligned(4))) void * gm_f_join_branch_anseitf(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2) {
    struct gm_fr_join_branch_anseitf F;
    void * gm_l0; /* cx */
    void * gm_l1; /* b */
    gm_t88rcucg1b7uk gm_l2; /* ty */
    GM_STACK_CHECK(&F, 159u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t3 = gm_l0;
        void * t4 = GM_LD(void *, (((uint8_t *)t3) + 0));
        void * t5 = (void *)(((uint8_t *)t4) + 8);
        gm_t88rcucg1b7uk t6 = gm_l2;
        gm_t53at80pfdus3 t7;
        gm_f_kind_jcnt4hm((void *)&t7, t5, GM_CONV(gm_csguankko4lmhd, t6));
        uint8_t t8 = ((GM_LD(uint8_t, ((uint8_t *)&t7) + 0) == ((uint8_t)5ULL)));
        if (!t8) goto ie2;
        {
            void * t10 = gm_l1;
            uint8_t t11 = 0;
            F.ret = t10;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t12 = gm_l0;
        void * t13 = gm_l1;
        gm_t88rcucg1b7uk t14 = gm_l2;
        void * t15 = gm_f_coerce_sudc6c1((void *)NULL, cx, NULL, t12, t13, t14);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t16 = 0;
        F.ret = t15;
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
inline __attribute__((aligned(4))) void gm_f_list_58nr42v_3(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_58nr42v_3 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_3v2gjd4_3();
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
inline __attribute__((aligned(4))) void gm_f_list_5kthfl8(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_5kthfl8 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_b8btuen();
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
inline __attribute__((aligned(4))) void gm_f_list_ir0h3tc_2(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_ir0h3tc_2 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_vj3ckdj_2();
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
inline __attribute__((aligned(4))) void gm_f_list_nof7vfj(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_nof7vfj F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_rcd2jln();
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
inline __attribute__((aligned(4))) void gm_f_list_s1kpkpr(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_s1kpkpr F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_fv6i5s8();
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
inline __attribute__((aligned(4))) void gm_f_list_s83jn2q(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_s83jn2q F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_s1lbe5s();
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
inline __attribute__((aligned(4))) void gm_f_list_uahmact(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_uahmact F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_l8m0eod();
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
inline __attribute__((aligned(4))) void gm_f_list_v5d1o9b_2(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_v5d1o9b_2 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_lmbdkhb_2();
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
inline __attribute__((aligned(4))) void gm_f_list_vp1djv1_2(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_vp1djv1_2 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_g3tddrb_2();
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_many_ptr_l4v11f8(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, gm_t88rcucg1b7uk a2) {
    struct gm_fr_many_ptr_l4v11f8 F;
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
    GM_ST(uint8_t, ((uint8_t *)&t5 + 0), ((uint8_t)9ULL));
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
inline __attribute__((aligned(4))) void * gm_f_mk_9bc97fd(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0, const void * a1, gm_tl874t2atnlh3 a2) {
    struct gm_fr_mk_9bc97fd F;
    gm_t88rcucg1b7uk gm_l0; /* ty */
    gm_tl874t2atnlh3 gm_l2; /* span */
    GM_STACK_CHECK(&F, 160u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&F.l1, a1, sizeof F.l1);
    gm_l2 = a2;
    gm_t69m56meqefs8 t1;
    memset((uint8_t *)&t1 + 4, 0, 4);
    memset((uint8_t *)&t1 + 84, 0, 4);
    gm_t88rcucg1b7uk t2 = gm_l0;
    GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t1 + 0, t2);
    memcpy((uint8_t *)&t1 + 8, ((uint8_t *)&F.l1), 64);
    gm_tl874t2atnlh3 t3 = gm_l2;
    GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t1 + 72, t3);
    gm_tq7s98ihonkju t4 = gm_f_new_ve8bb01_2((void *)NULL, cx, NULL, &t1, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_k3tttsr3hp1)));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    if (((((uint8_t *)&t4))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t4) + 8, gm_srcod98slk3vnra, 558, 81);
    void * t5 = GM_LD(void *, (((uint8_t *)&t4) + 8));
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
inline __attribute__((aligned(4))) void * gm_f_mk_pat_hr2l6c4(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0, const void * a1, gm_tl874t2atnlh3 a2) {
    struct gm_fr_mk_pat_hr2l6c4 F;
    gm_t88rcucg1b7uk gm_l0; /* ty */
    gm_tl874t2atnlh3 gm_l2; /* span */
    GM_STACK_CHECK(&F, 208u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&F.l1, a1, sizeof F.l1);
    gm_l2 = a2;
    gm_tc9qv69frb08e t1;
    memset((uint8_t *)&t1 + 4, 0, 4);
    memset((uint8_t *)&t1 + 132, 0, 4);
    gm_t88rcucg1b7uk t2 = gm_l0;
    GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t1 + 0, t2);
    memcpy((uint8_t *)&t1 + 8, ((uint8_t *)&F.l1), 112);
    gm_tl874t2atnlh3 t3 = gm_l2;
    GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t1 + 120, t3);
    gm_tq7s98ihonkju t4 = gm_f_new_1b2khgi((void *)NULL, cx, NULL, &t1, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_k652m2ksgeg)));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    if (((((uint8_t *)&t4))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t4) + 8, gm_srcod98slk3vnra, 311, 83);
    void * t5 = GM_LD(void *, (((uint8_t *)&t4) + 8));
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_never_ty_b949mgo(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_never_ty_b949mgo F;
    void * gm_l0; /* t */
    GM_STACK_CHECK(&F, 84u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    gm_t53at80pfdus3 t2;
    memset(&t2, 0, sizeof t2);
    GM_ST(uint8_t, ((uint8_t *)&t2 + 0), ((uint8_t)5ULL));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_one_expr_q6kn0hn(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_one_expr_q6kn0hn F;
    void * gm_l0; /* e */
    GM_STACK_CHECK(&F, 75u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_vp1djv1_2((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        void * t3 = gm_l0;
        gm_tftb1odc739t5 t4 = gm_f_push_d943vgp_2(t2, &(*(gm_t69m56meqefs8 *)((uint8_t *)t3)));
        if (((((uint8_t *)&t4))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t4) + 1, gm_srcg82mujgottmf, 140, 5);
        gm_b0_1 t5 = GM_LD(gm_b0_1, (((uint8_t *)&t4) + 1));
    }
    {
        gm_b16_8 t6 = GM_LD(gm_b16_8, (((uint8_t *)&F.l1) + 0));
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_optional_rh6okj3(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_optional_rh6okj3 F;
    void * gm_l0; /* t */
    gm_t88rcucg1b7uk gm_l1; /* inner */
    GM_STACK_CHECK(&F, 92u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    gm_t88rcucg1b7uk t2 = gm_l1;
    gm_t53at80pfdus3 t3;
    memset(&t3, 0, sizeof t3);
    GM_ST(uint8_t, ((uint8_t *)&t3 + 0), ((uint8_t)13ULL));
    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t3 + 8), t2);
    gm_t88rcucg1b7uk t4 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t1, &t3);
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
inline __attribute__((aligned(4))) gm_tnnjeerolos0a gm_f_param_kind_a0aikmb(void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_param_kind_a0aikmb F;
    void * gm_l0; /* s */
    uint32_t gm_l1; /* p */
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
    uint64_t t5 = GM_LD(uintptr_t, ((((uint8_t *)t4) + 1128) + 0) + 8);
    if (t3 >= (uintptr_t)(t5)) gm_trap_index(t3, t5, gm_srckg5f2c0p31ur, 249, 62);
    gm_tnnjeerolos0a t6 = GM_LD(gm_tnnjeerolos0a, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t4) + 1128) + 0)) + t3 * 48) + 0));
    F.ret = t6;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_t9805qtonbsag gm_f_pop_570g79n(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_pop_570g79n F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* n */
    uint32_t gm_l2; /* x */
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
            gm_t9805qtonbsag t8;
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
        if (o12) gm_trap("integer overflow in `-`", gm_srcrgc9bkquf6nr, 133, 18);
        uint64_t t13 = r11;
        uint64_t t14 = (uintptr_t)t13;
        void * t15 = gm_l0;
        uint64_t t16 = GM_LD(uintptr_t, (((uint8_t *)t15) + 0) + 8);
        if (t14 >= (uintptr_t)(t16)) gm_trap_index(t14, t16, gm_srcrgc9bkquf6nr, 133, 10);
        uint32_t t17 = GM_LD(uint32_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)t15) + 0)) + t14 * 4));
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
        if (o25) gm_trap("integer overflow in `-`", gm_srcrgc9bkquf6nr, 134, 26);
        uint64_t t26 = r24;
        uint64_t t27 = (uintptr_t)t26;
        if (t22 > t27 || t27 > (uintptr_t)t20) gm_trap_slice(t22, t27, t20, gm_srcrgc9bkquf6nr, 134, 15);
        gm_b16_8 t28;
        GM_ST(void *, (uint8_t *)&t28, (void *)(((uint8_t *)t21 + t22 * 4)));
        GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(t27 - t22));
        GM_ST(gm_b16_8, (((uint8_t *)t18) + 0), t28);
    }
    {
        uint32_t t29 = gm_l2;
        gm_t9805qtonbsag t30;
        GM_ST(uint8_t, (uint8_t *)&t30 + 5, 0);
        GM_ST(uint16_t, (uint8_t *)&t30 + 6, 0);
        GM_ST(uint32_t, ((uint8_t *)&t30), t29);
        ((uint8_t *)&t30)[4] = 1;
        uint8_t t31 = 0;
        F.ret = t30;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_pop_8sodrk9(void *gm_out, void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_pop_8sodrk9 F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* n */
    gm_t66j1im9ip128 gm_l2; /* x */
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
            gm_td8hfgklo8dil t8;
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
        if (o12) gm_trap("integer overflow in `-`", gm_srcrgc9bkquf6nr, 133, 18);
        uint64_t t13 = r11;
        uint64_t t14 = (uintptr_t)t13;
        void * t15 = gm_l0;
        uint64_t t16 = GM_LD(uintptr_t, (((uint8_t *)t15) + 0) + 8);
        if (t14 >= (uintptr_t)(t16)) gm_trap_index(t14, t16, gm_srcrgc9bkquf6nr, 133, 10);
        gm_t66j1im9ip128 t17 = GM_LD(gm_t66j1im9ip128, ((uint8_t *)GM_LD(void *, (((uint8_t *)t15) + 0)) + t14 * 32));
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
        if (o25) gm_trap("integer overflow in `-`", gm_srcrgc9bkquf6nr, 134, 26);
        uint64_t t26 = r24;
        uint64_t t27 = (uintptr_t)t26;
        if (t22 > t27 || t27 > (uintptr_t)t20) gm_trap_slice(t22, t27, t20, gm_srcrgc9bkquf6nr, 134, 15);
        gm_b16_8 t28;
        GM_ST(void *, (uint8_t *)&t28, (void *)(((uint8_t *)t21 + t22 * 32)));
        GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(t27 - t22));
        GM_ST(gm_b16_8, (((uint8_t *)t18) + 0), t28);
    }
    {
        const uint8_t *t29 = ((uint8_t *)&gm_l2);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_td8hfgklo8dil *)gm_out) + 33, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_td8hfgklo8dil *)gm_out) + 34, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_td8hfgklo8dil *)gm_out) + 36, 0);
        ((uint8_t *)&(*(gm_td8hfgklo8dil *)gm_out))[32] = 1;
        memmove((uint8_t *)&(*(gm_td8hfgklo8dil *)gm_out) + 0, t29, 32);
        goto gm_fdone;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_pop_dt2bd2s(void *gm_out, void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_pop_dt2bd2s F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* n */
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
            gm_tqetrgo0q24b5 t8;
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
        if (o12) gm_trap("integer overflow in `-`", gm_srcrgc9bkquf6nr, 133, 18);
        uint64_t t13 = r11;
        uint64_t t14 = (uintptr_t)t13;
        void * t15 = gm_l0;
        uint64_t t16 = GM_LD(uintptr_t, (((uint8_t *)t15) + 0) + 8);
        if (t14 >= (uintptr_t)(t16)) gm_trap_index(t14, t16, gm_srcrgc9bkquf6nr, 133, 10);
        gm_trqfvmjtt4sij t17 = GM_LD(gm_trqfvmjtt4sij, ((uint8_t *)GM_LD(void *, (((uint8_t *)t15) + 0)) + t14 * 40));
        F.l2 = t17;
    }
    {
        void * t18 = gm_l0;
        void * t19 = gm_l0;
        uint64_t t20 = GM_LD(uintptr_t, (((uint8_t *)t19) + 0) + 8);
        void * t21 = ((uint8_t *)GM_LD(void *, (((uint8_t *)t19) + 0)));
        uint64_t t22 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t23 = gm_l1;
        uint64_t r24; bool o25 = __builtin_sub_overflow(t23, ((uint64_t)1ULL), &r24);
        if (o25) gm_trap("integer overflow in `-`", gm_srcrgc9bkquf6nr, 134, 26);
        uint64_t t26 = r24;
        uint64_t t27 = (uintptr_t)t26;
        if (t22 > t27 || t27 > (uintptr_t)t20) gm_trap_slice(t22, t27, t20, gm_srcrgc9bkquf6nr, 134, 15);
        gm_b16_8 t28;
        GM_ST(void *, (uint8_t *)&t28, (void *)(((uint8_t *)t21 + t22 * 40)));
        GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(t27 - t22));
        GM_ST(gm_b16_8, (((uint8_t *)t18) + 0), t28);
    }
    {
        const uint8_t *t29 = ((uint8_t *)&F.l2);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tqetrgo0q24b5 *)gm_out) + 41, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tqetrgo0q24b5 *)gm_out) + 42, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tqetrgo0q24b5 *)gm_out) + 44, 0);
        ((uint8_t *)&(*(gm_tqetrgo0q24b5 *)gm_out))[40] = 1;
        memmove((uint8_t *)&(*(gm_tqetrgo0q24b5 *)gm_out) + 0, t29, 40);
        goto gm_fdone;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_pop_scope_g5j7o4e(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_pop_scope_g5j7o4e F;
    void * gm_l0; /* cx */
    gm_t66j1im9ip128 gm_l1; /* m */
    GM_STACK_CHECK_SHALLOW(&F, 200u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        {
            void * t3 = gm_l0;
            void * t4 = (void *)(((uint8_t *)t3) + 56);
            gm_tqetrgo0q24b5 t5;
            gm_f_pop_dt2bd2s((void *)&t5, t4);
        }
    }
    {
        {
            void * t7 = gm_l0;
            void * t8 = (void *)(((uint8_t *)t7) + 616);
            gm_t9805qtonbsag t9 = gm_f_pop_570g79n(t8);
        }
    }
    {
        void * t11 = gm_l0;
        void * t12 = (void *)(((uint8_t *)t11) + 656);
        gm_td8hfgklo8dil t13;
        gm_f_pop_8sodrk9((void *)&t13, t12);
        if (!((((uint8_t *)&t13))[32] != 0)) goto ie10;
        gm_l1 = (*(gm_t66j1im9ip128 *)(((uint8_t *)&t13) + 0));
        {
            void * t15 = gm_l0;
            void * t16 = GM_LD(void *, (((uint8_t *)t15) + 0));
            gm_f_restore_scope_edqq1v8(t16, &(*(gm_t66j1im9ip128 *)((uint8_t *)&gm_l1)));
        }
        goto ix10;
ie10:;
ix10:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_ptr_t1h2m2d(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, gm_t88rcucg1b7uk a2) {
    struct gm_fr_ptr_t1h2m2d F;
    void * gm_l0; /* t */
    uint8_t gm_l1; /* is_const */
    gm_t88rcucg1b7uk gm_l2; /* pointee */
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
    GM_ST(uint8_t, ((uint8_t *)&t5 + 0), ((uint8_t)8ULL));
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_1voul2a(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_1voul2a F;
    void * gm_l0; /* l */
    gm_b16_8 gm_l1; /* x */
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
        uint8_t t8 = ((t6 == t7));
        if (!t8) goto ie4;
        {
            void * t10 = gm_l0;
            gm_b16_8 t11 = gm_f_empty_slice_l8m0eod();
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
            gm_tftb1odc739t5 t13 = gm_f_regrow_021udu6(t10, ((uint64_t)1ULL), t12);
            if (((((uint8_t *)&t13))[0] != 0)) {
                gm_t9qkh0rsooc8d t14 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&t13) + 1));
                gm_tftb1odc739t5 t15;
                ((uint8_t *)&t15)[0] = 1;
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t15 + 1), t14);
                F.ret = t15;
                goto gm_fret;
            }
            gm_b0_1 t16 = GM_LD(gm_b0_1, (((uint8_t *)&t13) + 1));
        }
        goto ix4;
ie4:;
ix4:;
    }
    {
        uint64_t t17 = gm_l2;
        uint64_t t18 = (uintptr_t)t17;
        void * t19 = gm_l0;
        void * t20 = GM_LD(void *, (((uint8_t *)t19) + 0));
        gm_b16_8 t21 = gm_l1;
        GM_ST(gm_b16_8, ((uint8_t *)t20 + t18 * 16), t21);
    }
    {
        void * t22 = gm_l0;
        void * t23 = gm_l0;
        void * t24 = GM_LD(void *, (((uint8_t *)t23) + 0));
        void * t25 = ((uint8_t *)t24);
        uint64_t t26 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t27 = gm_l2;
        uint64_t r28; bool o29 = __builtin_add_overflow(t27, ((uint64_t)1ULL), &r28);
        uint64_t t30 = r28;
        uint64_t t31 = (uintptr_t)t30;
        gm_b16_8 t32;
        GM_ST(void *, (uint8_t *)&t32, (void *)(((uint8_t *)t25 + t26 * 16)));
        GM_ST(uintptr_t, (uint8_t *)&t32 + 8, (uintptr_t)(t31 - t26));
        GM_ST(gm_b16_8, (((uint8_t *)t22) + 0), t32);
    }
    {
        gm_b0_1 t33;
        gm_tftb1odc739t5 t34;
        GM_ST(uint8_t, (uint8_t *)&t34 + 1, 0);
        ((uint8_t *)&t34)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t34 + 1), t33);
        uint8_t t35 = ((((uint8_t *)&t34))[0] != 0);
        F.ret = t34;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_3ef746b(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_3ef746b F;
    void * gm_l0; /* l */
    gm_b16_8 gm_l1; /* x */
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
        uint8_t t8 = ((t6 == t7));
        if (!t8) goto ie4;
        {
            void * t10 = gm_l0;
            gm_b16_8 t11 = gm_f_empty_slice_5nn7ph0();
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
            gm_tftb1odc739t5 t13 = gm_f_regrow_0jmnnpk(t10, ((uint64_t)1ULL), t12);
            if (((((uint8_t *)&t13))[0] != 0)) {
                gm_t9qkh0rsooc8d t14 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&t13) + 1));
                gm_tftb1odc739t5 t15;
                ((uint8_t *)&t15)[0] = 1;
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t15 + 1), t14);
                F.ret = t15;
                goto gm_fret;
            }
            gm_b0_1 t16 = GM_LD(gm_b0_1, (((uint8_t *)&t13) + 1));
        }
        goto ix4;
ie4:;
ix4:;
    }
    {
        uint64_t t17 = gm_l2;
        uint64_t t18 = (uintptr_t)t17;
        void * t19 = gm_l0;
        void * t20 = GM_LD(void *, (((uint8_t *)t19) + 0));
        gm_b16_8 t21 = gm_l1;
        GM_ST(gm_b16_8, ((uint8_t *)t20 + t18 * 16), t21);
    }
    {
        void * t22 = gm_l0;
        void * t23 = gm_l0;
        void * t24 = GM_LD(void *, (((uint8_t *)t23) + 0));
        void * t25 = ((uint8_t *)t24);
        uint64_t t26 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t27 = gm_l2;
        uint64_t r28; bool o29 = __builtin_add_overflow(t27, ((uint64_t)1ULL), &r28);
        uint64_t t30 = r28;
        uint64_t t31 = (uintptr_t)t30;
        gm_b16_8 t32;
        GM_ST(void *, (uint8_t *)&t32, (void *)(((uint8_t *)t25 + t26 * 16)));
        GM_ST(uintptr_t, (uint8_t *)&t32 + 8, (uintptr_t)(t31 - t26));
        GM_ST(gm_b16_8, (((uint8_t *)t22) + 0), t32);
    }
    {
        gm_b0_1 t33;
        gm_tftb1odc739t5 t34;
        GM_ST(uint8_t, (uint8_t *)&t34 + 1, 0);
        ((uint8_t *)&t34)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t34 + 1), t33);
        uint8_t t35 = ((((uint8_t *)&t34))[0] != 0);
        F.ret = t34;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_7koe9lk(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_7koe9lk F;
    void * gm_l0; /* l */
    uint64_t gm_l2; /* n */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&F.l1, a1, sizeof F.l1);
    {
        void * t2 = gm_l0;
        uint64_t t3 = GM_LD(uintptr_t, (((uint8_t *)t2) + 0) + 8);
        gm_l2 = t3;
    }
    {
        void * t5 = gm_l0;
        uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)t5) + 16));
        uint64_t t7 = gm_l2;
        uint8_t t8 = ((t6 == t7));
        if (!t8) goto ie4;
        {
            void * t10 = gm_l0;
            gm_b16_8 t11 = gm_f_empty_slice_qm5tp1g();
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
            gm_tftb1odc739t5 t13 = gm_f_regrow_emnjbt3(t10, ((uint64_t)1ULL), t12);
            if (((((uint8_t *)&t13))[0] != 0)) {
                gm_t9qkh0rsooc8d t14 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&t13) + 1));
                gm_tftb1odc739t5 t15;
                ((uint8_t *)&t15)[0] = 1;
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t15 + 1), t14);
                F.ret = t15;
                goto gm_fret;
            }
            gm_b0_1 t16 = GM_LD(gm_b0_1, (((uint8_t *)&t13) + 1));
        }
        goto ix4;
ie4:;
ix4:;
    }
    {
        uint64_t t17 = gm_l2;
        uint64_t t18 = (uintptr_t)t17;
        void * t19 = gm_l0;
        void * t20 = GM_LD(void *, (((uint8_t *)t19) + 0));
        gm_tpm220j5qhjke t21 = F.l1;
        GM_ST(gm_tpm220j5qhjke, ((uint8_t *)t20 + t18 * 48), t21);
    }
    {
        void * t22 = gm_l0;
        void * t23 = gm_l0;
        void * t24 = GM_LD(void *, (((uint8_t *)t23) + 0));
        void * t25 = ((uint8_t *)t24);
        uint64_t t26 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t27 = gm_l2;
        uint64_t r28; bool o29 = __builtin_add_overflow(t27, ((uint64_t)1ULL), &r28);
        uint64_t t30 = r28;
        uint64_t t31 = (uintptr_t)t30;
        gm_b16_8 t32;
        GM_ST(void *, (uint8_t *)&t32, (void *)(((uint8_t *)t25 + t26 * 48)));
        GM_ST(uintptr_t, (uint8_t *)&t32 + 8, (uintptr_t)(t31 - t26));
        GM_ST(gm_b16_8, (((uint8_t *)t22) + 0), t32);
    }
    {
        gm_b0_1 t33;
        gm_tftb1odc739t5 t34;
        GM_ST(uint8_t, (uint8_t *)&t34 + 1, 0);
        ((uint8_t *)&t34)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t34 + 1), t33);
        uint8_t t35 = ((((uint8_t *)&t34))[0] != 0);
        F.ret = t34;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_9a0n4ld(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_9a0n4ld F;
    void * gm_l0; /* l */
    uint64_t gm_l2; /* n */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        uint64_t t3 = GM_LD(uintptr_t, (((uint8_t *)t2) + 0) + 8);
        gm_l2 = t3;
    }
    {
        void * t5 = gm_l0;
        uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)t5) + 16));
        uint64_t t7 = gm_l2;
        uint8_t t8 = ((t6 == t7));
        if (!t8) goto ie4;
        {
            void * t10 = gm_l0;
            gm_b16_8 t11 = gm_f_empty_slice_b8btuen();
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
            gm_tftb1odc739t5 t13 = gm_f_regrow_52486fh(t10, ((uint64_t)1ULL), t12);
            if (((((uint8_t *)&t13))[0] != 0)) {
                gm_t9qkh0rsooc8d t14 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&t13) + 1));
                gm_tftb1odc739t5 t15;
                ((uint8_t *)&t15)[0] = 1;
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t15 + 1), t14);
                F.ret = t15;
                goto gm_fret;
            }
            gm_b0_1 t16 = GM_LD(gm_b0_1, (((uint8_t *)&t13) + 1));
        }
        goto ix4;
ie4:;
ix4:;
    }
    {
        uint64_t t17 = gm_l2;
        uint64_t t18 = (uintptr_t)t17;
        void * t19 = gm_l0;
        void * t20 = GM_LD(void *, (((uint8_t *)t19) + 0));
        gm_tm7a6rksvs6n5 t21 = GM_LD(gm_tm7a6rksvs6n5, ((uint8_t *)a1));
        GM_ST(gm_tm7a6rksvs6n5, ((uint8_t *)t20 + t18 * 48), t21);
    }
    {
        void * t22 = gm_l0;
        void * t23 = gm_l0;
        void * t24 = GM_LD(void *, (((uint8_t *)t23) + 0));
        void * t25 = ((uint8_t *)t24);
        uint64_t t26 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t27 = gm_l2;
        uint64_t r28; bool o29 = __builtin_add_overflow(t27, ((uint64_t)1ULL), &r28);
        uint64_t t30 = r28;
        uint64_t t31 = (uintptr_t)t30;
        gm_b16_8 t32;
        GM_ST(void *, (uint8_t *)&t32, (void *)(((uint8_t *)t25 + t26 * 48)));
        GM_ST(uintptr_t, (uint8_t *)&t32 + 8, (uintptr_t)(t31 - t26));
        GM_ST(gm_b16_8, (((uint8_t *)t22) + 0), t32);
    }
    {
        gm_b0_1 t33;
        gm_tftb1odc739t5 t34;
        GM_ST(uint8_t, (uint8_t *)&t34 + 1, 0);
        ((uint8_t *)&t34)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t34 + 1), t33);
        uint8_t t35 = ((((uint8_t *)&t34))[0] != 0);
        F.ret = t34;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_bpovr2k_3(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_bpovr2k_3 F;
    void * gm_l0; /* l */
    gm_tbbqefuhv2r8h gm_l1; /* x */
    uint64_t gm_l2; /* n */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        uint64_t t3 = GM_LD(uintptr_t, (((uint8_t *)t2) + 0) + 8);
        gm_l2 = t3;
    }
    {
        void * t5 = gm_l0;
        uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)t5) + 16));
        uint64_t t7 = gm_l2;
        uint8_t t8 = ((t6 == t7));
        if (!t8) goto ie4;
        {
            void * t10 = gm_l0;
            gm_b16_8 t11 = gm_f_empty_slice_3v2gjd4_3();
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
            gm_tftb1odc739t5 t13 = gm_f_regrow_h9tbucc_3(t10, ((uint64_t)1ULL), t12);
            if (((((uint8_t *)&t13))[0] != 0)) {
                gm_t9qkh0rsooc8d t14 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&t13) + 1));
                gm_tftb1odc739t5 t15;
                ((uint8_t *)&t15)[0] = 1;
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t15 + 1), t14);
                F.ret = t15;
                goto gm_fret;
            }
            gm_b0_1 t16 = GM_LD(gm_b0_1, (((uint8_t *)&t13) + 1));
        }
        goto ix4;
ie4:;
ix4:;
    }
    {
        uint64_t t17 = gm_l2;
        uint64_t t18 = (uintptr_t)t17;
        void * t19 = gm_l0;
        void * t20 = GM_LD(void *, (((uint8_t *)t19) + 0));
        gm_tbbqefuhv2r8h t21 = GM_LD(gm_tbbqefuhv2r8h, ((uint8_t *)a1));
        GM_ST(gm_tbbqefuhv2r8h, ((uint8_t *)t20 + t18 * 24), t21);
    }
    {
        void * t22 = gm_l0;
        void * t23 = gm_l0;
        void * t24 = GM_LD(void *, (((uint8_t *)t23) + 0));
        void * t25 = ((uint8_t *)t24);
        uint64_t t26 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t27 = gm_l2;
        uint64_t r28; bool o29 = __builtin_add_overflow(t27, ((uint64_t)1ULL), &r28);
        uint64_t t30 = r28;
        uint64_t t31 = (uintptr_t)t30;
        gm_b16_8 t32;
        GM_ST(void *, (uint8_t *)&t32, (void *)(((uint8_t *)t25 + t26 * 24)));
        GM_ST(uintptr_t, (uint8_t *)&t32 + 8, (uintptr_t)(t31 - t26));
        GM_ST(gm_b16_8, (((uint8_t *)t22) + 0), t32);
    }
    {
        gm_b0_1 t33;
        gm_tftb1odc739t5 t34;
        GM_ST(uint8_t, (uint8_t *)&t34 + 1, 0);
        ((uint8_t *)&t34)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t34 + 1), t33);
        uint8_t t35 = ((((uint8_t *)&t34))[0] != 0);
        F.ret = t34;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_d6c76ud_2(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_d6c76ud_2 F;
    void * gm_l0; /* l */
    gm_trcmmd12cefb1 gm_l1; /* x */
    uint64_t gm_l2; /* n */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&gm_l1, a1, sizeof gm_l1);
    {
        void * t2 = gm_l0;
        uint64_t t3 = GM_LD(uintptr_t, (((uint8_t *)t2) + 0) + 8);
        gm_l2 = t3;
    }
    {
        void * t5 = gm_l0;
        uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)t5) + 16));
        uint64_t t7 = gm_l2;
        uint8_t t8 = ((t6 == t7));
        if (!t8) goto ie4;
        {
            void * t10 = gm_l0;
            gm_b16_8 t11 = gm_f_empty_slice_vj3ckdj_2();
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
            gm_tftb1odc739t5 t13 = gm_f_regrow_h08har5_2(t10, ((uint64_t)1ULL), t12);
            if (((((uint8_t *)&t13))[0] != 0)) {
                gm_t9qkh0rsooc8d t14 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&t13) + 1));
                gm_tftb1odc739t5 t15;
                ((uint8_t *)&t15)[0] = 1;
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t15 + 1), t14);
                F.ret = t15;
                goto gm_fret;
            }
            gm_b0_1 t16 = GM_LD(gm_b0_1, (((uint8_t *)&t13) + 1));
        }
        goto ix4;
ie4:;
ix4:;
    }
    {
        uint64_t t17 = gm_l2;
        uint64_t t18 = (uintptr_t)t17;
        void * t19 = gm_l0;
        void * t20 = GM_LD(void *, (((uint8_t *)t19) + 0));
        gm_trcmmd12cefb1 t21 = gm_l1;
        GM_ST(gm_trcmmd12cefb1, ((uint8_t *)t20 + t18 * 32), t21);
    }
    {
        void * t22 = gm_l0;
        void * t23 = gm_l0;
        void * t24 = GM_LD(void *, (((uint8_t *)t23) + 0));
        void * t25 = ((uint8_t *)t24);
        uint64_t t26 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t27 = gm_l2;
        uint64_t r28; bool o29 = __builtin_add_overflow(t27, ((uint64_t)1ULL), &r28);
        uint64_t t30 = r28;
        uint64_t t31 = (uintptr_t)t30;
        gm_b16_8 t32;
        GM_ST(void *, (uint8_t *)&t32, (void *)(((uint8_t *)t25 + t26 * 32)));
        GM_ST(uintptr_t, (uint8_t *)&t32 + 8, (uintptr_t)(t31 - t26));
        GM_ST(gm_b16_8, (((uint8_t *)t22) + 0), t32);
    }
    {
        gm_b0_1 t33;
        gm_tftb1odc739t5 t34;
        GM_ST(uint8_t, (uint8_t *)&t34 + 1, 0);
        ((uint8_t *)&t34)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t34 + 1), t33);
        uint8_t t35 = ((((uint8_t *)&t34))[0] != 0);
        F.ret = t34;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_d943vgp_2(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_d943vgp_2 F;
    void * gm_l0; /* l */
    uint64_t gm_l2; /* n */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&F.l1, a1, sizeof F.l1);
    {
        void * t2 = gm_l0;
        uint64_t t3 = GM_LD(uintptr_t, (((uint8_t *)t2) + 0) + 8);
        gm_l2 = t3;
    }
    {
        void * t5 = gm_l0;
        uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)t5) + 16));
        uint64_t t7 = gm_l2;
        uint8_t t8 = ((t6 == t7));
        if (!t8) goto ie4;
        {
            void * t10 = gm_l0;
            gm_b16_8 t11 = gm_f_empty_slice_g3tddrb_2();
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
            gm_tftb1odc739t5 t13 = gm_f_regrow_61fb705_2(t10, ((uint64_t)1ULL), t12);
            if (((((uint8_t *)&t13))[0] != 0)) {
                gm_t9qkh0rsooc8d t14 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&t13) + 1));
                gm_tftb1odc739t5 t15;
                ((uint8_t *)&t15)[0] = 1;
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t15 + 1), t14);
                F.ret = t15;
                goto gm_fret;
            }
            gm_b0_1 t16 = GM_LD(gm_b0_1, (((uint8_t *)&t13) + 1));
        }
        goto ix4;
ie4:;
ix4:;
    }
    {
        uint64_t t17 = gm_l2;
        uint64_t t18 = (uintptr_t)t17;
        void * t19 = gm_l0;
        void * t20 = GM_LD(void *, (((uint8_t *)t19) + 0));
        gm_t69m56meqefs8 t21 = F.l1;
        GM_ST(gm_t69m56meqefs8, ((uint8_t *)t20 + t18 * 88), t21);
    }
    {
        void * t22 = gm_l0;
        void * t23 = gm_l0;
        void * t24 = GM_LD(void *, (((uint8_t *)t23) + 0));
        void * t25 = ((uint8_t *)t24);
        uint64_t t26 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t27 = gm_l2;
        uint64_t r28; bool o29 = __builtin_add_overflow(t27, ((uint64_t)1ULL), &r28);
        uint64_t t30 = r28;
        uint64_t t31 = (uintptr_t)t30;
        gm_b16_8 t32;
        GM_ST(void *, (uint8_t *)&t32, (void *)(((uint8_t *)t25 + t26 * 88)));
        GM_ST(uintptr_t, (uint8_t *)&t32 + 8, (uintptr_t)(t31 - t26));
        GM_ST(gm_b16_8, (((uint8_t *)t22) + 0), t32);
    }
    {
        gm_b0_1 t33;
        gm_tftb1odc739t5 t34;
        GM_ST(uint8_t, (uint8_t *)&t34 + 1, 0);
        ((uint8_t *)&t34)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t34 + 1), t33);
        uint8_t t35 = ((((uint8_t *)&t34))[0] != 0);
        F.ret = t34;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_eb7kbhp_2(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_eb7kbhp_2 F;
    void * gm_l0; /* l */
    gm_tlr9dp305htm7 gm_l1; /* x */
    uint64_t gm_l2; /* n */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&gm_l1, a1, sizeof gm_l1);
    {
        void * t2 = gm_l0;
        uint64_t t3 = GM_LD(uintptr_t, (((uint8_t *)t2) + 0) + 8);
        gm_l2 = t3;
    }
    {
        void * t5 = gm_l0;
        uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)t5) + 16));
        uint64_t t7 = gm_l2;
        uint8_t t8 = ((t6 == t7));
        if (!t8) goto ie4;
        {
            void * t10 = gm_l0;
            gm_b16_8 t11 = gm_f_empty_slice_lmbdkhb_2();
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
            gm_tftb1odc739t5 t13 = gm_f_regrow_4bta8ne_2(t10, ((uint64_t)1ULL), t12);
            if (((((uint8_t *)&t13))[0] != 0)) {
                gm_t9qkh0rsooc8d t14 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&t13) + 1));
                gm_tftb1odc739t5 t15;
                ((uint8_t *)&t15)[0] = 1;
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t15 + 1), t14);
                F.ret = t15;
                goto gm_fret;
            }
            gm_b0_1 t16 = GM_LD(gm_b0_1, (((uint8_t *)&t13) + 1));
        }
        goto ix4;
ie4:;
ix4:;
    }
    {
        uint64_t t17 = gm_l2;
        uint64_t t18 = (uintptr_t)t17;
        void * t19 = gm_l0;
        void * t20 = GM_LD(void *, (((uint8_t *)t19) + 0));
        gm_tlr9dp305htm7 t21 = gm_l1;
        GM_ST(gm_tlr9dp305htm7, ((uint8_t *)t20 + t18 * 24), t21);
    }
    {
        void * t22 = gm_l0;
        void * t23 = gm_l0;
        void * t24 = GM_LD(void *, (((uint8_t *)t23) + 0));
        void * t25 = ((uint8_t *)t24);
        uint64_t t26 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t27 = gm_l2;
        uint64_t r28; bool o29 = __builtin_add_overflow(t27, ((uint64_t)1ULL), &r28);
        uint64_t t30 = r28;
        uint64_t t31 = (uintptr_t)t30;
        gm_b16_8 t32;
        GM_ST(void *, (uint8_t *)&t32, (void *)(((uint8_t *)t25 + t26 * 24)));
        GM_ST(uintptr_t, (uint8_t *)&t32 + 8, (uintptr_t)(t31 - t26));
        GM_ST(gm_b16_8, (((uint8_t *)t22) + 0), t32);
    }
    {
        gm_b0_1 t33;
        gm_tftb1odc739t5 t34;
        GM_ST(uint8_t, (uint8_t *)&t34 + 1, 0);
        ((uint8_t *)&t34)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t34 + 1), t33);
        uint8_t t35 = ((((uint8_t *)&t34))[0] != 0);
        F.ret = t34;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_k3l9nic(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_k3l9nic F;
    void * gm_l0; /* l */
    gm_t88rcucg1b7uk gm_l1; /* x */
    uint64_t gm_l2; /* n */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    {
        void * t2 = gm_l0;
        uint64_t t3 = GM_LD(uintptr_t, (((uint8_t *)t2) + 0) + 8);
        gm_l2 = t3;
    }
    {
        void * t5 = gm_l0;
        uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)t5) + 16));
        uint64_t t7 = gm_l2;
        uint8_t t8 = ((t6 == t7));
        if (!t8) goto ie4;
        {
            void * t10 = gm_l0;
            gm_b16_8 t11 = gm_f_empty_slice_rcd2jln();
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
            gm_tftb1odc739t5 t13 = gm_f_regrow_j4iu2lk(t10, ((uint64_t)1ULL), t12);
            if (((((uint8_t *)&t13))[0] != 0)) {
                gm_t9qkh0rsooc8d t14 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&t13) + 1));
                gm_tftb1odc739t5 t15;
                ((uint8_t *)&t15)[0] = 1;
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t15 + 1), t14);
                F.ret = t15;
                goto gm_fret;
            }
            gm_b0_1 t16 = GM_LD(gm_b0_1, (((uint8_t *)&t13) + 1));
        }
        goto ix4;
ie4:;
ix4:;
    }
    {
        uint64_t t17 = gm_l2;
        uint64_t t18 = (uintptr_t)t17;
        void * t19 = gm_l0;
        void * t20 = GM_LD(void *, (((uint8_t *)t19) + 0));
        gm_t88rcucg1b7uk t21 = gm_l1;
        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)t20 + t18 * 4), t21);
    }
    {
        void * t22 = gm_l0;
        void * t23 = gm_l0;
        void * t24 = GM_LD(void *, (((uint8_t *)t23) + 0));
        void * t25 = ((uint8_t *)t24);
        uint64_t t26 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t27 = gm_l2;
        uint64_t r28; bool o29 = __builtin_add_overflow(t27, ((uint64_t)1ULL), &r28);
        uint64_t t30 = r28;
        uint64_t t31 = (uintptr_t)t30;
        gm_b16_8 t32;
        GM_ST(void *, (uint8_t *)&t32, (void *)(((uint8_t *)t25 + t26 * 4)));
        GM_ST(uintptr_t, (uint8_t *)&t32 + 8, (uintptr_t)(t31 - t26));
        GM_ST(gm_b16_8, (((uint8_t *)t22) + 0), t32);
    }
    {
        gm_b0_1 t33;
        gm_tftb1odc739t5 t34;
        GM_ST(uint8_t, (uint8_t *)&t34 + 1, 0);
        ((uint8_t *)&t34)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t34 + 1), t33);
        uint8_t t35 = ((((uint8_t *)&t34))[0] != 0);
        F.ret = t34;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_pf02nv6(void * a0, void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_pf02nv6 F;
    void * gm_l0; /* l */
    void * gm_l1; /* x */
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
        uint8_t t8 = ((t6 == t7));
        if (!t8) goto ie4;
        {
            void * t10 = gm_l0;
            gm_b16_8 t11 = gm_f_empty_slice_fv6i5s8();
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
            gm_tftb1odc739t5 t13 = gm_f_regrow_5719f1b(t10, ((uint64_t)1ULL), t12);
            if (((((uint8_t *)&t13))[0] != 0)) {
                gm_t9qkh0rsooc8d t14 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&t13) + 1));
                gm_tftb1odc739t5 t15;
                ((uint8_t *)&t15)[0] = 1;
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t15 + 1), t14);
                F.ret = t15;
                goto gm_fret;
            }
            gm_b0_1 t16 = GM_LD(gm_b0_1, (((uint8_t *)&t13) + 1));
        }
        goto ix4;
ie4:;
ix4:;
    }
    {
        uint64_t t17 = gm_l2;
        uint64_t t18 = (uintptr_t)t17;
        void * t19 = gm_l0;
        void * t20 = GM_LD(void *, (((uint8_t *)t19) + 0));
        void * t21 = gm_l1;
        GM_ST(void *, ((uint8_t *)t20 + t18 * 8), t21);
    }
    {
        void * t22 = gm_l0;
        void * t23 = gm_l0;
        void * t24 = GM_LD(void *, (((uint8_t *)t23) + 0));
        void * t25 = ((uint8_t *)t24);
        uint64_t t26 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t27 = gm_l2;
        uint64_t r28; bool o29 = __builtin_add_overflow(t27, ((uint64_t)1ULL), &r28);
        uint64_t t30 = r28;
        uint64_t t31 = (uintptr_t)t30;
        gm_b16_8 t32;
        GM_ST(void *, (uint8_t *)&t32, (void *)(((uint8_t *)t25 + t26 * 8)));
        GM_ST(uintptr_t, (uint8_t *)&t32 + 8, (uintptr_t)(t31 - t26));
        GM_ST(gm_b16_8, (((uint8_t *)t22) + 0), t32);
    }
    {
        gm_b0_1 t33;
        gm_tftb1odc739t5 t34;
        GM_ST(uint8_t, (uint8_t *)&t34 + 1, 0);
        ((uint8_t *)&t34)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t34 + 1), t33);
        uint8_t t35 = ((((uint8_t *)&t34))[0] != 0);
        F.ret = t34;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_vd1290p(void * a0, uint8_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_vd1290p F;
    void * gm_l0; /* l */
    uint8_t gm_l1; /* x */
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
        uint8_t t8 = ((t6 == t7));
        if (!t8) goto ie4;
        {
            void * t10 = gm_l0;
            gm_b16_8 t11 = gm_f_empty_slice_s1lbe5s();
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
            gm_tftb1odc739t5 t13 = gm_f_regrow_hkmvuvc(t10, ((uint64_t)1ULL), t12);
            if (((((uint8_t *)&t13))[0] != 0)) {
                gm_t9qkh0rsooc8d t14 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&t13) + 1));
                gm_tftb1odc739t5 t15;
                ((uint8_t *)&t15)[0] = 1;
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t15 + 1), t14);
                F.ret = t15;
                goto gm_fret;
            }
            gm_b0_1 t16 = GM_LD(gm_b0_1, (((uint8_t *)&t13) + 1));
        }
        goto ix4;
ie4:;
ix4:;
    }
    {
        uint64_t t17 = gm_l2;
        uint64_t t18 = (uintptr_t)t17;
        void * t19 = gm_l0;
        void * t20 = GM_LD(void *, (((uint8_t *)t19) + 0));
        uint8_t t21 = gm_l1;
        GM_ST(uint8_t, ((uint8_t *)t20 + t18 * 1), t21);
    }
    {
        void * t22 = gm_l0;
        void * t23 = gm_l0;
        void * t24 = GM_LD(void *, (((uint8_t *)t23) + 0));
        void * t25 = ((uint8_t *)t24);
        uint64_t t26 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t27 = gm_l2;
        uint64_t r28; bool o29 = __builtin_add_overflow(t27, ((uint64_t)1ULL), &r28);
        uint64_t t30 = r28;
        uint64_t t31 = (uintptr_t)t30;
        gm_b16_8 t32;
        GM_ST(void *, (uint8_t *)&t32, (void *)(((uint8_t *)t25 + t26 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t32 + 8, (uintptr_t)(t31 - t26));
        GM_ST(gm_b16_8, (((uint8_t *)t22) + 0), t32);
    }
    {
        gm_b0_1 t33;
        gm_tftb1odc739t5 t34;
        GM_ST(uint8_t, (uint8_t *)&t34 + 1, 0);
        ((uint8_t *)&t34)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t34 + 1), t33);
        uint8_t t35 = ((((uint8_t *)&t34))[0] != 0);
        F.ret = t34;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_t9805qtonbsag gm_f_range_template_dbt7218(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1) {
    struct gm_fr_range_template_dbt7218 F;
    void * gm_l0; /* s */
    uint8_t gm_l1; /* inclusive */
    GM_STACK_CHECK(&F, 122u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    gm_b16_8 t2;
    uint8_t t4 = gm_l1;
    if (!t4) goto ie3;
    gm_b16_8 t6;
    GM_ST(void *, (uint8_t *)&t6, (void *)(gm_s43cq4krigs2b));
    GM_ST(uintptr_t, (uint8_t *)&t6 + 8, (uintptr_t)(9));
    gm_b16_8 t7 = t6;
    t2 = t7;
    goto ix3;
ie3:;
    gm_b16_8 t9;
    GM_ST(void *, (uint8_t *)&t9, (void *)(gm_seue7mq07v0oc));
    GM_ST(uintptr_t, (uint8_t *)&t9 + 8, (uintptr_t)(5));
    gm_b16_8 t10 = t9;
    t2 = t10;
ix3:;
    gm_t9805qtonbsag t11 = gm_f_prelude_template_7s9nsp0((void *)NULL, cx, NULL, t1, t2);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_recursive_without_ret_48l31rm(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_tl874t2atnlh3 a2) {
    struct gm_fr_recursive_without_ret_48l31rm F;
    void * gm_l0; /* cx */
    gm_b16_8 gm_l1; /* name */
    gm_tl874t2atnlh3 gm_l2; /* span */
    GM_STACK_CHECK(&F, 144u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        gm_tl874t2atnlh3 t3 = gm_l2;
        gm_b16_8 t4 = gm_l1;
        gm_tnpifv20irnph t5;
        GM_ST(gm_b16_8, ((uint8_t *)&t5 + 0), t4);
        gm_tabb710d9mq2e t6;
        gm_f_format_c8qq9n9((void *)&t6, (void *)NULL, cx, NULL, t5);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t6))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t6) + 8, gm_srcl43af23eher3, 280, 20);
        gm_b16_8 t7 = GM_LD(gm_b16_8, (((uint8_t *)&t6) + 8));
        gm_b16_8 t8 = GM_LD(gm_b16_8, ((uint8_t *)&t7));
        gm_b0_1 t9 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t2, t3, t8);
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_021udu6(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_021udu6 F;
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
        gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)16ULL), ((uint64_t)8ULL));
        uint8_t t9 = ((((uint8_t *)&t8))[0] != 0);
        F.ret = t8;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_0jmnnpk(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_0jmnnpk F;
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
        gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)16ULL), ((uint64_t)8ULL));
        uint8_t t9 = ((((uint8_t *)&t8))[0] != 0);
        F.ret = t8;
        goto gm_fret;
    }
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_4bta8ne_2(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_4bta8ne_2 F;
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
        gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)24ULL), ((uint64_t)8ULL));
        uint8_t t9 = ((((uint8_t *)&t8))[0] != 0);
        F.ret = t8;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_52486fh(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_52486fh F;
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
        gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)48ULL), ((uint64_t)8ULL));
        uint8_t t9 = ((((uint8_t *)&t8))[0] != 0);
        F.ret = t8;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_5719f1b(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_5719f1b F;
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
        gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)8ULL), ((uint64_t)8ULL));
        uint8_t t9 = ((((uint8_t *)&t8))[0] != 0);
        F.ret = t8;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_61fb705_2(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_61fb705_2 F;
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
        gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)88ULL), ((uint64_t)8ULL));
        uint8_t t9 = ((((uint8_t *)&t8))[0] != 0);
        F.ret = t8;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_emnjbt3(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_emnjbt3 F;
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
        gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)48ULL), ((uint64_t)8ULL));
        uint8_t t9 = ((((uint8_t *)&t8))[0] != 0);
        F.ret = t8;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_h08har5_2(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_h08har5_2 F;
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
        gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)32ULL), ((uint64_t)8ULL));
        uint8_t t9 = ((((uint8_t *)&t8))[0] != 0);
        F.ret = t8;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_h9tbucc_3(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_h9tbucc_3 F;
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
        gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)24ULL), ((uint64_t)8ULL));
        uint8_t t9 = ((((uint8_t *)&t8))[0] != 0);
        F.ret = t8;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_hkmvuvc(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_hkmvuvc F;
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
        gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)1ULL), ((uint64_t)1ULL));
        uint8_t t9 = ((((uint8_t *)&t8))[0] != 0);
        F.ret = t8;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_j4iu2lk(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_j4iu2lk F;
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
inline __attribute__((aligned(4))) void gm_f_restore_scope_edqq1v8(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_restore_scope_edqq1v8 F;
    void * gm_l0; /* s */
    gm_t66j1im9ip128 gm_l1; /* mark */
    GM_STACK_CHECK_SHALLOW(&F, 136u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        void * t3 = (void *)(((uint8_t *)t2) + 1488);
        uint64_t t4 = GM_LD(uint64_t, (((uint8_t *)a1) + 0));
        gm_f_truncate_7r782uc(t3, t4);
    }
    {
        void * t5 = gm_l0;
        void * t6 = (void *)(((uint8_t *)t5) + 1528);
        uint64_t t7 = GM_LD(uint64_t, (((uint8_t *)a1) + 8));
        gm_f_truncate_daelje0(t6, t7);
    }
    {
        void * t8 = gm_l0;
        void * t9 = (void *)(((uint8_t *)t8) + 1608);
        uint64_t t10 = GM_LD(uint64_t, (((uint8_t *)a1) + 16));
        gm_f_truncate_dsb08pk(t9, t10);
    }
    {
        void * t11 = gm_l0;
        void * t12 = (void *)(((uint8_t *)t11) + 1568);
        uint64_t t13 = GM_LD(uint64_t, (((uint8_t *)a1) + 24));
        gm_f_truncate_upeumbt(t12, t13);
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_fsnl6fa(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_show_fsnl6fa F;
    void * gm_l0; /* cx */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    void * t2 = GM_LD(void *, (((uint8_t *)t1) + 0));
    gm_t88rcucg1b7uk t3 = gm_l1;
    gm_b16_8 t4 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t2, t3);
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
    GM_STACK_CHECK(&F, 129u);
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
        gm_b16_8 t8 = GM_LD(gm_b16_8, ((uint8_t *)&t7));
        uint8_t t9 = 0;
        F.ret = t8;
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
inline __attribute__((aligned(4))) uint32_t gm_f_temp_ilbvj8q(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_tl874t2atnlh3 a2) {
    struct gm_fr_temp_ilbvj8q F;
    void * gm_l0; /* cx */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    gm_tl874t2atnlh3 gm_l2; /* span */
    GM_STACK_CHECK(&F, 76u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t1 = gm_l0;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_shv1k5m05egbe));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(5));
    gm_t88rcucg1b7uk t3 = gm_l1;
    gm_tl874t2atnlh3 t4 = gm_l2;
    uint32_t t5 = gm_f_new_local_003h9es((void *)NULL, cx, NULL, t1, t2, t3, ((uint8_t)1), t4);
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
inline __attribute__((aligned(4))) void gm_f_truncate_7r782uc(void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_truncate_7r782uc F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* n */
    GM_STACK_CHECK_SHALLOW(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint64_t t3 = gm_l1;
        void * t4 = gm_l0;
        uint64_t t5 = GM_LD(uintptr_t, (((uint8_t *)t4) + 0) + 8);
        uint8_t t6 = (t3 < t5);
        if (!t6) goto ie2;
        {
            void * t8 = gm_l0;
            void * t9 = gm_l0;
            uint64_t t10 = GM_LD(uintptr_t, (((uint8_t *)t9) + 0) + 8);
            void * t11 = ((uint8_t *)GM_LD(void *, (((uint8_t *)t9) + 0)));
            uint64_t t12 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t13 = gm_l1;
            uint64_t t14 = (uintptr_t)t13;
            if (t12 > t14 || t14 > (uintptr_t)t10) gm_trap_slice(t12, t14, t10, gm_srcrgc9bkquf6nr, 125, 36);
            gm_b16_8 t15;
            GM_ST(void *, (uint8_t *)&t15, (void *)(((uint8_t *)t11 + t12 * 24)));
            GM_ST(uintptr_t, (uint8_t *)&t15 + 8, (uintptr_t)(t14 - t12));
            GM_ST(gm_b16_8, (((uint8_t *)t8) + 0), t15);
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
inline __attribute__((aligned(4))) void gm_f_truncate_daelje0(void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_truncate_daelje0 F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* n */
    GM_STACK_CHECK_SHALLOW(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint64_t t3 = gm_l1;
        void * t4 = gm_l0;
        uint64_t t5 = GM_LD(uintptr_t, (((uint8_t *)t4) + 0) + 8);
        uint8_t t6 = (t3 < t5);
        if (!t6) goto ie2;
        {
            void * t8 = gm_l0;
            void * t9 = gm_l0;
            uint64_t t10 = GM_LD(uintptr_t, (((uint8_t *)t9) + 0) + 8);
            void * t11 = ((uint8_t *)GM_LD(void *, (((uint8_t *)t9) + 0)));
            uint64_t t12 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t13 = gm_l1;
            uint64_t t14 = (uintptr_t)t13;
            if (t12 > t14 || t14 > (uintptr_t)t10) gm_trap_slice(t12, t14, t10, gm_srcrgc9bkquf6nr, 125, 36);
            gm_b16_8 t15;
            GM_ST(void *, (uint8_t *)&t15, (void *)(((uint8_t *)t11 + t12 * 24)));
            GM_ST(uintptr_t, (uint8_t *)&t15 + 8, (uintptr_t)(t14 - t12));
            GM_ST(gm_b16_8, (((uint8_t *)t8) + 0), t15);
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
inline __attribute__((aligned(4))) void gm_f_truncate_dsb08pk(void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_truncate_dsb08pk F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* n */
    GM_STACK_CHECK_SHALLOW(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint64_t t3 = gm_l1;
        void * t4 = gm_l0;
        uint64_t t5 = GM_LD(uintptr_t, (((uint8_t *)t4) + 0) + 8);
        uint8_t t6 = (t3 < t5);
        if (!t6) goto ie2;
        {
            void * t8 = gm_l0;
            void * t9 = gm_l0;
            uint64_t t10 = GM_LD(uintptr_t, (((uint8_t *)t9) + 0) + 8);
            void * t11 = ((uint8_t *)GM_LD(void *, (((uint8_t *)t9) + 0)));
            uint64_t t12 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t13 = gm_l1;
            uint64_t t14 = (uintptr_t)t13;
            if (t12 > t14 || t14 > (uintptr_t)t10) gm_trap_slice(t12, t14, t10, gm_srcrgc9bkquf6nr, 125, 36);
            gm_b16_8 t15;
            GM_ST(void *, (uint8_t *)&t15, (void *)(((uint8_t *)t11 + t12 * 24)));
            GM_ST(uintptr_t, (uint8_t *)&t15 + 8, (uintptr_t)(t14 - t12));
            GM_ST(gm_b16_8, (((uint8_t *)t8) + 0), t15);
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
inline __attribute__((aligned(4))) void gm_f_truncate_ua7l21h(void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_truncate_ua7l21h F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* n */
    GM_STACK_CHECK_SHALLOW(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint64_t t3 = gm_l1;
        void * t4 = gm_l0;
        uint64_t t5 = GM_LD(uintptr_t, (((uint8_t *)t4) + 0) + 8);
        uint8_t t6 = (t3 < t5);
        if (!t6) goto ie2;
        {
            void * t8 = gm_l0;
            void * t9 = gm_l0;
            uint64_t t10 = GM_LD(uintptr_t, (((uint8_t *)t9) + 0) + 8);
            void * t11 = ((uint8_t *)GM_LD(void *, (((uint8_t *)t9) + 0)));
            uint64_t t12 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t13 = gm_l1;
            uint64_t t14 = (uintptr_t)t13;
            if (t12 > t14 || t14 > (uintptr_t)t10) gm_trap_slice(t12, t14, t10, gm_srcrgc9bkquf6nr, 125, 36);
            gm_b16_8 t15;
            GM_ST(void *, (uint8_t *)&t15, (void *)(((uint8_t *)t11 + t12 * 40)));
            GM_ST(uintptr_t, (uint8_t *)&t15 + 8, (uintptr_t)(t14 - t12));
            GM_ST(gm_b16_8, (((uint8_t *)t8) + 0), t15);
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
inline __attribute__((aligned(4))) void gm_f_truncate_upeumbt(void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_truncate_upeumbt F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* n */
    GM_STACK_CHECK_SHALLOW(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint64_t t3 = gm_l1;
        void * t4 = gm_l0;
        uint64_t t5 = GM_LD(uintptr_t, (((uint8_t *)t4) + 0) + 8);
        uint8_t t6 = (t3 < t5);
        if (!t6) goto ie2;
        {
            void * t8 = gm_l0;
            void * t9 = gm_l0;
            uint64_t t10 = GM_LD(uintptr_t, (((uint8_t *)t9) + 0) + 8);
            void * t11 = ((uint8_t *)GM_LD(void *, (((uint8_t *)t9) + 0)));
            uint64_t t12 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t13 = gm_l1;
            uint64_t t14 = (uintptr_t)t13;
            if (t12 > t14 || t14 > (uintptr_t)t10) gm_trap_slice(t12, t14, t10, gm_srcrgc9bkquf6nr, 125, 36);
            gm_b16_8 t15;
            GM_ST(void *, (uint8_t *)&t15, (void *)(((uint8_t *)t11 + t12 * 224)));
            GM_ST(uintptr_t, (uint8_t *)&t15 + 8, (uintptr_t)(t14 - t12));
            GM_ST(gm_b16_8, (((uint8_t *)t8) + 0), t15);
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
inline __attribute__((aligned(4))) uint8_t gm_f_type_operand_ta2mhln(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_type_operand_ta2mhln F;
    void * gm_l0; /* cx */
    void * gm_l1; /* e */
    gm_b16_8 gm_l2; /* elems */
    GM_STACK_CHECK(&F, 87u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t3 = gm_l1;
        uint8_t t4 = GM_LD(uint8_t, ((((uint8_t *)t3) + 8) + 0));
        if (t4 != ((uint8_t)10ULL)) goto ie2;
        gm_b16_8 t5 = GM_LD(gm_b16_8, ((((uint8_t *)t3) + 8) + 8));
        gm_l2 = t5;
        uint64_t t6 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        uint8_t t7 = ((t6 == ((uint64_t)0ULL)));
        if (!t7) goto ie2;
        {
            uint8_t t9 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t10 = gm_l0;
        void * t11 = gm_l1;
        uint8_t t12 = gm_f_denotes_type_jgfet92((void *)NULL, cx, NULL, t10, t11);
        if (gm_unw.kind) {
            goto gm_lad;
        }
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_unit_2age34u(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_unit_2age34u F;
    void * gm_l0; /* t */
    GM_STACK_CHECK(&F, 180u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    gm_t6j6smj7qoctu t2;
    gm_b16_8 t3 = gm_f_empty_slice_rcd2jln();
    gm_b16_8 t4 = GM_LD(gm_b16_8, ((uint8_t *)&t3));
    GM_ST(gm_b16_8, (uint8_t *)&t2 + 0, t4);
    gm_b16_8 t5 = gm_f_empty_slice_5nn7ph0();
    gm_b16_8 t6 = GM_LD(gm_b16_8, ((uint8_t *)&t5));
    GM_ST(gm_b16_8, (uint8_t *)&t2 + 16, t6);
    gm_t53at80pfdus3 t7;
    GM_ST(uint8_t, (uint8_t *)&t7 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t7 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t7 + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&t7 + 40, 0);
    GM_ST(uint64_t, (uint8_t *)&t7 + 48, 0);
    GM_ST(uint8_t, ((uint8_t *)&t7 + 0), ((uint8_t)12ULL));
    GM_ST(gm_t6j6smj7qoctu, ((uint8_t *)&t7 + 8), t2);
    gm_t88rcucg1b7uk t8 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t1, &t7);
    if (gm_unw.kind) {
        goto gm_lad;
    }
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
inline __attribute__((aligned(4))) void * gm_f_unit_ohk62mc(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1) {
    struct gm_fr_unit_ohk62mc F;
    void * gm_l0; /* s */
    gm_tl874t2atnlh3 gm_l1; /* span */
    GM_STACK_CHECK(&F, 156u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    void * t2 = (void *)(((uint8_t *)t1) + 8);
    gm_t88rcucg1b7uk t3 = gm_f_unit_2age34u((void *)NULL, cx, NULL, t2);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_b16_8 t4 = gm_f_empty_slice_g3tddrb_2();
    gm_teti411eujvf4 t5;
    memset(&t5, 0, sizeof t5);
    GM_ST(uint8_t, ((uint8_t *)&t5 + 0), ((uint8_t)35ULL));
    GM_ST(gm_b16_8, ((uint8_t *)&t5 + 8), t4);
    gm_tl874t2atnlh3 t6 = gm_l1;
    void * t7 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t3, &t5, t6);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.ret = t7;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void * gm_f_unsupported_r3r7qdh(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2) {
    struct gm_fr_unsupported_r3r7qdh F;
    void * gm_l0; /* cx */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_b16_8 gm_l2; /* what */
    GM_STACK_CHECK(&F, 133u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 0));
        gm_tl874t2atnlh3 t4 = gm_l1;
        gm_b16_8 t5 = gm_l2;
        gm_b0_1 t6 = gm_f_unsupported_t7lgvp9((void *)NULL, cx, NULL, t3, t4, t5);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t7 = gm_l0;
        void * t8 = GM_LD(void *, (((uint8_t *)t7) + 0));
        gm_tl874t2atnlh3 t9 = gm_l1;
        void * t10 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t8, t9);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_unsupported_t7lgvp9(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2) {
    struct gm_fr_unsupported_t7lgvp9 F;
    void * gm_l0; /* s */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_b16_8 gm_l2; /* what */
    GM_STACK_CHECK(&F, 176u);
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
        if (((((uint8_t *)&t7))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t7) + 8, gm_srclv0dcbbfhves, 635, 19);
        gm_b16_8 t8 = GM_LD(gm_b16_8, (((uint8_t *)&t7) + 8));
        gm_b16_8 t9 = GM_LD(gm_b16_8, ((uint8_t *)&t8));
        gm_b0_1 t10 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t2, t3, t9);
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
inline __attribute__((aligned(4))) void gm_f_untyped_complex_20agrmu(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1) {
    struct gm_fr_untyped_complex_20agrmu F;
    GM_STACK_CHECK(&F, 216u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memcpy(&F.l1, a1, sizeof F.l1);
    gm_tu15p0hi2qlv6 t1;
    memcpy((uint8_t *)&t1 + 0, ((uint8_t *)a0), 96);
    memcpy((uint8_t *)&t1 + 96, ((uint8_t *)&F.l1), 96);
    gm_tq7s98ihonkju t2 = gm_f_new_fnepb5f((void *)NULL, cx, NULL, &t1, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_ktdkih9htpv)));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    if (((((uint8_t *)&t2))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t2) + 8, gm_srcod98slk3vnra, 564, 89);
    void * t3 = GM_LD(void *, (((uint8_t *)&t2) + 8));
    memset(&(*(gm_teti411eujvf4 *)gm_out), 0, sizeof (*(gm_teti411eujvf4 *)gm_out));
    GM_ST(uint8_t, (uint8_t *)&(*(gm_teti411eujvf4 *)gm_out) + 0, ((uint8_t)3ULL));
    GM_ST(void *, (uint8_t *)&(*(gm_teti411eujvf4 *)gm_out) + 8, t3);
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
inline __attribute__((aligned(4))) void gm_f_untyped_float_m6lbr0g(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_untyped_float_m6lbr0g F;
    GM_STACK_CHECK(&F, 24u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memcpy(&F.l0, a0, sizeof F.l0);
    gm_tq7s98ihonkju t1 = gm_f_new_u1gvj7k((void *)NULL, cx, NULL, &(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l0)), &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kf1lpoo3h6o)));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    if (((((uint8_t *)&t1))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t1) + 8, gm_srcod98slk3vnra, 561, 80);
    void * t2 = GM_LD(void *, (((uint8_t *)&t1) + 8));
    memset(&(*(gm_teti411eujvf4 *)gm_out), 0, sizeof (*(gm_teti411eujvf4 *)gm_out));
    GM_ST(uint8_t, (uint8_t *)&(*(gm_teti411eujvf4 *)gm_out) + 0, ((uint8_t)2ULL));
    GM_ST(void *, (uint8_t *)&(*(gm_teti411eujvf4 *)gm_out) + 8, t2);
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_usize_ty_qnrd30f(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_usize_ty_qnrd30f F;
    void * gm_l0; /* t */
    GM_STACK_CHECK(&F, 84u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    gm_t53at80pfdus3 t2;
    memset(&t2, 0, sizeof t2);
    GM_ST(uint8_t, ((uint8_t *)&t2 + 0), ((uint8_t)1ULL));
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
__attribute__((aligned(4))) void * gm_f_addr_of_expr_ffg7r6j(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_tkn42u7na3t81 a2, gm_tl874t2atnlh3 a3) {
    struct gm_fr_addr_of_expr_ffg7r6j F;
    void * gm_l0; /* cx */
    void * gm_l1; /* operand */
    gm_tkn42u7na3t81 gm_l2; /* expect */
    gm_tl874t2atnlh3 gm_l3; /* span */
    uint8_t gm_l4; /* temp_ok */
    gm_tkn42u7na3t81 gm_l5; /* hint */
    gm_t88rcucg1b7uk gm_l6; /* t */
    gm_t88rcucg1b7uk gm_l7; /* pointee */
    void * gm_l8; /* v */
    void * gm_l9; /* base */
    GM_STACK_CHECK(&F, 1108u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        void * t2 = gm_l0;
        uint8_t t3 = GM_LD(uint8_t, (((uint8_t *)t2) + 472));
        gm_l4 = t3;
    }
    {
        void * t4 = gm_l0;
        GM_ST(uint8_t, (((uint8_t *)t4) + 472), ((uint8_t)0));
    }
    {
        gm_tkn42u7na3t81 t5;
        memset(&t5, 0, sizeof t5);
        gm_l5 = t5;
    }
    {
        if (!((((uint8_t *)&gm_l2))[4] != 0)) goto ie6;
        gm_t88rcucg1b7uk t7 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l2));
        gm_l6 = t7;
        void * t8 = gm_l0;
        void * t9 = GM_LD(void *, (((uint8_t *)t8) + 0));
        void * t10 = (void *)(((uint8_t *)t9) + 8);
        void * t11 = gm_l0;
        void * t12 = (void *)(((uint8_t *)t11) + 96);
        void * t13 = gm_l0;
        void * t14 = GM_LD(void *, (((uint8_t *)t13) + 0));
        void * t15 = (void *)(((uint8_t *)t14) + 8);
        gm_t88rcucg1b7uk t16 = gm_l6;
        gm_t88rcucg1b7uk t17 = gm_f_shallow_8ur803a((void *)NULL, cx, NULL, t12, t15, t16);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t53at80pfdus3 t18;
        gm_f_kind_jcnt4hm((void *)&t18, t10, GM_CONV(gm_csguankko4lmhd, t17));
        uint8_t t19 = GM_LD(uint8_t, (((uint8_t *)&t18) + 0));
        if (t19 != ((uint8_t)8ULL)) goto ie6;
        gm_t88rcucg1b7uk t20 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t18) + 8) + 4));
        gm_l7 = t20;
        {
            gm_t88rcucg1b7uk t22 = gm_l7;
            gm_tkn42u7na3t81 t23;
            GM_ST(uint8_t, (uint8_t *)&t23 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t23 + 6, 0);
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t23), t22);
            ((uint8_t *)&t23)[4] = 1;
            gm_l5 = t23;
        }
        goto ix6;
ie6:;
ix6:;
    }
    {
        void * t24 = gm_l0;
        void * t25 = gm_l1;
        gm_tkn42u7na3t81 t26 = gm_l5;
        void * t27 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t24, t25, t26);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l8 = t27;
    }
    {
        void * t29 = gm_l0;
        void * t30 = gm_l8;
        gm_b16_8 t31;
        GM_ST(void *, (uint8_t *)&t31, (void *)(gm_s7i2mni64durc));
        GM_ST(uintptr_t, (uint8_t *)&t31 + 8, (uintptr_t)(17));
        uint8_t t32 = gm_f_reject_second_class_c561fm5((void *)NULL, cx, NULL, t29, t30, t31);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (!t32) goto ie28;
        {
            void * t34 = gm_l0;
            void * t35 = GM_LD(void *, (((uint8_t *)t34) + 0));
            gm_tl874t2atnlh3 t36 = gm_l3;
            void * t37 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t35, t36);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t38 = 0;
            F.ret = t37;
            goto gm_fret;
        }
        goto ix28;
ie28:;
ix28:;
    }
    {
        void * t40 = gm_l8;
        gm_tl874t2atnlh3 t41 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t40) + 72));
        void * t42 = gm_l0;
        void * t43 = GM_LD(void *, (((uint8_t *)t42) + 0));
        gm_b16_8 t44 = GM_LD(gm_b16_8, ((((uint8_t *)t43) + 2496) + 0));
        uint8_t t45 = 0;
        for (uintptr_t i = 0; i < (uintptr_t)(GM_LD(uintptr_t, ((uint8_t *)&t44) + 8)); i++) if (gm_eqff85ac10o3(((uint8_t *)GM_LD(void *, ((uint8_t *)&t44))) + i * 12, ((uint8_t *)&t41))) { t45 = 1; break; }
        uint8_t t46 = (uint8_t)(t45);
        if (!t46) goto ie39;
        {
            void * t48 = gm_l0;
            gm_tl874t2atnlh3 t49 = gm_l3;
            gm_b16_8 t50;
            GM_ST(void *, (uint8_t *)&t50, (void *)(gm_scuaf7ud4smlt));
            GM_ST(uintptr_t, (uint8_t *)&t50 + 8, (uintptr_t)(122));
            gm_b0_1 t51 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t48, t49, t50);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t52 = gm_l0;
            void * t53 = GM_LD(void *, (((uint8_t *)t52) + 0));
            gm_tl874t2atnlh3 t54 = gm_l3;
            void * t55 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t53, t54);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t56 = 0;
            F.ret = t55;
            goto gm_fret;
        }
        goto ix39;
ie39:;
ix39:;
    }
    {
        void * t58 = gm_l8;
        uint8_t t59 = GM_LD(uint8_t, ((((uint8_t *)t58) + 8) + 0));
        if (t59 != ((uint8_t)19ULL)) goto ie57;
        void * t60 = GM_LD(void *, (((((uint8_t *)t58) + 8) + 8) + 0));
        gm_l9 = t60;
        void * t61 = gm_l0;
        void * t62 = GM_LD(void *, (((uint8_t *)t61) + 0));
        void * t63 = gm_l9;
        gm_t88rcucg1b7uk t64 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t63) + 0));
        gm_tkn42u7na3t81 t65 = gm_f_bits_struct_ty_l1kjtap(t62, GM_CONV(gm_csguankko4lmhd, t64));
        uint8_t t66;
        if (((((uint8_t *)&t65))[4] != 0)) goto mn68;
        t66 = ((uint8_t)0);
        goto me67;
mn68:;
        t66 = ((uint8_t)1);
        goto me67;
mn69:;
        gm_trap("no match arm matched the scrutinee", gm_srch2l5nl06vsci, 685, 38);
me67:;
        if (!t66) goto ie57;
        {
            void * t71 = gm_l0;
            gm_tl874t2atnlh3 t72 = gm_l3;
            gm_b16_8 t73;
            GM_ST(void *, (uint8_t *)&t73, (void *)(gm_scfl995gsjrop));
            GM_ST(uintptr_t, (uint8_t *)&t73 + 8, (uintptr_t)(57));
            gm_b0_1 t74 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t71, t72, t73);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t75 = gm_l0;
            void * t76 = GM_LD(void *, (((uint8_t *)t75) + 0));
            gm_tl874t2atnlh3 t77 = gm_l3;
            void * t78 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t76, t77);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t79 = 0;
            F.ret = t78;
            goto gm_fret;
        }
        goto ix57;
ie57:;
ix57:;
    }
    {
        void * t80 = gm_l0;
        void * t81 = gm_l8;
        gm_tqs5p4n2tq19v t82 = gm_f_place_mutability_0tlm4g8(t80, t81);
        if (!((((uint8_t *)&t82))[1] != 0)) goto mn84;
        uint8_t t85 = GM_LD(uint8_t, ((uint8_t *)&t82));
        if (!(t85 == ((uint8_t)1))) goto mn84;
        void * t86 = gm_l0;
        void * t87 = GM_LD(void *, (((uint8_t *)t86) + 0));
        void * t88 = (void *)(((uint8_t *)t87) + 8);
        void * t89 = gm_l8;
        gm_t88rcucg1b7uk t90 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t89) + 0));
        gm_t88rcucg1b7uk t91 = gm_f_ptr_t1h2m2d((void *)NULL, cx, NULL, t88, ((uint8_t)0), t90);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t92 = gm_l8;
        gm_teti411eujvf4 t93;
        memset(&t93, 0, sizeof t93);
        GM_ST(uint8_t, ((uint8_t *)&t93 + 0), ((uint8_t)24ULL));
        GM_ST(void *, ((uint8_t *)&t93 + 8), t92);
        gm_tl874t2atnlh3 t94 = gm_l3;
        void * t95 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t91, &t93, t94);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t96 = 0;
        F.ret = t95;
        goto gm_fret;
        goto me83;
mn84:;
        if (!((((uint8_t *)&t82))[1] != 0)) goto mn97;
        uint8_t t98 = GM_LD(uint8_t, ((uint8_t *)&t82));
        if (!(t98 == ((uint8_t)0))) goto mn97;
        void * t99 = gm_l0;
        void * t100 = GM_LD(void *, (((uint8_t *)t99) + 0));
        void * t101 = (void *)(((uint8_t *)t100) + 8);
        void * t102 = gm_l8;
        gm_t88rcucg1b7uk t103 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t102) + 0));
        gm_t88rcucg1b7uk t104 = gm_f_ptr_t1h2m2d((void *)NULL, cx, NULL, t101, ((uint8_t)1), t103);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t105 = gm_l8;
        gm_teti411eujvf4 t106;
        memset(&t106, 0, sizeof t106);
        GM_ST(uint8_t, ((uint8_t *)&t106 + 0), ((uint8_t)24ULL));
        GM_ST(void *, ((uint8_t *)&t106 + 8), t105);
        gm_tl874t2atnlh3 t107 = gm_l3;
        void * t108 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t104, &t106, t107);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t109 = 0;
        F.ret = t108;
        goto gm_fret;
        goto me83;
mn97:;
        if (((((uint8_t *)&t82))[1] != 0)) goto mn110;
        {
            uint8_t t113 = gm_l4;
            if (!t113) goto ie112;
            {
                void * t115 = gm_l0;
                void * t116 = GM_LD(void *, (((uint8_t *)t115) + 0));
                void * t117 = (void *)(((uint8_t *)t116) + 8);
                void * t118 = gm_l8;
                gm_t88rcucg1b7uk t119 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t118) + 0));
                gm_t88rcucg1b7uk t120 = gm_f_ptr_t1h2m2d((void *)NULL, cx, NULL, t117, ((uint8_t)1), t119);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                void * t121 = gm_l8;
                gm_teti411eujvf4 t122;
                memset(&t122, 0, sizeof t122);
                GM_ST(uint8_t, ((uint8_t *)&t122 + 0), ((uint8_t)25ULL));
                GM_ST(void *, ((uint8_t *)&t122 + 8), t121);
                gm_tl874t2atnlh3 t123 = gm_l3;
                void * t124 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t120, &t122, t123);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t125 = 0;
                F.ret = t124;
                goto gm_fret;
            }
            goto ix112;
ie112:;
ix112:;
        }
        {
            void * t126 = gm_l0;
            gm_tl874t2atnlh3 t127 = gm_l3;
            gm_b16_8 t128;
            GM_ST(void *, (uint8_t *)&t128, (void *)(gm_s709tdhiijp4r));
            GM_ST(uintptr_t, (uint8_t *)&t128 + 8, (uintptr_t)(77));
            gm_b0_1 t129 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t126, t127, t128);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t130 = gm_l0;
            void * t131 = GM_LD(void *, (((uint8_t *)t130) + 0));
            gm_tl874t2atnlh3 t132 = gm_l3;
            void * t133 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t131, t132);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t134 = 0;
            F.ret = t133;
            goto gm_fret;
        }
        goto me83;
mn110:;
        gm_trap("no match arm matched the scrutinee", gm_srch2l5nl06vsci, 689, 5);
me83:;
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
__attribute__((aligned(4))) void * gm_f_arith_op_hsstk43(void *env, gm_node *cx, gm_node *inst, void * a0, gm_ts6oqppjh0ba3 a1, void * a2, void * a3, gm_tl874t2atnlh3 a4) {
    struct gm_fr_arith_op_hsstk43 F;
    void * gm_l0; /* cx */
    gm_ts6oqppjh0ba3 gm_l1; /* op */
    void * gm_l2; /* lhs */
    void * gm_l3; /* r */
    gm_tl874t2atnlh3 gm_l4; /* span */
    gm_b16_8 gm_l5; /* what */
    gm_t9qkh0rsooc8d gm_l6; /* f */
    gm_t9qkh0rsooc8d gm_l7; /* f */
    gm_t9qkh0rsooc8d gm_l8; /* f */
    gm_t88rcucg1b7uk gm_l9; /* ty */
    gm_ts6oqppjh0ba3 gm_l10; /* top */
    gm_t88rcucg1b7uk gm_l11; /* rty */
    gm_t88rcucg1b7uk gm_l12; /* ty */
    gm_t88rcucg1b7uk gm_l13; /* st */
    gm_t88rcucg1b7uk gm_l14; /* u */
    gm_ts6oqppjh0ba3 gm_l15; /* top */
    void * gm_l16; /* n */
    gm_ts6oqppjh0ba3 gm_l17; /* top */
    GM_STACK_CHECK(&F, 1344u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    {
        gm_ts6oqppjh0ba3 t2 = gm_l1;
        gm_b16_8 t3 = gm_f_binop_text_0ljfc0r(t2);
        gm_tnpifv20irnph t4;
        GM_ST(gm_b16_8, ((uint8_t *)&t4 + 0), t3);
        gm_tabb710d9mq2e t5;
        gm_f_format_ko5t8rr((void *)&t5, (void *)NULL, cx, NULL, t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t5))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t5) + 8, gm_srch2l5nl06vsci, 907, 13);
        gm_b16_8 t6 = GM_LD(gm_b16_8, (((uint8_t *)&t5) + 8));
        gm_l5 = t6;
    }
    {
        gm_ts6oqppjh0ba3 t8 = gm_l1;
        gm_t9qkh0rsooc8d t9;
        GM_ST(uint8_t, ((uint8_t *)&t9 + 0), ((uint8_t)3ULL));
        gm_ts6oqppjh0ba3 t10;
        GM_ST(uint8_t, ((uint8_t *)&t10 + 0), ((uint8_t)0ULL));
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t10 + 1), t9);
        gm_t9qkh0rsooc8d t11;
        GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)3ULL));
        gm_ts6oqppjh0ba3 t12;
        GM_ST(uint8_t, ((uint8_t *)&t12 + 0), ((uint8_t)1ULL));
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t12 + 1), t11);
        gm_t9qkh0rsooc8d t13;
        GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)3ULL));
        gm_ts6oqppjh0ba3 t14;
        GM_ST(uint8_t, ((uint8_t *)&t14 + 0), ((uint8_t)2ULL));
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 1), t13);
        gm_tl2e7spmb4osd t15;
        GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t15 + 0), t10);
        GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t15 + 2), t12);
        GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t15 + 4), t14);
        uint8_t t16 = 0;
        if (gm_eqbqb4gb1d92(((uint8_t *)&t15) + 0, ((uint8_t *)&t8))) t16 = 1;
        if (gm_eqbqb4gb1d92(((uint8_t *)&t15) + 2, ((uint8_t *)&t8))) t16 = 1;
        if (gm_eqbqb4gb1d92(((uint8_t *)&t15) + 4, ((uint8_t *)&t8))) t16 = 1;
        uint8_t t17 = (uint8_t)(t16);
        if (!t17) goto ie7;
        void * t18 = gm_l0;
        void * t19 = gm_l2;
        void * t20 = gm_l3;
        uint8_t t21 = gm_f_has_optional_operand_279ro7i((void *)NULL, cx, NULL, t18, t19, t20);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (!t21) goto ie7;
        {
            void * t23 = gm_l0;
            gm_ts6oqppjh0ba3 t24 = gm_l1;
            void * t25 = gm_l2;
            void * t26 = gm_l3;
            gm_tl874t2atnlh3 t27 = gm_l4;
            void * t28 = gm_f_chained_opt_op_nnt3ijn((void *)NULL, cx, NULL, t23, t24, t25, t26, t27);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t29 = 0;
            F.ret = t28;
            goto gm_fret;
        }
        goto ix7;
ie7:;
ix7:;
    }
    {
        uint8_t t34 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t34 != ((uint8_t)0ULL)) goto pa33;
        gm_t9qkh0rsooc8d t35 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l1) + 1));
        gm_l6 = t35;
        goto po32;
pa33:;
        uint8_t t37 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t37 != ((uint8_t)1ULL)) goto pa36;
        gm_t9qkh0rsooc8d t38 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l1) + 1));
        gm_l6 = t38;
        goto po32;
pa36:;
        uint8_t t40 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t40 != ((uint8_t)2ULL)) goto pa39;
        gm_t9qkh0rsooc8d t41 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l1) + 1));
        gm_l6 = t41;
        goto po32;
pa39:;
        goto mn31;
po32:;
        {
            void * t43 = gm_l0;
            void * t44 = gm_l2;
            gm_t88rcucg1b7uk t45 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t44) + 0));
            void * t46 = gm_l3;
            gm_t88rcucg1b7uk t47 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t46) + 0));
            gm_tl874t2atnlh3 t48 = gm_l4;
            gm_b16_8 t49 = gm_l5;
            gm_b16_8 t50 = GM_LD(gm_b16_8, ((uint8_t *)&t49));
            gm_t88rcucg1b7uk t51 = gm_f_unify_operands_5sp7j1j((void *)NULL, cx, NULL, t43, t45, t47, t48, t50);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l9 = t51;
        }
        {
            void * t52 = gm_l0;
            void * t53 = (void *)(((uint8_t *)t52) + 320);
            gm_t91vm1g0k2rpm t54;
            memset((uint8_t *)&t54 + 34, 0, 6);
            gm_t88rcucg1b7uk t55 = gm_l9;
            GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t54 + 0, t55);
            gm_tl874t2atnlh3 t56 = gm_l4;
            GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t54 + 4, t56);
            gm_ts6oqppjh0ba3 t57 = gm_l1;
            gm_b16_8 t58 = gm_f_binop_text_0ljfc0r(t57);
            GM_ST(gm_b16_8, (uint8_t *)&t54 + 16, t58);
            uint8_t t59 = (!(GM_LD(uint8_t, ((uint8_t *)&gm_l6) + 0) == ((uint8_t)0ULL)));
            GM_ST(uint8_t, (uint8_t *)&t54 + 32, t59);
            GM_ST(uint8_t, (uint8_t *)&t54 + 33, ((uint8_t)0));
            gm_tpm220j5qhjke t60;
            GM_ST(uint8_t, (uint8_t *)&t60 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t60 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t60 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t60 + 0), ((uint8_t)3ULL));
            GM_ST(gm_t91vm1g0k2rpm, ((uint8_t *)&t60 + 8), t54);
            gm_tftb1odc739t5 t61 = gm_f_push_7koe9lk(t53, &t60);
            if (((((uint8_t *)&t61))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t61) + 1, gm_srch2l5nl06vsci, 912, 13);
            gm_b0_1 t62 = GM_LD(gm_b0_1, (((uint8_t *)&t61) + 1));
        }
        {
            gm_ts6oqppjh0ba3 t63;
            uint8_t t69 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
            switch (t69) { case ((uint8_t)0ULL): goto ma65; case ((uint8_t)1ULL): goto ma66; case ((uint8_t)2ULL): goto ma67; case ((uint8_t)3ULL): goto ma67; case ((uint8_t)4ULL): goto ma67; case ((uint8_t)5ULL): goto ma67; case ((uint8_t)6ULL): goto ma67; case ((uint8_t)7ULL): goto ma67; case ((uint8_t)8ULL): goto ma67; case ((uint8_t)9ULL): goto ma67; case ((uint8_t)10ULL): goto ma67; case ((uint8_t)11ULL): goto ma67; case ((uint8_t)12ULL): goto ma67; case ((uint8_t)13ULL): goto ma67; case ((uint8_t)14ULL): goto ma67; case ((uint8_t)15ULL): goto ma67; case ((uint8_t)16ULL): goto ma67; case ((uint8_t)17ULL): goto ma67; case ((uint8_t)18ULL): goto ma67; case ((uint8_t)19ULL): goto ma67; case ((uint8_t)20ULL): goto ma67; case ((uint8_t)21ULL): goto ma67; default: goto ma67; }
ma65:;
            uint8_t t71 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
            if (t71 != ((uint8_t)0ULL)) goto mn70;
            gm_t9qkh0rsooc8d t72 = gm_l6;
            gm_ts6oqppjh0ba3 t73;
            GM_ST(uint8_t, ((uint8_t *)&t73 + 0), ((uint8_t)0ULL));
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t73 + 1), t72);
            t63 = t73;
            goto me64;
mn70:;
ma66:;
            uint8_t t75 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
            if (t75 != ((uint8_t)1ULL)) goto mn74;
            gm_t9qkh0rsooc8d t76 = gm_l6;
            gm_ts6oqppjh0ba3 t77;
            GM_ST(uint8_t, ((uint8_t *)&t77 + 0), ((uint8_t)1ULL));
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t77 + 1), t76);
            t63 = t77;
            goto me64;
mn74:;
ma67:;
            gm_t9qkh0rsooc8d t79 = gm_l6;
            gm_ts6oqppjh0ba3 t80;
            GM_ST(uint8_t, ((uint8_t *)&t80 + 0), ((uint8_t)2ULL));
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t80 + 1), t79);
            t63 = t80;
            goto me64;
mn78:;
ma68:;
            gm_trap("no match arm matched the scrutinee", gm_srch2l5nl06vsci, 913, 30);
me64:;
            gm_l10 = t63;
        }
        {
            gm_t88rcucg1b7uk t81;
            uint8_t t83 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l6) + 0) == ((uint8_t)3ULL)));
            if (!t83) goto ie82;
            void * t85 = gm_l0;
            void * t86 = GM_LD(void *, (((uint8_t *)t85) + 0));
            void * t87 = (void *)(((uint8_t *)t86) + 8);
            gm_t88rcucg1b7uk t88 = gm_l9;
            gm_t88rcucg1b7uk t89 = gm_f_optional_rh6okj3((void *)NULL, cx, NULL, t87, t88);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t88rcucg1b7uk t90 = t89;
            t81 = t90;
            goto ix82;
ie82:;
            gm_t88rcucg1b7uk t92 = gm_l9;
            gm_t88rcucg1b7uk t93 = t92;
            t81 = t93;
ix82:;
            gm_l11 = t81;
        }
        {
            gm_t88rcucg1b7uk t94 = gm_l11;
            gm_trl9d7m1k5lbd t95;
            memset((uint8_t *)&t95 + 2, 0, 6);
            gm_ts6oqppjh0ba3 t96 = gm_l10;
            GM_ST(gm_ts6oqppjh0ba3, (uint8_t *)&t95 + 0, t96);
            void * t97 = gm_l2;
            GM_ST(void *, (uint8_t *)&t95 + 8, t97);
            void * t98 = gm_l3;
            GM_ST(void *, (uint8_t *)&t95 + 16, t98);
            gm_teti411eujvf4 t99;
            memset(&t99, 0, sizeof t99);
            GM_ST(uint8_t, ((uint8_t *)&t99 + 0), ((uint8_t)30ULL));
            GM_ST(gm_trl9d7m1k5lbd, ((uint8_t *)&t99 + 8), t95);
            gm_tl874t2atnlh3 t100 = gm_l4;
            void * t101 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t94, &t99, t100);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t102 = 0;
            F.ret = t101;
            goto gm_fret;
        }
        goto me30;
mn31:;
        {
            void * t105 = gm_l0;
            void * t106 = gm_l2;
            gm_t88rcucg1b7uk t107 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t106) + 0));
            void * t108 = gm_l3;
            gm_t88rcucg1b7uk t109 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t108) + 0));
            gm_tl874t2atnlh3 t110 = gm_l4;
            gm_b16_8 t111 = gm_l5;
            gm_b16_8 t112 = GM_LD(gm_b16_8, ((uint8_t *)&t111));
            gm_t88rcucg1b7uk t113 = gm_f_unify_operands_5sp7j1j((void *)NULL, cx, NULL, t105, t107, t109, t110, t112);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l12 = t113;
        }
        {
            void * t114 = gm_l0;
            void * t115 = (void *)(((uint8_t *)t114) + 96);
            void * t116 = gm_l0;
            void * t117 = GM_LD(void *, (((uint8_t *)t116) + 0));
            gm_t88rcucg1b7uk t118 = gm_l12;
            gm_t88rcucg1b7uk t119 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t115, t117, t118);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l13 = t119;
        }
        {
            uint8_t t121 = (!(GM_LD(uint8_t, ((uint8_t *)&gm_l1) + 0) == ((uint8_t)4ULL)));
            if (!t121) goto ie120;
            uint8_t t122 = (!(GM_LD(uint8_t, ((uint8_t *)&gm_l1) + 0) == ((uint8_t)5ULL)));
            if (!t122) goto ie120;
            {
                void * t125 = gm_l0;
                gm_t88rcucg1b7uk t126 = gm_l13;
                gm_ts6oqppjh0ba3 t127 = gm_l1;
                gm_b16_8 t128 = gm_f_binop_text_0ljfc0r(t127);
                gm_tl874t2atnlh3 t129 = gm_l4;
                gm_tkn42u7na3t81 t130 = gm_f_bits_operand_d1ppvrt((void *)NULL, cx, NULL, t125, t126, t128, t129);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (!((((uint8_t *)&t130))[4] != 0)) goto ie124;
                gm_t88rcucg1b7uk t131 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&t130));
                gm_l14 = t131;
                {
                    gm_ts6oqppjh0ba3 t133;
                    uint8_t t139 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                    switch (t139) { case ((uint8_t)0ULL): goto ma137; case ((uint8_t)1ULL): goto ma137; case ((uint8_t)2ULL): goto ma137; case ((uint8_t)3ULL): goto ma137; case ((uint8_t)4ULL): goto ma137; case ((uint8_t)5ULL): goto ma137; case ((uint8_t)6ULL): goto ma137; case ((uint8_t)7ULL): goto ma135; case ((uint8_t)8ULL): goto ma136; case ((uint8_t)9ULL): goto ma137; case ((uint8_t)10ULL): goto ma137; case ((uint8_t)11ULL): goto ma137; case ((uint8_t)12ULL): goto ma137; case ((uint8_t)13ULL): goto ma137; case ((uint8_t)14ULL): goto ma137; case ((uint8_t)15ULL): goto ma137; case ((uint8_t)16ULL): goto ma137; case ((uint8_t)17ULL): goto ma137; case ((uint8_t)18ULL): goto ma137; case ((uint8_t)19ULL): goto ma137; case ((uint8_t)20ULL): goto ma137; case ((uint8_t)21ULL): goto ma137; default: goto ma137; }
ma135:;
                    uint8_t t141 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                    if (t141 != ((uint8_t)7ULL)) goto mn140;
                    gm_ts6oqppjh0ba3 t142;
                    GM_ST(uint8_t, (uint8_t *)&t142 + 1, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t142 + 0), ((uint8_t)7ULL));
                    t133 = t142;
                    goto me134;
mn140:;
ma136:;
                    uint8_t t144 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                    if (t144 != ((uint8_t)8ULL)) goto mn143;
                    gm_ts6oqppjh0ba3 t145;
                    GM_ST(uint8_t, (uint8_t *)&t145 + 1, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t145 + 0), ((uint8_t)8ULL));
                    t133 = t145;
                    goto me134;
mn143:;
ma137:;
                    gm_ts6oqppjh0ba3 t147;
                    GM_ST(uint8_t, (uint8_t *)&t147 + 1, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t147 + 0), ((uint8_t)9ULL));
                    t133 = t147;
                    goto me134;
mn146:;
ma138:;
                    gm_trap("no match arm matched the scrutinee", gm_srch2l5nl06vsci, 926, 38);
me134:;
                    gm_l15 = t133;
                }
                {
                    gm_t88rcucg1b7uk t148 = gm_l14;
                    gm_trl9d7m1k5lbd t149;
                    memset((uint8_t *)&t149 + 2, 0, 6);
                    gm_ts6oqppjh0ba3 t150 = gm_l15;
                    GM_ST(gm_ts6oqppjh0ba3, (uint8_t *)&t149 + 0, t150);
                    void * t151 = gm_l2;
                    gm_t88rcucg1b7uk t152 = gm_l14;
                    void * t153 = gm_f_bits_convert_8rcef5o((void *)NULL, cx, NULL, t151, t152);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    GM_ST(void *, (uint8_t *)&t149 + 8, t153);
                    void * t154 = gm_l3;
                    gm_t88rcucg1b7uk t155 = gm_l14;
                    void * t156 = gm_f_bits_convert_8rcef5o((void *)NULL, cx, NULL, t154, t155);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    GM_ST(void *, (uint8_t *)&t149 + 16, t156);
                    gm_teti411eujvf4 t157;
                    memset(&t157, 0, sizeof t157);
                    GM_ST(uint8_t, ((uint8_t *)&t157 + 0), ((uint8_t)30ULL));
                    GM_ST(gm_trl9d7m1k5lbd, ((uint8_t *)&t157 + 8), t149);
                    gm_tl874t2atnlh3 t158 = gm_l4;
                    void * t159 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t148, &t157, t158);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l16 = t159;
                }
                {
                    void * t160 = gm_l16;
                    gm_t88rcucg1b7uk t161 = gm_l13;
                    void * t162 = gm_f_bits_convert_8rcef5o((void *)NULL, cx, NULL, t160, t161);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t163 = 0;
                    F.ret = t162;
                    goto gm_fret;
                }
                goto ix124;
ie124:;
ix124:;
            }
            goto ix120;
ie120:;
ix120:;
        }
        {
            void * t164 = gm_l0;
            void * t165 = (void *)(((uint8_t *)t164) + 320);
            gm_t91vm1g0k2rpm t166;
            memset((uint8_t *)&t166 + 34, 0, 6);
            gm_t88rcucg1b7uk t167 = gm_l12;
            GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t166 + 0, t167);
            gm_tl874t2atnlh3 t168 = gm_l4;
            GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t166 + 4, t168);
            gm_ts6oqppjh0ba3 t169 = gm_l1;
            gm_b16_8 t170 = gm_f_binop_text_0ljfc0r(t169);
            GM_ST(gm_b16_8, (uint8_t *)&t166 + 16, t170);
            uint8_t t171 = (!(GM_LD(uint8_t, ((uint8_t *)&gm_l1) + 0) == ((uint8_t)4ULL)));
            GM_ST(uint8_t, (uint8_t *)&t166 + 32, t171);
            GM_ST(uint8_t, (uint8_t *)&t166 + 33, ((uint8_t)0));
            gm_tpm220j5qhjke t172;
            GM_ST(uint8_t, (uint8_t *)&t172 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t172 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t172 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t172 + 0), ((uint8_t)3ULL));
            GM_ST(gm_t91vm1g0k2rpm, ((uint8_t *)&t172 + 8), t166);
            gm_tftb1odc739t5 t173 = gm_f_push_7koe9lk(t165, &t172);
            if (((((uint8_t *)&t173))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t173) + 1, gm_srch2l5nl06vsci, 935, 13);
            gm_b0_1 t174 = GM_LD(gm_b0_1, (((uint8_t *)&t173) + 1));
        }
        {
            gm_ts6oqppjh0ba3 t175;
            uint8_t t183 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
            switch (t183) { case ((uint8_t)0ULL): goto ma181; case ((uint8_t)1ULL): goto ma181; case ((uint8_t)2ULL): goto ma181; case ((uint8_t)3ULL): goto ma181; case ((uint8_t)4ULL): goto ma177; case ((uint8_t)5ULL): goto ma178; case ((uint8_t)6ULL): goto ma181; case ((uint8_t)7ULL): goto ma179; case ((uint8_t)8ULL): goto ma180; case ((uint8_t)9ULL): goto ma181; case ((uint8_t)10ULL): goto ma181; case ((uint8_t)11ULL): goto ma181; case ((uint8_t)12ULL): goto ma181; case ((uint8_t)13ULL): goto ma181; case ((uint8_t)14ULL): goto ma181; case ((uint8_t)15ULL): goto ma181; case ((uint8_t)16ULL): goto ma181; case ((uint8_t)17ULL): goto ma181; case ((uint8_t)18ULL): goto ma181; case ((uint8_t)19ULL): goto ma181; case ((uint8_t)20ULL): goto ma181; case ((uint8_t)21ULL): goto ma181; default: goto ma181; }
ma177:;
            uint8_t t185 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
            if (t185 != ((uint8_t)4ULL)) goto mn184;
            gm_ts6oqppjh0ba3 t186;
            GM_ST(uint8_t, (uint8_t *)&t186 + 1, 0);
            GM_ST(uint8_t, ((uint8_t *)&t186 + 0), ((uint8_t)4ULL));
            t175 = t186;
            goto me176;
mn184:;
ma178:;
            uint8_t t188 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
            if (t188 != ((uint8_t)5ULL)) goto mn187;
            gm_ts6oqppjh0ba3 t189;
            GM_ST(uint8_t, (uint8_t *)&t189 + 1, 0);
            GM_ST(uint8_t, ((uint8_t *)&t189 + 0), ((uint8_t)5ULL));
            t175 = t189;
            goto me176;
mn187:;
ma179:;
            uint8_t t191 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
            if (t191 != ((uint8_t)7ULL)) goto mn190;
            gm_ts6oqppjh0ba3 t192;
            GM_ST(uint8_t, (uint8_t *)&t192 + 1, 0);
            GM_ST(uint8_t, ((uint8_t *)&t192 + 0), ((uint8_t)7ULL));
            t175 = t192;
            goto me176;
mn190:;
ma180:;
            uint8_t t194 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
            if (t194 != ((uint8_t)8ULL)) goto mn193;
            gm_ts6oqppjh0ba3 t195;
            GM_ST(uint8_t, (uint8_t *)&t195 + 1, 0);
            GM_ST(uint8_t, ((uint8_t *)&t195 + 0), ((uint8_t)8ULL));
            t175 = t195;
            goto me176;
mn193:;
ma181:;
            gm_ts6oqppjh0ba3 t197;
            GM_ST(uint8_t, (uint8_t *)&t197 + 1, 0);
            GM_ST(uint8_t, ((uint8_t *)&t197 + 0), ((uint8_t)9ULL));
            t175 = t197;
            goto me176;
mn196:;
ma182:;
            gm_trap("no match arm matched the scrutinee", gm_srch2l5nl06vsci, 936, 30);
me176:;
            gm_l17 = t175;
        }
        {
            gm_t88rcucg1b7uk t198 = gm_l12;
            gm_trl9d7m1k5lbd t199;
            memset((uint8_t *)&t199 + 2, 0, 6);
            gm_ts6oqppjh0ba3 t200 = gm_l17;
            GM_ST(gm_ts6oqppjh0ba3, (uint8_t *)&t199 + 0, t200);
            void * t201 = gm_l2;
            GM_ST(void *, (uint8_t *)&t199 + 8, t201);
            void * t202 = gm_l3;
            GM_ST(void *, (uint8_t *)&t199 + 16, t202);
            gm_teti411eujvf4 t203;
            memset(&t203, 0, sizeof t203);
            GM_ST(uint8_t, ((uint8_t *)&t203 + 0), ((uint8_t)30ULL));
            GM_ST(gm_trl9d7m1k5lbd, ((uint8_t *)&t203 + 8), t199);
            gm_tl874t2atnlh3 t204 = gm_l4;
            void * t205 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t198, &t203, t204);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t206 = 0;
            F.ret = t205;
            goto gm_fret;
        }
        goto me30;
mn103:;
        gm_trap("no match arm matched the scrutinee", gm_srch2l5nl06vsci, 909, 5);
me30:;
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
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_array_elem_join_clnoo81(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1) {
    struct gm_fr_array_elem_join_clnoo81 F;
    void * gm_l0; /* cx */
    gm_b16_8 gm_l1; /* types */
    void * gm_l2; /* tys */
    gm_tkn42u7na3t81 gm_l3; /* elem */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    gm_t88rcucg1b7uk gm_l6; /* t0 */
    gm_t88rcucg1b7uk gm_l7; /* t */
    gm_t88rcucg1b7uk gm_l8; /* e */
    gm_t88rcucg1b7uk gm_l9; /* p */
    GM_STACK_CHECK(&F, 407u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 0));
        void * t4 = (void *)(((uint8_t *)t3) + 8);
        gm_l2 = t4;
    }
    {
        gm_tkn42u7na3t81 t5;
        memset(&t5, 0, sizeof t5);
        gm_l3 = t5;
    }
    {
        {
            gm_b16_8 t7 = gm_l1;
            gm_l4 = t7;
        }
        {
            gm_l5 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t9 = gm_l5;
                uint64_t t10 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                uint8_t t11 = (t9 < t10);
                if (!t11) goto lb8;
                {
                    uint64_t t14 = gm_l5;
                    uint64_t t15 = (uintptr_t)t14;
                    uint64_t t16 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                    gm_t88rcucg1b7uk t19 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t15 * 4));
                    gm_l6 = t19;
                    {
                        void * t21 = gm_l0;
                        void * t22 = (void *)(((uint8_t *)t21) + 96);
                        void * t23 = gm_l0;
                        void * t24 = GM_LD(void *, (((uint8_t *)t23) + 0));
                        gm_t88rcucg1b7uk t25 = gm_l6;
                        gm_t88rcucg1b7uk t26 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t22, t24, t25);
                        if (gm_unw.kind) {
                            goto gm_ll12;
                        }
                        gm_l7 = t26;
                    }
                    {
                        void * t28 = gm_l2;
                        gm_t88rcucg1b7uk t29 = gm_l7;
                        gm_t53at80pfdus3 t30;
                        gm_f_kind_jcnt4hm((void *)&t30, t28, GM_CONV(gm_csguankko4lmhd, t29));
                        uint8_t t31 = GM_LD(uint8_t, (((uint8_t *)&t30) + 0));
                        if (t31 != ((uint8_t)11ULL)) goto ie27;
                        gm_t88rcucg1b7uk t32 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t30) + 8) + 8));
                        gm_l8 = t32;
                        {
                            uint8_t t35 = 0;
                            void * t36 = gm_l2;
                            gm_t88rcucg1b7uk t37 = gm_l7;
                            uint8_t t38 = gm_f_has_infer_3o82a0b(t36, GM_CONV(gm_csguankko4lmhd, t37));
                            if (!t38) {
                                void * t39 = gm_l2;
                                gm_t88rcucg1b7uk t40 = gm_l7;
                                uint8_t t41 = gm_f_is_positional_array_6sufsk8(t39, GM_CONV(gm_csguankko4lmhd, t40));
                                uint8_t t42 = (uint8_t)!t41;
                                t35 = t42;
                            } else t35 = 1;
                            if (!t35) goto ie34;
                            {
                                gm_tkn42u7na3t81 t44;
                                memset(&t44, 0, sizeof t44);
                                uint8_t t45 = 0;
                                F.ret = t44;
                                goto gm_fret;
                            }
                            goto ix34;
ie34:;
ix34:;
                        }
                        {
                            if (!((((uint8_t *)&gm_l3))[4] != 0)) goto ie46;
                            gm_t88rcucg1b7uk t47 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l3));
                            gm_l9 = t47;
                            gm_t88rcucg1b7uk t48 = gm_l9;
                            gm_t88rcucg1b7uk t49 = gm_l8;
                            uint8_t t50 = (!gm_eq9m6mvtmgde(((uint8_t *)&t48), ((uint8_t *)&t49)));
                            if (!t50) goto ie46;
                            {
                                gm_tkn42u7na3t81 t52;
                                memset(&t52, 0, sizeof t52);
                                uint8_t t53 = 0;
                                F.ret = t52;
                                goto gm_fret;
                            }
                            goto ix46;
ie46:;
ix46:;
                        }
                        {
                            gm_t88rcucg1b7uk t54 = gm_l8;
                            gm_tkn42u7na3t81 t55;
                            GM_ST(uint8_t, (uint8_t *)&t55 + 5, 0);
                            GM_ST(uint16_t, (uint8_t *)&t55 + 6, 0);
                            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t55), t54);
                            ((uint8_t *)&t55)[4] = 1;
                            gm_l3 = t55;
                        }
                        goto ix27;
ie27:;
                        {
                            gm_tkn42u7na3t81 t57;
                            memset(&t57, 0, sizeof t57);
                            uint8_t t58 = 0;
                            F.ret = t57;
                            goto gm_fret;
                        }
ix27:;
                    }
                    goto me17;
mn18:;
                    gm_trap("no match arm matched the scrutinee", gm_srch2l5nl06vsci, 1329, 5);
me17:;
                }
lc8:;
                {
                    uint64_t t60 = gm_l5;
                    uint64_t r61; bool o62 = __builtin_add_overflow(t60, ((uint64_t)1ULL), &r61);
                    uint64_t t63 = r61;
                    gm_l5 = t63;
                }
            }
gm_ll12:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb8; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc8; }
            }
            goto gm_lad;
lb8:;
        }
    }
    {
        gm_tkn42u7na3t81 t64 = gm_l3;
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
inline __attribute__((aligned(4))) void * gm_f_auto_deref_vdb1hdn(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_auto_deref_vdb1hdn F;
    void * gm_l0; /* cx */
    void * gm_l1; /* b */
    gm_t88rcucg1b7uk gm_l2; /* pointee */
    GM_STACK_CHECK(&F, 291u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t3 = gm_l0;
        void * t4 = GM_LD(void *, (((uint8_t *)t3) + 0));
        void * t5 = (void *)(((uint8_t *)t4) + 8);
        void * t6 = gm_l0;
        void * t7 = (void *)(((uint8_t *)t6) + 96);
        void * t8 = gm_l0;
        void * t9 = GM_LD(void *, (((uint8_t *)t8) + 0));
        void * t10 = (void *)(((uint8_t *)t9) + 8);
        void * t11 = gm_l1;
        gm_t88rcucg1b7uk t12 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t11) + 0));
        gm_t88rcucg1b7uk t13 = gm_f_shallow_8ur803a((void *)NULL, cx, NULL, t7, t10, t12);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t53at80pfdus3 t14;
        gm_f_kind_jcnt4hm((void *)&t14, t5, GM_CONV(gm_csguankko4lmhd, t13));
        uint8_t t15 = GM_LD(uint8_t, (((uint8_t *)&t14) + 0));
        if (t15 != ((uint8_t)8ULL)) goto ie2;
        gm_t88rcucg1b7uk t16 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t14) + 8) + 4));
        gm_l2 = t16;
        {
            gm_t88rcucg1b7uk t18 = gm_l2;
            void * t19 = gm_l1;
            gm_teti411eujvf4 t20;
            memset(&t20, 0, sizeof t20);
            GM_ST(uint8_t, ((uint8_t *)&t20 + 0), ((uint8_t)18ULL));
            GM_ST(void *, ((uint8_t *)&t20 + 8), t19);
            void * t21 = gm_l1;
            gm_tl874t2atnlh3 t22 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t21) + 72));
            void * t23 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t18, &t20, t22);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t24 = 0;
            F.ret = t23;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t25 = gm_l1;
        uint8_t t26 = 0;
        F.ret = t25;
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
__attribute__((aligned(4))) void * gm_f_binary_op_7ocdl4v(void *env, gm_node *cx, gm_node *inst, void * a0, gm_ts6oqppjh0ba3 a1, void * a2, void * a3, gm_tl874t2atnlh3 a4) {
    struct gm_fr_binary_op_7ocdl4v F;
    void * gm_l0; /* cx */
    gm_ts6oqppjh0ba3 gm_l1; /* op */
    void * gm_l2; /* lhs */
    void * gm_l3; /* rhs */
    gm_tl874t2atnlh3 gm_l4; /* span */
    void * gm_l5; /* tys */
    gm_t88rcucg1b7uk gm_l6; /* lt */
    void * gm_l7; /* r */
    void * gm_l8; /* l */
    void * gm_l9; /* r */
    void * gm_l11; /* r */
    gm_tjpqvfd72o62u gm_l12; /* li */
    gm_tjpqvfd72o62u gm_l13; /* ri */
    gm_t3mf58gv9kf3n gm_l14; /* eq */
    void * gm_l15; /* r */
    gm_t88rcucg1b7uk gm_l16; /* ty */
    gm_ts6oqppjh0ba3 gm_l17; /* top */
    void * gm_l18; /* r */
    gm_t88rcucg1b7uk gm_l19; /* t */
    gm_t9qkh0rsooc8d gm_l20; /* f */
    gm_ts6oqppjh0ba3 gm_l21; /* top */
    gm_t88rcucg1b7uk gm_l22; /* ty */
    gm_t88rcucg1b7uk gm_l23; /* expect */
    gm_t88rcucg1b7uk gm_l24; /* inner */
    void * gm_l25; /* r */
    GM_STACK_CHECK(&F, 3309u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    {
        void * t2 = gm_l0;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 0));
        void * t4 = (void *)(((uint8_t *)t3) + 8);
        gm_l5 = t4;
    }
    {
        void * t5 = gm_l0;
        void * t6 = (void *)(((uint8_t *)t5) + 96);
        void * t7 = gm_l0;
        void * t8 = GM_LD(void *, (((uint8_t *)t7) + 0));
        void * t9 = gm_l2;
        gm_t88rcucg1b7uk t10 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t9) + 0));
        gm_t88rcucg1b7uk t11 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t6, t8, t10);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l6 = t11;
    }
    {
        void * t13 = gm_l0;
        void * t14 = GM_LD(void *, (((uint8_t *)t13) + 0));
        void * t15 = (void *)(((uint8_t *)t14) + 8);
        gm_t88rcucg1b7uk t16 = gm_l6;
        uint8_t t17 = gm_f_is_vector_gkq6b88(t15, GM_CONV(gm_csguankko4lmhd, t16));
        if (!t17) goto ie12;
        gm_ts6oqppjh0ba3 t18 = gm_l1;
        gm_ts6oqppjh0ba3 t19;
        GM_ST(uint8_t, (uint8_t *)&t19 + 1, 0);
        GM_ST(uint8_t, ((uint8_t *)&t19 + 0), ((uint8_t)12ULL));
        gm_ts6oqppjh0ba3 t20;
        GM_ST(uint8_t, (uint8_t *)&t20 + 1, 0);
        GM_ST(uint8_t, ((uint8_t *)&t20 + 0), ((uint8_t)13ULL));
        gm_t9ldr1q5hn9fq t21;
        GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t21 + 0), t19);
        GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t21 + 2), t20);
        uint8_t t22 = 0;
        if (gm_eqbqb4gb1d92(((uint8_t *)&t21) + 0, ((uint8_t *)&t18))) t22 = 1;
        if (gm_eqbqb4gb1d92(((uint8_t *)&t21) + 2, ((uint8_t *)&t18))) t22 = 1;
        uint8_t t23 = (uint8_t)(!t22);
        if (!t23) goto ie12;
        {
            void * t25 = gm_l0;
            gm_ts6oqppjh0ba3 t26 = gm_l1;
            void * t27 = gm_l2;
            void * t28 = gm_l3;
            gm_tl874t2atnlh3 t29 = gm_l4;
            void * t30 = gm_f_vector_binary_bicu7u0((void *)NULL, cx, NULL, t25, t26, t27, t28, t29);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t31 = 0;
            F.ret = t30;
            goto gm_fret;
        }
        goto ix12;
ie12:;
ix12:;
    }
    {
        gm_ts6oqppjh0ba3 t33 = gm_l1;
        gm_ts6oqppjh0ba3 t34;
        GM_ST(uint8_t, (uint8_t *)&t34 + 1, 0);
        GM_ST(uint8_t, ((uint8_t *)&t34 + 0), ((uint8_t)12ULL));
        gm_ts6oqppjh0ba3 t35;
        GM_ST(uint8_t, (uint8_t *)&t35 + 1, 0);
        GM_ST(uint8_t, ((uint8_t *)&t35 + 0), ((uint8_t)13ULL));
        gm_ts6oqppjh0ba3 t36;
        GM_ST(uint8_t, (uint8_t *)&t36 + 1, 0);
        GM_ST(uint8_t, ((uint8_t *)&t36 + 0), ((uint8_t)20ULL));
        gm_ts6oqppjh0ba3 t37;
        GM_ST(uint8_t, (uint8_t *)&t37 + 1, 0);
        GM_ST(uint8_t, ((uint8_t *)&t37 + 0), ((uint8_t)21ULL));
        gm_ts6oqppjh0ba3 t38;
        GM_ST(uint8_t, (uint8_t *)&t38 + 1, 0);
        GM_ST(uint8_t, ((uint8_t *)&t38 + 0), ((uint8_t)14ULL));
        gm_ts6oqppjh0ba3 t39;
        GM_ST(uint8_t, (uint8_t *)&t39 + 1, 0);
        GM_ST(uint8_t, ((uint8_t *)&t39 + 0), ((uint8_t)15ULL));
        gm_ts6oqppjh0ba3 t40;
        GM_ST(uint8_t, (uint8_t *)&t40 + 1, 0);
        GM_ST(uint8_t, ((uint8_t *)&t40 + 0), ((uint8_t)10ULL));
        gm_ts6oqppjh0ba3 t41;
        GM_ST(uint8_t, (uint8_t *)&t41 + 1, 0);
        GM_ST(uint8_t, ((uint8_t *)&t41 + 0), ((uint8_t)11ULL));
        gm_tv78bma7gi894 t42;
        GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t42 + 0), t34);
        GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t42 + 2), t35);
        GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t42 + 4), t36);
        GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t42 + 6), t37);
        GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t42 + 8), t38);
        GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t42 + 10), t39);
        GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t42 + 12), t40);
        GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t42 + 14), t41);
        uint8_t t43 = 0;
        if (gm_eqbqb4gb1d92(((uint8_t *)&t42) + 0, ((uint8_t *)&t33))) t43 = 1;
        if (gm_eqbqb4gb1d92(((uint8_t *)&t42) + 2, ((uint8_t *)&t33))) t43 = 1;
        if (gm_eqbqb4gb1d92(((uint8_t *)&t42) + 4, ((uint8_t *)&t33))) t43 = 1;
        if (gm_eqbqb4gb1d92(((uint8_t *)&t42) + 6, ((uint8_t *)&t33))) t43 = 1;
        if (gm_eqbqb4gb1d92(((uint8_t *)&t42) + 8, ((uint8_t *)&t33))) t43 = 1;
        if (gm_eqbqb4gb1d92(((uint8_t *)&t42) + 10, ((uint8_t *)&t33))) t43 = 1;
        if (gm_eqbqb4gb1d92(((uint8_t *)&t42) + 12, ((uint8_t *)&t33))) t43 = 1;
        if (gm_eqbqb4gb1d92(((uint8_t *)&t42) + 14, ((uint8_t *)&t33))) t43 = 1;
        uint8_t t44 = (uint8_t)(!t43);
        if (!t44) goto ie32;
        void * t45 = gm_l0;
        void * t46 = gm_l2;
        gm_t88rcucg1b7uk t47 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t46) + 0));
        uint8_t t48 = gm_f_is_compound_number_kbipjl5((void *)NULL, cx, NULL, t45, t47);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (!t48) goto ie32;
        {
            void * t50 = gm_l0;
            void * t51 = gm_l3;
            gm_tkn42u7na3t81 t52;
            memset(&t52, 0, sizeof t52);
            void * t53 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t50, t51, t52);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l7 = t53;
        }
        {
            void * t54 = gm_l0;
            gm_ts6oqppjh0ba3 t55 = gm_l1;
            void * t56 = gm_l2;
            void * t57 = gm_l7;
            gm_tl874t2atnlh3 t58 = gm_l4;
            void * t59 = gm_f_compound_number_binary_p7eerjs((void *)NULL, cx, NULL, t54, t55, t56, t57, t58);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t60 = 0;
            F.ret = t59;
            goto gm_fret;
        }
        goto ix32;
ie32:;
ix32:;
    }
    {
        uint8_t t70 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        switch (t70) { case ((uint8_t)0ULL): goto ma66; case ((uint8_t)1ULL): goto ma66; case ((uint8_t)2ULL): goto ma66; case ((uint8_t)3ULL): goto ma65; case ((uint8_t)4ULL): goto ma66; case ((uint8_t)5ULL): goto ma66; case ((uint8_t)6ULL): goto ma65; case ((uint8_t)7ULL): goto ma66; case ((uint8_t)8ULL): goto ma66; case ((uint8_t)9ULL): goto ma66; case ((uint8_t)10ULL): goto ma68; case ((uint8_t)11ULL): goto ma68; case ((uint8_t)12ULL): goto ma63; case ((uint8_t)13ULL): goto ma63; case ((uint8_t)14ULL): goto ma67; case ((uint8_t)15ULL): goto ma67; case ((uint8_t)16ULL): goto ma64; case ((uint8_t)17ULL): goto ma64; case ((uint8_t)18ULL): goto ma64; case ((uint8_t)19ULL): goto ma64; case ((uint8_t)20ULL): goto ma62; case ((uint8_t)21ULL): goto ma62; default: goto ma69; }
ma62:;
        uint8_t t74 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t74 != ((uint8_t)20ULL)) goto pa73;
        goto po72;
pa73:;
        uint8_t t76 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t76 != ((uint8_t)21ULL)) goto pa75;
        goto po72;
pa75:;
        goto mn71;
po72:;
        {
            void * t78 = gm_l0;
            void * t79 = gm_l2;
            void * t80 = gm_f_expect_bool_cdo5g1q((void *)NULL, cx, NULL, t78, t79);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l8 = t80;
        }
        {
            void * t81 = gm_l0;
            void * t82 = gm_l3;
            void * t83 = gm_l5;
            gm_t88rcucg1b7uk t84 = gm_f_bool_ty_pn2p277((void *)NULL, cx, NULL, t83);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tkn42u7na3t81 t85;
            GM_ST(uint8_t, (uint8_t *)&t85 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t85 + 6, 0);
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t85), t84);
            ((uint8_t *)&t85)[4] = 1;
            void * t86 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t81, t82, t85);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l9 = t86;
        }
        {
            void * t87 = gm_l0;
            void * t88 = gm_l9;
            void * t89 = gm_f_expect_bool_cdo5g1q((void *)NULL, cx, NULL, t87, t88);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l9 = t89;
        }
        {
            void * t90 = gm_l5;
            gm_t88rcucg1b7uk t91 = gm_f_bool_ty_pn2p277((void *)NULL, cx, NULL, t90);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_ta612fil812pm t92;
            memset((uint8_t *)&t92 + 1, 0, 7);
            uint8_t t93 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l1) + 0) == ((uint8_t)20ULL)));
            GM_ST(uint8_t, (uint8_t *)&t92 + 0, t93);
            void * t94 = gm_l8;
            GM_ST(void *, (uint8_t *)&t92 + 8, t94);
            void * t95 = gm_l9;
            GM_ST(void *, (uint8_t *)&t92 + 16, t95);
            gm_teti411eujvf4 t96;
            memset(&t96, 0, sizeof t96);
            GM_ST(uint8_t, ((uint8_t *)&t96 + 0), ((uint8_t)31ULL));
            GM_ST(gm_ta612fil812pm, ((uint8_t *)&t96 + 8), t92);
            gm_tl874t2atnlh3 t97 = gm_l4;
            void * t98 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t91, &t96, t97);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t99 = 0;
            F.ret = t98;
            goto gm_fret;
        }
        goto me61;
mn71:;
ma63:;
        uint8_t t103 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t103 != ((uint8_t)12ULL)) goto pa102;
        goto po101;
pa102:;
        uint8_t t105 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t105 != ((uint8_t)13ULL)) goto pa104;
        goto po101;
pa104:;
        goto mn100;
po101:;
        {
            void * t108 = gm_l0;
            void * t109 = gm_l3;
            void * t110 = gm_l2;
            gm_t88rcucg1b7uk t111 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t110) + 0));
            gm_toqf51949ppls t112;
            gm_f_is_pattern_operand_p51tban((void *)&t112, (void *)NULL, cx, NULL, t108, t109, t111);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (!((((uint8_t *)&t112))[64] != 0)) goto ie107;
            F.l10 = (*(gm_t0d02142k4tu8 *)(((uint8_t *)&t112) + 0));
            {
                void * t114 = gm_l0;
                void * t115 = gm_l2;
                void * t116 = (void *)((uint8_t *)&F.l10);
                uint8_t t117 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l1) + 0) == ((uint8_t)13ULL)));
                gm_tl874t2atnlh3 t118 = gm_l4;
                void * t119 = gm_f_is_pattern_test_kdd7cfh((void *)NULL, cx, NULL, t114, t115, t116, t117, t118);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t120 = 0;
                F.ret = t119;
                goto gm_fret;
            }
            goto ix107;
ie107:;
ix107:;
        }
        {
            void * t121 = gm_l0;
            void * t122 = gm_l3;
            void * t123 = gm_l2;
            gm_t88rcucg1b7uk t124 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t123) + 0));
            gm_tkn42u7na3t81 t125;
            GM_ST(uint8_t, (uint8_t *)&t125 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t125 + 6, 0);
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t125), t124);
            ((uint8_t *)&t125)[4] = 1;
            void * t126 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t121, t122, t125);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l11 = t126;
        }
        {
            void * t128 = gm_l0;
            void * t129 = gm_l2;
            gm_t8cc1mtbrhlpt t130;
            gm_f_soa_of_jufdcj2((void *)&t130, (void *)NULL, cx, NULL, t128, t129);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (!((((uint8_t *)&t130))[32] != 0)) goto ie127;
            gm_l12 = (*(gm_tjpqvfd72o62u *)(((uint8_t *)&t130) + 0));
            {
                void * t133 = gm_l0;
                void * t134 = gm_l11;
                gm_t8cc1mtbrhlpt t135;
                gm_f_soa_of_jufdcj2((void *)&t135, (void *)NULL, cx, NULL, t133, t134);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (!((((uint8_t *)&t135))[32] != 0)) goto ie132;
                gm_l13 = (*(gm_tjpqvfd72o62u *)(((uint8_t *)&t135) + 0));
                {
                    uint8_t t138;
                    if ((((((uint8_t *)&gm_l12) + 8))[8] != 0)) goto mn140;
                    t138 = ((uint8_t)1);
                    goto me139;
mn140:;
                    t138 = ((uint8_t)0);
                    goto me139;
mn141:;
                    gm_trap("no match arm matched the scrutinee", gm_srch2l5nl06vsci, 741, 24);
me139:;
                    if (!t138) goto ie137;
                    uint8_t t142;
                    if ((((((uint8_t *)&gm_l13) + 8))[8] != 0)) goto mn144;
                    t142 = ((uint8_t)1);
                    goto me143;
mn144:;
                    t142 = ((uint8_t)0);
                    goto me143;
mn145:;
                    gm_trap("no match arm matched the scrutinee", gm_srch2l5nl06vsci, 741, 43);
me143:;
                    if (!t142) goto ie137;
                    gm_t88rcucg1b7uk t146 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l13) + 0));
                    gm_t88rcucg1b7uk t147 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l12) + 0));
                    uint8_t t148 = (gm_eq9m6mvtmgde(((uint8_t *)&t146), ((uint8_t *)&t147)));
                    if (!t148) goto ie137;
                    {
                        void * t150 = gm_l0;
                        void * t151 = gm_l2;
                        void * t152 = gm_l11;
                        gm_tjpqvfd72o62u t153;
                        uint8_t t155 = GM_LD(uint8_t, (((uint8_t *)&gm_l12) + 24));
                        if (!t155) goto ie154;
                        gm_tjpqvfd72o62u t157 = gm_l12;
                        gm_tjpqvfd72o62u t158 = t157;
                        t153 = t158;
                        goto ix154;
ie154:;
                        gm_tjpqvfd72o62u t160 = gm_l13;
                        gm_tjpqvfd72o62u t161 = t160;
                        t153 = t161;
ix154:;
                        uint8_t t162 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l1) + 0) == ((uint8_t)13ULL)));
                        gm_tl874t2atnlh3 t163 = gm_l4;
                        void * t164 = gm_f_soa_eq_ho8ji9t((void *)NULL, cx, NULL, t150, t151, t152, &t153, t162, t163);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t165 = 0;
                        F.ret = t164;
                        goto gm_fret;
                    }
                    goto ix137;
ie137:;
ix137:;
                }
                goto ix132;
ie132:;
ix132:;
            }
            goto ix127;
ie127:;
ix127:;
        }
        {
            void * t166 = gm_l0;
            void * t167 = gm_l2;
            void * t168 = gm_l11;
            gm_tl874t2atnlh3 t169 = gm_l4;
            gm_t3mf58gv9kf3n t170 = gm_f_equalize_ni1r5ra((void *)NULL, cx, NULL, t166, t167, t168, t169);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l14 = t170;
        }
        {
            void * t171 = gm_l5;
            gm_t88rcucg1b7uk t172 = gm_f_bool_ty_pn2p277((void *)NULL, cx, NULL, t171);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tlcl4u558l6p5 t173;
            memset((uint8_t *)&t173 + 17, 0, 7);
            void * t174 = GM_LD(void *, (((uint8_t *)&gm_l14) + 0));
            GM_ST(void *, (uint8_t *)&t173 + 0, t174);
            void * t175 = GM_LD(void *, (((uint8_t *)&gm_l14) + 8));
            GM_ST(void *, (uint8_t *)&t173 + 8, t175);
            uint8_t t176 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l1) + 0) == ((uint8_t)13ULL)));
            GM_ST(uint8_t, (uint8_t *)&t173 + 16, t176);
            gm_teti411eujvf4 t177;
            memset(&t177, 0, sizeof t177);
            GM_ST(uint8_t, ((uint8_t *)&t177 + 0), ((uint8_t)43ULL));
            GM_ST(gm_tlcl4u558l6p5, ((uint8_t *)&t177 + 8), t173);
            gm_tl874t2atnlh3 t178 = gm_l4;
            void * t179 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t172, &t177, t178);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t180 = 0;
            F.ret = t179;
            goto gm_fret;
        }
        goto me61;
mn100:;
ma64:;
        uint8_t t184 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t184 != ((uint8_t)16ULL)) goto pa183;
        goto po182;
pa183:;
        uint8_t t186 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t186 != ((uint8_t)17ULL)) goto pa185;
        goto po182;
pa185:;
        uint8_t t188 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t188 != ((uint8_t)18ULL)) goto pa187;
        goto po182;
pa187:;
        uint8_t t190 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t190 != ((uint8_t)19ULL)) goto pa189;
        goto po182;
pa189:;
        goto mn181;
po182:;
        {
            void * t192 = gm_l0;
            void * t193 = gm_l3;
            void * t194 = gm_l2;
            gm_t88rcucg1b7uk t195 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t194) + 0));
            gm_tkn42u7na3t81 t196;
            GM_ST(uint8_t, (uint8_t *)&t196 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t196 + 6, 0);
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t196), t195);
            ((uint8_t *)&t196)[4] = 1;
            void * t197 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t192, t193, t196);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l15 = t197;
        }
        {
            void * t198 = gm_l0;
            void * t199 = gm_l2;
            gm_t88rcucg1b7uk t200 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t199) + 0));
            void * t201 = gm_l15;
            gm_t88rcucg1b7uk t202 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t201) + 0));
            gm_tl874t2atnlh3 t203 = gm_l4;
            gm_b16_8 t204;
            GM_ST(void *, (uint8_t *)&t204, (void *)(gm_sie1pq1m4mo8b));
            GM_ST(uintptr_t, (uint8_t *)&t204 + 8, (uintptr_t)(28));
            gm_t88rcucg1b7uk t205 = gm_f_unify_operands_5sp7j1j((void *)NULL, cx, NULL, t198, t200, t202, t203, t204);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l16 = t205;
        }
        {
            void * t206 = gm_l0;
            void * t207 = (void *)(((uint8_t *)t206) + 320);
            gm_t88rcucg1b7uk t208 = gm_l16;
            gm_tl874t2atnlh3 t209 = gm_l4;
            gm_t2i8ne5ibd7v4 t210;
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t210 + 0), t208);
            GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t210 + 4), t209);
            gm_tpm220j5qhjke t211;
            GM_ST(uint8_t, (uint8_t *)&t211 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t211 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t211 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t211 + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&t211 + 32, 0);
            GM_ST(uint64_t, (uint8_t *)&t211 + 40, 0);
            GM_ST(uint8_t, ((uint8_t *)&t211 + 0), ((uint8_t)2ULL));
            GM_ST(gm_t2i8ne5ibd7v4, ((uint8_t *)&t211 + 8), t210);
            gm_tftb1odc739t5 t212 = gm_f_push_7koe9lk(t207, &t211);
            if (((((uint8_t *)&t212))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t212) + 1, gm_srch2l5nl06vsci, 752, 13);
            gm_b0_1 t213 = GM_LD(gm_b0_1, (((uint8_t *)&t212) + 1));
        }
        {
            gm_ts6oqppjh0ba3 t214;
            uint8_t t221 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
            switch (t221) { case ((uint8_t)0ULL): goto ma219; case ((uint8_t)1ULL): goto ma219; case ((uint8_t)2ULL): goto ma219; case ((uint8_t)3ULL): goto ma219; case ((uint8_t)4ULL): goto ma219; case ((uint8_t)5ULL): goto ma219; case ((uint8_t)6ULL): goto ma219; case ((uint8_t)7ULL): goto ma219; case ((uint8_t)8ULL): goto ma219; case ((uint8_t)9ULL): goto ma219; case ((uint8_t)10ULL): goto ma219; case ((uint8_t)11ULL): goto ma219; case ((uint8_t)12ULL): goto ma219; case ((uint8_t)13ULL): goto ma219; case ((uint8_t)14ULL): goto ma219; case ((uint8_t)15ULL): goto ma219; case ((uint8_t)16ULL): goto ma216; case ((uint8_t)17ULL): goto ma217; case ((uint8_t)18ULL): goto ma218; case ((uint8_t)19ULL): goto ma219; case ((uint8_t)20ULL): goto ma219; case ((uint8_t)21ULL): goto ma219; default: goto ma219; }
ma216:;
            uint8_t t223 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
            if (t223 != ((uint8_t)16ULL)) goto mn222;
            gm_ts6oqppjh0ba3 t224;
            GM_ST(uint8_t, (uint8_t *)&t224 + 1, 0);
            GM_ST(uint8_t, ((uint8_t *)&t224 + 0), ((uint8_t)10ULL));
            t214 = t224;
            goto me215;
mn222:;
ma217:;
            uint8_t t226 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
            if (t226 != ((uint8_t)17ULL)) goto mn225;
            gm_ts6oqppjh0ba3 t227;
            GM_ST(uint8_t, (uint8_t *)&t227 + 1, 0);
            GM_ST(uint8_t, ((uint8_t *)&t227 + 0), ((uint8_t)11ULL));
            t214 = t227;
            goto me215;
mn225:;
ma218:;
            uint8_t t229 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
            if (t229 != ((uint8_t)18ULL)) goto mn228;
            gm_ts6oqppjh0ba3 t230;
            GM_ST(uint8_t, (uint8_t *)&t230 + 1, 0);
            GM_ST(uint8_t, ((uint8_t *)&t230 + 0), ((uint8_t)12ULL));
            t214 = t230;
            goto me215;
mn228:;
ma219:;
            gm_ts6oqppjh0ba3 t232;
            GM_ST(uint8_t, (uint8_t *)&t232 + 1, 0);
            GM_ST(uint8_t, ((uint8_t *)&t232 + 0), ((uint8_t)13ULL));
            t214 = t232;
            goto me215;
mn231:;
ma220:;
            gm_trap("no match arm matched the scrutinee", gm_srch2l5nl06vsci, 753, 30);
me215:;
            gm_l17 = t214;
        }
        {
            void * t233 = gm_l5;
            gm_t88rcucg1b7uk t234 = gm_f_bool_ty_pn2p277((void *)NULL, cx, NULL, t233);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_trl9d7m1k5lbd t235;
            memset((uint8_t *)&t235 + 2, 0, 6);
            gm_ts6oqppjh0ba3 t236 = gm_l17;
            GM_ST(gm_ts6oqppjh0ba3, (uint8_t *)&t235 + 0, t236);
            void * t237 = gm_l2;
            GM_ST(void *, (uint8_t *)&t235 + 8, t237);
            void * t238 = gm_l15;
            GM_ST(void *, (uint8_t *)&t235 + 16, t238);
            gm_teti411eujvf4 t239;
            memset(&t239, 0, sizeof t239);
            GM_ST(uint8_t, ((uint8_t *)&t239 + 0), ((uint8_t)30ULL));
            GM_ST(gm_trl9d7m1k5lbd, ((uint8_t *)&t239 + 8), t235);
            gm_tl874t2atnlh3 t240 = gm_l4;
            void * t241 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t234, &t239, t240);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t242 = 0;
            F.ret = t241;
            goto gm_fret;
        }
        goto me61;
mn181:;
ma65:;
        uint8_t t246 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t246 != ((uint8_t)3ULL)) goto pa245;
        goto po244;
pa245:;
        uint8_t t248 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t248 != ((uint8_t)6ULL)) goto pa247;
        goto po244;
pa247:;
        goto mn243;
po244:;
        {
            void * t250 = gm_l0;
            void * t251 = gm_l3;
            gm_tkn42u7na3t81 t252;
            memset(&t252, 0, sizeof t252);
            void * t253 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t250, t251, t252);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l18 = t253;
        }
        {
            void * t255 = gm_l0;
            void * t256 = (void *)(((uint8_t *)t255) + 96);
            void * t257 = gm_l5;
            void * t258 = gm_l18;
            gm_t88rcucg1b7uk t259 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t258) + 0));
            gm_t9qkh0rsooc8d t260;
            GM_ST(uint8_t, ((uint8_t *)&t260 + 0), ((uint8_t)1ULL));
            uint8_t t261 = gm_f_require_unsigned_0fhlknt((void *)NULL, cx, NULL, t256, t257, t259, t260);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t262 = (uint8_t)!t261;
            if (!t262) goto ie254;
            void * t263 = gm_l5;
            void * t264 = gm_l18;
            gm_t88rcucg1b7uk t265 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t264) + 0));
            uint8_t t266 = gm_f_is_error_8k27gd1(t263, GM_CONV(gm_csguankko4lmhd, t265));
            uint8_t t267 = (uint8_t)!t266;
            if (!t267) goto ie254;
            {
                void * t269 = gm_l0;
                void * t270 = (void *)(((uint8_t *)t269) + 96);
                void * t271 = gm_l0;
                void * t272 = GM_LD(void *, (((uint8_t *)t271) + 0));
                void * t273 = gm_l18;
                gm_t88rcucg1b7uk t274 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t273) + 0));
                gm_t88rcucg1b7uk t275 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t270, t272, t274);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l19 = t275;
            }
            {
                void * t276 = gm_l0;
                void * t277 = gm_l18;
                gm_tl874t2atnlh3 t278 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t277) + 72));
                void * t279 = gm_l0;
                gm_t88rcucg1b7uk t280 = gm_l19;
                gm_b16_8 t281 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t279, t280);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tnpifv20irnph t282;
                GM_ST(gm_b16_8, ((uint8_t *)&t282 + 0), t281);
                gm_tabb710d9mq2e t283;
                gm_f_format_pdashts((void *)&t283, (void *)NULL, cx, NULL, t282);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t283))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t283) + 8, gm_srch2l5nl06vsci, 765, 34);
                gm_b16_8 t284 = GM_LD(gm_b16_8, (((uint8_t *)&t283) + 8));
                gm_b16_8 t285 = GM_LD(gm_b16_8, ((uint8_t *)&t284));
                gm_b0_1 t286 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t276, t278, t285);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix254;
ie254:;
ix254:;
        }
        {
            gm_ts6oqppjh0ba3 t288 = gm_l1;
            gm_t9qkh0rsooc8d t289;
            GM_ST(uint8_t, ((uint8_t *)&t289 + 0), ((uint8_t)3ULL));
            uint8_t t290 = (((GM_LD(uint8_t, ((uint8_t *)&t288) + 0) == ((uint8_t)3ULL)) && gm_eqehhdtpeti1((((uint8_t *)&t288) + 1), ((uint8_t *)&t289))));
            if (!t290) goto ie287;
            void * t291 = gm_l0;
            void * t292 = gm_l2;
            void * t293 = gm_l2;
            uint8_t t294 = gm_f_has_optional_operand_279ro7i((void *)NULL, cx, NULL, t291, t292, t293);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (!t294) goto ie287;
            {
                void * t296 = gm_l0;
                gm_ts6oqppjh0ba3 t297 = gm_l1;
                void * t298 = gm_l2;
                void * t299 = gm_l18;
                gm_tl874t2atnlh3 t300 = gm_l4;
                void * t301 = gm_f_chained_opt_op_nnt3ijn((void *)NULL, cx, NULL, t296, t297, t298, t299, t300);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t302 = 0;
                F.ret = t301;
                goto gm_fret;
            }
            goto ix287;
ie287:;
ix287:;
        }
        {
            void * t303 = gm_l0;
            void * t304 = (void *)(((uint8_t *)t303) + 320);
            gm_t91vm1g0k2rpm t305;
            memset((uint8_t *)&t305 + 34, 0, 6);
            void * t306 = gm_l2;
            gm_t88rcucg1b7uk t307 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t306) + 0));
            GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t305 + 0, t307);
            gm_tl874t2atnlh3 t308 = gm_l4;
            GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t305 + 4, t308);
            gm_ts6oqppjh0ba3 t309 = gm_l1;
            gm_b16_8 t310 = gm_f_binop_text_0ljfc0r(t309);
            GM_ST(gm_b16_8, (uint8_t *)&t305 + 16, t310);
            GM_ST(uint8_t, (uint8_t *)&t305 + 32, ((uint8_t)1));
            GM_ST(uint8_t, (uint8_t *)&t305 + 33, ((uint8_t)0));
            gm_tpm220j5qhjke t311;
            GM_ST(uint8_t, (uint8_t *)&t311 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t311 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t311 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t311 + 0), ((uint8_t)3ULL));
            GM_ST(gm_t91vm1g0k2rpm, ((uint8_t *)&t311 + 8), t305);
            gm_tftb1odc739t5 t312 = gm_f_push_7koe9lk(t304, &t311);
            if (((((uint8_t *)&t312))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t312) + 1, gm_srch2l5nl06vsci, 768, 13);
            gm_b0_1 t313 = GM_LD(gm_b0_1, (((uint8_t *)&t312) + 1));
        }
        {
            gm_ts6oqppjh0ba3 t314;
            uint8_t t316 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
            if (t316 != ((uint8_t)3ULL)) goto ie315;
            gm_t9qkh0rsooc8d t317 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l1) + 1));
            gm_l20 = t317;
            gm_t9qkh0rsooc8d t319 = gm_l20;
            gm_ts6oqppjh0ba3 t320;
            GM_ST(uint8_t, ((uint8_t *)&t320 + 0), ((uint8_t)3ULL));
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t320 + 1), t319);
            gm_ts6oqppjh0ba3 t321 = t320;
            t314 = t321;
            goto ix315;
ie315:;
            gm_ts6oqppjh0ba3 t323;
            GM_ST(uint8_t, (uint8_t *)&t323 + 1, 0);
            GM_ST(uint8_t, ((uint8_t *)&t323 + 0), ((uint8_t)6ULL));
            gm_ts6oqppjh0ba3 t324 = t323;
            t314 = t324;
ix315:;
            gm_l21 = t314;
        }
        {
            gm_t88rcucg1b7uk t325;
            gm_ts6oqppjh0ba3 t327 = gm_l1;
            gm_t9qkh0rsooc8d t328;
            GM_ST(uint8_t, ((uint8_t *)&t328 + 0), ((uint8_t)3ULL));
            uint8_t t329 = (((GM_LD(uint8_t, ((uint8_t *)&t327) + 0) == ((uint8_t)3ULL)) && gm_eqehhdtpeti1((((uint8_t *)&t327) + 1), ((uint8_t *)&t328))));
            if (!t329) goto ie326;
            void * t331 = gm_l5;
            void * t332 = gm_l2;
            gm_t88rcucg1b7uk t333 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t332) + 0));
            gm_t88rcucg1b7uk t334 = gm_f_optional_rh6okj3((void *)NULL, cx, NULL, t331, t333);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t88rcucg1b7uk t335 = t334;
            t325 = t335;
            goto ix326;
ie326:;
            void * t337 = gm_l2;
            gm_t88rcucg1b7uk t338 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t337) + 0));
            gm_t88rcucg1b7uk t339 = t338;
            t325 = t339;
ix326:;
            gm_l22 = t325;
        }
        {
            gm_t88rcucg1b7uk t340 = gm_l22;
            gm_trl9d7m1k5lbd t341;
            memset((uint8_t *)&t341 + 2, 0, 6);
            gm_ts6oqppjh0ba3 t342 = gm_l21;
            GM_ST(gm_ts6oqppjh0ba3, (uint8_t *)&t341 + 0, t342);
            void * t343 = gm_l2;
            GM_ST(void *, (uint8_t *)&t341 + 8, t343);
            void * t344 = gm_l18;
            GM_ST(void *, (uint8_t *)&t341 + 16, t344);
            gm_teti411eujvf4 t345;
            memset(&t345, 0, sizeof t345);
            GM_ST(uint8_t, ((uint8_t *)&t345 + 0), ((uint8_t)30ULL));
            GM_ST(gm_trl9d7m1k5lbd, ((uint8_t *)&t345 + 8), t341);
            gm_tl874t2atnlh3 t346 = gm_l4;
            void * t347 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t340, &t345, t346);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t348 = 0;
            F.ret = t347;
            goto gm_fret;
        }
        goto me61;
mn243:;
ma66:;
        uint8_t t352 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t352 != ((uint8_t)0ULL)) goto pa351;
        goto po350;
pa351:;
        uint8_t t354 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t354 != ((uint8_t)1ULL)) goto pa353;
        goto po350;
pa353:;
        uint8_t t356 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t356 != ((uint8_t)2ULL)) goto pa355;
        goto po350;
pa355:;
        uint8_t t358 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t358 != ((uint8_t)4ULL)) goto pa357;
        goto po350;
pa357:;
        uint8_t t360 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t360 != ((uint8_t)5ULL)) goto pa359;
        goto po350;
pa359:;
        uint8_t t362 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t362 != ((uint8_t)7ULL)) goto pa361;
        goto po350;
pa361:;
        uint8_t t364 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t364 != ((uint8_t)8ULL)) goto pa363;
        goto po350;
pa363:;
        uint8_t t366 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t366 != ((uint8_t)9ULL)) goto pa365;
        goto po350;
pa365:;
        goto mn349;
po350:;
        {
            void * t369 = gm_l5;
            gm_t88rcucg1b7uk t370 = gm_l6;
            gm_t53at80pfdus3 t371;
            gm_f_kind_jcnt4hm((void *)&t371, t369, GM_CONV(gm_csguankko4lmhd, t370));
            uint8_t t372;
            uint8_t t375 = GM_LD(uint8_t, (((uint8_t *)&t371) + 0));
            if (t375 != ((uint8_t)9ULL)) goto mn374;
            t372 = ((uint8_t)1);
            goto me373;
mn374:;
            t372 = ((uint8_t)0);
            goto me373;
mn376:;
            gm_trap("no match arm matched the scrutinee", gm_srch2l5nl06vsci, 774, 16);
me373:;
            if (!t372) goto ie368;
            gm_ts6oqppjh0ba3 t377 = gm_l1;
            gm_t9qkh0rsooc8d t378;
            GM_ST(uint8_t, ((uint8_t *)&t378 + 0), ((uint8_t)0ULL));
            gm_ts6oqppjh0ba3 t379;
            GM_ST(uint8_t, ((uint8_t *)&t379 + 0), ((uint8_t)0ULL));
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t379 + 1), t378);
            gm_t9qkh0rsooc8d t380;
            GM_ST(uint8_t, ((uint8_t *)&t380 + 0), ((uint8_t)0ULL));
            gm_ts6oqppjh0ba3 t381;
            GM_ST(uint8_t, ((uint8_t *)&t381 + 0), ((uint8_t)1ULL));
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t381 + 1), t380);
            gm_t9ldr1q5hn9fq t382;
            GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t382 + 0), t379);
            GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t382 + 2), t381);
            uint8_t t383 = 0;
            if (gm_eqbqb4gb1d92(((uint8_t *)&t382) + 0, ((uint8_t *)&t377))) t383 = 1;
            if (gm_eqbqb4gb1d92(((uint8_t *)&t382) + 2, ((uint8_t *)&t377))) t383 = 1;
            uint8_t t384 = (uint8_t)(t383);
            if (!t384) goto ie368;
            {
                void * t386 = gm_l0;
                gm_ts6oqppjh0ba3 t387 = gm_l1;
                void * t388 = gm_l2;
                void * t389 = gm_l3;
                gm_tl874t2atnlh3 t390 = gm_l4;
                void * t391 = gm_f_pointer_arith_abqsrkl((void *)NULL, cx, NULL, t386, t387, t388, t389, t390);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t392 = 0;
                F.ret = t391;
                goto gm_fret;
            }
            goto ix368;
ie368:;
ix368:;
        }
        {
            void * t394 = gm_l5;
            gm_t88rcucg1b7uk t395 = gm_l6;
            gm_t53at80pfdus3 t396;
            gm_f_kind_jcnt4hm((void *)&t396, t394, GM_CONV(gm_csguankko4lmhd, t395));
            uint8_t t397;
            uint8_t t403 = GM_LD(uint8_t, (((uint8_t *)&t396) + 0));
            switch (t403) { case ((uint8_t)0ULL): goto ma401; case ((uint8_t)1ULL): goto ma401; case ((uint8_t)2ULL): goto ma401; case ((uint8_t)3ULL): goto ma401; case ((uint8_t)4ULL): goto ma401; case ((uint8_t)5ULL): goto ma401; case ((uint8_t)6ULL): goto ma401; case ((uint8_t)7ULL): goto ma400; case ((uint8_t)8ULL): goto ma399; case ((uint8_t)9ULL): goto ma401; case ((uint8_t)10ULL): goto ma401; case ((uint8_t)11ULL): goto ma401; case ((uint8_t)12ULL): goto ma401; case ((uint8_t)13ULL): goto ma401; case ((uint8_t)14ULL): goto ma401; case ((uint8_t)15ULL): goto ma401; case ((uint8_t)16ULL): goto ma401; case ((uint8_t)17ULL): goto ma401; case ((uint8_t)18ULL): goto ma401; case ((uint8_t)19ULL): goto ma401; case ((uint8_t)20ULL): goto ma401; case ((uint8_t)21ULL): goto ma401; case ((uint8_t)22ULL): goto ma401; case ((uint8_t)23ULL): goto ma401; case ((uint8_t)24ULL): goto ma401; case ((uint8_t)25ULL): goto ma401; case ((uint8_t)26ULL): goto ma401; default: goto ma401; }
ma399:;
            uint8_t t405 = GM_LD(uint8_t, (((uint8_t *)&t396) + 0));
            if (t405 != ((uint8_t)8ULL)) goto mn404;
            t397 = ((uint8_t)1);
            goto me398;
mn404:;
ma400:;
            uint8_t t407 = GM_LD(uint8_t, (((uint8_t *)&t396) + 0));
            if (t407 != ((uint8_t)7ULL)) goto mn406;
            t397 = ((uint8_t)1);
            goto me398;
mn406:;
ma401:;
            t397 = ((uint8_t)0);
            goto me398;
mn408:;
ma402:;
            gm_trap("no match arm matched the scrutinee", gm_srch2l5nl06vsci, 775, 16);
me398:;
            if (!t397) goto ie393;
            uint8_t t409 = 0;
            uint8_t t410;
            uint8_t t413 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
            if (t413 != ((uint8_t)0ULL)) goto mn412;
            t410 = ((uint8_t)1);
            goto me411;
mn412:;
            t410 = ((uint8_t)0);
            goto me411;
mn414:;
            gm_trap("no match arm matched the scrutinee", gm_srch2l5nl06vsci, 775, 56);
me411:;
            if (!t410) {
                uint8_t t415;
                uint8_t t418 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                if (t418 != ((uint8_t)1ULL)) goto mn417;
                t415 = ((uint8_t)1);
                goto me416;
mn417:;
                t415 = ((uint8_t)0);
                goto me416;
mn419:;
                gm_trap("no match arm matched the scrutinee", gm_srch2l5nl06vsci, 775, 73);
me416:;
                t409 = t415;
            } else t409 = 1;
            if (!t409) goto ie393;
            {
                {
                    void * t422 = gm_l0;
                    void * t423 = gm_l3;
                    gm_tkn42u7na3t81 t424;
                    memset(&t424, 0, sizeof t424);
                    void * t425 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t422, t423, t424);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
            }
            {
                void * t426 = gm_l0;
                gm_tl874t2atnlh3 t427 = gm_l4;
                void * t428 = gm_l0;
                gm_t88rcucg1b7uk t429 = gm_l6;
                gm_b16_8 t430 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t428, t429);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tnpifv20irnph t431;
                GM_ST(gm_b16_8, ((uint8_t *)&t431 + 0), t430);
                gm_tabb710d9mq2e t432;
                gm_f_format_8oppoek((void *)&t432, (void *)NULL, cx, NULL, t431);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t432))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t432) + 8, gm_srch2l5nl06vsci, 777, 32);
                gm_b16_8 t433 = GM_LD(gm_b16_8, (((uint8_t *)&t432) + 8));
                gm_b16_8 t434 = GM_LD(gm_b16_8, ((uint8_t *)&t433));
                gm_b0_1 t435 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t426, t427, t434);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t436 = gm_l0;
                void * t437 = GM_LD(void *, (((uint8_t *)t436) + 0));
                gm_tl874t2atnlh3 t438 = gm_l4;
                void * t439 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t437, t438);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t440 = 0;
                F.ret = t439;
                goto gm_fret;
            }
            goto ix393;
ie393:;
ix393:;
        }
        {
            void * t441 = gm_l2;
            gm_t88rcucg1b7uk t442 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t441) + 0));
            gm_l23 = t442;
        }
        {
            gm_ts6oqppjh0ba3 t444 = gm_l1;
            gm_t9qkh0rsooc8d t445;
            GM_ST(uint8_t, ((uint8_t *)&t445 + 0), ((uint8_t)3ULL));
            gm_ts6oqppjh0ba3 t446;
            GM_ST(uint8_t, ((uint8_t *)&t446 + 0), ((uint8_t)0ULL));
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t446 + 1), t445);
            gm_t9qkh0rsooc8d t447;
            GM_ST(uint8_t, ((uint8_t *)&t447 + 0), ((uint8_t)3ULL));
            gm_ts6oqppjh0ba3 t448;
            GM_ST(uint8_t, ((uint8_t *)&t448 + 0), ((uint8_t)1ULL));
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t448 + 1), t447);
            gm_t9qkh0rsooc8d t449;
            GM_ST(uint8_t, ((uint8_t *)&t449 + 0), ((uint8_t)3ULL));
            gm_ts6oqppjh0ba3 t450;
            GM_ST(uint8_t, ((uint8_t *)&t450 + 0), ((uint8_t)2ULL));
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t450 + 1), t449);
            gm_tl2e7spmb4osd t451;
            GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t451 + 0), t446);
            GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t451 + 2), t448);
            GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t451 + 4), t450);
            uint8_t t452 = 0;
            if (gm_eqbqb4gb1d92(((uint8_t *)&t451) + 0, ((uint8_t *)&t444))) t452 = 1;
            if (gm_eqbqb4gb1d92(((uint8_t *)&t451) + 2, ((uint8_t *)&t444))) t452 = 1;
            if (gm_eqbqb4gb1d92(((uint8_t *)&t451) + 4, ((uint8_t *)&t444))) t452 = 1;
            uint8_t t453 = (uint8_t)(t452);
            if (!t453) goto ie443;
            {
                void * t456 = gm_l5;
                gm_t88rcucg1b7uk t457 = gm_l6;
                gm_t53at80pfdus3 t458;
                gm_f_kind_jcnt4hm((void *)&t458, t456, GM_CONV(gm_csguankko4lmhd, t457));
                uint8_t t459 = GM_LD(uint8_t, (((uint8_t *)&t458) + 0));
                if (t459 != ((uint8_t)13ULL)) goto ie455;
                gm_t88rcucg1b7uk t460 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t458) + 8));
                gm_l24 = t460;
                {
                    gm_t88rcucg1b7uk t462 = gm_l24;
                    gm_l23 = t462;
                }
                goto ix455;
ie455:;
ix455:;
            }
            goto ix443;
ie443:;
ix443:;
        }
        {
            void * t463 = gm_l0;
            void * t464 = gm_l3;
            gm_t88rcucg1b7uk t465 = gm_l23;
            gm_tkn42u7na3t81 t466;
            GM_ST(uint8_t, (uint8_t *)&t466 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t466 + 6, 0);
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t466), t465);
            ((uint8_t *)&t466)[4] = 1;
            void * t467 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t463, t464, t466);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l25 = t467;
        }
        {
            void * t469 = gm_l0;
            void * t470 = gm_l25;
            gm_t88rcucg1b7uk t471 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t470) + 0));
            uint8_t t472 = gm_f_is_compound_number_kbipjl5((void *)NULL, cx, NULL, t469, t471);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (!t472) goto ie468;
            {
                void * t474 = gm_l0;
                gm_ts6oqppjh0ba3 t475 = gm_l1;
                void * t476 = gm_l2;
                void * t477 = gm_l25;
                gm_tl874t2atnlh3 t478 = gm_l4;
                void * t479 = gm_f_compound_number_binary_p7eerjs((void *)NULL, cx, NULL, t474, t475, t476, t477, t478);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t480 = 0;
                F.ret = t479;
                goto gm_fret;
            }
            goto ix468;
ie468:;
ix468:;
        }
        {
            void * t481 = gm_l0;
            gm_ts6oqppjh0ba3 t482 = gm_l1;
            void * t483 = gm_l2;
            void * t484 = gm_l25;
            gm_tl874t2atnlh3 t485 = gm_l4;
            void * t486 = gm_f_arith_op_hsstk43((void *)NULL, cx, NULL, t481, t482, t483, t484, t485);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t487 = 0;
            F.ret = t486;
            goto gm_fret;
        }
        goto me61;
mn349:;
ma67:;
        uint8_t t491 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t491 != ((uint8_t)14ULL)) goto pa490;
        goto po489;
pa490:;
        uint8_t t493 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t493 != ((uint8_t)15ULL)) goto pa492;
        goto po489;
pa492:;
        goto mn488;
po489:;
        void * t494 = gm_l0;
        void * t495 = gm_l2;
        void * t496 = gm_l3;
        uint8_t t497 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l1) + 0) == ((uint8_t)15ULL)));
        gm_tl874t2atnlh3 t498 = gm_l4;
        void * t499 = gm_f_in_expr_su8ofmp((void *)NULL, cx, NULL, t494, t495, t496, t497, t498);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t500 = 0;
        F.ret = t499;
        goto gm_fret;
        goto me61;
mn488:;
ma68:;
        uint8_t t504 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t504 != ((uint8_t)10ULL)) goto pa503;
        goto po502;
pa503:;
        uint8_t t506 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t506 != ((uint8_t)11ULL)) goto pa505;
        goto po502;
pa505:;
        goto mn501;
po502:;
        {
            void * t508 = gm_l0;
            gm_tl874t2atnlh3 t509 = gm_l4;
            gm_b16_8 t510;
            GM_ST(void *, (uint8_t *)&t510, (void *)(gm_sje5p1r4d76uk));
            GM_ST(uintptr_t, (uint8_t *)&t510 + 8, (uintptr_t)(56));
            gm_b0_1 t511 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t508, t509, t510);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t512 = gm_l0;
            void * t513 = GM_LD(void *, (((uint8_t *)t512) + 0));
            gm_tl874t2atnlh3 t514 = gm_l4;
            void * t515 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t513, t514);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t516 = 0;
            F.ret = t515;
            goto gm_fret;
        }
        goto me61;
mn501:;
ma69:;
        gm_trap("no match arm matched the scrutinee", gm_srch2l5nl06vsci, 728, 5);
me61:;
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
inline __attribute__((aligned(4))) void * gm_f_bits_convert_8rcef5o(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_bits_convert_8rcef5o F;
    void * gm_l0; /* v */
    gm_t88rcucg1b7uk gm_l1; /* to */
    GM_STACK_CHECK(&F, 192u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_t88rcucg1b7uk t1 = gm_l1;
    gm_tt8o5kebou62r t2;
    memset((uint8_t *)&t2 + 12, 0, 4);
    gm_tvdg94bk7aa22 t3;
    GM_ST(uint8_t, (uint8_t *)&t3 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t3 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t3 + 4, 0);
    GM_ST(uint32_t, (uint8_t *)&t3 + 8, 0);
    GM_ST(uint8_t, ((uint8_t *)&t3 + 0), ((uint8_t)0ULL));
    GM_ST(gm_tvdg94bk7aa22, (uint8_t *)&t2 + 0, t3);
    void * t4 = gm_l0;
    gm_b16_8 t5 = gm_f_one_expr_q6kn0hn((void *)NULL, cx, NULL, t4);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    GM_ST(gm_b16_8, (uint8_t *)&t2 + 16, t5);
    gm_teti411eujvf4 t6;
    GM_ST(uint8_t, (uint8_t *)&t6 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t6 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t6 + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&t6 + 40, 0);
    GM_ST(uint64_t, (uint8_t *)&t6 + 48, 0);
    GM_ST(uint64_t, (uint8_t *)&t6 + 56, 0);
    GM_ST(uint8_t, ((uint8_t *)&t6 + 0), ((uint8_t)33ULL));
    GM_ST(gm_tt8o5kebou62r, ((uint8_t *)&t6 + 8), t2);
    void * t7 = gm_l0;
    gm_tl874t2atnlh3 t8 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t7) + 72));
    void * t9 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t1, &t6, t8);
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
inline __attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_bits_operand_d1ppvrt(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, gm_tl874t2atnlh3 a3) {
    struct gm_fr_bits_operand_d1ppvrt F;
    void * gm_l0; /* cx */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_b16_8 gm_l2; /* op */
    gm_tl874t2atnlh3 gm_l3; /* span */
    gm_t88rcucg1b7uk gm_l4; /* u */
    GM_STACK_CHECK(&F, 248u);
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
        gm_t88rcucg1b7uk t4 = gm_l1;
        gm_tkn42u7na3t81 t5 = gm_f_bits_struct_ty_l1kjtap(t3, GM_CONV(gm_csguankko4lmhd, t4));
        gm_t88rcucg1b7uk t6;
        if (((((uint8_t *)&t5))[4] != 0)) {
            gm_t88rcucg1b7uk t7 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t5) + 0));
            t6 = t7;
        } else {
            gm_tkn42u7na3t81 t8;
            memset(&t8, 0, sizeof t8);
            uint8_t t9 = 0;
            F.ret = t8;
            goto gm_fret;
        }
        gm_l4 = t6;
    }
    {
        void * t11 = gm_l0;
        void * t12 = GM_LD(void *, (((uint8_t *)t11) + 0));
        gm_t88rcucg1b7uk t13 = gm_l1;
        uint8_t t14 = gm_f_is_plain_n60iihm(t12, GM_CONV(gm_csguankko4lmhd, t13));
        uint8_t t15 = (uint8_t)!t14;
        if (!t15) goto ie10;
        {
            void * t17 = gm_l0;
            gm_tl874t2atnlh3 t18 = gm_l3;
            gm_b16_8 t19 = gm_l2;
            gm_tnpifv20irnph t20;
            GM_ST(gm_b16_8, ((uint8_t *)&t20 + 0), t19);
            gm_tabb710d9mq2e t21;
            gm_f_format_a612q5e((void *)&t21, (void *)NULL, cx, NULL, t20);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t21))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t21) + 8, gm_srch2l5nl06vsci, 972, 24);
            gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)&t21) + 8));
            gm_b16_8 t23 = GM_LD(gm_b16_8, ((uint8_t *)&t22));
            gm_b0_1 t24 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t17, t18, t23);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix10;
ie10:;
ix10:;
    }
    {
        gm_t88rcucg1b7uk t25 = gm_l4;
        gm_tkn42u7na3t81 t26;
        GM_ST(uint8_t, (uint8_t *)&t26 + 5, 0);
        GM_ST(uint16_t, (uint8_t *)&t26 + 6, 0);
        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t26), t25);
        ((uint8_t *)&t26)[4] = 1;
        uint8_t t27 = 0;
        F.ret = t26;
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
__attribute__((aligned(4))) void gm_f_bits_padding_mask_d4bk9a4(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_bits_padding_mask_d4bk9a4 F;
    void * gm_l0; /* cx */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_b16_8 gm_l2; /* fields */
    uint8_t gm_l3; /* padded */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    uint64_t gm_l8; /* off */
    gm_b16_8 gm_l9; /* <tmp> */
    uint64_t gm_l10; /* <tmp> */
    uint64_t gm_l12; /* w */
    GM_STACK_CHECK(&F, 958u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 0));
        gm_t88rcucg1b7uk t4 = gm_l1;
        gm_th2lqbo77pvsc t5;
        gm_f_struct_fields_chqu3it((void *)&t5, t3, GM_CONV(gm_csguankko4lmhd, t4));
        gm_b16_8 t6;
        if (((((uint8_t *)&t5))[16] != 0)) {
            gm_b16_8 t7 = GM_LD(gm_b16_8, (((uint8_t *)&t5) + 0));
            t6 = t7;
        } else {
            gm_thrdpodrs8pgt t8;
            memset(&t8, 0, sizeof t8);
            uint8_t t9 = 0;
            F.ret = t8;
            goto gm_fret;
        }
        gm_l2 = t6;
    }
    {
        gm_l3 = ((uint8_t)0);
    }
    {
        {
            gm_b16_8 t11 = gm_l2;
            gm_l4 = t11;
        }
        {
            gm_l5 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t13 = gm_l5;
                uint64_t t14 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                uint8_t t15 = (t13 < t14);
                if (!t15) goto lb12;
                {
                    uint64_t t18 = gm_l5;
                    uint64_t t19 = (uintptr_t)t18;
                    uint64_t t20 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                    gm_t0mglksrkfa5e t23 = GM_LD(gm_t0mglksrkfa5e, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t19 * 40));
                    F.l6 = t23;
                    {
                        uint8_t t26 = (((GM_LD(uintptr_t, (((uint8_t *)&F.l6) + 0) + 8) == 1) && memcmp(GM_LD(void *, (((uint8_t *)&F.l6) + 0)), gm_sfdbfrv46lihd, 1) == 0));
                        if (!t26) goto ie25;
                        {
                            gm_l3 = ((uint8_t)1);
                        }
                        goto ix25;
ie25:;
ix25:;
                    }
                    goto me21;
mn22:;
                    gm_trap("no match arm matched the scrutinee", gm_srch2l5nl06vsci, 953, 5);
me21:;
                }
lc12:;
                {
                    uint64_t t29 = gm_l5;
                    uint64_t r30; bool o31 = __builtin_add_overflow(t29, ((uint64_t)1ULL), &r30);
                    uint64_t t32 = r30;
                    gm_l5 = t32;
                }
            }
lb12:;
        }
    }
    {
        uint8_t t34 = gm_l3;
        uint8_t t35 = (uint8_t)!t34;
        if (!t35) goto ie33;
        {
            gm_thrdpodrs8pgt t37;
            memset(&t37, 0, sizeof t37);
            uint8_t t38 = 0;
            F.ret = t37;
            goto gm_fret;
        }
        goto ix33;
ie33:;
ix33:;
    }
    {
        gm_f_zero_s25l990((void *)&F.l7, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_l8 = ((uint64_t)0ULL);
    }
    {
        {
            gm_b16_8 t40 = gm_l2;
            gm_l9 = t40;
        }
        {
            gm_l10 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t42 = gm_l10;
                uint64_t t43 = GM_LD(uintptr_t, ((uint8_t *)&gm_l9) + 8);
                uint8_t t44 = (t42 < t43);
                if (!t44) goto lb41;
                {
                    uint64_t t47 = gm_l10;
                    uint64_t t48 = (uintptr_t)t47;
                    uint64_t t49 = GM_LD(uintptr_t, ((uint8_t *)&gm_l9) + 8);
                    gm_t0mglksrkfa5e t52 = GM_LD(gm_t0mglksrkfa5e, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l9)) + t48 * 40));
                    F.l11 = t52;
                    {
                        void * t54 = gm_l0;
                        void * t55 = GM_LD(void *, (((uint8_t *)t54) + 0));
                        gm_t88rcucg1b7uk t56 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l11) + 16));
                        gm_tf5srh4mp4b3i t57 = gm_f_bit_width_b7ofh2h(t55, GM_CONV(gm_csguankko4lmhd, t56));
                        uint64_t t58;
                        if (((((uint8_t *)&t57))[8] != 0)) {
                            uint64_t t59 = GM_LD(uint64_t, (((uint8_t *)&t57) + 0));
                            t58 = t59;
                        } else {
                            gm_thrdpodrs8pgt t60;
                            memset(&t60, 0, sizeof t60);
                            uint8_t t61 = 0;
                            F.ret = t60;
                            goto gm_fret;
                        }
                        gm_l12 = t58;
                    }
                    {
                        uint8_t t63 = (!((GM_LD(uintptr_t, (((uint8_t *)&F.l11) + 0) + 8) == 1) && memcmp(GM_LD(void *, (((uint8_t *)&F.l11) + 0)), gm_sfdbfrv46lihd, 1) == 0));
                        if (!t63) goto ie62;
                        {
                            gm_tbci5ospk5nnn t65 = F.l7;
                            gm_tbci5ospk5nnn t66;
                            gm_f_one_08qg1op((void *)&t66, (void *)NULL, cx, NULL);
                            if (gm_unw.kind) {
                                goto gm_ll45;
                            }
                            uint64_t t67 = gm_l12;
                            gm_tbci5ospk5nnn t68;
                            gm_f_shl_qqecl81((void *)&t68, (void *)NULL, cx, NULL, &t66, t67);
                            if (gm_unw.kind) {
                                goto gm_ll45;
                            }
                            gm_tbci5ospk5nnn t69;
                            gm_f_one_08qg1op((void *)&t69, (void *)NULL, cx, NULL);
                            if (gm_unw.kind) {
                                goto gm_ll45;
                            }
                            gm_tbci5ospk5nnn t70;
                            gm_f_sub_pktm5cr((void *)&t70, (void *)NULL, cx, NULL, &t68, &t69);
                            if (gm_unw.kind) {
                                goto gm_ll45;
                            }
                            uint64_t t71 = gm_l8;
                            gm_tbci5ospk5nnn t72;
                            gm_f_shl_qqecl81((void *)&t72, (void *)NULL, cx, NULL, &t70, t71);
                            if (gm_unw.kind) {
                                goto gm_ll45;
                            }
                            gm_f_bit_or_j1tdrhl((void *)&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l7)), (void *)NULL, cx, NULL, &t65, &t72);
                            if (gm_unw.kind) {
                                goto gm_ll45;
                            }
                        }
                        goto ix62;
ie62:;
ix62:;
                    }
                    {
                        uint64_t t73 = gm_l8;
                        uint64_t t74 = gm_l12;
                        uint64_t r75; bool o76 = __builtin_add_overflow(t73, t74, &r75);
                        if (o76) gm_trap("integer overflow in `+`", gm_srch2l5nl06vsci, 962, 9);
                        uint64_t t77 = r75;
                        gm_l8 = t77;
                    }
                    goto me50;
mn51:;
                    gm_trap("no match arm matched the scrutinee", gm_srch2l5nl06vsci, 959, 5);
me50:;
                }
lc41:;
                {
                    uint64_t t79 = gm_l10;
                    uint64_t r80; bool o81 = __builtin_add_overflow(t79, ((uint64_t)1ULL), &r80);
                    uint64_t t82 = r80;
                    gm_l10 = t82;
                }
            }
gm_ll45:;
            if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb41; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc41; }
            }
            goto gm_lad;
lb41:;
        }
    }
    {
        const uint8_t *t83 = ((uint8_t *)&F.l7);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_thrdpodrs8pgt *)gm_out) + 49, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_thrdpodrs8pgt *)gm_out) + 50, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_thrdpodrs8pgt *)gm_out) + 52, 0);
        ((uint8_t *)&(*(gm_thrdpodrs8pgt *)gm_out))[48] = 1;
        memmove((uint8_t *)&(*(gm_thrdpodrs8pgt *)gm_out) + 0, t83, 48);
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
__attribute__((aligned(4))) void * gm_f_chained_opt_op_nnt3ijn(void *env, gm_node *cx, gm_node *inst, void * a0, gm_ts6oqppjh0ba3 a1, void * a2, void * a3, gm_tl874t2atnlh3 a4) {
    struct gm_fr_chained_opt_op_nnt3ijn F;
    void * gm_l0; /* cx */
    gm_ts6oqppjh0ba3 gm_l1; /* op */
    void * gm_l2; /* lhs */
    void * gm_l3; /* r */
    gm_tl874t2atnlh3 gm_l4; /* span */
    void * gm_l5; /* tys */
    gm_t3mf58gv9kf3n gm_l9; /* <tmp> */
    gm_t5fru0lp11n7t gm_l10; /* <tmp> */
    gm_b16_8 gm_l11; /* <tmp> */
    uint64_t gm_l12; /* <tmp> */
    void * gm_l13; /* e */
    gm_t88rcucg1b7uk gm_l14; /* t */
    uint32_t gm_l15; /* tmp */
    void * gm_l16; /* value */
    gm_t88rcucg1b7uk gm_l17; /* inner */
    uint32_t gm_l18; /* x */
    void * gm_l19; /* bind */
    void * gm_l20; /* l */
    void * gm_l21; /* rv */
    void * gm_l22; /* then */
    gm_t88rcucg1b7uk gm_l23; /* ty */
    void * gm_l24; /* test */
    GM_STACK_CHECK(&F, 1940u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    {
        void * t2 = gm_l0;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 0));
        void * t4 = (void *)(((uint8_t *)t3) + 8);
        gm_l5 = t4;
    }
    {
        gm_f_list_ir0h3tc_2((void *)&F.l6, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_f_list_v5d1o9b_2((void *)&F.l7, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_f_list_s1kpkpr((void *)&F.l8, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            void * t6 = gm_l2;
            void * t7 = gm_l3;
            gm_t3mf58gv9kf3n t8;
            GM_ST(void *, ((uint8_t *)&t8 + 0), t6);
            GM_ST(void *, ((uint8_t *)&t8 + 8), t7);
            gm_l9 = t8;
        }
        {
            {
                {
                    void * t11 = GM_LD(void *, (((uint8_t *)&gm_l9) + 0));
                    void * t12 = GM_LD(void *, (((uint8_t *)&gm_l9) + 8));
                    gm_t5fru0lp11n7t t13;
                    GM_ST(void *, ((uint8_t *)&t13 + 0), t11);
                    GM_ST(void *, ((uint8_t *)&t13 + 8), t12);
                    gm_l10 = t13;
                }
                void * t14 = ((uint8_t *)&gm_l10);
                if ((uintptr_t)0 > (uintptr_t)2 || (uintptr_t)2 > (uintptr_t)2) gm_trap_slice((uintptr_t)0, (uintptr_t)2, 2, gm_srch2l5nl06vsci, 877, 5);
                gm_b16_8 t15;
                GM_ST(void *, (uint8_t *)&t15, (void *)(((uint8_t *)t14 + (uintptr_t)0 * 8)));
                GM_ST(uintptr_t, (uint8_t *)&t15 + 8, (uintptr_t)((uintptr_t)2 - (uintptr_t)0));
                gm_b16_8 t16 = t15;
                gm_l11 = t16;
            }
            {
                gm_l12 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t18 = gm_l12;
                    uint64_t t19 = GM_LD(uintptr_t, ((uint8_t *)&gm_l11) + 8);
                    uint8_t t20 = (t18 < t19);
                    if (!t20) goto lb17;
                    {
                        uint64_t t23 = gm_l12;
                        uint64_t t24 = (uintptr_t)t23;
                        uint64_t t25 = GM_LD(uintptr_t, ((uint8_t *)&gm_l11) + 8);
                        void * t26 = GM_LD(void *, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l11)) + t24 * 8));
                        void * t29 = GM_LD(void *, ((uint8_t *)&t26));
                        gm_l13 = t29;
                        {
                            void * t31 = gm_l0;
                            void * t32 = (void *)(((uint8_t *)t31) + 96);
                            void * t33 = gm_l0;
                            void * t34 = GM_LD(void *, (((uint8_t *)t33) + 0));
                            void * t35 = gm_l13;
                            gm_t88rcucg1b7uk t36 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t35) + 0));
                            gm_t88rcucg1b7uk t37 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t32, t34, t36);
                            if (gm_unw.kind) {
                                goto gm_ll21;
                            }
                            gm_l14 = t37;
                        }
                        {
                            void * t38 = gm_l0;
                            gm_t88rcucg1b7uk t39 = gm_l14;
                            void * t40 = gm_l13;
                            gm_tl874t2atnlh3 t41 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t40) + 72));
                            uint32_t t42 = gm_f_temp_ilbvj8q((void *)NULL, cx, NULL, t38, t39, t41);
                            if (gm_unw.kind) {
                                goto gm_ll21;
                            }
                            gm_l15 = t42;
                        }
                        {
                            void * t43 = (void *)((uint8_t *)&F.l6);
                            uint32_t t44 = gm_l15;
                            void * t45 = gm_l13;
                            gm_ttggq7ng4kp3p t46;
                            GM_ST(uint32_t, (uint8_t *)&t46 + 4, 0);
                            GM_ST(uint32_t, ((uint8_t *)&t46 + 0), t44);
                            GM_ST(void *, ((uint8_t *)&t46 + 8), t45);
                            gm_trcmmd12cefb1 t47;
                            GM_ST(uint8_t, (uint8_t *)&t47 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t47 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t47 + 4, 0);
                            GM_ST(uint64_t, (uint8_t *)&t47 + 24, 0);
                            GM_ST(uint8_t, ((uint8_t *)&t47 + 0), ((uint8_t)0ULL));
                            GM_ST(gm_ttggq7ng4kp3p, ((uint8_t *)&t47 + 8), t46);
                            gm_tftb1odc739t5 t48 = gm_f_push_d6c76ud_2(t43, &t47);
                            if (((((uint8_t *)&t48))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t48) + 1, gm_srch2l5nl06vsci, 880, 9);
                            gm_b0_1 t49 = GM_LD(gm_b0_1, (((uint8_t *)&t48) + 1));
                        }
                        {
                            gm_t88rcucg1b7uk t50 = gm_l14;
                            uint32_t t51 = gm_l15;
                            gm_teti411eujvf4 t52;
                            memset(&t52, 0, sizeof t52);
                            GM_ST(uint8_t, ((uint8_t *)&t52 + 0), ((uint8_t)15ULL));
                            GM_ST(uint32_t, ((uint8_t *)&t52 + 8), t51);
                            void * t53 = gm_l13;
                            gm_tl874t2atnlh3 t54 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t53) + 72));
                            void * t55 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t50, &t52, t54);
                            if (gm_unw.kind) {
                                goto gm_ll21;
                            }
                            gm_l16 = t55;
                        }
                        {
                            void * t57 = gm_l5;
                            gm_t88rcucg1b7uk t58 = gm_l14;
                            gm_t53at80pfdus3 t59;
                            gm_f_kind_jcnt4hm((void *)&t59, t57, GM_CONV(gm_csguankko4lmhd, t58));
                            uint8_t t60 = GM_LD(uint8_t, (((uint8_t *)&t59) + 0));
                            if (t60 != ((uint8_t)13ULL)) goto ie56;
                            gm_t88rcucg1b7uk t61 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t59) + 8));
                            gm_l17 = t61;
                            {
                                void * t63 = gm_l0;
                                gm_t88rcucg1b7uk t64 = gm_l17;
                                void * t65 = gm_l13;
                                gm_tl874t2atnlh3 t66 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t65) + 72));
                                uint32_t t67 = gm_f_temp_ilbvj8q((void *)NULL, cx, NULL, t63, t64, t66);
                                if (gm_unw.kind) {
                                    goto gm_ll21;
                                }
                                gm_l18 = t67;
                            }
                            {
                                gm_t88rcucg1b7uk t68 = gm_l17;
                                uint32_t t69 = gm_l18;
                                gm_tb38v5s9j1r32 t70;
                                memset(&t70, 0, sizeof t70);
                                GM_ST(uint8_t, ((uint8_t *)&t70 + 0), ((uint8_t)1ULL));
                                GM_ST(uint32_t, ((uint8_t *)&t70 + 8), t69);
                                void * t71 = gm_l13;
                                gm_tl874t2atnlh3 t72 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t71) + 72));
                                void * t73 = gm_f_mk_pat_hr2l6c4((void *)NULL, cx, NULL, t68, &t70, t72);
                                if (gm_unw.kind) {
                                    goto gm_ll21;
                                }
                                gm_l19 = t73;
                            }
                            {
                                void * t74 = (void *)((uint8_t *)&F.l7);
                                gm_t88rcucg1b7uk t75 = gm_l14;
                                void * t76 = gm_l19;
                                gm_tb38v5s9j1r32 t77;
                                memset(&t77, 0, sizeof t77);
                                GM_ST(uint8_t, ((uint8_t *)&t77 + 0), ((uint8_t)5ULL));
                                GM_ST(void *, ((uint8_t *)&t77 + 8), t76);
                                void * t78 = gm_l13;
                                gm_tl874t2atnlh3 t79 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t78) + 72));
                                void * t80 = gm_f_mk_pat_hr2l6c4((void *)NULL, cx, NULL, t75, &t77, t79);
                                if (gm_unw.kind) {
                                    goto gm_ll21;
                                }
                                void * t81 = gm_l16;
                                gm_t3mf58gv9kf3n t82;
                                GM_ST(void *, ((uint8_t *)&t82 + 0), t80);
                                GM_ST(void *, ((uint8_t *)&t82 + 8), t81);
                                gm_tlr9dp305htm7 t83;
                                GM_ST(uint8_t, (uint8_t *)&t83 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t83 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t83 + 4, 0);
                                GM_ST(uint8_t, ((uint8_t *)&t83 + 0), ((uint8_t)1ULL));
                                GM_ST(gm_t3mf58gv9kf3n, ((uint8_t *)&t83 + 8), t82);
                                gm_tftb1odc739t5 t84 = gm_f_push_eb7kbhp_2(t74, &t83);
                                if (((((uint8_t *)&t84))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t84) + 1, gm_srch2l5nl06vsci, 885, 13);
                                gm_b0_1 t85 = GM_LD(gm_b0_1, (((uint8_t *)&t84) + 1));
                            }
                            {
                                void * t86 = (void *)((uint8_t *)&F.l8);
                                gm_t88rcucg1b7uk t87 = gm_l17;
                                uint32_t t88 = gm_l18;
                                gm_teti411eujvf4 t89;
                                memset(&t89, 0, sizeof t89);
                                GM_ST(uint8_t, ((uint8_t *)&t89 + 0), ((uint8_t)15ULL));
                                GM_ST(uint32_t, ((uint8_t *)&t89 + 8), t88);
                                void * t90 = gm_l13;
                                gm_tl874t2atnlh3 t91 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t90) + 72));
                                void * t92 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t87, &t89, t91);
                                if (gm_unw.kind) {
                                    goto gm_ll21;
                                }
                                gm_tftb1odc739t5 t93 = gm_f_push_pf02nv6(t86, t92);
                                if (((((uint8_t *)&t93))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t93) + 1, gm_srch2l5nl06vsci, 886, 13);
                                gm_b0_1 t94 = GM_LD(gm_b0_1, (((uint8_t *)&t93) + 1));
                            }
                            goto ix56;
ie56:;
                            {
                                void * t96 = (void *)((uint8_t *)&F.l8);
                                void * t97 = gm_l16;
                                gm_tftb1odc739t5 t98 = gm_f_push_pf02nv6(t96, t97);
                                if (((((uint8_t *)&t98))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t98) + 1, gm_srch2l5nl06vsci, 888, 13);
                                gm_b0_1 t99 = GM_LD(gm_b0_1, (((uint8_t *)&t98) + 1));
                            }
ix56:;
                        }
                        goto me27;
mn28:;
                        gm_trap("no match arm matched the scrutinee", gm_srch2l5nl06vsci, 877, 5);
me27:;
                    }
lc17:;
                    {
                        uint64_t t101 = gm_l12;
                        uint64_t r102; bool o103 = __builtin_add_overflow(t101, ((uint64_t)1ULL), &r102);
                        uint64_t t104 = r102;
                        gm_l12 = t104;
                    }
                }
gm_ll21:;
                if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb17; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc17; }
                }
                goto gm_lad;
lb17:;
            }
        }
    }
    {
        uint64_t t105 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t106 = GM_LD(uintptr_t, (((uint8_t *)&F.l8) + 0) + 8);
        if (t105 >= (uintptr_t)(t106)) gm_trap_index(t105, t106, gm_srch2l5nl06vsci, 891, 10);
        void * t107 = GM_LD(void *, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l8) + 0)) + t105 * 8));
        gm_l20 = t107;
    }
    {
        uint64_t t108 = (uintptr_t)((uint64_t)1ULL);
        uint64_t t109 = GM_LD(uintptr_t, (((uint8_t *)&F.l8) + 0) + 8);
        if (t108 >= (uintptr_t)(t109)) gm_trap_index(t108, t109, gm_srch2l5nl06vsci, 892, 11);
        void * t110 = GM_LD(void *, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l8) + 0)) + t108 * 8));
        gm_l21 = t110;
    }
    {
        void * t111 = gm_l21;
        gm_l22 = t111;
    }
    {
        uint8_t t113;
        uint8_t t116 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t116 != ((uint8_t)3ULL)) goto mn115;
        t113 = ((uint8_t)1);
        goto me114;
mn115:;
        t113 = ((uint8_t)0);
        goto me114;
mn117:;
        gm_trap("no match arm matched the scrutinee", gm_srch2l5nl06vsci, 894, 8);
me114:;
        if (!t113) goto ie112;
        {
            void * t119 = gm_l0;
            void * t120 = (void *)(((uint8_t *)t119) + 320);
            gm_t91vm1g0k2rpm t121;
            memset((uint8_t *)&t121 + 34, 0, 6);
            void * t122 = gm_l20;
            gm_t88rcucg1b7uk t123 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t122) + 0));
            GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t121 + 0, t123);
            gm_tl874t2atnlh3 t124 = gm_l4;
            GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t121 + 4, t124);
            gm_ts6oqppjh0ba3 t125 = gm_l1;
            gm_b16_8 t126 = gm_f_binop_text_0ljfc0r(t125);
            GM_ST(gm_b16_8, (uint8_t *)&t121 + 16, t126);
            GM_ST(uint8_t, (uint8_t *)&t121 + 32, ((uint8_t)1));
            GM_ST(uint8_t, (uint8_t *)&t121 + 33, ((uint8_t)0));
            gm_tpm220j5qhjke t127;
            GM_ST(uint8_t, (uint8_t *)&t127 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t127 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t127 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t127 + 0), ((uint8_t)3ULL));
            GM_ST(gm_t91vm1g0k2rpm, ((uint8_t *)&t127 + 8), t121);
            gm_tftb1odc739t5 t128 = gm_f_push_7koe9lk(t120, &t127);
            if (((((uint8_t *)&t128))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t128) + 1, gm_srch2l5nl06vsci, 895, 9);
            gm_b0_1 t129 = GM_LD(gm_b0_1, (((uint8_t *)&t128) + 1));
        }
        {
            void * t130 = gm_l5;
            void * t131 = gm_l20;
            gm_t88rcucg1b7uk t132 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t131) + 0));
            gm_t88rcucg1b7uk t133 = gm_f_optional_rh6okj3((void *)NULL, cx, NULL, t130, t132);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_trl9d7m1k5lbd t134;
            memset((uint8_t *)&t134 + 2, 0, 6);
            gm_t9qkh0rsooc8d t135;
            GM_ST(uint8_t, ((uint8_t *)&t135 + 0), ((uint8_t)3ULL));
            gm_ts6oqppjh0ba3 t136;
            GM_ST(uint8_t, ((uint8_t *)&t136 + 0), ((uint8_t)3ULL));
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t136 + 1), t135);
            GM_ST(gm_ts6oqppjh0ba3, (uint8_t *)&t134 + 0, t136);
            void * t137 = gm_l20;
            GM_ST(void *, (uint8_t *)&t134 + 8, t137);
            void * t138 = gm_l21;
            GM_ST(void *, (uint8_t *)&t134 + 16, t138);
            gm_teti411eujvf4 t139;
            memset(&t139, 0, sizeof t139);
            GM_ST(uint8_t, ((uint8_t *)&t139 + 0), ((uint8_t)30ULL));
            GM_ST(gm_trl9d7m1k5lbd, ((uint8_t *)&t139 + 8), t134);
            gm_tl874t2atnlh3 t140 = gm_l4;
            void * t141 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t133, &t139, t140);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l22 = t141;
        }
        goto ix112;
ie112:;
        {
            void * t143 = gm_l0;
            gm_ts6oqppjh0ba3 t144 = gm_l1;
            void * t145 = gm_l20;
            void * t146 = gm_l21;
            gm_tl874t2atnlh3 t147 = gm_l4;
            void * t148 = gm_f_arith_op_hsstk43((void *)NULL, cx, NULL, t143, t144, t145, t146, t147);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l22 = t148;
        }
ix112:;
    }
    {
        void * t149 = gm_l22;
        gm_t88rcucg1b7uk t150 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t149) + 0));
        gm_l23 = t150;
    }
    {
        gm_t88rcucg1b7uk t151 = gm_l23;
        gm_trl2khmuphncd t152;
        gm_b16_8 t153 = GM_LD(gm_b16_8, (((uint8_t *)&F.l7) + 0));
        GM_ST(gm_b16_8, (uint8_t *)&t152 + 0, t153);
        void * t154 = gm_l22;
        GM_ST(void *, (uint8_t *)&t152 + 16, t154);
        gm_t88rcucg1b7uk t155 = gm_l23;
        gm_teti411eujvf4 t156;
        memset(&t156, 0, sizeof t156);
        GM_ST(uint8_t, ((uint8_t *)&t156 + 0), ((uint8_t)7ULL));
        gm_tl874t2atnlh3 t157 = gm_l4;
        void * t158 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t155, &t156, t157);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(void *, (uint8_t *)&t152 + 24, t158);
        gm_teti411eujvf4 t159;
        GM_ST(uint8_t, (uint8_t *)&t159 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t159 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t159 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t159 + 40, 0);
        GM_ST(uint64_t, (uint8_t *)&t159 + 48, 0);
        GM_ST(uint64_t, (uint8_t *)&t159 + 56, 0);
        GM_ST(uint8_t, ((uint8_t *)&t159 + 0), ((uint8_t)47ULL));
        GM_ST(gm_trl2khmuphncd, ((uint8_t *)&t159 + 8), t152);
        gm_tl874t2atnlh3 t160 = gm_l4;
        void * t161 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t151, &t159, t160);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l24 = t161;
    }
    {
        gm_t88rcucg1b7uk t162 = gm_l23;
        gm_b16_8 t163 = GM_LD(gm_b16_8, (((uint8_t *)&F.l6) + 0));
        void * t164 = gm_l24;
        gm_tl874t2atnlh3 t165 = gm_l4;
        void * t166 = gm_f_block_typed_e7svg7n((void *)NULL, cx, NULL, t162, t163, t164, t165);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t167 = 0;
        F.ret = t166;
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
inline __attribute__((aligned(4))) void * gm_f_coerce_element_9ue7d7u(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_tkn42u7na3t81 a2) {
    struct gm_fr_coerce_element_9ue7d7u F;
    void * gm_l0; /* cx */
    void * gm_l1; /* v */
    gm_tkn42u7na3t81 gm_l2; /* exp */
    void * gm_l3; /* tys */
    gm_t88rcucg1b7uk gm_l4; /* x */
    gm_t88rcucg1b7uk gm_l5; /* et */
    gm_t88rcucg1b7uk gm_l6; /* vt */
    GM_STACK_CHECK(&F, 393u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 0));
        void * t4 = (void *)(((uint8_t *)t3) + 8);
        gm_l3 = t4;
    }
    {
        gm_tkn42u7na3t81 t5 = gm_l2;
        gm_t88rcucg1b7uk t6;
        if (((((uint8_t *)&t5))[4] != 0)) {
            gm_t88rcucg1b7uk t7 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t5) + 0));
            t6 = t7;
        } else {
            void * t8 = gm_l1;
            uint8_t t9 = 0;
            F.ret = t8;
            goto gm_fret;
        }
        gm_l4 = t6;
    }
    {
        void * t10 = gm_l0;
        void * t11 = (void *)(((uint8_t *)t10) + 96);
        void * t12 = gm_l0;
        void * t13 = GM_LD(void *, (((uint8_t *)t12) + 0));
        gm_t88rcucg1b7uk t14 = gm_l4;
        gm_t88rcucg1b7uk t15 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t11, t13, t14);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l5 = t15;
    }
    {
        void * t17 = gm_l3;
        gm_t88rcucg1b7uk t18 = gm_l5;
        uint8_t t19 = gm_f_has_infer_3o82a0b(t17, GM_CONV(gm_csguankko4lmhd, t18));
        if (!t19) goto ie16;
        {
            void * t21 = gm_l1;
            uint8_t t22 = 0;
            F.ret = t21;
            goto gm_fret;
        }
        goto ix16;
ie16:;
ix16:;
    }
    {
        void * t23 = gm_l0;
        void * t24 = (void *)(((uint8_t *)t23) + 96);
        void * t25 = gm_l0;
        void * t26 = GM_LD(void *, (((uint8_t *)t25) + 0));
        void * t27 = gm_l1;
        gm_t88rcucg1b7uk t28 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t27) + 0));
        gm_t88rcucg1b7uk t29 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t24, t26, t28);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l6 = t29;
    }
    {
        gm_t88rcucg1b7uk t31 = gm_l6;
        gm_t88rcucg1b7uk t32 = gm_l5;
        uint8_t t33 = (!gm_eq9m6mvtmgde(((uint8_t *)&t31), ((uint8_t *)&t32)));
        if (!t33) goto ie30;
        uint8_t t34 = 0;
        void * t35 = gm_l3;
        gm_t88rcucg1b7uk t36 = gm_l6;
        gm_t53at80pfdus3 t37;
        gm_f_kind_jcnt4hm((void *)&t37, t35, GM_CONV(gm_csguankko4lmhd, t36));
        uint8_t t38;
        uint8_t t41 = GM_LD(uint8_t, (((uint8_t *)&t37) + 0));
        if (t41 != ((uint8_t)23ULL)) goto mn40;
        t38 = ((uint8_t)1);
        goto me39;
mn40:;
        t38 = ((uint8_t)0);
        goto me39;
mn42:;
        gm_trap("no match arm matched the scrutinee", gm_srch2l5nl06vsci, 481, 26);
me39:;
        if (!t38) {
            uint8_t t43 = 0;
            void * t44 = gm_l3;
            gm_t88rcucg1b7uk t45 = gm_l6;
            uint8_t t46 = gm_f_has_infer_3o82a0b(t44, GM_CONV(gm_csguankko4lmhd, t45));
            uint8_t t47 = (uint8_t)!t46;
            if (t47) {
                void * t48 = gm_l0;
                gm_t88rcucg1b7uk t49 = gm_l6;
                gm_t88rcucg1b7uk t50 = gm_l5;
                gm_th2lqbo77pvsc t51;
                gm_f_coercion_steps_ibqvhta((void *)&t51, (void *)NULL, cx, NULL, t48, t49, t50);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t52;
                if (((((uint8_t *)&t51))[16] != 0)) goto mn54;
                t52 = ((uint8_t)0);
                goto me53;
mn54:;
                t52 = ((uint8_t)1);
                goto me53;
mn55:;
                gm_trap("no match arm matched the scrutinee", gm_srch2l5nl06vsci, 481, 82);
me53:;
                t43 = t52;
            } else t43 = 0;
            t34 = t43;
        } else t34 = 1;
        if (!t34) goto ie30;
        {
            void * t57 = gm_l0;
            void * t58 = gm_l1;
            gm_t88rcucg1b7uk t59 = gm_l5;
            void * t60 = gm_f_coerce_sudc6c1((void *)NULL, cx, NULL, t57, t58, t59);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t61 = 0;
            F.ret = t60;
            goto gm_fret;
        }
        goto ix30;
ie30:;
ix30:;
    }
    {
        void * t62 = gm_l1;
        uint8_t t63 = 0;
        F.ret = t62;
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
__attribute__((aligned(4))) void * gm_f_decl_expr_lv2h8vf(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2, gm_tkn42u7na3t81 a3, gm_tl874t2atnlh3 a4) {
    struct gm_fr_decl_expr_lv2h8vf F;
    void * gm_l0; /* cx */
    uint64_t gm_l1; /* d */
    gm_b16_8 gm_l2; /* name */
    gm_tkn42u7na3t81 gm_l3; /* expect */
    gm_tl874t2atnlh3 gm_l4; /* span */
    uint32_t gm_l5; /* f */
    gm_t88rcucg1b7uk gm_l6; /* ty */
    gm_t88rcucg1b7uk gm_l8; /* ty */
    gm_tnpifv20irnph gm_l9; /* sys */
    GM_STACK_CHECK(&F, 1502u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    {
        void * t2 = gm_l0;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 0));
        uint64_t t4 = gm_l1;
        gm_tl874t2atnlh3 t5 = gm_l4;
        gm_b0_1 t6 = gm_f_check_deprecated_j3dq26t((void *)NULL, cx, NULL, t3, t4, t5);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t7 = gm_l0;
        void * t8 = GM_LD(void *, (((uint8_t *)t7) + 0));
        uint64_t t9 = gm_l1;
        gm_t6qooom8qa2do t10;
        gm_f_resolve_decl_pus042v((void *)&t10, (void *)NULL, cx, NULL, t8, t9);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t23 = GM_LD(uint8_t, (((uint8_t *)&t10) + 0));
        switch (t23) { case ((uint8_t)0ULL): goto ma15; case ((uint8_t)1ULL): goto ma15; case ((uint8_t)2ULL): goto ma16; case ((uint8_t)3ULL): goto ma16; case ((uint8_t)4ULL): goto ma17; case ((uint8_t)5ULL): goto ma12; case ((uint8_t)6ULL): goto ma18; case ((uint8_t)7ULL): goto ma13; case ((uint8_t)8ULL): goto ma14; case ((uint8_t)9ULL): goto ma19; case ((uint8_t)10ULL): goto ma20; case ((uint8_t)11ULL): goto ma21; default: goto ma22; }
ma12:;
        uint8_t t25 = GM_LD(uint8_t, (((uint8_t *)&t10) + 0));
        if (t25 != ((uint8_t)5ULL)) goto mn24;
        uint32_t t26 = GM_LD(uint32_t, (((uint8_t *)&t10) + 8));
        gm_l5 = t26;
        void * t27 = gm_l0;
        uint32_t t28 = gm_l5;
        gm_b16_8 t29 = gm_l2;
        gm_tkn42u7na3t81 t30 = gm_l3;
        gm_tl874t2atnlh3 t31 = gm_l4;
        void * t32 = gm_f_named_fn_value_b79g18s((void *)NULL, cx, NULL, t27, t28, t29, t30, t31);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t33 = 0;
        F.ret = t32;
        goto gm_fret;
        goto me11;
mn24:;
ma13:;
        uint8_t t35 = GM_LD(uint8_t, (((uint8_t *)&t10) + 0));
        if (t35 != ((uint8_t)7ULL)) goto mn34;
        gm_t88rcucg1b7uk t36 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t10) + 8));
        gm_l6 = t36;
        gm_t88rcucg1b7uk t37 = gm_l6;
        gm_t08olq97rscn3 t38;
        memset((uint8_t *)&t38 + 28, 0, 4);
        gm_b16_8 t39 = gm_l2;
        GM_ST(gm_b16_8, (uint8_t *)&t38 + 0, t39);
        uint64_t t40 = gm_l1;
        uint64_t t41 = (uintptr_t)t40;
        void * t42 = gm_l0;
        void * t43 = GM_LD(void *, (((uint8_t *)t42) + 0));
        void * t44 = GM_LD(void *, (((uint8_t *)t43) + 0));
        uint64_t t45 = GM_LD(uintptr_t, ((((uint8_t *)t44) + 240) + 0) + 8);
        if (t41 >= (uintptr_t)(t45)) gm_trap_index(t41, t45, gm_srch2l5nl06vsci, 386, 54);
        gm_tl874t2atnlh3 t46 = GM_LD(gm_tl874t2atnlh3, ((((uint8_t *)GM_LD(void *, ((((uint8_t *)t44) + 240) + 0)) + t41 * 216) + 0) + 16));
        GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t38 + 16, t46);
        gm_teti411eujvf4 t47;
        GM_ST(uint8_t, (uint8_t *)&t47 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t47 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t47 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t47 + 40, 0);
        GM_ST(uint64_t, (uint8_t *)&t47 + 48, 0);
        GM_ST(uint64_t, (uint8_t *)&t47 + 56, 0);
        GM_ST(uint8_t, ((uint8_t *)&t47 + 0), ((uint8_t)14ULL));
        GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t47 + 8), t38);
        gm_tl874t2atnlh3 t48 = gm_l4;
        void * t49 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t37, &t47, t48);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t50 = 0;
        F.ret = t49;
        goto gm_fret;
        goto me11;
mn34:;
ma14:;
        uint8_t t52 = GM_LD(uint8_t, (((uint8_t *)&t10) + 0));
        if (t52 != ((uint8_t)8ULL)) goto mn51;
        F.l7 = (*(gm_t0ialelq3ql1k *)(((uint8_t *)&t10) + 8));
        void * t53 = gm_l0;
        void * t54 = (void *)((uint8_t *)&F.l7);
        gm_tl874t2atnlh3 t55 = gm_l4;
        void * t56 = gm_f_untyped_literal_pfu1ft4((void *)NULL, cx, NULL, t53, t54, t55);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t57 = 0;
        F.ret = t56;
        goto gm_fret;
        goto me11;
mn51:;
ma15:;
        uint8_t t61 = GM_LD(uint8_t, (((uint8_t *)&t10) + 0));
        if (t61 != ((uint8_t)0ULL)) goto pa60;
        goto po59;
pa60:;
        uint8_t t63 = GM_LD(uint8_t, (((uint8_t *)&t10) + 0));
        if (t63 != ((uint8_t)1ULL)) goto pa62;
        goto po59;
pa62:;
        goto mn58;
po59:;
        void * t64 = gm_l0;
        gm_tl874t2atnlh3 t65 = gm_l4;
        gm_b16_8 t66 = gm_l2;
        gm_tnpifv20irnph t67;
        GM_ST(gm_b16_8, ((uint8_t *)&t67 + 0), t66);
        gm_tabb710d9mq2e t68;
        gm_f_format_r8e1h45((void *)&t68, (void *)NULL, cx, NULL, t67);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t68))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t68) + 8, gm_srch2l5nl06vsci, 388, 53);
        gm_b16_8 t69 = GM_LD(gm_b16_8, (((uint8_t *)&t68) + 8));
        gm_b16_8 t70 = GM_LD(gm_b16_8, ((uint8_t *)&t69));
        gm_b0_1 t71 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t64, t65, t70);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me11;
mn58:;
ma16:;
        uint8_t t75 = GM_LD(uint8_t, (((uint8_t *)&t10) + 0));
        if (t75 != ((uint8_t)2ULL)) goto pa74;
        goto po73;
pa74:;
        uint8_t t77 = GM_LD(uint8_t, (((uint8_t *)&t10) + 0));
        if (t77 != ((uint8_t)3ULL)) goto pa76;
        goto po73;
pa76:;
        goto mn72;
po73:;
        void * t78 = gm_l0;
        gm_tl874t2atnlh3 t79 = gm_l4;
        gm_b16_8 t80 = gm_l2;
        gm_tnpifv20irnph t81;
        GM_ST(gm_b16_8, ((uint8_t *)&t81 + 0), t80);
        gm_tabb710d9mq2e t82;
        gm_f_format_1p8frtu((void *)&t82, (void *)NULL, cx, NULL, t81);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t82))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t82) + 8, gm_srch2l5nl06vsci, 389, 54);
        gm_b16_8 t83 = GM_LD(gm_b16_8, (((uint8_t *)&t82) + 8));
        gm_b16_8 t84 = GM_LD(gm_b16_8, ((uint8_t *)&t83));
        gm_b0_1 t85 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t78, t79, t84);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me11;
mn72:;
ma17:;
        uint8_t t87 = GM_LD(uint8_t, (((uint8_t *)&t10) + 0));
        if (t87 != ((uint8_t)4ULL)) goto mn86;
        void * t88 = gm_l0;
        gm_tl874t2atnlh3 t89 = gm_l4;
        gm_b16_8 t90 = gm_l2;
        gm_tnpifv20irnph t91;
        GM_ST(gm_b16_8, ((uint8_t *)&t91 + 0), t90);
        gm_tabb710d9mq2e t92;
        gm_f_format_r6aoskm((void *)&t92, (void *)NULL, cx, NULL, t91);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t92))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t92) + 8, gm_srch2l5nl06vsci, 390, 41);
        gm_b16_8 t93 = GM_LD(gm_b16_8, (((uint8_t *)&t92) + 8));
        gm_b16_8 t94 = GM_LD(gm_b16_8, ((uint8_t *)&t93));
        gm_b0_1 t95 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t88, t89, t94);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me11;
mn86:;
ma18:;
        uint8_t t97 = GM_LD(uint8_t, (((uint8_t *)&t10) + 0));
        if (t97 != ((uint8_t)6ULL)) goto mn96;
        void * t98 = gm_l0;
        gm_tl874t2atnlh3 t99 = gm_l4;
        gm_b16_8 t100 = gm_l2;
        gm_tnpifv20irnph t101;
        GM_ST(gm_b16_8, ((uint8_t *)&t101 + 0), t100);
        gm_tabb710d9mq2e t102;
        gm_f_format_dukk0t3((void *)&t102, (void *)NULL, cx, NULL, t101);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t102))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t102) + 8, gm_srch2l5nl06vsci, 391, 34);
        gm_b16_8 t103 = GM_LD(gm_b16_8, (((uint8_t *)&t102) + 8));
        gm_b16_8 t104 = GM_LD(gm_b16_8, ((uint8_t *)&t103));
        gm_b0_1 t105 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t98, t99, t104);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me11;
mn96:;
ma19:;
        uint8_t t107 = GM_LD(uint8_t, (((uint8_t *)&t10) + 0));
        if (t107 != ((uint8_t)9ULL)) goto mn106;
        void * t108 = gm_l0;
        gm_tl874t2atnlh3 t109 = gm_l4;
        gm_b16_8 t110 = gm_l2;
        gm_tnpifv20irnph t111;
        GM_ST(gm_b16_8, ((uint8_t *)&t111 + 0), t110);
        gm_tabb710d9mq2e t112;
        gm_f_format_eadlofj((void *)&t112, (void *)NULL, cx, NULL, t111);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t112))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t112) + 8, gm_srch2l5nl06vsci, 392, 38);
        gm_b16_8 t113 = GM_LD(gm_b16_8, (((uint8_t *)&t112) + 8));
        gm_b16_8 t114 = GM_LD(gm_b16_8, ((uint8_t *)&t113));
        gm_b0_1 t115 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t108, t109, t114);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me11;
mn106:;
ma20:;
        uint8_t t117 = GM_LD(uint8_t, (((uint8_t *)&t10) + 0));
        if (t117 != ((uint8_t)10ULL)) goto mn116;
        gm_t88rcucg1b7uk t118 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t10) + 8));
        gm_l8 = t118;
        {
            void * t120 = gm_l0;
            void * t121 = GM_LD(void *, (((uint8_t *)t120) + 0));
            gm_tnpifv20irnph t122 = gm_f_sys_set_pqe8mto((void *)NULL, cx, NULL, t121);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l9 = t122;
        }
        {
            void * t123 = gm_l0;
            void * t124 = (void *)((uint8_t *)&gm_l9);
            gm_tl874t2atnlh3 t125 = gm_l4;
            gm_b0_1 t126 = gm_f_record_effects_0eel5vu((void *)NULL, cx, NULL, t123, t124, t125);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_t88rcucg1b7uk t127 = gm_l8;
            gm_t3877pk7lv748 t128;
            uint64_t t129 = gm_l1;
            uint64_t t130 = (uintptr_t)t129;
            void * t131 = gm_l0;
            void * t132 = GM_LD(void *, (((uint8_t *)t131) + 0));
            void * t133 = GM_LD(void *, (((uint8_t *)t132) + 0));
            uint64_t t134 = GM_LD(uintptr_t, ((((uint8_t *)t133) + 240) + 0) + 8);
            if (t130 >= (uintptr_t)(t134)) gm_trap_index(t130, t134, gm_srch2l5nl06vsci, 397, 45);
            gm_b16_8 t135 = GM_LD(gm_b16_8, ((((uint8_t *)GM_LD(void *, ((((uint8_t *)t133) + 240) + 0)) + t130 * 216) + 0) + 0));
            GM_ST(gm_b16_8, (uint8_t *)&t128 + 0, t135);
            uint64_t t136 = gm_l1;
            GM_ST(uint64_t, (uint8_t *)&t128 + 16, t136);
            gm_teti411eujvf4 t137;
            memset(&t137, 0, sizeof t137);
            GM_ST(uint8_t, ((uint8_t *)&t137 + 0), ((uint8_t)17ULL));
            GM_ST(gm_t3877pk7lv748, ((uint8_t *)&t137 + 8), t128);
            gm_tl874t2atnlh3 t138 = gm_l4;
            void * t139 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t127, &t137, t138);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t140 = 0;
            F.ret = t139;
            goto gm_fret;
        }
        goto me11;
mn116:;
ma21:;
        uint8_t t142 = GM_LD(uint8_t, (((uint8_t *)&t10) + 0));
        if (t142 != ((uint8_t)11ULL)) goto mn141;
        goto me11;
mn141:;
ma22:;
        gm_trap("no match arm matched the scrutinee", gm_srch2l5nl06vsci, 384, 5);
me11:;
    }
    {
        void * t144 = gm_l0;
        void * t145 = GM_LD(void *, (((uint8_t *)t144) + 0));
        gm_tl874t2atnlh3 t146 = gm_l4;
        void * t147 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t145, t146);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t148 = 0;
        F.ret = t147;
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
inline __attribute__((aligned(4))) void * gm_f_deref_expr_resldhc(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_tl874t2atnlh3 a2) {
    struct gm_fr_deref_expr_resldhc F;
    void * gm_l0; /* cx */
    void * gm_l1; /* p */
    gm_tl874t2atnlh3 gm_l2; /* span */
    void * gm_l3; /* v */
    gm_t88rcucg1b7uk gm_l4; /* pointee */
    gm_t88rcucg1b7uk gm_l5; /* t */
    GM_STACK_CHECK(&F, 574u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        void * t3 = gm_l1;
        gm_tkn42u7na3t81 t4;
        memset(&t4, 0, sizeof t4);
        void * t5 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t2, t3, t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l3 = t5;
    }
    {
        void * t6 = gm_l0;
        void * t7 = GM_LD(void *, (((uint8_t *)t6) + 0));
        void * t8 = (void *)(((uint8_t *)t7) + 8);
        void * t9 = gm_l0;
        void * t10 = (void *)(((uint8_t *)t9) + 96);
        void * t11 = gm_l0;
        void * t12 = GM_LD(void *, (((uint8_t *)t11) + 0));
        void * t13 = (void *)(((uint8_t *)t12) + 8);
        void * t14 = gm_l3;
        gm_t88rcucg1b7uk t15 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t14) + 0));
        gm_t88rcucg1b7uk t16 = gm_f_shallow_8ur803a((void *)NULL, cx, NULL, t10, t13, t15);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t53at80pfdus3 t17;
        gm_f_kind_jcnt4hm((void *)&t17, t8, GM_CONV(gm_csguankko4lmhd, t16));
        uint8_t t23 = GM_LD(uint8_t, (((uint8_t *)&t17) + 0));
        switch (t23) { case ((uint8_t)0ULL): goto ma21; case ((uint8_t)1ULL): goto ma21; case ((uint8_t)2ULL): goto ma21; case ((uint8_t)3ULL): goto ma21; case ((uint8_t)4ULL): goto ma21; case ((uint8_t)5ULL): goto ma21; case ((uint8_t)6ULL): goto ma21; case ((uint8_t)7ULL): goto ma21; case ((uint8_t)8ULL): goto ma19; case ((uint8_t)9ULL): goto ma21; case ((uint8_t)10ULL): goto ma21; case ((uint8_t)11ULL): goto ma21; case ((uint8_t)12ULL): goto ma21; case ((uint8_t)13ULL): goto ma21; case ((uint8_t)14ULL): goto ma21; case ((uint8_t)15ULL): goto ma21; case ((uint8_t)16ULL): goto ma21; case ((uint8_t)17ULL): goto ma21; case ((uint8_t)18ULL): goto ma21; case ((uint8_t)19ULL): goto ma21; case ((uint8_t)20ULL): goto ma21; case ((uint8_t)21ULL): goto ma21; case ((uint8_t)22ULL): goto ma21; case ((uint8_t)23ULL): goto ma21; case ((uint8_t)24ULL): goto ma21; case ((uint8_t)25ULL): goto ma21; case ((uint8_t)26ULL): goto ma20; default: goto ma21; }
ma19:;
        uint8_t t25 = GM_LD(uint8_t, (((uint8_t *)&t17) + 0));
        if (t25 != ((uint8_t)8ULL)) goto mn24;
        gm_t88rcucg1b7uk t26 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t17) + 8) + 4));
        gm_l4 = t26;
        gm_t88rcucg1b7uk t27 = gm_l4;
        void * t28 = gm_l3;
        gm_teti411eujvf4 t29;
        memset(&t29, 0, sizeof t29);
        GM_ST(uint8_t, ((uint8_t *)&t29 + 0), ((uint8_t)18ULL));
        GM_ST(void *, ((uint8_t *)&t29 + 8), t28);
        gm_tl874t2atnlh3 t30 = gm_l2;
        void * t31 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t27, &t29, t30);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t32 = 0;
        F.ret = t31;
        goto gm_fret;
        goto me18;
mn24:;
ma20:;
        uint8_t t34 = GM_LD(uint8_t, (((uint8_t *)&t17) + 0));
        if (t34 != ((uint8_t)26ULL)) goto mn33;
        void * t35 = gm_l0;
        void * t36 = GM_LD(void *, (((uint8_t *)t35) + 0));
        gm_tl874t2atnlh3 t37 = gm_l2;
        void * t38 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t36, t37);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t39 = 0;
        F.ret = t38;
        goto gm_fret;
        goto me18;
mn33:;
ma21:;
        {
            void * t42 = gm_l0;
            void * t43 = (void *)(((uint8_t *)t42) + 96);
            void * t44 = gm_l0;
            void * t45 = GM_LD(void *, (((uint8_t *)t44) + 0));
            void * t46 = gm_l3;
            gm_t88rcucg1b7uk t47 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t46) + 0));
            gm_t88rcucg1b7uk t48 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t43, t45, t47);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l5 = t48;
        }
        {
            void * t49 = gm_l0;
            gm_tl874t2atnlh3 t50 = gm_l2;
            void * t51 = gm_l0;
            gm_t88rcucg1b7uk t52 = gm_l5;
            gm_b16_8 t53 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t51, t52);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tnpifv20irnph t54;
            GM_ST(gm_b16_8, ((uint8_t *)&t54 + 0), t53);
            gm_tabb710d9mq2e t55;
            gm_f_format_ducddq0((void *)&t55, (void *)NULL, cx, NULL, t54);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t55))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t55) + 8, gm_srch2l5nl06vsci, 237, 28);
            gm_b16_8 t56 = GM_LD(gm_b16_8, (((uint8_t *)&t55) + 8));
            gm_b16_8 t57 = GM_LD(gm_b16_8, ((uint8_t *)&t56));
            gm_b0_1 t58 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t49, t50, t57);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t59 = gm_l0;
            void * t60 = GM_LD(void *, (((uint8_t *)t59) + 0));
            gm_tl874t2atnlh3 t61 = gm_l2;
            void * t62 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t60, t61);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t63 = 0;
            F.ret = t62;
            goto gm_fret;
        }
        goto me18;
mn40:;
ma22:;
        gm_trap("no match arm matched the scrutinee", gm_srch2l5nl06vsci, 232, 5);
me18:;
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
__attribute__((aligned(4))) uint8_t gm_f_embeds_2jpqm45(void * a0, gm_csguankko4lmhd a1, gm_csguankko4lmhd a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_embeds_2jpqm45 F;
    void * gm_l0; /* cx */
    gm_t88rcucg1b7uk gm_l1; /* src */
    gm_t88rcucg1b7uk gm_l2; /* target */
    void * gm_l3; /* tys */
    uint8_t gm_l4; /* sc */
    gm_t88rcucg1b7uk gm_l5; /* outer */
    uint8_t gm_l6; /* tc */
    gm_t88rcucg1b7uk gm_l7; /* inner */
    gm_t88rcucg1b7uk gm_l8; /* cur */
    uint64_t gm_l9; /* <tmp> */
    uint64_t gm_l10; /* <tmp> */
    uint32_t gm_l11; /* id */
    gm_b16_8 gm_l12; /* fields */
    GM_STACK_CHECK_SHALLOW(&F, 538u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    gm_l2 = GM_CONV(gm_t88rcucg1b7uk, a2);
    {
        void * t2 = gm_l0;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 0));
        void * t4 = (void *)(((uint8_t *)t3) + 8);
        gm_l3 = t4;
    }
    {
        void * t6 = gm_l3;
        gm_t88rcucg1b7uk t7 = gm_l1;
        gm_t53at80pfdus3 t8;
        gm_f_kind_jcnt4hm((void *)&t8, t6, GM_CONV(gm_csguankko4lmhd, t7));
        uint8_t t9 = GM_LD(uint8_t, (((uint8_t *)&t8) + 0));
        if (t9 != ((uint8_t)8ULL)) goto ie5;
        uint8_t t10 = GM_LD(uint8_t, ((((uint8_t *)&t8) + 8) + 0));
        gm_l4 = t10;
        gm_t88rcucg1b7uk t11 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t8) + 8) + 4));
        gm_l5 = t11;
        {
            void * t14 = gm_l3;
            gm_t88rcucg1b7uk t15 = gm_l2;
            gm_t53at80pfdus3 t16;
            gm_f_kind_jcnt4hm((void *)&t16, t14, GM_CONV(gm_csguankko4lmhd, t15));
            uint8_t t17 = GM_LD(uint8_t, (((uint8_t *)&t16) + 0));
            if (t17 != ((uint8_t)8ULL)) goto ie13;
            uint8_t t18 = GM_LD(uint8_t, ((((uint8_t *)&t16) + 8) + 0));
            gm_l6 = t18;
            gm_t88rcucg1b7uk t19 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t16) + 8) + 4));
            gm_l7 = t19;
            {
                uint8_t t22 = gm_l4;
                if (!t22) goto ie21;
                uint8_t t23 = gm_l6;
                uint8_t t24 = (uint8_t)!t23;
                if (!t24) goto ie21;
                {
                    uint8_t t26 = 0;
                    F.ret = ((uint8_t)0);
                    goto gm_fret;
                }
                goto ix21;
ie21:;
ix21:;
            }
            {
                gm_t88rcucg1b7uk t27 = gm_l5;
                gm_l8 = t27;
            }
            {
                {
                    gm_l10 = ((uint64_t)0ULL);
                }
                {
                    gm_l9 = ((uint64_t)64ULL);
                }
                {
                    for (;;) {
                        uint64_t t30 = gm_l10;
                        uint64_t t31 = gm_l9;
                        uint8_t t32 = (t30 < t31);
                        if (!t32) goto lb29;
                        {
                            uint64_t t35 = gm_l10;
                            {
                                void * t40 = gm_l3;
                                gm_t88rcucg1b7uk t41 = gm_l8;
                                gm_t53at80pfdus3 t42;
                                gm_f_kind_jcnt4hm((void *)&t42, t40, GM_CONV(gm_csguankko4lmhd, t41));
                                uint8_t t43 = GM_LD(uint8_t, (((uint8_t *)&t42) + 0));
                                if (t43 != ((uint8_t)17ULL)) goto ie39;
                                uint32_t t44 = GM_LD(uint32_t, ((((uint8_t *)&t42) + 8) + 0));
                                gm_l11 = t44;
                                {
                                    uint32_t t47 = gm_l11;
                                    uint64_t t48 = (uint64_t)t47;
                                    uint64_t t49 = (uintptr_t)t48;
                                    void * t50 = gm_l0;
                                    void * t51 = GM_LD(void *, (((uint8_t *)t50) + 0));
                                    uint64_t t52 = GM_LD(uintptr_t, ((((uint8_t *)t51) + 1088) + 0) + 8);
                                    if (t49 >= (uintptr_t)(t52)) gm_trap_index(t49, t52, gm_srch2l5nl06vsci, 1142, 16);
                                    uint8_t t53 = GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t51) + 1088) + 0)) + t49 * 184) + 112));
                                    uint8_t t54 = (uint8_t)!t53;
                                    if (!t54) goto ie46;
                                    {
                                        uint8_t t56 = 0;
                                        F.ret = ((uint8_t)0);
                                        goto gm_fret;
                                    }
                                    goto ix46;
ie46:;
ix46:;
                                }
                                {
                                    void * t57 = gm_l0;
                                    void * t58 = GM_LD(void *, (((uint8_t *)t57) + 0));
                                    gm_t88rcucg1b7uk t59 = gm_l8;
                                    gm_th2lqbo77pvsc t60;
                                    gm_f_struct_fields_chqu3it((void *)&t60, t58, GM_CONV(gm_csguankko4lmhd, t59));
                                    gm_b16_8 t61;
                                    if (((((uint8_t *)&t60))[16] != 0)) {
                                        gm_b16_8 t62 = GM_LD(gm_b16_8, (((uint8_t *)&t60) + 0));
                                        t61 = t62;
                                    } else {
                                        uint8_t t63 = 0;
                                        F.ret = ((uint8_t)0);
                                        goto gm_fret;
                                    }
                                    gm_l12 = t61;
                                }
                                {
                                    uint64_t t65 = GM_LD(uintptr_t, ((uint8_t *)&gm_l12) + 8);
                                    uint8_t t66 = ((t65 == ((uint64_t)0ULL)));
                                    if (!t66) goto ie64;
                                    {
                                        uint8_t t68 = 0;
                                        F.ret = ((uint8_t)0);
                                        goto gm_fret;
                                    }
                                    goto ix64;
ie64:;
ix64:;
                                }
                                {
                                    uint64_t t69 = (uintptr_t)((uint64_t)0ULL);
                                    uint64_t t70 = GM_LD(uintptr_t, ((uint8_t *)&gm_l12) + 8);
                                    if (t69 >= (uintptr_t)(t70)) gm_trap_index(t69, t70, gm_srch2l5nl06vsci, 1145, 15);
                                    gm_t88rcucg1b7uk t71 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l12)) + t69 * 40) + 16));
                                    gm_l8 = t71;
                                }
                                {
                                    gm_t88rcucg1b7uk t73 = gm_l8;
                                    gm_t88rcucg1b7uk t74 = gm_l7;
                                    uint8_t t75 = (gm_eq9m6mvtmgde(((uint8_t *)&t73), ((uint8_t *)&t74)));
                                    if (!t75) goto ie72;
                                    {
                                        uint8_t t77 = 0;
                                        F.ret = ((uint8_t)1);
                                        goto gm_fret;
                                    }
                                    goto ix72;
ie72:;
ix72:;
                                }
                                goto ix39;
ie39:;
                                {
                                    uint8_t t79 = 0;
                                    F.ret = ((uint8_t)0);
                                    goto gm_fret;
                                }
ix39:;
                            }
                            goto me36;
mn37:;
                            gm_trap("no match arm matched the scrutinee", gm_srch2l5nl06vsci, 1140, 5);
me36:;
                        }
lc29:;
                        {
                            uint64_t t81 = gm_l10;
                            uint64_t r82; bool o83 = __builtin_add_overflow(t81, ((uint64_t)1ULL), &r82);
                            uint64_t t84 = r82;
                            gm_l10 = t84;
                        }
                    }
lb29:;
                }
            }
            {
                uint8_t t85 = 0;
                F.ret = ((uint8_t)0);
                goto gm_fret;
            }
            goto ix13;
ie13:;
            {
                uint8_t t87 = 0;
                F.ret = ((uint8_t)0);
                goto gm_fret;
            }
ix13:;
        }
        goto ix5;
ie5:;
        {
            uint8_t t89 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
ix5:;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_eq_allowed_d841u9b(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_eq_allowed_d841u9b F;
    void * gm_l0; /* cx */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 123u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint8_t t1 = 0;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, (((uint8_t *)t2) + 0));
    gm_t88rcucg1b7uk t4 = gm_l1;
    uint8_t t5 = gm_f_has_eq_iba53hs((void *)NULL, cx, NULL, t3, t4);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    if (!t5) {
        uint8_t t6 = 0;
        void * t7 = gm_l0;
        uint64_t t8 = GM_LD(uint64_t, (((uint8_t *)t7) + 8));
        uint64_t t9 = (uintptr_t)t8;
        void * t10 = gm_l0;
        void * t11 = GM_LD(void *, (((uint8_t *)t10) + 0));
        void * t12 = GM_LD(void *, (((uint8_t *)t11) + 0));
        uint64_t t13 = GM_LD(uintptr_t, ((((uint8_t *)t12) + 200) + 0) + 8);
        if (t9 >= (uintptr_t)(t13)) gm_trap_index(t9, t13, gm_srch2l5nl06vsci, 1346, 78);
        uint8_t t14 = GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t12) + 200) + 0)) + t9 * 184) + 112));
        if (t14) {
            void * t15 = gm_l0;
            void * t16 = GM_LD(void *, (((uint8_t *)t15) + 0));
            void * t17 = (void *)(((uint8_t *)t16) + 8);
            gm_t88rcucg1b7uk t18 = gm_l1;
            uint8_t t19 = gm_f_has_param_u9t0efs(t17, GM_CONV(gm_csguankko4lmhd, t18));
            t6 = t19;
        } else t6 = 0;
        t1 = t6;
    } else t1 = 1;
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
__attribute__((aligned(4))) gm_t3mf58gv9kf3n gm_f_equalize_ni1r5ra(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2, gm_tl874t2atnlh3 a3) {
    struct gm_fr_equalize_ni1r5ra F;
    void * gm_l0; /* cx */
    void * gm_l1; /* l */
    void * gm_l2; /* r */
    gm_tl874t2atnlh3 gm_l3; /* span */
    gm_t88rcucg1b7uk gm_l5; /* lt */
    gm_t88rcucg1b7uk gm_l6; /* rt */
    gm_closure gm_l7; /* wraps */
    gm_t88rcucg1b7uk gm_l8; /* elem */
    void * gm_l9; /* p */
    gm_t88rcucg1b7uk gm_l10; /* elem */
    void * gm_l11; /* p */
    GM_STACK_CHECK(&F, 1527u);
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
        void * t4 = (void *)(((uint8_t *)t3) + 8);
        F.l4 = t4;
    }
    {
        void * t5 = gm_l0;
        void * t6 = (void *)(((uint8_t *)t5) + 96);
        void * t7 = gm_l0;
        void * t8 = GM_LD(void *, (((uint8_t *)t7) + 0));
        void * t9 = gm_l1;
        gm_t88rcucg1b7uk t10 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t9) + 0));
        gm_t88rcucg1b7uk t11 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t6, t8, t10);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l5 = t11;
    }
    {
        void * t12 = gm_l0;
        void * t13 = (void *)(((uint8_t *)t12) + 96);
        void * t14 = gm_l0;
        void * t15 = GM_LD(void *, (((uint8_t *)t14) + 0));
        void * t16 = gm_l2;
        gm_t88rcucg1b7uk t17 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t16) + 0));
        gm_t88rcucg1b7uk t18 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t13, t15, t17);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l6 = t18;
    }
    {
        void * t20 = F.l4;
        gm_t88rcucg1b7uk t21 = gm_l5;
        uint8_t t22 = gm_f_has_infer_3o82a0b(t20, GM_CONV(gm_csguankko4lmhd, t21));
        uint8_t t23 = (uint8_t)!t22;
        if (!t23) goto ie19;
        void * t24 = F.l4;
        gm_t88rcucg1b7uk t25 = gm_l5;
        uint8_t t26 = gm_f_is_error_8k27gd1(t24, GM_CONV(gm_csguankko4lmhd, t25));
        uint8_t t27 = (uint8_t)!t26;
        if (!t27) goto ie19;
        void * t28 = gm_l0;
        gm_t88rcucg1b7uk t29 = gm_l5;
        uint8_t t30 = gm_f_eq_allowed_d841u9b((void *)NULL, cx, NULL, t28, t29);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t31 = (uint8_t)!t30;
        if (!t31) goto ie19;
        {
            void * t33 = gm_l0;
            gm_tl874t2atnlh3 t34 = gm_l3;
            void * t35 = gm_l0;
            gm_t88rcucg1b7uk t36 = gm_l5;
            gm_b16_8 t37 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t35, t36);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tnpifv20irnph t38;
            GM_ST(gm_b16_8, ((uint8_t *)&t38 + 0), t37);
            gm_tabb710d9mq2e t39;
            gm_f_format_535vc4d((void *)&t39, (void *)NULL, cx, NULL, t38);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t39))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t39) + 8, gm_srch2l5nl06vsci, 986, 24);
            gm_b16_8 t40 = GM_LD(gm_b16_8, (((uint8_t *)&t39) + 8));
            gm_b16_8 t41 = GM_LD(gm_b16_8, ((uint8_t *)&t40));
            gm_b0_1 t42 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t33, t34, t41);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix19;
ie19:;
ix19:;
    }
    {
        gm_closure t43 = ((gm_closure){(void *)gm_f_wraps_mh8ot70, (void *)&F.h, cx});
        gm_l7 = t43;
    }
    {
        void * t45 = F.l4;
        gm_t88rcucg1b7uk t46 = gm_l6;
        gm_t53at80pfdus3 t47;
        gm_f_kind_jcnt4hm((void *)&t47, t45, GM_CONV(gm_csguankko4lmhd, t46));
        uint8_t t48;
        uint8_t t51 = GM_LD(uint8_t, (((uint8_t *)&t47) + 0));
        if (t51 != ((uint8_t)23ULL)) goto mn50;
        t48 = ((uint8_t)1);
        goto me49;
mn50:;
        t48 = ((uint8_t)0);
        goto me49;
mn52:;
        gm_trap("no match arm matched the scrutinee", gm_srch2l5nl06vsci, 990, 8);
me49:;
        if (!t48) goto ie44;
        gm_closure t53 = gm_l7;
        gm_t88rcucg1b7uk t54 = gm_l5;
        gm_closure t55 = t53;
        gm_node *ccx56 = t55.cx;
        void * t57 = t55.code;
        uint8_t t58;
        if (GM_PTR_TAGGED(t57)) {
            t58 = ((uint8_t (*)(gm_csguankko4lmhd))GM_PTR_UNTAG(t57))(GM_CONV(gm_csguankko4lmhd, t54));
        } else {
            t58 = ((uint8_t (*)(void *, gm_node *, gm_node *, gm_t88rcucg1b7uk))t57)(t55.env, ccx56, NULL, t54);
        }
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (!t58) goto ie44;
        {
            void * t60 = gm_l1;
            void * t61 = gm_l0;
            void * t62 = gm_l2;
            gm_t88rcucg1b7uk t63 = gm_l5;
            void * t64 = gm_f_coerce_sudc6c1((void *)NULL, cx, NULL, t61, t62, t63);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t3mf58gv9kf3n t65;
            GM_ST(void *, ((uint8_t *)&t65 + 0), t60);
            GM_ST(void *, ((uint8_t *)&t65 + 8), t64);
            uint8_t t66 = 0;
            F.ret = t65;
            goto gm_fret;
        }
        goto ix44;
ie44:;
ix44:;
    }
    {
        void * t68 = F.l4;
        gm_t88rcucg1b7uk t69 = gm_l5;
        gm_t53at80pfdus3 t70;
        gm_f_kind_jcnt4hm((void *)&t70, t68, GM_CONV(gm_csguankko4lmhd, t69));
        uint8_t t71;
        uint8_t t74 = GM_LD(uint8_t, (((uint8_t *)&t70) + 0));
        if (t74 != ((uint8_t)23ULL)) goto mn73;
        t71 = ((uint8_t)1);
        goto me72;
mn73:;
        t71 = ((uint8_t)0);
        goto me72;
mn75:;
        gm_trap("no match arm matched the scrutinee", gm_srch2l5nl06vsci, 991, 8);
me72:;
        if (!t71) goto ie67;
        gm_closure t76 = gm_l7;
        gm_t88rcucg1b7uk t77 = gm_l6;
        gm_closure t78 = t76;
        gm_node *ccx79 = t78.cx;
        void * t80 = t78.code;
        uint8_t t81;
        if (GM_PTR_TAGGED(t80)) {
            t81 = ((uint8_t (*)(gm_csguankko4lmhd))GM_PTR_UNTAG(t80))(GM_CONV(gm_csguankko4lmhd, t77));
        } else {
            t81 = ((uint8_t (*)(void *, gm_node *, gm_node *, gm_t88rcucg1b7uk))t80)(t78.env, ccx79, NULL, t77);
        }
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (!t81) goto ie67;
        {
            void * t83 = gm_l0;
            void * t84 = gm_l1;
            gm_t88rcucg1b7uk t85 = gm_l6;
            void * t86 = gm_f_coerce_sudc6c1((void *)NULL, cx, NULL, t83, t84, t85);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            void * t87 = gm_l2;
            gm_t3mf58gv9kf3n t88;
            GM_ST(void *, ((uint8_t *)&t88 + 0), t86);
            GM_ST(void *, ((uint8_t *)&t88 + 8), t87);
            uint8_t t89 = 0;
            F.ret = t88;
            goto gm_fret;
        }
        goto ix67;
ie67:;
ix67:;
    }
    {
        void * t91 = gm_l0;
        void * t92 = GM_LD(void *, (((uint8_t *)t91) + 0));
        gm_t88rcucg1b7uk t93 = gm_l5;
        gm_tkn42u7na3t81 t94 = gm_f_complex_of_kh32tib((void *)NULL, cx, NULL, t92, t93);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (!((((uint8_t *)&t94))[4] != 0)) goto ie90;
        gm_t88rcucg1b7uk t95 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&t94));
        gm_l8 = t95;
        {
            void * t98 = gm_l0;
            void * t99 = gm_l2;
            gm_t88rcucg1b7uk t100 = gm_l5;
            gm_t88rcucg1b7uk t101 = gm_l8;
            void * t102 = gm_f_promote_real_constant_v0nhili((void *)NULL, cx, NULL, t98, t99, t100, t101);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (!(GM_LD(void *, ((uint8_t *)&t102)) != 0)) goto ie97;
            void * t103 = GM_LD(void *, ((uint8_t *)&t102));
            gm_l9 = t103;
            {
                void * t105 = gm_l1;
                void * t106 = gm_l9;
                gm_t3mf58gv9kf3n t107;
                GM_ST(void *, ((uint8_t *)&t107 + 0), t105);
                GM_ST(void *, ((uint8_t *)&t107 + 8), t106);
                uint8_t t108 = 0;
                F.ret = t107;
                goto gm_fret;
            }
            goto ix97;
ie97:;
ix97:;
        }
        goto ix90;
ie90:;
ix90:;
    }
    {
        void * t110 = gm_l0;
        void * t111 = GM_LD(void *, (((uint8_t *)t110) + 0));
        gm_t88rcucg1b7uk t112 = gm_l6;
        gm_tkn42u7na3t81 t113 = gm_f_complex_of_kh32tib((void *)NULL, cx, NULL, t111, t112);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (!((((uint8_t *)&t113))[4] != 0)) goto ie109;
        gm_t88rcucg1b7uk t114 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&t113));
        gm_l10 = t114;
        {
            void * t117 = gm_l0;
            void * t118 = gm_l1;
            gm_t88rcucg1b7uk t119 = gm_l6;
            gm_t88rcucg1b7uk t120 = gm_l10;
            void * t121 = gm_f_promote_real_constant_v0nhili((void *)NULL, cx, NULL, t117, t118, t119, t120);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (!(GM_LD(void *, ((uint8_t *)&t121)) != 0)) goto ie116;
            void * t122 = GM_LD(void *, ((uint8_t *)&t121));
            gm_l11 = t122;
            {
                void * t124 = gm_l11;
                void * t125 = gm_l2;
                gm_t3mf58gv9kf3n t126;
                GM_ST(void *, ((uint8_t *)&t126 + 0), t124);
                GM_ST(void *, ((uint8_t *)&t126 + 8), t125);
                uint8_t t127 = 0;
                F.ret = t126;
                goto gm_fret;
            }
            goto ix116;
ie116:;
ix116:;
        }
        goto ix109;
ie109:;
ix109:;
    }
    {
        uint8_t t129 = 0;
        uint8_t t130 = 0;
        void * t131 = F.l4;
        gm_t88rcucg1b7uk t132 = gm_l5;
        uint8_t t133 = gm_f_has_infer_3o82a0b(t131, GM_CONV(gm_csguankko4lmhd, t132));
        if (!t133) {
            void * t134 = F.l4;
            gm_t88rcucg1b7uk t135 = gm_l6;
            uint8_t t136 = gm_f_has_infer_3o82a0b(t134, GM_CONV(gm_csguankko4lmhd, t135));
            t130 = t136;
        } else t130 = 1;
        if (!t130) {
            gm_t88rcucg1b7uk t137 = gm_l5;
            gm_t88rcucg1b7uk t138 = gm_l6;
            uint8_t t139 = (gm_eq9m6mvtmgde(((uint8_t *)&t137), ((uint8_t *)&t138)));
            t129 = t139;
        } else t129 = 1;
        if (!t129) goto ie128;
        {
            {
                void * t142 = gm_l0;
                void * t143 = gm_l1;
                gm_t88rcucg1b7uk t144 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t143) + 0));
                void * t145 = gm_l2;
                gm_t88rcucg1b7uk t146 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t145) + 0));
                gm_tl874t2atnlh3 t147 = gm_l3;
                gm_b16_8 t148;
                GM_ST(void *, (uint8_t *)&t148, (void *)(gm_s0pm20lshv21u));
                GM_ST(uintptr_t, (uint8_t *)&t148 + 8, (uintptr_t)(20));
                gm_t88rcucg1b7uk t149 = gm_f_unify_operands_5sp7j1j((void *)NULL, cx, NULL, t142, t144, t146, t147, t148);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
        }
        {
            void * t150 = gm_l1;
            void * t151 = gm_l2;
            gm_t3mf58gv9kf3n t152;
            GM_ST(void *, ((uint8_t *)&t152 + 0), t150);
            GM_ST(void *, ((uint8_t *)&t152 + 8), t151);
            uint8_t t153 = 0;
            F.ret = t152;
            goto gm_fret;
        }
        goto ix128;
ie128:;
ix128:;
    }
    {
        void * t155 = gm_l0;
        gm_t88rcucg1b7uk t156 = gm_l6;
        gm_t88rcucg1b7uk t157 = gm_l5;
        gm_th2lqbo77pvsc t158;
        gm_f_coercion_steps_ibqvhta((void *)&t158, (void *)NULL, cx, NULL, t155, t156, t157);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t159;
        if (((((uint8_t *)&t158))[16] != 0)) goto mn161;
        t159 = ((uint8_t)0);
        goto me160;
mn161:;
        t159 = ((uint8_t)1);
        goto me160;
mn162:;
        gm_trap("no match arm matched the scrutinee", gm_srch2l5nl06vsci, 1003, 8);
me160:;
        if (!t159) goto ie154;
        {
            void * t164 = gm_l1;
            void * t165 = gm_l0;
            void * t166 = gm_l2;
            gm_t88rcucg1b7uk t167 = gm_l5;
            void * t168 = gm_f_coerce_sudc6c1((void *)NULL, cx, NULL, t165, t166, t167);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t3mf58gv9kf3n t169;
            GM_ST(void *, ((uint8_t *)&t169 + 0), t164);
            GM_ST(void *, ((uint8_t *)&t169 + 8), t168);
            uint8_t t170 = 0;
            F.ret = t169;
            goto gm_fret;
        }
        goto ix154;
ie154:;
ix154:;
    }
    {
        void * t172 = gm_l0;
        gm_t88rcucg1b7uk t173 = gm_l5;
        gm_t88rcucg1b7uk t174 = gm_l6;
        gm_th2lqbo77pvsc t175;
        gm_f_coercion_steps_ibqvhta((void *)&t175, (void *)NULL, cx, NULL, t172, t173, t174);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t176;
        if (((((uint8_t *)&t175))[16] != 0)) goto mn178;
        t176 = ((uint8_t)0);
        goto me177;
mn178:;
        t176 = ((uint8_t)1);
        goto me177;
mn179:;
        gm_trap("no match arm matched the scrutinee", gm_srch2l5nl06vsci, 1004, 8);
me177:;
        if (!t176) goto ie171;
        {
            void * t181 = gm_l0;
            void * t182 = gm_l1;
            gm_t88rcucg1b7uk t183 = gm_l6;
            void * t184 = gm_f_coerce_sudc6c1((void *)NULL, cx, NULL, t181, t182, t183);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            void * t185 = gm_l2;
            gm_t3mf58gv9kf3n t186;
            GM_ST(void *, ((uint8_t *)&t186 + 0), t184);
            GM_ST(void *, ((uint8_t *)&t186 + 8), t185);
            uint8_t t187 = 0;
            F.ret = t186;
            goto gm_fret;
        }
        goto ix171;
ie171:;
ix171:;
    }
    {
        void * t188 = gm_l0;
        gm_tl874t2atnlh3 t189 = gm_l3;
        gm_t6j6smj7qoctu t190;
        void * t191 = gm_l0;
        gm_t88rcucg1b7uk t192 = gm_l5;
        gm_b16_8 t193 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t191, t192);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_b16_8, (uint8_t *)&t190 + 0, t193);
        void * t194 = gm_l0;
        gm_t88rcucg1b7uk t195 = gm_l6;
        gm_b16_8 t196 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t194, t195);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_b16_8, (uint8_t *)&t190 + 16, t196);
        gm_tabb710d9mq2e t197;
        gm_f_format_md1u4fd((void *)&t197, (void *)NULL, cx, NULL, &t190);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t197))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t197) + 8, gm_srch2l5nl06vsci, 1005, 20);
        gm_b16_8 t198 = GM_LD(gm_b16_8, (((uint8_t *)&t197) + 8));
        gm_b16_8 t199 = GM_LD(gm_b16_8, ((uint8_t *)&t198));
        gm_b0_1 t200 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t188, t189, t199);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t201 = gm_l1;
        void * t202 = gm_l2;
        gm_t3mf58gv9kf3n t203;
        GM_ST(void *, ((uint8_t *)&t203 + 0), t201);
        GM_ST(void *, ((uint8_t *)&t203 + 8), t202);
        uint8_t t204 = 0;
        F.ret = t203;
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
__attribute__((aligned(4))) void * gm_f_expr_kind_397ptdj(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_tkn42u7na3t81 a2) {
    struct gm_fr_expr_kind_397ptdj F;
    void * gm_l0; /* cx */
    void * gm_l1; /* e */
    gm_tkn42u7na3t81 gm_l2; /* expect */
    gm_tl874t2atnlh3 gm_l3; /* span */
    void * gm_l4; /* tys */
    void * gm_l5; /* lit */
    gm_tj2m9dbh1ekv6 gm_l6; /* suffix */
    gm_t88rcucg1b7uk gm_l7; /* ty */
    void * gm_l8; /* lit */
    gm_t88rcucg1b7uk gm_l9; /* ty */
    gm_t9qkh0rsooc8d gm_l10; /* suffix */
    gm_tibupt4dmcqai gm_l11; /* lit */
    uint8_t gm_l12; /* b */
    gm_t08olq97rscn3 gm_l13; /* id */
    void * gm_l14; /* inner */
    gm_b16_8 gm_l15; /* elems */
    gm_t9qkh0rsooc8d gm_l16; /* op */
    void * gm_l17; /* operand */
    gm_ts6oqppjh0ba3 gm_l18; /* op */
    void * gm_l19; /* lhs */
    void * gm_l20; /* rhs */
    void * gm_l21; /* l */
    void * gm_l22; /* p */
    void * gm_l23; /* inner */
    void * gm_l24; /* inner */
    void * gm_l26; /* base */
    void * gm_l27; /* i */
    gm_tao63pk3r18v5 gm_l28; /* sl */
    gm_t3mf58gv9kf3n gm_l29; /* a */
    void * gm_l30; /* r */
    void * gm_l31; /* base */
    gm_t08olq97rscn3 gm_l32; /* name */
    void * gm_l33; /* base */
    uint32_t gm_l34; /* index */
    void * gm_l35; /* callee */
    gm_b16_8 gm_l36; /* args */
    void * gm_l37; /* i */
    void * gm_l38; /* m */
    void * gm_l39; /* f */
    void * gm_l41; /* v */
    gm_t08olq97rscn3 gm_l43; /* label */
    gm_t08olq97rscn3 gm_l44; /* name */
    gm_t88rcucg1b7uk gm_l45; /* t */
    void * gm_l46; /* r */
    void * gm_l47; /* inner */
    void * gm_l48; /* call */
    gm_b16_8 gm_l49; /* args */
    gm_t88rcucg1b7uk gm_l50; /* t */
    gm_b16_8 gm_l51; /* attrs */
    void * gm_l52; /* inner */
    void * gm_l54; /* lit */
    void * gm_l55; /* lit */
    void * gm_l56; /* w */
    gm_t3mf58gv9kf3n gm_l57; /* r */
    void * gm_l58; /* inner */
    void * gm_l59; /* v */
    GM_STACK_CHECK(&F, 4878u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l1;
        gm_tl874t2atnlh3 t3 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t2) + 72));
        gm_l3 = t3;
    }
    {
        void * t4 = gm_l0;
        void * t5 = GM_LD(void *, (((uint8_t *)t4) + 0));
        void * t6 = (void *)(((uint8_t *)t5) + 8);
        gm_l4 = t6;
    }
    {
        void * t7 = gm_l1;
        gm_t2gtbh39ptekd t8 = GM_LD(gm_t2gtbh39ptekd, (((uint8_t *)t7) + 8));
        uint8_t t55 = GM_LD(uint8_t, (((uint8_t *)&t8) + 0));
        switch (t55) { case ((uint8_t)0ULL): goto ma10; case ((uint8_t)1ULL): goto ma11; case ((uint8_t)2ULL): goto ma12; case ((uint8_t)3ULL): goto ma13; case ((uint8_t)4ULL): goto ma14; case ((uint8_t)5ULL): goto ma15; case ((uint8_t)6ULL): goto ma17; case ((uint8_t)7ULL): goto ma16; case ((uint8_t)8ULL): goto ma41; case ((uint8_t)9ULL): goto ma18; case ((uint8_t)10ULL): goto ma19; case ((uint8_t)11ULL): goto ma52; case ((uint8_t)12ULL): goto ma20; case ((uint8_t)13ULL): goto ma21; case ((uint8_t)14ULL): goto ma25; case ((uint8_t)15ULL): goto ma23; case ((uint8_t)16ULL): goto ma24; case ((uint8_t)17ULL): goto ma53; case ((uint8_t)18ULL): goto ma22; case ((uint8_t)19ULL): goto ma31; case ((uint8_t)20ULL): goto ma26; case ((uint8_t)21ULL): goto ma27; case ((uint8_t)22ULL): goto ma29; case ((uint8_t)23ULL): goto ma30; case ((uint8_t)24ULL): goto ma42; case ((uint8_t)25ULL): goto ma45; case ((uint8_t)26ULL): goto ma45; case ((uint8_t)27ULL): goto ma45; case ((uint8_t)28ULL): goto ma28; case ((uint8_t)29ULL): goto ma45; case ((uint8_t)30ULL): goto ma45; case ((uint8_t)31ULL): goto ma45; case ((uint8_t)32ULL): goto ma45; case ((uint8_t)33ULL): goto ma45; case ((uint8_t)34ULL): goto ma45; case ((uint8_t)35ULL): goto ma44; case ((uint8_t)36ULL): goto ma48; case ((uint8_t)37ULL): goto ma49; case ((uint8_t)38ULL): goto ma53; case ((uint8_t)39ULL): goto ma53; case ((uint8_t)40ULL): goto ma32; case ((uint8_t)41ULL): goto ma33; case ((uint8_t)42ULL): goto ma50; case ((uint8_t)43ULL): goto ma34; case ((uint8_t)44ULL): goto ma35; case ((uint8_t)45ULL): goto ma36; case ((uint8_t)46ULL): goto ma38; case ((uint8_t)47ULL): goto ma39; case ((uint8_t)48ULL): goto ma51; case ((uint8_t)49ULL): goto ma47; case ((uint8_t)50ULL): goto ma46; default: goto ma53; }
ma10:;
        uint8_t t57 = GM_LD(uint8_t, (((uint8_t *)&t8) + 0));
        if (t57 != ((uint8_t)0ULL)) goto mn56;
        void * t58 = GM_LD(void *, (((uint8_t *)&t8) + 8));
        gm_l5 = t58;
        {
            gm_t88rcucg1b7uk t60;
            void * t62 = gm_l5;
            if (!(((((uint8_t *)t62) + 48))[8] != 0)) goto ie61;
            gm_tj2m9dbh1ekv6 t63 = GM_LD(gm_tj2m9dbh1ekv6, (((uint8_t *)t62) + 48));
            gm_l6 = t63;
            void * t65 = gm_l0;
            void * t66 = GM_LD(void *, (((uint8_t *)t65) + 0));
            gm_tj2m9dbh1ekv6 t67 = gm_l6;
            gm_t88rcucg1b7uk t68 = gm_f_suffix_type_icfvn23((void *)NULL, cx, NULL, t66, t67);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t88rcucg1b7uk t69 = t68;
            t60 = t69;
            goto ix61;
ie61:;
            void * t71 = gm_l0;
            void * t72 = (void *)(((uint8_t *)t71) + 96);
            void * t73 = gm_l4;
            gm_t9qkh0rsooc8d t74;
            GM_ST(uint8_t, ((uint8_t *)&t74 + 0), ((uint8_t)0ULL));
            gm_t88rcucg1b7uk t75 = gm_f_new_var_up2m1nq((void *)NULL, cx, NULL, t72, t73, t74);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t88rcucg1b7uk t76 = t75;
            t60 = t76;
ix61:;
            gm_l7 = t60;
        }
        {
            gm_t88rcucg1b7uk t77 = gm_l7;
            void * t78 = gm_l5;
            gm_tbci5ospk5nnn t79 = GM_LD(gm_tbci5ospk5nnn, (((uint8_t *)t78) + 0));
            gm_teti411eujvf4 t80;
            GM_ST(uint8_t, (uint8_t *)&t80 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t80 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t80 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t80 + 56, 0);
            GM_ST(uint8_t, ((uint8_t *)&t80 + 0), ((uint8_t)0ULL));
            GM_ST(gm_tbci5ospk5nnn, ((uint8_t *)&t80 + 8), t79);
            gm_tl874t2atnlh3 t81 = gm_l3;
            void * t82 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t77, &t80, t81);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t83 = 0;
            F.ret = t82;
            goto gm_fret;
        }
        goto me9;
mn56:;
ma11:;
        uint8_t t85 = GM_LD(uint8_t, (((uint8_t *)&t8) + 0));
        if (t85 != ((uint8_t)1ULL)) goto mn84;
        void * t86 = GM_LD(void *, (((uint8_t *)&t8) + 8));
        gm_l8 = t86;
        {
            void * t89 = gm_l8;
            uint8_t t90 = GM_LD(uint8_t, (((uint8_t *)t89) + 98));
            if (!t90) goto ie88;
            {
                void * t92 = gm_l0;
                void * t93 = gm_l8;
                gm_tl874t2atnlh3 t94 = gm_l3;
                void * t95 = gm_f_imaginary_literal_6d41gfv((void *)NULL, cx, NULL, t92, t93, t94);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t96 = 0;
                F.ret = t95;
                goto gm_fret;
            }
            goto ix88;
ie88:;
ix88:;
        }
        {
            void * t97 = gm_l0;
            void * t98 = (void *)(((uint8_t *)t97) + 96);
            void * t99 = gm_l4;
            gm_t9qkh0rsooc8d t100;
            GM_ST(uint8_t, ((uint8_t *)&t100 + 0), ((uint8_t)1ULL));
            gm_t88rcucg1b7uk t101 = gm_f_new_var_up2m1nq((void *)NULL, cx, NULL, t98, t99, t100);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l9 = t101;
        }
        {
            void * t103 = gm_l8;
            if (!(((((uint8_t *)t103) + 96))[1] != 0)) goto ie102;
            gm_t9qkh0rsooc8d t104 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)t103) + 96));
            gm_l10 = t104;
            {
                void * t106 = gm_l4;
                uint16_t t107;
                uint8_t t113 = GM_LD(uint8_t, (((uint8_t *)&gm_l10) + 0));
                switch (t113) { case ((uint8_t)0ULL): goto ma109; case ((uint8_t)1ULL): goto ma110; case ((uint8_t)2ULL): goto ma111; default: goto ma112; }
ma109:;
                uint8_t t115 = GM_LD(uint8_t, (((uint8_t *)&gm_l10) + 0));
                if (t115 != ((uint8_t)0ULL)) goto mn114;
                t107 = ((uint16_t)16ULL);
                goto me108;
mn114:;
ma110:;
                uint8_t t117 = GM_LD(uint8_t, (((uint8_t *)&gm_l10) + 0));
                if (t117 != ((uint8_t)1ULL)) goto mn116;
                t107 = ((uint16_t)32ULL);
                goto me108;
mn116:;
ma111:;
                uint8_t t119 = GM_LD(uint8_t, (((uint8_t *)&gm_l10) + 0));
                if (t119 != ((uint8_t)2ULL)) goto mn118;
                t107 = ((uint16_t)64ULL);
                goto me108;
mn118:;
ma112:;
                gm_trap("no match arm matched the scrutinee", gm_srch2l5nl06vsci, 50, 40);
me108:;
                gm_t53at80pfdus3 t120;
                memset(&t120, 0, sizeof t120);
                GM_ST(uint8_t, ((uint8_t *)&t120 + 0), ((uint8_t)3ULL));
                GM_ST(uint16_t, ((uint8_t *)&t120 + 8), t107);
                gm_t88rcucg1b7uk t121 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t106, &t120);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l9 = t121;
            }
            goto ix102;
ie102:;
ix102:;
        }
        {
            gm_t88rcucg1b7uk t122 = gm_l9;
            void * t123 = gm_l8;
            gm_teti411eujvf4 t124;
            gm_f_untyped_float_m6lbr0g((void *)&t124, (void *)NULL, cx, NULL, &(*(gm_tngtuiuskn938 *)(((uint8_t *)t123) + 0)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tl874t2atnlh3 t125 = gm_l3;
            void * t126 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t122, &t124, t125);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t127 = 0;
            F.ret = t126;
            goto gm_fret;
        }
        goto me9;
mn84:;
ma12:;
        uint8_t t129 = GM_LD(uint8_t, (((uint8_t *)&t8) + 0));
        if (t129 != ((uint8_t)2ULL)) goto mn128;
        gm_l11 = (*(gm_tibupt4dmcqai *)(((uint8_t *)&t8) + 8));
        {
            uint8_t t132 = ((GM_LD(uint8_t, (((uint8_t *)&gm_l11) + 16) + 0) == ((uint8_t)2ULL)));
            if (!t132) goto ie131;
            {
                void * t134 = gm_l4;
                void * t135 = gm_l4;
                gm_t88rcucg1b7uk t136 = gm_f_u8_ty_afkvj25((void *)NULL, cx, NULL, t135);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_t88rcucg1b7uk t137 = gm_f_many_ptr_l4v11f8((void *)NULL, cx, NULL, t134, ((uint8_t)1), t136);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_b16_8 t138 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l11) + 0));
                gm_b16_8 t139 = GM_LD(gm_b16_8, ((uint8_t *)&t138));
                gm_teti411eujvf4 t140;
                memset(&t140, 0, sizeof t140);
                GM_ST(uint8_t, ((uint8_t *)&t140 + 0), ((uint8_t)6ULL));
                GM_ST(gm_b16_8, ((uint8_t *)&t140 + 8), t139);
                gm_tl874t2atnlh3 t141 = gm_l3;
                void * t142 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t137, &t140, t141);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t143 = 0;
                F.ret = t142;
                goto gm_fret;
            }
            goto ix131;
ie131:;
ix131:;
        }
        {
            void * t144 = gm_l4;
            gm_t88rcucg1b7uk t145 = gm_f_string_ty_j74ff0o((void *)NULL, cx, NULL, t144);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_b16_8 t146 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l11) + 0));
            gm_b16_8 t147 = GM_LD(gm_b16_8, ((uint8_t *)&t146));
            gm_teti411eujvf4 t148;
            memset(&t148, 0, sizeof t148);
            GM_ST(uint8_t, ((uint8_t *)&t148 + 0), ((uint8_t)5ULL));
            GM_ST(gm_b16_8, ((uint8_t *)&t148 + 8), t147);
            gm_tl874t2atnlh3 t149 = gm_l3;
            void * t150 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t145, &t148, t149);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t151 = 0;
            F.ret = t150;
            goto gm_fret;
        }
        goto me9;
mn128:;
ma13:;
        uint8_t t153 = GM_LD(uint8_t, (((uint8_t *)&t8) + 0));
        if (t153 != ((uint8_t)3ULL)) goto mn152;
        uint8_t t154 = GM_LD(uint8_t, (((uint8_t *)&t8) + 8));
        gm_l12 = t154;
        void * t155 = gm_l4;
        gm_t88rcucg1b7uk t156 = gm_f_bool_ty_pn2p277((void *)NULL, cx, NULL, t155);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t157 = gm_l12;
        gm_teti411eujvf4 t158;
        memset(&t158, 0, sizeof t158);
        GM_ST(uint8_t, ((uint8_t *)&t158 + 0), ((uint8_t)4ULL));
        GM_ST(uint8_t, ((uint8_t *)&t158 + 8), t157);
        gm_tl874t2atnlh3 t159 = gm_l3;
        void * t160 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t156, &t158, t159);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t161 = 0;
        F.ret = t160;
        goto gm_fret;
        goto me9;
mn152:;
ma14:;
        uint8_t t163 = GM_LD(uint8_t, (((uint8_t *)&t8) + 0));
        if (t163 != ((uint8_t)4ULL)) goto mn162;
        void * t164 = gm_l0;
        gm_tkn42u7na3t81 t165 = gm_l2;
        gm_tl874t2atnlh3 t166 = gm_l3;
        void * t167 = gm_f_null_value_ruu6332((void *)NULL, cx, NULL, t164, t165, t166);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t168 = 0;
        F.ret = t167;
        goto gm_fret;
        goto me9;
mn162:;
ma15:;
        uint8_t t170 = GM_LD(uint8_t, (((uint8_t *)&t8) + 0));
        if (t170 != ((uint8_t)5ULL)) goto mn169;
        {
            void * t172 = gm_l0;
            gm_tl874t2atnlh3 t173 = gm_l3;
            gm_b16_8 t174;
            GM_ST(void *, (uint8_t *)&t174, (void *)(gm_sra3ncr12fqek));
            GM_ST(uintptr_t, (uint8_t *)&t174 + 8, (uintptr_t)(55));
            gm_b0_1 t175 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t172, t173, t174);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t176 = gm_l0;
            void * t177 = GM_LD(void *, (((uint8_t *)t176) + 0));
            gm_tl874t2atnlh3 t178 = gm_l3;
            void * t179 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t177, t178);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t180 = 0;
            F.ret = t179;
            goto gm_fret;
        }
        goto me9;
mn169:;
ma16:;
        uint8_t t182 = GM_LD(uint8_t, (((uint8_t *)&t8) + 0));
        if (t182 != ((uint8_t)7ULL)) goto mn181;
        {
            void * t184 = gm_l0;
            gm_tl874t2atnlh3 t185 = gm_l3;
            gm_b16_8 t186;
            GM_ST(void *, (uint8_t *)&t186, (void *)(gm_s4srjt2tusqhn));
            GM_ST(uintptr_t, (uint8_t *)&t186 + 8, (uintptr_t)(29));
            gm_b0_1 t187 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t184, t185, t186);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t188 = gm_l0;
            void * t189 = GM_LD(void *, (((uint8_t *)t188) + 0));
            gm_tl874t2atnlh3 t190 = gm_l3;
            void * t191 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t189, t190);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t192 = 0;
            F.ret = t191;
            goto gm_fret;
        }
        goto me9;
mn181:;
ma17:;
        uint8_t t194 = GM_LD(uint8_t, (((uint8_t *)&t8) + 0));
        if (t194 != ((uint8_t)6ULL)) goto mn193;
        gm_l13 = (*(gm_t08olq97rscn3 *)(((uint8_t *)&t8) + 8));
        void * t195 = gm_l0;
        gm_b16_8 t196 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l13) + 0));
        gm_tkn42u7na3t81 t197 = gm_l2;
        gm_tl874t2atnlh3 t198 = gm_l3;
        void * t199 = gm_f_name_blqdq2r((void *)NULL, cx, NULL, t195, t196, t197, t198);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t200 = 0;
        F.ret = t199;
        goto gm_fret;
        goto me9;
mn193:;
ma18:;
        uint8_t t202 = GM_LD(uint8_t, (((uint8_t *)&t8) + 0));
        if (t202 != ((uint8_t)9ULL)) goto mn201;
        void * t203 = GM_LD(void *, (((uint8_t *)&t8) + 8));
        gm_l14 = t203;
        void * t204 = gm_l0;
        void * t205 = gm_l14;
        gm_tkn42u7na3t81 t206 = gm_l2;
        void * t207 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t204, t205, t206);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t208 = 0;
        F.ret = t207;
        goto gm_fret;
        goto me9;
mn201:;
ma19:;
        uint8_t t210 = GM_LD(uint8_t, (((uint8_t *)&t8) + 0));
        if (t210 != ((uint8_t)10ULL)) goto mn209;
        gm_b16_8 t211 = GM_LD(gm_b16_8, (((uint8_t *)&t8) + 8));
        gm_l15 = t211;
        void * t212 = gm_l0;
        gm_b16_8 t213 = gm_l15;
        gm_b16_8 t214 = GM_LD(gm_b16_8, ((uint8_t *)&t213));
        gm_tkn42u7na3t81 t215 = gm_l2;
        gm_tl874t2atnlh3 t216 = gm_l3;
        void * t217 = gm_f_tuple_qkn0cor((void *)NULL, cx, NULL, t212, t214, t215, t216);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t218 = 0;
        F.ret = t217;
        goto gm_fret;
        goto me9;
mn209:;
ma20:;
        uint8_t t220 = GM_LD(uint8_t, (((uint8_t *)&t8) + 0));
        if (t220 != ((uint8_t)12ULL)) goto mn219;
        gm_t9qkh0rsooc8d t221 = GM_LD(gm_t9qkh0rsooc8d, ((((uint8_t *)&t8) + 8) + 0));
        gm_l16 = t221;
        void * t222 = GM_LD(void *, ((((uint8_t *)&t8) + 8) + 8));
        gm_l17 = t222;
        void * t223 = gm_l0;
        gm_t9qkh0rsooc8d t224 = gm_l16;
        void * t225 = gm_l17;
        gm_tkn42u7na3t81 t226 = gm_l2;
        gm_tl874t2atnlh3 t227 = gm_l3;
        void * t228 = gm_f_unary_asf0eot((void *)NULL, cx, NULL, t223, t224, t225, t226, t227);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t229 = 0;
        F.ret = t228;
        goto gm_fret;
        goto me9;
mn219:;
ma21:;
        uint8_t t231 = GM_LD(uint8_t, (((uint8_t *)&t8) + 0));
        if (t231 != ((uint8_t)13ULL)) goto mn230;
        gm_ts6oqppjh0ba3 t232 = GM_LD(gm_ts6oqppjh0ba3, ((((uint8_t *)&t8) + 8) + 0));
        gm_l18 = t232;
        void * t233 = GM_LD(void *, ((((uint8_t *)&t8) + 8) + 8));
        gm_l19 = t233;
        void * t234 = GM_LD(void *, ((((uint8_t *)&t8) + 8) + 16));
        gm_l20 = t234;
        {
            gm_ts6oqppjh0ba3 t237 = gm_l18;
            gm_ts6oqppjh0ba3 t238;
            GM_ST(uint8_t, (uint8_t *)&t238 + 1, 0);
            GM_ST(uint8_t, ((uint8_t *)&t238 + 0), ((uint8_t)10ULL));
            gm_ts6oqppjh0ba3 t239;
            GM_ST(uint8_t, (uint8_t *)&t239 + 1, 0);
            GM_ST(uint8_t, ((uint8_t *)&t239 + 0), ((uint8_t)11ULL));
            gm_t9ldr1q5hn9fq t240;
            GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t240 + 0), t238);
            GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t240 + 2), t239);
            uint8_t t241 = 0;
            if (gm_eqbqb4gb1d92(((uint8_t *)&t240) + 0, ((uint8_t *)&t237))) t241 = 1;
            if (gm_eqbqb4gb1d92(((uint8_t *)&t240) + 2, ((uint8_t *)&t237))) t241 = 1;
            uint8_t t242 = (uint8_t)(t241);
            if (!t242) goto ie236;
            {
                void * t244 = gm_l0;
                uint8_t t245 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l18) + 0) == ((uint8_t)11ULL)));
                void * t246 = gm_l19;
                void * t247 = gm_l20;
                gm_tkn42u7na3t81 t248 = gm_l2;
                gm_tl874t2atnlh3 t249 = gm_l3;
                void * t250 = gm_f_range_value_b02g2v9((void *)NULL, cx, NULL, t244, t245, t246, t247, t248, t249);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t251 = 0;
                F.ret = t250;
                goto gm_fret;
            }
            goto ix236;
ie236:;
ix236:;
        }
        {
            gm_ts6oqppjh0ba3 t253 = gm_l18;
            gm_ts6oqppjh0ba3 t254;
            GM_ST(uint8_t, (uint8_t *)&t254 + 1, 0);
            GM_ST(uint8_t, ((uint8_t *)&t254 + 0), ((uint8_t)12ULL));
            gm_ts6oqppjh0ba3 t255;
            GM_ST(uint8_t, (uint8_t *)&t255 + 1, 0);
            GM_ST(uint8_t, ((uint8_t *)&t255 + 0), ((uint8_t)13ULL));
            gm_t9ldr1q5hn9fq t256;
            GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t256 + 0), t254);
            GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t256 + 2), t255);
            uint8_t t257 = 0;
            if (gm_eqbqb4gb1d92(((uint8_t *)&t256) + 0, ((uint8_t *)&t253))) t257 = 1;
            if (gm_eqbqb4gb1d92(((uint8_t *)&t256) + 2, ((uint8_t *)&t253))) t257 = 1;
            uint8_t t258 = (uint8_t)(t257);
            if (!t258) goto ie252;
            uint8_t t259 = 0;
            void * t260 = gm_l0;
            void * t261 = gm_l19;
            uint8_t t262 = gm_f_type_operand_ta2mhln((void *)NULL, cx, NULL, t260, t261);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (!t262) {
                void * t263 = gm_l0;
                void * t264 = gm_l20;
                uint8_t t265 = gm_f_type_operand_ta2mhln((void *)NULL, cx, NULL, t263, t264);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                t259 = t265;
            } else t259 = 1;
            if (!t259) goto ie252;
            {
                void * t267 = gm_l0;
                uint8_t t268 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l18) + 0) == ((uint8_t)13ULL)));
                void * t269 = gm_l19;
                void * t270 = gm_l20;
                gm_tl874t2atnlh3 t271 = gm_l3;
                void * t272 = gm_f_type_is_683ln2e((void *)NULL, cx, NULL, t267, t268, t269, t270, t271);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t273 = 0;
                F.ret = t272;
                goto gm_fret;
            }
            goto ix252;
ie252:;
ix252:;
        }
        {
            void * t274 = gm_l0;
            void * t275 = gm_l19;
            gm_tkn42u7na3t81 t276;
            memset(&t276, 0, sizeof t276);
            void * t277 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t274, t275, t276);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l21 = t277;
        }
        {
            void * t278 = gm_l0;
            gm_ts6oqppjh0ba3 t279 = gm_l18;
            void * t280 = gm_l21;
            void * t281 = gm_l20;
            gm_tl874t2atnlh3 t282 = gm_l3;
            void * t283 = gm_f_binary_op_7ocdl4v((void *)NULL, cx, NULL, t278, t279, t280, t281, t282);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t284 = 0;
            F.ret = t283;
            goto gm_fret;
        }
        goto me9;
mn230:;
ma22:;
        uint8_t t286 = GM_LD(uint8_t, (((uint8_t *)&t8) + 0));
        if (t286 != ((uint8_t)18ULL)) goto mn285;
        void * t287 = GM_LD(void *, (((uint8_t *)&t8) + 8));
        gm_l22 = t287;
        void * t288 = gm_l0;
        void * t289 = gm_l22;
        gm_tl874t2atnlh3 t290 = gm_l3;
        void * t291 = gm_f_deref_expr_resldhc((void *)NULL, cx, NULL, t288, t289, t290);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t292 = 0;
        F.ret = t291;
        goto gm_fret;
        goto me9;
mn285:;
ma23:;
        uint8_t t294 = GM_LD(uint8_t, (((uint8_t *)&t8) + 0));
        if (t294 != ((uint8_t)15ULL)) goto mn293;
        void * t295 = GM_LD(void *, (((uint8_t *)&t8) + 8));
        gm_l23 = t295;
        void * t296 = gm_l0;
        void * t297 = gm_l23;
        gm_tl874t2atnlh3 t298 = gm_l3;
        void * t299 = gm_f_opt_unwrap_3qt46b5((void *)NULL, cx, NULL, t296, t297, t298);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t300 = 0;
        F.ret = t299;
        goto gm_fret;
        goto me9;
mn293:;
ma24:;
        uint8_t t302 = GM_LD(uint8_t, (((uint8_t *)&t8) + 0));
        if (t302 != ((uint8_t)16ULL)) goto mn301;
        void * t303 = GM_LD(void *, (((uint8_t *)&t8) + 8));
        gm_l24 = t303;
        void * t304 = gm_l0;
        void * t305 = gm_l24;
        gm_tl874t2atnlh3 t306 = gm_l3;
        void * t307 = gm_f_res_unwrap_e6id52i((void *)NULL, cx, NULL, t304, t305, t306);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t308 = 0;
        F.ret = t307;
        goto gm_fret;
        goto me9;
mn301:;
ma25:;
        uint8_t t310 = GM_LD(uint8_t, (((uint8_t *)&t8) + 0));
        if (t310 != ((uint8_t)14ULL)) goto mn309;
        F.l25 = (*(gm_tioi3jh9v22g5 *)(((uint8_t *)&t8) + 8));
        void * t311 = gm_l0;
        void * t312 = GM_LD(void *, (((uint8_t *)&F.l25) + 0));
        void * t313 = GM_LD(void *, (((uint8_t *)&F.l25) + 48));
        gm_tkn42u7na3t81 t314 = gm_l2;
        gm_tl874t2atnlh3 t315 = gm_l3;
        void * t316 = gm_f_else_expr_5lgn2qp((void *)NULL, cx, NULL, t311, t312, &(*(gm_t1s75i08qtptd *)(((uint8_t *)&F.l25) + 8)), t313, t314, t315);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t317 = 0;
        F.ret = t316;
        goto gm_fret;
        goto me9;
mn309:;
ma26:;
        uint8_t t319 = GM_LD(uint8_t, (((uint8_t *)&t8) + 0));
        if (t319 != ((uint8_t)20ULL)) goto mn318;
        void * t320 = GM_LD(void *, ((((uint8_t *)&t8) + 8) + 0));
        gm_l26 = t320;
        void * t321 = GM_LD(void *, ((((uint8_t *)&t8) + 8) + 8));
        gm_l27 = t321;
        void * t322 = gm_l0;
        void * t323 = gm_l26;
        void * t324 = gm_l27;
        gm_tl874t2atnlh3 t325 = gm_l3;
        void * t326 = gm_f_index_uh6hfri((void *)NULL, cx, NULL, t322, t323, t324, t325);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t327 = 0;
        F.ret = t326;
        goto gm_fret;
        goto me9;
mn318:;
ma27:;
        uint8_t t329 = GM_LD(uint8_t, (((uint8_t *)&t8) + 0));
        if (t329 != ((uint8_t)21ULL)) goto mn328;
        gm_l28 = (*(gm_tao63pk3r18v5 *)(((uint8_t *)&t8) + 8));
        void * t330 = gm_l0;
        void * t331 = GM_LD(void *, (((uint8_t *)&gm_l28) + 0));
        void * t332 = GM_LD(void *, (((uint8_t *)&gm_l28) + 8));
        void * t333 = GM_LD(void *, (((uint8_t *)&gm_l28) + 16));
        uint8_t t334 = GM_LD(uint8_t, (((uint8_t *)&gm_l28) + 24));
        gm_tl874t2atnlh3 t335 = gm_l3;
        void * t336 = gm_f_slice_expr_iamf9el((void *)NULL, cx, NULL, t330, t331, t332, t333, t334, t335);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t337 = 0;
        F.ret = t336;
        goto gm_fret;
        goto me9;
mn328:;
ma28:;
        uint8_t t339 = GM_LD(uint8_t, (((uint8_t *)&t8) + 0));
        if (t339 != ((uint8_t)28ULL)) goto mn338;
        gm_t3mf58gv9kf3n t340 = GM_LD(gm_t3mf58gv9kf3n, (((uint8_t *)&t8) + 8));
        gm_l29 = t340;
        {
            void * t343 = gm_l1;
            void * t344 = gm_f_hoist_prefix_r1mevpl((void *)NULL, cx, NULL, t343);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (!(GM_LD(void *, ((uint8_t *)&t344)) != 0)) goto ie342;
            void * t345 = GM_LD(void *, ((uint8_t *)&t344));
            gm_l30 = t345;
            {
                void * t347 = gm_l0;
                void * t348 = gm_l30;
                gm_tkn42u7na3t81 t349 = gm_l2;
                void * t350 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t347, t348, t349);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t351 = 0;
                F.ret = t350;
                goto gm_fret;
            }
            goto ix342;
ie342:;
ix342:;
        }
        {
            void * t353 = GM_LD(void *, (((uint8_t *)&gm_l29) + 8));
            void * t354 = gm_f_construction_call_ktqtrpo(t353);
            uint8_t t355;
            if ((GM_LD(void *, ((uint8_t *)&t354)) != 0)) goto mn357;
            t355 = ((uint8_t)1);
            goto me356;
mn357:;
            t355 = ((uint8_t)0);
            goto me356;
mn358:;
            gm_trap("no match arm matched the scrutinee", gm_srch2l5nl06vsci, 94, 16);
me356:;
            if (!t355) goto ie352;
            {
                void * t360 = gm_l0;
                gm_tl874t2atnlh3 t361 = gm_l3;
                gm_b16_8 t362;
                GM_ST(void *, (uint8_t *)&t362, (void *)(gm_safll42cn0mu7));
                GM_ST(uintptr_t, (uint8_t *)&t362 + 8, (uintptr_t)(30));
                gm_b0_1 t363 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t360, t361, t362);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t364 = gm_l0;
                void * t365 = GM_LD(void *, (((uint8_t *)t364) + 0));
                gm_tl874t2atnlh3 t366 = gm_l3;
                void * t367 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t365, t366);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t368 = 0;
                F.ret = t367;
                goto gm_fret;
            }
            goto ix352;
ie352:;
ix352:;
        }
        {
            void * t369 = gm_l0;
            void * t370 = gm_l1;
            void * t371 = GM_LD(void *, (((uint8_t *)&gm_l29) + 0));
            void * t372 = GM_LD(void *, (((uint8_t *)&gm_l29) + 8));
            gm_tl874t2atnlh3 t373 = gm_l3;
            void * t374 = gm_f_array_type_construction_vgjg706((void *)NULL, cx, NULL, t369, t370, t371, t372, t373);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t375 = 0;
            F.ret = t374;
            goto gm_fret;
        }
        goto me9;
mn338:;
ma29:;
        uint8_t t377 = GM_LD(uint8_t, (((uint8_t *)&t8) + 0));
        if (t377 != ((uint8_t)22ULL)) goto mn376;
        void * t378 = GM_LD(void *, ((((uint8_t *)&t8) + 8) + 0));
        gm_l31 = t378;
        gm_l32 = (*(gm_t08olq97rscn3 *)(((uint8_t *)&t8) + 16));
        void * t379 = gm_l0;
        void * t380 = gm_l31;
        gm_tl874t2atnlh3 t381 = gm_l3;
        void * t382 = gm_f_field_jlden9b((void *)NULL, cx, NULL, t379, t380, &(*(gm_t08olq97rscn3 *)((uint8_t *)&gm_l32)), t381);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t383 = 0;
        F.ret = t382;
        goto gm_fret;
        goto me9;
mn376:;
ma30:;
        uint8_t t385 = GM_LD(uint8_t, (((uint8_t *)&t8) + 0));
        if (t385 != ((uint8_t)23ULL)) goto mn384;
        void * t386 = GM_LD(void *, ((((uint8_t *)&t8) + 8) + 0));
        gm_l33 = t386;
        uint32_t t387 = GM_LD(uint32_t, ((((uint8_t *)&t8) + 8) + 8));
        gm_l34 = t387;
        void * t388 = gm_l0;
        void * t389 = gm_l33;
        uint32_t t390 = gm_l34;
        gm_tl874t2atnlh3 t391 = gm_l3;
        void * t392 = gm_f_tuple_field_dmnnit2((void *)NULL, cx, NULL, t388, t389, t390, t391);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t393 = 0;
        F.ret = t392;
        goto gm_fret;
        goto me9;
mn384:;
ma31:;
        uint8_t t395 = GM_LD(uint8_t, (((uint8_t *)&t8) + 0));
        if (t395 != ((uint8_t)19ULL)) goto mn394;
        void * t396 = GM_LD(void *, ((((uint8_t *)&t8) + 8) + 0));
        gm_l35 = t396;
        gm_b16_8 t397 = GM_LD(gm_b16_8, ((((uint8_t *)&t8) + 8) + 8));
        gm_l36 = t397;
        void * t398 = gm_l0;
        void * t399 = gm_l35;
        gm_b16_8 t400 = gm_l36;
        gm_b16_8 t401 = GM_LD(gm_b16_8, ((uint8_t *)&t400));
        gm_tkn42u7na3t81 t402 = gm_l2;
        gm_tl874t2atnlh3 t403 = gm_l3;
        void * t404 = gm_f_call_l10ebge((void *)NULL, cx, NULL, t398, t399, t401, t402, t403);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t405 = 0;
        F.ret = t404;
        goto gm_fret;
        goto me9;
mn394:;
ma32:;
        uint8_t t407 = GM_LD(uint8_t, (((uint8_t *)&t8) + 0));
        if (t407 != ((uint8_t)40ULL)) goto mn406;
        void * t408 = GM_LD(void *, (((uint8_t *)&t8) + 8));
        gm_l37 = t408;
        void * t409 = gm_l0;
        void * t410 = gm_l37;
        gm_tkn42u7na3t81 t411 = gm_l2;
        void * t412 = gm_f_if_expr_tiak47t((void *)NULL, cx, NULL, t409, t410, ((uint8_t)1), t411);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t413 = 0;
        F.ret = t412;
        goto gm_fret;
        goto me9;
mn406:;
ma33:;
        uint8_t t415 = GM_LD(uint8_t, (((uint8_t *)&t8) + 0));
        if (t415 != ((uint8_t)41ULL)) goto mn414;
        void * t416 = GM_LD(void *, (((uint8_t *)&t8) + 8));
        gm_l38 = t416;
        void * t417 = gm_l0;
        void * t418 = gm_l38;
        gm_tkn42u7na3t81 t419 = gm_l2;
        void * t420 = gm_f_match_expr_npcoo60((void *)NULL, cx, NULL, t417, t418, ((uint8_t)1), t419);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t421 = 0;
        F.ret = t420;
        goto gm_fret;
        goto me9;
mn414:;
ma34:;
        uint8_t t423 = GM_LD(uint8_t, (((uint8_t *)&t8) + 0));
        if (t423 != ((uint8_t)43ULL)) goto mn422;
        void * t424 = GM_LD(void *, (((uint8_t *)&t8) + 8));
        gm_l39 = t424;
        void * t425 = gm_l0;
        void * t426 = gm_l39;
        void * t427 = gm_f_for_loop_b6hjsbq((void *)NULL, cx, NULL, t425, t426);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t428 = 0;
        F.ret = t427;
        goto gm_fret;
        goto me9;
mn422:;
ma35:;
        uint8_t t430 = GM_LD(uint8_t, (((uint8_t *)&t8) + 0));
        if (t430 != ((uint8_t)44ULL)) goto mn429;
        F.l40 = (*(gm_t79rbdv4rafe5 *)(((uint8_t *)&t8) + 8));
        void * t431 = gm_l0;
        void * t432 = (void *)((uint8_t *)&F.l40);
        gm_tkn42u7na3t81 t433 = gm_l2;
        void * t434 = gm_f_block_expected_bv4ukm2((void *)NULL, cx, NULL, t431, t432, ((uint8_t)1), t433);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t435 = 0;
        F.ret = t434;
        goto gm_fret;
        goto me9;
mn429:;
ma36:;
        uint8_t t437 = GM_LD(uint8_t, (((uint8_t *)&t8) + 0));
        if (t437 != ((uint8_t)45ULL)) goto mn436;
        if (!(GM_LD(void *, (((uint8_t *)&t8) + 8)) != 0)) goto mn436;
        void * t438 = GM_LD(void *, (((uint8_t *)&t8) + 8));
        gm_l41 = t438;
        void * t439 = gm_l0;
        void * t440 = gm_l41;
        gm_tl874t2atnlh3 t441 = gm_l3;
        void * t442 = gm_f_return_expr_0qs023i((void *)NULL, cx, NULL, t439, t440, t441);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t443 = 0;
        F.ret = t442;
        goto gm_fret;
        goto me9;
mn436:;
ma37:;
        uint8_t t445 = GM_LD(uint8_t, (((uint8_t *)&t8) + 0));
        if (t445 != ((uint8_t)45ULL)) goto mn444;
        if ((GM_LD(void *, (((uint8_t *)&t8) + 8)) != 0)) goto mn444;
        void * t446 = gm_l0;
        gm_tl874t2atnlh3 t447 = gm_l3;
        void * t448 = gm_f_return_expr_0qs023i((void *)NULL, cx, NULL, t446, ((void *)0), t447);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t449 = 0;
        F.ret = t448;
        goto gm_fret;
        goto me9;
mn444:;
ma38:;
        uint8_t t451 = GM_LD(uint8_t, (((uint8_t *)&t8) + 0));
        if (t451 != ((uint8_t)46ULL)) goto mn450;
        F.l42 = (*(gm_tchr52g0ema53 *)(((uint8_t *)&t8) + 8));
        void * t452 = gm_l0;
        void * t453 = GM_LD(void *, (((uint8_t *)&F.l42) + 40));
        gm_tl874t2atnlh3 t454 = gm_l3;
        void * t455 = gm_f_break_expr_l653dlv((void *)NULL, cx, NULL, t452, &(*(gm_t1s75i08qtptd *)(((uint8_t *)&F.l42) + 0)), t453, t454);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t456 = 0;
        F.ret = t455;
        goto gm_fret;
        goto me9;
mn450:;
ma39:;
        uint8_t t458 = GM_LD(uint8_t, (((uint8_t *)&t8) + 0));
        if (t458 != ((uint8_t)47ULL)) goto mn457;
        if (!(((((uint8_t *)&t8) + 8))[32] != 0)) goto mn457;
        gm_l43 = (*(gm_t08olq97rscn3 *)(((uint8_t *)&t8) + 8));
        void * t459 = gm_l0;
        gm_t08olq97rscn3 t460 = gm_l43;
        gm_t1s75i08qtptd t461;
        GM_ST(uint8_t, (uint8_t *)&t461 + 33, 0);
        GM_ST(uint16_t, (uint8_t *)&t461 + 34, 0);
        GM_ST(uint32_t, (uint8_t *)&t461 + 36, 0);
        GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t461), t460);
        ((uint8_t *)&t461)[32] = 1;
        gm_tl874t2atnlh3 t462 = gm_l3;
        void * t463 = gm_f_continue_expr_53f04kf((void *)NULL, cx, NULL, t459, &t461, t462);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t464 = 0;
        F.ret = t463;
        goto gm_fret;
        goto me9;
mn457:;
ma40:;
        uint8_t t466 = GM_LD(uint8_t, (((uint8_t *)&t8) + 0));
        if (t466 != ((uint8_t)47ULL)) goto mn465;
        if ((((((uint8_t *)&t8) + 8))[32] != 0)) goto mn465;
        void * t467 = gm_l0;
        gm_t1s75i08qtptd t468;
        memset(&t468, 0, sizeof t468);
        gm_tl874t2atnlh3 t469 = gm_l3;
        void * t470 = gm_f_continue_expr_53f04kf((void *)NULL, cx, NULL, t467, &t468, t469);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t471 = 0;
        F.ret = t470;
        goto gm_fret;
        goto me9;
mn465:;
ma41:;
        uint8_t t473 = GM_LD(uint8_t, (((uint8_t *)&t8) + 0));
        if (t473 != ((uint8_t)8ULL)) goto mn472;
        gm_l44 = (*(gm_t08olq97rscn3 *)(((uint8_t *)&t8) + 8));
        {
            gm_tkn42u7na3t81 t475 = gm_l2;
            gm_t88rcucg1b7uk t476;
            if (((((uint8_t *)&t475))[4] != 0)) {
                gm_t88rcucg1b7uk t477 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t475) + 0));
                t476 = t477;
            } else {
                {
                    void * t479 = gm_l0;
                    gm_tl874t2atnlh3 t480 = gm_l3;
                    gm_b16_8 t481 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l44) + 0));
                    gm_tnpifv20irnph t482;
                    GM_ST(gm_b16_8, ((uint8_t *)&t482 + 0), t481);
                    gm_tabb710d9mq2e t483;
                    gm_f_format_bvitrsu((void *)&t483, (void *)NULL, cx, NULL, t482);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    if (((((uint8_t *)&t483))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t483) + 8, gm_srch2l5nl06vsci, 114, 32);
                    gm_b16_8 t484 = GM_LD(gm_b16_8, (((uint8_t *)&t483) + 8));
                    gm_b16_8 t485 = GM_LD(gm_b16_8, ((uint8_t *)&t484));
                    gm_b0_1 t486 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t479, t480, t485);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                void * t487 = gm_l0;
                void * t488 = GM_LD(void *, (((uint8_t *)t487) + 0));
                gm_tl874t2atnlh3 t489 = gm_l3;
                void * t490 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t488, t489);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t491 = 0;
                F.ret = t490;
                goto gm_fret;
            }
            gm_l45 = t476;
        }
        {
            void * t492 = gm_l0;
            void * t493 = gm_l0;
            void * t494 = gm_l0;
            void * t495 = (void *)(((uint8_t *)t494) + 96);
            void * t496 = gm_l0;
            void * t497 = GM_LD(void *, (((uint8_t *)t496) + 0));
            gm_t88rcucg1b7uk t498 = gm_l45;
            gm_t88rcucg1b7uk t499 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t495, t497, t498);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t88rcucg1b7uk t500 = GM_CONV(gm_t88rcucg1b7uk, gm_f_strip_wrappers_e5thpib(t493, GM_CONV(gm_csguankko4lmhd, t499)));
            gm_th2lqbo77pvsc t501;
            memset(&t501, 0, sizeof t501);
            gm_tl874t2atnlh3 t502 = gm_l3;
            void * t503 = gm_f_construct_variant_o6is81c((void *)NULL, cx, NULL, t492, t500, &(*(gm_t08olq97rscn3 *)((uint8_t *)&gm_l44)), &t501, t502);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t504 = 0;
            F.ret = t503;
            goto gm_fret;
        }
        goto me9;
mn472:;
ma42:;
        uint8_t t506 = GM_LD(uint8_t, (((uint8_t *)&t8) + 0));
        if (t506 != ((uint8_t)24ULL)) goto mn505;
        void * t507 = gm_l1;
        void * t508 = gm_f_hoist_prefix_r1mevpl((void *)NULL, cx, NULL, t507);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t509;
        if ((GM_LD(void *, ((uint8_t *)&t508)) != 0)) goto mn511;
        t509 = ((uint8_t)0);
        goto me510;
mn511:;
        t509 = ((uint8_t)1);
        goto me510;
mn512:;
        gm_trap("no match arm matched the scrutinee", gm_srch2l5nl06vsci, 119, 29);
me510:;
        if (!t509) goto mn505;
        {
            void * t514 = gm_l1;
            void * t515 = gm_f_hoist_prefix_r1mevpl((void *)NULL, cx, NULL, t514);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            void * t516;
            if ((GM_LD(void *, ((uint8_t *)&t515)) != 0)) {
                void * t517 = GM_LD(void *, (((uint8_t *)&t515) + 0));
                t516 = t517;
            } else {
                void * t518 = gm_l0;
                void * t519 = GM_LD(void *, (((uint8_t *)t518) + 0));
                gm_tl874t2atnlh3 t520 = gm_l3;
                void * t521 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t519, t520);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t522 = 0;
                F.ret = t521;
                goto gm_fret;
            }
            gm_l46 = t516;
        }
        {
            void * t523 = gm_l0;
            void * t524 = gm_l46;
            gm_tkn42u7na3t81 t525 = gm_l2;
            void * t526 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t523, t524, t525);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t527 = 0;
            F.ret = t526;
            goto gm_fret;
        }
        goto me9;
mn505:;
ma43:;
        uint8_t t529 = GM_LD(uint8_t, (((uint8_t *)&t8) + 0));
        if (t529 != ((uint8_t)24ULL)) goto mn528;
        void * t530 = GM_LD(void *, (((uint8_t *)&t8) + 8));
        gm_l47 = t530;
        void * t531 = gm_l47;
        uint8_t t532 = gm_f_prefix_on_call_f7od3q2(t531);
        if (!t532) goto mn528;
        {
            void * t534 = gm_l47;
            void * t535 = gm_f_construction_call_ktqtrpo(t534);
            void * t536;
            if ((GM_LD(void *, ((uint8_t *)&t535)) != 0)) {
                void * t537 = GM_LD(void *, (((uint8_t *)&t535) + 0));
                t536 = t537;
            } else {
                void * t538 = gm_l0;
                gm_tl874t2atnlh3 t539 = gm_l3;
                gm_b16_8 t540;
                GM_ST(void *, (uint8_t *)&t540, (void *)(gm_s6j4ug98j8dce));
                GM_ST(uintptr_t, (uint8_t *)&t540 + 8, (uintptr_t)(38));
                void * t541 = gm_f_unsupported_r3r7qdh((void *)NULL, cx, NULL, t538, t539, t540);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t542 = 0;
                F.ret = t541;
                goto gm_fret;
            }
            gm_l48 = t536;
        }
        {
            void * t544 = gm_l48;
            uint8_t t545 = GM_LD(uint8_t, ((((uint8_t *)t544) + 8) + 0));
            if (t545 != ((uint8_t)19ULL)) goto ie543;
            gm_b16_8 t546 = GM_LD(gm_b16_8, (((((uint8_t *)t544) + 8) + 8) + 8));
            gm_l49 = t546;
            {
                void * t548 = gm_l0;
                void * t549 = gm_l47;
                gm_tkn42u7na3t81 t550 = gm_f_construction_elem_type_qm1ncub((void *)NULL, cx, NULL, t548, t549);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_t88rcucg1b7uk t551;
                if (((((uint8_t *)&t550))[4] != 0)) {
                    gm_t88rcucg1b7uk t552 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t550) + 0));
                    t551 = t552;
                } else {
                    void * t553 = gm_l0;
                    void * t554 = GM_LD(void *, (((uint8_t *)t553) + 0));
                    gm_tl874t2atnlh3 t555 = gm_l3;
                    void * t556 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t554, t555);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t557 = 0;
                    F.ret = t556;
                    goto gm_fret;
                }
                gm_l50 = t551;
            }
            {
                void * t558 = gm_l0;
                void * t559 = gm_l0;
                void * t560 = GM_LD(void *, (((uint8_t *)t559) + 0));
                void * t561 = (void *)(((uint8_t *)t560) + 8);
                gm_t88rcucg1b7uk t562 = gm_l50;
                gm_t88rcucg1b7uk t563 = gm_f_optional_rh6okj3((void *)NULL, cx, NULL, t561, t562);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_t88rcucg1b7uk t564 = gm_l50;
                gm_b16_8 t565 = gm_l49;
                gm_b16_8 t566 = GM_LD(gm_b16_8, ((uint8_t *)&t565));
                gm_tl874t2atnlh3 t567 = gm_l3;
                void * t568 = gm_f_construct_optional_cffo1ol((void *)NULL, cx, NULL, t558, t563, t564, t566, t567);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t569 = 0;
                F.ret = t568;
                goto gm_fret;
            }
            goto ix543;
ie543:;
            {
                void * t571 = gm_l0;
                void * t572 = GM_LD(void *, (((uint8_t *)t571) + 0));
                gm_tl874t2atnlh3 t573 = gm_l3;
                void * t574 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t572, t573);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t575 = 0;
                F.ret = t574;
                goto gm_fret;
            }
ix543:;
        }
        goto me9;
mn528:;
ma44:;
        uint8_t t577 = GM_LD(uint8_t, (((uint8_t *)&t8) + 0));
        if (t577 != ((uint8_t)35ULL)) goto mn576;
        gm_b16_8 t578 = GM_LD(gm_b16_8, ((((uint8_t *)&t8) + 8) + 0));
        gm_l51 = t578;
        void * t579 = GM_LD(void *, ((((uint8_t *)&t8) + 8) + 16));
        gm_l52 = t579;
        uint8_t t580 = 0;
        void * t581 = gm_l52;
        uint8_t t582;
        uint8_t t585 = GM_LD(uint8_t, ((((uint8_t *)t581) + 8) + 0));
        if (t585 != ((uint8_t)28ULL)) goto mn584;
        t582 = ((uint8_t)1);
        goto me583;
mn584:;
        t582 = ((uint8_t)0);
        goto me583;
mn586:;
        gm_trap("no match arm matched the scrutinee", gm_srch2l5nl06vsci, 130, 38);
me583:;
        if (t582) {
            void * t587 = gm_l52;
            void * t588 = gm_f_construction_call_ktqtrpo(t587);
            uint8_t t589;
            if ((GM_LD(void *, ((uint8_t *)&t588)) != 0)) goto mn591;
            t589 = ((uint8_t)0);
            goto me590;
mn591:;
            t589 = ((uint8_t)1);
            goto me590;
mn592:;
            gm_trap("no match arm matched the scrutinee", gm_srch2l5nl06vsci, 130, 70);
me590:;
            t580 = t589;
        } else t580 = 0;
        if (!t580) goto mn576;
        {
            void * t594 = gm_l0;
            gm_b16_8 t595 = gm_l51;
            gm_b16_8 t596 = GM_LD(gm_b16_8, ((uint8_t *)&t595));
            void * t597 = gm_l52;
            gm_tl874t2atnlh3 t598 = gm_l3;
            void * t599 = gm_f_vector_construction_fhm2mm5((void *)NULL, cx, NULL, t594, t596, t597, t598);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t600 = 0;
            F.ret = t599;
            goto gm_fret;
        }
        goto me9;
mn576:;
ma45:;
        uint8_t t604 = GM_LD(uint8_t, (((uint8_t *)&t8) + 0));
        if (t604 != ((uint8_t)24ULL)) goto pa603;
        goto po602;
pa603:;
        uint8_t t606 = GM_LD(uint8_t, (((uint8_t *)&t8) + 0));
        if (t606 != ((uint8_t)25ULL)) goto pa605;
        goto po602;
pa605:;
        uint8_t t608 = GM_LD(uint8_t, (((uint8_t *)&t8) + 0));
        if (t608 != ((uint8_t)26ULL)) goto pa607;
        goto po602;
pa607:;
        uint8_t t610 = GM_LD(uint8_t, (((uint8_t *)&t8) + 0));
        if (t610 != ((uint8_t)27ULL)) goto pa609;
        goto po602;
pa609:;
        uint8_t t612 = GM_LD(uint8_t, (((uint8_t *)&t8) + 0));
        if (t612 != ((uint8_t)29ULL)) goto pa611;
        goto po602;
pa611:;
        uint8_t t614 = GM_LD(uint8_t, (((uint8_t *)&t8) + 0));
        if (t614 != ((uint8_t)30ULL)) goto pa613;
        goto po602;
pa613:;
        uint8_t t616 = GM_LD(uint8_t, (((uint8_t *)&t8) + 0));
        if (t616 != ((uint8_t)31ULL)) goto pa615;
        goto po602;
pa615:;
        uint8_t t618 = GM_LD(uint8_t, (((uint8_t *)&t8) + 0));
        if (t618 != ((uint8_t)32ULL)) goto pa617;
        goto po602;
pa617:;
        uint8_t t620 = GM_LD(uint8_t, (((uint8_t *)&t8) + 0));
        if (t620 != ((uint8_t)33ULL)) goto pa619;
        goto po602;
pa619:;
        uint8_t t622 = GM_LD(uint8_t, (((uint8_t *)&t8) + 0));
        if (t622 != ((uint8_t)34ULL)) goto pa621;
        goto po602;
pa621:;
        goto mn601;
po602:;
        {
            void * t624 = gm_l0;
            gm_tl874t2atnlh3 t625 = gm_l3;
            gm_b16_8 t626;
            GM_ST(void *, (uint8_t *)&t626, (void *)(gm_safll42cn0mu7));
            GM_ST(uintptr_t, (uint8_t *)&t626 + 8, (uintptr_t)(30));
            gm_b0_1 t627 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t624, t625, t626);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t628 = gm_l0;
            void * t629 = GM_LD(void *, (((uint8_t *)t628) + 0));
            gm_tl874t2atnlh3 t630 = gm_l3;
            void * t631 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t629, t630);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t632 = 0;
            F.ret = t631;
            goto gm_fret;
        }
        goto me9;
mn601:;
ma46:;
        uint8_t t634 = GM_LD(uint8_t, (((uint8_t *)&t8) + 0));
        if (t634 != ((uint8_t)50ULL)) goto mn633;
        void * t635 = gm_l0;
        void * t636 = GM_LD(void *, (((uint8_t *)t635) + 0));
        gm_tl874t2atnlh3 t637 = gm_l3;
        void * t638 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t636, t637);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t639 = 0;
        F.ret = t638;
        goto gm_fret;
        goto me9;
mn633:;
ma47:;
        uint8_t t641 = GM_LD(uint8_t, (((uint8_t *)&t8) + 0));
        if (t641 != ((uint8_t)49ULL)) goto mn640;
        F.l53 = (*(gm_tet9guhev5r4u *)(((uint8_t *)&t8) + 8));
        void * t642 = gm_l0;
        gm_tl874t2atnlh3 t643 = gm_l3;
        void * t644 = gm_f_intrinsic_usmsf3f((void *)NULL, cx, NULL, t642, &(*(gm_tet9guhev5r4u *)((uint8_t *)&F.l53)), t643);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t645 = 0;
        F.ret = t644;
        goto gm_fret;
        goto me9;
mn640:;
ma48:;
        uint8_t t647 = GM_LD(uint8_t, (((uint8_t *)&t8) + 0));
        if (t647 != ((uint8_t)36ULL)) goto mn646;
        void * t648 = GM_LD(void *, (((uint8_t *)&t8) + 8));
        gm_l54 = t648;
        void * t649 = gm_l0;
        void * t650 = gm_l54;
        gm_tkn42u7na3t81 t651 = gm_l2;
        gm_t1s75i08qtptd t652;
        memset(&t652, 0, sizeof t652);
        gm_tl874t2atnlh3 t653 = gm_l3;
        void * t654 = gm_f_fn_literal_fiu78lb((void *)NULL, cx, NULL, t649, t650, t651, &t652, t653);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t655 = 0;
        F.ret = t654;
        goto gm_fret;
        goto me9;
mn646:;
ma49:;
        uint8_t t657 = GM_LD(uint8_t, (((uint8_t *)&t8) + 0));
        if (t657 != ((uint8_t)37ULL)) goto mn656;
        void * t658 = GM_LD(void *, (((uint8_t *)&t8) + 8));
        gm_l55 = t658;
        void * t659 = gm_l0;
        void * t660 = gm_l55;
        gm_tkn42u7na3t81 t661;
        memset(&t661, 0, sizeof t661);
        gm_tl874t2atnlh3 t662 = gm_l3;
        void * t663 = gm_f_handler_literal_avosuha((void *)NULL, cx, NULL, t659, t660, t661, t662);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t664 = 0;
        F.ret = t663;
        goto gm_fret;
        goto me9;
mn656:;
ma50:;
        uint8_t t666 = GM_LD(uint8_t, (((uint8_t *)&t8) + 0));
        if (t666 != ((uint8_t)42ULL)) goto mn665;
        void * t667 = GM_LD(void *, (((uint8_t *)&t8) + 8));
        gm_l56 = t667;
        void * t668 = gm_l0;
        void * t669 = gm_l56;
        gm_tkn42u7na3t81 t670 = gm_l2;
        void * t671 = gm_f_with_expr_vi78dtj((void *)NULL, cx, NULL, t668, t669, ((uint8_t)1), t670);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t672 = 0;
        F.ret = t671;
        goto gm_fret;
        goto me9;
mn665:;
ma51:;
        uint8_t t674 = GM_LD(uint8_t, (((uint8_t *)&t8) + 0));
        if (t674 != ((uint8_t)48ULL)) goto mn673;
        gm_t3mf58gv9kf3n t675 = GM_LD(gm_t3mf58gv9kf3n, (((uint8_t *)&t8) + 8));
        gm_l57 = t675;
        void * t676 = gm_l0;
        void * t677 = GM_LD(void *, (((uint8_t *)&gm_l57) + 0));
        void * t678 = GM_LD(void *, (((uint8_t *)&gm_l57) + 8));
        gm_tl874t2atnlh3 t679 = gm_l3;
        void * t680 = gm_f_resume_expr_l33thg3((void *)NULL, cx, NULL, t676, t677, t678, t679);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t681 = 0;
        F.ret = t680;
        goto gm_fret;
        goto me9;
mn673:;
ma52:;
        uint8_t t683 = GM_LD(uint8_t, (((uint8_t *)&t8) + 0));
        if (t683 != ((uint8_t)11ULL)) goto mn682;
        void * t684 = GM_LD(void *, (((uint8_t *)&t8) + 8));
        gm_l58 = t684;
        {
            void * t686 = gm_l0;
            void * t687 = gm_l58;
            gm_tkn42u7na3t81 t688;
            memset(&t688, 0, sizeof t688);
            void * t689 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t686, t687, t688);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l59 = t689;
        }
        {
            void * t690 = gm_l0;
            void * t691 = gm_l59;
            void * t692 = gm_l58;
            gm_tl874t2atnlh3 t693 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t692) + 72));
            void * t694 = gm_f_elements_tuple_gdm5n6q((void *)NULL, cx, NULL, t690, t691, t693);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            void * t695;
            if ((GM_LD(void *, ((uint8_t *)&t694)) != 0)) {
                void * t696 = GM_LD(void *, (((uint8_t *)&t694) + 0));
                t695 = t696;
            } else {
                void * t697 = gm_l0;
                void * t698 = GM_LD(void *, (((uint8_t *)t697) + 0));
                gm_tl874t2atnlh3 t699 = gm_l3;
                void * t700 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t698, t699);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                t695 = t700;
            }
            uint8_t t701 = 0;
            F.ret = t695;
            goto gm_fret;
        }
        goto me9;
mn682:;
ma53:;
        void * t703 = gm_l0;
        gm_tl874t2atnlh3 t704 = gm_l3;
        void * t705 = gm_l1;
        void * t706 = (void *)(((uint8_t *)t705) + 8);
        gm_b16_8 t707 = gm_f_expr_kind_text_6rlftr8(t706);
        void * t708 = gm_f_unsupported_r3r7qdh((void *)NULL, cx, NULL, t703, t704, t707);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t709 = 0;
        F.ret = t708;
        goto gm_fret;
        goto me9;
mn702:;
ma54:;
        gm_trap("no match arm matched the scrutinee", gm_srch2l5nl06vsci, 41, 5);
me9:;
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
__attribute__((aligned(4))) gm_b16_8 gm_f_expr_kind_text_6rlftr8(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_expr_kind_text_6rlftr8 F;
    void * gm_l0; /* k */
    GM_STACK_CHECK_SHALLOW(&F, 370u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    gm_b16_8 t2;
    uint8_t t23 = GM_LD(uint8_t, (((uint8_t *)t1) + 0));
    switch (t23) { case ((uint8_t)0ULL): goto ma21; case ((uint8_t)1ULL): goto ma21; case ((uint8_t)2ULL): goto ma21; case ((uint8_t)3ULL): goto ma21; case ((uint8_t)4ULL): goto ma21; case ((uint8_t)5ULL): goto ma21; case ((uint8_t)6ULL): goto ma21; case ((uint8_t)7ULL): goto ma21; case ((uint8_t)8ULL): goto ma21; case ((uint8_t)9ULL): goto ma21; case ((uint8_t)10ULL): goto ma21; case ((uint8_t)11ULL): goto ma4; case ((uint8_t)12ULL): goto ma21; case ((uint8_t)13ULL): goto ma21; case ((uint8_t)14ULL): goto ma5; case ((uint8_t)15ULL): goto ma6; case ((uint8_t)16ULL): goto ma7; case ((uint8_t)17ULL): goto ma8; case ((uint8_t)18ULL): goto ma21; case ((uint8_t)19ULL): goto ma21; case ((uint8_t)20ULL): goto ma9; case ((uint8_t)21ULL): goto ma10; case ((uint8_t)22ULL): goto ma21; case ((uint8_t)23ULL): goto ma21; case ((uint8_t)24ULL): goto ma21; case ((uint8_t)25ULL): goto ma21; case ((uint8_t)26ULL): goto ma21; case ((uint8_t)27ULL): goto ma21; case ((uint8_t)28ULL): goto ma11; case ((uint8_t)29ULL): goto ma21; case ((uint8_t)30ULL): goto ma21; case ((uint8_t)31ULL): goto ma21; case ((uint8_t)32ULL): goto ma21; case ((uint8_t)33ULL): goto ma21; case ((uint8_t)34ULL): goto ma21; case ((uint8_t)35ULL): goto ma12; case ((uint8_t)36ULL): goto ma13; case ((uint8_t)37ULL): goto ma14; case ((uint8_t)38ULL): goto ma15; case ((uint8_t)39ULL): goto ma16; case ((uint8_t)40ULL): goto ma21; case ((uint8_t)41ULL): goto ma17; case ((uint8_t)42ULL): goto ma18; case ((uint8_t)43ULL): goto ma19; case ((uint8_t)44ULL): goto ma21; case ((uint8_t)45ULL): goto ma21; case ((uint8_t)46ULL): goto ma21; case ((uint8_t)47ULL): goto ma21; case ((uint8_t)48ULL): goto ma20; case ((uint8_t)49ULL): goto ma21; case ((uint8_t)50ULL): goto ma21; default: goto ma21; }
ma4:;
    uint8_t t25 = GM_LD(uint8_t, (((uint8_t *)t1) + 0));
    if (t25 != ((uint8_t)11ULL)) goto mn24;
    gm_b16_8 t26;
    GM_ST(void *, (uint8_t *)&t26, (void *)(gm_ss9l5l0jp0vfb));
    GM_ST(uintptr_t, (uint8_t *)&t26 + 8, (uintptr_t)(13));
    t2 = t26;
    goto me3;
mn24:;
ma5:;
    uint8_t t28 = GM_LD(uint8_t, (((uint8_t *)t1) + 0));
    if (t28 != ((uint8_t)14ULL)) goto mn27;
    gm_b16_8 t29;
    GM_ST(void *, (uint8_t *)&t29, (void *)(gm_slj1im0lsqj9h));
    GM_ST(uintptr_t, (uint8_t *)&t29 + 8, (uintptr_t)(31));
    t2 = t29;
    goto me3;
mn27:;
ma6:;
    uint8_t t31 = GM_LD(uint8_t, (((uint8_t *)t1) + 0));
    if (t31 != ((uint8_t)15ULL)) goto mn30;
    gm_b16_8 t32;
    GM_ST(void *, (uint8_t *)&t32, (void *)(gm_su30fl1m1ebg6));
    GM_ST(uintptr_t, (uint8_t *)&t32 + 8, (uintptr_t)(4));
    t2 = t32;
    goto me3;
mn30:;
ma7:;
    uint8_t t34 = GM_LD(uint8_t, (((uint8_t *)t1) + 0));
    if (t34 != ((uint8_t)16ULL)) goto mn33;
    gm_b16_8 t35;
    GM_ST(void *, (uint8_t *)&t35, (void *)(gm_s6i1ull1350kv));
    GM_ST(uintptr_t, (uint8_t *)&t35 + 8, (uintptr_t)(4));
    t2 = t35;
    goto me3;
mn33:;
ma8:;
    uint8_t t37 = GM_LD(uint8_t, (((uint8_t *)t1) + 0));
    if (t37 != ((uint8_t)17ULL)) goto mn36;
    gm_b16_8 t38;
    GM_ST(void *, (uint8_t *)&t38, (void *)(gm_sncbtqrmp1734));
    GM_ST(uintptr_t, (uint8_t *)&t38 + 8, (uintptr_t)(13));
    t2 = t38;
    goto me3;
mn36:;
ma9:;
    uint8_t t40 = GM_LD(uint8_t, (((uint8_t *)t1) + 0));
    if (t40 != ((uint8_t)20ULL)) goto mn39;
    gm_b16_8 t41;
    GM_ST(void *, (uint8_t *)&t41, (void *)(gm_suj5r301h9vv4));
    GM_ST(uintptr_t, (uint8_t *)&t41 + 8, (uintptr_t)(8));
    t2 = t41;
    goto me3;
mn39:;
ma10:;
    uint8_t t43 = GM_LD(uint8_t, (((uint8_t *)t1) + 0));
    if (t43 != ((uint8_t)21ULL)) goto mn42;
    gm_b16_8 t44;
    GM_ST(void *, (uint8_t *)&t44, (void *)(gm_s6n6v50inu7p0));
    GM_ST(uintptr_t, (uint8_t *)&t44 + 8, (uintptr_t)(7));
    t2 = t44;
    goto me3;
mn42:;
ma11:;
    uint8_t t46 = GM_LD(uint8_t, (((uint8_t *)t1) + 0));
    if (t46 != ((uint8_t)28ULL)) goto mn45;
    gm_b16_8 t47;
    GM_ST(void *, (uint8_t *)&t47, (void *)(gm_s3nknbqjesra1));
    GM_ST(uintptr_t, (uint8_t *)&t47 + 8, (uintptr_t)(18));
    t2 = t47;
    goto me3;
mn45:;
ma12:;
    uint8_t t49 = GM_LD(uint8_t, (((uint8_t *)t1) + 0));
    if (t49 != ((uint8_t)35ULL)) goto mn48;
    gm_b16_8 t50;
    GM_ST(void *, (uint8_t *)&t50, (void *)(gm_s6tc15kuvnm0c));
    GM_ST(uintptr_t, (uint8_t *)&t50 + 8, (uintptr_t)(25));
    t2 = t50;
    goto me3;
mn48:;
ma13:;
    uint8_t t52 = GM_LD(uint8_t, (((uint8_t *)t1) + 0));
    if (t52 != ((uint8_t)36ULL)) goto mn51;
    gm_b16_8 t53;
    GM_ST(void *, (uint8_t *)&t53, (void *)(gm_sn05fk3conlo9));
    GM_ST(uintptr_t, (uint8_t *)&t53 + 8, (uintptr_t)(17));
    t2 = t53;
    goto me3;
mn51:;
ma14:;
    uint8_t t55 = GM_LD(uint8_t, (((uint8_t *)t1) + 0));
    if (t55 != ((uint8_t)37ULL)) goto mn54;
    gm_b16_8 t56;
    GM_ST(void *, (uint8_t *)&t56, (void *)(gm_sbqhd9c69ol7k));
    GM_ST(uintptr_t, (uint8_t *)&t56 + 8, (uintptr_t)(16));
    t2 = t56;
    goto me3;
mn54:;
ma15:;
    uint8_t t58 = GM_LD(uint8_t, (((uint8_t *)t1) + 0));
    if (t58 != ((uint8_t)38ULL)) goto mn57;
    gm_b16_8 t59;
    GM_ST(void *, (uint8_t *)&t59, (void *)(gm_ss4rn3b9npsij));
    GM_ST(uintptr_t, (uint8_t *)&t59 + 8, (uintptr_t)(13));
    t2 = t59;
    goto me3;
mn57:;
ma16:;
    uint8_t t61 = GM_LD(uint8_t, (((uint8_t *)t1) + 0));
    if (t61 != ((uint8_t)39ULL)) goto mn60;
    gm_b16_8 t62;
    GM_ST(void *, (uint8_t *)&t62, (void *)(gm_s8952afsouaih));
    GM_ST(uintptr_t, (uint8_t *)&t62 + 8, (uintptr_t)(20));
    t2 = t62;
    goto me3;
mn60:;
ma17:;
    uint8_t t64 = GM_LD(uint8_t, (((uint8_t *)t1) + 0));
    if (t64 != ((uint8_t)41ULL)) goto mn63;
    gm_b16_8 t65;
    GM_ST(void *, (uint8_t *)&t65, (void *)(gm_s1leke9tn7lrm));
    GM_ST(uintptr_t, (uint8_t *)&t65 + 8, (uintptr_t)(7));
    t2 = t65;
    goto me3;
mn63:;
ma18:;
    uint8_t t67 = GM_LD(uint8_t, (((uint8_t *)t1) + 0));
    if (t67 != ((uint8_t)42ULL)) goto mn66;
    gm_b16_8 t68;
    GM_ST(void *, (uint8_t *)&t68, (void *)(gm_sdpig9sk6ngdh));
    GM_ST(uintptr_t, (uint8_t *)&t68 + 8, (uintptr_t)(6));
    t2 = t68;
    goto me3;
mn66:;
ma19:;
    uint8_t t70 = GM_LD(uint8_t, (((uint8_t *)t1) + 0));
    if (t70 != ((uint8_t)43ULL)) goto mn69;
    gm_b16_8 t71;
    GM_ST(void *, (uint8_t *)&t71, (void *)(gm_s1c0ll7cvuidm));
    GM_ST(uintptr_t, (uint8_t *)&t71 + 8, (uintptr_t)(11));
    t2 = t71;
    goto me3;
mn69:;
ma20:;
    uint8_t t73 = GM_LD(uint8_t, (((uint8_t *)t1) + 0));
    if (t73 != ((uint8_t)48ULL)) goto mn72;
    gm_b16_8 t74;
    GM_ST(void *, (uint8_t *)&t74, (void *)(gm_s0l72j5pkqi6s));
    GM_ST(uintptr_t, (uint8_t *)&t74 + 8, (uintptr_t)(8));
    t2 = t74;
    goto me3;
mn72:;
ma21:;
    gm_b16_8 t76;
    GM_ST(void *, (uint8_t *)&t76, (void *)(gm_sgfmudgd82uhr));
    GM_ST(uintptr_t, (uint8_t *)&t76 + 8, (uintptr_t)(23));
    t2 = t76;
    goto me3;
mn75:;
ma22:;
    gm_trap("no match arm matched the scrutinee", gm_srch2l5nl06vsci, 160, 59);
me3:;
    F.ret = t2;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) void * gm_f_expr_vmp704u(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_tkn42u7na3t81 a2) {
    struct gm_fr_expr_vmp704u F;
    void * gm_l0; /* cx */
    void * gm_l1; /* e */
    gm_tkn42u7na3t81 gm_l2; /* expect */
    void * gm_l3; /* r */
    void * gm_l6; /* r */
    GM_STACK_CHECK(&F, 882u);
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
        uint8_t t8 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
        if (t8 != ((uint8_t)13ULL)) goto mn7;
        t5 = ((uint8_t)1);
        goto me6;
mn7:;
        t5 = ((uint8_t)0);
        goto me6;
mn9:;
        gm_trap("no match arm matched the scrutinee", gm_srch2l5nl06vsci, 10, 8);
me6:;
        if (!t5) {
            void * t10 = gm_l1;
            uint8_t t11;
            uint8_t t14 = GM_LD(uint8_t, ((((uint8_t *)t10) + 8) + 0));
            if (t14 != ((uint8_t)12ULL)) goto mn13;
            t11 = ((uint8_t)1);
            goto me12;
mn13:;
            t11 = ((uint8_t)0);
            goto me12;
mn15:;
            gm_trap("no match arm matched the scrutinee", gm_srch2l5nl06vsci, 10, 32);
me12:;
            t3 = t11;
        } else t3 = 1;
        if (!t3) goto ie2;
        {
            void * t18 = gm_l0;
            uint8_t t19 = GM_LD(uint8_t, (((uint8_t *)t18) + 1288));
            if (!t19) goto ie17;
            {
                void * t21 = gm_l0;
                void * t22 = gm_l1;
                gm_tkn42u7na3t81 t23 = gm_l2;
                void * t24 = gm_f_expr_kind_397ptdj((void *)NULL, cx, NULL, t21, t22, t23);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t25 = 0;
                F.ret = t24;
                goto gm_fret;
            }
            goto ix17;
ie17:;
ix17:;
        }
        {
            void * t27 = gm_l0;
            void * t28 = gm_l1;
            uint8_t t29 = gm_f_mentions_complex_constan_lrgkqac((void *)NULL, cx, NULL, t27, t28);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t30 = (uint8_t)!t29;
            if (!t30) goto ie26;
            {
                void * t32 = gm_l0;
                GM_ST(uint8_t, (((uint8_t *)t32) + 1288), ((uint8_t)1));
            }
            {
                void * t33 = gm_l0;
                void * t34 = gm_l1;
                gm_tkn42u7na3t81 t35 = gm_l2;
                void * t36 = gm_f_expr_kind_397ptdj((void *)NULL, cx, NULL, t33, t34, t35);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l3 = t36;
            }
            {
                void * t37 = gm_l0;
                GM_ST(uint8_t, (((uint8_t *)t37) + 1288), ((uint8_t)0));
            }
            {
                void * t38 = gm_l3;
                uint8_t t39 = 0;
                F.ret = t38;
                goto gm_fret;
            }
            goto ix26;
ie26:;
ix26:;
        }
        {
            uint8_t t41 = 0;
            void * t42 = gm_l1;
            uint8_t t43;
            uint8_t t46 = GM_LD(uint8_t, ((((uint8_t *)t42) + 8) + 0));
            if (t46 != ((uint8_t)13ULL)) goto mn45;
            t43 = ((uint8_t)1);
            goto me44;
mn45:;
            t43 = ((uint8_t)0);
            goto me44;
mn47:;
            gm_trap("no match arm matched the scrutinee", gm_srch2l5nl06vsci, 18, 12);
me44:;
            if (!t43) {
                void * t48 = gm_l1;
                uint8_t t49;
                uint8_t t52 = GM_LD(uint8_t, ((((uint8_t *)t48) + 8) + 0));
                if (t52 != ((uint8_t)12ULL)) goto mn51;
                uint8_t t53 = GM_LD(uint8_t, ((((((uint8_t *)t48) + 8) + 8) + 0) + 0));
                if (t53 != ((uint8_t)0ULL)) goto mn51;
                t49 = ((uint8_t)1);
                goto me50;
mn51:;
                t49 = ((uint8_t)0);
                goto me50;
mn54:;
                gm_trap("no match arm matched the scrutinee", gm_srch2l5nl06vsci, 18, 36);
me50:;
                t41 = t49;
            } else t41 = 1;
            if (!t41) goto ie40;
            {
                void * t57 = gm_l0;
                void * t58 = gm_l1;
                gm_tkv1vap90vht2 t59;
                gm_f_try_untyped_8j8up35((void *)&t59, (void *)NULL, cx, NULL, t57, t58);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (!((((uint8_t *)&t59))[208] != 0)) goto ie56;
                F.l4 = (*(gm_t5vbkdq81qfs7 *)(((uint8_t *)&t59) + 0));
                {
                    uint8_t t63 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 0));
                    if (t63 != ((uint8_t)0ULL)) goto mn62;
                    gm_t0ialelq3ql1k t64 = GM_LD(gm_t0ialelq3ql1k, (((uint8_t *)&F.l4) + 8));
                    F.l5 = t64;
                    void * t65 = gm_l0;
                    void * t66 = (void *)((uint8_t *)&F.l5);
                    void * t67 = gm_l1;
                    gm_tl874t2atnlh3 t68 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t67) + 72));
                    void * t69 = gm_f_untyped_literal_pfu1ft4((void *)NULL, cx, NULL, t65, t66, t68);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t70 = 0;
                    F.ret = t69;
                    goto gm_fret;
                    goto me61;
mn62:;
                    uint8_t t72 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 0));
                    if (t72 != ((uint8_t)1ULL)) goto mn71;
                    void * t73 = gm_l0;
                    void * t74 = GM_LD(void *, (((uint8_t *)t73) + 0));
                    void * t75 = gm_l1;
                    gm_tl874t2atnlh3 t76 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t75) + 72));
                    void * t77 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t74, t76);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t78 = 0;
                    F.ret = t77;
                    goto gm_fret;
                    goto me61;
mn71:;
                    gm_trap("no match arm matched the scrutinee", gm_srch2l5nl06vsci, 20, 17);
me61:;
                }
                goto ix56;
ie56:;
ix56:;
            }
            goto ix40;
ie40:;
ix40:;
        }
        {
            void * t79 = gm_l0;
            void * t80 = gm_l1;
            gm_tkn42u7na3t81 t81 = gm_l2;
            void * t82 = gm_f_expr_kind_397ptdj((void *)NULL, cx, NULL, t79, t80, t81);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t83 = 0;
            F.ret = t82;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t85 = gm_l0;
        uint8_t t86 = GM_LD(uint8_t, (((uint8_t *)t85) + 1288));
        if (!t86) goto ie84;
        void * t87 = gm_l1;
        uint8_t t88;
        uint8_t t91 = GM_LD(uint8_t, ((((uint8_t *)t87) + 8) + 0));
        if (t91 != ((uint8_t)9ULL)) goto mn90;
        t88 = ((uint8_t)0);
        goto me89;
mn90:;
        t88 = ((uint8_t)1);
        goto me89;
mn92:;
        gm_trap("no match arm matched the scrutinee", gm_srch2l5nl06vsci, 28, 28);
me89:;
        if (!t88) goto ie84;
        {
            void * t94 = gm_l0;
            GM_ST(uint8_t, (((uint8_t *)t94) + 1288), ((uint8_t)0));
        }
        {
            void * t95 = gm_l0;
            void * t96 = gm_l1;
            gm_tkn42u7na3t81 t97 = gm_l2;
            void * t98 = gm_f_expr_kind_397ptdj((void *)NULL, cx, NULL, t95, t96, t97);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l6 = t98;
        }
        {
            void * t99 = gm_l0;
            GM_ST(uint8_t, (((uint8_t *)t99) + 1288), ((uint8_t)1));
        }
        {
            void * t100 = gm_l6;
            uint8_t t101 = 0;
            F.ret = t100;
            goto gm_fret;
        }
        goto ix84;
ie84:;
ix84:;
    }
    {
        void * t102 = gm_l0;
        void * t103 = gm_l1;
        gm_tkn42u7na3t81 t104 = gm_l2;
        void * t105 = gm_f_expr_kind_397ptdj((void *)NULL, cx, NULL, t102, t103, t104);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t106 = 0;
        F.ret = t105;
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
__attribute__((aligned(4))) void * gm_f_field_jlden9b(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, const void * a2, gm_tl874t2atnlh3 a3) {
    struct gm_fr_field_jlden9b F;
    void * gm_l0; /* cx */
    void * gm_l1; /* base */
    gm_t08olq97rscn3 gm_l2; /* name */
    gm_tl874t2atnlh3 gm_l3; /* span */
    uint64_t gm_l4; /* m */
    uint64_t gm_l5; /* d */
    gm_t88rcucg1b7uk gm_l6; /* t */
    void * gm_l7; /* inner */
    void * gm_l8; /* index */
    void * gm_l9; /* ib */
    gm_tjpqvfd72o62u gm_l10; /* info */
    GM_STACK_CHECK(&F, 763u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l3 = a3;
    {
        void * t3 = gm_l0;
        void * t4 = gm_l1;
        gm_tf5srh4mp4b3i t5 = gm_f_module_of_n0d5f6n((void *)NULL, cx, NULL, t3, t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (!((((uint8_t *)&t5))[8] != 0)) goto ie2;
        uint64_t t6 = GM_LD(uint64_t, ((uint8_t *)&t5));
        gm_l4 = t6;
        {
            void * t8 = gm_l0;
            void * t9 = GM_LD(void *, (((uint8_t *)t8) + 0));
            uint64_t t10 = gm_l4;
            void * t11 = gm_l0;
            uint64_t t12 = GM_LD(uint64_t, (((uint8_t *)t11) + 8));
            gm_tf5srh4mp4b3i t13 = gm_f_module_member_5fd2qm9((void *)NULL, cx, NULL, t9, t10, &(*(gm_t08olq97rscn3 *)((uint8_t *)a2)), t12);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint64_t t14;
            if (((((uint8_t *)&t13))[8] != 0)) {
                uint64_t t15 = GM_LD(uint64_t, (((uint8_t *)&t13) + 0));
                t14 = t15;
            } else {
                void * t16 = gm_l0;
                void * t17 = GM_LD(void *, (((uint8_t *)t16) + 0));
                gm_tl874t2atnlh3 t18 = gm_l3;
                void * t19 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t17, t18);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t20 = 0;
                F.ret = t19;
                goto gm_fret;
            }
            gm_l5 = t14;
        }
        {
            void * t21 = gm_l0;
            uint64_t t22 = gm_l5;
            gm_b16_8 t23 = GM_LD(gm_b16_8, (((uint8_t *)a2) + 0));
            gm_tkn42u7na3t81 t24;
            memset(&t24, 0, sizeof t24);
            gm_tl874t2atnlh3 t25 = gm_l3;
            void * t26 = gm_f_decl_expr_lv2h8vf((void *)NULL, cx, NULL, t21, t22, t23, t24, t25);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t27 = 0;
            F.ret = t26;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t29 = gm_l0;
        void * t30 = gm_l1;
        gm_tkn42u7na3t81 t31 = gm_f_type_of_name_oqtujpn((void *)NULL, cx, NULL, t29, t30);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (!((((uint8_t *)&t31))[4] != 0)) goto ie28;
        gm_t88rcucg1b7uk t32 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&t31));
        gm_l6 = t32;
        {
            void * t34 = gm_l0;
            gm_t88rcucg1b7uk t35 = gm_l6;
            gm_th2lqbo77pvsc t36;
            memset(&t36, 0, sizeof t36);
            gm_tl874t2atnlh3 t37 = gm_l3;
            void * t38 = gm_f_construct_variant_o6is81c((void *)NULL, cx, NULL, t34, t35, &(*(gm_t08olq97rscn3 *)((uint8_t *)a2)), &t36, t37);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t39 = 0;
            F.ret = t38;
            goto gm_fret;
        }
        goto ix28;
ie28:;
ix28:;
    }
    {
        void * t41 = gm_l1;
        uint8_t t42 = GM_LD(uint8_t, ((((uint8_t *)t41) + 8) + 0));
        if (t42 != ((uint8_t)20ULL)) goto ie40;
        void * t43 = GM_LD(void *, (((((uint8_t *)t41) + 8) + 8) + 0));
        gm_l7 = t43;
        void * t44 = GM_LD(void *, (((((uint8_t *)t41) + 8) + 8) + 8));
        gm_l8 = t44;
        {
            void * t46 = gm_l0;
            void * t47 = gm_l0;
            void * t48 = gm_l7;
            gm_tkn42u7na3t81 t49;
            memset(&t49, 0, sizeof t49);
            void * t50 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t47, t48, t49);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            void * t51 = gm_f_soa_deref_uiefo9t((void *)NULL, cx, NULL, t46, t50);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l9 = t51;
        }
        {
            void * t53 = gm_l0;
            void * t54 = gm_l9;
            gm_t8cc1mtbrhlpt t55;
            gm_f_soa_of_jufdcj2((void *)&t55, (void *)NULL, cx, NULL, t53, t54);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (!((((uint8_t *)&t55))[32] != 0)) goto ie52;
            gm_l10 = (*(gm_tjpqvfd72o62u *)(((uint8_t *)&t55) + 0));
            {
                void * t57 = gm_l0;
                void * t58 = gm_l9;
                void * t59 = gm_l8;
                gm_tl874t2atnlh3 t60 = gm_l3;
                void * t61 = gm_f_soa_element_field_lo71q0l((void *)NULL, cx, NULL, t57, t58, &(*(gm_tjpqvfd72o62u *)((uint8_t *)&gm_l10)), t59, &(*(gm_t08olq97rscn3 *)((uint8_t *)a2)), t60);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t62 = 0;
                F.ret = t61;
                goto gm_fret;
            }
            goto ix52;
ie52:;
ix52:;
        }
        {
            void * t63 = gm_l0;
            void * t64 = gm_l0;
            void * t65 = gm_l9;
            void * t66 = gm_l8;
            void * t67 = gm_l1;
            gm_tl874t2atnlh3 t68 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t67) + 72));
            void * t69 = gm_f_index_value_ufcur8j((void *)NULL, cx, NULL, t64, t65, t66, t68);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tl874t2atnlh3 t70 = gm_l3;
            void * t71 = gm_f_field_of_1bfelct((void *)NULL, cx, NULL, t63, t69, &(*(gm_t08olq97rscn3 *)((uint8_t *)a2)), t70);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t72 = 0;
            F.ret = t71;
            goto gm_fret;
        }
        goto ix40;
ie40:;
ix40:;
    }
    {
        void * t73 = gm_l0;
        void * t74 = gm_l0;
        void * t75 = gm_l1;
        gm_tkn42u7na3t81 t76;
        memset(&t76, 0, sizeof t76);
        void * t77 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t74, t75, t76);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tl874t2atnlh3 t78 = gm_l3;
        void * t79 = gm_f_field_of_1bfelct((void *)NULL, cx, NULL, t73, t77, &(*(gm_t08olq97rscn3 *)((uint8_t *)a2)), t78);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t80 = 0;
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
__attribute__((aligned(4))) void * gm_f_field_of_1bfelct(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, const void * a2, gm_tl874t2atnlh3 a3) {
    struct gm_fr_field_of_1bfelct F;
    void * gm_l0; /* cx */
    void * gm_l1; /* b0 */
    gm_t08olq97rscn3 gm_l2; /* name */
    gm_tl874t2atnlh3 gm_l3; /* span */
    void * gm_l4; /* tys */
    void * gm_l5; /* b */
    gm_tjpqvfd72o62u gm_l6; /* info */
    gm_t88rcucg1b7uk gm_l7; /* bt */
    gm_b16_8 gm_l8; /* elems */
    gm_b16_8 gm_l9; /* names */
    gm_b16_8 gm_l10; /* <tmp> */
    uint64_t gm_l11; /* <tmp> */
    uint64_t gm_l12; /* i */
    gm_b16_8 gm_l13; /* n */
    uint8_t gm_l14; /* c */
    gm_t88rcucg1b7uk gm_l15; /* e */
    uint64_t gm_l16; /* len */
    gm_t88rcucg1b7uk gm_l17; /* len */
    void * gm_l18; /* e */
    GM_STACK_CHECK(&F, 1628u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l3 = a3;
    {
        void * t2 = gm_l0;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 0));
        void * t4 = (void *)(((uint8_t *)t3) + 8);
        gm_l4 = t4;
    }
    {
        void * t5 = gm_l0;
        void * t6 = gm_l1;
        void * t7 = gm_f_soa_deref_uiefo9t((void *)NULL, cx, NULL, t5, t6);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l5 = t7;
    }
    {
        void * t9 = gm_l0;
        void * t10 = gm_l5;
        gm_t8cc1mtbrhlpt t11;
        gm_f_soa_of_jufdcj2((void *)&t11, (void *)NULL, cx, NULL, t9, t10);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (!((((uint8_t *)&t11))[32] != 0)) goto ie8;
        gm_l6 = (*(gm_tjpqvfd72o62u *)(((uint8_t *)&t11) + 0));
        {
            void * t13 = gm_l0;
            void * t14 = gm_l5;
            gm_tl874t2atnlh3 t15 = gm_l3;
            void * t16 = gm_f_soa_member_vkrr5kf((void *)NULL, cx, NULL, t13, t14, &(*(gm_tjpqvfd72o62u *)((uint8_t *)&gm_l6)), &(*(gm_t08olq97rscn3 *)((uint8_t *)a2)), t15);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t17 = 0;
            F.ret = t16;
            goto gm_fret;
        }
        goto ix8;
ie8:;
ix8:;
    }
    {
        void * t18 = gm_l0;
        void * t19 = gm_l5;
        void * t20 = gm_f_auto_deref_vdb1hdn((void *)NULL, cx, NULL, t18, t19);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l5 = t20;
    }
    {
        void * t21 = gm_l0;
        void * t22 = (void *)(((uint8_t *)t21) + 96);
        void * t23 = gm_l0;
        void * t24 = GM_LD(void *, (((uint8_t *)t23) + 0));
        void * t25 = gm_l5;
        gm_t88rcucg1b7uk t26 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t25) + 0));
        gm_t88rcucg1b7uk t27 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t22, t24, t26);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l7 = t27;
    }
    {
        void * t28 = gm_l4;
        gm_t88rcucg1b7uk t29 = gm_l7;
        gm_t53at80pfdus3 t30;
        gm_f_kind_jcnt4hm((void *)&t30, t28, GM_CONV(gm_csguankko4lmhd, t29));
        uint8_t t39 = GM_LD(uint8_t, (((uint8_t *)&t30) + 0));
        switch (t39) { case ((uint8_t)0ULL): goto ma37; case ((uint8_t)1ULL): goto ma37; case ((uint8_t)2ULL): goto ma37; case ((uint8_t)3ULL): goto ma37; case ((uint8_t)4ULL): goto ma37; case ((uint8_t)5ULL): goto ma37; case ((uint8_t)6ULL): goto ma37; case ((uint8_t)7ULL): goto ma37; case ((uint8_t)8ULL): goto ma37; case ((uint8_t)9ULL): goto ma37; case ((uint8_t)10ULL): goto ma33; case ((uint8_t)11ULL): goto ma34; case ((uint8_t)12ULL): goto ma32; case ((uint8_t)13ULL): goto ma37; case ((uint8_t)14ULL): goto ma37; case ((uint8_t)15ULL): goto ma37; case ((uint8_t)16ULL): goto ma37; case ((uint8_t)17ULL): goto ma37; case ((uint8_t)18ULL): goto ma37; case ((uint8_t)19ULL): goto ma37; case ((uint8_t)20ULL): goto ma35; case ((uint8_t)21ULL): goto ma37; case ((uint8_t)22ULL): goto ma37; case ((uint8_t)23ULL): goto ma37; case ((uint8_t)24ULL): goto ma37; case ((uint8_t)25ULL): goto ma37; case ((uint8_t)26ULL): goto ma36; default: goto ma37; }
ma32:;
        uint8_t t41 = GM_LD(uint8_t, (((uint8_t *)&t30) + 0));
        if (t41 != ((uint8_t)12ULL)) goto mn40;
        gm_b16_8 t42 = GM_LD(gm_b16_8, ((((uint8_t *)&t30) + 8) + 0));
        gm_l8 = t42;
        gm_b16_8 t43 = GM_LD(gm_b16_8, ((((uint8_t *)&t30) + 8) + 16));
        gm_l9 = t43;
        {
            {
                gm_b16_8 t46 = gm_l9;
                gm_l10 = t46;
            }
            {
                gm_l11 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t48 = gm_l11;
                    uint64_t t49 = GM_LD(uintptr_t, ((uint8_t *)&gm_l10) + 8);
                    uint8_t t50 = (t48 < t49);
                    if (!t50) goto lb47;
                    {
                        gm_tf1bjg81u0glv t53;
                        uint64_t t54 = gm_l11;
                        GM_ST(uint64_t, (uint8_t *)&t53 + 0, t54);
                        uint64_t t55 = gm_l11;
                        uint64_t t56 = (uintptr_t)t55;
                        uint64_t t57 = GM_LD(uintptr_t, ((uint8_t *)&gm_l10) + 8);
                        gm_b16_8 t58 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l10)) + t56 * 16));
                        GM_ST(gm_b16_8, (uint8_t *)&t53 + 8, t58);
                        uint64_t t61 = GM_LD(uint64_t, (((uint8_t *)&t53) + 0));
                        gm_l12 = t61;
                        gm_b16_8 t62 = GM_LD(gm_b16_8, (((uint8_t *)&t53) + 8));
                        gm_l13 = t62;
                        {
                            gm_b16_8 t65 = gm_l13;
                            gm_b16_8 t66 = GM_LD(gm_b16_8, (((uint8_t *)a2) + 0));
                            uint8_t t67 = (gm_eq0iqiubfm7e(((uint8_t *)&t65), ((uint8_t *)&t66)));
                            if (!t67) goto ie64;
                            {
                                uint64_t t69 = gm_l12;
                                uint64_t t70 = (uintptr_t)t69;
                                uint64_t t71 = GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8);
                                if (t70 >= (uintptr_t)(t71)) gm_trap_index(t70, t71, gm_srch2l5nl06vsci, 1113, 51);
                                gm_t88rcucg1b7uk t72 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l8)) + t70 * 4));
                                void * t73 = gm_l5;
                                uint64_t t74 = gm_l12;
                                uint32_t t75 = (uint32_t)t74;
                                gm_tbl8il92l97ut t76;
                                GM_ST(uint32_t, (uint8_t *)&t76 + 12, 0);
                                GM_ST(void *, ((uint8_t *)&t76 + 0), t73);
                                GM_ST(uint32_t, ((uint8_t *)&t76 + 8), t75);
                                gm_teti411eujvf4 t77;
                                memset(&t77, 0, sizeof t77);
                                GM_ST(uint8_t, ((uint8_t *)&t77 + 0), ((uint8_t)19ULL));
                                GM_ST(gm_tbl8il92l97ut, ((uint8_t *)&t77 + 8), t76);
                                gm_tl874t2atnlh3 t78 = gm_l3;
                                void * t79 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t72, &t77, t78);
                                if (gm_unw.kind) {
                                    goto gm_ll51;
                                }
                                uint8_t t80 = 0;
                                F.ret = t79;
                                goto gm_fret;
                            }
                            goto ix64;
ie64:;
ix64:;
                        }
                        goto me59;
mn60:;
                        gm_trap("no match arm matched the scrutinee", gm_srch2l5nl06vsci, 1112, 13);
me59:;
                    }
lc47:;
                    {
                        uint64_t t82 = gm_l11;
                        uint64_t r83; bool o84 = __builtin_add_overflow(t82, ((uint64_t)1ULL), &r83);
                        uint64_t t85 = r83;
                        gm_l11 = t85;
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
        goto me31;
mn40:;
ma33:;
        uint8_t t87 = GM_LD(uint8_t, (((uint8_t *)&t30) + 0));
        if (t87 != ((uint8_t)10ULL)) goto mn86;
        uint8_t t88 = GM_LD(uint8_t, ((((uint8_t *)&t30) + 8) + 0));
        gm_l14 = t88;
        gm_t88rcucg1b7uk t89 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t30) + 8) + 4));
        gm_l15 = t89;
        {
            uint8_t t92 = (((GM_LD(uintptr_t, (((uint8_t *)a2) + 0) + 8) == 3) && memcmp(GM_LD(void *, (((uint8_t *)a2) + 0)), gm_sm2k5tp7dtg07, 3) == 0));
            if (!t92) goto ie91;
            {
                void * t94 = gm_l4;
                gm_t88rcucg1b7uk t95 = gm_f_usize_ty_qnrd30f((void *)NULL, cx, NULL, t94);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                void * t96 = gm_l5;
                gm_teti411eujvf4 t97;
                memset(&t97, 0, sizeof t97);
                GM_ST(uint8_t, ((uint8_t *)&t97 + 0), ((uint8_t)22ULL));
                GM_ST(void *, ((uint8_t *)&t97 + 8), t96);
                gm_tl874t2atnlh3 t98 = gm_l3;
                void * t99 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t95, &t97, t98);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t100 = 0;
                F.ret = t99;
                goto gm_fret;
            }
            goto ix91;
ie91:;
ix91:;
        }
        {
            uint8_t t102 = (((GM_LD(uintptr_t, (((uint8_t *)a2) + 0) + 8) == 3) && memcmp(GM_LD(void *, (((uint8_t *)a2) + 0)), gm_s55kqbt4f7gau, 3) == 0));
            if (!t102) goto ie101;
            {
                void * t104 = gm_l4;
                uint8_t t105 = gm_l14;
                gm_t88rcucg1b7uk t106 = gm_l15;
                gm_t88rcucg1b7uk t107 = gm_f_many_ptr_l4v11f8((void *)NULL, cx, NULL, t104, t105, t106);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                void * t108 = gm_l5;
                gm_teti411eujvf4 t109;
                memset(&t109, 0, sizeof t109);
                GM_ST(uint8_t, ((uint8_t *)&t109 + 0), ((uint8_t)23ULL));
                GM_ST(void *, ((uint8_t *)&t109 + 8), t108);
                gm_tl874t2atnlh3 t110 = gm_l3;
                void * t111 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t107, &t109, t110);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t112 = 0;
                F.ret = t111;
                goto gm_fret;
            }
            goto ix101;
ie101:;
ix101:;
        }
        goto me31;
mn86:;
ma34:;
        uint8_t t114 = GM_LD(uint8_t, (((uint8_t *)&t30) + 0));
        if (t114 != ((uint8_t)11ULL)) goto mn113;
        uint64_t t115 = GM_LD(uint64_t, ((((uint8_t *)&t30) + 8) + 0));
        gm_l16 = t115;
        uint8_t t117 = (((GM_LD(uintptr_t, (((uint8_t *)a2) + 0) + 8) == 3) && memcmp(GM_LD(void *, (((uint8_t *)a2) + 0)), gm_sm2k5tp7dtg07, 3) == 0));
        if (!t117) goto ie116;
        {
            void * t119 = gm_l4;
            gm_t88rcucg1b7uk t120 = gm_f_usize_ty_qnrd30f((void *)NULL, cx, NULL, t119);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint64_t t121 = gm_l16;
            gm_tbci5ospk5nnn t122;
            gm_f_from_u64_ud13q5v((void *)&t122, (void *)NULL, cx, NULL, t121);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_teti411eujvf4 t123;
            GM_ST(uint8_t, (uint8_t *)&t123 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t123 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t123 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t123 + 56, 0);
            GM_ST(uint8_t, ((uint8_t *)&t123 + 0), ((uint8_t)0ULL));
            GM_ST(gm_tbci5ospk5nnn, ((uint8_t *)&t123 + 8), t122);
            gm_tl874t2atnlh3 t124 = gm_l3;
            void * t125 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t120, &t123, t124);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t126 = 0;
            F.ret = t125;
            goto gm_fret;
        }
        goto ix116;
ie116:;
ix116:;
        goto me31;
mn113:;
ma35:;
        uint8_t t128 = GM_LD(uint8_t, (((uint8_t *)&t30) + 0));
        if (t128 != ((uint8_t)20ULL)) goto mn127;
        gm_t88rcucg1b7uk t129 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t30) + 8) + 0));
        gm_l17 = t129;
        uint8_t t131 = (((GM_LD(uintptr_t, (((uint8_t *)a2) + 0) + 8) == 3) && memcmp(GM_LD(void *, (((uint8_t *)a2) + 0)), gm_sm2k5tp7dtg07, 3) == 0));
        if (!t131) goto ie130;
        {
            void * t133 = gm_l0;
            gm_t88rcucg1b7uk t134 = gm_l17;
            gm_tl874t2atnlh3 t135 = gm_l3;
            void * t136 = gm_f_len_value_t6b3fd0((void *)NULL, cx, NULL, t133, t134, t135);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t137 = 0;
            F.ret = t136;
            goto gm_fret;
        }
        goto ix130;
ie130:;
ix130:;
        goto me31;
mn127:;
ma36:;
        uint8_t t139 = GM_LD(uint8_t, (((uint8_t *)&t30) + 0));
        if (t139 != ((uint8_t)26ULL)) goto mn138;
        void * t140 = gm_l0;
        void * t141 = GM_LD(void *, (((uint8_t *)t140) + 0));
        gm_tl874t2atnlh3 t142 = gm_l3;
        void * t143 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t141, t142);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t144 = 0;
        F.ret = t143;
        goto gm_fret;
        goto me31;
mn138:;
ma37:;
        goto me31;
mn145:;
ma38:;
        gm_trap("no match arm matched the scrutinee", gm_srch2l5nl06vsci, 1110, 5);
me31:;
    }
    {
        void * t148 = gm_l0;
        void * t149 = gm_l5;
        gm_b16_8 t150 = GM_LD(gm_b16_8, (((uint8_t *)a2) + 0));
        gm_tl874t2atnlh3 t151 = gm_l3;
        void * t152 = gm_f_struct_field_cc6n63f((void *)NULL, cx, NULL, t148, t149, t150, t151, ((uint32_t)0ULL));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (!(GM_LD(void *, ((uint8_t *)&t152)) != 0)) goto ie147;
        void * t153 = GM_LD(void *, ((uint8_t *)&t152));
        gm_l18 = t153;
        {
            void * t155 = gm_l18;
            uint8_t t156 = 0;
            F.ret = t155;
            goto gm_fret;
        }
        goto ix147;
ie147:;
ix147:;
    }
    {
        gm_tl874t2atnlh3 t157 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)a2) + 16));
        gm_t6j6smj7qoctu t158;
        void * t159 = gm_l0;
        gm_t88rcucg1b7uk t160 = gm_l7;
        gm_b16_8 t161 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t159, t160);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_b16_8, (uint8_t *)&t158 + 0, t161);
        gm_b16_8 t162 = GM_LD(gm_b16_8, (((uint8_t *)a2) + 0));
        GM_ST(gm_b16_8, (uint8_t *)&t158 + 16, t162);
        gm_tabb710d9mq2e t163;
        gm_f_format_htqqv8t((void *)&t163, (void *)NULL, cx, NULL, &t158);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t163))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t163) + 8, gm_srch2l5nl06vsci, 1126, 32);
        gm_b16_8 t164 = GM_LD(gm_b16_8, (((uint8_t *)&t163) + 8));
        gm_b16_8 t165 = GM_LD(gm_b16_8, ((uint8_t *)&t164));
        gm_f_error_s6hn871((void *)&F.l19, (void *)NULL, cx, NULL, t157, t165);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t166 = gm_l0;
        void * t167 = gm_l1;
        gm_tl874t2atnlh3 t168 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t167) + 72));
        gm_tl874t2atnlh3 t169 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)a2) + 16));
        gm_b0_1 t170 = gm_f_emit_with_continuation_n_lv5ofqt((void *)NULL, cx, NULL, t166, &(*(gm_t4gut2c9fj1kt *)((uint8_t *)&F.l19)), t168, t169);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t171 = gm_l0;
        void * t172 = GM_LD(void *, (((uint8_t *)t171) + 0));
        gm_tl874t2atnlh3 t173 = gm_l3;
        void * t174 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t172, t173);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t175 = 0;
        F.ret = t174;
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
inline __attribute__((aligned(4))) uint8_t gm_f_field_visible_here_20vnoda(void * a0, gm_t9qkh0rsooc8d a1, gm_csguankko4lmhd a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_field_visible_here_20vnoda F;
    void * gm_l0; /* cx */
    gm_t9qkh0rsooc8d gm_l1; /* vis */
    gm_t88rcucg1b7uk gm_l2; /* t */
    uint32_t gm_l3; /* id */
    GM_STACK_CHECK_SHALLOW(&F, 198u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = GM_CONV(gm_t88rcucg1b7uk, a2);
    {
        uint8_t t3 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l1) + 0) == ((uint8_t)2ULL)));
        if (!t3) goto ie2;
        {
            uint8_t t5 = 0;
            F.ret = ((uint8_t)1);
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t7 = gm_l0;
        void * t8 = GM_LD(void *, (((uint8_t *)t7) + 0));
        void * t9 = (void *)(((uint8_t *)t8) + 8);
        gm_t88rcucg1b7uk t10 = gm_l2;
        gm_t53at80pfdus3 t11;
        gm_f_kind_jcnt4hm((void *)&t11, t9, GM_CONV(gm_csguankko4lmhd, t10));
        uint8_t t12 = GM_LD(uint8_t, (((uint8_t *)&t11) + 0));
        if (t12 != ((uint8_t)17ULL)) goto ie6;
        uint32_t t13 = GM_LD(uint32_t, ((((uint8_t *)&t11) + 8) + 0));
        gm_l3 = t13;
        {
            void * t15 = gm_l0;
            void * t16 = GM_LD(void *, (((uint8_t *)t15) + 0));
            gm_t9qkh0rsooc8d t17 = gm_l1;
            uint32_t t18 = gm_l3;
            uint64_t t19 = (uint64_t)t18;
            uint64_t t20 = (uintptr_t)t19;
            void * t21 = gm_l0;
            void * t22 = GM_LD(void *, (((uint8_t *)t21) + 0));
            uint64_t t23 = GM_LD(uintptr_t, ((((uint8_t *)t22) + 1088) + 0) + 8);
            if (t20 >= (uintptr_t)(t23)) gm_trap_index(t20, t23, gm_srch2l5nl06vsci, 1172, 30);
            uint64_t t24 = GM_LD(uint64_t, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t22) + 1088) + 0)) + t20 * 184) + 16));
            void * t25 = gm_l0;
            uint64_t t26 = GM_LD(uint64_t, (((uint8_t *)t25) + 8));
            uint8_t t27 = gm_f_visible_ll07dr9(t16, t17, t24, t26);
            uint8_t t28 = 0;
            F.ret = t27;
            goto gm_fret;
        }
        goto ix6;
ie6:;
        {
            uint8_t t30 = 0;
            F.ret = ((uint8_t)1);
            goto gm_fret;
        }
ix6:;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void * gm_f_fn_ref_aqs2fl4(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_tl874t2atnlh3 a2) {
    struct gm_fr_fn_ref_aqs2fl4 F;
    void * gm_l0; /* cx */
    uint32_t gm_l1; /* f */
    gm_tl874t2atnlh3 gm_l2; /* span */
    gm_t88rcucg1b7uk gm_l3; /* ty */
    GM_STACK_CHECK(&F, 337u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 0));
        void * t4 = (void *)(((uint8_t *)t3) + 8);
        void * t5 = gm_l0;
        void * t6 = GM_LD(void *, (((uint8_t *)t5) + 0));
        uint32_t t7 = gm_l1;
        uint64_t t8 = (uint64_t)t7;
        uint64_t t9 = (uintptr_t)t8;
        void * t10 = gm_l0;
        void * t11 = GM_LD(void *, (((uint8_t *)t10) + 0));
        uint64_t t12 = GM_LD(uintptr_t, ((((uint8_t *)t11) + 344) + 0) + 8);
        if (t9 >= (uintptr_t)(t12)) gm_trap_index(t9, t12, gm_srch2l5nl06vsci, 406, 46);
        void * t13 = (void *)((uint8_t *)GM_LD(void *, ((((uint8_t *)t11) + 344) + 0)) + t9 * 136);
        gm_tddtjfggdat0d t14;
        gm_f_fn_ty_qaih7ts((void *)&t14, (void *)NULL, cx, NULL, t6, t13);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t53at80pfdus3 t15;
        GM_ST(uint8_t, (uint8_t *)&t15 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t15 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t15 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t15 + 0), ((uint8_t)16ULL));
        GM_ST(gm_tddtjfggdat0d, ((uint8_t *)&t15 + 8), t14);
        gm_t88rcucg1b7uk t16 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t4, &t15);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l3 = t16;
    }
    {
        gm_t88rcucg1b7uk t17 = gm_l3;
        uint32_t t18 = gm_l1;
        gm_teti411eujvf4 t19;
        memset(&t19, 0, sizeof t19);
        GM_ST(uint8_t, ((uint8_t *)&t19 + 0), ((uint8_t)26ULL));
        GM_ST(uint32_t, ((uint8_t *)&t19 + 8), t18);
        gm_tl874t2atnlh3 t20 = gm_l2;
        void * t21 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t17, &t19, t20);
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
inline __attribute__((aligned(4))) uint8_t gm_f_has_optional_operand_279ro7i(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2) {
    struct gm_fr_has_optional_operand_279ro7i F;
    void * gm_l0; /* cx */
    void * gm_l1; /* a */
    void * gm_l2; /* b */
    gm_tgglga8ro3isc gm_l3; /* <tmp> */
    gm_tqsk8dt6ib3qq gm_l4; /* <tmp> */
    gm_b16_8 gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    gm_t88rcucg1b7uk gm_l7; /* t */
    GM_STACK_CHECK(&F, 343u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        {
            void * t3 = gm_l1;
            gm_t88rcucg1b7uk t4 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t3) + 0));
            void * t5 = gm_l2;
            gm_t88rcucg1b7uk t6 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t5) + 0));
            gm_tgglga8ro3isc t7;
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t7 + 0), t4);
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t7 + 4), t6);
            gm_l3 = t7;
        }
        {
            {
                {
                    gm_t88rcucg1b7uk t10 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l3) + 0));
                    gm_t88rcucg1b7uk t11 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l3) + 4));
                    gm_tqsk8dt6ib3qq t12;
                    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t12 + 0), t10);
                    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t12 + 4), t11);
                    gm_l4 = t12;
                }
                void * t13 = ((uint8_t *)&gm_l4);
                if ((uintptr_t)0 > (uintptr_t)2 || (uintptr_t)2 > (uintptr_t)2) gm_trap_slice((uintptr_t)0, (uintptr_t)2, 2, gm_srch2l5nl06vsci, 863, 5);
                gm_b16_8 t14;
                GM_ST(void *, (uint8_t *)&t14, (void *)(((uint8_t *)t13 + (uintptr_t)0 * 4)));
                GM_ST(uintptr_t, (uint8_t *)&t14 + 8, (uintptr_t)((uintptr_t)2 - (uintptr_t)0));
                gm_b16_8 t15 = t14;
                gm_l5 = t15;
            }
            {
                gm_l6 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t17 = gm_l6;
                    uint64_t t18 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                    uint8_t t19 = (t17 < t18);
                    if (!t19) goto lb16;
                    {
                        uint64_t t22 = gm_l6;
                        uint64_t t23 = (uintptr_t)t22;
                        uint64_t t24 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                        gm_t88rcucg1b7uk t27 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l5)) + t23 * 4));
                        gm_l7 = t27;
                        {
                            void * t30 = gm_l0;
                            void * t31 = GM_LD(void *, (((uint8_t *)t30) + 0));
                            void * t32 = (void *)(((uint8_t *)t31) + 8);
                            void * t33 = gm_l0;
                            void * t34 = (void *)(((uint8_t *)t33) + 96);
                            void * t35 = gm_l0;
                            void * t36 = GM_LD(void *, (((uint8_t *)t35) + 0));
                            gm_t88rcucg1b7uk t37 = gm_l7;
                            gm_t88rcucg1b7uk t38 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t34, t36, t37);
                            if (gm_unw.kind) {
                                goto gm_ll20;
                            }
                            gm_t53at80pfdus3 t39;
                            gm_f_kind_jcnt4hm((void *)&t39, t32, GM_CONV(gm_csguankko4lmhd, t38));
                            uint8_t t40;
                            uint8_t t43 = GM_LD(uint8_t, (((uint8_t *)&t39) + 0));
                            if (t43 != ((uint8_t)13ULL)) goto mn42;
                            t40 = ((uint8_t)1);
                            goto me41;
mn42:;
                            t40 = ((uint8_t)0);
                            goto me41;
mn44:;
                            gm_trap("no match arm matched the scrutinee", gm_srch2l5nl06vsci, 864, 12);
me41:;
                            if (!t40) goto ie29;
                            {
                                uint8_t t46 = 0;
                                F.ret = ((uint8_t)1);
                                goto gm_fret;
                            }
                            goto ix29;
ie29:;
ix29:;
                        }
                        goto me25;
mn26:;
                        gm_trap("no match arm matched the scrutinee", gm_srch2l5nl06vsci, 863, 5);
me25:;
                    }
lc16:;
                    {
                        uint64_t t48 = gm_l6;
                        uint64_t r49; bool o50 = __builtin_add_overflow(t48, ((uint64_t)1ULL), &r49);
                        uint64_t t51 = r49;
                        gm_l6 = t51;
                    }
                }
gm_ll20:;
                if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb16; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc16; }
                }
                goto gm_lad;
lb16:;
            }
        }
    }
    {
        uint8_t t52 = 0;
        F.ret = ((uint8_t)0);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_hidden_field_message_b0pdek7(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_t88rcucg1b7uk a2, gm_t9qkh0rsooc8d a3) {
    struct gm_fr_hidden_field_message_b0pdek7 F;
    void * gm_l0; /* cx */
    gm_b16_8 gm_l1; /* name */
    gm_t88rcucg1b7uk gm_l2; /* t */
    gm_t9qkh0rsooc8d gm_l3; /* vis */
    GM_STACK_CHECK(&F, 328u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        uint8_t t3 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l3) + 0) == ((uint8_t)1ULL)));
        if (!t3) goto ie2;
        {
            gm_t6j6smj7qoctu t5;
            gm_b16_8 t6 = gm_l1;
            GM_ST(gm_b16_8, (uint8_t *)&t5 + 0, t6);
            void * t7 = gm_l0;
            gm_t88rcucg1b7uk t8 = gm_l2;
            gm_b16_8 t9 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t7, t8);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(gm_b16_8, (uint8_t *)&t5 + 16, t9);
            gm_tabb710d9mq2e t10;
            gm_f_format_00gguat((void *)&t10, (void *)NULL, cx, NULL, &t5);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t10))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t10) + 8, gm_srch2l5nl06vsci, 1188, 33);
            gm_b16_8 t11 = GM_LD(gm_b16_8, (((uint8_t *)&t10) + 8));
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
        gm_t6j6smj7qoctu t14;
        gm_b16_8 t15 = gm_l1;
        GM_ST(gm_b16_8, (uint8_t *)&t14 + 0, t15);
        void * t16 = gm_l0;
        gm_t88rcucg1b7uk t17 = gm_l2;
        gm_b16_8 t18 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t16, t17);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_b16_8, (uint8_t *)&t14 + 16, t18);
        gm_tabb710d9mq2e t19;
        gm_f_format_bjrmbam((void *)&t19, (void *)NULL, cx, NULL, &t14);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t19))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t19) + 8, gm_srch2l5nl06vsci, 1189, 12);
        gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&t19) + 8));
        gm_b16_8 t21 = GM_LD(gm_b16_8, ((uint8_t *)&t20));
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
inline __attribute__((aligned(4))) void gm_f_hidden_field_u7uao2s(void *gm_out, void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_hidden_field_u7uao2s F;
    void * gm_l0; /* cx */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_b16_8 gm_l2; /* fields */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    GM_STACK_CHECK_SHALLOW(&F, 467u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    {
        void * t2 = gm_l0;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 0));
        gm_t88rcucg1b7uk t4 = gm_l1;
        gm_th2lqbo77pvsc t5;
        gm_f_struct_fields_chqu3it((void *)&t5, t3, GM_CONV(gm_csguankko4lmhd, t4));
        gm_b16_8 t6;
        if (((((uint8_t *)&t5))[16] != 0)) {
            gm_b16_8 t7 = GM_LD(gm_b16_8, (((uint8_t *)&t5) + 0));
            t6 = t7;
        } else {
            gm_ttu09lij36f8c t8;
            memset(&t8, 0, sizeof t8);
            uint8_t t9 = 0;
            F.ret = t8;
            goto gm_fret;
        }
        gm_l2 = t6;
    }
    {
        {
            gm_b16_8 t11 = gm_l2;
            gm_l3 = t11;
        }
        {
            gm_l4 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t13 = gm_l4;
                uint64_t t14 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                uint8_t t15 = (t13 < t14);
                if (!t15) goto lb12;
                {
                    uint64_t t18 = gm_l4;
                    uint64_t t19 = (uintptr_t)t18;
                    uint64_t t20 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                    gm_t0mglksrkfa5e t23 = GM_LD(gm_t0mglksrkfa5e, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t19 * 40));
                    F.l5 = t23;
                    {
                        void * t26 = gm_l0;
                        gm_t9qkh0rsooc8d t27 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&F.l5) + 20));
                        gm_t88rcucg1b7uk t28 = gm_l1;
                        uint8_t t29 = gm_f_field_visible_here_20vnoda(t26, t27, GM_CONV(gm_csguankko4lmhd, t28));
                        uint8_t t30 = (uint8_t)!t29;
                        if (!t30) goto ie25;
                        {
                            gm_t6j6smj7qoctu t32;
                            gm_b16_8 t33 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 0));
                            GM_ST(gm_b16_8, (uint8_t *)&t32 + 0, t33);
                            gm_b16_8 t34;
                            uint8_t t36 = ((GM_LD(uint8_t, (((uint8_t *)&F.l5) + 20) + 0) == ((uint8_t)1ULL)));
                            if (!t36) goto ie35;
                            gm_b16_8 t38;
                            GM_ST(void *, (uint8_t *)&t38, (void *)(gm_snqvo5bgc2fch));
                            GM_ST(uintptr_t, (uint8_t *)&t38 + 8, (uintptr_t)(7));
                            gm_b16_8 t39 = t38;
                            t34 = t39;
                            goto ix35;
ie35:;
                            gm_b16_8 t41;
                            GM_ST(void *, (uint8_t *)&t41, (void *)(gm_sbgl5dgur8udg));
                            GM_ST(uintptr_t, (uint8_t *)&t41 + 8, (uintptr_t)(6));
                            gm_b16_8 t42 = t41;
                            t34 = t42;
ix35:;
                            GM_ST(gm_b16_8, (uint8_t *)&t32 + 16, t34);
                            GM_ST(uint8_t, (uint8_t *)&(*(gm_ttu09lij36f8c *)gm_out) + 33, 0);
                            GM_ST(uint16_t, (uint8_t *)&(*(gm_ttu09lij36f8c *)gm_out) + 34, 0);
                            GM_ST(uint32_t, (uint8_t *)&(*(gm_ttu09lij36f8c *)gm_out) + 36, 0);
                            ((uint8_t *)&(*(gm_ttu09lij36f8c *)gm_out))[32] = 1;
                            GM_ST(gm_t6j6smj7qoctu, (uint8_t *)&(*(gm_ttu09lij36f8c *)gm_out) + 0, t32);
                            goto gm_fdone;
                        }
                        goto ix25;
ie25:;
ix25:;
                    }
                    goto me21;
mn22:;
                    gm_trap("no match arm matched the scrutinee", gm_srch2l5nl06vsci, 1179, 5);
me21:;
                }
lc12:;
                {
                    uint64_t t44 = gm_l4;
                    uint64_t r45; bool o46 = __builtin_add_overflow(t44, ((uint64_t)1ULL), &r45);
                    uint64_t t47 = r45;
                    gm_l4 = t47;
                }
            }
lb12:;
        }
    }
    {
        gm_ttu09lij36f8c t48;
        memset(&t48, 0, sizeof t48);
        uint8_t t49 = 0;
        F.ret = t48;
        goto gm_fret;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_ttu09lij36f8c gm_f_hidden_field_u7uao2s_c(void * a0, gm_csguankko4lmhd a1) { gm_ttu09lij36f8c gm_r; gm_f_hidden_field_u7uao2s(&gm_r, a0, a1); return gm_r; }
__attribute__((aligned(4))) void * gm_f_if_expr_tiak47t(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint8_t a2, gm_tkn42u7na3t81 a3) {
    struct gm_fr_if_expr_tiak47t F;
    void * gm_l0; /* cx */
    void * gm_l1; /* i */
    uint8_t gm_l2; /* value_ */
    gm_tkn42u7na3t81 gm_l3; /* expect */
    uint8_t gm_l4; /* value */
    void * gm_l5; /* tys */
    void * gm_l9; /* else_ */
    gm_b16_8 gm_l10; /* <tmp> */
    uint64_t gm_l11; /* <tmp> */
    uint64_t gm_l12; /* k */
    gm_t08olq97rscn3 gm_l14; /* name */
    void * gm_l15; /* body */
    void * gm_l17; /* x */
    void * gm_l18; /* p */
    void * gm_l19; /* v */
    void * gm_l20; /* then */
    gm_t88rcucg1b7uk gm_l21; /* vt */
    gm_t88rcucg1b7uk gm_l22; /* err */
    uint8_t gm_l23; /* ok */
    void * gm_l24; /* q */
    uint32_t gm_l25; /* id */
    void * gm_l26; /* d */
    void * gm_l28; /* err_pat */
    gm_b16_8 gm_l29; /* c */
    void * gm_l30; /* b */
    gm_t88rcucg1b7uk gm_l32; /* ty */
    gm_t88rcucg1b7uk gm_l33; /* x */
    gm_b16_8 gm_l35; /* <tmp> */
    uint64_t gm_l36; /* <tmp> */
    void * gm_l37; /* t */
    gm_tgqj9p927bnhp gm_l38; /* c */
    void * gm_l39; /* e */
    uint64_t gm_l40; /* <tmp> */
    uint64_t gm_l41; /* <tmp> */
    uint64_t gm_l42; /* k */
    void * gm_l43; /* e */
    uint8_t gm_l44; /* all_never */
    void * gm_l45; /* e */
    gm_b16_8 gm_l46; /* <tmp> */
    uint64_t gm_l47; /* <tmp> */
    void * gm_l48; /* t */
    gm_tgqj9p927bnhp gm_l49; /* c */
    gm_tgqj9p927bnhp gm_l50; /* c */
    void * gm_l51; /* then */
    void * gm_l52; /* d */
    uint64_t gm_l54; /* k */
    void * gm_l55; /* then */
    void * gm_l56; /* r */
    GM_STACK_CHECK(&F, 4331u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        uint8_t t2 = 0;
        uint8_t t3 = gm_l2;
        if (t3) {
            void * t4 = gm_l1;
            uint8_t t5;
            if ((((((uint8_t *)t4) + 16))[48] != 0)) goto mn7;
            t5 = ((uint8_t)0);
            goto me6;
mn7:;
            t5 = ((uint8_t)1);
            goto me6;
mn8:;
            gm_trap("no match arm matched the scrutinee", gm_srch2l5nl06vsci, 1197, 25);
me6:;
            t2 = t5;
        } else t2 = 0;
        gm_l4 = t2;
    }
    {
        void * t9 = gm_l0;
        void * t10 = GM_LD(void *, (((uint8_t *)t9) + 0));
        void * t11 = (void *)(((uint8_t *)t10) + 8);
        gm_l5 = t11;
    }
    {
        gm_f_list_uahmact((void *)&F.l6, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_f_list_s1kpkpr((void *)&F.l7, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_tsprp1cqsr44h t12;
        memset(&t12, 0, sizeof t12);
        F.l8 = t12;
    }
    {
        gm_l9 = ((void *)0);
    }
    {
        {
            void * t14 = gm_l1;
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)t14) + 0));
            gm_b16_8 t16 = GM_LD(gm_b16_8, ((uint8_t *)&t15));
            gm_l10 = t16;
        }
        {
            gm_l11 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t18 = gm_l11;
                uint64_t t19 = GM_LD(uintptr_t, ((uint8_t *)&gm_l10) + 8);
                uint8_t t20 = (t18 < t19);
                if (!t20) goto lb17;
                {
                    gm_tf9j5enq7gf67 t23;
                    uint64_t t24 = gm_l11;
                    GM_ST(uint64_t, (uint8_t *)&t23 + 0, t24);
                    uint64_t t25 = gm_l11;
                    uint64_t t26 = (uintptr_t)t25;
                    uint64_t t27 = GM_LD(uintptr_t, ((uint8_t *)&gm_l10) + 8);
                    memcpy((uint8_t *)&t23 + 8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l10)) + t26 * 72), 72);
                    uint64_t t30 = GM_LD(uint64_t, (((uint8_t *)&t23) + 0));
                    gm_l12 = t30;
                    F.l13 = (*(gm_tcuumjkg5eni6 *)(((uint8_t *)&t23) + 8));
                    {
                        void * t32 = gm_l0;
                        gm_b0_1 t33 = gm_f_push_scope_mu7j3lg((void *)NULL, cx, NULL, t32);
                        if (gm_unw.kind) {
                            goto gm_ll21;
                        }
                    }
                    {
                        void * t35 = gm_l1;
                        if (!(((((uint8_t *)t35) + 16))[48] != 0)) goto ie34;
                        uint8_t t36 = GM_LD(uint8_t, ((((uint8_t *)t35) + 16) + 0));
                        if (t36 != ((uint8_t)1ULL)) goto ie34;
                        gm_t08olq97rscn3 t37 = GM_LD(gm_t08olq97rscn3, (((((uint8_t *)t35) + 16) + 8) + 0));
                        gm_l14 = t37;
                        void * t38 = GM_LD(void *, (((((uint8_t *)t35) + 16) + 8) + 32));
                        gm_l15 = t38;
                        uint64_t t39 = gm_l12;
                        uint64_t r40; bool o41 = __builtin_add_overflow(t39, ((uint64_t)1ULL), &r40);
                        if (o41) gm_trap("integer overflow in `+`", gm_srch2l5nl06vsci, 1206, 48);
                        uint64_t t42 = r40;
                        void * t43 = gm_l1;
                        uint64_t t44 = GM_LD(uintptr_t, (((uint8_t *)t43) + 0) + 8);
                        uint8_t t45 = ((t42 == t44));
                        if (!t45) goto ie34;
                        {
                            uint8_t t48 = 0;
                            uint64_t t49 = GM_LD(uintptr_t, ((((uint8_t *)&F.l13) + 0) + 0) + 8);
                            uint8_t t50 = (!(t49 == ((uint64_t)1ULL)));
                            if (!t50) {
                                uint64_t t51 = (uintptr_t)((uint64_t)0ULL);
                                uint64_t t52 = GM_LD(uintptr_t, ((((uint8_t *)&F.l13) + 0) + 0) + 8);
                                if (t51 >= (uintptr_t)(t52)) gm_trap_index(t51, t52, gm_srch2l5nl06vsci, 1207, 46);
                                uint8_t t53;
                                uint8_t t56 = GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, ((((uint8_t *)&F.l13) + 0) + 0)) + t51 * 80) + 0));
                                if (t56 != ((uint8_t)0ULL)) goto mn55;
                                t53 = ((uint8_t)0);
                                goto me54;
mn55:;
                                t53 = ((uint8_t)1);
                                goto me54;
mn57:;
                                gm_trap("no match arm matched the scrutinee", gm_srch2l5nl06vsci, 1207, 46);
me54:;
                                t48 = t53;
                            } else t48 = 1;
                            if (!t48) goto ie47;
                            {
                                void * t59 = gm_l0;
                                gm_tl874t2atnlh3 t60 = GM_LD(gm_tl874t2atnlh3, ((((uint8_t *)&F.l13) + 0) + 16));
                                gm_b16_8 t61;
                                GM_ST(void *, (uint8_t *)&t61, (void *)(gm_s759q7smc97t0));
                                GM_ST(uintptr_t, (uint8_t *)&t61 + 8, (uintptr_t)(74));
                                gm_b0_1 t62 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t59, t60, t61);
                                if (gm_unw.kind) {
                                    goto gm_ll21;
                                }
                            }
                            {
                                void * t63 = gm_l0;
                                gm_f_pop_scope_g5j7o4e(t63);
                            }
                            {
                                void * t64 = gm_l0;
                                void * t65 = GM_LD(void *, (((uint8_t *)t64) + 0));
                                void * t66 = gm_l1;
                                gm_tl874t2atnlh3 t67 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t66) + 72));
                                void * t68 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t65, t67);
                                if (gm_unw.kind) {
                                    goto gm_ll21;
                                }
                                uint8_t t69 = 0;
                                F.ret = t68;
                                goto gm_fret;
                            }
                            goto ix47;
ie47:;
ix47:;
                        }
                        {
                            uint64_t t71 = (uintptr_t)((uint64_t)0ULL);
                            uint64_t t72 = GM_LD(uintptr_t, ((((uint8_t *)&F.l13) + 0) + 0) + 8);
                            if (t71 >= (uintptr_t)(t72)) gm_trap_index(t71, t72, gm_srch2l5nl06vsci, 1212, 30);
                            uint8_t t73 = GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, ((((uint8_t *)&F.l13) + 0) + 0)) + t71 * 80) + 0));
                            if (t73 != ((uint8_t)0ULL)) goto ie70;
                            gm_t0d02142k4tu8 t74 = GM_LD(gm_t0d02142k4tu8, ((((uint8_t *)GM_LD(void *, ((((uint8_t *)&F.l13) + 0) + 0)) + t71 * 80) + 8) + 0));
                            F.l16 = t74;
                            void * t75 = GM_LD(void *, ((((uint8_t *)GM_LD(void *, ((((uint8_t *)&F.l13) + 0) + 0)) + t71 * 80) + 8) + 64));
                            gm_l17 = t75;
                            {
                                void * t78 = gm_l0;
                                void * t79 = (void *)((uint8_t *)&F.l16);
                                void * t80 = gm_l17;
                                gm_tlr9dp305htm7 t81;
                                gm_f_binding_cond_v86td9t((void *)&t81, (void *)NULL, cx, NULL, t78, t79, t80);
                                if (gm_unw.kind) {
                                    goto gm_ll21;
                                }
                                uint8_t t82 = GM_LD(uint8_t, (((uint8_t *)&t81) + 0));
                                if (t82 != ((uint8_t)1ULL)) goto ie77;
                                void * t83 = GM_LD(void *, ((((uint8_t *)&t81) + 8) + 0));
                                gm_l18 = t83;
                                void * t84 = GM_LD(void *, ((((uint8_t *)&t81) + 8) + 8));
                                gm_l19 = t84;
                                {
                                    void * t86 = gm_l0;
                                    void * t87 = (void *)(((uint8_t *)&F.l13) + 32);
                                    uint8_t t88 = gm_l4;
                                    gm_tkn42u7na3t81 t89 = gm_l3;
                                    void * t90 = gm_f_block_expected_bv4ukm2((void *)NULL, cx, NULL, t86, t87, t88, t89);
                                    if (gm_unw.kind) {
                                        goto gm_ll21;
                                    }
                                    gm_l20 = t90;
                                }
                                {
                                    void * t91 = gm_l0;
                                    gm_f_pop_scope_g5j7o4e(t91);
                                }
                                {
                                    void * t92 = gm_l0;
                                    void * t93 = (void *)(((uint8_t *)t92) + 96);
                                    void * t94 = gm_l0;
                                    void * t95 = GM_LD(void *, (((uint8_t *)t94) + 0));
                                    void * t96 = gm_l19;
                                    gm_t88rcucg1b7uk t97 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t96) + 0));
                                    gm_t88rcucg1b7uk t98 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t93, t95, t97);
                                    if (gm_unw.kind) {
                                        goto gm_ll21;
                                    }
                                    gm_l21 = t98;
                                }
                                {
                                    void * t100 = gm_l5;
                                    gm_t88rcucg1b7uk t101 = gm_l21;
                                    gm_t53at80pfdus3 t102;
                                    gm_f_kind_jcnt4hm((void *)&t102, t100, GM_CONV(gm_csguankko4lmhd, t101));
                                    uint8_t t103 = GM_LD(uint8_t, (((uint8_t *)&t102) + 0));
                                    if (t103 != ((uint8_t)14ULL)) goto ie99;
                                    gm_t88rcucg1b7uk t104 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t102) + 8) + 0));
                                    gm_l22 = t104;
                                    {
                                        gm_l23 = ((uint8_t)0);
                                    }
                                    {
                                        void * t107 = gm_l18;
                                        uint8_t t108 = GM_LD(uint8_t, ((((uint8_t *)t107) + 8) + 0));
                                        if (t108 != ((uint8_t)6ULL)) goto ie106;
                                        void * t109 = GM_LD(void *, ((((uint8_t *)t107) + 8) + 8));
                                        gm_l24 = t109;
                                        {
                                            void * t111 = gm_l0;
                                            void * t112 = GM_LD(void *, (((uint8_t *)t111) + 0));
                                            void * t113 = gm_l24;
                                            uint8_t t114 = gm_f_irrefutable_lpkmeii((void *)NULL, cx, NULL, t112, t113);
                                            if (gm_unw.kind) {
                                                goto gm_ll21;
                                            }
                                            gm_l23 = t114;
                                        }
                                        goto ix106;
ie106:;
ix106:;
                                    }
                                    {
                                        uint8_t t116 = gm_l23;
                                        uint8_t t117 = (uint8_t)!t116;
                                        if (!t117) goto ie115;
                                        {
                                            void * t119 = gm_l0;
                                            gm_tl874t2atnlh3 t120 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l16) + 48));
                                            gm_b16_8 t121;
                                            GM_ST(void *, (uint8_t *)&t121, (void *)(gm_s01kpv06m7g4n));
                                            GM_ST(uintptr_t, (uint8_t *)&t121 + 8, (uintptr_t)(65));
                                            gm_b0_1 t122 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t119, t120, t121);
                                            if (gm_unw.kind) {
                                                goto gm_ll21;
                                            }
                                        }
                                        goto ix115;
ie115:;
ix115:;
                                    }
                                    {
                                        void * t123 = gm_l0;
                                        gm_b0_1 t124 = gm_f_push_scope_mu7j3lg((void *)NULL, cx, NULL, t123);
                                        if (gm_unw.kind) {
                                            goto gm_ll21;
                                        }
                                    }
                                    {
                                        void * t125 = gm_l0;
                                        gm_b16_8 t126 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l14) + 0));
                                        gm_t88rcucg1b7uk t127 = gm_l22;
                                        gm_tl874t2atnlh3 t128 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l14) + 16));
                                        uint32_t t129 = gm_f_new_local_003h9es((void *)NULL, cx, NULL, t125, t126, t127, ((uint8_t)0), t128);
                                        if (gm_unw.kind) {
                                            goto gm_ll21;
                                        }
                                        gm_l25 = t129;
                                    }
                                    {
                                        void * t130 = gm_l0;
                                        gm_b16_8 t131 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l14) + 0));
                                        uint32_t t132 = gm_l25;
                                        gm_t930q5l38hdap t133;
                                        memset(&t133, 0, sizeof t133);
                                        GM_ST(uint8_t, ((uint8_t *)&t133 + 0), ((uint8_t)0ULL));
                                        GM_ST(uint32_t, ((uint8_t *)&t133 + 8), t132);
                                        gm_tl874t2atnlh3 t134 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l14) + 16));
                                        gm_b0_1 t135 = gm_f_declare_3l5mhoj((void *)NULL, cx, NULL, t130, t131, &t133, t134);
                                        if (gm_unw.kind) {
                                            goto gm_ll21;
                                        }
                                    }
                                    {
                                        void * t136 = gm_l0;
                                        void * t137 = gm_l15;
                                        gm_tkn42u7na3t81 t138;
                                        uint8_t t140 = gm_l4;
                                        if (!t140) goto ie139;
                                        gm_tkn42u7na3t81 t142 = gm_l3;
                                        gm_tkn42u7na3t81 t143 = t142;
                                        t138 = t143;
                                        goto ix139;
ie139:;
                                        gm_tkn42u7na3t81 t145;
                                        memset(&t145, 0, sizeof t145);
                                        gm_tkn42u7na3t81 t146 = t145;
                                        t138 = t146;
ix139:;
                                        void * t147 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t136, t137, t138);
                                        if (gm_unw.kind) {
                                            goto gm_ll21;
                                        }
                                        gm_l26 = t147;
                                    }
                                    {
                                        void * t148 = gm_l0;
                                        gm_f_pop_scope_g5j7o4e(t148);
                                    }
                                    {
                                        uint8_t t150 = gm_l4;
                                        uint8_t t151 = (uint8_t)!t150;
                                        if (!t151) goto ie149;
                                        void * t152 = gm_l5;
                                        void * t153 = gm_l26;
                                        gm_t88rcucg1b7uk t154 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t153) + 0));
                                        gm_t53at80pfdus3 t155;
                                        gm_f_kind_jcnt4hm((void *)&t155, t152, GM_CONV(gm_csguankko4lmhd, t154));
                                        uint8_t t156 = (!(GM_LD(uint8_t, ((uint8_t *)&t155) + 0) == ((uint8_t)5ULL)));
                                        if (!t156) goto ie149;
                                        {
                                            gm_f_list_ir0h3tc_2((void *)&F.l27, (void *)NULL, cx, NULL);
                                            if (gm_unw.kind) {
                                                goto gm_ll21;
                                            }
                                        }
                                        {
                                            void * t158 = (void *)((uint8_t *)&F.l27);
                                            void * t159 = gm_l26;
                                            gm_trcmmd12cefb1 t160;
                                            GM_ST(uint8_t, (uint8_t *)&t160 + 1, 0);
                                            GM_ST(uint16_t, (uint8_t *)&t160 + 2, 0);
                                            GM_ST(uint32_t, (uint8_t *)&t160 + 4, 0);
                                            GM_ST(uint64_t, (uint8_t *)&t160 + 16, 0);
                                            GM_ST(uint64_t, (uint8_t *)&t160 + 24, 0);
                                            GM_ST(uint8_t, ((uint8_t *)&t160 + 0), ((uint8_t)4ULL));
                                            GM_ST(void *, ((uint8_t *)&t160 + 8), t159);
                                            gm_tftb1odc739t5 t161 = gm_f_push_d6c76ud_2(t158, &t160);
                                            if (((((uint8_t *)&t161))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t161) + 1, gm_srch2l5nl06vsci, 1232, 17);
                                            gm_b0_1 t162 = GM_LD(gm_b0_1, (((uint8_t *)&t161) + 1));
                                        }
                                        {
                                            void * t163 = gm_l0;
                                            void * t164 = GM_LD(void *, (((uint8_t *)t163) + 0));
                                            gm_b16_8 t165 = GM_LD(gm_b16_8, (((uint8_t *)&F.l27) + 0));
                                            void * t166 = gm_l26;
                                            gm_tl874t2atnlh3 t167 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t166) + 72));
                                            void * t168 = gm_f_block_of_2gjl2i1((void *)NULL, cx, NULL, t164, t165, t167);
                                            if (gm_unw.kind) {
                                                goto gm_ll21;
                                            }
                                            gm_l26 = t168;
                                        }
                                        goto ix149;
ie149:;
ix149:;
                                    }
                                    {
                                        gm_t88rcucg1b7uk t169 = gm_l21;
                                        gm_t88rcucg1b7uk t170 = gm_l22;
                                        uint32_t t171 = gm_l25;
                                        gm_tb38v5s9j1r32 t172;
                                        memset(&t172, 0, sizeof t172);
                                        GM_ST(uint8_t, ((uint8_t *)&t172 + 0), ((uint8_t)1ULL));
                                        GM_ST(uint32_t, ((uint8_t *)&t172 + 8), t171);
                                        gm_tl874t2atnlh3 t173 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l14) + 16));
                                        void * t174 = gm_f_mk_pat_hr2l6c4((void *)NULL, cx, NULL, t170, &t172, t173);
                                        if (gm_unw.kind) {
                                            goto gm_ll21;
                                        }
                                        gm_tb38v5s9j1r32 t175;
                                        memset(&t175, 0, sizeof t175);
                                        GM_ST(uint8_t, ((uint8_t *)&t175 + 0), ((uint8_t)7ULL));
                                        GM_ST(void *, ((uint8_t *)&t175 + 8), t174);
                                        gm_tl874t2atnlh3 t176 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l14) + 16));
                                        void * t177 = gm_f_mk_pat_hr2l6c4((void *)NULL, cx, NULL, t169, &t175, t176);
                                        if (gm_unw.kind) {
                                            goto gm_ll21;
                                        }
                                        gm_l28 = t177;
                                    }
                                    {
                                        gm_tgqj9p927bnhp t178;
                                        void * t179 = gm_l19;
                                        GM_ST(void *, (uint8_t *)&t178 + 0, t179);
                                        void * t180 = gm_l18;
                                        GM_ST(void *, (uint8_t *)&t178 + 8, t180);
                                        void * t181 = gm_l20;
                                        GM_ST(void *, (uint8_t *)&t178 + 16, t181);
                                        void * t182 = gm_l28;
                                        GM_ST(void *, (uint8_t *)&t178 + 24, t182);
                                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tsprp1cqsr44h *)((uint8_t *)&F.l8)) + 33, 0);
                                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tsprp1cqsr44h *)((uint8_t *)&F.l8)) + 34, 0);
                                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tsprp1cqsr44h *)((uint8_t *)&F.l8)) + 36, 0);
                                        ((uint8_t *)&(*(gm_tsprp1cqsr44h *)((uint8_t *)&F.l8)))[32] = 1;
                                        GM_ST(gm_tgqj9p927bnhp, (uint8_t *)&(*(gm_tsprp1cqsr44h *)((uint8_t *)&F.l8)) + 0, t178);
                                    }
                                    {
                                        void * t183 = gm_l26;
                                        gm_l9 = t183;
                                    }
                                    {
                                        goto lc17;
                                    }
                                    goto ix99;
ie99:;
                                    {
                                        {
                                            void * t187 = gm_l5;
                                            gm_t88rcucg1b7uk t188 = gm_l21;
                                            uint8_t t189 = gm_f_is_error_8k27gd1(t187, GM_CONV(gm_csguankko4lmhd, t188));
                                            uint8_t t190 = (uint8_t)!t189;
                                            if (!t190) goto ie186;
                                            {
                                                void * t192 = gm_l0;
                                                void * t193 = gm_l17;
                                                gm_tl874t2atnlh3 t194 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t193) + 72));
                                                void * t195 = gm_l0;
                                                gm_t88rcucg1b7uk t196 = gm_l21;
                                                gm_b16_8 t197 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t195, t196);
                                                if (gm_unw.kind) {
                                                    goto gm_ll21;
                                                }
                                                gm_tnpifv20irnph t198;
                                                GM_ST(gm_b16_8, ((uint8_t *)&t198 + 0), t197);
                                                gm_tabb710d9mq2e t199;
                                                gm_f_format_32r56ja((void *)&t199, (void *)NULL, cx, NULL, t198);
                                                if (gm_unw.kind) {
                                                    goto gm_ll21;
                                                }
                                                if (((((uint8_t *)&t199))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t199) + 8, gm_srch2l5nl06vsci, 1218, 60);
                                                gm_b16_8 t200 = GM_LD(gm_b16_8, (((uint8_t *)&t199) + 8));
                                                gm_b16_8 t201 = GM_LD(gm_b16_8, ((uint8_t *)&t200));
                                                gm_b0_1 t202 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t192, t194, t201);
                                                if (gm_unw.kind) {
                                                    goto gm_ll21;
                                                }
                                            }
                                            goto ix186;
ie186:;
ix186:;
                                        }
                                        {
                                            void * t203 = gm_l0;
                                            void * t204 = GM_LD(void *, (((uint8_t *)t203) + 0));
                                            void * t205 = gm_l1;
                                            gm_tl874t2atnlh3 t206 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t205) + 72));
                                            void * t207 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t204, t206);
                                            if (gm_unw.kind) {
                                                goto gm_ll21;
                                            }
                                            uint8_t t208 = 0;
                                            F.ret = t207;
                                            goto gm_fret;
                                        }
                                    }
ix99:;
                                }
                                goto ix77;
ie77:;
                                {
                                    void * t210 = gm_l0;
                                    void * t211 = GM_LD(void *, (((uint8_t *)t210) + 0));
                                    void * t212 = gm_l1;
                                    gm_tl874t2atnlh3 t213 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t212) + 72));
                                    void * t214 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t211, t213);
                                    if (gm_unw.kind) {
                                        goto gm_ll21;
                                    }
                                    uint8_t t215 = 0;
                                    F.ret = t214;
                                    goto gm_fret;
                                }
ix77:;
                            }
                            goto ix70;
ie70:;
                            {
                                void * t217 = gm_l0;
                                void * t218 = GM_LD(void *, (((uint8_t *)t217) + 0));
                                void * t219 = gm_l1;
                                gm_tl874t2atnlh3 t220 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t219) + 72));
                                void * t221 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t218, t220);
                                if (gm_unw.kind) {
                                    goto gm_ll21;
                                }
                                uint8_t t222 = 0;
                                F.ret = t221;
                                goto gm_fret;
                            }
ix70:;
                        }
                        goto ix34;
ie34:;
ix34:;
                    }
                    {
                        void * t223 = gm_l0;
                        void * t224 = (void *)(((uint8_t *)&F.l13) + 0);
                        gm_b16_8 t225 = gm_f_cond_99ajlhk((void *)NULL, cx, NULL, t223, t224);
                        if (gm_unw.kind) {
                            goto gm_ll21;
                        }
                        gm_l29 = t225;
                    }
                    {
                        void * t226 = gm_l0;
                        void * t227 = (void *)(((uint8_t *)&F.l13) + 32);
                        uint8_t t228 = gm_l4;
                        gm_tkn42u7na3t81 t229 = gm_l3;
                        void * t230 = gm_f_block_expected_bv4ukm2((void *)NULL, cx, NULL, t226, t227, t228, t229);
                        if (gm_unw.kind) {
                            goto gm_ll21;
                        }
                        gm_l30 = t230;
                    }
                    {
                        void * t231 = gm_l0;
                        gm_f_pop_scope_g5j7o4e(t231);
                    }
                    {
                        void * t232 = (void *)((uint8_t *)&F.l6);
                        gm_b16_8 t233 = gm_l29;
                        gm_tftb1odc739t5 t234 = gm_f_push_1voul2a(t232, t233);
                        if (((((uint8_t *)&t234))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t234) + 1, gm_srch2l5nl06vsci, 1244, 9);
                        gm_b0_1 t235 = GM_LD(gm_b0_1, (((uint8_t *)&t234) + 1));
                    }
                    {
                        void * t236 = (void *)((uint8_t *)&F.l7);
                        void * t237 = gm_l30;
                        gm_tftb1odc739t5 t238 = gm_f_push_pf02nv6(t236, t237);
                        if (((((uint8_t *)&t238))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t238) + 1, gm_srch2l5nl06vsci, 1245, 9);
                        gm_b0_1 t239 = GM_LD(gm_b0_1, (((uint8_t *)&t238) + 1));
                    }
                    goto me28;
mn29:;
                    gm_trap("no match arm matched the scrutinee", gm_srch2l5nl06vsci, 1204, 5);
me28:;
                }
lc17:;
                {
                    uint64_t t241 = gm_l11;
                    uint64_t r242; bool o243 = __builtin_add_overflow(t241, ((uint64_t)1ULL), &r242);
                    uint64_t t244 = r242;
                    gm_l11 = t244;
                }
            }
gm_ll21:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb17; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc17; }
            }
            goto gm_lad;
lb17:;
        }
    }
    {
        void * t246 = gm_l1;
        if (!(((((uint8_t *)t246) + 16))[48] != 0)) goto ie245;
        uint8_t t247 = GM_LD(uint8_t, ((((uint8_t *)t246) + 16) + 0));
        if (t247 != ((uint8_t)0ULL)) goto ie245;
        gm_t79rbdv4rafe5 t248 = GM_LD(gm_t79rbdv4rafe5, ((((uint8_t *)t246) + 16) + 8));
        F.l31 = t248;
        {
            void * t250 = gm_l0;
            void * t251 = (void *)((uint8_t *)&F.l31);
            uint8_t t252 = gm_l4;
            gm_tkn42u7na3t81 t253 = gm_l3;
            void * t254 = gm_f_block_expected_bv4ukm2((void *)NULL, cx, NULL, t250, t251, t252, t253);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l9 = t254;
        }
        goto ix245;
ie245:;
ix245:;
    }
    {
        void * t255 = gm_l5;
        gm_t88rcucg1b7uk t256 = gm_f_unit_2age34u((void *)NULL, cx, NULL, t255);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l32 = t256;
    }
    {
        uint8_t t258 = gm_l4;
        if (!t258) goto ie257;
        {
            if (!((((uint8_t *)&gm_l3))[4] != 0)) goto ie260;
            gm_t88rcucg1b7uk t261 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l3));
            gm_l33 = t261;
            {
                gm_t88rcucg1b7uk t263 = gm_l33;
                gm_l32 = t263;
            }
            goto ix260;
ie260:;
            {
                gm_f_list_nof7vfj((void *)&F.l34, (void *)NULL, cx, NULL);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                {
                    gm_b16_8 t266 = GM_LD(gm_b16_8, (((uint8_t *)&F.l7) + 0));
                    gm_l35 = t266;
                }
                {
                    gm_l36 = ((uint64_t)0ULL);
                }
                {
                    for (;;) {
                        uint64_t t268 = gm_l36;
                        uint64_t t269 = GM_LD(uintptr_t, ((uint8_t *)&gm_l35) + 8);
                        uint8_t t270 = (t268 < t269);
                        if (!t270) goto lb267;
                        {
                            uint64_t t273 = gm_l36;
                            uint64_t t274 = (uintptr_t)t273;
                            uint64_t t275 = GM_LD(uintptr_t, ((uint8_t *)&gm_l35) + 8);
                            void * t276 = GM_LD(void *, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l35)) + t274 * 8));
                            void * t279 = GM_LD(void *, ((uint8_t *)&t276));
                            gm_l37 = t279;
                            {
                                void * t281 = (void *)((uint8_t *)&F.l34);
                                void * t282 = gm_l37;
                                gm_t88rcucg1b7uk t283 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t282) + 0));
                                gm_tftb1odc739t5 t284 = gm_f_push_k3l9nic(t281, GM_CONV(gm_csguankko4lmhd, t283));
                                if (((((uint8_t *)&t284))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t284) + 1, gm_srch2l5nl06vsci, 1254, 36);
                                gm_b0_1 t285 = GM_LD(gm_b0_1, (((uint8_t *)&t284) + 1));
                            }
                            goto me277;
mn278:;
                            gm_trap("no match arm matched the scrutinee", gm_srch2l5nl06vsci, 1254, 13);
me277:;
                        }
lc267:;
                        {
                            uint64_t t287 = gm_l36;
                            uint64_t r288; bool o289 = __builtin_add_overflow(t287, ((uint64_t)1ULL), &r288);
                            uint64_t t290 = r288;
                            gm_l36 = t290;
                        }
                    }
lb267:;
                }
            }
            {
                if (!((((uint8_t *)&F.l8))[32] != 0)) goto ie291;
                gm_tgqj9p927bnhp t292 = GM_LD(gm_tgqj9p927bnhp, ((uint8_t *)&F.l8));
                gm_l38 = t292;
                {
                    void * t294 = (void *)((uint8_t *)&F.l34);
                    void * t295 = GM_LD(void *, (((uint8_t *)&gm_l38) + 16));
                    gm_t88rcucg1b7uk t296 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t295) + 0));
                    gm_tftb1odc739t5 t297 = gm_f_push_k3l9nic(t294, GM_CONV(gm_csguankko4lmhd, t296));
                    if (((((uint8_t *)&t297))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t297) + 1, gm_srch2l5nl06vsci, 1255, 32);
                    gm_b0_1 t298 = GM_LD(gm_b0_1, (((uint8_t *)&t297) + 1));
                }
                goto ix291;
ie291:;
ix291:;
            }
            {
                void * t300 = gm_l9;
                if (!(GM_LD(void *, ((uint8_t *)&t300)) != 0)) goto ie299;
                void * t301 = GM_LD(void *, ((uint8_t *)&t300));
                gm_l39 = t301;
                {
                    void * t303 = (void *)((uint8_t *)&F.l34);
                    void * t304 = gm_l39;
                    gm_t88rcucg1b7uk t305 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t304) + 0));
                    gm_tftb1odc739t5 t306 = gm_f_push_k3l9nic(t303, GM_CONV(gm_csguankko4lmhd, t305));
                    if (((((uint8_t *)&t306))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t306) + 1, gm_srch2l5nl06vsci, 1256, 29);
                    gm_b0_1 t307 = GM_LD(gm_b0_1, (((uint8_t *)&t306) + 1));
                }
                goto ix299;
ie299:;
ix299:;
            }
            {
                void * t308 = gm_l0;
                gm_b16_8 t309 = GM_LD(gm_b16_8, (((uint8_t *)&F.l34) + 0));
                gm_b16_8 t310 = GM_LD(gm_b16_8, ((uint8_t *)&t309));
                gm_t88rcucg1b7uk t311 = gm_f_join_type_cck70vp((void *)NULL, cx, NULL, t308, t310);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l32 = t311;
            }
ix260:;
        }
        {
            {
                gm_l41 = ((uint64_t)0ULL);
            }
            {
                uint64_t t313 = GM_LD(uintptr_t, (((uint8_t *)&F.l7) + 0) + 8);
                gm_l40 = t313;
            }
            {
                for (;;) {
                    uint64_t t315 = gm_l41;
                    uint64_t t316 = gm_l40;
                    uint8_t t317 = (t315 < t316);
                    if (!t317) goto lb314;
                    {
                        uint64_t t320 = gm_l41;
                        uint64_t t323 = GM_LD(uint64_t, ((uint8_t *)&t320));
                        gm_l42 = t323;
                        {
                            uint64_t t325 = gm_l42;
                            uint64_t t326 = (uintptr_t)t325;
                            uint64_t t327 = GM_LD(uintptr_t, (((uint8_t *)&F.l7) + 0) + 8);
                            if (t326 >= (uintptr_t)(t327)) gm_trap_index(t326, t327, gm_srch2l5nl06vsci, 1259, 39);
                            uint8_t *t328 = ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l7) + 0)) + t326 * 8);
                            void * t329 = gm_l0;
                            uint64_t t330 = gm_l42;
                            uint64_t t331 = (uintptr_t)t330;
                            uint64_t t332 = GM_LD(uintptr_t, (((uint8_t *)&F.l7) + 0) + 8);
                            if (t331 >= (uintptr_t)(t332)) gm_trap_index(t331, t332, gm_srch2l5nl06vsci, 1259, 71);
                            void * t333 = GM_LD(void *, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l7) + 0)) + t331 * 8));
                            gm_t88rcucg1b7uk t334 = gm_l32;
                            void * t335 = gm_f_join_branch_anseitf((void *)NULL, cx, NULL, t329, t333, t334);
                            if (gm_unw.kind) {
                                goto gm_ll318;
                            }
                            GM_ST(void *, t328, t335);
                        }
                        goto me321;
mn322:;
                        gm_trap("no match arm matched the scrutinee", gm_srch2l5nl06vsci, 1259, 9);
me321:;
                    }
lc314:;
                    {
                        uint64_t t337 = gm_l41;
                        uint64_t r338; bool o339 = __builtin_add_overflow(t337, ((uint64_t)1ULL), &r338);
                        uint64_t t340 = r338;
                        gm_l41 = t340;
                    }
                }
gm_ll318:;
                if (gm_unw.frame == &F.h && gm_unw.label == 2) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb314; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc314; }
                }
                goto gm_lad;
lb314:;
            }
        }
        {
            void * t342 = gm_l9;
            if (!(GM_LD(void *, ((uint8_t *)&t342)) != 0)) goto ie341;
            void * t343 = GM_LD(void *, ((uint8_t *)&t342));
            gm_l43 = t343;
            {
                void * t345 = gm_l0;
                void * t346 = gm_l43;
                gm_t88rcucg1b7uk t347 = gm_l32;
                void * t348 = gm_f_join_branch_anseitf((void *)NULL, cx, NULL, t345, t346, t347);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l9 = t348;
            }
            goto ix341;
ie341:;
ix341:;
        }
        goto ix257;
ie257:;
        {
            gm_l44 = ((uint8_t)0);
        }
        {
            void * t351 = gm_l9;
            if (!(GM_LD(void *, ((uint8_t *)&t351)) != 0)) goto ie350;
            void * t352 = GM_LD(void *, ((uint8_t *)&t351));
            gm_l45 = t352;
            {
                void * t354 = gm_l5;
                void * t355 = gm_l45;
                gm_t88rcucg1b7uk t356 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t355) + 0));
                gm_t53at80pfdus3 t357;
                gm_f_kind_jcnt4hm((void *)&t357, t354, GM_CONV(gm_csguankko4lmhd, t356));
                uint8_t t358 = ((GM_LD(uint8_t, ((uint8_t *)&t357) + 0) == ((uint8_t)5ULL)));
                gm_l44 = t358;
            }
            goto ix350;
ie350:;
ix350:;
        }
        {
            {
                gm_b16_8 t360 = GM_LD(gm_b16_8, (((uint8_t *)&F.l7) + 0));
                gm_l46 = t360;
            }
            {
                gm_l47 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t362 = gm_l47;
                    uint64_t t363 = GM_LD(uintptr_t, ((uint8_t *)&gm_l46) + 8);
                    uint8_t t364 = (t362 < t363);
                    if (!t364) goto lb361;
                    {
                        uint64_t t367 = gm_l47;
                        uint64_t t368 = (uintptr_t)t367;
                        uint64_t t369 = GM_LD(uintptr_t, ((uint8_t *)&gm_l46) + 8);
                        void * t370 = GM_LD(void *, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l46)) + t368 * 8));
                        void * t373 = GM_LD(void *, ((uint8_t *)&t370));
                        gm_l48 = t373;
                        {
                            void * t376 = gm_l5;
                            void * t377 = gm_l48;
                            gm_t88rcucg1b7uk t378 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t377) + 0));
                            gm_t53at80pfdus3 t379;
                            gm_f_kind_jcnt4hm((void *)&t379, t376, GM_CONV(gm_csguankko4lmhd, t378));
                            uint8_t t380 = (!(GM_LD(uint8_t, ((uint8_t *)&t379) + 0) == ((uint8_t)5ULL)));
                            if (!t380) goto ie375;
                            {
                                gm_l44 = ((uint8_t)0);
                            }
                            goto ix375;
ie375:;
ix375:;
                        }
                        goto me371;
mn372:;
                        gm_trap("no match arm matched the scrutinee", gm_srch2l5nl06vsci, 1264, 9);
me371:;
                    }
lc361:;
                    {
                        uint64_t t383 = gm_l47;
                        uint64_t r384; bool o385 = __builtin_add_overflow(t383, ((uint64_t)1ULL), &r384);
                        uint64_t t386 = r384;
                        gm_l47 = t386;
                    }
                }
lb361:;
            }
        }
        {
            if (!((((uint8_t *)&F.l8))[32] != 0)) goto ie387;
            gm_tgqj9p927bnhp t388 = GM_LD(gm_tgqj9p927bnhp, ((uint8_t *)&F.l8));
            gm_l49 = t388;
            void * t389 = gm_l5;
            void * t390 = GM_LD(void *, (((uint8_t *)&gm_l49) + 16));
            gm_t88rcucg1b7uk t391 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t390) + 0));
            gm_t53at80pfdus3 t392;
            gm_f_kind_jcnt4hm((void *)&t392, t389, GM_CONV(gm_csguankko4lmhd, t391));
            uint8_t t393 = (!(GM_LD(uint8_t, ((uint8_t *)&t392) + 0) == ((uint8_t)5ULL)));
            if (!t393) goto ie387;
            {
                gm_l44 = ((uint8_t)0);
            }
            goto ix387;
ie387:;
ix387:;
        }
        {
            uint8_t t396 = gm_l44;
            if (!t396) goto ie395;
            {
                void * t398 = gm_l5;
                gm_t88rcucg1b7uk t399 = gm_f_never_ty_b949mgo((void *)NULL, cx, NULL, t398);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l32 = t399;
            }
            goto ix395;
ie395:;
ix395:;
        }
ix257:;
    }
    {
        if (!((((uint8_t *)&F.l8))[32] != 0)) goto ie400;
        gm_tgqj9p927bnhp t401 = GM_LD(gm_tgqj9p927bnhp, ((uint8_t *)&F.l8));
        gm_l50 = t401;
        {
            void * t403;
            uint8_t t405 = gm_l4;
            if (!t405) goto ie404;
            void * t407 = gm_l0;
            void * t408 = GM_LD(void *, (((uint8_t *)&gm_l50) + 16));
            gm_t88rcucg1b7uk t409 = gm_l32;
            void * t410 = gm_f_join_branch_anseitf((void *)NULL, cx, NULL, t407, t408, t409);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            void * t411 = t410;
            t403 = t411;
            goto ix404;
ie404:;
            void * t413 = GM_LD(void *, (((uint8_t *)&gm_l50) + 16));
            void * t414 = t413;
            t403 = t414;
ix404:;
            gm_l51 = t403;
        }
        {
            void * t415 = gm_l9;
            void * t416;
            if ((GM_LD(void *, ((uint8_t *)&t415)) != 0)) {
                void * t417 = GM_LD(void *, (((uint8_t *)&t415) + 0));
                t416 = t417;
            } else {
                void * t418 = gm_l0;
                void * t419 = GM_LD(void *, (((uint8_t *)t418) + 0));
                void * t420 = gm_l1;
                gm_tl874t2atnlh3 t421 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t420) + 72));
                void * t422 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t419, t421);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t423 = 0;
                F.ret = t422;
                goto gm_fret;
            }
            gm_l52 = t416;
        }
        {
            gm_f_list_58nr42v_3((void *)&F.l53, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t424 = (void *)((uint8_t *)&F.l53);
            gm_tbbqefuhv2r8h t425;
            void * t426 = GM_LD(void *, (((uint8_t *)&gm_l50) + 8));
            GM_ST(void *, (uint8_t *)&t425 + 0, t426);
            GM_ST(void *, (uint8_t *)&t425 + 8, ((void *)0));
            void * t427 = gm_l51;
            GM_ST(void *, (uint8_t *)&t425 + 16, t427);
            gm_tftb1odc739t5 t428 = gm_f_push_bpovr2k_3(t424, &t425);
            if (((((uint8_t *)&t428))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t428) + 1, gm_srch2l5nl06vsci, 1274, 9);
            gm_b0_1 t429 = GM_LD(gm_b0_1, (((uint8_t *)&t428) + 1));
        }
        {
            void * t430 = (void *)((uint8_t *)&F.l53);
            gm_tbbqefuhv2r8h t431;
            void * t432 = GM_LD(void *, (((uint8_t *)&gm_l50) + 24));
            GM_ST(void *, (uint8_t *)&t431 + 0, t432);
            GM_ST(void *, (uint8_t *)&t431 + 8, ((void *)0));
            void * t433 = gm_l52;
            GM_ST(void *, (uint8_t *)&t431 + 16, t433);
            gm_tftb1odc739t5 t434 = gm_f_push_bpovr2k_3(t430, &t431);
            if (((((uint8_t *)&t434))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t434) + 1, gm_srch2l5nl06vsci, 1275, 9);
            gm_b0_1 t435 = GM_LD(gm_b0_1, (((uint8_t *)&t434) + 1));
        }
        {
            gm_t88rcucg1b7uk t436 = gm_l32;
            gm_tkaapcii86udk t437;
            void * t438 = GM_LD(void *, (((uint8_t *)&gm_l50) + 0));
            GM_ST(void *, (uint8_t *)&t437 + 0, t438);
            gm_b16_8 t439 = GM_LD(gm_b16_8, (((uint8_t *)&F.l53) + 0));
            GM_ST(gm_b16_8, (uint8_t *)&t437 + 8, t439);
            gm_teti411eujvf4 t440;
            memset(&t440, 0, sizeof t440);
            GM_ST(uint8_t, ((uint8_t *)&t440 + 0), ((uint8_t)44ULL));
            GM_ST(gm_tkaapcii86udk, ((uint8_t *)&t440 + 8), t437);
            void * t441 = gm_l51;
            gm_tl874t2atnlh3 t442 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t441) + 72));
            void * t443 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t436, &t440, t442);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l9 = t443;
        }
        goto ix400;
ie400:;
ix400:;
    }
    {
        uint64_t t444 = GM_LD(uintptr_t, (((uint8_t *)&F.l7) + 0) + 8);
        gm_l54 = t444;
    }
    {
        for (;;) {
            uint64_t t446 = gm_l54;
            uint8_t t447 = (t446 > ((uint64_t)0ULL));
            if (!t447) goto lb445;
            {
                uint64_t t450 = gm_l54;
                uint64_t r451; bool o452 = __builtin_sub_overflow(t450, ((uint64_t)1ULL), &r451);
                if (o452) gm_trap("integer overflow in `-`", gm_srch2l5nl06vsci, 1281, 9);
                uint64_t t453 = r451;
                gm_l54 = t453;
            }
            {
                uint64_t t454 = gm_l54;
                uint64_t t455 = (uintptr_t)t454;
                uint64_t t456 = GM_LD(uintptr_t, (((uint8_t *)&F.l7) + 0) + 8);
                if (t455 >= (uintptr_t)(t456)) gm_trap_index(t455, t456, gm_srch2l5nl06vsci, 1282, 17);
                void * t457 = GM_LD(void *, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l7) + 0)) + t455 * 8));
                gm_l55 = t457;
            }
            {
                gm_t88rcucg1b7uk t458 = gm_l32;
                gm_trl2khmuphncd t459;
                uint64_t t460 = gm_l54;
                uint64_t t461 = (uintptr_t)t460;
                uint64_t t462 = GM_LD(uintptr_t, (((uint8_t *)&F.l6) + 0) + 8);
                if (t461 >= (uintptr_t)(t462)) gm_trap_index(t461, t462, gm_srch2l5nl06vsci, 1283, 49);
                gm_b16_8 t463 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l6) + 0)) + t461 * 16));
                GM_ST(gm_b16_8, (uint8_t *)&t459 + 0, t463);
                void * t464 = gm_l55;
                GM_ST(void *, (uint8_t *)&t459 + 16, t464);
                void * t465 = gm_l9;
                GM_ST(void *, (uint8_t *)&t459 + 24, t465);
                gm_teti411eujvf4 t466;
                GM_ST(uint8_t, (uint8_t *)&t466 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t466 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t466 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t466 + 40, 0);
                GM_ST(uint64_t, (uint8_t *)&t466 + 48, 0);
                GM_ST(uint64_t, (uint8_t *)&t466 + 56, 0);
                GM_ST(uint8_t, ((uint8_t *)&t466 + 0), ((uint8_t)47ULL));
                GM_ST(gm_trl2khmuphncd, ((uint8_t *)&t466 + 8), t459);
                void * t467 = gm_l55;
                gm_tl874t2atnlh3 t468 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t467) + 72));
                void * t469 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t458, &t466, t468);
                if (gm_unw.kind) {
                    goto gm_ll448;
                }
                gm_l9 = t469;
            }
lc445:;
        }
gm_ll448:;
        if (gm_unw.frame == &F.h && gm_unw.label == 4) {
            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb445; }
            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc445; }
        }
        goto gm_lad;
lb445:;
    }
    {
        void * t470 = gm_l9;
        void * t471;
        if ((GM_LD(void *, ((uint8_t *)&t470)) != 0)) {
            void * t472 = GM_LD(void *, (((uint8_t *)&t470) + 0));
            t471 = t472;
        } else {
            void * t473 = gm_l0;
            void * t474 = GM_LD(void *, (((uint8_t *)t473) + 0));
            void * t475 = gm_l1;
            gm_tl874t2atnlh3 t476 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t475) + 72));
            void * t477 = gm_f_unit_ohk62mc((void *)NULL, cx, NULL, t474, t476);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            t471 = t477;
        }
        gm_l56 = t471;
    }
    {
        void * t478 = gm_l56;
        void * t479 = gm_l1;
        gm_tl874t2atnlh3 t480 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t479) + 72));
        GM_ST(gm_tl874t2atnlh3, (((uint8_t *)t478) + 72), t480);
    }
    {
        void * t481 = gm_l56;
        uint8_t t482 = 0;
        F.ret = t481;
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
inline __attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_index_of_part_7kb7erm(gm_b16_8 a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_index_of_part_7kb7erm F;
    gm_b16_8 gm_l0; /* parts */
    gm_b16_8 gm_l1; /* name */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    uint64_t gm_l4; /* i */
    GM_STACK_CHECK_SHALLOW(&F, 348u);
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
                    gm_t3gcf8sa6b1tu t10;
                    uint64_t t11 = gm_l3;
                    GM_ST(uint64_t, (uint8_t *)&t10 + 0, t11);
                    uint64_t t12 = gm_l3;
                    uint64_t t13 = (uintptr_t)t12;
                    uint64_t t14 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                    memcpy((uint8_t *)&t10 + 8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t13 * 48), 48);
                    uint64_t t17 = GM_LD(uint64_t, (((uint8_t *)&t10) + 0));
                    gm_l4 = t17;
                    F.l5 = (*(gm_tm7a6rksvs6n5 *)(((uint8_t *)&t10) + 8));
                    {
                        gm_th2lqbo77pvsc t20 = GM_LD(gm_th2lqbo77pvsc, (((uint8_t *)&F.l5) + 0));
                        gm_b16_8 t21 = gm_l1;
                        gm_th2lqbo77pvsc t22;
                        GM_ST(uint8_t, (uint8_t *)&t22 + 17, 0);
                        GM_ST(uint16_t, (uint8_t *)&t22 + 18, 0);
                        GM_ST(uint32_t, (uint8_t *)&t22 + 20, 0);
                        GM_ST(gm_b16_8, ((uint8_t *)&t22), t21);
                        ((uint8_t *)&t22)[16] = 1;
                        uint8_t t23 = (gm_eq0ogca82g1f(((uint8_t *)&t20), ((uint8_t *)&t22)));
                        if (!t23) goto ie19;
                        {
                            uint64_t t25 = gm_l4;
                            gm_tf5srh4mp4b3i t26;
                            GM_ST(uint8_t, (uint8_t *)&t26 + 9, 0);
                            GM_ST(uint16_t, (uint8_t *)&t26 + 10, 0);
                            GM_ST(uint32_t, (uint8_t *)&t26 + 12, 0);
                            GM_ST(uint64_t, ((uint8_t *)&t26), t25);
                            ((uint8_t *)&t26)[8] = 1;
                            uint8_t t27 = 0;
                            F.ret = t26;
                            goto gm_fret;
                        }
                        goto ix19;
ie19:;
ix19:;
                    }
                    goto me15;
mn16:;
                    gm_trap("no match arm matched the scrutinee", gm_srch2l5nl06vsci, 582, 5);
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
    {
        gm_tf5srh4mp4b3i t33;
        memset(&t33, 0, sizeof t33);
        uint8_t t34 = 0;
        F.ret = t33;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_literal_tif53t0(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_literal_tif53t0 F;
    void * gm_l0; /* e */
    void * gm_l1; /* inner */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    uint8_t t2;
    uint8_t t8 = GM_LD(uint8_t, ((((uint8_t *)t1) + 8) + 0));
    switch (t8) { case ((uint8_t)0ULL): goto ma4; case ((uint8_t)1ULL): goto ma4; case ((uint8_t)2ULL): goto ma6; case ((uint8_t)3ULL): goto ma6; case ((uint8_t)4ULL): goto ma6; case ((uint8_t)5ULL): goto ma6; case ((uint8_t)6ULL): goto ma6; case ((uint8_t)7ULL): goto ma6; case ((uint8_t)8ULL): goto ma6; case ((uint8_t)9ULL): goto ma5; case ((uint8_t)10ULL): goto ma6; case ((uint8_t)11ULL): goto ma6; case ((uint8_t)12ULL): goto ma6; case ((uint8_t)13ULL): goto ma6; case ((uint8_t)14ULL): goto ma6; case ((uint8_t)15ULL): goto ma6; case ((uint8_t)16ULL): goto ma6; case ((uint8_t)17ULL): goto ma6; case ((uint8_t)18ULL): goto ma6; case ((uint8_t)19ULL): goto ma6; case ((uint8_t)20ULL): goto ma6; case ((uint8_t)21ULL): goto ma6; case ((uint8_t)22ULL): goto ma6; case ((uint8_t)23ULL): goto ma6; case ((uint8_t)24ULL): goto ma6; case ((uint8_t)25ULL): goto ma6; case ((uint8_t)26ULL): goto ma6; case ((uint8_t)27ULL): goto ma6; case ((uint8_t)28ULL): goto ma6; case ((uint8_t)29ULL): goto ma6; case ((uint8_t)30ULL): goto ma6; case ((uint8_t)31ULL): goto ma6; case ((uint8_t)32ULL): goto ma6; case ((uint8_t)33ULL): goto ma6; case ((uint8_t)34ULL): goto ma6; case ((uint8_t)35ULL): goto ma6; case ((uint8_t)36ULL): goto ma6; case ((uint8_t)37ULL): goto ma6; case ((uint8_t)38ULL): goto ma6; case ((uint8_t)39ULL): goto ma6; case ((uint8_t)40ULL): goto ma6; case ((uint8_t)41ULL): goto ma6; case ((uint8_t)42ULL): goto ma6; case ((uint8_t)43ULL): goto ma6; case ((uint8_t)44ULL): goto ma6; case ((uint8_t)45ULL): goto ma6; case ((uint8_t)46ULL): goto ma6; case ((uint8_t)47ULL): goto ma6; case ((uint8_t)48ULL): goto ma6; case ((uint8_t)49ULL): goto ma6; case ((uint8_t)50ULL): goto ma6; default: goto ma6; }
ma4:;
    uint8_t t12 = GM_LD(uint8_t, ((((uint8_t *)t1) + 8) + 0));
    if (t12 != ((uint8_t)0ULL)) goto pa11;
    goto po10;
pa11:;
    uint8_t t14 = GM_LD(uint8_t, ((((uint8_t *)t1) + 8) + 0));
    if (t14 != ((uint8_t)1ULL)) goto pa13;
    goto po10;
pa13:;
    goto mn9;
po10:;
    t2 = ((uint8_t)1);
    goto me3;
mn9:;
ma5:;
    uint8_t t16 = GM_LD(uint8_t, ((((uint8_t *)t1) + 8) + 0));
    if (t16 != ((uint8_t)9ULL)) goto mn15;
    void * t17 = GM_LD(void *, ((((uint8_t *)t1) + 8) + 8));
    gm_l1 = t17;
    void * t18 = gm_l1;
    uint8_t t19 = gm_f_is_literal_tif53t0(t18);
    t2 = t19;
    goto me3;
mn15:;
ma6:;
    t2 = ((uint8_t)0);
    goto me3;
mn20:;
ma7:;
    gm_trap("no match arm matched the scrutinee", gm_srch2l5nl06vsci, 666, 49);
me3:;
    F.ret = t2;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) void * gm_f_join_branch_anseitf(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_join_type_cck70vp(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1) {
    struct gm_fr_join_type_cck70vp F;
    void * gm_l0; /* cx */
    gm_b16_8 gm_l1; /* branches */
    void * gm_l2; /* tys */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    gm_t88rcucg1b7uk gm_l6; /* t */
    gm_b16_8 gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    gm_t88rcucg1b7uk gm_l9; /* t */
    gm_t88rcucg1b7uk gm_l10; /* rt */
    uint8_t gm_l11; /* joins */
    gm_b16_8 gm_l12; /* <tmp> */
    uint64_t gm_l13; /* <tmp> */
    gm_t88rcucg1b7uk gm_l14; /* u0 */
    gm_t88rcucg1b7uk gm_l15; /* u */
    gm_t88rcucg1b7uk gm_l16; /* elem */
    GM_STACK_CHECK(&F, 750u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 0));
        void * t4 = (void *)(((uint8_t *)t3) + 8);
        gm_l2 = t4;
    }
    {
        gm_f_list_nof7vfj((void *)&F.l3, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t6 = gm_l1;
            gm_l4 = t6;
        }
        {
            gm_l5 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t8 = gm_l5;
                uint64_t t9 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                uint8_t t10 = (t8 < t9);
                if (!t10) goto lb7;
                {
                    uint64_t t13 = gm_l5;
                    uint64_t t14 = (uintptr_t)t13;
                    uint64_t t15 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                    gm_t88rcucg1b7uk t18 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t14 * 4));
                    gm_l6 = t18;
                    {
                        void * t21 = gm_l2;
                        gm_t88rcucg1b7uk t22 = gm_l6;
                        gm_t53at80pfdus3 t23;
                        gm_f_kind_jcnt4hm((void *)&t23, t21, GM_CONV(gm_csguankko4lmhd, t22));
                        uint8_t t24 = (!(GM_LD(uint8_t, ((uint8_t *)&t23) + 0) == ((uint8_t)5ULL)));
                        if (!t24) goto ie20;
                        {
                            void * t26 = (void *)((uint8_t *)&F.l3);
                            gm_t88rcucg1b7uk t27 = gm_l6;
                            gm_tftb1odc739t5 t28 = gm_f_push_k3l9nic(t26, GM_CONV(gm_csguankko4lmhd, t27));
                            if (((((uint8_t *)&t28))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t28) + 1, gm_srch2l5nl06vsci, 1307, 40);
                            gm_b0_1 t29 = GM_LD(gm_b0_1, (((uint8_t *)&t28) + 1));
                        }
                        goto ix20;
ie20:;
ix20:;
                    }
                    goto me16;
mn17:;
                    gm_trap("no match arm matched the scrutinee", gm_srch2l5nl06vsci, 1306, 5);
me16:;
                }
lc7:;
                {
                    uint64_t t31 = gm_l5;
                    uint64_t r32; bool o33 = __builtin_add_overflow(t31, ((uint64_t)1ULL), &r32);
                    uint64_t t34 = r32;
                    gm_l5 = t34;
                }
            }
lb7:;
        }
    }
    {
        uint64_t t36 = GM_LD(uintptr_t, (((uint8_t *)&F.l3) + 0) + 8);
        uint8_t t37 = ((t36 == ((uint64_t)0ULL)));
        if (!t37) goto ie35;
        {
            void * t39 = gm_l2;
            gm_t88rcucg1b7uk t40 = gm_f_never_ty_b949mgo((void *)NULL, cx, NULL, t39);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t41 = 0;
            F.ret = t40;
            goto gm_fret;
        }
        goto ix35;
ie35:;
ix35:;
    }
    {
        {
            gm_b16_8 t43 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
            gm_l7 = t43;
        }
        {
            gm_l8 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t45 = gm_l8;
                uint64_t t46 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                uint8_t t47 = (t45 < t46);
                if (!t47) goto lb44;
                {
                    uint64_t t50 = gm_l8;
                    uint64_t t51 = (uintptr_t)t50;
                    uint64_t t52 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                    gm_t88rcucg1b7uk t55 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l7)) + t51 * 4));
                    gm_l9 = t55;
                    {
                        void * t57 = gm_l0;
                        void * t58 = (void *)(((uint8_t *)t57) + 96);
                        void * t59 = gm_l0;
                        void * t60 = GM_LD(void *, (((uint8_t *)t59) + 0));
                        gm_t88rcucg1b7uk t61 = gm_l9;
                        gm_t88rcucg1b7uk t62 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t58, t60, t61);
                        if (gm_unw.kind) {
                            goto gm_ll48;
                        }
                        gm_l10 = t62;
                    }
                    {
                        void * t64 = gm_l2;
                        gm_t88rcucg1b7uk t65 = gm_l10;
                        uint8_t t66 = gm_f_has_infer_3o82a0b(t64, GM_CONV(gm_csguankko4lmhd, t65));
                        if (!t66) goto ie63;
                        {
                            goto lc44;
                        }
                        goto ix63;
ie63:;
ix63:;
                    }
                    {
                        gm_l11 = ((uint8_t)1);
                    }
                    {
                        {
                            gm_b16_8 t69 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
                            gm_l12 = t69;
                        }
                        {
                            gm_l13 = ((uint64_t)0ULL);
                        }
                        {
                            for (;;) {
                                uint64_t t71 = gm_l13;
                                uint64_t t72 = GM_LD(uintptr_t, ((uint8_t *)&gm_l12) + 8);
                                uint8_t t73 = (t71 < t72);
                                if (!t73) goto lb70;
                                {
                                    uint64_t t76 = gm_l13;
                                    uint64_t t77 = (uintptr_t)t76;
                                    uint64_t t78 = GM_LD(uintptr_t, ((uint8_t *)&gm_l12) + 8);
                                    gm_t88rcucg1b7uk t81 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l12)) + t77 * 4));
                                    gm_l14 = t81;
                                    {
                                        void * t83 = gm_l0;
                                        void * t84 = (void *)(((uint8_t *)t83) + 96);
                                        void * t85 = gm_l0;
                                        void * t86 = GM_LD(void *, (((uint8_t *)t85) + 0));
                                        gm_t88rcucg1b7uk t87 = gm_l14;
                                        gm_t88rcucg1b7uk t88 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t84, t86, t87);
                                        if (gm_unw.kind) {
                                            goto gm_ll74;
                                        }
                                        gm_l15 = t88;
                                    }
                                    {
                                        gm_t88rcucg1b7uk t90 = gm_l15;
                                        gm_t88rcucg1b7uk t91 = gm_l10;
                                        uint8_t t92 = (!gm_eq9m6mvtmgde(((uint8_t *)&t90), ((uint8_t *)&t91)));
                                        if (!t92) goto ie89;
                                        uint8_t t93 = 0;
                                        void * t94 = gm_l2;
                                        gm_t88rcucg1b7uk t95 = gm_l15;
                                        uint8_t t96 = gm_f_has_infer_3o82a0b(t94, GM_CONV(gm_csguankko4lmhd, t95));
                                        if (!t96) {
                                            void * t97 = gm_l0;
                                            gm_t88rcucg1b7uk t98 = gm_l15;
                                            gm_t88rcucg1b7uk t99 = gm_l10;
                                            gm_th2lqbo77pvsc t100;
                                            gm_f_coercion_steps_ibqvhta((void *)&t100, (void *)NULL, cx, NULL, t97, t98, t99);
                                            if (gm_unw.kind) {
                                                goto gm_ll74;
                                            }
                                            uint8_t t101;
                                            if (((((uint8_t *)&t100))[16] != 0)) goto mn103;
                                            t101 = ((uint8_t)1);
                                            goto me102;
mn103:;
                                            t101 = ((uint8_t)0);
                                            goto me102;
mn104:;
                                            gm_trap("no match arm matched the scrutinee", gm_srch2l5nl06vsci, 1316, 53);
me102:;
                                            t93 = t101;
                                        } else t93 = 1;
                                        if (!t93) goto ie89;
                                        {
                                            gm_l11 = ((uint8_t)0);
                                        }
                                        goto ix89;
ie89:;
ix89:;
                                    }
                                    goto me79;
mn80:;
                                    gm_trap("no match arm matched the scrutinee", gm_srch2l5nl06vsci, 1314, 9);
me79:;
                                }
lc70:;
                                {
                                    uint64_t t107 = gm_l13;
                                    uint64_t r108; bool o109 = __builtin_add_overflow(t107, ((uint64_t)1ULL), &r108);
                                    uint64_t t110 = r108;
                                    gm_l13 = t110;
                                }
                            }
gm_ll74:;
                            if (gm_unw.frame == &F.h && gm_unw.label == 2) {
                                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb70; }
                                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc70; }
                            }
                            goto gm_ll48;
lb70:;
                        }
                    }
                    {
                        uint8_t t112 = gm_l11;
                        if (!t112) goto ie111;
                        {
                            gm_t88rcucg1b7uk t114 = gm_l9;
                            uint8_t t115 = 0;
                            F.ret = t114;
                            goto gm_fret;
                        }
                        goto ix111;
ie111:;
ix111:;
                    }
                    goto me53;
mn54:;
                    gm_trap("no match arm matched the scrutinee", gm_srch2l5nl06vsci, 1310, 5);
me53:;
                }
lc44:;
                {
                    uint64_t t117 = gm_l8;
                    uint64_t r118; bool o119 = __builtin_add_overflow(t117, ((uint64_t)1ULL), &r118);
                    uint64_t t120 = r118;
                    gm_l8 = t120;
                }
            }
gm_ll48:;
            if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb44; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc44; }
            }
            goto gm_lad;
lb44:;
        }
    }
    {
        void * t122 = gm_l0;
        gm_b16_8 t123 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
        gm_b16_8 t124 = GM_LD(gm_b16_8, ((uint8_t *)&t123));
        gm_tkn42u7na3t81 t125 = gm_f_array_elem_join_clnoo81((void *)NULL, cx, NULL, t122, t124);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (!((((uint8_t *)&t125))[4] != 0)) goto ie121;
        gm_t88rcucg1b7uk t126 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&t125));
        gm_l16 = t126;
        {
            void * t128 = gm_l2;
            gm_t88rcucg1b7uk t129 = gm_l16;
            gm_t88rcucg1b7uk t130 = gm_f_slice_5dt9121((void *)NULL, cx, NULL, t128, ((uint8_t)1), t129);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t131 = 0;
            F.ret = t130;
            goto gm_fret;
        }
        goto ix121;
ie121:;
ix121:;
    }
    {
        uint64_t t132 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t133 = GM_LD(uintptr_t, (((uint8_t *)&F.l3) + 0) + 8);
        if (t132 >= (uintptr_t)(t133)) gm_trap_index(t132, t133, gm_srch2l5nl06vsci, 1322, 12);
        gm_t88rcucg1b7uk t134 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l3) + 0)) + t132 * 4));
        uint8_t t135 = 0;
        F.ret = t134;
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
__attribute__((aligned(4))) void * gm_f_len_value_t6b3fd0(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_tl874t2atnlh3 a2) {
    struct gm_fr_len_value_t6b3fd0 F;
    void * gm_l0; /* cx */
    gm_t88rcucg1b7uk gm_l1; /* len */
    gm_tl874t2atnlh3 gm_l2; /* span */
    void * gm_l3; /* tys */
    uint32_t gm_l4; /* p */
    void * gm_l5; /* v */
    gm_t428ue2u3hf5g gm_l6; /* k */
    gm_t9qkh0rsooc8d gm_l8; /* op */
    gm_t88rcucg1b7uk gm_l9; /* lhs */
    gm_t88rcucg1b7uk gm_l10; /* rhs */
    gm_ts6oqppjh0ba3 gm_l11; /* bop */
    void * gm_l12; /* l */
    void * gm_l13; /* r */
    GM_STACK_CHECK(&F, 1074u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 0));
        void * t4 = (void *)(((uint8_t *)t3) + 8);
        gm_l3 = t4;
    }
    {
        void * t5 = gm_l3;
        gm_t88rcucg1b7uk t6 = gm_l1;
        gm_t53at80pfdus3 t7;
        gm_f_kind_jcnt4hm((void *)&t7, t5, GM_CONV(gm_csguankko4lmhd, t6));
        uint8_t t14 = GM_LD(uint8_t, (((uint8_t *)&t7) + 0));
        switch (t14) { case ((uint8_t)0ULL): goto ma12; case ((uint8_t)1ULL): goto ma12; case ((uint8_t)2ULL): goto ma12; case ((uint8_t)3ULL): goto ma12; case ((uint8_t)4ULL): goto ma12; case ((uint8_t)5ULL): goto ma12; case ((uint8_t)6ULL): goto ma12; case ((uint8_t)7ULL): goto ma12; case ((uint8_t)8ULL): goto ma12; case ((uint8_t)9ULL): goto ma12; case ((uint8_t)10ULL): goto ma12; case ((uint8_t)11ULL): goto ma12; case ((uint8_t)12ULL): goto ma12; case ((uint8_t)13ULL): goto ma12; case ((uint8_t)14ULL): goto ma12; case ((uint8_t)15ULL): goto ma12; case ((uint8_t)16ULL): goto ma12; case ((uint8_t)17ULL): goto ma12; case ((uint8_t)18ULL): goto ma12; case ((uint8_t)19ULL): goto ma9; case ((uint8_t)20ULL): goto ma12; case ((uint8_t)21ULL): goto ma10; case ((uint8_t)22ULL): goto ma11; case ((uint8_t)23ULL): goto ma12; case ((uint8_t)24ULL): goto ma12; case ((uint8_t)25ULL): goto ma12; case ((uint8_t)26ULL): goto ma12; default: goto ma12; }
ma9:;
        uint8_t t16 = GM_LD(uint8_t, (((uint8_t *)&t7) + 0));
        if (t16 != ((uint8_t)19ULL)) goto mn15;
        uint32_t t17 = GM_LD(uint32_t, (((uint8_t *)&t7) + 8));
        gm_l4 = t17;
        {
            void * t19 = gm_l0;
            uint32_t t20 = gm_l4;
            gm_t88rcucg1b7uk t21 = gm_f_value_param_ty_qrv22u4((void *)NULL, cx, NULL, t19, t20);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint32_t t22 = gm_l4;
            gm_teti411eujvf4 t23;
            memset(&t23, 0, sizeof t23);
            GM_ST(uint8_t, ((uint8_t *)&t23 + 0), ((uint8_t)9ULL));
            GM_ST(uint32_t, ((uint8_t *)&t23 + 8), t22);
            gm_tl874t2atnlh3 t24 = gm_l2;
            void * t25 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t21, &t23, t24);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l5 = t25;
        }
        {
            void * t27 = gm_l5;
            gm_t88rcucg1b7uk t28 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t27) + 0));
            void * t29 = gm_l3;
            gm_t88rcucg1b7uk t30 = gm_f_usize_ty_qnrd30f((void *)NULL, cx, NULL, t29);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t31 = (gm_eq9m6mvtmgde(((uint8_t *)&t28), ((uint8_t *)&t30)));
            if (!t31) goto ie26;
            {
                void * t33 = gm_l5;
                uint8_t t34 = 0;
                F.ret = t33;
                goto gm_fret;
            }
            goto ix26;
ie26:;
ix26:;
        }
        {
            void * t35 = gm_l3;
            gm_t88rcucg1b7uk t36 = gm_f_usize_ty_qnrd30f((void *)NULL, cx, NULL, t35);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tmn7n0n9raou5 t37;
            GM_ST(uint8_t, (uint8_t *)&t37 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t37 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t37 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t37 + 0), ((uint8_t)0ULL));
            void * t38 = gm_l5;
            gm_t8jsc4n9vp2ib t39;
            GM_ST(gm_tmn7n0n9raou5, ((uint8_t *)&t39 + 0), t37);
            GM_ST(void *, ((uint8_t *)&t39 + 8), t38);
            gm_teti411eujvf4 t40;
            memset(&t40, 0, sizeof t40);
            GM_ST(uint8_t, ((uint8_t *)&t40 + 0), ((uint8_t)32ULL));
            GM_ST(gm_t8jsc4n9vp2ib, ((uint8_t *)&t40 + 8), t39);
            gm_tl874t2atnlh3 t41 = gm_l2;
            void * t42 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t36, &t40, t41);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t43 = 0;
            F.ret = t42;
            goto gm_fret;
        }
        goto me8;
mn15:;
ma10:;
        uint8_t t45 = GM_LD(uint8_t, (((uint8_t *)&t7) + 0));
        if (t45 != ((uint8_t)21ULL)) goto mn44;
        gm_l6 = (*(gm_t428ue2u3hf5g *)(((uint8_t *)&t7) + 8));
        {
            void * t47 = gm_l0;
            void * t48 = GM_LD(void *, (((uint8_t *)t47) + 0));
            gm_thrdpodrs8pgt t49;
            gm_f_key_int_pqscuvn((void *)&t49, (void *)NULL, cx, NULL, t48, &(*(gm_t428ue2u3hf5g *)((uint8_t *)&gm_l6)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tbci5ospk5nnn t50;
            if (((((uint8_t *)&t49))[48] != 0)) {
                t50 = (*(gm_tbci5ospk5nnn *)(((uint8_t *)&t49) + 0));
            } else {
                void * t51 = gm_l0;
                void * t52 = GM_LD(void *, (((uint8_t *)t51) + 0));
                gm_tl874t2atnlh3 t53 = gm_l2;
                void * t54 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t52, t53);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t55 = 0;
                F.ret = t54;
                goto gm_fret;
            }
            F.l7 = t50;
        }
        {
            void * t56 = gm_l3;
            gm_t88rcucg1b7uk t57 = gm_f_usize_ty_qnrd30f((void *)NULL, cx, NULL, t56);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tbci5ospk5nnn t58 = F.l7;
            gm_teti411eujvf4 t59;
            GM_ST(uint8_t, (uint8_t *)&t59 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t59 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t59 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t59 + 56, 0);
            GM_ST(uint8_t, ((uint8_t *)&t59 + 0), ((uint8_t)0ULL));
            GM_ST(gm_tbci5ospk5nnn, ((uint8_t *)&t59 + 8), t58);
            gm_tl874t2atnlh3 t60 = gm_l2;
            void * t61 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t57, &t59, t60);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t62 = 0;
            F.ret = t61;
            goto gm_fret;
        }
        goto me8;
mn44:;
ma11:;
        uint8_t t64 = GM_LD(uint8_t, (((uint8_t *)&t7) + 0));
        if (t64 != ((uint8_t)22ULL)) goto mn63;
        gm_t9qkh0rsooc8d t65 = GM_LD(gm_t9qkh0rsooc8d, ((((uint8_t *)&t7) + 8) + 0));
        gm_l8 = t65;
        gm_t88rcucg1b7uk t66 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t7) + 8) + 4));
        gm_l9 = t66;
        gm_t88rcucg1b7uk t67 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t7) + 8) + 8));
        gm_l10 = t67;
        {
            gm_ts6oqppjh0ba3 t69;
            uint8_t t82 = GM_LD(uint8_t, (((uint8_t *)&gm_l8) + 0));
            switch (t82) { case ((uint8_t)0ULL): goto ma71; case ((uint8_t)1ULL): goto ma72; case ((uint8_t)2ULL): goto ma73; case ((uint8_t)3ULL): goto ma75; case ((uint8_t)4ULL): goto ma76; case ((uint8_t)5ULL): goto ma74; case ((uint8_t)6ULL): goto ma77; case ((uint8_t)7ULL): goto ma78; case ((uint8_t)8ULL): goto ma79; case ((uint8_t)9ULL): goto ma80; default: goto ma81; }
ma71:;
            uint8_t t84 = GM_LD(uint8_t, (((uint8_t *)&gm_l8) + 0));
            if (t84 != ((uint8_t)0ULL)) goto mn83;
            gm_t9qkh0rsooc8d t85;
            GM_ST(uint8_t, ((uint8_t *)&t85 + 0), ((uint8_t)0ULL));
            gm_ts6oqppjh0ba3 t86;
            GM_ST(uint8_t, ((uint8_t *)&t86 + 0), ((uint8_t)0ULL));
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t86 + 1), t85);
            t69 = t86;
            goto me70;
mn83:;
ma72:;
            uint8_t t88 = GM_LD(uint8_t, (((uint8_t *)&gm_l8) + 0));
            if (t88 != ((uint8_t)1ULL)) goto mn87;
            gm_t9qkh0rsooc8d t89;
            GM_ST(uint8_t, ((uint8_t *)&t89 + 0), ((uint8_t)0ULL));
            gm_ts6oqppjh0ba3 t90;
            GM_ST(uint8_t, ((uint8_t *)&t90 + 0), ((uint8_t)1ULL));
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t90 + 1), t89);
            t69 = t90;
            goto me70;
mn87:;
ma73:;
            uint8_t t92 = GM_LD(uint8_t, (((uint8_t *)&gm_l8) + 0));
            if (t92 != ((uint8_t)2ULL)) goto mn91;
            gm_t9qkh0rsooc8d t93;
            GM_ST(uint8_t, ((uint8_t *)&t93 + 0), ((uint8_t)0ULL));
            gm_ts6oqppjh0ba3 t94;
            GM_ST(uint8_t, ((uint8_t *)&t94 + 0), ((uint8_t)2ULL));
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t94 + 1), t93);
            t69 = t94;
            goto me70;
mn91:;
ma74:;
            uint8_t t96 = GM_LD(uint8_t, (((uint8_t *)&gm_l8) + 0));
            if (t96 != ((uint8_t)5ULL)) goto mn95;
            gm_t9qkh0rsooc8d t97;
            GM_ST(uint8_t, ((uint8_t *)&t97 + 0), ((uint8_t)0ULL));
            gm_ts6oqppjh0ba3 t98;
            GM_ST(uint8_t, ((uint8_t *)&t98 + 0), ((uint8_t)3ULL));
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t98 + 1), t97);
            t69 = t98;
            goto me70;
mn95:;
ma75:;
            uint8_t t100 = GM_LD(uint8_t, (((uint8_t *)&gm_l8) + 0));
            if (t100 != ((uint8_t)3ULL)) goto mn99;
            gm_ts6oqppjh0ba3 t101;
            GM_ST(uint8_t, (uint8_t *)&t101 + 1, 0);
            GM_ST(uint8_t, ((uint8_t *)&t101 + 0), ((uint8_t)4ULL));
            t69 = t101;
            goto me70;
mn99:;
ma76:;
            uint8_t t103 = GM_LD(uint8_t, (((uint8_t *)&gm_l8) + 0));
            if (t103 != ((uint8_t)4ULL)) goto mn102;
            gm_ts6oqppjh0ba3 t104;
            GM_ST(uint8_t, (uint8_t *)&t104 + 1, 0);
            GM_ST(uint8_t, ((uint8_t *)&t104 + 0), ((uint8_t)5ULL));
            t69 = t104;
            goto me70;
mn102:;
ma77:;
            uint8_t t106 = GM_LD(uint8_t, (((uint8_t *)&gm_l8) + 0));
            if (t106 != ((uint8_t)6ULL)) goto mn105;
            gm_ts6oqppjh0ba3 t107;
            GM_ST(uint8_t, (uint8_t *)&t107 + 1, 0);
            GM_ST(uint8_t, ((uint8_t *)&t107 + 0), ((uint8_t)6ULL));
            t69 = t107;
            goto me70;
mn105:;
ma78:;
            uint8_t t109 = GM_LD(uint8_t, (((uint8_t *)&gm_l8) + 0));
            if (t109 != ((uint8_t)7ULL)) goto mn108;
            gm_ts6oqppjh0ba3 t110;
            GM_ST(uint8_t, (uint8_t *)&t110 + 1, 0);
            GM_ST(uint8_t, ((uint8_t *)&t110 + 0), ((uint8_t)7ULL));
            t69 = t110;
            goto me70;
mn108:;
ma79:;
            uint8_t t112 = GM_LD(uint8_t, (((uint8_t *)&gm_l8) + 0));
            if (t112 != ((uint8_t)8ULL)) goto mn111;
            gm_ts6oqppjh0ba3 t113;
            GM_ST(uint8_t, (uint8_t *)&t113 + 1, 0);
            GM_ST(uint8_t, ((uint8_t *)&t113 + 0), ((uint8_t)8ULL));
            t69 = t113;
            goto me70;
mn111:;
ma80:;
            uint8_t t115 = GM_LD(uint8_t, (((uint8_t *)&gm_l8) + 0));
            if (t115 != ((uint8_t)9ULL)) goto mn114;
            gm_ts6oqppjh0ba3 t116;
            GM_ST(uint8_t, (uint8_t *)&t116 + 1, 0);
            GM_ST(uint8_t, ((uint8_t *)&t116 + 0), ((uint8_t)9ULL));
            t69 = t116;
            goto me70;
mn114:;
ma81:;
            gm_trap("no match arm matched the scrutinee", gm_srch2l5nl06vsci, 351, 30);
me70:;
            gm_l11 = t69;
        }
        {
            void * t117 = gm_l0;
            gm_t88rcucg1b7uk t118 = gm_l9;
            gm_tl874t2atnlh3 t119 = gm_l2;
            void * t120 = gm_f_len_value_t6b3fd0((void *)NULL, cx, NULL, t117, t118, t119);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l12 = t120;
        }
        {
            void * t121 = gm_l0;
            gm_t88rcucg1b7uk t122 = gm_l10;
            gm_tl874t2atnlh3 t123 = gm_l2;
            void * t124 = gm_f_len_value_t6b3fd0((void *)NULL, cx, NULL, t121, t122, t123);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l13 = t124;
        }
        {
            void * t125 = gm_l3;
            gm_t88rcucg1b7uk t126 = gm_f_usize_ty_qnrd30f((void *)NULL, cx, NULL, t125);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_trl9d7m1k5lbd t127;
            memset((uint8_t *)&t127 + 2, 0, 6);
            gm_ts6oqppjh0ba3 t128 = gm_l11;
            GM_ST(gm_ts6oqppjh0ba3, (uint8_t *)&t127 + 0, t128);
            void * t129 = gm_l12;
            GM_ST(void *, (uint8_t *)&t127 + 8, t129);
            void * t130 = gm_l13;
            GM_ST(void *, (uint8_t *)&t127 + 16, t130);
            gm_teti411eujvf4 t131;
            memset(&t131, 0, sizeof t131);
            GM_ST(uint8_t, ((uint8_t *)&t131 + 0), ((uint8_t)30ULL));
            GM_ST(gm_trl9d7m1k5lbd, ((uint8_t *)&t131 + 8), t127);
            gm_tl874t2atnlh3 t132 = gm_l2;
            void * t133 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t126, &t131, t132);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t134 = 0;
            F.ret = t133;
            goto gm_fret;
        }
        goto me8;
mn63:;
ma12:;
        void * t136 = gm_l0;
        void * t137 = GM_LD(void *, (((uint8_t *)t136) + 0));
        gm_tl874t2atnlh3 t138 = gm_l2;
        void * t139 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t137, t138);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t140 = 0;
        F.ret = t139;
        goto gm_fret;
        goto me8;
mn135:;
ma13:;
        gm_trap("no match arm matched the scrutinee", gm_srch2l5nl06vsci, 340, 5);
me8:;
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
inline __attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_module_of_n0d5f6n(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_module_of_n0d5f6n F;
    void * gm_l0; /* cx */
    void * gm_l1; /* e */
    void * gm_l2; /* p */
    void * gm_l3; /* pb */
    void * gm_l4; /* root */
    void * gm_l5; /* base */
    gm_t08olq97rscn3 gm_l6; /* rid */
    GM_STACK_CHECK(&F, 618u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t3 = gm_l0;
        void * t4 = GM_LD(void *, (((uint8_t *)t3) + 1296));
        if (!(GM_LD(void *, ((uint8_t *)&t4)) != 0)) goto ie2;
        void * t5 = GM_LD(void *, ((uint8_t *)&t4));
        gm_l2 = t5;
        {
            void * t8 = gm_l2;
            void * t9 = gm_l1;
            uint8_t t10 = ((t8 == t9));
            if (!t10) goto ie7;
            {
                gm_tf5srh4mp4b3i t12;
                memset(&t12, 0, sizeof t12);
                uint8_t t13 = 0;
                F.ret = t12;
                goto gm_fret;
            }
            goto ix7;
ie7:;
ix7:;
        }
        {
            void * t15 = gm_l2;
            uint8_t t16 = GM_LD(uint8_t, ((((uint8_t *)t15) + 8) + 0));
            if (t16 != ((uint8_t)22ULL)) goto ie14;
            void * t17 = GM_LD(void *, (((((uint8_t *)t15) + 8) + 8) + 0));
            gm_l3 = t17;
            void * t18 = gm_l3;
            void * t19 = gm_l1;
            uint8_t t20 = ((t18 == t19));
            if (!t20) goto ie14;
            {
                void * t22 = gm_l0;
                void * t23 = gm_l1;
                GM_ST(void *, (((uint8_t *)t22) + 1296), t23);
            }
            {
                gm_tf5srh4mp4b3i t24;
                memset(&t24, 0, sizeof t24);
                uint8_t t25 = 0;
                F.ret = t24;
                goto gm_fret;
            }
            goto ix14;
ie14:;
ix14:;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t26 = gm_l1;
        gm_l4 = t26;
    }
    {
        for (;;) {
            void * t28 = gm_l4;
            uint8_t t29 = GM_LD(uint8_t, ((((uint8_t *)t28) + 8) + 0));
            if (t29 != ((uint8_t)22ULL)) goto lb27;
            void * t30 = GM_LD(void *, (((((uint8_t *)t28) + 8) + 8) + 0));
            gm_l5 = t30;
            {
                void * t33 = gm_l5;
                gm_l4 = t33;
            }
lc27:;
        }
lb27:;
    }
    {
        void * t35 = gm_l4;
        uint8_t t36 = GM_LD(uint8_t, ((((uint8_t *)t35) + 8) + 0));
        if (t36 != ((uint8_t)6ULL)) goto ie34;
        gm_t08olq97rscn3 t37 = GM_LD(gm_t08olq97rscn3, ((((uint8_t *)t35) + 8) + 8));
        gm_l6 = t37;
        void * t38 = gm_l0;
        gm_b16_8 t39 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l6) + 0));
        gm_tqvu78rpi6q1h t40;
        gm_f_lookup_local_6f38t7p((void *)&t40, t38, t39);
        uint8_t t41;
        if (((((uint8_t *)&t40))[208] != 0)) goto mn43;
        t41 = ((uint8_t)1);
        goto me42;
mn43:;
        t41 = ((uint8_t)0);
        goto me42;
mn44:;
        gm_trap("no match arm matched the scrutinee", gm_srch2l5nl06vsci, 1027, 37);
me42:;
        if (!t41) goto ie34;
        {
            void * t46 = gm_l0;
            void * t47 = gm_l1;
            gm_tf5srh4mp4b3i t48 = gm_f_module_path_lpneg5l((void *)NULL, cx, NULL, t46, t47);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t49 = 0;
            F.ret = t48;
            goto gm_fret;
        }
        goto ix34;
ie34:;
ix34:;
    }
    {
        void * t50 = gm_l0;
        void * t51 = gm_l1;
        GM_ST(void *, (((uint8_t *)t50) + 1296), t51);
    }
    {
        gm_tf5srh4mp4b3i t52;
        memset(&t52, 0, sizeof t52);
        uint8_t t53 = 0;
        F.ret = t52;
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
inline __attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_module_path_lpneg5l(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_module_path_lpneg5l F;
    void * gm_l0; /* cx */
    void * gm_l1; /* e */
    void * gm_l2; /* base */
    gm_t08olq97rscn3 gm_l3; /* name */
    uint64_t gm_l4; /* m */
    gm_t08olq97rscn3 gm_l5; /* id */
    uint64_t gm_l6; /* d */
    uint64_t gm_l7; /* m */
    GM_STACK_CHECK(&F, 769u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t3 = gm_l1;
        uint8_t t4 = GM_LD(uint8_t, ((((uint8_t *)t3) + 8) + 0));
        if (t4 != ((uint8_t)22ULL)) goto ie2;
        void * t5 = GM_LD(void *, (((((uint8_t *)t3) + 8) + 8) + 0));
        gm_l2 = t5;
        gm_t08olq97rscn3 t6 = GM_LD(gm_t08olq97rscn3, (((((uint8_t *)t3) + 8) + 8) + 8));
        gm_l3 = t6;
        {
            void * t8 = gm_l0;
            void * t9 = gm_l2;
            gm_tf5srh4mp4b3i t10 = gm_f_module_path_lpneg5l((void *)NULL, cx, NULL, t8, t9);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint64_t t11;
            if (((((uint8_t *)&t10))[8] != 0)) {
                uint64_t t12 = GM_LD(uint64_t, (((uint8_t *)&t10) + 0));
                t11 = t12;
            } else {
                gm_tf5srh4mp4b3i t13;
                memset(&t13, 0, sizeof t13);
                uint8_t t14 = 0;
                F.ret = t13;
                goto gm_fret;
            }
            gm_l4 = t11;
        }
        {
            void * t15 = gm_l0;
            void * t16 = GM_LD(void *, (((uint8_t *)t15) + 0));
            uint64_t t17 = gm_l4;
            gm_b16_8 t18 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            void * t19 = gm_l0;
            uint64_t t20 = GM_LD(uint64_t, (((uint8_t *)t19) + 8));
            gm_tf5srh4mp4b3i t21 = gm_f_public_module_member_e2i0at7((void *)NULL, cx, NULL, t16, t17, t18, t20);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t22 = 0;
            F.ret = t21;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t24 = gm_l1;
        uint8_t t25 = GM_LD(uint8_t, ((((uint8_t *)t24) + 8) + 0));
        if (t25 != ((uint8_t)6ULL)) goto ie23;
        gm_t08olq97rscn3 t26 = GM_LD(gm_t08olq97rscn3, ((((uint8_t *)t24) + 8) + 8));
        gm_l5 = t26;
        {
            void * t28 = gm_l0;
            void * t29 = GM_LD(void *, (((uint8_t *)t28) + 0));
            void * t30 = gm_l0;
            uint64_t t31 = GM_LD(uint64_t, (((uint8_t *)t30) + 8));
            gm_b16_8 t32 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l5) + 0));
            gm_tf5srh4mp4b3i t33 = gm_f_lookup_module_name_cukfp7t((void *)NULL, cx, NULL, t29, t31, t32);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint64_t t34;
            if (((((uint8_t *)&t33))[8] != 0)) {
                uint64_t t35 = GM_LD(uint64_t, (((uint8_t *)&t33) + 0));
                t34 = t35;
            } else {
                gm_tf5srh4mp4b3i t36;
                memset(&t36, 0, sizeof t36);
                uint8_t t37 = 0;
                F.ret = t36;
                goto gm_fret;
            }
            gm_l6 = t34;
        }
        {
            void * t39 = gm_l0;
            void * t40 = GM_LD(void *, (((uint8_t *)t39) + 0));
            uint64_t t41 = gm_l6;
            gm_t6qooom8qa2do t42;
            gm_f_resolve_decl_pus042v((void *)&t42, (void *)NULL, cx, NULL, t40, t41);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t43 = GM_LD(uint8_t, (((uint8_t *)&t42) + 0));
            if (t43 != ((uint8_t)9ULL)) goto ie38;
            uint64_t t44 = GM_LD(uint64_t, (((uint8_t *)&t42) + 8));
            gm_l7 = t44;
            {
                uint64_t t46 = gm_l7;
                gm_tf5srh4mp4b3i t47;
                GM_ST(uint8_t, (uint8_t *)&t47 + 9, 0);
                GM_ST(uint16_t, (uint8_t *)&t47 + 10, 0);
                GM_ST(uint32_t, (uint8_t *)&t47 + 12, 0);
                GM_ST(uint64_t, ((uint8_t *)&t47), t46);
                ((uint8_t *)&t47)[8] = 1;
                uint8_t t48 = 0;
                F.ret = t47;
                goto gm_fret;
            }
            goto ix38;
ie38:;
            {
                gm_tf5srh4mp4b3i t50;
                memset(&t50, 0, sizeof t50);
                uint8_t t51 = 0;
                F.ret = t50;
                goto gm_fret;
            }
ix38:;
        }
        goto ix23;
ie23:;
        {
            gm_tf5srh4mp4b3i t53;
            memset(&t53, 0, sizeof t53);
            uint8_t t54 = 0;
            F.ret = t53;
            goto gm_fret;
        }
ix23:;
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
__attribute__((aligned(4))) void * gm_f_name_blqdq2r(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_tkn42u7na3t81 a2, gm_tl874t2atnlh3 a3) {
    struct gm_fr_name_blqdq2r F;
    void * gm_l0; /* cx */
    gm_b16_8 gm_l1; /* name */
    gm_tkn42u7na3t81 gm_l2; /* expect */
    gm_tl874t2atnlh3 gm_l3; /* span */
    uint32_t gm_l5; /* id */
    gm_t88rcucg1b7uk gm_l7; /* ty */
    uint32_t gm_l8; /* f */
    gm_t88rcucg1b7uk gm_l11; /* t */
    uint32_t gm_l12; /* pid */
    uint32_t gm_l17; /* f */
    uint64_t gm_l18; /* d */
    uint8_t gm_l19; /* effect_param */
    gm_t88rcucg1b7uk gm_l20; /* t */
    uint32_t gm_l21; /* p */
    GM_STACK_CHECK(&F, 3598u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        void * t3 = gm_l0;
        gm_b16_8 t4 = gm_l1;
        gm_tsf6kp4ohnuqs t5;
        gm_f_lookup_local_at_ntm4scs((void *)&t5, t3, t4);
        if (!((((uint8_t *)&t5))[216] != 0)) goto ie2;
        F.l4 = (*(gm_tfj8gdjm726n4 *)(((uint8_t *)&t5) + 0));
        {
            uint8_t t15 = GM_LD(uint8_t, ((((uint8_t *)&F.l4) + 0) + 0));
            switch (t15) { case ((uint8_t)0ULL): goto ma8; case ((uint8_t)1ULL): goto ma10; case ((uint8_t)2ULL): goto ma12; case ((uint8_t)3ULL): goto ma9; case ((uint8_t)4ULL): goto ma11; case ((uint8_t)5ULL): goto ma13; default: goto ma14; }
ma8:;
            uint8_t t17 = GM_LD(uint8_t, ((((uint8_t *)&F.l4) + 0) + 0));
            if (t17 != ((uint8_t)0ULL)) goto mn16;
            uint32_t t18 = GM_LD(uint32_t, ((((uint8_t *)&F.l4) + 0) + 8));
            gm_l5 = t18;
            {
                void * t20 = gm_l0;
                uint32_t t21 = GM_LD(uint32_t, (((uint8_t *)&F.l4) + 208));
                uint32_t t22 = gm_l5;
                gm_f_local_at_p388mm4((void *)&F.l6, t20, t21, t22);
            }
            {
                gm_t88rcucg1b7uk t23 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l6) + 16));
                gm_l7 = t23;
            }
            {
                void * t24 = gm_l0;
                void * t25 = GM_LD(void *, (((uint8_t *)t24) + 0));
                gm_tl874t2atnlh3 t26 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l6) + 24));
                gm_tl874t2atnlh3 t27 = gm_l3;
                gm_b0_1 t28 = gm_f_note_local_use_h4lf1jh((void *)NULL, cx, NULL, t25, t26, t27);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                uint32_t t30 = GM_LD(uint32_t, (((uint8_t *)&F.l4) + 208));
                void * t31 = gm_l0;
                uint32_t t32 = gm_f_depth_fr7pn6n(t31);
                uint8_t t33 = (t30 < t32);
                if (!t33) goto ie29;
                {
                    void * t35 = gm_l0;
                    uint32_t t36 = GM_LD(uint32_t, (((uint8_t *)&F.l4) + 208));
                    gm_f_note_capture_0p0af7r(t35, t36);
                }
                {
                    gm_t88rcucg1b7uk t37 = gm_l7;
                    void * t38 = gm_l0;
                    uint32_t t39 = gm_f_depth_fr7pn6n(t38);
                    uint32_t t40 = GM_LD(uint32_t, (((uint8_t *)&F.l4) + 208));
                    uint32_t r41; bool o42 = __builtin_sub_overflow(t39, t40, &r41);
                    if (o42) gm_trap("integer overflow in `-`", gm_srch2l5nl06vsci, 274, 46);
                    uint32_t t43 = r41;
                    uint32_t t44 = gm_l5;
                    gm_tcc1rq4n1avb2 t45;
                    GM_ST(uint32_t, ((uint8_t *)&t45 + 0), t43);
                    GM_ST(uint32_t, ((uint8_t *)&t45 + 4), t44);
                    gm_teti411eujvf4 t46;
                    memset(&t46, 0, sizeof t46);
                    GM_ST(uint8_t, ((uint8_t *)&t46 + 0), ((uint8_t)16ULL));
                    GM_ST(gm_tcc1rq4n1avb2, ((uint8_t *)&t46 + 8), t45);
                    gm_tl874t2atnlh3 t47 = gm_l3;
                    void * t48 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t37, &t46, t47);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t49 = 0;
                    F.ret = t48;
                    goto gm_fret;
                }
                goto ix29;
ie29:;
ix29:;
            }
            {
                gm_t88rcucg1b7uk t50 = gm_l7;
                uint32_t t51 = gm_l5;
                gm_teti411eujvf4 t52;
                memset(&t52, 0, sizeof t52);
                GM_ST(uint8_t, ((uint8_t *)&t52 + 0), ((uint8_t)15ULL));
                GM_ST(uint32_t, ((uint8_t *)&t52 + 8), t51);
                gm_tl874t2atnlh3 t53 = gm_l3;
                void * t54 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t50, &t52, t53);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t55 = 0;
                F.ret = t54;
                goto gm_fret;
            }
            goto me7;
mn16:;
ma9:;
            uint8_t t57 = GM_LD(uint8_t, ((((uint8_t *)&F.l4) + 0) + 0));
            if (t57 != ((uint8_t)3ULL)) goto mn56;
            uint32_t t58 = GM_LD(uint32_t, ((((uint8_t *)&F.l4) + 0) + 8));
            gm_l8 = t58;
            {
                uint32_t t61 = gm_l8;
                void * t62 = gm_l0;
                gm_b16_8 t63 = GM_LD(gm_b16_8, ((((uint8_t *)t62) + 760) + 0));
                uint8_t t64 = 0;
                for (uintptr_t i = 0; i < (uintptr_t)(GM_LD(uintptr_t, ((uint8_t *)&t63) + 8)); i++) if (gm_eq467d5cm1d5(((uint8_t *)GM_LD(void *, ((uint8_t *)&t63))) + i * 4, ((uint8_t *)&t61))) { t64 = 1; break; }
                uint8_t t65 = (uint8_t)(t64);
                if (!t65) goto ie60;
                {
                    void * t67 = gm_l0;
                    gm_b16_8 t68 = gm_l1;
                    gm_tl874t2atnlh3 t69 = gm_l3;
                    gm_b0_1 t70 = gm_f_recursive_without_ret_48l31rm((void *)NULL, cx, NULL, t67, t68, t69);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                {
                    void * t71 = gm_l0;
                    void * t72 = GM_LD(void *, (((uint8_t *)t71) + 0));
                    gm_tl874t2atnlh3 t73 = gm_l3;
                    void * t74 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t72, t73);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t75 = 0;
                    F.ret = t74;
                    goto gm_fret;
                }
                goto ix60;
ie60:;
ix60:;
            }
            {
                void * t76 = gm_l0;
                uint32_t t77 = gm_l8;
                gm_b16_8 t78 = gm_l1;
                gm_tkn42u7na3t81 t79 = gm_l2;
                gm_tl874t2atnlh3 t80 = gm_l3;
                void * t81 = gm_f_named_fn_value_b79g18s((void *)NULL, cx, NULL, t76, t77, t78, t79, t80);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t82 = 0;
                F.ret = t81;
                goto gm_fret;
            }
            goto me7;
mn56:;
ma10:;
            uint8_t t84 = GM_LD(uint8_t, ((((uint8_t *)&F.l4) + 0) + 0));
            if (t84 != ((uint8_t)1ULL)) goto mn83;
            gm_t0ialelq3ql1k t85 = GM_LD(gm_t0ialelq3ql1k, ((((uint8_t *)&F.l4) + 0) + 8));
            F.l9 = t85;
            void * t86 = gm_l0;
            void * t87 = (void *)((uint8_t *)&F.l9);
            gm_tl874t2atnlh3 t88 = gm_l3;
            void * t89 = gm_f_untyped_literal_pfu1ft4((void *)NULL, cx, NULL, t86, t87, t88);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t90 = 0;
            F.ret = t89;
            goto gm_fret;
            goto me7;
mn83:;
ma11:;
            uint8_t t92 = GM_LD(uint8_t, ((((uint8_t *)&F.l4) + 0) + 0));
            if (t92 != ((uint8_t)4ULL)) goto mn91;
            gm_ts78ca2amthgu t93 = GM_LD(gm_ts78ca2amthgu, ((((uint8_t *)&F.l4) + 0) + 8));
            F.l10 = t93;
            gm_t88rcucg1b7uk t94 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l10) + 0));
            gm_t08olq97rscn3 t95;
            memset((uint8_t *)&t95 + 28, 0, 4);
            gm_b16_8 t96 = GM_LD(gm_b16_8, (((uint8_t *)&F.l10) + 8));
            GM_ST(gm_b16_8, (uint8_t *)&t95 + 0, t96);
            gm_tl874t2atnlh3 t97 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l10) + 24));
            GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t95 + 16, t97);
            gm_teti411eujvf4 t98;
            GM_ST(uint8_t, (uint8_t *)&t98 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t98 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t98 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t98 + 40, 0);
            GM_ST(uint64_t, (uint8_t *)&t98 + 48, 0);
            GM_ST(uint64_t, (uint8_t *)&t98 + 56, 0);
            GM_ST(uint8_t, ((uint8_t *)&t98 + 0), ((uint8_t)14ULL));
            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t98 + 8), t95);
            gm_tl874t2atnlh3 t99 = gm_l3;
            void * t100 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t94, &t98, t99);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t101 = 0;
            F.ret = t100;
            goto gm_fret;
            goto me7;
mn91:;
ma12:;
            uint8_t t103 = GM_LD(uint8_t, ((((uint8_t *)&F.l4) + 0) + 0));
            if (t103 != ((uint8_t)2ULL)) goto mn102;
            {
                void * t105 = gm_l0;
                gm_tl874t2atnlh3 t106 = gm_l3;
                gm_b16_8 t107 = gm_l1;
                gm_tnpifv20irnph t108;
                GM_ST(gm_b16_8, ((uint8_t *)&t108 + 0), t107);
                gm_tabb710d9mq2e t109;
                gm_f_format_vau3iu8((void *)&t109, (void *)NULL, cx, NULL, t108);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t109))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t109) + 8, gm_srch2l5nl06vsci, 288, 32);
                gm_b16_8 t110 = GM_LD(gm_b16_8, (((uint8_t *)&t109) + 8));
                gm_b16_8 t111 = GM_LD(gm_b16_8, ((uint8_t *)&t110));
                gm_b0_1 t112 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t105, t106, t111);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t113 = gm_l0;
                void * t114 = GM_LD(void *, (((uint8_t *)t113) + 0));
                gm_tl874t2atnlh3 t115 = gm_l3;
                void * t116 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t114, t115);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t117 = 0;
                F.ret = t116;
                goto gm_fret;
            }
            goto me7;
mn102:;
ma13:;
            uint8_t t119 = GM_LD(uint8_t, ((((uint8_t *)&F.l4) + 0) + 0));
            if (t119 != ((uint8_t)5ULL)) goto mn118;
            gm_t88rcucg1b7uk t120 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&F.l4) + 0) + 8));
            gm_l11 = t120;
            {
                void * t123 = gm_l0;
                void * t124 = GM_LD(void *, (((uint8_t *)t123) + 0));
                void * t125 = (void *)(((uint8_t *)t124) + 8);
                gm_t88rcucg1b7uk t126 = gm_l11;
                uint8_t t127 = gm_f_is_error_8k27gd1(t125, GM_CONV(gm_csguankko4lmhd, t126));
                uint8_t t128 = (uint8_t)!t127;
                if (!t128) goto ie122;
                {
                    void * t130 = gm_l0;
                    gm_tl874t2atnlh3 t131 = gm_l3;
                    void * t132 = gm_l0;
                    gm_t88rcucg1b7uk t133 = gm_l11;
                    gm_b16_8 t134 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t132, t133);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tnpifv20irnph t135;
                    GM_ST(gm_b16_8, ((uint8_t *)&t135 + 0), t134);
                    gm_tabb710d9mq2e t136;
                    gm_f_format_r8e1h45((void *)&t136, (void *)NULL, cx, NULL, t135);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    if (((((uint8_t *)&t136))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t136) + 8, gm_srch2l5nl06vsci, 292, 62);
                    gm_b16_8 t137 = GM_LD(gm_b16_8, (((uint8_t *)&t136) + 8));
                    gm_b16_8 t138 = GM_LD(gm_b16_8, ((uint8_t *)&t137));
                    gm_b0_1 t139 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t130, t131, t138);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                goto ix122;
ie122:;
ix122:;
            }
            {
                void * t140 = gm_l0;
                void * t141 = GM_LD(void *, (((uint8_t *)t140) + 0));
                gm_tl874t2atnlh3 t142 = gm_l3;
                void * t143 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t141, t142);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t144 = 0;
                F.ret = t143;
                goto gm_fret;
            }
            goto me7;
mn118:;
ma14:;
            gm_trap("no match arm matched the scrutinee", gm_srch2l5nl06vsci, 266, 9);
me7:;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t146 = gm_l0;
        void * t147 = GM_LD(void *, (((uint8_t *)t146) + 0));
        gm_b16_8 t148 = gm_l1;
        gm_t9805qtonbsag t149 = gm_f_value_param_tof1h6t(t147, t148);
        if (!((((uint8_t *)&t149))[4] != 0)) goto ie145;
        uint32_t t150 = GM_LD(uint32_t, ((uint8_t *)&t149));
        gm_l12 = t150;
        {
            void * t152 = gm_l0;
            uint32_t t153 = gm_l12;
            gm_t88rcucg1b7uk t154 = gm_f_value_param_ty_qrv22u4((void *)NULL, cx, NULL, t152, t153);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint32_t t155 = gm_l12;
            gm_teti411eujvf4 t156;
            memset(&t156, 0, sizeof t156);
            GM_ST(uint8_t, ((uint8_t *)&t156 + 0), ((uint8_t)9ULL));
            GM_ST(uint32_t, ((uint8_t *)&t156 + 8), t155);
            gm_tl874t2atnlh3 t157 = gm_l3;
            void * t158 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t154, &t156, t157);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t159 = 0;
            F.ret = t158;
            goto gm_fret;
        }
        goto ix145;
ie145:;
ix145:;
    }
    {
        void * t161 = gm_l0;
        void * t162 = GM_LD(void *, (((uint8_t *)t161) + 0));
        gm_b16_8 t163 = gm_l1;
        gm_tk1gen3hgkm51 t164;
        gm_f_local_const_idpokjh((void *)&t164, t162, t163);
        if (!((((uint8_t *)&t164))[224] != 0)) goto ie160;
        F.l13 = (*(gm_tb07pp6njg0bk *)(((uint8_t *)&t164) + 0));
        {
            uint8_t t171 = GM_LD(uint8_t, ((((uint8_t *)&F.l13) + 16) + 0));
            switch (t171) { case ((uint8_t)0ULL): goto ma167; case ((uint8_t)1ULL): goto ma168; case ((uint8_t)2ULL): goto ma169; default: goto ma170; }
ma167:;
            uint8_t t173 = GM_LD(uint8_t, ((((uint8_t *)&F.l13) + 16) + 0));
            if (t173 != ((uint8_t)0ULL)) goto mn172;
            gm_t0ialelq3ql1k t174 = GM_LD(gm_t0ialelq3ql1k, ((((uint8_t *)&F.l13) + 16) + 8));
            F.l14 = t174;
            void * t175 = gm_l0;
            void * t176 = (void *)((uint8_t *)&F.l14);
            gm_tl874t2atnlh3 t177 = gm_l3;
            void * t178 = gm_f_untyped_literal_pfu1ft4((void *)NULL, cx, NULL, t175, t176, t177);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t179 = 0;
            F.ret = t178;
            goto gm_fret;
            goto me166;
mn172:;
ma168:;
            uint8_t t181 = GM_LD(uint8_t, ((((uint8_t *)&F.l13) + 16) + 0));
            if (t181 != ((uint8_t)1ULL)) goto mn180;
            gm_ts78ca2amthgu t182 = GM_LD(gm_ts78ca2amthgu, ((((uint8_t *)&F.l13) + 16) + 8));
            F.l15 = t182;
            gm_t88rcucg1b7uk t183 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l15) + 0));
            gm_t08olq97rscn3 t184;
            memset((uint8_t *)&t184 + 28, 0, 4);
            gm_b16_8 t185 = GM_LD(gm_b16_8, (((uint8_t *)&F.l15) + 8));
            GM_ST(gm_b16_8, (uint8_t *)&t184 + 0, t185);
            gm_tl874t2atnlh3 t186 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l15) + 24));
            GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t184 + 16, t186);
            gm_teti411eujvf4 t187;
            GM_ST(uint8_t, (uint8_t *)&t187 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t187 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t187 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t187 + 40, 0);
            GM_ST(uint64_t, (uint8_t *)&t187 + 48, 0);
            GM_ST(uint64_t, (uint8_t *)&t187 + 56, 0);
            GM_ST(uint8_t, ((uint8_t *)&t187 + 0), ((uint8_t)14ULL));
            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t187 + 8), t184);
            gm_tl874t2atnlh3 t188 = gm_l3;
            void * t189 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t183, &t187, t188);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t190 = 0;
            F.ret = t189;
            goto gm_fret;
            goto me166;
mn180:;
ma169:;
            uint8_t t192 = GM_LD(uint8_t, ((((uint8_t *)&F.l13) + 16) + 0));
            if (t192 != ((uint8_t)2ULL)) goto mn191;
            {
                gm_tl874t2atnlh3 t194 = gm_l3;
                gm_b16_8 t195 = gm_l1;
                gm_tnpifv20irnph t196;
                GM_ST(gm_b16_8, ((uint8_t *)&t196 + 0), t195);
                gm_tabb710d9mq2e t197;
                gm_f_format_9hpfisf((void *)&t197, (void *)NULL, cx, NULL, t196);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t197))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t197) + 8, gm_srch2l5nl06vsci, 304, 39);
                gm_b16_8 t198 = GM_LD(gm_b16_8, (((uint8_t *)&t197) + 8));
                gm_b16_8 t199 = GM_LD(gm_b16_8, ((uint8_t *)&t198));
                gm_f_error_s6hn871((void *)&F.l16, (void *)NULL, cx, NULL, t194, t199);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t200 = (void *)((uint8_t *)&F.l16);
                gm_b16_8 t201;
                GM_ST(void *, (uint8_t *)&t201, (void *)(gm_safvj03783ie0));
                GM_ST(uintptr_t, (uint8_t *)&t201 + 8, (uintptr_t)(64));
                gm_f_add_note_46v502s(t200, t201);
            }
            {
                void * t202 = gm_l0;
                void * t203 = GM_LD(void *, (((uint8_t *)t202) + 0));
                gm_b0_1 t204 = gm_f_emit_dph5v1e((void *)NULL, cx, NULL, t203, &(*(gm_t4gut2c9fj1kt *)((uint8_t *)&F.l16)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t205 = gm_l0;
                void * t206 = GM_LD(void *, (((uint8_t *)t205) + 0));
                gm_tl874t2atnlh3 t207 = gm_l3;
                void * t208 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t206, t207);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t209 = 0;
                F.ret = t208;
                goto gm_fret;
            }
            goto me166;
mn191:;
ma170:;
            gm_trap("no match arm matched the scrutinee", gm_srch2l5nl06vsci, 300, 9);
me166:;
        }
        goto ix160;
ie160:;
ix160:;
    }
    {
        void * t211 = gm_l0;
        void * t212 = GM_LD(void *, (((uint8_t *)t211) + 0));
        gm_b16_8 t213 = gm_l1;
        gm_t9805qtonbsag t214 = gm_f_local_fn_aji8qnd(t212, t213);
        if (!((((uint8_t *)&t214))[4] != 0)) goto ie210;
        uint32_t t215 = GM_LD(uint32_t, ((uint8_t *)&t214));
        gm_l17 = t215;
        {
            void * t217 = gm_l0;
            uint32_t t218 = gm_l17;
            gm_tl874t2atnlh3 t219 = gm_l3;
            void * t220 = gm_f_fn_ref_aqs2fl4((void *)NULL, cx, NULL, t217, t218, t219);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t221 = 0;
            F.ret = t220;
            goto gm_fret;
        }
        goto ix210;
ie210:;
ix210:;
    }
    {
        void * t223 = gm_l0;
        void * t224 = GM_LD(void *, (((uint8_t *)t223) + 0));
        void * t225 = gm_l0;
        uint64_t t226 = GM_LD(uint64_t, (((uint8_t *)t225) + 8));
        gm_b16_8 t227 = gm_l1;
        gm_tf5srh4mp4b3i t228 = gm_f_lookup_module_name_cukfp7t((void *)NULL, cx, NULL, t224, t226, t227);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (!((((uint8_t *)&t228))[8] != 0)) goto ie222;
        uint64_t t229 = GM_LD(uint64_t, ((uint8_t *)&t228));
        gm_l18 = t229;
        {
            void * t231 = gm_l0;
            uint64_t t232 = gm_l18;
            gm_b16_8 t233 = gm_l1;
            gm_tkn42u7na3t81 t234 = gm_l2;
            gm_tl874t2atnlh3 t235 = gm_l3;
            void * t236 = gm_f_decl_expr_lv2h8vf((void *)NULL, cx, NULL, t231, t232, t233, t234, t235);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t237 = 0;
            F.ret = t236;
            goto gm_fret;
        }
        goto ix222;
ie222:;
ix222:;
    }
    {
        gm_l19 = ((uint8_t)0);
    }
    {
        void * t239 = gm_l0;
        void * t240 = GM_LD(void *, (((uint8_t *)t239) + 0));
        gm_b16_8 t241 = gm_l1;
        gm_tkn42u7na3t81 t242 = gm_f_type_param_3av4r8i(t240, t241);
        if (!((((uint8_t *)&t242))[4] != 0)) goto ie238;
        gm_t88rcucg1b7uk t243 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&t242));
        gm_l20 = t243;
        {
            void * t246 = gm_l0;
            void * t247 = GM_LD(void *, (((uint8_t *)t246) + 0));
            void * t248 = (void *)(((uint8_t *)t247) + 8);
            gm_t88rcucg1b7uk t249 = gm_l20;
            gm_t53at80pfdus3 t250;
            gm_f_kind_jcnt4hm((void *)&t250, t248, GM_CONV(gm_csguankko4lmhd, t249));
            uint8_t t251 = GM_LD(uint8_t, (((uint8_t *)&t250) + 0));
            if (t251 != ((uint8_t)19ULL)) goto ie245;
            uint32_t t252 = GM_LD(uint32_t, (((uint8_t *)&t250) + 8));
            gm_l21 = t252;
            {
                void * t254 = gm_l0;
                void * t255 = GM_LD(void *, (((uint8_t *)t254) + 0));
                uint32_t t256 = gm_l21;
                gm_tnnjeerolos0a t257 = gm_f_param_kind_a0aikmb(t255, t256);
                uint8_t t258 = ((GM_LD(uint8_t, ((uint8_t *)&t257) + 0) == ((uint8_t)1ULL)));
                gm_l19 = t258;
            }
            goto ix245;
ie245:;
ix245:;
        }
        goto ix238;
ie238:;
ix238:;
    }
    {
        uint8_t t260 = gm_l19;
        if (!t260) goto ie259;
        {
            void * t262 = gm_l0;
            gm_tl874t2atnlh3 t263 = gm_l3;
            gm_b16_8 t264 = gm_l1;
            gm_tnpifv20irnph t265;
            GM_ST(gm_b16_8, ((uint8_t *)&t265 + 0), t264);
            gm_tabb710d9mq2e t266;
            gm_f_format_r6aoskm((void *)&t266, (void *)NULL, cx, NULL, t265);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t266))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t266) + 8, gm_srch2l5nl06vsci, 319, 24);
            gm_b16_8 t267 = GM_LD(gm_b16_8, (((uint8_t *)&t266) + 8));
            gm_b16_8 t268 = GM_LD(gm_b16_8, ((uint8_t *)&t267));
            gm_b0_1 t269 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t262, t263, t268);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix259;
ie259:;
        gm_b16_8 t271 = gm_l1;
        uint8_t t272 = gm_f_is_builtin_type_name_v5ttoka(t271);
        if (!t272) goto ie270;
        {
            void * t274 = gm_l0;
            gm_tl874t2atnlh3 t275 = gm_l3;
            gm_b16_8 t276 = gm_l1;
            gm_tnpifv20irnph t277;
            GM_ST(gm_b16_8, ((uint8_t *)&t277 + 0), t276);
            gm_tabb710d9mq2e t278;
            gm_f_format_r8e1h45((void *)&t278, (void *)NULL, cx, NULL, t277);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t278))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t278) + 8, gm_srch2l5nl06vsci, 321, 24);
            gm_b16_8 t279 = GM_LD(gm_b16_8, (((uint8_t *)&t278) + 8));
            gm_b16_8 t280 = GM_LD(gm_b16_8, ((uint8_t *)&t279));
            gm_b0_1 t281 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t274, t275, t280);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix270;
ie270:;
        gm_b16_8 t283 = gm_l1;
        gm_tud7oovfvnf43 t284;
        gm_b16_8 t285;
        GM_ST(void *, (uint8_t *)&t285, (void *)(gm_s2r7keqsravo0));
        GM_ST(uintptr_t, (uint8_t *)&t285 + 8, (uintptr_t)(5));
        GM_ST(gm_b16_8, (uint8_t *)&t284 + 0, t285);
        gm_b16_8 t286;
        GM_ST(void *, (uint8_t *)&t286, (void *)(gm_so4gnucc3fl4v));
        GM_ST(uintptr_t, (uint8_t *)&t286 + 8, (uintptr_t)(6));
        GM_ST(gm_b16_8, (uint8_t *)&t284 + 16, t286);
        gm_b16_8 t287;
        GM_ST(void *, (uint8_t *)&t287, (void *)(gm_sinmpfqtbo9in));
        GM_ST(uintptr_t, (uint8_t *)&t287 + 8, (uintptr_t)(4));
        GM_ST(gm_b16_8, (uint8_t *)&t284 + 32, t287);
        gm_b16_8 t288;
        GM_ST(void *, (uint8_t *)&t288, (void *)(gm_s9ur7sj9r0g63));
        GM_ST(uintptr_t, (uint8_t *)&t288 + 8, (uintptr_t)(6));
        GM_ST(gm_b16_8, (uint8_t *)&t284 + 48, t288);
        gm_b16_8 t289;
        GM_ST(void *, (uint8_t *)&t289, (void *)(gm_s3231a310mran));
        GM_ST(uintptr_t, (uint8_t *)&t289 + 8, (uintptr_t)(9));
        GM_ST(gm_b16_8, (uint8_t *)&t284 + 64, t289);
        uint8_t t290 = 0;
        if (gm_eq0iqiubfm7e(((uint8_t *)&t284) + 0, ((uint8_t *)&t283))) t290 = 1;
        if (gm_eq0iqiubfm7e(((uint8_t *)&t284) + 16, ((uint8_t *)&t283))) t290 = 1;
        if (gm_eq0iqiubfm7e(((uint8_t *)&t284) + 32, ((uint8_t *)&t283))) t290 = 1;
        if (gm_eq0iqiubfm7e(((uint8_t *)&t284) + 48, ((uint8_t *)&t283))) t290 = 1;
        if (gm_eq0iqiubfm7e(((uint8_t *)&t284) + 64, ((uint8_t *)&t283))) t290 = 1;
        uint8_t t291 = (uint8_t)(t290);
        if (!t291) goto ie282;
        {
            void * t293 = gm_l0;
            gm_tl874t2atnlh3 t294 = gm_l3;
            gm_b16_8 t295 = gm_l1;
            gm_tnpifv20irnph t296;
            GM_ST(gm_b16_8, ((uint8_t *)&t296 + 0), t295);
            gm_tabb710d9mq2e t297;
            gm_f_format_aovijlt((void *)&t297, (void *)NULL, cx, NULL, t296);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t297))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t297) + 8, gm_srch2l5nl06vsci, 323, 24);
            gm_b16_8 t298 = GM_LD(gm_b16_8, (((uint8_t *)&t297) + 8));
            gm_b16_8 t299 = GM_LD(gm_b16_8, ((uint8_t *)&t298));
            gm_b0_1 t300 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t293, t294, t299);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix282;
ie282:;
        {
            void * t302 = gm_l0;
            void * t303 = GM_LD(void *, (((uint8_t *)t302) + 0));
            void * t304 = gm_l0;
            uint64_t t305 = GM_LD(uint64_t, (((uint8_t *)t304) + 8));
            gm_b16_8 t306 = gm_l1;
            gm_tl874t2atnlh3 t307 = gm_l3;
            gm_b0_1 t308 = gm_f_unknown_name_dmegum5((void *)NULL, cx, NULL, t303, t305, t306, t307);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
ix282:;
ix270:;
ix259:;
    }
    {
        void * t309 = gm_l0;
        void * t310 = GM_LD(void *, (((uint8_t *)t309) + 0));
        gm_tl874t2atnlh3 t311 = gm_l3;
        void * t312 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t310, t311);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t313 = 0;
        F.ret = t312;
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
inline __attribute__((aligned(4))) void * gm_f_named_fn_value_b79g18s(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_b16_8 a2, gm_tkn42u7na3t81 a3, gm_tl874t2atnlh3 a4) {
    struct gm_fr_named_fn_value_b79g18s F;
    void * gm_l0; /* cx */
    uint32_t gm_l1; /* f */
    gm_b16_8 gm_l2; /* name */
    gm_tkn42u7na3t81 gm_l3; /* expect */
    gm_tl874t2atnlh3 gm_l4; /* span */
    void * gm_l5; /* e */
    GM_STACK_CHECK(&F, 392u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    {
        uint32_t t3 = gm_l1;
        uint64_t t4 = (uint64_t)t3;
        uint64_t t5 = (uintptr_t)t4;
        void * t6 = gm_l0;
        void * t7 = GM_LD(void *, (((uint8_t *)t6) + 0));
        uint64_t t8 = GM_LD(uintptr_t, ((((uint8_t *)t7) + 344) + 0) + 8);
        if (t5 >= (uintptr_t)(t8)) gm_trap_index(t5, t8, gm_srch2l5nl06vsci, 373, 8);
        uint64_t t9 = GM_LD(uintptr_t, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t7) + 344) + 0)) + t5 * 136) + 80) + 8);
        uint8_t t10 = (t9 > ((uint64_t)0ULL));
        if (!t10) goto ie2;
        {
            void * t13 = gm_l0;
            uint32_t t14 = gm_l1;
            gm_tkn42u7na3t81 t15 = gm_l3;
            gm_tl874t2atnlh3 t16 = gm_l4;
            void * t17 = gm_f_generic_fn_from_expected_ok2k7go((void *)NULL, cx, NULL, t13, t14, t15, t16);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (!(GM_LD(void *, ((uint8_t *)&t17)) != 0)) goto ie12;
            void * t18 = GM_LD(void *, ((uint8_t *)&t17));
            gm_l5 = t18;
            {
                void * t20 = gm_l5;
                uint8_t t21 = 0;
                F.ret = t20;
                goto gm_fret;
            }
            goto ix12;
ie12:;
ix12:;
        }
        {
            void * t22 = gm_l0;
            gm_tl874t2atnlh3 t23 = gm_l4;
            gm_t6j6smj7qoctu t24;
            gm_b16_8 t25 = gm_l2;
            GM_ST(gm_b16_8, (uint8_t *)&t24 + 0, t25);
            gm_b16_8 t26 = gm_l2;
            GM_ST(gm_b16_8, (uint8_t *)&t24 + 16, t26);
            gm_tabb710d9mq2e t27;
            gm_f_format_54ifjd1((void *)&t27, (void *)NULL, cx, NULL, &t24);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t27))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t27) + 8, gm_srch2l5nl06vsci, 375, 24);
            gm_b16_8 t28 = GM_LD(gm_b16_8, (((uint8_t *)&t27) + 8));
            gm_b16_8 t29 = GM_LD(gm_b16_8, ((uint8_t *)&t28));
            gm_b0_1 t30 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t22, t23, t29);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t31 = gm_l0;
            void * t32 = GM_LD(void *, (((uint8_t *)t31) + 0));
            gm_tl874t2atnlh3 t33 = gm_l4;
            void * t34 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t32, t33);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t35 = 0;
            F.ret = t34;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t36 = gm_l0;
        uint32_t t37 = gm_l1;
        gm_tl874t2atnlh3 t38 = gm_l4;
        void * t39 = gm_f_fn_ref_aqs2fl4((void *)NULL, cx, NULL, t36, t37, t38);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t40 = 0;
        F.ret = t39;
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
__attribute__((aligned(4))) void * gm_f_neg_expr_p72q1ni(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_tkn42u7na3t81 a2, gm_tl874t2atnlh3 a3) {
    struct gm_fr_neg_expr_p72q1ni F;
    void * gm_l0; /* cx */
    void * gm_l1; /* operand */
    gm_tkn42u7na3t81 gm_l2; /* expect */
    gm_tl874t2atnlh3 gm_l3; /* span */
    void * gm_l4; /* v */
    uint8_t gm_l5; /* literal */
    void * gm_l7; /* r */
    gm_t88rcucg1b7uk gm_l8; /* t */
    GM_STACK_CHECK(&F, 1103u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        void * t2 = gm_l0;
        void * t3 = gm_l1;
        gm_tkn42u7na3t81 t4 = gm_l2;
        void * t5 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t2, t3, t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l4 = t5;
    }
    {
        uint8_t t6 = 0;
        void * t7 = gm_l1;
        uint8_t t8 = gm_f_is_literal_tif53t0(t7);
        if (!t8) {
            void * t9 = gm_l0;
            void * t10 = GM_LD(void *, (((uint8_t *)t9) + 0));
            void * t11 = (void *)(((uint8_t *)t10) + 8);
            void * t12 = gm_l4;
            gm_t88rcucg1b7uk t13 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t12) + 0));
            gm_t53at80pfdus3 t14;
            gm_f_kind_jcnt4hm((void *)&t14, t11, GM_CONV(gm_csguankko4lmhd, t13));
            uint8_t t15;
            uint8_t t18 = GM_LD(uint8_t, (((uint8_t *)&t14) + 0));
            if (t18 != ((uint8_t)23ULL)) goto mn17;
            t15 = ((uint8_t)1);
            goto me16;
mn17:;
            t15 = ((uint8_t)0);
            goto me16;
mn19:;
            gm_trap("no match arm matched the scrutinee", gm_srch2l5nl06vsci, 643, 39);
me16:;
            t6 = t15;
        } else t6 = 1;
        gm_l5 = t6;
    }
    {
        void * t20 = gm_l4;
        uint8_t t26 = GM_LD(uint8_t, ((((uint8_t *)t20) + 8) + 0));
        switch (t26) { case ((uint8_t)0ULL): goto ma22; case ((uint8_t)1ULL): goto ma24; case ((uint8_t)2ULL): goto ma23; case ((uint8_t)3ULL): goto ma24; case ((uint8_t)4ULL): goto ma24; case ((uint8_t)5ULL): goto ma24; case ((uint8_t)6ULL): goto ma24; case ((uint8_t)7ULL): goto ma24; case ((uint8_t)8ULL): goto ma24; case ((uint8_t)9ULL): goto ma24; case ((uint8_t)10ULL): goto ma24; case ((uint8_t)11ULL): goto ma24; case ((uint8_t)12ULL): goto ma24; case ((uint8_t)13ULL): goto ma24; case ((uint8_t)14ULL): goto ma24; case ((uint8_t)15ULL): goto ma24; case ((uint8_t)16ULL): goto ma24; case ((uint8_t)17ULL): goto ma24; case ((uint8_t)18ULL): goto ma24; case ((uint8_t)19ULL): goto ma24; case ((uint8_t)20ULL): goto ma24; case ((uint8_t)21ULL): goto ma24; case ((uint8_t)22ULL): goto ma24; case ((uint8_t)23ULL): goto ma24; case ((uint8_t)24ULL): goto ma24; case ((uint8_t)25ULL): goto ma24; case ((uint8_t)26ULL): goto ma24; case ((uint8_t)27ULL): goto ma24; case ((uint8_t)28ULL): goto ma24; case ((uint8_t)29ULL): goto ma24; case ((uint8_t)30ULL): goto ma24; case ((uint8_t)31ULL): goto ma24; case ((uint8_t)32ULL): goto ma24; case ((uint8_t)33ULL): goto ma24; case ((uint8_t)34ULL): goto ma24; case ((uint8_t)35ULL): goto ma24; case ((uint8_t)36ULL): goto ma24; case ((uint8_t)37ULL): goto ma24; case ((uint8_t)38ULL): goto ma24; case ((uint8_t)39ULL): goto ma24; case ((uint8_t)40ULL): goto ma24; case ((uint8_t)41ULL): goto ma24; case ((uint8_t)42ULL): goto ma24; case ((uint8_t)43ULL): goto ma24; case ((uint8_t)44ULL): goto ma24; case ((uint8_t)45ULL): goto ma24; case ((uint8_t)46ULL): goto ma24; case ((uint8_t)47ULL): goto ma24; case ((uint8_t)48ULL): goto ma24; case ((uint8_t)49ULL): goto ma24; case ((uint8_t)50ULL): goto ma24; case ((uint8_t)51ULL): goto ma24; case ((uint8_t)52ULL): goto ma24; case ((uint8_t)53ULL): goto ma24; case ((uint8_t)54ULL): goto ma24; case ((uint8_t)55ULL): goto ma24; case ((uint8_t)56ULL): goto ma24; case ((uint8_t)57ULL): goto ma24; case ((uint8_t)58ULL): goto ma24; default: goto ma24; }
ma22:;
        uint8_t t28 = GM_LD(uint8_t, ((((uint8_t *)t20) + 8) + 0));
        if (t28 != ((uint8_t)0ULL)) goto mn27;
        gm_tbci5ospk5nnn t29 = GM_LD(gm_tbci5ospk5nnn, ((((uint8_t *)t20) + 8) + 8));
        F.l6 = t29;
        uint8_t t31 = gm_l5;
        if (!t31) goto ie30;
        {
            void * t33 = gm_l4;
            uint8_t *t34 = (((uint8_t *)t33) + 8);
            gm_tbci5ospk5nnn t35;
            gm_f_neg_agd5c5k((void *)&t35, (void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l6)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(uint8_t, (uint8_t *)&(*(gm_teti411eujvf4 *)t34) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_teti411eujvf4 *)t34) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_teti411eujvf4 *)t34) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_teti411eujvf4 *)t34) + 56, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_teti411eujvf4 *)t34) + 0, ((uint8_t)0ULL));
            GM_ST(gm_tbci5ospk5nnn, (uint8_t *)&(*(gm_teti411eujvf4 *)t34) + 8, t35);
        }
        {
            void * t36 = gm_l4;
            gm_tl874t2atnlh3 t37 = gm_l3;
            GM_ST(gm_tl874t2atnlh3, (((uint8_t *)t36) + 72), t37);
        }
        {
            void * t38 = gm_l4;
            uint8_t t39 = 0;
            F.ret = t38;
            goto gm_fret;
        }
        goto ix30;
ie30:;
ix30:;
        goto me21;
mn27:;
ma23:;
        uint8_t t41 = GM_LD(uint8_t, ((((uint8_t *)t20) + 8) + 0));
        if (t41 != ((uint8_t)2ULL)) goto mn40;
        void * t42 = GM_LD(void *, ((((uint8_t *)t20) + 8) + 8));
        gm_l7 = t42;
        uint8_t t44 = gm_l5;
        if (!t44) goto ie43;
        void * t45 = gm_l7;
        uint8_t t46 = gm_f_is_zero_5aept6u(&(*(gm_tngtuiuskn938 *)((uint8_t *)t45)));
        uint8_t t47 = (uint8_t)!t46;
        if (!t47) goto ie43;
        {
            void * t49 = gm_l4;
            uint8_t *t50 = (((uint8_t *)t49) + 8);
            void * t51 = gm_l7;
            gm_tngtuiuskn938 t52;
            gm_f_neg_0u9uqg6((void *)&t52, (void *)NULL, cx, NULL, &(*(gm_tngtuiuskn938 *)((uint8_t *)t51)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_f_untyped_float_m6lbr0g((void *)&(*(gm_teti411eujvf4 *)t50), (void *)NULL, cx, NULL, &t52);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t53 = gm_l4;
            gm_tl874t2atnlh3 t54 = gm_l3;
            GM_ST(gm_tl874t2atnlh3, (((uint8_t *)t53) + 72), t54);
        }
        {
            void * t55 = gm_l4;
            uint8_t t56 = 0;
            F.ret = t55;
            goto gm_fret;
        }
        goto ix43;
ie43:;
ix43:;
        goto me21;
mn40:;
ma24:;
        goto me21;
mn57:;
ma25:;
        gm_trap("no match arm matched the scrutinee", gm_srch2l5nl06vsci, 644, 5);
me21:;
    }
    {
        void * t59 = gm_l0;
        void * t60 = (void *)(((uint8_t *)t59) + 96);
        void * t61 = gm_l0;
        void * t62 = GM_LD(void *, (((uint8_t *)t61) + 0));
        void * t63 = gm_l4;
        gm_t88rcucg1b7uk t64 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t63) + 0));
        gm_t88rcucg1b7uk t65 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t60, t62, t64);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l8 = t65;
    }
    {
        void * t67 = gm_l0;
        void * t68 = GM_LD(void *, (((uint8_t *)t67) + 0));
        void * t69 = (void *)(((uint8_t *)t68) + 8);
        gm_t88rcucg1b7uk t70 = gm_l8;
        uint8_t t71 = gm_f_is_vector_gkq6b88(t69, GM_CONV(gm_csguankko4lmhd, t70));
        if (!t71) goto ie66;
        {
            void * t73 = gm_l0;
            gm_t9qkh0rsooc8d t74;
            GM_ST(uint8_t, ((uint8_t *)&t74 + 0), ((uint8_t)0ULL));
            void * t75 = gm_l4;
            gm_tl874t2atnlh3 t76 = gm_l3;
            void * t77 = gm_f_vector_unary_2mbe97t((void *)NULL, cx, NULL, t73, t74, t75, t76);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t78 = 0;
            F.ret = t77;
            goto gm_fret;
        }
        goto ix66;
ie66:;
ix66:;
    }
    {
        void * t80 = gm_l0;
        void * t81 = gm_l4;
        gm_t88rcucg1b7uk t82 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t81) + 0));
        uint8_t t83 = gm_f_is_compound_number_kbipjl5((void *)NULL, cx, NULL, t80, t82);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (!t83) goto ie79;
        {
            void * t85 = gm_l0;
            gm_t9qkh0rsooc8d t86;
            GM_ST(uint8_t, ((uint8_t *)&t86 + 0), ((uint8_t)0ULL));
            void * t87 = gm_l4;
            gm_tl874t2atnlh3 t88 = gm_l3;
            void * t89 = gm_f_compound_number_unary_4h478fs((void *)NULL, cx, NULL, t85, t86, t87, t88);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t90 = 0;
            F.ret = t89;
            goto gm_fret;
        }
        goto ix79;
ie79:;
ix79:;
    }
    {
        void * t92 = gm_l0;
        void * t93 = GM_LD(void *, (((uint8_t *)t92) + 0));
        void * t94 = (void *)(((uint8_t *)t93) + 8);
        gm_t88rcucg1b7uk t95 = gm_l8;
        gm_t53at80pfdus3 t96;
        gm_f_kind_jcnt4hm((void *)&t96, t94, GM_CONV(gm_csguankko4lmhd, t95));
        uint8_t t97;
        uint8_t t100 = GM_LD(uint8_t, (((uint8_t *)&t96) + 0));
        if (t100 != ((uint8_t)17ULL)) goto mn99;
        t97 = ((uint8_t)1);
        goto me98;
mn99:;
        t97 = ((uint8_t)0);
        goto me98;
mn101:;
        gm_trap("no match arm matched the scrutinee", gm_srch2l5nl06vsci, 661, 8);
me98:;
        if (!t97) goto ie91;
        {
            void * t103 = gm_l0;
            gm_tl874t2atnlh3 t104 = gm_l3;
            gm_b16_8 t105;
            GM_ST(void *, (uint8_t *)&t105, (void *)(gm_shnmjdmsj431f));
            GM_ST(uintptr_t, (uint8_t *)&t105 + 8, (uintptr_t)(16));
            void * t106 = gm_f_unsupported_r3r7qdh((void *)NULL, cx, NULL, t103, t104, t105);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t107 = 0;
            F.ret = t106;
            goto gm_fret;
        }
        goto ix91;
ie91:;
ix91:;
    }
    {
        void * t108 = gm_l0;
        void * t109 = (void *)(((uint8_t *)t108) + 320);
        gm_t91vm1g0k2rpm t110;
        memset((uint8_t *)&t110 + 34, 0, 6);
        void * t111 = gm_l4;
        gm_t88rcucg1b7uk t112 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t111) + 0));
        GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t110 + 0, t112);
        gm_tl874t2atnlh3 t113 = gm_l3;
        GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t110 + 4, t113);
        gm_b16_8 t114;
        GM_ST(void *, (uint8_t *)&t114, (void *)(gm_skolmnfc0ccbd));
        GM_ST(uintptr_t, (uint8_t *)&t114 + 8, (uintptr_t)(1));
        GM_ST(gm_b16_8, (uint8_t *)&t110 + 16, t114);
        GM_ST(uint8_t, (uint8_t *)&t110 + 32, ((uint8_t)0));
        GM_ST(uint8_t, (uint8_t *)&t110 + 33, ((uint8_t)1));
        gm_tpm220j5qhjke t115;
        GM_ST(uint8_t, (uint8_t *)&t115 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t115 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t115 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t115 + 0), ((uint8_t)3ULL));
        GM_ST(gm_t91vm1g0k2rpm, ((uint8_t *)&t115 + 8), t110);
        gm_tftb1odc739t5 t116 = gm_f_push_7koe9lk(t109, &t115);
        if (((((uint8_t *)&t116))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t116) + 1, gm_srch2l5nl06vsci, 662, 5);
        gm_b0_1 t117 = GM_LD(gm_b0_1, (((uint8_t *)&t116) + 1));
    }
    {
        void * t118 = gm_l4;
        gm_t88rcucg1b7uk t119 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t118) + 0));
        gm_t9qkh0rsooc8d t120;
        GM_ST(uint8_t, ((uint8_t *)&t120 + 0), ((uint8_t)0ULL));
        void * t121 = gm_l4;
        gm_t8ghgl2jj0qha t122;
        GM_ST(uint8_t, (uint8_t *)&t122 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t122 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t122 + 4, 0);
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t122 + 0), t120);
        GM_ST(void *, ((uint8_t *)&t122 + 8), t121);
        gm_teti411eujvf4 t123;
        memset(&t123, 0, sizeof t123);
        GM_ST(uint8_t, ((uint8_t *)&t123 + 0), ((uint8_t)29ULL));
        GM_ST(gm_t8ghgl2jj0qha, ((uint8_t *)&t123 + 8), t122);
        gm_tl874t2atnlh3 t124 = gm_l3;
        void * t125 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t119, &t123, t124);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t126 = 0;
        F.ret = t125;
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
inline __attribute__((aligned(4))) void * gm_f_null_value_ruu6332(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tkn42u7na3t81 a1, gm_tl874t2atnlh3 a2) {
    struct gm_fr_null_value_ruu6332 F;
    void * gm_l0; /* cx */
    gm_tkn42u7na3t81 gm_l1; /* expect */
    gm_tl874t2atnlh3 gm_l2; /* span */
    gm_t88rcucg1b7uk gm_l3; /* x */
    gm_t88rcucg1b7uk gm_l4; /* t */
    gm_t88rcucg1b7uk gm_l5; /* ok */
    GM_STACK_CHECK(&F, 620u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        if (!((((uint8_t *)&gm_l1))[4] != 0)) goto ie2;
        gm_t88rcucg1b7uk t3 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l1));
        gm_l3 = t3;
        {
            void * t5 = gm_l0;
            void * t6 = (void *)(((uint8_t *)t5) + 96);
            void * t7 = gm_l0;
            void * t8 = GM_LD(void *, (((uint8_t *)t7) + 0));
            void * t9 = (void *)(((uint8_t *)t8) + 8);
            gm_t88rcucg1b7uk t10 = gm_l3;
            gm_t88rcucg1b7uk t11 = gm_f_shallow_8ur803a((void *)NULL, cx, NULL, t6, t9, t10);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l4 = t11;
        }
        {
            void * t13 = gm_l0;
            void * t14 = GM_LD(void *, (((uint8_t *)t13) + 0));
            void * t15 = (void *)(((uint8_t *)t14) + 8);
            gm_t88rcucg1b7uk t16 = gm_l4;
            gm_t53at80pfdus3 t17;
            gm_f_kind_jcnt4hm((void *)&t17, t15, GM_CONV(gm_csguankko4lmhd, t16));
            uint8_t t18;
            uint8_t t21 = GM_LD(uint8_t, (((uint8_t *)&t17) + 0));
            if (t21 != ((uint8_t)13ULL)) goto mn20;
            t18 = ((uint8_t)1);
            goto me19;
mn20:;
            t18 = ((uint8_t)0);
            goto me19;
mn22:;
            gm_trap("no match arm matched the scrutinee", gm_srch2l5nl06vsci, 222, 12);
me19:;
            if (!t18) goto ie12;
            {
                gm_t88rcucg1b7uk t24 = gm_l4;
                gm_teti411eujvf4 t25;
                memset(&t25, 0, sizeof t25);
                GM_ST(uint8_t, ((uint8_t *)&t25 + 0), ((uint8_t)7ULL));
                gm_tl874t2atnlh3 t26 = gm_l2;
                void * t27 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t24, &t25, t26);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t28 = 0;
                F.ret = t27;
                goto gm_fret;
            }
            goto ix12;
ie12:;
ix12:;
        }
        {
            void * t30 = gm_l0;
            void * t31 = GM_LD(void *, (((uint8_t *)t30) + 0));
            void * t32 = (void *)(((uint8_t *)t31) + 8);
            gm_t88rcucg1b7uk t33 = gm_l4;
            gm_t53at80pfdus3 t34;
            gm_f_kind_jcnt4hm((void *)&t34, t32, GM_CONV(gm_csguankko4lmhd, t33));
            uint8_t t35 = GM_LD(uint8_t, (((uint8_t *)&t34) + 0));
            if (t35 != ((uint8_t)14ULL)) goto ie29;
            gm_t88rcucg1b7uk t36 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t34) + 8) + 4));
            gm_l5 = t36;
            void * t37 = gm_l0;
            void * t38 = GM_LD(void *, (((uint8_t *)t37) + 0));
            void * t39 = (void *)(((uint8_t *)t38) + 8);
            gm_t88rcucg1b7uk t40 = gm_l5;
            gm_t53at80pfdus3 t41;
            gm_f_kind_jcnt4hm((void *)&t41, t39, GM_CONV(gm_csguankko4lmhd, t40));
            uint8_t t42;
            uint8_t t45 = GM_LD(uint8_t, (((uint8_t *)&t41) + 0));
            if (t45 != ((uint8_t)13ULL)) goto mn44;
            t42 = ((uint8_t)1);
            goto me43;
mn44:;
            t42 = ((uint8_t)0);
            goto me43;
mn46:;
            gm_trap("no match arm matched the scrutinee", gm_srch2l5nl06vsci, 223, 51);
me43:;
            if (!t42) goto ie29;
            {
                gm_t88rcucg1b7uk t48 = gm_l5;
                gm_teti411eujvf4 t49;
                memset(&t49, 0, sizeof t49);
                GM_ST(uint8_t, ((uint8_t *)&t49 + 0), ((uint8_t)7ULL));
                gm_tl874t2atnlh3 t50 = gm_l2;
                void * t51 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t48, &t49, t50);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t52 = 0;
                F.ret = t51;
                goto gm_fret;
            }
            goto ix29;
ie29:;
ix29:;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t53 = gm_l0;
        gm_tl874t2atnlh3 t54 = gm_l2;
        gm_b16_8 t55;
        GM_ST(void *, (uint8_t *)&t55, (void *)(gm_ssfg7m66qbmvk));
        GM_ST(uintptr_t, (uint8_t *)&t55 + 8, (uintptr_t)(64));
        gm_b0_1 t56 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t53, t54, t55);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t57 = gm_l0;
        void * t58 = GM_LD(void *, (((uint8_t *)t57) + 0));
        gm_tl874t2atnlh3 t59 = gm_l2;
        void * t60 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t58, t59);
        if (gm_unw.kind) {
            goto gm_lad;
        }
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
__attribute__((aligned(4))) void * gm_f_pointer_arith_abqsrkl(void *env, gm_node *cx, gm_node *inst, void * a0, gm_ts6oqppjh0ba3 a1, void * a2, void * a3, gm_tl874t2atnlh3 a4) {
    struct gm_fr_pointer_arith_abqsrkl F;
    void * gm_l0; /* cx */
    gm_ts6oqppjh0ba3 gm_l1; /* op */
    void * gm_l2; /* lhs */
    void * gm_l3; /* rhs */
    gm_tl874t2atnlh3 gm_l4; /* span */
    void * gm_l5; /* tys */
    gm_t88rcucg1b7uk gm_l6; /* lt */
    void * gm_l7; /* r */
    gm_t88rcucg1b7uk gm_l8; /* rt */
    uint8_t gm_l9; /* sub */
    gm_t88rcucg1b7uk gm_l10; /* elem */
    gm_t88rcucg1b7uk gm_l11; /* le */
    gm_t88rcucg1b7uk gm_l12; /* ro */
    void * gm_l13; /* l */
    void * gm_l14; /* r2 */
    gm_t5fru0lp11n7t gm_l15; /* <temporary array> */
    gm_t5fru0lp11n7t gm_l16; /* <temporary array> */
    GM_STACK_CHECK(&F, 1584u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    {
        void * t2 = gm_l0;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 0));
        void * t4 = (void *)(((uint8_t *)t3) + 8);
        gm_l5 = t4;
    }
    {
        void * t5 = gm_l0;
        void * t6 = (void *)(((uint8_t *)t5) + 96);
        void * t7 = gm_l0;
        void * t8 = GM_LD(void *, (((uint8_t *)t7) + 0));
        void * t9 = gm_l2;
        gm_t88rcucg1b7uk t10 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t9) + 0));
        gm_t88rcucg1b7uk t11 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t6, t8, t10);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l6 = t11;
    }
    {
        void * t12 = gm_l0;
        void * t13 = gm_l3;
        gm_tkn42u7na3t81 t14;
        memset(&t14, 0, sizeof t14);
        void * t15 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t12, t13, t14);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l7 = t15;
    }
    {
        void * t16 = gm_l0;
        void * t17 = (void *)(((uint8_t *)t16) + 96);
        void * t18 = gm_l0;
        void * t19 = GM_LD(void *, (((uint8_t *)t18) + 0));
        void * t20 = gm_l7;
        gm_t88rcucg1b7uk t21 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t20) + 0));
        gm_t88rcucg1b7uk t22 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t17, t19, t21);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l8 = t22;
    }
    {
        uint8_t t23;
        uint8_t t26 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t26 != ((uint8_t)1ULL)) goto mn25;
        t23 = ((uint8_t)1);
        goto me24;
mn25:;
        t23 = ((uint8_t)0);
        goto me24;
mn27:;
        gm_trap("no match arm matched the scrutinee", gm_srch2l5nl06vsci, 839, 12);
me24:;
        gm_l9 = t23;
    }
    {
        void * t29 = gm_l5;
        gm_t88rcucg1b7uk t30 = gm_l8;
        gm_t53at80pfdus3 t31;
        gm_f_kind_jcnt4hm((void *)&t31, t29, GM_CONV(gm_csguankko4lmhd, t30));
        uint8_t t32 = GM_LD(uint8_t, (((uint8_t *)&t31) + 0));
        if (t32 != ((uint8_t)9ULL)) goto ie28;
        gm_t88rcucg1b7uk t33 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t31) + 8) + 4));
        gm_l10 = t33;
        {
            void * t36 = gm_l5;
            gm_t88rcucg1b7uk t37 = gm_l6;
            gm_t53at80pfdus3 t38;
            gm_f_kind_jcnt4hm((void *)&t38, t36, GM_CONV(gm_csguankko4lmhd, t37));
            uint8_t t39 = GM_LD(uint8_t, (((uint8_t *)&t38) + 0));
            if (t39 != ((uint8_t)9ULL)) goto ie35;
            gm_t88rcucg1b7uk t40 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t38) + 8) + 4));
            gm_l11 = t40;
            {
                uint8_t t43 = 0;
                uint8_t t44 = gm_l9;
                uint8_t t45 = (uint8_t)!t44;
                if (!t45) {
                    gm_t88rcucg1b7uk t46 = gm_l10;
                    gm_t88rcucg1b7uk t47 = gm_l11;
                    uint8_t t48 = (!gm_eq9m6mvtmgde(((uint8_t *)&t46), ((uint8_t *)&t47)));
                    t43 = t48;
                } else t43 = 1;
                if (!t43) goto ie42;
                {
                    void * t50 = gm_l0;
                    gm_tl874t2atnlh3 t51 = gm_l4;
                    gm_tfeab279c16rr t52;
                    gm_b16_8 t53;
                    uint8_t t55 = gm_l9;
                    if (!t55) goto ie54;
                    gm_b16_8 t57;
                    GM_ST(void *, (uint8_t *)&t57, (void *)(gm_sa7q2bqmn8lgb));
                    GM_ST(uintptr_t, (uint8_t *)&t57 + 8, (uintptr_t)(8));
                    gm_b16_8 t58 = t57;
                    t53 = t58;
                    goto ix54;
ie54:;
                    gm_b16_8 t60;
                    GM_ST(void *, (uint8_t *)&t60, (void *)(gm_s4o70d89183c2));
                    GM_ST(uintptr_t, (uint8_t *)&t60 + 8, (uintptr_t)(3));
                    gm_b16_8 t61 = t60;
                    t53 = t61;
ix54:;
                    GM_ST(gm_b16_8, (uint8_t *)&t52 + 0, t53);
                    void * t62 = gm_l0;
                    gm_t88rcucg1b7uk t63 = gm_l6;
                    gm_b16_8 t64 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t62, t63);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    GM_ST(gm_b16_8, (uint8_t *)&t52 + 16, t64);
                    void * t65 = gm_l0;
                    gm_t88rcucg1b7uk t66 = gm_l8;
                    gm_b16_8 t67 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t65, t66);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    GM_ST(gm_b16_8, (uint8_t *)&t52 + 32, t67);
                    gm_tabb710d9mq2e t68;
                    gm_f_format_sfq90nb((void *)&t68, (void *)NULL, cx, NULL, &t52);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    if (((((uint8_t *)&t68))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t68) + 8, gm_srch2l5nl06vsci, 843, 28);
                    gm_b16_8 t69 = GM_LD(gm_b16_8, (((uint8_t *)&t68) + 8));
                    gm_b16_8 t70 = GM_LD(gm_b16_8, ((uint8_t *)&t69));
                    gm_b0_1 t71 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t50, t51, t70);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                {
                    void * t72 = gm_l0;
                    void * t73 = GM_LD(void *, (((uint8_t *)t72) + 0));
                    gm_tl874t2atnlh3 t74 = gm_l4;
                    void * t75 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t73, t74);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t76 = 0;
                    F.ret = t75;
                    goto gm_fret;
                }
                goto ix42;
ie42:;
ix42:;
            }
            {
                void * t77 = gm_l5;
                gm_t88rcucg1b7uk t78 = gm_l10;
                gm_t88rcucg1b7uk t79 = gm_f_many_ptr_l4v11f8((void *)NULL, cx, NULL, t77, ((uint8_t)1), t78);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l12 = t79;
            }
            {
                void * t80 = gm_l0;
                void * t81 = gm_l2;
                gm_t88rcucg1b7uk t82 = gm_l12;
                void * t83 = gm_f_coerce_sudc6c1((void *)NULL, cx, NULL, t80, t81, t82);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l13 = t83;
            }
            {
                void * t84 = gm_l0;
                void * t85 = gm_l7;
                gm_t88rcucg1b7uk t86 = gm_l12;
                void * t87 = gm_f_coerce_sudc6c1((void *)NULL, cx, NULL, t84, t85, t86);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l14 = t87;
            }
            {
            }
            {
                gm_tvdg94bk7aa22 t88;
                GM_ST(uint8_t, (uint8_t *)&t88 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t88 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t88 + 4, 0);
                GM_ST(uint32_t, (uint8_t *)&t88 + 8, 0);
                GM_ST(uint8_t, ((uint8_t *)&t88 + 0), ((uint8_t)12ULL));
                void * t89 = gm_l5;
                gm_t53at80pfdus3 t90;
                memset(&t90, 0, sizeof t90);
                GM_ST(uint8_t, ((uint8_t *)&t90 + 0), ((uint8_t)2ULL));
                gm_t88rcucg1b7uk t91 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t89, &t90);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                {
                    void * t93 = gm_l13;
                    void * t94 = gm_l14;
                    gm_t5fru0lp11n7t t95;
                    GM_ST(void *, ((uint8_t *)&t95 + 0), t93);
                    GM_ST(void *, ((uint8_t *)&t95 + 8), t94);
                    gm_l15 = t95;
                }
                void * t96 = ((uint8_t *)&gm_l15);
                if ((uintptr_t)0 > (uintptr_t)2 || (uintptr_t)2 > (uintptr_t)2) gm_trap_slice((uintptr_t)0, (uintptr_t)2, 2, gm_srch2l5nl06vsci, 850, 54);
                gm_b16_8 t97;
                GM_ST(void *, (uint8_t *)&t97, (void *)(((uint8_t *)t96 + (uintptr_t)0 * 8)));
                GM_ST(uintptr_t, (uint8_t *)&t97 + 8, (uintptr_t)((uintptr_t)2 - (uintptr_t)0));
                gm_b16_8 t98 = t97;
                gm_tl874t2atnlh3 t99 = gm_l4;
                void * t100 = gm_f_builtin_a7cjd7d((void *)NULL, cx, NULL, t88, t91, t98, t99);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t101 = 0;
                F.ret = t100;
                goto gm_fret;
            }
            goto ix35;
ie35:;
            {
                void * t103 = gm_l0;
                void * t104 = GM_LD(void *, (((uint8_t *)t103) + 0));
                gm_tl874t2atnlh3 t105 = gm_l4;
                void * t106 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t104, t105);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t107 = 0;
                F.ret = t106;
                goto gm_fret;
            }
ix35:;
        }
        goto ix28;
ie28:;
ix28:;
    }
    {
        void * t109 = gm_l5;
        gm_t88rcucg1b7uk t110 = gm_l8;
        gm_t53at80pfdus3 t111;
        gm_f_kind_jcnt4hm((void *)&t111, t109, GM_CONV(gm_csguankko4lmhd, t110));
        uint8_t t112;
        uint8_t t115 = GM_LD(uint8_t, (((uint8_t *)&t111) + 0));
        if (t115 != ((uint8_t)23ULL)) goto mn114;
        t112 = ((uint8_t)1);
        goto me113;
mn114:;
        t112 = ((uint8_t)0);
        goto me113;
mn116:;
        gm_trap("no match arm matched the scrutinee", gm_srch2l5nl06vsci, 852, 8);
me113:;
        if (!t112) goto ie108;
        {
            {
                void * t119 = gm_l0;
                gm_t88rcucg1b7uk t120 = gm_l8;
                void * t121 = gm_l5;
                gm_t88rcucg1b7uk t122 = gm_f_usize_ty_qnrd30f((void *)NULL, cx, NULL, t121);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tl874t2atnlh3 t123 = gm_l4;
                gm_b16_8 t124;
                GM_ST(void *, (uint8_t *)&t124, (void *)(gm_sfeo7g7huf2q1));
                GM_ST(uintptr_t, (uint8_t *)&t124 + 8, (uintptr_t)(16));
                gm_t88rcucg1b7uk t125 = gm_f_unify_operands_5sp7j1j((void *)NULL, cx, NULL, t119, t120, t122, t123, t124);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
        }
        goto ix108;
ie108:;
        gm_t88rcucg1b7uk t127 = gm_l8;
        void * t128 = gm_l5;
        gm_t88rcucg1b7uk t129 = gm_f_usize_ty_qnrd30f((void *)NULL, cx, NULL, t128);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t130 = (!gm_eq9m6mvtmgde(((uint8_t *)&t127), ((uint8_t *)&t129)));
        if (!t130) goto ie126;
        gm_t88rcucg1b7uk t131 = gm_l8;
        void * t132 = gm_l5;
        gm_t53at80pfdus3 t133;
        memset(&t133, 0, sizeof t133);
        GM_ST(uint8_t, ((uint8_t *)&t133 + 0), ((uint8_t)2ULL));
        gm_t88rcucg1b7uk t134 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t132, &t133);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t135 = (!gm_eq9m6mvtmgde(((uint8_t *)&t131), ((uint8_t *)&t134)));
        if (!t135) goto ie126;
        void * t136 = gm_l5;
        gm_t88rcucg1b7uk t137 = gm_l8;
        uint8_t t138 = gm_f_is_error_8k27gd1(t136, GM_CONV(gm_csguankko4lmhd, t137));
        uint8_t t139 = (uint8_t)!t138;
        if (!t139) goto ie126;
        {
            void * t141 = gm_l0;
            void * t142 = gm_l7;
            gm_tl874t2atnlh3 t143 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t142) + 72));
            void * t144 = gm_l0;
            gm_t88rcucg1b7uk t145 = gm_l8;
            gm_b16_8 t146 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t144, t145);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tnpifv20irnph t147;
            GM_ST(gm_b16_8, ((uint8_t *)&t147 + 0), t146);
            gm_tabb710d9mq2e t148;
            gm_f_format_33edc64((void *)&t148, (void *)NULL, cx, NULL, t147);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t148))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t148) + 8, gm_srch2l5nl06vsci, 855, 26);
            gm_b16_8 t149 = GM_LD(gm_b16_8, (((uint8_t *)&t148) + 8));
            gm_b16_8 t150 = GM_LD(gm_b16_8, ((uint8_t *)&t149));
            gm_b0_1 t151 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t141, t143, t150);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t152 = gm_l0;
            void * t153 = GM_LD(void *, (((uint8_t *)t152) + 0));
            gm_tl874t2atnlh3 t154 = gm_l4;
            void * t155 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t153, t154);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t156 = 0;
            F.ret = t155;
            goto gm_fret;
        }
        goto ix126;
ie126:;
ix126:;
ix108:;
    }
    {
    }
    {
        gm_tvdg94bk7aa22 t157;
        uint8_t t159 = gm_l9;
        if (!t159) goto ie158;
        gm_tvdg94bk7aa22 t161;
        GM_ST(uint8_t, (uint8_t *)&t161 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t161 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t161 + 4, 0);
        GM_ST(uint32_t, (uint8_t *)&t161 + 8, 0);
        GM_ST(uint8_t, ((uint8_t *)&t161 + 0), ((uint8_t)11ULL));
        gm_tvdg94bk7aa22 t162 = t161;
        t157 = t162;
        goto ix158;
ie158:;
        gm_tvdg94bk7aa22 t164;
        GM_ST(uint8_t, (uint8_t *)&t164 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t164 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t164 + 4, 0);
        GM_ST(uint32_t, (uint8_t *)&t164 + 8, 0);
        GM_ST(uint8_t, ((uint8_t *)&t164 + 0), ((uint8_t)10ULL));
        gm_tvdg94bk7aa22 t165 = t164;
        t157 = t165;
ix158:;
        gm_t88rcucg1b7uk t166 = gm_l6;
        {
            void * t168 = gm_l2;
            void * t169 = gm_l7;
            gm_t5fru0lp11n7t t170;
            GM_ST(void *, ((uint8_t *)&t170 + 0), t168);
            GM_ST(void *, ((uint8_t *)&t170 + 8), t169);
            gm_l16 = t170;
        }
        void * t171 = ((uint8_t *)&gm_l16);
        if ((uintptr_t)0 > (uintptr_t)2 || (uintptr_t)2 > (uintptr_t)2) gm_trap_slice((uintptr_t)0, (uintptr_t)2, 2, gm_srch2l5nl06vsci, 858, 83);
        gm_b16_8 t172;
        GM_ST(void *, (uint8_t *)&t172, (void *)(((uint8_t *)t171 + (uintptr_t)0 * 8)));
        GM_ST(uintptr_t, (uint8_t *)&t172 + 8, (uintptr_t)((uintptr_t)2 - (uintptr_t)0));
        gm_b16_8 t173 = t172;
        gm_tl874t2atnlh3 t174 = gm_l4;
        void * t175 = gm_f_builtin_a7cjd7d((void *)NULL, cx, NULL, t157, t166, t173, t174);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t176 = 0;
        F.ret = t175;
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
inline __attribute__((aligned(4))) uint8_t gm_f_prefix_on_call_f7od3q2(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_prefix_on_call_f7od3q2 F;
    void * gm_l0; /* e */
    void * gm_l1; /* inner */
    void * gm_l2; /* inner */
    void * gm_l3; /* inner */
    void * gm_l4; /* inner */
    GM_STACK_CHECK(&F, 98u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    uint8_t t2;
    uint8_t t8 = GM_LD(uint8_t, ((((uint8_t *)t1) + 8) + 0));
    switch (t8) { case ((uint8_t)0ULL): goto ma6; case ((uint8_t)1ULL): goto ma6; case ((uint8_t)2ULL): goto ma6; case ((uint8_t)3ULL): goto ma6; case ((uint8_t)4ULL): goto ma6; case ((uint8_t)5ULL): goto ma6; case ((uint8_t)6ULL): goto ma6; case ((uint8_t)7ULL): goto ma6; case ((uint8_t)8ULL): goto ma6; case ((uint8_t)9ULL): goto ma6; case ((uint8_t)10ULL): goto ma6; case ((uint8_t)11ULL): goto ma6; case ((uint8_t)12ULL): goto ma6; case ((uint8_t)13ULL): goto ma6; case ((uint8_t)14ULL): goto ma6; case ((uint8_t)15ULL): goto ma6; case ((uint8_t)16ULL): goto ma6; case ((uint8_t)17ULL): goto ma6; case ((uint8_t)18ULL): goto ma6; case ((uint8_t)19ULL): goto ma4; case ((uint8_t)20ULL): goto ma6; case ((uint8_t)21ULL): goto ma6; case ((uint8_t)22ULL): goto ma6; case ((uint8_t)23ULL): goto ma6; case ((uint8_t)24ULL): goto ma5; case ((uint8_t)25ULL): goto ma5; case ((uint8_t)26ULL): goto ma5; case ((uint8_t)27ULL): goto ma5; case ((uint8_t)28ULL): goto ma6; case ((uint8_t)29ULL): goto ma6; case ((uint8_t)30ULL): goto ma6; case ((uint8_t)31ULL): goto ma6; case ((uint8_t)32ULL): goto ma6; case ((uint8_t)33ULL): goto ma6; case ((uint8_t)34ULL): goto ma6; case ((uint8_t)35ULL): goto ma6; case ((uint8_t)36ULL): goto ma6; case ((uint8_t)37ULL): goto ma6; case ((uint8_t)38ULL): goto ma6; case ((uint8_t)39ULL): goto ma6; case ((uint8_t)40ULL): goto ma6; case ((uint8_t)41ULL): goto ma6; case ((uint8_t)42ULL): goto ma6; case ((uint8_t)43ULL): goto ma6; case ((uint8_t)44ULL): goto ma6; case ((uint8_t)45ULL): goto ma6; case ((uint8_t)46ULL): goto ma6; case ((uint8_t)47ULL): goto ma6; case ((uint8_t)48ULL): goto ma6; case ((uint8_t)49ULL): goto ma6; case ((uint8_t)50ULL): goto ma6; default: goto ma6; }
ma4:;
    uint8_t t10 = GM_LD(uint8_t, ((((uint8_t *)t1) + 8) + 0));
    if (t10 != ((uint8_t)19ULL)) goto mn9;
    t2 = ((uint8_t)1);
    goto me3;
mn9:;
ma5:;
    uint8_t t14 = GM_LD(uint8_t, ((((uint8_t *)t1) + 8) + 0));
    if (t14 != ((uint8_t)24ULL)) goto pa13;
    void * t15 = GM_LD(void *, ((((uint8_t *)t1) + 8) + 8));
    gm_l1 = t15;
    goto po12;
pa13:;
    uint8_t t17 = GM_LD(uint8_t, ((((uint8_t *)t1) + 8) + 0));
    if (t17 != ((uint8_t)25ULL)) goto pa16;
    void * t18 = GM_LD(void *, (((((uint8_t *)t1) + 8) + 8) + 8));
    gm_l1 = t18;
    goto po12;
pa16:;
    uint8_t t20 = GM_LD(uint8_t, ((((uint8_t *)t1) + 8) + 0));
    if (t20 != ((uint8_t)26ULL)) goto pa19;
    void * t21 = GM_LD(void *, (((((uint8_t *)t1) + 8) + 8) + 8));
    gm_l1 = t21;
    goto po12;
pa19:;
    uint8_t t23 = GM_LD(uint8_t, ((((uint8_t *)t1) + 8) + 0));
    if (t23 != ((uint8_t)27ULL)) goto pa22;
    void * t24 = GM_LD(void *, (((((uint8_t *)t1) + 8) + 8) + 8));
    gm_l1 = t24;
    goto po12;
pa22:;
    goto mn11;
po12:;
    void * t25 = gm_l1;
    uint8_t t26 = gm_f_prefix_on_call_f7od3q2(t25);
    t2 = t26;
    goto me3;
mn11:;
ma6:;
    t2 = ((uint8_t)0);
    goto me3;
mn27:;
ma7:;
    gm_trap("no match arm matched the scrutinee", gm_srch2l5nl06vsci, 153, 53);
me3:;
    F.ret = t2;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) void * gm_f_range_value_b02g2v9(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, void * a2, void * a3, gm_tkn42u7na3t81 a4, gm_tl874t2atnlh3 a5) {
    struct gm_fr_range_value_b02g2v9 F;
    void * gm_l0; /* cx */
    uint8_t gm_l1; /* inclusive */
    void * gm_l2; /* lo */
    void * gm_l3; /* hi */
    gm_tkn42u7na3t81 gm_l4; /* expect */
    gm_tl874t2atnlh3 gm_l5; /* span */
    void * gm_l6; /* tys */
    uint32_t gm_l7; /* template */
    gm_tkn42u7na3t81 gm_l8; /* want */
    gm_t88rcucg1b7uk gm_l9; /* x */
    gm_tbngattl1fo51 gm_l10; /* r */
    void * gm_l11; /* l */
    gm_t88rcucg1b7uk gm_l12; /* w */
    void * gm_l13; /* h */
    gm_t88rcucg1b7uk gm_l14; /* w */
    gm_t88rcucg1b7uk gm_l15; /* elem */
    gm_tnqk2qt7j42o9 gm_l16; /* <temporary array> */
    gm_t88rcucg1b7uk gm_l17; /* ty */
    GM_STACK_CHECK(&F, 1190u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_l5 = a5;
    {
        void * t2 = gm_l0;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 0));
        void * t4 = (void *)(((uint8_t *)t3) + 8);
        gm_l6 = t4;
    }
    {
        void * t5 = gm_l0;
        void * t6 = GM_LD(void *, (((uint8_t *)t5) + 0));
        uint8_t t7 = gm_l1;
        gm_t9805qtonbsag t8 = gm_f_range_template_dbt7218((void *)NULL, cx, NULL, t6, t7);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint32_t t9;
        if (((((uint8_t *)&t8))[4] != 0)) {
            uint32_t t10 = GM_LD(uint32_t, (((uint8_t *)&t8) + 0));
            t9 = t10;
        } else {
            {
                void * t12 = gm_l0;
                gm_tl874t2atnlh3 t13 = gm_l5;
                gm_b16_8 t14;
                GM_ST(void *, (uint8_t *)&t14, (void *)(gm_sgjci31f2rs96));
                GM_ST(uintptr_t, (uint8_t *)&t14 + 8, (uintptr_t)(48));
                gm_b0_1 t15 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t12, t13, t14);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            void * t16 = gm_l0;
            void * t17 = GM_LD(void *, (((uint8_t *)t16) + 0));
            gm_tl874t2atnlh3 t18 = gm_l5;
            void * t19 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t17, t18);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t20 = 0;
            F.ret = t19;
            goto gm_fret;
        }
        gm_l7 = t9;
    }
    {
        gm_tkn42u7na3t81 t21;
        memset(&t21, 0, sizeof t21);
        gm_l8 = t21;
    }
    {
        if (!((((uint8_t *)&gm_l4))[4] != 0)) goto ie22;
        gm_t88rcucg1b7uk t23 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l4));
        gm_l9 = t23;
        {
            void * t26 = gm_l0;
            void * t27 = GM_LD(void *, (((uint8_t *)t26) + 0));
            void * t28 = gm_l0;
            void * t29 = (void *)(((uint8_t *)t28) + 96);
            void * t30 = gm_l0;
            void * t31 = GM_LD(void *, (((uint8_t *)t30) + 0));
            gm_t88rcucg1b7uk t32 = gm_l9;
            gm_t88rcucg1b7uk t33 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t29, t31, t32);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_thetuhtouve7f t34 = gm_f_range_of_dmmo768((void *)NULL, cx, NULL, t27, t33);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (!((((uint8_t *)&t34))[8] != 0)) goto ie25;
            gm_tbngattl1fo51 t35 = GM_LD(gm_tbngattl1fo51, ((uint8_t *)&t34));
            gm_l10 = t35;
            {
                uint8_t t38 = GM_LD(uint8_t, (((uint8_t *)&gm_l10) + 0));
                uint8_t t39 = gm_l1;
                uint8_t t40 = ((t38 == t39));
                if (!t40) goto ie37;
                {
                    gm_t88rcucg1b7uk t42 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l10) + 4));
                    gm_tkn42u7na3t81 t43;
                    GM_ST(uint8_t, (uint8_t *)&t43 + 5, 0);
                    GM_ST(uint16_t, (uint8_t *)&t43 + 6, 0);
                    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t43), t42);
                    ((uint8_t *)&t43)[4] = 1;
                    gm_l8 = t43;
                }
                goto ix37;
ie37:;
ix37:;
            }
            goto ix25;
ie25:;
ix25:;
        }
        goto ix22;
ie22:;
ix22:;
    }
    {
        void * t44 = gm_l0;
        void * t45 = gm_l2;
        gm_tkn42u7na3t81 t46 = gm_l8;
        void * t47 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t44, t45, t46);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l11 = t47;
    }
    {
        void * t48 = gm_l0;
        void * t49 = gm_l3;
        gm_tkn42u7na3t81 t50;
        if (!((((uint8_t *)&gm_l8))[4] != 0)) goto ie51;
        gm_t88rcucg1b7uk t52 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l8));
        gm_l12 = t52;
        gm_t88rcucg1b7uk t54 = gm_l12;
        gm_t88rcucg1b7uk t55 = t54;
        gm_tkn42u7na3t81 t56;
        GM_ST(uint8_t, (uint8_t *)&t56 + 5, 0);
        GM_ST(uint16_t, (uint8_t *)&t56 + 6, 0);
        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t56), t55);
        ((uint8_t *)&t56)[4] = 1;
        t50 = t56;
        goto ix51;
ie51:;
        void * t58 = gm_l11;
        gm_t88rcucg1b7uk t59 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t58) + 0));
        gm_t88rcucg1b7uk t60 = t59;
        gm_tkn42u7na3t81 t61;
        GM_ST(uint8_t, (uint8_t *)&t61 + 5, 0);
        GM_ST(uint16_t, (uint8_t *)&t61 + 6, 0);
        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t61), t60);
        ((uint8_t *)&t61)[4] = 1;
        t50 = t61;
ix51:;
        void * t62 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t48, t49, t50);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l13 = t62;
    }
    {
        gm_t88rcucg1b7uk t63;
        if (!((((uint8_t *)&gm_l8))[4] != 0)) goto ie64;
        gm_t88rcucg1b7uk t65 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l8));
        gm_l14 = t65;
        gm_t88rcucg1b7uk t67 = gm_l14;
        gm_t88rcucg1b7uk t68 = t67;
        t63 = t68;
        goto ix64;
ie64:;
        void * t70 = gm_l0;
        void * t71 = gm_l11;
        gm_t88rcucg1b7uk t72 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t71) + 0));
        void * t73 = gm_l13;
        gm_t88rcucg1b7uk t74 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t73) + 0));
        gm_tl874t2atnlh3 t75 = gm_l5;
        gm_b16_8 t76;
        GM_ST(void *, (uint8_t *)&t76, (void *)(gm_sp6ona76fn0di));
        GM_ST(uintptr_t, (uint8_t *)&t76 + 8, (uintptr_t)(12));
        gm_t88rcucg1b7uk t77 = gm_f_unify_operands_5sp7j1j((void *)NULL, cx, NULL, t70, t72, t74, t75, t76);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t88rcucg1b7uk t78 = t77;
        t63 = t78;
ix64:;
        gm_l15 = t63;
    }
    {
        void * t80 = gm_l6;
        void * t81 = gm_l0;
        void * t82 = (void *)(((uint8_t *)t81) + 96);
        void * t83 = gm_l0;
        void * t84 = GM_LD(void *, (((uint8_t *)t83) + 0));
        gm_t88rcucg1b7uk t85 = gm_l15;
        gm_t88rcucg1b7uk t86 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t82, t84, t85);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t87 = gm_f_has_infer_3o82a0b(t80, GM_CONV(gm_csguankko4lmhd, t86));
        if (!t87) goto ie79;
        {
            {
                void * t90 = gm_l0;
                void * t91 = (void *)(((uint8_t *)t90) + 96);
                void * t92 = gm_l6;
                gm_t88rcucg1b7uk t93 = gm_l15;
                void * t94 = gm_l6;
                gm_t88rcucg1b7uk t95 = gm_f_usize_ty_qnrd30f((void *)NULL, cx, NULL, t94);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_t9qkh0rsooc8d t96 = gm_f_unify_t2qjv2p((void *)NULL, cx, NULL, t91, t92, t93, t95);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
        }
        goto ix79;
ie79:;
ix79:;
    }
    {
        void * t97 = gm_l0;
        void * t98 = (void *)(((uint8_t *)t97) + 96);
        void * t99 = gm_l0;
        void * t100 = GM_LD(void *, (((uint8_t *)t99) + 0));
        gm_t88rcucg1b7uk t101 = gm_l15;
        gm_t88rcucg1b7uk t102 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t98, t100, t101);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l15 = t102;
    }
    {
        uint8_t t104 = gm_l1;
        if (!t104) goto ie103;
        {
            void * t106 = gm_l0;
            void * t107 = (void *)(((uint8_t *)t106) + 320);
            gm_t91vm1g0k2rpm t108;
            memset((uint8_t *)&t108 + 34, 0, 6);
            gm_t88rcucg1b7uk t109 = gm_l15;
            GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t108 + 0, t109);
            gm_tl874t2atnlh3 t110 = gm_l5;
            GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t108 + 4, t110);
            gm_b16_8 t111;
            GM_ST(void *, (uint8_t *)&t111, (void *)(gm_svm0rfaqk3b2q));
            GM_ST(uintptr_t, (uint8_t *)&t111 + 8, (uintptr_t)(3));
            GM_ST(gm_b16_8, (uint8_t *)&t108 + 16, t111);
            GM_ST(uint8_t, (uint8_t *)&t108 + 32, ((uint8_t)1));
            GM_ST(uint8_t, (uint8_t *)&t108 + 33, ((uint8_t)0));
            gm_tpm220j5qhjke t112;
            GM_ST(uint8_t, (uint8_t *)&t112 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t112 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t112 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t112 + 0), ((uint8_t)3ULL));
            GM_ST(gm_t91vm1g0k2rpm, ((uint8_t *)&t112 + 8), t108);
            gm_tftb1odc739t5 t113 = gm_f_push_7koe9lk(t107, &t112);
            if (((((uint8_t *)&t113))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t113) + 1, gm_srch2l5nl06vsci, 820, 9);
            gm_b0_1 t114 = GM_LD(gm_b0_1, (((uint8_t *)&t113) + 1));
        }
        goto ix103;
ie103:;
        {
            void * t116 = gm_l0;
            void * t117 = (void *)(((uint8_t *)t116) + 320);
            gm_t88rcucg1b7uk t118 = gm_l15;
            gm_tl874t2atnlh3 t119 = gm_l5;
            gm_t2i8ne5ibd7v4 t120;
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t120 + 0), t118);
            GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t120 + 4), t119);
            gm_tpm220j5qhjke t121;
            GM_ST(uint8_t, (uint8_t *)&t121 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t121 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t121 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t121 + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&t121 + 32, 0);
            GM_ST(uint64_t, (uint8_t *)&t121 + 40, 0);
            GM_ST(uint8_t, ((uint8_t *)&t121 + 0), ((uint8_t)2ULL));
            GM_ST(gm_t2i8ne5ibd7v4, ((uint8_t *)&t121 + 8), t120);
            gm_tftb1odc739t5 t122 = gm_f_push_7koe9lk(t117, &t121);
            if (((((uint8_t *)&t122))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t122) + 1, gm_srch2l5nl06vsci, 822, 9);
            gm_b0_1 t123 = GM_LD(gm_b0_1, (((uint8_t *)&t122) + 1));
        }
ix103:;
    }
    {
        void * t124 = gm_l0;
        void * t125 = gm_l11;
        gm_t88rcucg1b7uk t126 = gm_l15;
        void * t127 = gm_f_coerce_sudc6c1((void *)NULL, cx, NULL, t124, t125, t126);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l11 = t127;
    }
    {
        void * t128 = gm_l0;
        void * t129 = gm_l13;
        gm_t88rcucg1b7uk t130 = gm_l15;
        void * t131 = gm_f_coerce_sudc6c1((void *)NULL, cx, NULL, t128, t129, t130);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l13 = t131;
    }
    {
    }
    {
        void * t132 = gm_l0;
        void * t133 = GM_LD(void *, (((uint8_t *)t132) + 0));
        uint32_t t134 = gm_l7;
        {
            gm_t88rcucg1b7uk t136 = gm_l15;
            gm_tnqk2qt7j42o9 t137;
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t137 + 0), t136);
            gm_l16 = t137;
        }
        void * t138 = ((uint8_t *)&gm_l16);
        if ((uintptr_t)0 > (uintptr_t)1 || (uintptr_t)1 > (uintptr_t)1) gm_trap_slice((uintptr_t)0, (uintptr_t)1, 1, gm_srch2l5nl06vsci, 826, 42);
        gm_b16_8 t139;
        GM_ST(void *, (uint8_t *)&t139, (void *)(((uint8_t *)t138 + (uintptr_t)0 * 4)));
        GM_ST(uintptr_t, (uint8_t *)&t139 + 8, (uintptr_t)((uintptr_t)1 - (uintptr_t)0));
        gm_b16_8 t140 = t139;
        gm_t88rcucg1b7uk t141 = gm_f_instantiate_adt_lbjcui1((void *)NULL, cx, NULL, t133, t134, t140);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l17 = t141;
    }
    {
        gm_f_list_vp1djv1_2((void *)&F.l18, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t142 = (void *)((uint8_t *)&F.l18);
        void * t143 = gm_l11;
        gm_tftb1odc739t5 t144 = gm_f_push_d943vgp_2(t142, &(*(gm_t69m56meqefs8 *)((uint8_t *)t143)));
        if (((((uint8_t *)&t144))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t144) + 1, gm_srch2l5nl06vsci, 828, 5);
        gm_b0_1 t145 = GM_LD(gm_b0_1, (((uint8_t *)&t144) + 1));
    }
    {
        void * t146 = (void *)((uint8_t *)&F.l18);
        void * t147 = gm_l13;
        gm_tftb1odc739t5 t148 = gm_f_push_d943vgp_2(t146, &(*(gm_t69m56meqefs8 *)((uint8_t *)t147)));
        if (((((uint8_t *)&t148))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t148) + 1, gm_srch2l5nl06vsci, 829, 5);
        gm_b0_1 t149 = GM_LD(gm_b0_1, (((uint8_t *)&t148) + 1));
    }
    {
        gm_t88rcucg1b7uk t150 = gm_l17;
        gm_b16_8 t151 = GM_LD(gm_b16_8, (((uint8_t *)&F.l18) + 0));
        gm_teti411eujvf4 t152;
        memset(&t152, 0, sizeof t152);
        GM_ST(uint8_t, ((uint8_t *)&t152 + 0), ((uint8_t)35ULL));
        GM_ST(gm_b16_8, ((uint8_t *)&t152 + 8), t151);
        gm_tl874t2atnlh3 t153 = gm_l5;
        void * t154 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t150, &t152, t153);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t155 = 0;
        F.ret = t154;
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
inline __attribute__((aligned(4))) uint8_t gm_f_reject_second_class_c561fm5(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_b16_8 a2) {
    struct gm_fr_reject_second_class_c561fm5 F;
    void * gm_l0; /* cx */
    void * gm_l1; /* v */
    gm_b16_8 gm_l2; /* what */
    GM_STACK_CHECK(&F, 399u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 0));
        void * t4 = (void *)(((uint8_t *)t3) + 8);
        void * t5 = gm_l0;
        void * t6 = (void *)(((uint8_t *)t5) + 96);
        void * t7 = gm_l0;
        void * t8 = GM_LD(void *, (((uint8_t *)t7) + 0));
        void * t9 = gm_l1;
        gm_t88rcucg1b7uk t10 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t9) + 0));
        gm_t88rcucg1b7uk t11 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t6, t8, t10);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t53at80pfdus3 t12;
        gm_f_kind_jcnt4hm((void *)&t12, t4, GM_CONV(gm_csguankko4lmhd, t11));
        uint8_t t18 = GM_LD(uint8_t, (((uint8_t *)&t12) + 0));
        switch (t18) { case ((uint8_t)0ULL): goto ma16; case ((uint8_t)1ULL): goto ma16; case ((uint8_t)2ULL): goto ma16; case ((uint8_t)3ULL): goto ma16; case ((uint8_t)4ULL): goto ma16; case ((uint8_t)5ULL): goto ma16; case ((uint8_t)6ULL): goto ma16; case ((uint8_t)7ULL): goto ma16; case ((uint8_t)8ULL): goto ma16; case ((uint8_t)9ULL): goto ma16; case ((uint8_t)10ULL): goto ma16; case ((uint8_t)11ULL): goto ma16; case ((uint8_t)12ULL): goto ma16; case ((uint8_t)13ULL): goto ma16; case ((uint8_t)14ULL): goto ma16; case ((uint8_t)15ULL): goto ma14; case ((uint8_t)16ULL): goto ma16; case ((uint8_t)17ULL): goto ma16; case ((uint8_t)18ULL): goto ma16; case ((uint8_t)19ULL): goto ma16; case ((uint8_t)20ULL): goto ma16; case ((uint8_t)21ULL): goto ma16; case ((uint8_t)22ULL): goto ma16; case ((uint8_t)23ULL): goto ma16; case ((uint8_t)24ULL): goto ma16; case ((uint8_t)25ULL): goto ma15; case ((uint8_t)26ULL): goto ma16; default: goto ma16; }
ma14:;
        uint8_t t20 = GM_LD(uint8_t, (((uint8_t *)&t12) + 0));
        if (t20 != ((uint8_t)15ULL)) goto mn19;
        void * t21 = gm_l0;
        void * t22 = gm_l1;
        gm_tl874t2atnlh3 t23 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t22) + 72));
        gm_b16_8 t24 = gm_l2;
        gm_tnpifv20irnph t25;
        GM_ST(gm_b16_8, ((uint8_t *)&t25 + 0), t24);
        gm_tabb710d9mq2e t26;
        gm_f_format_io9sb4j((void *)&t26, (void *)NULL, cx, NULL, t25);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t26))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t26) + 8, gm_srch2l5nl06vsci, 591, 36);
        gm_b16_8 t27 = GM_LD(gm_b16_8, (((uint8_t *)&t26) + 8));
        gm_b16_8 t28 = GM_LD(gm_b16_8, ((uint8_t *)&t27));
        gm_b0_1 t29 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t21, t23, t28);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me13;
mn19:;
ma15:;
        uint8_t t31 = GM_LD(uint8_t, (((uint8_t *)&t12) + 0));
        if (t31 != ((uint8_t)25ULL)) goto mn30;
        void * t32 = gm_l0;
        void * t33 = gm_l1;
        gm_tl874t2atnlh3 t34 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t33) + 72));
        gm_b16_8 t35 = gm_l2;
        gm_tnpifv20irnph t36;
        GM_ST(gm_b16_8, ((uint8_t *)&t36 + 0), t35);
        gm_tabb710d9mq2e t37;
        gm_f_format_aqpcar1((void *)&t37, (void *)NULL, cx, NULL, t36);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t37))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t37) + 8, gm_srch2l5nl06vsci, 592, 41);
        gm_b16_8 t38 = GM_LD(gm_b16_8, (((uint8_t *)&t37) + 8));
        gm_b16_8 t39 = GM_LD(gm_b16_8, ((uint8_t *)&t38));
        gm_b0_1 t40 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t32, t34, t39);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me13;
mn30:;
ma16:;
        uint8_t t42 = 0;
        F.ret = ((uint8_t)0);
        goto gm_fret;
        goto me13;
mn41:;
ma17:;
        gm_trap("no match arm matched the scrutinee", gm_srch2l5nl06vsci, 590, 5);
me13:;
    }
    {
        uint8_t t43 = 0;
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_resolve_ty_uk6c1lp(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_resolve_ty_uk6c1lp F;
    void * gm_l0; /* cx */
    void * gm_l1; /* e */
    gm_b16_8 gm_l3; /* args */
    uint64_t gm_l5; /* performed */
    void * gm_l6; /* v */
    GM_STACK_CHECK(&F, 487u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t3 = gm_l1;
        uint8_t t4 = GM_LD(uint8_t, ((((uint8_t *)t3) + 8) + 0));
        if (t4 != ((uint8_t)49ULL)) goto ie2;
        gm_tet9guhev5r4u t5 = GM_LD(gm_tet9guhev5r4u, ((((uint8_t *)t3) + 8) + 8));
        F.l2 = t5;
        uint8_t t6 = (((GM_LD(uintptr_t, ((((uint8_t *)&F.l2) + 0) + 0) + 8) == 7) && memcmp(GM_LD(void *, ((((uint8_t *)&F.l2) + 0) + 0)), gm_slbgk65sat4bm, 7) == 0));
        if (!t6) goto ie2;
        {
            if (!(((((uint8_t *)&F.l2) + 32))[16] != 0)) goto ie8;
            gm_b16_8 t9 = GM_LD(gm_b16_8, (((uint8_t *)&F.l2) + 32));
            gm_l3 = t9;
            uint64_t t10 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
            uint8_t t11 = ((t10 == ((uint64_t)1ULL)));
            if (!t11) goto ie8;
            uint64_t t12 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t13 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
            if (t12 >= (uintptr_t)(t13)) gm_trap_index(t12, t13, gm_srch2l5nl06vsci, 191, 67);
            uint8_t t14 = GM_LD(uint8_t, ((((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t12 * 144) + 0) + 0));
            if (t14 != ((uint8_t)0ULL)) goto ie8;
            gm_tc3oht4ksth9d t15 = GM_LD(gm_tc3oht4ksth9d, ((((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t12 * 144) + 0) + 8));
            F.l4 = t15;
            {
                void * t17 = gm_l0;
                uint64_t t18 = GM_LD(uintptr_t, ((((uint8_t *)t17) + 192) + 0) + 8);
                gm_l5 = t18;
            }
            {
                void * t19 = gm_l0;
                void * t20 = (void *)((uint8_t *)&F.l4);
                gm_tkn42u7na3t81 t21;
                memset(&t21, 0, sizeof t21);
                void * t22 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t19, t20, t21);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l6 = t22;
            }
            {
                void * t23 = gm_l0;
                void * t24 = (void *)(((uint8_t *)t23) + 192);
                uint64_t t25 = gm_l5;
                gm_f_truncate_ua7l21h(t24, t25);
            }
            {
                void * t26 = gm_l0;
                void * t27 = gm_l6;
                gm_t88rcucg1b7uk t28 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t27) + 0));
                void * t29 = gm_l1;
                gm_tl874t2atnlh3 t30 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t29) + 72));
                gm_t88rcucg1b7uk t31 = gm_f_settle_h9ikvs5((void *)NULL, cx, NULL, t26, t28, t30);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t32 = 0;
                F.ret = t31;
                goto gm_fret;
            }
            goto ix8;
ie8:;
ix8:;
        }
        {
            void * t33 = gm_l0;
            void * t34 = gm_l1;
            gm_tl874t2atnlh3 t35 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t34) + 72));
            gm_b16_8 t36;
            GM_ST(void *, (uint8_t *)&t36, (void *)(gm_sfpopfqbubj89));
            GM_ST(uintptr_t, (uint8_t *)&t36 + 8, (uintptr_t)(34));
            gm_b0_1 t37 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t33, t35, t36);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t38 = gm_l0;
            void * t39 = GM_LD(void *, (((uint8_t *)t38) + 0));
            void * t40 = (void *)(((uint8_t *)t39) + 8);
            gm_t88rcucg1b7uk t41 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t40);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t42 = 0;
            F.ret = t41;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t43 = gm_l0;
        void * t44 = GM_LD(void *, (((uint8_t *)t43) + 0));
        void * t45 = gm_l0;
        uint64_t t46 = GM_LD(uint64_t, (((uint8_t *)t45) + 8));
        void * t47 = gm_l1;
        gm_t88rcucg1b7uk t48 = gm_f_resolve_type_jjodbei((void *)NULL, cx, NULL, t44, t46, t47);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t49 = 0;
        F.ret = t48;
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
__attribute__((aligned(4))) void * gm_f_spread_tuple_6180uqs(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_tkn42u7na3t81 a2, gm_tl874t2atnlh3 a3) {
    struct gm_fr_spread_tuple_6180uqs F;
    void * gm_l0; /* cx */
    gm_b16_8 gm_l1; /* elems */
    gm_tkn42u7na3t81 gm_l2; /* want */
    gm_tl874t2atnlh3 gm_l3; /* span */
    void * gm_l4; /* tys */
    gm_b16_8 gm_l5; /* want_elems */
    gm_b16_8 gm_l6; /* want_names */
    gm_t88rcucg1b7uk gm_l7; /* w */
    gm_b16_8 gm_l8; /* es */
    gm_b16_8 gm_l9; /* ns */
    gm_b16_8 gm_l13; /* <tmp> */
    uint64_t gm_l14; /* <tmp> */
    gm_b16_8 gm_l16; /* ps */
    uint8_t gm_l17; /* reported */
    gm_b16_8 gm_l18; /* <tmp> */
    uint64_t gm_l19; /* <tmp> */
    gm_tpouv9r7lrri2 gm_l20; /* p */
    gm_b16_8 gm_l21; /* n */
    gm_tkn42u7na3t81 gm_l22; /* exp */
    gm_tl874t2atnlh3 gm_l23; /* at */
    gm_t08olq97rscn3 gm_l24; /* n */
    uint64_t gm_l25; /* i */
    gm_t88rcucg1b7uk gm_l26; /* t */
    void * gm_l27; /* v */
    uint64_t gm_l28; /* i */
    void * gm_l29; /* v */
    gm_t08olq97rscn3 gm_l30; /* n */
    uint8_t gm_l31; /* named */
    gm_b16_8 gm_l32; /* <tmp> */
    uint64_t gm_l33; /* <tmp> */
    uint8_t gm_l35; /* fits */
    gm_b16_8 gm_l36; /* <tmp> */
    uint64_t gm_l37; /* <tmp> */
    uint64_t gm_l38; /* i */
    gm_b16_8 gm_l43; /* <tmp> */
    uint64_t gm_l44; /* <tmp> */
    uint64_t gm_l45; /* i */
    void * gm_l47; /* v */
    gm_b16_8 gm_l48; /* n */
    gm_t88rcucg1b7uk gm_l49; /* ty */
    void * gm_l50; /* agg */
    GM_STACK_CHECK(&F, 3629u);
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
        void * t4 = (void *)(((uint8_t *)t3) + 8);
        gm_l4 = t4;
    }
    {
        gm_b16_8 t5 = gm_f_empty_slice_rcd2jln();
        gm_b16_8 t6 = GM_LD(gm_b16_8, ((uint8_t *)&t5));
        gm_l5 = t6;
    }
    {
        gm_b16_8 t7 = gm_f_empty_slice_5nn7ph0();
        gm_b16_8 t8 = GM_LD(gm_b16_8, ((uint8_t *)&t7));
        gm_l6 = t8;
    }
    {
        if (!((((uint8_t *)&gm_l2))[4] != 0)) goto ie9;
        gm_t88rcucg1b7uk t10 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l2));
        gm_l7 = t10;
        {
            void * t13 = gm_l4;
            gm_t88rcucg1b7uk t14 = gm_l7;
            gm_t53at80pfdus3 t15;
            gm_f_kind_jcnt4hm((void *)&t15, t13, GM_CONV(gm_csguankko4lmhd, t14));
            uint8_t t16 = GM_LD(uint8_t, (((uint8_t *)&t15) + 0));
            if (t16 != ((uint8_t)12ULL)) goto ie12;
            gm_b16_8 t17 = GM_LD(gm_b16_8, ((((uint8_t *)&t15) + 8) + 0));
            gm_l8 = t17;
            gm_b16_8 t18 = GM_LD(gm_b16_8, ((((uint8_t *)&t15) + 8) + 16));
            gm_l9 = t18;
            {
                gm_b16_8 t20 = gm_l8;
                gm_l5 = t20;
            }
            {
                gm_b16_8 t21 = gm_l9;
                gm_l6 = t21;
            }
            goto ix12;
ie12:;
ix12:;
        }
        goto ix9;
ie9:;
ix9:;
    }
    {
        gm_f_list_ir0h3tc_2((void *)&F.l10, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_f_list_5kthfl8((void *)&F.l11, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_f_list_s83jn2q((void *)&F.l12, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t23 = gm_l1;
            gm_l13 = t23;
        }
        {
            gm_l14 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t25 = gm_l14;
                uint64_t t26 = GM_LD(uintptr_t, ((uint8_t *)&gm_l13) + 8);
                uint8_t t27 = (t25 < t26);
                if (!t27) goto lb24;
                {
                    uint64_t t30 = gm_l14;
                    uint64_t t31 = (uintptr_t)t30;
                    uint64_t t32 = GM_LD(uintptr_t, ((uint8_t *)&gm_l13) + 8);
                    gm_tv1o1j44ii4db t35 = GM_LD(gm_tv1o1j44ii4db, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l13)) + t31 * 136));
                    F.l15 = t35;
                    {
                        uint8_t t38 = GM_LD(uint8_t, (((uint8_t *)&F.l15) + 128));
                        if (!t38) goto ie37;
                        {
                            void * t40 = gm_l0;
                            void * t41 = (void *)(((uint8_t *)&F.l15) + 40);
                            void * t42 = (void *)((uint8_t *)&F.l10);
                            gm_th2lqbo77pvsc t43;
                            gm_f_spread_parts_e62q2l2((void *)&t43, (void *)NULL, cx, NULL, t40, t41, t42);
                            if (gm_unw.kind) {
                                goto gm_ll28;
                            }
                            gm_b16_8 t44;
                            if (((((uint8_t *)&t43))[16] != 0)) {
                                gm_b16_8 t45 = GM_LD(gm_b16_8, (((uint8_t *)&t43) + 0));
                                t44 = t45;
                            } else {
                                goto lc24;
                            }
                            gm_l16 = t44;
                        }
                        {
                            gm_l17 = ((uint8_t)0);
                        }
                        {
                            {
                                gm_b16_8 t47 = gm_l16;
                                gm_l18 = t47;
                            }
                            {
                                gm_l19 = ((uint64_t)0ULL);
                            }
                            {
                                for (;;) {
                                    uint64_t t49 = gm_l19;
                                    uint64_t t50 = GM_LD(uintptr_t, ((uint8_t *)&gm_l18) + 8);
                                    uint8_t t51 = (t49 < t50);
                                    if (!t51) goto lb48;
                                    {
                                        uint64_t t54 = gm_l19;
                                        uint64_t t55 = (uintptr_t)t54;
                                        uint64_t t56 = GM_LD(uintptr_t, ((uint8_t *)&gm_l18) + 8);
                                        gm_tpouv9r7lrri2 t59 = GM_LD(gm_tpouv9r7lrri2, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l18)) + t55 * 32));
                                        gm_l20 = t59;
                                        {
                                            if (!(((((uint8_t *)&gm_l20) + 0))[16] != 0)) goto ie61;
                                            gm_b16_8 t62 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l20) + 0));
                                            gm_l21 = t62;
                                            {
                                                gm_b16_8 t65 = GM_LD(gm_b16_8, (((uint8_t *)&F.l11) + 0));
                                                gm_b16_8 t66 = GM_LD(gm_b16_8, ((uint8_t *)&t65));
                                                gm_b16_8 t67 = gm_l21;
                                                gm_tf5srh4mp4b3i t68 = gm_f_index_of_part_7kb7erm(t66, t67);
                                                uint8_t t69;
                                                if (((((uint8_t *)&t68))[8] != 0)) goto mn71;
                                                t69 = ((uint8_t)0);
                                                goto me70;
mn71:;
                                                t69 = ((uint8_t)1);
                                                goto me70;
mn72:;
                                                gm_trap("no match arm matched the scrutinee", gm_srch2l5nl06vsci, 512, 24);
me70:;
                                                if (!t69) goto ie64;
                                                {
                                                    uint8_t t75 = gm_l17;
                                                    uint8_t t76 = (uint8_t)!t75;
                                                    if (!t76) goto ie74;
                                                    {
                                                        void * t78 = gm_l0;
                                                        gm_tl874t2atnlh3 t79 = GM_LD(gm_tl874t2atnlh3, ((((uint8_t *)&F.l15) + 40) + 72));
                                                        gm_b16_8 t80 = gm_l21;
                                                        gm_tnpifv20irnph t81;
                                                        GM_ST(gm_b16_8, ((uint8_t *)&t81 + 0), t80);
                                                        gm_tabb710d9mq2e t82;
                                                        gm_f_format_ta7nl7k((void *)&t82, (void *)NULL, cx, NULL, t81);
                                                        if (gm_unw.kind) {
                                                            goto gm_ll52;
                                                        }
                                                        if (((((uint8_t *)&t82))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t82) + 8, gm_srch2l5nl06vsci, 513, 67);
                                                        gm_b16_8 t83 = GM_LD(gm_b16_8, (((uint8_t *)&t82) + 8));
                                                        gm_b16_8 t84 = GM_LD(gm_b16_8, ((uint8_t *)&t83));
                                                        gm_b0_1 t85 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t78, t79, t84);
                                                        if (gm_unw.kind) {
                                                            goto gm_ll52;
                                                        }
                                                    }
                                                    goto ix74;
ie74:;
ix74:;
                                                }
                                                {
                                                    gm_l17 = ((uint8_t)1);
                                                }
                                                {
                                                    goto lc48;
                                                }
                                                goto ix64;
ie64:;
ix64:;
                                            }
                                            goto ix61;
ie61:;
ix61:;
                                        }
                                        {
                                            void * t86 = (void *)((uint8_t *)&F.l11);
                                            gm_tm7a6rksvs6n5 t87;
                                            memset((uint8_t *)&t87 + 44, 0, 4);
                                            memcpy((uint8_t *)&t87 + 0, (((uint8_t *)&gm_l20) + 0), 24);
                                            void * t88 = GM_LD(void *, (((uint8_t *)&gm_l20) + 24));
                                            GM_ST(void *, (uint8_t *)&t87 + 24, t88);
                                            gm_tl874t2atnlh3 t89 = GM_LD(gm_tl874t2atnlh3, ((((uint8_t *)&F.l15) + 40) + 72));
                                            GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t87 + 32, t89);
                                            gm_tftb1odc739t5 t90 = gm_f_push_9a0n4ld(t86, &t87);
                                            if (((((uint8_t *)&t90))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t90) + 1, gm_srch2l5nl06vsci, 518, 17);
                                            gm_b0_1 t91 = GM_LD(gm_b0_1, (((uint8_t *)&t90) + 1));
                                        }
                                        {
                                            void * t92 = (void *)((uint8_t *)&F.l12);
                                            gm_tftb1odc739t5 t93 = gm_f_push_vd1290p(t92, ((uint8_t)1));
                                            if (((((uint8_t *)&t93))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t93) + 1, gm_srch2l5nl06vsci, 519, 17);
                                            gm_b0_1 t94 = GM_LD(gm_b0_1, (((uint8_t *)&t93) + 1));
                                        }
                                        goto me57;
mn58:;
                                        gm_trap("no match arm matched the scrutinee", gm_srch2l5nl06vsci, 510, 13);
me57:;
                                    }
lc48:;
                                    {
                                        uint64_t t96 = gm_l19;
                                        uint64_t r97; bool o98 = __builtin_add_overflow(t96, ((uint64_t)1ULL), &r97);
                                        uint64_t t99 = r97;
                                        gm_l19 = t99;
                                    }
                                }
gm_ll52:;
                                if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb48; }
                                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc48; }
                                }
                                goto gm_ll28;
lb48:;
                            }
                        }
                        {
                            goto lc24;
                        }
                        goto ix37;
ie37:;
ix37:;
                    }
                    {
                        gm_tkn42u7na3t81 t100;
                        memset(&t100, 0, sizeof t100);
                        gm_l22 = t100;
                    }
                    {
                        gm_tl874t2atnlh3 t101 = GM_LD(gm_tl874t2atnlh3, ((((uint8_t *)&F.l15) + 40) + 72));
                        gm_l23 = t101;
                    }
                    {
                        if (!(((((uint8_t *)&F.l15) + 0))[32] != 0)) goto ie102;
                        gm_t08olq97rscn3 t103 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&F.l15) + 0));
                        gm_l24 = t103;
                        {
                            gm_tl874t2atnlh3 t105 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l24) + 16));
                            gm_l23 = t105;
                        }
                        {
                            gm_b16_8 t107 = GM_LD(gm_b16_8, (((uint8_t *)&F.l11) + 0));
                            gm_b16_8 t108 = GM_LD(gm_b16_8, ((uint8_t *)&t107));
                            gm_b16_8 t109 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l24) + 0));
                            gm_tf5srh4mp4b3i t110 = gm_f_index_of_part_7kb7erm(t108, t109);
                            if (!((((uint8_t *)&t110))[8] != 0)) goto ie106;
                            uint64_t t111 = GM_LD(uint64_t, ((uint8_t *)&t110));
                            gm_l25 = t111;
                            {
                                uint64_t t114 = gm_l25;
                                uint64_t t115 = (uintptr_t)t114;
                                uint64_t t116 = GM_LD(uintptr_t, (((uint8_t *)&F.l12) + 0) + 8);
                                if (t115 >= (uintptr_t)(t116)) gm_trap_index(t115, t116, gm_srch2l5nl06vsci, 528, 24);
                                uint8_t t117 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l12) + 0)) + t115 * 1));
                                uint8_t t118 = (uint8_t)!t117;
                                if (!t118) goto ie113;
                                {
                                    void * t120 = gm_l0;
                                    gm_tl874t2atnlh3 t121 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l24) + 16));
                                    gm_b16_8 t122 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l24) + 0));
                                    gm_tnpifv20irnph t123;
                                    GM_ST(gm_b16_8, ((uint8_t *)&t123 + 0), t122);
                                    gm_tabb710d9mq2e t124;
                                    gm_f_format_6nmb1bf((void *)&t124, (void *)NULL, cx, NULL, t123);
                                    if (gm_unw.kind) {
                                        goto gm_ll28;
                                    }
                                    if (((((uint8_t *)&t124))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t124) + 8, gm_srch2l5nl06vsci, 529, 38);
                                    gm_b16_8 t125 = GM_LD(gm_b16_8, (((uint8_t *)&t124) + 8));
                                    gm_b16_8 t126 = GM_LD(gm_b16_8, ((uint8_t *)&t125));
                                    gm_b0_1 t127 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t120, t121, t126);
                                    if (gm_unw.kind) {
                                        goto gm_ll28;
                                    }
                                }
                                {
                                    goto lc24;
                                }
                                goto ix113;
ie113:;
ix113:;
                            }
                            {
                                uint64_t t128 = gm_l25;
                                uint64_t t129 = (uintptr_t)t128;
                                uint64_t t130 = GM_LD(uintptr_t, (((uint8_t *)&F.l11) + 0) + 8);
                                if (t129 >= (uintptr_t)(t130)) gm_trap_index(t129, t130, gm_srch2l5nl06vsci, 533, 22);
                                void * t131 = GM_LD(void *, (((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l11) + 0)) + t129 * 48) + 24));
                                gm_t88rcucg1b7uk t132 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t131) + 0));
                                gm_l26 = t132;
                            }
                            {
                                void * t133 = gm_l0;
                                void * t134 = gm_l0;
                                void * t135 = (void *)(((uint8_t *)&F.l15) + 40);
                                gm_t88rcucg1b7uk t136 = gm_l26;
                                gm_tkn42u7na3t81 t137;
                                GM_ST(uint8_t, (uint8_t *)&t137 + 5, 0);
                                GM_ST(uint16_t, (uint8_t *)&t137 + 6, 0);
                                GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t137), t136);
                                ((uint8_t *)&t137)[4] = 1;
                                void * t138 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t134, t135, t137);
                                if (gm_unw.kind) {
                                    goto gm_ll28;
                                }
                                gm_t88rcucg1b7uk t139 = gm_l26;
                                void * t140 = gm_f_coerce_sudc6c1((void *)NULL, cx, NULL, t133, t138, t139);
                                if (gm_unw.kind) {
                                    goto gm_ll28;
                                }
                                gm_l27 = t140;
                            }
                            {
                                {
                                    void * t142 = gm_l0;
                                    void * t143 = gm_l27;
                                    gm_b16_8 t144;
                                    GM_ST(void *, (uint8_t *)&t144, (void *)(gm_sj4o6222mnbmq));
                                    GM_ST(uintptr_t, (uint8_t *)&t144 + 8, (uintptr_t)(15));
                                    uint8_t t145 = gm_f_reject_second_class_c561fm5((void *)NULL, cx, NULL, t142, t143, t144);
                                    if (gm_unw.kind) {
                                        goto gm_ll28;
                                    }
                                }
                            }
                            {
                                uint64_t t146 = gm_l25;
                                uint64_t t147 = (uintptr_t)t146;
                                uint64_t t148 = GM_LD(uintptr_t, (((uint8_t *)&F.l11) + 0) + 8);
                                if (t147 >= (uintptr_t)(t148)) gm_trap_index(t147, t148, gm_srch2l5nl06vsci, 536, 17);
                                uint8_t *t149 = ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l11) + 0)) + t147 * 48);
                                memset((uint8_t *)&(*(gm_tm7a6rksvs6n5 *)t149) + 44, 0, 4);
                                gm_b16_8 t150 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l24) + 0));
                                gm_th2lqbo77pvsc t151;
                                GM_ST(uint8_t, (uint8_t *)&t151 + 17, 0);
                                GM_ST(uint16_t, (uint8_t *)&t151 + 18, 0);
                                GM_ST(uint32_t, (uint8_t *)&t151 + 20, 0);
                                GM_ST(gm_b16_8, ((uint8_t *)&t151), t150);
                                ((uint8_t *)&t151)[16] = 1;
                                void * t152 = gm_l0;
                                void * t153 = gm_l27;
                                void * t154 = (void *)((uint8_t *)&F.l10);
                                void * t155 = gm_f_bind_temp_sa5rcbr((void *)NULL, cx, NULL, t152, t153, t154);
                                if (gm_unw.kind) {
                                    goto gm_ll28;
                                }
                                gm_tl874t2atnlh3 t156 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l24) + 16));
                                GM_ST(gm_th2lqbo77pvsc, (uint8_t *)&(*(gm_tm7a6rksvs6n5 *)t149) + 0, t151);
                                GM_ST(void *, (uint8_t *)&(*(gm_tm7a6rksvs6n5 *)t149) + 24, t155);
                                GM_ST(gm_tl874t2atnlh3, (uint8_t *)&(*(gm_tm7a6rksvs6n5 *)t149) + 32, t156);
                            }
                            {
                                uint64_t t157 = gm_l25;
                                uint64_t t158 = (uintptr_t)t157;
                                uint64_t t159 = GM_LD(uintptr_t, (((uint8_t *)&F.l12) + 0) + 8);
                                if (t158 >= (uintptr_t)(t159)) gm_trap_index(t158, t159, gm_srch2l5nl06vsci, 537, 17);
                                GM_ST(uint8_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l12) + 0)) + t158 * 1), ((uint8_t)0));
                            }
                            {
                                goto lc24;
                            }
                            goto ix106;
ie106:;
ix106:;
                        }
                        {
                            gm_b16_8 t161 = gm_l6;
                            gm_b16_8 t162 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l24) + 0));
                            gm_tf5srh4mp4b3i t163 = gm_f_index_of_o9r57ha(t161, t162);
                            if (!((((uint8_t *)&t163))[8] != 0)) goto ie160;
                            uint64_t t164 = GM_LD(uint64_t, ((uint8_t *)&t163));
                            gm_l28 = t164;
                            {
                                uint64_t t166 = gm_l28;
                                uint64_t t167 = (uintptr_t)t166;
                                uint64_t t168 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                                if (t167 >= (uintptr_t)(t168)) gm_trap_index(t167, t168, gm_srch2l5nl06vsci, 540, 57);
                                gm_t88rcucg1b7uk t169 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l5)) + t167 * 4));
                                gm_tkn42u7na3t81 t170;
                                GM_ST(uint8_t, (uint8_t *)&t170 + 5, 0);
                                GM_ST(uint16_t, (uint8_t *)&t170 + 6, 0);
                                GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t170), t169);
                                ((uint8_t *)&t170)[4] = 1;
                                gm_l22 = t170;
                            }
                            goto ix160;
ie160:;
ix160:;
                        }
                        goto ix102;
ie102:;
                        uint64_t t172 = GM_LD(uintptr_t, (((uint8_t *)&F.l11) + 0) + 8);
                        uint64_t t173 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                        uint8_t t174 = (t172 < t173);
                        if (!t174) goto ie171;
                        {
                            uint64_t t176 = GM_LD(uintptr_t, (((uint8_t *)&F.l11) + 0) + 8);
                            uint64_t t177 = (uintptr_t)t176;
                            uint64_t t178 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                            if (t177 >= (uintptr_t)(t178)) gm_trap_index(t177, t178, gm_srch2l5nl06vsci, 542, 19);
                            gm_t88rcucg1b7uk t179 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l5)) + t177 * 4));
                            gm_tkn42u7na3t81 t180;
                            GM_ST(uint8_t, (uint8_t *)&t180 + 5, 0);
                            GM_ST(uint16_t, (uint8_t *)&t180 + 6, 0);
                            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t180), t179);
                            ((uint8_t *)&t180)[4] = 1;
                            gm_l22 = t180;
                        }
                        goto ix171;
ie171:;
ix171:;
ix102:;
                    }
                    {
                        void * t181 = gm_l0;
                        void * t182 = gm_l0;
                        void * t183 = (void *)(((uint8_t *)&F.l15) + 40);
                        gm_tkn42u7na3t81 t184 = gm_l22;
                        void * t185 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t182, t183, t184);
                        if (gm_unw.kind) {
                            goto gm_ll28;
                        }
                        gm_tkn42u7na3t81 t186 = gm_l22;
                        void * t187 = gm_f_coerce_element_9ue7d7u((void *)NULL, cx, NULL, t181, t185, t186);
                        if (gm_unw.kind) {
                            goto gm_ll28;
                        }
                        gm_l29 = t187;
                    }
                    {
                        {
                            void * t189 = gm_l0;
                            void * t190 = gm_l29;
                            gm_b16_8 t191;
                            GM_ST(void *, (uint8_t *)&t191, (void *)(gm_sj4o6222mnbmq));
                            GM_ST(uintptr_t, (uint8_t *)&t191 + 8, (uintptr_t)(15));
                            uint8_t t192 = gm_f_reject_second_class_c561fm5((void *)NULL, cx, NULL, t189, t190, t191);
                            if (gm_unw.kind) {
                                goto gm_ll28;
                            }
                        }
                    }
                    {
                        void * t193 = (void *)((uint8_t *)&F.l11);
                        gm_tm7a6rksvs6n5 t194;
                        memset((uint8_t *)&t194 + 44, 0, 4);
                        gm_th2lqbo77pvsc t195;
                        if (!(((((uint8_t *)&F.l15) + 0))[32] != 0)) goto ie196;
                        gm_t08olq97rscn3 t197 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&F.l15) + 0));
                        gm_l30 = t197;
                        gm_b16_8 t199 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l30) + 0));
                        gm_b16_8 t200 = t199;
                        gm_th2lqbo77pvsc t201;
                        GM_ST(uint8_t, (uint8_t *)&t201 + 17, 0);
                        GM_ST(uint16_t, (uint8_t *)&t201 + 18, 0);
                        GM_ST(uint32_t, (uint8_t *)&t201 + 20, 0);
                        GM_ST(gm_b16_8, ((uint8_t *)&t201), t200);
                        ((uint8_t *)&t201)[16] = 1;
                        t195 = t201;
                        goto ix196;
ie196:;
                        gm_th2lqbo77pvsc t203;
                        memset(&t203, 0, sizeof t203);
                        gm_th2lqbo77pvsc t204 = t203;
                        t195 = t204;
ix196:;
                        GM_ST(gm_th2lqbo77pvsc, (uint8_t *)&t194 + 0, t195);
                        void * t205 = gm_l0;
                        void * t206 = gm_l29;
                        void * t207 = (void *)((uint8_t *)&F.l10);
                        void * t208 = gm_f_bind_temp_sa5rcbr((void *)NULL, cx, NULL, t205, t206, t207);
                        if (gm_unw.kind) {
                            goto gm_ll28;
                        }
                        GM_ST(void *, (uint8_t *)&t194 + 24, t208);
                        gm_tl874t2atnlh3 t209 = gm_l23;
                        GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t194 + 32, t209);
                        gm_tftb1odc739t5 t210 = gm_f_push_9a0n4ld(t193, &t194);
                        if (((((uint8_t *)&t210))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t210) + 1, gm_srch2l5nl06vsci, 546, 9);
                        gm_b0_1 t211 = GM_LD(gm_b0_1, (((uint8_t *)&t210) + 1));
                    }
                    {
                        void * t212 = (void *)((uint8_t *)&F.l12);
                        gm_tftb1odc739t5 t213 = gm_f_push_vd1290p(t212, ((uint8_t)0));
                        if (((((uint8_t *)&t213))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t213) + 1, gm_srch2l5nl06vsci, 547, 9);
                        gm_b0_1 t214 = GM_LD(gm_b0_1, (((uint8_t *)&t213) + 1));
                    }
                    goto me33;
mn34:;
                    gm_trap("no match arm matched the scrutinee", gm_srch2l5nl06vsci, 506, 5);
me33:;
                }
lc24:;
                {
                    uint64_t t216 = gm_l14;
                    uint64_t r217; bool o218 = __builtin_add_overflow(t216, ((uint64_t)1ULL), &r217);
                    uint64_t t219 = r217;
                    gm_l14 = t219;
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
        uint8_t t220 = 0;
        uint64_t t221 = GM_LD(uintptr_t, (((uint8_t *)&F.l11) + 0) + 8);
        uint8_t t222 = (t221 > ((uint64_t)0ULL));
        if (t222) {
            uint64_t t223 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t224 = GM_LD(uintptr_t, (((uint8_t *)&F.l11) + 0) + 8);
            if (t223 >= (uintptr_t)(t224)) gm_trap_index(t223, t224, gm_srch2l5nl06vsci, 549, 38);
            uint8_t t225;
            if ((((((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l11) + 0)) + t223 * 48) + 0))[16] != 0)) goto mn227;
            t225 = ((uint8_t)0);
            goto me226;
mn227:;
            t225 = ((uint8_t)1);
            goto me226;
mn228:;
            gm_trap("no match arm matched the scrutinee", gm_srch2l5nl06vsci, 549, 38);
me226:;
            t220 = t225;
        } else t220 = 0;
        gm_l31 = t220;
    }
    {
        {
            gm_b16_8 t230 = GM_LD(gm_b16_8, (((uint8_t *)&F.l11) + 0));
            gm_l32 = t230;
        }
        {
            gm_l33 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t232 = gm_l33;
                uint64_t t233 = GM_LD(uintptr_t, ((uint8_t *)&gm_l32) + 8);
                uint8_t t234 = (t232 < t233);
                if (!t234) goto lb231;
                {
                    uint64_t t237 = gm_l33;
                    uint64_t t238 = (uintptr_t)t237;
                    uint64_t t239 = GM_LD(uintptr_t, ((uint8_t *)&gm_l32) + 8);
                    gm_tm7a6rksvs6n5 t242 = GM_LD(gm_tm7a6rksvs6n5, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l32)) + t238 * 48));
                    F.l34 = t242;
                    {
                        uint8_t t245;
                        if ((((((uint8_t *)&F.l34) + 0))[16] != 0)) goto mn247;
                        t245 = ((uint8_t)0);
                        goto me246;
mn247:;
                        t245 = ((uint8_t)1);
                        goto me246;
mn248:;
                        gm_trap("no match arm matched the scrutinee", gm_srch2l5nl06vsci, 551, 13);
me246:;
                        uint8_t t249 = gm_l31;
                        uint8_t t250 = (!(t245 == t249));
                        if (!t250) goto ie244;
                        {
                            void * t252 = gm_l0;
                            gm_tl874t2atnlh3 t253 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l34) + 32));
                            gm_b16_8 t254;
                            GM_ST(void *, (uint8_t *)&t254, (void *)(gm_serasf77ce9g3));
                            GM_ST(uintptr_t, (uint8_t *)&t254 + 8, (uintptr_t)(51));
                            gm_b0_1 t255 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t252, t253, t254);
                            if (gm_unw.kind) {
                                goto gm_ll235;
                            }
                        }
                        {
                            void * t256 = gm_l0;
                            void * t257 = GM_LD(void *, (((uint8_t *)t256) + 0));
                            gm_tl874t2atnlh3 t258 = gm_l3;
                            void * t259 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t257, t258);
                            if (gm_unw.kind) {
                                goto gm_ll235;
                            }
                            uint8_t t260 = 0;
                            F.ret = t259;
                            goto gm_fret;
                        }
                        goto ix244;
ie244:;
ix244:;
                    }
                    goto me240;
mn241:;
                    gm_trap("no match arm matched the scrutinee", gm_srch2l5nl06vsci, 550, 5);
me240:;
                }
lc231:;
                {
                    uint64_t t262 = gm_l33;
                    uint64_t r263; bool o264 = __builtin_add_overflow(t262, ((uint64_t)1ULL), &r263);
                    uint64_t t265 = r263;
                    gm_l33 = t265;
                }
            }
gm_ll235:;
            if (gm_unw.frame == &F.h && gm_unw.label == 2) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb231; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc231; }
            }
            goto gm_lad;
lb231:;
        }
    }
    {
        uint8_t t266 = 0;
        uint64_t t267 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
        uint64_t t268 = GM_LD(uintptr_t, (((uint8_t *)&F.l11) + 0) + 8);
        uint8_t t269 = ((t267 == t268));
        if (t269) {
            uint8_t t270 = 0;
            uint64_t t271 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
            uint8_t t272 = ((t271 == ((uint64_t)0ULL)));
            if (!t272) {
                uint8_t t273 = gm_l31;
                uint8_t t274 = (uint8_t)!t273;
                t270 = t274;
            } else t270 = 1;
            t266 = t270;
        } else t266 = 0;
        gm_l35 = t266;
    }
    {
        uint8_t t276 = gm_l31;
        if (!t276) goto ie275;
        uint64_t t277 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
        uint64_t t278 = GM_LD(uintptr_t, (((uint8_t *)&F.l11) + 0) + 8);
        uint8_t t279 = ((t277 == t278));
        if (!t279) goto ie275;
        {
            gm_l35 = ((uint8_t)1);
        }
        {
            {
                gm_b16_8 t282 = GM_LD(gm_b16_8, (((uint8_t *)&F.l11) + 0));
                gm_b16_8 t283 = GM_LD(gm_b16_8, ((uint8_t *)&t282));
                gm_l36 = t283;
            }
            {
                gm_l37 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t285 = gm_l37;
                    uint64_t t286 = GM_LD(uintptr_t, ((uint8_t *)&gm_l36) + 8);
                    uint8_t t287 = (t285 < t286);
                    if (!t287) goto lb284;
                    {
                        gm_t3gcf8sa6b1tu t290;
                        uint64_t t291 = gm_l37;
                        GM_ST(uint64_t, (uint8_t *)&t290 + 0, t291);
                        uint64_t t292 = gm_l37;
                        uint64_t t293 = (uintptr_t)t292;
                        uint64_t t294 = GM_LD(uintptr_t, ((uint8_t *)&gm_l36) + 8);
                        memcpy((uint8_t *)&t290 + 8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l36)) + t293 * 48), 48);
                        uint64_t t297 = GM_LD(uint64_t, (((uint8_t *)&t290) + 0));
                        gm_l38 = t297;
                        F.l39 = (*(gm_tm7a6rksvs6n5 *)(((uint8_t *)&t290) + 8));
                        {
                            gm_th2lqbo77pvsc t300 = GM_LD(gm_th2lqbo77pvsc, (((uint8_t *)&F.l39) + 0));
                            uint64_t t301 = gm_l38;
                            uint64_t t302 = (uintptr_t)t301;
                            uint64_t t303 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                            if (t302 >= (uintptr_t)(t303)) gm_trap_index(t302, t303, gm_srch2l5nl06vsci, 561, 30);
                            gm_b16_8 t304 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t302 * 16));
                            gm_th2lqbo77pvsc t305;
                            GM_ST(uint8_t, (uint8_t *)&t305 + 17, 0);
                            GM_ST(uint16_t, (uint8_t *)&t305 + 18, 0);
                            GM_ST(uint32_t, (uint8_t *)&t305 + 20, 0);
                            GM_ST(gm_b16_8, ((uint8_t *)&t305), t304);
                            ((uint8_t *)&t305)[16] = 1;
                            uint8_t t306 = (!gm_eq0ogca82g1f(((uint8_t *)&t300), ((uint8_t *)&t305)));
                            if (!t306) goto ie299;
                            {
                                gm_l35 = ((uint8_t)0);
                            }
                            goto ix299;
ie299:;
ix299:;
                        }
                        goto me295;
mn296:;
                        gm_trap("no match arm matched the scrutinee", gm_srch2l5nl06vsci, 560, 9);
me295:;
                    }
lc284:;
                    {
                        uint64_t t309 = gm_l37;
                        uint64_t r310; bool o311 = __builtin_add_overflow(t309, ((uint64_t)1ULL), &r310);
                        uint64_t t312 = r310;
                        gm_l37 = t312;
                    }
                }
lb284:;
            }
        }
        goto ix275;
ie275:;
ix275:;
    }
    {
        gm_f_list_vp1djv1_2((void *)&F.l40, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_f_list_nof7vfj((void *)&F.l41, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_f_list_epvducg((void *)&F.l42, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t314 = GM_LD(gm_b16_8, (((uint8_t *)&F.l11) + 0));
            gm_b16_8 t315 = GM_LD(gm_b16_8, ((uint8_t *)&t314));
            gm_l43 = t315;
        }
        {
            gm_l44 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t317 = gm_l44;
                uint64_t t318 = GM_LD(uintptr_t, ((uint8_t *)&gm_l43) + 8);
                uint8_t t319 = (t317 < t318);
                if (!t319) goto lb316;
                {
                    gm_t3gcf8sa6b1tu t322;
                    uint64_t t323 = gm_l44;
                    GM_ST(uint64_t, (uint8_t *)&t322 + 0, t323);
                    uint64_t t324 = gm_l44;
                    uint64_t t325 = (uintptr_t)t324;
                    uint64_t t326 = GM_LD(uintptr_t, ((uint8_t *)&gm_l43) + 8);
                    memcpy((uint8_t *)&t322 + 8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l43)) + t325 * 48), 48);
                    uint64_t t329 = GM_LD(uint64_t, (((uint8_t *)&t322) + 0));
                    gm_l45 = t329;
                    F.l46 = (*(gm_tm7a6rksvs6n5 *)(((uint8_t *)&t322) + 8));
                    {
                        void * t331 = GM_LD(void *, (((uint8_t *)&F.l46) + 24));
                        gm_l47 = t331;
                    }
                    {
                        uint8_t t333 = gm_l35;
                        if (!t333) goto ie332;
                        uint64_t t334 = gm_l45;
                        uint64_t t335 = (uintptr_t)t334;
                        uint64_t t336 = GM_LD(uintptr_t, (((uint8_t *)&F.l12) + 0) + 8);
                        if (t335 >= (uintptr_t)(t336)) gm_trap_index(t335, t336, gm_srch2l5nl06vsci, 569, 21);
                        uint8_t t337 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l12) + 0)) + t335 * 1));
                        if (!t337) goto ie332;
                        {
                            void * t339 = gm_l0;
                            void * t340 = gm_l47;
                            uint64_t t341 = gm_l45;
                            uint64_t t342 = (uintptr_t)t341;
                            uint64_t t343 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                            if (t342 >= (uintptr_t)(t343)) gm_trap_index(t342, t343, gm_srch2l5nl06vsci, 569, 64);
                            gm_t88rcucg1b7uk t344 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l5)) + t342 * 4));
                            gm_tkn42u7na3t81 t345;
                            GM_ST(uint8_t, (uint8_t *)&t345 + 5, 0);
                            GM_ST(uint16_t, (uint8_t *)&t345 + 6, 0);
                            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t345), t344);
                            ((uint8_t *)&t345)[4] = 1;
                            void * t346 = gm_f_coerce_element_9ue7d7u((void *)NULL, cx, NULL, t339, t340, t345);
                            if (gm_unw.kind) {
                                goto gm_ll320;
                            }
                            gm_l47 = t346;
                        }
                        goto ix332;
ie332:;
ix332:;
                    }
                    {
                        void * t347 = (void *)((uint8_t *)&F.l40);
                        void * t348 = gm_l47;
                        gm_tftb1odc739t5 t349 = gm_f_push_d943vgp_2(t347, &(*(gm_t69m56meqefs8 *)((uint8_t *)t348)));
                        if (((((uint8_t *)&t349))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t349) + 1, gm_srch2l5nl06vsci, 570, 9);
                        gm_b0_1 t350 = GM_LD(gm_b0_1, (((uint8_t *)&t349) + 1));
                    }
                    {
                        void * t351 = (void *)((uint8_t *)&F.l41);
                        void * t352 = gm_l47;
                        gm_t88rcucg1b7uk t353 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t352) + 0));
                        gm_tftb1odc739t5 t354 = gm_f_push_k3l9nic(t351, GM_CONV(gm_csguankko4lmhd, t353));
                        if (((((uint8_t *)&t354))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t354) + 1, gm_srch2l5nl06vsci, 571, 9);
                        gm_b0_1 t355 = GM_LD(gm_b0_1, (((uint8_t *)&t354) + 1));
                    }
                    {
                        if (!(((((uint8_t *)&F.l46) + 0))[16] != 0)) goto ie356;
                        gm_b16_8 t357 = GM_LD(gm_b16_8, (((uint8_t *)&F.l46) + 0));
                        gm_l48 = t357;
                        {
                            void * t359 = (void *)((uint8_t *)&F.l42);
                            gm_b16_8 t360 = gm_l48;
                            gm_tftb1odc739t5 t361 = gm_f_push_3ef746b(t359, t360);
                            if (((((uint8_t *)&t361))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t361) + 1, gm_srch2l5nl06vsci, 572, 26);
                            gm_b0_1 t362 = GM_LD(gm_b0_1, (((uint8_t *)&t361) + 1));
                        }
                        goto ix356;
ie356:;
ix356:;
                    }
                    goto me327;
mn328:;
                    gm_trap("no match arm matched the scrutinee", gm_srch2l5nl06vsci, 567, 5);
me327:;
                }
lc316:;
                {
                    uint64_t t364 = gm_l44;
                    uint64_t r365; bool o366 = __builtin_add_overflow(t364, ((uint64_t)1ULL), &r365);
                    uint64_t t367 = r365;
                    gm_l44 = t367;
                }
            }
gm_ll320:;
            if (gm_unw.frame == &F.h && gm_unw.label == 4) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb316; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc316; }
            }
            goto gm_lad;
lb316:;
        }
    }
    {
        void * t368 = gm_l4;
        gm_t6j6smj7qoctu t369;
        gm_b16_8 t370 = GM_LD(gm_b16_8, (((uint8_t *)&F.l41) + 0));
        gm_b16_8 t371 = GM_LD(gm_b16_8, ((uint8_t *)&t370));
        GM_ST(gm_b16_8, (uint8_t *)&t369 + 0, t371);
        gm_b16_8 t372 = GM_LD(gm_b16_8, (((uint8_t *)&F.l42) + 0));
        gm_b16_8 t373 = GM_LD(gm_b16_8, ((uint8_t *)&t372));
        GM_ST(gm_b16_8, (uint8_t *)&t369 + 16, t373);
        gm_t53at80pfdus3 t374;
        GM_ST(uint8_t, (uint8_t *)&t374 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t374 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t374 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t374 + 40, 0);
        GM_ST(uint64_t, (uint8_t *)&t374 + 48, 0);
        GM_ST(uint8_t, ((uint8_t *)&t374 + 0), ((uint8_t)12ULL));
        GM_ST(gm_t6j6smj7qoctu, ((uint8_t *)&t374 + 8), t369);
        gm_t88rcucg1b7uk t375 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t368, &t374);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l49 = t375;
    }
    {
        gm_t88rcucg1b7uk t376 = gm_l49;
        gm_b16_8 t377 = GM_LD(gm_b16_8, (((uint8_t *)&F.l40) + 0));
        gm_teti411eujvf4 t378;
        memset(&t378, 0, sizeof t378);
        GM_ST(uint8_t, ((uint8_t *)&t378 + 0), ((uint8_t)35ULL));
        GM_ST(gm_b16_8, ((uint8_t *)&t378 + 8), t377);
        gm_tl874t2atnlh3 t379 = gm_l3;
        void * t380 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t376, &t378, t379);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l50 = t380;
    }
    {
        uint64_t t382 = GM_LD(uintptr_t, (((uint8_t *)&F.l10) + 0) + 8);
        uint8_t t383 = ((t382 == ((uint64_t)0ULL)));
        if (!t383) goto ie381;
        {
            void * t385 = gm_l50;
            uint8_t t386 = 0;
            F.ret = t385;
            goto gm_fret;
        }
        goto ix381;
ie381:;
ix381:;
    }
    {
        gm_t88rcucg1b7uk t387 = gm_l49;
        gm_b16_8 t388 = GM_LD(gm_b16_8, (((uint8_t *)&F.l10) + 0));
        void * t389 = gm_l50;
        gm_tl874t2atnlh3 t390 = gm_l3;
        void * t391 = gm_f_block_typed_e7svg7n((void *)NULL, cx, NULL, t387, t388, t389, t390);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t392 = 0;
        F.ret = t391;
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
__attribute__((aligned(4))) void * gm_f_struct_field_cc6n63f(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_b16_8 a2, gm_tl874t2atnlh3 a3, uint32_t a4) {
    struct gm_fr_struct_field_cc6n63f F;
    void * gm_l0; /* cx */
    void * gm_l1; /* b */
    gm_b16_8 gm_l2; /* name */
    gm_tl874t2atnlh3 gm_l3; /* span */
    uint32_t gm_l4; /* depth */
    gm_t88rcucg1b7uk gm_l5; /* bt */
    gm_b16_8 gm_l6; /* fields */
    gm_b16_8 gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    uint64_t gm_l9; /* i */
    uint32_t gm_l11; /* id */
    void * gm_l12; /* inner */
    GM_STACK_CHECK(&F, 1062u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    {
        void * t2 = gm_l0;
        void * t3 = (void *)(((uint8_t *)t2) + 96);
        void * t4 = gm_l0;
        void * t5 = GM_LD(void *, (((uint8_t *)t4) + 0));
        void * t6 = gm_l1;
        gm_t88rcucg1b7uk t7 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t6) + 0));
        gm_t88rcucg1b7uk t8 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t3, t5, t7);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l5 = t8;
    }
    {
        void * t9 = gm_l0;
        void * t10 = GM_LD(void *, (((uint8_t *)t9) + 0));
        gm_t88rcucg1b7uk t11 = gm_l5;
        gm_th2lqbo77pvsc t12;
        gm_f_struct_fields_chqu3it((void *)&t12, t10, GM_CONV(gm_csguankko4lmhd, t11));
        gm_b16_8 t13;
        if (((((uint8_t *)&t12))[16] != 0)) {
            gm_b16_8 t14 = GM_LD(gm_b16_8, (((uint8_t *)&t12) + 0));
            t13 = t14;
        } else {
            void * t15 = gm_l0;
            void * t16 = GM_LD(void *, (((uint8_t *)t15) + 0));
            gm_t88rcucg1b7uk t17 = gm_l5;
            gm_th2lqbo77pvsc t18;
            gm_f_c_union_fields_btdvc8b((void *)&t18, t16, GM_CONV(gm_csguankko4lmhd, t17));
            gm_b16_8 t19;
            if (((((uint8_t *)&t18))[16] != 0)) {
                gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&t18) + 0));
                t19 = t20;
            } else {
                uint8_t t21 = 0;
                F.ret = ((void *)0);
                goto gm_fret;
            }
            t13 = t19;
        }
        gm_l6 = t13;
    }
    {
        {
            gm_b16_8 t23 = gm_l6;
            gm_b16_8 t24 = GM_LD(gm_b16_8, ((uint8_t *)&t23));
            gm_l7 = t24;
        }
        {
            gm_l8 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t26 = gm_l8;
                uint64_t t27 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                uint8_t t28 = (t26 < t27);
                if (!t28) goto lb25;
                {
                    gm_t8ta482bl6be0 t31;
                    uint64_t t32 = gm_l8;
                    GM_ST(uint64_t, (uint8_t *)&t31 + 0, t32);
                    uint64_t t33 = gm_l8;
                    uint64_t t34 = (uintptr_t)t33;
                    uint64_t t35 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                    memcpy((uint8_t *)&t31 + 8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l7)) + t34 * 40), 40);
                    uint64_t t38 = GM_LD(uint64_t, (((uint8_t *)&t31) + 0));
                    gm_l9 = t38;
                    F.l10 = (*(gm_t0mglksrkfa5e *)(((uint8_t *)&t31) + 8));
                    {
                        gm_b16_8 t41 = GM_LD(gm_b16_8, (((uint8_t *)&F.l10) + 0));
                        gm_b16_8 t42 = gm_l2;
                        uint8_t t43 = (!gm_eq0iqiubfm7e(((uint8_t *)&t41), ((uint8_t *)&t42)));
                        if (!t43) goto ie40;
                        {
                            goto lc25;
                        }
                        goto ix40;
ie40:;
ix40:;
                    }
                    {
                        void * t46 = gm_l0;
                        gm_t9qkh0rsooc8d t47 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&F.l10) + 20));
                        gm_t88rcucg1b7uk t48 = gm_l5;
                        uint8_t t49 = gm_f_field_visible_here_20vnoda(t46, t47, GM_CONV(gm_csguankko4lmhd, t48));
                        uint8_t t50 = (uint8_t)!t49;
                        if (!t50) goto ie45;
                        {
                            void * t52 = gm_l0;
                            gm_tl874t2atnlh3 t53 = gm_l3;
                            void * t54 = gm_l0;
                            gm_b16_8 t55 = gm_l2;
                            gm_t88rcucg1b7uk t56 = gm_l5;
                            gm_t9qkh0rsooc8d t57 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&F.l10) + 20));
                            gm_b16_8 t58 = gm_f_hidden_field_message_b0pdek7((void *)NULL, cx, NULL, t54, t55, t56, t57);
                            if (gm_unw.kind) {
                                goto gm_ll29;
                            }
                            gm_b0_1 t59 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t52, t53, t58);
                            if (gm_unw.kind) {
                                goto gm_ll29;
                            }
                        }
                        goto ix45;
ie45:;
ix45:;
                    }
                    {
                        gm_t88rcucg1b7uk t60 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l10) + 16));
                        void * t61 = gm_l1;
                        uint64_t t62 = gm_l9;
                        uint32_t t63 = (uint32_t)t62;
                        gm_tbl8il92l97ut t64;
                        GM_ST(uint32_t, (uint8_t *)&t64 + 12, 0);
                        GM_ST(void *, ((uint8_t *)&t64 + 0), t61);
                        GM_ST(uint32_t, ((uint8_t *)&t64 + 8), t63);
                        gm_teti411eujvf4 t65;
                        memset(&t65, 0, sizeof t65);
                        GM_ST(uint8_t, ((uint8_t *)&t65 + 0), ((uint8_t)19ULL));
                        GM_ST(gm_tbl8il92l97ut, ((uint8_t *)&t65 + 8), t64);
                        gm_tl874t2atnlh3 t66 = gm_l3;
                        void * t67 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t60, &t65, t66);
                        if (gm_unw.kind) {
                            goto gm_ll29;
                        }
                        uint8_t t68 = 0;
                        F.ret = t67;
                        goto gm_fret;
                    }
                    goto me36;
mn37:;
                    gm_trap("no match arm matched the scrutinee", gm_srch2l5nl06vsci, 1155, 5);
me36:;
                }
lc25:;
                {
                    uint64_t t70 = gm_l8;
                    uint64_t r71; bool o72 = __builtin_add_overflow(t70, ((uint64_t)1ULL), &r71);
                    uint64_t t73 = r71;
                    gm_l8 = t73;
                }
            }
gm_ll29:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb25; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc25; }
            }
            goto gm_lad;
lb25:;
        }
    }
    {
        void * t75 = gm_l0;
        void * t76 = GM_LD(void *, (((uint8_t *)t75) + 0));
        void * t77 = (void *)(((uint8_t *)t76) + 8);
        gm_t88rcucg1b7uk t78 = gm_l5;
        gm_t53at80pfdus3 t79;
        gm_f_kind_jcnt4hm((void *)&t79, t77, GM_CONV(gm_csguankko4lmhd, t78));
        uint8_t t80 = GM_LD(uint8_t, (((uint8_t *)&t79) + 0));
        if (t80 != ((uint8_t)17ULL)) goto ie74;
        uint32_t t81 = GM_LD(uint32_t, ((((uint8_t *)&t79) + 8) + 0));
        gm_l11 = t81;
        {
            uint32_t t84 = gm_l11;
            uint64_t t85 = (uint64_t)t84;
            uint64_t t86 = (uintptr_t)t85;
            void * t87 = gm_l0;
            void * t88 = GM_LD(void *, (((uint8_t *)t87) + 0));
            uint64_t t89 = GM_LD(uintptr_t, ((((uint8_t *)t88) + 1088) + 0) + 8);
            if (t86 >= (uintptr_t)(t89)) gm_trap_index(t86, t89, gm_srch2l5nl06vsci, 1161, 12);
            uint8_t t90 = GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t88) + 1088) + 0)) + t86 * 184) + 112));
            uint8_t t91 = (uint8_t)!t90;
            if (!t91) goto ie83;
            {
                uint8_t t93 = 0;
                F.ret = ((void *)0);
                goto gm_fret;
            }
            goto ix83;
ie83:;
ix83:;
        }
        {
            uint8_t t95 = 0;
            uint32_t t96 = gm_l4;
            uint8_t t97 = (t96 >= ((uint32_t)64ULL));
            if (!t97) {
                uint64_t t98 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                uint8_t t99 = ((t98 == ((uint64_t)0ULL)));
                t95 = t99;
            } else t95 = 1;
            if (!t95) goto ie94;
            {
                uint8_t t101 = 0;
                F.ret = ((void *)0);
                goto gm_fret;
            }
            goto ix94;
ie94:;
ix94:;
        }
        {
            uint64_t t102 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t103 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
            if (t102 >= (uintptr_t)(t103)) gm_trap_index(t102, t103, gm_srch2l5nl06vsci, 1164, 21);
            gm_t88rcucg1b7uk t104 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t102 * 40) + 16));
            void * t105 = gm_l1;
            gm_tbl8il92l97ut t106;
            GM_ST(uint32_t, (uint8_t *)&t106 + 12, 0);
            GM_ST(void *, ((uint8_t *)&t106 + 0), t105);
            GM_ST(uint32_t, ((uint8_t *)&t106 + 8), ((uint32_t)0ULL));
            gm_teti411eujvf4 t107;
            memset(&t107, 0, sizeof t107);
            GM_ST(uint8_t, ((uint8_t *)&t107 + 0), ((uint8_t)19ULL));
            GM_ST(gm_tbl8il92l97ut, ((uint8_t *)&t107 + 8), t106);
            gm_tl874t2atnlh3 t108 = gm_l3;
            void * t109 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t104, &t107, t108);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l12 = t109;
        }
        {
            void * t110 = gm_l0;
            void * t111 = gm_l12;
            gm_b16_8 t112 = gm_l2;
            gm_tl874t2atnlh3 t113 = gm_l3;
            uint32_t t114 = gm_l4;
            uint32_t r115; bool o116 = __builtin_add_overflow(t114, ((uint32_t)1ULL), &r115);
            if (o116) gm_trap("integer overflow in `+`", gm_srch2l5nl06vsci, 1165, 47);
            uint32_t t117 = r115;
            void * t118 = gm_f_struct_field_cc6n63f((void *)NULL, cx, NULL, t110, t111, t112, t113, t117);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t119 = 0;
            F.ret = t118;
            goto gm_fret;
        }
        goto ix74;
ie74:;
        {
            uint8_t t121 = 0;
            F.ret = ((void *)0);
            goto gm_fret;
        }
ix74:;
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
__attribute__((aligned(4))) void * gm_f_tuple_field_dmnnit2(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint32_t a2, gm_tl874t2atnlh3 a3) {
    struct gm_fr_tuple_field_dmnnit2 F;
    void * gm_l0; /* cx */
    void * gm_l1; /* base */
    uint32_t gm_l2; /* index */
    gm_tl874t2atnlh3 gm_l3; /* span */
    void * gm_l4; /* b */
    gm_t88rcucg1b7uk gm_l5; /* bt */
    gm_b16_8 gm_l6; /* elems */
    GM_STACK_CHECK(&F, 699u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        void * t2 = gm_l0;
        void * t3 = gm_l1;
        gm_tkn42u7na3t81 t4;
        memset(&t4, 0, sizeof t4);
        void * t5 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t2, t3, t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l4 = t5;
    }
    {
        void * t6 = gm_l0;
        void * t7 = (void *)(((uint8_t *)t6) + 96);
        void * t8 = gm_l0;
        void * t9 = GM_LD(void *, (((uint8_t *)t8) + 0));
        void * t10 = gm_l4;
        gm_t88rcucg1b7uk t11 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t10) + 0));
        gm_t88rcucg1b7uk t12 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t7, t9, t11);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l5 = t12;
    }
    {
        void * t13 = gm_l0;
        void * t14 = gm_l4;
        void * t15 = gm_f_auto_deref_vdb1hdn((void *)NULL, cx, NULL, t13, t14);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l4 = t15;
    }
    {
        void * t16 = gm_l0;
        void * t17 = GM_LD(void *, (((uint8_t *)t16) + 0));
        void * t18 = (void *)(((uint8_t *)t17) + 8);
        void * t19 = gm_l0;
        void * t20 = (void *)(((uint8_t *)t19) + 96);
        void * t21 = gm_l0;
        void * t22 = GM_LD(void *, (((uint8_t *)t21) + 0));
        void * t23 = gm_l4;
        gm_t88rcucg1b7uk t24 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t23) + 0));
        gm_t88rcucg1b7uk t25 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t20, t22, t24);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t53at80pfdus3 t26;
        gm_f_kind_jcnt4hm((void *)&t26, t18, GM_CONV(gm_csguankko4lmhd, t25));
        uint8_t t32 = GM_LD(uint8_t, (((uint8_t *)&t26) + 0));
        switch (t32) { case ((uint8_t)0ULL): goto ma30; case ((uint8_t)1ULL): goto ma30; case ((uint8_t)2ULL): goto ma30; case ((uint8_t)3ULL): goto ma30; case ((uint8_t)4ULL): goto ma30; case ((uint8_t)5ULL): goto ma30; case ((uint8_t)6ULL): goto ma30; case ((uint8_t)7ULL): goto ma30; case ((uint8_t)8ULL): goto ma30; case ((uint8_t)9ULL): goto ma30; case ((uint8_t)10ULL): goto ma30; case ((uint8_t)11ULL): goto ma30; case ((uint8_t)12ULL): goto ma28; case ((uint8_t)13ULL): goto ma30; case ((uint8_t)14ULL): goto ma30; case ((uint8_t)15ULL): goto ma30; case ((uint8_t)16ULL): goto ma30; case ((uint8_t)17ULL): goto ma30; case ((uint8_t)18ULL): goto ma30; case ((uint8_t)19ULL): goto ma30; case ((uint8_t)20ULL): goto ma30; case ((uint8_t)21ULL): goto ma30; case ((uint8_t)22ULL): goto ma30; case ((uint8_t)23ULL): goto ma30; case ((uint8_t)24ULL): goto ma30; case ((uint8_t)25ULL): goto ma30; case ((uint8_t)26ULL): goto ma29; default: goto ma30; }
ma28:;
        uint8_t t34 = GM_LD(uint8_t, (((uint8_t *)&t26) + 0));
        if (t34 != ((uint8_t)12ULL)) goto mn33;
        gm_b16_8 t35 = GM_LD(gm_b16_8, ((((uint8_t *)&t26) + 8) + 0));
        gm_l6 = t35;
        uint32_t t37 = gm_l2;
        uint64_t t38 = (uint64_t)t37;
        uint64_t t39 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
        uint8_t t40 = (t38 < t39);
        if (!t40) goto ie36;
        {
            uint32_t t42 = gm_l2;
            uint64_t t43 = (uint64_t)t42;
            uint64_t t44 = (uintptr_t)t43;
            uint64_t t45 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
            if (t44 >= (uintptr_t)(t45)) gm_trap_index(t44, t45, gm_srch2l5nl06vsci, 249, 73);
            gm_t88rcucg1b7uk t46 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t44 * 4));
            void * t47 = gm_l4;
            uint32_t t48 = gm_l2;
            gm_tbl8il92l97ut t49;
            GM_ST(uint32_t, (uint8_t *)&t49 + 12, 0);
            GM_ST(void *, ((uint8_t *)&t49 + 0), t47);
            GM_ST(uint32_t, ((uint8_t *)&t49 + 8), t48);
            gm_teti411eujvf4 t50;
            memset(&t50, 0, sizeof t50);
            GM_ST(uint8_t, ((uint8_t *)&t50 + 0), ((uint8_t)19ULL));
            GM_ST(gm_tbl8il92l97ut, ((uint8_t *)&t50 + 8), t49);
            gm_tl874t2atnlh3 t51 = gm_l3;
            void * t52 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t46, &t50, t51);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t53 = 0;
            F.ret = t52;
            goto gm_fret;
        }
        goto ix36;
ie36:;
ix36:;
        goto me27;
mn33:;
ma29:;
        uint8_t t55 = GM_LD(uint8_t, (((uint8_t *)&t26) + 0));
        if (t55 != ((uint8_t)26ULL)) goto mn54;
        void * t56 = gm_l0;
        void * t57 = GM_LD(void *, (((uint8_t *)t56) + 0));
        gm_tl874t2atnlh3 t58 = gm_l3;
        void * t59 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t57, t58);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t60 = 0;
        F.ret = t59;
        goto gm_fret;
        goto me27;
mn54:;
ma30:;
        goto me27;
mn61:;
ma31:;
        gm_trap("no match arm matched the scrutinee", gm_srch2l5nl06vsci, 248, 5);
me27:;
    }
    {
        void * t63 = gm_l0;
        gm_tl874t2atnlh3 t64 = gm_l3;
        gm_tommf4j9upomc t65;
        memset((uint8_t *)&t65 + 20, 0, 4);
        void * t66 = gm_l0;
        gm_t88rcucg1b7uk t67 = gm_l5;
        gm_b16_8 t68 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t66, t67);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_b16_8, (uint8_t *)&t65 + 0, t68);
        uint32_t t69 = gm_l2;
        GM_ST(uint32_t, (uint8_t *)&t65 + 16, t69);
        gm_tabb710d9mq2e t70;
        gm_f_format_c0n18m1((void *)&t70, (void *)NULL, cx, NULL, &t65);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t70))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t70) + 8, gm_srch2l5nl06vsci, 253, 20);
        gm_b16_8 t71 = GM_LD(gm_b16_8, (((uint8_t *)&t70) + 8));
        gm_b16_8 t72 = GM_LD(gm_b16_8, ((uint8_t *)&t71));
        gm_b0_1 t73 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t63, t64, t72);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t74 = gm_l0;
        void * t75 = GM_LD(void *, (((uint8_t *)t74) + 0));
        gm_tl874t2atnlh3 t76 = gm_l3;
        void * t77 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t75, t76);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t78 = 0;
        F.ret = t77;
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
__attribute__((aligned(4))) void * gm_f_tuple_qkn0cor(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_tkn42u7na3t81 a2, gm_tl874t2atnlh3 a3) {
    struct gm_fr_tuple_qkn0cor F;
    void * gm_l0; /* cx */
    gm_b16_8 gm_l1; /* elems */
    gm_tkn42u7na3t81 gm_l2; /* expect */
    gm_tl874t2atnlh3 gm_l3; /* span */
    void * gm_l4; /* tys */
    gm_tkn42u7na3t81 gm_l5; /* want */
    gm_t88rcucg1b7uk gm_l6; /* x */
    gm_t88rcucg1b7uk gm_l7; /* w */
    gm_t88rcucg1b7uk gm_l8; /* inner */
    gm_t88rcucg1b7uk gm_l9; /* inner */
    gm_th2lqbo77pvsc gm_l10; /* expected_elems */
    gm_t88rcucg1b7uk gm_l11; /* w */
    gm_b16_8 gm_l12; /* es */
    uint8_t gm_l13; /* named */
    gm_b16_8 gm_l14; /* <tmp> */
    uint64_t gm_l15; /* <tmp> */
    gm_b16_8 gm_l18; /* <tmp> */
    uint64_t gm_l19; /* <tmp> */
    uint64_t gm_l20; /* i */
    gm_tkn42u7na3t81 gm_l22; /* exp */
    gm_b16_8 gm_l23; /* es */
    void * gm_l24; /* v */
    gm_b16_8 gm_l26; /* <tmp> */
    uint64_t gm_l27; /* <tmp> */
    uint64_t gm_l28; /* i */
    gm_t08olq97rscn3 gm_l30; /* n */
    gm_b16_8 gm_l32; /* <tmp> */
    uint64_t gm_l33; /* <tmp> */
    GM_STACK_CHECK(&F, 2284u);
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
        void * t4 = (void *)(((uint8_t *)t3) + 8);
        gm_l4 = t4;
    }
    {
        gm_tkn42u7na3t81 t5;
        memset(&t5, 0, sizeof t5);
        gm_l5 = t5;
    }
    {
        if (!((((uint8_t *)&gm_l2))[4] != 0)) goto ie6;
        gm_t88rcucg1b7uk t7 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l2));
        gm_l6 = t7;
        {
            void * t9 = gm_l0;
            void * t10 = (void *)(((uint8_t *)t9) + 96);
            void * t11 = gm_l4;
            gm_t88rcucg1b7uk t12 = gm_l6;
            gm_t88rcucg1b7uk t13 = gm_f_shallow_8ur803a((void *)NULL, cx, NULL, t10, t11, t12);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tkn42u7na3t81 t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 6, 0);
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t14), t13);
            ((uint8_t *)&t14)[4] = 1;
            gm_l5 = t14;
        }
        goto ix6;
ie6:;
ix6:;
    }
    {
        for (;;) {
            if (!((((uint8_t *)&gm_l5))[4] != 0)) goto lb15;
            gm_t88rcucg1b7uk t16 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l5));
            gm_l7 = t16;
            {
                void * t19 = gm_l4;
                gm_t88rcucg1b7uk t20 = gm_l7;
                gm_t53at80pfdus3 t21;
                gm_f_kind_jcnt4hm((void *)&t21, t19, GM_CONV(gm_csguankko4lmhd, t20));
                uint8_t t26 = GM_LD(uint8_t, (((uint8_t *)&t21) + 0));
                if (t26 != ((uint8_t)13ULL)) goto pa25;
                gm_t88rcucg1b7uk t27 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t21) + 8));
                gm_l8 = t27;
                goto po24;
pa25:;
                uint8_t t29 = GM_LD(uint8_t, (((uint8_t *)&t21) + 0));
                if (t29 != ((uint8_t)14ULL)) goto pa28;
                gm_t88rcucg1b7uk t30 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t21) + 8) + 4));
                gm_l8 = t30;
                goto po24;
pa28:;
                goto mn23;
po24:;
                {
                    void * t32 = gm_l0;
                    void * t33 = (void *)(((uint8_t *)t32) + 96);
                    void * t34 = gm_l4;
                    gm_t88rcucg1b7uk t35 = gm_l8;
                    gm_t88rcucg1b7uk t36 = gm_f_shallow_8ur803a((void *)NULL, cx, NULL, t33, t34, t35);
                    if (gm_unw.kind) {
                        goto gm_ll17;
                    }
                    gm_tkn42u7na3t81 t37;
                    GM_ST(uint8_t, (uint8_t *)&t37 + 5, 0);
                    GM_ST(uint16_t, (uint8_t *)&t37 + 6, 0);
                    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t37), t36);
                    ((uint8_t *)&t37)[4] = 1;
                    gm_l5 = t37;
                }
                goto me22;
mn23:;
                goto lb15;
                goto me22;
mn38:;
                gm_trap("no match arm matched the scrutinee", gm_srch2l5nl06vsci, 431, 9);
me22:;
            }
lc15:;
        }
gm_ll17:;
        if (gm_unw.frame == &F.h && gm_unw.label == 0) {
            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb15; }
            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc15; }
        }
        goto gm_lad;
lb15:;
    }
    {
        gm_b16_8 t40 = gm_l1;
        uint8_t t41 = gm_f_has_spread_bndtpd6(t40);
        if (!t41) goto ie39;
        {
            void * t43 = gm_l0;
            gm_b16_8 t44 = gm_l1;
            gm_tkn42u7na3t81 t45 = gm_l5;
            gm_tl874t2atnlh3 t46 = gm_l3;
            void * t47 = gm_f_spread_tuple_6180uqs((void *)NULL, cx, NULL, t43, t44, t45, t46);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t48 = 0;
            F.ret = t47;
            goto gm_fret;
        }
        goto ix39;
ie39:;
ix39:;
    }
    {
        gm_th2lqbo77pvsc t49;
        memset(&t49, 0, sizeof t49);
        gm_l10 = t49;
    }
    {
        if (!((((uint8_t *)&gm_l5))[4] != 0)) goto ie50;
        gm_t88rcucg1b7uk t51 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l5));
        gm_l11 = t51;
        {
            void * t54 = gm_l4;
            gm_t88rcucg1b7uk t55 = gm_l11;
            gm_t53at80pfdus3 t56;
            gm_f_kind_jcnt4hm((void *)&t56, t54, GM_CONV(gm_csguankko4lmhd, t55));
            uint8_t t57 = GM_LD(uint8_t, (((uint8_t *)&t56) + 0));
            if (t57 != ((uint8_t)12ULL)) goto ie53;
            gm_b16_8 t58 = GM_LD(gm_b16_8, ((((uint8_t *)&t56) + 8) + 0));
            gm_l12 = t58;
            uint64_t t59 = GM_LD(uintptr_t, ((uint8_t *)&gm_l12) + 8);
            uint64_t t60 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            uint8_t t61 = ((t59 == t60));
            if (!t61) goto ie53;
            {
                gm_b16_8 t63 = gm_l12;
                GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l10)) + 17, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l10)) + 18, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l10)) + 20, 0);
                ((uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l10)))[16] = 1;
                GM_ST(gm_b16_8, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l10)) + 0, t63);
            }
            goto ix53;
ie53:;
ix53:;
        }
        goto ix50;
ie50:;
ix50:;
    }
    {
        uint8_t t64 = 0;
        uint64_t t65 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint8_t t66 = (t65 > ((uint64_t)0ULL));
        if (t66) {
            uint64_t t67 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t68 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            if (t67 >= (uintptr_t)(t68)) gm_trap_index(t67, t68, gm_srch2l5nl06vsci, 441, 32);
            uint8_t t69;
            if ((((((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t67 * 136) + 0))[32] != 0)) goto mn71;
            t69 = ((uint8_t)0);
            goto me70;
mn71:;
            t69 = ((uint8_t)1);
            goto me70;
mn72:;
            gm_trap("no match arm matched the scrutinee", gm_srch2l5nl06vsci, 441, 32);
me70:;
            t64 = t69;
        } else t64 = 0;
        gm_l13 = t64;
    }
    {
        {
            gm_b16_8 t74 = gm_l1;
            gm_l14 = t74;
        }
        {
            gm_l15 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t76 = gm_l15;
                uint64_t t77 = GM_LD(uintptr_t, ((uint8_t *)&gm_l14) + 8);
                uint8_t t78 = (t76 < t77);
                if (!t78) goto lb75;
                {
                    uint64_t t81 = gm_l15;
                    uint64_t t82 = (uintptr_t)t81;
                    uint64_t t83 = GM_LD(uintptr_t, ((uint8_t *)&gm_l14) + 8);
                    gm_tv1o1j44ii4db t86 = GM_LD(gm_tv1o1j44ii4db, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l14)) + t82 * 136));
                    F.l16 = t86;
                    {
                        uint8_t t89;
                        if ((((((uint8_t *)&F.l16) + 0))[32] != 0)) goto mn91;
                        t89 = ((uint8_t)0);
                        goto me90;
mn91:;
                        t89 = ((uint8_t)1);
                        goto me90;
mn92:;
                        gm_trap("no match arm matched the scrutinee", gm_srch2l5nl06vsci, 443, 13);
me90:;
                        uint8_t t93 = gm_l13;
                        uint8_t t94 = (!(t89 == t93));
                        if (!t94) goto ie88;
                        {
                            void * t96 = gm_l0;
                            gm_tl874t2atnlh3 t97 = gm_l3;
                            gm_b16_8 t98;
                            GM_ST(void *, (uint8_t *)&t98, (void *)(gm_serasf77ce9g3));
                            GM_ST(uintptr_t, (uint8_t *)&t98 + 8, (uintptr_t)(51));
                            gm_b0_1 t99 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t96, t97, t98);
                            if (gm_unw.kind) {
                                goto gm_ll79;
                            }
                        }
                        {
                            goto lb75;
                        }
                        goto ix88;
ie88:;
ix88:;
                    }
                    goto me84;
mn85:;
                    gm_trap("no match arm matched the scrutinee", gm_srch2l5nl06vsci, 442, 5);
me84:;
                }
lc75:;
                {
                    uint64_t t101 = gm_l15;
                    uint64_t r102; bool o103 = __builtin_add_overflow(t101, ((uint64_t)1ULL), &r102);
                    uint64_t t104 = r102;
                    gm_l15 = t104;
                }
            }
gm_ll79:;
            if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb75; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc75; }
            }
            goto gm_lad;
lb75:;
        }
    }
    {
        gm_f_list_vp1djv1_2((void *)&F.l17, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t106 = gm_l1;
            gm_l18 = t106;
        }
        {
            gm_l19 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t108 = gm_l19;
                uint64_t t109 = GM_LD(uintptr_t, ((uint8_t *)&gm_l18) + 8);
                uint8_t t110 = (t108 < t109);
                if (!t110) goto lb107;
                {
                    gm_t2cmlgt3383cp t113;
                    uint64_t t114 = gm_l19;
                    GM_ST(uint64_t, (uint8_t *)&t113 + 0, t114);
                    uint64_t t115 = gm_l19;
                    uint64_t t116 = (uintptr_t)t115;
                    uint64_t t117 = GM_LD(uintptr_t, ((uint8_t *)&gm_l18) + 8);
                    memcpy((uint8_t *)&t113 + 8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l18)) + t116 * 136), 136);
                    uint64_t t120 = GM_LD(uint64_t, (((uint8_t *)&t113) + 0));
                    gm_l20 = t120;
                    F.l21 = (*(gm_tv1o1j44ii4db *)(((uint8_t *)&t113) + 8));
                    {
                        gm_tkn42u7na3t81 t122;
                        memset(&t122, 0, sizeof t122);
                        gm_l22 = t122;
                    }
                    {
                        if (!((((uint8_t *)&gm_l10))[16] != 0)) goto ie123;
                        gm_b16_8 t124 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l10));
                        gm_l23 = t124;
                        {
                            uint64_t t126 = gm_l20;
                            uint64_t t127 = (uintptr_t)t126;
                            uint64_t t128 = GM_LD(uintptr_t, ((uint8_t *)&gm_l23) + 8);
                            if (t127 >= (uintptr_t)(t128)) gm_trap_index(t127, t128, gm_srch2l5nl06vsci, 451, 41);
                            gm_t88rcucg1b7uk t129 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l23)) + t127 * 4));
                            gm_tkn42u7na3t81 t130;
                            GM_ST(uint8_t, (uint8_t *)&t130 + 5, 0);
                            GM_ST(uint16_t, (uint8_t *)&t130 + 6, 0);
                            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t130), t129);
                            ((uint8_t *)&t130)[4] = 1;
                            gm_l22 = t130;
                        }
                        goto ix123;
ie123:;
ix123:;
                    }
                    {
                        void * t131 = gm_l0;
                        void * t132 = (void *)(((uint8_t *)&F.l21) + 40);
                        gm_tkn42u7na3t81 t133 = gm_l22;
                        void * t134 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t131, t132, t133);
                        if (gm_unw.kind) {
                            goto gm_ll111;
                        }
                        gm_l24 = t134;
                    }
                    {
                        void * t135 = gm_l0;
                        void * t136 = gm_l24;
                        gm_tkn42u7na3t81 t137 = gm_l22;
                        void * t138 = gm_f_coerce_element_9ue7d7u((void *)NULL, cx, NULL, t135, t136, t137);
                        if (gm_unw.kind) {
                            goto gm_ll111;
                        }
                        gm_l24 = t138;
                    }
                    {
                        {
                            void * t140 = gm_l0;
                            void * t141 = gm_l24;
                            gm_b16_8 t142;
                            GM_ST(void *, (uint8_t *)&t142, (void *)(gm_sj4o6222mnbmq));
                            GM_ST(uintptr_t, (uint8_t *)&t142 + 8, (uintptr_t)(15));
                            uint8_t t143 = gm_f_reject_second_class_c561fm5((void *)NULL, cx, NULL, t140, t141, t142);
                            if (gm_unw.kind) {
                                goto gm_ll111;
                            }
                        }
                    }
                    {
                        void * t144 = (void *)((uint8_t *)&F.l17);
                        void * t145 = gm_l24;
                        gm_tftb1odc739t5 t146 = gm_f_push_d943vgp_2(t144, &(*(gm_t69m56meqefs8 *)((uint8_t *)t145)));
                        if (((((uint8_t *)&t146))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t146) + 1, gm_srch2l5nl06vsci, 455, 9);
                        gm_b0_1 t147 = GM_LD(gm_b0_1, (((uint8_t *)&t146) + 1));
                    }
                    goto me118;
mn119:;
                    gm_trap("no match arm matched the scrutinee", gm_srch2l5nl06vsci, 449, 5);
me118:;
                }
lc107:;
                {
                    uint64_t t149 = gm_l19;
                    uint64_t r150; bool o151 = __builtin_add_overflow(t149, ((uint64_t)1ULL), &r150);
                    uint64_t t152 = r150;
                    gm_l19 = t152;
                }
            }
gm_ll111:;
            if (gm_unw.frame == &F.h && gm_unw.label == 2) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb107; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc107; }
            }
            goto gm_lad;
lb107:;
        }
    }
    {
        gm_f_list_epvducg((void *)&F.l25, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        uint8_t t154 = gm_l13;
        if (!t154) goto ie153;
        {
            {
                gm_b16_8 t157 = gm_l1;
                gm_l26 = t157;
            }
            {
                gm_l27 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t159 = gm_l27;
                    uint64_t t160 = GM_LD(uintptr_t, ((uint8_t *)&gm_l26) + 8);
                    uint8_t t161 = (t159 < t160);
                    if (!t161) goto lb158;
                    {
                        gm_t2cmlgt3383cp t164;
                        uint64_t t165 = gm_l27;
                        GM_ST(uint64_t, (uint8_t *)&t164 + 0, t165);
                        uint64_t t166 = gm_l27;
                        uint64_t t167 = (uintptr_t)t166;
                        uint64_t t168 = GM_LD(uintptr_t, ((uint8_t *)&gm_l26) + 8);
                        memcpy((uint8_t *)&t164 + 8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l26)) + t167 * 136), 136);
                        uint64_t t171 = GM_LD(uint64_t, (((uint8_t *)&t164) + 0));
                        gm_l28 = t171;
                        F.l29 = (*(gm_tv1o1j44ii4db *)(((uint8_t *)&t164) + 8));
                        {
                            if (!(((((uint8_t *)&F.l29) + 0))[32] != 0)) goto ie173;
                            gm_t08olq97rscn3 t174 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&F.l29) + 0));
                            gm_l30 = t174;
                            {
                                gm_b16_8 t177 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l30) + 0));
                                gm_b16_8 t178 = GM_LD(gm_b16_8, (((uint8_t *)&F.l25) + 0));
                                uint8_t t179 = 0;
                                for (uintptr_t i = 0; i < (uintptr_t)(GM_LD(uintptr_t, ((uint8_t *)&t178) + 8)); i++) if (gm_eq0iqiubfm7e(((uint8_t *)GM_LD(void *, ((uint8_t *)&t178))) + i * 16, ((uint8_t *)&t177))) { t179 = 1; break; }
                                uint8_t t180 = (uint8_t)(t179);
                                if (!t180) goto ie176;
                                {
                                    void * t182 = gm_l0;
                                    gm_tl874t2atnlh3 t183 = gm_l3;
                                    gm_b16_8 t184 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l30) + 0));
                                    gm_tnpifv20irnph t185;
                                    GM_ST(gm_b16_8, ((uint8_t *)&t185 + 0), t184);
                                    gm_tabb710d9mq2e t186;
                                    gm_f_format_6nmb1bf((void *)&t186, (void *)NULL, cx, NULL, t185);
                                    if (gm_unw.kind) {
                                        goto gm_ll162;
                                    }
                                    if (((((uint8_t *)&t186))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t186) + 8, gm_srch2l5nl06vsci, 462, 36);
                                    gm_b16_8 t187 = GM_LD(gm_b16_8, (((uint8_t *)&t186) + 8));
                                    gm_b16_8 t188 = GM_LD(gm_b16_8, ((uint8_t *)&t187));
                                    gm_b0_1 t189 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t182, t183, t188);
                                    if (gm_unw.kind) {
                                        goto gm_ll162;
                                    }
                                }
                                goto ix176;
ie176:;
ix176:;
                            }
                            {
                                void * t190 = (void *)((uint8_t *)&F.l25);
                                gm_b16_8 t191 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l30) + 0));
                                gm_tftb1odc739t5 t192 = gm_f_push_3ef746b(t190, t191);
                                if (((((uint8_t *)&t192))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t192) + 1, gm_srch2l5nl06vsci, 464, 17);
                                gm_b0_1 t193 = GM_LD(gm_b0_1, (((uint8_t *)&t192) + 1));
                            }
                            goto ix173;
ie173:;
ix173:;
                        }
                        goto me169;
mn170:;
                        gm_trap("no match arm matched the scrutinee", gm_srch2l5nl06vsci, 459, 9);
me169:;
                    }
lc158:;
                    {
                        uint64_t t195 = gm_l27;
                        uint64_t r196; bool o197 = __builtin_add_overflow(t195, ((uint64_t)1ULL), &r196);
                        uint64_t t198 = r196;
                        gm_l27 = t198;
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
        goto ix153;
ie153:;
ix153:;
    }
    {
        gm_f_list_nof7vfj((void *)&F.l31, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t200 = GM_LD(gm_b16_8, (((uint8_t *)&F.l17) + 0));
            gm_l32 = t200;
        }
        {
            gm_l33 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t202 = gm_l33;
                uint64_t t203 = GM_LD(uintptr_t, ((uint8_t *)&gm_l32) + 8);
                uint8_t t204 = (t202 < t203);
                if (!t204) goto lb201;
                {
                    uint64_t t207 = gm_l33;
                    uint64_t t208 = (uintptr_t)t207;
                    uint64_t t209 = GM_LD(uintptr_t, ((uint8_t *)&gm_l32) + 8);
                    gm_t69m56meqefs8 t212 = GM_LD(gm_t69m56meqefs8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l32)) + t208 * 88));
                    F.l34 = t212;
                    {
                        void * t214 = (void *)((uint8_t *)&F.l31);
                        gm_t88rcucg1b7uk t215 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l34) + 0));
                        gm_tftb1odc739t5 t216 = gm_f_push_k3l9nic(t214, GM_CONV(gm_csguankko4lmhd, t215));
                        if (((((uint8_t *)&t216))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t216) + 1, gm_srch2l5nl06vsci, 469, 27);
                        gm_b0_1 t217 = GM_LD(gm_b0_1, (((uint8_t *)&t216) + 1));
                    }
                    goto me210;
mn211:;
                    gm_trap("no match arm matched the scrutinee", gm_srch2l5nl06vsci, 469, 5);
me210:;
                }
lc201:;
                {
                    uint64_t t219 = gm_l33;
                    uint64_t r220; bool o221 = __builtin_add_overflow(t219, ((uint64_t)1ULL), &r220);
                    uint64_t t222 = r220;
                    gm_l33 = t222;
                }
            }
lb201:;
        }
    }
    {
        void * t223 = gm_l4;
        gm_t6j6smj7qoctu t224;
        gm_b16_8 t225 = GM_LD(gm_b16_8, (((uint8_t *)&F.l31) + 0));
        gm_b16_8 t226 = GM_LD(gm_b16_8, ((uint8_t *)&t225));
        GM_ST(gm_b16_8, (uint8_t *)&t224 + 0, t226);
        gm_b16_8 t227 = GM_LD(gm_b16_8, (((uint8_t *)&F.l25) + 0));
        gm_b16_8 t228 = GM_LD(gm_b16_8, ((uint8_t *)&t227));
        GM_ST(gm_b16_8, (uint8_t *)&t224 + 16, t228);
        gm_t53at80pfdus3 t229;
        GM_ST(uint8_t, (uint8_t *)&t229 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t229 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t229 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t229 + 40, 0);
        GM_ST(uint64_t, (uint8_t *)&t229 + 48, 0);
        GM_ST(uint8_t, ((uint8_t *)&t229 + 0), ((uint8_t)12ULL));
        GM_ST(gm_t6j6smj7qoctu, ((uint8_t *)&t229 + 8), t224);
        gm_t88rcucg1b7uk t230 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t223, &t229);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b16_8 t231 = GM_LD(gm_b16_8, (((uint8_t *)&F.l17) + 0));
        gm_teti411eujvf4 t232;
        memset(&t232, 0, sizeof t232);
        GM_ST(uint8_t, ((uint8_t *)&t232 + 0), ((uint8_t)35ULL));
        GM_ST(gm_b16_8, ((uint8_t *)&t232 + 8), t231);
        gm_tl874t2atnlh3 t233 = gm_l3;
        void * t234 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t230, &t232, t233);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t235 = 0;
        F.ret = t234;
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
__attribute__((aligned(4))) void * gm_f_type_is_683ln2e(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, void * a2, void * a3, gm_tl874t2atnlh3 a4) {
    struct gm_fr_type_is_683ln2e F;
    void * gm_l0; /* cx */
    uint8_t gm_l1; /* negated */
    void * gm_l2; /* lhs */
    void * gm_l3; /* rhs */
    gm_tl874t2atnlh3 gm_l4; /* span */
    gm_t88rcucg1b7uk gm_l5; /* a */
    gm_t88rcucg1b7uk gm_l6; /* b */
    GM_STACK_CHECK(&F, 624u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    {
        uint8_t t3 = 0;
        void * t4 = gm_l0;
        void * t5 = gm_l2;
        uint8_t t6 = gm_f_denotes_type_jgfet92((void *)NULL, cx, NULL, t4, t5);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t7 = (uint8_t)!t6;
        if (!t7) {
            void * t8 = gm_l0;
            void * t9 = gm_l3;
            uint8_t t10 = gm_f_denotes_type_jgfet92((void *)NULL, cx, NULL, t8, t9);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t11 = (uint8_t)!t10;
            t3 = t11;
        } else t3 = 1;
        if (!t3) goto ie2;
        {
            void * t13 = gm_l0;
            gm_tl874t2atnlh3 t14 = gm_l4;
            gm_b16_8 t15;
            GM_ST(void *, (uint8_t *)&t15, (void *)(gm_sk531tvbsm11t));
            GM_ST(uintptr_t, (uint8_t *)&t15 + 8, (uintptr_t)(43));
            gm_b0_1 t16 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t13, t14, t15);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t17 = gm_l0;
            void * t18 = GM_LD(void *, (((uint8_t *)t17) + 0));
            gm_tl874t2atnlh3 t19 = gm_l4;
            void * t20 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t18, t19);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t21 = 0;
            F.ret = t20;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t22 = gm_l0;
        void * t23 = gm_l2;
        gm_t88rcucg1b7uk t24 = gm_f_resolve_ty_uk6c1lp((void *)NULL, cx, NULL, t22, t23);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l5 = t24;
    }
    {
        void * t25 = gm_l0;
        void * t26 = gm_l3;
        gm_t88rcucg1b7uk t27 = gm_f_resolve_ty_uk6c1lp((void *)NULL, cx, NULL, t25, t26);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l6 = t27;
    }
    {
        uint8_t t29 = 0;
        void * t30 = gm_l0;
        void * t31 = GM_LD(void *, (((uint8_t *)t30) + 0));
        void * t32 = (void *)(((uint8_t *)t31) + 8);
        gm_t88rcucg1b7uk t33 = gm_l5;
        uint8_t t34 = gm_f_is_error_8k27gd1(t32, GM_CONV(gm_csguankko4lmhd, t33));
        if (!t34) {
            void * t35 = gm_l0;
            void * t36 = GM_LD(void *, (((uint8_t *)t35) + 0));
            void * t37 = (void *)(((uint8_t *)t36) + 8);
            gm_t88rcucg1b7uk t38 = gm_l6;
            uint8_t t39 = gm_f_is_error_8k27gd1(t37, GM_CONV(gm_csguankko4lmhd, t38));
            t29 = t39;
        } else t29 = 1;
        if (!t29) goto ie28;
        {
            void * t41 = gm_l0;
            void * t42 = GM_LD(void *, (((uint8_t *)t41) + 0));
            gm_tl874t2atnlh3 t43 = gm_l4;
            void * t44 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t42, t43);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t45 = 0;
            F.ret = t44;
            goto gm_fret;
        }
        goto ix28;
ie28:;
ix28:;
    }
    {
        uint8_t t47 = 0;
        void * t48 = gm_l0;
        void * t49 = GM_LD(void *, (((uint8_t *)t48) + 0));
        void * t50 = (void *)(((uint8_t *)t49) + 8);
        gm_t88rcucg1b7uk t51 = gm_l5;
        uint8_t t52 = gm_f_has_param_u9t0efs(t50, GM_CONV(gm_csguankko4lmhd, t51));
        if (!t52) {
            void * t53 = gm_l0;
            void * t54 = GM_LD(void *, (((uint8_t *)t53) + 0));
            void * t55 = (void *)(((uint8_t *)t54) + 8);
            gm_t88rcucg1b7uk t56 = gm_l6;
            uint8_t t57 = gm_f_has_param_u9t0efs(t55, GM_CONV(gm_csguankko4lmhd, t56));
            t47 = t57;
        } else t47 = 1;
        if (!t47) goto ie46;
        {
            void * t59 = gm_l0;
            void * t60 = GM_LD(void *, (((uint8_t *)t59) + 0));
            void * t61 = (void *)(((uint8_t *)t60) + 8);
            gm_t88rcucg1b7uk t62 = gm_f_bool_ty_pn2p277((void *)NULL, cx, NULL, t61);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t88rcucg1b7uk t63 = gm_l5;
            gm_t88rcucg1b7uk t64 = gm_l6;
            uint8_t t65 = gm_l1;
            gm_t3rcjibl8kkg4 t66;
            GM_ST(uint8_t, (uint8_t *)&t66 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t66 + 10, 0);
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t66 + 0), t63);
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t66 + 4), t64);
            GM_ST(uint8_t, ((uint8_t *)&t66 + 8), t65);
            gm_teti411eujvf4 t67;
            memset(&t67, 0, sizeof t67);
            GM_ST(uint8_t, ((uint8_t *)&t67 + 0), ((uint8_t)13ULL));
            GM_ST(gm_t3rcjibl8kkg4, ((uint8_t *)&t67 + 8), t66);
            gm_tl874t2atnlh3 t68 = gm_l4;
            void * t69 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t62, &t67, t68);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t70 = 0;
            F.ret = t69;
            goto gm_fret;
        }
        goto ix46;
ie46:;
ix46:;
    }
    {
        void * t71 = gm_l0;
        void * t72 = GM_LD(void *, (((uint8_t *)t71) + 0));
        void * t73 = (void *)(((uint8_t *)t72) + 8);
        gm_t88rcucg1b7uk t74 = gm_f_bool_ty_pn2p277((void *)NULL, cx, NULL, t73);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t88rcucg1b7uk t75 = gm_l5;
        gm_t88rcucg1b7uk t76 = gm_l6;
        uint8_t t77 = (gm_eq9m6mvtmgde(((uint8_t *)&t75), ((uint8_t *)&t76)));
        uint8_t t78 = gm_l1;
        uint8_t t79 = (!(t77 == t78));
        gm_teti411eujvf4 t80;
        memset(&t80, 0, sizeof t80);
        GM_ST(uint8_t, ((uint8_t *)&t80 + 0), ((uint8_t)4ULL));
        GM_ST(uint8_t, ((uint8_t *)&t80 + 8), t79);
        gm_tl874t2atnlh3 t81 = gm_l4;
        void * t82 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t74, &t80, t81);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t83 = 0;
        F.ret = t82;
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
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_type_of_name_oqtujpn(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_type_of_name_oqtujpn F;
    void * gm_l0; /* cx */
    void * gm_l1; /* e */
    void * gm_l2; /* base */
    gm_t08olq97rscn3 gm_l3; /* name */
    uint64_t gm_l4; /* m */
    uint64_t gm_l5; /* d */
    gm_t88rcucg1b7uk gm_l6; /* t */
    void * gm_l7; /* callee */
    uint64_t gm_l8; /* d */
    gm_t08olq97rscn3 gm_l9; /* id */
    void * gm_l10; /* base */
    gm_t08olq97rscn3 gm_l11; /* name */
    uint64_t gm_l12; /* m */
    gm_t88rcucg1b7uk gm_l13; /* t */
    gm_t08olq97rscn3 gm_l14; /* id */
    gm_t88rcucg1b7uk gm_l16; /* t */
    uint64_t gm_l17; /* d */
    gm_t88rcucg1b7uk gm_l18; /* t */
    GM_STACK_CHECK(&F, 2317u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t3 = gm_l1;
        uint8_t t4 = GM_LD(uint8_t, ((((uint8_t *)t3) + 8) + 0));
        if (t4 != ((uint8_t)22ULL)) goto ie2;
        void * t5 = GM_LD(void *, (((((uint8_t *)t3) + 8) + 8) + 0));
        gm_l2 = t5;
        gm_t08olq97rscn3 t6 = GM_LD(gm_t08olq97rscn3, (((((uint8_t *)t3) + 8) + 8) + 8));
        gm_l3 = t6;
        {
            void * t9 = gm_l0;
            void * t10 = gm_l2;
            gm_tf5srh4mp4b3i t11 = gm_f_module_of_n0d5f6n((void *)NULL, cx, NULL, t9, t10);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (!((((uint8_t *)&t11))[8] != 0)) goto ie8;
            uint64_t t12 = GM_LD(uint64_t, ((uint8_t *)&t11));
            gm_l4 = t12;
            {
                void * t14 = gm_l0;
                void * t15 = GM_LD(void *, (((uint8_t *)t14) + 0));
                uint64_t t16 = gm_l4;
                void * t17 = gm_l0;
                uint64_t t18 = GM_LD(uint64_t, (((uint8_t *)t17) + 8));
                gm_tf5srh4mp4b3i t19 = gm_f_module_member_5fd2qm9((void *)NULL, cx, NULL, t15, t16, &(*(gm_t08olq97rscn3 *)((uint8_t *)&gm_l3)), t18);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint64_t t20;
                if (((((uint8_t *)&t19))[8] != 0)) {
                    uint64_t t21 = GM_LD(uint64_t, (((uint8_t *)&t19) + 0));
                    t20 = t21;
                } else {
                    gm_tkn42u7na3t81 t22;
                    memset(&t22, 0, sizeof t22);
                    uint8_t t23 = 0;
                    F.ret = t22;
                    goto gm_fret;
                }
                gm_l5 = t20;
            }
            {
                void * t25 = gm_l0;
                void * t26 = GM_LD(void *, (((uint8_t *)t25) + 0));
                uint64_t t27 = gm_l5;
                gm_t6qooom8qa2do t28;
                gm_f_resolve_decl_pus042v((void *)&t28, (void *)NULL, cx, NULL, t26, t27);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t29 = GM_LD(uint8_t, (((uint8_t *)&t28) + 0));
                if (t29 != ((uint8_t)0ULL)) goto ie24;
                gm_t88rcucg1b7uk t30 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t28) + 8));
                gm_l6 = t30;
                {
                    gm_t88rcucg1b7uk t32 = gm_l6;
                    gm_tkn42u7na3t81 t33;
                    GM_ST(uint8_t, (uint8_t *)&t33 + 5, 0);
                    GM_ST(uint16_t, (uint8_t *)&t33 + 6, 0);
                    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t33), t32);
                    ((uint8_t *)&t33)[4] = 1;
                    uint8_t t34 = 0;
                    F.ret = t33;
                    goto gm_fret;
                }
                goto ix24;
ie24:;
                {
                    gm_tkn42u7na3t81 t36;
                    memset(&t36, 0, sizeof t36);
                    uint8_t t37 = 0;
                    F.ret = t36;
                    goto gm_fret;
                }
ix24:;
            }
            goto ix8;
ie8:;
ix8:;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t39 = gm_l1;
        uint8_t t40 = GM_LD(uint8_t, ((((uint8_t *)t39) + 8) + 0));
        if (t40 != ((uint8_t)19ULL)) goto ie38;
        void * t41 = GM_LD(void *, (((((uint8_t *)t39) + 8) + 8) + 0));
        gm_l7 = t41;
        {
            gm_l8 = ((uint64_t)0ULL);
        }
        {
            void * t43 = gm_l7;
            uint8_t t49 = GM_LD(uint8_t, ((((uint8_t *)t43) + 8) + 0));
            switch (t49) { case ((uint8_t)0ULL): goto ma47; case ((uint8_t)1ULL): goto ma47; case ((uint8_t)2ULL): goto ma47; case ((uint8_t)3ULL): goto ma47; case ((uint8_t)4ULL): goto ma47; case ((uint8_t)5ULL): goto ma47; case ((uint8_t)6ULL): goto ma45; case ((uint8_t)7ULL): goto ma47; case ((uint8_t)8ULL): goto ma47; case ((uint8_t)9ULL): goto ma47; case ((uint8_t)10ULL): goto ma47; case ((uint8_t)11ULL): goto ma47; case ((uint8_t)12ULL): goto ma47; case ((uint8_t)13ULL): goto ma47; case ((uint8_t)14ULL): goto ma47; case ((uint8_t)15ULL): goto ma47; case ((uint8_t)16ULL): goto ma47; case ((uint8_t)17ULL): goto ma47; case ((uint8_t)18ULL): goto ma47; case ((uint8_t)19ULL): goto ma47; case ((uint8_t)20ULL): goto ma47; case ((uint8_t)21ULL): goto ma47; case ((uint8_t)22ULL): goto ma46; case ((uint8_t)23ULL): goto ma47; case ((uint8_t)24ULL): goto ma47; case ((uint8_t)25ULL): goto ma47; case ((uint8_t)26ULL): goto ma47; case ((uint8_t)27ULL): goto ma47; case ((uint8_t)28ULL): goto ma47; case ((uint8_t)29ULL): goto ma47; case ((uint8_t)30ULL): goto ma47; case ((uint8_t)31ULL): goto ma47; case ((uint8_t)32ULL): goto ma47; case ((uint8_t)33ULL): goto ma47; case ((uint8_t)34ULL): goto ma47; case ((uint8_t)35ULL): goto ma47; case ((uint8_t)36ULL): goto ma47; case ((uint8_t)37ULL): goto ma47; case ((uint8_t)38ULL): goto ma47; case ((uint8_t)39ULL): goto ma47; case ((uint8_t)40ULL): goto ma47; case ((uint8_t)41ULL): goto ma47; case ((uint8_t)42ULL): goto ma47; case ((uint8_t)43ULL): goto ma47; case ((uint8_t)44ULL): goto ma47; case ((uint8_t)45ULL): goto ma47; case ((uint8_t)46ULL): goto ma47; case ((uint8_t)47ULL): goto ma47; case ((uint8_t)48ULL): goto ma47; case ((uint8_t)49ULL): goto ma47; case ((uint8_t)50ULL): goto ma47; default: goto ma47; }
ma45:;
            uint8_t t51 = GM_LD(uint8_t, ((((uint8_t *)t43) + 8) + 0));
            if (t51 != ((uint8_t)6ULL)) goto mn50;
            gm_t08olq97rscn3 t52 = GM_LD(gm_t08olq97rscn3, ((((uint8_t *)t43) + 8) + 8));
            gm_l9 = t52;
            {
                void * t55 = gm_l0;
                gm_b16_8 t56 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l9) + 0));
                gm_tqvu78rpi6q1h t57;
                gm_f_lookup_local_6f38t7p((void *)&t57, t55, t56);
                uint8_t t58;
                if (((((uint8_t *)&t57))[208] != 0)) goto mn60;
                t58 = ((uint8_t)0);
                goto me59;
mn60:;
                t58 = ((uint8_t)1);
                goto me59;
mn61:;
                gm_trap("no match arm matched the scrutinee", gm_srch2l5nl06vsci, 1058, 20);
me59:;
                if (!t58) goto ie54;
                {
                    gm_tkn42u7na3t81 t63;
                    memset(&t63, 0, sizeof t63);
                    uint8_t t64 = 0;
                    F.ret = t63;
                    goto gm_fret;
                }
                goto ix54;
ie54:;
ix54:;
            }
            {
                void * t65 = gm_l0;
                void * t66 = GM_LD(void *, (((uint8_t *)t65) + 0));
                void * t67 = gm_l0;
                uint64_t t68 = GM_LD(uint64_t, (((uint8_t *)t67) + 8));
                gm_b16_8 t69 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l9) + 0));
                gm_tf5srh4mp4b3i t70 = gm_f_lookup_module_name_cukfp7t((void *)NULL, cx, NULL, t66, t68, t69);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint64_t t71;
                if (((((uint8_t *)&t70))[8] != 0)) {
                    uint64_t t72 = GM_LD(uint64_t, (((uint8_t *)&t70) + 0));
                    t71 = t72;
                } else {
                    gm_tkn42u7na3t81 t73;
                    memset(&t73, 0, sizeof t73);
                    uint8_t t74 = 0;
                    F.ret = t73;
                    goto gm_fret;
                }
                gm_l8 = t71;
            }
            goto me44;
mn50:;
ma46:;
            uint8_t t76 = GM_LD(uint8_t, ((((uint8_t *)t43) + 8) + 0));
            if (t76 != ((uint8_t)22ULL)) goto mn75;
            void * t77 = GM_LD(void *, (((((uint8_t *)t43) + 8) + 8) + 0));
            gm_l10 = t77;
            gm_t08olq97rscn3 t78 = GM_LD(gm_t08olq97rscn3, (((((uint8_t *)t43) + 8) + 8) + 8));
            gm_l11 = t78;
            {
                void * t80 = gm_l0;
                void * t81 = gm_l10;
                gm_tf5srh4mp4b3i t82 = gm_f_module_of_n0d5f6n((void *)NULL, cx, NULL, t80, t81);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint64_t t83;
                if (((((uint8_t *)&t82))[8] != 0)) {
                    uint64_t t84 = GM_LD(uint64_t, (((uint8_t *)&t82) + 0));
                    t83 = t84;
                } else {
                    gm_tkn42u7na3t81 t85;
                    memset(&t85, 0, sizeof t85);
                    uint8_t t86 = 0;
                    F.ret = t85;
                    goto gm_fret;
                }
                gm_l12 = t83;
            }
            {
                void * t87 = gm_l0;
                void * t88 = GM_LD(void *, (((uint8_t *)t87) + 0));
                void * t89 = GM_LD(void *, (((uint8_t *)t88) + 0));
                uint64_t t90 = gm_l12;
                gm_b16_8 t91 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l11) + 0));
                gm_tf5srh4mp4b3i t92 = gm_f_lookup_59juqoo(t89, t90, t91);
                uint64_t t93;
                if (((((uint8_t *)&t92))[8] != 0)) {
                    uint64_t t94 = GM_LD(uint64_t, (((uint8_t *)&t92) + 0));
                    t93 = t94;
                } else {
                    gm_tkn42u7na3t81 t95;
                    memset(&t95, 0, sizeof t95);
                    uint8_t t96 = 0;
                    F.ret = t95;
                    goto gm_fret;
                }
                gm_l8 = t93;
            }
            {
                void * t98 = gm_l0;
                void * t99 = GM_LD(void *, (((uint8_t *)t98) + 0));
                uint64_t t100 = gm_l8;
                void * t101 = gm_l0;
                uint64_t t102 = GM_LD(uint64_t, (((uint8_t *)t101) + 8));
                uint8_t t103 = gm_f_decl_visible_bd168ti(t99, t100, t102);
                uint8_t t104 = (uint8_t)!t103;
                if (!t104) goto ie97;
                {
                    gm_tkn42u7na3t81 t106;
                    memset(&t106, 0, sizeof t106);
                    uint8_t t107 = 0;
                    F.ret = t106;
                    goto gm_fret;
                }
                goto ix97;
ie97:;
ix97:;
            }
            goto me44;
mn75:;
ma47:;
            gm_tkn42u7na3t81 t109;
            memset(&t109, 0, sizeof t109);
            uint8_t t110 = 0;
            F.ret = t109;
            goto gm_fret;
            goto me44;
mn108:;
ma48:;
            gm_trap("no match arm matched the scrutinee", gm_srch2l5nl06vsci, 1056, 9);
me44:;
        }
        {
            void * t112 = gm_l0;
            void * t113 = GM_LD(void *, (((uint8_t *)t112) + 0));
            uint64_t t114 = gm_l8;
            gm_t6qooom8qa2do t115;
            gm_f_resolve_decl_pus042v((void *)&t115, (void *)NULL, cx, NULL, t113, t114);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t116;
            uint8_t t119 = GM_LD(uint8_t, (((uint8_t *)&t115) + 0));
            if (t119 != ((uint8_t)1ULL)) goto mn118;
            t116 = ((uint8_t)0);
            goto me117;
mn118:;
            t116 = ((uint8_t)1);
            goto me117;
mn120:;
            gm_trap("no match arm matched the scrutinee", gm_srch2l5nl06vsci, 1068, 12);
me117:;
            if (!t116) goto ie111;
            {
                gm_tkn42u7na3t81 t122;
                memset(&t122, 0, sizeof t122);
                uint8_t t123 = 0;
                F.ret = t122;
                goto gm_fret;
            }
            goto ix111;
ie111:;
ix111:;
        }
        {
            void * t124 = gm_l0;
            void * t125 = gm_l1;
            gm_t88rcucg1b7uk t126 = gm_f_resolve_ty_uk6c1lp((void *)NULL, cx, NULL, t124, t125);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l13 = t126;
        }
        {
            void * t128 = gm_l0;
            void * t129 = GM_LD(void *, (((uint8_t *)t128) + 0));
            void * t130 = (void *)(((uint8_t *)t129) + 8);
            gm_t88rcucg1b7uk t131 = gm_l13;
            uint8_t t132 = gm_f_is_error_8k27gd1(t130, GM_CONV(gm_csguankko4lmhd, t131));
            if (!t132) goto ie127;
            {
                gm_tkn42u7na3t81 t134;
                memset(&t134, 0, sizeof t134);
                uint8_t t135 = 0;
                F.ret = t134;
                goto gm_fret;
            }
            goto ix127;
ie127:;
ix127:;
        }
        {
            gm_t88rcucg1b7uk t136 = gm_l13;
            gm_tkn42u7na3t81 t137;
            GM_ST(uint8_t, (uint8_t *)&t137 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t137 + 6, 0);
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t137), t136);
            ((uint8_t *)&t137)[4] = 1;
            uint8_t t138 = 0;
            F.ret = t137;
            goto gm_fret;
        }
        goto ix38;
ie38:;
ix38:;
    }
    {
        void * t140 = gm_l1;
        uint8_t t141 = GM_LD(uint8_t, ((((uint8_t *)t140) + 8) + 0));
        if (t141 != ((uint8_t)6ULL)) goto ie139;
        gm_t08olq97rscn3 t142 = GM_LD(gm_t08olq97rscn3, ((((uint8_t *)t140) + 8) + 8));
        gm_l14 = t142;
        {
            void * t145 = gm_l0;
            gm_b16_8 t146 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l14) + 0));
            gm_tqvu78rpi6q1h t147;
            gm_f_lookup_local_6f38t7p((void *)&t147, t145, t146);
            if (!((((uint8_t *)&t147))[208] != 0)) goto ie144;
            F.l15 = (*(gm_t930q5l38hdap *)(((uint8_t *)&t147) + 0));
            {
                uint8_t t150 = GM_LD(uint8_t, (((uint8_t *)&F.l15) + 0));
                if (t150 != ((uint8_t)5ULL)) goto ie149;
                gm_t88rcucg1b7uk t151 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l15) + 8));
                gm_l16 = t151;
                {
                    gm_t88rcucg1b7uk t153 = gm_l16;
                    gm_tkn42u7na3t81 t154;
                    GM_ST(uint8_t, (uint8_t *)&t154 + 5, 0);
                    GM_ST(uint16_t, (uint8_t *)&t154 + 6, 0);
                    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t154), t153);
                    ((uint8_t *)&t154)[4] = 1;
                    uint8_t t155 = 0;
                    F.ret = t154;
                    goto gm_fret;
                }
                goto ix149;
ie149:;
ix149:;
            }
            {
                gm_tkn42u7na3t81 t156;
                memset(&t156, 0, sizeof t156);
                uint8_t t157 = 0;
                F.ret = t156;
                goto gm_fret;
            }
            goto ix144;
ie144:;
ix144:;
        }
        {
            void * t159 = gm_l0;
            void * t160 = GM_LD(void *, (((uint8_t *)t159) + 0));
            void * t161 = gm_l0;
            uint64_t t162 = GM_LD(uint64_t, (((uint8_t *)t161) + 8));
            gm_b16_8 t163 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l14) + 0));
            gm_tf5srh4mp4b3i t164 = gm_f_lookup_module_name_cukfp7t((void *)NULL, cx, NULL, t160, t162, t163);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (!((((uint8_t *)&t164))[8] != 0)) goto ie158;
            uint64_t t165 = GM_LD(uint64_t, ((uint8_t *)&t164));
            gm_l17 = t165;
            {
                void * t168 = gm_l0;
                void * t169 = GM_LD(void *, (((uint8_t *)t168) + 0));
                uint64_t t170 = gm_l17;
                gm_t6qooom8qa2do t171;
                gm_f_resolve_decl_pus042v((void *)&t171, (void *)NULL, cx, NULL, t169, t170);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t172 = GM_LD(uint8_t, (((uint8_t *)&t171) + 0));
                if (t172 != ((uint8_t)0ULL)) goto ie167;
                gm_t88rcucg1b7uk t173 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t171) + 8));
                gm_l18 = t173;
                {
                    gm_t88rcucg1b7uk t175 = gm_l18;
                    gm_tkn42u7na3t81 t176;
                    GM_ST(uint8_t, (uint8_t *)&t176 + 5, 0);
                    GM_ST(uint16_t, (uint8_t *)&t176 + 6, 0);
                    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t176), t175);
                    ((uint8_t *)&t176)[4] = 1;
                    uint8_t t177 = 0;
                    F.ret = t176;
                    goto gm_fret;
                }
                goto ix167;
ie167:;
                {
                    gm_tkn42u7na3t81 t179;
                    memset(&t179, 0, sizeof t179);
                    uint8_t t180 = 0;
                    F.ret = t179;
                    goto gm_fret;
                }
ix167:;
            }
            goto ix158;
ie158:;
ix158:;
        }
        {
            void * t181 = gm_l0;
            void * t182 = GM_LD(void *, (((uint8_t *)t181) + 0));
            gm_b16_8 t183 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l14) + 0));
            gm_tkn42u7na3t81 t184 = gm_f_builtin_type_64h714c((void *)NULL, cx, NULL, t182, t183);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t185 = 0;
            F.ret = t184;
            goto gm_fret;
        }
        goto ix139;
ie139:;
        {
            gm_tkn42u7na3t81 t187;
            memset(&t187, 0, sizeof t187);
            uint8_t t188 = 0;
            F.ret = t187;
            goto gm_fret;
        }
ix139:;
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
extern __attribute__((aligned(4))) uint8_t gm_f_type_operand_ta2mhln(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) void * gm_f_unary_asf0eot(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t9qkh0rsooc8d a1, void * a2, gm_tkn42u7na3t81 a3, gm_tl874t2atnlh3 a4) {
    struct gm_fr_unary_asf0eot F;
    void * gm_l0; /* cx */
    gm_t9qkh0rsooc8d gm_l1; /* op */
    void * gm_l2; /* operand */
    gm_tkn42u7na3t81 gm_l3; /* expect */
    gm_tl874t2atnlh3 gm_l4; /* span */
    void * gm_l5; /* v */
    gm_t88rcucg1b7uk gm_l6; /* st */
    gm_t88rcucg1b7uk gm_l7; /* u */
    void * gm_l8; /* x */
    void * gm_l9; /* n */
    void * gm_l11; /* v */
    gm_t88rcucg1b7uk gm_l12; /* t */
    gm_t88rcucg1b7uk gm_l13; /* inner */
    void * gm_l14; /* r */
    GM_STACK_CHECK(&F, 2094u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    {
        uint8_t t10 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        switch (t10) { case ((uint8_t)0ULL): goto ma3; case ((uint8_t)1ULL): goto ma4; case ((uint8_t)2ULL): goto ma6; case ((uint8_t)3ULL): goto ma7; case ((uint8_t)4ULL): goto ma8; case ((uint8_t)5ULL): goto ma5; default: goto ma9; }
ma3:;
        uint8_t t12 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t12 != ((uint8_t)0ULL)) goto mn11;
        void * t13 = gm_l0;
        void * t14 = gm_l2;
        gm_tkn42u7na3t81 t15 = gm_l3;
        gm_tl874t2atnlh3 t16 = gm_l4;
        void * t17 = gm_f_neg_expr_p72q1ni((void *)NULL, cx, NULL, t13, t14, t15, t16);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t18 = 0;
        F.ret = t17;
        goto gm_fret;
        goto me2;
mn11:;
ma4:;
        uint8_t t20 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t20 != ((uint8_t)1ULL)) goto mn19;
        {
            void * t22 = gm_l0;
            void * t23 = gm_l2;
            gm_tkn42u7na3t81 t24 = gm_l3;
            void * t25 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t22, t23, t24);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l5 = t25;
        }
        {
            void * t26 = gm_l0;
            void * t27 = (void *)(((uint8_t *)t26) + 96);
            void * t28 = gm_l0;
            void * t29 = GM_LD(void *, (((uint8_t *)t28) + 0));
            void * t30 = gm_l5;
            gm_t88rcucg1b7uk t31 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t30) + 0));
            gm_t88rcucg1b7uk t32 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t27, t29, t31);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l6 = t32;
        }
        {
            void * t34 = gm_l0;
            void * t35 = GM_LD(void *, (((uint8_t *)t34) + 0));
            void * t36 = (void *)(((uint8_t *)t35) + 8);
            gm_t88rcucg1b7uk t37 = gm_l6;
            uint8_t t38 = gm_f_is_vector_gkq6b88(t36, GM_CONV(gm_csguankko4lmhd, t37));
            if (!t38) goto ie33;
            {
                void * t40 = gm_l0;
                gm_t9qkh0rsooc8d t41;
                GM_ST(uint8_t, ((uint8_t *)&t41 + 0), ((uint8_t)1ULL));
                void * t42 = gm_l5;
                gm_tl874t2atnlh3 t43 = gm_l4;
                void * t44 = gm_f_vector_unary_2mbe97t((void *)NULL, cx, NULL, t40, t41, t42, t43);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t45 = 0;
                F.ret = t44;
                goto gm_fret;
            }
            goto ix33;
ie33:;
ix33:;
        }
        {
            void * t47 = gm_l0;
            gm_t88rcucg1b7uk t48 = gm_l6;
            uint8_t t49 = gm_f_is_compound_number_kbipjl5((void *)NULL, cx, NULL, t47, t48);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (!t49) goto ie46;
            {
                void * t51 = gm_l0;
                gm_t9qkh0rsooc8d t52;
                GM_ST(uint8_t, ((uint8_t *)&t52 + 0), ((uint8_t)1ULL));
                void * t53 = gm_l5;
                gm_tl874t2atnlh3 t54 = gm_l4;
                void * t55 = gm_f_compound_number_unary_4h478fs((void *)NULL, cx, NULL, t51, t52, t53, t54);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t56 = 0;
                F.ret = t55;
                goto gm_fret;
            }
            goto ix46;
ie46:;
ix46:;
        }
        {
            void * t58 = gm_l0;
            gm_t88rcucg1b7uk t59 = gm_l6;
            gm_b16_8 t60;
            GM_ST(void *, (uint8_t *)&t60, (void *)(gm_sa831sfid0hf3));
            GM_ST(uintptr_t, (uint8_t *)&t60 + 8, (uintptr_t)(1));
            gm_tl874t2atnlh3 t61 = gm_l4;
            gm_tkn42u7na3t81 t62 = gm_f_bits_operand_d1ppvrt((void *)NULL, cx, NULL, t58, t59, t60, t61);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (!((((uint8_t *)&t62))[4] != 0)) goto ie57;
            gm_t88rcucg1b7uk t63 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&t62));
            gm_l7 = t63;
            {
                void * t65 = gm_l5;
                gm_t88rcucg1b7uk t66 = gm_l7;
                void * t67 = gm_f_bits_convert_8rcef5o((void *)NULL, cx, NULL, t65, t66);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l8 = t67;
            }
            {
                gm_t88rcucg1b7uk t68 = gm_l7;
                gm_t9qkh0rsooc8d t69;
                GM_ST(uint8_t, ((uint8_t *)&t69 + 0), ((uint8_t)1ULL));
                void * t70 = gm_l8;
                gm_t8ghgl2jj0qha t71;
                GM_ST(uint8_t, (uint8_t *)&t71 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t71 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t71 + 4, 0);
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t71 + 0), t69);
                GM_ST(void *, ((uint8_t *)&t71 + 8), t70);
                gm_teti411eujvf4 t72;
                memset(&t72, 0, sizeof t72);
                GM_ST(uint8_t, ((uint8_t *)&t72 + 0), ((uint8_t)29ULL));
                GM_ST(gm_t8ghgl2jj0qha, ((uint8_t *)&t72 + 8), t71);
                gm_tl874t2atnlh3 t73 = gm_l4;
                void * t74 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t68, &t72, t73);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l9 = t74;
            }
            {
                void * t76 = gm_l0;
                gm_t88rcucg1b7uk t77 = gm_l6;
                gm_thrdpodrs8pgt t78;
                gm_f_bits_padding_mask_d4bk9a4((void *)&t78, (void *)NULL, cx, NULL, t76, t77);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (!((((uint8_t *)&t78))[48] != 0)) goto ie75;
                F.l10 = (*(gm_tbci5ospk5nnn *)(((uint8_t *)&t78) + 0));
                {
                    gm_t88rcucg1b7uk t80 = gm_l7;
                    gm_trl9d7m1k5lbd t81;
                    memset((uint8_t *)&t81 + 2, 0, 6);
                    gm_ts6oqppjh0ba3 t82;
                    GM_ST(uint8_t, (uint8_t *)&t82 + 1, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t82 + 0), ((uint8_t)7ULL));
                    GM_ST(gm_ts6oqppjh0ba3, (uint8_t *)&t81 + 0, t82);
                    void * t83 = gm_l9;
                    GM_ST(void *, (uint8_t *)&t81 + 8, t83);
                    gm_t88rcucg1b7uk t84 = gm_l7;
                    gm_tbci5ospk5nnn t85 = F.l10;
                    gm_teti411eujvf4 t86;
                    GM_ST(uint8_t, (uint8_t *)&t86 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t86 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t86 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t86 + 56, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t86 + 0), ((uint8_t)0ULL));
                    GM_ST(gm_tbci5ospk5nnn, ((uint8_t *)&t86 + 8), t85);
                    gm_tl874t2atnlh3 t87 = gm_l4;
                    void * t88 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t84, &t86, t87);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    GM_ST(void *, (uint8_t *)&t81 + 16, t88);
                    gm_teti411eujvf4 t89;
                    memset(&t89, 0, sizeof t89);
                    GM_ST(uint8_t, ((uint8_t *)&t89 + 0), ((uint8_t)30ULL));
                    GM_ST(gm_trl9d7m1k5lbd, ((uint8_t *)&t89 + 8), t81);
                    gm_tl874t2atnlh3 t90 = gm_l4;
                    void * t91 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t80, &t89, t90);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l9 = t91;
                }
                goto ix75;
ie75:;
ix75:;
            }
            {
                void * t92 = gm_l9;
                gm_t88rcucg1b7uk t93 = gm_l6;
                void * t94 = gm_f_bits_convert_8rcef5o((void *)NULL, cx, NULL, t92, t93);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t95 = 0;
                F.ret = t94;
                goto gm_fret;
            }
            goto ix57;
ie57:;
ix57:;
        }
        {
            void * t97 = gm_l0;
            void * t98 = GM_LD(void *, (((uint8_t *)t97) + 0));
            void * t99 = (void *)(((uint8_t *)t98) + 8);
            gm_t88rcucg1b7uk t100 = gm_l6;
            gm_t53at80pfdus3 t101;
            gm_f_kind_jcnt4hm((void *)&t101, t99, GM_CONV(gm_csguankko4lmhd, t100));
            uint8_t t102;
            uint8_t t105 = GM_LD(uint8_t, (((uint8_t *)&t101) + 0));
            if (t105 != ((uint8_t)17ULL)) goto mn104;
            t102 = ((uint8_t)1);
            goto me103;
mn104:;
            t102 = ((uint8_t)0);
            goto me103;
mn106:;
            gm_trap("no match arm matched the scrutinee", gm_srch2l5nl06vsci, 617, 16);
me103:;
            if (!t102) goto ie96;
            {
                void * t108 = gm_l0;
                gm_tl874t2atnlh3 t109 = gm_l4;
                gm_b16_8 t110;
                GM_ST(void *, (uint8_t *)&t110, (void *)(gm_s2svafu2na27p));
                GM_ST(uintptr_t, (uint8_t *)&t110 + 8, (uintptr_t)(16));
                void * t111 = gm_f_unsupported_r3r7qdh((void *)NULL, cx, NULL, t108, t109, t110);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t112 = 0;
                F.ret = t111;
                goto gm_fret;
            }
            goto ix96;
ie96:;
ix96:;
        }
        {
            void * t113 = gm_l0;
            void * t114 = (void *)(((uint8_t *)t113) + 320);
            gm_t91vm1g0k2rpm t115;
            memset((uint8_t *)&t115 + 34, 0, 6);
            void * t116 = gm_l5;
            gm_t88rcucg1b7uk t117 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t116) + 0));
            GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t115 + 0, t117);
            gm_tl874t2atnlh3 t118 = gm_l4;
            GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t115 + 4, t118);
            gm_b16_8 t119;
            GM_ST(void *, (uint8_t *)&t119, (void *)(gm_sa831sfid0hf3));
            GM_ST(uintptr_t, (uint8_t *)&t119 + 8, (uintptr_t)(1));
            GM_ST(gm_b16_8, (uint8_t *)&t115 + 16, t119);
            GM_ST(uint8_t, (uint8_t *)&t115 + 32, ((uint8_t)1));
            GM_ST(uint8_t, (uint8_t *)&t115 + 33, ((uint8_t)0));
            gm_tpm220j5qhjke t120;
            GM_ST(uint8_t, (uint8_t *)&t120 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t120 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t120 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t120 + 0), ((uint8_t)3ULL));
            GM_ST(gm_t91vm1g0k2rpm, ((uint8_t *)&t120 + 8), t115);
            gm_tftb1odc739t5 t121 = gm_f_push_7koe9lk(t114, &t120);
            if (((((uint8_t *)&t121))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t121) + 1, gm_srch2l5nl06vsci, 618, 13);
            gm_b0_1 t122 = GM_LD(gm_b0_1, (((uint8_t *)&t121) + 1));
        }
        {
            void * t123 = gm_l5;
            gm_t88rcucg1b7uk t124 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t123) + 0));
            gm_t9qkh0rsooc8d t125;
            GM_ST(uint8_t, ((uint8_t *)&t125 + 0), ((uint8_t)1ULL));
            void * t126 = gm_l5;
            gm_t8ghgl2jj0qha t127;
            GM_ST(uint8_t, (uint8_t *)&t127 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t127 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t127 + 4, 0);
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t127 + 0), t125);
            GM_ST(void *, ((uint8_t *)&t127 + 8), t126);
            gm_teti411eujvf4 t128;
            memset(&t128, 0, sizeof t128);
            GM_ST(uint8_t, ((uint8_t *)&t128 + 0), ((uint8_t)29ULL));
            GM_ST(gm_t8ghgl2jj0qha, ((uint8_t *)&t128 + 8), t127);
            gm_tl874t2atnlh3 t129 = gm_l4;
            void * t130 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t124, &t128, t129);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t131 = 0;
            F.ret = t130;
            goto gm_fret;
        }
        goto me2;
mn19:;
ma5:;
        uint8_t t133 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t133 != ((uint8_t)5ULL)) goto mn132;
        {
            void * t135 = gm_l0;
            void * t136 = gm_l2;
            void * t137 = gm_l0;
            void * t138 = GM_LD(void *, (((uint8_t *)t137) + 0));
            void * t139 = (void *)(((uint8_t *)t138) + 8);
            gm_t88rcucg1b7uk t140 = gm_f_bool_ty_pn2p277((void *)NULL, cx, NULL, t139);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tkn42u7na3t81 t141;
            GM_ST(uint8_t, (uint8_t *)&t141 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t141 + 6, 0);
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t141), t140);
            ((uint8_t *)&t141)[4] = 1;
            void * t142 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t135, t136, t141);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l11 = t142;
        }
        {
            void * t144 = gm_l0;
            void * t145 = GM_LD(void *, (((uint8_t *)t144) + 0));
            void * t146 = (void *)(((uint8_t *)t145) + 8);
            void * t147 = gm_l0;
            void * t148 = (void *)(((uint8_t *)t147) + 96);
            void * t149 = gm_l0;
            void * t150 = GM_LD(void *, (((uint8_t *)t149) + 0));
            void * t151 = gm_l11;
            gm_t88rcucg1b7uk t152 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t151) + 0));
            gm_t88rcucg1b7uk t153 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t148, t150, t152);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t154 = gm_f_is_vector_gkq6b88(t146, GM_CONV(gm_csguankko4lmhd, t153));
            if (!t154) goto ie143;
            {
                void * t156 = gm_l0;
                gm_t9qkh0rsooc8d t157;
                GM_ST(uint8_t, ((uint8_t *)&t157 + 0), ((uint8_t)5ULL));
                void * t158 = gm_l11;
                gm_tl874t2atnlh3 t159 = gm_l4;
                void * t160 = gm_f_vector_unary_2mbe97t((void *)NULL, cx, NULL, t156, t157, t158, t159);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t161 = 0;
                F.ret = t160;
                goto gm_fret;
            }
            goto ix143;
ie143:;
ix143:;
        }
        {
            void * t162 = gm_l0;
            void * t163 = gm_l11;
            void * t164 = gm_f_expect_bool_cdo5g1q((void *)NULL, cx, NULL, t162, t163);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l11 = t164;
        }
        {
            void * t165 = gm_l0;
            void * t166 = GM_LD(void *, (((uint8_t *)t165) + 0));
            void * t167 = (void *)(((uint8_t *)t166) + 8);
            gm_t88rcucg1b7uk t168 = gm_f_bool_ty_pn2p277((void *)NULL, cx, NULL, t167);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t9qkh0rsooc8d t169;
            GM_ST(uint8_t, ((uint8_t *)&t169 + 0), ((uint8_t)2ULL));
            void * t170 = gm_l11;
            gm_t8ghgl2jj0qha t171;
            GM_ST(uint8_t, (uint8_t *)&t171 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t171 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t171 + 4, 0);
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t171 + 0), t169);
            GM_ST(void *, ((uint8_t *)&t171 + 8), t170);
            gm_teti411eujvf4 t172;
            memset(&t172, 0, sizeof t172);
            GM_ST(uint8_t, ((uint8_t *)&t172 + 0), ((uint8_t)29ULL));
            GM_ST(gm_t8ghgl2jj0qha, ((uint8_t *)&t172 + 8), t171);
            gm_tl874t2atnlh3 t173 = gm_l4;
            void * t174 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t168, &t172, t173);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t175 = 0;
            F.ret = t174;
            goto gm_fret;
        }
        goto me2;
mn132:;
ma6:;
        uint8_t t177 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t177 != ((uint8_t)2ULL)) goto mn176;
        void * t178 = gm_l0;
        void * t179 = gm_l2;
        gm_tkn42u7na3t81 t180 = gm_l3;
        gm_tl874t2atnlh3 t181 = gm_l4;
        void * t182 = gm_f_addr_of_expr_ffg7r6j((void *)NULL, cx, NULL, t178, t179, t180, t181);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t183 = 0;
        F.ret = t182;
        goto gm_fret;
        goto me2;
mn176:;
ma7:;
        uint8_t t185 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t185 != ((uint8_t)3ULL)) goto mn184;
        {
            if (!((((uint8_t *)&gm_l3))[4] != 0)) goto ie187;
            gm_t88rcucg1b7uk t188 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l3));
            gm_l12 = t188;
            void * t189 = gm_l0;
            void * t190 = GM_LD(void *, (((uint8_t *)t189) + 0));
            void * t191 = (void *)(((uint8_t *)t190) + 8);
            void * t192 = gm_l0;
            void * t193 = (void *)(((uint8_t *)t192) + 96);
            void * t194 = gm_l0;
            void * t195 = GM_LD(void *, (((uint8_t *)t194) + 0));
            void * t196 = (void *)(((uint8_t *)t195) + 8);
            gm_t88rcucg1b7uk t197 = gm_l12;
            gm_t88rcucg1b7uk t198 = gm_f_shallow_8ur803a((void *)NULL, cx, NULL, t193, t196, t197);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t53at80pfdus3 t199;
            gm_f_kind_jcnt4hm((void *)&t199, t191, GM_CONV(gm_csguankko4lmhd, t198));
            uint8_t t200 = GM_LD(uint8_t, (((uint8_t *)&t199) + 0));
            if (t200 != ((uint8_t)13ULL)) goto ie187;
            gm_t88rcucg1b7uk t201 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t199) + 8));
            gm_l13 = t201;
            void * t202 = gm_l0;
            void * t203 = GM_LD(void *, (((uint8_t *)t202) + 0));
            void * t204 = (void *)(((uint8_t *)t203) + 8);
            void * t205 = gm_l0;
            void * t206 = (void *)(((uint8_t *)t205) + 96);
            void * t207 = gm_l0;
            void * t208 = GM_LD(void *, (((uint8_t *)t207) + 0));
            void * t209 = (void *)(((uint8_t *)t208) + 8);
            gm_t88rcucg1b7uk t210 = gm_l13;
            gm_t88rcucg1b7uk t211 = gm_f_shallow_8ur803a((void *)NULL, cx, NULL, t206, t209, t210);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t53at80pfdus3 t212;
            gm_f_kind_jcnt4hm((void *)&t212, t204, GM_CONV(gm_csguankko4lmhd, t211));
            uint8_t t213;
            uint8_t t216 = GM_LD(uint8_t, (((uint8_t *)&t212) + 0));
            if (t216 != ((uint8_t)14ULL)) goto mn215;
            t213 = ((uint8_t)1);
            goto me214;
mn215:;
            t213 = ((uint8_t)0);
            goto me214;
mn217:;
            gm_trap("no match arm matched the scrutinee", gm_srch2l5nl06vsci, 630, 102);
me214:;
            if (!t213) goto ie187;
            {
                void * t219 = gm_l0;
                void * t220 = gm_l2;
                gm_t88rcucg1b7uk t221 = gm_l13;
                gm_tkn42u7na3t81 t222;
                GM_ST(uint8_t, (uint8_t *)&t222 + 5, 0);
                GM_ST(uint16_t, (uint8_t *)&t222 + 6, 0);
                GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t222), t221);
                ((uint8_t *)&t222)[4] = 1;
                gm_tl874t2atnlh3 t223 = gm_l4;
                void * t224 = gm_f_make_err_expr_6arm2q7((void *)NULL, cx, NULL, t219, t220, t222, t223);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l14 = t224;
            }
            {
                void * t225 = gm_l0;
                void * t226 = gm_l14;
                gm_t88rcucg1b7uk t227 = gm_l12;
                void * t228 = gm_f_coerce_sudc6c1((void *)NULL, cx, NULL, t225, t226, t227);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t229 = 0;
                F.ret = t228;
                goto gm_fret;
            }
            goto ix187;
ie187:;
ix187:;
        }
        {
            void * t230 = gm_l0;
            void * t231 = gm_l2;
            gm_tkn42u7na3t81 t232 = gm_l3;
            gm_tl874t2atnlh3 t233 = gm_l4;
            void * t234 = gm_f_make_err_expr_6arm2q7((void *)NULL, cx, NULL, t230, t231, t232, t233);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t235 = 0;
            F.ret = t234;
            goto gm_fret;
        }
        goto me2;
mn184:;
ma8:;
        uint8_t t237 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t237 != ((uint8_t)4ULL)) goto mn236;
        void * t238 = gm_l0;
        void * t239 = gm_l2;
        gm_tl874t2atnlh3 t240 = gm_l4;
        void * t241 = gm_f_try_expr_ntgm5mj((void *)NULL, cx, NULL, t238, t239, t240);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t242 = 0;
        F.ret = t241;
        goto gm_fret;
        goto me2;
mn236:;
ma9:;
        gm_trap("no match arm matched the scrutinee", gm_srch2l5nl06vsci, 603, 5);
me2:;
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
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_unify_operands_5sp7j1j(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_t88rcucg1b7uk a2, gm_tl874t2atnlh3 a3, gm_b16_8 a4) {
    struct gm_fr_unify_operands_5sp7j1j F;
    void * gm_l0; /* cx */
    gm_t88rcucg1b7uk gm_l1; /* a */
    gm_t88rcucg1b7uk gm_l2; /* b */
    gm_tl874t2atnlh3 gm_l3; /* span */
    gm_b16_8 gm_l4; /* what */
    gm_t88rcucg1b7uk gm_l5; /* ra */
    gm_t88rcucg1b7uk gm_l6; /* rb */
    uint8_t gm_l8; /* wrapped_a */
    uint8_t gm_l9; /* wrapped_b */
    GM_STACK_CHECK(&F, 678u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    {
        void * t3 = gm_l0;
        void * t4 = (void *)(((uint8_t *)t3) + 96);
        void * t5 = gm_l0;
        void * t6 = GM_LD(void *, (((uint8_t *)t5) + 0));
        void * t7 = (void *)(((uint8_t *)t6) + 8);
        gm_t88rcucg1b7uk t8 = gm_l1;
        gm_t88rcucg1b7uk t9 = gm_l2;
        gm_t9qkh0rsooc8d t10 = gm_f_unify_t2qjv2p((void *)NULL, cx, NULL, t4, t7, t8, t9);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t11;
        if (!((((uint8_t *)&t10))[0] != 0)) goto mn13;
        t11 = ((uint8_t)1);
        goto me12;
mn13:;
        t11 = ((uint8_t)0);
        goto me12;
mn14:;
        gm_trap("no match arm matched the scrutinee", gm_srch2l5nl06vsci, 702, 8);
me12:;
        if (!t11) goto ie2;
        {
            void * t16 = gm_l0;
            void * t17 = (void *)(((uint8_t *)t16) + 96);
            void * t18 = gm_l0;
            void * t19 = GM_LD(void *, (((uint8_t *)t18) + 0));
            gm_t88rcucg1b7uk t20 = gm_l1;
            gm_t88rcucg1b7uk t21 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t17, t19, t20);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l5 = t21;
        }
        {
            void * t22 = gm_l0;
            void * t23 = (void *)(((uint8_t *)t22) + 96);
            void * t24 = gm_l0;
            void * t25 = GM_LD(void *, (((uint8_t *)t24) + 0));
            gm_t88rcucg1b7uk t26 = gm_l2;
            gm_t88rcucg1b7uk t27 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t23, t25, t26);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l6 = t27;
        }
        {
            gm_tl874t2atnlh3 t28 = gm_l3;
            gm_tfeab279c16rr t29;
            gm_b16_8 t30 = gm_l4;
            GM_ST(gm_b16_8, (uint8_t *)&t29 + 0, t30);
            void * t31 = gm_l0;
            gm_t88rcucg1b7uk t32 = gm_l5;
            gm_b16_8 t33 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t31, t32);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(gm_b16_8, (uint8_t *)&t29 + 16, t33);
            void * t34 = gm_l0;
            gm_t88rcucg1b7uk t35 = gm_l6;
            gm_b16_8 t36 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t34, t35);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(gm_b16_8, (uint8_t *)&t29 + 32, t36);
            gm_tabb710d9mq2e t37;
            gm_f_format_32qgu0c((void *)&t37, (void *)NULL, cx, NULL, &t29);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t37))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t37) + 8, gm_srch2l5nl06vsci, 705, 31);
            gm_b16_8 t38 = GM_LD(gm_b16_8, (((uint8_t *)&t37) + 8));
            gm_b16_8 t39 = GM_LD(gm_b16_8, ((uint8_t *)&t38));
            gm_f_error_s6hn871((void *)&F.l7, (void *)NULL, cx, NULL, t28, t39);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t40 = gm_l0;
            void * t41 = GM_LD(void *, (((uint8_t *)t40) + 0));
            void * t42 = (void *)(((uint8_t *)t41) + 8);
            gm_t88rcucg1b7uk t43 = gm_l5;
            gm_t53at80pfdus3 t44;
            gm_f_kind_jcnt4hm((void *)&t44, t42, GM_CONV(gm_csguankko4lmhd, t43));
            uint8_t t45;
            uint8_t t51 = GM_LD(uint8_t, (((uint8_t *)&t44) + 0));
            switch (t51) { case ((uint8_t)0ULL): goto ma49; case ((uint8_t)1ULL): goto ma49; case ((uint8_t)2ULL): goto ma49; case ((uint8_t)3ULL): goto ma49; case ((uint8_t)4ULL): goto ma49; case ((uint8_t)5ULL): goto ma49; case ((uint8_t)6ULL): goto ma49; case ((uint8_t)7ULL): goto ma49; case ((uint8_t)8ULL): goto ma49; case ((uint8_t)9ULL): goto ma49; case ((uint8_t)10ULL): goto ma49; case ((uint8_t)11ULL): goto ma49; case ((uint8_t)12ULL): goto ma49; case ((uint8_t)13ULL): goto ma47; case ((uint8_t)14ULL): goto ma48; case ((uint8_t)15ULL): goto ma49; case ((uint8_t)16ULL): goto ma49; case ((uint8_t)17ULL): goto ma49; case ((uint8_t)18ULL): goto ma49; case ((uint8_t)19ULL): goto ma49; case ((uint8_t)20ULL): goto ma49; case ((uint8_t)21ULL): goto ma49; case ((uint8_t)22ULL): goto ma49; case ((uint8_t)23ULL): goto ma49; case ((uint8_t)24ULL): goto ma49; case ((uint8_t)25ULL): goto ma49; case ((uint8_t)26ULL): goto ma49; default: goto ma49; }
ma47:;
            uint8_t t53 = GM_LD(uint8_t, (((uint8_t *)&t44) + 0));
            if (t53 != ((uint8_t)13ULL)) goto mn52;
            t45 = ((uint8_t)1);
            goto me46;
mn52:;
ma48:;
            uint8_t t55 = GM_LD(uint8_t, (((uint8_t *)&t44) + 0));
            if (t55 != ((uint8_t)14ULL)) goto mn54;
            t45 = ((uint8_t)1);
            goto me46;
mn54:;
ma49:;
            t45 = ((uint8_t)0);
            goto me46;
mn56:;
ma50:;
            gm_trap("no match arm matched the scrutinee", gm_srch2l5nl06vsci, 707, 22);
me46:;
            gm_l8 = t45;
        }
        {
            void * t57 = gm_l0;
            void * t58 = GM_LD(void *, (((uint8_t *)t57) + 0));
            void * t59 = (void *)(((uint8_t *)t58) + 8);
            gm_t88rcucg1b7uk t60 = gm_l6;
            gm_t53at80pfdus3 t61;
            gm_f_kind_jcnt4hm((void *)&t61, t59, GM_CONV(gm_csguankko4lmhd, t60));
            uint8_t t62;
            uint8_t t68 = GM_LD(uint8_t, (((uint8_t *)&t61) + 0));
            switch (t68) { case ((uint8_t)0ULL): goto ma66; case ((uint8_t)1ULL): goto ma66; case ((uint8_t)2ULL): goto ma66; case ((uint8_t)3ULL): goto ma66; case ((uint8_t)4ULL): goto ma66; case ((uint8_t)5ULL): goto ma66; case ((uint8_t)6ULL): goto ma66; case ((uint8_t)7ULL): goto ma66; case ((uint8_t)8ULL): goto ma66; case ((uint8_t)9ULL): goto ma66; case ((uint8_t)10ULL): goto ma66; case ((uint8_t)11ULL): goto ma66; case ((uint8_t)12ULL): goto ma66; case ((uint8_t)13ULL): goto ma64; case ((uint8_t)14ULL): goto ma65; case ((uint8_t)15ULL): goto ma66; case ((uint8_t)16ULL): goto ma66; case ((uint8_t)17ULL): goto ma66; case ((uint8_t)18ULL): goto ma66; case ((uint8_t)19ULL): goto ma66; case ((uint8_t)20ULL): goto ma66; case ((uint8_t)21ULL): goto ma66; case ((uint8_t)22ULL): goto ma66; case ((uint8_t)23ULL): goto ma66; case ((uint8_t)24ULL): goto ma66; case ((uint8_t)25ULL): goto ma66; case ((uint8_t)26ULL): goto ma66; default: goto ma66; }
ma64:;
            uint8_t t70 = GM_LD(uint8_t, (((uint8_t *)&t61) + 0));
            if (t70 != ((uint8_t)13ULL)) goto mn69;
            t62 = ((uint8_t)1);
            goto me63;
mn69:;
ma65:;
            uint8_t t72 = GM_LD(uint8_t, (((uint8_t *)&t61) + 0));
            if (t72 != ((uint8_t)14ULL)) goto mn71;
            t62 = ((uint8_t)1);
            goto me63;
mn71:;
ma66:;
            t62 = ((uint8_t)0);
            goto me63;
mn73:;
ma67:;
            gm_trap("no match arm matched the scrutinee", gm_srch2l5nl06vsci, 708, 22);
me63:;
            gm_l9 = t62;
        }
        {
            uint8_t t75 = (((GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8) == 12) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l4)), gm_sp6ona76fn0di, 12) == 0));
            if (!t75) goto ie74;
            uint8_t t76 = gm_l8;
            uint8_t t77 = gm_l9;
            uint8_t t78 = (!(t76 == t77));
            if (!t78) goto ie74;
            {
                void * t80 = (void *)((uint8_t *)&F.l7);
                gm_b16_8 t81;
                GM_ST(void *, (uint8_t *)&t81, (void *)(gm_sqo8h5kg9id12));
                GM_ST(uintptr_t, (uint8_t *)&t81 + 8, (uintptr_t)(85));
                gm_f_add_note_46v502s(t80, t81);
            }
            goto ix74;
ie74:;
ix74:;
        }
        {
            void * t82 = gm_l0;
            void * t83 = GM_LD(void *, (((uint8_t *)t82) + 0));
            gm_b0_1 t84 = gm_f_emit_dph5v1e((void *)NULL, cx, NULL, t83, &(*(gm_t4gut2c9fj1kt *)((uint8_t *)&F.l7)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t85 = gm_l0;
            void * t86 = GM_LD(void *, (((uint8_t *)t85) + 0));
            void * t87 = (void *)(((uint8_t *)t86) + 8);
            gm_t88rcucg1b7uk t88 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t87);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t89 = 0;
            F.ret = t88;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t90 = gm_l0;
        void * t91 = (void *)(((uint8_t *)t90) + 96);
        void * t92 = gm_l0;
        void * t93 = GM_LD(void *, (((uint8_t *)t92) + 0));
        void * t94 = (void *)(((uint8_t *)t93) + 8);
        gm_t88rcucg1b7uk t95 = gm_l1;
        gm_t88rcucg1b7uk t96 = gm_f_shallow_8ur803a((void *)NULL, cx, NULL, t91, t94, t95);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t97 = 0;
        F.ret = t96;
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
__attribute__((aligned(4))) void * gm_f_untyped_literal_pfu1ft4(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_tl874t2atnlh3 a2) {
    struct gm_fr_untyped_literal_pfu1ft4 F;
    void * gm_l0; /* cx */
    void * gm_l1; /* v */
    gm_tl874t2atnlh3 gm_l2; /* span */
    void * gm_l3; /* tys */
    void * gm_l6; /* zero */
    uint8_t gm_l7; /* b */
    GM_STACK_CHECK(&F, 1239u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 0));
        void * t4 = (void *)(((uint8_t *)t3) + 8);
        gm_l3 = t4;
    }
    {
        void * t5 = gm_l1;
        uint8_t t13 = GM_LD(uint8_t, (((uint8_t *)t5) + 0));
        switch (t13) { case ((uint8_t)0ULL): goto ma7; case ((uint8_t)1ULL): goto ma8; case ((uint8_t)2ULL): goto ma9; case ((uint8_t)3ULL): goto ma10; case ((uint8_t)4ULL): goto ma11; default: goto ma12; }
ma7:;
        uint8_t t15 = GM_LD(uint8_t, (((uint8_t *)t5) + 0));
        if (t15 != ((uint8_t)0ULL)) goto mn14;
        gm_tbci5ospk5nnn t16 = GM_LD(gm_tbci5ospk5nnn, (((uint8_t *)t5) + 8));
        F.l4 = t16;
        void * t17 = gm_l0;
        void * t18 = (void *)(((uint8_t *)t17) + 96);
        void * t19 = gm_l3;
        gm_t9qkh0rsooc8d t20;
        GM_ST(uint8_t, ((uint8_t *)&t20 + 0), ((uint8_t)0ULL));
        gm_t88rcucg1b7uk t21 = gm_f_new_var_up2m1nq((void *)NULL, cx, NULL, t18, t19, t20);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tbci5ospk5nnn t22 = F.l4;
        gm_teti411eujvf4 t23;
        GM_ST(uint8_t, (uint8_t *)&t23 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t23 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t23 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t23 + 56, 0);
        GM_ST(uint8_t, ((uint8_t *)&t23 + 0), ((uint8_t)0ULL));
        GM_ST(gm_tbci5ospk5nnn, ((uint8_t *)&t23 + 8), t22);
        gm_tl874t2atnlh3 t24 = gm_l2;
        void * t25 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t21, &t23, t24);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t26 = 0;
        F.ret = t25;
        goto gm_fret;
        goto me6;
mn14:;
ma8:;
        uint8_t t28 = GM_LD(uint8_t, (((uint8_t *)t5) + 0));
        if (t28 != ((uint8_t)1ULL)) goto mn27;
        gm_tngtuiuskn938 t29 = GM_LD(gm_tngtuiuskn938, (((uint8_t *)t5) + 8));
        F.l5 = t29;
        void * t30 = gm_l0;
        void * t31 = (void *)(((uint8_t *)t30) + 96);
        void * t32 = gm_l3;
        gm_t9qkh0rsooc8d t33;
        GM_ST(uint8_t, ((uint8_t *)&t33 + 0), ((uint8_t)1ULL));
        gm_t88rcucg1b7uk t34 = gm_f_new_var_up2m1nq((void *)NULL, cx, NULL, t31, t32, t33);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_teti411eujvf4 t35;
        gm_f_untyped_float_m6lbr0g((void *)&t35, (void *)NULL, cx, NULL, &(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l5)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tl874t2atnlh3 t36 = gm_l2;
        void * t37 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t34, &t35, t36);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t38 = 0;
        F.ret = t37;
        goto gm_fret;
        goto me6;
mn27:;
ma9:;
        uint8_t t40 = GM_LD(uint8_t, (((uint8_t *)t5) + 0));
        if (t40 != ((uint8_t)2ULL)) goto mn39;
        {
            void * t42 = gm_l0;
            void * t43 = (void *)(((uint8_t *)t42) + 96);
            void * t44 = gm_l3;
            gm_t9qkh0rsooc8d t45;
            GM_ST(uint8_t, ((uint8_t *)&t45 + 0), ((uint8_t)1ULL));
            gm_t88rcucg1b7uk t46 = gm_f_new_var_up2m1nq((void *)NULL, cx, NULL, t43, t44, t45);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tngtuiuskn938 t47;
            gm_f_zero_9r6o7sj((void *)&t47, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_teti411eujvf4 t48;
            gm_f_untyped_float_m6lbr0g((void *)&t48, (void *)NULL, cx, NULL, &t47);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tl874t2atnlh3 t49 = gm_l2;
            void * t50 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t46, &t48, t49);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l6 = t50;
        }
        {
            void * t51 = gm_l6;
            gm_t88rcucg1b7uk t52 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t51) + 0));
            gm_t9qkh0rsooc8d t53;
            GM_ST(uint8_t, ((uint8_t *)&t53 + 0), ((uint8_t)0ULL));
            void * t54 = gm_l6;
            gm_t8ghgl2jj0qha t55;
            GM_ST(uint8_t, (uint8_t *)&t55 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t55 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t55 + 4, 0);
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t55 + 0), t53);
            GM_ST(void *, ((uint8_t *)&t55 + 8), t54);
            gm_teti411eujvf4 t56;
            memset(&t56, 0, sizeof t56);
            GM_ST(uint8_t, ((uint8_t *)&t56 + 0), ((uint8_t)29ULL));
            GM_ST(gm_t8ghgl2jj0qha, ((uint8_t *)&t56 + 8), t55);
            gm_tl874t2atnlh3 t57 = gm_l2;
            void * t58 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t52, &t56, t57);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t59 = 0;
            F.ret = t58;
            goto gm_fret;
        }
        goto me6;
mn39:;
ma10:;
        uint8_t t61 = GM_LD(uint8_t, (((uint8_t *)t5) + 0));
        if (t61 != ((uint8_t)3ULL)) goto mn60;
        uint8_t t62 = GM_LD(uint8_t, (((uint8_t *)t5) + 8));
        gm_l7 = t62;
        void * t63 = gm_l3;
        gm_t88rcucg1b7uk t64 = gm_f_bool_ty_pn2p277((void *)NULL, cx, NULL, t63);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t65 = gm_l7;
        gm_teti411eujvf4 t66;
        memset(&t66, 0, sizeof t66);
        GM_ST(uint8_t, ((uint8_t *)&t66 + 0), ((uint8_t)4ULL));
        GM_ST(uint8_t, ((uint8_t *)&t66 + 8), t65);
        gm_tl874t2atnlh3 t67 = gm_l2;
        void * t68 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t64, &t66, t67);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t69 = 0;
        F.ret = t68;
        goto gm_fret;
        goto me6;
mn60:;
ma11:;
        uint8_t t71 = GM_LD(uint8_t, (((uint8_t *)t5) + 0));
        if (t71 != ((uint8_t)4ULL)) goto mn70;
        gm_tu15p0hi2qlv6 t72 = GM_LD(gm_tu15p0hi2qlv6, (((uint8_t *)t5) + 8));
        F.l8 = t72;
        void * t73 = gm_l0;
        gm_t9qkh0rsooc8d t74;
        GM_ST(uint8_t, ((uint8_t *)&t74 + 0), ((uint8_t)2ULL));
        gm_t88rcucg1b7uk t75 = gm_f_literal_var_3sb5sti((void *)NULL, cx, NULL, t73, t74);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_teti411eujvf4 t76;
        gm_f_untyped_complex_20agrmu((void *)&t76, (void *)NULL, cx, NULL, &(*(gm_tngtuiuskn938 *)(((uint8_t *)&F.l8) + 0)), &(*(gm_tngtuiuskn938 *)(((uint8_t *)&F.l8) + 96)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tl874t2atnlh3 t77 = gm_l2;
        void * t78 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t75, &t76, t77);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t79 = 0;
        F.ret = t78;
        goto gm_fret;
        goto me6;
mn70:;
ma12:;
        gm_trap("no match arm matched the scrutinee", gm_srch2l5nl06vsci, 413, 5);
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_value_param_ty_qrv22u4(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1) {
    struct gm_fr_value_param_ty_qrv22u4 F;
    void * gm_l0; /* cx */
    uint32_t gm_l1; /* pid */
    gm_t88rcucg1b7uk gm_l2; /* t */
    GM_STACK_CHECK(&F, 91u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t3 = gm_l0;
        void * t4 = GM_LD(void *, (((uint8_t *)t3) + 0));
        uint32_t t5 = gm_l1;
        gm_tnnjeerolos0a t6 = gm_f_param_kind_a0aikmb(t4, t5);
        uint8_t t7 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
        if (t7 != ((uint8_t)2ULL)) goto ie2;
        gm_t88rcucg1b7uk t8 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t6) + 4));
        gm_l2 = t8;
        {
            gm_t88rcucg1b7uk t10 = gm_l2;
            uint8_t t11 = 0;
            F.ret = t10;
            goto gm_fret;
        }
        goto ix2;
ie2:;
        {
            void * t13 = gm_l0;
            void * t14 = GM_LD(void *, (((uint8_t *)t13) + 0));
            void * t15 = (void *)(((uint8_t *)t14) + 8);
            gm_t88rcucg1b7uk t16 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t15);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t17 = 0;
            F.ret = t16;
            goto gm_fret;
        }
ix2:;
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
inline __attribute__((aligned(4))) uint8_t gm_f_wraps_mh8ot70(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0) {
    struct gm_fr_wraps_mh8ot70 F;
    gm_t88rcucg1b7uk gm_l0; /* t */
    GM_STACK_CHECK_SHALLOW(&F, 93u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t t1 = 0;
    void * t2 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_equalize_ni1r5ra *)((&F.h)->up))->l4));
    gm_t88rcucg1b7uk t3 = gm_l0;
    gm_t53at80pfdus3 t4;
    gm_f_kind_jcnt4hm((void *)&t4, t2, GM_CONV(gm_csguankko4lmhd, t3));
    uint8_t t5;
    uint8_t t11 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
    switch (t11) { case ((uint8_t)0ULL): goto ma9; case ((uint8_t)1ULL): goto ma9; case ((uint8_t)2ULL): goto ma9; case ((uint8_t)3ULL): goto ma9; case ((uint8_t)4ULL): goto ma9; case ((uint8_t)5ULL): goto ma9; case ((uint8_t)6ULL): goto ma9; case ((uint8_t)7ULL): goto ma9; case ((uint8_t)8ULL): goto ma9; case ((uint8_t)9ULL): goto ma9; case ((uint8_t)10ULL): goto ma9; case ((uint8_t)11ULL): goto ma9; case ((uint8_t)12ULL): goto ma9; case ((uint8_t)13ULL): goto ma7; case ((uint8_t)14ULL): goto ma8; case ((uint8_t)15ULL): goto ma9; case ((uint8_t)16ULL): goto ma9; case ((uint8_t)17ULL): goto ma9; case ((uint8_t)18ULL): goto ma9; case ((uint8_t)19ULL): goto ma9; case ((uint8_t)20ULL): goto ma9; case ((uint8_t)21ULL): goto ma9; case ((uint8_t)22ULL): goto ma9; case ((uint8_t)23ULL): goto ma9; case ((uint8_t)24ULL): goto ma9; case ((uint8_t)25ULL): goto ma9; case ((uint8_t)26ULL): goto ma9; default: goto ma9; }
ma7:;
    uint8_t t13 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
    if (t13 != ((uint8_t)13ULL)) goto mn12;
    t5 = ((uint8_t)1);
    goto me6;
mn12:;
ma8:;
    uint8_t t15 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
    if (t15 != ((uint8_t)14ULL)) goto mn14;
    t5 = ((uint8_t)1);
    goto me6;
mn14:;
ma9:;
    t5 = ((uint8_t)0);
    goto me6;
mn16:;
ma10:;
    gm_trap("no match arm matched the scrutinee", gm_srch2l5nl06vsci, 989, 35);
me6:;
    if (t5) {
        void * t17 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_equalize_ni1r5ra *)((&F.h)->up))->l4));
        gm_t88rcucg1b7uk t18 = gm_l0;
        uint8_t t19 = gm_f_has_infer_3o82a0b(t17, GM_CONV(gm_csguankko4lmhd, t18));
        uint8_t t20 = (uint8_t)!t19;
        t1 = t20;
    } else t1 = 0;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
