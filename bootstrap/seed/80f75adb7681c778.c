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
typedef struct gm_cs6mnftt383dnt { void * f0; void * f1; } gm_cs6mnftt383dnt;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint32_t f0; uint8_t f1; uint8_t pe[3]; } gm_t9805qtonbsag;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { void * f0; void * f1; void * f2; uint8_t f3; uint8_t pe[7]; } gm_tao63pk3r18v5;
typedef struct { uint64_t a[3]; } gm_tc0nbpdhofi37;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint8_t f0; uint8_t p1[7]; void * f1; } gm_tgtub0v94d5u5;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { void * f0; void * f1; uint8_t f2; uint8_t pe[7]; } gm_tlcl4u558l6p5;
_Static_assert(sizeof(gm_cs6mnftt383dnt) == 16 && _Alignof(gm_cs6mnftt383dnt) == 8 && offsetof(gm_cs6mnftt383dnt, f0) == 0 && offsetof(gm_cs6mnftt383dnt, f1) == 8, "layout of `Allocator`");
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_t08olq97rscn3;
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t f2; uint8_t f3; uint8_t pe[5]; } gm_t0to1u196c9pc;
typedef struct { uint32_t f0; uint32_t f1; gm_b16_8 f2; } gm_t2s5j3n0guq0t;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { gm_b16_8 f0; uint32_t f1; uint32_t f2; } gm_t5hdnnftkup6m;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_t79rbdv4rafe5;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; void * f1; } gm_t8ghgl2jj0qha;
_Static_assert(sizeof(gm_t9805qtonbsag) == 8 && _Alignof(gm_t9805qtonbsag) == 4 && offsetof(gm_t9805qtonbsag, f0) == 0 && offsetof(gm_t9805qtonbsag, f1) == 4, "layout of `?u32`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
_Static_assert(sizeof(gm_tao63pk3r18v5) == 32 && _Alignof(gm_tao63pk3r18v5) == 8 && offsetof(gm_tao63pk3r18v5, f0) == 0 && offsetof(gm_tao63pk3r18v5, f1) == 8 && offsetof(gm_tao63pk3r18v5, f2) == 16 && offsetof(gm_tao63pk3r18v5, f3) == 24, "layout of `SliceExpr`");
typedef struct { gm_b16_8 f0; uint32_t f1; uint32_t f2; gm_b16_8 f3; } gm_tb40qn7hk0tl9;
typedef struct { uint64_t f0; uint64_t f1; gm_b16_8 f2; } gm_tbccvikg6b96e;
_Static_assert(sizeof(gm_tc0nbpdhofi37) == 24 && _Alignof(gm_tc0nbpdhofi37) == 8 && offsetof(gm_tc0nbpdhofi37, a) == 0, "layout of `[3]usize`");
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; } gm_tfeab279c16rr;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
_Static_assert(sizeof(gm_tgtub0v94d5u5) == 16 && _Alignof(gm_tgtub0v94d5u5) == 8 && offsetof(gm_tgtub0v94d5u5, f0) == 0 && offsetof(gm_tgtub0v94d5u5, f1) == 8, "layout of `(is_const: bool, inner: ^Expr)`");
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { gm_b16_8 f0; gm_t9qkh0rsooc8d f1; uint8_t pe[7]; } gm_tibupt4dmcqai;
typedef struct { gm_t3mf58gv9kf3n f0; void * f1; uint64_t f2; uint64_t f3; uint64_t f4; } gm_tjt4ka19str13;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; uint8_t f2; uint8_t pe[7]; } gm_tk9n3vlila9qg;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_tlcl4u558l6p5) == 24 && _Alignof(gm_tlcl4u558l6p5) == 8 && offsetof(gm_tlcl4u558l6p5, f0) == 0 && offsetof(gm_tlcl4u558l6p5, f1) == 8 && offsetof(gm_tlcl4u558l6p5, f2) == 16, "layout of `(lo: ^Expr, hi: ^Expr, inclusive: bool)`");
typedef struct { void * f0; gm_b16_8 f1; uint8_t f2; uint8_t pe[7]; } gm_tlljg61a2ctgq;
typedef struct { gm_tl874t2atnlh3 f0; uint8_t f1; uint8_t pe[3]; } gm_toceffb697p0u;
typedef struct { gm_b16_8 f0; void * f1; } gm_tpe1hv3fekn05;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_t9qkh0rsooc8d v3; } f1; } gm_ts6oqppjh0ba3;
typedef struct { void * f0; uint32_t f1; gm_tl874t2atnlh3 f2; } gm_tupk62cr6lbdu;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; void * f2; gm_t9qkh0rsooc8d f3; uint8_t p4[7]; void * f4; gm_tl874t2atnlh3 f5; uint8_t pe[4]; } gm_tv2uf4leil5hq;
_Static_assert(sizeof(gm_t08olq97rscn3) == 32 && _Alignof(gm_t08olq97rscn3) == 8 && offsetof(gm_t08olq97rscn3, f0) == 0 && offsetof(gm_t08olq97rscn3, f1) == 16, "layout of `File`");
_Static_assert(sizeof(gm_t0to1u196c9pc) == 24 && _Alignof(gm_t0to1u196c9pc) == 8 && offsetof(gm_t0to1u196c9pc, f0) == 0 && offsetof(gm_t0to1u196c9pc, f1) == 16 && offsetof(gm_t0to1u196c9pc, f2) == 17 && offsetof(gm_t0to1u196c9pc, f3) == 18, "layout of `Scan`");
typedef struct { gm_t08olq97rscn3 f0; uint8_t f1; uint8_t pe[7]; } gm_t1s75i08qtptd;
typedef struct { gm_trqfvmjtt4sij f0; uint64_t f1; uint64_t f2; uint32_t f3; uint8_t pe[4]; } gm_t2792tsg5eg72;
_Static_assert(sizeof(gm_t2s5j3n0guq0t) == 24 && _Alignof(gm_t2s5j3n0guq0t) == 8 && offsetof(gm_t2s5j3n0guq0t, f0) == 0 && offsetof(gm_t2s5j3n0guq0t, f1) == 4 && offsetof(gm_t2s5j3n0guq0t, f2) == 8, "layout of `Def`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; gm_t9qkh0rsooc8d v1; void * v2; void * v3; gm_tibupt4dmcqai v4; gm_b16_8 v5; gm_b16_8 v6; gm_t9qkh0rsooc8d v7; } f1; } gm_t3fcbuehh3bs2;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t p3[4]; gm_trqfvmjtt4sij f3; gm_trqfvmjtt4sij f4; gm_trqfvmjtt4sij f5; gm_th2lqbo77pvsc f6; } gm_t4gut2c9fj1kt;
_Static_assert(sizeof(gm_t5hdnnftkup6m) == 24 && _Alignof(gm_t5hdnnftkup6m) == 8 && offsetof(gm_t5hdnnftkup6m, f0) == 0 && offsetof(gm_t5hdnnftkup6m, f1) == 16 && offsetof(gm_t5hdnnftkup6m, f2) == 20, "layout of `(string, u32, u32)`");
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_t79rbdv4rafe5) == 40 && _Alignof(gm_t79rbdv4rafe5) == 8 && offsetof(gm_t79rbdv4rafe5, f0) == 0 && offsetof(gm_t79rbdv4rafe5, f1) == 8 && offsetof(gm_t79rbdv4rafe5, f2) == 24, "layout of `Block`");
typedef struct { void * f0; gm_b16_8 f1; gm_th2lqbo77pvsc f2; gm_tl874t2atnlh3 f3; uint8_t pe[4]; } gm_t7bjbmukv26e7;
_Static_assert(sizeof(gm_t8ghgl2jj0qha) == 16 && _Alignof(gm_t8ghgl2jj0qha) == 8 && offsetof(gm_t8ghgl2jj0qha, f0) == 0 && offsetof(gm_t8ghgl2jj0qha, f1) == 8, "layout of `(op: UnOp, operand: ^Expr)`");
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
typedef struct { void * f0; gm_th2lqbo77pvsc f1; } gm_tagc77imt2chm;
typedef struct { gm_b16_8 f0; gm_tv2uf4leil5hq f1; } gm_taiafe1jasg4m;
_Static_assert(sizeof(gm_tb40qn7hk0tl9) == 40 && _Alignof(gm_tb40qn7hk0tl9) == 8 && offsetof(gm_tb40qn7hk0tl9, f0) == 0 && offsetof(gm_tb40qn7hk0tl9, f1) == 16 && offsetof(gm_tb40qn7hk0tl9, f2) == 20 && offsetof(gm_tb40qn7hk0tl9, f3) == 24, "layout of `SourceLocation`");
_Static_assert(sizeof(gm_tbccvikg6b96e) == 32 && _Alignof(gm_tbccvikg6b96e) == 8 && offsetof(gm_tbccvikg6b96e, f0) == 0 && offsetof(gm_tbccvikg6b96e, f1) == 8 && offsetof(gm_tbccvikg6b96e, f2) == 16, "layout of `Layout`");
typedef struct { gm_tibupt4dmcqai f0; gm_tl874t2atnlh3 f1; uint8_t p2[4]; gm_t79rbdv4rafe5 f2; uint8_t f3; uint8_t pe[7]; } gm_tcfiotghd1s5k;
typedef struct { gm_t08olq97rscn3 f0; gm_th2lqbo77pvsc f1; } gm_tet9guhev5r4u;
_Static_assert(sizeof(gm_tfeab279c16rr) == 48 && _Alignof(gm_tfeab279c16rr) == 8 && offsetof(gm_tfeab279c16rr, f0) == 0 && offsetof(gm_tfeab279c16rr, f1) == 16 && offsetof(gm_tfeab279c16rr, f2) == 32, "layout of `Toolchain`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
typedef struct { gm_th2lqbo77pvsc f0; uint8_t f1; uint8_t pe[7]; } gm_ti5ramhglnvdt;
_Static_assert(sizeof(gm_tibupt4dmcqai) == 24 && _Alignof(gm_tibupt4dmcqai) == 8 && offsetof(gm_tibupt4dmcqai, f0) == 0 && offsetof(gm_tibupt4dmcqai, f1) == 16, "layout of `StrLit`");
typedef struct { gm_trqfvmjtt4sij f0; } gm_tin97tt4ud0s1;
typedef struct { void * f0; gm_t08olq97rscn3 f1; } gm_tioie2ju3q4ul;
typedef struct { uint8_t f0; union { gm_ts6oqppjh0ba3 v0; } f1; } gm_tjsp7bhdn2e8j;
_Static_assert(sizeof(gm_tjt4ka19str13) == 48 && _Alignof(gm_tjt4ka19str13) == 8 && offsetof(gm_tjt4ka19str13, f0) == 0 && offsetof(gm_tjt4ka19str13, f1) == 16 && offsetof(gm_tjt4ka19str13, f2) == 24 && offsetof(gm_tjt4ka19str13, f3) == 32 && offsetof(gm_tjt4ka19str13, f4) == 40, "layout of `Arena`");
_Static_assert(sizeof(gm_tk9n3vlila9qg) == 40 && _Alignof(gm_tk9n3vlila9qg) == 8 && offsetof(gm_tk9n3vlila9qg, f0) == 0 && offsetof(gm_tk9n3vlila9qg, f1) == 16 && offsetof(gm_tk9n3vlila9qg, f2) == 32, "layout of `(lib: string, link_name: string, variadic: bool)`");
typedef struct { gm_t08olq97rscn3 f0; gm_th2lqbo77pvsc f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_tkacitepg5fk0;
_Static_assert(sizeof(gm_tlljg61a2ctgq) == 32 && _Alignof(gm_tlljg61a2ctgq) == 8 && offsetof(gm_tlljg61a2ctgq, f0) == 0 && offsetof(gm_tlljg61a2ctgq, f1) == 8 && offsetof(gm_tlljg61a2ctgq, f2) == 24, "layout of `(callee: ^Expr, args: []Arg, trailing_comma: bool)`");
_Static_assert(sizeof(gm_toceffb697p0u) == 16 && _Alignof(gm_toceffb697p0u) == 4 && offsetof(gm_toceffb697p0u, f0) == 0 && offsetof(gm_toceffb697p0u, f1) == 12, "layout of `?Span`");
_Static_assert(sizeof(gm_tpe1hv3fekn05) == 24 && _Alignof(gm_tpe1hv3fekn05) == 8 && offsetof(gm_tpe1hv3fekn05, f0) == 0 && offsetof(gm_tpe1hv3fekn05, f1) == 16, "layout of `(attrs: []Attribute, inner: ^Expr)`");
typedef struct { gm_trqfvmjtt4sij f0; gm_trqfvmjtt4sij f1; gm_trqfvmjtt4sij f2; } gm_tr8dvlu9ik4gj;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; uint64_t f2; void * f3; gm_trqfvmjtt4sij f4; gm_trqfvmjtt4sij f5; gm_trqfvmjtt4sij f6; gm_tc0nbpdhofi37 f7; gm_trqfvmjtt4sij f8; gm_toceffb697p0u f9; gm_trqfvmjtt4sij f10; uint8_t f11; uint8_t pe[7]; } gm_trd6bg8is1v4n;
typedef struct { gm_ts6oqppjh0ba3 f0; uint8_t p1[6]; void * f1; void * f2; } gm_trl9d7m1k5lbd;
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_ts6oqppjh0ba3) == 2 && _Alignof(gm_ts6oqppjh0ba3) == 1 && offsetof(gm_ts6oqppjh0ba3, f0) == 0 && offsetof(gm_ts6oqppjh0ba3, f1) == 1, "layout of `BinOp`");
_Static_assert(sizeof(gm_tupk62cr6lbdu) == 24 && _Alignof(gm_tupk62cr6lbdu) == 8 && offsetof(gm_tupk62cr6lbdu, f0) == 0 && offsetof(gm_tupk62cr6lbdu, f1) == 8 && offsetof(gm_tupk62cr6lbdu, f2) == 12, "layout of `(base: ^Expr, index: u32, index_span: Span)`");
_Static_assert(sizeof(gm_tv2uf4leil5hq) == 64 && _Alignof(gm_tv2uf4leil5hq) == 8 && offsetof(gm_tv2uf4leil5hq, f0) == 0 && offsetof(gm_tv2uf4leil5hq, f1) == 8 && offsetof(gm_tv2uf4leil5hq, f2) == 24 && offsetof(gm_tv2uf4leil5hq, f3) == 32 && offsetof(gm_tv2uf4leil5hq, f4) == 40 && offsetof(gm_tv2uf4leil5hq, f5) == 48, "layout of `Binding`");
_Static_assert(sizeof(gm_t1s75i08qtptd) == 40 && _Alignof(gm_t1s75i08qtptd) == 8 && offsetof(gm_t1s75i08qtptd, f0) == 0 && offsetof(gm_t1s75i08qtptd, f1) == 32, "layout of `?Ident`");
_Static_assert(sizeof(gm_t2792tsg5eg72) == 64 && _Alignof(gm_t2792tsg5eg72) == 8 && offsetof(gm_t2792tsg5eg72, f0) == 0 && offsetof(gm_t2792tsg5eg72, f1) == 40 && offsetof(gm_t2792tsg5eg72, f2) == 48 && offsetof(gm_t2792tsg5eg72, f3) == 56, "layout of `Diagnostics`");
_Static_assert(sizeof(gm_t3fcbuehh3bs2) == 32 && _Alignof(gm_t3fcbuehh3bs2) == 8 && offsetof(gm_t3fcbuehh3bs2, f0) == 0 && offsetof(gm_t3fcbuehh3bs2, f1) == 8, "layout of `TokenKind`");
typedef struct { gm_t1s75i08qtptd f0; gm_t08olq97rscn3 f1; gm_t79rbdv4rafe5 f2; } gm_t43d6nbdfap6p;
typedef struct { gm_b16_8 f0; gm_tjsp7bhdn2e8j f1; uint8_t p2[5]; void * f2; } gm_t4d1qrgh0vnq1;
_Static_assert(sizeof(gm_t4gut2c9fj1kt) == 184 && _Alignof(gm_t4gut2c9fj1kt) == 8 && offsetof(gm_t4gut2c9fj1kt, f0) == 0 && offsetof(gm_t4gut2c9fj1kt, f1) == 8 && offsetof(gm_t4gut2c9fj1kt, f2) == 24 && offsetof(gm_t4gut2c9fj1kt, f3) == 40 && offsetof(gm_t4gut2c9fj1kt, f4) == 80 && offsetof(gm_t4gut2c9fj1kt, f5) == 120 && offsetof(gm_t4gut2c9fj1kt, f6) == 160, "layout of `Diagnostic`");
_Static_assert(sizeof(gm_t7bjbmukv26e7) == 64 && _Alignof(gm_t7bjbmukv26e7) == 8 && offsetof(gm_t7bjbmukv26e7, f0) == 0 && offsetof(gm_t7bjbmukv26e7, f1) == 8 && offsetof(gm_t7bjbmukv26e7, f2) == 24 && offsetof(gm_t7bjbmukv26e7, f3) == 48, "layout of `WhenItems`");
typedef struct { gm_th2lqbo77pvsc f0; gm_ti5ramhglnvdt f1; uint8_t f2; uint8_t pe[7]; } gm_t9feugf5hcdtg;
_Static_assert(sizeof(gm_tagc77imt2chm) == 32 && _Alignof(gm_tagc77imt2chm) == 8 && offsetof(gm_tagc77imt2chm, f0) == 0 && offsetof(gm_tagc77imt2chm, f1) == 8, "layout of `PathPattern`");
_Static_assert(sizeof(gm_taiafe1jasg4m) == 80 && _Alignof(gm_taiafe1jasg4m) == 8 && offsetof(gm_taiafe1jasg4m, f0) == 0 && offsetof(gm_taiafe1jasg4m, f1) == 16, "layout of `(attrs: []Attribute, binding: Binding)`");
_Static_assert(sizeof(gm_tcfiotghd1s5k) == 88 && _Alignof(gm_tcfiotghd1s5k) == 8 && offsetof(gm_tcfiotghd1s5k, f0) == 0 && offsetof(gm_tcfiotghd1s5k, f1) == 24 && offsetof(gm_tcfiotghd1s5k, f2) == 40 && offsetof(gm_tcfiotghd1s5k, f3) == 80, "layout of `TestDecl`");
typedef struct { gm_t1s75i08qtptd f0; void * f1; } gm_tchr52g0ema53;
_Static_assert(sizeof(gm_tet9guhev5r4u) == 56 && _Alignof(gm_tet9guhev5r4u) == 8 && offsetof(gm_tet9guhev5r4u, f0) == 0 && offsetof(gm_tet9guhev5r4u, f1) == 32, "layout of `IntrinsicExpr`");
_Static_assert(sizeof(gm_ti5ramhglnvdt) == 32 && _Alignof(gm_ti5ramhglnvdt) == 8 && offsetof(gm_ti5ramhglnvdt, f0) == 0 && offsetof(gm_ti5ramhglnvdt, f1) == 24, "layout of `??string`");
_Static_assert(sizeof(gm_tin97tt4ud0s1) == 40 && _Alignof(gm_tin97tt4ud0s1) == 8 && offsetof(gm_tin97tt4ud0s1, f0) == 0, "layout of `SourceMap`");
typedef struct { void * f0; gm_t1s75i08qtptd f1; void * f2; } gm_tioi3jh9v22g5;
_Static_assert(sizeof(gm_tioie2ju3q4ul) == 40 && _Alignof(gm_tioie2ju3q4ul) == 8 && offsetof(gm_tioie2ju3q4ul, f0) == 0 && offsetof(gm_tioie2ju3q4ul, f1) == 8, "layout of `(base: ^Expr, name: Ident)`");
_Static_assert(sizeof(gm_tjsp7bhdn2e8j) == 3 && _Alignof(gm_tjsp7bhdn2e8j) == 1 && offsetof(gm_tjsp7bhdn2e8j, f0) == 0 && offsetof(gm_tjsp7bhdn2e8j, f1) == 1, "layout of `AssignOp`");
_Static_assert(sizeof(gm_tkacitepg5fk0) == 72 && _Alignof(gm_tkacitepg5fk0) == 8 && offsetof(gm_tkacitepg5fk0, f0) == 0 && offsetof(gm_tkacitepg5fk0, f1) == 32 && offsetof(gm_tkacitepg5fk0, f2) == 56, "layout of `Attribute`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tv2uf4leil5hq v0; gm_t7bjbmukv26e7 v1; void * v2; gm_tcfiotghd1s5k v3; void * v4; } f1; } gm_tkoope2qgegd9;
typedef struct { gm_t1s75i08qtptd f0; gm_t9805qtonbsag f1; } gm_tm3mvv7hooqkm;
typedef struct { gm_t3fcbuehh3bs2 f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_tm4pc0emhrv1g;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t08olq97rscn3 v0; gm_t08olq97rscn3 v1; void * v2; gm_tlcl4u558l6p5 v3; void * v4; gm_tagc77imt2chm v5; gm_b16_8 v6; } f1; } gm_to361lgknsu69;
_Static_assert(sizeof(gm_tr8dvlu9ik4gj) == 120 && _Alignof(gm_tr8dvlu9ik4gj) == 8 && offsetof(gm_tr8dvlu9ik4gj, f0) == 0 && offsetof(gm_tr8dvlu9ik4gj, f1) == 40 && offsetof(gm_tr8dvlu9ik4gj, f2) == 80, "layout of `Extras`");
_Static_assert(sizeof(gm_trd6bg8is1v4n) == 288 && _Alignof(gm_trd6bg8is1v4n) == 8 && offsetof(gm_trd6bg8is1v4n, f0) == 0 && offsetof(gm_trd6bg8is1v4n, f1) == 8 && offsetof(gm_trd6bg8is1v4n, f2) == 24 && offsetof(gm_trd6bg8is1v4n, f3) == 32 && offsetof(gm_trd6bg8is1v4n, f4) == 40 && offsetof(gm_trd6bg8is1v4n, f5) == 80 && offsetof(gm_trd6bg8is1v4n, f6) == 120 && offsetof(gm_trd6bg8is1v4n, f7) == 160 && offsetof(gm_trd6bg8is1v4n, f8) == 184 && offsetof(gm_trd6bg8is1v4n, f9) == 224 && offsetof(gm_trd6bg8is1v4n, f10) == 240 && offsetof(gm_trd6bg8is1v4n, f11) == 280, "layout of `Lexer`");
_Static_assert(sizeof(gm_trl9d7m1k5lbd) == 24 && _Alignof(gm_trl9d7m1k5lbd) == 8 && offsetof(gm_trl9d7m1k5lbd, f0) == 0 && offsetof(gm_trl9d7m1k5lbd, f1) == 8 && offsetof(gm_trl9d7m1k5lbd, f2) == 16, "layout of `(op: BinOp, lhs: ^Expr, rhs: ^Expr)`");
typedef struct { uint32_t f0; uint8_t p1[4]; gm_to361lgknsu69 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_t0d02142k4tu8;
typedef struct { uint8_t f0; uint8_t p1[7]; union { void * v0; void * v1; gm_tibupt4dmcqai v2; uint8_t v3; gm_t08olq97rscn3 v4; gm_t08olq97rscn3 v5; void * v6; gm_b16_8 v7; void * v8; gm_t8ghgl2jj0qha v9; gm_trl9d7m1k5lbd v10; gm_tioi3jh9v22g5 v11; void * v12; void * v13; gm_t3mf58gv9kf3n v14; void * v15; gm_tlljg61a2ctgq v16; gm_t3mf58gv9kf3n v17; gm_tao63pk3r18v5 v18; gm_tioie2ju3q4ul v19; gm_tupk62cr6lbdu v20; void * v21; gm_tgtub0v94d5u5 v22; gm_tgtub0v94d5u5 v23; gm_tgtub0v94d5u5 v24; gm_t3mf58gv9kf3n v25; gm_t3mf58gv9kf3n v26; void * v27; void * v28; void * v29; void * v30; void * v31; gm_tpe1hv3fekn05 v32; void * v33; void * v34; gm_tibupt4dmcqai v35; void * v36; void * v37; void * v38; void * v39; void * v40; gm_t79rbdv4rafe5 v41; void * v42; gm_tchr52g0ema53 v43; gm_t1s75i08qtptd v44; gm_t3mf58gv9kf3n v45; gm_tet9guhev5r4u v46; } f1; } gm_t2gtbh39ptekd;
_Static_assert(sizeof(gm_t43d6nbdfap6p) == 112 && _Alignof(gm_t43d6nbdfap6p) == 8 && offsetof(gm_t43d6nbdfap6p, f0) == 0 && offsetof(gm_t43d6nbdfap6p, f1) == 40 && offsetof(gm_t43d6nbdfap6p, f2) == 72, "layout of `WhileStmt`");
_Static_assert(sizeof(gm_t4d1qrgh0vnq1) == 32 && _Alignof(gm_t4d1qrgh0vnq1) == 8 && offsetof(gm_t4d1qrgh0vnq1, f0) == 0 && offsetof(gm_t4d1qrgh0vnq1, f1) == 16 && offsetof(gm_t4d1qrgh0vnq1, f2) == 24, "layout of `(places: []Expr, op: AssignOp, value: ^Expr)`");
_Static_assert(sizeof(gm_t9feugf5hcdtg) == 64 && _Alignof(gm_t9feugf5hcdtg) == 8 && offsetof(gm_t9feugf5hcdtg, f0) == 0 && offsetof(gm_t9feugf5hcdtg, f1) == 24 && offsetof(gm_t9feugf5hcdtg, f2) == 56, "layout of `Classified`");
_Static_assert(sizeof(gm_tchr52g0ema53) == 48 && _Alignof(gm_tchr52g0ema53) == 8 && offsetof(gm_tchr52g0ema53, f0) == 0 && offsetof(gm_tchr52g0ema53, f1) == 40, "layout of `BreakExpr`");
typedef struct { uint64_t f0; gm_tm4pc0emhrv1g f1; } gm_tfhcigdju4v01;
_Static_assert(sizeof(gm_tioi3jh9v22g5) == 56 && _Alignof(gm_tioi3jh9v22g5) == 8 && offsetof(gm_tioi3jh9v22g5, f0) == 0 && offsetof(gm_tioi3jh9v22g5, f1) == 8 && offsetof(gm_tioi3jh9v22g5, f2) == 48, "layout of `ElseExpr`");
_Static_assert(sizeof(gm_tkoope2qgegd9) == 96 && _Alignof(gm_tkoope2qgegd9) == 8 && offsetof(gm_tkoope2qgegd9, f0) == 0 && offsetof(gm_tkoope2qgegd9, f1) == 8, "layout of `ItemKind`");
_Static_assert(sizeof(gm_tm3mvv7hooqkm) == 48 && _Alignof(gm_tm3mvv7hooqkm) == 8 && offsetof(gm_tm3mvv7hooqkm, f0) == 0 && offsetof(gm_tm3mvv7hooqkm, f1) == 40, "layout of `Parsed`");
_Static_assert(sizeof(gm_tm4pc0emhrv1g) == 48 && _Alignof(gm_tm4pc0emhrv1g) == 8 && offsetof(gm_tm4pc0emhrv1g, f0) == 0 && offsetof(gm_tm4pc0emhrv1g, f1) == 32, "layout of `Token`");
_Static_assert(sizeof(gm_to361lgknsu69) == 40 && _Alignof(gm_to361lgknsu69) == 8 && offsetof(gm_to361lgknsu69, f0) == 0 && offsetof(gm_to361lgknsu69, f1) == 8, "layout of `PatternKind`");
typedef struct { uint32_t f0; gm_tl874t2atnlh3 f1; gm_th2lqbo77pvsc f2; gm_b16_8 f3; gm_t9qkh0rsooc8d f4; uint8_t p5[7]; gm_tkoope2qgegd9 f5; } gm_tvpnktv5fo8nl;
_Static_assert(sizeof(gm_t0d02142k4tu8) == 64 && _Alignof(gm_t0d02142k4tu8) == 8 && offsetof(gm_t0d02142k4tu8, f0) == 0 && offsetof(gm_t0d02142k4tu8, f1) == 8 && offsetof(gm_t0d02142k4tu8, f2) == 48, "layout of `Pattern`");
typedef struct { gm_t0d02142k4tu8 f0; void * f1; void * f2; } gm_t15nkjiutlvf3;
_Static_assert(sizeof(gm_t2gtbh39ptekd) == 64 && _Alignof(gm_t2gtbh39ptekd) == 8 && offsetof(gm_t2gtbh39ptekd, f0) == 0 && offsetof(gm_t2gtbh39ptekd, f1) == 8, "layout of `ExprKind`");
_Static_assert(sizeof(gm_tfhcigdju4v01) == 56 && _Alignof(gm_tfhcigdju4v01) == 8 && offsetof(gm_tfhcigdju4v01, f0) == 0 && offsetof(gm_tfhcigdju4v01, f1) == 8, "layout of `(i: usize, x: Token)`");
_Static_assert(sizeof(gm_tvpnktv5fo8nl) == 160 && _Alignof(gm_tvpnktv5fo8nl) == 8 && offsetof(gm_tvpnktv5fo8nl, f0) == 0 && offsetof(gm_tvpnktv5fo8nl, f1) == 4 && offsetof(gm_tvpnktv5fo8nl, f2) == 16 && offsetof(gm_tvpnktv5fo8nl, f3) == 40 && offsetof(gm_tvpnktv5fo8nl, f4) == 56 && offsetof(gm_tvpnktv5fo8nl, f5) == 64, "layout of `Item`");
_Static_assert(sizeof(gm_t15nkjiutlvf3) == 80 && _Alignof(gm_t15nkjiutlvf3) == 8 && offsetof(gm_t15nkjiutlvf3, f0) == 0 && offsetof(gm_t15nkjiutlvf3, f1) == 64 && offsetof(gm_t15nkjiutlvf3, f2) == 72, "layout of `(pat: Pattern, value: ^Expr, fallback: ^Expr)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_taiafe1jasg4m v0; gm_t4d1qrgh0vnq1 v1; void * v2; gm_tgtub0v94d5u5 v3; void * v4; gm_t43d6nbdfap6p v5; void * v6; gm_t15nkjiutlvf3 v7; gm_t79rbdv4rafe5 v8; } f1; } gm_tem8p0t66u1ce;
_Static_assert(sizeof(gm_tem8p0t66u1ce) == 120 && _Alignof(gm_tem8p0t66u1ce) == 8 && offsetof(gm_tem8p0t66u1ce, f0) == 0 && offsetof(gm_tem8p0t66u1ce, f1) == 8, "layout of `StmtKind`");
typedef struct { uint32_t f0; uint8_t p1[4]; gm_tem8p0t66u1ce f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_to88ae1b67aev;
_Static_assert(sizeof(gm_to88ae1b67aev) == 144 && _Alignof(gm_to88ae1b67aev) == 8 && offsetof(gm_to88ae1b67aev, f0) == 0 && offsetof(gm_to88ae1b67aev, f1) == 8 && offsetof(gm_to88ae1b67aev, f2) == 128, "layout of `Stmt`");
struct gm_fr_allocator_5m783ja {
    gm_frame h;
    gm_t3mf58gv9kf3n ret;
};
struct gm_fr_classify_in_4kp1t31 {
    gm_frame h;
    gm_t9feugf5hcdtg l2; /* none */
    gm_t9feugf5hcdtg l3; /* c */
    gm_tm3mvv7hooqkm l4; /* items_parse */
    gm_trqfvmjtt4sij l6; /* items */
    gm_tvpnktv5fo8nl l10; /* it */
    gm_trqfvmjtt4sij l11; /* names */
    gm_tv2uf4leil5hq l13; /* b */
    gm_tm3mvv7hooqkm l17; /* stmts_parse */
    gm_tv2uf4leil5hq l19; /* b */
    gm_t79rbdv4rafe5 l21; /* block */
    gm_trqfvmjtt4sij l23; /* bindings */
    gm_to88ae1b67aev l26; /* st */
    gm_taiafe1jasg4m l27; /* bd */
    gm_t9feugf5hcdtg ret;
};
struct gm_fr_classify_rm38cme {
    gm_frame h;
    gm_tjt4ka19str13 l1; /* a */
    gm_t9feugf5hcdtg l2; /* c */
    gm_t9feugf5hcdtg ret;
    gm_t9feugf5hcdtg w0;
};
struct gm_fr_contains_gkki4kq {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_depth_js5aj0d {
    gm_frame h;
    gm_tm4pc0emhrv1g l4; /* t */
    int64_t ret;
};
struct gm_fr_diagnostics_n3kkhf4 {
    gm_frame h;
    gm_t2792tsg5eg72 ret;
};
struct gm_fr_empty_slice_1th396a {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_5nn7ph0 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_8jt91p4 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_fcinhvp {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ijtp9lo {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_lpi80k4_2 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_s801pu6_2 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_fn_8no2c2v {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_has_attr_7fhere0 {
    gm_frame h;
    gm_tkacitepg5fk0 l4; /* a */
    uint8_t ret;
};
struct gm_fr_has_errors_if7ai0m {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_blank_asoolmf {
    gm_frame h;
    gm_tjt4ka19str13 l1; /* a */
    gm_tm4pc0emhrv1g l6; /* t */
    uint8_t ret;
    uint8_t w0;
};
struct gm_fr_is_definition_3en3eog {
    gm_frame h;
    gm_t2gtbh39ptekd l0; /* kind */
    uint8_t ret;
};
struct gm_fr_lex_dc8mbmf {
    gm_frame h;
    gm_trd6bg8is1v4n l3; /* lx */
    gm_tr8dvlu9ik4gj ret;
};
struct gm_fr_list_293fcpb {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_bvkp9ir {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_epvducg {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_f0gjjbh_2 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_jekodor_2 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_prbqo9j {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_uvfu1k1 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_lookup_var_3vpm3u6 {
    gm_frame h;
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_may_mutate_uj4e7jp {
    gm_frame h;
    gm_tjt4ka19str13 l1; /* a */
    gm_tm4pc0emhrv1g l7; /* t */
    uint8_t ret;
    uint8_t w0;
};
struct gm_fr_new_jr5ejs0 {
    gm_frame h;
    gm_tjt4ka19str13 ret;
};
struct gm_fr_open_brackets_hicehud {
    gm_frame h;
    gm_tjt4ka19str13 l1; /* a */
    uint8_t ret;
    uint8_t w0;
};
struct gm_fr_own_46jsjq2 {
    gm_frame h;
    gm_t9feugf5hcdtg l0; /* c */
    gm_t9feugf5hcdtg l1; /* out */
    gm_trqfvmjtt4sij l3; /* copy */
    gm_tk9n3vlila9qg l6; /* it */
    gm_trqfvmjtt4sij l7; /* names */
    gm_trqfvmjtt4sij l12; /* copy */
    gm_t9feugf5hcdtg ret;
};
struct gm_fr_parse_items_g2pt6ce {
    gm_frame h;
    gm_tin97tt4ud0s1 l1; /* sm */
    gm_t2792tsg5eg72 l2; /* diags */
    gm_tm3mvv7hooqkm ret;
};
struct gm_fr_parse_source_i1af11u {
    gm_frame h;
    gm_tr8dvlu9ik4gj l4; /* lexed */
    gm_t4gut2c9fj1kt l10; /* d */
    gm_tm3mvv7hooqkm ret;
};
struct gm_fr_parse_stmts_mefdnbk {
    gm_frame h;
    gm_tin97tt4ud0s1 l1; /* sm */
    gm_t2792tsg5eg72 l2; /* diags */
    gm_trqfvmjtt4sij l3; /* src */
    gm_tm3mvv7hooqkm ret;
};
struct gm_fr_rename_47b7og5 {
    gm_frame h;
    gm_tjt4ka19str13 l3; /* a */
    gm_b16_8 ret;
    gm_b16_8 w0;
};
struct gm_fr_rename_in_nstmasl {
    gm_frame h;
    gm_trqfvmjtt4sij l4; /* edits */
    gm_tm4pc0emhrv1g l11; /* t */
    gm_trqfvmjtt4sij l20; /* out */
    gm_b16_8 ret;
};
struct gm_fr_scan_e9v4omo {
    gm_frame h;
    gm_tin97tt4ud0s1 l1; /* sm */
    gm_t2792tsg5eg72 l3; /* diags */
    gm_tr8dvlu9ik4gj l4; /* out */
    gm_t4gut2c9fj1kt l8; /* d */
    gm_t0to1u196c9pc ret;
};
struct gm_fr_sort_by_jo6rcsi {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_source_map_4v8dg4b {
    gm_frame h;
    gm_tin97tt4ud0s1 ret;
};
struct gm_fr_syntax_errors_fk23f5t {
    gm_frame h;
    gm_tjt4ka19str13 l2; /* a */
    gm_b16_8 ret;
    gm_b16_8 w0;
};
struct gm_fr_syntax_errors_in_o8fnm4b {
    gm_frame h;
    gm_tin97tt4ud0s1 l10; /* sm */
    gm_t2792tsg5eg72 l11; /* diags */
    gm_trqfvmjtt4sij l12; /* src */
    gm_b16_8 ret;
};
struct gm_fr_truncate_ov4pgoq {
    gm_frame h;
    gm_b0_1 ret;
};
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b);
inline bool gm_eqv3tj1pu43g(const uint8_t *a, const uint8_t *b);
__attribute__((aligned(4))) uint32_t gm_f_add_pva1tcq(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_cs6mnftt383dnt gm_f_allocator_5m783ja(void * a0);
__attribute__((aligned(4))) void gm_f_classify_in_4kp1t31(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_classify_rm38cme(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) uint8_t gm_f_contains_gkki4kq(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_deinit_sejdpqv(void * a0);
__attribute__((aligned(4))) int64_t gm_f_depth_js5aj0d(gm_b16_8 a0);
inline __attribute__((aligned(4))) void gm_f_diagnostics_n3kkhf4(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_dupe_g9iqpvn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, const void * a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_1th396a(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_5nn7ph0(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_8jt91p4(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_fcinhvp(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_lpi80k4_2(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_s801pu6_2(void);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_extend_0cs85gs(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_find_nab29co(gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) uint8_t gm_f_fn_8no2c2v(void * a0, void * a1);
__attribute__((aligned(4))) uint8_t gm_f_has_attr_7fhere0(gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) uint8_t gm_f_has_errors_if7ai0m(void * a0);
__attribute__((aligned(4))) void gm_f_introsort_3cueg4e(gm_b16_8 a0, gm_closure a1);
__attribute__((aligned(4))) uint8_t gm_f_is_blank_asoolmf(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_definition_3en3eog(const void * a0);
__attribute__((aligned(4))) uint8_t gm_f_is_definition_3en3eog_c(gm_t2gtbh39ptekd a0);
inline __attribute__((aligned(4))) void gm_f_lex_dc8mbmf(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint32_t a0, gm_b16_8 a1, void * a2);
inline __attribute__((aligned(4))) void gm_f_list_293fcpb(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_bvkp9ir(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_epvducg(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_f0gjjbh_2(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_jekodor_2(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_prbqo9j(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_lookup_var_3vpm3u6(void *gm_out, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_th2lqbo77pvsc gm_f_lookup_var_3vpm3u6_c(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) uint8_t gm_f_may_mutate_uj4e7jp(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) void gm_f_new_jr5ejs0(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_new_lexer_dm12g12(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint32_t a0, gm_b16_8 a1, void * a2);
__attribute__((aligned(4))) uint8_t gm_f_open_brackets_hicehud(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_own_46jsjq2(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_parse_file_70g7vok(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint32_t a0, void * a1, void * a2, void * a3);
inline __attribute__((aligned(4))) void gm_f_parse_items_g2pt6ce(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_parse_source_i1af11u(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, void * a1, void * a2);
__attribute__((aligned(4))) void gm_f_parse_stmts_mefdnbk(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_1e500nc_2(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_3ef746b(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_6fomkur(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_k96mobn_2(void * a0, const void * a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_rename_47b7og5(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b16_8 gm_f_rename_in_nstmasl(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b16_8 gm_f_render_region_ppot3lm(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint32_t a2, uint32_t a3, gm_b16_8 a4, gm_t9qkh0rsooc8d a5);
__attribute__((aligned(4))) void gm_f_run_v26is74(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_scan_e9v4omo(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) void gm_f_sort_by_jo6rcsi(gm_b16_8 a0, gm_closure a1);
inline __attribute__((aligned(4))) void gm_f_source_map_4v8dg4b(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_b16_8 gm_f_syntax_errors_fk23f5t(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_t9qkh0rsooc8d a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_syntax_errors_in_o8fnm4b(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_t9qkh0rsooc8d a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_trim_end_6sjv6rg(gm_b16_8 a0);
inline __attribute__((aligned(4))) void gm_f_truncate_ov4pgoq(void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_qe3m8jq(void * a0, const void * a1);
extern uint8_t gm_cbms0bo4q1a[16] __attribute__((aligned(8)));
extern uint8_t gm_cfc3i2mbui9[16] __attribute__((aligned(8)));
extern uint8_t gm_cjlj93gp0or[24] __attribute__((aligned(8)));
extern void *const gm_hd3t1u951rul[];
extern uint8_t gm_k3mmr0p0mh8[40] __attribute__((aligned(8)));
extern uint8_t gm_k61ski6fvbu[40] __attribute__((aligned(8)));
extern uint8_t gm_kd6k1g0uehn[40] __attribute__((aligned(8)));
extern uint8_t gm_klim7pfh497[40] __attribute__((aligned(8)));
extern uint8_t gm_kmgp10tqfjk[40] __attribute__((aligned(8)));
extern uint8_t gm_krt0ppukvqv[40] __attribute__((aligned(8)));
extern const uint8_t gm_s5t1b3eks758h[27];
extern const uint8_t gm_sb3u9q4alu9bg[7];
extern const uint8_t gm_sfcm41q17pr37[46];
extern const uint8_t gm_sptnm8c6c52dr[7];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_src3d0v42kavhv7[];
extern const char gm_srcip69en21ht7v[];
extern const char gm_srcr4magk9naq4q[];
extern const char gm_srcrgc9bkquf6nr[];
extern const uint8_t gm_sshj97ls6n4f3[18];
extern const gm_type gm_tyo86ks9qkvt;
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b) {
    uintptr_t n = GM_LD(uintptr_t, a + 8);
    if (n != GM_LD(uintptr_t, b + 8)) return false;
    const uint8_t *pa = GM_LD(void *, a), *pb = GM_LD(void *, b);
    return n == 0 || memcmp(pa, pb, n * 1) == 0;
}
inline bool gm_eqv3tj1pu43g(const uint8_t *a, const uint8_t *b) {
    uint8_t ta = GM_LD(uint8_t, a + 0), tb = GM_LD(uint8_t, b + 0);
    if (ta != tb) return false;
    return true;
}
inline __attribute__((aligned(4))) gm_cs6mnftt383dnt gm_f_allocator_5m783ja(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_allocator_5m783ja F;
    void * gm_l0; /* a */
    GM_STACK_CHECK_SHALLOW(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    void * t2 = t1;
    void * t3 = (void *)((uint8_t *)gm_cjlj93gp0or);
    gm_t3mf58gv9kf3n t4;
    GM_ST(void *, ((uint8_t *)&t4 + 0), t2);
    GM_ST(void *, ((uint8_t *)&t4 + 8), t3);
    F.ret = t4;
gm_fret:
    return GM_CONV(gm_cs6mnftt383dnt, F.ret);
gm_fprop:
    return GM_CONV(gm_cs6mnftt383dnt, F.ret);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_5nn7ph0(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_5nn7ph0 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_8jt91p4(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_8jt91p4 F;
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
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 80)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_fcinhvp(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_fcinhvp F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_lpi80k4_2(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_lpi80k4_2 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_s801pu6_2(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_s801pu6_2 F;
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
inline __attribute__((aligned(4))) uint8_t gm_f_fn_8no2c2v(void * a0, void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_fn_8no2c2v F;
    void * gm_l0; /* x */
    void * gm_l1; /* y */
    GM_STACK_CHECK_SHALLOW(&F, 51u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    uint64_t t2 = GM_LD(uint64_t, (((uint8_t *)t1) + 0));
    void * t3 = gm_l1;
    uint64_t t4 = GM_LD(uint64_t, (((uint8_t *)t3) + 0));
    uint8_t t5 = (t2 > t4);
    F.ret = t5;
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_definition_3en3eog(const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_definition_3en3eog F;
    GM_STACK_CHECK_SHALLOW(&F, 8u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    uint8_t t1;
    uint8_t t6 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t6 != ((uint8_t)36ULL)) goto pa5;
    goto po4;
pa5:;
    uint8_t t8 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t8 != ((uint8_t)32ULL)) goto pa7;
    goto po4;
pa7:;
    uint8_t t10 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t10 != ((uint8_t)33ULL)) goto pa9;
    goto po4;
pa9:;
    uint8_t t12 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t12 != ((uint8_t)34ULL)) goto pa11;
    goto po4;
pa11:;
    uint8_t t14 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t14 != ((uint8_t)38ULL)) goto pa13;
    goto po4;
pa13:;
    goto mn3;
po4:;
    t1 = ((uint8_t)1);
    goto me2;
mn3:;
    t1 = ((uint8_t)0);
    goto me2;
mn15:;
    gm_trap("no match arm matched the scrutinee", gm_srcr4magk9naq4q, 146, 52);
me2:;
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
inline __attribute__((aligned(4))) void gm_f_list_293fcpb(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_293fcpb F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_fcinhvp();
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
inline __attribute__((aligned(4))) void gm_f_list_bvkp9ir(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_bvkp9ir F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_8jt91p4();
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
inline __attribute__((aligned(4))) void gm_f_list_f0gjjbh_2(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_f0gjjbh_2 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_lpi80k4_2();
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
inline __attribute__((aligned(4))) void gm_f_list_jekodor_2(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_jekodor_2 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_s801pu6_2();
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
inline __attribute__((aligned(4))) void gm_f_new_jr5ejs0(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_new_jr5ejs0 F;
    GM_STACK_CHECK(&F, 16u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_node *pn1 = gm_find(cx, 587034433);
    if (!pn1) gm_trap("no handler installed for effect `Alloc`", gm_srcip69en21ht7v, 41, 55);
    gm_t3mf58gv9kf3n t2;
    t2 = ((gm_t3mf58gv9kf3n (*)(void *, gm_node *, gm_node *))pn1->clauses[0])(pn1->env, pn1->hctx, pn1);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    GM_ST(gm_t3mf58gv9kf3n, (uint8_t *)&(*(gm_tjt4ka19str13 *)gm_out) + 0, t2);
    GM_ST(void *, (uint8_t *)&(*(gm_tjt4ka19str13 *)gm_out) + 16, ((void *)0));
    GM_ST(uint64_t, (uint8_t *)&(*(gm_tjt4ka19str13 *)gm_out) + 24, ((uint64_t)0ULL));
    GM_ST(uint64_t, (uint8_t *)&(*(gm_tjt4ka19str13 *)gm_out) + 32, ((uint64_t)0ULL));
    GM_ST(uint64_t, (uint8_t *)&(*(gm_tjt4ka19str13 *)gm_out) + 40, ((uint64_t)0ULL));
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
inline __attribute__((aligned(4))) void gm_f_parse_items_g2pt6ce(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_parse_items_g2pt6ce F;
    gm_b16_8 gm_l0; /* text */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_source_map_4v8dg4b((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_f_diagnostics_n3kkhf4((void *)&F.l2, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_b16_8 t2 = gm_l0;
        void * t3 = (void *)((uint8_t *)&F.l1);
        void * t4 = (void *)((uint8_t *)&F.l2);
        gm_f_parse_source_i1af11u((void *)&(*(gm_tm3mvv7hooqkm *)gm_out), (void *)NULL, cx, NULL, t2, t3, t4);
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
inline __attribute__((aligned(4))) void gm_f_sort_by_jo6rcsi(gm_b16_8 a0, gm_closure a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_sort_by_jo6rcsi F;
    gm_b16_8 gm_l0; /* s */
    gm_closure gm_l1; /* less */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        gm_b16_8 t2 = gm_l0;
        gm_closure t3 = gm_l1;
        gm_f_introsort_3cueg4e(t2, t3);
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
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_source_map_4v8dg4b(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_source_map_4v8dg4b F;
    GM_STACK_CHECK(&F, 40u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_trqfvmjtt4sij t1;
    gm_f_list_bvkp9ir((void *)&t1, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    GM_ST(gm_trqfvmjtt4sij, (uint8_t *)&(*(gm_tin97tt4ud0s1 *)gm_out) + 0, t1);
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
__attribute__((aligned(4))) void gm_f_classify_in_4kp1t31(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_classify_in_4kp1t31 F;
    gm_b16_8 gm_l0; /* text */
    gm_t0to1u196c9pc gm_l1; /* s */
    gm_t08olq97rscn3 gm_l5; /* tree */
    uint8_t gm_l7; /* all_decls */
    gm_b16_8 gm_l8; /* <tmp> */
    uint64_t gm_l9; /* <tmp> */
    uint8_t gm_l12; /* plain_const */
    gm_b16_8 gm_l14; /* <tmp> */
    uint64_t gm_l15; /* <tmp> */
    gm_t08olq97rscn3 gm_l16; /* n */
    gm_t08olq97rscn3 gm_l18; /* tree */
    void * gm_l20; /* f */
    uint32_t gm_l22; /* off */
    gm_b16_8 gm_l24; /* <tmp> */
    uint64_t gm_l25; /* <tmp> */
    gm_b16_8 gm_l28; /* <tmp> */
    uint64_t gm_l29; /* <tmp> */
    gm_t08olq97rscn3 gm_l30; /* n */
    uint8_t gm_l31; /* single */
    uint32_t gm_l32; /* end */
    uint8_t gm_l33; /* stmts_at_end */
    uint8_t gm_l34; /* items_at_end */
    GM_STACK_CHECK(&F, 2319u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_b16_8 t2 = gm_l0;
        gm_f_scan_e9v4omo((void *)&gm_l1, (void *)NULL, cx, NULL, t2);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        memset((uint8_t *)&F.l2 + 57, 0, 7);
        gm_th2lqbo77pvsc t3;
        memset(&t3, 0, sizeof t3);
        GM_ST(gm_th2lqbo77pvsc, (uint8_t *)&F.l2 + 0, t3);
        gm_ti5ramhglnvdt t4;
        memset(&t4, 0, sizeof t4);
        GM_ST(gm_ti5ramhglnvdt, (uint8_t *)&F.l2 + 24, t4);
        GM_ST(uint8_t, (uint8_t *)&F.l2 + 56, ((uint8_t)0));
    }
    {
        uint8_t t6 = 0;
        uint8_t t7 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 16));
        if (!t7) {
            gm_b16_8 t8 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l1) + 0));
            gm_b16_8 t9 = t8;
            gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
            int64_t t11 = gm_f_depth_js5aj0d(t10);
            uint8_t t12 = (t11 > ((int64_t)0LL));
            t6 = t12;
        } else t6 = 1;
        if (!t6) goto ie5;
        {
            memset((uint8_t *)&(*(gm_t9feugf5hcdtg *)gm_out) + 57, 0, 7);
            gm_th2lqbo77pvsc t14;
            memset(&t14, 0, sizeof t14);
            GM_ST(gm_th2lqbo77pvsc, (uint8_t *)&(*(gm_t9feugf5hcdtg *)gm_out) + 0, t14);
            gm_ti5ramhglnvdt t15;
            memset(&t15, 0, sizeof t15);
            GM_ST(gm_ti5ramhglnvdt, (uint8_t *)&(*(gm_t9feugf5hcdtg *)gm_out) + 24, t15);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t9feugf5hcdtg *)gm_out) + 56, ((uint8_t)1));
            goto gm_fdone;
        }
        goto ix5;
ie5:;
ix5:;
    }
    {
        uint8_t t17 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 17));
        if (!t17) goto ie16;
        {
            memmove(gm_out, ((uint8_t *)&F.l2), 64);
            goto gm_fdone;
        }
        goto ix16;
ie16:;
ix16:;
    }
    {
        gm_t9feugf5hcdtg t19 = F.l2;
        F.l3 = t19;
    }
    {
        gm_b16_8 t20 = gm_l0;
        gm_f_parse_items_g2pt6ce((void *)&F.l4, (void *)NULL, cx, NULL, t20);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        if (!(((((uint8_t *)&F.l4) + 0))[32] != 0)) goto ie21;
        gm_t08olq97rscn3 t22 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&F.l4) + 0));
        gm_l5 = t22;
        {
            uint64_t t25 = GM_LD(uintptr_t, (((uint8_t *)&gm_l5) + 0) + 8);
            uint8_t t26 = (t25 > ((uint64_t)0ULL));
            if (!t26) goto ie24;
            {
                gm_f_list_f0gjjbh_2((void *)&F.l6, (void *)NULL, cx, NULL);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                gm_l7 = ((uint8_t)1);
            }
            {
                {
                    gm_b16_8 t29 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l5) + 0));
                    gm_l8 = t29;
                }
                {
                    gm_l9 = ((uint64_t)0ULL);
                }
                {
                    for (;;) {
                        uint64_t t31 = gm_l9;
                        uint64_t t32 = GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8);
                        uint8_t t33 = (t31 < t32);
                        if (!t33) goto lb30;
                        {
                            uint64_t t36 = gm_l9;
                            uint64_t t37 = (uintptr_t)t36;
                            uint64_t t38 = GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8);
                            gm_tvpnktv5fo8nl t41 = GM_LD(gm_tvpnktv5fo8nl, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l8)) + t37 * 160));
                            F.l10 = t41;
                            {
                                gm_f_list_epvducg((void *)&F.l11, (void *)NULL, cx, NULL);
                                if (gm_unw.kind) {
                                    goto gm_ll34;
                                }
                            }
                            {
                                gm_l12 = ((uint8_t)0);
                            }
                            {
                                uint8_t t44 = GM_LD(uint8_t, ((((uint8_t *)&F.l10) + 64) + 0));
                                if (t44 != ((uint8_t)0ULL)) goto ie43;
                                gm_tv2uf4leil5hq t45 = GM_LD(gm_tv2uf4leil5hq, ((((uint8_t *)&F.l10) + 64) + 8));
                                F.l13 = t45;
                                {
                                    {
                                        gm_b16_8 t48 = GM_LD(gm_b16_8, (((uint8_t *)&F.l13) + 8));
                                        gm_l14 = t48;
                                    }
                                    {
                                        gm_l15 = ((uint64_t)0ULL);
                                    }
                                    {
                                        for (;;) {
                                            uint64_t t50 = gm_l15;
                                            uint64_t t51 = GM_LD(uintptr_t, ((uint8_t *)&gm_l14) + 8);
                                            uint8_t t52 = (t50 < t51);
                                            if (!t52) goto lb49;
                                            {
                                                uint64_t t55 = gm_l15;
                                                uint64_t t56 = (uintptr_t)t55;
                                                uint64_t t57 = GM_LD(uintptr_t, ((uint8_t *)&gm_l14) + 8);
                                                gm_t08olq97rscn3 t60 = GM_LD(gm_t08olq97rscn3, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l14)) + t56 * 32));
                                                gm_l16 = t60;
                                                {
                                                    void * t62 = (void *)((uint8_t *)&F.l11);
                                                    gm_b16_8 t63 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l16) + 0));
                                                    gm_tftb1odc739t5 t64 = gm_f_push_3ef746b(t62, t63);
                                                    gm_tftb1odc739t5 t65 = t64;
                                                    if (((((uint8_t *)&t65))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t65) + 1, gm_srcr4magk9naq4q, 183, 40);
                                                    gm_b0_1 t66 = GM_LD(gm_b0_1, (((uint8_t *)&t65) + 1));
                                                }
                                                goto me58;
mn59:;
                                                gm_trap("no match arm matched the scrutinee", gm_srcr4magk9naq4q, 183, 21);
me58:;
                                            }
lc49:;
                                            {
                                                uint64_t t68 = gm_l15;
                                                uint64_t r69; bool o70 = __builtin_add_overflow(t68, ((uint64_t)1ULL), &r69);
                                                uint64_t t71 = r69;
                                                gm_l15 = t71;
                                            }
                                        }
lb49:;
                                    }
                                }
                                {
                                    uint8_t t72 = 0;
                                    uint8_t t73 = 0;
                                    uint8_t t74 = 0;
                                    uint8_t t75 = ((GM_LD(uint8_t, (((uint8_t *)&F.l13) + 32) + 0) == ((uint8_t)0ULL)));
                                    if (t75) {
                                        void * t76 = GM_LD(void *, (((uint8_t *)&F.l13) + 24));
                                        void * t77 = t76;
                                        uint8_t t78;
                                        if ((GM_LD(void *, ((uint8_t *)&t77)) != 0)) goto mn80;
                                        t78 = ((uint8_t)1);
                                        goto me79;
mn80:;
                                        t78 = ((uint8_t)0);
                                        goto me79;
mn81:;
                                        gm_trap("no match arm matched the scrutinee", gm_srcr4magk9naq4q, 184, 56);
me79:;
                                        t74 = t78;
                                    } else t74 = 0;
                                    if (t74) {
                                        void * t82 = GM_LD(void *, (((uint8_t *)&F.l13) + 40));
                                        uint8_t t83 = gm_f_is_definition_3en3eog(&(*(gm_t2gtbh39ptekd *)(((uint8_t *)t82) + 8)));
                                        uint8_t t84 = (uint8_t)!t83;
                                        t73 = t84;
                                    } else t73 = 0;
                                    if (t73) {
                                        gm_b16_8 t85 = GM_LD(gm_b16_8, (((uint8_t *)&F.l10) + 40));
                                        gm_b16_8 t86 = t85;
                                        gm_b16_8 t87 = GM_LD(gm_b16_8, ((uint8_t *)&t86));
                                        gm_b16_8 t88;
                                        GM_ST(void *, (uint8_t *)&t88, (void *)(gm_sptnm8c6c52dr));
                                        GM_ST(uintptr_t, (uint8_t *)&t88 + 8, (uintptr_t)(6));
                                        uint8_t t89 = gm_f_has_attr_7fhere0(t87, t88);
                                        uint8_t t90 = (uint8_t)!t89;
                                        t72 = t90;
                                    } else t72 = 0;
                                    gm_l12 = t72;
                                }
                                {
                                    uint8_t t92 = ((GM_LD(uint8_t, (((uint8_t *)&F.l13) + 32) + 0) == ((uint8_t)1ULL)));
                                    if (!t92) goto ie91;
                                    gm_b16_8 t93 = GM_LD(gm_b16_8, (((uint8_t *)&F.l10) + 40));
                                    gm_b16_8 t94 = t93;
                                    gm_b16_8 t95 = GM_LD(gm_b16_8, ((uint8_t *)&t94));
                                    gm_b16_8 t96;
                                    GM_ST(void *, (uint8_t *)&t96, (void *)(gm_sptnm8c6c52dr));
                                    GM_ST(uintptr_t, (uint8_t *)&t96 + 8, (uintptr_t)(6));
                                    uint8_t t97 = gm_f_has_attr_7fhere0(t95, t96);
                                    uint8_t t98 = (uint8_t)!t97;
                                    if (!t98) goto ie91;
                                    {
                                        gm_l7 = ((uint8_t)0);
                                    }
                                    goto ix91;
ie91:;
ix91:;
                                }
                                goto ix43;
ie43:;
ix43:;
                            }
                            {
                                void * t100 = (void *)((uint8_t *)&F.l6);
                                gm_tk9n3vlila9qg t101;
                                memset((uint8_t *)&t101 + 33, 0, 7);
                                gm_b16_8 t102 = GM_LD(gm_b16_8, (((uint8_t *)&F.l11) + 0));
                                GM_ST(gm_b16_8, (uint8_t *)&t101 + 0, t102);
                                uint64_t t103 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                                void * t104 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)));
                                uint32_t t105 = GM_LD(uint32_t, ((((uint8_t *)&F.l10) + 4) + 4));
                                uint64_t t106 = (uint64_t)t105;
                                uint64_t t107 = (uintptr_t)t106;
                                uint32_t t108 = GM_LD(uint32_t, ((((uint8_t *)&F.l10) + 4) + 8));
                                uint64_t t109 = (uint64_t)t108;
                                uint64_t t110 = (uintptr_t)t109;
                                if (t107 > t110 || t110 > (uintptr_t)t103) gm_trap_slice(t107, t110, t103, gm_srcr4magk9naq4q, 188, 59);
                                gm_b16_8 t111;
                                GM_ST(void *, (uint8_t *)&t111, (void *)(((uint8_t *)t104 + t107 * 1)));
                                GM_ST(uintptr_t, (uint8_t *)&t111 + 8, (uintptr_t)(t110 - t107));
                                GM_ST(gm_b16_8, (uint8_t *)&t101 + 16, t111);
                                uint8_t t112 = gm_l12;
                                GM_ST(uint8_t, (uint8_t *)&t101 + 32, t112);
                                gm_tftb1odc739t5 t113 = gm_f_push_1e500nc_2(t100, &t101);
                                gm_tftb1odc739t5 t114 = t113;
                                if (((((uint8_t *)&t114))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t114) + 1, gm_srcr4magk9naq4q, 188, 17);
                                gm_b0_1 t115 = GM_LD(gm_b0_1, (((uint8_t *)&t114) + 1));
                            }
                            goto me39;
mn40:;
                            gm_trap("no match arm matched the scrutinee", gm_srcr4magk9naq4q, 179, 13);
me39:;
                        }
lc30:;
                        {
                            uint64_t t117 = gm_l9;
                            uint64_t r118; bool o119 = __builtin_add_overflow(t117, ((uint64_t)1ULL), &r118);
                            uint64_t t120 = r118;
                            gm_l9 = t120;
                        }
                    }
gm_ll34:;
                    if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb30; }
                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc30; }
                    }
                    goto gm_lad;
lb30:;
                }
            }
            {
                uint8_t t122 = gm_l7;
                if (!t122) goto ie121;
                {
                    gm_b16_8 t124 = GM_LD(gm_b16_8, (((uint8_t *)&F.l6) + 0));
                    gm_th2lqbo77pvsc t125;
                    GM_ST(uint8_t, (uint8_t *)&t125 + 17, 0);
                    GM_ST(uint16_t, (uint8_t *)&t125 + 18, 0);
                    GM_ST(uint32_t, (uint8_t *)&t125 + 20, 0);
                    GM_ST(gm_b16_8, ((uint8_t *)&t125), t124);
                    ((uint8_t *)&t125)[16] = 1;
                    GM_ST(gm_th2lqbo77pvsc, (((uint8_t *)&F.l3) + 0), t125);
                }
                goto ix121;
ie121:;
ix121:;
            }
            goto ix24;
ie24:;
ix24:;
        }
        goto ix21;
ie21:;
ix21:;
    }
    {
        gm_b16_8 t126 = gm_l0;
        gm_f_parse_stmts_mefdnbk((void *)&F.l17, (void *)NULL, cx, NULL, t126);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        if (!(((((uint8_t *)&F.l17) + 0))[32] != 0)) goto ie127;
        gm_t08olq97rscn3 t128 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&F.l17) + 0));
        gm_l18 = t128;
        {
            uint64_t t131 = GM_LD(uintptr_t, (((uint8_t *)&gm_l18) + 0) + 8);
            uint8_t t132 = ((t131 == ((uint64_t)1ULL)));
            if (!t132) goto ie130;
            {
                uint64_t t135 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t136 = GM_LD(uintptr_t, (((uint8_t *)&gm_l18) + 0) + 8);
                if (t135 >= (uintptr_t)(t136)) gm_trap_index(t135, t136, gm_srcr4magk9naq4q, 196, 31);
                uint8_t t137 = GM_LD(uint8_t, ((((uint8_t *)GM_LD(void *, (((uint8_t *)&gm_l18) + 0)) + t135 * 160) + 64) + 0));
                if (t137 != ((uint8_t)0ULL)) goto ie134;
                gm_tv2uf4leil5hq t138 = GM_LD(gm_tv2uf4leil5hq, ((((uint8_t *)GM_LD(void *, (((uint8_t *)&gm_l18) + 0)) + t135 * 160) + 64) + 8));
                F.l19 = t138;
                {
                    void * t141 = GM_LD(void *, (((uint8_t *)&F.l19) + 40));
                    uint8_t t142 = GM_LD(uint8_t, ((((uint8_t *)t141) + 8) + 0));
                    if (t142 != ((uint8_t)36ULL)) goto ie140;
                    void * t143 = GM_LD(void *, ((((uint8_t *)t141) + 8) + 8));
                    gm_l20 = t143;
                    {
                        void * t146 = gm_l20;
                        uint8_t t147 = GM_LD(uint8_t, ((((uint8_t *)t146) + 88) + 0));
                        if (t147 != ((uint8_t)0ULL)) goto ie145;
                        gm_t79rbdv4rafe5 t148 = GM_LD(gm_t79rbdv4rafe5, ((((uint8_t *)t146) + 88) + 8));
                        F.l21 = t148;
                        {
                            uint64_t t150 = GM_LD(uintptr_t, ((uint8_t *)gm_cfc3i2mbui9) + 8);
                            uint32_t t151 = (uint32_t)t150;
                            gm_l22 = t151;
                        }
                        {
                            gm_f_list_293fcpb((void *)&F.l23, (void *)NULL, cx, NULL);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                        }
                        {
                            {
                                gm_b16_8 t153 = GM_LD(gm_b16_8, (((uint8_t *)&F.l21) + 8));
                                gm_l24 = t153;
                            }
                            {
                                gm_l25 = ((uint64_t)0ULL);
                            }
                            {
                                for (;;) {
                                    uint64_t t155 = gm_l25;
                                    uint64_t t156 = GM_LD(uintptr_t, ((uint8_t *)&gm_l24) + 8);
                                    uint8_t t157 = (t155 < t156);
                                    if (!t157) goto lb154;
                                    {
                                        uint64_t t160 = gm_l25;
                                        uint64_t t161 = (uintptr_t)t160;
                                        uint64_t t162 = GM_LD(uintptr_t, ((uint8_t *)&gm_l24) + 8);
                                        gm_to88ae1b67aev t165 = GM_LD(gm_to88ae1b67aev, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l24)) + t161 * 144));
                                        F.l26 = t165;
                                        {
                                            uint8_t t168 = GM_LD(uint8_t, ((((uint8_t *)&F.l26) + 8) + 0));
                                            if (t168 != ((uint8_t)0ULL)) goto ie167;
                                            gm_taiafe1jasg4m t169 = GM_LD(gm_taiafe1jasg4m, ((((uint8_t *)&F.l26) + 8) + 8));
                                            F.l27 = t169;
                                            {
                                                {
                                                    gm_b16_8 t172 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l27) + 16) + 8));
                                                    gm_l28 = t172;
                                                }
                                                {
                                                    gm_l29 = ((uint64_t)0ULL);
                                                }
                                                {
                                                    for (;;) {
                                                        uint64_t t174 = gm_l29;
                                                        uint64_t t175 = GM_LD(uintptr_t, ((uint8_t *)&gm_l28) + 8);
                                                        uint8_t t176 = (t174 < t175);
                                                        if (!t176) goto lb173;
                                                        {
                                                            uint64_t t179 = gm_l29;
                                                            uint64_t t180 = (uintptr_t)t179;
                                                            uint64_t t181 = GM_LD(uintptr_t, ((uint8_t *)&gm_l28) + 8);
                                                            gm_t08olq97rscn3 t184 = GM_LD(gm_t08olq97rscn3, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l28)) + t180 * 32));
                                                            gm_l30 = t184;
                                                            {
                                                                void * t186 = (void *)((uint8_t *)&F.l23);
                                                                gm_t5hdnnftkup6m t187;
                                                                gm_b16_8 t188 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l30) + 0));
                                                                GM_ST(gm_b16_8, (uint8_t *)&t187 + 0, t188);
                                                                uint32_t t189 = GM_LD(uint32_t, ((((uint8_t *)&gm_l30) + 16) + 4));
                                                                uint32_t t190 = gm_l22;
                                                                uint32_t r191; bool o192 = __builtin_sub_overflow(t189, t190, &r191);
                                                                if (o192) gm_trap("integer overflow in `-`", gm_srcr4magk9naq4q, 204, 75);
                                                                uint32_t t193 = r191;
                                                                GM_ST(uint32_t, (uint8_t *)&t187 + 16, t193);
                                                                uint32_t t194 = GM_LD(uint32_t, ((((uint8_t *)&gm_l30) + 16) + 8));
                                                                uint32_t t195 = gm_l22;
                                                                uint32_t r196; bool o197 = __builtin_sub_overflow(t194, t195, &r196);
                                                                if (o197) gm_trap("integer overflow in `-`", gm_srcr4magk9naq4q, 204, 96);
                                                                uint32_t t198 = r196;
                                                                GM_ST(uint32_t, (uint8_t *)&t187 + 20, t198);
                                                                gm_tftb1odc739t5 t199 = gm_f_push_6fomkur(t186, &t187);
                                                                gm_tftb1odc739t5 t200 = t199;
                                                                if (((((uint8_t *)&t200))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t200) + 1, gm_srcr4magk9naq4q, 204, 37);
                                                                gm_b0_1 t201 = GM_LD(gm_b0_1, (((uint8_t *)&t200) + 1));
                                                            }
                                                            goto me182;
mn183:;
                                                            gm_trap("no match arm matched the scrutinee", gm_srcr4magk9naq4q, 203, 33);
me182:;
                                                        }
lc173:;
                                                        {
                                                            uint64_t t203 = gm_l29;
                                                            uint64_t r204; bool o205 = __builtin_add_overflow(t203, ((uint64_t)1ULL), &r204);
                                                            uint64_t t206 = r204;
                                                            gm_l29 = t206;
                                                        }
                                                    }
lb173:;
                                                }
                                            }
                                            goto ix167;
ie167:;
ix167:;
                                        }
                                        goto me163;
mn164:;
                                        gm_trap("no match arm matched the scrutinee", gm_srcr4magk9naq4q, 201, 25);
me163:;
                                    }
lc154:;
                                    {
                                        uint64_t t208 = gm_l25;
                                        uint64_t r209; bool o210 = __builtin_add_overflow(t208, ((uint64_t)1ULL), &r209);
                                        uint64_t t211 = r209;
                                        gm_l25 = t211;
                                    }
                                }
lb154:;
                            }
                        }
                        {
                            uint8_t t212 = 0;
                            uint64_t t213 = GM_LD(uintptr_t, (((uint8_t *)&F.l21) + 8) + 8);
                            uint8_t t214 = ((t213 == ((uint64_t)1ULL)));
                            if (t214) {
                                uint64_t t215 = (uintptr_t)((uint64_t)0ULL);
                                uint64_t t216 = GM_LD(uintptr_t, (((uint8_t *)&F.l21) + 8) + 8);
                                if (t215 >= (uintptr_t)(t216)) gm_trap_index(t215, t216, gm_srcr4magk9naq4q, 208, 60);
                                uint8_t t217;
                                uint8_t t220 = GM_LD(uint8_t, ((((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l21) + 8)) + t215 * 144) + 8) + 0));
                                if (t220 != ((uint8_t)2ULL)) goto mn219;
                                t217 = ((uint8_t)1);
                                goto me218;
mn219:;
                                t217 = ((uint8_t)0);
                                goto me218;
mn221:;
                                gm_trap("no match arm matched the scrutinee", gm_srcr4magk9naq4q, 208, 60);
me218:;
                                t212 = t217;
                            } else t212 = 0;
                            gm_l31 = t212;
                        }
                        {
                            uint64_t t223 = GM_LD(uintptr_t, (((uint8_t *)&F.l21) + 8) + 8);
                            uint8_t t224 = (t223 > ((uint64_t)0ULL));
                            if (!t224) goto ie222;
                            {
                                gm_th2lqbo77pvsc t226;
                                memset((uint8_t *)&t226 + 17, 0, 7);
                                gm_b16_8 t227 = GM_LD(gm_b16_8, (((uint8_t *)&F.l23) + 0));
                                GM_ST(gm_b16_8, (uint8_t *)&t226 + 0, t227);
                                uint8_t t228 = gm_l31;
                                GM_ST(uint8_t, (uint8_t *)&t226 + 16, t228);
                                gm_ti5ramhglnvdt t229;
                                GM_ST(uint8_t, (uint8_t *)&t229 + 25, 0);
                                GM_ST(uint16_t, (uint8_t *)&t229 + 26, 0);
                                GM_ST(uint32_t, (uint8_t *)&t229 + 28, 0);
                                GM_ST(gm_th2lqbo77pvsc, ((uint8_t *)&t229), t226);
                                ((uint8_t *)&t229)[24] = 1;
                                GM_ST(gm_ti5ramhglnvdt, (((uint8_t *)&F.l3) + 24), t229);
                            }
                            goto ix222;
ie222:;
ix222:;
                        }
                        goto ix145;
ie145:;
ix145:;
                    }
                    goto ix140;
ie140:;
ix140:;
                }
                goto ix134;
ie134:;
ix134:;
            }
            goto ix130;
ie130:;
ix130:;
        }
        goto ix127;
ie127:;
ix127:;
    }
    {
        uint8_t t231;
        if ((((((uint8_t *)&F.l3) + 0))[16] != 0)) goto mn233;
        t231 = ((uint8_t)1);
        goto me232;
mn233:;
        t231 = ((uint8_t)0);
        goto me232;
mn234:;
        gm_trap("no match arm matched the scrutinee", gm_srcr4magk9naq4q, 215, 8);
me232:;
        if (!t231) goto ie230;
        uint8_t t235;
        if ((((((uint8_t *)&F.l3) + 24))[24] != 0)) goto mn237;
        t235 = ((uint8_t)1);
        goto me236;
mn237:;
        t235 = ((uint8_t)0);
        goto me236;
mn238:;
        gm_trap("no match arm matched the scrutinee", gm_srcr4magk9naq4q, 215, 28);
me236:;
        if (!t235) goto ie230;
        {
            gm_b16_8 t240 = gm_l0;
            gm_b16_8 t241 = gm_f_trim_end_6sjv6rg(t240);
            gm_b16_8 t242 = t241;
            uint64_t t243 = GM_LD(uintptr_t, ((uint8_t *)&t242) + 8);
            uint32_t t244 = (uint32_t)t243;
            gm_l32 = t244;
        }
        {
            uint8_t t245 = 0;
            gm_t9805qtonbsag t246 = GM_LD(gm_t9805qtonbsag, (((uint8_t *)&F.l17) + 40));
            gm_t9805qtonbsag t247 = t246;
            uint32_t t248;
            if (((((uint8_t *)&t247))[4] != 0)) {
                uint32_t t249 = GM_LD(uint32_t, (((uint8_t *)&t247) + 0));
                t248 = t249;
            } else {
                t248 = ((uint32_t)0ULL);
            }
            uint64_t t250 = GM_LD(uintptr_t, ((uint8_t *)gm_cfc3i2mbui9) + 8);
            uint32_t t251 = (uint32_t)t250;
            uint32_t t252 = gm_l32;
            uint32_t r253; bool o254 = __builtin_add_overflow(t251, t252, &r253);
            if (o254) gm_trap("integer overflow in `+`", gm_srcr4magk9naq4q, 217, 59);
            uint32_t t255 = r253;
            uint8_t t256 = (t248 >= t255);
            if (t256) {
                uint8_t t257;
                if ((((((uint8_t *)&F.l17) + 40))[4] != 0)) goto mn259;
                t257 = ((uint8_t)0);
                goto me258;
mn259:;
                t257 = ((uint8_t)1);
                goto me258;
mn260:;
                gm_trap("no match arm matched the scrutinee", gm_srcr4magk9naq4q, 217, 96);
me258:;
                t245 = t257;
            } else t245 = 0;
            gm_l33 = t245;
        }
        {
            uint8_t t261 = 0;
            gm_t9805qtonbsag t262 = GM_LD(gm_t9805qtonbsag, (((uint8_t *)&F.l4) + 40));
            gm_t9805qtonbsag t263 = t262;
            uint32_t t264;
            if (((((uint8_t *)&t263))[4] != 0)) {
                uint32_t t265 = GM_LD(uint32_t, (((uint8_t *)&t263) + 0));
                t264 = t265;
            } else {
                t264 = ((uint32_t)0ULL);
            }
            uint32_t t266 = gm_l32;
            uint8_t t267 = (t264 >= t266);
            if (t267) {
                uint8_t t268;
                if ((((((uint8_t *)&F.l4) + 40))[4] != 0)) goto mn270;
                t268 = ((uint8_t)0);
                goto me269;
mn270:;
                t268 = ((uint8_t)1);
                goto me269;
mn271:;
                gm_trap("no match arm matched the scrutinee", gm_srcr4magk9naq4q, 218, 67);
me269:;
                t261 = t268;
            } else t261 = 0;
            gm_l34 = t261;
        }
        {
            uint8_t t272 = 0;
            uint8_t t273 = gm_l33;
            if (!t273) {
                uint8_t t274 = gm_l34;
                t272 = t274;
            } else t272 = 1;
            GM_ST(uint8_t, (((uint8_t *)&F.l3) + 56), t272);
        }
        goto ix230;
ie230:;
ix230:;
    }
    {
        memmove(gm_out, ((uint8_t *)&F.l3), 64);
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
inline __attribute__((aligned(4))) void gm_f_classify_rm38cme(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_classify_rm38cme F;
    gm_b16_8 gm_l0; /* text */
    GM_STACK_CHECK(&F, 289u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_new_jr5ejs0((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
    }
    {
        void * t3 = (void *)((uint8_t *)&F.l1);
        gm_t3mf58gv9kf3n t4 = GM_CONV(gm_t3mf58gv9kf3n, gm_f_allocator_5m783ja(t3));
        gm_t3mf58gv9kf3n t5 = t4;
        gm_node wn6 = {.eff = 587034433, .kind = GM_DECL, .clauses = gm_hd3t1u951rul, .env = (void *)&t5, .hctx = cx, .with_frame = &F.h, .with_label = 0, .result = (void *)&F.w0, .fw = NULL, .next = cx};
        gm_b16_8 t10 = gm_l0;
        gm_t9feugf5hcdtg t11;
        gm_f_classify_in_4kp1t31((void *)&t11, (void *)NULL, (&wn6), NULL, t10);
        if (gm_unw.kind) {
            goto gm_ll8;
        }
        gm_t9feugf5hcdtg t12 = t11;
        F.w0 = t12;
        goto gm_ls13;
gm_ll8:;
        if (gm_unw.kind == GM_BREAK && gm_unw.frame == &F.h && gm_unw.label == 0) { gm_unw.kind = GM_NONE; goto we7; }
        goto gm_ld1_1;
gm_ls13:;
we7:;
        gm_t9feugf5hcdtg t14 = F.w0;
        F.l2 = t14;
    }
    {
        gm_f_own_46jsjq2((void *)&F.ret, (void *)NULL, cx, NULL, &(*(gm_t9feugf5hcdtg *)((uint8_t *)&F.l2)));
        if (gm_unw.kind) {
            goto gm_ld1_1;
        }
        uint8_t t15 = 0;
        gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = t15;
        goto gm_ld1_1;
    }
    {
        void * t16 = (void *)((uint8_t *)&F.l1);
        gm_f_deinit_sejdpqv(t16);
    }
    goto gm_ls17;
gm_ld1_1:;
    {
        gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
        void * t18 = (void *)((uint8_t *)&F.l1);
        gm_f_deinit_sejdpqv(t18);
        gm_unw = gm_saved;
    }
    goto gm_lad;
gm_ls17:;
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
inline __attribute__((aligned(4))) int64_t gm_f_depth_js5aj0d(gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_depth_js5aj0d F;
    gm_b16_8 gm_l0; /* tokens */
    int64_t gm_l1; /* d */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    gm_t9qkh0rsooc8d gm_l5; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 236u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_l1 = ((int64_t)0LL);
    }
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
                    uint64_t t10 = gm_l3;
                    uint64_t t11 = (uintptr_t)t10;
                    uint64_t t12 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                    gm_tm4pc0emhrv1g t15 = GM_LD(gm_tm4pc0emhrv1g, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t11 * 48));
                    F.l4 = t15;
                    {
                        uint8_t t18 = GM_LD(uint8_t, ((((uint8_t *)&F.l4) + 0) + 0));
                        if (t18 != ((uint8_t)8ULL)) goto ie17;
                        gm_t9qkh0rsooc8d t19 = GM_LD(gm_t9qkh0rsooc8d, ((((uint8_t *)&F.l4) + 0) + 8));
                        gm_l5 = t19;
                        {
                            uint8_t t26 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
                            switch (t26) { case ((uint8_t)0ULL): goto ma24; case ((uint8_t)1ULL): goto ma24; case ((uint8_t)2ULL): goto ma24; case ((uint8_t)3ULL): goto ma24; case ((uint8_t)4ULL): goto ma24; case ((uint8_t)5ULL): goto ma24; case ((uint8_t)6ULL): goto ma24; case ((uint8_t)7ULL): goto ma24; case ((uint8_t)8ULL): goto ma24; case ((uint8_t)9ULL): goto ma24; case ((uint8_t)10ULL): goto ma24; case ((uint8_t)11ULL): goto ma24; case ((uint8_t)12ULL): goto ma24; case ((uint8_t)13ULL): goto ma24; case ((uint8_t)14ULL): goto ma24; case ((uint8_t)15ULL): goto ma24; case ((uint8_t)16ULL): goto ma24; case ((uint8_t)17ULL): goto ma24; case ((uint8_t)18ULL): goto ma24; case ((uint8_t)19ULL): goto ma24; case ((uint8_t)20ULL): goto ma24; case ((uint8_t)21ULL): goto ma24; case ((uint8_t)22ULL): goto ma24; case ((uint8_t)23ULL): goto ma24; case ((uint8_t)24ULL): goto ma24; case ((uint8_t)25ULL): goto ma24; case ((uint8_t)26ULL): goto ma24; case ((uint8_t)27ULL): goto ma24; case ((uint8_t)28ULL): goto ma24; case ((uint8_t)29ULL): goto ma24; case ((uint8_t)30ULL): goto ma24; case ((uint8_t)31ULL): goto ma24; case ((uint8_t)32ULL): goto ma24; case ((uint8_t)33ULL): goto ma24; case ((uint8_t)34ULL): goto ma24; case ((uint8_t)35ULL): goto ma24; case ((uint8_t)36ULL): goto ma24; case ((uint8_t)37ULL): goto ma24; case ((uint8_t)38ULL): goto ma24; case ((uint8_t)39ULL): goto ma24; case ((uint8_t)40ULL): goto ma24; case ((uint8_t)41ULL): goto ma24; case ((uint8_t)42ULL): goto ma24; case ((uint8_t)43ULL): goto ma24; case ((uint8_t)44ULL): goto ma24; case ((uint8_t)45ULL): goto ma24; case ((uint8_t)46ULL): goto ma24; case ((uint8_t)47ULL): goto ma24; case ((uint8_t)48ULL): goto ma24; case ((uint8_t)49ULL): goto ma24; case ((uint8_t)50ULL): goto ma24; case ((uint8_t)51ULL): goto ma24; case ((uint8_t)52ULL): goto ma24; case ((uint8_t)53ULL): goto ma24; case ((uint8_t)54ULL): goto ma24; case ((uint8_t)55ULL): goto ma24; case ((uint8_t)56ULL): goto ma24; case ((uint8_t)57ULL): goto ma24; case ((uint8_t)58ULL): goto ma24; case ((uint8_t)59ULL): goto ma24; case ((uint8_t)60ULL): goto ma24; case ((uint8_t)61ULL): goto ma22; case ((uint8_t)62ULL): goto ma23; case ((uint8_t)63ULL): goto ma22; case ((uint8_t)64ULL): goto ma23; case ((uint8_t)65ULL): goto ma22; case ((uint8_t)66ULL): goto ma23; case ((uint8_t)67ULL): goto ma24; case ((uint8_t)68ULL): goto ma24; default: goto ma24; }
ma22:;
                            uint8_t t30 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
                            if (t30 != ((uint8_t)61ULL)) goto pa29;
                            goto po28;
pa29:;
                            uint8_t t32 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
                            if (t32 != ((uint8_t)63ULL)) goto pa31;
                            goto po28;
pa31:;
                            uint8_t t34 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
                            if (t34 != ((uint8_t)65ULL)) goto pa33;
                            goto po28;
pa33:;
                            goto mn27;
po28:;
                            {
                                int64_t t36 = gm_l1;
                                int64_t r37; bool o38 = __builtin_add_overflow(t36, ((int64_t)1LL), &r37);
                                if (o38) gm_trap("integer overflow in `+`", gm_srcr4magk9naq4q, 76, 50);
                                int64_t t39 = r37;
                                gm_l1 = t39;
                            }
                            goto me21;
mn27:;
ma23:;
                            uint8_t t43 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
                            if (t43 != ((uint8_t)62ULL)) goto pa42;
                            goto po41;
pa42:;
                            uint8_t t45 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
                            if (t45 != ((uint8_t)64ULL)) goto pa44;
                            goto po41;
pa44:;
                            uint8_t t47 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
                            if (t47 != ((uint8_t)66ULL)) goto pa46;
                            goto po41;
pa46:;
                            goto mn40;
po41:;
                            {
                                int64_t t49 = gm_l1;
                                int64_t r50; bool o51 = __builtin_sub_overflow(t49, ((int64_t)1LL), &r50);
                                if (o51) gm_trap("integer overflow in `-`", gm_srcr4magk9naq4q, 77, 50);
                                int64_t t52 = r50;
                                gm_l1 = t52;
                            }
                            goto me21;
mn40:;
ma24:;
                            goto me21;
mn53:;
ma25:;
                            gm_trap("no match arm matched the scrutinee", gm_srcr4magk9naq4q, 75, 13);
me21:;
                        }
                        goto ix17;
ie17:;
ix17:;
                    }
                    goto me13;
mn14:;
                    gm_trap("no match arm matched the scrutinee", gm_srcr4magk9naq4q, 73, 5);
me13:;
                }
lc4:;
                {
                    uint64_t t56 = gm_l3;
                    uint64_t r57; bool o58 = __builtin_add_overflow(t56, ((uint64_t)1ULL), &r57);
                    uint64_t t59 = r57;
                    gm_l3 = t59;
                }
            }
lb4:;
        }
    }
    {
        int64_t t60 = gm_l1;
        uint8_t t61 = 0;
        F.ret = t60;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) uint8_t gm_f_fn_8no2c2v(void * a0, void * a1);
inline __attribute__((aligned(4))) uint8_t gm_f_has_attr_7fhere0(gm_b16_8 a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_has_attr_7fhere0 F;
    gm_b16_8 gm_l0; /* attrs */
    gm_b16_8 gm_l1; /* name */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    GM_STACK_CHECK_SHALLOW(&F, 270u);
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
                    uint64_t t10 = gm_l3;
                    uint64_t t11 = (uintptr_t)t10;
                    uint64_t t12 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                    gm_tkacitepg5fk0 t15 = GM_LD(gm_tkacitepg5fk0, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t11 * 72));
                    F.l4 = t15;
                    {
                        gm_b16_8 t18 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l4) + 0) + 0));
                        gm_b16_8 t19 = gm_l1;
                        gm_b16_8 t20 = t18;
                        gm_b16_8 t21 = t19;
                        uint8_t t22 = (gm_eq0iqiubfm7e(((uint8_t *)&t20), ((uint8_t *)&t21)));
                        if (!t22) goto ie17;
                        {
                            uint8_t t24 = 0;
                            F.ret = ((uint8_t)1);
                            goto gm_fret;
                        }
                        goto ix17;
ie17:;
ix17:;
                    }
                    goto me13;
mn14:;
                    gm_trap("no match arm matched the scrutinee", gm_srcr4magk9naq4q, 152, 5);
me13:;
                }
lc4:;
                {
                    uint64_t t26 = gm_l3;
                    uint64_t r27; bool o28 = __builtin_add_overflow(t26, ((uint64_t)1ULL), &r27);
                    uint64_t t29 = r27;
                    gm_l3 = t29;
                }
            }
lb4:;
        }
    }
    {
        uint8_t t30 = 0;
        F.ret = ((uint8_t)0);
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_blank_asoolmf(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_is_blank_asoolmf F;
    gm_b16_8 gm_l0; /* text */
    gm_t0to1u196c9pc gm_l2; /* s */
    uint8_t gm_l3; /* blank */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    GM_STACK_CHECK(&F, 276u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_new_jr5ejs0((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
    }
    {
        void * t3 = (void *)((uint8_t *)&F.l1);
        gm_t3mf58gv9kf3n t4 = GM_CONV(gm_t3mf58gv9kf3n, gm_f_allocator_5m783ja(t3));
        gm_t3mf58gv9kf3n t5 = t4;
        gm_node wn6 = {.eff = 587034433, .kind = GM_DECL, .clauses = gm_hd3t1u951rul, .env = (void *)&t5, .hctx = cx, .with_frame = &F.h, .with_label = 0, .result = (void *)&F.w0, .fw = NULL, .next = cx};
        {
            gm_b16_8 t10 = gm_l0;
            gm_f_scan_e9v4omo((void *)&gm_l2, (void *)NULL, (&wn6), NULL, t10);
            if (gm_unw.kind) {
                goto gm_ll8;
            }
        }
        {
            uint8_t t11 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 18));
            uint8_t t12 = (uint8_t)!t11;
            gm_l3 = t12;
        }
        {
            {
                gm_b16_8 t14 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l2) + 0));
                gm_l4 = t14;
            }
            {
                gm_l5 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t16 = gm_l5;
                    uint64_t t17 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                    uint8_t t18 = (t16 < t17);
                    if (!t18) goto lb15;
                    {
                        uint64_t t21 = gm_l5;
                        uint64_t t22 = (uintptr_t)t21;
                        uint64_t t23 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                        gm_tm4pc0emhrv1g t26 = GM_LD(gm_tm4pc0emhrv1g, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t22 * 48));
                        F.l6 = t26;
                        {
                            uint8_t t29 = (!(GM_LD(uint8_t, (((uint8_t *)&F.l6) + 0) + 0) == ((uint8_t)9ULL)));
                            if (!t29) goto ie28;
                            uint8_t t30 = (!(GM_LD(uint8_t, (((uint8_t *)&F.l6) + 0) + 0) == ((uint8_t)10ULL)));
                            if (!t30) goto ie28;
                            {
                                gm_l3 = ((uint8_t)0);
                            }
                            goto ix28;
ie28:;
ix28:;
                        }
                        goto me24;
mn25:;
                        gm_trap("no match arm matched the scrutinee", gm_srcr4magk9naq4q, 104, 9);
me24:;
                    }
lc15:;
                    {
                        uint64_t t33 = gm_l5;
                        uint64_t r34; bool o35 = __builtin_add_overflow(t33, ((uint64_t)1ULL), &r34);
                        uint64_t t36 = r34;
                        gm_l5 = t36;
                    }
                }
lb15:;
            }
        }
        uint8_t t37 = gm_l3;
        uint8_t t38 = t37;
        F.w0 = t38;
        goto gm_ls39;
gm_ll8:;
        if (gm_unw.kind == GM_BREAK && gm_unw.frame == &F.h && gm_unw.label == 0) { gm_unw.kind = GM_NONE; goto we7; }
        goto gm_ld1_1;
gm_ls39:;
we7:;
        uint8_t t40 = F.w0;
        uint8_t t41 = 0;
        F.ret = t40;
        gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = t41;
        goto gm_ld1_1;
    }
    {
        void * t42 = (void *)((uint8_t *)&F.l1);
        gm_f_deinit_sejdpqv(t42);
    }
    goto gm_ls43;
gm_ld1_1:;
    {
        gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
        void * t44 = (void *)((uint8_t *)&F.l1);
        gm_f_deinit_sejdpqv(t44);
        gm_unw = gm_saved;
    }
    goto gm_lad;
gm_ls43:;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) uint8_t gm_f_is_definition_3en3eog(const void * a0);
__attribute__((aligned(4))) uint8_t gm_f_is_definition_3en3eog_c(gm_t2gtbh39ptekd a0) { return gm_f_is_definition_3en3eog(&a0); }
inline __attribute__((aligned(4))) void gm_f_lookup_var_3vpm3u6(void *gm_out, gm_b16_8 a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_lookup_var_3vpm3u6 F;
    gm_b16_8 gm_l0; /* vars */
    gm_b16_8 gm_l1; /* name */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    gm_t6j6smj7qoctu gm_l4; /* v */
    GM_STACK_CHECK_SHALLOW(&F, 324u);
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
                    uint64_t t10 = gm_l3;
                    uint64_t t11 = (uintptr_t)t10;
                    uint64_t t12 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                    gm_t6j6smj7qoctu t15 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t11 * 32));
                    gm_l4 = t15;
                    {
                        gm_b16_8 t18 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l4) + 0));
                        gm_b16_8 t19 = gm_l1;
                        gm_b16_8 t20 = t18;
                        gm_b16_8 t21 = t19;
                        uint8_t t22 = (gm_eq0iqiubfm7e(((uint8_t *)&t20), ((uint8_t *)&t21)));
                        if (!t22) goto ie17;
                        {
                            gm_b16_8 t24 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l4) + 16));
                            gm_th2lqbo77pvsc t25;
                            GM_ST(uint8_t, (uint8_t *)&t25 + 17, 0);
                            GM_ST(uint16_t, (uint8_t *)&t25 + 18, 0);
                            GM_ST(uint32_t, (uint8_t *)&t25 + 20, 0);
                            GM_ST(gm_b16_8, ((uint8_t *)&t25), t24);
                            ((uint8_t *)&t25)[16] = 1;
                            uint8_t t26 = 0;
                            F.ret = t25;
                            goto gm_fret;
                        }
                        goto ix17;
ie17:;
ix17:;
                    }
                    goto me13;
mn14:;
                    gm_trap("no match arm matched the scrutinee", gm_srcr4magk9naq4q, 277, 5);
me13:;
                }
lc4:;
                {
                    uint64_t t28 = gm_l3;
                    uint64_t r29; bool o30 = __builtin_add_overflow(t28, ((uint64_t)1ULL), &r29);
                    uint64_t t31 = r29;
                    gm_l3 = t31;
                }
            }
lb4:;
        }
    }
    {
        gm_th2lqbo77pvsc t32;
        memset(&t32, 0, sizeof t32);
        uint8_t t33 = 0;
        F.ret = t32;
        goto gm_fret;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_th2lqbo77pvsc gm_f_lookup_var_3vpm3u6_c(gm_b16_8 a0, gm_b16_8 a1) { gm_th2lqbo77pvsc gm_r; gm_f_lookup_var_3vpm3u6(&gm_r, a0, a1); return gm_r; }
__attribute__((aligned(4))) uint8_t gm_f_may_mutate_uj4e7jp(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_may_mutate_uj4e7jp F;
    gm_b16_8 gm_l0; /* text */
    gm_b16_8 gm_l2; /* tokens */
    uint8_t gm_l3; /* found */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    uint64_t gm_l6; /* i */
    gm_t9qkh0rsooc8d gm_l8; /* p */
    GM_STACK_CHECK(&F, 702u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_new_jr5ejs0((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
    }
    {
        void * t3 = (void *)((uint8_t *)&F.l1);
        gm_t3mf58gv9kf3n t4 = GM_CONV(gm_t3mf58gv9kf3n, gm_f_allocator_5m783ja(t3));
        gm_t3mf58gv9kf3n t5 = t4;
        gm_node wn6 = {.eff = 587034433, .kind = GM_DECL, .clauses = gm_hd3t1u951rul, .env = (void *)&t5, .hctx = cx, .with_frame = &F.h, .with_label = 0, .result = (void *)&F.w0, .fw = NULL, .next = cx};
        {
            gm_b16_8 t10 = gm_l0;
            gm_t0to1u196c9pc t11;
            gm_f_scan_e9v4omo((void *)&t11, (void *)NULL, (&wn6), NULL, t10);
            if (gm_unw.kind) {
                goto gm_ll8;
            }
            gm_t0to1u196c9pc t12 = t11;
            gm_b16_8 t13 = GM_LD(gm_b16_8, (((uint8_t *)&t12) + 0));
            gm_l2 = t13;
        }
        {
            gm_l3 = ((uint8_t)0);
        }
        {
            {
                gm_b16_8 t15 = gm_l2;
                gm_b16_8 t16 = t15;
                gm_b16_8 t17 = GM_LD(gm_b16_8, ((uint8_t *)&t16));
                gm_l4 = t17;
            }
            {
                gm_l5 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t19 = gm_l5;
                    uint64_t t20 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                    uint8_t t21 = (t19 < t20);
                    if (!t21) goto lb18;
                    {
                        gm_tfhcigdju4v01 t24;
                        uint64_t t25 = gm_l5;
                        GM_ST(uint64_t, (uint8_t *)&t24 + 0, t25);
                        uint64_t t26 = gm_l5;
                        uint64_t t27 = (uintptr_t)t26;
                        uint64_t t28 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                        memcpy((uint8_t *)&t24 + 8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t27 * 48), 48);
                        gm_tfhcigdju4v01 t29 = t24;
                        uint64_t t32 = GM_LD(uint64_t, (((uint8_t *)&t29) + 0));
                        gm_l6 = t32;
                        gm_tm4pc0emhrv1g t33 = GM_LD(gm_tm4pc0emhrv1g, (((uint8_t *)&t29) + 8));
                        F.l7 = t33;
                        {
                            uint8_t t36 = GM_LD(uint8_t, ((((uint8_t *)&F.l7) + 0) + 0));
                            if (t36 != ((uint8_t)8ULL)) goto ie35;
                            gm_t9qkh0rsooc8d t37 = GM_LD(gm_t9qkh0rsooc8d, ((((uint8_t *)&F.l7) + 0) + 8));
                            gm_l8 = t37;
                            {
                                uint8_t t40 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l8) + 0) == ((uint8_t)7ULL)));
                                if (!t40) goto ie39;
                                {
                                    uint64_t t43 = gm_l6;
                                    uint8_t t44 = ((t43 == ((uint64_t)0ULL)));
                                    if (!t44) goto ie42;
                                    {
                                        gm_l3 = ((uint8_t)1);
                                    }
                                    goto ix42;
ie42:;
                                    {
                                        uint64_t t47 = gm_l6;
                                        uint64_t r48; bool o49 = __builtin_sub_overflow(t47, ((uint64_t)1ULL), &r48);
                                        if (o49) gm_trap("integer overflow in `-`", gm_srcr4magk9naq4q, 256, 38);
                                        uint64_t t50 = r48;
                                        uint64_t t51 = (uintptr_t)t50;
                                        uint64_t t52 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                                        if (t51 >= (uintptr_t)(t52)) gm_trap_index(t51, t52, gm_srcr4magk9naq4q, 256, 31);
                                        uint8_t t57 = GM_LD(uint8_t, ((((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t51 * 48) + 0) + 0));
                                        if (t57 != ((uint8_t)0ULL)) goto pa56;
                                        goto po55;
pa56:;
                                        uint8_t t59 = GM_LD(uint8_t, ((((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t51 * 48) + 0) + 0));
                                        if (t59 != ((uint8_t)3ULL)) goto pa58;
                                        goto po55;
pa58:;
                                        goto mn54;
po55:;
                                        goto me53;
mn54:;
                                        {
                                            gm_l3 = ((uint8_t)1);
                                        }
                                        goto me53;
mn61:;
                                        gm_trap("no match arm matched the scrutinee", gm_srcr4magk9naq4q, 256, 25);
me53:;
                                    }
ix42:;
                                }
                                goto ix39;
ie39:;
ix39:;
                            }
                            {
                                uint8_t t64 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l8) + 0) == ((uint8_t)58ULL)));
                                if (!t64) goto ie63;
                                uint64_t t65 = gm_l6;
                                uint64_t r66; bool o67 = __builtin_add_overflow(t65, ((uint64_t)2ULL), &r66);
                                if (o67) gm_trap("integer overflow in `+`", gm_srcr4magk9naq4q, 262, 34);
                                uint64_t t68 = r66;
                                uint64_t t69 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                                uint8_t t70 = (t68 < t69);
                                if (!t70) goto ie63;
                                uint64_t t71 = gm_l6;
                                uint64_t r72; bool o73 = __builtin_add_overflow(t71, ((uint64_t)1ULL), &r72);
                                if (o73) gm_trap("integer overflow in `+`", gm_srcr4magk9naq4q, 262, 64);
                                uint64_t t74 = r72;
                                uint64_t t75 = (uintptr_t)t74;
                                uint64_t t76 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                                if (t75 >= (uintptr_t)(t76)) gm_trap_index(t75, t76, gm_srcr4magk9naq4q, 262, 57);
                                uint8_t t77;
                                uint8_t t80 = GM_LD(uint8_t, ((((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t75 * 48) + 0) + 0));
                                if (t80 != ((uint8_t)0ULL)) goto mn79;
                                t77 = ((uint8_t)1);
                                goto me78;
mn79:;
                                t77 = ((uint8_t)0);
                                goto me78;
mn81:;
                                gm_trap("no match arm matched the scrutinee", gm_srcr4magk9naq4q, 262, 57);
me78:;
                                if (!t77) goto ie63;
                                uint64_t t82 = gm_l6;
                                uint64_t r83; bool o84 = __builtin_add_overflow(t82, ((uint64_t)2ULL), &r83);
                                if (o84) gm_trap("integer overflow in `+`", gm_srcr4magk9naq4q, 262, 100);
                                uint64_t t85 = r83;
                                uint64_t t86 = (uintptr_t)t85;
                                uint64_t t87 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                                if (t86 >= (uintptr_t)(t87)) gm_trap_index(t86, t87, gm_srcr4magk9naq4q, 262, 93);
                                gm_t3fcbuehh3bs2 t88 = GM_LD(gm_t3fcbuehh3bs2, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t86 * 48) + 0));
                                gm_t3fcbuehh3bs2 t89 = t88;
                                gm_t9qkh0rsooc8d t90;
                                GM_ST(uint8_t, ((uint8_t *)&t90 + 0), ((uint8_t)61ULL));
                                gm_t9qkh0rsooc8d t91 = t90;
                                uint8_t t92 = (((GM_LD(uint8_t, ((uint8_t *)&t89) + 0) == ((uint8_t)8ULL)) && gm_eqv3tj1pu43g((((uint8_t *)&t89) + 8), ((uint8_t *)&t91))));
                                if (!t92) goto ie63;
                                {
                                    gm_l3 = ((uint8_t)1);
                                }
                                goto ix63;
ie63:;
ix63:;
                            }
                            goto ix35;
ie35:;
ix35:;
                        }
                        goto me30;
mn31:;
                        gm_trap("no match arm matched the scrutinee", gm_srcr4magk9naq4q, 252, 9);
me30:;
                    }
lc18:;
                    {
                        uint64_t t95 = gm_l5;
                        uint64_t r96; bool o97 = __builtin_add_overflow(t95, ((uint64_t)1ULL), &r96);
                        uint64_t t98 = r96;
                        gm_l5 = t98;
                    }
                }
lb18:;
            }
        }
        uint8_t t99 = gm_l3;
        uint8_t t100 = t99;
        F.w0 = t100;
        goto gm_ls101;
gm_ll8:;
        if (gm_unw.kind == GM_BREAK && gm_unw.frame == &F.h && gm_unw.label == 0) { gm_unw.kind = GM_NONE; goto we7; }
        goto gm_ld1_1;
gm_ls101:;
we7:;
        uint8_t t102 = F.w0;
        uint8_t t103 = 0;
        F.ret = t102;
        gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = t103;
        goto gm_ld1_1;
    }
    {
        void * t104 = (void *)((uint8_t *)&F.l1);
        gm_f_deinit_sejdpqv(t104);
    }
    goto gm_ls105;
gm_ld1_1:;
    {
        gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
        void * t106 = (void *)((uint8_t *)&F.l1);
        gm_f_deinit_sejdpqv(t106);
        gm_unw = gm_saved;
    }
    goto gm_lad;
gm_ls105:;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_open_brackets_hicehud(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_open_brackets_hicehud F;
    gm_b16_8 gm_l0; /* text */
    gm_t0to1u196c9pc gm_l2; /* s */
    GM_STACK_CHECK(&F, 184u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_new_jr5ejs0((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
    }
    {
        void * t3 = (void *)((uint8_t *)&F.l1);
        gm_t3mf58gv9kf3n t4 = GM_CONV(gm_t3mf58gv9kf3n, gm_f_allocator_5m783ja(t3));
        gm_t3mf58gv9kf3n t5 = t4;
        gm_node wn6 = {.eff = 587034433, .kind = GM_DECL, .clauses = gm_hd3t1u951rul, .env = (void *)&t5, .hctx = cx, .with_frame = &F.h, .with_label = 0, .result = (void *)&F.w0, .fw = NULL, .next = cx};
        {
            gm_b16_8 t10 = gm_l0;
            gm_f_scan_e9v4omo((void *)&gm_l2, (void *)NULL, (&wn6), NULL, t10);
            if (gm_unw.kind) {
                goto gm_ll8;
            }
        }
        uint8_t t11 = 0;
        uint8_t t12 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 16));
        if (!t12) {
            gm_b16_8 t13 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l2) + 0));
            gm_b16_8 t14 = t13;
            gm_b16_8 t15 = GM_LD(gm_b16_8, ((uint8_t *)&t14));
            int64_t t16 = gm_f_depth_js5aj0d(t15);
            uint8_t t17 = (t16 > ((int64_t)0LL));
            t11 = t17;
        } else t11 = 1;
        uint8_t t18 = t11;
        F.w0 = t18;
        goto gm_ls19;
gm_ll8:;
        if (gm_unw.kind == GM_BREAK && gm_unw.frame == &F.h && gm_unw.label == 0) { gm_unw.kind = GM_NONE; goto we7; }
        goto gm_ld1_1;
gm_ls19:;
we7:;
        uint8_t t20 = F.w0;
        uint8_t t21 = 0;
        F.ret = t20;
        gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = t21;
        goto gm_ld1_1;
    }
    {
        void * t22 = (void *)((uint8_t *)&F.l1);
        gm_f_deinit_sejdpqv(t22);
    }
    goto gm_ls23;
gm_ld1_1:;
    {
        gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
        void * t24 = (void *)((uint8_t *)&F.l1);
        gm_f_deinit_sejdpqv(t24);
        gm_unw = gm_saved;
    }
    goto gm_lad;
gm_ls23:;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) void gm_f_own_46jsjq2(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_own_46jsjq2 F;
    gm_b16_8 gm_l2; /* items */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    gm_b16_8 gm_l8; /* <tmp> */
    uint64_t gm_l9; /* <tmp> */
    gm_b16_8 gm_l10; /* n */
    gm_th2lqbo77pvsc gm_l11; /* s */
    gm_b16_8 gm_l13; /* <tmp> */
    uint64_t gm_l14; /* <tmp> */
    gm_t5hdnnftkup6m gm_l15; /* b */
    GM_STACK_CHECK(&F, 1122u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        memset((uint8_t *)&F.l1 + 57, 0, 7);
        gm_th2lqbo77pvsc t2;
        memset(&t2, 0, sizeof t2);
        GM_ST(gm_th2lqbo77pvsc, (uint8_t *)&F.l1 + 0, t2);
        gm_ti5ramhglnvdt t3;
        memset(&t3, 0, sizeof t3);
        GM_ST(gm_ti5ramhglnvdt, (uint8_t *)&F.l1 + 24, t3);
        uint8_t t4 = GM_LD(uint8_t, (((uint8_t *)a0) + 56));
        GM_ST(uint8_t, (uint8_t *)&F.l1 + 56, t4);
    }
    {
        if (!(((((uint8_t *)a0) + 0))[16] != 0)) goto ie5;
        gm_b16_8 t6 = GM_LD(gm_b16_8, (((uint8_t *)a0) + 0));
        gm_l2 = t6;
        {
            gm_f_list_f0gjjbh_2((void *)&F.l3, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            {
                gm_b16_8 t9 = gm_l2;
                gm_l4 = t9;
            }
            {
                gm_l5 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t11 = gm_l5;
                    uint64_t t12 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                    uint8_t t13 = (t11 < t12);
                    if (!t13) goto lb10;
                    {
                        uint64_t t16 = gm_l5;
                        uint64_t t17 = (uintptr_t)t16;
                        uint64_t t18 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                        gm_tk9n3vlila9qg t21 = GM_LD(gm_tk9n3vlila9qg, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t17 * 40));
                        F.l6 = t21;
                        {
                            gm_f_list_epvducg((void *)&F.l7, (void *)NULL, cx, NULL);
                            if (gm_unw.kind) {
                                goto gm_ll14;
                            }
                        }
                        {
                            {
                                gm_b16_8 t24 = GM_LD(gm_b16_8, (((uint8_t *)&F.l6) + 0));
                                gm_l8 = t24;
                            }
                            {
                                gm_l9 = ((uint64_t)0ULL);
                            }
                            {
                                for (;;) {
                                    uint64_t t26 = gm_l9;
                                    uint64_t t27 = GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8);
                                    uint8_t t28 = (t26 < t27);
                                    if (!t28) goto lb25;
                                    {
                                        uint64_t t31 = gm_l9;
                                        uint64_t t32 = (uintptr_t)t31;
                                        uint64_t t33 = GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8);
                                        gm_b16_8 t36 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l8)) + t32 * 16));
                                        gm_l10 = t36;
                                        {
                                            void * t38 = (void *)((uint8_t *)&F.l7);
                                            gm_b16_8 t39 = gm_l10;
                                            gm_tabb710d9mq2e t40;
                                            gm_f_dupe_g9iqpvn((void *)&t40, (void *)NULL, cx, NULL, t39, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kmgp10tqfjk)));
                                            if (gm_unw.kind) {
                                                goto gm_ll29;
                                            }
                                            gm_tabb710d9mq2e t41 = t40;
                                            if (((((uint8_t *)&t41))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t41) + 8, gm_srcr4magk9naq4q, 231, 44);
                                            gm_b16_8 t42 = GM_LD(gm_b16_8, (((uint8_t *)&t41) + 8));
                                            gm_b16_8 t43 = t42;
                                            gm_b16_8 t44 = GM_LD(gm_b16_8, ((uint8_t *)&t43));
                                            gm_tftb1odc739t5 t45 = gm_f_push_3ef746b(t38, t44);
                                            gm_tftb1odc739t5 t46 = t45;
                                            if (((((uint8_t *)&t46))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t46) + 1, gm_srcr4magk9naq4q, 231, 33);
                                            gm_b0_1 t47 = GM_LD(gm_b0_1, (((uint8_t *)&t46) + 1));
                                        }
                                        goto me34;
mn35:;
                                        gm_trap("no match arm matched the scrutinee", gm_srcr4magk9naq4q, 231, 13);
me34:;
                                    }
lc25:;
                                    {
                                        uint64_t t49 = gm_l9;
                                        uint64_t r50; bool o51 = __builtin_add_overflow(t49, ((uint64_t)1ULL), &r50);
                                        uint64_t t52 = r50;
                                        gm_l9 = t52;
                                    }
                                }
gm_ll29:;
                                if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb25; }
                                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc25; }
                                }
                                goto gm_ll14;
lb25:;
                            }
                        }
                        {
                            void * t53 = (void *)((uint8_t *)&F.l3);
                            gm_tk9n3vlila9qg t54;
                            memset((uint8_t *)&t54 + 33, 0, 7);
                            gm_b16_8 t55 = GM_LD(gm_b16_8, (((uint8_t *)&F.l7) + 0));
                            GM_ST(gm_b16_8, (uint8_t *)&t54 + 0, t55);
                            gm_b16_8 t56 = GM_LD(gm_b16_8, (((uint8_t *)&F.l6) + 16));
                            gm_tabb710d9mq2e t57;
                            gm_f_dupe_g9iqpvn((void *)&t57, (void *)NULL, cx, NULL, t56, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_k61ski6fvbu)));
                            if (gm_unw.kind) {
                                goto gm_ll14;
                            }
                            gm_tabb710d9mq2e t58 = t57;
                            if (((((uint8_t *)&t58))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t58) + 8, gm_srcr4magk9naq4q, 232, 54);
                            gm_b16_8 t59 = GM_LD(gm_b16_8, (((uint8_t *)&t58) + 8));
                            gm_b16_8 t60 = t59;
                            gm_b16_8 t61 = GM_LD(gm_b16_8, ((uint8_t *)&t60));
                            GM_ST(gm_b16_8, (uint8_t *)&t54 + 16, t61);
                            uint8_t t62 = GM_LD(uint8_t, (((uint8_t *)&F.l6) + 32));
                            GM_ST(uint8_t, (uint8_t *)&t54 + 32, t62);
                            gm_tftb1odc739t5 t63 = gm_f_push_1e500nc_2(t53, &t54);
                            gm_tftb1odc739t5 t64 = t63;
                            if (((((uint8_t *)&t64))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t64) + 1, gm_srcr4magk9naq4q, 232, 13);
                            gm_b0_1 t65 = GM_LD(gm_b0_1, (((uint8_t *)&t64) + 1));
                        }
                        goto me19;
mn20:;
                        gm_trap("no match arm matched the scrutinee", gm_srcr4magk9naq4q, 229, 9);
me19:;
                    }
lc10:;
                    {
                        uint64_t t67 = gm_l5;
                        uint64_t r68; bool o69 = __builtin_add_overflow(t67, ((uint64_t)1ULL), &r68);
                        uint64_t t70 = r68;
                        gm_l5 = t70;
                    }
                }
gm_ll14:;
                if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb10; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc10; }
                }
                goto gm_lad;
lb10:;
            }
        }
        {
            gm_b16_8 t71 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
            gm_th2lqbo77pvsc t72;
            GM_ST(uint8_t, (uint8_t *)&t72 + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&t72 + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&t72 + 20, 0);
            GM_ST(gm_b16_8, ((uint8_t *)&t72), t71);
            ((uint8_t *)&t72)[16] = 1;
            GM_ST(gm_th2lqbo77pvsc, (((uint8_t *)&F.l1) + 0), t72);
        }
        goto ix5;
ie5:;
ix5:;
    }
    {
        if (!(((((uint8_t *)a0) + 24))[24] != 0)) goto ie73;
        gm_th2lqbo77pvsc t74 = GM_LD(gm_th2lqbo77pvsc, (((uint8_t *)a0) + 24));
        gm_l11 = t74;
        {
            gm_f_list_293fcpb((void *)&F.l12, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            {
                gm_b16_8 t77 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l11) + 0));
                gm_l13 = t77;
            }
            {
                gm_l14 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t79 = gm_l14;
                    uint64_t t80 = GM_LD(uintptr_t, ((uint8_t *)&gm_l13) + 8);
                    uint8_t t81 = (t79 < t80);
                    if (!t81) goto lb78;
                    {
                        uint64_t t84 = gm_l14;
                        uint64_t t85 = (uintptr_t)t84;
                        uint64_t t86 = GM_LD(uintptr_t, ((uint8_t *)&gm_l13) + 8);
                        gm_t5hdnnftkup6m t89 = GM_LD(gm_t5hdnnftkup6m, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l13)) + t85 * 24));
                        gm_l15 = t89;
                        {
                            void * t91 = (void *)((uint8_t *)&F.l12);
                            gm_t5hdnnftkup6m t92;
                            gm_b16_8 t93 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l15) + 0));
                            gm_tabb710d9mq2e t94;
                            gm_f_dupe_g9iqpvn((void *)&t94, (void *)NULL, cx, NULL, t93, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_krt0ppukvqv)));
                            if (gm_unw.kind) {
                                goto gm_ll82;
                            }
                            gm_tabb710d9mq2e t95 = t94;
                            if (((((uint8_t *)&t95))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t95) + 8, gm_srcr4magk9naq4q, 238, 53);
                            gm_b16_8 t96 = GM_LD(gm_b16_8, (((uint8_t *)&t95) + 8));
                            gm_b16_8 t97 = t96;
                            gm_b16_8 t98 = GM_LD(gm_b16_8, ((uint8_t *)&t97));
                            GM_ST(gm_b16_8, (uint8_t *)&t92 + 0, t98);
                            uint32_t t99 = GM_LD(uint32_t, (((uint8_t *)&gm_l15) + 16));
                            GM_ST(uint32_t, (uint8_t *)&t92 + 16, t99);
                            uint32_t t100 = GM_LD(uint32_t, (((uint8_t *)&gm_l15) + 20));
                            GM_ST(uint32_t, (uint8_t *)&t92 + 20, t100);
                            gm_tftb1odc739t5 t101 = gm_f_push_6fomkur(t91, &t92);
                            gm_tftb1odc739t5 t102 = t101;
                            if (((((uint8_t *)&t102))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t102) + 1, gm_srcr4magk9naq4q, 238, 31);
                            gm_b0_1 t103 = GM_LD(gm_b0_1, (((uint8_t *)&t102) + 1));
                        }
                        goto me87;
mn88:;
                        gm_trap("no match arm matched the scrutinee", gm_srcr4magk9naq4q, 238, 9);
me87:;
                    }
lc78:;
                    {
                        uint64_t t105 = gm_l14;
                        uint64_t r106; bool o107 = __builtin_add_overflow(t105, ((uint64_t)1ULL), &r106);
                        uint64_t t108 = r106;
                        gm_l14 = t108;
                    }
                }
gm_ll82:;
                if (gm_unw.frame == &F.h && gm_unw.label == 2) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb78; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc78; }
                }
                goto gm_lad;
lb78:;
            }
        }
        {
            gm_th2lqbo77pvsc t109;
            memset((uint8_t *)&t109 + 17, 0, 7);
            gm_b16_8 t110 = GM_LD(gm_b16_8, (((uint8_t *)&F.l12) + 0));
            GM_ST(gm_b16_8, (uint8_t *)&t109 + 0, t110);
            uint8_t t111 = GM_LD(uint8_t, (((uint8_t *)&gm_l11) + 16));
            GM_ST(uint8_t, (uint8_t *)&t109 + 16, t111);
            gm_ti5ramhglnvdt t112;
            GM_ST(uint8_t, (uint8_t *)&t112 + 25, 0);
            GM_ST(uint16_t, (uint8_t *)&t112 + 26, 0);
            GM_ST(uint32_t, (uint8_t *)&t112 + 28, 0);
            GM_ST(gm_th2lqbo77pvsc, ((uint8_t *)&t112), t109);
            ((uint8_t *)&t112)[24] = 1;
            GM_ST(gm_ti5ramhglnvdt, (((uint8_t *)&F.l1) + 24), t112);
        }
        goto ix73;
ie73:;
ix73:;
    }
    {
        memmove(gm_out, ((uint8_t *)&F.l1), 64);
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
extern __attribute__((aligned(4))) void gm_f_parse_items_g2pt6ce(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) void gm_f_parse_source_i1af11u(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, void * a1, void * a2) {
    struct gm_fr_parse_source_i1af11u F;
    gm_b16_8 gm_l0; /* src */
    void * gm_l1; /* sm */
    void * gm_l2; /* diags */
    uint32_t gm_l3; /* id */
    uint32_t gm_l5; /* next_id */
    gm_t08olq97rscn3 gm_l6; /* tree */
    gm_t9805qtonbsag gm_l7; /* first */
    gm_b16_8 gm_l8; /* <tmp> */
    uint64_t gm_l9; /* <tmp> */
    GM_STACK_CHECK(&F, 657u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l1;
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sb3u9q4alu9bg));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(6));
        gm_b16_8 t4 = gm_l0;
        uint32_t t5 = gm_f_add_pva1tcq((void *)NULL, cx, NULL, t2, t3, t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l3 = t5;
    }
    {
        uint32_t t6 = gm_l3;
        gm_b16_8 t7 = gm_l0;
        void * t8 = gm_l2;
        gm_f_lex_dc8mbmf((void *)&F.l4, (void *)NULL, cx, NULL, t6, t7, t8);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_l5 = ((uint32_t)0ULL);
    }
    {
        uint32_t t9 = gm_l3;
        void * t10 = (void *)((uint8_t *)&F.l4);
        void * t11 = (void *)((uint8_t *)&gm_l5);
        void * t12 = gm_l2;
        gm_f_parse_file_70g7vok((void *)&gm_l6, (void *)NULL, cx, NULL, t9, t10, t11, t12);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_t9805qtonbsag t13;
        memset(&t13, 0, sizeof t13);
        gm_l7 = t13;
    }
    {
        {
            void * t15 = gm_l2;
            gm_b16_8 t16 = GM_LD(gm_b16_8, ((((uint8_t *)t15) + 0) + 0));
            gm_l8 = t16;
        }
        {
            gm_l9 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t18 = gm_l9;
                uint64_t t19 = GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8);
                uint8_t t20 = (t18 < t19);
                if (!t20) goto lb17;
                {
                    uint64_t t23 = gm_l9;
                    uint64_t t24 = (uintptr_t)t23;
                    uint64_t t25 = GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8);
                    gm_t4gut2c9fj1kt t28 = GM_LD(gm_t4gut2c9fj1kt, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l8)) + t24 * 184));
                    F.l10 = t28;
                    {
                        uint8_t t31 = 0;
                        uint8_t t32;
                        if (((((uint8_t *)&gm_l7))[4] != 0)) goto mn34;
                        t32 = ((uint8_t)1);
                        goto me33;
mn34:;
                        t32 = ((uint8_t)0);
                        goto me33;
mn35:;
                        gm_trap("no match arm matched the scrutinee", gm_srcr4magk9naq4q, 126, 12);
me33:;
                        if (!t32) {
                            uint32_t t36 = GM_LD(uint32_t, ((((uint8_t *)&F.l10) + 24) + 4));
                            gm_t9805qtonbsag t37 = gm_l7;
                            gm_t9805qtonbsag t38 = t37;
                            uint32_t t39;
                            if (((((uint8_t *)&t38))[4] != 0)) {
                                uint32_t t40 = GM_LD(uint32_t, (((uint8_t *)&t38) + 0));
                                t39 = t40;
                            } else {
                                t39 = ((uint32_t)0ULL);
                            }
                            uint8_t t41 = (t36 < t39);
                            t31 = t41;
                        } else t31 = 1;
                        if (!t31) goto ie30;
                        {
                            uint32_t t43 = GM_LD(uint32_t, ((((uint8_t *)&F.l10) + 24) + 4));
                            gm_t9805qtonbsag t44;
                            GM_ST(uint8_t, (uint8_t *)&t44 + 5, 0);
                            GM_ST(uint16_t, (uint8_t *)&t44 + 6, 0);
                            GM_ST(uint32_t, ((uint8_t *)&t44), t43);
                            ((uint8_t *)&t44)[4] = 1;
                            gm_l7 = t44;
                        }
                        goto ix30;
ie30:;
ix30:;
                    }
                    goto me26;
mn27:;
                    gm_trap("no match arm matched the scrutinee", gm_srcr4magk9naq4q, 125, 5);
me26:;
                }
lc17:;
                {
                    uint64_t t46 = gm_l9;
                    uint64_t r47; bool o48 = __builtin_add_overflow(t46, ((uint64_t)1ULL), &r47);
                    uint64_t t49 = r47;
                    gm_l9 = t49;
                }
            }
lb17:;
        }
    }
    {
        void * t51 = gm_l2;
        uint8_t t52 = gm_f_has_errors_if7ai0m(t51);
        if (!t52) goto ie50;
        {
            gm_t1s75i08qtptd t54;
            memset(&t54, 0, sizeof t54);
            GM_ST(gm_t1s75i08qtptd, (uint8_t *)&(*(gm_tm3mvv7hooqkm *)gm_out) + 0, t54);
            gm_t9805qtonbsag t55 = gm_l7;
            GM_ST(gm_t9805qtonbsag, (uint8_t *)&(*(gm_tm3mvv7hooqkm *)gm_out) + 40, t55);
            goto gm_fdone;
        }
        goto ix50;
ie50:;
ix50:;
    }
    {
        gm_t08olq97rscn3 t56 = gm_l6;
        gm_t1s75i08qtptd t57;
        GM_ST(uint8_t, (uint8_t *)&t57 + 33, 0);
        GM_ST(uint16_t, (uint8_t *)&t57 + 34, 0);
        GM_ST(uint32_t, (uint8_t *)&t57 + 36, 0);
        GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t57), t56);
        ((uint8_t *)&t57)[32] = 1;
        gm_t9805qtonbsag t58;
        memset(&t58, 0, sizeof t58);
        GM_ST(gm_t1s75i08qtptd, (uint8_t *)&(*(gm_tm3mvv7hooqkm *)gm_out) + 0, t57);
        GM_ST(gm_t9805qtonbsag, (uint8_t *)&(*(gm_tm3mvv7hooqkm *)gm_out) + 40, t58);
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
inline __attribute__((aligned(4))) void gm_f_parse_stmts_mefdnbk(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_parse_stmts_mefdnbk F;
    gm_b16_8 gm_l0; /* text */
    GM_STACK_CHECK(&F, 188u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_source_map_4v8dg4b((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_f_diagnostics_n3kkhf4((void *)&F.l2, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_f_list_uvfu1k1((void *)&F.l3, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l3);
        gm_tfeab279c16rr t3;
        gm_b16_8 t4 = GM_LD(gm_b16_8, gm_cfc3i2mbui9);
        GM_ST(gm_b16_8, (uint8_t *)&t3 + 0, t4);
        gm_b16_8 t5 = gm_l0;
        GM_ST(gm_b16_8, (uint8_t *)&t3 + 16, t5);
        gm_b16_8 t6 = GM_LD(gm_b16_8, gm_cbms0bo4q1a);
        GM_ST(gm_b16_8, (uint8_t *)&t3 + 32, t6);
        gm_tftb1odc739t5 t7 = gm_f_write_qe3m8jq(t2, &t3);
        gm_tftb1odc739t5 t8 = t7;
        if (((((uint8_t *)&t8))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t8) + 1, gm_srcr4magk9naq4q, 142, 5);
        gm_b0_1 t9 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
    }
    {
        gm_b16_8 t10 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
        gm_b16_8 t11 = t10;
        gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
        void * t13 = (void *)((uint8_t *)&F.l1);
        void * t14 = (void *)((uint8_t *)&F.l2);
        gm_f_parse_source_i1af11u((void *)&(*(gm_tm3mvv7hooqkm *)gm_out), (void *)NULL, cx, NULL, t12, t13, t14);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_rename_47b7og5(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1, gm_b16_8 a2) {
    struct gm_fr_rename_47b7og5 F;
    gm_b16_8 gm_l0; /* text */
    gm_b16_8 gm_l1; /* defs */
    gm_b16_8 gm_l2; /* uses */
    gm_b16_8 gm_l4; /* result */
    GM_STACK_CHECK(&F, 369u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        gm_f_new_jr5ejs0((void *)&F.l3, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
    }
    {
        void * t3 = (void *)((uint8_t *)&F.l3);
        gm_t3mf58gv9kf3n t4 = GM_CONV(gm_t3mf58gv9kf3n, gm_f_allocator_5m783ja(t3));
        gm_t3mf58gv9kf3n t5 = t4;
        gm_node wn6 = {.eff = 587034433, .kind = GM_DECL, .clauses = gm_hd3t1u951rul, .env = (void *)&t5, .hctx = cx, .with_frame = &F.h, .with_label = 0, .result = (void *)&F.w0, .fw = NULL, .next = cx};
        gm_b16_8 t10 = gm_l0;
        gm_b16_8 t11 = gm_l1;
        gm_b16_8 t12 = gm_l2;
        gm_b16_8 t13 = gm_f_rename_in_nstmasl((void *)NULL, (&wn6), NULL, t10, t11, t12);
        if (gm_unw.kind) {
            goto gm_ll8;
        }
        gm_b16_8 t14 = t13;
        F.w0 = t14;
        goto gm_ls15;
gm_ll8:;
        if (gm_unw.kind == GM_BREAK && gm_unw.frame == &F.h && gm_unw.label == 0) { gm_unw.kind = GM_NONE; goto we7; }
        goto gm_ld1_1;
gm_ls15:;
we7:;
        gm_b16_8 t16 = F.w0;
        gm_l4 = t16;
    }
    {
        gm_b16_8 t17 = gm_l4;
        gm_tabb710d9mq2e t18;
        gm_f_dupe_g9iqpvn((void *)&t18, (void *)NULL, cx, NULL, t17, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kd6k1g0uehn)));
        if (gm_unw.kind) {
            goto gm_ld1_1;
        }
        gm_tabb710d9mq2e t19 = t18;
        if (((((uint8_t *)&t19))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t19) + 8, gm_srcr4magk9naq4q, 303, 12);
        gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&t19) + 8));
        gm_b16_8 t21 = t20;
        gm_b16_8 t22 = GM_LD(gm_b16_8, ((uint8_t *)&t21));
        uint8_t t23 = 0;
        F.ret = t22;
        gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = t23;
        goto gm_ld1_1;
    }
    {
        void * t24 = (void *)((uint8_t *)&F.l3);
        gm_f_deinit_sejdpqv(t24);
    }
    goto gm_ls25;
gm_ld1_1:;
    {
        gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
        void * t26 = (void *)((uint8_t *)&F.l3);
        gm_f_deinit_sejdpqv(t26);
        gm_unw = gm_saved;
    }
    goto gm_lad;
gm_ls25:;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_b16_8 gm_f_rename_in_nstmasl(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1, gm_b16_8 a2) {
    struct gm_fr_rename_in_nstmasl F;
    gm_b16_8 gm_l0; /* text */
    gm_b16_8 gm_l1; /* defs */
    gm_b16_8 gm_l2; /* uses */
    gm_b16_8 gm_l3; /* tokens */
    gm_b16_8 gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    gm_t2s5j3n0guq0t gm_l7; /* d */
    gm_b16_8 gm_l8; /* <tmp> */
    uint64_t gm_l9; /* <tmp> */
    uint64_t gm_l10; /* i */
    gm_b16_8 gm_l12; /* name */
    gm_b16_8 gm_l13; /* new */
    uint64_t gm_l14; /* lo */
    uint64_t gm_l15; /* hi */
    uint8_t gm_l16; /* taken */
    gm_b16_8 gm_l17; /* <tmp> */
    uint64_t gm_l18; /* <tmp> */
    gm_tbccvikg6b96e gm_l19; /* e */
    gm_b16_8 gm_l21; /* <tmp> */
    uint64_t gm_l22; /* <tmp> */
    gm_tbccvikg6b96e gm_l23; /* e */
    gm_b16_8 gm_l24; /* tail */
    GM_STACK_CHECK(&F, 1949u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        gm_b16_8 t2 = gm_l0;
        gm_t0to1u196c9pc t3;
        gm_f_scan_e9v4omo((void *)&t3, (void *)NULL, cx, NULL, t2);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t0to1u196c9pc t4 = t3;
        gm_b16_8 t5 = GM_LD(gm_b16_8, (((uint8_t *)&t4) + 0));
        gm_l3 = t5;
    }
    {
        gm_f_list_jekodor_2((void *)&F.l4, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t7 = gm_l1;
            gm_l5 = t7;
        }
        {
            gm_l6 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t9 = gm_l6;
                uint64_t t10 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                uint8_t t11 = (t9 < t10);
                if (!t11) goto lb8;
                {
                    uint64_t t14 = gm_l6;
                    uint64_t t15 = (uintptr_t)t14;
                    uint64_t t16 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                    gm_t2s5j3n0guq0t t19 = GM_LD(gm_t2s5j3n0guq0t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l5)) + t15 * 24));
                    gm_l7 = t19;
                    {
                        void * t21 = (void *)((uint8_t *)&F.l4);
                        gm_tbccvikg6b96e t22;
                        uint32_t t23 = GM_LD(uint32_t, (((uint8_t *)&gm_l7) + 0));
                        uint64_t t24 = (uint64_t)t23;
                        GM_ST(uint64_t, (uint8_t *)&t22 + 0, t24);
                        uint32_t t25 = GM_LD(uint32_t, (((uint8_t *)&gm_l7) + 4));
                        uint64_t t26 = (uint64_t)t25;
                        GM_ST(uint64_t, (uint8_t *)&t22 + 8, t26);
                        gm_b16_8 t27 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l7) + 8));
                        GM_ST(gm_b16_8, (uint8_t *)&t22 + 16, t27);
                        gm_tftb1odc739t5 t28 = gm_f_push_k96mobn_2(t21, &t22);
                        gm_tftb1odc739t5 t29 = t28;
                        if (((((uint8_t *)&t29))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t29) + 1, gm_srcr4magk9naq4q, 309, 21);
                        gm_b0_1 t30 = GM_LD(gm_b0_1, (((uint8_t *)&t29) + 1));
                    }
                    goto me17;
mn18:;
                    gm_trap("no match arm matched the scrutinee", gm_srcr4magk9naq4q, 309, 5);
me17:;
                }
lc8:;
                {
                    uint64_t t32 = gm_l6;
                    uint64_t r33; bool o34 = __builtin_add_overflow(t32, ((uint64_t)1ULL), &r33);
                    uint64_t t35 = r33;
                    gm_l6 = t35;
                }
            }
lb8:;
        }
    }
    {
        {
            gm_b16_8 t37 = gm_l3;
            gm_b16_8 t38 = t37;
            gm_b16_8 t39 = GM_LD(gm_b16_8, ((uint8_t *)&t38));
            gm_l8 = t39;
        }
        {
            gm_l9 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t41 = gm_l9;
                uint64_t t42 = GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8);
                uint8_t t43 = (t41 < t42);
                if (!t43) goto lb40;
                {
                    gm_tfhcigdju4v01 t46;
                    uint64_t t47 = gm_l9;
                    GM_ST(uint64_t, (uint8_t *)&t46 + 0, t47);
                    uint64_t t48 = gm_l9;
                    uint64_t t49 = (uintptr_t)t48;
                    uint64_t t50 = GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8);
                    memcpy((uint8_t *)&t46 + 8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l8)) + t49 * 48), 48);
                    gm_tfhcigdju4v01 t51 = t46;
                    uint64_t t54 = GM_LD(uint64_t, (((uint8_t *)&t51) + 0));
                    gm_l10 = t54;
                    gm_tm4pc0emhrv1g t55 = GM_LD(gm_tm4pc0emhrv1g, (((uint8_t *)&t51) + 8));
                    F.l11 = t55;
                    {
                        uint8_t t58 = GM_LD(uint8_t, ((((uint8_t *)&F.l11) + 0) + 0));
                        if (t58 != ((uint8_t)0ULL)) goto ie57;
                        gm_b16_8 t59 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l11) + 0) + 8));
                        gm_l12 = t59;
                        {
                            gm_b16_8 t61 = gm_l2;
                            gm_b16_8 t62 = gm_l12;
                            gm_th2lqbo77pvsc t63;
                            gm_f_lookup_var_3vpm3u6((void *)&t63, t61, t62);
                            gm_th2lqbo77pvsc t64 = t63;
                            gm_b16_8 t65;
                            if (((((uint8_t *)&t64))[16] != 0)) {
                                gm_b16_8 t66 = GM_LD(gm_b16_8, (((uint8_t *)&t64) + 0));
                                t65 = t66;
                            } else {
                                goto lc40;
                            }
                            gm_l13 = t65;
                        }
                        {
                            uint32_t t67 = GM_LD(uint32_t, ((((uint8_t *)&F.l11) + 32) + 4));
                            uint64_t t68 = (uint64_t)t67;
                            gm_l14 = t68;
                        }
                        {
                            uint32_t t69 = GM_LD(uint32_t, ((((uint8_t *)&F.l11) + 32) + 8));
                            uint64_t t70 = (uint64_t)t69;
                            gm_l15 = t70;
                        }
                        {
                            gm_l16 = ((uint8_t)0);
                        }
                        {
                            {
                                gm_b16_8 t72 = GM_LD(gm_b16_8, (((uint8_t *)&F.l4) + 0));
                                gm_l17 = t72;
                            }
                            {
                                gm_l18 = ((uint64_t)0ULL);
                            }
                            {
                                for (;;) {
                                    uint64_t t74 = gm_l18;
                                    uint64_t t75 = GM_LD(uintptr_t, ((uint8_t *)&gm_l17) + 8);
                                    uint8_t t76 = (t74 < t75);
                                    if (!t76) goto lb73;
                                    {
                                        uint64_t t79 = gm_l18;
                                        uint64_t t80 = (uintptr_t)t79;
                                        uint64_t t81 = GM_LD(uintptr_t, ((uint8_t *)&gm_l17) + 8);
                                        gm_tbccvikg6b96e t84 = GM_LD(gm_tbccvikg6b96e, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l17)) + t80 * 32));
                                        gm_l19 = t84;
                                        {
                                            uint64_t t87 = GM_LD(uint64_t, (((uint8_t *)&gm_l19) + 0));
                                            uint64_t t88 = gm_l14;
                                            uint8_t t89 = ((t87 == t88));
                                            if (!t89) goto ie86;
                                            {
                                                gm_l16 = ((uint8_t)1);
                                            }
                                            goto ix86;
ie86:;
ix86:;
                                        }
                                        goto me82;
mn83:;
                                        gm_trap("no match arm matched the scrutinee", gm_srcr4magk9naq4q, 316, 9);
me82:;
                                    }
lc73:;
                                    {
                                        uint64_t t92 = gm_l18;
                                        uint64_t r93; bool o94 = __builtin_add_overflow(t92, ((uint64_t)1ULL), &r93);
                                        uint64_t t95 = r93;
                                        gm_l18 = t95;
                                    }
                                }
lb73:;
                            }
                        }
                        {
                            uint8_t t97 = gm_l16;
                            if (!t97) goto ie96;
                            {
                                goto lc40;
                            }
                            goto ix96;
ie96:;
ix96:;
                        }
                        {
                            uint64_t t100 = gm_l10;
                            uint8_t t101 = (t100 > ((uint64_t)0ULL));
                            if (!t101) goto ie99;
                            uint64_t t102 = gm_l10;
                            uint64_t r103; bool o104 = __builtin_sub_overflow(t102, ((uint64_t)1ULL), &r103);
                            if (o104) gm_trap("integer overflow in `-`", gm_srcr4magk9naq4q, 320, 29);
                            uint64_t t105 = r103;
                            uint64_t t106 = (uintptr_t)t105;
                            uint64_t t107 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                            if (t106 >= (uintptr_t)(t107)) gm_trap_index(t106, t107, gm_srcr4magk9naq4q, 320, 22);
                            gm_t3fcbuehh3bs2 t108 = GM_LD(gm_t3fcbuehh3bs2, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t106 * 48) + 0));
                            gm_t3fcbuehh3bs2 t109 = t108;
                            gm_t9qkh0rsooc8d t110;
                            GM_ST(uint8_t, ((uint8_t *)&t110 + 0), ((uint8_t)58ULL));
                            gm_t9qkh0rsooc8d t111 = t110;
                            uint8_t t112 = (((GM_LD(uint8_t, ((uint8_t *)&t109) + 0) == ((uint8_t)8ULL)) && gm_eqv3tj1pu43g((((uint8_t *)&t109) + 8), ((uint8_t *)&t111))));
                            if (!t112) goto ie99;
                            {
                                goto lc40;
                            }
                            goto ix99;
ie99:;
ix99:;
                        }
                        {
                            uint64_t t115 = gm_l10;
                            uint64_t r116; bool o117 = __builtin_add_overflow(t115, ((uint64_t)1ULL), &r116);
                            if (o117) gm_trap("integer overflow in `+`", gm_srcr4magk9naq4q, 321, 12);
                            uint64_t t118 = r116;
                            uint64_t t119 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                            uint8_t t120 = (t118 < t119);
                            if (!t120) goto ie114;
                            uint64_t t121 = gm_l10;
                            uint64_t r122; bool o123 = __builtin_add_overflow(t121, ((uint64_t)1ULL), &r122);
                            if (o123) gm_trap("integer overflow in `+`", gm_srcr4magk9naq4q, 321, 42);
                            uint64_t t124 = r122;
                            uint64_t t125 = (uintptr_t)t124;
                            uint64_t t126 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                            if (t125 >= (uintptr_t)(t126)) gm_trap_index(t125, t126, gm_srcr4magk9naq4q, 321, 35);
                            gm_t3fcbuehh3bs2 t127 = GM_LD(gm_t3fcbuehh3bs2, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t125 * 48) + 0));
                            gm_t3fcbuehh3bs2 t128 = t127;
                            gm_t9qkh0rsooc8d t129;
                            GM_ST(uint8_t, ((uint8_t *)&t129 + 0), ((uint8_t)56ULL));
                            gm_t9qkh0rsooc8d t130 = t129;
                            uint8_t t131 = (((GM_LD(uint8_t, ((uint8_t *)&t128) + 0) == ((uint8_t)8ULL)) && gm_eqv3tj1pu43g((((uint8_t *)&t128) + 8), ((uint8_t *)&t130))));
                            if (!t131) goto ie114;
                            {
                                goto lc40;
                            }
                            goto ix114;
ie114:;
ix114:;
                        }
                        {
                            void * t133 = (void *)((uint8_t *)&F.l4);
                            gm_tbccvikg6b96e t134;
                            uint64_t t135 = gm_l14;
                            GM_ST(uint64_t, (uint8_t *)&t134 + 0, t135);
                            uint64_t t136 = gm_l15;
                            GM_ST(uint64_t, (uint8_t *)&t134 + 8, t136);
                            gm_b16_8 t137 = gm_l13;
                            GM_ST(gm_b16_8, (uint8_t *)&t134 + 16, t137);
                            gm_tftb1odc739t5 t138 = gm_f_push_k96mobn_2(t133, &t134);
                            gm_tftb1odc739t5 t139 = t138;
                            if (((((uint8_t *)&t139))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t139) + 1, gm_srcr4magk9naq4q, 322, 9);
                            gm_b0_1 t140 = GM_LD(gm_b0_1, (((uint8_t *)&t139) + 1));
                        }
                        goto ix57;
ie57:;
                        {
                            goto lc40;
                        }
ix57:;
                    }
                    goto me52;
mn53:;
                    gm_trap("no match arm matched the scrutinee", gm_srcr4magk9naq4q, 310, 5);
me52:;
                }
lc40:;
                {
                    uint64_t t143 = gm_l9;
                    uint64_t r144; bool o145 = __builtin_add_overflow(t143, ((uint64_t)1ULL), &r144);
                    uint64_t t146 = r144;
                    gm_l9 = t146;
                }
            }
lb40:;
        }
    }
    {
        gm_b16_8 t147 = GM_LD(gm_b16_8, (((uint8_t *)&F.l4) + 0));
        gm_closure t148 = ((gm_closure){GM_PTR_TAG(gm_f_fn_8no2c2v), NULL, cx});
        gm_f_sort_by_jo6rcsi(t147, t148);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_f_list_uvfu1k1((void *)&F.l20, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t149 = (void *)((uint8_t *)&F.l20);
        gm_b16_8 t150 = gm_l0;
        gm_tftb1odc739t5 t151 = gm_f_extend_0cs85gs(t149, t150);
        gm_tftb1odc739t5 t152 = t151;
        if (((((uint8_t *)&t152))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t152) + 1, gm_srcr4magk9naq4q, 327, 5);
        gm_b0_1 t153 = GM_LD(gm_b0_1, (((uint8_t *)&t152) + 1));
    }
    {
        {
            gm_b16_8 t155 = GM_LD(gm_b16_8, (((uint8_t *)&F.l4) + 0));
            gm_l21 = t155;
        }
        {
            gm_l22 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t157 = gm_l22;
                uint64_t t158 = GM_LD(uintptr_t, ((uint8_t *)&gm_l21) + 8);
                uint8_t t159 = (t157 < t158);
                if (!t159) goto lb156;
                {
                    uint64_t t162 = gm_l22;
                    uint64_t t163 = (uintptr_t)t162;
                    uint64_t t164 = GM_LD(uintptr_t, ((uint8_t *)&gm_l21) + 8);
                    gm_tbccvikg6b96e t167 = GM_LD(gm_tbccvikg6b96e, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l21)) + t163 * 32));
                    gm_l23 = t167;
                    {
                        uint64_t t169 = GM_LD(uintptr_t, (((uint8_t *)&F.l20) + 0) + 8);
                        void * t170 = ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l20) + 0)));
                        uint64_t t171 = GM_LD(uint64_t, (((uint8_t *)&gm_l23) + 8));
                        uint64_t t172 = (uintptr_t)t171;
                        if (t172 > (uintptr_t)t169 || (uintptr_t)t169 > (uintptr_t)t169) gm_trap_slice(t172, (uintptr_t)t169, t169, gm_srcr4magk9naq4q, 329, 22);
                        gm_b16_8 t173;
                        GM_ST(void *, (uint8_t *)&t173, (void *)(((uint8_t *)t170 + t172 * 1)));
                        GM_ST(uintptr_t, (uint8_t *)&t173 + 8, (uintptr_t)((uintptr_t)t169 - t172));
                        gm_b16_8 t174 = t173;
                        gm_b16_8 t175 = GM_LD(gm_b16_8, ((uint8_t *)&t174));
                        gm_tabb710d9mq2e t176;
                        gm_f_dupe_g9iqpvn((void *)&t176, (void *)NULL, cx, NULL, t175, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_k3mmr0p0mh8)));
                        if (gm_unw.kind) {
                            goto gm_ll160;
                        }
                        gm_tabb710d9mq2e t177 = t176;
                        if (((((uint8_t *)&t177))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t177) + 8, gm_srcr4magk9naq4q, 329, 17);
                        gm_b16_8 t178 = GM_LD(gm_b16_8, (((uint8_t *)&t177) + 8));
                        gm_l24 = t178;
                    }
                    {
                        void * t179 = (void *)((uint8_t *)&F.l20);
                        uint64_t t180 = GM_LD(uint64_t, (((uint8_t *)&gm_l23) + 0));
                        gm_f_truncate_ov4pgoq(t179, t180);
                    }
                    {
                        void * t181 = (void *)((uint8_t *)&F.l20);
                        gm_b16_8 t182 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l23) + 16));
                        gm_tftb1odc739t5 t183 = gm_f_extend_0cs85gs(t181, t182);
                        gm_tftb1odc739t5 t184 = t183;
                        if (((((uint8_t *)&t184))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t184) + 1, gm_srcr4magk9naq4q, 331, 9);
                        gm_b0_1 t185 = GM_LD(gm_b0_1, (((uint8_t *)&t184) + 1));
                    }
                    {
                        void * t186 = (void *)((uint8_t *)&F.l20);
                        gm_b16_8 t187 = gm_l24;
                        gm_b16_8 t188 = t187;
                        gm_b16_8 t189 = GM_LD(gm_b16_8, ((uint8_t *)&t188));
                        gm_tftb1odc739t5 t190 = gm_f_extend_0cs85gs(t186, t189);
                        gm_tftb1odc739t5 t191 = t190;
                        if (((((uint8_t *)&t191))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t191) + 1, gm_srcr4magk9naq4q, 332, 9);
                        gm_b0_1 t192 = GM_LD(gm_b0_1, (((uint8_t *)&t191) + 1));
                    }
                    goto me165;
mn166:;
                    gm_trap("no match arm matched the scrutinee", gm_srcr4magk9naq4q, 328, 5);
me165:;
                }
lc156:;
                {
                    uint64_t t194 = gm_l22;
                    uint64_t r195; bool o196 = __builtin_add_overflow(t194, ((uint64_t)1ULL), &r195);
                    uint64_t t197 = r195;
                    gm_l22 = t197;
                }
            }
gm_ll160:;
            if (gm_unw.frame == &F.h && gm_unw.label == 3) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb156; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc156; }
            }
            goto gm_lad;
lb156:;
        }
    }
    {
        gm_b16_8 t198 = GM_LD(gm_b16_8, (((uint8_t *)&F.l20) + 0));
        gm_b16_8 t199 = t198;
        gm_b16_8 t200 = GM_LD(gm_b16_8, ((uint8_t *)&t199));
        uint8_t t201 = 0;
        F.ret = t200;
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
inline __attribute__((aligned(4))) void gm_f_scan_e9v4omo(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_scan_e9v4omo F;
    gm_b16_8 gm_l0; /* text */
    uint32_t gm_l2; /* id */
    gm_t0to1u196c9pc gm_l5; /* s */
    gm_b16_8 gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    GM_STACK_CHECK(&F, 488u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_source_map_4v8dg4b((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sb3u9q4alu9bg));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(6));
        gm_b16_8 t4 = gm_l0;
        uint32_t t5 = gm_f_add_pva1tcq((void *)NULL, cx, NULL, t2, t3, t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l2 = t5;
    }
    {
        gm_f_diagnostics_n3kkhf4((void *)&F.l3, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        uint32_t t6 = gm_l2;
        gm_b16_8 t7 = gm_l0;
        void * t8 = (void *)((uint8_t *)&F.l3);
        gm_f_lex_dc8mbmf((void *)&F.l4, (void *)NULL, cx, NULL, t6, t7, t8);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        memset((uint8_t *)&gm_l5 + 19, 0, 5);
        gm_b16_8 t9 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l4) + 0) + 0));
        GM_ST(gm_b16_8, (uint8_t *)&gm_l5 + 0, t9);
        GM_ST(uint8_t, (uint8_t *)&gm_l5 + 16, ((uint8_t)0));
        GM_ST(uint8_t, (uint8_t *)&gm_l5 + 17, ((uint8_t)0));
        void * t10 = (void *)((uint8_t *)&F.l3);
        uint8_t t11 = gm_f_has_errors_if7ai0m(t10);
        GM_ST(uint8_t, (uint8_t *)&gm_l5 + 18, t11);
    }
    {
        {
            gm_b16_8 t13 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l3) + 0) + 0));
            gm_l6 = t13;
        }
        {
            gm_l7 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t15 = gm_l7;
                uint64_t t16 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                uint8_t t17 = (t15 < t16);
                if (!t17) goto lb14;
                {
                    uint64_t t20 = gm_l7;
                    uint64_t t21 = (uintptr_t)t20;
                    uint64_t t22 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                    gm_t4gut2c9fj1kt t25 = GM_LD(gm_t4gut2c9fj1kt, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t21 * 184));
                    F.l8 = t25;
                    {
                        gm_b16_8 t28 = GM_LD(gm_b16_8, (((uint8_t *)&F.l8) + 8));
                        gm_b16_8 t29;
                        GM_ST(void *, (uint8_t *)&t29, (void *)(gm_s5t1b3eks758h));
                        GM_ST(uintptr_t, (uint8_t *)&t29 + 8, (uintptr_t)(26));
                        uint8_t t30 = gm_f_contains_gkki4kq(t28, t29);
                        if (!t30) goto ie27;
                        {
                            GM_ST(uint8_t, (((uint8_t *)&gm_l5) + 16), ((uint8_t)1));
                        }
                        goto ix27;
ie27:;
ix27:;
                    }
                    {
                        gm_b16_8 t33 = GM_LD(gm_b16_8, (((uint8_t *)&F.l8) + 8));
                        gm_b16_8 t34;
                        GM_ST(void *, (uint8_t *)&t34, (void *)(gm_sshj97ls6n4f3));
                        GM_ST(uintptr_t, (uint8_t *)&t34 + 8, (uintptr_t)(17));
                        uint8_t t35 = gm_f_contains_gkki4kq(t33, t34);
                        if (!t35) goto ie32;
                        {
                            GM_ST(uint8_t, (((uint8_t *)&gm_l5) + 17), ((uint8_t)1));
                        }
                        goto ix32;
ie32:;
ix32:;
                    }
                    goto me23;
mn24:;
                    gm_trap("no match arm matched the scrutinee", gm_srcr4magk9naq4q, 63, 5);
me23:;
                }
lc14:;
                {
                    uint64_t t38 = gm_l7;
                    uint64_t r39; bool o40 = __builtin_add_overflow(t38, ((uint64_t)1ULL), &r39);
                    uint64_t t41 = r39;
                    gm_l7 = t41;
                }
            }
lb14:;
        }
    }
    {
        memmove(gm_out, ((uint8_t *)&gm_l5), 24);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_syntax_errors_fk23f5t(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_t9qkh0rsooc8d a1) {
    struct gm_fr_syntax_errors_fk23f5t F;
    gm_b16_8 gm_l0; /* text */
    gm_t9qkh0rsooc8d gm_l1; /* format */
    gm_b16_8 gm_l3; /* result */
    GM_STACK_CHECK(&F, 307u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        gm_f_new_jr5ejs0((void *)&F.l2, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
    }
    {
        void * t3 = (void *)((uint8_t *)&F.l2);
        gm_t3mf58gv9kf3n t4 = GM_CONV(gm_t3mf58gv9kf3n, gm_f_allocator_5m783ja(t3));
        gm_t3mf58gv9kf3n t5 = t4;
        gm_node wn6 = {.eff = 587034433, .kind = GM_DECL, .clauses = gm_hd3t1u951rul, .env = (void *)&t5, .hctx = cx, .with_frame = &F.h, .with_label = 0, .result = (void *)&F.w0, .fw = NULL, .next = cx};
        gm_b16_8 t10 = gm_l0;
        gm_t9qkh0rsooc8d t11 = gm_l1;
        gm_b16_8 t12 = gm_f_syntax_errors_in_o8fnm4b((void *)NULL, (&wn6), NULL, t10, t11);
        if (gm_unw.kind) {
            goto gm_ll8;
        }
        gm_b16_8 t13 = t12;
        F.w0 = t13;
        goto gm_ls14;
gm_ll8:;
        if (gm_unw.kind == GM_BREAK && gm_unw.frame == &F.h && gm_unw.label == 0) { gm_unw.kind = GM_NONE; goto we7; }
        goto gm_ld1_1;
gm_ls14:;
we7:;
        gm_b16_8 t15 = F.w0;
        gm_l3 = t15;
    }
    {
        gm_b16_8 t16 = gm_l3;
        gm_tabb710d9mq2e t17;
        gm_f_dupe_g9iqpvn((void *)&t17, (void *)NULL, cx, NULL, t16, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_klim7pfh497)));
        if (gm_unw.kind) {
            goto gm_ld1_1;
        }
        gm_tabb710d9mq2e t18 = t17;
        if (((((uint8_t *)&t18))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t18) + 8, gm_srcr4magk9naq4q, 343, 12);
        gm_b16_8 t19 = GM_LD(gm_b16_8, (((uint8_t *)&t18) + 8));
        gm_b16_8 t20 = t19;
        gm_b16_8 t21 = GM_LD(gm_b16_8, ((uint8_t *)&t20));
        uint8_t t22 = 0;
        F.ret = t21;
        gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = t22;
        goto gm_ld1_1;
    }
    {
        void * t23 = (void *)((uint8_t *)&F.l2);
        gm_f_deinit_sejdpqv(t23);
    }
    goto gm_ls24;
gm_ld1_1:;
    {
        gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
        void * t25 = (void *)((uint8_t *)&F.l2);
        gm_f_deinit_sejdpqv(t25);
        gm_unw = gm_saved;
    }
    goto gm_lad;
gm_ls24:;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_b16_8 gm_f_syntax_errors_in_o8fnm4b(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_t9qkh0rsooc8d a1) {
    struct gm_fr_syntax_errors_in_o8fnm4b F;
    gm_b16_8 gm_l0; /* text */
    gm_t9qkh0rsooc8d gm_l1; /* format */
    uint8_t gm_l2; /* stray */
    gm_t9805qtonbsag gm_l3; /* items_err */
    gm_t9805qtonbsag gm_l4; /* stmts_err */
    uint32_t gm_l5; /* pre */
    uint32_t gm_l6; /* p */
    uint8_t gm_l7; /* use_stmts */
    uint32_t gm_l8; /* i */
    uint32_t gm_l9; /* s */
    GM_STACK_CHECK(&F, 929u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        gm_b16_8 t2 = gm_l0;
        gm_t0to1u196c9pc t3;
        gm_f_scan_e9v4omo((void *)&t3, (void *)NULL, cx, NULL, t2);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t0to1u196c9pc t4 = t3;
        uint8_t t5 = GM_LD(uint8_t, (((uint8_t *)&t4) + 17));
        gm_l2 = t5;
    }
    {
        gm_b16_8 t6 = gm_l0;
        gm_tm3mvv7hooqkm t7;
        gm_f_parse_items_g2pt6ce((void *)&t7, (void *)NULL, cx, NULL, t6);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tm3mvv7hooqkm t8 = t7;
        gm_t9805qtonbsag t9 = GM_LD(gm_t9805qtonbsag, (((uint8_t *)&t8) + 40));
        gm_l3 = t9;
    }
    {
        gm_b16_8 t10 = gm_l0;
        gm_tm3mvv7hooqkm t11;
        gm_f_parse_stmts_mefdnbk((void *)&t11, (void *)NULL, cx, NULL, t10);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tm3mvv7hooqkm t12 = t11;
        gm_t9805qtonbsag t13 = GM_LD(gm_t9805qtonbsag, (((uint8_t *)&t12) + 40));
        gm_l4 = t13;
    }
    {
        uint64_t t14 = GM_LD(uintptr_t, ((uint8_t *)gm_cfc3i2mbui9) + 8);
        uint32_t t15 = (uint32_t)t14;
        gm_l5 = t15;
    }
    {
        uint8_t t17 = gm_l2;
        uint8_t t18 = (uint8_t)!t17;
        if (!t18) goto ie16;
        uint8_t t19;
        if (((((uint8_t *)&gm_l3))[4] != 0)) goto mn21;
        t19 = ((uint8_t)0);
        goto me20;
mn21:;
        t19 = ((uint8_t)1);
        goto me20;
mn22:;
        gm_trap("no match arm matched the scrutinee", gm_srcr4magk9naq4q, 351, 22);
me20:;
        if (!t19) goto ie16;
        {
            if (!((((uint8_t *)&gm_l4))[4] != 0)) goto ie24;
            uint32_t t25 = GM_LD(uint32_t, ((uint8_t *)&gm_l4));
            gm_l6 = t25;
            {
                uint32_t t28 = gm_l6;
                uint32_t t29 = gm_l5;
                gm_b16_8 t30 = gm_l0;
                gm_b16_8 t31 = gm_f_trim_end_6sjv6rg(t30);
                gm_b16_8 t32 = t31;
                uint64_t t33 = GM_LD(uintptr_t, ((uint8_t *)&t32) + 8);
                uint32_t t34 = (uint32_t)t33;
                uint32_t r35; bool o36 = __builtin_add_overflow(t29, t34, &r35);
                if (o36) gm_trap("integer overflow in `+`", gm_srcr4magk9naq4q, 353, 21);
                uint32_t t37 = r35;
                uint8_t t38 = (t28 >= t37);
                if (!t38) goto ie27;
                {
                    gm_b16_8 t40;
                    GM_ST(void *, (uint8_t *)&t40, (void *)(gm_sfcm41q17pr37));
                    GM_ST(uintptr_t, (uint8_t *)&t40 + 8, (uintptr_t)(45));
                    uint8_t t41 = 0;
                    F.ret = t40;
                    goto gm_fret;
                }
                goto ix27;
ie27:;
ix27:;
            }
            goto ix24;
ie24:;
ix24:;
        }
        goto ix16;
ie16:;
ix16:;
    }
    {
        gm_l7 = ((uint8_t)0);
    }
    {
        uint8_t t43 = gm_l2;
        uint8_t t44 = (uint8_t)!t43;
        if (!t44) goto ie42;
        {
            if (!((((uint8_t *)&gm_l3))[4] != 0)) goto ie46;
            uint32_t t47 = GM_LD(uint32_t, ((uint8_t *)&gm_l3));
            gm_l8 = t47;
            {
                if (!((((uint8_t *)&gm_l4))[4] != 0)) goto ie49;
                uint32_t t50 = GM_LD(uint32_t, ((uint8_t *)&gm_l4));
                gm_l9 = t50;
                {
                    uint32_t t52 = gm_l9;
                    uint32_t t53 = gm_l5;
                    uint32_t r54; bool o55 = __builtin_sub_overflow(t52, t53, &r54);
                    uint32_t t56 = o55 ? (0 ? ((uint32_t)4294967295ULL) : ((uint32_t)0ULL)) : r54;
                    uint32_t t57 = gm_l8;
                    uint8_t t58 = (t56 > t57);
                    gm_l7 = t58;
                }
                goto ix49;
ie49:;
ix49:;
            }
            goto ix46;
ie46:;
ix46:;
        }
        goto ix42;
ie42:;
ix42:;
    }
    {
        gm_f_source_map_4v8dg4b((void *)&F.l10, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_f_diagnostics_n3kkhf4((void *)&F.l11, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        uint8_t t60 = gm_l7;
        if (!t60) goto ie59;
        {
            gm_f_list_uvfu1k1((void *)&F.l12, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t62 = (void *)((uint8_t *)&F.l12);
            gm_tfeab279c16rr t63;
            gm_b16_8 t64 = GM_LD(gm_b16_8, gm_cfc3i2mbui9);
            GM_ST(gm_b16_8, (uint8_t *)&t63 + 0, t64);
            gm_b16_8 t65 = gm_l0;
            GM_ST(gm_b16_8, (uint8_t *)&t63 + 16, t65);
            gm_b16_8 t66 = GM_LD(gm_b16_8, gm_cbms0bo4q1a);
            GM_ST(gm_b16_8, (uint8_t *)&t63 + 32, t66);
            gm_tftb1odc739t5 t67 = gm_f_write_qe3m8jq(t62, &t63);
            gm_tftb1odc739t5 t68 = t67;
            if (((((uint8_t *)&t68))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t68) + 1, gm_srcr4magk9naq4q, 366, 9);
            gm_b0_1 t69 = GM_LD(gm_b0_1, (((uint8_t *)&t68) + 1));
        }
        {
            {
                gm_b16_8 t71 = GM_LD(gm_b16_8, (((uint8_t *)&F.l12) + 0));
                gm_b16_8 t72 = t71;
                gm_b16_8 t73 = GM_LD(gm_b16_8, ((uint8_t *)&t72));
                void * t74 = (void *)((uint8_t *)&F.l10);
                void * t75 = (void *)((uint8_t *)&F.l11);
                gm_tm3mvv7hooqkm t76;
                gm_f_parse_source_i1af11u((void *)&t76, (void *)NULL, cx, NULL, t73, t74, t75);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
        }
        {
            void * t77 = (void *)((uint8_t *)&F.l10);
            void * t78 = (void *)((uint8_t *)&F.l11);
            uint32_t t79 = gm_l5;
            gm_b16_8 t80 = gm_l0;
            gm_t9qkh0rsooc8d t81 = gm_l1;
            gm_b16_8 t82 = gm_f_render_region_ppot3lm((void *)NULL, cx, NULL, t77, t78, ((uint32_t)0ULL), t79, t80, t81);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t83 = 0;
            F.ret = t82;
            goto gm_fret;
        }
        goto ix59;
ie59:;
ix59:;
    }
    {
        {
            gm_b16_8 t85 = gm_l0;
            void * t86 = (void *)((uint8_t *)&F.l10);
            void * t87 = (void *)((uint8_t *)&F.l11);
            gm_tm3mvv7hooqkm t88;
            gm_f_parse_source_i1af11u((void *)&t88, (void *)NULL, cx, NULL, t85, t86, t87);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
    }
    {
        void * t89 = (void *)((uint8_t *)&F.l10);
        void * t90 = (void *)((uint8_t *)&F.l11);
        gm_b16_8 t91 = gm_l0;
        gm_t9qkh0rsooc8d t92 = gm_l1;
        gm_b16_8 t93 = gm_f_render_region_ppot3lm((void *)NULL, cx, NULL, t89, t90, ((uint32_t)0ULL), ((uint32_t)0ULL), t91, t92);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t94 = 0;
        F.ret = t93;
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
