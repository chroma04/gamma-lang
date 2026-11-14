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
typedef struct { uint64_t f0; uint64_t f1; } gm_t2lqu8jsj511d;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint64_t f0; } gm_t4ce0jd3tdro4;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint8_t f0; uint8_t p1[7]; uint64_t f1; } gm_tphtgnn9vgkcv;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; } gm_t0uuhaljsf410;
_Static_assert(sizeof(gm_t2lqu8jsj511d) == 16 && _Alignof(gm_t2lqu8jsj511d) == 8 && offsetof(gm_t2lqu8jsj511d, f0) == 0 && offsetof(gm_t2lqu8jsj511d, f1) == 8, "layout of `(u64, u64)`");
typedef struct { gm_b16_8 f0; uint64_t f1; } gm_t3877pk7lv748;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
_Static_assert(sizeof(gm_t4ce0jd3tdro4) == 8 && _Alignof(gm_t4ce0jd3tdro4) == 8 && offsetof(gm_t4ce0jd3tdro4, f0) == 0, "layout of `Instant`");
typedef struct { gm_b16_8 f0; int64_t f1; } gm_t4d55u65a2eb7;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
typedef struct { uint64_t f0; gm_b16_8 f1; gm_b16_8 f2; } gm_t7neromnu8t62;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
typedef struct { uint64_t f0; uint64_t f1; gm_b16_8 f2; } gm_tbccvikg6b96e;
typedef struct { gm_b16_8 f0; __int128 f1; } gm_tctdkl637nfjf;
typedef struct { uint64_t f0; gm_b16_8 f1; } gm_tf1bjg81u0glv;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; } gm_tfeab279c16rr;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_b16_8 f0; uint64_t f1; uint64_t f2; } gm_tgbgti82mf879;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; gm_b16_8 f4; gm_b16_8 f5; gm_b16_8 f6; } gm_th2d9c7aju8hp;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
typedef struct { uint64_t f0; gm_b16_8 f1; uint64_t f2; } gm_tob1snuii5bme;
_Static_assert(sizeof(gm_tphtgnn9vgkcv) == 16 && _Alignof(gm_tphtgnn9vgkcv) == 8 && offsetof(gm_tphtgnn9vgkcv, f0) == 0 && offsetof(gm_tphtgnn9vgkcv, f1) == 8, "layout of `(found: bool, at: usize)`");
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t4ce0jd3tdro4 v0; } f1; } gm_ttiiv5vhiuvvr;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; gm_b16_8 f4; } gm_tud7oovfvnf43;
_Static_assert(sizeof(gm_t0uuhaljsf410) == 64 && _Alignof(gm_t0uuhaljsf410) == 8 && offsetof(gm_t0uuhaljsf410, f0) == 0 && offsetof(gm_t0uuhaljsf410, f1) == 16 && offsetof(gm_t0uuhaljsf410, f2) == 32 && offsetof(gm_t0uuhaljsf410, f3) == 48, "layout of `(string, string, string, string)`");
_Static_assert(sizeof(gm_t3877pk7lv748) == 24 && _Alignof(gm_t3877pk7lv748) == 8 && offsetof(gm_t3877pk7lv748, f0) == 0 && offsetof(gm_t3877pk7lv748, f1) == 16, "layout of `WasmReader`");
_Static_assert(sizeof(gm_t4d55u65a2eb7) == 24 && _Alignof(gm_t4d55u65a2eb7) == 8 && offsetof(gm_t4d55u65a2eb7, f0) == 0 && offsetof(gm_t4d55u65a2eb7, f1) == 16, "layout of `([]u8, i64)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_ttiiv5vhiuvvr v0; gm_tphtgnn9vgkcv v1; } f1; } gm_t4mjne9lcbiv4;
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_t7neromnu8t62) == 40 && _Alignof(gm_t7neromnu8t62) == 8 && offsetof(gm_t7neromnu8t62, f0) == 0 && offsetof(gm_t7neromnu8t62, f1) == 8 && offsetof(gm_t7neromnu8t62, f2) == 24, "layout of `(u64, string, string)`");
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
_Static_assert(sizeof(gm_tbccvikg6b96e) == 32 && _Alignof(gm_tbccvikg6b96e) == 8 && offsetof(gm_tbccvikg6b96e, f0) == 0 && offsetof(gm_tbccvikg6b96e, f1) == 8 && offsetof(gm_tbccvikg6b96e, f2) == 16, "layout of `Layout`");
_Static_assert(sizeof(gm_tctdkl637nfjf) == 32 && _Alignof(gm_tctdkl637nfjf) == 16 && offsetof(gm_tctdkl637nfjf, f0) == 0 && offsetof(gm_tctdkl637nfjf, f1) == 16, "layout of `(string, i128)`");
_Static_assert(sizeof(gm_tf1bjg81u0glv) == 24 && _Alignof(gm_tf1bjg81u0glv) == 8 && offsetof(gm_tf1bjg81u0glv, f0) == 0 && offsetof(gm_tf1bjg81u0glv, f1) == 8, "layout of `(i: usize, x: string)`");
_Static_assert(sizeof(gm_tfeab279c16rr) == 48 && _Alignof(gm_tfeab279c16rr) == 8 && offsetof(gm_tfeab279c16rr, f0) == 0 && offsetof(gm_tfeab279c16rr, f1) == 16 && offsetof(gm_tfeab279c16rr, f2) == 32, "layout of `Toolchain`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_tgbgti82mf879) == 32 && _Alignof(gm_tgbgti82mf879) == 8 && offsetof(gm_tgbgti82mf879, f0) == 0 && offsetof(gm_tgbgti82mf879, f1) == 16 && offsetof(gm_tgbgti82mf879, f2) == 24, "layout of `PayloadUnion`");
_Static_assert(sizeof(gm_th2d9c7aju8hp) == 112 && _Alignof(gm_th2d9c7aju8hp) == 8 && offsetof(gm_th2d9c7aju8hp, f0) == 0 && offsetof(gm_th2d9c7aju8hp, f1) == 16 && offsetof(gm_th2d9c7aju8hp, f2) == 32 && offsetof(gm_th2d9c7aju8hp, f3) == 48 && offsetof(gm_th2d9c7aju8hp, f4) == 64 && offsetof(gm_th2d9c7aju8hp, f5) == 80 && offsetof(gm_th2d9c7aju8hp, f6) == 96, "layout of `(string, string, string, string, string, string, string)`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
_Static_assert(sizeof(gm_tob1snuii5bme) == 32 && _Alignof(gm_tob1snuii5bme) == 8 && offsetof(gm_tob1snuii5bme, f0) == 0 && offsetof(gm_tob1snuii5bme, f1) == 8 && offsetof(gm_tob1snuii5bme, f2) == 24, "layout of `Reloc`");
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_ttiiv5vhiuvvr) == 16 && _Alignof(gm_ttiiv5vhiuvvr) == 8 && offsetof(gm_ttiiv5vhiuvvr, f0) == 0 && offsetof(gm_ttiiv5vhiuvvr, f1) == 8, "layout of `ParseError`");
_Static_assert(sizeof(gm_tud7oovfvnf43) == 80 && _Alignof(gm_tud7oovfvnf43) == 8 && offsetof(gm_tud7oovfvnf43, f0) == 0 && offsetof(gm_tud7oovfvnf43, f1) == 16 && offsetof(gm_tud7oovfvnf43, f2) == 32 && offsetof(gm_tud7oovfvnf43, f3) == 48 && offsetof(gm_tud7oovfvnf43, f4) == 64, "layout of `(string, string, string, string, string)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_ttiiv5vhiuvvr v0; uint64_t v1; } f1; } gm_tutnubt6u4hoq;
_Static_assert(sizeof(gm_t4mjne9lcbiv4) == 24 && _Alignof(gm_t4mjne9lcbiv4) == 8 && offsetof(gm_t4mjne9lcbiv4, f0) == 0 && offsetof(gm_t4mjne9lcbiv4, f1) == 8, "layout of `ParseError!(neg: bool, at: usize)`");
_Static_assert(sizeof(gm_tutnubt6u4hoq) == 24 && _Alignof(gm_tutnubt6u4hoq) == 8 && offsetof(gm_tutnubt6u4hoq, f0) == 0 && offsetof(gm_tutnubt6u4hoq, f1) == 8, "layout of `ParseError!usize`");
struct gm_fr_empty_slice_ijtp9lo {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_fn_0736hae {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_1j3gkem {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_1tiq8j9 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_2oeivui {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_2r7l0dc {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_37pdbfb {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_3psqmn1 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_3vsp7ft {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_472aruh {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_4pkhf4j {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_52crvc7 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_56r3vur {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_599rnh9 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_5ha0v65 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_5iksc4v {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_5ldr0dk {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_5ts1aan {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_64h5o83 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_6etic8u {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_6r4ifnd {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_79inkgl {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_9d1icfo {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_9o0kvur {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_aqoh8f0 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_arnu6be {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_b108j07 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_beikmkp {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_bgc9qfh {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_bmj7akp {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_boqd22f {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_bq2i257 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_c0ffr5d {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_c20aog9 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_cmjmpc7 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_d16m383 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_dh50lhg {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_dvaqvg7 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_e3q03rg {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_e94udsb {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_eaemvpe {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_eal3dae {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_eblih6b {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_f9l6pke {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_fi9qngm {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_fm5di8s {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_hjt9q04 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_ih2c9se {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_ih3upe1 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_ijni684 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_j86852b {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_jmu3b35 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_kbiuvc2 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_korree9 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_l0bcltv {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_l2f2ej8 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_ldl1hcr {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_lkdcm3n {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_lom5i6m {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_m79rh95 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_nc9pofj {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_ncrh922 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_ngj5ii9 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_nqvgqmm {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_nvpijsp {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_o9nv4oe {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_oct8fdl {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_ohjb0ee {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_oiu5u5h {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_okqu94u {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_pur3nlu {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_q5enbli {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_qg91ote {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_qhpnm5h {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_qo3jtdm {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_r6gf71l {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_rhf7rqh {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_rj4kesu {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_rl156nv {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_sdgejrh {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_sss9ick {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_su6p3o7 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_tddvoln {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_toflidg {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_tr10ktn {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_uavlrmf {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_udd1af4 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_ulelml8 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_umkmebp {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_vmupu7q {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_vqiit51 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_format_317bju7 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_32bp3b4 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_32n178h {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_32qgu0c {
    gm_frame h;
    gm_tfeab279c16rr l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tfeab279c16rr l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_32r56ja {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_331tgtc {
    gm_frame h;
    gm_tfeab279c16rr l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tfeab279c16rr l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_33edc64 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_34ti291 {
    gm_frame h;
    gm_t7neromnu8t62 l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_t7neromnu8t62 l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_359meqs {
    gm_frame h;
    gm_tfeab279c16rr l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tfeab279c16rr l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_35to3im {
    gm_frame h;
    gm_tfeab279c16rr l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tfeab279c16rr l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_3669u6h {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_368205k {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_37aah2s {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_37dbjte {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_37g0gks {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_38m1q8c {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_39snre1 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_3aao56v {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_3aass3a {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_3ahqitg {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_3bbfhut {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_3buamil {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_3dfoht6 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_3fe7hc3 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_3g143bv {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_3ifsvek {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_3itad0c {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_3j59e2u {
    gm_frame h;
    gm_tud7oovfvnf43 l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tud7oovfvnf43 l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_3mj8462 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_3mo2tb7 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_3mrt8ba {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_3nkc0o4 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_3o89hhj {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_3pa9rer {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_3pfhto5 {
    gm_frame h;
    gm_tfeab279c16rr l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tfeab279c16rr l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_3pk07bs {
    gm_frame h;
    gm_tfeab279c16rr l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tfeab279c16rr l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_3qphnbe {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_3rm05av {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_3s3fqnk {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_3s8s1g6 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_3tnlp8q {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_3tnvb6a {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_3tqcmdm {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_3v827ra {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_3vgp4uu {
    gm_frame h;
    gm_tfeab279c16rr l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tfeab279c16rr l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_3vtl3te {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_j07a6mr {
    gm_frame h;
    gm_tfeab279c16rr l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tfeab279c16rr l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_j0ggm04 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_j0i128e {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_j44ni19 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_j4le8o0 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_j4sks15 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_j7jgble {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_j8m87ti {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_ja2c5em {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_jagvq3q {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_jaikcua {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_jb19283 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_jb7bqji {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_jc3ftg7 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_jdgr48v {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_je4jsfp {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_jet3e97 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_jf46ta8 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_jfku65e {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_jh4rfvo {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_jha63mc {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_jhm2032 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_jicque0 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_jiop5bj {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_jj2aipm {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_jk4dii4 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_jlajof4 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_jm2cnr1 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_jm4dd7q {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_jn4ifp1 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_jo1gjr5 {
    gm_frame h;
    gm_t0uuhaljsf410 l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_t0uuhaljsf410 l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_jp1a7ai {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_jp1qc7o {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_jp4udcs {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_jpvhgp3 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_jpvl2js {
    gm_frame h;
    gm_th2d9c7aju8hp l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_th2d9c7aju8hp l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_jr8ev5r {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_jrdns83 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_jrngvku {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_jru5qda {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_jth3mgf {
    gm_frame h;
    gm_t0uuhaljsf410 l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_t0uuhaljsf410 l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_jtr5odb {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_jv6n7oh {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_jvf1ma2 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_is_upper_iekrftp {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_list_uvfu1k1 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_parse_int_3ce02np {
    gm_frame h;
    gm_tutnubt6u4hoq ret;
};
struct gm_fr_to_lower_0fu6efk {
    gm_frame h;
    uint8_t ret;
};
__attribute__((aligned(4))) void gm_f_deinit_r70pji5(void * a0);
__attribute__((aligned(4))) void gm_f_digits_nifrtn2(void *gm_out, gm_b16_8 a0, uint64_t a1, uint8_t a2, uint32_t a3);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_extend_0cs85gs(void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_0736hae(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_1j3gkem(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_1tiq8j9(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_2oeivui(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_2r7l0dc(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_37pdbfb(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_3psqmn1(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_3vsp7ft(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_472aruh(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_4pkhf4j(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_52crvc7(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_56r3vur(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_599rnh9(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_5ha0v65(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_5iksc4v(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_5ldr0dk(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_5ts1aan(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_64h5o83(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_6etic8u(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_6r4ifnd(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_79inkgl(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_9d1icfo(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_9o0kvur(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_aqoh8f0(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_arnu6be(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_b108j07(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_beikmkp(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_bgc9qfh(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_bmj7akp(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_boqd22f(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_bq2i257(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_c0ffr5d(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_c20aog9(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_cmjmpc7(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_d16m383(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_dh50lhg(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_dvaqvg7(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_e3q03rg(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_e94udsb(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_eaemvpe(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_eal3dae(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_eblih6b(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_f9l6pke(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_fi9qngm(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_fm5di8s(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_hjt9q04(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_ih2c9se(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_ih3upe1(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_ijni684(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_j86852b(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_jmu3b35(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_kbiuvc2(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_korree9(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_l0bcltv(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_l2f2ej8(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_ldl1hcr(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_lkdcm3n(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_lom5i6m(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_m79rh95(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_nc9pofj(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_ncrh922(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_ngj5ii9(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_nqvgqmm(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_nvpijsp(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_o9nv4oe(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_oct8fdl(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_ohjb0ee(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_oiu5u5h(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_okqu94u(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_pur3nlu(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_q5enbli(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_qg91ote(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_qhpnm5h(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_qo3jtdm(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_r6gf71l(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_rhf7rqh(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_rj4kesu(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_rl156nv(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_sdgejrh(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_sss9ick(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_su6p3o7(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_tddvoln(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_toflidg(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_tr10ktn(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_uavlrmf(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_udd1af4(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_ulelml8(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_umkmebp(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_vmupu7q(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_vqiit51(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_format_317bju7(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_32bp3b4(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_32n178h(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
__attribute__((aligned(4))) void gm_f_format_32qgu0c(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_32r56ja(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_331tgtc(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_33edc64(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_34ti291(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_359meqs(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_35to3im(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_3669u6h(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
__attribute__((aligned(4))) void gm_f_format_368205k(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
__attribute__((aligned(4))) void gm_f_format_37aah2s(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_37dbjte(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_37g0gks(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_38m1q8c(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t2lqu8jsj511d a0);
__attribute__((aligned(4))) void gm_f_format_39snre1(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_3aao56v(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_3aass3a(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_3ahqitg(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_3bbfhut(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_3buamil(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_3dfoht6(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_3fe7hc3(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_3g143bv(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_3ifsvek(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_3itad0c(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
__attribute__((aligned(4))) void gm_f_format_3j59e2u(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_3mj8462(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_3mo2tb7(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_3mrt8ba(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_3nkc0o4(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_3o89hhj(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_3pa9rer(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_3pfhto5(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_3pk07bs(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_3qphnbe(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_3rm05av(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_3s3fqnk(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_3s8s1g6(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_3tnlp8q(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_3tnvb6a(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
__attribute__((aligned(4))) void gm_f_format_3tqcmdm(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_3v827ra(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_3vgp4uu(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_3vtl3te(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_j07a6mr(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_j0ggm04(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_j0i128e(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_j44ni19(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
__attribute__((aligned(4))) void gm_f_format_j4le8o0(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_j4sks15(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_j7jgble(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
__attribute__((aligned(4))) void gm_f_format_j8m87ti(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_ja2c5em(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_jagvq3q(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
__attribute__((aligned(4))) void gm_f_format_jaikcua(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_jb19283(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_jb7bqji(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_jc3ftg7(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
__attribute__((aligned(4))) void gm_f_format_jdgr48v(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_je4jsfp(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_jet3e97(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_jf46ta8(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_jfku65e(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_jh4rfvo(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_jha63mc(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_jhm2032(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_jicque0(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_jiop5bj(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_jj2aipm(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_jk4dii4(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_jlajof4(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_jm2cnr1(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_jm4dd7q(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_jn4ifp1(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_jo1gjr5(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_jp1a7ai(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_jp1qc7o(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_jp4udcs(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_jpvhgp3(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
__attribute__((aligned(4))) void gm_f_format_jpvl2js(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_jr8ev5r(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_jrdns83(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_jrngvku(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_jru5qda(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_jth3mgf(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_jtr5odb(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_jv6n7oh(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_jvf1ma2(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_into_slice_hq0te56(void *gm_out, void * a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_upper_iekrftp(uint8_t a0);
inline __attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_parse_int_3ce02np(void *gm_out, gm_b16_8 a0);
__attribute__((aligned(4))) gm_tutnubt6u4hoq gm_f_parse_int_3ce02np_c(gm_b16_8 a0);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_reserve_d1qo8mf(void * a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_sign_utteujm(void *gm_out, gm_b16_8 a0);
inline __attribute__((aligned(4))) uint8_t gm_f_to_lower_0fu6efk(uint8_t a0);
GM_NOINLINE bool gm_pf0aqhubr4up(void *code, void *env, gm_node *cx, const void *d, uintptr_t n);
GM_NOINLINE bool gm_pfk8csn7us23(void *code, void *env, gm_node *cx, const gm_type *t, const void *v, const gm_spec *s);
extern const uint8_t gm_s03c24qg7i06q[13];
extern const uint8_t gm_s05crl6fa1at4[6];
extern const uint8_t gm_s088gqlg957uo[6];
extern const uint8_t gm_s0eipmcrsllno[8];
extern const uint8_t gm_s0j2j9kdc2obh[17];
extern const uint8_t gm_s0jhhkkbgc6ks[18];
extern const uint8_t gm_s0ljc8tssh8f7[8];
extern const uint8_t gm_s0mph2pjoa3np[17];
extern const uint8_t gm_s0q6kpmhsprri[33];
extern const uint8_t gm_s15g40fmv51gv[59];
extern const uint8_t gm_s16mf4cvajh59[13];
extern const uint8_t gm_s1757p1g7h4me[42];
extern const uint8_t gm_s17uhrtdo7u30[2];
extern const uint8_t gm_s1asl1e1tvb0k[2];
extern const uint8_t gm_s1bpuqd44lur7[7];
extern const uint8_t gm_s1dmrsprfdp53[8];
extern const uint8_t gm_s1fjspgm800ug[7];
extern const uint8_t gm_s1lekaltijhvv[8];
extern const uint8_t gm_s22tv9c5r20tl[4];
extern const uint8_t gm_s2568a81aaotm[70];
extern const uint8_t gm_s25euhn4i5qkh[72];
extern const uint8_t gm_s26crtotu14pn[3];
extern const uint8_t gm_s26gmgbksn2pm[61];
extern const uint8_t gm_s2gnpj7k4l22n[2];
extern const uint8_t gm_s2ph2ad6lffbq[17];
extern const uint8_t gm_s2s2hv5oav6fb[29];
extern const uint8_t gm_s2u5posfkbgnq[2];
extern const uint8_t gm_s2vj6kgk19dag[32];
extern const uint8_t gm_s38cjdjl7n99k[10];
extern const uint8_t gm_s3hinmnodmkmv[21];
extern const uint8_t gm_s3la5l2o2cgbq[43];
extern const uint8_t gm_s3ml7p21f55q7[14];
extern const uint8_t gm_s41bap0m04tt9[12];
extern const uint8_t gm_s4bcn9pd0h83n[48];
extern const uint8_t gm_s4lmdtjb9rnah[70];
extern const uint8_t gm_s4oil3epcv041[3];
extern const uint8_t gm_s4pru1na1k33s[3];
extern const uint8_t gm_s519l85f26pmp[81];
extern const uint8_t gm_s542rn9shphg8[16];
extern const uint8_t gm_s5dqpb4hur24f[37];
extern const uint8_t gm_s5i05gtdgdefb[42];
extern const uint8_t gm_s5lqhiu2bn6of[8];
extern const uint8_t gm_s5v9d9i0gsl8c[8];
extern const uint8_t gm_s66b184memqnd[4];
extern const uint8_t gm_s68lv7o2uide3[61];
extern const uint8_t gm_s6sv4vhunlp6l[9];
extern const uint8_t gm_s743265bqlq4a[14];
extern const uint8_t gm_s75m3dt9015lh[15];
extern const uint8_t gm_s7hndfpk1u0p0[7];
extern const uint8_t gm_s7ipvt5onv6ht[4];
extern const uint8_t gm_s7j8d035g0p9i[38];
extern const uint8_t gm_s7p349cm5h5jf[20];
extern const uint8_t gm_s7vsso7k8tg8d[25];
extern const uint8_t gm_s802sbfa6mtsg[27];
extern const uint8_t gm_s85m9c4pt0ikp[6];
extern const uint8_t gm_s86js8lt7ibda[48];
extern const uint8_t gm_s8e4i8e286ns3[25];
extern const uint8_t gm_s8l22p9l388io[67];
extern const uint8_t gm_s8pvtaofn65p9[29];
extern const uint8_t gm_s8qvuobg6jcbo[13];
extern const uint8_t gm_s91vsko3h6v6r[10];
extern const uint8_t gm_s93qviqoij24b[31];
extern const uint8_t gm_s98novtk49ooh[28];
extern const uint8_t gm_s9ab1ohnfcjkc[6];
extern const uint8_t gm_s9b7nekk74u38[3];
extern const uint8_t gm_s9b81q4kouna6[11];
extern const uint8_t gm_s9indmpqvpug4[58];
extern const uint8_t gm_s9jrvf2ogt807[17];
extern const uint8_t gm_s9r72jgd5hb05[5];
extern const uint8_t gm_s9vag5co36u2r[18];
extern const uint8_t gm_sa23gppmgki7j[11];
extern const uint8_t gm_sa71447djt33j[49];
extern const uint8_t gm_sa9dtqeprt7kf[75];
extern const uint8_t gm_sadscuj628cl7[8];
extern const uint8_t gm_saed870gk04of[14];
extern const uint8_t gm_saeqiapbqfqhj[28];
extern const uint8_t gm_sag4ku07t2k8t[25];
extern const uint8_t gm_sajhum9b4nr96[2];
extern const uint8_t gm_sao4mumvadoct[4];
extern const uint8_t gm_saovklm35s57t[184];
extern const uint8_t gm_saqo50iv9jjmk[9];
extern const uint8_t gm_sb1dc9u3lvvu1[5];
extern const uint8_t gm_sb54ui2amic32[2];
extern const uint8_t gm_sb6j3egm9oljk[5];
extern const uint8_t gm_sbabluaguu2bc[51];
extern const uint8_t gm_sbfr1qrcvp774[59];
extern const uint8_t gm_sbjshone12eeu[158];
extern const uint8_t gm_sbvmilijbiuka[26];
extern const uint8_t gm_sc092629f9on4[15];
extern const uint8_t gm_sc24a25pi012e[8];
extern const uint8_t gm_sccadmt684uri[23];
extern const uint8_t gm_scogrgnqo3tpu[22];
extern const uint8_t gm_scotq6054g01h[6];
extern const uint8_t gm_scq0i5n3f7it5[41];
extern const uint8_t gm_scs1evojsik8q[4];
extern const uint8_t gm_scs7jonv1denn[64];
extern const uint8_t gm_sd2mrmucl6ngt[7];
extern const uint8_t gm_sd6vqnf9glo8m[51];
extern const uint8_t gm_sdealia24icjj[40];
extern const uint8_t gm_sdgdb59na1854[23];
extern const uint8_t gm_sdjfsrjr2b2ke[2];
extern const uint8_t gm_sdmti3s9j8p44[11];
extern const uint8_t gm_sdpo2hjlq8e9b[31];
extern const uint8_t gm_sdrrh4gk917l3[10];
extern const uint8_t gm_sdru6cvprml58[29];
extern const uint8_t gm_sdtthp48kia81[5];
extern const uint8_t gm_sdu7bd5v9q750[65];
extern const uint8_t gm_sdugj3qbhl84q[5];
extern const uint8_t gm_sdv3b0kjlrdl2[46];
extern const uint8_t gm_se20eu14i6umf[9];
extern const uint8_t gm_se4r520bkfair[18];
extern const uint8_t gm_sefk7cselekki[3];
extern const uint8_t gm_sel0tu7gvv1na[4];
extern const uint8_t gm_seo41mk3n3ro5[13];
extern const uint8_t gm_sf1ljria5bdei[8];
extern const uint8_t gm_sf20puqngp9vm[9];
extern const uint8_t gm_sfdj0c5vltira[31];
extern const uint8_t gm_sfm8ps2e26dh1[3];
extern const uint8_t gm_sfm9elkq6bn2e[2];
extern const uint8_t gm_sfrdbcc53tuv9[18];
extern const uint8_t gm_sfs5rrntfu3mi[25];
extern const uint8_t gm_sg26if17vtome[23];
extern const uint8_t gm_sg58j44mq0qb5[61];
extern const uint8_t gm_sg5gdnrith2i6[3];
extern const uint8_t gm_sg6iftc6c3tph[47];
extern const uint8_t gm_sgahfuanpfk2r[60];
extern const uint8_t gm_sgc7crh43q9tj[6];
extern const uint8_t gm_sgfqei77bsia8[4];
extern const uint8_t gm_sgh0dvu15b9ta[10];
extern const uint8_t gm_sgnt3t4lgofos[35];
extern const uint8_t gm_sgpgu8iag22ja[15];
extern const uint8_t gm_sh54qlvpeqnin[10];
extern const uint8_t gm_sh5qb2l9hubh3[9];
extern const uint8_t gm_shbtdicfd8lcc[12];
extern const uint8_t gm_shdh4dd2ejebj[158];
extern const uint8_t gm_shg24ogv0rnt4[14];
extern const uint8_t gm_shmgu63rvoce3[36];
extern const uint8_t gm_shn9bhhpahcjo[16];
extern const uint8_t gm_shpjiqltl04un[33];
extern const uint8_t gm_sil41urphp1ol[12];
extern const uint8_t gm_sinogveu00g7m[36];
extern const uint8_t gm_sir2n5292lks8[8];
extern const uint8_t gm_sj1gcl0qo4i9p[7];
extern const uint8_t gm_sj21lfdl9q0t4[6];
extern const uint8_t gm_sjguhtmsnuhlj[58];
extern const uint8_t gm_sjljm9omp36ob[52];
extern const uint8_t gm_sjnkguo0atpm5[3];
extern const uint8_t gm_sjr44k79a48mq[4];
extern const uint8_t gm_sjtp8ip8t28uh[7];
extern const uint8_t gm_sjvtpaes95ji4[27];
extern const uint8_t gm_sk0kqh2pa96ut[73];
extern const uint8_t gm_sk2nrl11c01gj[20];
extern const uint8_t gm_sk30fte1u6en9[33];
extern const uint8_t gm_sk754mj3p1el6[2];
extern const uint8_t gm_skagf3vj30re5[17];
extern const uint8_t gm_skaimjkudbui9[39];
extern const uint8_t gm_skd5c72k4pcr6[23];
extern const uint8_t gm_skf282h8e3q1e[49];
extern const uint8_t gm_skfo9c0nr3nrd[50];
extern const uint8_t gm_sl1f8ibfoc1i1[13];
extern const uint8_t gm_sl3q52ep6aa53[2];
extern const uint8_t gm_sld5pmb7t0r25[9];
extern const uint8_t gm_sldmbttac8k2j[59];
extern const uint8_t gm_slgmqgkioa4u6[3];
extern const uint8_t gm_slmas8vbhthkn[20];
extern const uint8_t gm_slof5tl7m8grj[4];
extern const uint8_t gm_slqq08j0fro21[31];
extern const uint8_t gm_sltjg0sbhbo73[13];
extern const uint8_t gm_sluiboi2c0d5n[58];
extern const uint8_t gm_sm54129hqmnsd[16];
extern const uint8_t gm_smbgfcmcr0r8n[11];
extern const uint8_t gm_smbjf525bk9a0[20];
extern const uint8_t gm_smooaouqmm8dj[2];
extern const uint8_t gm_sn0785fueq6ki[64];
extern const uint8_t gm_snbque26o7lbi[7];
extern const uint8_t gm_snekrjln06tfl[14];
extern const uint8_t gm_snenkhfev166s[3];
extern const uint8_t gm_sni6hcs91b3qh[13];
extern const uint8_t gm_snsbh43f5eo46[5];
extern const uint8_t gm_snukl6jqep6jf[4];
extern const uint8_t gm_so2blaqutst8g[53];
extern const uint8_t gm_so514oj5nbhcu[2];
extern const uint8_t gm_so94vinsu4uib[50];
extern const uint8_t gm_so9fi6r28770a[3];
extern const uint8_t gm_sogkbe8nre26a[17];
extern const uint8_t gm_soiad5r9c65ru[37];
extern const uint8_t gm_soohe0i8iafj3[20];
extern const uint8_t gm_sovcjofctijvj[47];
extern const uint8_t gm_sovv48653dnju[40];
extern const uint8_t gm_sp02qqgucmpt7[3];
extern const uint8_t gm_sp7cdd0n91vli[8];
extern const uint8_t gm_sp8i1c1kf79ks[3];
extern const uint8_t gm_sp8vnj37dd7ds[7];
extern const uint8_t gm_spa5q1834e71s[49];
extern const uint8_t gm_speojrsjsem8a[50];
extern const uint8_t gm_spod3tmkh25up[2];
extern const uint8_t gm_sqcvjnsub5bfs[53];
extern const uint8_t gm_sqk8phuvunvgk[56];
extern const uint8_t gm_sqqh9r6c17rju[7];
extern const uint8_t gm_sqs6r7f5gl2vu[26];
extern const uint8_t gm_sqv6e5e2cne7b[59];
extern const uint8_t gm_sqvepl1893o5g[3];
extern const uint8_t gm_sr00p3fdg5qn3[19];
extern const uint8_t gm_sr44oukgimglr[23];
extern const uint8_t gm_sr7ga013iei9a[4];
extern const uint8_t gm_sr9i7j9bpmaec[11];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_src3d0v42kavhv7[];
extern const char gm_srci5ni72s6oj5f[];
extern const char gm_srcrgc9bkquf6nr[];
extern const uint8_t gm_srd2ukct5o1qh[6];
extern const uint8_t gm_srd5daqbo6ilc[58];
extern const uint8_t gm_srkbv7g2spehb[33];
extern const uint8_t gm_srn86a84qh2td[55];
extern const uint8_t gm_srojjig2k2m5d[13];
extern const uint8_t gm_srs4ljjaijhd2[24];
extern const uint8_t gm_srvgp776ipjtg[30];
extern const uint8_t gm_ss4tls6jr9q3m[10];
extern const uint8_t gm_ss9d6rvgfqq53[13];
extern const uint8_t gm_ssa2crodel6ih[29];
extern const uint8_t gm_sscmtmpbttmhb[93];
extern const uint8_t gm_ssgnvuivrkqi2[84];
extern const uint8_t gm_st02nolqdtiev[9];
extern const uint8_t gm_st3ja0nqi37bv[2];
extern const uint8_t gm_st3m5olipe6ov[17];
extern const uint8_t gm_st4gkaq24fvj9[32];
extern const uint8_t gm_st6hj8v0h80gt[6];
extern const uint8_t gm_st7fm661apctp[17];
extern const uint8_t gm_st984in2akmmc[43];
extern const uint8_t gm_statjrbfbnhar[11];
extern const uint8_t gm_stc6hgo9kskc3[8];
extern const uint8_t gm_stddp43pjd9r1[51];
extern const uint8_t gm_stf7lvoc7oe54[16];
extern const uint8_t gm_stobu0djed6ck[10];
extern const uint8_t gm_su5bsj1nioctr[22];
extern const uint8_t gm_su6ol10q7la3d[7];
extern const uint8_t gm_su71itml70ikf[5];
extern const uint8_t gm_suaek0bfou9a6[14];
extern const uint8_t gm_suifusi0pb6s0[12];
extern const uint8_t gm_sukjjfkvr8401[40];
extern const uint8_t gm_suqqp23fdh2ff[6];
extern const uint8_t gm_sut7mhrkhqp77[56];
extern const uint8_t gm_sv19dj08f7d6b[45];
extern const uint8_t gm_svc1hcla4tqfb[6];
extern const uint8_t gm_sveg1mb15s1b2[18];
extern const uint8_t gm_sveguffvp7vd6[15];
extern const uint8_t gm_svgk3n4sk1uat[9];
extern const uint8_t gm_svhavo13qha5k[4];
extern const uint8_t gm_svirf1u8t89n0[47];
extern const uint8_t gm_svjl1hr0jvvbv[9];
extern const gm_type gm_ty0iqiubfm7e;
extern const gm_type gm_ty467d5cm1d5;
extern const gm_type gm_ty6groq37ram;
extern const gm_type gm_tyb2nno7n67r;
extern const gm_type gm_tybihgc69jv3;
extern const gm_type gm_tyiiln09e647;
extern const gm_type gm_tysqt22rph28;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_0736hae(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_0736hae F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_jk4dii4 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_jk4dii4 *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_jk4dii4 *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_1j3gkem(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_1j3gkem F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_39snre1 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_39snre1 *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_39snre1 *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_1tiq8j9(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_1tiq8j9 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_jet3e97 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_jet3e97 *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_jet3e97 *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_2oeivui(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_2oeivui F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_3669u6h *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_3669u6h *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_3669u6h *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_2r7l0dc(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_2r7l0dc F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_3v827ra *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_3v827ra *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_3v827ra *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_37pdbfb(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_37pdbfb F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_jth3mgf *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_jth3mgf *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_jth3mgf *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_3psqmn1(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_3psqmn1 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_jb7bqji *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_jb7bqji *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_jb7bqji *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_3vsp7ft(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_3vsp7ft F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_368205k *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_368205k *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_368205k *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_472aruh(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_472aruh F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_37dbjte *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_37dbjte *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_37dbjte *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_4pkhf4j(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_4pkhf4j F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_jh4rfvo *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_jh4rfvo *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_jh4rfvo *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_52crvc7(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_52crvc7 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_38m1q8c *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_38m1q8c *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_38m1q8c *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_56r3vur(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_56r3vur F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_3mj8462 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_3mj8462 *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_3mj8462 *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_599rnh9(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_599rnh9 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_jiop5bj *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_jiop5bj *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_jiop5bj *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_5ha0v65(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_5ha0v65 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_3dfoht6 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_3dfoht6 *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_3dfoht6 *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_5iksc4v(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_5iksc4v F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_33edc64 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_33edc64 *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_33edc64 *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_5ldr0dk(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_5ldr0dk F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_jf46ta8 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_jf46ta8 *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_jf46ta8 *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_5ts1aan(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_5ts1aan F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_3mrt8ba *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_3mrt8ba *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_3mrt8ba *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_64h5o83(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_64h5o83 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_j4le8o0 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_j4le8o0 *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_j4le8o0 *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_6etic8u(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_6etic8u F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_j07a6mr *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_j07a6mr *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_j07a6mr *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_6r4ifnd(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_6r4ifnd F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_37g0gks *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_37g0gks *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_37g0gks *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_79inkgl(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_79inkgl F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_ja2c5em *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_ja2c5em *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_ja2c5em *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_9d1icfo(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_9d1icfo F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_jr8ev5r *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_jr8ev5r *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_jr8ev5r *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_9o0kvur(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_9o0kvur F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_jlajof4 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_jlajof4 *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_jlajof4 *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_aqoh8f0(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_aqoh8f0 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_3fe7hc3 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_3fe7hc3 *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_3fe7hc3 *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_arnu6be(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_arnu6be F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_jc3ftg7 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_jc3ftg7 *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_jc3ftg7 *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_b108j07(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_b108j07 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_j0ggm04 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_j0ggm04 *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_j0ggm04 *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_beikmkp(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_beikmkp F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_3tnvb6a *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_3tnvb6a *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_3tnvb6a *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_bgc9qfh(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_bgc9qfh F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_jhm2032 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_jhm2032 *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_jhm2032 *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_bmj7akp(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_bmj7akp F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_jp4udcs *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_jp4udcs *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_jp4udcs *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_boqd22f(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_boqd22f F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_jrdns83 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_jrdns83 *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_jrdns83 *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_bq2i257(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_bq2i257 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_jtr5odb *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_jtr5odb *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_jtr5odb *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_c0ffr5d(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_c0ffr5d F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_jv6n7oh *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_jv6n7oh *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_jv6n7oh *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_c20aog9(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_c20aog9 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_317bju7 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_317bju7 *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_317bju7 *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_cmjmpc7(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_cmjmpc7 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_jicque0 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_jicque0 *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_jicque0 *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_d16m383(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_d16m383 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_3rm05av *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_3rm05av *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_3rm05av *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_dh50lhg(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_dh50lhg F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_32r56ja *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_32r56ja *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_32r56ja *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_dvaqvg7(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_dvaqvg7 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_j8m87ti *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_j8m87ti *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_j8m87ti *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_e3q03rg(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_e3q03rg F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_37aah2s *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_37aah2s *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_37aah2s *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_e94udsb(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_e94udsb F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_3mo2tb7 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_3mo2tb7 *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_3mo2tb7 *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_eaemvpe(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_eaemvpe F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_jdgr48v *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_jdgr48v *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_jdgr48v *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_eal3dae(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_eal3dae F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_j4sks15 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_j4sks15 *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_j4sks15 *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_eblih6b(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_eblih6b F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_3tnlp8q *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_3tnlp8q *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_3tnlp8q *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_f9l6pke(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_f9l6pke F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_3o89hhj *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_3o89hhj *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_3o89hhj *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_fi9qngm(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_fi9qngm F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_3aass3a *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_3aass3a *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_3aass3a *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_fm5di8s(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_fm5di8s F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_jp1qc7o *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_jp1qc7o *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_jp1qc7o *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_hjt9q04(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_hjt9q04 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_jj2aipm *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_jj2aipm *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_jj2aipm *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_ih2c9se(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_ih2c9se F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_32n178h *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_32n178h *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_32n178h *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_ih3upe1(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_ih3upe1 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_jb19283 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_jb19283 *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_jb19283 *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_ijni684(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_ijni684 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_359meqs *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_359meqs *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_359meqs *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_j86852b(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_j86852b F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_3itad0c *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_3itad0c *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_3itad0c *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_jmu3b35(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_jmu3b35 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_3pk07bs *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_3pk07bs *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_3pk07bs *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_kbiuvc2(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_kbiuvc2 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_j0i128e *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_j0i128e *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_j0i128e *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_korree9(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_korree9 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_3s3fqnk *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_3s3fqnk *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_3s3fqnk *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_l0bcltv(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_l0bcltv F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_3j59e2u *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_3j59e2u *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_3j59e2u *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_l2f2ej8(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_l2f2ej8 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_jn4ifp1 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_jn4ifp1 *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_jn4ifp1 *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_ldl1hcr(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_ldl1hcr F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_3vtl3te *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_3vtl3te *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_3vtl3te *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_lkdcm3n(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_lkdcm3n F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_34ti291 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_34ti291 *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_34ti291 *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_lom5i6m(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_lom5i6m F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_j44ni19 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_j44ni19 *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_j44ni19 *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_m79rh95(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_m79rh95 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_jm4dd7q *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_jm4dd7q *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_jm4dd7q *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_nc9pofj(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_nc9pofj F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_3pfhto5 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_3pfhto5 *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_3pfhto5 *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_ncrh922(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_ncrh922 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_3ifsvek *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_3ifsvek *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_3ifsvek *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_ngj5ii9(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_ngj5ii9 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_jfku65e *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_jfku65e *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_jfku65e *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_nqvgqmm(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_nqvgqmm F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_3bbfhut *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_3bbfhut *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_3bbfhut *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_nvpijsp(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_nvpijsp F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_3ahqitg *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_3ahqitg *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_3ahqitg *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_o9nv4oe(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_o9nv4oe F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_32bp3b4 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_32bp3b4 *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_32bp3b4 *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_oct8fdl(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_oct8fdl F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_3tqcmdm *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_3tqcmdm *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_3tqcmdm *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_ohjb0ee(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_ohjb0ee F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_jrngvku *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_jrngvku *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_jrngvku *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_oiu5u5h(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_oiu5u5h F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_jpvhgp3 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_jpvhgp3 *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_jpvhgp3 *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_okqu94u(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_okqu94u F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_jp1a7ai *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_jp1a7ai *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_jp1a7ai *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_pur3nlu(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_pur3nlu F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_jaikcua *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_jaikcua *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_jaikcua *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_q5enbli(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_q5enbli F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_jagvq3q *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_jagvq3q *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_jagvq3q *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_qg91ote(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_qg91ote F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_3g143bv *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_3g143bv *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_3g143bv *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_qhpnm5h(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_qhpnm5h F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_32qgu0c *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_32qgu0c *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_32qgu0c *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_qo3jtdm(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_qo3jtdm F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_3qphnbe *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_3qphnbe *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_3qphnbe *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_r6gf71l(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_r6gf71l F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_35to3im *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_35to3im *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_35to3im *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_rhf7rqh(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_rhf7rqh F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_jru5qda *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_jru5qda *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_jru5qda *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_rj4kesu(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_rj4kesu F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_je4jsfp *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_je4jsfp *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_je4jsfp *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_rl156nv(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_rl156nv F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_3vgp4uu *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_3vgp4uu *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_3vgp4uu *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_sdgejrh(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_sdgejrh F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_jpvl2js *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_jpvl2js *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_jpvl2js *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_sss9ick(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_sss9ick F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_3buamil *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_3buamil *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_3buamil *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_su6p3o7(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_su6p3o7 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_331tgtc *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_331tgtc *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_331tgtc *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_tddvoln(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_tddvoln F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_3s8s1g6 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_3s8s1g6 *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_3s8s1g6 *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_toflidg(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_toflidg F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_3pa9rer *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_3pa9rer *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_3pa9rer *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_tr10ktn(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_tr10ktn F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_j7jgble *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_j7jgble *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_j7jgble *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_uavlrmf(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_uavlrmf F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_jvf1ma2 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_jvf1ma2 *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_jvf1ma2 *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_udd1af4(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_udd1af4 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_3aao56v *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_3aao56v *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_3aao56v *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_ulelml8(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_ulelml8 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_jo1gjr5 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_jo1gjr5 *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_jo1gjr5 *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_umkmebp(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_umkmebp F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_3nkc0o4 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_3nkc0o4 *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_3nkc0o4 *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_vmupu7q(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_vmupu7q F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_jha63mc *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_jha63mc *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_jha63mc *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_vqiit51(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_vqiit51 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_jm2cnr1 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_jm2cnr1 *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_jm2cnr1 *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
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
inline __attribute__((aligned(4))) uint8_t gm_f_to_lower_0fu6efk(uint8_t a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_to_lower_0fu6efk F;
    uint8_t gm_l0; /* c */
    GM_STACK_CHECK_SHALLOW(&F, 11u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t t1;
    uint8_t t3 = gm_l0;
    uint8_t t4 = gm_f_is_upper_iekrftp(t3);
    if (!t4) goto ie2;
    uint8_t t6 = gm_l0;
    uint8_t r7; bool o8 = __builtin_add_overflow(t6, ((uint8_t)32ULL), &r7);
    if (o8) gm_trap("integer overflow in `+`", gm_srci5ni72s6oj5f, 42, 53);
    uint8_t t9 = r7;
    uint8_t t10 = t9;
    t1 = t10;
    goto ix2;
ie2:;
    uint8_t t12 = gm_l0;
    uint8_t t13 = t12;
    t1 = t13;
ix2:;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_0736hae(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_1j3gkem(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_1tiq8j9(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_2oeivui(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_2r7l0dc(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_37pdbfb(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_3psqmn1(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_3vsp7ft(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_472aruh(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_4pkhf4j(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_52crvc7(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_56r3vur(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_599rnh9(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_5ha0v65(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_5iksc4v(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_5ldr0dk(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_5ts1aan(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_64h5o83(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_6etic8u(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_6r4ifnd(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_79inkgl(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_9d1icfo(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_9o0kvur(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_aqoh8f0(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_arnu6be(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_b108j07(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_beikmkp(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_bgc9qfh(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_bmj7akp(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_boqd22f(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_bq2i257(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_c0ffr5d(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_c20aog9(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_cmjmpc7(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_d16m383(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_dh50lhg(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_dvaqvg7(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_e3q03rg(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_e94udsb(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_eaemvpe(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_eal3dae(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_eblih6b(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_f9l6pke(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_fi9qngm(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_fm5di8s(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_hjt9q04(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_ih2c9se(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_ih3upe1(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_ijni684(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_j86852b(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_jmu3b35(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_kbiuvc2(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_korree9(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_l0bcltv(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_l2f2ej8(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_ldl1hcr(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_lkdcm3n(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_lom5i6m(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_m79rh95(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_nc9pofj(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_ncrh922(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_ngj5ii9(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_nqvgqmm(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_nvpijsp(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_o9nv4oe(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_oct8fdl(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_ohjb0ee(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_oiu5u5h(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_okqu94u(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_pur3nlu(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_q5enbli(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_qg91ote(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_qhpnm5h(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_qo3jtdm(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_r6gf71l(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_rhf7rqh(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_rj4kesu(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_rl156nv(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_sdgejrh(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_sss9ick(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_su6p3o7(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_tddvoln(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_toflidg(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_tr10ktn(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_uavlrmf(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_udd1af4(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_ulelml8(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_umkmebp(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_vmupu7q(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_vqiit51(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) void gm_f_format_317bju7(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_317bju7 F;
    gm_t3877pk7lv748 gm_l0; /* args */
    gm_t3877pk7lv748 gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 345u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s05crl6fa1at4));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(5));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)24ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t3877pk7lv748 t18 = GM_LD(gm_t3877pk7lv748, ((uint8_t *)a0));
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_c20aog9, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            uint64_t t22 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 16));
            uint64_t t23 = t22;
            gm_closure t24 = t19;
            gm_node *ccx25 = t24.cx;
            if (gm_pfk8csn7us23(t24.code, t24.env, ccx25, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_sajhum9b4nr96, 1)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t24.code, t24.env, ccx25, &gm_tyb2nno7n67r, ((uint8_t *)&t23), NULL)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t27 = F.l2;
        if (!t27) goto ie26;
        {
            void * t29 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t29);
        }
        {
            gm_t9qkh0rsooc8d t30;
            GM_ST(uint8_t, ((uint8_t *)&t30 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t31;
            GM_ST(uint8_t, (uint8_t *)&t31 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t31 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t31 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t31 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t31 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t31 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t31 + 16, 0);
            ((uint8_t *)&t31)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t31 + 8), t30);
            gm_tabb710d9mq2e t32 = t31;
            uint8_t t33 = ((((uint8_t *)&t32))[0] != 0);
            F.ret = t31;
            goto gm_fret;
        }
        goto ix26;
ie26:;
ix26:;
    }
    {
        void * t34 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t34);
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
inline __attribute__((aligned(4))) void gm_f_format_32bp3b4(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_32bp3b4 F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 305u);
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
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_st02nolqdtiev));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(8));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)16ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_o9nv4oe, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_closure t22 = t19;
            gm_node *ccx23 = t22.cx;
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_snbque26o7lbi, 6)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t22.code, t22.env, ccx23, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 8), t28);
            gm_tabb710d9mq2e t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t32);
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
inline __attribute__((aligned(4))) void gm_f_format_32n178h(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0) {
    struct gm_fr_format_32n178h F;
    gm_t88rcucg1b7uk gm_l0; /* args */
    gm_t88rcucg1b7uk gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 245u);
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
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s4pru1na1k33s));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(2));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)4ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t88rcucg1b7uk t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_ih2c9se, (void *)&F.h, cx});
            uint32_t t20 = GM_LD(uint32_t, (((uint8_t *)&gm_l3) + 0));
            uint32_t t21 = t20;
            gm_closure t22 = t19;
            gm_node *ccx23 = t22.cx;
            if (gm_pfk8csn7us23(t22.code, t22.env, ccx23, &gm_ty467d5cm1d5, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 8), t28);
            gm_tabb710d9mq2e t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t32);
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
inline __attribute__((aligned(4))) void gm_f_format_32qgu0c(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_32qgu0c F;
    GM_STACK_CHECK(&F, 369u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_skaimjkudbui9));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(38));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)48ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tfeab279c16rr t18 = GM_LD(gm_tfeab279c16rr, ((uint8_t *)a0));
            F.l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_qhpnm5h, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
            gm_b16_8 t21 = t20;
            gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 16));
            gm_b16_8 t23 = t22;
            gm_b16_8 t24 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 32));
            gm_b16_8 t25 = t24;
            gm_closure t26 = t19;
            gm_node *ccx27 = t26.cx;
            if (gm_pfk8csn7us23(t26.code, t26.env, ccx27, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t26.code, t26.env, ccx27, gm_s7vsso7k8tg8d, 24)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t26.code, t26.env, ccx27, &gm_ty0iqiubfm7e, ((uint8_t *)&t23), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t26.code, t26.env, ccx27, gm_sp7cdd0n91vli, 7)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t26.code, t26.env, ccx27, &gm_ty0iqiubfm7e, ((uint8_t *)&t25), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t26.code, t26.env, ccx27, gm_sfm9elkq6bn2e, 1)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t29 = F.l2;
        if (!t29) goto ie28;
        {
            void * t31 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t31);
        }
        {
            gm_t9qkh0rsooc8d t32;
            GM_ST(uint8_t, ((uint8_t *)&t32 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t33;
            GM_ST(uint8_t, (uint8_t *)&t33 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t33 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t33 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t33 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t33 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t33 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t33 + 16, 0);
            ((uint8_t *)&t33)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t33 + 8), t32);
            gm_tabb710d9mq2e t34 = t33;
            uint8_t t35 = ((((uint8_t *)&t34))[0] != 0);
            F.ret = t33;
            goto gm_fret;
        }
        goto ix28;
ie28:;
ix28:;
    }
    {
        void * t36 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t36);
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
inline __attribute__((aligned(4))) void gm_f_format_32r56ja(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_32r56ja F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 305u);
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
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s68lv7o2uide3));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(60));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)16ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_dh50lhg, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_closure t22 = t19;
            gm_node *ccx23 = t22.cx;
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_sluiboi2c0d5n, 57)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t22.code, t22.env, ccx23, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_sfm9elkq6bn2e, 1)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 8), t28);
            gm_tabb710d9mq2e t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t32);
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
inline __attribute__((aligned(4))) void gm_f_format_331tgtc(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_331tgtc F;
    GM_STACK_CHECK(&F, 369u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sldmbttac8k2j));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(58));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)48ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tfeab279c16rr t18 = GM_LD(gm_tfeab279c16rr, ((uint8_t *)a0));
            F.l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_su6p3o7, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
            gm_b16_8 t21 = t20;
            gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 16));
            gm_b16_8 t23 = t22;
            gm_b16_8 t24 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 32));
            gm_b16_8 t25 = t24;
            gm_closure t26 = t19;
            gm_node *ccx27 = t26.cx;
            if (gm_pf0aqhubr4up(t26.code, t26.env, ccx27, gm_sfm9elkq6bn2e, 1)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t26.code, t26.env, ccx27, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t26.code, t26.env, ccx27, gm_shn9bhhpahcjo, 15)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t26.code, t26.env, ccx27, &gm_ty0iqiubfm7e, ((uint8_t *)&t23), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t26.code, t26.env, ccx27, gm_s088gqlg957uo, 5)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t26.code, t26.env, ccx27, &gm_ty0iqiubfm7e, ((uint8_t *)&t25), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t26.code, t26.env, ccx27, gm_st4gkaq24fvj9, 31)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t29 = F.l2;
        if (!t29) goto ie28;
        {
            void * t31 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t31);
        }
        {
            gm_t9qkh0rsooc8d t32;
            GM_ST(uint8_t, ((uint8_t *)&t32 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t33;
            GM_ST(uint8_t, (uint8_t *)&t33 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t33 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t33 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t33 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t33 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t33 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t33 + 16, 0);
            ((uint8_t *)&t33)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t33 + 8), t32);
            gm_tabb710d9mq2e t34 = t33;
            uint8_t t35 = ((((uint8_t *)&t34))[0] != 0);
            F.ret = t33;
            goto gm_fret;
        }
        goto ix28;
ie28:;
ix28:;
    }
    {
        void * t36 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t36);
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
inline __attribute__((aligned(4))) void gm_f_format_33edc64(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_33edc64 F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 305u);
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
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s15g40fmv51gv));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(58));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)16ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_5iksc4v, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_closure t22 = t19;
            gm_node *ccx23 = t22.cx;
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_sut7mhrkhqp77, 55)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t22.code, t22.env, ccx23, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_sfm9elkq6bn2e, 1)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 8), t28);
            gm_tabb710d9mq2e t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t32);
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
inline __attribute__((aligned(4))) void gm_f_format_34ti291(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_34ti291 F;
    GM_STACK_CHECK(&F, 345u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sn0785fueq6ki));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(63));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)40ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t7neromnu8t62 t18 = GM_LD(gm_t7neromnu8t62, ((uint8_t *)a0));
            F.l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_lkdcm3n, (void *)&F.h, cx});
            uint64_t t20 = GM_LD(uint64_t, (((uint8_t *)&F.l3) + 0));
            uint64_t t21 = t20;
            gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 8));
            gm_b16_8 t23 = t22;
            gm_b16_8 t24 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 24));
            gm_b16_8 t25 = t24;
            gm_closure t26 = t19;
            gm_node *ccx27 = t26.cx;
            if (gm_pf0aqhubr4up(t26.code, t26.env, ccx27, gm_svirf1u8t89n0, 46)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t26.code, t26.env, ccx27, &gm_ty6groq37ram, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t26.code, t26.env, ccx27, gm_sp02qqgucmpt7, 2)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t26.code, t26.env, ccx27, &gm_tybihgc69jv3, ((uint8_t *)&t23), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t26.code, t26.env, ccx27, gm_s6sv4vhunlp6l, 8)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t26.code, t26.env, ccx27, &gm_tybihgc69jv3, ((uint8_t *)&t25), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t26.code, t26.env, ccx27, gm_s1asl1e1tvb0k, 1)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t29 = F.l2;
        if (!t29) goto ie28;
        {
            void * t31 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t31);
        }
        {
            gm_t9qkh0rsooc8d t32;
            GM_ST(uint8_t, ((uint8_t *)&t32 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t33;
            GM_ST(uint8_t, (uint8_t *)&t33 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t33 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t33 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t33 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t33 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t33 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t33 + 16, 0);
            ((uint8_t *)&t33)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t33 + 8), t32);
            gm_tabb710d9mq2e t34 = t33;
            uint8_t t35 = ((((uint8_t *)&t34))[0] != 0);
            F.ret = t33;
            goto gm_fret;
        }
        goto ix28;
ie28:;
ix28:;
    }
    {
        void * t36 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t36);
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
inline __attribute__((aligned(4))) void gm_f_format_359meqs(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_359meqs F;
    GM_STACK_CHECK(&F, 369u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sf1ljria5bdei));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(7));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)48ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tfeab279c16rr t18 = GM_LD(gm_tfeab279c16rr, ((uint8_t *)a0));
            F.l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_ijni684, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
            gm_b16_8 t21 = t20;
            gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 16));
            gm_b16_8 t23 = t22;
            gm_b16_8 t24 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 32));
            gm_b16_8 t25 = t24;
            gm_closure t26 = t19;
            gm_node *ccx27 = t26.cx;
            if (gm_pfk8csn7us23(t26.code, t26.env, ccx27, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t26.code, t26.env, ccx27, &gm_ty0iqiubfm7e, ((uint8_t *)&t23), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t26.code, t26.env, ccx27, gm_so514oj5nbhcu, 1)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t26.code, t26.env, ccx27, &gm_ty0iqiubfm7e, ((uint8_t *)&t25), NULL)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t29 = F.l2;
        if (!t29) goto ie28;
        {
            void * t31 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t31);
        }
        {
            gm_t9qkh0rsooc8d t32;
            GM_ST(uint8_t, ((uint8_t *)&t32 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t33;
            GM_ST(uint8_t, (uint8_t *)&t33 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t33 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t33 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t33 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t33 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t33 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t33 + 16, 0);
            ((uint8_t *)&t33)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t33 + 8), t32);
            gm_tabb710d9mq2e t34 = t33;
            uint8_t t35 = ((((uint8_t *)&t34))[0] != 0);
            F.ret = t33;
            goto gm_fret;
        }
        goto ix28;
ie28:;
ix28:;
    }
    {
        void * t36 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t36);
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
inline __attribute__((aligned(4))) void gm_f_format_35to3im(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_35to3im F;
    GM_STACK_CHECK(&F, 369u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sjvtpaes95ji4));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(26));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)48ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tfeab279c16rr t18 = GM_LD(gm_tfeab279c16rr, ((uint8_t *)a0));
            F.l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_r6gf71l, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
            gm_b16_8 t21 = t20;
            gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 16));
            gm_b16_8 t23 = t22;
            gm_b16_8 t24 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 32));
            gm_b16_8 t25 = t24;
            gm_closure t26 = t19;
            gm_node *ccx27 = t26.cx;
            if (gm_pf0aqhubr4up(t26.code, t26.env, ccx27, gm_so9fi6r28770a, 2)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t26.code, t26.env, ccx27, &gm_tybihgc69jv3, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t26.code, t26.env, ccx27, gm_sveguffvp7vd6, 14)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t26.code, t26.env, ccx27, &gm_ty0iqiubfm7e, ((uint8_t *)&t23), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t26.code, t26.env, ccx27, gm_sr7ga013iei9a, 3)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t26.code, t26.env, ccx27, &gm_tybihgc69jv3, ((uint8_t *)&t25), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t26.code, t26.env, ccx27, gm_s1asl1e1tvb0k, 1)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t29 = F.l2;
        if (!t29) goto ie28;
        {
            void * t31 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t31);
        }
        {
            gm_t9qkh0rsooc8d t32;
            GM_ST(uint8_t, ((uint8_t *)&t32 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t33;
            GM_ST(uint8_t, (uint8_t *)&t33 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t33 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t33 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t33 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t33 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t33 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t33 + 16, 0);
            ((uint8_t *)&t33)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t33 + 8), t32);
            gm_tabb710d9mq2e t34 = t33;
            uint8_t t35 = ((((uint8_t *)&t34))[0] != 0);
            F.ret = t33;
            goto gm_fret;
        }
        goto ix28;
ie28:;
ix28:;
    }
    {
        void * t36 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t36);
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
inline __attribute__((aligned(4))) void gm_f_format_3669u6h(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0) {
    struct gm_fr_format_3669u6h F;
    gm_t88rcucg1b7uk gm_l0; /* args */
    gm_t88rcucg1b7uk gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 245u);
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
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sc092629f9on4));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(14));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)4ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t88rcucg1b7uk t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_2oeivui, (void *)&F.h, cx});
            uint32_t t20 = GM_LD(uint32_t, (((uint8_t *)&gm_l3) + 0));
            uint32_t t21 = t20;
            gm_closure t22 = t19;
            gm_node *ccx23 = t22.cx;
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_ss9d6rvgfqq53, 12)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t22.code, t22.env, ccx23, &gm_ty467d5cm1d5, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 8), t28);
            gm_tabb710d9mq2e t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t32);
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
inline __attribute__((aligned(4))) void gm_f_format_368205k(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0) {
    struct gm_fr_format_368205k F;
    gm_t4ce0jd3tdro4 gm_l0; /* args */
    gm_t4ce0jd3tdro4 gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 265u);
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
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_se20eu14i6umf));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(8));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)8ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t4ce0jd3tdro4 t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_3vsp7ft, (void *)&F.h, cx});
            uint64_t t20 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 0));
            uint64_t t21 = t20;
            gm_closure t22 = t19;
            gm_node *ccx23 = t22.cx;
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_sdtthp48kia81, 4)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t22.code, t22.env, ccx23, &gm_tyb2nno7n67r, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_s26crtotu14pn, 2)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 8), t28);
            gm_tabb710d9mq2e t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t32);
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
inline __attribute__((aligned(4))) void gm_f_format_37aah2s(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_37aah2s F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 385u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sdu7bd5v9q750));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(64));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)32ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t6j6smj7qoctu t18 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_e3q03rg, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 16));
            gm_b16_8 t23 = t22;
            gm_closure t24 = t19;
            gm_node *ccx25 = t24.cx;
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_sltjg0sbhbo73, 12)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t24.code, t24.env, ccx25, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_s86js8lt7ibda, 47)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t24.code, t24.env, ccx25, &gm_ty0iqiubfm7e, ((uint8_t *)&t23), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_s1asl1e1tvb0k, 1)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t27 = F.l2;
        if (!t27) goto ie26;
        {
            void * t29 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t29);
        }
        {
            gm_t9qkh0rsooc8d t30;
            GM_ST(uint8_t, ((uint8_t *)&t30 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t31;
            GM_ST(uint8_t, (uint8_t *)&t31 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t31 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t31 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t31 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t31 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t31 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t31 + 16, 0);
            ((uint8_t *)&t31)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t31 + 8), t30);
            gm_tabb710d9mq2e t32 = t31;
            uint8_t t33 = ((((uint8_t *)&t32))[0] != 0);
            F.ret = t31;
            goto gm_fret;
        }
        goto ix26;
ie26:;
ix26:;
    }
    {
        void * t34 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t34);
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
inline __attribute__((aligned(4))) void gm_f_format_37dbjte(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_37dbjte F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 305u);
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
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sjljm9omp36ob));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(51));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)16ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_472aruh, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_closure t22 = t19;
            gm_node *ccx23 = t22.cx;
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_sfm9elkq6bn2e, 1)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t22.code, t22.env, ccx23, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_sa71447djt33j, 48)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 8), t28);
            gm_tabb710d9mq2e t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t32);
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
inline __attribute__((aligned(4))) void gm_f_format_37g0gks(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_37g0gks F;
    gm_tgbgti82mf879 gm_l0; /* args */
    gm_tgbgti82mf879 gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 385u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_so94vinsu4uib));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(49));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)32ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tgbgti82mf879 t18 = GM_LD(gm_tgbgti82mf879, ((uint8_t *)a0));
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_6r4ifnd, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            uint64_t t22 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 16));
            uint64_t t23 = t22;
            uint64_t t24 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 24));
            uint64_t t25 = t24;
            gm_closure t26 = t19;
            gm_node *ccx27 = t26.cx;
            if (gm_pf0aqhubr4up(t26.code, t26.env, ccx27, gm_sadscuj628cl7, 7)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t26.code, t26.env, ccx27, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t26.code, t26.env, ccx27, gm_snsbh43f5eo46, 4)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t26.code, t26.env, ccx27, &gm_tyb2nno7n67r, ((uint8_t *)&t23), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t26.code, t26.env, ccx27, gm_srs4ljjaijhd2, 23)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t26.code, t26.env, ccx27, &gm_tyb2nno7n67r, ((uint8_t *)&t25), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t26.code, t26.env, ccx27, gm_stobu0djed6ck, 9)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t29 = F.l2;
        if (!t29) goto ie28;
        {
            void * t31 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t31);
        }
        {
            gm_t9qkh0rsooc8d t32;
            GM_ST(uint8_t, ((uint8_t *)&t32 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t33;
            GM_ST(uint8_t, (uint8_t *)&t33 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t33 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t33 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t33 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t33 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t33 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t33 + 16, 0);
            ((uint8_t *)&t33)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t33 + 8), t32);
            gm_tabb710d9mq2e t34 = t33;
            uint8_t t35 = ((((uint8_t *)&t34))[0] != 0);
            F.ret = t33;
            goto gm_fret;
        }
        goto ix28;
ie28:;
ix28:;
    }
    {
        void * t36 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t36);
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
inline __attribute__((aligned(4))) void gm_f_format_38m1q8c(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t2lqu8jsj511d a0) {
    struct gm_fr_format_38m1q8c F;
    gm_t2lqu8jsj511d gm_l0; /* args */
    gm_t2lqu8jsj511d gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 305u);
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
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_skagf3vj30re5));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(16));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)16ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t2lqu8jsj511d t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_52crvc7, (void *)&F.h, cx});
            uint64_t t20 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 0));
            uint64_t t21 = t20;
            uint64_t t22 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
            uint64_t t23 = t22;
            gm_closure t24 = t19;
            gm_node *ccx25 = t24.cx;
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_s38cjdjl7n99k, 9)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t24.code, t24.env, ccx25, &gm_ty6groq37ram, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_sk754mj3p1el6, 1)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t24.code, t24.env, ccx25, &gm_tyb2nno7n67r, ((uint8_t *)&t23), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_sfm8ps2e26dh1, 2)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t27 = F.l2;
        if (!t27) goto ie26;
        {
            void * t29 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t29);
        }
        {
            gm_t9qkh0rsooc8d t30;
            GM_ST(uint8_t, ((uint8_t *)&t30 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t31;
            GM_ST(uint8_t, (uint8_t *)&t31 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t31 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t31 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t31 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t31 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t31 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t31 + 16, 0);
            ((uint8_t *)&t31)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t31 + 8), t30);
            gm_tabb710d9mq2e t32 = t31;
            uint8_t t33 = ((((uint8_t *)&t32))[0] != 0);
            F.ret = t31;
            goto gm_fret;
        }
        goto ix26;
ie26:;
ix26:;
    }
    {
        void * t34 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t34);
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
inline __attribute__((aligned(4))) void gm_f_format_39snre1(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_39snre1 F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 305u);
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
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_suqqp23fdh2ff));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(5));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)16ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_1j3gkem, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_closure t22 = t19;
            gm_node *ccx23 = t22.cx;
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_s7ipvt5onv6ht, 3)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t22.code, t22.env, ccx23, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 8), t28);
            gm_tabb710d9mq2e t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t32);
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
inline __attribute__((aligned(4))) void gm_f_format_3aao56v(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_3aao56v F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 385u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sdrrh4gk917l3));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(9));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)32ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t6j6smj7qoctu t18 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_udd1af4, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 16));
            gm_b16_8 t23 = t22;
            gm_closure t24 = t19;
            gm_node *ccx25 = t24.cx;
            if (gm_pfk8csn7us23(t24.code, t24.env, ccx25, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_svc1hcla4tqfb, 5)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t24.code, t24.env, ccx25, &gm_ty0iqiubfm7e, ((uint8_t *)&t23), NULL)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t27 = F.l2;
        if (!t27) goto ie26;
        {
            void * t29 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t29);
        }
        {
            gm_t9qkh0rsooc8d t30;
            GM_ST(uint8_t, ((uint8_t *)&t30 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t31;
            GM_ST(uint8_t, (uint8_t *)&t31 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t31 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t31 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t31 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t31 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t31 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t31 + 16, 0);
            ((uint8_t *)&t31)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t31 + 8), t30);
            gm_tabb710d9mq2e t32 = t31;
            uint8_t t33 = ((((uint8_t *)&t32))[0] != 0);
            F.ret = t31;
            goto gm_fret;
        }
        goto ix26;
ie26:;
ix26:;
    }
    {
        void * t34 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t34);
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
inline __attribute__((aligned(4))) void gm_f_format_3aass3a(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_3aass3a F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 305u);
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
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s9r72jgd5hb05));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(4));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)16ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_fi9qngm, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_closure t22 = t19;
            gm_node *ccx23 = t22.cx;
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_s17uhrtdo7u30, 1)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t22.code, t22.env, ccx23, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_s17uhrtdo7u30, 1)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 8), t28);
            gm_tabb710d9mq2e t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t32);
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
inline __attribute__((aligned(4))) void gm_f_format_3ahqitg(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_3ahqitg F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 305u);
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
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s98novtk49ooh));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(27));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)16ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_nvpijsp, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_closure t22 = t19;
            gm_node *ccx23 = t22.cx;
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_sqs6r7f5gl2vu, 25)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t22.code, t22.env, ccx23, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 8), t28);
            gm_tabb710d9mq2e t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t32);
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
inline __attribute__((aligned(4))) void gm_f_format_3bbfhut(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_3bbfhut F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 305u);
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
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sf20puqngp9vm));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(8));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)16ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_nqvgqmm, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_closure t22 = t19;
            gm_node *ccx23 = t22.cx;
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_s7hndfpk1u0p0, 6)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t22.code, t22.env, ccx23, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 8), t28);
            gm_tabb710d9mq2e t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t32);
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
inline __attribute__((aligned(4))) void gm_f_format_3buamil(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_3buamil F;
    gm_t3877pk7lv748 gm_l0; /* args */
    gm_t3877pk7lv748 gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 345u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sir2n5292lks8));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(7));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)24ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t3877pk7lv748 t18 = GM_LD(gm_t3877pk7lv748, ((uint8_t *)a0));
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_sss9ick, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            uint64_t t22 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 16));
            uint64_t t23 = t22;
            gm_closure t24 = t19;
            gm_node *ccx25 = t24.cx;
            if (gm_pfk8csn7us23(t24.code, t24.env, ccx25, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_sefk7cselekki, 2)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t24.code, t24.env, ccx25, &gm_ty6groq37ram, ((uint8_t *)&t23), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_spod3tmkh25up, 1)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t27 = F.l2;
        if (!t27) goto ie26;
        {
            void * t29 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t29);
        }
        {
            gm_t9qkh0rsooc8d t30;
            GM_ST(uint8_t, ((uint8_t *)&t30 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t31;
            GM_ST(uint8_t, (uint8_t *)&t31 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t31 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t31 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t31 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t31 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t31 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t31 + 16, 0);
            ((uint8_t *)&t31)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t31 + 8), t30);
            gm_tabb710d9mq2e t32 = t31;
            uint8_t t33 = ((((uint8_t *)&t32))[0] != 0);
            F.ret = t31;
            goto gm_fret;
        }
        goto ix26;
ie26:;
ix26:;
    }
    {
        void * t34 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t34);
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
inline __attribute__((aligned(4))) void gm_f_format_3dfoht6(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_3dfoht6 F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 305u);
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
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sk0kqh2pa96ut));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(72));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)16ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_5ha0v65, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_closure t22 = t19;
            gm_node *ccx23 = t22.cx;
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_su5bsj1nioctr, 21)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t22.code, t22.env, ccx23, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_skfo9c0nr3nrd, 49)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 8), t28);
            gm_tabb710d9mq2e t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t32);
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
inline __attribute__((aligned(4))) void gm_f_format_3fe7hc3(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_3fe7hc3 F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 305u);
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
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_speojrsjsem8a));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(49));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)16ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_aqoh8f0, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_closure t22 = t19;
            gm_node *ccx23 = t22.cx;
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_s4bcn9pd0h83n, 47)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t22.code, t22.env, ccx23, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 8), t28);
            gm_tabb710d9mq2e t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t32);
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
inline __attribute__((aligned(4))) void gm_f_format_3g143bv(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_3g143bv F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 305u);
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
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sgpgu8iag22ja));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(14));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)16ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_qg91ote, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_closure t22 = t19;
            gm_node *ccx23 = t22.cx;
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_s1bpuqd44lur7, 6)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t22.code, t22.env, ccx23, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_sd2mrmucl6ngt, 6)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 8), t28);
            gm_tabb710d9mq2e t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t32);
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
inline __attribute__((aligned(4))) void gm_f_format_3ifsvek(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_3ifsvek F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 305u);
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
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sg58j44mq0qb5));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(60));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)16ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_ncrh922, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_closure t22 = t19;
            gm_node *ccx23 = t22.cx;
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_sfm9elkq6bn2e, 1)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t22.code, t22.env, ccx23, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_s9indmpqvpug4, 57)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 8), t28);
            gm_tabb710d9mq2e t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t32);
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
inline __attribute__((aligned(4))) void gm_f_format_3itad0c(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0) {
    struct gm_fr_format_3itad0c F;
    gm_t88rcucg1b7uk gm_l0; /* args */
    gm_t88rcucg1b7uk gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 245u);
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
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sqqh9r6c17rju));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(6));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)4ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t88rcucg1b7uk t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_j86852b, (void *)&F.h, cx});
            uint32_t t20 = GM_LD(uint32_t, (((uint8_t *)&gm_l3) + 0));
            uint32_t t21 = t20;
            gm_closure t22 = t19;
            gm_node *ccx23 = t22.cx;
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_sb6j3egm9oljk, 4)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t22.code, t22.env, ccx23, &gm_ty467d5cm1d5, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 8), t28);
            gm_tabb710d9mq2e t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t32);
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
inline __attribute__((aligned(4))) void gm_f_format_3j59e2u(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_3j59e2u F;
    GM_STACK_CHECK(&F, 465u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sgahfuanpfk2r));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(59));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)80ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tud7oovfvnf43 t18 = GM_LD(gm_tud7oovfvnf43, ((uint8_t *)a0));
            F.l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_l0bcltv, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
            gm_b16_8 t21 = t20;
            gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 16));
            gm_b16_8 t23 = t22;
            gm_b16_8 t24 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 32));
            gm_b16_8 t25 = t24;
            gm_b16_8 t26 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 48));
            gm_b16_8 t27 = t26;
            gm_b16_8 t28 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 64));
            gm_b16_8 t29 = t28;
            gm_closure t30 = t19;
            gm_node *ccx31 = t30.cx;
            if (gm_pf0aqhubr4up(t30.code, t30.env, ccx31, gm_smooaouqmm8dj, 1)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t30.code, t30.env, ccx31, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t30.code, t30.env, ccx31, gm_s9b81q4kouna6, 10)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t30.code, t30.env, ccx31, &gm_ty0iqiubfm7e, ((uint8_t *)&t23), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t30.code, t30.env, ccx31, gm_s8pvtaofn65p9, 28)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t30.code, t30.env, ccx31, &gm_ty0iqiubfm7e, ((uint8_t *)&t25), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t30.code, t30.env, ccx31, gm_sc24a25pi012e, 7)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t30.code, t30.env, ccx31, &gm_ty0iqiubfm7e, ((uint8_t *)&t27), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t30.code, t30.env, ccx31, gm_snukl6jqep6jf, 3)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t30.code, t30.env, ccx31, &gm_ty0iqiubfm7e, ((uint8_t *)&t29), NULL)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t33 = F.l2;
        if (!t33) goto ie32;
        {
            void * t35 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t35);
        }
        {
            gm_t9qkh0rsooc8d t36;
            GM_ST(uint8_t, ((uint8_t *)&t36 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t37;
            GM_ST(uint8_t, (uint8_t *)&t37 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t37 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t37 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t37 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t37 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t37 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t37 + 16, 0);
            ((uint8_t *)&t37)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t37 + 8), t36);
            gm_tabb710d9mq2e t38 = t37;
            uint8_t t39 = ((((uint8_t *)&t38))[0] != 0);
            F.ret = t37;
            goto gm_fret;
        }
        goto ix32;
ie32:;
ix32:;
    }
    {
        void * t40 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t40);
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
inline __attribute__((aligned(4))) void gm_f_format_3mj8462(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_3mj8462 F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 385u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s0ljc8tssh8f7));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(7));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)32ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t6j6smj7qoctu t18 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_56r3vur, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 16));
            gm_b16_8 t23 = t22;
            gm_closure t24 = t19;
            gm_node *ccx25 = t24.cx;
            if (gm_pfk8csn7us23(t24.code, t24.env, ccx25, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_s22tv9c5r20tl, 3)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t24.code, t24.env, ccx25, &gm_ty0iqiubfm7e, ((uint8_t *)&t23), NULL)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t27 = F.l2;
        if (!t27) goto ie26;
        {
            void * t29 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t29);
        }
        {
            gm_t9qkh0rsooc8d t30;
            GM_ST(uint8_t, ((uint8_t *)&t30 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t31;
            GM_ST(uint8_t, (uint8_t *)&t31 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t31 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t31 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t31 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t31 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t31 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t31 + 16, 0);
            ((uint8_t *)&t31)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t31 + 8), t30);
            gm_tabb710d9mq2e t32 = t31;
            uint8_t t33 = ((((uint8_t *)&t32))[0] != 0);
            F.ret = t31;
            goto gm_fret;
        }
        goto ix26;
ie26:;
ix26:;
    }
    {
        void * t34 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t34);
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
inline __attribute__((aligned(4))) void gm_f_format_3mo2tb7(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_3mo2tb7 F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 385u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s7j8d035g0p9i));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(37));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)32ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t6j6smj7qoctu t18 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_e94udsb, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 16));
            gm_b16_8 t23 = t22;
            gm_closure t24 = t19;
            gm_node *ccx25 = t24.cx;
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_s4oil3epcv041, 2)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t24.code, t24.env, ccx25, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_sdpo2hjlq8e9b, 30)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t24.code, t24.env, ccx25, &gm_ty0iqiubfm7e, ((uint8_t *)&t23), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_sfm9elkq6bn2e, 1)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t27 = F.l2;
        if (!t27) goto ie26;
        {
            void * t29 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t29);
        }
        {
            gm_t9qkh0rsooc8d t30;
            GM_ST(uint8_t, ((uint8_t *)&t30 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t31;
            GM_ST(uint8_t, (uint8_t *)&t31 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t31 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t31 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t31 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t31 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t31 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t31 + 16, 0);
            ((uint8_t *)&t31)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t31 + 8), t30);
            gm_tabb710d9mq2e t32 = t31;
            uint8_t t33 = ((((uint8_t *)&t32))[0] != 0);
            F.ret = t31;
            goto gm_fret;
        }
        goto ix26;
ie26:;
ix26:;
    }
    {
        void * t34 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t34);
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
inline __attribute__((aligned(4))) void gm_f_format_3mrt8ba(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_3mrt8ba F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 305u);
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
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s26gmgbksn2pm));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(60));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)16ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_5ts1aan, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_closure t22 = t19;
            gm_node *ccx23 = t22.cx;
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_sfm9elkq6bn2e, 1)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t22.code, t22.env, ccx23, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_srd5daqbo6ilc, 57)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 8), t28);
            gm_tabb710d9mq2e t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t32);
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
inline __attribute__((aligned(4))) void gm_f_format_3nkc0o4(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_3nkc0o4 F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 305u);
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
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s1757p1g7h4me));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(41));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)16ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_umkmebp, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_closure t22 = t19;
            gm_node *ccx23 = t22.cx;
            if (gm_pfk8csn7us23(t22.code, t22.env, ccx23, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_sovv48653dnju, 39)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 8), t28);
            gm_tabb710d9mq2e t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t32);
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
inline __attribute__((aligned(4))) void gm_f_format_3o89hhj(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_3o89hhj F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 385u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s519l85f26pmp));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(80));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)32ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t6j6smj7qoctu t18 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_f9l6pke, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 16));
            gm_b16_8 t23 = t22;
            gm_closure t24 = t19;
            gm_node *ccx25 = t24.cx;
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_s16mf4cvajh59, 12)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t24.code, t24.env, ccx25, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_s75m3dt9015lh, 14)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t24.code, t24.env, ccx25, &gm_ty0iqiubfm7e, ((uint8_t *)&t23), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_stddp43pjd9r1, 50)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t27 = F.l2;
        if (!t27) goto ie26;
        {
            void * t29 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t29);
        }
        {
            gm_t9qkh0rsooc8d t30;
            GM_ST(uint8_t, ((uint8_t *)&t30 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t31;
            GM_ST(uint8_t, (uint8_t *)&t31 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t31 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t31 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t31 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t31 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t31 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t31 + 16, 0);
            ((uint8_t *)&t31)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t31 + 8), t30);
            gm_tabb710d9mq2e t32 = t31;
            uint8_t t33 = ((((uint8_t *)&t32))[0] != 0);
            F.ret = t31;
            goto gm_fret;
        }
        goto ix26;
ie26:;
ix26:;
    }
    {
        void * t34 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t34);
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
inline __attribute__((aligned(4))) void gm_f_format_3pa9rer(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_3pa9rer F;
    gm_tbccvikg6b96e gm_l0; /* args */
    gm_tbccvikg6b96e gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 385u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s5i05gtdgdefb));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(41));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)32ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tbccvikg6b96e t18 = GM_LD(gm_tbccvikg6b96e, ((uint8_t *)a0));
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_toflidg, (void *)&F.h, cx});
            uint64_t t20 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 0));
            uint64_t t21 = t20;
            uint64_t t22 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
            uint64_t t23 = t22;
            gm_b16_8 t24 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 16));
            gm_b16_8 t25 = t24;
            gm_closure t26 = t19;
            gm_node *ccx27 = t26.cx;
            if (gm_pf0aqhubr4up(t26.code, t26.env, ccx27, gm_saed870gk04of, 13)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t26.code, t26.env, ccx27, &gm_tyb2nno7n67r, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t26.code, t26.env, ccx27, gm_snenkhfev166s, 2)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t26.code, t26.env, ccx27, &gm_tyb2nno7n67r, ((uint8_t *)&t23), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t26.code, t26.env, ccx27, gm_s3hinmnodmkmv, 20)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t26.code, t26.env, ccx27, &gm_ty0iqiubfm7e, ((uint8_t *)&t25), NULL)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t29 = F.l2;
        if (!t29) goto ie28;
        {
            void * t31 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t31);
        }
        {
            gm_t9qkh0rsooc8d t32;
            GM_ST(uint8_t, ((uint8_t *)&t32 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t33;
            GM_ST(uint8_t, (uint8_t *)&t33 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t33 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t33 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t33 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t33 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t33 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t33 + 16, 0);
            ((uint8_t *)&t33)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t33 + 8), t32);
            gm_tabb710d9mq2e t34 = t33;
            uint8_t t35 = ((((uint8_t *)&t34))[0] != 0);
            F.ret = t33;
            goto gm_fret;
        }
        goto ix28;
ie28:;
ix28:;
    }
    {
        void * t36 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t36);
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
inline __attribute__((aligned(4))) void gm_f_format_3pfhto5(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_3pfhto5 F;
    GM_STACK_CHECK(&F, 369u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sni6hcs91b3qh));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(12));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)48ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tfeab279c16rr t18 = GM_LD(gm_tfeab279c16rr, ((uint8_t *)a0));
            F.l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_nc9pofj, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
            gm_b16_8 t21 = t20;
            gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 16));
            gm_b16_8 t23 = t22;
            gm_b16_8 t24 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 32));
            gm_b16_8 t25 = t24;
            gm_closure t26 = t19;
            gm_node *ccx27 = t26.cx;
            if (gm_pfk8csn7us23(t26.code, t26.env, ccx27, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t26.code, t26.env, ccx27, gm_srd2ukct5o1qh, 5)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t26.code, t26.env, ccx27, &gm_ty0iqiubfm7e, ((uint8_t *)&t23), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t26.code, t26.env, ccx27, gm_sl3q52ep6aa53, 1)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t26.code, t26.env, ccx27, &gm_ty0iqiubfm7e, ((uint8_t *)&t25), NULL)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t29 = F.l2;
        if (!t29) goto ie28;
        {
            void * t31 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t31);
        }
        {
            gm_t9qkh0rsooc8d t32;
            GM_ST(uint8_t, ((uint8_t *)&t32 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t33;
            GM_ST(uint8_t, (uint8_t *)&t33 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t33 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t33 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t33 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t33 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t33 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t33 + 16, 0);
            ((uint8_t *)&t33)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t33 + 8), t32);
            gm_tabb710d9mq2e t34 = t33;
            uint8_t t35 = ((((uint8_t *)&t34))[0] != 0);
            F.ret = t33;
            goto gm_fret;
        }
        goto ix28;
ie28:;
ix28:;
    }
    {
        void * t36 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t36);
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
inline __attribute__((aligned(4))) void gm_f_format_3pk07bs(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_3pk07bs F;
    GM_STACK_CHECK(&F, 369u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_scq0i5n3f7it5));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(40));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)48ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tfeab279c16rr t18 = GM_LD(gm_tfeab279c16rr, ((uint8_t *)a0));
            F.l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_jmu3b35, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
            gm_b16_8 t21 = t20;
            gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 16));
            gm_b16_8 t23 = t22;
            gm_b16_8 t24 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 32));
            gm_b16_8 t25 = t24;
            gm_closure t26 = t19;
            gm_node *ccx27 = t26.cx;
            if (gm_pfk8csn7us23(t26.code, t26.env, ccx27, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t26.code, t26.env, ccx27, gm_s0j2j9kdc2obh, 16)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t26.code, t26.env, ccx27, &gm_ty0iqiubfm7e, ((uint8_t *)&t23), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t26.code, t26.env, ccx27, gm_s0mph2pjoa3np, 16)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t26.code, t26.env, ccx27, &gm_ty0iqiubfm7e, ((uint8_t *)&t25), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t26.code, t26.env, ccx27, gm_sjnkguo0atpm5, 2)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t29 = F.l2;
        if (!t29) goto ie28;
        {
            void * t31 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t31);
        }
        {
            gm_t9qkh0rsooc8d t32;
            GM_ST(uint8_t, ((uint8_t *)&t32 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t33;
            GM_ST(uint8_t, (uint8_t *)&t33 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t33 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t33 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t33 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t33 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t33 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t33 + 16, 0);
            ((uint8_t *)&t33)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t33 + 8), t32);
            gm_tabb710d9mq2e t34 = t33;
            uint8_t t35 = ((((uint8_t *)&t34))[0] != 0);
            F.ret = t33;
            goto gm_fret;
        }
        goto ix28;
ie28:;
ix28:;
    }
    {
        void * t36 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t36);
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
inline __attribute__((aligned(4))) void gm_f_format_3qphnbe(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_3qphnbe F;
    gm_t4d55u65a2eb7 gm_l0; /* args */
    gm_t4d55u65a2eb7 gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 345u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s1dmrsprfdp53));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(7));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)24ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t4d55u65a2eb7 t18 = GM_LD(gm_t4d55u65a2eb7, ((uint8_t *)a0));
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_qo3jtdm, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            int64_t t22 = GM_LD(int64_t, (((uint8_t *)&gm_l3) + 16));
            int64_t t23 = t22;
            gm_closure t24 = t19;
            gm_node *ccx25 = t24.cx;
            if (gm_pfk8csn7us23(t24.code, t24.env, ccx25, &gm_tybihgc69jv3, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_sao4mumvadoct, 3)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t24.code, t24.env, ccx25, &gm_tysqt22rph28, ((uint8_t *)&t23), NULL)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t27 = F.l2;
        if (!t27) goto ie26;
        {
            void * t29 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t29);
        }
        {
            gm_t9qkh0rsooc8d t30;
            GM_ST(uint8_t, ((uint8_t *)&t30 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t31;
            GM_ST(uint8_t, (uint8_t *)&t31 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t31 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t31 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t31 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t31 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t31 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t31 + 16, 0);
            ((uint8_t *)&t31)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t31 + 8), t30);
            gm_tabb710d9mq2e t32 = t31;
            uint8_t t33 = ((((uint8_t *)&t32))[0] != 0);
            F.ret = t31;
            goto gm_fret;
        }
        goto ix26;
ie26:;
ix26:;
    }
    {
        void * t34 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t34);
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
inline __attribute__((aligned(4))) void gm_f_format_3rm05av(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_3rm05av F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 305u);
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
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sscmtmpbttmhb));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(92));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)16ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_d16m383, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_closure t22 = t19;
            gm_node *ccx23 = t22.cx;
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_st7fm661apctp, 16)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t22.code, t22.env, ccx23, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_sa9dtqeprt7kf, 74)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 8), t28);
            gm_tabb710d9mq2e t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t32);
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
inline __attribute__((aligned(4))) void gm_f_format_3s3fqnk(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_3s3fqnk F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 305u);
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
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s2ph2ad6lffbq));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(16));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)16ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_korree9, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_closure t22 = t19;
            gm_node *ccx23 = t22.cx;
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_suaek0bfou9a6, 13)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t22.code, t22.env, ccx23, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_s1asl1e1tvb0k, 1)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 8), t28);
            gm_tabb710d9mq2e t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t32);
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
inline __attribute__((aligned(4))) void gm_f_format_3s8s1g6(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_3s8s1g6 F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 385u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_stf7lvoc7oe54));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(15));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)32ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t6j6smj7qoctu t18 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_tddvoln, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 16));
            gm_b16_8 t23 = t22;
            gm_closure t24 = t19;
            gm_node *ccx25 = t24.cx;
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_svjl1hr0jvvbv, 8)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t24.code, t24.env, ccx25, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_sg5gdnrith2i6, 2)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t24.code, t24.env, ccx25, &gm_ty0iqiubfm7e, ((uint8_t *)&t23), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_s1asl1e1tvb0k, 1)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t27 = F.l2;
        if (!t27) goto ie26;
        {
            void * t29 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t29);
        }
        {
            gm_t9qkh0rsooc8d t30;
            GM_ST(uint8_t, ((uint8_t *)&t30 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t31;
            GM_ST(uint8_t, (uint8_t *)&t31 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t31 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t31 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t31 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t31 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t31 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t31 + 16, 0);
            ((uint8_t *)&t31)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t31 + 8), t30);
            gm_tabb710d9mq2e t32 = t31;
            uint8_t t33 = ((((uint8_t *)&t32))[0] != 0);
            F.ret = t31;
            goto gm_fret;
        }
        goto ix26;
ie26:;
ix26:;
    }
    {
        void * t34 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t34);
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
inline __attribute__((aligned(4))) void gm_f_format_3tnlp8q(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_3tnlp8q F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 385u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sfdj0c5vltira));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(30));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)32ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t6j6smj7qoctu t18 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_eblih6b, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 16));
            gm_b16_8 t23 = t22;
            gm_closure t24 = t19;
            gm_node *ccx25 = t24.cx;
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_sfm9elkq6bn2e, 1)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t24.code, t24.env, ccx25, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_sag4ku07t2k8t, 24)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t24.code, t24.env, ccx25, &gm_ty0iqiubfm7e, ((uint8_t *)&t23), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_sfm9elkq6bn2e, 1)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t27 = F.l2;
        if (!t27) goto ie26;
        {
            void * t29 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t29);
        }
        {
            gm_t9qkh0rsooc8d t30;
            GM_ST(uint8_t, ((uint8_t *)&t30 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t31;
            GM_ST(uint8_t, (uint8_t *)&t31 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t31 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t31 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t31 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t31 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t31 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t31 + 16, 0);
            ((uint8_t *)&t31)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t31 + 8), t30);
            gm_tabb710d9mq2e t32 = t31;
            uint8_t t33 = ((((uint8_t *)&t32))[0] != 0);
            F.ret = t31;
            goto gm_fret;
        }
        goto ix26;
ie26:;
ix26:;
    }
    {
        void * t34 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t34);
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
inline __attribute__((aligned(4))) void gm_f_format_3tnvb6a(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0) {
    struct gm_fr_format_3tnvb6a F;
    gm_t88rcucg1b7uk gm_l0; /* args */
    gm_t88rcucg1b7uk gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 245u);
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
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_slof5tl7m8grj));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(3));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)4ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t88rcucg1b7uk t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_beikmkp, (void *)&F.h, cx});
            uint32_t t20 = GM_LD(uint32_t, (((uint8_t *)&gm_l3) + 0));
            uint32_t t21 = t20;
            gm_closure t22 = t19;
            gm_node *ccx23 = t22.cx;
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_s2gnpj7k4l22n, 1)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t22.code, t22.env, ccx23, &gm_ty467d5cm1d5, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 8), t28);
            gm_tabb710d9mq2e t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t32);
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
inline __attribute__((aligned(4))) void gm_f_format_3tqcmdm(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_3tqcmdm F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 305u);
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
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sbvmilijbiuka));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(25));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)16ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_oct8fdl, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_closure t22 = t19;
            gm_node *ccx23 = t22.cx;
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_sdgdb59na1854, 22)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t22.code, t22.env, ccx23, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_sfm9elkq6bn2e, 1)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 8), t28);
            gm_tabb710d9mq2e t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t32);
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
inline __attribute__((aligned(4))) void gm_f_format_3v827ra(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_3v827ra F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 385u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sk2nrl11c01gj));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(19));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)32ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t6j6smj7qoctu t18 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_2r7l0dc, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 16));
            gm_b16_8 t23 = t22;
            gm_closure t24 = t19;
            gm_node *ccx25 = t24.cx;
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_snekrjln06tfl, 13)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t24.code, t24.env, ccx25, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_slgmqgkioa4u6, 2)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t24.code, t24.env, ccx25, &gm_ty0iqiubfm7e, ((uint8_t *)&t23), NULL)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t27 = F.l2;
        if (!t27) goto ie26;
        {
            void * t29 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t29);
        }
        {
            gm_t9qkh0rsooc8d t30;
            GM_ST(uint8_t, ((uint8_t *)&t30 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t31;
            GM_ST(uint8_t, (uint8_t *)&t31 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t31 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t31 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t31 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t31 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t31 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t31 + 16, 0);
            ((uint8_t *)&t31)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t31 + 8), t30);
            gm_tabb710d9mq2e t32 = t31;
            uint8_t t33 = ((((uint8_t *)&t32))[0] != 0);
            F.ret = t31;
            goto gm_fret;
        }
        goto ix26;
ie26:;
ix26:;
    }
    {
        void * t34 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t34);
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
inline __attribute__((aligned(4))) void gm_f_format_3vgp4uu(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_3vgp4uu F;
    GM_STACK_CHECK(&F, 369u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sv19dj08f7d6b));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(44));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)48ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tfeab279c16rr t18 = GM_LD(gm_tfeab279c16rr, ((uint8_t *)a0));
            F.l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_rl156nv, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
            gm_b16_8 t21 = t20;
            gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 16));
            gm_b16_8 t23 = t22;
            gm_b16_8 t24 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 32));
            gm_b16_8 t25 = t24;
            gm_closure t26 = t19;
            gm_node *ccx27 = t26.cx;
            if (gm_pfk8csn7us23(t26.code, t26.env, ccx27, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t26.code, t26.env, ccx27, gm_smooaouqmm8dj, 1)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t26.code, t26.env, ccx27, &gm_ty0iqiubfm7e, ((uint8_t *)&t23), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t26.code, t26.env, ccx27, gm_soiad5r9c65ru, 36)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t26.code, t26.env, ccx27, &gm_ty0iqiubfm7e, ((uint8_t *)&t25), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t26.code, t26.env, ccx27, gm_s1asl1e1tvb0k, 1)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t29 = F.l2;
        if (!t29) goto ie28;
        {
            void * t31 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t31);
        }
        {
            gm_t9qkh0rsooc8d t32;
            GM_ST(uint8_t, ((uint8_t *)&t32 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t33;
            GM_ST(uint8_t, (uint8_t *)&t33 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t33 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t33 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t33 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t33 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t33 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t33 + 16, 0);
            ((uint8_t *)&t33)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t33 + 8), t32);
            gm_tabb710d9mq2e t34 = t33;
            uint8_t t35 = ((((uint8_t *)&t34))[0] != 0);
            F.ret = t33;
            goto gm_fret;
        }
        goto ix28;
ie28:;
ix28:;
    }
    {
        void * t36 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t36);
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
inline __attribute__((aligned(4))) void gm_f_format_3vtl3te(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_3vtl3te F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 385u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sjtp8ip8t28uh));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(6));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)32ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t6j6smj7qoctu t18 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_ldl1hcr, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 16));
            gm_b16_8 t23 = t22;
            gm_closure t24 = t19;
            gm_node *ccx25 = t24.cx;
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_smooaouqmm8dj, 1)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t24.code, t24.env, ccx25, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_s1asl1e1tvb0k, 1)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t24.code, t24.env, ccx25, &gm_ty0iqiubfm7e, ((uint8_t *)&t23), NULL)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t27 = F.l2;
        if (!t27) goto ie26;
        {
            void * t29 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t29);
        }
        {
            gm_t9qkh0rsooc8d t30;
            GM_ST(uint8_t, ((uint8_t *)&t30 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t31;
            GM_ST(uint8_t, (uint8_t *)&t31 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t31 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t31 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t31 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t31 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t31 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t31 + 16, 0);
            ((uint8_t *)&t31)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t31 + 8), t30);
            gm_tabb710d9mq2e t32 = t31;
            uint8_t t33 = ((((uint8_t *)&t32))[0] != 0);
            F.ret = t31;
            goto gm_fret;
        }
        goto ix26;
ie26:;
ix26:;
    }
    {
        void * t34 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t34);
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
inline __attribute__((aligned(4))) void gm_f_format_j07a6mr(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_j07a6mr F;
    GM_STACK_CHECK(&F, 369u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s5v9d9i0gsl8c));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(7));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)48ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tfeab279c16rr t18 = GM_LD(gm_tfeab279c16rr, ((uint8_t *)a0));
            F.l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_6etic8u, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
            gm_b16_8 t21 = t20;
            gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 16));
            gm_b16_8 t23 = t22;
            gm_b16_8 t24 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 32));
            gm_b16_8 t25 = t24;
            gm_closure t26 = t19;
            gm_node *ccx27 = t26.cx;
            if (gm_pfk8csn7us23(t26.code, t26.env, ccx27, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t26.code, t26.env, ccx27, gm_st3ja0nqi37bv, 1)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t26.code, t26.env, ccx27, &gm_ty0iqiubfm7e, ((uint8_t *)&t23), NULL)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t26.code, t26.env, ccx27, &gm_ty0iqiubfm7e, ((uint8_t *)&t25), NULL)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t29 = F.l2;
        if (!t29) goto ie28;
        {
            void * t31 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t31);
        }
        {
            gm_t9qkh0rsooc8d t32;
            GM_ST(uint8_t, ((uint8_t *)&t32 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t33;
            GM_ST(uint8_t, (uint8_t *)&t33 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t33 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t33 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t33 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t33 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t33 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t33 + 16, 0);
            ((uint8_t *)&t33)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t33 + 8), t32);
            gm_tabb710d9mq2e t34 = t33;
            uint8_t t35 = ((((uint8_t *)&t34))[0] != 0);
            F.ret = t33;
            goto gm_fret;
        }
        goto ix28;
ie28:;
ix28:;
    }
    {
        void * t36 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t36);
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
inline __attribute__((aligned(4))) void gm_f_format_j0ggm04(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_j0ggm04 F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 305u);
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
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_soohe0i8iafj3));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(19));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)16ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_b108j07, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_closure t22 = t19;
            gm_node *ccx23 = t22.cx;
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_sogkbe8nre26a, 16)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t22.code, t22.env, ccx23, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_sfm9elkq6bn2e, 1)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 8), t28);
            gm_tabb710d9mq2e t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t32);
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
inline __attribute__((aligned(4))) void gm_f_format_j0i128e(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_j0i128e F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 385u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sh54qlvpeqnin));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(9));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)32ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t6j6smj7qoctu t18 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_kbiuvc2, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 16));
            gm_b16_8 t23 = t22;
            gm_closure t24 = t19;
            gm_node *ccx25 = t24.cx;
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_sj21lfdl9q0t4, 5)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t24.code, t24.env, ccx25, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t24.code, t24.env, ccx25, &gm_ty0iqiubfm7e, ((uint8_t *)&t23), NULL)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t27 = F.l2;
        if (!t27) goto ie26;
        {
            void * t29 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t29);
        }
        {
            gm_t9qkh0rsooc8d t30;
            GM_ST(uint8_t, ((uint8_t *)&t30 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t31;
            GM_ST(uint8_t, (uint8_t *)&t31 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t31 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t31 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t31 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t31 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t31 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t31 + 16, 0);
            ((uint8_t *)&t31)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t31 + 8), t30);
            gm_tabb710d9mq2e t32 = t31;
            uint8_t t33 = ((((uint8_t *)&t32))[0] != 0);
            F.ret = t31;
            goto gm_fret;
        }
        goto ix26;
ie26:;
ix26:;
    }
    {
        void * t34 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t34);
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
inline __attribute__((aligned(4))) void gm_f_format_j44ni19(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0) {
    struct gm_fr_format_j44ni19 F;
    gm_t4ce0jd3tdro4 gm_l0; /* args */
    gm_t4ce0jd3tdro4 gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 265u);
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
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s1fjspgm800ug));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(6));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)8ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t4ce0jd3tdro4 t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_lom5i6m, (void *)&F.h, cx});
            uint64_t t20 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 0));
            uint64_t t21 = t20;
            gm_closure t22 = t19;
            gm_node *ccx23 = t22.cx;
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_su71itml70ikf, 4)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t22.code, t22.env, ccx23, &gm_ty6groq37ram, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 8), t28);
            gm_tabb710d9mq2e t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t32);
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
inline __attribute__((aligned(4))) void gm_f_format_j4le8o0(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_j4le8o0 F;
    gm_tf1bjg81u0glv gm_l0; /* args */
    gm_tf1bjg81u0glv gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 345u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s802sbfa6mtsg));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(26));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)24ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tf1bjg81u0glv t18 = GM_LD(gm_tf1bjg81u0glv, ((uint8_t *)a0));
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_64h5o83, (void *)&F.h, cx});
            uint64_t t20 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 0));
            uint64_t t21 = t20;
            gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 8));
            gm_b16_8 t23 = t22;
            gm_closure t24 = t19;
            gm_node *ccx25 = t24.cx;
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_st3m5olipe6ov, 16)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t24.code, t24.env, ccx25, &gm_ty6groq37ram, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_st6hj8v0h80gt, 5)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t24.code, t24.env, ccx25, &gm_ty0iqiubfm7e, ((uint8_t *)&t23), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_sfm9elkq6bn2e, 1)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t27 = F.l2;
        if (!t27) goto ie26;
        {
            void * t29 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t29);
        }
        {
            gm_t9qkh0rsooc8d t30;
            GM_ST(uint8_t, ((uint8_t *)&t30 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t31;
            GM_ST(uint8_t, (uint8_t *)&t31 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t31 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t31 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t31 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t31 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t31 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t31 + 16, 0);
            ((uint8_t *)&t31)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t31 + 8), t30);
            gm_tabb710d9mq2e t32 = t31;
            uint8_t t33 = ((((uint8_t *)&t32))[0] != 0);
            F.ret = t31;
            goto gm_fret;
        }
        goto ix26;
ie26:;
ix26:;
    }
    {
        void * t34 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t34);
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
inline __attribute__((aligned(4))) void gm_f_format_j4sks15(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_j4sks15 F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 385u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sjguhtmsnuhlj));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(57));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)32ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t6j6smj7qoctu t18 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_eal3dae, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 16));
            gm_b16_8 t23 = t22;
            gm_closure t24 = t19;
            gm_node *ccx25 = t24.cx;
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_sfs5rrntfu3mi, 24)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t24.code, t24.env, ccx25, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_slmas8vbhthkn, 19)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t24.code, t24.env, ccx25, &gm_ty0iqiubfm7e, ((uint8_t *)&t23), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_smbgfcmcr0r8n, 10)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t27 = F.l2;
        if (!t27) goto ie26;
        {
            void * t29 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t29);
        }
        {
            gm_t9qkh0rsooc8d t30;
            GM_ST(uint8_t, ((uint8_t *)&t30 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t31;
            GM_ST(uint8_t, (uint8_t *)&t31 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t31 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t31 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t31 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t31 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t31 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t31 + 16, 0);
            ((uint8_t *)&t31)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t31 + 8), t30);
            gm_tabb710d9mq2e t32 = t31;
            uint8_t t33 = ((((uint8_t *)&t32))[0] != 0);
            F.ret = t31;
            goto gm_fret;
        }
        goto ix26;
ie26:;
ix26:;
    }
    {
        void * t34 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t34);
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
inline __attribute__((aligned(4))) void gm_f_format_j7jgble(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0) {
    struct gm_fr_format_j7jgble F;
    gm_t88rcucg1b7uk gm_l0; /* args */
    gm_t88rcucg1b7uk gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 245u);
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
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sb1dc9u3lvvu1));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(4));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)4ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t88rcucg1b7uk t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_tr10ktn, (void *)&F.h, cx});
            uint32_t t20 = GM_LD(uint32_t, (((uint8_t *)&gm_l3) + 0));
            uint32_t t21 = t20;
            gm_closure t22 = t19;
            gm_node *ccx23 = t22.cx;
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_sp8i1c1kf79ks, 2)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t22.code, t22.env, ccx23, &gm_ty467d5cm1d5, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 8), t28);
            gm_tabb710d9mq2e t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t32);
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
inline __attribute__((aligned(4))) void gm_f_format_j8m87ti(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_j8m87ti F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 305u);
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
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_ssgnvuivrkqi2));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(83));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)16ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_dvaqvg7, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_closure t22 = t19;
            gm_node *ccx23 = t22.cx;
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_s4lmdtjb9rnah, 69)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t22.code, t22.env, ccx23, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_seo41mk3n3ro5, 12)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 8), t28);
            gm_tabb710d9mq2e t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t32);
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
inline __attribute__((aligned(4))) void gm_f_format_ja2c5em(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_ja2c5em F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 305u);
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
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s9jrvf2ogt807));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(16));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)16ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_79inkgl, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_closure t22 = t19;
            gm_node *ccx23 = t22.cx;
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_su6ol10q7la3d, 6)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t22.code, t22.env, ccx23, &gm_tybihgc69jv3, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_sh5qb2l9hubh3, 8)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 8), t28);
            gm_tabb710d9mq2e t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t32);
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
inline __attribute__((aligned(4))) void gm_f_format_jagvq3q(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0) {
    struct gm_fr_format_jagvq3q F;
    gm_t88rcucg1b7uk gm_l0; /* args */
    gm_t88rcucg1b7uk gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 245u);
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
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sjr44k79a48mq));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(3));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)4ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t88rcucg1b7uk t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_q5enbli, (void *)&F.h, cx});
            uint32_t t20 = GM_LD(uint32_t, (((uint8_t *)&gm_l3) + 0));
            uint32_t t21 = t20;
            gm_closure t22 = t19;
            gm_node *ccx23 = t22.cx;
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_sdjfsrjr2b2ke, 1)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t22.code, t22.env, ccx23, &gm_ty467d5cm1d5, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 8), t28);
            gm_tabb710d9mq2e t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t32);
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
inline __attribute__((aligned(4))) void gm_f_format_jaikcua(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_jaikcua F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 305u);
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
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sk30fte1u6en9));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(32));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)16ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_pur3nlu, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_closure t22 = t19;
            gm_node *ccx23 = t22.cx;
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_srvgp776ipjtg, 29)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t22.code, t22.env, ccx23, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_sfm9elkq6bn2e, 1)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 8), t28);
            gm_tabb710d9mq2e t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t32);
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
inline __attribute__((aligned(4))) void gm_f_format_jb19283(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_jb19283 F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 305u);
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
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s8l22p9l388io));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(66));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)16ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_ih3upe1, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_closure t22 = t19;
            gm_node *ccx23 = t22.cx;
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_srn86a84qh2td, 54)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t22.code, t22.env, ccx23, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_sr9i7j9bpmaec, 10)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 8), t28);
            gm_tabb710d9mq2e t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t32);
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
inline __attribute__((aligned(4))) void gm_f_format_jb7bqji(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_jb7bqji F;
    gm_tctdkl637nfjf gm_l0; /* args */
    gm_tctdkl637nfjf gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 385u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_statjrbfbnhar));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(10));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)32ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tctdkl637nfjf t18 = GM_LD(gm_tctdkl637nfjf, ((uint8_t *)a0));
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_3psqmn1, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            __int128 t22 = GM_LD(__int128, (((uint8_t *)&gm_l3) + 16));
            __int128 t23 = t22;
            gm_closure t24 = t19;
            gm_node *ccx25 = t24.cx;
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_so9fi6r28770a, 2)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t24.code, t24.env, ccx25, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_s1asl1e1tvb0k, 1)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t24.code, t24.env, ccx25, &gm_tyiiln09e647, ((uint8_t *)&t23), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_sel0tu7gvv1na, 3)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t27 = F.l2;
        if (!t27) goto ie26;
        {
            void * t29 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t29);
        }
        {
            gm_t9qkh0rsooc8d t30;
            GM_ST(uint8_t, ((uint8_t *)&t30 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t31;
            GM_ST(uint8_t, (uint8_t *)&t31 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t31 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t31 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t31 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t31 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t31 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t31 + 16, 0);
            ((uint8_t *)&t31)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t31 + 8), t30);
            gm_tabb710d9mq2e t32 = t31;
            uint8_t t33 = ((((uint8_t *)&t32))[0] != 0);
            F.ret = t31;
            goto gm_fret;
        }
        goto ix26;
ie26:;
ix26:;
    }
    {
        void * t34 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t34);
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
inline __attribute__((aligned(4))) void gm_f_format_jc3ftg7(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0) {
    struct gm_fr_format_jc3ftg7 F;
    gm_t4ce0jd3tdro4 gm_l0; /* args */
    gm_t4ce0jd3tdro4 gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 265u);
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
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sdv3b0kjlrdl2));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(45));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)8ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t4ce0jd3tdro4 t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_arnu6be, (void *)&F.h, cx});
            uint64_t t20 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 0));
            uint64_t t21 = t20;
            gm_closure t22 = t19;
            gm_node *ccx23 = t22.cx;
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_scogrgnqo3tpu, 21)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t22.code, t22.env, ccx23, &gm_tyb2nno7n67r, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_sr44oukgimglr, 22)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 8), t28);
            gm_tabb710d9mq2e t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t32);
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
inline __attribute__((aligned(4))) void gm_f_format_jdgr48v(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_jdgr48v F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 385u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s93qviqoij24b));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(30));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)32ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t6j6smj7qoctu t18 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_eaemvpe, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 16));
            gm_b16_8 t23 = t22;
            gm_closure t24 = t19;
            gm_node *ccx25 = t24.cx;
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_s5lqhiu2bn6of, 7)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t24.code, t24.env, ccx25, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_smbjf525bk9a0, 19)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t24.code, t24.env, ccx25, &gm_ty0iqiubfm7e, ((uint8_t *)&t23), NULL)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t27 = F.l2;
        if (!t27) goto ie26;
        {
            void * t29 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t29);
        }
        {
            gm_t9qkh0rsooc8d t30;
            GM_ST(uint8_t, ((uint8_t *)&t30 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t31;
            GM_ST(uint8_t, (uint8_t *)&t31 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t31 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t31 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t31 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t31 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t31 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t31 + 16, 0);
            ((uint8_t *)&t31)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t31 + 8), t30);
            gm_tabb710d9mq2e t32 = t31;
            uint8_t t33 = ((((uint8_t *)&t32))[0] != 0);
            F.ret = t31;
            goto gm_fret;
        }
        goto ix26;
ie26:;
ix26:;
    }
    {
        void * t34 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t34);
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
inline __attribute__((aligned(4))) void gm_f_format_je4jsfp(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_je4jsfp F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 385u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sukjjfkvr8401));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(39));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)32ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t6j6smj7qoctu t18 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_rj4kesu, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 16));
            gm_b16_8 t23 = t22;
            gm_closure t24 = t19;
            gm_node *ccx25 = t24.cx;
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_s03c24qg7i06q, 12)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t24.code, t24.env, ccx25, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_skd5c72k4pcr6, 22)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t24.code, t24.env, ccx25, &gm_ty0iqiubfm7e, ((uint8_t *)&t23), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_sfm9elkq6bn2e, 1)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t27 = F.l2;
        if (!t27) goto ie26;
        {
            void * t29 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t29);
        }
        {
            gm_t9qkh0rsooc8d t30;
            GM_ST(uint8_t, ((uint8_t *)&t30 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t31;
            GM_ST(uint8_t, (uint8_t *)&t31 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t31 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t31 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t31 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t31 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t31 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t31 + 16, 0);
            ((uint8_t *)&t31)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t31 + 8), t30);
            gm_tabb710d9mq2e t32 = t31;
            uint8_t t33 = ((((uint8_t *)&t32))[0] != 0);
            F.ret = t31;
            goto gm_fret;
        }
        goto ix26;
ie26:;
ix26:;
    }
    {
        void * t34 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t34);
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
inline __attribute__((aligned(4))) void gm_f_format_jet3e97(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_jet3e97 F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 305u);
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
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s8qvuobg6jcbo));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(12));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)16ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_1tiq8j9, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_closure t22 = t19;
            gm_node *ccx23 = t22.cx;
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_sgh0dvu15b9ta, 9)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t22.code, t22.env, ccx23, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_s1asl1e1tvb0k, 1)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 8), t28);
            gm_tabb710d9mq2e t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t32);
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
inline __attribute__((aligned(4))) void gm_f_format_jf46ta8(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_jf46ta8 F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 385u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s0eipmcrsllno));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(7));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)32ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t6j6smj7qoctu t18 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_5ldr0dk, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 16));
            gm_b16_8 t23 = t22;
            gm_closure t24 = t19;
            gm_node *ccx25 = t24.cx;
            if (gm_pfk8csn7us23(t24.code, t24.env, ccx25, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_s9b7nekk74u38, 2)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t24.code, t24.env, ccx25, &gm_tybihgc69jv3, ((uint8_t *)&t23), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_sb54ui2amic32, 1)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t27 = F.l2;
        if (!t27) goto ie26;
        {
            void * t29 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t29);
        }
        {
            gm_t9qkh0rsooc8d t30;
            GM_ST(uint8_t, ((uint8_t *)&t30 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t31;
            GM_ST(uint8_t, (uint8_t *)&t31 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t31 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t31 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t31 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t31 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t31 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t31 + 16, 0);
            ((uint8_t *)&t31)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t31 + 8), t30);
            gm_tabb710d9mq2e t32 = t31;
            uint8_t t33 = ((((uint8_t *)&t32))[0] != 0);
            F.ret = t31;
            goto gm_fret;
        }
        goto ix26;
ie26:;
ix26:;
    }
    {
        void * t34 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t34);
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
inline __attribute__((aligned(4))) void gm_f_format_jfku65e(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_jfku65e F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 385u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sqv6e5e2cne7b));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(58));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)32ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t6j6smj7qoctu t18 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_ngj5ii9, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 16));
            gm_b16_8 t23 = t22;
            gm_closure t24 = t19;
            gm_node *ccx25 = t24.cx;
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_shg24ogv0rnt4, 13)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t24.code, t24.env, ccx25, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_sp7cdd0n91vli, 7)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t24.code, t24.env, ccx25, &gm_ty0iqiubfm7e, ((uint8_t *)&t23), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_sgnt3t4lgofos, 34)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t27 = F.l2;
        if (!t27) goto ie26;
        {
            void * t29 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t29);
        }
        {
            gm_t9qkh0rsooc8d t30;
            GM_ST(uint8_t, ((uint8_t *)&t30 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t31;
            GM_ST(uint8_t, (uint8_t *)&t31 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t31 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t31 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t31 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t31 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t31 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t31 + 16, 0);
            ((uint8_t *)&t31)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t31 + 8), t30);
            gm_tabb710d9mq2e t32 = t31;
            uint8_t t33 = ((((uint8_t *)&t32))[0] != 0);
            F.ret = t31;
            goto gm_fret;
        }
        goto ix26;
ie26:;
ix26:;
    }
    {
        void * t34 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t34);
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
inline __attribute__((aligned(4))) void gm_f_format_jh4rfvo(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_jh4rfvo F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 305u);
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
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sdru6cvprml58));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(28));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)16ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_4pkhf4j, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_closure t22 = t19;
            gm_node *ccx23 = t22.cx;
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_ss4tls6jr9q3m, 9)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t22.code, t22.env, ccx23, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_sveg1mb15s1b2, 17)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 8), t28);
            gm_tabb710d9mq2e t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t32);
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
inline __attribute__((aligned(4))) void gm_f_format_jha63mc(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_jha63mc F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 305u);
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
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s743265bqlq4a));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(13));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)16ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_vmupu7q, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_closure t22 = t19;
            gm_node *ccx23 = t22.cx;
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_sdmti3s9j8p44, 10)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t22.code, t22.env, ccx23, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_sfm9elkq6bn2e, 1)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 8), t28);
            gm_tabb710d9mq2e t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t32);
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
inline __attribute__((aligned(4))) void gm_f_format_jhm2032(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_jhm2032 F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 305u);
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
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sbfr1qrcvp774));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(58));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)16ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_bgc9qfh, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_closure t22 = t19;
            gm_node *ccx23 = t22.cx;
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_sqk8phuvunvgk, 55)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t22.code, t22.env, ccx23, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_sfm9elkq6bn2e, 1)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 8), t28);
            gm_tabb710d9mq2e t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t32);
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
inline __attribute__((aligned(4))) void gm_f_format_jicque0(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_jicque0 F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 305u);
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
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s1lekaltijhvv));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(7));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)16ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_cmjmpc7, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_closure t22 = t19;
            gm_node *ccx23 = t22.cx;
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_s9ab1ohnfcjkc, 5)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t22.code, t22.env, ccx23, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 8), t28);
            gm_tabb710d9mq2e t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t32);
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
inline __attribute__((aligned(4))) void gm_f_format_jiop5bj(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_jiop5bj F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 305u);
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
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_skf282h8e3q1e));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(48));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)16ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_599rnh9, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_closure t22 = t19;
            gm_node *ccx23 = t22.cx;
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_shbtdicfd8lcc, 11)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t22.code, t22.env, ccx23, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_sinogveu00g7m, 35)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 8), t28);
            gm_tabb710d9mq2e t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t32);
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
inline __attribute__((aligned(4))) void gm_f_format_jj2aipm(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_jj2aipm F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 305u);
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
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s2vj6kgk19dag));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(31));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)16ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_hjt9q04, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_closure t22 = t19;
            gm_node *ccx23 = t22.cx;
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_s2s2hv5oav6fb, 28)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t22.code, t22.env, ccx23, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_s1asl1e1tvb0k, 1)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 8), t28);
            gm_tabb710d9mq2e t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t32);
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
inline __attribute__((aligned(4))) void gm_f_format_jk4dii4(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_jk4dii4 F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 305u);
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
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_stc6hgo9kskc3));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(7));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)16ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_0736hae, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_closure t22 = t19;
            gm_node *ccx23 = t22.cx;
            if (gm_pfk8csn7us23(t22.code, t22.env, ccx23, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_s85m9c4pt0ikp, 5)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 8), t28);
            gm_tabb710d9mq2e t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t32);
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
inline __attribute__((aligned(4))) void gm_f_format_jlajof4(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_jlajof4 F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 305u);
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
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_srojjig2k2m5d));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(12));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)16ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_9o0kvur, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_closure t22 = t19;
            gm_node *ccx23 = t22.cx;
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_s91vsko3h6v6r, 9)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t22.code, t22.env, ccx23, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_sfm9elkq6bn2e, 1)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 8), t28);
            gm_tabb710d9mq2e t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t32);
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
inline __attribute__((aligned(4))) void gm_f_format_jm2cnr1(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_jm2cnr1 F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 305u);
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
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_ssa2crodel6ih));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(28));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)16ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_vqiit51, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_closure t22 = t19;
            gm_node *ccx23 = t22.cx;
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_suifusi0pb6s0, 11)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t22.code, t22.env, ccx23, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_s542rn9shphg8, 15)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 8), t28);
            gm_tabb710d9mq2e t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t32);
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
inline __attribute__((aligned(4))) void gm_f_format_jm4dd7q(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_jm4dd7q F;
    gm_tob1snuii5bme gm_l0; /* args */
    gm_tob1snuii5bme gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 385u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sovcjofctijvj));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(46));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)32ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tob1snuii5bme t18 = GM_LD(gm_tob1snuii5bme, ((uint8_t *)a0));
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_m79rh95, (void *)&F.h, cx});
            uint64_t t20 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 0));
            uint64_t t21 = t20;
            gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 8));
            gm_b16_8 t23 = t22;
            uint64_t t24 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 24));
            uint64_t t25 = t24;
            gm_closure t26 = t19;
            gm_node *ccx27 = t26.cx;
            if (gm_pf0aqhubr4up(t26.code, t26.env, ccx27, gm_scotq6054g01h, 5)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t26.code, t26.env, ccx27, &gm_tyb2nno7n67r, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t26.code, t26.env, ccx27, gm_sr00p3fdg5qn3, 18)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t26.code, t26.env, ccx27, &gm_ty0iqiubfm7e, ((uint8_t *)&t23), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t26.code, t26.env, ccx27, gm_sfrdbcc53tuv9, 17)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t26.code, t26.env, ccx27, &gm_tyb2nno7n67r, ((uint8_t *)&t25), NULL)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t29 = F.l2;
        if (!t29) goto ie28;
        {
            void * t31 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t31);
        }
        {
            gm_t9qkh0rsooc8d t32;
            GM_ST(uint8_t, ((uint8_t *)&t32 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t33;
            GM_ST(uint8_t, (uint8_t *)&t33 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t33 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t33 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t33 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t33 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t33 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t33 + 16, 0);
            ((uint8_t *)&t33)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t33 + 8), t32);
            gm_tabb710d9mq2e t34 = t33;
            uint8_t t35 = ((((uint8_t *)&t34))[0] != 0);
            F.ret = t33;
            goto gm_fret;
        }
        goto ix28;
ie28:;
ix28:;
    }
    {
        void * t36 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t36);
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
inline __attribute__((aligned(4))) void gm_f_format_jn4ifp1(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_jn4ifp1 F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 305u);
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
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s25euhn4i5qkh));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(71));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)16ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_l2f2ej8, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_closure t22 = t19;
            gm_node *ccx23 = t22.cx;
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_saeqiapbqfqhj, 27)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t22.code, t22.env, ccx23, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_s3la5l2o2cgbq, 42)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 8), t28);
            gm_tabb710d9mq2e t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t32);
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
inline __attribute__((aligned(4))) void gm_f_format_jo1gjr5(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_jo1gjr5 F;
    GM_STACK_CHECK(&F, 417u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s8e4i8e286ns3));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(24));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)64ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t0uuhaljsf410 t18 = GM_LD(gm_t0uuhaljsf410, ((uint8_t *)a0));
            F.l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_ulelml8, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
            gm_b16_8 t21 = t20;
            gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 16));
            gm_b16_8 t23 = t22;
            gm_b16_8 t24 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 32));
            gm_b16_8 t25 = t24;
            gm_b16_8 t26 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 48));
            gm_b16_8 t27 = t26;
            gm_closure t28 = t19;
            gm_node *ccx29 = t28.cx;
            if (gm_pf0aqhubr4up(t28.code, t28.env, ccx29, gm_smooaouqmm8dj, 1)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t28.code, t28.env, ccx29, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t28.code, t28.env, ccx29, gm_svhavo13qha5k, 3)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t28.code, t28.env, ccx29, &gm_ty0iqiubfm7e, ((uint8_t *)&t23), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t28.code, t28.env, ccx29, gm_sld5pmb7t0r25, 8)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t28.code, t28.env, ccx29, &gm_ty0iqiubfm7e, ((uint8_t *)&t25), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t28.code, t28.env, ccx29, gm_s1asl1e1tvb0k, 1)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t28.code, t28.env, ccx29, &gm_ty0iqiubfm7e, ((uint8_t *)&t27), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t28.code, t28.env, ccx29, gm_s1asl1e1tvb0k, 1)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t31 = F.l2;
        if (!t31) goto ie30;
        {
            void * t33 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t33);
        }
        {
            gm_t9qkh0rsooc8d t34;
            GM_ST(uint8_t, ((uint8_t *)&t34 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t35;
            GM_ST(uint8_t, (uint8_t *)&t35 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t35 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t35 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t35 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t35 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t35 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t35 + 16, 0);
            ((uint8_t *)&t35)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t35 + 8), t34);
            gm_tabb710d9mq2e t36 = t35;
            uint8_t t37 = ((((uint8_t *)&t36))[0] != 0);
            F.ret = t35;
            goto gm_fret;
        }
        goto ix30;
ie30:;
ix30:;
    }
    {
        void * t38 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t38);
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
inline __attribute__((aligned(4))) void gm_f_format_jp1a7ai(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_jp1a7ai F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 385u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_saovklm35s57t));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(183));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)32ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t6j6smj7qoctu t18 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_okqu94u, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 16));
            gm_b16_8 t23 = t22;
            gm_closure t24 = t19;
            gm_node *ccx25 = t24.cx;
            if (gm_pfk8csn7us23(t24.code, t24.env, ccx25, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_sg26if17vtome, 22)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t24.code, t24.env, ccx25, &gm_ty0iqiubfm7e, ((uint8_t *)&t23), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_shdh4dd2ejebj, 157)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t27 = F.l2;
        if (!t27) goto ie26;
        {
            void * t29 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t29);
        }
        {
            gm_t9qkh0rsooc8d t30;
            GM_ST(uint8_t, ((uint8_t *)&t30 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t31;
            GM_ST(uint8_t, (uint8_t *)&t31 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t31 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t31 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t31 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t31 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t31 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t31 + 16, 0);
            ((uint8_t *)&t31)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t31 + 8), t30);
            gm_tabb710d9mq2e t32 = t31;
            uint8_t t33 = ((((uint8_t *)&t32))[0] != 0);
            F.ret = t31;
            goto gm_fret;
        }
        goto ix26;
ie26:;
ix26:;
    }
    {
        void * t34 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t34);
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
inline __attribute__((aligned(4))) void gm_f_format_jp1qc7o(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_jp1qc7o F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 385u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sil41urphp1ol));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(11));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)32ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t6j6smj7qoctu t18 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_fm5di8s, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 16));
            gm_b16_8 t23 = t22;
            gm_closure t24 = t19;
            gm_node *ccx25 = t24.cx;
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_sp8vnj37dd7ds, 6)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t24.code, t24.env, ccx25, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_s2u5posfkbgnq, 1)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t24.code, t24.env, ccx25, &gm_ty0iqiubfm7e, ((uint8_t *)&t23), NULL)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t27 = F.l2;
        if (!t27) goto ie26;
        {
            void * t29 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t29);
        }
        {
            gm_t9qkh0rsooc8d t30;
            GM_ST(uint8_t, ((uint8_t *)&t30 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t31;
            GM_ST(uint8_t, (uint8_t *)&t31 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t31 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t31 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t31 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t31 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t31 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t31 + 16, 0);
            ((uint8_t *)&t31)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t31 + 8), t30);
            gm_tabb710d9mq2e t32 = t31;
            uint8_t t33 = ((((uint8_t *)&t32))[0] != 0);
            F.ret = t31;
            goto gm_fret;
        }
        goto ix26;
ie26:;
ix26:;
    }
    {
        void * t34 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t34);
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
inline __attribute__((aligned(4))) void gm_f_format_jp4udcs(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_jp4udcs F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 385u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s0q6kpmhsprri));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(32));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)32ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t6j6smj7qoctu t18 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_bmj7akp, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 16));
            gm_b16_8 t23 = t22;
            gm_closure t24 = t19;
            gm_node *ccx25 = t24.cx;
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_s3ml7p21f55q7, 13)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t24.code, t24.env, ccx25, &gm_tybihgc69jv3, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_sm54129hqmnsd, 15)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t24.code, t24.env, ccx25, &gm_ty0iqiubfm7e, ((uint8_t *)&t23), NULL)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t27 = F.l2;
        if (!t27) goto ie26;
        {
            void * t29 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t29);
        }
        {
            gm_t9qkh0rsooc8d t30;
            GM_ST(uint8_t, ((uint8_t *)&t30 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t31;
            GM_ST(uint8_t, (uint8_t *)&t31 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t31 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t31 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t31 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t31 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t31 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t31 + 16, 0);
            ((uint8_t *)&t31)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t31 + 8), t30);
            gm_tabb710d9mq2e t32 = t31;
            uint8_t t33 = ((((uint8_t *)&t32))[0] != 0);
            F.ret = t31;
            goto gm_fret;
        }
        goto ix26;
ie26:;
ix26:;
    }
    {
        void * t34 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t34);
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
inline __attribute__((aligned(4))) void gm_f_format_jpvhgp3(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0) {
    struct gm_fr_format_jpvhgp3 F;
    gm_t4ce0jd3tdro4 gm_l0; /* args */
    gm_t4ce0jd3tdro4 gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 265u);
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
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sa23gppmgki7j));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(10));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)8ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t4ce0jd3tdro4 t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_oiu5u5h, (void *)&F.h, cx});
            uint64_t t20 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 0));
            uint64_t t21 = t20;
            gm_closure t22 = t19;
            gm_node *ccx23 = t22.cx;
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_saqo50iv9jjmk, 8)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t22.code, t22.env, ccx23, &gm_tyb2nno7n67r, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 8), t28);
            gm_tabb710d9mq2e t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t32);
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
inline __attribute__((aligned(4))) void gm_f_format_jpvl2js(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_jpvl2js F;
    GM_STACK_CHECK(&F, 561u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s5dqpb4hur24f));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(36));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)112ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_th2d9c7aju8hp t18 = GM_LD(gm_th2d9c7aju8hp, ((uint8_t *)a0));
            F.l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_sdgejrh, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
            gm_b16_8 t21 = t20;
            gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 16));
            gm_b16_8 t23 = t22;
            gm_b16_8 t24 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 32));
            gm_b16_8 t25 = t24;
            gm_b16_8 t26 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 48));
            gm_b16_8 t27 = t26;
            gm_b16_8 t28 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 64));
            gm_b16_8 t29 = t28;
            gm_b16_8 t30 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 80));
            gm_b16_8 t31 = t30;
            gm_b16_8 t32 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 96));
            gm_b16_8 t33 = t32;
            gm_closure t34 = t19;
            gm_node *ccx35 = t34.cx;
            if (gm_pf0aqhubr4up(t34.code, t34.env, ccx35, gm_smooaouqmm8dj, 1)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t34.code, t34.env, ccx35, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t34.code, t34.env, ccx35, gm_sgfqei77bsia8, 3)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t34.code, t34.env, ccx35, &gm_ty0iqiubfm7e, ((uint8_t *)&t23), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t34.code, t34.env, ccx35, gm_scs1evojsik8q, 3)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t34.code, t34.env, ccx35, &gm_ty0iqiubfm7e, ((uint8_t *)&t25), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t34.code, t34.env, ccx35, gm_sdugj3qbhl84q, 4)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t34.code, t34.env, ccx35, &gm_ty0iqiubfm7e, ((uint8_t *)&t27), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t34.code, t34.env, ccx35, gm_s66b184memqnd, 3)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t34.code, t34.env, ccx35, &gm_ty0iqiubfm7e, ((uint8_t *)&t29), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t34.code, t34.env, ccx35, gm_scs1evojsik8q, 3)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t34.code, t34.env, ccx35, &gm_ty0iqiubfm7e, ((uint8_t *)&t31), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t34.code, t34.env, ccx35, gm_snukl6jqep6jf, 3)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t34.code, t34.env, ccx35, &gm_ty0iqiubfm7e, ((uint8_t *)&t33), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t34.code, t34.env, ccx35, gm_sqvepl1893o5g, 2)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t37 = F.l2;
        if (!t37) goto ie36;
        {
            void * t39 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t39);
        }
        {
            gm_t9qkh0rsooc8d t40;
            GM_ST(uint8_t, ((uint8_t *)&t40 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t41;
            GM_ST(uint8_t, (uint8_t *)&t41 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t41 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t41 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t41 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t41 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t41 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t41 + 16, 0);
            ((uint8_t *)&t41)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t41 + 8), t40);
            gm_tabb710d9mq2e t42 = t41;
            uint8_t t43 = ((((uint8_t *)&t42))[0] != 0);
            F.ret = t41;
            goto gm_fret;
        }
        goto ix36;
ie36:;
ix36:;
    }
    {
        void * t44 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t44);
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
inline __attribute__((aligned(4))) void gm_f_format_jr8ev5r(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_jr8ev5r F;
    gm_t3877pk7lv748 gm_l0; /* args */
    gm_t3877pk7lv748 gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 345u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sg6iftc6c3tph));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(46));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)24ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t3877pk7lv748 t18 = GM_LD(gm_t3877pk7lv748, ((uint8_t *)a0));
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_9d1icfo, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            uint64_t t22 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 16));
            uint64_t t23 = t22;
            gm_closure t24 = t19;
            gm_node *ccx25 = t24.cx;
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_s0jhhkkbgc6ks, 17)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t24.code, t24.env, ccx25, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_s7p349cm5h5jf, 19)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t24.code, t24.env, ccx25, &gm_tyb2nno7n67r, ((uint8_t *)&t23), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_sj1gcl0qo4i9p, 6)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t27 = F.l2;
        if (!t27) goto ie26;
        {
            void * t29 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t29);
        }
        {
            gm_t9qkh0rsooc8d t30;
            GM_ST(uint8_t, ((uint8_t *)&t30 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t31;
            GM_ST(uint8_t, (uint8_t *)&t31 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t31 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t31 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t31 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t31 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t31 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t31 + 16, 0);
            ((uint8_t *)&t31)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t31 + 8), t30);
            gm_tabb710d9mq2e t32 = t31;
            uint8_t t33 = ((((uint8_t *)&t32))[0] != 0);
            F.ret = t31;
            goto gm_fret;
        }
        goto ix26;
ie26:;
ix26:;
    }
    {
        void * t34 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t34);
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
inline __attribute__((aligned(4))) void gm_f_format_jrdns83(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_jrdns83 F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 305u);
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
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s2568a81aaotm));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(69));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)16ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_boqd22f, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_closure t22 = t19;
            gm_node *ccx23 = t22.cx;
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_se4r520bkfair, 17)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t22.code, t22.env, ccx23, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_sd6vqnf9glo8m, 50)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 8), t28);
            gm_tabb710d9mq2e t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t32);
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
inline __attribute__((aligned(4))) void gm_f_format_jrngvku(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_jrngvku F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 305u);
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
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_shmgu63rvoce3));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(35));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)16ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_ohjb0ee, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_closure t22 = t19;
            gm_node *ccx23 = t22.cx;
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_shpjiqltl04un, 32)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t22.code, t22.env, ccx23, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_sfm9elkq6bn2e, 1)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 8), t28);
            gm_tabb710d9mq2e t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t32);
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
inline __attribute__((aligned(4))) void gm_f_format_jru5qda(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_jru5qda F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 385u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s9vag5co36u2r));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(17));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)32ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t6j6smj7qoctu t18 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_rhf7rqh, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 16));
            gm_b16_8 t23 = t22;
            gm_closure t24 = t19;
            gm_node *ccx25 = t24.cx;
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_sfm9elkq6bn2e, 1)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t24.code, t24.env, ccx25, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_sl1f8ibfoc1i1, 12)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t24.code, t24.env, ccx25, &gm_ty0iqiubfm7e, ((uint8_t *)&t23), NULL)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t27 = F.l2;
        if (!t27) goto ie26;
        {
            void * t29 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t29);
        }
        {
            gm_t9qkh0rsooc8d t30;
            GM_ST(uint8_t, ((uint8_t *)&t30 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t31;
            GM_ST(uint8_t, (uint8_t *)&t31 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t31 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t31 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t31 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t31 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t31 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t31 + 16, 0);
            ((uint8_t *)&t31)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t31 + 8), t30);
            gm_tabb710d9mq2e t32 = t31;
            uint8_t t33 = ((((uint8_t *)&t32))[0] != 0);
            F.ret = t31;
            goto gm_fret;
        }
        goto ix26;
ie26:;
ix26:;
    }
    {
        void * t34 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t34);
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
inline __attribute__((aligned(4))) void gm_f_format_jth3mgf(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_jth3mgf F;
    GM_STACK_CHECK(&F, 417u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sbjshone12eeu));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(157));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)64ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t0uuhaljsf410 t18 = GM_LD(gm_t0uuhaljsf410, ((uint8_t *)a0));
            F.l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_37pdbfb, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
            gm_b16_8 t21 = t20;
            gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 16));
            gm_b16_8 t23 = t22;
            gm_b16_8 t24 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 32));
            gm_b16_8 t25 = t24;
            gm_b16_8 t26 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 48));
            gm_b16_8 t27 = t26;
            gm_closure t28 = t19;
            gm_node *ccx29 = t28.cx;
            if (gm_pf0aqhubr4up(t28.code, t28.env, ccx29, gm_slqq08j0fro21, 30)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t28.code, t28.env, ccx29, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t28.code, t28.env, ccx29, gm_s41bap0m04tt9, 11)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t28.code, t28.env, ccx29, &gm_ty0iqiubfm7e, ((uint8_t *)&t23), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t28.code, t28.env, ccx29, gm_svgk3n4sk1uat, 8)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t28.code, t28.env, ccx29, &gm_ty0iqiubfm7e, ((uint8_t *)&t25), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t28.code, t28.env, ccx29, gm_spa5q1834e71s, 48)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t28.code, t28.env, ccx29, &gm_ty0iqiubfm7e, ((uint8_t *)&t27), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t28.code, t28.env, ccx29, gm_so2blaqutst8g, 52)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t31 = F.l2;
        if (!t31) goto ie30;
        {
            void * t33 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t33);
        }
        {
            gm_t9qkh0rsooc8d t34;
            GM_ST(uint8_t, ((uint8_t *)&t34 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t35;
            GM_ST(uint8_t, (uint8_t *)&t35 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t35 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t35 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t35 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t35 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t35 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t35 + 16, 0);
            ((uint8_t *)&t35)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t35 + 8), t34);
            gm_tabb710d9mq2e t36 = t35;
            uint8_t t37 = ((((uint8_t *)&t36))[0] != 0);
            F.ret = t35;
            goto gm_fret;
        }
        goto ix30;
ie30:;
ix30:;
    }
    {
        void * t38 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t38);
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
inline __attribute__((aligned(4))) void gm_f_format_jtr5odb(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_jtr5odb F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 305u);
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
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_st984in2akmmc));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(42));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)16ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_bq2i257, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_closure t22 = t19;
            gm_node *ccx23 = t22.cx;
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_sfm9elkq6bn2e, 1)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t22.code, t22.env, ccx23, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_sdealia24icjj, 39)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 8), t28);
            gm_tabb710d9mq2e t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t32);
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
inline __attribute__((aligned(4))) void gm_f_format_jv6n7oh(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_jv6n7oh F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 385u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_scs7jonv1denn));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(63));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)32ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t6j6smj7qoctu t18 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_c0ffr5d, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 16));
            gm_b16_8 t23 = t22;
            gm_closure t24 = t19;
            gm_node *ccx25 = t24.cx;
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_sccadmt684uri, 22)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t24.code, t24.env, ccx25, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_sgc7crh43q9tj, 5)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t24.code, t24.env, ccx25, &gm_ty0iqiubfm7e, ((uint8_t *)&t23), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_srkbv7g2spehb, 32)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t27 = F.l2;
        if (!t27) goto ie26;
        {
            void * t29 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t29);
        }
        {
            gm_t9qkh0rsooc8d t30;
            GM_ST(uint8_t, ((uint8_t *)&t30 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t31;
            GM_ST(uint8_t, (uint8_t *)&t31 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t31 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t31 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t31 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t31 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t31 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t31 + 16, 0);
            ((uint8_t *)&t31)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t31 + 8), t30);
            gm_tabb710d9mq2e t32 = t31;
            uint8_t t33 = ((((uint8_t *)&t32))[0] != 0);
            F.ret = t31;
            goto gm_fret;
        }
        goto ix26;
ie26:;
ix26:;
    }
    {
        void * t34 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t34);
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
inline __attribute__((aligned(4))) void gm_f_format_jvf1ma2(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_jvf1ma2 F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 305u);
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
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sqcvjnsub5bfs));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(52));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)16ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_uavlrmf, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_closure t22 = t19;
            gm_node *ccx23 = t22.cx;
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_sbabluaguu2bc, 50)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t22.code, t22.env, ccx23, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 8), t28);
            gm_tabb710d9mq2e t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t32);
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
inline __attribute__((aligned(4))) void gm_f_parse_int_3ce02np(void *gm_out, gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_parse_int_3ce02np F;
    gm_b16_8 gm_l0; /* s */
    gm_tphtgnn9vgkcv gm_l1; /* sg */
    uint64_t gm_l2; /* i */
    uint32_t gm_l3; /* base */
    uint8_t gm_l4; /* c */
    GM_STACK_CHECK_SHALLOW(&F, 388u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint64_t t3 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        uint8_t t4 = ((t3 == ((uint64_t)0ULL)));
        if (!t4) goto ie2;
        {
            gm_ttiiv5vhiuvvr t6;
            GM_ST(uint8_t, (uint8_t *)&t6 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t6 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t6 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t6 + 8, 0);
            GM_ST(uint8_t, ((uint8_t *)&t6 + 0), ((uint8_t)0ULL));
            gm_tutnubt6u4hoq t7;
            GM_ST(uint8_t, (uint8_t *)&t7 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t7 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t7 + 4, 0);
            ((uint8_t *)&t7)[0] = 1;
            GM_ST(gm_ttiiv5vhiuvvr, ((uint8_t *)&t7 + 8), t6);
            gm_tutnubt6u4hoq t8 = t7;
            uint8_t t9 = ((((uint8_t *)&t8))[0] != 0);
            F.ret = t7;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        gm_b16_8 t10 = gm_l0;
        gm_t4mjne9lcbiv4 t11;
        gm_f_sign_utteujm((void *)&t11, t10);
        gm_t4mjne9lcbiv4 t12 = t11;
        if (((((uint8_t *)&t12))[0] != 0)) {
            gm_ttiiv5vhiuvvr t13 = GM_LD(gm_ttiiv5vhiuvvr, (((uint8_t *)&t12) + 8));
            gm_tutnubt6u4hoq t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_ttiiv5vhiuvvr, ((uint8_t *)&t14 + 8), t13);
            F.ret = t14;
            goto gm_fret;
        }
        gm_tphtgnn9vgkcv t15 = GM_LD(gm_tphtgnn9vgkcv, (((uint8_t *)&t12) + 8));
        gm_l1 = t15;
    }
    {
        uint64_t t16 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
        gm_l2 = t16;
    }
    {
        gm_l3 = ((uint32_t)10ULL);
    }
    {
        uint64_t t18 = gm_l2;
        uint64_t r19; bool o20 = __builtin_add_overflow(t18, ((uint64_t)1ULL), &r19);
        if (o20) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 373, 8);
        uint64_t t21 = r19;
        uint64_t t22 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        uint8_t t23 = (t21 < t22);
        if (!t23) goto ie17;
        uint64_t t24 = gm_l2;
        uint64_t t25 = (uintptr_t)t24;
        uint64_t t26 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        if (t25 >= (uintptr_t)(t26)) gm_trap_index(t25, t26, gm_src3d0v42kavhv7, 373, 26);
        uint8_t t27 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t25 * 1));
        uint8_t t28 = ((t27 == ((uint8_t)48ULL)));
        if (!t28) goto ie17;
        {
            uint64_t t30 = gm_l2;
            uint64_t r31; bool o32 = __builtin_add_overflow(t30, ((uint64_t)1ULL), &r31);
            if (o32) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 374, 31);
            uint64_t t33 = r31;
            uint64_t t34 = (uintptr_t)t33;
            uint64_t t35 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            if (t34 >= (uintptr_t)(t35)) gm_trap_index(t34, t35, gm_src3d0v42kavhv7, 374, 29);
            uint8_t t36 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t34 * 1));
            uint8_t t37 = gm_f_to_lower_0fu6efk(t36);
            gm_l4 = t37;
        }
        {
            uint8_t t39 = gm_l4;
            uint8_t t40 = ((t39 == ((uint8_t)120ULL)));
            if (!t40) goto ie38;
            {
                gm_l3 = ((uint32_t)16ULL);
            }
            goto ix38;
ie38:;
            uint8_t t43 = gm_l4;
            uint8_t t44 = ((t43 == ((uint8_t)111ULL)));
            if (!t44) goto ie42;
            {
                gm_l3 = ((uint32_t)8ULL);
            }
            goto ix42;
ie42:;
            uint8_t t47 = gm_l4;
            uint8_t t48 = ((t47 == ((uint8_t)98ULL)));
            if (!t48) goto ie46;
            {
                gm_l3 = ((uint32_t)2ULL);
            }
            goto ix46;
ie46:;
ix46:;
ix42:;
ix38:;
        }
        {
            uint32_t t51 = gm_l3;
            uint8_t t52 = (!(t51 == ((uint32_t)10ULL)));
            if (!t52) goto ie50;
            {
                uint64_t t54 = gm_l2;
                uint64_t r55; bool o56 = __builtin_add_overflow(t54, ((uint64_t)2ULL), &r55);
                if (o56) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 376, 29);
                uint64_t t57 = r55;
                gm_l2 = t57;
            }
            goto ix50;
ie50:;
ix50:;
        }
        goto ix17;
ie17:;
ix17:;
    }
    {
        gm_b16_8 t58 = gm_l0;
        uint64_t t59 = gm_l2;
        uint8_t t60 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        uint32_t t61 = gm_l3;
        gm_f_digits_nifrtn2((void *)&(*(gm_tutnubt6u4hoq *)gm_out), t58, t59, t60, t61);
        goto gm_fdone;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_tutnubt6u4hoq gm_f_parse_int_3ce02np_c(gm_b16_8 a0) { gm_tutnubt6u4hoq gm_r; gm_f_parse_int_3ce02np(&gm_r, a0); return gm_r; }
