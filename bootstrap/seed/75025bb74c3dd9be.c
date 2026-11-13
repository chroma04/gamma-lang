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
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint8_t f0; uint8_t f1; uint8_t f2; uint8_t f3; uint8_t f4; uint8_t f5; } gm_t4q2b5ibsrdik;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint8_t a[1]; } gm_tivanu2mfcv6o;
typedef struct { uint8_t f0; uint8_t p1[3]; union { int32_t v0; } f1; } gm_toe5fmr3vvf4d;
typedef struct { uint8_t f0; uint8_t f1; } gm_tqs5p4n2tq19v;
typedef struct { uint32_t f0; uint32_t f1; gm_b16_8 f2; } gm_t2s5j3n0guq0t;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
_Static_assert(sizeof(gm_t4q2b5ibsrdik) == 6 && _Alignof(gm_t4q2b5ibsrdik) == 1 && offsetof(gm_t4q2b5ibsrdik, f0) == 0 && offsetof(gm_t4q2b5ibsrdik, f1) == 1 && offsetof(gm_t4q2b5ibsrdik, f2) == 2 && offsetof(gm_t4q2b5ibsrdik, f3) == 3 && offsetof(gm_t4q2b5ibsrdik, f4) == 4 && offsetof(gm_t4q2b5ibsrdik, f5) == 5, "layout of `OpenOptions`");
typedef struct { gm_b16_8 f0; uint32_t f1; uint32_t f2; } gm_t5hdnnftkup6m;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
typedef struct { gm_b16_8 f0; uint32_t f1; uint32_t f2; gm_b16_8 f3; } gm_tb40qn7hk0tl9;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; } gm_tfeab279c16rr;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_toe5fmr3vvf4d v0; uint64_t v1; } f1; } gm_thf5kr0is9u9i;
_Static_assert(sizeof(gm_tivanu2mfcv6o) == 1 && _Alignof(gm_tivanu2mfcv6o) == 1 && offsetof(gm_tivanu2mfcv6o, a) == 0, "layout of `[1]u8`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_toe5fmr3vvf4d v0; gm_t9qkh0rsooc8d v1; } f1; } gm_tj5jn79kq9pnm;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; uint8_t f2; uint8_t pe[7]; } gm_tk9n3vlila9qg;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; } f1; } gm_tl572d7579hns;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
_Static_assert(sizeof(gm_toe5fmr3vvf4d) == 8 && _Alignof(gm_toe5fmr3vvf4d) == 4 && offsetof(gm_toe5fmr3vvf4d, f0) == 0 && offsetof(gm_toe5fmr3vvf4d, f1) == 4, "layout of `IoError`");
typedef struct { gm_b16_8 f0; uint32_t f1; uint8_t pe[4]; } gm_tommf4j9upomc;
typedef struct { void * f0; void * f1; void * f2; void * f3; uint32_t f4; uint32_t f5; uint32_t f6; uint8_t p7[4]; gm_b16_8 f7; } gm_toqvmi7jqorid;
_Static_assert(sizeof(gm_tqs5p4n2tq19v) == 2 && _Alignof(gm_tqs5p4n2tq19v) == 1 && offsetof(gm_tqs5p4n2tq19v, f0) == 0 && offsetof(gm_tqs5p4n2tq19v, f1) == 1, "layout of `(u8, u8)`");
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; gm_th2lqbo77pvsc f1; uint64_t f2; } gm_t04fuf8kbk07t;
_Static_assert(sizeof(gm_t2s5j3n0guq0t) == 24 && _Alignof(gm_t2s5j3n0guq0t) == 8 && offsetof(gm_t2s5j3n0guq0t, f0) == 0 && offsetof(gm_t2s5j3n0guq0t, f1) == 4 && offsetof(gm_t2s5j3n0guq0t, f2) == 8, "layout of `Def`");
_Static_assert(sizeof(gm_t5hdnnftkup6m) == 24 && _Alignof(gm_t5hdnnftkup6m) == 8 && offsetof(gm_t5hdnnftkup6m, f0) == 0 && offsetof(gm_t5hdnnftkup6m, f1) == 16 && offsetof(gm_t5hdnnftkup6m, f2) == 20, "layout of `(string, u32, u32)`");
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
_Static_assert(sizeof(gm_tb40qn7hk0tl9) == 40 && _Alignof(gm_tb40qn7hk0tl9) == 8 && offsetof(gm_tb40qn7hk0tl9, f0) == 0 && offsetof(gm_tb40qn7hk0tl9, f1) == 16 && offsetof(gm_tb40qn7hk0tl9, f2) == 20 && offsetof(gm_tb40qn7hk0tl9, f3) == 24, "layout of `SourceLocation`");
typedef struct { gm_tfeab279c16rr f0; uint8_t f1; uint8_t pe[7]; } gm_td6svc7gs468a;
_Static_assert(sizeof(gm_tfeab279c16rr) == 48 && _Alignof(gm_tfeab279c16rr) == 8 && offsetof(gm_tfeab279c16rr, f0) == 0 && offsetof(gm_tfeab279c16rr, f1) == 16 && offsetof(gm_tfeab279c16rr, f2) == 32, "layout of `Toolchain`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tj5jn79kq9pnm v0; gm_b16_8 v1; } f1; } gm_tgvh15qshf643;
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
_Static_assert(sizeof(gm_thf5kr0is9u9i) == 16 && _Alignof(gm_thf5kr0is9u9i) == 8 && offsetof(gm_thf5kr0is9u9i, f0) == 0 && offsetof(gm_thf5kr0is9u9i, f1) == 8, "layout of `IoError!usize`");
typedef struct { gm_th2lqbo77pvsc f0; uint8_t f1; uint8_t pe[7]; } gm_ti5ramhglnvdt;
_Static_assert(sizeof(gm_tj5jn79kq9pnm) == 12 && _Alignof(gm_tj5jn79kq9pnm) == 4 && offsetof(gm_tj5jn79kq9pnm, f0) == 0 && offsetof(gm_tj5jn79kq9pnm, f1) == 4, "layout of `ReadError`");
_Static_assert(sizeof(gm_tk9n3vlila9qg) == 40 && _Alignof(gm_tk9n3vlila9qg) == 8 && offsetof(gm_tk9n3vlila9qg, f0) == 0 && offsetof(gm_tk9n3vlila9qg, f1) == 16 && offsetof(gm_tk9n3vlila9qg, f2) == 32, "layout of `(lib: string, link_name: string, variadic: bool)`");
_Static_assert(sizeof(gm_tl572d7579hns) == 24 && _Alignof(gm_tl572d7579hns) == 8 && offsetof(gm_tl572d7579hns, f0) == 0 && offsetof(gm_tl572d7579hns, f1) == 8, "layout of `string!()`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
_Static_assert(sizeof(gm_tommf4j9upomc) == 24 && _Alignof(gm_tommf4j9upomc) == 8 && offsetof(gm_tommf4j9upomc, f0) == 0 && offsetof(gm_tommf4j9upomc, f1) == 16, "layout of `(string, u32)`");
_Static_assert(sizeof(gm_toqvmi7jqorid) == 64 && _Alignof(gm_toqvmi7jqorid) == 8 && offsetof(gm_toqvmi7jqorid, f0) == 0 && offsetof(gm_toqvmi7jqorid, f1) == 8 && offsetof(gm_toqvmi7jqorid, f2) == 16 && offsetof(gm_toqvmi7jqorid, f3) == 24 && offsetof(gm_toqvmi7jqorid, f4) == 32 && offsetof(gm_toqvmi7jqorid, f5) == 36 && offsetof(gm_toqvmi7jqorid, f6) == 40 && offsetof(gm_toqvmi7jqorid, f7) == 48, "layout of `Compiled`");
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
typedef struct { uint8_t f0; gm_t9qkh0rsooc8d f1; gm_t9qkh0rsooc8d f2; uint8_t p3[5]; gm_th2lqbo77pvsc f3; gm_b16_8 f4; } gm_tvcg39brotose;
_Static_assert(sizeof(gm_t04fuf8kbk07t) == 40 && _Alignof(gm_t04fuf8kbk07t) == 8 && offsetof(gm_t04fuf8kbk07t, f0) == 0 && offsetof(gm_t04fuf8kbk07t, f1) == 8 && offsetof(gm_t04fuf8kbk07t, f2) == 32, "layout of `Hit`");
typedef struct { gm_th2lqbo77pvsc f0; gm_ti5ramhglnvdt f1; uint8_t f2; uint8_t pe[7]; } gm_t9feugf5hcdtg;
_Static_assert(sizeof(gm_td6svc7gs468a) == 56 && _Alignof(gm_td6svc7gs468a) == 8 && offsetof(gm_td6svc7gs468a, f0) == 0 && offsetof(gm_td6svc7gs468a, f1) == 48, "layout of `?Toolchain`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tvcg39brotose v0; gm_b16_8 v1; } f1; } gm_tdpp5715iet0a;
typedef struct { gm_tvcg39brotose f0; gm_trqfvmjtt4sij f1; gm_trqfvmjtt4sij f2; gm_trqfvmjtt4sij f3; uint32_t f4; uint8_t pe[4]; } gm_tec68k60rd12v;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t04fuf8kbk07t v0; gm_b16_8 v1; } f1; } gm_tepv5jklv667k;
_Static_assert(sizeof(gm_tgvh15qshf643) == 24 && _Alignof(gm_tgvh15qshf643) == 8 && offsetof(gm_tgvh15qshf643, f0) == 0 && offsetof(gm_tgvh15qshf643, f1) == 8, "layout of `ReadError![]u8`");
_Static_assert(sizeof(gm_ti5ramhglnvdt) == 32 && _Alignof(gm_ti5ramhglnvdt) == 8 && offsetof(gm_ti5ramhglnvdt, f0) == 0 && offsetof(gm_ti5ramhglnvdt, f1) == 24, "layout of `??string`");
_Static_assert(sizeof(gm_tvcg39brotose) == 48 && _Alignof(gm_tvcg39brotose) == 8 && offsetof(gm_tvcg39brotose, f0) == 0 && offsetof(gm_tvcg39brotose, f1) == 1 && offsetof(gm_tvcg39brotose, f2) == 2 && offsetof(gm_tvcg39brotose, f3) == 8 && offsetof(gm_tvcg39brotose, f4) == 32, "layout of `Options`");
_Static_assert(sizeof(gm_t9feugf5hcdtg) == 64 && _Alignof(gm_t9feugf5hcdtg) == 8 && offsetof(gm_t9feugf5hcdtg, f0) == 0 && offsetof(gm_t9feugf5hcdtg, f1) == 24 && offsetof(gm_t9feugf5hcdtg, f2) == 56, "layout of `Classified`");
_Static_assert(sizeof(gm_tdpp5715iet0a) == 56 && _Alignof(gm_tdpp5715iet0a) == 8 && offsetof(gm_tdpp5715iet0a, f0) == 0 && offsetof(gm_tdpp5715iet0a, f1) == 8, "layout of `Cli`");
_Static_assert(sizeof(gm_tec68k60rd12v) == 176 && _Alignof(gm_tec68k60rd12v) == 8 && offsetof(gm_tec68k60rd12v, f0) == 0 && offsetof(gm_tec68k60rd12v, f1) == 48 && offsetof(gm_tec68k60rd12v, f2) == 88 && offsetof(gm_tec68k60rd12v, f3) == 128 && offsetof(gm_tec68k60rd12v, f4) == 168, "layout of `Repl`");
_Static_assert(sizeof(gm_tepv5jklv667k) == 48 && _Alignof(gm_tepv5jklv667k) == 8 && offsetof(gm_tepv5jklv667k, f0) == 0 && offsetof(gm_tepv5jklv667k, f1) == 8, "layout of `Scan`");
struct gm_fr_command_7gn4uju {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_command_run_06r7r92 {
    gm_frame h;
    gm_tvcg39brotose l0; /* options */
    gm_tvcg39brotose l1; /* opts */
    gm_tec68k60rd12v l4; /* r */
    gm_trqfvmjtt4sij l6; /* pending */
    gm_t9feugf5hcdtg l11; /* c */
    uint8_t ret;
};
struct gm_fr_compile_rj09gp7 {
    gm_frame h;
    gm_td6svc7gs468a l4; /* tail */
    gm_trqfvmjtt4sij l7; /* src */
    gm_tfeab279c16rr l24; /* t */
    gm_toqvmi7jqorid l25; /* c */
    gm_toqvmi7jqorid ret;
};
struct gm_fr_is_space_pq8dr45 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_list_session_6f2qqqv {
    gm_frame h;
    gm_tk9n3vlila9qg l6; /* rec */
    gm_b0_1 ret;
};
struct gm_fr_load_file_3kdl78m {
    gm_frame h;
    gm_t9feugf5hcdtg l6; /* c */
    gm_trqfvmjtt4sij l8; /* rest */
    gm_tk9n3vlila9qg l12; /* i */
    gm_trqfvmjtt4sij l18; /* parts */
    gm_tk9n3vlila9qg l21; /* i */
    uint8_t ret;
};
struct gm_fr_new_repl_hdsg2o4 {
    gm_frame h;
    gm_tvcg39brotose l0; /* opts */
    gm_tec68k60rd12v ret;
};
struct gm_fr_parse_options_9st61k7 {
    gm_frame h;
    gm_tvcg39brotose l1; /* o */
    gm_t04fuf8kbk07t l4; /* hit */
    gm_tdpp5715iet0a ret;
};
struct gm_fr_read_error_text_1lgi2bl {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_read_line_1oksar8 {
    gm_frame h;
    gm_trqfvmjtt4sij l0; /* bytes */
    gm_tl572d7579hns ret;
};
struct gm_fr_renamed_vars_fho5hvj {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* out */
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_replay_texts_3te9pi7 {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* out */
    gm_tk9n3vlila9qg l5; /* rec */
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_reset_pb2jqrj {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_set_var_pjsp5eq {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_show_861qdlm {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_statements_dafpns9 {
    gm_frame h;
    gm_trqfvmjtt4sij l3; /* defs */
    gm_trqfvmjtt4sij l4; /* next_vars */
    gm_trqfvmjtt4sij l9; /* uses */
    gm_trqfvmjtt4sij l15; /* replay */
    gm_tfeab279c16rr l16; /* raw */
    gm_toqvmi7jqorid l17; /* c_raw */
    gm_toqvmi7jqorid l18; /* chosen */
    gm_tfeab279c16rr l20; /* tail */
    gm_toqvmi7jqorid l21; /* c_val */
    gm_trqfvmjtt4sij l28; /* pure */
    gm_tfeab279c16rr l29; /* tail */
    gm_toqvmi7jqorid l30; /* probe */
    gm_trqfvmjtt4sij l33; /* names */
    uint8_t ret;
};
struct gm_fr_submit_pah0u77 {
    gm_frame h;
    gm_t9feugf5hcdtg l2; /* c */
    gm_tk9n3vlila9qg l9; /* i */
    gm_b0_1 ret;
};
struct gm_fr_submit_text_7dvfev6 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_trim_newlines_9rj3i73 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_try_decls_23vcohd {
    gm_frame h;
    gm_trqfvmjtt4sij l3; /* names */
    gm_tk9n3vlila9qg l6; /* i */
    gm_trqfvmjtt4sij l10; /* replay */
    gm_toqvmi7jqorid l11; /* c */
    gm_trqfvmjtt4sij l12; /* kept */
    gm_tk9n3vlila9qg l25; /* i */
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_type_of_b1a5sco {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* replay */
    gm_trqfvmjtt4sij l3; /* uses */
    gm_tfeab279c16rr l5; /* direct */
    gm_toqvmi7jqorid l6; /* c */
    gm_tfeab279c16rr l7; /* bound */
    gm_toqvmi7jqorid l8; /* c2 */
    gm_b0_1 ret;
};
bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b);
bool gm_eqjmccqdb90f(const uint8_t *a, const uint8_t *b);
__attribute__((aligned(4))) void gm_f_check_source_nh9qcnk(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_t9qkh0rsooc8d a1, uint32_t a2, gm_b16_8 a3);
__attribute__((aligned(4))) void gm_f_classify_rm38cme(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_clear_3ibm3tg(void * a0);
__attribute__((aligned(4))) void gm_f_clear_gp35vgp(void * a0);
__attribute__((aligned(4))) void gm_f_clear_h4369ui(void * a0);
__attribute__((aligned(4))) void gm_f_clear_sl9fc3u(void * a0);
__attribute__((aligned(4))) uint8_t gm_f_command_7gn4uju(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) uint8_t gm_f_command_run_06r7r92(void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_compile_rj09gp7(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_b16_8 a2, gm_b16_8 a3, const void * a4, gm_b16_8 a5, uint8_t a6);
__attribute__((aligned(4))) uint8_t gm_f_contains_gkki4kq(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_deinit_gvn47sj(void * a0);
__attribute__((aligned(4))) void gm_f_deinit_r70pji5(void * a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_describe_read_error_uikkr59(void *env, gm_node *cx, gm_node *inst, gm_tj5jn79kq9pnm a0);
__attribute__((aligned(4))) void gm_f_dupe_g9iqpvn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, const void * a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_5nn7ph0(void);
__attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_q5bjns8(void);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_extend_0cs85gs(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_extend_e1j40m3(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_format_5a05gmg(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_j0ggm04(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) gm_t9qkh0rsooc8d gm_f_format_of_nbvl1cj(gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_format_uq5dnfv(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) uint8_t gm_f_is_blank_asoolmf(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) uint8_t gm_f_is_space_pq8dr45(uint8_t a0);
__attribute__((aligned(4))) void gm_f_join_sfknljb(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_list_1t36jvm(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_list_93qogsr(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_list_e6ie1fq_2(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_list_epvducg(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_list_f0gjjbh_2(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_list_glg30gi(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_b0_1 gm_f_list_session_6f2qqqv(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) uint8_t gm_f_load_file_3kdl78m(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_lookup_var_3vpm3u6(void *gm_out, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) uint8_t gm_f_may_mutate_uj4e7jp(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_t9qkh0rsooc8d gm_f_mode_of_pq04he2(gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_new_repl_hdsg2o4(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) uint8_t gm_f_open_brackets_hicehud(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_parse_options_9st61k7(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_1e500nc_2(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_3ef746b(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_52bt1f1(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_ej78v89(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_f9bu01m(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_t6e85n8_2(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_t83rg2m(void * a0, uint8_t a1);
__attribute__((aligned(4))) void gm_f_read_all_r9rh9qd(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_read_error_text_1lgi2bl(void *env, gm_node *cx, gm_node *inst, gm_tj5jn79kq9pnm a0);
__attribute__((aligned(4))) void gm_f_read_line_1oksar8(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_b0_1 gm_f_release_mdh37fq(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_rename_47b7og5(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_renamed_vars_fho5hvj(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_replay_texts_3te9pi7(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1);
__attribute__((aligned(4))) void gm_f_reset_pb2jqrj(void * a0);
__attribute__((aligned(4))) uint8_t gm_f_run_aqa5hss(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t9qkh0rsooc8d a1, uint8_t a2);
__attribute__((aligned(4))) void gm_f_scan_ll1gqkn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, uint64_t a1, uint8_t a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_set_var_pjsp5eq(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_show_861qdlm(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_shown_o0bbr93(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t9qkh0rsooc8d a1);
__attribute__((aligned(4))) uint8_t gm_f_starts_with_6vvt01e(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) uint8_t gm_f_statements_dafpns9(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, const void * a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_submit_pah0u77(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, const void * a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_submit_text_7dvfev6(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_syntax_errors_fk23f5t(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_t9qkh0rsooc8d a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_trim_fdrfe6i(gm_b16_8 a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_trim_newlines_9rj3i73(gm_b16_8 a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_trim_start_179crak(gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_try_decls_23vcohd(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_type_of_b1a5sco(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) uint8_t gm_f_utf8_valid_a8oc1t8(gm_b16_8 a0);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_0gtanrr(void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_nre2qb9(void * a0, const void * a1);
GM_NOINLINE void gm_pf0nmuupvugn(gm_node *cx, gm_b16_8 a, const char *file, uint32_t line, uint32_t col);
GM_NOINLINE bool gm_pf1m4b8q4e78(gm_node *cx, const void *d, uintptr_t n, const char *file, uint32_t line, uint32_t col);
GM_NOINLINE bool gm_pf454qfrpt6r(gm_node *cx, const void *d, uintptr_t n, const char *file, uint32_t line, uint32_t col);
GM_NOINLINE bool gm_pf70j0q3sgt1(gm_node *cx, const gm_type *d, const void *v, const char *file, uint32_t line, uint32_t col);
GM_NOINLINE void gm_pfmaojsuds6a(gm_node *cx, gm_b16_8 a, const char *file, uint32_t line, uint32_t col);
GM_NOINLINE bool gm_pfu8dq5bikbh(gm_node *cx, const gm_type *d, const void *v, const char *file, uint32_t line, uint32_t col);
extern int32_t gm_x_isatty_dhpcvep(int32_t) __asm__(GM_SYM("isatty"));
extern uint8_t gm_c5dv10uirua[16] __attribute__((aligned(8)));
extern uint8_t gm_c7983l5vkhl[16] __attribute__((aligned(8)));
extern uint8_t gm_cd231ebi1kg[16] __attribute__((aligned(8)));
extern uint8_t gm_cdgbrlsjvrb[16] __attribute__((aligned(8)));
extern uint8_t gm_celpgpm1fi8[1] __attribute__((aligned(1)));
extern uint8_t gm_ckbflbrril1[1] __attribute__((aligned(1)));
extern uint8_t gm_cnohofor2q6[16] __attribute__((aligned(8)));
extern uint8_t gm_k093ni2ahjb[40] __attribute__((aligned(8)));
extern uint8_t gm_kdmasfkh2un[40] __attribute__((aligned(8)));
extern uint8_t gm_kif5rfegp72[40] __attribute__((aligned(8)));
extern const uint8_t gm_s055t7hpo88bt[4];
extern const uint8_t gm_s0iid7nkkvf30[55];
extern const uint8_t gm_s0of2eragnise[6];
extern const uint8_t gm_s1qrqfh656g4q[6];
extern const uint8_t gm_s39mljh669qgl[2];
extern const uint8_t gm_s3a1oio07ri1p[3];
extern const uint8_t gm_s3qd7ldqb9526[29];
extern const uint8_t gm_s3vfg72f8aupk[20];
extern const uint8_t gm_s4chbmodjkinv[7];
extern const uint8_t gm_s4hd92lokmtot[4];
extern const uint8_t gm_s4nquq7iq8v5h[29];
extern const uint8_t gm_s4uditudns916[3];
extern const uint8_t gm_s527dqlmmaier[6];
extern const uint8_t gm_s54emlnl8lmoe[38];
extern const uint8_t gm_s684ivkkpni41[7];
extern const uint8_t gm_s68c5jkfvg5dd[86];
extern const uint8_t gm_s6kq1mi4s1g58[3];
extern const uint8_t gm_s7cu0m5qc7arr[43];
extern const uint8_t gm_s8dbdqj1sqibj[3];
extern const uint8_t gm_s9pcjvamrhbn7[25];
extern const uint8_t gm_sdg1ovmjdibb5[6];
extern const uint8_t gm_sfdbfrv46lihd[2];
extern const uint8_t gm_sfeb57kq1r2n4[4];
extern const uint8_t gm_sg5gdnrith2i6[3];
extern const uint8_t gm_sgk14ir7dv1gf[6];
extern const uint8_t gm_sgvln0vb20p94[17];
extern const uint8_t gm_sho8l3vae133f[8];
extern const uint8_t gm_sii9tamh3vah6[32];
extern const uint8_t gm_sikpqtja8pqpv[3];
extern const uint8_t gm_sjbjet56e96co[17];
extern const uint8_t gm_sjsosmp24eqn0[36];
extern const uint8_t gm_skkiv5ia4t4au[39];
extern const uint8_t gm_skolmnfc0ccbd[2];
extern const uint8_t gm_slgmqgkioa4u6[3];
extern const uint8_t gm_slrfv83pestqp[31];
extern const uint8_t gm_smrc1u3r4i5hd[7];
extern const uint8_t gm_sn1sq59pu45vv[39];
extern const uint8_t gm_sn9nkqgu8isqo[3];
extern const uint8_t gm_snp0oqa4m79hk[6];
extern const uint8_t gm_so514oj5nbhcu[2];
extern const uint8_t gm_sog4rksrmsqd2[5];
extern const uint8_t gm_sojb2hhp0ln7r[31];
extern const uint8_t gm_som0q53jqvjh3[5];
extern const uint8_t gm_spun41oorb00o[30];
extern const uint8_t gm_squacltecdsom[5];
extern const uint8_t gm_sr4qbrk6tlgaa[1];
extern const char gm_src3dhhc4laniae[];
extern const uint8_t gm_sreprot2s508e[3];
extern const uint8_t gm_ssfjmtpagd1pu[6];
extern const uint8_t gm_sshi9ffc6nkc4[10];
extern const uint8_t gm_ssulg3jrm3q2v[16];
extern const uint8_t gm_su5tga7voctrl[7];
extern const gm_type gm_ty0iqiubfm7e;
extern const gm_type gm_tybihgc69jv3;
extern const gm_type gm_tyo86ks9qkvt;
__attribute__((aligned(4))) uint8_t gm_f_command_7gn4uju(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1) {
    struct gm_fr_command_7gn4uju F;
    void * gm_l0; /* r */
    gm_b16_8 gm_l1; /* line */
    gm_b16_8 gm_l2; /* cmd */
    gm_b16_8 gm_l3; /* arg */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    uint64_t gm_l6; /* i */
    uint8_t gm_l7; /* c */
    GM_STACK_CHECK(&F, 509u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        gm_b16_8 t2 = gm_l1;
        gm_l2 = t2;
    }
    {
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sr4qbrk6tlgaa));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(0));
        gm_l3 = t3;
    }
    {
        {
            gm_b16_8 t5 = gm_l1;
            gm_l4 = t5;
        }
        {
            gm_l5 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t7 = gm_l5;
                uint64_t t8 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                uint8_t t9 = (t7 < t8);
                if (!t9) goto lb6;
                {
                    uint64_t t12 = gm_l5;
                    uint64_t t13 = gm_l5;
                    uint64_t t14 = (uintptr_t)t13;
                    uint64_t t15 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                    uint8_t t16 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t14 * 1));
                    gm_tf5srh4mp4b3i t17;
                    GM_ST(uint8_t, (uint8_t *)&t17 + 9, 0);
                    GM_ST(uint16_t, (uint8_t *)&t17 + 10, 0);
                    GM_ST(uint32_t, (uint8_t *)&t17 + 12, 0);
                    GM_ST(uint64_t, ((uint8_t *)&t17 + 0), t12);
                    GM_ST(uint8_t, ((uint8_t *)&t17 + 8), t16);
                    gm_tf5srh4mp4b3i t18 = t17;
                    uint64_t t21 = GM_LD(uint64_t, (((uint8_t *)&t18) + 0));
                    gm_l6 = t21;
                    uint8_t t22 = GM_LD(uint8_t, (((uint8_t *)&t18) + 8));
                    gm_l7 = t22;
                    {
                        uint8_t t25 = gm_l7;
                        uint8_t t26 = gm_f_is_space_pq8dr45(t25);
                        if (!t26) goto ie24;
                        {
                            uint64_t t28 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                            void * t29 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)));
                            uint64_t t30 = (uintptr_t)((uint64_t)0ULL);
                            uint64_t t31 = gm_l6;
                            uint64_t t32 = (uintptr_t)t31;
                            if (t30 > t32 || t32 > (uintptr_t)t28) gm_trap_slice(t30, t32, t28, gm_src3dhhc4laniae, 454, 19);
                            gm_b16_8 t33;
                            GM_ST(void *, (uint8_t *)&t33, (void *)(((uint8_t *)t29 + t30 * 1)));
                            GM_ST(uintptr_t, (uint8_t *)&t33 + 8, (uintptr_t)(t32 - t30));
                            gm_l2 = t33;
                        }
                        {
                            uint64_t t34 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                            void * t35 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)));
                            uint64_t t36 = gm_l6;
                            uint64_t r37; bool o38 = __builtin_add_overflow(t36, ((uint64_t)1ULL), &r37);
                            if (o38) gm_trap("integer overflow in `+`", gm_src3dhhc4laniae, 455, 33);
                            uint64_t t39 = r37;
                            uint64_t t40 = (uintptr_t)t39;
                            if (t40 > (uintptr_t)t34 || (uintptr_t)t34 > (uintptr_t)t34) gm_trap_slice(t40, (uintptr_t)t34, t34, gm_src3dhhc4laniae, 455, 28);
                            gm_b16_8 t41;
                            GM_ST(void *, (uint8_t *)&t41, (void *)(((uint8_t *)t35 + t40 * 1)));
                            GM_ST(uintptr_t, (uint8_t *)&t41 + 8, (uintptr_t)((uintptr_t)t34 - t40));
                            gm_b16_8 t42 = gm_f_trim_fdrfe6i(t41);
                            gm_l3 = t42;
                        }
                        {
                            goto lb6;
                        }
                        goto ix24;
ie24:;
ix24:;
                    }
                    goto me19;
mn20:;
                    gm_trap("no match arm matched the scrutinee", gm_src3dhhc4laniae, 452, 5);
me19:;
                }
lc6:;
                {
                    uint64_t t44 = gm_l5;
                    uint64_t r45; bool o46 = __builtin_add_overflow(t44, ((uint64_t)1ULL), &r45);
                    uint64_t t47 = r45;
                    gm_l5 = t47;
                }
            }
lb6:;
        }
    }
    {
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8) == 5) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l2)), gm_sdg1ovmjdibb5, 5) == 0)) goto pa51;
        goto po50;
pa51:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8) == 2) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l2)), gm_sreprot2s508e, 2) == 0)) goto pa52;
        goto po50;
pa52:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8) == 5) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l2)), gm_sgk14ir7dv1gf, 5) == 0)) goto pa53;
        goto po50;
pa53:;
        goto mn49;
po50:;
        uint8_t t54 = 0;
        F.ret = ((uint8_t)0);
        goto gm_fret;
        goto me48;
mn49:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8) == 5) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l2)), gm_s527dqlmmaier, 5) == 0)) goto pa57;
        goto po56;
pa57:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8) == 2) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l2)), gm_s4uditudns916, 2) == 0)) goto pa58;
        goto po56;
pa58:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8) == 2) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l2)), gm_s6kq1mi4s1g58, 2) == 0)) goto pa59;
        goto po56;
pa59:;
        goto mn55;
po56:;
        gm_b16_8 t60 = GM_LD(gm_b16_8, gm_c5dv10uirua);
        gm_pfmaojsuds6a(cx, t60, gm_src3dhhc4laniae, 461, 32);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me48;
mn55:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8) == 6) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l2)), gm_s684ivkkpni41, 6) == 0)) goto mn61;
        void * t62 = gm_l0;
        gm_f_reset_pb2jqrj(t62);
        goto me48;
mn61:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8) == 5) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l2)), gm_ssfjmtpagd1pu, 5) == 0)) goto mn63;
        void * t64 = gm_l0;
        gm_b0_1 t65 = gm_f_list_session_6f2qqqv((void *)NULL, cx, NULL, t64);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me48;
mn63:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8) == 5) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l2)), gm_s0of2eragnise, 5) == 0)) goto pa68;
        goto po67;
pa68:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8) == 2) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l2)), gm_sn9nkqgu8isqo, 2) == 0)) goto pa69;
        goto po67;
pa69:;
        goto mn66;
po67:;
        {
            uint64_t t72 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
            uint8_t t73 = ((t72 == ((uint64_t)0ULL)));
            if (!t73) goto ie71;
            {
                if (gm_pf454qfrpt6r(cx, gm_sjsosmp24eqn0, 35, gm_src3dhhc4laniae, 465, 31)) {
                    goto gm_lad;
                }
            }
            goto ix71;
ie71:;
            {
                void * t76 = gm_l0;
                gm_b16_8 t77 = gm_l3;
                gm_b0_1 t78 = gm_f_type_of_b1a5sco((void *)NULL, cx, NULL, t76, t77);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
ix71:;
        }
        goto me48;
mn66:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8) == 5) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l2)), gm_snp0oqa4m79hk, 5) == 0)) goto mn79;
        {
            uint64_t t82 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
            uint8_t t83 = ((t82 == ((uint64_t)0ULL)));
            if (!t83) goto ie81;
            {
                if (gm_pf454qfrpt6r(cx, gm_s4nquq7iq8v5h, 28, gm_src3dhhc4laniae, 468, 31)) {
                    goto gm_lad;
                }
            }
            goto ix81;
ie81:;
            {
                {
                    void * t87 = gm_l0;
                    gm_b16_8 t88 = gm_l3;
                    uint8_t t89 = gm_f_load_file_3kdl78m((void *)NULL, cx, NULL, t87, t88);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
            }
ix81:;
        }
        goto me48;
mn79:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8) == 7) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l2)), gm_sho8l3vae133f, 7) == 0)) goto mn90;
        goto me48;
mn90:;
        gm_b16_8 t93 = gm_l2;
        gm_b16_8 t94 = t93;
        if (gm_pf454qfrpt6r(cx, gm_s9pcjvamrhbn7, 24, gm_src3dhhc4laniae, 471, 14)) {
            goto gm_lad;
        }
        if (gm_pfu8dq5bikbh(cx, &gm_ty0iqiubfm7e, ((uint8_t *)&t94), gm_src3dhhc4laniae, 471, 14)) {
            goto gm_lad;
        }
        if (gm_pf454qfrpt6r(cx, gm_sii9tamh3vah6, 31, gm_src3dhhc4laniae, 471, 14)) {
            goto gm_lad;
        }
        goto me48;
mn92:;
        gm_trap("no match arm matched the scrutinee", gm_src3dhhc4laniae, 459, 5);
me48:;
    }
    {
        uint8_t t95 = 0;
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
__attribute__((aligned(4))) uint8_t gm_f_command_run_06r7r92(void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_command_run_06r7r92 F;
    uint8_t gm_l2; /* interactive */
    uint8_t gm_l3; /* quiet */
    gm_b16_8 gm_l5; /* path */
    gm_tl572d7579hns gm_l7; /* next */
    gm_b16_8 gm_l8; /* l */
    gm_b16_8 gm_l9; /* line_text */
    gm_b16_8 gm_l10; /* line */
    gm_b16_8 gm_l12; /* text */
    GM_STACK_CHECK(&F, 1143u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memcpy(&F.l0, a0, sizeof F.l0);
    {
        gm_tvcg39brotose t2 = F.l0;
        F.l1 = t2;
    }
    {
        uint8_t t3 = 0;
        int32_t t4 = gm_x_isatty_dhpcvep(((int32_t)0LL));
        uint8_t t5 = (!(t4 == ((int32_t)0LL)));
        if (t5) {
            int32_t t6 = gm_x_isatty_dhpcvep(((int32_t)1LL));
            uint8_t t7 = (!(t6 == ((int32_t)0LL)));
            t3 = t7;
        } else t3 = 0;
        gm_l2 = t3;
    }
    {
        uint8_t t8 = 0;
        uint8_t t9 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
        if (!t9) {
            uint8_t t10 = gm_l2;
            uint8_t t11 = (uint8_t)!t10;
            t8 = t11;
        } else t8 = 1;
        GM_ST(uint8_t, (((uint8_t *)&F.l1) + 0), t8);
    }
    {
        uint8_t t12 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
        gm_l3 = t12;
    }
    {
        gm_f_new_repl_hdsg2o4((void *)&F.l4, (void *)NULL, cx, NULL, &(*(gm_tvcg39brotose *)((uint8_t *)&F.l1)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        if (!(((((uint8_t *)&F.l1) + 8))[16] != 0)) goto ie13;
        gm_b16_8 t14 = GM_LD(gm_b16_8, (((uint8_t *)&F.l1) + 8));
        gm_l5 = t14;
        {
            void * t17 = (void *)((uint8_t *)&F.l4);
            gm_b16_8 t18 = gm_l5;
            uint8_t t19 = gm_f_load_file_3kdl78m((void *)NULL, cx, NULL, t17, t18);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t20 = (uint8_t)!t19;
            if (!t20) goto ie16;
            {
                uint8_t t22 = 0;
                F.ret = ((uint8_t)1ULL);
                goto gm_fret;
            }
            goto ix16;
ie16:;
ix16:;
        }
        goto ix13;
ie13:;
ix13:;
    }
    {
        uint8_t t24 = gm_l3;
        uint8_t t25 = (uint8_t)!t24;
        if (!t25) goto ie23;
        {
            gm_b16_8 t27 = GM_LD(gm_b16_8, (((uint8_t *)&F.l1) + 32));
            gm_b16_8 t28 = t27;
            if (gm_pf1m4b8q4e78(cx, gm_su5tga7voctrl, 6, gm_src3dhhc4laniae, 517, 20)) {
                goto gm_lad;
            }
            if (gm_pf70j0q3sgt1(cx, &gm_ty0iqiubfm7e, ((uint8_t *)&t28), gm_src3dhhc4laniae, 517, 20)) {
                goto gm_lad;
            }
            if (gm_pf1m4b8q4e78(cx, gm_s7cu0m5qc7arr, 42, gm_src3dhhc4laniae, 517, 20)) {
                goto gm_lad;
            }
        }
        goto ix23;
ie23:;
ix23:;
    }
    {
        gm_f_list_uvfu1k1((void *)&F.l6, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        for (;;) {
            if (!((uint8_t)1)) goto lb29;
            {
                uint8_t t33 = gm_l3;
                uint8_t t34 = (uint8_t)!t33;
                if (!t34) goto ie32;
                {
                    gm_b16_8 t36;
                    uint64_t t38 = GM_LD(uintptr_t, (((uint8_t *)&F.l6) + 0) + 8);
                    uint8_t t39 = ((t38 == ((uint64_t)0ULL)));
                    if (!t39) goto ie37;
                    gm_b16_8 t41;
                    GM_ST(void *, (uint8_t *)&t41, (void *)(gm_s4hd92lokmtot));
                    GM_ST(uintptr_t, (uint8_t *)&t41 + 8, (uintptr_t)(3));
                    gm_b16_8 t42 = t41;
                    t36 = t42;
                    goto ix37;
ie37:;
                    gm_b16_8 t44;
                    GM_ST(void *, (uint8_t *)&t44, (void *)(gm_sfeb57kq1r2n4));
                    GM_ST(uintptr_t, (uint8_t *)&t44 + 8, (uintptr_t)(3));
                    gm_b16_8 t45 = t44;
                    t36 = t45;
ix37:;
                    gm_pfmaojsuds6a(cx, t36, gm_src3dhhc4laniae, 520, 24);
                    if (gm_unw.kind) {
                        goto gm_ll30;
                    }
                }
                goto ix32;
ie32:;
ix32:;
            }
            {
                gm_f_read_line_1oksar8((void *)&gm_l7, (void *)NULL, cx, NULL);
                if (gm_unw.kind) {
                    goto gm_ll30;
                }
            }
            {
                gm_b16_8 t46;
                uint8_t t52 = GM_LD(uint8_t, (((uint8_t *)&gm_l7) + 0));
                switch (t52) { case ((uint8_t)0ULL): goto ma50; case ((uint8_t)1ULL): goto ma48; case ((uint8_t)2ULL): goto ma49; default: goto ma51; }
ma48:;
                uint8_t t54 = GM_LD(uint8_t, (((uint8_t *)&gm_l7) + 0));
                if (t54 != ((uint8_t)1ULL)) goto mn53;
                {
                    uint64_t t57 = GM_LD(uintptr_t, (((uint8_t *)&F.l6) + 0) + 8);
                    uint8_t t58 = (t57 > ((uint64_t)0ULL));
                    if (!t58) goto ie56;
                    {
                        void * t60 = (void *)((uint8_t *)&F.l4);
                        gm_b16_8 t61 = GM_LD(gm_b16_8, (((uint8_t *)&F.l6) + 0));
                        gm_b16_8 t62 = t61;
                        gm_b16_8 t63 = GM_LD(gm_b16_8, ((uint8_t *)&t62));
                        gm_tabb710d9mq2e t64;
                        gm_f_dupe_g9iqpvn((void *)&t64, (void *)NULL, cx, NULL, t63, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_k093ni2ahjb)));
                        if (gm_unw.kind) {
                            goto gm_ll30;
                        }
                        gm_tabb710d9mq2e t65 = t64;
                        if (((((uint8_t *)&t65))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t65) + 8, gm_src3dhhc4laniae, 524, 58);
                        gm_b16_8 t66 = GM_LD(gm_b16_8, (((uint8_t *)&t65) + 8));
                        gm_b16_8 t67 = t66;
                        gm_b16_8 t68 = GM_LD(gm_b16_8, ((uint8_t *)&t67));
                        gm_b0_1 t69 = gm_f_submit_text_7dvfev6((void *)NULL, cx, NULL, t60, t68);
                        if (gm_unw.kind) {
                            goto gm_ll30;
                        }
                    }
                    goto ix56;
ie56:;
ix56:;
                }
                goto lb29;
                goto me47;
mn53:;
ma49:;
                uint8_t t71 = GM_LD(uint8_t, (((uint8_t *)&gm_l7) + 0));
                if (t71 != ((uint8_t)2ULL)) goto mn70;
                {
                    if (gm_pf454qfrpt6r(cx, gm_s0iid7nkkvf30, 54, gm_src3dhhc4laniae, 528, 17)) {
                        goto gm_ll30;
                    }
                }
                goto lc29;
                goto me47;
mn70:;
ma50:;
                uint8_t t74 = GM_LD(uint8_t, (((uint8_t *)&gm_l7) + 0));
                if (t74 != ((uint8_t)0ULL)) goto mn73;
                gm_b16_8 t75 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l7) + 8));
                gm_l8 = t75;
                gm_b16_8 t76 = gm_l8;
                t46 = t76;
                goto me47;
mn73:;
ma51:;
                gm_trap("no match arm matched the scrutinee", gm_src3dhhc4laniae, 522, 22);
me47:;
                gm_l9 = t46;
            }
            {
                gm_b16_8 t77 = gm_l9;
                gm_b16_8 t78 = gm_f_trim_newlines_9rj3i73(t77);
                gm_l10 = t78;
            }
            {
                uint64_t t80 = GM_LD(uintptr_t, (((uint8_t *)&F.l6) + 0) + 8);
                uint8_t t81 = ((t80 == ((uint64_t)0ULL)));
                if (!t81) goto ie79;
                {
                    gm_b16_8 t84 = gm_l10;
                    gm_b16_8 t85 = gm_f_trim_start_179crak(t84);
                    gm_b16_8 t86;
                    GM_ST(void *, (uint8_t *)&t86, (void *)(gm_s39mljh669qgl));
                    GM_ST(uintptr_t, (uint8_t *)&t86 + 8, (uintptr_t)(1));
                    uint8_t t87 = gm_f_starts_with_6vvt01e(t85, t86);
                    if (!t87) goto ie83;
                    {
                        void * t90 = (void *)((uint8_t *)&F.l4);
                        gm_b16_8 t91 = gm_l10;
                        gm_b16_8 t92 = gm_f_trim_fdrfe6i(t91);
                        uint8_t t93 = gm_f_command_7gn4uju((void *)NULL, cx, NULL, t90, t92);
                        if (gm_unw.kind) {
                            goto gm_ll30;
                        }
                        uint8_t t94 = (uint8_t)!t93;
                        if (!t94) goto ie89;
                        {
                            goto lb29;
                        }
                        goto ix89;
ie89:;
ix89:;
                    }
                    {
                        goto lc29;
                    }
                    goto ix83;
ie83:;
ix83:;
                }
                {
                    gm_b16_8 t97 = gm_l10;
                    uint8_t t98 = gm_f_is_blank_asoolmf((void *)NULL, cx, NULL, t97);
                    if (gm_unw.kind) {
                        goto gm_ll30;
                    }
                    if (!t98) goto ie96;
                    {
                        goto lc29;
                    }
                    goto ix96;
ie96:;
ix96:;
                }
                goto ix79;
ie79:;
                gm_b16_8 t101 = gm_l10;
                gm_b16_8 t102 = gm_f_trim_fdrfe6i(t101);
                gm_b16_8 t103 = t102;
                uint8_t t104 = (((GM_LD(uintptr_t, ((uint8_t *)&t103) + 8) == 7) && memcmp(GM_LD(void *, ((uint8_t *)&t103)), gm_sho8l3vae133f, 7) == 0));
                if (!t104) goto ie100;
                {
                    void * t106 = (void *)((uint8_t *)&F.l6);
                    gm_f_clear_h4369ui(t106);
                }
                {
                    goto lc29;
                }
                goto ix100;
ie100:;
ix100:;
ix79:;
            }
            {
                uint64_t t108 = GM_LD(uintptr_t, (((uint8_t *)&F.l6) + 0) + 8);
                uint8_t t109 = (t108 > ((uint64_t)0ULL));
                if (!t109) goto ie107;
                {
                    void * t111 = (void *)((uint8_t *)&F.l6);
                    gm_tftb1odc739t5 t112 = gm_f_push_t83rg2m(t111, ((uint8_t)10ULL));
                    gm_tftb1odc739t5 t113 = t112;
                    if (((((uint8_t *)&t113))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t113) + 1, gm_src3dhhc4laniae, 544, 36);
                    gm_b0_1 t114 = GM_LD(gm_b0_1, (((uint8_t *)&t113) + 1));
                }
                goto ix107;
ie107:;
ix107:;
            }
            {
                void * t115 = (void *)((uint8_t *)&F.l6);
                gm_b16_8 t116 = gm_l10;
                gm_tftb1odc739t5 t117 = gm_f_extend_0cs85gs(t115, t116);
                gm_tftb1odc739t5 t118 = t117;
                if (((((uint8_t *)&t118))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t118) + 1, gm_src3dhhc4laniae, 545, 9);
                gm_b0_1 t119 = GM_LD(gm_b0_1, (((uint8_t *)&t118) + 1));
            }
            {
                gm_b16_8 t120 = GM_LD(gm_b16_8, (((uint8_t *)&F.l6) + 0));
                gm_b16_8 t121 = t120;
                gm_b16_8 t122 = GM_LD(gm_b16_8, ((uint8_t *)&t121));
                gm_f_classify_rm38cme((void *)&F.l11, (void *)NULL, cx, NULL, t122);
                if (gm_unw.kind) {
                    goto gm_ll30;
                }
            }
            {
                uint8_t t124 = GM_LD(uint8_t, (((uint8_t *)&F.l11) + 56));
                if (!t124) goto ie123;
                uint8_t t125 = 0;
                gm_b16_8 t126 = gm_l10;
                gm_b16_8 t127 = gm_f_trim_fdrfe6i(t126);
                gm_b16_8 t128 = t127;
                uint64_t t129 = GM_LD(uintptr_t, ((uint8_t *)&t128) + 8);
                uint8_t t130 = ((t129 == ((uint64_t)0ULL)));
                if (t130) {
                    gm_b16_8 t131 = GM_LD(gm_b16_8, (((uint8_t *)&F.l6) + 0));
                    gm_b16_8 t132 = t131;
                    gm_b16_8 t133 = GM_LD(gm_b16_8, ((uint8_t *)&t132));
                    uint8_t t134 = gm_f_open_brackets_hicehud((void *)NULL, cx, NULL, t133);
                    if (gm_unw.kind) {
                        goto gm_ll30;
                    }
                    uint8_t t135 = (uint8_t)!t134;
                    t125 = t135;
                } else t125 = 0;
                uint8_t t136 = (uint8_t)!t125;
                if (!t136) goto ie123;
                {
                    goto lc29;
                }
                goto ix123;
ie123:;
ix123:;
            }
            {
                gm_b16_8 t138 = GM_LD(gm_b16_8, (((uint8_t *)&F.l6) + 0));
                gm_b16_8 t139 = t138;
                gm_b16_8 t140 = GM_LD(gm_b16_8, ((uint8_t *)&t139));
                gm_tabb710d9mq2e t141;
                gm_f_dupe_g9iqpvn((void *)&t141, (void *)NULL, cx, NULL, t140, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kif5rfegp72)));
                if (gm_unw.kind) {
                    goto gm_ll30;
                }
                gm_tabb710d9mq2e t142 = t141;
                if (((((uint8_t *)&t142))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t142) + 8, gm_src3dhhc4laniae, 549, 17);
                gm_b16_8 t143 = GM_LD(gm_b16_8, (((uint8_t *)&t142) + 8));
                gm_l12 = t143;
            }
            {
                void * t144 = (void *)((uint8_t *)&F.l6);
                gm_f_clear_h4369ui(t144);
            }
            {
                void * t145 = (void *)((uint8_t *)&F.l4);
                gm_b16_8 t146 = gm_l12;
                gm_b16_8 t147 = t146;
                gm_b16_8 t148 = GM_LD(gm_b16_8, ((uint8_t *)&t147));
                gm_b0_1 t149 = gm_f_submit_pah0u77((void *)NULL, cx, NULL, t145, t148, &(*(gm_t9feugf5hcdtg *)((uint8_t *)&F.l11)));
                if (gm_unw.kind) {
                    goto gm_ll30;
                }
            }
lc29:;
        }
gm_ll30:;
        if (gm_unw.frame == &F.h && gm_unw.label == 0) {
            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb29; }
            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc29; }
        }
        goto gm_lad;
lb29:;
    }
    {
        uint8_t t150 = 0;
        F.ret = ((uint8_t)0ULL);
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
__attribute__((aligned(4))) void gm_f_compile_rj09gp7(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_b16_8 a2, gm_b16_8 a3, const void * a4, gm_b16_8 a5, uint8_t a6) {
    struct gm_fr_compile_rj09gp7 F;
    void * gm_l0; /* r */
    gm_th2lqbo77pvsc gm_l1; /* new_decls */
    gm_b16_8 gm_l2; /* replaced */
    gm_b16_8 gm_l3; /* replay */
    gm_b16_8 gm_l5; /* effects */
    uint8_t gm_l6; /* sentinel */
    gm_b16_8 gm_l8; /* <tmp> */
    uint64_t gm_l9; /* <tmp> */
    gm_t6j6smj7qoctu gm_l10; /* c */
    uint8_t gm_l11; /* skip */
    gm_b16_8 gm_l12; /* <tmp> */
    uint64_t gm_l13; /* <tmp> */
    gm_b16_8 gm_l14; /* n */
    gm_b16_8 gm_l15; /* <tmp> */
    uint64_t gm_l16; /* <tmp> */
    gm_b16_8 gm_l17; /* x */
    uint64_t gm_l18; /* lo */
    gm_b16_8 gm_l19; /* input */
    gm_b16_8 gm_l20; /* d */
    gm_b16_8 gm_l21; /* <tmp> */
    uint64_t gm_l22; /* <tmp> */
    gm_b16_8 gm_l23; /* t */
    GM_STACK_CHECK(&F, 1415u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&gm_l1, a1, sizeof gm_l1);
    gm_l2 = a2;
    gm_l3 = a3;
    memcpy(&F.l4, a4, sizeof F.l4);
    gm_l5 = a5;
    gm_l6 = a6;
    {
        gm_f_list_uvfu1k1((void *)&F.l7, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l7);
        gm_b16_8 t3 = GM_LD(gm_b16_8, gm_cnohofor2q6);
        gm_tftb1odc739t5 t4 = gm_f_extend_0cs85gs(t2, t3);
        gm_tftb1odc739t5 t5 = t4;
        if (((((uint8_t *)&t5))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t5) + 1, gm_src3dhhc4laniae, 146, 5);
        gm_b0_1 t6 = GM_LD(gm_b0_1, (((uint8_t *)&t5) + 1));
    }
    {
        {
            void * t8 = gm_l0;
            gm_b16_8 t9 = GM_LD(gm_b16_8, ((((uint8_t *)t8) + 48) + 0));
            gm_l8 = t9;
        }
        {
            gm_l9 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t11 = gm_l9;
                uint64_t t12 = GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8);
                uint8_t t13 = (t11 < t12);
                if (!t13) goto lb10;
                {
                    uint64_t t16 = gm_l9;
                    uint64_t t17 = (uintptr_t)t16;
                    uint64_t t18 = GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8);
                    gm_t6j6smj7qoctu t21 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l8)) + t17 * 32));
                    gm_l10 = t21;
                    {
                        gm_l11 = ((uint8_t)0);
                    }
                    {
                        {
                            gm_b16_8 t24 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l10) + 0));
                            gm_l12 = t24;
                        }
                        {
                            gm_l13 = ((uint64_t)0ULL);
                        }
                        {
                            for (;;) {
                                uint64_t t26 = gm_l13;
                                uint64_t t27 = GM_LD(uintptr_t, ((uint8_t *)&gm_l12) + 8);
                                uint8_t t28 = (t26 < t27);
                                if (!t28) goto lb25;
                                {
                                    uint64_t t31 = gm_l13;
                                    uint64_t t32 = (uintptr_t)t31;
                                    uint64_t t33 = GM_LD(uintptr_t, ((uint8_t *)&gm_l12) + 8);
                                    gm_b16_8 t36 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l12)) + t32 * 16));
                                    gm_l14 = t36;
                                    {
                                        {
                                            gm_b16_8 t39 = gm_l2;
                                            gm_l15 = t39;
                                        }
                                        {
                                            gm_l16 = ((uint64_t)0ULL);
                                        }
                                        {
                                            for (;;) {
                                                uint64_t t41 = gm_l16;
                                                uint64_t t42 = GM_LD(uintptr_t, ((uint8_t *)&gm_l15) + 8);
                                                uint8_t t43 = (t41 < t42);
                                                if (!t43) goto lb40;
                                                {
                                                    uint64_t t46 = gm_l16;
                                                    uint64_t t47 = (uintptr_t)t46;
                                                    uint64_t t48 = GM_LD(uintptr_t, ((uint8_t *)&gm_l15) + 8);
                                                    gm_b16_8 t51 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l15)) + t47 * 16));
                                                    gm_l17 = t51;
                                                    {
                                                        gm_b16_8 t54 = gm_l14;
                                                        gm_b16_8 t55 = gm_l17;
                                                        gm_b16_8 t56 = t54;
                                                        gm_b16_8 t57 = t55;
                                                        uint8_t t58 = (gm_eq0iqiubfm7e(((uint8_t *)&t56), ((uint8_t *)&t57)));
                                                        if (!t58) goto ie53;
                                                        {
                                                            gm_l11 = ((uint8_t)1);
                                                        }
                                                        goto ix53;
ie53:;
ix53:;
                                                    }
                                                    goto me49;
mn50:;
                                                    gm_trap("no match arm matched the scrutinee", gm_src3dhhc4laniae, 150, 13);
me49:;
                                                }
lc40:;
                                                {
                                                    uint64_t t61 = gm_l16;
                                                    uint64_t r62; bool o63 = __builtin_add_overflow(t61, ((uint64_t)1ULL), &r62);
                                                    uint64_t t64 = r62;
                                                    gm_l16 = t64;
                                                }
                                            }
lb40:;
                                        }
                                    }
                                    goto me34;
mn35:;
                                    gm_trap("no match arm matched the scrutinee", gm_src3dhhc4laniae, 149, 9);
me34:;
                                }
lc25:;
                                {
                                    uint64_t t66 = gm_l13;
                                    uint64_t r67; bool o68 = __builtin_add_overflow(t66, ((uint64_t)1ULL), &r67);
                                    uint64_t t69 = r67;
                                    gm_l13 = t69;
                                }
                            }
lb25:;
                        }
                    }
                    {
                        uint8_t t71 = gm_l11;
                        if (!t71) goto ie70;
                        {
                            goto lc10;
                        }
                        goto ix70;
ie70:;
ix70:;
                    }
                    {
                        void * t73 = (void *)((uint8_t *)&F.l7);
                        gm_b16_8 t74 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l10) + 16));
                        gm_tftb1odc739t5 t75 = gm_f_extend_0cs85gs(t73, t74);
                        gm_tftb1odc739t5 t76 = t75;
                        if (((((uint8_t *)&t76))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t76) + 1, gm_src3dhhc4laniae, 155, 9);
                        gm_b0_1 t77 = GM_LD(gm_b0_1, (((uint8_t *)&t76) + 1));
                    }
                    {
                        void * t78 = (void *)((uint8_t *)&F.l7);
                        gm_b16_8 t79;
                        GM_ST(void *, (uint8_t *)&t79, (void *)(gm_s8dbdqj1sqibj));
                        GM_ST(uintptr_t, (uint8_t *)&t79 + 8, (uintptr_t)(2));
                        gm_tftb1odc739t5 t80 = gm_f_extend_0cs85gs(t78, t79);
                        gm_tftb1odc739t5 t81 = t80;
                        if (((((uint8_t *)&t81))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t81) + 1, gm_src3dhhc4laniae, 156, 9);
                        gm_b0_1 t82 = GM_LD(gm_b0_1, (((uint8_t *)&t81) + 1));
                    }
                    goto me19;
mn20:;
                    gm_trap("no match arm matched the scrutinee", gm_src3dhhc4laniae, 147, 5);
me19:;
                }
lc10:;
                {
                    uint64_t t84 = gm_l9;
                    uint64_t r85; bool o86 = __builtin_add_overflow(t84, ((uint64_t)1ULL), &r85);
                    uint64_t t87 = r85;
                    gm_l9 = t87;
                }
            }
lb10:;
        }
    }
    {
        gm_l18 = ((uint64_t)0ULL);
    }
    {
        gm_b16_8 t88;
        GM_ST(void *, (uint8_t *)&t88, (void *)(gm_sr4qbrk6tlgaa));
        GM_ST(uintptr_t, (uint8_t *)&t88 + 8, (uintptr_t)(0));
        gm_l19 = t88;
    }
    {
        if (!((((uint8_t *)&gm_l1))[16] != 0)) goto ie89;
        gm_b16_8 t90 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l1));
        gm_l20 = t90;
        {
            uint64_t t92 = GM_LD(uintptr_t, (((uint8_t *)&F.l7) + 0) + 8);
            gm_l18 = t92;
        }
        {
            gm_b16_8 t93 = gm_l20;
            gm_l19 = t93;
        }
        {
            void * t94 = (void *)((uint8_t *)&F.l7);
            gm_b16_8 t95 = gm_l20;
            gm_tftb1odc739t5 t96 = gm_f_extend_0cs85gs(t94, t95);
            gm_tftb1odc739t5 t97 = t96;
            if (((((uint8_t *)&t97))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t97) + 1, gm_src3dhhc4laniae, 163, 9);
            gm_b0_1 t98 = GM_LD(gm_b0_1, (((uint8_t *)&t97) + 1));
        }
        {
            void * t99 = (void *)((uint8_t *)&F.l7);
            gm_b16_8 t100;
            GM_ST(void *, (uint8_t *)&t100, (void *)(gm_s8dbdqj1sqibj));
            GM_ST(uintptr_t, (uint8_t *)&t100 + 8, (uintptr_t)(2));
            gm_tftb1odc739t5 t101 = gm_f_extend_0cs85gs(t99, t100);
            gm_tftb1odc739t5 t102 = t101;
            if (((((uint8_t *)&t102))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t102) + 1, gm_src3dhhc4laniae, 164, 9);
            gm_b0_1 t103 = GM_LD(gm_b0_1, (((uint8_t *)&t102) + 1));
        }
        goto ix89;
ie89:;
ix89:;
    }
    {
        void * t104 = (void *)((uint8_t *)&F.l7);
        gm_b16_8 t105 = gm_l5;
        gm_tnpifv20irnph t106;
        GM_ST(gm_b16_8, ((uint8_t *)&t106 + 0), t105);
        gm_tftb1odc739t5 t107 = gm_f_write_0gtanrr(t104, t106);
        gm_tftb1odc739t5 t108 = t107;
        if (((((uint8_t *)&t108))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t108) + 1, gm_src3dhhc4laniae, 166, 5);
        gm_b0_1 t109 = GM_LD(gm_b0_1, (((uint8_t *)&t108) + 1));
    }
    {
        {
            gm_b16_8 t111 = gm_l3;
            gm_l21 = t111;
        }
        {
            gm_l22 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t113 = gm_l22;
                uint64_t t114 = GM_LD(uintptr_t, ((uint8_t *)&gm_l21) + 8);
                uint8_t t115 = (t113 < t114);
                if (!t115) goto lb112;
                {
                    uint64_t t118 = gm_l22;
                    uint64_t t119 = (uintptr_t)t118;
                    uint64_t t120 = GM_LD(uintptr_t, ((uint8_t *)&gm_l21) + 8);
                    gm_b16_8 t123 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l21)) + t119 * 16));
                    gm_l23 = t123;
                    {
                        void * t125 = (void *)((uint8_t *)&F.l7);
                        gm_b16_8 t126;
                        GM_ST(void *, (uint8_t *)&t126, (void *)(gm_som0q53jqvjh3));
                        GM_ST(uintptr_t, (uint8_t *)&t126 + 8, (uintptr_t)(4));
                        gm_tftb1odc739t5 t127 = gm_f_extend_0cs85gs(t125, t126);
                        gm_tftb1odc739t5 t128 = t127;
                        if (((((uint8_t *)&t128))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t128) + 1, gm_src3dhhc4laniae, 168, 9);
                        gm_b0_1 t129 = GM_LD(gm_b0_1, (((uint8_t *)&t128) + 1));
                    }
                    {
                        void * t130 = (void *)((uint8_t *)&F.l7);
                        gm_b16_8 t131 = gm_l23;
                        gm_tftb1odc739t5 t132 = gm_f_extend_0cs85gs(t130, t131);
                        gm_tftb1odc739t5 t133 = t132;
                        if (((((uint8_t *)&t133))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t133) + 1, gm_src3dhhc4laniae, 169, 9);
                        gm_b0_1 t134 = GM_LD(gm_b0_1, (((uint8_t *)&t133) + 1));
                    }
                    {
                        void * t135 = (void *)((uint8_t *)&F.l7);
                        gm_tftb1odc739t5 t136 = gm_f_push_t83rg2m(t135, ((uint8_t)10ULL));
                        gm_tftb1odc739t5 t137 = t136;
                        if (((((uint8_t *)&t137))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t137) + 1, gm_src3dhhc4laniae, 170, 9);
                        gm_b0_1 t138 = GM_LD(gm_b0_1, (((uint8_t *)&t137) + 1));
                    }
                    goto me121;
mn122:;
                    gm_trap("no match arm matched the scrutinee", gm_src3dhhc4laniae, 167, 5);
me121:;
                }
lc112:;
                {
                    uint64_t t140 = gm_l22;
                    uint64_t r141; bool o142 = __builtin_add_overflow(t140, ((uint64_t)1ULL), &r141);
                    uint64_t t143 = r141;
                    gm_l22 = t143;
                }
            }
lb112:;
        }
    }
    {
        uint8_t t145 = gm_l6;
        if (!t145) goto ie144;
        uint64_t t146 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
        uint8_t t147 = (t146 > ((uint64_t)0ULL));
        if (!t147) goto ie144;
        {
            void * t149 = (void *)((uint8_t *)&F.l7);
            gm_t6j6smj7qoctu t150;
            gm_b16_8 t151 = GM_LD(gm_b16_8, gm_cdgbrlsjvrb);
            GM_ST(gm_b16_8, (uint8_t *)&t150 + 0, t151);
            gm_b16_8 t152 = GM_LD(gm_b16_8, gm_cdgbrlsjvrb);
            GM_ST(gm_b16_8, (uint8_t *)&t150 + 16, t152);
            gm_tftb1odc739t5 t153 = gm_f_write_nre2qb9(t149, &t150);
            gm_tftb1odc739t5 t154 = t153;
            if (((((uint8_t *)&t154))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t154) + 1, gm_src3dhhc4laniae, 173, 9);
            gm_b0_1 t155 = GM_LD(gm_b0_1, (((uint8_t *)&t154) + 1));
        }
        goto ix144;
ie144:;
ix144:;
    }
    {
        if (!((((uint8_t *)&F.l4))[48] != 0)) goto ie156;
        gm_tfeab279c16rr t157 = GM_LD(gm_tfeab279c16rr, ((uint8_t *)&F.l4));
        F.l24 = t157;
        {
            void * t159 = (void *)((uint8_t *)&F.l7);
            gm_b16_8 t160 = GM_LD(gm_b16_8, (((uint8_t *)&F.l24) + 0));
            gm_tftb1odc739t5 t161 = gm_f_extend_0cs85gs(t159, t160);
            gm_tftb1odc739t5 t162 = t161;
            if (((((uint8_t *)&t162))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t162) + 1, gm_src3dhhc4laniae, 176, 9);
            gm_b0_1 t163 = GM_LD(gm_b0_1, (((uint8_t *)&t162) + 1));
        }
        {
            uint64_t t164 = GM_LD(uintptr_t, (((uint8_t *)&F.l7) + 0) + 8);
            gm_l18 = t164;
        }
        {
            gm_b16_8 t165 = GM_LD(gm_b16_8, (((uint8_t *)&F.l24) + 16));
            gm_l19 = t165;
        }
        {
            void * t166 = (void *)((uint8_t *)&F.l7);
            gm_b16_8 t167 = GM_LD(gm_b16_8, (((uint8_t *)&F.l24) + 16));
            gm_tftb1odc739t5 t168 = gm_f_extend_0cs85gs(t166, t167);
            gm_tftb1odc739t5 t169 = t168;
            if (((((uint8_t *)&t169))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t169) + 1, gm_src3dhhc4laniae, 179, 9);
            gm_b0_1 t170 = GM_LD(gm_b0_1, (((uint8_t *)&t169) + 1));
        }
        {
            void * t171 = (void *)((uint8_t *)&F.l7);
            gm_b16_8 t172 = GM_LD(gm_b16_8, (((uint8_t *)&F.l24) + 32));
            gm_tftb1odc739t5 t173 = gm_f_extend_0cs85gs(t171, t172);
            gm_tftb1odc739t5 t174 = t173;
            if (((((uint8_t *)&t174))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t174) + 1, gm_src3dhhc4laniae, 180, 9);
            gm_b0_1 t175 = GM_LD(gm_b0_1, (((uint8_t *)&t174) + 1));
        }
        goto ix156;
ie156:;
ix156:;
    }
    {
        void * t176 = (void *)((uint8_t *)&F.l7);
        gm_b16_8 t177;
        GM_ST(void *, (uint8_t *)&t177, (void *)(gm_sikpqtja8pqpv));
        GM_ST(uintptr_t, (uint8_t *)&t177 + 8, (uintptr_t)(2));
        gm_tftb1odc739t5 t178 = gm_f_extend_0cs85gs(t176, t177);
        gm_tftb1odc739t5 t179 = t178;
        if (((((uint8_t *)&t179))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t179) + 1, gm_src3dhhc4laniae, 182, 5);
        gm_b0_1 t180 = GM_LD(gm_b0_1, (((uint8_t *)&t179) + 1));
    }
    {
        gm_b16_8 t181 = GM_LD(gm_b16_8, (((uint8_t *)&F.l7) + 0));
        gm_b16_8 t182 = t181;
        gm_b16_8 t183 = GM_LD(gm_b16_8, ((uint8_t *)&t182));
        void * t184 = gm_l0;
        gm_t9qkh0rsooc8d t185 = GM_LD(gm_t9qkh0rsooc8d, ((((uint8_t *)t184) + 0) + 1));
        uint64_t t186 = gm_l18;
        uint32_t t187 = (uint32_t)t186;
        gm_b16_8 t188 = gm_l19;
        gm_f_check_source_nh9qcnk((void *)&F.l25, (void *)NULL, cx, NULL, t183, t185, t187, t188);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t189 = (void *)((uint8_t *)&F.l7);
        gm_f_deinit_r70pji5(t189);
    }
    {
        memmove(gm_out, ((uint8_t *)&F.l25), 64);
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_space_pq8dr45(uint8_t a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_space_pq8dr45 F;
    uint8_t gm_l0; /* c */
    GM_STACK_CHECK(&F, 19u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t t1 = gm_l0;
    gm_t4q2b5ibsrdik t2;
    GM_ST(uint8_t, ((uint8_t *)&t2 + 0), ((uint8_t)32ULL));
    GM_ST(uint8_t, ((uint8_t *)&t2 + 1), ((uint8_t)9ULL));
    GM_ST(uint8_t, ((uint8_t *)&t2 + 2), ((uint8_t)10ULL));
    GM_ST(uint8_t, ((uint8_t *)&t2 + 3), ((uint8_t)13ULL));
    GM_ST(uint8_t, ((uint8_t *)&t2 + 4), ((uint8_t)11ULL));
    GM_ST(uint8_t, ((uint8_t *)&t2 + 5), ((uint8_t)12ULL));
    gm_t4q2b5ibsrdik t3 = t2;
    uint8_t t4 = 0;
    uint8_t t5 = t1;
    if (gm_eqjmccqdb90f(((uint8_t *)&t3) + 0, ((uint8_t *)&t5))) t4 = 1;
    if (gm_eqjmccqdb90f(((uint8_t *)&t3) + 1, ((uint8_t *)&t5))) t4 = 1;
    if (gm_eqjmccqdb90f(((uint8_t *)&t3) + 2, ((uint8_t *)&t5))) t4 = 1;
    if (gm_eqjmccqdb90f(((uint8_t *)&t3) + 3, ((uint8_t *)&t5))) t4 = 1;
    if (gm_eqjmccqdb90f(((uint8_t *)&t3) + 4, ((uint8_t *)&t5))) t4 = 1;
    if (gm_eqjmccqdb90f(((uint8_t *)&t3) + 5, ((uint8_t *)&t5))) t4 = 1;
    uint8_t t6 = (uint8_t)(t4);
    F.ret = t6;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_list_session_6f2qqqv(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_list_session_6f2qqqv F;
    void * gm_l0; /* r */
    gm_b16_8 gm_l1; /* <tmp> */
    uint64_t gm_l2; /* <tmp> */
    gm_t6j6smj7qoctu gm_l3; /* c */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    GM_STACK_CHECK(&F, 386u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        {
            void * t3 = gm_l0;
            gm_b16_8 t4 = GM_LD(gm_b16_8, ((((uint8_t *)t3) + 48) + 0));
            gm_l1 = t4;
        }
        {
            gm_l2 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t6 = gm_l2;
                uint64_t t7 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                uint8_t t8 = (t6 < t7);
                if (!t8) goto lb5;
                {
                    uint64_t t11 = gm_l2;
                    uint64_t t12 = (uintptr_t)t11;
                    uint64_t t13 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                    gm_t6j6smj7qoctu t16 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t12 * 32));
                    gm_l3 = t16;
                    {
                        gm_b16_8 t18 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 16));
                        gm_b16_8 t19 = t18;
                        if (gm_pf70j0q3sgt1(cx, &gm_ty0iqiubfm7e, ((uint8_t *)&t19), gm_src3dhhc4laniae, 442, 31)) {
                            goto gm_ll9;
                        }
                        if (gm_pf1m4b8q4e78(cx, gm_so514oj5nbhcu, 1, gm_src3dhhc4laniae, 442, 31)) {
                            goto gm_ll9;
                        }
                    }
                    goto me14;
mn15:;
                    gm_trap("no match arm matched the scrutinee", gm_src3dhhc4laniae, 442, 5);
me14:;
                }
lc5:;
                {
                    uint64_t t21 = gm_l2;
                    uint64_t r22; bool o23 = __builtin_add_overflow(t21, ((uint64_t)1ULL), &r22);
                    uint64_t t24 = r22;
                    gm_l2 = t24;
                }
            }
gm_ll9:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb5; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc5; }
            }
            goto gm_lad;
lb5:;
        }
    }
    {
        {
            void * t26 = gm_l0;
            gm_b16_8 t27 = GM_LD(gm_b16_8, ((((uint8_t *)t26) + 88) + 0));
            gm_l4 = t27;
        }
        {
            gm_l5 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t29 = gm_l5;
                uint64_t t30 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                uint8_t t31 = (t29 < t30);
                if (!t31) goto lb28;
                {
                    uint64_t t34 = gm_l5;
                    uint64_t t35 = (uintptr_t)t34;
                    uint64_t t36 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                    gm_tk9n3vlila9qg t39 = GM_LD(gm_tk9n3vlila9qg, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t35 * 40));
                    F.l6 = t39;
                    {
                        gm_b16_8 t41 = GM_LD(gm_b16_8, (((uint8_t *)&F.l6) + 0));
                        gm_b16_8 t42 = t41;
                        if (gm_pf70j0q3sgt1(cx, &gm_ty0iqiubfm7e, ((uint8_t *)&t42), gm_src3dhhc4laniae, 443, 31)) {
                            goto gm_ll32;
                        }
                        if (gm_pf1m4b8q4e78(cx, gm_so514oj5nbhcu, 1, gm_src3dhhc4laniae, 443, 31)) {
                            goto gm_ll32;
                        }
                    }
                    goto me37;
mn38:;
                    gm_trap("no match arm matched the scrutinee", gm_src3dhhc4laniae, 443, 5);
me37:;
                }
lc28:;
                {
                    uint64_t t44 = gm_l5;
                    uint64_t r45; bool o46 = __builtin_add_overflow(t44, ((uint64_t)1ULL), &r45);
                    uint64_t t47 = r45;
                    gm_l5 = t47;
                }
            }
gm_ll32:;
            if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb28; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc28; }
            }
            goto gm_lad;
lb28:;
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
__attribute__((aligned(4))) uint8_t gm_f_load_file_3kdl78m(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1) {
    struct gm_fr_load_file_3kdl78m F;
    void * gm_l0; /* r */
    gm_b16_8 gm_l1; /* path_text */
    gm_b16_8 gm_l2; /* path */
    gm_tj5jn79kq9pnm gm_l3; /* e */
    gm_b16_8 gm_l4; /* bytes */
    gm_b16_8 gm_l5; /* text */
    gm_b16_8 gm_l7; /* items */
    uint8_t gm_l9; /* has_main */
    gm_b16_8 gm_l10; /* <tmp> */
    uint64_t gm_l11; /* <tmp> */
    uint8_t gm_l13; /* is_main */
    gm_b16_8 gm_l14; /* <tmp> */
    uint64_t gm_l15; /* <tmp> */
    gm_b16_8 gm_l16; /* n */
    gm_b16_8 gm_l17; /* load_text */
    gm_b16_8 gm_l19; /* <tmp> */
    uint64_t gm_l20; /* <tmp> */
    gm_b16_8 gm_l22; /* msg */
    GM_STACK_CHECK(&F, 1446u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        gm_b16_8 t2 = gm_l1;
        gm_b16_8 t3 = gm_f_trim_fdrfe6i(t2);
        gm_l2 = t3;
    }
    {
        gm_b16_8 t4 = gm_l2;
        gm_tgvh15qshf643 t5;
        gm_f_read_all_r9rh9qd((void *)&t5, (void *)NULL, cx, NULL, t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tgvh15qshf643 t6 = t5;
        gm_b16_8 t7;
        if (!((((uint8_t *)&t6))[0] != 0)) {
            gm_b16_8 t8 = GM_LD(gm_b16_8, (((uint8_t *)&t6) + 8));
            t7 = t8;
        } else {
            gm_tj5jn79kq9pnm t9 = GM_LD(gm_tj5jn79kq9pnm, (((uint8_t *)&t6) + 8));
            gm_l3 = t9;
            {
                gm_b16_8 t11 = gm_l2;
                gm_b16_8 t12 = t11;
                gm_tj5jn79kq9pnm t13 = gm_l3;
                gm_b16_8 t14 = gm_f_read_error_text_1lgi2bl((void *)NULL, cx, NULL, t13);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_b16_8 t15 = t14;
                if (gm_pf454qfrpt6r(cx, gm_s3vfg72f8aupk, 19, gm_src3dhhc4laniae, 406, 9)) {
                    goto gm_lad;
                }
                if (gm_pfu8dq5bikbh(cx, &gm_ty0iqiubfm7e, ((uint8_t *)&t12), gm_src3dhhc4laniae, 406, 9)) {
                    goto gm_lad;
                }
                if (gm_pf454qfrpt6r(cx, gm_slgmqgkioa4u6, 2, gm_src3dhhc4laniae, 406, 9)) {
                    goto gm_lad;
                }
                if (gm_pfu8dq5bikbh(cx, &gm_ty0iqiubfm7e, ((uint8_t *)&t15), gm_src3dhhc4laniae, 406, 9)) {
                    goto gm_lad;
                }
                if (gm_pf454qfrpt6r(cx, gm_so514oj5nbhcu, 1, gm_src3dhhc4laniae, 406, 9)) {
                    goto gm_lad;
                }
            }
            uint8_t t16 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        gm_l4 = t7;
    }
    {
        gm_b16_8 t18 = gm_l4;
        gm_b16_8 t19 = t18;
        gm_b16_8 t20 = GM_LD(gm_b16_8, ((uint8_t *)&t19));
        uint8_t t21 = gm_f_utf8_valid_a8oc1t8(t20);
        uint8_t t22 = (uint8_t)!t21;
        if (!t22) goto ie17;
        {
            gm_b16_8 t24 = gm_l2;
            gm_b16_8 t25 = t24;
            if (gm_pf454qfrpt6r(cx, gm_s3vfg72f8aupk, 19, gm_src3dhhc4laniae, 410, 9)) {
                goto gm_lad;
            }
            if (gm_pfu8dq5bikbh(cx, &gm_ty0iqiubfm7e, ((uint8_t *)&t25), gm_src3dhhc4laniae, 410, 9)) {
                goto gm_lad;
            }
            if (gm_pf454qfrpt6r(cx, gm_s54emlnl8lmoe, 37, gm_src3dhhc4laniae, 410, 9)) {
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
        gm_b16_8 t27 = gm_l4;
        gm_b16_8 t28 = t27;
        gm_b16_8 t29 = GM_LD(gm_b16_8, ((uint8_t *)&t28));
        gm_l5 = t29;
    }
    {
        gm_b16_8 t30 = gm_l5;
        gm_f_classify_rm38cme((void *)&F.l6, (void *)NULL, cx, NULL, t30);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_th2lqbo77pvsc t31 = GM_LD(gm_th2lqbo77pvsc, (((uint8_t *)&F.l6) + 0));
        gm_th2lqbo77pvsc t32 = t31;
        gm_b16_8 t33;
        if (((((uint8_t *)&t32))[16] != 0)) {
            gm_b16_8 t34 = GM_LD(gm_b16_8, (((uint8_t *)&t32) + 0));
            t33 = t34;
        } else {
            {
                gm_b16_8 t36 = gm_l5;
                void * t37 = gm_l0;
                gm_t9qkh0rsooc8d t38 = GM_LD(gm_t9qkh0rsooc8d, ((((uint8_t *)t37) + 0) + 2));
                gm_b16_8 t39 = gm_f_syntax_errors_fk23f5t((void *)NULL, cx, NULL, t36, t38);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_b0_1 t40 = gm_f_show_861qdlm((void *)NULL, cx, NULL, t39);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            uint8_t t41 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        gm_l7 = t33;
    }
    {
        gm_f_list_f0gjjbh_2((void *)&F.l8, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_l9 = ((uint8_t)0);
    }
    {
        {
            gm_b16_8 t43 = gm_l7;
            gm_l10 = t43;
        }
        {
            gm_l11 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t45 = gm_l11;
                uint64_t t46 = GM_LD(uintptr_t, ((uint8_t *)&gm_l10) + 8);
                uint8_t t47 = (t45 < t46);
                if (!t47) goto lb44;
                {
                    uint64_t t50 = gm_l11;
                    uint64_t t51 = (uintptr_t)t50;
                    uint64_t t52 = GM_LD(uintptr_t, ((uint8_t *)&gm_l10) + 8);
                    gm_tk9n3vlila9qg t55 = GM_LD(gm_tk9n3vlila9qg, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l10)) + t51 * 40));
                    F.l12 = t55;
                    {
                        gm_l13 = ((uint8_t)0);
                    }
                    {
                        {
                            gm_b16_8 t58 = GM_LD(gm_b16_8, (((uint8_t *)&F.l12) + 0));
                            gm_l14 = t58;
                        }
                        {
                            gm_l15 = ((uint64_t)0ULL);
                        }
                        {
                            for (;;) {
                                uint64_t t60 = gm_l15;
                                uint64_t t61 = GM_LD(uintptr_t, ((uint8_t *)&gm_l14) + 8);
                                uint8_t t62 = (t60 < t61);
                                if (!t62) goto lb59;
                                {
                                    uint64_t t65 = gm_l15;
                                    uint64_t t66 = (uintptr_t)t65;
                                    uint64_t t67 = GM_LD(uintptr_t, ((uint8_t *)&gm_l14) + 8);
                                    gm_b16_8 t70 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l14)) + t66 * 16));
                                    gm_l16 = t70;
                                    {
                                        uint8_t t73 = (((GM_LD(uintptr_t, ((uint8_t *)&gm_l16) + 8) == 4) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l16)), gm_sog4rksrmsqd2, 4) == 0));
                                        if (!t73) goto ie72;
                                        {
                                            gm_l13 = ((uint8_t)1);
                                        }
                                        goto ix72;
ie72:;
ix72:;
                                    }
                                    goto me68;
mn69:;
                                    gm_trap("no match arm matched the scrutinee", gm_src3dhhc4laniae, 423, 9);
me68:;
                                }
lc59:;
                                {
                                    uint64_t t76 = gm_l15;
                                    uint64_t r77; bool o78 = __builtin_add_overflow(t76, ((uint64_t)1ULL), &r77);
                                    uint64_t t79 = r77;
                                    gm_l15 = t79;
                                }
                            }
lb59:;
                        }
                    }
                    {
                        uint8_t t81 = gm_l13;
                        if (!t81) goto ie80;
                        {
                            gm_l9 = ((uint8_t)1);
                        }
                        goto ix80;
ie80:;
                        {
                            void * t84 = (void *)((uint8_t *)&F.l8);
                            gm_tftb1odc739t5 t85 = gm_f_push_1e500nc_2(t84, &(*(gm_tk9n3vlila9qg *)((uint8_t *)&F.l12)));
                            gm_tftb1odc739t5 t86 = t85;
                            if (((((uint8_t *)&t86))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t86) + 1, gm_src3dhhc4laniae, 426, 47);
                            gm_b0_1 t87 = GM_LD(gm_b0_1, (((uint8_t *)&t86) + 1));
                        }
ix80:;
                    }
                    goto me53;
mn54:;
                    gm_trap("no match arm matched the scrutinee", gm_src3dhhc4laniae, 421, 5);
me53:;
                }
lc44:;
                {
                    uint64_t t89 = gm_l11;
                    uint64_t r90; bool o91 = __builtin_add_overflow(t89, ((uint64_t)1ULL), &r90);
                    uint64_t t92 = r90;
                    gm_l11 = t92;
                }
            }
lb44:;
        }
    }
    {
        gm_b16_8 t93 = gm_l5;
        gm_l17 = t93;
    }
    {
        uint8_t t95 = gm_l9;
        if (!t95) goto ie94;
        {
            gm_b16_8 t97 = gm_l2;
            gm_b16_8 t98 = t97;
            if (gm_pf454qfrpt6r(cx, gm_sjbjet56e96co, 16, gm_src3dhhc4laniae, 430, 9)) {
                goto gm_lad;
            }
            if (gm_pfu8dq5bikbh(cx, &gm_ty0iqiubfm7e, ((uint8_t *)&t98), gm_src3dhhc4laniae, 430, 9)) {
                goto gm_lad;
            }
            if (gm_pf454qfrpt6r(cx, gm_ssulg3jrm3q2v, 15, gm_src3dhhc4laniae, 430, 9)) {
                goto gm_lad;
            }
        }
        {
            gm_f_list_epvducg((void *)&F.l18, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            {
                gm_b16_8 t100 = GM_LD(gm_b16_8, (((uint8_t *)&F.l8) + 0));
                gm_l19 = t100;
            }
            {
                gm_l20 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t102 = gm_l20;
                    uint64_t t103 = GM_LD(uintptr_t, ((uint8_t *)&gm_l19) + 8);
                    uint8_t t104 = (t102 < t103);
                    if (!t104) goto lb101;
                    {
                        uint64_t t107 = gm_l20;
                        uint64_t t108 = (uintptr_t)t107;
                        uint64_t t109 = GM_LD(uintptr_t, ((uint8_t *)&gm_l19) + 8);
                        gm_tk9n3vlila9qg t112 = GM_LD(gm_tk9n3vlila9qg, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l19)) + t108 * 40));
                        F.l21 = t112;
                        {
                            void * t114 = (void *)((uint8_t *)&F.l18);
                            gm_b16_8 t115 = GM_LD(gm_b16_8, (((uint8_t *)&F.l21) + 16));
                            gm_tftb1odc739t5 t116 = gm_f_push_3ef746b(t114, t115);
                            gm_tftb1odc739t5 t117 = t116;
                            if (((((uint8_t *)&t117))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t117) + 1, gm_src3dhhc4laniae, 432, 31);
                            gm_b0_1 t118 = GM_LD(gm_b0_1, (((uint8_t *)&t117) + 1));
                        }
                        goto me110;
mn111:;
                        gm_trap("no match arm matched the scrutinee", gm_src3dhhc4laniae, 432, 9);
me110:;
                    }
lc101:;
                    {
                        uint64_t t120 = gm_l20;
                        uint64_t r121; bool o122 = __builtin_add_overflow(t120, ((uint64_t)1ULL), &r121);
                        uint64_t t123 = r121;
                        gm_l20 = t123;
                    }
                }
lb101:;
            }
        }
        {
            gm_b16_8 t124 = GM_LD(gm_b16_8, (((uint8_t *)&F.l18) + 0));
            gm_b16_8 t125 = t124;
            gm_b16_8 t126 = GM_LD(gm_b16_8, ((uint8_t *)&t125));
            gm_b16_8 t127;
            GM_ST(void *, (uint8_t *)&t127, (void *)(gm_s8dbdqj1sqibj));
            GM_ST(uintptr_t, (uint8_t *)&t127 + 8, (uintptr_t)(2));
            gm_tabb710d9mq2e t128;
            gm_f_join_sfknljb((void *)&t128, (void *)NULL, cx, NULL, t126, t127);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t129 = t128;
            if (((((uint8_t *)&t129))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t129) + 8, gm_src3dhhc4laniae, 433, 21);
            gm_b16_8 t130 = GM_LD(gm_b16_8, (((uint8_t *)&t129) + 8));
            gm_b16_8 t131 = t130;
            gm_b16_8 t132 = GM_LD(gm_b16_8, ((uint8_t *)&t131));
            gm_l17 = t132;
        }
        goto ix94;
ie94:;
ix94:;
    }
    {
        void * t133 = gm_l0;
        gm_b16_8 t134 = gm_l17;
        gm_b16_8 t135 = GM_LD(gm_b16_8, (((uint8_t *)&F.l8) + 0));
        gm_b16_8 t136 = t135;
        gm_b16_8 t137 = GM_LD(gm_b16_8, ((uint8_t *)&t136));
        gm_th2lqbo77pvsc t138;
        gm_f_try_decls_23vcohd((void *)&t138, (void *)NULL, cx, NULL, t133, t134, t137);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_th2lqbo77pvsc t139 = t138;
        gm_b16_8 t140;
        if (((((uint8_t *)&t139))[16] != 0)) {
            gm_b16_8 t141 = GM_LD(gm_b16_8, (((uint8_t *)&t139) + 0));
            t140 = t141;
        } else {
            uint8_t t142 = 0;
            F.ret = ((uint8_t)1);
            goto gm_fret;
        }
        gm_l22 = t140;
    }
    {
        gm_b16_8 t143 = gm_l22;
        gm_b0_1 t144 = gm_f_show_861qdlm((void *)NULL, cx, NULL, t143);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        uint8_t t145 = 0;
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
inline __attribute__((aligned(4))) void gm_f_new_repl_hdsg2o4(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_new_repl_hdsg2o4 F;
    GM_STACK_CHECK(&F, 168u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memcpy(&F.l0, a0, sizeof F.l0);
    memset((uint8_t *)&(*(gm_tec68k60rd12v *)gm_out) + 172, 0, 4);
    gm_tvcg39brotose t1 = F.l0;
    gm_trqfvmjtt4sij t2;
    gm_f_list_1t36jvm((void *)&t2, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_trqfvmjtt4sij t3;
    gm_f_list_93qogsr((void *)&t3, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_trqfvmjtt4sij t4;
    gm_f_list_e6ie1fq_2((void *)&t4, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    GM_ST(gm_tvcg39brotose, (uint8_t *)&(*(gm_tec68k60rd12v *)gm_out) + 0, t1);
    GM_ST(gm_trqfvmjtt4sij, (uint8_t *)&(*(gm_tec68k60rd12v *)gm_out) + 48, t2);
    GM_ST(gm_trqfvmjtt4sij, (uint8_t *)&(*(gm_tec68k60rd12v *)gm_out) + 88, t3);
    GM_ST(gm_trqfvmjtt4sij, (uint8_t *)&(*(gm_tec68k60rd12v *)gm_out) + 128, t4);
    GM_ST(uint32_t, (uint8_t *)&(*(gm_tec68k60rd12v *)gm_out) + 168, ((uint32_t)0ULL));
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
__attribute__((aligned(4))) void gm_f_parse_options_9st61k7(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_parse_options_9st61k7 F;
    gm_b16_8 gm_l0; /* args */
    uint64_t gm_l2; /* i */
    gm_b16_8 gm_l3; /* a */
    gm_b16_8 gm_l5; /* value */
    gm_b16_8 gm_l6; /* message */
    GM_STACK_CHECK(&F, 1206u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        memset((uint8_t *)&F.l1 + 3, 0, 5);
        GM_ST(uint8_t, (uint8_t *)&F.l1 + 0, ((uint8_t)0));
        gm_t9qkh0rsooc8d t2 = GM_LD(gm_t9qkh0rsooc8d, gm_celpgpm1fi8);
        GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&F.l1 + 1, t2);
        gm_t9qkh0rsooc8d t3 = GM_LD(gm_t9qkh0rsooc8d, gm_ckbflbrril1);
        GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&F.l1 + 2, t3);
        gm_th2lqbo77pvsc t4;
        memset(&t4, 0, sizeof t4);
        GM_ST(gm_th2lqbo77pvsc, (uint8_t *)&F.l1 + 8, t4);
        gm_b16_8 t5 = GM_LD(gm_b16_8, gm_c7983l5vkhl);
        GM_ST(gm_b16_8, (uint8_t *)&F.l1 + 32, t5);
    }
    {
        gm_l2 = ((uint64_t)0ULL);
    }
    {
        for (;;) {
            uint64_t t7 = gm_l2;
            uint64_t t8 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            uint8_t t9 = (t7 < t8);
            if (!t9) goto lb6;
            {
                uint64_t t12 = gm_l2;
                uint64_t t13 = (uintptr_t)t12;
                uint64_t t14 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                if (t13 >= (uintptr_t)(t14)) gm_trap_index(t13, t14, gm_src3dhhc4laniae, 58, 14);
                gm_b16_8 t15 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t13 * 16));
                gm_l3 = t15;
            }
            {
                gm_b16_8 t17 = gm_l3;
                gm_t6j6smj7qoctu t18;
                gm_b16_8 t19;
                GM_ST(void *, (uint8_t *)&t19, (void *)(gm_s4chbmodjkinv));
                GM_ST(uintptr_t, (uint8_t *)&t19 + 8, (uintptr_t)(6));
                GM_ST(gm_b16_8, (uint8_t *)&t18 + 0, t19);
                gm_b16_8 t20;
                GM_ST(void *, (uint8_t *)&t20, (void *)(gm_s3a1oio07ri1p));
                GM_ST(uintptr_t, (uint8_t *)&t20 + 8, (uintptr_t)(2));
                GM_ST(gm_b16_8, (uint8_t *)&t18 + 16, t20);
                gm_t6j6smj7qoctu t21 = t18;
                uint8_t t22 = 0;
                gm_b16_8 t23 = t17;
                if (gm_eq0iqiubfm7e(((uint8_t *)&t21) + 0, ((uint8_t *)&t23))) t22 = 1;
                if (gm_eq0iqiubfm7e(((uint8_t *)&t21) + 16, ((uint8_t *)&t23))) t22 = 1;
                uint8_t t24 = (uint8_t)(t22);
                if (!t24) goto ie16;
                {
                    gm_tdpp5715iet0a t26;
                    memset(&t26, 0, sizeof t26);
                    GM_ST(uint8_t, ((uint8_t *)&t26 + 0), ((uint8_t)1ULL));
                    uint8_t t27 = 0;
                    F.ret = t26;
                    goto gm_fret;
                }
                goto ix16;
ie16:;
ix16:;
            }
            {
                gm_b16_8 t28 = gm_l0;
                uint64_t t29 = gm_l2;
                gm_tepv5jklv667k t30;
                gm_f_scan_ll1gqkn((void *)&t30, (void *)NULL, cx, NULL, t28, t29, ((uint8_t)1));
                if (gm_unw.kind) {
                    goto gm_ll10;
                }
                gm_tepv5jklv667k t31 = t30;
                uint8_t t37 = GM_LD(uint8_t, (((uint8_t *)&t31) + 0));
                switch (t37) { case ((uint8_t)0ULL): goto ma33; case ((uint8_t)1ULL): goto ma35; case ((uint8_t)2ULL): goto ma34; default: goto ma36; }
ma33:;
                uint8_t t39 = GM_LD(uint8_t, (((uint8_t *)&t31) + 0));
                if (t39 != ((uint8_t)0ULL)) goto mn38;
                gm_t04fuf8kbk07t t40 = GM_LD(gm_t04fuf8kbk07t, (((uint8_t *)&t31) + 8));
                F.l4 = t40;
                {
                    gm_th2lqbo77pvsc t42 = GM_LD(gm_th2lqbo77pvsc, (((uint8_t *)&F.l4) + 8));
                    gm_th2lqbo77pvsc t43 = t42;
                    gm_b16_8 t44;
                    if (((((uint8_t *)&t43))[16] != 0)) {
                        gm_b16_8 t45 = GM_LD(gm_b16_8, (((uint8_t *)&t43) + 0));
                        t44 = t45;
                    } else {
                        gm_b16_8 t46;
                        GM_ST(void *, (uint8_t *)&t46, (void *)(gm_sr4qbrk6tlgaa));
                        GM_ST(uintptr_t, (uint8_t *)&t46 + 8, (uintptr_t)(0));
                        t44 = t46;
                    }
                    gm_l5 = t44;
                }
                {
                    uint8_t t53 = GM_LD(uint8_t, ((((uint8_t *)&F.l4) + 0) + 0));
                    switch (t53) { case ((uint8_t)0ULL): goto ma50; case ((uint8_t)1ULL): goto ma48; case ((uint8_t)2ULL): goto ma51; case ((uint8_t)3ULL): goto ma51; case ((uint8_t)4ULL): goto ma49; case ((uint8_t)5ULL): goto ma51; case ((uint8_t)6ULL): goto ma51; case ((uint8_t)7ULL): goto ma51; case ((uint8_t)8ULL): goto ma51; case ((uint8_t)9ULL): goto ma51; case ((uint8_t)10ULL): goto ma51; case ((uint8_t)11ULL): goto ma51; case ((uint8_t)12ULL): goto ma51; case ((uint8_t)13ULL): goto ma51; case ((uint8_t)14ULL): goto ma51; case ((uint8_t)15ULL): goto ma51; case ((uint8_t)16ULL): goto ma51; case ((uint8_t)17ULL): goto ma51; case ((uint8_t)18ULL): goto ma51; case ((uint8_t)19ULL): goto ma51; case ((uint8_t)20ULL): goto ma51; case ((uint8_t)21ULL): goto ma51; default: goto ma51; }
ma48:;
                    uint8_t t55 = GM_LD(uint8_t, ((((uint8_t *)&F.l4) + 0) + 0));
                    if (t55 != ((uint8_t)1ULL)) goto mn54;
                    {
                        GM_ST(uint8_t, (((uint8_t *)&F.l1) + 0), ((uint8_t)1));
                    }
                    goto me47;
mn54:;
ma49:;
                    uint8_t t58 = GM_LD(uint8_t, ((((uint8_t *)&F.l4) + 0) + 0));
                    if (t58 != ((uint8_t)4ULL)) goto mn57;
                    {
                        gm_b16_8 t60 = gm_l5;
                        gm_t9qkh0rsooc8d t61 = gm_f_mode_of_pq04he2(t60);
                        GM_ST(gm_t9qkh0rsooc8d, (((uint8_t *)&F.l1) + 1), t61);
                    }
                    goto me47;
mn57:;
ma50:;
                    uint8_t t63 = GM_LD(uint8_t, ((((uint8_t *)&F.l4) + 0) + 0));
                    if (t63 != ((uint8_t)0ULL)) goto mn62;
                    {
                        gm_b16_8 t65 = gm_l5;
                        gm_t9qkh0rsooc8d t66 = gm_f_format_of_nbvl1cj(t65);
                        GM_ST(gm_t9qkh0rsooc8d, (((uint8_t *)&F.l1) + 2), t66);
                    }
                    goto me47;
mn62:;
ma51:;
                    goto me47;
mn67:;
ma52:;
                    gm_trap("no match arm matched the scrutinee", gm_src3dhhc4laniae, 63, 17);
me47:;
                }
                {
                    uint64_t t69 = GM_LD(uint64_t, (((uint8_t *)&F.l4) + 32));
                    gm_l2 = t69;
                }
                {
                    goto lc6;
                }
                goto me32;
mn38:;
ma34:;
                uint8_t t71 = GM_LD(uint8_t, (((uint8_t *)&t31) + 0));
                if (t71 != ((uint8_t)2ULL)) goto mn70;
                gm_b16_8 t72 = GM_LD(gm_b16_8, (((uint8_t *)&t31) + 8));
                gm_l6 = t72;
                gm_b16_8 t73 = gm_l6;
                gm_tdpp5715iet0a t74;
                memset(&t74, 0, sizeof t74);
                GM_ST(uint8_t, ((uint8_t *)&t74 + 0), ((uint8_t)2ULL));
                GM_ST(gm_b16_8, ((uint8_t *)&t74 + 8), t73);
                uint8_t t75 = 0;
                F.ret = t74;
                goto gm_fret;
                goto me32;
mn70:;
ma35:;
                uint8_t t77 = GM_LD(uint8_t, (((uint8_t *)&t31) + 0));
                if (t77 != ((uint8_t)1ULL)) goto mn76;
                goto me32;
mn76:;
ma36:;
                gm_trap("no match arm matched the scrutinee", gm_src3dhhc4laniae, 60, 9);
me32:;
            }
            {
                uint64_t t79 = gm_l2;
                uint64_t r80; bool o81 = __builtin_add_overflow(t79, ((uint64_t)1ULL), &r80);
                if (o81) gm_trap("integer overflow in `+`", gm_src3dhhc4laniae, 75, 9);
                uint64_t t82 = r80;
                gm_l2 = t82;
            }
            {
                gm_b16_8 t84 = gm_l3;
                gm_b16_8 t85;
                GM_ST(void *, (uint8_t *)&t85, (void *)(gm_skolmnfc0ccbd));
                GM_ST(uintptr_t, (uint8_t *)&t85 + 8, (uintptr_t)(1));
                uint8_t t86 = gm_f_starts_with_6vvt01e(t84, t85);
                if (!t86) goto ie83;
                {
                    gm_b16_8 t88 = gm_l3;
                    gm_tnpifv20irnph t89;
                    GM_ST(gm_b16_8, ((uint8_t *)&t89 + 0), t88);
                    gm_tabb710d9mq2e t90;
                    gm_f_format_j0ggm04((void *)&t90, (void *)NULL, cx, NULL, t89);
                    if (gm_unw.kind) {
                        goto gm_ll10;
                    }
                    gm_tabb710d9mq2e t91 = t90;
                    if (((((uint8_t *)&t91))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t91) + 8, gm_src3dhhc4laniae, 76, 45);
                    gm_b16_8 t92 = GM_LD(gm_b16_8, (((uint8_t *)&t91) + 8));
                    gm_b16_8 t93 = t92;
                    gm_b16_8 t94 = GM_LD(gm_b16_8, ((uint8_t *)&t93));
                    gm_tdpp5715iet0a t95;
                    memset(&t95, 0, sizeof t95);
                    GM_ST(uint8_t, ((uint8_t *)&t95 + 0), ((uint8_t)2ULL));
                    GM_ST(gm_b16_8, ((uint8_t *)&t95 + 8), t94);
                    uint8_t t96 = 0;
                    F.ret = t95;
                    goto gm_fret;
                }
                goto ix83;
ie83:;
ix83:;
            }
            {
                uint8_t t98;
                if ((((((uint8_t *)&F.l1) + 8))[16] != 0)) goto mn100;
                t98 = ((uint8_t)0);
                goto me99;
mn100:;
                t98 = ((uint8_t)1);
                goto me99;
mn101:;
                gm_trap("no match arm matched the scrutinee", gm_src3dhhc4laniae, 77, 12);
me99:;
                if (!t98) goto ie97;
                {
                    gm_b16_8 t103;
                    GM_ST(void *, (uint8_t *)&t103, (void *)(gm_slrfv83pestqp));
                    GM_ST(uintptr_t, (uint8_t *)&t103 + 8, (uintptr_t)(30));
                    gm_tdpp5715iet0a t104;
                    memset(&t104, 0, sizeof t104);
                    GM_ST(uint8_t, ((uint8_t *)&t104 + 0), ((uint8_t)2ULL));
                    GM_ST(gm_b16_8, ((uint8_t *)&t104 + 8), t103);
                    uint8_t t105 = 0;
                    F.ret = t104;
                    goto gm_fret;
                }
                goto ix97;
ie97:;
ix97:;
            }
            {
                gm_b16_8 t106 = gm_l3;
                gm_th2lqbo77pvsc t107;
                GM_ST(uint8_t, (uint8_t *)&t107 + 17, 0);
                GM_ST(uint16_t, (uint8_t *)&t107 + 18, 0);
                GM_ST(uint32_t, (uint8_t *)&t107 + 20, 0);
                GM_ST(gm_b16_8, ((uint8_t *)&t107), t106);
                ((uint8_t *)&t107)[16] = 1;
                GM_ST(gm_th2lqbo77pvsc, (((uint8_t *)&F.l1) + 8), t107);
            }
lc6:;
        }
gm_ll10:;
        if (gm_unw.frame == &F.h && gm_unw.label == 0) {
            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb6; }
            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc6; }
        }
        goto gm_lad;
lb6:;
    }
    {
        gm_tvcg39brotose t108 = F.l1;
        gm_tdpp5715iet0a t109;
        GM_ST(uint8_t, (uint8_t *)&t109 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t109 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t109 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t109 + 0), ((uint8_t)0ULL));
        GM_ST(gm_tvcg39brotose, ((uint8_t *)&t109 + 8), t108);
        uint8_t t110 = 0;
        F.ret = t109;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_read_error_text_1lgi2bl(void *env, gm_node *cx, gm_node *inst, gm_tj5jn79kq9pnm a0) {
    struct gm_fr_read_error_text_1lgi2bl F;
    gm_tj5jn79kq9pnm gm_l0; /* e */
    int32_t gm_l1; /* code */
    GM_STACK_CHECK(&F, 105u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
        if (t3 != ((uint8_t)0ULL)) goto ie2;
        uint8_t t4 = GM_LD(uint8_t, ((((uint8_t *)&gm_l0) + 4) + 0));
        if (t4 != ((uint8_t)13ULL)) goto ie2;
        int32_t t5 = GM_LD(int32_t, ((((uint8_t *)&gm_l0) + 4) + 4));
        gm_l1 = t5;
        {
            int32_t t8 = gm_l1;
            uint8_t t9 = ((t8 == ((int32_t)21LL)));
            if (!t9) goto ie7;
            {
                gm_b16_8 t11;
                GM_ST(void *, (uint8_t *)&t11, (void *)(gm_s3qd7ldqb9526));
                GM_ST(uintptr_t, (uint8_t *)&t11 + 8, (uintptr_t)(28));
                uint8_t t12 = 0;
                F.ret = t11;
                goto gm_fret;
            }
            goto ix7;
ie7:;
ix7:;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        gm_tj5jn79kq9pnm t13 = gm_l0;
        gm_b16_8 t14 = gm_f_describe_read_error_uikkr59((void *)NULL, cx, NULL, t13);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t15 = 0;
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
inline __attribute__((aligned(4))) void gm_f_read_line_1oksar8(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_read_line_1oksar8 F;
    gm_tivanu2mfcv6o gm_l1; /* one */
    uint64_t gm_l2; /* n */
    GM_STACK_CHECK(&F, 303u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l0, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
    }
    {
        for (;;) {
            if (!((uint8_t)1)) goto lb2;
            {
                void * t5 = ((uint8_t *)&gm_l1);
                if ((uintptr_t)0 > (uintptr_t)1 || (uintptr_t)1 > (uintptr_t)1) gm_trap_slice((uintptr_t)0, (uintptr_t)1, 1, gm_src3dhhc4laniae, 490, 22);
                gm_b16_8 t6;
                GM_ST(void *, (uint8_t *)&t6, (void *)(((uint8_t *)t5 + (uintptr_t)0 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t6 + 8, (uintptr_t)((uintptr_t)1 - (uintptr_t)0));
                gm_node *pn7 = gm_find(cx, 1018230276);
                if (!pn7) gm_trap("no handler installed for effect `In`", gm_src3dhhc4laniae, 490, 14);
                gm_thf5kr0is9u9i t8;
                t8 = ((gm_thf5kr0is9u9i (*)(void *, gm_node *, gm_node *, gm_b16_8))pn7->clauses[0])(pn7->env, pn7->hctx, pn7, t6);
                if (gm_unw.kind) {
                    goto gm_ll3;
                }
                gm_thf5kr0is9u9i t9 = t8;
                uint64_t t10;
                if (!((((uint8_t *)&t9))[0] != 0)) {
                    uint64_t t11 = GM_LD(uint64_t, (((uint8_t *)&t9) + 8));
                    t10 = t11;
                } else {
                    t10 = ((uint64_t)0ULL);
                }
                gm_l2 = t10;
            }
            {
                uint64_t t13 = gm_l2;
                uint8_t t14 = ((t13 == ((uint64_t)0ULL)));
                if (!t14) goto ie12;
                {
                    goto lb2;
                }
                goto ix12;
ie12:;
ix12:;
            }
            {
                void * t16 = (void *)((uint8_t *)&F.l0);
                uint64_t t17 = (uintptr_t)((uint64_t)0ULL);
                if (t17 >= (uintptr_t)(1)) gm_trap_index(t17, 1, gm_src3dhhc4laniae, 492, 20);
                uint8_t t18 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + t17 * 1));
                gm_tftb1odc739t5 t19 = gm_f_push_t83rg2m(t16, t18);
                gm_tftb1odc739t5 t20 = t19;
                if (((((uint8_t *)&t20))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t20) + 1, gm_src3dhhc4laniae, 492, 9);
                gm_b0_1 t21 = GM_LD(gm_b0_1, (((uint8_t *)&t20) + 1));
            }
            {
                uint64_t t23 = (uintptr_t)((uint64_t)0ULL);
                if (t23 >= (uintptr_t)(1)) gm_trap_index(t23, 1, gm_src3dhhc4laniae, 493, 12);
                uint8_t t24 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + t23 * 1));
                uint8_t t25 = ((t24 == ((uint8_t)10ULL)));
                if (!t25) goto ie22;
                {
                    goto lb2;
                }
                goto ix22;
ie22:;
ix22:;
            }
lc2:;
        }
gm_ll3:;
        if (gm_unw.frame == &F.h && gm_unw.label == 0) {
            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb2; }
            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc2; }
        }
        goto gm_lad;
lb2:;
    }
    {
        uint64_t t28 = GM_LD(uintptr_t, (((uint8_t *)&F.l0) + 0) + 8);
        uint8_t t29 = ((t28 == ((uint64_t)0ULL)));
        if (!t29) goto ie27;
        {
            gm_tl572d7579hns t31;
            GM_ST(uint8_t, (uint8_t *)&t31 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t31 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t31 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t31 + 8, 0);
            GM_ST(uint64_t, (uint8_t *)&t31 + 16, 0);
            GM_ST(uint8_t, ((uint8_t *)&t31 + 0), ((uint8_t)1ULL));
            uint8_t t32 = 0;
            F.ret = t31;
            goto gm_fret;
        }
        goto ix27;
ie27:;
ix27:;
    }
    {
        gm_b16_8 t34 = GM_LD(gm_b16_8, (((uint8_t *)&F.l0) + 0));
        gm_b16_8 t35 = t34;
        gm_b16_8 t36 = GM_LD(gm_b16_8, ((uint8_t *)&t35));
        uint8_t t37 = gm_f_utf8_valid_a8oc1t8(t36);
        uint8_t t38 = (uint8_t)!t37;
        if (!t38) goto ie33;
        {
            gm_tl572d7579hns t40;
            GM_ST(uint8_t, (uint8_t *)&t40 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t40 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t40 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t40 + 8, 0);
            GM_ST(uint64_t, (uint8_t *)&t40 + 16, 0);
            GM_ST(uint8_t, ((uint8_t *)&t40 + 0), ((uint8_t)2ULL));
            uint8_t t41 = 0;
            F.ret = t40;
            goto gm_fret;
        }
        goto ix33;
ie33:;
ix33:;
    }
    {
        gm_b16_8 t42 = GM_LD(gm_b16_8, (((uint8_t *)&F.l0) + 0));
        gm_b16_8 t43 = t42;
        gm_b16_8 t44 = GM_LD(gm_b16_8, ((uint8_t *)&t43));
        gm_tl572d7579hns t45;
        GM_ST(uint8_t, (uint8_t *)&t45 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t45 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t45 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t45 + 0), ((uint8_t)0ULL));
        GM_ST(gm_b16_8, ((uint8_t *)&t45 + 8), t44);
        uint8_t t46 = 0;
        F.ret = t45;
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
inline __attribute__((aligned(4))) void gm_f_renamed_vars_fho5hvj(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_renamed_vars_fho5hvj F;
    void * gm_l0; /* r */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    gm_t6j6smj7qoctu gm_l4; /* v */
    GM_STACK_CHECK(&F, 254u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_e6ie1fq_2((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            void * t3 = gm_l0;
            gm_b16_8 t4 = GM_LD(gm_b16_8, ((((uint8_t *)t3) + 128) + 0));
            gm_l2 = t4;
        }
        {
            gm_l3 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t6 = gm_l3;
                uint64_t t7 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                uint8_t t8 = (t6 < t7);
                if (!t8) goto lb5;
                {
                    uint64_t t11 = gm_l3;
                    uint64_t t12 = (uintptr_t)t11;
                    uint64_t t13 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                    gm_t6j6smj7qoctu t16 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t12 * 32));
                    gm_l4 = t16;
                    {
                        gm_b16_8 t19 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l4) + 0));
                        gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l4) + 16));
                        gm_b16_8 t21 = t19;
                        gm_b16_8 t22 = t20;
                        uint8_t t23 = (!gm_eq0iqiubfm7e(((uint8_t *)&t21), ((uint8_t *)&t22)));
                        if (!t23) goto ie18;
                        {
                            void * t25 = (void *)((uint8_t *)&F.l1);
                            gm_tftb1odc739t5 t26 = gm_f_push_t6e85n8_2(t25, &(*(gm_t6j6smj7qoctu *)((uint8_t *)&gm_l4)));
                            gm_tftb1odc739t5 t27 = t26;
                            if (((((uint8_t *)&t27))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t27) + 1, gm_src3dhhc4laniae, 202, 39);
                            gm_b0_1 t28 = GM_LD(gm_b0_1, (((uint8_t *)&t27) + 1));
                        }
                        goto ix18;
ie18:;
ix18:;
                    }
                    goto me14;
mn15:;
                    gm_trap("no match arm matched the scrutinee", gm_src3dhhc4laniae, 201, 5);
me14:;
                }
lc5:;
                {
                    uint64_t t30 = gm_l3;
                    uint64_t r31; bool o32 = __builtin_add_overflow(t30, ((uint64_t)1ULL), &r31);
                    uint64_t t33 = r31;
                    gm_l3 = t33;
                }
            }
lb5:;
        }
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
inline __attribute__((aligned(4))) void gm_f_replay_texts_3te9pi7(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1) {
    struct gm_fr_replay_texts_3te9pi7 F;
    void * gm_l0; /* r */
    uint8_t gm_l1; /* pure_only */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    GM_STACK_CHECK(&F, 184u);
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
            void * t3 = gm_l0;
            gm_b16_8 t4 = GM_LD(gm_b16_8, ((((uint8_t *)t3) + 88) + 0));
            gm_l3 = t4;
        }
        {
            gm_l4 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t6 = gm_l4;
                uint64_t t7 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                uint8_t t8 = (t6 < t7);
                if (!t8) goto lb5;
                {
                    uint64_t t11 = gm_l4;
                    uint64_t t12 = (uintptr_t)t11;
                    uint64_t t13 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                    gm_tk9n3vlila9qg t16 = GM_LD(gm_tk9n3vlila9qg, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t12 * 40));
                    F.l5 = t16;
                    {
                        uint8_t t19 = gm_l1;
                        if (!t19) goto ie18;
                        uint8_t t20 = GM_LD(uint8_t, (((uint8_t *)&F.l5) + 32));
                        if (!t20) goto ie18;
                        {
                            goto lc5;
                        }
                        goto ix18;
ie18:;
ix18:;
                    }
                    {
                        void * t22 = (void *)((uint8_t *)&F.l2);
                        gm_b16_8 t23 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 16));
                        gm_tftb1odc739t5 t24 = gm_f_push_3ef746b(t22, t23);
                        gm_tftb1odc739t5 t25 = t24;
                        if (((((uint8_t *)&t25))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t25) + 1, gm_src3dhhc4laniae, 193, 9);
                        gm_b0_1 t26 = GM_LD(gm_b0_1, (((uint8_t *)&t25) + 1));
                    }
                    goto me14;
mn15:;
                    gm_trap("no match arm matched the scrutinee", gm_src3dhhc4laniae, 191, 5);
me14:;
                }
lc5:;
                {
                    uint64_t t28 = gm_l4;
                    uint64_t r29; bool o30 = __builtin_add_overflow(t28, ((uint64_t)1ULL), &r29);
                    uint64_t t31 = r29;
                    gm_l4 = t31;
                }
            }
lb5:;
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
inline __attribute__((aligned(4))) void gm_f_reset_pb2jqrj(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_reset_pb2jqrj F;
    void * gm_l0; /* r */
    GM_STACK_CHECK(&F, 56u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        void * t3 = (void *)(((uint8_t *)t2) + 48);
        gm_f_clear_3ibm3tg(t3);
    }
    {
        void * t4 = gm_l0;
        void * t5 = (void *)(((uint8_t *)t4) + 88);
        gm_f_clear_gp35vgp(t5);
    }
    {
        void * t6 = gm_l0;
        void * t7 = (void *)(((uint8_t *)t6) + 128);
        gm_f_clear_sl9fc3u(t7);
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_set_var_pjsp5eq(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2) {
    struct gm_fr_set_var_pjsp5eq F;
    void * gm_l0; /* vars */
    gm_b16_8 gm_l1; /* name */
    gm_b16_8 gm_l2; /* internal */
    uint64_t gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    uint64_t gm_l5; /* i */
    GM_STACK_CHECK(&F, 359u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        {
            gm_l4 = ((uint64_t)0ULL);
        }
        {
            void * t3 = gm_l0;
            uint64_t t4 = GM_LD(uintptr_t, (((uint8_t *)t3) + 0) + 8);
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
                        uint64_t t18 = gm_l5;
                        uint64_t t19 = (uintptr_t)t18;
                        void * t20 = gm_l0;
                        uint64_t t21 = GM_LD(uintptr_t, (((uint8_t *)t20) + 0) + 8);
                        if (t19 >= (uintptr_t)(t21)) gm_trap_index(t19, t21, gm_src3dhhc4laniae, 361, 12);
                        gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)GM_LD(void *, (((uint8_t *)t20) + 0)) + t19 * 32) + 0));
                        gm_b16_8 t23 = gm_l1;
                        gm_b16_8 t24 = t22;
                        gm_b16_8 t25 = t23;
                        uint8_t t26 = (gm_eq0iqiubfm7e(((uint8_t *)&t24), ((uint8_t *)&t25)));
                        if (!t26) goto ie17;
                        {
                            uint64_t t28 = gm_l5;
                            uint64_t t29 = (uintptr_t)t28;
                            void * t30 = gm_l0;
                            uint64_t t31 = GM_LD(uintptr_t, (((uint8_t *)t30) + 0) + 8);
                            if (t29 >= (uintptr_t)(t31)) gm_trap_index(t29, t31, gm_src3dhhc4laniae, 362, 13);
                            gm_b16_8 t32 = gm_l2;
                            GM_ST(gm_b16_8, (((uint8_t *)GM_LD(void *, (((uint8_t *)t30) + 0)) + t29 * 32) + 16), t32);
                        }
                        {
                            gm_b0_1 t33;
                            uint8_t t34 = 0;
                            F.ret = t33;
                            goto gm_fret;
                        }
                        goto ix17;
ie17:;
ix17:;
                    }
                    goto me13;
mn14:;
                    gm_trap("no match arm matched the scrutinee", gm_src3dhhc4laniae, 360, 5);
me13:;
                }
lc5:;
                {
                    uint64_t t36 = gm_l4;
                    uint64_t r37; bool o38 = __builtin_add_overflow(t36, ((uint64_t)1ULL), &r37);
                    uint64_t t39 = r37;
                    gm_l4 = t39;
                }
            }
lb5:;
        }
    }
    {
        void * t40 = gm_l0;
        gm_t6j6smj7qoctu t41;
        gm_b16_8 t42 = gm_l1;
        GM_ST(gm_b16_8, (uint8_t *)&t41 + 0, t42);
        gm_b16_8 t43 = gm_l2;
        GM_ST(gm_b16_8, (uint8_t *)&t41 + 16, t43);
        gm_tftb1odc739t5 t44 = gm_f_push_t6e85n8_2(t40, &t41);
        gm_tftb1odc739t5 t45 = t44;
        if (((((uint8_t *)&t45))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t45) + 1, gm_src3dhhc4laniae, 366, 5);
        gm_b0_1 t46 = GM_LD(gm_b0_1, (((uint8_t *)&t45) + 1));
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_show_861qdlm(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_show_861qdlm F;
    gm_b16_8 gm_l0; /* text */
    GM_STACK_CHECK(&F, 41u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint64_t t3 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        uint8_t t4 = (t3 > ((uint64_t)0ULL));
        if (!t4) goto ie2;
        {
            gm_b16_8 t6 = gm_l0;
            gm_pf0nmuupvugn(cx, t6, gm_src3dhhc4laniae, 209, 23);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix2;
ie2:;
ix2:;
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
__attribute__((aligned(4))) uint8_t gm_f_statements_dafpns9(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, const void * a2) {
    struct gm_fr_statements_dafpns9 F;
    void * gm_l0; /* r */
    gm_b16_8 gm_l1; /* text */
    gm_th2lqbo77pvsc gm_l2; /* info */
    gm_b16_8 gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    gm_t5hdnnftkup6m gm_l7; /* b */
    gm_b16_8 gm_l8; /* internal */
    gm_b16_8 gm_l10; /* body */
    uint8_t gm_l11; /* changed */
    gm_b16_8 gm_l12; /* <tmp> */
    uint64_t gm_l13; /* <tmp> */
    gm_t2s5j3n0guq0t gm_l14; /* d */
    uint8_t gm_l19; /* value */
    uint8_t gm_l22; /* unused */
    void * gm_l23; /* shown_c */
    gm_th2lqbo77pvsc gm_l24; /* kept */
    uint8_t gm_l25; /* impure */
    gm_th2lqbo77pvsc gm_l26; /* kept_text */
    gm_b16_8 gm_l27; /* k */
    uint8_t gm_l31; /* ok */
    gm_b16_8 gm_l32; /* k */
    gm_b16_8 gm_l34; /* <tmp> */
    uint64_t gm_l35; /* <tmp> */
    gm_t5hdnnftkup6m gm_l36; /* b */
    gm_b16_8 gm_l37; /* joined */
    GM_STACK_CHECK(&F, 3591u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        gm_f_list_glg30gi((void *)&F.l3, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_f_list_e6ie1fq_2((void *)&F.l4, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l4);
        void * t3 = gm_l0;
        gm_b16_8 t4 = GM_LD(gm_b16_8, ((((uint8_t *)t3) + 128) + 0));
        gm_b16_8 t5 = t4;
        gm_b16_8 t6 = GM_LD(gm_b16_8, ((uint8_t *)&t5));
        gm_tftb1odc739t5 t7 = gm_f_extend_e1j40m3(t2, t6);
        gm_tftb1odc739t5 t8 = t7;
        if (((((uint8_t *)&t8))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t8) + 1, gm_src3dhhc4laniae, 273, 5);
        gm_b0_1 t9 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
    }
    {
        {
            gm_b16_8 t11 = GM_LD(gm_b16_8, (((uint8_t *)a2) + 0));
            gm_l5 = t11;
        }
        {
            gm_l6 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t13 = gm_l6;
                uint64_t t14 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                uint8_t t15 = (t13 < t14);
                if (!t15) goto lb12;
                {
                    uint64_t t18 = gm_l6;
                    uint64_t t19 = (uintptr_t)t18;
                    uint64_t t20 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                    gm_t5hdnnftkup6m t23 = GM_LD(gm_t5hdnnftkup6m, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l5)) + t19 * 24));
                    gm_l7 = t23;
                    {
                        uint8_t t26 = (((GM_LD(uintptr_t, (((uint8_t *)&gm_l7) + 0) + 8) == 1) && memcmp(GM_LD(void *, (((uint8_t *)&gm_l7) + 0)), gm_sfdbfrv46lihd, 1) == 0));
                        if (!t26) goto ie25;
                        {
                            goto lc12;
                        }
                        goto ix25;
ie25:;
ix25:;
                    }
                    {
                        gm_b16_8 t28 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l7) + 0));
                        gm_l8 = t28;
                    }
                    {
                        void * t30 = gm_l0;
                        gm_b16_8 t31 = GM_LD(gm_b16_8, ((((uint8_t *)t30) + 128) + 0));
                        gm_b16_8 t32 = t31;
                        gm_b16_8 t33 = GM_LD(gm_b16_8, ((uint8_t *)&t32));
                        gm_b16_8 t34 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l7) + 0));
                        gm_th2lqbo77pvsc t35;
                        gm_f_lookup_var_3vpm3u6((void *)&t35, t33, t34);
                        gm_th2lqbo77pvsc t36 = t35;
                        uint8_t t37;
                        if (((((uint8_t *)&t36))[16] != 0)) goto mn39;
                        t37 = ((uint8_t)0);
                        goto me38;
mn39:;
                        t37 = ((uint8_t)1);
                        goto me38;
mn40:;
                        gm_trap("no match arm matched the scrutinee", gm_src3dhhc4laniae, 277, 12);
me38:;
                        if (!t37) goto ie29;
                        {
                            void * t42 = gm_l0;
                            void * t43 = gm_l0;
                            uint32_t t44 = GM_LD(uint32_t, (((uint8_t *)t43) + 168));
                            uint32_t r45; bool o46 = __builtin_add_overflow(t44, ((uint32_t)1ULL), &r45);
                            if (o46) gm_trap("integer overflow in `+`", gm_src3dhhc4laniae, 278, 13);
                            uint32_t t47 = r45;
                            GM_ST(uint32_t, (((uint8_t *)t42) + 168), t47);
                        }
                        {
                            gm_tommf4j9upomc t48;
                            memset((uint8_t *)&t48 + 20, 0, 4);
                            gm_b16_8 t49 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l7) + 0));
                            GM_ST(gm_b16_8, (uint8_t *)&t48 + 0, t49);
                            void * t50 = gm_l0;
                            uint32_t t51 = GM_LD(uint32_t, (((uint8_t *)t50) + 168));
                            GM_ST(uint32_t, (uint8_t *)&t48 + 16, t51);
                            gm_tabb710d9mq2e t52;
                            gm_f_format_5a05gmg((void *)&t52, (void *)NULL, cx, NULL, &t48);
                            if (gm_unw.kind) {
                                goto gm_ll16;
                            }
                            gm_tabb710d9mq2e t53 = t52;
                            if (((((uint8_t *)&t53))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t53) + 8, gm_src3dhhc4laniae, 279, 24);
                            gm_b16_8 t54 = GM_LD(gm_b16_8, (((uint8_t *)&t53) + 8));
                            gm_b16_8 t55 = t54;
                            gm_b16_8 t56 = GM_LD(gm_b16_8, ((uint8_t *)&t55));
                            gm_l8 = t56;
                        }
                        goto ix29;
ie29:;
ix29:;
                    }
                    {
                        void * t57 = (void *)((uint8_t *)&F.l3);
                        gm_t2s5j3n0guq0t t58;
                        uint32_t t59 = GM_LD(uint32_t, (((uint8_t *)&gm_l7) + 16));
                        GM_ST(uint32_t, (uint8_t *)&t58 + 0, t59);
                        uint32_t t60 = GM_LD(uint32_t, (((uint8_t *)&gm_l7) + 20));
                        GM_ST(uint32_t, (uint8_t *)&t58 + 4, t60);
                        gm_b16_8 t61 = gm_l8;
                        GM_ST(gm_b16_8, (uint8_t *)&t58 + 8, t61);
                        gm_tftb1odc739t5 t62 = gm_f_push_f9bu01m(t57, &t58);
                        gm_tftb1odc739t5 t63 = t62;
                        if (((((uint8_t *)&t63))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t63) + 1, gm_src3dhhc4laniae, 281, 9);
                        gm_b0_1 t64 = GM_LD(gm_b0_1, (((uint8_t *)&t63) + 1));
                    }
                    {
                        void * t65 = (void *)((uint8_t *)&F.l4);
                        gm_b16_8 t66 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l7) + 0));
                        gm_b16_8 t67 = gm_l8;
                        gm_b0_1 t68 = gm_f_set_var_pjsp5eq((void *)NULL, cx, NULL, t65, t66, t67);
                        if (gm_unw.kind) {
                            goto gm_ll16;
                        }
                    }
                    goto me21;
mn22:;
                    gm_trap("no match arm matched the scrutinee", gm_src3dhhc4laniae, 274, 5);
me21:;
                }
lc12:;
                {
                    uint64_t t70 = gm_l6;
                    uint64_t r71; bool o72 = __builtin_add_overflow(t70, ((uint64_t)1ULL), &r71);
                    uint64_t t73 = r71;
                    gm_l6 = t73;
                }
            }
gm_ll16:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb12; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc12; }
            }
            goto gm_lad;
lb12:;
        }
    }
    {
        void * t74 = gm_l0;
        gm_f_renamed_vars_fho5hvj((void *)&F.l9, (void *)NULL, cx, NULL, t74);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_b16_8 t75 = gm_l1;
        gm_l10 = t75;
    }
    {
        uint64_t t76 = GM_LD(uintptr_t, (((uint8_t *)&F.l9) + 0) + 8);
        uint8_t t77 = (t76 > ((uint64_t)0ULL));
        gm_l11 = t77;
    }
    {
        {
            gm_b16_8 t79 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
            gm_l12 = t79;
        }
        {
            gm_l13 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t81 = gm_l13;
                uint64_t t82 = GM_LD(uintptr_t, ((uint8_t *)&gm_l12) + 8);
                uint8_t t83 = (t81 < t82);
                if (!t83) goto lb80;
                {
                    uint64_t t86 = gm_l13;
                    uint64_t t87 = (uintptr_t)t86;
                    uint64_t t88 = GM_LD(uintptr_t, ((uint8_t *)&gm_l12) + 8);
                    gm_t2s5j3n0guq0t t91 = GM_LD(gm_t2s5j3n0guq0t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l12)) + t87 * 24));
                    gm_l14 = t91;
                    {
                        gm_b16_8 t94 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l14) + 8));
                        uint64_t t95 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                        void * t96 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)));
                        uint32_t t97 = GM_LD(uint32_t, (((uint8_t *)&gm_l14) + 0));
                        uint64_t t98 = (uint64_t)t97;
                        uint64_t t99 = (uintptr_t)t98;
                        uint32_t t100 = GM_LD(uint32_t, (((uint8_t *)&gm_l14) + 4));
                        uint64_t t101 = (uint64_t)t100;
                        uint64_t t102 = (uintptr_t)t101;
                        if (t99 > t102 || t102 > (uintptr_t)t95) gm_trap_slice(t99, t102, t95, gm_src3dhhc4laniae, 288, 26);
                        gm_b16_8 t103;
                        GM_ST(void *, (uint8_t *)&t103, (void *)(((uint8_t *)t96 + t99 * 1)));
                        GM_ST(uintptr_t, (uint8_t *)&t103 + 8, (uintptr_t)(t102 - t99));
                        gm_b16_8 t104 = t94;
                        gm_b16_8 t105 = t103;
                        uint8_t t106 = (!gm_eq0iqiubfm7e(((uint8_t *)&t104), ((uint8_t *)&t105)));
                        if (!t106) goto ie93;
                        {
                            gm_l11 = ((uint8_t)1);
                        }
                        goto ix93;
ie93:;
ix93:;
                    }
                    goto me89;
mn90:;
                    gm_trap("no match arm matched the scrutinee", gm_src3dhhc4laniae, 287, 5);
me89:;
                }
lc80:;
                {
                    uint64_t t109 = gm_l13;
                    uint64_t r110; bool o111 = __builtin_add_overflow(t109, ((uint64_t)1ULL), &r110);
                    uint64_t t112 = r110;
                    gm_l13 = t112;
                }
            }
lb80:;
        }
    }
    {
        uint8_t t114 = gm_l11;
        if (!t114) goto ie113;
        {
            gm_b16_8 t116 = gm_l1;
            gm_b16_8 t117 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
            gm_b16_8 t118 = t117;
            gm_b16_8 t119 = GM_LD(gm_b16_8, ((uint8_t *)&t118));
            gm_b16_8 t120 = GM_LD(gm_b16_8, (((uint8_t *)&F.l9) + 0));
            gm_b16_8 t121 = t120;
            gm_b16_8 t122 = GM_LD(gm_b16_8, ((uint8_t *)&t121));
            gm_b16_8 t123 = gm_f_rename_47b7og5((void *)NULL, cx, NULL, t116, t119, t122);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l10 = t123;
        }
        goto ix113;
ie113:;
ix113:;
    }
    {
        void * t124 = gm_l0;
        gm_f_replay_texts_3te9pi7((void *)&F.l15, (void *)NULL, cx, NULL, t124, ((uint8_t)0));
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_b16_8 t125;
        GM_ST(void *, (uint8_t *)&t125, (void *)(gm_som0q53jqvjh3));
        GM_ST(uintptr_t, (uint8_t *)&t125 + 8, (uintptr_t)(4));
        GM_ST(gm_b16_8, (uint8_t *)&F.l16 + 0, t125);
        gm_b16_8 t126 = gm_l10;
        GM_ST(gm_b16_8, (uint8_t *)&F.l16 + 16, t126);
        gm_b16_8 t127;
        GM_ST(void *, (uint8_t *)&t127, (void *)(gm_so514oj5nbhcu));
        GM_ST(uintptr_t, (uint8_t *)&t127 + 8, (uintptr_t)(1));
        GM_ST(gm_b16_8, (uint8_t *)&F.l16 + 32, t127);
    }
    {
        void * t128 = gm_l0;
        gm_th2lqbo77pvsc t129;
        memset(&t129, 0, sizeof t129);
        gm_b16_8 t130 = gm_f_empty_slice_5nn7ph0();
        gm_b16_8 t131 = t130;
        gm_b16_8 t132 = GM_LD(gm_b16_8, ((uint8_t *)&t131));
        gm_b16_8 t133 = GM_LD(gm_b16_8, (((uint8_t *)&F.l15) + 0));
        gm_b16_8 t134 = t133;
        gm_b16_8 t135 = GM_LD(gm_b16_8, ((uint8_t *)&t134));
        gm_tfeab279c16rr t136 = F.l16;
        gm_td6svc7gs468a t137;
        GM_ST(uint8_t, (uint8_t *)&t137 + 49, 0);
        GM_ST(uint16_t, (uint8_t *)&t137 + 50, 0);
        GM_ST(uint32_t, (uint8_t *)&t137 + 52, 0);
        GM_ST(gm_tfeab279c16rr, ((uint8_t *)&t137), t136);
        ((uint8_t *)&t137)[48] = 1;
        gm_b16_8 t138 = GM_LD(gm_b16_8, gm_cd231ebi1kg);
        gm_f_compile_rj09gp7((void *)&F.l17, (void *)NULL, cx, NULL, t128, &t129, t132, t135, &t137, t138, ((uint8_t)1));
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_toqvmi7jqorid t139 = F.l17;
        F.l18 = t139;
    }
    {
        gm_l19 = ((uint8_t)0);
    }
    {
        void * t141 = GM_LD(void *, (((uint8_t *)&F.l17) + 24));
        void * t142 = t141;
        uint8_t t143;
        if ((GM_LD(void *, ((uint8_t *)&t142)) != 0)) goto mn145;
        t143 = ((uint8_t)0);
        goto me144;
mn145:;
        t143 = ((uint8_t)1);
        goto me144;
mn146:;
        gm_trap("no match arm matched the scrutinee", gm_src3dhhc4laniae, 296, 8);
me144:;
        if (!t143) goto ie140;
        goto ix140;
ie140:;
        uint8_t t149 = GM_LD(uint8_t, (((uint8_t *)a2) + 16));
        if (!t149) goto ie148;
        {
            gm_b16_8 t151;
            GM_ST(void *, (uint8_t *)&t151, (void *)(gm_sgvln0vb20p94));
            GM_ST(uintptr_t, (uint8_t *)&t151 + 8, (uintptr_t)(16));
            GM_ST(gm_b16_8, (uint8_t *)&F.l20 + 0, t151);
            gm_b16_8 t152 = gm_l10;
            GM_ST(gm_b16_8, (uint8_t *)&F.l20 + 16, t152);
            gm_b16_8 t153;
            GM_ST(void *, (uint8_t *)&t153, (void *)(gm_spun41oorb00o));
            GM_ST(uintptr_t, (uint8_t *)&t153 + 8, (uintptr_t)(29));
            GM_ST(gm_b16_8, (uint8_t *)&F.l20 + 32, t153);
        }
        {
            void * t154 = gm_l0;
            gm_th2lqbo77pvsc t155;
            memset(&t155, 0, sizeof t155);
            gm_b16_8 t156 = gm_f_empty_slice_5nn7ph0();
            gm_b16_8 t157 = t156;
            gm_b16_8 t158 = GM_LD(gm_b16_8, ((uint8_t *)&t157));
            gm_b16_8 t159 = GM_LD(gm_b16_8, (((uint8_t *)&F.l15) + 0));
            gm_b16_8 t160 = t159;
            gm_b16_8 t161 = GM_LD(gm_b16_8, ((uint8_t *)&t160));
            gm_tfeab279c16rr t162 = F.l20;
            gm_td6svc7gs468a t163;
            GM_ST(uint8_t, (uint8_t *)&t163 + 49, 0);
            GM_ST(uint16_t, (uint8_t *)&t163 + 50, 0);
            GM_ST(uint32_t, (uint8_t *)&t163 + 52, 0);
            GM_ST(gm_tfeab279c16rr, ((uint8_t *)&t163), t162);
            ((uint8_t *)&t163)[48] = 1;
            gm_b16_8 t164 = GM_LD(gm_b16_8, gm_cd231ebi1kg);
            gm_f_compile_rj09gp7((void *)&F.l21, (void *)NULL, cx, NULL, t154, &t155, t158, t161, &t163, t164, ((uint8_t)1));
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t166 = GM_LD(void *, (((uint8_t *)&F.l21) + 24));
            void * t167 = t166;
            uint8_t t168;
            if ((GM_LD(void *, ((uint8_t *)&t167)) != 0)) goto mn170;
            t168 = ((uint8_t)0);
            goto me169;
mn170:;
            t168 = ((uint8_t)1);
            goto me169;
mn171:;
            gm_trap("no match arm matched the scrutinee", gm_src3dhhc4laniae, 301, 12);
me169:;
            if (!t168) goto ie165;
            {
                gm_toqvmi7jqorid t173 = F.l21;
                F.l18 = t173;
            }
            {
                gm_l19 = ((uint8_t)1);
            }
            {
                void * t174 = (void *)((uint8_t *)&F.l17);
                gm_b0_1 t175 = gm_f_release_mdh37fq((void *)NULL, cx, NULL, t174);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix165;
ie165:;
            {
                gm_l22 = ((uint8_t)0);
            }
            {
                void * t178 = GM_LD(void *, (((uint8_t *)&F.l17) + 16));
                uint64_t t179 = GM_LD(uintptr_t, ((((uint8_t *)t178) + 0) + 0) + 8);
                uint8_t t180 = (t179 > ((uint64_t)0ULL));
                if (!t180) goto ie177;
                {
                    uint64_t t182 = (uintptr_t)((uint64_t)0ULL);
                    void * t183 = GM_LD(void *, (((uint8_t *)&F.l17) + 16));
                    uint64_t t184 = GM_LD(uintptr_t, ((((uint8_t *)t183) + 0) + 0) + 8);
                    if (t182 >= (uintptr_t)(t184)) gm_trap_index(t182, t184, gm_src3dhhc4laniae, 307, 71);
                    gm_b16_8 t185 = GM_LD(gm_b16_8, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t183) + 0) + 0)) + t182 * 184) + 8));
                    gm_b16_8 t186;
                    GM_ST(void *, (uint8_t *)&t186, (void *)(gm_sshi9ffc6nkc4));
                    GM_ST(uintptr_t, (uint8_t *)&t186 + 8, (uintptr_t)(9));
                    uint8_t t187 = gm_f_contains_gkki4kq(t185, t186);
                    gm_l22 = t187;
                }
                goto ix177;
ie177:;
ix177:;
            }
            {
                void * t188;
                uint8_t t190 = gm_l22;
                if (!t190) goto ie189;
                void * t192 = (void *)((uint8_t *)&F.l21);
                void * t193 = t192;
                t188 = t193;
                goto ix189;
ie189:;
                void * t195 = (void *)((uint8_t *)&F.l17);
                void * t196 = t195;
                t188 = t196;
ix189:;
                gm_l23 = t188;
            }
            {
                void * t197 = gm_l23;
                void * t198 = gm_l0;
                gm_t9qkh0rsooc8d t199 = GM_LD(gm_t9qkh0rsooc8d, ((((uint8_t *)t198) + 0) + 2));
                gm_b16_8 t200 = gm_f_shown_o0bbr93((void *)NULL, cx, NULL, t197, t199);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_b0_1 t201 = gm_f_show_861qdlm((void *)NULL, cx, NULL, t200);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t202 = (void *)((uint8_t *)&F.l21);
                gm_b0_1 t203 = gm_f_release_mdh37fq((void *)NULL, cx, NULL, t202);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t204 = (void *)((uint8_t *)&F.l17);
                gm_b0_1 t205 = gm_f_release_mdh37fq((void *)NULL, cx, NULL, t204);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t206 = (void *)((uint8_t *)&F.l15);
                gm_f_deinit_gvn47sj(t206);
            }
            {
                uint8_t t207 = 0;
                F.ret = ((uint8_t)0);
                goto gm_fret;
            }
ix165:;
        }
        goto ix148;
ie148:;
        {
            void * t209 = (void *)((uint8_t *)&F.l17);
            void * t210 = gm_l0;
            gm_t9qkh0rsooc8d t211 = GM_LD(gm_t9qkh0rsooc8d, ((((uint8_t *)t210) + 0) + 2));
            gm_b16_8 t212 = gm_f_shown_o0bbr93((void *)NULL, cx, NULL, t209, t211);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_b0_1 t213 = gm_f_show_861qdlm((void *)NULL, cx, NULL, t212);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t214 = (void *)((uint8_t *)&F.l17);
            gm_b0_1 t215 = gm_f_release_mdh37fq((void *)NULL, cx, NULL, t214);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t216 = (void *)((uint8_t *)&F.l15);
            gm_f_deinit_gvn47sj(t216);
        }
        {
            uint8_t t217 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
ix148:;
ix140:;
    }
    {
        void * t218 = (void *)((uint8_t *)&F.l15);
        gm_f_deinit_gvn47sj(t218);
    }
    {
    }
    {
        void * t221 = (void *)((uint8_t *)&F.l18);
        void * t222 = gm_l0;
        gm_t9qkh0rsooc8d t223 = GM_LD(gm_t9qkh0rsooc8d, ((((uint8_t *)t222) + 0) + 2));
        gm_b16_8 t224 = gm_f_shown_o0bbr93((void *)NULL, cx, NULL, t221, t223);
        if (gm_unw.kind) {
            goto gm_ld1_1;
        }
        gm_b0_1 t225 = gm_f_show_861qdlm((void *)NULL, cx, NULL, t224);
        if (gm_unw.kind) {
            goto gm_ld1_1;
        }
    }
    {
        gm_th2lqbo77pvsc t226;
        memset(&t226, 0, sizeof t226);
        gm_l24 = t226;
    }
    {
        uint8_t t228 = gm_l19;
        uint8_t t229 = (uint8_t)!t228;
        if (!t229) goto ie227;
        {
            gm_b16_8 t231 = gm_l10;
            gm_th2lqbo77pvsc t232;
            GM_ST(uint8_t, (uint8_t *)&t232 + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&t232 + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&t232 + 20, 0);
            GM_ST(gm_b16_8, ((uint8_t *)&t232), t231);
            ((uint8_t *)&t232)[16] = 1;
            gm_l24 = t232;
        }
        goto ix227;
ie227:;
        gm_b16_8 t234 = gm_l10;
        uint8_t t235 = gm_f_may_mutate_uj4e7jp((void *)NULL, cx, NULL, t234);
        if (gm_unw.kind) {
            goto gm_ld1_1;
        }
        if (!t235) goto ie233;
        {
            gm_b16_8 t237 = gm_l10;
            gm_tnpifv20irnph t238;
            GM_ST(gm_b16_8, ((uint8_t *)&t238 + 0), t237);
            gm_tabb710d9mq2e t239;
            gm_f_format_uq5dnfv((void *)&t239, (void *)NULL, cx, NULL, t238);
            if (gm_unw.kind) {
                goto gm_ld1_1;
            }
            gm_tabb710d9mq2e t240 = t239;
            if (((((uint8_t *)&t240))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t240) + 8, gm_src3dhhc4laniae, 330, 16);
            gm_b16_8 t241 = GM_LD(gm_b16_8, (((uint8_t *)&t240) + 8));
            gm_b16_8 t242 = t241;
            gm_b16_8 t243 = GM_LD(gm_b16_8, ((uint8_t *)&t242));
            gm_th2lqbo77pvsc t244;
            GM_ST(uint8_t, (uint8_t *)&t244 + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&t244 + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&t244 + 20, 0);
            GM_ST(gm_b16_8, ((uint8_t *)&t244), t243);
            ((uint8_t *)&t244)[16] = 1;
            gm_l24 = t244;
        }
        goto ix233;
ie233:;
ix233:;
ix227:;
    }
    {
        gm_l25 = ((uint8_t)0);
    }
    {
        gm_th2lqbo77pvsc t245;
        memset(&t245, 0, sizeof t245);
        gm_l26 = t245;
    }
    {
        if (!((((uint8_t *)&gm_l24))[16] != 0)) goto ie246;
        gm_b16_8 t247 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l24));
        gm_l27 = t247;
        {
            void * t249 = gm_l0;
            gm_f_replay_texts_3te9pi7((void *)&F.l28, (void *)NULL, cx, NULL, t249, ((uint8_t)1));
            if (gm_unw.kind) {
                goto gm_ld1_1;
            }
        }
        {
            gm_b16_8 t250;
            GM_ST(void *, (uint8_t *)&t250, (void *)(gm_som0q53jqvjh3));
            GM_ST(uintptr_t, (uint8_t *)&t250 + 8, (uintptr_t)(4));
            GM_ST(gm_b16_8, (uint8_t *)&F.l29 + 0, t250);
            gm_b16_8 t251 = gm_l27;
            GM_ST(gm_b16_8, (uint8_t *)&F.l29 + 16, t251);
            gm_b16_8 t252;
            GM_ST(void *, (uint8_t *)&t252, (void *)(gm_so514oj5nbhcu));
            GM_ST(uintptr_t, (uint8_t *)&t252 + 8, (uintptr_t)(1));
            GM_ST(gm_b16_8, (uint8_t *)&F.l29 + 32, t252);
        }
        {
            void * t253 = gm_l0;
            gm_th2lqbo77pvsc t254;
            memset(&t254, 0, sizeof t254);
            gm_b16_8 t255 = gm_f_empty_slice_5nn7ph0();
            gm_b16_8 t256 = t255;
            gm_b16_8 t257 = GM_LD(gm_b16_8, ((uint8_t *)&t256));
            gm_b16_8 t258 = GM_LD(gm_b16_8, (((uint8_t *)&F.l28) + 0));
            gm_b16_8 t259 = t258;
            gm_b16_8 t260 = GM_LD(gm_b16_8, ((uint8_t *)&t259));
            gm_tfeab279c16rr t261 = F.l29;
            gm_td6svc7gs468a t262;
            GM_ST(uint8_t, (uint8_t *)&t262 + 49, 0);
            GM_ST(uint16_t, (uint8_t *)&t262 + 50, 0);
            GM_ST(uint32_t, (uint8_t *)&t262 + 52, 0);
            GM_ST(gm_tfeab279c16rr, ((uint8_t *)&t262), t261);
            ((uint8_t *)&t262)[48] = 1;
            gm_b16_8 t263;
            GM_ST(void *, (uint8_t *)&t263, (void *)(gm_s1qrqfh656g4q));
            GM_ST(uintptr_t, (uint8_t *)&t263 + 8, (uintptr_t)(5));
            gm_f_compile_rj09gp7((void *)&F.l30, (void *)NULL, cx, NULL, t253, &t254, t257, t260, &t262, t263, ((uint8_t)0));
            if (gm_unw.kind) {
                goto gm_ld1_1;
            }
        }
        {
            void * t264 = GM_LD(void *, (((uint8_t *)&F.l30) + 24));
            void * t265 = t264;
            uint8_t t266;
            if ((GM_LD(void *, ((uint8_t *)&t265)) != 0)) goto mn268;
            t266 = ((uint8_t)1);
            goto me267;
mn268:;
            t266 = ((uint8_t)0);
            goto me267;
mn269:;
            gm_trap("no match arm matched the scrutinee", gm_src3dhhc4laniae, 338, 18);
me267:;
            gm_l25 = t266;
        }
        {
            void * t270 = (void *)((uint8_t *)&F.l30);
            gm_b0_1 t271 = gm_f_release_mdh37fq((void *)NULL, cx, NULL, t270);
            if (gm_unw.kind) {
                goto gm_ld1_1;
            }
        }
        {
            void * t272 = (void *)((uint8_t *)&F.l28);
            gm_f_deinit_gvn47sj(t272);
        }
        {
            uint8_t t274 = 0;
            uint8_t t275 = gm_l25;
            uint8_t t276 = (uint8_t)!t275;
            if (!t276) {
                uint64_t t277 = GM_LD(uintptr_t, (((uint8_t *)a2) + 0) + 8);
                uint8_t t278 = (t277 > ((uint64_t)0ULL));
                t274 = t278;
            } else t274 = 1;
            if (!t274) goto ie273;
            {
                gm_b16_8 t280 = gm_l27;
                gm_th2lqbo77pvsc t281;
                GM_ST(uint8_t, (uint8_t *)&t281 + 17, 0);
                GM_ST(uint16_t, (uint8_t *)&t281 + 18, 0);
                GM_ST(uint32_t, (uint8_t *)&t281 + 20, 0);
                GM_ST(gm_b16_8, ((uint8_t *)&t281), t280);
                ((uint8_t *)&t281)[16] = 1;
                gm_l26 = t281;
            }
            goto ix273;
ie273:;
ix273:;
        }
        goto ix246;
ie246:;
ix246:;
    }
    {
        void * t282 = (void *)((uint8_t *)&F.l18);
        void * t283 = gm_l0;
        gm_t9qkh0rsooc8d t284 = GM_LD(gm_t9qkh0rsooc8d, ((((uint8_t *)t283) + 0) + 1));
        void * t285 = gm_l0;
        uint64_t t286 = GM_LD(uintptr_t, ((((uint8_t *)t285) + 88) + 0) + 8);
        uint8_t t287 = (t286 > ((uint64_t)0ULL));
        uint8_t t288 = gm_f_run_aqa5hss((void *)NULL, cx, NULL, t282, t284, t287);
        if (gm_unw.kind) {
            goto gm_ld1_1;
        }
        gm_l31 = t288;
    }
    {
        uint8_t t290 = gm_l31;
        uint8_t t291 = (uint8_t)!t290;
        if (!t291) goto ie289;
        {
            uint8_t t293 = 0;
            F.ret = ((uint8_t)0);
            gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = t293;
            goto gm_ld1_1;
        }
        goto ix289;
ie289:;
ix289:;
    }
    {
        if (!((((uint8_t *)&gm_l26))[16] != 0)) goto ie294;
        gm_b16_8 t295 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l26));
        gm_l32 = t295;
        {
            uint8_t t298 = gm_l25;
            if (!t298) goto ie297;
            {
                gm_f_list_epvducg((void *)&F.l33, (void *)NULL, cx, NULL);
                if (gm_unw.kind) {
                    goto gm_ld1_1;
                }
            }
            {
                {
                    gm_b16_8 t301 = GM_LD(gm_b16_8, (((uint8_t *)a2) + 0));
                    gm_l34 = t301;
                }
                {
                    gm_l35 = ((uint64_t)0ULL);
                }
                {
                    for (;;) {
                        uint64_t t303 = gm_l35;
                        uint64_t t304 = GM_LD(uintptr_t, ((uint8_t *)&gm_l34) + 8);
                        uint8_t t305 = (t303 < t304);
                        if (!t305) goto lb302;
                        {
                            uint64_t t308 = gm_l35;
                            uint64_t t309 = (uintptr_t)t308;
                            uint64_t t310 = GM_LD(uintptr_t, ((uint8_t *)&gm_l34) + 8);
                            gm_t5hdnnftkup6m t313 = GM_LD(gm_t5hdnnftkup6m, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l34)) + t309 * 24));
                            gm_l36 = t313;
                            {
                                void * t315 = (void *)((uint8_t *)&F.l33);
                                gm_b16_8 t316 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l36) + 0));
                                gm_tftb1odc739t5 t317 = gm_f_push_3ef746b(t315, t316);
                                gm_tftb1odc739t5 t318 = t317;
                                if (((((uint8_t *)&t318))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t318) + 1, gm_src3dhhc4laniae, 348, 38);
                                gm_b0_1 t319 = GM_LD(gm_b0_1, (((uint8_t *)&t318) + 1));
                            }
                            goto me311;
mn312:;
                            gm_trap("no match arm matched the scrutinee", gm_src3dhhc4laniae, 348, 13);
me311:;
                        }
lc302:;
                        {
                            uint64_t t321 = gm_l35;
                            uint64_t r322; bool o323 = __builtin_add_overflow(t321, ((uint64_t)1ULL), &r322);
                            uint64_t t324 = r322;
                            gm_l35 = t324;
                        }
                    }
lb302:;
                }
            }
            {
                gm_b16_8 t325 = GM_LD(gm_b16_8, (((uint8_t *)&F.l33) + 0));
                gm_b16_8 t326 = t325;
                gm_b16_8 t327 = GM_LD(gm_b16_8, ((uint8_t *)&t326));
                gm_b16_8 t328;
                GM_ST(void *, (uint8_t *)&t328, (void *)(gm_sg5gdnrith2i6));
                GM_ST(uintptr_t, (uint8_t *)&t328 + 8, (uintptr_t)(2));
                gm_tabb710d9mq2e t329;
                gm_f_join_sfknljb((void *)&t329, (void *)NULL, cx, NULL, t327, t328);
                if (gm_unw.kind) {
                    goto gm_ld1_1;
                }
                gm_tabb710d9mq2e t330 = t329;
                if (((((uint8_t *)&t330))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t330) + 8, gm_src3dhhc4laniae, 349, 23);
                gm_b16_8 t331 = GM_LD(gm_b16_8, (((uint8_t *)&t330) + 8));
                gm_l37 = t331;
            }
            {
                gm_b16_8 t332 = gm_l37;
                gm_b16_8 t333 = t332;
                if (gm_pf454qfrpt6r(cx, gm_smrc1u3r4i5hd, 6, gm_src3dhhc4laniae, 350, 13)) {
                    goto gm_ld1_1;
                }
                if (gm_pfu8dq5bikbh(cx, &gm_tybihgc69jv3, ((uint8_t *)&t333), gm_src3dhhc4laniae, 350, 13)) {
                    goto gm_ld1_1;
                }
                if (gm_pf454qfrpt6r(cx, gm_s68c5jkfvg5dd, 85, gm_src3dhhc4laniae, 350, 13)) {
                    goto gm_ld1_1;
                }
            }
            goto ix297;
ie297:;
ix297:;
        }
        {
            void * t334 = gm_l0;
            void * t335 = (void *)(((uint8_t *)t334) + 88);
            gm_tk9n3vlila9qg t336;
            memset((uint8_t *)&t336 + 33, 0, 7);
            gm_b16_8 t337 = gm_l1;
            gm_tabb710d9mq2e t338;
            gm_f_dupe_g9iqpvn((void *)&t338, (void *)NULL, cx, NULL, t337, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kdmasfkh2un)));
            if (gm_unw.kind) {
                goto gm_ld1_1;
            }
            gm_tabb710d9mq2e t339 = t338;
            if (((((uint8_t *)&t339))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t339) + 8, gm_src3dhhc4laniae, 352, 32);
            gm_b16_8 t340 = GM_LD(gm_b16_8, (((uint8_t *)&t339) + 8));
            gm_b16_8 t341 = t340;
            gm_b16_8 t342 = GM_LD(gm_b16_8, ((uint8_t *)&t341));
            GM_ST(gm_b16_8, (uint8_t *)&t336 + 0, t342);
            gm_b16_8 t343 = gm_l32;
            GM_ST(gm_b16_8, (uint8_t *)&t336 + 16, t343);
            uint8_t t344 = gm_l25;
            GM_ST(uint8_t, (uint8_t *)&t336 + 32, t344);
            gm_tftb1odc739t5 t345 = gm_f_push_ej78v89(t335, &t336);
            gm_tftb1odc739t5 t346 = t345;
            if (((((uint8_t *)&t346))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t346) + 1, gm_src3dhhc4laniae, 352, 9);
            gm_b0_1 t347 = GM_LD(gm_b0_1, (((uint8_t *)&t346) + 1));
        }
        goto ix294;
ie294:;
ix294:;
    }
    {
        void * t348 = gm_l0;
        gm_trqfvmjtt4sij t349 = F.l4;
        GM_ST(gm_trqfvmjtt4sij, (((uint8_t *)t348) + 128), t349);
    }
    {
        uint8_t t350 = 0;
        F.ret = ((uint8_t)1);
        gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = t350;
        goto gm_ld1_1;
    }
    {
        void * t351 = (void *)((uint8_t *)&F.l18);
        gm_b0_1 t352 = gm_f_release_mdh37fq((void *)NULL, cx, NULL, t351);
    }
    goto gm_ls353;
gm_ld1_1:;
    {
        gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
        void * t354 = (void *)((uint8_t *)&F.l18);
        gm_b0_1 t355 = gm_f_release_mdh37fq((void *)NULL, cx, NULL, t354);
        gm_unw = gm_saved;
    }
    goto gm_lad;
gm_ls353:;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_b0_1 gm_f_submit_pah0u77(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, const void * a2) {
    struct gm_fr_submit_pah0u77 F;
    void * gm_l0; /* r */
    gm_b16_8 gm_l1; /* text */
    gm_b16_8 gm_l3; /* items */
    gm_ti5ramhglnvdt gm_l4; /* local */
    gm_th2lqbo77pvsc gm_l5; /* s */
    uint8_t gm_l6; /* all */
    gm_b16_8 gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    gm_b16_8 gm_l10; /* msg */
    gm_th2lqbo77pvsc gm_l11; /* s */
    gm_th2lqbo77pvsc gm_l12; /* s */
    GM_STACK_CHECK(&F, 741u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint8_t t3;
        if ((((((uint8_t *)a2) + 0))[16] != 0)) goto mn5;
        t3 = ((uint8_t)1);
        goto me4;
mn5:;
        t3 = ((uint8_t)0);
        goto me4;
mn6:;
        gm_trap("no match arm matched the scrutinee", gm_src3dhhc4laniae, 213, 8);
me4:;
        if (!t3) goto ie2;
        uint8_t t7;
        if ((((((uint8_t *)a2) + 24))[24] != 0)) goto mn9;
        t7 = ((uint8_t)1);
        goto me8;
mn9:;
        t7 = ((uint8_t)0);
        goto me8;
mn10:;
        gm_trap("no match arm matched the scrutinee", gm_src3dhhc4laniae, 213, 28);
me8:;
        if (!t7) goto ie2;
        {
            gm_b16_8 t12 = gm_l1;
            void * t13 = gm_l0;
            gm_t9qkh0rsooc8d t14 = GM_LD(gm_t9qkh0rsooc8d, ((((uint8_t *)t13) + 0) + 2));
            gm_b16_8 t15 = gm_f_syntax_errors_fk23f5t((void *)NULL, cx, NULL, t12, t14);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_b0_1 t16 = gm_f_show_861qdlm((void *)NULL, cx, NULL, t15);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_b0_1 t17;
            uint8_t t18 = 0;
            F.ret = t17;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        if (!(((((uint8_t *)a2) + 0))[16] != 0)) goto ie19;
        gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)a2) + 0));
        gm_l3 = t20;
        {
            gm_ti5ramhglnvdt t22;
            memset(&t22, 0, sizeof t22);
            gm_l4 = t22;
        }
        {
            if (!(((((uint8_t *)a2) + 24))[24] != 0)) goto ie23;
            gm_th2lqbo77pvsc t24 = GM_LD(gm_th2lqbo77pvsc, (((uint8_t *)a2) + 24));
            gm_l5 = t24;
            {
                gm_l6 = ((uint8_t)1);
            }
            {
                {
                    gm_b16_8 t27 = gm_l3;
                    gm_l7 = t27;
                }
                {
                    gm_l8 = ((uint64_t)0ULL);
                }
                {
                    for (;;) {
                        uint64_t t29 = gm_l8;
                        uint64_t t30 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                        uint8_t t31 = (t29 < t30);
                        if (!t31) goto lb28;
                        {
                            uint64_t t34 = gm_l8;
                            uint64_t t35 = (uintptr_t)t34;
                            uint64_t t36 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                            gm_tk9n3vlila9qg t39 = GM_LD(gm_tk9n3vlila9qg, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l7)) + t35 * 40));
                            F.l9 = t39;
                            {
                                uint8_t t42 = GM_LD(uint8_t, (((uint8_t *)&F.l9) + 32));
                                uint8_t t43 = (uint8_t)!t42;
                                if (!t43) goto ie41;
                                {
                                    gm_l6 = ((uint8_t)0);
                                }
                                goto ix41;
ie41:;
ix41:;
                            }
                            goto me37;
mn38:;
                            gm_trap("no match arm matched the scrutinee", gm_src3dhhc4laniae, 222, 13);
me37:;
                        }
lc28:;
                        {
                            uint64_t t46 = gm_l8;
                            uint64_t r47; bool o48 = __builtin_add_overflow(t46, ((uint64_t)1ULL), &r47);
                            uint64_t t49 = r47;
                            gm_l8 = t49;
                        }
                    }
lb28:;
                }
            }
            {
                uint8_t t51 = gm_l6;
                if (!t51) goto ie50;
                {
                    gm_th2lqbo77pvsc t53 = gm_l5;
                    gm_ti5ramhglnvdt t54;
                    GM_ST(uint8_t, (uint8_t *)&t54 + 25, 0);
                    GM_ST(uint16_t, (uint8_t *)&t54 + 26, 0);
                    GM_ST(uint32_t, (uint8_t *)&t54 + 28, 0);
                    GM_ST(gm_th2lqbo77pvsc, ((uint8_t *)&t54), t53);
                    ((uint8_t *)&t54)[24] = 1;
                    gm_l4 = t54;
                }
                goto ix50;
ie50:;
ix50:;
            }
            goto ix23;
ie23:;
ix23:;
        }
        {
            void * t55 = gm_l0;
            gm_b16_8 t56 = gm_l1;
            gm_b16_8 t57 = gm_l3;
            gm_b16_8 t58 = t57;
            gm_b16_8 t59 = GM_LD(gm_b16_8, ((uint8_t *)&t58));
            gm_th2lqbo77pvsc t60;
            gm_f_try_decls_23vcohd((void *)&t60, (void *)NULL, cx, NULL, t55, t56, t59);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_th2lqbo77pvsc t61 = t60;
            gm_b16_8 t62;
            if (((((uint8_t *)&t61))[16] != 0)) {
                gm_b16_8 t63 = GM_LD(gm_b16_8, (((uint8_t *)&t61) + 0));
                t62 = t63;
            } else {
                gm_b0_1 t64;
                uint8_t t65 = 0;
                F.ret = t64;
                goto gm_fret;
            }
            gm_l10 = t62;
        }
        {
            if (!((((uint8_t *)&gm_l4))[24] != 0)) goto ie66;
            gm_th2lqbo77pvsc t67 = GM_LD(gm_th2lqbo77pvsc, ((uint8_t *)&gm_l4));
            gm_l11 = t67;
            {
                {
                    void * t70 = gm_l0;
                    gm_b16_8 t71 = gm_l1;
                    uint8_t t72 = gm_f_statements_dafpns9((void *)NULL, cx, NULL, t70, t71, &(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l11)));
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
            }
            goto ix66;
ie66:;
            {
                gm_b16_8 t74 = gm_l10;
                gm_b0_1 t75 = gm_f_show_861qdlm((void *)NULL, cx, NULL, t74);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
ix66:;
        }
        {
            gm_b0_1 t76;
            uint8_t t77 = 0;
            F.ret = t76;
            goto gm_fret;
        }
        goto ix19;
ie19:;
ix19:;
    }
    {
        if (!(((((uint8_t *)a2) + 24))[24] != 0)) goto ie78;
        gm_th2lqbo77pvsc t79 = GM_LD(gm_th2lqbo77pvsc, (((uint8_t *)a2) + 24));
        gm_l12 = t79;
        {
            {
                void * t82 = gm_l0;
                gm_b16_8 t83 = gm_l1;
                uint8_t t84 = gm_f_statements_dafpns9((void *)NULL, cx, NULL, t82, t83, &(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l12)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
        }
        goto ix78;
ie78:;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_submit_text_7dvfev6(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1) {
    struct gm_fr_submit_text_7dvfev6 F;
    void * gm_l0; /* r */
    gm_b16_8 gm_l1; /* text */
    GM_STACK_CHECK(&F, 128u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        gm_b16_8 t3 = gm_l1;
        gm_b16_8 t4 = gm_l1;
        gm_t9feugf5hcdtg t5;
        gm_f_classify_rm38cme((void *)&t5, (void *)NULL, cx, NULL, t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b0_1 t6 = gm_f_submit_pah0u77((void *)NULL, cx, NULL, t2, t3, &t5);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_trim_newlines_9rj3i73(gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_trim_newlines_9rj3i73 F;
    gm_b16_8 gm_l0; /* s */
    uint64_t gm_l1; /* n */
    GM_STACK_CHECK(&F, 186u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint64_t t2 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        gm_l1 = t2;
    }
    {
        for (;;) {
            uint64_t t4 = gm_l1;
            uint8_t t5 = (t4 > ((uint64_t)0ULL));
            if (!t5) goto lb3;
            uint64_t t6 = gm_l1;
            uint64_t r7; bool o8 = __builtin_sub_overflow(t6, ((uint64_t)1ULL), &r7);
            if (o8) gm_trap("integer overflow in `-`", gm_src3dhhc4laniae, 503, 23);
            uint64_t t9 = r7;
            uint64_t t10 = (uintptr_t)t9;
            uint64_t t11 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            if (t10 >= (uintptr_t)(t11)) gm_trap_index(t10, t11, gm_src3dhhc4laniae, 503, 21);
            uint8_t t12 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t10 * 1));
            gm_tqs5p4n2tq19v t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)10ULL));
            GM_ST(uint8_t, ((uint8_t *)&t13 + 1), ((uint8_t)13ULL));
            gm_tqs5p4n2tq19v t14 = t13;
            uint8_t t15 = 0;
            uint8_t t16 = t12;
            if (gm_eqjmccqdb90f(((uint8_t *)&t14) + 0, ((uint8_t *)&t16))) t15 = 1;
            if (gm_eqjmccqdb90f(((uint8_t *)&t14) + 1, ((uint8_t *)&t16))) t15 = 1;
            uint8_t t17 = (uint8_t)(t15);
            if (!t17) goto lb3;
            {
                uint64_t t20 = gm_l1;
                uint64_t r21; bool o22 = __builtin_sub_overflow(t20, ((uint64_t)1ULL), &r21);
                if (o22) gm_trap("integer overflow in `-`", gm_src3dhhc4laniae, 503, 48);
                uint64_t t23 = r21;
                gm_l1 = t23;
            }
lc3:;
        }
lb3:;
    }
    {
        uint64_t t24 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        void * t25 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)));
        uint64_t t26 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t27 = gm_l1;
        uint64_t t28 = (uintptr_t)t27;
        if (t26 > t28 || t28 > (uintptr_t)t24) gm_trap_slice(t26, t28, t24, gm_src3dhhc4laniae, 504, 12);
        gm_b16_8 t29;
        GM_ST(void *, (uint8_t *)&t29, (void *)(((uint8_t *)t25 + t26 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t29 + 8, (uintptr_t)(t28 - t26));
        uint8_t t30 = 0;
        F.ret = t29;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) void gm_f_try_decls_23vcohd(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2) {
    struct gm_fr_try_decls_23vcohd F;
    void * gm_l0; /* r */
    gm_b16_8 gm_l1; /* text */
    gm_b16_8 gm_l2; /* items */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    gm_b16_8 gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    gm_b16_8 gm_l9; /* n */
    gm_b16_8 gm_l13; /* <tmp> */
    uint64_t gm_l14; /* <tmp> */
    gm_t6j6smj7qoctu gm_l15; /* ch */
    uint8_t gm_l16; /* replaced */
    gm_b16_8 gm_l17; /* <tmp> */
    uint64_t gm_l18; /* <tmp> */
    gm_b16_8 gm_l19; /* n */
    gm_b16_8 gm_l20; /* <tmp> */
    uint64_t gm_l21; /* <tmp> */
    gm_b16_8 gm_l22; /* x */
    gm_b16_8 gm_l23; /* <tmp> */
    uint64_t gm_l24; /* <tmp> */
    GM_STACK_CHECK(&F, 1533u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        gm_f_list_epvducg((void *)&F.l3, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t3 = gm_l2;
            gm_l4 = t3;
        }
        {
            gm_l5 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t5 = gm_l5;
                uint64_t t6 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                uint8_t t7 = (t5 < t6);
                if (!t7) goto lb4;
                {
                    uint64_t t10 = gm_l5;
                    uint64_t t11 = (uintptr_t)t10;
                    uint64_t t12 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                    gm_tk9n3vlila9qg t15 = GM_LD(gm_tk9n3vlila9qg, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t11 * 40));
                    F.l6 = t15;
                    {
                        {
                            gm_b16_8 t18 = GM_LD(gm_b16_8, (((uint8_t *)&F.l6) + 0));
                            gm_l7 = t18;
                        }
                        {
                            gm_l8 = ((uint64_t)0ULL);
                        }
                        {
                            for (;;) {
                                uint64_t t20 = gm_l8;
                                uint64_t t21 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                                uint8_t t22 = (t20 < t21);
                                if (!t22) goto lb19;
                                {
                                    uint64_t t25 = gm_l8;
                                    uint64_t t26 = (uintptr_t)t25;
                                    uint64_t t27 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                                    gm_b16_8 t30 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l7)) + t26 * 16));
                                    gm_l9 = t30;
                                    {
                                        uint8_t t33 = (((GM_LD(uintptr_t, ((uint8_t *)&gm_l9) + 8) == 4) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l9)), gm_sog4rksrmsqd2, 4) == 0));
                                        if (!t33) goto ie32;
                                        {
                                            gm_b16_8 t35;
                                            GM_ST(void *, (uint8_t *)&t35, (void *)(gm_sn1sq59pu45vv));
                                            GM_ST(uintptr_t, (uint8_t *)&t35 + 8, (uintptr_t)(38));
                                            gm_th2lqbo77pvsc t36;
                                            GM_ST(uint8_t, (uint8_t *)&t36 + 17, 0);
                                            GM_ST(uint16_t, (uint8_t *)&t36 + 18, 0);
                                            GM_ST(uint32_t, (uint8_t *)&t36 + 20, 0);
                                            GM_ST(gm_b16_8, ((uint8_t *)&t36), t35);
                                            ((uint8_t *)&t36)[16] = 1;
                                            uint8_t t37 = 0;
                                            F.ret = t36;
                                            goto gm_fret;
                                        }
                                        goto ix32;
ie32:;
ix32:;
                                    }
                                    {
                                        void * t38 = (void *)((uint8_t *)&F.l3);
                                        gm_b16_8 t39 = gm_l9;
                                        gm_tftb1odc739t5 t40 = gm_f_push_3ef746b(t38, t39);
                                        gm_tftb1odc739t5 t41 = t40;
                                        if (((((uint8_t *)&t41))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t41) + 1, gm_src3dhhc4laniae, 244, 13);
                                        gm_b0_1 t42 = GM_LD(gm_b0_1, (((uint8_t *)&t41) + 1));
                                    }
                                    goto me28;
mn29:;
                                    gm_trap("no match arm matched the scrutinee", gm_src3dhhc4laniae, 242, 9);
me28:;
                                }
lc19:;
                                {
                                    uint64_t t44 = gm_l8;
                                    uint64_t r45; bool o46 = __builtin_add_overflow(t44, ((uint64_t)1ULL), &r45);
                                    uint64_t t47 = r45;
                                    gm_l8 = t47;
                                }
                            }
lb19:;
                        }
                    }
                    goto me13;
mn14:;
                    gm_trap("no match arm matched the scrutinee", gm_src3dhhc4laniae, 241, 5);
me13:;
                }
lc4:;
                {
                    uint64_t t49 = gm_l5;
                    uint64_t r50; bool o51 = __builtin_add_overflow(t49, ((uint64_t)1ULL), &r50);
                    uint64_t t52 = r50;
                    gm_l5 = t52;
                }
            }
lb4:;
        }
    }
    {
        void * t53 = gm_l0;
        gm_f_replay_texts_3te9pi7((void *)&F.l10, (void *)NULL, cx, NULL, t53, ((uint8_t)0));
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t54 = gm_l0;
        gm_b16_8 t55 = gm_l1;
        gm_th2lqbo77pvsc t56;
        GM_ST(uint8_t, (uint8_t *)&t56 + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&t56 + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&t56 + 20, 0);
        GM_ST(gm_b16_8, ((uint8_t *)&t56), t55);
        ((uint8_t *)&t56)[16] = 1;
        gm_b16_8 t57 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
        gm_b16_8 t58 = t57;
        gm_b16_8 t59 = GM_LD(gm_b16_8, ((uint8_t *)&t58));
        gm_b16_8 t60 = GM_LD(gm_b16_8, (((uint8_t *)&F.l10) + 0));
        gm_b16_8 t61 = t60;
        gm_b16_8 t62 = GM_LD(gm_b16_8, ((uint8_t *)&t61));
        gm_td6svc7gs468a t63;
        memset(&t63, 0, sizeof t63);
        gm_b16_8 t64 = GM_LD(gm_b16_8, gm_cd231ebi1kg);
        gm_f_compile_rj09gp7((void *)&F.l11, (void *)NULL, cx, NULL, t54, &t56, t59, t62, &t63, t64, ((uint8_t)0));
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t65 = (void *)((uint8_t *)&F.l10);
        gm_f_deinit_gvn47sj(t65);
    }
    {
    }
    {
        void * t69 = GM_LD(void *, (((uint8_t *)&F.l11) + 24));
        void * t70 = t69;
        uint8_t t71;
        if ((GM_LD(void *, ((uint8_t *)&t70)) != 0)) goto mn73;
        t71 = ((uint8_t)1);
        goto me72;
mn73:;
        t71 = ((uint8_t)0);
        goto me72;
mn74:;
        gm_trap("no match arm matched the scrutinee", gm_src3dhhc4laniae, 251, 8);
me72:;
        if (!t71) goto ie68;
        {
            void * t76 = (void *)((uint8_t *)&F.l11);
            void * t77 = gm_l0;
            gm_t9qkh0rsooc8d t78 = GM_LD(gm_t9qkh0rsooc8d, ((((uint8_t *)t77) + 0) + 2));
            gm_b16_8 t79 = gm_f_shown_o0bbr93((void *)NULL, cx, NULL, t76, t78);
            if (gm_unw.kind) {
                goto gm_ld1_1;
            }
            gm_th2lqbo77pvsc t80;
            GM_ST(uint8_t, (uint8_t *)&t80 + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&t80 + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&t80 + 20, 0);
            GM_ST(gm_b16_8, ((uint8_t *)&t80), t79);
            ((uint8_t *)&t80)[16] = 1;
            uint8_t t81 = 0;
            F.ret = t80;
            gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = t81;
            goto gm_ld1_1;
        }
        goto ix68;
ie68:;
ix68:;
    }
    {
        void * t82 = (void *)((uint8_t *)&F.l11);
        void * t83 = gm_l0;
        gm_t9qkh0rsooc8d t84 = GM_LD(gm_t9qkh0rsooc8d, ((((uint8_t *)t83) + 0) + 2));
        gm_b16_8 t85 = gm_f_shown_o0bbr93((void *)NULL, cx, NULL, t82, t84);
        if (gm_unw.kind) {
            goto gm_ld1_1;
        }
        gm_b0_1 t86 = gm_f_show_861qdlm((void *)NULL, cx, NULL, t85);
        if (gm_unw.kind) {
            goto gm_ld1_1;
        }
    }
    {
        gm_f_list_1t36jvm((void *)&F.l12, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_ld1_1;
        }
    }
    {
        {
            void * t88 = gm_l0;
            gm_b16_8 t89 = GM_LD(gm_b16_8, ((((uint8_t *)t88) + 48) + 0));
            gm_l13 = t89;
        }
        {
            gm_l14 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t91 = gm_l14;
                uint64_t t92 = GM_LD(uintptr_t, ((uint8_t *)&gm_l13) + 8);
                uint8_t t93 = (t91 < t92);
                if (!t93) goto lb90;
                {
                    uint64_t t96 = gm_l14;
                    uint64_t t97 = (uintptr_t)t96;
                    uint64_t t98 = GM_LD(uintptr_t, ((uint8_t *)&gm_l13) + 8);
                    gm_t6j6smj7qoctu t101 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l13)) + t97 * 32));
                    gm_l15 = t101;
                    {
                        gm_l16 = ((uint8_t)0);
                    }
                    {
                        {
                            gm_b16_8 t104 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l15) + 0));
                            gm_l17 = t104;
                        }
                        {
                            gm_l18 = ((uint64_t)0ULL);
                        }
                        {
                            for (;;) {
                                uint64_t t106 = gm_l18;
                                uint64_t t107 = GM_LD(uintptr_t, ((uint8_t *)&gm_l17) + 8);
                                uint8_t t108 = (t106 < t107);
                                if (!t108) goto lb105;
                                {
                                    uint64_t t111 = gm_l18;
                                    uint64_t t112 = (uintptr_t)t111;
                                    uint64_t t113 = GM_LD(uintptr_t, ((uint8_t *)&gm_l17) + 8);
                                    gm_b16_8 t116 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l17)) + t112 * 16));
                                    gm_l19 = t116;
                                    {
                                        {
                                            gm_b16_8 t119 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
                                            gm_l20 = t119;
                                        }
                                        {
                                            gm_l21 = ((uint64_t)0ULL);
                                        }
                                        {
                                            for (;;) {
                                                uint64_t t121 = gm_l21;
                                                uint64_t t122 = GM_LD(uintptr_t, ((uint8_t *)&gm_l20) + 8);
                                                uint8_t t123 = (t121 < t122);
                                                if (!t123) goto lb120;
                                                {
                                                    uint64_t t126 = gm_l21;
                                                    uint64_t t127 = (uintptr_t)t126;
                                                    uint64_t t128 = GM_LD(uintptr_t, ((uint8_t *)&gm_l20) + 8);
                                                    gm_b16_8 t131 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l20)) + t127 * 16));
                                                    gm_l22 = t131;
                                                    {
                                                        gm_b16_8 t134 = gm_l19;
                                                        gm_b16_8 t135 = gm_l22;
                                                        gm_b16_8 t136 = t134;
                                                        gm_b16_8 t137 = t135;
                                                        uint8_t t138 = (gm_eq0iqiubfm7e(((uint8_t *)&t136), ((uint8_t *)&t137)));
                                                        if (!t138) goto ie133;
                                                        {
                                                            gm_l16 = ((uint8_t)1);
                                                        }
                                                        goto ix133;
ie133:;
ix133:;
                                                    }
                                                    goto me129;
mn130:;
                                                    gm_trap("no match arm matched the scrutinee", gm_src3dhhc4laniae, 257, 13);
me129:;
                                                }
lc120:;
                                                {
                                                    uint64_t t141 = gm_l21;
                                                    uint64_t r142; bool o143 = __builtin_add_overflow(t141, ((uint64_t)1ULL), &r142);
                                                    uint64_t t144 = r142;
                                                    gm_l21 = t144;
                                                }
                                            }
lb120:;
                                        }
                                    }
                                    goto me114;
mn115:;
                                    gm_trap("no match arm matched the scrutinee", gm_src3dhhc4laniae, 256, 9);
me114:;
                                }
lc105:;
                                {
                                    uint64_t t146 = gm_l18;
                                    uint64_t r147; bool o148 = __builtin_add_overflow(t146, ((uint64_t)1ULL), &r147);
                                    uint64_t t149 = r147;
                                    gm_l18 = t149;
                                }
                            }
lb105:;
                        }
                    }
                    {
                        uint8_t t151 = gm_l16;
                        uint8_t t152 = (uint8_t)!t151;
                        if (!t152) goto ie150;
                        {
                            void * t154 = (void *)((uint8_t *)&F.l12);
                            gm_tftb1odc739t5 t155 = gm_f_push_52bt1f1(t154, &(*(gm_t6j6smj7qoctu *)((uint8_t *)&gm_l15)));
                            gm_tftb1odc739t5 t156 = t155;
                            if (((((uint8_t *)&t156))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t156) + 1, gm_src3dhhc4laniae, 261, 27);
                            gm_b0_1 t157 = GM_LD(gm_b0_1, (((uint8_t *)&t156) + 1));
                        }
                        goto ix150;
ie150:;
ix150:;
                    }
                    goto me99;
mn100:;
                    gm_trap("no match arm matched the scrutinee", gm_src3dhhc4laniae, 254, 5);
me99:;
                }
lc90:;
                {
                    uint64_t t159 = gm_l14;
                    uint64_t r160; bool o161 = __builtin_add_overflow(t159, ((uint64_t)1ULL), &r160);
                    uint64_t t162 = r160;
                    gm_l14 = t162;
                }
            }
lb90:;
        }
    }
    {
        {
            gm_b16_8 t164 = gm_l2;
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
                    gm_tk9n3vlila9qg t176 = GM_LD(gm_tk9n3vlila9qg, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l23)) + t172 * 40));
                    F.l25 = t176;
                    {
                        void * t178 = (void *)((uint8_t *)&F.l12);
                        gm_t6j6smj7qoctu t179;
                        gm_b16_8 t180 = GM_LD(gm_b16_8, (((uint8_t *)&F.l25) + 0));
                        GM_ST(gm_b16_8, (uint8_t *)&t179 + 0, t180);
                        gm_b16_8 t181 = GM_LD(gm_b16_8, (((uint8_t *)&F.l25) + 16));
                        GM_ST(gm_b16_8, (uint8_t *)&t179 + 16, t181);
                        gm_tftb1odc739t5 t182 = gm_f_push_52bt1f1(t178, &t179);
                        gm_tftb1odc739t5 t183 = t182;
                        if (((((uint8_t *)&t183))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t183) + 1, gm_src3dhhc4laniae, 263, 22);
                        gm_b0_1 t184 = GM_LD(gm_b0_1, (((uint8_t *)&t183) + 1));
                    }
                    goto me174;
mn175:;
                    gm_trap("no match arm matched the scrutinee", gm_src3dhhc4laniae, 263, 5);
me174:;
                }
lc165:;
                {
                    uint64_t t186 = gm_l24;
                    uint64_t r187; bool o188 = __builtin_add_overflow(t186, ((uint64_t)1ULL), &r187);
                    uint64_t t189 = r187;
                    gm_l24 = t189;
                }
            }
lb165:;
        }
    }
    {
        void * t190 = gm_l0;
        gm_trqfvmjtt4sij t191 = F.l12;
        GM_ST(gm_trqfvmjtt4sij, (((uint8_t *)t190) + 48), t191);
    }
    {
        gm_th2lqbo77pvsc t192;
        memset(&t192, 0, sizeof t192);
        uint8_t t193 = 0;
        F.ret = t192;
        gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = t193;
        goto gm_ld1_1;
    }
    {
        void * t194 = (void *)((uint8_t *)&F.l11);
        gm_b0_1 t195 = gm_f_release_mdh37fq((void *)NULL, cx, NULL, t194);
    }
    goto gm_ls196;
gm_ld1_1:;
    {
        gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
        void * t197 = (void *)((uint8_t *)&F.l11);
        gm_b0_1 t198 = gm_f_release_mdh37fq((void *)NULL, cx, NULL, t197);
        gm_unw = gm_saved;
    }
    goto gm_lad;
gm_ls196:;
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
__attribute__((aligned(4))) gm_b0_1 gm_f_type_of_b1a5sco(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1) {
    struct gm_fr_type_of_b1a5sco F;
    void * gm_l0; /* r */
    gm_b16_8 gm_l1; /* source_text */
    gm_b16_8 gm_l4; /* expr */
    GM_STACK_CHECK(&F, 1024u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        gm_f_replay_texts_3te9pi7((void *)&F.l2, (void *)NULL, cx, NULL, t2, ((uint8_t)0));
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
    }
    {
        void * t4 = gm_l0;
        gm_f_renamed_vars_fho5hvj((void *)&F.l3, (void *)NULL, cx, NULL, t4);
        if (gm_unw.kind) {
            goto gm_ld1_1;
        }
    }
    {
        gm_b16_8 t5 = gm_l1;
        gm_l4 = t5;
    }
    {
        uint64_t t7 = GM_LD(uintptr_t, (((uint8_t *)&F.l3) + 0) + 8);
        uint8_t t8 = (t7 > ((uint64_t)0ULL));
        if (!t8) goto ie6;
        {
            gm_b16_8 t10 = gm_l1;
            gm_b16_8 t11 = gm_f_empty_slice_q5bjns8();
            gm_b16_8 t12 = t11;
            gm_b16_8 t13 = GM_LD(gm_b16_8, ((uint8_t *)&t12));
            gm_b16_8 t14 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
            gm_b16_8 t15 = t14;
            gm_b16_8 t16 = GM_LD(gm_b16_8, ((uint8_t *)&t15));
            gm_b16_8 t17 = gm_f_rename_47b7og5((void *)NULL, cx, NULL, t10, t13, t16);
            if (gm_unw.kind) {
                goto gm_ld1_1;
            }
            gm_l4 = t17;
        }
        goto ix6;
ie6:;
ix6:;
    }
    {
        gm_b16_8 t18;
        GM_ST(void *, (uint8_t *)&t18, (void *)(gm_skkiv5ia4t4au));
        GM_ST(uintptr_t, (uint8_t *)&t18 + 8, (uintptr_t)(38));
        GM_ST(gm_b16_8, (uint8_t *)&F.l5 + 0, t18);
        gm_b16_8 t19 = gm_l4;
        GM_ST(gm_b16_8, (uint8_t *)&F.l5 + 16, t19);
        gm_b16_8 t20;
        GM_ST(void *, (uint8_t *)&t20, (void *)(gm_squacltecdsom));
        GM_ST(uintptr_t, (uint8_t *)&t20 + 8, (uintptr_t)(4));
        GM_ST(gm_b16_8, (uint8_t *)&F.l5 + 32, t20);
    }
    {
        void * t21 = gm_l0;
        gm_th2lqbo77pvsc t22;
        memset(&t22, 0, sizeof t22);
        gm_b16_8 t23 = gm_f_empty_slice_5nn7ph0();
        gm_b16_8 t24 = t23;
        gm_b16_8 t25 = GM_LD(gm_b16_8, ((uint8_t *)&t24));
        gm_b16_8 t26 = GM_LD(gm_b16_8, (((uint8_t *)&F.l2) + 0));
        gm_b16_8 t27 = t26;
        gm_b16_8 t28 = GM_LD(gm_b16_8, ((uint8_t *)&t27));
        gm_tfeab279c16rr t29 = F.l5;
        gm_td6svc7gs468a t30;
        GM_ST(uint8_t, (uint8_t *)&t30 + 49, 0);
        GM_ST(uint16_t, (uint8_t *)&t30 + 50, 0);
        GM_ST(uint32_t, (uint8_t *)&t30 + 52, 0);
        GM_ST(gm_tfeab279c16rr, ((uint8_t *)&t30), t29);
        ((uint8_t *)&t30)[48] = 1;
        gm_b16_8 t31 = GM_LD(gm_b16_8, gm_cd231ebi1kg);
        gm_f_compile_rj09gp7((void *)&F.l6, (void *)NULL, cx, NULL, t21, &t22, t25, t28, &t30, t31, ((uint8_t)1));
        if (gm_unw.kind) {
            goto gm_ld1_1;
        }
    }
    {
        void * t33 = GM_LD(void *, (((uint8_t *)&F.l6) + 24));
        void * t34 = t33;
        uint8_t t35;
        if ((GM_LD(void *, ((uint8_t *)&t34)) != 0)) goto mn37;
        t35 = ((uint8_t)1);
        goto me36;
mn37:;
        t35 = ((uint8_t)0);
        goto me36;
mn38:;
        gm_trap("no match arm matched the scrutinee", gm_src3dhhc4laniae, 378, 8);
me36:;
        if (!t35) goto ie32;
        {
            gm_b16_8 t40;
            GM_ST(void *, (uint8_t *)&t40, (void *)(gm_sojb2hhp0ln7r));
            GM_ST(uintptr_t, (uint8_t *)&t40 + 8, (uintptr_t)(30));
            GM_ST(gm_b16_8, (uint8_t *)&F.l7 + 0, t40);
            gm_b16_8 t41 = gm_l4;
            GM_ST(gm_b16_8, (uint8_t *)&F.l7 + 16, t41);
            gm_b16_8 t42;
            GM_ST(void *, (uint8_t *)&t42, (void *)(gm_s055t7hpo88bt));
            GM_ST(uintptr_t, (uint8_t *)&t42 + 8, (uintptr_t)(3));
            GM_ST(gm_b16_8, (uint8_t *)&F.l7 + 32, t42);
        }
        {
            void * t43 = gm_l0;
            gm_th2lqbo77pvsc t44;
            memset(&t44, 0, sizeof t44);
            gm_b16_8 t45 = gm_f_empty_slice_5nn7ph0();
            gm_b16_8 t46 = t45;
            gm_b16_8 t47 = GM_LD(gm_b16_8, ((uint8_t *)&t46));
            gm_b16_8 t48 = GM_LD(gm_b16_8, (((uint8_t *)&F.l2) + 0));
            gm_b16_8 t49 = t48;
            gm_b16_8 t50 = GM_LD(gm_b16_8, ((uint8_t *)&t49));
            gm_tfeab279c16rr t51 = F.l7;
            gm_td6svc7gs468a t52;
            GM_ST(uint8_t, (uint8_t *)&t52 + 49, 0);
            GM_ST(uint16_t, (uint8_t *)&t52 + 50, 0);
            GM_ST(uint32_t, (uint8_t *)&t52 + 52, 0);
            GM_ST(gm_tfeab279c16rr, ((uint8_t *)&t52), t51);
            ((uint8_t *)&t52)[48] = 1;
            gm_b16_8 t53 = GM_LD(gm_b16_8, gm_cd231ebi1kg);
            gm_f_compile_rj09gp7((void *)&F.l8, (void *)NULL, cx, NULL, t43, &t44, t47, t50, &t52, t53, ((uint8_t)1));
            if (gm_unw.kind) {
                goto gm_ld1_1;
            }
        }
        {
            void * t55 = GM_LD(void *, (((uint8_t *)&F.l8) + 24));
            void * t56 = t55;
            uint8_t t57;
            if ((GM_LD(void *, ((uint8_t *)&t56)) != 0)) goto mn59;
            t57 = ((uint8_t)1);
            goto me58;
mn59:;
            t57 = ((uint8_t)0);
            goto me58;
mn60:;
            gm_trap("no match arm matched the scrutinee", gm_src3dhhc4laniae, 381, 12);
me58:;
            if (!t57) goto ie54;
            {
                void * t62 = (void *)((uint8_t *)&F.l6);
                void * t63 = gm_l0;
                gm_t9qkh0rsooc8d t64 = GM_LD(gm_t9qkh0rsooc8d, ((((uint8_t *)t63) + 0) + 2));
                gm_b16_8 t65 = gm_f_shown_o0bbr93((void *)NULL, cx, NULL, t62, t64);
                if (gm_unw.kind) {
                    goto gm_ld1_1;
                }
                gm_b0_1 t66 = gm_f_show_861qdlm((void *)NULL, cx, NULL, t65);
                if (gm_unw.kind) {
                    goto gm_ld1_1;
                }
            }
            {
                void * t67 = (void *)((uint8_t *)&F.l6);
                gm_b0_1 t68 = gm_f_release_mdh37fq((void *)NULL, cx, NULL, t67);
                if (gm_unw.kind) {
                    goto gm_ld1_1;
                }
            }
            {
                void * t69 = (void *)((uint8_t *)&F.l8);
                gm_b0_1 t70 = gm_f_release_mdh37fq((void *)NULL, cx, NULL, t69);
                if (gm_unw.kind) {
                    goto gm_ld1_1;
                }
            }
            {
                gm_b0_1 t71;
                uint8_t t72 = 0;
                F.ret = t71;
                gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = t72;
                goto gm_ld1_1;
            }
            goto ix54;
ie54:;
ix54:;
        }
        {
            void * t73 = (void *)((uint8_t *)&F.l6);
            gm_b0_1 t74 = gm_f_release_mdh37fq((void *)NULL, cx, NULL, t73);
            if (gm_unw.kind) {
                goto gm_ld1_1;
            }
        }
        {
            gm_toqvmi7jqorid t75 = F.l8;
            F.l6 = t75;
        }
        goto ix32;
ie32:;
ix32:;
    }
    {
        {
            void * t77 = (void *)((uint8_t *)&F.l6);
            void * t78 = gm_l0;
            gm_t9qkh0rsooc8d t79 = GM_LD(gm_t9qkh0rsooc8d, ((((uint8_t *)t78) + 0) + 1));
            void * t80 = gm_l0;
            uint64_t t81 = GM_LD(uintptr_t, ((((uint8_t *)t80) + 88) + 0) + 8);
            uint8_t t82 = (t81 > ((uint64_t)0ULL));
            uint8_t t83 = gm_f_run_aqa5hss((void *)NULL, cx, NULL, t77, t79, t82);
            if (gm_unw.kind) {
                goto gm_ld1_1;
            }
        }
    }
    {
        void * t84 = (void *)((uint8_t *)&F.l6);
        gm_b0_1 t85 = gm_f_release_mdh37fq((void *)NULL, cx, NULL, t84);
        if (gm_unw.kind) {
            goto gm_ld1_1;
        }
    }
    {
        void * t86 = (void *)((uint8_t *)&F.l2);
        gm_f_deinit_gvn47sj(t86);
    }
    goto gm_ls87;
gm_ld1_1:;
    {
        gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
        void * t88 = (void *)((uint8_t *)&F.l2);
        gm_f_deinit_gvn47sj(t88);
        gm_unw = gm_saved;
    }
    goto gm_lad;
gm_ls87:;
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
