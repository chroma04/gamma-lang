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
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint8_t f0; uint8_t p1[3]; union { int32_t v0; } f1; } gm_toe5fmr3vvf4d;
typedef struct { uint64_t f0; gm_t9qkh0rsooc8d f1; uint8_t p2[7]; int64_t f2; uint32_t f3; uint8_t pe[4]; } gm_t5j14m8ru132p;
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
_Static_assert(sizeof(gm_toe5fmr3vvf4d) == 8 && _Alignof(gm_toe5fmr3vvf4d) == 4 && offsetof(gm_toe5fmr3vvf4d, f0) == 0 && offsetof(gm_toe5fmr3vvf4d, f1) == 4, "layout of `IoError`");
_Static_assert(sizeof(gm_t5j14m8ru132p) == 32 && _Alignof(gm_t5j14m8ru132p) == 8 && offsetof(gm_t5j14m8ru132p, f0) == 0 && offsetof(gm_t5j14m8ru132p, f1) == 8 && offsetof(gm_t5j14m8ru132p, f2) == 16 && offsetof(gm_t5j14m8ru132p, f3) == 24, "layout of `FileInfo`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_toe5fmr3vvf4d v0; gm_t5j14m8ru132p v1; } f1; } gm_tih6h4avv5nrb;
_Static_assert(sizeof(gm_tih6h4avv5nrb) == 40 && _Alignof(gm_tih6h4avv5nrb) == 8 && offsetof(gm_tih6h4avv5nrb, f0) == 0 && offsetof(gm_tih6h4avv5nrb, f1) == 8, "layout of `IoError!FileInfo`");
struct gm_fr_for_body_25s43ua {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_is_file_tcuv9rv {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_program_on_path_fm43kjf {
    gm_frame h;
    gm_b16_8 l0; /* prog */
    uint8_t ret;
    gm_b0_1 w0;
};
inline __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_25s43ua(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_for_body_3msbccb(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_for_body_69vpqmq(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_file_tcuv9rv(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_join_qveh5b2(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
extern uint8_t gm_c15bjid4jl4[10] __attribute__((aligned(1)));
extern uint8_t gm_c18viqkn44m[73] __attribute__((aligned(1)));
extern uint8_t gm_c1cveujlpof[4] __attribute__((aligned(4)));
extern uint8_t gm_c1prl48ao31[9] __attribute__((aligned(1)));
extern uint8_t gm_c1smbes90gp[45] __attribute__((aligned(1)));
extern uint8_t gm_c260filn0hg[12] __attribute__((aligned(1)));
extern uint8_t gm_c2826uudqo9[4] __attribute__((aligned(1)));
extern uint8_t gm_c311b27lqem[5] __attribute__((aligned(1)));
extern uint8_t gm_c35cj6dafmd[4] __attribute__((aligned(1)));
extern uint8_t gm_c3r46h2npg4[32] __attribute__((aligned(1)));
extern uint8_t gm_c4d8h2b82ul[13] __attribute__((aligned(1)));
extern uint8_t gm_c68kng0dmc9[4] __attribute__((aligned(1)));
extern uint8_t gm_c73blk4giq8[41] __attribute__((aligned(1)));
extern uint8_t gm_c8a5rud5775[3] __attribute__((aligned(1)));
extern uint8_t gm_c8fi7e4b2c0[3] __attribute__((aligned(1)));
extern uint8_t gm_c96ci4s389r[12] __attribute__((aligned(1)));
extern uint8_t gm_c9vs9vsb4ar[6] __attribute__((aligned(1)));
extern uint8_t gm_ca853dhk557[3] __attribute__((aligned(1)));
extern uint8_t gm_ca9oq2dug1j[5] __attribute__((aligned(1)));
extern uint8_t gm_captp4b2jvk[11] __attribute__((aligned(1)));
extern uint8_t gm_cb7r52n9k7f[8] __attribute__((aligned(8)));
extern uint8_t gm_cbbjgr0cpr9[8] __attribute__((aligned(8)));
extern uint8_t gm_cbcjb1mvjd8[8] __attribute__((aligned(8)));
extern uint8_t gm_cc2870p5ue7[4] __attribute__((aligned(1)));
extern uint8_t gm_ccat59d4o57[4] __attribute__((aligned(1)));
extern uint8_t gm_ccliptj8gld[6] __attribute__((aligned(1)));
extern uint8_t gm_cco1tols022[7] __attribute__((aligned(1)));
extern uint8_t gm_ccpknhlhf5b[14] __attribute__((aligned(1)));
extern uint8_t gm_cddg9214lam[8] __attribute__((aligned(8)));
extern uint8_t gm_cdmki9acqi9[4] __attribute__((aligned(4)));
extern uint8_t gm_cduv4b53gs5[16] __attribute__((aligned(8)));
extern uint8_t gm_ce19ifs9rfv[14] __attribute__((aligned(1)));
extern uint8_t gm_ce60a0uriev[4] __attribute__((aligned(1)));
extern uint8_t gm_ceihpp68v5g[46] __attribute__((aligned(1)));
extern uint8_t gm_cfa8lmn3344[48] __attribute__((aligned(8)));
extern uint8_t gm_cfcggcra0bi[128] __attribute__((aligned(8)));
extern uint8_t gm_cfqm8fakuqu[10] __attribute__((aligned(1)));
extern uint8_t gm_cg0jqpbrhpd[7] __attribute__((aligned(1)));
extern uint8_t gm_cgmvo0tct53[4] __attribute__((aligned(1)));
extern uint8_t gm_ch5ml6cehoi[7] __attribute__((aligned(1)));
extern uint8_t gm_chnhbcnvjio[38] __attribute__((aligned(1)));
extern uint8_t gm_chops3mqdq7[16] __attribute__((aligned(1)));
extern uint8_t gm_chu6kqk7n7m[3] __attribute__((aligned(1)));
extern uint8_t gm_ci656gdeljc[112] __attribute__((aligned(1)));
extern uint8_t gm_cii63jo6a5p[3] __attribute__((aligned(1)));
extern uint8_t gm_cjfdffcngsv[16] __attribute__((aligned(1)));
extern uint8_t gm_cjher1hugak[8] __attribute__((aligned(1)));
extern uint8_t gm_cjrpcsokd66[4] __attribute__((aligned(1)));
extern uint8_t gm_ck6sh4an5ll[3] __attribute__((aligned(1)));
extern uint8_t gm_ck843vssqab[6] __attribute__((aligned(1)));
extern uint8_t gm_cl5s6n9vfg7[55] __attribute__((aligned(1)));
extern uint8_t gm_cl7gtp51edk[4] __attribute__((aligned(4)));
extern uint8_t gm_cm1qt6s2ccu[8] __attribute__((aligned(8)));
extern uint8_t gm_cmh24klcov2[4] __attribute__((aligned(1)));
extern uint8_t gm_coobb7mgu3c[11] __attribute__((aligned(1)));
extern uint8_t gm_cos4c858gsq[1] __attribute__((aligned(1)));
extern uint8_t gm_cqe47fqk7bg[10] __attribute__((aligned(1)));
extern uint8_t gm_cql8du9th1j[10] __attribute__((aligned(1)));
extern uint8_t gm_cs5d24kc9mj[9] __attribute__((aligned(1)));
extern uint8_t gm_csem906k37c[67] __attribute__((aligned(1)));
extern uint8_t gm_csl5i9dkkt0[14] __attribute__((aligned(1)));
extern uint8_t gm_cu7fu2rbnlr[10] __attribute__((aligned(1)));
extern uint8_t gm_cus4oh838pa[8] __attribute__((aligned(8)));
extern uint8_t gm_cvjvdnfnhe8[11] __attribute__((aligned(1)));
extern uint8_t gm_cvorh1r4oo3[11] __attribute__((aligned(1)));
extern void *const gm_hl1l1s81m49k[];
extern void *const gm_hl5do7vn7rgp[];
extern void *const gm_hlv5qrjggnmh[];
extern uint8_t gm_k2t1c54q9lq[40] __attribute__((aligned(8)));
extern uint8_t gm_k5c0o81h2qv[40] __attribute__((aligned(8)));
extern uint8_t gm_k8upcoeidcs[40] __attribute__((aligned(8)));
extern uint8_t gm_k8v0lok1ldk[40] __attribute__((aligned(8)));
extern uint8_t gm_k9m9dc9i2tf[40] __attribute__((aligned(8)));
extern uint8_t gm_kbjtngq55qv[40] __attribute__((aligned(8)));
extern uint8_t gm_kc84hfpu27t[40] __attribute__((aligned(8)));
extern uint8_t gm_kdhojj6r45o[40] __attribute__((aligned(8)));
extern uint8_t gm_kfh7mc23f4a[40] __attribute__((aligned(8)));
extern uint8_t gm_kg90e3uuvge[40] __attribute__((aligned(8)));
extern uint8_t gm_kib6nfi69s7[40] __attribute__((aligned(8)));
extern uint8_t gm_kja7oc5dvo4[40] __attribute__((aligned(8)));
extern uint8_t gm_kk1no0qhlfa[40] __attribute__((aligned(8)));
extern uint8_t gm_km2d9tibmvp[40] __attribute__((aligned(8)));
extern uint8_t gm_kmgp10tqfjk[40] __attribute__((aligned(8)));
extern uint8_t gm_kn7qjfrkcl7[40] __attribute__((aligned(8)));
extern uint8_t gm_kq6mn01fhkq[40] __attribute__((aligned(8)));
extern uint8_t gm_ksb741egs0v[40] __attribute__((aligned(8)));
extern uint8_t gm_ksq8uu39c9e[40] __attribute__((aligned(8)));
extern uint8_t gm_kvubgn35fiq[40] __attribute__((aligned(8)));
extern const uint8_t gm_s02lv5e9c8ffs[41];
extern const uint8_t gm_s030jgonh2s3s[105];
extern const uint8_t gm_s046fb0aoed79[41];
extern const uint8_t gm_s05crl6fa1at4[6];
extern const uint8_t gm_s067qe7d1tphl[31];
extern const uint8_t gm_s08fuan8vv08d[10];
extern const uint8_t gm_s0hlhgvgnhpal[60];
extern const uint8_t gm_s0k6ioi39f878[4];
extern const uint8_t gm_s0m9d22aen6f8[2];
extern const uint8_t gm_s0p6vt35dv9nv[6];
extern const uint8_t gm_s0pk9hes3k3ft[22];
extern const uint8_t gm_s0rphlcbno91v[8];
extern const uint8_t gm_s121p29851qjl[39];
extern const uint8_t gm_s12ng38ve3205[8];
extern const uint8_t gm_s137iv03ua7e7[30];
extern const uint8_t gm_s14vcrbl91fi8[32];
extern const uint8_t gm_s156lri6d35ip[77];
extern const uint8_t gm_s1ktljc2oac64[4];
extern const uint8_t gm_s1mh1dhprjqfo[19];
extern const uint8_t gm_s1qml6papvbci[11];
extern const uint8_t gm_s1tfd03l0gv6e[14];
extern const uint8_t gm_s2289h15tkpi1[12];
extern const uint8_t gm_s230fdcusa4us[33];
extern const uint8_t gm_s23lfljk9icjn[9];
extern const uint8_t gm_s2520ab14fc1c[16];
extern const uint8_t gm_s26md5sqfb6u7[7];
extern const uint8_t gm_s27n8348bef64[63];
extern const uint8_t gm_s2df2fvelno2h[7];
extern const uint8_t gm_s2dtfgg301ms9[24];
extern const uint8_t gm_s2fdsodctojm1[10];
extern const uint8_t gm_s2g9gpcumahrc[24];
extern const uint8_t gm_s2go8ju7rh9bh[38];
extern const uint8_t gm_s2hnapin8228t[21];
extern const uint8_t gm_s2j7kqlnfrbrh[9];
extern const uint8_t gm_s2jmrvqu8r8aq[58];
extern const uint8_t gm_s2kuocg9pm4qc[63];
extern const uint8_t gm_s2nl2e5anglov[4];
extern const uint8_t gm_s2pa39hqq5hrk[12];
extern const uint8_t gm_s2pmug7pm7rdg[31];
extern const uint8_t gm_s2reoulhu3o4k[49];
extern const uint8_t gm_s2u5posfkbgnq[2];
extern const uint8_t gm_s2umangg4hn1g[15];
extern const uint8_t gm_s323uftq8kr36[49];
extern const uint8_t gm_s332n2u4obj9h[49];
extern const uint8_t gm_s37sjoj9k6mkj[52];
extern const uint8_t gm_s39mljh669qgl[2];
extern const uint8_t gm_s3a1oio07ri1p[3];
extern const uint8_t gm_s3ajneg0jdfi2[15];
extern const uint8_t gm_s3d91jdjen8gc[8];
extern const uint8_t gm_s3kdrrcff51rt[69];
extern const uint8_t gm_s3l6h9h61sdbp[29];
extern const uint8_t gm_s3n43rret3i7v[21];
extern const uint8_t gm_s3nt8pf0qk5gn[22];
extern const uint8_t gm_s3pq04qmqnihs[12];
extern const uint8_t gm_s3tfok7urjm2v[8];
extern const uint8_t gm_s3vqae7u5ljtm[14];
extern const uint8_t gm_s407iureo3mia[17];
extern const uint8_t gm_s4087kcl9bpus[29];
extern const uint8_t gm_s43vlgrackeg9[17];
extern const uint8_t gm_s445fcdtcvspc[25];
extern const uint8_t gm_s4a980fahtblo[3];
extern const uint8_t gm_s4b0873bt7c1q[5];
extern const uint8_t gm_s4cjqdlbfp2g0[17];
extern const uint8_t gm_s4dis7c905evg[10];
extern const uint8_t gm_s4e5mut9ghpn7[273];
extern const uint8_t gm_s4ica9th0h3pb[54];
extern const uint8_t gm_s4imhc4v1tf2p[10];
extern const uint8_t gm_s4jh2uktkqkro[22];
extern const uint8_t gm_s4lo69slu4igt[8];
extern const uint8_t gm_s4smf8g4h1pk2[14];
extern const uint8_t gm_s55ts2fqn36f4[11];
extern const uint8_t gm_s5709o8srsr55[41];
extern const uint8_t gm_s588gob2um10i[2];
extern const uint8_t gm_s5agga61vhqnt[20];
extern const uint8_t gm_s5angofg6gc01[74];
extern const uint8_t gm_s5avakeqanvk7[10];
extern const uint8_t gm_s5bj9cslts4df[34];
extern const uint8_t gm_s5btp6s8l6mkb[41];
extern const uint8_t gm_s5d58k7uql7sf[72];
extern const uint8_t gm_s5f956mm1l828[6];
extern const uint8_t gm_s5i2sl6mv6b47[11];
extern const uint8_t gm_s5iv4icrhn1vp[19];
extern const uint8_t gm_s5o6ultk7ms9u[16];
extern const uint8_t gm_s5p756i54h2sb[52];
extern const uint8_t gm_s5q2lnsoba445[41];
extern const uint8_t gm_s63011l92pvfr[40];
extern const uint8_t gm_s63emi69ethib[50];
extern const uint8_t gm_s68lv7o2uide3[61];
extern const uint8_t gm_s6ag7edtaqqri[5];
extern const uint8_t gm_s6fdn1h3c830p[5];
extern const uint8_t gm_s6gt8b7hsisgu[3];
extern const uint8_t gm_s6iroad3kr6qd[7];
extern const uint8_t gm_s6j315qbv99q6[12];
extern const uint8_t gm_s6kp8voc0etjv[60];
extern const uint8_t gm_s6mpqtffqhn51[80];
extern const uint8_t gm_s6p52ubfcgt5t[20];
extern const uint8_t gm_s6q7rsv30u3ma[34];
extern const uint8_t gm_s6r94a2k1goos[9];
extern const uint8_t gm_s6s4ngfaj87vv[5];
extern const uint8_t gm_s6t0sieqs3tpi[48];
extern const uint8_t gm_s70g70juoj4nh[42];
extern const uint8_t gm_s72hk5u5ngq4i[11];
extern const uint8_t gm_s74ohph6l9htf[21];
extern const uint8_t gm_s76d71nockpip[17];
extern const uint8_t gm_s79ai2f6v6lof[41];
extern const uint8_t gm_s7i070auf0cel[6];
extern const uint8_t gm_s7j8d035g0p9i[38];
extern const uint8_t gm_s7jj9572hd242[6];
extern const uint8_t gm_s7s32h358ordg[6];
extern const uint8_t gm_s7teaveu6apab[4];
extern const uint8_t gm_s84smn81idetm[11];
extern const uint8_t gm_s85q0hfg1arf2[38];
extern const uint8_t gm_s89pk8bfrfh3n[27];
extern const uint8_t gm_s8a7jvbqoerpn[21];
extern const uint8_t gm_s8ahsd1lo2um5[38];
extern const uint8_t gm_s8bi9iapfhnht[11];
extern const uint8_t gm_s8difjetdnmv8[41];
extern const uint8_t gm_s8fq66mhsgcib[104];
extern const uint8_t gm_s8ilcu32tpscb[21];
extern const uint8_t gm_s8km061fqa0rq[63];
extern const uint8_t gm_s8lbhmq10rt7i[42];
extern const uint8_t gm_s8nekj984lthm[15];
extern const uint8_t gm_s8s6shfnaqahd[7];
extern const uint8_t gm_s8vts15kskc6n[11];
extern const uint8_t gm_s93umj3p0tala[5];
extern const uint8_t gm_s967vts9f19o3[11];
extern const uint8_t gm_s98lgd2com794[12];
extern const uint8_t gm_s9blr7hh7gofq[54];
extern const uint8_t gm_s9c2oj94op2h6[10];
extern const uint8_t gm_s9citadvj27tv[39];
extern const uint8_t gm_s9h93c7asacna[7];
extern const uint8_t gm_s9ln4rnb6talr[4];
extern const uint8_t gm_s9o66vhtid5af[4];
extern const uint8_t gm_s9ofbo8c28rrv[16];
extern const uint8_t gm_s9p1knha37i5h[27];
extern const uint8_t gm_s9pf9com5692e[39];
extern const uint8_t gm_s9tdiht68h68o[4];
extern const uint8_t gm_s9ur7sj9r0g63[7];
extern const uint8_t gm_s9vtro0rvol4u[17];
extern const uint8_t gm_sa0rsiiph6kui[12];
extern const uint8_t gm_sa8q5t8i6ht30[6];
extern const uint8_t gm_sa902c1tbtg0i[4];
extern const uint8_t gm_sa92qppdjht4t[100];
extern const uint8_t gm_sa9kcg34ga2f3[15];
extern const uint8_t gm_sa9ktdbn49iui[16];
extern const uint8_t gm_safnb563bt6ue[12];
extern const uint8_t gm_sand7bpicpp6t[26];
extern const uint8_t gm_sasi3vvkcdvo0[40];
extern const uint8_t gm_sb2k9e71jsraf[7];
extern const uint8_t gm_sb398tg9q30m5[6];
extern const uint8_t gm_sb3u9q4alu9bg[7];
extern const uint8_t gm_sb8airrqr74u5[73];
extern const uint8_t gm_sbc7a60c47o3n[19];
extern const uint8_t gm_sbckhsd8r0nr0[20];
extern const uint8_t gm_sbe2tiimd9c3v[8];
extern const uint8_t gm_sbjgombb463qi[9];
extern const uint8_t gm_sbjpc99k8hon9[22];
extern const uint8_t gm_sbkvlhvo2a3ic[7];
extern const uint8_t gm_sblt6ctj6g4ck[21];
extern const uint8_t gm_sbnuvhnotkfkr[5];
extern const uint8_t gm_sbo9b5v1bvurr[29];
extern const uint8_t gm_sbr6d06ao5ra7[16];
extern const uint8_t gm_sbrnv8n0dpinb[8];
extern const uint8_t gm_sbu9dvh1hebne[26];
extern const uint8_t gm_sc4t0u3ku1fbu[23];
extern const uint8_t gm_sc4vvo9i5cgel[86];
extern const uint8_t gm_scdb74to7b5sl[39];
extern const uint8_t gm_sci6hkonrjrne[19];
extern const uint8_t gm_sciu02cqahje3[52];
extern const uint8_t gm_sckj2gedlq7u6[55];
extern const uint8_t gm_scqp3lehp7mc6[17];
extern const uint8_t gm_scs8vqh2gmmhd[51];
extern const uint8_t gm_scuc7vp51n9rl[41];
extern const uint8_t gm_sd00cc6js4td5[7];
extern const uint8_t gm_sd2n08j3tm5k4[17];
extern const uint8_t gm_sdainihnab59h[10];
extern const uint8_t gm_sdbqk29a3n8jm[13];
extern const uint8_t gm_sdhauhfs9i578[12];
extern const uint8_t gm_sdkba1tit6s52[17];
extern const uint8_t gm_sdmdremodiqov[25];
extern const uint8_t gm_sdnsovsr4en9v[49];
extern const uint8_t gm_sdo5tfkgp02tp[26];
extern const uint8_t gm_sdoflo4g2h6ev[14];
extern const uint8_t gm_sdpo2hjlq8e9b[31];
extern const uint8_t gm_sdr1j0cf4fck6[20];
extern const uint8_t gm_sdrkb67lqt61h[88];
extern const uint8_t gm_sds8ed13tvvnt[48];
extern const uint8_t gm_sdtjbut83s1a1[21];
extern const uint8_t gm_sdtpg69jtnl30[30];
extern const uint8_t gm_se0a26478itki[7];
extern const uint8_t gm_se0f2jka437t4[2];
extern const uint8_t gm_se2e37nl2hi6g[4];
extern const uint8_t gm_se47039ecu2b8[10];
extern const uint8_t gm_se4r520bkfair[18];
extern const uint8_t gm_seegi1cuj806d[168];
extern const uint8_t gm_segv6dkv6t4tr[7];
extern const uint8_t gm_seiql9tkbkhim[52];
extern const uint8_t gm_semi8nh4n9n0n[24];
extern const uint8_t gm_senose3qe4tvp[3];
extern const uint8_t gm_seqndsmrihepc[10];
extern const uint8_t gm_sf1gce4kbrsde[8];
extern const uint8_t gm_sf1p9u1vovjso[5];
extern const uint8_t gm_sf6ug774thos9[15];
extern const uint8_t gm_sfa8f3bk569gj[26];
extern const uint8_t gm_sfad5qbegckoi[28];
extern const uint8_t gm_sfbb5kv3rbn0v[29];
extern const uint8_t gm_sfcc9d16o906b[24];
extern const uint8_t gm_sfd6isdlu0d13[21];
extern const uint8_t gm_sfmnli2if0k7v[15];
extern const uint8_t gm_sfphojfmj7uk6[5];
extern const uint8_t gm_sfrjcsmisriv1[3];
extern const uint8_t gm_sg0bl57kiha8j[27];
extern const uint8_t gm_sg3kpa3h16no3[7];
extern const uint8_t gm_sg9vhq4ehlfsf[18];
extern const uint8_t gm_sgfbh9novfe35[7];
extern const uint8_t gm_sgfva5dbm38o8[10];
extern const uint8_t gm_sgpgu8iag22ja[15];
extern const uint8_t gm_sgpo1se8uggtq[18];
extern const uint8_t gm_sgrap32255cto[47];
extern const uint8_t gm_sgrqv16lctl3g[41];
extern const uint8_t gm_sgu334n5g1kog[16];
extern const uint8_t gm_sh0274v2fqhs4[18];
extern const uint8_t gm_sh0rvdctsredm[64];
extern const uint8_t gm_sh3h2k0f6ktgl[13];
extern const uint8_t gm_sh3r0ji6563do[127];
extern const uint8_t gm_sh44iolkubjmq[41];
extern const uint8_t gm_sh6h8rdicu443[23];
extern const uint8_t gm_sh9ju7u678u0l[28];
extern const uint8_t gm_shanrrdljtv7g[4];
extern const uint8_t gm_shauoqbc8hgsg[4];
extern const uint8_t gm_shc8a6qgqrqaf[42];
extern const uint8_t gm_shgi3g6gdv99o[4];
extern const uint8_t gm_shj1t0cb4970k[27];
extern const uint8_t gm_shn9a2s1f1vl0[17];
extern const uint8_t gm_shnpduonp99d0[12];
extern const uint8_t gm_shquo2s3jci8a[42];
extern const uint8_t gm_shsisdh16t023[28];
extern const uint8_t gm_shvpku7ssg8d7[11];
extern const uint8_t gm_si5knpkeaqca7[137];
extern const uint8_t gm_si68mvbt2sjam[19];
extern const uint8_t gm_si7hg8a0jmhu5[3];
extern const uint8_t gm_si7qrooamq5ei[26];
extern const uint8_t gm_si9fi6l9mcekc[41];
extern const uint8_t gm_sidtn67sdk4e0[7];
extern const uint8_t gm_sifqpftcv49j6[22];
extern const uint8_t gm_sig89vu0mvqtm[19];
extern const uint8_t gm_siipob9mkm9hq[24];
extern const uint8_t gm_simh6hd7oitki[22];
extern const uint8_t gm_siopg3b55pic9[12];
extern const uint8_t gm_sioup6jsbm84g[10];
extern const uint8_t gm_siupt3h9siv5v[10];
extern const uint8_t gm_sius9pki5u4e3[5];
extern const uint8_t gm_sivr8g8c8o0kq[13];
extern const uint8_t gm_sj0kptrfbec99[3];
extern const uint8_t gm_sj2if4k4r1jer[8];
extern const uint8_t gm_sj53lcu5ne531[20];
extern const uint8_t gm_sj6oj7rmit9sk[93];
extern const uint8_t gm_sj70rum4dd30i[18];
extern const uint8_t gm_sj7f8n5rgjl7r[11];
extern const uint8_t gm_sj90kfpommb4k[44];
extern const uint8_t gm_sjc0gph5pdnnm[9];
extern const uint8_t gm_sjfs8et3lk0l5[7];
extern const uint8_t gm_sjg14535b6ovp[41];
extern const uint8_t gm_sjhqrnq2lvjcm[35];
extern const uint8_t gm_sjl83c3218sse[19];
extern const uint8_t gm_sjsojd1o1489e[34];
extern const uint8_t gm_sjviirpelj2et[26];
extern const uint8_t gm_sk0bf2ce2h62k[10];
extern const uint8_t gm_sk1gl0oknml68[18];
extern const uint8_t gm_sk5d5fbcb3pir[10];
extern const uint8_t gm_sk698gp5chukg[10];
extern const uint8_t gm_sk754qkda90si[6];
extern const uint8_t gm_sk7ckkvc4fg11[28];
extern const uint8_t gm_skcb9kf2svs50[17];
extern const uint8_t gm_skcto6psrjh96[28];
extern const uint8_t gm_skesaudrffe2q[55];
extern const uint8_t gm_ski66jdm8e243[9];
extern const uint8_t gm_skkvljkq130ke[15];
extern const uint8_t gm_skmq6kit541c7[6];
extern const uint8_t gm_skof48dte1s1o[13];
extern const uint8_t gm_skpepkrdit47b[11];
extern const uint8_t gm_skpghu6j8rgck[12];
extern const uint8_t gm_skr4l871ttakp[13];
extern const uint8_t gm_sksh6i7obnq40[8];
extern const uint8_t gm_sl37uvr16qsu8[11];
extern const uint8_t gm_sl6eotv49v54f[38];
extern const uint8_t gm_sl7cl2p91o7b5[55];
extern const uint8_t gm_sl88m8js9368j[39];
extern const uint8_t gm_slad86501rp5c[19];
extern const uint8_t gm_sldikqa9i70sm[52];
extern const uint8_t gm_sldjvqvpa1t8p[16];
extern const uint8_t gm_sldmbttac8k2j[59];
extern const uint8_t gm_slfcjb9tfk917[27];
extern const uint8_t gm_slfn5huoi5r5n[12];
extern const uint8_t gm_slhm5kfb42dip[11];
extern const uint8_t gm_sljor9gn53lam[13];
extern const uint8_t gm_slknnflnp0v0v[37];
extern const uint8_t gm_slkubuckp49ih[12];
extern const uint8_t gm_slnqcu8l15gll[12];
extern const uint8_t gm_slogp373o1rii[24];
extern const uint8_t gm_slpfvblm7qee4[13];
extern const uint8_t gm_slqgeomhftr16[15];
extern const uint8_t gm_sltau19rt9o73[8];
extern const uint8_t gm_slv9mko23s9sn[33];
extern const uint8_t gm_sm17am2sq1qe4[10];
extern const uint8_t gm_sm1jl2enra6o1[41];
extern const uint8_t gm_sm26tj0iuodd4[41];
extern const uint8_t gm_sm337p11h7spc[8];
extern const uint8_t gm_sm3o0uerjumpm[4];
extern const uint8_t gm_sm3sqa0e3amks[9];
extern const uint8_t gm_sm9funvq1222p[44];
extern const uint8_t gm_smblbmr0dp3js[8];
extern const uint8_t gm_smc2kdmn5qjql[113];
extern const uint8_t gm_smd81jcp0bga9[19];
extern const uint8_t gm_smh12ad93nju6[45];
extern const uint8_t gm_sml1jlm4abpm3[24];
extern const uint8_t gm_smpmtloh1fun3[36];
extern const uint8_t gm_smr39cplkqb3n[51];
extern const uint8_t gm_smrba58613c3b[17];
extern const uint8_t gm_smsc5gshqt6cd[20];
extern const uint8_t gm_smv2cp06tp4la[13];
extern const uint8_t gm_sn1h26ucppu4i[110];
extern const uint8_t gm_sn2furlv6e7jl[4];
extern const uint8_t gm_sn5f6h6alcm9q[12];
extern const uint8_t gm_sn5ntorhqodmg[39];
extern const uint8_t gm_sn5sjb95plsev[27];
extern const uint8_t gm_snas2pfnujj1i[15];
extern const uint8_t gm_sne7l93pr2tmb[34];
extern const uint8_t gm_snf9ak4s55usv[18];
extern const uint8_t gm_sngfc91643s6a[55];
extern const uint8_t gm_snh0sl2cdagdn[13];
extern const uint8_t gm_snh12e0jas620[13];
extern const uint8_t gm_snnov1h58rig5[52];
extern const uint8_t gm_snoj0t5lf2lht[4];
extern const uint8_t gm_snqbh8eqcn135[17];
extern const uint8_t gm_snqvo5bgc2fch[8];
extern const uint8_t gm_snti96ut68ga4[120];
extern const uint8_t gm_snuinkttlbd28[14];
extern const uint8_t gm_snvldulppvqal[23];
extern const uint8_t gm_so1jvf7g8en80[26];
extern const uint8_t gm_so3oq96gr0k5r[10];
extern const uint8_t gm_so6qi13ng7s70[41];
extern const uint8_t gm_so8fkrgrobcsk[9];
extern const uint8_t gm_sobumja6poqii[13];
extern const uint8_t gm_soeb2q5rdgp9r[14];
extern const uint8_t gm_soft3ordeghp1[17];
extern const uint8_t gm_solagrm41ggme[33];
extern const uint8_t gm_som446v3b5hhj[176];
extern const uint8_t gm_somhu4vltiil1[4];
extern const uint8_t gm_soo4rtckloi4d[13];
extern const uint8_t gm_sovc43d45he5a[21];
extern const uint8_t gm_sp0doh9njf7i3[27];
extern const uint8_t gm_sp0p0g46490o0[24];
extern const uint8_t gm_sp3jmpmb6sldf[5];
extern const uint8_t gm_sp6oikjubf9mg[7];
extern const uint8_t gm_sp8vnj37dd7ds[7];
extern const uint8_t gm_spg6s3e4lci26[10];
extern const uint8_t gm_spgt2nbblhu8j[9];
extern const uint8_t gm_sphr6fu31dtrl[3];
extern const uint8_t gm_spic2fa42u00e[19];
extern const uint8_t gm_spiov306ab2eh[10];
extern const uint8_t gm_spkgb882eim6u[10];
extern const uint8_t gm_spm6tgoorr19q[29];
extern const uint8_t gm_spmp7ok3h3mlq[11];
extern const uint8_t gm_spn2ndrnbok71[34];
extern const uint8_t gm_spncc9hduhmp8[33];
extern const uint8_t gm_spqqkr6igks2d[28];
extern const uint8_t gm_sps94d2dd3g7k[44];
extern const uint8_t gm_spshvjknob1uq[81];
extern const uint8_t gm_sptjupp93c9n4[56];
extern const uint8_t gm_sq2cbiau3hhqs[11];
extern const uint8_t gm_sq77ose3aa457[24];
extern const uint8_t gm_sq94cop5evc92[18];
extern const uint8_t gm_sq9ir3ha51jdh[24];
extern const uint8_t gm_sqaek8776r3c5[9];
extern const uint8_t gm_sqefppq1so38u[21];
extern const uint8_t gm_sqf9pgeqvvbb4[77];
extern const uint8_t gm_sqffn766fd6b5[14];
extern const uint8_t gm_sqhkrul9t68sa[8];
extern const uint8_t gm_sqhqlj2oc6h5k[20];
extern const uint8_t gm_sqljkj8r253om[7];
extern const uint8_t gm_sqofj6305nq58[8];
extern const uint8_t gm_sqqajgod2bb4o[4];
extern const uint8_t gm_sqtvhnbm32dvp[23];
extern const uint8_t gm_sqv6e5e2cne7b[59];
extern const uint8_t gm_sr0s4jkm98l01[41];
extern const uint8_t gm_sr66ijstfdd2o[41];
extern const uint8_t gm_sr9uf1l734d81[12];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_src2eo2k3vc53po[];
extern const char gm_src2odv5tht4lj8[];
extern const char gm_srcavbheg5nq9nl[];
extern const char gm_srcc2qutubarto1[];
extern const char gm_srccr53ejqr1dde[];
extern const uint8_t gm_srcf8l485c5uq[10];
extern const char gm_srcfa3vclj60ege[];
extern const char gm_srchhdunc42909u[];
extern const char gm_srcj10g653a00lt[];
extern const char gm_srcm2bronu107ea[];
extern const char gm_srcmeaag4q4qrjl[];
extern const char gm_srcn4uur2c1j8lu[];
extern const char gm_srcod98slk3vnra[];
extern const char gm_srcp1pl693rgtdc[];
extern const char gm_srcpsceljs9unl1[];
extern const char gm_srcseljft758gtd[];
extern const char gm_srcsffver18si54[];
extern const char gm_srcuktvltncsesc[];
extern const char gm_srcv1nri2247d2r[];
extern const uint8_t gm_srji1enk20v8f[21];
extern const uint8_t gm_srntlv7ksi1vk[16];
extern const uint8_t gm_srp5cji88najc[10];
extern const uint8_t gm_srramd997nju1[39];
extern const uint8_t gm_srthtvs2627tn[29];
extern const uint8_t gm_sruq3cf9ro3q2[66];
extern const uint8_t gm_srvgp776ipjtg[30];
extern const uint8_t gm_ss308e7joejdo[13];
extern const uint8_t gm_ss4tls6jr9q3m[10];
extern const uint8_t gm_ss88mk7b1rh87[18];
extern const uint8_t gm_ssao50s66pj11[8];
extern const uint8_t gm_sscsdlec4a8vt[18];
extern const uint8_t gm_ssnn9u3skptri[75];
extern const uint8_t gm_ssqmgnf1snsdj[8];
extern const uint8_t gm_ssr3n7oenqiui[13];
extern const uint8_t gm_ssrq4cnmtqbuh[83];
extern const uint8_t gm_ssullrletdua5[41];
extern const uint8_t gm_st1mshpgajfu8[41];
extern const uint8_t gm_st1pc9bsku00q[9];
extern const uint8_t gm_st2pui5pcm1j4[13];
extern const uint8_t gm_st4bliqvgaqre[127];
extern const uint8_t gm_stgav1sk1eq6u[59];
extern const uint8_t gm_stjei6jfiba56[5];
extern const uint8_t gm_str1v7jglok76[3];
extern const uint8_t gm_su3j467gt9vk3[14];
extern const uint8_t gm_su3sef888krmb[22];
extern const uint8_t gm_sua4hqf3te7mn[15];
extern const uint8_t gm_suh0878eve8t9[10];
extern const uint8_t gm_suki713bapaoe[7];
extern const uint8_t gm_sukitf61448ms[14];
extern const uint8_t gm_sup67cuqst8bs[8];
extern const uint8_t gm_sus2pjl36h2nd[10];
extern const uint8_t gm_suskhmu5llblc[67];
extern const uint8_t gm_sv0u97lrebfd4[41];
extern const uint8_t gm_sv44h36stbh8s[7];
extern const uint8_t gm_sv9p44pp2n1jk[10];
extern const uint8_t gm_svb51eiulbs8i[41];
extern const uint8_t gm_sveg1mb15s1b2[18];
extern const uint8_t gm_svkleo8hhv87n[4];
extern const uint8_t gm_svo4dku3ftf1l[177];
extern const uint8_t gm_svu9pa07vreqt[14];
extern const gm_type gm_tyb2nno7n67r;
extern const gm_variant gm_tyo86ks9qkvt_v[];
inline __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_25s43ua(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_for_body_25s43ua F;
    gm_b16_8 gm_l0; /* <item> */
    gm_b16_8 gm_l1; /* dir */
    GM_STACK_CHECK(&F, 98u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t3 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l0));
    gm_l1 = t3;
    {
        gm_b16_8 t6 = gm_l1;
        gm_b16_8 t7 = GM_LD(gm_b16_8, ((uint8_t *)&((struct gm_fr_program_on_path_fm43kjf *)((&F.h)->up))->l0));
        gm_b16_8 t8 = gm_f_join_qveh5b2((void *)NULL, cx, NULL, t6, t7);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t9 = gm_f_is_file_tcuv9rv((void *)NULL, cx, NULL, t8);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (!t9) goto ie5;
        {
            uint8_t t11 = 0;
            ((struct gm_fr_program_on_path_fm43kjf *)((&F.h)->up))->ret = ((uint8_t)1);
            gm_unw.kind = GM_RETURN; gm_unw.frame = ((&F.h)->up); gm_unw.label = 0; gm_unw.is_err = t11;
            goto gm_lad;
        }
        goto ix5;
ie5:;
ix5:;
    }
    {
        gm_b0_1 t12;
    }
    goto me1;
mn2:;
    gm_trap("no match arm matched the scrutinee", gm_srchhdunc42909u, 114, 5);
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_file_tcuv9rv(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_is_file_tcuv9rv F;
    gm_b16_8 gm_l0; /* p */
    gm_t5j14m8ru132p gm_l1; /* info */
    GM_STACK_CHECK(&F, 213u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_b16_8 t2 = gm_l0;
        gm_node *pn3 = gm_find(cx, 2126215853);
        if (!pn3) gm_trap("no handler installed for effect `Fs`", gm_srcc2qutubarto1, 234, 13);
        gm_tih6h4avv5nrb t4;
        ((void (*)(void *, void *, gm_node *, gm_node *, gm_b16_8))pn3->clauses[5])((void *)&t4, pn3->env, pn3->hctx, pn3, t2);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tih6h4avv5nrb t5 = t4;
        gm_t5j14m8ru132p t6;
        if (!((((uint8_t *)&t5))[0] != 0)) {
            gm_t5j14m8ru132p t7 = GM_LD(gm_t5j14m8ru132p, (((uint8_t *)&t5) + 8));
            t6 = t7;
        } else {
            uint8_t t8 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        gm_l1 = t6;
    }
    {
        uint8_t t9 = ((GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 8) + 0) == ((uint8_t)0ULL)));
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
const char gm_src2ah81i3aa8hm[] = "<core>/prelude.gm";
const char gm_src2eo2k3vc53po[] = "compiler/sema/consts.gm";
const char gm_src2odv5tht4lj8[] = "compiler/lsp/lsp.gm";
const char gm_srcavbheg5nq9nl[] = "compiler/ide/build.gm";
const char gm_srccr53ejqr1dde[] = "compiler/pkg/cli.gm";
const char gm_srcfa3vclj60ege[] = "compiler/check/cont_check.gm";
const char gm_srchhdunc42909u[] = "compiler/load/exe.gm";
const char gm_srcj10g653a00lt[] = "compiler/interp/ops.gm";
const char gm_srcm2bronu107ea[] = "compiler/doc/html.gm";
const char gm_srcmeaag4q4qrjl[] = "compiler/cgen/calls.gm";
const char gm_srcn4uur2c1j8lu[] = "compiler/sema/layout.gm";
const char gm_srcod98slk3vnra[] = "compiler/tir/tir.gm";
const char gm_srcp1pl693rgtdc[] = "compiler/check/eval_prepare.gm";
const char gm_srcpsceljs9unl1[] = "compiler/pkg/resolve.gm";
const char gm_srcseljft758gtd[] = "compiler/check/finalize.gm";
const char gm_srcsffver18si54[] = "compiler/sema/ty_expr.gm";
const char gm_srcuktvltncsesc[] = "compiler/lint/lint.gm";
const char gm_srcv1nri2247d2r[] = "compiler/source/source.gm";
const gm_type gm_tyb2nno7n67r = {.size = 8, .align = 8, .kind = GM_K_INT, .bits = 64, .is_signed = false};
const gm_type gm_tyb2nno7n67r;
const gm_variant gm_tyo86ks9qkvt_v[] = {{"OutOfMemory", 0, NULL}};
const uint8_t gm_s02lv5e9c8ffs[41] = "\000\000\000\000\000\000\000\000\025\000\000\000\000\000\000\000Q\002\000\000 \000\000\000\000\000\000\000\000\000\000\000\005\000\000\000\000\000\000\000";
const uint8_t gm_s030jgonh2s3s[105] = "gamma cannot evaluate this `when` condition yet: it handles `target`, `@size_of`, literals and constants";
const uint8_t gm_s046fb0aoed79[41] = "\000\000\000\000\000\000\000\000\030\000\000\000\000\000\000\000\230\003\000\000\014\000\000\000\000\000\000\000\000\000\000\000\011\000\000\000\000\000\000\000";
const uint8_t gm_s05crl6fa1at4[6] = "{}+{}";
const uint8_t gm_s067qe7d1tphl[31] = "#define GM_RT_SEPARATE 1\012{}\012{}";
const uint8_t gm_s08fuan8vv08d[10] = "` has one";
const uint8_t gm_s0hlhgvgnhpal[60] = "`@offset_of(S, field)` needs a struct type and a field name";
const uint8_t gm_s0k6ioi39f878[4] = "git";
const uint8_t gm_s0m9d22aen6f8[2] = "u";
const uint8_t gm_s0p6vt35dv9nv[6] = "#else";
const uint8_t gm_s0pk9hes3k3ft[22] = "this pattern is for `";
const uint8_t gm_s0rphlcbno91v[8] = "Complex";
const uint8_t gm_s121p29851qjl[39] = "operator `{}` is not defined on floats";
const uint8_t gm_s12ng38ve3205[8] = "gm_fmin";
const uint8_t gm_s137iv03ua7e7[30] = "{} __attribute__((packed)) {}";
const uint8_t gm_s14vcrbl91fi8[32] = "this expression at compile time";
const uint8_t gm_s156lri6d35ip[77] = "`@write` takes a sink, a template string and the values for its placeholders";
const uint8_t gm_s1ktljc2oac64[4] = "tar";
const uint8_t gm_s1mh1dhprjqfo[19] = "    gm_cover_init(";
const uint8_t gm_s1qml6papvbci[11] = "GM_F16({})";
const uint8_t gm_s1tfd03l0gv6e[14] = " filtered out";
const uint8_t gm_s2289h15tkpi1[12] = "ProcTryWait";
const uint8_t gm_s230fdcusa4us[33] = "invalid package version `{}`: {}";
const uint8_t gm_s23lfljk9icjn[9] = "branch `";
const uint8_t gm_s2520ab14fc1c[16] = "gm_scratch_live";
const uint8_t gm_s26md5sqfb6u7[7] = "op_add";
const uint8_t gm_s27n8348bef64[63] = "internal error: a thread operation reached the system services";
const uint8_t gm_s2df2fvelno2h[7] = ".gamma";
const uint8_t gm_s2dtfgg301ms9[24] = "; `Signed` is now `Neg`";
const uint8_t gm_s2fdsodctojm1[10] = "AtomicRmw";
const uint8_t gm_s2g9gpcumahrc[24] = "` by its element type `";
const uint8_t gm_s2go8ju7rh9bh[38] = "invalid {} name `{}`: it is a keyword";
const uint8_t gm_s2hnapin8228t[21] = "pow on a non-integer";
const uint8_t gm_s2j7kqlnfrbrh[9] = "WASM_OPT";
const uint8_t gm_s2jmrvqu8r8aq[58] = "`{}` is not available on vectors: a lane cannot be `null`";
const uint8_t gm_s2kuocg9pm4qc[63] = "dependency `{}`: {} has no gamma.toml in its package directory";
const uint8_t gm_s2nl2e5anglov[4] = "Clz";
const uint8_t gm_s2pa39hqq5hrk[12] = "struct body";
const uint8_t gm_s2pmug7pm7rdg[31] = "`{}` = {} does not fit in `{}`";
const uint8_t gm_s2reoulhu3o4k[49] = "stack overflow: a local of {} bytes does not fit";
const uint8_t gm_s2u5posfkbgnq[2] = "|";
const uint8_t gm_s2umangg4hn1g[15] = "the variable `";
const uint8_t gm_s323uftq8kr36[49] = "`{}` has no field `{}` (supplied by this spread)";
const uint8_t gm_s332n2u4obj9h[49] = "`, which dies when the function returns (\302\24715.1)";
const uint8_t gm_s37sjoj9k6mkj[52] = "{}: warning: module `{}` has no `//!` documentation";
const uint8_t gm_s39mljh669qgl[2] = ":";
const uint8_t gm_s3a1oio07ri1p[3] = "-h";
const uint8_t gm_s3ajneg0jdfi2[15] = "` in one and `";
const uint8_t gm_s3d91jdjen8gc[8] = " failed";
const uint8_t gm_s3kdrrcff51rt[69] = "`@variants` needs a concrete union type, not the type parameter `{}`";
const uint8_t gm_s3l6h9h61sdbp[29] = "calling a value of type `{}`";
const uint8_t gm_s3n43rret3i7v[21] = "minor version number";
const uint8_t gm_s3nt8pf0qk5gn[22] = "compiler/load/toml.gm";
const uint8_t gm_s3pq04qmqnihs[12] = "broken pipe";
const uint8_t gm_s3tfok7urjm2v[8] = "coerce ";
const uint8_t gm_s3vqae7u5ljtm[14] = "if (fn != 0) ";
const uint8_t gm_s407iureo3mia[17] = "` is not covered";
const uint8_t gm_s4087kcl9bpus[29] = "write `}}` for a literal `}`";
const uint8_t gm_s43vlgrackeg9[17] = ".event_fiber == ";
const uint8_t gm_s445fcdtcvspc[25] = "Connection reset by peer";
const uint8_t gm_s4a980fahtblo[3] = ");";
const uint8_t gm_s4b0873bt7c1q[5] = "'\\\\'";
const uint8_t gm_s4cjqdlbfp2g0[17] = "gm_proc_read_out";
const uint8_t gm_s4dis7c905evg[10] = "when-stmt";
const uint8_t gm_s4e5mut9ghpn7[273] = "\000\000\000\000\000\000\000\000\002\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\003\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\003\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\003\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\005\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\002\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\003\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\003\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\003\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\005\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\003\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\003\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\004\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\006\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\004\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\004\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\004\000\000\000\000\000\000\000";
const uint8_t gm_s4ica9th0h3pb[54] = "`, which depends on it; the cycle cannot be evaluated";
const uint8_t gm_s4imhc4v1tf2p[10] = "{integer}";
const uint8_t gm_s4jh2uktkqkro[22] = " --asyncify` failed:\012";
const uint8_t gm_s4lo69slu4igt[8] = "literal";
const uint8_t gm_s4smf8g4h1pk2[14] = "gm_futex_wait";
const uint8_t gm_s55ts2fqn36f4[11] = "gm_reparse";
const uint8_t gm_s5709o8srsr55[41] = "\000\000\000\000\000\000\000\000\020\000\000\000\000\000\000\0004\000\000\000\015\000\000\000\000\000\000\000\000\000\000\000\010\000\000\000\000\000\000\000";
const uint8_t gm_s588gob2um10i[2] = "h";
const uint8_t gm_s5agga61vhqnt[20] = "unknown effect `{}`";
const uint8_t gm_s5angofg6gc01[74] = "a pointer into a list's items, used after a call that may have moved them";
const uint8_t gm_s5avakeqanvk7[10] = "parse_for";
const uint8_t gm_s5bj9cslts4df[34] = "cannot infer the type parameter `";
const uint8_t gm_s5btp6s8l6mkb[41] = "\000\000\000\000\000\000\000\000\030\000\000\000\000\000\000\000^\000\000\000\015\000\000\000\000\000\000\000\000\000\000\000\004\000\000\000\000\000\000\000";
const uint8_t gm_s5d58k7uql7sf[72] = "the members of a tuple compared by `in` must have one type, but found `";
const uint8_t gm_s5f956mm1l828[6] = " ... ";
const uint8_t gm_s5i2sl6mv6b47[11] = "gm_fmt_str";
const uint8_t gm_s5iv4icrhn1vp[19] = "downcast_unchecked";
const uint8_t gm_s5o6ultk7ms9u[16] = "prepareProvider";
const uint8_t gm_s5p756i54h2sb[52] = "this arm can never be chosen: earlier arms cover it";
const uint8_t gm_s5q2lnsoba445[41] = "\000\000\000\000\000\000\000\000\031\000\000\000\000\000\000\000\026\000\000\000\013\000\000\000\000\000\000\000\000\000\000\000\011\000\000\000\000\000\000\000";
const uint8_t gm_s63011l92pvfr[40] = ".cx, .with_frame = &F.h, .with_label = ";
const uint8_t gm_s63emi69ethib[50] = "gamma: `--coverage` is an option of `gamma test`\012";
const uint8_t gm_s68lv7o2uide3[61] = "`else e =>` captures the error of a result, but this is `{}`";
const uint8_t gm_s6ag7edtaqqri[5] = " mut";
const uint8_t gm_s6fdn1h3c830p[5] = "norm";
const uint8_t gm_s6gt8b7hsisgu[3] = "\\n";
const uint8_t gm_s6iroad3kr6qd[7] = "Little";
const uint8_t gm_s6j315qbv99q6[12] = "gm_pow_{}{}";
const uint8_t gm_s6kp8voc0etjv[60] = "static bool {}(gm_node *cx, const void *d, uintptr_t n, {})";
const uint8_t gm_s6mpqtffqhn51[80] = "for a condition known at compile time, `when` leaves the other branch unchecked";
const uint8_t gm_s6p52ubfcgt5t[20] = "gm_net_set_timeouts";
const uint8_t gm_s6q7rsv30u3ma[34] = "` has several variants carrying `";
const uint8_t gm_s6r94a2k1goos[9] = "uint32_t";
const uint8_t gm_s6s4ngfaj87vv[5] = ") : ";
const uint8_t gm_s6t0sieqs3tpi[48] = "this continuation was already consumed (\302\24714.2)";
const uint8_t gm_s70g70juoj4nh[42] = "attribute `#{}` does not apply to a field";
const uint8_t gm_s72hk5u5ngq4i[11] = "return;\012}\012";
const uint8_t gm_s74ohph6l9htf[21] = "unknown attribute `#";
const uint8_t gm_s76d71nockpip[17] = "kind = GM_K_BITS";
const uint8_t gm_s79ai2f6v6lof[41] = "\000\000\000\000\000\000\000\000\025\000\000\000\000\000\000\000\266\001\000\000 \000\000\000\000\000\000\000\000\000\000\000\004\000\000\000\000\000\000\000";
const uint8_t gm_s7i070auf0cel[6] = "https";
const uint8_t gm_s7j8d035g0p9i[38] = "`@{}` takes an integer type, not `{}`";
const uint8_t gm_s7jj9572hd242[6] = " := &";
const uint8_t gm_s7s32h358ordg[6] = "paren";
const uint8_t gm_s7teaveu6apab[4] = ")`)";
const uint8_t gm_s84smn81idetm[11] = ")[0] != 0)";
const uint8_t gm_s85q0hfg1arf2[38] = "gamma: `--all` needs `-o <directory>`";
const uint8_t gm_s89pk8bfrfh3n[27] = "@@gamma-repl-replay-done@@";
const uint8_t gm_s8a7jvbqoerpn[21] = "static void gm_ginit";
const uint8_t gm_s8ahsd1lo2um5[38] = "`.to` converts to a float type, not `";
const uint8_t gm_s8bi9iapfhnht[11] = "Operations";
const uint8_t gm_s8difjetdnmv8[41] = "\000\000\000\000\000\000\000\000\030\000\000\000\000\000\000\000\340\000\000\000'\000\000\000\000\000\000\000\000\000\000\000\012\000\000\000\000\000\000\000";
const uint8_t gm_s8fq66mhsgcib[104] = "`{}` is a `#c_union`: it has no tag, so it cannot be matched; read one of its fields instead (\302\2475.11.1)";
const uint8_t gm_s8ilcu32tpscb[21] = "textDocument/didSave";
const uint8_t gm_s8km061fqa0rq[63] = "` takes the remaining positional arguments; it cannot be named";
const uint8_t gm_s8lbhmq10rt7i[42] = "unexpected leading zero, expected nothing";
const uint8_t gm_s8nekj984lthm[15] = "a special file";
const uint8_t gm_s8s6shfnaqahd[7] = "LaneLt";
const uint8_t gm_s8vts15kskc6n[11] = "expected: ";
const uint8_t gm_s93umj3p0tala[5] = "Read";
const uint8_t gm_s967vts9f19o3[11] = "(pa + i * ";
const uint8_t gm_s98lgd2com794[12] = "a benchmark";
const uint8_t gm_s9blr7hh7gofq[54] = "(GM_LD(void *, &a), GM_LD(uintptr_t, (uint8_t *)&a + ";
const uint8_t gm_s9c2oj94op2h6[10] = "revision ";
const uint8_t gm_s9citadvj27tv[39] = "--pass-arg=asyncify-imports@gamma.none";
const uint8_t gm_s9h93c7asacna[7] = "Select";
const uint8_t gm_s9ln4rnb6talr[4] = ": _";
const uint8_t gm_s9o66vhtid5af[4] = "exe";
const uint8_t gm_s9ofbo8c28rrv[16] = "this comparison";
const uint8_t gm_s9p1knha37i5h[27] = "(gm_node *cx) { (void)cx; ";
const uint8_t gm_s9pf9com5692e[39] = "missing field `{}` (it has no default)";
const uint8_t gm_s9tdiht68h68o[4] = " & ";
const uint8_t gm_s9ur7sj9r0g63[7] = "assert";
const uint8_t gm_s9vtro0rvol4u[17] = "flag_offset = {}";
const uint8_t gm_sa0rsiiph6kui[12] = "gm_buf_free";
const uint8_t gm_sa8q5t8i6ht30[6] = "{}{};";
const uint8_t gm_sa902c1tbtg0i[4] = "Sys";
const uint8_t gm_sa92qppdjht4t[100] = "`.to` converts to a float type, not `{}`; use `.trunc`, `.round`, `.checked` or `.sat` for integers";
const uint8_t gm_sa9kcg34ga2f3[15] = "constructing `";
const uint8_t gm_sa9ktdbn49iui[16] = "kind = GM_K_PTR";
const uint8_t gm_safnb563bt6ue[12] = "hexadecimal";
const uint8_t gm_sand7bpicpp6t[26] = "`; it has no dependencies";
const uint8_t gm_sasi3vvkcdvo0[40] = "    gm_flush();\012    return (uint8_t)r;\012";
const uint8_t gm_sb2k9e71jsraf[7] = "{}{}{}";
const uint8_t gm_sb398tg9q30m5[6] = ") << ";
const uint8_t gm_sb3u9q4alu9bg[7] = "<repl>";
const uint8_t gm_sb8airrqr74u5[73] = "`for` iterates over a range, an array or slice, or a call to an iterator";
const uint8_t gm_sbc7a60c47o3n[19] = "` is declared by `";
const uint8_t gm_sbckhsd8r0nr0[20] = "invalid tuple index";
const uint8_t gm_sbe2tiimd9c3v[8] = "file://";
const uint8_t gm_sbjgombb463qi[9] = "\377\377\377\377\377\377\377\177";
const uint8_t gm_sbjpc99k8hon9[22] = "--allow --warn --deny";
const uint8_t gm_sbkvlhvo2a3ic[7] = "gammac";
const uint8_t gm_sblt6ctj6g4ck[21] = "this `else` is empty";
const uint8_t gm_sbnuvhnotkfkr[5] = " r; ";
const uint8_t gm_sbo9b5v1bvurr[29] = "a value compared with itself";
const uint8_t gm_sbr6d06ao5ra7[16] = "parse_when_stmt";
const uint8_t gm_sbrnv8n0dpinb[8] = "private";
const uint8_t gm_sbu9dvh1hebne[26] = " more calls are not shown";
const uint8_t gm_sc4t0u3ku1fbu[23] = "#define GM_RUN_EXPORT ";
const uint8_t gm_sc4vvo9i5cgel[86] = "the fill character of a format spec is a printable ASCII character other than a brace";
const uint8_t gm_scdb74to7b5sl[39] = "fmt: the base must be between 2 and 36";
const uint8_t gm_sci6hkonrjrne[19] = "remove both `not`s";
const uint8_t gm_sciu02cqahje3[52] = "bswap requires a whole number of bytes, not {} bits";
const uint8_t gm_sckj2gedlq7u6[55] = "((__int128)(((unsigned __int128){}ULL << 64) | {}ULL))";
const uint8_t gm_scqp3lehp7mc6[17] = "gm_bswap({}, {})";
const uint8_t gm_scs8vqh2gmmhd[51] = "`.round` converts to an integer; for the nearest `";
const uint8_t gm_scuc7vp51n9rl[41] = "\000\000\000\000\000\000\000\000\030\000\000\000\000\000\000\000Y\000\000\000\013\000\000\000\000\000\000\000\000\000\000\000\014\000\000\000\000\000\000\000";
const uint8_t gm_sd00cc6js4td5[7] = "index ";
const uint8_t gm_sd2n08j3tm5k4[17] = "human short json";
const uint8_t gm_sdainihnab59h[10] = "` lists `";
const uint8_t gm_sdbqk29a3n8jm[13] = "include-dirs";
const uint8_t gm_sdhauhfs9i578[12] = "gm_u128_dec";
const uint8_t gm_sdkba1tit6s52[17] = "((uint8_t *)&{})";
const uint8_t gm_sdmdremodiqov[25] = "protocol.ext.allow=never";
const uint8_t gm_sdnsovsr4en9v[49] = "`.bits` can only produce plain-data types, not `";
const uint8_t gm_sdo5tfkgp02tp[26] = "client/registerCapability";
const uint8_t gm_sdoflo4g2h6ev[14] = "unmatched `}`";
const uint8_t gm_sdpo2hjlq8e9b[31] = "` takes an integer type, not `";
const uint8_t gm_sdr1j0cf4fck6[20] = " string, expected `";
const uint8_t gm_sdrkb67lqt61h[88] = "the exported name `{}` is also a name of the compiled program's runtime; choose another";
const uint8_t gm_sds8ed13tvvnt[48] = "`{}` does not satisfy `{}({})` required by `{}`";
const uint8_t gm_sdtjbut83s1a1[21] = "cannot convert from ";
const uint8_t gm_sdtpg69jtnl30[30] = "this condition is always true";
const uint8_t gm_se0a26478itki[7] = "pat-ok";
const uint8_t gm_se0f2jka437t4[2] = "o";
const uint8_t gm_se2e37nl2hi6g[4] = "])(";
const uint8_t gm_se47039ecu2b8[10] = "(void) {\012";
const uint8_t gm_se4r520bkfair[18] = "unknown profile `";
const uint8_t gm_seegi1cuj806d[168] = "`{}` is an iterator, not a value: call it as the source of a `for` loop (`for x in {}(\342\200\246)`) or pass it to a function that runs it, such as `collect({}(\342\200\246))` (\302\24713.5)";
const uint8_t gm_segv6dkv6t4tr[7] = " < 0))";
const uint8_t gm_seiql9tkbkhim[52] = "this call never returns: its result type is `never`";
const uint8_t gm_semi8nh4n9n0n[24] = "unexpected end of input";
const uint8_t gm_senose3qe4tvp[3] = ". ";
const uint8_t gm_seqndsmrihepc[10] = "parameter";
const uint8_t gm_sf1gce4kbrsde[8] = "low_pat";
const uint8_t gm_sf1p9u1vovjso[5] = "gm_t";
const uint8_t gm_sf6ug774thos9[15] = "this changes `";
const uint8_t gm_sfa8f3bk569gj[26] = "an untyped complex number";
const uint8_t gm_sfad5qbegckoi[28] = ":</p>\012<dl class=\"members\">\012";
const uint8_t gm_sfbb5kv3rbn0v[29] = "(({})((({}){}) << {}) >> {})";
const uint8_t gm_sfcc9d16o906b[24] = "debug checked safe fast";
const uint8_t gm_sfd6isdlu0d13[21] = "major version number";
const uint8_t gm_sfmnli2if0k7v[15] = "gm_fs_make_dir";
const uint8_t gm_sfphojfmj7uk6[5] = "\240\206\001\000";
const uint8_t gm_sfrjcsmisriv1[3] = "to";
const uint8_t gm_sg0bl57kiha8j[27] = "`{}` has no operation `{}`";
const uint8_t gm_sg3kpa3h16no3[7] = "import";
const uint8_t gm_sg9vhq4ehlfsf[18] = "`{}` is recursive";
const uint8_t gm_sgfbh9novfe35[7] = "url = ";
const uint8_t gm_sgfva5dbm38o8[10] = "((struct ";
const uint8_t gm_sgpgu8iag22ja[15] = "GM_LD({}, env)";
const uint8_t gm_sgpo1se8uggtq[18] = ") gm_trap_unwrap(";
const uint8_t gm_sgrap32255cto[47] = "this loop never runs: its condition is `false`";
const uint8_t gm_sgrqv16lctl3g[41] = "\000\000\000\000\000\000\000\000\020\000\000\000\000\000\000\000A\000\000\000\005\000\000\000\000\000\000\000\000\000\000\000\010\000\000\000\000\000\000\000";
const uint8_t gm_sgu334n5g1kog[16] = "self-assignment";
const uint8_t gm_sh0274v2fqhs4[18] = "package checkouts";
const uint8_t gm_sh0rvdctsredm[64] = "a `defer` or `errdefer` that is the last statement of its block";
const uint8_t gm_sh3h2k0f6ktgl[13] = "the effect `";
const uint8_t gm_sh3r0ji6563do[127] = "dependency `{}` ({}) is not in gamma.lock or the lock is out of date; it cannot be resolved with --offline (run `gamma fetch`)";
const uint8_t gm_sh44iolkubjmq[41] = "\000\000\000\000\000\000\000\000\030\000\000\000\000\000\000\000&\000\000\000\017\000\000\000\000\000\000\000\000\000\000\000\014\000\000\000\000\000\000\000";
const uint8_t gm_sh6h8rdicu443[23] = "compiler/check/call.gm";
const uint8_t gm_sh9ju7u678u0l[28] = "`, so `break` needs a value";
const uint8_t gm_shanrrdljtv7g[4] = "//!";
const uint8_t gm_shauoqbc8hgsg[4] = "KiB";
const uint8_t gm_shc8a6qgqrqaf[42] = "unknown profile `{}` (known profiles: {})";
const uint8_t gm_shgi3g6gdv99o[4] = "-Og";
const uint8_t gm_shj1t0cb4970k[27] = "` has no parameter named `";
const uint8_t gm_shn9a2s1f1vl0[17] = "evok{} \077 {} : {}";
const uint8_t gm_shnpduonp99d0[12] = " & 1) == 0)";
const uint8_t gm_shquo2s3jci8a[42] = "this `if` has an empty body and no `else`";
const uint8_t gm_shsisdh16t023[28] = "cannot read the archive: {}";
const uint8_t gm_shvpku7ssg8d7[11] = "array-fill";
const uint8_t gm_si5knpkeaqca7[137] = "the import path `{}` leaves the directory of `{}`: after the first segment it may not contain empty, `..` or absolute components (\302\2473.2)";
const uint8_t gm_si68mvbt2sjam[19] = "; gm_unw.is_err = ";
const uint8_t gm_si7hg8a0jmhu5[3] = "cn";
const uint8_t gm_si7qrooamq5ei[26] = "` is a label, not a value";
const uint8_t gm_si9fi6l9mcekc[41] = "\000\000\000\000\000\000\000\000\030\000\000\000\000\000\000\000X\000\000\0003\000\000\000\000\000\000\000\000\000\000\000\020\000\000\000\000\000\000\000";
const uint8_t gm_sidtn67sdk4e0[7] = "not in";
const uint8_t gm_sifqpftcv49j6[22] = ", &fl, sizeof fl); }\012";
const uint8_t gm_sig89vu0mvqtm[19] = "`: it is a keyword";
const uint8_t gm_siipob9mkm9hq[24] = " of a known type, not `";
const uint8_t gm_simh6hd7oitki[22] = " positional arguments";
const uint8_t gm_siopg3b55pic9[12] = ", (void *)(";
const uint8_t gm_sioup6jsbm84g[10] = " lint fix";
const uint8_t gm_siupt3h9siv5v[10] = "--format=";
const uint8_t gm_sius9pki5u4e3[5] = "name";
const uint8_t gm_sivr8g8c8o0kq[13] = "(uint8_t)!{}";
const uint8_t gm_sj0kptrfbec99[3] = "/=";
const uint8_t gm_sj2if4k4r1jer[8] = "field `";
const uint8_t gm_sj53lcu5ne531[20] = "#define B (*gm_Bp)\012";
const uint8_t gm_sj6oj7rmit9sk[93] = "(((unsigned __int128)({}) > (unsigned __int128)UINTPTR_MAX) \077 UINTPTR_MAX : (uintptr_t)({}))";
const uint8_t gm_sj70rum4dd30i[18] = "min/max/clamp on ";
const uint8_t gm_sj7f8n5rgjl7r[11] = "gm_hb_find";
const uint8_t gm_sj90kfpommb4k[44] = "expected a value, found the effect set `{}`";
const uint8_t gm_sjc0gph5pdnnm[9] = "\000\000\000\000\001\000\000\000";
const uint8_t gm_sjfs8et3lk0l5[7] = "interp";
const uint8_t gm_sjg14535b6ovp[41] = "` is a git dependency; run `gamma fetch`";
const uint8_t gm_sjhqrnq2lvjcm[35] = "cannot run the C compiler `{}`: {}";
const uint8_t gm_sjl83c3218sse[19] = "codeActionProvider";
const uint8_t gm_sjsojd1o1489e[34] = "; divide by a scalar on the right";
const uint8_t gm_sjviirpelj2et[26] = "expected a function with ";
const uint8_t gm_sk0bf2ce2h62k[10] = "gm_bt_top";
const uint8_t gm_sk1gl0oknml68[18] = "` cannot be run: ";
const uint8_t gm_sk5d5fbcb3pir[10] = "`{}` = {}";
const uint8_t gm_sk698gp5chukg[10] = "Cover({})";
const uint8_t gm_sk754qkda90si[6] = "linux";
const uint8_t gm_sk7ckkvc4fg11[28] = "` has no meaning on vectors";
const uint8_t gm_skcb9kf2svs50[17] = "\000\000\000\000\000\000\000\000\241\006\000\000\000\000\000\000";
const uint8_t gm_skcto6psrjh96[28] = "to close the parameter list";
const uint8_t gm_skesaudrffe2q[55] = "<h2 id=\"contents\">Contents</h2>\012<ul class=\"contents\">\012";
const uint8_t gm_ski66jdm8e243[9] = "<h3 id=\"";
const uint8_t gm_skkvljkq130ke[15] = "string literal";
const uint8_t gm_skmq6kit541c7[6] = "tag `";
const uint8_t gm_skof48dte1s1o[13] = "dependencies";
const uint8_t gm_skpepkrdit47b[11] = "atomic_and";
const uint8_t gm_skpghu6j8rgck[12] = "pat-or : {}";
const uint8_t gm_skr4l871ttakp[13] = "handler-ref ";
const uint8_t gm_sksh6i7obnq40[8] = "module ";
const uint8_t gm_sl37uvr16qsu8[11] = "Bitreverse";
const uint8_t gm_sl6eotv49v54f[38] = "coverage: no code of the package ran\012";
const uint8_t gm_sl7cl2p91o7b5[55] = "` cannot be matched with a struct pattern outside its ";
const uint8_t gm_sl88m8js9368j[39] = "unknown format `{}` (markdown or html)";
const uint8_t gm_slad86501rp5c[19] = "kind = GM_K_STRUCT";
const uint8_t gm_sldikqa9i70sm[52] = "a spread argument cannot instantiate a generic type";
const uint8_t gm_sldjvqvpa1t8p[16] = " while parsing ";
const uint8_t gm_sldmbttac8k2j[59] = "`{}` matches only {}, so {} here goes to the catch-all arm";
const uint8_t gm_slfcjb9tfk917[27] = "unknown escape sequence `\\";
const uint8_t gm_slfn5huoi5r5n[12] = "gm_trap_pow";
const uint8_t gm_slhm5kfb42dip[11] = "gm_pt{}_{}";
const uint8_t gm_sljor9gn53lam[13] = "cannot list ";
const uint8_t gm_slknnflnp0v0v[37] = "main :: fn() -> AllocError!() needs ";
const uint8_t gm_slkubuckp49ih[12] = "thread_join";
const uint8_t gm_slnqcu8l15gll[12] = " = {.eff = ";
const uint8_t gm_slogp373o1rii[24] = "textDocument/completion";
const uint8_t gm_slpfvblm7qee4[13] = "gm_proc_read";
const uint8_t gm_slqgeomhftr16[15] = "#global {}: {}";
const uint8_t gm_sltau19rt9o73[8] = "raw-str";
const uint8_t gm_slv9mko23s9sn[33] = "unexpected character {} after {}";
const uint8_t gm_sm17am2sq1qe4[10] = "--offline";
const uint8_t gm_sm1jl2enra6o1[41] = "\000\000\000\000\000\000\000\000\030\000\000\000\000\000\000\000\004\001\000\000.\000\000\000\000\000\000\000\000\000\000\000\023\000\000\000\000\000\000\000";
const uint8_t gm_sm26tj0iuodd4[41] = "\000\000\000\000\000\000\000\000\031\000\000\000\000\000\000\000~\000\000\000\022\000\000\000\000\000\000\000\000\000\000\000\014\000\000\000\000\000\000\000";
const uint8_t gm_sm337p11h7spc[8] = "^const ";
const uint8_t gm_sm3o0uerjumpm[4] = "min";
const uint8_t gm_sm3sqa0e3amks[9] = "switch (";
const uint8_t gm_sm9funvq1222p[44] = "`[_]` is allowed only in array constructors";
const uint8_t gm_smblbmr0dp3js[8] = "gm_proc";
const uint8_t gm_smc2kdmn5qjql[113] = "this performs the effect `{}`, but {} {}: instantiations with different arguments are different effects (\302\24712.3)";
const uint8_t gm_smd81jcp0bga9[19] = "*(volatile {} *){}";
const uint8_t gm_smh12ad93nju6[45] = "` is constructed by naming each variant of `";
const uint8_t gm_sml1jlm4abpm3[24] = "compiler/fmt/command.gm";
const uint8_t gm_smpmtloh1fun3[36] = "`: it is the name of a builtin type";
const uint8_t gm_smr39cplkqb3n[51] = "cannot rename `{}`: it is part of the core library";
const uint8_t gm_smrba58613c3b[17] = "(GM_LD(void *, &";
const uint8_t gm_smsc5gshqt6cd[20] = " from dependencies\012";
const uint8_t gm_smv2cp06tp4la[13] = "gm_proc_wait";
const uint8_t gm_sn1h26ucppu4i[110] = "a slice of a temporary array is read-only; to pass the array for mutation, put it in a variable first (\302\2477.2)";
const uint8_t gm_sn2furlv6e7jl[4] = "run";
const uint8_t gm_sn5f6h6alcm9q[12] = "operation `";
const uint8_t gm_sn5ntorhqodmg[39] = "the `path` of dependency `{}` is empty";
const uint8_t gm_sn5sjb95plsev[27] = "typedef struct { _Alignas(";
const uint8_t gm_snas2pfnujj1i[15] = "intrinsic-decl";
const uint8_t gm_sne7l93pr2tmb[34] = "invalid boolean, expected `false`";
const uint8_t gm_snf9ak4s55usv[18] = "cannot copy into ";
const uint8_t gm_sngfc91643s6a[55] = "invalid unicode value, expected a unicode scalar value";
const uint8_t gm_snh0sl2cdagdn[13] = "attribute `#";
const uint8_t gm_snh12e0jas620[13] = "gm_out_errno";
const uint8_t gm_snnov1h58rig5[52] = "a compile-time parameter needs a type: `N :: usize`";
const uint8_t gm_snoj0t5lf2lht[4] = "Num";
const uint8_t gm_snqbh8eqcn135[17] = " >= (uintptr_t)(";
const uint8_t gm_snqvo5bgc2fch[8] = "package";
const uint8_t gm_snti96ut68ga4[120] = "`{}` is a constant, but a bare name in a pattern always binds a new name; compare with a guard instead (`x if x is {}`)";
const uint8_t gm_snuinkttlbd28[14] = "{} values are";
const uint8_t gm_snvldulppvqal[23] = "    static gm_node amb";
const uint8_t gm_so1jvf7g8en80[26] = "` have different types: `";
const uint8_t gm_so3oq96gr0k5r[10] = "FsMakeDir";
const uint8_t gm_so6qi13ng7s70[41] = "\000\000\000\000\000\000\000\000\025\000\000\000\000\000\000\000\265\001\000\000\036\000\000\000\000\000\000\000\000\000\000\000\004\000\000\000\000\000\000\000";
const uint8_t gm_so8fkrgrobcsk[9] = "FsRename";
const uint8_t gm_sobumja6poqii[13] = "gm_start(); ";
const uint8_t gm_soeb2q5rdgp9r[14] = "gm_buf_extend";
const uint8_t gm_soft3ordeghp1[17] = "` takes no value";
const uint8_t gm_solagrm41ggme[33] = "(uintptr_t)strlen((const char *)";
const uint8_t gm_som446v3b5hhj[176] = "gamma: a program for a freestanding target builds to an object file only, so the C sources and libraries of its packages are not linked in; link them with the object yourself\012";
const uint8_t gm_somhu4vltiil1[4] = "(*(";
const uint8_t gm_soo4rtckloi4d[13] = "Freestanding";
const uint8_t gm_sovc43d45he5a[21] = "it is above U+10FFFF";
const uint8_t gm_sp0doh9njf7i3[27] = ";\012    gm_init_consts();\012}\012";
const uint8_t gm_sp0p0g46490o0[24] = "gamma: unknown option `";
const uint8_t gm_sp3jmpmb6sldf[5] = "/tmp";
const uint8_t gm_sp6oikjubf9mg[7] = "len = ";
const uint8_t gm_sp8vnj37dd7ds[7] = "piece|";
const uint8_t gm_spg6s3e4lci26[10] = "push_down";
const uint8_t gm_spgt2nbblhu8j[9] = "element ";
const uint8_t gm_sphr6fu31dtrl[3] = "==";
const uint8_t gm_spic2fa42u00e[19] = "constant-condition";
const uint8_t gm_spiov306ab2eh[10] = ", true);\012";
const uint8_t gm_spkgb882eim6u[10] = "checkouts";
const uint8_t gm_spm6tgoorr19q[29] = "this array-to-slice coercion";
const uint8_t gm_spmp7ok3h3mlq[11] = "GAMMA_CORE";
const uint8_t gm_spn2ndrnbok71[34] = "deadlock: no task is ready to run";
const uint8_t gm_spncc9hduhmp8[33] = ", &fp, sizeof fp); memcpy(loc + ";
const uint8_t gm_spqqkr6igks2d[28] = "the server is shutting down";
const uint8_t gm_sps94d2dd3g7k[44] = "internal error: control flow escaped `main`";
const uint8_t gm_spshvjknob1uq[81] = "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000";
const uint8_t gm_sptjupp93c9n4[56] = "`.abs` needs signed integer or float elements, not `{}`";
const uint8_t gm_sq2cbiau3hhqs[11] = " suspended";
const uint8_t gm_sq77ose3aa457[24] = "    gm_thread_stack();\012";
const uint8_t gm_sq94cop5evc92[18] = "gamma-register-{}";
const uint8_t gm_sq9ir3ha51jdh[24] = " positional argument(s)";
const uint8_t gm_sqaek8776r3c5[9] = "variant ";
const uint8_t gm_sqefppq1so38u[21] = "gamma: cannot read `";
const uint8_t gm_sqf9pgeqvvbb4[77] = "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\012<title>";
const uint8_t gm_sqffn766fd6b5[14] = "gm_fmt_fields";
const uint8_t gm_sqhkrul9t68sa[8] = "EnvArgs";
const uint8_t gm_sqhqlj2oc6h5k[20] = "(void) { if (!gm_gi";
const uint8_t gm_sqljkj8r253om[7] = "remove";
const uint8_t gm_sqofj6305nq58[8] = "machine";
const uint8_t gm_sqqajgod2bb4o[4] = "f64";
const uint8_t gm_sqtvhnbm32dvp[23] = "`; write it explicitly";
const uint8_t gm_sqv6e5e2cne7b[59] = "the results `{}` and `{}` differ and are not both pointers";
const uint8_t gm_sr0s4jkm98l01[41] = "\000\000\000\000\000\000\000\000\026\000\000\000\000\000\000\000\352\002\000\000\030\000\000\000\000\000\000\000\000\000\000\000\016\000\000\000\000\000\000\000";
const uint8_t gm_sr66ijstfdd2o[41] = "\000\000\000\000\000\000\000\000\031\000\000\000\000\000\000\000\262\001\000\000`\000\000\000\000\000\000\000\000\000\000\000\004\000\000\000\000\000\000\000";
const uint8_t gm_sr9uf1l734d81[12] = "atomic_load";
const uint8_t gm_srcf8l485c5uq[10] = "c-sources";
const uint8_t gm_srji1enk20v8f[21] = "` (markdown or html)";
const uint8_t gm_srntlv7ksi1vk[16] = "unwrap-optional";
const uint8_t gm_srp5cji88najc[10] = " passed, ";
const uint8_t gm_srramd997nju1[39] = "`@asm` takes positional arguments only";
const uint8_t gm_srthtvs2627tn[29] = "`@{}` is not defined on `{}`";
const uint8_t gm_sruq3cf9ro3q2[66] = "the template has {}, so this spread must have {} values, not `{}`";
const uint8_t gm_srvgp776ipjtg[30] = "cannot call a value of type `";
const uint8_t gm_ss308e7joejdo[13] = "ThreadDetach";
const uint8_t gm_ss4tls6jr9q3m[10] = "profile `";
const uint8_t gm_ss88mk7b1rh87[18] = "converting from `";
const uint8_t gm_ssao50s66pj11[8] = "gm_xr{}";
const uint8_t gm_sscsdlec4a8vt[18] = "    gm_with_end(&";
const uint8_t gm_ssnn9u3skptri[75] = "a shift takes a math vector on the left and one scalar amount on the right";
const uint8_t gm_ssqmgnf1snsdj[8] = "type-is";
const uint8_t gm_ssr3n7oenqiui[13] = "parse_number";
const uint8_t gm_ssrq4cnmtqbuh[83] = "</h1>\012<table>\012<thead><tr><th>Module</th><th>Description</th></tr></thead>\012<tbody>\012";
const uint8_t gm_ssullrletdua5[41] = "\000\000\000\000\000\000\000\000\024\000\000\000\000\000\000\000\304\001\000\000&\000\000\000\000\000\000\000\000\000\000\000\020\000\000\000\000\000\000\000";
const uint8_t gm_st1mshpgajfu8[41] = "\000\000\000\000\000\000\000\000\031\000\000\000\000\000\000\000\362\001\000\000\032\000\000\000\000\000\000\000\000\000\000\000\006\000\000\000\000\000\000\000";
const uint8_t gm_st1pc9bsku00q[9] = "PtrToPtr";
const uint8_t gm_st2pui5pcm1j4[13] = "gm_atag_find";
const uint8_t gm_st4bliqvgaqre[127] = "a function with a calling convention other than C must have effects at most `{Sys}`: it cannot take hidden parameters (\302\24719.2)";
const uint8_t gm_stgav1sk1eq6u[59] = "`, so a method of that name could never be called (\302\2477.10)";
const uint8_t gm_stjei6jfiba56[5] = "fuel";
const uint8_t gm_str1v7jglok76[3] = "ie";
const uint8_t gm_su3j467gt9vk3[14] = "gm_alloc_tag(";
const uint8_t gm_su3sef888krmb[22] = "stale-element-pointer";
const uint8_t gm_sua4hqf3te7mn[15] = "[All modules](";
const uint8_t gm_suh0878eve8t9[10] = "soa_alloc";
const uint8_t gm_suki713bapaoe[7] = "{} are";
const uint8_t gm_sukitf61448ms[14] = "__asm__({});\012";
const uint8_t gm_sup67cuqst8bs[8] = "__riscv";
const uint8_t gm_sus2pjl36h2nd[10] = "alloc_tag";
const uint8_t gm_suskhmu5llblc[67] = "`{}` is made by slicing an array or by `.make(n)`, not constructed";
const uint8_t gm_sv0u97lrebfd4[41] = "internal error: a slice of a #bits field";
const uint8_t gm_sv44h36stbh8s[7] = "` here";
const uint8_t gm_sv9p44pp2n1jk[10] = "clock_now";
const uint8_t gm_svb51eiulbs8i[41] = "\000\000\000\000\000\000\000\000\025\000\000\000\000\000\000\000\213\001\000\000h\000\000\000\000\000\000\000\000\000\000\000\007\000\000\000\000\000\000\000";
const uint8_t gm_sveg1mb15s1b2[18] = "` must be a table";
const uint8_t gm_svkleo8hhv87n[4] = "*|=";
const uint8_t gm_svo4dku3ftf1l[177] = "a list that grows moves its items to a new block, so a pointer or slice into the old ones dangles; index the list again after growing it, or keep the index instead of a pointer";
const uint8_t gm_svu9pa07vreqt[14] = "no space left";
uint8_t gm_c15bjid4jl4[10] __attribute__((aligned(1)));
uint8_t gm_c18viqkn44m[73] __attribute__((aligned(1)));
uint8_t gm_c1cveujlpof[4] __attribute__((aligned(4)));
uint8_t gm_c1prl48ao31[9] __attribute__((aligned(1)));
uint8_t gm_c1smbes90gp[45] __attribute__((aligned(1)));
uint8_t gm_c260filn0hg[12] __attribute__((aligned(1)));
uint8_t gm_c2826uudqo9[4] __attribute__((aligned(1)));
uint8_t gm_c311b27lqem[5] __attribute__((aligned(1)));
uint8_t gm_c35cj6dafmd[4] __attribute__((aligned(1)));
uint8_t gm_c3r46h2npg4[32] __attribute__((aligned(1)));
uint8_t gm_c4d8h2b82ul[13] __attribute__((aligned(1)));
uint8_t gm_c68kng0dmc9[4] __attribute__((aligned(1)));
uint8_t gm_c73blk4giq8[41] __attribute__((aligned(1)));
uint8_t gm_c8a5rud5775[3] __attribute__((aligned(1)));
uint8_t gm_c8fi7e4b2c0[3] __attribute__((aligned(1)));
uint8_t gm_c96ci4s389r[12] __attribute__((aligned(1)));
uint8_t gm_c9vs9vsb4ar[6] __attribute__((aligned(1)));
uint8_t gm_ca853dhk557[3] __attribute__((aligned(1)));
uint8_t gm_ca9oq2dug1j[5] __attribute__((aligned(1)));
uint8_t gm_captp4b2jvk[11] __attribute__((aligned(1)));
uint8_t gm_cb7r52n9k7f[8] __attribute__((aligned(8)));
uint8_t gm_cbbjgr0cpr9[8] __attribute__((aligned(8)));
uint8_t gm_cbcjb1mvjd8[8] __attribute__((aligned(8)));
uint8_t gm_cc2870p5ue7[4] __attribute__((aligned(1)));
uint8_t gm_ccat59d4o57[4] __attribute__((aligned(1)));
uint8_t gm_ccliptj8gld[6] __attribute__((aligned(1)));
uint8_t gm_cco1tols022[7] __attribute__((aligned(1)));
uint8_t gm_ccpknhlhf5b[14] __attribute__((aligned(1)));
uint8_t gm_cddg9214lam[8] __attribute__((aligned(8)));
uint8_t gm_cdmki9acqi9[4] __attribute__((aligned(4)));
uint8_t gm_cduv4b53gs5[16] __attribute__((aligned(8)));
uint8_t gm_ce19ifs9rfv[14] __attribute__((aligned(1)));
uint8_t gm_ce60a0uriev[4] __attribute__((aligned(1)));
uint8_t gm_ceihpp68v5g[46] __attribute__((aligned(1)));
uint8_t gm_cfa8lmn3344[48] __attribute__((aligned(8)));
uint8_t gm_cfcggcra0bi[128] __attribute__((aligned(8)));
uint8_t gm_cfqm8fakuqu[10] __attribute__((aligned(1)));
uint8_t gm_cg0jqpbrhpd[7] __attribute__((aligned(1)));
uint8_t gm_cgmvo0tct53[4] __attribute__((aligned(1)));
uint8_t gm_ch5ml6cehoi[7] __attribute__((aligned(1)));
uint8_t gm_chnhbcnvjio[38] __attribute__((aligned(1)));
uint8_t gm_chops3mqdq7[16] __attribute__((aligned(1)));
uint8_t gm_chu6kqk7n7m[3] __attribute__((aligned(1)));
uint8_t gm_ci656gdeljc[112] __attribute__((aligned(1)));
uint8_t gm_cii63jo6a5p[3] __attribute__((aligned(1)));
uint8_t gm_cjfdffcngsv[16] __attribute__((aligned(1)));
uint8_t gm_cjher1hugak[8] __attribute__((aligned(1)));
uint8_t gm_cjrpcsokd66[4] __attribute__((aligned(1)));
uint8_t gm_ck6sh4an5ll[3] __attribute__((aligned(1)));
uint8_t gm_ck843vssqab[6] __attribute__((aligned(1)));
uint8_t gm_cl5s6n9vfg7[55] __attribute__((aligned(1)));
uint8_t gm_cl7gtp51edk[4] __attribute__((aligned(4)));
uint8_t gm_cm1qt6s2ccu[8] __attribute__((aligned(8)));
uint8_t gm_cmh24klcov2[4] __attribute__((aligned(1)));
uint8_t gm_coobb7mgu3c[11] __attribute__((aligned(1)));
uint8_t gm_cos4c858gsq[1] __attribute__((aligned(1)));
uint8_t gm_cqe47fqk7bg[10] __attribute__((aligned(1)));
uint8_t gm_cql8du9th1j[10] __attribute__((aligned(1)));
uint8_t gm_cs5d24kc9mj[9] __attribute__((aligned(1)));
uint8_t gm_csem906k37c[67] __attribute__((aligned(1)));
uint8_t gm_csl5i9dkkt0[14] __attribute__((aligned(1)));
uint8_t gm_cu7fu2rbnlr[10] __attribute__((aligned(1)));
uint8_t gm_cus4oh838pa[8] __attribute__((aligned(8)));
uint8_t gm_cvjvdnfnhe8[11] __attribute__((aligned(1)));
uint8_t gm_cvorh1r4oo3[11] __attribute__((aligned(1)));
uint8_t gm_k2t1c54q9lq[40] __attribute__((aligned(8)));
uint8_t gm_k5c0o81h2qv[40] __attribute__((aligned(8)));
uint8_t gm_k8upcoeidcs[40] __attribute__((aligned(8)));
uint8_t gm_k8v0lok1ldk[40] __attribute__((aligned(8)));
uint8_t gm_k9m9dc9i2tf[40] __attribute__((aligned(8)));
uint8_t gm_kbjtngq55qv[40] __attribute__((aligned(8)));
uint8_t gm_kc84hfpu27t[40] __attribute__((aligned(8)));
uint8_t gm_kdhojj6r45o[40] __attribute__((aligned(8)));
uint8_t gm_kfh7mc23f4a[40] __attribute__((aligned(8)));
uint8_t gm_kg90e3uuvge[40] __attribute__((aligned(8)));
uint8_t gm_kib6nfi69s7[40] __attribute__((aligned(8)));
uint8_t gm_kja7oc5dvo4[40] __attribute__((aligned(8)));
uint8_t gm_kk1no0qhlfa[40] __attribute__((aligned(8)));
uint8_t gm_km2d9tibmvp[40] __attribute__((aligned(8)));
uint8_t gm_kmgp10tqfjk[40] __attribute__((aligned(8)));
uint8_t gm_kn7qjfrkcl7[40] __attribute__((aligned(8)));
uint8_t gm_kq6mn01fhkq[40] __attribute__((aligned(8)));
uint8_t gm_ksb741egs0v[40] __attribute__((aligned(8)));
uint8_t gm_ksq8uu39c9e[40] __attribute__((aligned(8)));
uint8_t gm_kvubgn35fiq[40] __attribute__((aligned(8)));
void *const gm_hl1l1s81m49k[] = {(void *)gm_f_for_body_69vpqmq};
void *const gm_hl5do7vn7rgp[] = {(void *)gm_f_for_body_3msbccb};
void *const gm_hlv5qrjggnmh[] = {(void *)gm_f_for_body_25s43ua};
