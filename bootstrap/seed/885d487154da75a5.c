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
typedef struct gm_csguankko4lmhd { uint32_t f0; } gm_csguankko4lmhd;
typedef struct { uint64_t f0; uint64_t f1; } gm_t2lqu8jsj511d;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint64_t f0; } gm_t4ce0jd3tdro4;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint8_t f0; uint8_t p1[1]; uint16_t f1; } gm_tjcj4j2gbh6c8;
typedef struct { void * f0; void * f1; uint32_t f2; uint32_t f3; } gm_tmm67r2c94d5j;
typedef struct { uint8_t f0; uint8_t f1; } gm_tqs5p4n2tq19v;
typedef struct { uint16_t f0; } gm_tu0pangbbaeec;
typedef struct { uint8_t f0; uint8_t p1[3]; uint32_t f1; uint64_t f2; } gm_tuglhjjmj2g58;
_Static_assert(sizeof(gm_csguankko4lmhd) == 4 && _Alignof(gm_csguankko4lmhd) == 4 && offsetof(gm_csguankko4lmhd, f0) == 0, "layout of `Mutex`");
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; gm_t9qkh0rsooc8d f2; uint8_t p3[3]; gm_tf5srh4mp4b3i f3; } gm_t0mglksrkfa5e;
typedef struct { uint64_t f0; uint8_t f1; uint8_t p2[7]; uint64_t f2; gm_b16_8 f3; gm_b16_8 f4; uint8_t f5; uint8_t f6; uint8_t pe[6]; } gm_t0rfe1ou14h21;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; } gm_t0uuhaljsf410;
_Static_assert(sizeof(gm_t2lqu8jsj511d) == 16 && _Alignof(gm_t2lqu8jsj511d) == 8 && offsetof(gm_t2lqu8jsj511d, f0) == 0 && offsetof(gm_t2lqu8jsj511d, f1) == 8, "layout of `(u64, u64)`");
typedef struct { gm_b16_8 f0; uint64_t f1; } gm_t3877pk7lv748;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { gm_b16_8 f0; uint64_t f1; uint64_t f2; uint64_t f3; gm_t3mf58gv9kf3n f4; } gm_t4c7ipjj9s3fp;
_Static_assert(sizeof(gm_t4ce0jd3tdro4) == 8 && _Alignof(gm_t4ce0jd3tdro4) == 8 && offsetof(gm_t4ce0jd3tdro4, f0) == 0, "layout of `Instant`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tuglhjjmj2g58 v0; uint16_t v1; } f1; } gm_t5v8n6qrn4rh4;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
typedef struct { uint64_t f0; uint64_t f1; gm_b16_8 f2; } gm_tbccvikg6b96e;
typedef struct { uint64_t f0; uint64_t f1; uint64_t f2; gm_b16_8 f3; } gm_tbfdi36rjh0el;
typedef struct { uint8_t f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; } gm_tbngattl1fo51;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tcmkompbl09il;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; gm_t88rcucg1b7uk f2; } gm_tcmtrmhj7ak4u;
typedef struct { gm_b16_8 f0; uint32_t f1; uint8_t p2[4]; gm_b16_8 f2; } gm_temv198mna2jp;
typedef struct { uint64_t f0; gm_b16_8 f1; } gm_tf1bjg81u0glv;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; } gm_tfeab279c16rr;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; uint64_t f2; } gm_tftksr4op114e;
typedef struct { gm_b16_8 f0; uint64_t f1; uint64_t f2; } gm_tgbgti82mf879;
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; } gm_tgglga8ro3isc;
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_b16_8 f2; uint64_t f3; } gm_thddbqb6snabm;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
_Static_assert(sizeof(gm_tjcj4j2gbh6c8) == 4 && _Alignof(gm_tjcj4j2gbh6c8) == 2 && offsetof(gm_tjcj4j2gbh6c8, f0) == 0 && offsetof(gm_tjcj4j2gbh6c8, f1) == 2, "layout of `(signed: bool, bits: u16)`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t f1; uint8_t pe[3]; } gm_tkn42u7na3t81;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; } f1; } gm_tl572d7579hns;
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; uint8_t pe[4]; } gm_tlcm4lvu3g1a8;
typedef struct { gm_b16_8 f0; uint64_t f1; uint64_t f2; gm_tf5srh4mp4b3i f3; uint64_t f4; } gm_tml3m6miq9l2k;
_Static_assert(sizeof(gm_tmm67r2c94d5j) == 24 && _Alignof(gm_tmm67r2c94d5j) == 8 && offsetof(gm_tmm67r2c94d5j, f0) == 0 && offsetof(gm_tmm67r2c94d5j, f1) == 8 && offsetof(gm_tmm67r2c94d5j, f2) == 16 && offsetof(gm_tmm67r2c94d5j, f3) == 20, "layout of `Printer`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t88rcucg1b7uk v0; } f1; } gm_tnnjeerolos0a;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
typedef struct { uint32_t f0; uint32_t f1; gm_t88rcucg1b7uk f2; } gm_tov4mtrfp5e7v;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_b16_8 f2; } gm_tq3m5qgvkpje8;
_Static_assert(sizeof(gm_tqs5p4n2tq19v) == 2 && _Alignof(gm_tqs5p4n2tq19v) == 1 && offsetof(gm_tqs5p4n2tq19v, f0) == 0 && offsetof(gm_tqs5p4n2tq19v, f1) == 1, "layout of `(u8, u8)`");
typedef struct { gm_t88rcucg1b7uk a[2]; } gm_tqsk8dt6ib3qq;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
_Static_assert(sizeof(gm_tu0pangbbaeec) == 2 && _Alignof(gm_tu0pangbbaeec) == 2 && offsetof(gm_tu0pangbbaeec, f0) == 0, "layout of `(u16,)`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; gm_b16_8 f4; } gm_tud7oovfvnf43;
_Static_assert(sizeof(gm_tuglhjjmj2g58) == 16 && _Alignof(gm_tuglhjjmj2g58) == 8 && offsetof(gm_tuglhjjmj2g58, f0) == 0 && offsetof(gm_tuglhjjmj2g58, f1) == 4 && offsetof(gm_tuglhjjmj2g58, f2) == 8, "layout of `(signed: bool, bits: u32, storage: u64)`");
_Static_assert(sizeof(gm_t0mglksrkfa5e) == 40 && _Alignof(gm_t0mglksrkfa5e) == 8 && offsetof(gm_t0mglksrkfa5e, f0) == 0 && offsetof(gm_t0mglksrkfa5e, f1) == 16 && offsetof(gm_t0mglksrkfa5e, f2) == 20 && offsetof(gm_t0mglksrkfa5e, f3) == 24, "layout of `FieldDef`");
_Static_assert(sizeof(gm_t0rfe1ou14h21) == 64 && _Alignof(gm_t0rfe1ou14h21) == 8 && offsetof(gm_t0rfe1ou14h21, f0) == 0 && offsetof(gm_t0rfe1ou14h21, f1) == 8 && offsetof(gm_t0rfe1ou14h21, f2) == 16 && offsetof(gm_t0rfe1ou14h21, f3) == 24 && offsetof(gm_t0rfe1ou14h21, f4) == 40 && offsetof(gm_t0rfe1ou14h21, f5) == 56 && offsetof(gm_t0rfe1ou14h21, f6) == 57, "layout of `Target`");
_Static_assert(sizeof(gm_t0uuhaljsf410) == 64 && _Alignof(gm_t0uuhaljsf410) == 8 && offsetof(gm_t0uuhaljsf410, f0) == 0 && offsetof(gm_t0uuhaljsf410, f1) == 16 && offsetof(gm_t0uuhaljsf410, f2) == 32 && offsetof(gm_t0uuhaljsf410, f3) == 48, "layout of `(string, string, string, string)`");
typedef struct { uint64_t f0; gm_th2lqbo77pvsc f1; } gm_t13urt52695sh;
_Static_assert(sizeof(gm_t3877pk7lv748) == 24 && _Alignof(gm_t3877pk7lv748) == 8 && offsetof(gm_t3877pk7lv748, f0) == 0 && offsetof(gm_t3877pk7lv748, f1) == 16, "layout of `WasmReader`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tcmkompbl09il v0; gm_tiilmfd16v3fc v1; gm_b16_8 v2; } f1; } gm_t428ue2u3hf5g;
_Static_assert(sizeof(gm_t4c7ipjj9s3fp) == 56 && _Alignof(gm_t4c7ipjj9s3fp) == 8 && offsetof(gm_t4c7ipjj9s3fp, f0) == 0 && offsetof(gm_t4c7ipjj9s3fp, f1) == 16 && offsetof(gm_t4c7ipjj9s3fp, f2) == 24 && offsetof(gm_t4c7ipjj9s3fp, f3) == 32 && offsetof(gm_t4c7ipjj9s3fp, f4) == 40, "layout of `HashMap(TyKind, u32, null, null)`");
typedef struct { gm_t0rfe1ou14h21 f0; void * f1; } gm_t4grvdhp6dkf1;
typedef struct { gm_tcmkompbl09il f0; uint8_t f1; uint8_t pe[7]; } gm_t57euh612ligr;
_Static_assert(sizeof(gm_t5v8n6qrn4rh4) == 24 && _Alignof(gm_t5v8n6qrn4rh4) == 8 && offsetof(gm_t5v8n6qrn4rh4, f0) == 0 && offsetof(gm_t5v8n6qrn4rh4, f1) == 8, "layout of `Repr`");
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
typedef struct { gm_tgbgti82mf879 f0; uint8_t f1; uint8_t pe[7]; } gm_t8ldkt09h6492;
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; gm_tnnjeerolos0a f2; uint8_t pe[4]; } gm_t8nckeqfenl8a;
typedef struct { uint64_t f0; gm_t0mglksrkfa5e f1; } gm_t8ta482bl6be0;
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
_Static_assert(sizeof(gm_tbccvikg6b96e) == 32 && _Alignof(gm_tbccvikg6b96e) == 8 && offsetof(gm_tbccvikg6b96e, f0) == 0 && offsetof(gm_tbccvikg6b96e, f1) == 8 && offsetof(gm_tbccvikg6b96e, f2) == 16, "layout of `Layout`");
typedef struct { uint8_t f0; uint8_t p1[7]; gm_trqfvmjtt4sij f1; } gm_tbci5ospk5nnn;
_Static_assert(sizeof(gm_tbfdi36rjh0el) == 40 && _Alignof(gm_tbfdi36rjh0el) == 8 && offsetof(gm_tbfdi36rjh0el, f0) == 0 && offsetof(gm_tbfdi36rjh0el, f1) == 8 && offsetof(gm_tbfdi36rjh0el, f2) == 16 && offsetof(gm_tbfdi36rjh0el, f3) == 24, "layout of `(u64, u64, u64, string)`");
_Static_assert(sizeof(gm_tbngattl1fo51) == 8 && _Alignof(gm_tbngattl1fo51) == 4 && offsetof(gm_tbngattl1fo51, f0) == 0 && offsetof(gm_tbngattl1fo51, f1) == 4, "layout of `(is_const: bool, pointee: Ty)`");
typedef struct { gm_t4c7ipjj9s3fp f0; gm_trqfvmjtt4sij f1; uint8_t f2; uint8_t p3[7]; gm_trqfvmjtt4sij f3; gm_t4c7ipjj9s3fp f4; gm_t4c7ipjj9s3fp f5; } gm_tbpdf5ej15iof;
_Static_assert(sizeof(gm_tcmkompbl09il) == 24 && _Alignof(gm_tcmkompbl09il) == 8 && offsetof(gm_tcmkompbl09il, f0) == 0 && offsetof(gm_tcmkompbl09il, f1) == 8, "layout of `(ty: Ty, bytes: string)`");
_Static_assert(sizeof(gm_tcmtrmhj7ak4u) == 12 && _Alignof(gm_tcmtrmhj7ak4u) == 4 && offsetof(gm_tcmtrmhj7ak4u, f0) == 0 && offsetof(gm_tcmtrmhj7ak4u, f1) == 4 && offsetof(gm_tcmtrmhj7ak4u, f2) == 8, "layout of `(op: LenOp, lhs: Ty, rhs: Ty)`");
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t p2[4]; gm_tnpifv20irnph f2; uint8_t f3; uint8_t pe[7]; } gm_tddtjfggdat0d;
_Static_assert(sizeof(gm_temv198mna2jp) == 40 && _Alignof(gm_temv198mna2jp) == 8 && offsetof(gm_temv198mna2jp, f0) == 0 && offsetof(gm_temv198mna2jp, f1) == 16 && offsetof(gm_temv198mna2jp, f2) == 24, "layout of `(string, u32, string)`");
_Static_assert(sizeof(gm_tf1bjg81u0glv) == 24 && _Alignof(gm_tf1bjg81u0glv) == 8 && offsetof(gm_tf1bjg81u0glv, f0) == 0 && offsetof(gm_tf1bjg81u0glv, f1) == 8, "layout of `(i: usize, x: string)`");
_Static_assert(sizeof(gm_tfeab279c16rr) == 48 && _Alignof(gm_tfeab279c16rr) == 8 && offsetof(gm_tfeab279c16rr, f0) == 0 && offsetof(gm_tfeab279c16rr, f1) == 16 && offsetof(gm_tfeab279c16rr, f2) == 32, "layout of `Toolchain`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_tftksr4op114e) == 40 && _Alignof(gm_tftksr4op114e) == 8 && offsetof(gm_tftksr4op114e, f0) == 0 && offsetof(gm_tftksr4op114e, f1) == 16 && offsetof(gm_tftksr4op114e, f2) == 32, "layout of `(string, string, usize)`");
_Static_assert(sizeof(gm_tgbgti82mf879) == 32 && _Alignof(gm_tgbgti82mf879) == 8 && offsetof(gm_tgbgti82mf879, f0) == 0 && offsetof(gm_tgbgti82mf879, f1) == 16 && offsetof(gm_tgbgti82mf879, f2) == 24, "layout of `PayloadUnion`");
_Static_assert(sizeof(gm_tgglga8ro3isc) == 8 && _Alignof(gm_tgglga8ro3isc) == 4 && offsetof(gm_tgglga8ro3isc, f0) == 0 && offsetof(gm_tgglga8ro3isc, f1) == 4, "layout of `(err: Ty, ok: Ty)`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
_Static_assert(sizeof(gm_thddbqb6snabm) == 48 && _Alignof(gm_thddbqb6snabm) == 8 && offsetof(gm_thddbqb6snabm, f0) == 0 && offsetof(gm_thddbqb6snabm, f1) == 16 && offsetof(gm_thddbqb6snabm, f2) == 24 && offsetof(gm_thddbqb6snabm, f3) == 40, "layout of `(string, u64, string, u64)`");
typedef struct { gm_th2lqbo77pvsc f0; uint8_t f1; uint8_t pe[7]; } gm_ti5ramhglnvdt;
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
_Static_assert(sizeof(gm_tkn42u7na3t81) == 8 && _Alignof(gm_tkn42u7na3t81) == 4 && offsetof(gm_tkn42u7na3t81, f0) == 0 && offsetof(gm_tkn42u7na3t81, f1) == 4, "layout of `?Ty`");
_Static_assert(sizeof(gm_tl572d7579hns) == 24 && _Alignof(gm_tl572d7579hns) == 8 && offsetof(gm_tl572d7579hns, f0) == 0 && offsetof(gm_tl572d7579hns, f1) == 8, "layout of `string!()`");
_Static_assert(sizeof(gm_tlcm4lvu3g1a8) == 16 && _Alignof(gm_tlcm4lvu3g1a8) == 8 && offsetof(gm_tlcm4lvu3g1a8, f0) == 0 && offsetof(gm_tlcm4lvu3g1a8, f1) == 8, "layout of `(i: usize, x: Ty)`");
_Static_assert(sizeof(gm_tml3m6miq9l2k) == 56 && _Alignof(gm_tml3m6miq9l2k) == 8 && offsetof(gm_tml3m6miq9l2k, f0) == 0 && offsetof(gm_tml3m6miq9l2k, f1) == 16 && offsetof(gm_tml3m6miq9l2k, f2) == 24 && offsetof(gm_tml3m6miq9l2k, f3) == 32 && offsetof(gm_tml3m6miq9l2k, f4) == 48, "layout of `Member`");
_Static_assert(sizeof(gm_tnnjeerolos0a) == 8 && _Alignof(gm_tnnjeerolos0a) == 4 && offsetof(gm_tnnjeerolos0a, f0) == 0 && offsetof(gm_tnnjeerolos0a, f1) == 4, "layout of `ArrayKind`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
_Static_assert(sizeof(gm_tov4mtrfp5e7v) == 12 && _Alignof(gm_tov4mtrfp5e7v) == 4 && offsetof(gm_tov4mtrfp5e7v, f0) == 0 && offsetof(gm_tov4mtrfp5e7v, f1) == 4 && offsetof(gm_tov4mtrfp5e7v, f2) == 8, "layout of `BitField`");
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t p2[7]; gm_tf5srh4mp4b3i f2; gm_tkn42u7na3t81 f3; } gm_tpautr4br7h3l;
_Static_assert(sizeof(gm_tq3m5qgvkpje8) == 40 && _Alignof(gm_tq3m5qgvkpje8) == 8 && offsetof(gm_tq3m5qgvkpje8, f0) == 0 && offsetof(gm_tq3m5qgvkpje8, f1) == 16 && offsetof(gm_tq3m5qgvkpje8, f2) == 24, "layout of `(string, usize, string)`");
_Static_assert(sizeof(gm_tqsk8dt6ib3qq) == 8 && _Alignof(gm_tqsk8dt6ib3qq) == 4 && offsetof(gm_tqsk8dt6ib3qq, a) == 0, "layout of `[2]Ty`");
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
typedef struct { gm_t4c7ipjj9s3fp f0; } gm_ttiitul0263ld;
typedef struct { uint64_t f0; gm_tml3m6miq9l2k f1; } gm_ttos9gukqrqer;
typedef struct { gm_t6j6smj7qoctu f0; uint8_t f1; uint8_t pe[7]; } gm_ttu09lij36f8c;
_Static_assert(sizeof(gm_tud7oovfvnf43) == 80 && _Alignof(gm_tud7oovfvnf43) == 8 && offsetof(gm_tud7oovfvnf43, f0) == 0 && offsetof(gm_tud7oovfvnf43, f1) == 16 && offsetof(gm_tud7oovfvnf43, f2) == 32 && offsetof(gm_tud7oovfvnf43, f3) == 48 && offsetof(gm_tud7oovfvnf43, f4) == 64, "layout of `(string, string, string, string, string)`");
typedef struct { uint64_t f0; uint64_t f1; gm_tl572d7579hns f2; gm_b16_8 f3; } gm_tulpvg6gm4fqa;
_Static_assert(sizeof(gm_t13urt52695sh) == 32 && _Alignof(gm_t13urt52695sh) == 8 && offsetof(gm_t13urt52695sh, f0) == 0 && offsetof(gm_t13urt52695sh, f1) == 8, "layout of `(i: usize, x: ?string)`");
_Static_assert(sizeof(gm_t428ue2u3hf5g) == 32 && _Alignof(gm_t428ue2u3hf5g) == 8 && offsetof(gm_t428ue2u3hf5g, f0) == 0 && offsetof(gm_t428ue2u3hf5g, f1) == 8, "layout of `ConstKey`");
_Static_assert(sizeof(gm_t4grvdhp6dkf1) == 72 && _Alignof(gm_t4grvdhp6dkf1) == 8 && offsetof(gm_t4grvdhp6dkf1, f0) == 0 && offsetof(gm_t4grvdhp6dkf1, f1) == 64, "layout of `Layouts`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tjcj4j2gbh6c8 v0; uint16_t v1; gm_tbngattl1fo51 v2; gm_tbngattl1fo51 v3; gm_tbngattl1fo51 v4; gm_t8nckeqfenl8a v5; gm_t6j6smj7qoctu v6; gm_t88rcucg1b7uk v7; gm_tgglga8ro3isc v8; gm_tddtjfggdat0d v9; gm_tddtjfggdat0d v10; gm_tiilmfd16v3fc v11; gm_t88rcucg1b7uk v12; uint32_t v13; gm_tgglga8ro3isc v14; gm_t428ue2u3hf5g v15; gm_tcmtrmhj7ak4u v16; uint32_t v17; gm_tnpifv20irnph v18; uint32_t v19; } f1; } gm_t53at80pfdus3;
_Static_assert(sizeof(gm_t57euh612ligr) == 32 && _Alignof(gm_t57euh612ligr) == 8 && offsetof(gm_t57euh612ligr, f0) == 0 && offsetof(gm_t57euh612ligr, f1) == 24, "layout of `?BitsLayout`");
_Static_assert(sizeof(gm_t8ldkt09h6492) == 40 && _Alignof(gm_t8ldkt09h6492) == 8 && offsetof(gm_t8ldkt09h6492, f0) == 0 && offsetof(gm_t8ldkt09h6492, f1) == 32, "layout of `?PayloadUnion`");
_Static_assert(sizeof(gm_t8nckeqfenl8a) == 24 && _Alignof(gm_t8nckeqfenl8a) == 8 && offsetof(gm_t8nckeqfenl8a, f0) == 0 && offsetof(gm_t8nckeqfenl8a, f1) == 8 && offsetof(gm_t8nckeqfenl8a, f2) == 12, "layout of `(len: u64, elem: Ty, kind: ArrayKind)`");
_Static_assert(sizeof(gm_t8ta482bl6be0) == 48 && _Alignof(gm_t8ta482bl6be0) == 8 && offsetof(gm_t8ta482bl6be0, f0) == 0 && offsetof(gm_t8ta482bl6be0, f1) == 8, "layout of `(i: usize, x: FieldDef)`");
_Static_assert(sizeof(gm_tbci5ospk5nnn) == 48 && _Alignof(gm_tbci5ospk5nnn) == 8 && offsetof(gm_tbci5ospk5nnn, f0) == 0 && offsetof(gm_tbci5ospk5nnn, f1) == 8, "layout of `Int`");
_Static_assert(sizeof(gm_tbpdf5ej15iof) == 256 && _Alignof(gm_tbpdf5ej15iof) == 8 && offsetof(gm_tbpdf5ej15iof, f0) == 0 && offsetof(gm_tbpdf5ej15iof, f1) == 56 && offsetof(gm_tbpdf5ej15iof, f2) == 96 && offsetof(gm_tbpdf5ej15iof, f3) == 104 && offsetof(gm_tbpdf5ej15iof, f4) == 144 && offsetof(gm_tbpdf5ej15iof, f5) == 200, "layout of `Views`");
typedef struct { gm_b16_8 f0; gm_tkn42u7na3t81 f1; gm_tbci5ospk5nnn f2; } gm_tckdobunaulee;
_Static_assert(sizeof(gm_tddtjfggdat0d) == 48 && _Alignof(gm_tddtjfggdat0d) == 8 && offsetof(gm_tddtjfggdat0d, f0) == 0 && offsetof(gm_tddtjfggdat0d, f1) == 16 && offsetof(gm_tddtjfggdat0d, f2) == 24 && offsetof(gm_tddtjfggdat0d, f3) == 40, "layout of `FnTy`");
_Static_assert(sizeof(gm_ti5ramhglnvdt) == 32 && _Alignof(gm_ti5ramhglnvdt) == 8 && offsetof(gm_ti5ramhglnvdt, f0) == 0 && offsetof(gm_ti5ramhglnvdt, f1) == 24, "layout of `??string`");
_Static_assert(sizeof(gm_tpautr4br7h3l) == 48 && _Alignof(gm_tpautr4br7h3l) == 8 && offsetof(gm_tpautr4br7h3l, f0) == 0 && offsetof(gm_tpautr4br7h3l, f1) == 16 && offsetof(gm_tpautr4br7h3l, f2) == 24 && offsetof(gm_tpautr4br7h3l, f3) == 40, "layout of `StructDef`");
typedef struct { void * f0; gm_t4grvdhp6dkf1 f1; gm_trqfvmjtt4sij f2; gm_t4c7ipjj9s3fp f3; gm_t4c7ipjj9s3fp f4; gm_ttiitul0263ld f5; gm_trqfvmjtt4sij f6; gm_trqfvmjtt4sij f7; gm_trqfvmjtt4sij f8; gm_t4c7ipjj9s3fp f9; gm_trqfvmjtt4sij f10; gm_t4c7ipjj9s3fp f11; gm_trqfvmjtt4sij f12; gm_t4c7ipjj9s3fp f13; gm_t4c7ipjj9s3fp f14; gm_trqfvmjtt4sij f15; gm_t4c7ipjj9s3fp f16; uint8_t f17; uint8_t p18[7]; gm_t4c7ipjj9s3fp f18; gm_tbpdf5ej15iof f19; } gm_trg1s7erfs461;
_Static_assert(sizeof(gm_ttiitul0263ld) == 56 && _Alignof(gm_ttiitul0263ld) == 8 && offsetof(gm_ttiitul0263ld, f0) == 0, "layout of `Namer`");
_Static_assert(sizeof(gm_ttos9gukqrqer) == 64 && _Alignof(gm_ttos9gukqrqer) == 8 && offsetof(gm_ttos9gukqrqer, f0) == 0 && offsetof(gm_ttos9gukqrqer, f1) == 8, "layout of `(i: usize, x: Member)`");
_Static_assert(sizeof(gm_ttu09lij36f8c) == 40 && _Alignof(gm_ttu09lij36f8c) == 8 && offsetof(gm_ttu09lij36f8c, f0) == 0 && offsetof(gm_ttu09lij36f8c, f1) == 32, "layout of `?Split`");
_Static_assert(sizeof(gm_tulpvg6gm4fqa) == 56 && _Alignof(gm_tulpvg6gm4fqa) == 8 && offsetof(gm_tulpvg6gm4fqa, f0) == 0 && offsetof(gm_tulpvg6gm4fqa, f1) == 8 && offsetof(gm_tulpvg6gm4fqa, f2) == 16 && offsetof(gm_tulpvg6gm4fqa, f3) == 40, "layout of `BlobDef`");
_Static_assert(sizeof(gm_t53at80pfdus3) == 56 && _Alignof(gm_t53at80pfdus3) == 8 && offsetof(gm_t53at80pfdus3, f0) == 0 && offsetof(gm_t53at80pfdus3, f1) == 8, "layout of `TyKind`");
_Static_assert(sizeof(gm_tckdobunaulee) == 72 && _Alignof(gm_tckdobunaulee) == 8 && offsetof(gm_tckdobunaulee, f0) == 0 && offsetof(gm_tckdobunaulee, f1) == 16 && offsetof(gm_tckdobunaulee, f2) == 24, "layout of `VariantDef`");
_Static_assert(sizeof(gm_trg1s7erfs461) == 1128 && _Alignof(gm_trg1s7erfs461) == 8 && offsetof(gm_trg1s7erfs461, f0) == 0 && offsetof(gm_trg1s7erfs461, f1) == 8 && offsetof(gm_trg1s7erfs461, f2) == 80 && offsetof(gm_trg1s7erfs461, f3) == 120 && offsetof(gm_trg1s7erfs461, f4) == 176 && offsetof(gm_trg1s7erfs461, f5) == 232 && offsetof(gm_trg1s7erfs461, f6) == 288 && offsetof(gm_trg1s7erfs461, f7) == 328 && offsetof(gm_trg1s7erfs461, f8) == 368 && offsetof(gm_trg1s7erfs461, f9) == 408 && offsetof(gm_trg1s7erfs461, f10) == 464 && offsetof(gm_trg1s7erfs461, f11) == 504 && offsetof(gm_trg1s7erfs461, f12) == 560 && offsetof(gm_trg1s7erfs461, f13) == 600 && offsetof(gm_trg1s7erfs461, f14) == 656 && offsetof(gm_trg1s7erfs461, f15) == 712 && offsetof(gm_trg1s7erfs461, f16) == 752 && offsetof(gm_trg1s7erfs461, f17) == 808 && offsetof(gm_trg1s7erfs461, f18) == 816 && offsetof(gm_trg1s7erfs461, f19) == 872, "layout of `CTypes`");
struct gm_fr_add_member_name_j3i0eje {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_adt_of_gej99vn {
    gm_frame h;
    void * ret;
};
struct gm_fr_align_of_k6eqqrn {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_align_uu2harl {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_assert_message_6bd9l6u {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* out */
    gm_b16_8 ret;
};
struct gm_fr_bit_width_u86m8ir {
    gm_frame h;
    uint32_t ret;
};
struct gm_fr_bits_layout_23rect9 {
    gm_frame h;
    gm_t57euh612ligr ret;
};
struct gm_fr_blob_before_9p1c704 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_blob_kind_kp13j9k {
    gm_frame h;
    gm_tl572d7579hns ret;
};
struct gm_fr_blob_name_sjpa1ub {
    gm_frame h;
    gm_trqfvmjtt4sij l4; /* lanes */
    gm_b16_8 ret;
};
struct gm_fr_blob_typedef_8ta3rbi {
    gm_frame h;
    gm_trqfvmjtt4sij l7; /* members */
    gm_b16_8 ret;
};
struct gm_fr_blob_typedefs_4nfn4l8 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* out */
    gm_trqfvmjtt4sij l2; /* sorted */
    gm_tulpvg6gm4fqa l5; /* b */
    gm_b16_8 ret;
};
struct gm_fr_bool_ty_kotvo8t {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_bool_ty_pn2p277 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_by_ref_fi0o860 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_c_string_n7h27b3 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* out */
    gm_b16_8 ret;
};
struct gm_fr_content_name_83mn5uf {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_cty_66ee0kf {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_ctypes_j6au0st {
    gm_frame h;
    gm_t0rfe1ou14h21 l1; /* t */
    gm_trg1s7erfs461 ret;
};
struct gm_fr_define_typed_m3mqpm3 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_desc_0cag2kq {
    gm_frame h;
    gm_trqfvmjtt4sij l4; /* f */
    gm_trqfvmjtt4sij l16; /* parts */
    gm_tpautr4br7h3l l32; /* sd */
    gm_trqfvmjtt4sij l34; /* parts */
    gm_t0mglksrkfa5e l38; /* fd */
    gm_trqfvmjtt4sij l41; /* parts */
    gm_t0mglksrkfa5e l45; /* fd */
    gm_trqfvmjtt4sij l51; /* parts */
    gm_tckdobunaulee l54; /* v */
    gm_trqfvmjtt4sij l57; /* fields */
    gm_b16_8 ret;
};
struct gm_fr_descriptors_fnnm3h1 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_08mdrht {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_2g727l2 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_5nn7ph0 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ab79bmt {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_hm6i8si {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ijtp9lo {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_jckt05t {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_k21v01c {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ose1ba9 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ppl3r45 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_qsq36pn {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_rcd2jln {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_vu26gaj {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_field_cty_nfj8rcv {
    gm_frame h;
    gm_ttu09lij36f8c ret;
};
struct gm_fr_fields_array_tikg33t {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_6gkpg0v {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_for_body_0h3rgr6 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_get_p217beu {
    gm_frame h;
    gm_tqs5p4n2tq19v ret;
};
struct gm_fr_hash_map_1s9eugt {
    gm_frame h;
    gm_t4c7ipjj9s3fp ret;
};
struct gm_fr_hash_map_cuh4fs9 {
    gm_frame h;
    gm_t4c7ipjj9s3fp ret;
};
struct gm_fr_hash_map_m04clhi {
    gm_frame h;
    gm_t4c7ipjj9s3fp ret;
};
struct gm_fr_hash_map_p3t5tj8 {
    gm_frame h;
    gm_t4c7ipjj9s3fp ret;
};
struct gm_fr_hash_map_tl7apid {
    gm_frame h;
    gm_t4c7ipjj9s3fp ret;
};
struct gm_fr_int_ci6airg {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_int_cty_s62af0i {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_int_ty_7nma1b7 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_is_float_gthejtb {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_integer_8d4f9ek {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_integer_eo6jkc5 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_never_in5ftma {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_signed_integer_ige9b74 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_unit_17uennd {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_unit_n83ttnb {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_vector_fm7q9br {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_void_vs2outo {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_keyed_h72lrop {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_kind_ghvav22 {
    gm_frame h;
    gm_t53at80pfdus3 ret;
};
struct gm_fr_kind_jcnt4hm {
    gm_frame h;
    gm_t53at80pfdus3 ret;
};
struct gm_fr_kind_rank_g1kdlst {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_layout_of_e6hfj1r {
    gm_frame h;
    gm_tbccvikg6b96e ret;
};
struct gm_fr_layouts_au8nfbt {
    gm_frame h;
    gm_t0rfe1ou14h21 l1; /* t */
    gm_t4grvdhp6dkf1 ret;
};
struct gm_fr_list_0c2v515 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_792g2go {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_cfvjofe {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_epvducg {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_nof7vfj {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_nvrrj0t {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_uvfu1k1 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_make_cty_3kvgdd5 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_make_mirror_rudvl6p {
    gm_frame h;
    gm_tpautr4br7h3l l7; /* def */
    gm_trqfvmjtt4sij l10; /* names */
    gm_trqfvmjtt4sij l11; /* field_names */
    gm_t0mglksrkfa5e l14; /* f */
    gm_t4c7ipjj9s3fp l15; /* taken */
    gm_trqfvmjtt4sij l23; /* members */
    gm_t0mglksrkfa5e l27; /* f */
    gm_trqfvmjtt4sij l34; /* attrs */
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_make_typed_2bh0dpj {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* items */
    gm_trqfvmjtt4sij l7; /* named */
    gm_tpautr4br7h3l l22; /* def */
    gm_t0mglksrkfa5e l27; /* f */
    gm_trqfvmjtt4sij l31; /* payloads */
    gm_tckdobunaulee l34; /* v */
    gm_trqfvmjtt4sij l37; /* members */
    gm_trqfvmjtt4sij l38; /* named */
    gm_tml3m6miq9l2k l44; /* m */
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_member_names_17el54k {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* out */
    gm_b16_8 ret;
    gm_b0_1 w0;
};
struct gm_fr_mirror_lcjegsf {
    gm_frame h;
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_mirror_text_cr5muc9 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_new_namer_5q88o9g {
    gm_frame h;
    gm_ttiitul0263ld ret;
};
struct gm_fr_nullable_pointer_7qfmn6o {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_offsets_eh6vfle {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_param_cty_e8tvhrm {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_payload_union_feshns2 {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* members */
    gm_t8ldkt09h6492 ret;
};
struct gm_fr_plain_cty_q9lhlci {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_printer_h7jigd7 {
    gm_frame h;
    gm_tmm67r2c94d5j ret;
};
struct gm_fr_ptr_bits_glps5i7 {
    gm_frame h;
    uint32_t ret;
};
struct gm_fr_repr_53quqki {
    gm_frame h;
    gm_t5v8n6qrn4rh4 ret;
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
struct gm_fr_slice_5dt9121 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_sort_by_55adsi9 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_sort_skeree7 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_string_ty_j74ff0o {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_string_ty_kk94re3 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_to_decimal_8bshn5b {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_trqfvmjtt4sij l1; /* out */
    gm_b16_8 ret;
};
struct gm_fr_typed_stkcgvr {
    gm_frame h;
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_u8_ty_afkvj25 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_unique_members_mrojmjf {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_unit_2age34u {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_unit_ty_vgceilu {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_usize_ty_9mijhtc {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_usize_ty_qnrd30f {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_vector_typedef_vpgf6u8 {
    gm_frame h;
    gm_b16_8 ret;
};
inline __attribute__((aligned(4))) gm_b0_1 gm_f_add_member_name_j3i0eje(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void * gm_f_adt_of_gej99vn(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) uint64_t gm_f_align_of_k6eqqrn(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) uint64_t gm_f_align_uu2harl(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_assert_message_6bd9l6u(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) uint32_t gm_f_bit_width_u86m8ir(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) void gm_f_bits_layout_23rect9(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_bits_layout_u6kkccp(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) uint8_t gm_f_blob_before_9p1c704(void * a0, void * a1);
__attribute__((aligned(4))) void gm_f_blob_kind_kp13j9k(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, uint64_t a2, uint64_t a3);
__attribute__((aligned(4))) gm_b16_8 gm_f_blob_name_sjpa1ub(void *env, gm_node *cx, gm_node *inst, uint64_t a0, uint64_t a1, const void * a2);
__attribute__((aligned(4))) gm_b16_8 gm_f_blob_typedef_8ta3rbi(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, uint64_t a2, const void * a3, gm_b16_8 a4);
__attribute__((aligned(4))) gm_b16_8 gm_f_blob_typedefs_4nfn4l8(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_bool_ty_kotvo8t(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_bool_ty_pn2p277(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) uint8_t gm_f_by_ref_fi0o860(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_c_string_n7h27b3(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_content_name_83mn5uf(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_cty_66ee0kf(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_ctypes_j6au0st(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_define_typed_m3mqpm3(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, gm_b16_8 a3, uint64_t a4);
__attribute__((aligned(4))) gm_b16_8 gm_f_desc_0cag2kq(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_descriptors_fnnm3h1(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_08mdrht(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_2g727l2(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_5nn7ph0(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ab79bmt(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_hm6i8si(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_jckt05t(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_k21v01c(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ose1ba9(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ppl3r45(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_qsq36pn(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_rcd2jln(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_vu26gaj(void);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_extend_0cs85gs(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_extend_glhj3bs(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) uint32_t gm_f_field_bits_2ktfm1k(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_field_cty_nfj8rcv(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_fields_array_tikg33t(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2, void * a3);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_find_722uua4(void * a0, void * a1);
inline __attribute__((aligned(4))) uint8_t gm_f_fn_6gkpg0v(void * a0, void * a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_0h3rgr6(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_format_19e6va7(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_19ftgop(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_1cgbgnn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_1dfo4pq(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_1il5da4(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_38m1q8c(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t2lqu8jsj511d a0);
__attribute__((aligned(4))) void gm_f_format_3buamil(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_4v9fh5b(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
__attribute__((aligned(4))) void gm_f_format_6a91mh8(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_8n626pi(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_9117rt8(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
__attribute__((aligned(4))) void gm_f_format_a4r7k06(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_aiomp5b(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
__attribute__((aligned(4))) void gm_f_format_aqb9tiu(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_c19sabv(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_ch41dev(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
__attribute__((aligned(4))) void gm_f_format_df9v4jq(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t9qkh0rsooc8d a0);
__attribute__((aligned(4))) void gm_f_format_dtmr1c2(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_ef193cn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
__attribute__((aligned(4))) void gm_f_format_ef6t3dr(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t2lqu8jsj511d a0);
__attribute__((aligned(4))) void gm_f_format_f6fn313(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t2lqu8jsj511d a0);
__attribute__((aligned(4))) void gm_f_format_g0ibdjv(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_gkjg5ld(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_h6b2ofo(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_io38sjt(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
__attribute__((aligned(4))) void gm_f_format_irg2n6e(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
__attribute__((aligned(4))) void gm_f_format_jpvhgp3(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
__attribute__((aligned(4))) void gm_f_format_ka5ced8(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_kl8i1sq(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
__attribute__((aligned(4))) void gm_f_format_km55tit(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
__attribute__((aligned(4))) void gm_f_format_ktg7ckd(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
__attribute__((aligned(4))) void gm_f_format_lkeo172(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t9qkh0rsooc8d a0);
__attribute__((aligned(4))) void gm_f_format_mfspujt(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
__attribute__((aligned(4))) void gm_f_format_ms4f9aj(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tu0pangbbaeec a0);
__attribute__((aligned(4))) void gm_f_format_n3gtmrf(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_nbg44fl(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_o1q76rb(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_onn3qvb(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t9qkh0rsooc8d a0);
__attribute__((aligned(4))) void gm_f_format_p144a0o(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_qc4s0m5(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
__attribute__((aligned(4))) void gm_f_format_s44fp1q(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_s51nisc(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_s9gvae7(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_tbvfjdi(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
__attribute__((aligned(4))) void gm_f_format_tc7tn98(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
__attribute__((aligned(4))) void gm_f_format_tjkla39(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_tra8sop(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_udu99n6(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_vgl5p2e(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_vsbf5l9(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_get_9k2rdij(void *gm_out, void * a0, uint32_t a1);
__attribute__((aligned(4))) void gm_f_get_e3joqcc(void *gm_out, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_get_gnmase9(void *gm_out, void * a0, uint32_t a1);
inline __attribute__((aligned(4))) gm_tqs5p4n2tq19v gm_f_get_p217beu(void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) void gm_f_hash_map_1s9eugt(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_hash_map_cuh4fs9(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_hash_map_m04clhi(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_hash_map_p3t5tj8(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_hash_map_tl7apid(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_b16_8 gm_f_hash_text_4aekv10(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, uint64_t a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_int_ci6airg(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, uint16_t a2);
__attribute__((aligned(4))) gm_b16_8 gm_f_int_cty_s62af0i(uint8_t a0, uint64_t a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_int_ty_7nma1b7(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, uint16_t a2);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_intern_ia19mhl(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_into_slice_hq0te56(void *gm_out, void * a0);
__attribute__((aligned(4))) void gm_f_introsort_r6p6ckl(gm_b16_8 a0, gm_closure a1);
__attribute__((aligned(4))) void gm_f_introsort_shnrbt7(gm_b16_8 a0, gm_closure a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_float_gthejtb(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_integer_8d4f9ek(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_integer_eo6jkc5(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_never_in5ftma(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) uint8_t gm_f_is_nullable_pointer_nkie06s(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_signed_integer_ige9b74(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) uint8_t gm_f_is_signed_integer_s66djm7(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_unit_17uennd(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_unit_n83ttnb(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) uint8_t gm_f_is_vector_fm7q9br(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_void_vs2outo(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void gm_f_join_sfknljb(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_keyed_h72lrop(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_keys_qc5bjlr_s31qjug(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) void gm_f_kind_ghvav22(void *gm_out, void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) gm_t53at80pfdus3 gm_f_kind_ghvav22_c(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void gm_f_kind_jcnt4hm(void *gm_out, void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) uint8_t gm_f_kind_rank_g1kdlst(const void * a0);
__attribute__((aligned(4))) uint8_t gm_f_kind_rank_g1kdlst_c(gm_tl572d7579hns a0);
__attribute__((aligned(4))) void gm_f_layout_a5ovnrm(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) void gm_f_layout_of_e6hfj1r(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) void gm_f_layouts_au8nfbt(void *gm_out, void * a0, const void * a1);
inline __attribute__((aligned(4))) void gm_f_list_0c2v515(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_792g2go(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_cfvjofe(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_epvducg(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_nof7vfj(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_nvrrj0t(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_b16_8 gm_f_make_cty_3kvgdd5(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_make_mirror_rudvl6p(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_make_typed_2bh0dpj(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) uint8_t gm_f_may_hold_pointers_iuorcah(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_member_name_mj2bvs3(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_member_names_17el54k(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_mirror_lcjegsf(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_mirror_text_cr5muc9(void * a0);
inline __attribute__((aligned(4))) void gm_f_new_namer_5q88o9g(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) uint8_t gm_f_nullable_pointer_7qfmn6o(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_offsets_eh6vfle(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_param_cty_e8tvhrm(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_payload_union_feshns2(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_plain_cty_q9lhlci(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_pointer_words_ljph5no(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) void gm_f_printer_h7jigd7(void *gm_out, void * a0, void * a1);
inline __attribute__((aligned(4))) uint32_t gm_f_ptr_bits_glps5i7(void * a0);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_3ef746b(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_d0271rf(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_k3l9nic(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_m55rf90(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_pmmlu39(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_q9fsb22(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_t83rg2m(void * a0, uint8_t a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_5c6qgui(void * a0, gm_b16_8 a1, uint8_t a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_7shfu9u(void * a0, gm_b16_8 a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_dm7u1sg(void * a0, uint32_t a1, const void * a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_po09vud(void * a0, uint32_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_repr_53quqki(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_5qbi623(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_kmjeh8s(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) uint64_t gm_f_size_dmhtkib(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) uint64_t gm_f_size_of_k0fluj2(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_slice_5dt9121(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, gm_t88rcucg1b7uk a2);
inline __attribute__((aligned(4))) void gm_f_sort_by_55adsi9(gm_b16_8 a0, gm_closure a1);
inline __attribute__((aligned(4))) void gm_f_sort_skeree7(gm_b16_8 a0);
__attribute__((aligned(4))) uint8_t gm_f_starts_with_6vvt01e(gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_string_ty_j74ff0o(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_string_ty_kk94re3(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_to_decimal_8bshn5b(void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_ty_vka2r8h(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_typed_stkcgvr(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_u8_ty_afkvj25(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_unique_fba8sj3(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_unique_members_mrojmjf(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, void * a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_unique_with_mffff7e(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, void * a1, uint8_t a2);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_unit_2age34u(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_unit_ty_vgceilu(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_usize_ty_9mijhtc(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_usize_ty_qnrd30f(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_vector_typedef_vpgf6u8(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2, uint64_t a3, uint64_t a4);
__attribute__((aligned(4))) void gm_f_views_bc1b3r4(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_1h8ac6i(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_2h6ucjl(void * a0, gm_t4ce0jd3tdro4 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_2hik1mg(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_3f2sd55(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_3mmgun5(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_712qp7n(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_cscq5c8(void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_write_decimal_ke367ab(void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_f1h65c6(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_fle5ipv(void * a0, gm_t9qkh0rsooc8d a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_pecnce5(void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_r7au1ks(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_trmu0m5(void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_uf08lpv(void * a0, const void * a1);
inline bool gm_lt0iqiubfm7e(const uint8_t *a, const uint8_t *b);
extern void *const gm_hlak2jvvbqg4[];
extern uint8_t gm_kr85ffi32os[24] __attribute__((aligned(8)));
extern const uint8_t gm_s04qus0luoga5[5];
extern const uint8_t gm_s0f7epcithn0r[18];
extern const uint8_t gm_s1a1r1qe3ssot[9];
extern const uint8_t gm_s2ph9f020upd2[7];
extern const uint8_t gm_s3p0rbs9hc3lg[2];
extern const uint8_t gm_s3p8gug7bvl1p[3];
extern const uint8_t gm_s44ek1gecbajs[7];
extern const uint8_t gm_s4cess8gum6i7[6];
extern const uint8_t gm_s58f7dj21gtf9[18];
extern const uint8_t gm_s6g2mpli2mu72[11];
extern const uint8_t gm_s6r94a2k1goos[9];
extern const uint8_t gm_s76d71nockpip[17];
extern const uint8_t gm_s7qhcf46f682d[21];
extern const uint8_t gm_s866ofni9sb8u[7];
extern const uint8_t gm_s8p1h96ji9kh0[6];
extern const uint8_t gm_s9grtrvtmi962[8];
extern const uint8_t gm_sa9ktdbn49iui[16];
extern const uint8_t gm_sbec11r2rv4ti[23];
extern const uint8_t gm_sc0vbtqan26pc[18];
extern const uint8_t gm_sc49g68j2cekh[7];
extern const uint8_t gm_scj0sm2kpivk4[9];
extern const uint8_t gm_scttf7jtr4fn9[6];
extern const uint8_t gm_sdam23bqvafqn[18];
extern const uint8_t gm_sf1p9u1vovjso[5];
extern const uint8_t gm_sg5gdnrith2i6[3];
extern const uint8_t gm_shn8mjp9kc2n2[9];
extern const uint8_t gm_shrq42dntcbrd[19];
extern const uint8_t gm_siblunnp4nahm[10];
extern const uint8_t gm_ska87i9f7924f[8];
extern const uint8_t gm_sl3q52ep6aa53[2];
extern const uint8_t gm_slad86501rp5c[19];
extern const uint8_t gm_sm6ioruobq064[6];
extern const uint8_t gm_smee1janbl2t9[3];
extern const uint8_t gm_smiavg3q7ucd9[8];
extern const uint8_t gm_snokcvhtidt97[6];
extern const uint8_t gm_so3qrsvnd4euf[12];
extern const uint8_t gm_so4g53vn8c2d4[8];
extern const uint8_t gm_spi406027g4jo[19];
extern const uint8_t gm_sqtc9fffut4s2[13];
extern const uint8_t gm_sr4qbrk6tlgaa[1];
extern const char gm_src0ih77f43mhf2[];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_src4o48ksu43djd[];
extern const char gm_srcbsc23is03ttm[];
extern const char gm_srcht195tfrfuqo[];
extern const char gm_srckfibkpt681aa[];
extern const char gm_srclv3pj5es0f8o[];
extern const char gm_srcrgc9bkquf6nr[];
extern const uint8_t gm_sropo1941pvlt[16];
extern const uint8_t gm_st53sho4849cg[18];
extern const uint8_t gm_st9g0aublffth[18];
extern const uint8_t gm_std3g89n1j9ln[8];
extern const uint8_t gm_suggn3kp9dfmg[9];
extern const uint8_t gm_suuq26jjqhul4[7];
extern const uint8_t gm_suvf0cqih9u7i[17];
extern const gm_type gm_tyo86ks9qkvt;
inline __attribute__((aligned(4))) gm_b0_1 gm_f_add_member_name_j3i0eje(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1) {
    struct gm_fr_add_member_name_j3i0eje F;
    void * gm_l0; /* ty */
    gm_b16_8 gm_l1; /* name */
    GM_STACK_CHECK(&F, 60u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        void * t3 = (void *)(((uint8_t *)t2) + 816);
        gm_b16_8 t4 = gm_l1;
        gm_tftb1odc739t5 t5 = gm_f_put_5c6qgui(t3, t4, ((uint8_t)1));
        gm_tftb1odc739t5 t6 = t5;
        if (((((uint8_t *)&t6))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t6) + 1, gm_srckfibkpt681aa, 450, 5);
        gm_b0_1 t7 = GM_LD(gm_b0_1, (((uint8_t *)&t6) + 1));
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
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
inline __attribute__((aligned(4))) uint32_t gm_f_bit_width_u86m8ir(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_bit_width_u86m8ir F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    void * t2 = GM_LD(void *, (((uint8_t *)t1) + 0));
    gm_t88rcucg1b7uk t3 = gm_l1;
    uint32_t t4 = gm_f_field_bits_2ktfm1k((void *)NULL, cx, NULL, t2, t3);
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
inline __attribute__((aligned(4))) void gm_f_bits_layout_23rect9(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_bits_layout_23rect9 F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    void * t2 = GM_LD(void *, (((uint8_t *)t1) + 0));
    gm_t88rcucg1b7uk t3 = gm_l1;
    gm_f_bits_layout_u6kkccp((void *)&(*(gm_t57euh612ligr *)gm_out), (void *)NULL, cx, NULL, t2, t3);
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_bool_ty_kotvo8t(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_bool_ty_kotvo8t F;
    void * gm_l0; /* ty */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    void * t2 = GM_LD(void *, (((uint8_t *)t1) + 0));
    void * t3 = (void *)(((uint8_t *)t2) + 8);
    gm_t88rcucg1b7uk t4 = gm_f_bool_ty_pn2p277((void *)NULL, cx, NULL, t3);
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_bool_ty_pn2p277(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_bool_ty_pn2p277 F;
    void * gm_l0; /* t */
    GM_STACK_CHECK(&F, 84u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    gm_t53at80pfdus3 t2;
    memset(&t2, 0, sizeof t2);
    GM_ST(uint8_t, ((uint8_t *)&t2 + 0), ((uint8_t)4ULL));
    gm_t88rcucg1b7uk t3 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t1, &t2);
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
inline __attribute__((aligned(4))) uint8_t gm_f_by_ref_fi0o860(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_by_ref_fi0o860 F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 97u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint8_t t1 = 0;
    void * t2 = gm_l0;
    gm_t88rcucg1b7uk t3 = gm_l1;
    gm_t5v8n6qrn4rh4 t4;
    gm_f_repr_53quqki((void *)&t4, (void *)NULL, cx, NULL, t2, t3);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_t5v8n6qrn4rh4 t5 = t4;
    uint8_t t6 = ((GM_LD(uint8_t, ((uint8_t *)&t5) + 0) == ((uint8_t)6ULL)));
    if (t6) {
        void * t7 = gm_l0;
        gm_t88rcucg1b7uk t8 = gm_l1;
        uint64_t t9 = gm_f_size_dmhtkib((void *)NULL, cx, NULL, t7, t8);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t10 = (t9 > ((uint64_t)16ULL));
        t1 = t10;
    } else t1 = 0;
    F.ret = t1;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_content_name_83mn5uf(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1) {
    struct gm_fr_content_name_83mn5uf F;
    gm_b16_8 gm_l0; /* prefix */
    gm_b16_8 gm_l1; /* content */
    GM_STACK_CHECK(&F, 240u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_t6j6smj7qoctu t1;
    gm_b16_8 t2 = gm_l0;
    GM_ST(gm_b16_8, (uint8_t *)&t1 + 0, t2);
    gm_b16_8 t3 = gm_l1;
    gm_b16_8 t4 = gm_f_hash_text_4aekv10((void *)NULL, cx, NULL, t3, ((uint64_t)12ULL));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    GM_ST(gm_b16_8, (uint8_t *)&t1 + 16, t4);
    gm_tabb710d9mq2e t5;
    gm_f_format_8n626pi((void *)&t5, (void *)NULL, cx, NULL, &t1);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tabb710d9mq2e t6 = t5;
    if (((((uint8_t *)&t6))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t6) + 8, gm_srcht195tfrfuqo, 84, 82);
    gm_b16_8 t7 = GM_LD(gm_b16_8, (((uint8_t *)&t6) + 8));
    gm_b16_8 t8 = t7;
    gm_b16_8 t9 = GM_LD(gm_b16_8, ((uint8_t *)&t8));
    F.ret = t9;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_descriptors_fnnm3h1(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_descriptors_fnnm3h1 F;
    void * gm_l0; /* ty */
    GM_STACK_CHECK(&F, 256u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tfeab279c16rr t1;
    void * t2 = gm_l0;
    gm_b16_8 t3 = GM_LD(gm_b16_8, ((((uint8_t *)t2) + 288) + 0));
    GM_ST(gm_b16_8, (uint8_t *)&t1 + 0, t3);
    void * t4 = gm_l0;
    gm_b16_8 t5 = GM_LD(gm_b16_8, ((((uint8_t *)t4) + 328) + 0));
    GM_ST(gm_b16_8, (uint8_t *)&t1 + 16, t5);
    void * t6 = gm_l0;
    gm_b16_8 t7 = GM_LD(gm_b16_8, ((((uint8_t *)t6) + 368) + 0));
    GM_ST(gm_b16_8, (uint8_t *)&t1 + 32, t7);
    gm_tabb710d9mq2e t8;
    gm_f_format_o1q76rb((void *)&t8, (void *)NULL, cx, NULL, &t1);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tabb710d9mq2e t9 = t8;
    if (((((uint8_t *)&t9))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t9) + 8, gm_srckfibkpt681aa, 627, 63);
    gm_b16_8 t10 = GM_LD(gm_b16_8, (((uint8_t *)&t9) + 8));
    gm_b16_8 t11 = t10;
    gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
    F.ret = t12;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_08mdrht(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_08mdrht F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_2g727l2(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_2g727l2 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ab79bmt(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_ab79bmt F;
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
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 56)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_hm6i8si(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_hm6i8si F;
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
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 56)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_jckt05t(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_jckt05t F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_k21v01c(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_k21v01c F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ose1ba9(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_ose1ba9 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_qsq36pn(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_qsq36pn F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_rcd2jln(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_rcd2jln F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_vu26gaj(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_vu26gaj F;
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
inline __attribute__((aligned(4))) uint8_t gm_f_fn_6gkpg0v(void * a0, void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_fn_6gkpg0v F;
    void * gm_l0; /* a */
    void * gm_l1; /* b */
    GM_STACK_CHECK_SHALLOW(&F, 99u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    gm_b16_8 t2 = GM_LD(gm_b16_8, ((uint8_t *)t1));
    void * t3 = gm_l1;
    gm_b16_8 t4 = GM_LD(gm_b16_8, ((uint8_t *)t3));
    gm_b16_8 t5 = t2;
    gm_b16_8 t6 = t4;
    uint8_t t7 = (uint8_t)gm_lt0iqiubfm7e(((uint8_t *)&t5), ((uint8_t *)&t6));
    F.ret = t7;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_0h3rgr6(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_for_body_0h3rgr6 F;
    gm_b16_8 gm_l0; /* <item> */
    gm_b16_8 gm_l1; /* k */
    GM_STACK_CHECK(&F, 76u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t3 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l0));
    gm_l1 = t3;
    {
        void * t5 = (void *)((uint8_t *)&((struct gm_fr_member_names_17el54k *)((&F.h)->up))->l1);
        gm_b16_8 t6 = gm_l1;
        gm_tftb1odc739t5 t7 = gm_f_push_3ef746b(t5, t6);
        gm_tftb1odc739t5 t8 = t7;
        if (((((uint8_t *)&t8))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t8) + 1, gm_srckfibkpt681aa, 456, 37);
        gm_b0_1 t9 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
    }
    goto me1;
mn2:;
    gm_trap("no match arm matched the scrutinee", gm_srckfibkpt681aa, 456, 5);
me1:;
    F.ret = GM_ZERO(gm_b0_1);
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
inline __attribute__((aligned(4))) void gm_f_hash_map_1s9eugt(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_hash_map_1s9eugt F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_b16_8 t2 = gm_f_empty_slice_08mdrht();
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
inline __attribute__((aligned(4))) void gm_f_hash_map_cuh4fs9(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_hash_map_cuh4fs9 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_b16_8 t2 = gm_f_empty_slice_k21v01c();
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
inline __attribute__((aligned(4))) void gm_f_hash_map_m04clhi(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_hash_map_m04clhi F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_b16_8 t2 = gm_f_empty_slice_jckt05t();
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
inline __attribute__((aligned(4))) void gm_f_hash_map_p3t5tj8(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_hash_map_p3t5tj8 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_b16_8 t2 = gm_f_empty_slice_vu26gaj();
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
inline __attribute__((aligned(4))) void gm_f_hash_map_tl7apid(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_hash_map_tl7apid F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_b16_8 t2 = gm_f_empty_slice_qsq36pn();
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_int_ci6airg(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, uint16_t a2) {
    struct gm_fr_int_ci6airg F;
    void * gm_l0; /* t */
    uint8_t gm_l1; /* signed */
    uint16_t gm_l2; /* bits */
    GM_STACK_CHECK(&F, 94u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t1 = gm_l0;
    uint8_t t2 = gm_l1;
    uint16_t t3 = gm_l2;
    gm_tjcj4j2gbh6c8 t4;
    GM_ST(uint8_t, (uint8_t *)&t4 + 1, 0);
    GM_ST(uint8_t, ((uint8_t *)&t4 + 0), t2);
    GM_ST(uint16_t, ((uint8_t *)&t4 + 2), t3);
    gm_t53at80pfdus3 t5;
    memset(&t5, 0, sizeof t5);
    GM_ST(uint8_t, ((uint8_t *)&t5 + 0), ((uint8_t)0ULL));
    GM_ST(gm_tjcj4j2gbh6c8, ((uint8_t *)&t5 + 8), t4);
    gm_t88rcucg1b7uk t6 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t1, &t5);
    if (gm_unw.kind) {
        goto gm_lad;
    }
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_int_ty_7nma1b7(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, uint16_t a2) {
    struct gm_fr_int_ty_7nma1b7 F;
    void * gm_l0; /* ty */
    uint8_t gm_l1; /* signed */
    uint16_t gm_l2; /* bits */
    GM_STACK_CHECK(&F, 50u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t1 = gm_l0;
    void * t2 = GM_LD(void *, (((uint8_t *)t1) + 0));
    void * t3 = (void *)(((uint8_t *)t2) + 8);
    uint8_t t4 = gm_l1;
    uint16_t t5 = gm_l2;
    gm_t88rcucg1b7uk t6 = gm_f_int_ci6airg((void *)NULL, cx, NULL, t3, t4, t5);
    if (gm_unw.kind) {
        goto gm_lad;
    }
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_float_gthejtb(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_float_gthejtb F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK_SHALLOW(&F, 156u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    void * t1 = gm_l0;
    void * t2 = GM_LD(void *, (((uint8_t *)t1) + 0));
    void * t3 = (void *)(((uint8_t *)t2) + 8);
    gm_t88rcucg1b7uk t4 = gm_l1;
    gm_t53at80pfdus3 t5;
    gm_f_kind_jcnt4hm((void *)&t5, t3, GM_CONV(gm_csguankko4lmhd, t4));
    gm_t53at80pfdus3 t6 = t5;
    uint8_t t7;
    uint8_t t10 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
    if (t10 != ((uint8_t)3ULL)) goto mn9;
    t7 = ((uint8_t)1);
    goto me8;
mn9:;
    t7 = ((uint8_t)0);
    goto me8;
mn11:;
    gm_trap("no match arm matched the scrutinee", gm_srckfibkpt681aa, 112, 53);
me8:;
    F.ret = t7;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_integer_8d4f9ek(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_integer_8d4f9ek F;
    void * gm_l0; /* t */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    GM_STACK_CHECK_SHALLOW(&F, 142u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    void * t1 = gm_l0;
    gm_t88rcucg1b7uk t2 = gm_l1;
    gm_t53at80pfdus3 t3;
    gm_f_kind_jcnt4hm((void *)&t3, t1, GM_CONV(gm_csguankko4lmhd, t2));
    gm_t53at80pfdus3 t4 = t3;
    uint8_t t5;
    uint8_t t10 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
    if (t10 != ((uint8_t)0ULL)) goto pa9;
    goto po8;
pa9:;
    uint8_t t12 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
    if (t12 != ((uint8_t)1ULL)) goto pa11;
    goto po8;
pa11:;
    uint8_t t14 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
    if (t14 != ((uint8_t)2ULL)) goto pa13;
    goto po8;
pa13:;
    goto mn7;
po8:;
    t5 = ((uint8_t)1);
    goto me6;
mn7:;
    t5 = ((uint8_t)0);
    goto me6;
mn15:;
    gm_trap("no match arm matched the scrutinee", gm_src0ih77f43mhf2, 374, 58);
me6:;
    F.ret = t5;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_integer_eo6jkc5(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_integer_eo6jkc5 F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK_SHALLOW(&F, 43u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    void * t1 = gm_l0;
    void * t2 = GM_LD(void *, (((uint8_t *)t1) + 0));
    void * t3 = (void *)(((uint8_t *)t2) + 8);
    gm_t88rcucg1b7uk t4 = gm_l1;
    uint8_t t5 = gm_f_is_integer_8d4f9ek(t3, GM_CONV(gm_csguankko4lmhd, t4));
    F.ret = t5;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_never_in5ftma(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_never_in5ftma F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK_SHALLOW(&F, 155u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    void * t1 = gm_l0;
    void * t2 = GM_LD(void *, (((uint8_t *)t1) + 0));
    void * t3 = (void *)(((uint8_t *)t2) + 8);
    gm_t88rcucg1b7uk t4 = gm_l1;
    gm_t53at80pfdus3 t5;
    gm_f_kind_jcnt4hm((void *)&t5, t3, GM_CONV(gm_csguankko4lmhd, t4));
    gm_t53at80pfdus3 t6 = t5;
    uint8_t t7 = ((GM_LD(uint8_t, ((uint8_t *)&t6) + 0) == ((uint8_t)5ULL)));
    F.ret = t7;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_signed_integer_ige9b74(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_signed_integer_ige9b74 F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK_SHALLOW(&F, 43u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    void * t1 = gm_l0;
    void * t2 = GM_LD(void *, (((uint8_t *)t1) + 0));
    void * t3 = (void *)(((uint8_t *)t2) + 8);
    gm_t88rcucg1b7uk t4 = gm_l1;
    uint8_t t5 = gm_f_is_signed_integer_s66djm7(t3, GM_CONV(gm_csguankko4lmhd, t4));
    F.ret = t5;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_unit_17uennd(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_unit_17uennd F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK_SHALLOW(&F, 43u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    void * t1 = gm_l0;
    void * t2 = GM_LD(void *, (((uint8_t *)t1) + 0));
    void * t3 = (void *)(((uint8_t *)t2) + 8);
    gm_t88rcucg1b7uk t4 = gm_l1;
    uint8_t t5 = gm_f_is_unit_n83ttnb(t3, GM_CONV(gm_csguankko4lmhd, t4));
    F.ret = t5;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_unit_n83ttnb(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_unit_n83ttnb F;
    void * gm_l0; /* t */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    gm_b16_8 gm_l2; /* elems */
    GM_STACK_CHECK_SHALLOW(&F, 182u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    {
        void * t3 = gm_l0;
        gm_t88rcucg1b7uk t4 = gm_l1;
        gm_t53at80pfdus3 t5;
        gm_f_kind_jcnt4hm((void *)&t5, t3, GM_CONV(gm_csguankko4lmhd, t4));
        gm_t53at80pfdus3 t6 = t5;
        uint8_t t7 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
        if (t7 != ((uint8_t)12ULL)) goto ie2;
        gm_b16_8 t8 = GM_LD(gm_b16_8, ((((uint8_t *)&t6) + 8) + 0));
        gm_l2 = t8;
        {
            uint64_t t10 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
            uint8_t t11 = ((t10 == ((uint64_t)0ULL)));
            uint8_t t12 = 0;
            F.ret = t11;
            goto gm_fret;
        }
        goto ix2;
ie2:;
        {
            uint8_t t14 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
ix2:;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_void_vs2outo(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_void_vs2outo F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK_SHALLOW(&F, 41u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    uint8_t t1 = 0;
    void * t2 = gm_l0;
    gm_t88rcucg1b7uk t3 = gm_l1;
    uint8_t t4 = gm_f_is_unit_17uennd(t2, GM_CONV(gm_csguankko4lmhd, t3));
    if (!t4) {
        void * t5 = gm_l0;
        gm_t88rcucg1b7uk t6 = gm_l1;
        uint8_t t7 = gm_f_is_never_in5ftma(t5, GM_CONV(gm_csguankko4lmhd, t6));
        t1 = t7;
    } else t1 = 1;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_keyed_h72lrop(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2) {
    struct gm_fr_keyed_h72lrop F;
    void * gm_l0; /* n */
    gm_b16_8 gm_l1; /* prefix */
    gm_b16_8 gm_l2; /* key */
    GM_STACK_CHECK(&F, 272u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t1 = gm_l0;
    gm_t6j6smj7qoctu t2;
    gm_b16_8 t3 = gm_l1;
    GM_ST(gm_b16_8, (uint8_t *)&t2 + 0, t3);
    gm_b16_8 t4 = gm_l2;
    gm_b16_8 t5 = gm_f_hash_text_4aekv10((void *)NULL, cx, NULL, t4, ((uint64_t)10ULL));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    GM_ST(gm_b16_8, (uint8_t *)&t2 + 16, t5);
    gm_tabb710d9mq2e t6;
    gm_f_format_8n626pi((void *)&t6, (void *)NULL, cx, NULL, &t2);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tabb710d9mq2e t7 = t6;
    if (((((uint8_t *)&t7))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t7) + 8, gm_srcht195tfrfuqo, 80, 91);
    gm_b16_8 t8 = GM_LD(gm_b16_8, (((uint8_t *)&t7) + 8));
    gm_b16_8 t9 = t8;
    gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
    gm_b16_8 t11 = gm_f_unique_fba8sj3((void *)NULL, cx, NULL, t1, t10);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.ret = t11;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
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
inline __attribute__((aligned(4))) uint8_t gm_f_kind_rank_g1kdlst(const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_kind_rank_g1kdlst F;
    gm_tl572d7579hns gm_l0; /* k */
    GM_STACK_CHECK_SHALLOW(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    uint8_t t1;
    uint8_t t8 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    switch (t8) { case ((uint8_t)0ULL): goto ma3; case ((uint8_t)1ULL): goto ma4; case ((uint8_t)2ULL): goto ma5; case ((uint8_t)3ULL): goto ma6; default: goto ma7; }
ma3:;
    uint8_t t10 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t10 != ((uint8_t)0ULL)) goto mn9;
    t1 = ((uint8_t)0ULL);
    goto me2;
mn9:;
ma4:;
    uint8_t t12 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t12 != ((uint8_t)1ULL)) goto mn11;
    t1 = ((uint8_t)1ULL);
    goto me2;
mn11:;
ma5:;
    uint8_t t14 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t14 != ((uint8_t)2ULL)) goto mn13;
    t1 = ((uint8_t)2ULL);
    goto me2;
mn13:;
ma6:;
    uint8_t t16 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t16 != ((uint8_t)3ULL)) goto mn15;
    t1 = ((uint8_t)3ULL);
    goto me2;
mn15:;
ma7:;
    gm_trap("no match arm matched the scrutinee", gm_srckfibkpt681aa, 691, 39);
me2:;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
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
inline __attribute__((aligned(4))) void gm_f_layouts_au8nfbt(void *gm_out, void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_layouts_au8nfbt F;
    void * gm_l0; /* s */
    GM_STACK_CHECK_SHALLOW(&F, 16u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy((uint8_t *)&(*(gm_t4grvdhp6dkf1 *)gm_out) + 0, ((uint8_t *)a1), 64);
    void * t1 = gm_l0;
    GM_ST(void *, (uint8_t *)&(*(gm_t4grvdhp6dkf1 *)gm_out) + 64, t1);
    goto gm_fdone;
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
inline __attribute__((aligned(4))) void gm_f_list_792g2go(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_792g2go F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_ab79bmt();
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
inline __attribute__((aligned(4))) void gm_f_list_cfvjofe(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_cfvjofe F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_2g727l2();
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
inline __attribute__((aligned(4))) void gm_f_list_nof7vfj(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_nof7vfj F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_rcd2jln();
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
inline __attribute__((aligned(4))) void gm_f_list_nvrrj0t(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_nvrrj0t F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_hm6i8si();
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_mirror_text_cr5muc9(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_mirror_text_cr5muc9 F;
    void * gm_l0; /* ty */
    GM_STACK_CHECK_SHALLOW(&F, 96u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    gm_b16_8 t2 = GM_LD(gm_b16_8, ((((uint8_t *)t1) + 712) + 0));
    gm_b16_8 t3 = t2;
    gm_b16_8 t4 = GM_LD(gm_b16_8, ((uint8_t *)&t3));
    F.ret = t4;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_new_namer_5q88o9g(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_new_namer_5q88o9g F;
    GM_STACK_CHECK(&F, 56u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_t4c7ipjj9s3fp t1;
    gm_f_hash_map_m04clhi((void *)&t1, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    GM_ST(gm_t4c7ipjj9s3fp, (uint8_t *)&(*(gm_ttiitul0263ld *)gm_out) + 0, t1);
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
inline __attribute__((aligned(4))) uint8_t gm_f_nullable_pointer_7qfmn6o(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_nullable_pointer_7qfmn6o F;
    void * gm_l0; /* s */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK_SHALLOW(&F, 27u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    void * t1 = gm_l0;
    gm_t88rcucg1b7uk t2 = gm_l1;
    uint8_t t3 = gm_f_is_nullable_pointer_nkie06s(t1, GM_CONV(gm_csguankko4lmhd, t2));
    F.ret = t3;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_offsets_eh6vfle(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_offsets_eh6vfle F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 144u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    void * t2 = (void *)(((uint8_t *)t1) + 8);
    gm_t88rcucg1b7uk t3 = gm_l1;
    gm_tbccvikg6b96e t4;
    gm_f_layout_of_e6hfj1r((void *)&t4, (void *)NULL, cx, NULL, t2, t3);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tbccvikg6b96e t5 = t4;
    gm_b16_8 t6 = GM_LD(gm_b16_8, (((uint8_t *)&t5) + 16));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_param_cty_e8tvhrm(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_param_cty_e8tvhrm F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 149u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_b16_8 t1;
    void * t3 = gm_l0;
    gm_t88rcucg1b7uk t4 = gm_l1;
    uint8_t t5 = gm_f_by_ref_fi0o860((void *)NULL, cx, NULL, t3, t4);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    if (!t5) goto ie2;
    gm_b16_8 t7;
    GM_ST(void *, (uint8_t *)&t7, (void *)(gm_sqtc9fffut4s2));
    GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)(12));
    gm_b16_8 t8 = t7;
    t1 = t8;
    goto ix2;
ie2:;
    void * t10 = gm_l0;
    gm_t88rcucg1b7uk t11 = gm_l1;
    gm_b16_8 t12 = gm_f_cty_66ee0kf((void *)NULL, cx, NULL, t10, t11);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_b16_8 t13 = t12;
    t1 = t13;
ix2:;
    F.ret = t1;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_plain_cty_q9lhlci(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_plain_cty_q9lhlci F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_b16_8 gm_l2; /* m */
    GM_STACK_CHECK(&F, 182u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t3 = gm_l0;
        gm_t88rcucg1b7uk t4 = gm_l1;
        gm_th2lqbo77pvsc t5;
        gm_f_mirror_lcjegsf((void *)&t5, (void *)NULL, cx, NULL, t3, t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_th2lqbo77pvsc t6 = t5;
        if (!((((uint8_t *)&t6))[16] != 0)) goto ie2;
        gm_b16_8 t7 = GM_LD(gm_b16_8, ((uint8_t *)&t6));
        gm_l2 = t7;
        {
            gm_b16_8 t9 = gm_l2;
            uint8_t t10 = 0;
            F.ret = t9;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t11 = gm_l0;
        gm_t88rcucg1b7uk t12 = gm_l1;
        gm_b16_8 t13 = gm_f_cty_66ee0kf((void *)NULL, cx, NULL, t11, t12);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t14 = 0;
        F.ret = t13;
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
inline __attribute__((aligned(4))) uint32_t gm_f_ptr_bits_glps5i7(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_ptr_bits_glps5i7 F;
    void * gm_l0; /* t */
    GM_STACK_CHECK_SHALLOW(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    uint64_t t2 = GM_LD(uint64_t, (((uint8_t *)t1) + 0));
    uint64_t r3; bool o4 = __builtin_mul_overflow(t2, ((uint64_t)8ULL), &r3);
    if (o4) gm_trap("integer overflow in `*`", gm_src4o48ksu43djd, 91, 49);
    uint64_t t5 = r3;
    uint32_t t6 = (uint32_t)t5;
    F.ret = t6;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_slice_5dt9121(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, gm_t88rcucg1b7uk a2) {
    struct gm_fr_slice_5dt9121 F;
    void * gm_l0; /* t */
    uint8_t gm_l1; /* is_const */
    gm_t88rcucg1b7uk gm_l2; /* elem */
    GM_STACK_CHECK(&F, 102u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t1 = gm_l0;
    uint8_t t2 = gm_l1;
    gm_t88rcucg1b7uk t3 = gm_l2;
    gm_tbngattl1fo51 t4;
    GM_ST(uint8_t, (uint8_t *)&t4 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t4 + 2, 0);
    GM_ST(uint8_t, ((uint8_t *)&t4 + 0), t2);
    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t4 + 4), t3);
    gm_t53at80pfdus3 t5;
    memset(&t5, 0, sizeof t5);
    GM_ST(uint8_t, ((uint8_t *)&t5 + 0), ((uint8_t)10ULL));
    GM_ST(gm_tbngattl1fo51, ((uint8_t *)&t5 + 8), t4);
    gm_t88rcucg1b7uk t6 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t1, &t5);
    if (gm_unw.kind) {
        goto gm_lad;
    }
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
inline __attribute__((aligned(4))) void gm_f_sort_by_55adsi9(gm_b16_8 a0, gm_closure a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_sort_by_55adsi9 F;
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
        gm_f_introsort_shnrbt7(t2, t3);
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
inline __attribute__((aligned(4))) void gm_f_sort_skeree7(gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_sort_skeree7 F;
    gm_b16_8 gm_l0; /* s */
    GM_STACK_CHECK(&F, 56u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_b16_8 t2 = gm_l0;
        gm_closure t3 = ((gm_closure){GM_PTR_TAG(gm_f_fn_6gkpg0v), NULL, cx});
        gm_f_introsort_r6p6ckl(t2, t3);
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_string_ty_j74ff0o(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_string_ty_j74ff0o F;
    void * gm_l0; /* t */
    GM_STACK_CHECK(&F, 40u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    void * t2 = gm_l0;
    gm_t88rcucg1b7uk t3 = gm_f_u8_ty_afkvj25((void *)NULL, cx, NULL, t2);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_t88rcucg1b7uk t4 = gm_f_slice_5dt9121((void *)NULL, cx, NULL, t1, ((uint8_t)1), t3);
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_string_ty_kk94re3(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_string_ty_kk94re3 F;
    void * gm_l0; /* ty */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    void * t2 = GM_LD(void *, (((uint8_t *)t1) + 0));
    void * t3 = (void *)(((uint8_t *)t2) + 8);
    gm_t88rcucg1b7uk t4 = gm_f_string_ty_j74ff0o((void *)NULL, cx, NULL, t3);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_to_decimal_8bshn5b(void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_to_decimal_8bshn5b F;
    GM_STACK_CHECK(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memcpy(&F.l0, a0, sizeof F.l0);
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b0_1 t3 = gm_f_write_decimal_ke367ab((void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l0)), t2);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t4 = (void *)((uint8_t *)&F.l1);
        gm_tabb710d9mq2e t5;
        gm_f_into_slice_hq0te56((void *)&t5, t4);
        gm_tabb710d9mq2e t6 = t5;
        if (((((uint8_t *)&t6))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t6) + 8, gm_srclv3pj5es0f8o, 624, 12);
        gm_b16_8 t7 = GM_LD(gm_b16_8, (((uint8_t *)&t6) + 8));
        uint8_t t8 = 0;
        F.ret = t7;
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_u8_ty_afkvj25(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_u8_ty_afkvj25 F;
    void * gm_l0; /* t */
    GM_STACK_CHECK(&F, 28u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    gm_t88rcucg1b7uk t2 = gm_f_int_ci6airg((void *)NULL, cx, NULL, t1, ((uint8_t)0), ((uint16_t)8ULL));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.ret = t2;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_unit_2age34u(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_unit_2age34u F;
    void * gm_l0; /* t */
    GM_STACK_CHECK(&F, 212u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    gm_t6j6smj7qoctu t2;
    gm_b16_8 t3 = gm_f_empty_slice_rcd2jln();
    gm_b16_8 t4 = t3;
    gm_b16_8 t5 = GM_LD(gm_b16_8, ((uint8_t *)&t4));
    GM_ST(gm_b16_8, (uint8_t *)&t2 + 0, t5);
    gm_b16_8 t6 = gm_f_empty_slice_5nn7ph0();
    gm_b16_8 t7 = t6;
    gm_b16_8 t8 = GM_LD(gm_b16_8, ((uint8_t *)&t7));
    GM_ST(gm_b16_8, (uint8_t *)&t2 + 16, t8);
    gm_t53at80pfdus3 t9;
    GM_ST(uint8_t, (uint8_t *)&t9 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t9 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t9 + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&t9 + 40, 0);
    GM_ST(uint64_t, (uint8_t *)&t9 + 48, 0);
    GM_ST(uint8_t, ((uint8_t *)&t9 + 0), ((uint8_t)12ULL));
    GM_ST(gm_t6j6smj7qoctu, ((uint8_t *)&t9 + 8), t2);
    gm_t88rcucg1b7uk t10 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t1, &t9);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.ret = t10;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_unit_ty_vgceilu(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_unit_ty_vgceilu F;
    void * gm_l0; /* ty */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    void * t2 = GM_LD(void *, (((uint8_t *)t1) + 0));
    void * t3 = (void *)(((uint8_t *)t2) + 8);
    gm_t88rcucg1b7uk t4 = gm_f_unit_2age34u((void *)NULL, cx, NULL, t3);
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_usize_ty_9mijhtc(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_usize_ty_9mijhtc F;
    void * gm_l0; /* ty */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    void * t2 = GM_LD(void *, (((uint8_t *)t1) + 0));
    void * t3 = (void *)(((uint8_t *)t2) + 8);
    gm_t88rcucg1b7uk t4 = gm_f_usize_ty_qnrd30f((void *)NULL, cx, NULL, t3);
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_usize_ty_qnrd30f(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_usize_ty_qnrd30f F;
    void * gm_l0; /* t */
    GM_STACK_CHECK(&F, 84u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    gm_t53at80pfdus3 t2;
    memset(&t2, 0, sizeof t2);
    GM_ST(uint8_t, ((uint8_t *)&t2 + 0), ((uint8_t)1ULL));
    gm_t88rcucg1b7uk t3 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t1, &t2);
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
inline bool gm_lt0iqiubfm7e(const uint8_t *a, const uint8_t *b) {
    uintptr_t na = GM_LD(uintptr_t, a + 8), nb = GM_LD(uintptr_t, b + 8);
    const uint8_t *pa = GM_LD(void *, a), *pb = GM_LD(void *, b);
    uintptr_t m = na < nb ? na : nb;
    int c = m == 0 ? 0 : memcmp(pa, pb, m);
    return c != 0 ? c < 0 : na < nb;
}
extern __attribute__((aligned(4))) gm_b0_1 gm_f_add_member_name_j3i0eje(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) void * gm_f_adt_of_gej99vn(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_adt_of_gej99vn F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    uint32_t gm_l2; /* id */
    GM_STACK_CHECK_SHALLOW(&F, 230u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    {
        void * t3 = gm_l0;
        void * t4 = GM_LD(void *, (((uint8_t *)t3) + 0));
        void * t5 = (void *)(((uint8_t *)t4) + 8);
        gm_t88rcucg1b7uk t6 = gm_l1;
        gm_t53at80pfdus3 t7;
        gm_f_kind_jcnt4hm((void *)&t7, t5, GM_CONV(gm_csguankko4lmhd, t6));
        gm_t53at80pfdus3 t8 = t7;
        uint8_t t9 = GM_LD(uint8_t, (((uint8_t *)&t8) + 0));
        if (t9 != ((uint8_t)17ULL)) goto ie2;
        uint32_t t10 = GM_LD(uint32_t, ((((uint8_t *)&t8) + 8) + 0));
        gm_l2 = t10;
        {
            uint32_t t12 = gm_l2;
            uint64_t t13 = (uint64_t)t12;
            uint64_t t14 = (uintptr_t)t13;
            void * t15 = gm_l0;
            void * t16 = GM_LD(void *, (((uint8_t *)t15) + 0));
            uint64_t t17 = GM_LD(uintptr_t, ((((uint8_t *)t16) + 1088) + 0) + 8);
            if (t14 >= (uintptr_t)(t17)) gm_trap_index(t14, t17, gm_srckfibkpt681aa, 130, 13);
            void * t18 = (void *)((uint8_t *)GM_LD(void *, ((((uint8_t *)t16) + 1088) + 0)) + t14 * 184);
            uint8_t t19 = 0;
            F.ret = t18;
            goto gm_fret;
        }
        goto ix2;
ie2:;
        {
            gm_trap("reached `@unreachable()`", gm_srckfibkpt681aa, 129, 42);
        }
ix2:;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) uint64_t gm_f_align_uu2harl(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_assert_message_6bd9l6u(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_assert_message_6bd9l6u F;
    gm_b16_8 gm_l0; /* text */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    uint8_t gm_l4; /* c */
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
        gm_tftb1odc739t5 t3 = gm_f_push_t83rg2m(t2, ((uint8_t)34ULL));
        gm_tftb1odc739t5 t4 = t3;
        if (((((uint8_t *)&t4))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t4) + 1, gm_srckfibkpt681aa, 734, 5);
        gm_b0_1 t5 = GM_LD(gm_b0_1, (((uint8_t *)&t4) + 1));
    }
    {
        {
            gm_b16_8 t7 = gm_l0;
            gm_l2 = t7;
        }
        {
            gm_l3 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t9 = gm_l3;
                uint64_t t10 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                uint8_t t11 = (t9 < t10);
                if (!t11) goto lb8;
                {
                    uint64_t t14 = gm_l3;
                    uint64_t t15 = (uintptr_t)t14;
                    uint64_t t16 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                    uint8_t t17 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t15 * 1));
                    uint8_t t18 = t17;
                    uint8_t t21 = GM_LD(uint8_t, ((uint8_t *)&t18));
                    gm_l4 = t21;
                    {
                        uint8_t t23 = gm_l4;
                        uint8_t t24 = t23;
                        uint8_t t29 = GM_LD(uint8_t, ((uint8_t *)&t24));
                        if (!(t29 == ((uint8_t)34ULL))) goto pa28;
                        goto po27;
pa28:;
                        uint8_t t31 = GM_LD(uint8_t, ((uint8_t *)&t24));
                        if (!(t31 == ((uint8_t)92ULL))) goto pa30;
                        goto po27;
pa30:;
                        goto mn26;
po27:;
                        {
                            void * t33 = (void *)((uint8_t *)&F.l1);
                            gm_tftb1odc739t5 t34 = gm_f_push_t83rg2m(t33, ((uint8_t)92ULL));
                            gm_tftb1odc739t5 t35 = t34;
                            if (((((uint8_t *)&t35))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t35) + 1, gm_srckfibkpt681aa, 738, 17);
                            gm_b0_1 t36 = GM_LD(gm_b0_1, (((uint8_t *)&t35) + 1));
                        }
                        {
                            void * t37 = (void *)((uint8_t *)&F.l1);
                            uint8_t t38 = gm_l4;
                            gm_tftb1odc739t5 t39 = gm_f_push_t83rg2m(t37, t38);
                            gm_tftb1odc739t5 t40 = t39;
                            if (((((uint8_t *)&t40))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t40) + 1, gm_srckfibkpt681aa, 739, 17);
                            gm_b0_1 t41 = GM_LD(gm_b0_1, (((uint8_t *)&t40) + 1));
                        }
                        goto me25;
mn26:;
                        uint8_t t43 = GM_LD(uint8_t, ((uint8_t *)&t24));
                        if ((t43 < ((uint8_t)32ULL)) || (t43 > ((uint8_t)126ULL))) goto mn42;
                        void * t44 = (void *)((uint8_t *)&F.l1);
                        uint8_t t45 = gm_l4;
                        gm_tftb1odc739t5 t46 = gm_f_push_t83rg2m(t44, t45);
                        gm_tftb1odc739t5 t47 = t46;
                        if (((((uint8_t *)&t47))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t47) + 1, gm_srckfibkpt681aa, 741, 26);
                        gm_b0_1 t48 = GM_LD(gm_b0_1, (((uint8_t *)&t47) + 1));
                        goto me25;
mn42:;
                        goto me25;
mn49:;
                        gm_trap("no match arm matched the scrutinee", gm_srckfibkpt681aa, 736, 9);
me25:;
                    }
                    goto me19;
mn20:;
                    gm_trap("no match arm matched the scrutinee", gm_srckfibkpt681aa, 735, 5);
me19:;
                }
lc8:;
                {
                    uint64_t t52 = gm_l3;
                    uint64_t r53; bool o54 = __builtin_add_overflow(t52, ((uint64_t)1ULL), &r53);
                    uint64_t t55 = r53;
                    gm_l3 = t55;
                }
            }
lb8:;
        }
    }
    {
        void * t56 = (void *)((uint8_t *)&F.l1);
        gm_tftb1odc739t5 t57 = gm_f_push_t83rg2m(t56, ((uint8_t)34ULL));
        gm_tftb1odc739t5 t58 = t57;
        if (((((uint8_t *)&t58))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t58) + 1, gm_srckfibkpt681aa, 745, 5);
        gm_b0_1 t59 = GM_LD(gm_b0_1, (((uint8_t *)&t58) + 1));
    }
    {
        gm_b16_8 t60 = GM_LD(gm_b16_8, (((uint8_t *)&F.l1) + 0));
        gm_b16_8 t61 = t60;
        gm_b16_8 t62 = GM_LD(gm_b16_8, ((uint8_t *)&t61));
        uint8_t t63 = 0;
        F.ret = t62;
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
extern __attribute__((aligned(4))) uint32_t gm_f_bit_width_u86m8ir(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
extern __attribute__((aligned(4))) void gm_f_bits_layout_23rect9(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) uint8_t gm_f_blob_before_9p1c704(void * a0, void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_blob_before_9p1c704 F;
    void * gm_l0; /* x */
    void * gm_l1; /* y */
    uint8_t gm_l2; /* rx */
    uint8_t gm_l3; /* ry */
    gm_b16_8 gm_l4; /* a */
    gm_b16_8 gm_l5; /* b */
    uint64_t gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    uint64_t gm_l8; /* i */
    GM_STACK_CHECK_SHALLOW(&F, 462u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t3 = gm_l0;
        uint64_t t4 = GM_LD(uint64_t, (((uint8_t *)t3) + 0));
        void * t5 = gm_l1;
        uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)t5) + 0));
        uint8_t t7 = (!(t4 == t6));
        if (!t7) goto ie2;
        {
            void * t9 = gm_l0;
            uint64_t t10 = GM_LD(uint64_t, (((uint8_t *)t9) + 0));
            void * t11 = gm_l1;
            uint64_t t12 = GM_LD(uint64_t, (((uint8_t *)t11) + 0));
            uint8_t t13 = (t10 < t12);
            uint8_t t14 = 0;
            F.ret = t13;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t16 = gm_l0;
        uint64_t t17 = GM_LD(uint64_t, (((uint8_t *)t16) + 8));
        void * t18 = gm_l1;
        uint64_t t19 = GM_LD(uint64_t, (((uint8_t *)t18) + 8));
        uint8_t t20 = (!(t17 == t19));
        if (!t20) goto ie15;
        {
            void * t22 = gm_l0;
            uint64_t t23 = GM_LD(uint64_t, (((uint8_t *)t22) + 8));
            void * t24 = gm_l1;
            uint64_t t25 = GM_LD(uint64_t, (((uint8_t *)t24) + 8));
            uint8_t t26 = (t23 < t25);
            uint8_t t27 = 0;
            F.ret = t26;
            goto gm_fret;
        }
        goto ix15;
ie15:;
ix15:;
    }
    {
        void * t28 = gm_l0;
        uint8_t t29 = gm_f_kind_rank_g1kdlst(&(*(gm_tl572d7579hns *)(((uint8_t *)t28) + 16)));
        gm_l2 = t29;
    }
    {
        void * t30 = gm_l1;
        uint8_t t31 = gm_f_kind_rank_g1kdlst(&(*(gm_tl572d7579hns *)(((uint8_t *)t30) + 16)));
        gm_l3 = t31;
    }
    {
        uint8_t t33 = gm_l2;
        uint8_t t34 = gm_l3;
        uint8_t t35 = (!(t33 == t34));
        if (!t35) goto ie32;
        {
            uint8_t t37 = gm_l2;
            uint8_t t38 = gm_l3;
            uint8_t t39 = (t37 < t38);
            uint8_t t40 = 0;
            F.ret = t39;
            goto gm_fret;
        }
        goto ix32;
ie32:;
ix32:;
    }
    {
        void * t42 = gm_l0;
        uint8_t t43 = GM_LD(uint8_t, ((((uint8_t *)t42) + 16) + 0));
        if (t43 != ((uint8_t)3ULL)) goto ie41;
        gm_b16_8 t44 = GM_LD(gm_b16_8, ((((uint8_t *)t42) + 16) + 8));
        gm_l4 = t44;
        void * t45 = gm_l1;
        uint8_t t46 = GM_LD(uint8_t, ((((uint8_t *)t45) + 16) + 0));
        if (t46 != ((uint8_t)3ULL)) goto ie41;
        gm_b16_8 t47 = GM_LD(gm_b16_8, ((((uint8_t *)t45) + 16) + 8));
        gm_l5 = t47;
        {
            {
                gm_l7 = ((uint64_t)0ULL);
            }
            {
                uint64_t t50 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                uint64_t t51 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                uint64_t t52 = (t50 < t51 ? t50 : t51);
                gm_l6 = t52;
            }
            {
                for (;;) {
                    uint64_t t54 = gm_l7;
                    uint64_t t55 = gm_l6;
                    uint8_t t56 = (t54 < t55);
                    if (!t56) goto lb53;
                    {
                        uint64_t t59 = gm_l7;
                        uint64_t t60 = t59;
                        uint64_t t63 = GM_LD(uint64_t, ((uint8_t *)&t60));
                        gm_l8 = t63;
                        {
                            uint64_t t66 = gm_l8;
                            uint64_t t67 = (uintptr_t)t66;
                            uint64_t t68 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                            if (t67 >= (uintptr_t)(t68)) gm_trap_index(t67, t68, gm_srckfibkpt681aa, 684, 16);
                            uint8_t t69 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t67 * 1));
                            uint64_t t70 = gm_l8;
                            uint64_t t71 = (uintptr_t)t70;
                            uint64_t t72 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                            if (t71 >= (uintptr_t)(t72)) gm_trap_index(t71, t72, gm_srckfibkpt681aa, 684, 28);
                            uint8_t t73 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l5)) + t71 * 1));
                            uint8_t t74 = (!(t69 == t73));
                            if (!t74) goto ie65;
                            {
                                uint64_t t76 = gm_l8;
                                uint64_t t77 = (uintptr_t)t76;
                                uint64_t t78 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                                if (t77 >= (uintptr_t)(t78)) gm_trap_index(t77, t78, gm_srckfibkpt681aa, 684, 42);
                                uint8_t t79 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l5)) + t77 * 1));
                                uint8_t t80 = 0;
                                F.ret = t79;
                                goto gm_fret;
                            }
                            goto ix65;
ie65:;
ix65:;
                        }
                        goto me61;
mn62:;
                        gm_trap("no match arm matched the scrutinee", gm_srckfibkpt681aa, 683, 9);
me61:;
                    }
lc53:;
                    {
                        uint64_t t82 = gm_l7;
                        uint64_t r83; bool o84 = __builtin_add_overflow(t82, ((uint64_t)1ULL), &r83);
                        uint64_t t85 = r83;
                        gm_l7 = t85;
                    }
                }
lb53:;
            }
        }
        {
            uint64_t t86 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
            uint64_t t87 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
            uint8_t t88 = (t86 < t87);
            uint8_t t89 = 0;
            F.ret = t88;
            goto gm_fret;
        }
        goto ix41;
ie41:;
ix41:;
    }
    {
        uint8_t t90 = 0;
        F.ret = ((uint8_t)0);
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_blob_kind_kp13j9k(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, uint64_t a2, uint64_t a3) {
    struct gm_fr_blob_kind_kp13j9k F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    uint64_t gm_l2; /* s */
    uint64_t gm_l3; /* a */
    uint64_t gm_l4; /* pb */
    gm_b16_8 gm_l5; /* lanes */
    GM_STACK_CHECK(&F, 394u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        void * t2 = gm_l0;
        uint64_t t3 = GM_LD(uint64_t, (((((uint8_t *)t2) + 8) + 0) + 0));
        gm_l4 = t3;
    }
    {
        uint8_t t5 = 0;
        uint8_t t6 = 0;
        uint8_t t7 = 0;
        void * t8 = gm_l0;
        uint8_t t9 = GM_LD(uint8_t, (((((uint8_t *)t8) + 8) + 0) + 56));
        uint8_t t10 = (uint8_t)!t9;
        if (!t10) {
            uint64_t t11 = gm_l3;
            uint64_t t12 = gm_l4;
            uint8_t t13 = (t11 < t12);
            t7 = t13;
        } else t7 = 1;
        if (!t7) {
            uint64_t t14 = gm_l2;
            uint64_t t15 = gm_l4;
            uint8_t t16 = (t14 < t15);
            t6 = t16;
        } else t6 = 1;
        if (!t6) {
            uint64_t t17 = gm_l2;
            uint64_t t18 = gm_l4;
            if (t18 == 0) gm_trap("division by zero", gm_srckfibkpt681aa, 701, 54);
            uint64_t t19 = (uint64_t)(t17 % t18);
            uint8_t t20 = (!(t19 == ((uint64_t)0ULL)));
            t5 = t20;
        } else t5 = 1;
        if (!t5) goto ie4;
        {
            gm_tl572d7579hns t22;
            GM_ST(uint8_t, (uint8_t *)&t22 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t22 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t22 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t22 + 8, 0);
            GM_ST(uint64_t, (uint8_t *)&t22 + 16, 0);
            GM_ST(uint8_t, ((uint8_t *)&t22 + 0), ((uint8_t)0ULL));
            uint8_t t23 = 0;
            F.ret = t22;
            goto gm_fret;
        }
        goto ix4;
ie4:;
ix4:;
    }
    {
        void * t25 = gm_l0;
        void * t26 = (void *)(((uint8_t *)t25) + 8);
        gm_t88rcucg1b7uk t27 = gm_l1;
        uint8_t t28 = gm_f_may_hold_pointers_iuorcah((void *)NULL, cx, NULL, t26, t27);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t29 = (uint8_t)!t28;
        if (!t29) goto ie24;
        {
            gm_tl572d7579hns t31;
            GM_ST(uint8_t, (uint8_t *)&t31 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t31 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t31 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t31 + 8, 0);
            GM_ST(uint64_t, (uint8_t *)&t31 + 16, 0);
            GM_ST(uint8_t, ((uint8_t *)&t31 + 0), ((uint8_t)2ULL));
            uint8_t t32 = 0;
            F.ret = t31;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t34 = gm_l0;
        void * t35 = (void *)(((uint8_t *)t34) + 8);
        gm_t88rcucg1b7uk t36 = gm_l1;
        gm_th2lqbo77pvsc t37;
        gm_f_pointer_words_ljph5no((void *)&t37, (void *)NULL, cx, NULL, t35, t36);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_th2lqbo77pvsc t38 = t37;
        if (!((((uint8_t *)&t38))[16] != 0)) goto ie33;
        gm_b16_8 t39 = GM_LD(gm_b16_8, ((uint8_t *)&t38));
        gm_l5 = t39;
        {
            gm_b16_8 t41 = gm_l5;
            gm_b16_8 t42 = t41;
            gm_b16_8 t43 = GM_LD(gm_b16_8, ((uint8_t *)&t42));
            gm_tl572d7579hns t44;
            GM_ST(uint8_t, (uint8_t *)&t44 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t44 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t44 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t44 + 0), ((uint8_t)3ULL));
            GM_ST(gm_b16_8, ((uint8_t *)&t44 + 8), t43);
            uint8_t t45 = 0;
            F.ret = t44;
            goto gm_fret;
        }
        goto ix33;
ie33:;
ix33:;
    }
    {
        gm_tl572d7579hns t46;
        GM_ST(uint8_t, (uint8_t *)&t46 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t46 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t46 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t46 + 8, 0);
        GM_ST(uint64_t, (uint8_t *)&t46 + 16, 0);
        GM_ST(uint8_t, ((uint8_t *)&t46 + 0), ((uint8_t)1ULL));
        uint8_t t47 = 0;
        F.ret = t46;
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
__attribute__((aligned(4))) gm_b16_8 gm_f_blob_name_sjpa1ub(void *env, gm_node *cx, gm_node *inst, uint64_t a0, uint64_t a1, const void * a2) {
    struct gm_fr_blob_name_sjpa1ub F;
    uint64_t gm_l0; /* size */
    uint64_t gm_l1; /* align */
    gm_tl572d7579hns gm_l2; /* kind */
    gm_b16_8 gm_l3; /* ptrs */
    gm_b16_8 gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    uint8_t gm_l7; /* p */
    GM_STACK_CHECK(&F, 672u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_b16_8 t1;
    uint8_t t7 = GM_LD(uint8_t, (((uint8_t *)a2) + 0));
    switch (t7) { case ((uint8_t)0ULL): goto ma5; case ((uint8_t)1ULL): goto ma5; case ((uint8_t)2ULL): goto ma3; case ((uint8_t)3ULL): goto ma4; default: goto ma5; }
ma3:;
    uint8_t t9 = GM_LD(uint8_t, (((uint8_t *)a2) + 0));
    if (t9 != ((uint8_t)2ULL)) goto mn8;
    uint64_t t10 = gm_l0;
    uint64_t t11 = gm_l1;
    gm_t2lqu8jsj511d t12;
    GM_ST(uint64_t, ((uint8_t *)&t12 + 0), t10);
    GM_ST(uint64_t, ((uint8_t *)&t12 + 8), t11);
    gm_tabb710d9mq2e t13;
    gm_f_format_ef6t3dr((void *)&t13, (void *)NULL, cx, NULL, t12);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tabb710d9mq2e t14 = t13;
    if (((((uint8_t *)&t14))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t14) + 8, gm_srckfibkpt681aa, 638, 15);
    gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&t14) + 8));
    gm_b16_8 t16 = t15;
    gm_b16_8 t17 = GM_LD(gm_b16_8, ((uint8_t *)&t16));
    t1 = t17;
    goto me2;
mn8:;
ma4:;
    uint8_t t19 = GM_LD(uint8_t, (((uint8_t *)a2) + 0));
    if (t19 != ((uint8_t)3ULL)) goto mn18;
    gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)a2) + 8));
    gm_l3 = t20;
    {
        gm_f_list_uvfu1k1((void *)&F.l4, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t23 = gm_l3;
            gm_l5 = t23;
        }
        {
            gm_l6 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t25 = gm_l6;
                uint64_t t26 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                uint8_t t27 = (t25 < t26);
                if (!t27) goto lb24;
                {
                    uint64_t t30 = gm_l6;
                    uint64_t t31 = (uintptr_t)t30;
                    uint64_t t32 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                    uint8_t t33 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l5)) + t31 * 1));
                    uint8_t t34 = t33;
                    uint8_t t37 = GM_LD(uint8_t, ((uint8_t *)&t34));
                    gm_l7 = t37;
                    {
                        void * t39 = (void *)((uint8_t *)&F.l4);
                        uint8_t t40;
                        uint8_t t42 = gm_l7;
                        if (!t42) goto ie41;
                        uint8_t t44 = ((uint8_t)112ULL);
                        t40 = t44;
                        goto ix41;
ie41:;
                        uint8_t t46 = ((uint8_t)119ULL);
                        t40 = t46;
ix41:;
                        gm_tftb1odc739t5 t47 = gm_f_push_t83rg2m(t39, t40);
                        gm_tftb1odc739t5 t48 = t47;
                        if (((((uint8_t *)&t48))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t48) + 1, gm_srckfibkpt681aa, 641, 25);
                        gm_b0_1 t49 = GM_LD(gm_b0_1, (((uint8_t *)&t48) + 1));
                    }
                    goto me35;
mn36:;
                    gm_trap("no match arm matched the scrutinee", gm_srckfibkpt681aa, 641, 9);
me35:;
                }
lc24:;
                {
                    uint64_t t51 = gm_l6;
                    uint64_t r52; bool o53 = __builtin_add_overflow(t51, ((uint64_t)1ULL), &r52);
                    uint64_t t54 = r52;
                    gm_l6 = t54;
                }
            }
lb24:;
        }
    }
    gm_tbccvikg6b96e t55;
    uint64_t t56 = gm_l0;
    GM_ST(uint64_t, (uint8_t *)&t55 + 0, t56);
    uint64_t t57 = gm_l1;
    GM_ST(uint64_t, (uint8_t *)&t55 + 8, t57);
    gm_b16_8 t58 = GM_LD(gm_b16_8, (((uint8_t *)&F.l4) + 0));
    GM_ST(gm_b16_8, (uint8_t *)&t55 + 16, t58);
    gm_tabb710d9mq2e t59;
    gm_f_format_udu99n6((void *)&t59, (void *)NULL, cx, NULL, &t55);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tabb710d9mq2e t60 = t59;
    if (((((uint8_t *)&t60))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t60) + 8, gm_srckfibkpt681aa, 642, 9);
    gm_b16_8 t61 = GM_LD(gm_b16_8, (((uint8_t *)&t60) + 8));
    gm_b16_8 t62 = t61;
    gm_b16_8 t63 = t62;
    gm_b16_8 t64 = GM_LD(gm_b16_8, ((uint8_t *)&t63));
    t1 = t64;
    goto me2;
mn18:;
ma5:;
    uint64_t t66 = gm_l0;
    uint64_t t67 = gm_l1;
    gm_t2lqu8jsj511d t68;
    GM_ST(uint64_t, ((uint8_t *)&t68 + 0), t66);
    GM_ST(uint64_t, ((uint8_t *)&t68 + 8), t67);
    gm_tabb710d9mq2e t69;
    gm_f_format_f6fn313((void *)&t69, (void *)NULL, cx, NULL, t68);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tabb710d9mq2e t70 = t69;
    if (((((uint8_t *)&t70))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t70) + 8, gm_srckfibkpt681aa, 644, 10);
    gm_b16_8 t71 = GM_LD(gm_b16_8, (((uint8_t *)&t70) + 8));
    gm_b16_8 t72 = t71;
    gm_b16_8 t73 = GM_LD(gm_b16_8, ((uint8_t *)&t72));
    t1 = t73;
    goto me2;
mn65:;
ma6:;
    gm_trap("no match arm matched the scrutinee", gm_srckfibkpt681aa, 637, 81);
me2:;
    F.ret = t1;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_b16_8 gm_f_blob_typedef_8ta3rbi(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, uint64_t a2, const void * a3, gm_b16_8 a4) {
    struct gm_fr_blob_typedef_8ta3rbi F;
    void * gm_l0; /* ty */
    uint64_t gm_l1; /* s */
    uint64_t gm_l2; /* a */
    gm_tl572d7579hns gm_l3; /* kind */
    gm_b16_8 gm_l4; /* name */
    uint64_t gm_l5; /* pb */
    gm_b16_8 gm_l6; /* ptrs */
    gm_b16_8 gm_l8; /* <tmp> */
    uint64_t gm_l9; /* <tmp> */
    uint64_t gm_l10; /* i */
    uint8_t gm_l11; /* p */
    GM_STACK_CHECK(&F, 1188u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l4 = a4;
    {
        void * t2 = gm_l0;
        uint64_t t3 = GM_LD(uint64_t, (((((uint8_t *)t2) + 8) + 0) + 0));
        gm_l5 = t3;
    }
    {
        uint8_t t10 = GM_LD(uint8_t, (((uint8_t *)a3) + 0));
        switch (t10) { case ((uint8_t)0ULL): goto ma5; case ((uint8_t)1ULL): goto ma6; case ((uint8_t)2ULL): goto ma7; case ((uint8_t)3ULL): goto ma8; default: goto ma9; }
ma5:;
        uint8_t t12 = GM_LD(uint8_t, (((uint8_t *)a3) + 0));
        if (t12 != ((uint8_t)0ULL)) goto mn11;
        gm_tbccvikg6b96e t13;
        uint64_t t14 = gm_l2;
        GM_ST(uint64_t, (uint8_t *)&t13 + 0, t14);
        uint64_t t15 = gm_l1;
        GM_ST(uint64_t, (uint8_t *)&t13 + 8, t15);
        gm_b16_8 t16 = gm_l4;
        GM_ST(gm_b16_8, (uint8_t *)&t13 + 16, t16);
        gm_tabb710d9mq2e t17;
        gm_f_format_a4r7k06((void *)&t17, (void *)NULL, cx, NULL, &t13);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t18 = t17;
        if (((((uint8_t *)&t18))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t18) + 8, gm_srckfibkpt681aa, 714, 26);
        gm_b16_8 t19 = GM_LD(gm_b16_8, (((uint8_t *)&t18) + 8));
        gm_b16_8 t20 = t19;
        gm_b16_8 t21 = GM_LD(gm_b16_8, ((uint8_t *)&t20));
        uint8_t t22 = 0;
        F.ret = t21;
        goto gm_fret;
        goto me4;
mn11:;
ma6:;
        uint8_t t24 = GM_LD(uint8_t, (((uint8_t *)a3) + 0));
        if (t24 != ((uint8_t)1ULL)) goto mn23;
        gm_tbfdi36rjh0el t25;
        uint64_t t26 = gm_l2;
        GM_ST(uint64_t, (uint8_t *)&t25 + 0, t26);
        uint64_t t27 = gm_l1;
        GM_ST(uint64_t, (uint8_t *)&t25 + 8, t27);
        uint64_t t28 = gm_l1;
        uint64_t t29 = gm_l5;
        if (t29 == 0) gm_trap("division by zero", gm_srckfibkpt681aa, 715, 123);
        uint64_t t30 = (uint64_t)(t28 / t29);
        GM_ST(uint64_t, (uint8_t *)&t25 + 16, t30);
        gm_b16_8 t31 = gm_l4;
        GM_ST(gm_b16_8, (uint8_t *)&t25 + 24, t31);
        gm_tabb710d9mq2e t32;
        gm_f_format_c19sabv((void *)&t32, (void *)NULL, cx, NULL, &t25);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t33 = t32;
        if (((((uint8_t *)&t33))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t33) + 8, gm_srckfibkpt681aa, 715, 29);
        gm_b16_8 t34 = GM_LD(gm_b16_8, (((uint8_t *)&t33) + 8));
        gm_b16_8 t35 = t34;
        gm_b16_8 t36 = GM_LD(gm_b16_8, ((uint8_t *)&t35));
        uint8_t t37 = 0;
        F.ret = t36;
        goto gm_fret;
        goto me4;
mn23:;
ma7:;
        uint8_t t39 = GM_LD(uint8_t, (((uint8_t *)a3) + 0));
        if (t39 != ((uint8_t)2ULL)) goto mn38;
        gm_tbccvikg6b96e t40;
        uint64_t t41 = gm_l2;
        GM_ST(uint64_t, (uint8_t *)&t40 + 0, t41);
        uint64_t t42 = gm_l1;
        uint64_t t43 = gm_l5;
        if (t43 == 0) gm_trap("division by zero", gm_srckfibkpt681aa, 716, 103);
        uint64_t t44 = (uint64_t)(t42 / t43);
        GM_ST(uint64_t, (uint8_t *)&t40 + 8, t44);
        gm_b16_8 t45 = gm_l4;
        GM_ST(gm_b16_8, (uint8_t *)&t40 + 16, t45);
        gm_tabb710d9mq2e t46;
        gm_f_format_s9gvae7((void *)&t46, (void *)NULL, cx, NULL, &t40);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t47 = t46;
        if (((((uint8_t *)&t47))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t47) + 8, gm_srckfibkpt681aa, 716, 26);
        gm_b16_8 t48 = GM_LD(gm_b16_8, (((uint8_t *)&t47) + 8));
        gm_b16_8 t49 = t48;
        gm_b16_8 t50 = GM_LD(gm_b16_8, ((uint8_t *)&t49));
        uint8_t t51 = 0;
        F.ret = t50;
        goto gm_fret;
        goto me4;
mn38:;
ma8:;
        uint8_t t53 = GM_LD(uint8_t, (((uint8_t *)a3) + 0));
        if (t53 != ((uint8_t)3ULL)) goto mn52;
        gm_b16_8 t54 = GM_LD(gm_b16_8, (((uint8_t *)a3) + 8));
        gm_l6 = t54;
        {
            gm_f_list_uvfu1k1((void *)&F.l7, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            {
                gm_b16_8 t57 = gm_l6;
                gm_l8 = t57;
            }
            {
                gm_l9 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t59 = gm_l9;
                    uint64_t t60 = GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8);
                    uint8_t t61 = (t59 < t60);
                    if (!t61) goto lb58;
                    {
                        uint64_t t64 = gm_l9;
                        uint64_t t65 = gm_l9;
                        uint64_t t66 = (uintptr_t)t65;
                        uint64_t t67 = GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8);
                        uint8_t t68 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l8)) + t66 * 1));
                        gm_tf5srh4mp4b3i t69;
                        GM_ST(uint8_t, (uint8_t *)&t69 + 9, 0);
                        GM_ST(uint16_t, (uint8_t *)&t69 + 10, 0);
                        GM_ST(uint32_t, (uint8_t *)&t69 + 12, 0);
                        GM_ST(uint64_t, ((uint8_t *)&t69 + 0), t64);
                        GM_ST(uint8_t, ((uint8_t *)&t69 + 8), t68);
                        gm_tf5srh4mp4b3i t70 = t69;
                        uint64_t t73 = GM_LD(uint64_t, (((uint8_t *)&t70) + 0));
                        gm_l10 = t73;
                        uint8_t t74 = GM_LD(uint8_t, (((uint8_t *)&t70) + 8));
                        gm_l11 = t74;
                        {
                            uint64_t t77 = gm_l10;
                            uint8_t t78 = (t77 > ((uint64_t)0ULL));
                            if (!t78) goto ie76;
                            {
                                void * t80 = (void *)((uint8_t *)&F.l7);
                                gm_tftb1odc739t5 t81 = gm_f_push_t83rg2m(t80, ((uint8_t)32ULL));
                                gm_tftb1odc739t5 t82 = t81;
                                if (((((uint8_t *)&t82))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t82) + 1, gm_srckfibkpt681aa, 720, 28);
                                gm_b0_1 t83 = GM_LD(gm_b0_1, (((uint8_t *)&t82) + 1));
                            }
                            goto ix76;
ie76:;
ix76:;
                        }
                        {
                            uint64_t t85 = gm_l10;
                            uint8_t t86 = ((t85 == ((uint64_t)0ULL)));
                            if (!t86) goto ie84;
                            {
                                void * t88 = (void *)((uint8_t *)&F.l7);
                                uint64_t t89 = gm_l2;
                                gm_t4ce0jd3tdro4 t90;
                                GM_ST(uint64_t, ((uint8_t *)&t90 + 0), t89);
                                gm_tftb1odc739t5 t91 = gm_f_write_2h6ucjl(t88, t90);
                                gm_tftb1odc739t5 t92 = t91;
                                if (((((uint8_t *)&t92))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t92) + 1, gm_srckfibkpt681aa, 721, 29);
                                gm_b0_1 t93 = GM_LD(gm_b0_1, (((uint8_t *)&t92) + 1));
                            }
                            goto ix84;
ie84:;
ix84:;
                        }
                        {
                            void * t94 = (void *)((uint8_t *)&F.l7);
                            gm_t3877pk7lv748 t95;
                            gm_b16_8 t96;
                            uint8_t t98 = gm_l11;
                            if (!t98) goto ie97;
                            gm_b16_8 t100;
                            GM_ST(void *, (uint8_t *)&t100, (void *)(gm_s44ek1gecbajs));
                            GM_ST(uintptr_t, (uint8_t *)&t100 + 8, (uintptr_t)(6));
                            gm_b16_8 t101 = t100;
                            t96 = t101;
                            goto ix97;
ie97:;
                            gm_b16_8 t103;
                            GM_ST(void *, (uint8_t *)&t103, (void *)(gm_siblunnp4nahm));
                            GM_ST(uintptr_t, (uint8_t *)&t103 + 8, (uintptr_t)(9));
                            gm_b16_8 t104 = t103;
                            t96 = t104;
ix97:;
                            GM_ST(gm_b16_8, (uint8_t *)&t95 + 0, t96);
                            uint64_t t105 = gm_l10;
                            GM_ST(uint64_t, (uint8_t *)&t95 + 16, t105);
                            gm_tftb1odc739t5 t106 = gm_f_write_2hik1mg(t94, &t95);
                            gm_tftb1odc739t5 t107 = t106;
                            if (((((uint8_t *)&t107))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t107) + 1, gm_srckfibkpt681aa, 722, 17);
                            gm_b0_1 t108 = GM_LD(gm_b0_1, (((uint8_t *)&t107) + 1));
                        }
                        goto me71;
mn72:;
                        gm_trap("no match arm matched the scrutinee", gm_srckfibkpt681aa, 719, 13);
me71:;
                    }
lc58:;
                    {
                        uint64_t t110 = gm_l9;
                        uint64_t r111; bool o112 = __builtin_add_overflow(t110, ((uint64_t)1ULL), &r111);
                        uint64_t t113 = r111;
                        gm_l9 = t113;
                    }
                }
lb58:;
            }
        }
        {
            gm_t6j6smj7qoctu t114;
            gm_b16_8 t115 = GM_LD(gm_b16_8, (((uint8_t *)&F.l7) + 0));
            GM_ST(gm_b16_8, (uint8_t *)&t114 + 0, t115);
            gm_b16_8 t116 = gm_l4;
            GM_ST(gm_b16_8, (uint8_t *)&t114 + 16, t116);
            gm_tabb710d9mq2e t117;
            gm_f_format_tjkla39((void *)&t117, (void *)NULL, cx, NULL, &t114);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t118 = t117;
            if (((((uint8_t *)&t118))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t118) + 8, gm_srckfibkpt681aa, 724, 20);
            gm_b16_8 t119 = GM_LD(gm_b16_8, (((uint8_t *)&t118) + 8));
            gm_b16_8 t120 = t119;
            gm_b16_8 t121 = GM_LD(gm_b16_8, ((uint8_t *)&t120));
            uint8_t t122 = 0;
            F.ret = t121;
            goto gm_fret;
        }
        goto me4;
mn52:;
ma9:;
        gm_trap("no match arm matched the scrutinee", gm_srckfibkpt681aa, 713, 5);
me4:;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_blob_typedefs_4nfn4l8(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_blob_typedefs_4nfn4l8 F;
    void * gm_l0; /* ty */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    GM_STACK_CHECK(&F, 446u);
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
        gm_f_list_nvrrj0t((void *)&F.l2, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l2);
        void * t3 = gm_l0;
        gm_b16_8 t4 = GM_LD(gm_b16_8, ((((uint8_t *)t3) + 80) + 0));
        gm_b16_8 t5 = t4;
        gm_b16_8 t6 = GM_LD(gm_b16_8, ((uint8_t *)&t5));
        gm_tftb1odc739t5 t7 = gm_f_extend_glhj3bs(t2, t6);
        gm_tftb1odc739t5 t8 = t7;
        if (((((uint8_t *)&t8))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t8) + 1, gm_srckfibkpt681aa, 469, 5);
        gm_b0_1 t9 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
    }
    {
        gm_b16_8 t10 = GM_LD(gm_b16_8, (((uint8_t *)&F.l2) + 0));
        gm_closure t11 = ((gm_closure){GM_PTR_TAG(((void *)gm_f_blob_before_9p1c704)), NULL, cx});
        gm_f_sort_by_55adsi9(t10, t11);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t13 = GM_LD(gm_b16_8, (((uint8_t *)&F.l2) + 0));
            gm_l3 = t13;
        }
        {
            gm_l4 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t15 = gm_l4;
                uint64_t t16 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                uint8_t t17 = (t15 < t16);
                if (!t17) goto lb14;
                {
                    uint64_t t20 = gm_l4;
                    uint64_t t21 = (uintptr_t)t20;
                    uint64_t t22 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                    gm_tulpvg6gm4fqa t25 = GM_LD(gm_tulpvg6gm4fqa, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t21 * 56));
                    F.l5 = t25;
                    {
                        void * t27 = (void *)((uint8_t *)&F.l1);
                        gm_b16_8 t28 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 40));
                        gm_tftb1odc739t5 t29 = gm_f_extend_0cs85gs(t27, t28);
                        gm_tftb1odc739t5 t30 = t29;
                        if (((((uint8_t *)&t30))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t30) + 1, gm_srckfibkpt681aa, 471, 29);
                        gm_b0_1 t31 = GM_LD(gm_b0_1, (((uint8_t *)&t30) + 1));
                    }
                    goto me23;
mn24:;
                    gm_trap("no match arm matched the scrutinee", gm_srckfibkpt681aa, 471, 5);
me23:;
                }
lc14:;
                {
                    uint64_t t33 = gm_l4;
                    uint64_t r34; bool o35 = __builtin_add_overflow(t33, ((uint64_t)1ULL), &r34);
                    uint64_t t36 = r34;
                    gm_l4 = t36;
                }
            }
lb14:;
        }
    }
    {
        void * t37 = (void *)((uint8_t *)&F.l1);
        void * t38 = gm_l0;
        gm_b16_8 t39 = GM_LD(gm_b16_8, ((((uint8_t *)t38) + 560) + 0));
        gm_b16_8 t40 = t39;
        gm_b16_8 t41 = GM_LD(gm_b16_8, ((uint8_t *)&t40));
        gm_tftb1odc739t5 t42 = gm_f_extend_0cs85gs(t37, t41);
        gm_tftb1odc739t5 t43 = t42;
        if (((((uint8_t *)&t43))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t43) + 1, gm_srckfibkpt681aa, 472, 5);
        gm_b0_1 t44 = GM_LD(gm_b0_1, (((uint8_t *)&t43) + 1));
    }
    {
        gm_b16_8 t45 = GM_LD(gm_b16_8, (((uint8_t *)&F.l1) + 0));
        gm_b16_8 t46 = t45;
        gm_b16_8 t47 = GM_LD(gm_b16_8, ((uint8_t *)&t46));
        uint8_t t48 = 0;
        F.ret = t47;
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
extern __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_bool_ty_kotvo8t(void *env, gm_node *cx, gm_node *inst, void * a0);
extern __attribute__((aligned(4))) uint8_t gm_f_by_ref_fi0o860(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_c_string_n7h27b3(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_c_string_n7h27b3 F;
    gm_b16_8 gm_l0; /* bytes */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    uint8_t gm_l4; /* b */
    GM_STACK_CHECK(&F, 312u);
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
        gm_tftb1odc739t5 t3 = gm_f_push_t83rg2m(t2, ((uint8_t)34ULL));
        gm_tftb1odc739t5 t4 = t3;
        if (((((uint8_t *)&t4))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t4) + 1, gm_srckfibkpt681aa, 752, 5);
        gm_b0_1 t5 = GM_LD(gm_b0_1, (((uint8_t *)&t4) + 1));
    }
    {
        {
            gm_b16_8 t7 = gm_l0;
            gm_l2 = t7;
        }
        {
            gm_l3 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t9 = gm_l3;
                uint64_t t10 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                uint8_t t11 = (t9 < t10);
                if (!t11) goto lb8;
                {
                    uint64_t t14 = gm_l3;
                    uint64_t t15 = (uintptr_t)t14;
                    uint64_t t16 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                    uint8_t t17 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t15 * 1));
                    uint8_t t18 = t17;
                    uint8_t t21 = GM_LD(uint8_t, ((uint8_t *)&t18));
                    gm_l4 = t21;
                    {
                        uint8_t t23 = gm_l4;
                        uint8_t t24 = t23;
                        uint8_t t27 = GM_LD(uint8_t, ((uint8_t *)&t24));
                        if (!(t27 == ((uint8_t)34ULL))) goto mn26;
                        void * t28 = (void *)((uint8_t *)&F.l1);
                        gm_b16_8 t29;
                        GM_ST(void *, (uint8_t *)&t29, (void *)(gm_smee1janbl2t9));
                        GM_ST(uintptr_t, (uint8_t *)&t29 + 8, (uintptr_t)(2));
                        gm_tftb1odc739t5 t30 = gm_f_extend_0cs85gs(t28, t29);
                        gm_tftb1odc739t5 t31 = t30;
                        if (((((uint8_t *)&t31))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t31) + 1, gm_srckfibkpt681aa, 755, 20);
                        gm_b0_1 t32 = GM_LD(gm_b0_1, (((uint8_t *)&t31) + 1));
                        goto me25;
mn26:;
                        uint8_t t34 = GM_LD(uint8_t, ((uint8_t *)&t24));
                        if (!(t34 == ((uint8_t)92ULL))) goto mn33;
                        void * t35 = (void *)((uint8_t *)&F.l1);
                        gm_b16_8 t36;
                        GM_ST(void *, (uint8_t *)&t36, (void *)(gm_s3p8gug7bvl1p));
                        GM_ST(uintptr_t, (uint8_t *)&t36 + 8, (uintptr_t)(2));
                        gm_tftb1odc739t5 t37 = gm_f_extend_0cs85gs(t35, t36);
                        gm_tftb1odc739t5 t38 = t37;
                        if (((((uint8_t *)&t38))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t38) + 1, gm_srckfibkpt681aa, 756, 21);
                        gm_b0_1 t39 = GM_LD(gm_b0_1, (((uint8_t *)&t38) + 1));
                        goto me25;
mn33:;
                        uint8_t t41 = GM_LD(uint8_t, ((uint8_t *)&t24));
                        if ((t41 < ((uint8_t)32ULL)) || (t41 > ((uint8_t)126ULL))) goto mn40;
                        uint8_t t42 = gm_l4;
                        uint8_t t43 = (!(t42 == ((uint8_t)63ULL)));
                        if (!t43) goto mn40;
                        void * t44 = (void *)((uint8_t *)&F.l1);
                        uint8_t t45 = gm_l4;
                        gm_tftb1odc739t5 t46 = gm_f_push_t83rg2m(t44, t45);
                        gm_tftb1odc739t5 t47 = t46;
                        if (((((uint8_t *)&t47))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t47) + 1, gm_srckfibkpt681aa, 757, 44);
                        gm_b0_1 t48 = GM_LD(gm_b0_1, (((uint8_t *)&t47) + 1));
                        goto me25;
mn40:;
                        void * t50 = (void *)((uint8_t *)&F.l1);
                        uint8_t t51 = gm_l4;
                        gm_t9qkh0rsooc8d t52;
                        GM_ST(uint8_t, ((uint8_t *)&t52 + 0), t51);
                        gm_tftb1odc739t5 t53 = gm_f_write_fle5ipv(t50, t52);
                        gm_tftb1odc739t5 t54 = t53;
                        if (((((uint8_t *)&t54))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t54) + 1, gm_srckfibkpt681aa, 758, 18);
                        gm_b0_1 t55 = GM_LD(gm_b0_1, (((uint8_t *)&t54) + 1));
                        goto me25;
mn49:;
                        gm_trap("no match arm matched the scrutinee", gm_srckfibkpt681aa, 754, 9);
me25:;
                    }
                    goto me19;
mn20:;
                    gm_trap("no match arm matched the scrutinee", gm_srckfibkpt681aa, 753, 5);
me19:;
                }
lc8:;
                {
                    uint64_t t57 = gm_l3;
                    uint64_t r58; bool o59 = __builtin_add_overflow(t57, ((uint64_t)1ULL), &r58);
                    uint64_t t60 = r58;
                    gm_l3 = t60;
                }
            }
lb8:;
        }
    }
    {
        void * t61 = (void *)((uint8_t *)&F.l1);
        gm_tftb1odc739t5 t62 = gm_f_push_t83rg2m(t61, ((uint8_t)34ULL));
        gm_tftb1odc739t5 t63 = t62;
        if (((((uint8_t *)&t63))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t63) + 1, gm_srckfibkpt681aa, 761, 5);
        gm_b0_1 t64 = GM_LD(gm_b0_1, (((uint8_t *)&t63) + 1));
    }
    {
        gm_b16_8 t65 = GM_LD(gm_b16_8, (((uint8_t *)&F.l1) + 0));
        gm_b16_8 t66 = t65;
        gm_b16_8 t67 = GM_LD(gm_b16_8, ((uint8_t *)&t66));
        uint8_t t68 = 0;
        F.ret = t67;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_cty_66ee0kf(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_cty_66ee0kf F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    uint64_t gm_l2; /* i */
    gm_b16_8 gm_l3; /* c */
    gm_b16_8 gm_l4; /* c */
    GM_STACK_CHECK(&F, 372u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint32_t t2 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 0));
        uint64_t t3 = (uint64_t)t2;
        gm_l2 = t3;
    }
    {
        uint64_t t5 = gm_l2;
        void * t6 = gm_l0;
        uint64_t t7 = GM_LD(uintptr_t, ((((uint8_t *)t6) + 464) + 0) + 8);
        uint8_t t8 = (t5 < t7);
        if (!t8) goto ie4;
        {
            uint64_t t11 = gm_l2;
            uint64_t t12 = (uintptr_t)t11;
            void * t13 = gm_l0;
            uint64_t t14 = GM_LD(uintptr_t, ((((uint8_t *)t13) + 464) + 0) + 8);
            if (t12 >= (uintptr_t)(t14)) gm_trap_index(t12, t14, gm_srckfibkpt681aa, 160, 17);
            if (!((((uint8_t *)GM_LD(void *, ((((uint8_t *)t13) + 464) + 0)) + t12 * 24))[16] != 0)) goto ie10;
            gm_b16_8 t15 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t13) + 464) + 0)) + t12 * 24));
            gm_l3 = t15;
            {
                gm_b16_8 t17 = gm_l3;
                uint8_t t18 = 0;
                F.ret = t17;
                goto gm_fret;
            }
            goto ix10;
ie10:;
ix10:;
        }
        goto ix4;
ie4:;
ix4:;
    }
    {
        void * t19 = gm_l0;
        gm_t88rcucg1b7uk t20 = gm_l1;
        gm_b16_8 t21 = gm_f_make_cty_3kvgdd5((void *)NULL, cx, NULL, t19, t20);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l4 = t21;
    }
    {
        for (;;) {
            void * t23 = gm_l0;
            uint64_t t24 = GM_LD(uintptr_t, ((((uint8_t *)t23) + 464) + 0) + 8);
            uint64_t t25 = gm_l2;
            uint8_t t26 = (t24 <= t25);
            if (!t26) goto lb22;
            {
                void * t29 = gm_l0;
                void * t30 = (void *)(((uint8_t *)t29) + 464);
                gm_th2lqbo77pvsc t31;
                memset(&t31, 0, sizeof t31);
                gm_tftb1odc739t5 t32 = gm_f_push_d0271rf(t30, &t31);
                gm_tftb1odc739t5 t33 = t32;
                if (((((uint8_t *)&t33))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t33) + 1, gm_srckfibkpt681aa, 163, 41);
                gm_b0_1 t34 = GM_LD(gm_b0_1, (((uint8_t *)&t33) + 1));
            }
lc22:;
        }
lb22:;
    }
    {
        uint64_t t35 = gm_l2;
        uint64_t t36 = (uintptr_t)t35;
        void * t37 = gm_l0;
        uint64_t t38 = GM_LD(uintptr_t, ((((uint8_t *)t37) + 464) + 0) + 8);
        if (t36 >= (uintptr_t)(t38)) gm_trap_index(t36, t38, gm_srckfibkpt681aa, 164, 5);
        gm_b16_8 t39 = gm_l4;
        gm_th2lqbo77pvsc t40;
        GM_ST(uint8_t, (uint8_t *)&t40 + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&t40 + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&t40 + 20, 0);
        GM_ST(gm_b16_8, ((uint8_t *)&t40), t39);
        ((uint8_t *)&t40)[16] = 1;
        GM_ST(gm_th2lqbo77pvsc, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t37) + 464) + 0)) + t36 * 24), t40);
    }
    {
        gm_b16_8 t41 = gm_l4;
        uint8_t t42 = 0;
        F.ret = t41;
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
inline __attribute__((aligned(4))) void gm_f_ctypes_j6au0st(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_ctypes_j6au0st F;
    void * gm_l0; /* s */
    GM_STACK_CHECK(&F, 1136u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&F.l1, a1, sizeof F.l1);
    memset((uint8_t *)&(*(gm_trg1s7erfs461 *)gm_out) + 809, 0, 7);
    void * t1 = gm_l0;
    void * t2 = gm_l0;
    gm_t4grvdhp6dkf1 t3;
    gm_f_layouts_au8nfbt((void *)&t3, t2, &(*(gm_t0rfe1ou14h21 *)((uint8_t *)&F.l1)));
    gm_trqfvmjtt4sij t4;
    gm_f_list_nvrrj0t((void *)&t4, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_t4c7ipjj9s3fp t5;
    gm_f_hash_map_cuh4fs9((void *)&t5, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_t4c7ipjj9s3fp t6;
    gm_f_hash_map_tl7apid((void *)&t6, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_ttiitul0263ld t7;
    gm_f_new_namer_5q88o9g((void *)&t7, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_trqfvmjtt4sij t8;
    gm_f_list_uvfu1k1((void *)&t8, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_trqfvmjtt4sij t9;
    gm_f_list_uvfu1k1((void *)&t9, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_trqfvmjtt4sij t10;
    gm_f_list_uvfu1k1((void *)&t10, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_t4c7ipjj9s3fp t11;
    gm_f_hash_map_1s9eugt((void *)&t11, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_trqfvmjtt4sij t12;
    gm_f_list_0c2v515((void *)&t12, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_t4c7ipjj9s3fp t13;
    gm_f_hash_map_p3t5tj8((void *)&t13, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_trqfvmjtt4sij t14;
    gm_f_list_uvfu1k1((void *)&t14, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_t4c7ipjj9s3fp t15;
    gm_f_hash_map_1s9eugt((void *)&t15, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_t4c7ipjj9s3fp t16;
    gm_f_hash_map_p3t5tj8((void *)&t16, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_trqfvmjtt4sij t17;
    gm_f_list_uvfu1k1((void *)&t17, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_t4c7ipjj9s3fp t18;
    gm_f_hash_map_cuh4fs9((void *)&t18, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_t4c7ipjj9s3fp t19;
    gm_f_hash_map_cuh4fs9((void *)&t19, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tbpdf5ej15iof t20;
    gm_f_views_bc1b3r4((void *)&t20, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    GM_ST(void *, (uint8_t *)&(*(gm_trg1s7erfs461 *)gm_out) + 0, t1);
    GM_ST(gm_t4grvdhp6dkf1, (uint8_t *)&(*(gm_trg1s7erfs461 *)gm_out) + 8, t3);
    GM_ST(gm_trqfvmjtt4sij, (uint8_t *)&(*(gm_trg1s7erfs461 *)gm_out) + 80, t4);
    GM_ST(gm_t4c7ipjj9s3fp, (uint8_t *)&(*(gm_trg1s7erfs461 *)gm_out) + 120, t5);
    GM_ST(gm_t4c7ipjj9s3fp, (uint8_t *)&(*(gm_trg1s7erfs461 *)gm_out) + 176, t6);
    GM_ST(gm_ttiitul0263ld, (uint8_t *)&(*(gm_trg1s7erfs461 *)gm_out) + 232, t7);
    GM_ST(gm_trqfvmjtt4sij, (uint8_t *)&(*(gm_trg1s7erfs461 *)gm_out) + 288, t8);
    GM_ST(gm_trqfvmjtt4sij, (uint8_t *)&(*(gm_trg1s7erfs461 *)gm_out) + 328, t9);
    GM_ST(gm_trqfvmjtt4sij, (uint8_t *)&(*(gm_trg1s7erfs461 *)gm_out) + 368, t10);
    GM_ST(gm_t4c7ipjj9s3fp, (uint8_t *)&(*(gm_trg1s7erfs461 *)gm_out) + 408, t11);
    GM_ST(gm_trqfvmjtt4sij, (uint8_t *)&(*(gm_trg1s7erfs461 *)gm_out) + 464, t12);
    GM_ST(gm_t4c7ipjj9s3fp, (uint8_t *)&(*(gm_trg1s7erfs461 *)gm_out) + 504, t13);
    GM_ST(gm_trqfvmjtt4sij, (uint8_t *)&(*(gm_trg1s7erfs461 *)gm_out) + 560, t14);
    GM_ST(gm_t4c7ipjj9s3fp, (uint8_t *)&(*(gm_trg1s7erfs461 *)gm_out) + 600, t15);
    GM_ST(gm_t4c7ipjj9s3fp, (uint8_t *)&(*(gm_trg1s7erfs461 *)gm_out) + 656, t16);
    GM_ST(gm_trqfvmjtt4sij, (uint8_t *)&(*(gm_trg1s7erfs461 *)gm_out) + 712, t17);
    GM_ST(gm_t4c7ipjj9s3fp, (uint8_t *)&(*(gm_trg1s7erfs461 *)gm_out) + 752, t18);
    GM_ST(uint8_t, (uint8_t *)&(*(gm_trg1s7erfs461 *)gm_out) + 808, ((uint8_t)0));
    GM_ST(gm_t4c7ipjj9s3fp, (uint8_t *)&(*(gm_trg1s7erfs461 *)gm_out) + 816, t19);
    GM_ST(gm_tbpdf5ej15iof, (uint8_t *)&(*(gm_trg1s7erfs461 *)gm_out) + 872, t20);
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
__attribute__((aligned(4))) gm_b16_8 gm_f_define_typed_m3mqpm3(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, gm_b16_8 a3, uint64_t a4) {
    struct gm_fr_define_typed_m3mqpm3 F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_b16_8 gm_l2; /* members */
    gm_b16_8 gm_l3; /* named */
    uint64_t gm_l4; /* natural */
    uint64_t gm_l5; /* size */
    uint64_t gm_l6; /* al */
    gm_b16_8 gm_l7; /* attr */
    gm_b16_8 gm_l8; /* shape */
    gm_b16_8 gm_l9; /* n */
    gm_b16_8 gm_l10; /* name */
    gm_b16_8 gm_l11; /* <tmp> */
    uint64_t gm_l12; /* <tmp> */
    gm_t3877pk7lv748 gm_l13; /* m */
    GM_STACK_CHECK(&F, 1520u);
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
        gm_t88rcucg1b7uk t3 = gm_l1;
        uint64_t t4 = gm_f_size_dmhtkib((void *)NULL, cx, NULL, t2, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l5 = t4;
    }
    {
        void * t5 = gm_l0;
        gm_t88rcucg1b7uk t6 = gm_l1;
        uint64_t t7 = gm_f_align_uu2harl((void *)NULL, cx, NULL, t5, t6);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l6 = t7;
    }
    {
        gm_b16_8 t8;
        uint64_t t10 = gm_l6;
        uint64_t t11 = gm_l4;
        uint8_t t12 = (t10 > t11);
        if (!t12) goto ie9;
        uint64_t t14 = gm_l6;
        gm_t4ce0jd3tdro4 t15;
        GM_ST(uint64_t, ((uint8_t *)&t15 + 0), t14);
        gm_tabb710d9mq2e t16;
        gm_f_format_qc4s0m5((void *)&t16, (void *)NULL, cx, NULL, t15);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t17 = t16;
        if (((((uint8_t *)&t17))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t17) + 8, gm_srckfibkpt681aa, 313, 38);
        gm_b16_8 t18 = GM_LD(gm_b16_8, (((uint8_t *)&t17) + 8));
        gm_b16_8 t19 = t18;
        gm_b16_8 t20 = t19;
        gm_b16_8 t21 = GM_LD(gm_b16_8, ((uint8_t *)&t20));
        t8 = t21;
        goto ix9;
ie9:;
        gm_b16_8 t23;
        GM_ST(void *, (uint8_t *)&t23, (void *)(gm_sr4qbrk6tlgaa));
        GM_ST(uintptr_t, (uint8_t *)&t23 + 8, (uintptr_t)(0));
        gm_b16_8 t24 = t23;
        t8 = t24;
ix9:;
        gm_l7 = t8;
    }
    {
        gm_t6j6smj7qoctu t25;
        gm_b16_8 t26 = gm_l7;
        GM_ST(gm_b16_8, (uint8_t *)&t25 + 0, t26);
        gm_b16_8 t27 = gm_l2;
        GM_ST(gm_b16_8, (uint8_t *)&t25 + 16, t27);
        gm_tabb710d9mq2e t28;
        gm_f_format_8n626pi((void *)&t28, (void *)NULL, cx, NULL, &t25);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t29 = t28;
        if (((((uint8_t *)&t29))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t29) + 8, gm_srckfibkpt681aa, 314, 14);
        gm_b16_8 t30 = GM_LD(gm_b16_8, (((uint8_t *)&t29) + 8));
        gm_l8 = t30;
    }
    {
        void * t32 = gm_l0;
        void * t33 = (void *)(((uint8_t *)t32) + 504);
        gm_b16_8 t34 = gm_l8;
        gm_b16_8 t35 = t34;
        gm_b16_8 t36 = GM_LD(gm_b16_8, ((uint8_t *)&t35));
        gm_th2lqbo77pvsc t37;
        gm_f_get_e3joqcc((void *)&t37, t33, t36);
        gm_th2lqbo77pvsc t38 = t37;
        if (!((((uint8_t *)&t38))[16] != 0)) goto ie31;
        gm_b16_8 t39 = GM_LD(gm_b16_8, ((uint8_t *)&t38));
        gm_l9 = t39;
        {
            gm_b16_8 t41 = gm_l9;
            uint8_t t42 = 0;
            F.ret = t41;
            goto gm_fret;
        }
        goto ix31;
ie31:;
ix31:;
    }
    {
        gm_b16_8 t43;
        GM_ST(void *, (uint8_t *)&t43, (void *)(gm_sf1p9u1vovjso));
        GM_ST(uintptr_t, (uint8_t *)&t43 + 8, (uintptr_t)(4));
        gm_b16_8 t44 = gm_l8;
        gm_b16_8 t45 = t44;
        gm_b16_8 t46 = GM_LD(gm_b16_8, ((uint8_t *)&t45));
        gm_b16_8 t47 = gm_f_content_name_83mn5uf((void *)NULL, cx, NULL, t43, t46);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l10 = t47;
    }
    {
        void * t48 = gm_l0;
        void * t49 = (void *)(((uint8_t *)t48) + 560);
        gm_tfeab279c16rr t50;
        gm_b16_8 t51 = gm_l7;
        GM_ST(gm_b16_8, (uint8_t *)&t50 + 0, t51);
        gm_b16_8 t52 = gm_l2;
        GM_ST(gm_b16_8, (uint8_t *)&t50 + 16, t52);
        gm_b16_8 t53 = gm_l10;
        GM_ST(gm_b16_8, (uint8_t *)&t50 + 32, t53);
        gm_tftb1odc739t5 t54 = gm_f_write_f1h65c6(t49, &t50);
        gm_tftb1odc739t5 t55 = t54;
        if (((((uint8_t *)&t55))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t55) + 1, gm_srckfibkpt681aa, 317, 5);
        gm_b0_1 t56 = GM_LD(gm_b0_1, (((uint8_t *)&t55) + 1));
    }
    {
        void * t57 = gm_l0;
        void * t58 = (void *)(((uint8_t *)t57) + 560);
        gm_thddbqb6snabm t59;
        gm_b16_8 t60 = gm_l10;
        GM_ST(gm_b16_8, (uint8_t *)&t59 + 0, t60);
        uint64_t t61 = gm_l5;
        GM_ST(uint64_t, (uint8_t *)&t59 + 16, t61);
        gm_b16_8 t62 = gm_l10;
        GM_ST(gm_b16_8, (uint8_t *)&t59 + 24, t62);
        uint64_t t63 = gm_l6;
        GM_ST(uint64_t, (uint8_t *)&t59 + 40, t63);
        gm_tftb1odc739t5 t64 = gm_f_write_cscq5c8(t58, &t59);
        gm_tftb1odc739t5 t65 = t64;
        if (((((uint8_t *)&t65))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t65) + 1, gm_srckfibkpt681aa, 318, 5);
        gm_b0_1 t66 = GM_LD(gm_b0_1, (((uint8_t *)&t65) + 1));
    }
    {
        {
            gm_b16_8 t68 = gm_l3;
            gm_l11 = t68;
        }
        {
            gm_l12 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t70 = gm_l12;
                uint64_t t71 = GM_LD(uintptr_t, ((uint8_t *)&gm_l11) + 8);
                uint8_t t72 = (t70 < t71);
                if (!t72) goto lb69;
                {
                    uint64_t t75 = gm_l12;
                    uint64_t t76 = (uintptr_t)t75;
                    uint64_t t77 = GM_LD(uintptr_t, ((uint8_t *)&gm_l11) + 8);
                    gm_t3877pk7lv748 t80 = GM_LD(gm_t3877pk7lv748, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l11)) + t76 * 24));
                    gm_l13 = t80;
                    {
                        void * t82 = gm_l0;
                        void * t83 = (void *)(((uint8_t *)t82) + 560);
                        gm_tftksr4op114e t84;
                        gm_b16_8 t85 = gm_l10;
                        GM_ST(gm_b16_8, (uint8_t *)&t84 + 0, t85);
                        gm_b16_8 t86 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l13) + 0));
                        GM_ST(gm_b16_8, (uint8_t *)&t84 + 16, t86);
                        uint64_t t87 = GM_LD(uint64_t, (((uint8_t *)&gm_l13) + 16));
                        GM_ST(uint64_t, (uint8_t *)&t84 + 32, t87);
                        gm_tftb1odc739t5 t88 = gm_f_write_3f2sd55(t83, &t84);
                        gm_tftb1odc739t5 t89 = t88;
                        if (((((uint8_t *)&t89))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t89) + 1, gm_srckfibkpt681aa, 319, 22);
                        gm_b0_1 t90 = GM_LD(gm_b0_1, (((uint8_t *)&t89) + 1));
                    }
                    goto me78;
mn79:;
                    gm_trap("no match arm matched the scrutinee", gm_srckfibkpt681aa, 319, 5);
me78:;
                }
lc69:;
                {
                    uint64_t t92 = gm_l12;
                    uint64_t r93; bool o94 = __builtin_add_overflow(t92, ((uint64_t)1ULL), &r93);
                    uint64_t t95 = r93;
                    gm_l12 = t95;
                }
            }
lb69:;
        }
    }
    {
        void * t96 = gm_l0;
        void * t97 = (void *)(((uint8_t *)t96) + 560);
        void * t98 = gm_l0;
        gm_t88rcucg1b7uk t99 = gm_l1;
        gm_b16_8 t100 = gm_f_show_5qbi623((void *)NULL, cx, NULL, t98, t99);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tnpifv20irnph t101;
        GM_ST(gm_b16_8, ((uint8_t *)&t101 + 0), t100);
        gm_tabb710d9mq2e t102;
        gm_f_format_1cgbgnn((void *)&t102, (void *)NULL, cx, NULL, t101);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t103 = t102;
        if (((((uint8_t *)&t103))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t103) + 8, gm_srckfibkpt681aa, 320, 52);
        gm_b16_8 t104 = GM_LD(gm_b16_8, (((uint8_t *)&t103) + 8));
        gm_b16_8 t105 = t104;
        gm_b16_8 t106 = GM_LD(gm_b16_8, ((uint8_t *)&t105));
        gm_b16_8 t107 = gm_f_assert_message_6bd9l6u((void *)NULL, cx, NULL, t106);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tnpifv20irnph t108;
        GM_ST(gm_b16_8, ((uint8_t *)&t108 + 0), t107);
        gm_tftb1odc739t5 t109 = gm_f_write_pecnce5(t97, t108);
        gm_tftb1odc739t5 t110 = t109;
        if (((((uint8_t *)&t110))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t110) + 1, gm_srckfibkpt681aa, 320, 5);
        gm_b0_1 t111 = GM_LD(gm_b0_1, (((uint8_t *)&t110) + 1));
    }
    {
        void * t112 = gm_l0;
        void * t113 = (void *)(((uint8_t *)t112) + 504);
        gm_b16_8 t114 = gm_l8;
        gm_b16_8 t115 = t114;
        gm_b16_8 t116 = GM_LD(gm_b16_8, ((uint8_t *)&t115));
        gm_b16_8 t117 = gm_l10;
        gm_tftb1odc739t5 t118 = gm_f_put_7shfu9u(t113, t116, t117);
        gm_tftb1odc739t5 t119 = t118;
        if (((((uint8_t *)&t119))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t119) + 1, gm_srckfibkpt681aa, 321, 5);
        gm_b0_1 t120 = GM_LD(gm_b0_1, (((uint8_t *)&t119) + 1));
    }
    {
        gm_b16_8 t121 = gm_l10;
        uint8_t t122 = 0;
        F.ret = t121;
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
__attribute__((aligned(4))) gm_b16_8 gm_f_desc_0cag2kq(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_desc_0cag2kq F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_b16_8 gm_l2; /* i */
    gm_b16_8 gm_l3; /* i */
    uint8_t gm_l5; /* signed */
    uint16_t gm_l6; /* bits */
    uint16_t gm_l7; /* bits */
    gm_t88rcucg1b7uk gm_l8; /* elem */
    gm_b16_8 gm_l9; /* e */
    uint64_t gm_l10; /* len */
    gm_t88rcucg1b7uk gm_l11; /* elem */
    gm_b16_8 gm_l12; /* e */
    gm_b16_8 gm_l13; /* elems */
    gm_b16_8 gm_l14; /* names */
    gm_b16_8 gm_l15; /* offs */
    gm_b16_8 gm_l17; /* <tmp> */
    uint64_t gm_l18; /* <tmp> */
    uint64_t gm_l19; /* k */
    gm_t88rcucg1b7uk gm_l20; /* et */
    gm_b16_8 gm_l21; /* d */
    gm_b16_8 gm_l22; /* name */
    gm_t88rcucg1b7uk gm_l23; /* inner */
    gm_b16_8 gm_l24; /* e */
    gm_t88rcucg1b7uk gm_l25; /* err */
    gm_t88rcucg1b7uk gm_l26; /* ok */
    gm_b16_8 gm_l27; /* e */
    gm_b16_8 gm_l28; /* o */
    gm_b16_8 gm_l29; /* offs */
    uint32_t gm_l30; /* id */
    void * gm_l31; /* def */
    gm_b16_8 gm_l33; /* offs */
    gm_b16_8 gm_l35; /* <tmp> */
    uint64_t gm_l36; /* <tmp> */
    uint64_t gm_l37; /* k */
    gm_b16_8 gm_l39; /* d */
    gm_tcmkompbl09il gm_l40; /* bl */
    gm_b16_8 gm_l42; /* <tmp> */
    uint64_t gm_l43; /* <tmp> */
    uint64_t gm_l44; /* k */
    gm_tov4mtrfp5e7v gm_l46; /* bf */
    gm_b16_8 gm_l47; /* d */
    gm_t88rcucg1b7uk gm_l48; /* tag */
    gm_b16_8 gm_l49; /* variants */
    gm_b16_8 gm_l50; /* offs */
    gm_b16_8 gm_l52; /* <tmp> */
    uint64_t gm_l53; /* <tmp> */
    gm_t88rcucg1b7uk gm_l55; /* pt */
    gm_b16_8 gm_l56; /* p */
    gm_b16_8 gm_l58; /* <tmp> */
    uint64_t gm_l59; /* <tmp> */
    gm_b16_8 gm_l60; /* x */
    GM_STACK_CHECK(&F, 9414u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t3 = gm_l0;
        void * t4 = (void *)(((uint8_t *)t3) + 176);
        uint32_t t5 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 0));
        gm_th2lqbo77pvsc t6;
        gm_f_get_gnmase9((void *)&t6, t4, t5);
        gm_th2lqbo77pvsc t7 = t6;
        if (!((((uint8_t *)&t7))[16] != 0)) goto ie2;
        gm_b16_8 t8 = GM_LD(gm_b16_8, ((uint8_t *)&t7));
        gm_l2 = t8;
        {
            gm_b16_8 t10 = gm_l2;
            gm_tnpifv20irnph t11;
            GM_ST(gm_b16_8, ((uint8_t *)&t11 + 0), t10);
            gm_tabb710d9mq2e t12;
            gm_f_format_h6b2ofo((void *)&t12, (void *)NULL, cx, NULL, t11);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t13 = t12;
            if (((((uint8_t *)&t13))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t13) + 8, gm_srckfibkpt681aa, 482, 44);
            gm_b16_8 t14 = GM_LD(gm_b16_8, (((uint8_t *)&t13) + 8));
            gm_b16_8 t15 = t14;
            gm_b16_8 t16 = GM_LD(gm_b16_8, ((uint8_t *)&t15));
            uint8_t t17 = 0;
            F.ret = t16;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t18 = gm_l0;
        void * t19 = (void *)(((uint8_t *)t18) + 232);
        gm_b16_8 t20;
        GM_ST(void *, (uint8_t *)&t20, (void *)(gm_sm6ioruobq064));
        GM_ST(uintptr_t, (uint8_t *)&t20 + 8, (uintptr_t)(5));
        void * t21 = gm_l0;
        gm_t88rcucg1b7uk t22 = gm_l1;
        gm_b16_8 t23 = gm_f_show_5qbi623((void *)NULL, cx, NULL, t21, t22);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b16_8 t24 = gm_f_keyed_h72lrop((void *)NULL, cx, NULL, t19, t20, t23);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l3 = t24;
    }
    {
        void * t25 = gm_l0;
        void * t26 = (void *)(((uint8_t *)t25) + 176);
        uint32_t t27 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 0));
        gm_b16_8 t28 = gm_l3;
        gm_tftb1odc739t5 t29 = gm_f_put_po09vud(t26, t27, t28);
        gm_tftb1odc739t5 t30 = t29;
        if (((((uint8_t *)&t30))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t30) + 1, gm_srckfibkpt681aa, 484, 5);
        gm_b0_1 t31 = GM_LD(gm_b0_1, (((uint8_t *)&t30) + 1));
    }
    {
        void * t32 = gm_l0;
        void * t33 = (void *)(((uint8_t *)t32) + 288);
        gm_b16_8 t34 = gm_l3;
        gm_tnpifv20irnph t35;
        GM_ST(gm_b16_8, ((uint8_t *)&t35 + 0), t34);
        gm_tftb1odc739t5 t36 = gm_f_write_trmu0m5(t33, t35);
        gm_tftb1odc739t5 t37 = t36;
        if (((((uint8_t *)&t37))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t37) + 1, gm_srckfibkpt681aa, 485, 5);
        gm_b0_1 t38 = GM_LD(gm_b0_1, (((uint8_t *)&t37) + 1));
    }
    {
        gm_f_list_epvducg((void *)&F.l4, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t39 = (void *)((uint8_t *)&F.l4);
        void * t40 = gm_l0;
        gm_t88rcucg1b7uk t41 = gm_l1;
        uint64_t t42 = gm_f_size_dmhtkib((void *)NULL, cx, NULL, t40, t41);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t4ce0jd3tdro4 t43;
        GM_ST(uint64_t, ((uint8_t *)&t43 + 0), t42);
        gm_tabb710d9mq2e t44;
        gm_f_format_jpvhgp3((void *)&t44, (void *)NULL, cx, NULL, t43);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t45 = t44;
        if (((((uint8_t *)&t45))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t45) + 8, gm_srckfibkpt681aa, 487, 12);
        gm_b16_8 t46 = GM_LD(gm_b16_8, (((uint8_t *)&t45) + 8));
        gm_b16_8 t47 = t46;
        gm_b16_8 t48 = GM_LD(gm_b16_8, ((uint8_t *)&t47));
        gm_tftb1odc739t5 t49 = gm_f_push_3ef746b(t39, t48);
        gm_tftb1odc739t5 t50 = t49;
        if (((((uint8_t *)&t50))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t50) + 1, gm_srckfibkpt681aa, 487, 5);
        gm_b0_1 t51 = GM_LD(gm_b0_1, (((uint8_t *)&t50) + 1));
    }
    {
        void * t52 = (void *)((uint8_t *)&F.l4);
        void * t53 = gm_l0;
        gm_t88rcucg1b7uk t54 = gm_l1;
        uint64_t t55 = gm_f_align_uu2harl((void *)NULL, cx, NULL, t53, t54);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t4ce0jd3tdro4 t56;
        GM_ST(uint64_t, ((uint8_t *)&t56 + 0), t55);
        gm_tabb710d9mq2e t57;
        gm_f_format_ef193cn((void *)&t57, (void *)NULL, cx, NULL, t56);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t58 = t57;
        if (((((uint8_t *)&t58))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t58) + 8, gm_srckfibkpt681aa, 488, 12);
        gm_b16_8 t59 = GM_LD(gm_b16_8, (((uint8_t *)&t58) + 8));
        gm_b16_8 t60 = t59;
        gm_b16_8 t61 = GM_LD(gm_b16_8, ((uint8_t *)&t60));
        gm_tftb1odc739t5 t62 = gm_f_push_3ef746b(t52, t61);
        gm_tftb1odc739t5 t63 = t62;
        if (((((uint8_t *)&t63))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t63) + 1, gm_srckfibkpt681aa, 488, 5);
        gm_b0_1 t64 = GM_LD(gm_b0_1, (((uint8_t *)&t63) + 1));
    }
    {
        void * t65 = gm_l0;
        gm_t88rcucg1b7uk t66 = gm_l1;
        gm_t53at80pfdus3 t67;
        gm_f_kind_ghvav22((void *)&t67, t65, GM_CONV(gm_csguankko4lmhd, t66));
        gm_t53at80pfdus3 t68 = t67;
        uint8_t t83 = GM_LD(uint8_t, (((uint8_t *)&t68) + 0));
        switch (t83) { case ((uint8_t)0ULL): goto ma70; case ((uint8_t)1ULL): goto ma71; case ((uint8_t)2ULL): goto ma71; case ((uint8_t)3ULL): goto ma72; case ((uint8_t)4ULL): goto ma73; case ((uint8_t)5ULL): goto ma81; case ((uint8_t)6ULL): goto ma81; case ((uint8_t)7ULL): goto ma74; case ((uint8_t)8ULL): goto ma74; case ((uint8_t)9ULL): goto ma74; case ((uint8_t)10ULL): goto ma75; case ((uint8_t)11ULL): goto ma76; case ((uint8_t)12ULL): goto ma77; case ((uint8_t)13ULL): goto ma78; case ((uint8_t)14ULL): goto ma79; case ((uint8_t)15ULL): goto ma81; case ((uint8_t)16ULL): goto ma74; case ((uint8_t)17ULL): goto ma80; case ((uint8_t)18ULL): goto ma81; case ((uint8_t)19ULL): goto ma81; case ((uint8_t)20ULL): goto ma81; case ((uint8_t)21ULL): goto ma81; case ((uint8_t)22ULL): goto ma81; case ((uint8_t)23ULL): goto ma81; case ((uint8_t)24ULL): goto ma81; case ((uint8_t)25ULL): goto ma81; case ((uint8_t)26ULL): goto ma81; default: goto ma81; }
ma70:;
        uint8_t t85 = GM_LD(uint8_t, (((uint8_t *)&t68) + 0));
        if (t85 != ((uint8_t)0ULL)) goto mn84;
        uint8_t t86 = GM_LD(uint8_t, ((((uint8_t *)&t68) + 8) + 0));
        gm_l5 = t86;
        uint16_t t87 = GM_LD(uint16_t, ((((uint8_t *)&t68) + 8) + 2));
        gm_l6 = t87;
        {
            void * t89 = (void *)((uint8_t *)&F.l4);
            gm_b16_8 t90;
            GM_ST(void *, (uint8_t *)&t90, (void *)(gm_sropo1941pvlt));
            GM_ST(uintptr_t, (uint8_t *)&t90 + 8, (uintptr_t)(15));
            gm_tftb1odc739t5 t91 = gm_f_push_3ef746b(t89, t90);
            gm_tftb1odc739t5 t92 = t91;
            if (((((uint8_t *)&t92))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t92) + 1, gm_srckfibkpt681aa, 491, 13);
            gm_b0_1 t93 = GM_LD(gm_b0_1, (((uint8_t *)&t92) + 1));
        }
        {
            void * t94 = (void *)((uint8_t *)&F.l4);
            uint16_t t95 = gm_l6;
            gm_tu0pangbbaeec t96;
            GM_ST(uint16_t, ((uint8_t *)&t96 + 0), t95);
            gm_tabb710d9mq2e t97;
            gm_f_format_ms4f9aj((void *)&t97, (void *)NULL, cx, NULL, t96);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t98 = t97;
            if (((((uint8_t *)&t98))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t98) + 8, gm_srckfibkpt681aa, 492, 20);
            gm_b16_8 t99 = GM_LD(gm_b16_8, (((uint8_t *)&t98) + 8));
            gm_b16_8 t100 = t99;
            gm_b16_8 t101 = GM_LD(gm_b16_8, ((uint8_t *)&t100));
            gm_tftb1odc739t5 t102 = gm_f_push_3ef746b(t94, t101);
            gm_tftb1odc739t5 t103 = t102;
            if (((((uint8_t *)&t103))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t103) + 1, gm_srckfibkpt681aa, 492, 13);
            gm_b0_1 t104 = GM_LD(gm_b0_1, (((uint8_t *)&t103) + 1));
        }
        {
            void * t105 = (void *)((uint8_t *)&F.l4);
            uint8_t t106 = gm_l5;
            gm_t9qkh0rsooc8d t107;
            GM_ST(uint8_t, ((uint8_t *)&t107 + 0), t106);
            gm_tabb710d9mq2e t108;
            gm_f_format_lkeo172((void *)&t108, (void *)NULL, cx, NULL, t107);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t109 = t108;
            if (((((uint8_t *)&t109))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t109) + 8, gm_srckfibkpt681aa, 493, 20);
            gm_b16_8 t110 = GM_LD(gm_b16_8, (((uint8_t *)&t109) + 8));
            gm_b16_8 t111 = t110;
            gm_b16_8 t112 = GM_LD(gm_b16_8, ((uint8_t *)&t111));
            gm_tftb1odc739t5 t113 = gm_f_push_3ef746b(t105, t112);
            gm_tftb1odc739t5 t114 = t113;
            if (((((uint8_t *)&t114))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t114) + 1, gm_srckfibkpt681aa, 493, 13);
            gm_b0_1 t115 = GM_LD(gm_b0_1, (((uint8_t *)&t114) + 1));
        }
        goto me69;
mn84:;
ma71:;
        uint8_t t119 = GM_LD(uint8_t, (((uint8_t *)&t68) + 0));
        if (t119 != ((uint8_t)1ULL)) goto pa118;
        goto po117;
pa118:;
        uint8_t t121 = GM_LD(uint8_t, (((uint8_t *)&t68) + 0));
        if (t121 != ((uint8_t)2ULL)) goto pa120;
        goto po117;
pa120:;
        goto mn116;
po117:;
        {
            void * t123 = (void *)((uint8_t *)&F.l4);
            gm_b16_8 t124;
            GM_ST(void *, (uint8_t *)&t124, (void *)(gm_sropo1941pvlt));
            GM_ST(uintptr_t, (uint8_t *)&t124 + 8, (uintptr_t)(15));
            gm_tftb1odc739t5 t125 = gm_f_push_3ef746b(t123, t124);
            gm_tftb1odc739t5 t126 = t125;
            if (((((uint8_t *)&t126))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t126) + 1, gm_srckfibkpt681aa, 496, 13);
            gm_b0_1 t127 = GM_LD(gm_b0_1, (((uint8_t *)&t126) + 1));
        }
        {
            void * t128 = (void *)((uint8_t *)&F.l4);
            void * t129 = gm_l0;
            void * t130 = (void *)((((uint8_t *)t129) + 8) + 0);
            uint32_t t131 = gm_f_ptr_bits_glps5i7(t130);
            gm_t88rcucg1b7uk t132;
            GM_ST(uint32_t, ((uint8_t *)&t132 + 0), t131);
            gm_tabb710d9mq2e t133;
            gm_f_format_ch41dev((void *)&t133, (void *)NULL, cx, NULL, t132);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t134 = t133;
            if (((((uint8_t *)&t134))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t134) + 8, gm_srckfibkpt681aa, 497, 20);
            gm_b16_8 t135 = GM_LD(gm_b16_8, (((uint8_t *)&t134) + 8));
            gm_b16_8 t136 = t135;
            gm_b16_8 t137 = GM_LD(gm_b16_8, ((uint8_t *)&t136));
            gm_tftb1odc739t5 t138 = gm_f_push_3ef746b(t128, t137);
            gm_tftb1odc739t5 t139 = t138;
            if (((((uint8_t *)&t139))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t139) + 1, gm_srckfibkpt681aa, 497, 13);
            gm_b0_1 t140 = GM_LD(gm_b0_1, (((uint8_t *)&t139) + 1));
        }
        {
            void * t141 = (void *)((uint8_t *)&F.l4);
            void * t142 = gm_l0;
            gm_t88rcucg1b7uk t143 = gm_l1;
            gm_t53at80pfdus3 t144;
            gm_f_kind_ghvav22((void *)&t144, t142, GM_CONV(gm_csguankko4lmhd, t143));
            gm_t53at80pfdus3 t145 = t144;
            uint8_t t146 = ((GM_LD(uint8_t, ((uint8_t *)&t145) + 0) == ((uint8_t)2ULL)));
            gm_t9qkh0rsooc8d t147;
            GM_ST(uint8_t, ((uint8_t *)&t147 + 0), t146);
            gm_tabb710d9mq2e t148;
            gm_f_format_lkeo172((void *)&t148, (void *)NULL, cx, NULL, t147);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t149 = t148;
            if (((((uint8_t *)&t149))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t149) + 8, gm_srckfibkpt681aa, 498, 20);
            gm_b16_8 t150 = GM_LD(gm_b16_8, (((uint8_t *)&t149) + 8));
            gm_b16_8 t151 = t150;
            gm_b16_8 t152 = GM_LD(gm_b16_8, ((uint8_t *)&t151));
            gm_tftb1odc739t5 t153 = gm_f_push_3ef746b(t141, t152);
            gm_tftb1odc739t5 t154 = t153;
            if (((((uint8_t *)&t154))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t154) + 1, gm_srckfibkpt681aa, 498, 13);
            gm_b0_1 t155 = GM_LD(gm_b0_1, (((uint8_t *)&t154) + 1));
        }
        goto me69;
mn116:;
ma72:;
        uint8_t t157 = GM_LD(uint8_t, (((uint8_t *)&t68) + 0));
        if (t157 != ((uint8_t)3ULL)) goto mn156;
        uint16_t t158 = GM_LD(uint16_t, (((uint8_t *)&t68) + 8));
        gm_l7 = t158;
        {
            void * t160 = (void *)((uint8_t *)&F.l4);
            gm_b16_8 t161;
            GM_ST(void *, (uint8_t *)&t161, (void *)(gm_st53sho4849cg));
            GM_ST(uintptr_t, (uint8_t *)&t161 + 8, (uintptr_t)(17));
            gm_tftb1odc739t5 t162 = gm_f_push_3ef746b(t160, t161);
            gm_tftb1odc739t5 t163 = t162;
            if (((((uint8_t *)&t163))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t163) + 1, gm_srckfibkpt681aa, 501, 13);
            gm_b0_1 t164 = GM_LD(gm_b0_1, (((uint8_t *)&t163) + 1));
        }
        {
            void * t165 = (void *)((uint8_t *)&F.l4);
            uint16_t t166 = gm_l7;
            gm_tu0pangbbaeec t167;
            GM_ST(uint16_t, ((uint8_t *)&t167 + 0), t166);
            gm_tabb710d9mq2e t168;
            gm_f_format_ms4f9aj((void *)&t168, (void *)NULL, cx, NULL, t167);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t169 = t168;
            if (((((uint8_t *)&t169))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t169) + 8, gm_srckfibkpt681aa, 502, 20);
            gm_b16_8 t170 = GM_LD(gm_b16_8, (((uint8_t *)&t169) + 8));
            gm_b16_8 t171 = t170;
            gm_b16_8 t172 = GM_LD(gm_b16_8, ((uint8_t *)&t171));
            gm_tftb1odc739t5 t173 = gm_f_push_3ef746b(t165, t172);
            gm_tftb1odc739t5 t174 = t173;
            if (((((uint8_t *)&t174))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t174) + 1, gm_srckfibkpt681aa, 502, 13);
            gm_b0_1 t175 = GM_LD(gm_b0_1, (((uint8_t *)&t174) + 1));
        }
        goto me69;
mn156:;
ma73:;
        uint8_t t177 = GM_LD(uint8_t, (((uint8_t *)&t68) + 0));
        if (t177 != ((uint8_t)4ULL)) goto mn176;
        void * t178 = (void *)((uint8_t *)&F.l4);
        gm_b16_8 t179;
        GM_ST(void *, (uint8_t *)&t179, (void *)(gm_suvf0cqih9u7i));
        GM_ST(uintptr_t, (uint8_t *)&t179 + 8, (uintptr_t)(16));
        gm_tftb1odc739t5 t180 = gm_f_push_3ef746b(t178, t179);
        gm_tftb1odc739t5 t181 = t180;
        if (((((uint8_t *)&t181))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t181) + 1, gm_srckfibkpt681aa, 504, 18);
        gm_b0_1 t182 = GM_LD(gm_b0_1, (((uint8_t *)&t181) + 1));
        goto me69;
mn176:;
ma74:;
        uint8_t t186 = GM_LD(uint8_t, (((uint8_t *)&t68) + 0));
        if (t186 != ((uint8_t)8ULL)) goto pa185;
        goto po184;
pa185:;
        uint8_t t188 = GM_LD(uint8_t, (((uint8_t *)&t68) + 0));
        if (t188 != ((uint8_t)9ULL)) goto pa187;
        goto po184;
pa187:;
        uint8_t t190 = GM_LD(uint8_t, (((uint8_t *)&t68) + 0));
        if (t190 != ((uint8_t)7ULL)) goto pa189;
        goto po184;
pa189:;
        uint8_t t192 = GM_LD(uint8_t, (((uint8_t *)&t68) + 0));
        if (t192 != ((uint8_t)16ULL)) goto pa191;
        goto po184;
pa191:;
        goto mn183;
po184:;
        void * t193 = (void *)((uint8_t *)&F.l4);
        gm_b16_8 t194;
        GM_ST(void *, (uint8_t *)&t194, (void *)(gm_sa9ktdbn49iui));
        GM_ST(uintptr_t, (uint8_t *)&t194 + 8, (uintptr_t)(15));
        gm_tftb1odc739t5 t195 = gm_f_push_3ef746b(t193, t194);
        gm_tftb1odc739t5 t196 = t195;
        if (((((uint8_t *)&t196))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t196) + 1, gm_srckfibkpt681aa, 505, 53);
        gm_b0_1 t197 = GM_LD(gm_b0_1, (((uint8_t *)&t196) + 1));
        goto me69;
mn183:;
ma75:;
        uint8_t t199 = GM_LD(uint8_t, (((uint8_t *)&t68) + 0));
        if (t199 != ((uint8_t)10ULL)) goto mn198;
        gm_t88rcucg1b7uk t200 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t68) + 8) + 4));
        gm_l8 = t200;
        {
            void * t202 = gm_l0;
            gm_t88rcucg1b7uk t203 = gm_l8;
            gm_b16_8 t204 = gm_f_desc_0cag2kq((void *)NULL, cx, NULL, t202, t203);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l9 = t204;
        }
        {
            void * t205 = (void *)((uint8_t *)&F.l4);
            gm_b16_8 t206;
            GM_ST(void *, (uint8_t *)&t206, (void *)(gm_sdam23bqvafqn));
            GM_ST(uintptr_t, (uint8_t *)&t206 + 8, (uintptr_t)(17));
            gm_tftb1odc739t5 t207 = gm_f_push_3ef746b(t205, t206);
            gm_tftb1odc739t5 t208 = t207;
            if (((((uint8_t *)&t208))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t208) + 1, gm_srckfibkpt681aa, 508, 13);
            gm_b0_1 t209 = GM_LD(gm_b0_1, (((uint8_t *)&t208) + 1));
        }
        {
            void * t210 = (void *)((uint8_t *)&F.l4);
            gm_b16_8 t211 = gm_l9;
            gm_tnpifv20irnph t212;
            GM_ST(gm_b16_8, ((uint8_t *)&t212 + 0), t211);
            gm_tabb710d9mq2e t213;
            gm_f_format_vsbf5l9((void *)&t213, (void *)NULL, cx, NULL, t212);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t214 = t213;
            if (((((uint8_t *)&t214))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t214) + 8, gm_srckfibkpt681aa, 509, 20);
            gm_b16_8 t215 = GM_LD(gm_b16_8, (((uint8_t *)&t214) + 8));
            gm_b16_8 t216 = t215;
            gm_b16_8 t217 = GM_LD(gm_b16_8, ((uint8_t *)&t216));
            gm_tftb1odc739t5 t218 = gm_f_push_3ef746b(t210, t217);
            gm_tftb1odc739t5 t219 = t218;
            if (((((uint8_t *)&t219))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t219) + 1, gm_srckfibkpt681aa, 509, 13);
            gm_b0_1 t220 = GM_LD(gm_b0_1, (((uint8_t *)&t219) + 1));
        }
        goto me69;
mn198:;
ma76:;
        uint8_t t222 = GM_LD(uint8_t, (((uint8_t *)&t68) + 0));
        if (t222 != ((uint8_t)11ULL)) goto mn221;
        uint64_t t223 = GM_LD(uint64_t, ((((uint8_t *)&t68) + 8) + 0));
        gm_l10 = t223;
        gm_t88rcucg1b7uk t224 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t68) + 8) + 8));
        gm_l11 = t224;
        {
            void * t226 = gm_l0;
            gm_t88rcucg1b7uk t227 = gm_l11;
            gm_b16_8 t228 = gm_f_desc_0cag2kq((void *)NULL, cx, NULL, t226, t227);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l12 = t228;
        }
        {
            void * t229 = (void *)((uint8_t *)&F.l4);
            gm_b16_8 t230;
            GM_ST(void *, (uint8_t *)&t230, (void *)(gm_s0f7epcithn0r));
            GM_ST(uintptr_t, (uint8_t *)&t230 + 8, (uintptr_t)(17));
            gm_tftb1odc739t5 t231 = gm_f_push_3ef746b(t229, t230);
            gm_tftb1odc739t5 t232 = t231;
            if (((((uint8_t *)&t232))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t232) + 1, gm_srckfibkpt681aa, 513, 13);
            gm_b0_1 t233 = GM_LD(gm_b0_1, (((uint8_t *)&t232) + 1));
        }
        {
            void * t234 = (void *)((uint8_t *)&F.l4);
            gm_b16_8 t235 = gm_l12;
            gm_tnpifv20irnph t236;
            GM_ST(gm_b16_8, ((uint8_t *)&t236 + 0), t235);
            gm_tabb710d9mq2e t237;
            gm_f_format_vsbf5l9((void *)&t237, (void *)NULL, cx, NULL, t236);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t238 = t237;
            if (((((uint8_t *)&t238))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t238) + 8, gm_srckfibkpt681aa, 514, 20);
            gm_b16_8 t239 = GM_LD(gm_b16_8, (((uint8_t *)&t238) + 8));
            gm_b16_8 t240 = t239;
            gm_b16_8 t241 = GM_LD(gm_b16_8, ((uint8_t *)&t240));
            gm_tftb1odc739t5 t242 = gm_f_push_3ef746b(t234, t241);
            gm_tftb1odc739t5 t243 = t242;
            if (((((uint8_t *)&t243))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t243) + 1, gm_srckfibkpt681aa, 514, 13);
            gm_b0_1 t244 = GM_LD(gm_b0_1, (((uint8_t *)&t243) + 1));
        }
        {
            void * t245 = (void *)((uint8_t *)&F.l4);
            uint64_t t246 = gm_l10;
            gm_t4ce0jd3tdro4 t247;
            GM_ST(uint64_t, ((uint8_t *)&t247 + 0), t246);
            gm_tabb710d9mq2e t248;
            gm_f_format_km55tit((void *)&t248, (void *)NULL, cx, NULL, t247);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t249 = t248;
            if (((((uint8_t *)&t249))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t249) + 8, gm_srckfibkpt681aa, 515, 20);
            gm_b16_8 t250 = GM_LD(gm_b16_8, (((uint8_t *)&t249) + 8));
            gm_b16_8 t251 = t250;
            gm_b16_8 t252 = GM_LD(gm_b16_8, ((uint8_t *)&t251));
            gm_tftb1odc739t5 t253 = gm_f_push_3ef746b(t245, t252);
            gm_tftb1odc739t5 t254 = t253;
            if (((((uint8_t *)&t254))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t254) + 1, gm_srckfibkpt681aa, 515, 13);
            gm_b0_1 t255 = GM_LD(gm_b0_1, (((uint8_t *)&t254) + 1));
        }
        goto me69;
mn221:;
ma77:;
        uint8_t t257 = GM_LD(uint8_t, (((uint8_t *)&t68) + 0));
        if (t257 != ((uint8_t)12ULL)) goto mn256;
        gm_b16_8 t258 = GM_LD(gm_b16_8, ((((uint8_t *)&t68) + 8) + 0));
        gm_l13 = t258;
        gm_b16_8 t259 = GM_LD(gm_b16_8, ((((uint8_t *)&t68) + 8) + 16));
        gm_l14 = t259;
        {
            void * t261 = gm_l0;
            gm_t88rcucg1b7uk t262 = gm_l1;
            gm_b16_8 t263 = gm_f_offsets_eh6vfle((void *)NULL, cx, NULL, t261, t262);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l15 = t263;
        }
        {
            gm_f_list_epvducg((void *)&F.l16, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            {
                gm_b16_8 t265 = gm_l13;
                gm_l17 = t265;
            }
            {
                gm_l18 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t267 = gm_l18;
                    uint64_t t268 = GM_LD(uintptr_t, ((uint8_t *)&gm_l17) + 8);
                    uint8_t t269 = (t267 < t268);
                    if (!t269) goto lb266;
                    {
                        uint64_t t272 = gm_l18;
                        uint64_t t273 = gm_l18;
                        uint64_t t274 = (uintptr_t)t273;
                        uint64_t t275 = GM_LD(uintptr_t, ((uint8_t *)&gm_l17) + 8);
                        gm_t88rcucg1b7uk t276 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l17)) + t274 * 4));
                        gm_tlcm4lvu3g1a8 t277;
                        GM_ST(uint32_t, (uint8_t *)&t277 + 12, 0);
                        GM_ST(uint64_t, ((uint8_t *)&t277 + 0), t272);
                        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t277 + 8), t276);
                        gm_tlcm4lvu3g1a8 t278 = t277;
                        uint64_t t281 = GM_LD(uint64_t, (((uint8_t *)&t278) + 0));
                        gm_l19 = t281;
                        gm_t88rcucg1b7uk t282 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t278) + 8));
                        gm_l20 = t282;
                        {
                            void * t284 = gm_l0;
                            gm_t88rcucg1b7uk t285 = gm_l20;
                            gm_b16_8 t286 = gm_f_desc_0cag2kq((void *)NULL, cx, NULL, t284, t285);
                            if (gm_unw.kind) {
                                goto gm_ll270;
                            }
                            gm_l21 = t286;
                        }
                        {
                            gm_b16_8 t287;
                            uint64_t t289 = GM_LD(uintptr_t, ((uint8_t *)&gm_l14) + 8);
                            uint8_t t290 = (t289 > ((uint64_t)0ULL));
                            if (!t290) goto ie288;
                            uint64_t t292 = gm_l19;
                            uint64_t t293 = (uintptr_t)t292;
                            uint64_t t294 = GM_LD(uintptr_t, ((uint8_t *)&gm_l14) + 8);
                            if (t293 >= (uintptr_t)(t294)) gm_trap_index(t293, t294, gm_srckfibkpt681aa, 522, 60);
                            gm_b16_8 t295 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l14)) + t293 * 16));
                            gm_b16_8 t296 = gm_f_c_string_n7h27b3((void *)NULL, cx, NULL, t295);
                            if (gm_unw.kind) {
                                goto gm_ll270;
                            }
                            gm_b16_8 t297 = t296;
                            t287 = t297;
                            goto ix288;
ie288:;
                            gm_b16_8 t299;
                            GM_ST(void *, (uint8_t *)&t299, (void *)(gm_s04qus0luoga5));
                            GM_ST(uintptr_t, (uint8_t *)&t299 + 8, (uintptr_t)(4));
                            gm_b16_8 t300 = t299;
                            t287 = t300;
ix288:;
                            gm_l22 = t287;
                        }
                        {
                            void * t301 = (void *)((uint8_t *)&F.l16);
                            gm_tq3m5qgvkpje8 t302;
                            gm_b16_8 t303 = gm_l22;
                            GM_ST(gm_b16_8, (uint8_t *)&t302 + 0, t303);
                            uint64_t t304 = gm_l19;
                            uint64_t t305 = (uintptr_t)t304;
                            uint64_t t306 = GM_LD(uintptr_t, ((uint8_t *)&gm_l15) + 8);
                            if (t305 >= (uintptr_t)(t306)) gm_trap_index(t305, t306, gm_srckfibkpt681aa, 523, 63);
                            uint64_t t307 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l15)) + t305 * 8));
                            GM_ST(uint64_t, (uint8_t *)&t302 + 16, t307);
                            gm_b16_8 t308 = gm_l21;
                            GM_ST(gm_b16_8, (uint8_t *)&t302 + 24, t308);
                            gm_tabb710d9mq2e t309;
                            gm_f_format_1il5da4((void *)&t309, (void *)NULL, cx, NULL, &t302);
                            if (gm_unw.kind) {
                                goto gm_ll270;
                            }
                            gm_tabb710d9mq2e t310 = t309;
                            if (((((uint8_t *)&t310))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t310) + 8, gm_srckfibkpt681aa, 523, 28);
                            gm_b16_8 t311 = GM_LD(gm_b16_8, (((uint8_t *)&t310) + 8));
                            gm_b16_8 t312 = t311;
                            gm_b16_8 t313 = GM_LD(gm_b16_8, ((uint8_t *)&t312));
                            gm_tftb1odc739t5 t314 = gm_f_push_3ef746b(t301, t313);
                            gm_tftb1odc739t5 t315 = t314;
                            if (((((uint8_t *)&t315))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t315) + 1, gm_srckfibkpt681aa, 523, 17);
                            gm_b0_1 t316 = GM_LD(gm_b0_1, (((uint8_t *)&t315) + 1));
                        }
                        goto me279;
mn280:;
                        gm_trap("no match arm matched the scrutinee", gm_srckfibkpt681aa, 520, 13);
me279:;
                    }
lc266:;
                    {
                        uint64_t t318 = gm_l18;
                        uint64_t r319; bool o320 = __builtin_add_overflow(t318, ((uint64_t)1ULL), &r319);
                        uint64_t t321 = r319;
                        gm_l18 = t321;
                    }
                }
gm_ll270:;
                if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb266; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc266; }
                }
                goto gm_lad;
lb266:;
            }
        }
        {
            void * t322 = (void *)((uint8_t *)&F.l4);
            gm_b16_8 t323;
            GM_ST(void *, (uint8_t *)&t323, (void *)(gm_sc0vbtqan26pc));
            GM_ST(uintptr_t, (uint8_t *)&t323 + 8, (uintptr_t)(17));
            gm_tftb1odc739t5 t324 = gm_f_push_3ef746b(t322, t323);
            gm_tftb1odc739t5 t325 = t324;
            if (((((uint8_t *)&t325))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t325) + 1, gm_srckfibkpt681aa, 525, 13);
            gm_b0_1 t326 = GM_LD(gm_b0_1, (((uint8_t *)&t325) + 1));
        }
        {
            void * t327 = (void *)((uint8_t *)&F.l4);
            uint64_t t328 = GM_LD(uintptr_t, ((uint8_t *)&gm_l14) + 8);
            uint8_t t329 = (t328 > ((uint64_t)0ULL));
            gm_t9qkh0rsooc8d t330;
            GM_ST(uint8_t, ((uint8_t *)&t330 + 0), t329);
            gm_tabb710d9mq2e t331;
            gm_f_format_df9v4jq((void *)&t331, (void *)NULL, cx, NULL, t330);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t332 = t331;
            if (((((uint8_t *)&t332))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t332) + 8, gm_srckfibkpt681aa, 526, 20);
            gm_b16_8 t333 = GM_LD(gm_b16_8, (((uint8_t *)&t332) + 8));
            gm_b16_8 t334 = t333;
            gm_b16_8 t335 = GM_LD(gm_b16_8, ((uint8_t *)&t334));
            gm_tftb1odc739t5 t336 = gm_f_push_3ef746b(t327, t335);
            gm_tftb1odc739t5 t337 = t336;
            if (((((uint8_t *)&t337))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t337) + 1, gm_srckfibkpt681aa, 526, 13);
            gm_b0_1 t338 = GM_LD(gm_b0_1, (((uint8_t *)&t337) + 1));
        }
        {
            void * t339 = gm_l0;
            gm_b16_8 t340 = gm_l3;
            gm_b16_8 t341 = GM_LD(gm_b16_8, (((uint8_t *)&F.l16) + 0));
            gm_b16_8 t342 = t341;
            gm_b16_8 t343 = GM_LD(gm_b16_8, ((uint8_t *)&t342));
            void * t344 = (void *)((uint8_t *)&F.l4);
            gm_b0_1 t345 = gm_f_fields_array_tikg33t((void *)NULL, cx, NULL, t339, t340, t343, t344);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto me69;
mn256:;
ma78:;
        uint8_t t347 = GM_LD(uint8_t, (((uint8_t *)&t68) + 0));
        if (t347 != ((uint8_t)13ULL)) goto mn346;
        gm_t88rcucg1b7uk t348 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t68) + 8));
        gm_l23 = t348;
        {
            void * t350 = gm_l0;
            gm_t88rcucg1b7uk t351 = gm_l23;
            gm_b16_8 t352 = gm_f_desc_0cag2kq((void *)NULL, cx, NULL, t350, t351);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l24 = t352;
        }
        {
            void * t353 = (void *)((uint8_t *)&F.l4);
            gm_b16_8 t354;
            GM_ST(void *, (uint8_t *)&t354, (void *)(gm_s7qhcf46f682d));
            GM_ST(uintptr_t, (uint8_t *)&t354 + 8, (uintptr_t)(20));
            gm_tftb1odc739t5 t355 = gm_f_push_3ef746b(t353, t354);
            gm_tftb1odc739t5 t356 = t355;
            if (((((uint8_t *)&t356))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t356) + 1, gm_srckfibkpt681aa, 531, 13);
            gm_b0_1 t357 = GM_LD(gm_b0_1, (((uint8_t *)&t356) + 1));
        }
        {
            void * t358 = (void *)((uint8_t *)&F.l4);
            gm_b16_8 t359 = gm_l24;
            gm_tnpifv20irnph t360;
            GM_ST(gm_b16_8, ((uint8_t *)&t360 + 0), t359);
            gm_tabb710d9mq2e t361;
            gm_f_format_vsbf5l9((void *)&t361, (void *)NULL, cx, NULL, t360);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t362 = t361;
            if (((((uint8_t *)&t362))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t362) + 8, gm_srckfibkpt681aa, 532, 20);
            gm_b16_8 t363 = GM_LD(gm_b16_8, (((uint8_t *)&t362) + 8));
            gm_b16_8 t364 = t363;
            gm_b16_8 t365 = GM_LD(gm_b16_8, ((uint8_t *)&t364));
            gm_tftb1odc739t5 t366 = gm_f_push_3ef746b(t358, t365);
            gm_tftb1odc739t5 t367 = t366;
            if (((((uint8_t *)&t367))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t367) + 1, gm_srckfibkpt681aa, 532, 13);
            gm_b0_1 t368 = GM_LD(gm_b0_1, (((uint8_t *)&t367) + 1));
        }
        {
            void * t370 = gm_l0;
            void * t371 = GM_LD(void *, (((uint8_t *)t370) + 0));
            gm_t88rcucg1b7uk t372 = gm_l23;
            uint8_t t373 = gm_f_nullable_pointer_7qfmn6o(t371, GM_CONV(gm_csguankko4lmhd, t372));
            if (!t373) goto ie369;
            {
                void * t375 = (void *)((uint8_t *)&F.l4);
                gm_b16_8 t376;
                GM_ST(void *, (uint8_t *)&t376, (void *)(gm_sbec11r2rv4ti));
                GM_ST(uintptr_t, (uint8_t *)&t376 + 8, (uintptr_t)(22));
                gm_tftb1odc739t5 t377 = gm_f_push_3ef746b(t375, t376);
                gm_tftb1odc739t5 t378 = t377;
                if (((((uint8_t *)&t378))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t378) + 1, gm_srckfibkpt681aa, 534, 17);
                gm_b0_1 t379 = GM_LD(gm_b0_1, (((uint8_t *)&t378) + 1));
            }
            goto ix369;
ie369:;
            {
                void * t381 = (void *)((uint8_t *)&F.l4);
                uint64_t t382 = (uintptr_t)((uint64_t)1ULL);
                void * t383 = gm_l0;
                gm_t88rcucg1b7uk t384 = gm_l1;
                gm_b16_8 t385 = gm_f_offsets_eh6vfle((void *)NULL, cx, NULL, t383, t384);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_b16_8 t386 = t385;
                uint64_t t387 = GM_LD(uintptr_t, ((uint8_t *)&t386) + 8);
                if (t382 >= (uintptr_t)(t387)) gm_trap_index(t382, t387, gm_srckfibkpt681aa, 536, 55);
                uint64_t t388 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&t386)) + t382 * 8));
                gm_t4ce0jd3tdro4 t389;
                GM_ST(uint64_t, ((uint8_t *)&t389 + 0), t388);
                gm_tabb710d9mq2e t390;
                gm_f_format_9117rt8((void *)&t390, (void *)NULL, cx, NULL, t389);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t391 = t390;
                if (((((uint8_t *)&t391))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t391) + 8, gm_srckfibkpt681aa, 536, 24);
                gm_b16_8 t392 = GM_LD(gm_b16_8, (((uint8_t *)&t391) + 8));
                gm_b16_8 t393 = t392;
                gm_b16_8 t394 = GM_LD(gm_b16_8, ((uint8_t *)&t393));
                gm_tftb1odc739t5 t395 = gm_f_push_3ef746b(t381, t394);
                gm_tftb1odc739t5 t396 = t395;
                if (((((uint8_t *)&t396))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t396) + 1, gm_srckfibkpt681aa, 536, 17);
                gm_b0_1 t397 = GM_LD(gm_b0_1, (((uint8_t *)&t396) + 1));
            }
ix369:;
        }
        goto me69;
mn346:;
ma79:;
        uint8_t t399 = GM_LD(uint8_t, (((uint8_t *)&t68) + 0));
        if (t399 != ((uint8_t)14ULL)) goto mn398;
        gm_t88rcucg1b7uk t400 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t68) + 8) + 0));
        gm_l25 = t400;
        gm_t88rcucg1b7uk t401 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t68) + 8) + 4));
        gm_l26 = t401;
        {
            void * t403 = gm_l0;
            gm_t88rcucg1b7uk t404 = gm_l25;
            gm_b16_8 t405 = gm_f_desc_0cag2kq((void *)NULL, cx, NULL, t403, t404);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l27 = t405;
        }
        {
            void * t406 = gm_l0;
            gm_t88rcucg1b7uk t407 = gm_l26;
            gm_b16_8 t408 = gm_f_desc_0cag2kq((void *)NULL, cx, NULL, t406, t407);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l28 = t408;
        }
        {
            void * t409 = gm_l0;
            gm_t88rcucg1b7uk t410 = gm_l1;
            gm_b16_8 t411 = gm_f_offsets_eh6vfle((void *)NULL, cx, NULL, t409, t410);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l29 = t411;
        }
        {
            void * t412 = (void *)((uint8_t *)&F.l4);
            gm_b16_8 t413;
            GM_ST(void *, (uint8_t *)&t413, (void *)(gm_shrq42dntcbrd));
            GM_ST(uintptr_t, (uint8_t *)&t413 + 8, (uintptr_t)(18));
            gm_tftb1odc739t5 t414 = gm_f_push_3ef746b(t412, t413);
            gm_tftb1odc739t5 t415 = t414;
            if (((((uint8_t *)&t415))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t415) + 1, gm_srckfibkpt681aa, 543, 13);
            gm_b0_1 t416 = GM_LD(gm_b0_1, (((uint8_t *)&t415) + 1));
        }
        {
            void * t417 = (void *)((uint8_t *)&F.l4);
            gm_b16_8 t418 = gm_l28;
            gm_tnpifv20irnph t419;
            GM_ST(gm_b16_8, ((uint8_t *)&t419 + 0), t418);
            gm_tabb710d9mq2e t420;
            gm_f_format_vsbf5l9((void *)&t420, (void *)NULL, cx, NULL, t419);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t421 = t420;
            if (((((uint8_t *)&t421))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t421) + 8, gm_srckfibkpt681aa, 544, 20);
            gm_b16_8 t422 = GM_LD(gm_b16_8, (((uint8_t *)&t421) + 8));
            gm_b16_8 t423 = t422;
            gm_b16_8 t424 = GM_LD(gm_b16_8, ((uint8_t *)&t423));
            gm_tftb1odc739t5 t425 = gm_f_push_3ef746b(t417, t424);
            gm_tftb1odc739t5 t426 = t425;
            if (((((uint8_t *)&t426))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t426) + 1, gm_srckfibkpt681aa, 544, 13);
            gm_b0_1 t427 = GM_LD(gm_b0_1, (((uint8_t *)&t426) + 1));
        }
        {
            void * t428 = (void *)((uint8_t *)&F.l4);
            gm_b16_8 t429 = gm_l27;
            gm_tnpifv20irnph t430;
            GM_ST(gm_b16_8, ((uint8_t *)&t430 + 0), t429);
            gm_tabb710d9mq2e t431;
            gm_f_format_s44fp1q((void *)&t431, (void *)NULL, cx, NULL, t430);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t432 = t431;
            if (((((uint8_t *)&t432))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t432) + 8, gm_srckfibkpt681aa, 545, 20);
            gm_b16_8 t433 = GM_LD(gm_b16_8, (((uint8_t *)&t432) + 8));
            gm_b16_8 t434 = t433;
            gm_b16_8 t435 = GM_LD(gm_b16_8, ((uint8_t *)&t434));
            gm_tftb1odc739t5 t436 = gm_f_push_3ef746b(t428, t435);
            gm_tftb1odc739t5 t437 = t436;
            if (((((uint8_t *)&t437))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t437) + 1, gm_srckfibkpt681aa, 545, 13);
            gm_b0_1 t438 = GM_LD(gm_b0_1, (((uint8_t *)&t437) + 1));
        }
        {
            void * t439 = (void *)((uint8_t *)&F.l4);
            uint64_t t440 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t441 = GM_LD(uintptr_t, ((uint8_t *)&gm_l29) + 8);
            if (t440 >= (uintptr_t)(t441)) gm_trap_index(t440, t441, gm_srckfibkpt681aa, 546, 50);
            uint64_t t442 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l29)) + t440 * 8));
            gm_t4ce0jd3tdro4 t443;
            GM_ST(uint64_t, ((uint8_t *)&t443 + 0), t442);
            gm_tabb710d9mq2e t444;
            gm_f_format_4v9fh5b((void *)&t444, (void *)NULL, cx, NULL, t443);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t445 = t444;
            if (((((uint8_t *)&t445))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t445) + 8, gm_srckfibkpt681aa, 546, 20);
            gm_b16_8 t446 = GM_LD(gm_b16_8, (((uint8_t *)&t445) + 8));
            gm_b16_8 t447 = t446;
            gm_b16_8 t448 = GM_LD(gm_b16_8, ((uint8_t *)&t447));
            gm_tftb1odc739t5 t449 = gm_f_push_3ef746b(t439, t448);
            gm_tftb1odc739t5 t450 = t449;
            if (((((uint8_t *)&t450))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t450) + 1, gm_srckfibkpt681aa, 546, 13);
            gm_b0_1 t451 = GM_LD(gm_b0_1, (((uint8_t *)&t450) + 1));
        }
        {
            void * t452 = (void *)((uint8_t *)&F.l4);
            uint64_t t453 = (uintptr_t)((uint64_t)1ULL);
            uint64_t t454 = GM_LD(uintptr_t, ((uint8_t *)&gm_l29) + 8);
            if (t453 >= (uintptr_t)(t454)) gm_trap_index(t453, t454, gm_srckfibkpt681aa, 547, 54);
            uint64_t t455 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l29)) + t453 * 8));
            gm_t4ce0jd3tdro4 t456;
            GM_ST(uint64_t, ((uint8_t *)&t456 + 0), t455);
            gm_tabb710d9mq2e t457;
            gm_f_format_io38sjt((void *)&t457, (void *)NULL, cx, NULL, t456);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t458 = t457;
            if (((((uint8_t *)&t458))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t458) + 8, gm_srckfibkpt681aa, 547, 20);
            gm_b16_8 t459 = GM_LD(gm_b16_8, (((uint8_t *)&t458) + 8));
            gm_b16_8 t460 = t459;
            gm_b16_8 t461 = GM_LD(gm_b16_8, ((uint8_t *)&t460));
            gm_tftb1odc739t5 t462 = gm_f_push_3ef746b(t452, t461);
            gm_tftb1odc739t5 t463 = t462;
            if (((((uint8_t *)&t463))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t463) + 1, gm_srckfibkpt681aa, 547, 13);
            gm_b0_1 t464 = GM_LD(gm_b0_1, (((uint8_t *)&t463) + 1));
        }
        goto me69;
mn398:;
ma80:;
        uint8_t t466 = GM_LD(uint8_t, (((uint8_t *)&t68) + 0));
        if (t466 != ((uint8_t)17ULL)) goto mn465;
        uint32_t t467 = GM_LD(uint32_t, ((((uint8_t *)&t68) + 8) + 0));
        gm_l30 = t467;
        {
            uint32_t t469 = gm_l30;
            uint64_t t470 = (uint64_t)t469;
            uint64_t t471 = (uintptr_t)t470;
            void * t472 = gm_l0;
            void * t473 = GM_LD(void *, (((uint8_t *)t472) + 0));
            uint64_t t474 = GM_LD(uintptr_t, ((((uint8_t *)t473) + 1088) + 0) + 8);
            if (t471 >= (uintptr_t)(t474)) gm_trap_index(t471, t474, gm_srckfibkpt681aa, 550, 21);
            void * t475 = (void *)((uint8_t *)GM_LD(void *, ((((uint8_t *)t473) + 1088) + 0)) + t471 * 184);
            gm_l31 = t475;
        }
        {
            void * t476 = (void *)((uint8_t *)&F.l4);
            void * t477 = gm_l31;
            gm_b16_8 t478 = GM_LD(gm_b16_8, (((uint8_t *)t477) + 0));
            gm_b16_8 t479 = gm_f_c_string_n7h27b3((void *)NULL, cx, NULL, t478);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tnpifv20irnph t480;
            GM_ST(gm_b16_8, ((uint8_t *)&t480 + 0), t479);
            gm_tabb710d9mq2e t481;
            gm_f_format_nbg44fl((void *)&t481, (void *)NULL, cx, NULL, t480);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t482 = t481;
            if (((((uint8_t *)&t482))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t482) + 8, gm_srckfibkpt681aa, 551, 20);
            gm_b16_8 t483 = GM_LD(gm_b16_8, (((uint8_t *)&t482) + 8));
            gm_b16_8 t484 = t483;
            gm_b16_8 t485 = GM_LD(gm_b16_8, ((uint8_t *)&t484));
            gm_tftb1odc739t5 t486 = gm_f_push_3ef746b(t476, t485);
            gm_tftb1odc739t5 t487 = t486;
            if (((((uint8_t *)&t487))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t487) + 1, gm_srckfibkpt681aa, 551, 13);
            gm_b0_1 t488 = GM_LD(gm_b0_1, (((uint8_t *)&t487) + 1));
        }
        {
            void * t489 = gm_l31;
            uint8_t t495 = GM_LD(uint8_t, ((((uint8_t *)t489) + 24) + 0));
            switch (t495) { case ((uint8_t)0ULL): goto ma491; case ((uint8_t)1ULL): goto ma492; case ((uint8_t)2ULL): goto ma493; default: goto ma494; }
ma491:;
            uint8_t t497 = GM_LD(uint8_t, ((((uint8_t *)t489) + 24) + 0));
            if (t497 != ((uint8_t)0ULL)) goto mn496;
            gm_tpautr4br7h3l t498 = GM_LD(gm_tpautr4br7h3l, ((((uint8_t *)t489) + 24) + 8));
            F.l32 = t498;
            {
                uint8_t t501;
                if ((((((uint8_t *)&F.l32) + 40))[4] != 0)) goto mn503;
                t501 = ((uint8_t)1);
                goto me502;
mn503:;
                t501 = ((uint8_t)0);
                goto me502;
mn504:;
                gm_trap("no match arm matched the scrutinee", gm_srckfibkpt681aa, 554, 24);
me502:;
                if (!t501) goto ie500;
                {
                    void * t506 = gm_l0;
                    gm_t88rcucg1b7uk t507 = gm_l1;
                    gm_b16_8 t508 = gm_f_offsets_eh6vfle((void *)NULL, cx, NULL, t506, t507);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l33 = t508;
                }
                {
                    gm_f_list_epvducg((void *)&F.l34, (void *)NULL, cx, NULL);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                {
                    {
                        gm_b16_8 t510 = GM_LD(gm_b16_8, (((uint8_t *)&F.l32) + 0));
                        gm_b16_8 t511 = t510;
                        gm_b16_8 t512 = GM_LD(gm_b16_8, ((uint8_t *)&t511));
                        gm_l35 = t512;
                    }
                    {
                        gm_l36 = ((uint64_t)0ULL);
                    }
                    {
                        for (;;) {
                            uint64_t t514 = gm_l36;
                            uint64_t t515 = GM_LD(uintptr_t, ((uint8_t *)&gm_l35) + 8);
                            uint8_t t516 = (t514 < t515);
                            if (!t516) goto lb513;
                            {
                                gm_t8ta482bl6be0 t519;
                                uint64_t t520 = gm_l36;
                                GM_ST(uint64_t, (uint8_t *)&t519 + 0, t520);
                                uint64_t t521 = gm_l36;
                                uint64_t t522 = (uintptr_t)t521;
                                uint64_t t523 = GM_LD(uintptr_t, ((uint8_t *)&gm_l35) + 8);
                                memcpy((uint8_t *)&t519 + 8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l35)) + t522 * 40), 40);
                                gm_t8ta482bl6be0 t524 = t519;
                                uint64_t t527 = GM_LD(uint64_t, (((uint8_t *)&t524) + 0));
                                gm_l37 = t527;
                                gm_t0mglksrkfa5e t528 = GM_LD(gm_t0mglksrkfa5e, (((uint8_t *)&t524) + 8));
                                F.l38 = t528;
                                {
                                    void * t530 = gm_l0;
                                    gm_t88rcucg1b7uk t531 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l38) + 16));
                                    gm_b16_8 t532 = gm_f_desc_0cag2kq((void *)NULL, cx, NULL, t530, t531);
                                    if (gm_unw.kind) {
                                        goto gm_ll517;
                                    }
                                    gm_l39 = t532;
                                }
                                {
                                    void * t533 = (void *)((uint8_t *)&F.l34);
                                    gm_tq3m5qgvkpje8 t534;
                                    gm_b16_8 t535 = GM_LD(gm_b16_8, (((uint8_t *)&F.l38) + 0));
                                    gm_b16_8 t536 = gm_f_c_string_n7h27b3((void *)NULL, cx, NULL, t535);
                                    if (gm_unw.kind) {
                                        goto gm_ll517;
                                    }
                                    GM_ST(gm_b16_8, (uint8_t *)&t534 + 0, t536);
                                    uint64_t t537 = gm_l37;
                                    uint64_t t538 = (uintptr_t)t537;
                                    uint64_t t539 = GM_LD(uintptr_t, ((uint8_t *)&gm_l33) + 8);
                                    if (t538 >= (uintptr_t)(t539)) gm_trap_index(t538, t539, gm_srckfibkpt681aa, 559, 88);
                                    uint64_t t540 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l33)) + t538 * 8));
                                    GM_ST(uint64_t, (uint8_t *)&t534 + 16, t540);
                                    gm_b16_8 t541 = gm_l39;
                                    GM_ST(gm_b16_8, (uint8_t *)&t534 + 24, t541);
                                    gm_tabb710d9mq2e t542;
                                    gm_f_format_1il5da4((void *)&t542, (void *)NULL, cx, NULL, &t534);
                                    if (gm_unw.kind) {
                                        goto gm_ll517;
                                    }
                                    gm_tabb710d9mq2e t543 = t542;
                                    if (((((uint8_t *)&t543))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t543) + 8, gm_srckfibkpt681aa, 559, 40);
                                    gm_b16_8 t544 = GM_LD(gm_b16_8, (((uint8_t *)&t543) + 8));
                                    gm_b16_8 t545 = t544;
                                    gm_b16_8 t546 = GM_LD(gm_b16_8, ((uint8_t *)&t545));
                                    gm_tftb1odc739t5 t547 = gm_f_push_3ef746b(t533, t546);
                                    gm_tftb1odc739t5 t548 = t547;
                                    if (((((uint8_t *)&t548))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t548) + 1, gm_srckfibkpt681aa, 559, 29);
                                    gm_b0_1 t549 = GM_LD(gm_b0_1, (((uint8_t *)&t548) + 1));
                                }
                                goto me525;
mn526:;
                                gm_trap("no match arm matched the scrutinee", gm_srckfibkpt681aa, 557, 25);
me525:;
                            }
lc513:;
                            {
                                uint64_t t551 = gm_l36;
                                uint64_t r552; bool o553 = __builtin_add_overflow(t551, ((uint64_t)1ULL), &r552);
                                uint64_t t554 = r552;
                                gm_l36 = t554;
                            }
                        }
gm_ll517:;
                        if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb513; }
                            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc513; }
                        }
                        goto gm_lad;
lb513:;
                    }
                }
                {
                    void * t555 = (void *)((uint8_t *)&F.l4);
                    gm_b16_8 t556;
                    GM_ST(void *, (uint8_t *)&t556, (void *)(gm_slad86501rp5c));
                    GM_ST(uintptr_t, (uint8_t *)&t556 + 8, (uintptr_t)(18));
                    gm_tftb1odc739t5 t557 = gm_f_push_3ef746b(t555, t556);
                    gm_tftb1odc739t5 t558 = t557;
                    if (((((uint8_t *)&t558))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t558) + 1, gm_srckfibkpt681aa, 561, 25);
                    gm_b0_1 t559 = GM_LD(gm_b0_1, (((uint8_t *)&t558) + 1));
                }
                {
                    void * t560 = gm_l0;
                    gm_b16_8 t561 = gm_l3;
                    gm_b16_8 t562 = GM_LD(gm_b16_8, (((uint8_t *)&F.l34) + 0));
                    gm_b16_8 t563 = t562;
                    gm_b16_8 t564 = GM_LD(gm_b16_8, ((uint8_t *)&t563));
                    void * t565 = (void *)((uint8_t *)&F.l4);
                    gm_b0_1 t566 = gm_f_fields_array_tikg33t((void *)NULL, cx, NULL, t560, t561, t564, t565);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                goto ix500;
ie500:;
                {
                    void * t568 = gm_l0;
                    gm_t88rcucg1b7uk t569 = gm_l1;
                    gm_t57euh612ligr t570;
                    gm_f_bits_layout_23rect9((void *)&t570, (void *)NULL, cx, NULL, t568, t569);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_t57euh612ligr t571 = t570;
                    if (!((((uint8_t *)&t571))[24] != 0)) gm_trap("unwrap of null", gm_srckfibkpt681aa, 564, 31);
                    gm_tcmkompbl09il t572 = GM_LD(gm_tcmkompbl09il, ((uint8_t *)&t571));
                    gm_l40 = t572;
                }
                {
                    gm_f_list_epvducg((void *)&F.l41, (void *)NULL, cx, NULL);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                {
                    {
                        gm_b16_8 t574 = GM_LD(gm_b16_8, (((uint8_t *)&F.l32) + 0));
                        gm_b16_8 t575 = t574;
                        gm_b16_8 t576 = GM_LD(gm_b16_8, ((uint8_t *)&t575));
                        gm_l42 = t576;
                    }
                    {
                        gm_l43 = ((uint64_t)0ULL);
                    }
                    {
                        for (;;) {
                            uint64_t t578 = gm_l43;
                            uint64_t t579 = GM_LD(uintptr_t, ((uint8_t *)&gm_l42) + 8);
                            uint8_t t580 = (t578 < t579);
                            if (!t580) goto lb577;
                            {
                                gm_t8ta482bl6be0 t583;
                                uint64_t t584 = gm_l43;
                                GM_ST(uint64_t, (uint8_t *)&t583 + 0, t584);
                                uint64_t t585 = gm_l43;
                                uint64_t t586 = (uintptr_t)t585;
                                uint64_t t587 = GM_LD(uintptr_t, ((uint8_t *)&gm_l42) + 8);
                                memcpy((uint8_t *)&t583 + 8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l42)) + t586 * 40), 40);
                                gm_t8ta482bl6be0 t588 = t583;
                                uint64_t t591 = GM_LD(uint64_t, (((uint8_t *)&t588) + 0));
                                gm_l44 = t591;
                                gm_t0mglksrkfa5e t592 = GM_LD(gm_t0mglksrkfa5e, (((uint8_t *)&t588) + 8));
                                F.l45 = t592;
                                {
                                    uint64_t t594 = gm_l44;
                                    uint64_t t595 = (uintptr_t)t594;
                                    uint64_t t596 = GM_LD(uintptr_t, (((uint8_t *)&gm_l40) + 8) + 8);
                                    if (t595 >= (uintptr_t)(t596)) gm_trap_index(t595, t596, gm_srckfibkpt681aa, 567, 35);
                                    gm_tov4mtrfp5e7v t597 = GM_LD(gm_tov4mtrfp5e7v, ((uint8_t *)GM_LD(void *, (((uint8_t *)&gm_l40) + 8)) + t595 * 12));
                                    gm_l46 = t597;
                                }
                                {
                                    void * t598 = gm_l0;
                                    gm_t88rcucg1b7uk t599 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l46) + 8));
                                    gm_b16_8 t600 = gm_f_desc_0cag2kq((void *)NULL, cx, NULL, t598, t599);
                                    if (gm_unw.kind) {
                                        goto gm_ll581;
                                    }
                                    gm_l47 = t600;
                                }
                                {
                                    void * t601 = (void *)((uint8_t *)&F.l41);
                                    gm_temv198mna2jp t602;
                                    memset((uint8_t *)&t602 + 20, 0, 4);
                                    gm_b16_8 t603 = GM_LD(gm_b16_8, (((uint8_t *)&F.l45) + 0));
                                    gm_b16_8 t604 = gm_f_c_string_n7h27b3((void *)NULL, cx, NULL, t603);
                                    if (gm_unw.kind) {
                                        goto gm_ll581;
                                    }
                                    GM_ST(gm_b16_8, (uint8_t *)&t602 + 0, t604);
                                    uint32_t t605 = GM_LD(uint32_t, (((uint8_t *)&gm_l46) + 0));
                                    GM_ST(uint32_t, (uint8_t *)&t602 + 16, t605);
                                    gm_b16_8 t606 = gm_l47;
                                    GM_ST(gm_b16_8, (uint8_t *)&t602 + 24, t606);
                                    gm_tabb710d9mq2e t607;
                                    gm_f_format_n3gtmrf((void *)&t607, (void *)NULL, cx, NULL, &t602);
                                    if (gm_unw.kind) {
                                        goto gm_ll581;
                                    }
                                    gm_tabb710d9mq2e t608 = t607;
                                    if (((((uint8_t *)&t608))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t608) + 8, gm_srckfibkpt681aa, 569, 40);
                                    gm_b16_8 t609 = GM_LD(gm_b16_8, (((uint8_t *)&t608) + 8));
                                    gm_b16_8 t610 = t609;
                                    gm_b16_8 t611 = GM_LD(gm_b16_8, ((uint8_t *)&t610));
                                    gm_tftb1odc739t5 t612 = gm_f_push_3ef746b(t601, t611);
                                    gm_tftb1odc739t5 t613 = t612;
                                    if (((((uint8_t *)&t613))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t613) + 1, gm_srckfibkpt681aa, 569, 29);
                                    gm_b0_1 t614 = GM_LD(gm_b0_1, (((uint8_t *)&t613) + 1));
                                }
                                goto me589;
mn590:;
                                gm_trap("no match arm matched the scrutinee", gm_srckfibkpt681aa, 566, 25);
me589:;
                            }
lc577:;
                            {
                                uint64_t t616 = gm_l43;
                                uint64_t r617; bool o618 = __builtin_add_overflow(t616, ((uint64_t)1ULL), &r617);
                                uint64_t t619 = r617;
                                gm_l43 = t619;
                            }
                        }
gm_ll581:;
                        if (gm_unw.frame == &F.h && gm_unw.label == 2) {
                            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb577; }
                            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc577; }
                        }
                        goto gm_lad;
lb577:;
                    }
                }
                {
                    void * t620 = (void *)((uint8_t *)&F.l4);
                    gm_b16_8 t621;
                    GM_ST(void *, (uint8_t *)&t621, (void *)(gm_s76d71nockpip));
                    GM_ST(uintptr_t, (uint8_t *)&t621 + 8, (uintptr_t)(16));
                    gm_tftb1odc739t5 t622 = gm_f_push_3ef746b(t620, t621);
                    gm_tftb1odc739t5 t623 = t622;
                    if (((((uint8_t *)&t623))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t623) + 1, gm_srckfibkpt681aa, 571, 25);
                    gm_b0_1 t624 = GM_LD(gm_b0_1, (((uint8_t *)&t623) + 1));
                }
                {
                    void * t625 = (void *)((uint8_t *)&F.l4);
                    void * t626 = gm_l0;
                    gm_t88rcucg1b7uk t627 = gm_l1;
                    uint32_t t628 = gm_f_bit_width_u86m8ir((void *)NULL, cx, NULL, t626, t627);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_t88rcucg1b7uk t629;
                    GM_ST(uint32_t, ((uint8_t *)&t629 + 0), t628);
                    gm_tabb710d9mq2e t630;
                    gm_f_format_ch41dev((void *)&t630, (void *)NULL, cx, NULL, t629);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tabb710d9mq2e t631 = t630;
                    if (((((uint8_t *)&t631))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t631) + 8, gm_srckfibkpt681aa, 572, 32);
                    gm_b16_8 t632 = GM_LD(gm_b16_8, (((uint8_t *)&t631) + 8));
                    gm_b16_8 t633 = t632;
                    gm_b16_8 t634 = GM_LD(gm_b16_8, ((uint8_t *)&t633));
                    gm_tftb1odc739t5 t635 = gm_f_push_3ef746b(t625, t634);
                    gm_tftb1odc739t5 t636 = t635;
                    if (((((uint8_t *)&t636))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t636) + 1, gm_srckfibkpt681aa, 572, 25);
                    gm_b0_1 t637 = GM_LD(gm_b0_1, (((uint8_t *)&t636) + 1));
                }
                {
                    void * t638 = gm_l0;
                    gm_b16_8 t639 = gm_l3;
                    gm_b16_8 t640 = GM_LD(gm_b16_8, (((uint8_t *)&F.l41) + 0));
                    gm_b16_8 t641 = t640;
                    gm_b16_8 t642 = GM_LD(gm_b16_8, ((uint8_t *)&t641));
                    void * t643 = (void *)((uint8_t *)&F.l4);
                    gm_b0_1 t644 = gm_f_fields_array_tikg33t((void *)NULL, cx, NULL, t638, t639, t642, t643);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
ix500:;
            }
            goto me490;
mn496:;
ma492:;
            uint8_t t646 = GM_LD(uint8_t, ((((uint8_t *)t489) + 24) + 0));
            if (t646 != ((uint8_t)1ULL)) goto mn645;
            gm_t88rcucg1b7uk t647 = GM_LD(gm_t88rcucg1b7uk, (((((uint8_t *)t489) + 24) + 8) + 0));
            gm_l48 = t647;
            gm_b16_8 t648 = GM_LD(gm_b16_8, (((((uint8_t *)t489) + 24) + 8) + 8));
            gm_l49 = t648;
            {
                void * t650 = gm_l0;
                gm_t88rcucg1b7uk t651 = gm_l1;
                gm_b16_8 t652 = gm_f_offsets_eh6vfle((void *)NULL, cx, NULL, t650, t651);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l50 = t652;
            }
            {
                gm_f_list_epvducg((void *)&F.l51, (void *)NULL, cx, NULL);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                {
                    gm_b16_8 t654 = gm_l49;
                    gm_l52 = t654;
                }
                {
                    gm_l53 = ((uint64_t)0ULL);
                }
                {
                    for (;;) {
                        uint64_t t656 = gm_l53;
                        uint64_t t657 = GM_LD(uintptr_t, ((uint8_t *)&gm_l52) + 8);
                        uint8_t t658 = (t656 < t657);
                        if (!t658) goto lb655;
                        {
                            uint64_t t661 = gm_l53;
                            uint64_t t662 = (uintptr_t)t661;
                            uint64_t t663 = GM_LD(uintptr_t, ((uint8_t *)&gm_l52) + 8);
                            gm_tckdobunaulee t666 = GM_LD(gm_tckdobunaulee, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l52)) + t662 * 72));
                            F.l54 = t666;
                            {
                                gm_b16_8 t668;
                                if (!(((((uint8_t *)&F.l54) + 16))[4] != 0)) goto ie669;
                                gm_t88rcucg1b7uk t670 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l54) + 16));
                                gm_l55 = t670;
                                void * t672 = gm_l0;
                                gm_t88rcucg1b7uk t673 = gm_l55;
                                gm_b16_8 t674 = gm_f_desc_0cag2kq((void *)NULL, cx, NULL, t672, t673);
                                if (gm_unw.kind) {
                                    goto gm_ll659;
                                }
                                gm_b16_8 t675 = t674;
                                t668 = t675;
                                goto ix669;
ie669:;
                                gm_b16_8 t677;
                                GM_ST(void *, (uint8_t *)&t677, (void *)(gm_s04qus0luoga5));
                                GM_ST(uintptr_t, (uint8_t *)&t677 + 8, (uintptr_t)(4));
                                gm_b16_8 t678 = t677;
                                t668 = t678;
ix669:;
                                gm_l56 = t668;
                            }
                            {
                                void * t679 = (void *)((uint8_t *)&F.l51);
                                gm_tfeab279c16rr t680;
                                gm_b16_8 t681 = GM_LD(gm_b16_8, (((uint8_t *)&F.l54) + 0));
                                gm_b16_8 t682 = gm_f_c_string_n7h27b3((void *)NULL, cx, NULL, t681);
                                if (gm_unw.kind) {
                                    goto gm_ll659;
                                }
                                GM_ST(gm_b16_8, (uint8_t *)&t680 + 0, t682);
                                gm_b16_8 t683 = gm_f_to_decimal_8bshn5b((void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)(((uint8_t *)&F.l54) + 24)));
                                if (gm_unw.kind) {
                                    goto gm_ll659;
                                }
                                GM_ST(gm_b16_8, (uint8_t *)&t680 + 16, t683);
                                gm_b16_8 t684 = gm_l56;
                                GM_ST(gm_b16_8, (uint8_t *)&t680 + 32, t684);
                                gm_tabb710d9mq2e t685;
                                gm_f_format_aqb9tiu((void *)&t685, (void *)NULL, cx, NULL, &t680);
                                if (gm_unw.kind) {
                                    goto gm_ll659;
                                }
                                gm_tabb710d9mq2e t686 = t685;
                                if (((((uint8_t *)&t686))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t686) + 8, gm_srckfibkpt681aa, 581, 36);
                                gm_b16_8 t687 = GM_LD(gm_b16_8, (((uint8_t *)&t686) + 8));
                                gm_b16_8 t688 = t687;
                                gm_b16_8 t689 = GM_LD(gm_b16_8, ((uint8_t *)&t688));
                                gm_tftb1odc739t5 t690 = gm_f_push_3ef746b(t679, t689);
                                gm_tftb1odc739t5 t691 = t690;
                                if (((((uint8_t *)&t691))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t691) + 1, gm_srckfibkpt681aa, 581, 25);
                                gm_b0_1 t692 = GM_LD(gm_b0_1, (((uint8_t *)&t691) + 1));
                            }
                            goto me664;
mn665:;
                            gm_trap("no match arm matched the scrutinee", gm_srckfibkpt681aa, 579, 21);
me664:;
                        }
lc655:;
                        {
                            uint64_t t694 = gm_l53;
                            uint64_t r695; bool o696 = __builtin_add_overflow(t694, ((uint64_t)1ULL), &r695);
                            uint64_t t697 = r695;
                            gm_l53 = t697;
                        }
                    }
gm_ll659:;
                    if (gm_unw.frame == &F.h && gm_unw.label == 3) {
                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb655; }
                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc655; }
                    }
                    goto gm_lad;
lb655:;
                }
            }
            {
                void * t698 = gm_l0;
                void * t699 = (void *)(((uint8_t *)t698) + 328);
                gm_t6j6smj7qoctu t700;
                gm_b16_8 t701 = gm_l3;
                GM_ST(gm_b16_8, (uint8_t *)&t700 + 0, t701);
                gm_b16_8 t702 = GM_LD(gm_b16_8, (((uint8_t *)&F.l51) + 0));
                gm_b16_8 t703 = t702;
                gm_b16_8 t704 = GM_LD(gm_b16_8, ((uint8_t *)&t703));
                gm_b16_8 t705;
                GM_ST(void *, (uint8_t *)&t705, (void *)(gm_sg5gdnrith2i6));
                GM_ST(uintptr_t, (uint8_t *)&t705 + 8, (uintptr_t)(2));
                gm_tabb710d9mq2e t706;
                gm_f_join_sfknljb((void *)&t706, (void *)NULL, cx, NULL, t704, t705);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t707 = t706;
                if (((((uint8_t *)&t707))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t707) + 8, gm_srckfibkpt681aa, 583, 90);
                gm_b16_8 t708 = GM_LD(gm_b16_8, (((uint8_t *)&t707) + 8));
                GM_ST(gm_b16_8, (uint8_t *)&t700 + 16, t708);
                gm_tftb1odc739t5 t709 = gm_f_write_r7au1ks(t699, &t700);
                gm_tftb1odc739t5 t710 = t709;
                if (((((uint8_t *)&t710))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t710) + 1, gm_srckfibkpt681aa, 583, 21);
                gm_b0_1 t711 = GM_LD(gm_b0_1, (((uint8_t *)&t710) + 1));
            }
            {
                void * t712 = (void *)((uint8_t *)&F.l4);
                gm_b16_8 t713;
                GM_ST(void *, (uint8_t *)&t713, (void *)(gm_s58f7dj21gtf9));
                GM_ST(uintptr_t, (uint8_t *)&t713 + 8, (uintptr_t)(17));
                gm_tftb1odc739t5 t714 = gm_f_push_3ef746b(t712, t713);
                gm_tftb1odc739t5 t715 = t714;
                if (((((uint8_t *)&t715))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t715) + 1, gm_srckfibkpt681aa, 584, 21);
                gm_b0_1 t716 = GM_LD(gm_b0_1, (((uint8_t *)&t715) + 1));
            }
            {
                void * t717 = (void *)((uint8_t *)&F.l4);
                uint64_t t718 = GM_LD(uintptr_t, ((uint8_t *)&gm_l49) + 8);
                gm_t4ce0jd3tdro4 t719;
                GM_ST(uint64_t, ((uint8_t *)&t719 + 0), t718);
                gm_tabb710d9mq2e t720;
                gm_f_format_ktg7ckd((void *)&t720, (void *)NULL, cx, NULL, t719);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t721 = t720;
                if (((((uint8_t *)&t721))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t721) + 8, gm_srckfibkpt681aa, 585, 28);
                gm_b16_8 t722 = GM_LD(gm_b16_8, (((uint8_t *)&t721) + 8));
                gm_b16_8 t723 = t722;
                gm_b16_8 t724 = GM_LD(gm_b16_8, ((uint8_t *)&t723));
                gm_tftb1odc739t5 t725 = gm_f_push_3ef746b(t717, t724);
                gm_tftb1odc739t5 t726 = t725;
                if (((((uint8_t *)&t726))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t726) + 1, gm_srckfibkpt681aa, 585, 21);
                gm_b0_1 t727 = GM_LD(gm_b0_1, (((uint8_t *)&t726) + 1));
            }
            {
                void * t728 = (void *)((uint8_t *)&F.l4);
                gm_b16_8 t729 = gm_l3;
                gm_tnpifv20irnph t730;
                GM_ST(gm_b16_8, ((uint8_t *)&t730 + 0), t729);
                gm_tabb710d9mq2e t731;
                gm_f_format_ka5ced8((void *)&t731, (void *)NULL, cx, NULL, t730);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t732 = t731;
                if (((((uint8_t *)&t732))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t732) + 8, gm_srckfibkpt681aa, 586, 28);
                gm_b16_8 t733 = GM_LD(gm_b16_8, (((uint8_t *)&t732) + 8));
                gm_b16_8 t734 = t733;
                gm_b16_8 t735 = GM_LD(gm_b16_8, ((uint8_t *)&t734));
                gm_tftb1odc739t5 t736 = gm_f_push_3ef746b(t728, t735);
                gm_tftb1odc739t5 t737 = t736;
                if (((((uint8_t *)&t737))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t737) + 1, gm_srckfibkpt681aa, 586, 21);
                gm_b0_1 t738 = GM_LD(gm_b0_1, (((uint8_t *)&t737) + 1));
            }
            {
                void * t739 = (void *)((uint8_t *)&F.l4);
                uint64_t t740 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t741 = GM_LD(uintptr_t, ((uint8_t *)&gm_l50) + 8);
                if (t740 >= (uintptr_t)(t741)) gm_trap_index(t740, t741, gm_srckfibkpt681aa, 587, 58);
                uint64_t t742 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l50)) + t740 * 8));
                gm_t4ce0jd3tdro4 t743;
                GM_ST(uint64_t, ((uint8_t *)&t743 + 0), t742);
                gm_tabb710d9mq2e t744;
                gm_f_format_4v9fh5b((void *)&t744, (void *)NULL, cx, NULL, t743);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t745 = t744;
                if (((((uint8_t *)&t745))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t745) + 8, gm_srckfibkpt681aa, 587, 28);
                gm_b16_8 t746 = GM_LD(gm_b16_8, (((uint8_t *)&t745) + 8));
                gm_b16_8 t747 = t746;
                gm_b16_8 t748 = GM_LD(gm_b16_8, ((uint8_t *)&t747));
                gm_tftb1odc739t5 t749 = gm_f_push_3ef746b(t739, t748);
                gm_tftb1odc739t5 t750 = t749;
                if (((((uint8_t *)&t750))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t750) + 1, gm_srckfibkpt681aa, 587, 21);
                gm_b0_1 t751 = GM_LD(gm_b0_1, (((uint8_t *)&t750) + 1));
            }
            {
                void * t752 = (void *)((uint8_t *)&F.l4);
                uint64_t t753;
                uint64_t t755 = GM_LD(uintptr_t, ((uint8_t *)&gm_l50) + 8);
                uint8_t t756 = (t755 > ((uint64_t)1ULL));
                if (!t756) goto ie754;
                uint64_t t758 = (uintptr_t)((uint64_t)1ULL);
                uint64_t t759 = GM_LD(uintptr_t, ((uint8_t *)&gm_l50) + 8);
                if (t758 >= (uintptr_t)(t759)) gm_trap_index(t758, t759, gm_srckfibkpt681aa, 588, 80);
                uint64_t t760 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l50)) + t758 * 8));
                uint64_t t761 = t760;
                t753 = t761;
                goto ix754;
ie754:;
                uint64_t t763 = ((uint64_t)0ULL);
                t753 = t763;
ix754:;
                gm_t4ce0jd3tdro4 t764;
                GM_ST(uint64_t, ((uint8_t *)&t764 + 0), t753);
                gm_tabb710d9mq2e t765;
                gm_f_format_io38sjt((void *)&t765, (void *)NULL, cx, NULL, t764);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t766 = t765;
                if (((((uint8_t *)&t766))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t766) + 8, gm_srckfibkpt681aa, 588, 28);
                gm_b16_8 t767 = GM_LD(gm_b16_8, (((uint8_t *)&t766) + 8));
                gm_b16_8 t768 = t767;
                gm_b16_8 t769 = GM_LD(gm_b16_8, ((uint8_t *)&t768));
                gm_tftb1odc739t5 t770 = gm_f_push_3ef746b(t752, t769);
                gm_tftb1odc739t5 t771 = t770;
                if (((((uint8_t *)&t771))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t771) + 1, gm_srckfibkpt681aa, 588, 21);
                gm_b0_1 t772 = GM_LD(gm_b0_1, (((uint8_t *)&t771) + 1));
            }
            {
                void * t773 = (void *)((uint8_t *)&F.l4);
                void * t774 = gm_l0;
                gm_t88rcucg1b7uk t775 = gm_l48;
                uint64_t t776 = gm_f_size_dmhtkib((void *)NULL, cx, NULL, t774, t775);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_t4ce0jd3tdro4 t777;
                GM_ST(uint64_t, ((uint8_t *)&t777 + 0), t776);
                gm_tabb710d9mq2e t778;
                gm_f_format_tbvfjdi((void *)&t778, (void *)NULL, cx, NULL, t777);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t779 = t778;
                if (((((uint8_t *)&t779))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t779) + 8, gm_srckfibkpt681aa, 589, 28);
                gm_b16_8 t780 = GM_LD(gm_b16_8, (((uint8_t *)&t779) + 8));
                gm_b16_8 t781 = t780;
                gm_b16_8 t782 = GM_LD(gm_b16_8, ((uint8_t *)&t781));
                gm_tftb1odc739t5 t783 = gm_f_push_3ef746b(t773, t782);
                gm_tftb1odc739t5 t784 = t783;
                if (((((uint8_t *)&t784))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t784) + 1, gm_srckfibkpt681aa, 589, 21);
                gm_b0_1 t785 = GM_LD(gm_b0_1, (((uint8_t *)&t784) + 1));
            }
            {
                void * t786 = (void *)((uint8_t *)&F.l4);
                void * t787 = gm_l0;
                gm_t88rcucg1b7uk t788 = gm_l48;
                uint32_t t789 = gm_f_bit_width_u86m8ir((void *)NULL, cx, NULL, t787, t788);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_t88rcucg1b7uk t790;
                GM_ST(uint32_t, ((uint8_t *)&t790 + 0), t789);
                gm_tabb710d9mq2e t791;
                gm_f_format_ch41dev((void *)&t791, (void *)NULL, cx, NULL, t790);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t792 = t791;
                if (((((uint8_t *)&t792))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t792) + 8, gm_srckfibkpt681aa, 590, 28);
                gm_b16_8 t793 = GM_LD(gm_b16_8, (((uint8_t *)&t792) + 8));
                gm_b16_8 t794 = t793;
                gm_b16_8 t795 = GM_LD(gm_b16_8, ((uint8_t *)&t794));
                gm_tftb1odc739t5 t796 = gm_f_push_3ef746b(t786, t795);
                gm_tftb1odc739t5 t797 = t796;
                if (((((uint8_t *)&t797))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t797) + 1, gm_srckfibkpt681aa, 590, 21);
                gm_b0_1 t798 = GM_LD(gm_b0_1, (((uint8_t *)&t797) + 1));
            }
            {
                void * t799 = (void *)((uint8_t *)&F.l4);
                void * t800 = gm_l0;
                gm_t88rcucg1b7uk t801 = gm_l48;
                uint8_t t802 = gm_f_is_signed_integer_ige9b74(t800, GM_CONV(gm_csguankko4lmhd, t801));
                gm_t9qkh0rsooc8d t803;
                GM_ST(uint8_t, ((uint8_t *)&t803 + 0), t802);
                gm_tabb710d9mq2e t804;
                gm_f_format_onn3qvb((void *)&t804, (void *)NULL, cx, NULL, t803);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t805 = t804;
                if (((((uint8_t *)&t805))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t805) + 8, gm_srckfibkpt681aa, 591, 28);
                gm_b16_8 t806 = GM_LD(gm_b16_8, (((uint8_t *)&t805) + 8));
                gm_b16_8 t807 = t806;
                gm_b16_8 t808 = GM_LD(gm_b16_8, ((uint8_t *)&t807));
                gm_tftb1odc739t5 t809 = gm_f_push_3ef746b(t799, t808);
                gm_tftb1odc739t5 t810 = t809;
                if (((((uint8_t *)&t810))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t810) + 1, gm_srckfibkpt681aa, 591, 21);
                gm_b0_1 t811 = GM_LD(gm_b0_1, (((uint8_t *)&t810) + 1));
            }
            goto me490;
mn645:;
ma493:;
            uint8_t t813 = GM_LD(uint8_t, ((((uint8_t *)t489) + 24) + 0));
            if (t813 != ((uint8_t)2ULL)) goto mn812;
            {
                void * t815 = (void *)((uint8_t *)&F.l4);
                gm_b16_8 t816;
                GM_ST(void *, (uint8_t *)&t816, (void *)(gm_spi406027g4jo));
                GM_ST(uintptr_t, (uint8_t *)&t816 + 8, (uintptr_t)(18));
                gm_tftb1odc739t5 t817 = gm_f_push_3ef746b(t815, t816);
                gm_tftb1odc739t5 t818 = t817;
                if (((((uint8_t *)&t818))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t818) + 1, gm_srckfibkpt681aa, 594, 21);
                gm_b0_1 t819 = GM_LD(gm_b0_1, (((uint8_t *)&t818) + 1));
            }
            {
                void * t820 = (void *)((uint8_t *)&F.l4);
                void * t821 = gm_l31;
                gm_b16_8 t822 = GM_LD(gm_b16_8, (((uint8_t *)t821) + 0));
                gm_tnpifv20irnph t823;
                GM_ST(gm_b16_8, ((uint8_t *)&t823 + 0), t822);
                gm_tabb710d9mq2e t824;
                gm_f_format_gkjg5ld((void *)&t824, (void *)NULL, cx, NULL, t823);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t825 = t824;
                if (((((uint8_t *)&t825))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t825) + 8, gm_srckfibkpt681aa, 595, 61);
                gm_b16_8 t826 = GM_LD(gm_b16_8, (((uint8_t *)&t825) + 8));
                gm_b16_8 t827 = t826;
                gm_b16_8 t828 = GM_LD(gm_b16_8, ((uint8_t *)&t827));
                gm_b16_8 t829 = gm_f_c_string_n7h27b3((void *)NULL, cx, NULL, t828);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tnpifv20irnph t830;
                GM_ST(gm_b16_8, ((uint8_t *)&t830 + 0), t829);
                gm_tabb710d9mq2e t831;
                gm_f_format_nbg44fl((void *)&t831, (void *)NULL, cx, NULL, t830);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t832 = t831;
                if (((((uint8_t *)&t832))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t832) + 8, gm_srckfibkpt681aa, 595, 28);
                gm_b16_8 t833 = GM_LD(gm_b16_8, (((uint8_t *)&t832) + 8));
                gm_b16_8 t834 = t833;
                gm_b16_8 t835 = GM_LD(gm_b16_8, ((uint8_t *)&t834));
                gm_tftb1odc739t5 t836 = gm_f_push_3ef746b(t820, t835);
                gm_tftb1odc739t5 t837 = t836;
                if (((((uint8_t *)&t837))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t837) + 1, gm_srckfibkpt681aa, 595, 21);
                gm_b0_1 t838 = GM_LD(gm_b0_1, (((uint8_t *)&t837) + 1));
            }
            goto me490;
mn812:;
ma494:;
            gm_trap("no match arm matched the scrutinee", gm_srckfibkpt681aa, 552, 13);
me490:;
        }
        goto me69;
mn465:;
ma81:;
        {
            void * t841 = (void *)((uint8_t *)&F.l4);
            gm_b16_8 t842;
            GM_ST(void *, (uint8_t *)&t842, (void *)(gm_spi406027g4jo));
            GM_ST(uintptr_t, (uint8_t *)&t842 + 8, (uintptr_t)(18));
            gm_tftb1odc739t5 t843 = gm_f_push_3ef746b(t841, t842);
            gm_tftb1odc739t5 t844 = t843;
            if (((((uint8_t *)&t844))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t844) + 1, gm_srckfibkpt681aa, 600, 13);
            gm_b0_1 t845 = GM_LD(gm_b0_1, (((uint8_t *)&t844) + 1));
        }
        {
            void * t846 = (void *)((uint8_t *)&F.l4);
            void * t847 = gm_l0;
            gm_t88rcucg1b7uk t848 = gm_l1;
            gm_b16_8 t849 = gm_f_show_5qbi623((void *)NULL, cx, NULL, t847, t848);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tnpifv20irnph t850;
            GM_ST(gm_b16_8, ((uint8_t *)&t850 + 0), t849);
            gm_tabb710d9mq2e t851;
            gm_f_format_gkjg5ld((void *)&t851, (void *)NULL, cx, NULL, t850);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t852 = t851;
            if (((((uint8_t *)&t852))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t852) + 8, gm_srckfibkpt681aa, 601, 53);
            gm_b16_8 t853 = GM_LD(gm_b16_8, (((uint8_t *)&t852) + 8));
            gm_b16_8 t854 = t853;
            gm_b16_8 t855 = GM_LD(gm_b16_8, ((uint8_t *)&t854));
            gm_b16_8 t856 = gm_f_c_string_n7h27b3((void *)NULL, cx, NULL, t855);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tnpifv20irnph t857;
            GM_ST(gm_b16_8, ((uint8_t *)&t857 + 0), t856);
            gm_tabb710d9mq2e t858;
            gm_f_format_nbg44fl((void *)&t858, (void *)NULL, cx, NULL, t857);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t859 = t858;
            if (((((uint8_t *)&t859))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t859) + 8, gm_srckfibkpt681aa, 601, 20);
            gm_b16_8 t860 = GM_LD(gm_b16_8, (((uint8_t *)&t859) + 8));
            gm_b16_8 t861 = t860;
            gm_b16_8 t862 = GM_LD(gm_b16_8, ((uint8_t *)&t861));
            gm_tftb1odc739t5 t863 = gm_f_push_3ef746b(t846, t862);
            gm_tftb1odc739t5 t864 = t863;
            if (((((uint8_t *)&t864))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t864) + 1, gm_srckfibkpt681aa, 601, 13);
            gm_b0_1 t865 = GM_LD(gm_b0_1, (((uint8_t *)&t864) + 1));
        }
        goto me69;
mn839:;
ma82:;
        gm_trap("no match arm matched the scrutinee", gm_srckfibkpt681aa, 489, 5);
me69:;
    }
    {
        gm_f_list_epvducg((void *)&F.l57, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t867 = GM_LD(gm_b16_8, (((uint8_t *)&F.l4) + 0));
            gm_l58 = t867;
        }
        {
            gm_l59 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t869 = gm_l59;
                uint64_t t870 = GM_LD(uintptr_t, ((uint8_t *)&gm_l58) + 8);
                uint8_t t871 = (t869 < t870);
                if (!t871) goto lb868;
                {
                    uint64_t t874 = gm_l59;
                    uint64_t t875 = (uintptr_t)t874;
                    uint64_t t876 = GM_LD(uintptr_t, ((uint8_t *)&gm_l58) + 8);
                    gm_b16_8 t879 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l58)) + t875 * 16));
                    gm_l60 = t879;
                    {
                        void * t881 = (void *)((uint8_t *)&F.l57);
                        gm_b16_8 t882;
                        gm_b16_8 t884 = gm_l60;
                        gm_b16_8 t885;
                        GM_ST(void *, (uint8_t *)&t885, (void *)(gm_sl3q52ep6aa53));
                        GM_ST(uintptr_t, (uint8_t *)&t885 + 8, (uintptr_t)(1));
                        uint8_t t886 = gm_f_starts_with_6vvt01e(t884, t885);
                        if (!t886) goto ie883;
                        gm_b16_8 t888 = gm_l60;
                        gm_b16_8 t889 = t888;
                        t882 = t889;
                        goto ix883;
ie883:;
                        gm_b16_8 t891 = gm_l60;
                        gm_tnpifv20irnph t892;
                        GM_ST(gm_b16_8, ((uint8_t *)&t892 + 0), t891);
                        gm_tabb710d9mq2e t893;
                        gm_f_format_g0ibdjv((void *)&t893, (void *)NULL, cx, NULL, t892);
                        if (gm_unw.kind) {
                            goto gm_ll872;
                        }
                        gm_tabb710d9mq2e t894 = t893;
                        if (((((uint8_t *)&t894))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t894) + 8, gm_srckfibkpt681aa, 605, 71);
                        gm_b16_8 t895 = GM_LD(gm_b16_8, (((uint8_t *)&t894) + 8));
                        gm_b16_8 t896 = t895;
                        gm_b16_8 t897 = t896;
                        gm_b16_8 t898 = GM_LD(gm_b16_8, ((uint8_t *)&t897));
                        t882 = t898;
ix883:;
                        gm_tftb1odc739t5 t899 = gm_f_push_3ef746b(t881, t882);
                        gm_tftb1odc739t5 t900 = t899;
                        if (((((uint8_t *)&t900))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t900) + 1, gm_srckfibkpt681aa, 605, 24);
                        gm_b0_1 t901 = GM_LD(gm_b0_1, (((uint8_t *)&t900) + 1));
                    }
                    goto me877;
mn878:;
                    gm_trap("no match arm matched the scrutinee", gm_srckfibkpt681aa, 605, 5);
me877:;
                }
lc868:;
                {
                    uint64_t t903 = gm_l59;
                    uint64_t r904; bool o905 = __builtin_add_overflow(t903, ((uint64_t)1ULL), &r904);
                    uint64_t t906 = r904;
                    gm_l59 = t906;
                }
            }
gm_ll872:;
            if (gm_unw.frame == &F.h && gm_unw.label == 4) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb868; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc868; }
            }
            goto gm_lad;
lb868:;
        }
    }
    {
        void * t907 = gm_l0;
        void * t908 = (void *)(((uint8_t *)t907) + 368);
        gm_t6j6smj7qoctu t909;
        gm_b16_8 t910 = gm_l3;
        GM_ST(gm_b16_8, (uint8_t *)&t909 + 0, t910);
        gm_b16_8 t911 = GM_LD(gm_b16_8, (((uint8_t *)&F.l57) + 0));
        gm_b16_8 t912 = t911;
        gm_b16_8 t913 = GM_LD(gm_b16_8, ((uint8_t *)&t912));
        gm_b16_8 t914;
        GM_ST(void *, (uint8_t *)&t914, (void *)(gm_sg5gdnrith2i6));
        GM_ST(uintptr_t, (uint8_t *)&t914 + 8, (uintptr_t)(2));
        gm_tabb710d9mq2e t915;
        gm_f_join_sfknljb((void *)&t915, (void *)NULL, cx, NULL, t913, t914);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t916 = t915;
        if (((((uint8_t *)&t916))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t916) + 8, gm_srckfibkpt681aa, 606, 66);
        gm_b16_8 t917 = GM_LD(gm_b16_8, (((uint8_t *)&t916) + 8));
        GM_ST(gm_b16_8, (uint8_t *)&t909 + 16, t917);
        gm_tftb1odc739t5 t918 = gm_f_write_uf08lpv(t908, &t909);
        gm_tftb1odc739t5 t919 = t918;
        if (((((uint8_t *)&t919))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t919) + 1, gm_srckfibkpt681aa, 606, 5);
        gm_b0_1 t920 = GM_LD(gm_b0_1, (((uint8_t *)&t919) + 1));
    }
    {
        gm_b16_8 t921 = gm_l3;
        gm_tnpifv20irnph t922;
        GM_ST(gm_b16_8, ((uint8_t *)&t922 + 0), t921);
        gm_tabb710d9mq2e t923;
        gm_f_format_h6b2ofo((void *)&t923, (void *)NULL, cx, NULL, t922);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t924 = t923;
        if (((((uint8_t *)&t924))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t924) + 8, gm_srckfibkpt681aa, 607, 12);
        gm_b16_8 t925 = GM_LD(gm_b16_8, (((uint8_t *)&t924) + 8));
        gm_b16_8 t926 = t925;
        gm_b16_8 t927 = GM_LD(gm_b16_8, ((uint8_t *)&t926));
        uint8_t t928 = 0;
        F.ret = t927;
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
extern __attribute__((aligned(4))) gm_b16_8 gm_f_descriptors_fnnm3h1(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_field_cty_nfj8rcv(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_field_cty_nfj8rcv F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    uint64_t gm_l2; /* len */
    gm_t88rcucg1b7uk gm_l3; /* elem */
    gm_t6j6smj7qoctu gm_l4; /* inner */
    gm_b16_8 gm_l5; /* m */
    GM_STACK_CHECK(&F, 1301u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        gm_t88rcucg1b7uk t3 = gm_l1;
        gm_t5v8n6qrn4rh4 t4;
        gm_f_repr_53quqki((void *)&t4, (void *)NULL, cx, NULL, t2, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t5v8n6qrn4rh4 t5 = t4;
        uint8_t t12 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        switch (t12) { case ((uint8_t)0ULL): goto ma8; case ((uint8_t)1ULL): goto ma8; case ((uint8_t)2ULL): goto ma7; case ((uint8_t)3ULL): goto ma8; case ((uint8_t)4ULL): goto ma9; case ((uint8_t)5ULL): goto ma9; case ((uint8_t)6ULL): goto ma10; default: goto ma11; }
ma7:;
        uint8_t t14 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t14 != ((uint8_t)2ULL)) goto mn13;
        gm_t6j6smj7qoctu t15;
        gm_b16_8 t16;
        GM_ST(void *, (uint8_t *)&t16, (void *)(gm_snokcvhtidt97));
        GM_ST(uintptr_t, (uint8_t *)&t16 + 8, (uintptr_t)(5));
        GM_ST(gm_b16_8, (uint8_t *)&t15 + 0, t16);
        gm_b16_8 t17;
        GM_ST(void *, (uint8_t *)&t17, (void *)(gm_sr4qbrk6tlgaa));
        GM_ST(uintptr_t, (uint8_t *)&t17 + 8, (uintptr_t)(0));
        GM_ST(gm_b16_8, (uint8_t *)&t15 + 16, t17);
        gm_ttu09lij36f8c t18;
        GM_ST(uint8_t, (uint8_t *)&t18 + 33, 0);
        GM_ST(uint16_t, (uint8_t *)&t18 + 34, 0);
        GM_ST(uint32_t, (uint8_t *)&t18 + 36, 0);
        GM_ST(gm_t6j6smj7qoctu, ((uint8_t *)&t18), t15);
        ((uint8_t *)&t18)[32] = 1;
        uint8_t t19 = 0;
        F.ret = t18;
        goto gm_fret;
        goto me6;
mn13:;
ma8:;
        uint8_t t23 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t23 != ((uint8_t)0ULL)) goto pa22;
        goto po21;
pa22:;
        uint8_t t25 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t25 != ((uint8_t)1ULL)) goto pa24;
        goto po21;
pa24:;
        uint8_t t27 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t27 != ((uint8_t)3ULL)) goto pa26;
        goto po21;
pa26:;
        goto mn20;
po21:;
        gm_t6j6smj7qoctu t28;
        void * t29 = gm_l0;
        gm_t88rcucg1b7uk t30 = gm_l1;
        gm_b16_8 t31 = gm_f_cty_66ee0kf((void *)NULL, cx, NULL, t29, t30);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_b16_8, (uint8_t *)&t28 + 0, t31);
        gm_b16_8 t32;
        GM_ST(void *, (uint8_t *)&t32, (void *)(gm_sr4qbrk6tlgaa));
        GM_ST(uintptr_t, (uint8_t *)&t32 + 8, (uintptr_t)(0));
        GM_ST(gm_b16_8, (uint8_t *)&t28 + 16, t32);
        gm_ttu09lij36f8c t33;
        GM_ST(uint8_t, (uint8_t *)&t33 + 33, 0);
        GM_ST(uint16_t, (uint8_t *)&t33 + 34, 0);
        GM_ST(uint32_t, (uint8_t *)&t33 + 36, 0);
        GM_ST(gm_t6j6smj7qoctu, ((uint8_t *)&t33), t28);
        ((uint8_t *)&t33)[32] = 1;
        uint8_t t34 = 0;
        F.ret = t33;
        goto gm_fret;
        goto me6;
mn20:;
ma9:;
        uint8_t t38 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t38 != ((uint8_t)4ULL)) goto pa37;
        goto po36;
pa37:;
        uint8_t t40 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t40 != ((uint8_t)5ULL)) goto pa39;
        goto po36;
pa39:;
        goto mn35;
po36:;
        gm_ttu09lij36f8c t41;
        memset(&t41, 0, sizeof t41);
        uint8_t t42 = 0;
        F.ret = t41;
        goto gm_fret;
        goto me6;
mn35:;
ma10:;
        uint8_t t44 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t44 != ((uint8_t)6ULL)) goto mn43;
        goto me6;
mn43:;
ma11:;
        gm_trap("no match arm matched the scrutinee", gm_srckfibkpt681aa, 414, 5);
me6:;
    }
    {
        void * t46 = gm_l0;
        gm_t88rcucg1b7uk t47 = gm_l1;
        gm_t53at80pfdus3 t48;
        gm_f_kind_ghvav22((void *)&t48, t46, GM_CONV(gm_csguankko4lmhd, t47));
        gm_t53at80pfdus3 t49 = t48;
        uint8_t t55 = GM_LD(uint8_t, (((uint8_t *)&t49) + 0));
        switch (t55) { case ((uint8_t)0ULL): goto ma53; case ((uint8_t)1ULL): goto ma53; case ((uint8_t)2ULL): goto ma53; case ((uint8_t)3ULL): goto ma53; case ((uint8_t)4ULL): goto ma53; case ((uint8_t)5ULL): goto ma53; case ((uint8_t)6ULL): goto ma53; case ((uint8_t)7ULL): goto ma53; case ((uint8_t)8ULL): goto ma53; case ((uint8_t)9ULL): goto ma53; case ((uint8_t)10ULL): goto ma53; case ((uint8_t)11ULL): goto ma51; case ((uint8_t)12ULL): goto ma53; case ((uint8_t)13ULL): goto ma53; case ((uint8_t)14ULL): goto ma53; case ((uint8_t)15ULL): goto ma53; case ((uint8_t)16ULL): goto ma53; case ((uint8_t)17ULL): goto ma52; case ((uint8_t)18ULL): goto ma53; case ((uint8_t)19ULL): goto ma53; case ((uint8_t)20ULL): goto ma53; case ((uint8_t)21ULL): goto ma53; case ((uint8_t)22ULL): goto ma53; case ((uint8_t)23ULL): goto ma53; case ((uint8_t)24ULL): goto ma53; case ((uint8_t)25ULL): goto ma53; case ((uint8_t)26ULL): goto ma53; default: goto ma53; }
ma51:;
        uint8_t t57 = GM_LD(uint8_t, (((uint8_t *)&t49) + 0));
        if (t57 != ((uint8_t)11ULL)) goto mn56;
        uint64_t t58 = GM_LD(uint64_t, ((((uint8_t *)&t49) + 8) + 0));
        gm_l2 = t58;
        gm_t88rcucg1b7uk t59 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t49) + 8) + 8));
        gm_l3 = t59;
        {
            uint64_t t62 = gm_l2;
            uint8_t t63 = ((t62 == ((uint64_t)0ULL)));
            if (!t63) goto ie61;
            {
                gm_ttu09lij36f8c t65;
                memset(&t65, 0, sizeof t65);
                uint8_t t66 = 0;
                F.ret = t65;
                goto gm_fret;
            }
            goto ix61;
ie61:;
ix61:;
        }
        {
            void * t67 = gm_l0;
            gm_t88rcucg1b7uk t68 = gm_l3;
            gm_ttu09lij36f8c t69;
            gm_f_field_cty_nfj8rcv((void *)&t69, (void *)NULL, cx, NULL, t67, t68);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_ttu09lij36f8c t70 = t69;
            gm_t6j6smj7qoctu t71;
            if (((((uint8_t *)&t70))[32] != 0)) {
                gm_t6j6smj7qoctu t72 = GM_LD(gm_t6j6smj7qoctu, (((uint8_t *)&t70) + 0));
                t71 = t72;
            } else {
                gm_ttu09lij36f8c t73;
                memset(&t73, 0, sizeof t73);
                uint8_t t74 = 0;
                F.ret = t73;
                goto gm_fret;
            }
            gm_l4 = t71;
        }
        {
            gm_t6j6smj7qoctu t75;
            gm_b16_8 t76 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l4) + 0));
            GM_ST(gm_b16_8, (uint8_t *)&t75 + 0, t76);
            gm_tf1bjg81u0glv t77;
            uint64_t t78 = gm_l2;
            GM_ST(uint64_t, (uint8_t *)&t77 + 0, t78);
            gm_b16_8 t79 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l4) + 16));
            GM_ST(gm_b16_8, (uint8_t *)&t77 + 8, t79);
            gm_tabb710d9mq2e t80;
            gm_f_format_6a91mh8((void *)&t80, (void *)NULL, cx, NULL, &t77);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t81 = t80;
            if (((((uint8_t *)&t81))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t81) + 8, gm_srckfibkpt681aa, 424, 51);
            gm_b16_8 t82 = GM_LD(gm_b16_8, (((uint8_t *)&t81) + 8));
            gm_b16_8 t83 = t82;
            gm_b16_8 t84 = GM_LD(gm_b16_8, ((uint8_t *)&t83));
            GM_ST(gm_b16_8, (uint8_t *)&t75 + 16, t84);
            gm_ttu09lij36f8c t85;
            GM_ST(uint8_t, (uint8_t *)&t85 + 33, 0);
            GM_ST(uint16_t, (uint8_t *)&t85 + 34, 0);
            GM_ST(uint32_t, (uint8_t *)&t85 + 36, 0);
            GM_ST(gm_t6j6smj7qoctu, ((uint8_t *)&t85), t75);
            ((uint8_t *)&t85)[32] = 1;
            uint8_t t86 = 0;
            F.ret = t85;
            goto gm_fret;
        }
        goto me50;
mn56:;
ma52:;
        uint8_t t88 = GM_LD(uint8_t, (((uint8_t *)&t49) + 0));
        if (t88 != ((uint8_t)17ULL)) goto mn87;
        {
            void * t90 = gm_l0;
            gm_t88rcucg1b7uk t91 = gm_l1;
            gm_th2lqbo77pvsc t92;
            gm_f_mirror_lcjegsf((void *)&t92, (void *)NULL, cx, NULL, t90, t91);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_th2lqbo77pvsc t93 = t92;
            gm_b16_8 t94;
            if (((((uint8_t *)&t93))[16] != 0)) {
                gm_b16_8 t95 = GM_LD(gm_b16_8, (((uint8_t *)&t93) + 0));
                t94 = t95;
            } else {
                gm_ttu09lij36f8c t96;
                memset(&t96, 0, sizeof t96);
                uint8_t t97 = 0;
                F.ret = t96;
                goto gm_fret;
            }
            gm_l5 = t94;
        }
        {
            gm_t6j6smj7qoctu t98;
            gm_b16_8 t99 = gm_l5;
            GM_ST(gm_b16_8, (uint8_t *)&t98 + 0, t99);
            gm_b16_8 t100;
            GM_ST(void *, (uint8_t *)&t100, (void *)(gm_sr4qbrk6tlgaa));
            GM_ST(uintptr_t, (uint8_t *)&t100 + 8, (uintptr_t)(0));
            GM_ST(gm_b16_8, (uint8_t *)&t98 + 16, t100);
            gm_ttu09lij36f8c t101;
            GM_ST(uint8_t, (uint8_t *)&t101 + 33, 0);
            GM_ST(uint16_t, (uint8_t *)&t101 + 34, 0);
            GM_ST(uint32_t, (uint8_t *)&t101 + 36, 0);
            GM_ST(gm_t6j6smj7qoctu, ((uint8_t *)&t101), t98);
            ((uint8_t *)&t101)[32] = 1;
            uint8_t t102 = 0;
            F.ret = t101;
            goto gm_fret;
        }
        goto me50;
mn87:;
ma53:;
        gm_ttu09lij36f8c t104;
        memset(&t104, 0, sizeof t104);
        uint8_t t105 = 0;
        F.ret = t104;
        goto gm_fret;
        goto me50;
mn103:;
ma54:;
        gm_trap("no match arm matched the scrutinee", gm_srckfibkpt681aa, 420, 5);
me50:;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fields_array_tikg33t(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2, void * a3) {
    struct gm_fr_fields_array_tikg33t F;
    void * gm_l0; /* ty */
    gm_b16_8 gm_l1; /* i */
    gm_b16_8 gm_l2; /* parts */
    void * gm_l3; /* f */
    GM_STACK_CHECK(&F, 514u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        uint64_t t3 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        uint8_t t4 = ((t3 == ((uint64_t)0ULL)));
        if (!t4) goto ie2;
        {
            void * t6 = gm_l3;
            gm_b16_8 t7;
            GM_ST(void *, (uint8_t *)&t7, (void *)(gm_so3qrsvnd4euf));
            GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)(11));
            gm_tftb1odc739t5 t8 = gm_f_push_3ef746b(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            if (((((uint8_t *)&t9))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t9) + 1, gm_srckfibkpt681aa, 612, 9);
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
        }
        {
            gm_b0_1 t11;
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
        void * t14 = (void *)(((uint8_t *)t13) + 328);
        gm_t6j6smj7qoctu t15;
        gm_b16_8 t16 = gm_l1;
        GM_ST(gm_b16_8, (uint8_t *)&t15 + 0, t16);
        gm_b16_8 t17 = gm_l2;
        gm_b16_8 t18;
        GM_ST(void *, (uint8_t *)&t18, (void *)(gm_sg5gdnrith2i6));
        GM_ST(uintptr_t, (uint8_t *)&t18 + 8, (uintptr_t)(2));
        gm_tabb710d9mq2e t19;
        gm_f_join_sfknljb((void *)&t19, (void *)NULL, cx, NULL, t17, t18);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t20 = t19;
        if (((((uint8_t *)&t20))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t20) + 8, gm_srckfibkpt681aa, 615, 72);
        gm_b16_8 t21 = GM_LD(gm_b16_8, (((uint8_t *)&t20) + 8));
        GM_ST(gm_b16_8, (uint8_t *)&t15 + 16, t21);
        gm_tftb1odc739t5 t22 = gm_f_write_3mmgun5(t14, &t15);
        gm_tftb1odc739t5 t23 = t22;
        if (((((uint8_t *)&t23))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t23) + 1, gm_srckfibkpt681aa, 615, 5);
        gm_b0_1 t24 = GM_LD(gm_b0_1, (((uint8_t *)&t23) + 1));
    }
    {
        void * t25 = gm_l3;
        uint64_t t26 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        gm_t4ce0jd3tdro4 t27;
        GM_ST(uint64_t, ((uint8_t *)&t27 + 0), t26);
        gm_tabb710d9mq2e t28;
        gm_f_format_aiomp5b((void *)&t28, (void *)NULL, cx, NULL, t27);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t29 = t28;
        if (((((uint8_t *)&t29))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t29) + 8, gm_srckfibkpt681aa, 616, 12);
        gm_b16_8 t30 = GM_LD(gm_b16_8, (((uint8_t *)&t29) + 8));
        gm_b16_8 t31 = t30;
        gm_b16_8 t32 = GM_LD(gm_b16_8, ((uint8_t *)&t31));
        gm_tftb1odc739t5 t33 = gm_f_push_3ef746b(t25, t32);
        gm_tftb1odc739t5 t34 = t33;
        if (((((uint8_t *)&t34))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t34) + 1, gm_srckfibkpt681aa, 616, 5);
        gm_b0_1 t35 = GM_LD(gm_b0_1, (((uint8_t *)&t34) + 1));
    }
    {
        void * t36 = gm_l3;
        gm_b16_8 t37 = gm_l1;
        gm_tnpifv20irnph t38;
        GM_ST(gm_b16_8, ((uint8_t *)&t38 + 0), t37);
        gm_tabb710d9mq2e t39;
        gm_f_format_s51nisc((void *)&t39, (void *)NULL, cx, NULL, t38);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t40 = t39;
        if (((((uint8_t *)&t40))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t40) + 8, gm_srckfibkpt681aa, 617, 12);
        gm_b16_8 t41 = GM_LD(gm_b16_8, (((uint8_t *)&t40) + 8));
        gm_b16_8 t42 = t41;
        gm_b16_8 t43 = GM_LD(gm_b16_8, ((uint8_t *)&t42));
        gm_tftb1odc739t5 t44 = gm_f_push_3ef746b(t36, t43);
        gm_tftb1odc739t5 t45 = t44;
        if (((((uint8_t *)&t45))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t45) + 1, gm_srckfibkpt681aa, 617, 5);
        gm_b0_1 t46 = GM_LD(gm_b0_1, (((uint8_t *)&t45) + 1));
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
extern __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_0h3rgr6(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_int_cty_s62af0i(uint8_t a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_int_cty_s62af0i F;
    uint8_t gm_l0; /* signed */
    uint64_t gm_l1; /* storage */
    GM_STACK_CHECK_SHALLOW(&F, 510u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint64_t t1 = gm_l1;
    uint64_t t2 = t1;
    gm_b16_8 t3;
    uint64_t t6 = GM_LD(uint64_t, ((uint8_t *)&t2));
    if (!(t6 == ((uint64_t)1ULL))) goto mn5;
    gm_b16_8 t7;
    uint8_t t9 = gm_l0;
    if (!t9) goto ie8;
    gm_b16_8 t11;
    GM_ST(void *, (uint8_t *)&t11, (void *)(gm_suuq26jjqhul4));
    GM_ST(uintptr_t, (uint8_t *)&t11 + 8, (uintptr_t)(6));
    gm_b16_8 t12 = t11;
    t7 = t12;
    goto ix8;
ie8:;
    gm_b16_8 t14;
    GM_ST(void *, (uint8_t *)&t14, (void *)(gm_s9grtrvtmi962));
    GM_ST(uintptr_t, (uint8_t *)&t14 + 8, (uintptr_t)(7));
    gm_b16_8 t15 = t14;
    t7 = t15;
ix8:;
    t3 = t7;
    goto me4;
mn5:;
    uint64_t t17 = GM_LD(uint64_t, ((uint8_t *)&t2));
    if (!(t17 == ((uint64_t)2ULL))) goto mn16;
    gm_b16_8 t18;
    uint8_t t20 = gm_l0;
    if (!t20) goto ie19;
    gm_b16_8 t22;
    GM_ST(void *, (uint8_t *)&t22, (void *)(gm_std3g89n1j9ln));
    GM_ST(uintptr_t, (uint8_t *)&t22 + 8, (uintptr_t)(7));
    gm_b16_8 t23 = t22;
    t18 = t23;
    goto ix19;
ie19:;
    gm_b16_8 t25;
    GM_ST(void *, (uint8_t *)&t25, (void *)(gm_suggn3kp9dfmg));
    GM_ST(uintptr_t, (uint8_t *)&t25 + 8, (uintptr_t)(8));
    gm_b16_8 t26 = t25;
    t18 = t26;
ix19:;
    t3 = t18;
    goto me4;
mn16:;
    uint64_t t28 = GM_LD(uint64_t, ((uint8_t *)&t2));
    if (!(t28 == ((uint64_t)4ULL))) goto mn27;
    gm_b16_8 t29;
    uint8_t t31 = gm_l0;
    if (!t31) goto ie30;
    gm_b16_8 t33;
    GM_ST(void *, (uint8_t *)&t33, (void *)(gm_ska87i9f7924f));
    GM_ST(uintptr_t, (uint8_t *)&t33 + 8, (uintptr_t)(7));
    gm_b16_8 t34 = t33;
    t29 = t34;
    goto ix30;
ie30:;
    gm_b16_8 t36;
    GM_ST(void *, (uint8_t *)&t36, (void *)(gm_s6r94a2k1goos));
    GM_ST(uintptr_t, (uint8_t *)&t36 + 8, (uintptr_t)(8));
    gm_b16_8 t37 = t36;
    t29 = t37;
ix30:;
    t3 = t29;
    goto me4;
mn27:;
    uint64_t t39 = GM_LD(uint64_t, ((uint8_t *)&t2));
    if (!(t39 == ((uint64_t)8ULL))) goto mn38;
    gm_b16_8 t40;
    uint8_t t42 = gm_l0;
    if (!t42) goto ie41;
    gm_b16_8 t44;
    GM_ST(void *, (uint8_t *)&t44, (void *)(gm_so4g53vn8c2d4));
    GM_ST(uintptr_t, (uint8_t *)&t44 + 8, (uintptr_t)(7));
    gm_b16_8 t45 = t44;
    t40 = t45;
    goto ix41;
ie41:;
    gm_b16_8 t47;
    GM_ST(void *, (uint8_t *)&t47, (void *)(gm_shn8mjp9kc2n2));
    GM_ST(uintptr_t, (uint8_t *)&t47 + 8, (uintptr_t)(8));
    gm_b16_8 t48 = t47;
    t40 = t48;
ix41:;
    t3 = t40;
    goto me4;
mn38:;
    gm_b16_8 t50;
    uint8_t t52 = gm_l0;
    if (!t52) goto ie51;
    gm_b16_8 t54;
    GM_ST(void *, (uint8_t *)&t54, (void *)(gm_s1a1r1qe3ssot));
    GM_ST(uintptr_t, (uint8_t *)&t54 + 8, (uintptr_t)(8));
    gm_b16_8 t55 = t54;
    t50 = t55;
    goto ix51;
ie51:;
    gm_b16_8 t57;
    GM_ST(void *, (uint8_t *)&t57, (void *)(gm_st9g0aublffth));
    GM_ST(uintptr_t, (uint8_t *)&t57 + 8, (uintptr_t)(17));
    gm_b16_8 t58 = t57;
    t50 = t58;
ix51:;
    t3 = t50;
    goto me4;
mn49:;
    gm_trap("no match arm matched the scrutinee", gm_srckfibkpt681aa, 629, 56);
me4:;
    F.ret = t3;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_int_ty_7nma1b7(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, uint16_t a2);
extern __attribute__((aligned(4))) uint8_t gm_f_is_float_gthejtb(void * a0, gm_csguankko4lmhd a1);
extern __attribute__((aligned(4))) uint8_t gm_f_is_integer_eo6jkc5(void * a0, gm_csguankko4lmhd a1);
extern __attribute__((aligned(4))) uint8_t gm_f_is_never_in5ftma(void * a0, gm_csguankko4lmhd a1);
extern __attribute__((aligned(4))) uint8_t gm_f_is_signed_integer_ige9b74(void * a0, gm_csguankko4lmhd a1);
extern __attribute__((aligned(4))) uint8_t gm_f_is_unit_17uennd(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_vector_fm7q9br(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_vector_fm7q9br F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK_SHALLOW(&F, 160u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    void * t1 = gm_l0;
    void * t2 = GM_LD(void *, (((uint8_t *)t1) + 0));
    void * t3 = (void *)(((uint8_t *)t2) + 8);
    gm_t88rcucg1b7uk t4 = gm_l1;
    gm_t53at80pfdus3 t5;
    gm_f_kind_jcnt4hm((void *)&t5, t3, GM_CONV(gm_csguankko4lmhd, t4));
    gm_t53at80pfdus3 t6 = t5;
    uint8_t t7;
    uint8_t t13 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
    switch (t13) { case ((uint8_t)0ULL): goto ma11; case ((uint8_t)1ULL): goto ma11; case ((uint8_t)2ULL): goto ma11; case ((uint8_t)3ULL): goto ma11; case ((uint8_t)4ULL): goto ma11; case ((uint8_t)5ULL): goto ma11; case ((uint8_t)6ULL): goto ma11; case ((uint8_t)7ULL): goto ma11; case ((uint8_t)8ULL): goto ma11; case ((uint8_t)9ULL): goto ma11; case ((uint8_t)10ULL): goto ma11; case ((uint8_t)11ULL): goto ma9; case ((uint8_t)12ULL): goto ma11; case ((uint8_t)13ULL): goto ma11; case ((uint8_t)14ULL): goto ma11; case ((uint8_t)15ULL): goto ma11; case ((uint8_t)16ULL): goto ma11; case ((uint8_t)17ULL): goto ma11; case ((uint8_t)18ULL): goto ma11; case ((uint8_t)19ULL): goto ma11; case ((uint8_t)20ULL): goto ma11; case ((uint8_t)21ULL): goto ma11; case ((uint8_t)22ULL): goto ma11; case ((uint8_t)23ULL): goto ma11; case ((uint8_t)24ULL): goto ma11; case ((uint8_t)25ULL): goto ma11; case ((uint8_t)26ULL): goto ma11; default: goto ma11; }
ma9:;
    uint8_t t15 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
    if (t15 != ((uint8_t)11ULL)) goto mn14;
    uint8_t t16 = GM_LD(uint8_t, (((((uint8_t *)&t6) + 8) + 12) + 0));
    if (t16 != ((uint8_t)2ULL)) goto mn14;
    t7 = ((uint8_t)1);
    goto me8;
mn14:;
ma10:;
    uint8_t t18 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
    if (t18 != ((uint8_t)11ULL)) goto mn17;
    uint8_t t19 = GM_LD(uint8_t, (((((uint8_t *)&t6) + 8) + 12) + 0));
    if (t19 != ((uint8_t)3ULL)) goto mn17;
    t7 = ((uint8_t)1);
    goto me8;
mn17:;
ma11:;
    t7 = ((uint8_t)0);
    goto me8;
mn20:;
ma12:;
    gm_trap("no match arm matched the scrutinee", gm_srckfibkpt681aa, 115, 54);
me8:;
    F.ret = t7;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) uint8_t gm_f_is_void_vs2outo(void * a0, gm_csguankko4lmhd a1);
extern __attribute__((aligned(4))) void gm_f_kind_ghvav22(void *gm_out, void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) gm_t53at80pfdus3 gm_f_kind_ghvav22_c(void * a0, gm_csguankko4lmhd a1) { gm_t53at80pfdus3 gm_r; gm_f_kind_ghvav22(&gm_r, a0, a1); return gm_r; }
extern __attribute__((aligned(4))) uint8_t gm_f_kind_rank_g1kdlst(const void * a0);
__attribute__((aligned(4))) uint8_t gm_f_kind_rank_g1kdlst_c(gm_tl572d7579hns a0) { return gm_f_kind_rank_g1kdlst(&a0); }
__attribute__((aligned(4))) gm_b16_8 gm_f_make_cty_3kvgdd5(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_make_cty_3kvgdd5 F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    uint8_t gm_l2; /* signed */
    uint64_t gm_l3; /* storage */
    gm_b16_8 gm_l4; /* n */
    uint64_t gm_l5; /* s */
    uint64_t gm_l6; /* a */
    gm_tl572d7579hns gm_l7; /* kind */
    gm_b16_8 gm_l8; /* name */
    GM_STACK_CHECK(&F, 792u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        gm_t88rcucg1b7uk t3 = gm_l1;
        gm_t5v8n6qrn4rh4 t4;
        gm_f_repr_53quqki((void *)&t4, (void *)NULL, cx, NULL, t2, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t5v8n6qrn4rh4 t5 = t4;
        uint8_t t17 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        switch (t17) { case ((uint8_t)0ULL): goto ma7; case ((uint8_t)1ULL): goto ma8; case ((uint8_t)2ULL): goto ma11; case ((uint8_t)3ULL): goto ma12; case ((uint8_t)4ULL): goto ma13; case ((uint8_t)5ULL): goto ma14; case ((uint8_t)6ULL): goto ma15; default: goto ma16; }
ma7:;
        uint8_t t19 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t19 != ((uint8_t)0ULL)) goto mn18;
        uint8_t t20 = GM_LD(uint8_t, ((((uint8_t *)&t5) + 8) + 0));
        gm_l2 = t20;
        uint64_t t21 = GM_LD(uint64_t, ((((uint8_t *)&t5) + 8) + 8));
        gm_l3 = t21;
        uint8_t t22 = gm_l2;
        uint64_t t23 = gm_l3;
        gm_b16_8 t24 = gm_f_int_cty_s62af0i(t22, t23);
        uint8_t t25 = 0;
        F.ret = t24;
        goto gm_fret;
        goto me6;
mn18:;
ma8:;
        uint8_t t27 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t27 != ((uint8_t)1ULL)) goto mn26;
        uint16_t t28 = GM_LD(uint16_t, (((uint8_t *)&t5) + 8));
        if (!(t28 == ((uint16_t)16ULL))) goto mn26;
        gm_b16_8 t29;
        GM_ST(void *, (uint8_t *)&t29, (void *)(gm_scj0sm2kpivk4));
        GM_ST(uintptr_t, (uint8_t *)&t29 + 8, (uintptr_t)(8));
        uint8_t t30 = 0;
        F.ret = t29;
        goto gm_fret;
        goto me6;
mn26:;
ma9:;
        uint8_t t32 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t32 != ((uint8_t)1ULL)) goto mn31;
        uint16_t t33 = GM_LD(uint16_t, (((uint8_t *)&t5) + 8));
        if (!(t33 == ((uint16_t)32ULL))) goto mn31;
        gm_b16_8 t34;
        GM_ST(void *, (uint8_t *)&t34, (void *)(gm_s4cess8gum6i7));
        GM_ST(uintptr_t, (uint8_t *)&t34 + 8, (uintptr_t)(5));
        uint8_t t35 = 0;
        F.ret = t34;
        goto gm_fret;
        goto me6;
mn31:;
ma10:;
        uint8_t t37 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t37 != ((uint8_t)1ULL)) goto mn36;
        gm_b16_8 t38;
        GM_ST(void *, (uint8_t *)&t38, (void *)(gm_s866ofni9sb8u));
        GM_ST(uintptr_t, (uint8_t *)&t38 + 8, (uintptr_t)(6));
        uint8_t t39 = 0;
        F.ret = t38;
        goto gm_fret;
        goto me6;
mn36:;
ma11:;
        uint8_t t41 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t41 != ((uint8_t)2ULL)) goto mn40;
        gm_b16_8 t42;
        GM_ST(void *, (uint8_t *)&t42, (void *)(gm_s9grtrvtmi962));
        GM_ST(uintptr_t, (uint8_t *)&t42 + 8, (uintptr_t)(7));
        uint8_t t43 = 0;
        F.ret = t42;
        goto gm_fret;
        goto me6;
mn40:;
ma12:;
        uint8_t t45 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t45 != ((uint8_t)3ULL)) goto mn44;
        gm_b16_8 t46;
        GM_ST(void *, (uint8_t *)&t46, (void *)(gm_s44ek1gecbajs));
        GM_ST(uintptr_t, (uint8_t *)&t46 + 8, (uintptr_t)(6));
        uint8_t t47 = 0;
        F.ret = t46;
        goto gm_fret;
        goto me6;
mn44:;
ma13:;
        uint8_t t49 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t49 != ((uint8_t)4ULL)) goto mn48;
        gm_b16_8 t50;
        GM_ST(void *, (uint8_t *)&t50, (void *)(gm_s6g2mpli2mu72));
        GM_ST(uintptr_t, (uint8_t *)&t50 + 8, (uintptr_t)(10));
        uint8_t t51 = 0;
        F.ret = t50;
        goto gm_fret;
        goto me6;
mn48:;
ma14:;
        uint8_t t53 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t53 != ((uint8_t)5ULL)) goto mn52;
        gm_b16_8 t54;
        GM_ST(void *, (uint8_t *)&t54, (void *)(gm_smiavg3q7ucd9));
        GM_ST(uintptr_t, (uint8_t *)&t54 + 8, (uintptr_t)(7));
        uint8_t t55 = 0;
        F.ret = t54;
        goto gm_fret;
        goto me6;
mn52:;
ma15:;
        uint8_t t57 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t57 != ((uint8_t)6ULL)) goto mn56;
        {
            void * t60 = gm_l0;
            gm_t88rcucg1b7uk t61 = gm_l1;
            gm_th2lqbo77pvsc t62;
            gm_f_typed_stkcgvr((void *)&t62, (void *)NULL, cx, NULL, t60, t61);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_th2lqbo77pvsc t63 = t62;
            if (!((((uint8_t *)&t63))[16] != 0)) goto ie59;
            gm_b16_8 t64 = GM_LD(gm_b16_8, ((uint8_t *)&t63));
            gm_l4 = t64;
            {
                gm_b16_8 t66 = gm_l4;
                uint8_t t67 = 0;
                F.ret = t66;
                goto gm_fret;
            }
            goto ix59;
ie59:;
ix59:;
        }
        {
            void * t68 = gm_l0;
            gm_t88rcucg1b7uk t69 = gm_l1;
            uint64_t t70 = gm_f_size_dmhtkib((void *)NULL, cx, NULL, t68, t69);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l5 = t70;
        }
        {
            void * t71 = gm_l0;
            gm_t88rcucg1b7uk t72 = gm_l1;
            uint64_t t73 = gm_f_align_uu2harl((void *)NULL, cx, NULL, t71, t72);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint64_t t74 = (t73 > ((uint64_t)1ULL) ? t73 : ((uint64_t)1ULL));
            gm_l6 = t74;
        }
        {
            void * t75 = gm_l0;
            gm_t88rcucg1b7uk t76 = gm_l1;
            uint64_t t77 = gm_l5;
            uint64_t t78 = gm_l6;
            gm_f_blob_kind_kp13j9k((void *)&gm_l7, (void *)NULL, cx, NULL, t75, t76, t77, t78);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            uint64_t t79 = gm_l5;
            uint64_t t80 = gm_l6;
            gm_b16_8 t81 = gm_f_blob_name_sjpa1ub((void *)NULL, cx, NULL, t79, t80, &(*(gm_tl572d7579hns *)((uint8_t *)&gm_l7)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l8 = t81;
        }
        {
            void * t83 = gm_l0;
            void * t84 = (void *)(((uint8_t *)t83) + 120);
            gm_b16_8 t85 = gm_l8;
            gm_tqs5p4n2tq19v t86 = gm_f_get_p217beu(t84, t85);
            gm_tqs5p4n2tq19v t87 = t86;
            uint8_t t88;
            if (((((uint8_t *)&t87))[1] != 0)) goto mn90;
            t88 = ((uint8_t)1);
            goto me89;
mn90:;
            t88 = ((uint8_t)0);
            goto me89;
mn91:;
            gm_trap("no match arm matched the scrutinee", gm_srckfibkpt681aa, 184, 16);
me89:;
            if (!t88) goto ie82;
            {
                void * t93 = gm_l0;
                void * t94 = (void *)(((uint8_t *)t93) + 120);
                gm_b16_8 t95 = gm_l8;
                gm_tftb1odc739t5 t96 = gm_f_put_5c6qgui(t94, t95, ((uint8_t)1));
                gm_tftb1odc739t5 t97 = t96;
                if (((((uint8_t *)&t97))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t97) + 1, gm_srckfibkpt681aa, 185, 17);
                gm_b0_1 t98 = GM_LD(gm_b0_1, (((uint8_t *)&t97) + 1));
            }
            {
                void * t99 = gm_l0;
                void * t100 = (void *)(((uint8_t *)t99) + 80);
                gm_tulpvg6gm4fqa t101;
                uint64_t t102 = gm_l5;
                GM_ST(uint64_t, (uint8_t *)&t101 + 0, t102);
                uint64_t t103 = gm_l6;
                GM_ST(uint64_t, (uint8_t *)&t101 + 8, t103);
                memcpy((uint8_t *)&t101 + 16, ((uint8_t *)&gm_l7), 24);
                void * t104 = gm_l0;
                uint64_t t105 = gm_l5;
                uint64_t t106 = gm_l6;
                gm_b16_8 t107 = gm_l8;
                gm_b16_8 t108 = gm_f_blob_typedef_8ta3rbi((void *)NULL, cx, NULL, t104, t105, t106, &(*(gm_tl572d7579hns *)((uint8_t *)&gm_l7)), t107);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(gm_b16_8, (uint8_t *)&t101 + 40, t108);
                gm_tftb1odc739t5 t109 = gm_f_push_pmmlu39(t100, &t101);
                gm_tftb1odc739t5 t110 = t109;
                if (((((uint8_t *)&t110))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t110) + 1, gm_srckfibkpt681aa, 186, 17);
                gm_b0_1 t111 = GM_LD(gm_b0_1, (((uint8_t *)&t110) + 1));
            }
            goto ix82;
ie82:;
ix82:;
        }
        {
            gm_b16_8 t112 = gm_l8;
            uint8_t t113 = 0;
            F.ret = t112;
            goto gm_fret;
        }
        goto me6;
mn56:;
ma16:;
        gm_trap("no match arm matched the scrutinee", gm_srckfibkpt681aa, 169, 5);
me6:;
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
__attribute__((aligned(4))) void gm_f_make_mirror_rudvl6p(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_make_mirror_rudvl6p F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    uint32_t gm_l2; /* id */
    gm_b16_8 gm_l3; /* keyword */
    gm_b16_8 gm_l4; /* fields */
    uint8_t gm_l5; /* packed */
    gm_tf5srh4mp4b3i gm_l6; /* explicit */
    gm_t88rcucg1b7uk gm_l8; /* u */
    gm_b16_8 gm_l9; /* fs */
    gm_b16_8 gm_l12; /* <tmp> */
    uint64_t gm_l13; /* <tmp> */
    gm_b16_8 gm_l16; /* <tmp> */
    uint64_t gm_l17; /* <tmp> */
    uint64_t gm_l18; /* i */
    gm_th2lqbo77pvsc gm_l19; /* n */
    uint64_t gm_l20; /* <tmp> */
    uint64_t gm_l21; /* <tmp> */
    uint64_t gm_l22; /* i */
    gm_b16_8 gm_l24; /* <tmp> */
    uint64_t gm_l25; /* <tmp> */
    uint64_t gm_l26; /* i */
    gm_t6j6smj7qoctu gm_l28; /* fc */
    uint64_t gm_l29; /* a */
    gm_b16_8 gm_l30; /* al */
    gm_b16_8 gm_l31; /* <tmp> */
    uint64_t gm_l32; /* <tmp> */
    gm_b16_8 gm_l33; /* n */
    uint64_t gm_l35; /* a */
    gm_b16_8 gm_l36; /* attr_text */
    gm_b16_8 gm_l37; /* content */
    gm_b16_8 gm_l38; /* n */
    gm_b16_8 gm_l39; /* name */
    gm_b16_8 gm_l40; /* offs */
    uint64_t gm_l41; /* <tmp> */
    uint64_t gm_l42; /* <tmp> */
    uint64_t gm_l43; /* i */
    GM_STACK_CHECK(&F, 4889u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t3 = gm_l0;
        gm_t88rcucg1b7uk t4 = gm_l1;
        gm_t53at80pfdus3 t5;
        gm_f_kind_ghvav22((void *)&t5, t3, GM_CONV(gm_csguankko4lmhd, t4));
        gm_t53at80pfdus3 t6 = t5;
        uint8_t t7 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
        if (t7 != ((uint8_t)17ULL)) goto ie2;
        uint32_t t8 = GM_LD(uint32_t, ((((uint8_t *)&t6) + 8) + 0));
        gm_l2 = t8;
        {
            gm_b16_8 t10;
            GM_ST(void *, (uint8_t *)&t10, (void *)(gm_sc49g68j2cekh));
            GM_ST(uintptr_t, (uint8_t *)&t10 + 8, (uintptr_t)(6));
            gm_l3 = t10;
        }
        {
            gm_b16_8 t11 = gm_f_empty_slice_ose1ba9();
            gm_b16_8 t12 = t11;
            gm_b16_8 t13 = GM_LD(gm_b16_8, ((uint8_t *)&t12));
            gm_l4 = t13;
        }
        {
            gm_l5 = ((uint8_t)0);
        }
        {
            gm_tf5srh4mp4b3i t14;
            memset(&t14, 0, sizeof t14);
            gm_l6 = t14;
        }
        {
            uint32_t t15 = gm_l2;
            uint64_t t16 = (uint64_t)t15;
            uint64_t t17 = (uintptr_t)t16;
            void * t18 = gm_l0;
            void * t19 = GM_LD(void *, (((uint8_t *)t18) + 0));
            uint64_t t20 = GM_LD(uintptr_t, ((((uint8_t *)t19) + 1088) + 0) + 8);
            if (t17 >= (uintptr_t)(t20)) gm_trap_index(t17, t20, gm_srckfibkpt681aa, 351, 11);
            uint8_t t26 = GM_LD(uint8_t, ((((uint8_t *)GM_LD(void *, ((((uint8_t *)t19) + 1088) + 0)) + t17 * 184) + 24) + 0));
            switch (t26) { case ((uint8_t)0ULL): goto ma22; case ((uint8_t)1ULL): goto ma24; case ((uint8_t)2ULL): goto ma23; default: goto ma25; }
ma22:;
            uint8_t t28 = GM_LD(uint8_t, ((((uint8_t *)GM_LD(void *, ((((uint8_t *)t19) + 1088) + 0)) + t17 * 184) + 24) + 0));
            if (t28 != ((uint8_t)0ULL)) goto mn27;
            gm_tpautr4br7h3l t29 = GM_LD(gm_tpautr4br7h3l, ((((uint8_t *)GM_LD(void *, ((((uint8_t *)t19) + 1088) + 0)) + t17 * 184) + 24) + 8));
            F.l7 = t29;
            {
                if (!(((((uint8_t *)&F.l7) + 40))[4] != 0)) goto ie31;
                gm_t88rcucg1b7uk t32 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l7) + 40));
                gm_l8 = t32;
                {
                    void * t34 = gm_l0;
                    gm_t88rcucg1b7uk t35 = gm_l8;
                    gm_b16_8 t36 = gm_f_cty_66ee0kf((void *)NULL, cx, NULL, t34, t35);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_th2lqbo77pvsc t37;
                    GM_ST(uint8_t, (uint8_t *)&t37 + 17, 0);
                    GM_ST(uint16_t, (uint8_t *)&t37 + 18, 0);
                    GM_ST(uint32_t, (uint8_t *)&t37 + 20, 0);
                    GM_ST(gm_b16_8, ((uint8_t *)&t37), t36);
                    ((uint8_t *)&t37)[16] = 1;
                    uint8_t t38 = 0;
                    F.ret = t37;
                    goto gm_fret;
                }
                goto ix31;
ie31:;
ix31:;
            }
            {
                gm_b16_8 t39 = GM_LD(gm_b16_8, (((uint8_t *)&F.l7) + 0));
                gm_b16_8 t40 = t39;
                gm_b16_8 t41 = GM_LD(gm_b16_8, ((uint8_t *)&t40));
                gm_l4 = t41;
            }
            {
                uint8_t t42 = GM_LD(uint8_t, (((uint8_t *)&F.l7) + 16));
                gm_l5 = t42;
            }
            {
                gm_tf5srh4mp4b3i t43 = GM_LD(gm_tf5srh4mp4b3i, (((uint8_t *)&F.l7) + 24));
                gm_l6 = t43;
            }
            goto me21;
mn27:;
ma23:;
            uint8_t t45 = GM_LD(uint8_t, ((((uint8_t *)GM_LD(void *, ((((uint8_t *)t19) + 1088) + 0)) + t17 * 184) + 24) + 0));
            if (t45 != ((uint8_t)2ULL)) goto mn44;
            gm_b16_8 t46 = GM_LD(gm_b16_8, ((((uint8_t *)GM_LD(void *, ((((uint8_t *)t19) + 1088) + 0)) + t17 * 184) + 24) + 8));
            gm_l9 = t46;
            {
                gm_b16_8 t48;
                GM_ST(void *, (uint8_t *)&t48, (void *)(gm_s8p1h96ji9kh0));
                GM_ST(uintptr_t, (uint8_t *)&t48 + 8, (uintptr_t)(5));
                gm_l3 = t48;
            }
            {
                gm_b16_8 t49 = gm_l9;
                gm_b16_8 t50 = t49;
                gm_b16_8 t51 = GM_LD(gm_b16_8, ((uint8_t *)&t50));
                gm_l4 = t51;
            }
            goto me21;
mn44:;
ma24:;
            uint8_t t53 = GM_LD(uint8_t, ((((uint8_t *)GM_LD(void *, ((((uint8_t *)t19) + 1088) + 0)) + t17 * 184) + 24) + 0));
            if (t53 != ((uint8_t)1ULL)) goto mn52;
            gm_th2lqbo77pvsc t54;
            memset(&t54, 0, sizeof t54);
            uint8_t t55 = 0;
            F.ret = t54;
            goto gm_fret;
            goto me21;
mn52:;
ma25:;
            gm_trap("no match arm matched the scrutinee", gm_srckfibkpt681aa, 351, 5);
me21:;
        }
        {
            uint8_t t57 = 0;
            uint64_t t58 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
            uint8_t t59 = ((t58 == ((uint64_t)0ULL)));
            if (!t59) {
                void * t60 = gm_l0;
                gm_t88rcucg1b7uk t61 = gm_l1;
                uint64_t t62 = gm_f_size_dmhtkib((void *)NULL, cx, NULL, t60, t61);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t63 = ((t62 == ((uint64_t)0ULL)));
                t57 = t63;
            } else t57 = 1;
            if (!t57) goto ie56;
            {
                gm_th2lqbo77pvsc t65;
                memset(&t65, 0, sizeof t65);
                uint8_t t66 = 0;
                F.ret = t65;
                goto gm_fret;
            }
            goto ix56;
ie56:;
ix56:;
        }
        {
            gm_f_list_epvducg((void *)&F.l10, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t68 = gm_l0;
            uint8_t t69 = GM_LD(uint8_t, (((uint8_t *)t68) + 808));
            if (!t69) goto ie67;
            {
                gm_f_list_epvducg((void *)&F.l11, (void *)NULL, cx, NULL);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                {
                    gm_b16_8 t72 = gm_l4;
                    gm_l12 = t72;
                }
                {
                    gm_l13 = ((uint64_t)0ULL);
                }
                {
                    for (;;) {
                        uint64_t t74 = gm_l13;
                        uint64_t t75 = GM_LD(uintptr_t, ((uint8_t *)&gm_l12) + 8);
                        uint8_t t76 = (t74 < t75);
                        if (!t76) goto lb73;
                        {
                            uint64_t t79 = gm_l13;
                            uint64_t t80 = (uintptr_t)t79;
                            uint64_t t81 = GM_LD(uintptr_t, ((uint8_t *)&gm_l12) + 8);
                            gm_t0mglksrkfa5e t84 = GM_LD(gm_t0mglksrkfa5e, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l12)) + t80 * 40));
                            F.l14 = t84;
                            {
                                void * t86 = (void *)((uint8_t *)&F.l11);
                                gm_b16_8 t87 = GM_LD(gm_b16_8, (((uint8_t *)&F.l14) + 0));
                                gm_tftb1odc739t5 t88 = gm_f_push_3ef746b(t86, t87);
                                gm_tftb1odc739t5 t89 = t88;
                                if (((((uint8_t *)&t89))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t89) + 1, gm_srckfibkpt681aa, 369, 27);
                                gm_b0_1 t90 = GM_LD(gm_b0_1, (((uint8_t *)&t89) + 1));
                            }
                            goto me82;
mn83:;
                            gm_trap("no match arm matched the scrutinee", gm_srckfibkpt681aa, 369, 9);
me82:;
                        }
lc73:;
                        {
                            uint64_t t92 = gm_l13;
                            uint64_t r93; bool o94 = __builtin_add_overflow(t92, ((uint64_t)1ULL), &r93);
                            uint64_t t95 = r93;
                            gm_l13 = t95;
                        }
                    }
lb73:;
                }
            }
            {
                gm_f_hash_map_cuh4fs9((void *)&F.l15, (void *)NULL, cx, NULL);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                {
                    gm_b16_8 t97 = GM_LD(gm_b16_8, (((uint8_t *)&F.l11) + 0));
                    gm_b16_8 t98 = t97;
                    gm_b16_8 t99 = GM_LD(gm_b16_8, ((uint8_t *)&t98));
                    void * t100 = (void *)((uint8_t *)&F.l15);
                    gm_b16_8 t101 = gm_f_unique_members_mrojmjf((void *)NULL, cx, NULL, t99, t100);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_b16_8 t102 = t101;
                    gm_b16_8 t103 = GM_LD(gm_b16_8, ((uint8_t *)&t102));
                    gm_l16 = t103;
                }
                {
                    gm_l17 = ((uint64_t)0ULL);
                }
                {
                    for (;;) {
                        uint64_t t105 = gm_l17;
                        uint64_t t106 = GM_LD(uintptr_t, ((uint8_t *)&gm_l16) + 8);
                        uint8_t t107 = (t105 < t106);
                        if (!t107) goto lb104;
                        {
                            gm_t13urt52695sh t110;
                            uint64_t t111 = gm_l17;
                            GM_ST(uint64_t, (uint8_t *)&t110 + 0, t111);
                            uint64_t t112 = gm_l17;
                            uint64_t t113 = (uintptr_t)t112;
                            uint64_t t114 = GM_LD(uintptr_t, ((uint8_t *)&gm_l16) + 8);
                            memcpy((uint8_t *)&t110 + 8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l16)) + t113 * 24), 24);
                            gm_t13urt52695sh t115 = t110;
                            uint64_t t118 = GM_LD(uint64_t, (((uint8_t *)&t115) + 0));
                            gm_l18 = t118;
                            gm_th2lqbo77pvsc t119 = GM_LD(gm_th2lqbo77pvsc, (((uint8_t *)&t115) + 8));
                            gm_l19 = t119;
                            {
                                void * t121 = (void *)((uint8_t *)&F.l10);
                                gm_th2lqbo77pvsc t122 = gm_l19;
                                gm_th2lqbo77pvsc t123 = t122;
                                gm_b16_8 t124;
                                if (((((uint8_t *)&t123))[16] != 0)) {
                                    gm_b16_8 t125 = GM_LD(gm_b16_8, (((uint8_t *)&t123) + 0));
                                    t124 = t125;
                                } else {
                                    uint64_t t126 = gm_l18;
                                    gm_t4ce0jd3tdro4 t127;
                                    GM_ST(uint64_t, ((uint8_t *)&t127 + 0), t126);
                                    gm_tabb710d9mq2e t128;
                                    gm_f_format_tc7tn98((void *)&t128, (void *)NULL, cx, NULL, t127);
                                    if (gm_unw.kind) {
                                        goto gm_ll108;
                                    }
                                    gm_tabb710d9mq2e t129 = t128;
                                    if (((((uint8_t *)&t129))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t129) + 8, gm_srckfibkpt681aa, 371, 94);
                                    gm_b16_8 t130 = GM_LD(gm_b16_8, (((uint8_t *)&t129) + 8));
                                    gm_b16_8 t131 = t130;
                                    gm_b16_8 t132 = GM_LD(gm_b16_8, ((uint8_t *)&t131));
                                    t124 = t132;
                                }
                                gm_tftb1odc739t5 t133 = gm_f_push_3ef746b(t121, t124);
                                gm_tftb1odc739t5 t134 = t133;
                                if (((((uint8_t *)&t134))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t134) + 1, gm_srckfibkpt681aa, 371, 76);
                                gm_b0_1 t135 = GM_LD(gm_b0_1, (((uint8_t *)&t134) + 1));
                            }
                            goto me116;
mn117:;
                            gm_trap("no match arm matched the scrutinee", gm_srckfibkpt681aa, 371, 9);
me116:;
                        }
lc104:;
                        {
                            uint64_t t137 = gm_l17;
                            uint64_t r138; bool o139 = __builtin_add_overflow(t137, ((uint64_t)1ULL), &r138);
                            uint64_t t140 = r138;
                            gm_l17 = t140;
                        }
                    }
gm_ll108:;
                    if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb104; }
                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc104; }
                    }
                    goto gm_lad;
lb104:;
                }
            }
            goto ix67;
ie67:;
            {
                {
                    gm_l21 = ((uint64_t)0ULL);
                }
                {
                    uint64_t t143 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                    gm_l20 = t143;
                }
                {
                    for (;;) {
                        uint64_t t145 = gm_l21;
                        uint64_t t146 = gm_l20;
                        uint8_t t147 = (t145 < t146);
                        if (!t147) goto lb144;
                        {
                            uint64_t t150 = gm_l21;
                            uint64_t t151 = t150;
                            uint64_t t154 = GM_LD(uint64_t, ((uint8_t *)&t151));
                            gm_l22 = t154;
                            {
                                void * t156 = (void *)((uint8_t *)&F.l10);
                                uint64_t t157 = gm_l22;
                                gm_t4ce0jd3tdro4 t158;
                                GM_ST(uint64_t, ((uint8_t *)&t158 + 0), t157);
                                gm_tabb710d9mq2e t159;
                                gm_f_format_tc7tn98((void *)&t159, (void *)NULL, cx, NULL, t158);
                                if (gm_unw.kind) {
                                    goto gm_ll148;
                                }
                                gm_tabb710d9mq2e t160 = t159;
                                if (((((uint8_t *)&t160))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t160) + 8, gm_srckfibkpt681aa, 373, 45);
                                gm_b16_8 t161 = GM_LD(gm_b16_8, (((uint8_t *)&t160) + 8));
                                gm_b16_8 t162 = t161;
                                gm_b16_8 t163 = GM_LD(gm_b16_8, ((uint8_t *)&t162));
                                gm_tftb1odc739t5 t164 = gm_f_push_3ef746b(t156, t163);
                                gm_tftb1odc739t5 t165 = t164;
                                if (((((uint8_t *)&t165))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t165) + 1, gm_srckfibkpt681aa, 373, 34);
                                gm_b0_1 t166 = GM_LD(gm_b0_1, (((uint8_t *)&t165) + 1));
                            }
                            goto me152;
mn153:;
                            gm_trap("no match arm matched the scrutinee", gm_srckfibkpt681aa, 373, 9);
me152:;
                        }
lc144:;
                        {
                            uint64_t t168 = gm_l21;
                            uint64_t r169; bool o170 = __builtin_add_overflow(t168, ((uint64_t)1ULL), &r169);
                            uint64_t t171 = r169;
                            gm_l21 = t171;
                        }
                    }
gm_ll148:;
                    if (gm_unw.frame == &F.h && gm_unw.label == 2) {
                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb144; }
                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc144; }
                    }
                    goto gm_lad;
lb144:;
                }
            }
ix67:;
        }
        {
            gm_f_list_epvducg((void *)&F.l23, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            {
                gm_b16_8 t173 = gm_l4;
                gm_l24 = t173;
            }
            {
                gm_l25 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t175 = gm_l25;
                    uint64_t t176 = GM_LD(uintptr_t, ((uint8_t *)&gm_l24) + 8);
                    uint8_t t177 = (t175 < t176);
                    if (!t177) goto lb174;
                    {
                        gm_t8ta482bl6be0 t180;
                        uint64_t t181 = gm_l25;
                        GM_ST(uint64_t, (uint8_t *)&t180 + 0, t181);
                        uint64_t t182 = gm_l25;
                        uint64_t t183 = (uintptr_t)t182;
                        uint64_t t184 = GM_LD(uintptr_t, ((uint8_t *)&gm_l24) + 8);
                        memcpy((uint8_t *)&t180 + 8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l24)) + t183 * 40), 40);
                        gm_t8ta482bl6be0 t185 = t180;
                        uint64_t t188 = GM_LD(uint64_t, (((uint8_t *)&t185) + 0));
                        gm_l26 = t188;
                        gm_t0mglksrkfa5e t189 = GM_LD(gm_t0mglksrkfa5e, (((uint8_t *)&t185) + 8));
                        F.l27 = t189;
                        {
                            void * t191 = gm_l0;
                            gm_t88rcucg1b7uk t192 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l27) + 16));
                            gm_ttu09lij36f8c t193;
                            gm_f_field_cty_nfj8rcv((void *)&t193, (void *)NULL, cx, NULL, t191, t192);
                            if (gm_unw.kind) {
                                goto gm_ll178;
                            }
                            gm_ttu09lij36f8c t194 = t193;
                            gm_t6j6smj7qoctu t195;
                            if (((((uint8_t *)&t194))[32] != 0)) {
                                gm_t6j6smj7qoctu t196 = GM_LD(gm_t6j6smj7qoctu, (((uint8_t *)&t194) + 0));
                                t195 = t196;
                            } else {
                                gm_th2lqbo77pvsc t197;
                                memset(&t197, 0, sizeof t197);
                                uint8_t t198 = 0;
                                F.ret = t197;
                                goto gm_fret;
                            }
                            gm_l28 = t195;
                        }
                        {
                            gm_b16_8 t199;
                            if (!(((((uint8_t *)&F.l27) + 24))[8] != 0)) goto ie200;
                            uint64_t t201 = GM_LD(uint64_t, (((uint8_t *)&F.l27) + 24));
                            gm_l29 = t201;
                            uint64_t t203 = gm_l29;
                            gm_t4ce0jd3tdro4 t204;
                            GM_ST(uint64_t, ((uint8_t *)&t204 + 0), t203);
                            gm_tabb710d9mq2e t205;
                            gm_f_format_irg2n6e((void *)&t205, (void *)NULL, cx, NULL, t204);
                            if (gm_unw.kind) {
                                goto gm_ll178;
                            }
                            gm_tabb710d9mq2e t206 = t205;
                            if (((((uint8_t *)&t206))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t206) + 8, gm_srckfibkpt681aa, 378, 40);
                            gm_b16_8 t207 = GM_LD(gm_b16_8, (((uint8_t *)&t206) + 8));
                            gm_b16_8 t208 = t207;
                            gm_b16_8 t209 = t208;
                            gm_b16_8 t210 = GM_LD(gm_b16_8, ((uint8_t *)&t209));
                            t199 = t210;
                            goto ix200;
ie200:;
                            gm_b16_8 t212;
                            GM_ST(void *, (uint8_t *)&t212, (void *)(gm_sr4qbrk6tlgaa));
                            GM_ST(uintptr_t, (uint8_t *)&t212 + 8, (uintptr_t)(0));
                            gm_b16_8 t213 = t212;
                            t199 = t213;
ix200:;
                            gm_l30 = t199;
                        }
                        {
                            void * t214 = (void *)((uint8_t *)&F.l23);
                            gm_t0uuhaljsf410 t215;
                            gm_b16_8 t216 = gm_l30;
                            GM_ST(gm_b16_8, (uint8_t *)&t215 + 0, t216);
                            gm_b16_8 t217 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l28) + 0));
                            GM_ST(gm_b16_8, (uint8_t *)&t215 + 16, t217);
                            uint64_t t218 = gm_l26;
                            uint64_t t219 = (uintptr_t)t218;
                            uint64_t t220 = GM_LD(uintptr_t, (((uint8_t *)&F.l10) + 0) + 8);
                            if (t219 >= (uintptr_t)(t220)) gm_trap_index(t219, t220, gm_srckfibkpt681aa, 379, 59);
                            gm_b16_8 t221 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l10) + 0)) + t219 * 16));
                            GM_ST(gm_b16_8, (uint8_t *)&t215 + 32, t221);
                            gm_b16_8 t222 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l28) + 16));
                            GM_ST(gm_b16_8, (uint8_t *)&t215 + 48, t222);
                            gm_tabb710d9mq2e t223;
                            gm_f_format_tra8sop((void *)&t223, (void *)NULL, cx, NULL, &t215);
                            if (gm_unw.kind) {
                                goto gm_ll178;
                            }
                            gm_tabb710d9mq2e t224 = t223;
                            if (((((uint8_t *)&t224))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t224) + 8, gm_srckfibkpt681aa, 379, 22);
                            gm_b16_8 t225 = GM_LD(gm_b16_8, (((uint8_t *)&t224) + 8));
                            gm_b16_8 t226 = t225;
                            gm_b16_8 t227 = GM_LD(gm_b16_8, ((uint8_t *)&t226));
                            gm_tftb1odc739t5 t228 = gm_f_push_3ef746b(t214, t227);
                            gm_tftb1odc739t5 t229 = t228;
                            if (((((uint8_t *)&t229))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t229) + 1, gm_srckfibkpt681aa, 379, 9);
                            gm_b0_1 t230 = GM_LD(gm_b0_1, (((uint8_t *)&t229) + 1));
                        }
                        goto me186;
mn187:;
                        gm_trap("no match arm matched the scrutinee", gm_srckfibkpt681aa, 376, 5);
me186:;
                    }
lc174:;
                    {
                        uint64_t t232 = gm_l25;
                        uint64_t r233; bool o234 = __builtin_add_overflow(t232, ((uint64_t)1ULL), &r233);
                        uint64_t t235 = r233;
                        gm_l25 = t235;
                    }
                }
gm_ll178:;
                if (gm_unw.frame == &F.h && gm_unw.label == 3) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb174; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc174; }
                }
                goto gm_lad;
lb174:;
            }
        }
        {
            void * t237 = gm_l0;
            uint8_t t238 = GM_LD(uint8_t, (((uint8_t *)t237) + 808));
            if (!t238) goto ie236;
            {
                {
                    gm_b16_8 t241 = GM_LD(gm_b16_8, (((uint8_t *)&F.l10) + 0));
                    gm_l31 = t241;
                }
                {
                    gm_l32 = ((uint64_t)0ULL);
                }
                {
                    for (;;) {
                        uint64_t t243 = gm_l32;
                        uint64_t t244 = GM_LD(uintptr_t, ((uint8_t *)&gm_l31) + 8);
                        uint8_t t245 = (t243 < t244);
                        if (!t245) goto lb242;
                        {
                            uint64_t t248 = gm_l32;
                            uint64_t t249 = (uintptr_t)t248;
                            uint64_t t250 = GM_LD(uintptr_t, ((uint8_t *)&gm_l31) + 8);
                            gm_b16_8 t253 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l31)) + t249 * 16));
                            gm_l33 = t253;
                            {
                                gm_b16_8 t256 = gm_l33;
                                gm_th2lqbo77pvsc t257;
                                gm_f_member_name_mj2bvs3((void *)&t257, (void *)NULL, cx, NULL, t256);
                                if (gm_unw.kind) {
                                    goto gm_ll246;
                                }
                                gm_th2lqbo77pvsc t258 = t257;
                                uint8_t t259;
                                if (((((uint8_t *)&t258))[16] != 0)) goto mn261;
                                t259 = ((uint8_t)0);
                                goto me260;
mn261:;
                                t259 = ((uint8_t)1);
                                goto me260;
mn262:;
                                gm_trap("no match arm matched the scrutinee", gm_srckfibkpt681aa, 383, 16);
me260:;
                                if (!t259) goto ie255;
                                {
                                    void * t264 = gm_l0;
                                    gm_b16_8 t265 = gm_l33;
                                    gm_b0_1 t266 = gm_f_add_member_name_j3i0eje((void *)NULL, cx, NULL, t264, t265);
                                    if (gm_unw.kind) {
                                        goto gm_ll246;
                                    }
                                }
                                goto ix255;
ie255:;
ix255:;
                            }
                            goto me251;
mn252:;
                            gm_trap("no match arm matched the scrutinee", gm_srckfibkpt681aa, 382, 9);
me251:;
                        }
lc242:;
                        {
                            uint64_t t268 = gm_l32;
                            uint64_t r269; bool o270 = __builtin_add_overflow(t268, ((uint64_t)1ULL), &r269);
                            uint64_t t271 = r269;
                            gm_l32 = t271;
                        }
                    }
gm_ll246:;
                    if (gm_unw.frame == &F.h && gm_unw.label == 4) {
                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb242; }
                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc242; }
                    }
                    goto gm_lad;
lb242:;
                }
            }
            goto ix236;
ie236:;
ix236:;
        }
        {
            gm_f_list_epvducg((void *)&F.l34, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            uint8_t t273 = gm_l5;
            if (!t273) goto ie272;
            {
                void * t275 = (void *)((uint8_t *)&F.l34);
                gm_b16_8 t276;
                GM_ST(void *, (uint8_t *)&t276, (void *)(gm_s2ph9f020upd2));
                GM_ST(uintptr_t, (uint8_t *)&t276 + 8, (uintptr_t)(6));
                gm_tftb1odc739t5 t277 = gm_f_push_3ef746b(t275, t276);
                gm_tftb1odc739t5 t278 = t277;
                if (((((uint8_t *)&t278))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t278) + 1, gm_srckfibkpt681aa, 387, 17);
                gm_b0_1 t279 = GM_LD(gm_b0_1, (((uint8_t *)&t278) + 1));
            }
            goto ix272;
ie272:;
ix272:;
        }
        {
            if (!((((uint8_t *)&gm_l6))[8] != 0)) goto ie280;
            uint64_t t281 = GM_LD(uint64_t, ((uint8_t *)&gm_l6));
            gm_l35 = t281;
            {
                void * t283 = (void *)((uint8_t *)&F.l34);
                uint64_t t284 = gm_l35;
                gm_t4ce0jd3tdro4 t285;
                GM_ST(uint64_t, ((uint8_t *)&t285 + 0), t284);
                gm_tabb710d9mq2e t286;
                gm_f_format_mfspujt((void *)&t286, (void *)NULL, cx, NULL, t285);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t287 = t286;
                if (((((uint8_t *)&t287))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t287) + 8, gm_srckfibkpt681aa, 388, 35);
                gm_b16_8 t288 = GM_LD(gm_b16_8, (((uint8_t *)&t287) + 8));
                gm_b16_8 t289 = t288;
                gm_b16_8 t290 = GM_LD(gm_b16_8, ((uint8_t *)&t289));
                gm_tftb1odc739t5 t291 = gm_f_push_3ef746b(t283, t290);
                gm_tftb1odc739t5 t292 = t291;
                if (((((uint8_t *)&t292))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t292) + 1, gm_srckfibkpt681aa, 388, 24);
                gm_b0_1 t293 = GM_LD(gm_b0_1, (((uint8_t *)&t292) + 1));
            }
            goto ix280;
ie280:;
ix280:;
        }
        {
            gm_b16_8 t294;
            uint64_t t296 = GM_LD(uintptr_t, (((uint8_t *)&F.l34) + 0) + 8);
            uint8_t t297 = ((t296 == ((uint64_t)0ULL)));
            if (!t297) goto ie295;
            gm_b16_8 t299;
            GM_ST(void *, (uint8_t *)&t299, (void *)(gm_sr4qbrk6tlgaa));
            GM_ST(uintptr_t, (uint8_t *)&t299 + 8, (uintptr_t)(0));
            gm_b16_8 t300 = t299;
            t294 = t300;
            goto ix295;
ie295:;
            gm_b16_8 t302 = GM_LD(gm_b16_8, (((uint8_t *)&F.l34) + 0));
            gm_b16_8 t303 = t302;
            gm_b16_8 t304 = GM_LD(gm_b16_8, ((uint8_t *)&t303));
            gm_b16_8 t305;
            GM_ST(void *, (uint8_t *)&t305, (void *)(gm_sg5gdnrith2i6));
            GM_ST(uintptr_t, (uint8_t *)&t305 + 8, (uintptr_t)(2));
            gm_tabb710d9mq2e t306;
            gm_f_join_sfknljb((void *)&t306, (void *)NULL, cx, NULL, t304, t305);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t307 = t306;
            if (((((uint8_t *)&t307))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t307) + 8, gm_srckfibkpt681aa, 389, 98);
            gm_b16_8 t308 = GM_LD(gm_b16_8, (((uint8_t *)&t307) + 8));
            gm_tnpifv20irnph t309;
            GM_ST(gm_b16_8, ((uint8_t *)&t309 + 0), t308);
            gm_tabb710d9mq2e t310;
            gm_f_format_p144a0o((void *)&t310, (void *)NULL, cx, NULL, t309);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t311 = t310;
            if (((((uint8_t *)&t311))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t311) + 8, gm_srckfibkpt681aa, 389, 63);
            gm_b16_8 t312 = GM_LD(gm_b16_8, (((uint8_t *)&t311) + 8));
            gm_b16_8 t313 = t312;
            gm_b16_8 t314 = t313;
            gm_b16_8 t315 = GM_LD(gm_b16_8, ((uint8_t *)&t314));
            t294 = t315;
ix295:;
            gm_l36 = t294;
        }
        {
            gm_tfeab279c16rr t316;
            gm_b16_8 t317 = gm_l3;
            GM_ST(gm_b16_8, (uint8_t *)&t316 + 0, t317);
            gm_b16_8 t318 = gm_l36;
            GM_ST(gm_b16_8, (uint8_t *)&t316 + 16, t318);
            gm_b16_8 t319 = GM_LD(gm_b16_8, (((uint8_t *)&F.l23) + 0));
            gm_b16_8 t320 = t319;
            gm_b16_8 t321 = GM_LD(gm_b16_8, ((uint8_t *)&t320));
            gm_b16_8 t322;
            GM_ST(void *, (uint8_t *)&t322, (void *)(gm_s3p0rbs9hc3lg));
            GM_ST(uintptr_t, (uint8_t *)&t322 + 8, (uintptr_t)(1));
            gm_tabb710d9mq2e t323;
            gm_f_join_sfknljb((void *)&t323, (void *)NULL, cx, NULL, t321, t322);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t324 = t323;
            if (((((uint8_t *)&t324))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t324) + 8, gm_srckfibkpt681aa, 391, 58);
            gm_b16_8 t325 = GM_LD(gm_b16_8, (((uint8_t *)&t324) + 8));
            GM_ST(gm_b16_8, (uint8_t *)&t316 + 32, t325);
            gm_tabb710d9mq2e t326;
            gm_f_format_19e6va7((void *)&t326, (void *)NULL, cx, NULL, &t316);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t327 = t326;
            if (((((uint8_t *)&t327))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t327) + 8, gm_srckfibkpt681aa, 391, 16);
            gm_b16_8 t328 = GM_LD(gm_b16_8, (((uint8_t *)&t327) + 8));
            gm_l37 = t328;
        }
        {
            void * t330 = gm_l0;
            void * t331 = (void *)(((uint8_t *)t330) + 656);
            gm_b16_8 t332 = gm_l37;
            gm_b16_8 t333 = t332;
            gm_b16_8 t334 = GM_LD(gm_b16_8, ((uint8_t *)&t333));
            gm_th2lqbo77pvsc t335;
            gm_f_get_e3joqcc((void *)&t335, t331, t334);
            gm_th2lqbo77pvsc t336 = t335;
            if (!((((uint8_t *)&t336))[16] != 0)) goto ie329;
            gm_b16_8 t337 = GM_LD(gm_b16_8, ((uint8_t *)&t336));
            gm_l38 = t337;
            {
                gm_b16_8 t339 = gm_l38;
                gm_th2lqbo77pvsc t340;
                GM_ST(uint8_t, (uint8_t *)&t340 + 17, 0);
                GM_ST(uint16_t, (uint8_t *)&t340 + 18, 0);
                GM_ST(uint32_t, (uint8_t *)&t340 + 20, 0);
                GM_ST(gm_b16_8, ((uint8_t *)&t340), t339);
                ((uint8_t *)&t340)[16] = 1;
                uint8_t t341 = 0;
                F.ret = t340;
                goto gm_fret;
            }
            goto ix329;
ie329:;
ix329:;
        }
        {
            gm_b16_8 t342;
            GM_ST(void *, (uint8_t *)&t342, (void *)(gm_scttf7jtr4fn9));
            GM_ST(uintptr_t, (uint8_t *)&t342 + 8, (uintptr_t)(5));
            gm_b16_8 t343 = gm_l37;
            gm_b16_8 t344 = t343;
            gm_b16_8 t345 = GM_LD(gm_b16_8, ((uint8_t *)&t344));
            gm_b16_8 t346 = gm_f_content_name_83mn5uf((void *)NULL, cx, NULL, t342, t345);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l39 = t346;
        }
        {
            void * t347 = gm_l0;
            void * t348 = (void *)(((uint8_t *)t347) + 656);
            gm_b16_8 t349 = gm_l37;
            gm_b16_8 t350 = t349;
            gm_b16_8 t351 = GM_LD(gm_b16_8, ((uint8_t *)&t350));
            gm_b16_8 t352 = gm_l39;
            gm_tftb1odc739t5 t353 = gm_f_put_7shfu9u(t348, t351, t352);
            gm_tftb1odc739t5 t354 = t353;
            if (((((uint8_t *)&t354))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t354) + 1, gm_srckfibkpt681aa, 394, 5);
            gm_b0_1 t355 = GM_LD(gm_b0_1, (((uint8_t *)&t354) + 1));
        }
        {
            void * t356 = gm_l0;
            void * t357 = (void *)(((uint8_t *)t356) + 712);
            gm_tud7oovfvnf43 t358;
            gm_b16_8 t359 = gm_l3;
            GM_ST(gm_b16_8, (uint8_t *)&t358 + 0, t359);
            gm_b16_8 t360 = gm_l36;
            GM_ST(gm_b16_8, (uint8_t *)&t358 + 16, t360);
            gm_b16_8 t361 = gm_l39;
            GM_ST(gm_b16_8, (uint8_t *)&t358 + 32, t361);
            gm_b16_8 t362 = GM_LD(gm_b16_8, (((uint8_t *)&F.l23) + 0));
            gm_b16_8 t363 = t362;
            gm_b16_8 t364 = GM_LD(gm_b16_8, ((uint8_t *)&t363));
            gm_b16_8 t365;
            GM_ST(void *, (uint8_t *)&t365, (void *)(gm_s3p0rbs9hc3lg));
            GM_ST(uintptr_t, (uint8_t *)&t365 + 8, (uintptr_t)(1));
            gm_tabb710d9mq2e t366;
            gm_f_join_sfknljb((void *)&t366, (void *)NULL, cx, NULL, t364, t365);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t367 = t366;
            if (((((uint8_t *)&t367))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t367) + 8, gm_srckfibkpt681aa, 395, 86);
            gm_b16_8 t368 = GM_LD(gm_b16_8, (((uint8_t *)&t367) + 8));
            GM_ST(gm_b16_8, (uint8_t *)&t358 + 48, t368);
            gm_b16_8 t369 = gm_l39;
            GM_ST(gm_b16_8, (uint8_t *)&t358 + 64, t369);
            gm_tftb1odc739t5 t370 = gm_f_write_712qp7n(t357, &t358);
            gm_tftb1odc739t5 t371 = t370;
            if (((((uint8_t *)&t371))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t371) + 1, gm_srckfibkpt681aa, 395, 5);
            gm_b0_1 t372 = GM_LD(gm_b0_1, (((uint8_t *)&t371) + 1));
        }
        {
            void * t373 = gm_l0;
            void * t374 = (void *)(((uint8_t *)t373) + 712);
            gm_thddbqb6snabm t375;
            gm_b16_8 t376 = gm_l39;
            GM_ST(gm_b16_8, (uint8_t *)&t375 + 0, t376);
            void * t377 = gm_l0;
            gm_t88rcucg1b7uk t378 = gm_l1;
            uint64_t t379 = gm_f_size_dmhtkib((void *)NULL, cx, NULL, t377, t378);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(uint64_t, (uint8_t *)&t375 + 16, t379);
            gm_b16_8 t380 = gm_l39;
            GM_ST(gm_b16_8, (uint8_t *)&t375 + 24, t380);
            void * t381 = gm_l0;
            gm_t88rcucg1b7uk t382 = gm_l1;
            uint64_t t383 = gm_f_align_uu2harl((void *)NULL, cx, NULL, t381, t382);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(uint64_t, (uint8_t *)&t375 + 40, t383);
            gm_tftb1odc739t5 t384 = gm_f_write_cscq5c8(t374, &t375);
            gm_tftb1odc739t5 t385 = t384;
            if (((((uint8_t *)&t385))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t385) + 1, gm_srckfibkpt681aa, 397, 5);
            gm_b0_1 t386 = GM_LD(gm_b0_1, (((uint8_t *)&t385) + 1));
        }
        {
            void * t387 = gm_l0;
            gm_t88rcucg1b7uk t388 = gm_l1;
            gm_b16_8 t389 = gm_f_offsets_eh6vfle((void *)NULL, cx, NULL, t387, t388);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l40 = t389;
        }
        {
            {
                gm_l42 = ((uint64_t)0ULL);
            }
            {
                uint64_t t391 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                gm_l41 = t391;
            }
            {
                for (;;) {
                    uint64_t t393 = gm_l42;
                    uint64_t t394 = gm_l41;
                    uint8_t t395 = (t393 < t394);
                    if (!t395) goto lb392;
                    {
                        uint64_t t398 = gm_l42;
                        uint64_t t399 = t398;
                        uint64_t t402 = GM_LD(uint64_t, ((uint8_t *)&t399));
                        gm_l43 = t402;
                        {
                            uint64_t t405 = gm_l43;
                            uint64_t t406 = GM_LD(uintptr_t, ((uint8_t *)&gm_l40) + 8);
                            uint8_t t407 = (t405 < t406);
                            if (!t407) goto ie404;
                            {
                                void * t409 = gm_l0;
                                void * t410 = (void *)(((uint8_t *)t409) + 712);
                                gm_tftksr4op114e t411;
                                gm_b16_8 t412 = gm_l39;
                                GM_ST(gm_b16_8, (uint8_t *)&t411 + 0, t412);
                                uint64_t t413 = gm_l43;
                                uint64_t t414 = (uintptr_t)t413;
                                uint64_t t415 = GM_LD(uintptr_t, (((uint8_t *)&F.l10) + 0) + 8);
                                if (t414 >= (uintptr_t)(t415)) gm_trap_index(t414, t415, gm_srckfibkpt681aa, 400, 84);
                                gm_b16_8 t416 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l10) + 0)) + t414 * 16));
                                GM_ST(gm_b16_8, (uint8_t *)&t411 + 16, t416);
                                uint64_t t417 = gm_l43;
                                uint64_t t418 = (uintptr_t)t417;
                                uint64_t t419 = GM_LD(uintptr_t, ((uint8_t *)&gm_l40) + 8);
                                if (t418 >= (uintptr_t)(t419)) gm_trap_index(t418, t419, gm_srckfibkpt681aa, 400, 100);
                                uint64_t t420 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l40)) + t418 * 8));
                                GM_ST(uint64_t, (uint8_t *)&t411 + 32, t420);
                                gm_tftb1odc739t5 t421 = gm_f_write_3f2sd55(t410, &t411);
                                gm_tftb1odc739t5 t422 = t421;
                                if (((((uint8_t *)&t422))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t422) + 1, gm_srckfibkpt681aa, 400, 27);
                                gm_b0_1 t423 = GM_LD(gm_b0_1, (((uint8_t *)&t422) + 1));
                            }
                            goto ix404;
ie404:;
ix404:;
                        }
                        goto me400;
mn401:;
                        gm_trap("no match arm matched the scrutinee", gm_srckfibkpt681aa, 399, 5);
me400:;
                    }
lc392:;
                    {
                        uint64_t t425 = gm_l42;
                        uint64_t r426; bool o427 = __builtin_add_overflow(t425, ((uint64_t)1ULL), &r426);
                        uint64_t t428 = r426;
                        gm_l42 = t428;
                    }
                }
lb392:;
            }
        }
        {
            void * t429 = gm_l0;
            void * t430 = (void *)(((uint8_t *)t429) + 712);
            void * t431 = gm_l0;
            gm_t88rcucg1b7uk t432 = gm_l1;
            gm_b16_8 t433 = gm_f_show_5qbi623((void *)NULL, cx, NULL, t431, t432);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tnpifv20irnph t434;
            GM_ST(gm_b16_8, ((uint8_t *)&t434 + 0), t433);
            gm_tabb710d9mq2e t435;
            gm_f_format_1cgbgnn((void *)&t435, (void *)NULL, cx, NULL, t434);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t436 = t435;
            if (((((uint8_t *)&t436))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t436) + 8, gm_srckfibkpt681aa, 402, 53);
            gm_b16_8 t437 = GM_LD(gm_b16_8, (((uint8_t *)&t436) + 8));
            gm_b16_8 t438 = t437;
            gm_b16_8 t439 = GM_LD(gm_b16_8, ((uint8_t *)&t438));
            gm_b16_8 t440 = gm_f_assert_message_6bd9l6u((void *)NULL, cx, NULL, t439);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tnpifv20irnph t441;
            GM_ST(gm_b16_8, ((uint8_t *)&t441 + 0), t440);
            gm_tftb1odc739t5 t442 = gm_f_write_pecnce5(t430, t441);
            gm_tftb1odc739t5 t443 = t442;
            if (((((uint8_t *)&t443))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t443) + 1, gm_srckfibkpt681aa, 402, 5);
            gm_b0_1 t444 = GM_LD(gm_b0_1, (((uint8_t *)&t443) + 1));
        }
        {
            gm_b16_8 t445 = gm_l39;
            gm_th2lqbo77pvsc t446;
            GM_ST(uint8_t, (uint8_t *)&t446 + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&t446 + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&t446 + 20, 0);
            GM_ST(gm_b16_8, ((uint8_t *)&t446), t445);
            ((uint8_t *)&t446)[16] = 1;
            uint8_t t447 = 0;
            F.ret = t446;
            goto gm_fret;
        }
        goto ix2;
ie2:;
        {
            gm_th2lqbo77pvsc t449;
            memset(&t449, 0, sizeof t449);
            uint8_t t450 = 0;
            F.ret = t449;
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
__attribute__((aligned(4))) void gm_f_make_typed_2bh0dpj(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_make_typed_2bh0dpj F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    uint64_t gm_l3; /* len */
    gm_t88rcucg1b7uk gm_l4; /* elem */
    gm_b16_8 gm_l5; /* ec */
    gm_b16_8 gm_l6; /* members */
    gm_b16_8 gm_l8; /* elems */
    gm_b16_8 gm_l9; /* offs */
    gm_b16_8 gm_l10; /* <tmp> */
    uint64_t gm_l11; /* <tmp> */
    uint64_t gm_l12; /* i */
    gm_t88rcucg1b7uk gm_l13; /* ft */
    gm_t88rcucg1b7uk gm_l14; /* inner */
    gm_b16_8 gm_l15; /* offs */
    gm_t88rcucg1b7uk gm_l16; /* err */
    gm_t88rcucg1b7uk gm_l17; /* ok */
    gm_b16_8 gm_l18; /* offs */
    gm_tqsk8dt6ib3qq gm_l19; /* pair */
    gm_tgbgti82mf879 gm_l20; /* p */
    uint32_t gm_l21; /* id */
    gm_b16_8 gm_l23; /* offs */
    gm_b16_8 gm_l24; /* <tmp> */
    uint64_t gm_l25; /* <tmp> */
    uint64_t gm_l26; /* i */
    gm_t88rcucg1b7uk gm_l28; /* tag */
    gm_b16_8 gm_l29; /* variants */
    gm_b16_8 gm_l30; /* offs */
    gm_b16_8 gm_l32; /* <tmp> */
    uint64_t gm_l33; /* <tmp> */
    gm_t88rcucg1b7uk gm_l35; /* pt */
    gm_tgbgti82mf879 gm_l36; /* p */
    uint64_t gm_l39; /* cursor */
    uint64_t gm_l40; /* natural */
    gm_b16_8 gm_l41; /* <tmp> */
    uint64_t gm_l42; /* <tmp> */
    uint64_t gm_l43; /* i */
    uint64_t gm_l45; /* x */
    uint64_t gm_l46; /* x */
    gm_b16_8 gm_l47; /* alignas */
    uint64_t gm_l48; /* a */
    GM_STACK_CHECK(&F, 5212u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint8_t t3 = 0;
        uint8_t t4 = 0;
        void * t5 = gm_l0;
        uint8_t t6 = GM_LD(uint8_t, (((((uint8_t *)t5) + 8) + 0) + 56));
        if (!t6) {
            void * t7 = gm_l0;
            uint64_t t8 = GM_LD(uint64_t, (((((uint8_t *)t7) + 8) + 0) + 0));
            uint8_t t9 = (!(t8 == ((uint64_t)8ULL)));
            t4 = t9;
        } else t4 = 1;
        if (!t4) {
            void * t10 = gm_l0;
            gm_t88rcucg1b7uk t11 = gm_l1;
            uint64_t t12 = gm_f_size_dmhtkib((void *)NULL, cx, NULL, t10, t11);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t13 = ((t12 == ((uint64_t)0ULL)));
            t3 = t13;
        } else t3 = 1;
        if (!t3) goto ie2;
        {
            gm_th2lqbo77pvsc t15;
            memset(&t15, 0, sizeof t15);
            uint8_t t16 = 0;
            F.ret = t15;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        gm_f_list_792g2go((void *)&F.l2, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t17 = gm_l0;
        gm_t88rcucg1b7uk t18 = gm_l1;
        gm_t53at80pfdus3 t19;
        gm_f_kind_ghvav22((void *)&t19, t17, GM_CONV(gm_csguankko4lmhd, t18));
        gm_t53at80pfdus3 t20 = t19;
        uint8_t t29 = GM_LD(uint8_t, (((uint8_t *)&t20) + 0));
        switch (t29) { case ((uint8_t)0ULL): goto ma27; case ((uint8_t)1ULL): goto ma27; case ((uint8_t)2ULL): goto ma27; case ((uint8_t)3ULL): goto ma27; case ((uint8_t)4ULL): goto ma27; case ((uint8_t)5ULL): goto ma27; case ((uint8_t)6ULL): goto ma27; case ((uint8_t)7ULL): goto ma27; case ((uint8_t)8ULL): goto ma27; case ((uint8_t)9ULL): goto ma27; case ((uint8_t)10ULL): goto ma27; case ((uint8_t)11ULL): goto ma22; case ((uint8_t)12ULL): goto ma23; case ((uint8_t)13ULL): goto ma24; case ((uint8_t)14ULL): goto ma25; case ((uint8_t)15ULL): goto ma27; case ((uint8_t)16ULL): goto ma27; case ((uint8_t)17ULL): goto ma26; case ((uint8_t)18ULL): goto ma27; case ((uint8_t)19ULL): goto ma27; case ((uint8_t)20ULL): goto ma27; case ((uint8_t)21ULL): goto ma27; case ((uint8_t)22ULL): goto ma27; case ((uint8_t)23ULL): goto ma27; case ((uint8_t)24ULL): goto ma27; case ((uint8_t)25ULL): goto ma27; case ((uint8_t)26ULL): goto ma27; default: goto ma27; }
ma22:;
        uint8_t t31 = GM_LD(uint8_t, (((uint8_t *)&t20) + 0));
        if (t31 != ((uint8_t)11ULL)) goto mn30;
        uint64_t t32 = GM_LD(uint64_t, ((((uint8_t *)&t20) + 8) + 0));
        gm_l3 = t32;
        gm_t88rcucg1b7uk t33 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t20) + 8) + 8));
        gm_l4 = t33;
        {
            uint8_t t36 = 0;
            uint64_t t37 = gm_l3;
            uint8_t t38 = ((t37 == ((uint64_t)0ULL)));
            if (!t38) {
                void * t39 = gm_l0;
                gm_t88rcucg1b7uk t40 = gm_l4;
                uint64_t t41 = gm_f_size_dmhtkib((void *)NULL, cx, NULL, t39, t40);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t42 = ((t41 == ((uint64_t)0ULL)));
                t36 = t42;
            } else t36 = 1;
            if (!t36) goto ie35;
            {
                gm_th2lqbo77pvsc t44;
                memset(&t44, 0, sizeof t44);
                uint8_t t45 = 0;
                F.ret = t44;
                goto gm_fret;
            }
            goto ix35;
ie35:;
ix35:;
        }
        {
            void * t46 = gm_l0;
            gm_t88rcucg1b7uk t47 = gm_l4;
            gm_b16_8 t48 = gm_f_cty_66ee0kf((void *)NULL, cx, NULL, t46, t47);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l5 = t48;
        }
        {
            gm_t3877pk7lv748 t49;
            gm_b16_8 t50 = gm_l5;
            GM_ST(gm_b16_8, (uint8_t *)&t49 + 0, t50);
            uint64_t t51 = gm_l3;
            GM_ST(uint64_t, (uint8_t *)&t49 + 16, t51);
            gm_tabb710d9mq2e t52;
            gm_f_format_19ftgop((void *)&t52, (void *)NULL, cx, NULL, &t49);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t53 = t52;
            if (((((uint8_t *)&t53))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t53) + 8, gm_srckfibkpt681aa, 225, 24);
            gm_b16_8 t54 = GM_LD(gm_b16_8, (((uint8_t *)&t53) + 8));
            gm_l6 = t54;
        }
        {
            gm_f_list_cfvjofe((void *)&F.l7, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t55 = (void *)((uint8_t *)&F.l7);
            gm_tftb1odc739t5 t56 = gm_f_push_q9fsb22(t55, &(*(gm_t3877pk7lv748 *)((uint8_t *)gm_kr85ffi32os)));
            gm_tftb1odc739t5 t57 = t56;
            if (((((uint8_t *)&t57))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t57) + 1, gm_srckfibkpt681aa, 227, 13);
            gm_b0_1 t58 = GM_LD(gm_b0_1, (((uint8_t *)&t57) + 1));
        }
        {
            void * t59 = gm_l0;
            gm_t88rcucg1b7uk t60 = gm_l1;
            gm_b16_8 t61 = gm_l6;
            gm_b16_8 t62 = t61;
            gm_b16_8 t63 = GM_LD(gm_b16_8, ((uint8_t *)&t62));
            gm_b16_8 t64 = GM_LD(gm_b16_8, (((uint8_t *)&F.l7) + 0));
            gm_b16_8 t65 = t64;
            gm_b16_8 t66 = GM_LD(gm_b16_8, ((uint8_t *)&t65));
            void * t67 = gm_l0;
            gm_t88rcucg1b7uk t68 = gm_l4;
            uint64_t t69 = gm_f_align_uu2harl((void *)NULL, cx, NULL, t67, t68);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_b16_8 t70 = gm_f_define_typed_m3mqpm3((void *)NULL, cx, NULL, t59, t60, t63, t66, t69);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_th2lqbo77pvsc t71;
            GM_ST(uint8_t, (uint8_t *)&t71 + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&t71 + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&t71 + 20, 0);
            GM_ST(gm_b16_8, ((uint8_t *)&t71), t70);
            ((uint8_t *)&t71)[16] = 1;
            uint8_t t72 = 0;
            F.ret = t71;
            goto gm_fret;
        }
        goto me21;
mn30:;
ma23:;
        uint8_t t74 = GM_LD(uint8_t, (((uint8_t *)&t20) + 0));
        if (t74 != ((uint8_t)12ULL)) goto mn73;
        gm_b16_8 t75 = GM_LD(gm_b16_8, ((((uint8_t *)&t20) + 8) + 0));
        gm_l8 = t75;
        {
            void * t77 = gm_l0;
            gm_t88rcucg1b7uk t78 = gm_l1;
            gm_b16_8 t79 = gm_f_offsets_eh6vfle((void *)NULL, cx, NULL, t77, t78);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l9 = t79;
        }
        {
            {
                gm_b16_8 t81 = gm_l8;
                gm_l10 = t81;
            }
            {
                gm_l11 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t83 = gm_l11;
                    uint64_t t84 = GM_LD(uintptr_t, ((uint8_t *)&gm_l10) + 8);
                    uint8_t t85 = (t83 < t84);
                    if (!t85) goto lb82;
                    {
                        uint64_t t88 = gm_l11;
                        uint64_t t89 = gm_l11;
                        uint64_t t90 = (uintptr_t)t89;
                        uint64_t t91 = GM_LD(uintptr_t, ((uint8_t *)&gm_l10) + 8);
                        gm_t88rcucg1b7uk t92 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l10)) + t90 * 4));
                        gm_tlcm4lvu3g1a8 t93;
                        GM_ST(uint32_t, (uint8_t *)&t93 + 12, 0);
                        GM_ST(uint64_t, ((uint8_t *)&t93 + 0), t88);
                        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t93 + 8), t92);
                        gm_tlcm4lvu3g1a8 t94 = t93;
                        uint64_t t97 = GM_LD(uint64_t, (((uint8_t *)&t94) + 0));
                        gm_l12 = t97;
                        gm_t88rcucg1b7uk t98 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t94) + 8));
                        gm_l13 = t98;
                        {
                            void * t100 = (void *)((uint8_t *)&F.l2);
                            gm_tml3m6miq9l2k t101;
                            void * t102 = gm_l0;
                            gm_t88rcucg1b7uk t103 = gm_l13;
                            gm_b16_8 t104 = gm_f_cty_66ee0kf((void *)NULL, cx, NULL, t102, t103);
                            if (gm_unw.kind) {
                                goto gm_ll86;
                            }
                            GM_ST(gm_b16_8, (uint8_t *)&t101 + 0, t104);
                            void * t105 = gm_l0;
                            gm_t88rcucg1b7uk t106 = gm_l13;
                            uint64_t t107 = gm_f_size_dmhtkib((void *)NULL, cx, NULL, t105, t106);
                            if (gm_unw.kind) {
                                goto gm_ll86;
                            }
                            GM_ST(uint64_t, (uint8_t *)&t101 + 16, t107);
                            void * t108 = gm_l0;
                            gm_t88rcucg1b7uk t109 = gm_l13;
                            uint64_t t110 = gm_f_align_uu2harl((void *)NULL, cx, NULL, t108, t109);
                            if (gm_unw.kind) {
                                goto gm_ll86;
                            }
                            GM_ST(uint64_t, (uint8_t *)&t101 + 24, t110);
                            gm_tf5srh4mp4b3i t111;
                            memset(&t111, 0, sizeof t111);
                            GM_ST(gm_tf5srh4mp4b3i, (uint8_t *)&t101 + 32, t111);
                            uint64_t t112 = gm_l12;
                            uint64_t t113 = (uintptr_t)t112;
                            uint64_t t114 = GM_LD(uintptr_t, ((uint8_t *)&gm_l9) + 8);
                            if (t113 >= (uintptr_t)(t114)) gm_trap_index(t113, t114, gm_srckfibkpt681aa, 232, 141);
                            uint64_t t115 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l9)) + t113 * 8));
                            GM_ST(uint64_t, (uint8_t *)&t101 + 48, t115);
                            gm_tftb1odc739t5 t116 = gm_f_push_m55rf90(t100, &t101);
                            gm_tftb1odc739t5 t117 = t116;
                            if (((((uint8_t *)&t117))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t117) + 1, gm_srckfibkpt681aa, 232, 45);
                            gm_b0_1 t118 = GM_LD(gm_b0_1, (((uint8_t *)&t117) + 1));
                        }
                        goto me95;
mn96:;
                        gm_trap("no match arm matched the scrutinee", gm_srckfibkpt681aa, 232, 13);
me95:;
                    }
lc82:;
                    {
                        uint64_t t120 = gm_l11;
                        uint64_t r121; bool o122 = __builtin_add_overflow(t120, ((uint64_t)1ULL), &r121);
                        uint64_t t123 = r121;
                        gm_l11 = t123;
                    }
                }
gm_ll86:;
                if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb82; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc82; }
                }
                goto gm_lad;
lb82:;
            }
        }
        goto me21;
mn73:;
ma24:;
        uint8_t t125 = GM_LD(uint8_t, (((uint8_t *)&t20) + 0));
        if (t125 != ((uint8_t)13ULL)) goto mn124;
        gm_t88rcucg1b7uk t126 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t20) + 8));
        gm_l14 = t126;
        {
            void * t128 = gm_l0;
            gm_t88rcucg1b7uk t129 = gm_l1;
            gm_b16_8 t130 = gm_f_offsets_eh6vfle((void *)NULL, cx, NULL, t128, t129);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l15 = t130;
        }
        {
            void * t131 = (void *)((uint8_t *)&F.l2);
            gm_tml3m6miq9l2k t132;
            void * t133 = gm_l0;
            gm_t88rcucg1b7uk t134 = gm_l14;
            gm_b16_8 t135 = gm_f_cty_66ee0kf((void *)NULL, cx, NULL, t133, t134);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(gm_b16_8, (uint8_t *)&t132 + 0, t135);
            void * t136 = gm_l0;
            gm_t88rcucg1b7uk t137 = gm_l14;
            uint64_t t138 = gm_f_size_dmhtkib((void *)NULL, cx, NULL, t136, t137);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(uint64_t, (uint8_t *)&t132 + 16, t138);
            void * t139 = gm_l0;
            gm_t88rcucg1b7uk t140 = gm_l14;
            uint64_t t141 = gm_f_align_uu2harl((void *)NULL, cx, NULL, t139, t140);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(uint64_t, (uint8_t *)&t132 + 24, t141);
            gm_tf5srh4mp4b3i t142;
            memset(&t142, 0, sizeof t142);
            GM_ST(gm_tf5srh4mp4b3i, (uint8_t *)&t132 + 32, t142);
            uint64_t t143 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t144 = GM_LD(uintptr_t, ((uint8_t *)&gm_l15) + 8);
            if (t143 >= (uintptr_t)(t144)) gm_trap_index(t143, t144, gm_srckfibkpt681aa, 236, 118);
            uint64_t t145 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l15)) + t143 * 8));
            GM_ST(uint64_t, (uint8_t *)&t132 + 48, t145);
            gm_tftb1odc739t5 t146 = gm_f_push_m55rf90(t131, &t132);
            gm_tftb1odc739t5 t147 = t146;
            if (((((uint8_t *)&t147))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t147) + 1, gm_srckfibkpt681aa, 236, 13);
            gm_b0_1 t148 = GM_LD(gm_b0_1, (((uint8_t *)&t147) + 1));
        }
        {
            void * t149 = (void *)((uint8_t *)&F.l2);
            gm_tml3m6miq9l2k t150;
            gm_b16_8 t151;
            GM_ST(void *, (uint8_t *)&t151, (void *)(gm_s9grtrvtmi962));
            GM_ST(uintptr_t, (uint8_t *)&t151 + 8, (uintptr_t)(7));
            GM_ST(gm_b16_8, (uint8_t *)&t150 + 0, t151);
            GM_ST(uint64_t, (uint8_t *)&t150 + 16, ((uint64_t)1ULL));
            GM_ST(uint64_t, (uint8_t *)&t150 + 24, ((uint64_t)1ULL));
            gm_tf5srh4mp4b3i t152;
            memset(&t152, 0, sizeof t152);
            GM_ST(gm_tf5srh4mp4b3i, (uint8_t *)&t150 + 32, t152);
            uint64_t t153 = (uintptr_t)((uint64_t)1ULL);
            uint64_t t154 = GM_LD(uintptr_t, ((uint8_t *)&gm_l15) + 8);
            if (t153 >= (uintptr_t)(t154)) gm_trap_index(t153, t154, gm_srckfibkpt681aa, 237, 87);
            uint64_t t155 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l15)) + t153 * 8));
            GM_ST(uint64_t, (uint8_t *)&t150 + 48, t155);
            gm_tftb1odc739t5 t156 = gm_f_push_m55rf90(t149, &t150);
            gm_tftb1odc739t5 t157 = t156;
            if (((((uint8_t *)&t157))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t157) + 1, gm_srckfibkpt681aa, 237, 13);
            gm_b0_1 t158 = GM_LD(gm_b0_1, (((uint8_t *)&t157) + 1));
        }
        goto me21;
mn124:;
ma25:;
        uint8_t t160 = GM_LD(uint8_t, (((uint8_t *)&t20) + 0));
        if (t160 != ((uint8_t)14ULL)) goto mn159;
        gm_t88rcucg1b7uk t161 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t20) + 8) + 0));
        gm_l16 = t161;
        gm_t88rcucg1b7uk t162 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t20) + 8) + 4));
        gm_l17 = t162;
        {
            void * t164 = gm_l0;
            gm_t88rcucg1b7uk t165 = gm_l1;
            gm_b16_8 t166 = gm_f_offsets_eh6vfle((void *)NULL, cx, NULL, t164, t165);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l18 = t166;
        }
        {
            void * t167 = (void *)((uint8_t *)&F.l2);
            gm_tml3m6miq9l2k t168;
            gm_b16_8 t169;
            GM_ST(void *, (uint8_t *)&t169, (void *)(gm_s9grtrvtmi962));
            GM_ST(uintptr_t, (uint8_t *)&t169 + 8, (uintptr_t)(7));
            GM_ST(gm_b16_8, (uint8_t *)&t168 + 0, t169);
            GM_ST(uint64_t, (uint8_t *)&t168 + 16, ((uint64_t)1ULL));
            GM_ST(uint64_t, (uint8_t *)&t168 + 24, ((uint64_t)1ULL));
            gm_tf5srh4mp4b3i t170;
            memset(&t170, 0, sizeof t170);
            GM_ST(gm_tf5srh4mp4b3i, (uint8_t *)&t168 + 32, t170);
            uint64_t t171 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t172 = GM_LD(uintptr_t, ((uint8_t *)&gm_l18) + 8);
            if (t171 >= (uintptr_t)(t172)) gm_trap_index(t171, t172, gm_srckfibkpt681aa, 241, 87);
            uint64_t t173 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l18)) + t171 * 8));
            GM_ST(uint64_t, (uint8_t *)&t168 + 48, t173);
            gm_tftb1odc739t5 t174 = gm_f_push_m55rf90(t167, &t168);
            gm_tftb1odc739t5 t175 = t174;
            if (((((uint8_t *)&t175))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t175) + 1, gm_srckfibkpt681aa, 241, 13);
            gm_b0_1 t176 = GM_LD(gm_b0_1, (((uint8_t *)&t175) + 1));
        }
        {
            gm_t88rcucg1b7uk t177 = gm_l16;
            gm_t88rcucg1b7uk t178 = gm_l17;
            gm_tqsk8dt6ib3qq t179;
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t179 + 0), t177);
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t179 + 4), t178);
            gm_l19 = t179;
        }
        {
            void * t181 = gm_l0;
            void * t182 = ((uint8_t *)&gm_l19);
            if ((uintptr_t)0 > (uintptr_t)2 || (uintptr_t)2 > (uintptr_t)2) gm_trap_slice((uintptr_t)0, (uintptr_t)2, 2, gm_srckfibkpt681aa, 243, 38);
            gm_b16_8 t183;
            GM_ST(void *, (uint8_t *)&t183, (void *)(((uint8_t *)t182 + (uintptr_t)0 * 4)));
            GM_ST(uintptr_t, (uint8_t *)&t183 + 8, (uintptr_t)((uintptr_t)2 - (uintptr_t)0));
            gm_b16_8 t184 = t183;
            gm_b16_8 t185 = GM_LD(gm_b16_8, ((uint8_t *)&t184));
            gm_t8ldkt09h6492 t186;
            gm_f_payload_union_feshns2((void *)&t186, (void *)NULL, cx, NULL, t181, t185);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t8ldkt09h6492 t187 = t186;
            if (!((((uint8_t *)&t187))[32] != 0)) goto ie180;
            gm_tgbgti82mf879 t188 = GM_LD(gm_tgbgti82mf879, ((uint8_t *)&t187));
            gm_l20 = t188;
            {
                void * t190 = (void *)((uint8_t *)&F.l2);
                gm_tml3m6miq9l2k t191;
                gm_b16_8 t192 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l20) + 0));
                GM_ST(gm_b16_8, (uint8_t *)&t191 + 0, t192);
                uint64_t t193 = GM_LD(uint64_t, (((uint8_t *)&gm_l20) + 16));
                GM_ST(uint64_t, (uint8_t *)&t191 + 16, t193);
                uint64_t t194 = GM_LD(uint64_t, (((uint8_t *)&gm_l20) + 24));
                GM_ST(uint64_t, (uint8_t *)&t191 + 24, t194);
                gm_tf5srh4mp4b3i t195;
                memset(&t195, 0, sizeof t195);
                GM_ST(gm_tf5srh4mp4b3i, (uint8_t *)&t191 + 32, t195);
                uint64_t t196 = (uintptr_t)((uint64_t)1ULL);
                uint64_t t197 = GM_LD(uintptr_t, ((uint8_t *)&gm_l18) + 8);
                if (t196 >= (uintptr_t)(t197)) gm_trap_index(t196, t197, gm_srckfibkpt681aa, 243, 131);
                uint64_t t198 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l18)) + t196 * 8));
                GM_ST(uint64_t, (uint8_t *)&t191 + 48, t198);
                gm_tftb1odc739t5 t199 = gm_f_push_m55rf90(t190, &t191);
                gm_tftb1odc739t5 t200 = t199;
                if (((((uint8_t *)&t200))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t200) + 1, gm_srckfibkpt681aa, 243, 50);
                gm_b0_1 t201 = GM_LD(gm_b0_1, (((uint8_t *)&t200) + 1));
            }
            goto ix180;
ie180:;
ix180:;
        }
        goto me21;
mn159:;
ma26:;
        uint8_t t203 = GM_LD(uint8_t, (((uint8_t *)&t20) + 0));
        if (t203 != ((uint8_t)17ULL)) goto mn202;
        uint32_t t204 = GM_LD(uint32_t, ((((uint8_t *)&t20) + 8) + 0));
        gm_l21 = t204;
        uint32_t t205 = gm_l21;
        uint64_t t206 = (uint64_t)t205;
        uint64_t t207 = (uintptr_t)t206;
        void * t208 = gm_l0;
        void * t209 = GM_LD(void *, (((uint8_t *)t208) + 0));
        uint64_t t210 = GM_LD(uintptr_t, ((((uint8_t *)t209) + 1088) + 0) + 8);
        if (t207 >= (uintptr_t)(t210)) gm_trap_index(t207, t210, gm_srckfibkpt681aa, 245, 30);
        uint8_t t216 = GM_LD(uint8_t, ((((uint8_t *)GM_LD(void *, ((((uint8_t *)t209) + 1088) + 0)) + t207 * 184) + 24) + 0));
        switch (t216) { case ((uint8_t)0ULL): goto ma212; case ((uint8_t)1ULL): goto ma213; case ((uint8_t)2ULL): goto ma214; default: goto ma215; }
ma212:;
        uint8_t t218 = GM_LD(uint8_t, ((((uint8_t *)GM_LD(void *, ((((uint8_t *)t209) + 1088) + 0)) + t207 * 184) + 24) + 0));
        if (t218 != ((uint8_t)0ULL)) goto mn217;
        gm_tpautr4br7h3l t219 = GM_LD(gm_tpautr4br7h3l, ((((uint8_t *)GM_LD(void *, ((((uint8_t *)t209) + 1088) + 0)) + t207 * 184) + 24) + 8));
        F.l22 = t219;
        {
            uint8_t t222 = 0;
            uint8_t t223 = GM_LD(uint8_t, (((uint8_t *)&F.l22) + 16));
            if (!t223) {
                uint8_t t224;
                if ((((((uint8_t *)&F.l22) + 40))[4] != 0)) goto mn226;
                t224 = ((uint8_t)0);
                goto me225;
mn226:;
                t224 = ((uint8_t)1);
                goto me225;
mn227:;
                gm_trap("no match arm matched the scrutinee", gm_srckfibkpt681aa, 247, 34);
me225:;
                t222 = t224;
            } else t222 = 1;
            if (!t222) goto ie221;
            {
                gm_th2lqbo77pvsc t229;
                memset(&t229, 0, sizeof t229);
                uint8_t t230 = 0;
                F.ret = t229;
                goto gm_fret;
            }
            goto ix221;
ie221:;
ix221:;
        }
        {
            void * t231 = gm_l0;
            gm_t88rcucg1b7uk t232 = gm_l1;
            gm_b16_8 t233 = gm_f_offsets_eh6vfle((void *)NULL, cx, NULL, t231, t232);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l23 = t233;
        }
        {
            {
                gm_b16_8 t235 = GM_LD(gm_b16_8, (((uint8_t *)&F.l22) + 0));
                gm_b16_8 t236 = t235;
                gm_b16_8 t237 = GM_LD(gm_b16_8, ((uint8_t *)&t236));
                gm_l24 = t237;
            }
            {
                gm_l25 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t239 = gm_l25;
                    uint64_t t240 = GM_LD(uintptr_t, ((uint8_t *)&gm_l24) + 8);
                    uint8_t t241 = (t239 < t240);
                    if (!t241) goto lb238;
                    {
                        gm_t8ta482bl6be0 t244;
                        uint64_t t245 = gm_l25;
                        GM_ST(uint64_t, (uint8_t *)&t244 + 0, t245);
                        uint64_t t246 = gm_l25;
                        uint64_t t247 = (uintptr_t)t246;
                        uint64_t t248 = GM_LD(uintptr_t, ((uint8_t *)&gm_l24) + 8);
                        memcpy((uint8_t *)&t244 + 8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l24)) + t247 * 40), 40);
                        gm_t8ta482bl6be0 t249 = t244;
                        uint64_t t252 = GM_LD(uint64_t, (((uint8_t *)&t249) + 0));
                        gm_l26 = t252;
                        gm_t0mglksrkfa5e t253 = GM_LD(gm_t0mglksrkfa5e, (((uint8_t *)&t249) + 8));
                        F.l27 = t253;
                        {
                            void * t255 = (void *)((uint8_t *)&F.l2);
                            gm_tml3m6miq9l2k t256;
                            void * t257 = gm_l0;
                            gm_t88rcucg1b7uk t258 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l27) + 16));
                            gm_b16_8 t259 = gm_f_cty_66ee0kf((void *)NULL, cx, NULL, t257, t258);
                            if (gm_unw.kind) {
                                goto gm_ll242;
                            }
                            GM_ST(gm_b16_8, (uint8_t *)&t256 + 0, t259);
                            void * t260 = gm_l0;
                            gm_t88rcucg1b7uk t261 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l27) + 16));
                            uint64_t t262 = gm_f_size_dmhtkib((void *)NULL, cx, NULL, t260, t261);
                            if (gm_unw.kind) {
                                goto gm_ll242;
                            }
                            GM_ST(uint64_t, (uint8_t *)&t256 + 16, t262);
                            void * t263 = gm_l0;
                            gm_t88rcucg1b7uk t264 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l27) + 16));
                            uint64_t t265 = gm_f_align_uu2harl((void *)NULL, cx, NULL, t263, t264);
                            if (gm_unw.kind) {
                                goto gm_ll242;
                            }
                            GM_ST(uint64_t, (uint8_t *)&t256 + 24, t265);
                            gm_tf5srh4mp4b3i t266 = GM_LD(gm_tf5srh4mp4b3i, (((uint8_t *)&F.l27) + 24));
                            GM_ST(gm_tf5srh4mp4b3i, (uint8_t *)&t256 + 32, t266);
                            uint64_t t267 = gm_l26;
                            uint64_t t268 = (uintptr_t)t267;
                            uint64_t t269 = GM_LD(uintptr_t, ((uint8_t *)&gm_l23) + 8);
                            if (t268 >= (uintptr_t)(t269)) gm_trap_index(t268, t269, gm_srckfibkpt681aa, 249, 158);
                            uint64_t t270 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l23)) + t268 * 8));
                            GM_ST(uint64_t, (uint8_t *)&t256 + 48, t270);
                            gm_tftb1odc739t5 t271 = gm_f_push_m55rf90(t255, &t256);
                            gm_tftb1odc739t5 t272 = t271;
                            if (((((uint8_t *)&t272))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t272) + 1, gm_srckfibkpt681aa, 249, 53);
                            gm_b0_1 t273 = GM_LD(gm_b0_1, (((uint8_t *)&t272) + 1));
                        }
                        goto me250;
mn251:;
                        gm_trap("no match arm matched the scrutinee", gm_srckfibkpt681aa, 249, 17);
me250:;
                    }
lc238:;
                    {
                        uint64_t t275 = gm_l25;
                        uint64_t r276; bool o277 = __builtin_add_overflow(t275, ((uint64_t)1ULL), &r276);
                        uint64_t t278 = r276;
                        gm_l25 = t278;
                    }
                }
gm_ll242:;
                if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb238; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc238; }
                }
                goto gm_lad;
lb238:;
            }
        }
        goto me211;
mn217:;
ma213:;
        uint8_t t280 = GM_LD(uint8_t, ((((uint8_t *)GM_LD(void *, ((((uint8_t *)t209) + 1088) + 0)) + t207 * 184) + 24) + 0));
        if (t280 != ((uint8_t)1ULL)) goto mn279;
        gm_t88rcucg1b7uk t281 = GM_LD(gm_t88rcucg1b7uk, (((((uint8_t *)GM_LD(void *, ((((uint8_t *)t209) + 1088) + 0)) + t207 * 184) + 24) + 8) + 0));
        gm_l28 = t281;
        gm_b16_8 t282 = GM_LD(gm_b16_8, (((((uint8_t *)GM_LD(void *, ((((uint8_t *)t209) + 1088) + 0)) + t207 * 184) + 24) + 8) + 8));
        gm_l29 = t282;
        {
            void * t284 = gm_l0;
            gm_t88rcucg1b7uk t285 = gm_l1;
            gm_b16_8 t286 = gm_f_offsets_eh6vfle((void *)NULL, cx, NULL, t284, t285);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l30 = t286;
        }
        {
            void * t287 = (void *)((uint8_t *)&F.l2);
            gm_tml3m6miq9l2k t288;
            void * t289 = gm_l0;
            gm_t88rcucg1b7uk t290 = gm_l28;
            gm_b16_8 t291 = gm_f_cty_66ee0kf((void *)NULL, cx, NULL, t289, t290);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(gm_b16_8, (uint8_t *)&t288 + 0, t291);
            void * t292 = gm_l0;
            gm_t88rcucg1b7uk t293 = gm_l28;
            uint64_t t294 = gm_f_size_dmhtkib((void *)NULL, cx, NULL, t292, t293);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(uint64_t, (uint8_t *)&t288 + 16, t294);
            void * t295 = gm_l0;
            gm_t88rcucg1b7uk t296 = gm_l28;
            uint64_t t297 = gm_f_align_uu2harl((void *)NULL, cx, NULL, t295, t296);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(uint64_t, (uint8_t *)&t288 + 24, t297);
            gm_tf5srh4mp4b3i t298;
            memset(&t298, 0, sizeof t298);
            GM_ST(gm_tf5srh4mp4b3i, (uint8_t *)&t288 + 32, t298);
            uint64_t t299 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t300 = GM_LD(uintptr_t, ((uint8_t *)&gm_l30) + 8);
            if (t299 >= (uintptr_t)(t300)) gm_trap_index(t299, t300, gm_srckfibkpt681aa, 253, 116);
            uint64_t t301 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l30)) + t299 * 8));
            GM_ST(uint64_t, (uint8_t *)&t288 + 48, t301);
            gm_tftb1odc739t5 t302 = gm_f_push_m55rf90(t287, &t288);
            gm_tftb1odc739t5 t303 = t302;
            if (((((uint8_t *)&t303))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t303) + 1, gm_srckfibkpt681aa, 253, 17);
            gm_b0_1 t304 = GM_LD(gm_b0_1, (((uint8_t *)&t303) + 1));
        }
        {
            gm_f_list_nof7vfj((void *)&F.l31, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            {
                gm_b16_8 t306 = gm_l29;
                gm_l32 = t306;
            }
            {
                gm_l33 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t308 = gm_l33;
                    uint64_t t309 = GM_LD(uintptr_t, ((uint8_t *)&gm_l32) + 8);
                    uint8_t t310 = (t308 < t309);
                    if (!t310) goto lb307;
                    {
                        uint64_t t313 = gm_l33;
                        uint64_t t314 = (uintptr_t)t313;
                        uint64_t t315 = GM_LD(uintptr_t, ((uint8_t *)&gm_l32) + 8);
                        gm_tckdobunaulee t318 = GM_LD(gm_tckdobunaulee, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l32)) + t314 * 72));
                        F.l34 = t318;
                        {
                            if (!(((((uint8_t *)&F.l34) + 16))[4] != 0)) goto ie320;
                            gm_t88rcucg1b7uk t321 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l34) + 16));
                            gm_l35 = t321;
                            {
                                void * t323 = (void *)((uint8_t *)&F.l31);
                                gm_t88rcucg1b7uk t324 = gm_l35;
                                gm_tftb1odc739t5 t325 = gm_f_push_k3l9nic(t323, GM_CONV(gm_csguankko4lmhd, t324));
                                gm_tftb1odc739t5 t326 = t325;
                                if (((((uint8_t *)&t326))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t326) + 1, gm_srckfibkpt681aa, 256, 42);
                                gm_b0_1 t327 = GM_LD(gm_b0_1, (((uint8_t *)&t326) + 1));
                            }
                            goto ix320;
ie320:;
ix320:;
                        }
                        goto me316;
mn317:;
                        gm_trap("no match arm matched the scrutinee", gm_srckfibkpt681aa, 255, 17);
me316:;
                    }
lc307:;
                    {
                        uint64_t t329 = gm_l33;
                        uint64_t r330; bool o331 = __builtin_add_overflow(t329, ((uint64_t)1ULL), &r330);
                        uint64_t t332 = r330;
                        gm_l33 = t332;
                    }
                }
lb307:;
            }
        }
        {
            uint64_t t334 = GM_LD(uintptr_t, ((uint8_t *)&gm_l30) + 8);
            uint8_t t335 = (t334 > ((uint64_t)1ULL));
            if (!t335) goto ie333;
            {
                void * t338 = gm_l0;
                gm_b16_8 t339 = GM_LD(gm_b16_8, (((uint8_t *)&F.l31) + 0));
                gm_b16_8 t340 = t339;
                gm_b16_8 t341 = GM_LD(gm_b16_8, ((uint8_t *)&t340));
                gm_t8ldkt09h6492 t342;
                gm_f_payload_union_feshns2((void *)&t342, (void *)NULL, cx, NULL, t338, t341);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_t8ldkt09h6492 t343 = t342;
                if (!((((uint8_t *)&t343))[32] != 0)) goto ie337;
                gm_tgbgti82mf879 t344 = GM_LD(gm_tgbgti82mf879, ((uint8_t *)&t343));
                gm_l36 = t344;
                {
                    void * t346 = (void *)((uint8_t *)&F.l2);
                    gm_tml3m6miq9l2k t347;
                    gm_b16_8 t348 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l36) + 0));
                    GM_ST(gm_b16_8, (uint8_t *)&t347 + 0, t348);
                    uint64_t t349 = GM_LD(uint64_t, (((uint8_t *)&gm_l36) + 16));
                    GM_ST(uint64_t, (uint8_t *)&t347 + 16, t349);
                    uint64_t t350 = GM_LD(uint64_t, (((uint8_t *)&gm_l36) + 24));
                    GM_ST(uint64_t, (uint8_t *)&t347 + 24, t350);
                    gm_tf5srh4mp4b3i t351;
                    memset(&t351, 0, sizeof t351);
                    GM_ST(gm_tf5srh4mp4b3i, (uint8_t *)&t347 + 32, t351);
                    uint64_t t352 = (uintptr_t)((uint64_t)1ULL);
                    uint64_t t353 = GM_LD(uintptr_t, ((uint8_t *)&gm_l30) + 8);
                    if (t352 >= (uintptr_t)(t353)) gm_trap_index(t352, t353, gm_srckfibkpt681aa, 259, 145);
                    uint64_t t354 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l30)) + t352 * 8));
                    GM_ST(uint64_t, (uint8_t *)&t347 + 48, t354);
                    gm_tftb1odc739t5 t355 = gm_f_push_m55rf90(t346, &t347);
                    gm_tftb1odc739t5 t356 = t355;
                    if (((((uint8_t *)&t356))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t356) + 1, gm_srckfibkpt681aa, 259, 64);
                    gm_b0_1 t357 = GM_LD(gm_b0_1, (((uint8_t *)&t356) + 1));
                }
                goto ix337;
ie337:;
ix337:;
            }
            goto ix333;
ie333:;
ix333:;
        }
        goto me211;
mn279:;
ma214:;
        uint8_t t359 = GM_LD(uint8_t, ((((uint8_t *)GM_LD(void *, ((((uint8_t *)t209) + 1088) + 0)) + t207 * 184) + 24) + 0));
        if (t359 != ((uint8_t)2ULL)) goto mn358;
        gm_th2lqbo77pvsc t360;
        memset(&t360, 0, sizeof t360);
        uint8_t t361 = 0;
        F.ret = t360;
        goto gm_fret;
        goto me211;
mn358:;
ma215:;
        gm_trap("no match arm matched the scrutinee", gm_srckfibkpt681aa, 245, 24);
me211:;
        goto me21;
mn202:;
ma27:;
        gm_th2lqbo77pvsc t363;
        memset(&t363, 0, sizeof t363);
        uint8_t t364 = 0;
        F.ret = t363;
        goto gm_fret;
        goto me21;
mn362:;
ma28:;
        gm_trap("no match arm matched the scrutinee", gm_srckfibkpt681aa, 221, 5);
me21:;
    }
    {
        gm_f_list_epvducg((void *)&F.l37, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_f_list_cfvjofe((void *)&F.l38, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_l39 = ((uint64_t)0ULL);
    }
    {
        gm_l40 = ((uint64_t)1ULL);
    }
    {
        {
            gm_b16_8 t366 = GM_LD(gm_b16_8, (((uint8_t *)&F.l2) + 0));
            gm_b16_8 t367 = t366;
            gm_b16_8 t368 = GM_LD(gm_b16_8, ((uint8_t *)&t367));
            gm_l41 = t368;
        }
        {
            gm_l42 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t370 = gm_l42;
                uint64_t t371 = GM_LD(uintptr_t, ((uint8_t *)&gm_l41) + 8);
                uint8_t t372 = (t370 < t371);
                if (!t372) goto lb369;
                {
                    gm_ttos9gukqrqer t375;
                    uint64_t t376 = gm_l42;
                    GM_ST(uint64_t, (uint8_t *)&t375 + 0, t376);
                    uint64_t t377 = gm_l42;
                    uint64_t t378 = (uintptr_t)t377;
                    uint64_t t379 = GM_LD(uintptr_t, ((uint8_t *)&gm_l41) + 8);
                    memcpy((uint8_t *)&t375 + 8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l41)) + t378 * 56), 56);
                    gm_ttos9gukqrqer t380 = t375;
                    uint64_t t383 = GM_LD(uint64_t, (((uint8_t *)&t380) + 0));
                    gm_l43 = t383;
                    gm_tml3m6miq9l2k t384 = GM_LD(gm_tml3m6miq9l2k, (((uint8_t *)&t380) + 8));
                    F.l44 = t384;
                    {
                        uint64_t t387 = GM_LD(uint64_t, (((uint8_t *)&F.l44) + 16));
                        uint8_t t388 = ((t387 == ((uint64_t)0ULL)));
                        if (!t388) goto ie386;
                        {
                            goto lc369;
                        }
                        goto ix386;
ie386:;
ix386:;
                    }
                    {
                        uint64_t t391 = GM_LD(uint64_t, (((uint8_t *)&F.l44) + 48));
                        uint64_t t392 = gm_l39;
                        uint8_t t393 = (t391 > t392);
                        if (!t393) goto ie390;
                        {
                            void * t395 = (void *)((uint8_t *)&F.l37);
                            uint64_t t396 = gm_l43;
                            uint64_t t397 = GM_LD(uint64_t, (((uint8_t *)&F.l44) + 48));
                            uint64_t t398 = gm_l39;
                            uint64_t r399; bool o400 = __builtin_sub_overflow(t397, t398, &r399);
                            if (o400) gm_trap("integer overflow in `-`", gm_srckfibkpt681aa, 272, 76);
                            uint64_t t401 = r399;
                            gm_t2lqu8jsj511d t402;
                            GM_ST(uint64_t, ((uint8_t *)&t402 + 0), t396);
                            GM_ST(uint64_t, ((uint8_t *)&t402 + 8), t401);
                            gm_tabb710d9mq2e t403;
                            gm_f_format_38m1q8c((void *)&t403, (void *)NULL, cx, NULL, t402);
                            if (gm_unw.kind) {
                                goto gm_ll373;
                            }
                            gm_tabb710d9mq2e t404 = t403;
                            if (((((uint8_t *)&t404))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t404) + 8, gm_srckfibkpt681aa, 272, 42);
                            gm_b16_8 t405 = GM_LD(gm_b16_8, (((uint8_t *)&t404) + 8));
                            gm_b16_8 t406 = t405;
                            gm_b16_8 t407 = GM_LD(gm_b16_8, ((uint8_t *)&t406));
                            gm_tftb1odc739t5 t408 = gm_f_push_3ef746b(t395, t407);
                            gm_tftb1odc739t5 t409 = t408;
                            if (((((uint8_t *)&t409))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t409) + 1, gm_srckfibkpt681aa, 272, 29);
                            gm_b0_1 t410 = GM_LD(gm_b0_1, (((uint8_t *)&t409) + 1));
                        }
                        goto ix390;
ie390:;
ix390:;
                    }
                    {
                        uint64_t t411 = gm_l40;
                        uint64_t t412;
                        if (!(((((uint8_t *)&F.l44) + 32))[8] != 0)) goto ie413;
                        uint64_t t414 = GM_LD(uint64_t, (((uint8_t *)&F.l44) + 32));
                        gm_l45 = t414;
                        uint64_t t416 = gm_l45;
                        uint64_t t417 = GM_LD(uint64_t, (((uint8_t *)&F.l44) + 24));
                        uint64_t t418 = (t416 > t417 ? t416 : t417);
                        uint64_t t419 = t418;
                        t412 = t419;
                        goto ix413;
ie413:;
                        uint64_t t421 = GM_LD(uint64_t, (((uint8_t *)&F.l44) + 24));
                        uint64_t t422 = t421;
                        t412 = t422;
ix413:;
                        uint64_t t423 = (t411 > t412 ? t411 : t412);
                        gm_l40 = t423;
                    }
                    {
                        gm_b16_8 t424;
                        if (!(((((uint8_t *)&F.l44) + 32))[8] != 0)) goto ie425;
                        uint64_t t426 = GM_LD(uint64_t, (((uint8_t *)&F.l44) + 32));
                        gm_l46 = t426;
                        uint64_t t428 = gm_l46;
                        gm_t4ce0jd3tdro4 t429;
                        GM_ST(uint64_t, ((uint8_t *)&t429 + 0), t428);
                        gm_tabb710d9mq2e t430;
                        gm_f_format_irg2n6e((void *)&t430, (void *)NULL, cx, NULL, t429);
                        if (gm_unw.kind) {
                            goto gm_ll373;
                        }
                        gm_tabb710d9mq2e t431 = t430;
                        if (((((uint8_t *)&t431))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t431) + 8, gm_srckfibkpt681aa, 274, 48);
                        gm_b16_8 t432 = GM_LD(gm_b16_8, (((uint8_t *)&t431) + 8));
                        gm_b16_8 t433 = t432;
                        gm_b16_8 t434 = t433;
                        gm_b16_8 t435 = GM_LD(gm_b16_8, ((uint8_t *)&t434));
                        t424 = t435;
                        goto ix425;
ie425:;
                        gm_b16_8 t437;
                        GM_ST(void *, (uint8_t *)&t437, (void *)(gm_sr4qbrk6tlgaa));
                        GM_ST(uintptr_t, (uint8_t *)&t437 + 8, (uintptr_t)(0));
                        gm_b16_8 t438 = t437;
                        t424 = t438;
ix425:;
                        gm_l47 = t424;
                    }
                    {
                        void * t439 = (void *)((uint8_t *)&F.l37);
                        gm_tftksr4op114e t440;
                        gm_b16_8 t441 = gm_l47;
                        GM_ST(gm_b16_8, (uint8_t *)&t440 + 0, t441);
                        gm_b16_8 t442 = GM_LD(gm_b16_8, (((uint8_t *)&F.l44) + 0));
                        GM_ST(gm_b16_8, (uint8_t *)&t440 + 16, t442);
                        uint64_t t443 = gm_l43;
                        GM_ST(uint64_t, (uint8_t *)&t440 + 32, t443);
                        gm_tabb710d9mq2e t444;
                        gm_f_format_1dfo4pq((void *)&t444, (void *)NULL, cx, NULL, &t440);
                        if (gm_unw.kind) {
                            goto gm_ll373;
                        }
                        gm_tabb710d9mq2e t445 = t444;
                        if (((((uint8_t *)&t445))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t445) + 8, gm_srckfibkpt681aa, 275, 22);
                        gm_b16_8 t446 = GM_LD(gm_b16_8, (((uint8_t *)&t445) + 8));
                        gm_b16_8 t447 = t446;
                        gm_b16_8 t448 = GM_LD(gm_b16_8, ((uint8_t *)&t447));
                        gm_tftb1odc739t5 t449 = gm_f_push_3ef746b(t439, t448);
                        gm_tftb1odc739t5 t450 = t449;
                        if (((((uint8_t *)&t450))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t450) + 1, gm_srckfibkpt681aa, 275, 9);
                        gm_b0_1 t451 = GM_LD(gm_b0_1, (((uint8_t *)&t450) + 1));
                    }
                    {
                        void * t452 = (void *)((uint8_t *)&F.l38);
                        gm_t3877pk7lv748 t453;
                        uint64_t t454 = gm_l43;
                        gm_t4ce0jd3tdro4 t455;
                        GM_ST(uint64_t, ((uint8_t *)&t455 + 0), t454);
                        gm_tabb710d9mq2e t456;
                        gm_f_format_tc7tn98((void *)&t456, (void *)NULL, cx, NULL, t455);
                        if (gm_unw.kind) {
                            goto gm_ll373;
                        }
                        gm_tabb710d9mq2e t457 = t456;
                        if (((((uint8_t *)&t457))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t457) + 8, gm_srckfibkpt681aa, 276, 38);
                        gm_b16_8 t458 = GM_LD(gm_b16_8, (((uint8_t *)&t457) + 8));
                        gm_b16_8 t459 = t458;
                        gm_b16_8 t460 = GM_LD(gm_b16_8, ((uint8_t *)&t459));
                        GM_ST(gm_b16_8, (uint8_t *)&t453 + 0, t460);
                        uint64_t t461 = GM_LD(uint64_t, (((uint8_t *)&F.l44) + 48));
                        GM_ST(uint64_t, (uint8_t *)&t453 + 16, t461);
                        gm_tftb1odc739t5 t462 = gm_f_push_q9fsb22(t452, &t453);
                        gm_tftb1odc739t5 t463 = t462;
                        if (((((uint8_t *)&t463))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t463) + 1, gm_srckfibkpt681aa, 276, 9);
                        gm_b0_1 t464 = GM_LD(gm_b0_1, (((uint8_t *)&t463) + 1));
                    }
                    {
                        uint64_t t465 = GM_LD(uint64_t, (((uint8_t *)&F.l44) + 24));
                        uint64_t t466 = (t465 > ((uint64_t)1ULL) ? t465 : ((uint64_t)1ULL));
                        gm_l48 = t466;
                    }
                    {
                        uint64_t t467 = GM_LD(uint64_t, (((uint8_t *)&F.l44) + 48));
                        uint64_t t468 = GM_LD(uint64_t, (((uint8_t *)&F.l44) + 16));
                        uint64_t t469 = gm_l48;
                        uint64_t r470; bool o471 = __builtin_add_overflow(t468, t469, &r470);
                        if (o471) gm_trap("integer overflow in `+`", gm_srckfibkpt681aa, 279, 27);
                        uint64_t t472 = r470;
                        uint64_t r473; bool o474 = __builtin_sub_overflow(t472, ((uint64_t)1ULL), &r473);
                        if (o474) gm_trap("integer overflow in `-`", gm_srckfibkpt681aa, 279, 27);
                        uint64_t t475 = r473;
                        uint64_t t476 = gm_l48;
                        if (t476 == 0) gm_trap("division by zero", gm_srckfibkpt681aa, 279, 26);
                        uint64_t t477 = (uint64_t)(t475 / t476);
                        uint64_t t478 = gm_l48;
                        uint64_t r479; bool o480 = __builtin_mul_overflow(t477, t478, &r479);
                        if (o480) gm_trap("integer overflow in `*`", gm_srckfibkpt681aa, 279, 26);
                        uint64_t t481 = r479;
                        uint64_t r482; bool o483 = __builtin_add_overflow(t467, t481, &r482);
                        if (o483) gm_trap("integer overflow in `+`", gm_srckfibkpt681aa, 279, 18);
                        uint64_t t484 = r482;
                        gm_l39 = t484;
                    }
                    goto me381;
mn382:;
                    gm_trap("no match arm matched the scrutinee", gm_srckfibkpt681aa, 270, 5);
me381:;
                }
lc369:;
                {
                    uint64_t t486 = gm_l42;
                    uint64_t r487; bool o488 = __builtin_add_overflow(t486, ((uint64_t)1ULL), &r487);
                    uint64_t t489 = r487;
                    gm_l42 = t489;
                }
            }
gm_ll373:;
            if (gm_unw.frame == &F.h && gm_unw.label == 3) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb369; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc369; }
            }
            goto gm_lad;
lb369:;
        }
    }
    {
        void * t491 = gm_l0;
        gm_t88rcucg1b7uk t492 = gm_l1;
        uint64_t t493 = gm_f_size_dmhtkib((void *)NULL, cx, NULL, t491, t492);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint64_t t494 = gm_l39;
        uint8_t t495 = (t493 > t494);
        if (!t495) goto ie490;
        {
            void * t497 = (void *)((uint8_t *)&F.l37);
            void * t498 = gm_l0;
            gm_t88rcucg1b7uk t499 = gm_l1;
            uint64_t t500 = gm_f_size_dmhtkib((void *)NULL, cx, NULL, t498, t499);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint64_t t501 = gm_l39;
            uint64_t r502; bool o503 = __builtin_sub_overflow(t500, t501, &r502);
            if (o503) gm_trap("integer overflow in `-`", gm_srckfibkpt681aa, 281, 73);
            uint64_t t504 = r502;
            gm_t4ce0jd3tdro4 t505;
            GM_ST(uint64_t, ((uint8_t *)&t505 + 0), t504);
            gm_tabb710d9mq2e t506;
            gm_f_format_kl8i1sq((void *)&t506, (void *)NULL, cx, NULL, t505);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t507 = t506;
            if (((((uint8_t *)&t507))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t507) + 8, gm_srckfibkpt681aa, 281, 43);
            gm_b16_8 t508 = GM_LD(gm_b16_8, (((uint8_t *)&t507) + 8));
            gm_b16_8 t509 = t508;
            gm_b16_8 t510 = GM_LD(gm_b16_8, ((uint8_t *)&t509));
            gm_tftb1odc739t5 t511 = gm_f_push_3ef746b(t497, t510);
            gm_tftb1odc739t5 t512 = t511;
            if (((((uint8_t *)&t512))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t512) + 1, gm_srckfibkpt681aa, 281, 30);
            gm_b0_1 t513 = GM_LD(gm_b0_1, (((uint8_t *)&t512) + 1));
        }
        goto ix490;
ie490:;
ix490:;
    }
    {
        void * t514 = gm_l0;
        gm_t88rcucg1b7uk t515 = gm_l1;
        gm_b16_8 t516 = GM_LD(gm_b16_8, (((uint8_t *)&F.l37) + 0));
        gm_b16_8 t517 = t516;
        gm_b16_8 t518 = GM_LD(gm_b16_8, ((uint8_t *)&t517));
        gm_b16_8 t519;
        GM_ST(void *, (uint8_t *)&t519, (void *)(gm_s3p0rbs9hc3lg));
        GM_ST(uintptr_t, (uint8_t *)&t519 + 8, (uintptr_t)(1));
        gm_tabb710d9mq2e t520;
        gm_f_join_sfknljb((void *)&t520, (void *)NULL, cx, NULL, t518, t519);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t521 = t520;
        if (((((uint8_t *)&t521))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t521) + 8, gm_srckfibkpt681aa, 282, 31);
        gm_b16_8 t522 = GM_LD(gm_b16_8, (((uint8_t *)&t521) + 8));
        gm_b16_8 t523 = t522;
        gm_b16_8 t524 = GM_LD(gm_b16_8, ((uint8_t *)&t523));
        gm_b16_8 t525 = GM_LD(gm_b16_8, (((uint8_t *)&F.l38) + 0));
        gm_b16_8 t526 = t525;
        gm_b16_8 t527 = GM_LD(gm_b16_8, ((uint8_t *)&t526));
        uint64_t t528 = gm_l40;
        gm_b16_8 t529 = gm_f_define_typed_m3mqpm3((void *)NULL, cx, NULL, t514, t515, t524, t527, t528);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_th2lqbo77pvsc t530;
        GM_ST(uint8_t, (uint8_t *)&t530 + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&t530 + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&t530 + 20, 0);
        GM_ST(gm_b16_8, ((uint8_t *)&t530), t529);
        ((uint8_t *)&t530)[16] = 1;
        uint8_t t531 = 0;
        F.ret = t530;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_member_names_17el54k(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_member_names_17el54k F;
    void * gm_l0; /* ty */
    void * gm_l2; /* <tmp> */
    GM_STACK_CHECK(&F, 121u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_epvducg((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            void * t3 = gm_l0;
            void * t4 = (void *)(((uint8_t *)t3) + 816);
            gm_l2 = t4;
        }
        gm_hval t5 = ((gm_hval){(void *)&F.h, cx});
        gm_node wn6 = {.eff = 993381376, .kind = GM_LIT, .clauses = gm_hlak2jvvbqg4, .env = t5.env, .hctx = t5.cx, .with_frame = &F.h, .with_label = 0, .result = (void *)&F.w0, .fw = NULL, .next = cx};
        void * t9 = gm_l2;
        gm_b0_1 t10 = gm_f_keys_qc5bjlr_s31qjug((void *)NULL, (&wn6), NULL, t9);
        if (gm_unw.kind) {
            goto gm_ll8;
        }
        F.w0 = t10;
        goto gm_ls11;
gm_ll8:;
        if (gm_unw.kind == GM_BREAK && gm_unw.frame == &F.h && gm_unw.label == 0) { gm_unw.kind = GM_NONE; goto we7; }
        goto gm_lad;
gm_ls11:;
we7:;
        gm_b0_1 t12 = F.w0;
        gm_b0_1 t13 = t12;
    }
    {
        gm_b16_8 t14 = GM_LD(gm_b16_8, (((uint8_t *)&F.l1) + 0));
        gm_f_sort_skeree7(t14);
    }
    {
        gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&F.l1) + 0));
        uint8_t t16 = 0;
        F.ret = t15;
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
inline __attribute__((aligned(4))) void gm_f_mirror_lcjegsf(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_mirror_lcjegsf F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_th2lqbo77pvsc gm_l2; /* m */
    gm_th2lqbo77pvsc gm_l3; /* m */
    GM_STACK_CHECK(&F, 204u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t3 = gm_l0;
        void * t4 = (void *)(((uint8_t *)t3) + 600);
        uint32_t t5 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 0));
        gm_ti5ramhglnvdt t6;
        gm_f_get_9k2rdij((void *)&t6, t4, t5);
        gm_ti5ramhglnvdt t7 = t6;
        if (!((((uint8_t *)&t7))[24] != 0)) goto ie2;
        gm_th2lqbo77pvsc t8 = GM_LD(gm_th2lqbo77pvsc, ((uint8_t *)&t7));
        gm_l2 = t8;
        {
            memmove(gm_out, ((uint8_t *)&gm_l2), 24);
            goto gm_fdone;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t10 = gm_l0;
        gm_t88rcucg1b7uk t11 = gm_l1;
        gm_f_make_mirror_rudvl6p((void *)&gm_l3, (void *)NULL, cx, NULL, t10, t11);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t12 = gm_l0;
        void * t13 = (void *)(((uint8_t *)t12) + 600);
        uint32_t t14 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 0));
        gm_tftb1odc739t5 t15 = gm_f_put_dm7u1sg(t13, t14, &(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l3)));
        gm_tftb1odc739t5 t16 = t15;
        if (((((uint8_t *)&t16))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t16) + 1, gm_srckfibkpt681aa, 341, 5);
        gm_b0_1 t17 = GM_LD(gm_b0_1, (((uint8_t *)&t16) + 1));
    }
    {
        memmove(gm_out, ((uint8_t *)&gm_l3), 24);
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
extern __attribute__((aligned(4))) gm_b16_8 gm_f_mirror_text_cr5muc9(void * a0);
extern __attribute__((aligned(4))) gm_b16_8 gm_f_offsets_eh6vfle(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
extern __attribute__((aligned(4))) gm_b16_8 gm_f_param_cty_e8tvhrm(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_payload_union_feshns2(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1) {
    struct gm_fr_payload_union_feshns2 F;
    void * gm_l0; /* ty */
    gm_b16_8 gm_l1; /* tys */
    uint64_t gm_l3; /* size */
    uint64_t gm_l4; /* align */
    gm_b16_8 gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    uint64_t gm_l7; /* i */
    gm_t88rcucg1b7uk gm_l8; /* t */
    GM_STACK_CHECK(&F, 845u);
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
        gm_l3 = ((uint64_t)0ULL);
    }
    {
        gm_l4 = ((uint64_t)1ULL);
    }
    {
        {
            gm_b16_8 t3 = gm_l1;
            gm_l5 = t3;
        }
        {
            gm_l6 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t5 = gm_l6;
                uint64_t t6 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                uint8_t t7 = (t5 < t6);
                if (!t7) goto lb4;
                {
                    uint64_t t10 = gm_l6;
                    uint64_t t11 = gm_l6;
                    uint64_t t12 = (uintptr_t)t11;
                    uint64_t t13 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                    gm_t88rcucg1b7uk t14 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l5)) + t12 * 4));
                    gm_tlcm4lvu3g1a8 t15;
                    GM_ST(uint32_t, (uint8_t *)&t15 + 12, 0);
                    GM_ST(uint64_t, ((uint8_t *)&t15 + 0), t10);
                    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t15 + 8), t14);
                    gm_tlcm4lvu3g1a8 t16 = t15;
                    uint64_t t19 = GM_LD(uint64_t, (((uint8_t *)&t16) + 0));
                    gm_l7 = t19;
                    gm_t88rcucg1b7uk t20 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t16) + 8));
                    gm_l8 = t20;
                    {
                        void * t23 = gm_l0;
                        gm_t88rcucg1b7uk t24 = gm_l8;
                        uint64_t t25 = gm_f_size_dmhtkib((void *)NULL, cx, NULL, t23, t24);
                        if (gm_unw.kind) {
                            goto gm_ll8;
                        }
                        uint8_t t26 = (t25 > ((uint64_t)0ULL));
                        if (!t26) goto ie22;
                        {
                            void * t28 = (void *)((uint8_t *)&F.l2);
                            gm_t3877pk7lv748 t29;
                            void * t30 = gm_l0;
                            gm_t88rcucg1b7uk t31 = gm_l8;
                            gm_b16_8 t32 = gm_f_cty_66ee0kf((void *)NULL, cx, NULL, t30, t31);
                            if (gm_unw.kind) {
                                goto gm_ll8;
                            }
                            GM_ST(gm_b16_8, (uint8_t *)&t29 + 0, t32);
                            uint64_t t33 = gm_l7;
                            GM_ST(uint64_t, (uint8_t *)&t29 + 16, t33);
                            gm_tabb710d9mq2e t34;
                            gm_f_format_3buamil((void *)&t34, (void *)NULL, cx, NULL, &t29);
                            if (gm_unw.kind) {
                                goto gm_ll8;
                            }
                            gm_tabb710d9mq2e t35 = t34;
                            if (((((uint8_t *)&t35))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t35) + 8, gm_srckfibkpt681aa, 299, 26);
                            gm_b16_8 t36 = GM_LD(gm_b16_8, (((uint8_t *)&t35) + 8));
                            gm_b16_8 t37 = t36;
                            gm_b16_8 t38 = GM_LD(gm_b16_8, ((uint8_t *)&t37));
                            gm_tftb1odc739t5 t39 = gm_f_push_3ef746b(t28, t38);
                            gm_tftb1odc739t5 t40 = t39;
                            if (((((uint8_t *)&t40))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t40) + 1, gm_srckfibkpt681aa, 299, 13);
                            gm_b0_1 t41 = GM_LD(gm_b0_1, (((uint8_t *)&t40) + 1));
                        }
                        {
                            uint64_t t42 = gm_l3;
                            void * t43 = gm_l0;
                            gm_t88rcucg1b7uk t44 = gm_l8;
                            uint64_t t45 = gm_f_size_dmhtkib((void *)NULL, cx, NULL, t43, t44);
                            if (gm_unw.kind) {
                                goto gm_ll8;
                            }
                            uint64_t t46 = (t42 > t45 ? t42 : t45);
                            gm_l3 = t46;
                        }
                        {
                            uint64_t t47 = gm_l4;
                            void * t48 = gm_l0;
                            gm_t88rcucg1b7uk t49 = gm_l8;
                            uint64_t t50 = gm_f_align_uu2harl((void *)NULL, cx, NULL, t48, t49);
                            if (gm_unw.kind) {
                                goto gm_ll8;
                            }
                            uint64_t t51 = (t47 > t50 ? t47 : t50);
                            gm_l4 = t51;
                        }
                        goto ix22;
ie22:;
ix22:;
                    }
                    goto me17;
mn18:;
                    gm_trap("no match arm matched the scrutinee", gm_srckfibkpt681aa, 297, 5);
me17:;
                }
lc4:;
                {
                    uint64_t t53 = gm_l6;
                    uint64_t r54; bool o55 = __builtin_add_overflow(t53, ((uint64_t)1ULL), &r54);
                    uint64_t t56 = r54;
                    gm_l6 = t56;
                }
            }
gm_ll8:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb4; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc4; }
            }
            goto gm_lad;
lb4:;
        }
    }
    {
        uint64_t t58 = GM_LD(uintptr_t, (((uint8_t *)&F.l2) + 0) + 8);
        uint8_t t59 = ((t58 == ((uint64_t)0ULL)));
        if (!t59) goto ie57;
        {
            gm_t8ldkt09h6492 t61;
            memset(&t61, 0, sizeof t61);
            uint8_t t62 = 0;
            F.ret = t61;
            goto gm_fret;
        }
        goto ix57;
ie57:;
ix57:;
    }
    {
        gm_tgbgti82mf879 t63;
        gm_b16_8 t64 = GM_LD(gm_b16_8, (((uint8_t *)&F.l2) + 0));
        gm_b16_8 t65 = t64;
        gm_b16_8 t66 = GM_LD(gm_b16_8, ((uint8_t *)&t65));
        gm_b16_8 t67;
        GM_ST(void *, (uint8_t *)&t67, (void *)(gm_s3p0rbs9hc3lg));
        GM_ST(uintptr_t, (uint8_t *)&t67 + 8, (uintptr_t)(1));
        gm_tabb710d9mq2e t68;
        gm_f_join_sfknljb((void *)&t68, (void *)NULL, cx, NULL, t66, t67);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t69 = t68;
        if (((((uint8_t *)&t69))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t69) + 8, gm_srckfibkpt681aa, 305, 59);
        gm_b16_8 t70 = GM_LD(gm_b16_8, (((uint8_t *)&t69) + 8));
        gm_tnpifv20irnph t71;
        GM_ST(gm_b16_8, ((uint8_t *)&t71 + 0), t70);
        gm_tabb710d9mq2e t72;
        gm_f_format_vgl5p2e((void *)&t72, (void *)NULL, cx, NULL, t71);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t73 = t72;
        if (((((uint8_t *)&t73))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t73) + 8, gm_srckfibkpt681aa, 305, 30);
        gm_b16_8 t74 = GM_LD(gm_b16_8, (((uint8_t *)&t73) + 8));
        gm_b16_8 t75 = t74;
        gm_b16_8 t76 = GM_LD(gm_b16_8, ((uint8_t *)&t75));
        GM_ST(gm_b16_8, (uint8_t *)&t63 + 0, t76);
        uint64_t t77 = gm_l3;
        GM_ST(uint64_t, (uint8_t *)&t63 + 16, t77);
        uint64_t t78 = gm_l4;
        GM_ST(uint64_t, (uint8_t *)&t63 + 24, t78);
        gm_t8ldkt09h6492 t79;
        GM_ST(uint8_t, (uint8_t *)&t79 + 33, 0);
        GM_ST(uint16_t, (uint8_t *)&t79 + 34, 0);
        GM_ST(uint32_t, (uint8_t *)&t79 + 36, 0);
        GM_ST(gm_tgbgti82mf879, ((uint8_t *)&t79), t63);
        ((uint8_t *)&t79)[32] = 1;
        uint8_t t80 = 0;
        F.ret = t79;
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
extern __attribute__((aligned(4))) gm_b16_8 gm_f_plain_cty_q9lhlci(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_repr_53quqki(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_repr_53quqki F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    uint8_t gm_l2; /* signed */
    uint16_t gm_l3; /* bits */
    uint16_t gm_l4; /* bits */
    gm_t88rcucg1b7uk gm_l5; /* inner */
    GM_STACK_CHECK(&F, 697u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    gm_t88rcucg1b7uk t2 = gm_l1;
    gm_t53at80pfdus3 t3;
    gm_f_kind_ghvav22((void *)&t3, t1, GM_CONV(gm_csguankko4lmhd, t2));
    gm_t53at80pfdus3 t4 = t3;
    gm_t5v8n6qrn4rh4 t5;
    uint8_t t18 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
    switch (t18) { case ((uint8_t)0ULL): goto ma7; case ((uint8_t)1ULL): goto ma8; case ((uint8_t)2ULL): goto ma9; case ((uint8_t)3ULL): goto ma10; case ((uint8_t)4ULL): goto ma11; case ((uint8_t)5ULL): goto ma16; case ((uint8_t)6ULL): goto ma16; case ((uint8_t)7ULL): goto ma12; case ((uint8_t)8ULL): goto ma12; case ((uint8_t)9ULL): goto ma12; case ((uint8_t)10ULL): goto ma16; case ((uint8_t)11ULL): goto ma16; case ((uint8_t)12ULL): goto ma16; case ((uint8_t)13ULL): goto ma13; case ((uint8_t)14ULL): goto ma16; case ((uint8_t)15ULL): goto ma14; case ((uint8_t)16ULL): goto ma12; case ((uint8_t)17ULL): goto ma16; case ((uint8_t)18ULL): goto ma16; case ((uint8_t)19ULL): goto ma16; case ((uint8_t)20ULL): goto ma16; case ((uint8_t)21ULL): goto ma16; case ((uint8_t)22ULL): goto ma16; case ((uint8_t)23ULL): goto ma16; case ((uint8_t)24ULL): goto ma16; case ((uint8_t)25ULL): goto ma15; case ((uint8_t)26ULL): goto ma16; default: goto ma16; }
ma7:;
    uint8_t t20 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
    if (t20 != ((uint8_t)0ULL)) goto mn19;
    uint8_t t21 = GM_LD(uint8_t, ((((uint8_t *)&t4) + 8) + 0));
    gm_l2 = t21;
    uint16_t t22 = GM_LD(uint16_t, ((((uint8_t *)&t4) + 8) + 2));
    gm_l3 = t22;
    uint8_t t23 = gm_l2;
    uint16_t t24 = gm_l3;
    uint32_t t25 = (uint32_t)t24;
    void * t26 = gm_l0;
    gm_t88rcucg1b7uk t27 = gm_l1;
    uint64_t t28 = gm_f_size_dmhtkib((void *)NULL, cx, NULL, t26, t27);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tuglhjjmj2g58 t29;
    GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
    GM_ST(uint8_t, ((uint8_t *)&t29 + 0), t23);
    GM_ST(uint32_t, ((uint8_t *)&t29 + 4), t25);
    GM_ST(uint64_t, ((uint8_t *)&t29 + 8), t28);
    gm_t5v8n6qrn4rh4 t30;
    GM_ST(uint8_t, (uint8_t *)&t30 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t30 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t30 + 4, 0);
    GM_ST(uint8_t, ((uint8_t *)&t30 + 0), ((uint8_t)0ULL));
    GM_ST(gm_tuglhjjmj2g58, ((uint8_t *)&t30 + 8), t29);
    t5 = t30;
    goto me6;
mn19:;
ma8:;
    uint8_t t32 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
    if (t32 != ((uint8_t)1ULL)) goto mn31;
    void * t33 = gm_l0;
    void * t34 = (void *)((((uint8_t *)t33) + 8) + 0);
    uint32_t t35 = gm_f_ptr_bits_glps5i7(t34);
    void * t36 = gm_l0;
    uint64_t t37 = GM_LD(uint64_t, (((((uint8_t *)t36) + 8) + 0) + 0));
    gm_tuglhjjmj2g58 t38;
    GM_ST(uint8_t, (uint8_t *)&t38 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t38 + 2, 0);
    GM_ST(uint8_t, ((uint8_t *)&t38 + 0), ((uint8_t)0));
    GM_ST(uint32_t, ((uint8_t *)&t38 + 4), t35);
    GM_ST(uint64_t, ((uint8_t *)&t38 + 8), t37);
    gm_t5v8n6qrn4rh4 t39;
    GM_ST(uint8_t, (uint8_t *)&t39 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t39 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t39 + 4, 0);
    GM_ST(uint8_t, ((uint8_t *)&t39 + 0), ((uint8_t)0ULL));
    GM_ST(gm_tuglhjjmj2g58, ((uint8_t *)&t39 + 8), t38);
    t5 = t39;
    goto me6;
mn31:;
ma9:;
    uint8_t t41 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
    if (t41 != ((uint8_t)2ULL)) goto mn40;
    void * t42 = gm_l0;
    void * t43 = (void *)((((uint8_t *)t42) + 8) + 0);
    uint32_t t44 = gm_f_ptr_bits_glps5i7(t43);
    void * t45 = gm_l0;
    uint64_t t46 = GM_LD(uint64_t, (((((uint8_t *)t45) + 8) + 0) + 0));
    gm_tuglhjjmj2g58 t47;
    GM_ST(uint8_t, (uint8_t *)&t47 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t47 + 2, 0);
    GM_ST(uint8_t, ((uint8_t *)&t47 + 0), ((uint8_t)1));
    GM_ST(uint32_t, ((uint8_t *)&t47 + 4), t44);
    GM_ST(uint64_t, ((uint8_t *)&t47 + 8), t46);
    gm_t5v8n6qrn4rh4 t48;
    GM_ST(uint8_t, (uint8_t *)&t48 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t48 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t48 + 4, 0);
    GM_ST(uint8_t, ((uint8_t *)&t48 + 0), ((uint8_t)0ULL));
    GM_ST(gm_tuglhjjmj2g58, ((uint8_t *)&t48 + 8), t47);
    t5 = t48;
    goto me6;
mn40:;
ma10:;
    uint8_t t50 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
    if (t50 != ((uint8_t)3ULL)) goto mn49;
    uint16_t t51 = GM_LD(uint16_t, (((uint8_t *)&t4) + 8));
    gm_l4 = t51;
    uint16_t t52 = gm_l4;
    gm_t5v8n6qrn4rh4 t53;
    GM_ST(uint8_t, (uint8_t *)&t53 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t53 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t53 + 4, 0);
    GM_ST(uint16_t, (uint8_t *)&t53 + 10, 0);
    GM_ST(uint32_t, (uint8_t *)&t53 + 12, 0);
    GM_ST(uint64_t, (uint8_t *)&t53 + 16, 0);
    GM_ST(uint8_t, ((uint8_t *)&t53 + 0), ((uint8_t)1ULL));
    GM_ST(uint16_t, ((uint8_t *)&t53 + 8), t52);
    t5 = t53;
    goto me6;
mn49:;
ma11:;
    uint8_t t55 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
    if (t55 != ((uint8_t)4ULL)) goto mn54;
    gm_t5v8n6qrn4rh4 t56;
    GM_ST(uint8_t, (uint8_t *)&t56 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t56 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t56 + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&t56 + 8, 0);
    GM_ST(uint64_t, (uint8_t *)&t56 + 16, 0);
    GM_ST(uint8_t, ((uint8_t *)&t56 + 0), ((uint8_t)2ULL));
    t5 = t56;
    goto me6;
mn54:;
ma12:;
    uint8_t t60 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
    if (t60 != ((uint8_t)8ULL)) goto pa59;
    goto po58;
pa59:;
    uint8_t t62 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
    if (t62 != ((uint8_t)9ULL)) goto pa61;
    goto po58;
pa61:;
    uint8_t t64 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
    if (t64 != ((uint8_t)7ULL)) goto pa63;
    goto po58;
pa63:;
    uint8_t t66 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
    if (t66 != ((uint8_t)16ULL)) goto pa65;
    goto po58;
pa65:;
    goto mn57;
po58:;
    gm_t5v8n6qrn4rh4 t67;
    GM_ST(uint8_t, (uint8_t *)&t67 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t67 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t67 + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&t67 + 8, 0);
    GM_ST(uint64_t, (uint8_t *)&t67 + 16, 0);
    GM_ST(uint8_t, ((uint8_t *)&t67 + 0), ((uint8_t)3ULL));
    t5 = t67;
    goto me6;
mn57:;
ma13:;
    uint8_t t69 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
    if (t69 != ((uint8_t)13ULL)) goto mn68;
    gm_t88rcucg1b7uk t70 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t4) + 8));
    gm_l5 = t70;
    gm_t5v8n6qrn4rh4 t71;
    void * t73 = gm_l0;
    void * t74 = GM_LD(void *, (((uint8_t *)t73) + 0));
    gm_t88rcucg1b7uk t75 = gm_l5;
    uint8_t t76 = gm_f_nullable_pointer_7qfmn6o(t74, GM_CONV(gm_csguankko4lmhd, t75));
    if (!t76) goto ie72;
    gm_t5v8n6qrn4rh4 t78;
    GM_ST(uint8_t, (uint8_t *)&t78 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t78 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t78 + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&t78 + 8, 0);
    GM_ST(uint64_t, (uint8_t *)&t78 + 16, 0);
    GM_ST(uint8_t, ((uint8_t *)&t78 + 0), ((uint8_t)3ULL));
    gm_t5v8n6qrn4rh4 t79 = t78;
    t71 = t79;
    goto ix72;
ie72:;
    gm_t5v8n6qrn4rh4 t81;
    GM_ST(uint8_t, (uint8_t *)&t81 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t81 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t81 + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&t81 + 8, 0);
    GM_ST(uint64_t, (uint8_t *)&t81 + 16, 0);
    GM_ST(uint8_t, ((uint8_t *)&t81 + 0), ((uint8_t)6ULL));
    gm_t5v8n6qrn4rh4 t82 = t81;
    t71 = t82;
ix72:;
    t5 = t71;
    goto me6;
mn68:;
ma14:;
    uint8_t t84 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
    if (t84 != ((uint8_t)15ULL)) goto mn83;
    gm_t5v8n6qrn4rh4 t85;
    GM_ST(uint8_t, (uint8_t *)&t85 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t85 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t85 + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&t85 + 8, 0);
    GM_ST(uint64_t, (uint8_t *)&t85 + 16, 0);
    GM_ST(uint8_t, ((uint8_t *)&t85 + 0), ((uint8_t)4ULL));
    t5 = t85;
    goto me6;
mn83:;
ma15:;
    uint8_t t87 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
    if (t87 != ((uint8_t)25ULL)) goto mn86;
    gm_t5v8n6qrn4rh4 t88;
    GM_ST(uint8_t, (uint8_t *)&t88 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t88 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t88 + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&t88 + 8, 0);
    GM_ST(uint64_t, (uint8_t *)&t88 + 16, 0);
    GM_ST(uint8_t, ((uint8_t *)&t88 + 0), ((uint8_t)5ULL));
    t5 = t88;
    goto me6;
mn86:;
ma16:;
    gm_t5v8n6qrn4rh4 t90;
    GM_ST(uint8_t, (uint8_t *)&t90 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t90 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t90 + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&t90 + 8, 0);
    GM_ST(uint64_t, (uint8_t *)&t90 + 16, 0);
    GM_ST(uint8_t, ((uint8_t *)&t90 + 0), ((uint8_t)6ULL));
    t5 = t90;
    goto me6;
mn89:;
ma17:;
    gm_trap("no match arm matched the scrutinee", gm_srckfibkpt681aa, 143, 55);
me6:;
    F.ret = t5;
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
extern __attribute__((aligned(4))) gm_b16_8 gm_f_show_5qbi623(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
extern __attribute__((aligned(4))) uint64_t gm_f_size_dmhtkib(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
extern __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_string_ty_kk94re3(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) void gm_f_typed_stkcgvr(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_typed_stkcgvr F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_th2lqbo77pvsc gm_l2; /* m */
    gm_th2lqbo77pvsc gm_l3; /* m */
    GM_STACK_CHECK(&F, 204u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t3 = gm_l0;
        void * t4 = (void *)(((uint8_t *)t3) + 408);
        uint32_t t5 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 0));
        gm_ti5ramhglnvdt t6;
        gm_f_get_9k2rdij((void *)&t6, t4, t5);
        gm_ti5ramhglnvdt t7 = t6;
        if (!((((uint8_t *)&t7))[24] != 0)) goto ie2;
        gm_th2lqbo77pvsc t8 = GM_LD(gm_th2lqbo77pvsc, ((uint8_t *)&t7));
        gm_l2 = t8;
        {
            memmove(gm_out, ((uint8_t *)&gm_l2), 24);
            goto gm_fdone;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t10 = gm_l0;
        gm_t88rcucg1b7uk t11 = gm_l1;
        gm_f_make_typed_2bh0dpj((void *)&gm_l3, (void *)NULL, cx, NULL, t10, t11);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t12 = gm_l0;
        void * t13 = (void *)(((uint8_t *)t12) + 408);
        uint32_t t14 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 0));
        gm_tftb1odc739t5 t15 = gm_f_put_dm7u1sg(t13, t14, &(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l3)));
        gm_tftb1odc739t5 t16 = t15;
        if (((((uint8_t *)&t16))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t16) + 1, gm_srckfibkpt681aa, 200, 5);
        gm_b0_1 t17 = GM_LD(gm_b0_1, (((uint8_t *)&t16) + 1));
    }
    {
        memmove(gm_out, ((uint8_t *)&gm_l3), 24);
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
extern __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_unit_ty_vgceilu(void *env, gm_node *cx, gm_node *inst, void * a0);
extern __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_usize_ty_9mijhtc(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_vector_typedef_vpgf6u8(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2, uint64_t a3, uint64_t a4) {
    struct gm_fr_vector_typedef_vpgf6u8 F;
    void * gm_l0; /* ty */
    gm_b16_8 gm_l1; /* lane */
    gm_b16_8 gm_l2; /* tag */
    uint64_t gm_l3; /* n */
    uint64_t gm_l4; /* bytes */
    gm_b16_8 gm_l5; /* name */
    GM_STACK_CHECK(&F, 518u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    {
        gm_tf1bjg81u0glv t2;
        uint64_t t3 = gm_l3;
        GM_ST(uint64_t, (uint8_t *)&t2 + 0, t3);
        gm_b16_8 t4 = gm_l2;
        GM_ST(gm_b16_8, (uint8_t *)&t2 + 8, t4);
        gm_tabb710d9mq2e t5;
        gm_f_format_dtmr1c2((void *)&t5, (void *)NULL, cx, NULL, &t2);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t6 = t5;
        if (((((uint8_t *)&t6))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t6) + 8, gm_srckfibkpt681aa, 328, 13);
        gm_b16_8 t7 = GM_LD(gm_b16_8, (((uint8_t *)&t6) + 8));
        gm_l5 = t7;
    }
    {
        void * t9 = gm_l0;
        void * t10 = (void *)(((uint8_t *)t9) + 752);
        gm_b16_8 t11 = gm_l5;
        gm_b16_8 t12 = t11;
        gm_b16_8 t13 = GM_LD(gm_b16_8, ((uint8_t *)&t12));
        gm_tqs5p4n2tq19v t14 = gm_f_get_p217beu(t10, t13);
        gm_tqs5p4n2tq19v t15 = t14;
        uint8_t t16;
        if (((((uint8_t *)&t15))[1] != 0)) goto mn18;
        t16 = ((uint8_t)1);
        goto me17;
mn18:;
        t16 = ((uint8_t)0);
        goto me17;
mn19:;
        gm_trap("no match arm matched the scrutinee", gm_srckfibkpt681aa, 329, 8);
me17:;
        if (!t16) goto ie8;
        {
            void * t21 = gm_l0;
            void * t22 = (void *)(((uint8_t *)t21) + 752);
            gm_b16_8 t23 = gm_l5;
            gm_b16_8 t24 = t23;
            gm_b16_8 t25 = GM_LD(gm_b16_8, ((uint8_t *)&t24));
            gm_tftb1odc739t5 t26 = gm_f_put_5c6qgui(t22, t25, ((uint8_t)1));
            gm_tftb1odc739t5 t27 = t26;
            if (((((uint8_t *)&t27))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t27) + 1, gm_srckfibkpt681aa, 330, 9);
            gm_b0_1 t28 = GM_LD(gm_b0_1, (((uint8_t *)&t27) + 1));
        }
        {
            void * t29 = gm_l0;
            void * t30 = (void *)(((uint8_t *)t29) + 560);
            gm_tftksr4op114e t31;
            gm_b16_8 t32 = gm_l1;
            GM_ST(gm_b16_8, (uint8_t *)&t31 + 0, t32);
            gm_b16_8 t33 = gm_l5;
            GM_ST(gm_b16_8, (uint8_t *)&t31 + 16, t33);
            uint64_t t34 = gm_l3;
            uint64_t t35 = gm_l4;
            uint64_t r36; bool o37 = __builtin_mul_overflow(t34, t35, &r36);
            if (o37) gm_trap("integer overflow in `*`", gm_srckfibkpt681aa, 331, 94);
            uint64_t t38 = r36;
            GM_ST(uint64_t, (uint8_t *)&t31 + 32, t38);
            gm_tftb1odc739t5 t39 = gm_f_write_1h8ac6i(t30, &t31);
            gm_tftb1odc739t5 t40 = t39;
            if (((((uint8_t *)&t40))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t40) + 1, gm_srckfibkpt681aa, 331, 9);
            gm_b0_1 t41 = GM_LD(gm_b0_1, (((uint8_t *)&t40) + 1));
        }
        goto ix8;
ie8:;
ix8:;
    }
    {
        gm_b16_8 t42 = gm_l5;
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
