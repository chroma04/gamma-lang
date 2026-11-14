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
typedef struct gm_cseahvg8v7p0vs { uint32_t f0; uint32_t f1; uint32_t f2; } gm_cseahvg8v7p0vs;
typedef struct gm_cskjv6t053tkje { uint32_t f0; uint64_t f1; } gm_cskjv6t053tkje;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint8_t f0; uint8_t f1; uint8_t f2; uint8_t f3; uint8_t f4; } gm_tb1msgo1fu1n3;
typedef struct { uint32_t f0; uint8_t p1[4]; uint64_t f1; } gm_tb31k5l794t7p;
typedef struct { uint64_t a[3]; } gm_tc0nbpdhofi37;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { uint8_t f0; uint8_t f1; } gm_tqs5p4n2tq19v;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; uint32_t f3; uint32_t f4; uint32_t f5; uint32_t f6; uint32_t f7; } gm_tu4r32bi6mhcq;
_Static_assert(sizeof(gm_cseahvg8v7p0vs) == 12 && _Alignof(gm_cseahvg8v7p0vs) == 4 && offsetof(gm_cseahvg8v7p0vs, f0) == 0 && offsetof(gm_cseahvg8v7p0vs, f1) == 4 && offsetof(gm_cseahvg8v7p0vs, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_cskjv6t053tkje) == 16 && _Alignof(gm_cskjv6t053tkje) == 8 && offsetof(gm_cskjv6t053tkje, f0) == 0 && offsetof(gm_cskjv6t053tkje, f1) == 8, "layout of `Char`");
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[3]; gm_tl874t2atnlh3 f1; } gm_t2eb59dvglnle;
typedef struct { gm_tl874t2atnlh3 f0; uint8_t p1[4]; gm_b16_8 f1; uint32_t f2; uint8_t pe[4]; } gm_t3jim1gvsdr87;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { gm_tqs5p4n2tq19v f0; gm_tqs5p4n2tq19v f1; } gm_t5r6k0fdcfsb3;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
typedef struct { gm_t9qkh0rsooc8d f0; gm_t9qkh0rsooc8d f1; gm_t9qkh0rsooc8d f2; gm_t9qkh0rsooc8d f3; gm_t9qkh0rsooc8d f4; gm_t9qkh0rsooc8d f5; gm_t9qkh0rsooc8d f6; } gm_t6ofeqo2h242p;
typedef struct { gm_t9qkh0rsooc8d f0; gm_t9qkh0rsooc8d f1; gm_t9qkh0rsooc8d f2; gm_t9qkh0rsooc8d f3; gm_t9qkh0rsooc8d f4; } gm_t724v60om7sh5;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t f1; } gm_taari1s0m51a7;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
_Static_assert(sizeof(gm_tb1msgo1fu1n3) == 5 && _Alignof(gm_tb1msgo1fu1n3) == 1 && offsetof(gm_tb1msgo1fu1n3, f0) == 0 && offsetof(gm_tb1msgo1fu1n3, f1) == 1 && offsetof(gm_tb1msgo1fu1n3, f2) == 2 && offsetof(gm_tb1msgo1fu1n3, f3) == 3 && offsetof(gm_tb1msgo1fu1n3, f4) == 4, "layout of `(u8, u8, u8, u8, u8)`");
_Static_assert(sizeof(gm_tb31k5l794t7p) == 16 && _Alignof(gm_tb31k5l794t7p) == 8 && offsetof(gm_tb31k5l794t7p, f0) == 0 && offsetof(gm_tb31k5l794t7p, f1) == 8, "layout of `Char`");
typedef struct { gm_tl874t2atnlh3 f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tbgd14esf0jvi;
_Static_assert(sizeof(gm_tc0nbpdhofi37) == 24 && _Alignof(gm_tc0nbpdhofi37) == 8 && offsetof(gm_tc0nbpdhofi37, a) == 0, "layout of `[3]usize`");
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; uint64_t f1; } gm_te0mgnjdphs16;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; } gm_tfeab279c16rr;
typedef struct { gm_t9qkh0rsooc8d f0; gm_t9qkh0rsooc8d f1; gm_t9qkh0rsooc8d f2; gm_t9qkh0rsooc8d f3; gm_t9qkh0rsooc8d f4; gm_t9qkh0rsooc8d f5; gm_t9qkh0rsooc8d f6; gm_t9qkh0rsooc8d f7; gm_t9qkh0rsooc8d f8; gm_t9qkh0rsooc8d f9; } gm_tfh78knfg7urq;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { gm_b16_8 f0; gm_t9qkh0rsooc8d f1; uint8_t pe[7]; } gm_tibupt4dmcqai;
typedef struct { gm_t9qkh0rsooc8d f0; gm_t9qkh0rsooc8d f1; gm_t9qkh0rsooc8d f2; } gm_tk22flr9mh3ql;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
typedef struct { gm_tl874t2atnlh3 f0; uint8_t f1; uint8_t pe[3]; } gm_toceffb697p0u;
typedef struct { gm_b16_8 f0; uint32_t f1; uint8_t pe[4]; } gm_tommf4j9upomc;
_Static_assert(sizeof(gm_tqs5p4n2tq19v) == 2 && _Alignof(gm_tqs5p4n2tq19v) == 1 && offsetof(gm_tqs5p4n2tq19v, f0) == 0 && offsetof(gm_tqs5p4n2tq19v, f1) == 1, "layout of `(u8, u8)`");
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
_Static_assert(sizeof(gm_tu4r32bi6mhcq) == 32 && _Alignof(gm_tu4r32bi6mhcq) == 4 && offsetof(gm_tu4r32bi6mhcq, f0) == 0 && offsetof(gm_tu4r32bi6mhcq, f1) == 4 && offsetof(gm_tu4r32bi6mhcq, f2) == 8 && offsetof(gm_tu4r32bi6mhcq, f3) == 12 && offsetof(gm_tu4r32bi6mhcq, f4) == 16 && offsetof(gm_tu4r32bi6mhcq, f5) == 20 && offsetof(gm_tu4r32bi6mhcq, f6) == 24 && offsetof(gm_tu4r32bi6mhcq, f7) == 28, "layout of `(u32, u32, u32, u32, u32, u32, u32, u32)`");
typedef struct { gm_trqfvmjtt4sij f0; uint64_t f1; uint64_t f2; uint32_t f3; uint8_t pe[4]; } gm_t2792tsg5eg72;
_Static_assert(sizeof(gm_t2eb59dvglnle) == 16 && _Alignof(gm_t2eb59dvglnle) == 4 && offsetof(gm_t2eb59dvglnle, f0) == 0 && offsetof(gm_t2eb59dvglnle, f1) == 4, "layout of `Bracket`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; gm_t9qkh0rsooc8d v1; void * v2; void * v3; gm_tibupt4dmcqai v4; gm_b16_8 v5; gm_b16_8 v6; gm_t9qkh0rsooc8d v7; } f1; } gm_t3fcbuehh3bs2;
_Static_assert(sizeof(gm_t3jim1gvsdr87) == 40 && _Alignof(gm_t3jim1gvsdr87) == 8 && offsetof(gm_t3jim1gvsdr87, f0) == 0 && offsetof(gm_t3jim1gvsdr87, f1) == 16 && offsetof(gm_t3jim1gvsdr87, f2) == 32, "layout of `DocComment`");
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t p3[4]; gm_trqfvmjtt4sij f3; gm_trqfvmjtt4sij f4; gm_trqfvmjtt4sij f5; gm_th2lqbo77pvsc f6; } gm_t4gut2c9fj1kt;
_Static_assert(sizeof(gm_t5r6k0fdcfsb3) == 4 && _Alignof(gm_t5r6k0fdcfsb3) == 1 && offsetof(gm_t5r6k0fdcfsb3, f0) == 0 && offsetof(gm_t5r6k0fdcfsb3, f1) == 2, "layout of `(?u8, ?u8)`");
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_t6ofeqo2h242p) == 7 && _Alignof(gm_t6ofeqo2h242p) == 1 && offsetof(gm_t6ofeqo2h242p, f0) == 0 && offsetof(gm_t6ofeqo2h242p, f1) == 1 && offsetof(gm_t6ofeqo2h242p, f2) == 2 && offsetof(gm_t6ofeqo2h242p, f3) == 3 && offsetof(gm_t6ofeqo2h242p, f4) == 4 && offsetof(gm_t6ofeqo2h242p, f5) == 5 && offsetof(gm_t6ofeqo2h242p, f6) == 6, "layout of `(Command, Command, Command, Command, Command, Command, Command)`");
_Static_assert(sizeof(gm_t724v60om7sh5) == 5 && _Alignof(gm_t724v60om7sh5) == 1 && offsetof(gm_t724v60om7sh5, f0) == 0 && offsetof(gm_t724v60om7sh5, f1) == 1 && offsetof(gm_t724v60om7sh5, f2) == 2 && offsetof(gm_t724v60om7sh5, f3) == 3 && offsetof(gm_t724v60om7sh5, f4) == 4, "layout of `(Keyword, Keyword, Keyword, Keyword, Keyword)`");
_Static_assert(sizeof(gm_taari1s0m51a7) == 2 && _Alignof(gm_taari1s0m51a7) == 1 && offsetof(gm_taari1s0m51a7, f0) == 0 && offsetof(gm_taari1s0m51a7, f1) == 1, "layout of `?UnOp`");
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
_Static_assert(sizeof(gm_tbgd14esf0jvi) == 32 && _Alignof(gm_tbgd14esf0jvi) == 8 && offsetof(gm_tbgd14esf0jvi, f0) == 0 && offsetof(gm_tbgd14esf0jvi, f1) == 16, "layout of `Edit`");
_Static_assert(sizeof(gm_te0mgnjdphs16) == 16 && _Alignof(gm_te0mgnjdphs16) == 8 && offsetof(gm_te0mgnjdphs16, f0) == 0 && offsetof(gm_te0mgnjdphs16, f1) == 8, "layout of `Chunk`");
_Static_assert(sizeof(gm_tfeab279c16rr) == 48 && _Alignof(gm_tfeab279c16rr) == 8 && offsetof(gm_tfeab279c16rr, f0) == 0 && offsetof(gm_tfeab279c16rr, f1) == 16 && offsetof(gm_tfeab279c16rr, f2) == 32, "layout of `Toolchain`");
_Static_assert(sizeof(gm_tfh78knfg7urq) == 10 && _Alignof(gm_tfh78knfg7urq) == 1 && offsetof(gm_tfh78knfg7urq, f0) == 0 && offsetof(gm_tfh78knfg7urq, f1) == 1 && offsetof(gm_tfh78knfg7urq, f2) == 2 && offsetof(gm_tfh78knfg7urq, f3) == 3 && offsetof(gm_tfh78knfg7urq, f4) == 4 && offsetof(gm_tfh78knfg7urq, f5) == 5 && offsetof(gm_tfh78knfg7urq, f6) == 6 && offsetof(gm_tfh78knfg7urq, f7) == 7 && offsetof(gm_tfh78knfg7urq, f8) == 8 && offsetof(gm_tfh78knfg7urq, f9) == 9, "layout of `(Punct, Punct, Punct, Punct, Punct, Punct, Punct, Punct, Punct, Punct)`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
_Static_assert(sizeof(gm_tibupt4dmcqai) == 24 && _Alignof(gm_tibupt4dmcqai) == 8 && offsetof(gm_tibupt4dmcqai, f0) == 0 && offsetof(gm_tibupt4dmcqai, f1) == 16, "layout of `StrLit`");
_Static_assert(sizeof(gm_tk22flr9mh3ql) == 3 && _Alignof(gm_tk22flr9mh3ql) == 1 && offsetof(gm_tk22flr9mh3ql, f0) == 0 && offsetof(gm_tk22flr9mh3ql, f1) == 1 && offsetof(gm_tk22flr9mh3ql, f2) == 2, "layout of `(Keyword, Keyword, Keyword)`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
_Static_assert(sizeof(gm_toceffb697p0u) == 16 && _Alignof(gm_toceffb697p0u) == 4 && offsetof(gm_toceffb697p0u, f0) == 0 && offsetof(gm_toceffb697p0u, f1) == 12, "layout of `?Span`");
_Static_assert(sizeof(gm_tommf4j9upomc) == 24 && _Alignof(gm_tommf4j9upomc) == 8 && offsetof(gm_tommf4j9upomc, f0) == 0 && offsetof(gm_tommf4j9upomc, f1) == 16, "layout of `(string, u32)`");
typedef struct { gm_trqfvmjtt4sij f0; gm_trqfvmjtt4sij f1; gm_trqfvmjtt4sij f2; } gm_tr8dvlu9ik4gj;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; uint64_t f2; void * f3; gm_trqfvmjtt4sij f4; gm_trqfvmjtt4sij f5; gm_trqfvmjtt4sij f6; gm_tc0nbpdhofi37 f7; gm_trqfvmjtt4sij f8; gm_toceffb697p0u f9; gm_trqfvmjtt4sij f10; uint8_t f11; uint8_t pe[7]; } gm_trd6bg8is1v4n;
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
typedef struct { gm_t2eb59dvglnle f0; uint8_t f1; uint8_t pe[3]; } gm_tu1ap1uj3psaf;
_Static_assert(sizeof(gm_t2792tsg5eg72) == 64 && _Alignof(gm_t2792tsg5eg72) == 8 && offsetof(gm_t2792tsg5eg72, f0) == 0 && offsetof(gm_t2792tsg5eg72, f1) == 40 && offsetof(gm_t2792tsg5eg72, f2) == 48 && offsetof(gm_t2792tsg5eg72, f3) == 56, "layout of `Diagnostics`");
_Static_assert(sizeof(gm_t3fcbuehh3bs2) == 32 && _Alignof(gm_t3fcbuehh3bs2) == 8 && offsetof(gm_t3fcbuehh3bs2, f0) == 0 && offsetof(gm_t3fcbuehh3bs2, f1) == 8, "layout of `TokenKind`");
_Static_assert(sizeof(gm_t4gut2c9fj1kt) == 184 && _Alignof(gm_t4gut2c9fj1kt) == 8 && offsetof(gm_t4gut2c9fj1kt, f0) == 0 && offsetof(gm_t4gut2c9fj1kt, f1) == 8 && offsetof(gm_t4gut2c9fj1kt, f2) == 24 && offsetof(gm_t4gut2c9fj1kt, f3) == 40 && offsetof(gm_t4gut2c9fj1kt, f4) == 80 && offsetof(gm_t4gut2c9fj1kt, f5) == 120 && offsetof(gm_t4gut2c9fj1kt, f6) == 160, "layout of `Diagnostic`");
typedef struct { gm_t3fcbuehh3bs2 f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_tm4pc0emhrv1g;
_Static_assert(sizeof(gm_tr8dvlu9ik4gj) == 120 && _Alignof(gm_tr8dvlu9ik4gj) == 8 && offsetof(gm_tr8dvlu9ik4gj, f0) == 0 && offsetof(gm_tr8dvlu9ik4gj, f1) == 40 && offsetof(gm_tr8dvlu9ik4gj, f2) == 80, "layout of `Extras`");
_Static_assert(sizeof(gm_trd6bg8is1v4n) == 288 && _Alignof(gm_trd6bg8is1v4n) == 8 && offsetof(gm_trd6bg8is1v4n, f0) == 0 && offsetof(gm_trd6bg8is1v4n, f1) == 8 && offsetof(gm_trd6bg8is1v4n, f2) == 24 && offsetof(gm_trd6bg8is1v4n, f3) == 32 && offsetof(gm_trd6bg8is1v4n, f4) == 40 && offsetof(gm_trd6bg8is1v4n, f5) == 80 && offsetof(gm_trd6bg8is1v4n, f6) == 120 && offsetof(gm_trd6bg8is1v4n, f7) == 160 && offsetof(gm_trd6bg8is1v4n, f8) == 184 && offsetof(gm_trd6bg8is1v4n, f9) == 224 && offsetof(gm_trd6bg8is1v4n, f10) == 240 && offsetof(gm_trd6bg8is1v4n, f11) == 280, "layout of `Lexer`");
_Static_assert(sizeof(gm_tu1ap1uj3psaf) == 20 && _Alignof(gm_tu1ap1uj3psaf) == 4 && offsetof(gm_tu1ap1uj3psaf, f0) == 0 && offsetof(gm_tu1ap1uj3psaf, f1) == 16, "layout of `?Bracket`");
_Static_assert(sizeof(gm_tm4pc0emhrv1g) == 48 && _Alignof(gm_tm4pc0emhrv1g) == 8 && offsetof(gm_tm4pc0emhrv1g, f0) == 0 && offsetof(gm_tm4pc0emhrv1g, f1) == 32, "layout of `Token`");
struct gm_fr_add_label_50fj7vv {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_backtick_run_l95fb09 {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_block_comment_fvce7qt {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_byte_b4v8vul {
    gm_frame h;
    gm_tqs5p4n2tq19v ret;
};
struct gm_fr_c_equality_u566nc4 {
    gm_frame h;
    gm_t4gut2c9fj1kt l10; /* d */
    gm_b0_1 ret;
};
struct gm_fr_clear_4qp22g2 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_close_all_brackets_bql4j88 {
    gm_frame h;
    gm_t4gut2c9fj1kt l6; /* d */
    gm_b0_1 ret;
};
struct gm_fr_close_bracket_tbjprpe {
    gm_frame h;
    gm_t4gut2c9fj1kt l11; /* d */
    gm_b0_1 ret;
};
struct gm_fr_closer_fus0off {
    gm_frame h;
    gm_t9qkh0rsooc8d ret;
};
struct gm_fr_closer_slot_f4sb6v0 {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_closing_na3a1ur {
    gm_frame h;
    gm_taari1s0m51a7 ret;
};
struct gm_fr_current_char_ctktsl2 {
    gm_frame h;
    gm_tb31k5l794t7p ret;
};
struct gm_fr_declaration_starts_at_mp730g1 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_diagnostics_n3kkhf4 {
    gm_frame h;
    gm_t2792tsg5eg72 ret;
};
struct gm_fr_emit_ekvv703 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_emit_semicolon_7da5b93 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_empty_slice_1th396a {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_3pd55t2 {
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
struct gm_fr_empty_slice_hc7thq6 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_qbupbr6 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_error_p5r0d56 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_flavors_qsu4oq0 {
    gm_frame h;
    gm_t6ofeqo2h242p ret;
};
struct gm_fr_flush_newline_9023ml8 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_has_errors_if7ai0m {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_inline_comment_end_8i5h1eq {
    gm_frame h;
    gm_tf5srh4mp4b3i ret;
};
struct gm_fr_inline_raw_string_end_6lenf33 {
    gm_frame h;
    gm_tf5srh4mp4b3i ret;
};
struct gm_fr_is_alnum_n81ervr {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_alpha_12215ga {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_ascii_space_t2ltvst {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_digit_2hoggnj {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_ident_cont_317fpdb {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_ident_start_rjefpfm {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_lower_4ebokoh {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_punct_fdeminj {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_unicode_space_b2m9rku {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_upper_iekrftp {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_lex_dc8mbmf {
    gm_frame h;
    gm_trd6bg8is1v4n l3; /* lx */
    gm_tr8dvlu9ik4gj ret;
};
struct gm_fr_lex_recovering_obo2892 {
    gm_frame h;
    gm_t2792tsg5eg72 l3; /* first */
    gm_trd6bg8is1v4n l4; /* lx */
    gm_tr8dvlu9ik4gj l5; /* out */
    gm_t4gut2c9fj1kt l8; /* d */
    gm_trd6bg8is1v4n l9; /* again */
    gm_tr8dvlu9ik4gj ret;
};
struct gm_fr_lex_token_h8g44s4 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_lex_word_9li16te {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_line_binds_jm0pnul {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_line_comment_s71u8hq {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_list_2iogcnb {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_5dhgg4i {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_5o6u5a9 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_go2qg0b {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_lpokg1m {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_prbqo9j {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_lone_cr_ktb0jvm {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_new_lexer_dm12g12 {
    gm_frame h;
    gm_trd6bg8is1v4n ret;
};
struct gm_fr_note_newline_cchbr7g {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_one_or_two_lava5tu {
    gm_frame h;
    gm_te0mgnjdphs16 ret;
};
struct gm_fr_open_bracket_3kcj4k8 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_opener_8hqhoc3 {
    gm_frame h;
    gm_t9qkh0rsooc8d ret;
};
struct gm_fr_peek_72bostn {
    gm_frame h;
    gm_tqs5p4n2tq19v ret;
};
struct gm_fr_peek_at_74p8lkq {
    gm_frame h;
    gm_tqs5p4n2tq19v ret;
};
struct gm_fr_pop_1q0s3gu {
    gm_frame h;
    gm_tu1ap1uj3psaf ret;
};
struct gm_fr_pop_j4rpvm5 {
    gm_frame h;
    gm_tf5srh4mp4b3i ret;
};
struct gm_fr_push_newline_ke0tojq {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_report_eardf07 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_run_v26is74 {
    gm_frame h;
    gm_tr8dvlu9ik4gj ret;
};
struct gm_fr_scan_ident_h1b2n4u {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_scan_punct_ecrh51c {
    gm_frame h;
    gm_taari1s0m51a7 ret;
};
struct gm_fr_second_word_end_dpnj7g8 {
    gm_frame h;
    gm_tf5srh4mp4b3i ret;
};
struct gm_fr_skip_trivia_014s7ai {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_slot_4oe07nr {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_span_64igq5o {
    gm_frame h;
    gm_tl874t2atnlh3 ret;
};
struct gm_fr_span_tg7cev6 {
    gm_frame h;
    gm_tl874t2atnlh3 ret;
};
struct gm_fr_starts_continuation_n3333i7 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_suppresses_newline_after_o08ord2 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_truncate_h4ov51t {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_unexpected_char_r3t8d4c {
    gm_frame h;
    gm_b0_1 ret;
};
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b);
inline bool gm_eq2ceic3f8rp(const uint8_t *a, const uint8_t *b);
inline bool gm_eq467d5cm1d5(const uint8_t *a, const uint8_t *b);
inline bool gm_eqb44cb3cf1n(const uint8_t *a, const uint8_t *b);
inline bool gm_eqgj9fu5tboe(const uint8_t *a, const uint8_t *b);
inline bool gm_eqjmccqdb90f(const uint8_t *a, const uint8_t *b);
inline bool gm_eqq6nrh55uon(const uint8_t *a, const uint8_t *b);
inline bool gm_eqv3tj1pu43g(const uint8_t *a, const uint8_t *b);
__attribute__((aligned(4))) gm_b0_1 gm_f_add_fix_0gkcr4c(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_tl874t2atnlh3 a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) void gm_f_add_label_50fj7vv(void * a0, gm_cseahvg8v7p0vs a1, gm_b16_8 a2);
__attribute__((aligned(4))) uint64_t gm_f_backtick_run_l95fb09(gm_b16_8 a0, uint64_t a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_block_comment_fvce7qt(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_tqs5p4n2tq19v gm_f_byte_b4v8vul(void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_c_equality_u566nc4(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1);
inline __attribute__((aligned(4))) void gm_f_clear_4qp22g2(void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_close_all_brackets_bql4j88(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_close_bracket_tbjprpe(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t9qkh0rsooc8d a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) gm_t9qkh0rsooc8d gm_f_closer_fus0off(gm_t9qkh0rsooc8d a0);
__attribute__((aligned(4))) uint64_t gm_f_closer_slot_f4sb6v0(gm_t9qkh0rsooc8d a0);
__attribute__((aligned(4))) gm_taari1s0m51a7 gm_f_closing_na3a1ur(const void * a0);
__attribute__((aligned(4))) gm_taari1s0m51a7 gm_f_closing_na3a1ur_c(gm_t3fcbuehh3bs2 a0);
inline __attribute__((aligned(4))) gm_cskjv6t053tkje gm_f_current_char_ctktsl2(void * a0);
__attribute__((aligned(4))) uint8_t gm_f_declaration_starts_at_mp730g1(void * a0, uint64_t a1, const void * a2);
__attribute__((aligned(4))) uint8_t gm_f_declaration_starts_at_mp730g1_c(void * a0, uint64_t a1, gm_t3fcbuehh3bs2 a2);
__attribute__((aligned(4))) gm_cskjv6t053tkje gm_f_decode_jrk6b03(gm_b16_8 a0, uint64_t a1);
inline __attribute__((aligned(4))) void gm_f_diagnostics_n3kkhf4(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_b0_1 gm_f_emit_ekvv703(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, uint64_t a2, uint64_t a3);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_emit_semicolon_7da5b93(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_1th396a(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_3pd55t2(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_73v4ttb(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_d914i5u(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_hc7thq6(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_qbupbr6(void);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_error_p5r0d56(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, uint64_t a2, gm_b16_8 a3);
__attribute__((aligned(4))) void gm_f_error_s6hn871(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_t6ofeqo2h242p gm_f_flavors_qsu4oq0(uint8_t a0);
__attribute__((aligned(4))) void gm_f_flush_newline_9023ml8(void * a0, const void * a1, uint64_t a2);
__attribute__((aligned(4))) void gm_f_flush_newline_9023ml8_c(void * a0, gm_t3fcbuehh3bs2 a1, uint64_t a2);
__attribute__((aligned(4))) void gm_f_format_7a5f58b(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_d7osfot(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_daod35i(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_dsta2ql(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_fuvjr35(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_g637ghi(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_i801868(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
__attribute__((aligned(4))) void gm_f_format_ooibcq2(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_pscs6r9(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_t32jdqr(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_t4ivgd1(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
inline __attribute__((aligned(4))) uint8_t gm_f_has_errors_if7ai0m(void * a0);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_inline_comment_end_8i5h1eq(gm_b16_8 a0, uint64_t a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_inline_raw_string_end_6lenf33(gm_b16_8 a0, uint64_t a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_alnum_n81ervr(uint8_t a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_alpha_12215ga(uint8_t a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_ascii_space_t2ltvst(uint8_t a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_digit_2hoggnj(uint8_t a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_ident_cont_317fpdb(uint8_t a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_ident_start_rjefpfm(uint8_t a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_lower_4ebokoh(uint8_t a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_punct_fdeminj(const void * a0, gm_t9qkh0rsooc8d a1);
__attribute__((aligned(4))) uint8_t gm_f_is_unicode_space_b2m9rku(uint32_t a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_upper_iekrftp(uint8_t a0);
__attribute__((aligned(4))) gm_taari1s0m51a7 gm_f_keyword_from_1ofmh1u(gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_lex_char_4qk4vjp(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) void gm_f_lex_dc8mbmf(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint32_t a0, gm_b16_8 a1, void * a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_lex_number_9j9sdfi(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_lex_raw_string_uf6bd4g(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_lex_recovering_obo2892(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint32_t a0, gm_b16_8 a1, void * a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_lex_string_bcdbdjm(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_t9qkh0rsooc8d a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_lex_token_h8g44s4(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_lex_word_9li16te(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) uint8_t gm_f_line_binds_jm0pnul(gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_line_comment_s71u8hq(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) void gm_f_list_2iogcnb(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_5dhgg4i(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_5o6u5a9(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_go2qg0b(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_lpokg1m(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_prbqo9j(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_lone_cr_ktb0jvm(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_new_lexer_dm12g12(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint32_t a0, gm_b16_8 a1, void * a2);
inline __attribute__((aligned(4))) void gm_f_note_newline_cchbr7g(void * a0, uint64_t a1, uint64_t a2);
inline __attribute__((aligned(4))) gm_te0mgnjdphs16 gm_f_one_or_two_lava5tu(uint8_t a0, gm_t9qkh0rsooc8d a1, gm_t9qkh0rsooc8d a2);
__attribute__((aligned(4))) void gm_f_open_bracket_3kcj4k8(void * a0, gm_t9qkh0rsooc8d a1, gm_cseahvg8v7p0vs a2);
__attribute__((aligned(4))) gm_t9qkh0rsooc8d gm_f_opener_8hqhoc3(gm_t9qkh0rsooc8d a0);
inline __attribute__((aligned(4))) gm_tqs5p4n2tq19v gm_f_peek_72bostn(void * a0);
inline __attribute__((aligned(4))) gm_tqs5p4n2tq19v gm_f_peek_at_74p8lkq(void * a0, uint64_t a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_pop_1q0s3gu(void *gm_out, void * a0);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tf5srh4mp4b3i gm_f_pop_j4rpvm5(void * a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_punct_text_ov4r40u(gm_t9qkh0rsooc8d a0);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_im9d4ro(void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_kiclmcn(void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_push_newline_ke0tojq(void * a0, gm_cseahvg8v7p0vs a1);
__attribute__((aligned(4))) void gm_f_push_o4ckdr1(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_o6f0em6(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_pnf14h1(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_ucgmlo2(void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_vs3f95a(void * a0, gm_t2eb59dvglnle a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_report_eardf07(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_reserve_i65s8oj(void * a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_run_v26is74(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_scan_ident_h1b2n4u(void * a0);
__attribute__((aligned(4))) gm_taari1s0m51a7 gm_f_scan_punct_ecrh51c(void * a0);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_second_word_end_dpnj7g8(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_skip_trivia_014s7ai(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) uint64_t gm_f_slot_4oe07nr(gm_t9qkh0rsooc8d a0);
inline __attribute__((aligned(4))) gm_cseahvg8v7p0vs gm_f_span_64igq5o(uint32_t a0, uint64_t a1, uint64_t a2);
inline __attribute__((aligned(4))) gm_cseahvg8v7p0vs gm_f_span_tg7cev6(void * a0, uint64_t a1, uint64_t a2);
__attribute__((aligned(4))) uint8_t gm_f_starts_continuation_n3333i7(const void * a0);
__attribute__((aligned(4))) uint8_t gm_f_starts_continuation_n3333i7_c(gm_t3fcbuehh3bs2 a0);
__attribute__((aligned(4))) uint8_t gm_f_starts_with_6vvt01e(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) uint8_t gm_f_suppresses_newline_after_o08ord2(const void * a0);
__attribute__((aligned(4))) uint8_t gm_f_suppresses_newline_after_o08ord2_c(gm_t3fcbuehh3bs2 a0);
inline __attribute__((aligned(4))) void gm_f_truncate_h4ov51t(void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_unexpected_char_r3t8d4c(void *env, gm_node *cx, gm_node *inst, void * a0);
extern uint8_t gm_c644mb9o5bq[16] __attribute__((aligned(8)));
extern const uint8_t gm_s1c4sfff8ac39[7];
extern const uint8_t gm_s2gnpj7k4l22n[2];
extern const uint8_t gm_s3p0rbs9hc3lg[2];
extern const uint8_t gm_s5t1b3eks758h[27];
extern const uint8_t gm_s9r64t1166q1f[5];
extern const uint8_t gm_sb54ui2amic32[2];
extern const uint8_t gm_scvhpklutvpjg[4];
extern const uint8_t gm_sfdbfrv46lihd[2];
extern const uint8_t gm_sgpdmvc9s0d2p[3];
extern const uint8_t gm_sic5vsema0r36[8];
extern const uint8_t gm_sphr6fu31dtrl[3];
extern const uint8_t gm_sr4qbrk6tlgaa[1];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_src41vqrhgekac9[];
extern const char gm_srcgfglnv8p059r[];
extern const char gm_srcrgc9bkquf6nr[];
extern const char gm_srcvmmksi0829pa[];
extern const uint8_t gm_srfagufvviad7[6];
extern const uint8_t gm_srh29phg8b30r[4];
extern const uint8_t gm_ss67f0gmfen1m[3];
extern const uint8_t gm_stnlf4pvbut1c[3];
extern const uint8_t gm_svbhcvlahd4n2[3];
extern const gm_type gm_tyo86ks9qkvt;
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b) {
    uintptr_t n = GM_LD(uintptr_t, a + 8);
    if (n != GM_LD(uintptr_t, b + 8)) return false;
    const uint8_t *pa = GM_LD(void *, a), *pb = GM_LD(void *, b);
    return n == 0 || memcmp(pa, pb, n * 1) == 0;
}
inline bool gm_eq2ceic3f8rp(const uint8_t *a, const uint8_t *b) {
    uint8_t ta = GM_LD(uint8_t, a + 0), tb = GM_LD(uint8_t, b + 0);
    if (ta != tb) return false;
    return true;
}
inline bool gm_eq467d5cm1d5(const uint8_t *a, const uint8_t *b) {
    return GM_LD(uint32_t, a) == GM_LD(uint32_t, b);
}
inline bool gm_eqb44cb3cf1n(const uint8_t *a, const uint8_t *b) {
    if (a[1] != b[1]) return false;
    return !a[1] || gm_eqjmccqdb90f(a, b);
}
inline bool gm_eqgj9fu5tboe(const uint8_t *a, const uint8_t *b) {
    uint8_t ta = GM_LD(uint8_t, a + 0), tb = GM_LD(uint8_t, b + 0);
    if (ta != tb) return false;
    return true;
}
inline bool gm_eqjmccqdb90f(const uint8_t *a, const uint8_t *b) {
    return GM_LD(uint8_t, a) == GM_LD(uint8_t, b);
}
inline bool gm_eqq6nrh55uon(const uint8_t *a, const uint8_t *b) {
    if (a[1] != b[1]) return false;
    return !a[1] || gm_eqgj9fu5tboe(a, b);
}
inline bool gm_eqv3tj1pu43g(const uint8_t *a, const uint8_t *b) {
    uint8_t ta = GM_LD(uint8_t, a + 0), tb = GM_LD(uint8_t, b + 0);
    if (ta != tb) return false;
    return true;
}
inline __attribute__((aligned(4))) void gm_f_add_label_50fj7vv(void * a0, gm_cseahvg8v7p0vs a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_add_label_50fj7vv F;
    void * gm_l0; /* d */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_b16_8 gm_l2; /* message */
    GM_STACK_CHECK(&F, 116u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_tl874t2atnlh3, a1);
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        void * t3 = (void *)(((uint8_t *)t2) + 40);
        gm_tbgd14esf0jvi t4;
        memset((uint8_t *)&t4 + 12, 0, 4);
        gm_tl874t2atnlh3 t5 = gm_l1;
        GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t4 + 0, t5);
        gm_b16_8 t6 = gm_l2;
        GM_ST(gm_b16_8, (uint8_t *)&t4 + 16, t6);
        gm_tftb1odc739t5 t7 = gm_f_push_kiclmcn(t3, &t4);
        gm_tftb1odc739t5 t8 = t7;
        if (((((uint8_t *)&t8))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t8) + 1, gm_srcgfglnv8p059r, 92, 5);
        gm_b0_1 t9 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
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
    GM_STACK_CHECK_SHALLOW(&F, 99u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    gm_b16_8 t2 = GM_LD(gm_b16_8, (((uint8_t *)t1) + 8));
    gm_b16_8 t3 = t2;
    void * t4 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t3)));
    uint64_t t5 = (uintptr_t)GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
    uint64_t t6 = gm_l1;
    uint64_t t7 = (uintptr_t)t6;
    gm_tqs5p4n2tq19v t8;
    if (t7 < t5) {
    uint8_t t9 = GM_LD(uint8_t, ((uint8_t *)t4 + t7 * 1));
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
inline __attribute__((aligned(4))) void gm_f_clear_4qp22g2(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_clear_4qp22g2 F;
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
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 8)));
        GM_ST(uintptr_t, (uint8_t *)&t8 + 8, (uintptr_t)(t7 - t6));
        GM_ST(gm_b16_8, (((uint8_t *)t2) + 0), t8);
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
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
inline __attribute__((aligned(4))) void gm_f_diagnostics_n3kkhf4(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_diagnostics_n3kkhf4 F;
    GM_STACK_CHECK(&F, 40u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memset((uint8_t *)&(*(gm_t2792tsg5eg72 *)gm_out) + 60, 0, 4);
    gm_trqfvmjtt4sij t1;
    gm_f_list_prbqo9j((void *)&t1, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    GM_ST(gm_trqfvmjtt4sij, (uint8_t *)&(*(gm_t2792tsg5eg72 *)gm_out) + 0, t1);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_t2792tsg5eg72 *)gm_out) + 40, ((uint64_t)0ULL));
    GM_ST(uint64_t, (uint8_t *)&(*(gm_t2792tsg5eg72 *)gm_out) + 48, ((uint64_t)0ULL));
    GM_ST(uint32_t, (uint8_t *)&(*(gm_t2792tsg5eg72 *)gm_out) + 56, ((uint32_t)0ULL));
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_emit_semicolon_7da5b93(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1) {
    struct gm_fr_emit_semicolon_7da5b93 F;
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
        gm_t3fcbuehh3bs2 t3;
        GM_ST(uint8_t, (uint8_t *)&t3 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t3 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t3 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t3 + 8, 0);
        GM_ST(uint64_t, (uint8_t *)&t3 + 16, 0);
        GM_ST(uint64_t, (uint8_t *)&t3 + 24, 0);
        GM_ST(uint8_t, ((uint8_t *)&t3 + 0), ((uint8_t)9ULL));
        uint64_t t4 = gm_l1;
        gm_f_flush_newline_9023ml8(t2, &t3, t4);
    }
    {
        void * t5 = gm_l0;
        void * t6 = gm_l0;
        uint64_t t7 = gm_l1;
        uint64_t t8 = gm_l1;
        uint64_t r9; bool o10 = __builtin_add_overflow(t8, ((uint64_t)1ULL), &r9);
        if (o10) gm_trap("integer overflow in `+`", gm_srcvmmksi0829pa, 654, 33);
        uint64_t t11 = r9;
        gm_tl874t2atnlh3 t12 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_tg7cev6(t6, t7, t11));
        gm_f_push_newline_ke0tojq(t5, GM_CONV(gm_cseahvg8v7p0vs, t12));
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_1th396a(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_1th396a F;
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
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 184)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_3pd55t2(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_3pd55t2 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_hc7thq6(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_hc7thq6 F;
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
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 40)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_qbupbr6(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_qbupbr6 F;
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
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 48)));
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
inline __attribute__((aligned(4))) uint8_t gm_f_has_errors_if7ai0m(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_has_errors_if7ai0m F;
    void * gm_l0; /* ds */
    GM_STACK_CHECK_SHALLOW(&F, 27u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    uint64_t t2 = GM_LD(uint64_t, (((uint8_t *)t1) + 40));
    uint8_t t3 = (t2 > ((uint64_t)0ULL));
    F.ret = t3;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_alnum_n81ervr(uint8_t a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_alnum_n81ervr F;
    uint8_t gm_l0; /* c */
    GM_STACK_CHECK_SHALLOW(&F, 8u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t t1 = 0;
    uint8_t t2 = gm_l0;
    uint8_t t3 = gm_f_is_alpha_12215ga(t2);
    if (!t3) {
        uint8_t t4 = gm_l0;
        uint8_t t5 = gm_f_is_digit_2hoggnj(t4);
        t1 = t5;
    } else t1 = 1;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_alpha_12215ga(uint8_t a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_alpha_12215ga F;
    uint8_t gm_l0; /* c */
    GM_STACK_CHECK_SHALLOW(&F, 8u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t t1 = 0;
    uint8_t t2 = gm_l0;
    uint8_t t3 = gm_f_is_upper_iekrftp(t2);
    if (!t3) {
        uint8_t t4 = gm_l0;
        uint8_t t5 = gm_f_is_lower_4ebokoh(t4);
        t1 = t5;
    } else t1 = 1;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_ascii_space_t2ltvst(uint8_t a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_ascii_space_t2ltvst F;
    uint8_t gm_l0; /* c */
    GM_STACK_CHECK_SHALLOW(&F, 17u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t t1 = gm_l0;
    gm_tb1msgo1fu1n3 t2;
    GM_ST(uint8_t, ((uint8_t *)&t2 + 0), ((uint8_t)32ULL));
    GM_ST(uint8_t, ((uint8_t *)&t2 + 1), ((uint8_t)9ULL));
    GM_ST(uint8_t, ((uint8_t *)&t2 + 2), ((uint8_t)10ULL));
    GM_ST(uint8_t, ((uint8_t *)&t2 + 3), ((uint8_t)12ULL));
    GM_ST(uint8_t, ((uint8_t *)&t2 + 4), ((uint8_t)13ULL));
    gm_tb1msgo1fu1n3 t3 = t2;
    uint8_t t4 = 0;
    uint8_t t5 = t1;
    if (gm_eqjmccqdb90f(((uint8_t *)&t3) + 0, ((uint8_t *)&t5))) t4 = 1;
    if (gm_eqjmccqdb90f(((uint8_t *)&t3) + 1, ((uint8_t *)&t5))) t4 = 1;
    if (gm_eqjmccqdb90f(((uint8_t *)&t3) + 2, ((uint8_t *)&t5))) t4 = 1;
    if (gm_eqjmccqdb90f(((uint8_t *)&t3) + 3, ((uint8_t *)&t5))) t4 = 1;
    if (gm_eqjmccqdb90f(((uint8_t *)&t3) + 4, ((uint8_t *)&t5))) t4 = 1;
    uint8_t t6 = (uint8_t)(t4);
    F.ret = t6;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_ident_cont_317fpdb(uint8_t a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_ident_cont_317fpdb F;
    uint8_t gm_l0; /* c */
    GM_STACK_CHECK_SHALLOW(&F, 8u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t t1 = 0;
    uint8_t t2 = gm_l0;
    uint8_t t3 = gm_f_is_alnum_n81ervr(t2);
    if (!t3) {
        uint8_t t4 = gm_l0;
        uint8_t t5 = ((t4 == ((uint8_t)95ULL)));
        t1 = t5;
    } else t1 = 1;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_ident_start_rjefpfm(uint8_t a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_ident_start_rjefpfm F;
    uint8_t gm_l0; /* c */
    GM_STACK_CHECK_SHALLOW(&F, 8u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t t1 = 0;
    uint8_t t2 = gm_l0;
    uint8_t t3 = gm_f_is_alpha_12215ga(t2);
    if (!t3) {
        uint8_t t4 = gm_l0;
        uint8_t t5 = ((t4 == ((uint8_t)95ULL)));
        t1 = t5;
    } else t1 = 1;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_lower_4ebokoh(uint8_t a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_lower_4ebokoh F;
    uint8_t gm_l0; /* c */
    GM_STACK_CHECK_SHALLOW(&F, 8u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t t1 = 0;
    uint8_t t2 = gm_l0;
    uint8_t t3 = (t2 >= ((uint8_t)97ULL));
    if (t3) {
        uint8_t t4 = gm_l0;
        uint8_t t5 = (t4 <= ((uint8_t)122ULL));
        t1 = t5;
    } else t1 = 0;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_punct_fdeminj(const void * a0, gm_t9qkh0rsooc8d a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_punct_fdeminj F;
    gm_t3fcbuehh3bs2 gm_l0; /* k */
    gm_t9qkh0rsooc8d gm_l1; /* p */
    gm_t9qkh0rsooc8d gm_l2; /* q */
    GM_STACK_CHECK_SHALLOW(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l1 = a1;
    uint8_t t1;
    uint8_t t4 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t4 != ((uint8_t)8ULL)) goto mn3;
    gm_t9qkh0rsooc8d t5 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)a0) + 8));
    gm_l2 = t5;
    gm_t9qkh0rsooc8d t6 = gm_l2;
    gm_t9qkh0rsooc8d t7 = gm_l1;
    gm_t9qkh0rsooc8d t8 = t6;
    gm_t9qkh0rsooc8d t9 = t7;
    uint8_t t10 = (gm_eqv3tj1pu43g(((uint8_t *)&t8), ((uint8_t *)&t9)));
    t1 = t10;
    goto me2;
mn3:;
    t1 = ((uint8_t)0);
    goto me2;
mn11:;
    gm_trap("no match arm matched the scrutinee", gm_src41vqrhgekac9, 308, 51);
me2:;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_upper_iekrftp(uint8_t a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_upper_iekrftp F;
    uint8_t gm_l0; /* c */
    GM_STACK_CHECK_SHALLOW(&F, 8u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t t1 = 0;
    uint8_t t2 = gm_l0;
    uint8_t t3 = (t2 >= ((uint8_t)65ULL));
    if (t3) {
        uint8_t t4 = gm_l0;
        uint8_t t5 = (t4 <= ((uint8_t)90ULL));
        t1 = t5;
    } else t1 = 0;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_lex_dc8mbmf(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint32_t a0, gm_b16_8 a1, void * a2) {
    struct gm_fr_lex_dc8mbmf F;
    uint32_t gm_l0; /* file */
    gm_b16_8 gm_l1; /* src */
    void * gm_l2; /* diags */
    GM_STACK_CHECK(&F, 64u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        uint32_t t2 = gm_l0;
        gm_b16_8 t3 = gm_l1;
        void * t4 = gm_l2;
        gm_f_new_lexer_dm12g12((void *)&F.l3, (void *)NULL, cx, NULL, t2, t3, t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t5 = (void *)((uint8_t *)&F.l3);
        gm_f_run_v26is74((void *)&(*(gm_tr8dvlu9ik4gj *)gm_out), (void *)NULL, cx, NULL, t5);
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
inline __attribute__((aligned(4))) void gm_f_list_2iogcnb(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_2iogcnb F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_3pd55t2();
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
inline __attribute__((aligned(4))) void gm_f_list_5o6u5a9(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_5o6u5a9 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_hc7thq6();
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
inline __attribute__((aligned(4))) void gm_f_list_lpokg1m(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_lpokg1m F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_qbupbr6();
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
inline __attribute__((aligned(4))) void gm_f_list_prbqo9j(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_prbqo9j F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_1th396a();
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
inline __attribute__((aligned(4))) void gm_f_note_newline_cchbr7g(void * a0, uint64_t a1, uint64_t a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_note_newline_cchbr7g F;
    void * gm_l0; /* lx */
    uint64_t gm_l1; /* lo */
    uint64_t gm_l2; /* hi */
    GM_STACK_CHECK_SHALLOW(&F, 101u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t3 = gm_l0;
        uint8_t t4;
        if ((((((uint8_t *)t3) + 224))[12] != 0)) goto mn6;
        t4 = ((uint8_t)1);
        goto me5;
mn6:;
        t4 = ((uint8_t)0);
        goto me5;
mn7:;
        gm_trap("no match arm matched the scrutinee", gm_srcvmmksi0829pa, 203, 8);
me5:;
        if (!t4) goto ie2;
        {
            void * t9 = gm_l0;
            uint8_t *t10 = (((uint8_t *)t9) + 224);
            void * t11 = gm_l0;
            uint64_t t12 = gm_l1;
            uint64_t t13 = gm_l2;
            gm_tl874t2atnlh3 t14 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_tg7cev6(t11, t12, t13));
            gm_toceffb697p0u t15;
            GM_ST(uint8_t, (uint8_t *)&t15 + 13, 0);
            GM_ST(uint16_t, (uint8_t *)&t15 + 14, 0);
            GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t15), t14);
            ((uint8_t *)&t15)[12] = 1;
            GM_ST(gm_toceffb697p0u, t10, t15);
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
inline __attribute__((aligned(4))) gm_te0mgnjdphs16 gm_f_one_or_two_lava5tu(uint8_t a0, gm_t9qkh0rsooc8d a1, gm_t9qkh0rsooc8d a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_one_or_two_lava5tu F;
    uint8_t gm_l0; /* two */
    gm_t9qkh0rsooc8d gm_l1; /* long */
    gm_t9qkh0rsooc8d gm_l2; /* short */
    GM_STACK_CHECK_SHALLOW(&F, 118u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_te0mgnjdphs16 t1;
    uint8_t t3 = gm_l0;
    if (!t3) goto ie2;
    gm_t9qkh0rsooc8d t5 = gm_l1;
    gm_te0mgnjdphs16 t6;
    GM_ST(uint8_t, (uint8_t *)&t6 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t6 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t6 + 4, 0);
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t6 + 0), t5);
    GM_ST(uint64_t, ((uint8_t *)&t6 + 8), ((uint64_t)2ULL));
    gm_te0mgnjdphs16 t7 = t6;
    t1 = t7;
    goto ix2;
ie2:;
    gm_t9qkh0rsooc8d t9 = gm_l2;
    gm_te0mgnjdphs16 t10;
    GM_ST(uint8_t, (uint8_t *)&t10 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t10 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t10 + 4, 0);
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t10 + 0), t9);
    GM_ST(uint64_t, ((uint8_t *)&t10 + 8), ((uint64_t)1ULL));
    gm_te0mgnjdphs16 t11 = t10;
    t1 = t11;
ix2:;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tqs5p4n2tq19v gm_f_peek_72bostn(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_peek_72bostn F;
    void * gm_l0; /* lx */
    GM_STACK_CHECK_SHALLOW(&F, 99u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    gm_b16_8 t2 = GM_LD(gm_b16_8, (((uint8_t *)t1) + 8));
    gm_b16_8 t3 = t2;
    void * t4 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t3)));
    uint64_t t5 = (uintptr_t)GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
    void * t6 = gm_l0;
    uint64_t t7 = GM_LD(uint64_t, (((uint8_t *)t6) + 24));
    uint64_t t8 = (uintptr_t)t7;
    gm_tqs5p4n2tq19v t9;
    if (t8 < t5) {
    uint8_t t10 = GM_LD(uint8_t, ((uint8_t *)t4 + t8 * 1));
    gm_tqs5p4n2tq19v t11;
    GM_ST(uint8_t, ((uint8_t *)&t11), t10);
    ((uint8_t *)&t11)[1] = 1;
        t9 = t11;
    } else {
    gm_tqs5p4n2tq19v t12;
    memset(&t12, 0, sizeof t12);
        t9 = t12;
    }
    F.ret = t9;
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
    GM_STACK_CHECK_SHALLOW(&F, 123u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    gm_b16_8 t2 = GM_LD(gm_b16_8, (((uint8_t *)t1) + 8));
    gm_b16_8 t3 = t2;
    void * t4 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t3)));
    uint64_t t5 = (uintptr_t)GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
    void * t6 = gm_l0;
    uint64_t t7 = GM_LD(uint64_t, (((uint8_t *)t6) + 24));
    uint64_t t8 = gm_l1;
    uint64_t r9; bool o10 = __builtin_add_overflow(t7, t8, &r9);
    if (o10) gm_trap("integer overflow in `+`", gm_srcvmmksi0829pa, 153, 64);
    uint64_t t11 = r9;
    uint64_t t12 = (uintptr_t)t11;
    gm_tqs5p4n2tq19v t13;
    if (t12 < t5) {
    uint8_t t14 = GM_LD(uint8_t, ((uint8_t *)t4 + t12 * 1));
    gm_tqs5p4n2tq19v t15;
    GM_ST(uint8_t, ((uint8_t *)&t15), t14);
    ((uint8_t *)&t15)[1] = 1;
        t13 = t15;
    } else {
    gm_tqs5p4n2tq19v t16;
    memset(&t16, 0, sizeof t16);
        t13 = t16;
    }
    F.ret = t13;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_pop_1q0s3gu(void *gm_out, void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_pop_1q0s3gu F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* n */
    gm_t2eb59dvglnle gm_l2; /* x */
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
            gm_tu1ap1uj3psaf t8;
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
        gm_t2eb59dvglnle t17 = GM_LD(gm_t2eb59dvglnle, ((uint8_t *)GM_LD(void *, (((uint8_t *)t15) + 0)) + t14 * 16));
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
        gm_t2eb59dvglnle t29 = gm_l2;
        gm_tu1ap1uj3psaf t30;
        GM_ST(uint8_t, (uint8_t *)&t30 + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&t30 + 18, 0);
        GM_ST(gm_t2eb59dvglnle, ((uint8_t *)&t30), t29);
        ((uint8_t *)&t30)[16] = 1;
        uint8_t t31 = 0;
        F.ret = t30;
        goto gm_fret;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tf5srh4mp4b3i gm_f_pop_j4rpvm5(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_pop_j4rpvm5 F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* n */
    uint64_t gm_l2; /* x */
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
            gm_tf5srh4mp4b3i t8;
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
        uint64_t t17 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)t15) + 0)) + t14 * 8));
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
        GM_ST(void *, (uint8_t *)&t28, (void *)(((uint8_t *)t21 + t22 * 8)));
        GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(t27 - t22));
        GM_ST(gm_b16_8, (((uint8_t *)t18) + 0), t28);
    }
    {
        uint64_t t29 = gm_l2;
        gm_tf5srh4mp4b3i t30;
        GM_ST(uint8_t, (uint8_t *)&t30 + 9, 0);
        GM_ST(uint16_t, (uint8_t *)&t30 + 10, 0);
        GM_ST(uint32_t, (uint8_t *)&t30 + 12, 0);
        GM_ST(uint64_t, ((uint8_t *)&t30), t29);
        ((uint8_t *)&t30)[8] = 1;
        uint8_t t31 = 0;
        F.ret = t30;
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
inline __attribute__((aligned(4))) uint64_t gm_f_slot_4oe07nr(gm_t9qkh0rsooc8d a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_slot_4oe07nr F;
    gm_t9qkh0rsooc8d gm_l0; /* k */
    GM_STACK_CHECK_SHALLOW(&F, 30u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint64_t t1;
    uint8_t t7 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    switch (t7) { case ((uint8_t)0ULL): goto ma3; case ((uint8_t)1ULL): goto ma4; case ((uint8_t)2ULL): goto ma5; case ((uint8_t)3ULL): goto ma5; default: goto ma6; }
ma3:;
    uint8_t t9 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t9 != ((uint8_t)0ULL)) goto mn8;
    t1 = ((uint64_t)0ULL);
    goto me2;
mn8:;
ma4:;
    uint8_t t11 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t11 != ((uint8_t)1ULL)) goto mn10;
    t1 = ((uint64_t)1ULL);
    goto me2;
mn10:;
ma5:;
    uint8_t t15 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t15 != ((uint8_t)2ULL)) goto pa14;
    goto po13;
pa14:;
    uint8_t t17 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t17 != ((uint8_t)3ULL)) goto pa16;
    goto po13;
pa16:;
    goto mn12;
po13:;
    t1 = ((uint64_t)2ULL);
    goto me2;
mn12:;
ma6:;
    gm_trap("no match arm matched the scrutinee", gm_srcvmmksi0829pa, 60, 40);
me2:;
    F.ret = t1;
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
inline __attribute__((aligned(4))) void gm_f_truncate_h4ov51t(void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_truncate_h4ov51t F;
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
            if (t12 > t14 || t14 > (uintptr_t)t10) gm_trap_slice(t12, t14, t10, gm_srcrgc9bkquf6nr, 122, 36);
            gm_b16_8 t15;
            GM_ST(void *, (uint8_t *)&t15, (void *)(((uint8_t *)t11 + t12 * 16)));
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
inline __attribute__((aligned(4))) uint64_t gm_f_backtick_run_l95fb09(gm_b16_8 a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_backtick_run_l95fb09 F;
    gm_b16_8 gm_l0; /* s */
    uint64_t gm_l1; /* i */
    uint64_t gm_l2; /* j */
    GM_STACK_CHECK_SHALLOW(&F, 140u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint64_t t2 = gm_l1;
        gm_l2 = t2;
    }
    {
        for (;;) {
            uint64_t t4 = gm_l2;
            uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            uint8_t t6 = (t4 < t5);
            if (!t6) goto lb3;
            uint64_t t7 = gm_l2;
            uint64_t t8 = (uintptr_t)t7;
            uint64_t t9 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            if (t8 >= (uintptr_t)(t9)) gm_trap_index(t8, t9, gm_srcvmmksi0829pa, 732, 25);
            uint8_t t10 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t8 * 1));
            uint8_t t11 = ((t10 == ((uint8_t)96ULL)));
            if (!t11) goto lb3;
            {
                uint64_t t14 = gm_l2;
                uint64_t r15; bool o16 = __builtin_add_overflow(t14, ((uint64_t)1ULL), &r15);
                if (o16) gm_trap("integer overflow in `+`", gm_srcvmmksi0829pa, 732, 39);
                uint64_t t17 = r15;
                gm_l2 = t17;
            }
lc3:;
        }
lb3:;
    }
    {
        uint64_t t18 = gm_l2;
        uint64_t t19 = gm_l1;
        uint64_t r20; bool o21 = __builtin_sub_overflow(t18, t19, &r20);
        if (o21) gm_trap("integer overflow in `-`", gm_srcvmmksi0829pa, 733, 12);
        uint64_t t22 = r20;
        uint8_t t23 = 0;
        F.ret = t22;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_b0_1 gm_f_block_comment_fvce7qt(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_block_comment_fvce7qt F;
    void * gm_l0; /* lx */
    uint64_t gm_l1; /* lo */
    uint64_t gm_l2; /* i */
    int32_t gm_l3; /* depth */
    uint8_t gm_l4; /* b */
    gm_tqs5p4n2tq19v gm_l5; /* next */
    GM_STACK_CHECK(&F, 441u);
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
        uint64_t t4 = gm_l1;
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)2ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_srcvmmksi0829pa, 235, 10);
        uint64_t t7 = r5;
        gm_l2 = t7;
    }
    {
        gm_l3 = ((int32_t)1LL);
    }
    {
        for (;;) {
            int32_t t9 = gm_l3;
            uint8_t t10 = (t9 > ((int32_t)0LL));
            if (!t10) goto lb8;
            {
                void * t13 = gm_l0;
                uint64_t t14 = gm_l2;
                gm_tqs5p4n2tq19v t15 = gm_f_byte_b4v8vul(t13, t14);
                gm_tqs5p4n2tq19v t16 = t15;
                uint8_t t17;
                if (((((uint8_t *)&t16))[1] != 0)) {
                    uint8_t t18 = GM_LD(uint8_t, (((uint8_t *)&t16) + 0));
                    t17 = t18;
                } else {
                    {
                        void * t20 = gm_l0;
                        uint64_t t21 = gm_l1;
                        uint64_t t22 = gm_l1;
                        uint64_t r23; bool o24 = __builtin_add_overflow(t22, ((uint64_t)2ULL), &r23);
                        if (o24) gm_trap("integer overflow in `+`", gm_srcvmmksi0829pa, 239, 26);
                        uint64_t t25 = r23;
                        gm_b16_8 t26;
                        GM_ST(void *, (uint8_t *)&t26, (void *)(gm_s5t1b3eks758h));
                        GM_ST(uintptr_t, (uint8_t *)&t26 + 8, (uintptr_t)(26));
                        gm_b0_1 t27 = gm_f_error_p5r0d56((void *)NULL, cx, NULL, t20, t21, t25, t26);
                        if (gm_unw.kind) {
                            goto gm_ll11;
                        }
                    }
                    goto lb8;
                }
                gm_l4 = t17;
            }
            {
                void * t28 = gm_l0;
                uint64_t t29 = gm_l2;
                uint64_t r30; bool o31 = __builtin_add_overflow(t29, ((uint64_t)1ULL), &r30);
                if (o31) gm_trap("integer overflow in `+`", gm_srcvmmksi0829pa, 242, 25);
                uint64_t t32 = r30;
                gm_tqs5p4n2tq19v t33 = gm_f_byte_b4v8vul(t28, t32);
                gm_l5 = t33;
            }
            {
                uint8_t t35 = gm_l4;
                uint8_t t36 = ((t35 == ((uint8_t)47ULL)));
                if (!t36) goto ie34;
                gm_tqs5p4n2tq19v t37 = gm_l5;
                gm_tqs5p4n2tq19v t38;
                GM_ST(uint8_t, ((uint8_t *)&t38), ((uint8_t)42ULL));
                ((uint8_t *)&t38)[1] = 1;
                gm_tqs5p4n2tq19v t39 = t37;
                gm_tqs5p4n2tq19v t40 = t38;
                uint8_t t41 = (gm_eqb44cb3cf1n(((uint8_t *)&t39), ((uint8_t *)&t40)));
                if (!t41) goto ie34;
                {
                    int32_t t43 = gm_l3;
                    int32_t r44; bool o45 = __builtin_add_overflow(t43, ((int32_t)1LL), &r44);
                    if (o45) gm_trap("integer overflow in `+`", gm_srcvmmksi0829pa, 244, 13);
                    int32_t t46 = r44;
                    gm_l3 = t46;
                }
                {
                    uint64_t t47 = gm_l2;
                    uint64_t r48; bool o49 = __builtin_add_overflow(t47, ((uint64_t)2ULL), &r48);
                    if (o49) gm_trap("integer overflow in `+`", gm_srcvmmksi0829pa, 245, 13);
                    uint64_t t50 = r48;
                    gm_l2 = t50;
                }
                goto ix34;
ie34:;
                uint8_t t52 = gm_l4;
                uint8_t t53 = ((t52 == ((uint8_t)42ULL)));
                if (!t53) goto ie51;
                gm_tqs5p4n2tq19v t54 = gm_l5;
                gm_tqs5p4n2tq19v t55;
                GM_ST(uint8_t, ((uint8_t *)&t55), ((uint8_t)47ULL));
                ((uint8_t *)&t55)[1] = 1;
                gm_tqs5p4n2tq19v t56 = t54;
                gm_tqs5p4n2tq19v t57 = t55;
                uint8_t t58 = (gm_eqb44cb3cf1n(((uint8_t *)&t56), ((uint8_t *)&t57)));
                if (!t58) goto ie51;
                {
                    int32_t t60 = gm_l3;
                    int32_t r61; bool o62 = __builtin_sub_overflow(t60, ((int32_t)1LL), &r61);
                    if (o62) gm_trap("integer overflow in `-`", gm_srcvmmksi0829pa, 247, 13);
                    int32_t t63 = r61;
                    gm_l3 = t63;
                }
                {
                    uint64_t t64 = gm_l2;
                    uint64_t r65; bool o66 = __builtin_add_overflow(t64, ((uint64_t)2ULL), &r65);
                    if (o66) gm_trap("integer overflow in `+`", gm_srcvmmksi0829pa, 248, 13);
                    uint64_t t67 = r65;
                    gm_l2 = t67;
                }
                goto ix51;
ie51:;
                uint8_t t69 = gm_l4;
                uint8_t t70 = ((t69 == ((uint8_t)10ULL)));
                if (!t70) goto ie68;
                {
                    void * t72 = gm_l0;
                    uint64_t t73 = gm_l2;
                    uint64_t t74 = gm_l2;
                    uint64_t r75; bool o76 = __builtin_add_overflow(t74, ((uint64_t)1ULL), &r75);
                    if (o76) gm_trap("integer overflow in `+`", gm_srcvmmksi0829pa, 250, 32);
                    uint64_t t77 = r75;
                    gm_f_note_newline_cchbr7g(t72, t73, t77);
                }
                {
                    uint64_t t78 = gm_l2;
                    uint64_t r79; bool o80 = __builtin_add_overflow(t78, ((uint64_t)1ULL), &r79);
                    if (o80) gm_trap("integer overflow in `+`", gm_srcvmmksi0829pa, 251, 13);
                    uint64_t t81 = r79;
                    gm_l2 = t81;
                }
                goto ix68;
ie68:;
                uint8_t t83 = gm_l4;
                uint8_t t84 = ((t83 == ((uint8_t)13ULL)));
                if (!t84) goto ie82;
                gm_tqs5p4n2tq19v t85 = gm_l5;
                gm_tqs5p4n2tq19v t86;
                GM_ST(uint8_t, ((uint8_t *)&t86), ((uint8_t)10ULL));
                ((uint8_t *)&t86)[1] = 1;
                gm_tqs5p4n2tq19v t87 = t85;
                gm_tqs5p4n2tq19v t88 = t86;
                uint8_t t89 = (gm_eqb44cb3cf1n(((uint8_t *)&t87), ((uint8_t *)&t88)));
                if (!t89) goto ie82;
                {
                    void * t91 = gm_l0;
                    uint64_t t92 = gm_l2;
                    uint64_t t93 = gm_l2;
                    uint64_t r94; bool o95 = __builtin_add_overflow(t93, ((uint64_t)2ULL), &r94);
                    if (o95) gm_trap("integer overflow in `+`", gm_srcvmmksi0829pa, 253, 32);
                    uint64_t t96 = r94;
                    gm_f_note_newline_cchbr7g(t91, t92, t96);
                }
                {
                    uint64_t t97 = gm_l2;
                    uint64_t r98; bool o99 = __builtin_add_overflow(t97, ((uint64_t)2ULL), &r98);
                    if (o99) gm_trap("integer overflow in `+`", gm_srcvmmksi0829pa, 254, 13);
                    uint64_t t100 = r98;
                    gm_l2 = t100;
                }
                goto ix82;
ie82:;
                uint8_t t102 = gm_l4;
                uint8_t t103 = ((t102 == ((uint8_t)13ULL)));
                if (!t103) goto ie101;
                {
                    void * t105 = gm_l0;
                    uint64_t t106 = gm_l2;
                    uint64_t t107 = gm_l2;
                    uint64_t r108; bool o109 = __builtin_add_overflow(t107, ((uint64_t)1ULL), &r108);
                    if (o109) gm_trap("integer overflow in `+`", gm_srcvmmksi0829pa, 256, 25);
                    uint64_t t110 = r108;
                    gm_b16_8 t111 = GM_LD(gm_b16_8, gm_c644mb9o5bq);
                    gm_b0_1 t112 = gm_f_error_p5r0d56((void *)NULL, cx, NULL, t105, t106, t110, t111);
                    if (gm_unw.kind) {
                        goto gm_ll11;
                    }
                }
                {
                    uint64_t t113 = gm_l2;
                    uint64_t r114; bool o115 = __builtin_add_overflow(t113, ((uint64_t)1ULL), &r114);
                    if (o115) gm_trap("integer overflow in `+`", gm_srcvmmksi0829pa, 257, 13);
                    uint64_t t116 = r114;
                    gm_l2 = t116;
                }
                goto ix101;
ie101:;
                {
                    uint64_t t118 = gm_l2;
                    uint64_t r119; bool o120 = __builtin_add_overflow(t118, ((uint64_t)1ULL), &r119);
                    if (o120) gm_trap("integer overflow in `+`", gm_srcvmmksi0829pa, 259, 13);
                    uint64_t t121 = r119;
                    gm_l2 = t121;
                }
ix101:;
ix82:;
ix68:;
ix51:;
ix34:;
            }
lc8:;
        }
gm_ll11:;
        if (gm_unw.frame == &F.h && gm_unw.label == 0) {
            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb8; }
            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc8; }
        }
        goto gm_lad;
lb8:;
    }
    {
        void * t122 = gm_l0;
        uint64_t t123 = gm_l2;
        GM_ST(uint64_t, (((uint8_t *)t122) + 24), t123);
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
extern __attribute__((aligned(4))) gm_tqs5p4n2tq19v gm_f_byte_b4v8vul(void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_c_equality_u566nc4(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1) {
    struct gm_fr_c_equality_u566nc4 F;
    void * gm_l0; /* lx */
    uint8_t gm_l1; /* b */
    uint64_t gm_l2; /* lo */
    uint8_t gm_l3; /* eq */
    gm_b16_8 gm_l4; /* op */
    gm_b16_8 gm_l5; /* written */
    gm_t3fcbuehh3bs2 gm_l6; /* kind */
    gm_b16_8 gm_l7; /* before */
    gm_b16_8 gm_l8; /* after */
    uint8_t gm_l9; /* c */
    GM_STACK_CHECK(&F, 1294u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        uint64_t t3 = GM_LD(uint64_t, (((uint8_t *)t2) + 24));
        gm_l2 = t3;
    }
    {
        void * t4 = gm_l0;
        void * t5 = gm_l0;
        uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)t5) + 24));
        uint64_t r7; bool o8 = __builtin_add_overflow(t6, ((uint64_t)2ULL), &r7);
        if (o8) gm_trap("integer overflow in `+`", gm_srcvmmksi0829pa, 329, 5);
        uint64_t t9 = r7;
        GM_ST(uint64_t, (((uint8_t *)t4) + 24), t9);
    }
    {
        uint8_t t10 = gm_l1;
        uint8_t t11 = ((t10 == ((uint8_t)61ULL)));
        gm_l3 = t11;
    }
    {
        gm_b16_8 t12;
        uint8_t t14 = gm_l3;
        if (!t14) goto ie13;
        gm_b16_8 t16;
        GM_ST(void *, (uint8_t *)&t16, (void *)(gm_sgpdmvc9s0d2p));
        GM_ST(uintptr_t, (uint8_t *)&t16 + 8, (uintptr_t)(2));
        gm_b16_8 t17 = t16;
        t12 = t17;
        goto ix13;
ie13:;
        gm_b16_8 t19;
        GM_ST(void *, (uint8_t *)&t19, (void *)(gm_s1c4sfff8ac39));
        GM_ST(uintptr_t, (uint8_t *)&t19 + 8, (uintptr_t)(6));
        gm_b16_8 t20 = t19;
        t12 = t20;
ix13:;
        gm_l4 = t12;
    }
    {
        gm_b16_8 t21;
        uint8_t t23 = gm_l3;
        if (!t23) goto ie22;
        gm_b16_8 t25;
        GM_ST(void *, (uint8_t *)&t25, (void *)(gm_sphr6fu31dtrl));
        GM_ST(uintptr_t, (uint8_t *)&t25 + 8, (uintptr_t)(2));
        gm_b16_8 t26 = t25;
        t21 = t26;
        goto ix22;
ie22:;
        gm_b16_8 t28;
        GM_ST(void *, (uint8_t *)&t28, (void *)(gm_stnlf4pvbut1c));
        GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(2));
        gm_b16_8 t29 = t28;
        t21 = t29;
ix22:;
        gm_l5 = t21;
    }
    {
        gm_t3fcbuehh3bs2 t30;
        uint8_t t32 = gm_l3;
        if (!t32) goto ie31;
        gm_t9qkh0rsooc8d t34;
        GM_ST(uint8_t, ((uint8_t *)&t34 + 0), ((uint8_t)16ULL));
        gm_t3fcbuehh3bs2 t35;
        GM_ST(uint8_t, (uint8_t *)&t35 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t35 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t35 + 4, 0);
        GM_ST(uint8_t, (uint8_t *)&t35 + 9, 0);
        GM_ST(uint16_t, (uint8_t *)&t35 + 10, 0);
        GM_ST(uint32_t, (uint8_t *)&t35 + 12, 0);
        GM_ST(uint64_t, (uint8_t *)&t35 + 16, 0);
        GM_ST(uint64_t, (uint8_t *)&t35 + 24, 0);
        GM_ST(uint8_t, ((uint8_t *)&t35 + 0), ((uint8_t)2ULL));
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t35 + 8), t34);
        gm_t3fcbuehh3bs2 t36 = t35;
        t30 = t36;
        goto ix31;
ie31:;
        gm_t9qkh0rsooc8d t38;
        GM_ST(uint8_t, ((uint8_t *)&t38 + 0), ((uint8_t)67ULL));
        gm_t3fcbuehh3bs2 t39;
        GM_ST(uint8_t, (uint8_t *)&t39 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t39 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t39 + 4, 0);
        GM_ST(uint8_t, (uint8_t *)&t39 + 9, 0);
        GM_ST(uint16_t, (uint8_t *)&t39 + 10, 0);
        GM_ST(uint32_t, (uint8_t *)&t39 + 12, 0);
        GM_ST(uint64_t, (uint8_t *)&t39 + 16, 0);
        GM_ST(uint64_t, (uint8_t *)&t39 + 24, 0);
        GM_ST(uint8_t, ((uint8_t *)&t39 + 0), ((uint8_t)8ULL));
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t39 + 8), t38);
        gm_t3fcbuehh3bs2 t40 = t39;
        t30 = t40;
ix31:;
        gm_l6 = t30;
    }
    {
        gm_b16_8 t41;
        uint64_t t43 = gm_l2;
        uint8_t t44 = (t43 > ((uint64_t)0ULL));
        if (!t44) goto ie42;
        uint64_t t45 = gm_l2;
        uint64_t r46; bool o47 = __builtin_sub_overflow(t45, ((uint64_t)1ULL), &r46);
        if (o47) gm_trap("integer overflow in `-`", gm_srcvmmksi0829pa, 335, 55);
        uint64_t t48 = r46;
        uint64_t t49 = (uintptr_t)t48;
        void * t50 = gm_l0;
        uint64_t t51 = GM_LD(uintptr_t, (((uint8_t *)t50) + 8) + 8);
        if (t49 >= (uintptr_t)(t51)) gm_trap_index(t49, t51, gm_srcvmmksi0829pa, 335, 48);
        uint8_t t52 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)t50) + 8)) + t49 * 1));
        uint8_t t53 = gm_f_is_ascii_space_t2ltvst(t52);
        uint8_t t54 = (uint8_t)!t53;
        if (!t54) goto ie42;
        gm_b16_8 t56;
        GM_ST(void *, (uint8_t *)&t56, (void *)(gm_s3p0rbs9hc3lg));
        GM_ST(uintptr_t, (uint8_t *)&t56 + 8, (uintptr_t)(1));
        gm_b16_8 t57 = t56;
        t41 = t57;
        goto ix42;
ie42:;
        gm_b16_8 t59;
        GM_ST(void *, (uint8_t *)&t59, (void *)(gm_sr4qbrk6tlgaa));
        GM_ST(uintptr_t, (uint8_t *)&t59 + 8, (uintptr_t)(0));
        gm_b16_8 t60 = t59;
        t41 = t60;
ix42:;
        gm_l7 = t41;
    }
    {
        gm_b16_8 t61;
        GM_ST(void *, (uint8_t *)&t61, (void *)(gm_sr4qbrk6tlgaa));
        GM_ST(uintptr_t, (uint8_t *)&t61 + 8, (uintptr_t)(0));
        gm_l8 = t61;
    }
    {
        void * t63 = gm_l0;
        gm_tqs5p4n2tq19v t64 = gm_f_peek_72bostn(t63);
        gm_tqs5p4n2tq19v t65 = t64;
        if (!((((uint8_t *)&t65))[1] != 0)) goto ie62;
        uint8_t t66 = GM_LD(uint8_t, ((uint8_t *)&t65));
        gm_l9 = t66;
        {
            uint8_t t69 = gm_l9;
            uint8_t t70 = gm_f_is_ascii_space_t2ltvst(t69);
            uint8_t t71 = (uint8_t)!t70;
            if (!t71) goto ie68;
            {
                gm_b16_8 t73;
                GM_ST(void *, (uint8_t *)&t73, (void *)(gm_s3p0rbs9hc3lg));
                GM_ST(uintptr_t, (uint8_t *)&t73 + 8, (uintptr_t)(1));
                gm_l8 = t73;
            }
            goto ix68;
ie68:;
ix68:;
        }
        goto ix62;
ie62:;
ix62:;
    }
    {
        void * t74 = gm_l0;
        uint64_t t75 = gm_l2;
        void * t76 = gm_l0;
        uint64_t t77 = GM_LD(uint64_t, (((uint8_t *)t76) + 24));
        gm_tl874t2atnlh3 t78 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_tg7cev6(t74, t75, t77));
        gm_t6j6smj7qoctu t79;
        gm_b16_8 t80 = gm_l5;
        GM_ST(gm_b16_8, (uint8_t *)&t79 + 0, t80);
        gm_b16_8 t81 = gm_l4;
        GM_ST(gm_b16_8, (uint8_t *)&t79 + 16, t81);
        gm_tabb710d9mq2e t82;
        gm_f_format_g637ghi((void *)&t82, (void *)NULL, cx, NULL, &t79);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t83 = t82;
        if (((((uint8_t *)&t83))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t83) + 8, gm_srcvmmksi0829pa, 340, 42);
        gm_b16_8 t84 = GM_LD(gm_b16_8, (((uint8_t *)&t83) + 8));
        gm_b16_8 t85 = t84;
        gm_b16_8 t86 = GM_LD(gm_b16_8, ((uint8_t *)&t85));
        gm_f_error_s6hn871((void *)&F.l10, (void *)NULL, cx, NULL, t78, t86);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t87 = (void *)((uint8_t *)&F.l10);
        gm_b16_8 t88 = gm_l4;
        gm_tnpifv20irnph t89;
        GM_ST(gm_b16_8, ((uint8_t *)&t89 + 0), t88);
        gm_tabb710d9mq2e t90;
        gm_f_format_ooibcq2((void *)&t90, (void *)NULL, cx, NULL, t89);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t91 = t90;
        if (((((uint8_t *)&t91))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t91) + 8, gm_srcvmmksi0829pa, 341, 15);
        gm_b16_8 t92 = GM_LD(gm_b16_8, (((uint8_t *)&t91) + 8));
        gm_b16_8 t93 = t92;
        gm_b16_8 t94 = GM_LD(gm_b16_8, ((uint8_t *)&t93));
        void * t95 = gm_l0;
        uint64_t t96 = gm_l2;
        void * t97 = gm_l0;
        uint64_t t98 = GM_LD(uint64_t, (((uint8_t *)t97) + 24));
        gm_tl874t2atnlh3 t99 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_tg7cev6(t95, t96, t98));
        gm_tfeab279c16rr t100;
        gm_b16_8 t101 = gm_l7;
        GM_ST(gm_b16_8, (uint8_t *)&t100 + 0, t101);
        gm_b16_8 t102 = gm_l4;
        GM_ST(gm_b16_8, (uint8_t *)&t100 + 16, t102);
        gm_b16_8 t103 = gm_l8;
        GM_ST(gm_b16_8, (uint8_t *)&t100 + 32, t103);
        gm_tabb710d9mq2e t104;
        gm_f_format_dsta2ql((void *)&t104, (void *)NULL, cx, NULL, &t100);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t105 = t104;
        if (((((uint8_t *)&t105))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t105) + 8, gm_srcvmmksi0829pa, 341, 67);
        gm_b16_8 t106 = GM_LD(gm_b16_8, (((uint8_t *)&t105) + 8));
        gm_b16_8 t107 = t106;
        gm_b16_8 t108 = GM_LD(gm_b16_8, ((uint8_t *)&t107));
        gm_b0_1 t109 = gm_f_add_fix_0gkcr4c((void *)NULL, cx, NULL, t87, t94, t99, t108);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t110 = gm_l0;
        void * t111 = GM_LD(void *, (((uint8_t *)t110) + 32));
        gm_f_push_o4ckdr1(t111, &(*(gm_t4gut2c9fj1kt *)((uint8_t *)&F.l10)));
    }
    {
        void * t112 = gm_l0;
        uint64_t t113 = gm_l2;
        void * t114 = gm_l0;
        uint64_t t115 = GM_LD(uint64_t, (((uint8_t *)t114) + 24));
        gm_b0_1 t116 = gm_f_emit_ekvv703((void *)NULL, cx, NULL, t112, &(*(gm_t3fcbuehh3bs2 *)((uint8_t *)&gm_l6)), t113, t115);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_close_all_brackets_bql4j88(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1) {
    struct gm_fr_close_all_brackets_bql4j88 F;
    void * gm_l0; /* lx */
    uint64_t gm_l1; /* lo */
    gm_tl874t2atnlh3 gm_l2; /* here */
    gm_t2eb59dvglnle gm_l3; /* b */
    void * gm_l4; /* <tmp> */
    gm_b16_8 gm_l5; /* open */
    GM_STACK_CHECK(&F, 737u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        gm_toceffb697p0u t3;
        memset(&t3, 0, sizeof t3);
        GM_ST(gm_toceffb697p0u, (((uint8_t *)t2) + 224), t3);
    }
    {
        void * t4 = gm_l0;
        uint64_t t5 = gm_l1;
        uint64_t t6 = gm_l1;
        gm_tl874t2atnlh3 t7 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_tg7cev6(t4, t5, t6));
        gm_l2 = t7;
    }
    {
        for (;;) {
            void * t9 = gm_l0;
            void * t10 = (void *)(((uint8_t *)t9) + 120);
            gm_tu1ap1uj3psaf t11;
            gm_f_pop_1q0s3gu((void *)&t11, t10);
            gm_tu1ap1uj3psaf t12 = t11;
            if (!((((uint8_t *)&t12))[16] != 0)) goto lb8;
            gm_t2eb59dvglnle t13 = GM_LD(gm_t2eb59dvglnle, ((uint8_t *)&t12));
            gm_l3 = t13;
            {
                {
                    void * t17 = gm_l0;
                    uint8_t *t18 = (uint8_t *)((((uint8_t *)t17) + 160));
                    gm_t9qkh0rsooc8d t19 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l3) + 0));
                    uint64_t t20 = gm_f_slot_4oe07nr(t19);
                    uint64_t t21 = (uintptr_t)t20;
                    if (t21 >= (uintptr_t)(3)) gm_trap_index(t21, 3, gm_srcvmmksi0829pa, 612, 9);
                    void * t22 = (void *)(t18 + t21 * 8);
                    gm_l4 = t22;
                }
                {
                    void * t23 = gm_l4;
                    void * t24 = gm_l4;
                    uint64_t t25 = GM_LD(uint64_t, ((uint8_t *)t24));
                    uint64_t r26; bool o27 = __builtin_sub_overflow(t25, ((uint64_t)1ULL), &r26);
                    if (o27) gm_trap("integer overflow in `-`", gm_srcvmmksi0829pa, 612, 9);
                    uint64_t t28 = r26;
                    GM_ST(uint64_t, ((uint8_t *)t23), t28);
                }
            }
            {
                gm_t9qkh0rsooc8d t29 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l3) + 0));
                gm_t9qkh0rsooc8d t30 = gm_f_opener_8hqhoc3(t29);
                gm_b16_8 t31 = gm_f_punct_text_ov4r40u(t30);
                gm_l5 = t31;
            }
            {
                gm_tl874t2atnlh3 t32 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l3) + 4));
                gm_b16_8 t33 = gm_l5;
                gm_tnpifv20irnph t34;
                GM_ST(gm_b16_8, ((uint8_t *)&t34 + 0), t33);
                gm_tabb710d9mq2e t35;
                gm_f_format_t32jdqr((void *)&t35, (void *)NULL, cx, NULL, t34);
                if (gm_unw.kind) {
                    goto gm_ll14;
                }
                gm_tabb710d9mq2e t36 = t35;
                if (((((uint8_t *)&t36))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t36) + 8, gm_srcvmmksi0829pa, 614, 33);
                gm_b16_8 t37 = GM_LD(gm_b16_8, (((uint8_t *)&t36) + 8));
                gm_b16_8 t38 = t37;
                gm_b16_8 t39 = GM_LD(gm_b16_8, ((uint8_t *)&t38));
                gm_f_error_s6hn871((void *)&F.l6, (void *)NULL, cx, NULL, t32, t39);
                if (gm_unw.kind) {
                    goto gm_ll14;
                }
            }
            {
                void * t40 = (void *)((uint8_t *)&F.l6);
                gm_tl874t2atnlh3 t41 = gm_l2;
                gm_b16_8 t42 = gm_l5;
                gm_tnpifv20irnph t43;
                GM_ST(gm_b16_8, ((uint8_t *)&t43 + 0), t42);
                gm_tabb710d9mq2e t44;
                gm_f_format_d7osfot((void *)&t44, (void *)NULL, cx, NULL, t43);
                if (gm_unw.kind) {
                    goto gm_ll14;
                }
                gm_tabb710d9mq2e t45 = t44;
                if (((((uint8_t *)&t45))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t45) + 8, gm_srcvmmksi0829pa, 615, 27);
                gm_b16_8 t46 = GM_LD(gm_b16_8, (((uint8_t *)&t45) + 8));
                gm_b16_8 t47 = t46;
                gm_b16_8 t48 = GM_LD(gm_b16_8, ((uint8_t *)&t47));
                gm_f_add_label_50fj7vv(t40, GM_CONV(gm_cseahvg8v7p0vs, t41), t48);
            }
            {
                void * t49 = gm_l0;
                void * t50 = GM_LD(void *, (((uint8_t *)t49) + 32));
                gm_f_push_o4ckdr1(t50, &(*(gm_t4gut2c9fj1kt *)((uint8_t *)&F.l6)));
            }
            {
                void * t51 = gm_l0;
                void * t52 = (void *)(((uint8_t *)t51) + 40);
                gm_tm4pc0emhrv1g t53;
                memset((uint8_t *)&t53 + 44, 0, 4);
                gm_t9qkh0rsooc8d t54 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l3) + 0));
                gm_t9qkh0rsooc8d t55 = gm_f_closer_fus0off(t54);
                gm_t3fcbuehh3bs2 t56;
                GM_ST(uint8_t, (uint8_t *)&t56 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t56 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t56 + 4, 0);
                GM_ST(uint8_t, (uint8_t *)&t56 + 9, 0);
                GM_ST(uint16_t, (uint8_t *)&t56 + 10, 0);
                GM_ST(uint32_t, (uint8_t *)&t56 + 12, 0);
                GM_ST(uint64_t, (uint8_t *)&t56 + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&t56 + 24, 0);
                GM_ST(uint8_t, ((uint8_t *)&t56 + 0), ((uint8_t)8ULL));
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t56 + 8), t55);
                GM_ST(gm_t3fcbuehh3bs2, (uint8_t *)&t53 + 0, t56);
                gm_tl874t2atnlh3 t57 = gm_l2;
                GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t53 + 32, t57);
                gm_tftb1odc739t5 t58 = gm_f_push_pnf14h1(t52, &t53);
                gm_tftb1odc739t5 t59 = t58;
                if (((((uint8_t *)&t59))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t59) + 1, gm_srcvmmksi0829pa, 617, 9);
                gm_b0_1 t60 = GM_LD(gm_b0_1, (((uint8_t *)&t59) + 1));
            }
lc8:;
        }
gm_ll14:;
        if (gm_unw.frame == &F.h && gm_unw.label == 0) {
            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb8; }
            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc8; }
        }
        goto gm_lad;
lb8:;
    }
    {
        void * t61 = gm_l0;
        void * t62 = (void *)(((uint8_t *)t61) + 184);
        gm_f_clear_4qp22g2(t62);
    }
    {
        void * t63 = gm_l0;
        gm_tl874t2atnlh3 t64 = gm_l2;
        gm_f_push_newline_ke0tojq(t63, GM_CONV(gm_cseahvg8v7p0vs, t64));
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
__attribute__((aligned(4))) gm_b0_1 gm_f_close_bracket_tbjprpe(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t9qkh0rsooc8d a1, gm_tl874t2atnlh3 a2) {
    struct gm_fr_close_bracket_tbjprpe F;
    void * gm_l0; /* lx */
    gm_t9qkh0rsooc8d gm_l1; /* c */
    gm_tl874t2atnlh3 gm_l2; /* at */
    gm_b16_8 gm_l3; /* items */
    uint64_t gm_l4; /* i */
    uint64_t gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    uint64_t gm_l7; /* k */
    gm_t2eb59dvglnle gm_l8; /* skipped */
    void * gm_l9; /* <tmp> */
    gm_b16_8 gm_l10; /* open */
    void * gm_l12; /* <tmp> */
    uint64_t gm_l13; /* depth */
    uint64_t gm_l14; /* n */
    GM_STACK_CHECK(&F, 981u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        gm_b16_8 t3 = GM_LD(gm_b16_8, ((((uint8_t *)t2) + 120) + 0));
        gm_l3 = t3;
    }
    {
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
        uint64_t r5; bool o6 = __builtin_sub_overflow(t4, ((uint64_t)1ULL), &r5);
        if (o6) gm_trap("integer overflow in `-`", gm_srcvmmksi0829pa, 632, 10);
        uint64_t t7 = r5;
        gm_l4 = t7;
    }
    {
        for (;;) {
            uint64_t t9 = gm_l4;
            uint8_t t10 = (t9 > ((uint64_t)0ULL));
            if (!t10) goto lb8;
            uint64_t t11 = gm_l4;
            uint64_t t12 = (uintptr_t)t11;
            uint64_t t13 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
            if (t12 >= (uintptr_t)(t13)) gm_trap_index(t12, t13, gm_srcvmmksi0829pa, 633, 28);
            gm_t9qkh0rsooc8d t14 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t12 * 16) + 0));
            gm_t9qkh0rsooc8d t15 = gm_f_closer_fus0off(t14);
            gm_t9qkh0rsooc8d t16 = gm_l1;
            gm_t9qkh0rsooc8d t17 = t15;
            gm_t9qkh0rsooc8d t18 = t16;
            uint8_t t19 = (!gm_eqv3tj1pu43g(((uint8_t *)&t17), ((uint8_t *)&t18)));
            if (!t19) goto lb8;
            {
                uint64_t t22 = gm_l4;
                uint64_t r23; bool o24 = __builtin_sub_overflow(t22, ((uint64_t)1ULL), &r23);
                if (o24) gm_trap("integer overflow in `-`", gm_srcvmmksi0829pa, 633, 54);
                uint64_t t25 = r23;
                gm_l4 = t25;
            }
lc8:;
        }
lb8:;
    }
    {
        {
            uint64_t t27 = gm_l4;
            uint64_t r28; bool o29 = __builtin_add_overflow(t27, ((uint64_t)1ULL), &r28);
            if (o29) gm_trap("integer overflow in `+`", gm_srcvmmksi0829pa, 634, 14);
            uint64_t t30 = r28;
            gm_l6 = t30;
        }
        {
            uint64_t t31 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
            gm_l5 = t31;
        }
        {
            for (;;) {
                uint64_t t33 = gm_l6;
                uint64_t t34 = gm_l5;
                uint8_t t35 = (t33 < t34);
                if (!t35) goto lb32;
                {
                    uint64_t t38 = gm_l6;
                    uint64_t t39 = t38;
                    uint64_t t42 = GM_LD(uint64_t, ((uint8_t *)&t39));
                    gm_l7 = t42;
                    {
                        uint64_t t44 = gm_l7;
                        uint64_t t45 = (uintptr_t)t44;
                        uint64_t t46 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                        if (t45 >= (uintptr_t)(t46)) gm_trap_index(t45, t46, gm_srcvmmksi0829pa, 635, 20);
                        gm_t2eb59dvglnle t47 = GM_LD(gm_t2eb59dvglnle, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t45 * 16));
                        gm_l8 = t47;
                    }
                    {
                        {
                            void * t49 = gm_l0;
                            uint8_t *t50 = (uint8_t *)((((uint8_t *)t49) + 160));
                            gm_t9qkh0rsooc8d t51 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l8) + 0));
                            uint64_t t52 = gm_f_slot_4oe07nr(t51);
                            uint64_t t53 = (uintptr_t)t52;
                            if (t53 >= (uintptr_t)(3)) gm_trap_index(t53, 3, gm_srcvmmksi0829pa, 636, 9);
                            void * t54 = (void *)(t50 + t53 * 8);
                            gm_l9 = t54;
                        }
                        {
                            void * t55 = gm_l9;
                            void * t56 = gm_l9;
                            uint64_t t57 = GM_LD(uint64_t, ((uint8_t *)t56));
                            uint64_t r58; bool o59 = __builtin_sub_overflow(t57, ((uint64_t)1ULL), &r58);
                            if (o59) gm_trap("integer overflow in `-`", gm_srcvmmksi0829pa, 636, 9);
                            uint64_t t60 = r58;
                            GM_ST(uint64_t, ((uint8_t *)t55), t60);
                        }
                    }
                    {
                        gm_t9qkh0rsooc8d t61 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l8) + 0));
                        gm_t9qkh0rsooc8d t62 = gm_f_opener_8hqhoc3(t61);
                        gm_b16_8 t63 = gm_f_punct_text_ov4r40u(t62);
                        gm_l10 = t63;
                    }
                    {
                        gm_tl874t2atnlh3 t64 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l8) + 4));
                        gm_b16_8 t65 = gm_l10;
                        gm_tnpifv20irnph t66;
                        GM_ST(gm_b16_8, ((uint8_t *)&t66 + 0), t65);
                        gm_tabb710d9mq2e t67;
                        gm_f_format_t32jdqr((void *)&t67, (void *)NULL, cx, NULL, t66);
                        if (gm_unw.kind) {
                            goto gm_ll36;
                        }
                        gm_tabb710d9mq2e t68 = t67;
                        if (((((uint8_t *)&t68))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t68) + 8, gm_srcvmmksi0829pa, 638, 39);
                        gm_b16_8 t69 = GM_LD(gm_b16_8, (((uint8_t *)&t68) + 8));
                        gm_b16_8 t70 = t69;
                        gm_b16_8 t71 = GM_LD(gm_b16_8, ((uint8_t *)&t70));
                        gm_f_error_s6hn871((void *)&F.l11, (void *)NULL, cx, NULL, t64, t71);
                        if (gm_unw.kind) {
                            goto gm_ll36;
                        }
                    }
                    {
                        void * t72 = (void *)((uint8_t *)&F.l11);
                        gm_tl874t2atnlh3 t73 = gm_l2;
                        gm_t6j6smj7qoctu t74;
                        gm_t9qkh0rsooc8d t75 = gm_l1;
                        gm_b16_8 t76 = gm_f_punct_text_ov4r40u(t75);
                        GM_ST(gm_b16_8, (uint8_t *)&t74 + 0, t76);
                        gm_b16_8 t77 = gm_l10;
                        GM_ST(gm_b16_8, (uint8_t *)&t74 + 16, t77);
                        gm_tabb710d9mq2e t78;
                        gm_f_format_7a5f58b((void *)&t78, (void *)NULL, cx, NULL, &t74);
                        if (gm_unw.kind) {
                            goto gm_ll36;
                        }
                        gm_tabb710d9mq2e t79 = t78;
                        if (((((uint8_t *)&t79))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t79) + 8, gm_srcvmmksi0829pa, 639, 25);
                        gm_b16_8 t80 = GM_LD(gm_b16_8, (((uint8_t *)&t79) + 8));
                        gm_b16_8 t81 = t80;
                        gm_b16_8 t82 = GM_LD(gm_b16_8, ((uint8_t *)&t81));
                        gm_f_add_label_50fj7vv(t72, GM_CONV(gm_cseahvg8v7p0vs, t73), t82);
                    }
                    {
                        void * t83 = gm_l0;
                        void * t84 = GM_LD(void *, (((uint8_t *)t83) + 32));
                        gm_f_push_o4ckdr1(t84, &(*(gm_t4gut2c9fj1kt *)((uint8_t *)&F.l11)));
                    }
                    goto me40;
mn41:;
                    gm_trap("no match arm matched the scrutinee", gm_srcvmmksi0829pa, 634, 5);
me40:;
                }
lc32:;
                {
                    uint64_t t86 = gm_l6;
                    uint64_t r87; bool o88 = __builtin_add_overflow(t86, ((uint64_t)1ULL), &r87);
                    uint64_t t89 = r87;
                    gm_l6 = t89;
                }
            }
gm_ll36:;
            if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb32; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc32; }
            }
            goto gm_lad;
lb32:;
        }
    }
    {
        {
            void * t91 = gm_l0;
            uint8_t *t92 = (uint8_t *)((((uint8_t *)t91) + 160));
            gm_t9qkh0rsooc8d t93 = gm_l1;
            uint64_t t94 = gm_f_closer_slot_f4sb6v0(t93);
            uint64_t t95 = (uintptr_t)t94;
            if (t95 >= (uintptr_t)(3)) gm_trap_index(t95, 3, gm_srcvmmksi0829pa, 642, 5);
            void * t96 = (void *)(t92 + t95 * 8);
            gm_l12 = t96;
        }
        {
            void * t97 = gm_l12;
            void * t98 = gm_l12;
            uint64_t t99 = GM_LD(uint64_t, ((uint8_t *)t98));
            uint64_t r100; bool o101 = __builtin_sub_overflow(t99, ((uint64_t)1ULL), &r100);
            if (o101) gm_trap("integer overflow in `-`", gm_srcvmmksi0829pa, 642, 5);
            uint64_t t102 = r100;
            GM_ST(uint64_t, ((uint8_t *)t97), t102);
        }
    }
    {
        void * t103 = gm_l0;
        void * t104 = (void *)(((uint8_t *)t103) + 120);
        uint64_t t105 = gm_l4;
        gm_f_truncate_h4ov51t(t104, t105);
    }
    {
        uint64_t t106 = gm_l4;
        gm_l13 = t106;
    }
    {
        for (;;) {
            if (!((uint8_t)1)) goto lb107;
            {
                void * t110 = gm_l0;
                uint64_t t111 = GM_LD(uintptr_t, ((((uint8_t *)t110) + 184) + 0) + 8);
                gm_l14 = t111;
            }
            {
                uint8_t t113 = 0;
                uint64_t t114 = gm_l14;
                uint8_t t115 = ((t114 == ((uint64_t)0ULL)));
                if (!t115) {
                    uint64_t t116 = gm_l14;
                    uint64_t r117; bool o118 = __builtin_sub_overflow(t116, ((uint64_t)1ULL), &r117);
                    if (o118) gm_trap("integer overflow in `-`", gm_srcvmmksi0829pa, 647, 47);
                    uint64_t t119 = r117;
                    uint64_t t120 = (uintptr_t)t119;
                    void * t121 = gm_l0;
                    uint64_t t122 = GM_LD(uintptr_t, ((((uint8_t *)t121) + 184) + 0) + 8);
                    if (t120 >= (uintptr_t)(t122)) gm_trap_index(t120, t122, gm_srcvmmksi0829pa, 647, 22);
                    uint64_t t123 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t121) + 184) + 0)) + t120 * 8));
                    uint64_t t124 = gm_l13;
                    uint8_t t125 = (t123 <= t124);
                    t113 = t125;
                } else t113 = 1;
                if (!t113) goto ie112;
                {
                    goto lb107;
                }
                goto ix112;
ie112:;
ix112:;
            }
            {
                {
                    void * t128 = gm_l0;
                    void * t129 = (void *)(((uint8_t *)t128) + 184);
                    gm_tf5srh4mp4b3i t130 = gm_f_pop_j4rpvm5(t129);
                }
            }
lc107:;
        }
lb107:;
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
inline __attribute__((aligned(4))) gm_t9qkh0rsooc8d gm_f_closer_fus0off(gm_t9qkh0rsooc8d a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_closer_fus0off F;
    gm_t9qkh0rsooc8d gm_l0; /* k */
    GM_STACK_CHECK_SHALLOW(&F, 12u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_t9qkh0rsooc8d t1;
    uint8_t t7 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    switch (t7) { case ((uint8_t)0ULL): goto ma3; case ((uint8_t)1ULL): goto ma4; case ((uint8_t)2ULL): goto ma5; case ((uint8_t)3ULL): goto ma5; default: goto ma6; }
ma3:;
    uint8_t t9 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t9 != ((uint8_t)0ULL)) goto mn8;
    gm_t9qkh0rsooc8d t10;
    GM_ST(uint8_t, ((uint8_t *)&t10 + 0), ((uint8_t)62ULL));
    t1 = t10;
    goto me2;
mn8:;
ma4:;
    uint8_t t12 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t12 != ((uint8_t)1ULL)) goto mn11;
    gm_t9qkh0rsooc8d t13;
    GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)64ULL));
    t1 = t13;
    goto me2;
mn11:;
ma5:;
    uint8_t t17 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t17 != ((uint8_t)2ULL)) goto pa16;
    goto po15;
pa16:;
    uint8_t t19 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t19 != ((uint8_t)3ULL)) goto pa18;
    goto po15;
pa18:;
    goto mn14;
po15:;
    gm_t9qkh0rsooc8d t20;
    GM_ST(uint8_t, ((uint8_t *)&t20 + 0), ((uint8_t)66ULL));
    t1 = t20;
    goto me2;
mn14:;
ma6:;
    gm_trap("no match arm matched the scrutinee", gm_srcvmmksi0829pa, 53, 42);
me2:;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint64_t gm_f_closer_slot_f4sb6v0(gm_t9qkh0rsooc8d a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_closer_slot_f4sb6v0 F;
    gm_t9qkh0rsooc8d gm_l0; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 28u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint64_t t1;
    uint8_t t7 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    switch (t7) { case ((uint8_t)0ULL): goto ma5; case ((uint8_t)1ULL): goto ma5; case ((uint8_t)2ULL): goto ma5; case ((uint8_t)3ULL): goto ma5; case ((uint8_t)4ULL): goto ma5; case ((uint8_t)5ULL): goto ma5; case ((uint8_t)6ULL): goto ma5; case ((uint8_t)7ULL): goto ma5; case ((uint8_t)8ULL): goto ma5; case ((uint8_t)9ULL): goto ma5; case ((uint8_t)10ULL): goto ma5; case ((uint8_t)11ULL): goto ma5; case ((uint8_t)12ULL): goto ma5; case ((uint8_t)13ULL): goto ma5; case ((uint8_t)14ULL): goto ma5; case ((uint8_t)15ULL): goto ma5; case ((uint8_t)16ULL): goto ma5; case ((uint8_t)17ULL): goto ma5; case ((uint8_t)18ULL): goto ma5; case ((uint8_t)19ULL): goto ma5; case ((uint8_t)20ULL): goto ma5; case ((uint8_t)21ULL): goto ma5; case ((uint8_t)22ULL): goto ma5; case ((uint8_t)23ULL): goto ma5; case ((uint8_t)24ULL): goto ma5; case ((uint8_t)25ULL): goto ma5; case ((uint8_t)26ULL): goto ma5; case ((uint8_t)27ULL): goto ma5; case ((uint8_t)28ULL): goto ma5; case ((uint8_t)29ULL): goto ma5; case ((uint8_t)30ULL): goto ma5; case ((uint8_t)31ULL): goto ma5; case ((uint8_t)32ULL): goto ma5; case ((uint8_t)33ULL): goto ma5; case ((uint8_t)34ULL): goto ma5; case ((uint8_t)35ULL): goto ma5; case ((uint8_t)36ULL): goto ma5; case ((uint8_t)37ULL): goto ma5; case ((uint8_t)38ULL): goto ma5; case ((uint8_t)39ULL): goto ma5; case ((uint8_t)40ULL): goto ma5; case ((uint8_t)41ULL): goto ma5; case ((uint8_t)42ULL): goto ma5; case ((uint8_t)43ULL): goto ma5; case ((uint8_t)44ULL): goto ma5; case ((uint8_t)45ULL): goto ma5; case ((uint8_t)46ULL): goto ma5; case ((uint8_t)47ULL): goto ma5; case ((uint8_t)48ULL): goto ma5; case ((uint8_t)49ULL): goto ma5; case ((uint8_t)50ULL): goto ma5; case ((uint8_t)51ULL): goto ma5; case ((uint8_t)52ULL): goto ma5; case ((uint8_t)53ULL): goto ma5; case ((uint8_t)54ULL): goto ma5; case ((uint8_t)55ULL): goto ma5; case ((uint8_t)56ULL): goto ma5; case ((uint8_t)57ULL): goto ma5; case ((uint8_t)58ULL): goto ma5; case ((uint8_t)59ULL): goto ma5; case ((uint8_t)60ULL): goto ma5; case ((uint8_t)61ULL): goto ma5; case ((uint8_t)62ULL): goto ma3; case ((uint8_t)63ULL): goto ma5; case ((uint8_t)64ULL): goto ma4; case ((uint8_t)65ULL): goto ma5; case ((uint8_t)66ULL): goto ma5; case ((uint8_t)67ULL): goto ma5; case ((uint8_t)68ULL): goto ma5; default: goto ma5; }
ma3:;
    uint8_t t9 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t9 != ((uint8_t)62ULL)) goto mn8;
    t1 = ((uint64_t)0ULL);
    goto me2;
mn8:;
ma4:;
    uint8_t t11 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t11 != ((uint8_t)64ULL)) goto mn10;
    t1 = ((uint64_t)1ULL);
    goto me2;
mn10:;
ma5:;
    t1 = ((uint64_t)2ULL);
    goto me2;
mn12:;
ma6:;
    gm_trap("no match arm matched the scrutinee", gm_srcvmmksi0829pa, 67, 41);
me2:;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_taari1s0m51a7 gm_f_closing_na3a1ur(const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_closing_na3a1ur F;
    gm_t3fcbuehh3bs2 gm_l0; /* k */
    gm_t9qkh0rsooc8d gm_l1; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 123u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_t3fcbuehh3bs2 t1 = GM_LD(gm_t3fcbuehh3bs2, ((uint8_t *)a0));
    gm_t3fcbuehh3bs2 t2 = t1;
    gm_taari1s0m51a7 t3;
    uint8_t t6 = GM_LD(uint8_t, (((uint8_t *)&t2) + 0));
    if (t6 != ((uint8_t)8ULL)) goto mn5;
    gm_t9qkh0rsooc8d t7 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&t2) + 8));
    gm_l1 = t7;
    gm_t9qkh0rsooc8d t8 = gm_l1;
    gm_t9qkh0rsooc8d t9;
    GM_ST(uint8_t, ((uint8_t *)&t9 + 0), ((uint8_t)62ULL));
    gm_t9qkh0rsooc8d t10;
    GM_ST(uint8_t, ((uint8_t *)&t10 + 0), ((uint8_t)64ULL));
    gm_t9qkh0rsooc8d t11;
    GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)66ULL));
    gm_tk22flr9mh3ql t12;
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t12 + 0), t9);
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t12 + 1), t10);
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t12 + 2), t11);
    gm_tk22flr9mh3ql t13 = t12;
    uint8_t t14 = 0;
    gm_t9qkh0rsooc8d t15 = t8;
    if (gm_eqv3tj1pu43g(((uint8_t *)&t13) + 0, ((uint8_t *)&t15))) t14 = 1;
    if (gm_eqv3tj1pu43g(((uint8_t *)&t13) + 1, ((uint8_t *)&t15))) t14 = 1;
    if (gm_eqv3tj1pu43g(((uint8_t *)&t13) + 2, ((uint8_t *)&t15))) t14 = 1;
    uint8_t t16 = (uint8_t)(t14);
    if (!t16) goto mn5;
    gm_t9qkh0rsooc8d t17 = gm_l1;
    gm_taari1s0m51a7 t18;
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t18), t17);
    ((uint8_t *)&t18)[1] = 1;
    t3 = t18;
    goto me4;
mn5:;
    gm_taari1s0m51a7 t20;
    memset(&t20, 0, sizeof t20);
    t3 = t20;
    goto me4;
mn19:;
    gm_trap("no match arm matched the scrutinee", gm_srcvmmksi0829pa, 74, 42);
me4:;
    F.ret = t3;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_taari1s0m51a7 gm_f_closing_na3a1ur_c(gm_t3fcbuehh3bs2 a0) { return gm_f_closing_na3a1ur(&a0); }
extern __attribute__((aligned(4))) gm_cskjv6t053tkje gm_f_current_char_ctktsl2(void * a0);
__attribute__((aligned(4))) uint8_t gm_f_declaration_starts_at_mp730g1(void * a0, uint64_t a1, const void * a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_declaration_starts_at_mp730g1 F;
    void * gm_l0; /* lx */
    uint64_t gm_l1; /* lo */
    gm_t3fcbuehh3bs2 gm_l2; /* kind */
    uint64_t gm_l3; /* i */
    gm_b16_8 gm_l4; /* word */
    gm_b16_8 gm_l5; /* after */
    gm_t9qkh0rsooc8d gm_l6; /* k */
    GM_STACK_CHECK(&F, 692u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint64_t t3 = gm_l1;
        uint8_t t4 = (!(t3 == ((uint64_t)0ULL)));
        if (!t4) goto ie2;
        uint64_t t5 = gm_l1;
        uint64_t r6; bool o7 = __builtin_sub_overflow(t5, ((uint64_t)1ULL), &r6);
        if (o7) gm_trap("integer overflow in `-`", gm_srcvmmksi0829pa, 590, 31);
        uint64_t t8 = r6;
        uint64_t t9 = (uintptr_t)t8;
        void * t10 = gm_l0;
        uint64_t t11 = GM_LD(uintptr_t, (((uint8_t *)t10) + 8) + 8);
        if (t9 >= (uintptr_t)(t11)) gm_trap_index(t9, t11, gm_srcvmmksi0829pa, 590, 24);
        uint8_t t12 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)t10) + 8)) + t9 * 1));
        uint8_t t13 = (!(t12 == ((uint8_t)10ULL)));
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
        uint64_t t16 = gm_l1;
        gm_l3 = t16;
    }
    {
        for (;;) {
            uint64_t t18 = gm_l3;
            void * t19 = gm_l0;
            uint64_t t20 = GM_LD(uintptr_t, (((uint8_t *)t19) + 8) + 8);
            uint8_t t21 = (t18 < t20);
            if (!t21) goto lb17;
            uint64_t t22 = gm_l3;
            uint64_t t23 = (uintptr_t)t22;
            void * t24 = gm_l0;
            uint64_t t25 = GM_LD(uintptr_t, (((uint8_t *)t24) + 8) + 8);
            if (t23 >= (uintptr_t)(t25)) gm_trap_index(t23, t25, gm_srcvmmksi0829pa, 592, 51);
            uint8_t t26 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)t24) + 8)) + t23 * 1));
            uint8_t t27 = gm_f_is_ident_cont_317fpdb(t26);
            if (!t27) goto lb17;
            {
                uint64_t t30 = gm_l3;
                uint64_t r31; bool o32 = __builtin_add_overflow(t30, ((uint64_t)1ULL), &r31);
                if (o32) gm_trap("integer overflow in `+`", gm_srcvmmksi0829pa, 592, 64);
                uint64_t t33 = r31;
                gm_l3 = t33;
            }
lc17:;
        }
lb17:;
    }
    {
        void * t34 = gm_l0;
        uint64_t t35 = GM_LD(uintptr_t, (((uint8_t *)t34) + 8) + 8);
        void * t36 = ((uint8_t *)GM_LD(void *, (((uint8_t *)t34) + 8)));
        uint64_t t37 = gm_l1;
        uint64_t t38 = (uintptr_t)t37;
        uint64_t t39 = gm_l3;
        uint64_t t40 = (uintptr_t)t39;
        if (t38 > t40 || t40 > (uintptr_t)t35) gm_trap_slice(t38, t40, t35, gm_srcvmmksi0829pa, 593, 13);
        gm_b16_8 t41;
        GM_ST(void *, (uint8_t *)&t41, (void *)(((uint8_t *)t36 + t38 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t41 + 8, (uintptr_t)(t40 - t38));
        gm_l4 = t41;
    }
    {
        for (;;) {
            uint64_t t43 = gm_l3;
            void * t44 = gm_l0;
            uint64_t t45 = GM_LD(uintptr_t, (((uint8_t *)t44) + 8) + 8);
            uint8_t t46 = (t43 < t45);
            if (!t46) goto lb42;
            uint64_t t47 = gm_l3;
            uint64_t t48 = (uintptr_t)t47;
            void * t49 = gm_l0;
            uint64_t t50 = GM_LD(uintptr_t, (((uint8_t *)t49) + 8) + 8);
            if (t48 >= (uintptr_t)(t50)) gm_trap_index(t48, t50, gm_srcvmmksi0829pa, 594, 30);
            uint8_t t51 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)t49) + 8)) + t48 * 1));
            gm_tqs5p4n2tq19v t52;
            GM_ST(uint8_t, ((uint8_t *)&t52 + 0), ((uint8_t)32ULL));
            GM_ST(uint8_t, ((uint8_t *)&t52 + 1), ((uint8_t)9ULL));
            gm_tqs5p4n2tq19v t53 = t52;
            uint8_t t54 = 0;
            uint8_t t55 = t51;
            if (gm_eqjmccqdb90f(((uint8_t *)&t53) + 0, ((uint8_t *)&t55))) t54 = 1;
            if (gm_eqjmccqdb90f(((uint8_t *)&t53) + 1, ((uint8_t *)&t55))) t54 = 1;
            uint8_t t56 = (uint8_t)(t54);
            if (!t56) goto lb42;
            {
                uint64_t t59 = gm_l3;
                uint64_t r60; bool o61 = __builtin_add_overflow(t59, ((uint64_t)1ULL), &r60);
                if (o61) gm_trap("integer overflow in `+`", gm_srcvmmksi0829pa, 594, 57);
                uint64_t t62 = r60;
                gm_l3 = t62;
            }
lc42:;
        }
lb42:;
    }
    {
        void * t63 = gm_l0;
        uint64_t t64 = GM_LD(uintptr_t, (((uint8_t *)t63) + 8) + 8);
        void * t65 = ((uint8_t *)GM_LD(void *, (((uint8_t *)t63) + 8)));
        uint64_t t66 = gm_l3;
        uint64_t t67 = (uintptr_t)t66;
        if (t67 > (uintptr_t)t64 || (uintptr_t)t64 > (uintptr_t)t64) gm_trap_slice(t67, (uintptr_t)t64, t64, gm_srcvmmksi0829pa, 595, 14);
        gm_b16_8 t68;
        GM_ST(void *, (uint8_t *)&t68, (void *)(((uint8_t *)t65 + t67 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t68 + 8, (uintptr_t)((uintptr_t)t64 - t67));
        gm_l5 = t68;
    }
    {
        uint8_t t69;
        uint8_t t76 = GM_LD(uint8_t, (((uint8_t *)a2) + 0));
        switch (t76) { case ((uint8_t)0ULL): goto ma73; case ((uint8_t)1ULL): goto ma74; case ((uint8_t)2ULL): goto ma72; case ((uint8_t)3ULL): goto ma74; case ((uint8_t)4ULL): goto ma74; case ((uint8_t)5ULL): goto ma74; case ((uint8_t)6ULL): goto ma74; case ((uint8_t)7ULL): goto ma71; case ((uint8_t)8ULL): goto ma74; case ((uint8_t)9ULL): goto ma74; case ((uint8_t)10ULL): goto ma74; default: goto ma74; }
ma71:;
        uint8_t t78 = GM_LD(uint8_t, (((uint8_t *)a2) + 0));
        if (t78 != ((uint8_t)7ULL)) goto mn77;
        t69 = ((uint8_t)1);
        goto me70;
mn77:;
ma72:;
        uint8_t t80 = GM_LD(uint8_t, (((uint8_t *)a2) + 0));
        if (t80 != ((uint8_t)2ULL)) goto mn79;
        gm_t9qkh0rsooc8d t81 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)a2) + 8));
        gm_l6 = t81;
        uint8_t t82 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l6) + 0) == ((uint8_t)21ULL)));
        t69 = t82;
        goto me70;
mn79:;
ma73:;
        uint8_t t84 = GM_LD(uint8_t, (((uint8_t *)a2) + 0));
        if (t84 != ((uint8_t)0ULL)) goto mn83;
        uint8_t t86 = 0;
        uint8_t t87 = 0;
        gm_b16_8 t88 = gm_l5;
        gm_b16_8 t89;
        GM_ST(void *, (uint8_t *)&t89, (void *)(gm_ss67f0gmfen1m));
        GM_ST(uintptr_t, (uint8_t *)&t89 + 8, (uintptr_t)(2));
        uint8_t t90 = gm_f_starts_with_6vvt01e(t88, t89);
        if (!t90) {
            uint8_t t91 = 0;
            gm_b16_8 t92 = gm_l4;
            gm_t6j6smj7qoctu t93;
            gm_b16_8 t94;
            GM_ST(void *, (uint8_t *)&t94, (void *)(gm_s9r64t1166q1f));
            GM_ST(uintptr_t, (uint8_t *)&t94 + 8, (uintptr_t)(4));
            GM_ST(gm_b16_8, (uint8_t *)&t93 + 0, t94);
            gm_b16_8 t95;
            GM_ST(void *, (uint8_t *)&t95, (void *)(gm_srfagufvviad7));
            GM_ST(uintptr_t, (uint8_t *)&t95 + 8, (uintptr_t)(5));
            GM_ST(gm_b16_8, (uint8_t *)&t93 + 16, t95);
            gm_t6j6smj7qoctu t96 = t93;
            uint8_t t97 = 0;
            gm_b16_8 t98 = t92;
            if (gm_eq0iqiubfm7e(((uint8_t *)&t96) + 0, ((uint8_t *)&t98))) t97 = 1;
            if (gm_eq0iqiubfm7e(((uint8_t *)&t96) + 16, ((uint8_t *)&t98))) t97 = 1;
            uint8_t t99 = (uint8_t)(t97);
            if (t99) {
                gm_b16_8 t100 = gm_l5;
                gm_b16_8 t101;
                GM_ST(void *, (uint8_t *)&t101, (void *)(gm_sb54ui2amic32));
                GM_ST(uintptr_t, (uint8_t *)&t101 + 8, (uintptr_t)(1));
                uint8_t t102 = gm_f_starts_with_6vvt01e(t100, t101);
                t91 = t102;
            } else t91 = 0;
            t87 = t91;
        } else t87 = 1;
        if (!t87) {
            uint8_t t103 = 0;
            uint8_t t104 = 0;
            uint8_t t105 = (((GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8) == 7) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l4)), gm_sic5vsema0r36, 7) == 0));
            if (t105) {
                uint64_t t106 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                uint8_t t107 = (t106 > ((uint64_t)0ULL));
                t104 = t107;
            } else t104 = 0;
            if (t104) {
                uint8_t t108 = 0;
                uint64_t t109 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t110 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                if (t109 >= (uintptr_t)(t110)) gm_trap_index(t109, t110, gm_srcvmmksi0829pa, 601, 62);
                uint8_t t111 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l5)) + t109 * 1));
                uint8_t t112 = ((t111 == ((uint8_t)40ULL)));
                if (!t112) {
                    uint64_t t113 = (uintptr_t)((uint64_t)0ULL);
                    uint64_t t114 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                    if (t113 >= (uintptr_t)(t114)) gm_trap_index(t113, t114, gm_srcvmmksi0829pa, 601, 103);
                    uint8_t t115 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l5)) + t113 * 1));
                    uint8_t t116 = gm_f_is_ident_start_rjefpfm(t115);
                    t108 = t116;
                } else t108 = 1;
                t103 = t108;
            } else t103 = 0;
            t86 = t103;
        } else t86 = 1;
        uint8_t t117 = t86;
        t69 = t117;
        goto me70;
mn83:;
ma74:;
        t69 = ((uint8_t)0);
        goto me70;
mn118:;
ma75:;
        gm_trap("no match arm matched the scrutinee", gm_srcvmmksi0829pa, 596, 12);
me70:;
        uint8_t t119 = 0;
        F.ret = t69;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) uint8_t gm_f_declaration_starts_at_mp730g1_c(void * a0, uint64_t a1, gm_t3fcbuehh3bs2 a2) { return gm_f_declaration_starts_at_mp730g1(a0, a1, &a2); }
__attribute__((aligned(4))) gm_b0_1 gm_f_emit_ekvv703(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, uint64_t a2, uint64_t a3) {
    struct gm_fr_emit_ekvv703 F;
    void * gm_l0; /* lx */
    gm_t3fcbuehh3bs2 gm_l1; /* kind */
    uint64_t gm_l2; /* lo */
    uint64_t gm_l3; /* hi */
    gm_tl874t2atnlh3 gm_l4; /* here */
    gm_t9qkh0rsooc8d gm_l5; /* c */
    uint64_t gm_l6; /* depth */
    uint64_t gm_l7; /* n */
    uint8_t gm_l8; /* is_match_body */
    gm_t9qkh0rsooc8d gm_l9; /* p */
    GM_STACK_CHECK(&F, 816u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&gm_l1, a1, sizeof gm_l1);
    gm_l2 = a2;
    gm_l3 = a3;
    {
        void * t2 = gm_l0;
        uint64_t t3 = gm_l2;
        uint64_t t4 = gm_l3;
        gm_tl874t2atnlh3 t5 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_tg7cev6(t2, t3, t4));
        gm_l4 = t5;
    }
    {
        gm_taari1s0m51a7 t7 = gm_f_closing_na3a1ur(&(*(gm_t3fcbuehh3bs2 *)((uint8_t *)&gm_l1)));
        gm_taari1s0m51a7 t8 = t7;
        if (!((((uint8_t *)&t8))[1] != 0)) goto ie6;
        gm_t9qkh0rsooc8d t9 = GM_LD(gm_t9qkh0rsooc8d, ((uint8_t *)&t8));
        gm_l5 = t9;
        {
            void * t12 = gm_l0;
            uint8_t *t13 = (uint8_t *)((((uint8_t *)t12) + 160));
            gm_t9qkh0rsooc8d t14 = gm_l5;
            uint64_t t15 = gm_f_closer_slot_f4sb6v0(t14);
            uint64_t t16 = (uintptr_t)t15;
            if (t16 >= (uintptr_t)(3)) gm_trap_index(t16, 3, gm_srcvmmksi0829pa, 561, 12);
            uint64_t t17 = GM_LD(uint64_t, (t13 + t16 * 8));
            uint8_t t18 = ((t17 == ((uint64_t)0ULL)));
            if (!t18) goto ie11;
            {
                void * t20 = gm_l0;
                uint64_t t21 = gm_l2;
                uint64_t t22 = gm_l3;
                gm_t9qkh0rsooc8d t23 = gm_l5;
                gm_b16_8 t24 = gm_f_punct_text_ov4r40u(t23);
                gm_tnpifv20irnph t25;
                GM_ST(gm_b16_8, ((uint8_t *)&t25 + 0), t24);
                gm_tabb710d9mq2e t26;
                gm_f_format_daod35i((void *)&t26, (void *)NULL, cx, NULL, t25);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t27 = t26;
                if (((((uint8_t *)&t27))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t27) + 8, gm_srcvmmksi0829pa, 562, 30);
                gm_b16_8 t28 = GM_LD(gm_b16_8, (((uint8_t *)&t27) + 8));
                gm_b16_8 t29 = t28;
                gm_b16_8 t30 = GM_LD(gm_b16_8, ((uint8_t *)&t29));
                gm_b0_1 t31 = gm_f_error_p5r0d56((void *)NULL, cx, NULL, t20, t21, t22, t30);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                gm_b0_1 t32;
                uint8_t t33 = 0;
                F.ret = t32;
                goto gm_fret;
            }
            goto ix11;
ie11:;
ix11:;
        }
        goto ix6;
ie6:;
ix6:;
    }
    {
        void * t35 = gm_l0;
        uint8_t t36 = GM_LD(uint8_t, (((uint8_t *)t35) + 280));
        if (!t36) goto ie34;
        void * t37 = gm_l0;
        uint64_t t38 = GM_LD(uintptr_t, ((((uint8_t *)t37) + 120) + 0) + 8);
        uint8_t t39 = (t38 > ((uint64_t)0ULL));
        if (!t39) goto ie34;
        void * t40 = gm_l0;
        uint64_t t41 = gm_l2;
        uint8_t t42 = gm_f_declaration_starts_at_mp730g1(t40, t41, &(*(gm_t3fcbuehh3bs2 *)((uint8_t *)&gm_l1)));
        if (!t42) goto ie34;
        {
            void * t44 = gm_l0;
            uint64_t t45 = gm_l2;
            gm_b0_1 t46 = gm_f_close_all_brackets_bql4j88((void *)NULL, cx, NULL, t44, t45);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix34;
ie34:;
ix34:;
    }
    {
        void * t47 = gm_l0;
        uint64_t t48 = gm_l3;
        gm_f_flush_newline_9023ml8(t47, &(*(gm_t3fcbuehh3bs2 *)((uint8_t *)&gm_l1)), t48);
    }
    {
        gm_t3fcbuehh3bs2 t49 = gm_l1;
        gm_t3fcbuehh3bs2 t50 = t49;
        uint8_t t59 = GM_LD(uint8_t, (((uint8_t *)&t50) + 0));
        switch (t59) { case ((uint8_t)0ULL): goto ma57; case ((uint8_t)1ULL): goto ma57; case ((uint8_t)2ULL): goto ma56; case ((uint8_t)3ULL): goto ma57; case ((uint8_t)4ULL): goto ma57; case ((uint8_t)5ULL): goto ma57; case ((uint8_t)6ULL): goto ma57; case ((uint8_t)7ULL): goto ma57; case ((uint8_t)8ULL): goto ma52; case ((uint8_t)9ULL): goto ma57; case ((uint8_t)10ULL): goto ma57; default: goto ma57; }
ma52:;
        uint8_t t61 = GM_LD(uint8_t, (((uint8_t *)&t50) + 0));
        if (t61 != ((uint8_t)8ULL)) goto mn60;
        uint8_t t62 = GM_LD(uint8_t, ((((uint8_t *)&t50) + 8) + 0));
        if (t62 != ((uint8_t)61ULL)) goto mn60;
        void * t63 = gm_l0;
        gm_t9qkh0rsooc8d t64;
        GM_ST(uint8_t, ((uint8_t *)&t64 + 0), ((uint8_t)0ULL));
        gm_tl874t2atnlh3 t65 = gm_l4;
        gm_f_open_bracket_3kcj4k8(t63, t64, GM_CONV(gm_cseahvg8v7p0vs, t65));
        goto me51;
mn60:;
ma53:;
        uint8_t t67 = GM_LD(uint8_t, (((uint8_t *)&t50) + 0));
        if (t67 != ((uint8_t)8ULL)) goto mn66;
        uint8_t t68 = GM_LD(uint8_t, ((((uint8_t *)&t50) + 8) + 0));
        if (t68 != ((uint8_t)63ULL)) goto mn66;
        void * t69 = gm_l0;
        gm_t9qkh0rsooc8d t70;
        GM_ST(uint8_t, ((uint8_t *)&t70 + 0), ((uint8_t)1ULL));
        gm_tl874t2atnlh3 t71 = gm_l4;
        gm_f_open_bracket_3kcj4k8(t69, t70, GM_CONV(gm_cseahvg8v7p0vs, t71));
        goto me51;
mn66:;
ma54:;
        uint8_t t73 = GM_LD(uint8_t, (((uint8_t *)&t50) + 0));
        if (t73 != ((uint8_t)8ULL)) goto mn72;
        uint8_t t74 = GM_LD(uint8_t, ((((uint8_t *)&t50) + 8) + 0));
        if (t74 != ((uint8_t)65ULL)) goto mn72;
        {
            void * t76 = gm_l0;
            uint64_t t77 = GM_LD(uintptr_t, ((((uint8_t *)t76) + 120) + 0) + 8);
            gm_l6 = t77;
        }
        {
            void * t78 = gm_l0;
            uint64_t t79 = GM_LD(uintptr_t, ((((uint8_t *)t78) + 184) + 0) + 8);
            gm_l7 = t79;
        }
        {
            uint8_t t80 = 0;
            uint64_t t81 = gm_l7;
            uint8_t t82 = (t81 > ((uint64_t)0ULL));
            if (t82) {
                uint64_t t83 = gm_l7;
                uint64_t r84; bool o85 = __builtin_sub_overflow(t83, ((uint64_t)1ULL), &r84);
                if (o85) gm_trap("integer overflow in `-`", gm_srcvmmksi0829pa, 576, 65);
                uint64_t t86 = r84;
                uint64_t t87 = (uintptr_t)t86;
                void * t88 = gm_l0;
                uint64_t t89 = GM_LD(uintptr_t, ((((uint8_t *)t88) + 184) + 0) + 8);
                if (t87 >= (uintptr_t)(t89)) gm_trap_index(t87, t89, gm_srcvmmksi0829pa, 576, 40);
                uint64_t t90 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t88) + 184) + 0)) + t87 * 8));
                uint64_t t91 = gm_l6;
                uint8_t t92 = ((t90 == t91));
                t80 = t92;
            } else t80 = 0;
            gm_l8 = t80;
        }
        {
            uint8_t t94 = gm_l8;
            if (!t94) goto ie93;
            {
                {
                    void * t97 = gm_l0;
                    void * t98 = (void *)(((uint8_t *)t97) + 184);
                    gm_tf5srh4mp4b3i t99 = gm_f_pop_j4rpvm5(t98);
                }
            }
            goto ix93;
ie93:;
ix93:;
        }
        {
            void * t100 = gm_l0;
            gm_t9qkh0rsooc8d t101;
            uint8_t t103 = gm_l8;
            if (!t103) goto ie102;
            gm_t9qkh0rsooc8d t105;
            GM_ST(uint8_t, ((uint8_t *)&t105 + 0), ((uint8_t)3ULL));
            gm_t9qkh0rsooc8d t106 = t105;
            t101 = t106;
            goto ix102;
ie102:;
            gm_t9qkh0rsooc8d t108;
            GM_ST(uint8_t, ((uint8_t *)&t108 + 0), ((uint8_t)2ULL));
            gm_t9qkh0rsooc8d t109 = t108;
            t101 = t109;
ix102:;
            gm_tl874t2atnlh3 t110 = gm_l4;
            gm_f_open_bracket_3kcj4k8(t100, t101, GM_CONV(gm_cseahvg8v7p0vs, t110));
        }
        goto me51;
mn72:;
ma55:;
        uint8_t t112 = GM_LD(uint8_t, (((uint8_t *)&t50) + 0));
        if (t112 != ((uint8_t)8ULL)) goto mn111;
        gm_t9qkh0rsooc8d t113 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&t50) + 8));
        gm_l9 = t113;
        gm_t9qkh0rsooc8d t114 = gm_l9;
        gm_t9qkh0rsooc8d t115;
        GM_ST(uint8_t, ((uint8_t *)&t115 + 0), ((uint8_t)62ULL));
        gm_t9qkh0rsooc8d t116;
        GM_ST(uint8_t, ((uint8_t *)&t116 + 0), ((uint8_t)64ULL));
        gm_t9qkh0rsooc8d t117;
        GM_ST(uint8_t, ((uint8_t *)&t117 + 0), ((uint8_t)66ULL));
        gm_tk22flr9mh3ql t118;
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t118 + 0), t115);
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t118 + 1), t116);
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t118 + 2), t117);
        gm_tk22flr9mh3ql t119 = t118;
        uint8_t t120 = 0;
        gm_t9qkh0rsooc8d t121 = t114;
        if (gm_eqv3tj1pu43g(((uint8_t *)&t119) + 0, ((uint8_t *)&t121))) t120 = 1;
        if (gm_eqv3tj1pu43g(((uint8_t *)&t119) + 1, ((uint8_t *)&t121))) t120 = 1;
        if (gm_eqv3tj1pu43g(((uint8_t *)&t119) + 2, ((uint8_t *)&t121))) t120 = 1;
        uint8_t t122 = (uint8_t)(t120);
        if (!t122) goto mn111;
        void * t123 = gm_l0;
        gm_t9qkh0rsooc8d t124 = gm_l9;
        gm_tl874t2atnlh3 t125 = gm_l4;
        gm_b0_1 t126 = gm_f_close_bracket_tbjprpe((void *)NULL, cx, NULL, t123, t124, t125);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me51;
mn111:;
ma56:;
        uint8_t t128 = GM_LD(uint8_t, (((uint8_t *)&t50) + 0));
        if (t128 != ((uint8_t)2ULL)) goto mn127;
        uint8_t t129 = GM_LD(uint8_t, ((((uint8_t *)&t50) + 8) + 0));
        if (t129 != ((uint8_t)17ULL)) goto mn127;
        void * t130 = gm_l0;
        void * t131 = (void *)(((uint8_t *)t130) + 184);
        void * t132 = gm_l0;
        uint64_t t133 = GM_LD(uintptr_t, ((((uint8_t *)t132) + 120) + 0) + 8);
        gm_tftb1odc739t5 t134 = gm_f_push_ucgmlo2(t131, t133);
        gm_tftb1odc739t5 t135 = t134;
        if (((((uint8_t *)&t135))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t135) + 1, gm_srcvmmksi0829pa, 581, 29);
        gm_b0_1 t136 = GM_LD(gm_b0_1, (((uint8_t *)&t135) + 1));
        goto me51;
mn127:;
ma57:;
        goto me51;
mn137:;
ma58:;
        gm_trap("no match arm matched the scrutinee", gm_srcvmmksi0829pa, 570, 5);
me51:;
    }
    {
        void * t139 = gm_l0;
        void * t140 = (void *)(((uint8_t *)t139) + 40);
        gm_tm4pc0emhrv1g t141;
        memset((uint8_t *)&t141 + 44, 0, 4);
        memcpy((uint8_t *)&t141 + 0, ((uint8_t *)&gm_l1), 32);
        gm_tl874t2atnlh3 t142 = gm_l4;
        GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t141 + 32, t142);
        gm_tftb1odc739t5 t143 = gm_f_push_pnf14h1(t140, &t141);
        gm_tftb1odc739t5 t144 = t143;
        if (((((uint8_t *)&t144))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t144) + 1, gm_srcvmmksi0829pa, 584, 5);
        gm_b0_1 t145 = GM_LD(gm_b0_1, (((uint8_t *)&t144) + 1));
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
extern __attribute__((aligned(4))) gm_b0_1 gm_f_emit_semicolon_7da5b93(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_error_p5r0d56(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, uint64_t a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) gm_t6ofeqo2h242p gm_f_flavors_qsu4oq0(uint8_t a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_flavors_qsu4oq0 F;
    uint8_t gm_l0; /* c */
    GM_STACK_CHECK_SHALLOW(&F, 68u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t t1 = gm_l0;
    uint8_t t2 = t1;
    gm_t6ofeqo2h242p t3;
    uint8_t t6 = GM_LD(uint8_t, ((uint8_t *)&t2));
    if (!(t6 == ((uint8_t)43ULL))) goto mn5;
    gm_t9qkh0rsooc8d t7;
    GM_ST(uint8_t, ((uint8_t *)&t7 + 0), ((uint8_t)0ULL));
    gm_t9qkh0rsooc8d t8;
    GM_ST(uint8_t, ((uint8_t *)&t8 + 0), ((uint8_t)23ULL));
    gm_t9qkh0rsooc8d t9;
    GM_ST(uint8_t, ((uint8_t *)&t9 + 0), ((uint8_t)10ULL));
    gm_t9qkh0rsooc8d t10;
    GM_ST(uint8_t, ((uint8_t *)&t10 + 0), ((uint8_t)14ULL));
    gm_t9qkh0rsooc8d t11;
    GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)18ULL));
    gm_t9qkh0rsooc8d t12;
    GM_ST(uint8_t, ((uint8_t *)&t12 + 0), ((uint8_t)33ULL));
    gm_t9qkh0rsooc8d t13;
    GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)37ULL));
    gm_t6ofeqo2h242p t14;
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 0), t7);
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 1), t8);
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 2), t9);
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 3), t10);
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 4), t11);
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 5), t12);
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 6), t13);
    t3 = t14;
    goto me4;
mn5:;
    uint8_t t16 = GM_LD(uint8_t, ((uint8_t *)&t2));
    if (!(t16 == ((uint8_t)45ULL))) goto mn15;
    gm_t9qkh0rsooc8d t17;
    GM_ST(uint8_t, ((uint8_t *)&t17 + 0), ((uint8_t)1ULL));
    gm_t9qkh0rsooc8d t18;
    GM_ST(uint8_t, ((uint8_t *)&t18 + 0), ((uint8_t)24ULL));
    gm_t9qkh0rsooc8d t19;
    GM_ST(uint8_t, ((uint8_t *)&t19 + 0), ((uint8_t)11ULL));
    gm_t9qkh0rsooc8d t20;
    GM_ST(uint8_t, ((uint8_t *)&t20 + 0), ((uint8_t)15ULL));
    gm_t9qkh0rsooc8d t21;
    GM_ST(uint8_t, ((uint8_t *)&t21 + 0), ((uint8_t)19ULL));
    gm_t9qkh0rsooc8d t22;
    GM_ST(uint8_t, ((uint8_t *)&t22 + 0), ((uint8_t)34ULL));
    gm_t9qkh0rsooc8d t23;
    GM_ST(uint8_t, ((uint8_t *)&t23 + 0), ((uint8_t)38ULL));
    gm_t6ofeqo2h242p t24;
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t24 + 0), t17);
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t24 + 1), t18);
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t24 + 2), t19);
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t24 + 3), t20);
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t24 + 4), t21);
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t24 + 5), t22);
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t24 + 6), t23);
    t3 = t24;
    goto me4;
mn15:;
    gm_t9qkh0rsooc8d t26;
    GM_ST(uint8_t, ((uint8_t *)&t26 + 0), ((uint8_t)2ULL));
    gm_t9qkh0rsooc8d t27;
    GM_ST(uint8_t, ((uint8_t *)&t27 + 0), ((uint8_t)25ULL));
    gm_t9qkh0rsooc8d t28;
    GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)12ULL));
    gm_t9qkh0rsooc8d t29;
    GM_ST(uint8_t, ((uint8_t *)&t29 + 0), ((uint8_t)16ULL));
    gm_t9qkh0rsooc8d t30;
    GM_ST(uint8_t, ((uint8_t *)&t30 + 0), ((uint8_t)20ULL));
    gm_t9qkh0rsooc8d t31;
    GM_ST(uint8_t, ((uint8_t *)&t31 + 0), ((uint8_t)35ULL));
    gm_t9qkh0rsooc8d t32;
    GM_ST(uint8_t, ((uint8_t *)&t32 + 0), ((uint8_t)39ULL));
    gm_t6ofeqo2h242p t33;
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t33 + 0), t26);
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t33 + 1), t27);
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t33 + 2), t28);
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t33 + 3), t29);
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t33 + 4), t30);
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t33 + 5), t31);
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t33 + 6), t32);
    t3 = t33;
    goto me4;
mn25:;
    gm_trap("no match arm matched the scrutinee", gm_srcvmmksi0829pa, 540, 36);
me4:;
    F.ret = t3;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) void gm_f_flush_newline_9023ml8(void * a0, const void * a1, uint64_t a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_flush_newline_9023ml8 F;
    void * gm_l0; /* lx */
    gm_t3fcbuehh3bs2 gm_l1; /* next */
    uint64_t gm_l2; /* next_hi */
    gm_tl874t2atnlh3 gm_l3; /* at */
    uint64_t gm_l4; /* depth */
    gm_taari1s0m51a7 gm_l5; /* innermost */
    uint64_t gm_l6; /* n */
    uint8_t gm_l7; /* dot_name */
    GM_STACK_CHECK(&F, 513u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&gm_l1, a1, sizeof gm_l1);
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        gm_toceffb697p0u t3 = GM_LD(gm_toceffb697p0u, (((uint8_t *)t2) + 224));
        gm_toceffb697p0u t4 = t3;
        gm_tl874t2atnlh3 t5;
        if (((((uint8_t *)&t4))[12] != 0)) {
            gm_tl874t2atnlh3 t6 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&t4) + 0));
            t5 = t6;
        } else {
            gm_b0_1 t7;
            uint8_t t8 = 0;
            F.ret = t7;
            goto gm_fret;
        }
        gm_l3 = t5;
    }
    {
        void * t9 = gm_l0;
        gm_toceffb697p0u t10;
        memset(&t10, 0, sizeof t10);
        GM_ST(gm_toceffb697p0u, (((uint8_t *)t9) + 224), t10);
    }
    {
        void * t11 = gm_l0;
        uint64_t t12 = GM_LD(uintptr_t, ((((uint8_t *)t11) + 120) + 0) + 8);
        gm_l4 = t12;
    }
    {
        gm_taari1s0m51a7 t13;
        uint64_t t15 = gm_l4;
        uint8_t t16 = (t15 > ((uint64_t)0ULL));
        if (!t16) goto ie14;
        uint64_t t18 = gm_l4;
        uint64_t r19; bool o20 = __builtin_sub_overflow(t18, ((uint64_t)1ULL), &r19);
        if (o20) gm_trap("integer overflow in `-`", gm_srcvmmksi0829pa, 671, 64);
        uint64_t t21 = r19;
        uint64_t t22 = (uintptr_t)t21;
        void * t23 = gm_l0;
        uint64_t t24 = GM_LD(uintptr_t, ((((uint8_t *)t23) + 120) + 0) + 8);
        if (t22 >= (uintptr_t)(t24)) gm_trap_index(t22, t24, gm_srcvmmksi0829pa, 671, 46);
        gm_t9qkh0rsooc8d t25 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t23) + 120) + 0)) + t22 * 16) + 0));
        gm_t9qkh0rsooc8d t26 = t25;
        gm_taari1s0m51a7 t27;
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t27), t26);
        ((uint8_t *)&t27)[1] = 1;
        t13 = t27;
        goto ix14;
ie14:;
        gm_taari1s0m51a7 t29;
        memset(&t29, 0, sizeof t29);
        gm_taari1s0m51a7 t30 = t29;
        t13 = t30;
ix14:;
        gm_l5 = t13;
    }
    {
        uint8_t t32 = 0;
        gm_taari1s0m51a7 t33 = gm_l5;
        gm_t9qkh0rsooc8d t34;
        GM_ST(uint8_t, ((uint8_t *)&t34 + 0), ((uint8_t)0ULL));
        gm_taari1s0m51a7 t35;
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t35), t34);
        ((uint8_t *)&t35)[1] = 1;
        gm_taari1s0m51a7 t36 = t33;
        gm_taari1s0m51a7 t37 = t35;
        uint8_t t38 = (gm_eqq6nrh55uon(((uint8_t *)&t36), ((uint8_t *)&t37)));
        if (!t38) {
            gm_taari1s0m51a7 t39 = gm_l5;
            gm_t9qkh0rsooc8d t40;
            GM_ST(uint8_t, ((uint8_t *)&t40 + 0), ((uint8_t)1ULL));
            gm_taari1s0m51a7 t41;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t41), t40);
            ((uint8_t *)&t41)[1] = 1;
            gm_taari1s0m51a7 t42 = t39;
            gm_taari1s0m51a7 t43 = t41;
            uint8_t t44 = (gm_eqq6nrh55uon(((uint8_t *)&t42), ((uint8_t *)&t43)));
            t32 = t44;
        } else t32 = 1;
        if (!t32) goto ie31;
        {
            gm_b0_1 t46;
            uint8_t t47 = 0;
            F.ret = t46;
            goto gm_fret;
        }
        goto ix31;
ie31:;
ix31:;
    }
    {
        void * t48 = gm_l0;
        uint64_t t49 = GM_LD(uintptr_t, ((((uint8_t *)t48) + 40) + 0) + 8);
        gm_l6 = t49;
    }
    {
        uint64_t t51 = gm_l6;
        uint8_t t52 = ((t51 == ((uint64_t)0ULL)));
        if (!t52) goto ie50;
        {
            gm_b0_1 t54;
            uint8_t t55 = 0;
            F.ret = t54;
            goto gm_fret;
        }
        goto ix50;
ie50:;
ix50:;
    }
    {
        uint64_t t57 = gm_l6;
        uint64_t r58; bool o59 = __builtin_sub_overflow(t57, ((uint64_t)1ULL), &r58);
        if (o59) gm_trap("integer overflow in `-`", gm_srcvmmksi0829pa, 675, 49);
        uint64_t t60 = r58;
        uint64_t t61 = (uintptr_t)t60;
        void * t62 = gm_l0;
        uint64_t t63 = GM_LD(uintptr_t, ((((uint8_t *)t62) + 40) + 0) + 8);
        if (t61 >= (uintptr_t)(t63)) gm_trap_index(t61, t63, gm_srcvmmksi0829pa, 675, 33);
        uint8_t t64 = gm_f_suppresses_newline_after_o08ord2(&(*(gm_t3fcbuehh3bs2 *)(((uint8_t *)GM_LD(void *, ((((uint8_t *)t62) + 40) + 0)) + t61 * 48) + 0)));
        if (!t64) goto ie56;
        {
            void * t66 = gm_l0;
            void * t67 = (void *)(((uint8_t *)t66) + 240);
            uint32_t t68 = GM_LD(uint32_t, (((uint8_t *)&gm_l3) + 4));
            gm_tftb1odc739t5 t69 = gm_f_push_im9d4ro(t67, t68);
            gm_tftb1odc739t5 t70 = t69;
            if (((((uint8_t *)&t70))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t70) + 1, gm_srcvmmksi0829pa, 676, 9);
            gm_b0_1 t71 = GM_LD(gm_b0_1, (((uint8_t *)&t70) + 1));
        }
        {
            gm_b0_1 t72;
            uint8_t t73 = 0;
            F.ret = t72;
            goto gm_fret;
        }
        goto ix56;
ie56:;
ix56:;
    }
    {
        gm_taari1s0m51a7 t75 = gm_l5;
        gm_t9qkh0rsooc8d t76;
        GM_ST(uint8_t, ((uint8_t *)&t76 + 0), ((uint8_t)3ULL));
        gm_taari1s0m51a7 t77;
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t77), t76);
        ((uint8_t *)&t77)[1] = 1;
        gm_taari1s0m51a7 t78 = t75;
        gm_taari1s0m51a7 t79 = t77;
        uint8_t t80 = (!gm_eqq6nrh55uon(((uint8_t *)&t78), ((uint8_t *)&t79)));
        if (!t80) goto ie74;
        {
            uint8_t t82 = 0;
            uint8_t t83 = 0;
            uint8_t t84 = 0;
            gm_t9qkh0rsooc8d t85;
            GM_ST(uint8_t, ((uint8_t *)&t85 + 0), ((uint8_t)58ULL));
            uint8_t t86 = gm_f_is_punct_fdeminj(&(*(gm_t3fcbuehh3bs2 *)((uint8_t *)&gm_l1)), t85);
            if (t86) {
                uint64_t t87 = gm_l2;
                void * t88 = gm_l0;
                uint64_t t89 = GM_LD(uintptr_t, (((uint8_t *)t88) + 8) + 8);
                uint8_t t90 = (t87 < t89);
                t84 = t90;
            } else t84 = 0;
            if (t84) {
                uint64_t t91 = gm_l2;
                uint64_t t92 = (uintptr_t)t91;
                void * t93 = gm_l0;
                uint64_t t94 = GM_LD(uintptr_t, (((uint8_t *)t93) + 8) + 8);
                if (t92 >= (uintptr_t)(t94)) gm_trap_index(t92, t94, gm_srcvmmksi0829pa, 681, 92);
                uint8_t t95 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)t93) + 8)) + t92 * 1));
                uint8_t t96 = gm_f_is_ident_start_rjefpfm(t95);
                t83 = t96;
            } else t83 = 0;
            if (t83) {
                void * t97 = gm_l0;
                uint64_t t98 = GM_LD(uintptr_t, (((uint8_t *)t97) + 8) + 8);
                void * t99 = ((uint8_t *)GM_LD(void *, (((uint8_t *)t97) + 8)));
                uint64_t t100 = gm_l2;
                uint64_t t101 = (uintptr_t)t100;
                if (t101 > (uintptr_t)t98 || (uintptr_t)t98 > (uintptr_t)t98) gm_trap_slice(t101, (uintptr_t)t98, t98, gm_srcvmmksi0829pa, 682, 32);
                gm_b16_8 t102;
                GM_ST(void *, (uint8_t *)&t102, (void *)(((uint8_t *)t99 + t101 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t102 + 8, (uintptr_t)((uintptr_t)t98 - t101));
                uint8_t t103 = gm_f_line_binds_jm0pnul(t102);
                uint8_t t104 = (uint8_t)!t103;
                t82 = t104;
            } else t82 = 0;
            gm_l7 = t82;
        }
        {
            uint8_t t106 = 0;
            uint8_t t107 = gm_l7;
            if (!t107) {
                uint8_t t108 = gm_f_starts_continuation_n3333i7(&(*(gm_t3fcbuehh3bs2 *)((uint8_t *)&gm_l1)));
                t106 = t108;
            } else t106 = 1;
            if (!t106) goto ie105;
            {
                gm_b0_1 t110;
                uint8_t t111 = 0;
                F.ret = t110;
                goto gm_fret;
            }
            goto ix105;
ie105:;
ix105:;
        }
        goto ix74;
ie74:;
ix74:;
    }
    {
        void * t112 = gm_l0;
        gm_tl874t2atnlh3 t113 = gm_l3;
        gm_f_push_newline_ke0tojq(t112, GM_CONV(gm_cseahvg8v7p0vs, t113));
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) void gm_f_flush_newline_9023ml8_c(void * a0, gm_t3fcbuehh3bs2 a1, uint64_t a2) { gm_f_flush_newline_9023ml8(a0, &a1, a2); }
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_inline_comment_end_8i5h1eq(gm_b16_8 a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_inline_comment_end_8i5h1eq F;
    gm_b16_8 gm_l0; /* s */
    uint64_t gm_l1; /* lo */
    uint64_t gm_l2; /* i */
    int32_t gm_l3; /* depth */
    uint8_t gm_l4; /* b */
    gm_tqs5p4n2tq19v gm_l5; /* next */
    GM_STACK_CHECK_SHALLOW(&F, 399u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint64_t t2 = gm_l1;
        uint64_t r3; bool o4 = __builtin_add_overflow(t2, ((uint64_t)2ULL), &r3);
        if (o4) gm_trap("integer overflow in `+`", gm_srcvmmksi0829pa, 709, 10);
        uint64_t t5 = r3;
        gm_l2 = t5;
    }
    {
        gm_l3 = ((int32_t)1LL);
    }
    {
        for (;;) {
            int32_t t7 = gm_l3;
            uint8_t t8 = (t7 > ((int32_t)0LL));
            if (!t8) goto lb6;
            {
                gm_b16_8 t11 = gm_l0;
                gm_b16_8 t12 = t11;
                void * t13 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t12)));
                uint64_t t14 = (uintptr_t)GM_LD(uintptr_t, ((uint8_t *)&t12) + 8);
                uint64_t t15 = gm_l2;
                uint64_t t16 = (uintptr_t)t15;
                gm_tqs5p4n2tq19v t17;
                if (t16 < t14) {
                uint8_t t18 = GM_LD(uint8_t, ((uint8_t *)t13 + t16 * 1));
                gm_tqs5p4n2tq19v t19;
                GM_ST(uint8_t, ((uint8_t *)&t19), t18);
                ((uint8_t *)&t19)[1] = 1;
                    t17 = t19;
                } else {
                gm_tqs5p4n2tq19v t20;
                memset(&t20, 0, sizeof t20);
                    t17 = t20;
                }
                gm_tqs5p4n2tq19v t21 = t17;
                uint8_t t22;
                if (((((uint8_t *)&t21))[1] != 0)) {
                    uint8_t t23 = GM_LD(uint8_t, (((uint8_t *)&t21) + 0));
                    t22 = t23;
                } else {
                    gm_tf5srh4mp4b3i t24;
                    memset(&t24, 0, sizeof t24);
                    uint8_t t25 = 0;
                    F.ret = t24;
                    goto gm_fret;
                }
                gm_l4 = t22;
            }
            {
                gm_b16_8 t26 = gm_l0;
                gm_b16_8 t27 = t26;
                void * t28 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t27)));
                uint64_t t29 = (uintptr_t)GM_LD(uintptr_t, ((uint8_t *)&t27) + 8);
                uint64_t t30 = gm_l2;
                uint64_t r31; bool o32 = __builtin_add_overflow(t30, ((uint64_t)1ULL), &r31);
                if (o32) gm_trap("integer overflow in `+`", gm_srcvmmksi0829pa, 713, 23);
                uint64_t t33 = r31;
                uint64_t t34 = (uintptr_t)t33;
                gm_tqs5p4n2tq19v t35;
                if (t34 < t29) {
                uint8_t t36 = GM_LD(uint8_t, ((uint8_t *)t28 + t34 * 1));
                gm_tqs5p4n2tq19v t37;
                GM_ST(uint8_t, ((uint8_t *)&t37), t36);
                ((uint8_t *)&t37)[1] = 1;
                    t35 = t37;
                } else {
                gm_tqs5p4n2tq19v t38;
                memset(&t38, 0, sizeof t38);
                    t35 = t38;
                }
                gm_l5 = t35;
            }
            {
                uint8_t t40 = gm_l4;
                uint8_t t41 = ((t40 == ((uint8_t)47ULL)));
                if (!t41) goto ie39;
                gm_tqs5p4n2tq19v t42 = gm_l5;
                gm_tqs5p4n2tq19v t43;
                GM_ST(uint8_t, ((uint8_t *)&t43), ((uint8_t)42ULL));
                ((uint8_t *)&t43)[1] = 1;
                gm_tqs5p4n2tq19v t44 = t42;
                gm_tqs5p4n2tq19v t45 = t43;
                uint8_t t46 = (gm_eqb44cb3cf1n(((uint8_t *)&t44), ((uint8_t *)&t45)));
                if (!t46) goto ie39;
                {
                    int32_t t48 = gm_l3;
                    int32_t r49; bool o50 = __builtin_add_overflow(t48, ((int32_t)1LL), &r49);
                    if (o50) gm_trap("integer overflow in `+`", gm_srcvmmksi0829pa, 715, 13);
                    int32_t t51 = r49;
                    gm_l3 = t51;
                }
                {
                    uint64_t t52 = gm_l2;
                    uint64_t r53; bool o54 = __builtin_add_overflow(t52, ((uint64_t)2ULL), &r53);
                    if (o54) gm_trap("integer overflow in `+`", gm_srcvmmksi0829pa, 716, 13);
                    uint64_t t55 = r53;
                    gm_l2 = t55;
                }
                goto ix39;
ie39:;
                uint8_t t57 = gm_l4;
                uint8_t t58 = ((t57 == ((uint8_t)42ULL)));
                if (!t58) goto ie56;
                gm_tqs5p4n2tq19v t59 = gm_l5;
                gm_tqs5p4n2tq19v t60;
                GM_ST(uint8_t, ((uint8_t *)&t60), ((uint8_t)47ULL));
                ((uint8_t *)&t60)[1] = 1;
                gm_tqs5p4n2tq19v t61 = t59;
                gm_tqs5p4n2tq19v t62 = t60;
                uint8_t t63 = (gm_eqb44cb3cf1n(((uint8_t *)&t61), ((uint8_t *)&t62)));
                if (!t63) goto ie56;
                {
                    int32_t t65 = gm_l3;
                    int32_t r66; bool o67 = __builtin_sub_overflow(t65, ((int32_t)1LL), &r66);
                    if (o67) gm_trap("integer overflow in `-`", gm_srcvmmksi0829pa, 718, 13);
                    int32_t t68 = r66;
                    gm_l3 = t68;
                }
                {
                    uint64_t t69 = gm_l2;
                    uint64_t r70; bool o71 = __builtin_add_overflow(t69, ((uint64_t)2ULL), &r70);
                    if (o71) gm_trap("integer overflow in `+`", gm_srcvmmksi0829pa, 719, 13);
                    uint64_t t72 = r70;
                    gm_l2 = t72;
                }
                goto ix56;
ie56:;
                uint8_t t74 = gm_l4;
                gm_tqs5p4n2tq19v t75;
                GM_ST(uint8_t, ((uint8_t *)&t75 + 0), ((uint8_t)10ULL));
                GM_ST(uint8_t, ((uint8_t *)&t75 + 1), ((uint8_t)13ULL));
                gm_tqs5p4n2tq19v t76 = t75;
                uint8_t t77 = 0;
                uint8_t t78 = t74;
                if (gm_eqjmccqdb90f(((uint8_t *)&t76) + 0, ((uint8_t *)&t78))) t77 = 1;
                if (gm_eqjmccqdb90f(((uint8_t *)&t76) + 1, ((uint8_t *)&t78))) t77 = 1;
                uint8_t t79 = (uint8_t)(t77);
                if (!t79) goto ie73;
                {
                    gm_tf5srh4mp4b3i t81;
                    memset(&t81, 0, sizeof t81);
                    uint8_t t82 = 0;
                    F.ret = t81;
                    goto gm_fret;
                }
                goto ix73;
ie73:;
                {
                    uint64_t t84 = gm_l2;
                    uint64_t r85; bool o86 = __builtin_add_overflow(t84, ((uint64_t)1ULL), &r85);
                    if (o86) gm_trap("integer overflow in `+`", gm_srcvmmksi0829pa, 723, 13);
                    uint64_t t87 = r85;
                    gm_l2 = t87;
                }
ix73:;
ix56:;
ix39:;
            }
lc6:;
        }
lb6:;
    }
    {
        uint64_t t88 = gm_l2;
        gm_tf5srh4mp4b3i t89;
        GM_ST(uint8_t, (uint8_t *)&t89 + 9, 0);
        GM_ST(uint16_t, (uint8_t *)&t89 + 10, 0);
        GM_ST(uint32_t, (uint8_t *)&t89 + 12, 0);
        GM_ST(uint64_t, ((uint8_t *)&t89), t88);
        ((uint8_t *)&t89)[8] = 1;
        uint8_t t90 = 0;
        F.ret = t89;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_inline_raw_string_end_6lenf33(gm_b16_8 a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_inline_raw_string_end_6lenf33 F;
    gm_b16_8 gm_l0; /* s */
    uint64_t gm_l1; /* lo */
    uint64_t gm_l2; /* delim */
    uint64_t gm_l3; /* i */
    uint8_t gm_l4; /* b */
    uint64_t gm_l5; /* n */
    GM_STACK_CHECK_SHALLOW(&F, 370u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        gm_b16_8 t2 = gm_l0;
        uint64_t t3 = gm_l1;
        uint64_t t4 = gm_f_backtick_run_l95fb09(t2, t3);
        gm_l2 = t4;
    }
    {
        uint64_t t5 = gm_l1;
        uint64_t t6 = gm_l2;
        uint64_t r7; bool o8 = __builtin_add_overflow(t5, t6, &r7);
        if (o8) gm_trap("integer overflow in `+`", gm_srcvmmksi0829pa, 740, 10);
        uint64_t t9 = r7;
        gm_l3 = t9;
    }
    {
        for (;;) {
            if (!((uint8_t)1)) goto lb10;
            {
                gm_b16_8 t13 = gm_l0;
                gm_b16_8 t14 = t13;
                void * t15 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t14)));
                uint64_t t16 = (uintptr_t)GM_LD(uintptr_t, ((uint8_t *)&t14) + 8);
                uint64_t t17 = gm_l3;
                uint64_t t18 = (uintptr_t)t17;
                gm_tqs5p4n2tq19v t19;
                if (t18 < t16) {
                uint8_t t20 = GM_LD(uint8_t, ((uint8_t *)t15 + t18 * 1));
                gm_tqs5p4n2tq19v t21;
                GM_ST(uint8_t, ((uint8_t *)&t21), t20);
                ((uint8_t *)&t21)[1] = 1;
                    t19 = t21;
                } else {
                gm_tqs5p4n2tq19v t22;
                memset(&t22, 0, sizeof t22);
                    t19 = t22;
                }
                gm_tqs5p4n2tq19v t23 = t19;
                uint8_t t24;
                if (((((uint8_t *)&t23))[1] != 0)) {
                    uint8_t t25 = GM_LD(uint8_t, (((uint8_t *)&t23) + 0));
                    t24 = t25;
                } else {
                    gm_tf5srh4mp4b3i t26;
                    memset(&t26, 0, sizeof t26);
                    uint8_t t27 = 0;
                    F.ret = t26;
                    goto gm_fret;
                }
                gm_l4 = t24;
            }
            {
                uint8_t t29 = gm_l4;
                uint8_t t30 = ((t29 == ((uint8_t)96ULL)));
                if (!t30) goto ie28;
                {
                    gm_b16_8 t32 = gm_l0;
                    uint64_t t33 = gm_l3;
                    uint64_t t34 = gm_f_backtick_run_l95fb09(t32, t33);
                    gm_l5 = t34;
                }
                {
                    uint64_t t35 = gm_l3;
                    uint64_t t36 = gm_l5;
                    uint64_t r37; bool o38 = __builtin_add_overflow(t35, t36, &r37);
                    if (o38) gm_trap("integer overflow in `+`", gm_srcvmmksi0829pa, 745, 13);
                    uint64_t t39 = r37;
                    gm_l3 = t39;
                }
                {
                    uint64_t t41 = gm_l5;
                    uint64_t t42 = gm_l2;
                    uint8_t t43 = ((t41 == t42));
                    if (!t43) goto ie40;
                    {
                        uint64_t t45 = gm_l3;
                        gm_tf5srh4mp4b3i t46;
                        GM_ST(uint8_t, (uint8_t *)&t46 + 9, 0);
                        GM_ST(uint16_t, (uint8_t *)&t46 + 10, 0);
                        GM_ST(uint32_t, (uint8_t *)&t46 + 12, 0);
                        GM_ST(uint64_t, ((uint8_t *)&t46), t45);
                        ((uint8_t *)&t46)[8] = 1;
                        uint8_t t47 = 0;
                        F.ret = t46;
                        goto gm_fret;
                    }
                    goto ix40;
ie40:;
ix40:;
                }
                goto ix28;
ie28:;
                uint8_t t49 = gm_l4;
                gm_tqs5p4n2tq19v t50;
                GM_ST(uint8_t, ((uint8_t *)&t50 + 0), ((uint8_t)10ULL));
                GM_ST(uint8_t, ((uint8_t *)&t50 + 1), ((uint8_t)13ULL));
                gm_tqs5p4n2tq19v t51 = t50;
                uint8_t t52 = 0;
                uint8_t t53 = t49;
                if (gm_eqjmccqdb90f(((uint8_t *)&t51) + 0, ((uint8_t *)&t53))) t52 = 1;
                if (gm_eqjmccqdb90f(((uint8_t *)&t51) + 1, ((uint8_t *)&t53))) t52 = 1;
                uint8_t t54 = (uint8_t)(t52);
                if (!t54) goto ie48;
                {
                    gm_tf5srh4mp4b3i t56;
                    memset(&t56, 0, sizeof t56);
                    uint8_t t57 = 0;
                    F.ret = t56;
                    goto gm_fret;
                }
                goto ix48;
ie48:;
                {
                    uint64_t t59 = gm_l3;
                    uint64_t r60; bool o61 = __builtin_add_overflow(t59, ((uint64_t)1ULL), &r60);
                    if (o61) gm_trap("integer overflow in `+`", gm_srcvmmksi0829pa, 750, 13);
                    uint64_t t62 = r60;
                    gm_l3 = t62;
                }
ix48:;
ix28:;
            }
lc10:;
        }
lb10:;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) uint8_t gm_f_is_ascii_space_t2ltvst(uint8_t a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_unicode_space_b2m9rku(uint32_t a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_unicode_space_b2m9rku F;
    uint32_t gm_l0; /* c */
    uint32_t gm_l1; /* <tmp> */
    GM_STACK_CHECK_SHALLOW(&F, 102u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t2 = 0;
        uint32_t t3 = gm_l0;
        gm_tu4r32bi6mhcq t4;
        GM_ST(uint32_t, (uint8_t *)&t4 + 0, ((uint32_t)133ULL));
        GM_ST(uint32_t, (uint8_t *)&t4 + 4, ((uint32_t)160ULL));
        GM_ST(uint32_t, (uint8_t *)&t4 + 8, ((uint32_t)5760ULL));
        GM_ST(uint32_t, (uint8_t *)&t4 + 12, ((uint32_t)8232ULL));
        GM_ST(uint32_t, (uint8_t *)&t4 + 16, ((uint32_t)8233ULL));
        GM_ST(uint32_t, (uint8_t *)&t4 + 20, ((uint32_t)8239ULL));
        GM_ST(uint32_t, (uint8_t *)&t4 + 24, ((uint32_t)8287ULL));
        GM_ST(uint32_t, (uint8_t *)&t4 + 28, ((uint32_t)12288ULL));
        gm_tu4r32bi6mhcq t5 = t4;
        uint8_t t6 = 0;
        uint32_t t7 = t3;
        if (gm_eq467d5cm1d5(((uint8_t *)&t5) + 0, ((uint8_t *)&t7))) t6 = 1;
        if (gm_eq467d5cm1d5(((uint8_t *)&t5) + 4, ((uint8_t *)&t7))) t6 = 1;
        if (gm_eq467d5cm1d5(((uint8_t *)&t5) + 8, ((uint8_t *)&t7))) t6 = 1;
        if (gm_eq467d5cm1d5(((uint8_t *)&t5) + 12, ((uint8_t *)&t7))) t6 = 1;
        if (gm_eq467d5cm1d5(((uint8_t *)&t5) + 16, ((uint8_t *)&t7))) t6 = 1;
        if (gm_eq467d5cm1d5(((uint8_t *)&t5) + 20, ((uint8_t *)&t7))) t6 = 1;
        if (gm_eq467d5cm1d5(((uint8_t *)&t5) + 24, ((uint8_t *)&t7))) t6 = 1;
        if (gm_eq467d5cm1d5(((uint8_t *)&t5) + 28, ((uint8_t *)&t7))) t6 = 1;
        uint8_t t8 = (uint8_t)(t6);
        if (!t8) {
            {
                uint32_t t10 = gm_l0;
                gm_l1 = t10;
            }
            uint8_t t11 = 0;
            uint32_t t12 = gm_l1;
            uint8_t t13 = (((uint32_t)8192ULL) <= t12);
            if (t13) {
                uint32_t t14 = gm_l1;
                uint8_t t15 = (t14 <= ((uint32_t)8202ULL));
                t11 = t15;
            } else t11 = 0;
            uint8_t t16 = t11;
            t2 = t16;
        } else t2 = 1;
        uint8_t t17 = 0;
        F.ret = t2;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) void gm_f_lex_dc8mbmf(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint32_t a0, gm_b16_8 a1, void * a2);
inline __attribute__((aligned(4))) void gm_f_lex_recovering_obo2892(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint32_t a0, gm_b16_8 a1, void * a2) {
    struct gm_fr_lex_recovering_obo2892 F;
    uint32_t gm_l0; /* file */
    gm_b16_8 gm_l1; /* src */
    void * gm_l2; /* diags */
    gm_b16_8 gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    GM_STACK_CHECK(&F, 399u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        gm_f_diagnostics_n3kkhf4((void *)&F.l3, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        uint32_t t2 = gm_l0;
        gm_b16_8 t3 = gm_l1;
        void * t4 = (void *)((uint8_t *)&F.l3);
        gm_f_new_lexer_dm12g12((void *)&F.l4, (void *)NULL, cx, NULL, t2, t3, t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t5 = (void *)((uint8_t *)&F.l4);
        gm_f_run_v26is74((void *)&F.l5, (void *)NULL, cx, NULL, t5);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t7 = (void *)((uint8_t *)&F.l3);
        uint8_t t8 = gm_f_has_errors_if7ai0m(t7);
        uint8_t t9 = (uint8_t)!t8;
        if (!t9) goto ie6;
        {
            {
                gm_b16_8 t12 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l3) + 0) + 0));
                gm_l6 = t12;
            }
            {
                gm_l7 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t14 = gm_l7;
                    uint64_t t15 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                    uint8_t t16 = (t14 < t15);
                    if (!t16) goto lb13;
                    {
                        uint64_t t19 = gm_l7;
                        uint64_t t20 = (uintptr_t)t19;
                        uint64_t t21 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                        gm_t4gut2c9fj1kt t24 = GM_LD(gm_t4gut2c9fj1kt, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t20 * 184));
                        F.l8 = t24;
                        {
                            void * t26 = gm_l2;
                            gm_f_push_o4ckdr1(t26, &(*(gm_t4gut2c9fj1kt *)((uint8_t *)&F.l8)));
                        }
                        goto me22;
mn23:;
                        gm_trap("no match arm matched the scrutinee", gm_srcvmmksi0829pa, 30, 9);
me22:;
                    }
lc13:;
                    {
                        uint64_t t28 = gm_l7;
                        uint64_t r29; bool o30 = __builtin_add_overflow(t28, ((uint64_t)1ULL), &r29);
                        uint64_t t31 = r29;
                        gm_l7 = t31;
                    }
                }
lb13:;
            }
        }
        {
            memmove(gm_out, ((uint8_t *)&F.l5), 120);
            goto gm_fdone;
        }
        goto ix6;
ie6:;
ix6:;
    }
    {
        uint32_t t32 = gm_l0;
        gm_b16_8 t33 = gm_l1;
        void * t34 = gm_l2;
        gm_f_new_lexer_dm12g12((void *)&F.l9, (void *)NULL, cx, NULL, t32, t33, t34);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        GM_ST(uint8_t, (((uint8_t *)&F.l9) + 280), ((uint8_t)1));
    }
    {
        void * t35 = (void *)((uint8_t *)&F.l9);
        gm_f_run_v26is74((void *)&(*(gm_tr8dvlu9ik4gj *)gm_out), (void *)NULL, cx, NULL, t35);
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
__attribute__((aligned(4))) gm_b0_1 gm_f_lex_token_h8g44s4(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1) {
    struct gm_fr_lex_token_h8g44s4 F;
    void * gm_l0; /* lx */
    uint8_t gm_l1; /* b */
    uint64_t gm_l2; /* lo */
    uint8_t gm_l3; /* next */
    gm_b16_8 gm_l4; /* name */
    gm_t3fcbuehh3bs2 gm_l5; /* kind */
    gm_t9qkh0rsooc8d gm_l6; /* p */
    GM_STACK_CHECK(&F, 675u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        uint64_t t3 = GM_LD(uint64_t, (((uint8_t *)t2) + 24));
        gm_l2 = t3;
    }
    {
        uint8_t t4 = gm_l1;
        uint8_t t5 = t4;
        uint8_t t10 = GM_LD(uint8_t, ((uint8_t *)&t5));
        if ((t10 < ((uint8_t)97ULL)) || (t10 > ((uint8_t)122ULL))) goto pa9;
        goto po8;
pa9:;
        uint8_t t12 = GM_LD(uint8_t, ((uint8_t *)&t5));
        if ((t12 < ((uint8_t)65ULL)) || (t12 > ((uint8_t)90ULL))) goto pa11;
        goto po8;
pa11:;
        uint8_t t14 = GM_LD(uint8_t, ((uint8_t *)&t5));
        if (!(t14 == ((uint8_t)95ULL))) goto pa13;
        goto po8;
pa13:;
        goto mn7;
po8:;
        void * t15 = gm_l0;
        gm_b0_1 t16 = gm_f_lex_word_9li16te((void *)NULL, cx, NULL, t15);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me6;
mn7:;
        uint8_t t18 = GM_LD(uint8_t, ((uint8_t *)&t5));
        if ((t18 < ((uint8_t)48ULL)) || (t18 > ((uint8_t)57ULL))) goto mn17;
        void * t19 = gm_l0;
        gm_b0_1 t20 = gm_f_lex_number_9j9sdfi((void *)NULL, cx, NULL, t19);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me6;
mn17:;
        uint8_t t22 = GM_LD(uint8_t, ((uint8_t *)&t5));
        if (!(t22 == ((uint8_t)34ULL))) goto mn21;
        void * t23 = gm_l0;
        uint64_t t24 = gm_l2;
        gm_t9qkh0rsooc8d t25;
        GM_ST(uint8_t, ((uint8_t *)&t25 + 0), ((uint8_t)0ULL));
        gm_b0_1 t26 = gm_f_lex_string_bcdbdjm((void *)NULL, cx, NULL, t23, t24, t25);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me6;
mn21:;
        uint8_t t28 = GM_LD(uint8_t, ((uint8_t *)&t5));
        if (!(t28 == ((uint8_t)96ULL))) goto mn27;
        void * t29 = gm_l0;
        gm_b0_1 t30 = gm_f_lex_raw_string_uf6bd4g((void *)NULL, cx, NULL, t29);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me6;
mn27:;
        uint8_t t32 = GM_LD(uint8_t, ((uint8_t *)&t5));
        if (!(t32 == ((uint8_t)39ULL))) goto mn31;
        void * t33 = gm_l0;
        gm_b0_1 t34 = gm_f_lex_char_4qk4vjp((void *)NULL, cx, NULL, t33);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me6;
mn31:;
        uint8_t t36 = GM_LD(uint8_t, ((uint8_t *)&t5));
        if (!(t36 == ((uint8_t)59ULL))) goto mn35;
        {
            void * t38 = gm_l0;
            void * t39 = gm_l0;
            uint64_t t40 = GM_LD(uint64_t, (((uint8_t *)t39) + 24));
            uint64_t r41; bool o42 = __builtin_add_overflow(t40, ((uint64_t)1ULL), &r41);
            if (o42) gm_trap("integer overflow in `+`", gm_srcvmmksi0829pa, 299, 13);
            uint64_t t43 = r41;
            GM_ST(uint64_t, (((uint8_t *)t38) + 24), t43);
        }
        {
            void * t44 = gm_l0;
            uint64_t t45 = gm_l2;
            gm_b0_1 t46 = gm_f_emit_semicolon_7da5b93((void *)NULL, cx, NULL, t44, t45);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto me6;
mn35:;
        uint8_t t50 = GM_LD(uint8_t, ((uint8_t *)&t5));
        if (!(t50 == ((uint8_t)64ULL))) goto pa49;
        goto po48;
pa49:;
        uint8_t t52 = GM_LD(uint8_t, ((uint8_t *)&t5));
        if (!(t52 == ((uint8_t)35ULL))) goto pa51;
        goto po48;
pa51:;
        goto mn47;
po48:;
        {
            void * t54 = gm_l0;
            void * t55 = gm_l0;
            uint64_t t56 = GM_LD(uint64_t, (((uint8_t *)t55) + 24));
            uint64_t r57; bool o58 = __builtin_add_overflow(t56, ((uint64_t)1ULL), &r57);
            if (o58) gm_trap("integer overflow in `+`", gm_srcvmmksi0829pa, 303, 13);
            uint64_t t59 = r57;
            GM_ST(uint64_t, (((uint8_t *)t54) + 24), t59);
        }
        {
            void * t60 = gm_l0;
            gm_tqs5p4n2tq19v t61 = gm_f_peek_72bostn(t60);
            gm_tqs5p4n2tq19v t62 = t61;
            uint8_t t63;
            if (((((uint8_t *)&t62))[1] != 0)) {
                uint8_t t64 = GM_LD(uint8_t, (((uint8_t *)&t62) + 0));
                t63 = t64;
            } else {
                t63 = ((uint8_t)0ULL);
            }
            gm_l3 = t63;
        }
        {
            uint8_t t66 = gm_l3;
            uint8_t t67 = gm_f_is_ident_start_rjefpfm(t66);
            if (!t67) goto ie65;
            {
                void * t69 = gm_l0;
                gm_b16_8 t70 = gm_f_scan_ident_h1b2n4u(t69);
                gm_l4 = t70;
            }
            {
                gm_t3fcbuehh3bs2 t71;
                uint8_t t73 = gm_l1;
                uint8_t t74 = ((t73 == ((uint8_t)64ULL)));
                if (!t74) goto ie72;
                gm_b16_8 t76 = gm_l4;
                gm_t3fcbuehh3bs2 t77;
                GM_ST(uint8_t, (uint8_t *)&t77 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t77 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t77 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t77 + 24, 0);
                GM_ST(uint8_t, ((uint8_t *)&t77 + 0), ((uint8_t)6ULL));
                GM_ST(gm_b16_8, ((uint8_t *)&t77 + 8), t76);
                gm_t3fcbuehh3bs2 t78 = t77;
                t71 = t78;
                goto ix72;
ie72:;
                gm_b16_8 t80 = gm_l4;
                gm_t3fcbuehh3bs2 t81;
                GM_ST(uint8_t, (uint8_t *)&t81 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t81 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t81 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t81 + 24, 0);
                GM_ST(uint8_t, ((uint8_t *)&t81 + 0), ((uint8_t)7ULL));
                GM_ST(gm_b16_8, ((uint8_t *)&t81 + 8), t80);
                gm_t3fcbuehh3bs2 t82 = t81;
                t71 = t82;
ix72:;
                gm_l5 = t71;
            }
            {
                void * t83 = gm_l0;
                uint64_t t84 = gm_l2;
                void * t85 = gm_l0;
                uint64_t t86 = GM_LD(uint64_t, (((uint8_t *)t85) + 24));
                gm_b0_1 t87 = gm_f_emit_ekvv703((void *)NULL, cx, NULL, t83, &(*(gm_t3fcbuehh3bs2 *)((uint8_t *)&gm_l5)), t84, t86);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix65;
ie65:;
            {
                void * t89 = gm_l0;
                gm_t9qkh0rsooc8d t90;
                uint8_t t92 = gm_l1;
                uint8_t t93 = ((t92 == ((uint8_t)64ULL)));
                if (!t93) goto ie91;
                gm_t9qkh0rsooc8d t95;
                GM_ST(uint8_t, ((uint8_t *)&t95 + 0), ((uint8_t)48ULL));
                gm_t9qkh0rsooc8d t96 = t95;
                t90 = t96;
                goto ix91;
ie91:;
                gm_t9qkh0rsooc8d t98;
                GM_ST(uint8_t, ((uint8_t *)&t98 + 0), ((uint8_t)49ULL));
                gm_t9qkh0rsooc8d t99 = t98;
                t90 = t99;
ix91:;
                gm_t3fcbuehh3bs2 t100;
                GM_ST(uint8_t, (uint8_t *)&t100 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t100 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t100 + 4, 0);
                GM_ST(uint8_t, (uint8_t *)&t100 + 9, 0);
                GM_ST(uint16_t, (uint8_t *)&t100 + 10, 0);
                GM_ST(uint32_t, (uint8_t *)&t100 + 12, 0);
                GM_ST(uint64_t, (uint8_t *)&t100 + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&t100 + 24, 0);
                GM_ST(uint8_t, ((uint8_t *)&t100 + 0), ((uint8_t)8ULL));
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t100 + 8), t90);
                uint64_t t101 = gm_l2;
                void * t102 = gm_l0;
                uint64_t t103 = GM_LD(uint64_t, (((uint8_t *)t102) + 24));
                gm_b0_1 t104 = gm_f_emit_ekvv703((void *)NULL, cx, NULL, t89, &t100, t101, t103);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
ix65:;
        }
        goto me6;
mn47:;
        uint8_t t108 = GM_LD(uint8_t, ((uint8_t *)&t5));
        if (!(t108 == ((uint8_t)61ULL))) goto pa107;
        goto po106;
pa107:;
        uint8_t t110 = GM_LD(uint8_t, ((uint8_t *)&t5));
        if (!(t110 == ((uint8_t)33ULL))) goto pa109;
        goto po106;
pa109:;
        goto mn105;
po106:;
        void * t111 = gm_l0;
        gm_tqs5p4n2tq19v t112 = gm_f_peek_at_74p8lkq(t111, ((uint64_t)1ULL));
        gm_tqs5p4n2tq19v t113;
        GM_ST(uint8_t, ((uint8_t *)&t113), ((uint8_t)61ULL));
        ((uint8_t *)&t113)[1] = 1;
        gm_tqs5p4n2tq19v t114 = t112;
        gm_tqs5p4n2tq19v t115 = t113;
        uint8_t t116 = (gm_eqb44cb3cf1n(((uint8_t *)&t114), ((uint8_t *)&t115)));
        if (!t116) goto mn105;
        void * t117 = gm_l0;
        uint8_t t118 = gm_l1;
        gm_b0_1 t119 = gm_f_c_equality_u566nc4((void *)NULL, cx, NULL, t117, t118);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me6;
mn105:;
        {
            void * t123 = gm_l0;
            gm_taari1s0m51a7 t124 = gm_f_scan_punct_ecrh51c(t123);
            gm_taari1s0m51a7 t125 = t124;
            if (!((((uint8_t *)&t125))[1] != 0)) goto ie122;
            gm_t9qkh0rsooc8d t126 = GM_LD(gm_t9qkh0rsooc8d, ((uint8_t *)&t125));
            gm_l6 = t126;
            {
                void * t128 = gm_l0;
                gm_t9qkh0rsooc8d t129 = gm_l6;
                gm_t3fcbuehh3bs2 t130;
                GM_ST(uint8_t, (uint8_t *)&t130 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t130 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t130 + 4, 0);
                GM_ST(uint8_t, (uint8_t *)&t130 + 9, 0);
                GM_ST(uint16_t, (uint8_t *)&t130 + 10, 0);
                GM_ST(uint32_t, (uint8_t *)&t130 + 12, 0);
                GM_ST(uint64_t, (uint8_t *)&t130 + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&t130 + 24, 0);
                GM_ST(uint8_t, ((uint8_t *)&t130 + 0), ((uint8_t)8ULL));
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t130 + 8), t129);
                uint64_t t131 = gm_l2;
                void * t132 = gm_l0;
                uint64_t t133 = GM_LD(uint64_t, (((uint8_t *)t132) + 24));
                gm_b0_1 t134 = gm_f_emit_ekvv703((void *)NULL, cx, NULL, t128, &t130, t131, t133);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix122;
ie122:;
            {
                void * t136 = gm_l0;
                gm_b0_1 t137 = gm_f_unexpected_char_r3t8d4c((void *)NULL, cx, NULL, t136);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
ix122:;
        }
        goto me6;
mn120:;
        gm_trap("no match arm matched the scrutinee", gm_srcvmmksi0829pa, 292, 5);
me6:;
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
__attribute__((aligned(4))) gm_b0_1 gm_f_lex_word_9li16te(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_lex_word_9li16te F;
    void * gm_l0; /* lx */
    uint64_t gm_l1; /* lo */
    gm_b16_8 gm_l2; /* text */
    gm_t9qkh0rsooc8d gm_l3; /* kw */
    gm_taari1s0m51a7 gm_l4; /* two_word */
    uint64_t gm_l5; /* end */
    uint64_t gm_l6; /* e */
    uint64_t gm_l7; /* e */
    gm_t9qkh0rsooc8d gm_l8; /* p */
    GM_STACK_CHECK(&F, 630u);
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
        gm_b16_8 t5 = gm_f_scan_ident_h1b2n4u(t4);
        gm_l2 = t5;
    }
    {
        uint8_t t7 = (((GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8) == 1) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l2)), gm_s2gnpj7k4l22n, 1) == 0));
        if (!t7) goto ie6;
        void * t8 = gm_l0;
        gm_tqs5p4n2tq19v t9 = gm_f_peek_72bostn(t8);
        gm_tqs5p4n2tq19v t10;
        GM_ST(uint8_t, ((uint8_t *)&t10), ((uint8_t)34ULL));
        ((uint8_t *)&t10)[1] = 1;
        gm_tqs5p4n2tq19v t11 = t9;
        gm_tqs5p4n2tq19v t12 = t10;
        uint8_t t13 = (gm_eqb44cb3cf1n(((uint8_t *)&t11), ((uint8_t *)&t12)));
        if (!t13) goto ie6;
        {
            void * t15 = gm_l0;
            uint64_t t16 = gm_l1;
            gm_t9qkh0rsooc8d t17;
            GM_ST(uint8_t, ((uint8_t *)&t17 + 0), ((uint8_t)2ULL));
            gm_b0_1 t18 = gm_f_lex_string_bcdbdjm((void *)NULL, cx, NULL, t15, t16, t17);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_b0_1 t19;
            uint8_t t20 = 0;
            F.ret = t19;
            goto gm_fret;
        }
        goto ix6;
ie6:;
ix6:;
    }
    {
        uint8_t t22 = (((GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8) == 1) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l2)), gm_sfdbfrv46lihd, 1) == 0));
        if (!t22) goto ie21;
        {
            void * t24 = gm_l0;
            gm_t3fcbuehh3bs2 t25;
            GM_ST(uint8_t, (uint8_t *)&t25 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t25 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t25 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t25 + 8, 0);
            GM_ST(uint64_t, (uint8_t *)&t25 + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&t25 + 24, 0);
            GM_ST(uint8_t, ((uint8_t *)&t25 + 0), ((uint8_t)1ULL));
            uint64_t t26 = gm_l1;
            void * t27 = gm_l0;
            uint64_t t28 = GM_LD(uint64_t, (((uint8_t *)t27) + 24));
            gm_b0_1 t29 = gm_f_emit_ekvv703((void *)NULL, cx, NULL, t24, &t25, t26, t28);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_b0_1 t30;
            uint8_t t31 = 0;
            F.ret = t30;
            goto gm_fret;
        }
        goto ix21;
ie21:;
ix21:;
    }
    {
        gm_b16_8 t32 = gm_l2;
        gm_taari1s0m51a7 t33 = gm_f_keyword_from_1ofmh1u(t32);
        gm_taari1s0m51a7 t34 = t33;
        gm_t9qkh0rsooc8d t35;
        if (((((uint8_t *)&t34))[1] != 0)) {
            gm_t9qkh0rsooc8d t36 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&t34) + 0));
            t35 = t36;
        } else {
            {
                void * t38 = gm_l0;
                gm_b16_8 t39 = gm_l2;
                gm_t3fcbuehh3bs2 t40;
                GM_ST(uint8_t, (uint8_t *)&t40 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t40 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t40 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t40 + 24, 0);
                GM_ST(uint8_t, ((uint8_t *)&t40 + 0), ((uint8_t)0ULL));
                GM_ST(gm_b16_8, ((uint8_t *)&t40 + 8), t39);
                uint64_t t41 = gm_l1;
                void * t42 = gm_l0;
                uint64_t t43 = GM_LD(uint64_t, (((uint8_t *)t42) + 24));
                gm_b0_1 t44 = gm_f_emit_ekvv703((void *)NULL, cx, NULL, t38, &t40, t41, t43);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b0_1 t45;
            uint8_t t46 = 0;
            F.ret = t45;
            goto gm_fret;
        }
        gm_l3 = t35;
    }
    {
        gm_taari1s0m51a7 t47;
        memset(&t47, 0, sizeof t47);
        gm_l4 = t47;
    }
    {
        gm_l5 = ((uint64_t)0ULL);
    }
    {
        uint8_t t49 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l3) + 0) == ((uint8_t)16ULL)));
        if (!t49) goto ie48;
        {
            void * t52 = gm_l0;
            void * t53 = gm_l0;
            uint64_t t54 = GM_LD(uint64_t, (((uint8_t *)t53) + 24));
            gm_b16_8 t55;
            GM_ST(void *, (uint8_t *)&t55, (void *)(gm_scvhpklutvpjg));
            GM_ST(uintptr_t, (uint8_t *)&t55 + 8, (uintptr_t)(3));
            gm_tf5srh4mp4b3i t56 = gm_f_second_word_end_dpnj7g8(t52, t54, t55);
            gm_tf5srh4mp4b3i t57 = t56;
            if (!((((uint8_t *)&t57))[8] != 0)) goto ie51;
            uint64_t t58 = GM_LD(uint64_t, ((uint8_t *)&t57));
            gm_l6 = t58;
            {
                gm_t9qkh0rsooc8d t60;
                GM_ST(uint8_t, ((uint8_t *)&t60 + 0), ((uint8_t)67ULL));
                gm_taari1s0m51a7 t61;
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t61), t60);
                ((uint8_t *)&t61)[1] = 1;
                gm_l4 = t61;
            }
            {
                uint64_t t62 = gm_l6;
                gm_l5 = t62;
            }
            goto ix51;
ie51:;
ix51:;
        }
        goto ix48;
ie48:;
        uint8_t t64 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l3) + 0) == ((uint8_t)18ULL)));
        if (!t64) goto ie63;
        {
            void * t67 = gm_l0;
            void * t68 = gm_l0;
            uint64_t t69 = GM_LD(uint64_t, (((uint8_t *)t68) + 24));
            gm_b16_8 t70;
            GM_ST(void *, (uint8_t *)&t70, (void *)(gm_svbhcvlahd4n2));
            GM_ST(uintptr_t, (uint8_t *)&t70 + 8, (uintptr_t)(2));
            gm_tf5srh4mp4b3i t71 = gm_f_second_word_end_dpnj7g8(t67, t69, t70);
            gm_tf5srh4mp4b3i t72 = t71;
            if (!((((uint8_t *)&t72))[8] != 0)) goto ie66;
            uint64_t t73 = GM_LD(uint64_t, ((uint8_t *)&t72));
            gm_l7 = t73;
            {
                gm_t9qkh0rsooc8d t75;
                GM_ST(uint8_t, ((uint8_t *)&t75 + 0), ((uint8_t)68ULL));
                gm_taari1s0m51a7 t76;
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t76), t75);
                ((uint8_t *)&t76)[1] = 1;
                gm_l4 = t76;
            }
            {
                uint64_t t77 = gm_l7;
                gm_l5 = t77;
            }
            goto ix66;
ie66:;
ix66:;
        }
        goto ix63;
ie63:;
ix63:;
ix48:;
    }
    {
        if (!((((uint8_t *)&gm_l4))[1] != 0)) goto ie78;
        gm_t9qkh0rsooc8d t79 = GM_LD(gm_t9qkh0rsooc8d, ((uint8_t *)&gm_l4));
        gm_l8 = t79;
        {
            void * t81 = gm_l0;
            uint64_t t82 = gm_l5;
            GM_ST(uint64_t, (((uint8_t *)t81) + 24), t82);
        }
        {
            void * t83 = gm_l0;
            gm_t9qkh0rsooc8d t84 = gm_l8;
            gm_t3fcbuehh3bs2 t85;
            GM_ST(uint8_t, (uint8_t *)&t85 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t85 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t85 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t85 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t85 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t85 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t85 + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&t85 + 24, 0);
            GM_ST(uint8_t, ((uint8_t *)&t85 + 0), ((uint8_t)8ULL));
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t85 + 8), t84);
            uint64_t t86 = gm_l1;
            uint64_t t87 = gm_l5;
            gm_b0_1 t88 = gm_f_emit_ekvv703((void *)NULL, cx, NULL, t83, &t85, t86, t87);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix78;
ie78:;
        {
            void * t90 = gm_l0;
            gm_t9qkh0rsooc8d t91 = gm_l3;
            gm_t3fcbuehh3bs2 t92;
            GM_ST(uint8_t, (uint8_t *)&t92 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t92 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t92 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t92 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t92 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t92 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t92 + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&t92 + 24, 0);
            GM_ST(uint8_t, ((uint8_t *)&t92 + 0), ((uint8_t)2ULL));
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t92 + 8), t91);
            uint64_t t93 = gm_l1;
            void * t94 = gm_l0;
            uint64_t t95 = GM_LD(uint64_t, (((uint8_t *)t94) + 24));
            gm_b0_1 t96 = gm_f_emit_ekvv703((void *)NULL, cx, NULL, t90, &t92, t93, t95);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
ix78:;
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
__attribute__((aligned(4))) uint8_t gm_f_line_binds_jm0pnul(gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_line_binds_jm0pnul F;
    gm_b16_8 gm_l0; /* rest */
    int32_t gm_l1; /* depth */
    uint64_t gm_l2; /* i */
    uint8_t gm_l3; /* c */
    GM_STACK_CHECK_SHALLOW(&F, 692u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_l1 = ((int32_t)0LL);
    }
    {
        gm_l2 = ((uint64_t)0ULL);
    }
    {
        for (;;) {
            uint64_t t3 = gm_l2;
            uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            uint8_t t5 = (t3 < t4);
            if (!t5) goto lb2;
            {
                uint64_t t8 = gm_l2;
                uint64_t t9 = (uintptr_t)t8;
                uint64_t t10 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                if (t9 >= (uintptr_t)(t10)) gm_trap_index(t9, t10, gm_srcvmmksi0829pa, 762, 14);
                uint8_t t11 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t9 * 1));
                gm_l3 = t11;
            }
            {
                uint8_t t12 = gm_l3;
                uint8_t t13 = t12;
                uint8_t t16 = GM_LD(uint8_t, ((uint8_t *)&t13));
                if (!(t16 == ((uint8_t)10ULL))) goto mn15;
                uint8_t t17 = 0;
                F.ret = ((uint8_t)0);
                goto gm_fret;
                goto me14;
mn15:;
                uint8_t t21 = GM_LD(uint8_t, ((uint8_t *)&t13));
                if (!(t21 == ((uint8_t)40ULL))) goto pa20;
                goto po19;
pa20:;
                uint8_t t23 = GM_LD(uint8_t, ((uint8_t *)&t13));
                if (!(t23 == ((uint8_t)91ULL))) goto pa22;
                goto po19;
pa22:;
                uint8_t t25 = GM_LD(uint8_t, ((uint8_t *)&t13));
                if (!(t25 == ((uint8_t)123ULL))) goto pa24;
                goto po19;
pa24:;
                goto mn18;
po19:;
                {
                    int32_t t27 = gm_l1;
                    int32_t r28; bool o29 = __builtin_add_overflow(t27, ((int32_t)1LL), &r28);
                    if (o29) gm_trap("integer overflow in `+`", gm_srcvmmksi0829pa, 765, 32);
                    int32_t t30 = r28;
                    gm_l1 = t30;
                }
                goto me14;
mn18:;
                uint8_t t34 = GM_LD(uint8_t, ((uint8_t *)&t13));
                if (!(t34 == ((uint8_t)41ULL))) goto pa33;
                goto po32;
pa33:;
                uint8_t t36 = GM_LD(uint8_t, ((uint8_t *)&t13));
                if (!(t36 == ((uint8_t)93ULL))) goto pa35;
                goto po32;
pa35:;
                uint8_t t38 = GM_LD(uint8_t, ((uint8_t *)&t13));
                if (!(t38 == ((uint8_t)125ULL))) goto pa37;
                goto po32;
pa37:;
                goto mn31;
po32:;
                {
                    int32_t t40 = gm_l1;
                    int32_t r41; bool o42 = __builtin_sub_overflow(t40, ((int32_t)1LL), &r41);
                    if (o42) gm_trap("integer overflow in `-`", gm_srcvmmksi0829pa, 767, 17);
                    int32_t t43 = r41;
                    gm_l1 = t43;
                }
                {
                    int32_t t45 = gm_l1;
                    uint8_t t46 = (t45 < ((int32_t)0LL));
                    if (!t46) goto ie44;
                    {
                        uint8_t t48 = 0;
                        F.ret = ((uint8_t)0);
                        goto gm_fret;
                    }
                    goto ix44;
ie44:;
ix44:;
                }
                goto me14;
mn31:;
                uint8_t t52 = GM_LD(uint8_t, ((uint8_t *)&t13));
                if (!(t52 == ((uint8_t)34ULL))) goto pa51;
                goto po50;
pa51:;
                uint8_t t54 = GM_LD(uint8_t, ((uint8_t *)&t13));
                if (!(t54 == ((uint8_t)39ULL))) goto pa53;
                goto po50;
pa53:;
                goto mn49;
po50:;
                {
                    uint64_t t56 = gm_l2;
                    uint64_t r57; bool o58 = __builtin_add_overflow(t56, ((uint64_t)1ULL), &r57);
                    if (o58) gm_trap("integer overflow in `+`", gm_srcvmmksi0829pa, 771, 17);
                    uint64_t t59 = r57;
                    gm_l2 = t59;
                }
                {
                    for (;;) {
                        uint64_t t61 = gm_l2;
                        uint64_t t62 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                        uint8_t t63 = (t61 < t62);
                        if (!t63) goto lb60;
                        uint64_t t64 = gm_l2;
                        uint64_t t65 = (uintptr_t)t64;
                        uint64_t t66 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                        if (t65 >= (uintptr_t)(t66)) gm_trap_index(t65, t66, gm_srcvmmksi0829pa, 772, 40);
                        uint8_t t67 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t65 * 1));
                        uint8_t t68 = gm_l3;
                        uint8_t t69 = (!(t67 == t68));
                        if (!t69) goto lb60;
                        uint64_t t70 = gm_l2;
                        uint64_t t71 = (uintptr_t)t70;
                        uint64_t t72 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                        if (t71 >= (uintptr_t)(t72)) gm_trap_index(t71, t72, gm_srcvmmksi0829pa, 772, 61);
                        uint8_t t73 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t71 * 1));
                        uint8_t t74 = (!(t73 == ((uint8_t)10ULL)));
                        if (!t74) goto lb60;
                        {
                            uint64_t t77 = gm_l2;
                            uint64_t t78;
                            uint64_t t80 = gm_l2;
                            uint64_t t81 = (uintptr_t)t80;
                            uint64_t t82 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                            if (t81 >= (uintptr_t)(t82)) gm_trap_index(t81, t82, gm_srcvmmksi0829pa, 773, 29);
                            uint8_t t83 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t81 * 1));
                            uint8_t t84 = ((t83 == ((uint8_t)92ULL)));
                            if (!t84) goto ie79;
                            uint64_t t86 = ((uint64_t)2ULL);
                            t78 = t86;
                            goto ix79;
ie79:;
                            uint64_t t88 = ((uint64_t)1ULL);
                            t78 = t88;
ix79:;
                            uint64_t r89; bool o90 = __builtin_add_overflow(t77, t78, &r89);
                            if (o90) gm_trap("integer overflow in `+`", gm_srcvmmksi0829pa, 773, 21);
                            uint64_t t91 = r89;
                            gm_l2 = t91;
                        }
lc60:;
                    }
lb60:;
                }
                goto me14;
mn49:;
                uint8_t t93 = GM_LD(uint8_t, ((uint8_t *)&t13));
                if (!(t93 == ((uint8_t)47ULL))) goto mn92;
                gm_b16_8 t94 = gm_l0;
                gm_b16_8 t95 = t94;
                void * t96 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t95)));
                uint64_t t97 = (uintptr_t)GM_LD(uintptr_t, ((uint8_t *)&t95) + 8);
                uint64_t t98 = gm_l2;
                uint64_t r99; bool o100 = __builtin_add_overflow(t98, ((uint64_t)1ULL), &r99);
                if (o100) gm_trap("integer overflow in `+`", gm_srcvmmksi0829pa, 776, 29);
                uint64_t t101 = r99;
                uint64_t t102 = (uintptr_t)t101;
                gm_tqs5p4n2tq19v t103;
                if (t102 < t97) {
                uint8_t t104 = GM_LD(uint8_t, ((uint8_t *)t96 + t102 * 1));
                gm_tqs5p4n2tq19v t105;
                GM_ST(uint8_t, ((uint8_t *)&t105), t104);
                ((uint8_t *)&t105)[1] = 1;
                    t103 = t105;
                } else {
                gm_tqs5p4n2tq19v t106;
                memset(&t106, 0, sizeof t106);
                    t103 = t106;
                }
                gm_tqs5p4n2tq19v t107;
                GM_ST(uint8_t, ((uint8_t *)&t107), ((uint8_t)47ULL));
                ((uint8_t *)&t107)[1] = 1;
                gm_tqs5p4n2tq19v t108 = t103;
                gm_tqs5p4n2tq19v t109 = t107;
                uint8_t t110 = (gm_eqb44cb3cf1n(((uint8_t *)&t108), ((uint8_t *)&t109)));
                if (!t110) goto mn92;
                uint8_t t111 = 0;
                F.ret = ((uint8_t)0);
                goto gm_fret;
                goto me14;
mn92:;
                uint8_t t113 = GM_LD(uint8_t, ((uint8_t *)&t13));
                if (!(t113 == ((uint8_t)47ULL))) goto mn112;
                gm_b16_8 t114 = gm_l0;
                gm_b16_8 t115 = t114;
                void * t116 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t115)));
                uint64_t t117 = (uintptr_t)GM_LD(uintptr_t, ((uint8_t *)&t115) + 8);
                uint64_t t118 = gm_l2;
                uint64_t r119; bool o120 = __builtin_add_overflow(t118, ((uint64_t)1ULL), &r119);
                if (o120) gm_trap("integer overflow in `+`", gm_srcvmmksi0829pa, 777, 29);
                uint64_t t121 = r119;
                uint64_t t122 = (uintptr_t)t121;
                gm_tqs5p4n2tq19v t123;
                if (t122 < t117) {
                uint8_t t124 = GM_LD(uint8_t, ((uint8_t *)t116 + t122 * 1));
                gm_tqs5p4n2tq19v t125;
                GM_ST(uint8_t, ((uint8_t *)&t125), t124);
                ((uint8_t *)&t125)[1] = 1;
                    t123 = t125;
                } else {
                gm_tqs5p4n2tq19v t126;
                memset(&t126, 0, sizeof t126);
                    t123 = t126;
                }
                gm_tqs5p4n2tq19v t127;
                GM_ST(uint8_t, ((uint8_t *)&t127), ((uint8_t)42ULL));
                ((uint8_t *)&t127)[1] = 1;
                gm_tqs5p4n2tq19v t128 = t123;
                gm_tqs5p4n2tq19v t129 = t127;
                uint8_t t130 = (gm_eqb44cb3cf1n(((uint8_t *)&t128), ((uint8_t *)&t129)));
                if (!t130) goto mn112;
                {
                    gm_b16_8 t132 = gm_l0;
                    uint64_t t133 = gm_l2;
                    gm_tf5srh4mp4b3i t134 = gm_f_inline_comment_end_8i5h1eq(t132, t133);
                    gm_tf5srh4mp4b3i t135 = t134;
                    uint64_t t136;
                    if (((((uint8_t *)&t135))[8] != 0)) {
                        uint64_t t137 = GM_LD(uint64_t, (((uint8_t *)&t135) + 0));
                        t136 = t137;
                    } else {
                        uint8_t t138 = 0;
                        F.ret = ((uint8_t)0);
                        goto gm_fret;
                    }
                    gm_l2 = t136;
                }
                {
                    goto lc2;
                }
                goto me14;
mn112:;
                uint8_t t140 = GM_LD(uint8_t, ((uint8_t *)&t13));
                if (!(t140 == ((uint8_t)96ULL))) goto mn139;
                {
                    gm_b16_8 t142 = gm_l0;
                    uint64_t t143 = gm_l2;
                    gm_tf5srh4mp4b3i t144 = gm_f_inline_raw_string_end_6lenf33(t142, t143);
                    gm_tf5srh4mp4b3i t145 = t144;
                    uint64_t t146;
                    if (((((uint8_t *)&t145))[8] != 0)) {
                        uint64_t t147 = GM_LD(uint64_t, (((uint8_t *)&t145) + 0));
                        t146 = t147;
                    } else {
                        uint8_t t148 = 0;
                        F.ret = ((uint8_t)0);
                        goto gm_fret;
                    }
                    gm_l2 = t146;
                }
                {
                    goto lc2;
                }
                goto me14;
mn139:;
                uint8_t t150 = GM_LD(uint8_t, ((uint8_t *)&t13));
                if (!(t150 == ((uint8_t)58ULL))) goto mn149;
                uint8_t t151 = 0;
                int32_t t152 = gm_l1;
                uint8_t t153 = ((t152 == ((int32_t)0LL)));
                if (t153) {
                    gm_b16_8 t154 = gm_l0;
                    gm_b16_8 t155 = t154;
                    void * t156 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t155)));
                    uint64_t t157 = (uintptr_t)GM_LD(uintptr_t, ((uint8_t *)&t155) + 8);
                    uint64_t t158 = gm_l2;
                    uint64_t r159; bool o160 = __builtin_add_overflow(t158, ((uint64_t)1ULL), &r159);
                    if (o160) gm_trap("integer overflow in `+`", gm_srcvmmksi0829pa, 785, 44);
                    uint64_t t161 = r159;
                    uint64_t t162 = (uintptr_t)t161;
                    gm_tqs5p4n2tq19v t163;
                    if (t162 < t157) {
                    uint8_t t164 = GM_LD(uint8_t, ((uint8_t *)t156 + t162 * 1));
                    gm_tqs5p4n2tq19v t165;
                    GM_ST(uint8_t, ((uint8_t *)&t165), t164);
                    ((uint8_t *)&t165)[1] = 1;
                        t163 = t165;
                    } else {
                    gm_tqs5p4n2tq19v t166;
                    memset(&t166, 0, sizeof t166);
                        t163 = t166;
                    }
                    gm_tqs5p4n2tq19v t167;
                    GM_ST(uint8_t, ((uint8_t *)&t167), ((uint8_t)61ULL));
                    ((uint8_t *)&t167)[1] = 1;
                    gm_tqs5p4n2tq19v t168 = t163;
                    gm_tqs5p4n2tq19v t169 = t167;
                    uint8_t t170 = (gm_eqb44cb3cf1n(((uint8_t *)&t168), ((uint8_t *)&t169)));
                    t151 = t170;
                } else t151 = 0;
                if (!t151) goto mn149;
                uint8_t t171 = 0;
                F.ret = ((uint8_t)1);
                goto gm_fret;
                goto me14;
mn149:;
                goto me14;
mn172:;
                gm_trap("no match arm matched the scrutinee", gm_srcvmmksi0829pa, 763, 9);
me14:;
            }
            {
                uint64_t t174 = gm_l2;
                uint64_t r175; bool o176 = __builtin_add_overflow(t174, ((uint64_t)1ULL), &r175);
                if (o176) gm_trap("integer overflow in `+`", gm_srcvmmksi0829pa, 788, 9);
                uint64_t t177 = r175;
                gm_l2 = t177;
            }
lc2:;
        }
lb2:;
    }
    {
        uint8_t t178 = 0;
        F.ret = ((uint8_t)0);
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_b0_1 gm_f_line_comment_s71u8hq(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_line_comment_s71u8hq F;
    void * gm_l0; /* lx */
    uint64_t gm_l1; /* lo */
    uint8_t gm_l2; /* is_doc */
    uint64_t gm_l3; /* i */
    uint8_t gm_l4; /* b */
    gm_b16_8 gm_l5; /* text */
    uint64_t gm_l6; /* next */
    GM_STACK_CHECK(&F, 673u);
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
        uint8_t t4 = 0;
        void * t5 = gm_l0;
        gm_tqs5p4n2tq19v t6 = gm_f_peek_at_74p8lkq(t5, ((uint64_t)2ULL));
        gm_tqs5p4n2tq19v t7;
        GM_ST(uint8_t, ((uint8_t *)&t7), ((uint8_t)47ULL));
        ((uint8_t *)&t7)[1] = 1;
        gm_tqs5p4n2tq19v t8 = t6;
        gm_tqs5p4n2tq19v t9 = t7;
        uint8_t t10 = (gm_eqb44cb3cf1n(((uint8_t *)&t8), ((uint8_t *)&t9)));
        if (t10) {
            void * t11 = gm_l0;
            gm_tqs5p4n2tq19v t12 = gm_f_peek_at_74p8lkq(t11, ((uint64_t)3ULL));
            gm_tqs5p4n2tq19v t13;
            GM_ST(uint8_t, ((uint8_t *)&t13), ((uint8_t)47ULL));
            ((uint8_t *)&t13)[1] = 1;
            gm_tqs5p4n2tq19v t14 = t12;
            gm_tqs5p4n2tq19v t15 = t13;
            uint8_t t16 = (!gm_eqb44cb3cf1n(((uint8_t *)&t14), ((uint8_t *)&t15)));
            t4 = t16;
        } else t4 = 0;
        gm_l2 = t4;
    }
    {
        uint64_t t17 = gm_l1;
        uint64_t r18; bool o19 = __builtin_add_overflow(t17, ((uint64_t)2ULL), &r18);
        if (o19) gm_trap("integer overflow in `+`", gm_srcvmmksi0829pa, 209, 10);
        uint64_t t20 = r18;
        gm_l3 = t20;
    }
    {
        for (;;) {
            void * t22 = gm_l0;
            uint64_t t23 = gm_l3;
            gm_tqs5p4n2tq19v t24 = gm_f_byte_b4v8vul(t22, t23);
            gm_tqs5p4n2tq19v t25 = t24;
            if (!((((uint8_t *)&t25))[1] != 0)) goto lb21;
            uint8_t t26 = GM_LD(uint8_t, ((uint8_t *)&t25));
            gm_l4 = t26;
            {
                uint8_t t30 = gm_l4;
                uint8_t t31 = ((t30 == ((uint8_t)10ULL)));
                if (!t31) goto ie29;
                {
                    goto lb21;
                }
                goto ix29;
ie29:;
ix29:;
            }
            {
                uint8_t t34 = gm_l4;
                uint8_t t35 = ((t34 == ((uint8_t)13ULL)));
                if (!t35) goto ie33;
                {
                    void * t38 = gm_l0;
                    uint64_t t39 = gm_l3;
                    uint64_t r40; bool o41 = __builtin_add_overflow(t39, ((uint64_t)1ULL), &r40);
                    if (o41) gm_trap("integer overflow in `+`", gm_srcvmmksi0829pa, 213, 24);
                    uint64_t t42 = r40;
                    gm_tqs5p4n2tq19v t43 = gm_f_byte_b4v8vul(t38, t42);
                    gm_tqs5p4n2tq19v t44;
                    GM_ST(uint8_t, ((uint8_t *)&t44), ((uint8_t)10ULL));
                    ((uint8_t *)&t44)[1] = 1;
                    gm_tqs5p4n2tq19v t45 = t43;
                    gm_tqs5p4n2tq19v t46 = t44;
                    uint8_t t47 = (gm_eqb44cb3cf1n(((uint8_t *)&t45), ((uint8_t *)&t46)));
                    if (!t47) goto ie37;
                    {
                        goto lb21;
                    }
                    goto ix37;
ie37:;
ix37:;
                }
                {
                    void * t49 = gm_l0;
                    uint64_t t50 = gm_l3;
                    uint64_t t51 = gm_l3;
                    uint64_t r52; bool o53 = __builtin_add_overflow(t51, ((uint64_t)1ULL), &r52);
                    if (o53) gm_trap("integer overflow in `+`", gm_srcvmmksi0829pa, 214, 25);
                    uint64_t t54 = r52;
                    gm_b16_8 t55 = GM_LD(gm_b16_8, gm_c644mb9o5bq);
                    gm_b0_1 t56 = gm_f_error_p5r0d56((void *)NULL, cx, NULL, t49, t50, t54, t55);
                    if (gm_unw.kind) {
                        goto gm_ll27;
                    }
                }
                goto ix33;
ie33:;
ix33:;
            }
            {
                uint64_t t57 = gm_l3;
                uint64_t r58; bool o59 = __builtin_add_overflow(t57, ((uint64_t)1ULL), &r58);
                if (o59) gm_trap("integer overflow in `+`", gm_srcvmmksi0829pa, 216, 9);
                uint64_t t60 = r58;
                gm_l3 = t60;
            }
lc21:;
        }
gm_ll27:;
        if (gm_unw.frame == &F.h && gm_unw.label == 0) {
            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb21; }
            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc21; }
        }
        goto gm_lad;
lb21:;
    }
    {
        uint8_t t62 = gm_l2;
        if (!t62) goto ie61;
        {
            void * t64 = gm_l0;
            uint64_t t65 = GM_LD(uintptr_t, (((uint8_t *)t64) + 8) + 8);
            void * t66 = ((uint8_t *)GM_LD(void *, (((uint8_t *)t64) + 8)));
            uint64_t t67 = gm_l1;
            uint64_t r68; bool o69 = __builtin_add_overflow(t67, ((uint64_t)3ULL), &r68);
            if (o69) gm_trap("integer overflow in `+`", gm_srcvmmksi0829pa, 219, 24);
            uint64_t t70 = r68;
            uint64_t t71 = (uintptr_t)t70;
            uint64_t t72 = gm_l3;
            uint64_t t73 = (uintptr_t)t72;
            if (t71 > t73 || t73 > (uintptr_t)t65) gm_trap_slice(t71, t73, t65, gm_srcvmmksi0829pa, 219, 17);
            gm_b16_8 t74;
            GM_ST(void *, (uint8_t *)&t74, (void *)(((uint8_t *)t66 + t71 * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t74 + 8, (uintptr_t)(t73 - t71));
            gm_l5 = t74;
        }
        {
            gm_b16_8 t76 = gm_l5;
            gm_b16_8 t77;
            GM_ST(void *, (uint8_t *)&t77, (void *)(gm_s3p0rbs9hc3lg));
            GM_ST(uintptr_t, (uint8_t *)&t77 + 8, (uintptr_t)(1));
            uint8_t t78 = gm_f_starts_with_6vvt01e(t76, t77);
            if (!t78) goto ie75;
            {
                uint64_t t80 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                void * t81 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l5)));
                uint64_t t82 = (uintptr_t)((uint64_t)1ULL);
                if (t82 > (uintptr_t)t80 || (uintptr_t)t80 > (uintptr_t)t80) gm_trap_slice(t82, (uintptr_t)t80, t80, gm_srcvmmksi0829pa, 220, 43);
                gm_b16_8 t83;
                GM_ST(void *, (uint8_t *)&t83, (void *)(((uint8_t *)t81 + t82 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t83 + 8, (uintptr_t)((uintptr_t)t80 - t82));
                gm_l5 = t83;
            }
            goto ix75;
ie75:;
ix75:;
        }
        {
            uint64_t t84 = gm_l3;
            gm_l6 = t84;
        }
        {
            void * t86 = gm_l0;
            uint64_t t87 = gm_l6;
            gm_tqs5p4n2tq19v t88 = gm_f_byte_b4v8vul(t86, t87);
            gm_tqs5p4n2tq19v t89;
            GM_ST(uint8_t, ((uint8_t *)&t89), ((uint8_t)13ULL));
            ((uint8_t *)&t89)[1] = 1;
            gm_tqs5p4n2tq19v t90 = t88;
            gm_tqs5p4n2tq19v t91 = t89;
            uint8_t t92 = (gm_eqb44cb3cf1n(((uint8_t *)&t90), ((uint8_t *)&t91)));
            if (!t92) goto ie85;
            {
                uint64_t t94 = gm_l6;
                uint64_t r95; bool o96 = __builtin_add_overflow(t94, ((uint64_t)1ULL), &r95);
                if (o96) gm_trap("integer overflow in `+`", gm_srcvmmksi0829pa, 222, 36);
                uint64_t t97 = r95;
                gm_l6 = t97;
            }
            goto ix85;
ie85:;
ix85:;
        }
        {
            void * t99 = gm_l0;
            uint64_t t100 = gm_l6;
            gm_tqs5p4n2tq19v t101 = gm_f_byte_b4v8vul(t99, t100);
            gm_tqs5p4n2tq19v t102;
            GM_ST(uint8_t, ((uint8_t *)&t102), ((uint8_t)10ULL));
            ((uint8_t *)&t102)[1] = 1;
            gm_tqs5p4n2tq19v t103 = t101;
            gm_tqs5p4n2tq19v t104 = t102;
            uint8_t t105 = (gm_eqb44cb3cf1n(((uint8_t *)&t103), ((uint8_t *)&t104)));
            if (!t105) goto ie98;
            {
                uint64_t t107 = gm_l6;
                uint64_t r108; bool o109 = __builtin_add_overflow(t107, ((uint64_t)1ULL), &r108);
                if (o109) gm_trap("integer overflow in `+`", gm_srcvmmksi0829pa, 224, 13);
                uint64_t t110 = r108;
                gm_l6 = t110;
            }
            {
                for (;;) {
                    void * t112 = gm_l0;
                    uint64_t t113 = gm_l6;
                    gm_tqs5p4n2tq19v t114 = gm_f_byte_b4v8vul(t112, t113);
                    gm_tqs5p4n2tq19v t115;
                    GM_ST(uint8_t, ((uint8_t *)&t115), ((uint8_t)32ULL));
                    ((uint8_t *)&t115)[1] = 1;
                    gm_tqs5p4n2tq19v t116;
                    GM_ST(uint8_t, ((uint8_t *)&t116), ((uint8_t)9ULL));
                    ((uint8_t *)&t116)[1] = 1;
                    gm_t5r6k0fdcfsb3 t117;
                    GM_ST(gm_tqs5p4n2tq19v, ((uint8_t *)&t117 + 0), t115);
                    GM_ST(gm_tqs5p4n2tq19v, ((uint8_t *)&t117 + 2), t116);
                    gm_t5r6k0fdcfsb3 t118 = t117;
                    uint8_t t119 = 0;
                    gm_tqs5p4n2tq19v t120 = t114;
                    if (gm_eqb44cb3cf1n(((uint8_t *)&t118) + 0, ((uint8_t *)&t120))) t119 = 1;
                    if (gm_eqb44cb3cf1n(((uint8_t *)&t118) + 2, ((uint8_t *)&t120))) t119 = 1;
                    uint8_t t121 = (uint8_t)(t119);
                    if (!t121) goto lb111;
                    {
                        uint64_t t124 = gm_l6;
                        uint64_t r125; bool o126 = __builtin_add_overflow(t124, ((uint64_t)1ULL), &r125);
                        if (o126) gm_trap("integer overflow in `+`", gm_srcvmmksi0829pa, 225, 50);
                        uint64_t t127 = r125;
                        gm_l6 = t127;
                    }
lc111:;
                }
lb111:;
            }
            goto ix98;
ie98:;
ix98:;
        }
        {
            void * t128 = gm_l0;
            void * t129 = (void *)(((uint8_t *)t128) + 80);
            gm_t3jim1gvsdr87 t130;
            memset((uint8_t *)&t130 + 12, 0, 4);
            memset((uint8_t *)&t130 + 36, 0, 4);
            void * t131 = gm_l0;
            uint64_t t132 = gm_l1;
            uint64_t t133 = gm_l3;
            gm_tl874t2atnlh3 t134 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_tg7cev6(t131, t132, t133));
            GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t130 + 0, t134);
            gm_b16_8 t135 = gm_l5;
            GM_ST(gm_b16_8, (uint8_t *)&t130 + 16, t135);
            uint64_t t136 = gm_l6;
            uint32_t t137 = (uint32_t)t136;
            GM_ST(uint32_t, (uint8_t *)&t130 + 32, t137);
            gm_tftb1odc739t5 t138 = gm_f_push_o6f0em6(t129, &t130);
            gm_tftb1odc739t5 t139 = t138;
            if (((((uint8_t *)&t139))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t139) + 1, gm_srcvmmksi0829pa, 227, 9);
            gm_b0_1 t140 = GM_LD(gm_b0_1, (((uint8_t *)&t139) + 1));
        }
        goto ix61;
ie61:;
ix61:;
    }
    {
        void * t141 = gm_l0;
        uint64_t t142 = gm_l3;
        GM_ST(uint64_t, (((uint8_t *)t141) + 24), t142);
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
extern __attribute__((aligned(4))) gm_b0_1 gm_f_lone_cr_ktb0jvm(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) void gm_f_new_lexer_dm12g12(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint32_t a0, gm_b16_8 a1, void * a2) {
    struct gm_fr_new_lexer_dm12g12 F;
    uint32_t gm_l0; /* file */
    gm_b16_8 gm_l1; /* src */
    void * gm_l2; /* diags */
    GM_STACK_CHECK(&F, 296u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        memset((uint8_t *)&(*(gm_trd6bg8is1v4n *)gm_out) + 4, 0, 4);
        memset((uint8_t *)&(*(gm_trd6bg8is1v4n *)gm_out) + 281, 0, 7);
        uint32_t t2 = gm_l0;
        gm_b16_8 t3 = gm_l1;
        void * t4 = gm_l2;
        gm_trqfvmjtt4sij t5;
        gm_f_list_lpokg1m((void *)&t5, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_trqfvmjtt4sij t6;
        gm_f_list_5o6u5a9((void *)&t6, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_trqfvmjtt4sij t7;
        gm_f_list_2iogcnb((void *)&t7, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tc0nbpdhofi37 t8;
        for (uint64_t i = 0; i < 3; i++) GM_ST(uint64_t, (uint8_t *)&t8 + i * 8, ((uint64_t)0ULL));
        gm_trqfvmjtt4sij t9;
        gm_f_list_5dhgg4i((void *)&t9, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_toceffb697p0u t10;
        memset(&t10, 0, sizeof t10);
        gm_trqfvmjtt4sij t11;
        gm_f_list_go2qg0b((void *)&t11, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(uint32_t, (uint8_t *)&(*(gm_trd6bg8is1v4n *)gm_out) + 0, t2);
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_trd6bg8is1v4n *)gm_out) + 8, t3);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_trd6bg8is1v4n *)gm_out) + 24, ((uint64_t)0ULL));
        GM_ST(void *, (uint8_t *)&(*(gm_trd6bg8is1v4n *)gm_out) + 32, t4);
        GM_ST(gm_trqfvmjtt4sij, (uint8_t *)&(*(gm_trd6bg8is1v4n *)gm_out) + 40, t5);
        GM_ST(gm_trqfvmjtt4sij, (uint8_t *)&(*(gm_trd6bg8is1v4n *)gm_out) + 80, t6);
        GM_ST(gm_trqfvmjtt4sij, (uint8_t *)&(*(gm_trd6bg8is1v4n *)gm_out) + 120, t7);
        GM_ST(gm_tc0nbpdhofi37, (uint8_t *)&(*(gm_trd6bg8is1v4n *)gm_out) + 160, t8);
        GM_ST(gm_trqfvmjtt4sij, (uint8_t *)&(*(gm_trd6bg8is1v4n *)gm_out) + 184, t9);
        GM_ST(gm_toceffb697p0u, (uint8_t *)&(*(gm_trd6bg8is1v4n *)gm_out) + 224, t10);
        GM_ST(gm_trqfvmjtt4sij, (uint8_t *)&(*(gm_trd6bg8is1v4n *)gm_out) + 240, t11);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_trd6bg8is1v4n *)gm_out) + 280, ((uint8_t)0));
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
extern __attribute__((aligned(4))) void gm_f_note_newline_cchbr7g(void * a0, uint64_t a1, uint64_t a2);
extern __attribute__((aligned(4))) gm_te0mgnjdphs16 gm_f_one_or_two_lava5tu(uint8_t a0, gm_t9qkh0rsooc8d a1, gm_t9qkh0rsooc8d a2);
inline __attribute__((aligned(4))) void gm_f_open_bracket_3kcj4k8(void * a0, gm_t9qkh0rsooc8d a1, gm_cseahvg8v7p0vs a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_open_bracket_3kcj4k8 F;
    void * gm_l0; /* lx */
    gm_t9qkh0rsooc8d gm_l1; /* kind */
    gm_tl874t2atnlh3 gm_l2; /* at */
    void * gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 143u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = GM_CONV(gm_tl874t2atnlh3, a2);
    {
        {
            void * t3 = gm_l0;
            uint8_t *t4 = (uint8_t *)((((uint8_t *)t3) + 160));
            gm_t9qkh0rsooc8d t5 = gm_l1;
            uint64_t t6 = gm_f_slot_4oe07nr(t5);
            uint64_t t7 = (uintptr_t)t6;
            if (t7 >= (uintptr_t)(3)) gm_trap_index(t7, 3, gm_srcvmmksi0829pa, 624, 5);
            void * t8 = (void *)(t4 + t7 * 8);
            gm_l3 = t8;
        }
        {
            void * t9 = gm_l3;
            void * t10 = gm_l3;
            uint64_t t11 = GM_LD(uint64_t, ((uint8_t *)t10));
            uint64_t r12; bool o13 = __builtin_add_overflow(t11, ((uint64_t)1ULL), &r12);
            if (o13) gm_trap("integer overflow in `+`", gm_srcvmmksi0829pa, 624, 5);
            uint64_t t14 = r12;
            GM_ST(uint64_t, ((uint8_t *)t9), t14);
        }
    }
    {
        void * t15 = gm_l0;
        void * t16 = (void *)(((uint8_t *)t15) + 120);
        gm_t9qkh0rsooc8d t17 = gm_l1;
        gm_tl874t2atnlh3 t18 = gm_l2;
        gm_t2eb59dvglnle t19;
        GM_ST(uint8_t, (uint8_t *)&t19 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t19 + 2, 0);
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t19 + 0), t17);
        GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t19 + 4), t18);
        gm_tftb1odc739t5 t20 = gm_f_push_vs3f95a(t16, t19);
        gm_tftb1odc739t5 t21 = t20;
        if (((((uint8_t *)&t21))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t21) + 1, gm_srcvmmksi0829pa, 625, 5);
        gm_b0_1 t22 = GM_LD(gm_b0_1, (((uint8_t *)&t21) + 1));
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) gm_t9qkh0rsooc8d gm_f_opener_8hqhoc3(gm_t9qkh0rsooc8d a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_opener_8hqhoc3 F;
    gm_t9qkh0rsooc8d gm_l0; /* k */
    GM_STACK_CHECK_SHALLOW(&F, 12u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_t9qkh0rsooc8d t1;
    uint8_t t7 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    switch (t7) { case ((uint8_t)0ULL): goto ma3; case ((uint8_t)1ULL): goto ma4; case ((uint8_t)2ULL): goto ma5; case ((uint8_t)3ULL): goto ma5; default: goto ma6; }
ma3:;
    uint8_t t9 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t9 != ((uint8_t)0ULL)) goto mn8;
    gm_t9qkh0rsooc8d t10;
    GM_ST(uint8_t, ((uint8_t *)&t10 + 0), ((uint8_t)61ULL));
    t1 = t10;
    goto me2;
mn8:;
ma4:;
    uint8_t t12 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t12 != ((uint8_t)1ULL)) goto mn11;
    gm_t9qkh0rsooc8d t13;
    GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)63ULL));
    t1 = t13;
    goto me2;
mn11:;
ma5:;
    uint8_t t17 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t17 != ((uint8_t)2ULL)) goto pa16;
    goto po15;
pa16:;
    uint8_t t19 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t19 != ((uint8_t)3ULL)) goto pa18;
    goto po15;
pa18:;
    goto mn14;
po15:;
    gm_t9qkh0rsooc8d t20;
    GM_ST(uint8_t, ((uint8_t *)&t20 + 0), ((uint8_t)65ULL));
    t1 = t20;
    goto me2;
mn14:;
ma6:;
    gm_trap("no match arm matched the scrutinee", gm_srcvmmksi0829pa, 47, 42);
me2:;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) gm_tqs5p4n2tq19v gm_f_peek_72bostn(void * a0);
extern __attribute__((aligned(4))) gm_tqs5p4n2tq19v gm_f_peek_at_74p8lkq(void * a0, uint64_t a1);
inline __attribute__((aligned(4))) void gm_f_push_newline_ke0tojq(void * a0, gm_cseahvg8v7p0vs a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_newline_ke0tojq F;
    void * gm_l0; /* lx */
    gm_tl874t2atnlh3 gm_l1; /* at */
    uint64_t gm_l2; /* n */
    GM_STACK_CHECK(&F, 206u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_tl874t2atnlh3, a1);
    {
        void * t2 = gm_l0;
        uint64_t t3 = GM_LD(uintptr_t, ((((uint8_t *)t2) + 40) + 0) + 8);
        gm_l2 = t3;
    }
    {
        uint64_t t5 = gm_l2;
        uint8_t t6 = (t5 > ((uint64_t)0ULL));
        if (!t6) goto ie4;
        uint64_t t7 = gm_l2;
        uint64_t r8; bool o9 = __builtin_sub_overflow(t7, ((uint64_t)1ULL), &r8);
        if (o9) gm_trap("integer overflow in `-`", gm_srcvmmksi0829pa, 660, 34);
        uint64_t t10 = r8;
        uint64_t t11 = (uintptr_t)t10;
        void * t12 = gm_l0;
        uint64_t t13 = GM_LD(uintptr_t, ((((uint8_t *)t12) + 40) + 0) + 8);
        if (t11 >= (uintptr_t)(t13)) gm_trap_index(t11, t13, gm_srcvmmksi0829pa, 660, 18);
        uint8_t t14 = (!(GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t12) + 40) + 0)) + t11 * 48) + 0) + 0) == ((uint8_t)9ULL)));
        if (!t14) goto ie4;
        {
            void * t16 = gm_l0;
            void * t17 = (void *)(((uint8_t *)t16) + 40);
            gm_tm4pc0emhrv1g t18;
            memset((uint8_t *)&t18 + 44, 0, 4);
            gm_t3fcbuehh3bs2 t19;
            GM_ST(uint8_t, (uint8_t *)&t19 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t19 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t19 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t19 + 8, 0);
            GM_ST(uint64_t, (uint8_t *)&t19 + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&t19 + 24, 0);
            GM_ST(uint8_t, ((uint8_t *)&t19 + 0), ((uint8_t)9ULL));
            GM_ST(gm_t3fcbuehh3bs2, (uint8_t *)&t18 + 0, t19);
            gm_tl874t2atnlh3 t20 = gm_l1;
            GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t18 + 32, t20);
            gm_tftb1odc739t5 t21 = gm_f_push_pnf14h1(t17, &t18);
            gm_tftb1odc739t5 t22 = t21;
            if (((((uint8_t *)&t22))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t22) + 1, gm_srcvmmksi0829pa, 661, 9);
            gm_b0_1 t23 = GM_LD(gm_b0_1, (((uint8_t *)&t22) + 1));
        }
        goto ix4;
ie4:;
ix4:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) void gm_f_run_v26is74(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_run_v26is74 F;
    void * gm_l0; /* lx */
    uint8_t gm_l1; /* b */
    uint64_t gm_l2; /* end */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    gm_t2eb59dvglnle gm_l5; /* b */
    GM_STACK_CHECK(&F, 788u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t3 = gm_l0;
        gm_b16_8 t4 = GM_LD(gm_b16_8, (((uint8_t *)t3) + 8));
        gm_b16_8 t5;
        GM_ST(void *, (uint8_t *)&t5, (void *)(gm_srh29phg8b30r));
        GM_ST(uintptr_t, (uint8_t *)&t5 + 8, (uintptr_t)(3));
        uint8_t t6 = gm_f_starts_with_6vvt01e(t4, t5);
        if (!t6) goto ie2;
        {
            void * t8 = gm_l0;
            GM_ST(uint64_t, (((uint8_t *)t8) + 24), ((uint64_t)3ULL));
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t9 = gm_l0;
        void * t10 = (void *)(((uint8_t *)t9) + 40);
        void * t11 = gm_l0;
        uint64_t t12 = GM_LD(uintptr_t, (((uint8_t *)t11) + 8) + 8);
        if (((uint64_t)4ULL) == 0) gm_trap("division by zero", gm_srcvmmksi0829pa, 128, 23);
        uint64_t t13 = (uint64_t)(t12 / ((uint64_t)4ULL));
        uint64_t r14; bool o15 = __builtin_add_overflow(t13, ((uint64_t)16ULL), &r14);
        if (o15) gm_trap("integer overflow in `+`", gm_srcvmmksi0829pa, 128, 23);
        uint64_t t16 = r14;
        gm_tftb1odc739t5 t17 = gm_f_reserve_i65s8oj(t10, t16);
        gm_tftb1odc739t5 t18 = t17;
        if (((((uint8_t *)&t18))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t18) + 1, gm_srcvmmksi0829pa, 128, 5);
        gm_b0_1 t19 = GM_LD(gm_b0_1, (((uint8_t *)&t18) + 1));
    }
    {
        for (;;) {
            if (!((uint8_t)1)) goto lb20;
            {
                void * t23 = gm_l0;
                gm_b0_1 t24 = gm_f_skip_trivia_014s7ai((void *)NULL, cx, NULL, t23);
                if (gm_unw.kind) {
                    goto gm_ll21;
                }
            }
            {
                void * t25 = gm_l0;
                gm_tqs5p4n2tq19v t26 = gm_f_peek_72bostn(t25);
                gm_tqs5p4n2tq19v t27 = t26;
                uint8_t t28;
                if (((((uint8_t *)&t27))[1] != 0)) {
                    uint8_t t29 = GM_LD(uint8_t, (((uint8_t *)&t27) + 0));
                    t28 = t29;
                } else {
                    goto lb20;
                }
                gm_l1 = t28;
            }
            {
                void * t30 = gm_l0;
                uint8_t t31 = gm_l1;
                gm_b0_1 t32 = gm_f_lex_token_h8g44s4((void *)NULL, cx, NULL, t30, t31);
                if (gm_unw.kind) {
                    goto gm_ll21;
                }
            }
lc20:;
        }
gm_ll21:;
        if (gm_unw.frame == &F.h && gm_unw.label == 0) {
            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb20; }
            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc20; }
        }
        goto gm_lad;
lb20:;
    }
    {
        void * t33 = gm_l0;
        uint64_t t34 = GM_LD(uintptr_t, (((uint8_t *)t33) + 8) + 8);
        gm_l2 = t34;
    }
    {
        void * t35 = gm_l0;
        gm_t3fcbuehh3bs2 t36;
        GM_ST(uint8_t, (uint8_t *)&t36 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t36 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t36 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t36 + 8, 0);
        GM_ST(uint64_t, (uint8_t *)&t36 + 16, 0);
        GM_ST(uint64_t, (uint8_t *)&t36 + 24, 0);
        GM_ST(uint8_t, ((uint8_t *)&t36 + 0), ((uint8_t)10ULL));
        uint64_t t37 = gm_l2;
        gm_f_flush_newline_9023ml8(t35, &t36, t37);
    }
    {
        {
            void * t39 = gm_l0;
            gm_b16_8 t40 = GM_LD(gm_b16_8, ((((uint8_t *)t39) + 120) + 0));
            gm_l3 = t40;
        }
        {
            gm_l4 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t42 = gm_l4;
                uint64_t t43 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                uint8_t t44 = (t42 < t43);
                if (!t44) goto lb41;
                {
                    uint64_t t47 = gm_l4;
                    uint64_t t48 = (uintptr_t)t47;
                    uint64_t t49 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                    gm_t2eb59dvglnle t52 = GM_LD(gm_t2eb59dvglnle, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t48 * 16));
                    gm_l5 = t52;
                    {
                        void * t54 = gm_l0;
                        void * t55 = GM_LD(void *, (((uint8_t *)t54) + 32));
                        gm_tl874t2atnlh3 t56 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l5) + 4));
                        gm_t9qkh0rsooc8d t57 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l5) + 0));
                        gm_t9qkh0rsooc8d t58 = gm_f_opener_8hqhoc3(t57);
                        gm_b16_8 t59 = gm_f_punct_text_ov4r40u(t58);
                        gm_tnpifv20irnph t60;
                        GM_ST(gm_b16_8, ((uint8_t *)&t60 + 0), t59);
                        gm_tabb710d9mq2e t61;
                        gm_f_format_t32jdqr((void *)&t61, (void *)NULL, cx, NULL, t60);
                        if (gm_unw.kind) {
                            goto gm_ll45;
                        }
                        gm_tabb710d9mq2e t62 = t61;
                        if (((((uint8_t *)&t62))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t62) + 8, gm_srcvmmksi0829pa, 137, 33);
                        gm_b16_8 t63 = GM_LD(gm_b16_8, (((uint8_t *)&t62) + 8));
                        gm_b16_8 t64 = t63;
                        gm_b16_8 t65 = GM_LD(gm_b16_8, ((uint8_t *)&t64));
                        gm_b0_1 t66 = gm_f_report_eardf07((void *)NULL, cx, NULL, t55, t56, t65);
                        if (gm_unw.kind) {
                            goto gm_ll45;
                        }
                    }
                    goto me50;
mn51:;
                    gm_trap("no match arm matched the scrutinee", gm_srcvmmksi0829pa, 136, 5);
me50:;
                }
lc41:;
                {
                    uint64_t t68 = gm_l4;
                    uint64_t r69; bool o70 = __builtin_add_overflow(t68, ((uint64_t)1ULL), &r69);
                    uint64_t t71 = r69;
                    gm_l4 = t71;
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
        void * t72 = gm_l0;
        void * t73 = (void *)(((uint8_t *)t72) + 40);
        gm_tm4pc0emhrv1g t74;
        memset((uint8_t *)&t74 + 44, 0, 4);
        gm_t3fcbuehh3bs2 t75;
        GM_ST(uint8_t, (uint8_t *)&t75 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t75 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t75 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t75 + 8, 0);
        GM_ST(uint64_t, (uint8_t *)&t75 + 16, 0);
        GM_ST(uint64_t, (uint8_t *)&t75 + 24, 0);
        GM_ST(uint8_t, ((uint8_t *)&t75 + 0), ((uint8_t)10ULL));
        GM_ST(gm_t3fcbuehh3bs2, (uint8_t *)&t74 + 0, t75);
        void * t76 = gm_l0;
        uint64_t t77 = gm_l2;
        uint64_t t78 = gm_l2;
        gm_tl874t2atnlh3 t79 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_tg7cev6(t76, t77, t78));
        GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t74 + 32, t79);
        gm_tftb1odc739t5 t80 = gm_f_push_pnf14h1(t73, &t74);
        gm_tftb1odc739t5 t81 = t80;
        if (((((uint8_t *)&t81))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t81) + 1, gm_srcvmmksi0829pa, 139, 5);
        gm_b0_1 t82 = GM_LD(gm_b0_1, (((uint8_t *)&t81) + 1));
    }
    {
        void * t83 = gm_l0;
        gm_trqfvmjtt4sij t84 = GM_LD(gm_trqfvmjtt4sij, (((uint8_t *)t83) + 40));
        void * t85 = gm_l0;
        gm_trqfvmjtt4sij t86 = GM_LD(gm_trqfvmjtt4sij, (((uint8_t *)t85) + 80));
        void * t87 = gm_l0;
        gm_trqfvmjtt4sij t88 = GM_LD(gm_trqfvmjtt4sij, (((uint8_t *)t87) + 240));
        GM_ST(gm_trqfvmjtt4sij, (uint8_t *)&(*(gm_tr8dvlu9ik4gj *)gm_out) + 0, t84);
        GM_ST(gm_trqfvmjtt4sij, (uint8_t *)&(*(gm_tr8dvlu9ik4gj *)gm_out) + 40, t86);
        GM_ST(gm_trqfvmjtt4sij, (uint8_t *)&(*(gm_tr8dvlu9ik4gj *)gm_out) + 80, t88);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_scan_ident_h1b2n4u(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_scan_ident_h1b2n4u F;
    void * gm_l0; /* lx */
    uint64_t gm_l1; /* start */
    uint8_t gm_l2; /* c */
    GM_STACK_CHECK(&F, 194u);
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
            gm_tqs5p4n2tq19v t6 = gm_f_peek_72bostn(t5);
            gm_tqs5p4n2tq19v t7 = t6;
            if (!((((uint8_t *)&t7))[1] != 0)) goto lb4;
            uint8_t t8 = GM_LD(uint8_t, ((uint8_t *)&t7));
            gm_l2 = t8;
            {
                uint8_t t12 = gm_l2;
                uint8_t t13 = gm_f_is_ident_cont_317fpdb(t12);
                uint8_t t14 = (uint8_t)!t13;
                if (!t14) goto ie11;
                {
                    goto lb4;
                }
                goto ix11;
ie11:;
ix11:;
            }
            {
                void * t16 = gm_l0;
                void * t17 = gm_l0;
                uint64_t t18 = GM_LD(uint64_t, (((uint8_t *)t17) + 24));
                uint64_t r19; bool o20 = __builtin_add_overflow(t18, ((uint64_t)1ULL), &r19);
                if (o20) gm_trap("integer overflow in `+`", gm_srcvmmksi0829pa, 355, 9);
                uint64_t t21 = r19;
                GM_ST(uint64_t, (((uint8_t *)t16) + 24), t21);
            }
lc4:;
        }
lb4:;
    }
    {
        void * t22 = gm_l0;
        uint64_t t23 = GM_LD(uintptr_t, (((uint8_t *)t22) + 8) + 8);
        void * t24 = ((uint8_t *)GM_LD(void *, (((uint8_t *)t22) + 8)));
        uint64_t t25 = gm_l1;
        uint64_t t26 = (uintptr_t)t25;
        void * t27 = gm_l0;
        uint64_t t28 = GM_LD(uint64_t, (((uint8_t *)t27) + 24));
        uint64_t t29 = (uintptr_t)t28;
        if (t26 > t29 || t29 > (uintptr_t)t23) gm_trap_slice(t26, t29, t23, gm_srcvmmksi0829pa, 357, 12);
        gm_b16_8 t30;
        GM_ST(void *, (uint8_t *)&t30, (void *)(((uint8_t *)t24 + t26 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t30 + 8, (uintptr_t)(t29 - t26));
        uint8_t t31 = 0;
        F.ret = t30;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_taari1s0m51a7 gm_f_scan_punct_ecrh51c(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_scan_punct_ecrh51c F;
    void * gm_l0; /* lx */
    uint8_t gm_l1; /* first */
    uint8_t gm_l2; /* a1 */
    uint8_t gm_l3; /* a2 */
    uint8_t gm_l4; /* a3 */
    gm_t9qkh0rsooc8d gm_l5; /* p */
    uint64_t gm_l6; /* len */
    gm_t6ofeqo2h242p gm_l7; /* fl */
    GM_STACK_CHECK_SHALLOW(&F, 1287u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        uint64_t t3 = GM_LD(uint64_t, (((uint8_t *)t2) + 24));
        uint64_t t4 = (uintptr_t)t3;
        void * t5 = gm_l0;
        uint64_t t6 = GM_LD(uintptr_t, (((uint8_t *)t5) + 8) + 8);
        if (t4 >= (uintptr_t)(t6)) gm_trap_index(t4, t6, gm_srcvmmksi0829pa, 427, 14);
        uint8_t t7 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)t5) + 8)) + t4 * 1));
        gm_l1 = t7;
    }
    {
        void * t8 = gm_l0;
        gm_tqs5p4n2tq19v t9 = gm_f_peek_at_74p8lkq(t8, ((uint64_t)1ULL));
        gm_tqs5p4n2tq19v t10 = t9;
        uint8_t t11;
        if (((((uint8_t *)&t10))[1] != 0)) {
            uint8_t t12 = GM_LD(uint8_t, (((uint8_t *)&t10) + 0));
            t11 = t12;
        } else {
            t11 = ((uint8_t)0ULL);
        }
        gm_l2 = t11;
    }
    {
        void * t13 = gm_l0;
        gm_tqs5p4n2tq19v t14 = gm_f_peek_at_74p8lkq(t13, ((uint64_t)2ULL));
        gm_tqs5p4n2tq19v t15 = t14;
        uint8_t t16;
        if (((((uint8_t *)&t15))[1] != 0)) {
            uint8_t t17 = GM_LD(uint8_t, (((uint8_t *)&t15) + 0));
            t16 = t17;
        } else {
            t16 = ((uint8_t)0ULL);
        }
        gm_l3 = t16;
    }
    {
        void * t18 = gm_l0;
        gm_tqs5p4n2tq19v t19 = gm_f_peek_at_74p8lkq(t18, ((uint64_t)3ULL));
        gm_tqs5p4n2tq19v t20 = t19;
        uint8_t t21;
        if (((((uint8_t *)&t20))[1] != 0)) {
            uint8_t t22 = GM_LD(uint8_t, (((uint8_t *)&t20) + 0));
            t21 = t22;
        } else {
            t21 = ((uint8_t)0ULL);
        }
        gm_l4 = t21;
    }
    {
        gm_t9qkh0rsooc8d t23;
        GM_ST(uint8_t, ((uint8_t *)&t23 + 0), ((uint8_t)0ULL));
        gm_l5 = t23;
    }
    {
        gm_l6 = ((uint64_t)1ULL);
    }
    {
        uint8_t t24 = gm_l1;
        uint8_t t25 = t24;
        uint8_t t30 = GM_LD(uint8_t, ((uint8_t *)&t25));
        if (!(t30 == ((uint8_t)43ULL))) goto pa29;
        goto po28;
pa29:;
        uint8_t t32 = GM_LD(uint8_t, ((uint8_t *)&t25));
        if (!(t32 == ((uint8_t)45ULL))) goto pa31;
        goto po28;
pa31:;
        uint8_t t34 = GM_LD(uint8_t, ((uint8_t *)&t25));
        if (!(t34 == ((uint8_t)42ULL))) goto pa33;
        goto po28;
pa33:;
        goto mn27;
po28:;
        {
            uint8_t t36 = gm_l1;
            gm_t6ofeqo2h242p t37 = gm_f_flavors_qsu4oq0(t36);
            gm_l7 = t37;
        }
        {
            uint8_t t39 = gm_l1;
            uint8_t t40 = ((t39 == ((uint8_t)45ULL)));
            if (!t40) goto ie38;
            uint8_t t41 = gm_l2;
            uint8_t t42 = ((t41 == ((uint8_t)45ULL)));
            if (!t42) goto ie38;
            uint8_t t43 = gm_l3;
            uint8_t t44 = ((t43 == ((uint8_t)45ULL)));
            if (!t44) goto ie38;
            {
                gm_t9qkh0rsooc8d t46;
                GM_ST(uint8_t, ((uint8_t *)&t46 + 0), ((uint8_t)53ULL));
                gm_te0mgnjdphs16 t47;
                GM_ST(uint8_t, (uint8_t *)&t47 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t47 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t47 + 4, 0);
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t47 + 0), t46);
                GM_ST(uint64_t, ((uint8_t *)&t47 + 8), ((uint64_t)3ULL));
                gm_te0mgnjdphs16 t48 = t47;
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&gm_l5), GM_LD(gm_t9qkh0rsooc8d, (uint8_t *)&t48 + 0));
                GM_ST(uint64_t, ((uint8_t *)&gm_l6), GM_LD(uint64_t, (uint8_t *)&t48 + 8));
            }
            goto ix38;
ie38:;
            uint8_t t50 = gm_l1;
            uint8_t t51 = ((t50 == ((uint8_t)45ULL)));
            if (!t51) goto ie49;
            uint8_t t52 = gm_l2;
            uint8_t t53 = ((t52 == ((uint8_t)62ULL)));
            if (!t53) goto ie49;
            {
                gm_t9qkh0rsooc8d t55;
                GM_ST(uint8_t, ((uint8_t *)&t55 + 0), ((uint8_t)60ULL));
                gm_te0mgnjdphs16 t56;
                GM_ST(uint8_t, (uint8_t *)&t56 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t56 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t56 + 4, 0);
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t56 + 0), t55);
                GM_ST(uint64_t, ((uint8_t *)&t56 + 8), ((uint64_t)2ULL));
                gm_te0mgnjdphs16 t57 = t56;
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&gm_l5), GM_LD(gm_t9qkh0rsooc8d, (uint8_t *)&t57 + 0));
                GM_ST(uint64_t, ((uint8_t *)&gm_l6), GM_LD(uint64_t, (uint8_t *)&t57 + 8));
            }
            goto ix49;
ie49:;
            uint8_t t59 = gm_l2;
            uint8_t t60 = ((t59 == ((uint8_t)61ULL)));
            if (!t60) goto ie58;
            {
                gm_t9qkh0rsooc8d t62 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l7) + 1));
                gm_te0mgnjdphs16 t63;
                GM_ST(uint8_t, (uint8_t *)&t63 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t63 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t63 + 4, 0);
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t63 + 0), t62);
                GM_ST(uint64_t, ((uint8_t *)&t63 + 8), ((uint64_t)2ULL));
                gm_te0mgnjdphs16 t64 = t63;
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&gm_l5), GM_LD(gm_t9qkh0rsooc8d, (uint8_t *)&t64 + 0));
                GM_ST(uint64_t, ((uint8_t *)&gm_l6), GM_LD(uint64_t, (uint8_t *)&t64 + 8));
            }
            goto ix58;
ie58:;
            uint8_t t66 = gm_l2;
            uint8_t t67 = ((t66 == ((uint8_t)37ULL)));
            if (!t67) goto ie65;
            uint8_t t68 = gm_l3;
            uint8_t t69 = ((t68 == ((uint8_t)61ULL)));
            if (!t69) goto ie65;
            {
                gm_t9qkh0rsooc8d t71 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l7) + 5));
                gm_te0mgnjdphs16 t72;
                GM_ST(uint8_t, (uint8_t *)&t72 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t72 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t72 + 4, 0);
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t72 + 0), t71);
                GM_ST(uint64_t, ((uint8_t *)&t72 + 8), ((uint64_t)3ULL));
                gm_te0mgnjdphs16 t73 = t72;
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&gm_l5), GM_LD(gm_t9qkh0rsooc8d, (uint8_t *)&t73 + 0));
                GM_ST(uint64_t, ((uint8_t *)&gm_l6), GM_LD(uint64_t, (uint8_t *)&t73 + 8));
            }
            goto ix65;
ie65:;
            uint8_t t75 = gm_l2;
            uint8_t t76 = ((t75 == ((uint8_t)37ULL)));
            if (!t76) goto ie74;
            {
                gm_t9qkh0rsooc8d t78 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l7) + 2));
                gm_te0mgnjdphs16 t79;
                GM_ST(uint8_t, (uint8_t *)&t79 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t79 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t79 + 4, 0);
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t79 + 0), t78);
                GM_ST(uint64_t, ((uint8_t *)&t79 + 8), ((uint64_t)2ULL));
                gm_te0mgnjdphs16 t80 = t79;
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&gm_l5), GM_LD(gm_t9qkh0rsooc8d, (uint8_t *)&t80 + 0));
                GM_ST(uint64_t, ((uint8_t *)&gm_l6), GM_LD(uint64_t, (uint8_t *)&t80 + 8));
            }
            goto ix74;
ie74:;
            uint8_t t82 = gm_l2;
            uint8_t t83 = ((t82 == ((uint8_t)124ULL)));
            if (!t83) goto ie81;
            uint8_t t84 = gm_l3;
            uint8_t t85 = ((t84 == ((uint8_t)61ULL)));
            if (!t85) goto ie81;
            {
                gm_t9qkh0rsooc8d t87 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l7) + 6));
                gm_te0mgnjdphs16 t88;
                GM_ST(uint8_t, (uint8_t *)&t88 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t88 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t88 + 4, 0);
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t88 + 0), t87);
                GM_ST(uint64_t, ((uint8_t *)&t88 + 8), ((uint64_t)3ULL));
                gm_te0mgnjdphs16 t89 = t88;
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&gm_l5), GM_LD(gm_t9qkh0rsooc8d, (uint8_t *)&t89 + 0));
                GM_ST(uint64_t, ((uint8_t *)&gm_l6), GM_LD(uint64_t, (uint8_t *)&t89 + 8));
            }
            goto ix81;
ie81:;
            uint8_t t91 = gm_l2;
            uint8_t t92 = ((t91 == ((uint8_t)124ULL)));
            if (!t92) goto ie90;
            {
                gm_t9qkh0rsooc8d t94 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l7) + 3));
                gm_te0mgnjdphs16 t95;
                GM_ST(uint8_t, (uint8_t *)&t95 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t95 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t95 + 4, 0);
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t95 + 0), t94);
                GM_ST(uint64_t, ((uint8_t *)&t95 + 8), ((uint64_t)2ULL));
                gm_te0mgnjdphs16 t96 = t95;
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&gm_l5), GM_LD(gm_t9qkh0rsooc8d, (uint8_t *)&t96 + 0));
                GM_ST(uint64_t, ((uint8_t *)&gm_l6), GM_LD(uint64_t, (uint8_t *)&t96 + 8));
            }
            goto ix90;
ie90:;
            uint8_t t98 = gm_l2;
            uint8_t t99 = ((t98 == ((uint8_t)63ULL)));
            if (!t99) goto ie97;
            {
                gm_t9qkh0rsooc8d t101 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l7) + 4));
                gm_te0mgnjdphs16 t102;
                GM_ST(uint8_t, (uint8_t *)&t102 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t102 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t102 + 4, 0);
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t102 + 0), t101);
                GM_ST(uint64_t, ((uint8_t *)&t102 + 8), ((uint64_t)2ULL));
                gm_te0mgnjdphs16 t103 = t102;
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&gm_l5), GM_LD(gm_t9qkh0rsooc8d, (uint8_t *)&t103 + 0));
                GM_ST(uint64_t, ((uint8_t *)&gm_l6), GM_LD(uint64_t, (uint8_t *)&t103 + 8));
            }
            goto ix97;
ie97:;
            {
                gm_t9qkh0rsooc8d t105 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l7) + 0));
                gm_l5 = t105;
            }
ix97:;
ix90:;
ix81:;
ix74:;
ix65:;
ix58:;
ix49:;
ix38:;
        }
        goto me26;
mn27:;
        uint8_t t107 = GM_LD(uint8_t, ((uint8_t *)&t25));
        if (!(t107 == ((uint8_t)47ULL))) goto mn106;
        {
            uint8_t t109 = gm_l2;
            uint8_t t110 = ((t109 == ((uint8_t)61ULL)));
            gm_t9qkh0rsooc8d t111;
            GM_ST(uint8_t, ((uint8_t *)&t111 + 0), ((uint8_t)26ULL));
            gm_t9qkh0rsooc8d t112;
            GM_ST(uint8_t, ((uint8_t *)&t112 + 0), ((uint8_t)3ULL));
            gm_te0mgnjdphs16 t113 = gm_f_one_or_two_lava5tu(t110, t111, t112);
            gm_te0mgnjdphs16 t114 = t113;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&gm_l5), GM_LD(gm_t9qkh0rsooc8d, (uint8_t *)&t114 + 0));
            GM_ST(uint64_t, ((uint8_t *)&gm_l6), GM_LD(uint64_t, (uint8_t *)&t114 + 8));
        }
        goto me26;
mn106:;
        uint8_t t116 = GM_LD(uint8_t, ((uint8_t *)&t25));
        if (!(t116 == ((uint8_t)37ULL))) goto mn115;
        {
            uint8_t t118 = gm_l2;
            uint8_t t119 = ((t118 == ((uint8_t)61ULL)));
            gm_t9qkh0rsooc8d t120;
            GM_ST(uint8_t, ((uint8_t *)&t120 + 0), ((uint8_t)27ULL));
            gm_t9qkh0rsooc8d t121;
            GM_ST(uint8_t, ((uint8_t *)&t121 + 0), ((uint8_t)4ULL));
            gm_te0mgnjdphs16 t122 = gm_f_one_or_two_lava5tu(t119, t120, t121);
            gm_te0mgnjdphs16 t123 = t122;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&gm_l5), GM_LD(gm_t9qkh0rsooc8d, (uint8_t *)&t123 + 0));
            GM_ST(uint64_t, ((uint8_t *)&gm_l6), GM_LD(uint64_t, (uint8_t *)&t123 + 8));
        }
        goto me26;
mn115:;
        uint8_t t125 = GM_LD(uint8_t, ((uint8_t *)&t25));
        if (!(t125 == ((uint8_t)60ULL))) goto mn124;
        {
            uint8_t t128 = gm_l2;
            uint8_t t129 = ((t128 == ((uint8_t)60ULL)));
            if (!t129) goto ie127;
            {
                uint8_t t132 = gm_l3;
                uint8_t t133 = ((t132 == ((uint8_t)37ULL)));
                if (!t133) goto ie131;
                uint8_t t134 = gm_l4;
                uint8_t t135 = ((t134 == ((uint8_t)61ULL)));
                if (!t135) goto ie131;
                {
                    gm_t9qkh0rsooc8d t137;
                    GM_ST(uint8_t, ((uint8_t *)&t137 + 0), ((uint8_t)36ULL));
                    gm_te0mgnjdphs16 t138;
                    GM_ST(uint8_t, (uint8_t *)&t138 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t138 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t138 + 4, 0);
                    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t138 + 0), t137);
                    GM_ST(uint64_t, ((uint8_t *)&t138 + 8), ((uint64_t)4ULL));
                    gm_te0mgnjdphs16 t139 = t138;
                    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&gm_l5), GM_LD(gm_t9qkh0rsooc8d, (uint8_t *)&t139 + 0));
                    GM_ST(uint64_t, ((uint8_t *)&gm_l6), GM_LD(uint64_t, (uint8_t *)&t139 + 8));
                }
                goto ix131;
ie131:;
                uint8_t t141 = gm_l3;
                uint8_t t142 = ((t141 == ((uint8_t)124ULL)));
                if (!t142) goto ie140;
                uint8_t t143 = gm_l4;
                uint8_t t144 = ((t143 == ((uint8_t)61ULL)));
                if (!t144) goto ie140;
                {
                    gm_t9qkh0rsooc8d t146;
                    GM_ST(uint8_t, ((uint8_t *)&t146 + 0), ((uint8_t)40ULL));
                    gm_te0mgnjdphs16 t147;
                    GM_ST(uint8_t, (uint8_t *)&t147 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t147 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t147 + 4, 0);
                    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t147 + 0), t146);
                    GM_ST(uint64_t, ((uint8_t *)&t147 + 8), ((uint64_t)4ULL));
                    gm_te0mgnjdphs16 t148 = t147;
                    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&gm_l5), GM_LD(gm_t9qkh0rsooc8d, (uint8_t *)&t148 + 0));
                    GM_ST(uint64_t, ((uint8_t *)&gm_l6), GM_LD(uint64_t, (uint8_t *)&t148 + 8));
                }
                goto ix140;
ie140:;
                uint8_t t150 = gm_l3;
                uint8_t t151 = ((t150 == ((uint8_t)37ULL)));
                if (!t151) goto ie149;
                {
                    gm_t9qkh0rsooc8d t153;
                    GM_ST(uint8_t, ((uint8_t *)&t153 + 0), ((uint8_t)13ULL));
                    gm_te0mgnjdphs16 t154;
                    GM_ST(uint8_t, (uint8_t *)&t154 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t154 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t154 + 4, 0);
                    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t154 + 0), t153);
                    GM_ST(uint64_t, ((uint8_t *)&t154 + 8), ((uint64_t)3ULL));
                    gm_te0mgnjdphs16 t155 = t154;
                    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&gm_l5), GM_LD(gm_t9qkh0rsooc8d, (uint8_t *)&t155 + 0));
                    GM_ST(uint64_t, ((uint8_t *)&gm_l6), GM_LD(uint64_t, (uint8_t *)&t155 + 8));
                }
                goto ix149;
ie149:;
                uint8_t t157 = gm_l3;
                uint8_t t158 = ((t157 == ((uint8_t)124ULL)));
                if (!t158) goto ie156;
                {
                    gm_t9qkh0rsooc8d t160;
                    GM_ST(uint8_t, ((uint8_t *)&t160 + 0), ((uint8_t)17ULL));
                    gm_te0mgnjdphs16 t161;
                    GM_ST(uint8_t, (uint8_t *)&t161 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t161 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t161 + 4, 0);
                    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t161 + 0), t160);
                    GM_ST(uint64_t, ((uint8_t *)&t161 + 8), ((uint64_t)3ULL));
                    gm_te0mgnjdphs16 t162 = t161;
                    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&gm_l5), GM_LD(gm_t9qkh0rsooc8d, (uint8_t *)&t162 + 0));
                    GM_ST(uint64_t, ((uint8_t *)&gm_l6), GM_LD(uint64_t, (uint8_t *)&t162 + 8));
                }
                goto ix156;
ie156:;
                uint8_t t164 = gm_l3;
                uint8_t t165 = ((t164 == ((uint8_t)63ULL)));
                if (!t165) goto ie163;
                {
                    gm_t9qkh0rsooc8d t167;
                    GM_ST(uint8_t, ((uint8_t *)&t167 + 0), ((uint8_t)21ULL));
                    gm_te0mgnjdphs16 t168;
                    GM_ST(uint8_t, (uint8_t *)&t168 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t168 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t168 + 4, 0);
                    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t168 + 0), t167);
                    GM_ST(uint64_t, ((uint8_t *)&t168 + 8), ((uint64_t)3ULL));
                    gm_te0mgnjdphs16 t169 = t168;
                    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&gm_l5), GM_LD(gm_t9qkh0rsooc8d, (uint8_t *)&t169 + 0));
                    GM_ST(uint64_t, ((uint8_t *)&gm_l6), GM_LD(uint64_t, (uint8_t *)&t169 + 8));
                }
                goto ix163;
ie163:;
                uint8_t t171 = gm_l3;
                uint8_t t172 = ((t171 == ((uint8_t)61ULL)));
                if (!t172) goto ie170;
                {
                    gm_t9qkh0rsooc8d t174;
                    GM_ST(uint8_t, ((uint8_t *)&t174 + 0), ((uint8_t)28ULL));
                    gm_te0mgnjdphs16 t175;
                    GM_ST(uint8_t, (uint8_t *)&t175 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t175 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t175 + 4, 0);
                    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t175 + 0), t174);
                    GM_ST(uint64_t, ((uint8_t *)&t175 + 8), ((uint64_t)3ULL));
                    gm_te0mgnjdphs16 t176 = t175;
                    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&gm_l5), GM_LD(gm_t9qkh0rsooc8d, (uint8_t *)&t176 + 0));
                    GM_ST(uint64_t, ((uint8_t *)&gm_l6), GM_LD(uint64_t, (uint8_t *)&t176 + 8));
                }
                goto ix170;
ie170:;
                {
                    gm_t9qkh0rsooc8d t178;
                    GM_ST(uint8_t, ((uint8_t *)&t178 + 0), ((uint8_t)5ULL));
                    gm_te0mgnjdphs16 t179;
                    GM_ST(uint8_t, (uint8_t *)&t179 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t179 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t179 + 4, 0);
                    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t179 + 0), t178);
                    GM_ST(uint64_t, ((uint8_t *)&t179 + 8), ((uint64_t)2ULL));
                    gm_te0mgnjdphs16 t180 = t179;
                    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&gm_l5), GM_LD(gm_t9qkh0rsooc8d, (uint8_t *)&t180 + 0));
                    GM_ST(uint64_t, ((uint8_t *)&gm_l6), GM_LD(uint64_t, (uint8_t *)&t180 + 8));
                }
ix170:;
ix163:;
ix156:;
ix149:;
ix140:;
ix131:;
            }
            goto ix127;
ie127:;
            uint8_t t182 = gm_l2;
            uint8_t t183 = ((t182 == ((uint8_t)61ULL)));
            if (!t183) goto ie181;
            {
                gm_t9qkh0rsooc8d t185;
                GM_ST(uint8_t, ((uint8_t *)&t185 + 0), ((uint8_t)42ULL));
                gm_te0mgnjdphs16 t186;
                GM_ST(uint8_t, (uint8_t *)&t186 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t186 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t186 + 4, 0);
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t186 + 0), t185);
                GM_ST(uint64_t, ((uint8_t *)&t186 + 8), ((uint64_t)2ULL));
                gm_te0mgnjdphs16 t187 = t186;
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&gm_l5), GM_LD(gm_t9qkh0rsooc8d, (uint8_t *)&t187 + 0));
                GM_ST(uint64_t, ((uint8_t *)&gm_l6), GM_LD(uint64_t, (uint8_t *)&t187 + 8));
            }
            goto ix181;
ie181:;
            {
                gm_t9qkh0rsooc8d t189;
                GM_ST(uint8_t, ((uint8_t *)&t189 + 0), ((uint8_t)41ULL));
                gm_l5 = t189;
            }
ix181:;
ix127:;
        }
        goto me26;
mn124:;
        uint8_t t191 = GM_LD(uint8_t, ((uint8_t *)&t25));
        if (!(t191 == ((uint8_t)62ULL))) goto mn190;
        {
            uint8_t t194 = gm_l2;
            uint8_t t195 = ((t194 == ((uint8_t)62ULL)));
            if (!t195) goto ie193;
            uint8_t t196 = gm_l3;
            uint8_t t197 = ((t196 == ((uint8_t)61ULL)));
            if (!t197) goto ie193;
            {
                gm_t9qkh0rsooc8d t199;
                GM_ST(uint8_t, ((uint8_t *)&t199 + 0), ((uint8_t)29ULL));
                gm_te0mgnjdphs16 t200;
                GM_ST(uint8_t, (uint8_t *)&t200 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t200 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t200 + 4, 0);
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t200 + 0), t199);
                GM_ST(uint64_t, ((uint8_t *)&t200 + 8), ((uint64_t)3ULL));
                gm_te0mgnjdphs16 t201 = t200;
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&gm_l5), GM_LD(gm_t9qkh0rsooc8d, (uint8_t *)&t201 + 0));
                GM_ST(uint64_t, ((uint8_t *)&gm_l6), GM_LD(uint64_t, (uint8_t *)&t201 + 8));
            }
            goto ix193;
ie193:;
            uint8_t t203 = gm_l2;
            uint8_t t204 = ((t203 == ((uint8_t)62ULL)));
            if (!t204) goto ie202;
            {
                gm_t9qkh0rsooc8d t206;
                GM_ST(uint8_t, ((uint8_t *)&t206 + 0), ((uint8_t)6ULL));
                gm_te0mgnjdphs16 t207;
                GM_ST(uint8_t, (uint8_t *)&t207 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t207 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t207 + 4, 0);
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t207 + 0), t206);
                GM_ST(uint64_t, ((uint8_t *)&t207 + 8), ((uint64_t)2ULL));
                gm_te0mgnjdphs16 t208 = t207;
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&gm_l5), GM_LD(gm_t9qkh0rsooc8d, (uint8_t *)&t208 + 0));
                GM_ST(uint64_t, ((uint8_t *)&gm_l6), GM_LD(uint64_t, (uint8_t *)&t208 + 8));
            }
            goto ix202;
ie202:;
            uint8_t t210 = gm_l2;
            uint8_t t211 = ((t210 == ((uint8_t)61ULL)));
            if (!t211) goto ie209;
            {
                gm_t9qkh0rsooc8d t213;
                GM_ST(uint8_t, ((uint8_t *)&t213 + 0), ((uint8_t)44ULL));
                gm_te0mgnjdphs16 t214;
                GM_ST(uint8_t, (uint8_t *)&t214 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t214 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t214 + 4, 0);
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t214 + 0), t213);
                GM_ST(uint64_t, ((uint8_t *)&t214 + 8), ((uint64_t)2ULL));
                gm_te0mgnjdphs16 t215 = t214;
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&gm_l5), GM_LD(gm_t9qkh0rsooc8d, (uint8_t *)&t215 + 0));
                GM_ST(uint64_t, ((uint8_t *)&gm_l6), GM_LD(uint64_t, (uint8_t *)&t215 + 8));
            }
            goto ix209;
ie209:;
            {
                gm_t9qkh0rsooc8d t217;
                GM_ST(uint8_t, ((uint8_t *)&t217 + 0), ((uint8_t)43ULL));
                gm_l5 = t217;
            }
ix209:;
ix202:;
ix193:;
        }
        goto me26;
mn190:;
        uint8_t t219 = GM_LD(uint8_t, ((uint8_t *)&t25));
        if (!(t219 == ((uint8_t)38ULL))) goto mn218;
        {
            uint8_t t221 = gm_l2;
            uint8_t t222 = ((t221 == ((uint8_t)61ULL)));
            gm_t9qkh0rsooc8d t223;
            GM_ST(uint8_t, ((uint8_t *)&t223 + 0), ((uint8_t)30ULL));
            gm_t9qkh0rsooc8d t224;
            GM_ST(uint8_t, ((uint8_t *)&t224 + 0), ((uint8_t)7ULL));
            gm_te0mgnjdphs16 t225 = gm_f_one_or_two_lava5tu(t222, t223, t224);
            gm_te0mgnjdphs16 t226 = t225;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&gm_l5), GM_LD(gm_t9qkh0rsooc8d, (uint8_t *)&t226 + 0));
            GM_ST(uint64_t, ((uint8_t *)&gm_l6), GM_LD(uint64_t, (uint8_t *)&t226 + 8));
        }
        goto me26;
mn218:;
        uint8_t t228 = GM_LD(uint8_t, ((uint8_t *)&t25));
        if (!(t228 == ((uint8_t)124ULL))) goto mn227;
        {
            uint8_t t230 = gm_l2;
            uint8_t t231 = ((t230 == ((uint8_t)61ULL)));
            gm_t9qkh0rsooc8d t232;
            GM_ST(uint8_t, ((uint8_t *)&t232 + 0), ((uint8_t)31ULL));
            gm_t9qkh0rsooc8d t233;
            GM_ST(uint8_t, ((uint8_t *)&t233 + 0), ((uint8_t)8ULL));
            gm_te0mgnjdphs16 t234 = gm_f_one_or_two_lava5tu(t231, t232, t233);
            gm_te0mgnjdphs16 t235 = t234;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&gm_l5), GM_LD(gm_t9qkh0rsooc8d, (uint8_t *)&t235 + 0));
            GM_ST(uint64_t, ((uint8_t *)&gm_l6), GM_LD(uint64_t, (uint8_t *)&t235 + 8));
        }
        goto me26;
mn227:;
        uint8_t t237 = GM_LD(uint8_t, ((uint8_t *)&t25));
        if (!(t237 == ((uint8_t)126ULL))) goto mn236;
        {
            uint8_t t239 = gm_l2;
            uint8_t t240 = ((t239 == ((uint8_t)61ULL)));
            gm_t9qkh0rsooc8d t241;
            GM_ST(uint8_t, ((uint8_t *)&t241 + 0), ((uint8_t)32ULL));
            gm_t9qkh0rsooc8d t242;
            GM_ST(uint8_t, ((uint8_t *)&t242 + 0), ((uint8_t)9ULL));
            gm_te0mgnjdphs16 t243 = gm_f_one_or_two_lava5tu(t240, t241, t242);
            gm_te0mgnjdphs16 t244 = t243;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&gm_l5), GM_LD(gm_t9qkh0rsooc8d, (uint8_t *)&t244 + 0));
            GM_ST(uint64_t, ((uint8_t *)&gm_l6), GM_LD(uint64_t, (uint8_t *)&t244 + 8));
        }
        goto me26;
mn236:;
        uint8_t t246 = GM_LD(uint8_t, ((uint8_t *)&t25));
        if (!(t246 == ((uint8_t)61ULL))) goto mn245;
        {
            uint8_t t248 = gm_l2;
            uint8_t t249 = ((t248 == ((uint8_t)62ULL)));
            gm_t9qkh0rsooc8d t250;
            GM_ST(uint8_t, ((uint8_t *)&t250 + 0), ((uint8_t)59ULL));
            gm_t9qkh0rsooc8d t251;
            GM_ST(uint8_t, ((uint8_t *)&t251 + 0), ((uint8_t)22ULL));
            gm_te0mgnjdphs16 t252 = gm_f_one_or_two_lava5tu(t249, t250, t251);
            gm_te0mgnjdphs16 t253 = t252;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&gm_l5), GM_LD(gm_t9qkh0rsooc8d, (uint8_t *)&t253 + 0));
            GM_ST(uint64_t, ((uint8_t *)&gm_l6), GM_LD(uint64_t, (uint8_t *)&t253 + 8));
        }
        goto me26;
mn245:;
        uint8_t t255 = GM_LD(uint8_t, ((uint8_t *)&t25));
        if (!(t255 == ((uint8_t)46ULL))) goto mn254;
        {
            uint8_t t258 = gm_l2;
            uint8_t t259 = ((t258 == ((uint8_t)46ULL)));
            if (!t259) goto ie257;
            uint8_t t260 = gm_l3;
            uint8_t t261 = ((t260 == ((uint8_t)46ULL)));
            if (!t261) goto ie257;
            {
                gm_t9qkh0rsooc8d t263;
                GM_ST(uint8_t, ((uint8_t *)&t263 + 0), ((uint8_t)52ULL));
                gm_te0mgnjdphs16 t264;
                GM_ST(uint8_t, (uint8_t *)&t264 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t264 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t264 + 4, 0);
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t264 + 0), t263);
                GM_ST(uint64_t, ((uint8_t *)&t264 + 8), ((uint64_t)3ULL));
                gm_te0mgnjdphs16 t265 = t264;
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&gm_l5), GM_LD(gm_t9qkh0rsooc8d, (uint8_t *)&t265 + 0));
                GM_ST(uint64_t, ((uint8_t *)&gm_l6), GM_LD(uint64_t, (uint8_t *)&t265 + 8));
            }
            goto ix257;
ie257:;
            uint8_t t267 = gm_l2;
            uint8_t t268 = ((t267 == ((uint8_t)46ULL)));
            if (!t268) goto ie266;
            uint8_t t269 = gm_l3;
            uint8_t t270 = ((t269 == ((uint8_t)61ULL)));
            if (!t270) goto ie266;
            {
                gm_t9qkh0rsooc8d t272;
                GM_ST(uint8_t, ((uint8_t *)&t272 + 0), ((uint8_t)51ULL));
                gm_te0mgnjdphs16 t273;
                GM_ST(uint8_t, (uint8_t *)&t273 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t273 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t273 + 4, 0);
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t273 + 0), t272);
                GM_ST(uint64_t, ((uint8_t *)&t273 + 8), ((uint64_t)3ULL));
                gm_te0mgnjdphs16 t274 = t273;
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&gm_l5), GM_LD(gm_t9qkh0rsooc8d, (uint8_t *)&t274 + 0));
                GM_ST(uint64_t, ((uint8_t *)&gm_l6), GM_LD(uint64_t, (uint8_t *)&t274 + 8));
            }
            goto ix266;
ie266:;
            uint8_t t276 = gm_l2;
            uint8_t t277 = ((t276 == ((uint8_t)46ULL)));
            if (!t277) goto ie275;
            {
                gm_t9qkh0rsooc8d t279;
                GM_ST(uint8_t, ((uint8_t *)&t279 + 0), ((uint8_t)50ULL));
                gm_te0mgnjdphs16 t280;
                GM_ST(uint8_t, (uint8_t *)&t280 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t280 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t280 + 4, 0);
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t280 + 0), t279);
                GM_ST(uint64_t, ((uint8_t *)&t280 + 8), ((uint64_t)2ULL));
                gm_te0mgnjdphs16 t281 = t280;
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&gm_l5), GM_LD(gm_t9qkh0rsooc8d, (uint8_t *)&t281 + 0));
                GM_ST(uint64_t, ((uint8_t *)&gm_l6), GM_LD(uint64_t, (uint8_t *)&t281 + 8));
            }
            goto ix275;
ie275:;
            {
                gm_t9qkh0rsooc8d t283;
                GM_ST(uint8_t, ((uint8_t *)&t283 + 0), ((uint8_t)58ULL));
                gm_l5 = t283;
            }
ix275:;
ix266:;
ix257:;
        }
        goto me26;
mn254:;
        uint8_t t285 = GM_LD(uint8_t, ((uint8_t *)&t25));
        if (!(t285 == ((uint8_t)58ULL))) goto mn284;
        {
            uint8_t t288 = gm_l2;
            uint8_t t289 = ((t288 == ((uint8_t)58ULL)));
            if (!t289) goto ie287;
            {
                gm_t9qkh0rsooc8d t291;
                GM_ST(uint8_t, ((uint8_t *)&t291 + 0), ((uint8_t)54ULL));
                gm_te0mgnjdphs16 t292;
                GM_ST(uint8_t, (uint8_t *)&t292 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t292 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t292 + 4, 0);
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t292 + 0), t291);
                GM_ST(uint64_t, ((uint8_t *)&t292 + 8), ((uint64_t)2ULL));
                gm_te0mgnjdphs16 t293 = t292;
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&gm_l5), GM_LD(gm_t9qkh0rsooc8d, (uint8_t *)&t293 + 0));
                GM_ST(uint64_t, ((uint8_t *)&gm_l6), GM_LD(uint64_t, (uint8_t *)&t293 + 8));
            }
            goto ix287;
ie287:;
            uint8_t t295 = gm_l2;
            uint8_t t296 = ((t295 == ((uint8_t)61ULL)));
            if (!t296) goto ie294;
            {
                gm_t9qkh0rsooc8d t298;
                GM_ST(uint8_t, ((uint8_t *)&t298 + 0), ((uint8_t)55ULL));
                gm_te0mgnjdphs16 t299;
                GM_ST(uint8_t, (uint8_t *)&t299 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t299 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t299 + 4, 0);
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t299 + 0), t298);
                GM_ST(uint64_t, ((uint8_t *)&t299 + 8), ((uint64_t)2ULL));
                gm_te0mgnjdphs16 t300 = t299;
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&gm_l5), GM_LD(gm_t9qkh0rsooc8d, (uint8_t *)&t300 + 0));
                GM_ST(uint64_t, ((uint8_t *)&gm_l6), GM_LD(uint64_t, (uint8_t *)&t300 + 8));
            }
            goto ix294;
ie294:;
            {
                gm_t9qkh0rsooc8d t302;
                GM_ST(uint8_t, ((uint8_t *)&t302 + 0), ((uint8_t)56ULL));
                gm_l5 = t302;
            }
ix294:;
ix287:;
        }
        goto me26;
mn284:;
        uint8_t t304 = GM_LD(uint8_t, ((uint8_t *)&t25));
        if (!(t304 == ((uint8_t)63ULL))) goto mn303;
        {
            gm_t9qkh0rsooc8d t306;
            GM_ST(uint8_t, ((uint8_t *)&t306 + 0), ((uint8_t)45ULL));
            gm_l5 = t306;
        }
        goto me26;
mn303:;
        uint8_t t308 = GM_LD(uint8_t, ((uint8_t *)&t25));
        if (!(t308 == ((uint8_t)33ULL))) goto mn307;
        {
            gm_t9qkh0rsooc8d t310;
            GM_ST(uint8_t, ((uint8_t *)&t310 + 0), ((uint8_t)46ULL));
            gm_l5 = t310;
        }
        goto me26;
mn307:;
        uint8_t t312 = GM_LD(uint8_t, ((uint8_t *)&t25));
        if (!(t312 == ((uint8_t)94ULL))) goto mn311;
        {
            gm_t9qkh0rsooc8d t314;
            GM_ST(uint8_t, ((uint8_t *)&t314 + 0), ((uint8_t)47ULL));
            gm_l5 = t314;
        }
        goto me26;
mn311:;
        uint8_t t316 = GM_LD(uint8_t, ((uint8_t *)&t25));
        if (!(t316 == ((uint8_t)44ULL))) goto mn315;
        {
            gm_t9qkh0rsooc8d t318;
            GM_ST(uint8_t, ((uint8_t *)&t318 + 0), ((uint8_t)57ULL));
            gm_l5 = t318;
        }
        goto me26;
mn315:;
        uint8_t t320 = GM_LD(uint8_t, ((uint8_t *)&t25));
        if (!(t320 == ((uint8_t)40ULL))) goto mn319;
        {
            gm_t9qkh0rsooc8d t322;
            GM_ST(uint8_t, ((uint8_t *)&t322 + 0), ((uint8_t)61ULL));
            gm_l5 = t322;
        }
        goto me26;
mn319:;
        uint8_t t324 = GM_LD(uint8_t, ((uint8_t *)&t25));
        if (!(t324 == ((uint8_t)41ULL))) goto mn323;
        {
            gm_t9qkh0rsooc8d t326;
            GM_ST(uint8_t, ((uint8_t *)&t326 + 0), ((uint8_t)62ULL));
            gm_l5 = t326;
        }
        goto me26;
mn323:;
        uint8_t t328 = GM_LD(uint8_t, ((uint8_t *)&t25));
        if (!(t328 == ((uint8_t)91ULL))) goto mn327;
        {
            gm_t9qkh0rsooc8d t330;
            GM_ST(uint8_t, ((uint8_t *)&t330 + 0), ((uint8_t)63ULL));
            gm_l5 = t330;
        }
        goto me26;
mn327:;
        uint8_t t332 = GM_LD(uint8_t, ((uint8_t *)&t25));
        if (!(t332 == ((uint8_t)93ULL))) goto mn331;
        {
            gm_t9qkh0rsooc8d t334;
            GM_ST(uint8_t, ((uint8_t *)&t334 + 0), ((uint8_t)64ULL));
            gm_l5 = t334;
        }
        goto me26;
mn331:;
        uint8_t t336 = GM_LD(uint8_t, ((uint8_t *)&t25));
        if (!(t336 == ((uint8_t)123ULL))) goto mn335;
        {
            gm_t9qkh0rsooc8d t338;
            GM_ST(uint8_t, ((uint8_t *)&t338 + 0), ((uint8_t)65ULL));
            gm_l5 = t338;
        }
        goto me26;
mn335:;
        uint8_t t340 = GM_LD(uint8_t, ((uint8_t *)&t25));
        if (!(t340 == ((uint8_t)125ULL))) goto mn339;
        {
            gm_t9qkh0rsooc8d t342;
            GM_ST(uint8_t, ((uint8_t *)&t342 + 0), ((uint8_t)66ULL));
            gm_l5 = t342;
        }
        goto me26;
mn339:;
        gm_taari1s0m51a7 t344;
        memset(&t344, 0, sizeof t344);
        uint8_t t345 = 0;
        F.ret = t344;
        goto gm_fret;
        goto me26;
mn343:;
        gm_trap("no match arm matched the scrutinee", gm_srcvmmksi0829pa, 433, 5);
me26:;
    }
    {
        void * t346 = gm_l0;
        void * t347 = gm_l0;
        uint64_t t348 = GM_LD(uint64_t, (((uint8_t *)t347) + 24));
        uint64_t t349 = gm_l6;
        uint64_t r350; bool o351 = __builtin_add_overflow(t348, t349, &r350);
        if (o351) gm_trap("integer overflow in `+`", gm_srcvmmksi0829pa, 528, 5);
        uint64_t t352 = r350;
        GM_ST(uint64_t, (((uint8_t *)t346) + 24), t352);
    }
    {
        gm_t9qkh0rsooc8d t353 = gm_l5;
        gm_taari1s0m51a7 t354;
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t354), t353);
        ((uint8_t *)&t354)[1] = 1;
        uint8_t t355 = 0;
        F.ret = t354;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_second_word_end_dpnj7g8(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_second_word_end_dpnj7g8 F;
    void * gm_l0; /* lx */
    uint64_t gm_l1; /* from */
    gm_b16_8 gm_l2; /* word */
    uint64_t gm_l3; /* i */
    uint8_t gm_l4; /* b */
    uint64_t gm_l5; /* end */
    uint8_t gm_l6; /* c */
    GM_STACK_CHECK(&F, 448u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        uint64_t t2 = gm_l1;
        gm_l3 = t2;
    }
    {
        for (;;) {
            if (!((uint8_t)1)) goto lb3;
            {
                void * t6 = gm_l0;
                uint64_t t7 = gm_l3;
                gm_tqs5p4n2tq19v t8 = gm_f_byte_b4v8vul(t6, t7);
                gm_tqs5p4n2tq19v t9 = t8;
                uint8_t t10;
                if (((((uint8_t *)&t9))[1] != 0)) {
                    uint8_t t11 = GM_LD(uint8_t, (((uint8_t *)&t9) + 0));
                    t10 = t11;
                } else {
                    goto lb3;
                }
                gm_l4 = t10;
            }
            {
                uint8_t t13 = gm_l4;
                gm_tqs5p4n2tq19v t14;
                GM_ST(uint8_t, ((uint8_t *)&t14 + 0), ((uint8_t)32ULL));
                GM_ST(uint8_t, ((uint8_t *)&t14 + 1), ((uint8_t)9ULL));
                gm_tqs5p4n2tq19v t15 = t14;
                uint8_t t16 = 0;
                uint8_t t17 = t13;
                if (gm_eqjmccqdb90f(((uint8_t *)&t15) + 0, ((uint8_t *)&t17))) t16 = 1;
                if (gm_eqjmccqdb90f(((uint8_t *)&t15) + 1, ((uint8_t *)&t17))) t16 = 1;
                uint8_t t18 = (uint8_t)(t16);
                if (!t18) goto ie12;
                {
                    uint64_t t20 = gm_l3;
                    uint64_t r21; bool o22 = __builtin_add_overflow(t20, ((uint64_t)1ULL), &r21);
                    if (o22) gm_trap("integer overflow in `+`", gm_srcvmmksi0829pa, 273, 13);
                    uint64_t t23 = r21;
                    gm_l3 = t23;
                }
                goto ix12;
ie12:;
                uint8_t t25 = gm_l4;
                uint8_t t26 = ((t25 == ((uint8_t)47ULL)));
                if (!t26) goto ie24;
                void * t27 = gm_l0;
                uint64_t t28 = gm_l3;
                uint64_t r29; bool o30 = __builtin_add_overflow(t28, ((uint64_t)1ULL), &r29);
                if (o30) gm_trap("integer overflow in `+`", gm_srcvmmksi0829pa, 274, 40);
                uint64_t t31 = r29;
                gm_tqs5p4n2tq19v t32 = gm_f_byte_b4v8vul(t27, t31);
                gm_tqs5p4n2tq19v t33;
                GM_ST(uint8_t, ((uint8_t *)&t33), ((uint8_t)42ULL));
                ((uint8_t *)&t33)[1] = 1;
                gm_tqs5p4n2tq19v t34 = t32;
                gm_tqs5p4n2tq19v t35 = t33;
                uint8_t t36 = (gm_eqb44cb3cf1n(((uint8_t *)&t34), ((uint8_t *)&t35)));
                if (!t36) goto ie24;
                {
                    void * t38 = gm_l0;
                    gm_b16_8 t39 = GM_LD(gm_b16_8, (((uint8_t *)t38) + 8));
                    uint64_t t40 = gm_l3;
                    gm_tf5srh4mp4b3i t41 = gm_f_inline_comment_end_8i5h1eq(t39, t40);
                    gm_tf5srh4mp4b3i t42 = t41;
                    uint64_t t43;
                    if (((((uint8_t *)&t42))[8] != 0)) {
                        uint64_t t44 = GM_LD(uint64_t, (((uint8_t *)&t42) + 0));
                        t43 = t44;
                    } else {
                        gm_tf5srh4mp4b3i t45;
                        memset(&t45, 0, sizeof t45);
                        uint8_t t46 = 0;
                        F.ret = t45;
                        goto gm_fret;
                    }
                    gm_l3 = t43;
                }
                goto ix24;
ie24:;
                {
                    goto lb3;
                }
ix24:;
ix12:;
            }
lc3:;
        }
lb3:;
    }
    {
        uint64_t t48 = gm_l3;
        uint64_t t49 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        uint64_t r50; bool o51 = __builtin_add_overflow(t48, t49, &r50);
        if (o51) gm_trap("integer overflow in `+`", gm_srcvmmksi0829pa, 280, 12);
        uint64_t t52 = r50;
        gm_l5 = t52;
    }
    {
        void * t54 = gm_l0;
        uint64_t t55 = GM_LD(uintptr_t, (((uint8_t *)t54) + 8) + 8);
        void * t56 = ((uint8_t *)GM_LD(void *, (((uint8_t *)t54) + 8)));
        uint64_t t57 = gm_l3;
        uint64_t t58 = (uintptr_t)t57;
        if (t58 > (uintptr_t)t55 || (uintptr_t)t55 > (uintptr_t)t55) gm_trap_slice(t58, (uintptr_t)t55, t55, gm_srcvmmksi0829pa, 281, 12);
        gm_b16_8 t59;
        GM_ST(void *, (uint8_t *)&t59, (void *)(((uint8_t *)t56 + t58 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t59 + 8, (uintptr_t)((uintptr_t)t55 - t58));
        gm_b16_8 t60 = gm_l2;
        uint8_t t61 = gm_f_starts_with_6vvt01e(t59, t60);
        uint8_t t62 = (uint8_t)!t61;
        if (!t62) goto ie53;
        {
            gm_tf5srh4mp4b3i t64;
            memset(&t64, 0, sizeof t64);
            uint8_t t65 = 0;
            F.ret = t64;
            goto gm_fret;
        }
        goto ix53;
ie53:;
ix53:;
    }
    {
        void * t67 = gm_l0;
        uint64_t t68 = gm_l5;
        gm_tqs5p4n2tq19v t69 = gm_f_byte_b4v8vul(t67, t68);
        gm_tqs5p4n2tq19v t70 = t69;
        if (!((((uint8_t *)&t70))[1] != 0)) goto ie66;
        uint8_t t71 = GM_LD(uint8_t, ((uint8_t *)&t70));
        gm_l6 = t71;
        {
            uint8_t t74 = gm_l6;
            uint8_t t75 = gm_f_is_ident_cont_317fpdb(t74);
            if (!t75) goto ie73;
            {
                gm_tf5srh4mp4b3i t77;
                memset(&t77, 0, sizeof t77);
                uint8_t t78 = 0;
                F.ret = t77;
                goto gm_fret;
            }
            goto ix73;
ie73:;
ix73:;
        }
        goto ix66;
ie66:;
ix66:;
    }
    {
        uint64_t t79 = gm_l5;
        gm_tf5srh4mp4b3i t80;
        GM_ST(uint8_t, (uint8_t *)&t80 + 9, 0);
        GM_ST(uint16_t, (uint8_t *)&t80 + 10, 0);
        GM_ST(uint32_t, (uint8_t *)&t80 + 12, 0);
        GM_ST(uint64_t, ((uint8_t *)&t80), t79);
        ((uint8_t *)&t80)[8] = 1;
        uint8_t t81 = 0;
        F.ret = t80;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_b0_1 gm_f_skip_trivia_014s7ai(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_skip_trivia_014s7ai F;
    void * gm_l0; /* lx */
    uint8_t gm_l1; /* b */
    gm_tqs5p4n2tq19v gm_l2; /* next */
    GM_STACK_CHECK(&F, 294u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        for (;;) {
            void * t3 = gm_l0;
            gm_tqs5p4n2tq19v t4 = gm_f_peek_72bostn(t3);
            gm_tqs5p4n2tq19v t5 = t4;
            if (!((((uint8_t *)&t5))[1] != 0)) goto lb2;
            uint8_t t6 = GM_LD(uint8_t, ((uint8_t *)&t5));
            gm_l1 = t6;
            {
                uint8_t t9 = gm_l1;
                uint8_t t10 = t9;
                uint8_t t15 = GM_LD(uint8_t, ((uint8_t *)&t10));
                if (!(t15 == ((uint8_t)32ULL))) goto pa14;
                goto po13;
pa14:;
                uint8_t t17 = GM_LD(uint8_t, ((uint8_t *)&t10));
                if (!(t17 == ((uint8_t)9ULL))) goto pa16;
                goto po13;
pa16:;
                goto mn12;
po13:;
                {
                    void * t19 = gm_l0;
                    void * t20 = gm_l0;
                    uint64_t t21 = GM_LD(uint64_t, (((uint8_t *)t20) + 24));
                    uint64_t r22; bool o23 = __builtin_add_overflow(t21, ((uint64_t)1ULL), &r22);
                    if (o23) gm_trap("integer overflow in `+`", gm_srcvmmksi0829pa, 166, 28);
                    uint64_t t24 = r22;
                    GM_ST(uint64_t, (((uint8_t *)t19) + 24), t24);
                }
                goto me11;
mn12:;
                uint8_t t26 = GM_LD(uint8_t, ((uint8_t *)&t10));
                if (!(t26 == ((uint8_t)10ULL))) goto mn25;
                {
                    void * t28 = gm_l0;
                    void * t29 = gm_l0;
                    uint64_t t30 = GM_LD(uint64_t, (((uint8_t *)t29) + 24));
                    void * t31 = gm_l0;
                    uint64_t t32 = GM_LD(uint64_t, (((uint8_t *)t31) + 24));
                    uint64_t r33; bool o34 = __builtin_add_overflow(t32, ((uint64_t)1ULL), &r33);
                    if (o34) gm_trap("integer overflow in `+`", gm_srcvmmksi0829pa, 168, 41);
                    uint64_t t35 = r33;
                    gm_f_note_newline_cchbr7g(t28, t30, t35);
                }
                {
                    void * t36 = gm_l0;
                    void * t37 = gm_l0;
                    uint64_t t38 = GM_LD(uint64_t, (((uint8_t *)t37) + 24));
                    uint64_t r39; bool o40 = __builtin_add_overflow(t38, ((uint64_t)1ULL), &r39);
                    if (o40) gm_trap("integer overflow in `+`", gm_srcvmmksi0829pa, 169, 17);
                    uint64_t t41 = r39;
                    GM_ST(uint64_t, (((uint8_t *)t36) + 24), t41);
                }
                goto me11;
mn25:;
                uint8_t t43 = GM_LD(uint8_t, ((uint8_t *)&t10));
                if (!(t43 == ((uint8_t)13ULL))) goto mn42;
                {
                    void * t46 = gm_l0;
                    gm_tqs5p4n2tq19v t47 = gm_f_peek_at_74p8lkq(t46, ((uint64_t)1ULL));
                    gm_tqs5p4n2tq19v t48;
                    GM_ST(uint8_t, ((uint8_t *)&t48), ((uint8_t)10ULL));
                    ((uint8_t *)&t48)[1] = 1;
                    gm_tqs5p4n2tq19v t49 = t47;
                    gm_tqs5p4n2tq19v t50 = t48;
                    uint8_t t51 = (gm_eqb44cb3cf1n(((uint8_t *)&t49), ((uint8_t *)&t50)));
                    if (!t51) goto ie45;
                    {
                        void * t53 = gm_l0;
                        void * t54 = gm_l0;
                        uint64_t t55 = GM_LD(uint64_t, (((uint8_t *)t54) + 24));
                        void * t56 = gm_l0;
                        uint64_t t57 = GM_LD(uint64_t, (((uint8_t *)t56) + 24));
                        uint64_t r58; bool o59 = __builtin_add_overflow(t57, ((uint64_t)2ULL), &r58);
                        if (o59) gm_trap("integer overflow in `+`", gm_srcvmmksi0829pa, 173, 45);
                        uint64_t t60 = r58;
                        gm_f_note_newline_cchbr7g(t53, t55, t60);
                    }
                    {
                        void * t61 = gm_l0;
                        void * t62 = gm_l0;
                        uint64_t t63 = GM_LD(uint64_t, (((uint8_t *)t62) + 24));
                        uint64_t r64; bool o65 = __builtin_add_overflow(t63, ((uint64_t)2ULL), &r64);
                        if (o65) gm_trap("integer overflow in `+`", gm_srcvmmksi0829pa, 174, 21);
                        uint64_t t66 = r64;
                        GM_ST(uint64_t, (((uint8_t *)t61) + 24), t66);
                    }
                    goto ix45;
ie45:;
                    {
                        void * t68 = gm_l0;
                        gm_b0_1 t69 = gm_f_lone_cr_ktb0jvm((void *)NULL, cx, NULL, t68);
                        if (gm_unw.kind) {
                            goto gm_ll7;
                        }
                    }
ix45:;
                }
                goto me11;
mn42:;
                uint8_t t71 = GM_LD(uint8_t, ((uint8_t *)&t10));
                if (!(t71 == ((uint8_t)47ULL))) goto mn70;
                {
                    void * t73 = gm_l0;
                    gm_tqs5p4n2tq19v t74 = gm_f_peek_at_74p8lkq(t73, ((uint64_t)1ULL));
                    gm_l2 = t74;
                }
                {
                    gm_tqs5p4n2tq19v t76 = gm_l2;
                    gm_tqs5p4n2tq19v t77;
                    GM_ST(uint8_t, ((uint8_t *)&t77), ((uint8_t)47ULL));
                    ((uint8_t *)&t77)[1] = 1;
                    gm_tqs5p4n2tq19v t78 = t76;
                    gm_tqs5p4n2tq19v t79 = t77;
                    uint8_t t80 = (gm_eqb44cb3cf1n(((uint8_t *)&t78), ((uint8_t *)&t79)));
                    if (!t80) goto ie75;
                    {
                        void * t82 = gm_l0;
                        gm_b0_1 t83 = gm_f_line_comment_s71u8hq((void *)NULL, cx, NULL, t82);
                        if (gm_unw.kind) {
                            goto gm_ll7;
                        }
                    }
                    goto ix75;
ie75:;
                    gm_tqs5p4n2tq19v t85 = gm_l2;
                    gm_tqs5p4n2tq19v t86;
                    GM_ST(uint8_t, ((uint8_t *)&t86), ((uint8_t)42ULL));
                    ((uint8_t *)&t86)[1] = 1;
                    gm_tqs5p4n2tq19v t87 = t85;
                    gm_tqs5p4n2tq19v t88 = t86;
                    uint8_t t89 = (gm_eqb44cb3cf1n(((uint8_t *)&t87), ((uint8_t *)&t88)));
                    if (!t89) goto ie84;
                    {
                        void * t91 = gm_l0;
                        gm_b0_1 t92 = gm_f_block_comment_fvce7qt((void *)NULL, cx, NULL, t91);
                        if (gm_unw.kind) {
                            goto gm_ll7;
                        }
                    }
                    goto ix84;
ie84:;
                    {
                        gm_b0_1 t94;
                        uint8_t t95 = 0;
                        F.ret = t94;
                        goto gm_fret;
                    }
ix84:;
ix75:;
                }
                goto me11;
mn70:;
                gm_b0_1 t97;
                uint8_t t98 = 0;
                F.ret = t97;
                goto gm_fret;
                goto me11;
mn96:;
                gm_trap("no match arm matched the scrutinee", gm_srcvmmksi0829pa, 165, 9);
me11:;
            }
lc2:;
        }
gm_ll7:;
        if (gm_unw.frame == &F.h && gm_unw.label == 0) {
            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb2; }
            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc2; }
        }
        goto gm_lad;
lb2:;
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
extern __attribute__((aligned(4))) uint64_t gm_f_slot_4oe07nr(gm_t9qkh0rsooc8d a0);
extern __attribute__((aligned(4))) gm_cseahvg8v7p0vs gm_f_span_tg7cev6(void * a0, uint64_t a1, uint64_t a2);
inline __attribute__((aligned(4))) uint8_t gm_f_starts_continuation_n3333i7(const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_starts_continuation_n3333i7 F;
    gm_t3fcbuehh3bs2 gm_l0; /* kind */
    gm_t9qkh0rsooc8d gm_l1; /* k */
    GM_STACK_CHECK_SHALLOW(&F, 51u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    uint8_t t1;
    uint8_t t4 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t4 != ((uint8_t)2ULL)) goto mn3;
    gm_t9qkh0rsooc8d t5 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)a0) + 8));
    gm_l1 = t5;
    gm_t9qkh0rsooc8d t6 = gm_l1;
    gm_t9qkh0rsooc8d t7;
    GM_ST(uint8_t, ((uint8_t *)&t7 + 0), ((uint8_t)7ULL));
    gm_t9qkh0rsooc8d t8;
    GM_ST(uint8_t, ((uint8_t *)&t8 + 0), ((uint8_t)0ULL));
    gm_t9qkh0rsooc8d t9;
    GM_ST(uint8_t, ((uint8_t *)&t9 + 0), ((uint8_t)20ULL));
    gm_tk22flr9mh3ql t10;
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t10 + 0), t7);
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t10 + 1), t8);
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t10 + 2), t9);
    gm_tk22flr9mh3ql t11 = t10;
    uint8_t t12 = 0;
    gm_t9qkh0rsooc8d t13 = t6;
    if (gm_eq2ceic3f8rp(((uint8_t *)&t11) + 0, ((uint8_t *)&t13))) t12 = 1;
    if (gm_eq2ceic3f8rp(((uint8_t *)&t11) + 1, ((uint8_t *)&t13))) t12 = 1;
    if (gm_eq2ceic3f8rp(((uint8_t *)&t11) + 2, ((uint8_t *)&t13))) t12 = 1;
    uint8_t t14 = (uint8_t)(t12);
    t1 = t14;
    goto me2;
mn3:;
    t1 = ((uint8_t)0);
    goto me2;
mn15:;
    gm_trap("no match arm matched the scrutinee", gm_srcvmmksi0829pa, 701, 55);
me2:;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) uint8_t gm_f_starts_continuation_n3333i7_c(gm_t3fcbuehh3bs2 a0) { return gm_f_starts_continuation_n3333i7(&a0); }
inline __attribute__((aligned(4))) uint8_t gm_f_suppresses_newline_after_o08ord2(const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_suppresses_newline_after_o08ord2 F;
    gm_t3fcbuehh3bs2 gm_l0; /* kind */
    gm_t9qkh0rsooc8d gm_l1; /* k */
    gm_t9qkh0rsooc8d gm_l2; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 95u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    uint8_t t1;
    uint8_t t7 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    switch (t7) { case ((uint8_t)0ULL): goto ma5; case ((uint8_t)1ULL): goto ma5; case ((uint8_t)2ULL): goto ma3; case ((uint8_t)3ULL): goto ma5; case ((uint8_t)4ULL): goto ma5; case ((uint8_t)5ULL): goto ma5; case ((uint8_t)6ULL): goto ma5; case ((uint8_t)7ULL): goto ma5; case ((uint8_t)8ULL): goto ma4; case ((uint8_t)9ULL): goto ma5; case ((uint8_t)10ULL): goto ma5; default: goto ma5; }
ma3:;
    uint8_t t9 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t9 != ((uint8_t)2ULL)) goto mn8;
    gm_t9qkh0rsooc8d t10 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)a0) + 8));
    gm_l1 = t10;
    gm_t9qkh0rsooc8d t11 = gm_l1;
    gm_t9qkh0rsooc8d t12;
    GM_ST(uint8_t, ((uint8_t *)&t12 + 0), ((uint8_t)16ULL));
    gm_t9qkh0rsooc8d t13;
    GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)15ULL));
    gm_t9qkh0rsooc8d t14;
    GM_ST(uint8_t, ((uint8_t *)&t14 + 0), ((uint8_t)0ULL));
    gm_t9qkh0rsooc8d t15;
    GM_ST(uint8_t, ((uint8_t *)&t15 + 0), ((uint8_t)20ULL));
    gm_t9qkh0rsooc8d t16;
    GM_ST(uint8_t, ((uint8_t *)&t16 + 0), ((uint8_t)7ULL));
    gm_t724v60om7sh5 t17;
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t17 + 0), t12);
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t17 + 1), t13);
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t17 + 2), t14);
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t17 + 3), t15);
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t17 + 4), t16);
    gm_t724v60om7sh5 t18 = t17;
    uint8_t t19 = 0;
    gm_t9qkh0rsooc8d t20 = t11;
    if (gm_eq2ceic3f8rp(((uint8_t *)&t18) + 0, ((uint8_t *)&t20))) t19 = 1;
    if (gm_eq2ceic3f8rp(((uint8_t *)&t18) + 1, ((uint8_t *)&t20))) t19 = 1;
    if (gm_eq2ceic3f8rp(((uint8_t *)&t18) + 2, ((uint8_t *)&t20))) t19 = 1;
    if (gm_eq2ceic3f8rp(((uint8_t *)&t18) + 3, ((uint8_t *)&t20))) t19 = 1;
    if (gm_eq2ceic3f8rp(((uint8_t *)&t18) + 4, ((uint8_t *)&t20))) t19 = 1;
    uint8_t t21 = (uint8_t)(t19);
    t1 = t21;
    goto me2;
mn8:;
ma4:;
    uint8_t t23 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t23 != ((uint8_t)8ULL)) goto mn22;
    gm_t9qkh0rsooc8d t24 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)a0) + 8));
    gm_l2 = t24;
    gm_t9qkh0rsooc8d t25 = gm_l2;
    gm_t9qkh0rsooc8d t26;
    GM_ST(uint8_t, ((uint8_t *)&t26 + 0), ((uint8_t)45ULL));
    gm_t9qkh0rsooc8d t27;
    GM_ST(uint8_t, ((uint8_t *)&t27 + 0), ((uint8_t)46ULL));
    gm_t9qkh0rsooc8d t28;
    GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)47ULL));
    gm_t9qkh0rsooc8d t29;
    GM_ST(uint8_t, ((uint8_t *)&t29 + 0), ((uint8_t)48ULL));
    gm_t9qkh0rsooc8d t30;
    GM_ST(uint8_t, ((uint8_t *)&t30 + 0), ((uint8_t)49ULL));
    gm_t9qkh0rsooc8d t31;
    GM_ST(uint8_t, ((uint8_t *)&t31 + 0), ((uint8_t)52ULL));
    gm_t9qkh0rsooc8d t32;
    GM_ST(uint8_t, ((uint8_t *)&t32 + 0), ((uint8_t)53ULL));
    gm_t9qkh0rsooc8d t33;
    GM_ST(uint8_t, ((uint8_t *)&t33 + 0), ((uint8_t)62ULL));
    gm_t9qkh0rsooc8d t34;
    GM_ST(uint8_t, ((uint8_t *)&t34 + 0), ((uint8_t)64ULL));
    gm_t9qkh0rsooc8d t35;
    GM_ST(uint8_t, ((uint8_t *)&t35 + 0), ((uint8_t)66ULL));
    gm_tfh78knfg7urq t36;
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t36 + 0), t26);
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t36 + 1), t27);
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t36 + 2), t28);
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t36 + 3), t29);
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t36 + 4), t30);
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t36 + 5), t31);
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t36 + 6), t32);
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t36 + 7), t33);
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t36 + 8), t34);
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t36 + 9), t35);
    gm_tfh78knfg7urq t37 = t36;
    uint8_t t38 = 0;
    gm_t9qkh0rsooc8d t39 = t25;
    if (gm_eqv3tj1pu43g(((uint8_t *)&t37) + 0, ((uint8_t *)&t39))) t38 = 1;
    if (gm_eqv3tj1pu43g(((uint8_t *)&t37) + 1, ((uint8_t *)&t39))) t38 = 1;
    if (gm_eqv3tj1pu43g(((uint8_t *)&t37) + 2, ((uint8_t *)&t39))) t38 = 1;
    if (gm_eqv3tj1pu43g(((uint8_t *)&t37) + 3, ((uint8_t *)&t39))) t38 = 1;
    if (gm_eqv3tj1pu43g(((uint8_t *)&t37) + 4, ((uint8_t *)&t39))) t38 = 1;
    if (gm_eqv3tj1pu43g(((uint8_t *)&t37) + 5, ((uint8_t *)&t39))) t38 = 1;
    if (gm_eqv3tj1pu43g(((uint8_t *)&t37) + 6, ((uint8_t *)&t39))) t38 = 1;
    if (gm_eqv3tj1pu43g(((uint8_t *)&t37) + 7, ((uint8_t *)&t39))) t38 = 1;
    if (gm_eqv3tj1pu43g(((uint8_t *)&t37) + 8, ((uint8_t *)&t39))) t38 = 1;
    if (gm_eqv3tj1pu43g(((uint8_t *)&t37) + 9, ((uint8_t *)&t39))) t38 = 1;
    uint8_t t40 = (uint8_t)(!t38);
    t1 = t40;
    goto me2;
mn22:;
ma5:;
    t1 = ((uint8_t)0);
    goto me2;
mn41:;
ma6:;
    gm_trap("no match arm matched the scrutinee", gm_srcvmmksi0829pa, 689, 60);
me2:;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) uint8_t gm_f_suppresses_newline_after_o08ord2_c(gm_t3fcbuehh3bs2 a0) { return gm_f_suppresses_newline_after_o08ord2(&a0); }
__attribute__((aligned(4))) gm_b0_1 gm_f_unexpected_char_r3t8d4c(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_unexpected_char_r3t8d4c F;
    void * gm_l0; /* lx */
    uint64_t gm_l1; /* lo */
    gm_tb31k5l794t7p gm_l2; /* c */
    gm_b16_8 gm_l3; /* message */
    uint32_t gm_l4; /* <tmp> */
    GM_STACK_CHECK(&F, 831u);
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
        gm_tb31k5l794t7p t5 = GM_CONV(gm_tb31k5l794t7p, gm_f_current_char_ctktsl2(t4));
        gm_l2 = t5;
    }
    {
        void * t6 = gm_l0;
        void * t7 = gm_l0;
        uint64_t t8 = GM_LD(uint64_t, (((uint8_t *)t7) + 24));
        uint64_t t9 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
        uint64_t r10; bool o11 = __builtin_add_overflow(t8, t9, &r10);
        if (o11) gm_trap("integer overflow in `+`", gm_srcvmmksi0829pa, 401, 5);
        uint64_t t12 = r10;
        GM_ST(uint64_t, (((uint8_t *)t6) + 24), t12);
    }
    {
        gm_b16_8 t13;
        GM_ST(void *, (uint8_t *)&t13, (void *)(gm_sr4qbrk6tlgaa));
        GM_ST(uintptr_t, (uint8_t *)&t13 + 8, (uintptr_t)(0));
        gm_l3 = t13;
    }
    {
        uint32_t t15 = GM_LD(uint32_t, (((uint8_t *)&gm_l2) + 0));
        uint8_t t16 = (t15 >= ((uint32_t)128ULL));
        if (!t16) goto ie14;
        uint32_t t17 = GM_LD(uint32_t, (((uint8_t *)&gm_l2) + 0));
        uint8_t t18 = gm_f_is_unicode_space_b2m9rku(t17);
        if (!t18) goto ie14;
        {
            uint32_t t20 = GM_LD(uint32_t, (((uint8_t *)&gm_l2) + 0));
            gm_t88rcucg1b7uk t21;
            GM_ST(uint32_t, ((uint8_t *)&t21 + 0), t20);
            gm_tabb710d9mq2e t22;
            gm_f_format_t4ivgd1((void *)&t22, (void *)NULL, cx, NULL, t21);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t23 = t22;
            if (((((uint8_t *)&t23))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t23) + 8, gm_srcvmmksi0829pa, 404, 19);
            gm_b16_8 t24 = GM_LD(gm_b16_8, (((uint8_t *)&t23) + 8));
            gm_b16_8 t25 = t24;
            gm_b16_8 t26 = GM_LD(gm_b16_8, ((uint8_t *)&t25));
            gm_l3 = t26;
        }
        goto ix14;
ie14:;
        uint32_t t28 = GM_LD(uint32_t, (((uint8_t *)&gm_l2) + 0));
        uint8_t t29 = (t28 >= ((uint32_t)128ULL));
        if (!t29) goto ie27;
        {
            gm_tommf4j9upomc t31;
            memset((uint8_t *)&t31 + 20, 0, 4);
            void * t32 = gm_l0;
            uint64_t t33 = GM_LD(uintptr_t, (((uint8_t *)t32) + 8) + 8);
            void * t34 = ((uint8_t *)GM_LD(void *, (((uint8_t *)t32) + 8)));
            uint64_t t35 = gm_l1;
            uint64_t t36 = (uintptr_t)t35;
            void * t37 = gm_l0;
            uint64_t t38 = GM_LD(uint64_t, (((uint8_t *)t37) + 24));
            uint64_t t39 = (uintptr_t)t38;
            if (t36 > t39 || t39 > (uintptr_t)t33) gm_trap_slice(t36, t39, t33, gm_srcvmmksi0829pa, 408, 13);
            gm_b16_8 t40;
            GM_ST(void *, (uint8_t *)&t40, (void *)(((uint8_t *)t34 + t36 * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t40 + 8, (uintptr_t)(t39 - t36));
            GM_ST(gm_b16_8, (uint8_t *)&t31 + 0, t40);
            uint32_t t41 = GM_LD(uint32_t, (((uint8_t *)&gm_l2) + 0));
            GM_ST(uint32_t, (uint8_t *)&t31 + 16, t41);
            gm_tabb710d9mq2e t42;
            gm_f_format_pscs6r9((void *)&t42, (void *)NULL, cx, NULL, &t31);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t43 = t42;
            if (((((uint8_t *)&t43))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t43) + 8, gm_srcvmmksi0829pa, 406, 19);
            gm_b16_8 t44 = GM_LD(gm_b16_8, (((uint8_t *)&t43) + 8));
            gm_b16_8 t45 = t44;
            gm_b16_8 t46 = GM_LD(gm_b16_8, ((uint8_t *)&t45));
            gm_l3 = t46;
        }
        goto ix27;
ie27:;
        {
            uint32_t t49 = GM_LD(uint32_t, (((uint8_t *)&gm_l2) + 0));
            gm_l4 = t49;
        }
        uint8_t t50 = 0;
        uint32_t t51 = gm_l4;
        uint8_t t52 = (((uint32_t)33ULL) <= t51);
        if (t52) {
            uint32_t t53 = gm_l4;
            uint8_t t54 = (t53 <= ((uint32_t)126ULL));
            t50 = t54;
        } else t50 = 0;
        uint8_t t55 = t50;
        if (!t55) goto ie47;
        {
            void * t57 = gm_l0;
            uint64_t t58 = GM_LD(uintptr_t, (((uint8_t *)t57) + 8) + 8);
            void * t59 = ((uint8_t *)GM_LD(void *, (((uint8_t *)t57) + 8)));
            uint64_t t60 = gm_l1;
            uint64_t t61 = (uintptr_t)t60;
            void * t62 = gm_l0;
            uint64_t t63 = GM_LD(uint64_t, (((uint8_t *)t62) + 24));
            uint64_t t64 = (uintptr_t)t63;
            if (t61 > t64 || t64 > (uintptr_t)t58) gm_trap_slice(t61, t64, t58, gm_srcvmmksi0829pa, 412, 59);
            gm_b16_8 t65;
            GM_ST(void *, (uint8_t *)&t65, (void *)(((uint8_t *)t59 + t61 * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t65 + 8, (uintptr_t)(t64 - t61));
            gm_tnpifv20irnph t66;
            GM_ST(gm_b16_8, ((uint8_t *)&t66 + 0), t65);
            gm_tabb710d9mq2e t67;
            gm_f_format_fuvjr35((void *)&t67, (void *)NULL, cx, NULL, t66);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t68 = t67;
            if (((((uint8_t *)&t68))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t68) + 8, gm_srcvmmksi0829pa, 412, 19);
            gm_b16_8 t69 = GM_LD(gm_b16_8, (((uint8_t *)&t68) + 8));
            gm_b16_8 t70 = t69;
            gm_b16_8 t71 = GM_LD(gm_b16_8, ((uint8_t *)&t70));
            gm_l3 = t71;
        }
        goto ix47;
ie47:;
        {
            uint32_t t73 = GM_LD(uint32_t, (((uint8_t *)&gm_l2) + 0));
            gm_t88rcucg1b7uk t74;
            GM_ST(uint32_t, ((uint8_t *)&t74 + 0), t73);
            gm_tabb710d9mq2e t75;
            gm_f_format_i801868((void *)&t75, (void *)NULL, cx, NULL, t74);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t76 = t75;
            if (((((uint8_t *)&t76))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t76) + 8, gm_srcvmmksi0829pa, 414, 19);
            gm_b16_8 t77 = GM_LD(gm_b16_8, (((uint8_t *)&t76) + 8));
            gm_b16_8 t78 = t77;
            gm_b16_8 t79 = GM_LD(gm_b16_8, ((uint8_t *)&t78));
            gm_l3 = t79;
        }
ix47:;
ix27:;
ix14:;
    }
    {
        void * t80 = gm_l0;
        uint64_t t81 = gm_l1;
        void * t82 = gm_l0;
        uint64_t t83 = GM_LD(uint64_t, (((uint8_t *)t82) + 24));
        gm_b16_8 t84 = gm_l3;
        gm_b0_1 t85 = gm_f_error_p5r0d56((void *)NULL, cx, NULL, t80, t81, t83, t84);
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
