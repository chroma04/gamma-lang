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
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint32_t f0; uint8_t f1; uint8_t pe[3]; } gm_t9805qtonbsag;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint8_t a[5]; } gm_timmc9i8o8nqm;
typedef struct { uint8_t f0; uint8_t p1[3]; union { int32_t v0; } f1; } gm_toe5fmr3vvf4d;
typedef struct { uint8_t a[4096]; } gm_tpbv7nqggnvqr;
typedef struct { uint8_t f0; uint8_t f1; } gm_tqs5p4n2tq19v;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { gm_b16_8 f0; uint64_t f1; uint64_t f2; uint64_t f3; gm_t3mf58gv9kf3n f4; } gm_t4c7ipjj9s3fp;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
_Static_assert(sizeof(gm_t9805qtonbsag) == 8 && _Alignof(gm_t9805qtonbsag) == 4 && offsetof(gm_t9805qtonbsag, f0) == 0 && offsetof(gm_t9805qtonbsag, f1) == 4, "layout of `?u32`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
typedef struct { uint64_t f0; gm_b16_8 f1; } gm_tf1bjg81u0glv;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_toe5fmr3vvf4d v0; uint64_t v1; } f1; } gm_thf5kr0is9u9i;
_Static_assert(sizeof(gm_timmc9i8o8nqm) == 5 && _Alignof(gm_timmc9i8o8nqm) == 1 && offsetof(gm_timmc9i8o8nqm, a) == 0, "layout of `[5]u8`");
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
_Static_assert(sizeof(gm_toe5fmr3vvf4d) == 8 && _Alignof(gm_toe5fmr3vvf4d) == 4 && offsetof(gm_toe5fmr3vvf4d, f0) == 0 && offsetof(gm_toe5fmr3vvf4d, f1) == 4, "layout of `IoError`");
_Static_assert(sizeof(gm_tpbv7nqggnvqr) == 4096 && _Alignof(gm_tpbv7nqggnvqr) == 1 && offsetof(gm_tpbv7nqggnvqr, a) == 0, "layout of `[4096]u8`");
_Static_assert(sizeof(gm_tqs5p4n2tq19v) == 2 && _Alignof(gm_tqs5p4n2tq19v) == 1 && offsetof(gm_tqs5p4n2tq19v, f0) == 0 && offsetof(gm_tqs5p4n2tq19v, f1) == 1, "layout of `(u8, u8)`");
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_toe5fmr3vvf4d v0; } f1; } gm_ttln0d0hd70en;
_Static_assert(sizeof(gm_t4c7ipjj9s3fp) == 56 && _Alignof(gm_t4c7ipjj9s3fp) == 8 && offsetof(gm_t4c7ipjj9s3fp, f0) == 0 && offsetof(gm_t4c7ipjj9s3fp, f1) == 16 && offsetof(gm_t4c7ipjj9s3fp, f2) == 24 && offsetof(gm_t4c7ipjj9s3fp, f3) == 32 && offsetof(gm_t4c7ipjj9s3fp, f4) == 40, "layout of `HashMap(TyKind, u32, null, null)`");
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
_Static_assert(sizeof(gm_tf1bjg81u0glv) == 24 && _Alignof(gm_tf1bjg81u0glv) == 8 && offsetof(gm_tf1bjg81u0glv, f0) == 0 && offsetof(gm_tf1bjg81u0glv, f1) == 8, "layout of `(i: usize, x: string)`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
_Static_assert(sizeof(gm_thf5kr0is9u9i) == 16 && _Alignof(gm_thf5kr0is9u9i) == 8 && offsetof(gm_thf5kr0is9u9i, f0) == 0 && offsetof(gm_thf5kr0is9u9i, f1) == 8, "layout of `IoError!usize`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_ttln0d0hd70en) == 12 && _Alignof(gm_ttln0d0hd70en) == 4 && offsetof(gm_ttln0d0hd70en, f0) == 0 && offsetof(gm_ttln0d0hd70en, f1) == 4, "layout of `IoError!()`");
struct gm_fr_absolute_6pqhsaj {
    gm_frame h;
    gm_tpbv7nqggnvqr l1; /* buf */
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ijtp9lo {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_jagk2mc {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ppl3r45 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_for_body_8edr2np {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_for_body_ig27ig2 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_gdb_script_section_320gurp {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* asm */
    gm_b16_8 ret;
    gm_b0_1 w0;
};
struct gm_fr_get_p217beu {
    gm_frame h;
    gm_tqs5p4n2tq19v ret;
};
struct gm_fr_guard_o7k9a0e {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* out */
    gm_b16_8 ret;
};
struct gm_fr_hash_map_rs5nlfs {
    gm_frame h;
    gm_t4c7ipjj9s3fp ret;
};
struct gm_fr_list_0c2v515 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_uvfu1k1 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_member_name_mj2bvs3 {
    gm_frame h;
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_parent_dir_df7dgp5 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_resolve_lines_h4bf30f {
    gm_frame h;
    gm_b16_8 l1; /* c_file */
    gm_trqfvmjtt4sij l2; /* out */
    uint64_t l3; /* n */
    gm_th2lqbo77pvsc l4; /* active */
    gm_b16_8 l5; /* last */
    gm_b16_8 ret;
    gm_b0_1 w0;
};
struct gm_fr_unique_members_mrojmjf {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_unique_view_members_3bvsjcm {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_unique_with_mffff7e {
    gm_frame h;
    gm_t4c7ipjj9s3fp l3; /* count */
    gm_trqfvmjtt4sij l8; /* out */
    gm_b16_8 ret;
};
struct gm_fr_write_lldb_formatters_m0rjk7q {
    gm_frame h;
    uint8_t ret;
};
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b);
__attribute__((aligned(4))) gm_b16_8 gm_f_absolute_6pqhsaj(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_c_string_n7h27b3(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_jagk2mc(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ppl3r45(void);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_extend_0cs85gs(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_find_722uua4(void * a0, void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_for_body_8edr2np(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_for_body_ig27ig2(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_format_8rfbft0(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_ckll4kg(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_gdb_script_section_320gurp(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_t9805qtonbsag gm_f_get_m1knqqk(void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_tqs5p4n2tq19v gm_f_get_p217beu(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_guard_o7k9a0e(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) void gm_f_hash_map_rs5nlfs(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_b16_8 gm_f_join_qveh5b2(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_lines_9uns0k2_sersjf1(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_lines_9uns0k2_sld76pa(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) void gm_f_list_0c2v515(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_member_name_mj2bvs3(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_normalize_bch4n2l(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_parent_dir_df7dgp5(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_parent_lk0iero(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_d0271rf(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_t83rg2m(void * a0, uint8_t a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_074448v(void * a0, gm_b16_8 a1, uint32_t a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_5c6qgui(void * a0, gm_b16_8 a1, uint8_t a2);
__attribute__((aligned(4))) gm_b16_8 gm_f_resolve_lines_h4bf30f(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) uint8_t gm_f_starts_with_6vvt01e(gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_unique_members_mrojmjf(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, void * a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_unique_view_members_3bvsjcm(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, void * a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_unique_with_mffff7e(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, void * a1, uint8_t a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_6a87mea(void * a0, const void * a1);
__attribute__((aligned(4))) gm_ttln0d0hd70en gm_f_write_all_o8ucaji(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_cdouvhd(void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) uint8_t gm_f_write_lldb_formatters_m0rjk7q(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_to8v62c(void * a0, gm_tnpifv20irnph a1);
GM_NOINLINE bool gm_pf454qfrpt6r(gm_node *cx, const void *d, uintptr_t n, const char *file, uint32_t line, uint32_t col);
GM_NOINLINE bool gm_pfu8dq5bikbh(gm_node *cx, const gm_type *d, const void *v, const char *file, uint32_t line, uint32_t col);
extern uint8_t gm_c23qivcb2qk[16] __attribute__((aligned(8)));
extern uint8_t gm_c66tv9kq8na[208] __attribute__((aligned(8)));
extern uint8_t gm_ccmgjp6th6r[16] __attribute__((aligned(8)));
extern uint8_t gm_cphj36bitbq[688] __attribute__((aligned(8)));
extern void *const gm_hlifma0pdvu5[];
extern void *const gm_hln6al4vijjv[];
extern const uint8_t gm_s1k3lfare8sei[4];
extern const uint8_t gm_s3inajboct11s[8];
extern const uint8_t gm_s588gob2um10i[2];
extern const uint8_t gm_sbqm20163frvc[5];
extern const uint8_t gm_sc6gojr5sjib4[21];
extern const uint8_t gm_scohuf4dfj32r[37];
extern const uint8_t gm_sebojpbfjfgaf[3];
extern const uint8_t gm_sf00qdbn1q8j8[9];
extern const uint8_t gm_sl3q52ep6aa53[2];
extern const uint8_t gm_snekrjln06tfl[14];
extern const uint8_t gm_so514oj5nbhcu[2];
extern const uint8_t gm_sockrjemj2if8[61];
extern const uint8_t gm_spkrf9e7cr6h4[7];
extern const uint8_t gm_sr4qbrk6tlgaa[1];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_srcbsc23is03ttm[];
extern const char gm_srce5lfiiur8udn[];
extern const char gm_srcrgc9bkquf6nr[];
extern const gm_type gm_ty0iqiubfm7e;
extern const gm_type gm_tyo86ks9qkvt;
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b) {
    uintptr_t n = GM_LD(uintptr_t, a + 8);
    if (n != GM_LD(uintptr_t, b + 8)) return false;
    const uint8_t *pa = GM_LD(void *, a), *pb = GM_LD(void *, b);
    return n == 0 || memcmp(pa, pb, n * 1) == 0;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_jagk2mc(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_jagk2mc F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ppl3r45(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_ppl3r45 F;
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
inline __attribute__((aligned(4))) gm_tqs5p4n2tq19v gm_f_get_p217beu(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_get_p217beu F;
    void * gm_l0; /* m */
    gm_b16_8 gm_l1; /* k */
    uint64_t gm_l2; /* i */
    GM_STACK_CHECK(&F, 139u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        void * t3 = (void *)((uint8_t *)&gm_l1);
        gm_tf5srh4mp4b3i t4 = gm_f_find_722uua4(t2, t3);
        gm_tf5srh4mp4b3i t5 = t4;
        uint64_t t6;
        if (((((uint8_t *)&t5))[8] != 0)) {
            uint64_t t7 = GM_LD(uint64_t, (((uint8_t *)&t5) + 0));
            t6 = t7;
        } else {
            gm_tqs5p4n2tq19v t8;
            memset(&t8, 0, sizeof t8);
            uint8_t t9 = 0;
            F.ret = t8;
            goto gm_fret;
        }
        gm_l2 = t6;
    }
    {
        uint64_t t10 = gm_l2;
        uint64_t t11 = (uintptr_t)t10;
        void * t12 = gm_l0;
        uint64_t t13 = GM_LD(uintptr_t, (((uint8_t *)t12) + 0) + 8);
        if (t11 >= (uintptr_t)(t13)) gm_trap_index(t11, t13, gm_srcbsc23is03ttm, 119, 12);
        uint8_t t14 = GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, (((uint8_t *)t12) + 0)) + t11 * 32) + 24));
        gm_tqs5p4n2tq19v t15;
        GM_ST(uint8_t, ((uint8_t *)&t15), t14);
        ((uint8_t *)&t15)[1] = 1;
        uint8_t t16 = 0;
        F.ret = t15;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_hash_map_rs5nlfs(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_hash_map_rs5nlfs F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_b16_8 t2 = gm_f_empty_slice_jagk2mc();
        gm_node *pn3 = gm_find(cx, 587034433);
        if (!pn3) gm_trap("no handler installed for effect `Alloc`", gm_srcbsc23is03ttm, 35, 96);
        gm_t3mf58gv9kf3n t4;
        t4 = ((gm_t3mf58gv9kf3n (*)(void *, gm_node *, gm_node *))pn3->clauses[0])(pn3->env, pn3->hctx, pn3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t4c7ipjj9s3fp *)gm_out) + 0, t2);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t4c7ipjj9s3fp *)gm_out) + 16, ((uint64_t)0ULL));
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t4c7ipjj9s3fp *)gm_out) + 24, ((uint64_t)0ULL));
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t4c7ipjj9s3fp *)gm_out) + 32, ((uint64_t)11400714819323198485ULL));
        GM_ST(gm_t3mf58gv9kf3n, (uint8_t *)&(*(gm_t4c7ipjj9s3fp *)gm_out) + 40, t4);
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
inline __attribute__((aligned(4))) void gm_f_list_0c2v515(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_0c2v515 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_ppl3r45();
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_unique_members_mrojmjf(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, void * a1) {
    struct gm_fr_unique_members_mrojmjf F;
    gm_b16_8 gm_l0; /* names */
    void * gm_l1; /* taken */
    GM_STACK_CHECK(&F, 96u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_b16_8 t1 = gm_l0;
    void * t2 = gm_l1;
    gm_b16_8 t3 = gm_f_unique_with_mffff7e((void *)NULL, cx, NULL, t1, t2, ((uint8_t)0));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_unique_view_members_3bvsjcm(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, void * a1) {
    struct gm_fr_unique_view_members_3bvsjcm F;
    gm_b16_8 gm_l0; /* names */
    void * gm_l1; /* taken */
    GM_STACK_CHECK(&F, 96u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_b16_8 t1 = gm_l0;
    void * t2 = gm_l1;
    gm_b16_8 t3 = gm_f_unique_with_mffff7e((void *)NULL, cx, NULL, t1, t2, ((uint8_t)1));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_absolute_6pqhsaj(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_absolute_6pqhsaj F;
    gm_b16_8 gm_l0; /* p */
    uint64_t gm_l2; /* n */
    GM_STACK_CHECK(&F, 274u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
    }
    {
        void * t2 = ((uint8_t *)&F.l1);
        if ((uintptr_t)0 > (uintptr_t)4096 || (uintptr_t)4096 > (uintptr_t)4096) gm_trap_slice((uintptr_t)0, (uintptr_t)4096, 4096, gm_srce5lfiiur8udn, 175, 17);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(((uint8_t *)t2 + (uintptr_t)0 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)((uintptr_t)4096 - (uintptr_t)0));
        gm_node *pn4 = gm_find(cx, 2126215853);
        if (!pn4) gm_trap("no handler installed for effect `Fs`", gm_srce5lfiiur8udn, 175, 10);
        gm_thf5kr0is9u9i t5;
        t5 = ((gm_thf5kr0is9u9i (*)(void *, gm_node *, gm_node *, gm_b16_8))pn4->clauses[16])(pn4->env, pn4->hctx, pn4, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_thf5kr0is9u9i t6 = t5;
        uint64_t t7;
        if (!((((uint8_t *)&t6))[0] != 0)) {
            uint64_t t8 = GM_LD(uint64_t, (((uint8_t *)&t6) + 8));
            t7 = t8;
        } else {
            gm_b16_8 t9 = gm_l0;
            uint8_t t10 = 0;
            F.ret = t9;
            goto gm_fret;
        }
        gm_l2 = t7;
    }
    {
        void * t11 = ((uint8_t *)&F.l1);
        uint64_t t12 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t13 = gm_l2;
        uint64_t t14 = (uintptr_t)t13;
        if (t12 > t14 || t14 > (uintptr_t)4096) gm_trap_slice(t12, t14, 4096, gm_srce5lfiiur8udn, 176, 41);
        gm_b16_8 t15;
        GM_ST(void *, (uint8_t *)&t15, (void *)(((uint8_t *)t11 + t12 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t15 + 8, (uintptr_t)(t14 - t12));
        gm_b16_8 t16 = t15;
        gm_b16_8 t17 = GM_LD(gm_b16_8, ((uint8_t *)&t16));
        gm_b16_8 t18 = gm_l0;
        gm_b16_8 t19 = gm_f_join_qveh5b2((void *)NULL, cx, NULL, t17, t18);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b16_8 t20 = gm_f_normalize_bch4n2l((void *)NULL, cx, NULL, t19);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t21 = 0;
        F.ret = t20;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_8edr2np(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_for_body_8edr2np F;
    gm_b16_8 gm_l0; /* <item> */
    gm_b16_8 gm_l1; /* line */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    uint8_t gm_l4; /* c */
    GM_STACK_CHECK(&F, 235u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t3 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l0));
    gm_l1 = t3;
    {
        void * t5 = (void *)((uint8_t *)&((struct gm_fr_gdb_script_section_320gurp *)((&F.h)->up))->l1);
        gm_b16_8 t6;
        GM_ST(void *, (uint8_t *)&t6, (void *)(gm_sf00qdbn1q8j8));
        GM_ST(uintptr_t, (uint8_t *)&t6 + 8, (uintptr_t)(8));
        gm_tftb1odc739t5 t7 = gm_f_extend_0cs85gs(t5, t6);
        gm_tftb1odc739t5 t8 = t7;
        if (((((uint8_t *)&t8))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t8) + 1, gm_srce5lfiiur8udn, 72, 9);
        gm_b0_1 t9 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
    }
    {
        {
            gm_b16_8 t11 = gm_l1;
            gm_l2 = t11;
        }
        {
            gm_l3 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t13 = gm_l3;
                uint64_t t14 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                uint8_t t15 = (t13 < t14);
                if (!t15) goto lb12;
                {
                    uint64_t t18 = gm_l3;
                    uint64_t t19 = (uintptr_t)t18;
                    uint64_t t20 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                    uint8_t t21 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t19 * 1));
                    uint8_t t22 = t21;
                    uint8_t t25 = GM_LD(uint8_t, ((uint8_t *)&t22));
                    gm_l4 = t25;
                    {
                        uint8_t t28 = 0;
                        uint8_t t29 = gm_l4;
                        uint8_t t30 = ((t29 == ((uint8_t)92ULL)));
                        if (!t30) {
                            uint8_t t31 = gm_l4;
                            uint8_t t32 = ((t31 == ((uint8_t)34ULL)));
                            t28 = t32;
                        } else t28 = 1;
                        if (!t28) goto ie27;
                        {
                            void * t34 = (void *)((uint8_t *)&((struct gm_fr_gdb_script_section_320gurp *)((&F.h)->up))->l1);
                            gm_tftb1odc739t5 t35 = gm_f_push_t83rg2m(t34, ((uint8_t)92ULL));
                            gm_tftb1odc739t5 t36 = t35;
                            if (((((uint8_t *)&t36))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t36) + 1, gm_srce5lfiiur8udn, 74, 40);
                            gm_b0_1 t37 = GM_LD(gm_b0_1, (((uint8_t *)&t36) + 1));
                        }
                        goto ix27;
ie27:;
ix27:;
                    }
                    {
                        void * t38 = (void *)((uint8_t *)&((struct gm_fr_gdb_script_section_320gurp *)((&F.h)->up))->l1);
                        uint8_t t39 = gm_l4;
                        gm_tftb1odc739t5 t40 = gm_f_push_t83rg2m(t38, t39);
                        gm_tftb1odc739t5 t41 = t40;
                        if (((((uint8_t *)&t41))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t41) + 1, gm_srce5lfiiur8udn, 75, 13);
                        gm_b0_1 t42 = GM_LD(gm_b0_1, (((uint8_t *)&t41) + 1));
                    }
                    goto me23;
mn24:;
                    gm_trap("no match arm matched the scrutinee", gm_srce5lfiiur8udn, 73, 9);
me23:;
                }
lc12:;
                {
                    uint64_t t44 = gm_l3;
                    uint64_t r45; bool o46 = __builtin_add_overflow(t44, ((uint64_t)1ULL), &r45);
                    uint64_t t47 = r45;
                    gm_l3 = t47;
                }
            }
lb12:;
        }
    }
    {
        void * t48 = (void *)((uint8_t *)&((struct gm_fr_gdb_script_section_320gurp *)((&F.h)->up))->l1);
        gm_b16_8 t49;
        GM_ST(void *, (uint8_t *)&t49, (void *)(gm_sbqm20163frvc));
        GM_ST(uintptr_t, (uint8_t *)&t49 + 8, (uintptr_t)(4));
        gm_tftb1odc739t5 t50 = gm_f_extend_0cs85gs(t48, t49);
        gm_tftb1odc739t5 t51 = t50;
        if (((((uint8_t *)&t51))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t51) + 1, gm_srce5lfiiur8udn, 77, 9);
        gm_b0_1 t52 = GM_LD(gm_b0_1, (((uint8_t *)&t51) + 1));
    }
    goto me1;
mn2:;
    gm_trap("no match arm matched the scrutinee", gm_srce5lfiiur8udn, 71, 5);
me1:;
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_ig27ig2(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_for_body_ig27ig2 F;
    gm_b16_8 gm_l0; /* <item> */
    gm_b16_8 gm_l1; /* line */
    gm_b16_8 gm_l2; /* d */
    GM_STACK_CHECK(&F, 627u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t3 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l0));
    gm_l1 = t3;
    {
        gm_b16_8 t6 = gm_l1;
        gm_b16_8 t7 = GM_LD(gm_b16_8, gm_ccmgjp6th6r);
        gm_b16_8 t8 = t6;
        gm_b16_8 t9 = t7;
        uint8_t t10 = (gm_eq0iqiubfm7e(((uint8_t *)&t8), ((uint8_t *)&t9)));
        if (!t10) goto ie5;
        {
            gm_th2lqbo77pvsc t12;
            memset(&t12, 0, sizeof t12);
            GM_ST(gm_th2lqbo77pvsc, ((uint8_t *)&((struct gm_fr_resolve_lines_h4bf30f *)((&F.h)->up))->l4), t12);
        }
        {
            gm_tf1bjg81u0glv t13;
            uint64_t t14 = GM_LD(uint64_t, ((uint8_t *)&((struct gm_fr_resolve_lines_h4bf30f *)((&F.h)->up))->l3));
            uint64_t r15; bool o16 = __builtin_add_overflow(t14, ((uint64_t)2ULL), &r15);
            if (o16) gm_trap("integer overflow in `+`", gm_srce5lfiiur8udn, 45, 46);
            uint64_t t17 = r15;
            GM_ST(uint64_t, (uint8_t *)&t13 + 0, t17);
            gm_b16_8 t18 = GM_LD(gm_b16_8, ((uint8_t *)&((struct gm_fr_resolve_lines_h4bf30f *)((&F.h)->up))->l1));
            gm_b16_8 t19 = gm_f_c_string_n7h27b3((void *)NULL, cx, NULL, t18);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(gm_b16_8, (uint8_t *)&t13 + 8, t19);
            gm_tabb710d9mq2e t20;
            gm_f_format_ckll4kg((void *)&t20, (void *)NULL, cx, NULL, &t13);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t21 = t20;
            if (((((uint8_t *)&t21))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t21) + 8, gm_srce5lfiiur8udn, 45, 20);
            gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)&t21) + 8));
            gm_b16_8 t23 = t22;
            gm_b16_8 t24 = GM_LD(gm_b16_8, ((uint8_t *)&t23));
            GM_ST(gm_b16_8, ((uint8_t *)&((struct gm_fr_resolve_lines_h4bf30f *)((&F.h)->up))->l5), t24);
        }
        goto ix5;
ie5:;
        gm_b16_8 t26 = gm_l1;
        gm_b16_8 t27;
        GM_ST(void *, (uint8_t *)&t27, (void *)(gm_spkrf9e7cr6h4));
        GM_ST(uintptr_t, (uint8_t *)&t27 + 8, (uintptr_t)(6));
        uint8_t t28 = gm_f_starts_with_6vvt01e(t26, t27);
        if (!t28) goto ie25;
        {
            gm_b16_8 t30 = gm_l1;
            gm_th2lqbo77pvsc t31;
            GM_ST(uint8_t, (uint8_t *)&t31 + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&t31 + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&t31 + 20, 0);
            GM_ST(gm_b16_8, ((uint8_t *)&t31), t30);
            ((uint8_t *)&t31)[16] = 1;
            GM_ST(gm_th2lqbo77pvsc, ((uint8_t *)&((struct gm_fr_resolve_lines_h4bf30f *)((&F.h)->up))->l4), t31);
        }
        {
            gm_b16_8 t32 = gm_l1;
            GM_ST(gm_b16_8, ((uint8_t *)&((struct gm_fr_resolve_lines_h4bf30f *)((&F.h)->up))->l5), t32);
        }
        goto ix25;
ie25:;
        {
            if (!((((uint8_t *)&((struct gm_fr_resolve_lines_h4bf30f *)((&F.h)->up))->l4))[16] != 0)) goto ie34;
            gm_b16_8 t35 = GM_LD(gm_b16_8, ((uint8_t *)&((struct gm_fr_resolve_lines_h4bf30f *)((&F.h)->up))->l4));
            gm_l2 = t35;
            {
                gm_b16_8 t38 = GM_LD(gm_b16_8, ((uint8_t *)&((struct gm_fr_resolve_lines_h4bf30f *)((&F.h)->up))->l5));
                gm_b16_8 t39 = gm_l2;
                gm_b16_8 t40 = t38;
                gm_b16_8 t41 = t39;
                uint8_t t42 = (!gm_eq0iqiubfm7e(((uint8_t *)&t40), ((uint8_t *)&t41)));
                if (!t42) goto ie37;
                {
                    void * t44 = (void *)((uint8_t *)&((struct gm_fr_resolve_lines_h4bf30f *)((&F.h)->up))->l2);
                    gm_b16_8 t45 = gm_l2;
                    gm_tnpifv20irnph t46;
                    GM_ST(gm_b16_8, ((uint8_t *)&t46 + 0), t45);
                    gm_tftb1odc739t5 t47 = gm_f_write_cdouvhd(t44, t46);
                    gm_tftb1odc739t5 t48 = t47;
                    if (((((uint8_t *)&t48))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t48) + 1, gm_srce5lfiiur8udn, 52, 21);
                    gm_b0_1 t49 = GM_LD(gm_b0_1, (((uint8_t *)&t48) + 1));
                }
                {
                    uint64_t t50 = GM_LD(uint64_t, ((uint8_t *)&((struct gm_fr_resolve_lines_h4bf30f *)((&F.h)->up))->l3));
                    uint64_t r51; bool o52 = __builtin_add_overflow(t50, ((uint64_t)1ULL), &r51);
                    if (o52) gm_trap("integer overflow in `+`", gm_srce5lfiiur8udn, 53, 21);
                    uint64_t t53 = r51;
                    GM_ST(uint64_t, ((uint8_t *)&((struct gm_fr_resolve_lines_h4bf30f *)((&F.h)->up))->l3), t53);
                }
                goto ix37;
ie37:;
ix37:;
            }
            goto ix34;
ie34:;
ix34:;
        }
        {
            gm_b16_8 t54 = gm_l1;
            GM_ST(gm_b16_8, ((uint8_t *)&((struct gm_fr_resolve_lines_h4bf30f *)((&F.h)->up))->l5), t54);
        }
ix25:;
ix5:;
    }
    {
        void * t55 = (void *)((uint8_t *)&((struct gm_fr_resolve_lines_h4bf30f *)((&F.h)->up))->l2);
        gm_b16_8 t56 = GM_LD(gm_b16_8, ((uint8_t *)&((struct gm_fr_resolve_lines_h4bf30f *)((&F.h)->up))->l5));
        gm_tnpifv20irnph t57;
        GM_ST(gm_b16_8, ((uint8_t *)&t57 + 0), t56);
        gm_tftb1odc739t5 t58 = gm_f_write_cdouvhd(t55, t57);
        gm_tftb1odc739t5 t59 = t58;
        if (((((uint8_t *)&t59))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t59) + 1, gm_srce5lfiiur8udn, 58, 9);
        gm_b0_1 t60 = GM_LD(gm_b0_1, (((uint8_t *)&t59) + 1));
    }
    {
        uint64_t t61 = GM_LD(uint64_t, ((uint8_t *)&((struct gm_fr_resolve_lines_h4bf30f *)((&F.h)->up))->l3));
        uint64_t r62; bool o63 = __builtin_add_overflow(t61, ((uint64_t)1ULL), &r62);
        if (o63) gm_trap("integer overflow in `+`", gm_srce5lfiiur8udn, 59, 9);
        uint64_t t64 = r62;
        GM_ST(uint64_t, ((uint8_t *)&((struct gm_fr_resolve_lines_h4bf30f *)((&F.h)->up))->l3), t64);
    }
    goto me1;
mn2:;
    gm_trap("no match arm matched the scrutinee", gm_srce5lfiiur8udn, 41, 5);
me1:;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_gdb_script_section_320gurp(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_gdb_script_section_320gurp F;
    gm_b16_8 gm_l0; /* script */
    gm_b16_8 gm_l2; /* <tmp> */
    GM_STACK_CHECK(&F, 373u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sockrjemj2if8));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(60));
        gm_tftb1odc739t5 t4 = gm_f_extend_0cs85gs(t2, t3);
        gm_tftb1odc739t5 t5 = t4;
        if (((((uint8_t *)&t5))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t5) + 1, gm_srce5lfiiur8udn, 69, 5);
        gm_b0_1 t6 = GM_LD(gm_b0_1, (((uint8_t *)&t5) + 1));
    }
    {
        void * t7 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t8;
        GM_ST(void *, (uint8_t *)&t8, (void *)(gm_scohuf4dfj32r));
        GM_ST(uintptr_t, (uint8_t *)&t8 + 8, (uintptr_t)(36));
        gm_tftb1odc739t5 t9 = gm_f_extend_0cs85gs(t7, t8);
        gm_tftb1odc739t5 t10 = t9;
        if (((((uint8_t *)&t10))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t10) + 1, gm_srce5lfiiur8udn, 70, 5);
        gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
    }
    {
        {
            gm_b16_8 t13 = gm_l0;
            gm_l2 = t13;
        }
        gm_hval t14 = ((gm_hval){(void *)&F.h, cx});
        gm_node wn15 = {.eff = 993381376, .kind = GM_LIT, .clauses = gm_hln6al4vijjv, .env = t14.env, .hctx = t14.cx, .with_frame = &F.h, .with_label = 0, .result = (void *)&F.w0, .fw = NULL, .next = cx};
        gm_b16_8 t18 = gm_l2;
        gm_b0_1 t19 = gm_f_lines_9uns0k2_sld76pa((void *)NULL, (&wn15), NULL, t18);
        if (gm_unw.kind) {
            goto gm_ll17;
        }
        F.w0 = t19;
        goto gm_ls20;
gm_ll17:;
        if (gm_unw.kind == GM_BREAK && gm_unw.frame == &F.h && gm_unw.label == 0) { gm_unw.kind = GM_NONE; goto we16; }
        goto gm_lad;
gm_ls20:;
we16:;
        gm_b0_1 t21 = F.w0;
        gm_b0_1 t22 = t21;
    }
    {
        void * t23 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t24;
        GM_ST(void *, (uint8_t *)&t24, (void *)(gm_sc6gojr5sjib4));
        GM_ST(uintptr_t, (uint8_t *)&t24 + 8, (uintptr_t)(20));
        gm_tftb1odc739t5 t25 = gm_f_extend_0cs85gs(t23, t24);
        gm_tftb1odc739t5 t26 = t25;
        if (((((uint8_t *)&t26))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t26) + 1, gm_srce5lfiiur8udn, 79, 5);
        gm_b0_1 t27 = GM_LD(gm_b0_1, (((uint8_t *)&t26) + 1));
    }
    {
        gm_b16_8 t28 = GM_LD(gm_b16_8, (((uint8_t *)&F.l1) + 0));
        gm_b16_8 t29 = t28;
        gm_b16_8 t30 = GM_LD(gm_b16_8, ((uint8_t *)&t29));
        gm_b16_8 t31 = gm_f_c_string_n7h27b3((void *)NULL, cx, NULL, t30);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tnpifv20irnph t32;
        GM_ST(gm_b16_8, ((uint8_t *)&t32 + 0), t31);
        gm_tabb710d9mq2e t33;
        gm_f_format_8rfbft0((void *)&t33, (void *)NULL, cx, NULL, t32);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t34 = t33;
        if (((((uint8_t *)&t34))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t34) + 8, gm_srce5lfiiur8udn, 80, 12);
        gm_b16_8 t35 = GM_LD(gm_b16_8, (((uint8_t *)&t34) + 8));
        gm_b16_8 t36 = t35;
        gm_b16_8 t37 = GM_LD(gm_b16_8, ((uint8_t *)&t36));
        uint8_t t38 = 0;
        F.ret = t37;
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
__attribute__((aligned(4))) gm_b16_8 gm_f_guard_o7k9a0e(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1) {
    struct gm_fr_guard_o7k9a0e F;
    gm_b16_8 gm_l0; /* names */
    gm_b16_8 gm_l1; /* decls */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    gm_b16_8 gm_l5; /* n */
    gm_b16_8 gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    gm_b16_8 gm_l8; /* n */
    GM_STACK_CHECK(&F, 557u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint8_t t3 = 0;
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        uint8_t t5 = ((t4 == ((uint64_t)0ULL)));
        if (!t5) {
            uint64_t t6 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            uint8_t t7 = ((t6 == ((uint64_t)0ULL)));
            t3 = t7;
        } else t3 = 1;
        if (!t3) goto ie2;
        {
            gm_b16_8 t9 = gm_l1;
            uint8_t t10 = 0;
            F.ret = t9;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        gm_f_list_uvfu1k1((void *)&F.l2, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t12 = gm_l0;
            gm_l3 = t12;
        }
        {
            gm_l4 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t14 = gm_l4;
                uint64_t t15 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                uint8_t t16 = (t14 < t15);
                if (!t16) goto lb13;
                {
                    uint64_t t19 = gm_l4;
                    uint64_t t20 = (uintptr_t)t19;
                    uint64_t t21 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                    gm_b16_8 t24 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t20 * 16));
                    gm_l5 = t24;
                    {
                        uint8_t t27 = (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8) == 7) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l5)), gm_s3inajboct11s, 7) == 0));
                        if (!t27) goto ie26;
                        {
                            void * t29 = (void *)((uint8_t *)&F.l2);
                            gm_t6j6smj7qoctu t30;
                            gm_b16_8 t31 = gm_l5;
                            GM_ST(gm_b16_8, (uint8_t *)&t30 + 0, t31);
                            gm_b16_8 t32 = gm_l5;
                            GM_ST(gm_b16_8, (uint8_t *)&t30 + 16, t32);
                            gm_tftb1odc739t5 t33 = gm_f_write_6a87mea(t29, &t30);
                            gm_tftb1odc739t5 t34 = t33;
                            if (((((uint8_t *)&t34))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t34) + 1, gm_srce5lfiiur8udn, 162, 33);
                            gm_b0_1 t35 = GM_LD(gm_b0_1, (((uint8_t *)&t34) + 1));
                        }
                        goto ix26;
ie26:;
ix26:;
                    }
                    goto me22;
mn23:;
                    gm_trap("no match arm matched the scrutinee", gm_srce5lfiiur8udn, 161, 5);
me22:;
                }
lc13:;
                {
                    uint64_t t37 = gm_l4;
                    uint64_t r38; bool o39 = __builtin_add_overflow(t37, ((uint64_t)1ULL), &r38);
                    uint64_t t40 = r38;
                    gm_l4 = t40;
                }
            }
lb13:;
        }
    }
    {
        void * t41 = (void *)((uint8_t *)&F.l2);
        gm_b16_8 t42 = gm_l1;
        gm_tftb1odc739t5 t43 = gm_f_extend_0cs85gs(t41, t42);
        gm_tftb1odc739t5 t44 = t43;
        if (((((uint8_t *)&t44))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t44) + 1, gm_srce5lfiiur8udn, 164, 5);
        gm_b0_1 t45 = GM_LD(gm_b0_1, (((uint8_t *)&t44) + 1));
    }
    {
        {
            gm_b16_8 t47 = gm_l0;
            gm_l6 = t47;
        }
        {
            gm_l7 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t49 = gm_l7;
                uint64_t t50 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                uint8_t t51 = (t49 < t50);
                if (!t51) goto lb48;
                {
                    uint64_t t54 = gm_l7;
                    uint64_t t55 = (uintptr_t)t54;
                    uint64_t t56 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                    gm_b16_8 t59 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t55 * 16));
                    gm_l8 = t59;
                    {
                        uint8_t t62 = (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8) == 7) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l8)), gm_s3inajboct11s, 7) == 0));
                        if (!t62) goto ie61;
                        {
                            void * t64 = (void *)((uint8_t *)&F.l2);
                            gm_b16_8 t65 = gm_l8;
                            gm_tnpifv20irnph t66;
                            GM_ST(gm_b16_8, ((uint8_t *)&t66 + 0), t65);
                            gm_tftb1odc739t5 t67 = gm_f_write_to8v62c(t64, t66);
                            gm_tftb1odc739t5 t68 = t67;
                            if (((((uint8_t *)&t68))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t68) + 1, gm_srce5lfiiur8udn, 166, 33);
                            gm_b0_1 t69 = GM_LD(gm_b0_1, (((uint8_t *)&t68) + 1));
                        }
                        goto ix61;
ie61:;
ix61:;
                    }
                    goto me57;
mn58:;
                    gm_trap("no match arm matched the scrutinee", gm_srce5lfiiur8udn, 165, 5);
me57:;
                }
lc48:;
                {
                    uint64_t t71 = gm_l7;
                    uint64_t r72; bool o73 = __builtin_add_overflow(t71, ((uint64_t)1ULL), &r72);
                    uint64_t t74 = r72;
                    gm_l7 = t74;
                }
            }
lb48:;
        }
    }
    {
        gm_b16_8 t75 = GM_LD(gm_b16_8, (((uint8_t *)&F.l2) + 0));
        gm_b16_8 t76 = t75;
        gm_b16_8 t77 = GM_LD(gm_b16_8, ((uint8_t *)&t76));
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
__attribute__((aligned(4))) void gm_f_member_name_mj2bvs3(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_member_name_mj2bvs3 F;
    gm_b16_8 gm_l0; /* name */
    gm_b16_8 gm_l1; /* <tmp> */
    uint64_t gm_l2; /* <tmp> */
    uint64_t gm_l3; /* i */
    uint8_t gm_l4; /* c */
    uint8_t gm_l5; /* <tmp> */
    uint8_t gm_l6; /* <tmp> */
    uint8_t gm_l7; /* alpha */
    uint8_t gm_l8; /* <tmp> */
    uint8_t gm_l9; /* <tmp> */
    gm_timmc9i8o8nqm gm_l10; /* <tmp> */
    gm_b16_8 gm_l11; /* <tmp> */
    uint64_t gm_l12; /* <tmp> */
    uint8_t gm_l13; /* p */
    uint8_t gm_l14; /* numbered */
    gm_b16_8 gm_l15; /* <tmp> */
    uint64_t gm_l16; /* <tmp> */
    uint8_t gm_l17; /* c */
    uint8_t gm_l18; /* <tmp> */
    gm_b16_8 gm_l19; /* <tmp> */
    uint64_t gm_l20; /* <tmp> */
    gm_b16_8 gm_l21; /* own */
    gm_b16_8 gm_l22; /* <tmp> */
    uint64_t gm_l23; /* <tmp> */
    gm_b16_8 gm_l24; /* k */
    GM_STACK_CHECK_SHALLOW(&F, 1346u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint64_t t3 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        uint8_t t4 = ((t3 == ((uint64_t)0ULL)));
        if (!t4) goto ie2;
        {
            gm_th2lqbo77pvsc t6;
            memset(&t6, 0, sizeof t6);
            uint8_t t7 = 0;
            F.ret = t6;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        {
            gm_b16_8 t9 = gm_l0;
            gm_l1 = t9;
        }
        {
            gm_l2 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t11 = gm_l2;
                uint64_t t12 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                uint8_t t13 = (t11 < t12);
                if (!t13) goto lb10;
                {
                    uint64_t t16 = gm_l2;
                    uint64_t t17 = gm_l2;
                    uint64_t t18 = (uintptr_t)t17;
                    uint64_t t19 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                    uint8_t t20 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t18 * 1));
                    gm_tf5srh4mp4b3i t21;
                    GM_ST(uint8_t, (uint8_t *)&t21 + 9, 0);
                    GM_ST(uint16_t, (uint8_t *)&t21 + 10, 0);
                    GM_ST(uint32_t, (uint8_t *)&t21 + 12, 0);
                    GM_ST(uint64_t, ((uint8_t *)&t21 + 0), t16);
                    GM_ST(uint8_t, ((uint8_t *)&t21 + 8), t20);
                    gm_tf5srh4mp4b3i t22 = t21;
                    uint64_t t25 = GM_LD(uint64_t, (((uint8_t *)&t22) + 0));
                    gm_l3 = t25;
                    uint8_t t26 = GM_LD(uint8_t, (((uint8_t *)&t22) + 8));
                    gm_l4 = t26;
                    {
                        uint8_t t28 = 0;
                        uint8_t t29 = 0;
                        {
                            uint8_t t31 = gm_l4;
                            gm_l5 = t31;
                        }
                        uint8_t t32 = 0;
                        uint8_t t33 = gm_l5;
                        uint8_t t34 = (((uint8_t)97ULL) <= t33);
                        if (t34) {
                            uint8_t t35 = gm_l5;
                            uint8_t t36 = (t35 <= ((uint8_t)122ULL));
                            t32 = t36;
                        } else t32 = 0;
                        uint8_t t37 = t32;
                        if (!t37) {
                            {
                                uint8_t t39 = gm_l4;
                                gm_l6 = t39;
                            }
                            uint8_t t40 = 0;
                            uint8_t t41 = gm_l6;
                            uint8_t t42 = (((uint8_t)65ULL) <= t41);
                            if (t42) {
                                uint8_t t43 = gm_l6;
                                uint8_t t44 = (t43 <= ((uint8_t)90ULL));
                                t40 = t44;
                            } else t40 = 0;
                            uint8_t t45 = t40;
                            t29 = t45;
                        } else t29 = 1;
                        if (!t29) {
                            uint8_t t46 = gm_l4;
                            uint8_t t47 = ((t46 == ((uint8_t)95ULL)));
                            t28 = t47;
                        } else t28 = 1;
                        gm_l7 = t28;
                    }
                    {
                        uint8_t t49 = 0;
                        uint8_t t50 = gm_l7;
                        if (!t50) {
                            uint8_t t51 = 0;
                            uint64_t t52 = gm_l3;
                            uint8_t t53 = (t52 > ((uint64_t)0ULL));
                            if (t53) {
                                {
                                    uint8_t t55 = gm_l4;
                                    gm_l8 = t55;
                                }
                                uint8_t t56 = 0;
                                uint8_t t57 = gm_l8;
                                uint8_t t58 = (((uint8_t)48ULL) <= t57);
                                if (t58) {
                                    uint8_t t59 = gm_l8;
                                    uint8_t t60 = (t59 <= ((uint8_t)57ULL));
                                    t56 = t60;
                                } else t56 = 0;
                                uint8_t t61 = t56;
                                t51 = t61;
                            } else t51 = 0;
                            t49 = t51;
                        } else t49 = 1;
                        uint8_t t62 = (uint8_t)!t49;
                        if (!t62) goto ie48;
                        {
                            gm_th2lqbo77pvsc t64;
                            memset(&t64, 0, sizeof t64);
                            uint8_t t65 = 0;
                            F.ret = t64;
                            goto gm_fret;
                        }
                        goto ix48;
ie48:;
ix48:;
                    }
                    goto me23;
mn24:;
                    gm_trap("no match arm matched the scrutinee", gm_srce5lfiiur8udn, 87, 5);
me23:;
                }
lc10:;
                {
                    uint64_t t67 = gm_l2;
                    uint64_t r68; bool o69 = __builtin_add_overflow(t67, ((uint64_t)1ULL), &r68);
                    uint64_t t70 = r68;
                    gm_l2 = t70;
                }
            }
lb10:;
        }
    }
    {
        uint8_t t72 = 0;
        gm_b16_8 t73 = gm_l0;
        gm_b16_8 t74;
        GM_ST(void *, (uint8_t *)&t74, (void *)(gm_sebojpbfjfgaf));
        GM_ST(uintptr_t, (uint8_t *)&t74 + 8, (uintptr_t)(2));
        uint8_t t75 = gm_f_starts_with_6vvt01e(t73, t74);
        if (!t75) {
            uint8_t t76 = 0;
            uint8_t t77 = 0;
            uint64_t t78 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t79 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            if (t78 >= (uintptr_t)(t79)) gm_trap_index(t78, t79, gm_srce5lfiiur8udn, 92, 35);
            uint8_t t80 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t78 * 1));
            uint8_t t81 = ((t80 == ((uint8_t)95ULL)));
            if (t81) {
                uint64_t t82 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                uint8_t t83 = (t82 > ((uint64_t)1ULL));
                t77 = t83;
            } else t77 = 0;
            if (t77) {
                {
                    uint64_t t85 = (uintptr_t)((uint64_t)1ULL);
                    uint64_t t86 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                    if (t85 >= (uintptr_t)(t86)) gm_trap_index(t85, t86, gm_srce5lfiiur8udn, 92, 71);
                    uint8_t t87 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t85 * 1));
                    gm_l9 = t87;
                }
                uint8_t t88 = 0;
                uint8_t t89 = gm_l9;
                uint8_t t90 = (((uint8_t)65ULL) <= t89);
                if (t90) {
                    uint8_t t91 = gm_l9;
                    uint8_t t92 = (t91 <= ((uint8_t)90ULL));
                    t88 = t92;
                } else t88 = 0;
                uint8_t t93 = t88;
                t76 = t93;
            } else t76 = 0;
            t72 = t76;
        } else t72 = 1;
        if (!t72) goto ie71;
        {
            gm_th2lqbo77pvsc t95;
            memset(&t95, 0, sizeof t95);
            uint8_t t96 = 0;
            F.ret = t95;
            goto gm_fret;
        }
        goto ix71;
ie71:;
ix71:;
    }
    {
        uint64_t t98 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        uint8_t t99 = (t98 >= ((uint64_t)3ULL));
        if (!t99) goto ie97;
        uint8_t t100 = 0;
        uint64_t t101 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t102 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        if (t101 >= (uintptr_t)(t102)) gm_trap_index(t101, t102, gm_srce5lfiiur8udn, 93, 27);
        uint8_t t103 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t101 * 1));
        uint8_t t104 = ((t103 == ((uint8_t)103ULL)));
        if (!t104) {
            uint64_t t105 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t106 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            if (t105 >= (uintptr_t)(t106)) gm_trap_index(t105, t106, gm_srce5lfiiur8udn, 93, 45);
            uint8_t t107 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t105 * 1));
            uint8_t t108 = ((t107 == ((uint8_t)71ULL)));
            t100 = t108;
        } else t100 = 1;
        if (!t100) goto ie97;
        uint8_t t109 = 0;
        uint64_t t110 = (uintptr_t)((uint64_t)1ULL);
        uint64_t t111 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        if (t110 >= (uintptr_t)(t111)) gm_trap_index(t110, t111, gm_srce5lfiiur8udn, 93, 66);
        uint8_t t112 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t110 * 1));
        uint8_t t113 = ((t112 == ((uint8_t)109ULL)));
        if (!t113) {
            uint64_t t114 = (uintptr_t)((uint64_t)1ULL);
            uint64_t t115 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            if (t114 >= (uintptr_t)(t115)) gm_trap_index(t114, t115, gm_srce5lfiiur8udn, 93, 84);
            uint8_t t116 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t114 * 1));
            uint8_t t117 = ((t116 == ((uint8_t)77ULL)));
            t109 = t117;
        } else t109 = 1;
        if (!t109) goto ie97;
        uint64_t t118 = (uintptr_t)((uint64_t)2ULL);
        uint64_t t119 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        if (t118 >= (uintptr_t)(t119)) gm_trap_index(t118, t119, gm_srce5lfiiur8udn, 93, 104);
        uint8_t t120 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t118 * 1));
        uint8_t t121 = ((t120 == ((uint8_t)95ULL)));
        if (!t121) goto ie97;
        {
            gm_th2lqbo77pvsc t123;
            memset(&t123, 0, sizeof t123);
            uint8_t t124 = 0;
            F.ret = t123;
            goto gm_fret;
        }
        goto ix97;
ie97:;
ix97:;
    }
    {
        {
            {
                gm_timmc9i8o8nqm t127;
                GM_ST(uint8_t, ((uint8_t *)&t127 + 0), ((uint8_t)108ULL));
                GM_ST(uint8_t, ((uint8_t *)&t127 + 1), ((uint8_t)119ULL));
                GM_ST(uint8_t, ((uint8_t *)&t127 + 2), ((uint8_t)102ULL));
                GM_ST(uint8_t, ((uint8_t *)&t127 + 3), ((uint8_t)97ULL));
                GM_ST(uint8_t, ((uint8_t *)&t127 + 4), ((uint8_t)98ULL));
                gm_l10 = t127;
            }
            void * t128 = ((uint8_t *)&gm_l10);
            if ((uintptr_t)0 > (uintptr_t)5 || (uintptr_t)5 > (uintptr_t)5) gm_trap_slice((uintptr_t)0, (uintptr_t)5, 5, gm_srce5lfiiur8udn, 96, 5);
            gm_b16_8 t129;
            GM_ST(void *, (uint8_t *)&t129, (void *)(((uint8_t *)t128 + (uintptr_t)0 * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t129 + 8, (uintptr_t)((uintptr_t)5 - (uintptr_t)0));
            gm_b16_8 t130 = t129;
            gm_l11 = t130;
        }
        {
            gm_l12 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t132 = gm_l12;
                uint64_t t133 = GM_LD(uintptr_t, ((uint8_t *)&gm_l11) + 8);
                uint8_t t134 = (t132 < t133);
                if (!t134) goto lb131;
                {
                    uint64_t t137 = gm_l12;
                    uint64_t t138 = (uintptr_t)t137;
                    uint64_t t139 = GM_LD(uintptr_t, ((uint8_t *)&gm_l11) + 8);
                    uint8_t t140 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l11)) + t138 * 1));
                    uint8_t t141 = t140;
                    uint8_t t144 = GM_LD(uint8_t, ((uint8_t *)&t141));
                    gm_l13 = t144;
                    {
                        uint64_t t147 = (uintptr_t)((uint64_t)0ULL);
                        uint64_t t148 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                        if (t147 >= (uintptr_t)(t148)) gm_trap_index(t147, t148, gm_srce5lfiiur8udn, 97, 12);
                        uint8_t t149 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t147 * 1));
                        uint8_t t150 = gm_l13;
                        uint8_t t151 = ((t149 == t150));
                        if (!t151) goto ie146;
                        uint64_t t152 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                        uint8_t t153 = (t152 > ((uint64_t)1ULL));
                        if (!t153) goto ie146;
                        {
                            gm_l14 = ((uint8_t)1);
                        }
                        {
                            {
                                uint64_t t156 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                                void * t157 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)));
                                uint64_t t158 = (uintptr_t)((uint64_t)1ULL);
                                uint64_t t159 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                                uint64_t t160 = (uintptr_t)t159;
                                if (t158 > t160 || t160 > (uintptr_t)t156) gm_trap_slice(t158, t160, t156, gm_srce5lfiiur8udn, 99, 22);
                                gm_b16_8 t161;
                                GM_ST(void *, (uint8_t *)&t161, (void *)(((uint8_t *)t157 + t158 * 1)));
                                GM_ST(uintptr_t, (uint8_t *)&t161 + 8, (uintptr_t)(t160 - t158));
                                gm_l15 = t161;
                            }
                            {
                                gm_l16 = ((uint64_t)0ULL);
                            }
                            {
                                for (;;) {
                                    uint64_t t163 = gm_l16;
                                    uint64_t t164 = GM_LD(uintptr_t, ((uint8_t *)&gm_l15) + 8);
                                    uint8_t t165 = (t163 < t164);
                                    if (!t165) goto lb162;
                                    {
                                        uint64_t t168 = gm_l16;
                                        uint64_t t169 = (uintptr_t)t168;
                                        uint64_t t170 = GM_LD(uintptr_t, ((uint8_t *)&gm_l15) + 8);
                                        uint8_t t171 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l15)) + t169 * 1));
                                        uint8_t t172 = t171;
                                        uint8_t t175 = GM_LD(uint8_t, ((uint8_t *)&t172));
                                        gm_l17 = t175;
                                        {
                                            {
                                                uint8_t t179 = gm_l17;
                                                gm_l18 = t179;
                                            }
                                            uint8_t t180 = 0;
                                            uint8_t t181 = gm_l18;
                                            uint8_t t182 = (((uint8_t)48ULL) <= t181);
                                            if (t182) {
                                                uint8_t t183 = gm_l18;
                                                uint8_t t184 = (t183 <= ((uint8_t)57ULL));
                                                t180 = t184;
                                            } else t180 = 0;
                                            uint8_t t185 = (uint8_t)!t180;
                                            uint8_t t186 = t185;
                                            if (!t186) goto ie177;
                                            {
                                                gm_l14 = ((uint8_t)0);
                                            }
                                            goto ix177;
ie177:;
ix177:;
                                        }
                                        goto me173;
mn174:;
                                        gm_trap("no match arm matched the scrutinee", gm_srce5lfiiur8udn, 99, 13);
me173:;
                                    }
lc162:;
                                    {
                                        uint64_t t189 = gm_l16;
                                        uint64_t r190; bool o191 = __builtin_add_overflow(t189, ((uint64_t)1ULL), &r190);
                                        uint64_t t192 = r190;
                                        gm_l16 = t192;
                                    }
                                }
lb162:;
                            }
                        }
                        {
                            uint8_t t194 = gm_l14;
                            if (!t194) goto ie193;
                            {
                                gm_th2lqbo77pvsc t196;
                                memset(&t196, 0, sizeof t196);
                                uint8_t t197 = 0;
                                F.ret = t196;
                                goto gm_fret;
                            }
                            goto ix193;
ie193:;
ix193:;
                        }
                        goto ix146;
ie146:;
ix146:;
                    }
                    goto me142;
mn143:;
                    gm_trap("no match arm matched the scrutinee", gm_srce5lfiiur8udn, 96, 5);
me142:;
                }
lc131:;
                {
                    uint64_t t199 = gm_l12;
                    uint64_t r200; bool o201 = __builtin_add_overflow(t199, ((uint64_t)1ULL), &r200);
                    uint64_t t202 = r200;
                    gm_l12 = t202;
                }
            }
lb131:;
        }
    }
    {
        {
            void * t204 = ((uint8_t *)gm_c66tv9kq8na);
            if ((uintptr_t)0 > (uintptr_t)13 || (uintptr_t)13 > (uintptr_t)13) gm_trap_slice((uintptr_t)0, (uintptr_t)13, 13, gm_srce5lfiiur8udn, 105, 5);
            gm_b16_8 t205;
            GM_ST(void *, (uint8_t *)&t205, (void *)(((uint8_t *)t204 + (uintptr_t)0 * 16)));
            GM_ST(uintptr_t, (uint8_t *)&t205 + 8, (uintptr_t)((uintptr_t)13 - (uintptr_t)0));
            gm_l19 = t205;
        }
        {
            gm_l20 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t207 = gm_l20;
                uint64_t t208 = GM_LD(uintptr_t, ((uint8_t *)&gm_l19) + 8);
                uint8_t t209 = (t207 < t208);
                if (!t209) goto lb206;
                {
                    uint64_t t212 = gm_l20;
                    uint64_t t213 = (uintptr_t)t212;
                    uint64_t t214 = GM_LD(uintptr_t, ((uint8_t *)&gm_l19) + 8);
                    gm_b16_8 t217 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l19)) + t213 * 16));
                    gm_l21 = t217;
                    {
                        gm_b16_8 t220 = gm_l0;
                        gm_b16_8 t221 = gm_l21;
                        gm_b16_8 t222 = t220;
                        gm_b16_8 t223 = t221;
                        uint8_t t224 = (gm_eq0iqiubfm7e(((uint8_t *)&t222), ((uint8_t *)&t223)));
                        if (!t224) goto ie219;
                        {
                            gm_th2lqbo77pvsc t226;
                            memset(&t226, 0, sizeof t226);
                            uint8_t t227 = 0;
                            F.ret = t226;
                            goto gm_fret;
                        }
                        goto ix219;
ie219:;
ix219:;
                    }
                    goto me215;
mn216:;
                    gm_trap("no match arm matched the scrutinee", gm_srce5lfiiur8udn, 105, 5);
me215:;
                }
lc206:;
                {
                    uint64_t t229 = gm_l20;
                    uint64_t r230; bool o231 = __builtin_add_overflow(t229, ((uint64_t)1ULL), &r230);
                    uint64_t t232 = r230;
                    gm_l20 = t232;
                }
            }
lb206:;
        }
    }
    {
        {
            void * t234 = ((uint8_t *)gm_cphj36bitbq);
            if ((uintptr_t)0 > (uintptr_t)43 || (uintptr_t)43 > (uintptr_t)43) gm_trap_slice((uintptr_t)0, (uintptr_t)43, 43, gm_srce5lfiiur8udn, 108, 5);
            gm_b16_8 t235;
            GM_ST(void *, (uint8_t *)&t235, (void *)(((uint8_t *)t234 + (uintptr_t)0 * 16)));
            GM_ST(uintptr_t, (uint8_t *)&t235 + 8, (uintptr_t)((uintptr_t)43 - (uintptr_t)0));
            gm_l22 = t235;
        }
        {
            gm_l23 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t237 = gm_l23;
                uint64_t t238 = GM_LD(uintptr_t, ((uint8_t *)&gm_l22) + 8);
                uint8_t t239 = (t237 < t238);
                if (!t239) goto lb236;
                {
                    uint64_t t242 = gm_l23;
                    uint64_t t243 = (uintptr_t)t242;
                    uint64_t t244 = GM_LD(uintptr_t, ((uint8_t *)&gm_l22) + 8);
                    gm_b16_8 t247 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l22)) + t243 * 16));
                    gm_l24 = t247;
                    {
                        gm_b16_8 t250 = gm_l0;
                        gm_b16_8 t251 = gm_l24;
                        gm_b16_8 t252 = t250;
                        gm_b16_8 t253 = t251;
                        uint8_t t254 = (gm_eq0iqiubfm7e(((uint8_t *)&t252), ((uint8_t *)&t253)));
                        if (!t254) goto ie249;
                        {
                            gm_th2lqbo77pvsc t256;
                            memset(&t256, 0, sizeof t256);
                            uint8_t t257 = 0;
                            F.ret = t256;
                            goto gm_fret;
                        }
                        goto ix249;
ie249:;
ix249:;
                    }
                    goto me245;
mn246:;
                    gm_trap("no match arm matched the scrutinee", gm_srce5lfiiur8udn, 108, 5);
me245:;
                }
lc236:;
                {
                    uint64_t t259 = gm_l23;
                    uint64_t r260; bool o261 = __builtin_add_overflow(t259, ((uint64_t)1ULL), &r260);
                    uint64_t t262 = r260;
                    gm_l23 = t262;
                }
            }
lb236:;
        }
    }
    {
        gm_b16_8 t263 = gm_l0;
        gm_th2lqbo77pvsc t264;
        GM_ST(uint8_t, (uint8_t *)&t264 + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&t264 + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&t264 + 20, 0);
        GM_ST(gm_b16_8, ((uint8_t *)&t264), t263);
        ((uint8_t *)&t264)[16] = 1;
        uint8_t t265 = 0;
        F.ret = t264;
        goto gm_fret;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_parent_dir_df7dgp5(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_parent_dir_df7dgp5 F;
    gm_b16_8 gm_l0; /* exe */
    gm_b16_8 gm_l1; /* p */
    GM_STACK_CHECK(&F, 267u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_b16_8 t2 = gm_l0;
        gm_th2lqbo77pvsc t3;
        gm_f_parent_lk0iero((void *)&t3, (void *)NULL, cx, NULL, t2);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_th2lqbo77pvsc t4 = t3;
        gm_b16_8 t5;
        if (((((uint8_t *)&t4))[16] != 0)) {
            gm_b16_8 t6 = GM_LD(gm_b16_8, (((uint8_t *)&t4) + 0));
            t5 = t6;
        } else {
            gm_b16_8 t7;
            GM_ST(void *, (uint8_t *)&t7, (void *)(gm_sl3q52ep6aa53));
            GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)(1));
            uint8_t t8 = 0;
            F.ret = t7;
            goto gm_fret;
        }
        gm_l1 = t5;
    }
    {
        gm_b16_8 t9;
        uint64_t t11 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint8_t t12 = ((t11 == ((uint64_t)0ULL)));
        if (!t12) goto ie10;
        gm_b16_8 t14;
        GM_ST(void *, (uint8_t *)&t14, (void *)(gm_sl3q52ep6aa53));
        GM_ST(uintptr_t, (uint8_t *)&t14 + 8, (uintptr_t)(1));
        gm_b16_8 t15 = t14;
        t9 = t15;
        goto ix10;
ie10:;
        gm_b16_8 t17 = gm_l1;
        gm_b16_8 t18 = t17;
        t9 = t18;
ix10:;
        uint8_t t19 = 0;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_resolve_lines_h4bf30f(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1) {
    struct gm_fr_resolve_lines_h4bf30f F;
    gm_b16_8 gm_l0; /* code */
    gm_b16_8 gm_l6; /* <tmp> */
    GM_STACK_CHECK(&F, 201u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    F.l1 = a1;
    {
        gm_f_list_uvfu1k1((void *)&F.l2, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        F.l3 = ((uint64_t)0ULL);
    }
    {
        gm_th2lqbo77pvsc t2;
        memset(&t2, 0, sizeof t2);
        F.l4 = t2;
    }
    {
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sr4qbrk6tlgaa));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(0));
        F.l5 = t3;
    }
    {
        {
            gm_b16_8 t5 = gm_l0;
            gm_l6 = t5;
        }
        gm_hval t6 = ((gm_hval){(void *)&F.h, cx});
        gm_node wn7 = {.eff = 993381376, .kind = GM_LIT, .clauses = gm_hlifma0pdvu5, .env = t6.env, .hctx = t6.cx, .with_frame = &F.h, .with_label = 0, .result = (void *)&F.w0, .fw = NULL, .next = cx};
        gm_b16_8 t10 = gm_l6;
        gm_b0_1 t11 = gm_f_lines_9uns0k2_sersjf1((void *)NULL, (&wn7), NULL, t10);
        if (gm_unw.kind) {
            goto gm_ll9;
        }
        F.w0 = t11;
        goto gm_ls12;
gm_ll9:;
        if (gm_unw.kind == GM_BREAK && gm_unw.frame == &F.h && gm_unw.label == 0) { gm_unw.kind = GM_NONE; goto we8; }
        goto gm_lad;
gm_ls12:;
we8:;
        gm_b0_1 t13 = F.w0;
        gm_b0_1 t14 = t13;
    }
    {
        gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&F.l2) + 0));
        gm_b16_8 t16 = t15;
        gm_b16_8 t17 = GM_LD(gm_b16_8, ((uint8_t *)&t16));
        uint8_t t18 = 0;
        F.ret = t17;
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
extern __attribute__((aligned(4))) gm_b16_8 gm_f_unique_members_mrojmjf(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, void * a1);
extern __attribute__((aligned(4))) gm_b16_8 gm_f_unique_view_members_3bvsjcm(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, void * a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_unique_with_mffff7e(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, void * a1, uint8_t a2) {
    struct gm_fr_unique_with_mffff7e F;
    gm_b16_8 gm_l0; /* names */
    void * gm_l1; /* taken */
    uint8_t gm_l2; /* view */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    gm_b16_8 gm_l6; /* n */
    uint32_t gm_l7; /* c */
    gm_b16_8 gm_l9; /* <tmp> */
    uint64_t gm_l10; /* <tmp> */
    gm_b16_8 gm_l11; /* n */
    gm_th2lqbo77pvsc gm_l12; /* m */
    gm_b16_8 gm_l13; /* name */
    GM_STACK_CHECK(&F, 855u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        gm_f_hash_map_rs5nlfs((void *)&F.l3, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t3 = gm_l0;
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
                    gm_b16_8 t15 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t11 * 16));
                    gm_l6 = t15;
                    {
                        void * t17 = (void *)((uint8_t *)&F.l3);
                        gm_b16_8 t18 = gm_l6;
                        gm_t9805qtonbsag t19 = gm_f_get_m1knqqk(t17, t18);
                        gm_t9805qtonbsag t20 = t19;
                        uint32_t t21;
                        if (((((uint8_t *)&t20))[4] != 0)) {
                            uint32_t t22 = GM_LD(uint32_t, (((uint8_t *)&t20) + 0));
                            t21 = t22;
                        } else {
                            t21 = ((uint32_t)0ULL);
                        }
                        gm_l7 = t21;
                    }
                    {
                        void * t23 = (void *)((uint8_t *)&F.l3);
                        gm_b16_8 t24 = gm_l6;
                        uint32_t t25 = gm_l7;
                        uint32_t r26; bool o27 = __builtin_add_overflow(t25, ((uint32_t)1ULL), &r26);
                        if (o27) gm_trap("integer overflow in `+`", gm_srce5lfiiur8udn, 136, 22);
                        uint32_t t28 = r26;
                        gm_tftb1odc739t5 t29 = gm_f_put_074448v(t23, t24, t28);
                        gm_tftb1odc739t5 t30 = t29;
                        if (((((uint8_t *)&t30))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t30) + 1, gm_srce5lfiiur8udn, 136, 9);
                        gm_b0_1 t31 = GM_LD(gm_b0_1, (((uint8_t *)&t30) + 1));
                    }
                    goto me13;
mn14:;
                    gm_trap("no match arm matched the scrutinee", gm_srce5lfiiur8udn, 134, 5);
me13:;
                }
lc4:;
                {
                    uint64_t t33 = gm_l5;
                    uint64_t r34; bool o35 = __builtin_add_overflow(t33, ((uint64_t)1ULL), &r34);
                    uint64_t t36 = r34;
                    gm_l5 = t36;
                }
            }
lb4:;
        }
    }
    {
        gm_f_list_0c2v515((void *)&F.l8, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t38 = gm_l0;
            gm_l9 = t38;
        }
        {
            gm_l10 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t40 = gm_l10;
                uint64_t t41 = GM_LD(uintptr_t, ((uint8_t *)&gm_l9) + 8);
                uint8_t t42 = (t40 < t41);
                if (!t42) goto lb39;
                {
                    uint64_t t45 = gm_l10;
                    uint64_t t46 = (uintptr_t)t45;
                    uint64_t t47 = GM_LD(uintptr_t, ((uint8_t *)&gm_l9) + 8);
                    gm_b16_8 t50 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l9)) + t46 * 16));
                    gm_l11 = t50;
                    {
                        gm_th2lqbo77pvsc t52;
                        memset(&t52, 0, sizeof t52);
                        gm_l12 = t52;
                    }
                    {
                        uint8_t t54 = gm_l2;
                        if (!t54) goto ie53;
                        uint8_t t55 = 0;
                        uint8_t t56 = (((GM_LD(uintptr_t, ((uint8_t *)&gm_l11) + 8) == 1) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l11)), gm_s588gob2um10i, 1) == 0));
                        if (!t56) {
                            uint8_t t57 = (((GM_LD(uintptr_t, ((uint8_t *)&gm_l11) + 8) == 3) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l11)), gm_s1k3lfare8sei, 3) == 0));
                            t55 = t57;
                        } else t55 = 1;
                        if (!t55) goto ie53;
                        {
                            gm_b16_8 t59 = gm_l11;
                            gm_th2lqbo77pvsc t60;
                            GM_ST(uint8_t, (uint8_t *)&t60 + 17, 0);
                            GM_ST(uint16_t, (uint8_t *)&t60 + 18, 0);
                            GM_ST(uint32_t, (uint8_t *)&t60 + 20, 0);
                            GM_ST(gm_b16_8, ((uint8_t *)&t60), t59);
                            ((uint8_t *)&t60)[16] = 1;
                            gm_l12 = t60;
                        }
                        goto ix53;
ie53:;
                        {
                            gm_b16_8 t62 = gm_l11;
                            gm_f_member_name_mj2bvs3((void *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l12)), (void *)NULL, cx, NULL, t62);
                            if (gm_unw.kind) {
                                goto gm_ll43;
                            }
                        }
ix53:;
                    }
                    {
                        gm_th2lqbo77pvsc t63 = gm_l12;
                        gm_th2lqbo77pvsc t64 = t63;
                        gm_b16_8 t65;
                        if (((((uint8_t *)&t64))[16] != 0)) {
                            gm_b16_8 t66 = GM_LD(gm_b16_8, (((uint8_t *)&t64) + 0));
                            t65 = t66;
                        } else {
                            {
                                void * t68 = (void *)((uint8_t *)&F.l8);
                                gm_th2lqbo77pvsc t69;
                                memset(&t69, 0, sizeof t69);
                                gm_tftb1odc739t5 t70 = gm_f_push_d0271rf(t68, &t69);
                                gm_tftb1odc739t5 t71 = t70;
                                if (((((uint8_t *)&t71))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t71) + 1, gm_srce5lfiiur8udn, 143, 13);
                                gm_b0_1 t72 = GM_LD(gm_b0_1, (((uint8_t *)&t71) + 1));
                            }
                            goto lc39;
                        }
                        gm_l13 = t65;
                    }
                    {
                        uint8_t t74 = 0;
                        void * t75 = (void *)((uint8_t *)&F.l3);
                        gm_b16_8 t76 = gm_l11;
                        gm_t9805qtonbsag t77 = gm_f_get_m1knqqk(t75, t76);
                        gm_t9805qtonbsag t78 = t77;
                        uint32_t t79;
                        if (((((uint8_t *)&t78))[4] != 0)) {
                            uint32_t t80 = GM_LD(uint32_t, (((uint8_t *)&t78) + 0));
                            t79 = t80;
                        } else {
                            t79 = ((uint32_t)0ULL);
                        }
                        uint8_t t81 = (t79 > ((uint32_t)1ULL));
                        if (!t81) {
                            void * t82 = gm_l1;
                            gm_b16_8 t83 = gm_l13;
                            gm_tqs5p4n2tq19v t84 = gm_f_get_p217beu(t82, t83);
                            gm_tqs5p4n2tq19v t85 = t84;
                            uint8_t t86;
                            if (((((uint8_t *)&t85))[1] != 0)) goto mn88;
                            t86 = ((uint8_t)0);
                            goto me87;
mn88:;
                            t86 = ((uint8_t)1);
                            goto me87;
mn89:;
                            gm_trap("no match arm matched the scrutinee", gm_srce5lfiiur8udn, 146, 41);
me87:;
                            t74 = t86;
                        } else t74 = 1;
                        if (!t74) goto ie73;
                        {
                            void * t91 = (void *)((uint8_t *)&F.l8);
                            gm_th2lqbo77pvsc t92;
                            memset(&t92, 0, sizeof t92);
                            gm_tftb1odc739t5 t93 = gm_f_push_d0271rf(t91, &t92);
                            gm_tftb1odc739t5 t94 = t93;
                            if (((((uint8_t *)&t94))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t94) + 1, gm_srce5lfiiur8udn, 147, 13);
                            gm_b0_1 t95 = GM_LD(gm_b0_1, (((uint8_t *)&t94) + 1));
                        }
                        {
                            goto lc39;
                        }
                        goto ix73;
ie73:;
ix73:;
                    }
                    {
                        void * t96 = gm_l1;
                        gm_b16_8 t97 = gm_l13;
                        gm_tftb1odc739t5 t98 = gm_f_put_5c6qgui(t96, t97, ((uint8_t)1));
                        gm_tftb1odc739t5 t99 = t98;
                        if (((((uint8_t *)&t99))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t99) + 1, gm_srce5lfiiur8udn, 150, 9);
                        gm_b0_1 t100 = GM_LD(gm_b0_1, (((uint8_t *)&t99) + 1));
                    }
                    {
                        void * t101 = (void *)((uint8_t *)&F.l8);
                        gm_b16_8 t102 = gm_l13;
                        gm_th2lqbo77pvsc t103;
                        GM_ST(uint8_t, (uint8_t *)&t103 + 17, 0);
                        GM_ST(uint16_t, (uint8_t *)&t103 + 18, 0);
                        GM_ST(uint32_t, (uint8_t *)&t103 + 20, 0);
                        GM_ST(gm_b16_8, ((uint8_t *)&t103), t102);
                        ((uint8_t *)&t103)[16] = 1;
                        gm_tftb1odc739t5 t104 = gm_f_push_d0271rf(t101, &t103);
                        gm_tftb1odc739t5 t105 = t104;
                        if (((((uint8_t *)&t105))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t105) + 1, gm_srce5lfiiur8udn, 151, 9);
                        gm_b0_1 t106 = GM_LD(gm_b0_1, (((uint8_t *)&t105) + 1));
                    }
                    goto me48;
mn49:;
                    gm_trap("no match arm matched the scrutinee", gm_srce5lfiiur8udn, 139, 5);
me48:;
                }
lc39:;
                {
                    uint64_t t108 = gm_l10;
                    uint64_t r109; bool o110 = __builtin_add_overflow(t108, ((uint64_t)1ULL), &r109);
                    uint64_t t111 = r109;
                    gm_l10 = t111;
                }
            }
gm_ll43:;
            if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb39; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc39; }
            }
            goto gm_lad;
lb39:;
        }
    }
    {
        gm_b16_8 t112 = GM_LD(gm_b16_8, (((uint8_t *)&F.l8) + 0));
        uint8_t t113 = 0;
        F.ret = t112;
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
inline __attribute__((aligned(4))) uint8_t gm_f_write_lldb_formatters_m0rjk7q(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1) {
    struct gm_fr_write_lldb_formatters_m0rjk7q F;
    gm_b16_8 gm_l0; /* exe */
    gm_b16_8 gm_l1; /* formatters */
    gm_b16_8 gm_l2; /* path */
    GM_STACK_CHECK(&F, 204u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        gm_b16_8 t2 = gm_l0;
        gm_b16_8 t3 = gm_f_parent_dir_df7dgp5((void *)NULL, cx, NULL, t2);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b16_8 t4 = GM_LD(gm_b16_8, gm_c23qivcb2qk);
        gm_b16_8 t5 = gm_f_join_qveh5b2((void *)NULL, cx, NULL, t3, t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l2 = t5;
    }
    {
        gm_b16_8 t6 = gm_l2;
        gm_b16_8 t7 = gm_l1;
        gm_ttln0d0hd70en t8 = gm_f_write_all_o8ucaji((void *)NULL, cx, NULL, t6, t7);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_ttln0d0hd70en t9 = t8;
        gm_b0_1 t10;
        if (!((((uint8_t *)&t9))[0] != 0)) {
            gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 4));
            t10 = t11;
        } else {
            {
                gm_b16_8 t13 = gm_l2;
                gm_b16_8 t14 = t13;
                if (gm_pf454qfrpt6r(cx, gm_snekrjln06tfl, 13, gm_srce5lfiiur8udn, 191, 9)) {
                    goto gm_lad;
                }
                if (gm_pfu8dq5bikbh(cx, &gm_ty0iqiubfm7e, ((uint8_t *)&t14), gm_srce5lfiiur8udn, 191, 9)) {
                    goto gm_lad;
                }
                if (gm_pf454qfrpt6r(cx, gm_so514oj5nbhcu, 1, gm_srce5lfiiur8udn, 191, 9)) {
                    goto gm_lad;
                }
            }
            uint8_t t15 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
    }
    {
        uint8_t t16 = 0;
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
