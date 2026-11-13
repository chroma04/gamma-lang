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
typedef struct gm_cseahvg8v7p0vs { uint32_t f0; uint32_t f1; uint32_t f2; } gm_cseahvg8v7p0vs;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint32_t f0; uint8_t f1; uint8_t pe[3]; } gm_t9805qtonbsag;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint32_t f0; uint32_t f1; } gm_tcc1rq4n1avb2;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
_Static_assert(sizeof(gm_cseahvg8v7p0vs) == 12 && _Alignof(gm_cseahvg8v7p0vs) == 4 && offsetof(gm_cseahvg8v7p0vs, f0) == 0 && offsetof(gm_cseahvg8v7p0vs, f1) == 4 && offsetof(gm_cseahvg8v7p0vs, f2) == 8, "layout of `Span`");
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_t08olq97rscn3;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; } gm_t0uuhaljsf410;
typedef struct { gm_tl874t2atnlh3 f0; gm_tl874t2atnlh3 f1; } gm_t2p74ki8sl4an;
typedef struct { gm_b16_8 f0; uint64_t f1; } gm_t3877pk7lv748;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
_Static_assert(sizeof(gm_t9805qtonbsag) == 8 && _Alignof(gm_t9805qtonbsag) == 4 && offsetof(gm_t9805qtonbsag, f0) == 0 && offsetof(gm_t9805qtonbsag, f1) == 4, "layout of `?u32`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
typedef struct { gm_tl874t2atnlh3 f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tbgd14esf0jvi;
_Static_assert(sizeof(gm_tcc1rq4n1avb2) == 8 && _Alignof(gm_tcc1rq4n1avb2) == 4 && offsetof(gm_tcc1rq4n1avb2, f0) == 0 && offsetof(gm_tcc1rq4n1avb2, f1) == 4, "layout of `LineCol`");
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; } gm_tfeab279c16rr;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; gm_b16_8 f4; gm_b16_8 f5; gm_b16_8 f6; } gm_th2d9c7aju8hp;
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { gm_b16_8 f0; gm_t9qkh0rsooc8d f1; uint8_t pe[7]; } gm_tibupt4dmcqai;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; gm_b16_8 f4; gm_b16_8 f5; } gm_tif3elqk01bdl;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; gm_b16_8 f4; } gm_tud7oovfvnf43;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; void * f2; gm_t9qkh0rsooc8d f3; uint8_t p4[7]; void * f4; gm_tl874t2atnlh3 f5; uint8_t pe[4]; } gm_tv2uf4leil5hq;
_Static_assert(sizeof(gm_t08olq97rscn3) == 32 && _Alignof(gm_t08olq97rscn3) == 8 && offsetof(gm_t08olq97rscn3, f0) == 0 && offsetof(gm_t08olq97rscn3, f1) == 16, "layout of `File`");
_Static_assert(sizeof(gm_t0uuhaljsf410) == 64 && _Alignof(gm_t0uuhaljsf410) == 8 && offsetof(gm_t0uuhaljsf410, f0) == 0 && offsetof(gm_t0uuhaljsf410, f1) == 16 && offsetof(gm_t0uuhaljsf410, f2) == 32 && offsetof(gm_t0uuhaljsf410, f3) == 48, "layout of `(string, string, string, string)`");
_Static_assert(sizeof(gm_t2p74ki8sl4an) == 24 && _Alignof(gm_t2p74ki8sl4an) == 4 && offsetof(gm_t2p74ki8sl4an, f0) == 0 && offsetof(gm_t2p74ki8sl4an, f1) == 12, "layout of `(Span, Span)`");
_Static_assert(sizeof(gm_t3877pk7lv748) == 24 && _Alignof(gm_t3877pk7lv748) == 8 && offsetof(gm_t3877pk7lv748, f0) == 0 && offsetof(gm_t3877pk7lv748, f1) == 16, "layout of `WasmReader`");
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t p3[4]; gm_trqfvmjtt4sij f3; gm_trqfvmjtt4sij f4; gm_trqfvmjtt4sij f5; gm_th2lqbo77pvsc f6; } gm_t4gut2c9fj1kt;
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
typedef struct { uint8_t f0; uint8_t p1[7]; gm_trqfvmjtt4sij f1; } gm_tbci5ospk5nnn;
_Static_assert(sizeof(gm_tbgd14esf0jvi) == 32 && _Alignof(gm_tbgd14esf0jvi) == 8 && offsetof(gm_tbgd14esf0jvi, f0) == 0 && offsetof(gm_tbgd14esf0jvi, f1) == 16, "layout of `Edit`");
typedef struct { gm_t08olq97rscn3 f0; gm_t9qkh0rsooc8d f1; uint8_t p2[7]; gm_b16_8 f2; void * f3; gm_tl874t2atnlh3 f4; uint8_t pe[4]; } gm_tc0sbs1tp6d7f;
_Static_assert(sizeof(gm_tfeab279c16rr) == 48 && _Alignof(gm_tfeab279c16rr) == 8 && offsetof(gm_tfeab279c16rr, f0) == 0 && offsetof(gm_tfeab279c16rr, f1) == 16 && offsetof(gm_tfeab279c16rr, f2) == 32, "layout of `Toolchain`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_th2d9c7aju8hp) == 112 && _Alignof(gm_th2d9c7aju8hp) == 8 && offsetof(gm_th2d9c7aju8hp, f0) == 0 && offsetof(gm_th2d9c7aju8hp, f1) == 16 && offsetof(gm_th2d9c7aju8hp, f2) == 32 && offsetof(gm_th2d9c7aju8hp, f3) == 48 && offsetof(gm_th2d9c7aju8hp, f4) == 64 && offsetof(gm_th2d9c7aju8hp, f5) == 80 && offsetof(gm_th2d9c7aju8hp, f6) == 96, "layout of `(string, string, string, string, string, string, string)`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
_Static_assert(sizeof(gm_tibupt4dmcqai) == 24 && _Alignof(gm_tibupt4dmcqai) == 8 && offsetof(gm_tibupt4dmcqai, f0) == 0 && offsetof(gm_tibupt4dmcqai, f1) == 16, "layout of `StrLit`");
_Static_assert(sizeof(gm_tif3elqk01bdl) == 96 && _Alignof(gm_tif3elqk01bdl) == 8 && offsetof(gm_tif3elqk01bdl, f0) == 0 && offsetof(gm_tif3elqk01bdl, f1) == 16 && offsetof(gm_tif3elqk01bdl, f2) == 32 && offsetof(gm_tif3elqk01bdl, f3) == 48 && offsetof(gm_tif3elqk01bdl, f4) == 64 && offsetof(gm_tif3elqk01bdl, f5) == 80, "layout of `(string, string, string, string, string, string)`");
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
typedef struct { gm_t08olq97rscn3 f0; gm_th2lqbo77pvsc f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_tkacitepg5fk0;
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
typedef struct { gm_trqfvmjtt4sij f0; uint8_t f1; uint8_t pe[7]; } gm_tqetrgo0q24b5;
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_tud7oovfvnf43) == 80 && _Alignof(gm_tud7oovfvnf43) == 8 && offsetof(gm_tud7oovfvnf43, f0) == 0 && offsetof(gm_tud7oovfvnf43, f1) == 16 && offsetof(gm_tud7oovfvnf43, f2) == 32 && offsetof(gm_tud7oovfvnf43, f3) == 48 && offsetof(gm_tud7oovfvnf43, f4) == 64, "layout of `(string, string, string, string, string)`");
_Static_assert(sizeof(gm_tv2uf4leil5hq) == 64 && _Alignof(gm_tv2uf4leil5hq) == 8 && offsetof(gm_tv2uf4leil5hq, f0) == 0 && offsetof(gm_tv2uf4leil5hq, f1) == 8 && offsetof(gm_tv2uf4leil5hq, f2) == 24 && offsetof(gm_tv2uf4leil5hq, f3) == 32 && offsetof(gm_tv2uf4leil5hq, f4) == 40 && offsetof(gm_tv2uf4leil5hq, f5) == 48, "layout of `Binding`");
_Static_assert(sizeof(gm_t4gut2c9fj1kt) == 184 && _Alignof(gm_t4gut2c9fj1kt) == 8 && offsetof(gm_t4gut2c9fj1kt, f0) == 0 && offsetof(gm_t4gut2c9fj1kt, f1) == 8 && offsetof(gm_t4gut2c9fj1kt, f2) == 24 && offsetof(gm_t4gut2c9fj1kt, f3) == 40 && offsetof(gm_t4gut2c9fj1kt, f4) == 80 && offsetof(gm_t4gut2c9fj1kt, f5) == 120 && offsetof(gm_t4gut2c9fj1kt, f6) == 160, "layout of `Diagnostic`");
_Static_assert(sizeof(gm_tbci5ospk5nnn) == 48 && _Alignof(gm_tbci5ospk5nnn) == 8 && offsetof(gm_tbci5ospk5nnn, f0) == 0 && offsetof(gm_tbci5ospk5nnn, f1) == 8, "layout of `Int`");
_Static_assert(sizeof(gm_tc0sbs1tp6d7f) == 80 && _Alignof(gm_tc0sbs1tp6d7f) == 8 && offsetof(gm_tc0sbs1tp6d7f, f0) == 0 && offsetof(gm_tc0sbs1tp6d7f, f1) == 32 && offsetof(gm_tc0sbs1tp6d7f, f2) == 40 && offsetof(gm_tc0sbs1tp6d7f, f3) == 56 && offsetof(gm_tc0sbs1tp6d7f, f4) == 64, "layout of `OpDecl`");
_Static_assert(sizeof(gm_tkacitepg5fk0) == 72 && _Alignof(gm_tkacitepg5fk0) == 8 && offsetof(gm_tkacitepg5fk0, f0) == 0 && offsetof(gm_tkacitepg5fk0, f1) == 32 && offsetof(gm_tkacitepg5fk0, f2) == 56, "layout of `Attribute`");
typedef struct { gm_tbci5ospk5nnn f0; gm_tbci5ospk5nnn f1; } gm_tngtuiuskn938;
_Static_assert(sizeof(gm_tqetrgo0q24b5) == 48 && _Alignof(gm_tqetrgo0q24b5) == 8 && offsetof(gm_tqetrgo0q24b5, f0) == 0 && offsetof(gm_tqetrgo0q24b5, f1) == 40, "layout of `?List(usize)`");
_Static_assert(sizeof(gm_tngtuiuskn938) == 96 && _Alignof(gm_tngtuiuskn938) == 8 && offsetof(gm_tngtuiuskn938, f0) == 0 && offsetof(gm_tngtuiuskn938, f1) == 48, "layout of `Rat`");
typedef struct { gm_tngtuiuskn938 f0; gm_tngtuiuskn938 f1; } gm_tu15p0hi2qlv6;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tbci5ospk5nnn v0; gm_tngtuiuskn938 v1; uint8_t v2; gm_tu15p0hi2qlv6 v3; } f1; } gm_t0ialelq3ql1k;
_Static_assert(sizeof(gm_tu15p0hi2qlv6) == 192 && _Alignof(gm_tu15p0hi2qlv6) == 8 && offsetof(gm_tu15p0hi2qlv6, f0) == 0 && offsetof(gm_tu15p0hi2qlv6, f1) == 96, "layout of `(re: Rat, im: Rat)`");
_Static_assert(sizeof(gm_t0ialelq3ql1k) == 200 && _Alignof(gm_t0ialelq3ql1k) == 8 && offsetof(gm_t0ialelq3ql1k, f0) == 0 && offsetof(gm_t0ialelq3ql1k, f1) == 8, "layout of `Untyped`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t88rcucg1b7uk v0; uint32_t v1; uint32_t v2; gm_tiilmfd16v3fc v3; gm_tnpifv20irnph v4; uint32_t v5; gm_tcc1rq4n1avb2 v6; gm_t88rcucg1b7uk v7; gm_t0ialelq3ql1k v8; uint64_t v9; gm_t88rcucg1b7uk v10; } f1; } gm_t6qooom8qa2do;
_Static_assert(sizeof(gm_t6qooom8qa2do) == 208 && _Alignof(gm_t6qooom8qa2do) == 8 && offsetof(gm_t6qooom8qa2do, f0) == 0 && offsetof(gm_t6qooom8qa2do, f1) == 8, "layout of `DeclValue`");
struct gm_fr_check_deprecated_j3dq26t {
    gm_frame h;
    gm_tkacitepg5fk0 l6; /* attr */
    gm_t4gut2c9fj1kt l10; /* w */
    gm_b0_1 ret;
};
struct gm_fr_check_freestanding_use_n95fsk4 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_decl_visible_bd168ti {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_effect_with_op_ol1t6ca {
    gm_frame h;
    gm_tv2uf4leil5hq l8; /* b */
    gm_tc0sbs1tp6d7f l12; /* op */
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_import_fix_7822666 {
    gm_frame h;
    gm_trqfvmjtt4sij l7; /* edits */
    gm_tv2uf4leil5hq l14; /* b */
    gm_tqetrgo0q24b5 ret;
};
struct gm_fr_is_decl_used_oldf2ja {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_used_outside_tbk81ks {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_lookup_module_name_cukfp7t {
    gm_frame h;
    gm_tf5srh4mp4b3i ret;
};
struct gm_fr_module_member_5fd2qm9 {
    gm_frame h;
    gm_t4gut2c9fj1kt l7; /* d */
    gm_tf5srh4mp4b3i ret;
};
struct gm_fr_module_named_n1mcl3b {
    gm_frame h;
    gm_tf5srh4mp4b3i ret;
};
struct gm_fr_note_decl_use_ui57s3b {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_note_local_use_h4lf1jh {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_note_use_from_2lmujsq {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_op_effect_n2jr58f {
    gm_frame h;
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_op_hint_rfghuf5 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_public_module_member_e2i0at7 {
    gm_frame h;
    gm_tf5srh4mp4b3i ret;
};
struct gm_fr_public_op_effect_72d4rbe {
    gm_frame h;
    gm_tv2uf4leil5hq l7; /* b */
    gm_tc0sbs1tp6d7f l11; /* op */
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_same_package_vmps24r {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_std_home_dsdl68c {
    gm_frame h;
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_universe_hidden_gsk2b8r {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_universe_hint_ams3f10 {
    gm_frame h;
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_unknown_name_dmegum5 {
    gm_frame h;
    gm_t4gut2c9fj1kt l5; /* d */
    gm_t4gut2c9fj1kt l6; /* d */
    gm_trqfvmjtt4sij l8; /* edits */
    gm_b0_1 ret;
};
struct gm_fr_visible_ll07dr9 {
    gm_frame h;
    uint8_t ret;
};
bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b);
bool gm_eqff85ac10o3(const uint8_t *a, const uint8_t *b);
__attribute__((aligned(4))) gm_b0_1 gm_f_add_fix_0gkcr4c(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_tl874t2atnlh3 a2, gm_b16_8 a3);
__attribute__((aligned(4))) gm_b0_1 gm_f_add_fix_edits_em0b48a(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_add_label_50fj7vv(void * a0, gm_cseahvg8v7p0vs a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_add_note_46v502s(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_check_deprecated_j3dq26t(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_check_freestanding_use_n95fsk4(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) uint8_t gm_f_contains_a8d6g03(void * a0, const void * a1);
__attribute__((aligned(4))) uint8_t gm_f_decl_visible_bd168ti(void * a0, uint64_t a1, uint64_t a2);
__attribute__((aligned(4))) void gm_f_effect_with_op_ol1t6ca(void *gm_out, void * a0, uint64_t a1, gm_b16_8 a2, uint8_t a3);
__attribute__((aligned(4))) gm_th2lqbo77pvsc gm_f_effect_with_op_ol1t6ca_c(void * a0, uint64_t a1, gm_b16_8 a2, uint8_t a3);
__attribute__((aligned(4))) gm_b0_1 gm_f_emit_dph5v1e(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_error_89glhjr(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_error_s6hn871(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void * gm_f_file_o15fqku(void * a0, uint32_t a1);
__attribute__((aligned(4))) void gm_f_format_1o1mt78(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_1u4jnnu(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_6kfrubb(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_7her5m9(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_88vph9n(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_8hego7t(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_9ihba55(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_adbd97i(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_ahgk48j(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_k09npf9(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_lu3aj0k(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_m420rhg(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_mqdhvcm(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_ooibcq2(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_p16u9uu(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_sbfc2dl(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_vdba58s(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_vlsb354(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) gm_t9805qtonbsag gm_f_get_rlnefnq(void * a0, gm_cseahvg8v7p0vs a1);
__attribute__((aligned(4))) void gm_f_import_fix_7822666(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_index_of_hdeg351(gm_b16_8 a0, uint8_t a1);
__attribute__((aligned(4))) uint8_t gm_f_is_decl_used_oldf2ja(void * a0, uint64_t a1);
__attribute__((aligned(4))) uint8_t gm_f_is_used_outside_tbk81ks(void * a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_list_jekodor(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_lookup_59juqoo(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_lookup_module_name_cukfp7t(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_module_member_5fd2qm9(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, const void * a2, uint64_t a3);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_module_named_n1mcl3b(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_note_decl_use_ui57s3b(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_note_local_use_h4lf1jh(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_note_use_from_2lmujsq(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, uint64_t a2);
__attribute__((aligned(4))) void gm_f_op_effect_n2jr58f(void *gm_out, void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_th2lqbo77pvsc gm_f_op_effect_n2jr58f_c(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b16_8 gm_f_op_hint_rfghuf5(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1, const void * a2);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_public_module_member_e2i0at7(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2, uint64_t a3);
__attribute__((aligned(4))) void gm_f_public_op_effect_72d4rbe(void *gm_out, void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_th2lqbo77pvsc gm_f_public_op_effect_72d4rbe_c(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_k96mobn(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_vd1290p(void * a0, uint8_t a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_3e4k1js(void * a0, gm_cseahvg8v7p0vs a1, uint32_t a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_h767iu8(void * a0, const void * a1, uint8_t a2);
__attribute__((aligned(4))) void gm_f_resolve_decl_pus042v(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
__attribute__((aligned(4))) uint8_t gm_f_same_package_vmps24r(void * a0, uint64_t a1, uint64_t a2);
__attribute__((aligned(4))) gm_cseahvg8v7p0vs gm_f_span_64igq5o(uint32_t a0, uint64_t a1, uint64_t a2);
__attribute__((aligned(4))) uint8_t gm_f_starts_with_6vvt01e(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_std_home_dsdl68c(void *gm_out, gm_b16_8 a0);
__attribute__((aligned(4))) gm_th2lqbo77pvsc gm_f_std_home_dsdl68c_c(gm_b16_8 a0);
__attribute__((aligned(4))) uint8_t gm_f_universe_hidden_gsk2b8r(void * a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_universe_hint_ams3f10(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_unknown_name_dmegum5(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) uint8_t gm_f_visible_ll07dr9(void * a0, gm_t9qkh0rsooc8d a1, uint64_t a2, uint64_t a3);
__attribute__((aligned(4))) void gm_f_warning_q0m5jjs(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0, gm_b16_8 a1);
extern uint8_t gm_c55218gtcr0[448] __attribute__((aligned(8)));
extern uint8_t gm_cejn1dj0054[12] __attribute__((aligned(4)));
extern uint8_t gm_cfiqcgtjhen[416] __attribute__((aligned(8)));
extern const uint8_t gm_s16bsnuu7i04s[3];
extern const uint8_t gm_s1j5pceke7iuk[5];
extern const uint8_t gm_s48njnbffa02m[5];
extern const uint8_t gm_s4pmpk07ugvq6[5];
extern const uint8_t gm_s5le7rpnp1iq6[9];
extern const uint8_t gm_s79oh9p16aa8u[3];
extern const uint8_t gm_s7o7e6piqr3hn[14];
extern const uint8_t gm_s7tvcka4r1prv[10];
extern const uint8_t gm_s84ckbhm2d3ul[9];
extern const uint8_t gm_s88rffcs9cj2p[12];
extern const uint8_t gm_s8hpuukc6o9qu[9];
extern const uint8_t gm_sg6dkeojalp6f[14];
extern const uint8_t gm_sgqcrkl2nh339[4];
extern const uint8_t gm_sgqs124m28gfg[7];
extern const uint8_t gm_shrc3j552p60m[9];
extern const uint8_t gm_si4j8sh2qika1[3];
extern const uint8_t gm_sj97id02v9fh3[6];
extern const uint8_t gm_slmii6hjigrcr[7];
extern const uint8_t gm_sltqebpmj61tp[3];
extern const uint8_t gm_smnot3vdfhqsc[6];
extern const uint8_t gm_smo0niiqr9onn[11];
extern const uint8_t gm_snil57vrv77lr[12];
extern const uint8_t gm_snivf25e5fme8[6];
extern const uint8_t gm_snu0769072h68[9];
extern const uint8_t gm_so97uarpe8rtg[9];
extern const uint8_t gm_sonl5k4jilrbc[13];
extern const uint8_t gm_soo4rtckloi4d[13];
extern const uint8_t gm_sr4qbrk6tlgaa[1];
extern const char gm_src6lm8esmv16p1[];
extern const uint8_t gm_srugm21uf2sb0[9];
extern const uint8_t gm_ssqd2isjrp8nh[11];
extern const uint8_t gm_st3p5nb1e6cc8[5];
extern const uint8_t gm_stingg155o3k5[8];
extern const uint8_t gm_su6mi500md00b[7];
extern const uint8_t gm_suhbskqupk9jm[15];
extern const uint8_t gm_suhmdi1gd6hta[7];
extern const uint8_t gm_sulfrs2bmdlu2[6];
extern const gm_type gm_tyo86ks9qkvt;
__attribute__((aligned(4))) gm_b0_1 gm_f_check_deprecated_j3dq26t(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_tl874t2atnlh3 a2) {
    struct gm_fr_check_deprecated_j3dq26t F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* d */
    gm_tl874t2atnlh3 gm_l2; /* span */
    void * gm_l3; /* decl */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    gm_b16_8 gm_l7; /* msg */
    gm_b16_8 gm_l8; /* args */
    gm_tibupt4dmcqai gm_l9; /* lit */
    GM_STACK_CHECK(&F, 789u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        uint64_t t3 = gm_l1;
        gm_tl874t2atnlh3 t4 = gm_l2;
        gm_b0_1 t5 = gm_f_check_freestanding_use_n95fsk4((void *)NULL, cx, NULL, t2, t3, t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        uint64_t t6 = gm_l1;
        uint64_t t7 = (uintptr_t)t6;
        void * t8 = gm_l0;
        void * t9 = GM_LD(void *, (((uint8_t *)t8) + 0));
        uint64_t t10 = GM_LD(uintptr_t, ((((uint8_t *)t9) + 240) + 0) + 8);
        if (t7 >= (uintptr_t)(t10)) gm_trap_index(t7, t10, gm_src6lm8esmv16p1, 278, 14);
        void * t11 = (void *)((uint8_t *)GM_LD(void *, ((((uint8_t *)t9) + 240) + 0)) + t7 * 216);
        gm_l3 = t11;
    }
    {
        {
            void * t13 = gm_l3;
            gm_b16_8 t14 = GM_LD(gm_b16_8, ((((uint8_t *)t13) + 40) + 40));
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
                    gm_tkacitepg5fk0 t26 = GM_LD(gm_tkacitepg5fk0, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t22 * 72));
                    F.l6 = t26;
                    {
                        uint8_t t29 = (!((GM_LD(uintptr_t, ((((uint8_t *)&F.l6) + 0) + 0) + 8) == 10) && memcmp(GM_LD(void *, ((((uint8_t *)&F.l6) + 0) + 0)), gm_smo0niiqr9onn, 10) == 0));
                        if (!t29) goto ie28;
                        {
                            goto lc15;
                        }
                        goto ix28;
ie28:;
ix28:;
                    }
                    {
                        gm_b16_8 t31;
                        GM_ST(void *, (uint8_t *)&t31, (void *)(gm_sr4qbrk6tlgaa));
                        GM_ST(uintptr_t, (uint8_t *)&t31 + 8, (uintptr_t)(0));
                        gm_l7 = t31;
                    }
                    {
                        if (!(((((uint8_t *)&F.l6) + 32))[16] != 0)) goto ie32;
                        gm_b16_8 t33 = GM_LD(gm_b16_8, (((uint8_t *)&F.l6) + 32));
                        gm_l8 = t33;
                        {
                            uint64_t t36 = GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8);
                            uint8_t t37 = ((t36 == ((uint64_t)1ULL)));
                            if (!t37) goto ie35;
                            {
                                uint64_t t40 = (uintptr_t)((uint64_t)0ULL);
                                uint64_t t41 = GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8);
                                if (t40 >= (uintptr_t)(t41)) gm_trap_index(t40, t41, gm_src6lm8esmv16p1, 284, 33);
                                uint8_t t42 = GM_LD(uint8_t, ((((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l8)) + t40 * 136) + 8) + 0));
                                if (t42 != ((uint8_t)2ULL)) goto ie39;
                                gm_tibupt4dmcqai t43 = GM_LD(gm_tibupt4dmcqai, ((((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l8)) + t40 * 136) + 8) + 8));
                                gm_l9 = t43;
                                {
                                    gm_b16_8 t45 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l9) + 0));
                                    gm_tnpifv20irnph t46;
                                    GM_ST(gm_b16_8, ((uint8_t *)&t46 + 0), t45);
                                    gm_tabb710d9mq2e t47;
                                    gm_f_format_lu3aj0k((void *)&t47, (void *)NULL, cx, NULL, t46);
                                    if (gm_unw.kind) {
                                        goto gm_ll19;
                                    }
                                    gm_tabb710d9mq2e t48 = t47;
                                    if (((((uint8_t *)&t48))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t48) + 8, gm_src6lm8esmv16p1, 284, 54);
                                    gm_b16_8 t49 = GM_LD(gm_b16_8, (((uint8_t *)&t48) + 8));
                                    gm_b16_8 t50 = t49;
                                    gm_b16_8 t51 = GM_LD(gm_b16_8, ((uint8_t *)&t50));
                                    gm_l7 = t51;
                                }
                                goto ix39;
ie39:;
ix39:;
                            }
                            goto ix35;
ie35:;
ix35:;
                        }
                        goto ix32;
ie32:;
ix32:;
                    }
                    {
                        gm_tl874t2atnlh3 t52 = gm_l2;
                        gm_t6j6smj7qoctu t53;
                        void * t54 = gm_l3;
                        gm_b16_8 t55 = GM_LD(gm_b16_8, ((((uint8_t *)t54) + 0) + 0));
                        GM_ST(gm_b16_8, (uint8_t *)&t53 + 0, t55);
                        gm_b16_8 t56 = gm_l7;
                        GM_ST(gm_b16_8, (uint8_t *)&t53 + 16, t56);
                        gm_tabb710d9mq2e t57;
                        gm_f_format_k09npf9((void *)&t57, (void *)NULL, cx, NULL, &t53);
                        if (gm_unw.kind) {
                            goto gm_ll19;
                        }
                        gm_tabb710d9mq2e t58 = t57;
                        if (((((uint8_t *)&t58))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t58) + 8, gm_src6lm8esmv16p1, 287, 33);
                        gm_b16_8 t59 = GM_LD(gm_b16_8, (((uint8_t *)&t58) + 8));
                        gm_b16_8 t60 = t59;
                        gm_b16_8 t61 = GM_LD(gm_b16_8, ((uint8_t *)&t60));
                        gm_f_warning_q0m5jjs((void *)&F.l10, (void *)NULL, cx, NULL, t52, t61);
                        if (gm_unw.kind) {
                            goto gm_ll19;
                        }
                    }
                    {
                        void * t62 = (void *)((uint8_t *)&F.l10);
                        void * t63 = gm_l3;
                        gm_tl874t2atnlh3 t64 = GM_LD(gm_tl874t2atnlh3, ((((uint8_t *)t63) + 0) + 16));
                        gm_b16_8 t65;
                        GM_ST(void *, (uint8_t *)&t65, (void *)(gm_sg6dkeojalp6f));
                        GM_ST(uintptr_t, (uint8_t *)&t65 + 8, (uintptr_t)(13));
                        gm_f_add_label_50fj7vv(t62, GM_CONV(gm_cseahvg8v7p0vs, t64), t65);
                    }
                    {
                        void * t66 = gm_l0;
                        gm_b0_1 t67 = gm_f_emit_dph5v1e((void *)NULL, cx, NULL, t66, &(*(gm_t4gut2c9fj1kt *)((uint8_t *)&F.l10)));
                        if (gm_unw.kind) {
                            goto gm_ll19;
                        }
                    }
                    {
                        gm_b0_1 t68;
                        uint8_t t69 = 0;
                        F.ret = t68;
                        goto gm_fret;
                    }
                    goto me24;
mn25:;
                    gm_trap("no match arm matched the scrutinee", gm_src6lm8esmv16p1, 279, 5);
me24:;
                }
lc15:;
                {
                    uint64_t t71 = gm_l5;
                    uint64_t r72; bool o73 = __builtin_add_overflow(t71, ((uint64_t)1ULL), &r72);
                    uint64_t t74 = r72;
                    gm_l5 = t74;
                }
            }
gm_ll19:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb15; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc15; }
            }
            goto gm_lad;
lb15:;
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
__attribute__((aligned(4))) gm_b0_1 gm_f_check_freestanding_use_n95fsk4(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_tl874t2atnlh3 a2) {
    struct gm_fr_check_freestanding_use_n95fsk4 F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* d */
    gm_tl874t2atnlh3 gm_l2; /* span */
    void * gm_l3; /* decl */
    gm_b16_8 gm_l4; /* name */
    gm_b16_8 gm_l5; /* path */
    GM_STACK_CHECK(&F, 772u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        uint8_t t3 = 0;
        void * t4 = gm_l0;
        void * t5 = GM_LD(void *, (((uint8_t *)t4) + 0));
        uint8_t t6 = (!((GM_LD(uintptr_t, ((((uint8_t *)t5) + 88) + 24) + 8) == 12) && memcmp(GM_LD(void *, ((((uint8_t *)t5) + 88) + 24)), gm_soo4rtckloi4d, 12) == 0));
        if (!t6) {
            gm_tl874t2atnlh3 t7 = gm_l2;
            gm_tl874t2atnlh3 t8 = GM_LD(gm_tl874t2atnlh3, gm_cejn1dj0054);
            gm_tl874t2atnlh3 t9 = t7;
            gm_tl874t2atnlh3 t10 = t8;
            uint8_t t11 = (gm_eqff85ac10o3(((uint8_t *)&t9), ((uint8_t *)&t10)));
            t3 = t11;
        } else t3 = 1;
        if (!t3) goto ie2;
        {
            gm_b0_1 t13;
            uint8_t t14 = 0;
            F.ret = t13;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        uint64_t t15 = gm_l1;
        uint64_t t16 = (uintptr_t)t15;
        void * t17 = gm_l0;
        void * t18 = GM_LD(void *, (((uint8_t *)t17) + 0));
        uint64_t t19 = GM_LD(uintptr_t, ((((uint8_t *)t18) + 240) + 0) + 8);
        if (t16 >= (uintptr_t)(t19)) gm_trap_index(t16, t19, gm_src6lm8esmv16p1, 298, 14);
        void * t20 = (void *)((uint8_t *)GM_LD(void *, ((((uint8_t *)t18) + 240) + 0)) + t16 * 216);
        gm_l3 = t20;
    }
    {
        void * t21 = gm_l3;
        gm_b16_8 t22 = GM_LD(gm_b16_8, ((((uint8_t *)t21) + 0) + 0));
        gm_l4 = t22;
    }
    {
        uint8_t t24 = 0;
        void * t25 = gm_l3;
        uint64_t t26 = GM_LD(uint64_t, (((uint8_t *)t25) + 32));
        uint64_t t27 = (uintptr_t)t26;
        void * t28 = gm_l0;
        void * t29 = GM_LD(void *, (((uint8_t *)t28) + 0));
        uint64_t t30 = GM_LD(uintptr_t, ((((uint8_t *)t29) + 200) + 0) + 8);
        if (t27 >= (uintptr_t)(t30)) gm_trap_index(t27, t30, gm_src6lm8esmv16p1, 300, 12);
        uint8_t t31 = GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t29) + 200) + 0)) + t27 * 184) + 112));
        uint8_t t32 = (uint8_t)!t31;
        if (!t32) {
            gm_b16_8 t33 = gm_l4;
            gm_tfeab279c16rr t34;
            gm_b16_8 t35;
            GM_ST(void *, (uint8_t *)&t35, (void *)(gm_suhbskqupk9jm));
            GM_ST(uintptr_t, (uint8_t *)&t35 + 8, (uintptr_t)(14));
            GM_ST(gm_b16_8, (uint8_t *)&t34 + 0, t35);
            gm_b16_8 t36;
            GM_ST(void *, (uint8_t *)&t36, (void *)(gm_s7tvcka4r1prv));
            GM_ST(uintptr_t, (uint8_t *)&t36 + 8, (uintptr_t)(9));
            GM_ST(gm_b16_8, (uint8_t *)&t34 + 16, t36);
            gm_b16_8 t37;
            GM_ST(void *, (uint8_t *)&t37, (void *)(gm_s7o7e6piqr3hn));
            GM_ST(uintptr_t, (uint8_t *)&t37 + 8, (uintptr_t)(13));
            GM_ST(gm_b16_8, (uint8_t *)&t34 + 32, t37);
            gm_tfeab279c16rr t38 = t34;
            uint8_t t39 = 0;
            gm_b16_8 t40 = t33;
            if (gm_eq0iqiubfm7e(((uint8_t *)&t38) + 0, ((uint8_t *)&t40))) t39 = 1;
            if (gm_eq0iqiubfm7e(((uint8_t *)&t38) + 16, ((uint8_t *)&t40))) t39 = 1;
            if (gm_eq0iqiubfm7e(((uint8_t *)&t38) + 32, ((uint8_t *)&t40))) t39 = 1;
            uint8_t t41 = (uint8_t)(!t39);
            t24 = t41;
        } else t24 = 1;
        if (!t24) goto ie23;
        {
            gm_b0_1 t43;
            uint8_t t44 = 0;
            F.ret = t43;
            goto gm_fret;
        }
        goto ix23;
ie23:;
ix23:;
    }
    {
        void * t45 = gm_l0;
        void * t46 = GM_LD(void *, (((uint8_t *)t45) + 0));
        void * t47 = GM_LD(void *, (((uint8_t *)t46) + 0));
        uint32_t t48 = GM_LD(uint32_t, (((uint8_t *)&gm_l2) + 0));
        void * t49 = gm_f_file_o15fqku(t47, t48);
        gm_b16_8 t50 = GM_LD(gm_b16_8, (((uint8_t *)t49) + 0));
        gm_l5 = t50;
    }
    {
        uint8_t t52 = 0;
        uint8_t t53 = 0;
        gm_b16_8 t54 = gm_l5;
        gm_b16_8 t55;
        GM_ST(void *, (uint8_t *)&t55, (void *)(gm_slmii6hjigrcr));
        GM_ST(uintptr_t, (uint8_t *)&t55 + 8, (uintptr_t)(6));
        uint8_t t56 = gm_f_starts_with_6vvt01e(t54, t55);
        if (!t56) {
            gm_b16_8 t57 = gm_l5;
            void * t58 = gm_l0;
            void * t59 = GM_LD(void *, (((uint8_t *)t58) + 0));
            gm_b16_8 t60 = GM_LD(gm_b16_8, (((uint8_t *)t59) + 152));
            uint8_t t61 = gm_f_starts_with_6vvt01e(t57, t60);
            t53 = t61;
        } else t53 = 1;
        if (!t53) {
            gm_b16_8 t62 = gm_l5;
            void * t63 = gm_l0;
            void * t64 = GM_LD(void *, (((uint8_t *)t63) + 0));
            gm_b16_8 t65 = GM_LD(gm_b16_8, (((uint8_t *)t64) + 168));
            uint8_t t66 = gm_f_starts_with_6vvt01e(t62, t65);
            t52 = t66;
        } else t52 = 1;
        if (!t52) goto ie51;
        {
            gm_b0_1 t68;
            uint8_t t69 = 0;
            F.ret = t68;
            goto gm_fret;
        }
        goto ix51;
ie51:;
ix51:;
    }
    {
        void * t70 = gm_l0;
        gm_tl874t2atnlh3 t71 = gm_l2;
        gm_b16_8 t72 = gm_l4;
        gm_tnpifv20irnph t73;
        GM_ST(gm_b16_8, ((uint8_t *)&t73 + 0), t72);
        gm_tabb710d9mq2e t74;
        gm_f_format_88vph9n((void *)&t74, (void *)NULL, cx, NULL, t73);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t75 = t74;
        if (((((uint8_t *)&t75))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t75) + 8, gm_src6lm8esmv16p1, 303, 19);
        gm_b16_8 t76 = GM_LD(gm_b16_8, (((uint8_t *)&t75) + 8));
        gm_b16_8 t77 = t76;
        gm_b16_8 t78 = GM_LD(gm_b16_8, ((uint8_t *)&t77));
        gm_b0_1 t79 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t70, t71, t78);
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
inline __attribute__((aligned(4))) uint8_t gm_f_decl_visible_bd168ti(void * a0, uint64_t a1, uint64_t a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_decl_visible_bd168ti F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* d */
    uint64_t gm_l2; /* from */
    void * gm_l3; /* decl */
    GM_STACK_CHECK(&F, 125u);
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
__attribute__((aligned(4))) void gm_f_effect_with_op_ol1t6ca(void *gm_out, void * a0, uint64_t a1, gm_b16_8 a2, uint8_t a3) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_effect_with_op_ol1t6ca F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* m */
    gm_b16_8 gm_l2; /* name */
    uint8_t gm_l3; /* public_only */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    gm_t3877pk7lv748 gm_l6; /* n */
    void * gm_l7; /* d */
    void * gm_l9; /* lit */
    gm_b16_8 gm_l10; /* <tmp> */
    uint64_t gm_l11; /* <tmp> */
    GM_STACK_CHECK(&F, 706u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        {
            uint64_t t3 = gm_l1;
            uint64_t t4 = (uintptr_t)t3;
            void * t5 = gm_l0;
            void * t6 = GM_LD(void *, (((uint8_t *)t5) + 0));
            uint64_t t7 = GM_LD(uintptr_t, ((((uint8_t *)t6) + 200) + 0) + 8);
            if (t4 >= (uintptr_t)(t7)) gm_trap_index(t4, t7, gm_src6lm8esmv16p1, 231, 14);
            gm_b16_8 t8 = GM_LD(gm_b16_8, ((((uint8_t *)GM_LD(void *, ((((uint8_t *)t6) + 200) + 0)) + t4 * 184) + 16) + 0));
            gm_l4 = t8;
        }
        {
            gm_l5 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t10 = gm_l5;
                uint64_t t11 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                uint8_t t12 = (t10 < t11);
                if (!t12) goto lb9;
                {
                    uint64_t t15 = gm_l5;
                    uint64_t t16 = (uintptr_t)t15;
                    uint64_t t17 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                    gm_t3877pk7lv748 t20 = GM_LD(gm_t3877pk7lv748, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t16 * 24));
                    gm_l6 = t20;
                    {
                        uint64_t t22 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 16));
                        uint64_t t23 = (uintptr_t)t22;
                        void * t24 = gm_l0;
                        void * t25 = GM_LD(void *, (((uint8_t *)t24) + 0));
                        uint64_t t26 = GM_LD(uintptr_t, ((((uint8_t *)t25) + 240) + 0) + 8);
                        if (t23 >= (uintptr_t)(t26)) gm_trap_index(t23, t26, gm_src6lm8esmv16p1, 232, 15);
                        void * t27 = (void *)((uint8_t *)GM_LD(void *, ((((uint8_t *)t25) + 240) + 0)) + t23 * 216);
                        gm_l7 = t27;
                    }
                    {
                        uint8_t t29 = gm_l3;
                        if (!t29) goto ie28;
                        void * t30 = gm_l7;
                        uint8_t t31 = (!(GM_LD(uint8_t, ((((uint8_t *)t30) + 40) + 56) + 0) == ((uint8_t)2ULL)));
                        if (!t31) goto ie28;
                        {
                            goto lc9;
                        }
                        goto ix28;
ie28:;
ix28:;
                    }
                    {
                        void * t34 = gm_l7;
                        uint8_t t35 = GM_LD(uint8_t, (((((uint8_t *)t34) + 40) + 64) + 0));
                        if (t35 != ((uint8_t)0ULL)) goto ie33;
                        gm_tv2uf4leil5hq t36 = GM_LD(gm_tv2uf4leil5hq, (((((uint8_t *)t34) + 40) + 64) + 8));
                        F.l8 = t36;
                        {
                            void * t39 = GM_LD(void *, (((uint8_t *)&F.l8) + 40));
                            uint8_t t40 = GM_LD(uint8_t, ((((uint8_t *)t39) + 8) + 0));
                            if (t40 != ((uint8_t)34ULL)) goto ie38;
                            void * t41 = GM_LD(void *, ((((uint8_t *)t39) + 8) + 8));
                            gm_l9 = t41;
                            {
                                {
                                    void * t44 = gm_l9;
                                    gm_b16_8 t45 = GM_LD(gm_b16_8, (((uint8_t *)t44) + 24));
                                    gm_l10 = t45;
                                }
                                {
                                    gm_l11 = ((uint64_t)0ULL);
                                }
                                {
                                    for (;;) {
                                        uint64_t t47 = gm_l11;
                                        uint64_t t48 = GM_LD(uintptr_t, ((uint8_t *)&gm_l10) + 8);
                                        uint8_t t49 = (t47 < t48);
                                        if (!t49) goto lb46;
                                        {
                                            uint64_t t52 = gm_l11;
                                            uint64_t t53 = (uintptr_t)t52;
                                            uint64_t t54 = GM_LD(uintptr_t, ((uint8_t *)&gm_l10) + 8);
                                            gm_tc0sbs1tp6d7f t57 = GM_LD(gm_tc0sbs1tp6d7f, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l10)) + t53 * 80));
                                            F.l12 = t57;
                                            {
                                                gm_b16_8 t60 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l12) + 0) + 0));
                                                gm_b16_8 t61 = gm_l2;
                                                gm_b16_8 t62 = t60;
                                                gm_b16_8 t63 = t61;
                                                uint8_t t64 = (gm_eq0iqiubfm7e(((uint8_t *)&t62), ((uint8_t *)&t63)));
                                                if (!t64) goto ie59;
                                                {
                                                    void * t66 = gm_l7;
                                                    gm_b16_8 t67 = GM_LD(gm_b16_8, ((((uint8_t *)t66) + 0) + 0));
                                                    gm_th2lqbo77pvsc t68;
                                                    GM_ST(uint8_t, (uint8_t *)&t68 + 17, 0);
                                                    GM_ST(uint16_t, (uint8_t *)&t68 + 18, 0);
                                                    GM_ST(uint32_t, (uint8_t *)&t68 + 20, 0);
                                                    GM_ST(gm_b16_8, ((uint8_t *)&t68), t67);
                                                    ((uint8_t *)&t68)[16] = 1;
                                                    uint8_t t69 = 0;
                                                    F.ret = t68;
                                                    goto gm_fret;
                                                }
                                                goto ix59;
ie59:;
ix59:;
                                            }
                                            goto me55;
mn56:;
                                            gm_trap("no match arm matched the scrutinee", gm_src6lm8esmv16p1, 236, 9);
me55:;
                                        }
lc46:;
                                        {
                                            uint64_t t71 = gm_l11;
                                            uint64_t r72; bool o73 = __builtin_add_overflow(t71, ((uint64_t)1ULL), &r72);
                                            uint64_t t74 = r72;
                                            gm_l11 = t74;
                                        }
                                    }
lb46:;
                                }
                            }
                            goto ix38;
ie38:;
                            {
                                goto lc9;
                            }
ix38:;
                        }
                        goto ix33;
ie33:;
                        {
                            goto lc9;
                        }
ix33:;
                    }
                    goto me18;
mn19:;
                    gm_trap("no match arm matched the scrutinee", gm_src6lm8esmv16p1, 231, 5);
me18:;
                }
lc9:;
                {
                    uint64_t t78 = gm_l5;
                    uint64_t r79; bool o80 = __builtin_add_overflow(t78, ((uint64_t)1ULL), &r79);
                    uint64_t t81 = r79;
                    gm_l5 = t81;
                }
            }
lb9:;
        }
    }
    {
        gm_th2lqbo77pvsc t82;
        memset(&t82, 0, sizeof t82);
        uint8_t t83 = 0;
        F.ret = t82;
        goto gm_fret;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_th2lqbo77pvsc gm_f_effect_with_op_ol1t6ca_c(void * a0, uint64_t a1, gm_b16_8 a2, uint8_t a3) { gm_th2lqbo77pvsc gm_r; gm_f_effect_with_op_ol1t6ca(&gm_r, a0, a1, a2, a3); return gm_r; }
__attribute__((aligned(4))) void gm_f_import_fix_7822666(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2, gm_tl874t2atnlh3 a3) {
    struct gm_fr_import_fix_7822666 F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* module */
    gm_b16_8 gm_l2; /* name */
    gm_tl874t2atnlh3 gm_l3; /* span */
    gm_b16_8 gm_l4; /* path */
    void * gm_l5; /* m */
    gm_b16_8 gm_l6; /* alias */
    gm_th2lqbo77pvsc gm_l8; /* existing */
    gm_t9805qtonbsag gm_l9; /* last_import_end */
    gm_b16_8 gm_l10; /* <tmp> */
    uint64_t gm_l11; /* <tmp> */
    gm_t3877pk7lv748 gm_l12; /* n */
    void * gm_l13; /* decl */
    gm_tibupt4dmcqai gm_l15; /* p */
    uint32_t gm_l16; /* hi */
    uint32_t gm_l17; /* e */
    gm_b16_8 gm_l18; /* text */
    gm_b16_8 gm_l19; /* name_text */
    gm_b16_8 gm_l20; /* a */
    gm_b16_8 gm_l21; /* line */
    uint64_t gm_l22; /* at */
    uint32_t gm_l23; /* hi */
    uint64_t gm_l24; /* nl */
    uint64_t gm_l25; /* nl */
    GM_STACK_CHECK(&F, 2552u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        gm_b16_8 t2 = gm_l2;
        gm_th2lqbo77pvsc t3;
        gm_f_std_home_dsdl68c((void *)&t3, t2);
        gm_th2lqbo77pvsc t4 = t3;
        gm_b16_8 t5;
        if (((((uint8_t *)&t4))[16] != 0)) {
            gm_b16_8 t6 = GM_LD(gm_b16_8, (((uint8_t *)&t4) + 0));
            t5 = t6;
        } else {
            gm_tqetrgo0q24b5 t7;
            memset(&t7, 0, sizeof t7);
            uint8_t t8 = 0;
            F.ret = t7;
            goto gm_fret;
        }
        gm_l4 = t5;
    }
    {
        uint64_t t9 = gm_l1;
        uint64_t t10 = (uintptr_t)t9;
        void * t11 = gm_l0;
        void * t12 = GM_LD(void *, (((uint8_t *)t11) + 0));
        uint64_t t13 = GM_LD(uintptr_t, ((((uint8_t *)t12) + 200) + 0) + 8);
        if (t10 >= (uintptr_t)(t13)) gm_trap_index(t10, t13, gm_src6lm8esmv16p1, 100, 11);
        void * t14 = (void *)((uint8_t *)GM_LD(void *, ((((uint8_t *)t12) + 200) + 0)) + t10 * 184);
        gm_l5 = t14;
    }
    {
        uint8_t t16 = 0;
        void * t17 = gm_l5;
        uint8_t t18 = GM_LD(uint8_t, (((uint8_t *)t17) + 112));
        if (!t18) {
            void * t19 = gm_l5;
            uint8_t t20 = GM_LD(uint8_t, (((uint8_t *)t19) + 113));
            t16 = t20;
        } else t16 = 1;
        if (!t16) goto ie15;
        {
            gm_tqetrgo0q24b5 t22;
            memset(&t22, 0, sizeof t22);
            uint8_t t23 = 0;
            F.ret = t22;
            goto gm_fret;
        }
        goto ix15;
ie15:;
ix15:;
    }
    {
        uint64_t t24 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
        void * t25 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)));
        uint64_t t26 = (uintptr_t)((uint64_t)4ULL);
        if (t26 > (uintptr_t)t24 || (uintptr_t)t24 > (uintptr_t)t24) gm_trap_slice(t26, (uintptr_t)t24, t24, gm_src6lm8esmv16p1, 102, 14);
        gm_b16_8 t27;
        GM_ST(void *, (uint8_t *)&t27, (void *)(((uint8_t *)t25 + t26 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t27 + 8, (uintptr_t)((uintptr_t)t24 - t26));
        gm_l6 = t27;
    }
    {
        gm_f_list_jekodor((void *)&F.l7, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_th2lqbo77pvsc t28;
        memset(&t28, 0, sizeof t28);
        gm_l8 = t28;
    }
    {
        gm_t9805qtonbsag t29;
        memset(&t29, 0, sizeof t29);
        gm_l9 = t29;
    }
    {
        {
            void * t31 = gm_l5;
            gm_b16_8 t32 = GM_LD(gm_b16_8, ((((uint8_t *)t31) + 16) + 0));
            gm_l10 = t32;
        }
        {
            gm_l11 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t34 = gm_l11;
                uint64_t t35 = GM_LD(uintptr_t, ((uint8_t *)&gm_l10) + 8);
                uint8_t t36 = (t34 < t35);
                if (!t36) goto lb33;
                {
                    uint64_t t39 = gm_l11;
                    uint64_t t40 = (uintptr_t)t39;
                    uint64_t t41 = GM_LD(uintptr_t, ((uint8_t *)&gm_l10) + 8);
                    gm_t3877pk7lv748 t44 = GM_LD(gm_t3877pk7lv748, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l10)) + t40 * 24));
                    gm_l12 = t44;
                    {
                        uint64_t t46 = GM_LD(uint64_t, (((uint8_t *)&gm_l12) + 16));
                        uint64_t t47 = (uintptr_t)t46;
                        void * t48 = gm_l0;
                        void * t49 = GM_LD(void *, (((uint8_t *)t48) + 0));
                        uint64_t t50 = GM_LD(uintptr_t, ((((uint8_t *)t49) + 240) + 0) + 8);
                        if (t47 >= (uintptr_t)(t50)) gm_trap_index(t47, t50, gm_src6lm8esmv16p1, 107, 18);
                        void * t51 = (void *)((uint8_t *)GM_LD(void *, ((((uint8_t *)t49) + 240) + 0)) + t47 * 216);
                        gm_l13 = t51;
                    }
                    {
                        void * t53 = gm_l13;
                        uint32_t t54 = GM_LD(uint32_t, (((((uint8_t *)t53) + 40) + 4) + 0));
                        uint32_t t55 = GM_LD(uint32_t, (((uint8_t *)&gm_l3) + 0));
                        uint8_t t56 = (!(t54 == t55));
                        if (!t56) goto ie52;
                        {
                            goto lc33;
                        }
                        goto ix52;
ie52:;
ix52:;
                    }
                    {
                        void * t59 = gm_l13;
                        uint8_t t60 = GM_LD(uint8_t, (((((uint8_t *)t59) + 40) + 64) + 0));
                        if (t60 != ((uint8_t)0ULL)) goto ie58;
                        gm_tv2uf4leil5hq t61 = GM_LD(gm_tv2uf4leil5hq, (((((uint8_t *)t59) + 40) + 64) + 8));
                        F.l14 = t61;
                        {
                            uint8_t t64 = 0;
                            void * t65 = GM_LD(void *, (((uint8_t *)&F.l14) + 24));
                            void * t66 = t65;
                            uint8_t t67;
                            if ((GM_LD(void *, ((uint8_t *)&t66)) != 0)) goto mn69;
                            t67 = ((uint8_t)0);
                            goto me68;
mn69:;
                            t67 = ((uint8_t)1);
                            goto me68;
mn70:;
                            gm_trap("no match arm matched the scrutinee", gm_src6lm8esmv16p1, 110, 12);
me68:;
                            if (!t67) {
                                uint64_t t71 = GM_LD(uintptr_t, (((uint8_t *)&F.l14) + 8) + 8);
                                uint8_t t72 = (!(t71 == ((uint64_t)1ULL)));
                                t64 = t72;
                            } else t64 = 1;
                            if (!t64) goto ie63;
                            {
                                goto lc33;
                            }
                            goto ix63;
ie63:;
ix63:;
                        }
                        {
                            void * t75 = GM_LD(void *, (((uint8_t *)&F.l14) + 40));
                            uint8_t t76 = GM_LD(uint8_t, ((((uint8_t *)t75) + 8) + 0));
                            if (t76 != ((uint8_t)38ULL)) goto ie74;
                            gm_tibupt4dmcqai t77 = GM_LD(gm_tibupt4dmcqai, ((((uint8_t *)t75) + 8) + 8));
                            gm_l15 = t77;
                            {
                                gm_b16_8 t80 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l15) + 0));
                                gm_b16_8 t81 = t80;
                                gm_b16_8 t82 = GM_LD(gm_b16_8, ((uint8_t *)&t81));
                                gm_b16_8 t83 = gm_l4;
                                gm_b16_8 t84 = t82;
                                gm_b16_8 t85 = t83;
                                uint8_t t86 = (gm_eq0iqiubfm7e(((uint8_t *)&t84), ((uint8_t *)&t85)));
                                if (!t86) goto ie79;
                                {
                                    void * t88 = gm_l13;
                                    gm_b16_8 t89 = GM_LD(gm_b16_8, ((((uint8_t *)t88) + 0) + 0));
                                    gm_th2lqbo77pvsc t90;
                                    GM_ST(uint8_t, (uint8_t *)&t90 + 17, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t90 + 18, 0);
                                    GM_ST(uint32_t, (uint8_t *)&t90 + 20, 0);
                                    GM_ST(gm_b16_8, ((uint8_t *)&t90), t89);
                                    ((uint8_t *)&t90)[16] = 1;
                                    gm_l8 = t90;
                                }
                                goto ix79;
ie79:;
ix79:;
                            }
                            {
                                void * t91 = GM_LD(void *, (((uint8_t *)&F.l14) + 40));
                                uint32_t t92 = GM_LD(uint32_t, ((((uint8_t *)t91) + 120) + 8));
                                gm_l16 = t92;
                            }
                            {
                                if (!((((uint8_t *)&gm_l9))[4] != 0)) goto ie93;
                                uint32_t t94 = GM_LD(uint32_t, ((uint8_t *)&gm_l9));
                                gm_l17 = t94;
                                {
                                    uint32_t t97 = gm_l16;
                                    uint32_t t98 = gm_l17;
                                    uint8_t t99 = (t97 > t98);
                                    if (!t99) goto ie96;
                                    {
                                        uint32_t t101 = gm_l16;
                                        gm_t9805qtonbsag t102;
                                        GM_ST(uint8_t, (uint8_t *)&t102 + 5, 0);
                                        GM_ST(uint16_t, (uint8_t *)&t102 + 6, 0);
                                        GM_ST(uint32_t, ((uint8_t *)&t102), t101);
                                        ((uint8_t *)&t102)[4] = 1;
                                        gm_l9 = t102;
                                    }
                                    goto ix96;
ie96:;
ix96:;
                                }
                                goto ix93;
ie93:;
                                {
                                    uint32_t t104 = gm_l16;
                                    gm_t9805qtonbsag t105;
                                    GM_ST(uint8_t, (uint8_t *)&t105 + 5, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t105 + 6, 0);
                                    GM_ST(uint32_t, ((uint8_t *)&t105), t104);
                                    ((uint8_t *)&t105)[4] = 1;
                                    gm_l9 = t105;
                                }
ix93:;
                            }
                            goto ix74;
ie74:;
                            {
                                goto lc33;
                            }
ix74:;
                        }
                        goto ix58;
ie58:;
                        {
                            goto lc33;
                        }
ix58:;
                    }
                    goto me42;
mn43:;
                    gm_trap("no match arm matched the scrutinee", gm_src6lm8esmv16p1, 106, 5);
me42:;
                }
lc33:;
                {
                    uint64_t t109 = gm_l11;
                    uint64_t r110; bool o111 = __builtin_add_overflow(t109, ((uint64_t)1ULL), &r110);
                    uint64_t t112 = r110;
                    gm_l11 = t112;
                }
            }
lb33:;
        }
    }
    {
        void * t113 = gm_l0;
        void * t114 = GM_LD(void *, (((uint8_t *)t113) + 0));
        void * t115 = GM_LD(void *, (((uint8_t *)t114) + 0));
        uint32_t t116 = GM_LD(uint32_t, (((uint8_t *)&gm_l3) + 0));
        void * t117 = gm_f_file_o15fqku(t115, t116);
        gm_b16_8 t118 = GM_LD(gm_b16_8, (((uint8_t *)t117) + 16));
        gm_l18 = t118;
    }
    {
        gm_b16_8 t119;
        GM_ST(void *, (uint8_t *)&t119, (void *)(gm_sr4qbrk6tlgaa));
        GM_ST(uintptr_t, (uint8_t *)&t119 + 8, (uintptr_t)(0));
        gm_l19 = t119;
    }
    {
        if (!((((uint8_t *)&gm_l8))[16] != 0)) goto ie120;
        gm_b16_8 t121 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l8));
        gm_l20 = t121;
        {
            gm_b16_8 t123 = gm_l20;
            gm_l19 = t123;
        }
        goto ix120;
ie120:;
        {
            void * t126 = gm_l0;
            void * t127 = GM_LD(void *, (((uint8_t *)t126) + 0));
            uint64_t t128 = gm_l1;
            gm_b16_8 t129 = gm_l6;
            gm_tf5srh4mp4b3i t130 = gm_f_lookup_59juqoo(t127, t128, t129);
            gm_tf5srh4mp4b3i t131 = t130;
            uint8_t t132;
            if (((((uint8_t *)&t131))[8] != 0)) goto mn134;
            t132 = ((uint8_t)0);
            goto me133;
mn134:;
            t132 = ((uint8_t)1);
            goto me133;
mn135:;
            gm_trap("no match arm matched the scrutinee", gm_src6lm8esmv16p1, 125, 12);
me133:;
            if (!t132) goto ie125;
            {
                gm_tqetrgo0q24b5 t137;
                memset(&t137, 0, sizeof t137);
                uint8_t t138 = 0;
                F.ret = t137;
                goto gm_fret;
            }
            goto ix125;
ie125:;
ix125:;
        }
        {
            gm_t6j6smj7qoctu t139;
            gm_b16_8 t140 = gm_l6;
            GM_ST(gm_b16_8, (uint8_t *)&t139 + 0, t140);
            gm_b16_8 t141 = gm_l4;
            GM_ST(gm_b16_8, (uint8_t *)&t139 + 16, t141);
            gm_tabb710d9mq2e t142;
            gm_f_format_m420rhg((void *)&t142, (void *)NULL, cx, NULL, &t139);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t143 = t142;
            if (((((uint8_t *)&t143))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t143) + 8, gm_src6lm8esmv16p1, 126, 17);
            gm_b16_8 t144 = GM_LD(gm_b16_8, (((uint8_t *)&t143) + 8));
            gm_l21 = t144;
        }
        {
            gm_l22 = ((uint64_t)0ULL);
        }
        {
            if (!((((uint8_t *)&gm_l9))[4] != 0)) goto ie145;
            uint32_t t146 = GM_LD(uint32_t, ((uint8_t *)&gm_l9));
            gm_l23 = t146;
            {
                uint64_t t148 = GM_LD(uintptr_t, ((uint8_t *)&gm_l18) + 8);
                void * t149 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l18)));
                uint32_t t150 = gm_l23;
                uint64_t t151 = (uint64_t)t150;
                uint64_t t152 = (uintptr_t)t151;
                if (t152 > (uintptr_t)t148 || (uintptr_t)t148 > (uintptr_t)t148) gm_trap_slice(t152, (uintptr_t)t148, t148, gm_src6lm8esmv16p1, 130, 19);
                gm_b16_8 t153;
                GM_ST(void *, (uint8_t *)&t153, (void *)(((uint8_t *)t149 + t152 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t153 + 8, (uintptr_t)((uintptr_t)t148 - t152));
                gm_tf5srh4mp4b3i t154 = gm_f_index_of_hdeg351(t153, ((uint8_t)10ULL));
                gm_tf5srh4mp4b3i t155 = t154;
                uint64_t t156;
                if (((((uint8_t *)&t155))[8] != 0)) {
                    uint64_t t157 = GM_LD(uint64_t, (((uint8_t *)&t155) + 0));
                    t156 = t157;
                } else {
                    gm_tqetrgo0q24b5 t158;
                    memset(&t158, 0, sizeof t158);
                    uint8_t t159 = 0;
                    F.ret = t158;
                    goto gm_fret;
                }
                gm_l24 = t156;
            }
            {
                uint32_t t160 = gm_l23;
                uint64_t t161 = (uint64_t)t160;
                uint64_t t162 = gm_l24;
                uint64_t r163; bool o164 = __builtin_add_overflow(t161, t162, &r163);
                if (o164) gm_trap("integer overflow in `+`", gm_src6lm8esmv16p1, 131, 18);
                uint64_t t165 = r163;
                uint64_t r166; bool o167 = __builtin_add_overflow(t165, ((uint64_t)1ULL), &r166);
                if (o167) gm_trap("integer overflow in `+`", gm_src6lm8esmv16p1, 131, 18);
                uint64_t t168 = r166;
                gm_l22 = t168;
            }
            goto ix145;
ie145:;
            {
                for (;;) {
                    uint64_t t171 = GM_LD(uintptr_t, ((uint8_t *)&gm_l18) + 8);
                    void * t172 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l18)));
                    uint64_t t173 = gm_l22;
                    uint64_t t174 = (uintptr_t)t173;
                    if (t174 > (uintptr_t)t171 || (uintptr_t)t171 > (uintptr_t)t171) gm_trap_slice(t174, (uintptr_t)t171, t171, gm_src6lm8esmv16p1, 134, 19);
                    gm_b16_8 t175;
                    GM_ST(void *, (uint8_t *)&t175, (void *)(((uint8_t *)t172 + t174 * 1)));
                    GM_ST(uintptr_t, (uint8_t *)&t175 + 8, (uintptr_t)((uintptr_t)t171 - t174));
                    gm_b16_8 t176;
                    GM_ST(void *, (uint8_t *)&t176, (void *)(gm_si4j8sh2qika1));
                    GM_ST(uintptr_t, (uint8_t *)&t176 + 8, (uintptr_t)(2));
                    uint8_t t177 = gm_f_starts_with_6vvt01e(t175, t176);
                    if (!t177) goto lb170;
                    {
                        uint64_t t181 = GM_LD(uintptr_t, ((uint8_t *)&gm_l18) + 8);
                        void * t182 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l18)));
                        uint64_t t183 = gm_l22;
                        uint64_t t184 = (uintptr_t)t183;
                        if (t184 > (uintptr_t)t181 || (uintptr_t)t181 > (uintptr_t)t181) gm_trap_slice(t184, (uintptr_t)t181, t181, gm_src6lm8esmv16p1, 135, 26);
                        gm_b16_8 t185;
                        GM_ST(void *, (uint8_t *)&t185, (void *)(((uint8_t *)t182 + t184 * 1)));
                        GM_ST(uintptr_t, (uint8_t *)&t185 + 8, (uintptr_t)((uintptr_t)t181 - t184));
                        gm_tf5srh4mp4b3i t186 = gm_f_index_of_hdeg351(t185, ((uint8_t)10ULL));
                        gm_tf5srh4mp4b3i t187 = t186;
                        if (!((((uint8_t *)&t187))[8] != 0)) goto ie180;
                        uint64_t t188 = GM_LD(uint64_t, ((uint8_t *)&t187));
                        gm_l25 = t188;
                        {
                            uint64_t t190 = gm_l22;
                            uint64_t t191 = gm_l25;
                            uint64_t r192; bool o193 = __builtin_add_overflow(t191, ((uint64_t)1ULL), &r192);
                            if (o193) gm_trap("integer overflow in `+`", gm_src6lm8esmv16p1, 135, 60);
                            uint64_t t194 = r192;
                            uint64_t r195; bool o196 = __builtin_add_overflow(t190, t194, &r195);
                            if (o196) gm_trap("integer overflow in `+`", gm_src6lm8esmv16p1, 135, 54);
                            uint64_t t197 = r195;
                            gm_l22 = t197;
                        }
                        goto ix180;
ie180:;
                        {
                            uint64_t t199 = GM_LD(uintptr_t, ((uint8_t *)&gm_l18) + 8);
                            gm_l22 = t199;
                        }
ix180:;
                    }
lc170:;
                }
lb170:;
            }
            {
                gm_b16_8 t200;
                uint64_t t202 = gm_l22;
                uint8_t t203 = ((t202 == ((uint64_t)0ULL)));
                if (!t203) goto ie201;
                gm_b16_8 t205 = gm_l21;
                gm_tnpifv20irnph t206;
                GM_ST(gm_b16_8, ((uint8_t *)&t206 + 0), t205);
                gm_tabb710d9mq2e t207;
                gm_f_format_1u4jnnu((void *)&t207, (void *)NULL, cx, NULL, t206);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t208 = t207;
                if (((((uint8_t *)&t208))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t208) + 8, gm_src6lm8esmv16p1, 137, 33);
                gm_b16_8 t209 = GM_LD(gm_b16_8, (((uint8_t *)&t208) + 8));
                gm_b16_8 t210 = t209;
                t200 = t210;
                goto ix201;
ie201:;
                gm_b16_8 t212 = gm_l21;
                gm_tnpifv20irnph t213;
                GM_ST(gm_b16_8, ((uint8_t *)&t213 + 0), t212);
                gm_tabb710d9mq2e t214;
                gm_f_format_8hego7t((void *)&t214, (void *)NULL, cx, NULL, t213);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t215 = t214;
                if (((((uint8_t *)&t215))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t215) + 8, gm_src6lm8esmv16p1, 137, 68);
                gm_b16_8 t216 = GM_LD(gm_b16_8, (((uint8_t *)&t215) + 8));
                gm_b16_8 t217 = t216;
                t200 = t217;
ix201:;
                gm_l21 = t200;
            }
ix145:;
        }
        {
            void * t218 = (void *)((uint8_t *)&F.l7);
            gm_tbgd14esf0jvi t219;
            memset((uint8_t *)&t219 + 12, 0, 4);
            uint32_t t220 = GM_LD(uint32_t, (((uint8_t *)&gm_l3) + 0));
            uint64_t t221 = gm_l22;
            uint64_t t222 = gm_l22;
            gm_tl874t2atnlh3 t223 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_64igq5o(t220, t221, t222));
            GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t219 + 0, t223);
            gm_b16_8 t224 = gm_l21;
            gm_b16_8 t225 = t224;
            gm_b16_8 t226 = GM_LD(gm_b16_8, ((uint8_t *)&t225));
            GM_ST(gm_b16_8, (uint8_t *)&t219 + 16, t226);
            gm_tftb1odc739t5 t227 = gm_f_push_k96mobn(t218, &t219);
            gm_tftb1odc739t5 t228 = t227;
            if (((((uint8_t *)&t228))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t228) + 1, gm_src6lm8esmv16p1, 139, 9);
            gm_b0_1 t229 = GM_LD(gm_b0_1, (((uint8_t *)&t228) + 1));
        }
        {
            gm_b16_8 t230 = gm_l6;
            gm_l19 = t230;
        }
ix120:;
    }
    {
        void * t231 = (void *)((uint8_t *)&F.l7);
        gm_tbgd14esf0jvi t232;
        memset((uint8_t *)&t232 + 12, 0, 4);
        gm_tl874t2atnlh3 t233 = gm_l3;
        GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t232 + 0, t233);
        gm_t6j6smj7qoctu t234;
        gm_b16_8 t235 = gm_l19;
        GM_ST(gm_b16_8, (uint8_t *)&t234 + 0, t235);
        gm_b16_8 t236 = gm_l2;
        GM_ST(gm_b16_8, (uint8_t *)&t234 + 16, t236);
        gm_tabb710d9mq2e t237;
        gm_f_format_ahgk48j((void *)&t237, (void *)NULL, cx, NULL, &t234);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t238 = t237;
        if (((((uint8_t *)&t238))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t238) + 8, gm_src6lm8esmv16p1, 142, 44);
        gm_b16_8 t239 = GM_LD(gm_b16_8, (((uint8_t *)&t238) + 8));
        gm_b16_8 t240 = t239;
        gm_b16_8 t241 = GM_LD(gm_b16_8, ((uint8_t *)&t240));
        GM_ST(gm_b16_8, (uint8_t *)&t232 + 16, t241);
        gm_tftb1odc739t5 t242 = gm_f_push_k96mobn(t231, &t232);
        gm_tftb1odc739t5 t243 = t242;
        if (((((uint8_t *)&t243))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t243) + 1, gm_src6lm8esmv16p1, 142, 5);
        gm_b0_1 t244 = GM_LD(gm_b0_1, (((uint8_t *)&t243) + 1));
    }
    {
        gm_trqfvmjtt4sij t245 = F.l7;
        gm_tqetrgo0q24b5 t246;
        GM_ST(uint8_t, (uint8_t *)&t246 + 41, 0);
        GM_ST(uint16_t, (uint8_t *)&t246 + 42, 0);
        GM_ST(uint32_t, (uint8_t *)&t246 + 44, 0);
        GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t246), t245);
        ((uint8_t *)&t246)[40] = 1;
        uint8_t t247 = 0;
        F.ret = t246;
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_decl_used_oldf2ja(void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_decl_used_oldf2ja F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* d */
    GM_STACK_CHECK(&F, 77u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint8_t t1 = 0;
    uint64_t t2 = gm_l1;
    void * t3 = gm_l0;
    uint64_t t4 = GM_LD(uintptr_t, ((((uint8_t *)t3) + 2544) + 0) + 8);
    uint8_t t5 = (t2 < t4);
    if (t5) {
        uint64_t t6 = gm_l1;
        uint64_t t7 = (uintptr_t)t6;
        void * t8 = gm_l0;
        uint64_t t9 = GM_LD(uintptr_t, ((((uint8_t *)t8) + 2544) + 0) + 8);
        if (t7 >= (uintptr_t)(t9)) gm_trap_index(t7, t9, gm_src6lm8esmv16p1, 61, 91);
        uint8_t t10 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t8) + 2544) + 0)) + t7 * 1));
        t1 = t10;
    } else t1 = 0;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_used_outside_tbk81ks(void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_used_outside_tbk81ks F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* d */
    GM_STACK_CHECK(&F, 77u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint8_t t1 = 0;
    uint64_t t2 = gm_l1;
    void * t3 = gm_l0;
    uint64_t t4 = GM_LD(uintptr_t, ((((uint8_t *)t3) + 2584) + 0) + 8);
    uint8_t t5 = (t2 < t4);
    if (t5) {
        uint64_t t6 = gm_l1;
        uint64_t t7 = (uintptr_t)t6;
        void * t8 = gm_l0;
        uint64_t t9 = GM_LD(uintptr_t, ((((uint8_t *)t8) + 2584) + 0) + 8);
        if (t7 >= (uintptr_t)(t9)) gm_trap_index(t7, t9, gm_src6lm8esmv16p1, 64, 97);
        uint8_t t10 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t8) + 2584) + 0)) + t7 * 1));
        t1 = t10;
    } else t1 = 0;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_lookup_module_name_cukfp7t(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2) {
    struct gm_fr_lookup_module_name_cukfp7t F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* module */
    gm_b16_8 gm_l2; /* name */
    uint64_t gm_l3; /* d */
    uint64_t gm_l4; /* prelude */
    uint64_t gm_l5; /* d */
    void * gm_l6; /* m */
    GM_STACK_CHECK(&F, 732u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t3 = gm_l0;
        void * t4 = GM_LD(void *, (((uint8_t *)t3) + 0));
        uint64_t t5 = gm_l1;
        gm_b16_8 t6 = gm_l2;
        gm_tf5srh4mp4b3i t7 = gm_f_lookup_59juqoo(t4, t5, t6);
        gm_tf5srh4mp4b3i t8 = t7;
        if (!((((uint8_t *)&t8))[8] != 0)) goto ie2;
        uint64_t t9 = GM_LD(uint64_t, ((uint8_t *)&t8));
        gm_l3 = t9;
        {
            void * t11 = gm_l0;
            uint64_t t12 = gm_l3;
            gm_b0_1 t13 = gm_f_note_decl_use_ui57s3b((void *)NULL, cx, NULL, t11, t12);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            uint64_t t14 = gm_l3;
            gm_tf5srh4mp4b3i t15;
            GM_ST(uint8_t, (uint8_t *)&t15 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t15 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t15 + 12, 0);
            GM_ST(uint64_t, ((uint8_t *)&t15), t14);
            ((uint8_t *)&t15)[8] = 1;
            uint8_t t16 = 0;
            F.ret = t15;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t17 = gm_l0;
        void * t18 = GM_LD(void *, (((uint8_t *)t17) + 0));
        gm_tf5srh4mp4b3i t19 = GM_LD(gm_tf5srh4mp4b3i, (((uint8_t *)t18) + 320));
        gm_tf5srh4mp4b3i t20 = t19;
        uint64_t t21;
        if (((((uint8_t *)&t20))[8] != 0)) {
            uint64_t t22 = GM_LD(uint64_t, (((uint8_t *)&t20) + 0));
            t21 = t22;
        } else {
            gm_tf5srh4mp4b3i t23;
            memset(&t23, 0, sizeof t23);
            uint8_t t24 = 0;
            F.ret = t23;
            goto gm_fret;
        }
        gm_l4 = t21;
    }
    {
        void * t25 = gm_l0;
        void * t26 = GM_LD(void *, (((uint8_t *)t25) + 0));
        uint64_t t27 = gm_l4;
        gm_b16_8 t28 = gm_l2;
        gm_tf5srh4mp4b3i t29 = gm_f_lookup_59juqoo(t26, t27, t28);
        gm_tf5srh4mp4b3i t30 = t29;
        uint64_t t31;
        if (((((uint8_t *)&t30))[8] != 0)) {
            uint64_t t32 = GM_LD(uint64_t, (((uint8_t *)&t30) + 0));
            t31 = t32;
        } else {
            gm_tf5srh4mp4b3i t33;
            memset(&t33, 0, sizeof t33);
            uint8_t t34 = 0;
            F.ret = t33;
            goto gm_fret;
        }
        gm_l5 = t31;
    }
    {
        uint64_t t35 = gm_l1;
        uint64_t t36 = (uintptr_t)t35;
        void * t37 = gm_l0;
        void * t38 = GM_LD(void *, (((uint8_t *)t37) + 0));
        uint64_t t39 = GM_LD(uintptr_t, ((((uint8_t *)t38) + 200) + 0) + 8);
        if (t36 >= (uintptr_t)(t39)) gm_trap_index(t36, t39, gm_src6lm8esmv16p1, 31, 11);
        void * t40 = (void *)((uint8_t *)GM_LD(void *, ((((uint8_t *)t38) + 200) + 0)) + t36 * 184);
        gm_l6 = t40;
    }
    {
        void * t42 = gm_l6;
        uint8_t t43 = GM_LD(uint8_t, (((uint8_t *)t42) + 112));
        if (!t43) goto ie41;
        {
            uint64_t t45 = gm_l5;
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
        goto ix41;
ie41:;
ix41:;
    }
    {
        uint64_t t49 = gm_l5;
        uint64_t t50 = (uintptr_t)t49;
        void * t51 = gm_l0;
        void * t52 = GM_LD(void *, (((uint8_t *)t51) + 0));
        uint64_t t53 = GM_LD(uintptr_t, ((((uint8_t *)t52) + 240) + 0) + 8);
        if (t50 >= (uintptr_t)(t53)) gm_trap_index(t50, t53, gm_src6lm8esmv16p1, 33, 8);
        uint8_t t54 = (!(GM_LD(uint8_t, ((((uint8_t *)GM_LD(void *, ((((uint8_t *)t52) + 240) + 0)) + t50 * 216) + 40) + 56) + 0) == ((uint8_t)2ULL)));
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
ix48:;
    }
    {
        uint8_t t59 = (((GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8) == 4) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l2)), gm_st3p5nb1e6cc8, 4) == 0));
        if (!t59) goto ie58;
        {
            gm_tf5srh4mp4b3i t61;
            void * t63 = gm_l6;
            uint8_t t64 = GM_LD(uint8_t, (((uint8_t *)t63) + 113));
            if (!t64) goto ie62;
            uint64_t t66 = gm_l5;
            uint64_t t67 = t66;
            gm_tf5srh4mp4b3i t68;
            GM_ST(uint8_t, (uint8_t *)&t68 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t68 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t68 + 12, 0);
            GM_ST(uint64_t, ((uint8_t *)&t68), t67);
            ((uint8_t *)&t68)[8] = 1;
            t61 = t68;
            goto ix62;
ie62:;
            gm_tf5srh4mp4b3i t70;
            memset(&t70, 0, sizeof t70);
            gm_tf5srh4mp4b3i t71 = t70;
            t61 = t71;
ix62:;
            uint8_t t72 = 0;
            F.ret = t61;
            goto gm_fret;
        }
        goto ix58;
ie58:;
ix58:;
    }
    {
        gm_tf5srh4mp4b3i t73;
        void * t75 = gm_l0;
        uint64_t t76 = gm_l5;
        uint8_t t77 = gm_f_universe_hidden_gsk2b8r(t75, t76);
        if (!t77) goto ie74;
        gm_tf5srh4mp4b3i t79;
        memset(&t79, 0, sizeof t79);
        gm_tf5srh4mp4b3i t80 = t79;
        t73 = t80;
        goto ix74;
ie74:;
        uint64_t t82 = gm_l5;
        uint64_t t83 = t82;
        gm_tf5srh4mp4b3i t84;
        GM_ST(uint8_t, (uint8_t *)&t84 + 9, 0);
        GM_ST(uint16_t, (uint8_t *)&t84 + 10, 0);
        GM_ST(uint32_t, (uint8_t *)&t84 + 12, 0);
        GM_ST(uint64_t, ((uint8_t *)&t84), t83);
        ((uint8_t *)&t84)[8] = 1;
        t73 = t84;
ix74:;
        uint8_t t85 = 0;
        F.ret = t73;
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
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_module_member_5fd2qm9(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, const void * a2, uint64_t a3) {
    struct gm_fr_module_member_5fd2qm9 F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* m */
    gm_t08olq97rscn3 gm_l2; /* name */
    uint64_t gm_l3; /* from */
    gm_b16_8 gm_l4; /* module_name */
    uint64_t gm_l5; /* d */
    gm_b16_8 gm_l6; /* effect_ */
    GM_STACK_CHECK(&F, 1591u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l3 = a3;
    {
        uint64_t t2 = gm_l1;
        uint64_t t3 = (uintptr_t)t2;
        void * t4 = gm_l0;
        void * t5 = GM_LD(void *, (((uint8_t *)t4) + 0));
        uint64_t t6 = GM_LD(uintptr_t, ((((uint8_t *)t5) + 200) + 0) + 8);
        if (t3 >= (uintptr_t)(t6)) gm_trap_index(t3, t6, gm_src6lm8esmv16p1, 193, 20);
        gm_b16_8 t7 = GM_LD(gm_b16_8, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t5) + 200) + 0)) + t3 * 184) + 0));
        gm_l4 = t7;
    }
    {
        void * t9 = gm_l0;
        void * t10 = GM_LD(void *, (((uint8_t *)t9) + 0));
        uint64_t t11 = gm_l1;
        gm_b16_8 t12 = GM_LD(gm_b16_8, (((uint8_t *)a2) + 0));
        gm_tf5srh4mp4b3i t13 = gm_f_lookup_59juqoo(t10, t11, t12);
        gm_tf5srh4mp4b3i t14 = t13;
        if (!((((uint8_t *)&t14))[8] != 0)) goto ie8;
        uint64_t t15 = GM_LD(uint64_t, ((uint8_t *)&t14));
        gm_l5 = t15;
        {
            void * t18 = gm_l0;
            uint64_t t19 = gm_l5;
            uint64_t t20 = gm_l3;
            uint8_t t21 = gm_f_decl_visible_bd168ti(t18, t19, t20);
            if (!t21) goto ie17;
            {
                void * t23 = gm_l0;
                uint64_t t24 = gm_l5;
                gm_b0_1 t25 = gm_f_note_decl_use_ui57s3b((void *)NULL, cx, NULL, t23, t24);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t26 = gm_l0;
                uint64_t t27 = gm_l5;
                uint64_t t28 = gm_l3;
                gm_b0_1 t29 = gm_f_note_use_from_2lmujsq((void *)NULL, cx, NULL, t26, t27, t28);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t30 = gm_l0;
                uint64_t t31 = gm_l5;
                gm_tl874t2atnlh3 t32 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)a2) + 16));
                gm_b0_1 t33 = gm_f_check_deprecated_j3dq26t((void *)NULL, cx, NULL, t30, t31, t32);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                uint64_t t34 = gm_l5;
                gm_tf5srh4mp4b3i t35;
                GM_ST(uint8_t, (uint8_t *)&t35 + 9, 0);
                GM_ST(uint16_t, (uint8_t *)&t35 + 10, 0);
                GM_ST(uint32_t, (uint8_t *)&t35 + 12, 0);
                GM_ST(uint64_t, ((uint8_t *)&t35), t34);
                ((uint8_t *)&t35)[8] = 1;
                uint8_t t36 = 0;
                F.ret = t35;
                goto gm_fret;
            }
            goto ix17;
ie17:;
ix17:;
        }
        {
            uint64_t t38 = gm_l5;
            uint64_t t39 = (uintptr_t)t38;
            void * t40 = gm_l0;
            void * t41 = GM_LD(void *, (((uint8_t *)t40) + 0));
            uint64_t t42 = GM_LD(uintptr_t, ((((uint8_t *)t41) + 240) + 0) + 8);
            if (t39 >= (uintptr_t)(t42)) gm_trap_index(t39, t42, gm_src6lm8esmv16p1, 201, 12);
            uint8_t t43 = ((GM_LD(uint8_t, ((((uint8_t *)GM_LD(void *, ((((uint8_t *)t41) + 240) + 0)) + t39 * 216) + 40) + 56) + 0) == ((uint8_t)1ULL)));
            if (!t43) goto ie37;
            {
                void * t45 = gm_l0;
                gm_tl874t2atnlh3 t46 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)a2) + 16));
                gm_t6j6smj7qoctu t47;
                gm_b16_8 t48 = GM_LD(gm_b16_8, (((uint8_t *)a2) + 0));
                GM_ST(gm_b16_8, (uint8_t *)&t47 + 0, t48);
                gm_b16_8 t49 = gm_l4;
                GM_ST(gm_b16_8, (uint8_t *)&t47 + 16, t49);
                gm_tabb710d9mq2e t50;
                gm_f_format_adbd97i((void *)&t50, (void *)NULL, cx, NULL, &t47);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t51 = t50;
                if (((((uint8_t *)&t51))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t51) + 8, gm_src6lm8esmv16p1, 202, 32);
                gm_b16_8 t52 = GM_LD(gm_b16_8, (((uint8_t *)&t51) + 8));
                gm_b16_8 t53 = t52;
                gm_b16_8 t54 = GM_LD(gm_b16_8, ((uint8_t *)&t53));
                gm_b0_1 t55 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t45, t46, t54);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix37;
ie37:;
            {
                void * t57 = gm_l0;
                gm_tl874t2atnlh3 t58 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)a2) + 16));
                gm_t6j6smj7qoctu t59;
                gm_b16_8 t60 = GM_LD(gm_b16_8, (((uint8_t *)a2) + 0));
                GM_ST(gm_b16_8, (uint8_t *)&t59 + 0, t60);
                gm_b16_8 t61 = gm_l4;
                GM_ST(gm_b16_8, (uint8_t *)&t59 + 16, t61);
                gm_tabb710d9mq2e t62;
                gm_f_format_vlsb354((void *)&t62, (void *)NULL, cx, NULL, &t59);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t63 = t62;
                if (((((uint8_t *)&t63))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t63) + 8, gm_src6lm8esmv16p1, 204, 32);
                gm_b16_8 t64 = GM_LD(gm_b16_8, (((uint8_t *)&t63) + 8));
                gm_b16_8 t65 = t64;
                gm_b16_8 t66 = GM_LD(gm_b16_8, ((uint8_t *)&t65));
                gm_b0_1 t67 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t57, t58, t66);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
ix37:;
        }
        {
            gm_tf5srh4mp4b3i t68;
            memset(&t68, 0, sizeof t68);
            uint8_t t69 = 0;
            F.ret = t68;
            goto gm_fret;
        }
        goto ix8;
ie8:;
ix8:;
    }
    {
        void * t71 = gm_l0;
        uint64_t t72 = gm_l1;
        gm_b16_8 t73 = GM_LD(gm_b16_8, (((uint8_t *)a2) + 0));
        gm_th2lqbo77pvsc t74;
        gm_f_public_op_effect_72d4rbe((void *)&t74, t71, t72, t73);
        gm_th2lqbo77pvsc t75 = t74;
        if (!((((uint8_t *)&t75))[16] != 0)) goto ie70;
        gm_b16_8 t76 = GM_LD(gm_b16_8, ((uint8_t *)&t75));
        gm_l6 = t76;
        {
            void * t78 = gm_l0;
            gm_tl874t2atnlh3 t79 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)a2) + 16));
            gm_b16_8 t80 = gm_l6;
            gm_b16_8 t81 = GM_LD(gm_b16_8, (((uint8_t *)a2) + 0));
            gm_b16_8 t82 = gm_l4;
            gm_th2lqbo77pvsc t83;
            GM_ST(uint8_t, (uint8_t *)&t83 + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&t83 + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&t83 + 20, 0);
            GM_ST(gm_b16_8, ((uint8_t *)&t83), t82);
            ((uint8_t *)&t83)[16] = 1;
            gm_b16_8 t84 = gm_f_op_hint_rfghuf5((void *)NULL, cx, NULL, t80, t81, &t83);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_b0_1 t85 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t78, t79, t84);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_tf5srh4mp4b3i t86;
            memset(&t86, 0, sizeof t86);
            uint8_t t87 = 0;
            F.ret = t86;
            goto gm_fret;
        }
        goto ix70;
ie70:;
ix70:;
    }
    {
        gm_tl874t2atnlh3 t88 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)a2) + 16));
        gm_t6j6smj7qoctu t89;
        gm_b16_8 t90 = gm_l4;
        GM_ST(gm_b16_8, (uint8_t *)&t89 + 0, t90);
        gm_b16_8 t91 = GM_LD(gm_b16_8, (((uint8_t *)a2) + 0));
        GM_ST(gm_b16_8, (uint8_t *)&t89 + 16, t91);
        gm_tabb710d9mq2e t92;
        gm_f_format_1o1mt78((void *)&t92, (void *)NULL, cx, NULL, &t89);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t93 = t92;
        if (((((uint8_t *)&t93))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t93) + 8, gm_src6lm8esmv16p1, 212, 32);
        gm_b16_8 t94 = GM_LD(gm_b16_8, (((uint8_t *)&t93) + 8));
        gm_b16_8 t95 = t94;
        gm_b16_8 t96 = GM_LD(gm_b16_8, ((uint8_t *)&t95));
        gm_f_error_s6hn871((void *)&F.l7, (void *)NULL, cx, NULL, t88, t96);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        uint8_t t98 = 0;
        gm_b16_8 t99 = GM_LD(gm_b16_8, (((uint8_t *)a2) + 0));
        void * t100 = ((uint8_t *)gm_c55218gtcr0);
        if ((uintptr_t)0 > (uintptr_t)28 || (uintptr_t)28 > (uintptr_t)28) gm_trap_slice((uintptr_t)0, (uintptr_t)28, 28, gm_src6lm8esmv16p1, 214, 21);
        gm_b16_8 t101;
        GM_ST(void *, (uint8_t *)&t101, (void *)(((uint8_t *)t100 + (uintptr_t)0 * 16)));
        GM_ST(uintptr_t, (uint8_t *)&t101 + 8, (uintptr_t)((uintptr_t)28 - (uintptr_t)0));
        gm_b16_8 t102 = t101;
        uint8_t t103 = 0;
        gm_b16_8 t104 = t99;
        for (uintptr_t i = 0; i < (uintptr_t)(GM_LD(uintptr_t, ((uint8_t *)&t102) + 8)); i++) if (gm_eq0iqiubfm7e(((uint8_t *)GM_LD(void *, ((uint8_t *)&t102))) + i * 16, ((uint8_t *)&t104))) { t103 = 1; break; }
        uint8_t t105 = (uint8_t)(t103);
        if (!t105) {
            gm_b16_8 t106 = GM_LD(gm_b16_8, (((uint8_t *)a2) + 0));
            gm_t6j6smj7qoctu t107;
            gm_b16_8 t108;
            GM_ST(void *, (uint8_t *)&t108, (void *)(gm_snivf25e5fme8));
            GM_ST(uintptr_t, (uint8_t *)&t108 + 8, (uintptr_t)(5));
            GM_ST(gm_b16_8, (uint8_t *)&t107 + 0, t108);
            gm_b16_8 t109;
            GM_ST(void *, (uint8_t *)&t109, (void *)(gm_s4pmpk07ugvq6));
            GM_ST(uintptr_t, (uint8_t *)&t109 + 8, (uintptr_t)(4));
            GM_ST(gm_b16_8, (uint8_t *)&t107 + 16, t109);
            gm_t6j6smj7qoctu t110 = t107;
            uint8_t t111 = 0;
            gm_b16_8 t112 = t106;
            if (gm_eq0iqiubfm7e(((uint8_t *)&t110) + 0, ((uint8_t *)&t112))) t111 = 1;
            if (gm_eq0iqiubfm7e(((uint8_t *)&t110) + 16, ((uint8_t *)&t112))) t111 = 1;
            uint8_t t113 = (uint8_t)(t111);
            t98 = t113;
        } else t98 = 1;
        if (!t98) goto ie97;
        {
            void * t115 = (void *)((uint8_t *)&F.l7);
            gm_t6j6smj7qoctu t116;
            gm_b16_8 t117 = GM_LD(gm_b16_8, (((uint8_t *)a2) + 0));
            GM_ST(gm_b16_8, (uint8_t *)&t116 + 0, t117);
            gm_b16_8 t118 = GM_LD(gm_b16_8, (((uint8_t *)a2) + 0));
            GM_ST(gm_b16_8, (uint8_t *)&t116 + 16, t118);
            gm_tabb710d9mq2e t119;
            gm_f_format_p16u9uu((void *)&t119, (void *)NULL, cx, NULL, &t116);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t120 = t119;
            if (((((uint8_t *)&t120))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t120) + 8, gm_src6lm8esmv16p1, 215, 20);
            gm_b16_8 t121 = GM_LD(gm_b16_8, (((uint8_t *)&t120) + 8));
            gm_b16_8 t122 = t121;
            gm_b16_8 t123 = GM_LD(gm_b16_8, ((uint8_t *)&t122));
            gm_f_add_note_46v502s(t115, t123);
        }
        goto ix97;
ie97:;
ix97:;
    }
    {
        void * t124 = gm_l0;
        gm_b0_1 t125 = gm_f_emit_dph5v1e((void *)NULL, cx, NULL, t124, &(*(gm_t4gut2c9fj1kt *)((uint8_t *)&F.l7)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_tf5srh4mp4b3i t126;
        memset(&t126, 0, sizeof t126);
        uint8_t t127 = 0;
        F.ret = t126;
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
inline __attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_module_named_n1mcl3b(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2) {
    struct gm_fr_module_named_n1mcl3b F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* module */
    void * gm_l2; /* e */
    void * gm_l3; /* base */
    gm_t08olq97rscn3 gm_l4; /* name */
    uint64_t gm_l5; /* m */
    gm_t08olq97rscn3 gm_l6; /* id */
    uint64_t gm_l7; /* d */
    uint64_t gm_l8; /* m */
    GM_STACK_CHECK(&F, 985u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t3 = gm_l2;
        uint8_t t4 = GM_LD(uint8_t, ((((uint8_t *)t3) + 8) + 0));
        if (t4 != ((uint8_t)22ULL)) goto ie2;
        void * t5 = GM_LD(void *, (((((uint8_t *)t3) + 8) + 8) + 0));
        gm_l3 = t5;
        gm_t08olq97rscn3 t6 = GM_LD(gm_t08olq97rscn3, (((((uint8_t *)t3) + 8) + 8) + 8));
        gm_l4 = t6;
        {
            void * t8 = gm_l0;
            uint64_t t9 = gm_l1;
            void * t10 = gm_l3;
            gm_tf5srh4mp4b3i t11 = gm_f_module_named_n1mcl3b((void *)NULL, cx, NULL, t8, t9, t10);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tf5srh4mp4b3i t12 = t11;
            uint64_t t13;
            if (((((uint8_t *)&t12))[8] != 0)) {
                uint64_t t14 = GM_LD(uint64_t, (((uint8_t *)&t12) + 0));
                t13 = t14;
            } else {
                gm_tf5srh4mp4b3i t15;
                memset(&t15, 0, sizeof t15);
                uint8_t t16 = 0;
                F.ret = t15;
                goto gm_fret;
            }
            gm_l5 = t13;
        }
        {
            void * t17 = gm_l0;
            uint64_t t18 = gm_l5;
            gm_b16_8 t19 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l4) + 0));
            uint64_t t20 = gm_l1;
            gm_tf5srh4mp4b3i t21 = gm_f_public_module_member_e2i0at7((void *)NULL, cx, NULL, t17, t18, t19, t20);
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
        void * t24 = gm_l2;
        uint8_t t25 = GM_LD(uint8_t, ((((uint8_t *)t24) + 8) + 0));
        if (t25 != ((uint8_t)6ULL)) goto ie23;
        gm_t08olq97rscn3 t26 = GM_LD(gm_t08olq97rscn3, ((((uint8_t *)t24) + 8) + 8));
        gm_l6 = t26;
        {
            void * t28 = gm_l0;
            uint64_t t29 = gm_l1;
            gm_b16_8 t30 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l6) + 0));
            gm_tf5srh4mp4b3i t31 = gm_f_lookup_module_name_cukfp7t((void *)NULL, cx, NULL, t28, t29, t30);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tf5srh4mp4b3i t32 = t31;
            uint64_t t33;
            if (((((uint8_t *)&t32))[8] != 0)) {
                uint64_t t34 = GM_LD(uint64_t, (((uint8_t *)&t32) + 0));
                t33 = t34;
            } else {
                gm_tf5srh4mp4b3i t35;
                memset(&t35, 0, sizeof t35);
                uint8_t t36 = 0;
                F.ret = t35;
                goto gm_fret;
            }
            gm_l7 = t33;
        }
        {
            void * t38 = gm_l0;
            uint64_t t39 = gm_l7;
            gm_t6qooom8qa2do t40;
            gm_f_resolve_decl_pus042v((void *)&t40, (void *)NULL, cx, NULL, t38, t39);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t6qooom8qa2do t41 = t40;
            uint8_t t42 = GM_LD(uint8_t, (((uint8_t *)&t41) + 0));
            if (t42 != ((uint8_t)9ULL)) goto ie37;
            uint64_t t43 = GM_LD(uint64_t, (((uint8_t *)&t41) + 8));
            gm_l8 = t43;
            {
                uint64_t t45 = gm_l8;
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
            goto ix37;
ie37:;
            {
                gm_tf5srh4mp4b3i t49;
                memset(&t49, 0, sizeof t49);
                uint8_t t50 = 0;
                F.ret = t49;
                goto gm_fret;
            }
ix37:;
        }
        goto ix23;
ie23:;
        {
            gm_tf5srh4mp4b3i t52;
            memset(&t52, 0, sizeof t52);
            uint8_t t53 = 0;
            F.ret = t52;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_note_decl_use_ui57s3b(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1) {
    struct gm_fr_note_decl_use_ui57s3b F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* d */
    GM_STACK_CHECK(&F, 93u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        for (;;) {
            void * t3 = gm_l0;
            uint64_t t4 = GM_LD(uintptr_t, ((((uint8_t *)t3) + 2544) + 0) + 8);
            uint64_t t5 = gm_l1;
            uint8_t t6 = (t4 <= t5);
            if (!t6) goto lb2;
            {
                void * t9 = gm_l0;
                void * t10 = (void *)(((uint8_t *)t9) + 2544);
                gm_tftb1odc739t5 t11 = gm_f_push_vd1290p(t10, ((uint8_t)0));
                gm_tftb1odc739t5 t12 = t11;
                if (((((uint8_t *)&t12))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t12) + 1, gm_src6lm8esmv16p1, 41, 40);
                gm_b0_1 t13 = GM_LD(gm_b0_1, (((uint8_t *)&t12) + 1));
            }
lc2:;
        }
lb2:;
    }
    {
        uint64_t t14 = gm_l1;
        uint64_t t15 = (uintptr_t)t14;
        void * t16 = gm_l0;
        uint64_t t17 = GM_LD(uintptr_t, ((((uint8_t *)t16) + 2544) + 0) + 8);
        if (t15 >= (uintptr_t)(t17)) gm_trap_index(t15, t17, gm_src6lm8esmv16p1, 42, 5);
        GM_ST(uint8_t, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t16) + 2544) + 0)) + t15 * 1), ((uint8_t)1));
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_note_local_use_h4lf1jh(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_tl874t2atnlh3 a2) {
    struct gm_fr_note_local_use_h4lf1jh F;
    void * gm_l0; /* s */
    gm_tl874t2atnlh3 gm_l1; /* decl */
    gm_tl874t2atnlh3 gm_l2; /* at */
    uint32_t gm_l3; /* n */
    GM_STACK_CHECK(&F, 262u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t3 = gm_l0;
        void * t4 = (void *)(((uint8_t *)t3) + 2624);
        gm_t2p74ki8sl4an t5;
        gm_tl874t2atnlh3 t6 = gm_l1;
        GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t5 + 0, t6);
        gm_tl874t2atnlh3 t7 = gm_l2;
        GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t5 + 12, t7);
        uint8_t t8 = gm_f_contains_a8d6g03(t4, &t5);
        if (!t8) goto ie2;
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
        void * t12 = gm_l0;
        void * t13 = (void *)(((uint8_t *)t12) + 2624);
        gm_t2p74ki8sl4an t14;
        gm_tl874t2atnlh3 t15 = gm_l1;
        GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t14 + 0, t15);
        gm_tl874t2atnlh3 t16 = gm_l2;
        GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t14 + 12, t16);
        gm_tftb1odc739t5 t17 = gm_f_put_h767iu8(t13, &t14, ((uint8_t)1));
        gm_tftb1odc739t5 t18 = t17;
        if (((((uint8_t *)&t18))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t18) + 1, gm_src6lm8esmv16p1, 55, 5);
        gm_b0_1 t19 = GM_LD(gm_b0_1, (((uint8_t *)&t18) + 1));
    }
    {
        void * t20 = gm_l0;
        void * t21 = (void *)(((uint8_t *)t20) + 2680);
        gm_tl874t2atnlh3 t22 = gm_l1;
        gm_t9805qtonbsag t23 = gm_f_get_rlnefnq(t21, GM_CONV(gm_cseahvg8v7p0vs, t22));
        gm_t9805qtonbsag t24 = t23;
        uint32_t t25;
        if (((((uint8_t *)&t24))[4] != 0)) {
            uint32_t t26 = GM_LD(uint32_t, (((uint8_t *)&t24) + 0));
            t25 = t26;
        } else {
            t25 = ((uint32_t)0ULL);
        }
        gm_l3 = t25;
    }
    {
        void * t27 = gm_l0;
        void * t28 = (void *)(((uint8_t *)t27) + 2680);
        gm_tl874t2atnlh3 t29 = gm_l1;
        uint32_t t30 = gm_l3;
        uint32_t r31; bool o32 = __builtin_add_overflow(t30, ((uint32_t)1ULL), &r31);
        if (o32) gm_trap("integer overflow in `+`", gm_src6lm8esmv16p1, 57, 34);
        uint32_t t33 = r31;
        gm_tftb1odc739t5 t34 = gm_f_put_3e4k1js(t28, GM_CONV(gm_cseahvg8v7p0vs, t29), t33);
        gm_tftb1odc739t5 t35 = t34;
        if (((((uint8_t *)&t35))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t35) + 1, gm_src6lm8esmv16p1, 57, 5);
        gm_b0_1 t36 = GM_LD(gm_b0_1, (((uint8_t *)&t35) + 1));
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_note_use_from_2lmujsq(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, uint64_t a2) {
    struct gm_fr_note_use_from_2lmujsq F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* d */
    uint64_t gm_l2; /* from */
    GM_STACK_CHECK(&F, 159u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        uint64_t t3 = gm_l1;
        uint64_t t4 = (uintptr_t)t3;
        void * t5 = gm_l0;
        void * t6 = GM_LD(void *, (((uint8_t *)t5) + 0));
        uint64_t t7 = GM_LD(uintptr_t, ((((uint8_t *)t6) + 240) + 0) + 8);
        if (t4 >= (uintptr_t)(t7)) gm_trap_index(t4, t7, gm_src6lm8esmv16p1, 47, 8);
        uint64_t t8 = GM_LD(uint64_t, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t6) + 240) + 0)) + t4 * 216) + 32));
        uint64_t t9 = gm_l2;
        uint8_t t10 = ((t8 == t9));
        if (!t10) goto ie2;
        {
            gm_b0_1 t12;
            uint8_t t13 = 0;
            F.ret = t12;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        for (;;) {
            void * t15 = gm_l0;
            uint64_t t16 = GM_LD(uintptr_t, ((((uint8_t *)t15) + 2584) + 0) + 8);
            uint64_t t17 = gm_l1;
            uint8_t t18 = (t16 <= t17);
            if (!t18) goto lb14;
            {
                void * t21 = gm_l0;
                void * t22 = (void *)(((uint8_t *)t21) + 2584);
                gm_tftb1odc739t5 t23 = gm_f_push_vd1290p(t22, ((uint8_t)0));
                gm_tftb1odc739t5 t24 = t23;
                if (((((uint8_t *)&t24))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t24) + 1, gm_src6lm8esmv16p1, 48, 43);
                gm_b0_1 t25 = GM_LD(gm_b0_1, (((uint8_t *)&t24) + 1));
            }
lc14:;
        }
lb14:;
    }
    {
        uint64_t t26 = gm_l1;
        uint64_t t27 = (uintptr_t)t26;
        void * t28 = gm_l0;
        uint64_t t29 = GM_LD(uintptr_t, ((((uint8_t *)t28) + 2584) + 0) + 8);
        if (t27 >= (uintptr_t)(t29)) gm_trap_index(t27, t29, gm_src6lm8esmv16p1, 49, 5);
        GM_ST(uint8_t, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t28) + 2584) + 0)) + t27 * 1), ((uint8_t)1));
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_op_effect_n2jr58f(void *gm_out, void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_op_effect_n2jr58f F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* module */
    gm_b16_8 gm_l2; /* name */
    gm_b16_8 gm_l3; /* e */
    uint64_t gm_l4; /* prelude */
    GM_STACK_CHECK(&F, 314u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t3 = gm_l0;
        uint64_t t4 = gm_l1;
        gm_b16_8 t5 = gm_l2;
        gm_th2lqbo77pvsc t6;
        gm_f_effect_with_op_ol1t6ca((void *)&t6, t3, t4, t5, ((uint8_t)0));
        gm_th2lqbo77pvsc t7 = t6;
        if (!((((uint8_t *)&t7))[16] != 0)) goto ie2;
        gm_b16_8 t8 = GM_LD(gm_b16_8, ((uint8_t *)&t7));
        gm_l3 = t8;
        {
            gm_b16_8 t10 = gm_l3;
            gm_th2lqbo77pvsc t11;
            GM_ST(uint8_t, (uint8_t *)&t11 + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&t11 + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&t11 + 20, 0);
            GM_ST(gm_b16_8, ((uint8_t *)&t11), t10);
            ((uint8_t *)&t11)[16] = 1;
            uint8_t t12 = 0;
            F.ret = t11;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t13 = gm_l0;
        void * t14 = GM_LD(void *, (((uint8_t *)t13) + 0));
        gm_tf5srh4mp4b3i t15 = GM_LD(gm_tf5srh4mp4b3i, (((uint8_t *)t14) + 320));
        gm_tf5srh4mp4b3i t16 = t15;
        uint64_t t17;
        if (((((uint8_t *)&t16))[8] != 0)) {
            uint64_t t18 = GM_LD(uint64_t, (((uint8_t *)&t16) + 0));
            t17 = t18;
        } else {
            gm_th2lqbo77pvsc t19;
            memset(&t19, 0, sizeof t19);
            uint8_t t20 = 0;
            F.ret = t19;
            goto gm_fret;
        }
        gm_l4 = t17;
    }
    {
        void * t21 = gm_l0;
        uint64_t t22 = gm_l4;
        gm_b16_8 t23 = gm_l2;
        gm_f_effect_with_op_ol1t6ca((void *)&(*(gm_th2lqbo77pvsc *)gm_out), t21, t22, t23, ((uint8_t)1));
        goto gm_fdone;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_th2lqbo77pvsc gm_f_op_effect_n2jr58f_c(void * a0, uint64_t a1, gm_b16_8 a2) { gm_th2lqbo77pvsc gm_r; gm_f_op_effect_n2jr58f(&gm_r, a0, a1, a2); return gm_r; }
inline __attribute__((aligned(4))) gm_b16_8 gm_f_op_hint_rfghuf5(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1, const void * a2) {
    struct gm_fr_op_hint_rfghuf5 F;
    gm_b16_8 gm_l0; /* effect_ */
    gm_b16_8 gm_l1; /* name */
    gm_th2lqbo77pvsc gm_l2; /* qualifier */
    gm_b16_8 gm_l3; /* m */
    gm_b16_8 gm_l4; /* q */
    GM_STACK_CHECK(&F, 585u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    memcpy(&gm_l2, a2, sizeof gm_l2);
    {
        gm_b16_8 t2;
        if (!((((uint8_t *)&gm_l2))[16] != 0)) goto ie3;
        gm_b16_8 t4 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l2));
        gm_l3 = t4;
        gm_b16_8 t6 = gm_l3;
        gm_tnpifv20irnph t7;
        GM_ST(gm_b16_8, ((uint8_t *)&t7 + 0), t6);
        gm_tabb710d9mq2e t8;
        gm_f_format_6kfrubb((void *)&t8, (void *)NULL, cx, NULL, t7);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t9 = t8;
        if (((((uint8_t *)&t9))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t9) + 8, gm_src6lm8esmv16p1, 91, 37);
        gm_b16_8 t10 = GM_LD(gm_b16_8, (((uint8_t *)&t9) + 8));
        gm_b16_8 t11 = t10;
        gm_b16_8 t12 = t11;
        gm_b16_8 t13 = GM_LD(gm_b16_8, ((uint8_t *)&t12));
        t2 = t13;
        goto ix3;
ie3:;
        gm_b16_8 t15;
        GM_ST(void *, (uint8_t *)&t15, (void *)(gm_sr4qbrk6tlgaa));
        GM_ST(uintptr_t, (uint8_t *)&t15 + 8, (uintptr_t)(0));
        gm_b16_8 t16 = t15;
        t2 = t16;
ix3:;
        gm_l4 = t2;
    }
    {
        gm_tud7oovfvnf43 t17;
        gm_b16_8 t18 = gm_l1;
        GM_ST(gm_b16_8, (uint8_t *)&t17 + 0, t18);
        gm_b16_8 t19 = gm_l0;
        GM_ST(gm_b16_8, (uint8_t *)&t17 + 16, t19);
        gm_b16_8 t20 = gm_l4;
        GM_ST(gm_b16_8, (uint8_t *)&t17 + 32, t20);
        gm_b16_8 t21 = gm_l0;
        GM_ST(gm_b16_8, (uint8_t *)&t17 + 48, t21);
        gm_b16_8 t22 = gm_l1;
        GM_ST(gm_b16_8, (uint8_t *)&t17 + 64, t22);
        gm_tabb710d9mq2e t23;
        gm_f_format_mqdhvcm((void *)&t23, (void *)NULL, cx, NULL, &t17);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t24 = t23;
        if (((((uint8_t *)&t24))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t24) + 8, gm_src6lm8esmv16p1, 92, 12);
        gm_b16_8 t25 = GM_LD(gm_b16_8, (((uint8_t *)&t24) + 8));
        gm_b16_8 t26 = t25;
        gm_b16_8 t27 = GM_LD(gm_b16_8, ((uint8_t *)&t26));
        uint8_t t28 = 0;
        F.ret = t27;
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
inline __attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_public_module_member_e2i0at7(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2, uint64_t a3) {
    struct gm_fr_public_module_member_e2i0at7 F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* m */
    gm_b16_8 gm_l2; /* name */
    uint64_t gm_l3; /* from */
    uint64_t gm_l4; /* d */
    uint64_t gm_l5; /* r */
    GM_STACK_CHECK(&F, 743u);
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
        uint64_t t4 = gm_l1;
        gm_b16_8 t5 = gm_l2;
        gm_tf5srh4mp4b3i t6 = gm_f_lookup_59juqoo(t3, t4, t5);
        gm_tf5srh4mp4b3i t7 = t6;
        uint64_t t8;
        if (((((uint8_t *)&t7))[8] != 0)) {
            uint64_t t9 = GM_LD(uint64_t, (((uint8_t *)&t7) + 0));
            t8 = t9;
        } else {
            gm_tf5srh4mp4b3i t10;
            memset(&t10, 0, sizeof t10);
            uint8_t t11 = 0;
            F.ret = t10;
            goto gm_fret;
        }
        gm_l4 = t8;
    }
    {
        void * t13 = gm_l0;
        uint64_t t14 = gm_l4;
        uint64_t t15 = gm_l3;
        uint8_t t16 = gm_f_decl_visible_bd168ti(t13, t14, t15);
        uint8_t t17 = (uint8_t)!t16;
        if (!t17) goto ie12;
        {
            gm_tf5srh4mp4b3i t19;
            memset(&t19, 0, sizeof t19);
            uint8_t t20 = 0;
            F.ret = t19;
            goto gm_fret;
        }
        goto ix12;
ie12:;
ix12:;
    }
    {
        void * t21 = gm_l0;
        uint64_t t22 = gm_l4;
        uint64_t t23 = gm_l3;
        gm_b0_1 t24 = gm_f_note_use_from_2lmujsq((void *)NULL, cx, NULL, t21, t22, t23);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t26 = gm_l0;
        uint64_t t27 = gm_l4;
        gm_t6qooom8qa2do t28;
        gm_f_resolve_decl_pus042v((void *)&t28, (void *)NULL, cx, NULL, t26, t27);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t6qooom8qa2do t29 = t28;
        uint8_t t30 = GM_LD(uint8_t, (((uint8_t *)&t29) + 0));
        if (t30 != ((uint8_t)9ULL)) goto ie25;
        uint64_t t31 = GM_LD(uint64_t, (((uint8_t *)&t29) + 8));
        gm_l5 = t31;
        {
            uint64_t t33 = gm_l5;
            gm_tf5srh4mp4b3i t34;
            GM_ST(uint8_t, (uint8_t *)&t34 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t34 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t34 + 12, 0);
            GM_ST(uint64_t, ((uint8_t *)&t34), t33);
            ((uint8_t *)&t34)[8] = 1;
            uint8_t t35 = 0;
            F.ret = t34;
            goto gm_fret;
        }
        goto ix25;
ie25:;
        {
            gm_tf5srh4mp4b3i t37;
            memset(&t37, 0, sizeof t37);
            uint8_t t38 = 0;
            F.ret = t37;
            goto gm_fret;
        }
ix25:;
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
__attribute__((aligned(4))) void gm_f_public_op_effect_72d4rbe(void *gm_out, void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_public_op_effect_72d4rbe F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* m */
    gm_b16_8 gm_l2; /* name */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    gm_t3877pk7lv748 gm_l5; /* n */
    void * gm_l6; /* d */
    void * gm_l8; /* lit */
    gm_b16_8 gm_l9; /* <tmp> */
    uint64_t gm_l10; /* <tmp> */
    GM_STACK_CHECK(&F, 704u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        {
            uint64_t t3 = gm_l1;
            uint64_t t4 = (uintptr_t)t3;
            void * t5 = gm_l0;
            void * t6 = GM_LD(void *, (((uint8_t *)t5) + 0));
            uint64_t t7 = GM_LD(uintptr_t, ((((uint8_t *)t6) + 200) + 0) + 8);
            if (t4 >= (uintptr_t)(t7)) gm_trap_index(t4, t7, gm_src6lm8esmv16p1, 263, 14);
            gm_b16_8 t8 = GM_LD(gm_b16_8, ((((uint8_t *)GM_LD(void *, ((((uint8_t *)t6) + 200) + 0)) + t4 * 184) + 16) + 0));
            gm_l3 = t8;
        }
        {
            gm_l4 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t10 = gm_l4;
                uint64_t t11 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                uint8_t t12 = (t10 < t11);
                if (!t12) goto lb9;
                {
                    uint64_t t15 = gm_l4;
                    uint64_t t16 = (uintptr_t)t15;
                    uint64_t t17 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                    gm_t3877pk7lv748 t20 = GM_LD(gm_t3877pk7lv748, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t16 * 24));
                    gm_l5 = t20;
                    {
                        uint64_t t22 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 16));
                        uint64_t t23 = (uintptr_t)t22;
                        void * t24 = gm_l0;
                        void * t25 = GM_LD(void *, (((uint8_t *)t24) + 0));
                        uint64_t t26 = GM_LD(uintptr_t, ((((uint8_t *)t25) + 240) + 0) + 8);
                        if (t23 >= (uintptr_t)(t26)) gm_trap_index(t23, t26, gm_src6lm8esmv16p1, 264, 15);
                        void * t27 = (void *)((uint8_t *)GM_LD(void *, ((((uint8_t *)t25) + 240) + 0)) + t23 * 216);
                        gm_l6 = t27;
                    }
                    {
                        void * t29 = gm_l6;
                        uint8_t t30 = ((GM_LD(uint8_t, ((((uint8_t *)t29) + 40) + 56) + 0) == ((uint8_t)0ULL)));
                        if (!t30) goto ie28;
                        {
                            goto lc9;
                        }
                        goto ix28;
ie28:;
ix28:;
                    }
                    {
                        void * t33 = gm_l6;
                        uint8_t t34 = GM_LD(uint8_t, (((((uint8_t *)t33) + 40) + 64) + 0));
                        if (t34 != ((uint8_t)0ULL)) goto ie32;
                        gm_tv2uf4leil5hq t35 = GM_LD(gm_tv2uf4leil5hq, (((((uint8_t *)t33) + 40) + 64) + 8));
                        F.l7 = t35;
                        {
                            void * t38 = GM_LD(void *, (((uint8_t *)&F.l7) + 40));
                            uint8_t t39 = GM_LD(uint8_t, ((((uint8_t *)t38) + 8) + 0));
                            if (t39 != ((uint8_t)34ULL)) goto ie37;
                            void * t40 = GM_LD(void *, ((((uint8_t *)t38) + 8) + 8));
                            gm_l8 = t40;
                            {
                                {
                                    void * t43 = gm_l8;
                                    gm_b16_8 t44 = GM_LD(gm_b16_8, (((uint8_t *)t43) + 24));
                                    gm_l9 = t44;
                                }
                                {
                                    gm_l10 = ((uint64_t)0ULL);
                                }
                                {
                                    for (;;) {
                                        uint64_t t46 = gm_l10;
                                        uint64_t t47 = GM_LD(uintptr_t, ((uint8_t *)&gm_l9) + 8);
                                        uint8_t t48 = (t46 < t47);
                                        if (!t48) goto lb45;
                                        {
                                            uint64_t t51 = gm_l10;
                                            uint64_t t52 = (uintptr_t)t51;
                                            uint64_t t53 = GM_LD(uintptr_t, ((uint8_t *)&gm_l9) + 8);
                                            gm_tc0sbs1tp6d7f t56 = GM_LD(gm_tc0sbs1tp6d7f, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l9)) + t52 * 80));
                                            F.l11 = t56;
                                            {
                                                gm_b16_8 t59 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l11) + 0) + 0));
                                                gm_b16_8 t60 = gm_l2;
                                                gm_b16_8 t61 = t59;
                                                gm_b16_8 t62 = t60;
                                                uint8_t t63 = (gm_eq0iqiubfm7e(((uint8_t *)&t61), ((uint8_t *)&t62)));
                                                if (!t63) goto ie58;
                                                {
                                                    void * t65 = gm_l6;
                                                    gm_b16_8 t66 = GM_LD(gm_b16_8, ((((uint8_t *)t65) + 0) + 0));
                                                    gm_th2lqbo77pvsc t67;
                                                    GM_ST(uint8_t, (uint8_t *)&t67 + 17, 0);
                                                    GM_ST(uint16_t, (uint8_t *)&t67 + 18, 0);
                                                    GM_ST(uint32_t, (uint8_t *)&t67 + 20, 0);
                                                    GM_ST(gm_b16_8, ((uint8_t *)&t67), t66);
                                                    ((uint8_t *)&t67)[16] = 1;
                                                    uint8_t t68 = 0;
                                                    F.ret = t67;
                                                    goto gm_fret;
                                                }
                                                goto ix58;
ie58:;
ix58:;
                                            }
                                            goto me54;
mn55:;
                                            gm_trap("no match arm matched the scrutinee", gm_src6lm8esmv16p1, 268, 9);
me54:;
                                        }
lc45:;
                                        {
                                            uint64_t t70 = gm_l10;
                                            uint64_t r71; bool o72 = __builtin_add_overflow(t70, ((uint64_t)1ULL), &r71);
                                            uint64_t t73 = r71;
                                            gm_l10 = t73;
                                        }
                                    }
lb45:;
                                }
                            }
                            goto ix37;
ie37:;
                            {
                                goto lc9;
                            }
ix37:;
                        }
                        goto ix32;
ie32:;
                        {
                            goto lc9;
                        }
ix32:;
                    }
                    goto me18;
mn19:;
                    gm_trap("no match arm matched the scrutinee", gm_src6lm8esmv16p1, 263, 5);
me18:;
                }
lc9:;
                {
                    uint64_t t77 = gm_l4;
                    uint64_t r78; bool o79 = __builtin_add_overflow(t77, ((uint64_t)1ULL), &r78);
                    uint64_t t80 = r78;
                    gm_l4 = t80;
                }
            }
lb9:;
        }
    }
    {
        gm_th2lqbo77pvsc t81;
        memset(&t81, 0, sizeof t81);
        uint8_t t82 = 0;
        F.ret = t81;
        goto gm_fret;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_th2lqbo77pvsc gm_f_public_op_effect_72d4rbe_c(void * a0, uint64_t a1, gm_b16_8 a2) { gm_th2lqbo77pvsc gm_r; gm_f_public_op_effect_72d4rbe(&gm_r, a0, a1, a2); return gm_r; }
inline __attribute__((aligned(4))) uint8_t gm_f_same_package_vmps24r(void * a0, uint64_t a1, uint64_t a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_same_package_vmps24r F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* a */
    uint64_t gm_l2; /* b */
    void * gm_l3; /* ma */
    void * gm_l4; /* mb */
    GM_STACK_CHECK(&F, 212u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        uint64_t t3 = gm_l1;
        uint64_t t4 = gm_l2;
        uint8_t t5 = ((t3 == t4));
        if (!t5) goto ie2;
        {
            uint8_t t7 = 0;
            F.ret = ((uint8_t)1);
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        uint64_t t8 = gm_l1;
        uint64_t t9 = (uintptr_t)t8;
        void * t10 = gm_l0;
        void * t11 = GM_LD(void *, (((uint8_t *)t10) + 0));
        uint64_t t12 = GM_LD(uintptr_t, ((((uint8_t *)t11) + 200) + 0) + 8);
        if (t9 >= (uintptr_t)(t12)) gm_trap_index(t9, t12, gm_src6lm8esmv16p1, 161, 12);
        void * t13 = (void *)((uint8_t *)GM_LD(void *, ((((uint8_t *)t11) + 200) + 0)) + t9 * 184);
        gm_l3 = t13;
    }
    {
        uint64_t t14 = gm_l2;
        uint64_t t15 = (uintptr_t)t14;
        void * t16 = gm_l0;
        void * t17 = GM_LD(void *, (((uint8_t *)t16) + 0));
        uint64_t t18 = GM_LD(uintptr_t, ((((uint8_t *)t17) + 200) + 0) + 8);
        if (t15 >= (uintptr_t)(t18)) gm_trap_index(t15, t18, gm_src6lm8esmv16p1, 162, 12);
        void * t19 = (void *)((uint8_t *)GM_LD(void *, ((((uint8_t *)t17) + 200) + 0)) + t15 * 184);
        gm_l4 = t19;
    }
    {
        uint8_t t20 = 0;
        uint8_t t21 = 0;
        void * t22 = gm_l3;
        uint8_t t23 = GM_LD(uint8_t, (((uint8_t *)t22) + 112));
        uint8_t t24 = (uint8_t)!t23;
        if (t24) {
            void * t25 = gm_l4;
            uint8_t t26 = GM_LD(uint8_t, (((uint8_t *)t25) + 112));
            uint8_t t27 = (uint8_t)!t26;
            t21 = t27;
        } else t21 = 0;
        if (t21) {
            void * t28 = gm_l3;
            uint64_t t29 = GM_LD(uint64_t, (((uint8_t *)t28) + 136));
            void * t30 = gm_l4;
            uint64_t t31 = GM_LD(uint64_t, (((uint8_t *)t30) + 136));
            uint8_t t32 = ((t29 == t31));
            t20 = t32;
        } else t20 = 0;
        uint8_t t33 = 0;
        F.ret = t20;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) void gm_f_std_home_dsdl68c(void *gm_out, gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_std_home_dsdl68c F;
    gm_b16_8 gm_l0; /* name */
    GM_STACK_CHECK(&F, 1301u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_b16_8 t3 = gm_l0;
        gm_tfeab279c16rr t4;
        gm_b16_8 t5;
        GM_ST(void *, (uint8_t *)&t5, (void *)(gm_sgqs124m28gfg));
        GM_ST(uintptr_t, (uint8_t *)&t5 + 8, (uintptr_t)(6));
        GM_ST(gm_b16_8, (uint8_t *)&t4 + 0, t5);
        gm_b16_8 t6;
        GM_ST(void *, (uint8_t *)&t6, (void *)(gm_so97uarpe8rtg));
        GM_ST(uintptr_t, (uint8_t *)&t6 + 8, (uintptr_t)(8));
        GM_ST(gm_b16_8, (uint8_t *)&t4 + 16, t6);
        gm_b16_8 t7;
        GM_ST(void *, (uint8_t *)&t7, (void *)(gm_srugm21uf2sb0));
        GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)(8));
        GM_ST(gm_b16_8, (uint8_t *)&t4 + 32, t7);
        gm_tfeab279c16rr t8 = t4;
        uint8_t t9 = 0;
        gm_b16_8 t10 = t3;
        if (gm_eq0iqiubfm7e(((uint8_t *)&t8) + 0, ((uint8_t *)&t10))) t9 = 1;
        if (gm_eq0iqiubfm7e(((uint8_t *)&t8) + 16, ((uint8_t *)&t10))) t9 = 1;
        if (gm_eq0iqiubfm7e(((uint8_t *)&t8) + 32, ((uint8_t *)&t10))) t9 = 1;
        uint8_t t11 = (uint8_t)(t9);
        if (!t11) goto ie2;
        {
            gm_b16_8 t13;
            GM_ST(void *, (uint8_t *)&t13, (void *)(gm_stingg155o3k5));
            GM_ST(uintptr_t, (uint8_t *)&t13 + 8, (uintptr_t)(7));
            gm_th2lqbo77pvsc t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 20, 0);
            GM_ST(gm_b16_8, ((uint8_t *)&t14), t13);
            ((uint8_t *)&t14)[16] = 1;
            uint8_t t15 = 0;
            F.ret = t14;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        gm_b16_8 t17 = gm_l0;
        gm_th2d9c7aju8hp t18;
        gm_b16_8 t19;
        GM_ST(void *, (uint8_t *)&t19, (void *)(gm_s48njnbffa02m));
        GM_ST(uintptr_t, (uint8_t *)&t19 + 8, (uintptr_t)(4));
        GM_ST(gm_b16_8, (uint8_t *)&t18 + 0, t19);
        gm_b16_8 t20;
        GM_ST(void *, (uint8_t *)&t20, (void *)(gm_sgqcrkl2nh339));
        GM_ST(uintptr_t, (uint8_t *)&t20 + 8, (uintptr_t)(3));
        GM_ST(gm_b16_8, (uint8_t *)&t18 + 16, t20);
        gm_b16_8 t21;
        GM_ST(void *, (uint8_t *)&t21, (void *)(gm_snil57vrv77lr));
        GM_ST(uintptr_t, (uint8_t *)&t21 + 8, (uintptr_t)(11));
        GM_ST(gm_b16_8, (uint8_t *)&t18 + 32, t21);
        gm_b16_8 t22;
        GM_ST(void *, (uint8_t *)&t22, (void *)(gm_s5le7rpnp1iq6));
        GM_ST(uintptr_t, (uint8_t *)&t22 + 8, (uintptr_t)(8));
        GM_ST(gm_b16_8, (uint8_t *)&t18 + 48, t22);
        gm_b16_8 t23;
        GM_ST(void *, (uint8_t *)&t23, (void *)(gm_s84ckbhm2d3ul));
        GM_ST(uintptr_t, (uint8_t *)&t23 + 8, (uintptr_t)(8));
        GM_ST(gm_b16_8, (uint8_t *)&t18 + 64, t23);
        gm_b16_8 t24;
        GM_ST(void *, (uint8_t *)&t24, (void *)(gm_snu0769072h68));
        GM_ST(uintptr_t, (uint8_t *)&t24 + 8, (uintptr_t)(8));
        GM_ST(gm_b16_8, (uint8_t *)&t18 + 80, t24);
        gm_b16_8 t25;
        GM_ST(void *, (uint8_t *)&t25, (void *)(gm_shrc3j552p60m));
        GM_ST(uintptr_t, (uint8_t *)&t25 + 8, (uintptr_t)(8));
        GM_ST(gm_b16_8, (uint8_t *)&t18 + 96, t25);
        gm_th2d9c7aju8hp t26 = t18;
        uint8_t t27 = 0;
        gm_b16_8 t28 = t17;
        if (gm_eq0iqiubfm7e(((uint8_t *)&t26) + 0, ((uint8_t *)&t28))) t27 = 1;
        if (gm_eq0iqiubfm7e(((uint8_t *)&t26) + 16, ((uint8_t *)&t28))) t27 = 1;
        if (gm_eq0iqiubfm7e(((uint8_t *)&t26) + 32, ((uint8_t *)&t28))) t27 = 1;
        if (gm_eq0iqiubfm7e(((uint8_t *)&t26) + 48, ((uint8_t *)&t28))) t27 = 1;
        if (gm_eq0iqiubfm7e(((uint8_t *)&t26) + 64, ((uint8_t *)&t28))) t27 = 1;
        if (gm_eq0iqiubfm7e(((uint8_t *)&t26) + 80, ((uint8_t *)&t28))) t27 = 1;
        if (gm_eq0iqiubfm7e(((uint8_t *)&t26) + 96, ((uint8_t *)&t28))) t27 = 1;
        uint8_t t29 = (uint8_t)(t27);
        if (!t29) goto ie16;
        {
            gm_b16_8 t31;
            GM_ST(void *, (uint8_t *)&t31, (void *)(gm_su6mi500md00b));
            GM_ST(uintptr_t, (uint8_t *)&t31 + 8, (uintptr_t)(6));
            gm_th2lqbo77pvsc t32;
            GM_ST(uint8_t, (uint8_t *)&t32 + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&t32 + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&t32 + 20, 0);
            GM_ST(gm_b16_8, ((uint8_t *)&t32), t31);
            ((uint8_t *)&t32)[16] = 1;
            uint8_t t33 = 0;
            F.ret = t32;
            goto gm_fret;
        }
        goto ix16;
ie16:;
ix16:;
    }
    {
        gm_b16_8 t35 = gm_l0;
        gm_tif3elqk01bdl t36;
        gm_b16_8 t37;
        GM_ST(void *, (uint8_t *)&t37, (void *)(gm_sulfrs2bmdlu2));
        GM_ST(uintptr_t, (uint8_t *)&t37 + 8, (uintptr_t)(5));
        GM_ST(gm_b16_8, (uint8_t *)&t36 + 0, t37);
        gm_b16_8 t38;
        GM_ST(void *, (uint8_t *)&t38, (void *)(gm_smnot3vdfhqsc));
        GM_ST(uintptr_t, (uint8_t *)&t38 + 8, (uintptr_t)(5));
        GM_ST(gm_b16_8, (uint8_t *)&t36 + 16, t38);
        gm_b16_8 t39;
        GM_ST(void *, (uint8_t *)&t39, (void *)(gm_suhmdi1gd6hta));
        GM_ST(uintptr_t, (uint8_t *)&t39 + 8, (uintptr_t)(6));
        GM_ST(gm_b16_8, (uint8_t *)&t36 + 32, t39);
        gm_b16_8 t40;
        GM_ST(void *, (uint8_t *)&t40, (void *)(gm_sonl5k4jilrbc));
        GM_ST(uintptr_t, (uint8_t *)&t40 + 8, (uintptr_t)(12));
        GM_ST(gm_b16_8, (uint8_t *)&t36 + 48, t40);
        gm_b16_8 t41;
        GM_ST(void *, (uint8_t *)&t41, (void *)(gm_sj97id02v9fh3));
        GM_ST(uintptr_t, (uint8_t *)&t41 + 8, (uintptr_t)(5));
        GM_ST(gm_b16_8, (uint8_t *)&t36 + 64, t41);
        gm_b16_8 t42;
        GM_ST(void *, (uint8_t *)&t42, (void *)(gm_ssqd2isjrp8nh));
        GM_ST(uintptr_t, (uint8_t *)&t42 + 8, (uintptr_t)(10));
        GM_ST(gm_b16_8, (uint8_t *)&t36 + 80, t42);
        gm_tif3elqk01bdl t43 = t36;
        uint8_t t44 = 0;
        gm_b16_8 t45 = t35;
        if (gm_eq0iqiubfm7e(((uint8_t *)&t43) + 0, ((uint8_t *)&t45))) t44 = 1;
        if (gm_eq0iqiubfm7e(((uint8_t *)&t43) + 16, ((uint8_t *)&t45))) t44 = 1;
        if (gm_eq0iqiubfm7e(((uint8_t *)&t43) + 32, ((uint8_t *)&t45))) t44 = 1;
        if (gm_eq0iqiubfm7e(((uint8_t *)&t43) + 48, ((uint8_t *)&t45))) t44 = 1;
        if (gm_eq0iqiubfm7e(((uint8_t *)&t43) + 64, ((uint8_t *)&t45))) t44 = 1;
        if (gm_eq0iqiubfm7e(((uint8_t *)&t43) + 80, ((uint8_t *)&t45))) t44 = 1;
        uint8_t t46 = (uint8_t)(t44);
        if (!t46) goto ie34;
        {
            gm_b16_8 t48;
            GM_ST(void *, (uint8_t *)&t48, (void *)(gm_s88rffcs9cj2p));
            GM_ST(uintptr_t, (uint8_t *)&t48 + 8, (uintptr_t)(11));
            gm_th2lqbo77pvsc t49;
            GM_ST(uint8_t, (uint8_t *)&t49 + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&t49 + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&t49 + 20, 0);
            GM_ST(gm_b16_8, ((uint8_t *)&t49), t48);
            ((uint8_t *)&t49)[16] = 1;
            uint8_t t50 = 0;
            F.ret = t49;
            goto gm_fret;
        }
        goto ix34;
ie34:;
ix34:;
    }
    {
        gm_b16_8 t52 = gm_l0;
        gm_t0uuhaljsf410 t53;
        gm_b16_8 t54;
        GM_ST(void *, (uint8_t *)&t54, (void *)(gm_s79oh9p16aa8u));
        GM_ST(uintptr_t, (uint8_t *)&t54 + 8, (uintptr_t)(2));
        GM_ST(gm_b16_8, (uint8_t *)&t53 + 0, t54);
        gm_b16_8 t55;
        GM_ST(void *, (uint8_t *)&t55, (void *)(gm_sltqebpmj61tp));
        GM_ST(uintptr_t, (uint8_t *)&t55 + 8, (uintptr_t)(2));
        GM_ST(gm_b16_8, (uint8_t *)&t53 + 16, t55);
        gm_b16_8 t56;
        GM_ST(void *, (uint8_t *)&t56, (void *)(gm_s16bsnuu7i04s));
        GM_ST(uintptr_t, (uint8_t *)&t56 + 8, (uintptr_t)(2));
        GM_ST(gm_b16_8, (uint8_t *)&t53 + 32, t56);
        gm_b16_8 t57;
        GM_ST(void *, (uint8_t *)&t57, (void *)(gm_s1j5pceke7iuk));
        GM_ST(uintptr_t, (uint8_t *)&t57 + 8, (uintptr_t)(4));
        GM_ST(gm_b16_8, (uint8_t *)&t53 + 48, t57);
        gm_t0uuhaljsf410 t58 = t53;
        uint8_t t59 = 0;
        gm_b16_8 t60 = t52;
        if (gm_eq0iqiubfm7e(((uint8_t *)&t58) + 0, ((uint8_t *)&t60))) t59 = 1;
        if (gm_eq0iqiubfm7e(((uint8_t *)&t58) + 16, ((uint8_t *)&t60))) t59 = 1;
        if (gm_eq0iqiubfm7e(((uint8_t *)&t58) + 32, ((uint8_t *)&t60))) t59 = 1;
        if (gm_eq0iqiubfm7e(((uint8_t *)&t58) + 48, ((uint8_t *)&t60))) t59 = 1;
        uint8_t t61 = (uint8_t)(t59);
        if (!t61) goto ie51;
        {
            gm_b16_8 t63;
            GM_ST(void *, (uint8_t *)&t63, (void *)(gm_s8hpuukc6o9qu));
            GM_ST(uintptr_t, (uint8_t *)&t63 + 8, (uintptr_t)(8));
            gm_th2lqbo77pvsc t64;
            GM_ST(uint8_t, (uint8_t *)&t64 + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&t64 + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&t64 + 20, 0);
            GM_ST(gm_b16_8, ((uint8_t *)&t64), t63);
            ((uint8_t *)&t64)[16] = 1;
            uint8_t t65 = 0;
            F.ret = t64;
            goto gm_fret;
        }
        goto ix51;
ie51:;
ix51:;
    }
    {
        gm_th2lqbo77pvsc t66;
        memset(&t66, 0, sizeof t66);
        uint8_t t67 = 0;
        F.ret = t66;
        goto gm_fret;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_th2lqbo77pvsc gm_f_std_home_dsdl68c_c(gm_b16_8 a0) { gm_th2lqbo77pvsc gm_r; gm_f_std_home_dsdl68c(&gm_r, a0); return gm_r; }
inline __attribute__((aligned(4))) uint8_t gm_f_universe_hidden_gsk2b8r(void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_universe_hidden_gsk2b8r F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* d */
    GM_STACK_CHECK(&F, 132u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint64_t t1 = gm_l1;
    uint64_t t2 = (uintptr_t)t1;
    void * t3 = gm_l0;
    void * t4 = GM_LD(void *, (((uint8_t *)t3) + 0));
    uint64_t t5 = GM_LD(uintptr_t, ((((uint8_t *)t4) + 240) + 0) + 8);
    if (t2 >= (uintptr_t)(t5)) gm_trap_index(t2, t5, gm_src6lm8esmv16p1, 67, 64);
    gm_b16_8 t6 = GM_LD(gm_b16_8, ((((uint8_t *)GM_LD(void *, ((((uint8_t *)t4) + 240) + 0)) + t2 * 216) + 0) + 0));
    void * t7 = ((uint8_t *)gm_cfiqcgtjhen);
    if ((uintptr_t)0 > (uintptr_t)26 || (uintptr_t)26 > (uintptr_t)26) gm_trap_slice((uintptr_t)0, (uintptr_t)26, 26, gm_src6lm8esmv16p1, 67, 96);
    gm_b16_8 t8;
    GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t7 + (uintptr_t)0 * 16)));
    GM_ST(uintptr_t, (uint8_t *)&t8 + 8, (uintptr_t)((uintptr_t)26 - (uintptr_t)0));
    gm_b16_8 t9 = t8;
    uint8_t t10 = 0;
    gm_b16_8 t11 = t6;
    for (uintptr_t i = 0; i < (uintptr_t)(GM_LD(uintptr_t, ((uint8_t *)&t9) + 8)); i++) if (gm_eq0iqiubfm7e(((uint8_t *)GM_LD(void *, ((uint8_t *)&t9))) + i * 16, ((uint8_t *)&t11))) { t10 = 1; break; }
    uint8_t t12 = (uint8_t)(t10);
    F.ret = t12;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) void gm_f_universe_hint_ams3f10(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1) {
    struct gm_fr_universe_hint_ams3f10 F;
    void * gm_l0; /* s */
    gm_b16_8 gm_l1; /* name */
    uint64_t gm_l2; /* prelude */
    uint64_t gm_l3; /* d */
    gm_b16_8 gm_l4; /* m */
    GM_STACK_CHECK(&F, 882u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 0));
        gm_tf5srh4mp4b3i t4 = GM_LD(gm_tf5srh4mp4b3i, (((uint8_t *)t3) + 320));
        gm_tf5srh4mp4b3i t5 = t4;
        uint64_t t6;
        if (((((uint8_t *)&t5))[8] != 0)) {
            uint64_t t7 = GM_LD(uint64_t, (((uint8_t *)&t5) + 0));
            t6 = t7;
        } else {
            gm_th2lqbo77pvsc t8;
            memset(&t8, 0, sizeof t8);
            uint8_t t9 = 0;
            F.ret = t8;
            goto gm_fret;
        }
        gm_l2 = t6;
    }
    {
        void * t10 = gm_l0;
        void * t11 = GM_LD(void *, (((uint8_t *)t10) + 0));
        uint64_t t12 = gm_l2;
        gm_b16_8 t13 = gm_l1;
        gm_tf5srh4mp4b3i t14 = gm_f_lookup_59juqoo(t11, t12, t13);
        gm_tf5srh4mp4b3i t15 = t14;
        uint64_t t16;
        if (((((uint8_t *)&t15))[8] != 0)) {
            uint64_t t17 = GM_LD(uint64_t, (((uint8_t *)&t15) + 0));
            t16 = t17;
        } else {
            gm_th2lqbo77pvsc t18;
            memset(&t18, 0, sizeof t18);
            uint8_t t19 = 0;
            F.ret = t18;
            goto gm_fret;
        }
        gm_l3 = t16;
    }
    {
        uint8_t t21 = 0;
        void * t22 = gm_l0;
        uint64_t t23 = gm_l3;
        uint8_t t24 = gm_f_universe_hidden_gsk2b8r(t22, t23);
        uint8_t t25 = (uint8_t)!t24;
        if (!t25) {
            uint64_t t26 = gm_l3;
            uint64_t t27 = (uintptr_t)t26;
            void * t28 = gm_l0;
            void * t29 = GM_LD(void *, (((uint8_t *)t28) + 0));
            uint64_t t30 = GM_LD(uintptr_t, ((((uint8_t *)t29) + 240) + 0) + 8);
            if (t27 >= (uintptr_t)(t30)) gm_trap_index(t27, t30, gm_src6lm8esmv16p1, 74, 36);
            uint8_t t31;
            uint8_t t34 = GM_LD(uint8_t, ((((uint8_t *)GM_LD(void *, ((((uint8_t *)t29) + 240) + 0)) + t27 * 216) + 200) + 0));
            if (t34 != ((uint8_t)1ULL)) goto mn33;
            t31 = ((uint8_t)1);
            goto me32;
mn33:;
            t31 = ((uint8_t)0);
            goto me32;
mn35:;
            gm_trap("no match arm matched the scrutinee", gm_src6lm8esmv16p1, 74, 36);
me32:;
            t21 = t31;
        } else t21 = 1;
        if (!t21) goto ie20;
        {
            gm_th2lqbo77pvsc t37;
            memset(&t37, 0, sizeof t37);
            uint8_t t38 = 0;
            F.ret = t37;
            goto gm_fret;
        }
        goto ix20;
ie20:;
ix20:;
    }
    {
        gm_b16_8 t40 = gm_l1;
        gm_th2lqbo77pvsc t41;
        gm_f_std_home_dsdl68c((void *)&t41, t40);
        gm_th2lqbo77pvsc t42 = t41;
        if (!((((uint8_t *)&t42))[16] != 0)) goto ie39;
        gm_b16_8 t43 = GM_LD(gm_b16_8, ((uint8_t *)&t42));
        gm_l4 = t43;
        {
            gm_t0uuhaljsf410 t45;
            gm_b16_8 t46 = gm_l1;
            GM_ST(gm_b16_8, (uint8_t *)&t45 + 0, t46);
            gm_b16_8 t47 = gm_l4;
            GM_ST(gm_b16_8, (uint8_t *)&t45 + 16, t47);
            uint64_t t48 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
            void * t49 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)));
            uint64_t t50 = (uintptr_t)((uint64_t)4ULL);
            if (t50 > (uintptr_t)t48 || (uintptr_t)t48 > (uintptr_t)t48) gm_trap_slice(t50, (uintptr_t)t48, t48, gm_src6lm8esmv16p1, 76, 93);
            gm_b16_8 t51;
            GM_ST(void *, (uint8_t *)&t51, (void *)(((uint8_t *)t49 + t50 * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t51 + 8, (uintptr_t)((uintptr_t)t48 - t50));
            GM_ST(gm_b16_8, (uint8_t *)&t45 + 32, t51);
            gm_b16_8 t52 = gm_l1;
            GM_ST(gm_b16_8, (uint8_t *)&t45 + 48, t52);
            gm_tabb710d9mq2e t53;
            gm_f_format_9ihba55((void *)&t53, (void *)NULL, cx, NULL, &t45);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t54 = t53;
            if (((((uint8_t *)&t54))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t54) + 8, gm_src6lm8esmv16p1, 76, 16);
            gm_b16_8 t55 = GM_LD(gm_b16_8, (((uint8_t *)&t54) + 8));
            gm_b16_8 t56 = t55;
            gm_b16_8 t57 = GM_LD(gm_b16_8, ((uint8_t *)&t56));
            gm_th2lqbo77pvsc t58;
            GM_ST(uint8_t, (uint8_t *)&t58 + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&t58 + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&t58 + 20, 0);
            GM_ST(gm_b16_8, ((uint8_t *)&t58), t57);
            ((uint8_t *)&t58)[16] = 1;
            uint8_t t59 = 0;
            F.ret = t58;
            goto gm_fret;
        }
        goto ix39;
ie39:;
ix39:;
    }
    {
        gm_t6j6smj7qoctu t60;
        gm_b16_8 t61 = gm_l1;
        GM_ST(gm_b16_8, (uint8_t *)&t60 + 0, t61);
        gm_b16_8 t62 = gm_l1;
        GM_ST(gm_b16_8, (uint8_t *)&t60 + 16, t62);
        gm_tabb710d9mq2e t63;
        gm_f_format_vdba58s((void *)&t63, (void *)NULL, cx, NULL, &t60);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t64 = t63;
        if (((((uint8_t *)&t64))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t64) + 8, gm_src6lm8esmv16p1, 78, 12);
        gm_b16_8 t65 = GM_LD(gm_b16_8, (((uint8_t *)&t64) + 8));
        gm_b16_8 t66 = t65;
        gm_b16_8 t67 = GM_LD(gm_b16_8, ((uint8_t *)&t66));
        gm_th2lqbo77pvsc t68;
        GM_ST(uint8_t, (uint8_t *)&t68 + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&t68 + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&t68 + 20, 0);
        GM_ST(gm_b16_8, ((uint8_t *)&t68), t67);
        ((uint8_t *)&t68)[16] = 1;
        uint8_t t69 = 0;
        F.ret = t68;
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
__attribute__((aligned(4))) gm_b0_1 gm_f_unknown_name_dmegum5(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2, gm_tl874t2atnlh3 a3) {
    struct gm_fr_unknown_name_dmegum5 F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* module */
    gm_b16_8 gm_l2; /* name */
    gm_tl874t2atnlh3 gm_l3; /* span */
    gm_b16_8 gm_l4; /* effect_ */
    gm_b16_8 gm_l7; /* hint */
    GM_STACK_CHECK(&F, 1241u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        void * t3 = gm_l0;
        uint64_t t4 = gm_l1;
        gm_b16_8 t5 = gm_l2;
        gm_th2lqbo77pvsc t6;
        gm_f_op_effect_n2jr58f((void *)&t6, t3, t4, t5);
        gm_th2lqbo77pvsc t7 = t6;
        if (!((((uint8_t *)&t7))[16] != 0)) goto ie2;
        gm_b16_8 t8 = GM_LD(gm_b16_8, ((uint8_t *)&t7));
        gm_l4 = t8;
        {
            gm_tl874t2atnlh3 t10 = gm_l3;
            gm_b16_8 t11 = gm_l4;
            gm_b16_8 t12 = gm_l2;
            gm_th2lqbo77pvsc t13;
            memset(&t13, 0, sizeof t13);
            gm_b16_8 t14 = gm_f_op_hint_rfghuf5((void *)NULL, cx, NULL, t11, t12, &t13);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_f_error_s6hn871((void *)&F.l5, (void *)NULL, cx, NULL, t10, t14);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t15 = (void *)((uint8_t *)&F.l5);
            gm_t6j6smj7qoctu t16;
            gm_b16_8 t17 = gm_l4;
            GM_ST(gm_b16_8, (uint8_t *)&t16 + 0, t17);
            gm_b16_8 t18 = gm_l2;
            GM_ST(gm_b16_8, (uint8_t *)&t16 + 16, t18);
            gm_tabb710d9mq2e t19;
            gm_f_format_7her5m9((void *)&t19, (void *)NULL, cx, NULL, &t16);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t20 = t19;
            if (((((uint8_t *)&t20))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t20) + 8, gm_src6lm8esmv16p1, 247, 19);
            gm_b16_8 t21 = GM_LD(gm_b16_8, (((uint8_t *)&t20) + 8));
            gm_b16_8 t22 = t21;
            gm_b16_8 t23 = GM_LD(gm_b16_8, ((uint8_t *)&t22));
            uint32_t t24 = GM_LD(uint32_t, (((uint8_t *)&gm_l3) + 0));
            uint32_t t25 = GM_LD(uint32_t, (((uint8_t *)&gm_l3) + 4));
            uint32_t t26 = GM_LD(uint32_t, (((uint8_t *)&gm_l3) + 4));
            gm_tl874t2atnlh3 t27;
            GM_ST(uint32_t, ((uint8_t *)&t27 + 0), t24);
            GM_ST(uint32_t, ((uint8_t *)&t27 + 4), t25);
            GM_ST(uint32_t, ((uint8_t *)&t27 + 8), t26);
            gm_b16_8 t28 = gm_l4;
            gm_tnpifv20irnph t29;
            GM_ST(gm_b16_8, ((uint8_t *)&t29 + 0), t28);
            gm_tabb710d9mq2e t30;
            gm_f_format_6kfrubb((void *)&t30, (void *)NULL, cx, NULL, t29);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t31 = t30;
            if (((((uint8_t *)&t31))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t31) + 8, gm_src6lm8esmv16p1, 247, 120);
            gm_b16_8 t32 = GM_LD(gm_b16_8, (((uint8_t *)&t31) + 8));
            gm_b16_8 t33 = t32;
            gm_b16_8 t34 = GM_LD(gm_b16_8, ((uint8_t *)&t33));
            gm_b0_1 t35 = gm_f_add_fix_0gkcr4c((void *)NULL, cx, NULL, t15, t23, t27, t34);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t36 = gm_l0;
            gm_b0_1 t37 = gm_f_emit_dph5v1e((void *)NULL, cx, NULL, t36, &(*(gm_t4gut2c9fj1kt *)((uint8_t *)&F.l5)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_b0_1 t38;
            uint8_t t39 = 0;
            F.ret = t38;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        gm_tl874t2atnlh3 t40 = gm_l3;
        gm_b16_8 t41 = gm_l2;
        gm_tnpifv20irnph t42;
        GM_ST(gm_b16_8, ((uint8_t *)&t42 + 0), t41);
        gm_tabb710d9mq2e t43;
        gm_f_format_sbfc2dl((void *)&t43, (void *)NULL, cx, NULL, t42);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t44 = t43;
        if (((((uint8_t *)&t44))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t44) + 8, gm_src6lm8esmv16p1, 251, 27);
        gm_b16_8 t45 = GM_LD(gm_b16_8, (((uint8_t *)&t44) + 8));
        gm_b16_8 t46 = t45;
        gm_b16_8 t47 = GM_LD(gm_b16_8, ((uint8_t *)&t46));
        gm_f_error_s6hn871((void *)&F.l6, (void *)NULL, cx, NULL, t40, t47);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t49 = gm_l0;
        gm_b16_8 t50 = gm_l2;
        gm_th2lqbo77pvsc t51;
        gm_f_universe_hint_ams3f10((void *)&t51, (void *)NULL, cx, NULL, t49, t50);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_th2lqbo77pvsc t52 = t51;
        if (!((((uint8_t *)&t52))[16] != 0)) goto ie48;
        gm_b16_8 t53 = GM_LD(gm_b16_8, ((uint8_t *)&t52));
        gm_l7 = t53;
        {
            void * t55 = (void *)((uint8_t *)&F.l6);
            gm_b16_8 t56 = gm_l7;
            gm_f_add_note_46v502s(t55, t56);
        }
        {
            void * t58 = gm_l0;
            uint64_t t59 = gm_l1;
            gm_b16_8 t60 = gm_l2;
            gm_tl874t2atnlh3 t61 = gm_l3;
            gm_tqetrgo0q24b5 t62;
            gm_f_import_fix_7822666((void *)&t62, (void *)NULL, cx, NULL, t58, t59, t60, t61);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tqetrgo0q24b5 t63 = t62;
            if (!((((uint8_t *)&t63))[40] != 0)) goto ie57;
            gm_trqfvmjtt4sij t64 = GM_LD(gm_trqfvmjtt4sij, ((uint8_t *)&t63));
            F.l8 = t64;
            {
                void * t66 = (void *)((uint8_t *)&F.l6);
                uint64_t t67 = GM_LD(uintptr_t, (((uint8_t *)&F.l8) + 0) + 8);
                uint64_t r68; bool o69 = __builtin_sub_overflow(t67, ((uint64_t)1ULL), &r68);
                if (o69) gm_trap("integer overflow in `-`", gm_src6lm8esmv16p1, 255, 66);
                uint64_t t70 = r68;
                uint64_t t71 = (uintptr_t)t70;
                uint64_t t72 = GM_LD(uintptr_t, (((uint8_t *)&F.l8) + 0) + 8);
                if (t71 >= (uintptr_t)(t72)) gm_trap_index(t71, t72, gm_src6lm8esmv16p1, 255, 54);
                gm_b16_8 t73 = GM_LD(gm_b16_8, (((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l8) + 0)) + t71 * 32) + 16));
                gm_tnpifv20irnph t74;
                GM_ST(gm_b16_8, ((uint8_t *)&t74 + 0), t73);
                gm_tabb710d9mq2e t75;
                gm_f_format_ooibcq2((void *)&t75, (void *)NULL, cx, NULL, t74);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t76 = t75;
                if (((((uint8_t *)&t76))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t76) + 8, gm_src6lm8esmv16p1, 255, 29);
                gm_b16_8 t77 = GM_LD(gm_b16_8, (((uint8_t *)&t76) + 8));
                gm_b16_8 t78 = t77;
                gm_b16_8 t79 = GM_LD(gm_b16_8, ((uint8_t *)&t78));
                gm_b16_8 t80 = GM_LD(gm_b16_8, (((uint8_t *)&F.l8) + 0));
                gm_b16_8 t81 = t80;
                gm_b16_8 t82 = GM_LD(gm_b16_8, ((uint8_t *)&t81));
                gm_b0_1 t83 = gm_f_add_fix_edits_em0b48a((void *)NULL, cx, NULL, t66, t79, t82);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix57;
ie57:;
ix57:;
        }
        goto ix48;
ie48:;
ix48:;
    }
    {
        void * t84 = gm_l0;
        gm_b0_1 t85 = gm_f_emit_dph5v1e((void *)NULL, cx, NULL, t84, &(*(gm_t4gut2c9fj1kt *)((uint8_t *)&F.l6)));
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
inline __attribute__((aligned(4))) uint8_t gm_f_visible_ll07dr9(void * a0, gm_t9qkh0rsooc8d a1, uint64_t a2, uint64_t a3) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_visible_ll07dr9 F;
    void * gm_l0; /* s */
    gm_t9qkh0rsooc8d gm_l1; /* vis */
    uint64_t gm_l2; /* owner */
    uint64_t gm_l3; /* from */
    GM_STACK_CHECK(&F, 74u);
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
