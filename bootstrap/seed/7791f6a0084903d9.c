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
__attribute__((aligned(4))) gm_b0_1 gm_f_for_body_efhhmqv(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern uint8_t gm_c0lkld0prd0[18] __attribute__((aligned(1)));
extern uint8_t gm_c12s7ectveb[13] __attribute__((aligned(1)));
extern uint8_t gm_c17ch78chab[7] __attribute__((aligned(1)));
extern uint8_t gm_c19scujv4gq[6] __attribute__((aligned(1)));
extern uint8_t gm_c1f2aelscv0[8] __attribute__((aligned(1)));
extern uint8_t gm_c2t15afgpnn[8] __attribute__((aligned(8)));
extern uint8_t gm_c42ctv8im24[920] __attribute__((aligned(8)));
extern uint8_t gm_c4qfm55t8rc[3] __attribute__((aligned(1)));
extern uint8_t gm_c50ghpj4uh6[38] __attribute__((aligned(1)));
extern uint8_t gm_c50isunhcrj[16] __attribute__((aligned(8)));
extern uint8_t gm_c5glhoerm7q[21] __attribute__((aligned(1)));
extern uint8_t gm_c699d0m66pa[1] __attribute__((aligned(1)));
extern uint8_t gm_c6eh5l9ouqu[8] __attribute__((aligned(1)));
extern uint8_t gm_c6fgtimd3il[6] __attribute__((aligned(1)));
extern uint8_t gm_c82i6abqvvu[4] __attribute__((aligned(1)));
extern uint8_t gm_c854jn9qffs[12] __attribute__((aligned(1)));
extern uint8_t gm_c9nh9uke9t1[15] __attribute__((aligned(1)));
extern uint8_t gm_c9pef0g9fng[4] __attribute__((aligned(4)));
extern uint8_t gm_cbocru68uam[11] __attribute__((aligned(1)));
extern uint8_t gm_cd888ba1l0u[6] __attribute__((aligned(1)));
extern uint8_t gm_cdlh2b67kra[8] __attribute__((aligned(8)));
extern uint8_t gm_cduhcpt00sl[12] __attribute__((aligned(1)));
extern uint8_t gm_ce94e889ff7[9] __attribute__((aligned(1)));
extern uint8_t gm_cen8idnq6iv[6] __attribute__((aligned(1)));
extern uint8_t gm_cenuluelo82[3] __attribute__((aligned(1)));
extern uint8_t gm_cepccru8ka8[6] __attribute__((aligned(1)));
extern uint8_t gm_cfp6752ffka[12] __attribute__((aligned(1)));
extern uint8_t gm_cfrasdt0jpm[14] __attribute__((aligned(1)));
extern uint8_t gm_cfslp7hoejg[3] __attribute__((aligned(1)));
extern uint8_t gm_cg7qavecf9k[3] __attribute__((aligned(1)));
extern uint8_t gm_cgsatlrqc4j[3] __attribute__((aligned(1)));
extern uint8_t gm_chsbbntq1fu[11] __attribute__((aligned(1)));
extern uint8_t gm_chtdbvfshqk[10] __attribute__((aligned(1)));
extern uint8_t gm_ciu2mtp0ojg[10] __attribute__((aligned(1)));
extern uint8_t gm_civonf0vj4j[7] __attribute__((aligned(1)));
extern uint8_t gm_cj5c837l8v6[10] __attribute__((aligned(1)));
extern uint8_t gm_cjs2aavhl2o[8] __attribute__((aligned(8)));
extern uint8_t gm_ckbflbrril1[1] __attribute__((aligned(1)));
extern uint8_t gm_cl1d82mrtnv[40] __attribute__((aligned(1)));
extern uint8_t gm_clb2b60g1qs[13] __attribute__((aligned(1)));
extern uint8_t gm_clbqegpecii[16] __attribute__((aligned(1)));
extern uint8_t gm_cm0ptlu1qd7[25] __attribute__((aligned(1)));
extern uint8_t gm_cm8gajkonri[6] __attribute__((aligned(1)));
extern uint8_t gm_cnj3q1sfajm[12] __attribute__((aligned(1)));
extern uint8_t gm_cntjgiv42f7[23] __attribute__((aligned(1)));
extern uint8_t gm_co987o3ird4[8] __attribute__((aligned(1)));
extern uint8_t gm_cpkd7s1srcc[4517] __attribute__((aligned(1)));
extern uint8_t gm_cpma9mav80s[3] __attribute__((aligned(1)));
extern uint8_t gm_cpn2np70vcf[8] __attribute__((aligned(8)));
extern uint8_t gm_cri2dckkbh8[9] __attribute__((aligned(1)));
extern uint8_t gm_crpb2rekieb[1] __attribute__((aligned(1)));
extern uint8_t gm_csp8t1dbqmf[13] __attribute__((aligned(1)));
extern uint8_t gm_csvkj3mk95q[11] __attribute__((aligned(1)));
extern uint8_t gm_ctq6t8frtor[4] __attribute__((aligned(4)));
extern uint8_t gm_cu0b9l65c0v[1] __attribute__((aligned(1)));
extern uint8_t gm_cu6pb2rap57[3] __attribute__((aligned(1)));
extern uint8_t gm_cuqbj62gqhf_2[16] __attribute__((aligned(8)));
extern uint8_t gm_cvb3726geh8[3] __attribute__((aligned(1)));
extern uint8_t gm_cvrgakgvbh7[14] __attribute__((aligned(1)));
extern void *const gm_hls2l2mrnqun[];
extern uint8_t gm_k02l1tj96tm[40] __attribute__((aligned(8)));
extern uint8_t gm_k0nv110r163[40] __attribute__((aligned(8)));
extern uint8_t gm_k48l3rluocl[40] __attribute__((aligned(8)));
extern uint8_t gm_k4l7ojp6msf[40] __attribute__((aligned(8)));
extern uint8_t gm_k7bkj4i7gae[40] __attribute__((aligned(8)));
extern uint8_t gm_ka73u23j5nf[40] __attribute__((aligned(8)));
extern uint8_t gm_kc6vr8pmeqd[40] __attribute__((aligned(8)));
extern uint8_t gm_km7a24is5mf[40] __attribute__((aligned(8)));
extern uint8_t gm_ko53fduln7u[40] __attribute__((aligned(8)));
extern uint8_t gm_kq6te48ps2f[40] __attribute__((aligned(8)));
extern uint8_t gm_ks677q1halv[40] __attribute__((aligned(8)));
extern uint8_t gm_kt2nhjs8rv5[40] __attribute__((aligned(8)));
extern uint8_t gm_ktjao6lvic2[40] __attribute__((aligned(8)));
extern uint8_t gm_ku5cbgje11b[40] __attribute__((aligned(8)));
extern uint8_t gm_kujrnb073g1[40] __attribute__((aligned(8)));
extern uint8_t gm_kv89j160jqp[40] __attribute__((aligned(8)));
extern const uint8_t gm_s00046u02oar6[41];
extern const uint8_t gm_s01ofvj3chukm[10];
extern const uint8_t gm_s04qus0luoga5[5];
extern const uint8_t gm_s05336tfii06a[18];
extern const uint8_t gm_s078u1763sqk3[11];
extern const uint8_t gm_s07uhsrd798kj[9];
extern const uint8_t gm_s0adesbq24e9u[6];
extern const uint8_t gm_s0cf71ouneh5m[12];
extern const uint8_t gm_s0cpkv7s4ef85[24];
extern const uint8_t gm_s0ggo8ek7kfrt[34];
extern const uint8_t gm_s0jtk73jm5ft9[16];
extern const uint8_t gm_s0lrfrtvffhu7[4];
extern const uint8_t gm_s0notp50gncsl[12];
extern const uint8_t gm_s0odi4lbspe7f[25];
extern const uint8_t gm_s0qbjm15aot6s[11];
extern const uint8_t gm_s0rsc8ficqfcm[78];
extern const uint8_t gm_s0se3p14t5670[14];
extern const uint8_t gm_s0tuij8156k40[41];
extern const uint8_t gm_s108thc59vc7u[37];
extern const uint8_t gm_s11lbv9jc177b[4];
extern const uint8_t gm_s129prqlnb2kt[41];
extern const uint8_t gm_s148evlodt315[18];
extern const uint8_t gm_s15c9j2vv1d2a[39];
extern const uint8_t gm_s15p9kmrsfogp[2];
extern const uint8_t gm_s18dhen1d87oi[20];
extern const uint8_t gm_s1ctr86inn5iu[24];
extern const uint8_t gm_s1dc9boe5unhg[32];
extern const uint8_t gm_s1gm43jp1jqr0[89];
extern const uint8_t gm_s1jdtclvlju06[9];
extern const uint8_t gm_s1leke9tn7lrm[8];
extern const uint8_t gm_s1livc5021kui[9];
extern const uint8_t gm_s1lj31vo6lvqe[26];
extern const uint8_t gm_s1lnh4indnr62[4];
extern const uint8_t gm_s1rnr94j0tq8t[17];
extern const uint8_t gm_s1t5armttgi3e[41];
extern const uint8_t gm_s211k6darac91[10];
extern const uint8_t gm_s29jv7rttsnpq[52];
extern const uint8_t gm_s2hi6cc9n75l5[7];
extern const uint8_t gm_s2hut6059od59[42];
extern const uint8_t gm_s2j8t1jqamft8[5];
extern const uint8_t gm_s2k3mi7crs28d[5];
extern const uint8_t gm_s2nf76g2su2k3[15];
extern const uint8_t gm_s2nn3moc8tfri[17];
extern const uint8_t gm_s2nq2i5ut1dni[13];
extern const uint8_t gm_s2of8ntot1on0[81];
extern const uint8_t gm_s2tnbv3fg2r22[13];
extern const uint8_t gm_s2u46tincrqmv[25];
extern const uint8_t gm_s2u7e0bju5qb8[28];
extern const uint8_t gm_s336v1oga58d4[41];
extern const uint8_t gm_s33pl04oikskr[5];
extern const uint8_t gm_s3c7ikv4cpqg1[13];
extern const uint8_t gm_s3jgqb0t6i7b8[22];
extern const uint8_t gm_s3lovgpoph0mh[9];
extern const uint8_t gm_s3on77ql0ndko[17];
extern const uint8_t gm_s3sb6rrf98gpi[20];
extern const uint8_t gm_s3v312rvfthpf[43];
extern const uint8_t gm_s44ek1gecbajs[7];
extern const uint8_t gm_s4769el4glhs1[4];
extern const uint8_t gm_s487q6bd4pfu3[50];
extern const uint8_t gm_s498ag88l373e[14];
extern const uint8_t gm_s4a4j4epvkeso[11];
extern const uint8_t gm_s4b8airp3rh69[50];
extern const uint8_t gm_s4chbmodjkinv[7];
extern const uint8_t gm_s4emnk8r9sirq[11];
extern const uint8_t gm_s4gilalcmdvu3[8];
extern const uint8_t gm_s4ifh5kc7nc0d[62];
extern const uint8_t gm_s4jv43jfqhfjq[41];
extern const uint8_t gm_s4kt3mhlp0or9[11];
extern const uint8_t gm_s4nj79mj9jk57[21];
extern const uint8_t gm_s4o70d89183c2[4];
extern const uint8_t gm_s4ou57tmjn6c7[39];
extern const uint8_t gm_s4r2qm241merr[12];
extern const uint8_t gm_s519l85f26pmp[81];
extern const uint8_t gm_s52n7ji3kulf2[29];
extern const uint8_t gm_s53c18pcajigs[51];
extern const uint8_t gm_s55edm0in8jpg[19];
extern const uint8_t gm_s588gr73ph5f5[45];
extern const uint8_t gm_s58b2tdsrfkg1[85];
extern const uint8_t gm_s59a022mrijic[9];
extern const uint8_t gm_s5gbgbdradbg1[25];
extern const uint8_t gm_s5i05gtdgdefb[42];
extern const uint8_t gm_s5ivi0sts5foi[9];
extern const uint8_t gm_s5n5e484glelv[5];
extern const uint8_t gm_s5q33up034m64[37];
extern const uint8_t gm_s5r5k5b2k7bru[20];
extern const uint8_t gm_s5ucn4crtdrq8[11];
extern const uint8_t gm_s632oftf9nssu[43];
extern const uint8_t gm_s63o5blu3j46d[16];
extern const uint8_t gm_s65rai75evpi7[14];
extern const uint8_t gm_s66fk044n1n0k[9];
extern const uint8_t gm_s69kv55uiaqoj[10];
extern const uint8_t gm_s6d960pgtv3l9[26];
extern const uint8_t gm_s6db4pm3k5olv[3];
extern const uint8_t gm_s6dh2ca0ubali[7];
extern const uint8_t gm_s6jld2vf184ip[28];
extern const uint8_t gm_s6khfdbr5aof9[34];
extern const uint8_t gm_s6rivmf4qclvb[71];
extern const uint8_t gm_s70dhiaqijjg0[24];
extern const uint8_t gm_s721l5hj93rdl[50];
extern const uint8_t gm_s74v1q63ilf9i[122];
extern const uint8_t gm_s76ovr591an92[4];
extern const uint8_t gm_s7826b3jmrqb0[10];
extern const uint8_t gm_s794tb9qjlq1h[42];
extern const uint8_t gm_s7b0d890pj8dv[14];
extern const uint8_t gm_s7b7j2m1f6fle[71];
extern const uint8_t gm_s7bj0jma5l2mg[39];
extern const uint8_t gm_s7euumoo24h31[3];
extern const uint8_t gm_s7f47bs0i7q74[21];
extern const uint8_t gm_s7ipvt5onv6ht[4];
extern const uint8_t gm_s7pnhdrn5iusl[53];
extern const uint8_t gm_s7rl9plavbrfo[17];
extern const uint8_t gm_s7s8viq00e4g2[15];
extern const uint8_t gm_s7ti0q30v2iq7[8];
extern const uint8_t gm_s81trlqpvp2do[11];
extern const uint8_t gm_s85m9c4pt0ikp[6];
extern const uint8_t gm_s872048h9ttis[26];
extern const uint8_t gm_s873vbpojuh8v[5];
extern const uint8_t gm_s87isqt5t284q[9];
extern const uint8_t gm_s894fmp947sfb[22];
extern const uint8_t gm_s8952afsouaih[21];
extern const uint8_t gm_s8c43u4dovbrb[17];
extern const uint8_t gm_s8e4n8lgtb0fh[41];
extern const uint8_t gm_s8m850dccae8f[88];
extern const uint8_t gm_s8olahjn69at6[21];
extern const uint8_t gm_s8oln6mi5b5qr[46];
extern const uint8_t gm_s8pt0g4bme9og[30];
extern const uint8_t gm_s8qn6r2vquv2d[24];
extern const uint8_t gm_s8srbeac8og16[29];
extern const uint8_t gm_s8u1980tbab9f[15];
extern const uint8_t gm_s8vhc5kn5i8tv[38];
extern const uint8_t gm_s8vthjuk0el8j[63];
extern const uint8_t gm_s927f9s6klgc5[4];
extern const uint8_t gm_s966nqk7bt67q[19];
extern const uint8_t gm_s988kcbq5r7tp[14];
extern const uint8_t gm_s99fe9sgbdt37[14];
extern const uint8_t gm_s9k2jkfctp1g5[72];
extern const uint8_t gm_s9oqmmiu8qcsh[4];
extern const uint8_t gm_s9pcjvamrhbn7[25];
extern const uint8_t gm_s9po3krkcue72[4];
extern const uint8_t gm_s9ug0q3248l8e[44];
extern const uint8_t gm_sa132smnv97ed[4];
extern const uint8_t gm_sa16a135q4am6[13];
extern const uint8_t gm_sa23fj423hlk4[6];
extern const uint8_t gm_sa7fm83sqt8aa[16];
extern const uint8_t gm_sa7gjgqof9s56[44];
extern const uint8_t gm_sa831sfid0hf3[2];
extern const uint8_t gm_sa98jv6l3j8el[6];
extern const uint8_t gm_sa9ehjurfkhlj[17];
extern const uint8_t gm_sabcps3pq8iv7[15];
extern const uint8_t gm_sag4ku07t2k8t[25];
extern const uint8_t gm_sakaoktvp5alh[52];
extern const uint8_t gm_samqd3pmlgqmg[14];
extern const uint8_t gm_sankagtb3ecgn[58];
extern const uint8_t gm_sao9h06bhkdja[26];
extern const uint8_t gm_saturamfhig5d[45];
extern const uint8_t gm_saufuk3rs2i2m[15];
extern const uint8_t gm_savnf0pcaokd0[24];
extern const uint8_t gm_sb2sr4t6stbn1[16];
extern const uint8_t gm_sb3o36rmuvj28[56];
extern const uint8_t gm_sb8jvk2vbau9p[25];
extern const uint8_t gm_sba0715plrn4t[22];
extern const uint8_t gm_sbabklk1lp75f[30];
extern const uint8_t gm_sbbre965bpo5f[77];
extern const uint8_t gm_sbcpd47bv0ru9[63];
extern const uint8_t gm_sbg03hl2obk72[10];
extern const uint8_t gm_sbg28ee7l5gjs[6];
extern const uint8_t gm_sbgdbpkf33nsc[30];
extern const uint8_t gm_sbhq27eoeitmb[24];
extern const uint8_t gm_sblodmj4vs4nd[24];
extern const uint8_t gm_sbmedt8vmt5ve[5];
extern const uint8_t gm_sbnms71r2jqr5[7];
extern const uint8_t gm_sbp7rkf7ng8nb[33];
extern const uint8_t gm_sc149ns1jm5on[9];
extern const uint8_t gm_sc73gru3hpv0i[8];
extern const uint8_t gm_sc7bkbe71it8c[10];
extern const uint8_t gm_sc8spe93fl51n[45];
extern const uint8_t gm_scavc1uf2hs8l[7];
extern const uint8_t gm_scc8p0a9eachg[4];
extern const uint8_t gm_scch7vqldh7aj[13];
extern const uint8_t gm_scd7cqpmpn0p9[78];
extern const uint8_t gm_sci406m9tkvjp[32];
extern const uint8_t gm_scjfd9b9vg99c[12];
extern const uint8_t gm_scongfv16i8kh[21];
extern const uint8_t gm_scstobchrdgiu[67];
extern const uint8_t gm_sctlci97unb16[36];
extern const uint8_t gm_scuaf7ud4smlt[123];
extern const uint8_t gm_sd1uj6gf5v8qh[8];
extern const uint8_t gm_sd80su3vlvhjt[27];
extern const uint8_t gm_sd98uehpeed1c[11];
extern const uint8_t gm_sd9r0l17bbuoe[5];
extern const uint8_t gm_sdark9dtsgt1q[41];
extern const uint8_t gm_sdbo9gngsfqdj[8];
extern const uint8_t gm_sdckdu8fe6iog[7];
extern const uint8_t gm_sdes3rh8ctb7v[21];
extern const uint8_t gm_sdgcpos3mblup[37];
extern const uint8_t gm_sdl8mgtvebfcs[3];
extern const uint8_t gm_sdpkj0pb6qtc0[33];
extern const uint8_t gm_sdpl6uk3q4cch[6];
extern const uint8_t gm_sdstli6hlsa1e[7];
extern const uint8_t gm_se39cci44d9rt[35];
extern const uint8_t gm_seb50ruktgoun[14];
extern const uint8_t gm_sebidnsgqg13d[45];
extern const uint8_t gm_seh12nehvgmhj[12];
extern const uint8_t gm_sehko5bb96s6p[5];
extern const uint8_t gm_sehokd57s045g[11];
extern const uint8_t gm_seleu2adn5qnd[18];
extern const uint8_t gm_selmorb0539e5[5];
extern const uint8_t gm_sen05m4ui88rt[58];
extern const uint8_t gm_seoeuc6nik756[40];
extern const uint8_t gm_seoiboio4n733[5];
extern const uint8_t gm_ses9qtp6kdd22[8];
extern const uint8_t gm_setftr5leknrn[23];
extern const uint8_t gm_sf0p9o7g7fr6l[7];
extern const uint8_t gm_sf18e6o1dss49[6];
extern const uint8_t gm_sf7ldnukv79cp[24];
extern const uint8_t gm_sfa86erpod86s[4];
extern const uint8_t gm_sfc9avb7sceba[21];
extern const uint8_t gm_sfltedqje18on[9];
extern const uint8_t gm_sfn5aos9cps1h[13];
extern const uint8_t gm_sfp78bn17ei97[60];
extern const uint8_t gm_sfpg6jsb3cfp6[23];
extern const uint8_t gm_sfss9c30cdmun[8];
extern const uint8_t gm_sfvc4ovr5hl6h[23];
extern const uint8_t gm_sg47f6qrh6jrs[32];
extern const uint8_t gm_sg5h7btla5ksa[29];
extern const uint8_t gm_sgdrhdkbtd5gr[19];
extern const uint8_t gm_sgfmudgd82uhr[24];
extern const uint8_t gm_sgga2ijjm9cqd[7];
extern const uint8_t gm_sgib96mf5meqv[11];
extern const uint8_t gm_sgj6i64633q0l[11];
extern const uint8_t gm_sglu5epjv38rl[92];
extern const uint8_t gm_sgmttpp6cmh41[14];
extern const uint8_t gm_sgqsd7lfikrak[41];
extern const uint8_t gm_sgurqv8kb2rs7[32];
extern const uint8_t gm_sh1to1i0u67lu[9];
extern const uint8_t gm_sh2urbm3ndjnn[31];
extern const uint8_t gm_sh4sp5o4s4bj1[9];
extern const uint8_t gm_sh5rvkuevnqlk[7];
extern const uint8_t gm_sha7302s2fme2[18];
extern const uint8_t gm_shbfu7hj4uhkt[16];
extern const uint8_t gm_shdcf0n4l1d3a[41];
extern const uint8_t gm_shde4bsckk9mt[4];
extern const uint8_t gm_she92i1u1a4v5[30];
extern const uint8_t gm_shgptuodlhrcq[61];
extern const uint8_t gm_shkq4botdoed7[6];
extern const uint8_t gm_shlk3g0tk7m4f[31];
extern const uint8_t gm_shnmjdmsj431f[17];
extern const uint8_t gm_sho5nuaja1vvf[5];
extern const uint8_t gm_shq89iblo3j6k[51];
extern const uint8_t gm_shrtuk3jka1au[16];
extern const uint8_t gm_shsifme8hcenm[17];
extern const uint8_t gm_shvak64vhokft[26];
extern const uint8_t gm_si0b5q45ot754[101];
extern const uint8_t gm_si0mnqg6j1eu8[19];
extern const uint8_t gm_si23lpqv3htdl[27];
extern const uint8_t gm_sibmp9mck7osv[13];
extern const uint8_t gm_sigac1ehmei1f[69];
extern const uint8_t gm_siiqktevj2s2h[25];
extern const uint8_t gm_sij2r6jhpquej[12];
extern const uint8_t gm_sikspprj7od7l[43];
extern const uint8_t gm_sio8t6i3tf974[11];
extern const uint8_t gm_siot2vlveunn8[18];
extern const uint8_t gm_sirg36m1q4cjm[55];
extern const uint8_t gm_sisb3n56u9bmt[15];
extern const uint8_t gm_siselrhsqskvi[6];
extern const uint8_t gm_sisp52otedii5[23];
extern const uint8_t gm_sithisfj5qgfv[67];
extern const uint8_t gm_siv3d9he09v3r[44];
extern const uint8_t gm_sivuq9c7rjrh5[16];
extern const uint8_t gm_sj2tf1ift8sga[21];
extern const uint8_t gm_sj36eemfvep3a[56];
extern const uint8_t gm_sj486prdcn2go[14];
extern const uint8_t gm_sj7i3vhsq2ifl[38];
extern const uint8_t gm_sjdn5gs7lu39i[57];
extern const uint8_t gm_sjjc564tiep5s[13];
extern const uint8_t gm_sjk23rfp9022d[14];
extern const uint8_t gm_sjnv3p8t49clu[9];
extern const uint8_t gm_sjqs7q3ostqbm[4];
extern const uint8_t gm_sju4s3ndpnsge[107];
extern const uint8_t gm_sk0h761p8pjoe[43];
extern const uint8_t gm_sk5qo3et4heam[40];
extern const uint8_t gm_sk88l68smd6kt[23];
extern const uint8_t gm_skat03ddcievj[39];
extern const uint8_t gm_skbmmrtbskpao[8];
extern const uint8_t gm_skem4050p6ec1[13];
extern const uint8_t gm_skkqaektdhkn1[29];
extern const uint8_t gm_skmg81d57fq1r[31];
extern const uint8_t gm_skmsgkj4jeo7n[6];
extern const uint8_t gm_skpsvuvlfj0ep[68];
extern const uint8_t gm_skq2b15b87en9[63];
extern const uint8_t gm_skqf89fdi6p8e[12];
extern const uint8_t gm_skqko8fn9asq3[97];
extern const uint8_t gm_skr2kn3pd8es7[11];
extern const uint8_t gm_skvqkn3gte5mk[11];
extern const uint8_t gm_sl439hggqojfk[15];
extern const uint8_t gm_sl69n28ucapfg[21];
extern const uint8_t gm_sl7p035rs001b[27];
extern const uint8_t gm_sl8of2tnl835f[6];
extern const uint8_t gm_sla366gd7nphd[58];
extern const uint8_t gm_slbqo2q5mg776[107];
extern const uint8_t gm_sll3452l5oa19[6];
extern const uint8_t gm_slnfks4sl1aoc[8];
extern const uint8_t gm_slpo22sjs83b1[5];
extern const uint8_t gm_slrtg04n254p4[19];
extern const uint8_t gm_sm3pe8sptk65k[24];
extern const uint8_t gm_sm8t3g6c72ol4[62];
extern const uint8_t gm_smc854k364mj6[21];
extern const uint8_t gm_smdv8hgj9kr3n[21];
extern const uint8_t gm_smf5jnr5ps1bl[5];
extern const uint8_t gm_smohsnbvmf059[10];
extern const uint8_t gm_smptjqchq7bt0[47];
extern const uint8_t gm_smu48daue2qai[6];
extern const uint8_t gm_sn0peadng5i31[3];
extern const uint8_t gm_sn2gg3b98t635[21];
extern const uint8_t gm_sn790bmojc3gl[33];
extern const uint8_t gm_sn7k3frnltojm[8];
extern const uint8_t gm_sn7tl9jolqoj1[3];
extern const uint8_t gm_sn8alsh9na9ne[12];
extern const uint8_t gm_snenkhfev166s[3];
extern const uint8_t gm_snf16gr70fhka[11];
extern const uint8_t gm_snf9ccc1lqjni[5];
extern const uint8_t gm_snh5ve0osdp08[7];
extern const uint8_t gm_snikjq3vkoue5[14];
extern const uint8_t gm_snivf25e5fme8[6];
extern const uint8_t gm_snja4gioias3h[19];
extern const uint8_t gm_snmep73bl34j8[7];
extern const uint8_t gm_snmm68ee1rq48[41];
extern const uint8_t gm_snp0oqa4m79hk[6];
extern const uint8_t gm_snps96dj7ecq0[17];
extern const uint8_t gm_sns75reqq32d2[4];
extern const uint8_t gm_snu49maifa670[41];
extern const uint8_t gm_so2sjlodq81qn[13];
extern const uint8_t gm_so5c2bcepn6c6[15];
extern const uint8_t gm_so5l2ibq9ia3i[65];
extern const uint8_t gm_so6e0kl9gkq21[46];
extern const uint8_t gm_so7v1cppe0bu8[41];
extern const uint8_t gm_so9fi6r28770a[3];
extern const uint8_t gm_soa9mnl4ahjud[8];
extern const uint8_t gm_soaukr1udufqk[19];
extern const uint8_t gm_sobspv56mhf83[15];
extern const uint8_t gm_sodq0gu838ini[139];
extern const uint8_t gm_sog9gpimub31i[7];
extern const uint8_t gm_sojts6dcvra33[35];
extern const uint8_t gm_somon4mdq0gpj[24];
extern const uint8_t gm_son0j0k0t10ro[8];
extern const uint8_t gm_soo5q2bffam33[33];
extern const uint8_t gm_soqt3jnmovq5l[4];
extern const uint8_t gm_sovv48653dnju[40];
extern const uint8_t gm_sp0iqkuu2lvpl[14];
extern const uint8_t gm_sp4acsbhsp3do[35];
extern const uint8_t gm_sp51dovg85ku9[40];
extern const uint8_t gm_sp598n426d61b[40];
extern const uint8_t gm_sp8as5hphue34[3];
extern const uint8_t gm_spdm0kqdda6co[25];
extern const uint8_t gm_spdpobhso0dbc[10];
extern const uint8_t gm_speojrsjsem8a[50];
extern const uint8_t gm_spf5krhuq011v[11];
extern const uint8_t gm_spgnoteg4vcil[11];
extern const uint8_t gm_sphl918t1t6qp[11];
extern const uint8_t gm_spj0hqsg585et[14];
extern const uint8_t gm_spkcu8lv7g8jc[5];
extern const uint8_t gm_spni6j7splqto[8];
extern const uint8_t gm_spu4ur06pm76g[17];
extern const uint8_t gm_sq0h7jjc6dtk5[4];
extern const uint8_t gm_sq7ohh8h77bn7[26];
extern const uint8_t gm_sqfe8a5cd6b60[7];
extern const uint8_t gm_sqh0568fl8oao[14];
extern const uint8_t gm_sqhe7365v82gi[16];
extern const uint8_t gm_sqj99shb29i55[11];
extern const uint8_t gm_sqkk84ckt4rm6[46];
extern const uint8_t gm_sqlk0h0kae56e[8];
extern const uint8_t gm_sqmr87iegdcns[7];
extern const uint8_t gm_sqn4031c223rk[13];
extern const uint8_t gm_sqqfe3o1gh1ab[55];
extern const uint8_t gm_sqtojn396s09d[11];
extern const uint8_t gm_squ7ob8ltdkn4[20];
extern const uint8_t gm_sqvb27hiolbh8[11];
extern const uint8_t gm_sr0u787bpqkqg[28];
extern const uint8_t gm_sr2ptnspelf8s[98];
extern const uint8_t gm_sr5j1k0g236qh[68];
extern const uint8_t gm_sr7ga013iei9a[4];
extern const uint8_t gm_sr9i7j9bpmaec[11];
extern const uint8_t gm_sragmmlrm20js[3];
extern const uint8_t gm_srb0ps8mir4li[9];
extern const uint8_t gm_srbp7aar5n2sa[14];
extern const char gm_src0dtioet3p4f6[];
extern const char gm_src27ouot20tc5m[];
extern const char gm_src4rbevsr9gjng[];
extern const char gm_src5eu5l4a7gv6s[];
extern const char gm_src9pvgnpibl2nm[];
extern const char gm_srcb4m5qm2daehm[];
extern const char gm_srcfk7rg8t5apoh[];
extern const char gm_srck5orfpjti60c[];
extern const char gm_srcl8pn2pjtmj6k[];
extern const char gm_srcm8m4ndqum1bp[];
extern const char gm_srcmhupam4se3h5[];
extern const char gm_srcph0vopkouaai[];
extern const char gm_srcuj9anpi7p1mq[];
extern const char gm_srcvmmksi0829pa[];
extern const uint8_t gm_sreie3avjjb9l[64];
extern const uint8_t gm_srhtnnu7r6ead[20];
extern const uint8_t gm_sri9mnfjmc5ma[11];
extern const uint8_t gm_srkvn420e4huu[26];
extern const uint8_t gm_srm32inqe4h6s[10];
extern const uint8_t gm_srrkjtivok8h4[9];
extern const uint8_t gm_srs1vrn2uaams[2];
extern const uint8_t gm_srtlfdmpif4qk[9];
extern const uint8_t gm_srv5rbhjb8ns5[15];
extern const uint8_t gm_ss41pl5vtg7un[72];
extern const uint8_t gm_ss6dev5u7tl99[12];
extern const uint8_t gm_ss7iv2kssvf80[7];
extern const uint8_t gm_ss9vj0o06n3e8[8];
extern const uint8_t gm_ssa0pabeurppq[20];
extern const uint8_t gm_ssd7gce6jed13[7];
extern const uint8_t gm_sse9dgkusbtnf[5];
extern const uint8_t gm_ssgeom7733jik[5];
extern const uint8_t gm_sshop8kf5qfhm[63];
extern const uint8_t gm_ssj1mfqvo884a[62];
extern const uint8_t gm_ssr6vk8erbj18[13];
extern const uint8_t gm_sss60if2anvc8[5];
extern const uint8_t gm_sss94v5r4qh46[13];
extern const uint8_t gm_sst2khrvr4nus[10];
extern const uint8_t gm_st1bq6djjin2k[17];
extern const uint8_t gm_st2ertk6tjva7[14];
extern const uint8_t gm_st3m5olipe6ov[17];
extern const uint8_t gm_st3mnofi7dhrs[19];
extern const uint8_t gm_st4gkaq24fvj9[32];
extern const uint8_t gm_st78509mboaa4[22];
extern const uint8_t gm_statjrbfbnhar[11];
extern const uint8_t gm_stm36rdb2vbg4[13];
extern const uint8_t gm_stobu0djed6ck[10];
extern const uint8_t gm_stou2bl2ross2[70];
extern const uint8_t gm_stqvtvp1hc371[12];
extern const uint8_t gm_strcbt1c3upss[4];
extern const uint8_t gm_stsvc1ssgs83i[62];
extern const uint8_t gm_sttasaa2bp05i[22];
extern const uint8_t gm_su2imns6t7mh5[41];
extern const uint8_t gm_su37g0462a9t0[41];
extern const uint8_t gm_su50gais1htu0[38];
extern const uint8_t gm_su5bsj1nioctr[22];
extern const uint8_t gm_su66bdcms0hot[28];
extern const uint8_t gm_su6ol10q7la3d[7];
extern const uint8_t gm_su8mkvs9pi0v1[18];
extern const uint8_t gm_su9fl4d8e88sc[49];
extern const uint8_t gm_sujs43rrijg57[102];
extern const uint8_t gm_sukriak12588g[5];
extern const uint8_t gm_sunidvof1bsgr[33];
extern const uint8_t gm_sup5ecv5jiqbn[119];
extern const uint8_t gm_sup9bfr0np428[9];
extern const uint8_t gm_surt6bemh510v[31];
extern const uint8_t gm_sv732pptj1lih[10];
extern const uint8_t gm_svaaloo7n72id[9];
extern const uint8_t gm_svk1fq3llqdta[10];
extern const uint8_t gm_svlbiokurll04[12];
extern const uint8_t gm_svv3t2si5df2q[15];
extern const gm_variant gm_tytje7cegoml_v[];
const char gm_src0dtioet3p4f6[] = "compiler/parse/expr.gm";
const char gm_src27ouot20tc5m[] = "compiler/lint/present.gm";
const char gm_src4rbevsr9gjng[] = "compiler/driver/rtcache.gm";
const char gm_src5eu5l4a7gv6s[] = "compiler/check/program.gm";
const char gm_src9pvgnpibl2nm[] = "compiler/interp/num.gm";
const char gm_srcb4m5qm2daehm[] = "compiler/check/soa.gm";
const char gm_srcfk7rg8t5apoh[] = "compiler/lint/stale.gm";
const char gm_srck5orfpjti60c[] = "compiler/types/vectors.gm";
const char gm_srcl8pn2pjtmj6k[] = "compiler/interp/builtins.gm";
const char gm_srcm8m4ndqum1bp[] = "compiler/check/bindings.gm";
const char gm_srcmhupam4se3h5[] = "compiler/sema/fnsig.gm";
const char gm_srcph0vopkouaai[] = "compiler/interp/stmt.gm";
const char gm_srcuj9anpi7p1mq[] = "compiler/cgen/program.gm";
const char gm_srcvmmksi0829pa[] = "compiler/syntax/lexer.gm";
const gm_variant gm_tytje7cegoml_v[] = {{"Read", 0, NULL}, {"ClockNow", 1, NULL}, {"ClockWall", 2, NULL}, {"Sleep", 3, NULL}, {"RandFill", 4, NULL}, {"FsOpen", 5, NULL}, {"FsClose", 6, NULL}, {"FsRead", 7, NULL}, {"FsWrite", 8, NULL}, {"FsSeek", 9, NULL}, {"FsStat", 10, NULL}, {"FsRemove", 11, NULL}, {"FsRename", 12, NULL}, {"FsMakeDir", 13, NULL}, {"FsRemoveDir", 14, NULL}, {"FsOpenDir", 15, NULL}, {"FsNextEntry", 16, NULL}, {"FsCloseDir", 17, NULL}, {"FsLstat", 18, NULL}, {"FsTruncate", 19, NULL}, {"FsSync", 20, NULL}, {"FsCwd", 21, NULL}, {"FsCanonical", 22, NULL}, {"FsReadLink", 23, NULL}, {"FsSymlink", 24, NULL}, {"FsSetMode", 25, NULL}, {"FsSetModified", 26, NULL}, {"NetConnect", 27, NULL}, {"NetListen", 28, NULL}, {"NetLocalPort", 29, NULL}, {"NetAccept", 30, NULL}, {"NetSend", 31, NULL}, {"NetRecv", 32, NULL}, {"NetClose", 33, NULL}, {"NetShutdown", 34, NULL}, {"NetSetTimeouts", 35, NULL}, {"NetPeer", 36, NULL}, {"ProcLaunch", 37, NULL}, {"ProcWriteIn", 38, NULL}, {"ProcCloseIn", 39, NULL}, {"ProcReadOut", 40, NULL}, {"ProcWait", 41, NULL}, {"ProcKill", 42, NULL}, {"ProcTryWait", 43, NULL}, {"EnvArgs", 44, NULL}, {"EnvVar", 45, NULL}, {"EnvExit", 46, NULL}, {"ThreadSpawn", 47, NULL}, {"ThreadJoin", 48, NULL}, {"ThreadDetach", 49, NULL}, {"ThreadYield", 50, NULL}, {"FutexWait", 51, NULL}, {"FutexWake", 52, NULL}, {"CpuCount", 53, NULL}, {"SysWrite", 54, NULL}};
const uint8_t gm_s00046u02oar6[41] = "\000\000\000\000\000\000\000\000\032\000\000\000\000\000\000\000Q\001\000\0005\000\000\000\000\000\000\000\000\000\000\000\017\000\000\000\000\000\000\000";
const uint8_t gm_s01ofvj3chukm[10] = " of `Env`";
const uint8_t gm_s04qus0luoga5[5] = "NULL";
const uint8_t gm_s05336tfii06a[18] = " exceeds u64::MAX";
const uint8_t gm_s078u1763sqk3[11] = ") has none";
const uint8_t gm_s07uhsrd798kj[9] = "effects ";
const uint8_t gm_s0adesbq24e9u[6] = ")) { ";
const uint8_t gm_s0cf71ouneh5m[12] = "gm_fs_close";
const uint8_t gm_s0cpkv7s4ef85[24] = "bswap of a partial byte";
const uint8_t gm_s0ggo8ek7kfrt[34] = "a constant that was not evaluated";
const uint8_t gm_s0jtk73jm5ft9[16] = "registerOptions";
const uint8_t gm_s0lrfrtvffhu7[4] = "NaN";
const uint8_t gm_s0notp50gncsl[12] = "gm_net_init";
const uint8_t gm_s0odi4lbspe7f[25] = "EOF while parsing a list";
const uint8_t gm_s0qbjm15aot6s[11] = ", (void *)";
const uint8_t gm_s0rsc8ficqfcm[78] = "cannot rename `{}` to `{}`: `{}` is already declared in a scope that overlaps";
const uint8_t gm_s0se3p14t5670[14] = "lib/gamma/std";
const uint8_t gm_s0tuij8156k40[41] = "\000\000\000\000\000\000\000\000\031\000\000\000\000\000\000\000\262\000\000\000\035\000\000\000\000\000\000\000\000\000\000\000\016\000\000\000\000\000\000\000";
const uint8_t gm_s108thc59vc7u[37] = "index {} out of bounds for length {}";
const uint8_t gm_s11lbv9jc177b[4] = "hex";
const uint8_t gm_s129prqlnb2kt[41] = "unexpected character {} while parsing {}";
const uint8_t gm_s148evlodt315[18] = ")` has no bound: ";
const uint8_t gm_s15c9j2vv1d2a[39] = "the fields of this `#bits` struct use ";
const uint8_t gm_s15p9kmrsfogp[2] = "p";
const uint8_t gm_s18dhen1d87oi[20] = "`else` on this type";
const uint8_t gm_s1ctr86inn5iu[24] = "the C compiler failed:\012";
const uint8_t gm_s1dc9boe5unhg[32] = "this `break` gives a `with` a `";
const uint8_t gm_s1gm43jp1jqr0[89] = " a;\012    GM_ST(void *, (uint8_t *)&a, (void *)(d));\012    GM_ST(uintptr_t, (uint8_t *)&a + ";
const uint8_t gm_s1jdtclvlju06[9] = "fn {} {}";
const uint8_t gm_s1leke9tn7lrm[8] = "`match`";
const uint8_t gm_s1livc5021kui[9] = "parse_if";
const uint8_t gm_s1lj31vo6lvqe[26] = "field `{}` is given twice";
const uint8_t gm_s1lnh4indnr62[4] = "{}.";
const uint8_t gm_s1rnr94j0tq8t[17] = " *)((uint8_t *)&";
const uint8_t gm_s1t5armttgi3e[41] = "\000\000\000\000\000\000\000\000\026\000\000\000\000\000\000\000\350\000\000\0006\000\000\000\000\000\000\000\000\000\000\000\003\000\000\000\000\000\000\000";
const uint8_t gm_s211k6darac91[10] = "byte_text";
const uint8_t gm_s29jv7rttsnpq[52] = ".from_tag` needs an enum (a union without payloads)";
const uint8_t gm_s2hi6cc9n75l5[7] = "gm_lad";
const uint8_t gm_s2hut6059od59[42] = "cannot infer the union type of `.{}` here";
const uint8_t gm_s2j8t1jqamft8[5] = "url+";
const uint8_t gm_s2k3mi7crs28d[5] = "goto";
const uint8_t gm_s2nf76g2su2k3[15] = "builtin method";
const uint8_t gm_s2nn3moc8tfri[17] = "    gm_start();\012";
const uint8_t gm_s2nq2i5ut1dni[13] = "fmt_shortest";
const uint8_t gm_s2of8ntot1on0[81] = "a function with a rest parameter takes a spread only as its whole rest arguments";
const uint8_t gm_s2tnbv3fg2r22[13] = " tg = GM_LD(";
const uint8_t gm_s2u46tincrqmv[25] = "compiler/lsp/analysis.gm";
const uint8_t gm_s2u7e0bju5qb8[28] = " = __builtin_shufflevector(";
const uint8_t gm_s336v1oga58d4[41] = "`{}` does not apply to complex constants";
const uint8_t gm_s33pl04oikskr[5] = "<<|=";
const uint8_t gm_s3c7ikv4cpqg1[13] = "Slice { lo: ";
const uint8_t gm_s3jgqb0t6i7b8[22] = "operator `{}` on `{}`";
const uint8_t gm_s3lovgpoph0mh[9] = "prefetch";
const uint8_t gm_s3on77ql0ndko[17] = "a float, for `e`";
const uint8_t gm_s3sb6rrf98gpi[20] = " is not valid UTF-8";
const uint8_t gm_s3v312rvfthpf[43] = "radix prefixes must be lowercase; write `0";
const uint8_t gm_s44ek1gecbajs[7] = "void *";
const uint8_t gm_s4769el4glhs1[4] = "end";
const uint8_t gm_s487q6bd4pfu3[50] = "`cast` converts between pointer types, not from `";
const uint8_t gm_s498ag88l373e[14] = "gm_table_take";
const uint8_t gm_s4a4j4epvkeso[11] = "nfields = ";
const uint8_t gm_s4b8airp3rh69[50] = "rest parameters and defaults of function literals";
const uint8_t gm_s4chbmodjkinv[7] = "--help";
const uint8_t gm_s4emnk8r9sirq[11] = "gm_x_{}_{}";
const uint8_t gm_s4gilalcmdvu3[8] = "NetRecv";
const uint8_t gm_s4ifh5kc7nc0d[62] = "` goes directly before a `struct` or `union` literal (\302\24720.2)";
const uint8_t gm_s4jv43jfqhfjq[41] = "\000\000\000\000\000\000\000\000\030\000\000\000\000\000\000\000\337\000\000\000\032\000\000\000\000\000\000\000\000\000\000\000\016\000\000\000\000\000\000\000";
const uint8_t gm_s4kt3mhlp0or9[11] = "formatting";
const uint8_t gm_s4nj79mj9jk57[21] = "compiler/sema/soa.gm";
const uint8_t gm_s4o70d89183c2[4] = "add";
const uint8_t gm_s4ou57tmjn6c7[39] = "bool evok = gm_env_var(GM_LD(void *, &";
const uint8_t gm_s4r2qm241merr[12] = "const {}@{}";
const uint8_t gm_s519l85f26pmp[81] = "static void {}(gm_node *cx, {} a, const char *file, uint32_t line, uint32_t col)";
const uint8_t gm_s52n7ji3kulf2[29] = "` requires integer constants";
const uint8_t gm_s53c18pcajigs[51] = "`: integer types are at most 128 bits wide (\302\2475.2)";
const uint8_t gm_s55edm0in8jpg[19] = "connection refused";
const uint8_t gm_s588gr73ph5f5[45] = "internal error: an instruction needs a frame";
const uint8_t gm_s58b2tdsrfkg1[85] = "`{}` would have two methods named `{}`: a method call must find exactly one (\302\2477.10)";
const uint8_t gm_s59a022mrijic[9] = "net_recv";
const uint8_t gm_s5gbgbdradbg1[25] = "bigint: division by zero";
const uint8_t gm_s5i05gtdgdefb[42] = "slice bounds {}..{} invalid for length {}";
const uint8_t gm_s5ivi0sts5foi[9] = "_v[] = {";
const uint8_t gm_s5n5e484glelv[5] = "Left";
const uint8_t gm_s5q33up034m64[37] = "the template writes value {}, but {}";
const uint8_t gm_s5r5k5b2k7bru[20] = "/share/wasi-sysroot";
const uint8_t gm_s5ucn4crtdrq8[11] = "gm_int_dec";
const uint8_t gm_s632oftf9nssu[43] = "internal error: control flow left a thread";
const uint8_t gm_s63o5blu3j46d[16] = "__builtin_sqrtf";
const uint8_t gm_s65rai75evpi7[14] = "always_inline";
const uint8_t gm_s66fk044n1n0k[9] = "from_tag";
const uint8_t gm_s69kv55uiaqoj[10] = "--timeout";
const uint8_t gm_s6d960pgtv3l9[26] = "`main` must be a function";
const uint8_t gm_s6db4pm3k5olv[3] = "&B";
const uint8_t gm_s6dh2ca0ubali[7] = "size_t";
const uint8_t gm_s6jld2vf184ip[28] = "constant `@asm` constraints";
const uint8_t gm_s6khfdbr5aof9[34] = "no handler installed for effect `";
const uint8_t gm_s6rivmf4qclvb[71] = "cannot rename `{}` to `{}`: `{}` is already declared in the same scope";
const uint8_t gm_s70dhiaqijjg0[24] = "Operation not permitted";
const uint8_t gm_s721l5hj93rdl[50] = "a placeholder's value number has no leading zeros";
const uint8_t gm_s74v1q63ilf9i[122] = "static _Noreturn void gm_trap_hook_call(const uint8_t *msg, size_t len, const char *file, uint32_t line, uint32_t col) {\012";
const uint8_t gm_s76ovr591an92[4] = "_c(";
const uint8_t gm_s7826b3jmrqb0[10] = "_Alignas(";
const uint8_t gm_s794tb9qjlq1h[42] = " (a literal at `{}` needs `where {}({})`)";
const uint8_t gm_s7b0d890pj8dv[14] = "gm_fiber_cont";
const uint8_t gm_s7b7j2m1f6fle[71] = "this operation never returns to its caller: its result type is `never`";
const uint8_t gm_s7bj0jma5l2mg[39] = "gamma: cannot start the reader thread\012";
const uint8_t gm_s7euumoo24h31[3] = "]\012";
const uint8_t gm_s7f47bs0i7q74[21] = "Connection timed out";
const uint8_t gm_s7ipvt5onv6ht[4] = "gm_";
const uint8_t gm_s7pnhdrn5iusl[53] = "expected a handler, but no handler is declared for `";
const uint8_t gm_s7rl9plavbrfo[17] = "missing argument";
const uint8_t gm_s7s8viq00e4g2[15] = "expected value";
const uint8_t gm_s7ti0q30v2iq7[8] = "gm_atag";
const uint8_t gm_s81trlqpvp2do[11] = "_overflow(";
const uint8_t gm_s85m9c4pt0ikp[6] = " len;";
const uint8_t gm_s872048h9ttis[26] = "` is given more than once";
const uint8_t gm_s873vbpojuh8v[5] = "Mul(";
const uint8_t gm_s87isqt5t284q[9] = "ull);\012}\012";
const uint8_t gm_s894fmp947sfb[22] = "((unsigned __int128)(";
const uint8_t gm_s8952afsouaih[21] = "local foreign blocks";
const uint8_t gm_s8c43u4dovbrb[17] = "\000\000\000\000\000\000\000\000 \000\000\000\000\000\000\000";
const uint8_t gm_s8e4n8lgtb0fh[41] = "\000\000\000\000\000\000\000\000\033\000\000\000\000\000\000\000:\001\000\000N\000\000\000\000\000\000\000\000\000\000\000\020\000\000\000\000\000\000\000";
const uint8_t gm_s8m850dccae8f[88] = "`: a path starts with `./`, `../`, `std/`, or a dependency listed in gamma.toml (\302\2473.2)";
const uint8_t gm_s8olahjn69at6[21] = "({})(({})0 - ({}){})";
const uint8_t gm_s8oln6mi5b5qr[46] = "`@black_box` takes one value: `@black_box(x)`";
const uint8_t gm_s8pt0g4bme9og[30] = "the `git` URL of dependency `";
const uint8_t gm_s8qn6r2vquv2d[24] = "to begin the union body";
const uint8_t gm_s8srbeac8og16[29] = "replace the `if` with `{}{}`";
const uint8_t gm_s8u1980tbab9f[15] = "` is not Fil-C";
const uint8_t gm_s8vhc5kn5i8tv[38] = "a field name or tuple index after `.`";
const uint8_t gm_s8vthjuk0el8j[63] = "`.to(F)` converts numeric lanes to `f32` or `f64` lanes, not `";
const uint8_t gm_s927f9s6klgc5[4] = "F.l";
const uint8_t gm_s966nqk7bt67q[19] = " does not fit in `";
const uint8_t gm_s988kcbq5r7tp[14] = "call perform ";
const uint8_t gm_s99fe9sgbdt37[14] = "gm_find_outer";
const uint8_t gm_s9k2jkfctp1g5[72] = "`@hash` is defined only for types with `is`, and `{}` has none (\302\24716.8)";
const uint8_t gm_s9oqmmiu8qcsh[4] = "u16";
const uint8_t gm_s9pcjvamrhbn7[25] = "error: unknown command `";
const uint8_t gm_s9po3krkcue72[4] = "l{}";
const uint8_t gm_s9ug0q3248l8e[44] = "print how long each test took and the total";
const uint8_t gm_sa132smnv97ed[4] = " / ";
const uint8_t gm_sa16a135q4am6[13] = ", .result = ";
const uint8_t gm_sa23fj423hlk4[6] = "touch";
const uint8_t gm_sa7fm83sqt8aa[16] = "` is deprecated";
const uint8_t gm_sa7gjgqof9s56[44] = "a function type cannot have type parameters";
const uint8_t gm_sa831sfid0hf3[2] = "~";
const uint8_t gm_sa98jv6l3j8el[6] = "--fix";
const uint8_t gm_sa9ehjurfkhlj[17] = " __attribute__((";
const uint8_t gm_sabcps3pq8iv7[15] = "gm_heap_alloc(";
const uint8_t gm_sag4ku07t2k8t[25] = "` has no element named `";
const uint8_t gm_sakaoktvp5alh[52] = "calls of functions whose bodies are not checked yet";
const uint8_t gm_samqd3pmlgqmg[14] = "tag_signed = ";
const uint8_t gm_sankagtb3ecgn[58] = "use only one of `--tag`, `--branch`, `--rev`, `--version`";
const uint8_t gm_sao9h06bhkdja[26] = "->cx = inst->fw->body_cx;";
const uint8_t gm_saturamfhig5d[45] = "__atomic_thread_fence(gm_ord[GM_LD(uint8_t, ";
const uint8_t gm_saufuk3rs2i2m[15] = "foreign-global";
const uint8_t gm_savnf0pcaokd0[24] = "a vector holds at most ";
const uint8_t gm_sb2sr4t6stbn1[16] = "gm_pipe_cloexec";
const uint8_t gm_sb3o36rmuvj28[56] = " levels deep, the limit of this implementation (\302\24723.5)";
const uint8_t gm_sb8jvk2vbau9p[25] = "this compile-time result";
const uint8_t gm_sba0715plrn4t[22] = "the function declares";
const uint8_t gm_sbabklk1lp75f[30] = "    for (uint64_t i = 0; i < ";
const uint8_t gm_sbbre965bpo5f[77] = "`{}` applies only to `#bits` structs whose fields are all `bool` or integers";
const uint8_t gm_sbcpd47bv0ru9[63] = "` is not a valid package name; use `--name` to choose another\012";
const uint8_t gm_sbg03hl2obk72[10] = "{}:{}: {}";
const uint8_t gm_sbg28ee7l5gjs[6] = "core/";
const uint8_t gm_sbgdbpkf33nsc[30] = " cannot be compared with `is`";
const uint8_t gm_sbhq27eoeitmb[24] = "for (size_t i = 0; i < ";
const uint8_t gm_sblodmj4vs4nd[24] = "a core library function";
const uint8_t gm_sbmedt8vmt5ve[5] = "gm_w";
const uint8_t gm_sbnms71r2jqr5[7] = "LaneLe";
const uint8_t gm_sbp7rkf7ng8nb[33] = "` would have two methods named `";
const uint8_t gm_sc149ns1jm5on[9] = "lib{}.{}";
const uint8_t gm_sc73gru3hpv0i[8] = "} else ";
const uint8_t gm_sc7bkbe71it8c[10] = "continue ";
const uint8_t gm_sc8spe93fl51n[45] = "also print the output of the tests that pass";
const uint8_t gm_scavc1uf2hs8l[7] = "Cover(";
const uint8_t gm_scc8p0a9eachg[4] = "for";
const uint8_t gm_scch7vqldh7aj[13] = "the integer ";
const uint8_t gm_scd7cqpmpn0p9[78] = "this slice of a temporary array is returned, but the array ends with the call";
const uint8_t gm_sci406m9tkvjp[32] = "    return gm_unw.kind != 0;\012}\012";
const uint8_t gm_scjfd9b9vg99c[12] = " (os error ";
const uint8_t gm_scongfv16i8kh[21] = "slicing this pointer";
const uint8_t gm_scstobchrdgiu[67] = "` is also a name of the compiled program's runtime; choose another";
const uint8_t gm_sctlci97unb16[36] = "conflicting requirements for {}: {}";
const uint8_t gm_scuaf7ud4smlt[123] = "an element of a `#soa` has no address, since its fields are stored apart (\302\2475.6.4); take the address of a field: `&a[i].f`";
const uint8_t gm_sd1uj6gf5v8qh[8] = "inline ";
const uint8_t gm_sd80su3vlvhjt[27] = "documentFormattingProvider";
const uint8_t gm_sd98uehpeed1c[11] = "gm_fs_stat";
const uint8_t gm_sd9r0l17bbuoe[5] = ": {}";
const uint8_t gm_sdark9dtsgt1q[41] = "the package contains the symbolic link `";
const uint8_t gm_sdbo9gngsfqdj[8] = "gm_type";
const uint8_t gm_sdckdu8fe6iog[7] = "label ";
const uint8_t gm_sdes3rh8ctb7v[21] = "#define GM_RUN_MAIN ";
const uint8_t gm_sdgcpos3mblup[37] = "no handler installed for effect `{}`";
const uint8_t gm_sdl8mgtvebfcs[3] = "on";
const uint8_t gm_sdpkj0pb6qtc0[33] = "the builtin `{}` at compile time";
const uint8_t gm_sdpl6uk3q4cch[6] = "\" in ";
const uint8_t gm_sdstli6hlsa1e[7] = "{}; {}";
const uint8_t gm_se39cci44d9rt[35] = "     Initialized a git repository\012";
const uint8_t gm_seb50ruktgoun[14] = "syntax_errors";
const uint8_t gm_sebidnsgqg13d[45] = "package `{}`: `rev` must be a full commit id";
const uint8_t gm_seh12nehvgmhj[12] = "checksum = ";
const uint8_t gm_sehko5bb96s6p[5] = "true";
const uint8_t gm_sehokd57s045g[11] = "gamma-run-";
const uint8_t gm_seleu2adn5qnd[18] = "` at compile time";
const uint8_t gm_selmorb0539e5[5] = "(m, ";
const uint8_t gm_sen05m4ui88rt[58] = "internal error: a #bits value of a type that is not #bits";
const uint8_t gm_seoeuc6nik756[40] = "a tag value must be an integer constant";
const uint8_t gm_seoiboio4n733[5] = "\003\000\000\000";
const uint8_t gm_ses9qtp6kdd22[8] = "default";
const uint8_t gm_setftr5leknrn[23] = "unmatched closing `{}`";
const uint8_t gm_sf0p9o7g7fr6l[7] = "[] = {";
const uint8_t gm_sf18e6o1dss49[6] = "; };\012";
const uint8_t gm_sf7ldnukv79cp[24] = "write it: `{}({}, \342\200\246)`";
const uint8_t gm_sfa86erpod86s[4] = "nan";
const uint8_t gm_sfc9avb7sceba[21] = "key must be a string";
const uint8_t gm_sfltedqje18on[9] = "({} < 0)";
const uint8_t gm_sfn5aos9cps1h[13] = "{}return {};";
const uint8_t gm_sfp78bn17ei97[60] = "`---` requires a plain-data type, but `{}` is not (\302\2474.4){}";
const uint8_t gm_sfpg6jsb3cfp6[23] = "compiler/lsp/server.gm";
const uint8_t gm_sfss9c30cdmun[8] = "error: ";
const uint8_t gm_sfvc4ovr5hl6h[23] = "`@{}` takes one `bool`";
const uint8_t gm_sg47f6qrh6jrs[32] = "write it by name: `{}({}: \342\200\246)`";
const uint8_t gm_sg5h7btla5ksa[29] = "` is assigned but never read";
const uint8_t gm_sgdrhdkbtd5gr[19] = ", .env = (void *)&";
const uint8_t gm_sgfmudgd82uhr[24] = "this kind of expression";
const uint8_t gm_sgga2ijjm9cqd[7] = "gm_ord";
const uint8_t gm_sgib96mf5meqv[11] = "gamma.lock";
const uint8_t gm_sgj6i64633q0l[11] = "gm_fs{}_{}";
const uint8_t gm_sglu5epjv38rl[92] = "` has a calling convention other than C, so it cannot be a second-class `fn` value (\302\24719.2)";
const uint8_t gm_sgmttpp6cmh41[14] = "gm_trap_index";
const uint8_t gm_sgqsd7lfikrak[41] = "\000\000\000\000\000\000\000\000\030\000\000\000\000\000\000\000u\001\000\000\035\000\000\000\000\000\000\000\000\000\000\000\016\000\000\000\000\000\000\000";
const uint8_t gm_sgurqv8kb2rs7[32] = "the receiver and argument of `.";
const uint8_t gm_sh1to1i0u67lu[9] = "lanes_lt";
const uint8_t gm_sh2urbm3ndjnn[31] = "cannot index a value of type `";
const uint8_t gm_sh4sp5o4s4bj1[9] = "generic ";
const uint8_t gm_sh5rvkuevnqlk[7] = "gm_ibp";
const uint8_t gm_sha7302s2fme2[18] = "GIT_INDEX_FILE={}";
const uint8_t gm_shbfu7hj4uhkt[16] = "` needs a value";
const uint8_t gm_shdcf0n4l1d3a[41] = "\000\000\000\000\000\000\000\000\031\000\000\000\000\000\000\000\332\001\000\000\026\000\000\000\000\000\000\000\000\000\000\000\022\000\000\000\000\000\000\000";
const uint8_t gm_shde4bsckk9mt[4] = " | ";
const uint8_t gm_she92i1u1a4v5[30] = " (there are no truthy values)";
const uint8_t gm_shgptuodlhrcq[61] = "# This file is generated by `gamma`. Do not edit it by hand.";
const uint8_t gm_shkq4botdoed7[6] = "First";
const uint8_t gm_shlk3g0tk7m4f[31] = "__attribute__((noreturn)) void";
const uint8_t gm_shnmjdmsj431f[17] = "`-` on this type";
const uint8_t gm_sho5nuaja1vvf[5] = "self";
const uint8_t gm_shq89iblo3j6k[51] = "this pattern has {} elements, but the value has {}";
const uint8_t gm_shrtuk3jka1au[16] = "    memcpy(gm_g";
const uint8_t gm_shsifme8hcenm[17] = "cannot download ";
const uint8_t gm_shvak64vhokft[26] = "#define GM_RT_SEPARATE 1\012";
const uint8_t gm_si0b5q45ot754[101] = "` is a `#c_union`: it has no tag, so it cannot be matched; read one of its fields instead (\302\2475.11.1)";
const uint8_t gm_si0mnqg6j1eu8[19] = "__atomic_fetch_add";
const uint8_t gm_si23lpqv3htdl[27] = " (a `#c_union` has no tag)";
const uint8_t gm_sibmp9mck7osv[13] = ".len != 0) {";
const uint8_t gm_sigac1ehmei1f[69] = "` names a type; a local type may not have the name of a type (\302\2474.6)";
const uint8_t gm_siiqktevj2s2h[25] = "gm_fmin(gm_fmax((double)";
const uint8_t gm_sij2r6jhpquej[12] = "LaneConvert";
const uint8_t gm_sikspprj7od7l[43] = "destructuring needs a tuple, but this is `";
const uint8_t gm_sio8t6i3tf974[11] = "AtomicRmw(";
const uint8_t gm_siot2vlveunn8[18] = "{} run, {} failed";
const uint8_t gm_sirg36m1q4cjm[55] = "cannot rename `{}`: a use at {}:{} is not a plain name";
const uint8_t gm_sisb3n56u9bmt[15] = "gm_fs_set_mode";
const uint8_t gm_siselrhsqskvi[6] = "<li>\012";
const uint8_t gm_sisp52otedii5[23] = "-endian machine, but `";
const uint8_t gm_sithisfj5qgfv[67] = "unquoted keys cannot be empty, expected letters, numbers, `-`, `_`";
const uint8_t gm_siv3d9he09v3r[44] = "`) is unused; write `_ = \342\200\246` to discard it";
const uint8_t gm_sivuq9c7rjrh5[16] = "--deny-warnings";
const uint8_t gm_sj2tf1ift8sga[21] = "the directory name `";
const uint8_t gm_sj36eemfvep3a[56] = "` is a built-in method of slices and strings: write `s.";
const uint8_t gm_sj486prdcn2go[14] = "the match arm";
const uint8_t gm_sj7i3vhsq2ifl[38] = "a compound assignment takes one place";
const uint8_t gm_sjdn5gs7lu39i[57] = "a tuple type must be either fully named or fully unnamed";
const uint8_t gm_sjjc564tiep5s[13] = "else-capture";
const uint8_t gm_sjk23rfp9022d[14] = ", precision: ";
const uint8_t gm_sjnv3p8t49clu[9] = "` needs ";
const uint8_t gm_sjqs7q3ostqbm[4] = "pub";
const uint8_t gm_sju4s3ndpnsge[107] = "the `else` of a pattern binding must leave the block: `return`, `break`, `continue` or `trap(\342\200\246)` (\302\2474.5)";
const uint8_t gm_sk0h761p8pjoe[43] = "` needs an array variable, not a temporary";
const uint8_t gm_sk5qo3et4heam[40] = "list the rules and their default levels";
const uint8_t gm_sk88l68smd6kt[23] = "gamma: no input file\012\012";
const uint8_t gm_skat03ddcievj[39] = "`!` unwraps a result, but this is `{}`";
const uint8_t gm_skbmmrtbskpao[8] = "-static";
const uint8_t gm_skem4050p6ec1[13] = "#define gm_g";
const uint8_t gm_skkqaektdhkn1[29] = "` leaves the package (\302\2473.2)";
const uint8_t gm_skmg81d57fq1r[31] = "element `{}` is supplied twice";
const uint8_t gm_skmsgkj4jeo7n[6] = "ccx{}";
const uint8_t gm_skpsvuvlfj0ep[68] = "the array length `{}` overflows or divides by zero with {} (\302\24716.5)";
const uint8_t gm_skq2b15b87en9[63] = "cannot use the path {}: No such file or directory (os error 2)";
const uint8_t gm_skqf89fdi6p8e[12] = "u << 64) | ";
const uint8_t gm_skqko8fn9asq3[97] = "`--backend=cranelift` is not supported: gamma has the C back end and, for `run`, the interpreter";
const uint8_t gm_skr2kn3pd8es7[11] = "extern {};";
const uint8_t gm_skvqkn3gte5mk[11] = "variant {}";
const uint8_t gm_sl439hggqojfk[15] = "pack_rest_args";
const uint8_t gm_sl69n28ucapfg[21] = "((void *)(uintptr_t)";
const uint8_t gm_sl7p035rs001b[27] = "+refs/heads/*:refs/heads/*";
const uint8_t gm_sl8of2tnl835f[6] = "gm_pf";
const uint8_t gm_sla366gd7nphd[58] = "this points into a field of a `#packed` struct, where a `";
const uint8_t gm_slbqo2q5mg776[107] = "when a member of an inline tuple of `in` is a pattern, every member must be a pattern or a literal (\302\2477.7)";
const uint8_t gm_sll3452l5oa19[6] = "imag ";
const uint8_t gm_slnfks4sl1aoc[8] = ") goto ";
const uint8_t gm_slpo22sjs83b1[5] = "when";
const uint8_t gm_slrtg04n254p4[19] = " (already exists)\012";
const uint8_t gm_sm3pe8sptk65k[24] = "` does not convert to `";
const uint8_t gm_sm8t3g6c72ol4[62] = "the parameter `{}` must appear in the type the handler is for";
const uint8_t gm_smc854k364mj6[21] = " is a `usize`, but `";
const uint8_t gm_smdv8hgj9kr3n[21] = "GIT_OBJECT_DIRECTORY";
const uint8_t gm_smf5jnr5ps1bl[5] = "gm_b";
const uint8_t gm_smohsnbvmf059[10] = "heap_free";
const uint8_t gm_smptjqchq7bt0[47] = "((gm_closure){{(void *){}, (void *)&F.h, {}}})";
const uint8_t gm_smu48daue2qai[6] = "using";
const uint8_t gm_sn0peadng5i31[3] = "-g";
const uint8_t gm_sn2gg3b98t635[21] = "gm_bt_top = B.prev; ";
const uint8_t gm_sn790bmojc3gl[33] = "`{}` has no parameter named `{}`";
const uint8_t gm_sn7k3frnltojm[8] = "`@hash`";
const uint8_t gm_sn7tl9jolqoj1[3] = ".*";
const uint8_t gm_sn8alsh9na9ne[12] = "this atomic";
const uint8_t gm_snenkhfev166s[3] = "..";
const uint8_t gm_snf16gr70fhka[11] = "this guard";
const uint8_t gm_snf9ccc1lqjni[5] = "pa{}";
const uint8_t gm_snh5ve0osdp08[7] = "`@asm`";
const uint8_t gm_snikjq3vkoue5[14] = "static_assert";
const uint8_t gm_snivf25e5fme8[6] = "first";
const uint8_t gm_snja4gioias3h[19] = "` is not an effect";
const uint8_t gm_snmep73bl34j8[7] = "</li>\012";
const uint8_t gm_snmm68ee1rq48[41] = " (the existing gamma.toml is unchanged)\012";
const uint8_t gm_snp0oqa4m79hk[6] = ":load";
const uint8_t gm_snps96dj7ecq0[17] = "the archive of `";
const uint8_t gm_sns75reqq32d2[4] = "inf";
const uint8_t gm_snu49maifa670[41] = "\000\000\000\000\000\000\000\000\027\000\000\000\000\000\000\000\335\000\000\000\032\000\000\000\000\000\000\000\000\000\000\000\015\000\000\000\000\000\000\000";
const uint8_t gm_so2sjlodq81qn[13] = "NetLocalPort";
const uint8_t gm_so5c2bcepn6c6[15] = "XDG_CACHE_HOME";
const uint8_t gm_so5l2ibq9ia3i[65] = "`@variant_name` takes a value of a tagged union type, not `{}`{}";
const uint8_t gm_so6e0kl9gkq21[46] = "use only one of `--path`, `--git` and `--url`";
const uint8_t gm_so7v1cppe0bu8[41] = "\000\000\000\000\000\000\000\000\025\000\000\000\000\000\000\000\321\001\000\000\031\000\000\000\000\000\000\000\000\000\000\000\004\000\000\000\000\000\000\000";
const uint8_t gm_so9fi6r28770a[3] = "((";
const uint8_t gm_soa9mnl4ahjud[8] = "static ";
const uint8_t gm_soaukr1udufqk[19] = "` has no method `.";
const uint8_t gm_sobspv56mhf83[15] = "GIT_INDEX_FILE";
const uint8_t gm_sodq0gu838ini[139] = "`{}(x)` would be a conversion to a type parameter, which is not lossless for every type; use `x.trunc({})`, `x.sat({})` or `x.checked({})`";
const uint8_t gm_sog9gpimub31i[7] = "` -> `";
const uint8_t gm_sojts6dcvra33[35] = "` takes one value per element, so ";
const uint8_t gm_somon4mdq0gpj[24] = ", cx->env, cx->hctx, cx";
const uint8_t gm_son0j0k0t10ro[8] = "sha256:";
const uint8_t gm_soo5q2bffam33[33] = "unknown attribute `#{}` (\302\24720.2)";
const uint8_t gm_soqt3jnmovq5l[4] = "...";
const uint8_t gm_sovv48653dnju[40] = " must be parenthesized in this position";
const uint8_t gm_sp0iqkuu2lvpl[14] = "{}; otherwise";
const uint8_t gm_sp4acsbhsp3do[35] = "{} run, {} failed, {} filtered out";
const uint8_t gm_sp51dovg85ku9[40] = "empty string, expected a semver version";
const uint8_t gm_sp598n426d61b[40] = "` is already declared in the same scope";
const uint8_t gm_sp8as5hphue34[3] = "lb";
const uint8_t gm_spdm0kqdda6co[25] = "`; its dependencies are ";
const uint8_t gm_spdpobhso0dbc[10] = "GM_RESUME";
const uint8_t gm_speojrsjsem8a[50] = "internal error: formatted output does not lex: {}";
const uint8_t gm_spf5krhuq011v[11] = "`, not by ";
const uint8_t gm_spgnoteg4vcil[11] = "{}{} {}{};";
const uint8_t gm_sphl918t1t6qp[11] = "`; write `";
const uint8_t gm_spj0hqsg585et[14] = "integer lanes";
const uint8_t gm_spkcu8lv7g8jc[5] = "pn{}";
const uint8_t gm_spni6j7splqto[8] = " takes ";
const uint8_t gm_spu4ur06pm76g[17] = "\000\000\000\000\000\000\000\0001\000\000\000\000\000\000\000";
const uint8_t gm_sq0h7jjc6dtk5[4] = " lo";
const uint8_t gm_sq7ohh8h77bn7[26] = "compiler/interp/memory.gm";
const uint8_t gm_sqfe8a5cd6b60[7] = "needs ";
const uint8_t gm_sqh0568fl8oao[14] = "this coercion";
const uint8_t gm_sqhe7365v82gi[16] = "this assignment";
const uint8_t gm_sqj99shb29i55[11] = "gm_hb_used";
const uint8_t gm_sqkk84ckt4rm6[46] = "`@prefetch` takes one pointer: `@prefetch(p)`";
const uint8_t gm_sqlk0h0kae56e[8] = "NetSend";
const uint8_t gm_sqmr87iegdcns[7] = "naming";
const uint8_t gm_sqn4031c223rk[13] = "; size_t evn";
const uint8_t gm_sqqfe3o1gh1ab[55] = "foreign function `{}` cannot be called at compile time";
const uint8_t gm_sqtojn396s09d[11] = "copy_elems";
const uint8_t gm_squ7ob8ltdkn4[20] = "`git {}` failed: {}";
const uint8_t gm_sqvb27hiolbh8[11] = "soa_expand";
const uint8_t gm_sr0u787bpqkqg[28] = "`@fiber` with this function";
const uint8_t gm_sr2ptnspelf8s[98] = "dependency cycle: `{}` is constructed while the default of one of its fields is evaluated (\302\2473.4)";
const uint8_t gm_sr5j1k0g236qh[68] = "`.round` converts to an integer; for the nearest `{}` use `.to({})`";
const uint8_t gm_sr7ga013iei9a[4] = "))(";
const uint8_t gm_sr9i7j9bpmaec[11] = "` (\302\24714.6)";
const uint8_t gm_sragmmlrm20js[3] = "}`";
const uint8_t gm_srb0ps8mir4li[9] = "();\012    ";
const uint8_t gm_srbp7aar5n2sa[14] = "replace_alloc";
const uint8_t gm_sreie3avjjb9l[64] = "        gm_buf b = {0};\012        gm_buf_push(&b, \"error: \", 7);\012";
const uint8_t gm_srhtnnu7r6ead[20] = "check_import_cycles";
const uint8_t gm_sri9mnfjmc5ma[11] = "gm_threads";
const uint8_t gm_srkvn420e4huu[26] = "this is nested more than ";
const uint8_t gm_srm32inqe4h6s[10] = "  import ";
const uint8_t gm_srrkjtivok8h4[9] = " literal";
const uint8_t gm_srs1vrn2uaams[2] = "^";
const uint8_t gm_srtlfdmpif4qk[9] = "quickfix";
const uint8_t gm_srv5rbhjb8ns5[15] = "{{{}, {}, {}}}";
const uint8_t gm_ss41pl5vtg7un[72] = "    if (gm_unw.kind == GM_RESUME) { gm_unw.kind = GM_NONE; gm_with_go(&";
const uint8_t gm_ss6dev5u7tl99[12] = "ThreadYield";
const uint8_t gm_ss7iv2kssvf80[7] = "rev = ";
const uint8_t gm_ss9vj0o06n3e8[8] = "--proto";
const uint8_t gm_ssa0pabeurppq[20] = "static {}int {}({})";
const uint8_t gm_ssd7gce6jed13[7] = " -> {}";
const uint8_t gm_sse9dgkusbtnf[5] = "Last";
const uint8_t gm_ssgeom7733jik[5] = "Wrap";
const uint8_t gm_sshop8kf5qfhm[63] = "the fields of this `#bits` struct use {} bits, but `{}` has {}";
const uint8_t gm_ssj1mfqvo884a[62] = "this address of `{}` is returned, but `{}` ends with the call";
const uint8_t gm_ssr6vk8erbj18[13] = "` has type `";
const uint8_t gm_sss60if2anvc8[5] = "PATH";
const uint8_t gm_sss94v5r4qh46[13] = "gm_clock_now";
const uint8_t gm_sst2khrvr4nus[10] = "argument ";
const uint8_t gm_st1bq6djjin2k[17] = "\000\000\000\000\000\000\000\000\001\000\000\000\000\000\000\000";
const uint8_t gm_st2ertk6tjva7[14] = "` points into";
const uint8_t gm_st3m5olipe6ov[17] = "missing element ";
const uint8_t gm_st3mnofi7dhrs[19] = "includeDeclaration";
const uint8_t gm_st4gkaq24fvj9[32] = " here goes to the catch-all arm";
const uint8_t gm_st78509mboaa4[22] = "gm_bitreverse({}, {})";
const uint8_t gm_statjrbfbnhar[11] = "(({}){}LL)";
const uint8_t gm_stm36rdb2vbg4[13] = "gm_child_env";
const uint8_t gm_stobu0djed6ck[10] = " elements";
const uint8_t gm_stou2bl2ross2[70] = "this slice of a temporary array is stored where it outlives the array";
const uint8_t gm_stqvtvp1hc371[12] = "\012  package ";
const uint8_t gm_strcbt1c3upss[4] = "../";
const uint8_t gm_stsvc1ssgs83i[62] = "assigning to {} places needs a tuple of {} elements, not `{}`";
const uint8_t gm_sttasaa2bp05i[22] = "signatureHelpProvider";
const uint8_t gm_su2imns6t7mh5[41] = "cannot take the address of a #bits field";
const uint8_t gm_su37g0462a9t0[41] = "\000\000\000\000\000\000\000\000\026\000\000\000\000\000\000\0001\000\000\000\036\000\000\000\000\000\000\000\000\000\000\000\020\000\000\000\000\000\000\000";
const uint8_t gm_su50gais1htu0[38] = "` is not a C-compatible type (\302\24719.1)";
const uint8_t gm_su5bsj1nioctr[22] = "`#soa` cannot store `";
const uint8_t gm_su66bdcms0hot[28] = "static GM_TLS uint8_t gm_gi";
const uint8_t gm_su6ol10q7la3d[7] = "; use ";
const uint8_t gm_su8mkvs9pi0v1[18] = "unknown name `{}`";
const uint8_t gm_su9fl4d8e88sc[49] = "\000\000\000\000\000\000\000\000\007\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\007\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\005\000\000\000\000\000\000\000";
const uint8_t gm_sujs43rrijg57[102] = "static bool {}(void *code, void *env, gm_node *cx, const gm_type *t, const void *v, const gm_spec *s)";
const uint8_t gm_sukriak12588g[5] = "ev{}";
const uint8_t gm_sunidvof1bsgr[33] = "`return` needs a value of type `";
const uint8_t gm_sup5ecv5jiqbn[119] = "    uintptr_t m = na < nb \077 na : nb;\012    int c = m == 0 \077 0 : memcmp(pa, pb, m);\012    return c != 0 \077 c < 0 : na < nb;\012";
const uint8_t gm_sup9bfr0np428[9] = "{} in {}";
const uint8_t gm_surt6bemh510v[31] = "`{}` needs {} type argument(s)";
const uint8_t gm_sv732pptj1lih[10] = "item_json";
const uint8_t gm_svaaloo7n72id[9] = "{} = {}\012";
const uint8_t gm_svk1fq3llqdta[10] = "refs/tags";
const uint8_t gm_svlbiokurll04[12] = "parameter `";
const uint8_t gm_svv3t2si5df2q[15] = ", a) == GM_LD(";
uint8_t gm_c0lkld0prd0[18] __attribute__((aligned(1)));
uint8_t gm_c12s7ectveb[13] __attribute__((aligned(1)));
uint8_t gm_c17ch78chab[7] __attribute__((aligned(1)));
uint8_t gm_c19scujv4gq[6] __attribute__((aligned(1)));
uint8_t gm_c1f2aelscv0[8] __attribute__((aligned(1)));
uint8_t gm_c2t15afgpnn[8] __attribute__((aligned(8)));
uint8_t gm_c42ctv8im24[920] __attribute__((aligned(8)));
uint8_t gm_c4qfm55t8rc[3] __attribute__((aligned(1)));
uint8_t gm_c50ghpj4uh6[38] __attribute__((aligned(1)));
uint8_t gm_c50isunhcrj[16] __attribute__((aligned(8)));
uint8_t gm_c5glhoerm7q[21] __attribute__((aligned(1)));
uint8_t gm_c699d0m66pa[1] __attribute__((aligned(1)));
uint8_t gm_c6eh5l9ouqu[8] __attribute__((aligned(1)));
uint8_t gm_c6fgtimd3il[6] __attribute__((aligned(1)));
uint8_t gm_c82i6abqvvu[4] __attribute__((aligned(1)));
uint8_t gm_c854jn9qffs[12] __attribute__((aligned(1)));
uint8_t gm_c9nh9uke9t1[15] __attribute__((aligned(1)));
uint8_t gm_c9pef0g9fng[4] __attribute__((aligned(4)));
uint8_t gm_cbocru68uam[11] __attribute__((aligned(1)));
uint8_t gm_cd888ba1l0u[6] __attribute__((aligned(1)));
uint8_t gm_cdlh2b67kra[8] __attribute__((aligned(8)));
uint8_t gm_cduhcpt00sl[12] __attribute__((aligned(1)));
uint8_t gm_ce94e889ff7[9] __attribute__((aligned(1)));
uint8_t gm_cen8idnq6iv[6] __attribute__((aligned(1)));
uint8_t gm_cenuluelo82[3] __attribute__((aligned(1)));
uint8_t gm_cepccru8ka8[6] __attribute__((aligned(1)));
uint8_t gm_cfp6752ffka[12] __attribute__((aligned(1)));
uint8_t gm_cfrasdt0jpm[14] __attribute__((aligned(1)));
uint8_t gm_cfslp7hoejg[3] __attribute__((aligned(1)));
uint8_t gm_cg7qavecf9k[3] __attribute__((aligned(1)));
uint8_t gm_cgsatlrqc4j[3] __attribute__((aligned(1)));
uint8_t gm_chsbbntq1fu[11] __attribute__((aligned(1)));
uint8_t gm_chtdbvfshqk[10] __attribute__((aligned(1)));
uint8_t gm_ciu2mtp0ojg[10] __attribute__((aligned(1)));
uint8_t gm_civonf0vj4j[7] __attribute__((aligned(1)));
uint8_t gm_cj5c837l8v6[10] __attribute__((aligned(1)));
uint8_t gm_cjs2aavhl2o[8] __attribute__((aligned(8)));
uint8_t gm_ckbflbrril1[1] __attribute__((aligned(1)));
uint8_t gm_cl1d82mrtnv[40] __attribute__((aligned(1)));
uint8_t gm_clb2b60g1qs[13] __attribute__((aligned(1)));
uint8_t gm_clbqegpecii[16] __attribute__((aligned(1)));
uint8_t gm_cm0ptlu1qd7[25] __attribute__((aligned(1)));
uint8_t gm_cm8gajkonri[6] __attribute__((aligned(1)));
uint8_t gm_cnj3q1sfajm[12] __attribute__((aligned(1)));
uint8_t gm_cntjgiv42f7[23] __attribute__((aligned(1)));
uint8_t gm_co987o3ird4[8] __attribute__((aligned(1)));
uint8_t gm_cpkd7s1srcc[4517] __attribute__((aligned(1)));
uint8_t gm_cpma9mav80s[3] __attribute__((aligned(1)));
uint8_t gm_cpn2np70vcf[8] __attribute__((aligned(8)));
uint8_t gm_cri2dckkbh8[9] __attribute__((aligned(1)));
uint8_t gm_crpb2rekieb[1] __attribute__((aligned(1)));
uint8_t gm_csp8t1dbqmf[13] __attribute__((aligned(1)));
uint8_t gm_csvkj3mk95q[11] __attribute__((aligned(1)));
uint8_t gm_ctq6t8frtor[4] __attribute__((aligned(4)));
uint8_t gm_cu0b9l65c0v[1] __attribute__((aligned(1)));
uint8_t gm_cu6pb2rap57[3] __attribute__((aligned(1)));
uint8_t gm_cuqbj62gqhf_2[16] __attribute__((aligned(8)));
uint8_t gm_cvb3726geh8[3] __attribute__((aligned(1)));
uint8_t gm_cvrgakgvbh7[14] __attribute__((aligned(1)));
uint8_t gm_k02l1tj96tm[40] __attribute__((aligned(8)));
uint8_t gm_k0nv110r163[40] __attribute__((aligned(8)));
uint8_t gm_k48l3rluocl[40] __attribute__((aligned(8)));
uint8_t gm_k4l7ojp6msf[40] __attribute__((aligned(8)));
uint8_t gm_k7bkj4i7gae[40] __attribute__((aligned(8)));
uint8_t gm_ka73u23j5nf[40] __attribute__((aligned(8)));
uint8_t gm_kc6vr8pmeqd[40] __attribute__((aligned(8)));
uint8_t gm_km7a24is5mf[40] __attribute__((aligned(8)));
uint8_t gm_ko53fduln7u[40] __attribute__((aligned(8)));
uint8_t gm_kq6te48ps2f[40] __attribute__((aligned(8)));
uint8_t gm_ks677q1halv[40] __attribute__((aligned(8)));
uint8_t gm_kt2nhjs8rv5[40] __attribute__((aligned(8)));
uint8_t gm_ktjao6lvic2[40] __attribute__((aligned(8)));
uint8_t gm_ku5cbgje11b[40] __attribute__((aligned(8)));
uint8_t gm_kujrnb073g1[40] __attribute__((aligned(8)));
uint8_t gm_kv89j160jqp[40] __attribute__((aligned(8)));
void *const gm_hls2l2mrnqun[] = {(void *)gm_f_for_body_efhhmqv};
