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
typedef struct gm_cs2lqoac0a5ng4 { _Bool f0; uint32_t f1; uint64_t f2; } gm_cs2lqoac0a5ng4;
typedef struct gm_csguankko4lmhd { uint32_t f0; } gm_csguankko4lmhd;
typedef struct { uint64_t f0; uint64_t f1; } gm_t2lqu8jsj511d;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { unsigned __int128 f0; unsigned __int128 f1; } gm_t793vlgpa3b88;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint8_t f0; uint8_t p1[1]; uint16_t f1; } gm_tjcj4j2gbh6c8;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { void * f0; void * f1; uint32_t f2; uint32_t f3; } gm_tmm67r2c94d5j;
typedef struct { uint8_t f0; uint8_t p1[3]; uint32_t f1; uint64_t f2; } gm_tuglhjjmj2g58;
_Static_assert(sizeof(gm_cs2lqoac0a5ng4) == 16 && _Alignof(gm_cs2lqoac0a5ng4) == 8 && offsetof(gm_cs2lqoac0a5ng4, f0) == 0 && offsetof(gm_cs2lqoac0a5ng4, f1) == 4 && offsetof(gm_cs2lqoac0a5ng4, f2) == 8, "layout of `IntInfo`");
_Static_assert(sizeof(gm_csguankko4lmhd) == 4 && _Alignof(gm_csguankko4lmhd) == 4 && offsetof(gm_csguankko4lmhd, f0) == 0, "layout of `Mutex`");
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_t08olq97rscn3;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; } gm_t0uuhaljsf410;
_Static_assert(sizeof(gm_t2lqu8jsj511d) == 16 && _Alignof(gm_t2lqu8jsj511d) == 8 && offsetof(gm_t2lqu8jsj511d, f0) == 0 && offsetof(gm_t2lqu8jsj511d, f1) == 8, "layout of `(u64, u64)`");
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tuglhjjmj2g58 v0; uint16_t v1; } f1; } gm_t5v8n6qrn4rh4;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
_Static_assert(sizeof(gm_t793vlgpa3b88) == 32 && _Alignof(gm_t793vlgpa3b88) == 16 && offsetof(gm_t793vlgpa3b88, f0) == 0 && offsetof(gm_t793vlgpa3b88, f1) == 16, "layout of `(lo: u128, hi: u128)`");
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t f1; } gm_taari1s0m51a7;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
typedef struct { uint64_t f0; uint64_t f1; gm_b16_8 f2; } gm_tbccvikg6b96e;
typedef struct { uint8_t f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; } gm_tbngattl1fo51;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tcmkompbl09il;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; gm_t88rcucg1b7uk f2; } gm_tcmtrmhj7ak4u;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; } gm_tfeab279c16rr;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; uint64_t f3; uint64_t f4; } gm_tg23pq9a29gh1;
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; } gm_tgglga8ro3isc;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; gm_b16_8 f4; gm_b16_8 f5; gm_b16_8 f6; } gm_th2d9c7aju8hp;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; gm_b16_8 f4; gm_b16_8 f5; } gm_tif3elqk01bdl;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; uint32_t f3; uint8_t pe[4]; } gm_tj4tbafenpsjm;
_Static_assert(sizeof(gm_tjcj4j2gbh6c8) == 4 && _Alignof(gm_tjcj4j2gbh6c8) == 2 && offsetof(gm_tjcj4j2gbh6c8, f0) == 0 && offsetof(gm_tjcj4j2gbh6c8, f1) == 2, "layout of `(signed: bool, bits: u16)`");
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_tmm67r2c94d5j) == 24 && _Alignof(gm_tmm67r2c94d5j) == 8 && offsetof(gm_tmm67r2c94d5j, f0) == 0 && offsetof(gm_tmm67r2c94d5j, f1) == 8 && offsetof(gm_tmm67r2c94d5j, f2) == 16 && offsetof(gm_tmm67r2c94d5j, f3) == 20, "layout of `Printer`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t88rcucg1b7uk v0; } f1; } gm_tnnjeerolos0a;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
typedef struct { gm_b16_8 f0; uint32_t f1; uint8_t pe[4]; } gm_tommf4j9upomc;
typedef struct { gm_b16_8 f0; uint32_t f1; uint8_t p2[4]; gm_b16_8 f2; gm_b16_8 f3; gm_b16_8 f4; } gm_tp6khiphboddr;
typedef struct { gm_tuglhjjmj2g58 f0; uint8_t f1; uint8_t pe[7]; } gm_tqcc6dmlne11b;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_t9qkh0rsooc8d v3; } f1; } gm_ts6oqppjh0ba3;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; gm_b16_8 f4; } gm_tud7oovfvnf43;
_Static_assert(sizeof(gm_tuglhjjmj2g58) == 16 && _Alignof(gm_tuglhjjmj2g58) == 8 && offsetof(gm_tuglhjjmj2g58, f0) == 0 && offsetof(gm_tuglhjjmj2g58, f1) == 4 && offsetof(gm_tuglhjjmj2g58, f2) == 8, "layout of `(signed: bool, bits: u32, storage: u64)`");
_Static_assert(sizeof(gm_t08olq97rscn3) == 32 && _Alignof(gm_t08olq97rscn3) == 8 && offsetof(gm_t08olq97rscn3, f0) == 0 && offsetof(gm_t08olq97rscn3, f1) == 16, "layout of `File`");
_Static_assert(sizeof(gm_t0uuhaljsf410) == 64 && _Alignof(gm_t0uuhaljsf410) == 8 && offsetof(gm_t0uuhaljsf410, f0) == 0 && offsetof(gm_t0uuhaljsf410, f1) == 16 && offsetof(gm_t0uuhaljsf410, f2) == 32 && offsetof(gm_t0uuhaljsf410, f3) == 48, "layout of `(string, string, string, string)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tcmkompbl09il v0; gm_tiilmfd16v3fc v1; gm_b16_8 v2; } f1; } gm_t428ue2u3hf5g;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t08olq97rscn3 v0; gm_b16_8 v1; } f1; } gm_t5o0tpv5jka2l;
_Static_assert(sizeof(gm_t5v8n6qrn4rh4) == 24 && _Alignof(gm_t5v8n6qrn4rh4) == 8 && offsetof(gm_t5v8n6qrn4rh4, f0) == 0 && offsetof(gm_t5v8n6qrn4rh4, f1) == 8, "layout of `Repr`");
typedef struct { gm_ts6oqppjh0ba3 f0; gm_ts6oqppjh0ba3 f1; gm_ts6oqppjh0ba3 f2; gm_ts6oqppjh0ba3 f3; } gm_t6htphat2tk14;
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; gm_tnnjeerolos0a f2; uint8_t pe[4]; } gm_t8nckeqfenl8a;
_Static_assert(sizeof(gm_taari1s0m51a7) == 2 && _Alignof(gm_taari1s0m51a7) == 1 && offsetof(gm_taari1s0m51a7, f0) == 0 && offsetof(gm_taari1s0m51a7, f1) == 1, "layout of `?UnOp`");
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
_Static_assert(sizeof(gm_tbccvikg6b96e) == 32 && _Alignof(gm_tbccvikg6b96e) == 8 && offsetof(gm_tbccvikg6b96e, f0) == 0 && offsetof(gm_tbccvikg6b96e, f1) == 8 && offsetof(gm_tbccvikg6b96e, f2) == 16, "layout of `Layout`");
typedef struct { uint8_t f0; uint8_t p1[7]; gm_trqfvmjtt4sij f1; } gm_tbci5ospk5nnn;
_Static_assert(sizeof(gm_tbngattl1fo51) == 8 && _Alignof(gm_tbngattl1fo51) == 4 && offsetof(gm_tbngattl1fo51, f0) == 0 && offsetof(gm_tbngattl1fo51, f1) == 4, "layout of `(is_const: bool, pointee: Ty)`");
_Static_assert(sizeof(gm_tcmkompbl09il) == 24 && _Alignof(gm_tcmkompbl09il) == 8 && offsetof(gm_tcmkompbl09il, f0) == 0 && offsetof(gm_tcmkompbl09il, f1) == 8, "layout of `(ty: Ty, bytes: string)`");
_Static_assert(sizeof(gm_tcmtrmhj7ak4u) == 12 && _Alignof(gm_tcmtrmhj7ak4u) == 4 && offsetof(gm_tcmtrmhj7ak4u, f0) == 0 && offsetof(gm_tcmtrmhj7ak4u, f1) == 4 && offsetof(gm_tcmtrmhj7ak4u, f2) == 8, "layout of `(op: LenOp, lhs: Ty, rhs: Ty)`");
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t p2[4]; gm_tnpifv20irnph f2; uint8_t f3; uint8_t pe[7]; } gm_tddtjfggdat0d;
_Static_assert(sizeof(gm_tfeab279c16rr) == 48 && _Alignof(gm_tfeab279c16rr) == 8 && offsetof(gm_tfeab279c16rr, f0) == 0 && offsetof(gm_tfeab279c16rr, f1) == 16 && offsetof(gm_tfeab279c16rr, f2) == 32, "layout of `Toolchain`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_tg23pq9a29gh1) == 64 && _Alignof(gm_tg23pq9a29gh1) == 8 && offsetof(gm_tg23pq9a29gh1, f0) == 0 && offsetof(gm_tg23pq9a29gh1, f1) == 16 && offsetof(gm_tg23pq9a29gh1, f2) == 32 && offsetof(gm_tg23pq9a29gh1, f3) == 48 && offsetof(gm_tg23pq9a29gh1, f4) == 56, "layout of `(string, string, string, u64, u64)`");
_Static_assert(sizeof(gm_tgglga8ro3isc) == 8 && _Alignof(gm_tgglga8ro3isc) == 4 && offsetof(gm_tgglga8ro3isc, f0) == 0 && offsetof(gm_tgglga8ro3isc, f1) == 4, "layout of `(err: Ty, ok: Ty)`");
_Static_assert(sizeof(gm_th2d9c7aju8hp) == 112 && _Alignof(gm_th2d9c7aju8hp) == 8 && offsetof(gm_th2d9c7aju8hp, f0) == 0 && offsetof(gm_th2d9c7aju8hp, f1) == 16 && offsetof(gm_th2d9c7aju8hp, f2) == 32 && offsetof(gm_th2d9c7aju8hp, f3) == 48 && offsetof(gm_th2d9c7aju8hp, f4) == 64 && offsetof(gm_th2d9c7aju8hp, f5) == 80 && offsetof(gm_th2d9c7aju8hp, f6) == 96, "layout of `(string, string, string, string, string, string, string)`");
_Static_assert(sizeof(gm_tif3elqk01bdl) == 96 && _Alignof(gm_tif3elqk01bdl) == 8 && offsetof(gm_tif3elqk01bdl, f0) == 0 && offsetof(gm_tif3elqk01bdl, f1) == 16 && offsetof(gm_tif3elqk01bdl, f2) == 32 && offsetof(gm_tif3elqk01bdl, f3) == 48 && offsetof(gm_tif3elqk01bdl, f4) == 64 && offsetof(gm_tif3elqk01bdl, f5) == 80, "layout of `(string, string, string, string, string, string)`");
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
_Static_assert(sizeof(gm_tj4tbafenpsjm) == 56 && _Alignof(gm_tj4tbafenpsjm) == 8 && offsetof(gm_tj4tbafenpsjm, f0) == 0 && offsetof(gm_tj4tbafenpsjm, f1) == 16 && offsetof(gm_tj4tbafenpsjm, f2) == 32 && offsetof(gm_tj4tbafenpsjm, f3) == 48, "layout of `(string, string, string, u32)`");
_Static_assert(sizeof(gm_tnnjeerolos0a) == 8 && _Alignof(gm_tnnjeerolos0a) == 4 && offsetof(gm_tnnjeerolos0a, f0) == 0 && offsetof(gm_tnnjeerolos0a, f1) == 4, "layout of `ArrayKind`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
_Static_assert(sizeof(gm_tommf4j9upomc) == 24 && _Alignof(gm_tommf4j9upomc) == 8 && offsetof(gm_tommf4j9upomc, f0) == 0 && offsetof(gm_tommf4j9upomc, f1) == 16, "layout of `(string, u32)`");
_Static_assert(sizeof(gm_tp6khiphboddr) == 72 && _Alignof(gm_tp6khiphboddr) == 8 && offsetof(gm_tp6khiphboddr, f0) == 0 && offsetof(gm_tp6khiphboddr, f1) == 16 && offsetof(gm_tp6khiphboddr, f2) == 24 && offsetof(gm_tp6khiphboddr, f3) == 40 && offsetof(gm_tp6khiphboddr, f4) == 56, "layout of `([]u8, u32, []u8, string, string)`");
_Static_assert(sizeof(gm_tqcc6dmlne11b) == 24 && _Alignof(gm_tqcc6dmlne11b) == 8 && offsetof(gm_tqcc6dmlne11b, f0) == 0 && offsetof(gm_tqcc6dmlne11b, f1) == 16, "layout of `?IntInfo`");
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_ts6oqppjh0ba3) == 2 && _Alignof(gm_ts6oqppjh0ba3) == 1 && offsetof(gm_ts6oqppjh0ba3, f0) == 0 && offsetof(gm_ts6oqppjh0ba3, f1) == 1, "layout of `BinOp`");
_Static_assert(sizeof(gm_tud7oovfvnf43) == 80 && _Alignof(gm_tud7oovfvnf43) == 8 && offsetof(gm_tud7oovfvnf43, f0) == 0 && offsetof(gm_tud7oovfvnf43, f1) == 16 && offsetof(gm_tud7oovfvnf43, f2) == 32 && offsetof(gm_tud7oovfvnf43, f3) == 48 && offsetof(gm_tud7oovfvnf43, f4) == 64, "layout of `(string, string, string, string, string)`");
_Static_assert(sizeof(gm_t428ue2u3hf5g) == 32 && _Alignof(gm_t428ue2u3hf5g) == 8 && offsetof(gm_t428ue2u3hf5g, f0) == 0 && offsetof(gm_t428ue2u3hf5g, f1) == 8, "layout of `ConstKey`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tjcj4j2gbh6c8 v0; uint16_t v1; gm_tbngattl1fo51 v2; gm_tbngattl1fo51 v3; gm_tbngattl1fo51 v4; gm_t8nckeqfenl8a v5; gm_t6j6smj7qoctu v6; gm_t88rcucg1b7uk v7; gm_tgglga8ro3isc v8; gm_tddtjfggdat0d v9; gm_tddtjfggdat0d v10; gm_tiilmfd16v3fc v11; gm_t88rcucg1b7uk v12; uint32_t v13; gm_tgglga8ro3isc v14; gm_t428ue2u3hf5g v15; gm_tcmtrmhj7ak4u v16; uint32_t v17; gm_tnpifv20irnph v18; uint32_t v19; } f1; } gm_t53at80pfdus3;
_Static_assert(sizeof(gm_t5o0tpv5jka2l) == 40 && _Alignof(gm_t5o0tpv5jka2l) == 8 && offsetof(gm_t5o0tpv5jka2l, f0) == 0 && offsetof(gm_t5o0tpv5jka2l, f1) == 8, "layout of `Unsup!string`");
_Static_assert(sizeof(gm_t6htphat2tk14) == 8 && _Alignof(gm_t6htphat2tk14) == 1 && offsetof(gm_t6htphat2tk14, f0) == 0 && offsetof(gm_t6htphat2tk14, f1) == 2 && offsetof(gm_t6htphat2tk14, f2) == 4 && offsetof(gm_t6htphat2tk14, f3) == 6, "layout of `(BinOp, BinOp, BinOp, BinOp)`");
_Static_assert(sizeof(gm_t8nckeqfenl8a) == 24 && _Alignof(gm_t8nckeqfenl8a) == 8 && offsetof(gm_t8nckeqfenl8a, f0) == 0 && offsetof(gm_t8nckeqfenl8a, f1) == 8 && offsetof(gm_t8nckeqfenl8a, f2) == 12, "layout of `(len: u64, elem: Ty, kind: ArrayKind)`");
_Static_assert(sizeof(gm_tbci5ospk5nnn) == 48 && _Alignof(gm_tbci5ospk5nnn) == 8 && offsetof(gm_tbci5ospk5nnn, f0) == 0 && offsetof(gm_tbci5ospk5nnn, f1) == 8, "layout of `Int`");
_Static_assert(sizeof(gm_tddtjfggdat0d) == 48 && _Alignof(gm_tddtjfggdat0d) == 8 && offsetof(gm_tddtjfggdat0d, f0) == 0 && offsetof(gm_tddtjfggdat0d, f1) == 16 && offsetof(gm_tddtjfggdat0d, f2) == 24 && offsetof(gm_tddtjfggdat0d, f3) == 40, "layout of `FnTy`");
_Static_assert(sizeof(gm_t53at80pfdus3) == 56 && _Alignof(gm_t53at80pfdus3) == 8 && offsetof(gm_t53at80pfdus3, f0) == 0 && offsetof(gm_t53at80pfdus3, f1) == 8, "layout of `TyKind`");
struct gm_fr_align_of_k6eqqrn {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_align_uu2harl {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_arith_fits_7degt3e {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_arith_flavor_e1m6u89 {
    gm_frame h;
    gm_taari1s0m51a7 ret;
};
struct gm_fr_arith_int_to_f64_tiud1hu {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* v */
    double ret;
};
struct gm_fr_arith_pointee_align_vqoa1mk {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_binary_k69ukj0 {
    gm_frame h;
    gm_t5o0tpv5jka2l ret;
};
struct gm_fr_copy_pmkg67r {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_trqfvmjtt4sij l1; /* l */
    gm_tbci5ospk5nnn ret;
};
struct gm_fr_empty_slice_d914i5u {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ijtp9lo {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_finish_int_cr0p55i {
    gm_frame h;
    gm_t5o0tpv5jka2l ret;
};
struct gm_fr_float_from_r6b8fu0 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_fresh_5kokjmg {
    gm_frame h;
    uint32_t ret;
};
struct gm_fr_ii_cty_i2b4e03 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_ii_exact_g6a19dp {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_ii_max_gtp5h90 {
    gm_frame h;
    gm_tbci5ospk5nnn ret;
};
struct gm_fr_ii_min_lku7pdi {
    gm_frame h;
    gm_tbci5ospk5nnn ret;
};
struct gm_fr_ii_ucty_b40h5ot {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_int_binary_kp3u7i8 {
    gm_frame h;
    gm_t5o0tpv5jka2l ret;
};
struct gm_fr_int_info_2tcdf1n {
    gm_frame h;
    gm_tqcc6dmlne11b ret;
};
struct gm_fr_kind_ghvav22 {
    gm_frame h;
    gm_t53at80pfdus3 ret;
};
struct gm_fr_kind_jcnt4hm {
    gm_frame h;
    gm_t53at80pfdus3 ret;
};
struct gm_fr_list_go2qg0b {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_uvfu1k1 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_loc_u9c0aqb {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_make_r4pbu77 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* m */
    gm_trqfvmjtt4sij l2; /* l */
    gm_tbci5ospk5nnn ret;
};
struct gm_fr_neg_agd5c5k {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_tbci5ospk5nnn l1; /* r */
    gm_tbci5ospk5nnn ret;
};
struct gm_fr_normalize_jch381e {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_one_08qg1op {
    gm_frame h;
    gm_tbci5ospk5nnn ret;
};
struct gm_fr_printer_h7jigd7 {
    gm_frame h;
    gm_tmm67r2c94d5j ret;
};
struct gm_fr_shl_qqecl81 {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_tbci5ospk5nnn ret;
};
struct gm_fr_show_5qbi623 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_show_kmjeh8s {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* out */
    gm_b16_8 ret;
};
struct gm_fr_size_dmhtkib {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_size_of_k0fluj2 {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_split_u128_vrmr6en {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* v */
    gm_t793vlgpa3b88 ret;
};
struct gm_fr_sub_pktm5cr {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_tbci5ospk5nnn l1; /* b */
    gm_tbci5ospk5nnn ret;
};
struct gm_fr_unary_i1ekgvj {
    gm_frame h;
    gm_t5o0tpv5jka2l ret;
};
struct gm_fr_unsupported_531q51r {
    gm_frame h;
    gm_t08olq97rscn3 ret;
};
struct gm_fr_zero_s25l990 {
    gm_frame h;
    gm_tbci5ospk5nnn ret;
};
inline bool gm_eqbqb4gb1d92_2(const uint8_t *a, const uint8_t *b);
inline bool gm_eqehhdtpeti1(const uint8_t *a, const uint8_t *b);
__attribute__((aligned(4))) gm_b16_8 gm_f_addr_of_hiaqkn0(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) uint64_t gm_f_align_of_k6eqqrn(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) uint64_t gm_f_align_uu2harl(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_arith_fits_7degt3e(void *env, gm_node *cx, gm_node *inst, gm_tuglhjjmj2g58 a0, gm_b16_8 a1, uint8_t a2);
__attribute__((aligned(4))) gm_taari1s0m51a7 gm_f_arith_flavor_e1m6u89(gm_ts6oqppjh0ba3 a0);
__attribute__((aligned(4))) double gm_f_arith_int_to_f64_tiud1hu(const void * a0);
__attribute__((aligned(4))) double gm_f_arith_int_to_f64_tiud1hu_c(gm_tbci5ospk5nnn a0);
__attribute__((aligned(4))) uint64_t gm_f_arith_pointee_align_vqoa1mk(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_binary_k69ukj0(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_ts6oqppjh0ba3 a2, void * a3, void * a4);
__attribute__((aligned(4))) gm_b16_8 gm_f_binop_symbol_9ijp0eo(gm_ts6oqppjh0ba3 a0);
__attribute__((aligned(4))) unsigned __int128 gm_f_bits128_jigkui4(const void * a0);
inline __attribute__((aligned(4))) void gm_f_copy_pmkg67r(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_cty_66ee0kf(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_d914i5u(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
__attribute__((aligned(4))) gm_b16_8 gm_f_eq_fn_5udvcnu(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) void gm_f_expr_porbc9t(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_extend_7hs67uj(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_finish_int_cr0p55i(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t9qkh0rsooc8d a1, gm_tuglhjjmj2g58 a2, gm_t88rcucg1b7uk a3, gm_t88rcucg1b7uk a4, gm_b16_8 a5, gm_b16_8 a6, gm_b16_8 a7, gm_b16_8 a8, gm_b16_8 a9);
__attribute__((aligned(4))) gm_b16_8 gm_f_float_from_r6b8fu0(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_096fu1h(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_0p6a28s(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_1ck6tsm(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_3vtl3te(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_4q9e8cc(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_5jha7ss(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_5pe4c2v(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_5r372o6(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_67eb57q(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_9221qv6(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_dlemvkb(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_e0d41cg(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_e6ar907(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t2lqu8jsj511d a0);
__attribute__((aligned(4))) void gm_f_format_eb9vbgb(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_fsfutor(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_hb3htfv(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_ht2r43m(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_jagvq3q(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
__attribute__((aligned(4))) void gm_f_format_kgo83c4(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_kr44ep8(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_las1g7o(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_loa1toi(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_mdlbfc1(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_mp5vmqe(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_norassj(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
__attribute__((aligned(4))) void gm_f_format_o8nngn3(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_oe49e3n(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_q6prg09(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_qskma1s(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_sflh0jm(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t2lqu8jsj511d a0);
__attribute__((aligned(4))) void gm_f_format_tth3l1d(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_u9gq54a(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_uhjjs86(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_v2sfrt0(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
inline __attribute__((aligned(4))) uint32_t gm_f_fresh_5kokjmg(void * a0);
__attribute__((aligned(4))) void gm_f_from_u64_ud13q5v(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint64_t a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_ii_cty_i2b4e03(gm_cs2lqoac0a5ng4 a0);
inline __attribute__((aligned(4))) uint8_t gm_f_ii_exact_g6a19dp(gm_cs2lqoac0a5ng4 a0);
__attribute__((aligned(4))) void gm_f_ii_max_gtp5h90(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tuglhjjmj2g58 a0);
__attribute__((aligned(4))) void gm_f_ii_min_lku7pdi(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tuglhjjmj2g58 a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_ii_ucty_b40h5ot(gm_cs2lqoac0a5ng4 a0);
__attribute__((aligned(4))) void gm_f_int_binary_kp3u7i8(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_ts6oqppjh0ba3 a2, gm_t88rcucg1b7uk a3, gm_b16_8 a4, gm_b16_8 a5);
__attribute__((aligned(4))) gm_b16_8 gm_f_int_cty_s62af0i(uint8_t a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_int_info_2tcdf1n(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_int_lit_ai37456(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, const void * a2);
inline __attribute__((aligned(4))) void gm_f_kind_ghvav22(void *gm_out, void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void gm_f_kind_jcnt4hm(void *gm_out, void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void gm_f_layout_a5ovnrm(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_less_fn_64um0ab(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_line_l6u87p9(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_13gk771(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_2lhkgdf(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_a8sab3a(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_bq6igl9(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_calnga5(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_dpq84co(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_fdhsfa1(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_g6msml8(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_gh0ah67(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_pamii31(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_r08knlm(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_rf3vkph(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_v6ea1vv(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
inline __attribute__((aligned(4))) void gm_f_list_go2qg0b(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_loc_u9c0aqb(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) void gm_f_mag_shl_5df643p(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, uint64_t a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_make_none_v2s8s0l(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) void gm_f_make_r4pbu77(void *gm_out, uint8_t a0, const void * a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_make_some_9aqq928(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_neg_agd5c5k(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_normalize_jch381e(void *env, gm_node *cx, gm_node *inst, gm_tuglhjjmj2g58 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) void gm_f_one_08qg1op(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_printer_h7jigd7(void *gm_out, void * a0, void * a1);
__attribute__((aligned(4))) void gm_f_repr_53quqki(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) void gm_f_shl_qqecl81(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, uint64_t a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_5qbi623(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_kmjeh8s(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_signed_add_ek0dpmo(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint8_t a0, gm_b16_8 a1, uint8_t a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) uint64_t gm_f_size_dmhtkib(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) uint64_t gm_f_size_of_k0fluj2(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) void gm_f_split_u128_vrmr6en(void *gm_out, const void * a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_src_loc_lrhtj7i(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1);
inline __attribute__((aligned(4))) void gm_f_sub_pktm5cr(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_tmp_uninit_jr39s5u(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_tmp_uohsptq(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_trim_8rdppmk(void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_ty_vka2r8h(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_unary_i1ekgvj(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t9qkh0rsooc8d a2, void * a3);
inline __attribute__((aligned(4))) void gm_f_unsupported_531q51r(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) void gm_f_zero_s25l990(void *gm_out, void *env, gm_node *cx, gm_node *inst);
extern const uint8_t gm_s02uuedsru1qu[9];
extern const uint8_t gm_s2cuflhqkkuaq[2];
extern const uint8_t gm_s2u5posfkbgnq[2];
extern const uint8_t gm_s4o70d89183c2[4];
extern const uint8_t gm_s6s96daqmhcs9[2];
extern const uint8_t gm_sajhum9b4nr96[2];
extern const uint8_t gm_si62ejtp0vbct[3];
extern const uint8_t gm_sicgmpca63jln[2];
extern const uint8_t gm_skolmnfc0ccbd[2];
extern const uint8_t gm_smerqm8g793l6[2];
extern const uint8_t gm_smpmbscbko3gk[4];
extern const uint8_t gm_snqbu1qqmr5lp[4];
extern const char gm_src0gtuh94b1fce[];
extern const char gm_src0ih77f43mhf2[];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_src4pc97o88riqc[];
extern const char gm_srcd5arorrf8jkg[];
extern const char gm_srclv3pj5es0f8o[];
extern const char gm_srcmok80ho8hh01[];
extern const char gm_srcrgc9bkquf6nr[];
extern const uint8_t gm_srs1vrn2uaams[2];
extern const uint8_t gm_st3ja0nqi37bv[2];
extern const uint8_t gm_svubtddeidu1d[2];
extern const gm_type gm_tyo86ks9qkvt;
inline bool gm_eqbqb4gb1d92_2(const uint8_t *a, const uint8_t *b) {
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
inline __attribute__((aligned(4))) uint64_t gm_f_align_of_k6eqqrn(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_align_of_k6eqqrn F;
    void * gm_l0; /* lay */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 120u);
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
    gm_tbccvikg6b96e t5 = t4;
    uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)&t5) + 8));
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
inline __attribute__((aligned(4))) uint64_t gm_f_align_uu2harl(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_align_uu2harl F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 56u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    void * t2 = (void *)(((uint8_t *)t1) + 8);
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
inline __attribute__((aligned(4))) void gm_f_copy_pmkg67r(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_copy_pmkg67r F;
    GM_STACK_CHECK(&F, 61u);
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
        gm_b16_8 t4 = t3;
        gm_b16_8 t5 = GM_LD(gm_b16_8, ((uint8_t *)&t4));
        gm_tftb1odc739t5 t6 = gm_f_extend_7hs67uj(t2, t5);
        gm_tftb1odc739t5 t7 = t6;
        if (((((uint8_t *)&t7))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t7) + 1, gm_srclv3pj5es0f8o, 330, 5);
        gm_b0_1 t8 = GM_LD(gm_b0_1, (((uint8_t *)&t7) + 1));
    }
    {
        memset((uint8_t *)&(*(gm_tbci5ospk5nnn *)gm_out) + 1, 0, 7);
        uint8_t t9 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tbci5ospk5nnn *)gm_out) + 0, t9);
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
inline __attribute__((aligned(4))) uint32_t gm_f_fresh_5kokjmg(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_fresh_5kokjmg F;
    void * gm_l0; /* fg */
    GM_STACK_CHECK_SHALLOW(&F, 77u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        void * t3 = gm_l0;
        uint32_t t4 = GM_LD(uint32_t, (((uint8_t *)t3) + 232));
        uint32_t r5; bool o6 = __builtin_add_overflow(t4, ((uint32_t)1ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_srcmok80ho8hh01, 452, 5);
        uint32_t t7 = r5;
        GM_ST(uint32_t, (((uint8_t *)t2) + 232), t7);
    }
    {
        void * t8 = gm_l0;
        void * t9 = gm_l0;
        uint32_t t10 = GM_LD(uint32_t, (((uint8_t *)t9) + 344));
        uint32_t r11; bool o12 = __builtin_add_overflow(t10, ((uint32_t)1ULL), &r11);
        if (o12) gm_trap("integer overflow in `+`", gm_srcmok80ho8hh01, 453, 5);
        uint32_t t13 = r11;
        GM_ST(uint32_t, (((uint8_t *)t8) + 344), t13);
    }
    {
        void * t14 = gm_l0;
        uint32_t t15 = GM_LD(uint32_t, (((uint8_t *)t14) + 232));
        uint8_t t16 = 0;
        F.ret = t15;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_ii_cty_i2b4e03(gm_cs2lqoac0a5ng4 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_ii_cty_i2b4e03 F;
    gm_tuglhjjmj2g58 gm_l0; /* ii */
    GM_STACK_CHECK_SHALLOW(&F, 73u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = GM_CONV(gm_tuglhjjmj2g58, a0);
    uint8_t t1 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    uint64_t t2 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
    gm_b16_8 t3 = gm_f_int_cty_s62af0i(t1, t2);
    F.ret = t3;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_ii_exact_g6a19dp(gm_cs2lqoac0a5ng4 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_ii_exact_g6a19dp F;
    gm_tuglhjjmj2g58 gm_l0; /* ii */
    GM_STACK_CHECK_SHALLOW(&F, 47u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = GM_CONV(gm_tuglhjjmj2g58, a0);
    uint32_t t1 = GM_LD(uint32_t, (((uint8_t *)&gm_l0) + 4));
    uint64_t t2 = (uint64_t)t1;
    uint64_t t3 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
    uint64_t r4; bool o5 = __builtin_mul_overflow(t3, ((uint64_t)8ULL), &r4);
    if (o5) gm_trap("integer overflow in `*`", gm_src4pc97o88riqc, 22, 56);
    uint64_t t6 = r4;
    uint8_t t7 = ((t2 == t6));
    F.ret = t7;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_ii_ucty_b40h5ot(gm_cs2lqoac0a5ng4 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_ii_ucty_b40h5ot F;
    gm_tuglhjjmj2g58 gm_l0; /* ii */
    GM_STACK_CHECK_SHALLOW(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = GM_CONV(gm_tuglhjjmj2g58, a0);
    uint64_t t1 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
    gm_b16_8 t2 = gm_f_int_cty_s62af0i(((uint8_t)0), t1);
    F.ret = t2;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_kind_ghvav22(void *gm_out, void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_kind_ghvav22 F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK_SHALLOW(&F, 40u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    void * t1 = gm_l0;
    void * t2 = GM_LD(void *, (((uint8_t *)t1) + 0));
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
    GM_STACK_CHECK_SHALLOW(&F, 104u);
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
    gm_t53at80pfdus3 t6 = GM_LD(gm_t53at80pfdus3, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t4) + 0) + 0)) + t3 * 56));
    F.ret = t6;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_loc_u9c0aqb(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_loc_u9c0aqb F;
    void * gm_l0; /* fg */
    void * gm_l1; /* e */
    GM_STACK_CHECK(&F, 100u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    void * t2 = GM_LD(void *, (((uint8_t *)t1) + 0));
    void * t3 = gm_l1;
    gm_tl874t2atnlh3 t4 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t3) + 72));
    gm_b16_8 t5 = gm_f_src_loc_lrhtj7i((void *)NULL, cx, NULL, t2, t4);
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
inline __attribute__((aligned(4))) void gm_f_shl_qqecl81(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, uint64_t a1) {
    struct gm_fr_shl_qqecl81 F;
    uint64_t gm_l1; /* n */
    GM_STACK_CHECK(&F, 105u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memcpy(&F.l0, a0, sizeof F.l0);
    gm_l1 = a1;
    uint8_t t1 = GM_LD(uint8_t, (((uint8_t *)&F.l0) + 0));
    gm_b16_8 t2 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l0) + 8) + 0));
    gm_b16_8 t3 = t2;
    gm_b16_8 t4 = GM_LD(gm_b16_8, ((uint8_t *)&t3));
    uint64_t t5 = gm_l1;
    gm_trqfvmjtt4sij t6;
    gm_f_mag_shl_5df643p((void *)&t6, (void *)NULL, cx, NULL, t4, t5);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_f_make_r4pbu77((void *)&(*(gm_tbci5ospk5nnn *)gm_out), t1, &t6);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_5qbi623(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_show_5qbi623 F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 88u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    void * t2 = GM_LD(void *, (((uint8_t *)t1) + 0));
    void * t3 = (void *)(((uint8_t *)t2) + 8);
    gm_t88rcucg1b7uk t4 = gm_l1;
    gm_b16_8 t5 = gm_f_show_kmjeh8s((void *)NULL, cx, NULL, t3, t4);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_kmjeh8s(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_show_kmjeh8s F;
    void * gm_l0; /* t */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    gm_tmm67r2c94d5j gm_l3; /* p */
    GM_STACK_CHECK(&F, 145u);
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
        gm_b16_8 t8 = t7;
        gm_b16_8 t9 = GM_LD(gm_b16_8, ((uint8_t *)&t8));
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
inline __attribute__((aligned(4))) uint64_t gm_f_size_dmhtkib(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_size_dmhtkib F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 56u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    void * t2 = (void *)(((uint8_t *)t1) + 8);
    gm_t88rcucg1b7uk t3 = gm_l1;
    uint64_t t4 = gm_f_size_of_k0fluj2((void *)NULL, cx, NULL, t2, t3);
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
inline __attribute__((aligned(4))) uint64_t gm_f_size_of_k0fluj2(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_size_of_k0fluj2 F;
    void * gm_l0; /* lay */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 120u);
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
    gm_tbccvikg6b96e t5 = t4;
    uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)&t5) + 0));
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
inline __attribute__((aligned(4))) void gm_f_split_u128_vrmr6en(void *gm_out, const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_split_u128_vrmr6en F;
    unsigned __int128 gm_l1; /* m */
    GM_STACK_CHECK_SHALLOW(&F, 193u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        unsigned __int128 t2 = gm_f_bits128_jigkui4(&(*(gm_tbci5ospk5nnn *)((uint8_t *)a0)));
        gm_l1 = t2;
    }
    {
        gm_t793vlgpa3b88 t3;
        unsigned __int128 t4 = gm_l1;
        unsigned __int128 t5 = (unsigned __int128)(t4 & ((unsigned __int128)(((unsigned __int128)0u << 64) | 18446744073709551615u)));
        GM_ST(unsigned __int128, (uint8_t *)&t3 + 0, t5);
        unsigned __int128 t6 = gm_l1;
        unsigned __int128 n7 = (unsigned __int128)((uint64_t)64ULL);
        if (n7 >= 128) gm_trap_shift((uint64_t)n7, "u128", gm_src0gtuh94b1fce, 1141, 40);
        unsigned __int128 t8 = (unsigned __int128)(t6 >> (int)n7);
        GM_ST(unsigned __int128, (uint8_t *)&t3 + 16, t8);
        gm_t793vlgpa3b88 t9 = t3;
        gm_t793vlgpa3b88 t10 = GM_LD(gm_t793vlgpa3b88, ((uint8_t *)&t9));
        uint8_t t11 = 0;
        F.ret = t10;
        goto gm_fret;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_sub_pktm5cr(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1) {
    struct gm_fr_sub_pktm5cr F;
    GM_STACK_CHECK(&F, 99u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memcpy(&F.l0, a0, sizeof F.l0);
    uint8_t t1 = GM_LD(uint8_t, (((uint8_t *)&F.l0) + 0));
    gm_b16_8 t2 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l0) + 8) + 0));
    gm_b16_8 t3 = t2;
    gm_b16_8 t4 = GM_LD(gm_b16_8, ((uint8_t *)&t3));
    uint8_t t5 = GM_LD(uint8_t, (((uint8_t *)a1) + 0));
    uint8_t t6 = (uint8_t)!t5;
    gm_b16_8 t7 = GM_LD(gm_b16_8, ((((uint8_t *)a1) + 8) + 0));
    gm_b16_8 t8 = t7;
    gm_b16_8 t9 = GM_LD(gm_b16_8, ((uint8_t *)&t8));
    gm_f_signed_add_ek0dpmo((void *)&(*(gm_tbci5ospk5nnn *)gm_out), (void *)NULL, cx, NULL, t1, t4, t6, t9);
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
inline __attribute__((aligned(4))) void gm_f_unsupported_531q51r(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0, gm_b16_8 a1) {
    struct gm_fr_unsupported_531q51r F;
    gm_tl874t2atnlh3 gm_l0; /* span */
    gm_b16_8 gm_l1; /* what */
    GM_STACK_CHECK(&F, 168u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        memset((uint8_t *)&(*(gm_t08olq97rscn3 *)gm_out) + 28, 0, 4);
        gm_b16_8 t2 = gm_l1;
        gm_tnpifv20irnph t3;
        GM_ST(gm_b16_8, ((uint8_t *)&t3 + 0), t2);
        gm_tabb710d9mq2e t4;
        gm_f_format_fsfutor((void *)&t4, (void *)NULL, cx, NULL, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t5 = t4;
        if (((((uint8_t *)&t5))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t5) + 8, gm_srcd5arorrf8jkg, 35, 27);
        gm_b16_8 t6 = GM_LD(gm_b16_8, (((uint8_t *)&t5) + 8));
        gm_b16_8 t7 = t6;
        gm_b16_8 t8 = GM_LD(gm_b16_8, ((uint8_t *)&t7));
        gm_tl874t2atnlh3 t9 = gm_l0;
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t08olq97rscn3 *)gm_out) + 0, t8);
        GM_ST(gm_tl874t2atnlh3, (uint8_t *)&(*(gm_t08olq97rscn3 *)gm_out) + 16, t9);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_arith_fits_7degt3e(void *env, gm_node *cx, gm_node *inst, gm_tuglhjjmj2g58 a0, gm_b16_8 a1, uint8_t a2) {
    struct gm_fr_arith_fits_7degt3e F;
    gm_tuglhjjmj2g58 gm_l0; /* ii */
    gm_b16_8 gm_l1; /* v */
    uint8_t gm_l2; /* signed */
    gm_t793vlgpa3b88 gm_l3; /* max */
    gm_b16_8 gm_l4; /* max_lit */
    gm_t793vlgpa3b88 gm_l5; /* min */
    gm_b16_8 gm_l6; /* min_lit */
    GM_STACK_CHECK(&F, 964u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        gm_tuglhjjmj2g58 t2 = gm_l0;
        gm_tbci5ospk5nnn t3;
        gm_f_ii_max_gtp5h90((void *)&t3, (void *)NULL, cx, NULL, t2);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_split_u128_vrmr6en((void *)&gm_l3, &t3);
    }
    {
        unsigned __int128 t4 = GM_LD(unsigned __int128, (((uint8_t *)&gm_l3) + 16));
        uint64_t t5 = (uint64_t)t4;
        unsigned __int128 t6 = GM_LD(unsigned __int128, (((uint8_t *)&gm_l3) + 0));
        uint64_t t7 = (uint64_t)t6;
        gm_t2lqu8jsj511d t8;
        GM_ST(uint64_t, ((uint8_t *)&t8 + 0), t5);
        GM_ST(uint64_t, ((uint8_t *)&t8 + 8), t7);
        gm_tabb710d9mq2e t9;
        gm_f_format_e6ar907((void *)&t9, (void *)NULL, cx, NULL, t8);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t10 = t9;
        if (((((uint8_t *)&t10))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t10) + 8, gm_src4pc97o88riqc, 248, 16);
        gm_b16_8 t11 = GM_LD(gm_b16_8, (((uint8_t *)&t10) + 8));
        gm_l4 = t11;
    }
    {
        uint8_t t13 = gm_l2;
        if (!t13) goto ie12;
        {
            gm_tuglhjjmj2g58 t15 = gm_l0;
            gm_tbci5ospk5nnn t16;
            gm_f_ii_min_lku7pdi((void *)&t16, (void *)NULL, cx, NULL, t15);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_f_split_u128_vrmr6en((void *)&gm_l5, &t16);
        }
        {
            unsigned __int128 t17 = GM_LD(unsigned __int128, (((uint8_t *)&gm_l5) + 16));
            uint64_t t18 = (uint64_t)t17;
            unsigned __int128 t19 = GM_LD(unsigned __int128, (((uint8_t *)&gm_l5) + 0));
            uint64_t t20 = (uint64_t)t19;
            gm_t2lqu8jsj511d t21;
            GM_ST(uint64_t, ((uint8_t *)&t21 + 0), t18);
            GM_ST(uint64_t, ((uint8_t *)&t21 + 8), t20);
            gm_tabb710d9mq2e t22;
            gm_f_format_sflh0jm((void *)&t22, (void *)NULL, cx, NULL, t21);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t23 = t22;
            if (((((uint8_t *)&t23))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t23) + 8, gm_src4pc97o88riqc, 251, 20);
            gm_b16_8 t24 = GM_LD(gm_b16_8, (((uint8_t *)&t23) + 8));
            gm_l6 = t24;
        }
        {
            gm_tud7oovfvnf43 t25;
            gm_b16_8 t26 = gm_l1;
            GM_ST(gm_b16_8, (uint8_t *)&t25 + 0, t26);
            gm_b16_8 t27 = gm_l6;
            GM_ST(gm_b16_8, (uint8_t *)&t25 + 16, t27);
            gm_b16_8 t28 = gm_l1;
            GM_ST(gm_b16_8, (uint8_t *)&t25 + 32, t28);
            gm_b16_8 t29 = gm_l1;
            GM_ST(gm_b16_8, (uint8_t *)&t25 + 48, t29);
            gm_b16_8 t30 = gm_l4;
            GM_ST(gm_b16_8, (uint8_t *)&t25 + 64, t30);
            gm_tabb710d9mq2e t31;
            gm_f_format_q6prg09((void *)&t31, (void *)NULL, cx, NULL, &t25);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t32 = t31;
            if (((((uint8_t *)&t32))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t32) + 8, gm_src4pc97o88riqc, 252, 16);
            gm_b16_8 t33 = GM_LD(gm_b16_8, (((uint8_t *)&t32) + 8));
            gm_b16_8 t34 = t33;
            gm_b16_8 t35 = GM_LD(gm_b16_8, ((uint8_t *)&t34));
            uint8_t t36 = 0;
            F.ret = t35;
            goto gm_fret;
        }
        goto ix12;
ie12:;
ix12:;
    }
    {
        gm_t6j6smj7qoctu t37;
        gm_b16_8 t38 = gm_l1;
        GM_ST(gm_b16_8, (uint8_t *)&t37 + 0, t38);
        gm_b16_8 t39 = gm_l4;
        GM_ST(gm_b16_8, (uint8_t *)&t37 + 16, t39);
        gm_tabb710d9mq2e t40;
        gm_f_format_67eb57q((void *)&t40, (void *)NULL, cx, NULL, &t37);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t41 = t40;
        if (((((uint8_t *)&t41))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t41) + 8, gm_src4pc97o88riqc, 254, 12);
        gm_b16_8 t42 = GM_LD(gm_b16_8, (((uint8_t *)&t41) + 8));
        gm_b16_8 t43 = t42;
        gm_b16_8 t44 = GM_LD(gm_b16_8, ((uint8_t *)&t43));
        uint8_t t45 = 0;
        F.ret = t44;
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
inline __attribute__((aligned(4))) gm_taari1s0m51a7 gm_f_arith_flavor_e1m6u89(gm_ts6oqppjh0ba3 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_arith_flavor_e1m6u89 F;
    gm_ts6oqppjh0ba3 gm_l0; /* op */
    gm_t9qkh0rsooc8d gm_l1; /* f */
    gm_t9qkh0rsooc8d gm_l2; /* f */
    gm_t9qkh0rsooc8d gm_l3; /* f */
    gm_t9qkh0rsooc8d gm_l4; /* f */
    GM_STACK_CHECK_SHALLOW(&F, 35u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_taari1s0m51a7 t1;
    uint8_t t9 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    switch (t9) { case ((uint8_t)0ULL): goto ma3; case ((uint8_t)1ULL): goto ma4; case ((uint8_t)2ULL): goto ma5; case ((uint8_t)3ULL): goto ma6; case ((uint8_t)4ULL): goto ma7; case ((uint8_t)5ULL): goto ma7; case ((uint8_t)6ULL): goto ma7; case ((uint8_t)7ULL): goto ma7; case ((uint8_t)8ULL): goto ma7; case ((uint8_t)9ULL): goto ma7; case ((uint8_t)10ULL): goto ma7; case ((uint8_t)11ULL): goto ma7; case ((uint8_t)12ULL): goto ma7; case ((uint8_t)13ULL): goto ma7; default: goto ma7; }
ma3:;
    uint8_t t11 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t11 != ((uint8_t)0ULL)) goto mn10;
    gm_t9qkh0rsooc8d t12 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l0) + 1));
    gm_l1 = t12;
    gm_t9qkh0rsooc8d t13 = gm_l1;
    gm_taari1s0m51a7 t14;
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14), t13);
    ((uint8_t *)&t14)[1] = 1;
    t1 = t14;
    goto me2;
mn10:;
ma4:;
    uint8_t t16 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t16 != ((uint8_t)1ULL)) goto mn15;
    gm_t9qkh0rsooc8d t17 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l0) + 1));
    gm_l2 = t17;
    gm_t9qkh0rsooc8d t18 = gm_l2;
    gm_taari1s0m51a7 t19;
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t19), t18);
    ((uint8_t *)&t19)[1] = 1;
    t1 = t19;
    goto me2;
mn15:;
ma5:;
    uint8_t t21 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t21 != ((uint8_t)2ULL)) goto mn20;
    gm_t9qkh0rsooc8d t22 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l0) + 1));
    gm_l3 = t22;
    gm_t9qkh0rsooc8d t23 = gm_l3;
    gm_taari1s0m51a7 t24;
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t24), t23);
    ((uint8_t *)&t24)[1] = 1;
    t1 = t24;
    goto me2;
mn20:;
ma6:;
    uint8_t t26 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t26 != ((uint8_t)3ULL)) goto mn25;
    gm_t9qkh0rsooc8d t27 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l0) + 1));
    gm_l4 = t27;
    gm_t9qkh0rsooc8d t28 = gm_l4;
    gm_taari1s0m51a7 t29;
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29), t28);
    ((uint8_t *)&t29)[1] = 1;
    t1 = t29;
    goto me2;
mn25:;
ma7:;
    gm_taari1s0m51a7 t31;
    memset(&t31, 0, sizeof t31);
    t1 = t31;
    goto me2;
mn30:;
ma8:;
    gm_trap("no match arm matched the scrutinee", gm_src4pc97o88riqc, 41, 53);
me2:;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) double gm_f_arith_int_to_f64_tiud1hu(const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_arith_int_to_f64_tiud1hu F;
    double gm_l1; /* acc */
    uint64_t gm_l2; /* i */
    GM_STACK_CHECK_SHALLOW(&F, 175u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_l1 = ((double)0e0);
    }
    {
        uint64_t t2 = GM_LD(uintptr_t, ((((uint8_t *)a0) + 8) + 0) + 8);
        gm_l2 = t2;
    }
    {
        for (;;) {
            uint64_t t4 = gm_l2;
            uint8_t t5 = (t4 > ((uint64_t)0ULL));
            if (!t5) goto lb3;
            {
                uint64_t t8 = gm_l2;
                uint64_t r9; bool o10 = __builtin_sub_overflow(t8, ((uint64_t)1ULL), &r9);
                if (o10) gm_trap("integer overflow in `-`", gm_src4pc97o88riqc, 273, 9);
                uint64_t t11 = r9;
                gm_l2 = t11;
            }
            {
                double t12 = gm_l1;
                double t13 = (t12 * ((double)4.294967296e9));
                uint64_t t14 = gm_l2;
                uint64_t t15 = (uintptr_t)t14;
                uint64_t t16 = GM_LD(uintptr_t, ((((uint8_t *)a0) + 8) + 0) + 8);
                if (t15 >= (uintptr_t)(t16)) gm_trap_index(t15, t16, gm_src4pc97o88riqc, 274, 40);
                uint32_t t17 = GM_LD(uint32_t, ((uint8_t *)GM_LD(void *, ((((uint8_t *)a0) + 8) + 0)) + t15 * 4));
                double t18 = (double)(t17);
                double t19 = (t13 + t18);
                gm_l1 = t19;
            }
lc3:;
        }
lb3:;
    }
    {
        double t20;
        uint8_t t22 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
        if (!t22) goto ie21;
        double t24 = gm_l1;
        double t25 = -t24;
        double t26 = t25;
        t20 = t26;
        goto ix21;
ie21:;
        double t28 = gm_l1;
        double t29 = t28;
        t20 = t29;
ix21:;
        uint8_t t30 = 0;
        F.ret = t20;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) double gm_f_arith_int_to_f64_tiud1hu_c(gm_tbci5ospk5nnn a0) { return gm_f_arith_int_to_f64_tiud1hu(&a0); }
inline __attribute__((aligned(4))) uint64_t gm_f_arith_pointee_align_vqoa1mk(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_arith_pointee_align_vqoa1mk F;
    void * gm_l0; /* fg */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_t88rcucg1b7uk gm_l2; /* p */
    gm_t88rcucg1b7uk gm_l3; /* p */
    gm_t88rcucg1b7uk gm_l4; /* x */
    GM_STACK_CHECK(&F, 293u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 0));
        void * t4 = (void *)(((uint8_t *)t3) + 8);
        gm_t88rcucg1b7uk t5 = gm_l1;
        gm_t53at80pfdus3 t6;
        gm_f_kind_ghvav22((void *)&t6, t4, GM_CONV(gm_csguankko4lmhd, t5));
        gm_t53at80pfdus3 t7 = t6;
        uint8_t t13 = GM_LD(uint8_t, (((uint8_t *)&t7) + 0));
        switch (t13) { case ((uint8_t)0ULL): goto ma11; case ((uint8_t)1ULL): goto ma11; case ((uint8_t)2ULL): goto ma11; case ((uint8_t)3ULL): goto ma11; case ((uint8_t)4ULL): goto ma11; case ((uint8_t)5ULL): goto ma11; case ((uint8_t)6ULL): goto ma11; case ((uint8_t)7ULL): goto ma11; case ((uint8_t)8ULL): goto ma9; case ((uint8_t)9ULL): goto ma9; case ((uint8_t)10ULL): goto ma11; case ((uint8_t)11ULL): goto ma11; case ((uint8_t)12ULL): goto ma11; case ((uint8_t)13ULL): goto ma10; case ((uint8_t)14ULL): goto ma11; case ((uint8_t)15ULL): goto ma11; case ((uint8_t)16ULL): goto ma11; case ((uint8_t)17ULL): goto ma11; case ((uint8_t)18ULL): goto ma11; case ((uint8_t)19ULL): goto ma11; case ((uint8_t)20ULL): goto ma11; case ((uint8_t)21ULL): goto ma11; case ((uint8_t)22ULL): goto ma11; case ((uint8_t)23ULL): goto ma11; case ((uint8_t)24ULL): goto ma11; case ((uint8_t)25ULL): goto ma11; case ((uint8_t)26ULL): goto ma11; default: goto ma11; }
ma9:;
        uint8_t t17 = GM_LD(uint8_t, (((uint8_t *)&t7) + 0));
        if (t17 != ((uint8_t)8ULL)) goto pa16;
        gm_t88rcucg1b7uk t18 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t7) + 8) + 4));
        gm_l2 = t18;
        goto po15;
pa16:;
        uint8_t t20 = GM_LD(uint8_t, (((uint8_t *)&t7) + 0));
        if (t20 != ((uint8_t)9ULL)) goto pa19;
        gm_t88rcucg1b7uk t21 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t7) + 8) + 4));
        gm_l2 = t21;
        goto po15;
pa19:;
        goto mn14;
po15:;
        {
            void * t24 = gm_l0;
            void * t25 = GM_LD(void *, (((uint8_t *)t24) + 0));
            void * t26 = (void *)(((uint8_t *)t25) + 8);
            gm_t88rcucg1b7uk t27 = gm_l2;
            uint64_t t28 = gm_f_size_dmhtkib((void *)NULL, cx, NULL, t26, t27);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t29 = ((t28 == ((uint64_t)0ULL)));
            if (!t29) goto ie23;
            {
                uint8_t t31 = 0;
                F.ret = ((uint64_t)1ULL);
                goto gm_fret;
            }
            goto ix23;
ie23:;
ix23:;
        }
        {
            void * t32 = gm_l0;
            void * t33 = GM_LD(void *, (((uint8_t *)t32) + 0));
            void * t34 = (void *)(((uint8_t *)t33) + 8);
            gm_t88rcucg1b7uk t35 = gm_l2;
            uint64_t t36 = gm_f_align_uu2harl((void *)NULL, cx, NULL, t34, t35);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t37 = 0;
            F.ret = t36;
            goto gm_fret;
        }
        goto me8;
mn14:;
ma10:;
        uint8_t t39 = GM_LD(uint8_t, (((uint8_t *)&t7) + 0));
        if (t39 != ((uint8_t)13ULL)) goto mn38;
        gm_t88rcucg1b7uk t40 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t7) + 8));
        gm_l4 = t40;
        void * t41 = gm_l0;
        gm_t88rcucg1b7uk t42 = gm_l4;
        uint64_t t43 = gm_f_arith_pointee_align_vqoa1mk((void *)NULL, cx, NULL, t41, t42);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t44 = 0;
        F.ret = t43;
        goto gm_fret;
        goto me8;
mn38:;
ma11:;
        uint8_t t46 = 0;
        F.ret = ((uint64_t)1ULL);
        goto gm_fret;
        goto me8;
mn45:;
ma12:;
        gm_trap("no match arm matched the scrutinee", gm_src4pc97o88riqc, 258, 5);
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
__attribute__((aligned(4))) void gm_f_binary_k69ukj0(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_ts6oqppjh0ba3 a2, void * a3, void * a4) {
    struct gm_fr_binary_k69ukj0 F;
    void * gm_l0; /* fg */
    void * gm_l1; /* e */
    gm_ts6oqppjh0ba3 gm_l2; /* op */
    void * gm_l3; /* l */
    void * gm_l4; /* r */
    gm_b16_8 gm_l5; /* a */
    gm_b16_8 gm_l6; /* b */
    gm_t88rcucg1b7uk gm_l7; /* t */
    gm_b16_8 gm_l8; /* lt */
    gm_b16_8 gm_l9; /* eq */
    gm_b16_8 gm_l10; /* pa */
    gm_b16_8 gm_l11; /* pb */
    gm_b16_8 gm_l12; /* c */
    gm_b16_8 gm_l13; /* sym */
    GM_STACK_CHECK(&F, 3818u);
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
        void * t3 = gm_l3;
        gm_t5o0tpv5jka2l t4;
        gm_f_expr_porbc9t((void *)&t4, (void *)NULL, cx, NULL, t2, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t5o0tpv5jka2l t5 = t4;
        if (((((uint8_t *)&t5))[0] != 0)) {
            gm_t08olq97rscn3 t6 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&t5) + 8));
            gm_t5o0tpv5jka2l t7;
            GM_ST(uint8_t, (uint8_t *)&t7 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t7 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t7 + 4, 0);
            ((uint8_t *)&t7)[0] = 1;
            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t7 + 8), t6);
            F.ret = t7;
            goto gm_fret;
        }
        gm_b16_8 t8 = GM_LD(gm_b16_8, (((uint8_t *)&t5) + 8));
        gm_l5 = t8;
    }
    {
        void * t9 = gm_l0;
        void * t10 = gm_l4;
        gm_t5o0tpv5jka2l t11;
        gm_f_expr_porbc9t((void *)&t11, (void *)NULL, cx, NULL, t9, t10);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t5o0tpv5jka2l t12 = t11;
        if (((((uint8_t *)&t12))[0] != 0)) {
            gm_t08olq97rscn3 t13 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&t12) + 8));
            gm_t5o0tpv5jka2l t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t14 + 8), t13);
            F.ret = t14;
            goto gm_fret;
        }
        gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&t12) + 8));
        gm_l6 = t15;
    }
    {
        void * t16 = gm_l1;
        gm_t88rcucg1b7uk t17 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t16) + 0));
        gm_l7 = t17;
    }
    {
        gm_ts6oqppjh0ba3 t19 = gm_l2;
        gm_ts6oqppjh0ba3 t20;
        GM_ST(uint8_t, (uint8_t *)&t20 + 1, 0);
        GM_ST(uint8_t, ((uint8_t *)&t20 + 0), ((uint8_t)10ULL));
        gm_ts6oqppjh0ba3 t21;
        GM_ST(uint8_t, (uint8_t *)&t21 + 1, 0);
        GM_ST(uint8_t, ((uint8_t *)&t21 + 0), ((uint8_t)11ULL));
        gm_ts6oqppjh0ba3 t22;
        GM_ST(uint8_t, (uint8_t *)&t22 + 1, 0);
        GM_ST(uint8_t, ((uint8_t *)&t22 + 0), ((uint8_t)12ULL));
        gm_ts6oqppjh0ba3 t23;
        GM_ST(uint8_t, (uint8_t *)&t23 + 1, 0);
        GM_ST(uint8_t, ((uint8_t *)&t23 + 0), ((uint8_t)13ULL));
        gm_t6htphat2tk14 t24;
        GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t24 + 0), t20);
        GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t24 + 2), t21);
        GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t24 + 4), t22);
        GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t24 + 6), t23);
        gm_t6htphat2tk14 t25 = t24;
        uint8_t t26 = 0;
        gm_ts6oqppjh0ba3 t27 = t19;
        if (gm_eqbqb4gb1d92_2(((uint8_t *)&t25) + 0, ((uint8_t *)&t27))) t26 = 1;
        if (gm_eqbqb4gb1d92_2(((uint8_t *)&t25) + 2, ((uint8_t *)&t27))) t26 = 1;
        if (gm_eqbqb4gb1d92_2(((uint8_t *)&t25) + 4, ((uint8_t *)&t27))) t26 = 1;
        if (gm_eqbqb4gb1d92_2(((uint8_t *)&t25) + 6, ((uint8_t *)&t27))) t26 = 1;
        uint8_t t28 = (uint8_t)(t26);
        if (!t28) goto ie18;
        {
            void * t30 = gm_l0;
            void * t31 = GM_LD(void *, (((uint8_t *)t30) + 0));
            void * t32 = (void *)(((uint8_t *)t31) + 8);
            void * t33 = gm_l3;
            gm_t88rcucg1b7uk t34 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t33) + 0));
            gm_t5v8n6qrn4rh4 t35;
            gm_f_repr_53quqki((void *)&t35, (void *)NULL, cx, NULL, t32, t34);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t5v8n6qrn4rh4 t36 = t35;
            uint8_t t41 = GM_LD(uint8_t, (((uint8_t *)&t36) + 0));
            if (t41 != ((uint8_t)0ULL)) goto pa40;
            goto po39;
pa40:;
            uint8_t t43 = GM_LD(uint8_t, (((uint8_t *)&t36) + 0));
            if (t43 != ((uint8_t)1ULL)) goto pa42;
            goto po39;
pa42:;
            uint8_t t45 = GM_LD(uint8_t, (((uint8_t *)&t36) + 0));
            if (t45 != ((uint8_t)2ULL)) goto pa44;
            goto po39;
pa44:;
            uint8_t t47 = GM_LD(uint8_t, (((uint8_t *)&t36) + 0));
            if (t47 != ((uint8_t)3ULL)) goto pa46;
            goto po39;
pa46:;
            goto mn38;
po39:;
            {
                void * t49 = gm_l0;
                gm_t88rcucg1b7uk t50 = gm_l7;
                gm_tfeab279c16rr t51;
                gm_b16_8 t52 = gm_l5;
                GM_ST(gm_b16_8, (uint8_t *)&t51 + 0, t52);
                gm_ts6oqppjh0ba3 t53 = gm_l2;
                gm_b16_8 t54 = gm_f_binop_symbol_9ijp0eo(t53);
                GM_ST(gm_b16_8, (uint8_t *)&t51 + 16, t54);
                gm_b16_8 t55 = gm_l6;
                GM_ST(gm_b16_8, (uint8_t *)&t51 + 32, t55);
                gm_tabb710d9mq2e t56;
                gm_f_format_mdlbfc1((void *)&t56, (void *)NULL, cx, NULL, &t51);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t57 = t56;
                if (((((uint8_t *)&t57))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t57) + 8, gm_src4pc97o88riqc, 99, 34);
                gm_b16_8 t58 = GM_LD(gm_b16_8, (((uint8_t *)&t57) + 8));
                gm_b16_8 t59 = t58;
                gm_b16_8 t60 = GM_LD(gm_b16_8, ((uint8_t *)&t59));
                gm_b16_8 t61 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t49, t50, t60);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_t5o0tpv5jka2l t62;
                GM_ST(uint8_t, (uint8_t *)&t62 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t62 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t62 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t62 + 24, 0);
                GM_ST(uint64_t, (uint8_t *)&t62 + 32, 0);
                ((uint8_t *)&t62)[0] = 0;
                GM_ST(gm_b16_8, ((uint8_t *)&t62 + 8), t61);
                gm_t5o0tpv5jka2l t63 = t62;
                uint8_t t64 = ((((uint8_t *)&t63))[0] != 0);
                F.ret = t62;
                goto gm_fret;
            }
            goto me37;
mn38:;
            {
                void * t67 = gm_l0;
                void * t68 = GM_LD(void *, (((uint8_t *)t67) + 0));
                void * t69 = (void *)(((uint8_t *)t68) + 1968);
                void * t70 = gm_l0;
                void * t71 = GM_LD(void *, (((uint8_t *)t70) + 0));
                void * t72 = (void *)(((uint8_t *)t71) + 8);
                void * t73 = gm_l3;
                gm_t88rcucg1b7uk t74 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t73) + 0));
                gm_b16_8 t75 = gm_f_less_fn_64um0ab((void *)NULL, cx, NULL, t69, t72, t74);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l8 = t75;
            }
            {
                void * t76 = gm_l0;
                void * t77 = GM_LD(void *, (((uint8_t *)t76) + 0));
                void * t78 = (void *)(((uint8_t *)t77) + 1968);
                void * t79 = gm_l0;
                void * t80 = GM_LD(void *, (((uint8_t *)t79) + 0));
                void * t81 = (void *)(((uint8_t *)t80) + 8);
                void * t82 = gm_l3;
                gm_t88rcucg1b7uk t83 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t82) + 0));
                gm_b16_8 t84 = gm_f_eq_fn_5udvcnu((void *)NULL, cx, NULL, t78, t81, t83);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l9 = t84;
            }
            {
                void * t85 = gm_l0;
                void * t86 = gm_l3;
                gm_t88rcucg1b7uk t87 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t86) + 0));
                gm_b16_8 t88 = gm_l5;
                gm_b16_8 t89 = gm_f_addr_of_hiaqkn0((void *)NULL, cx, NULL, t85, t87, t88);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l10 = t89;
            }
            {
                void * t90 = gm_l0;
                void * t91 = gm_l3;
                gm_t88rcucg1b7uk t92 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t91) + 0));
                gm_b16_8 t93 = gm_l6;
                gm_b16_8 t94 = gm_f_addr_of_hiaqkn0((void *)NULL, cx, NULL, t90, t92, t93);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l11 = t94;
            }
            {
                gm_b16_8 t95;
                uint8_t t102 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                switch (t102) { case ((uint8_t)0ULL): goto ma100; case ((uint8_t)1ULL): goto ma100; case ((uint8_t)2ULL): goto ma100; case ((uint8_t)3ULL): goto ma100; case ((uint8_t)4ULL): goto ma100; case ((uint8_t)5ULL): goto ma100; case ((uint8_t)6ULL): goto ma100; case ((uint8_t)7ULL): goto ma100; case ((uint8_t)8ULL): goto ma100; case ((uint8_t)9ULL): goto ma100; case ((uint8_t)10ULL): goto ma97; case ((uint8_t)11ULL): goto ma99; case ((uint8_t)12ULL): goto ma98; case ((uint8_t)13ULL): goto ma100; default: goto ma100; }
ma97:;
                uint8_t t104 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                if (t104 != ((uint8_t)10ULL)) goto mn103;
                gm_tfeab279c16rr t105;
                gm_b16_8 t106 = gm_l8;
                GM_ST(gm_b16_8, (uint8_t *)&t105 + 0, t106);
                gm_b16_8 t107 = gm_l10;
                GM_ST(gm_b16_8, (uint8_t *)&t105 + 16, t107);
                gm_b16_8 t108 = gm_l11;
                GM_ST(gm_b16_8, (uint8_t *)&t105 + 32, t108);
                gm_tabb710d9mq2e t109;
                gm_f_format_ht2r43m((void *)&t109, (void *)NULL, cx, NULL, &t105);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t110 = t109;
                if (((((uint8_t *)&t110))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t110) + 8, gm_src4pc97o88riqc, 107, 28);
                gm_b16_8 t111 = GM_LD(gm_b16_8, (((uint8_t *)&t110) + 8));
                gm_b16_8 t112 = t111;
                gm_b16_8 t113 = GM_LD(gm_b16_8, ((uint8_t *)&t112));
                t95 = t113;
                goto me96;
mn103:;
ma98:;
                uint8_t t115 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                if (t115 != ((uint8_t)12ULL)) goto mn114;
                gm_tfeab279c16rr t116;
                gm_b16_8 t117 = gm_l8;
                GM_ST(gm_b16_8, (uint8_t *)&t116 + 0, t117);
                gm_b16_8 t118 = gm_l11;
                GM_ST(gm_b16_8, (uint8_t *)&t116 + 16, t118);
                gm_b16_8 t119 = gm_l10;
                GM_ST(gm_b16_8, (uint8_t *)&t116 + 32, t119);
                gm_tabb710d9mq2e t120;
                gm_f_format_ht2r43m((void *)&t120, (void *)NULL, cx, NULL, &t116);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t121 = t120;
                if (((((uint8_t *)&t121))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t121) + 8, gm_src4pc97o88riqc, 108, 28);
                gm_b16_8 t122 = GM_LD(gm_b16_8, (((uint8_t *)&t121) + 8));
                gm_b16_8 t123 = t122;
                gm_b16_8 t124 = GM_LD(gm_b16_8, ((uint8_t *)&t123));
                t95 = t124;
                goto me96;
mn114:;
ma99:;
                uint8_t t126 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                if (t126 != ((uint8_t)11ULL)) goto mn125;
                gm_tif3elqk01bdl t127;
                gm_b16_8 t128 = gm_l8;
                GM_ST(gm_b16_8, (uint8_t *)&t127 + 0, t128);
                gm_b16_8 t129 = gm_l10;
                GM_ST(gm_b16_8, (uint8_t *)&t127 + 16, t129);
                gm_b16_8 t130 = gm_l11;
                GM_ST(gm_b16_8, (uint8_t *)&t127 + 32, t130);
                gm_b16_8 t131 = gm_l9;
                GM_ST(gm_b16_8, (uint8_t *)&t127 + 48, t131);
                gm_b16_8 t132 = gm_l10;
                GM_ST(gm_b16_8, (uint8_t *)&t127 + 64, t132);
                gm_b16_8 t133 = gm_l11;
                GM_ST(gm_b16_8, (uint8_t *)&t127 + 80, t133);
                gm_tabb710d9mq2e t134;
                gm_f_format_qskma1s((void *)&t134, (void *)NULL, cx, NULL, &t127);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t135 = t134;
                if (((((uint8_t *)&t135))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t135) + 8, gm_src4pc97o88riqc, 109, 28);
                gm_b16_8 t136 = GM_LD(gm_b16_8, (((uint8_t *)&t135) + 8));
                gm_b16_8 t137 = t136;
                gm_b16_8 t138 = GM_LD(gm_b16_8, ((uint8_t *)&t137));
                t95 = t138;
                goto me96;
mn125:;
ma100:;
                gm_tif3elqk01bdl t140;
                gm_b16_8 t141 = gm_l8;
                GM_ST(gm_b16_8, (uint8_t *)&t140 + 0, t141);
                gm_b16_8 t142 = gm_l11;
                GM_ST(gm_b16_8, (uint8_t *)&t140 + 16, t142);
                gm_b16_8 t143 = gm_l10;
                GM_ST(gm_b16_8, (uint8_t *)&t140 + 32, t143);
                gm_b16_8 t144 = gm_l9;
                GM_ST(gm_b16_8, (uint8_t *)&t140 + 48, t144);
                gm_b16_8 t145 = gm_l11;
                GM_ST(gm_b16_8, (uint8_t *)&t140 + 64, t145);
                gm_b16_8 t146 = gm_l10;
                GM_ST(gm_b16_8, (uint8_t *)&t140 + 80, t146);
                gm_tabb710d9mq2e t147;
                gm_f_format_qskma1s((void *)&t147, (void *)NULL, cx, NULL, &t140);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t148 = t147;
                if (((((uint8_t *)&t148))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t148) + 8, gm_src4pc97o88riqc, 110, 26);
                gm_b16_8 t149 = GM_LD(gm_b16_8, (((uint8_t *)&t148) + 8));
                gm_b16_8 t150 = t149;
                gm_b16_8 t151 = GM_LD(gm_b16_8, ((uint8_t *)&t150));
                t95 = t151;
                goto me96;
mn139:;
ma101:;
                gm_trap("no match arm matched the scrutinee", gm_src4pc97o88riqc, 106, 29);
me96:;
                gm_l12 = t95;
            }
            {
                void * t152 = gm_l0;
                gm_t88rcucg1b7uk t153 = gm_l7;
                gm_b16_8 t154 = gm_l12;
                gm_tnpifv20irnph t155;
                GM_ST(gm_b16_8, ((uint8_t *)&t155 + 0), t154);
                gm_tabb710d9mq2e t156;
                gm_f_format_096fu1h((void *)&t156, (void *)NULL, cx, NULL, t155);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t157 = t156;
                if (((((uint8_t *)&t157))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t157) + 8, gm_src4pc97o88riqc, 112, 34);
                gm_b16_8 t158 = GM_LD(gm_b16_8, (((uint8_t *)&t157) + 8));
                gm_b16_8 t159 = t158;
                gm_b16_8 t160 = GM_LD(gm_b16_8, ((uint8_t *)&t159));
                gm_b16_8 t161 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t152, t153, t160);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_t5o0tpv5jka2l t162;
                GM_ST(uint8_t, (uint8_t *)&t162 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t162 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t162 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t162 + 24, 0);
                GM_ST(uint64_t, (uint8_t *)&t162 + 32, 0);
                ((uint8_t *)&t162)[0] = 0;
                GM_ST(gm_b16_8, ((uint8_t *)&t162 + 8), t161);
                gm_t5o0tpv5jka2l t163 = t162;
                uint8_t t164 = ((((uint8_t *)&t163))[0] != 0);
                F.ret = t162;
                goto gm_fret;
            }
            goto me37;
mn65:;
            gm_trap("no match arm matched the scrutinee", gm_src4pc97o88riqc, 97, 9);
me37:;
        }
        goto ix18;
ie18:;
ix18:;
    }
    {
        void * t165 = gm_l0;
        void * t166 = GM_LD(void *, (((uint8_t *)t165) + 0));
        void * t167 = (void *)(((uint8_t *)t166) + 8);
        void * t168 = gm_l3;
        gm_t88rcucg1b7uk t169 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t168) + 0));
        gm_t5v8n6qrn4rh4 t170;
        gm_f_repr_53quqki((void *)&t170, (void *)NULL, cx, NULL, t167, t169);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t5v8n6qrn4rh4 t171 = t170;
        uint8_t t177 = GM_LD(uint8_t, (((uint8_t *)&t171) + 0));
        switch (t177) { case ((uint8_t)0ULL): goto ma174; case ((uint8_t)1ULL): goto ma173; case ((uint8_t)2ULL): goto ma175; case ((uint8_t)3ULL): goto ma175; case ((uint8_t)4ULL): goto ma175; case ((uint8_t)5ULL): goto ma175; case ((uint8_t)6ULL): goto ma175; default: goto ma175; }
ma173:;
        uint8_t t179 = GM_LD(uint8_t, (((uint8_t *)&t171) + 0));
        if (t179 != ((uint8_t)1ULL)) goto mn178;
        {
            gm_b16_8 t181;
            uint8_t t189 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            switch (t189) { case ((uint8_t)0ULL): goto ma183; case ((uint8_t)1ULL): goto ma184; case ((uint8_t)2ULL): goto ma185; case ((uint8_t)3ULL): goto ma187; case ((uint8_t)4ULL): goto ma186; case ((uint8_t)5ULL): goto ma187; case ((uint8_t)6ULL): goto ma187; case ((uint8_t)7ULL): goto ma187; case ((uint8_t)8ULL): goto ma187; case ((uint8_t)9ULL): goto ma187; case ((uint8_t)10ULL): goto ma187; case ((uint8_t)11ULL): goto ma187; case ((uint8_t)12ULL): goto ma187; case ((uint8_t)13ULL): goto ma187; default: goto ma187; }
ma183:;
            uint8_t t191 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t191 != ((uint8_t)0ULL)) goto mn190;
            gm_b16_8 t192;
            GM_ST(void *, (uint8_t *)&t192, (void *)(gm_sajhum9b4nr96));
            GM_ST(uintptr_t, (uint8_t *)&t192 + 8, (uintptr_t)(1));
            t181 = t192;
            goto me182;
mn190:;
ma184:;
            uint8_t t194 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t194 != ((uint8_t)1ULL)) goto mn193;
            gm_b16_8 t195;
            GM_ST(void *, (uint8_t *)&t195, (void *)(gm_skolmnfc0ccbd));
            GM_ST(uintptr_t, (uint8_t *)&t195 + 8, (uintptr_t)(1));
            t181 = t195;
            goto me182;
mn193:;
ma185:;
            uint8_t t197 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t197 != ((uint8_t)2ULL)) goto mn196;
            gm_b16_8 t198;
            GM_ST(void *, (uint8_t *)&t198, (void *)(gm_s6s96daqmhcs9));
            GM_ST(uintptr_t, (uint8_t *)&t198 + 8, (uintptr_t)(1));
            t181 = t198;
            goto me182;
mn196:;
ma186:;
            uint8_t t200 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t200 != ((uint8_t)4ULL)) goto mn199;
            gm_b16_8 t201;
            GM_ST(void *, (uint8_t *)&t201, (void *)(gm_st3ja0nqi37bv));
            GM_ST(uintptr_t, (uint8_t *)&t201 + 8, (uintptr_t)(1));
            t181 = t201;
            goto me182;
mn199:;
ma187:;
            void * t203 = gm_l1;
            gm_tl874t2atnlh3 t204 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t203) + 72));
            gm_ts6oqppjh0ba3 t205 = gm_l2;
            gm_b16_8 t206 = gm_f_binop_symbol_9ijp0eo(t205);
            gm_tnpifv20irnph t207;
            GM_ST(gm_b16_8, ((uint8_t *)&t207 + 0), t206);
            gm_tabb710d9mq2e t208;
            gm_f_format_5jha7ss((void *)&t208, (void *)NULL, cx, NULL, t207);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t209 = t208;
            if (((((uint8_t *)&t209))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t209) + 8, gm_src4pc97o88riqc, 123, 50);
            gm_b16_8 t210 = GM_LD(gm_b16_8, (((uint8_t *)&t209) + 8));
            gm_b16_8 t211 = t210;
            gm_b16_8 t212 = GM_LD(gm_b16_8, ((uint8_t *)&t211));
            gm_t08olq97rscn3 t213;
            gm_f_unsupported_531q51r((void *)&t213, (void *)NULL, cx, NULL, t204, t212);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t5o0tpv5jka2l t214;
            GM_ST(uint8_t, (uint8_t *)&t214 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t214 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t214 + 4, 0);
            ((uint8_t *)&t214)[0] = 1;
            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t214 + 8), t213);
            gm_t5o0tpv5jka2l t215 = t214;
            uint8_t t216 = ((((uint8_t *)&t215))[0] != 0);
            F.ret = t214;
            goto gm_fret;
            goto me182;
mn202:;
ma188:;
            gm_trap("no match arm matched the scrutinee", gm_src4pc97o88riqc, 118, 27);
me182:;
            gm_l13 = t181;
        }
        {
            void * t217 = gm_l0;
            gm_t88rcucg1b7uk t218 = gm_l7;
            gm_tfeab279c16rr t219;
            gm_b16_8 t220 = gm_l5;
            GM_ST(gm_b16_8, (uint8_t *)&t219 + 0, t220);
            gm_b16_8 t221 = gm_l13;
            GM_ST(gm_b16_8, (uint8_t *)&t219 + 16, t221);
            gm_b16_8 t222 = gm_l6;
            GM_ST(gm_b16_8, (uint8_t *)&t219 + 32, t222);
            gm_tabb710d9mq2e t223;
            gm_f_format_mdlbfc1((void *)&t223, (void *)NULL, cx, NULL, &t219);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t224 = t223;
            if (((((uint8_t *)&t224))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t224) + 8, gm_src4pc97o88riqc, 125, 30);
            gm_b16_8 t225 = GM_LD(gm_b16_8, (((uint8_t *)&t224) + 8));
            gm_b16_8 t226 = t225;
            gm_b16_8 t227 = GM_LD(gm_b16_8, ((uint8_t *)&t226));
            gm_b16_8 t228 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t217, t218, t227);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t5o0tpv5jka2l t229;
            GM_ST(uint8_t, (uint8_t *)&t229 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t229 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t229 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t229 + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&t229 + 32, 0);
            ((uint8_t *)&t229)[0] = 0;
            GM_ST(gm_b16_8, ((uint8_t *)&t229 + 8), t228);
            gm_t5o0tpv5jka2l t230 = t229;
            uint8_t t231 = ((((uint8_t *)&t230))[0] != 0);
            F.ret = t229;
            goto gm_fret;
        }
        goto me172;
mn178:;
ma174:;
        uint8_t t233 = GM_LD(uint8_t, (((uint8_t *)&t171) + 0));
        if (t233 != ((uint8_t)0ULL)) goto mn232;
        void * t234 = gm_l0;
        void * t235 = gm_l1;
        gm_ts6oqppjh0ba3 t236 = gm_l2;
        void * t237 = gm_l3;
        gm_t88rcucg1b7uk t238 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t237) + 0));
        gm_b16_8 t239 = gm_l5;
        gm_b16_8 t240 = gm_l6;
        gm_t5o0tpv5jka2l t241;
        gm_f_int_binary_kp3u7i8((void *)&t241, (void *)NULL, cx, NULL, t234, t235, t236, t238, t239, t240);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t5o0tpv5jka2l t242 = t241;
        if (((((uint8_t *)&t242))[0] != 0)) {
            gm_t08olq97rscn3 t243 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&t242) + 8));
            gm_t5o0tpv5jka2l t244;
            GM_ST(uint8_t, (uint8_t *)&t244 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t244 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t244 + 4, 0);
            ((uint8_t *)&t244)[0] = 1;
            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t244 + 8), t243);
            F.ret = t244;
            goto gm_fret;
        }
        gm_b16_8 t245 = GM_LD(gm_b16_8, (((uint8_t *)&t242) + 8));
        gm_t5o0tpv5jka2l t246;
        GM_ST(uint8_t, (uint8_t *)&t246 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t246 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t246 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t246 + 24, 0);
        GM_ST(uint64_t, (uint8_t *)&t246 + 32, 0);
        ((uint8_t *)&t246)[0] = 0;
        GM_ST(gm_b16_8, ((uint8_t *)&t246 + 8), t245);
        gm_t5o0tpv5jka2l t247 = t246;
        uint8_t t248 = ((((uint8_t *)&t247))[0] != 0);
        F.ret = t246;
        goto gm_fret;
        goto me172;
mn232:;
ma175:;
        void * t250 = gm_l1;
        gm_tl874t2atnlh3 t251 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t250) + 72));
        gm_t6j6smj7qoctu t252;
        gm_ts6oqppjh0ba3 t253 = gm_l2;
        gm_b16_8 t254 = gm_f_binop_symbol_9ijp0eo(t253);
        GM_ST(gm_b16_8, (uint8_t *)&t252 + 0, t254);
        void * t255 = gm_l0;
        void * t256 = GM_LD(void *, (((uint8_t *)t255) + 0));
        void * t257 = (void *)(((uint8_t *)t256) + 8);
        void * t258 = gm_l3;
        gm_t88rcucg1b7uk t259 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t258) + 0));
        gm_b16_8 t260 = gm_f_show_5qbi623((void *)NULL, cx, NULL, t257, t259);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_b16_8, (uint8_t *)&t252 + 16, t260);
        gm_tabb710d9mq2e t261;
        gm_f_format_kgo83c4((void *)&t261, (void *)NULL, cx, NULL, &t252);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t262 = t261;
        if (((((uint8_t *)&t262))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t262) + 8, gm_src4pc97o88riqc, 128, 42);
        gm_b16_8 t263 = GM_LD(gm_b16_8, (((uint8_t *)&t262) + 8));
        gm_b16_8 t264 = t263;
        gm_b16_8 t265 = GM_LD(gm_b16_8, ((uint8_t *)&t264));
        gm_t08olq97rscn3 t266;
        gm_f_unsupported_531q51r((void *)&t266, (void *)NULL, cx, NULL, t251, t265);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t5o0tpv5jka2l t267;
        GM_ST(uint8_t, (uint8_t *)&t267 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t267 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t267 + 4, 0);
        ((uint8_t *)&t267)[0] = 1;
        GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t267 + 8), t266);
        gm_t5o0tpv5jka2l t268 = t267;
        uint8_t t269 = ((((uint8_t *)&t268))[0] != 0);
        F.ret = t267;
        goto gm_fret;
        goto me172;
mn249:;
ma176:;
        gm_trap("no match arm matched the scrutinee", gm_src4pc97o88riqc, 116, 5);
me172:;
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
__attribute__((aligned(4))) void gm_f_finish_int_cr0p55i(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t9qkh0rsooc8d a1, gm_tuglhjjmj2g58 a2, gm_t88rcucg1b7uk a3, gm_t88rcucg1b7uk a4, gm_b16_8 a5, gm_b16_8 a6, gm_b16_8 a7, gm_b16_8 a8, gm_b16_8 a9) {
    struct gm_fr_finish_int_cr0p55i F;
    void * gm_l0; /* fg */
    gm_t9qkh0rsooc8d gm_l1; /* fl */
    gm_tuglhjjmj2g58 gm_l2; /* ii */
    gm_t88rcucg1b7uk gm_l3; /* lt */
    gm_t88rcucg1b7uk gm_l4; /* t */
    gm_b16_8 gm_l5; /* r */
    gm_b16_8 gm_l6; /* o */
    gm_b16_8 gm_l7; /* sat_up */
    gm_b16_8 gm_l8; /* loc */
    gm_b16_8 gm_l9; /* sym */
    gm_b16_8 gm_l10; /* wrapped */
    gm_b16_8 gm_l11; /* min */
    gm_b16_8 gm_l12; /* max */
    gm_b16_8 gm_l13; /* res */
    gm_b16_8 gm_l14; /* none */
    gm_b16_8 gm_l15; /* w */
    gm_b16_8 gm_l16; /* some */
    GM_STACK_CHECK(&F, 1671u);
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
    gm_l8 = a8;
    gm_l9 = a9;
    {
        gm_tuglhjjmj2g58 t2 = gm_l2;
        gm_b16_8 t3 = gm_l5;
        gm_b16_8 t4 = gm_f_normalize_jch381e((void *)NULL, cx, NULL, t2, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l10 = t4;
    }
    {
        uint8_t t11 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        switch (t11) { case ((uint8_t)0ULL): goto ma6; case ((uint8_t)1ULL): goto ma7; case ((uint8_t)2ULL): goto ma8; case ((uint8_t)3ULL): goto ma9; default: goto ma10; }
ma6:;
        uint8_t t13 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t13 != ((uint8_t)0ULL)) goto mn12;
        {
            void * t16 = gm_l0;
            void * t17 = GM_LD(void *, (((uint8_t *)t16) + 0));
            uint8_t t18 = GM_LD(uint8_t, ((((uint8_t *)t17) + 1136) + 0));
            if (!t18) goto ie15;
            {
                void * t20 = gm_l0;
                gm_tfeab279c16rr t21;
                gm_b16_8 t22 = gm_l6;
                GM_ST(gm_b16_8, (uint8_t *)&t21 + 0, t22);
                gm_b16_8 t23 = gm_l9;
                GM_ST(gm_b16_8, (uint8_t *)&t21 + 16, t23);
                gm_b16_8 t24 = gm_l8;
                GM_ST(gm_b16_8, (uint8_t *)&t21 + 32, t24);
                gm_b0_1 t25 = gm_f_linef_pamii31((void *)NULL, cx, NULL, t20, &t21);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix15;
ie15:;
ix15:;
        }
        {
            void * t26 = gm_l0;
            gm_t88rcucg1b7uk t27 = gm_l4;
            gm_b16_8 t28 = gm_l10;
            gm_b16_8 t29 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t26, t27, t28);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t5o0tpv5jka2l t30;
            GM_ST(uint8_t, (uint8_t *)&t30 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t30 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t30 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t30 + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&t30 + 32, 0);
            ((uint8_t *)&t30)[0] = 0;
            GM_ST(gm_b16_8, ((uint8_t *)&t30 + 8), t29);
            gm_t5o0tpv5jka2l t31 = t30;
            uint8_t t32 = ((((uint8_t *)&t31))[0] != 0);
            F.ret = t30;
            goto gm_fret;
        }
        goto me5;
mn12:;
ma7:;
        uint8_t t34 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t34 != ((uint8_t)1ULL)) goto mn33;
        void * t35 = gm_l0;
        gm_t88rcucg1b7uk t36 = gm_l4;
        gm_b16_8 t37 = gm_l10;
        gm_b16_8 t38 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t35, t36, t37);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t5o0tpv5jka2l t39;
        GM_ST(uint8_t, (uint8_t *)&t39 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t39 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t39 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t39 + 24, 0);
        GM_ST(uint64_t, (uint8_t *)&t39 + 32, 0);
        ((uint8_t *)&t39)[0] = 0;
        GM_ST(gm_b16_8, ((uint8_t *)&t39 + 8), t38);
        gm_t5o0tpv5jka2l t40 = t39;
        uint8_t t41 = ((((uint8_t *)&t40))[0] != 0);
        F.ret = t39;
        goto gm_fret;
        goto me5;
mn33:;
ma8:;
        uint8_t t43 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t43 != ((uint8_t)2ULL)) goto mn42;
        {
            void * t45 = gm_l0;
            gm_t88rcucg1b7uk t46 = gm_l3;
            gm_tuglhjjmj2g58 t47 = gm_l2;
            gm_tbci5ospk5nnn t48;
            gm_f_ii_min_lku7pdi((void *)&t48, (void *)NULL, cx, NULL, t47);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_b16_8 t49 = gm_f_int_lit_ai37456((void *)NULL, cx, NULL, t45, t46, &t48);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l11 = t49;
        }
        {
            void * t50 = gm_l0;
            gm_t88rcucg1b7uk t51 = gm_l3;
            gm_tuglhjjmj2g58 t52 = gm_l2;
            gm_tbci5ospk5nnn t53;
            gm_f_ii_max_gtp5h90((void *)&t53, (void *)NULL, cx, NULL, t52);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_b16_8 t54 = gm_f_int_lit_ai37456((void *)NULL, cx, NULL, t50, t51, &t53);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l12 = t54;
        }
        {
            void * t55 = gm_l0;
            gm_t88rcucg1b7uk t56 = gm_l4;
            gm_tud7oovfvnf43 t57;
            gm_b16_8 t58 = gm_l6;
            GM_ST(gm_b16_8, (uint8_t *)&t57 + 0, t58);
            gm_b16_8 t59 = gm_l7;
            GM_ST(gm_b16_8, (uint8_t *)&t57 + 16, t59);
            gm_b16_8 t60 = gm_l12;
            GM_ST(gm_b16_8, (uint8_t *)&t57 + 32, t60);
            gm_b16_8 t61 = gm_l11;
            GM_ST(gm_b16_8, (uint8_t *)&t57 + 48, t61);
            gm_b16_8 t62 = gm_l10;
            GM_ST(gm_b16_8, (uint8_t *)&t57 + 64, t62);
            gm_tabb710d9mq2e t63;
            gm_f_format_5pe4c2v((void *)&t63, (void *)NULL, cx, NULL, &t57);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t64 = t63;
            if (((((uint8_t *)&t64))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t64) + 8, gm_src4pc97o88riqc, 228, 30);
            gm_b16_8 t65 = GM_LD(gm_b16_8, (((uint8_t *)&t64) + 8));
            gm_b16_8 t66 = t65;
            gm_b16_8 t67 = GM_LD(gm_b16_8, ((uint8_t *)&t66));
            gm_b16_8 t68 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t55, t56, t67);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t5o0tpv5jka2l t69;
            GM_ST(uint8_t, (uint8_t *)&t69 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t69 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t69 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t69 + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&t69 + 32, 0);
            ((uint8_t *)&t69)[0] = 0;
            GM_ST(gm_b16_8, ((uint8_t *)&t69 + 8), t68);
            gm_t5o0tpv5jka2l t70 = t69;
            uint8_t t71 = ((((uint8_t *)&t70))[0] != 0);
            F.ret = t69;
            goto gm_fret;
        }
        goto me5;
mn42:;
ma9:;
        uint8_t t73 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t73 != ((uint8_t)3ULL)) goto mn72;
        {
            void * t75 = gm_l0;
            gm_t88rcucg1b7uk t76 = gm_l4;
            gm_b16_8 t77 = gm_f_tmp_uninit_jr39s5u((void *)NULL, cx, NULL, t75, t76);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l13 = t77;
        }
        {
            void * t78 = gm_l0;
            gm_b16_8 t79 = gm_l6;
            gm_tnpifv20irnph t80;
            GM_ST(gm_b16_8, ((uint8_t *)&t80 + 0), t79);
            gm_b0_1 t81 = gm_f_linef_g6msml8((void *)NULL, cx, NULL, t78, t80);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t82 = gm_l0;
            gm_t88rcucg1b7uk t83 = gm_l4;
            gm_b16_8 t84 = gm_f_make_none_v2s8s0l((void *)NULL, cx, NULL, t82, t83);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l14 = t84;
        }
        {
            void * t85 = gm_l0;
            gm_t6j6smj7qoctu t86;
            gm_b16_8 t87 = gm_l13;
            GM_ST(gm_b16_8, (uint8_t *)&t86 + 0, t87);
            gm_b16_8 t88 = gm_l14;
            GM_ST(gm_b16_8, (uint8_t *)&t86 + 16, t88);
            gm_b0_1 t89 = gm_f_linef_bq6igl9((void *)NULL, cx, NULL, t85, &t86);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t90 = gm_l0;
            gm_b16_8 t91;
            GM_ST(void *, (uint8_t *)&t91, (void *)(gm_s02uuedsru1qu));
            GM_ST(uintptr_t, (uint8_t *)&t91 + 8, (uintptr_t)(8));
            gm_b0_1 t92 = gm_f_line_l6u87p9((void *)NULL, cx, NULL, t90, t91);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t93 = gm_l0;
            gm_t88rcucg1b7uk t94 = gm_l3;
            gm_b16_8 t95 = gm_l10;
            gm_b16_8 t96 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t93, t94, t95);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l15 = t96;
        }
        {
            void * t97 = gm_l0;
            gm_t88rcucg1b7uk t98 = gm_l4;
            gm_b16_8 t99 = gm_l15;
            gm_b16_8 t100 = gm_f_make_some_9aqq928((void *)NULL, cx, NULL, t97, t98, t99);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l16 = t100;
        }
        {
            void * t101 = gm_l0;
            gm_t6j6smj7qoctu t102;
            gm_b16_8 t103 = gm_l13;
            GM_ST(gm_b16_8, (uint8_t *)&t102 + 0, t103);
            gm_b16_8 t104 = gm_l16;
            GM_ST(gm_b16_8, (uint8_t *)&t102 + 16, t104);
            gm_b0_1 t105 = gm_f_linef_bq6igl9((void *)NULL, cx, NULL, t101, &t102);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t106 = gm_l0;
            gm_b16_8 t107;
            GM_ST(void *, (uint8_t *)&t107, (void *)(gm_s2cuflhqkkuaq));
            GM_ST(uintptr_t, (uint8_t *)&t107 + 8, (uintptr_t)(1));
            gm_b0_1 t108 = gm_f_line_l6u87p9((void *)NULL, cx, NULL, t106, t107);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_b16_8 t109 = gm_l13;
            gm_t5o0tpv5jka2l t110;
            GM_ST(uint8_t, (uint8_t *)&t110 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t110 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t110 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t110 + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&t110 + 32, 0);
            ((uint8_t *)&t110)[0] = 0;
            GM_ST(gm_b16_8, ((uint8_t *)&t110 + 8), t109);
            gm_t5o0tpv5jka2l t111 = t110;
            uint8_t t112 = ((((uint8_t *)&t111))[0] != 0);
            F.ret = t110;
            goto gm_fret;
        }
        goto me5;
mn72:;
ma10:;
        gm_trap("no match arm matched the scrutinee", gm_src4pc97o88riqc, 217, 5);
me5:;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_float_from_r6b8fu0(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2) {
    struct gm_fr_float_from_r6b8fu0 F;
    void * gm_l0; /* fg */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_b16_8 gm_l2; /* v */
    GM_STACK_CHECK(&F, 519u);
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
        gm_t88rcucg1b7uk t6 = gm_l1;
        gm_t53at80pfdus3 t7;
        gm_f_kind_ghvav22((void *)&t7, t5, GM_CONV(gm_csguankko4lmhd, t6));
        gm_t53at80pfdus3 t8 = t7;
        uint8_t t9 = (((GM_LD(uint8_t, ((uint8_t *)&t8) + 0) == ((uint8_t)3ULL)) && (GM_LD(uint16_t, (((uint8_t *)&t8) + 8)) == ((uint16_t)16ULL))));
        if (!t9) goto ie2;
        {
            gm_b16_8 t11 = gm_l2;
            gm_tnpifv20irnph t12;
            GM_ST(gm_b16_8, ((uint8_t *)&t12 + 0), t11);
            gm_tabb710d9mq2e t13;
            gm_f_format_0p6a28s((void *)&t13, (void *)NULL, cx, NULL, t12);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t14 = t13;
            if (((((uint8_t *)&t14))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t14) + 8, gm_src4pc97o88riqc, 135, 47);
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&t14) + 8));
            gm_b16_8 t16 = t15;
            gm_b16_8 t17 = GM_LD(gm_b16_8, ((uint8_t *)&t16));
            uint8_t t18 = 0;
            F.ret = t17;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        gm_t6j6smj7qoctu t19;
        void * t20 = gm_l0;
        void * t21 = GM_LD(void *, (((uint8_t *)t20) + 0));
        void * t22 = (void *)(((uint8_t *)t21) + 8);
        gm_t88rcucg1b7uk t23 = gm_l1;
        gm_b16_8 t24 = gm_f_cty_66ee0kf((void *)NULL, cx, NULL, t22, t23);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_b16_8, (uint8_t *)&t19 + 0, t24);
        gm_b16_8 t25 = gm_l2;
        GM_ST(gm_b16_8, (uint8_t *)&t19 + 16, t25);
        gm_tabb710d9mq2e t26;
        gm_f_format_mp5vmqe((void *)&t26, (void *)NULL, cx, NULL, &t19);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t27 = t26;
        if (((((uint8_t *)&t27))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t27) + 8, gm_src4pc97o88riqc, 136, 12);
        gm_b16_8 t28 = GM_LD(gm_b16_8, (((uint8_t *)&t27) + 8));
        gm_b16_8 t29 = t28;
        gm_b16_8 t30 = GM_LD(gm_b16_8, ((uint8_t *)&t29));
        uint8_t t31 = 0;
        F.ret = t30;
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
extern __attribute__((aligned(4))) gm_b16_8 gm_f_ii_cty_i2b4e03(gm_cs2lqoac0a5ng4 a0);
extern __attribute__((aligned(4))) uint8_t gm_f_ii_exact_g6a19dp(gm_cs2lqoac0a5ng4 a0);
inline __attribute__((aligned(4))) void gm_f_ii_max_gtp5h90(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tuglhjjmj2g58 a0) {
    struct gm_fr_ii_max_gtp5h90 F;
    gm_tuglhjjmj2g58 gm_l0; /* ii */
    GM_STACK_CHECK(&F, 337u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
        if (!t3) goto ie2;
        {
            gm_tbci5ospk5nnn t5;
            gm_f_one_08qg1op((void *)&t5, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint32_t t6 = GM_LD(uint32_t, (((uint8_t *)&gm_l0) + 4));
            uint64_t t7 = (uint64_t)t6;
            uint64_t r8; bool o9 = __builtin_sub_overflow(t7, ((uint64_t)1ULL), &r8);
            if (o9) gm_trap("integer overflow in `-`", gm_src4pc97o88riqc, 36, 63);
            uint64_t t10 = r8;
            gm_tbci5ospk5nnn t11;
            gm_f_shl_qqecl81((void *)&t11, (void *)NULL, cx, NULL, &t5, t10);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tbci5ospk5nnn t12;
            gm_f_one_08qg1op((void *)&t12, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_f_sub_pktm5cr((void *)&(*(gm_tbci5ospk5nnn *)gm_out), (void *)NULL, cx, NULL, &t11, &t12);
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
        gm_tbci5ospk5nnn t13;
        gm_f_one_08qg1op((void *)&t13, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint32_t t14 = GM_LD(uint32_t, (((uint8_t *)&gm_l0) + 4));
        uint64_t t15 = (uint64_t)t14;
        gm_tbci5ospk5nnn t16;
        gm_f_shl_qqecl81((void *)&t16, (void *)NULL, cx, NULL, &t13, t15);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tbci5ospk5nnn t17;
        gm_f_one_08qg1op((void *)&t17, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_sub_pktm5cr((void *)&(*(gm_tbci5ospk5nnn *)gm_out), (void *)NULL, cx, NULL, &t16, &t17);
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
inline __attribute__((aligned(4))) void gm_f_ii_min_lku7pdi(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tuglhjjmj2g58 a0) {
    struct gm_fr_ii_min_lku7pdi F;
    gm_tuglhjjmj2g58 gm_l0; /* ii */
    GM_STACK_CHECK(&F, 133u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
        if (!t3) goto ie2;
        {
            gm_tbci5ospk5nnn t5;
            gm_f_one_08qg1op((void *)&t5, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint32_t t6 = GM_LD(uint32_t, (((uint8_t *)&gm_l0) + 4));
            uint64_t t7 = (uint64_t)t6;
            uint64_t r8; bool o9 = __builtin_sub_overflow(t7, ((uint64_t)1ULL), &r8);
            if (o9) gm_trap("integer overflow in `-`", gm_src4pc97o88riqc, 31, 63);
            uint64_t t10 = r8;
            gm_tbci5ospk5nnn t11;
            gm_f_shl_qqecl81((void *)&t11, (void *)NULL, cx, NULL, &t5, t10);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_f_neg_agd5c5k((void *)&(*(gm_tbci5ospk5nnn *)gm_out), (void *)NULL, cx, NULL, &t11);
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
        gm_f_zero_s25l990((void *)&(*(gm_tbci5ospk5nnn *)gm_out), (void *)NULL, cx, NULL);
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
extern __attribute__((aligned(4))) gm_b16_8 gm_f_ii_ucty_b40h5ot(gm_cs2lqoac0a5ng4 a0);
__attribute__((aligned(4))) void gm_f_int_binary_kp3u7i8(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_ts6oqppjh0ba3 a2, gm_t88rcucg1b7uk a3, gm_b16_8 a4, gm_b16_8 a5) {
    struct gm_fr_int_binary_kp3u7i8 F;
    void * gm_l0; /* fg */
    void * gm_l1; /* e */
    gm_ts6oqppjh0ba3 gm_l2; /* op */
    gm_t88rcucg1b7uk gm_l3; /* lt */
    gm_b16_8 gm_l4; /* a */
    gm_b16_8 gm_l5; /* b */
    gm_tuglhjjmj2g58 gm_l6; /* ii */
    gm_b16_8 gm_l7; /* c */
    gm_b16_8 gm_l8; /* loc */
    gm_t88rcucg1b7uk gm_l9; /* t */
    gm_t9qkh0rsooc8d gm_l10; /* fl */
    gm_t9qkh0rsooc8d gm_l11; /* fl */
    gm_t9qkh0rsooc8d gm_l12; /* fl */
    gm_b16_8 gm_l13; /* name */
    gm_b16_8 gm_l14; /* r */
    gm_b16_8 gm_l15; /* o */
    gm_b16_8 gm_l16; /* min */
    gm_b16_8 gm_l17; /* max */
    gm_b16_8 gm_l18; /* sat */
    gm_t9qkh0rsooc8d gm_l19; /* fl */
    gm_b16_8 gm_l20; /* n */
    gm_b16_8 gm_l21; /* r */
    gm_b16_8 gm_l22; /* raw */
    gm_b16_8 gm_l23; /* norm */
    gm_b16_8 gm_l24; /* o */
    gm_b16_8 gm_l25; /* sat */
    gm_b16_8 gm_l26; /* sym */
    gm_b16_8 gm_l27; /* min */
    gm_b16_8 gm_l28; /* r */
    gm_b16_8 gm_l29; /* s */
    GM_STACK_CHECK(&F, 5678u);
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
        gm_t88rcucg1b7uk t3 = gm_l3;
        gm_tqcc6dmlne11b t4;
        gm_f_int_info_2tcdf1n((void *)&t4, (void *)NULL, cx, NULL, t2, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tqcc6dmlne11b t5 = t4;
        if (!((((uint8_t *)&t5))[16] != 0)) gm_trap("unwrap of null", gm_src4pc97o88riqc, 140, 11);
        gm_tuglhjjmj2g58 t6 = GM_LD(gm_tuglhjjmj2g58, ((uint8_t *)&t5));
        gm_l6 = t6;
    }
    {
        gm_tuglhjjmj2g58 t7 = gm_l6;
        gm_b16_8 t8 = gm_f_ii_cty_i2b4e03(GM_CONV(gm_cs2lqoac0a5ng4, t7));
        gm_l7 = t8;
    }
    {
        void * t9 = gm_l0;
        void * t10 = gm_l1;
        gm_b16_8 t11 = gm_f_loc_u9c0aqb((void *)NULL, cx, NULL, t9, t10);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l8 = t11;
    }
    {
        void * t12 = gm_l1;
        gm_t88rcucg1b7uk t13 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t12) + 0));
        gm_l9 = t13;
    }
    {
        uint8_t t21 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        switch (t21) { case ((uint8_t)0ULL): goto ma15; case ((uint8_t)1ULL): goto ma15; case ((uint8_t)2ULL): goto ma15; case ((uint8_t)3ULL): goto ma16; case ((uint8_t)4ULL): goto ma17; case ((uint8_t)5ULL): goto ma17; case ((uint8_t)6ULL): goto ma16; case ((uint8_t)7ULL): goto ma18; case ((uint8_t)8ULL): goto ma18; case ((uint8_t)9ULL): goto ma18; case ((uint8_t)10ULL): goto ma19; case ((uint8_t)11ULL): goto ma19; case ((uint8_t)12ULL): goto ma19; case ((uint8_t)13ULL): goto ma19; default: goto ma19; }
ma15:;
        uint8_t t25 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t25 != ((uint8_t)0ULL)) goto pa24;
        gm_t9qkh0rsooc8d t26 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l2) + 1));
        gm_l10 = t26;
        goto po23;
pa24:;
        uint8_t t28 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t28 != ((uint8_t)1ULL)) goto pa27;
        gm_t9qkh0rsooc8d t29 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l2) + 1));
        gm_l10 = t29;
        goto po23;
pa27:;
        uint8_t t31 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t31 != ((uint8_t)2ULL)) goto pa30;
        gm_t9qkh0rsooc8d t32 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l2) + 1));
        gm_l10 = t32;
        goto po23;
pa30:;
        goto mn22;
po23:;
        {
            gm_b16_8 t34;
            uint8_t t40 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            switch (t40) { case ((uint8_t)0ULL): goto ma36; case ((uint8_t)1ULL): goto ma37; case ((uint8_t)2ULL): goto ma38; case ((uint8_t)3ULL): goto ma38; case ((uint8_t)4ULL): goto ma38; case ((uint8_t)5ULL): goto ma38; case ((uint8_t)6ULL): goto ma38; case ((uint8_t)7ULL): goto ma38; case ((uint8_t)8ULL): goto ma38; case ((uint8_t)9ULL): goto ma38; case ((uint8_t)10ULL): goto ma38; case ((uint8_t)11ULL): goto ma38; case ((uint8_t)12ULL): goto ma38; case ((uint8_t)13ULL): goto ma38; default: goto ma38; }
ma36:;
            uint8_t t42 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t42 != ((uint8_t)0ULL)) goto mn41;
            gm_b16_8 t43;
            GM_ST(void *, (uint8_t *)&t43, (void *)(gm_s4o70d89183c2));
            GM_ST(uintptr_t, (uint8_t *)&t43 + 8, (uintptr_t)(3));
            t34 = t43;
            goto me35;
mn41:;
ma37:;
            uint8_t t45 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t45 != ((uint8_t)1ULL)) goto mn44;
            gm_b16_8 t46;
            GM_ST(void *, (uint8_t *)&t46, (void *)(gm_snqbu1qqmr5lp));
            GM_ST(uintptr_t, (uint8_t *)&t46 + 8, (uintptr_t)(3));
            t34 = t46;
            goto me35;
mn44:;
ma38:;
            gm_b16_8 t48;
            GM_ST(void *, (uint8_t *)&t48, (void *)(gm_smpmbscbko3gk));
            GM_ST(uintptr_t, (uint8_t *)&t48 + 8, (uintptr_t)(3));
            t34 = t48;
            goto me35;
mn47:;
ma39:;
            gm_trap("no match arm matched the scrutinee", gm_src4pc97o88riqc, 146, 28);
me35:;
            gm_l13 = t34;
        }
        {
            void * t49 = gm_l0;
            uint32_t t50 = gm_f_fresh_5kokjmg(t49);
            gm_t88rcucg1b7uk t51;
            GM_ST(uint32_t, ((uint8_t *)&t51 + 0), t50);
            gm_tabb710d9mq2e t52;
            gm_f_format_jagvq3q((void *)&t52, (void *)NULL, cx, NULL, t51);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t53 = t52;
            if (((((uint8_t *)&t53))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t53) + 8, gm_src4pc97o88riqc, 151, 18);
            gm_b16_8 t54 = GM_LD(gm_b16_8, (((uint8_t *)&t53) + 8));
            gm_l14 = t54;
        }
        {
            void * t55 = gm_l0;
            uint32_t t56 = gm_f_fresh_5kokjmg(t55);
            gm_t88rcucg1b7uk t57;
            GM_ST(uint32_t, ((uint8_t *)&t57 + 0), t56);
            gm_tabb710d9mq2e t58;
            gm_f_format_v2sfrt0((void *)&t58, (void *)NULL, cx, NULL, t57);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t59 = t58;
            if (((((uint8_t *)&t59))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t59) + 8, gm_src4pc97o88riqc, 152, 18);
            gm_b16_8 t60 = GM_LD(gm_b16_8, (((uint8_t *)&t59) + 8));
            gm_l15 = t60;
        }
        {
            void * t61 = gm_l0;
            gm_th2d9c7aju8hp t62;
            gm_b16_8 t63 = gm_l7;
            GM_ST(gm_b16_8, (uint8_t *)&t62 + 0, t63);
            gm_b16_8 t64 = gm_l14;
            GM_ST(gm_b16_8, (uint8_t *)&t62 + 16, t64);
            gm_b16_8 t65 = gm_l15;
            GM_ST(gm_b16_8, (uint8_t *)&t62 + 32, t65);
            gm_b16_8 t66 = gm_l13;
            GM_ST(gm_b16_8, (uint8_t *)&t62 + 48, t66);
            gm_b16_8 t67 = gm_l4;
            GM_ST(gm_b16_8, (uint8_t *)&t62 + 64, t67);
            gm_b16_8 t68 = gm_l5;
            GM_ST(gm_b16_8, (uint8_t *)&t62 + 80, t68);
            gm_b16_8 t69 = gm_l14;
            GM_ST(gm_b16_8, (uint8_t *)&t62 + 96, t69);
            gm_b0_1 t70 = gm_f_linef_r08knlm((void *)NULL, cx, NULL, t61, &t62);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_tuglhjjmj2g58 t72 = gm_l6;
            uint8_t t73 = gm_f_ii_exact_g6a19dp(GM_CONV(gm_cs2lqoac0a5ng4, t72));
            uint8_t t74 = (uint8_t)!t73;
            if (!t74) goto ie71;
            {
                void * t76 = gm_l0;
                gm_t88rcucg1b7uk t77 = gm_l3;
                gm_tuglhjjmj2g58 t78 = gm_l6;
                gm_tbci5ospk5nnn t79;
                gm_f_ii_min_lku7pdi((void *)&t79, (void *)NULL, cx, NULL, t78);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_b16_8 t80 = gm_f_int_lit_ai37456((void *)NULL, cx, NULL, t76, t77, &t79);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l16 = t80;
            }
            {
                void * t81 = gm_l0;
                gm_t88rcucg1b7uk t82 = gm_l3;
                gm_tuglhjjmj2g58 t83 = gm_l6;
                gm_tbci5ospk5nnn t84;
                gm_f_ii_max_gtp5h90((void *)&t84, (void *)NULL, cx, NULL, t83);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_b16_8 t85 = gm_f_int_lit_ai37456((void *)NULL, cx, NULL, t81, t82, &t84);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l17 = t85;
            }
            {
                void * t86 = gm_l0;
                gm_tif3elqk01bdl t87;
                gm_b16_8 t88 = gm_l15;
                GM_ST(gm_b16_8, (uint8_t *)&t87 + 0, t88);
                gm_b16_8 t89 = gm_l15;
                GM_ST(gm_b16_8, (uint8_t *)&t87 + 16, t89);
                gm_b16_8 t90 = gm_l14;
                GM_ST(gm_b16_8, (uint8_t *)&t87 + 32, t90);
                gm_b16_8 t91 = gm_l16;
                GM_ST(gm_b16_8, (uint8_t *)&t87 + 48, t91);
                gm_b16_8 t92 = gm_l14;
                GM_ST(gm_b16_8, (uint8_t *)&t87 + 64, t92);
                gm_b16_8 t93 = gm_l17;
                GM_ST(gm_b16_8, (uint8_t *)&t87 + 80, t93);
                gm_b0_1 t94 = gm_f_linef_calnga5((void *)NULL, cx, NULL, t86, &t87);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix71;
ie71:;
ix71:;
        }
        {
            gm_b16_8 t95;
            GM_ST(void *, (uint8_t *)&t95, (void *)(gm_svubtddeidu1d));
            GM_ST(uintptr_t, (uint8_t *)&t95 + 8, (uintptr_t)(1));
            gm_l18 = t95;
        }
        {
            uint8_t t97 = GM_LD(uint8_t, (((uint8_t *)&gm_l6) + 0));
            if (!t97) goto ie96;
            {
                uint8_t t104 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                switch (t104) { case ((uint8_t)0ULL): goto ma100; case ((uint8_t)1ULL): goto ma101; case ((uint8_t)2ULL): goto ma102; case ((uint8_t)3ULL): goto ma102; case ((uint8_t)4ULL): goto ma102; case ((uint8_t)5ULL): goto ma102; case ((uint8_t)6ULL): goto ma102; case ((uint8_t)7ULL): goto ma102; case ((uint8_t)8ULL): goto ma102; case ((uint8_t)9ULL): goto ma102; case ((uint8_t)10ULL): goto ma102; case ((uint8_t)11ULL): goto ma102; case ((uint8_t)12ULL): goto ma102; case ((uint8_t)13ULL): goto ma102; default: goto ma102; }
ma100:;
                uint8_t t106 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                if (t106 != ((uint8_t)0ULL)) goto mn105;
                {
                    gm_b16_8 t108 = gm_l5;
                    gm_tnpifv20irnph t109;
                    GM_ST(gm_b16_8, ((uint8_t *)&t109 + 0), t108);
                    gm_tabb710d9mq2e t110;
                    gm_f_format_o8nngn3((void *)&t110, (void *)NULL, cx, NULL, t109);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tabb710d9mq2e t111 = t110;
                    if (((((uint8_t *)&t111))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t111) + 8, gm_src4pc97o88riqc, 162, 40);
                    gm_b16_8 t112 = GM_LD(gm_b16_8, (((uint8_t *)&t111) + 8));
                    gm_b16_8 t113 = t112;
                    gm_b16_8 t114 = GM_LD(gm_b16_8, ((uint8_t *)&t113));
                    gm_l18 = t114;
                }
                goto me99;
mn105:;
ma101:;
                uint8_t t116 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                if (t116 != ((uint8_t)1ULL)) goto mn115;
                {
                    gm_b16_8 t118 = gm_l5;
                    gm_tnpifv20irnph t119;
                    GM_ST(gm_b16_8, ((uint8_t *)&t119 + 0), t118);
                    gm_tabb710d9mq2e t120;
                    gm_f_format_uhjjs86((void *)&t120, (void *)NULL, cx, NULL, t119);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tabb710d9mq2e t121 = t120;
                    if (((((uint8_t *)&t121))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t121) + 8, gm_src4pc97o88riqc, 163, 40);
                    gm_b16_8 t122 = GM_LD(gm_b16_8, (((uint8_t *)&t121) + 8));
                    gm_b16_8 t123 = t122;
                    gm_b16_8 t124 = GM_LD(gm_b16_8, ((uint8_t *)&t123));
                    gm_l18 = t124;
                }
                goto me99;
mn115:;
ma102:;
                {
                    gm_t6j6smj7qoctu t127;
                    gm_b16_8 t128 = gm_l4;
                    GM_ST(gm_b16_8, (uint8_t *)&t127 + 0, t128);
                    gm_b16_8 t129 = gm_l5;
                    GM_ST(gm_b16_8, (uint8_t *)&t127 + 16, t129);
                    gm_tabb710d9mq2e t130;
                    gm_f_format_kr44ep8((void *)&t130, (void *)NULL, cx, NULL, &t127);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tabb710d9mq2e t131 = t130;
                    if (((((uint8_t *)&t131))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t131) + 8, gm_src4pc97o88riqc, 164, 34);
                    gm_b16_8 t132 = GM_LD(gm_b16_8, (((uint8_t *)&t131) + 8));
                    gm_b16_8 t133 = t132;
                    gm_b16_8 t134 = GM_LD(gm_b16_8, ((uint8_t *)&t133));
                    gm_l18 = t134;
                }
                goto me99;
mn125:;
ma103:;
                gm_trap("no match arm matched the scrutinee", gm_src4pc97o88riqc, 161, 17);
me99:;
            }
            goto ix96;
ie96:;
            uint8_t t136;
            uint8_t t139 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t139 != ((uint8_t)1ULL)) goto mn138;
            t136 = ((uint8_t)1);
            goto me137;
mn138:;
            t136 = ((uint8_t)0);
            goto me137;
mn140:;
            gm_trap("no match arm matched the scrutinee", gm_src4pc97o88riqc, 166, 23);
me137:;
            if (!t136) goto ie135;
            {
                gm_b16_8 t142;
                GM_ST(void *, (uint8_t *)&t142, (void *)(gm_sicgmpca63jln));
                GM_ST(uintptr_t, (uint8_t *)&t142 + 8, (uintptr_t)(1));
                gm_l18 = t142;
            }
            goto ix135;
ie135:;
ix135:;
ix96:;
        }
        {
            void * t143 = gm_l0;
            gm_t9qkh0rsooc8d t144 = gm_l10;
            gm_tuglhjjmj2g58 t145 = gm_l6;
            gm_t88rcucg1b7uk t146 = gm_l3;
            gm_t88rcucg1b7uk t147 = gm_l9;
            gm_b16_8 t148 = gm_l14;
            gm_b16_8 t149 = t148;
            gm_b16_8 t150 = GM_LD(gm_b16_8, ((uint8_t *)&t149));
            gm_b16_8 t151 = gm_l15;
            gm_b16_8 t152 = t151;
            gm_b16_8 t153 = GM_LD(gm_b16_8, ((uint8_t *)&t152));
            gm_b16_8 t154 = gm_l18;
            gm_b16_8 t155 = gm_l8;
            gm_ts6oqppjh0ba3 t156 = gm_l2;
            gm_b16_8 t157 = gm_f_binop_symbol_9ijp0eo(t156);
            gm_t5o0tpv5jka2l t158;
            gm_f_finish_int_cr0p55i((void *)&t158, (void *)NULL, cx, NULL, t143, t144, t145, t146, t147, t150, t153, t154, t155, t157);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t5o0tpv5jka2l t159 = t158;
            if (((((uint8_t *)&t159))[0] != 0)) {
                gm_t08olq97rscn3 t160 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&t159) + 8));
                gm_t5o0tpv5jka2l t161;
                GM_ST(uint8_t, (uint8_t *)&t161 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t161 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t161 + 4, 0);
                ((uint8_t *)&t161)[0] = 1;
                GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t161 + 8), t160);
                F.ret = t161;
                goto gm_fret;
            }
            gm_b16_8 t162 = GM_LD(gm_b16_8, (((uint8_t *)&t159) + 8));
            gm_t5o0tpv5jka2l t163;
            GM_ST(uint8_t, (uint8_t *)&t163 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t163 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t163 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t163 + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&t163 + 32, 0);
            ((uint8_t *)&t163)[0] = 0;
            GM_ST(gm_b16_8, ((uint8_t *)&t163 + 8), t162);
            gm_t5o0tpv5jka2l t164 = t163;
            uint8_t t165 = ((((uint8_t *)&t164))[0] != 0);
            F.ret = t163;
            goto gm_fret;
        }
        goto me14;
mn22:;
ma16:;
        uint8_t t169 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t169 != ((uint8_t)3ULL)) goto pa168;
        goto po167;
pa168:;
        uint8_t t171 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t171 != ((uint8_t)6ULL)) goto pa170;
        goto po167;
pa170:;
        goto mn166;
po167:;
        {
            gm_ts6oqppjh0ba3 t173 = gm_l2;
            gm_taari1s0m51a7 t174 = gm_f_arith_flavor_e1m6u89(t173);
            gm_taari1s0m51a7 t175 = t174;
            gm_t9qkh0rsooc8d t176;
            if (((((uint8_t *)&t175))[1] != 0)) {
                gm_t9qkh0rsooc8d t177 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&t175) + 0));
                t176 = t177;
            } else {
                gm_t9qkh0rsooc8d t178;
                GM_ST(uint8_t, ((uint8_t *)&t178 + 0), ((uint8_t)0ULL));
                t176 = t178;
            }
            gm_l19 = t176;
        }
        {
            void * t179 = gm_l0;
            uint32_t t180 = gm_f_fresh_5kokjmg(t179);
            gm_t88rcucg1b7uk t181;
            GM_ST(uint32_t, ((uint8_t *)&t181 + 0), t180);
            gm_tabb710d9mq2e t182;
            gm_f_format_norassj((void *)&t182, (void *)NULL, cx, NULL, t181);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t183 = t182;
            if (((((uint8_t *)&t183))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t183) + 8, gm_src4pc97o88riqc, 173, 18);
            gm_b16_8 t184 = GM_LD(gm_b16_8, (((uint8_t *)&t183) + 8));
            gm_l20 = t184;
        }
        {
            void * t185 = gm_l0;
            gm_t6j6smj7qoctu t186;
            gm_b16_8 t187 = gm_l20;
            GM_ST(gm_b16_8, (uint8_t *)&t186 + 0, t187);
            gm_b16_8 t188 = gm_l5;
            GM_ST(gm_b16_8, (uint8_t *)&t186 + 16, t188);
            gm_b0_1 t189 = gm_f_linef_13gk771((void *)NULL, cx, NULL, t185, &t186);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t191 = gm_l0;
            void * t192 = GM_LD(void *, (((uint8_t *)t191) + 0));
            uint8_t t193 = GM_LD(uint8_t, ((((uint8_t *)t192) + 1136) + 0));
            if (!t193) goto ie190;
            {
                void * t195 = gm_l0;
                gm_tp6khiphboddr t196;
                memset((uint8_t *)&t196 + 20, 0, 4);
                gm_b16_8 t197 = gm_l20;
                GM_ST(gm_b16_8, (uint8_t *)&t196 + 0, t197);
                uint32_t t198 = GM_LD(uint32_t, (((uint8_t *)&gm_l6) + 4));
                GM_ST(uint32_t, (uint8_t *)&t196 + 16, t198);
                gm_b16_8 t199 = gm_l20;
                GM_ST(gm_b16_8, (uint8_t *)&t196 + 24, t199);
                void * t200 = gm_l0;
                void * t201 = GM_LD(void *, (((uint8_t *)t200) + 0));
                void * t202 = (void *)(((uint8_t *)t201) + 8);
                gm_t88rcucg1b7uk t203 = gm_l3;
                gm_b16_8 t204 = gm_f_show_5qbi623((void *)NULL, cx, NULL, t202, t203);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(gm_b16_8, (uint8_t *)&t196 + 40, t204);
                gm_b16_8 t205 = gm_l8;
                GM_ST(gm_b16_8, (uint8_t *)&t196 + 56, t205);
                gm_b0_1 t206 = gm_f_linef_rf3vkph((void *)NULL, cx, NULL, t195, &t196);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix190;
ie190:;
            {
                void * t208 = gm_l0;
                gm_tommf4j9upomc t209;
                memset((uint8_t *)&t209 + 20, 0, 4);
                gm_b16_8 t210 = gm_l20;
                GM_ST(gm_b16_8, (uint8_t *)&t209 + 0, t210);
                uint32_t t211 = GM_LD(uint32_t, (((uint8_t *)&gm_l6) + 4));
                GM_ST(uint32_t, (uint8_t *)&t209 + 16, t211);
                gm_b0_1 t212 = gm_f_linef_2lhkgdf((void *)NULL, cx, NULL, t208, &t209);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
ix190:;
        }
        {
            uint8_t t214 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l2) + 0) == ((uint8_t)6ULL)));
            if (!t214) goto ie213;
            {
                void * t216 = gm_l0;
                gm_t88rcucg1b7uk t217 = gm_l9;
                gm_tfeab279c16rr t218;
                gm_b16_8 t219 = gm_l7;
                GM_ST(gm_b16_8, (uint8_t *)&t218 + 0, t219);
                gm_b16_8 t220 = gm_l4;
                GM_ST(gm_b16_8, (uint8_t *)&t218 + 16, t220);
                gm_b16_8 t221 = gm_l20;
                GM_ST(gm_b16_8, (uint8_t *)&t218 + 32, t221);
                gm_tabb710d9mq2e t222;
                gm_f_format_hb3htfv((void *)&t222, (void *)NULL, cx, NULL, &t218);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t223 = t222;
                if (((((uint8_t *)&t223))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t223) + 8, gm_src4pc97o88riqc, 180, 46);
                gm_b16_8 t224 = GM_LD(gm_b16_8, (((uint8_t *)&t223) + 8));
                gm_b16_8 t225 = t224;
                gm_b16_8 t226 = GM_LD(gm_b16_8, ((uint8_t *)&t225));
                gm_b16_8 t227 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t216, t217, t226);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_t5o0tpv5jka2l t228;
                GM_ST(uint8_t, (uint8_t *)&t228 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t228 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t228 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t228 + 24, 0);
                GM_ST(uint64_t, (uint8_t *)&t228 + 32, 0);
                ((uint8_t *)&t228)[0] = 0;
                GM_ST(gm_b16_8, ((uint8_t *)&t228 + 8), t227);
                gm_t5o0tpv5jka2l t229 = t228;
                uint8_t t230 = ((((uint8_t *)&t229))[0] != 0);
                F.ret = t228;
                goto gm_fret;
            }
            goto ix213;
ie213:;
ix213:;
        }
        {
            void * t231 = gm_l0;
            uint32_t t232 = gm_f_fresh_5kokjmg(t231);
            gm_t88rcucg1b7uk t233;
            GM_ST(uint32_t, ((uint8_t *)&t233 + 0), t232);
            gm_tabb710d9mq2e t234;
            gm_f_format_jagvq3q((void *)&t234, (void *)NULL, cx, NULL, t233);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t235 = t234;
            if (((((uint8_t *)&t235))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t235) + 8, gm_src4pc97o88riqc, 181, 18);
            gm_b16_8 t236 = GM_LD(gm_b16_8, (((uint8_t *)&t235) + 8));
            gm_l21 = t236;
        }
        {
            gm_t0uuhaljsf410 t237;
            gm_b16_8 t238 = gm_l7;
            GM_ST(gm_b16_8, (uint8_t *)&t237 + 0, t238);
            gm_tuglhjjmj2g58 t239 = gm_l6;
            gm_b16_8 t240 = gm_f_ii_ucty_b40h5ot(GM_CONV(gm_cs2lqoac0a5ng4, t239));
            GM_ST(gm_b16_8, (uint8_t *)&t237 + 16, t240);
            gm_b16_8 t241 = gm_l4;
            GM_ST(gm_b16_8, (uint8_t *)&t237 + 32, t241);
            gm_b16_8 t242 = gm_l20;
            GM_ST(gm_b16_8, (uint8_t *)&t237 + 48, t242);
            gm_tabb710d9mq2e t243;
            gm_f_format_eb9vbgb((void *)&t243, (void *)NULL, cx, NULL, &t237);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t244 = t243;
            if (((((uint8_t *)&t244))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t244) + 8, gm_src4pc97o88riqc, 182, 20);
            gm_b16_8 t245 = GM_LD(gm_b16_8, (((uint8_t *)&t244) + 8));
            gm_l22 = t245;
        }
        {
            gm_tuglhjjmj2g58 t246 = gm_l6;
            gm_b16_8 t247 = gm_l22;
            gm_b16_8 t248 = t247;
            gm_b16_8 t249 = GM_LD(gm_b16_8, ((uint8_t *)&t248));
            gm_b16_8 t250 = gm_f_normalize_jch381e((void *)NULL, cx, NULL, t246, t249);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l23 = t250;
        }
        {
            void * t251 = gm_l0;
            gm_tfeab279c16rr t252;
            gm_b16_8 t253 = gm_l7;
            GM_ST(gm_b16_8, (uint8_t *)&t252 + 0, t253);
            gm_b16_8 t254 = gm_l21;
            GM_ST(gm_b16_8, (uint8_t *)&t252 + 16, t254);
            gm_b16_8 t255 = gm_l23;
            GM_ST(gm_b16_8, (uint8_t *)&t252 + 32, t255);
            gm_b0_1 t256 = gm_f_linef_dpq84co((void *)NULL, cx, NULL, t251, &t252);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t257 = gm_l0;
            uint32_t t258 = gm_f_fresh_5kokjmg(t257);
            gm_t88rcucg1b7uk t259;
            GM_ST(uint32_t, ((uint8_t *)&t259 + 0), t258);
            gm_tabb710d9mq2e t260;
            gm_f_format_v2sfrt0((void *)&t260, (void *)NULL, cx, NULL, t259);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t261 = t260;
            if (((((uint8_t *)&t261))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t261) + 8, gm_src4pc97o88riqc, 185, 18);
            gm_b16_8 t262 = GM_LD(gm_b16_8, (((uint8_t *)&t261) + 8));
            gm_l24 = t262;
        }
        {
            void * t263 = gm_l0;
            gm_tud7oovfvnf43 t264;
            gm_b16_8 t265 = gm_l24;
            GM_ST(gm_b16_8, (uint8_t *)&t264 + 0, t265);
            gm_b16_8 t266 = gm_l7;
            GM_ST(gm_b16_8, (uint8_t *)&t264 + 16, t266);
            gm_b16_8 t267 = gm_l21;
            GM_ST(gm_b16_8, (uint8_t *)&t264 + 32, t267);
            gm_b16_8 t268 = gm_l20;
            GM_ST(gm_b16_8, (uint8_t *)&t264 + 48, t268);
            gm_b16_8 t269 = gm_l4;
            GM_ST(gm_b16_8, (uint8_t *)&t264 + 64, t269);
            gm_b0_1 t270 = gm_f_linef_gh0ah67((void *)NULL, cx, NULL, t263, &t264);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_b16_8 t271 = gm_l4;
            gm_tnpifv20irnph t272;
            GM_ST(gm_b16_8, ((uint8_t *)&t272 + 0), t271);
            gm_tabb710d9mq2e t273;
            gm_f_format_o8nngn3((void *)&t273, (void *)NULL, cx, NULL, t272);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t274 = t273;
            if (((((uint8_t *)&t274))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t274) + 8, gm_src4pc97o88riqc, 187, 20);
            gm_b16_8 t275 = GM_LD(gm_b16_8, (((uint8_t *)&t274) + 8));
            gm_l25 = t275;
        }
        {
            void * t276 = gm_l0;
            gm_t9qkh0rsooc8d t277 = gm_l19;
            gm_tuglhjjmj2g58 t278 = gm_l6;
            gm_t88rcucg1b7uk t279 = gm_l3;
            gm_t88rcucg1b7uk t280 = gm_l9;
            gm_b16_8 t281 = gm_l21;
            gm_b16_8 t282 = t281;
            gm_b16_8 t283 = GM_LD(gm_b16_8, ((uint8_t *)&t282));
            gm_b16_8 t284 = gm_l24;
            gm_b16_8 t285 = t284;
            gm_b16_8 t286 = GM_LD(gm_b16_8, ((uint8_t *)&t285));
            gm_b16_8 t287 = gm_l25;
            gm_b16_8 t288 = t287;
            gm_b16_8 t289 = GM_LD(gm_b16_8, ((uint8_t *)&t288));
            gm_b16_8 t290 = gm_l8;
            gm_b16_8 t291;
            GM_ST(void *, (uint8_t *)&t291, (void *)(gm_si62ejtp0vbct));
            GM_ST(uintptr_t, (uint8_t *)&t291 + 8, (uintptr_t)(2));
            gm_t5o0tpv5jka2l t292;
            gm_f_finish_int_cr0p55i((void *)&t292, (void *)NULL, cx, NULL, t276, t277, t278, t279, t280, t283, t286, t289, t290, t291);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t5o0tpv5jka2l t293 = t292;
            if (((((uint8_t *)&t293))[0] != 0)) {
                gm_t08olq97rscn3 t294 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&t293) + 8));
                gm_t5o0tpv5jka2l t295;
                GM_ST(uint8_t, (uint8_t *)&t295 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t295 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t295 + 4, 0);
                ((uint8_t *)&t295)[0] = 1;
                GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t295 + 8), t294);
                F.ret = t295;
                goto gm_fret;
            }
            gm_b16_8 t296 = GM_LD(gm_b16_8, (((uint8_t *)&t293) + 8));
            gm_t5o0tpv5jka2l t297;
            GM_ST(uint8_t, (uint8_t *)&t297 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t297 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t297 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t297 + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&t297 + 32, 0);
            ((uint8_t *)&t297)[0] = 0;
            GM_ST(gm_b16_8, ((uint8_t *)&t297 + 8), t296);
            gm_t5o0tpv5jka2l t298 = t297;
            uint8_t t299 = ((((uint8_t *)&t298))[0] != 0);
            F.ret = t297;
            goto gm_fret;
        }
        goto me14;
mn166:;
ma17:;
        uint8_t t303 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t303 != ((uint8_t)4ULL)) goto pa302;
        goto po301;
pa302:;
        uint8_t t305 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t305 != ((uint8_t)5ULL)) goto pa304;
        goto po301;
pa304:;
        goto mn300;
po301:;
        {
            gm_ts6oqppjh0ba3 t307 = gm_l2;
            gm_b16_8 t308 = gm_f_binop_symbol_9ijp0eo(t307);
            gm_l26 = t308;
        }
        {
            void * t309 = gm_l0;
            gm_t6j6smj7qoctu t310;
            gm_b16_8 t311 = gm_l5;
            GM_ST(gm_b16_8, (uint8_t *)&t310 + 0, t311);
            gm_b16_8 t312 = gm_l8;
            GM_ST(gm_b16_8, (uint8_t *)&t310 + 16, t312);
            gm_b0_1 t313 = gm_f_linef_a8sab3a((void *)NULL, cx, NULL, t309, &t310);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            uint8_t t315 = GM_LD(uint8_t, (((uint8_t *)&gm_l6) + 0));
            if (!t315) goto ie314;
            {
                void * t317 = gm_l0;
                gm_t88rcucg1b7uk t318 = gm_l3;
                gm_tuglhjjmj2g58 t319 = gm_l6;
                gm_tbci5ospk5nnn t320;
                gm_f_ii_min_lku7pdi((void *)&t320, (void *)NULL, cx, NULL, t319);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_b16_8 t321 = gm_f_int_lit_ai37456((void *)NULL, cx, NULL, t317, t318, &t320);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l27 = t321;
            }
            {
                void * t322 = gm_l0;
                gm_tif3elqk01bdl t323;
                gm_b16_8 t324 = gm_l4;
                GM_ST(gm_b16_8, (uint8_t *)&t323 + 0, t324);
                gm_b16_8 t325 = gm_l27;
                GM_ST(gm_b16_8, (uint8_t *)&t323 + 16, t325);
                gm_b16_8 t326 = gm_l5;
                GM_ST(gm_b16_8, (uint8_t *)&t323 + 32, t326);
                gm_b16_8 t327 = gm_l7;
                GM_ST(gm_b16_8, (uint8_t *)&t323 + 48, t327);
                gm_b16_8 t328 = gm_l26;
                GM_ST(gm_b16_8, (uint8_t *)&t323 + 64, t328);
                gm_b16_8 t329 = gm_l8;
                GM_ST(gm_b16_8, (uint8_t *)&t323 + 80, t329);
                gm_b0_1 t330 = gm_f_linef_v6ea1vv((void *)NULL, cx, NULL, t322, &t323);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix314;
ie314:;
ix314:;
        }
        {
            gm_b16_8 t331;
            uint8_t t333 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l2) + 0) == ((uint8_t)4ULL)));
            if (!t333) goto ie332;
            gm_t6j6smj7qoctu t335;
            gm_b16_8 t336 = gm_l4;
            GM_ST(gm_b16_8, (uint8_t *)&t335 + 0, t336);
            gm_b16_8 t337 = gm_l5;
            GM_ST(gm_b16_8, (uint8_t *)&t335 + 16, t337);
            gm_tabb710d9mq2e t338;
            gm_f_format_e0d41cg((void *)&t338, (void *)NULL, cx, NULL, &t335);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t339 = t338;
            if (((((uint8_t *)&t339))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t339) + 8, gm_src4pc97o88riqc, 197, 41);
            gm_b16_8 t340 = GM_LD(gm_b16_8, (((uint8_t *)&t339) + 8));
            gm_b16_8 t341 = t340;
            gm_b16_8 t342 = t341;
            gm_b16_8 t343 = GM_LD(gm_b16_8, ((uint8_t *)&t342));
            t331 = t343;
            goto ix332;
ie332:;
            gm_t6j6smj7qoctu t345;
            gm_b16_8 t346 = gm_l4;
            GM_ST(gm_b16_8, (uint8_t *)&t345 + 0, t346);
            gm_b16_8 t347 = gm_l5;
            GM_ST(gm_b16_8, (uint8_t *)&t345 + 16, t347);
            gm_tabb710d9mq2e t348;
            gm_f_format_4q9e8cc((void *)&t348, (void *)NULL, cx, NULL, &t345);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t349 = t348;
            if (((((uint8_t *)&t349))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t349) + 8, gm_src4pc97o88riqc, 197, 81);
            gm_b16_8 t350 = GM_LD(gm_b16_8, (((uint8_t *)&t349) + 8));
            gm_b16_8 t351 = t350;
            gm_b16_8 t352 = t351;
            gm_b16_8 t353 = GM_LD(gm_b16_8, ((uint8_t *)&t352));
            t331 = t353;
ix332:;
            gm_l28 = t331;
        }
        {
            void * t354 = gm_l0;
            gm_t88rcucg1b7uk t355 = gm_l9;
            gm_t6j6smj7qoctu t356;
            gm_b16_8 t357 = gm_l7;
            GM_ST(gm_b16_8, (uint8_t *)&t356 + 0, t357);
            gm_b16_8 t358 = gm_l28;
            GM_ST(gm_b16_8, (uint8_t *)&t356 + 16, t358);
            gm_tabb710d9mq2e t359;
            gm_f_format_3vtl3te((void *)&t359, (void *)NULL, cx, NULL, &t356);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t360 = t359;
            if (((((uint8_t *)&t360))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t360) + 8, gm_src4pc97o88riqc, 198, 30);
            gm_b16_8 t361 = GM_LD(gm_b16_8, (((uint8_t *)&t360) + 8));
            gm_b16_8 t362 = t361;
            gm_b16_8 t363 = GM_LD(gm_b16_8, ((uint8_t *)&t362));
            gm_b16_8 t364 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t354, t355, t363);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t5o0tpv5jka2l t365;
            GM_ST(uint8_t, (uint8_t *)&t365 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t365 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t365 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t365 + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&t365 + 32, 0);
            ((uint8_t *)&t365)[0] = 0;
            GM_ST(gm_b16_8, ((uint8_t *)&t365 + 8), t364);
            gm_t5o0tpv5jka2l t366 = t365;
            uint8_t t367 = ((((uint8_t *)&t366))[0] != 0);
            F.ret = t365;
            goto gm_fret;
        }
        goto me14;
mn300:;
ma18:;
        uint8_t t371 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t371 != ((uint8_t)7ULL)) goto pa370;
        goto po369;
pa370:;
        uint8_t t373 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t373 != ((uint8_t)8ULL)) goto pa372;
        goto po369;
pa372:;
        uint8_t t375 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t375 != ((uint8_t)9ULL)) goto pa374;
        goto po369;
pa374:;
        goto mn368;
po369:;
        {
            gm_b16_8 t377;
            uint8_t t383 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            switch (t383) { case ((uint8_t)0ULL): goto ma381; case ((uint8_t)1ULL): goto ma381; case ((uint8_t)2ULL): goto ma381; case ((uint8_t)3ULL): goto ma381; case ((uint8_t)4ULL): goto ma381; case ((uint8_t)5ULL): goto ma381; case ((uint8_t)6ULL): goto ma381; case ((uint8_t)7ULL): goto ma379; case ((uint8_t)8ULL): goto ma380; case ((uint8_t)9ULL): goto ma381; case ((uint8_t)10ULL): goto ma381; case ((uint8_t)11ULL): goto ma381; case ((uint8_t)12ULL): goto ma381; case ((uint8_t)13ULL): goto ma381; default: goto ma381; }
ma379:;
            uint8_t t385 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t385 != ((uint8_t)7ULL)) goto mn384;
            gm_b16_8 t386;
            GM_ST(void *, (uint8_t *)&t386, (void *)(gm_smerqm8g793l6));
            GM_ST(uintptr_t, (uint8_t *)&t386 + 8, (uintptr_t)(1));
            t377 = t386;
            goto me378;
mn384:;
ma380:;
            uint8_t t388 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t388 != ((uint8_t)8ULL)) goto mn387;
            gm_b16_8 t389;
            GM_ST(void *, (uint8_t *)&t389, (void *)(gm_s2u5posfkbgnq));
            GM_ST(uintptr_t, (uint8_t *)&t389 + 8, (uintptr_t)(1));
            t377 = t389;
            goto me378;
mn387:;
ma381:;
            gm_b16_8 t391;
            GM_ST(void *, (uint8_t *)&t391, (void *)(gm_srs1vrn2uaams));
            GM_ST(uintptr_t, (uint8_t *)&t391 + 8, (uintptr_t)(1));
            t377 = t391;
            goto me378;
mn390:;
ma382:;
            gm_trap("no match arm matched the scrutinee", gm_src4pc97o88riqc, 201, 25);
me378:;
            gm_l29 = t377;
        }
        {
            void * t392 = gm_l0;
            gm_t88rcucg1b7uk t393 = gm_l9;
            gm_t0uuhaljsf410 t394;
            gm_b16_8 t395 = gm_l7;
            GM_ST(gm_b16_8, (uint8_t *)&t394 + 0, t395);
            gm_b16_8 t396 = gm_l4;
            GM_ST(gm_b16_8, (uint8_t *)&t394 + 16, t396);
            gm_b16_8 t397 = gm_l29;
            GM_ST(gm_b16_8, (uint8_t *)&t394 + 32, t397);
            gm_b16_8 t398 = gm_l5;
            GM_ST(gm_b16_8, (uint8_t *)&t394 + 48, t398);
            gm_tabb710d9mq2e t399;
            gm_f_format_tth3l1d((void *)&t399, (void *)NULL, cx, NULL, &t394);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t400 = t399;
            if (((((uint8_t *)&t400))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t400) + 8, gm_src4pc97o88riqc, 206, 30);
            gm_b16_8 t401 = GM_LD(gm_b16_8, (((uint8_t *)&t400) + 8));
            gm_b16_8 t402 = t401;
            gm_b16_8 t403 = GM_LD(gm_b16_8, ((uint8_t *)&t402));
            gm_b16_8 t404 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t392, t393, t403);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t5o0tpv5jka2l t405;
            GM_ST(uint8_t, (uint8_t *)&t405 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t405 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t405 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t405 + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&t405 + 32, 0);
            ((uint8_t *)&t405)[0] = 0;
            GM_ST(gm_b16_8, ((uint8_t *)&t405 + 8), t404);
            gm_t5o0tpv5jka2l t406 = t405;
            uint8_t t407 = ((((uint8_t *)&t406))[0] != 0);
            F.ret = t405;
            goto gm_fret;
        }
        goto me14;
mn368:;
ma19:;
        gm_trap("reached `@unreachable()`", gm_src4pc97o88riqc, 208, 14);
        goto me14;
mn408:;
ma20:;
        gm_trap("no match arm matched the scrutinee", gm_src4pc97o88riqc, 144, 5);
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
inline __attribute__((aligned(4))) void gm_f_int_info_2tcdf1n(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_int_info_2tcdf1n F;
    void * gm_l0; /* fg */
    gm_t88rcucg1b7uk gm_l1; /* t */
    uint8_t gm_l2; /* signed */
    uint32_t gm_l3; /* bits */
    uint64_t gm_l4; /* storage */
    GM_STACK_CHECK(&F, 194u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t3 = gm_l0;
        void * t4 = GM_LD(void *, (((uint8_t *)t3) + 0));
        void * t5 = (void *)(((uint8_t *)t4) + 8);
        gm_t88rcucg1b7uk t6 = gm_l1;
        gm_t5v8n6qrn4rh4 t7;
        gm_f_repr_53quqki((void *)&t7, (void *)NULL, cx, NULL, t5, t6);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t5v8n6qrn4rh4 t8 = t7;
        uint8_t t9 = GM_LD(uint8_t, (((uint8_t *)&t8) + 0));
        if (t9 != ((uint8_t)0ULL)) goto ie2;
        uint8_t t10 = GM_LD(uint8_t, ((((uint8_t *)&t8) + 8) + 0));
        gm_l2 = t10;
        uint32_t t11 = GM_LD(uint32_t, ((((uint8_t *)&t8) + 8) + 4));
        gm_l3 = t11;
        uint64_t t12 = GM_LD(uint64_t, ((((uint8_t *)&t8) + 8) + 8));
        gm_l4 = t12;
        {
            uint8_t t14 = gm_l2;
            uint32_t t15 = gm_l3;
            uint64_t t16 = gm_l4;
            gm_tuglhjjmj2g58 t17;
            GM_ST(uint8_t, (uint8_t *)&t17 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t17 + 2, 0);
            GM_ST(uint8_t, ((uint8_t *)&t17 + 0), t14);
            GM_ST(uint32_t, ((uint8_t *)&t17 + 4), t15);
            GM_ST(uint64_t, ((uint8_t *)&t17 + 8), t16);
            gm_tqcc6dmlne11b t18;
            GM_ST(uint8_t, (uint8_t *)&t18 + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&t18 + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&t18 + 20, 0);
            GM_ST(gm_tuglhjjmj2g58, ((uint8_t *)&t18), t17);
            ((uint8_t *)&t18)[16] = 1;
            uint8_t t19 = 0;
            F.ret = t18;
            goto gm_fret;
        }
        goto ix2;
ie2:;
        {
            gm_tqcc6dmlne11b t21;
            memset(&t21, 0, sizeof t21);
            uint8_t t22 = 0;
            F.ret = t21;
            goto gm_fret;
        }
ix2:;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_normalize_jch381e(void *env, gm_node *cx, gm_node *inst, gm_tuglhjjmj2g58 a0, gm_b16_8 a1) {
    struct gm_fr_normalize_jch381e F;
    gm_tuglhjjmj2g58 gm_l0; /* ii */
    gm_b16_8 gm_l1; /* v */
    uint64_t gm_l2; /* shift */
    GM_STACK_CHECK(&F, 637u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        gm_tuglhjjmj2g58 t3 = gm_l0;
        uint8_t t4 = gm_f_ii_exact_g6a19dp(GM_CONV(gm_cs2lqoac0a5ng4, t3));
        if (!t4) goto ie2;
        {
            gm_b16_8 t6 = gm_l1;
            uint8_t t7 = 0;
            F.ret = t6;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        uint64_t t8 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
        uint64_t r9; bool o10 = __builtin_mul_overflow(t8, ((uint64_t)8ULL), &r9);
        if (o10) gm_trap("integer overflow in `*`", gm_src4pc97o88riqc, 57, 14);
        uint64_t t11 = r9;
        uint32_t t12 = GM_LD(uint32_t, (((uint8_t *)&gm_l0) + 4));
        uint64_t t13 = (uint64_t)t12;
        uint64_t r14; bool o15 = __builtin_sub_overflow(t11, t13, &r14);
        if (o15) gm_trap("integer overflow in `-`", gm_src4pc97o88riqc, 57, 14);
        uint64_t t16 = r14;
        gm_l2 = t16;
    }
    {
        uint8_t t18 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
        if (!t18) goto ie17;
        {
            gm_tg23pq9a29gh1 t20;
            gm_tuglhjjmj2g58 t21 = gm_l0;
            gm_b16_8 t22 = gm_f_ii_cty_i2b4e03(GM_CONV(gm_cs2lqoac0a5ng4, t21));
            GM_ST(gm_b16_8, (uint8_t *)&t20 + 0, t22);
            gm_tuglhjjmj2g58 t23 = gm_l0;
            gm_b16_8 t24 = gm_f_ii_ucty_b40h5ot(GM_CONV(gm_cs2lqoac0a5ng4, t23));
            GM_ST(gm_b16_8, (uint8_t *)&t20 + 16, t24);
            gm_b16_8 t25 = gm_l1;
            GM_ST(gm_b16_8, (uint8_t *)&t20 + 32, t25);
            uint64_t t26 = gm_l2;
            GM_ST(uint64_t, (uint8_t *)&t20 + 48, t26);
            uint64_t t27 = gm_l2;
            GM_ST(uint64_t, (uint8_t *)&t20 + 56, t27);
            gm_tabb710d9mq2e t28;
            gm_f_format_u9gq54a((void *)&t28, (void *)NULL, cx, NULL, &t20);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t29 = t28;
            if (((((uint8_t *)&t29))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t29) + 8, gm_src4pc97o88riqc, 59, 16);
            gm_b16_8 t30 = GM_LD(gm_b16_8, (((uint8_t *)&t29) + 8));
            gm_b16_8 t31 = t30;
            gm_b16_8 t32 = GM_LD(gm_b16_8, ((uint8_t *)&t31));
            uint8_t t33 = 0;
            F.ret = t32;
            goto gm_fret;
        }
        goto ix17;
ie17:;
ix17:;
    }
    {
        gm_tj4tbafenpsjm t34;
        memset((uint8_t *)&t34 + 52, 0, 4);
        gm_tuglhjjmj2g58 t35 = gm_l0;
        gm_b16_8 t36 = gm_f_ii_cty_i2b4e03(GM_CONV(gm_cs2lqoac0a5ng4, t35));
        GM_ST(gm_b16_8, (uint8_t *)&t34 + 0, t36);
        gm_b16_8 t37 = gm_l1;
        GM_ST(gm_b16_8, (uint8_t *)&t34 + 16, t37);
        gm_tuglhjjmj2g58 t38 = gm_l0;
        gm_b16_8 t39 = gm_f_ii_cty_i2b4e03(GM_CONV(gm_cs2lqoac0a5ng4, t38));
        GM_ST(gm_b16_8, (uint8_t *)&t34 + 32, t39);
        uint32_t t40 = GM_LD(uint32_t, (((uint8_t *)&gm_l0) + 4));
        GM_ST(uint32_t, (uint8_t *)&t34 + 48, t40);
        gm_tabb710d9mq2e t41;
        gm_f_format_1ck6tsm((void *)&t41, (void *)NULL, cx, NULL, &t34);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t42 = t41;
        if (((((uint8_t *)&t42))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t42) + 8, gm_src4pc97o88riqc, 61, 12);
        gm_b16_8 t43 = GM_LD(gm_b16_8, (((uint8_t *)&t42) + 8));
        gm_b16_8 t44 = t43;
        gm_b16_8 t45 = GM_LD(gm_b16_8, ((uint8_t *)&t44));
        uint8_t t46 = 0;
        F.ret = t45;
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
__attribute__((aligned(4))) void gm_f_unary_i1ekgvj(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t9qkh0rsooc8d a2, void * a3) {
    struct gm_fr_unary_i1ekgvj F;
    void * gm_l0; /* fg */
    void * gm_l1; /* e */
    gm_t9qkh0rsooc8d gm_l2; /* op */
    void * gm_l3; /* x */
    gm_b16_8 gm_l4; /* v */
    gm_t88rcucg1b7uk gm_l5; /* t */
    gm_tuglhjjmj2g58 gm_l6; /* ii */
    gm_b16_8 gm_l7; /* min */
    gm_b16_8 gm_l8; /* loc */
    gm_b16_8 gm_l9; /* r */
    gm_b16_8 gm_l10; /* r2 */
    gm_tuglhjjmj2g58 gm_l11; /* ii */
    gm_b16_8 gm_l12; /* r */
    GM_STACK_CHECK(&F, 2727u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        void * t2 = gm_l0;
        void * t3 = gm_l3;
        gm_t5o0tpv5jka2l t4;
        gm_f_expr_porbc9t((void *)&t4, (void *)NULL, cx, NULL, t2, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t5o0tpv5jka2l t5 = t4;
        if (((((uint8_t *)&t5))[0] != 0)) {
            gm_t08olq97rscn3 t6 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&t5) + 8));
            gm_t5o0tpv5jka2l t7;
            GM_ST(uint8_t, (uint8_t *)&t7 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t7 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t7 + 4, 0);
            ((uint8_t *)&t7)[0] = 1;
            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t7 + 8), t6);
            F.ret = t7;
            goto gm_fret;
        }
        gm_b16_8 t8 = GM_LD(gm_b16_8, (((uint8_t *)&t5) + 8));
        gm_l4 = t8;
    }
    {
        void * t9 = gm_l1;
        gm_t88rcucg1b7uk t10 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t9) + 0));
        gm_l5 = t10;
    }
    {
        uint8_t t16 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        switch (t16) { case ((uint8_t)0ULL): goto ma13; case ((uint8_t)1ULL): goto ma14; case ((uint8_t)2ULL): goto ma12; default: goto ma15; }
ma12:;
        uint8_t t18 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t18 != ((uint8_t)2ULL)) goto mn17;
        void * t19 = gm_l0;
        gm_t88rcucg1b7uk t20 = gm_l5;
        gm_b16_8 t21 = gm_l4;
        gm_tnpifv20irnph t22;
        GM_ST(gm_b16_8, ((uint8_t *)&t22 + 0), t21);
        gm_tabb710d9mq2e t23;
        gm_f_format_9221qv6((void *)&t23, (void *)NULL, cx, NULL, t22);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t24 = t23;
        if (((((uint8_t *)&t24))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t24) + 8, gm_src4pc97o88riqc, 68, 34);
        gm_b16_8 t25 = GM_LD(gm_b16_8, (((uint8_t *)&t24) + 8));
        gm_b16_8 t26 = t25;
        gm_b16_8 t27 = GM_LD(gm_b16_8, ((uint8_t *)&t26));
        gm_b16_8 t28 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t19, t20, t27);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t5o0tpv5jka2l t29;
        GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t29 + 24, 0);
        GM_ST(uint64_t, (uint8_t *)&t29 + 32, 0);
        ((uint8_t *)&t29)[0] = 0;
        GM_ST(gm_b16_8, ((uint8_t *)&t29 + 8), t28);
        gm_t5o0tpv5jka2l t30 = t29;
        uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
        F.ret = t29;
        goto gm_fret;
        goto me11;
mn17:;
ma13:;
        uint8_t t33 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t33 != ((uint8_t)0ULL)) goto mn32;
        void * t34 = gm_l0;
        void * t35 = GM_LD(void *, (((uint8_t *)t34) + 0));
        void * t36 = (void *)(((uint8_t *)t35) + 8);
        void * t37 = gm_l3;
        gm_t88rcucg1b7uk t38 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t37) + 0));
        gm_t5v8n6qrn4rh4 t39;
        gm_f_repr_53quqki((void *)&t39, (void *)NULL, cx, NULL, t36, t38);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t5v8n6qrn4rh4 t40 = t39;
        uint8_t t46 = GM_LD(uint8_t, (((uint8_t *)&t40) + 0));
        switch (t46) { case ((uint8_t)0ULL): goto ma43; case ((uint8_t)1ULL): goto ma42; case ((uint8_t)2ULL): goto ma44; case ((uint8_t)3ULL): goto ma44; case ((uint8_t)4ULL): goto ma44; case ((uint8_t)5ULL): goto ma44; case ((uint8_t)6ULL): goto ma44; default: goto ma44; }
ma42:;
        uint8_t t48 = GM_LD(uint8_t, (((uint8_t *)&t40) + 0));
        if (t48 != ((uint8_t)1ULL)) goto mn47;
        void * t49 = gm_l0;
        gm_t88rcucg1b7uk t50 = gm_l5;
        gm_b16_8 t51 = gm_l4;
        gm_tnpifv20irnph t52;
        GM_ST(gm_b16_8, ((uint8_t *)&t52 + 0), t51);
        gm_tabb710d9mq2e t53;
        gm_f_format_dlemvkb((void *)&t53, (void *)NULL, cx, NULL, t52);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t54 = t53;
        if (((((uint8_t *)&t54))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t54) + 8, gm_src4pc97o88riqc, 70, 43);
        gm_b16_8 t55 = GM_LD(gm_b16_8, (((uint8_t *)&t54) + 8));
        gm_b16_8 t56 = t55;
        gm_b16_8 t57 = GM_LD(gm_b16_8, ((uint8_t *)&t56));
        gm_b16_8 t58 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t49, t50, t57);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t5o0tpv5jka2l t59;
        GM_ST(uint8_t, (uint8_t *)&t59 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t59 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t59 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t59 + 24, 0);
        GM_ST(uint64_t, (uint8_t *)&t59 + 32, 0);
        ((uint8_t *)&t59)[0] = 0;
        GM_ST(gm_b16_8, ((uint8_t *)&t59 + 8), t58);
        gm_t5o0tpv5jka2l t60 = t59;
        uint8_t t61 = ((((uint8_t *)&t60))[0] != 0);
        F.ret = t59;
        goto gm_fret;
        goto me41;
mn47:;
ma43:;
        uint8_t t63 = GM_LD(uint8_t, (((uint8_t *)&t40) + 0));
        if (t63 != ((uint8_t)0ULL)) goto mn62;
        {
            void * t65 = gm_l0;
            void * t66 = gm_l3;
            gm_t88rcucg1b7uk t67 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t66) + 0));
            gm_tqcc6dmlne11b t68;
            gm_f_int_info_2tcdf1n((void *)&t68, (void *)NULL, cx, NULL, t65, t67);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tqcc6dmlne11b t69 = t68;
            if (!((((uint8_t *)&t69))[16] != 0)) gm_trap("unwrap of null", gm_src4pc97o88riqc, 72, 23);
            gm_tuglhjjmj2g58 t70 = GM_LD(gm_tuglhjjmj2g58, ((uint8_t *)&t69));
            gm_l6 = t70;
        }
        {
            void * t71 = gm_l0;
            void * t72 = gm_l3;
            gm_t88rcucg1b7uk t73 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t72) + 0));
            gm_tuglhjjmj2g58 t74 = gm_l6;
            gm_tbci5ospk5nnn t75;
            gm_f_ii_min_lku7pdi((void *)&t75, (void *)NULL, cx, NULL, t74);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_b16_8 t76 = gm_f_int_lit_ai37456((void *)NULL, cx, NULL, t71, t73, &t75);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l7 = t76;
        }
        {
            void * t78 = gm_l0;
            void * t79 = GM_LD(void *, (((uint8_t *)t78) + 0));
            uint8_t t80 = GM_LD(uint8_t, ((((uint8_t *)t79) + 1136) + 0));
            if (!t80) goto ie77;
            {
                void * t82 = gm_l0;
                void * t83 = gm_l1;
                gm_b16_8 t84 = gm_f_loc_u9c0aqb((void *)NULL, cx, NULL, t82, t83);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l8 = t84;
            }
            {
                void * t85 = gm_l0;
                gm_tfeab279c16rr t86;
                gm_b16_8 t87 = gm_l4;
                GM_ST(gm_b16_8, (uint8_t *)&t86 + 0, t87);
                gm_b16_8 t88 = gm_l7;
                GM_ST(gm_b16_8, (uint8_t *)&t86 + 16, t88);
                gm_b16_8 t89 = gm_l8;
                GM_ST(gm_b16_8, (uint8_t *)&t86 + 32, t89);
                gm_b0_1 t90 = gm_f_linef_fdhsfa1((void *)NULL, cx, NULL, t85, &t86);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix77;
ie77:;
ix77:;
        }
        {
            gm_t0uuhaljsf410 t91;
            gm_tuglhjjmj2g58 t92 = gm_l6;
            gm_b16_8 t93 = gm_f_ii_cty_i2b4e03(GM_CONV(gm_cs2lqoac0a5ng4, t92));
            GM_ST(gm_b16_8, (uint8_t *)&t91 + 0, t93);
            gm_tuglhjjmj2g58 t94 = gm_l6;
            gm_b16_8 t95 = gm_f_ii_ucty_b40h5ot(GM_CONV(gm_cs2lqoac0a5ng4, t94));
            GM_ST(gm_b16_8, (uint8_t *)&t91 + 16, t95);
            gm_tuglhjjmj2g58 t96 = gm_l6;
            gm_b16_8 t97 = gm_f_ii_ucty_b40h5ot(GM_CONV(gm_cs2lqoac0a5ng4, t96));
            GM_ST(gm_b16_8, (uint8_t *)&t91 + 32, t97);
            gm_b16_8 t98 = gm_l4;
            GM_ST(gm_b16_8, (uint8_t *)&t91 + 48, t98);
            gm_tabb710d9mq2e t99;
            gm_f_format_5r372o6((void *)&t99, (void *)NULL, cx, NULL, &t91);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t100 = t99;
            if (((((uint8_t *)&t100))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t100) + 8, gm_src4pc97o88riqc, 78, 22);
            gm_b16_8 t101 = GM_LD(gm_b16_8, (((uint8_t *)&t100) + 8));
            gm_l9 = t101;
        }
        {
            gm_tuglhjjmj2g58 t102 = gm_l6;
            gm_b16_8 t103 = gm_l9;
            gm_b16_8 t104 = t103;
            gm_b16_8 t105 = GM_LD(gm_b16_8, ((uint8_t *)&t104));
            gm_b16_8 t106 = gm_f_normalize_jch381e((void *)NULL, cx, NULL, t102, t105);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l10 = t106;
        }
        {
            void * t107 = gm_l0;
            gm_t88rcucg1b7uk t108 = gm_l5;
            gm_b16_8 t109 = gm_l10;
            gm_b16_8 t110 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t107, t108, t109);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t5o0tpv5jka2l t111;
            GM_ST(uint8_t, (uint8_t *)&t111 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t111 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t111 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t111 + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&t111 + 32, 0);
            ((uint8_t *)&t111)[0] = 0;
            GM_ST(gm_b16_8, ((uint8_t *)&t111 + 8), t110);
            gm_t5o0tpv5jka2l t112 = t111;
            uint8_t t113 = ((((uint8_t *)&t112))[0] != 0);
            F.ret = t111;
            goto gm_fret;
        }
        goto me41;
mn62:;
ma44:;
        void * t115 = gm_l1;
        gm_tl874t2atnlh3 t116 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t115) + 72));
        void * t117 = gm_l0;
        void * t118 = GM_LD(void *, (((uint8_t *)t117) + 0));
        void * t119 = (void *)(((uint8_t *)t118) + 8);
        void * t120 = gm_l3;
        gm_t88rcucg1b7uk t121 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t120) + 0));
        gm_b16_8 t122 = gm_f_show_5qbi623((void *)NULL, cx, NULL, t119, t121);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tnpifv20irnph t123;
        GM_ST(gm_b16_8, ((uint8_t *)&t123 + 0), t122);
        gm_tabb710d9mq2e t124;
        gm_f_format_las1g7o((void *)&t124, (void *)NULL, cx, NULL, t123);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t125 = t124;
        if (((((uint8_t *)&t125))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t125) + 8, gm_src4pc97o88riqc, 82, 46);
        gm_b16_8 t126 = GM_LD(gm_b16_8, (((uint8_t *)&t125) + 8));
        gm_b16_8 t127 = t126;
        gm_b16_8 t128 = GM_LD(gm_b16_8, ((uint8_t *)&t127));
        gm_t08olq97rscn3 t129;
        gm_f_unsupported_531q51r((void *)&t129, (void *)NULL, cx, NULL, t116, t128);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t5o0tpv5jka2l t130;
        GM_ST(uint8_t, (uint8_t *)&t130 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t130 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t130 + 4, 0);
        ((uint8_t *)&t130)[0] = 1;
        GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t130 + 8), t129);
        gm_t5o0tpv5jka2l t131 = t130;
        uint8_t t132 = ((((uint8_t *)&t131))[0] != 0);
        F.ret = t130;
        goto gm_fret;
        goto me41;
mn114:;
ma45:;
        gm_trap("no match arm matched the scrutinee", gm_src4pc97o88riqc, 69, 17);
me41:;
        goto me11;
mn32:;
ma14:;
        uint8_t t134 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t134 != ((uint8_t)1ULL)) goto mn133;
        {
            void * t136 = gm_l0;
            void * t137 = gm_l3;
            gm_t88rcucg1b7uk t138 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t137) + 0));
            gm_tqcc6dmlne11b t139;
            gm_f_int_info_2tcdf1n((void *)&t139, (void *)NULL, cx, NULL, t136, t138);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tqcc6dmlne11b t140 = t139;
            gm_tuglhjjmj2g58 t141;
            if (((((uint8_t *)&t140))[16] != 0)) {
                gm_tuglhjjmj2g58 t142 = GM_LD(gm_tuglhjjmj2g58, (((uint8_t *)&t140) + 0));
                t141 = t142;
            } else {
                void * t143 = gm_l1;
                gm_tl874t2atnlh3 t144 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t143) + 72));
                void * t145 = gm_l0;
                void * t146 = GM_LD(void *, (((uint8_t *)t145) + 0));
                void * t147 = (void *)(((uint8_t *)t146) + 8);
                void * t148 = gm_l3;
                gm_t88rcucg1b7uk t149 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t148) + 0));
                gm_b16_8 t150 = gm_f_show_5qbi623((void *)NULL, cx, NULL, t147, t149);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tnpifv20irnph t151;
                GM_ST(gm_b16_8, ((uint8_t *)&t151 + 0), t150);
                gm_tabb710d9mq2e t152;
                gm_f_format_loa1toi((void *)&t152, (void *)NULL, cx, NULL, t151);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t153 = t152;
                if (((((uint8_t *)&t153))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t153) + 8, gm_src4pc97o88riqc, 85, 70);
                gm_b16_8 t154 = GM_LD(gm_b16_8, (((uint8_t *)&t153) + 8));
                gm_b16_8 t155 = t154;
                gm_b16_8 t156 = GM_LD(gm_b16_8, ((uint8_t *)&t155));
                gm_t08olq97rscn3 t157;
                gm_f_unsupported_531q51r((void *)&t157, (void *)NULL, cx, NULL, t144, t156);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_t5o0tpv5jka2l t158;
                GM_ST(uint8_t, (uint8_t *)&t158 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t158 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t158 + 4, 0);
                ((uint8_t *)&t158)[0] = 1;
                GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t158 + 8), t157);
                gm_t5o0tpv5jka2l t159 = t158;
                uint8_t t160 = ((((uint8_t *)&t159))[0] != 0);
                F.ret = t158;
                goto gm_fret;
            }
            gm_l11 = t141;
        }
        {
            gm_tuglhjjmj2g58 t161 = gm_l11;
            gm_t6j6smj7qoctu t162;
            gm_tuglhjjmj2g58 t163 = gm_l11;
            gm_b16_8 t164 = gm_f_ii_cty_i2b4e03(GM_CONV(gm_cs2lqoac0a5ng4, t163));
            GM_ST(gm_b16_8, (uint8_t *)&t162 + 0, t164);
            gm_b16_8 t165 = gm_l4;
            GM_ST(gm_b16_8, (uint8_t *)&t162 + 16, t165);
            gm_tabb710d9mq2e t166;
            gm_f_format_oe49e3n((void *)&t166, (void *)NULL, cx, NULL, &t162);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t167 = t166;
            if (((((uint8_t *)&t167))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t167) + 8, gm_src4pc97o88riqc, 86, 32);
            gm_b16_8 t168 = GM_LD(gm_b16_8, (((uint8_t *)&t167) + 8));
            gm_b16_8 t169 = t168;
            gm_b16_8 t170 = GM_LD(gm_b16_8, ((uint8_t *)&t169));
            gm_b16_8 t171 = gm_f_normalize_jch381e((void *)NULL, cx, NULL, t161, t170);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l12 = t171;
        }
        {
            void * t172 = gm_l0;
            gm_t88rcucg1b7uk t173 = gm_l5;
            gm_b16_8 t174 = gm_l12;
            gm_b16_8 t175 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t172, t173, t174);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t5o0tpv5jka2l t176;
            GM_ST(uint8_t, (uint8_t *)&t176 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t176 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t176 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t176 + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&t176 + 32, 0);
            ((uint8_t *)&t176)[0] = 0;
            GM_ST(gm_b16_8, ((uint8_t *)&t176 + 8), t175);
            gm_t5o0tpv5jka2l t177 = t176;
            uint8_t t178 = ((((uint8_t *)&t177))[0] != 0);
            F.ret = t176;
            goto gm_fret;
        }
        goto me11;
mn133:;
ma15:;
        gm_trap("no match arm matched the scrutinee", gm_src4pc97o88riqc, 67, 5);
me11:;
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
