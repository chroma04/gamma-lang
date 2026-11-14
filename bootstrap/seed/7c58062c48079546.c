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
typedef struct gm_csc7lc6jlkqkuq { uint64_t f0; uint64_t f1; } gm_csc7lc6jlkqkuq;
typedef struct gm_csguankko4lmhd { uint32_t f0; } gm_csguankko4lmhd;
typedef struct { int32_t f0; } gm_t19enj90vg8bd;
typedef struct { uint64_t f0; uint64_t f1; } gm_t2lqu8jsj511d;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint64_t f0; int32_t f1; uint8_t pe[4]; } gm_t43v1gep754th;
typedef struct { uint64_t f0; } gm_t4ce0jd3tdro4;
typedef struct { uint64_t f0; uint64_t f1; uint64_t f2; uint64_t f3; } gm_t66j1im9ip128;
typedef struct { uint8_t a[40]; } gm_t6kvj62qe5fp2;
typedef struct { uint8_t a[32]; } gm_t7mlrlibdq1ja;
typedef struct { int64_t a[3]; } gm_t7vno1ieejig6;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint32_t f0; uint8_t f1; uint8_t pe[3]; } gm_t9805qtonbsag;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { float f0; } gm_tdqbns26udv5e;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint8_t a[131]; } gm_tfbi275tn5ccf;
typedef struct { double f0; } gm_tidu2er0d7h95;
typedef struct { uint8_t f0; uint8_t p1[1]; uint16_t f1; } gm_tjcj4j2gbh6c8;
typedef struct { void * f0; void * f1; uint32_t f2; uint32_t f3; } gm_tmm67r2c94d5j;
typedef struct { int64_t f0; int32_t f1; uint8_t pe[4]; } gm_tnur0m5k1ttis;
typedef struct { __int128 f0; } gm_tpl3q6e8s9qo1;
typedef struct { unsigned __int128 f0; } gm_tsn2omssg6cpf;
typedef struct { uint8_t f0; uint8_t p1[3]; uint32_t f1; } gm_ttovhfee8mf28;
_Static_assert(sizeof(gm_csc7lc6jlkqkuq) == 16 && _Alignof(gm_csc7lc6jlkqkuq) == 8 && offsetof(gm_csc7lc6jlkqkuq, f0) == 0 && offsetof(gm_csc7lc6jlkqkuq, f1) == 8, "layout of `BlobPart`");
_Static_assert(sizeof(gm_csguankko4lmhd) == 4 && _Alignof(gm_csguankko4lmhd) == 4 && offsetof(gm_csguankko4lmhd, f0) == 0, "layout of `Mutex`");
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; gm_t9qkh0rsooc8d f2; uint8_t p3[3]; gm_tf5srh4mp4b3i f3; } gm_t0mglksrkfa5e;
_Static_assert(sizeof(gm_t19enj90vg8bd) == 4 && _Alignof(gm_t19enj90vg8bd) == 4 && offsetof(gm_t19enj90vg8bd, f0) == 0, "layout of `(i32,)`");
_Static_assert(sizeof(gm_t2lqu8jsj511d) == 16 && _Alignof(gm_t2lqu8jsj511d) == 8 && offsetof(gm_t2lqu8jsj511d, f0) == 0 && offsetof(gm_t2lqu8jsj511d, f1) == 8, "layout of `(u64, u64)`");
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
_Static_assert(sizeof(gm_t43v1gep754th) == 16 && _Alignof(gm_t43v1gep754th) == 8 && offsetof(gm_t43v1gep754th, f0) == 0 && offsetof(gm_t43v1gep754th, f1) == 8, "layout of `(n: usize, exp: i32)`");
_Static_assert(sizeof(gm_t4ce0jd3tdro4) == 8 && _Alignof(gm_t4ce0jd3tdro4) == 8 && offsetof(gm_t4ce0jd3tdro4, f0) == 0, "layout of `Instant`");
typedef struct { uint64_t f0; gm_t9qkh0rsooc8d f1; uint8_t pe[7]; } gm_t617vr72bd7b3;
_Static_assert(sizeof(gm_t66j1im9ip128) == 32 && _Alignof(gm_t66j1im9ip128) == 8 && offsetof(gm_t66j1im9ip128, f0) == 0 && offsetof(gm_t66j1im9ip128, f1) == 8 && offsetof(gm_t66j1im9ip128, f2) == 16 && offsetof(gm_t66j1im9ip128, f3) == 24, "layout of `(types: usize, values: usize, fns: usize, consts: usize)`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
_Static_assert(sizeof(gm_t6kvj62qe5fp2) == 40 && _Alignof(gm_t6kvj62qe5fp2) == 1 && offsetof(gm_t6kvj62qe5fp2, a) == 0, "layout of `[40]u8`");
_Static_assert(sizeof(gm_t7mlrlibdq1ja) == 32 && _Alignof(gm_t7mlrlibdq1ja) == 1 && offsetof(gm_t7mlrlibdq1ja, a) == 0, "layout of `[32]u8`");
_Static_assert(sizeof(gm_t7vno1ieejig6) == 24 && _Alignof(gm_t7vno1ieejig6) == 8 && offsetof(gm_t7vno1ieejig6, a) == 0, "layout of `[3]i64`");
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
_Static_assert(sizeof(gm_t9805qtonbsag) == 8 && _Alignof(gm_t9805qtonbsag) == 4 && offsetof(gm_t9805qtonbsag, f0) == 0 && offsetof(gm_t9805qtonbsag, f1) == 4, "layout of `?u32`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
typedef struct { gm_b16_8 f0; uint32_t f1; uint32_t f2; gm_b16_8 f3; } gm_tb40qn7hk0tl9;
typedef struct { uint64_t f0; uint64_t f1; gm_b16_8 f2; } gm_tbccvikg6b96e;
typedef struct { uint8_t f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; } gm_tbngattl1fo51;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tcmkompbl09il;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; gm_t88rcucg1b7uk f2; } gm_tcmtrmhj7ak4u;
_Static_assert(sizeof(gm_tdqbns26udv5e) == 4 && _Alignof(gm_tdqbns26udv5e) == 4 && offsetof(gm_tdqbns26udv5e, f0) == 0, "layout of `(f32,)`");
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
_Static_assert(sizeof(gm_tfbi275tn5ccf) == 131 && _Alignof(gm_tfbi275tn5ccf) == 1 && offsetof(gm_tfbi275tn5ccf, a) == 0, "layout of `[131]u8`");
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_ttovhfee8mf28 f0; uint8_t f1; uint8_t pe[3]; } gm_tg5lql6itufk7;
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; } gm_tgglga8ro3isc;
_Static_assert(sizeof(gm_tidu2er0d7h95) == 8 && _Alignof(gm_tidu2er0d7h95) == 8 && offsetof(gm_tidu2er0d7h95, f0) == 0, "layout of `(f64,)`");
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
_Static_assert(sizeof(gm_tjcj4j2gbh6c8) == 4 && _Alignof(gm_tjcj4j2gbh6c8) == 2 && offsetof(gm_tjcj4j2gbh6c8, f0) == 0 && offsetof(gm_tjcj4j2gbh6c8, f1) == 2, "layout of `(signed: bool, bits: u16)`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t f1; uint8_t pe[3]; } gm_tkn42u7na3t81;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; } f1; } gm_tl572d7579hns;
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; uint8_t pe[4]; } gm_tlcm4lvu3g1a8;
_Static_assert(sizeof(gm_tmm67r2c94d5j) == 24 && _Alignof(gm_tmm67r2c94d5j) == 8 && offsetof(gm_tmm67r2c94d5j, f0) == 0 && offsetof(gm_tmm67r2c94d5j, f1) == 8 && offsetof(gm_tmm67r2c94d5j, f2) == 16 && offsetof(gm_tmm67r2c94d5j, f3) == 20, "layout of `Printer`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t88rcucg1b7uk v0; } f1; } gm_tnnjeerolos0a;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
_Static_assert(sizeof(gm_tnur0m5k1ttis) == 16 && _Alignof(gm_tnur0m5k1ttis) == 8 && offsetof(gm_tnur0m5k1ttis, f0) == 0 && offsetof(gm_tnur0m5k1ttis, f1) == 8, "layout of `(i64, i32)`");
_Static_assert(sizeof(gm_tpl3q6e8s9qo1) == 16 && _Alignof(gm_tpl3q6e8s9qo1) == 16 && offsetof(gm_tpl3q6e8s9qo1, f0) == 0, "layout of `(i128,)`");
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
_Static_assert(sizeof(gm_tsn2omssg6cpf) == 16 && _Alignof(gm_tsn2omssg6cpf) == 16 && offsetof(gm_tsn2omssg6cpf, f0) == 0, "layout of `(u128,)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t4ce0jd3tdro4 v0; } f1; } gm_ttiiv5vhiuvvr;
_Static_assert(sizeof(gm_ttovhfee8mf28) == 8 && _Alignof(gm_ttovhfee8mf28) == 4 && offsetof(gm_ttovhfee8mf28, f0) == 0 && offsetof(gm_ttovhfee8mf28, f1) == 4, "layout of `(signed: bool, bits: u32)`");
_Static_assert(sizeof(gm_t0mglksrkfa5e) == 40 && _Alignof(gm_t0mglksrkfa5e) == 8 && offsetof(gm_t0mglksrkfa5e, f0) == 0 && offsetof(gm_t0mglksrkfa5e, f1) == 16 && offsetof(gm_t0mglksrkfa5e, f2) == 20 && offsetof(gm_t0mglksrkfa5e, f3) == 24, "layout of `FieldDef`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tcmkompbl09il v0; gm_tiilmfd16v3fc v1; gm_b16_8 v2; } f1; } gm_t428ue2u3hf5g;
_Static_assert(sizeof(gm_t617vr72bd7b3) == 16 && _Alignof(gm_t617vr72bd7b3) == 8 && offsetof(gm_t617vr72bd7b3, f0) == 0 && offsetof(gm_t617vr72bd7b3, f1) == 8, "layout of `DirEntry`");
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; gm_tnnjeerolos0a f2; uint8_t pe[4]; } gm_t8nckeqfenl8a;
typedef struct { uint64_t f0; gm_t0mglksrkfa5e f1; } gm_t8ta482bl6be0;
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
_Static_assert(sizeof(gm_tb40qn7hk0tl9) == 40 && _Alignof(gm_tb40qn7hk0tl9) == 8 && offsetof(gm_tb40qn7hk0tl9, f0) == 0 && offsetof(gm_tb40qn7hk0tl9, f1) == 16 && offsetof(gm_tb40qn7hk0tl9, f2) == 20 && offsetof(gm_tb40qn7hk0tl9, f3) == 24, "layout of `SourceLocation`");
_Static_assert(sizeof(gm_tbccvikg6b96e) == 32 && _Alignof(gm_tbccvikg6b96e) == 8 && offsetof(gm_tbccvikg6b96e, f0) == 0 && offsetof(gm_tbccvikg6b96e, f1) == 8 && offsetof(gm_tbccvikg6b96e, f2) == 16, "layout of `Layout`");
_Static_assert(sizeof(gm_tbngattl1fo51) == 8 && _Alignof(gm_tbngattl1fo51) == 4 && offsetof(gm_tbngattl1fo51, f0) == 0 && offsetof(gm_tbngattl1fo51, f1) == 4, "layout of `(is_const: bool, pointee: Ty)`");
_Static_assert(sizeof(gm_tcmkompbl09il) == 24 && _Alignof(gm_tcmkompbl09il) == 8 && offsetof(gm_tcmkompbl09il, f0) == 0 && offsetof(gm_tcmkompbl09il, f1) == 8, "layout of `(ty: Ty, bytes: string)`");
_Static_assert(sizeof(gm_tcmtrmhj7ak4u) == 12 && _Alignof(gm_tcmtrmhj7ak4u) == 4 && offsetof(gm_tcmtrmhj7ak4u, f0) == 0 && offsetof(gm_tcmtrmhj7ak4u, f1) == 4 && offsetof(gm_tcmtrmhj7ak4u, f2) == 8, "layout of `(op: LenOp, lhs: Ty, rhs: Ty)`");
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t p2[4]; gm_tnpifv20irnph f2; uint8_t f3; uint8_t pe[7]; } gm_tddtjfggdat0d;
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_tg5lql6itufk7) == 12 && _Alignof(gm_tg5lql6itufk7) == 4 && offsetof(gm_tg5lql6itufk7, f0) == 0 && offsetof(gm_tg5lql6itufk7, f1) == 8, "layout of `?(signed: bool, bits: u32)`");
_Static_assert(sizeof(gm_tgglga8ro3isc) == 8 && _Alignof(gm_tgglga8ro3isc) == 4 && offsetof(gm_tgglga8ro3isc, f0) == 0 && offsetof(gm_tgglga8ro3isc, f1) == 4, "layout of `(err: Ty, ok: Ty)`");
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
_Static_assert(sizeof(gm_tkn42u7na3t81) == 8 && _Alignof(gm_tkn42u7na3t81) == 4 && offsetof(gm_tkn42u7na3t81, f0) == 0 && offsetof(gm_tkn42u7na3t81, f1) == 4, "layout of `?Ty`");
_Static_assert(sizeof(gm_tl572d7579hns) == 24 && _Alignof(gm_tl572d7579hns) == 8 && offsetof(gm_tl572d7579hns, f0) == 0 && offsetof(gm_tl572d7579hns, f1) == 8, "layout of `string!()`");
_Static_assert(sizeof(gm_tlcm4lvu3g1a8) == 16 && _Alignof(gm_tlcm4lvu3g1a8) == 8 && offsetof(gm_tlcm4lvu3g1a8, f0) == 0 && offsetof(gm_tlcm4lvu3g1a8, f1) == 8, "layout of `(i: usize, x: Ty)`");
_Static_assert(sizeof(gm_tnnjeerolos0a) == 8 && _Alignof(gm_tnnjeerolos0a) == 4 && offsetof(gm_tnnjeerolos0a, f0) == 0 && offsetof(gm_tnnjeerolos0a, f1) == 4, "layout of `ArrayKind`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t p2[7]; gm_tf5srh4mp4b3i f2; gm_tkn42u7na3t81 f3; } gm_tpautr4br7h3l;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t2lqu8jsj511d v0; gm_t617vr72bd7b3 v1; gm_t66j1im9ip128 v2; gm_t2lqu8jsj511d v3; uint64_t v4; gm_t617vr72bd7b3 v5; } f1; } gm_tpekhfs32op0f;
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_ttiiv5vhiuvvr v0; double v1; } f1; } gm_tspt3vn9ljcnd;
_Static_assert(sizeof(gm_ttiiv5vhiuvvr) == 16 && _Alignof(gm_ttiiv5vhiuvvr) == 8 && offsetof(gm_ttiiv5vhiuvvr, f0) == 0 && offsetof(gm_ttiiv5vhiuvvr, f1) == 8, "layout of `ParseError`");
_Static_assert(sizeof(gm_t428ue2u3hf5g) == 32 && _Alignof(gm_t428ue2u3hf5g) == 8 && offsetof(gm_t428ue2u3hf5g, f0) == 0 && offsetof(gm_t428ue2u3hf5g, f1) == 8, "layout of `ConstKey`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tjcj4j2gbh6c8 v0; uint16_t v1; gm_tbngattl1fo51 v2; gm_tbngattl1fo51 v3; gm_tbngattl1fo51 v4; gm_t8nckeqfenl8a v5; gm_t6j6smj7qoctu v6; gm_t88rcucg1b7uk v7; gm_tgglga8ro3isc v8; gm_tddtjfggdat0d v9; gm_tddtjfggdat0d v10; gm_tiilmfd16v3fc v11; gm_t88rcucg1b7uk v12; uint32_t v13; gm_tgglga8ro3isc v14; gm_t428ue2u3hf5g v15; gm_tcmtrmhj7ak4u v16; uint32_t v17; gm_tnpifv20irnph v18; uint32_t v19; } f1; } gm_t53at80pfdus3;
_Static_assert(sizeof(gm_t8nckeqfenl8a) == 24 && _Alignof(gm_t8nckeqfenl8a) == 8 && offsetof(gm_t8nckeqfenl8a, f0) == 0 && offsetof(gm_t8nckeqfenl8a, f1) == 8 && offsetof(gm_t8nckeqfenl8a, f2) == 12, "layout of `(len: u64, elem: Ty, kind: ArrayKind)`");
_Static_assert(sizeof(gm_t8ta482bl6be0) == 48 && _Alignof(gm_t8ta482bl6be0) == 8 && offsetof(gm_t8ta482bl6be0, f0) == 0 && offsetof(gm_t8ta482bl6be0, f1) == 8, "layout of `(i: usize, x: FieldDef)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tpekhfs32op0f v0; gm_b16_8 v1; } f1; } gm_t934k1j7u70ct;
_Static_assert(sizeof(gm_tddtjfggdat0d) == 48 && _Alignof(gm_tddtjfggdat0d) == 8 && offsetof(gm_tddtjfggdat0d, f0) == 0 && offsetof(gm_tddtjfggdat0d, f1) == 16 && offsetof(gm_tddtjfggdat0d, f2) == 24 && offsetof(gm_tddtjfggdat0d, f3) == 40, "layout of `FnTy`");
_Static_assert(sizeof(gm_tpautr4br7h3l) == 48 && _Alignof(gm_tpautr4br7h3l) == 8 && offsetof(gm_tpautr4br7h3l, f0) == 0 && offsetof(gm_tpautr4br7h3l, f1) == 16 && offsetof(gm_tpautr4br7h3l, f2) == 24 && offsetof(gm_tpautr4br7h3l, f3) == 40, "layout of `StructDef`");
_Static_assert(sizeof(gm_tpekhfs32op0f) == 40 && _Alignof(gm_tpekhfs32op0f) == 8 && offsetof(gm_tpekhfs32op0f, f0) == 0 && offsetof(gm_tpekhfs32op0f, f1) == 8, "layout of `MemError`");
_Static_assert(sizeof(gm_tspt3vn9ljcnd) == 24 && _Alignof(gm_tspt3vn9ljcnd) == 8 && offsetof(gm_tspt3vn9ljcnd, f0) == 0 && offsetof(gm_tspt3vn9ljcnd, f1) == 8, "layout of `ParseError!f64`");
_Static_assert(sizeof(gm_t53at80pfdus3) == 56 && _Alignof(gm_t53at80pfdus3) == 8 && offsetof(gm_t53at80pfdus3, f0) == 0 && offsetof(gm_t53at80pfdus3, f1) == 8, "layout of `TyKind`");
_Static_assert(sizeof(gm_t934k1j7u70ct) == 48 && _Alignof(gm_t934k1j7u70ct) == 8 && offsetof(gm_t934k1j7u70ct, f0) == 0 && offsetof(gm_t934k1j7u70ct, f1) == 8, "layout of `MemError![]u8`");
struct gm_fr_aligned_q6sor4h {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_dec_395otau {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ijtp9lo {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_fill_abmf5g5 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fixed_buf_len_gor7a4d {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_fmt_adt_p6448k2 {
    gm_frame h;
    gm_tpautr4br7h3l l5; /* def */
    gm_t0mglksrkfa5e l10; /* f */
    gm_tl572d7579hns ret;
};
struct gm_fr_fmt_float_1ivmudg {
    gm_frame h;
    gm_t6kvj62qe5fp2 l3; /* dig */
    gm_b0_1 ret;
};
struct gm_fr_fmt_float_bits_9d6f5r5 {
    gm_frame h;
    double ret;
};
struct gm_fr_fmt_float_spec_glt99ob {
    gm_frame h;
    gm_t6kvj62qe5fp2 l9; /* dig */
    gm_b0_1 ret;
};
struct gm_fr_fmt_int_svhqra7 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fmt_pad_ev4lnnu {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fmt_pow2_1p38akk {
    gm_frame h;
    double ret;
};
struct gm_fr_fmt_quote_f7sotri {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fmt_seq_c3i8abi {
    gm_frame h;
    gm_tl572d7579hns ret;
};
struct gm_fr_fmt_shortest16_cln2heq {
    gm_frame h;
    gm_t43v1gep754th ret;
};
struct gm_fr_fmt_shortest_2l54as9 {
    gm_frame h;
    gm_t43v1gep754th ret;
};
struct gm_fr_fmt_split_exp_0kapfov {
    gm_frame h;
    gm_t43v1gep754th ret;
};
struct gm_fr_fmt_valid_utf8_9mug7al {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_format_spec_7aph60t {
    gm_frame h;
    gm_trqfvmjtt4sij l5; /* body */
    gm_tfbi275tn5ccf l12; /* buf */
    gm_trqfvmjtt4sij l17; /* head */
    gm_tl572d7579hns ret;
};
struct gm_fr_format_value_vmle7qr {
    gm_frame h;
    gm_tpekhfs32op0f l11; /* e */
    gm_tl572d7579hns ret;
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
struct gm_fr_layout_of_e6hfj1r {
    gm_frame h;
    gm_tbccvikg6b96e ret;
};
struct gm_fr_list_uvfu1k1 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_mark_kpppm1j {
    gm_frame h;
    gm_t2lqu8jsj511d ret;
};
struct gm_fr_offsets_qpt7c30 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_payload_offset_la0c8ft {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_printer_h7jigd7 {
    gm_frame h;
    gm_tmm67r2c94d5j ret;
};
struct gm_fr_ptr_of_b77b4c9 {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_rd_word_8flcq6o {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_read_u6r4vns {
    gm_frame h;
    gm_t934k1j7u70ct ret;
};
struct gm_fr_release_jn5kqdi {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_res_is_err_8jbgndp {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_sci_buf_len_tedn1r4 {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_show_8uijkbs {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_show_kmjeh8s {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* out */
    gm_b16_8 ret;
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
inline bool gm_eq929lennfd3(const uint8_t *a, const uint8_t *b);
inline bool gm_eq9oqmmiu8qc(const uint8_t *a, const uint8_t *b);
inline bool gm_eqo3v9599nus(const uint8_t *a, const uint8_t *b);
__attribute__((aligned(4))) void * gm_f_adt_of_8k5vqud(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline uint8_t gm_f_aligned_q6sor4h(uint64_t a0, uint64_t a1);
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_bits_struct_ty_22ue20s(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_dec_395otau(int64_t a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_deinit_r70pji5(void * a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_extend_0cs85gs(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_fault_u31b92l(void *gm_out, void * a0, uint64_t a1, uint64_t a2, uint64_t a3, uint8_t a4);
__attribute__((aligned(4))) gm_b16_8 gm_f_field_bytes_bhivr4s(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, uint32_t a3);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_fill_abmf5g5(gm_b16_8 a0, uint8_t a1);
inline __attribute__((aligned(4))) uint64_t gm_f_fixed_buf_len_gor7a4d(uint32_t a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_fixed_rppim5r(double a0, uint32_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_fmt_adt_p6448k2(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3);
__attribute__((aligned(4))) gm_b0_1 gm_f_fmt_float_1ivmudg(void *env, gm_node *cx, gm_node *inst, void * a0, double a1, uint16_t a2);
__attribute__((aligned(4))) double gm_f_fmt_float_bits_9d6f5r5(void * a0, uint16_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_fmt_float_spec_glt99ob(void *env, gm_node *cx, gm_node *inst, void * a0, double a1, uint16_t a2, void * a3);
__attribute__((aligned(4))) gm_b0_1 gm_f_fmt_int_svhqra7(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3);
__attribute__((aligned(4))) gm_b0_1 gm_f_fmt_pad_ev4lnnu(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2, uint32_t a3, uint8_t a4, gm_t9qkh0rsooc8d a5, uint8_t a6, uint8_t a7);
inline __attribute__((aligned(4))) double gm_f_fmt_pow2_1p38akk(int32_t a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_fmt_quote_f7sotri(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_fmt_seq_c3i8abi(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, uint64_t a3, gm_b16_8 a4, uint64_t a5);
__attribute__((aligned(4))) gm_t43v1gep754th gm_f_fmt_shortest16_cln2heq(void *env, gm_node *cx, gm_node *inst, double a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_t43v1gep754th gm_f_fmt_shortest_2l54as9(void *env, gm_node *cx, gm_node *inst, double a0, uint16_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_t43v1gep754th gm_f_fmt_split_exp_0kapfov(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) uint8_t gm_f_fmt_valid_utf8_9mug7al(gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_format_13oum9l(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnur0m5k1ttis a0);
__attribute__((aligned(4))) void gm_f_format_eucvbnh(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tidu2er0d7h95 a0);
__attribute__((aligned(4))) void gm_f_format_h8e7ps1(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_odb8fco(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tdqbns26udv5e a0);
__attribute__((aligned(4))) void gm_f_format_spec_7aph60t(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3, void * a4);
__attribute__((aligned(4))) void gm_f_format_value_vmle7qr(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3, uint8_t a4);
__attribute__((aligned(4))) gm_b0_1 gm_f_free_rkuh2rm(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, const void * a1);
__attribute__((aligned(4))) unsigned __int128 gm_f_get_int_pg7p4gc(void * a0, gm_csguankko4lmhd a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) uint64_t gm_f_get_u64_tgpb2vh(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) unsigned __int128 gm_f_get_uint_bq8c4b7(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tg5lql6itufk7 gm_f_int_info_ulcevdv(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_invalid_tag_text_f3l5hi7(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) void gm_f_kind_htv6093(void *gm_out, void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void gm_f_kind_jcnt4hm(void *gm_out, void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void gm_f_layout_a5ovnrm(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) void gm_f_layout_of_e6hfj1r(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_make_fill_jpvm0kc(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint64_t a0, uint8_t a1, const void * a2);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_csc7lc6jlkqkuq gm_f_mark_kpppm1j(void * a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_mem_message_e0go901(void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_offsets_qpt7c30(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_opt_payload_1pbnobb(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
__attribute__((aligned(4))) uint8_t gm_f_opt_present_lb4i2ip(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_parse_float_17hgksa(void *gm_out, gm_b16_8 a0);
inline __attribute__((aligned(4))) uint64_t gm_f_payload_offset_la0c8ft(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) void gm_f_printer_h7jigd7(void *gm_out, void * a0, void * a1);
inline __attribute__((aligned(4))) uint64_t gm_f_ptr_of_b77b4c9(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_t83rg2m(void * a0, uint8_t a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_radix_with_e6f5j99(int64_t a0, uint32_t a1, uint8_t a2, uint8_t a3, gm_b16_8 a4);
__attribute__((aligned(4))) gm_b16_8 gm_f_radix_with_qaamfpk(unsigned __int128 a0, uint32_t a1, uint8_t a2, uint8_t a3, gm_b16_8 a4);
__attribute__((aligned(4))) __attribute__((always_inline)) inline uint64_t gm_f_rd_word_8flcq6o(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_read_field_ua0742c(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, uint32_t a3, gm_b16_8 a4);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_read_u6r4vns(void *gm_out, void * a0, uint64_t a1, uint64_t a2, uint64_t a3);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_release_jn5kqdi(void * a0, gm_csc7lc6jlkqkuq a1);
inline __attribute__((aligned(4))) uint8_t gm_f_res_is_err_8jbgndp(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b16_8 gm_f_res_payload_pb2hcmh(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) uint64_t gm_f_sci_buf_len_tedn1r4(uint32_t a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_sci_oikoiqs(double a0, uint32_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_8uijkbs(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_kmjeh8s(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) uint64_t gm_f_size_hr4b76n(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_t2lqu8jsj511d gm_f_slice_parts_1se5hu4(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_b16_8 gm_f_tmp_8k6u9c9(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
__attribute__((aligned(4))) __attribute__((noinline)) gm_b16_8 gm_f_tmp_next_dalgo1f(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_ty_vka2r8h(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline uint64_t gm_f_us_cuv01fp(uint64_t a0);
__attribute__((aligned(4))) gm_t9805qtonbsag gm_f_variant_index_s2p6fs4(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void * gm_f_view_qalkbv1(void * a0, uint64_t a1, uint64_t a2, uint8_t a3);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_1599ben(void * a0, gm_tsn2omssg6cpf a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_cnos3th(void * a0, gm_tpl3q6e8s9qo1 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_fsqql49(void * a0, gm_t9qkh0rsooc8d a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_jkg06fo(void * a0, gm_t19enj90vg8bd a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_jvie1fn(void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_kvqtui9(void * a0, gm_t4ce0jd3tdro4 a1);
extern uint8_t gm_c6p4chcutca[8] __attribute__((aligned(8)));
extern uint8_t gm_k30q9glr4ue[40] __attribute__((aligned(8)));
extern uint8_t gm_k3pvi109nh9[40] __attribute__((aligned(8)));
extern uint8_t gm_ka73u23j5nf[40] __attribute__((aligned(8)));
extern uint8_t gm_kbfuu8gel4m[40] __attribute__((aligned(8)));
extern uint8_t gm_kkpcsaqkj4k[40] __attribute__((aligned(8)));
extern uint8_t gm_km7a24is5mf[40] __attribute__((aligned(8)));
extern const uint8_t gm_s22g482qk7a40[3];
extern const uint8_t gm_s2v5amvfkfoll[3];
extern const uint8_t gm_s3p8gug7bvl1p[3];
extern const uint8_t gm_s6gt8b7hsisgu[3];
extern const uint8_t gm_s7cvig9qav6l5[3];
extern const uint8_t gm_s9cb6j5ok7bdk[3];
extern const uint8_t gm_s9lclteeapkg2[3];
extern const uint8_t gm_sdc2n4tsqs8nn[4];
extern const uint8_t gm_sdk5n6a42f7nl[3];
extern const uint8_t gm_segch6a5dme9u[5];
extern const uint8_t gm_sehko5bb96s6p[5];
extern const uint8_t gm_sfa86erpod86s[4];
extern const uint8_t gm_sfdbfrv46lihd[2];
extern const uint8_t gm_sg5gdnrith2i6[3];
extern const uint8_t gm_sicgmpca63jln[2];
extern const uint8_t gm_sjq55q61p743j[3];
extern const uint8_t gm_slgmqgkioa4u6[3];
extern const uint8_t gm_smee1janbl2t9[3];
extern const uint8_t gm_sns75reqq32d2[4];
extern const uint8_t gm_so4mj8r7k5aek[3];
extern const uint8_t gm_sogehh8t1m7tn[6];
extern const uint8_t gm_sqp26euqk4rkd[16];
extern const uint8_t gm_squedv51820an[5];
extern const uint8_t gm_sr4qbrk6tlgaa[1];
extern const char gm_src0ih77f43mhf2[];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_srcc24pnihhgkug[];
extern const char gm_srcfde9lsgge1ld[];
extern const char gm_srcnqtpfma6qe4a[];
extern const char gm_srcpit8mdn0781q[];
extern const char gm_srcq7ohh8h77bn7[];
extern const char gm_srcrgc9bkquf6nr[];
extern const char gm_srcrsgf2gf2b1dq[];
extern const gm_type gm_tyo86ks9qkvt;
inline bool gm_eq929lennfd3(const uint8_t *a, const uint8_t *b) {
    return (a[0] != 0) == (b[0] != 0);
}
inline bool gm_eq9oqmmiu8qc(const uint8_t *a, const uint8_t *b) {
    return GM_LD(uint16_t, a) == GM_LD(uint16_t, b);
}
inline bool gm_eqo3v9599nus(const uint8_t *a, const uint8_t *b) {
    if (!gm_eq929lennfd3(a + 0, b + 0)) return false;
    if (!gm_eq9oqmmiu8qc(a + 2, b + 2)) return false;
    return true;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_dec_395otau(int64_t a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_dec_395otau F;
    int64_t gm_l0; /* x */
    gm_b16_8 gm_l1; /* buf */
    GM_STACK_CHECK_SHALLOW(&F, 96u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    int64_t t1 = gm_l0;
    gm_b16_8 t2 = gm_l1;
    gm_b16_8 t3 = gm_f_radix_with_e6f5j99(t1, ((uint32_t)10ULL), ((uint8_t)0), ((uint8_t)0), t2);
    F.ret = t3;
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
inline __attribute__((aligned(4))) uint64_t gm_f_fixed_buf_len_gor7a4d(uint32_t a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_fixed_buf_len_gor7a4d F;
    uint32_t gm_l0; /* decimals */
    GM_STACK_CHECK_SHALLOW(&F, 40u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint32_t t1 = gm_l0;
    uint64_t t2 = (uint64_t)t1;
    uint64_t r3; bool o4 = __builtin_add_overflow(t2, ((uint64_t)312ULL), &r3);
    if (o4) gm_trap("integer overflow in `+`", gm_srcrsgf2gf2b1dq, 447, 52);
    uint64_t t5 = r3;
    F.ret = t5;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) double gm_f_fmt_pow2_1p38akk(int32_t a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_fmt_pow2_1p38akk F;
    int32_t gm_l0; /* k */
    GM_STACK_CHECK_SHALLOW(&F, 52u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    int32_t t1 = gm_l0;
    int32_t r2; bool o3 = __builtin_add_overflow(t1, ((int32_t)1023LL), &r2);
    if (o3) gm_trap("integer overflow in `+`", gm_srcfde9lsgge1ld, 194, 36);
    int32_t t4 = r2;
    uint64_t t5 = (uint64_t)t4;
    unsigned __int128 n6 = (unsigned __int128)((uint64_t)52ULL);
    if (n6 >= 64) gm_trap_shift((uint64_t)n6, "u64", gm_srcfde9lsgge1ld, 194, 35);
    uint64_t r7 = (uint64_t)((uint64_t)t5 << (int)n6);
    bool o8 = (uint64_t)(r7 >> (int)n6) != t5;
    if (o8) gm_trap("integer overflow in `<<`", gm_srcfde9lsgge1ld, 194, 35);
    uint64_t t9 = r7;
    double t10;
    memset(&t10, 0, sizeof t10); memcpy(&t10, ((uint8_t *)&t9), sizeof t10 < 8 ? sizeof t10 : 8);
    F.ret = t10;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
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
inline __attribute__((aligned(4))) void gm_f_layout_of_e6hfj1r(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_layout_of_e6hfj1r F;
    void * gm_l0; /* lay */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    void * t2 = GM_LD(void *, (((uint8_t *)t1) + 64));
    gm_t88rcucg1b7uk t3 = gm_l1;
    gm_f_layout_a5ovnrm((void *)&(*(gm_tbccvikg6b96e *)gm_out), (void *)NULL, cx, NULL, t2, t3);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_offsets_qpt7c30(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_offsets_qpt7c30 F;
    void * gm_l0; /* m */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 112u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    void * t2 = (void *)(((uint8_t *)t1) + 16);
    gm_t88rcucg1b7uk t3 = gm_l1;
    gm_tbccvikg6b96e t4;
    gm_f_layout_of_e6hfj1r((void *)&t4, (void *)NULL, cx, NULL, t2, t3);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_b16_8 t5 = GM_LD(gm_b16_8, (((uint8_t *)&t4) + 16));
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
inline __attribute__((aligned(4))) uint64_t gm_f_payload_offset_la0c8ft(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_payload_offset_la0c8ft F;
    void * gm_l0; /* m */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_b16_8 gm_l2; /* offs */
    GM_STACK_CHECK(&F, 130u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        gm_t88rcucg1b7uk t3 = gm_l1;
        gm_b16_8 t4 = gm_f_offsets_qpt7c30((void *)NULL, cx, NULL, t2, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l2 = t4;
    }
    {
        uint64_t t5;
        uint64_t t7 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        uint8_t t8 = (t7 > ((uint64_t)1ULL));
        if (!t8) goto ie6;
        uint64_t t10 = (uintptr_t)((uint64_t)1ULL);
        uint64_t t11 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        if (t10 >= (uintptr_t)(t11)) gm_trap_index(t10, t11, gm_srcnqtpfma6qe4a, 231, 30);
        uint64_t t12 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t10 * 8));
        uint64_t t13 = t12;
        t5 = t13;
        goto ix6;
ie6:;
        uint64_t t15 = ((uint64_t)0ULL);
        t5 = t15;
ix6:;
        uint8_t t16 = 0;
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
inline __attribute__((aligned(4))) uint8_t gm_f_res_is_err_8jbgndp(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2) {
    struct gm_fr_res_is_err_8jbgndp F;
    void * gm_l0; /* m */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_b16_8 gm_l2; /* b */
    GM_STACK_CHECK(&F, 124u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_b16_8 t1 = gm_l2;
    gm_b16_8 t2 = t1;
    uint64_t t3 = (uintptr_t)((uint64_t)0ULL);
    void * t4 = gm_l0;
    gm_t88rcucg1b7uk t5 = gm_l1;
    gm_b16_8 t6 = gm_f_offsets_qpt7c30((void *)NULL, cx, NULL, t4, t5);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint64_t t7 = GM_LD(uintptr_t, ((uint8_t *)&t6) + 8);
    if (t3 >= (uintptr_t)(t7)) gm_trap_index(t3, t7, gm_srcnqtpfma6qe4a, 174, 81);
    uint64_t t8 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&t6)) + t3 * 8));
    uint64_t t9 = gm_f_us_cuv01fp(t8);
    uint64_t t10 = (uintptr_t)t9;
    uint64_t t11 = GM_LD(uintptr_t, ((uint8_t *)&t2) + 8);
    if (t10 >= (uintptr_t)(t11)) gm_trap_index(t10, t11, gm_srcnqtpfma6qe4a, 174, 76);
    uint8_t t12 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&t2)) + t10 * 1));
    uint8_t t13 = (!(t12 == ((uint8_t)0ULL)));
    F.ret = t13;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint64_t gm_f_sci_buf_len_tedn1r4(uint32_t a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_sci_buf_len_tedn1r4 F;
    uint32_t gm_l0; /* digits */
    GM_STACK_CHECK_SHALLOW(&F, 40u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint32_t t1 = gm_l0;
    uint64_t t2 = (uint64_t)t1;
    uint64_t r3; bool o4 = __builtin_add_overflow(t2, ((uint64_t)8ULL), &r3);
    if (o4) gm_trap("integer overflow in `+`", gm_srcrsgf2gf2b1dq, 451, 48);
    uint64_t t5 = r3;
    F.ret = t5;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
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
__attribute__((aligned(4))) void gm_f_fmt_adt_p6448k2(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3) {
    struct gm_fr_fmt_adt_p6448k2 F;
    void * gm_l0; /* m */
    void * gm_l1; /* out */
    gm_t88rcucg1b7uk gm_l2; /* t */
    gm_b16_8 gm_l3; /* b */
    void * gm_l4; /* a */
    uint8_t gm_l6; /* first */
    gm_b16_8 gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    uint64_t gm_l9; /* i */
    gm_t2lqu8jsj511d gm_l11; /* mk */
    gm_b16_8 gm_l12; /* fb */
    gm_tl572d7579hns gm_l13; /* r */
    gm_t88rcucg1b7uk gm_l14; /* tag */
    gm_b16_8 gm_l15; /* variants */
    uint32_t gm_l16; /* v */
    gm_t88rcucg1b7uk gm_l17; /* pt */
    uint64_t gm_l18; /* off */
    gm_b16_8 gm_l19; /* pb */
    gm_b16_8 gm_l20; /* elems */
    gm_b16_8 gm_l21; /* names */
    gm_b16_8 gm_l22; /* <tmp> */
    uint64_t gm_l23; /* <tmp> */
    uint64_t gm_l24; /* i */
    gm_t88rcucg1b7uk gm_l25; /* et */
    GM_STACK_CHECK(&F, 2162u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        {
            void * t3 = gm_l0;
            gm_t88rcucg1b7uk t4 = gm_l2;
            void * t5 = gm_f_adt_of_8k5vqud(t3, GM_CONV(gm_csguankko4lmhd, t4));
            void * t6;
            if ((GM_LD(void *, ((uint8_t *)&t5)) != 0)) {
                void * t7 = GM_LD(void *, (((uint8_t *)&t5) + 0));
                t6 = t7;
            } else {
                void * t8 = gm_l0;
                void * t9 = GM_LD(void *, (((uint8_t *)t8) + 8));
                gm_t88rcucg1b7uk t10 = gm_l2;
                gm_b16_8 t11 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t9, t10);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tnpifv20irnph t12;
                GM_ST(gm_b16_8, ((uint8_t *)&t12 + 0), t11);
                gm_tabb710d9mq2e t13;
                gm_f_format_h8e7ps1((void *)&t13, (void *)NULL, cx, NULL, t12);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t13))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t13) + 8, gm_srcfde9lsgge1ld, 63, 35);
                gm_b16_8 t14 = GM_LD(gm_b16_8, (((uint8_t *)&t13) + 8));
                gm_b16_8 t15 = GM_LD(gm_b16_8, ((uint8_t *)&t14));
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 4, 0);
                ((uint8_t *)&(*(gm_tl572d7579hns *)gm_out))[0] = 1;
                GM_ST(gm_b16_8, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 8, t15);
                goto gm_fdone;
            }
            gm_l4 = t6;
        }
        {
            void * t16 = gm_l4;
            uint8_t t22 = GM_LD(uint8_t, ((((uint8_t *)t16) + 24) + 0));
            switch (t22) { case ((uint8_t)0ULL): goto ma18; case ((uint8_t)1ULL): goto ma19; case ((uint8_t)2ULL): goto ma20; default: goto ma21; }
ma18:;
            uint8_t t24 = GM_LD(uint8_t, ((((uint8_t *)t16) + 24) + 0));
            if (t24 != ((uint8_t)0ULL)) goto mn23;
            gm_tpautr4br7h3l t25 = GM_LD(gm_tpautr4br7h3l, ((((uint8_t *)t16) + 24) + 8));
            F.l5 = t25;
            {
                void * t27 = gm_l1;
                void * t28 = gm_l4;
                gm_b16_8 t29 = GM_LD(gm_b16_8, (((uint8_t *)t28) + 0));
                gm_tftb1odc739t5 t30 = gm_f_extend_0cs85gs(t27, t29);
                if (((((uint8_t *)&t30))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t30) + 1, gm_srcfde9lsgge1ld, 66, 13);
                gm_b0_1 t31 = GM_LD(gm_b0_1, (((uint8_t *)&t30) + 1));
            }
            {
                void * t32 = gm_l1;
                gm_tftb1odc739t5 t33 = gm_f_push_t83rg2m(t32, ((uint8_t)40ULL));
                if (((((uint8_t *)&t33))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t33) + 1, gm_srcfde9lsgge1ld, 67, 13);
                gm_b0_1 t34 = GM_LD(gm_b0_1, (((uint8_t *)&t33) + 1));
            }
            {
                gm_l6 = ((uint8_t)1);
            }
            {
                {
                    gm_b16_8 t36 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 0));
                    gm_b16_8 t37 = GM_LD(gm_b16_8, ((uint8_t *)&t36));
                    gm_l7 = t37;
                }
                {
                    gm_l8 = ((uint64_t)0ULL);
                }
                {
                    for (;;) {
                        uint64_t t39 = gm_l8;
                        uint64_t t40 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                        uint8_t t41 = (t39 < t40);
                        if (!t41) goto lb38;
                        {
                            gm_t8ta482bl6be0 t44;
                            uint64_t t45 = gm_l8;
                            GM_ST(uint64_t, (uint8_t *)&t44 + 0, t45);
                            uint64_t t46 = gm_l8;
                            uint64_t t47 = (uintptr_t)t46;
                            uint64_t t48 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                            memcpy((uint8_t *)&t44 + 8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l7)) + t47 * 40), 40);
                            uint64_t t51 = GM_LD(uint64_t, (((uint8_t *)&t44) + 0));
                            gm_l9 = t51;
                            F.l10 = (*(gm_t0mglksrkfa5e *)(((uint8_t *)&t44) + 8));
                            {
                                uint8_t t54 = (((GM_LD(uintptr_t, (((uint8_t *)&F.l10) + 0) + 8) == 1) && memcmp(GM_LD(void *, (((uint8_t *)&F.l10) + 0)), gm_sfdbfrv46lihd, 1) == 0));
                                if (!t54) goto ie53;
                                {
                                    goto lc38;
                                }
                                goto ix53;
ie53:;
ix53:;
                            }
                            {
                                uint8_t t57 = gm_l6;
                                uint8_t t58 = (uint8_t)!t57;
                                if (!t58) goto ie56;
                                {
                                    void * t60 = gm_l1;
                                    gm_b16_8 t61;
                                    GM_ST(void *, (uint8_t *)&t61, (void *)(gm_sg5gdnrith2i6));
                                    GM_ST(uintptr_t, (uint8_t *)&t61 + 8, (uintptr_t)(2));
                                    gm_tftb1odc739t5 t62 = gm_f_extend_0cs85gs(t60, t61);
                                    if (((((uint8_t *)&t62))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t62) + 1, gm_srcfde9lsgge1ld, 71, 32);
                                    gm_b0_1 t63 = GM_LD(gm_b0_1, (((uint8_t *)&t62) + 1));
                                }
                                goto ix56;
ie56:;
ix56:;
                            }
                            {
                                gm_l6 = ((uint8_t)0);
                            }
                            {
                                void * t64 = gm_l1;
                                gm_b16_8 t65 = GM_LD(gm_b16_8, (((uint8_t *)&F.l10) + 0));
                                gm_tftb1odc739t5 t66 = gm_f_extend_0cs85gs(t64, t65);
                                if (((((uint8_t *)&t66))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t66) + 1, gm_srcfde9lsgge1ld, 73, 17);
                                gm_b0_1 t67 = GM_LD(gm_b0_1, (((uint8_t *)&t66) + 1));
                            }
                            {
                                void * t68 = gm_l1;
                                gm_b16_8 t69;
                                GM_ST(void *, (uint8_t *)&t69, (void *)(gm_slgmqgkioa4u6));
                                GM_ST(uintptr_t, (uint8_t *)&t69 + 8, (uintptr_t)(2));
                                gm_tftb1odc739t5 t70 = gm_f_extend_0cs85gs(t68, t69);
                                if (((((uint8_t *)&t70))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t70) + 1, gm_srcfde9lsgge1ld, 74, 17);
                                gm_b0_1 t71 = GM_LD(gm_b0_1, (((uint8_t *)&t70) + 1));
                            }
                            {
                                void * t72 = gm_l0;
                                gm_t2lqu8jsj511d t73 = GM_CONV(gm_t2lqu8jsj511d, gm_f_mark_kpppm1j(t72));
                                gm_l11 = t73;
                            }
                            {
                                gm_b16_8 t74 = gm_f_empty_slice_ijtp9lo();
                                gm_l12 = t74;
                            }
                            {
                                void * t76 = gm_l0;
                                gm_t88rcucg1b7uk t77 = gm_l2;
                                gm_tkn42u7na3t81 t78 = gm_f_bits_struct_ty_22ue20s(t76, GM_CONV(gm_csguankko4lmhd, t77));
                                uint8_t t79;
                                if (((((uint8_t *)&t78))[4] != 0)) goto mn81;
                                t79 = ((uint8_t)0);
                                goto me80;
mn81:;
                                t79 = ((uint8_t)1);
                                goto me80;
mn82:;
                                gm_trap("no match arm matched the scrutinee", gm_srcfde9lsgge1ld, 77, 20);
me80:;
                                if (!t79) goto ie75;
                                {
                                    void * t84 = gm_l0;
                                    void * t85 = gm_l0;
                                    gm_t88rcucg1b7uk t86 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l10) + 16));
                                    uint64_t t87 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t85, t86);
                                    if (gm_unw.kind) {
                                        goto gm_ll42;
                                    }
                                    gm_b16_8 t88 = gm_f_tmp_8k6u9c9((void *)NULL, cx, NULL, t84, t87);
                                    if (gm_unw.kind) {
                                        goto gm_ll42;
                                    }
                                    gm_l12 = t88;
                                }
                                {
                                    void * t89 = gm_l0;
                                    gm_t88rcucg1b7uk t90 = gm_l2;
                                    gm_b16_8 t91 = gm_l3;
                                    uint64_t t92 = gm_l9;
                                    uint32_t t93 = (uint32_t)t92;
                                    gm_b16_8 t94 = gm_l12;
                                    gm_b0_1 t95 = gm_f_read_field_ua0742c((void *)NULL, cx, NULL, t89, t90, t91, t93, t94);
                                    if (gm_unw.kind) {
                                        goto gm_ll42;
                                    }
                                }
                                goto ix75;
ie75:;
                                {
                                    void * t97 = gm_l0;
                                    gm_t88rcucg1b7uk t98 = gm_l2;
                                    gm_b16_8 t99 = gm_l3;
                                    uint64_t t100 = gm_l9;
                                    uint32_t t101 = (uint32_t)t100;
                                    gm_b16_8 t102 = gm_f_field_bytes_bhivr4s((void *)NULL, cx, NULL, t97, t98, t99, t101);
                                    if (gm_unw.kind) {
                                        goto gm_ll42;
                                    }
                                    gm_l12 = t102;
                                }
ix75:;
                            }
                            {
                                void * t103 = gm_l0;
                                void * t104 = gm_l1;
                                gm_t88rcucg1b7uk t105 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l10) + 16));
                                gm_b16_8 t106 = gm_l12;
                                gm_f_format_value_vmle7qr((void *)&gm_l13, (void *)NULL, cx, NULL, t103, t104, t105, t106, ((uint8_t)0));
                                if (gm_unw.kind) {
                                    goto gm_ll42;
                                }
                            }
                            {
                                void * t107 = gm_l0;
                                gm_t2lqu8jsj511d t108 = gm_l11;
                                gm_f_release_jn5kqdi(t107, GM_CONV(gm_csc7lc6jlkqkuq, t108));
                            }
                            {
                                gm_tl572d7579hns t109 = gm_l13;
                                if (((((uint8_t *)&t109))[0] != 0)) {
                                    gm_b16_8 t110 = GM_LD(gm_b16_8, (((uint8_t *)&t109) + 8));
                                    gm_tl572d7579hns t111;
                                    GM_ST(uint8_t, (uint8_t *)&t111 + 1, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t111 + 2, 0);
                                    GM_ST(uint32_t, (uint8_t *)&t111 + 4, 0);
                                    ((uint8_t *)&t111)[0] = 1;
                                    GM_ST(gm_b16_8, ((uint8_t *)&t111 + 8), t110);
                                    F.ret = t111;
                                    goto gm_fret;
                                }
                                gm_b0_1 t112 = GM_LD(gm_b0_1, (((uint8_t *)&t109) + 8));
                            }
                            goto me49;
mn50:;
                            gm_trap("no match arm matched the scrutinee", gm_srcfde9lsgge1ld, 69, 13);
me49:;
                        }
lc38:;
                        {
                            uint64_t t114 = gm_l8;
                            uint64_t r115; bool o116 = __builtin_add_overflow(t114, ((uint64_t)1ULL), &r115);
                            uint64_t t117 = r115;
                            gm_l8 = t117;
                        }
                    }
gm_ll42:;
                    if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb38; }
                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc38; }
                    }
                    goto gm_lad;
lb38:;
                }
            }
            {
                void * t118 = gm_l1;
                gm_tftb1odc739t5 t119 = gm_f_push_t83rg2m(t118, ((uint8_t)41ULL));
                if (((((uint8_t *)&t119))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t119) + 1, gm_srcfde9lsgge1ld, 87, 13);
                gm_b0_1 t120 = GM_LD(gm_b0_1, (((uint8_t *)&t119) + 1));
            }
            goto me17;
mn23:;
ma19:;
            uint8_t t122 = GM_LD(uint8_t, ((((uint8_t *)t16) + 24) + 0));
            if (t122 != ((uint8_t)1ULL)) goto mn121;
            gm_t88rcucg1b7uk t123 = GM_LD(gm_t88rcucg1b7uk, (((((uint8_t *)t16) + 24) + 8) + 0));
            gm_l14 = t123;
            gm_b16_8 t124 = GM_LD(gm_b16_8, (((((uint8_t *)t16) + 24) + 8) + 8));
            gm_l15 = t124;
            {
                void * t126 = gm_l0;
                gm_t88rcucg1b7uk t127 = gm_l2;
                gm_b16_8 t128 = gm_l3;
                gm_b16_8 t129 = GM_LD(gm_b16_8, ((uint8_t *)&t128));
                gm_t9805qtonbsag t130 = gm_f_variant_index_s2p6fs4((void *)NULL, cx, NULL, t126, t127, t129);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint32_t t131;
                if (((((uint8_t *)&t130))[4] != 0)) {
                    uint32_t t132 = GM_LD(uint32_t, (((uint8_t *)&t130) + 0));
                    t131 = t132;
                } else {
                    void * t133 = gm_l0;
                    gm_t88rcucg1b7uk t134 = gm_l2;
                    gm_t88rcucg1b7uk t135 = gm_l14;
                    gm_b16_8 t136 = gm_l3;
                    gm_b16_8 t137 = GM_LD(gm_b16_8, ((uint8_t *)&t136));
                    gm_b16_8 t138 = gm_f_invalid_tag_text_f3l5hi7((void *)NULL, cx, NULL, t133, t134, t135, t137);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 4, 0);
                    ((uint8_t *)&(*(gm_tl572d7579hns *)gm_out))[0] = 1;
                    GM_ST(gm_b16_8, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 8, t138);
                    goto gm_fdone;
                }
                gm_l16 = t131;
            }
            {
                void * t139 = gm_l1;
                gm_tftb1odc739t5 t140 = gm_f_push_t83rg2m(t139, ((uint8_t)46ULL));
                if (((((uint8_t *)&t140))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t140) + 1, gm_srcfde9lsgge1ld, 91, 13);
                gm_b0_1 t141 = GM_LD(gm_b0_1, (((uint8_t *)&t140) + 1));
            }
            {
                void * t142 = gm_l1;
                gm_b16_8 t143 = gm_l15;
                gm_b16_8 t144 = t143;
                uint32_t t145 = gm_l16;
                uint64_t t146 = (uint64_t)t145;
                uint64_t t147 = gm_f_us_cuv01fp(t146);
                uint64_t t148 = (uintptr_t)t147;
                uint64_t t149 = GM_LD(uintptr_t, ((uint8_t *)&t144) + 8);
                if (t148 >= (uintptr_t)(t149)) gm_trap_index(t148, t149, gm_srcfde9lsgge1ld, 92, 24);
                gm_b16_8 t150 = GM_LD(gm_b16_8, (((uint8_t *)GM_LD(void *, ((uint8_t *)&t144)) + t148 * 72) + 0));
                gm_tftb1odc739t5 t151 = gm_f_extend_0cs85gs(t142, t150);
                if (((((uint8_t *)&t151))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t151) + 1, gm_srcfde9lsgge1ld, 92, 13);
                gm_b0_1 t152 = GM_LD(gm_b0_1, (((uint8_t *)&t151) + 1));
            }
            {
                gm_b16_8 t154 = gm_l15;
                gm_b16_8 t155 = t154;
                uint32_t t156 = gm_l16;
                uint64_t t157 = (uint64_t)t156;
                uint64_t t158 = gm_f_us_cuv01fp(t157);
                uint64_t t159 = (uintptr_t)t158;
                uint64_t t160 = GM_LD(uintptr_t, ((uint8_t *)&t155) + 8);
                if (t159 >= (uintptr_t)(t160)) gm_trap_index(t159, t160, gm_srcfde9lsgge1ld, 93, 22);
                if (!(((((uint8_t *)GM_LD(void *, ((uint8_t *)&t155)) + t159 * 72) + 16))[4] != 0)) goto ie153;
                gm_t88rcucg1b7uk t161 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, ((uint8_t *)&t155)) + t159 * 72) + 16));
                gm_l17 = t161;
                {
                    void * t163 = gm_l0;
                    gm_t88rcucg1b7uk t164 = gm_l2;
                    uint64_t t165 = gm_f_payload_offset_la0c8ft((void *)NULL, cx, NULL, t163, t164);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint64_t t166 = gm_f_us_cuv01fp(t165);
                    gm_l18 = t166;
                }
                {
                    gm_b16_8 t167 = gm_l3;
                    uint64_t t168 = GM_LD(uintptr_t, ((uint8_t *)&t167) + 8);
                    void * t169 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t167)));
                    uint64_t t170 = gm_l18;
                    uint64_t t171 = (uintptr_t)t170;
                    uint64_t t172 = gm_l18;
                    void * t173 = gm_l0;
                    gm_t88rcucg1b7uk t174 = gm_l17;
                    uint64_t t175 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t173, t174);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint64_t t176 = gm_f_us_cuv01fp(t175);
                    uint64_t r177; bool o178 = __builtin_add_overflow(t172, t176, &r177);
                    if (o178) gm_trap("integer overflow in `+`", gm_srcfde9lsgge1ld, 95, 30);
                    uint64_t t179 = r177;
                    uint64_t t180 = (uintptr_t)t179;
                    if (t171 > t180 || t180 > (uintptr_t)t168) gm_trap_slice(t171, t180, t168, gm_srcfde9lsgge1ld, 95, 23);
                    gm_b16_8 t181;
                    GM_ST(void *, (uint8_t *)&t181, (void *)(((uint8_t *)t169 + t171 * 1)));
                    GM_ST(uintptr_t, (uint8_t *)&t181 + 8, (uintptr_t)(t180 - t171));
                    gm_l19 = t181;
                }
                {
                    void * t182 = gm_l1;
                    gm_tftb1odc739t5 t183 = gm_f_push_t83rg2m(t182, ((uint8_t)40ULL));
                    if (((((uint8_t *)&t183))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t183) + 1, gm_srcfde9lsgge1ld, 96, 17);
                    gm_b0_1 t184 = GM_LD(gm_b0_1, (((uint8_t *)&t183) + 1));
                }
                {
                    void * t186 = gm_l0;
                    gm_t88rcucg1b7uk t187 = gm_l17;
                    gm_t53at80pfdus3 t188;
                    gm_f_kind_htv6093((void *)&t188, t186, GM_CONV(gm_csguankko4lmhd, t187));
                    uint8_t t189 = GM_LD(uint8_t, (((uint8_t *)&t188) + 0));
                    if (t189 != ((uint8_t)12ULL)) goto ie185;
                    gm_b16_8 t190 = GM_LD(gm_b16_8, ((((uint8_t *)&t188) + 8) + 0));
                    gm_l20 = t190;
                    gm_b16_8 t191 = GM_LD(gm_b16_8, ((((uint8_t *)&t188) + 8) + 16));
                    gm_l21 = t191;
                    {
                        {
                            gm_b16_8 t194 = gm_l20;
                            gm_l22 = t194;
                        }
                        {
                            gm_l23 = ((uint64_t)0ULL);
                        }
                        {
                            for (;;) {
                                uint64_t t196 = gm_l23;
                                uint64_t t197 = GM_LD(uintptr_t, ((uint8_t *)&gm_l22) + 8);
                                uint8_t t198 = (t196 < t197);
                                if (!t198) goto lb195;
                                {
                                    uint64_t t201 = gm_l23;
                                    uint64_t t202 = gm_l23;
                                    uint64_t t203 = (uintptr_t)t202;
                                    uint64_t t204 = GM_LD(uintptr_t, ((uint8_t *)&gm_l22) + 8);
                                    gm_t88rcucg1b7uk t205 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l22)) + t203 * 4));
                                    gm_tlcm4lvu3g1a8 t206;
                                    GM_ST(uint32_t, (uint8_t *)&t206 + 12, 0);
                                    GM_ST(uint64_t, ((uint8_t *)&t206 + 0), t201);
                                    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t206 + 8), t205);
                                    uint64_t t209 = GM_LD(uint64_t, (((uint8_t *)&t206) + 0));
                                    gm_l24 = t209;
                                    gm_t88rcucg1b7uk t210 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t206) + 8));
                                    gm_l25 = t210;
                                    {
                                        uint64_t t213 = gm_l24;
                                        uint8_t t214 = (t213 > ((uint64_t)0ULL));
                                        if (!t214) goto ie212;
                                        {
                                            void * t216 = gm_l1;
                                            gm_b16_8 t217;
                                            GM_ST(void *, (uint8_t *)&t217, (void *)(gm_sg5gdnrith2i6));
                                            GM_ST(uintptr_t, (uint8_t *)&t217 + 8, (uintptr_t)(2));
                                            gm_tftb1odc739t5 t218 = gm_f_extend_0cs85gs(t216, t217);
                                            if (((((uint8_t *)&t218))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t218) + 1, gm_srcfde9lsgge1ld, 99, 36);
                                            gm_b0_1 t219 = GM_LD(gm_b0_1, (((uint8_t *)&t218) + 1));
                                        }
                                        goto ix212;
ie212:;
ix212:;
                                    }
                                    {
                                        uint64_t t221 = GM_LD(uintptr_t, ((uint8_t *)&gm_l21) + 8);
                                        uint8_t t222 = (t221 > ((uint64_t)0ULL));
                                        if (!t222) goto ie220;
                                        {
                                            void * t224 = gm_l1;
                                            uint64_t t225 = gm_l24;
                                            uint64_t t226 = (uintptr_t)t225;
                                            uint64_t t227 = GM_LD(uintptr_t, ((uint8_t *)&gm_l21) + 8);
                                            if (t226 >= (uintptr_t)(t227)) gm_trap_index(t226, t227, gm_srcfde9lsgge1ld, 101, 40);
                                            gm_b16_8 t228 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l21)) + t226 * 16));
                                            gm_tftb1odc739t5 t229 = gm_f_extend_0cs85gs(t224, t228);
                                            if (((((uint8_t *)&t229))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t229) + 1, gm_srcfde9lsgge1ld, 101, 29);
                                            gm_b0_1 t230 = GM_LD(gm_b0_1, (((uint8_t *)&t229) + 1));
                                        }
                                        {
                                            void * t231 = gm_l1;
                                            gm_b16_8 t232;
                                            GM_ST(void *, (uint8_t *)&t232, (void *)(gm_slgmqgkioa4u6));
                                            GM_ST(uintptr_t, (uint8_t *)&t232 + 8, (uintptr_t)(2));
                                            gm_tftb1odc739t5 t233 = gm_f_extend_0cs85gs(t231, t232);
                                            if (((((uint8_t *)&t233))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t233) + 1, gm_srcfde9lsgge1ld, 102, 29);
                                            gm_b0_1 t234 = GM_LD(gm_b0_1, (((uint8_t *)&t233) + 1));
                                        }
                                        goto ix220;
ie220:;
ix220:;
                                    }
                                    {
                                        void * t235 = gm_l0;
                                        void * t236 = gm_l1;
                                        gm_t88rcucg1b7uk t237 = gm_l25;
                                        void * t238 = gm_l0;
                                        gm_t88rcucg1b7uk t239 = gm_l17;
                                        gm_b16_8 t240 = gm_l19;
                                        uint64_t t241 = gm_l24;
                                        uint32_t t242 = (uint32_t)t241;
                                        gm_b16_8 t243 = gm_f_field_bytes_bhivr4s((void *)NULL, cx, NULL, t238, t239, t240, t242);
                                        if (gm_unw.kind) {
                                            goto gm_ll199;
                                        }
                                        gm_tl572d7579hns t244;
                                        gm_f_format_value_vmle7qr((void *)&t244, (void *)NULL, cx, NULL, t235, t236, t237, t243, ((uint8_t)0));
                                        if (gm_unw.kind) {
                                            goto gm_ll199;
                                        }
                                        if (((((uint8_t *)&t244))[0] != 0)) {
                                            gm_b16_8 t245 = GM_LD(gm_b16_8, (((uint8_t *)&t244) + 8));
                                            gm_tl572d7579hns t246;
                                            GM_ST(uint8_t, (uint8_t *)&t246 + 1, 0);
                                            GM_ST(uint16_t, (uint8_t *)&t246 + 2, 0);
                                            GM_ST(uint32_t, (uint8_t *)&t246 + 4, 0);
                                            ((uint8_t *)&t246)[0] = 1;
                                            GM_ST(gm_b16_8, ((uint8_t *)&t246 + 8), t245);
                                            F.ret = t246;
                                            goto gm_fret;
                                        }
                                        gm_b0_1 t247 = GM_LD(gm_b0_1, (((uint8_t *)&t244) + 8));
                                    }
                                    goto me207;
mn208:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcfde9lsgge1ld, 98, 21);
me207:;
                                }
lc195:;
                                {
                                    uint64_t t249 = gm_l23;
                                    uint64_t r250; bool o251 = __builtin_add_overflow(t249, ((uint64_t)1ULL), &r250);
                                    uint64_t t252 = r250;
                                    gm_l23 = t252;
                                }
                            }
gm_ll199:;
                            if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb195; }
                                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc195; }
                            }
                            goto gm_lad;
lb195:;
                        }
                    }
                    goto ix185;
ie185:;
                    {
                        void * t254 = gm_l0;
                        void * t255 = gm_l1;
                        gm_t88rcucg1b7uk t256 = gm_l17;
                        gm_b16_8 t257 = gm_l19;
                        gm_tl572d7579hns t258;
                        gm_f_format_value_vmle7qr((void *)&t258, (void *)NULL, cx, NULL, t254, t255, t256, t257, ((uint8_t)0));
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        if (((((uint8_t *)&t258))[0] != 0)) {
                            gm_b16_8 t259 = GM_LD(gm_b16_8, (((uint8_t *)&t258) + 8));
                            gm_tl572d7579hns t260;
                            GM_ST(uint8_t, (uint8_t *)&t260 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t260 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t260 + 4, 0);
                            ((uint8_t *)&t260)[0] = 1;
                            GM_ST(gm_b16_8, ((uint8_t *)&t260 + 8), t259);
                            F.ret = t260;
                            goto gm_fret;
                        }
                        gm_b0_1 t261 = GM_LD(gm_b0_1, (((uint8_t *)&t258) + 8));
                    }
ix185:;
                }
                {
                    void * t262 = gm_l1;
                    gm_tftb1odc739t5 t263 = gm_f_push_t83rg2m(t262, ((uint8_t)41ULL));
                    if (((((uint8_t *)&t263))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t263) + 1, gm_srcfde9lsgge1ld, 109, 17);
                    gm_b0_1 t264 = GM_LD(gm_b0_1, (((uint8_t *)&t263) + 1));
                }
                goto ix153;
ie153:;
ix153:;
            }
            goto me17;
mn121:;
ma20:;
            uint8_t t266 = GM_LD(uint8_t, ((((uint8_t *)t16) + 24) + 0));
            if (t266 != ((uint8_t)2ULL)) goto mn265;
            void * t267 = gm_l1;
            void * t268 = gm_l4;
            gm_b16_8 t269 = GM_LD(gm_b16_8, (((uint8_t *)t268) + 0));
            gm_tnpifv20irnph t270;
            GM_ST(gm_b16_8, ((uint8_t *)&t270 + 0), t269);
            gm_tftb1odc739t5 t271 = gm_f_write_jvie1fn(t267, t270);
            if (((((uint8_t *)&t271))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t271) + 1, gm_srcfde9lsgge1ld, 112, 23);
            gm_b0_1 t272 = GM_LD(gm_b0_1, (((uint8_t *)&t271) + 1));
            goto me17;
mn265:;
ma21:;
            gm_trap("no match arm matched the scrutinee", gm_srcfde9lsgge1ld, 64, 5);
me17:;
        }
    }
    gm_b0_1 t273;
    GM_ST(uint8_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 8, 0);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 16, 0);
    ((uint8_t *)&(*(gm_tl572d7579hns *)gm_out))[0] = 0;
    GM_ST(gm_b0_1, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 8, t273);
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
__attribute__((aligned(4))) gm_b0_1 gm_f_fmt_float_1ivmudg(void *env, gm_node *cx, gm_node *inst, void * a0, double a1, uint16_t a2) {
    struct gm_fr_fmt_float_1ivmudg F;
    void * gm_l0; /* out */
    double gm_l1; /* x */
    uint16_t gm_l2; /* bits */
    gm_t43v1gep754th gm_l4; /* sh */
    uint64_t gm_l5; /* n */
    int32_t gm_l6; /* exp */
    uint64_t gm_l7; /* int_len */
    uint64_t gm_l8; /* <tmp> */
    uint64_t gm_l9; /* <tmp> */
    uint64_t gm_l10; /* <tmp> */
    uint64_t gm_l11; /* <tmp> */
    GM_STACK_CHECK(&F, 1137u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        double t3 = gm_l1;
        uint8_t t4 = (uint8_t)__builtin_isnan(t3);
        if (!t4) goto ie2;
        {
            void * t6 = gm_l0;
            gm_b16_8 t7;
            GM_ST(void *, (uint8_t *)&t7, (void *)(gm_sfa86erpod86s));
            GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)(3));
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            if (((((uint8_t *)&t8))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t8) + 1, gm_srcfde9lsgge1ld, 292, 9);
            gm_b0_1 t9 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
        }
        {
            gm_b0_1 t10;
            uint8_t t11 = 0;
            F.ret = t10;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        double t13 = gm_l1;
        uint8_t t14 = (uint8_t)__builtin_isinf(t13);
        if (!t14) goto ie12;
        {
            void * t16 = gm_l0;
            gm_b16_8 t17;
            double t19 = gm_l1;
            uint8_t t20 = (t19 < ((double)0e0));
            if (!t20) goto ie18;
            gm_b16_8 t22;
            GM_ST(void *, (uint8_t *)&t22, (void *)(gm_segch6a5dme9u));
            GM_ST(uintptr_t, (uint8_t *)&t22 + 8, (uintptr_t)(4));
            gm_b16_8 t23 = t22;
            t17 = t23;
            goto ix18;
ie18:;
            gm_b16_8 t25;
            GM_ST(void *, (uint8_t *)&t25, (void *)(gm_sns75reqq32d2));
            GM_ST(uintptr_t, (uint8_t *)&t25 + 8, (uintptr_t)(3));
            gm_b16_8 t26 = t25;
            t17 = t26;
ix18:;
            gm_tftb1odc739t5 t27 = gm_f_extend_0cs85gs(t16, t17);
            if (((((uint8_t *)&t27))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t27) + 1, gm_srcfde9lsgge1ld, 296, 9);
            gm_b0_1 t28 = GM_LD(gm_b0_1, (((uint8_t *)&t27) + 1));
        }
        {
            gm_b0_1 t29;
            uint8_t t30 = 0;
            F.ret = t29;
            goto gm_fret;
        }
        goto ix12;
ie12:;
ix12:;
    }
    {
        double t32 = gm_l1;
        uint8_t t33 = ((t32 == ((double)0e0)));
        if (!t33) goto ie31;
        {
            void * t35 = gm_l0;
            gm_b16_8 t36;
            double t38 = gm_l1;
            uint8_t t39 = (uint8_t)(__builtin_signbit(t38) != 0);
            if (!t39) goto ie37;
            gm_b16_8 t41;
            GM_ST(void *, (uint8_t *)&t41, (void *)(gm_s9lclteeapkg2));
            GM_ST(uintptr_t, (uint8_t *)&t41 + 8, (uintptr_t)(2));
            gm_b16_8 t42 = t41;
            t36 = t42;
            goto ix37;
ie37:;
            gm_b16_8 t44;
            GM_ST(void *, (uint8_t *)&t44, (void *)(gm_sicgmpca63jln));
            GM_ST(uintptr_t, (uint8_t *)&t44 + 8, (uintptr_t)(1));
            gm_b16_8 t45 = t44;
            t36 = t45;
ix37:;
            gm_tftb1odc739t5 t46 = gm_f_extend_0cs85gs(t35, t36);
            if (((((uint8_t *)&t46))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t46) + 1, gm_srcfde9lsgge1ld, 300, 9);
            gm_b0_1 t47 = GM_LD(gm_b0_1, (((uint8_t *)&t46) + 1));
        }
        {
            gm_b0_1 t48;
            uint8_t t49 = 0;
            F.ret = t48;
            goto gm_fret;
        }
        goto ix31;
ie31:;
ix31:;
    }
    {
        double t51 = gm_l1;
        uint8_t t52 = (t51 < ((double)0e0));
        if (!t52) goto ie50;
        {
            void * t54 = gm_l0;
            gm_tftb1odc739t5 t55 = gm_f_push_t83rg2m(t54, ((uint8_t)45ULL));
            if (((((uint8_t *)&t55))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t55) + 1, gm_srcfde9lsgge1ld, 303, 18);
            gm_b0_1 t56 = GM_LD(gm_b0_1, (((uint8_t *)&t55) + 1));
        }
        goto ix50;
ie50:;
ix50:;
    }
    {
        gm_t6kvj62qe5fp2 t57;
        for (uint64_t i = 0; i < 40; i++) GM_ST(uint8_t, (uint8_t *)&t57 + i * 1, ((uint8_t)0ULL));
        F.l3 = t57;
    }
    {
        double t58 = gm_l1;
        double t59 = (t58 < 0 || ((t58) == 0 && __builtin_signbit(t58))) ? -t58 : t58;
        uint16_t t60 = gm_l2;
        void * t61 = ((uint8_t *)&F.l3);
        if ((uintptr_t)0 > (uintptr_t)40 || (uintptr_t)40 > (uintptr_t)40) gm_trap_slice((uintptr_t)0, (uintptr_t)40, 40, gm_srcfde9lsgge1ld, 305, 39);
        gm_b16_8 t62;
        GM_ST(void *, (uint8_t *)&t62, (void *)(((uint8_t *)t61 + (uintptr_t)0 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t62 + 8, (uintptr_t)((uintptr_t)40 - (uintptr_t)0));
        gm_t43v1gep754th t63 = gm_f_fmt_shortest_2l54as9((void *)NULL, cx, NULL, t59, t60, t62);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l4 = t63;
    }
    {
        uint64_t t64 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 0));
        gm_l5 = t64;
    }
    {
        int32_t t65 = GM_LD(int32_t, (((uint8_t *)&gm_l4) + 8));
        gm_l6 = t65;
    }
    {
        int32_t t67 = gm_l6;
        uint8_t t68 = (t67 >= ((int32_t)-6LL));
        if (!t68) goto ie66;
        int32_t t69 = gm_l6;
        uint8_t t70 = (t69 <= ((int32_t)20LL));
        if (!t70) goto ie66;
        {
            int32_t t73 = gm_l6;
            uint8_t t74 = (t73 >= ((int32_t)0LL));
            if (!t74) goto ie72;
            {
                int32_t t76 = gm_l6;
                uint64_t t77 = (uint64_t)t76;
                uint64_t r78; bool o79 = __builtin_add_overflow(t77, ((uint64_t)1ULL), &r78);
                if (o79) gm_trap("integer overflow in `+`", gm_srcfde9lsgge1ld, 310, 24);
                uint64_t t80 = r78;
                gm_l7 = t80;
            }
            {
                uint64_t t82 = gm_l5;
                uint64_t t83 = gm_l7;
                uint8_t t84 = (t82 <= t83);
                if (!t84) goto ie81;
                {
                    void * t86 = gm_l0;
                    void * t87 = ((uint8_t *)&F.l3);
                    uint64_t t88 = (uintptr_t)((uint64_t)0ULL);
                    uint64_t t89 = gm_l5;
                    uint64_t t90 = (uintptr_t)t89;
                    if (t88 > t90 || t90 > (uintptr_t)40) gm_trap_slice(t88, t90, 40, gm_srcfde9lsgge1ld, 312, 28);
                    gm_b16_8 t91;
                    GM_ST(void *, (uint8_t *)&t91, (void *)(((uint8_t *)t87 + t88 * 1)));
                    GM_ST(uintptr_t, (uint8_t *)&t91 + 8, (uintptr_t)(t90 - t88));
                    gm_b16_8 t92 = GM_LD(gm_b16_8, ((uint8_t *)&t91));
                    gm_tftb1odc739t5 t93 = gm_f_extend_0cs85gs(t86, t92);
                    if (((((uint8_t *)&t93))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t93) + 1, gm_srcfde9lsgge1ld, 312, 17);
                    gm_b0_1 t94 = GM_LD(gm_b0_1, (((uint8_t *)&t93) + 1));
                }
                {
                    {
                        gm_l9 = ((uint64_t)0ULL);
                    }
                    {
                        uint64_t t96 = gm_l7;
                        uint64_t t97 = gm_l5;
                        uint64_t r98; bool o99 = __builtin_sub_overflow(t96, t97, &r98);
                        if (o99) gm_trap("integer overflow in `-`", gm_srcfde9lsgge1ld, 313, 29);
                        uint64_t t100 = r98;
                        gm_l8 = t100;
                    }
                    {
                        for (;;) {
                            uint64_t t102 = gm_l9;
                            uint64_t t103 = gm_l8;
                            uint8_t t104 = (t102 < t103);
                            if (!t104) goto lb101;
                            {
                                uint64_t t107 = gm_l9;
                                {
                                    void * t111 = gm_l0;
                                    gm_tftb1odc739t5 t112 = gm_f_push_t83rg2m(t111, ((uint8_t)48ULL));
                                    if (((((uint8_t *)&t112))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t112) + 1, gm_srcfde9lsgge1ld, 313, 43);
                                    gm_b0_1 t113 = GM_LD(gm_b0_1, (((uint8_t *)&t112) + 1));
                                }
                                goto me108;
mn109:;
                                gm_trap("no match arm matched the scrutinee", gm_srcfde9lsgge1ld, 313, 17);
me108:;
                            }
lc101:;
                            {
                                uint64_t t115 = gm_l9;
                                uint64_t r116; bool o117 = __builtin_add_overflow(t115, ((uint64_t)1ULL), &r116);
                                uint64_t t118 = r116;
                                gm_l9 = t118;
                            }
                        }
lb101:;
                    }
                }
                goto ix81;
ie81:;
                {
                    void * t120 = gm_l0;
                    void * t121 = ((uint8_t *)&F.l3);
                    uint64_t t122 = (uintptr_t)((uint64_t)0ULL);
                    uint64_t t123 = gm_l7;
                    uint64_t t124 = (uintptr_t)t123;
                    if (t122 > t124 || t124 > (uintptr_t)40) gm_trap_slice(t122, t124, 40, gm_srcfde9lsgge1ld, 315, 28);
                    gm_b16_8 t125;
                    GM_ST(void *, (uint8_t *)&t125, (void *)(((uint8_t *)t121 + t122 * 1)));
                    GM_ST(uintptr_t, (uint8_t *)&t125 + 8, (uintptr_t)(t124 - t122));
                    gm_b16_8 t126 = GM_LD(gm_b16_8, ((uint8_t *)&t125));
                    gm_tftb1odc739t5 t127 = gm_f_extend_0cs85gs(t120, t126);
                    if (((((uint8_t *)&t127))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t127) + 1, gm_srcfde9lsgge1ld, 315, 17);
                    gm_b0_1 t128 = GM_LD(gm_b0_1, (((uint8_t *)&t127) + 1));
                }
                {
                    void * t129 = gm_l0;
                    gm_tftb1odc739t5 t130 = gm_f_push_t83rg2m(t129, ((uint8_t)46ULL));
                    if (((((uint8_t *)&t130))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t130) + 1, gm_srcfde9lsgge1ld, 316, 17);
                    gm_b0_1 t131 = GM_LD(gm_b0_1, (((uint8_t *)&t130) + 1));
                }
                {
                    void * t132 = gm_l0;
                    void * t133 = ((uint8_t *)&F.l3);
                    uint64_t t134 = gm_l7;
                    uint64_t t135 = (uintptr_t)t134;
                    uint64_t t136 = gm_l5;
                    uint64_t t137 = (uintptr_t)t136;
                    if (t135 > t137 || t137 > (uintptr_t)40) gm_trap_slice(t135, t137, 40, gm_srcfde9lsgge1ld, 317, 28);
                    gm_b16_8 t138;
                    GM_ST(void *, (uint8_t *)&t138, (void *)(((uint8_t *)t133 + t135 * 1)));
                    GM_ST(uintptr_t, (uint8_t *)&t138 + 8, (uintptr_t)(t137 - t135));
                    gm_b16_8 t139 = GM_LD(gm_b16_8, ((uint8_t *)&t138));
                    gm_tftb1odc739t5 t140 = gm_f_extend_0cs85gs(t132, t139);
                    if (((((uint8_t *)&t140))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t140) + 1, gm_srcfde9lsgge1ld, 317, 17);
                    gm_b0_1 t141 = GM_LD(gm_b0_1, (((uint8_t *)&t140) + 1));
                }
ix81:;
            }
            goto ix72;
ie72:;
            {
                void * t143 = gm_l0;
                gm_b16_8 t144;
                GM_ST(void *, (uint8_t *)&t144, (void *)(gm_sjq55q61p743j));
                GM_ST(uintptr_t, (uint8_t *)&t144 + 8, (uintptr_t)(2));
                gm_tftb1odc739t5 t145 = gm_f_extend_0cs85gs(t143, t144);
                if (((((uint8_t *)&t145))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t145) + 1, gm_srcfde9lsgge1ld, 320, 13);
                gm_b0_1 t146 = GM_LD(gm_b0_1, (((uint8_t *)&t145) + 1));
            }
            {
                {
                    gm_l11 = ((uint64_t)0ULL);
                }
                {
                    int32_t t148 = gm_l6;
                    int32_t r149; bool o150 = __builtin_sub_overflow(((int32_t)0LL), t148, &r149);
                    if (o150) gm_trap("integer overflow in `-`", gm_srcfde9lsgge1ld, 321, 26);
                    int32_t t151 = r149;
                    int32_t r152; bool o153 = __builtin_sub_overflow(t151, ((int32_t)1LL), &r152);
                    if (o153) gm_trap("integer overflow in `-`", gm_srcfde9lsgge1ld, 321, 26);
                    int32_t t154 = r152;
                    uint64_t t155 = (uint64_t)t154;
                    gm_l10 = t155;
                }
                {
                    for (;;) {
                        uint64_t t157 = gm_l11;
                        uint64_t t158 = gm_l10;
                        uint8_t t159 = (t157 < t158);
                        if (!t159) goto lb156;
                        {
                            uint64_t t162 = gm_l11;
                            {
                                void * t166 = gm_l0;
                                gm_tftb1odc739t5 t167 = gm_f_push_t83rg2m(t166, ((uint8_t)48ULL));
                                if (((((uint8_t *)&t167))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t167) + 1, gm_srcfde9lsgge1ld, 321, 54);
                                gm_b0_1 t168 = GM_LD(gm_b0_1, (((uint8_t *)&t167) + 1));
                            }
                            goto me163;
mn164:;
                            gm_trap("no match arm matched the scrutinee", gm_srcfde9lsgge1ld, 321, 13);
me163:;
                        }
lc156:;
                        {
                            uint64_t t170 = gm_l11;
                            uint64_t r171; bool o172 = __builtin_add_overflow(t170, ((uint64_t)1ULL), &r171);
                            uint64_t t173 = r171;
                            gm_l11 = t173;
                        }
                    }
lb156:;
                }
            }
            {
                void * t174 = gm_l0;
                void * t175 = ((uint8_t *)&F.l3);
                uint64_t t176 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t177 = gm_l5;
                uint64_t t178 = (uintptr_t)t177;
                if (t176 > t178 || t178 > (uintptr_t)40) gm_trap_slice(t176, t178, 40, gm_srcfde9lsgge1ld, 322, 24);
                gm_b16_8 t179;
                GM_ST(void *, (uint8_t *)&t179, (void *)(((uint8_t *)t175 + t176 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t179 + 8, (uintptr_t)(t178 - t176));
                gm_b16_8 t180 = GM_LD(gm_b16_8, ((uint8_t *)&t179));
                gm_tftb1odc739t5 t181 = gm_f_extend_0cs85gs(t174, t180);
                if (((((uint8_t *)&t181))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t181) + 1, gm_srcfde9lsgge1ld, 322, 13);
                gm_b0_1 t182 = GM_LD(gm_b0_1, (((uint8_t *)&t181) + 1));
            }
ix72:;
        }
        goto ix66;
ie66:;
        {
            void * t184 = gm_l0;
            uint64_t t185 = (uintptr_t)((uint64_t)0ULL);
            if (t185 >= (uintptr_t)(40)) gm_trap_index(t185, 40, gm_srcfde9lsgge1ld, 325, 18);
            uint8_t t186 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + t185 * 1));
            gm_tftb1odc739t5 t187 = gm_f_push_t83rg2m(t184, t186);
            if (((((uint8_t *)&t187))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t187) + 1, gm_srcfde9lsgge1ld, 325, 9);
            gm_b0_1 t188 = GM_LD(gm_b0_1, (((uint8_t *)&t187) + 1));
        }
        {
            uint64_t t190 = gm_l5;
            uint8_t t191 = (t190 > ((uint64_t)1ULL));
            if (!t191) goto ie189;
            {
                void * t193 = gm_l0;
                gm_tftb1odc739t5 t194 = gm_f_push_t83rg2m(t193, ((uint8_t)46ULL));
                if (((((uint8_t *)&t194))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t194) + 1, gm_srcfde9lsgge1ld, 327, 13);
                gm_b0_1 t195 = GM_LD(gm_b0_1, (((uint8_t *)&t194) + 1));
            }
            {
                void * t196 = gm_l0;
                void * t197 = ((uint8_t *)&F.l3);
                uint64_t t198 = (uintptr_t)((uint64_t)1ULL);
                uint64_t t199 = gm_l5;
                uint64_t t200 = (uintptr_t)t199;
                if (t198 > t200 || t200 > (uintptr_t)40) gm_trap_slice(t198, t200, 40, gm_srcfde9lsgge1ld, 328, 24);
                gm_b16_8 t201;
                GM_ST(void *, (uint8_t *)&t201, (void *)(((uint8_t *)t197 + t198 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t201 + 8, (uintptr_t)(t200 - t198));
                gm_b16_8 t202 = GM_LD(gm_b16_8, ((uint8_t *)&t201));
                gm_tftb1odc739t5 t203 = gm_f_extend_0cs85gs(t196, t202);
                if (((((uint8_t *)&t203))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t203) + 1, gm_srcfde9lsgge1ld, 328, 13);
                gm_b0_1 t204 = GM_LD(gm_b0_1, (((uint8_t *)&t203) + 1));
            }
            goto ix189;
ie189:;
ix189:;
        }
        {
            void * t205 = gm_l0;
            int32_t t206 = gm_l6;
            gm_t19enj90vg8bd t207;
            GM_ST(int32_t, ((uint8_t *)&t207 + 0), t206);
            gm_tftb1odc739t5 t208 = gm_f_write_jkg06fo(t205, t207);
            if (((((uint8_t *)&t208))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t208) + 1, gm_srcfde9lsgge1ld, 330, 9);
            gm_b0_1 t209 = GM_LD(gm_b0_1, (((uint8_t *)&t208) + 1));
        }
ix66:;
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
__attribute__((aligned(4))) double gm_f_fmt_float_bits_9d6f5r5(void * a0, uint16_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_fmt_float_bits_9d6f5r5 F;
    void * gm_l0; /* m */
    uint16_t gm_l1; /* bits */
    gm_b16_8 gm_l2; /* b */
    unsigned __int128 gm_l3; /* raw */
    uint16_t gm_l4; /* h */
    uint8_t gm_l5; /* neg */
    int32_t gm_l6; /* e */
    uint16_t gm_l7; /* man */
    double gm_l8; /* mag */
    GM_STACK_CHECK_SHALLOW(&F, 490u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        gm_b16_8 t3 = gm_l2;
        unsigned __int128 t4 = gm_f_get_uint_bq8c4b7(t2, t3);
        gm_l3 = t4;
    }
    {
        uint16_t t6 = gm_l1;
        uint8_t t7 = ((t6 == ((uint16_t)64ULL)));
        if (!t7) goto ie5;
        {
            unsigned __int128 t9 = gm_l3;
            uint64_t t10 = (uint64_t)t9;
            double t11;
            memset(&t11, 0, sizeof t11); memcpy(&t11, ((uint8_t *)&t10), sizeof t11 < 8 ? sizeof t11 : 8);
            uint8_t t12 = 0;
            F.ret = t11;
            goto gm_fret;
        }
        goto ix5;
ie5:;
ix5:;
    }
    {
        uint16_t t14 = gm_l1;
        uint8_t t15 = ((t14 == ((uint16_t)32ULL)));
        if (!t15) goto ie13;
        {
            unsigned __int128 t17 = gm_l3;
            uint32_t t18 = (uint32_t)t17;
            float t19;
            memset(&t19, 0, sizeof t19); memcpy(&t19, ((uint8_t *)&t18), sizeof t19 < 4 ? sizeof t19 : 4);
            double t20 = (double)((double)t19);
            uint8_t t21 = 0;
            F.ret = t20;
            goto gm_fret;
        }
        goto ix13;
ie13:;
ix13:;
    }
    {
        unsigned __int128 t22 = gm_l3;
        uint16_t t23 = (uint16_t)t22;
        gm_l4 = t23;
    }
    {
        uint16_t t24 = gm_l4;
        uint16_t t25 = (uint16_t)(t24 & ((uint16_t)32768ULL));
        uint8_t t26 = (!(t25 == ((uint16_t)0ULL)));
        gm_l5 = t26;
    }
    {
        uint16_t t27 = gm_l4;
        unsigned __int128 n28 = (unsigned __int128)((uint64_t)10ULL);
        if (n28 >= 16) gm_trap_shift((uint64_t)n28, "u16", gm_srcfde9lsgge1ld, 203, 15);
        uint16_t t29 = (uint16_t)(t27 >> (int)n28);
        uint16_t t30 = (uint16_t)(t29 & ((uint16_t)31ULL));
        int32_t t31 = (int32_t)t30;
        gm_l6 = t31;
    }
    {
        uint16_t t32 = gm_l4;
        uint16_t t33 = (uint16_t)(t32 & ((uint16_t)1023ULL));
        gm_l7 = t33;
    }
    {
        int32_t t35 = gm_l6;
        uint8_t t36 = ((t35 == ((int32_t)31LL)));
        if (!t36) goto ie34;
        {
            uint16_t t38 = gm_l4;
            uint64_t t39 = (uint64_t)t38;
            uint64_t t40 = (uint64_t)(t39 & ((uint64_t)32768ULL));
            unsigned __int128 n41 = (unsigned __int128)((uint64_t)48ULL);
            if (n41 >= 64) gm_trap_shift((uint64_t)n41, "u64", gm_srcfde9lsgge1ld, 207, 18);
            uint64_t r42 = (uint64_t)((uint64_t)t40 << (int)n41);
            bool o43 = (uint64_t)(r42 >> (int)n41) != t40;
            if (o43) gm_trap("integer overflow in `<<`", gm_srcfde9lsgge1ld, 207, 18);
            uint64_t t44 = r42;
            uint64_t t45 = (uint64_t)(t44 | ((uint64_t)9218868437227405312ULL));
            uint64_t t46;
            uint16_t t48 = gm_l7;
            uint8_t t49 = ((t48 == ((uint16_t)0ULL)));
            if (!t49) goto ie47;
            uint64_t t51 = ((uint64_t)0ULL);
            t46 = t51;
            goto ix47;
ie47:;
            uint16_t t53 = gm_l7;
            uint64_t t54 = (uint64_t)t53;
            unsigned __int128 n55 = (unsigned __int128)((uint64_t)42ULL);
            if (n55 >= 64) gm_trap_shift((uint64_t)n55, "u64", gm_srcfde9lsgge1ld, 207, 125);
            uint64_t r56 = (uint64_t)((uint64_t)t54 << (int)n55);
            bool o57 = (uint64_t)(r56 >> (int)n55) != t54;
            if (o57) gm_trap("integer overflow in `<<`", gm_srcfde9lsgge1ld, 207, 125);
            uint64_t t58 = r56;
            uint64_t t59 = (uint64_t)(((uint64_t)2251799813685248ULL) | t58);
            uint64_t t60 = t59;
            t46 = t60;
ix47:;
            uint64_t t61 = (uint64_t)(t45 | t46);
            double t62;
            memset(&t62, 0, sizeof t62); memcpy(&t62, ((uint8_t *)&t61), sizeof t62 < 8 ? sizeof t62 : 8);
            uint8_t t63 = 0;
            F.ret = t62;
            goto gm_fret;
        }
        goto ix34;
ie34:;
ix34:;
    }
    {
        double t64;
        int32_t t66 = gm_l6;
        uint8_t t67 = ((t66 == ((int32_t)0LL)));
        if (!t67) goto ie65;
        uint16_t t69 = gm_l7;
        double t70 = (double)(t69);
        double t71 = gm_f_fmt_pow2_1p38akk(((int32_t)-24LL));
        double t72 = (t70 * t71);
        double t73 = t72;
        t64 = t73;
        goto ix65;
ie65:;
        uint16_t t75 = gm_l7;
        uint16_t r76; bool o77 = __builtin_add_overflow(((uint16_t)1024ULL), t75, &r76);
        if (o77) gm_trap("integer overflow in `+`", gm_srcfde9lsgge1ld, 209, 66);
        uint16_t t78 = r76;
        double t79 = (double)(t78);
        int32_t t80 = gm_l6;
        int32_t r81; bool o82 = __builtin_sub_overflow(t80, ((int32_t)25LL), &r81);
        if (o82) gm_trap("integer overflow in `-`", gm_srcfde9lsgge1ld, 209, 89);
        int32_t t83 = r81;
        double t84 = gm_f_fmt_pow2_1p38akk(t83);
        double t85 = (t79 * t84);
        double t86 = t85;
        t64 = t86;
ix65:;
        gm_l8 = t64;
    }
    {
        double t87;
        uint8_t t89 = gm_l5;
        if (!t89) goto ie88;
        double t91 = gm_l8;
        uint64_t t92;
        memset(&t92, 0, sizeof t92); memcpy(&t92, ((uint8_t *)&t91), sizeof t92 < 8 ? sizeof t92 : 8);
        uint64_t t93 = (uint64_t)(t92 | ((uint64_t)9223372036854775808ULL));
        double t94;
        memset(&t94, 0, sizeof t94); memcpy(&t94, ((uint8_t *)&t93), sizeof t94 < 8 ? sizeof t94 : 8);
        double t95 = t94;
        t87 = t95;
        goto ix88;
ie88:;
        double t97 = gm_l8;
        double t98 = t97;
        t87 = t98;
ix88:;
        uint8_t t99 = 0;
        F.ret = t87;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_b0_1 gm_f_fmt_float_spec_glt99ob(void *env, gm_node *cx, gm_node *inst, void * a0, double a1, uint16_t a2, void * a3) {
    struct gm_fr_fmt_float_spec_glt99ob F;
    void * gm_l0; /* body */
    double gm_l1; /* a */
    uint16_t gm_l2; /* bits */
    void * gm_l3; /* spec */
    uint32_t gm_l4; /* p */
    gm_b16_8 gm_l5; /* buf */
    gm_b16_8 gm_l6; /* text */
    uint64_t gm_l7; /* e */
    int32_t gm_l8; /* ex */
    gm_t43v1gep754th gm_l10; /* sh */
    uint32_t gm_l11; /* p */
    gm_b16_8 gm_l12; /* buf */
    GM_STACK_CHECK(&F, 926u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        void * t3 = gm_l3;
        uint8_t t4 = ((GM_LD(uint8_t, (((uint8_t *)t3) + 20) + 0) == ((uint8_t)5ULL)));
        if (!t4) goto ie2;
        {
            void * t7 = gm_l3;
            if (!(((((uint8_t *)t7) + 12))[4] != 0)) goto ie6;
            uint32_t t8 = GM_LD(uint32_t, (((uint8_t *)t7) + 12));
            gm_l4 = t8;
            {
                uint32_t t10 = gm_l4;
                uint64_t t11 = gm_f_sci_buf_len_tedn1r4(t10);
                gm_tabb710d9mq2e t12;
                gm_f_make_fill_jpvm0kc((void *)&t12, (void *)NULL, cx, NULL, t11, ((uint8_t)0ULL), &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_k3pvi109nh9)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t12))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t12) + 8, gm_srcfde9lsgge1ld, 408, 20);
                gm_b16_8 t13 = GM_LD(gm_b16_8, (((uint8_t *)&t12) + 8));
                gm_l5 = t13;
            }
            {
                double t14 = gm_l1;
                uint32_t t15 = gm_l4;
                gm_b16_8 t16 = gm_l5;
                gm_b16_8 t17 = gm_f_sci_oikoiqs(t14, t15, t16);
                gm_l6 = t17;
            }
            {
                gm_l7 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t19 = gm_l7;
                    uint64_t t20 = (uintptr_t)t19;
                    uint64_t t21 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                    if (t20 >= (uintptr_t)(t21)) gm_trap_index(t20, t21, gm_srcfde9lsgge1ld, 411, 19);
                    uint8_t t22 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t20 * 1));
                    uint8_t t23 = (!(t22 == ((uint8_t)101ULL)));
                    if (!t23) goto lb18;
                    {
                        uint64_t t26 = gm_l7;
                        uint64_t r27; bool o28 = __builtin_add_overflow(t26, ((uint64_t)1ULL), &r27);
                        if (o28) gm_trap("integer overflow in `+`", gm_srcfde9lsgge1ld, 411, 40);
                        uint64_t t29 = r27;
                        gm_l7 = t29;
                    }
lc18:;
                }
lb18:;
            }
            {
                uint64_t t30 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                void * t31 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)));
                uint64_t t32 = gm_l7;
                uint64_t t33 = (uintptr_t)t32;
                if (t33 > (uintptr_t)t30 || (uintptr_t)t30 > (uintptr_t)t30) gm_trap_slice(t33, (uintptr_t)t30, t30, gm_srcfde9lsgge1ld, 412, 33);
                gm_b16_8 t34;
                GM_ST(void *, (uint8_t *)&t34, (void *)(((uint8_t *)t31 + t33 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t34 + 8, (uintptr_t)((uintptr_t)t30 - t33));
                uint64_t t35 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                void * t36 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l5)));
                uint64_t t37 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t38 = (uintptr_t)((uint64_t)0ULL);
                if (t37 > t38 || t38 > (uintptr_t)t35) gm_trap_slice(t37, t38, t35, gm_srcfde9lsgge1ld, 412, 44);
                gm_b16_8 t39;
                GM_ST(void *, (uint8_t *)&t39, (void *)(((uint8_t *)t36 + t37 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t39 + 8, (uintptr_t)(t38 - t37));
                gm_t43v1gep754th t40 = gm_f_fmt_split_exp_0kapfov(t34, t39);
                int32_t t41 = GM_LD(int32_t, (((uint8_t *)&t40) + 8));
                gm_l8 = t41;
            }
            {
                void * t42 = gm_l0;
                uint64_t t43 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                void * t44 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)));
                uint64_t t45 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t46 = gm_l7;
                uint64_t t47 = (uintptr_t)t46;
                if (t45 > t47 || t47 > (uintptr_t)t43) gm_trap_slice(t45, t47, t43, gm_srcfde9lsgge1ld, 413, 25);
                gm_b16_8 t48;
                GM_ST(void *, (uint8_t *)&t48, (void *)(((uint8_t *)t44 + t45 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t48 + 8, (uintptr_t)(t47 - t45));
                gm_tftb1odc739t5 t49 = gm_f_extend_0cs85gs(t42, t48);
                if (((((uint8_t *)&t49))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t49) + 1, gm_srcfde9lsgge1ld, 413, 13);
                gm_b0_1 t50 = GM_LD(gm_b0_1, (((uint8_t *)&t49) + 1));
            }
            {
                void * t51 = gm_l0;
                int32_t t52 = gm_l8;
                gm_t19enj90vg8bd t53;
                GM_ST(int32_t, ((uint8_t *)&t53 + 0), t52);
                gm_tftb1odc739t5 t54 = gm_f_write_jkg06fo(t51, t53);
                if (((((uint8_t *)&t54))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t54) + 1, gm_srcfde9lsgge1ld, 414, 13);
                gm_b0_1 t55 = GM_LD(gm_b0_1, (((uint8_t *)&t54) + 1));
            }
            {
                gm_b16_8 t56 = gm_l5;
                gm_b16_8 t57 = GM_LD(gm_b16_8, ((uint8_t *)&t56));
                gm_b0_1 t58 = gm_f_free_rkuh2rm((void *)NULL, cx, NULL, t57, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_k30q9glr4ue)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix6;
ie6:;
            double t60 = gm_l1;
            uint8_t t61 = ((t60 == ((double)0e0)));
            if (!t61) goto ie59;
            {
                void * t63 = gm_l0;
                gm_b16_8 t64;
                GM_ST(void *, (uint8_t *)&t64, (void *)(gm_sdc2n4tsqs8nn));
                GM_ST(uintptr_t, (uint8_t *)&t64 + 8, (uintptr_t)(3));
                gm_tftb1odc739t5 t65 = gm_f_extend_0cs85gs(t63, t64);
                if (((((uint8_t *)&t65))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t65) + 1, gm_srcfde9lsgge1ld, 417, 13);
                gm_b0_1 t66 = GM_LD(gm_b0_1, (((uint8_t *)&t65) + 1));
            }
            goto ix59;
ie59:;
            {
                gm_t6kvj62qe5fp2 t68;
                for (uint64_t i = 0; i < 40; i++) GM_ST(uint8_t, (uint8_t *)&t68 + i * 1, ((uint8_t)0ULL));
                F.l9 = t68;
            }
            {
                double t69 = gm_l1;
                uint16_t t70 = gm_l2;
                void * t71 = ((uint8_t *)&F.l9);
                if ((uintptr_t)0 > (uintptr_t)40 || (uintptr_t)40 > (uintptr_t)40) gm_trap_slice((uintptr_t)0, (uintptr_t)40, 40, gm_srcfde9lsgge1ld, 420, 41);
                gm_b16_8 t72;
                GM_ST(void *, (uint8_t *)&t72, (void *)(((uint8_t *)t71 + (uintptr_t)0 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t72 + 8, (uintptr_t)((uintptr_t)40 - (uintptr_t)0));
                gm_t43v1gep754th t73 = gm_f_fmt_shortest_2l54as9((void *)NULL, cx, NULL, t69, t70, t72);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l10 = t73;
            }
            {
                void * t74 = gm_l0;
                uint64_t t75 = (uintptr_t)((uint64_t)0ULL);
                if (t75 >= (uintptr_t)(40)) gm_trap_index(t75, 40, gm_srcfde9lsgge1ld, 421, 23);
                uint8_t t76 = GM_LD(uint8_t, (((uint8_t *)&F.l9) + t75 * 1));
                gm_tftb1odc739t5 t77 = gm_f_push_t83rg2m(t74, t76);
                if (((((uint8_t *)&t77))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t77) + 1, gm_srcfde9lsgge1ld, 421, 13);
                gm_b0_1 t78 = GM_LD(gm_b0_1, (((uint8_t *)&t77) + 1));
            }
            {
                uint64_t t80 = GM_LD(uint64_t, (((uint8_t *)&gm_l10) + 0));
                uint8_t t81 = (t80 > ((uint64_t)1ULL));
                if (!t81) goto ie79;
                {
                    void * t83 = gm_l0;
                    gm_tftb1odc739t5 t84 = gm_f_push_t83rg2m(t83, ((uint8_t)46ULL));
                    if (((((uint8_t *)&t84))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t84) + 1, gm_srcfde9lsgge1ld, 423, 17);
                    gm_b0_1 t85 = GM_LD(gm_b0_1, (((uint8_t *)&t84) + 1));
                }
                {
                    void * t86 = gm_l0;
                    void * t87 = ((uint8_t *)&F.l9);
                    uint64_t t88 = (uintptr_t)((uint64_t)1ULL);
                    uint64_t t89 = GM_LD(uint64_t, (((uint8_t *)&gm_l10) + 0));
                    uint64_t t90 = (uintptr_t)t89;
                    if (t88 > t90 || t90 > (uintptr_t)40) gm_trap_slice(t88, t90, 40, gm_srcfde9lsgge1ld, 424, 29);
                    gm_b16_8 t91;
                    GM_ST(void *, (uint8_t *)&t91, (void *)(((uint8_t *)t87 + t88 * 1)));
                    GM_ST(uintptr_t, (uint8_t *)&t91 + 8, (uintptr_t)(t90 - t88));
                    gm_b16_8 t92 = GM_LD(gm_b16_8, ((uint8_t *)&t91));
                    gm_tftb1odc739t5 t93 = gm_f_extend_0cs85gs(t86, t92);
                    if (((((uint8_t *)&t93))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t93) + 1, gm_srcfde9lsgge1ld, 424, 17);
                    gm_b0_1 t94 = GM_LD(gm_b0_1, (((uint8_t *)&t93) + 1));
                }
                goto ix79;
ie79:;
ix79:;
            }
            {
                void * t95 = gm_l0;
                int32_t t96 = GM_LD(int32_t, (((uint8_t *)&gm_l10) + 8));
                gm_t19enj90vg8bd t97;
                GM_ST(int32_t, ((uint8_t *)&t97 + 0), t96);
                gm_tftb1odc739t5 t98 = gm_f_write_jkg06fo(t95, t97);
                if (((((uint8_t *)&t98))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t98) + 1, gm_srcfde9lsgge1ld, 426, 13);
                gm_b0_1 t99 = GM_LD(gm_b0_1, (((uint8_t *)&t98) + 1));
            }
ix59:;
ix6:;
        }
        {
            gm_b0_1 t100;
            uint8_t t101 = 0;
            F.ret = t100;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t103 = gm_l3;
        if (!(((((uint8_t *)t103) + 12))[4] != 0)) goto ie102;
        uint32_t t104 = GM_LD(uint32_t, (((uint8_t *)t103) + 12));
        gm_l11 = t104;
        {
            uint32_t t106 = gm_l11;
            uint64_t t107 = gm_f_fixed_buf_len_gor7a4d(t106);
            gm_tabb710d9mq2e t108;
            gm_f_make_fill_jpvm0kc((void *)&t108, (void *)NULL, cx, NULL, t107, ((uint8_t)0ULL), &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kbfuu8gel4m)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t108))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t108) + 8, gm_srcfde9lsgge1ld, 431, 16);
            gm_b16_8 t109 = GM_LD(gm_b16_8, (((uint8_t *)&t108) + 8));
            gm_l12 = t109;
        }
        {
            void * t110 = gm_l0;
            double t111 = gm_l1;
            uint32_t t112 = gm_l11;
            gm_b16_8 t113 = gm_l12;
            gm_b16_8 t114 = gm_f_fixed_rppim5r(t111, t112, t113);
            gm_tftb1odc739t5 t115 = gm_f_extend_0cs85gs(t110, t114);
            if (((((uint8_t *)&t115))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t115) + 1, gm_srcfde9lsgge1ld, 432, 9);
            gm_b0_1 t116 = GM_LD(gm_b0_1, (((uint8_t *)&t115) + 1));
        }
        {
            gm_b16_8 t117 = gm_l12;
            gm_b16_8 t118 = GM_LD(gm_b16_8, ((uint8_t *)&t117));
            gm_b0_1 t119 = gm_f_free_rkuh2rm((void *)NULL, cx, NULL, t118, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kkpcsaqkj4k)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_b0_1 t120;
            uint8_t t121 = 0;
            F.ret = t120;
            goto gm_fret;
        }
        goto ix102;
ie102:;
ix102:;
    }
    {
        void * t122 = gm_l0;
        double t123 = gm_l1;
        uint16_t t124 = gm_l2;
        gm_b0_1 t125 = gm_f_fmt_float_1ivmudg((void *)NULL, cx, NULL, t122, t123, t124);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fmt_int_svhqra7(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3) {
    struct gm_fr_fmt_int_svhqra7 F;
    void * gm_l0; /* m */
    void * gm_l1; /* out */
    gm_t88rcucg1b7uk gm_l2; /* t */
    gm_b16_8 gm_l3; /* b */
    unsigned __int128 gm_l4; /* v */
    gm_ttovhfee8mf28 gm_l5; /* info */
    GM_STACK_CHECK(&F, 262u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        void * t2 = gm_l0;
        gm_t88rcucg1b7uk t3 = gm_l2;
        gm_b16_8 t4 = gm_l3;
        gm_b16_8 t5 = GM_LD(gm_b16_8, ((uint8_t *)&t4));
        unsigned __int128 t6 = gm_f_get_int_pg7p4gc(t2, GM_CONV(gm_csguankko4lmhd, t3), t5);
        gm_l4 = t6;
    }
    {
        void * t7 = gm_l0;
        gm_t88rcucg1b7uk t8 = gm_l2;
        gm_tg5lql6itufk7 t9 = gm_f_int_info_ulcevdv(t7, GM_CONV(gm_csguankko4lmhd, t8));
        gm_ttovhfee8mf28 t10;
        if (((((uint8_t *)&t9))[8] != 0)) {
            gm_ttovhfee8mf28 t11 = GM_LD(gm_ttovhfee8mf28, (((uint8_t *)&t9) + 0));
            t10 = t11;
        } else {
            gm_b0_1 t12;
            uint8_t t13 = 0;
            F.ret = t12;
            goto gm_fret;
        }
        gm_l5 = t10;
    }
    {
        uint8_t t15 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
        if (!t15) goto ie14;
        {
            void * t17 = gm_l1;
            unsigned __int128 t18 = gm_l4;
            __int128 t19;
            memset(&t19, 0, sizeof t19); memcpy(&t19, ((uint8_t *)&t18), sizeof t19 < 16 ? sizeof t19 : 16);
            gm_tpl3q6e8s9qo1 t20;
            GM_ST(__int128, ((uint8_t *)&t20 + 0), t19);
            gm_tftb1odc739t5 t21 = gm_f_write_cnos3th(t17, t20);
            if (((((uint8_t *)&t21))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t21) + 1, gm_srcfde9lsgge1ld, 183, 9);
            gm_b0_1 t22 = GM_LD(gm_b0_1, (((uint8_t *)&t21) + 1));
        }
        goto ix14;
ie14:;
        {
            void * t24 = gm_l1;
            unsigned __int128 t25 = gm_l4;
            gm_tsn2omssg6cpf t26;
            GM_ST(unsigned __int128, ((uint8_t *)&t26 + 0), t25);
            gm_tftb1odc739t5 t27 = gm_f_write_1599ben(t24, t26);
            if (((((uint8_t *)&t27))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t27) + 1, gm_srcfde9lsgge1ld, 185, 9);
            gm_b0_1 t28 = GM_LD(gm_b0_1, (((uint8_t *)&t27) + 1));
        }
ix14:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_b0_1 gm_f_fmt_pad_ev4lnnu(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2, uint32_t a3, uint8_t a4, gm_t9qkh0rsooc8d a5, uint8_t a6, uint8_t a7) {
    struct gm_fr_fmt_pad_ev4lnnu F;
    void * gm_l0; /* out */
    gm_b16_8 gm_l1; /* head */
    gm_b16_8 gm_l2; /* body */
    uint32_t gm_l3; /* width */
    uint8_t gm_l4; /* fill */
    gm_t9qkh0rsooc8d gm_l5; /* align */
    uint8_t gm_l6; /* zero */
    uint8_t gm_l7; /* number */
    uint64_t gm_l8; /* len */
    gm_b16_8 gm_l9; /* <tmp> */
    uint64_t gm_l10; /* <tmp> */
    uint8_t gm_l11; /* c */
    gm_b16_8 gm_l12; /* <tmp> */
    uint64_t gm_l13; /* <tmp> */
    uint8_t gm_l14; /* c */
    uint64_t gm_l15; /* gap */
    uint64_t gm_l16; /* <tmp> */
    uint64_t gm_l17; /* <tmp> */
    uint64_t gm_l18; /* before */
    uint64_t gm_l19; /* after */
    uint64_t gm_l20; /* <tmp> */
    uint64_t gm_l21; /* <tmp> */
    uint64_t gm_l22; /* <tmp> */
    uint64_t gm_l23; /* <tmp> */
    GM_STACK_CHECK(&F, 810u);
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
        gm_l8 = ((uint64_t)0ULL);
    }
    {
        {
            gm_b16_8 t3 = gm_l1;
            gm_l9 = t3;
        }
        {
            gm_l10 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t5 = gm_l10;
                uint64_t t6 = GM_LD(uintptr_t, ((uint8_t *)&gm_l9) + 8);
                uint8_t t7 = (t5 < t6);
                if (!t7) goto lb4;
                {
                    uint64_t t10 = gm_l10;
                    uint64_t t11 = (uintptr_t)t10;
                    uint64_t t12 = GM_LD(uintptr_t, ((uint8_t *)&gm_l9) + 8);
                    uint8_t t13 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l9)) + t11 * 1));
                    uint8_t t16 = GM_LD(uint8_t, ((uint8_t *)&t13));
                    gm_l11 = t16;
                    {
                        uint8_t t19 = gm_l11;
                        uint8_t t20 = (uint8_t)(t19 & ((uint8_t)192ULL));
                        uint8_t t21 = (!(t20 == ((uint8_t)128ULL)));
                        if (!t21) goto ie18;
                        {
                            uint64_t t23 = gm_l8;
                            uint64_t r24; bool o25 = __builtin_add_overflow(t23, ((uint64_t)1ULL), &r24);
                            if (o25) gm_trap("integer overflow in `+`", gm_srcfde9lsgge1ld, 443, 47);
                            uint64_t t26 = r24;
                            gm_l8 = t26;
                        }
                        goto ix18;
ie18:;
ix18:;
                    }
                    goto me14;
mn15:;
                    gm_trap("no match arm matched the scrutinee", gm_srcfde9lsgge1ld, 443, 5);
me14:;
                }
lc4:;
                {
                    uint64_t t28 = gm_l10;
                    uint64_t r29; bool o30 = __builtin_add_overflow(t28, ((uint64_t)1ULL), &r29);
                    uint64_t t31 = r29;
                    gm_l10 = t31;
                }
            }
lb4:;
        }
    }
    {
        {
            gm_b16_8 t33 = gm_l2;
            gm_l12 = t33;
        }
        {
            gm_l13 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t35 = gm_l13;
                uint64_t t36 = GM_LD(uintptr_t, ((uint8_t *)&gm_l12) + 8);
                uint8_t t37 = (t35 < t36);
                if (!t37) goto lb34;
                {
                    uint64_t t40 = gm_l13;
                    uint64_t t41 = (uintptr_t)t40;
                    uint64_t t42 = GM_LD(uintptr_t, ((uint8_t *)&gm_l12) + 8);
                    uint8_t t43 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l12)) + t41 * 1));
                    uint8_t t46 = GM_LD(uint8_t, ((uint8_t *)&t43));
                    gm_l14 = t46;
                    {
                        uint8_t t49 = gm_l14;
                        uint8_t t50 = (uint8_t)(t49 & ((uint8_t)192ULL));
                        uint8_t t51 = (!(t50 == ((uint8_t)128ULL)));
                        if (!t51) goto ie48;
                        {
                            uint64_t t53 = gm_l8;
                            uint64_t r54; bool o55 = __builtin_add_overflow(t53, ((uint64_t)1ULL), &r54);
                            if (o55) gm_trap("integer overflow in `+`", gm_srcfde9lsgge1ld, 444, 47);
                            uint64_t t56 = r54;
                            gm_l8 = t56;
                        }
                        goto ix48;
ie48:;
ix48:;
                    }
                    goto me44;
mn45:;
                    gm_trap("no match arm matched the scrutinee", gm_srcfde9lsgge1ld, 444, 5);
me44:;
                }
lc34:;
                {
                    uint64_t t58 = gm_l13;
                    uint64_t r59; bool o60 = __builtin_add_overflow(t58, ((uint64_t)1ULL), &r59);
                    uint64_t t61 = r59;
                    gm_l13 = t61;
                }
            }
lb34:;
        }
    }
    {
        uint64_t t62;
        uint32_t t64 = gm_l3;
        uint64_t t65 = (uint64_t)t64;
        uint64_t t66 = gm_l8;
        uint8_t t67 = (t65 > t66);
        if (!t67) goto ie63;
        uint32_t t69 = gm_l3;
        uint64_t t70 = (uint64_t)t69;
        uint64_t t71 = gm_l8;
        uint64_t r72; bool o73 = __builtin_sub_overflow(t70, t71, &r72);
        if (o73) gm_trap("integer overflow in `-`", gm_srcfde9lsgge1ld, 445, 36);
        uint64_t t74 = r72;
        uint64_t t75 = t74;
        t62 = t75;
        goto ix63;
ie63:;
        uint64_t t77 = ((uint64_t)0ULL);
        t62 = t77;
ix63:;
        gm_l15 = t62;
    }
    {
        uint8_t t79 = gm_l6;
        if (!t79) goto ie78;
        {
            void * t81 = gm_l0;
            gm_b16_8 t82 = gm_l1;
            gm_tftb1odc739t5 t83 = gm_f_extend_0cs85gs(t81, t82);
            if (((((uint8_t *)&t83))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t83) + 1, gm_srcfde9lsgge1ld, 447, 9);
            gm_b0_1 t84 = GM_LD(gm_b0_1, (((uint8_t *)&t83) + 1));
        }
        {
            {
                gm_l17 = ((uint64_t)0ULL);
            }
            {
                uint64_t t86 = gm_l15;
                gm_l16 = t86;
            }
            {
                for (;;) {
                    uint64_t t88 = gm_l17;
                    uint64_t t89 = gm_l16;
                    uint8_t t90 = (t88 < t89);
                    if (!t90) goto lb87;
                    {
                        uint64_t t93 = gm_l17;
                        {
                            void * t97 = gm_l0;
                            gm_tftb1odc739t5 t98 = gm_f_push_t83rg2m(t97, ((uint8_t)48ULL));
                            if (((((uint8_t *)&t98))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t98) + 1, gm_srcfde9lsgge1ld, 448, 27);
                            gm_b0_1 t99 = GM_LD(gm_b0_1, (((uint8_t *)&t98) + 1));
                        }
                        goto me94;
mn95:;
                        gm_trap("no match arm matched the scrutinee", gm_srcfde9lsgge1ld, 448, 9);
me94:;
                    }
lc87:;
                    {
                        uint64_t t101 = gm_l17;
                        uint64_t r102; bool o103 = __builtin_add_overflow(t101, ((uint64_t)1ULL), &r102);
                        uint64_t t104 = r102;
                        gm_l17 = t104;
                    }
                }
lb87:;
            }
        }
        {
            void * t105 = gm_l0;
            gm_b16_8 t106 = gm_l2;
            gm_tftb1odc739t5 t107 = gm_f_extend_0cs85gs(t105, t106);
            if (((((uint8_t *)&t107))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t107) + 1, gm_srcfde9lsgge1ld, 449, 9);
            gm_b0_1 t108 = GM_LD(gm_b0_1, (((uint8_t *)&t107) + 1));
        }
        {
            gm_b0_1 t109;
            uint8_t t110 = 0;
            F.ret = t109;
            goto gm_fret;
        }
        goto ix78;
ie78:;
ix78:;
    }
    {
        gm_l18 = ((uint64_t)0ULL);
    }
    {
        gm_l19 = ((uint64_t)0ULL);
    }
    {
        uint8_t t117 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
        switch (t117) { case ((uint8_t)0ULL): goto ma115; case ((uint8_t)1ULL): goto ma112; case ((uint8_t)2ULL): goto ma113; case ((uint8_t)3ULL): goto ma114; default: goto ma116; }
ma112:;
        uint8_t t119 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
        if (t119 != ((uint8_t)1ULL)) goto mn118;
        {
            uint64_t t121 = gm_l15;
            gm_l19 = t121;
        }
        goto me111;
mn118:;
ma113:;
        uint8_t t123 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
        if (t123 != ((uint8_t)2ULL)) goto mn122;
        {
            uint64_t t125 = gm_l15;
            gm_l18 = t125;
        }
        goto me111;
mn122:;
ma114:;
        uint8_t t127 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
        if (t127 != ((uint8_t)3ULL)) goto mn126;
        {
            uint64_t t129 = gm_l15;
            if (((uint64_t)2ULL) == 0) gm_trap("division by zero", gm_srcfde9lsgge1ld, 458, 22);
            uint64_t t130 = (uint64_t)(t129 / ((uint64_t)2ULL));
            gm_l18 = t130;
        }
        {
            uint64_t t131 = gm_l15;
            uint64_t t132 = gm_l15;
            if (((uint64_t)2ULL) == 0) gm_trap("division by zero", gm_srcfde9lsgge1ld, 459, 27);
            uint64_t t133 = (uint64_t)(t132 / ((uint64_t)2ULL));
            uint64_t r134; bool o135 = __builtin_sub_overflow(t131, t133, &r134);
            if (o135) gm_trap("integer overflow in `-`", gm_srcfde9lsgge1ld, 459, 21);
            uint64_t t136 = r134;
            gm_l19 = t136;
        }
        goto me111;
mn126:;
ma115:;
        uint8_t t138 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
        if (t138 != ((uint8_t)0ULL)) goto mn137;
        {
            uint8_t t141 = gm_l7;
            if (!t141) goto ie140;
            {
                uint64_t t143 = gm_l15;
                gm_l18 = t143;
            }
            goto ix140;
ie140:;
            {
                uint64_t t145 = gm_l15;
                gm_l19 = t145;
            }
ix140:;
        }
        goto me111;
mn137:;
ma116:;
        gm_trap("no match arm matched the scrutinee", gm_srcfde9lsgge1ld, 454, 5);
me111:;
    }
    {
        {
            gm_l21 = ((uint64_t)0ULL);
        }
        {
            uint64_t t147 = gm_l18;
            gm_l20 = t147;
        }
        {
            for (;;) {
                uint64_t t149 = gm_l21;
                uint64_t t150 = gm_l20;
                uint8_t t151 = (t149 < t150);
                if (!t151) goto lb148;
                {
                    uint64_t t154 = gm_l21;
                    {
                        void * t158 = gm_l0;
                        uint8_t t159 = gm_l4;
                        gm_tftb1odc739t5 t160 = gm_f_push_t83rg2m(t158, t159);
                        if (((((uint8_t *)&t160))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t160) + 1, gm_srcfde9lsgge1ld, 465, 26);
                        gm_b0_1 t161 = GM_LD(gm_b0_1, (((uint8_t *)&t160) + 1));
                    }
                    goto me155;
mn156:;
                    gm_trap("no match arm matched the scrutinee", gm_srcfde9lsgge1ld, 465, 5);
me155:;
                }
lc148:;
                {
                    uint64_t t163 = gm_l21;
                    uint64_t r164; bool o165 = __builtin_add_overflow(t163, ((uint64_t)1ULL), &r164);
                    uint64_t t166 = r164;
                    gm_l21 = t166;
                }
            }
lb148:;
        }
    }
    {
        void * t167 = gm_l0;
        gm_b16_8 t168 = gm_l1;
        gm_tftb1odc739t5 t169 = gm_f_extend_0cs85gs(t167, t168);
        if (((((uint8_t *)&t169))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t169) + 1, gm_srcfde9lsgge1ld, 466, 5);
        gm_b0_1 t170 = GM_LD(gm_b0_1, (((uint8_t *)&t169) + 1));
    }
    {
        void * t171 = gm_l0;
        gm_b16_8 t172 = gm_l2;
        gm_tftb1odc739t5 t173 = gm_f_extend_0cs85gs(t171, t172);
        if (((((uint8_t *)&t173))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t173) + 1, gm_srcfde9lsgge1ld, 467, 5);
        gm_b0_1 t174 = GM_LD(gm_b0_1, (((uint8_t *)&t173) + 1));
    }
    {
        {
            gm_l23 = ((uint64_t)0ULL);
        }
        {
            uint64_t t176 = gm_l19;
            gm_l22 = t176;
        }
        {
            for (;;) {
                uint64_t t178 = gm_l23;
                uint64_t t179 = gm_l22;
                uint8_t t180 = (t178 < t179);
                if (!t180) goto lb177;
                {
                    uint64_t t183 = gm_l23;
                    {
                        void * t187 = gm_l0;
                        uint8_t t188 = gm_l4;
                        gm_tftb1odc739t5 t189 = gm_f_push_t83rg2m(t187, t188);
                        if (((((uint8_t *)&t189))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t189) + 1, gm_srcfde9lsgge1ld, 468, 25);
                        gm_b0_1 t190 = GM_LD(gm_b0_1, (((uint8_t *)&t189) + 1));
                    }
                    goto me184;
mn185:;
                    gm_trap("no match arm matched the scrutinee", gm_srcfde9lsgge1ld, 468, 5);
me184:;
                }
lc177:;
                {
                    uint64_t t192 = gm_l23;
                    uint64_t r193; bool o194 = __builtin_add_overflow(t192, ((uint64_t)1ULL), &r193);
                    uint64_t t195 = r193;
                    gm_l23 = t195;
                }
            }
lb177:;
        }
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) double gm_f_fmt_pow2_1p38akk(int32_t a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_fmt_quote_f7sotri(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1) {
    struct gm_fr_fmt_quote_f7sotri F;
    void * gm_l0; /* out */
    gm_b16_8 gm_l1; /* bytes */
    uint64_t gm_l2; /* i */
    uint8_t gm_l3; /* c */
    uint64_t gm_l4; /* n */
    GM_STACK_CHECK(&F, 667u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        gm_tftb1odc739t5 t3 = gm_f_push_t83rg2m(t2, ((uint8_t)34ULL));
        if (((((uint8_t *)&t3))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t3) + 1, gm_srcfde9lsgge1ld, 128, 5);
        gm_b0_1 t4 = GM_LD(gm_b0_1, (((uint8_t *)&t3) + 1));
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
                if (t12 >= (uintptr_t)(t13)) gm_trap_index(t12, t13, gm_srcfde9lsgge1ld, 131, 14);
                uint8_t t14 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t12 * 1));
                gm_l3 = t14;
            }
            {
                uint64_t t15 = gm_l2;
                uint64_t r16; bool o17 = __builtin_add_overflow(t15, ((uint64_t)1ULL), &r16);
                if (o17) gm_trap("integer overflow in `+`", gm_srcfde9lsgge1ld, 132, 9);
                uint64_t t18 = r16;
                gm_l2 = t18;
            }
            {
                uint8_t t20 = gm_l3;
                uint8_t t21 = ((t20 == ((uint8_t)34ULL)));
                if (!t21) goto ie19;
                {
                    void * t23 = gm_l0;
                    gm_b16_8 t24;
                    GM_ST(void *, (uint8_t *)&t24, (void *)(gm_smee1janbl2t9));
                    GM_ST(uintptr_t, (uint8_t *)&t24 + 8, (uintptr_t)(2));
                    gm_tftb1odc739t5 t25 = gm_f_extend_0cs85gs(t23, t24);
                    if (((((uint8_t *)&t25))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t25) + 1, gm_srcfde9lsgge1ld, 134, 13);
                    gm_b0_1 t26 = GM_LD(gm_b0_1, (((uint8_t *)&t25) + 1));
                }
                goto ix19;
ie19:;
                uint8_t t28 = gm_l3;
                uint8_t t29 = ((t28 == ((uint8_t)92ULL)));
                if (!t29) goto ie27;
                {
                    void * t31 = gm_l0;
                    gm_b16_8 t32;
                    GM_ST(void *, (uint8_t *)&t32, (void *)(gm_s3p8gug7bvl1p));
                    GM_ST(uintptr_t, (uint8_t *)&t32 + 8, (uintptr_t)(2));
                    gm_tftb1odc739t5 t33 = gm_f_extend_0cs85gs(t31, t32);
                    if (((((uint8_t *)&t33))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t33) + 1, gm_srcfde9lsgge1ld, 136, 13);
                    gm_b0_1 t34 = GM_LD(gm_b0_1, (((uint8_t *)&t33) + 1));
                }
                goto ix27;
ie27:;
                uint8_t t36 = gm_l3;
                uint8_t t37 = ((t36 == ((uint8_t)10ULL)));
                if (!t37) goto ie35;
                {
                    void * t39 = gm_l0;
                    gm_b16_8 t40;
                    GM_ST(void *, (uint8_t *)&t40, (void *)(gm_s6gt8b7hsisgu));
                    GM_ST(uintptr_t, (uint8_t *)&t40 + 8, (uintptr_t)(2));
                    gm_tftb1odc739t5 t41 = gm_f_extend_0cs85gs(t39, t40);
                    if (((((uint8_t *)&t41))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t41) + 1, gm_srcfde9lsgge1ld, 138, 13);
                    gm_b0_1 t42 = GM_LD(gm_b0_1, (((uint8_t *)&t41) + 1));
                }
                goto ix35;
ie35:;
                uint8_t t44 = gm_l3;
                uint8_t t45 = ((t44 == ((uint8_t)13ULL)));
                if (!t45) goto ie43;
                {
                    void * t47 = gm_l0;
                    gm_b16_8 t48;
                    GM_ST(void *, (uint8_t *)&t48, (void *)(gm_s9cb6j5ok7bdk));
                    GM_ST(uintptr_t, (uint8_t *)&t48 + 8, (uintptr_t)(2));
                    gm_tftb1odc739t5 t49 = gm_f_extend_0cs85gs(t47, t48);
                    if (((((uint8_t *)&t49))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t49) + 1, gm_srcfde9lsgge1ld, 140, 13);
                    gm_b0_1 t50 = GM_LD(gm_b0_1, (((uint8_t *)&t49) + 1));
                }
                goto ix43;
ie43:;
                uint8_t t52 = gm_l3;
                uint8_t t53 = ((t52 == ((uint8_t)9ULL)));
                if (!t53) goto ie51;
                {
                    void * t55 = gm_l0;
                    gm_b16_8 t56;
                    GM_ST(void *, (uint8_t *)&t56, (void *)(gm_s22g482qk7a40));
                    GM_ST(uintptr_t, (uint8_t *)&t56 + 8, (uintptr_t)(2));
                    gm_tftb1odc739t5 t57 = gm_f_extend_0cs85gs(t55, t56);
                    if (((((uint8_t *)&t57))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t57) + 1, gm_srcfde9lsgge1ld, 142, 13);
                    gm_b0_1 t58 = GM_LD(gm_b0_1, (((uint8_t *)&t57) + 1));
                }
                goto ix51;
ie51:;
                uint8_t t60 = gm_l3;
                uint8_t t61 = ((t60 == ((uint8_t)0ULL)));
                if (!t61) goto ie59;
                {
                    void * t63 = gm_l0;
                    gm_b16_8 t64;
                    GM_ST(void *, (uint8_t *)&t64, (void *)(gm_s2v5amvfkfoll));
                    GM_ST(uintptr_t, (uint8_t *)&t64 + 8, (uintptr_t)(2));
                    gm_tftb1odc739t5 t65 = gm_f_extend_0cs85gs(t63, t64);
                    if (((((uint8_t *)&t65))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t65) + 1, gm_srcfde9lsgge1ld, 144, 13);
                    gm_b0_1 t66 = GM_LD(gm_b0_1, (((uint8_t *)&t65) + 1));
                }
                goto ix59;
ie59:;
                uint8_t t68 = gm_l3;
                uint8_t t69 = (t68 >= ((uint8_t)32ULL));
                if (!t69) goto ie67;
                uint8_t t70 = gm_l3;
                uint8_t t71 = (t70 <= ((uint8_t)126ULL));
                if (!t71) goto ie67;
                {
                    void * t73 = gm_l0;
                    uint8_t t74 = gm_l3;
                    gm_tftb1odc739t5 t75 = gm_f_push_t83rg2m(t73, t74);
                    if (((((uint8_t *)&t75))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t75) + 1, gm_srcfde9lsgge1ld, 146, 13);
                    gm_b0_1 t76 = GM_LD(gm_b0_1, (((uint8_t *)&t75) + 1));
                }
                goto ix67;
ie67:;
                {
                    uint64_t t78;
                    uint8_t t80 = gm_l3;
                    uint8_t t81 = (t80 >= ((uint8_t)194ULL));
                    if (!t81) goto ie79;
                    uint8_t t82 = gm_l3;
                    uint8_t t83 = (t82 <= ((uint8_t)223ULL));
                    if (!t83) goto ie79;
                    uint64_t t85 = ((uint64_t)2ULL);
                    t78 = t85;
                    goto ix79;
ie79:;
                    uint64_t t86;
                    uint8_t t88 = gm_l3;
                    uint8_t t89 = (t88 >= ((uint8_t)224ULL));
                    if (!t89) goto ie87;
                    uint8_t t90 = gm_l3;
                    uint8_t t91 = (t90 <= ((uint8_t)239ULL));
                    if (!t91) goto ie87;
                    uint64_t t93 = ((uint64_t)3ULL);
                    t86 = t93;
                    goto ix87;
ie87:;
                    uint64_t t94;
                    uint8_t t96 = gm_l3;
                    uint8_t t97 = (t96 >= ((uint8_t)240ULL));
                    if (!t97) goto ie95;
                    uint8_t t98 = gm_l3;
                    uint8_t t99 = (t98 <= ((uint8_t)244ULL));
                    if (!t99) goto ie95;
                    uint64_t t101 = ((uint64_t)4ULL);
                    t94 = t101;
                    goto ix95;
ie95:;
                    uint64_t t103 = ((uint64_t)0ULL);
                    t94 = t103;
ix95:;
                    t86 = t94;
ix87:;
                    t78 = t86;
ix79:;
                    gm_l4 = t78;
                }
                {
                    uint64_t t105 = gm_l4;
                    uint8_t t106 = (t105 > ((uint64_t)0ULL));
                    if (!t106) goto ie104;
                    uint64_t t107 = gm_l2;
                    uint64_t r108; bool o109 = __builtin_sub_overflow(t107, ((uint64_t)1ULL), &r108);
                    if (o109) gm_trap("integer overflow in `-`", gm_srcfde9lsgge1ld, 149, 26);
                    uint64_t t110 = r108;
                    uint64_t t111 = gm_l4;
                    uint64_t r112; bool o113 = __builtin_add_overflow(t110, t111, &r112);
                    if (o113) gm_trap("integer overflow in `+`", gm_srcfde9lsgge1ld, 149, 26);
                    uint64_t t114 = r112;
                    uint64_t t115 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                    uint8_t t116 = (t114 <= t115);
                    if (!t116) goto ie104;
                    uint64_t t117 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                    void * t118 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)));
                    uint64_t t119 = gm_l2;
                    uint64_t r120; bool o121 = __builtin_sub_overflow(t119, ((uint64_t)1ULL), &r120);
                    if (o121) gm_trap("integer overflow in `-`", gm_srcfde9lsgge1ld, 149, 74);
                    uint64_t t122 = r120;
                    uint64_t t123 = (uintptr_t)t122;
                    uint64_t t124 = gm_l2;
                    uint64_t r125; bool o126 = __builtin_sub_overflow(t124, ((uint64_t)1ULL), &r125);
                    if (o126) gm_trap("integer overflow in `-`", gm_srcfde9lsgge1ld, 149, 81);
                    uint64_t t127 = r125;
                    uint64_t t128 = gm_l4;
                    uint64_t r129; bool o130 = __builtin_add_overflow(t127, t128, &r129);
                    if (o130) gm_trap("integer overflow in `+`", gm_srcfde9lsgge1ld, 149, 81);
                    uint64_t t131 = r129;
                    uint64_t t132 = (uintptr_t)t131;
                    if (t123 > t132 || t132 > (uintptr_t)t117) gm_trap_slice(t123, t132, t117, gm_srcfde9lsgge1ld, 149, 68);
                    gm_b16_8 t133;
                    GM_ST(void *, (uint8_t *)&t133, (void *)(((uint8_t *)t118 + t123 * 1)));
                    GM_ST(uintptr_t, (uint8_t *)&t133 + 8, (uintptr_t)(t132 - t123));
                    uint8_t t134 = gm_f_fmt_valid_utf8_9mug7al(t133);
                    if (!t134) goto ie104;
                    {
                        void * t136 = gm_l0;
                        uint64_t t137 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                        void * t138 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)));
                        uint64_t t139 = gm_l2;
                        uint64_t r140; bool o141 = __builtin_sub_overflow(t139, ((uint64_t)1ULL), &r140);
                        if (o141) gm_trap("integer overflow in `-`", gm_srcfde9lsgge1ld, 150, 34);
                        uint64_t t142 = r140;
                        uint64_t t143 = (uintptr_t)t142;
                        uint64_t t144 = gm_l2;
                        uint64_t r145; bool o146 = __builtin_sub_overflow(t144, ((uint64_t)1ULL), &r145);
                        if (o146) gm_trap("integer overflow in `-`", gm_srcfde9lsgge1ld, 150, 41);
                        uint64_t t147 = r145;
                        uint64_t t148 = gm_l4;
                        uint64_t r149; bool o150 = __builtin_add_overflow(t147, t148, &r149);
                        if (o150) gm_trap("integer overflow in `+`", gm_srcfde9lsgge1ld, 150, 41);
                        uint64_t t151 = r149;
                        uint64_t t152 = (uintptr_t)t151;
                        if (t143 > t152 || t152 > (uintptr_t)t137) gm_trap_slice(t143, t152, t137, gm_srcfde9lsgge1ld, 150, 28);
                        gm_b16_8 t153;
                        GM_ST(void *, (uint8_t *)&t153, (void *)(((uint8_t *)t138 + t143 * 1)));
                        GM_ST(uintptr_t, (uint8_t *)&t153 + 8, (uintptr_t)(t152 - t143));
                        gm_tftb1odc739t5 t154 = gm_f_extend_0cs85gs(t136, t153);
                        if (((((uint8_t *)&t154))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t154) + 1, gm_srcfde9lsgge1ld, 150, 17);
                        gm_b0_1 t155 = GM_LD(gm_b0_1, (((uint8_t *)&t154) + 1));
                    }
                    {
                        uint64_t t156 = gm_l2;
                        uint64_t t157 = gm_l4;
                        uint64_t r158; bool o159 = __builtin_sub_overflow(t157, ((uint64_t)1ULL), &r158);
                        if (o159) gm_trap("integer overflow in `-`", gm_srcfde9lsgge1ld, 151, 22);
                        uint64_t t160 = r158;
                        uint64_t r161; bool o162 = __builtin_add_overflow(t156, t160, &r161);
                        if (o162) gm_trap("integer overflow in `+`", gm_srcfde9lsgge1ld, 151, 17);
                        uint64_t t163 = r161;
                        gm_l2 = t163;
                    }
                    goto ix104;
ie104:;
                    {
                        void * t165 = gm_l0;
                        uint8_t t166 = gm_l3;
                        gm_t9qkh0rsooc8d t167;
                        GM_ST(uint8_t, ((uint8_t *)&t167 + 0), t166);
                        gm_tftb1odc739t5 t168 = gm_f_write_fsqql49(t165, t167);
                        if (((((uint8_t *)&t168))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t168) + 1, gm_srcfde9lsgge1ld, 153, 17);
                        gm_b0_1 t169 = GM_LD(gm_b0_1, (((uint8_t *)&t168) + 1));
                    }
ix104:;
                }
ix67:;
ix59:;
ix51:;
ix43:;
ix35:;
ix27:;
ix19:;
            }
lc5:;
        }
lb5:;
    }
    {
        void * t170 = gm_l0;
        gm_tftb1odc739t5 t171 = gm_f_push_t83rg2m(t170, ((uint8_t)34ULL));
        if (((((uint8_t *)&t171))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t171) + 1, gm_srcfde9lsgge1ld, 157, 5);
        gm_b0_1 t172 = GM_LD(gm_b0_1, (((uint8_t *)&t171) + 1));
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_fmt_seq_c3i8abi(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, uint64_t a3, gm_b16_8 a4, uint64_t a5) {
    struct gm_fr_fmt_seq_c3i8abi F;
    void * gm_l0; /* m */
    void * gm_l1; /* out */
    gm_t88rcucg1b7uk gm_l2; /* elem */
    uint64_t gm_l3; /* esz */
    gm_b16_8 gm_l4; /* data */
    uint64_t gm_l5; /* n */
    uint64_t gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    uint64_t gm_l8; /* i */
    GM_STACK_CHECK(&F, 376u);
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
        {
            void * t3 = gm_l1;
            gm_tftb1odc739t5 t4 = gm_f_push_t83rg2m(t3, ((uint8_t)91ULL));
            if (((((uint8_t *)&t4))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t4) + 1, gm_srcfde9lsgge1ld, 118, 5);
            gm_b0_1 t5 = GM_LD(gm_b0_1, (((uint8_t *)&t4) + 1));
        }
        {
            {
                gm_l7 = ((uint64_t)0ULL);
            }
            {
                uint64_t t7 = gm_l5;
                gm_l6 = t7;
            }
            {
                for (;;) {
                    uint64_t t9 = gm_l7;
                    uint64_t t10 = gm_l6;
                    uint8_t t11 = (t9 < t10);
                    if (!t11) goto lb8;
                    {
                        uint64_t t14 = gm_l7;
                        uint64_t t17 = GM_LD(uint64_t, ((uint8_t *)&t14));
                        gm_l8 = t17;
                        {
                            uint64_t t20 = gm_l8;
                            uint8_t t21 = (t20 > ((uint64_t)0ULL));
                            if (!t21) goto ie19;
                            {
                                void * t23 = gm_l1;
                                gm_b16_8 t24;
                                GM_ST(void *, (uint8_t *)&t24, (void *)(gm_sg5gdnrith2i6));
                                GM_ST(uintptr_t, (uint8_t *)&t24 + 8, (uintptr_t)(2));
                                gm_tftb1odc739t5 t25 = gm_f_extend_0cs85gs(t23, t24);
                                if (((((uint8_t *)&t25))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t25) + 1, gm_srcfde9lsgge1ld, 120, 20);
                                gm_b0_1 t26 = GM_LD(gm_b0_1, (((uint8_t *)&t25) + 1));
                            }
                            goto ix19;
ie19:;
ix19:;
                        }
                        {
                            void * t27 = gm_l0;
                            void * t28 = gm_l1;
                            gm_t88rcucg1b7uk t29 = gm_l2;
                            uint64_t t30 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                            void * t31 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)));
                            uint64_t t32 = gm_l8;
                            uint64_t t33 = gm_l3;
                            uint64_t r34; bool o35 = __builtin_mul_overflow(t32, t33, &r34);
                            if (o35) gm_trap("integer overflow in `*`", gm_srcfde9lsgge1ld, 121, 45);
                            uint64_t t36 = r34;
                            uint64_t t37 = (uintptr_t)t36;
                            uint64_t t38 = gm_l8;
                            uint64_t r39; bool o40 = __builtin_add_overflow(t38, ((uint64_t)1ULL), &r39);
                            if (o40) gm_trap("integer overflow in `+`", gm_srcfde9lsgge1ld, 121, 55);
                            uint64_t t41 = r39;
                            uint64_t t42 = gm_l3;
                            uint64_t r43; bool o44 = __builtin_mul_overflow(t41, t42, &r43);
                            if (o44) gm_trap("integer overflow in `*`", gm_srcfde9lsgge1ld, 121, 54);
                            uint64_t t45 = r43;
                            uint64_t t46 = (uintptr_t)t45;
                            if (t37 > t46 || t46 > (uintptr_t)t30) gm_trap_slice(t37, t46, t30, gm_srcfde9lsgge1ld, 121, 40);
                            gm_b16_8 t47;
                            GM_ST(void *, (uint8_t *)&t47, (void *)(((uint8_t *)t31 + t37 * 1)));
                            GM_ST(uintptr_t, (uint8_t *)&t47 + 8, (uintptr_t)(t46 - t37));
                            gm_tl572d7579hns t48;
                            gm_f_format_value_vmle7qr((void *)&t48, (void *)NULL, cx, NULL, t27, t28, t29, t47, ((uint8_t)0));
                            if (gm_unw.kind) {
                                goto gm_ll12;
                            }
                            if (((((uint8_t *)&t48))[0] != 0)) {
                                gm_b16_8 t49 = GM_LD(gm_b16_8, (((uint8_t *)&t48) + 8));
                                gm_tl572d7579hns t50;
                                GM_ST(uint8_t, (uint8_t *)&t50 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t50 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t50 + 4, 0);
                                ((uint8_t *)&t50)[0] = 1;
                                GM_ST(gm_b16_8, ((uint8_t *)&t50 + 8), t49);
                                F.ret = t50;
                                goto gm_fret;
                            }
                            gm_b0_1 t51 = GM_LD(gm_b0_1, (((uint8_t *)&t48) + 8));
                        }
                        goto me15;
mn16:;
                        gm_trap("no match arm matched the scrutinee", gm_srcfde9lsgge1ld, 119, 5);
me15:;
                    }
lc8:;
                    {
                        uint64_t t53 = gm_l7;
                        uint64_t r54; bool o55 = __builtin_add_overflow(t53, ((uint64_t)1ULL), &r54);
                        uint64_t t56 = r54;
                        gm_l7 = t56;
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
            void * t57 = gm_l1;
            gm_tftb1odc739t5 t58 = gm_f_push_t83rg2m(t57, ((uint8_t)93ULL));
            if (((((uint8_t *)&t58))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t58) + 1, gm_srcfde9lsgge1ld, 123, 5);
            gm_b0_1 t59 = GM_LD(gm_b0_1, (((uint8_t *)&t58) + 1));
        }
    }
    gm_b0_1 t60;
    GM_ST(uint8_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 8, 0);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 16, 0);
    ((uint8_t *)&(*(gm_tl572d7579hns *)gm_out))[0] = 0;
    GM_ST(gm_b0_1, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 8, t60);
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
__attribute__((aligned(4))) gm_t43v1gep754th gm_f_fmt_shortest16_cln2heq(void *env, gm_node *cx, gm_node *inst, double a0, gm_b16_8 a1) {
    struct gm_fr_fmt_shortest16_cln2heq F;
    double gm_l0; /* x */
    gm_b16_8 gm_l1; /* dig */
    uint64_t gm_l2; /* h */
    int32_t gm_l3; /* be */
    uint8_t gm_l4; /* low */
    int32_t gm_l5; /* q */
    double gm_l6; /* c */
    uint32_t gm_l7; /* cu */
    double gm_l8; /* ulp */
    double gm_l9; /* hi */
    double gm_l10; /* lo */
    uint8_t gm_l11; /* incl */
    gm_t7mlrlibdq1ja gm_l12; /* buf */
    uint64_t gm_l13; /* <tmp> */
    uint64_t gm_l14; /* <tmp> */
    uint8_t gm_l15; /* <tmp> */
    uint64_t gm_l16; /* digits */
    gm_b16_8 gm_l17; /* text */
    gm_t43v1gep754th gm_l18; /* sp */
    int64_t gm_l19; /* d */
    uint64_t gm_l20; /* <tmp> */
    uint64_t gm_l21; /* <tmp> */
    uint64_t gm_l22; /* k */
    int32_t gm_l23; /* e10 */
    gm_t7vno1ieejig6 gm_l24; /* <tmp> */
    gm_b16_8 gm_l25; /* <tmp> */
    uint64_t gm_l26; /* <tmp> */
    int64_t gm_l27; /* delta */
    int64_t gm_l28; /* cand */
    gm_b16_8 gm_l29; /* s */
    gm_ttiiv5vhiuvvr gm_l30; /* e */
    double gm_l31; /* v */
    uint8_t gm_l32; /* ok */
    gm_b16_8 gm_l33; /* nb */
    uint64_t gm_l34; /* <tmp> */
    uint64_t gm_l35; /* <tmp> */
    uint64_t gm_l36; /* k */
    GM_STACK_CHECK(&F, 1656u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        double t2 = gm_l0;
        uint64_t t3;
        memset(&t3, 0, sizeof t3); memcpy(&t3, ((uint8_t *)&t2), sizeof t3 < 8 ? sizeof t3 : 8);
        gm_l2 = t3;
    }
    {
        uint64_t t4 = gm_l2;
        unsigned __int128 n5 = (unsigned __int128)((uint64_t)52ULL);
        if (n5 >= 64) gm_trap_shift((uint64_t)n5, "u64", gm_srcfde9lsgge1ld, 253, 13);
        uint64_t t6 = (uint64_t)(t4 >> (int)n5);
        uint64_t t7 = (uint64_t)(t6 & ((uint64_t)2047ULL));
        int32_t t8 = (int32_t)t7;
        int32_t r9; bool o10 = __builtin_sub_overflow(t8, ((int32_t)1023LL), &r9);
        if (o10) gm_trap("integer overflow in `-`", gm_srcfde9lsgge1ld, 253, 11);
        int32_t t11 = r9;
        gm_l3 = t11;
    }
    {
        int32_t t12 = gm_l3;
        uint8_t t13 = (t12 < ((int32_t)-14LL));
        gm_l4 = t13;
    }
    {
        int32_t t14;
        uint8_t t16 = gm_l4;
        if (!t16) goto ie15;
        int32_t t18 = ((int32_t)-24LL);
        t14 = t18;
        goto ix15;
ie15:;
        int32_t t20 = gm_l3;
        int32_t r21; bool o22 = __builtin_sub_overflow(t20, ((int32_t)10LL), &r21);
        if (o22) gm_trap("integer overflow in `-`", gm_srcfde9lsgge1ld, 255, 36);
        int32_t t23 = r21;
        int32_t t24 = t23;
        t14 = t24;
ix15:;
        gm_l5 = t14;
    }
    {
        double t25 = gm_l0;
        int32_t t26 = gm_l5;
        double t27 = gm_f_fmt_pow2_1p38akk(t26);
        double t28 = (t25 / t27);
        gm_l6 = t28;
    }
    {
        double t29 = gm_l6;
        double t30 = (double)t29;
        double t31 = __builtin_trunc(t30);
        if (!(__builtin_isfinite(t31) && t31 >= 0e0 && t31 < 4.294967296e9)) gm_trap("float to integer conversion out of range", gm_srcfde9lsgge1ld, 257, 11);
        uint32_t t32 = (uint32_t)t31;
        gm_l7 = t32;
    }
    {
        int32_t t33 = gm_l5;
        double t34 = gm_f_fmt_pow2_1p38akk(t33);
        gm_l8 = t34;
    }
    {
        double t35 = gm_l0;
        double t36 = gm_l8;
        double t37 = (t36 / ((double)2e0));
        double t38 = (t35 + t37);
        gm_l9 = t38;
    }
    {
        double t39;
        uint32_t t41 = gm_l7;
        uint8_t t42 = ((t41 == ((uint32_t)1024ULL)));
        if (!t42) goto ie40;
        int32_t t43 = gm_l5;
        uint8_t t44 = (t43 > ((int32_t)-24LL));
        if (!t44) goto ie40;
        double t46 = gm_l0;
        double t47 = gm_l8;
        double t48 = (t47 / ((double)4e0));
        double t49 = (t46 - t48);
        double t50 = t49;
        t39 = t50;
        goto ix40;
ie40:;
        double t52 = gm_l0;
        double t53 = gm_l8;
        double t54 = (t53 / ((double)2e0));
        double t55 = (t52 - t54);
        double t56 = t55;
        t39 = t56;
ix40:;
        gm_l10 = t39;
    }
    {
        uint32_t t57 = gm_l7;
        uint32_t t58 = (uint32_t)(t57 & ((uint32_t)1ULL));
        uint8_t t59 = ((t58 == ((uint32_t)0ULL)));
        gm_l11 = t59;
    }
    {
        gm_t7mlrlibdq1ja t60;
        for (uint64_t i = 0; i < 32; i++) GM_ST(uint8_t, (uint8_t *)&t60 + i * 1, ((uint8_t)0ULL));
        gm_l12 = t60;
    }
    {
        {
            gm_l14 = ((uint64_t)1ULL);
        }
        {
            gm_l13 = ((uint64_t)17ULL);
        }
        {
            uint64_t t62 = gm_l14;
            uint64_t t63 = gm_l13;
            uint8_t t64 = (t62 <= t63);
            gm_l15 = t64;
        }
        {
            for (;;) {
                uint8_t t66 = gm_l15;
                if (!t66) goto lb65;
                {
                    uint64_t t69 = gm_l14;
                    uint64_t t72 = GM_LD(uint64_t, ((uint8_t *)&t69));
                    gm_l16 = t72;
                    {
                        double t74 = gm_l0;
                        uint64_t t75 = gm_l16;
                        uint64_t r76; bool o77 = __builtin_sub_overflow(t75, ((uint64_t)1ULL), &r76);
                        if (o77) gm_trap("integer overflow in `-`", gm_srcfde9lsgge1ld, 264, 32);
                        uint64_t t78 = r76;
                        uint32_t t79 = (uint32_t)t78;
                        void * t80 = ((uint8_t *)&gm_l12);
                        if ((uintptr_t)0 > (uintptr_t)32 || (uintptr_t)32 > (uintptr_t)32) gm_trap_slice((uintptr_t)0, (uintptr_t)32, 32, gm_srcfde9lsgge1ld, 264, 56);
                        gm_b16_8 t81;
                        GM_ST(void *, (uint8_t *)&t81, (void *)(((uint8_t *)t80 + (uintptr_t)0 * 1)));
                        GM_ST(uintptr_t, (uint8_t *)&t81 + 8, (uintptr_t)((uintptr_t)32 - (uintptr_t)0));
                        gm_b16_8 t82 = gm_f_sci_oikoiqs(t74, t79, t81);
                        gm_l17 = t82;
                    }
                    {
                        gm_b16_8 t83 = gm_l17;
                        gm_b16_8 t84 = gm_l1;
                        gm_t43v1gep754th t85 = gm_f_fmt_split_exp_0kapfov(t83, t84);
                        gm_l18 = t85;
                    }
                    {
                        gm_l19 = ((int64_t)0LL);
                    }
                    {
                        {
                            gm_l21 = ((uint64_t)0ULL);
                        }
                        {
                            uint64_t t87 = GM_LD(uint64_t, (((uint8_t *)&gm_l18) + 0));
                            gm_l20 = t87;
                        }
                        {
                            for (;;) {
                                uint64_t t89 = gm_l21;
                                uint64_t t90 = gm_l20;
                                uint8_t t91 = (t89 < t90);
                                if (!t91) goto lb88;
                                {
                                    uint64_t t94 = gm_l21;
                                    uint64_t t97 = GM_LD(uint64_t, ((uint8_t *)&t94));
                                    gm_l22 = t97;
                                    {
                                        int64_t t99 = gm_l19;
                                        int64_t r100; bool o101 = __builtin_mul_overflow(t99, ((int64_t)10LL), &r100);
                                        if (o101) gm_trap("integer overflow in `*`", gm_srcfde9lsgge1ld, 267, 32);
                                        int64_t t102 = r100;
                                        uint64_t t103 = gm_l22;
                                        uint64_t t104 = (uintptr_t)t103;
                                        uint64_t t105 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                                        if (t104 >= (uintptr_t)(t105)) gm_trap_index(t104, t105, gm_srcfde9lsgge1ld, 267, 45);
                                        uint8_t t106 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t104 * 1));
                                        uint8_t r107; bool o108 = __builtin_sub_overflow(t106, ((uint8_t)48ULL), &r107);
                                        if (o108) gm_trap("integer overflow in `-`", gm_srcfde9lsgge1ld, 267, 45);
                                        uint8_t t109 = r107;
                                        int64_t t110 = (int64_t)t109;
                                        int64_t r111; bool o112 = __builtin_add_overflow(t102, t110, &r111);
                                        if (o112) gm_trap("integer overflow in `+`", gm_srcfde9lsgge1ld, 267, 32);
                                        int64_t t113 = r111;
                                        gm_l19 = t113;
                                    }
                                    goto me95;
mn96:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcfde9lsgge1ld, 267, 9);
me95:;
                                }
lc88:;
                                {
                                    uint64_t t115 = gm_l21;
                                    uint64_t r116; bool o117 = __builtin_add_overflow(t115, ((uint64_t)1ULL), &r116);
                                    uint64_t t118 = r116;
                                    gm_l21 = t118;
                                }
                            }
lb88:;
                        }
                    }
                    {
                        int32_t t119 = GM_LD(int32_t, (((uint8_t *)&gm_l18) + 8));
                        uint64_t t120 = gm_l16;
                        uint64_t r121; bool o122 = __builtin_sub_overflow(t120, ((uint64_t)1ULL), &r121);
                        if (o122) gm_trap("integer overflow in `-`", gm_srcfde9lsgge1ld, 268, 26);
                        uint64_t t123 = r121;
                        int32_t t124 = (int32_t)t123;
                        int32_t r125; bool o126 = __builtin_sub_overflow(t119, t124, &r125);
                        if (o126) gm_trap("integer overflow in `-`", gm_srcfde9lsgge1ld, 268, 16);
                        int32_t t127 = r125;
                        gm_l23 = t127;
                    }
                    {
                        {
                            {
                                GM_ST(int64_t, (uint8_t *)&gm_l24 + 0, ((int64_t)0LL));
                                GM_ST(int64_t, (uint8_t *)&gm_l24 + 8, ((int64_t)-1LL));
                                GM_ST(int64_t, (uint8_t *)&gm_l24 + 16, ((int64_t)1LL));
                            }
                            void * t130 = ((uint8_t *)&gm_l24);
                            if ((uintptr_t)0 > (uintptr_t)3 || (uintptr_t)3 > (uintptr_t)3) gm_trap_slice((uintptr_t)0, (uintptr_t)3, 3, gm_srcfde9lsgge1ld, 271, 9);
                            gm_b16_8 t131;
                            GM_ST(void *, (uint8_t *)&t131, (void *)(((uint8_t *)t130 + (uintptr_t)0 * 8)));
                            GM_ST(uintptr_t, (uint8_t *)&t131 + 8, (uintptr_t)((uintptr_t)3 - (uintptr_t)0));
                            gm_b16_8 t132 = t131;
                            gm_l25 = t132;
                        }
                        {
                            gm_l26 = ((uint64_t)0ULL);
                        }
                        {
                            for (;;) {
                                uint64_t t134 = gm_l26;
                                uint64_t t135 = GM_LD(uintptr_t, ((uint8_t *)&gm_l25) + 8);
                                uint8_t t136 = (t134 < t135);
                                if (!t136) goto lb133;
                                {
                                    uint64_t t139 = gm_l26;
                                    uint64_t t140 = (uintptr_t)t139;
                                    uint64_t t141 = GM_LD(uintptr_t, ((uint8_t *)&gm_l25) + 8);
                                    int64_t t142 = GM_LD(int64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l25)) + t140 * 8));
                                    int64_t t145 = GM_LD(int64_t, ((uint8_t *)&t142));
                                    gm_l27 = t145;
                                    {
                                        int64_t t147 = gm_l19;
                                        int64_t t148 = gm_l27;
                                        int64_t r149; bool o150 = __builtin_add_overflow(t147, t148, &r149);
                                        if (o150) gm_trap("integer overflow in `+`", gm_srcfde9lsgge1ld, 272, 21);
                                        int64_t t151 = r149;
                                        gm_l28 = t151;
                                    }
                                    {
                                        int64_t t153 = gm_l28;
                                        uint8_t t154 = (t153 <= ((int64_t)0LL));
                                        if (!t154) goto ie152;
                                        {
                                            goto lc133;
                                        }
                                        goto ix152;
ie152:;
ix152:;
                                    }
                                    {
                                        int64_t t156 = gm_l28;
                                        int32_t t157 = gm_l23;
                                        gm_tnur0m5k1ttis t158;
                                        GM_ST(uint32_t, (uint8_t *)&t158 + 12, 0);
                                        GM_ST(int64_t, ((uint8_t *)&t158 + 0), t156);
                                        GM_ST(int32_t, ((uint8_t *)&t158 + 8), t157);
                                        gm_tabb710d9mq2e t159;
                                        gm_f_format_13oum9l((void *)&t159, (void *)NULL, cx, NULL, t158);
                                        if (gm_unw.kind) {
                                            goto gm_ll137;
                                        }
                                        if (((((uint8_t *)&t159))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t159) + 8, gm_srcfde9lsgge1ld, 274, 18);
                                        gm_b16_8 t160 = GM_LD(gm_b16_8, (((uint8_t *)&t159) + 8));
                                        gm_l29 = t160;
                                    }
                                    {
                                        gm_b16_8 t161 = gm_l29;
                                        gm_b16_8 t162 = GM_LD(gm_b16_8, ((uint8_t *)&t161));
                                        gm_tspt3vn9ljcnd t163;
                                        gm_f_parse_float_17hgksa((void *)&t163, t162);
                                        double t164;
                                        if (!((((uint8_t *)&t163))[0] != 0)) {
                                            double t165 = GM_LD(double, (((uint8_t *)&t163) + 8));
                                            t164 = t165;
                                        } else {
                                            gm_ttiiv5vhiuvvr t166 = GM_LD(gm_ttiiv5vhiuvvr, (((uint8_t *)&t163) + 8));
                                            gm_l30 = t166;
                                            double t167 = gm_l0;
                                            t164 = t167;
                                        }
                                        gm_l31 = t164;
                                    }
                                    {
                                        gm_b16_8 t168 = gm_l29;
                                        gm_b16_8 t169 = GM_LD(gm_b16_8, ((uint8_t *)&t168));
                                        gm_b0_1 t170 = gm_f_free_rkuh2rm((void *)NULL, cx, NULL, t169, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_ka73u23j5nf)));
                                        if (gm_unw.kind) {
                                            goto gm_ll137;
                                        }
                                    }
                                    {
                                        uint8_t t171;
                                        uint8_t t173 = gm_l11;
                                        if (!t173) goto ie172;
                                        uint8_t t175 = 0;
                                        double t176 = gm_l31;
                                        double t177 = gm_l10;
                                        uint8_t t178 = (t176 >= t177);
                                        if (t178) {
                                            double t179 = gm_l31;
                                            double t180 = gm_l9;
                                            uint8_t t181 = (t179 <= t180);
                                            t175 = t181;
                                        } else t175 = 0;
                                        uint8_t t182 = t175;
                                        t171 = t182;
                                        goto ix172;
ie172:;
                                        uint8_t t184 = 0;
                                        double t185 = gm_l31;
                                        double t186 = gm_l10;
                                        uint8_t t187 = (t185 > t186);
                                        if (t187) {
                                            double t188 = gm_l31;
                                            double t189 = gm_l9;
                                            uint8_t t190 = (t188 < t189);
                                            t184 = t190;
                                        } else t184 = 0;
                                        uint8_t t191 = t184;
                                        t171 = t191;
ix172:;
                                        gm_l32 = t171;
                                    }
                                    {
                                        uint8_t t193 = gm_l32;
                                        if (!t193) goto ie192;
                                        {
                                            int64_t t195 = gm_l28;
                                            void * t196 = ((uint8_t *)&gm_l12);
                                            if ((uintptr_t)0 > (uintptr_t)32 || (uintptr_t)32 > (uintptr_t)32) gm_trap_slice((uintptr_t)0, (uintptr_t)32, 32, gm_srcfde9lsgge1ld, 279, 40);
                                            gm_b16_8 t197;
                                            GM_ST(void *, (uint8_t *)&t197, (void *)(((uint8_t *)t196 + (uintptr_t)0 * 1)));
                                            GM_ST(uintptr_t, (uint8_t *)&t197 + 8, (uintptr_t)((uintptr_t)32 - (uintptr_t)0));
                                            gm_b16_8 t198 = gm_f_dec_395otau(t195, t197);
                                            gm_l33 = t198;
                                        }
                                        {
                                            {
                                                gm_l35 = ((uint64_t)0ULL);
                                            }
                                            {
                                                uint64_t t200 = GM_LD(uintptr_t, ((uint8_t *)&gm_l33) + 8);
                                                gm_l34 = t200;
                                            }
                                            {
                                                for (;;) {
                                                    uint64_t t202 = gm_l35;
                                                    uint64_t t203 = gm_l34;
                                                    uint8_t t204 = (t202 < t203);
                                                    if (!t204) goto lb201;
                                                    {
                                                        uint64_t t207 = gm_l35;
                                                        uint64_t t210 = GM_LD(uint64_t, ((uint8_t *)&t207));
                                                        gm_l36 = t210;
                                                        {
                                                            uint64_t t212 = gm_l36;
                                                            uint64_t t213 = (uintptr_t)t212;
                                                            uint64_t t214 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                                                            if (t213 >= (uintptr_t)(t214)) gm_trap_index(t213, t214, gm_srcfde9lsgge1ld, 280, 38);
                                                            uint64_t t215 = gm_l36;
                                                            uint64_t t216 = (uintptr_t)t215;
                                                            uint64_t t217 = GM_LD(uintptr_t, ((uint8_t *)&gm_l33) + 8);
                                                            if (t216 >= (uintptr_t)(t217)) gm_trap_index(t216, t217, gm_srcfde9lsgge1ld, 280, 47);
                                                            uint8_t t218 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l33)) + t216 * 1));
                                                            GM_ST(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t213 * 1), t218);
                                                        }
                                                        goto me208;
mn209:;
                                                        gm_trap("no match arm matched the scrutinee", gm_srcfde9lsgge1ld, 280, 17);
me208:;
                                                    }
lc201:;
                                                    {
                                                        uint64_t t220 = gm_l35;
                                                        uint64_t r221; bool o222 = __builtin_add_overflow(t220, ((uint64_t)1ULL), &r221);
                                                        uint64_t t223 = r221;
                                                        gm_l35 = t223;
                                                    }
                                                }
lb201:;
                                            }
                                        }
                                        {
                                            uint64_t t224 = GM_LD(uintptr_t, ((uint8_t *)&gm_l33) + 8);
                                            int32_t t225 = gm_l23;
                                            uint64_t t226 = GM_LD(uintptr_t, ((uint8_t *)&gm_l33) + 8);
                                            int32_t t227 = (int32_t)t226;
                                            int32_t r228; bool o229 = __builtin_add_overflow(t225, t227, &r228);
                                            if (o229) gm_trap("integer overflow in `+`", gm_srcfde9lsgge1ld, 281, 41);
                                            int32_t t230 = r228;
                                            int32_t r231; bool o232 = __builtin_sub_overflow(t230, ((int32_t)1LL), &r231);
                                            if (o232) gm_trap("integer overflow in `-`", gm_srcfde9lsgge1ld, 281, 41);
                                            int32_t t233 = r231;
                                            gm_t43v1gep754th t234;
                                            GM_ST(uint32_t, (uint8_t *)&t234 + 12, 0);
                                            GM_ST(uint64_t, ((uint8_t *)&t234 + 0), t224);
                                            GM_ST(int32_t, ((uint8_t *)&t234 + 8), t233);
                                            uint8_t t235 = 0;
                                            F.ret = t234;
                                            goto gm_fret;
                                        }
                                        goto ix192;
ie192:;
ix192:;
                                    }
                                    goto me143;
mn144:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcfde9lsgge1ld, 271, 9);
me143:;
                                }
lc133:;
                                {
                                    uint64_t t237 = gm_l26;
                                    uint64_t r238; bool o239 = __builtin_add_overflow(t237, ((uint64_t)1ULL), &r238);
                                    uint64_t t240 = r238;
                                    gm_l26 = t240;
                                }
                            }
gm_ll137:;
                            if (gm_unw.frame == &F.h && gm_unw.label == 2) {
                                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb133; }
                                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc133; }
                            }
                            goto gm_ll67;
lb133:;
                        }
                    }
                    goto me70;
mn71:;
                    gm_trap("no match arm matched the scrutinee", gm_srcfde9lsgge1ld, 263, 5);
me70:;
                }
lc65:;
                uint64_t t242 = gm_l14;
                uint64_t t243 = gm_l13;
                uint8_t t244 = ((t242 == t243));
                if (!t244) goto ie241;
                {
                    gm_l15 = ((uint8_t)0);
                }
                goto ix241;
ie241:;
                {
                    uint64_t t247 = gm_l14;
                    uint64_t r248; bool o249 = __builtin_add_overflow(t247, ((uint64_t)1ULL), &r248);
                    uint64_t t250 = r248;
                    gm_l14 = t250;
                }
ix241:;
            }
gm_ll67:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb65; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc65; }
            }
            goto gm_lad;
lb65:;
        }
    }
    {
        gm_t43v1gep754th t251;
        GM_ST(uint32_t, (uint8_t *)&t251 + 12, 0);
        GM_ST(uint64_t, ((uint8_t *)&t251 + 0), ((uint64_t)0ULL));
        GM_ST(int32_t, ((uint8_t *)&t251 + 8), ((int32_t)0LL));
        uint8_t t252 = 0;
        F.ret = t251;
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
inline __attribute__((aligned(4))) gm_t43v1gep754th gm_f_fmt_shortest_2l54as9(void *env, gm_node *cx, gm_node *inst, double a0, uint16_t a1, gm_b16_8 a2) {
    struct gm_fr_fmt_shortest_2l54as9 F;
    double gm_l0; /* x */
    uint16_t gm_l1; /* bits */
    gm_b16_8 gm_l2; /* dig */
    gm_b16_8 gm_l3; /* text */
    gm_t43v1gep754th gm_l4; /* r */
    GM_STACK_CHECK(&F, 410u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        uint16_t t3 = gm_l1;
        uint8_t t4 = ((t3 == ((uint16_t)16ULL)));
        if (!t4) goto ie2;
        {
            double t6 = gm_l0;
            gm_b16_8 t7 = gm_l2;
            gm_t43v1gep754th t8 = gm_f_fmt_shortest16_cln2heq((void *)NULL, cx, NULL, t6, t7);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t9 = 0;
            F.ret = t8;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        gm_b16_8 t10;
        uint16_t t12 = gm_l1;
        uint8_t t13 = ((t12 == ((uint16_t)32ULL)));
        if (!t13) goto ie11;
        double t15 = gm_l0;
        float t16 = (float)((double)t15);
        gm_tdqbns26udv5e t17;
        GM_ST(float, ((uint8_t *)&t17 + 0), t16);
        gm_tabb710d9mq2e t18;
        gm_f_format_odb8fco((void *)&t18, (void *)NULL, cx, NULL, t17);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t18))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t18) + 8, gm_srcfde9lsgge1ld, 242, 29);
        gm_b16_8 t19 = GM_LD(gm_b16_8, (((uint8_t *)&t18) + 8));
        gm_b16_8 t20 = t19;
        t10 = t20;
        goto ix11;
ie11:;
        double t22 = gm_l0;
        gm_tidu2er0d7h95 t23;
        GM_ST(double, ((uint8_t *)&t23 + 0), t22);
        gm_tabb710d9mq2e t24;
        gm_f_format_eucvbnh((void *)&t24, (void *)NULL, cx, NULL, t23);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t24))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t24) + 8, gm_srcfde9lsgge1ld, 242, 69);
        gm_b16_8 t25 = GM_LD(gm_b16_8, (((uint8_t *)&t24) + 8));
        gm_b16_8 t26 = t25;
        t10 = t26;
ix11:;
        gm_l3 = t10;
    }
    {
        gm_b16_8 t27 = gm_l3;
        gm_b16_8 t28 = GM_LD(gm_b16_8, ((uint8_t *)&t27));
        gm_b16_8 t29 = gm_l2;
        gm_t43v1gep754th t30 = gm_f_fmt_split_exp_0kapfov(t28, t29);
        gm_l4 = t30;
    }
    {
        gm_b16_8 t31 = gm_l3;
        gm_b16_8 t32 = GM_LD(gm_b16_8, ((uint8_t *)&t31));
        gm_b0_1 t33 = gm_f_free_rkuh2rm((void *)NULL, cx, NULL, t32, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_km7a24is5mf)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_t43v1gep754th t34 = gm_l4;
        uint8_t t35 = 0;
        F.ret = t34;
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
__attribute__((aligned(4))) gm_t43v1gep754th gm_f_fmt_split_exp_0kapfov(gm_b16_8 a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_fmt_split_exp_0kapfov F;
    gm_b16_8 gm_l0; /* text */
    gm_b16_8 gm_l1; /* dig */
    uint64_t gm_l2; /* n */
    uint64_t gm_l3; /* i */
    uint8_t gm_l4; /* neg */
    int32_t gm_l5; /* e */
    GM_STACK_CHECK_SHALLOW(&F, 506u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        gm_l2 = ((uint64_t)0ULL);
    }
    {
        gm_l3 = ((uint64_t)0ULL);
    }
    {
        for (;;) {
            uint64_t t3 = gm_l3;
            uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            uint8_t t5 = (t3 < t4);
            if (!t5) goto lb2;
            uint64_t t6 = gm_l3;
            uint64_t t7 = (uintptr_t)t6;
            uint64_t t8 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            if (t7 >= (uintptr_t)(t8)) gm_trap_index(t7, t8, gm_srcfde9lsgge1ld, 219, 28);
            uint8_t t9 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t7 * 1));
            uint8_t t10 = (!(t9 == ((uint8_t)101ULL)));
            if (!t10) goto lb2;
            {
                uint64_t t14 = gm_l3;
                uint64_t t15 = (uintptr_t)t14;
                uint64_t t16 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                if (t15 >= (uintptr_t)(t16)) gm_trap_index(t15, t16, gm_srcfde9lsgge1ld, 220, 12);
                uint8_t t17 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t15 * 1));
                uint8_t t18 = (!(t17 == ((uint8_t)46ULL)));
                if (!t18) goto ie13;
                {
                    uint64_t t20 = gm_l2;
                    uint64_t t21 = (uintptr_t)t20;
                    uint64_t t22 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                    if (t21 >= (uintptr_t)(t22)) gm_trap_index(t21, t22, gm_srcfde9lsgge1ld, 221, 13);
                    uint64_t t23 = gm_l3;
                    uint64_t t24 = (uintptr_t)t23;
                    uint64_t t25 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                    if (t24 >= (uintptr_t)(t25)) gm_trap_index(t24, t25, gm_srcfde9lsgge1ld, 221, 22);
                    uint8_t t26 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t24 * 1));
                    GM_ST(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t21 * 1), t26);
                }
                {
                    uint64_t t27 = gm_l2;
                    uint64_t r28; bool o29 = __builtin_add_overflow(t27, ((uint64_t)1ULL), &r28);
                    if (o29) gm_trap("integer overflow in `+`", gm_srcfde9lsgge1ld, 222, 13);
                    uint64_t t30 = r28;
                    gm_l2 = t30;
                }
                goto ix13;
ie13:;
ix13:;
            }
            {
                uint64_t t31 = gm_l3;
                uint64_t r32; bool o33 = __builtin_add_overflow(t31, ((uint64_t)1ULL), &r32);
                if (o33) gm_trap("integer overflow in `+`", gm_srcfde9lsgge1ld, 224, 9);
                uint64_t t34 = r32;
                gm_l3 = t34;
            }
lc2:;
        }
lb2:;
    }
    {
        uint64_t t35 = gm_l3;
        uint64_t r36; bool o37 = __builtin_add_overflow(t35, ((uint64_t)1ULL), &r36);
        if (o37) gm_trap("integer overflow in `+`", gm_srcfde9lsgge1ld, 226, 5);
        uint64_t t38 = r36;
        gm_l3 = t38;
    }
    {
        uint8_t t39 = 0;
        uint64_t t40 = gm_l3;
        uint64_t t41 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        uint8_t t42 = (t40 < t41);
        if (t42) {
            uint64_t t43 = gm_l3;
            uint64_t t44 = (uintptr_t)t43;
            uint64_t t45 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            if (t44 >= (uintptr_t)(t45)) gm_trap_index(t44, t45, gm_srcfde9lsgge1ld, 227, 29);
            uint8_t t46 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t44 * 1));
            uint8_t t47 = ((t46 == ((uint8_t)45ULL)));
            t39 = t47;
        } else t39 = 0;
        gm_l4 = t39;
    }
    {
        uint64_t t49 = gm_l3;
        uint64_t t50 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        uint8_t t51 = (t49 < t50);
        if (!t51) goto ie48;
        uint8_t t52 = 0;
        uint64_t t53 = gm_l3;
        uint64_t t54 = (uintptr_t)t53;
        uint64_t t55 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        if (t54 >= (uintptr_t)(t55)) gm_trap_index(t54, t55, gm_srcfde9lsgge1ld, 228, 26);
        uint8_t t56 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t54 * 1));
        uint8_t t57 = ((t56 == ((uint8_t)45ULL)));
        if (!t57) {
            uint64_t t58 = gm_l3;
            uint64_t t59 = (uintptr_t)t58;
            uint64_t t60 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            if (t59 >= (uintptr_t)(t60)) gm_trap_index(t59, t60, gm_srcfde9lsgge1ld, 228, 44);
            uint8_t t61 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t59 * 1));
            uint8_t t62 = ((t61 == ((uint8_t)43ULL)));
            t52 = t62;
        } else t52 = 1;
        if (!t52) goto ie48;
        {
            uint64_t t64 = gm_l3;
            uint64_t r65; bool o66 = __builtin_add_overflow(t64, ((uint64_t)1ULL), &r65);
            if (o66) gm_trap("integer overflow in `+`", gm_srcfde9lsgge1ld, 228, 62);
            uint64_t t67 = r65;
            gm_l3 = t67;
        }
        goto ix48;
ie48:;
ix48:;
    }
    {
        gm_l5 = ((int32_t)0LL);
    }
    {
        for (;;) {
            uint64_t t69 = gm_l3;
            uint64_t t70 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            uint8_t t71 = (t69 < t70);
            if (!t71) goto lb68;
            {
                int32_t t74 = gm_l5;
                int32_t r75; bool o76 = __builtin_mul_overflow(t74, ((int32_t)10LL), &r75);
                if (o76) gm_trap("integer overflow in `*`", gm_srcfde9lsgge1ld, 231, 13);
                int32_t t77 = r75;
                uint64_t t78 = gm_l3;
                uint64_t t79 = (uintptr_t)t78;
                uint64_t t80 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                if (t79 >= (uintptr_t)(t80)) gm_trap_index(t79, t80, gm_srcfde9lsgge1ld, 231, 26);
                uint8_t t81 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t79 * 1));
                uint8_t r82; bool o83 = __builtin_sub_overflow(t81, ((uint8_t)48ULL), &r82);
                if (o83) gm_trap("integer overflow in `-`", gm_srcfde9lsgge1ld, 231, 26);
                uint8_t t84 = r82;
                int32_t t85 = (int32_t)t84;
                int32_t r86; bool o87 = __builtin_add_overflow(t77, t85, &r86);
                if (o87) gm_trap("integer overflow in `+`", gm_srcfde9lsgge1ld, 231, 13);
                int32_t t88 = r86;
                gm_l5 = t88;
            }
            {
                uint64_t t89 = gm_l3;
                uint64_t r90; bool o91 = __builtin_add_overflow(t89, ((uint64_t)1ULL), &r90);
                if (o91) gm_trap("integer overflow in `+`", gm_srcfde9lsgge1ld, 232, 9);
                uint64_t t92 = r90;
                gm_l3 = t92;
            }
lc68:;
        }
lb68:;
    }
    {
        uint64_t t93 = gm_l2;
        int32_t t94;
        uint8_t t96 = gm_l4;
        if (!t96) goto ie95;
        int32_t t98 = gm_l5;
        int32_t r99; bool o100 = __builtin_sub_overflow(((int32_t)0LL), t98, &r99);
        if (o100) gm_trap("integer overflow in `-`", gm_srcfde9lsgge1ld, 234, 33);
        int32_t t101 = r99;
        int32_t t102 = t101;
        t94 = t102;
        goto ix95;
ie95:;
        int32_t t104 = gm_l5;
        int32_t t105 = t104;
        t94 = t105;
ix95:;
        gm_t43v1gep754th t106;
        GM_ST(uint32_t, (uint8_t *)&t106 + 12, 0);
        GM_ST(uint64_t, ((uint8_t *)&t106 + 0), t93);
        GM_ST(int32_t, ((uint8_t *)&t106 + 8), t94);
        uint8_t t107 = 0;
        F.ret = t106;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) uint8_t gm_f_fmt_valid_utf8_9mug7al(gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_fmt_valid_utf8_9mug7al F;
    gm_b16_8 gm_l0; /* s */
    uint64_t gm_l1; /* <tmp> */
    uint64_t gm_l2; /* <tmp> */
    uint64_t gm_l3; /* k */
    uint8_t gm_l4; /* c */
    GM_STACK_CHECK_SHALLOW(&F, 248u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        {
            gm_l2 = ((uint64_t)1ULL);
        }
        {
            uint64_t t3 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            gm_l1 = t3;
        }
        {
            for (;;) {
                uint64_t t5 = gm_l2;
                uint64_t t6 = gm_l1;
                uint8_t t7 = (t5 < t6);
                if (!t7) goto lb4;
                {
                    uint64_t t10 = gm_l2;
                    uint64_t t13 = GM_LD(uint64_t, ((uint8_t *)&t10));
                    gm_l3 = t13;
                    {
                        uint64_t t16 = gm_l3;
                        uint64_t t17 = (uintptr_t)t16;
                        uint64_t t18 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                        if (t17 >= (uintptr_t)(t18)) gm_trap_index(t17, t18, gm_srcfde9lsgge1ld, 164, 12);
                        uint8_t t19 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t17 * 1));
                        uint8_t t20 = (uint8_t)(t19 & ((uint8_t)192ULL));
                        uint8_t t21 = (!(t20 == ((uint8_t)128ULL)));
                        if (!t21) goto ie15;
                        {
                            uint8_t t23 = 0;
                            F.ret = ((uint8_t)0);
                            goto gm_fret;
                        }
                        goto ix15;
ie15:;
ix15:;
                    }
                    goto me11;
mn12:;
                    gm_trap("no match arm matched the scrutinee", gm_srcfde9lsgge1ld, 163, 5);
me11:;
                }
lc4:;
                {
                    uint64_t t25 = gm_l2;
                    uint64_t r26; bool o27 = __builtin_add_overflow(t25, ((uint64_t)1ULL), &r26);
                    uint64_t t28 = r26;
                    gm_l2 = t28;
                }
            }
lb4:;
        }
    }
    {
        uint64_t t29 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t30 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        if (t29 >= (uintptr_t)(t30)) gm_trap_index(t29, t30, gm_srcfde9lsgge1ld, 166, 10);
        uint8_t t31 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t29 * 1));
        gm_l4 = t31;
    }
    {
        uint64_t t33 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        uint8_t t34 = ((t33 == ((uint64_t)3ULL)));
        if (!t34) goto ie32;
        {
            uint8_t t37 = gm_l4;
            uint8_t t38 = ((t37 == ((uint8_t)224ULL)));
            if (!t38) goto ie36;
            {
                uint64_t t40 = (uintptr_t)((uint64_t)1ULL);
                uint64_t t41 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                if (t40 >= (uintptr_t)(t41)) gm_trap_index(t40, t41, gm_srcfde9lsgge1ld, 168, 31);
                uint8_t t42 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t40 * 1));
                uint8_t t43 = (t42 >= ((uint8_t)160ULL));
                uint8_t t44 = 0;
                F.ret = t43;
                goto gm_fret;
            }
            goto ix36;
ie36:;
ix36:;
        }
        {
            uint8_t t46 = gm_l4;
            uint8_t t47 = ((t46 == ((uint8_t)237ULL)));
            if (!t47) goto ie45;
            {
                uint64_t t49 = (uintptr_t)((uint64_t)1ULL);
                uint64_t t50 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                if (t49 >= (uintptr_t)(t50)) gm_trap_index(t49, t50, gm_srcfde9lsgge1ld, 169, 31);
                uint8_t t51 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t49 * 1));
                uint8_t t52 = (t51 < ((uint8_t)160ULL));
                uint8_t t53 = 0;
                F.ret = t52;
                goto gm_fret;
            }
            goto ix45;
ie45:;
ix45:;
        }
        goto ix32;
ie32:;
ix32:;
    }
    {
        uint64_t t55 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        uint8_t t56 = ((t55 == ((uint64_t)4ULL)));
        if (!t56) goto ie54;
        {
            uint8_t t59 = gm_l4;
            uint8_t t60 = ((t59 == ((uint8_t)240ULL)));
            if (!t60) goto ie58;
            {
                uint64_t t62 = (uintptr_t)((uint64_t)1ULL);
                uint64_t t63 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                if (t62 >= (uintptr_t)(t63)) gm_trap_index(t62, t63, gm_srcfde9lsgge1ld, 172, 31);
                uint8_t t64 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t62 * 1));
                uint8_t t65 = (t64 >= ((uint8_t)144ULL));
                uint8_t t66 = 0;
                F.ret = t65;
                goto gm_fret;
            }
            goto ix58;
ie58:;
ix58:;
        }
        {
            uint8_t t68 = gm_l4;
            uint8_t t69 = ((t68 == ((uint8_t)244ULL)));
            if (!t69) goto ie67;
            {
                uint64_t t71 = (uintptr_t)((uint64_t)1ULL);
                uint64_t t72 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                if (t71 >= (uintptr_t)(t72)) gm_trap_index(t71, t72, gm_srcfde9lsgge1ld, 173, 31);
                uint8_t t73 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t71 * 1));
                uint8_t t74 = (t73 < ((uint8_t)144ULL));
                uint8_t t75 = 0;
                F.ret = t74;
                goto gm_fret;
            }
            goto ix67;
ie67:;
ix67:;
        }
        goto ix54;
ie54:;
ix54:;
    }
    {
        uint8_t t76 = 0;
        F.ret = ((uint8_t)1);
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) void gm_f_format_spec_7aph60t(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3, void * a4) {
    struct gm_fr_format_spec_7aph60t F;
    void * gm_l0; /* m */
    void * gm_l1; /* out */
    gm_t88rcucg1b7uk gm_l2; /* t */
    gm_b16_8 gm_l3; /* b */
    void * gm_l4; /* spec */
    uint8_t gm_l6; /* neg */
    uint8_t gm_l7; /* finite */
    uint8_t gm_l8; /* number */
    unsigned __int128 gm_l9; /* v */
    gm_ttovhfee8mf28 gm_l10; /* info */
    unsigned __int128 gm_l11; /* mag */
    gm_b16_8 gm_l13; /* s */
    uint16_t gm_l14; /* bits */
    double gm_l15; /* x */
    double gm_l16; /* a */
    GM_STACK_CHECK(&F, 1709u);
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
            gm_f_list_uvfu1k1((void *)&F.l5, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
        }
        {
            gm_l6 = ((uint8_t)0);
        }
        {
            gm_l7 = ((uint8_t)1);
        }
        {
            gm_l8 = ((uint8_t)1);
        }
        {
            void * t4 = gm_l0;
            gm_t88rcucg1b7uk t5 = gm_l2;
            gm_t53at80pfdus3 t6;
            gm_f_kind_htv6093((void *)&t6, t4, GM_CONV(gm_csguankko4lmhd, t5));
            uint8_t t12 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
            switch (t12) { case ((uint8_t)0ULL): goto ma8; case ((uint8_t)1ULL): goto ma8; case ((uint8_t)2ULL): goto ma8; case ((uint8_t)3ULL): goto ma9; case ((uint8_t)4ULL): goto ma10; case ((uint8_t)5ULL): goto ma10; case ((uint8_t)6ULL): goto ma10; case ((uint8_t)7ULL): goto ma10; case ((uint8_t)8ULL): goto ma10; case ((uint8_t)9ULL): goto ma10; case ((uint8_t)10ULL): goto ma10; case ((uint8_t)11ULL): goto ma10; case ((uint8_t)12ULL): goto ma10; case ((uint8_t)13ULL): goto ma10; case ((uint8_t)14ULL): goto ma10; case ((uint8_t)15ULL): goto ma10; case ((uint8_t)16ULL): goto ma10; case ((uint8_t)17ULL): goto ma10; case ((uint8_t)18ULL): goto ma10; case ((uint8_t)19ULL): goto ma10; case ((uint8_t)20ULL): goto ma10; case ((uint8_t)21ULL): goto ma10; case ((uint8_t)22ULL): goto ma10; case ((uint8_t)23ULL): goto ma10; case ((uint8_t)24ULL): goto ma10; case ((uint8_t)25ULL): goto ma10; case ((uint8_t)26ULL): goto ma10; default: goto ma10; }
ma8:;
            uint8_t t16 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
            if (t16 != ((uint8_t)0ULL)) goto pa15;
            goto po14;
pa15:;
            uint8_t t18 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
            if (t18 != ((uint8_t)1ULL)) goto pa17;
            goto po14;
pa17:;
            uint8_t t20 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
            if (t20 != ((uint8_t)2ULL)) goto pa19;
            goto po14;
pa19:;
            goto mn13;
po14:;
            {
                void * t22 = gm_l0;
                gm_t88rcucg1b7uk t23 = gm_l2;
                gm_b16_8 t24 = gm_l3;
                gm_b16_8 t25 = GM_LD(gm_b16_8, ((uint8_t *)&t24));
                unsigned __int128 t26 = gm_f_get_int_pg7p4gc(t22, GM_CONV(gm_csguankko4lmhd, t23), t25);
                gm_l9 = t26;
            }
            {
                void * t27 = gm_l0;
                gm_t88rcucg1b7uk t28 = gm_l2;
                gm_tg5lql6itufk7 t29 = gm_f_int_info_ulcevdv(t27, GM_CONV(gm_csguankko4lmhd, t28));
                gm_ttovhfee8mf28 t30;
                if (((((uint8_t *)&t29))[8] != 0)) {
                    gm_ttovhfee8mf28 t31 = GM_LD(gm_ttovhfee8mf28, (((uint8_t *)&t29) + 0));
                    t30 = t31;
                } else {
                    gm_b0_1 t32;
                    GM_ST(uint8_t, (uint8_t *)&F.ret + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&F.ret + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&F.ret + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&F.ret + 8, 0);
                    GM_ST(uint64_t, (uint8_t *)&F.ret + 16, 0);
                    ((uint8_t *)&F.ret)[0] = 0;
                    GM_ST(gm_b0_1, (uint8_t *)&F.ret + 8, t32);
                    uint8_t t33 = ((((uint8_t *)&F.ret))[0] != 0);
                    gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = t33;
                    goto gm_ld2_1;
                }
                gm_l10 = t30;
            }
            {
                uint8_t t34 = 0;
                uint8_t t35 = GM_LD(uint8_t, (((uint8_t *)&gm_l10) + 0));
                if (t35) {
                    unsigned __int128 t36 = gm_l9;
                    __int128 t37;
                    memset(&t37, 0, sizeof t37); memcpy(&t37, ((uint8_t *)&t36), sizeof t37 < 16 ? sizeof t37 : 16);
                    uint8_t t38 = (t37 < ((__int128)(((unsigned __int128)0u << 64) | 0u)));
                    t34 = t38;
                } else t34 = 0;
                gm_l6 = t34;
            }
            {
                unsigned __int128 t39;
                uint8_t t41 = gm_l6;
                if (!t41) goto ie40;
                unsigned __int128 t43 = gm_l9;
                unsigned __int128 r44; bool o45 = __builtin_sub_overflow(((unsigned __int128)(((unsigned __int128)0u << 64) | 0u)), t43, &r44);
                unsigned __int128 t46 = r44;
                unsigned __int128 t47 = t46;
                t39 = t47;
                goto ix40;
ie40:;
                unsigned __int128 t49 = gm_l9;
                unsigned __int128 t50 = t49;
                t39 = t50;
ix40:;
                gm_l11 = t39;
            }
            {
                gm_tfbi275tn5ccf t51;
                for (uint64_t i = 0; i < 131; i++) GM_ST(uint8_t, (uint8_t *)&t51 + i * 1, ((uint8_t)0ULL));
                F.l12 = t51;
            }
            {
                void * t52 = gm_l4;
                gm_b16_8 t53;
                uint8_t t61 = GM_LD(uint8_t, ((((uint8_t *)t52) + 20) + 0));
                switch (t61) { case ((uint8_t)0ULL): goto ma59; case ((uint8_t)1ULL): goto ma55; case ((uint8_t)2ULL): goto ma56; case ((uint8_t)3ULL): goto ma57; case ((uint8_t)4ULL): goto ma58; case ((uint8_t)5ULL): goto ma59; case ((uint8_t)6ULL): goto ma59; default: goto ma59; }
ma55:;
                uint8_t t63 = GM_LD(uint8_t, ((((uint8_t *)t52) + 20) + 0));
                if (t63 != ((uint8_t)1ULL)) goto mn62;
                unsigned __int128 t64 = gm_l11;
                void * t65 = ((uint8_t *)&F.l12);
                if ((uintptr_t)0 > (uintptr_t)131 || (uintptr_t)131 > (uintptr_t)131) gm_trap_slice((uintptr_t)0, (uintptr_t)131, 131, gm_srcfde9lsgge1ld, 354, 66);
                gm_b16_8 t66;
                GM_ST(void *, (uint8_t *)&t66, (void *)(((uint8_t *)t65 + (uintptr_t)0 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t66 + 8, (uintptr_t)((uintptr_t)131 - (uintptr_t)0));
                gm_b16_8 t67 = gm_f_radix_with_qaamfpk(t64, ((uint32_t)16ULL), ((uint8_t)0), ((uint8_t)0), t66);
                t53 = t67;
                goto me54;
mn62:;
ma56:;
                uint8_t t69 = GM_LD(uint8_t, ((((uint8_t *)t52) + 20) + 0));
                if (t69 != ((uint8_t)2ULL)) goto mn68;
                unsigned __int128 t70 = gm_l11;
                void * t71 = ((uint8_t *)&F.l12);
                if ((uintptr_t)0 > (uintptr_t)131 || (uintptr_t)131 > (uintptr_t)131) gm_trap_slice((uintptr_t)0, (uintptr_t)131, 131, gm_srcfde9lsgge1ld, 355, 70);
                gm_b16_8 t72;
                GM_ST(void *, (uint8_t *)&t72, (void *)(((uint8_t *)t71 + (uintptr_t)0 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t72 + 8, (uintptr_t)((uintptr_t)131 - (uintptr_t)0));
                gm_b16_8 t73 = gm_f_radix_with_qaamfpk(t70, ((uint32_t)16ULL), ((uint8_t)1), ((uint8_t)0), t72);
                t53 = t73;
                goto me54;
mn68:;
ma57:;
                uint8_t t75 = GM_LD(uint8_t, ((((uint8_t *)t52) + 20) + 0));
                if (t75 != ((uint8_t)3ULL)) goto mn74;
                unsigned __int128 t76 = gm_l11;
                void * t77 = ((uint8_t *)&F.l12);
                if ((uintptr_t)0 > (uintptr_t)131 || (uintptr_t)131 > (uintptr_t)131) gm_trap_slice((uintptr_t)0, (uintptr_t)131, 131, gm_srcfde9lsgge1ld, 356, 65);
                gm_b16_8 t78;
                GM_ST(void *, (uint8_t *)&t78, (void *)(((uint8_t *)t77 + (uintptr_t)0 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t78 + 8, (uintptr_t)((uintptr_t)131 - (uintptr_t)0));
                gm_b16_8 t79 = gm_f_radix_with_qaamfpk(t76, ((uint32_t)8ULL), ((uint8_t)0), ((uint8_t)0), t78);
                t53 = t79;
                goto me54;
mn74:;
ma58:;
                uint8_t t81 = GM_LD(uint8_t, ((((uint8_t *)t52) + 20) + 0));
                if (t81 != ((uint8_t)4ULL)) goto mn80;
                unsigned __int128 t82 = gm_l11;
                void * t83 = ((uint8_t *)&F.l12);
                if ((uintptr_t)0 > (uintptr_t)131 || (uintptr_t)131 > (uintptr_t)131) gm_trap_slice((uintptr_t)0, (uintptr_t)131, 131, gm_srcfde9lsgge1ld, 357, 65);
                gm_b16_8 t84;
                GM_ST(void *, (uint8_t *)&t84, (void *)(((uint8_t *)t83 + (uintptr_t)0 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t84 + 8, (uintptr_t)((uintptr_t)131 - (uintptr_t)0));
                gm_b16_8 t85 = gm_f_radix_with_qaamfpk(t82, ((uint32_t)2ULL), ((uint8_t)0), ((uint8_t)0), t84);
                t53 = t85;
                goto me54;
mn80:;
ma59:;
                unsigned __int128 t87 = gm_l11;
                void * t88 = ((uint8_t *)&F.l12);
                if ((uintptr_t)0 > (uintptr_t)131 || (uintptr_t)131 > (uintptr_t)131) gm_trap_slice((uintptr_t)0, (uintptr_t)131, 131, gm_srcfde9lsgge1ld, 358, 63);
                gm_b16_8 t89;
                GM_ST(void *, (uint8_t *)&t89, (void *)(((uint8_t *)t88 + (uintptr_t)0 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t89 + 8, (uintptr_t)((uintptr_t)131 - (uintptr_t)0));
                gm_b16_8 t90 = gm_f_radix_with_qaamfpk(t87, ((uint32_t)10ULL), ((uint8_t)0), ((uint8_t)0), t89);
                t53 = t90;
                goto me54;
mn86:;
ma60:;
                gm_trap("no match arm matched the scrutinee", gm_srcfde9lsgge1ld, 353, 18);
me54:;
                gm_l13 = t53;
            }
            {
                void * t91 = (void *)((uint8_t *)&F.l5);
                gm_b16_8 t92 = gm_l13;
                gm_tftb1odc739t5 t93 = gm_f_extend_0cs85gs(t91, t92);
                if (((((uint8_t *)&t93))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t93) + 1, gm_srcfde9lsgge1ld, 360, 13);
                gm_b0_1 t94 = GM_LD(gm_b0_1, (((uint8_t *)&t93) + 1));
            }
            goto me7;
mn13:;
ma9:;
            uint8_t t96 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
            if (t96 != ((uint8_t)3ULL)) goto mn95;
            uint16_t t97 = GM_LD(uint16_t, (((uint8_t *)&t6) + 8));
            gm_l14 = t97;
            {
                void * t99 = gm_l0;
                uint16_t t100 = gm_l14;
                gm_b16_8 t101 = gm_l3;
                gm_b16_8 t102 = GM_LD(gm_b16_8, ((uint8_t *)&t101));
                double t103 = gm_f_fmt_float_bits_9d6f5r5(t99, t100, t102);
                gm_l15 = t103;
            }
            {
                double t104 = gm_l15;
                uint8_t t105 = (uint8_t)__builtin_isfinite(t104);
                gm_l7 = t105;
            }
            {
                uint8_t t106 = 0;
                double t107 = gm_l15;
                uint8_t t108 = (uint8_t)(__builtin_signbit(t107) != 0);
                if (t108) {
                    double t109 = gm_l15;
                    uint8_t t110 = (uint8_t)__builtin_isnan(t109);
                    uint8_t t111 = (uint8_t)!t110;
                    t106 = t111;
                } else t106 = 0;
                gm_l6 = t106;
            }
            {
                double t112 = gm_l15;
                double t113 = (t112 < 0 || ((t112) == 0 && __builtin_signbit(t112))) ? -t112 : t112;
                gm_l16 = t113;
            }
            {
                double t115 = gm_l15;
                uint8_t t116 = (uint8_t)__builtin_isnan(t115);
                if (!t116) goto ie114;
                {
                    void * t118 = (void *)((uint8_t *)&F.l5);
                    gm_b16_8 t119;
                    GM_ST(void *, (uint8_t *)&t119, (void *)(gm_sfa86erpod86s));
                    GM_ST(uintptr_t, (uint8_t *)&t119 + 8, (uintptr_t)(3));
                    gm_tftb1odc739t5 t120 = gm_f_extend_0cs85gs(t118, t119);
                    if (((((uint8_t *)&t120))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t120) + 1, gm_srcfde9lsgge1ld, 368, 17);
                    gm_b0_1 t121 = GM_LD(gm_b0_1, (((uint8_t *)&t120) + 1));
                }
                goto ix114;
ie114:;
                uint8_t t123 = gm_l7;
                uint8_t t124 = (uint8_t)!t123;
                if (!t124) goto ie122;
                {
                    void * t126 = (void *)((uint8_t *)&F.l5);
                    gm_b16_8 t127;
                    GM_ST(void *, (uint8_t *)&t127, (void *)(gm_sns75reqq32d2));
                    GM_ST(uintptr_t, (uint8_t *)&t127 + 8, (uintptr_t)(3));
                    gm_tftb1odc739t5 t128 = gm_f_extend_0cs85gs(t126, t127);
                    if (((((uint8_t *)&t128))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t128) + 1, gm_srcfde9lsgge1ld, 370, 17);
                    gm_b0_1 t129 = GM_LD(gm_b0_1, (((uint8_t *)&t128) + 1));
                }
                goto ix122;
ie122:;
                {
                    void * t131 = (void *)((uint8_t *)&F.l5);
                    double t132 = gm_l16;
                    uint16_t t133 = gm_l14;
                    void * t134 = gm_l4;
                    gm_b0_1 t135 = gm_f_fmt_float_spec_glt99ob((void *)NULL, cx, NULL, t131, t132, t133, t134);
                    if (gm_unw.kind) {
                        goto gm_ld2_1;
                    }
                }
ix122:;
ix114:;
            }
            goto me7;
mn95:;
ma10:;
            {
                gm_l8 = ((uint8_t)0);
            }
            {
                void * t138 = gm_l0;
                void * t139 = (void *)((uint8_t *)&F.l5);
                gm_t88rcucg1b7uk t140 = gm_l2;
                gm_b16_8 t141 = gm_l3;
                void * t142 = gm_l4;
                uint8_t t143 = (!(GM_LD(uint8_t, (((uint8_t *)t142) + 20) + 0) == ((uint8_t)6ULL)));
                gm_tl572d7579hns t144;
                gm_f_format_value_vmle7qr((void *)&t144, (void *)NULL, cx, NULL, t138, t139, t140, t141, t143);
                if (gm_unw.kind) {
                    goto gm_ld2_1;
                }
                if (((((uint8_t *)&t144))[0] != 0)) {
                    gm_b16_8 t145 = GM_LD(gm_b16_8, (((uint8_t *)&t144) + 8));
                    gm_tl572d7579hns t146;
                    GM_ST(uint8_t, (uint8_t *)&t146 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t146 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t146 + 4, 0);
                    ((uint8_t *)&t146)[0] = 1;
                    GM_ST(gm_b16_8, ((uint8_t *)&t146 + 8), t145);
                    F.ret = t146;
                    gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = 1;
                    goto gm_ld2_1;
                }
                gm_b0_1 t147 = GM_LD(gm_b0_1, (((uint8_t *)&t144) + 8));
            }
            {
                void * t148 = gm_l1;
                gm_b16_8 t149;
                GM_ST(void *, (uint8_t *)&t149, (void *)(gm_sr4qbrk6tlgaa));
                GM_ST(uintptr_t, (uint8_t *)&t149 + 8, (uintptr_t)(0));
                gm_b16_8 t150 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 0));
                gm_b16_8 t151 = GM_LD(gm_b16_8, ((uint8_t *)&t150));
                void * t152 = gm_l4;
                uint32_t t153 = GM_LD(uint32_t, (((uint8_t *)t152) + 8));
                void * t154 = gm_l4;
                uint8_t t155 = GM_LD(uint8_t, (((uint8_t *)t154) + 0));
                void * t156 = gm_l4;
                gm_t9qkh0rsooc8d t157 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)t156) + 1));
                void * t158 = gm_l4;
                uint8_t t159 = GM_LD(uint8_t, (((uint8_t *)t158) + 4));
                gm_b0_1 t160 = gm_f_fmt_pad_ev4lnnu((void *)NULL, cx, NULL, t148, t149, t151, t153, t155, t157, t159, ((uint8_t)0));
                if (gm_unw.kind) {
                    goto gm_ld2_1;
                }
            }
            {
                gm_b0_1 t161;
                GM_ST(uint8_t, (uint8_t *)&F.ret + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&F.ret + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&F.ret + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&F.ret + 8, 0);
                GM_ST(uint64_t, (uint8_t *)&F.ret + 16, 0);
                ((uint8_t *)&F.ret)[0] = 0;
                GM_ST(gm_b0_1, (uint8_t *)&F.ret + 8, t161);
                uint8_t t162 = ((((uint8_t *)&F.ret))[0] != 0);
                gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = t162;
                goto gm_ld2_1;
            }
            goto me7;
mn136:;
ma11:;
            gm_trap("no match arm matched the scrutinee", gm_srcfde9lsgge1ld, 346, 5);
me7:;
        }
        {
            gm_f_list_uvfu1k1((void *)&F.l17, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_ld2_1;
            }
        }
        {
        }
        {
            uint8_t t165 = gm_l6;
            if (!t165) goto ie164;
            {
                void * t167 = (void *)((uint8_t *)&F.l17);
                gm_tftb1odc739t5 t168 = gm_f_push_t83rg2m(t167, ((uint8_t)45ULL));
                if (((((uint8_t *)&t168))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t168) + 1, gm_srcfde9lsgge1ld, 385, 9);
                gm_b0_1 t169 = GM_LD(gm_b0_1, (((uint8_t *)&t168) + 1));
            }
            goto ix164;
ie164:;
            void * t171 = gm_l4;
            uint8_t t172 = GM_LD(uint8_t, (((uint8_t *)t171) + 2));
            if (!t172) goto ie170;
            gm_b16_8 t173 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 0));
            gm_b16_8 t174 = GM_LD(gm_b16_8, ((uint8_t *)&t173));
            uint8_t t175 = (((GM_LD(uintptr_t, ((uint8_t *)&t174) + 8) == 3) && memcmp(GM_LD(void *, ((uint8_t *)&t174)), gm_sfa86erpod86s, 3) == 0));
            uint8_t t176 = (uint8_t)!t175;
            if (!t176) goto ie170;
            {
                void * t178 = (void *)((uint8_t *)&F.l17);
                gm_tftb1odc739t5 t179 = gm_f_push_t83rg2m(t178, ((uint8_t)43ULL));
                if (((((uint8_t *)&t179))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t179) + 1, gm_srcfde9lsgge1ld, 387, 9);
                gm_b0_1 t180 = GM_LD(gm_b0_1, (((uint8_t *)&t179) + 1));
            }
            goto ix170;
ie170:;
ix170:;
ix164:;
        }
        {
            void * t182 = gm_l4;
            uint8_t t183 = GM_LD(uint8_t, (((uint8_t *)t182) + 3));
            if (!t183) goto ie181;
            {
                void * t185 = (void *)((uint8_t *)&F.l17);
                void * t186 = gm_l4;
                gm_b16_8 t187;
                uint8_t t193 = GM_LD(uint8_t, ((((uint8_t *)t186) + 20) + 0));
                switch (t193) { case ((uint8_t)0ULL): goto ma191; case ((uint8_t)1ULL): goto ma189; case ((uint8_t)2ULL): goto ma189; case ((uint8_t)3ULL): goto ma190; case ((uint8_t)4ULL): goto ma191; case ((uint8_t)5ULL): goto ma191; case ((uint8_t)6ULL): goto ma191; default: goto ma191; }
ma189:;
                uint8_t t197 = GM_LD(uint8_t, ((((uint8_t *)t186) + 20) + 0));
                if (t197 != ((uint8_t)1ULL)) goto pa196;
                goto po195;
pa196:;
                uint8_t t199 = GM_LD(uint8_t, ((((uint8_t *)t186) + 20) + 0));
                if (t199 != ((uint8_t)2ULL)) goto pa198;
                goto po195;
pa198:;
                goto mn194;
po195:;
                gm_b16_8 t200;
                GM_ST(void *, (uint8_t *)&t200, (void *)(gm_sdk5n6a42f7nl));
                GM_ST(uintptr_t, (uint8_t *)&t200 + 8, (uintptr_t)(2));
                t187 = t200;
                goto me188;
mn194:;
ma190:;
                uint8_t t202 = GM_LD(uint8_t, ((((uint8_t *)t186) + 20) + 0));
                if (t202 != ((uint8_t)3ULL)) goto mn201;
                gm_b16_8 t203;
                GM_ST(void *, (uint8_t *)&t203, (void *)(gm_so4mj8r7k5aek));
                GM_ST(uintptr_t, (uint8_t *)&t203 + 8, (uintptr_t)(2));
                t187 = t203;
                goto me188;
mn201:;
ma191:;
                gm_b16_8 t205;
                GM_ST(void *, (uint8_t *)&t205, (void *)(gm_s7cvig9qav6l5));
                GM_ST(uintptr_t, (uint8_t *)&t205 + 8, (uintptr_t)(2));
                t187 = t205;
                goto me188;
mn204:;
ma192:;
                gm_trap("no match arm matched the scrutinee", gm_srcfde9lsgge1ld, 390, 21);
me188:;
                gm_tftb1odc739t5 t206 = gm_f_extend_0cs85gs(t185, t187);
                if (((((uint8_t *)&t206))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t206) + 1, gm_srcfde9lsgge1ld, 390, 9);
                gm_b0_1 t207 = GM_LD(gm_b0_1, (((uint8_t *)&t206) + 1));
            }
            goto ix181;
ie181:;
ix181:;
        }
        {
            void * t209 = gm_l4;
            uint8_t t210 = GM_LD(uint8_t, (((uint8_t *)t209) + 4));
            if (!t210) goto ie208;
            uint8_t t211 = gm_l7;
            uint8_t t212 = (uint8_t)!t211;
            if (!t212) goto ie208;
            {
                void * t214 = gm_l1;
                gm_b16_8 t215 = GM_LD(gm_b16_8, (((uint8_t *)&F.l17) + 0));
                gm_b16_8 t216 = GM_LD(gm_b16_8, ((uint8_t *)&t215));
                gm_b16_8 t217 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 0));
                gm_b16_8 t218 = GM_LD(gm_b16_8, ((uint8_t *)&t217));
                void * t219 = gm_l4;
                uint32_t t220 = GM_LD(uint32_t, (((uint8_t *)t219) + 8));
                void * t221 = gm_l4;
                uint8_t t222 = GM_LD(uint8_t, (((uint8_t *)t221) + 0));
                gm_t9qkh0rsooc8d t223;
                GM_ST(uint8_t, ((uint8_t *)&t223 + 0), ((uint8_t)2ULL));
                uint8_t t224 = gm_l8;
                gm_b0_1 t225 = gm_f_fmt_pad_ev4lnnu((void *)NULL, cx, NULL, t214, t216, t218, t220, t222, t223, ((uint8_t)0), t224);
                if (gm_unw.kind) {
                    goto gm_ld2_2;
                }
            }
            goto ix208;
ie208:;
            {
                void * t227 = gm_l1;
                gm_b16_8 t228 = GM_LD(gm_b16_8, (((uint8_t *)&F.l17) + 0));
                gm_b16_8 t229 = GM_LD(gm_b16_8, ((uint8_t *)&t228));
                gm_b16_8 t230 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 0));
                gm_b16_8 t231 = GM_LD(gm_b16_8, ((uint8_t *)&t230));
                void * t232 = gm_l4;
                uint32_t t233 = GM_LD(uint32_t, (((uint8_t *)t232) + 8));
                void * t234 = gm_l4;
                uint8_t t235 = GM_LD(uint8_t, (((uint8_t *)t234) + 0));
                void * t236 = gm_l4;
                gm_t9qkh0rsooc8d t237 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)t236) + 1));
                void * t238 = gm_l4;
                uint8_t t239 = GM_LD(uint8_t, (((uint8_t *)t238) + 4));
                uint8_t t240 = gm_l8;
                gm_b0_1 t241 = gm_f_fmt_pad_ev4lnnu((void *)NULL, cx, NULL, t227, t229, t231, t233, t235, t237, t239, t240);
                if (gm_unw.kind) {
                    goto gm_ld2_2;
                }
            }
ix208:;
        }
        {
            void * t242 = (void *)((uint8_t *)&F.l17);
            gm_f_deinit_r70pji5(t242);
        }
        {
            void * t243 = (void *)((uint8_t *)&F.l5);
            gm_f_deinit_r70pji5(t243);
        }
        goto gm_ls244;
gm_ld2_2:;
        {
            gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
            void * t245 = (void *)((uint8_t *)&F.l17);
            gm_f_deinit_r70pji5(t245);
            gm_unw = gm_saved;
        }
gm_ld2_1:;
        {
            gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
            void * t246 = (void *)((uint8_t *)&F.l5);
            gm_f_deinit_r70pji5(t246);
            gm_unw = gm_saved;
        }
        goto gm_lad;
gm_ls244:;
    }
    gm_b0_1 t247;
    GM_ST(uint8_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 8, 0);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 16, 0);
    ((uint8_t *)&(*(gm_tl572d7579hns *)gm_out))[0] = 0;
    GM_ST(gm_b0_1, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 8, t247);
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
__attribute__((aligned(4))) void gm_f_format_value_vmle7qr(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3, uint8_t a4) {
    struct gm_fr_format_value_vmle7qr F;
    void * gm_l0; /* m */
    void * gm_l1; /* out */
    gm_t88rcucg1b7uk gm_l2; /* t */
    gm_b16_8 gm_l3; /* b */
    uint8_t gm_l4; /* top */
    uint16_t gm_l5; /* bits */
    gm_t88rcucg1b7uk gm_l6; /* elem */
    gm_t2lqu8jsj511d gm_l7; /* sp */
    uint64_t gm_l8; /* esz */
    uint64_t gm_l9; /* total */
    gm_b16_8 gm_l10; /* data */
    uint64_t gm_l12; /* len */
    gm_t88rcucg1b7uk gm_l13; /* elem */
    gm_b16_8 gm_l14; /* elems */
    gm_b16_8 gm_l15; /* names */
    gm_b16_8 gm_l16; /* <tmp> */
    uint64_t gm_l17; /* <tmp> */
    uint64_t gm_l18; /* i */
    gm_t88rcucg1b7uk gm_l19; /* et */
    gm_t88rcucg1b7uk gm_l20; /* inner */
    gm_t88rcucg1b7uk gm_l21; /* err */
    gm_t88rcucg1b7uk gm_l22; /* ok */
    gm_b16_8 gm_l23; /* payload */
    GM_STACK_CHECK(&F, 2461u);
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
            uint8_t t19 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
            switch (t19) { case ((uint8_t)0ULL): goto ma7; case ((uint8_t)1ULL): goto ma7; case ((uint8_t)2ULL): goto ma7; case ((uint8_t)3ULL): goto ma8; case ((uint8_t)4ULL): goto ma9; case ((uint8_t)5ULL): goto ma17; case ((uint8_t)6ULL): goto ma17; case ((uint8_t)7ULL): goto ma10; case ((uint8_t)8ULL): goto ma10; case ((uint8_t)9ULL): goto ma10; case ((uint8_t)10ULL): goto ma11; case ((uint8_t)11ULL): goto ma12; case ((uint8_t)12ULL): goto ma13; case ((uint8_t)13ULL): goto ma14; case ((uint8_t)14ULL): goto ma15; case ((uint8_t)15ULL): goto ma17; case ((uint8_t)16ULL): goto ma10; case ((uint8_t)17ULL): goto ma16; case ((uint8_t)18ULL): goto ma17; case ((uint8_t)19ULL): goto ma17; case ((uint8_t)20ULL): goto ma17; case ((uint8_t)21ULL): goto ma17; case ((uint8_t)22ULL): goto ma17; case ((uint8_t)23ULL): goto ma17; case ((uint8_t)24ULL): goto ma17; case ((uint8_t)25ULL): goto ma17; case ((uint8_t)26ULL): goto ma17; default: goto ma17; }
ma7:;
            uint8_t t23 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
            if (t23 != ((uint8_t)0ULL)) goto pa22;
            goto po21;
pa22:;
            uint8_t t25 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
            if (t25 != ((uint8_t)1ULL)) goto pa24;
            goto po21;
pa24:;
            uint8_t t27 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
            if (t27 != ((uint8_t)2ULL)) goto pa26;
            goto po21;
pa26:;
            goto mn20;
po21:;
            void * t28 = gm_l0;
            void * t29 = gm_l1;
            gm_t88rcucg1b7uk t30 = gm_l2;
            gm_b16_8 t31 = gm_l3;
            gm_b0_1 t32 = gm_f_fmt_int_svhqra7((void *)NULL, cx, NULL, t28, t29, t30, t31);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto me6;
mn20:;
ma8:;
            uint8_t t34 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
            if (t34 != ((uint8_t)3ULL)) goto mn33;
            uint16_t t35 = GM_LD(uint16_t, (((uint8_t *)&t5) + 8));
            gm_l5 = t35;
            void * t36 = gm_l1;
            void * t37 = gm_l0;
            uint16_t t38 = gm_l5;
            gm_b16_8 t39 = gm_l3;
            gm_b16_8 t40 = GM_LD(gm_b16_8, ((uint8_t *)&t39));
            double t41 = gm_f_fmt_float_bits_9d6f5r5(t37, t38, t40);
            uint16_t t42 = gm_l5;
            gm_b0_1 t43 = gm_f_fmt_float_1ivmudg((void *)NULL, cx, NULL, t36, t41, t42);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto me6;
mn33:;
ma9:;
            uint8_t t45 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
            if (t45 != ((uint8_t)4ULL)) goto mn44;
            void * t46 = gm_l1;
            gm_b16_8 t47;
            uint64_t t49 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t50 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
            if (t49 >= (uintptr_t)(t50)) gm_trap_index(t49, t50, gm_srcfde9lsgge1ld, 12, 32);
            uint8_t t51 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t49 * 1));
            uint8_t t52 = (!(t51 == ((uint8_t)0ULL)));
            if (!t52) goto ie48;
            gm_b16_8 t54;
            GM_ST(void *, (uint8_t *)&t54, (void *)(gm_sehko5bb96s6p));
            GM_ST(uintptr_t, (uint8_t *)&t54 + 8, (uintptr_t)(4));
            gm_b16_8 t55 = t54;
            t47 = t55;
            goto ix48;
ie48:;
            gm_b16_8 t57;
            GM_ST(void *, (uint8_t *)&t57, (void *)(gm_sogehh8t1m7tn));
            GM_ST(uintptr_t, (uint8_t *)&t57 + 8, (uintptr_t)(5));
            gm_b16_8 t58 = t57;
            t47 = t58;
ix48:;
            gm_tftb1odc739t5 t59 = gm_f_extend_0cs85gs(t46, t47);
            if (((((uint8_t *)&t59))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t59) + 1, gm_srcfde9lsgge1ld, 12, 18);
            gm_b0_1 t60 = GM_LD(gm_b0_1, (((uint8_t *)&t59) + 1));
            goto me6;
mn44:;
ma10:;
            uint8_t t64 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
            if (t64 != ((uint8_t)8ULL)) goto pa63;
            goto po62;
pa63:;
            uint8_t t66 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
            if (t66 != ((uint8_t)9ULL)) goto pa65;
            goto po62;
pa65:;
            uint8_t t68 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
            if (t68 != ((uint8_t)7ULL)) goto pa67;
            goto po62;
pa67:;
            uint8_t t70 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
            if (t70 != ((uint8_t)16ULL)) goto pa69;
            goto po62;
pa69:;
            goto mn61;
po62:;
            void * t71 = gm_l1;
            void * t72 = gm_l0;
            gm_b16_8 t73 = gm_l3;
            gm_b16_8 t74 = GM_LD(gm_b16_8, ((uint8_t *)&t73));
            uint64_t t75 = gm_f_ptr_of_b77b4c9(t72, t74);
            gm_t4ce0jd3tdro4 t76;
            GM_ST(uint64_t, ((uint8_t *)&t76 + 0), t75);
            gm_tftb1odc739t5 t77 = gm_f_write_kvqtui9(t71, t76);
            if (((((uint8_t *)&t77))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t77) + 1, gm_srcfde9lsgge1ld, 13, 59);
            gm_b0_1 t78 = GM_LD(gm_b0_1, (((uint8_t *)&t77) + 1));
            goto me6;
mn61:;
ma11:;
            uint8_t t80 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
            if (t80 != ((uint8_t)10ULL)) goto mn79;
            gm_t88rcucg1b7uk t81 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t5) + 8) + 4));
            gm_l6 = t81;
            {
                void * t83 = gm_l0;
                gm_b16_8 t84 = gm_l3;
                gm_b16_8 t85 = GM_LD(gm_b16_8, ((uint8_t *)&t84));
                gm_t2lqu8jsj511d t86 = gm_f_slice_parts_1se5hu4(t83, t85);
                gm_l7 = t86;
            }
            {
                void * t87 = gm_l0;
                gm_t88rcucg1b7uk t88 = gm_l6;
                uint64_t t89 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t87, t88);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l8 = t89;
            }
            {
                uint64_t t90 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
                uint64_t t91 = gm_l8;
                uint64_t r92; bool o93 = __builtin_mul_overflow(t90, t91, &r92);
                gm_tf5srh4mp4b3i t94;
                if (o93) {
                gm_tf5srh4mp4b3i t95;
                memset(&t95, 0, sizeof t95);
                    t94 = t95;
                } else {
                uint64_t t96 = r92;
                gm_tf5srh4mp4b3i t97;
                GM_ST(uint8_t, (uint8_t *)&t97 + 9, 0);
                GM_ST(uint16_t, (uint8_t *)&t97 + 10, 0);
                GM_ST(uint32_t, (uint8_t *)&t97 + 12, 0);
                GM_ST(uint64_t, ((uint8_t *)&t97), t96);
                ((uint8_t *)&t97)[8] = 1;
                    t94 = t97;
                }
                uint64_t t98;
                if (((((uint8_t *)&t94))[8] != 0)) {
                    uint64_t t99 = GM_LD(uint64_t, (((uint8_t *)&t94) + 0));
                    t98 = t99;
                } else {
                    gm_b16_8 t100;
                    GM_ST(void *, (uint8_t *)&t100, (void *)(gm_sqp26euqk4rkd));
                    GM_ST(uintptr_t, (uint8_t *)&t100 + 8, (uintptr_t)(15));
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 4, 0);
                    ((uint8_t *)&(*(gm_tl572d7579hns *)gm_out))[0] = 1;
                    GM_ST(gm_b16_8, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 8, t100);
                    goto gm_fdone;
                }
                gm_l9 = t98;
            }
            {
                gm_b16_8 t101 = gm_f_empty_slice_ijtp9lo();
                gm_l10 = t101;
            }
            {
                uint64_t t103 = gm_l9;
                uint8_t t104 = (t103 > ((uint64_t)0ULL));
                if (!t104) goto ie102;
                {
                    void * t106 = gm_l0;
                    void * t107 = (void *)(((uint8_t *)t106) + 184);
                    uint64_t t108 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 0));
                    uint64_t t109 = gm_l9;
                    gm_t934k1j7u70ct t110;
                    gm_f_read_u6r4vns((void *)&t110, t107, t108, t109, ((uint64_t)1ULL));
                    gm_b16_8 t111;
                    if (!((((uint8_t *)&t110))[0] != 0)) {
                        gm_b16_8 t112 = GM_LD(gm_b16_8, (((uint8_t *)&t110) + 8));
                        t111 = t112;
                    } else {
                        F.l11 = (*(gm_tpekhfs32op0f *)(((uint8_t *)&t110) + 8));
                        gm_b16_8 t113 = gm_f_mem_message_e0go901((void *)NULL, cx, NULL, &(*(gm_tpekhfs32op0f *)((uint8_t *)&F.l11)));
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 4, 0);
                        ((uint8_t *)&(*(gm_tl572d7579hns *)gm_out))[0] = 1;
                        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 8, t113);
                        goto gm_fdone;
                    }
                    gm_l10 = t111;
                }
                goto ix102;
ie102:;
ix102:;
            }
            {
                void * t115 = gm_l0;
                gm_t88rcucg1b7uk t116 = gm_l6;
                gm_t53at80pfdus3 t117;
                gm_f_kind_htv6093((void *)&t117, t115, GM_CONV(gm_csguankko4lmhd, t116));
                gm_tjcj4j2gbh6c8 t118;
                GM_ST(uint8_t, (uint8_t *)&t118 + 1, 0);
                GM_ST(uint8_t, ((uint8_t *)&t118 + 0), ((uint8_t)0));
                GM_ST(uint16_t, ((uint8_t *)&t118 + 2), ((uint16_t)8ULL));
                uint8_t t119 = (((GM_LD(uint8_t, ((uint8_t *)&t117) + 0) == ((uint8_t)0ULL)) && gm_eqo3v9599nus((((uint8_t *)&t117) + 8), ((uint8_t *)&t118))));
                if (!t119) goto ie114;
                {
                    uint8_t t122 = gm_l4;
                    if (!t122) goto ie121;
                    {
                        void * t124 = gm_l1;
                        gm_b16_8 t125 = gm_l10;
                        gm_b16_8 t126 = GM_LD(gm_b16_8, ((uint8_t *)&t125));
                        gm_tftb1odc739t5 t127 = gm_f_extend_0cs85gs(t124, t126);
                        if (((((uint8_t *)&t127))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t127) + 1, gm_srcfde9lsgge1ld, 21, 26);
                        gm_b0_1 t128 = GM_LD(gm_b0_1, (((uint8_t *)&t127) + 1));
                    }
                    goto ix121;
ie121:;
                    {
                        void * t130 = gm_l1;
                        gm_b16_8 t131 = gm_l10;
                        gm_b16_8 t132 = GM_LD(gm_b16_8, ((uint8_t *)&t131));
                        gm_b0_1 t133 = gm_f_fmt_quote_f7sotri((void *)NULL, cx, NULL, t130, t132);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
ix121:;
                }
                goto ix114;
ie114:;
                {
                    void * t135 = gm_l0;
                    void * t136 = gm_l1;
                    gm_t88rcucg1b7uk t137 = gm_l6;
                    uint64_t t138 = gm_l8;
                    uint64_t t139 = gm_f_us_cuv01fp(t138);
                    gm_b16_8 t140 = gm_l10;
                    uint64_t t141 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
                    uint64_t t142 = gm_f_us_cuv01fp(t141);
                    gm_tl572d7579hns t143;
                    gm_f_fmt_seq_c3i8abi((void *)&t143, (void *)NULL, cx, NULL, t135, t136, t137, t139, t140, t142);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    if (((((uint8_t *)&t143))[0] != 0)) {
                        gm_b16_8 t144 = GM_LD(gm_b16_8, (((uint8_t *)&t143) + 8));
                        gm_tl572d7579hns t145;
                        GM_ST(uint8_t, (uint8_t *)&t145 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t145 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t145 + 4, 0);
                        ((uint8_t *)&t145)[0] = 1;
                        GM_ST(gm_b16_8, ((uint8_t *)&t145 + 8), t144);
                        F.ret = t145;
                        goto gm_fret;
                    }
                    gm_b0_1 t146 = GM_LD(gm_b0_1, (((uint8_t *)&t143) + 8));
                }
ix114:;
            }
            goto me6;
mn79:;
ma12:;
            uint8_t t148 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
            if (t148 != ((uint8_t)11ULL)) goto mn147;
            uint64_t t149 = GM_LD(uint64_t, ((((uint8_t *)&t5) + 8) + 0));
            gm_l12 = t149;
            gm_t88rcucg1b7uk t150 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t5) + 8) + 8));
            gm_l13 = t150;
            void * t151 = gm_l0;
            void * t152 = gm_l1;
            gm_t88rcucg1b7uk t153 = gm_l13;
            void * t154 = gm_l0;
            gm_t88rcucg1b7uk t155 = gm_l13;
            uint64_t t156 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t154, t155);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint64_t t157 = gm_f_us_cuv01fp(t156);
            gm_b16_8 t158 = gm_l3;
            uint64_t t159 = gm_l12;
            uint64_t t160 = gm_f_us_cuv01fp(t159);
            gm_tl572d7579hns t161;
            gm_f_fmt_seq_c3i8abi((void *)&t161, (void *)NULL, cx, NULL, t151, t152, t153, t157, t158, t160);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t161))[0] != 0)) {
                gm_b16_8 t162 = GM_LD(gm_b16_8, (((uint8_t *)&t161) + 8));
                gm_tl572d7579hns t163;
                GM_ST(uint8_t, (uint8_t *)&t163 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t163 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t163 + 4, 0);
                ((uint8_t *)&t163)[0] = 1;
                GM_ST(gm_b16_8, ((uint8_t *)&t163 + 8), t162);
                F.ret = t163;
                goto gm_fret;
            }
            gm_b0_1 t164 = GM_LD(gm_b0_1, (((uint8_t *)&t161) + 8));
            goto me6;
mn147:;
ma13:;
            uint8_t t166 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
            if (t166 != ((uint8_t)12ULL)) goto mn165;
            gm_b16_8 t167 = GM_LD(gm_b16_8, ((((uint8_t *)&t5) + 8) + 0));
            gm_l14 = t167;
            gm_b16_8 t168 = GM_LD(gm_b16_8, ((((uint8_t *)&t5) + 8) + 16));
            gm_l15 = t168;
            {
                void * t170 = gm_l1;
                gm_tftb1odc739t5 t171 = gm_f_push_t83rg2m(t170, ((uint8_t)40ULL));
                if (((((uint8_t *)&t171))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t171) + 1, gm_srcfde9lsgge1ld, 28, 13);
                gm_b0_1 t172 = GM_LD(gm_b0_1, (((uint8_t *)&t171) + 1));
            }
            {
                {
                    gm_b16_8 t174 = gm_l14;
                    gm_l16 = t174;
                }
                {
                    gm_l17 = ((uint64_t)0ULL);
                }
                {
                    for (;;) {
                        uint64_t t176 = gm_l17;
                        uint64_t t177 = GM_LD(uintptr_t, ((uint8_t *)&gm_l16) + 8);
                        uint8_t t178 = (t176 < t177);
                        if (!t178) goto lb175;
                        {
                            uint64_t t181 = gm_l17;
                            uint64_t t182 = gm_l17;
                            uint64_t t183 = (uintptr_t)t182;
                            uint64_t t184 = GM_LD(uintptr_t, ((uint8_t *)&gm_l16) + 8);
                            gm_t88rcucg1b7uk t185 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l16)) + t183 * 4));
                            gm_tlcm4lvu3g1a8 t186;
                            GM_ST(uint32_t, (uint8_t *)&t186 + 12, 0);
                            GM_ST(uint64_t, ((uint8_t *)&t186 + 0), t181);
                            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t186 + 8), t185);
                            uint64_t t189 = GM_LD(uint64_t, (((uint8_t *)&t186) + 0));
                            gm_l18 = t189;
                            gm_t88rcucg1b7uk t190 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t186) + 8));
                            gm_l19 = t190;
                            {
                                uint64_t t193 = gm_l18;
                                uint8_t t194 = (t193 > ((uint64_t)0ULL));
                                if (!t194) goto ie192;
                                {
                                    void * t196 = gm_l1;
                                    gm_b16_8 t197;
                                    GM_ST(void *, (uint8_t *)&t197, (void *)(gm_sg5gdnrith2i6));
                                    GM_ST(uintptr_t, (uint8_t *)&t197 + 8, (uintptr_t)(2));
                                    gm_tftb1odc739t5 t198 = gm_f_extend_0cs85gs(t196, t197);
                                    if (((((uint8_t *)&t198))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t198) + 1, gm_srcfde9lsgge1ld, 30, 28);
                                    gm_b0_1 t199 = GM_LD(gm_b0_1, (((uint8_t *)&t198) + 1));
                                }
                                goto ix192;
ie192:;
ix192:;
                            }
                            {
                                uint64_t t201 = GM_LD(uintptr_t, ((uint8_t *)&gm_l15) + 8);
                                uint8_t t202 = (t201 > ((uint64_t)0ULL));
                                if (!t202) goto ie200;
                                {
                                    void * t204 = gm_l1;
                                    uint64_t t205 = gm_l18;
                                    uint64_t t206 = (uintptr_t)t205;
                                    uint64_t t207 = GM_LD(uintptr_t, ((uint8_t *)&gm_l15) + 8);
                                    if (t206 >= (uintptr_t)(t207)) gm_trap_index(t206, t207, gm_srcfde9lsgge1ld, 32, 32);
                                    gm_b16_8 t208 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l15)) + t206 * 16));
                                    gm_tftb1odc739t5 t209 = gm_f_extend_0cs85gs(t204, t208);
                                    if (((((uint8_t *)&t209))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t209) + 1, gm_srcfde9lsgge1ld, 32, 21);
                                    gm_b0_1 t210 = GM_LD(gm_b0_1, (((uint8_t *)&t209) + 1));
                                }
                                {
                                    void * t211 = gm_l1;
                                    gm_b16_8 t212;
                                    GM_ST(void *, (uint8_t *)&t212, (void *)(gm_slgmqgkioa4u6));
                                    GM_ST(uintptr_t, (uint8_t *)&t212 + 8, (uintptr_t)(2));
                                    gm_tftb1odc739t5 t213 = gm_f_extend_0cs85gs(t211, t212);
                                    if (((((uint8_t *)&t213))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t213) + 1, gm_srcfde9lsgge1ld, 33, 21);
                                    gm_b0_1 t214 = GM_LD(gm_b0_1, (((uint8_t *)&t213) + 1));
                                }
                                goto ix200;
ie200:;
ix200:;
                            }
                            {
                                void * t215 = gm_l0;
                                void * t216 = gm_l1;
                                gm_t88rcucg1b7uk t217 = gm_l19;
                                void * t218 = gm_l0;
                                gm_t88rcucg1b7uk t219 = gm_l2;
                                gm_b16_8 t220 = gm_l3;
                                uint64_t t221 = gm_l18;
                                uint32_t t222 = (uint32_t)t221;
                                gm_b16_8 t223 = gm_f_field_bytes_bhivr4s((void *)NULL, cx, NULL, t218, t219, t220, t222);
                                if (gm_unw.kind) {
                                    goto gm_ll179;
                                }
                                gm_tl572d7579hns t224;
                                gm_f_format_value_vmle7qr((void *)&t224, (void *)NULL, cx, NULL, t215, t216, t217, t223, ((uint8_t)0));
                                if (gm_unw.kind) {
                                    goto gm_ll179;
                                }
                                if (((((uint8_t *)&t224))[0] != 0)) {
                                    gm_b16_8 t225 = GM_LD(gm_b16_8, (((uint8_t *)&t224) + 8));
                                    gm_tl572d7579hns t226;
                                    GM_ST(uint8_t, (uint8_t *)&t226 + 1, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t226 + 2, 0);
                                    GM_ST(uint32_t, (uint8_t *)&t226 + 4, 0);
                                    ((uint8_t *)&t226)[0] = 1;
                                    GM_ST(gm_b16_8, ((uint8_t *)&t226 + 8), t225);
                                    F.ret = t226;
                                    goto gm_fret;
                                }
                                gm_b0_1 t227 = GM_LD(gm_b0_1, (((uint8_t *)&t224) + 8));
                            }
                            goto me187;
mn188:;
                            gm_trap("no match arm matched the scrutinee", gm_srcfde9lsgge1ld, 29, 13);
me187:;
                        }
lc175:;
                        {
                            uint64_t t229 = gm_l17;
                            uint64_t r230; bool o231 = __builtin_add_overflow(t229, ((uint64_t)1ULL), &r230);
                            uint64_t t232 = r230;
                            gm_l17 = t232;
                        }
                    }
gm_ll179:;
                    if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb175; }
                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc175; }
                    }
                    goto gm_lad;
lb175:;
                }
            }
            {
                uint64_t t234 = GM_LD(uintptr_t, ((uint8_t *)&gm_l14) + 8);
                uint8_t t235 = ((t234 == ((uint64_t)1ULL)));
                if (!t235) goto ie233;
                uint64_t t236 = GM_LD(uintptr_t, ((uint8_t *)&gm_l15) + 8);
                uint8_t t237 = ((t236 == ((uint64_t)0ULL)));
                if (!t237) goto ie233;
                {
                    void * t239 = gm_l1;
                    gm_tftb1odc739t5 t240 = gm_f_push_t83rg2m(t239, ((uint8_t)44ULL));
                    if (((((uint8_t *)&t240))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t240) + 1, gm_srcfde9lsgge1ld, 37, 52);
                    gm_b0_1 t241 = GM_LD(gm_b0_1, (((uint8_t *)&t240) + 1));
                }
                goto ix233;
ie233:;
ix233:;
            }
            {
                void * t242 = gm_l1;
                gm_tftb1odc739t5 t243 = gm_f_push_t83rg2m(t242, ((uint8_t)41ULL));
                if (((((uint8_t *)&t243))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t243) + 1, gm_srcfde9lsgge1ld, 38, 13);
                gm_b0_1 t244 = GM_LD(gm_b0_1, (((uint8_t *)&t243) + 1));
            }
            goto me6;
mn165:;
ma14:;
            uint8_t t246 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
            if (t246 != ((uint8_t)13ULL)) goto mn245;
            gm_t88rcucg1b7uk t247 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t5) + 8));
            gm_l20 = t247;
            {
                void * t250 = gm_l0;
                gm_t88rcucg1b7uk t251 = gm_l2;
                gm_b16_8 t252 = gm_l3;
                gm_b16_8 t253 = GM_LD(gm_b16_8, ((uint8_t *)&t252));
                uint8_t t254 = gm_f_opt_present_lb4i2ip((void *)NULL, cx, NULL, t250, t251, t253);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (!t254) goto ie249;
                {
                    void * t256 = gm_l0;
                    void * t257 = gm_l1;
                    gm_t88rcucg1b7uk t258 = gm_l20;
                    void * t259 = gm_l0;
                    gm_t88rcucg1b7uk t260 = gm_l2;
                    gm_b16_8 t261 = gm_l3;
                    gm_b16_8 t262 = gm_f_opt_payload_1pbnobb((void *)NULL, cx, NULL, t259, t260, t261);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tl572d7579hns t263;
                    gm_f_format_value_vmle7qr((void *)&t263, (void *)NULL, cx, NULL, t256, t257, t258, t262, ((uint8_t)0));
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    if (((((uint8_t *)&t263))[0] != 0)) {
                        gm_b16_8 t264 = GM_LD(gm_b16_8, (((uint8_t *)&t263) + 8));
                        gm_tl572d7579hns t265;
                        GM_ST(uint8_t, (uint8_t *)&t265 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t265 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t265 + 4, 0);
                        ((uint8_t *)&t265)[0] = 1;
                        GM_ST(gm_b16_8, ((uint8_t *)&t265 + 8), t264);
                        F.ret = t265;
                        goto gm_fret;
                    }
                    gm_b0_1 t266 = GM_LD(gm_b0_1, (((uint8_t *)&t263) + 8));
                }
                goto ix249;
ie249:;
                {
                    void * t268 = gm_l1;
                    gm_b16_8 t269;
                    GM_ST(void *, (uint8_t *)&t269, (void *)(gm_squedv51820an));
                    GM_ST(uintptr_t, (uint8_t *)&t269 + 8, (uintptr_t)(4));
                    gm_tftb1odc739t5 t270 = gm_f_extend_0cs85gs(t268, t269);
                    if (((((uint8_t *)&t270))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t270) + 1, gm_srcfde9lsgge1ld, 44, 17);
                    gm_b0_1 t271 = GM_LD(gm_b0_1, (((uint8_t *)&t270) + 1));
                }
ix249:;
            }
            goto me6;
mn245:;
ma15:;
            uint8_t t273 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
            if (t273 != ((uint8_t)14ULL)) goto mn272;
            gm_t88rcucg1b7uk t274 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t5) + 8) + 0));
            gm_l21 = t274;
            gm_t88rcucg1b7uk t275 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t5) + 8) + 4));
            gm_l22 = t275;
            {
                void * t277 = gm_l0;
                gm_t88rcucg1b7uk t278 = gm_l2;
                gm_b16_8 t279 = gm_l3;
                gm_b16_8 t280 = gm_f_res_payload_pb2hcmh((void *)NULL, cx, NULL, t277, t278, t279);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l23 = t280;
            }
            {
                void * t282 = gm_l0;
                gm_t88rcucg1b7uk t283 = gm_l2;
                gm_b16_8 t284 = gm_l3;
                gm_b16_8 t285 = GM_LD(gm_b16_8, ((uint8_t *)&t284));
                uint8_t t286 = gm_f_res_is_err_8jbgndp((void *)NULL, cx, NULL, t282, t283, t285);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (!t286) goto ie281;
                {
                    void * t288 = gm_l1;
                    gm_tftb1odc739t5 t289 = gm_f_push_t83rg2m(t288, ((uint8_t)33ULL));
                    if (((((uint8_t *)&t289))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t289) + 1, gm_srcfde9lsgge1ld, 50, 17);
                    gm_b0_1 t290 = GM_LD(gm_b0_1, (((uint8_t *)&t289) + 1));
                }
                {
                    void * t291 = gm_l0;
                    void * t292 = gm_l1;
                    gm_t88rcucg1b7uk t293 = gm_l21;
                    gm_b16_8 t294 = gm_l23;
                    gm_tl572d7579hns t295;
                    gm_f_format_value_vmle7qr((void *)&t295, (void *)NULL, cx, NULL, t291, t292, t293, t294, ((uint8_t)0));
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    if (((((uint8_t *)&t295))[0] != 0)) {
                        gm_b16_8 t296 = GM_LD(gm_b16_8, (((uint8_t *)&t295) + 8));
                        gm_tl572d7579hns t297;
                        GM_ST(uint8_t, (uint8_t *)&t297 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t297 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t297 + 4, 0);
                        ((uint8_t *)&t297)[0] = 1;
                        GM_ST(gm_b16_8, ((uint8_t *)&t297 + 8), t296);
                        F.ret = t297;
                        goto gm_fret;
                    }
                    gm_b0_1 t298 = GM_LD(gm_b0_1, (((uint8_t *)&t295) + 8));
                }
                goto ix281;
ie281:;
                {
                    void * t300 = gm_l0;
                    void * t301 = gm_l1;
                    gm_t88rcucg1b7uk t302 = gm_l22;
                    gm_b16_8 t303 = gm_l23;
                    gm_tl572d7579hns t304;
                    gm_f_format_value_vmle7qr((void *)&t304, (void *)NULL, cx, NULL, t300, t301, t302, t303, ((uint8_t)0));
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    if (((((uint8_t *)&t304))[0] != 0)) {
                        gm_b16_8 t305 = GM_LD(gm_b16_8, (((uint8_t *)&t304) + 8));
                        gm_tl572d7579hns t306;
                        GM_ST(uint8_t, (uint8_t *)&t306 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t306 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t306 + 4, 0);
                        ((uint8_t *)&t306)[0] = 1;
                        GM_ST(gm_b16_8, ((uint8_t *)&t306 + 8), t305);
                        F.ret = t306;
                        goto gm_fret;
                    }
                    gm_b0_1 t307 = GM_LD(gm_b0_1, (((uint8_t *)&t304) + 8));
                }
ix281:;
            }
            goto me6;
mn272:;
ma16:;
            uint8_t t309 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
            if (t309 != ((uint8_t)17ULL)) goto mn308;
            void * t310 = gm_l0;
            void * t311 = gm_l1;
            gm_t88rcucg1b7uk t312 = gm_l2;
            gm_b16_8 t313 = gm_l3;
            gm_tl572d7579hns t314;
            gm_f_fmt_adt_p6448k2((void *)&t314, (void *)NULL, cx, NULL, t310, t311, t312, t313);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t314))[0] != 0)) {
                gm_b16_8 t315 = GM_LD(gm_b16_8, (((uint8_t *)&t314) + 8));
                gm_tl572d7579hns t316;
                GM_ST(uint8_t, (uint8_t *)&t316 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t316 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t316 + 4, 0);
                ((uint8_t *)&t316)[0] = 1;
                GM_ST(gm_b16_8, ((uint8_t *)&t316 + 8), t315);
                F.ret = t316;
                goto gm_fret;
            }
            gm_b0_1 t317 = GM_LD(gm_b0_1, (((uint8_t *)&t314) + 8));
            goto me6;
mn308:;
ma17:;
            void * t319 = gm_l1;
            void * t320 = gm_l0;
            void * t321 = GM_LD(void *, (((uint8_t *)t320) + 8));
            gm_t88rcucg1b7uk t322 = gm_l2;
            gm_b16_8 t323 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t321, t322);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tnpifv20irnph t324;
            GM_ST(gm_b16_8, ((uint8_t *)&t324 + 0), t323);
            gm_tftb1odc739t5 t325 = gm_f_write_jvie1fn(t319, t324);
            if (((((uint8_t *)&t325))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t325) + 1, gm_srcfde9lsgge1ld, 57, 14);
            gm_b0_1 t326 = GM_LD(gm_b0_1, (((uint8_t *)&t325) + 1));
            goto me6;
mn318:;
ma18:;
            gm_trap("no match arm matched the scrutinee", gm_srcfde9lsgge1ld, 9, 5);
me6:;
        }
    }
    gm_b0_1 t327;
    GM_ST(uint8_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 8, 0);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 16, 0);
    ((uint8_t *)&(*(gm_tl572d7579hns *)gm_out))[0] = 0;
    GM_ST(gm_b0_1, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 8, t327);
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
