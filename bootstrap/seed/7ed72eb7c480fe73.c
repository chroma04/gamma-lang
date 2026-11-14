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
typedef struct gm_csfq64m1m1rt6m { int64_t f0; } gm_csfq64m1m1rt6m;
typedef struct { uint8_t a[8]; } gm_t2doi72kmmlil;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint8_t f0; uint8_t f1; uint8_t f2; uint8_t f3; uint8_t f4; uint8_t f5; } gm_t4q2b5ibsrdik;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint8_t a[256]; } gm_tlv7b7b9lcrkj;
typedef struct { int64_t f0; } gm_tm98ap4112hee;
typedef struct { uint8_t f0; uint8_t p1[3]; union { int32_t v0; } f1; } gm_toe5fmr3vvf4d;
typedef struct { uint8_t a[4096]; } gm_tpbv7nqggnvqr;
typedef struct { uint8_t a[4]; } gm_tpvg3mjn3dp06;
_Static_assert(sizeof(gm_csfq64m1m1rt6m) == 8 && _Alignof(gm_csfq64m1m1rt6m) == 8 && offsetof(gm_csfq64m1m1rt6m, f0) == 0, "layout of `Dir`");
typedef struct gm_cskqtm7tpd0qif { gm_csfq64m1m1rt6m f0; uint64_t f1; } gm_cskqtm7tpd0qif;
_Static_assert(sizeof(gm_t2doi72kmmlil) == 8 && _Alignof(gm_t2doi72kmmlil) == 1 && offsetof(gm_t2doi72kmmlil, a) == 0, "layout of `[8]u8`");
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
_Static_assert(sizeof(gm_t4q2b5ibsrdik) == 6 && _Alignof(gm_t4q2b5ibsrdik) == 1 && offsetof(gm_t4q2b5ibsrdik, f0) == 0 && offsetof(gm_t4q2b5ibsrdik, f1) == 1 && offsetof(gm_t4q2b5ibsrdik, f2) == 2 && offsetof(gm_t4q2b5ibsrdik, f3) == 3 && offsetof(gm_t4q2b5ibsrdik, f4) == 4 && offsetof(gm_t4q2b5ibsrdik, f5) == 5, "layout of `OpenOptions`");
typedef struct { uint64_t f0; gm_t9qkh0rsooc8d f1; uint8_t p2[7]; int64_t f2; uint32_t f3; uint8_t pe[4]; } gm_t5j14m8ru132p;
typedef struct { gm_tm98ap4112hee f0; uint64_t f1; } gm_t5nmvq97ncshv;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_toe5fmr3vvf4d v0; gm_b16_8 v1; } f1; } gm_t5uv522f0p612;
typedef struct { uint64_t f0; gm_t9qkh0rsooc8d f1; uint8_t pe[7]; } gm_t617vr72bd7b3;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_toe5fmr3vvf4d v0; gm_tm98ap4112hee v1; } f1; } gm_t62ebbi2i5rkd;
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
typedef struct { gm_b16_8 f0; uint32_t f1; uint32_t f2; gm_b16_8 f3; } gm_tb40qn7hk0tl9;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_toe5fmr3vvf4d v0; uint64_t v1; } f1; } gm_thf5kr0is9u9i;
typedef struct { gm_b16_8 f0; gm_t9qkh0rsooc8d f1; uint8_t pe[7]; } gm_tibupt4dmcqai;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_toe5fmr3vvf4d v0; gm_t9qkh0rsooc8d v1; } f1; } gm_tj5jn79kq9pnm;
_Static_assert(sizeof(gm_tlv7b7b9lcrkj) == 256 && _Alignof(gm_tlv7b7b9lcrkj) == 1 && offsetof(gm_tlv7b7b9lcrkj, a) == 0, "layout of `[256]u8`");
_Static_assert(sizeof(gm_tm98ap4112hee) == 8 && _Alignof(gm_tm98ap4112hee) == 8 && offsetof(gm_tm98ap4112hee, f0) == 0, "layout of `File`");
_Static_assert(sizeof(gm_toe5fmr3vvf4d) == 8 && _Alignof(gm_toe5fmr3vvf4d) == 4 && offsetof(gm_toe5fmr3vvf4d, f0) == 0 && offsetof(gm_toe5fmr3vvf4d, f1) == 4, "layout of `IoError`");
_Static_assert(sizeof(gm_tpbv7nqggnvqr) == 4096 && _Alignof(gm_tpbv7nqggnvqr) == 1 && offsetof(gm_tpbv7nqggnvqr, a) == 0, "layout of `[4096]u8`");
_Static_assert(sizeof(gm_tpvg3mjn3dp06) == 4 && _Alignof(gm_tpvg3mjn3dp06) == 1 && offsetof(gm_tpvg3mjn3dp06, a) == 0, "layout of `[4]u8`");
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_toe5fmr3vvf4d v0; } f1; } gm_ttln0d0hd70en;
_Static_assert(sizeof(gm_cskqtm7tpd0qif) == 16 && _Alignof(gm_cskqtm7tpd0qif) == 8 && offsetof(gm_cskqtm7tpd0qif, f0) == 0 && offsetof(gm_cskqtm7tpd0qif, f1) == 8, "layout of `Frame`");
typedef struct { gm_t617vr72bd7b3 f0; uint8_t f1; uint8_t pe[7]; } gm_t11t9d8s5diq9;
_Static_assert(sizeof(gm_t5j14m8ru132p) == 32 && _Alignof(gm_t5j14m8ru132p) == 8 && offsetof(gm_t5j14m8ru132p, f0) == 0 && offsetof(gm_t5j14m8ru132p, f1) == 8 && offsetof(gm_t5j14m8ru132p, f2) == 16 && offsetof(gm_t5j14m8ru132p, f3) == 24, "layout of `FileInfo`");
_Static_assert(sizeof(gm_t5nmvq97ncshv) == 16 && _Alignof(gm_t5nmvq97ncshv) == 8 && offsetof(gm_t5nmvq97ncshv, f0) == 0 && offsetof(gm_t5nmvq97ncshv, f1) == 8, "layout of `Frame`");
_Static_assert(sizeof(gm_t5uv522f0p612) == 24 && _Alignof(gm_t5uv522f0p612) == 8 && offsetof(gm_t5uv522f0p612, f0) == 0 && offsetof(gm_t5uv522f0p612, f1) == 8, "layout of `IoError!string`");
_Static_assert(sizeof(gm_t617vr72bd7b3) == 16 && _Alignof(gm_t617vr72bd7b3) == 8 && offsetof(gm_t617vr72bd7b3, f0) == 0 && offsetof(gm_t617vr72bd7b3, f1) == 8, "layout of `DirEntry`");
_Static_assert(sizeof(gm_t62ebbi2i5rkd) == 16 && _Alignof(gm_t62ebbi2i5rkd) == 8 && offsetof(gm_t62ebbi2i5rkd, f0) == 0 && offsetof(gm_t62ebbi2i5rkd, f1) == 8, "layout of `IoError!File`");
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
_Static_assert(sizeof(gm_tb40qn7hk0tl9) == 40 && _Alignof(gm_tb40qn7hk0tl9) == 8 && offsetof(gm_tb40qn7hk0tl9, f0) == 0 && offsetof(gm_tb40qn7hk0tl9, f1) == 16 && offsetof(gm_tb40qn7hk0tl9, f2) == 20 && offsetof(gm_tb40qn7hk0tl9, f3) == 24, "layout of `SourceLocation`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_toe5fmr3vvf4d v0; gm_tibupt4dmcqai v1; } f1; } gm_te2n4j2vtm53d;
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tj5jn79kq9pnm v0; gm_b16_8 v1; } f1; } gm_tgvh15qshf643;
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
_Static_assert(sizeof(gm_thf5kr0is9u9i) == 16 && _Alignof(gm_thf5kr0is9u9i) == 8 && offsetof(gm_thf5kr0is9u9i, f0) == 0 && offsetof(gm_thf5kr0is9u9i, f1) == 8, "layout of `IoError!usize`");
_Static_assert(sizeof(gm_tibupt4dmcqai) == 24 && _Alignof(gm_tibupt4dmcqai) == 8 && offsetof(gm_tibupt4dmcqai, f0) == 0 && offsetof(gm_tibupt4dmcqai, f1) == 16, "layout of `StrLit`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_toe5fmr3vvf4d v0; gm_t5j14m8ru132p v1; } f1; } gm_tih6h4avv5nrb;
_Static_assert(sizeof(gm_tj5jn79kq9pnm) == 12 && _Alignof(gm_tj5jn79kq9pnm) == 4 && offsetof(gm_tj5jn79kq9pnm, f0) == 0 && offsetof(gm_tj5jn79kq9pnm, f1) == 4, "layout of `ReadError`");
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
typedef struct { gm_t5nmvq97ncshv f0; uint8_t f1; uint8_t pe[7]; } gm_tsbj0adhq3qho;
_Static_assert(sizeof(gm_ttln0d0hd70en) == 12 && _Alignof(gm_ttln0d0hd70en) == 4 && offsetof(gm_ttln0d0hd70en, f0) == 0 && offsetof(gm_ttln0d0hd70en, f1) == 4, "layout of `IoError!()`");
_Static_assert(sizeof(gm_t11t9d8s5diq9) == 24 && _Alignof(gm_t11t9d8s5diq9) == 8 && offsetof(gm_t11t9d8s5diq9, f0) == 0 && offsetof(gm_t11t9d8s5diq9, f1) == 16, "layout of `?DirEntry`");
_Static_assert(sizeof(gm_te2n4j2vtm53d) == 32 && _Alignof(gm_te2n4j2vtm53d) == 8 && offsetof(gm_te2n4j2vtm53d, f0) == 0 && offsetof(gm_te2n4j2vtm53d, f1) == 8, "layout of `IoError!Entry`");
_Static_assert(sizeof(gm_tgvh15qshf643) == 24 && _Alignof(gm_tgvh15qshf643) == 8 && offsetof(gm_tgvh15qshf643, f0) == 0 && offsetof(gm_tgvh15qshf643, f1) == 8, "layout of `ReadError![]u8`");
_Static_assert(sizeof(gm_tih6h4avv5nrb) == 40 && _Alignof(gm_tih6h4avv5nrb) == 8 && offsetof(gm_tih6h4avv5nrb, f0) == 0 && offsetof(gm_tih6h4avv5nrb, f1) == 8, "layout of `IoError!FileInfo`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_toe5fmr3vvf4d v0; gm_t11t9d8s5diq9 v1; } f1; } gm_tiuq7k5hm0kak;
_Static_assert(sizeof(gm_tsbj0adhq3qho) == 24 && _Alignof(gm_tsbj0adhq3qho) == 8 && offsetof(gm_tsbj0adhq3qho, f0) == 0 && offsetof(gm_tsbj0adhq3qho, f1) == 16, "layout of `?Frame`");
_Static_assert(sizeof(gm_tiuq7k5hm0kak) == 32 && _Alignof(gm_tiuq7k5hm0kak) == 8 && offsetof(gm_tiuq7k5hm0kak, f0) == 0 && offsetof(gm_tiuq7k5hm0kak, f1) == 8, "layout of `IoError!?DirEntry`");
struct gm_fr_close_frames_46uf1s1 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_copy_fg1ge35 {
    gm_frame h;
    gm_tpbv7nqggnvqr l4; /* buf */
    gm_ttln0d0hd70en ret;
};
struct gm_fr_copy_from_f7j6p05 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_empty_slice_ijtp9lo {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_p5lo8ed {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_entries_44o5n9f {
    gm_frame h;
    gm_tlv7b7b9lcrkj l3; /* buf */
    gm_b0_1 ret;
};
struct gm_fr_exists_1uo1rk0 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_fill_temp_g12d8pg {
    gm_frame h;
    gm_ttln0d0hd70en ret;
};
struct gm_fr_hex_digit_fdbg8g2 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_sep_h2f3b7l {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_separator_mbkq9i8 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_list_g3vmkan {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_uvfu1k1 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_make_dir_all_q6phjan {
    gm_frame h;
    gm_ttln0d0hd70en ret;
};
struct gm_fr_make_temp_dir_vi8f88d {
    gm_frame h;
    gm_t5uv522f0p612 ret;
};
struct gm_fr_pop_f3uajlg {
    gm_frame h;
    gm_tsbj0adhq3qho ret;
};
struct gm_fr_read_all_r9rh9qd {
    gm_frame h;
    gm_tgvh15qshf643 ret;
};
struct gm_fr_remove_all_h0k1qkh {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* cur */
    gm_trqfvmjtt4sij l3; /* frames */
    gm_tlv7b7b9lcrkj l5; /* buf */
    gm_ttln0d0hd70en ret;
};
struct gm_fr_temp_dir_i6apmsd {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_temp_random_aie9j36 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_temp_stem_3b7s4ll {
    gm_frame h;
    gm_tf5srh4mp4b3i ret;
};
struct gm_fr_truncate_ov4pgoq {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_write_all_o8ucaji {
    gm_frame h;
    gm_ttln0d0hd70en ret;
};
struct gm_fr_write_all_to_fe22ls9 {
    gm_frame h;
    gm_ttln0d0hd70en ret;
};
struct gm_fr_write_atomic_18k5vfb {
    gm_frame h;
    gm_tpbv7nqggnvqr l2; /* name */
    gm_ttln0d0hd70en ret;
};
__attribute__((aligned(4))) gm_b0_1 gm_f_close_frames_46uf1s1(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_ttln0d0hd70en gm_f_copy_fg1ge35(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_copy_from_f7j6p05(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_deinit_ectdopm(void * a0);
__attribute__((aligned(4))) void gm_f_deinit_r70pji5(void * a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_p5lo8ed(void);
__attribute__((aligned(4))) gm_b0_1 gm_f_entries_44o5n9f(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_entries_44o5n9f_s2qjl18(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_entries_44o5n9f_s40i17b(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_entries_44o5n9f_s58h1st(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_entries_44o5n9f_sbacjg5(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_entries_44o5n9f_sh4kvg0(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_entries_44o5n9f_siirbaa(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_entries_44o5n9f_skhefef(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_entries_44o5n9f_sr88c6d(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_entries_44o5n9f_sre2b72(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_entries_44o5n9f_ssu37o5(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_entries_44o5n9f_suo216n(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) uint8_t gm_f_exists_1uo1rk0(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_extend_0cs85gs(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_ttln0d0hd70en gm_f_fill_temp_g12d8pg(void *env, gm_node *cx, gm_node *inst, gm_tm98ap4112hee a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_for_body_0m0ntf2(void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_for_body_1b63vnj(void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_for_body_4g8ig10(void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_for_body_6ellthc(void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_for_body_86gje82(void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_for_body_bq6tmqb(void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_for_body_fnpff4s(void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_for_body_ghum27l(void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_for_body_iv0iced(void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_for_body_nnvr1n3(void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_for_body_terksaa(void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_free_rkuh2rm(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, const void * a1);
inline __attribute__((aligned(4))) uint8_t gm_f_hex_digit_fdbg8g2(uint8_t a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_sep_h2f3b7l(uint8_t a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_separator_mbkq9i8(uint8_t a0);
inline __attribute__((aligned(4))) void gm_f_list_g3vmkan(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_make_2ip78bj(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint64_t a0, const void * a1);
__attribute__((aligned(4))) gm_ttln0d0hd70en gm_f_make_dir_all_q6phjan(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_make_temp_dir_vi8f88d(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1, gm_b16_8 a2);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_pop_f3uajlg(void *gm_out, void * a0);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_8h1r7s6(void * a0, gm_cskqtm7tpd0qif a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_t83rg2m(void * a0, uint8_t a1);
__attribute__((aligned(4))) void gm_f_read_all_r9rh9qd(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_ttln0d0hd70en gm_f_remove_all_h0k1qkh(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_temp_dir_i6apmsd(void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_b0_1 gm_f_temp_random_aie9j36(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, uint64_t a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_temp_stem_3b7s4ll(gm_b16_8 a0, gm_b16_8 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_truncate_ov4pgoq(void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_ttln0d0hd70en gm_f_write_all_o8ucaji(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_ttln0d0hd70en gm_f_write_all_to_fe22ls9(void *env, gm_node *cx, gm_node *inst, gm_tm98ap4112hee a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_ttln0d0hd70en gm_f_write_atomic_18k5vfb(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
extern uint8_t gm_kfgeo9idkmp[40] __attribute__((aligned(8)));
extern uint8_t gm_km7ke510mna[40] __attribute__((aligned(8)));
extern uint8_t gm_koeeht11j75[40] __attribute__((aligned(8)));
extern uint8_t gm_kr3abq56gec[40] __attribute__((aligned(8)));
extern uint8_t gm_kreaco1pgk3[40] __attribute__((aligned(8)));
extern uint8_t gm_ku5cbgje11b[40] __attribute__((aligned(8)));
extern uint8_t gm_kutnfkdjl3d[40] __attribute__((aligned(8)));
extern uint8_t gm_kvu1vd24o16[40] __attribute__((aligned(8)));
extern const uint8_t gm_s4j8rhn4svm30[7];
extern const uint8_t gm_sp3jmpmb6sldf[5];
extern const uint8_t gm_sqofr58g480ri[39];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_srcc24pnihhgkug[];
extern const char gm_srcdo1hnokt0mq8[];
extern const char gm_srcrgc9bkquf6nr[];
extern const uint8_t gm_srd2ukct5o1qh[6];
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
            gm_buf mb = {0}; size_t ml = GM_LD(uintptr_t, ((uint8_t *)&t5) + 8); const uint8_t *mp = GM_LD(void *, ((uint8_t *)&t5)); if (ml == 16 && memcmp(mp, "assertion failed", 16) == 0) gm_buf_push(&mb, mp, ml); else { gm_buf_push(&mb, "assertion failed: ", 18); gm_buf_push(&mb, mp, ml); } gm_trap_bytes(mb.data, mb.len, gm_srcc24pnihhgkug, 24, 5);
        }
    }
    {
        gm_b16_8 t6 = gm_l0;
        gm_b16_8 t7 = gm_l1;
        if (GM_LD(uintptr_t, ((uint8_t *)&t6) + 8) != 0) memmove(GM_LD(void *, ((uint8_t *)&t6)), GM_LD(void *, ((uint8_t *)&t7)), GM_LD(uintptr_t, ((uint8_t *)&t6) + 8) * 1);
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_p5lo8ed(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_p5lo8ed F;
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
inline __attribute__((aligned(4))) uint8_t gm_f_exists_1uo1rk0(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_exists_1uo1rk0 F;
    gm_b16_8 gm_l0; /* path */
    GM_STACK_CHECK(&F, 108u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        {
            gm_b16_8 t3 = gm_l0;
            gm_node *pn4 = gm_find(cx, 2126215853);
            if (!pn4) gm_trap("no handler installed for effect `Fs`", gm_srcdo1hnokt0mq8, 124, 9);
            gm_tih6h4avv5nrb t5;
            ((void (*)(void *, void *, gm_node *, gm_node *, gm_b16_8))pn4->clauses[5])((void *)&t5, pn4->env, pn4->hctx, pn4, t3);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t5j14m8ru132p t6;
            if (!((((uint8_t *)&t5))[0] != 0)) {
                t6 = (*(gm_t5j14m8ru132p *)(((uint8_t *)&t5) + 8));
            } else {
                uint8_t t7 = 0;
                F.ret = ((uint8_t)0);
                goto gm_fret;
            }
        }
    }
    {
        uint8_t t8 = 0;
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
inline __attribute__((aligned(4))) uint8_t gm_f_hex_digit_fdbg8g2(uint8_t a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_hex_digit_fdbg8g2 F;
    uint8_t gm_l0; /* v */
    GM_STACK_CHECK_SHALLOW(&F, 13u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t t1;
    uint8_t t3 = gm_l0;
    uint8_t t4 = (t3 < ((uint8_t)10ULL));
    if (!t4) goto ie2;
    uint8_t t6 = gm_l0;
    uint8_t r7; bool o8 = __builtin_add_overflow(((uint8_t)48ULL), t6, &r7);
    if (o8) gm_trap("integer overflow in `+`", gm_srcdo1hnokt0mq8, 530, 45);
    uint8_t t9 = r7;
    uint8_t t10 = t9;
    t1 = t10;
    goto ix2;
ie2:;
    uint8_t t12 = gm_l0;
    uint8_t r13; bool o14 = __builtin_add_overflow(((uint8_t)97ULL), t12, &r13);
    if (o14) gm_trap("integer overflow in `+`", gm_srcdo1hnokt0mq8, 530, 62);
    uint8_t t15 = r13;
    uint8_t r16; bool o17 = __builtin_sub_overflow(t15, ((uint8_t)10ULL), &r16);
    if (o17) gm_trap("integer overflow in `-`", gm_srcdo1hnokt0mq8, 530, 62);
    uint8_t t18 = r16;
    uint8_t t19 = t18;
    t1 = t19;
ix2:;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_sep_h2f3b7l(uint8_t a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_sep_h2f3b7l F;
    uint8_t gm_l0; /* c */
    GM_STACK_CHECK_SHALLOW(&F, 6u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        {
            uint8_t t3 = gm_l0;
            uint8_t t4 = ((t3 == ((uint8_t)47ULL)));
            uint8_t t5 = 0;
            F.ret = t4;
            goto gm_fret;
        }
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_separator_mbkq9i8(uint8_t a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_separator_mbkq9i8 F;
    uint8_t gm_l0; /* c */
    GM_STACK_CHECK_SHALLOW(&F, 6u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        {
            uint8_t t3 = gm_l0;
            uint8_t t4 = ((t3 == ((uint8_t)47ULL)));
            uint8_t t5 = 0;
            F.ret = t4;
            goto gm_fret;
        }
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_list_g3vmkan(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_g3vmkan F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_p5lo8ed();
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_pop_f3uajlg(void *gm_out, void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_pop_f3uajlg F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* n */
    gm_t5nmvq97ncshv gm_l2; /* x */
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
            gm_tsbj0adhq3qho t8;
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
        gm_t5nmvq97ncshv t17 = GM_LD(gm_t5nmvq97ncshv, ((uint8_t *)GM_LD(void *, (((uint8_t *)t15) + 0)) + t14 * 16));
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
        gm_t5nmvq97ncshv t29 = gm_l2;
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tsbj0adhq3qho *)gm_out) + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tsbj0adhq3qho *)gm_out) + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tsbj0adhq3qho *)gm_out) + 20, 0);
        ((uint8_t *)&(*(gm_tsbj0adhq3qho *)gm_out))[16] = 1;
        GM_ST(gm_t5nmvq97ncshv, (uint8_t *)&(*(gm_tsbj0adhq3qho *)gm_out) + 0, t29);
        goto gm_fdone;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_truncate_ov4pgoq(void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_truncate_ov4pgoq F;
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
            GM_ST(void *, (uint8_t *)&t15, (void *)(((uint8_t *)t11 + t12 * 1)));
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_close_frames_46uf1s1(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_close_frames_46uf1s1 F;
    void * gm_l0; /* frames */
    gm_b16_8 gm_l1; /* <tmp> */
    uint64_t gm_l2; /* <tmp> */
    gm_t5nmvq97ncshv gm_l3; /* f */
    GM_STACK_CHECK(&F, 161u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        {
            void * t3 = gm_l0;
            gm_b16_8 t4 = GM_LD(gm_b16_8, (((uint8_t *)t3) + 0));
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
                    gm_t5nmvq97ncshv t16 = GM_LD(gm_t5nmvq97ncshv, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t12 * 16));
                    gm_l3 = t16;
                    {
                        gm_tm98ap4112hee t18 = GM_LD(gm_tm98ap4112hee, (((uint8_t *)&gm_l3) + 0));
                        gm_node *pn19 = gm_find(cx, 2126215853);
                        if (!pn19) gm_trap("no handler installed for effect `Fs`", gm_srcdo1hnokt0mq8, 202, 29);
                        gm_b0_1 t20;
                        t20 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, gm_tm98ap4112hee))pn19->clauses[12])(pn19->env, pn19->hctx, pn19, t18);
                        if (gm_unw.kind) {
                            goto gm_ll9;
                        }
                    }
                    goto me14;
mn15:;
                    gm_trap("no match arm matched the scrutinee", gm_srcdo1hnokt0mq8, 202, 5);
me14:;
                }
lc5:;
                {
                    uint64_t t22 = gm_l2;
                    uint64_t r23; bool o24 = __builtin_add_overflow(t22, ((uint64_t)1ULL), &r23);
                    uint64_t t25 = r23;
                    gm_l2 = t25;
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
        void * t26 = gm_l0;
        gm_f_deinit_ectdopm(t26);
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
__attribute__((aligned(4))) gm_ttln0d0hd70en gm_f_copy_fg1ge35(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1) {
    struct gm_fr_copy_fg1ge35 F;
    gm_b16_8 gm_l0; /* from */
    gm_b16_8 gm_l1; /* to */
    gm_tm98ap4112hee gm_l2; /* src */
    gm_tm98ap4112hee gm_l3; /* dst */
    uint64_t gm_l5; /* total */
    uint64_t gm_l6; /* n */
    GM_STACK_CHECK(&F, 550u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        gm_b16_8 t2 = gm_l0;
        gm_t4q2b5ibsrdik t3;
        GM_ST(uint8_t, ((uint8_t *)&t3 + 0), ((uint8_t)1));
        GM_ST(uint8_t, ((uint8_t *)&t3 + 1), ((uint8_t)0));
        GM_ST(uint8_t, ((uint8_t *)&t3 + 2), ((uint8_t)0));
        GM_ST(uint8_t, ((uint8_t *)&t3 + 3), ((uint8_t)0));
        GM_ST(uint8_t, ((uint8_t *)&t3 + 4), ((uint8_t)0));
        GM_ST(uint8_t, ((uint8_t *)&t3 + 5), ((uint8_t)0));
        gm_node *pn4 = gm_find(cx, 2126215853);
        if (!pn4) gm_trap("no handler installed for effect `Fs`", gm_srcdo1hnokt0mq8, 131, 16);
        gm_t62ebbi2i5rkd t5;
        t5 = ((gm_t62ebbi2i5rkd (*)(void *, gm_node *, gm_node *, gm_b16_8, gm_t4q2b5ibsrdik))pn4->clauses[0])(pn4->env, pn4->hctx, pn4, t2, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t5))[0] != 0)) {
            gm_toe5fmr3vvf4d t6 = GM_LD(gm_toe5fmr3vvf4d, (((uint8_t *)&t5) + 8));
            gm_ttln0d0hd70en t7;
            GM_ST(uint8_t, (uint8_t *)&t7 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t7 + 2, 0);
            ((uint8_t *)&t7)[0] = 1;
            GM_ST(gm_toe5fmr3vvf4d, ((uint8_t *)&t7 + 4), t6);
            F.ret = t7;
            goto gm_fret;
        }
        gm_tm98ap4112hee t8 = GM_LD(gm_tm98ap4112hee, (((uint8_t *)&t5) + 8));
        gm_l2 = t8;
    }
    {
    }
    {
        gm_b16_8 t12 = gm_l1;
        gm_t4q2b5ibsrdik t13;
        GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0));
        GM_ST(uint8_t, ((uint8_t *)&t13 + 1), ((uint8_t)1));
        GM_ST(uint8_t, ((uint8_t *)&t13 + 2), ((uint8_t)0));
        GM_ST(uint8_t, ((uint8_t *)&t13 + 3), ((uint8_t)1));
        GM_ST(uint8_t, ((uint8_t *)&t13 + 4), ((uint8_t)0));
        GM_ST(uint8_t, ((uint8_t *)&t13 + 5), ((uint8_t)0));
        gm_node *pn14 = gm_find(cx, 2126215853);
        if (!pn14) gm_trap("no handler installed for effect `Fs`", gm_srcdo1hnokt0mq8, 135, 16);
        gm_t62ebbi2i5rkd t15;
        t15 = ((gm_t62ebbi2i5rkd (*)(void *, gm_node *, gm_node *, gm_b16_8, gm_t4q2b5ibsrdik))pn14->clauses[0])(pn14->env, pn14->hctx, pn14, t12, t13);
        if (gm_unw.kind) {
            goto gm_ld1_1;
        }
        if (((((uint8_t *)&t15))[0] != 0)) {
            gm_toe5fmr3vvf4d t16 = GM_LD(gm_toe5fmr3vvf4d, (((uint8_t *)&t15) + 8));
            gm_ttln0d0hd70en t17;
            GM_ST(uint8_t, (uint8_t *)&t17 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t17 + 2, 0);
            ((uint8_t *)&t17)[0] = 1;
            GM_ST(gm_toe5fmr3vvf4d, ((uint8_t *)&t17 + 4), t16);
            F.ret = t17;
            gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = 1;
            goto gm_ld1_1;
        }
        gm_tm98ap4112hee t18 = GM_LD(gm_tm98ap4112hee, (((uint8_t *)&t15) + 8));
        gm_l3 = t18;
    }
    {
    }
    {
    }
    {
        gm_l5 = ((uint64_t)0ULL);
    }
    {
        for (;;) {
            if (!((uint8_t)1)) goto lb22;
            {
                gm_tm98ap4112hee t25 = gm_l2;
                void * t26 = ((uint8_t *)&F.l4);
                if ((uintptr_t)0 > (uintptr_t)4096 || (uintptr_t)4096 > (uintptr_t)4096) gm_trap_slice((uintptr_t)0, (uintptr_t)4096, 4096, gm_srcdo1hnokt0mq8, 140, 27);
                gm_b16_8 t27;
                GM_ST(void *, (uint8_t *)&t27, (void *)(((uint8_t *)t26 + (uintptr_t)0 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t27 + 8, (uintptr_t)((uintptr_t)4096 - (uintptr_t)0));
                gm_node *pn28 = gm_find(cx, 2126215853);
                if (!pn28) gm_trap("no handler installed for effect `Fs`", gm_srcdo1hnokt0mq8, 140, 18);
                gm_thf5kr0is9u9i t29;
                t29 = ((gm_thf5kr0is9u9i (*)(void *, gm_node *, gm_node *, gm_tm98ap4112hee, gm_b16_8))pn28->clauses[2])(pn28->env, pn28->hctx, pn28, t25, t27);
                if (gm_unw.kind) {
                    goto gm_ll23;
                }
                if (((((uint8_t *)&t29))[0] != 0)) {
                    gm_toe5fmr3vvf4d t30 = GM_LD(gm_toe5fmr3vvf4d, (((uint8_t *)&t29) + 8));
                    gm_ttln0d0hd70en t31;
                    GM_ST(uint8_t, (uint8_t *)&t31 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t31 + 2, 0);
                    ((uint8_t *)&t31)[0] = 1;
                    GM_ST(gm_toe5fmr3vvf4d, ((uint8_t *)&t31 + 4), t30);
                    F.ret = t31;
                    gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = 1;
                    goto gm_ll23;
                }
                uint64_t t32 = GM_LD(uint64_t, (((uint8_t *)&t29) + 8));
                gm_l6 = t32;
            }
            {
                uint64_t t34 = gm_l6;
                uint8_t t35 = ((t34 == ((uint64_t)0ULL)));
                if (!t35) goto ie33;
                {
                    goto lb22;
                }
                goto ix33;
ie33:;
ix33:;
            }
            {
                gm_tm98ap4112hee t37 = gm_l3;
                void * t38 = ((uint8_t *)&F.l4);
                uint64_t t39 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t40 = gm_l6;
                uint64_t t41 = (uintptr_t)t40;
                if (t39 > t41 || t41 > (uintptr_t)4096) gm_trap_slice(t39, t41, 4096, gm_srcdo1hnokt0mq8, 142, 31);
                gm_b16_8 t42;
                GM_ST(void *, (uint8_t *)&t42, (void *)(((uint8_t *)t38 + t39 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t42 + 8, (uintptr_t)(t41 - t39));
                gm_b16_8 t43 = GM_LD(gm_b16_8, ((uint8_t *)&t42));
                gm_ttln0d0hd70en t44 = gm_f_write_all_to_fe22ls9((void *)NULL, cx, NULL, t37, t43);
                if (gm_unw.kind) {
                    goto gm_ll23;
                }
                if (((((uint8_t *)&t44))[0] != 0)) {
                    gm_toe5fmr3vvf4d t45 = GM_LD(gm_toe5fmr3vvf4d, (((uint8_t *)&t44) + 4));
                    gm_ttln0d0hd70en t46;
                    GM_ST(uint8_t, (uint8_t *)&t46 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t46 + 2, 0);
                    ((uint8_t *)&t46)[0] = 1;
                    GM_ST(gm_toe5fmr3vvf4d, ((uint8_t *)&t46 + 4), t45);
                    F.ret = t46;
                    gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = 1;
                    goto gm_ll23;
                }
                gm_b0_1 t47 = GM_LD(gm_b0_1, (((uint8_t *)&t44) + 4));
            }
            {
                uint64_t t48 = gm_l5;
                uint64_t t49 = gm_l6;
                uint64_t t50 = (uint64_t)t49;
                uint64_t r51; bool o52 = __builtin_add_overflow(t48, t50, &r51);
                if (o52) gm_trap("integer overflow in `+`", gm_srcdo1hnokt0mq8, 143, 9);
                uint64_t t53 = r51;
                gm_l5 = t53;
            }
lc22:;
        }
gm_ll23:;
        if (gm_unw.frame == &F.h && gm_unw.label == 0) {
            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb22; }
            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc22; }
        }
        goto gm_ld1_2;
lb22:;
    }
    {
        gm_tm98ap4112hee t54 = gm_l3;
        uint64_t t55 = gm_l5;
        gm_node *pn56 = gm_find(cx, 2126215853);
        if (!pn56) gm_trap("no handler installed for effect `Fs`", gm_srcdo1hnokt0mq8, 145, 9);
        gm_ttln0d0hd70en t57;
        t57 = ((gm_ttln0d0hd70en (*)(void *, gm_node *, gm_node *, gm_tm98ap4112hee, uint64_t))pn56->clauses[14])(pn56->env, pn56->hctx, pn56, t54, t55);
        if (gm_unw.kind) {
            goto gm_ld1_2;
        }
        if (((((uint8_t *)&t57))[0] != 0)) {
            gm_toe5fmr3vvf4d t58 = GM_LD(gm_toe5fmr3vvf4d, (((uint8_t *)&t57) + 4));
            gm_ttln0d0hd70en t59;
            GM_ST(uint8_t, (uint8_t *)&t59 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t59 + 2, 0);
            ((uint8_t *)&t59)[0] = 1;
            GM_ST(gm_toe5fmr3vvf4d, ((uint8_t *)&t59 + 4), t58);
            F.ret = t59;
            gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = 1;
            goto gm_ld1_2;
        }
        gm_b0_1 t60 = GM_LD(gm_b0_1, (((uint8_t *)&t57) + 4));
    }
    {
        gm_b0_1 t61;
        gm_ttln0d0hd70en t62;
        GM_ST(uint8_t, (uint8_t *)&t62 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t62 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t62 + 4, 0);
        GM_ST(uint32_t, (uint8_t *)&t62 + 8, 0);
        ((uint8_t *)&t62)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t62 + 4), t61);
        uint8_t t63 = ((((uint8_t *)&t62))[0] != 0);
        F.ret = t62;
        gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = t63;
        goto gm_ld1_2;
    }
    {
        gm_tm98ap4112hee t64 = gm_l3;
        gm_node *pn65 = gm_find(cx, 2126215853);
        if (!pn65) gm_trap("no handler installed for effect `Fs`", gm_srcdo1hnokt0mq8, 136, 11);
        gm_b0_1 t66;
        t66 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, gm_tm98ap4112hee))pn65->clauses[1])(pn65->env, pn65->hctx, pn65, t64);
    }
    {
        gm_tm98ap4112hee t67 = gm_l2;
        gm_node *pn68 = gm_find(cx, 2126215853);
        if (!pn68) gm_trap("no handler installed for effect `Fs`", gm_srcdo1hnokt0mq8, 132, 11);
        gm_b0_1 t69;
        t69 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, gm_tm98ap4112hee))pn68->clauses[1])(pn68->env, pn68->hctx, pn68, t67);
    }
    goto gm_ls70;
gm_ld1_2:;
    {
        gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
        gm_tm98ap4112hee t71 = gm_l3;
        gm_node *pn72 = gm_find(cx, 2126215853);
        if (!pn72) gm_trap("no handler installed for effect `Fs`", gm_srcdo1hnokt0mq8, 136, 11);
        gm_b0_1 t73;
        t73 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, gm_tm98ap4112hee))pn72->clauses[1])(pn72->env, pn72->hctx, pn72, t71);
        gm_unw = gm_saved;
    }
gm_ld1_1:;
    {
        gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
        gm_tm98ap4112hee t74 = gm_l2;
        gm_node *pn75 = gm_find(cx, 2126215853);
        if (!pn75) gm_trap("no handler installed for effect `Fs`", gm_srcdo1hnokt0mq8, 132, 11);
        gm_b0_1 t76;
        t76 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, gm_tm98ap4112hee))pn75->clauses[1])(pn75->env, pn75->hctx, pn75, t74);
        gm_unw = gm_saved;
    }
    goto gm_lad;
gm_ls70:;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_entries_44o5n9f(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_entries_44o5n9f F;
    gm_b16_8 gm_l0; /* path */
    gm_toe5fmr3vvf4d gm_l1; /* e */
    gm_tm98ap4112hee gm_l2; /* d */
    gm_toe5fmr3vvf4d gm_l4; /* e */
    gm_t11t9d8s5diq9 gm_l5; /* next */
    gm_t617vr72bd7b3 gm_l6; /* ent */
    GM_STACK_CHECK(&F, 515u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_b16_8 t2 = gm_l0;
        gm_node *pn3 = gm_find(cx, 2126215853);
        if (!pn3) gm_trap("no handler installed for effect `Fs`", gm_srcdo1hnokt0mq8, 170, 10);
        gm_t62ebbi2i5rkd t4;
        t4 = ((gm_t62ebbi2i5rkd (*)(void *, gm_node *, gm_node *, gm_b16_8))pn3->clauses[10])(pn3->env, pn3->hctx, pn3, t2);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tm98ap4112hee t5;
        if (!((((uint8_t *)&t4))[0] != 0)) {
            gm_tm98ap4112hee t6 = GM_LD(gm_tm98ap4112hee, (((uint8_t *)&t4) + 8));
            t5 = t6;
        } else {
            gm_toe5fmr3vvf4d t7 = GM_LD(gm_toe5fmr3vvf4d, (((uint8_t *)&t4) + 8));
            gm_l1 = t7;
            {
                gm_toe5fmr3vvf4d t9 = gm_l1;
                gm_te2n4j2vtm53d t10;
                GM_ST(uint8_t, (uint8_t *)&t10 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t10 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t10 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t10 + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&t10 + 24, 0);
                ((uint8_t *)&t10)[0] = 1;
                GM_ST(gm_toe5fmr3vvf4d, ((uint8_t *)&t10 + 8), t9);
                gm_node *pn11 = gm_find(cx, 2076700068);
                if (!pn11) gm_trap("no handler installed for effect `Yield`", gm_srcdo1hnokt0mq8, 171, 9);
                gm_b0_1 t12;
                t12 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, const void *))pn11->clauses[0])(pn11->env, pn11->hctx, pn11, &t10);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b0_1 t13;
            uint8_t t14 = 0;
            F.ret = t13;
            goto gm_fret;
        }
        gm_l2 = t5;
    }
    {
    }
    {
    }
    {
        for (;;) {
            if (!((uint8_t)1)) goto lb18;
            {
                gm_tm98ap4112hee t21 = gm_l2;
                void * t22 = ((uint8_t *)&F.l3);
                if ((uintptr_t)0 > (uintptr_t)256 || (uintptr_t)256 > (uintptr_t)256) gm_trap_slice((uintptr_t)0, (uintptr_t)256, 256, gm_srcdo1hnokt0mq8, 178, 30);
                gm_b16_8 t23;
                GM_ST(void *, (uint8_t *)&t23, (void *)(((uint8_t *)t22 + (uintptr_t)0 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t23 + 8, (uintptr_t)((uintptr_t)256 - (uintptr_t)0));
                gm_node *pn24 = gm_find(cx, 2126215853);
                if (!pn24) gm_trap("no handler installed for effect `Fs`", gm_srcdo1hnokt0mq8, 178, 17);
                gm_tiuq7k5hm0kak t25;
                ((void (*)(void *, void *, gm_node *, gm_node *, gm_tm98ap4112hee, gm_b16_8))pn24->clauses[11])((void *)&t25, pn24->env, pn24->hctx, pn24, t21, t23);
                if (gm_unw.kind) {
                    goto gm_ll19;
                }
                gm_t11t9d8s5diq9 t26;
                if (!((((uint8_t *)&t25))[0] != 0)) {
                    t26 = (*(gm_t11t9d8s5diq9 *)(((uint8_t *)&t25) + 8));
                } else {
                    gm_toe5fmr3vvf4d t27 = GM_LD(gm_toe5fmr3vvf4d, (((uint8_t *)&t25) + 8));
                    gm_l4 = t27;
                    {
                        gm_toe5fmr3vvf4d t29 = gm_l4;
                        gm_te2n4j2vtm53d t30;
                        GM_ST(uint8_t, (uint8_t *)&t30 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t30 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t30 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t30 + 16, 0);
                        GM_ST(uint64_t, (uint8_t *)&t30 + 24, 0);
                        ((uint8_t *)&t30)[0] = 1;
                        GM_ST(gm_toe5fmr3vvf4d, ((uint8_t *)&t30 + 8), t29);
                        gm_node *pn31 = gm_find(cx, 2076700068);
                        if (!pn31) gm_trap("no handler installed for effect `Yield`", gm_srcdo1hnokt0mq8, 179, 13);
                        gm_b0_1 t32;
                        t32 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, const void *))pn31->clauses[0])(pn31->env, pn31->hctx, pn31, &t30);
                        if (gm_unw.kind) {
                            goto gm_ll19;
                        }
                    }
                    gm_b0_1 t33;
                    uint8_t t34 = 0;
                    F.ret = t33;
                    gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = t34;
                    goto gm_ll19;
                }
                gm_l5 = t26;
            }
            {
                gm_t11t9d8s5diq9 t35 = gm_l5;
                gm_t617vr72bd7b3 t36;
                if (((((uint8_t *)&t35))[16] != 0)) {
                    gm_t617vr72bd7b3 t37 = GM_LD(gm_t617vr72bd7b3, (((uint8_t *)&t35) + 0));
                    t36 = t37;
                } else {
                    goto lb18;
                }
                gm_l6 = t36;
            }
            {
                gm_tibupt4dmcqai t38;
                memset((uint8_t *)&t38 + 17, 0, 7);
                void * t39 = ((uint8_t *)&F.l3);
                uint64_t t40 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t41 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 0));
                uint64_t t42 = (uintptr_t)t41;
                if (t40 > t42 || t42 > (uintptr_t)256) gm_trap_slice(t40, t42, 256, gm_srcdo1hnokt0mq8, 183, 27);
                gm_b16_8 t43;
                GM_ST(void *, (uint8_t *)&t43, (void *)(((uint8_t *)t39 + t40 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t43 + 8, (uintptr_t)(t42 - t40));
                gm_b16_8 t44 = GM_LD(gm_b16_8, ((uint8_t *)&t43));
                GM_ST(gm_b16_8, (uint8_t *)&t38 + 0, t44);
                gm_t9qkh0rsooc8d t45 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l6) + 8));
                GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&t38 + 16, t45);
                gm_te2n4j2vtm53d t46;
                GM_ST(uint8_t, (uint8_t *)&t46 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t46 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t46 + 4, 0);
                ((uint8_t *)&t46)[0] = 0;
                GM_ST(gm_tibupt4dmcqai, ((uint8_t *)&t46 + 8), t38);
                gm_node *pn47 = gm_find(cx, 2076700068);
                if (!pn47) gm_trap("no handler installed for effect `Yield`", gm_srcdo1hnokt0mq8, 183, 9);
                gm_b0_1 t48;
                t48 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, const void *))pn47->clauses[0])(pn47->env, pn47->hctx, pn47, &t46);
                if (gm_unw.kind) {
                    goto gm_ll19;
                }
            }
lc18:;
        }
gm_ll19:;
        if (gm_unw.frame == &F.h && gm_unw.label == 0) {
            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb18; }
            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc18; }
        }
        goto gm_ld1_1;
lb18:;
    }
    {
        gm_tm98ap4112hee t49 = gm_l2;
        gm_node *pn50 = gm_find(cx, 2126215853);
        if (!pn50) gm_trap("no handler installed for effect `Fs`", gm_srcdo1hnokt0mq8, 174, 11);
        gm_b0_1 t51;
        t51 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, gm_tm98ap4112hee))pn50->clauses[12])(pn50->env, pn50->hctx, pn50, t49);
    }
    goto gm_ls52;
gm_ld1_1:;
    {
        gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
        gm_tm98ap4112hee t53 = gm_l2;
        gm_node *pn54 = gm_find(cx, 2126215853);
        if (!pn54) gm_trap("no handler installed for effect `Fs`", gm_srcdo1hnokt0mq8, 174, 11);
        gm_b0_1 t55;
        t55 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, gm_tm98ap4112hee))pn54->clauses[12])(pn54->env, pn54->hctx, pn54, t53);
        gm_unw = gm_saved;
    }
    goto gm_lad;
gm_ls52:;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_entries_44o5n9f_s2qjl18(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_entries_44o5n9f F;
    gm_b16_8 gm_l0; /* path */
    gm_toe5fmr3vvf4d gm_l1; /* e */
    gm_tm98ap4112hee gm_l2; /* d */
    gm_toe5fmr3vvf4d gm_l4; /* e */
    gm_t11t9d8s5diq9 gm_l5; /* next */
    gm_t617vr72bd7b3 gm_l6; /* ent */
    GM_STACK_CHECK(&F, 515u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_b16_8 t2 = gm_l0;
        gm_node *pn3 = gm_find(cx, 2126215853);
        if (!pn3) gm_trap("no handler installed for effect `Fs`", gm_srcdo1hnokt0mq8, 170, 10);
        gm_t62ebbi2i5rkd t4;
        t4 = ((gm_t62ebbi2i5rkd (*)(void *, gm_node *, gm_node *, gm_b16_8))pn3->clauses[10])(pn3->env, pn3->hctx, pn3, t2);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tm98ap4112hee t5;
        if (!((((uint8_t *)&t4))[0] != 0)) {
            gm_tm98ap4112hee t6 = GM_LD(gm_tm98ap4112hee, (((uint8_t *)&t4) + 8));
            t5 = t6;
        } else {
            gm_toe5fmr3vvf4d t7 = GM_LD(gm_toe5fmr3vvf4d, (((uint8_t *)&t4) + 8));
            gm_l1 = t7;
            {
                gm_toe5fmr3vvf4d t9 = gm_l1;
                gm_te2n4j2vtm53d t10;
                GM_ST(uint8_t, (uint8_t *)&t10 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t10 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t10 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t10 + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&t10 + 24, 0);
                ((uint8_t *)&t10)[0] = 1;
                GM_ST(gm_toe5fmr3vvf4d, ((uint8_t *)&t10 + 8), t9);
                gm_b0_1 t11 = gm_f_for_body_iv0iced(cx->env, cx->hctx, cx, &t10);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b0_1 t12;
            uint8_t t13 = 0;
            F.ret = t12;
            goto gm_fret;
        }
        gm_l2 = t5;
    }
    {
    }
    {
    }
    {
        for (;;) {
            if (!((uint8_t)1)) goto lb17;
            {
                gm_tm98ap4112hee t20 = gm_l2;
                void * t21 = ((uint8_t *)&F.l3);
                if ((uintptr_t)0 > (uintptr_t)256 || (uintptr_t)256 > (uintptr_t)256) gm_trap_slice((uintptr_t)0, (uintptr_t)256, 256, gm_srcdo1hnokt0mq8, 178, 30);
                gm_b16_8 t22;
                GM_ST(void *, (uint8_t *)&t22, (void *)(((uint8_t *)t21 + (uintptr_t)0 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t22 + 8, (uintptr_t)((uintptr_t)256 - (uintptr_t)0));
                gm_node *pn23 = gm_find(cx, 2126215853);
                if (!pn23) gm_trap("no handler installed for effect `Fs`", gm_srcdo1hnokt0mq8, 178, 17);
                gm_tiuq7k5hm0kak t24;
                ((void (*)(void *, void *, gm_node *, gm_node *, gm_tm98ap4112hee, gm_b16_8))pn23->clauses[11])((void *)&t24, pn23->env, pn23->hctx, pn23, t20, t22);
                if (gm_unw.kind) {
                    goto gm_ll18;
                }
                gm_t11t9d8s5diq9 t25;
                if (!((((uint8_t *)&t24))[0] != 0)) {
                    t25 = (*(gm_t11t9d8s5diq9 *)(((uint8_t *)&t24) + 8));
                } else {
                    gm_toe5fmr3vvf4d t26 = GM_LD(gm_toe5fmr3vvf4d, (((uint8_t *)&t24) + 8));
                    gm_l4 = t26;
                    {
                        gm_toe5fmr3vvf4d t28 = gm_l4;
                        gm_te2n4j2vtm53d t29;
                        GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
                        GM_ST(uint64_t, (uint8_t *)&t29 + 24, 0);
                        ((uint8_t *)&t29)[0] = 1;
                        GM_ST(gm_toe5fmr3vvf4d, ((uint8_t *)&t29 + 8), t28);
                        gm_b0_1 t30 = gm_f_for_body_iv0iced(cx->env, cx->hctx, cx, &t29);
                        if (gm_unw.kind) {
                            goto gm_ll18;
                        }
                    }
                    gm_b0_1 t31;
                    uint8_t t32 = 0;
                    F.ret = t31;
                    gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = t32;
                    goto gm_ll18;
                }
                gm_l5 = t25;
            }
            {
                gm_t11t9d8s5diq9 t33 = gm_l5;
                gm_t617vr72bd7b3 t34;
                if (((((uint8_t *)&t33))[16] != 0)) {
                    gm_t617vr72bd7b3 t35 = GM_LD(gm_t617vr72bd7b3, (((uint8_t *)&t33) + 0));
                    t34 = t35;
                } else {
                    goto lb17;
                }
                gm_l6 = t34;
            }
            {
                gm_tibupt4dmcqai t36;
                memset((uint8_t *)&t36 + 17, 0, 7);
                void * t37 = ((uint8_t *)&F.l3);
                uint64_t t38 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t39 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 0));
                uint64_t t40 = (uintptr_t)t39;
                if (t38 > t40 || t40 > (uintptr_t)256) gm_trap_slice(t38, t40, 256, gm_srcdo1hnokt0mq8, 183, 27);
                gm_b16_8 t41;
                GM_ST(void *, (uint8_t *)&t41, (void *)(((uint8_t *)t37 + t38 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t41 + 8, (uintptr_t)(t40 - t38));
                gm_b16_8 t42 = GM_LD(gm_b16_8, ((uint8_t *)&t41));
                GM_ST(gm_b16_8, (uint8_t *)&t36 + 0, t42);
                gm_t9qkh0rsooc8d t43 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l6) + 8));
                GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&t36 + 16, t43);
                gm_te2n4j2vtm53d t44;
                GM_ST(uint8_t, (uint8_t *)&t44 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t44 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t44 + 4, 0);
                ((uint8_t *)&t44)[0] = 0;
                GM_ST(gm_tibupt4dmcqai, ((uint8_t *)&t44 + 8), t36);
                gm_b0_1 t45 = gm_f_for_body_iv0iced(cx->env, cx->hctx, cx, &t44);
                if (gm_unw.kind) {
                    goto gm_ll18;
                }
            }
lc17:;
        }
gm_ll18:;
        if (gm_unw.frame == &F.h && gm_unw.label == 0) {
            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb17; }
            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc17; }
        }
        goto gm_ld1_1;
lb17:;
    }
    {
        gm_tm98ap4112hee t46 = gm_l2;
        gm_node *pn47 = gm_find(cx, 2126215853);
        if (!pn47) gm_trap("no handler installed for effect `Fs`", gm_srcdo1hnokt0mq8, 174, 11);
        gm_b0_1 t48;
        t48 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, gm_tm98ap4112hee))pn47->clauses[12])(pn47->env, pn47->hctx, pn47, t46);
    }
    goto gm_ls49;
gm_ld1_1:;
    {
        gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
        gm_tm98ap4112hee t50 = gm_l2;
        gm_node *pn51 = gm_find(cx, 2126215853);
        if (!pn51) gm_trap("no handler installed for effect `Fs`", gm_srcdo1hnokt0mq8, 174, 11);
        gm_b0_1 t52;
        t52 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, gm_tm98ap4112hee))pn51->clauses[12])(pn51->env, pn51->hctx, pn51, t50);
        gm_unw = gm_saved;
    }
    goto gm_lad;
gm_ls49:;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_entries_44o5n9f_s40i17b(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_entries_44o5n9f F;
    gm_b16_8 gm_l0; /* path */
    gm_toe5fmr3vvf4d gm_l1; /* e */
    gm_tm98ap4112hee gm_l2; /* d */
    gm_toe5fmr3vvf4d gm_l4; /* e */
    gm_t11t9d8s5diq9 gm_l5; /* next */
    gm_t617vr72bd7b3 gm_l6; /* ent */
    GM_STACK_CHECK(&F, 515u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_b16_8 t2 = gm_l0;
        gm_node *pn3 = gm_find(cx, 2126215853);
        if (!pn3) gm_trap("no handler installed for effect `Fs`", gm_srcdo1hnokt0mq8, 170, 10);
        gm_t62ebbi2i5rkd t4;
        t4 = ((gm_t62ebbi2i5rkd (*)(void *, gm_node *, gm_node *, gm_b16_8))pn3->clauses[10])(pn3->env, pn3->hctx, pn3, t2);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tm98ap4112hee t5;
        if (!((((uint8_t *)&t4))[0] != 0)) {
            gm_tm98ap4112hee t6 = GM_LD(gm_tm98ap4112hee, (((uint8_t *)&t4) + 8));
            t5 = t6;
        } else {
            gm_toe5fmr3vvf4d t7 = GM_LD(gm_toe5fmr3vvf4d, (((uint8_t *)&t4) + 8));
            gm_l1 = t7;
            {
                gm_toe5fmr3vvf4d t9 = gm_l1;
                gm_te2n4j2vtm53d t10;
                GM_ST(uint8_t, (uint8_t *)&t10 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t10 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t10 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t10 + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&t10 + 24, 0);
                ((uint8_t *)&t10)[0] = 1;
                GM_ST(gm_toe5fmr3vvf4d, ((uint8_t *)&t10 + 8), t9);
                gm_b0_1 t11 = gm_f_for_body_nnvr1n3(cx->env, cx->hctx, cx, &t10);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b0_1 t12;
            uint8_t t13 = 0;
            F.ret = t12;
            goto gm_fret;
        }
        gm_l2 = t5;
    }
    {
    }
    {
    }
    {
        for (;;) {
            if (!((uint8_t)1)) goto lb17;
            {
                gm_tm98ap4112hee t20 = gm_l2;
                void * t21 = ((uint8_t *)&F.l3);
                if ((uintptr_t)0 > (uintptr_t)256 || (uintptr_t)256 > (uintptr_t)256) gm_trap_slice((uintptr_t)0, (uintptr_t)256, 256, gm_srcdo1hnokt0mq8, 178, 30);
                gm_b16_8 t22;
                GM_ST(void *, (uint8_t *)&t22, (void *)(((uint8_t *)t21 + (uintptr_t)0 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t22 + 8, (uintptr_t)((uintptr_t)256 - (uintptr_t)0));
                gm_node *pn23 = gm_find(cx, 2126215853);
                if (!pn23) gm_trap("no handler installed for effect `Fs`", gm_srcdo1hnokt0mq8, 178, 17);
                gm_tiuq7k5hm0kak t24;
                ((void (*)(void *, void *, gm_node *, gm_node *, gm_tm98ap4112hee, gm_b16_8))pn23->clauses[11])((void *)&t24, pn23->env, pn23->hctx, pn23, t20, t22);
                if (gm_unw.kind) {
                    goto gm_ll18;
                }
                gm_t11t9d8s5diq9 t25;
                if (!((((uint8_t *)&t24))[0] != 0)) {
                    t25 = (*(gm_t11t9d8s5diq9 *)(((uint8_t *)&t24) + 8));
                } else {
                    gm_toe5fmr3vvf4d t26 = GM_LD(gm_toe5fmr3vvf4d, (((uint8_t *)&t24) + 8));
                    gm_l4 = t26;
                    {
                        gm_toe5fmr3vvf4d t28 = gm_l4;
                        gm_te2n4j2vtm53d t29;
                        GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
                        GM_ST(uint64_t, (uint8_t *)&t29 + 24, 0);
                        ((uint8_t *)&t29)[0] = 1;
                        GM_ST(gm_toe5fmr3vvf4d, ((uint8_t *)&t29 + 8), t28);
                        gm_b0_1 t30 = gm_f_for_body_nnvr1n3(cx->env, cx->hctx, cx, &t29);
                        if (gm_unw.kind) {
                            goto gm_ll18;
                        }
                    }
                    gm_b0_1 t31;
                    uint8_t t32 = 0;
                    F.ret = t31;
                    gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = t32;
                    goto gm_ll18;
                }
                gm_l5 = t25;
            }
            {
                gm_t11t9d8s5diq9 t33 = gm_l5;
                gm_t617vr72bd7b3 t34;
                if (((((uint8_t *)&t33))[16] != 0)) {
                    gm_t617vr72bd7b3 t35 = GM_LD(gm_t617vr72bd7b3, (((uint8_t *)&t33) + 0));
                    t34 = t35;
                } else {
                    goto lb17;
                }
                gm_l6 = t34;
            }
            {
                gm_tibupt4dmcqai t36;
                memset((uint8_t *)&t36 + 17, 0, 7);
                void * t37 = ((uint8_t *)&F.l3);
                uint64_t t38 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t39 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 0));
                uint64_t t40 = (uintptr_t)t39;
                if (t38 > t40 || t40 > (uintptr_t)256) gm_trap_slice(t38, t40, 256, gm_srcdo1hnokt0mq8, 183, 27);
                gm_b16_8 t41;
                GM_ST(void *, (uint8_t *)&t41, (void *)(((uint8_t *)t37 + t38 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t41 + 8, (uintptr_t)(t40 - t38));
                gm_b16_8 t42 = GM_LD(gm_b16_8, ((uint8_t *)&t41));
                GM_ST(gm_b16_8, (uint8_t *)&t36 + 0, t42);
                gm_t9qkh0rsooc8d t43 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l6) + 8));
                GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&t36 + 16, t43);
                gm_te2n4j2vtm53d t44;
                GM_ST(uint8_t, (uint8_t *)&t44 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t44 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t44 + 4, 0);
                ((uint8_t *)&t44)[0] = 0;
                GM_ST(gm_tibupt4dmcqai, ((uint8_t *)&t44 + 8), t36);
                gm_b0_1 t45 = gm_f_for_body_nnvr1n3(cx->env, cx->hctx, cx, &t44);
                if (gm_unw.kind) {
                    goto gm_ll18;
                }
            }
lc17:;
        }
gm_ll18:;
        if (gm_unw.frame == &F.h && gm_unw.label == 0) {
            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb17; }
            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc17; }
        }
        goto gm_ld1_1;
lb17:;
    }
    {
        gm_tm98ap4112hee t46 = gm_l2;
        gm_node *pn47 = gm_find(cx, 2126215853);
        if (!pn47) gm_trap("no handler installed for effect `Fs`", gm_srcdo1hnokt0mq8, 174, 11);
        gm_b0_1 t48;
        t48 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, gm_tm98ap4112hee))pn47->clauses[12])(pn47->env, pn47->hctx, pn47, t46);
    }
    goto gm_ls49;
gm_ld1_1:;
    {
        gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
        gm_tm98ap4112hee t50 = gm_l2;
        gm_node *pn51 = gm_find(cx, 2126215853);
        if (!pn51) gm_trap("no handler installed for effect `Fs`", gm_srcdo1hnokt0mq8, 174, 11);
        gm_b0_1 t52;
        t52 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, gm_tm98ap4112hee))pn51->clauses[12])(pn51->env, pn51->hctx, pn51, t50);
        gm_unw = gm_saved;
    }
    goto gm_lad;
gm_ls49:;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_entries_44o5n9f_s58h1st(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_entries_44o5n9f F;
    gm_b16_8 gm_l0; /* path */
    gm_toe5fmr3vvf4d gm_l1; /* e */
    gm_tm98ap4112hee gm_l2; /* d */
    gm_toe5fmr3vvf4d gm_l4; /* e */
    gm_t11t9d8s5diq9 gm_l5; /* next */
    gm_t617vr72bd7b3 gm_l6; /* ent */
    GM_STACK_CHECK(&F, 515u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_b16_8 t2 = gm_l0;
        gm_node *pn3 = gm_find(cx, 2126215853);
        if (!pn3) gm_trap("no handler installed for effect `Fs`", gm_srcdo1hnokt0mq8, 170, 10);
        gm_t62ebbi2i5rkd t4;
        t4 = ((gm_t62ebbi2i5rkd (*)(void *, gm_node *, gm_node *, gm_b16_8))pn3->clauses[10])(pn3->env, pn3->hctx, pn3, t2);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tm98ap4112hee t5;
        if (!((((uint8_t *)&t4))[0] != 0)) {
            gm_tm98ap4112hee t6 = GM_LD(gm_tm98ap4112hee, (((uint8_t *)&t4) + 8));
            t5 = t6;
        } else {
            gm_toe5fmr3vvf4d t7 = GM_LD(gm_toe5fmr3vvf4d, (((uint8_t *)&t4) + 8));
            gm_l1 = t7;
            {
                gm_toe5fmr3vvf4d t9 = gm_l1;
                gm_te2n4j2vtm53d t10;
                GM_ST(uint8_t, (uint8_t *)&t10 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t10 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t10 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t10 + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&t10 + 24, 0);
                ((uint8_t *)&t10)[0] = 1;
                GM_ST(gm_toe5fmr3vvf4d, ((uint8_t *)&t10 + 8), t9);
                gm_b0_1 t11 = gm_f_for_body_86gje82(cx->env, cx->hctx, cx, &t10);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b0_1 t12;
            uint8_t t13 = 0;
            F.ret = t12;
            goto gm_fret;
        }
        gm_l2 = t5;
    }
    {
    }
    {
    }
    {
        for (;;) {
            if (!((uint8_t)1)) goto lb17;
            {
                gm_tm98ap4112hee t20 = gm_l2;
                void * t21 = ((uint8_t *)&F.l3);
                if ((uintptr_t)0 > (uintptr_t)256 || (uintptr_t)256 > (uintptr_t)256) gm_trap_slice((uintptr_t)0, (uintptr_t)256, 256, gm_srcdo1hnokt0mq8, 178, 30);
                gm_b16_8 t22;
                GM_ST(void *, (uint8_t *)&t22, (void *)(((uint8_t *)t21 + (uintptr_t)0 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t22 + 8, (uintptr_t)((uintptr_t)256 - (uintptr_t)0));
                gm_node *pn23 = gm_find(cx, 2126215853);
                if (!pn23) gm_trap("no handler installed for effect `Fs`", gm_srcdo1hnokt0mq8, 178, 17);
                gm_tiuq7k5hm0kak t24;
                ((void (*)(void *, void *, gm_node *, gm_node *, gm_tm98ap4112hee, gm_b16_8))pn23->clauses[11])((void *)&t24, pn23->env, pn23->hctx, pn23, t20, t22);
                if (gm_unw.kind) {
                    goto gm_ll18;
                }
                gm_t11t9d8s5diq9 t25;
                if (!((((uint8_t *)&t24))[0] != 0)) {
                    t25 = (*(gm_t11t9d8s5diq9 *)(((uint8_t *)&t24) + 8));
                } else {
                    gm_toe5fmr3vvf4d t26 = GM_LD(gm_toe5fmr3vvf4d, (((uint8_t *)&t24) + 8));
                    gm_l4 = t26;
                    {
                        gm_toe5fmr3vvf4d t28 = gm_l4;
                        gm_te2n4j2vtm53d t29;
                        GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
                        GM_ST(uint64_t, (uint8_t *)&t29 + 24, 0);
                        ((uint8_t *)&t29)[0] = 1;
                        GM_ST(gm_toe5fmr3vvf4d, ((uint8_t *)&t29 + 8), t28);
                        gm_b0_1 t30 = gm_f_for_body_86gje82(cx->env, cx->hctx, cx, &t29);
                        if (gm_unw.kind) {
                            goto gm_ll18;
                        }
                    }
                    gm_b0_1 t31;
                    uint8_t t32 = 0;
                    F.ret = t31;
                    gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = t32;
                    goto gm_ll18;
                }
                gm_l5 = t25;
            }
            {
                gm_t11t9d8s5diq9 t33 = gm_l5;
                gm_t617vr72bd7b3 t34;
                if (((((uint8_t *)&t33))[16] != 0)) {
                    gm_t617vr72bd7b3 t35 = GM_LD(gm_t617vr72bd7b3, (((uint8_t *)&t33) + 0));
                    t34 = t35;
                } else {
                    goto lb17;
                }
                gm_l6 = t34;
            }
            {
                gm_tibupt4dmcqai t36;
                memset((uint8_t *)&t36 + 17, 0, 7);
                void * t37 = ((uint8_t *)&F.l3);
                uint64_t t38 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t39 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 0));
                uint64_t t40 = (uintptr_t)t39;
                if (t38 > t40 || t40 > (uintptr_t)256) gm_trap_slice(t38, t40, 256, gm_srcdo1hnokt0mq8, 183, 27);
                gm_b16_8 t41;
                GM_ST(void *, (uint8_t *)&t41, (void *)(((uint8_t *)t37 + t38 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t41 + 8, (uintptr_t)(t40 - t38));
                gm_b16_8 t42 = GM_LD(gm_b16_8, ((uint8_t *)&t41));
                GM_ST(gm_b16_8, (uint8_t *)&t36 + 0, t42);
                gm_t9qkh0rsooc8d t43 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l6) + 8));
                GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&t36 + 16, t43);
                gm_te2n4j2vtm53d t44;
                GM_ST(uint8_t, (uint8_t *)&t44 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t44 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t44 + 4, 0);
                ((uint8_t *)&t44)[0] = 0;
                GM_ST(gm_tibupt4dmcqai, ((uint8_t *)&t44 + 8), t36);
                gm_b0_1 t45 = gm_f_for_body_86gje82(cx->env, cx->hctx, cx, &t44);
                if (gm_unw.kind) {
                    goto gm_ll18;
                }
            }
lc17:;
        }
gm_ll18:;
        if (gm_unw.frame == &F.h && gm_unw.label == 0) {
            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb17; }
            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc17; }
        }
        goto gm_ld1_1;
lb17:;
    }
    {
        gm_tm98ap4112hee t46 = gm_l2;
        gm_node *pn47 = gm_find(cx, 2126215853);
        if (!pn47) gm_trap("no handler installed for effect `Fs`", gm_srcdo1hnokt0mq8, 174, 11);
        gm_b0_1 t48;
        t48 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, gm_tm98ap4112hee))pn47->clauses[12])(pn47->env, pn47->hctx, pn47, t46);
    }
    goto gm_ls49;
gm_ld1_1:;
    {
        gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
        gm_tm98ap4112hee t50 = gm_l2;
        gm_node *pn51 = gm_find(cx, 2126215853);
        if (!pn51) gm_trap("no handler installed for effect `Fs`", gm_srcdo1hnokt0mq8, 174, 11);
        gm_b0_1 t52;
        t52 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, gm_tm98ap4112hee))pn51->clauses[12])(pn51->env, pn51->hctx, pn51, t50);
        gm_unw = gm_saved;
    }
    goto gm_lad;
gm_ls49:;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_entries_44o5n9f_sbacjg5(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_entries_44o5n9f F;
    gm_b16_8 gm_l0; /* path */
    gm_toe5fmr3vvf4d gm_l1; /* e */
    gm_tm98ap4112hee gm_l2; /* d */
    gm_toe5fmr3vvf4d gm_l4; /* e */
    gm_t11t9d8s5diq9 gm_l5; /* next */
    gm_t617vr72bd7b3 gm_l6; /* ent */
    GM_STACK_CHECK(&F, 515u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_b16_8 t2 = gm_l0;
        gm_node *pn3 = gm_find(cx, 2126215853);
        if (!pn3) gm_trap("no handler installed for effect `Fs`", gm_srcdo1hnokt0mq8, 170, 10);
        gm_t62ebbi2i5rkd t4;
        t4 = ((gm_t62ebbi2i5rkd (*)(void *, gm_node *, gm_node *, gm_b16_8))pn3->clauses[10])(pn3->env, pn3->hctx, pn3, t2);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tm98ap4112hee t5;
        if (!((((uint8_t *)&t4))[0] != 0)) {
            gm_tm98ap4112hee t6 = GM_LD(gm_tm98ap4112hee, (((uint8_t *)&t4) + 8));
            t5 = t6;
        } else {
            gm_toe5fmr3vvf4d t7 = GM_LD(gm_toe5fmr3vvf4d, (((uint8_t *)&t4) + 8));
            gm_l1 = t7;
            {
                gm_toe5fmr3vvf4d t9 = gm_l1;
                gm_te2n4j2vtm53d t10;
                GM_ST(uint8_t, (uint8_t *)&t10 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t10 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t10 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t10 + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&t10 + 24, 0);
                ((uint8_t *)&t10)[0] = 1;
                GM_ST(gm_toe5fmr3vvf4d, ((uint8_t *)&t10 + 8), t9);
                gm_b0_1 t11 = gm_f_for_body_fnpff4s(cx->env, cx->hctx, cx, &t10);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b0_1 t12;
            uint8_t t13 = 0;
            F.ret = t12;
            goto gm_fret;
        }
        gm_l2 = t5;
    }
    {
    }
    {
    }
    {
        for (;;) {
            if (!((uint8_t)1)) goto lb17;
            {
                gm_tm98ap4112hee t20 = gm_l2;
                void * t21 = ((uint8_t *)&F.l3);
                if ((uintptr_t)0 > (uintptr_t)256 || (uintptr_t)256 > (uintptr_t)256) gm_trap_slice((uintptr_t)0, (uintptr_t)256, 256, gm_srcdo1hnokt0mq8, 178, 30);
                gm_b16_8 t22;
                GM_ST(void *, (uint8_t *)&t22, (void *)(((uint8_t *)t21 + (uintptr_t)0 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t22 + 8, (uintptr_t)((uintptr_t)256 - (uintptr_t)0));
                gm_node *pn23 = gm_find(cx, 2126215853);
                if (!pn23) gm_trap("no handler installed for effect `Fs`", gm_srcdo1hnokt0mq8, 178, 17);
                gm_tiuq7k5hm0kak t24;
                ((void (*)(void *, void *, gm_node *, gm_node *, gm_tm98ap4112hee, gm_b16_8))pn23->clauses[11])((void *)&t24, pn23->env, pn23->hctx, pn23, t20, t22);
                if (gm_unw.kind) {
                    goto gm_ll18;
                }
                gm_t11t9d8s5diq9 t25;
                if (!((((uint8_t *)&t24))[0] != 0)) {
                    t25 = (*(gm_t11t9d8s5diq9 *)(((uint8_t *)&t24) + 8));
                } else {
                    gm_toe5fmr3vvf4d t26 = GM_LD(gm_toe5fmr3vvf4d, (((uint8_t *)&t24) + 8));
                    gm_l4 = t26;
                    {
                        gm_toe5fmr3vvf4d t28 = gm_l4;
                        gm_te2n4j2vtm53d t29;
                        GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
                        GM_ST(uint64_t, (uint8_t *)&t29 + 24, 0);
                        ((uint8_t *)&t29)[0] = 1;
                        GM_ST(gm_toe5fmr3vvf4d, ((uint8_t *)&t29 + 8), t28);
                        gm_b0_1 t30 = gm_f_for_body_fnpff4s(cx->env, cx->hctx, cx, &t29);
                        if (gm_unw.kind) {
                            goto gm_ll18;
                        }
                    }
                    gm_b0_1 t31;
                    uint8_t t32 = 0;
                    F.ret = t31;
                    gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = t32;
                    goto gm_ll18;
                }
                gm_l5 = t25;
            }
            {
                gm_t11t9d8s5diq9 t33 = gm_l5;
                gm_t617vr72bd7b3 t34;
                if (((((uint8_t *)&t33))[16] != 0)) {
                    gm_t617vr72bd7b3 t35 = GM_LD(gm_t617vr72bd7b3, (((uint8_t *)&t33) + 0));
                    t34 = t35;
                } else {
                    goto lb17;
                }
                gm_l6 = t34;
            }
            {
                gm_tibupt4dmcqai t36;
                memset((uint8_t *)&t36 + 17, 0, 7);
                void * t37 = ((uint8_t *)&F.l3);
                uint64_t t38 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t39 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 0));
                uint64_t t40 = (uintptr_t)t39;
                if (t38 > t40 || t40 > (uintptr_t)256) gm_trap_slice(t38, t40, 256, gm_srcdo1hnokt0mq8, 183, 27);
                gm_b16_8 t41;
                GM_ST(void *, (uint8_t *)&t41, (void *)(((uint8_t *)t37 + t38 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t41 + 8, (uintptr_t)(t40 - t38));
                gm_b16_8 t42 = GM_LD(gm_b16_8, ((uint8_t *)&t41));
                GM_ST(gm_b16_8, (uint8_t *)&t36 + 0, t42);
                gm_t9qkh0rsooc8d t43 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l6) + 8));
                GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&t36 + 16, t43);
                gm_te2n4j2vtm53d t44;
                GM_ST(uint8_t, (uint8_t *)&t44 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t44 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t44 + 4, 0);
                ((uint8_t *)&t44)[0] = 0;
                GM_ST(gm_tibupt4dmcqai, ((uint8_t *)&t44 + 8), t36);
                gm_b0_1 t45 = gm_f_for_body_fnpff4s(cx->env, cx->hctx, cx, &t44);
                if (gm_unw.kind) {
                    goto gm_ll18;
                }
            }
lc17:;
        }
gm_ll18:;
        if (gm_unw.frame == &F.h && gm_unw.label == 0) {
            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb17; }
            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc17; }
        }
        goto gm_ld1_1;
lb17:;
    }
    {
        gm_tm98ap4112hee t46 = gm_l2;
        gm_node *pn47 = gm_find(cx, 2126215853);
        if (!pn47) gm_trap("no handler installed for effect `Fs`", gm_srcdo1hnokt0mq8, 174, 11);
        gm_b0_1 t48;
        t48 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, gm_tm98ap4112hee))pn47->clauses[12])(pn47->env, pn47->hctx, pn47, t46);
    }
    goto gm_ls49;
gm_ld1_1:;
    {
        gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
        gm_tm98ap4112hee t50 = gm_l2;
        gm_node *pn51 = gm_find(cx, 2126215853);
        if (!pn51) gm_trap("no handler installed for effect `Fs`", gm_srcdo1hnokt0mq8, 174, 11);
        gm_b0_1 t52;
        t52 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, gm_tm98ap4112hee))pn51->clauses[12])(pn51->env, pn51->hctx, pn51, t50);
        gm_unw = gm_saved;
    }
    goto gm_lad;
gm_ls49:;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_entries_44o5n9f_sh4kvg0(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_entries_44o5n9f F;
    gm_b16_8 gm_l0; /* path */
    gm_toe5fmr3vvf4d gm_l1; /* e */
    gm_tm98ap4112hee gm_l2; /* d */
    gm_toe5fmr3vvf4d gm_l4; /* e */
    gm_t11t9d8s5diq9 gm_l5; /* next */
    gm_t617vr72bd7b3 gm_l6; /* ent */
    GM_STACK_CHECK(&F, 515u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_b16_8 t2 = gm_l0;
        gm_node *pn3 = gm_find(cx, 2126215853);
        if (!pn3) gm_trap("no handler installed for effect `Fs`", gm_srcdo1hnokt0mq8, 170, 10);
        gm_t62ebbi2i5rkd t4;
        t4 = ((gm_t62ebbi2i5rkd (*)(void *, gm_node *, gm_node *, gm_b16_8))pn3->clauses[10])(pn3->env, pn3->hctx, pn3, t2);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tm98ap4112hee t5;
        if (!((((uint8_t *)&t4))[0] != 0)) {
            gm_tm98ap4112hee t6 = GM_LD(gm_tm98ap4112hee, (((uint8_t *)&t4) + 8));
            t5 = t6;
        } else {
            gm_toe5fmr3vvf4d t7 = GM_LD(gm_toe5fmr3vvf4d, (((uint8_t *)&t4) + 8));
            gm_l1 = t7;
            {
                gm_toe5fmr3vvf4d t9 = gm_l1;
                gm_te2n4j2vtm53d t10;
                GM_ST(uint8_t, (uint8_t *)&t10 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t10 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t10 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t10 + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&t10 + 24, 0);
                ((uint8_t *)&t10)[0] = 1;
                GM_ST(gm_toe5fmr3vvf4d, ((uint8_t *)&t10 + 8), t9);
                gm_b0_1 t11 = gm_f_for_body_1b63vnj(cx->env, cx->hctx, cx, &t10);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b0_1 t12;
            uint8_t t13 = 0;
            F.ret = t12;
            goto gm_fret;
        }
        gm_l2 = t5;
    }
    {
    }
    {
    }
    {
        for (;;) {
            if (!((uint8_t)1)) goto lb17;
            {
                gm_tm98ap4112hee t20 = gm_l2;
                void * t21 = ((uint8_t *)&F.l3);
                if ((uintptr_t)0 > (uintptr_t)256 || (uintptr_t)256 > (uintptr_t)256) gm_trap_slice((uintptr_t)0, (uintptr_t)256, 256, gm_srcdo1hnokt0mq8, 178, 30);
                gm_b16_8 t22;
                GM_ST(void *, (uint8_t *)&t22, (void *)(((uint8_t *)t21 + (uintptr_t)0 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t22 + 8, (uintptr_t)((uintptr_t)256 - (uintptr_t)0));
                gm_node *pn23 = gm_find(cx, 2126215853);
                if (!pn23) gm_trap("no handler installed for effect `Fs`", gm_srcdo1hnokt0mq8, 178, 17);
                gm_tiuq7k5hm0kak t24;
                ((void (*)(void *, void *, gm_node *, gm_node *, gm_tm98ap4112hee, gm_b16_8))pn23->clauses[11])((void *)&t24, pn23->env, pn23->hctx, pn23, t20, t22);
                if (gm_unw.kind) {
                    goto gm_ll18;
                }
                gm_t11t9d8s5diq9 t25;
                if (!((((uint8_t *)&t24))[0] != 0)) {
                    t25 = (*(gm_t11t9d8s5diq9 *)(((uint8_t *)&t24) + 8));
                } else {
                    gm_toe5fmr3vvf4d t26 = GM_LD(gm_toe5fmr3vvf4d, (((uint8_t *)&t24) + 8));
                    gm_l4 = t26;
                    {
                        gm_toe5fmr3vvf4d t28 = gm_l4;
                        gm_te2n4j2vtm53d t29;
                        GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
                        GM_ST(uint64_t, (uint8_t *)&t29 + 24, 0);
                        ((uint8_t *)&t29)[0] = 1;
                        GM_ST(gm_toe5fmr3vvf4d, ((uint8_t *)&t29 + 8), t28);
                        gm_b0_1 t30 = gm_f_for_body_1b63vnj(cx->env, cx->hctx, cx, &t29);
                        if (gm_unw.kind) {
                            goto gm_ll18;
                        }
                    }
                    gm_b0_1 t31;
                    uint8_t t32 = 0;
                    F.ret = t31;
                    gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = t32;
                    goto gm_ll18;
                }
                gm_l5 = t25;
            }
            {
                gm_t11t9d8s5diq9 t33 = gm_l5;
                gm_t617vr72bd7b3 t34;
                if (((((uint8_t *)&t33))[16] != 0)) {
                    gm_t617vr72bd7b3 t35 = GM_LD(gm_t617vr72bd7b3, (((uint8_t *)&t33) + 0));
                    t34 = t35;
                } else {
                    goto lb17;
                }
                gm_l6 = t34;
            }
            {
                gm_tibupt4dmcqai t36;
                memset((uint8_t *)&t36 + 17, 0, 7);
                void * t37 = ((uint8_t *)&F.l3);
                uint64_t t38 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t39 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 0));
                uint64_t t40 = (uintptr_t)t39;
                if (t38 > t40 || t40 > (uintptr_t)256) gm_trap_slice(t38, t40, 256, gm_srcdo1hnokt0mq8, 183, 27);
                gm_b16_8 t41;
                GM_ST(void *, (uint8_t *)&t41, (void *)(((uint8_t *)t37 + t38 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t41 + 8, (uintptr_t)(t40 - t38));
                gm_b16_8 t42 = GM_LD(gm_b16_8, ((uint8_t *)&t41));
                GM_ST(gm_b16_8, (uint8_t *)&t36 + 0, t42);
                gm_t9qkh0rsooc8d t43 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l6) + 8));
                GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&t36 + 16, t43);
                gm_te2n4j2vtm53d t44;
                GM_ST(uint8_t, (uint8_t *)&t44 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t44 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t44 + 4, 0);
                ((uint8_t *)&t44)[0] = 0;
                GM_ST(gm_tibupt4dmcqai, ((uint8_t *)&t44 + 8), t36);
                gm_b0_1 t45 = gm_f_for_body_1b63vnj(cx->env, cx->hctx, cx, &t44);
                if (gm_unw.kind) {
                    goto gm_ll18;
                }
            }
lc17:;
        }
gm_ll18:;
        if (gm_unw.frame == &F.h && gm_unw.label == 0) {
            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb17; }
            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc17; }
        }
        goto gm_ld1_1;
lb17:;
    }
    {
        gm_tm98ap4112hee t46 = gm_l2;
        gm_node *pn47 = gm_find(cx, 2126215853);
        if (!pn47) gm_trap("no handler installed for effect `Fs`", gm_srcdo1hnokt0mq8, 174, 11);
        gm_b0_1 t48;
        t48 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, gm_tm98ap4112hee))pn47->clauses[12])(pn47->env, pn47->hctx, pn47, t46);
    }
    goto gm_ls49;
gm_ld1_1:;
    {
        gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
        gm_tm98ap4112hee t50 = gm_l2;
        gm_node *pn51 = gm_find(cx, 2126215853);
        if (!pn51) gm_trap("no handler installed for effect `Fs`", gm_srcdo1hnokt0mq8, 174, 11);
        gm_b0_1 t52;
        t52 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, gm_tm98ap4112hee))pn51->clauses[12])(pn51->env, pn51->hctx, pn51, t50);
        gm_unw = gm_saved;
    }
    goto gm_lad;
gm_ls49:;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_entries_44o5n9f_siirbaa(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_entries_44o5n9f F;
    gm_b16_8 gm_l0; /* path */
    gm_toe5fmr3vvf4d gm_l1; /* e */
    gm_tm98ap4112hee gm_l2; /* d */
    gm_toe5fmr3vvf4d gm_l4; /* e */
    gm_t11t9d8s5diq9 gm_l5; /* next */
    gm_t617vr72bd7b3 gm_l6; /* ent */
    GM_STACK_CHECK(&F, 515u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_b16_8 t2 = gm_l0;
        gm_node *pn3 = gm_find(cx, 2126215853);
        if (!pn3) gm_trap("no handler installed for effect `Fs`", gm_srcdo1hnokt0mq8, 170, 10);
        gm_t62ebbi2i5rkd t4;
        t4 = ((gm_t62ebbi2i5rkd (*)(void *, gm_node *, gm_node *, gm_b16_8))pn3->clauses[10])(pn3->env, pn3->hctx, pn3, t2);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tm98ap4112hee t5;
        if (!((((uint8_t *)&t4))[0] != 0)) {
            gm_tm98ap4112hee t6 = GM_LD(gm_tm98ap4112hee, (((uint8_t *)&t4) + 8));
            t5 = t6;
        } else {
            gm_toe5fmr3vvf4d t7 = GM_LD(gm_toe5fmr3vvf4d, (((uint8_t *)&t4) + 8));
            gm_l1 = t7;
            {
                gm_toe5fmr3vvf4d t9 = gm_l1;
                gm_te2n4j2vtm53d t10;
                GM_ST(uint8_t, (uint8_t *)&t10 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t10 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t10 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t10 + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&t10 + 24, 0);
                ((uint8_t *)&t10)[0] = 1;
                GM_ST(gm_toe5fmr3vvf4d, ((uint8_t *)&t10 + 8), t9);
                gm_b0_1 t11 = gm_f_for_body_bq6tmqb(cx->env, cx->hctx, cx, &t10);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b0_1 t12;
            uint8_t t13 = 0;
            F.ret = t12;
            goto gm_fret;
        }
        gm_l2 = t5;
    }
    {
    }
    {
    }
    {
        for (;;) {
            if (!((uint8_t)1)) goto lb17;
            {
                gm_tm98ap4112hee t20 = gm_l2;
                void * t21 = ((uint8_t *)&F.l3);
                if ((uintptr_t)0 > (uintptr_t)256 || (uintptr_t)256 > (uintptr_t)256) gm_trap_slice((uintptr_t)0, (uintptr_t)256, 256, gm_srcdo1hnokt0mq8, 178, 30);
                gm_b16_8 t22;
                GM_ST(void *, (uint8_t *)&t22, (void *)(((uint8_t *)t21 + (uintptr_t)0 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t22 + 8, (uintptr_t)((uintptr_t)256 - (uintptr_t)0));
                gm_node *pn23 = gm_find(cx, 2126215853);
                if (!pn23) gm_trap("no handler installed for effect `Fs`", gm_srcdo1hnokt0mq8, 178, 17);
                gm_tiuq7k5hm0kak t24;
                ((void (*)(void *, void *, gm_node *, gm_node *, gm_tm98ap4112hee, gm_b16_8))pn23->clauses[11])((void *)&t24, pn23->env, pn23->hctx, pn23, t20, t22);
                if (gm_unw.kind) {
                    goto gm_ll18;
                }
                gm_t11t9d8s5diq9 t25;
                if (!((((uint8_t *)&t24))[0] != 0)) {
                    t25 = (*(gm_t11t9d8s5diq9 *)(((uint8_t *)&t24) + 8));
                } else {
                    gm_toe5fmr3vvf4d t26 = GM_LD(gm_toe5fmr3vvf4d, (((uint8_t *)&t24) + 8));
                    gm_l4 = t26;
                    {
                        gm_toe5fmr3vvf4d t28 = gm_l4;
                        gm_te2n4j2vtm53d t29;
                        GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
                        GM_ST(uint64_t, (uint8_t *)&t29 + 24, 0);
                        ((uint8_t *)&t29)[0] = 1;
                        GM_ST(gm_toe5fmr3vvf4d, ((uint8_t *)&t29 + 8), t28);
                        gm_b0_1 t30 = gm_f_for_body_bq6tmqb(cx->env, cx->hctx, cx, &t29);
                        if (gm_unw.kind) {
                            goto gm_ll18;
                        }
                    }
                    gm_b0_1 t31;
                    uint8_t t32 = 0;
                    F.ret = t31;
                    gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = t32;
                    goto gm_ll18;
                }
                gm_l5 = t25;
            }
            {
                gm_t11t9d8s5diq9 t33 = gm_l5;
                gm_t617vr72bd7b3 t34;
                if (((((uint8_t *)&t33))[16] != 0)) {
                    gm_t617vr72bd7b3 t35 = GM_LD(gm_t617vr72bd7b3, (((uint8_t *)&t33) + 0));
                    t34 = t35;
                } else {
                    goto lb17;
                }
                gm_l6 = t34;
            }
            {
                gm_tibupt4dmcqai t36;
                memset((uint8_t *)&t36 + 17, 0, 7);
                void * t37 = ((uint8_t *)&F.l3);
                uint64_t t38 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t39 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 0));
                uint64_t t40 = (uintptr_t)t39;
                if (t38 > t40 || t40 > (uintptr_t)256) gm_trap_slice(t38, t40, 256, gm_srcdo1hnokt0mq8, 183, 27);
                gm_b16_8 t41;
                GM_ST(void *, (uint8_t *)&t41, (void *)(((uint8_t *)t37 + t38 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t41 + 8, (uintptr_t)(t40 - t38));
                gm_b16_8 t42 = GM_LD(gm_b16_8, ((uint8_t *)&t41));
                GM_ST(gm_b16_8, (uint8_t *)&t36 + 0, t42);
                gm_t9qkh0rsooc8d t43 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l6) + 8));
                GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&t36 + 16, t43);
                gm_te2n4j2vtm53d t44;
                GM_ST(uint8_t, (uint8_t *)&t44 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t44 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t44 + 4, 0);
                ((uint8_t *)&t44)[0] = 0;
                GM_ST(gm_tibupt4dmcqai, ((uint8_t *)&t44 + 8), t36);
                gm_b0_1 t45 = gm_f_for_body_bq6tmqb(cx->env, cx->hctx, cx, &t44);
                if (gm_unw.kind) {
                    goto gm_ll18;
                }
            }
lc17:;
        }
gm_ll18:;
        if (gm_unw.frame == &F.h && gm_unw.label == 0) {
            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb17; }
            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc17; }
        }
        goto gm_ld1_1;
lb17:;
    }
    {
        gm_tm98ap4112hee t46 = gm_l2;
        gm_node *pn47 = gm_find(cx, 2126215853);
        if (!pn47) gm_trap("no handler installed for effect `Fs`", gm_srcdo1hnokt0mq8, 174, 11);
        gm_b0_1 t48;
        t48 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, gm_tm98ap4112hee))pn47->clauses[12])(pn47->env, pn47->hctx, pn47, t46);
    }
    goto gm_ls49;
gm_ld1_1:;
    {
        gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
        gm_tm98ap4112hee t50 = gm_l2;
        gm_node *pn51 = gm_find(cx, 2126215853);
        if (!pn51) gm_trap("no handler installed for effect `Fs`", gm_srcdo1hnokt0mq8, 174, 11);
        gm_b0_1 t52;
        t52 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, gm_tm98ap4112hee))pn51->clauses[12])(pn51->env, pn51->hctx, pn51, t50);
        gm_unw = gm_saved;
    }
    goto gm_lad;
gm_ls49:;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_entries_44o5n9f_skhefef(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_entries_44o5n9f F;
    gm_b16_8 gm_l0; /* path */
    gm_toe5fmr3vvf4d gm_l1; /* e */
    gm_tm98ap4112hee gm_l2; /* d */
    gm_toe5fmr3vvf4d gm_l4; /* e */
    gm_t11t9d8s5diq9 gm_l5; /* next */
    gm_t617vr72bd7b3 gm_l6; /* ent */
    GM_STACK_CHECK(&F, 515u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_b16_8 t2 = gm_l0;
        gm_node *pn3 = gm_find(cx, 2126215853);
        if (!pn3) gm_trap("no handler installed for effect `Fs`", gm_srcdo1hnokt0mq8, 170, 10);
        gm_t62ebbi2i5rkd t4;
        t4 = ((gm_t62ebbi2i5rkd (*)(void *, gm_node *, gm_node *, gm_b16_8))pn3->clauses[10])(pn3->env, pn3->hctx, pn3, t2);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tm98ap4112hee t5;
        if (!((((uint8_t *)&t4))[0] != 0)) {
            gm_tm98ap4112hee t6 = GM_LD(gm_tm98ap4112hee, (((uint8_t *)&t4) + 8));
            t5 = t6;
        } else {
            gm_toe5fmr3vvf4d t7 = GM_LD(gm_toe5fmr3vvf4d, (((uint8_t *)&t4) + 8));
            gm_l1 = t7;
            {
                gm_toe5fmr3vvf4d t9 = gm_l1;
                gm_te2n4j2vtm53d t10;
                GM_ST(uint8_t, (uint8_t *)&t10 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t10 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t10 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t10 + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&t10 + 24, 0);
                ((uint8_t *)&t10)[0] = 1;
                GM_ST(gm_toe5fmr3vvf4d, ((uint8_t *)&t10 + 8), t9);
                gm_b0_1 t11 = gm_f_for_body_ghum27l(cx->env, cx->hctx, cx, &t10);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b0_1 t12;
            uint8_t t13 = 0;
            F.ret = t12;
            goto gm_fret;
        }
        gm_l2 = t5;
    }
    {
    }
    {
    }
    {
        for (;;) {
            if (!((uint8_t)1)) goto lb17;
            {
                gm_tm98ap4112hee t20 = gm_l2;
                void * t21 = ((uint8_t *)&F.l3);
                if ((uintptr_t)0 > (uintptr_t)256 || (uintptr_t)256 > (uintptr_t)256) gm_trap_slice((uintptr_t)0, (uintptr_t)256, 256, gm_srcdo1hnokt0mq8, 178, 30);
                gm_b16_8 t22;
                GM_ST(void *, (uint8_t *)&t22, (void *)(((uint8_t *)t21 + (uintptr_t)0 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t22 + 8, (uintptr_t)((uintptr_t)256 - (uintptr_t)0));
                gm_node *pn23 = gm_find(cx, 2126215853);
                if (!pn23) gm_trap("no handler installed for effect `Fs`", gm_srcdo1hnokt0mq8, 178, 17);
                gm_tiuq7k5hm0kak t24;
                ((void (*)(void *, void *, gm_node *, gm_node *, gm_tm98ap4112hee, gm_b16_8))pn23->clauses[11])((void *)&t24, pn23->env, pn23->hctx, pn23, t20, t22);
                if (gm_unw.kind) {
                    goto gm_ll18;
                }
                gm_t11t9d8s5diq9 t25;
                if (!((((uint8_t *)&t24))[0] != 0)) {
                    t25 = (*(gm_t11t9d8s5diq9 *)(((uint8_t *)&t24) + 8));
                } else {
                    gm_toe5fmr3vvf4d t26 = GM_LD(gm_toe5fmr3vvf4d, (((uint8_t *)&t24) + 8));
                    gm_l4 = t26;
                    {
                        gm_toe5fmr3vvf4d t28 = gm_l4;
                        gm_te2n4j2vtm53d t29;
                        GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
                        GM_ST(uint64_t, (uint8_t *)&t29 + 24, 0);
                        ((uint8_t *)&t29)[0] = 1;
                        GM_ST(gm_toe5fmr3vvf4d, ((uint8_t *)&t29 + 8), t28);
                        gm_b0_1 t30 = gm_f_for_body_ghum27l(cx->env, cx->hctx, cx, &t29);
                        if (gm_unw.kind) {
                            goto gm_ll18;
                        }
                    }
                    gm_b0_1 t31;
                    uint8_t t32 = 0;
                    F.ret = t31;
                    gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = t32;
                    goto gm_ll18;
                }
                gm_l5 = t25;
            }
            {
                gm_t11t9d8s5diq9 t33 = gm_l5;
                gm_t617vr72bd7b3 t34;
                if (((((uint8_t *)&t33))[16] != 0)) {
                    gm_t617vr72bd7b3 t35 = GM_LD(gm_t617vr72bd7b3, (((uint8_t *)&t33) + 0));
                    t34 = t35;
                } else {
                    goto lb17;
                }
                gm_l6 = t34;
            }
            {
                gm_tibupt4dmcqai t36;
                memset((uint8_t *)&t36 + 17, 0, 7);
                void * t37 = ((uint8_t *)&F.l3);
                uint64_t t38 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t39 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 0));
                uint64_t t40 = (uintptr_t)t39;
                if (t38 > t40 || t40 > (uintptr_t)256) gm_trap_slice(t38, t40, 256, gm_srcdo1hnokt0mq8, 183, 27);
                gm_b16_8 t41;
                GM_ST(void *, (uint8_t *)&t41, (void *)(((uint8_t *)t37 + t38 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t41 + 8, (uintptr_t)(t40 - t38));
                gm_b16_8 t42 = GM_LD(gm_b16_8, ((uint8_t *)&t41));
                GM_ST(gm_b16_8, (uint8_t *)&t36 + 0, t42);
                gm_t9qkh0rsooc8d t43 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l6) + 8));
                GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&t36 + 16, t43);
                gm_te2n4j2vtm53d t44;
                GM_ST(uint8_t, (uint8_t *)&t44 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t44 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t44 + 4, 0);
                ((uint8_t *)&t44)[0] = 0;
                GM_ST(gm_tibupt4dmcqai, ((uint8_t *)&t44 + 8), t36);
                gm_b0_1 t45 = gm_f_for_body_ghum27l(cx->env, cx->hctx, cx, &t44);
                if (gm_unw.kind) {
                    goto gm_ll18;
                }
            }
lc17:;
        }
gm_ll18:;
        if (gm_unw.frame == &F.h && gm_unw.label == 0) {
            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb17; }
            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc17; }
        }
        goto gm_ld1_1;
lb17:;
    }
    {
        gm_tm98ap4112hee t46 = gm_l2;
        gm_node *pn47 = gm_find(cx, 2126215853);
        if (!pn47) gm_trap("no handler installed for effect `Fs`", gm_srcdo1hnokt0mq8, 174, 11);
        gm_b0_1 t48;
        t48 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, gm_tm98ap4112hee))pn47->clauses[12])(pn47->env, pn47->hctx, pn47, t46);
    }
    goto gm_ls49;
gm_ld1_1:;
    {
        gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
        gm_tm98ap4112hee t50 = gm_l2;
        gm_node *pn51 = gm_find(cx, 2126215853);
        if (!pn51) gm_trap("no handler installed for effect `Fs`", gm_srcdo1hnokt0mq8, 174, 11);
        gm_b0_1 t52;
        t52 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, gm_tm98ap4112hee))pn51->clauses[12])(pn51->env, pn51->hctx, pn51, t50);
        gm_unw = gm_saved;
    }
    goto gm_lad;
gm_ls49:;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_entries_44o5n9f_sr88c6d(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_entries_44o5n9f F;
    gm_b16_8 gm_l0; /* path */
    gm_toe5fmr3vvf4d gm_l1; /* e */
    gm_tm98ap4112hee gm_l2; /* d */
    gm_toe5fmr3vvf4d gm_l4; /* e */
    gm_t11t9d8s5diq9 gm_l5; /* next */
    gm_t617vr72bd7b3 gm_l6; /* ent */
    GM_STACK_CHECK(&F, 515u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_b16_8 t2 = gm_l0;
        gm_node *pn3 = gm_find(cx, 2126215853);
        if (!pn3) gm_trap("no handler installed for effect `Fs`", gm_srcdo1hnokt0mq8, 170, 10);
        gm_t62ebbi2i5rkd t4;
        t4 = ((gm_t62ebbi2i5rkd (*)(void *, gm_node *, gm_node *, gm_b16_8))pn3->clauses[10])(pn3->env, pn3->hctx, pn3, t2);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tm98ap4112hee t5;
        if (!((((uint8_t *)&t4))[0] != 0)) {
            gm_tm98ap4112hee t6 = GM_LD(gm_tm98ap4112hee, (((uint8_t *)&t4) + 8));
            t5 = t6;
        } else {
            gm_toe5fmr3vvf4d t7 = GM_LD(gm_toe5fmr3vvf4d, (((uint8_t *)&t4) + 8));
            gm_l1 = t7;
            {
                gm_toe5fmr3vvf4d t9 = gm_l1;
                gm_te2n4j2vtm53d t10;
                GM_ST(uint8_t, (uint8_t *)&t10 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t10 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t10 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t10 + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&t10 + 24, 0);
                ((uint8_t *)&t10)[0] = 1;
                GM_ST(gm_toe5fmr3vvf4d, ((uint8_t *)&t10 + 8), t9);
                gm_b0_1 t11 = gm_f_for_body_terksaa(cx->env, cx->hctx, cx, &t10);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b0_1 t12;
            uint8_t t13 = 0;
            F.ret = t12;
            goto gm_fret;
        }
        gm_l2 = t5;
    }
    {
    }
    {
    }
    {
        for (;;) {
            if (!((uint8_t)1)) goto lb17;
            {
                gm_tm98ap4112hee t20 = gm_l2;
                void * t21 = ((uint8_t *)&F.l3);
                if ((uintptr_t)0 > (uintptr_t)256 || (uintptr_t)256 > (uintptr_t)256) gm_trap_slice((uintptr_t)0, (uintptr_t)256, 256, gm_srcdo1hnokt0mq8, 178, 30);
                gm_b16_8 t22;
                GM_ST(void *, (uint8_t *)&t22, (void *)(((uint8_t *)t21 + (uintptr_t)0 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t22 + 8, (uintptr_t)((uintptr_t)256 - (uintptr_t)0));
                gm_node *pn23 = gm_find(cx, 2126215853);
                if (!pn23) gm_trap("no handler installed for effect `Fs`", gm_srcdo1hnokt0mq8, 178, 17);
                gm_tiuq7k5hm0kak t24;
                ((void (*)(void *, void *, gm_node *, gm_node *, gm_tm98ap4112hee, gm_b16_8))pn23->clauses[11])((void *)&t24, pn23->env, pn23->hctx, pn23, t20, t22);
                if (gm_unw.kind) {
                    goto gm_ll18;
                }
                gm_t11t9d8s5diq9 t25;
                if (!((((uint8_t *)&t24))[0] != 0)) {
                    t25 = (*(gm_t11t9d8s5diq9 *)(((uint8_t *)&t24) + 8));
                } else {
                    gm_toe5fmr3vvf4d t26 = GM_LD(gm_toe5fmr3vvf4d, (((uint8_t *)&t24) + 8));
                    gm_l4 = t26;
                    {
                        gm_toe5fmr3vvf4d t28 = gm_l4;
                        gm_te2n4j2vtm53d t29;
                        GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
                        GM_ST(uint64_t, (uint8_t *)&t29 + 24, 0);
                        ((uint8_t *)&t29)[0] = 1;
                        GM_ST(gm_toe5fmr3vvf4d, ((uint8_t *)&t29 + 8), t28);
                        gm_b0_1 t30 = gm_f_for_body_terksaa(cx->env, cx->hctx, cx, &t29);
                        if (gm_unw.kind) {
                            goto gm_ll18;
                        }
                    }
                    gm_b0_1 t31;
                    uint8_t t32 = 0;
                    F.ret = t31;
                    gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = t32;
                    goto gm_ll18;
                }
                gm_l5 = t25;
            }
            {
                gm_t11t9d8s5diq9 t33 = gm_l5;
                gm_t617vr72bd7b3 t34;
                if (((((uint8_t *)&t33))[16] != 0)) {
                    gm_t617vr72bd7b3 t35 = GM_LD(gm_t617vr72bd7b3, (((uint8_t *)&t33) + 0));
                    t34 = t35;
                } else {
                    goto lb17;
                }
                gm_l6 = t34;
            }
            {
                gm_tibupt4dmcqai t36;
                memset((uint8_t *)&t36 + 17, 0, 7);
                void * t37 = ((uint8_t *)&F.l3);
                uint64_t t38 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t39 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 0));
                uint64_t t40 = (uintptr_t)t39;
                if (t38 > t40 || t40 > (uintptr_t)256) gm_trap_slice(t38, t40, 256, gm_srcdo1hnokt0mq8, 183, 27);
                gm_b16_8 t41;
                GM_ST(void *, (uint8_t *)&t41, (void *)(((uint8_t *)t37 + t38 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t41 + 8, (uintptr_t)(t40 - t38));
                gm_b16_8 t42 = GM_LD(gm_b16_8, ((uint8_t *)&t41));
                GM_ST(gm_b16_8, (uint8_t *)&t36 + 0, t42);
                gm_t9qkh0rsooc8d t43 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l6) + 8));
                GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&t36 + 16, t43);
                gm_te2n4j2vtm53d t44;
                GM_ST(uint8_t, (uint8_t *)&t44 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t44 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t44 + 4, 0);
                ((uint8_t *)&t44)[0] = 0;
                GM_ST(gm_tibupt4dmcqai, ((uint8_t *)&t44 + 8), t36);
                gm_b0_1 t45 = gm_f_for_body_terksaa(cx->env, cx->hctx, cx, &t44);
                if (gm_unw.kind) {
                    goto gm_ll18;
                }
            }
lc17:;
        }
gm_ll18:;
        if (gm_unw.frame == &F.h && gm_unw.label == 0) {
            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb17; }
            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc17; }
        }
        goto gm_ld1_1;
lb17:;
    }
    {
        gm_tm98ap4112hee t46 = gm_l2;
        gm_node *pn47 = gm_find(cx, 2126215853);
        if (!pn47) gm_trap("no handler installed for effect `Fs`", gm_srcdo1hnokt0mq8, 174, 11);
        gm_b0_1 t48;
        t48 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, gm_tm98ap4112hee))pn47->clauses[12])(pn47->env, pn47->hctx, pn47, t46);
    }
    goto gm_ls49;
gm_ld1_1:;
    {
        gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
        gm_tm98ap4112hee t50 = gm_l2;
        gm_node *pn51 = gm_find(cx, 2126215853);
        if (!pn51) gm_trap("no handler installed for effect `Fs`", gm_srcdo1hnokt0mq8, 174, 11);
        gm_b0_1 t52;
        t52 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, gm_tm98ap4112hee))pn51->clauses[12])(pn51->env, pn51->hctx, pn51, t50);
        gm_unw = gm_saved;
    }
    goto gm_lad;
gm_ls49:;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_entries_44o5n9f_sre2b72(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_entries_44o5n9f F;
    gm_b16_8 gm_l0; /* path */
    gm_toe5fmr3vvf4d gm_l1; /* e */
    gm_tm98ap4112hee gm_l2; /* d */
    gm_toe5fmr3vvf4d gm_l4; /* e */
    gm_t11t9d8s5diq9 gm_l5; /* next */
    gm_t617vr72bd7b3 gm_l6; /* ent */
    GM_STACK_CHECK(&F, 515u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_b16_8 t2 = gm_l0;
        gm_node *pn3 = gm_find(cx, 2126215853);
        if (!pn3) gm_trap("no handler installed for effect `Fs`", gm_srcdo1hnokt0mq8, 170, 10);
        gm_t62ebbi2i5rkd t4;
        t4 = ((gm_t62ebbi2i5rkd (*)(void *, gm_node *, gm_node *, gm_b16_8))pn3->clauses[10])(pn3->env, pn3->hctx, pn3, t2);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tm98ap4112hee t5;
        if (!((((uint8_t *)&t4))[0] != 0)) {
            gm_tm98ap4112hee t6 = GM_LD(gm_tm98ap4112hee, (((uint8_t *)&t4) + 8));
            t5 = t6;
        } else {
            gm_toe5fmr3vvf4d t7 = GM_LD(gm_toe5fmr3vvf4d, (((uint8_t *)&t4) + 8));
            gm_l1 = t7;
            {
                gm_toe5fmr3vvf4d t9 = gm_l1;
                gm_te2n4j2vtm53d t10;
                GM_ST(uint8_t, (uint8_t *)&t10 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t10 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t10 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t10 + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&t10 + 24, 0);
                ((uint8_t *)&t10)[0] = 1;
                GM_ST(gm_toe5fmr3vvf4d, ((uint8_t *)&t10 + 8), t9);
                gm_b0_1 t11 = gm_f_for_body_6ellthc(cx->env, cx->hctx, cx, &t10);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b0_1 t12;
            uint8_t t13 = 0;
            F.ret = t12;
            goto gm_fret;
        }
        gm_l2 = t5;
    }
    {
    }
    {
    }
    {
        for (;;) {
            if (!((uint8_t)1)) goto lb17;
            {
                gm_tm98ap4112hee t20 = gm_l2;
                void * t21 = ((uint8_t *)&F.l3);
                if ((uintptr_t)0 > (uintptr_t)256 || (uintptr_t)256 > (uintptr_t)256) gm_trap_slice((uintptr_t)0, (uintptr_t)256, 256, gm_srcdo1hnokt0mq8, 178, 30);
                gm_b16_8 t22;
                GM_ST(void *, (uint8_t *)&t22, (void *)(((uint8_t *)t21 + (uintptr_t)0 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t22 + 8, (uintptr_t)((uintptr_t)256 - (uintptr_t)0));
                gm_node *pn23 = gm_find(cx, 2126215853);
                if (!pn23) gm_trap("no handler installed for effect `Fs`", gm_srcdo1hnokt0mq8, 178, 17);
                gm_tiuq7k5hm0kak t24;
                ((void (*)(void *, void *, gm_node *, gm_node *, gm_tm98ap4112hee, gm_b16_8))pn23->clauses[11])((void *)&t24, pn23->env, pn23->hctx, pn23, t20, t22);
                if (gm_unw.kind) {
                    goto gm_ll18;
                }
                gm_t11t9d8s5diq9 t25;
                if (!((((uint8_t *)&t24))[0] != 0)) {
                    t25 = (*(gm_t11t9d8s5diq9 *)(((uint8_t *)&t24) + 8));
                } else {
                    gm_toe5fmr3vvf4d t26 = GM_LD(gm_toe5fmr3vvf4d, (((uint8_t *)&t24) + 8));
                    gm_l4 = t26;
                    {
                        gm_toe5fmr3vvf4d t28 = gm_l4;
                        gm_te2n4j2vtm53d t29;
                        GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
                        GM_ST(uint64_t, (uint8_t *)&t29 + 24, 0);
                        ((uint8_t *)&t29)[0] = 1;
                        GM_ST(gm_toe5fmr3vvf4d, ((uint8_t *)&t29 + 8), t28);
                        gm_b0_1 t30 = gm_f_for_body_6ellthc(cx->env, cx->hctx, cx, &t29);
                        if (gm_unw.kind) {
                            goto gm_ll18;
                        }
                    }
                    gm_b0_1 t31;
                    uint8_t t32 = 0;
                    F.ret = t31;
                    gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = t32;
                    goto gm_ll18;
                }
                gm_l5 = t25;
            }
            {
                gm_t11t9d8s5diq9 t33 = gm_l5;
                gm_t617vr72bd7b3 t34;
                if (((((uint8_t *)&t33))[16] != 0)) {
                    gm_t617vr72bd7b3 t35 = GM_LD(gm_t617vr72bd7b3, (((uint8_t *)&t33) + 0));
                    t34 = t35;
                } else {
                    goto lb17;
                }
                gm_l6 = t34;
            }
            {
                gm_tibupt4dmcqai t36;
                memset((uint8_t *)&t36 + 17, 0, 7);
                void * t37 = ((uint8_t *)&F.l3);
                uint64_t t38 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t39 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 0));
                uint64_t t40 = (uintptr_t)t39;
                if (t38 > t40 || t40 > (uintptr_t)256) gm_trap_slice(t38, t40, 256, gm_srcdo1hnokt0mq8, 183, 27);
                gm_b16_8 t41;
                GM_ST(void *, (uint8_t *)&t41, (void *)(((uint8_t *)t37 + t38 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t41 + 8, (uintptr_t)(t40 - t38));
                gm_b16_8 t42 = GM_LD(gm_b16_8, ((uint8_t *)&t41));
                GM_ST(gm_b16_8, (uint8_t *)&t36 + 0, t42);
                gm_t9qkh0rsooc8d t43 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l6) + 8));
                GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&t36 + 16, t43);
                gm_te2n4j2vtm53d t44;
                GM_ST(uint8_t, (uint8_t *)&t44 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t44 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t44 + 4, 0);
                ((uint8_t *)&t44)[0] = 0;
                GM_ST(gm_tibupt4dmcqai, ((uint8_t *)&t44 + 8), t36);
                gm_b0_1 t45 = gm_f_for_body_6ellthc(cx->env, cx->hctx, cx, &t44);
                if (gm_unw.kind) {
                    goto gm_ll18;
                }
            }
lc17:;
        }
gm_ll18:;
        if (gm_unw.frame == &F.h && gm_unw.label == 0) {
            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb17; }
            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc17; }
        }
        goto gm_ld1_1;
lb17:;
    }
    {
        gm_tm98ap4112hee t46 = gm_l2;
        gm_node *pn47 = gm_find(cx, 2126215853);
        if (!pn47) gm_trap("no handler installed for effect `Fs`", gm_srcdo1hnokt0mq8, 174, 11);
        gm_b0_1 t48;
        t48 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, gm_tm98ap4112hee))pn47->clauses[12])(pn47->env, pn47->hctx, pn47, t46);
    }
    goto gm_ls49;
gm_ld1_1:;
    {
        gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
        gm_tm98ap4112hee t50 = gm_l2;
        gm_node *pn51 = gm_find(cx, 2126215853);
        if (!pn51) gm_trap("no handler installed for effect `Fs`", gm_srcdo1hnokt0mq8, 174, 11);
        gm_b0_1 t52;
        t52 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, gm_tm98ap4112hee))pn51->clauses[12])(pn51->env, pn51->hctx, pn51, t50);
        gm_unw = gm_saved;
    }
    goto gm_lad;
gm_ls49:;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_entries_44o5n9f_ssu37o5(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_entries_44o5n9f F;
    gm_b16_8 gm_l0; /* path */
    gm_toe5fmr3vvf4d gm_l1; /* e */
    gm_tm98ap4112hee gm_l2; /* d */
    gm_toe5fmr3vvf4d gm_l4; /* e */
    gm_t11t9d8s5diq9 gm_l5; /* next */
    gm_t617vr72bd7b3 gm_l6; /* ent */
    GM_STACK_CHECK(&F, 515u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_b16_8 t2 = gm_l0;
        gm_node *pn3 = gm_find(cx, 2126215853);
        if (!pn3) gm_trap("no handler installed for effect `Fs`", gm_srcdo1hnokt0mq8, 170, 10);
        gm_t62ebbi2i5rkd t4;
        t4 = ((gm_t62ebbi2i5rkd (*)(void *, gm_node *, gm_node *, gm_b16_8))pn3->clauses[10])(pn3->env, pn3->hctx, pn3, t2);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tm98ap4112hee t5;
        if (!((((uint8_t *)&t4))[0] != 0)) {
            gm_tm98ap4112hee t6 = GM_LD(gm_tm98ap4112hee, (((uint8_t *)&t4) + 8));
            t5 = t6;
        } else {
            gm_toe5fmr3vvf4d t7 = GM_LD(gm_toe5fmr3vvf4d, (((uint8_t *)&t4) + 8));
            gm_l1 = t7;
            {
                gm_toe5fmr3vvf4d t9 = gm_l1;
                gm_te2n4j2vtm53d t10;
                GM_ST(uint8_t, (uint8_t *)&t10 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t10 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t10 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t10 + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&t10 + 24, 0);
                ((uint8_t *)&t10)[0] = 1;
                GM_ST(gm_toe5fmr3vvf4d, ((uint8_t *)&t10 + 8), t9);
                gm_b0_1 t11 = gm_f_for_body_4g8ig10(cx->env, cx->hctx, cx, &t10);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b0_1 t12;
            uint8_t t13 = 0;
            F.ret = t12;
            goto gm_fret;
        }
        gm_l2 = t5;
    }
    {
    }
    {
    }
    {
        for (;;) {
            if (!((uint8_t)1)) goto lb17;
            {
                gm_tm98ap4112hee t20 = gm_l2;
                void * t21 = ((uint8_t *)&F.l3);
                if ((uintptr_t)0 > (uintptr_t)256 || (uintptr_t)256 > (uintptr_t)256) gm_trap_slice((uintptr_t)0, (uintptr_t)256, 256, gm_srcdo1hnokt0mq8, 178, 30);
                gm_b16_8 t22;
                GM_ST(void *, (uint8_t *)&t22, (void *)(((uint8_t *)t21 + (uintptr_t)0 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t22 + 8, (uintptr_t)((uintptr_t)256 - (uintptr_t)0));
                gm_node *pn23 = gm_find(cx, 2126215853);
                if (!pn23) gm_trap("no handler installed for effect `Fs`", gm_srcdo1hnokt0mq8, 178, 17);
                gm_tiuq7k5hm0kak t24;
                ((void (*)(void *, void *, gm_node *, gm_node *, gm_tm98ap4112hee, gm_b16_8))pn23->clauses[11])((void *)&t24, pn23->env, pn23->hctx, pn23, t20, t22);
                if (gm_unw.kind) {
                    goto gm_ll18;
                }
                gm_t11t9d8s5diq9 t25;
                if (!((((uint8_t *)&t24))[0] != 0)) {
                    t25 = (*(gm_t11t9d8s5diq9 *)(((uint8_t *)&t24) + 8));
                } else {
                    gm_toe5fmr3vvf4d t26 = GM_LD(gm_toe5fmr3vvf4d, (((uint8_t *)&t24) + 8));
                    gm_l4 = t26;
                    {
                        gm_toe5fmr3vvf4d t28 = gm_l4;
                        gm_te2n4j2vtm53d t29;
                        GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
                        GM_ST(uint64_t, (uint8_t *)&t29 + 24, 0);
                        ((uint8_t *)&t29)[0] = 1;
                        GM_ST(gm_toe5fmr3vvf4d, ((uint8_t *)&t29 + 8), t28);
                        gm_b0_1 t30 = gm_f_for_body_4g8ig10(cx->env, cx->hctx, cx, &t29);
                        if (gm_unw.kind) {
                            goto gm_ll18;
                        }
                    }
                    gm_b0_1 t31;
                    uint8_t t32 = 0;
                    F.ret = t31;
                    gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = t32;
                    goto gm_ll18;
                }
                gm_l5 = t25;
            }
            {
                gm_t11t9d8s5diq9 t33 = gm_l5;
                gm_t617vr72bd7b3 t34;
                if (((((uint8_t *)&t33))[16] != 0)) {
                    gm_t617vr72bd7b3 t35 = GM_LD(gm_t617vr72bd7b3, (((uint8_t *)&t33) + 0));
                    t34 = t35;
                } else {
                    goto lb17;
                }
                gm_l6 = t34;
            }
            {
                gm_tibupt4dmcqai t36;
                memset((uint8_t *)&t36 + 17, 0, 7);
                void * t37 = ((uint8_t *)&F.l3);
                uint64_t t38 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t39 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 0));
                uint64_t t40 = (uintptr_t)t39;
                if (t38 > t40 || t40 > (uintptr_t)256) gm_trap_slice(t38, t40, 256, gm_srcdo1hnokt0mq8, 183, 27);
                gm_b16_8 t41;
                GM_ST(void *, (uint8_t *)&t41, (void *)(((uint8_t *)t37 + t38 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t41 + 8, (uintptr_t)(t40 - t38));
                gm_b16_8 t42 = GM_LD(gm_b16_8, ((uint8_t *)&t41));
                GM_ST(gm_b16_8, (uint8_t *)&t36 + 0, t42);
                gm_t9qkh0rsooc8d t43 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l6) + 8));
                GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&t36 + 16, t43);
                gm_te2n4j2vtm53d t44;
                GM_ST(uint8_t, (uint8_t *)&t44 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t44 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t44 + 4, 0);
                ((uint8_t *)&t44)[0] = 0;
                GM_ST(gm_tibupt4dmcqai, ((uint8_t *)&t44 + 8), t36);
                gm_b0_1 t45 = gm_f_for_body_4g8ig10(cx->env, cx->hctx, cx, &t44);
                if (gm_unw.kind) {
                    goto gm_ll18;
                }
            }
lc17:;
        }
gm_ll18:;
        if (gm_unw.frame == &F.h && gm_unw.label == 0) {
            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb17; }
            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc17; }
        }
        goto gm_ld1_1;
lb17:;
    }
    {
        gm_tm98ap4112hee t46 = gm_l2;
        gm_node *pn47 = gm_find(cx, 2126215853);
        if (!pn47) gm_trap("no handler installed for effect `Fs`", gm_srcdo1hnokt0mq8, 174, 11);
        gm_b0_1 t48;
        t48 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, gm_tm98ap4112hee))pn47->clauses[12])(pn47->env, pn47->hctx, pn47, t46);
    }
    goto gm_ls49;
gm_ld1_1:;
    {
        gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
        gm_tm98ap4112hee t50 = gm_l2;
        gm_node *pn51 = gm_find(cx, 2126215853);
        if (!pn51) gm_trap("no handler installed for effect `Fs`", gm_srcdo1hnokt0mq8, 174, 11);
        gm_b0_1 t52;
        t52 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, gm_tm98ap4112hee))pn51->clauses[12])(pn51->env, pn51->hctx, pn51, t50);
        gm_unw = gm_saved;
    }
    goto gm_lad;
gm_ls49:;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_entries_44o5n9f_suo216n(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_entries_44o5n9f F;
    gm_b16_8 gm_l0; /* path */
    gm_toe5fmr3vvf4d gm_l1; /* e */
    gm_tm98ap4112hee gm_l2; /* d */
    gm_toe5fmr3vvf4d gm_l4; /* e */
    gm_t11t9d8s5diq9 gm_l5; /* next */
    gm_t617vr72bd7b3 gm_l6; /* ent */
    GM_STACK_CHECK(&F, 515u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_b16_8 t2 = gm_l0;
        gm_node *pn3 = gm_find(cx, 2126215853);
        if (!pn3) gm_trap("no handler installed for effect `Fs`", gm_srcdo1hnokt0mq8, 170, 10);
        gm_t62ebbi2i5rkd t4;
        t4 = ((gm_t62ebbi2i5rkd (*)(void *, gm_node *, gm_node *, gm_b16_8))pn3->clauses[10])(pn3->env, pn3->hctx, pn3, t2);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tm98ap4112hee t5;
        if (!((((uint8_t *)&t4))[0] != 0)) {
            gm_tm98ap4112hee t6 = GM_LD(gm_tm98ap4112hee, (((uint8_t *)&t4) + 8));
            t5 = t6;
        } else {
            gm_toe5fmr3vvf4d t7 = GM_LD(gm_toe5fmr3vvf4d, (((uint8_t *)&t4) + 8));
            gm_l1 = t7;
            {
                gm_toe5fmr3vvf4d t9 = gm_l1;
                gm_te2n4j2vtm53d t10;
                GM_ST(uint8_t, (uint8_t *)&t10 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t10 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t10 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t10 + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&t10 + 24, 0);
                ((uint8_t *)&t10)[0] = 1;
                GM_ST(gm_toe5fmr3vvf4d, ((uint8_t *)&t10 + 8), t9);
                gm_b0_1 t11 = gm_f_for_body_0m0ntf2(cx->env, cx->hctx, cx, &t10);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b0_1 t12;
            uint8_t t13 = 0;
            F.ret = t12;
            goto gm_fret;
        }
        gm_l2 = t5;
    }
    {
    }
    {
    }
    {
        for (;;) {
            if (!((uint8_t)1)) goto lb17;
            {
                gm_tm98ap4112hee t20 = gm_l2;
                void * t21 = ((uint8_t *)&F.l3);
                if ((uintptr_t)0 > (uintptr_t)256 || (uintptr_t)256 > (uintptr_t)256) gm_trap_slice((uintptr_t)0, (uintptr_t)256, 256, gm_srcdo1hnokt0mq8, 178, 30);
                gm_b16_8 t22;
                GM_ST(void *, (uint8_t *)&t22, (void *)(((uint8_t *)t21 + (uintptr_t)0 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t22 + 8, (uintptr_t)((uintptr_t)256 - (uintptr_t)0));
                gm_node *pn23 = gm_find(cx, 2126215853);
                if (!pn23) gm_trap("no handler installed for effect `Fs`", gm_srcdo1hnokt0mq8, 178, 17);
                gm_tiuq7k5hm0kak t24;
                ((void (*)(void *, void *, gm_node *, gm_node *, gm_tm98ap4112hee, gm_b16_8))pn23->clauses[11])((void *)&t24, pn23->env, pn23->hctx, pn23, t20, t22);
                if (gm_unw.kind) {
                    goto gm_ll18;
                }
                gm_t11t9d8s5diq9 t25;
                if (!((((uint8_t *)&t24))[0] != 0)) {
                    t25 = (*(gm_t11t9d8s5diq9 *)(((uint8_t *)&t24) + 8));
                } else {
                    gm_toe5fmr3vvf4d t26 = GM_LD(gm_toe5fmr3vvf4d, (((uint8_t *)&t24) + 8));
                    gm_l4 = t26;
                    {
                        gm_toe5fmr3vvf4d t28 = gm_l4;
                        gm_te2n4j2vtm53d t29;
                        GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
                        GM_ST(uint64_t, (uint8_t *)&t29 + 24, 0);
                        ((uint8_t *)&t29)[0] = 1;
                        GM_ST(gm_toe5fmr3vvf4d, ((uint8_t *)&t29 + 8), t28);
                        gm_b0_1 t30 = gm_f_for_body_0m0ntf2(cx->env, cx->hctx, cx, &t29);
                        if (gm_unw.kind) {
                            goto gm_ll18;
                        }
                    }
                    gm_b0_1 t31;
                    uint8_t t32 = 0;
                    F.ret = t31;
                    gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = t32;
                    goto gm_ll18;
                }
                gm_l5 = t25;
            }
            {
                gm_t11t9d8s5diq9 t33 = gm_l5;
                gm_t617vr72bd7b3 t34;
                if (((((uint8_t *)&t33))[16] != 0)) {
                    gm_t617vr72bd7b3 t35 = GM_LD(gm_t617vr72bd7b3, (((uint8_t *)&t33) + 0));
                    t34 = t35;
                } else {
                    goto lb17;
                }
                gm_l6 = t34;
            }
            {
                gm_tibupt4dmcqai t36;
                memset((uint8_t *)&t36 + 17, 0, 7);
                void * t37 = ((uint8_t *)&F.l3);
                uint64_t t38 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t39 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 0));
                uint64_t t40 = (uintptr_t)t39;
                if (t38 > t40 || t40 > (uintptr_t)256) gm_trap_slice(t38, t40, 256, gm_srcdo1hnokt0mq8, 183, 27);
                gm_b16_8 t41;
                GM_ST(void *, (uint8_t *)&t41, (void *)(((uint8_t *)t37 + t38 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t41 + 8, (uintptr_t)(t40 - t38));
                gm_b16_8 t42 = GM_LD(gm_b16_8, ((uint8_t *)&t41));
                GM_ST(gm_b16_8, (uint8_t *)&t36 + 0, t42);
                gm_t9qkh0rsooc8d t43 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l6) + 8));
                GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&t36 + 16, t43);
                gm_te2n4j2vtm53d t44;
                GM_ST(uint8_t, (uint8_t *)&t44 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t44 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t44 + 4, 0);
                ((uint8_t *)&t44)[0] = 0;
                GM_ST(gm_tibupt4dmcqai, ((uint8_t *)&t44 + 8), t36);
                gm_b0_1 t45 = gm_f_for_body_0m0ntf2(cx->env, cx->hctx, cx, &t44);
                if (gm_unw.kind) {
                    goto gm_ll18;
                }
            }
lc17:;
        }
gm_ll18:;
        if (gm_unw.frame == &F.h && gm_unw.label == 0) {
            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb17; }
            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc17; }
        }
        goto gm_ld1_1;
lb17:;
    }
    {
        gm_tm98ap4112hee t46 = gm_l2;
        gm_node *pn47 = gm_find(cx, 2126215853);
        if (!pn47) gm_trap("no handler installed for effect `Fs`", gm_srcdo1hnokt0mq8, 174, 11);
        gm_b0_1 t48;
        t48 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, gm_tm98ap4112hee))pn47->clauses[12])(pn47->env, pn47->hctx, pn47, t46);
    }
    goto gm_ls49;
gm_ld1_1:;
    {
        gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
        gm_tm98ap4112hee t50 = gm_l2;
        gm_node *pn51 = gm_find(cx, 2126215853);
        if (!pn51) gm_trap("no handler installed for effect `Fs`", gm_srcdo1hnokt0mq8, 174, 11);
        gm_b0_1 t52;
        t52 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, gm_tm98ap4112hee))pn51->clauses[12])(pn51->env, pn51->hctx, pn51, t50);
        gm_unw = gm_saved;
    }
    goto gm_lad;
gm_ls49:;
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
extern __attribute__((aligned(4))) uint8_t gm_f_exists_1uo1rk0(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_ttln0d0hd70en gm_f_fill_temp_g12d8pg(void *env, gm_node *cx, gm_node *inst, gm_tm98ap4112hee a0, gm_b16_8 a1) {
    struct gm_fr_fill_temp_g12d8pg F;
    gm_tm98ap4112hee gm_l0; /* f */
    gm_b16_8 gm_l1; /* bytes */
    GM_STACK_CHECK(&F, 125u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        gm_tm98ap4112hee t2 = gm_l0;
        gm_b16_8 t3 = gm_l1;
        gm_ttln0d0hd70en t4 = gm_f_write_all_to_fe22ls9((void *)NULL, cx, NULL, t2, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t4))[0] != 0)) {
            gm_toe5fmr3vvf4d t5 = GM_LD(gm_toe5fmr3vvf4d, (((uint8_t *)&t4) + 4));
            gm_ttln0d0hd70en t6;
            GM_ST(uint8_t, (uint8_t *)&t6 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t6 + 2, 0);
            ((uint8_t *)&t6)[0] = 1;
            GM_ST(gm_toe5fmr3vvf4d, ((uint8_t *)&t6 + 4), t5);
            F.ret = t6;
            goto gm_fret;
        }
        gm_b0_1 t7 = GM_LD(gm_b0_1, (((uint8_t *)&t4) + 4));
    }
    {
        gm_tm98ap4112hee t8 = gm_l0;
        gm_node *pn9 = gm_find(cx, 2126215853);
        if (!pn9) gm_trap("no handler installed for effect `Fs`", gm_srcdo1hnokt0mq8, 527, 12);
        gm_ttln0d0hd70en t10;
        t10 = ((gm_ttln0d0hd70en (*)(void *, gm_node *, gm_node *, gm_tm98ap4112hee))pn9->clauses[15])(pn9->env, pn9->hctx, pn9, t8);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t11 = ((((uint8_t *)&t10))[0] != 0);
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
extern __attribute__((aligned(4))) uint8_t gm_f_hex_digit_fdbg8g2(uint8_t a0);
extern __attribute__((aligned(4))) uint8_t gm_f_is_sep_h2f3b7l(uint8_t a0);
extern __attribute__((aligned(4))) uint8_t gm_f_is_separator_mbkq9i8(uint8_t a0);
__attribute__((aligned(4))) gm_ttln0d0hd70en gm_f_make_dir_all_q6phjan(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_make_dir_all_q6phjan F;
    gm_b16_8 gm_l0; /* path */
    uint64_t gm_l1; /* i */
    gm_b16_8 gm_l2; /* prefix */
    gm_toe5fmr3vvf4d gm_l3; /* e */
    gm_t5j14m8ru132p gm_l4; /* info */
    GM_STACK_CHECK(&F, 482u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_l1 = ((uint64_t)0ULL);
    }
    {
        for (;;) {
            uint64_t t3 = gm_l1;
            uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            uint8_t t5 = (t3 < t4);
            if (!t5) goto lb2;
            {
                for (;;) {
                    uint64_t t9 = gm_l1;
                    uint64_t t10 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                    uint8_t t11 = (t9 < t10);
                    if (!t11) goto lb8;
                    uint64_t t12 = gm_l1;
                    uint64_t t13 = (uintptr_t)t12;
                    uint64_t t14 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                    if (t13 >= (uintptr_t)(t14)) gm_trap_index(t13, t14, gm_srcdo1hnokt0mq8, 282, 45);
                    uint8_t t15 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t13 * 1));
                    uint8_t t16 = gm_f_is_separator_mbkq9i8(t15);
                    if (!t16) goto lb8;
                    {
                        uint64_t t19 = gm_l1;
                        uint64_t r20; bool o21 = __builtin_add_overflow(t19, ((uint64_t)1ULL), &r20);
                        if (o21) gm_trap("integer overflow in `+`", gm_srcdo1hnokt0mq8, 282, 56);
                        uint64_t t22 = r20;
                        gm_l1 = t22;
                    }
lc8:;
                }
lb8:;
            }
            {
                uint64_t t24 = gm_l1;
                uint64_t t25 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                uint8_t t26 = (t24 >= t25);
                if (!t26) goto ie23;
                {
                    goto lb2;
                }
                goto ix23;
ie23:;
ix23:;
            }
            {
                for (;;) {
                    uint64_t t29 = gm_l1;
                    uint64_t t30 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                    uint8_t t31 = (t29 < t30);
                    if (!t31) goto lb28;
                    uint64_t t32 = gm_l1;
                    uint64_t t33 = (uintptr_t)t32;
                    uint64_t t34 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                    if (t33 >= (uintptr_t)(t34)) gm_trap_index(t33, t34, gm_srcdo1hnokt0mq8, 284, 49);
                    uint8_t t35 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t33 * 1));
                    uint8_t t36 = gm_f_is_separator_mbkq9i8(t35);
                    uint8_t t37 = (uint8_t)!t36;
                    if (!t37) goto lb28;
                    {
                        uint64_t t40 = gm_l1;
                        uint64_t r41; bool o42 = __builtin_add_overflow(t40, ((uint64_t)1ULL), &r41);
                        if (o42) gm_trap("integer overflow in `+`", gm_srcdo1hnokt0mq8, 284, 60);
                        uint64_t t43 = r41;
                        gm_l1 = t43;
                    }
lc28:;
                }
lb28:;
            }
            {
                uint64_t t44 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                void * t45 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)));
                uint64_t t46 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t47 = gm_l1;
                uint64_t t48 = (uintptr_t)t47;
                if (t46 > t48 || t48 > (uintptr_t)t44) gm_trap_slice(t46, t48, t44, gm_srcdo1hnokt0mq8, 285, 19);
                gm_b16_8 t49;
                GM_ST(void *, (uint8_t *)&t49, (void *)(((uint8_t *)t45 + t46 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t49 + 8, (uintptr_t)(t48 - t46));
                gm_l2 = t49;
            }
            {
                gm_b0_1 t50;
            }
            {
                gm_b16_8 t51 = gm_l2;
                gm_node *pn52 = gm_find(cx, 2126215853);
                if (!pn52) gm_trap("no handler installed for effect `Fs`", gm_srcdo1hnokt0mq8, 290, 9);
                gm_ttln0d0hd70en t53;
                t53 = ((gm_ttln0d0hd70en (*)(void *, gm_node *, gm_node *, gm_b16_8))pn52->clauses[8])(pn52->env, pn52->hctx, pn52, t51);
                if (gm_unw.kind) {
                    goto gm_ll6;
                }
                gm_b0_1 t54;
                if (!((((uint8_t *)&t53))[0] != 0)) {
                    gm_b0_1 t55 = GM_LD(gm_b0_1, (((uint8_t *)&t53) + 4));
                    t54 = t55;
                } else {
                    gm_toe5fmr3vvf4d t56 = GM_LD(gm_toe5fmr3vvf4d, (((uint8_t *)&t53) + 4));
                    gm_l3 = t56;
                    {
                        uint8_t t59 = (!(GM_LD(uint8_t, ((uint8_t *)&gm_l3) + 0) == ((uint8_t)2ULL)));
                        if (!t59) goto ie58;
                        {
                            gm_toe5fmr3vvf4d t61 = gm_l3;
                            gm_ttln0d0hd70en t62;
                            GM_ST(uint8_t, (uint8_t *)&t62 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t62 + 2, 0);
                            ((uint8_t *)&t62)[0] = 1;
                            GM_ST(gm_toe5fmr3vvf4d, ((uint8_t *)&t62 + 4), t61);
                            uint8_t t63 = ((((uint8_t *)&t62))[0] != 0);
                            F.ret = t62;
                            goto gm_fret;
                        }
                        goto ix58;
ie58:;
ix58:;
                    }
                    {
                        gm_b16_8 t64 = gm_l2;
                        gm_node *pn65 = gm_find(cx, 2126215853);
                        if (!pn65) gm_trap("no handler installed for effect `Fs`", gm_srcdo1hnokt0mq8, 292, 25);
                        gm_tih6h4avv5nrb t66;
                        ((void (*)(void *, void *, gm_node *, gm_node *, gm_b16_8))pn65->clauses[5])((void *)&t66, pn65->env, pn65->hctx, pn65, t64);
                        if (gm_unw.kind) {
                            goto gm_ll6;
                        }
                        if (((((uint8_t *)&t66))[0] != 0)) {
                            gm_toe5fmr3vvf4d t67 = GM_LD(gm_toe5fmr3vvf4d, (((uint8_t *)&t66) + 8));
                            gm_ttln0d0hd70en t68;
                            GM_ST(uint8_t, (uint8_t *)&t68 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t68 + 2, 0);
                            ((uint8_t *)&t68)[0] = 1;
                            GM_ST(gm_toe5fmr3vvf4d, ((uint8_t *)&t68 + 4), t67);
                            F.ret = t68;
                            goto gm_fret;
                        }
                        gm_l4 = (*(gm_t5j14m8ru132p *)(((uint8_t *)&t66) + 8));
                    }
                    uint8_t t70 = (!(GM_LD(uint8_t, (((uint8_t *)&gm_l4) + 8) + 0) == ((uint8_t)1ULL)));
                    if (!t70) goto ie69;
                    {
                        gm_toe5fmr3vvf4d t72 = gm_l3;
                        gm_ttln0d0hd70en t73;
                        GM_ST(uint8_t, (uint8_t *)&t73 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t73 + 2, 0);
                        ((uint8_t *)&t73)[0] = 1;
                        GM_ST(gm_toe5fmr3vvf4d, ((uint8_t *)&t73 + 4), t72);
                        uint8_t t74 = ((((uint8_t *)&t73))[0] != 0);
                        F.ret = t73;
                        goto gm_fret;
                    }
                    goto ix69;
ie69:;
ix69:;
                    gm_b0_1 t75 = GM_ZERO(gm_b0_1);
                    t54 = t75;
                }
            }
lc2:;
        }
gm_ll6:;
        if (gm_unw.frame == &F.h && gm_unw.label == 0) {
            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb2; }
            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc2; }
        }
        goto gm_lad;
lb2:;
    }
    {
        gm_b0_1 t76;
        gm_ttln0d0hd70en t77;
        GM_ST(uint8_t, (uint8_t *)&t77 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t77 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t77 + 4, 0);
        GM_ST(uint32_t, (uint8_t *)&t77 + 8, 0);
        ((uint8_t *)&t77)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t77 + 4), t76);
        uint8_t t78 = ((((uint8_t *)&t77))[0] != 0);
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
inline __attribute__((aligned(4))) void gm_f_make_temp_dir_vi8f88d(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1, gm_b16_8 a2) {
    struct gm_fr_make_temp_dir_vi8f88d F;
    gm_b16_8 gm_l0; /* dir */
    gm_b16_8 gm_l1; /* prefix */
    gm_b16_8 gm_l2; /* buf */
    uint64_t gm_l3; /* n */
    uint32_t gm_l4; /* tries */
    gm_b16_8 gm_l5; /* path */
    gm_toe5fmr3vvf4d gm_l6; /* e */
    GM_STACK_CHECK(&F, 370u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        gm_b16_8 t2 = gm_l0;
        gm_b16_8 t3 = gm_l1;
        gm_b16_8 t4 = gm_l2;
        gm_tf5srh4mp4b3i t5 = gm_f_temp_stem_3b7s4ll(t2, t3, t4);
        uint64_t t6;
        if (((((uint8_t *)&t5))[8] != 0)) {
            uint64_t t7 = GM_LD(uint64_t, (((uint8_t *)&t5) + 0));
            t6 = t7;
        } else {
            gm_toe5fmr3vvf4d t8;
            GM_ST(uint8_t, (uint8_t *)&t8 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t8 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t8 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t8 + 0), ((uint8_t)7ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t5uv522f0p612 *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t5uv522f0p612 *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t5uv522f0p612 *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t5uv522f0p612 *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_t5uv522f0p612 *)gm_out))[0] = 1;
            GM_ST(gm_toe5fmr3vvf4d, (uint8_t *)&(*(gm_t5uv522f0p612 *)gm_out) + 8, t8);
            goto gm_fdone;
        }
        gm_l3 = t6;
    }
    {
        gm_l4 = ((uint32_t)0ULL);
    }
    {
        for (;;) {
            if (!((uint8_t)1)) goto lb9;
            {
                gm_b16_8 t12 = gm_l2;
                uint64_t t13 = gm_l3;
                gm_b0_1 t14 = gm_f_temp_random_aie9j36((void *)NULL, cx, NULL, t12, t13);
                if (gm_unw.kind) {
                    goto gm_ll10;
                }
            }
            {
                uint64_t t15 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                void * t16 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)));
                uint64_t t17 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t18 = gm_l3;
                uint64_t r19; bool o20 = __builtin_add_overflow(t18, ((uint64_t)16ULL), &r19);
                if (o20) gm_trap("integer overflow in `+`", gm_srcdo1hnokt0mq8, 467, 24);
                uint64_t t21 = r19;
                uint64_t t22 = (uintptr_t)t21;
                if (t17 > t22 || t22 > (uintptr_t)t15) gm_trap_slice(t17, t22, t15, gm_srcdo1hnokt0mq8, 467, 17);
                gm_b16_8 t23;
                GM_ST(void *, (uint8_t *)&t23, (void *)(((uint8_t *)t16 + t17 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t23 + 8, (uintptr_t)(t22 - t17));
                gm_l5 = t23;
            }
            {
                gm_b16_8 t24 = gm_l5;
                gm_b16_8 t25 = GM_LD(gm_b16_8, ((uint8_t *)&t24));
                gm_node *pn26 = gm_find(cx, 2126215853);
                if (!pn26) gm_trap("no handler installed for effect `Fs`", gm_srcdo1hnokt0mq8, 468, 9);
                gm_ttln0d0hd70en t27;
                t27 = ((gm_ttln0d0hd70en (*)(void *, gm_node *, gm_node *, gm_b16_8))pn26->clauses[8])(pn26->env, pn26->hctx, pn26, t25);
                if (gm_unw.kind) {
                    goto gm_ll10;
                }
                gm_b0_1 t28;
                if (!((((uint8_t *)&t27))[0] != 0)) {
                    gm_b0_1 t29 = GM_LD(gm_b0_1, (((uint8_t *)&t27) + 4));
                    t28 = t29;
                } else {
                    gm_toe5fmr3vvf4d t30 = GM_LD(gm_toe5fmr3vvf4d, (((uint8_t *)&t27) + 4));
                    gm_l6 = t30;
                    {
                        uint8_t t33 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l6) + 0) == ((uint8_t)2ULL)));
                        if (!t33) goto ie32;
                        uint32_t t34 = gm_l4;
                        uint32_t r35; bool o36 = __builtin_add_overflow(t34, ((uint32_t)1ULL), &r35);
                        if (o36) gm_trap("integer overflow in `+`", gm_srcdo1hnokt0mq8, 469, 40);
                        uint32_t t37 = r35;
                        uint8_t t38 = (t37 < ((uint32_t)16ULL));
                        if (!t38) goto ie32;
                        {
                            uint32_t t40 = gm_l4;
                            uint32_t r41; bool o42 = __builtin_add_overflow(t40, ((uint32_t)1ULL), &r41);
                            if (o42) gm_trap("integer overflow in `+`", gm_srcdo1hnokt0mq8, 470, 17);
                            uint32_t t43 = r41;
                            gm_l4 = t43;
                        }
                        {
                            goto lc9;
                        }
                        goto ix32;
ie32:;
ix32:;
                    }
                    gm_toe5fmr3vvf4d t44 = gm_l6;
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_t5uv522f0p612 *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_t5uv522f0p612 *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_t5uv522f0p612 *)gm_out) + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_t5uv522f0p612 *)gm_out) + 16, 0);
                    ((uint8_t *)&(*(gm_t5uv522f0p612 *)gm_out))[0] = 1;
                    GM_ST(gm_toe5fmr3vvf4d, (uint8_t *)&(*(gm_t5uv522f0p612 *)gm_out) + 8, t44);
                    goto gm_fdone;
                }
            }
            {
                gm_b16_8 t45 = gm_l5;
                gm_b16_8 t46 = GM_LD(gm_b16_8, ((uint8_t *)&t45));
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t5uv522f0p612 *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_t5uv522f0p612 *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_t5uv522f0p612 *)gm_out) + 4, 0);
                ((uint8_t *)&(*(gm_t5uv522f0p612 *)gm_out))[0] = 0;
                GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t5uv522f0p612 *)gm_out) + 8, t46);
                goto gm_fdone;
            }
lc9:;
        }
gm_ll10:;
        if (gm_unw.frame == &F.h && gm_unw.label == 0) {
            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb9; }
            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc9; }
        }
        goto gm_lad;
lb9:;
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
__attribute__((aligned(4))) void gm_f_read_all_r9rh9qd(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_read_all_r9rh9qd F;
    gm_b16_8 gm_l0; /* path */
    gm_tm98ap4112hee gm_l1; /* f */
    uint64_t gm_l2; /* size */
    uint64_t gm_l3; /* cap */
    gm_b16_8 gm_l4; /* data */
    uint64_t gm_l5; /* len */
    gm_t9qkh0rsooc8d gm_l6; /* e */
    gm_b16_8 gm_l7; /* bigger */
    gm_toe5fmr3vvf4d gm_l8; /* e */
    uint64_t gm_l9; /* n */
    gm_t9qkh0rsooc8d gm_l10; /* e */
    gm_b16_8 gm_l11; /* exact */
    GM_STACK_CHECK(&F, 1213u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_b16_8 t2 = gm_l0;
        gm_t4q2b5ibsrdik t3;
        GM_ST(uint8_t, ((uint8_t *)&t3 + 0), ((uint8_t)1));
        GM_ST(uint8_t, ((uint8_t *)&t3 + 1), ((uint8_t)0));
        GM_ST(uint8_t, ((uint8_t *)&t3 + 2), ((uint8_t)0));
        GM_ST(uint8_t, ((uint8_t *)&t3 + 3), ((uint8_t)0));
        GM_ST(uint8_t, ((uint8_t *)&t3 + 4), ((uint8_t)0));
        GM_ST(uint8_t, ((uint8_t *)&t3 + 5), ((uint8_t)0));
        gm_node *pn4 = gm_find(cx, 2126215853);
        if (!pn4) gm_trap("no handler installed for effect `Fs`", gm_srcdo1hnokt0mq8, 33, 14);
        gm_t62ebbi2i5rkd t5;
        t5 = ((gm_t62ebbi2i5rkd (*)(void *, gm_node *, gm_node *, gm_b16_8, gm_t4q2b5ibsrdik))pn4->clauses[0])(pn4->env, pn4->hctx, pn4, t2, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t5))[0] != 0)) {
            gm_toe5fmr3vvf4d t6 = GM_LD(gm_toe5fmr3vvf4d, (((uint8_t *)&t5) + 8));
            gm_tj5jn79kq9pnm t7;
            GM_ST(uint8_t, (uint8_t *)&t7 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t7 + 2, 0);
            GM_ST(uint8_t, ((uint8_t *)&t7 + 0), ((uint8_t)0ULL));
            GM_ST(gm_toe5fmr3vvf4d, ((uint8_t *)&t7 + 4), t6);
            gm_tgvh15qshf643 t8;
            GM_ST(uint8_t, (uint8_t *)&t8 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t8 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t8 + 4, 0);
            GM_ST(uint32_t, (uint8_t *)&t8 + 20, 0);
            ((uint8_t *)&t8)[0] = 1;
            GM_ST(gm_tj5jn79kq9pnm, ((uint8_t *)&t8 + 8), t7);
            F.ret = t8;
            goto gm_fret;
        }
        gm_tm98ap4112hee t9 = GM_LD(gm_tm98ap4112hee, (((uint8_t *)&t5) + 8));
        gm_l1 = t9;
    }
    {
    }
    {
        gm_b16_8 t13 = gm_l0;
        gm_node *pn14 = gm_find(cx, 2126215853);
        if (!pn14) gm_trap("no handler installed for effect `Fs`", gm_srcdo1hnokt0mq8, 35, 18);
        gm_tih6h4avv5nrb t15;
        ((void (*)(void *, void *, gm_node *, gm_node *, gm_b16_8))pn14->clauses[5])((void *)&t15, pn14->env, pn14->hctx, pn14, t13);
        if (gm_unw.kind) {
            goto gm_ld1_1;
        }
        if (((((uint8_t *)&t15))[0] != 0)) {
            gm_toe5fmr3vvf4d t16 = GM_LD(gm_toe5fmr3vvf4d, (((uint8_t *)&t15) + 8));
            gm_tj5jn79kq9pnm t17;
            GM_ST(uint8_t, (uint8_t *)&t17 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t17 + 2, 0);
            GM_ST(uint8_t, ((uint8_t *)&t17 + 0), ((uint8_t)0ULL));
            GM_ST(gm_toe5fmr3vvf4d, ((uint8_t *)&t17 + 4), t16);
            gm_tgvh15qshf643 t18;
            GM_ST(uint8_t, (uint8_t *)&t18 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t18 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t18 + 4, 0);
            GM_ST(uint32_t, (uint8_t *)&t18 + 20, 0);
            ((uint8_t *)&t18)[0] = 1;
            GM_ST(gm_tj5jn79kq9pnm, ((uint8_t *)&t18 + 8), t17);
            F.ret = t18;
            gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = 1;
            goto gm_ld1_1;
        }
        uint64_t t19 = GM_LD(uint64_t, ((((uint8_t *)&t15) + 8) + 0));
        uint64_t t20 = (uint64_t)t19;
        gm_l2 = t20;
    }
    {
        uint64_t t21 = gm_l2;
        uint64_t r22; bool o23 = __builtin_add_overflow(t21, ((uint64_t)1ULL), &r22);
        if (o23) gm_trap("integer overflow in `+`", gm_srcdo1hnokt0mq8, 37, 12);
        uint64_t t24 = r22;
        gm_l3 = t24;
    }
    {
        uint64_t t25 = gm_l3;
        gm_tabb710d9mq2e t26;
        gm_f_make_2ip78bj((void *)&t26, (void *)NULL, cx, NULL, t25, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kr3abq56gec)));
        if (gm_unw.kind) {
            goto gm_ld1_1;
        }
        if (((((uint8_t *)&t26))[0] != 0)) {
            gm_t9qkh0rsooc8d t27 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&t26) + 8));
            gm_tj5jn79kq9pnm t28;
            GM_ST(uint8_t, (uint8_t *)&t28 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t28 + 2, 0);
            GM_ST(uint8_t, (uint8_t *)&t28 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t28 + 6, 0);
            GM_ST(uint32_t, (uint8_t *)&t28 + 8, 0);
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)1ULL));
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t28 + 4), t27);
            gm_tgvh15qshf643 t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 20, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_tj5jn79kq9pnm, ((uint8_t *)&t29 + 8), t28);
            F.ret = t29;
            gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = 1;
            goto gm_ld1_1;
        }
        gm_b16_8 t30 = GM_LD(gm_b16_8, (((uint8_t *)&t26) + 8));
        gm_l4 = t30;
    }
    {
        gm_l5 = ((uint64_t)0ULL);
    }
    {
        for (;;) {
            if (!((uint8_t)1)) goto lb31;
            {
                uint64_t t35 = gm_l5;
                uint64_t t36 = gm_l3;
                uint8_t t37 = ((t35 == t36));
                if (!t37) goto ie34;
                {
                    uint64_t t39 = gm_l3;
                    uint64_t r40; bool o41 = __builtin_mul_overflow(t39, ((uint64_t)2ULL), &r40);
                    if (o41) gm_trap("integer overflow in `*`", gm_srcdo1hnokt0mq8, 42, 32);
                    uint64_t t42 = r40;
                    gm_tabb710d9mq2e t43;
                    gm_f_make_2ip78bj((void *)&t43, (void *)NULL, cx, NULL, t42, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kfgeo9idkmp)));
                    if (gm_unw.kind) {
                        goto gm_ll32;
                    }
                    gm_b16_8 t44;
                    if (!((((uint8_t *)&t43))[0] != 0)) {
                        gm_b16_8 t45 = GM_LD(gm_b16_8, (((uint8_t *)&t43) + 8));
                        t44 = t45;
                    } else {
                        gm_t9qkh0rsooc8d t46 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&t43) + 8));
                        gm_l6 = t46;
                        {
                            gm_b16_8 t48 = gm_l4;
                            gm_b16_8 t49 = GM_LD(gm_b16_8, ((uint8_t *)&t48));
                            gm_b0_1 t50 = gm_f_free_rkuh2rm((void *)NULL, cx, NULL, t49, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_ku5cbgje11b)));
                            if (gm_unw.kind) {
                                goto gm_ll32;
                            }
                        }
                        gm_t9qkh0rsooc8d t51 = gm_l6;
                        gm_tj5jn79kq9pnm t52;
                        GM_ST(uint8_t, (uint8_t *)&t52 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t52 + 2, 0);
                        GM_ST(uint8_t, (uint8_t *)&t52 + 5, 0);
                        GM_ST(uint16_t, (uint8_t *)&t52 + 6, 0);
                        GM_ST(uint32_t, (uint8_t *)&t52 + 8, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t52 + 0), ((uint8_t)1ULL));
                        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t52 + 4), t51);
                        GM_ST(uint8_t, (uint8_t *)&F.ret + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&F.ret + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&F.ret + 4, 0);
                        GM_ST(uint32_t, (uint8_t *)&F.ret + 20, 0);
                        ((uint8_t *)&F.ret)[0] = 1;
                        GM_ST(gm_tj5jn79kq9pnm, (uint8_t *)&F.ret + 8, t52);
                        uint8_t t53 = ((((uint8_t *)&F.ret))[0] != 0);
                        gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = t53;
                        goto gm_ll32;
                    }
                    gm_l7 = t44;
                }
                {
                    uint64_t t54 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                    void * t55 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l7)));
                    uint64_t t56 = (uintptr_t)((uint64_t)0ULL);
                    uint64_t t57 = gm_l5;
                    uint64_t t58 = (uintptr_t)t57;
                    if (t56 > t58 || t58 > (uintptr_t)t54) gm_trap_slice(t56, t58, t54, gm_srcdo1hnokt0mq8, 46, 13);
                    gm_b16_8 t59;
                    GM_ST(void *, (uint8_t *)&t59, (void *)(((uint8_t *)t55 + t56 * 1)));
                    GM_ST(uintptr_t, (uint8_t *)&t59 + 8, (uintptr_t)(t58 - t56));
                    gm_b16_8 t60 = gm_l4;
                    gm_b16_8 t61 = GM_LD(gm_b16_8, ((uint8_t *)&t60));
                    gm_f_copy_from_f7j6p05(t59, t61);
                }
                {
                    gm_b16_8 t62 = gm_l4;
                    gm_b16_8 t63 = GM_LD(gm_b16_8, ((uint8_t *)&t62));
                    gm_b0_1 t64 = gm_f_free_rkuh2rm((void *)NULL, cx, NULL, t63, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kvu1vd24o16)));
                    if (gm_unw.kind) {
                        goto gm_ll32;
                    }
                }
                {
                    gm_b16_8 t65 = gm_l7;
                    gm_l4 = t65;
                }
                {
                    uint64_t t66 = gm_l3;
                    uint64_t r67; bool o68 = __builtin_mul_overflow(t66, ((uint64_t)2ULL), &r67);
                    if (o68) gm_trap("integer overflow in `*`", gm_srcdo1hnokt0mq8, 49, 13);
                    uint64_t t69 = r67;
                    gm_l3 = t69;
                }
                goto ix34;
ie34:;
ix34:;
            }
            {
                gm_tm98ap4112hee t70 = gm_l1;
                uint64_t t71 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                void * t72 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)));
                uint64_t t73 = gm_l5;
                uint64_t t74 = (uintptr_t)t73;
                uint64_t t75 = gm_l3;
                uint64_t t76 = (uintptr_t)t75;
                if (t74 > t76 || t76 > (uintptr_t)t71) gm_trap_slice(t74, t76, t71, gm_srcdo1hnokt0mq8, 51, 21);
                gm_b16_8 t77;
                GM_ST(void *, (uint8_t *)&t77, (void *)(((uint8_t *)t72 + t74 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t77 + 8, (uintptr_t)(t76 - t74));
                gm_node *pn78 = gm_find(cx, 2126215853);
                if (!pn78) gm_trap("no handler installed for effect `Fs`", gm_srcdo1hnokt0mq8, 51, 14);
                gm_thf5kr0is9u9i t79;
                t79 = ((gm_thf5kr0is9u9i (*)(void *, gm_node *, gm_node *, gm_tm98ap4112hee, gm_b16_8))pn78->clauses[2])(pn78->env, pn78->hctx, pn78, t70, t77);
                if (gm_unw.kind) {
                    goto gm_ll32;
                }
                uint64_t t80;
                if (!((((uint8_t *)&t79))[0] != 0)) {
                    uint64_t t81 = GM_LD(uint64_t, (((uint8_t *)&t79) + 8));
                    t80 = t81;
                } else {
                    gm_toe5fmr3vvf4d t82 = GM_LD(gm_toe5fmr3vvf4d, (((uint8_t *)&t79) + 8));
                    gm_l8 = t82;
                    {
                        gm_b16_8 t84 = gm_l4;
                        gm_b16_8 t85 = GM_LD(gm_b16_8, ((uint8_t *)&t84));
                        gm_b0_1 t86 = gm_f_free_rkuh2rm((void *)NULL, cx, NULL, t85, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kutnfkdjl3d)));
                        if (gm_unw.kind) {
                            goto gm_ll32;
                        }
                    }
                    gm_toe5fmr3vvf4d t87 = gm_l8;
                    gm_tj5jn79kq9pnm t88;
                    GM_ST(uint8_t, (uint8_t *)&t88 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t88 + 2, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t88 + 0), ((uint8_t)0ULL));
                    GM_ST(gm_toe5fmr3vvf4d, ((uint8_t *)&t88 + 4), t87);
                    GM_ST(uint8_t, (uint8_t *)&F.ret + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&F.ret + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&F.ret + 4, 0);
                    GM_ST(uint32_t, (uint8_t *)&F.ret + 20, 0);
                    ((uint8_t *)&F.ret)[0] = 1;
                    GM_ST(gm_tj5jn79kq9pnm, (uint8_t *)&F.ret + 8, t88);
                    uint8_t t89 = ((((uint8_t *)&F.ret))[0] != 0);
                    gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = t89;
                    goto gm_ll32;
                }
                gm_l9 = t80;
            }
            {
                uint64_t t91 = gm_l9;
                uint8_t t92 = ((t91 == ((uint64_t)0ULL)));
                if (!t92) goto ie90;
                {
                    goto lb31;
                }
                goto ix90;
ie90:;
ix90:;
            }
            {
                uint64_t t94 = gm_l5;
                uint64_t t95 = gm_l9;
                uint64_t r96; bool o97 = __builtin_add_overflow(t94, t95, &r96);
                if (o97) gm_trap("integer overflow in `+`", gm_srcdo1hnokt0mq8, 56, 9);
                uint64_t t98 = r96;
                gm_l5 = t98;
            }
lc31:;
        }
gm_ll32:;
        if (gm_unw.frame == &F.h && gm_unw.label == 0) {
            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb31; }
            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc31; }
        }
        goto gm_ld1_1;
lb31:;
    }
    {
        uint64_t t100 = gm_l5;
        uint64_t t101 = gm_l3;
        uint8_t t102 = ((t100 == t101));
        if (!t102) goto ie99;
        {
            gm_b16_8 t104 = gm_l4;
            GM_ST(uint8_t, (uint8_t *)&F.ret + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&F.ret + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&F.ret + 4, 0);
            ((uint8_t *)&F.ret)[0] = 0;
            GM_ST(gm_b16_8, (uint8_t *)&F.ret + 8, t104);
            uint8_t t105 = ((((uint8_t *)&F.ret))[0] != 0);
            gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = t105;
            goto gm_ld1_1;
        }
        goto ix99;
ie99:;
ix99:;
    }
    {
        uint64_t t106 = gm_l5;
        gm_tabb710d9mq2e t107;
        gm_f_make_2ip78bj((void *)&t107, (void *)NULL, cx, NULL, t106, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kreaco1pgk3)));
        if (gm_unw.kind) {
            goto gm_ld1_1;
        }
        gm_b16_8 t108;
        if (!((((uint8_t *)&t107))[0] != 0)) {
            gm_b16_8 t109 = GM_LD(gm_b16_8, (((uint8_t *)&t107) + 8));
            t108 = t109;
        } else {
            gm_t9qkh0rsooc8d t110 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&t107) + 8));
            gm_l10 = t110;
            {
                gm_b16_8 t112 = gm_l4;
                gm_b16_8 t113 = GM_LD(gm_b16_8, ((uint8_t *)&t112));
                gm_b0_1 t114 = gm_f_free_rkuh2rm((void *)NULL, cx, NULL, t113, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_koeeht11j75)));
                if (gm_unw.kind) {
                    goto gm_ld1_1;
                }
            }
            gm_t9qkh0rsooc8d t115 = gm_l10;
            gm_tj5jn79kq9pnm t116;
            GM_ST(uint8_t, (uint8_t *)&t116 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t116 + 2, 0);
            GM_ST(uint8_t, (uint8_t *)&t116 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t116 + 6, 0);
            GM_ST(uint32_t, (uint8_t *)&t116 + 8, 0);
            GM_ST(uint8_t, ((uint8_t *)&t116 + 0), ((uint8_t)1ULL));
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t116 + 4), t115);
            GM_ST(uint8_t, (uint8_t *)&F.ret + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&F.ret + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&F.ret + 4, 0);
            GM_ST(uint32_t, (uint8_t *)&F.ret + 20, 0);
            ((uint8_t *)&F.ret)[0] = 1;
            GM_ST(gm_tj5jn79kq9pnm, (uint8_t *)&F.ret + 8, t116);
            uint8_t t117 = ((((uint8_t *)&F.ret))[0] != 0);
            gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = t117;
            goto gm_ld1_1;
        }
        gm_l11 = t108;
    }
    {
        gm_b16_8 t118 = gm_l11;
        uint64_t t119 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
        void * t120 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)));
        uint64_t t121 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t122 = gm_l5;
        uint64_t t123 = (uintptr_t)t122;
        if (t121 > t123 || t123 > (uintptr_t)t119) gm_trap_slice(t121, t123, t119, gm_srcdo1hnokt0mq8, 64, 21);
        gm_b16_8 t124;
        GM_ST(void *, (uint8_t *)&t124, (void *)(((uint8_t *)t120 + t121 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t124 + 8, (uintptr_t)(t123 - t121));
        gm_b16_8 t125 = GM_LD(gm_b16_8, ((uint8_t *)&t124));
        gm_f_copy_from_f7j6p05(t118, t125);
    }
    {
        gm_b16_8 t126 = gm_l4;
        gm_b16_8 t127 = GM_LD(gm_b16_8, ((uint8_t *)&t126));
        gm_b0_1 t128 = gm_f_free_rkuh2rm((void *)NULL, cx, NULL, t127, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_km7ke510mna)));
        if (gm_unw.kind) {
            goto gm_ld1_1;
        }
    }
    {
        gm_b16_8 t129 = gm_l11;
        GM_ST(uint8_t, (uint8_t *)&F.ret + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&F.ret + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&F.ret + 4, 0);
        ((uint8_t *)&F.ret)[0] = 0;
        GM_ST(gm_b16_8, (uint8_t *)&F.ret + 8, t129);
        uint8_t t130 = ((((uint8_t *)&F.ret))[0] != 0);
        gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = t130;
        goto gm_ld1_1;
    }
    {
        gm_tm98ap4112hee t131 = gm_l1;
        gm_node *pn132 = gm_find(cx, 2126215853);
        if (!pn132) gm_trap("no handler installed for effect `Fs`", gm_srcdo1hnokt0mq8, 34, 11);
        gm_b0_1 t133;
        t133 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, gm_tm98ap4112hee))pn132->clauses[1])(pn132->env, pn132->hctx, pn132, t131);
    }
    goto gm_ls134;
gm_ld1_1:;
    {
        gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
        gm_tm98ap4112hee t135 = gm_l1;
        gm_node *pn136 = gm_find(cx, 2126215853);
        if (!pn136) gm_trap("no handler installed for effect `Fs`", gm_srcdo1hnokt0mq8, 34, 11);
        gm_b0_1 t137;
        t137 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, gm_tm98ap4112hee))pn136->clauses[1])(pn136->env, pn136->hctx, pn136, t135);
        gm_unw = gm_saved;
    }
    goto gm_lad;
gm_ls134:;
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
__attribute__((aligned(4))) gm_ttln0d0hd70en gm_f_remove_all_h0k1qkh(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_remove_all_h0k1qkh F;
    gm_b16_8 gm_l0; /* path */
    gm_t5j14m8ru132p gm_l1; /* info */
    gm_tm98ap4112hee gm_l4; /* first */
    gm_t5nmvq97ncshv gm_l6; /* top */
    gm_t11t9d8s5diq9 gm_l7; /* next */
    gm_toe5fmr3vvf4d gm_l8; /* e */
    gm_t617vr72bd7b3 gm_l9; /* ent */
    gm_toe5fmr3vvf4d gm_l10; /* e */
    gm_tm98ap4112hee gm_l11; /* sub */
    gm_toe5fmr3vvf4d gm_l12; /* e */
    GM_STACK_CHECK(&F, 1265u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_b16_8 t2 = gm_l0;
        gm_node *pn3 = gm_find(cx, 2126215853);
        if (!pn3) gm_trap("no handler installed for effect `Fs`", gm_srcdo1hnokt0mq8, 317, 17);
        gm_tih6h4avv5nrb t4;
        ((void (*)(void *, void *, gm_node *, gm_node *, gm_b16_8))pn3->clauses[13])((void *)&t4, pn3->env, pn3->hctx, pn3, t2);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t4))[0] != 0)) {
            gm_toe5fmr3vvf4d t5 = GM_LD(gm_toe5fmr3vvf4d, (((uint8_t *)&t4) + 8));
            gm_ttln0d0hd70en t6;
            GM_ST(uint8_t, (uint8_t *)&t6 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t6 + 2, 0);
            ((uint8_t *)&t6)[0] = 1;
            GM_ST(gm_toe5fmr3vvf4d, ((uint8_t *)&t6 + 4), t5);
            F.ret = t6;
            goto gm_fret;
        }
        gm_l1 = (*(gm_t5j14m8ru132p *)(((uint8_t *)&t4) + 8));
    }
    {
        uint8_t t8 = (!(GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 8) + 0) == ((uint8_t)1ULL)));
        if (!t8) goto ie7;
        {
            gm_b16_8 t10 = gm_l0;
            gm_node *pn11 = gm_find(cx, 2126215853);
            if (!pn11) gm_trap("no handler installed for effect `Fs`", gm_srcdo1hnokt0mq8, 319, 16);
            gm_ttln0d0hd70en t12;
            t12 = ((gm_ttln0d0hd70en (*)(void *, gm_node *, gm_node *, gm_b16_8))pn11->clauses[6])(pn11->env, pn11->hctx, pn11, t10);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t13 = ((((uint8_t *)&t12))[0] != 0);
            F.ret = t12;
            goto gm_fret;
        }
        goto ix7;
ie7:;
ix7:;
    }
    {
        gm_f_list_uvfu1k1((void *)&F.l2, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
    }
    {
        gm_f_list_g3vmkan((void *)&F.l3, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_ld1_1;
        }
    }
    {
    }
    {
        void * t17 = (void *)((uint8_t *)&F.l2);
        gm_b16_8 t18 = gm_l0;
        gm_tftb1odc739t5 t19 = gm_f_extend_0cs85gs(t17, t18);
        gm_b0_1 t20;
        if (!((((uint8_t *)&t19))[0] != 0)) {
            gm_b0_1 t21 = GM_LD(gm_b0_1, (((uint8_t *)&t19) + 1));
            t20 = t21;
        } else {
            gm_toe5fmr3vvf4d t22;
            GM_ST(uint8_t, (uint8_t *)&t22 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t22 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t22 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t22 + 0), ((uint8_t)9ULL));
            gm_ttln0d0hd70en t23;
            GM_ST(uint8_t, (uint8_t *)&t23 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t23 + 2, 0);
            ((uint8_t *)&t23)[0] = 1;
            GM_ST(gm_toe5fmr3vvf4d, ((uint8_t *)&t23 + 4), t22);
            uint8_t t24 = ((((uint8_t *)&t23))[0] != 0);
            F.ret = t23;
            gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = t24;
            goto gm_ld1_2;
        }
    }
    {
        gm_b16_8 t25 = gm_l0;
        gm_node *pn26 = gm_find(cx, 2126215853);
        if (!pn26) gm_trap("no handler installed for effect `Fs`", gm_srcdo1hnokt0mq8, 326, 18);
        gm_t62ebbi2i5rkd t27;
        t27 = ((gm_t62ebbi2i5rkd (*)(void *, gm_node *, gm_node *, gm_b16_8))pn26->clauses[10])(pn26->env, pn26->hctx, pn26, t25);
        if (gm_unw.kind) {
            goto gm_ld1_2;
        }
        if (((((uint8_t *)&t27))[0] != 0)) {
            gm_toe5fmr3vvf4d t28 = GM_LD(gm_toe5fmr3vvf4d, (((uint8_t *)&t27) + 8));
            gm_ttln0d0hd70en t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_toe5fmr3vvf4d, ((uint8_t *)&t29 + 4), t28);
            F.ret = t29;
            gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = 1;
            goto gm_ld1_2;
        }
        gm_tm98ap4112hee t30 = GM_LD(gm_tm98ap4112hee, (((uint8_t *)&t27) + 8));
        gm_l4 = t30;
    }
    {
        void * t31 = (void *)((uint8_t *)&F.l3);
        gm_tm98ap4112hee t32 = gm_l4;
        uint64_t t33 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        gm_t5nmvq97ncshv t34;
        GM_ST(gm_tm98ap4112hee, ((uint8_t *)&t34 + 0), t32);
        GM_ST(uint64_t, ((uint8_t *)&t34 + 8), t33);
        gm_tftb1odc739t5 t35 = gm_f_push_8h1r7s6(t31, GM_CONV(gm_cskqtm7tpd0qif, t34));
        gm_b0_1 t36;
        if (!((((uint8_t *)&t35))[0] != 0)) {
            gm_b0_1 t37 = GM_LD(gm_b0_1, (((uint8_t *)&t35) + 1));
            t36 = t37;
        } else {
            {
                gm_tm98ap4112hee t39 = gm_l4;
                gm_node *pn40 = gm_find(cx, 2126215853);
                if (!pn40) gm_trap("no handler installed for effect `Fs`", gm_srcdo1hnokt0mq8, 328, 9);
                gm_b0_1 t41;
                t41 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, gm_tm98ap4112hee))pn40->clauses[12])(pn40->env, pn40->hctx, pn40, t39);
                if (gm_unw.kind) {
                    goto gm_ld1_2;
                }
            }
            gm_toe5fmr3vvf4d t42;
            GM_ST(uint8_t, (uint8_t *)&t42 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t42 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t42 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t42 + 0), ((uint8_t)9ULL));
            gm_ttln0d0hd70en t43;
            GM_ST(uint8_t, (uint8_t *)&t43 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t43 + 2, 0);
            ((uint8_t *)&t43)[0] = 1;
            GM_ST(gm_toe5fmr3vvf4d, ((uint8_t *)&t43 + 4), t42);
            uint8_t t44 = ((((uint8_t *)&t43))[0] != 0);
            F.ret = t43;
            gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = t44;
            goto gm_ld1_2;
        }
    }
    {
    }
    {
        for (;;) {
            uint64_t t46 = GM_LD(uintptr_t, (((uint8_t *)&F.l3) + 0) + 8);
            uint8_t t47 = (t46 > ((uint64_t)0ULL));
            if (!t47) goto lb45;
            {
                uint64_t t50 = GM_LD(uintptr_t, (((uint8_t *)&F.l3) + 0) + 8);
                uint64_t r51; bool o52 = __builtin_sub_overflow(t50, ((uint64_t)1ULL), &r51);
                if (o52) gm_trap("integer overflow in `-`", gm_srcdo1hnokt0mq8, 333, 29);
                uint64_t t53 = r51;
                uint64_t t54 = (uintptr_t)t53;
                uint64_t t55 = GM_LD(uintptr_t, (((uint8_t *)&F.l3) + 0) + 8);
                if (t54 >= (uintptr_t)(t55)) gm_trap_index(t54, t55, gm_srcdo1hnokt0mq8, 333, 16);
                gm_t5nmvq97ncshv t56 = GM_LD(gm_t5nmvq97ncshv, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l3) + 0)) + t54 * 16));
                gm_l6 = t56;
            }
            {
                gm_tm98ap4112hee t57 = GM_LD(gm_tm98ap4112hee, (((uint8_t *)&gm_l6) + 0));
                void * t58 = ((uint8_t *)&F.l5);
                if ((uintptr_t)0 > (uintptr_t)256 || (uintptr_t)256 > (uintptr_t)256) gm_trap_slice((uintptr_t)0, (uintptr_t)256, 256, gm_srcdo1hnokt0mq8, 334, 40);
                gm_b16_8 t59;
                GM_ST(void *, (uint8_t *)&t59, (void *)(((uint8_t *)t58 + (uintptr_t)0 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t59 + 8, (uintptr_t)((uintptr_t)256 - (uintptr_t)0));
                gm_node *pn60 = gm_find(cx, 2126215853);
                if (!pn60) gm_trap("no handler installed for effect `Fs`", gm_srcdo1hnokt0mq8, 334, 21);
                gm_tiuq7k5hm0kak t61;
                ((void (*)(void *, void *, gm_node *, gm_node *, gm_tm98ap4112hee, gm_b16_8))pn60->clauses[11])((void *)&t61, pn60->env, pn60->hctx, pn60, t57, t59);
                if (gm_unw.kind) {
                    goto gm_ll48;
                }
                if (((((uint8_t *)&t61))[0] != 0)) {
                    gm_toe5fmr3vvf4d t62 = GM_LD(gm_toe5fmr3vvf4d, (((uint8_t *)&t61) + 8));
                    gm_ttln0d0hd70en t63;
                    GM_ST(uint8_t, (uint8_t *)&t63 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t63 + 2, 0);
                    ((uint8_t *)&t63)[0] = 1;
                    GM_ST(gm_toe5fmr3vvf4d, ((uint8_t *)&t63 + 4), t62);
                    F.ret = t63;
                    gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = 1;
                    goto gm_ll48;
                }
                gm_l7 = (*(gm_t11t9d8s5diq9 *)(((uint8_t *)&t61) + 8));
            }
            {
                gm_t11t9d8s5diq9 t64 = gm_l7;
                gm_t617vr72bd7b3 t65;
                if (((((uint8_t *)&t64))[16] != 0)) {
                    gm_t617vr72bd7b3 t66 = GM_LD(gm_t617vr72bd7b3, (((uint8_t *)&t64) + 0));
                    t65 = t66;
                } else {
                    {
                        gm_tm98ap4112hee t68 = GM_LD(gm_tm98ap4112hee, (((uint8_t *)&gm_l6) + 0));
                        gm_node *pn69 = gm_find(cx, 2126215853);
                        if (!pn69) gm_trap("no handler installed for effect `Fs`", gm_srcdo1hnokt0mq8, 337, 13);
                        gm_b0_1 t70;
                        t70 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, gm_tm98ap4112hee))pn69->clauses[12])(pn69->env, pn69->hctx, pn69, t68);
                        if (gm_unw.kind) {
                            goto gm_ll48;
                        }
                    }
                    {
                        {
                            void * t72 = (void *)((uint8_t *)&F.l3);
                            gm_tsbj0adhq3qho t73;
                            gm_f_pop_f3uajlg((void *)&t73, t72);
                        }
                    }
                    {
                        void * t74 = (void *)((uint8_t *)&F.l2);
                        uint64_t t75 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                        gm_f_truncate_ov4pgoq(t74, t75);
                    }
                    {
                        gm_b16_8 t76 = GM_LD(gm_b16_8, (((uint8_t *)&F.l2) + 0));
                        gm_b16_8 t77 = GM_LD(gm_b16_8, ((uint8_t *)&t76));
                        gm_node *pn78 = gm_find(cx, 2126215853);
                        if (!pn78) gm_trap("no handler installed for effect `Fs`", gm_srcdo1hnokt0mq8, 340, 13);
                        gm_ttln0d0hd70en t79;
                        t79 = ((gm_ttln0d0hd70en (*)(void *, gm_node *, gm_node *, gm_b16_8))pn78->clauses[9])(pn78->env, pn78->hctx, pn78, t77);
                        if (gm_unw.kind) {
                            goto gm_ll48;
                        }
                        gm_b0_1 t80;
                        if (!((((uint8_t *)&t79))[0] != 0)) {
                            gm_b0_1 t81 = GM_LD(gm_b0_1, (((uint8_t *)&t79) + 4));
                            t80 = t81;
                        } else {
                            gm_toe5fmr3vvf4d t82 = GM_LD(gm_toe5fmr3vvf4d, (((uint8_t *)&t79) + 4));
                            gm_l8 = t82;
                            uint8_t t85 = (!(GM_LD(uint8_t, ((uint8_t *)&gm_l8) + 0) == ((uint8_t)0ULL)));
                            if (!t85) goto ie84;
                            {
                                gm_toe5fmr3vvf4d t87 = gm_l8;
                                gm_ttln0d0hd70en t88;
                                GM_ST(uint8_t, (uint8_t *)&t88 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t88 + 2, 0);
                                ((uint8_t *)&t88)[0] = 1;
                                GM_ST(gm_toe5fmr3vvf4d, ((uint8_t *)&t88 + 4), t87);
                                uint8_t t89 = ((((uint8_t *)&t88))[0] != 0);
                                F.ret = t88;
                                gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = t89;
                                goto gm_ll48;
                            }
                            goto ix84;
ie84:;
ix84:;
                            gm_b0_1 t90 = GM_ZERO(gm_b0_1);
                            t80 = t90;
                        }
                    }
                    goto lc45;
                }
                gm_l9 = t65;
            }
            {
                void * t91 = (void *)((uint8_t *)&F.l2);
                uint64_t t92 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                gm_f_truncate_ov4pgoq(t91, t92);
            }
            {
                uint64_t t94 = GM_LD(uintptr_t, (((uint8_t *)&F.l2) + 0) + 8);
                uint8_t t95 = (t94 > ((uint64_t)0ULL));
                if (!t95) goto ie93;
                uint64_t t96 = GM_LD(uintptr_t, (((uint8_t *)&F.l2) + 0) + 8);
                uint64_t r97; bool o98 = __builtin_sub_overflow(t96, ((uint64_t)1ULL), &r97);
                if (o98) gm_trap("integer overflow in `-`", gm_srcdo1hnokt0mq8, 346, 44);
                uint64_t t99 = r97;
                uint64_t t100 = (uintptr_t)t99;
                uint64_t t101 = GM_LD(uintptr_t, (((uint8_t *)&F.l2) + 0) + 8);
                if (t100 >= (uintptr_t)(t101)) gm_trap_index(t100, t101, gm_srcdo1hnokt0mq8, 346, 34);
                uint8_t t102 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l2) + 0)) + t100 * 1));
                uint8_t t103 = (!(t102 == ((uint8_t)47ULL)));
                if (!t103) goto ie93;
                {
                    void * t105 = (void *)((uint8_t *)&F.l2);
                    gm_tftb1odc739t5 t106 = gm_f_push_t83rg2m(t105, ((uint8_t)47ULL));
                    gm_b0_1 t107;
                    if (!((((uint8_t *)&t106))[0] != 0)) {
                        gm_b0_1 t108 = GM_LD(gm_b0_1, (((uint8_t *)&t106) + 1));
                        t107 = t108;
                    } else {
                        gm_toe5fmr3vvf4d t109;
                        GM_ST(uint8_t, (uint8_t *)&t109 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t109 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t109 + 4, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t109 + 0), ((uint8_t)9ULL));
                        gm_ttln0d0hd70en t110;
                        GM_ST(uint8_t, (uint8_t *)&t110 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t110 + 2, 0);
                        ((uint8_t *)&t110)[0] = 1;
                        GM_ST(gm_toe5fmr3vvf4d, ((uint8_t *)&t110 + 4), t109);
                        uint8_t t111 = ((((uint8_t *)&t110))[0] != 0);
                        F.ret = t110;
                        gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = t111;
                        goto gm_ll48;
                    }
                }
                goto ix93;
ie93:;
ix93:;
            }
            {
                void * t112 = (void *)((uint8_t *)&F.l2);
                void * t113 = ((uint8_t *)&F.l5);
                uint64_t t114 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t115 = GM_LD(uint64_t, (((uint8_t *)&gm_l9) + 0));
                uint64_t t116 = (uintptr_t)t115;
                if (t114 > t116 || t116 > (uintptr_t)256) gm_trap_slice(t114, t116, 256, gm_srcdo1hnokt0mq8, 349, 20);
                gm_b16_8 t117;
                GM_ST(void *, (uint8_t *)&t117, (void *)(((uint8_t *)t113 + t114 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t117 + 8, (uintptr_t)(t116 - t114));
                gm_b16_8 t118 = GM_LD(gm_b16_8, ((uint8_t *)&t117));
                gm_tftb1odc739t5 t119 = gm_f_extend_0cs85gs(t112, t118);
                gm_b0_1 t120;
                if (!((((uint8_t *)&t119))[0] != 0)) {
                    gm_b0_1 t121 = GM_LD(gm_b0_1, (((uint8_t *)&t119) + 1));
                    t120 = t121;
                } else {
                    gm_toe5fmr3vvf4d t122;
                    GM_ST(uint8_t, (uint8_t *)&t122 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t122 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t122 + 4, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t122 + 0), ((uint8_t)9ULL));
                    gm_ttln0d0hd70en t123;
                    GM_ST(uint8_t, (uint8_t *)&t123 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t123 + 2, 0);
                    ((uint8_t *)&t123)[0] = 1;
                    GM_ST(gm_toe5fmr3vvf4d, ((uint8_t *)&t123 + 4), t122);
                    uint8_t t124 = ((((uint8_t *)&t123))[0] != 0);
                    F.ret = t123;
                    gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = t124;
                    goto gm_ll48;
                }
            }
            {
                uint8_t t126 = ((GM_LD(uint8_t, (((uint8_t *)&gm_l9) + 8) + 0) == ((uint8_t)1ULL)));
                if (!t126) goto ie125;
                {
                    gm_b16_8 t128 = GM_LD(gm_b16_8, (((uint8_t *)&F.l2) + 0));
                    gm_b16_8 t129 = GM_LD(gm_b16_8, ((uint8_t *)&t128));
                    gm_node *pn130 = gm_find(cx, 2126215853);
                    if (!pn130) gm_trap("no handler installed for effect `Fs`", gm_srcdo1hnokt0mq8, 351, 20);
                    gm_t62ebbi2i5rkd t131;
                    t131 = ((gm_t62ebbi2i5rkd (*)(void *, gm_node *, gm_node *, gm_b16_8))pn130->clauses[10])(pn130->env, pn130->hctx, pn130, t129);
                    if (gm_unw.kind) {
                        goto gm_ll48;
                    }
                    gm_tm98ap4112hee t132;
                    if (!((((uint8_t *)&t131))[0] != 0)) {
                        gm_tm98ap4112hee t133 = GM_LD(gm_tm98ap4112hee, (((uint8_t *)&t131) + 8));
                        t132 = t133;
                    } else {
                        gm_toe5fmr3vvf4d t134 = GM_LD(gm_toe5fmr3vvf4d, (((uint8_t *)&t131) + 8));
                        gm_l10 = t134;
                        {
                            uint8_t t137 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l10) + 0) == ((uint8_t)0ULL)));
                            if (!t137) goto ie136;
                            {
                                goto lc45;
                            }
                            goto ix136;
ie136:;
ix136:;
                        }
                        gm_toe5fmr3vvf4d t139 = gm_l10;
                        gm_ttln0d0hd70en t140;
                        GM_ST(uint8_t, (uint8_t *)&t140 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t140 + 2, 0);
                        ((uint8_t *)&t140)[0] = 1;
                        GM_ST(gm_toe5fmr3vvf4d, ((uint8_t *)&t140 + 4), t139);
                        uint8_t t141 = ((((uint8_t *)&t140))[0] != 0);
                        F.ret = t140;
                        gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = t141;
                        goto gm_ll48;
                    }
                    gm_l11 = t132;
                }
                {
                    void * t142 = (void *)((uint8_t *)&F.l3);
                    gm_tm98ap4112hee t143 = gm_l11;
                    uint64_t t144 = GM_LD(uintptr_t, (((uint8_t *)&F.l2) + 0) + 8);
                    gm_t5nmvq97ncshv t145;
                    GM_ST(gm_tm98ap4112hee, ((uint8_t *)&t145 + 0), t143);
                    GM_ST(uint64_t, ((uint8_t *)&t145 + 8), t144);
                    gm_tftb1odc739t5 t146 = gm_f_push_8h1r7s6(t142, GM_CONV(gm_cskqtm7tpd0qif, t145));
                    gm_b0_1 t147;
                    if (!((((uint8_t *)&t146))[0] != 0)) {
                        gm_b0_1 t148 = GM_LD(gm_b0_1, (((uint8_t *)&t146) + 1));
                        t147 = t148;
                    } else {
                        {
                            gm_tm98ap4112hee t150 = gm_l11;
                            gm_node *pn151 = gm_find(cx, 2126215853);
                            if (!pn151) gm_trap("no handler installed for effect `Fs`", gm_srcdo1hnokt0mq8, 357, 17);
                            gm_b0_1 t152;
                            t152 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, gm_tm98ap4112hee))pn151->clauses[12])(pn151->env, pn151->hctx, pn151, t150);
                            if (gm_unw.kind) {
                                goto gm_ll48;
                            }
                        }
                        gm_toe5fmr3vvf4d t153;
                        GM_ST(uint8_t, (uint8_t *)&t153 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t153 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t153 + 4, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t153 + 0), ((uint8_t)9ULL));
                        gm_ttln0d0hd70en t154;
                        GM_ST(uint8_t, (uint8_t *)&t154 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t154 + 2, 0);
                        ((uint8_t *)&t154)[0] = 1;
                        GM_ST(gm_toe5fmr3vvf4d, ((uint8_t *)&t154 + 4), t153);
                        uint8_t t155 = ((((uint8_t *)&t154))[0] != 0);
                        F.ret = t154;
                        gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = t155;
                        goto gm_ll48;
                    }
                }
                goto ix125;
ie125:;
                {
                    gm_b16_8 t157 = GM_LD(gm_b16_8, (((uint8_t *)&F.l2) + 0));
                    gm_b16_8 t158 = GM_LD(gm_b16_8, ((uint8_t *)&t157));
                    gm_node *pn159 = gm_find(cx, 2126215853);
                    if (!pn159) gm_trap("no handler installed for effect `Fs`", gm_srcdo1hnokt0mq8, 361, 13);
                    gm_ttln0d0hd70en t160;
                    t160 = ((gm_ttln0d0hd70en (*)(void *, gm_node *, gm_node *, gm_b16_8))pn159->clauses[6])(pn159->env, pn159->hctx, pn159, t158);
                    if (gm_unw.kind) {
                        goto gm_ll48;
                    }
                    gm_b0_1 t161;
                    if (!((((uint8_t *)&t160))[0] != 0)) {
                        gm_b0_1 t162 = GM_LD(gm_b0_1, (((uint8_t *)&t160) + 4));
                        t161 = t162;
                    } else {
                        gm_toe5fmr3vvf4d t163 = GM_LD(gm_toe5fmr3vvf4d, (((uint8_t *)&t160) + 4));
                        gm_l12 = t163;
                        uint8_t t166 = (!(GM_LD(uint8_t, ((uint8_t *)&gm_l12) + 0) == ((uint8_t)0ULL)));
                        if (!t166) goto ie165;
                        {
                            gm_toe5fmr3vvf4d t168 = gm_l12;
                            gm_ttln0d0hd70en t169;
                            GM_ST(uint8_t, (uint8_t *)&t169 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t169 + 2, 0);
                            ((uint8_t *)&t169)[0] = 1;
                            GM_ST(gm_toe5fmr3vvf4d, ((uint8_t *)&t169 + 4), t168);
                            uint8_t t170 = ((((uint8_t *)&t169))[0] != 0);
                            F.ret = t169;
                            gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = t170;
                            goto gm_ll48;
                        }
                        goto ix165;
ie165:;
ix165:;
                        gm_b0_1 t171 = GM_ZERO(gm_b0_1);
                        t161 = t171;
                    }
                }
ix125:;
            }
lc45:;
        }
gm_ll48:;
        if (gm_unw.frame == &F.h && gm_unw.label == 0) {
            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb45; }
            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc45; }
        }
        goto gm_ld1_2;
lb45:;
    }
    {
        gm_b0_1 t172;
        gm_ttln0d0hd70en t173;
        GM_ST(uint8_t, (uint8_t *)&t173 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t173 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t173 + 4, 0);
        GM_ST(uint32_t, (uint8_t *)&t173 + 8, 0);
        ((uint8_t *)&t173)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t173 + 4), t172);
        uint8_t t174 = ((((uint8_t *)&t173))[0] != 0);
        F.ret = t173;
        gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = t174;
        goto gm_ld1_2;
    }
    {
        void * t175 = (void *)((uint8_t *)&F.l3);
        gm_b0_1 t176 = gm_f_close_frames_46uf1s1((void *)NULL, cx, NULL, t175);
    }
    {
        void * t177 = (void *)((uint8_t *)&F.l2);
        gm_f_deinit_r70pji5(t177);
    }
    goto gm_ls178;
gm_ld1_2:;
    {
        gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
        void * t179 = (void *)((uint8_t *)&F.l3);
        gm_b0_1 t180 = gm_f_close_frames_46uf1s1((void *)NULL, cx, NULL, t179);
        gm_unw = gm_saved;
    }
gm_ld1_1:;
    {
        gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
        void * t181 = (void *)((uint8_t *)&F.l2);
        gm_f_deinit_r70pji5(t181);
        gm_unw = gm_saved;
    }
    goto gm_lad;
gm_ls178:;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_temp_dir_i6apmsd(void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_temp_dir_i6apmsd F;
    gm_b16_8 gm_l0; /* v */
    GM_STACK_CHECK(&F, 291u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        {
            gm_b16_8 t3;
            GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s4j8rhn4svm30));
            GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(6));
            gm_node *pn4 = gm_find(cx, 621690230);
            if (!pn4) gm_trap("no handler installed for effect `Env`", gm_srcdo1hnokt0mq8, 392, 14);
            gm_th2lqbo77pvsc t5;
            if (pn4->kind == GM_NATIVE) {
                const uint8_t *ev; size_t evn;
                bool evok = gm_env_var(GM_LD(void *, &t3), GM_LD(uintptr_t, (uint8_t *)&t3 + 8), &ev, &evn);
                gm_b16_8 t6;
                GM_ST(void *, (uint8_t *)&t6, (void *)(ev));
                GM_ST(uintptr_t, (uint8_t *)&t6 + 8, (uintptr_t)(evn));
                gm_th2lqbo77pvsc t7;
                GM_ST(uint8_t, (uint8_t *)&t7 + 17, 0);
                GM_ST(uint16_t, (uint8_t *)&t7 + 18, 0);
                GM_ST(uint32_t, (uint8_t *)&t7 + 20, 0);
                GM_ST(gm_b16_8, ((uint8_t *)&t7), t6);
                ((uint8_t *)&t7)[16] = 1;
                gm_th2lqbo77pvsc t8;
                memset(&t8, 0, sizeof t8);
                t5 = evok ? t7 : t8;
            } else {
                ((void (*)(void *, void *, gm_node *, gm_node *, gm_b16_8))pn4->clauses[1])((void *)&t5, pn4->env, pn4->hctx, pn4, t3);
            }
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_b16_8 t9;
            if (((((uint8_t *)&t5))[16] != 0)) {
                gm_b16_8 t10 = GM_LD(gm_b16_8, (((uint8_t *)&t5) + 0));
                t9 = t10;
            } else {
                gm_b16_8 t11;
                GM_ST(void *, (uint8_t *)&t11, (void *)(gm_sp3jmpmb6sldf));
                GM_ST(uintptr_t, (uint8_t *)&t11 + 8, (uintptr_t)(4));
                uint8_t t12 = 0;
                F.ret = t11;
                goto gm_fret;
            }
            gm_l0 = t9;
        }
        {
            gm_b16_8 t13;
            uint64_t t15 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            uint8_t t16 = (t15 > ((uint64_t)0ULL));
            if (!t16) goto ie14;
            gm_b16_8 t18 = gm_l0;
            gm_b16_8 t19 = t18;
            t13 = t19;
            goto ix14;
ie14:;
            gm_b16_8 t21;
            GM_ST(void *, (uint8_t *)&t21, (void *)(gm_sp3jmpmb6sldf));
            GM_ST(uintptr_t, (uint8_t *)&t21 + 8, (uintptr_t)(4));
            gm_b16_8 t22 = t21;
            t13 = t22;
ix14:;
            uint8_t t23 = 0;
            F.ret = t13;
            goto gm_fret;
        }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_temp_random_aie9j36(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, uint64_t a1) {
    struct gm_fr_temp_random_aie9j36 F;
    gm_b16_8 gm_l0; /* buf */
    uint64_t gm_l1; /* at */
    gm_t2doi72kmmlil gm_l2; /* r */
    uint64_t gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    uint64_t gm_l5; /* i */
    GM_STACK_CHECK(&F, 287u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
    }
    {
        void * t2 = ((uint8_t *)&gm_l2);
        if ((uintptr_t)0 > (uintptr_t)8 || (uintptr_t)8 > (uintptr_t)8) gm_trap_slice((uintptr_t)0, (uintptr_t)8, 8, gm_srcdo1hnokt0mq8, 425, 15);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(((uint8_t *)t2 + (uintptr_t)0 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)((uintptr_t)8 - (uintptr_t)0));
        gm_node *pn4 = gm_find(cx, 1087599154);
        if (!pn4) gm_trap("no handler installed for effect `Rand`", gm_srcdo1hnokt0mq8, 425, 5);
        gm_b0_1 t5;
        t5 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, gm_b16_8))pn4->clauses[0])(pn4->env, pn4->hctx, pn4, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_l4 = ((uint64_t)0ULL);
        }
        {
            gm_l3 = ((uint64_t)8ULL);
        }
        {
            for (;;) {
                uint64_t t8 = gm_l4;
                uint64_t t9 = gm_l3;
                uint8_t t10 = (t8 < t9);
                if (!t10) goto lb7;
                {
                    uint64_t t13 = gm_l4;
                    uint64_t t16 = GM_LD(uint64_t, ((uint8_t *)&t13));
                    gm_l5 = t16;
                    {
                        uint64_t t18 = gm_l1;
                        uint64_t t19 = gm_l5;
                        uint64_t r20; bool o21 = __builtin_mul_overflow(((uint64_t)2ULL), t19, &r20);
                        if (o21) gm_trap("integer overflow in `*`", gm_srcdo1hnokt0mq8, 427, 18);
                        uint64_t t22 = r20;
                        uint64_t r23; bool o24 = __builtin_add_overflow(t18, t22, &r23);
                        if (o24) gm_trap("integer overflow in `+`", gm_srcdo1hnokt0mq8, 427, 13);
                        uint64_t t25 = r23;
                        uint64_t t26 = (uintptr_t)t25;
                        uint64_t t27 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                        if (t26 >= (uintptr_t)(t27)) gm_trap_index(t26, t27, gm_srcdo1hnokt0mq8, 427, 9);
                        uint8_t *t28 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t26 * 1);
                        uint64_t t29 = gm_l5;
                        uint64_t t30 = (uintptr_t)t29;
                        if (t30 >= (uintptr_t)(8)) gm_trap_index(t30, 8, gm_srcdo1hnokt0mq8, 427, 37);
                        uint8_t t31 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + t30 * 1));
                        unsigned __int128 n32 = (unsigned __int128)((uint64_t)4ULL);
                        if (n32 >= 8) gm_trap_shift((uint64_t)n32, "u8", gm_srcdo1hnokt0mq8, 427, 37);
                        uint8_t t33 = (uint8_t)(t31 >> (int)n32);
                        uint8_t t34 = gm_f_hex_digit_fdbg8g2(t33);
                        GM_ST(uint8_t, t28, t34);
                    }
                    {
                        uint64_t t35 = gm_l1;
                        uint64_t t36 = gm_l5;
                        uint64_t r37; bool o38 = __builtin_mul_overflow(((uint64_t)2ULL), t36, &r37);
                        if (o38) gm_trap("integer overflow in `*`", gm_srcdo1hnokt0mq8, 428, 18);
                        uint64_t t39 = r37;
                        uint64_t r40; bool o41 = __builtin_add_overflow(t35, t39, &r40);
                        if (o41) gm_trap("integer overflow in `+`", gm_srcdo1hnokt0mq8, 428, 13);
                        uint64_t t42 = r40;
                        uint64_t r43; bool o44 = __builtin_add_overflow(t42, ((uint64_t)1ULL), &r43);
                        if (o44) gm_trap("integer overflow in `+`", gm_srcdo1hnokt0mq8, 428, 13);
                        uint64_t t45 = r43;
                        uint64_t t46 = (uintptr_t)t45;
                        uint64_t t47 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                        if (t46 >= (uintptr_t)(t47)) gm_trap_index(t46, t47, gm_srcdo1hnokt0mq8, 428, 9);
                        uint8_t *t48 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t46 * 1);
                        uint64_t t49 = gm_l5;
                        uint64_t t50 = (uintptr_t)t49;
                        if (t50 >= (uintptr_t)(8)) gm_trap_index(t50, 8, gm_srcdo1hnokt0mq8, 428, 41);
                        uint8_t t51 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + t50 * 1));
                        uint8_t t52 = (uint8_t)(t51 & ((uint8_t)15ULL));
                        uint8_t t53 = gm_f_hex_digit_fdbg8g2(t52);
                        GM_ST(uint8_t, t48, t53);
                    }
                    goto me14;
mn15:;
                    gm_trap("no match arm matched the scrutinee", gm_srcdo1hnokt0mq8, 426, 5);
me14:;
                }
lc7:;
                {
                    uint64_t t55 = gm_l4;
                    uint64_t r56; bool o57 = __builtin_add_overflow(t55, ((uint64_t)1ULL), &r56);
                    uint64_t t58 = r56;
                    gm_l4 = t58;
                }
            }
lb7:;
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
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_temp_stem_3b7s4ll(gm_b16_8 a0, gm_b16_8 a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_temp_stem_3b7s4ll F;
    gm_b16_8 gm_l0; /* dir */
    gm_b16_8 gm_l1; /* prefix */
    gm_b16_8 gm_l2; /* buf */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    uint8_t gm_l5; /* c */
    uint64_t gm_l6; /* sep */
    uint64_t gm_l7; /* n */
    GM_STACK_CHECK_SHALLOW(&F, 591u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        {
            gm_b16_8 t3 = gm_l1;
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
                    uint64_t t11 = (uintptr_t)t10;
                    uint64_t t12 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                    uint8_t t13 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t11 * 1));
                    uint8_t t16 = GM_LD(uint8_t, ((uint8_t *)&t13));
                    gm_l5 = t16;
                    {
                        uint8_t t19 = gm_l5;
                        uint8_t t20 = gm_f_is_sep_h2f3b7l(t19);
                        if (!t20) goto ie18;
                        {
                            gm_tf5srh4mp4b3i t22;
                            memset(&t22, 0, sizeof t22);
                            uint8_t t23 = 0;
                            F.ret = t22;
                            goto gm_fret;
                        }
                        goto ix18;
ie18:;
ix18:;
                    }
                    goto me14;
mn15:;
                    gm_trap("no match arm matched the scrutinee", gm_srcdo1hnokt0mq8, 410, 5);
me14:;
                }
lc4:;
                {
                    uint64_t t25 = gm_l4;
                    uint64_t r26; bool o27 = __builtin_add_overflow(t25, ((uint64_t)1ULL), &r26);
                    uint64_t t28 = r26;
                    gm_l4 = t28;
                }
            }
lb4:;
        }
    }
    {
        uint64_t t29;
        uint64_t t31 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        uint8_t t32 = (t31 > ((uint64_t)0ULL));
        if (!t32) goto ie30;
        uint64_t t33 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        uint64_t r34; bool o35 = __builtin_sub_overflow(t33, ((uint64_t)1ULL), &r34);
        if (o35) gm_trap("integer overflow in `-`", gm_srcdo1hnokt0mq8, 413, 52);
        uint64_t t36 = r34;
        uint64_t t37 = (uintptr_t)t36;
        uint64_t t38 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        if (t37 >= (uintptr_t)(t38)) gm_trap_index(t37, t38, gm_srcdo1hnokt0mq8, 413, 48);
        uint8_t t39 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t37 * 1));
        uint8_t t40 = gm_f_is_sep_h2f3b7l(t39);
        uint8_t t41 = (uint8_t)!t40;
        if (!t41) goto ie30;
        uint64_t t43 = ((uint64_t)1ULL);
        t29 = t43;
        goto ix30;
ie30:;
        uint64_t t45 = ((uint64_t)0ULL);
        t29 = t45;
ix30:;
        gm_l6 = t29;
    }
    {
        uint64_t t46 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        uint64_t t47 = gm_l6;
        uint64_t r48; bool o49 = __builtin_add_overflow(t46, t47, &r48);
        if (o49) gm_trap("integer overflow in `+`", gm_srcdo1hnokt0mq8, 414, 10);
        uint64_t t50 = r48;
        uint64_t t51 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint64_t r52; bool o53 = __builtin_add_overflow(t50, t51, &r52);
        if (o53) gm_trap("integer overflow in `+`", gm_srcdo1hnokt0mq8, 414, 10);
        uint64_t t54 = r52;
        gm_l7 = t54;
    }
    {
        uint64_t t56 = gm_l7;
        uint64_t r57; bool o58 = __builtin_add_overflow(t56, ((uint64_t)16ULL), &r57);
        if (o58) gm_trap("integer overflow in `+`", gm_srcdo1hnokt0mq8, 415, 8);
        uint64_t t59 = r57;
        uint64_t t60 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        uint8_t t61 = (t59 > t60);
        if (!t61) goto ie55;
        {
            gm_tf5srh4mp4b3i t63;
            memset(&t63, 0, sizeof t63);
            uint8_t t64 = 0;
            F.ret = t63;
            goto gm_fret;
        }
        goto ix55;
ie55:;
ix55:;
    }
    {
        uint64_t t65 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        void * t66 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)));
        uint64_t t67 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t68 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        uint64_t t69 = (uintptr_t)t68;
        if (t67 > t69 || t69 > (uintptr_t)t65) gm_trap_slice(t67, t69, t65, gm_srcdo1hnokt0mq8, 416, 5);
        gm_b16_8 t70;
        GM_ST(void *, (uint8_t *)&t70, (void *)(((uint8_t *)t66 + t67 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t70 + 8, (uintptr_t)(t69 - t67));
        gm_b16_8 t71 = gm_l0;
        gm_f_copy_from_f7j6p05(t70, t71);
    }
    {
        uint64_t t73 = gm_l6;
        uint8_t t74 = ((t73 == ((uint64_t)1ULL)));
        if (!t74) goto ie72;
        {
            uint64_t t76 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            uint64_t t77 = (uintptr_t)t76;
            uint64_t t78 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
            if (t77 >= (uintptr_t)(t78)) gm_trap_index(t77, t78, gm_srcdo1hnokt0mq8, 417, 19);
            GM_ST(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t77 * 1), ((uint8_t)47ULL));
        }
        goto ix72;
ie72:;
ix72:;
    }
    {
        uint64_t t79 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        void * t80 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)));
        uint64_t t81 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        uint64_t t82 = gm_l6;
        uint64_t r83; bool o84 = __builtin_add_overflow(t81, t82, &r83);
        if (o84) gm_trap("integer overflow in `+`", gm_srcdo1hnokt0mq8, 418, 9);
        uint64_t t85 = r83;
        uint64_t t86 = (uintptr_t)t85;
        uint64_t t87 = gm_l7;
        uint64_t t88 = (uintptr_t)t87;
        if (t86 > t88 || t88 > (uintptr_t)t79) gm_trap_slice(t86, t88, t79, gm_srcdo1hnokt0mq8, 418, 5);
        gm_b16_8 t89;
        GM_ST(void *, (uint8_t *)&t89, (void *)(((uint8_t *)t80 + t86 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t89 + 8, (uintptr_t)(t88 - t86));
        gm_b16_8 t90 = gm_l1;
        gm_f_copy_from_f7j6p05(t89, t90);
    }
    {
        uint64_t t91 = gm_l7;
        gm_tf5srh4mp4b3i t92;
        GM_ST(uint8_t, (uint8_t *)&t92 + 9, 0);
        GM_ST(uint16_t, (uint8_t *)&t92 + 10, 0);
        GM_ST(uint32_t, (uint8_t *)&t92 + 12, 0);
        GM_ST(uint64_t, ((uint8_t *)&t92), t91);
        ((uint8_t *)&t92)[8] = 1;
        uint8_t t93 = 0;
        F.ret = t92;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_ttln0d0hd70en gm_f_write_all_o8ucaji(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1) {
    struct gm_fr_write_all_o8ucaji F;
    gm_b16_8 gm_l0; /* path */
    gm_b16_8 gm_l1; /* bytes */
    gm_tm98ap4112hee gm_l2; /* f */
    GM_STACK_CHECK(&F, 191u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        gm_b16_8 t2 = gm_l0;
        gm_t4q2b5ibsrdik t3;
        GM_ST(uint8_t, ((uint8_t *)&t3 + 0), ((uint8_t)0));
        GM_ST(uint8_t, ((uint8_t *)&t3 + 1), ((uint8_t)1));
        GM_ST(uint8_t, ((uint8_t *)&t3 + 2), ((uint8_t)0));
        GM_ST(uint8_t, ((uint8_t *)&t3 + 3), ((uint8_t)1));
        GM_ST(uint8_t, ((uint8_t *)&t3 + 4), ((uint8_t)1));
        GM_ST(uint8_t, ((uint8_t *)&t3 + 5), ((uint8_t)0));
        gm_node *pn4 = gm_find(cx, 2126215853);
        if (!pn4) gm_trap("no handler installed for effect `Fs`", gm_srcdo1hnokt0mq8, 110, 14);
        gm_t62ebbi2i5rkd t5;
        t5 = ((gm_t62ebbi2i5rkd (*)(void *, gm_node *, gm_node *, gm_b16_8, gm_t4q2b5ibsrdik))pn4->clauses[0])(pn4->env, pn4->hctx, pn4, t2, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t5))[0] != 0)) {
            gm_toe5fmr3vvf4d t6 = GM_LD(gm_toe5fmr3vvf4d, (((uint8_t *)&t5) + 8));
            gm_ttln0d0hd70en t7;
            GM_ST(uint8_t, (uint8_t *)&t7 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t7 + 2, 0);
            ((uint8_t *)&t7)[0] = 1;
            GM_ST(gm_toe5fmr3vvf4d, ((uint8_t *)&t7 + 4), t6);
            F.ret = t7;
            goto gm_fret;
        }
        gm_tm98ap4112hee t8 = GM_LD(gm_tm98ap4112hee, (((uint8_t *)&t5) + 8));
        gm_l2 = t8;
    }
    {
    }
    {
        gm_tm98ap4112hee t12 = gm_l2;
        gm_b16_8 t13 = gm_l1;
        gm_ttln0d0hd70en t14 = gm_f_write_all_to_fe22ls9((void *)NULL, cx, NULL, t12, t13);
        if (gm_unw.kind) {
            goto gm_ld1_1;
        }
        uint8_t t15 = ((((uint8_t *)&t14))[0] != 0);
        F.ret = t14;
        gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = t15;
        goto gm_ld1_1;
    }
    {
        gm_tm98ap4112hee t16 = gm_l2;
        gm_node *pn17 = gm_find(cx, 2126215853);
        if (!pn17) gm_trap("no handler installed for effect `Fs`", gm_srcdo1hnokt0mq8, 111, 11);
        gm_b0_1 t18;
        t18 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, gm_tm98ap4112hee))pn17->clauses[1])(pn17->env, pn17->hctx, pn17, t16);
    }
    goto gm_ls19;
gm_ld1_1:;
    {
        gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
        gm_tm98ap4112hee t20 = gm_l2;
        gm_node *pn21 = gm_find(cx, 2126215853);
        if (!pn21) gm_trap("no handler installed for effect `Fs`", gm_srcdo1hnokt0mq8, 111, 11);
        gm_b0_1 t22;
        t22 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, gm_tm98ap4112hee))pn21->clauses[1])(pn21->env, pn21->hctx, pn21, t20);
        gm_unw = gm_saved;
    }
    goto gm_lad;
gm_ls19:;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_ttln0d0hd70en gm_f_write_all_to_fe22ls9(void *env, gm_node *cx, gm_node *inst, gm_tm98ap4112hee a0, gm_b16_8 a1) {
    struct gm_fr_write_all_to_fe22ls9 F;
    gm_tm98ap4112hee gm_l0; /* f */
    gm_b16_8 gm_l1; /* bytes */
    uint64_t gm_l2; /* done */
    uint64_t gm_l3; /* n */
    GM_STACK_CHECK(&F, 264u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        gm_l2 = ((uint64_t)0ULL);
    }
    {
        for (;;) {
            uint64_t t3 = gm_l2;
            uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            uint8_t t5 = (t3 < t4);
            if (!t5) goto lb2;
            {
                gm_tm98ap4112hee t8 = gm_l0;
                uint64_t t9 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                void * t10 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)));
                uint64_t t11 = gm_l2;
                uint64_t t12 = (uintptr_t)t11;
                uint64_t t13 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                uint64_t t14 = (uintptr_t)t13;
                if (t12 > t14 || t14 > (uintptr_t)t9) gm_trap_slice(t12, t14, t9, gm_srcdo1hnokt0mq8, 101, 26);
                gm_b16_8 t15;
                GM_ST(void *, (uint8_t *)&t15, (void *)(((uint8_t *)t10 + t12 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t15 + 8, (uintptr_t)(t14 - t12));
                gm_node *pn16 = gm_find(cx, 2126215853);
                if (!pn16) gm_trap("no handler installed for effect `Fs`", gm_srcdo1hnokt0mq8, 101, 18);
                gm_thf5kr0is9u9i t17;
                t17 = ((gm_thf5kr0is9u9i (*)(void *, gm_node *, gm_node *, gm_tm98ap4112hee, gm_b16_8))pn16->clauses[3])(pn16->env, pn16->hctx, pn16, t8, t15);
                if (gm_unw.kind) {
                    goto gm_ll6;
                }
                if (((((uint8_t *)&t17))[0] != 0)) {
                    gm_toe5fmr3vvf4d t18 = GM_LD(gm_toe5fmr3vvf4d, (((uint8_t *)&t17) + 8));
                    gm_ttln0d0hd70en t19;
                    GM_ST(uint8_t, (uint8_t *)&t19 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t19 + 2, 0);
                    ((uint8_t *)&t19)[0] = 1;
                    GM_ST(gm_toe5fmr3vvf4d, ((uint8_t *)&t19 + 4), t18);
                    F.ret = t19;
                    goto gm_fret;
                }
                uint64_t t20 = GM_LD(uint64_t, (((uint8_t *)&t17) + 8));
                gm_l3 = t20;
            }
            {
                uint64_t t22 = gm_l3;
                uint8_t t23 = ((t22 == ((uint64_t)0ULL)));
                if (!t23) goto ie21;
                {
                    gm_toe5fmr3vvf4d t25;
                    GM_ST(uint8_t, (uint8_t *)&t25 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t25 + 2, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t25 + 0), ((uint8_t)13ULL));
                    GM_ST(int32_t, ((uint8_t *)&t25 + 4), ((int32_t)0LL));
                    gm_ttln0d0hd70en t26;
                    GM_ST(uint8_t, (uint8_t *)&t26 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t26 + 2, 0);
                    ((uint8_t *)&t26)[0] = 1;
                    GM_ST(gm_toe5fmr3vvf4d, ((uint8_t *)&t26 + 4), t25);
                    uint8_t t27 = ((((uint8_t *)&t26))[0] != 0);
                    F.ret = t26;
                    goto gm_fret;
                }
                goto ix21;
ie21:;
ix21:;
            }
            {
                uint64_t t28 = gm_l2;
                uint64_t t29 = gm_l3;
                uint64_t r30; bool o31 = __builtin_add_overflow(t28, t29, &r30);
                if (o31) gm_trap("integer overflow in `+`", gm_srcdo1hnokt0mq8, 103, 9);
                uint64_t t32 = r30;
                gm_l2 = t32;
            }
lc2:;
        }
gm_ll6:;
        if (gm_unw.frame == &F.h && gm_unw.label == 0) {
            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb2; }
            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc2; }
        }
        goto gm_lad;
lb2:;
    }
    {
        gm_b0_1 t33;
        gm_ttln0d0hd70en t34;
        GM_ST(uint8_t, (uint8_t *)&t34 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t34 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t34 + 4, 0);
        GM_ST(uint32_t, (uint8_t *)&t34 + 8, 0);
        ((uint8_t *)&t34)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t34 + 4), t33);
        uint8_t t35 = ((((uint8_t *)&t34))[0] != 0);
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
__attribute__((aligned(4))) gm_ttln0d0hd70en gm_f_write_atomic_18k5vfb(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1) {
    struct gm_fr_write_atomic_18k5vfb F;
    gm_b16_8 gm_l0; /* path */
    gm_b16_8 gm_l1; /* bytes */
    gm_b16_8 gm_l3; /* tmp */
    gm_t4q2b5ibsrdik gm_l4; /* opts */
    uint32_t gm_l5; /* tries */
    gm_tm98ap4112hee gm_l6; /* f */
    gm_tpvg3mjn3dp06 gm_l7; /* r */
    uint64_t gm_l8; /* <tmp> */
    uint64_t gm_l9; /* <tmp> */
    uint64_t gm_l10; /* i */
    gm_toe5fmr3vvf4d gm_l11; /* e */
    gm_toe5fmr3vvf4d gm_l12; /* e */
    gm_toe5fmr3vvf4d gm_l13; /* e */
    GM_STACK_CHECK(&F, 1009u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        gm_b0_1 t2;
    }
    {
    }
    {
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        uint8_t t5 = (t4 > ((uint64_t)4000ULL));
        if (!t5) goto ie3;
        {
            gm_toe5fmr3vvf4d t7;
            GM_ST(uint8_t, (uint8_t *)&t7 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t7 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t7 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t7 + 0), ((uint8_t)7ULL));
            gm_ttln0d0hd70en t8;
            GM_ST(uint8_t, (uint8_t *)&t8 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t8 + 2, 0);
            ((uint8_t *)&t8)[0] = 1;
            GM_ST(gm_toe5fmr3vvf4d, ((uint8_t *)&t8 + 4), t7);
            uint8_t t9 = ((((uint8_t *)&t8))[0] != 0);
            F.ret = t8;
            goto gm_fret;
        }
        goto ix3;
ie3:;
ix3:;
    }
    {
        void * t10 = ((uint8_t *)&F.l2);
        uint64_t t11 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t12 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        uint64_t t13 = (uintptr_t)t12;
        if (t11 > t13 || t13 > (uintptr_t)4096) gm_trap_slice(t11, t13, 4096, gm_srcdo1hnokt0mq8, 489, 5);
        gm_b16_8 t14;
        GM_ST(void *, (uint8_t *)&t14, (void *)(((uint8_t *)t10 + t11 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t14 + 8, (uintptr_t)(t13 - t11));
        gm_b16_8 t15 = gm_l0;
        gm_f_copy_from_f7j6p05(t14, t15);
    }
    {
        void * t16 = ((uint8_t *)&F.l2);
        uint64_t t17 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t18 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        uint64_t r19; bool o20 = __builtin_add_overflow(t18, ((uint64_t)13ULL), &r19);
        if (o20) gm_trap("integer overflow in `+`", gm_srcdo1hnokt0mq8, 490, 20);
        uint64_t t21 = r19;
        uint64_t t22 = (uintptr_t)t21;
        if (t17 > t22 || t22 > (uintptr_t)4096) gm_trap_slice(t17, t22, 4096, gm_srcdo1hnokt0mq8, 490, 12);
        gm_b16_8 t23;
        GM_ST(void *, (uint8_t *)&t23, (void *)(((uint8_t *)t16 + t17 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t23 + 8, (uintptr_t)(t22 - t17));
        gm_l3 = t23;
    }
    {
        uint64_t t24 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
        void * t25 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)));
        uint64_t t26 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        uint64_t t27 = (uintptr_t)t26;
        uint64_t t28 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        uint64_t r29; bool o30 = __builtin_add_overflow(t28, ((uint64_t)5ULL), &r29);
        if (o30) gm_trap("integer overflow in `+`", gm_srcdo1hnokt0mq8, 491, 19);
        uint64_t t31 = r29;
        uint64_t t32 = (uintptr_t)t31;
        if (t27 > t32 || t32 > (uintptr_t)t24) gm_trap_slice(t27, t32, t24, gm_srcdo1hnokt0mq8, 491, 5);
        gm_b16_8 t33;
        GM_ST(void *, (uint8_t *)&t33, (void *)(((uint8_t *)t25 + t27 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t33 + 8, (uintptr_t)(t32 - t27));
        gm_b16_8 t34;
        GM_ST(void *, (uint8_t *)&t34, (void *)(gm_srd2ukct5o1qh));
        GM_ST(uintptr_t, (uint8_t *)&t34 + 8, (uintptr_t)(5));
        gm_f_copy_from_f7j6p05(t33, t34);
    }
    {
        gm_t4q2b5ibsrdik t35;
        GM_ST(uint8_t, ((uint8_t *)&t35 + 0), ((uint8_t)0));
        GM_ST(uint8_t, ((uint8_t *)&t35 + 1), ((uint8_t)1));
        GM_ST(uint8_t, ((uint8_t *)&t35 + 2), ((uint8_t)0));
        GM_ST(uint8_t, ((uint8_t *)&t35 + 3), ((uint8_t)1));
        GM_ST(uint8_t, ((uint8_t *)&t35 + 4), ((uint8_t)0));
        GM_ST(uint8_t, ((uint8_t *)&t35 + 5), ((uint8_t)1));
        gm_l4 = t35;
    }
    {
        gm_l5 = ((uint32_t)0ULL);
    }
    {
    }
    {
        for (;;) {
            if (!((uint8_t)1)) goto lb36;
            {
            }
            {
                void * t39 = ((uint8_t *)&gm_l7);
                if ((uintptr_t)0 > (uintptr_t)4 || (uintptr_t)4 > (uintptr_t)4) gm_trap_slice((uintptr_t)0, (uintptr_t)4, 4, gm_srcdo1hnokt0mq8, 497, 19);
                gm_b16_8 t40;
                GM_ST(void *, (uint8_t *)&t40, (void *)(((uint8_t *)t39 + (uintptr_t)0 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t40 + 8, (uintptr_t)((uintptr_t)4 - (uintptr_t)0));
                gm_node *pn41 = gm_find(cx, 1087599154);
                if (!pn41) gm_trap("no handler installed for effect `Rand`", gm_srcdo1hnokt0mq8, 497, 9);
                gm_b0_1 t42;
                t42 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, gm_b16_8))pn41->clauses[0])(pn41->env, pn41->hctx, pn41, t40);
                if (gm_unw.kind) {
                    goto gm_ll37;
                }
            }
            {
                {
                    gm_l9 = ((uint64_t)0ULL);
                }
                {
                    gm_l8 = ((uint64_t)4ULL);
                }
                {
                    for (;;) {
                        uint64_t t45 = gm_l9;
                        uint64_t t46 = gm_l8;
                        uint8_t t47 = (t45 < t46);
                        if (!t47) goto lb44;
                        {
                            uint64_t t50 = gm_l9;
                            uint64_t t53 = GM_LD(uint64_t, ((uint8_t *)&t50));
                            gm_l10 = t53;
                            {
                                uint64_t t55 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                                uint64_t r56; bool o57 = __builtin_add_overflow(t55, ((uint64_t)5ULL), &r56);
                                if (o57) gm_trap("integer overflow in `+`", gm_srcdo1hnokt0mq8, 499, 17);
                                uint64_t t58 = r56;
                                uint64_t t59 = gm_l10;
                                uint64_t r60; bool o61 = __builtin_mul_overflow(((uint64_t)2ULL), t59, &r60);
                                if (o61) gm_trap("integer overflow in `*`", gm_srcdo1hnokt0mq8, 499, 32);
                                uint64_t t62 = r60;
                                uint64_t r63; bool o64 = __builtin_add_overflow(t58, t62, &r63);
                                if (o64) gm_trap("integer overflow in `+`", gm_srcdo1hnokt0mq8, 499, 17);
                                uint64_t t65 = r63;
                                uint64_t t66 = (uintptr_t)t65;
                                uint64_t t67 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                                if (t66 >= (uintptr_t)(t67)) gm_trap_index(t66, t67, gm_srcdo1hnokt0mq8, 499, 13);
                                uint8_t *t68 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t66 * 1);
                                uint64_t t69 = gm_l10;
                                uint64_t t70 = (uintptr_t)t69;
                                if (t70 >= (uintptr_t)(4)) gm_trap_index(t70, 4, gm_srcdo1hnokt0mq8, 499, 51);
                                uint8_t t71 = GM_LD(uint8_t, (((uint8_t *)&gm_l7) + t70 * 1));
                                unsigned __int128 n72 = (unsigned __int128)((uint64_t)4ULL);
                                if (n72 >= 8) gm_trap_shift((uint64_t)n72, "u8", gm_srcdo1hnokt0mq8, 499, 51);
                                uint8_t t73 = (uint8_t)(t71 >> (int)n72);
                                uint8_t t74 = gm_f_hex_digit_fdbg8g2(t73);
                                GM_ST(uint8_t, t68, t74);
                            }
                            {
                                uint64_t t75 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                                uint64_t r76; bool o77 = __builtin_add_overflow(t75, ((uint64_t)6ULL), &r76);
                                if (o77) gm_trap("integer overflow in `+`", gm_srcdo1hnokt0mq8, 500, 17);
                                uint64_t t78 = r76;
                                uint64_t t79 = gm_l10;
                                uint64_t r80; bool o81 = __builtin_mul_overflow(((uint64_t)2ULL), t79, &r80);
                                if (o81) gm_trap("integer overflow in `*`", gm_srcdo1hnokt0mq8, 500, 32);
                                uint64_t t82 = r80;
                                uint64_t r83; bool o84 = __builtin_add_overflow(t78, t82, &r83);
                                if (o84) gm_trap("integer overflow in `+`", gm_srcdo1hnokt0mq8, 500, 17);
                                uint64_t t85 = r83;
                                uint64_t t86 = (uintptr_t)t85;
                                uint64_t t87 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                                if (t86 >= (uintptr_t)(t87)) gm_trap_index(t86, t87, gm_srcdo1hnokt0mq8, 500, 13);
                                uint8_t *t88 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t86 * 1);
                                uint64_t t89 = gm_l10;
                                uint64_t t90 = (uintptr_t)t89;
                                if (t90 >= (uintptr_t)(4)) gm_trap_index(t90, 4, gm_srcdo1hnokt0mq8, 500, 51);
                                uint8_t t91 = GM_LD(uint8_t, (((uint8_t *)&gm_l7) + t90 * 1));
                                uint8_t t92 = (uint8_t)(t91 & ((uint8_t)15ULL));
                                uint8_t t93 = gm_f_hex_digit_fdbg8g2(t92);
                                GM_ST(uint8_t, t88, t93);
                            }
                            goto me51;
mn52:;
                            gm_trap("no match arm matched the scrutinee", gm_srcdo1hnokt0mq8, 498, 9);
me51:;
                        }
lc44:;
                        {
                            uint64_t t95 = gm_l9;
                            uint64_t r96; bool o97 = __builtin_add_overflow(t95, ((uint64_t)1ULL), &r96);
                            uint64_t t98 = r96;
                            gm_l9 = t98;
                        }
                    }
lb44:;
                }
            }
            {
                gm_b16_8 t99 = gm_l3;
                gm_b16_8 t100 = GM_LD(gm_b16_8, ((uint8_t *)&t99));
                gm_t4q2b5ibsrdik t101 = gm_l4;
                gm_node *pn102 = gm_find(cx, 2126215853);
                if (!pn102) gm_trap("no handler installed for effect `Fs`", gm_srcdo1hnokt0mq8, 502, 13);
                gm_t62ebbi2i5rkd t103;
                t103 = ((gm_t62ebbi2i5rkd (*)(void *, gm_node *, gm_node *, gm_b16_8, gm_t4q2b5ibsrdik))pn102->clauses[0])(pn102->env, pn102->hctx, pn102, t100, t101);
                if (gm_unw.kind) {
                    goto gm_ll37;
                }
                gm_tm98ap4112hee t104;
                if (!((((uint8_t *)&t103))[0] != 0)) {
                    gm_tm98ap4112hee t105 = GM_LD(gm_tm98ap4112hee, (((uint8_t *)&t103) + 8));
                    t104 = t105;
                } else {
                    gm_toe5fmr3vvf4d t106 = GM_LD(gm_toe5fmr3vvf4d, (((uint8_t *)&t103) + 8));
                    gm_l11 = t106;
                    {
                        uint8_t t109 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l11) + 0) == ((uint8_t)2ULL)));
                        if (!t109) goto ie108;
                        uint32_t t110 = gm_l5;
                        uint8_t t111 = (t110 < ((uint32_t)8ULL));
                        if (!t111) goto ie108;
                        {
                            uint32_t t113 = gm_l5;
                            uint32_t r114; bool o115 = __builtin_add_overflow(t113, ((uint32_t)1ULL), &r114);
                            if (o115) gm_trap("integer overflow in `+`", gm_srcdo1hnokt0mq8, 505, 17);
                            uint32_t t116 = r114;
                            gm_l5 = t116;
                        }
                        {
                            goto lc36;
                        }
                        goto ix108;
ie108:;
ix108:;
                    }
                    gm_toe5fmr3vvf4d t117 = gm_l11;
                    gm_ttln0d0hd70en t118;
                    GM_ST(uint8_t, (uint8_t *)&t118 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t118 + 2, 0);
                    ((uint8_t *)&t118)[0] = 1;
                    GM_ST(gm_toe5fmr3vvf4d, ((uint8_t *)&t118 + 4), t117);
                    uint8_t t119 = ((((uint8_t *)&t118))[0] != 0);
                    F.ret = t118;
                    goto gm_fret;
                }
                gm_l6 = t104;
            }
            {
                goto lb36;
            }
lc36:;
        }
gm_ll37:;
        if (gm_unw.frame == &F.h && gm_unw.label == 0) {
            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb36; }
            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc36; }
        }
        goto gm_lad;
lb36:;
    }
    {
        gm_tm98ap4112hee t120 = gm_l6;
        gm_b16_8 t121 = gm_l1;
        gm_ttln0d0hd70en t122 = gm_f_fill_temp_g12d8pg((void *)NULL, cx, NULL, t120, t121);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b0_1 t123;
        if (!((((uint8_t *)&t122))[0] != 0)) {
            gm_b0_1 t124 = GM_LD(gm_b0_1, (((uint8_t *)&t122) + 4));
            t123 = t124;
        } else {
            gm_toe5fmr3vvf4d t125 = GM_LD(gm_toe5fmr3vvf4d, (((uint8_t *)&t122) + 4));
            gm_l12 = t125;
            {
                gm_tm98ap4112hee t127 = gm_l6;
                gm_node *pn128 = gm_find(cx, 2126215853);
                if (!pn128) gm_trap("no handler installed for effect `Fs`", gm_srcdo1hnokt0mq8, 513, 9);
                gm_b0_1 t129;
                t129 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, gm_tm98ap4112hee))pn128->clauses[1])(pn128->env, pn128->hctx, pn128, t127);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                {
                    gm_b16_8 t131 = gm_l3;
                    gm_b16_8 t132 = GM_LD(gm_b16_8, ((uint8_t *)&t131));
                    gm_node *pn133 = gm_find(cx, 2126215853);
                    if (!pn133) gm_trap("no handler installed for effect `Fs`", gm_srcdo1hnokt0mq8, 514, 13);
                    gm_ttln0d0hd70en t134;
                    t134 = ((gm_ttln0d0hd70en (*)(void *, gm_node *, gm_node *, gm_b16_8))pn133->clauses[6])(pn133->env, pn133->hctx, pn133, t132);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
            }
            gm_toe5fmr3vvf4d t135 = gm_l12;
            gm_ttln0d0hd70en t136;
            GM_ST(uint8_t, (uint8_t *)&t136 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t136 + 2, 0);
            ((uint8_t *)&t136)[0] = 1;
            GM_ST(gm_toe5fmr3vvf4d, ((uint8_t *)&t136 + 4), t135);
            uint8_t t137 = ((((uint8_t *)&t136))[0] != 0);
            F.ret = t136;
            goto gm_fret;
        }
    }
    {
        gm_tm98ap4112hee t138 = gm_l6;
        gm_node *pn139 = gm_find(cx, 2126215853);
        if (!pn139) gm_trap("no handler installed for effect `Fs`", gm_srcdo1hnokt0mq8, 517, 5);
        gm_b0_1 t140;
        t140 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, gm_tm98ap4112hee))pn139->clauses[1])(pn139->env, pn139->hctx, pn139, t138);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_b16_8 t141 = gm_l3;
        gm_b16_8 t142 = GM_LD(gm_b16_8, ((uint8_t *)&t141));
        gm_b16_8 t143 = gm_l0;
        gm_node *pn144 = gm_find(cx, 2126215853);
        if (!pn144) gm_trap("no handler installed for effect `Fs`", gm_srcdo1hnokt0mq8, 518, 5);
        gm_ttln0d0hd70en t145;
        t145 = ((gm_ttln0d0hd70en (*)(void *, gm_node *, gm_node *, gm_b16_8, gm_b16_8))pn144->clauses[7])(pn144->env, pn144->hctx, pn144, t142, t143);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b0_1 t146;
        if (!((((uint8_t *)&t145))[0] != 0)) {
            gm_b0_1 t147 = GM_LD(gm_b0_1, (((uint8_t *)&t145) + 4));
            t146 = t147;
        } else {
            gm_toe5fmr3vvf4d t148 = GM_LD(gm_toe5fmr3vvf4d, (((uint8_t *)&t145) + 4));
            gm_l13 = t148;
            {
                {
                    gm_b16_8 t151 = gm_l3;
                    gm_b16_8 t152 = GM_LD(gm_b16_8, ((uint8_t *)&t151));
                    gm_node *pn153 = gm_find(cx, 2126215853);
                    if (!pn153) gm_trap("no handler installed for effect `Fs`", gm_srcdo1hnokt0mq8, 519, 13);
                    gm_ttln0d0hd70en t154;
                    t154 = ((gm_ttln0d0hd70en (*)(void *, gm_node *, gm_node *, gm_b16_8))pn153->clauses[6])(pn153->env, pn153->hctx, pn153, t152);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
            }
            gm_toe5fmr3vvf4d t155 = gm_l13;
            gm_ttln0d0hd70en t156;
            GM_ST(uint8_t, (uint8_t *)&t156 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t156 + 2, 0);
            ((uint8_t *)&t156)[0] = 1;
            GM_ST(gm_toe5fmr3vvf4d, ((uint8_t *)&t156 + 4), t155);
            uint8_t t157 = ((((uint8_t *)&t156))[0] != 0);
            F.ret = t156;
            goto gm_fret;
        }
    }
    {
        gm_b0_1 t158;
        gm_ttln0d0hd70en t159;
        GM_ST(uint8_t, (uint8_t *)&t159 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t159 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t159 + 4, 0);
        GM_ST(uint32_t, (uint8_t *)&t159 + 8, 0);
        ((uint8_t *)&t159)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t159 + 4), t158);
        uint8_t t160 = ((((uint8_t *)&t159))[0] != 0);
        F.ret = t159;
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
