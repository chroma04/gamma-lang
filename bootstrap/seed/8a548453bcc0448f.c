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
typedef struct gm_cseahvg8v7p0vs { uint32_t f0; uint32_t f1; uint32_t f2; } gm_cseahvg8v7p0vs;
typedef struct gm_csguankko4lmhd { uint32_t f0; } gm_csguankko4lmhd;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint64_t v0; } f1; } gm_t8c5h7u09n10a;
typedef struct { uint32_t f0; uint8_t f1; uint8_t pe[3]; } gm_t9805qtonbsag;
typedef struct { unsigned __int128 f0; uint8_t f1; uint8_t pe[15]; } gm_t9p38b040grms;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { void * f0; void * f1; void * f2; uint8_t f3; uint8_t pe[7]; } gm_tao63pk3r18v5;
typedef struct { uint32_t f0; uint32_t f1; } gm_tcc1rq4n1avb2;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint8_t f0; uint8_t p1[7]; void * f1; } gm_tgtub0v94d5u5;
typedef struct { uint8_t f0; uint8_t p1[1]; uint16_t f1; } gm_tjcj4j2gbh6c8;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { void * f0; void * f1; uint32_t f2; uint32_t f3; } gm_tmm67r2c94d5j;
typedef struct { uint8_t f0; uint8_t f1; } gm_tqs5p4n2tq19v;
_Static_assert(sizeof(gm_cseahvg8v7p0vs) == 12 && _Alignof(gm_cseahvg8v7p0vs) == 4 && offsetof(gm_cseahvg8v7p0vs, f0) == 0 && offsetof(gm_cseahvg8v7p0vs, f1) == 4 && offsetof(gm_cseahvg8v7p0vs, f2) == 8, "layout of `Span`");
typedef struct gm_csedogq9e4rp5r { uint32_t f0; gm_cseahvg8v7p0vs f1; } gm_csedogq9e4rp5r;
_Static_assert(sizeof(gm_csguankko4lmhd) == 4 && _Alignof(gm_csguankko4lmhd) == 4 && offsetof(gm_csguankko4lmhd, f0) == 0, "layout of `Mutex`");
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_t08olq97rscn3;
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; gm_t9qkh0rsooc8d f2; uint8_t p3[3]; gm_tf5srh4mp4b3i f3; } gm_t0mglksrkfa5e;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; } gm_t0uuhaljsf410;
typedef struct { gm_t88rcucg1b7uk f0; gm_t9qkh0rsooc8d f1; uint8_t pe[3]; } gm_t3jsf43p1pths;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { gm_b16_8 f0; uint64_t f1; uint64_t f2; uint64_t f3; gm_t3mf58gv9kf3n f4; } gm_t4c7ipjj9s3fp;
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; gm_tl874t2atnlh3 f2; void * f3; gm_t9805qtonbsag f4; uint8_t f5; uint8_t pe[7]; } gm_t5v0c8ia6a5mo;
typedef struct { gm_b16_8 a[12]; } gm_t61mja4eihh2b;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_t79rbdv4rafe5;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
_Static_assert(sizeof(gm_t8c5h7u09n10a) == 16 && _Alignof(gm_t8c5h7u09n10a) == 8 && offsetof(gm_t8c5h7u09n10a, f0) == 0 && offsetof(gm_t8c5h7u09n10a, f1) == 8, "layout of `DeclState`");
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; void * f1; } gm_t8ghgl2jj0qha;
_Static_assert(sizeof(gm_t9805qtonbsag) == 8 && _Alignof(gm_t9805qtonbsag) == 4 && offsetof(gm_t9805qtonbsag, f0) == 0 && offsetof(gm_t9805qtonbsag, f1) == 4, "layout of `?u32`");
typedef struct { uint32_t f0; gm_tl874t2atnlh3 f1; } gm_t9d4sf0vqn7dt;
_Static_assert(sizeof(gm_t9p38b040grms) == 32 && _Alignof(gm_t9p38b040grms) == 16 && offsetof(gm_t9p38b040grms, f0) == 0 && offsetof(gm_t9p38b040grms, f1) == 16, "layout of `?u128`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_b16_8 f2; gm_b16_8 f3; } gm_tammvh1iespi3;
_Static_assert(sizeof(gm_tao63pk3r18v5) == 32 && _Alignof(gm_tao63pk3r18v5) == 8 && offsetof(gm_tao63pk3r18v5, f0) == 0 && offsetof(gm_tao63pk3r18v5, f1) == 8 && offsetof(gm_tao63pk3r18v5, f2) == 16 && offsetof(gm_tao63pk3r18v5, f3) == 24, "layout of `SliceExpr`");
typedef struct { gm_tf5srh4mp4b3i f0; } gm_tba9024q2up98;
typedef struct { uint32_t f0; gm_tqs5p4n2tq19v f1; uint8_t f2; uint8_t f3; } gm_tbcgm40aljf9h;
typedef struct { gm_tl874t2atnlh3 f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tbgd14esf0jvi;
typedef struct { uint8_t f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; } gm_tbngattl1fo51;
typedef struct { uint8_t f0; uint8_t p1[3]; gm_tl874t2atnlh3 f1; } gm_tbvif938ns15l;
_Static_assert(sizeof(gm_tcc1rq4n1avb2) == 8 && _Alignof(gm_tcc1rq4n1avb2) == 4 && offsetof(gm_tcc1rq4n1avb2, f0) == 0 && offsetof(gm_tcc1rq4n1avb2, f1) == 4, "layout of `LineCol`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tcmkompbl09il;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; gm_t88rcucg1b7uk f2; } gm_tcmtrmhj7ak4u;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; gm_b16_8 f1; gm_b16_8 f2; void * f3; gm_t9805qtonbsag f4; uint8_t f5; uint8_t pe[7]; } gm_tdgfduqjheqdm;
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t pe[4]; } gm_tep2drsl9408b;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; } gm_tgglga8ro3isc;
_Static_assert(sizeof(gm_tgtub0v94d5u5) == 16 && _Alignof(gm_tgtub0v94d5u5) == 8 && offsetof(gm_tgtub0v94d5u5, f0) == 0 && offsetof(gm_tgtub0v94d5u5, f1) == 8, "layout of `(is_const: bool, inner: ^Expr)`");
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { gm_b16_8 f0; gm_t9qkh0rsooc8d f1; uint8_t pe[7]; } gm_tibupt4dmcqai;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
_Static_assert(sizeof(gm_tjcj4j2gbh6c8) == 4 && _Alignof(gm_tjcj4j2gbh6c8) == 2 && offsetof(gm_tjcj4j2gbh6c8, f0) == 0 && offsetof(gm_tjcj4j2gbh6c8, f1) == 2, "layout of `(signed: bool, bits: u16)`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t f1; uint8_t pe[3]; } gm_tkn42u7na3t81;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
typedef struct { void * f0; gm_b16_8 f1; uint8_t f2; uint8_t pe[7]; } gm_tlljg61a2ctgq;
_Static_assert(sizeof(gm_tmm67r2c94d5j) == 24 && _Alignof(gm_tmm67r2c94d5j) == 8 && offsetof(gm_tmm67r2c94d5j, f0) == 0 && offsetof(gm_tmm67r2c94d5j, f1) == 8 && offsetof(gm_tmm67r2c94d5j, f2) == 16 && offsetof(gm_tmm67r2c94d5j, f3) == 20, "layout of `Printer`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t88rcucg1b7uk v0; } f1; } gm_tnnjeerolos0a;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
typedef struct { gm_tl874t2atnlh3 f0; uint8_t f1; uint8_t pe[3]; } gm_toceffb697p0u;
typedef struct { gm_b16_8 f0; void * f1; } gm_tpe1hv3fekn05;
_Static_assert(sizeof(gm_tqs5p4n2tq19v) == 2 && _Alignof(gm_tqs5p4n2tq19v) == 1 && offsetof(gm_tqs5p4n2tq19v, f0) == 0 && offsetof(gm_tqs5p4n2tq19v, f1) == 1, "layout of `(u8, u8)`");
typedef struct { gm_tl874t2atnlh3 f0; gm_t88rcucg1b7uk f1; } gm_trlabqsjjp0ds;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_t9qkh0rsooc8d v3; } f1; } gm_ts6oqppjh0ba3;
typedef struct { void * f0; uint32_t f1; gm_tl874t2atnlh3 f2; } gm_tupk62cr6lbdu;
typedef struct { gm_b16_8 f0; gm_t9qkh0rsooc8d f1; uint8_t p2[7]; gm_b16_8 f2; gm_t88rcucg1b7uk f3; uint8_t pe[4]; } gm_tv0k2vg3054a6;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; void * f2; gm_t9qkh0rsooc8d f3; uint8_t p4[7]; void * f4; gm_tl874t2atnlh3 f5; uint8_t pe[4]; } gm_tv2uf4leil5hq;
_Static_assert(sizeof(gm_csedogq9e4rp5r) == 16 && _Alignof(gm_csedogq9e4rp5r) == 4 && offsetof(gm_csedogq9e4rp5r, f0) == 0 && offsetof(gm_csedogq9e4rp5r, f1) == 4, "layout of `TraceCall`");
_Static_assert(sizeof(gm_t08olq97rscn3) == 32 && _Alignof(gm_t08olq97rscn3) == 8 && offsetof(gm_t08olq97rscn3, f0) == 0 && offsetof(gm_t08olq97rscn3, f1) == 16, "layout of `File`");
_Static_assert(sizeof(gm_t0mglksrkfa5e) == 40 && _Alignof(gm_t0mglksrkfa5e) == 8 && offsetof(gm_t0mglksrkfa5e, f0) == 0 && offsetof(gm_t0mglksrkfa5e, f1) == 16 && offsetof(gm_t0mglksrkfa5e, f2) == 20 && offsetof(gm_t0mglksrkfa5e, f3) == 24, "layout of `FieldDef`");
_Static_assert(sizeof(gm_t0uuhaljsf410) == 64 && _Alignof(gm_t0uuhaljsf410) == 8 && offsetof(gm_t0uuhaljsf410, f0) == 0 && offsetof(gm_t0uuhaljsf410, f1) == 16 && offsetof(gm_t0uuhaljsf410, f2) == 32 && offsetof(gm_t0uuhaljsf410, f3) == 48, "layout of `(string, string, string, string)`");
typedef struct { gm_t08olq97rscn3 f0; uint8_t f1; uint8_t pe[7]; } gm_t1s75i08qtptd;
_Static_assert(sizeof(gm_t3jsf43p1pths) == 8 && _Alignof(gm_t3jsf43p1pths) == 4 && offsetof(gm_t3jsf43p1pths, f0) == 0 && offsetof(gm_t3jsf43p1pths, f1) == 4, "layout of `Mention`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tcmkompbl09il v0; gm_tiilmfd16v3fc v1; gm_b16_8 v2; } f1; } gm_t428ue2u3hf5g;
_Static_assert(sizeof(gm_t4c7ipjj9s3fp) == 56 && _Alignof(gm_t4c7ipjj9s3fp) == 8 && offsetof(gm_t4c7ipjj9s3fp, f0) == 0 && offsetof(gm_t4c7ipjj9s3fp, f1) == 16 && offsetof(gm_t4c7ipjj9s3fp, f2) == 24 && offsetof(gm_t4c7ipjj9s3fp, f3) == 32 && offsetof(gm_t4c7ipjj9s3fp, f4) == 40, "layout of `HashMap(TyKind, u32, null, null)`");
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t p3[4]; gm_trqfvmjtt4sij f3; gm_trqfvmjtt4sij f4; gm_trqfvmjtt4sij f5; gm_th2lqbo77pvsc f6; } gm_t4gut2c9fj1kt;
_Static_assert(sizeof(gm_t5v0c8ia6a5mo) == 56 && _Alignof(gm_t5v0c8ia6a5mo) == 8 && offsetof(gm_t5v0c8ia6a5mo, f0) == 0 && offsetof(gm_t5v0c8ia6a5mo, f1) == 16 && offsetof(gm_t5v0c8ia6a5mo, f2) == 20 && offsetof(gm_t5v0c8ia6a5mo, f3) == 32 && offsetof(gm_t5v0c8ia6a5mo, f4) == 40 && offsetof(gm_t5v0c8ia6a5mo, f5) == 48, "layout of `ParamInfo`");
_Static_assert(sizeof(gm_t61mja4eihh2b) == 192 && _Alignof(gm_t61mja4eihh2b) == 8 && offsetof(gm_t61mja4eihh2b, a) == 0, "layout of `[12]string`");
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_t79rbdv4rafe5) == 40 && _Alignof(gm_t79rbdv4rafe5) == 8 && offsetof(gm_t79rbdv4rafe5, f0) == 0 && offsetof(gm_t79rbdv4rafe5, f1) == 8 && offsetof(gm_t79rbdv4rafe5, f2) == 24, "layout of `Block`");
typedef struct { void * f0; gm_b16_8 f1; gm_th2lqbo77pvsc f2; gm_tl874t2atnlh3 f3; uint8_t pe[4]; } gm_t7bjbmukv26e7;
typedef struct { gm_tbvif938ns15l f0; uint8_t f1; uint8_t pe[3]; } gm_t84dv0cg48dut;
typedef struct { gm_tba9024q2up98 f0; uint8_t f1; uint8_t pe[7]; } gm_t89h15e2c5o3p;
_Static_assert(sizeof(gm_t8ghgl2jj0qha) == 16 && _Alignof(gm_t8ghgl2jj0qha) == 8 && offsetof(gm_t8ghgl2jj0qha, f0) == 0 && offsetof(gm_t8ghgl2jj0qha, f1) == 8, "layout of `(op: UnOp, operand: ^Expr)`");
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; gm_tnnjeerolos0a f2; uint8_t pe[4]; } gm_t8nckeqfenl8a;
_Static_assert(sizeof(gm_t9d4sf0vqn7dt) == 16 && _Alignof(gm_t9d4sf0vqn7dt) == 4 && offsetof(gm_t9d4sf0vqn7dt, f0) == 0 && offsetof(gm_t9d4sf0vqn7dt, f1) == 4, "layout of `TraceCall`");
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
_Static_assert(sizeof(gm_tammvh1iespi3) == 56 && _Alignof(gm_tammvh1iespi3) == 8 && offsetof(gm_tammvh1iespi3, f0) == 0 && offsetof(gm_tammvh1iespi3, f1) == 16 && offsetof(gm_tammvh1iespi3, f2) == 24 && offsetof(gm_tammvh1iespi3, f3) == 40, "layout of `EffectInfo`");
_Static_assert(sizeof(gm_tba9024q2up98) == 16 && _Alignof(gm_tba9024q2up98) == 8 && offsetof(gm_tba9024q2up98, f0) == 0, "layout of `AliasTarget`");
_Static_assert(sizeof(gm_tbcgm40aljf9h) == 8 && _Alignof(gm_tbcgm40aljf9h) == 4 && offsetof(gm_tbcgm40aljf9h, f0) == 0 && offsetof(gm_tbcgm40aljf9h, f1) == 4 && offsetof(gm_tbcgm40aljf9h, f2) == 6 && offsetof(gm_tbcgm40aljf9h, f3) == 7, "layout of `FnHint`");
typedef struct { uint8_t f0; uint8_t p1[7]; gm_trqfvmjtt4sij f1; } gm_tbci5ospk5nnn;
_Static_assert(sizeof(gm_tbgd14esf0jvi) == 32 && _Alignof(gm_tbgd14esf0jvi) == 8 && offsetof(gm_tbgd14esf0jvi, f0) == 0 && offsetof(gm_tbgd14esf0jvi, f1) == 16, "layout of `Edit`");
_Static_assert(sizeof(gm_tbngattl1fo51) == 8 && _Alignof(gm_tbngattl1fo51) == 4 && offsetof(gm_tbngattl1fo51, f0) == 0 && offsetof(gm_tbngattl1fo51, f1) == 4, "layout of `(is_const: bool, pointee: Ty)`");
_Static_assert(sizeof(gm_tbvif938ns15l) == 16 && _Alignof(gm_tbvif938ns15l) == 4 && offsetof(gm_tbvif938ns15l, f0) == 0 && offsetof(gm_tbvif938ns15l, f1) == 4, "layout of `(win64: bool, span: Span)`");
typedef struct { gm_tibupt4dmcqai f0; gm_tl874t2atnlh3 f1; uint8_t p2[4]; gm_t79rbdv4rafe5 f2; uint8_t f3; uint8_t pe[7]; } gm_tcfiotghd1s5k;
_Static_assert(sizeof(gm_tcmkompbl09il) == 24 && _Alignof(gm_tcmkompbl09il) == 8 && offsetof(gm_tcmkompbl09il, f0) == 0 && offsetof(gm_tcmkompbl09il, f1) == 8, "layout of `(ty: Ty, bytes: string)`");
_Static_assert(sizeof(gm_tcmtrmhj7ak4u) == 12 && _Alignof(gm_tcmtrmhj7ak4u) == 4 && offsetof(gm_tcmtrmhj7ak4u, f0) == 0 && offsetof(gm_tcmtrmhj7ak4u, f1) == 4 && offsetof(gm_tcmtrmhj7ak4u, f2) == 8, "layout of `(op: LenOp, lhs: Ty, rhs: Ty)`");
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t p2[4]; gm_tnpifv20irnph f2; uint8_t f3; uint8_t pe[7]; } gm_tddtjfggdat0d;
_Static_assert(sizeof(gm_tdgfduqjheqdm) == 64 && _Alignof(gm_tdgfduqjheqdm) == 8 && offsetof(gm_tdgfduqjheqdm, f0) == 0 && offsetof(gm_tdgfduqjheqdm, f1) == 8 && offsetof(gm_tdgfduqjheqdm, f2) == 24 && offsetof(gm_tdgfduqjheqdm, f3) == 40 && offsetof(gm_tdgfduqjheqdm, f4) == 48 && offsetof(gm_tdgfduqjheqdm, f5) == 56, "layout of `FnDef`");
typedef struct { gm_tdgfduqjheqdm f0; uint8_t f1; uint8_t pe[7]; } gm_tdn4erebgflsv;
_Static_assert(sizeof(gm_tep2drsl9408b) == 24 && _Alignof(gm_tep2drsl9408b) == 8 && offsetof(gm_tep2drsl9408b, f0) == 0 && offsetof(gm_tep2drsl9408b, f1) == 16, "layout of `ValTy`");
typedef struct { gm_t08olq97rscn3 f0; gm_th2lqbo77pvsc f1; } gm_tet9guhev5r4u;
typedef struct { gm_tiilmfd16v3fc a[1]; } gm_tfe2ufaqq8re9;
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_tgglga8ro3isc) == 8 && _Alignof(gm_tgglga8ro3isc) == 4 && offsetof(gm_tgglga8ro3isc, f0) == 0 && offsetof(gm_tgglga8ro3isc, f1) == 4, "layout of `(err: Ty, ok: Ty)`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
typedef struct { gm_tbngattl1fo51 f0; uint8_t f1; uint8_t pe[3]; } gm_thetuhtouve7f;
_Static_assert(sizeof(gm_tibupt4dmcqai) == 24 && _Alignof(gm_tibupt4dmcqai) == 8 && offsetof(gm_tibupt4dmcqai, f0) == 0 && offsetof(gm_tibupt4dmcqai, f1) == 16, "layout of `StrLit`");
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
typedef struct { void * f0; gm_t08olq97rscn3 f1; } gm_tioie2ju3q4ul;
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; gm_t88rcucg1b7uk f2; uint64_t f3; void * f4; gm_tkn42u7na3t81 f5; uint64_t f6; void * f7; gm_t9qkh0rsooc8d f8; uint8_t p9[7]; void * f9; } gm_tjcptv9sagfq8;
typedef struct { gm_t08olq97rscn3 f0; gm_th2lqbo77pvsc f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_tkacitepg5fk0;
_Static_assert(sizeof(gm_tkn42u7na3t81) == 8 && _Alignof(gm_tkn42u7na3t81) == 4 && offsetof(gm_tkn42u7na3t81, f0) == 0 && offsetof(gm_tkn42u7na3t81, f1) == 4, "layout of `?Ty`");
_Static_assert(sizeof(gm_tlljg61a2ctgq) == 32 && _Alignof(gm_tlljg61a2ctgq) == 8 && offsetof(gm_tlljg61a2ctgq, f0) == 0 && offsetof(gm_tlljg61a2ctgq, f1) == 8 && offsetof(gm_tlljg61a2ctgq, f2) == 24, "layout of `(callee: ^Expr, args: []Arg, trailing_comma: bool)`");
_Static_assert(sizeof(gm_tnnjeerolos0a) == 8 && _Alignof(gm_tnnjeerolos0a) == 4 && offsetof(gm_tnnjeerolos0a, f0) == 0 && offsetof(gm_tnnjeerolos0a, f1) == 4, "layout of `ArrayKind`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
_Static_assert(sizeof(gm_toceffb697p0u) == 16 && _Alignof(gm_toceffb697p0u) == 4 && offsetof(gm_toceffb697p0u, f0) == 0 && offsetof(gm_toceffb697p0u, f1) == 12, "layout of `?Span`");
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t p2[7]; gm_tf5srh4mp4b3i f2; gm_tkn42u7na3t81 f3; } gm_tpautr4br7h3l;
typedef struct { gm_trqfvmjtt4sij f0; gm_trqfvmjtt4sij f1; gm_t4c7ipjj9s3fp f2; gm_trqfvmjtt4sij f3; gm_trqfvmjtt4sij f4; gm_trqfvmjtt4sij f5; gm_trqfvmjtt4sij f6; } gm_tpcj7kul6hlib;
_Static_assert(sizeof(gm_tpe1hv3fekn05) == 24 && _Alignof(gm_tpe1hv3fekn05) == 8 && offsetof(gm_tpe1hv3fekn05, f0) == 0 && offsetof(gm_tpe1hv3fekn05, f1) == 16, "layout of `(attrs: []Attribute, inner: ^Expr)`");
typedef struct { uint64_t f0; gm_b16_8 f1; gm_t88rcucg1b7uk f2; uint8_t p3[4]; uint64_t f3; void * f4; uint8_t f5; uint8_t p6[7]; gm_th2lqbo77pvsc f6; gm_tf5srh4mp4b3i f7; void * f8; } gm_tq7qjpkmqmc5s;
typedef struct { gm_tiilmfd16v3fc f0; uint8_t f1; uint8_t pe[7]; } gm_tqu3m2l3n8mnc;
typedef struct { gm_t3jsf43p1pths f0; uint8_t f1; uint8_t pe[3]; } gm_trcagkhgm1qdu;
typedef struct { gm_ts6oqppjh0ba3 f0; uint8_t p1[6]; void * f1; void * f2; } gm_trl9d7m1k5lbd;
_Static_assert(sizeof(gm_trlabqsjjp0ds) == 16 && _Alignof(gm_trlabqsjjp0ds) == 4 && offsetof(gm_trlabqsjjp0ds, f0) == 0 && offsetof(gm_trlabqsjjp0ds, f1) == 12, "layout of `(Span, Ty)`");
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_ts6oqppjh0ba3) == 2 && _Alignof(gm_ts6oqppjh0ba3) == 1 && offsetof(gm_ts6oqppjh0ba3, f0) == 0 && offsetof(gm_ts6oqppjh0ba3, f1) == 1, "layout of `BinOp`");
_Static_assert(sizeof(gm_tupk62cr6lbdu) == 24 && _Alignof(gm_tupk62cr6lbdu) == 8 && offsetof(gm_tupk62cr6lbdu, f0) == 0 && offsetof(gm_tupk62cr6lbdu, f1) == 8 && offsetof(gm_tupk62cr6lbdu, f2) == 12, "layout of `(base: ^Expr, index: u32, index_span: Span)`");
_Static_assert(sizeof(gm_tv0k2vg3054a6) == 48 && _Alignof(gm_tv0k2vg3054a6) == 8 && offsetof(gm_tv0k2vg3054a6, f0) == 0 && offsetof(gm_tv0k2vg3054a6, f1) == 16 && offsetof(gm_tv0k2vg3054a6, f2) == 24 && offsetof(gm_tv0k2vg3054a6, f3) == 40, "layout of `OpInfo`");
_Static_assert(sizeof(gm_tv2uf4leil5hq) == 64 && _Alignof(gm_tv2uf4leil5hq) == 8 && offsetof(gm_tv2uf4leil5hq, f0) == 0 && offsetof(gm_tv2uf4leil5hq, f1) == 8 && offsetof(gm_tv2uf4leil5hq, f2) == 24 && offsetof(gm_tv2uf4leil5hq, f3) == 32 && offsetof(gm_tv2uf4leil5hq, f4) == 40 && offsetof(gm_tv2uf4leil5hq, f5) == 48, "layout of `Binding`");
_Static_assert(sizeof(gm_t1s75i08qtptd) == 40 && _Alignof(gm_t1s75i08qtptd) == 8 && offsetof(gm_t1s75i08qtptd, f0) == 0 && offsetof(gm_t1s75i08qtptd, f1) == 32, "layout of `?Ident`");
_Static_assert(sizeof(gm_t428ue2u3hf5g) == 32 && _Alignof(gm_t428ue2u3hf5g) == 8 && offsetof(gm_t428ue2u3hf5g, f0) == 0 && offsetof(gm_t428ue2u3hf5g, f1) == 8, "layout of `ConstKey`");
_Static_assert(sizeof(gm_t4gut2c9fj1kt) == 184 && _Alignof(gm_t4gut2c9fj1kt) == 8 && offsetof(gm_t4gut2c9fj1kt, f0) == 0 && offsetof(gm_t4gut2c9fj1kt, f1) == 8 && offsetof(gm_t4gut2c9fj1kt, f2) == 24 && offsetof(gm_t4gut2c9fj1kt, f3) == 40 && offsetof(gm_t4gut2c9fj1kt, f4) == 80 && offsetof(gm_t4gut2c9fj1kt, f5) == 120 && offsetof(gm_t4gut2c9fj1kt, f6) == 160, "layout of `Diagnostic`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tjcj4j2gbh6c8 v0; uint16_t v1; gm_tbngattl1fo51 v2; gm_tbngattl1fo51 v3; gm_tbngattl1fo51 v4; gm_t8nckeqfenl8a v5; gm_t6j6smj7qoctu v6; gm_t88rcucg1b7uk v7; gm_tgglga8ro3isc v8; gm_tddtjfggdat0d v9; gm_tddtjfggdat0d v10; gm_tiilmfd16v3fc v11; gm_t88rcucg1b7uk v12; uint32_t v13; gm_tgglga8ro3isc v14; gm_t428ue2u3hf5g v15; gm_tcmtrmhj7ak4u v16; uint32_t v17; gm_tnpifv20irnph v18; uint32_t v19; } f1; } gm_t53at80pfdus3;
_Static_assert(sizeof(gm_t7bjbmukv26e7) == 64 && _Alignof(gm_t7bjbmukv26e7) == 8 && offsetof(gm_t7bjbmukv26e7, f0) == 0 && offsetof(gm_t7bjbmukv26e7, f1) == 8 && offsetof(gm_t7bjbmukv26e7, f2) == 24 && offsetof(gm_t7bjbmukv26e7, f3) == 48, "layout of `WhenItems`");
_Static_assert(sizeof(gm_t84dv0cg48dut) == 20 && _Alignof(gm_t84dv0cg48dut) == 4 && offsetof(gm_t84dv0cg48dut, f0) == 0 && offsetof(gm_t84dv0cg48dut, f1) == 16, "layout of `?(win64: bool, span: Span)`");
_Static_assert(sizeof(gm_t89h15e2c5o3p) == 24 && _Alignof(gm_t89h15e2c5o3p) == 8 && offsetof(gm_t89h15e2c5o3p, f0) == 0 && offsetof(gm_t89h15e2c5o3p, f1) == 16, "layout of `?AliasTarget`");
_Static_assert(sizeof(gm_t8nckeqfenl8a) == 24 && _Alignof(gm_t8nckeqfenl8a) == 8 && offsetof(gm_t8nckeqfenl8a, f0) == 0 && offsetof(gm_t8nckeqfenl8a, f1) == 8 && offsetof(gm_t8nckeqfenl8a, f2) == 12, "layout of `(len: u64, elem: Ty, kind: ArrayKind)`");
_Static_assert(sizeof(gm_tbci5ospk5nnn) == 48 && _Alignof(gm_tbci5ospk5nnn) == 8 && offsetof(gm_tbci5ospk5nnn, f0) == 0 && offsetof(gm_tbci5ospk5nnn, f1) == 8, "layout of `Int`");
_Static_assert(sizeof(gm_tcfiotghd1s5k) == 88 && _Alignof(gm_tcfiotghd1s5k) == 8 && offsetof(gm_tcfiotghd1s5k, f0) == 0 && offsetof(gm_tcfiotghd1s5k, f1) == 24 && offsetof(gm_tcfiotghd1s5k, f2) == 40 && offsetof(gm_tcfiotghd1s5k, f3) == 80, "layout of `TestDecl`");
typedef struct { gm_t1s75i08qtptd f0; void * f1; } gm_tchr52g0ema53;
typedef struct { gm_b16_8 f0; gm_tkn42u7na3t81 f1; gm_tbci5ospk5nnn f2; } gm_tckdobunaulee;
typedef struct { void * f0; gm_tpcj7kul6hlib f1; gm_trqfvmjtt4sij f2; gm_trqfvmjtt4sij f3; gm_trqfvmjtt4sij f4; gm_trqfvmjtt4sij f5; gm_trqfvmjtt4sij f6; gm_trqfvmjtt4sij f7; gm_trqfvmjtt4sij f8; uint8_t f9; uint8_t p10[7]; gm_trqfvmjtt4sij f10; gm_trqfvmjtt4sij f11; uint8_t f12; uint8_t p13[7]; gm_trqfvmjtt4sij f13; gm_trqfvmjtt4sij f14; gm_trqfvmjtt4sij f15; gm_trqfvmjtt4sij f16; gm_trqfvmjtt4sij f17; gm_trqfvmjtt4sij f18; gm_trqfvmjtt4sij f19; gm_t9805qtonbsag f20; gm_trqfvmjtt4sij f21; gm_trqfvmjtt4sij f22; gm_trqfvmjtt4sij f23; gm_trqfvmjtt4sij f24; gm_trqfvmjtt4sij f25; gm_trqfvmjtt4sij f26; gm_t4c7ipjj9s3fp f27; gm_t4c7ipjj9s3fp f28; gm_trqfvmjtt4sij f29; uint32_t f30; uint8_t f31; uint8_t p32[3]; gm_trqfvmjtt4sij f32; gm_trqfvmjtt4sij f33; gm_trqfvmjtt4sij f34; gm_trqfvmjtt4sij f35; gm_trqfvmjtt4sij f36; gm_trqfvmjtt4sij f37; gm_trqfvmjtt4sij f38; gm_trqfvmjtt4sij f39; gm_trqfvmjtt4sij f40; gm_trqfvmjtt4sij f41; uint32_t f42; uint8_t p43[4]; gm_trqfvmjtt4sij f43; uint32_t f44; uint8_t p45[4]; gm_trqfvmjtt4sij f45; uint8_t f46; uint8_t p47[7]; gm_trqfvmjtt4sij f47; gm_trqfvmjtt4sij f48; uint32_t f49; gm_t9805qtonbsag f50; uint8_t p51[4]; gm_trqfvmjtt4sij f51; gm_t4c7ipjj9s3fp f52; gm_t4c7ipjj9s3fp f53; gm_trqfvmjtt4sij f54; uint32_t f55; uint8_t p56[4]; void * f56; void * f57; void * f58; gm_trqfvmjtt4sij f59; gm_trqfvmjtt4sij f60; gm_trqfvmjtt4sij f61; uint64_t f62; gm_t4c7ipjj9s3fp f63; gm_trqfvmjtt4sij f64; gm_trqfvmjtt4sij f65; gm_trqfvmjtt4sij f66; gm_trqfvmjtt4sij f67; uint8_t f68; uint8_t p69[7]; gm_trqfvmjtt4sij f69; gm_trqfvmjtt4sij f70; gm_t4c7ipjj9s3fp f71; gm_t4c7ipjj9s3fp f72; gm_t4c7ipjj9s3fp f73; } gm_tcrr6nmqqhjbj;
typedef struct { gm_tkacitepg5fk0 f0; uint8_t f1; uint8_t pe[7]; } gm_tcus0chk5r9uq;
_Static_assert(sizeof(gm_tddtjfggdat0d) == 48 && _Alignof(gm_tddtjfggdat0d) == 8 && offsetof(gm_tddtjfggdat0d, f0) == 0 && offsetof(gm_tddtjfggdat0d, f1) == 16 && offsetof(gm_tddtjfggdat0d, f2) == 24 && offsetof(gm_tddtjfggdat0d, f3) == 40, "layout of `FnTy`");
_Static_assert(sizeof(gm_tdn4erebgflsv) == 72 && _Alignof(gm_tdn4erebgflsv) == 8 && offsetof(gm_tdn4erebgflsv, f0) == 0 && offsetof(gm_tdn4erebgflsv, f1) == 64, "layout of `?FnDef`");
_Static_assert(sizeof(gm_tet9guhev5r4u) == 56 && _Alignof(gm_tet9guhev5r4u) == 8 && offsetof(gm_tet9guhev5r4u, f0) == 0 && offsetof(gm_tet9guhev5r4u, f1) == 32, "layout of `IntrinsicExpr`");
_Static_assert(sizeof(gm_tfe2ufaqq8re9) == 24 && _Alignof(gm_tfe2ufaqq8re9) == 8 && offsetof(gm_tfe2ufaqq8re9, a) == 0, "layout of `[1]EffectRef`");
_Static_assert(sizeof(gm_thetuhtouve7f) == 12 && _Alignof(gm_thetuhtouve7f) == 4 && offsetof(gm_thetuhtouve7f, f0) == 0 && offsetof(gm_thetuhtouve7f, f1) == 8, "layout of `?(inclusive: bool, elem: Ty)`");
typedef struct { void * f0; gm_t1s75i08qtptd f1; void * f2; } gm_tioi3jh9v22g5;
_Static_assert(sizeof(gm_tioie2ju3q4ul) == 40 && _Alignof(gm_tioie2ju3q4ul) == 8 && offsetof(gm_tioie2ju3q4ul, f0) == 0 && offsetof(gm_tioie2ju3q4ul, f1) == 8, "layout of `(base: ^Expr, name: Ident)`");
_Static_assert(sizeof(gm_tjcptv9sagfq8) == 88 && _Alignof(gm_tjcptv9sagfq8) == 8 && offsetof(gm_tjcptv9sagfq8, f0) == 0 && offsetof(gm_tjcptv9sagfq8, f1) == 16 && offsetof(gm_tjcptv9sagfq8, f2) == 28 && offsetof(gm_tjcptv9sagfq8, f3) == 32 && offsetof(gm_tjcptv9sagfq8, f4) == 40 && offsetof(gm_tjcptv9sagfq8, f5) == 48 && offsetof(gm_tjcptv9sagfq8, f6) == 56 && offsetof(gm_tjcptv9sagfq8, f7) == 64 && offsetof(gm_tjcptv9sagfq8, f8) == 72 && offsetof(gm_tjcptv9sagfq8, f9) == 80, "layout of `ConstDef`");
_Static_assert(sizeof(gm_tkacitepg5fk0) == 72 && _Alignof(gm_tkacitepg5fk0) == 8 && offsetof(gm_tkacitepg5fk0, f0) == 0 && offsetof(gm_tkacitepg5fk0, f1) == 32 && offsetof(gm_tkacitepg5fk0, f2) == 56, "layout of `Attribute`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tv2uf4leil5hq v0; gm_t7bjbmukv26e7 v1; void * v2; gm_tcfiotghd1s5k v3; void * v4; } f1; } gm_tkoope2qgegd9;
typedef struct { gm_tbci5ospk5nnn f0; gm_tbci5ospk5nnn f1; } gm_tngtuiuskn938;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t88rcucg1b7uk v0; gm_t428ue2u3hf5g v1; } f1; } gm_toa2vhe3l4hfr;
_Static_assert(sizeof(gm_tpautr4br7h3l) == 48 && _Alignof(gm_tpautr4br7h3l) == 8 && offsetof(gm_tpautr4br7h3l, f0) == 0 && offsetof(gm_tpautr4br7h3l, f1) == 16 && offsetof(gm_tpautr4br7h3l, f2) == 24 && offsetof(gm_tpautr4br7h3l, f3) == 40, "layout of `StructDef`");
_Static_assert(sizeof(gm_tpcj7kul6hlib) == 296 && _Alignof(gm_tpcj7kul6hlib) == 8 && offsetof(gm_tpcj7kul6hlib, f0) == 0 && offsetof(gm_tpcj7kul6hlib, f1) == 40 && offsetof(gm_tpcj7kul6hlib, f2) == 80 && offsetof(gm_tpcj7kul6hlib, f3) == 136 && offsetof(gm_tpcj7kul6hlib, f4) == 176 && offsetof(gm_tpcj7kul6hlib, f5) == 216 && offsetof(gm_tpcj7kul6hlib, f6) == 256, "layout of `Types`");
_Static_assert(sizeof(gm_tq7qjpkmqmc5s) == 104 && _Alignof(gm_tq7qjpkmqmc5s) == 8 && offsetof(gm_tq7qjpkmqmc5s, f0) == 0 && offsetof(gm_tq7qjpkmqmc5s, f1) == 8 && offsetof(gm_tq7qjpkmqmc5s, f2) == 24 && offsetof(gm_tq7qjpkmqmc5s, f3) == 32 && offsetof(gm_tq7qjpkmqmc5s, f4) == 40 && offsetof(gm_tq7qjpkmqmc5s, f5) == 48 && offsetof(gm_tq7qjpkmqmc5s, f6) == 56 && offsetof(gm_tq7qjpkmqmc5s, f7) == 80 && offsetof(gm_tq7qjpkmqmc5s, f8) == 96, "layout of `GlobalInfo`");
_Static_assert(sizeof(gm_tqu3m2l3n8mnc) == 32 && _Alignof(gm_tqu3m2l3n8mnc) == 8 && offsetof(gm_tqu3m2l3n8mnc, f0) == 0 && offsetof(gm_tqu3m2l3n8mnc, f1) == 24, "layout of `?FnInstance`");
_Static_assert(sizeof(gm_trcagkhgm1qdu) == 12 && _Alignof(gm_trcagkhgm1qdu) == 4 && offsetof(gm_trcagkhgm1qdu, f0) == 0 && offsetof(gm_trcagkhgm1qdu, f1) == 8, "layout of `?(ty: Ty, vis: Visibility)`");
_Static_assert(sizeof(gm_trl9d7m1k5lbd) == 24 && _Alignof(gm_trl9d7m1k5lbd) == 8 && offsetof(gm_trl9d7m1k5lbd, f0) == 0 && offsetof(gm_trl9d7m1k5lbd, f1) == 8 && offsetof(gm_trl9d7m1k5lbd, f2) == 16, "layout of `(op: BinOp, lhs: ^Expr, rhs: ^Expr)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { void * v0; void * v1; gm_tibupt4dmcqai v2; uint8_t v3; gm_t08olq97rscn3 v4; gm_t08olq97rscn3 v5; void * v6; gm_b16_8 v7; void * v8; gm_t8ghgl2jj0qha v9; gm_trl9d7m1k5lbd v10; gm_tioi3jh9v22g5 v11; void * v12; void * v13; gm_t3mf58gv9kf3n v14; void * v15; gm_tlljg61a2ctgq v16; gm_t3mf58gv9kf3n v17; gm_tao63pk3r18v5 v18; gm_tioie2ju3q4ul v19; gm_tupk62cr6lbdu v20; void * v21; gm_tgtub0v94d5u5 v22; gm_tgtub0v94d5u5 v23; gm_tgtub0v94d5u5 v24; gm_t3mf58gv9kf3n v25; gm_t3mf58gv9kf3n v26; void * v27; void * v28; void * v29; void * v30; void * v31; gm_tpe1hv3fekn05 v32; void * v33; void * v34; gm_tibupt4dmcqai v35; void * v36; void * v37; void * v38; void * v39; void * v40; gm_t79rbdv4rafe5 v41; void * v42; gm_tchr52g0ema53 v43; gm_t1s75i08qtptd v44; gm_t3mf58gv9kf3n v45; gm_tet9guhev5r4u v46; } f1; } gm_t2gtbh39ptekd;
_Static_assert(sizeof(gm_t53at80pfdus3) == 56 && _Alignof(gm_t53at80pfdus3) == 8 && offsetof(gm_t53at80pfdus3, f0) == 0 && offsetof(gm_t53at80pfdus3, f1) == 8, "layout of `TyKind`");
_Static_assert(sizeof(gm_tchr52g0ema53) == 48 && _Alignof(gm_tchr52g0ema53) == 8 && offsetof(gm_tchr52g0ema53, f0) == 0 && offsetof(gm_tchr52g0ema53, f1) == 40, "layout of `BreakExpr`");
_Static_assert(sizeof(gm_tckdobunaulee) == 72 && _Alignof(gm_tckdobunaulee) == 8 && offsetof(gm_tckdobunaulee, f0) == 0 && offsetof(gm_tckdobunaulee, f1) == 16 && offsetof(gm_tckdobunaulee, f2) == 24, "layout of `VariantDef`");
_Static_assert(sizeof(gm_tcrr6nmqqhjbj) == 2792 && _Alignof(gm_tcrr6nmqqhjbj) == 8 && offsetof(gm_tcrr6nmqqhjbj, f0) == 0 && offsetof(gm_tcrr6nmqqhjbj, f1) == 8 && offsetof(gm_tcrr6nmqqhjbj, f2) == 304 && offsetof(gm_tcrr6nmqqhjbj, f3) == 344 && offsetof(gm_tcrr6nmqqhjbj, f4) == 384 && offsetof(gm_tcrr6nmqqhjbj, f5) == 424 && offsetof(gm_tcrr6nmqqhjbj, f6) == 464 && offsetof(gm_tcrr6nmqqhjbj, f7) == 504 && offsetof(gm_tcrr6nmqqhjbj, f8) == 544 && offsetof(gm_tcrr6nmqqhjbj, f9) == 584 && offsetof(gm_tcrr6nmqqhjbj, f10) == 592 && offsetof(gm_tcrr6nmqqhjbj, f11) == 632 && offsetof(gm_tcrr6nmqqhjbj, f12) == 672 && offsetof(gm_tcrr6nmqqhjbj, f13) == 680 && offsetof(gm_tcrr6nmqqhjbj, f14) == 720 && offsetof(gm_tcrr6nmqqhjbj, f15) == 760 && offsetof(gm_tcrr6nmqqhjbj, f16) == 800 && offsetof(gm_tcrr6nmqqhjbj, f17) == 840 && offsetof(gm_tcrr6nmqqhjbj, f18) == 880 && offsetof(gm_tcrr6nmqqhjbj, f19) == 920 && offsetof(gm_tcrr6nmqqhjbj, f20) == 960 && offsetof(gm_tcrr6nmqqhjbj, f21) == 968 && offsetof(gm_tcrr6nmqqhjbj, f22) == 1008 && offsetof(gm_tcrr6nmqqhjbj, f23) == 1048 && offsetof(gm_tcrr6nmqqhjbj, f24) == 1088 && offsetof(gm_tcrr6nmqqhjbj, f25) == 1128 && offsetof(gm_tcrr6nmqqhjbj, f26) == 1168 && offsetof(gm_tcrr6nmqqhjbj, f27) == 1208 && offsetof(gm_tcrr6nmqqhjbj, f28) == 1264 && offsetof(gm_tcrr6nmqqhjbj, f29) == 1320 && offsetof(gm_tcrr6nmqqhjbj, f30) == 1360 && offsetof(gm_tcrr6nmqqhjbj, f31) == 1364 && offsetof(gm_tcrr6nmqqhjbj, f32) == 1368 && offsetof(gm_tcrr6nmqqhjbj, f33) == 1408 && offsetof(gm_tcrr6nmqqhjbj, f34) == 1448 && offsetof(gm_tcrr6nmqqhjbj, f35) == 1488 && offsetof(gm_tcrr6nmqqhjbj, f36) == 1528 && offsetof(gm_tcrr6nmqqhjbj, f37) == 1568 && offsetof(gm_tcrr6nmqqhjbj, f38) == 1608 && offsetof(gm_tcrr6nmqqhjbj, f39) == 1648 && offsetof(gm_tcrr6nmqqhjbj, f40) == 1688 && offsetof(gm_tcrr6nmqqhjbj, f41) == 1728 && offsetof(gm_tcrr6nmqqhjbj, f42) == 1768 && offsetof(gm_tcrr6nmqqhjbj, f43) == 1776 && offsetof(gm_tcrr6nmqqhjbj, f44) == 1816 && offsetof(gm_tcrr6nmqqhjbj, f45) == 1824 && offsetof(gm_tcrr6nmqqhjbj, f46) == 1864 && offsetof(gm_tcrr6nmqqhjbj, f47) == 1872 && offsetof(gm_tcrr6nmqqhjbj, f48) == 1912 && offsetof(gm_tcrr6nmqqhjbj, f49) == 1952 && offsetof(gm_tcrr6nmqqhjbj, f50) == 1956 && offsetof(gm_tcrr6nmqqhjbj, f51) == 1968 && offsetof(gm_tcrr6nmqqhjbj, f52) == 2008 && offsetof(gm_tcrr6nmqqhjbj, f53) == 2064 && offsetof(gm_tcrr6nmqqhjbj, f54) == 2120 && offsetof(gm_tcrr6nmqqhjbj, f55) == 2160 && offsetof(gm_tcrr6nmqqhjbj, f56) == 2168 && offsetof(gm_tcrr6nmqqhjbj, f57) == 2176 && offsetof(gm_tcrr6nmqqhjbj, f58) == 2184 && offsetof(gm_tcrr6nmqqhjbj, f59) == 2192 && offsetof(gm_tcrr6nmqqhjbj, f60) == 2232 && offsetof(gm_tcrr6nmqqhjbj, f61) == 2272 && offsetof(gm_tcrr6nmqqhjbj, f62) == 2312 && offsetof(gm_tcrr6nmqqhjbj, f63) == 2320 && offsetof(gm_tcrr6nmqqhjbj, f64) == 2376 && offsetof(gm_tcrr6nmqqhjbj, f65) == 2416 && offsetof(gm_tcrr6nmqqhjbj, f66) == 2456 && offsetof(gm_tcrr6nmqqhjbj, f67) == 2496 && offsetof(gm_tcrr6nmqqhjbj, f68) == 2536 && offsetof(gm_tcrr6nmqqhjbj, f69) == 2544 && offsetof(gm_tcrr6nmqqhjbj, f70) == 2584 && offsetof(gm_tcrr6nmqqhjbj, f71) == 2624 && offsetof(gm_tcrr6nmqqhjbj, f72) == 2680 && offsetof(gm_tcrr6nmqqhjbj, f73) == 2736, "layout of `Sema`");
_Static_assert(sizeof(gm_tcus0chk5r9uq) == 80 && _Alignof(gm_tcus0chk5r9uq) == 8 && offsetof(gm_tcus0chk5r9uq, f0) == 0 && offsetof(gm_tcus0chk5r9uq, f1) == 72, "layout of `?Attribute`");
_Static_assert(sizeof(gm_tioi3jh9v22g5) == 56 && _Alignof(gm_tioi3jh9v22g5) == 8 && offsetof(gm_tioi3jh9v22g5, f0) == 0 && offsetof(gm_tioi3jh9v22g5, f1) == 8 && offsetof(gm_tioi3jh9v22g5, f2) == 48, "layout of `ElseExpr`");
_Static_assert(sizeof(gm_tkoope2qgegd9) == 96 && _Alignof(gm_tkoope2qgegd9) == 8 && offsetof(gm_tkoope2qgegd9, f0) == 0 && offsetof(gm_tkoope2qgegd9, f1) == 8, "layout of `ItemKind`");
_Static_assert(sizeof(gm_tngtuiuskn938) == 96 && _Alignof(gm_tngtuiuskn938) == 8 && offsetof(gm_tngtuiuskn938, f0) == 0 && offsetof(gm_tngtuiuskn938, f1) == 48, "layout of `Rat`");
_Static_assert(sizeof(gm_toa2vhe3l4hfr) == 40 && _Alignof(gm_toa2vhe3l4hfr) == 8 && offsetof(gm_toa2vhe3l4hfr, f0) == 0 && offsetof(gm_toa2vhe3l4hfr, f1) == 8, "layout of `GenericArg`");
typedef struct { gm_tngtuiuskn938 f0; gm_tngtuiuskn938 f1; } gm_tu15p0hi2qlv6;
typedef struct { uint32_t f0; gm_tl874t2atnlh3 f1; gm_th2lqbo77pvsc f2; gm_b16_8 f3; gm_t9qkh0rsooc8d f4; uint8_t p5[7]; gm_tkoope2qgegd9 f5; } gm_tvpnktv5fo8nl;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tbci5ospk5nnn v0; gm_tngtuiuskn938 v1; uint8_t v2; gm_tu15p0hi2qlv6 v3; } f1; } gm_t0ialelq3ql1k;
_Static_assert(sizeof(gm_t2gtbh39ptekd) == 64 && _Alignof(gm_t2gtbh39ptekd) == 8 && offsetof(gm_t2gtbh39ptekd, f0) == 0 && offsetof(gm_t2gtbh39ptekd, f1) == 8, "layout of `ExprKind`");
typedef struct { gm_t08olq97rscn3 f0; uint64_t f1; gm_tvpnktv5fo8nl f2; gm_t8c5h7u09n10a f3; } gm_t52j636u3vgk3;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_t2gtbh39ptekd f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_tc3oht4ksth9d;
_Static_assert(sizeof(gm_tu15p0hi2qlv6) == 192 && _Alignof(gm_tu15p0hi2qlv6) == 8 && offsetof(gm_tu15p0hi2qlv6, f0) == 0 && offsetof(gm_tu15p0hi2qlv6, f1) == 96, "layout of `(re: Rat, im: Rat)`");
_Static_assert(sizeof(gm_tvpnktv5fo8nl) == 160 && _Alignof(gm_tvpnktv5fo8nl) == 8 && offsetof(gm_tvpnktv5fo8nl, f0) == 0 && offsetof(gm_tvpnktv5fo8nl, f1) == 4 && offsetof(gm_tvpnktv5fo8nl, f2) == 16 && offsetof(gm_tvpnktv5fo8nl, f3) == 40 && offsetof(gm_tvpnktv5fo8nl, f4) == 56 && offsetof(gm_tvpnktv5fo8nl, f5) == 64, "layout of `Item`");
_Static_assert(sizeof(gm_t0ialelq3ql1k) == 200 && _Alignof(gm_t0ialelq3ql1k) == 8 && offsetof(gm_t0ialelq3ql1k, f0) == 0 && offsetof(gm_t0ialelq3ql1k, f1) == 8, "layout of `Untyped`");
_Static_assert(sizeof(gm_t52j636u3vgk3) == 216 && _Alignof(gm_t52j636u3vgk3) == 8 && offsetof(gm_t52j636u3vgk3, f0) == 0 && offsetof(gm_t52j636u3vgk3, f1) == 32 && offsetof(gm_t52j636u3vgk3, f2) == 40 && offsetof(gm_t52j636u3vgk3, f3) == 200, "layout of `Decl`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t0ialelq3ql1k v0; } f1; } gm_t5vbkdq81qfs7;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t88rcucg1b7uk v0; uint32_t v1; uint32_t v2; gm_tiilmfd16v3fc v3; gm_tnpifv20irnph v4; uint32_t v5; gm_tcc1rq4n1avb2 v6; gm_t88rcucg1b7uk v7; gm_t0ialelq3ql1k v8; uint64_t v9; gm_t88rcucg1b7uk v10; } f1; } gm_t6qooom8qa2do;
_Static_assert(sizeof(gm_tc3oht4ksth9d) == 88 && _Alignof(gm_tc3oht4ksth9d) == 8 && offsetof(gm_tc3oht4ksth9d, f0) == 0 && offsetof(gm_tc3oht4ksth9d, f1) == 8 && offsetof(gm_tc3oht4ksth9d, f2) == 72, "layout of `Expr`");
_Static_assert(sizeof(gm_t5vbkdq81qfs7) == 208 && _Alignof(gm_t5vbkdq81qfs7) == 8 && offsetof(gm_t5vbkdq81qfs7, f0) == 0 && offsetof(gm_t5vbkdq81qfs7, f1) == 8, "layout of `UntypedResult`");
_Static_assert(sizeof(gm_t6qooom8qa2do) == 208 && _Alignof(gm_t6qooom8qa2do) == 8 && offsetof(gm_t6qooom8qa2do, f0) == 0 && offsetof(gm_t6qooom8qa2do, f1) == 8, "layout of `DeclValue`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t6qooom8qa2do v0; } f1; } gm_tagrkbqh054h7;
typedef struct { gm_t5vbkdq81qfs7 f0; uint8_t f1; uint8_t pe[7]; } gm_tkv1vap90vht2;
_Static_assert(sizeof(gm_tagrkbqh054h7) == 216 && _Alignof(gm_tagrkbqh054h7) == 8 && offsetof(gm_tagrkbqh054h7, f0) == 0 && offsetof(gm_tagrkbqh054h7, f1) == 8, "layout of `DeclState`");
_Static_assert(sizeof(gm_tkv1vap90vht2) == 216 && _Alignof(gm_tkv1vap90vht2) == 8 && offsetof(gm_tkv1vap90vht2, f0) == 0 && offsetof(gm_tkv1vap90vht2, f1) == 208, "layout of `?UntypedResult`");
struct gm_fr_add_const_8u8b1ja {
    gm_frame h;
    gm_tjcptv9sagfq8 l1; /* c */
    uint32_t ret;
};
struct gm_fr_add_label_50fj7vv {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_adt_field_tys_oggq5ji {
    gm_frame h;
    gm_tpautr4br7h3l l4; /* def */
    gm_tckdobunaulee l9; /* v */
    gm_b0_1 ret;
};
struct gm_fr_adt_name_h7mjik7 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_aliased_decl_jv6ilmn {
    gm_frame h;
    gm_t89h15e2c5o3p ret;
};
struct gm_fr_check_allow_attr_3321gm7 {
    gm_frame h;
    gm_tc3oht4ksth9d l5; /* a */
    gm_b0_1 ret;
};
struct gm_fr_check_method_candidates_k08rvu6 {
    gm_frame h;
    gm_t4c7ipjj9s3fp l1; /* seen */
    gm_t79rbdv4rafe5 l4; /* c */
    gm_t4gut2c9fj1kt l8; /* d */
    gm_b0_1 ret;
};
struct gm_fr_check_pub_signatures_plkds6r {
    gm_frame h;
    gm_t6qooom8qa2do l6; /* v */
    gm_trqfvmjtt4sij l7; /* tys */
    gm_t5v0c8ia6a5mo l12; /* p */
    gm_tv0k2vg3054a6 l16; /* op */
    gm_t5v0c8ia6a5mo l19; /* p */
    gm_b0_1 ret;
};
struct gm_fr_clear_ufjejrf {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_complex_of_kh32tib {
    gm_frame h;
    gm_tkn42u7na3t81 ret;
};
struct gm_fr_compute_binding_cl3ogob {
    gm_frame h;
    gm_t4gut2c9fj1kt l5; /* e */
    gm_t5vbkdq81qfs7 l17; /* r */
    gm_t0ialelq3ql1k l18; /* v */
    gm_t6qooom8qa2do ret;
};
struct gm_fr_compute_decl_gcf8mq9 {
    gm_frame h;
    gm_t52j636u3vgk3 l2; /* decl */
    gm_tvpnktv5fo8nl l3; /* item */
    gm_tv2uf4leil5hq l6; /* b */
    gm_tcus0chk5r9uq l10; /* hints */
    gm_tcus0chk5r9uq l13; /* align_attr */
    gm_t1s75i08qtptd l14; /* export */
    gm_tkacitepg5fk0 l19; /* attr */
    gm_t5vbkdq81qfs7 l22; /* r */
    gm_tbci5ospk5nnn l23; /* i */
    gm_tkacitepg5fk0 l34; /* a */
    gm_t6qooom8qa2do l35; /* v */
    gm_tkacitepg5fk0 l40; /* h */
    gm_t6qooom8qa2do ret;
};
struct gm_fr_const_id_2mlh0og {
    gm_frame h;
    gm_t9805qtonbsag ret;
};
struct gm_fr_declare_adt_iefkbkk {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_declare_const_or_type_4jaj4k6 {
    gm_frame h;
    gm_t6qooom8qa2do l7; /* v */
    gm_t6qooom8qa2do ret;
};
struct gm_fr_declare_method_candidate_e20bo1i {
    gm_frame h;
    gm_tpautr4br7h3l l7; /* def */
    gm_t0mglksrkfa5e l10; /* f */
    gm_b0_1 ret;
};
struct gm_fr_def_of_m512sbu {
    gm_frame h;
    gm_tdgfduqjheqdm l2; /* d */
    gm_tdgfduqjheqdm ret;
};
struct gm_fr_effect_arg_tys_2a7r8hh {
    gm_frame h;
    gm_toa2vhe3l4hfr l8; /* a */
    gm_b0_1 ret;
};
struct gm_fr_effect_terms_bphj7th {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* out */
    gm_b16_8 ret;
};
struct gm_fr_emit_dph5v1e {
    gm_frame h;
    gm_t4gut2c9fj1kt l1; /* d */
    gm_b0_1 ret;
};
struct gm_fr_empty_slice_1fu8oa9 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_1mrcnd6 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_1scem7m {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_2k4643t {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_5nn7ph0 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_73v4ttb {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_7adh53f {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_7od16il {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_8nf04m8 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_95isii0 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_a6or8fm {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_c3qnk0a {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_cp9r5v5 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_d0lhrvs {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_d3uqusp {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_d914i5u {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_dq2isu2 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_e7jr93v {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_eoluluh {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_f1ngfj0 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_f6g91fe {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_fh7svmj {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_g3tddrb {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_g6fvoq5 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_hii1vte {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_hp0e9gl {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ijtp9lo {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ipqblac {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_iqgue92 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_j0utaj4 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_j6dmic1 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_j98q5u1 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_jpu5akn {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_kgfehvr {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_lh8ov0e {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_lp0ck0u {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_lpero57 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_mbkocse {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_nqsj3l3 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_oqjc4ke {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_osbc35j {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ot5ttb7 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_p6mn4us {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_pu15hdv {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_q870hn3 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_qmcc4fe {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_r81t436 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_s1lbe5s {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_s6l2ctu {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_sc53cu2 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_shtdeku {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_tslvjbf {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_uq9fi1n {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_v0dtga2 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_error_89glhjr {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_field_default_expr_tqfuiff {
    gm_frame h;
    void * ret;
};
struct gm_fr_file_o15fqku {
    gm_frame h;
    void * ret;
};
struct gm_fr_fn_bfunt71 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_hcjjkrm {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_repdalk {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_frame_up_h0ddp1d {
    gm_frame h;
    gm_t9805qtonbsag ret;
};
struct gm_fr_generic_of_1861hm9 {
    gm_frame h;
    uint32_t ret;
};
struct gm_fr_has_param_u9t0efs {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_hash_map_0vd239u {
    gm_frame h;
    gm_t4c7ipjj9s3fp ret;
};
struct gm_fr_hash_map_1so3e0v {
    gm_frame h;
    gm_t4c7ipjj9s3fp ret;
};
struct gm_fr_hash_map_84luf3l {
    gm_frame h;
    gm_t4c7ipjj9s3fp ret;
};
struct gm_fr_hash_map_8lre197 {
    gm_frame h;
    gm_t4c7ipjj9s3fp ret;
};
struct gm_fr_hash_map_gsp2k0o {
    gm_frame h;
    gm_t4c7ipjj9s3fp ret;
};
struct gm_fr_hash_map_iibum07 {
    gm_frame h;
    gm_t4c7ipjj9s3fp ret;
};
struct gm_fr_hash_map_iug9p19 {
    gm_frame h;
    gm_t4c7ipjj9s3fp ret;
};
struct gm_fr_hash_map_ve01i1t {
    gm_frame h;
    gm_t4c7ipjj9s3fp ret;
};
struct gm_fr_hidden_part_is5hgf9 {
    gm_frame h;
    void * l0; /* s */
    gm_t9qkh0rsooc8d l2; /* need */
    gm_trcagkhgm1qdu l3; /* found */
    gm_trcagkhgm1qdu ret;
};
struct gm_fr_in_generic_scope_rvb92ab {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_ambient_m41pl1e {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_error_8k27gd1 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_positive_su660ab {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    uint8_t ret;
};
struct gm_fr_is_structural_alias_qpmv4mu {
    gm_frame h;
    gm_tv2uf4leil5hq l3; /* b */
    uint8_t ret;
};
struct gm_fr_kind_jcnt4hm {
    gm_frame h;
    gm_t53at80pfdus3 ret;
};
struct gm_fr_list_0bg6tcp {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_0bj26n7 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_0npefvp {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_1jabh22 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_1teief7 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_2343nn9 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_406coeg {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_4kh12ib {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_5dhgg4i {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_5pborc0 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_7s4om10 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_8r7pelg {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_9q64p2g {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_atkrn3m {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_au01ire {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_c02589n {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_dql1gah {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_dvbol2p {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_dvp9pla {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_eb5ua5r {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_epvducg {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_fam8ktt {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_go2qg0b {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_h93u1dq {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_i6iqfhd {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_ib5k4gb {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_je8tl46 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_kk91efj {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_l3jkoh2 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_m7es0sh {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_mvhhv5u {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_qs7fohu {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_qv7bq94 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_s83jn2q {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_sajcold {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_t29l5ca {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_tn37ahu {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_tqcq99u {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_ue2t2bm {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_uun92du {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_uvfu1k1 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_v3cpbrb {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_vhqj1o4 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_lookup_59juqoo {
    gm_frame h;
    gm_tf5srh4mp4b3i ret;
};
struct gm_fr_non_constant_part_itk7b0v {
    gm_frame h;
    void * l0; /* s */
    gm_tkn42u7na3t81 l2; /* bad */
    gm_tkn42u7na3t81 ret;
};
struct gm_fr_pop_j4rpvm5 {
    gm_frame h;
    gm_tf5srh4mp4b3i ret;
};
struct gm_fr_prelude_effect_gilrmm5 {
    gm_frame h;
    gm_tqu3m2l3n8mnc ret;
};
struct gm_fr_prelude_instance_arg_kh32dmf {
    gm_frame h;
    gm_tkn42u7na3t81 ret;
};
struct gm_fr_prelude_template_7s9nsp0 {
    gm_frame h;
    gm_t9805qtonbsag ret;
};
struct gm_fr_printer_h7jigd7 {
    gm_frame h;
    gm_tmm67r2c94d5j ret;
};
struct gm_fr_push_field_tys_v4ep4t3 {
    gm_frame h;
    gm_t0mglksrkfa5e l5; /* f */
    gm_b0_1 ret;
};
struct gm_fr_range_of_dmmo768 {
    gm_frame h;
    gm_thetuhtouve7f ret;
};
struct gm_fr_range_template_dbt7218 {
    gm_frame h;
    gm_t9805qtonbsag ret;
};
struct gm_fr_report_cycle_mauubd4 {
    gm_frame h;
    gm_trqfvmjtt4sij l4; /* names */
    gm_t4gut2c9fj1kt l9; /* d */
    gm_b0_1 ret;
};
struct gm_fr_report_eardf07 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_resolve_ambient_6orl0vo {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_resolve_decl_pus042v {
    gm_frame h;
    gm_t6qooom8qa2do l2; /* v */
    gm_t6qooom8qa2do l3; /* v */
    gm_trqfvmjtt4sij l6; /* type_params */
    gm_trqfvmjtt4sij l7; /* value_scope */
    gm_trqfvmjtt4sij l8; /* local_consts */
    gm_trqfvmjtt4sij l9; /* local_fns */
    gm_t6qooom8qa2do l10; /* v */
    gm_t6qooom8qa2do l11; /* r */
    gm_t6qooom8qa2do ret;
};
struct gm_fr_resolve_declarations_vrpg1fl {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_sema_r1nlnnm {
    gm_frame h;
    gm_tcrr6nmqqhjbj l1; /* s */
    gm_tcrr6nmqqhjbj ret;
};
struct gm_fr_set_inst_def_tcpabg0 {
    gm_frame h;
    gm_tdgfduqjheqdm l2; /* d */
    gm_b0_1 ret;
};
struct gm_fr_set_state_3j300fi {
    gm_frame h;
    gm_tagrkbqh054h7 l2; /* v */
    gm_b0_1 ret;
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
struct gm_fr_state_a5bnsjb {
    gm_frame h;
    gm_tagrkbqh054h7 ret;
};
struct gm_fr_template_9o92cvv {
    gm_frame h;
    uint32_t ret;
};
struct gm_fr_to_u128_haka99r {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_t9p38b040grms ret;
};
struct gm_fr_to_u64_m01t1da {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_tf5srh4mp4b3i ret;
};
struct gm_fr_trim_fdrfe6i {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_unsupported_t7lgvp9 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_walk_v858co7 {
    gm_frame h;
    void * l0; /* s */
    gm_closure l2; /* f */
    gm_t53at80pfdus3 l3; /* k */
    gm_b0_1 ret;
};
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b);
inline bool gm_eq6groq37ram(const uint8_t *a, const uint8_t *b);
inline bool gm_eq7mri15hk78(const uint8_t *a, const uint8_t *b);
__attribute__((aligned(4))) uint32_t gm_f_add_const_8u8b1ja(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_add_fix_0gkcr4c(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_tl874t2atnlh3 a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) void gm_f_add_label_50fj7vv(void * a0, gm_cseahvg8v7p0vs a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_adt_field_tys_oggq5ji(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_t9qkh0rsooc8d a2, void * a3);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_adt_name_h7mjik7(void * a0, uint32_t a1);
__attribute__((aligned(4))) void gm_f_aliased_decl_jv6ilmn(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_apply_callconv_197l87p(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, uint8_t a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_attr_power_of_two_gj02n2t(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
__attribute__((aligned(4))) gm_tqs5p4n2tq19v gm_f_callconv_attr_dd08ovc(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_check_allow_attr_3321gm7(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_check_import_cycles_2u7oqo0(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_check_method_candidates_k08rvu6(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_check_pub_signatures_plkds6r(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) void gm_f_clear_ufjejrf(void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_collect_terms_n8okg7m(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
inline __attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_complex_of_kh32tib(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_compute_binding_cl3ogob(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, uint64_t a2, const void * a3, void * a4);
__attribute__((aligned(4))) void gm_f_compute_decl_gcf8mq9(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_const_check_d48mnu7(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2, gm_tkn42u7na3t81 a3);
inline __attribute__((aligned(4))) gm_t9805qtonbsag gm_f_const_id_2mlh0og(void * a0, gm_cseahvg8v7p0vs a1, gm_csguankko4lmhd a2);
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_const_type_7rijs95(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2, gm_tkn42u7na3t81 a3);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_declare_adt_iefkbkk(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_tf5srh4mp4b3i a2, gm_b16_8 a3, void * a4);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_declare_adt_in_74tov03(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_tf5srh4mp4b3i a2, gm_b16_8 a3, void * a4, uint8_t a5);
__attribute__((aligned(4))) void gm_f_declare_const_or_type_4jaj4k6(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, const void * a2, void * a3, void * a4);
__attribute__((aligned(4))) void gm_f_declare_effect_9n12hc8(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, uint64_t a2, const void * a3, void * a4);
__attribute__((aligned(4))) void gm_f_declare_fn_6piolnr(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, const void * a2, void * a3);
__attribute__((aligned(4))) void gm_f_declare_foreign_6s86jje(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, const void * a2, void * a3);
__attribute__((aligned(4))) void gm_f_declare_global_729nttf(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, const void * a2, void * a3);
__attribute__((aligned(4))) gm_b0_1 gm_f_declare_method_candidate_e20bo1i(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, const void * a2, gm_t88rcucg1b7uk a3);
inline __attribute__((aligned(4))) void gm_f_def_of_m512sbu(void *gm_out, void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_tdgfduqjheqdm gm_f_def_of_m512sbu_c(void * a0, uint32_t a1);
__attribute__((aligned(4))) void gm_f_deinit_2l7c12h(void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_effect_arg_tys_2a7r8hh(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_t9qkh0rsooc8d a1, void * a2);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_effect_terms_bphj7th(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_emit_dph5v1e(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_1fu8oa9(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_1mrcnd6(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_1scem7m(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_2k4643t(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_5nn7ph0(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_73v4ttb(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_7adh53f(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_7od16il(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_8nf04m8(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_95isii0(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_a6or8fm(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_c3qnk0a(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_cp9r5v5(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_d0lhrvs(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_d3uqusp(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_d914i5u(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_dq2isu2(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_e7jr93v(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_eoluluh(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_f1ngfj0(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_f6g91fe(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_fh7svmj(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_g3tddrb(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_g6fvoq5(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_hii1vte(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_hp0e9gl(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ipqblac(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_iqgue92(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_j0utaj4(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_j6dmic1(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_j98q5u1(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_jpu5akn(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_kgfehvr(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_lh8ov0e(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_lp0ck0u(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_lpero57(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_mbkocse(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_nqsj3l3(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_oqjc4ke(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_osbc35j(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ot5ttb7(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_p6mn4us(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_pu15hdv(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_q870hn3(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_qmcc4fe(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_r81t436(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_s1lbe5s(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_s6l2ctu(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_sc53cu2(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_shtdeku(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_tslvjbf(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_uq9fi1n(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_v0dtga2(void);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_error_89glhjr(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_error_s6hn871(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_export_fn_8hcuasc(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_b16_8 a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) void * gm_f_field_default_expr_tqfuiff(void * a0, uint32_t a1, uint64_t a2);
inline __attribute__((aligned(4))) void * gm_f_file_o15fqku(void * a0, uint32_t a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_bfunt71(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_hcjjkrm(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_repdalk(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
__attribute__((aligned(4))) void gm_f_for_each_component_o5cuh7c(void * a0, gm_closure a1);
__attribute__((aligned(4))) void gm_f_format_0dvj28f(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_28p84ds(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_ck7vger(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_e79cvtl(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_h4jcodi(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_j4sks15(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_slm5ovr(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_t5fmuah(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_u2mas3v(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_v0s3sc7(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) gm_t9805qtonbsag gm_f_frame_up_h0ddp1d(void * a0, uint32_t a1, uint32_t a2);
inline __attribute__((aligned(4))) uint32_t gm_f_generic_of_1861hm9(void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_get_espvf96(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_get_mk00hfd(void *gm_out, void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_t9805qtonbsag gm_f_get_pk8ncss(void * a0, gm_trlabqsjjp0ds a1);
__attribute__((aligned(4))) gm_toceffb697p0u gm_f_get_qjsv97t(void * a0, const void * a1);
inline __attribute__((aligned(4))) uint8_t gm_f_has_param_u9t0efs(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) uint8_t gm_f_has_splice_80pgqas(gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_hash_map_0vd239u(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_hash_map_1so3e0v(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_hash_map_84luf3l(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_hash_map_8lre197(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_hash_map_gsp2k0o(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_hash_map_iibum07(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_hash_map_iug9p19(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_hash_map_ve01i1t(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) gm_trcagkhgm1qdu gm_f_hidden_part_is5hgf9(void * a0, gm_csguankko4lmhd a1, gm_t9qkh0rsooc8d a2);
__attribute__((aligned(4))) uint8_t gm_f_in_generic_scope_rvb92ab(void * a0);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_index_of_s9trr09(gm_b16_8 a0, uint64_t a1);
__attribute__((aligned(4))) uint8_t gm_f_is_ambient_m41pl1e(void * a0, uint32_t a1);
__attribute__((aligned(4))) uint8_t gm_f_is_effect_sum_dd9le16(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
inline __attribute__((aligned(4))) uint8_t gm_f_is_error_8k27gd1(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_positive_su660ab(const void * a0);
__attribute__((aligned(4))) uint8_t gm_f_is_structural_alias_qpmv4mu(void * a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_join_sfknljb(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) void gm_f_kind_jcnt4hm(void *gm_out, void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void gm_f_list_0bg6tcp(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_0bj26n7(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_0npefvp(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_1jabh22(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_1teief7(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_2343nn9(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_406coeg(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_4kh12ib(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_5dhgg4i(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_5pborc0(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_7s4om10(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_8r7pelg(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_9q64p2g(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_atkrn3m(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_au01ire(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_c02589n(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_dql1gah(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_dvbol2p(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_dvp9pla(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_eb5ua5r(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_epvducg(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_fam8ktt(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_go2qg0b(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_h93u1dq(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_i6iqfhd(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_ib5k4gb(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_je8tl46(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_kk91efj(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_l3jkoh2(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_m7es0sh(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_mvhhv5u(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_qs7fohu(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_qv7bq94(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_s83jn2q(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_sajcold(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_t29l5ca(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_tn37ahu(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_tqcq99u(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_ue2t2bm(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_uun92du(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_v3cpbrb(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_vhqj1o4(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) uint8_t gm_f_looks_like_type_ho5qv1k(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
inline __attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_lookup_59juqoo(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_lookup_module_name_cukfp7t(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_mag_to_u128_f843dal(void *gm_out, const void * a0);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_module_member_5fd2qm9(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, const void * a2, uint64_t a3);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_module_named_n1mcl3b(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
inline __attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_non_constant_part_itk7b0v(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tf5srh4mp4b3i gm_f_pop_j4rpvm5(void * a0);
__attribute__((aligned(4))) void gm_f_prelude_effect_gilrmm5(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_prelude_instance_arg_kh32dmf(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_t9805qtonbsag gm_f_prelude_template_7s9nsp0(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) void gm_f_printer_h7jigd7(void *gm_out, void * a0, void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_0fe09r3(void * a0, gm_tbcgm40aljf9h a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_3ef746b(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_4p3ivmb(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_89d0k90(void * a0, gm_csedogq9e4rp5r a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_8cr0sgp(void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_push_field_tys_v4ep4t3(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_t9qkh0rsooc8d a1, void * a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_fomabnf(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_h37silv(void * a0, gm_t3jsf43p1pths a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_kiclmcn(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_kpjuj6c(void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_push_o4ckdr1(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_puil3ql(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_ucgmlo2(void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_3pvr70r(void * a0, uint32_t a1, const void * a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_pjhbiaq(void * a0, const void * a1, gm_cseahvg8v7p0vs a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_qrf6f79(void * a0, gm_trlabqsjjp0ds a1, uint32_t a2);
__attribute__((aligned(4))) gm_thetuhtouve7f gm_f_range_of_dmmo768(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_t9805qtonbsag gm_f_range_template_dbt7218(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1);
__attribute__((aligned(4))) uint64_t gm_f_remove_om8q0q2(void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_report_cycle_mauubd4(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_report_eardf07(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_resolve_ambient_6orl0vo(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_resolve_decl_pus042v(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_resolve_declarations_vrpg1fl(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_resolve_effect_ref_bggirs2(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
__attribute__((aligned(4))) gm_tnpifv20irnph gm_f_resolve_effect_set_o7rc5od(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_resolve_import_944uqa9(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_resolve_type_jjodbei(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
__attribute__((aligned(4))) void gm_f_sema_r1nlnnm(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_set_inst_def_tcpabg0(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, const void * a2);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_set_state_3j300fi(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, const void * a2);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_8uijkbs(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_kmjeh8s(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_state_a5bnsjb(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
inline __attribute__((aligned(4))) uint32_t gm_f_template_9o92cvv(void * a0, uint32_t a1);
inline __attribute__((aligned(4))) void gm_f_to_u128_haka99r(void *gm_out, const void * a0);
inline __attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_to_u64_m01t1da(const void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_trap_hook_fn_5c0a4ds(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) gm_b16_8 gm_f_trim_end_6sjv6rg(gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_trim_fdrfe6i(gm_b16_8 a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_trim_start_179crak(gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_try_untyped_fj0h8i5(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_ty_vka2r8h(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_types_8a61ga3(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_unsupported_t7lgvp9(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) uint8_t gm_f_utf8_valid_a8oc1t8(gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_walk_v858co7(void * a0, gm_csguankko4lmhd a1, gm_closure a2);
extern uint8_t gm_c0u09tvsmdv[1] __attribute__((aligned(1)));
extern uint8_t gm_c1n574q3spi[8] __attribute__((aligned(8)));
extern uint8_t gm_c3t36dlh4g5[16] __attribute__((aligned(8)));
extern uint8_t gm_cbbjgr0cpr9[8] __attribute__((aligned(8)));
extern uint8_t gm_cp0okt01b9q[192] __attribute__((aligned(8)));
extern const uint8_t gm_s0rphlcbno91v[8];
extern const uint8_t gm_s1ft2nrvnhcbp[10];
extern const uint8_t gm_s2ph9f020upd2[7];
extern const uint8_t gm_s2urait735ofq[5];
extern const uint8_t gm_s43cq4krigs2b[10];
extern const uint8_t gm_s4eilu97sooh1[10];
extern const uint8_t gm_s523ivho5m3dq[13];
extern const uint8_t gm_s68uiiatavvqh[18];
extern const uint8_t gm_s6ibuqseh9tt7[152];
extern const uint8_t gm_s6lnp3h2ojl1f[77];
extern const uint8_t gm_s6vvtrmtnt3j1[5];
extern const uint8_t gm_s7f3pdsji9g3a[9];
extern const uint8_t gm_s80p0ld1j6rfg[10];
extern const uint8_t gm_s8n5rdam5o1sm[69];
extern const uint8_t gm_s8ro53eq54s60[44];
extern const uint8_t gm_s9e3360r5ee0o[7];
extern const uint8_t gm_s9ocqto6ckbos[58];
extern const uint8_t gm_sbqv06vnui8sl[88];
extern const uint8_t gm_sbrnv8n0dpinb[8];
extern const uint8_t gm_scacg345bm166[69];
extern const uint8_t gm_scllp0lpbljde[15];
extern const uint8_t gm_sdbvaa960ee0f[5];
extern const uint8_t gm_ses7tdrdt8a6u[6];
extern const uint8_t gm_seue7mq07v0oc[6];
extern const uint8_t gm_sg7i6m1frjk5n[63];
extern const uint8_t gm_shge5n743ua4h[3];
extern const uint8_t gm_shofa8pdml9fb[13];
extern const uint8_t gm_shud0j6jb8ucc[37];
extern const uint8_t gm_sinoph5a0op58[10];
extern const uint8_t gm_sj1k4t2de5144[6];
extern const uint8_t gm_sj2f7h0km68ut[32];
extern const uint8_t gm_sj3lfuqd4f1np[37];
extern const uint8_t gm_sjqs7q3ostqbm[4];
extern const uint8_t gm_slb6cj7ndgvdm[6];
extern const uint8_t gm_smo0niiqr9onn[11];
extern const uint8_t gm_snenkhfev166s[3];
extern const uint8_t gm_spe8t3e79vnal[7];
extern const uint8_t gm_sppcsr92ukfol[64];
extern const uint8_t gm_spq5ja4c7ciga[11];
extern const uint8_t gm_sptnm8c6c52dr[7];
extern const uint8_t gm_spvg0rt0dloah[9];
extern const uint8_t gm_sq4sf3euttrc8[5];
extern const uint8_t gm_srb1158jlk9u7[75];
extern const char gm_src0ih77f43mhf2[];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_srcbsc23is03ttm[];
extern const char gm_srcc2qutubarto1[];
extern const char gm_srcgfglnv8p059r[];
extern const char gm_srclv0dcbbfhves[];
extern const char gm_srcrgc9bkquf6nr[];
extern const char gm_srcv1nri2247d2r[];
extern const uint8_t gm_srjo6vku5gka5[20];
extern const uint8_t gm_srsiesc4al3v4[50];
extern const uint8_t gm_ss6g2koqn63bf[5];
extern const uint8_t gm_st027n235d4us[45];
extern const uint8_t gm_st6dd0k5nckh3[58];
extern const uint8_t gm_stjei6jfiba56[5];
extern const uint8_t gm_str26682vod5l[38];
extern const uint8_t gm_suuv0qpi4v4e4[8];
extern const uint8_t gm_sv48vll3qortv[40];
extern const gm_type gm_tyo86ks9qkvt;
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b) {
    uintptr_t n = GM_LD(uintptr_t, a + 8);
    if (n != GM_LD(uintptr_t, b + 8)) return false;
    const uint8_t *pa = GM_LD(void *, a), *pb = GM_LD(void *, b);
    return n == 0 || memcmp(pa, pb, n * 1) == 0;
}
inline bool gm_eq6groq37ram(const uint8_t *a, const uint8_t *b) {
    return GM_LD(uint64_t, a) == GM_LD(uint64_t, b);
}
inline bool gm_eq7mri15hk78(const uint8_t *a, const uint8_t *b) {
    if (a[8] != b[8]) return false;
    return !a[8] || gm_eq6groq37ram(a, b);
}
inline __attribute__((aligned(4))) void gm_f_add_label_50fj7vv(void * a0, gm_cseahvg8v7p0vs a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_add_label_50fj7vv F;
    void * gm_l0; /* d */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_b16_8 gm_l2; /* message */
    GM_STACK_CHECK(&F, 116u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_tl874t2atnlh3, a1);
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        void * t3 = (void *)(((uint8_t *)t2) + 40);
        gm_tbgd14esf0jvi t4;
        memset((uint8_t *)&t4 + 12, 0, 4);
        gm_tl874t2atnlh3 t5 = gm_l1;
        GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t4 + 0, t5);
        gm_b16_8 t6 = gm_l2;
        GM_ST(gm_b16_8, (uint8_t *)&t4 + 16, t6);
        gm_tftb1odc739t5 t7 = gm_f_push_kiclmcn(t3, &t4);
        gm_tftb1odc739t5 t8 = t7;
        if (((((uint8_t *)&t8))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t8) + 1, gm_srcgfglnv8p059r, 92, 5);
        gm_b0_1 t9 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_adt_name_h7mjik7(void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_adt_name_h7mjik7 F;
    void * gm_l0; /* t */
    uint32_t gm_l1; /* id */
    GM_STACK_CHECK_SHALLOW(&F, 96u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint32_t t1 = gm_l1;
    uint64_t t2 = (uint64_t)t1;
    uint64_t t3 = (uintptr_t)t2;
    void * t4 = gm_l0;
    uint64_t t5 = GM_LD(uintptr_t, ((((uint8_t *)t4) + 136) + 0) + 8);
    if (t3 >= (uintptr_t)(t5)) gm_trap_index(t3, t5, gm_src0ih77f43mhf2, 268, 59);
    gm_b16_8 t6 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t4) + 136) + 0)) + t3 * 16));
    F.ret = t6;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_clear_ufjejrf(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_clear_ufjejrf F;
    void * gm_l0; /* l */
    GM_STACK_CHECK_SHALLOW(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        void * t3 = gm_l0;
        uint64_t t4 = GM_LD(uintptr_t, (((uint8_t *)t3) + 0) + 8);
        void * t5 = ((uint8_t *)GM_LD(void *, (((uint8_t *)t3) + 0)));
        uint64_t t6 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t7 = (uintptr_t)((uint64_t)0ULL);
        if (t6 > t7 || t7 > (uintptr_t)t4) gm_trap_slice(t6, t7, t4, gm_srcrgc9bkquf6nr, 137, 15);
        gm_b16_8 t8;
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 40)));
        GM_ST(uintptr_t, (uint8_t *)&t8 + 8, (uintptr_t)(t7 - t6));
        GM_ST(gm_b16_8, (((uint8_t *)t2) + 0), t8);
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_complex_of_kh32tib(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_complex_of_kh32tib F;
    void * gm_l0; /* s */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 64u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    gm_t88rcucg1b7uk t2 = gm_l1;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s0rphlcbno91v));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(7));
    gm_tkn42u7na3t81 t4 = gm_f_prelude_instance_arg_kh32dmf((void *)NULL, cx, NULL, t1, t2, t3);
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
inline __attribute__((aligned(4))) gm_t9805qtonbsag gm_f_const_id_2mlh0og(void * a0, gm_cseahvg8v7p0vs a1, gm_csguankko4lmhd a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_const_id_2mlh0og F;
    void * gm_l0; /* s */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_t88rcucg1b7uk gm_l2; /* ty */
    GM_STACK_CHECK(&F, 96u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_tl874t2atnlh3, a1);
    gm_l2 = GM_CONV(gm_t88rcucg1b7uk, a2);
    void * t1 = gm_l0;
    void * t2 = (void *)(((uint8_t *)t1) + 2008);
    gm_tl874t2atnlh3 t3 = gm_l1;
    gm_t88rcucg1b7uk t4 = gm_l2;
    gm_trlabqsjjp0ds t5;
    GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t5 + 0), t3);
    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t5 + 12), t4);
    gm_t9805qtonbsag t6 = gm_f_get_pk8ncss(t2, t5);
    F.ret = t6;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_declare_adt_iefkbkk(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_tf5srh4mp4b3i a2, gm_b16_8 a3, void * a4) {
    struct gm_fr_declare_adt_iefkbkk F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* module */
    gm_tf5srh4mp4b3i gm_l2; /* decl */
    gm_b16_8 gm_l3; /* name */
    void * gm_l4; /* lit */
    GM_STACK_CHECK(&F, 124u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    void * t1 = gm_l0;
    uint64_t t2 = gm_l1;
    gm_tf5srh4mp4b3i t3 = gm_l2;
    gm_b16_8 t4 = gm_l3;
    void * t5 = gm_l4;
    gm_t88rcucg1b7uk t6 = gm_f_declare_adt_in_74tov03((void *)NULL, cx, NULL, t1, t2, t3, t4, t5, ((uint8_t)0));
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
inline __attribute__((aligned(4))) void gm_f_def_of_m512sbu(void *gm_out, void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_def_of_m512sbu F;
    void * gm_l0; /* s */
    uint32_t gm_l1; /* f */
    GM_STACK_CHECK(&F, 341u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t3 = gm_l0;
        void * t4 = (void *)(((uint8_t *)t3) + 2064);
        uint32_t t5 = gm_l1;
        gm_tdn4erebgflsv t6;
        gm_f_get_mk00hfd((void *)&t6, t4, t5);
        gm_tdn4erebgflsv t7 = t6;
        if (!((((uint8_t *)&t7))[64] != 0)) goto ie2;
        gm_tdgfduqjheqdm t8 = GM_LD(gm_tdgfduqjheqdm, ((uint8_t *)&t7));
        F.l2 = t8;
        {
            memmove(gm_out, ((uint8_t *)&F.l2), 64);
            goto gm_fdone;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        uint32_t t10 = gm_l1;
        uint64_t t11 = (uint64_t)t10;
        uint64_t t12 = (uintptr_t)t11;
        void * t13 = gm_l0;
        uint64_t t14 = GM_LD(uintptr_t, ((((uint8_t *)t13) + 384) + 0) + 8);
        if (t12 >= (uintptr_t)(t14)) gm_trap_index(t12, t14, gm_srclv0dcbbfhves, 581, 12);
        gm_tdgfduqjheqdm t15 = GM_LD(gm_tdgfduqjheqdm, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t13) + 384) + 0)) + t12 * 64));
        uint8_t t16 = 0;
        F.ret = t15;
        goto gm_fret;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_effect_terms_bphj7th(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_effect_terms_bphj7th F;
    void * gm_l0; /* e */
    GM_STACK_CHECK(&F, 73u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_qs7fohu((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = gm_l0;
        void * t3 = (void *)((uint8_t *)&F.l1);
        gm_b0_1 t4 = gm_f_collect_terms_n8okg7m((void *)NULL, cx, NULL, t2, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_b16_8 t5 = GM_LD(gm_b16_8, (((uint8_t *)&F.l1) + 0));
        uint8_t t6 = 0;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_emit_dph5v1e(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_emit_dph5v1e F;
    void * gm_l0; /* s */
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&F.l1, a1, sizeof F.l1);
    {
        void * t2 = gm_l0;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 0));
        void * t4 = GM_LD(void *, (((uint8_t *)t3) + 8));
        gm_f_push_o4ckdr1(t4, &(*(gm_t4gut2c9fj1kt *)((uint8_t *)&F.l1)));
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_1fu8oa9(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_1fu8oa9 F;
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
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 64)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_1mrcnd6(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_1mrcnd6 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_1scem7m(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_1scem7m F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_2k4643t(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_2k4643t F;
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
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 48)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_73v4ttb(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_73v4ttb F;
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
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 8)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_7adh53f(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_7adh53f F;
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
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 48)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_7od16il(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_7od16il F;
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
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 96)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_8nf04m8(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_8nf04m8 F;
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
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 88)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_95isii0(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_95isii0 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_a6or8fm(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_a6or8fm F;
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
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 20)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_c3qnk0a(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_c3qnk0a F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_cp9r5v5(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_cp9r5v5 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_d0lhrvs(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_d0lhrvs F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_d3uqusp(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_d3uqusp F;
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
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 28)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_dq2isu2(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_dq2isu2 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_e7jr93v(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_e7jr93v F;
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
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 48)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_eoluluh(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_eoluluh F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_f1ngfj0(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_f1ngfj0 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_f6g91fe(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_f6g91fe F;
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
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 224)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_fh7svmj(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_fh7svmj F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_g3tddrb(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_g3tddrb F;
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
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 88)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_g6fvoq5(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_g6fvoq5 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_hii1vte(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_hii1vte F;
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
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 64)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_hp0e9gl(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_hp0e9gl F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ipqblac(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_ipqblac F;
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
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 216)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_iqgue92(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_iqgue92 F;
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
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 8)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_j0utaj4(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_j0utaj4 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_j6dmic1(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_j6dmic1 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_j98q5u1(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_j98q5u1 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_jpu5akn(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_jpu5akn F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_kgfehvr(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_kgfehvr F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_lh8ov0e(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_lh8ov0e F;
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
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 72)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_lp0ck0u(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_lp0ck0u F;
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
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 8)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_lpero57(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_lpero57 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_mbkocse(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_mbkocse F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_nqsj3l3(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_nqsj3l3 F;
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
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 104)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_oqjc4ke(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_oqjc4ke F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_osbc35j(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_osbc35j F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ot5ttb7(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_ot5ttb7 F;
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
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 136)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_p6mn4us(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_p6mn4us F;
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
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 48)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_pu15hdv(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_pu15hdv F;
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
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 20)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_q870hn3(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_q870hn3 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_qmcc4fe(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_qmcc4fe F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_r81t436(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_r81t436 F;
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
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 12)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_s1lbe5s(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_s1lbe5s F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_s6l2ctu(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_s6l2ctu F;
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
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 48)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_sc53cu2(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_sc53cu2 F;
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
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 12)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_shtdeku(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_shtdeku F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_tslvjbf(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_tslvjbf F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_uq9fi1n(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_uq9fi1n F;
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
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 8)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_v0dtga2(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_v0dtga2 F;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_error_89glhjr(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2) {
    struct gm_fr_error_89glhjr F;
    void * gm_l0; /* s */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_b16_8 gm_l2; /* message */
    GM_STACK_CHECK(&F, 88u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 0));
        void * t4 = GM_LD(void *, (((uint8_t *)t3) + 8));
        gm_tl874t2atnlh3 t5 = gm_l1;
        gm_b16_8 t6 = gm_l2;
        gm_b0_1 t7 = gm_f_report_eardf07((void *)NULL, cx, NULL, t4, t5, t6);
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
inline __attribute__((aligned(4))) void * gm_f_file_o15fqku(void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_file_o15fqku F;
    void * gm_l0; /* sm */
    uint32_t gm_l1; /* id */
    GM_STACK_CHECK_SHALLOW(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint32_t t1 = gm_l1;
    uint64_t t2 = (uint64_t)t1;
    uint64_t t3 = (uintptr_t)t2;
    void * t4 = gm_l0;
    uint64_t t5 = GM_LD(uintptr_t, ((((uint8_t *)t4) + 0) + 0) + 8);
    if (t3 >= (uintptr_t)(t5)) gm_trap_index(t3, t5, gm_srcv1nri2247d2r, 78, 75);
    void * t6 = (void *)((uint8_t *)GM_LD(void *, ((((uint8_t *)t4) + 0) + 0)) + t3 * 80);
    F.ret = t6;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_bfunt71(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0) {
    struct gm_fr_fn_bfunt71 F;
    gm_t88rcucg1b7uk gm_l0; /* c */
    GM_STACK_CHECK(&F, 40u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_walk_v858co7 *)((&F.h)->up))->l0));
        gm_t88rcucg1b7uk t3 = gm_l0;
        gm_closure t4 = GM_LD(gm_closure, ((uint8_t *)&((struct gm_fr_walk_v858co7 *)((&F.h)->up))->l2));
        gm_f_walk_v858co7(t2, GM_CONV(gm_csguankko4lmhd, t3), t4);
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
inline __attribute__((aligned(4))) uint32_t gm_f_generic_of_1861hm9(void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_generic_of_1861hm9 F;
    void * gm_l0; /* s */
    uint32_t gm_l1; /* f */
    gm_tiilmfd16v3fc gm_l2; /* inst */
    GM_STACK_CHECK_SHALLOW(&F, 114u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint32_t t3 = gm_l1;
        uint64_t t4 = (uint64_t)t3;
        uint64_t t5 = (uintptr_t)t4;
        void * t6 = gm_l0;
        uint64_t t7 = GM_LD(uintptr_t, ((((uint8_t *)t6) + 344) + 0) + 8);
        if (t5 >= (uintptr_t)(t7)) gm_trap_index(t5, t7, gm_srclv0dcbbfhves, 590, 16);
        if (!(((((uint8_t *)GM_LD(void *, ((((uint8_t *)t6) + 344) + 0)) + t5 * 136) + 96))[24] != 0)) goto ie2;
        gm_tiilmfd16v3fc t8 = GM_LD(gm_tiilmfd16v3fc, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t6) + 344) + 0)) + t5 * 136) + 96));
        gm_l2 = t8;
        {
            uint32_t t10 = GM_LD(uint32_t, (((uint8_t *)&gm_l2) + 0));
            uint8_t t11 = 0;
            F.ret = t10;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        uint32_t t12 = gm_l1;
        uint8_t t13 = 0;
        F.ret = t12;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_has_param_u9t0efs(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_has_param_u9t0efs F;
    void * gm_l0; /* t */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    GM_STACK_CHECK_SHALLOW(&F, 53u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    uint32_t t1 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 0));
    uint64_t t2 = (uint64_t)t1;
    uint64_t t3 = (uintptr_t)t2;
    void * t4 = gm_l0;
    uint64_t t5 = GM_LD(uintptr_t, ((((uint8_t *)t4) + 40) + 0) + 8);
    if (t3 >= (uintptr_t)(t5)) gm_trap_index(t3, t5, gm_src0ih77f43mhf2, 249, 57);
    uint8_t t6 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t4) + 40) + 0)) + t3 * 1));
    uint8_t t7 = (uint8_t)(t6 & ((uint8_t)1ULL));
    uint8_t t8 = (!(t7 == ((uint8_t)0ULL)));
    F.ret = t8;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_hash_map_0vd239u(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_hash_map_0vd239u F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_b16_8 t2 = gm_f_empty_slice_j98q5u1();
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
inline __attribute__((aligned(4))) void gm_f_hash_map_1so3e0v(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_hash_map_1so3e0v F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_b16_8 t2 = gm_f_empty_slice_95isii0();
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
inline __attribute__((aligned(4))) void gm_f_hash_map_84luf3l(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_hash_map_84luf3l F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_b16_8 t2 = gm_f_empty_slice_jpu5akn();
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
inline __attribute__((aligned(4))) void gm_f_hash_map_8lre197(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_hash_map_8lre197 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_b16_8 t2 = gm_f_empty_slice_sc53cu2();
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
inline __attribute__((aligned(4))) void gm_f_hash_map_gsp2k0o(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_hash_map_gsp2k0o F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_b16_8 t2 = gm_f_empty_slice_7adh53f();
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
inline __attribute__((aligned(4))) void gm_f_hash_map_iibum07(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_hash_map_iibum07 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_b16_8 t2 = gm_f_empty_slice_lh8ov0e();
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
inline __attribute__((aligned(4))) void gm_f_hash_map_iug9p19(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_hash_map_iug9p19 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_b16_8 t2 = gm_f_empty_slice_mbkocse();
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
inline __attribute__((aligned(4))) void gm_f_hash_map_ve01i1t(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_hash_map_ve01i1t F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_b16_8 t2 = gm_f_empty_slice_pu15hdv();
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
inline __attribute__((aligned(4))) gm_trcagkhgm1qdu gm_f_hidden_part_is5hgf9(void * a0, gm_csguankko4lmhd a1, gm_t9qkh0rsooc8d a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_hidden_part_is5hgf9 F;
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 89u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    F.l2 = a2;
    {
        gm_trcagkhgm1qdu t2;
        memset(&t2, 0, sizeof t2);
        F.l3 = t2;
    }
    {
        void * t3 = F.l0;
        gm_t88rcucg1b7uk t4 = gm_l1;
        gm_closure t5 = ((gm_closure){(void *)gm_f_fn_repdalk, (void *)&F.h, cx});
        gm_f_walk_v858co7(t3, GM_CONV(gm_csguankko4lmhd, t4), t5);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_trcagkhgm1qdu t6 = F.l3;
        uint8_t t7 = 0;
        F.ret = t6;
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_error_8k27gd1(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_error_8k27gd1 F;
    void * gm_l0; /* t */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    GM_STACK_CHECK_SHALLOW(&F, 139u);
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
    uint8_t t5 = ((GM_LD(uint8_t, ((uint8_t *)&t4) + 0) == ((uint8_t)26ULL)));
    F.ret = t5;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_positive_su660ab(const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_positive_su660ab F;
    GM_STACK_CHECK_SHALLOW(&F, 14u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    uint8_t t1 = 0;
    uint8_t t2 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    uint8_t t3 = (uint8_t)!t2;
    if (t3) {
        uint64_t t4 = GM_LD(uintptr_t, ((((uint8_t *)a0) + 8) + 0) + 8);
        uint8_t t5 = (t4 > ((uint64_t)0ULL));
        t1 = t5;
    } else t1 = 0;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
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
inline __attribute__((aligned(4))) void gm_f_list_0bg6tcp(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_0bg6tcp F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_kgfehvr();
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
inline __attribute__((aligned(4))) void gm_f_list_0bj26n7(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_0bj26n7 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_qmcc4fe();
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
inline __attribute__((aligned(4))) void gm_f_list_0npefvp(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_0npefvp F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_dq2isu2();
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
inline __attribute__((aligned(4))) void gm_f_list_1jabh22(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_1jabh22 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_f6g91fe();
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
inline __attribute__((aligned(4))) void gm_f_list_1teief7(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_1teief7 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_1mrcnd6();
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
inline __attribute__((aligned(4))) void gm_f_list_2343nn9(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_2343nn9 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_eoluluh();
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
inline __attribute__((aligned(4))) void gm_f_list_406coeg(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_406coeg F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_j0utaj4();
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
inline __attribute__((aligned(4))) void gm_f_list_4kh12ib(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_4kh12ib F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_s6l2ctu();
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
inline __attribute__((aligned(4))) void gm_f_list_5dhgg4i(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_5dhgg4i F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_73v4ttb();
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
inline __attribute__((aligned(4))) void gm_f_list_5pborc0(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_5pborc0 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_nqsj3l3();
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
inline __attribute__((aligned(4))) void gm_f_list_7s4om10(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_7s4om10 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_r81t436();
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
inline __attribute__((aligned(4))) void gm_f_list_8r7pelg(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_8r7pelg F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_a6or8fm();
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
inline __attribute__((aligned(4))) void gm_f_list_9q64p2g(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_9q64p2g F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_v0dtga2();
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
inline __attribute__((aligned(4))) void gm_f_list_atkrn3m(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_atkrn3m F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_g6fvoq5();
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
inline __attribute__((aligned(4))) void gm_f_list_au01ire(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_au01ire F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_f1ngfj0();
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
inline __attribute__((aligned(4))) void gm_f_list_c02589n(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_c02589n F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_e7jr93v();
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
inline __attribute__((aligned(4))) void gm_f_list_dql1gah(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_dql1gah F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_cp9r5v5();
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
inline __attribute__((aligned(4))) void gm_f_list_dvbol2p(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_dvbol2p F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_1fu8oa9();
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
inline __attribute__((aligned(4))) void gm_f_list_dvp9pla(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_dvp9pla F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_ot5ttb7();
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
inline __attribute__((aligned(4))) void gm_f_list_eb5ua5r(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_eb5ua5r F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_ipqblac();
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
inline __attribute__((aligned(4))) void gm_f_list_fam8ktt(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_fam8ktt F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_iqgue92();
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
inline __attribute__((aligned(4))) void gm_f_list_h93u1dq(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_h93u1dq F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_oqjc4ke();
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
inline __attribute__((aligned(4))) void gm_f_list_i6iqfhd(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_i6iqfhd F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_lpero57();
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
inline __attribute__((aligned(4))) void gm_f_list_ib5k4gb(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_ib5k4gb F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_d3uqusp();
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
inline __attribute__((aligned(4))) void gm_f_list_je8tl46(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_je8tl46 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_shtdeku();
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
inline __attribute__((aligned(4))) void gm_f_list_kk91efj(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_kk91efj F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_hp0e9gl();
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
inline __attribute__((aligned(4))) void gm_f_list_l3jkoh2(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_l3jkoh2 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_8nf04m8();
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
inline __attribute__((aligned(4))) void gm_f_list_m7es0sh(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_m7es0sh F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_c3qnk0a();
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
inline __attribute__((aligned(4))) void gm_f_list_mvhhv5u(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_mvhhv5u F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_osbc35j();
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
inline __attribute__((aligned(4))) void gm_f_list_qs7fohu(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_qs7fohu F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_lp0ck0u();
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
inline __attribute__((aligned(4))) void gm_f_list_qv7bq94(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_qv7bq94 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_hii1vte();
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
inline __attribute__((aligned(4))) void gm_f_list_s83jn2q(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_s83jn2q F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_s1lbe5s();
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
inline __attribute__((aligned(4))) void gm_f_list_sajcold(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_sajcold F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_tslvjbf();
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
inline __attribute__((aligned(4))) void gm_f_list_t29l5ca(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_t29l5ca F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_2k4643t();
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
inline __attribute__((aligned(4))) void gm_f_list_tn37ahu(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_tn37ahu F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_q870hn3();
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
inline __attribute__((aligned(4))) void gm_f_list_tqcq99u(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_tqcq99u F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_j6dmic1();
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
inline __attribute__((aligned(4))) void gm_f_list_ue2t2bm(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_ue2t2bm F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_d0lhrvs();
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
inline __attribute__((aligned(4))) void gm_f_list_uun92du(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_uun92du F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_uq9fi1n();
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
inline __attribute__((aligned(4))) void gm_f_list_v3cpbrb(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_v3cpbrb F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_1scem7m();
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
inline __attribute__((aligned(4))) void gm_f_list_vhqj1o4(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_vhqj1o4 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_7od16il();
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
inline __attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_lookup_59juqoo(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_lookup_59juqoo F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* module */
    gm_b16_8 gm_l2; /* name */
    GM_STACK_CHECK(&F, 136u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    uint64_t t1 = gm_l1;
    uint64_t t2 = (uintptr_t)t1;
    void * t3 = gm_l0;
    uint64_t t4 = GM_LD(uintptr_t, ((((uint8_t *)t3) + 200) + 0) + 8);
    if (t2 >= (uintptr_t)(t4)) gm_trap_index(t2, t4, gm_srcc2qutubarto1, 547, 78);
    void * t5 = (void *)(((uint8_t *)GM_LD(void *, ((((uint8_t *)t3) + 200) + 0)) + t2 * 184) + 56);
    gm_b16_8 t6 = gm_l2;
    gm_tf5srh4mp4b3i t7 = gm_f_get_espvf96(t5, t6);
    F.ret = t7;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_non_constant_part_itk7b0v(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_non_constant_part_itk7b0v F;
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 73u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    {
        gm_tkn42u7na3t81 t2;
        memset(&t2, 0, sizeof t2);
        F.l2 = t2;
    }
    {
        void * t3 = F.l0;
        gm_t88rcucg1b7uk t4 = gm_l1;
        gm_closure t5 = ((gm_closure){(void *)gm_f_fn_hcjjkrm, (void *)&F.h, cx});
        gm_f_walk_v858co7(t3, GM_CONV(gm_csguankko4lmhd, t4), t5);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_tkn42u7na3t81 t6 = F.l2;
        uint8_t t7 = 0;
        F.ret = t6;
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tf5srh4mp4b3i gm_f_pop_j4rpvm5(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_pop_j4rpvm5 F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* n */
    uint64_t gm_l2; /* x */
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
            gm_tf5srh4mp4b3i t8;
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
        uint64_t t17 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)t15) + 0)) + t14 * 8));
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
        GM_ST(void *, (uint8_t *)&t28, (void *)(((uint8_t *)t21 + t22 * 8)));
        GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(t27 - t22));
        GM_ST(gm_b16_8, (((uint8_t *)t18) + 0), t28);
    }
    {
        uint64_t t29 = gm_l2;
        gm_tf5srh4mp4b3i t30;
        GM_ST(uint8_t, (uint8_t *)&t30 + 9, 0);
        GM_ST(uint16_t, (uint8_t *)&t30 + 10, 0);
        GM_ST(uint32_t, (uint8_t *)&t30 + 12, 0);
        GM_ST(uint64_t, ((uint8_t *)&t30), t29);
        ((uint8_t *)&t30)[8] = 1;
        uint8_t t31 = 0;
        F.ret = t30;
        goto gm_fret;
    }
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
inline __attribute__((aligned(4))) gm_t9805qtonbsag gm_f_range_template_dbt7218(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1) {
    struct gm_fr_range_template_dbt7218 F;
    void * gm_l0; /* s */
    uint8_t gm_l1; /* inclusive */
    GM_STACK_CHECK(&F, 122u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    gm_b16_8 t2;
    uint8_t t4 = gm_l1;
    if (!t4) goto ie3;
    gm_b16_8 t6;
    GM_ST(void *, (uint8_t *)&t6, (void *)(gm_s43cq4krigs2b));
    GM_ST(uintptr_t, (uint8_t *)&t6 + 8, (uintptr_t)(9));
    gm_b16_8 t7 = t6;
    t2 = t7;
    goto ix3;
ie3:;
    gm_b16_8 t9;
    GM_ST(void *, (uint8_t *)&t9, (void *)(gm_seue7mq07v0oc));
    GM_ST(uintptr_t, (uint8_t *)&t9 + 8, (uintptr_t)(5));
    gm_b16_8 t10 = t9;
    t2 = t10;
ix3:;
    gm_t9805qtonbsag t11 = gm_f_prelude_template_7s9nsp0((void *)NULL, cx, NULL, t1, t2);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_report_eardf07(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2) {
    struct gm_fr_report_eardf07 F;
    void * gm_l0; /* ds */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_b16_8 gm_l2; /* message */
    GM_STACK_CHECK(&F, 256u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        gm_tl874t2atnlh3 t3 = gm_l1;
        gm_b16_8 t4 = gm_l2;
        gm_t4gut2c9fj1kt t5;
        gm_f_error_s6hn871((void *)&t5, (void *)NULL, cx, NULL, t3, t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_push_o4ckdr1(t2, &t5);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_set_inst_def_tcpabg0(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, const void * a2) {
    struct gm_fr_set_inst_def_tcpabg0 F;
    void * gm_l0; /* s */
    uint32_t gm_l1; /* f */
    GM_STACK_CHECK(&F, 36u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        void * t3 = (void *)(((uint8_t *)t2) + 2064);
        uint32_t t4 = gm_l1;
        gm_tftb1odc739t5 t5 = gm_f_put_3pvr70r(t3, t4, &(*(gm_tdgfduqjheqdm *)((uint8_t *)a2)));
        gm_tftb1odc739t5 t6 = t5;
        if (((((uint8_t *)&t6))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t6) + 1, gm_srclv0dcbbfhves, 607, 5);
        gm_b0_1 t7 = GM_LD(gm_b0_1, (((uint8_t *)&t6) + 1));
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_set_state_3j300fi(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, const void * a2) {
    struct gm_fr_set_state_3j300fi F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* id */
    GM_STACK_CHECK(&F, 496u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    memcpy(&F.l2, a2, sizeof F.l2);
    {
        {
            void * t3 = gm_l0;
            uint64_t t4 = gm_l1;
            gm_tagrkbqh054h7 t5;
            gm_f_state_a5bnsjb((void *)&t5, (void *)NULL, cx, NULL, t3, t4);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
    }
    {
        uint64_t t6 = gm_l1;
        uint64_t t7 = (uintptr_t)t6;
        void * t8 = gm_l0;
        uint64_t t9 = GM_LD(uintptr_t, ((((uint8_t *)t8) + 304) + 0) + 8);
        if (t7 >= (uintptr_t)(t9)) gm_trap_index(t7, t9, gm_srclv0dcbbfhves, 752, 5);
        gm_tagrkbqh054h7 t10 = F.l2;
        GM_ST(gm_tagrkbqh054h7, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t8) + 304) + 0)) + t7 * 216), t10);
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
inline __attribute__((aligned(4))) uint32_t gm_f_template_9o92cvv(void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_template_9o92cvv F;
    void * gm_l0; /* t */
    uint32_t gm_l1; /* id */
    GM_STACK_CHECK_SHALLOW(&F, 60u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint32_t t1 = gm_l1;
    uint64_t t2 = (uint64_t)t1;
    uint64_t t3 = (uintptr_t)t2;
    void * t4 = gm_l0;
    uint64_t t5 = GM_LD(uintptr_t, ((((uint8_t *)t4) + 176) + 0) + 8);
    if (t3 >= (uintptr_t)(t5)) gm_trap_index(t3, t5, gm_src0ih77f43mhf2, 276, 56);
    uint32_t t6 = GM_LD(uint32_t, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t4) + 176) + 0)) + t3 * 4));
    F.ret = t6;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_to_u128_haka99r(void *gm_out, const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_to_u128_haka99r F;
    GM_STACK_CHECK_SHALLOW(&F, 34u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        uint8_t t3 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
        if (!t3) goto ie2;
        {
            gm_t9p38b040grms t5;
            memset(&t5, 0, sizeof t5);
            uint8_t t6 = 0;
            F.ret = t5;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        gm_f_mag_to_u128_f843dal((void *)&(*(gm_t9p38b040grms *)gm_out), &(*(gm_tbci5ospk5nnn *)((uint8_t *)a0)));
        goto gm_fdone;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_to_u64_m01t1da(const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_to_u64_m01t1da F;
    unsigned __int128 gm_l1; /* v */
    GM_STACK_CHECK_SHALLOW(&F, 234u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_t9p38b040grms t2;
        gm_f_to_u128_haka99r((void *)&t2, &(*(gm_tbci5ospk5nnn *)((uint8_t *)a0)));
        gm_t9p38b040grms t3 = t2;
        unsigned __int128 t4;
        if (((((uint8_t *)&t3))[16] != 0)) {
            unsigned __int128 t5 = GM_LD(unsigned __int128, (((uint8_t *)&t3) + 0));
            t4 = t5;
        } else {
            gm_tf5srh4mp4b3i t6;
            memset(&t6, 0, sizeof t6);
            uint8_t t7 = 0;
            F.ret = t6;
            goto gm_fret;
        }
        gm_l1 = t4;
    }
    {
        unsigned __int128 t8 = gm_l1;
        gm_tf5srh4mp4b3i t9;
        uint64_t t10 = (uint64_t)t8;
        gm_tf5srh4mp4b3i t11;
        GM_ST(uint8_t, (uint8_t *)&t11 + 9, 0);
        GM_ST(uint16_t, (uint8_t *)&t11 + 10, 0);
        GM_ST(uint32_t, (uint8_t *)&t11 + 12, 0);
        GM_ST(uint64_t, ((uint8_t *)&t11), t10);
        ((uint8_t *)&t11)[8] = 1;
        gm_tf5srh4mp4b3i t12;
        memset(&t12, 0, sizeof t12);
        t9 = ((unsigned __int128)(t8) <= (((unsigned __int128)0ULL << 64) | 18446744073709551615ULL)) ? t11 : t12;
        uint8_t t13 = 0;
        F.ret = t9;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_trim_fdrfe6i(gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_trim_fdrfe6i F;
    gm_b16_8 gm_l0; /* s */
    GM_STACK_CHECK_SHALLOW(&F, 96u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t1 = gm_l0;
    gm_b16_8 t2 = gm_f_trim_start_179crak(t1);
    gm_b16_8 t3 = gm_f_trim_end_6sjv6rg(t2);
    F.ret = t3;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_unsupported_t7lgvp9(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2) {
    struct gm_fr_unsupported_t7lgvp9 F;
    void * gm_l0; /* s */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_b16_8 gm_l2; /* what */
    GM_STACK_CHECK(&F, 216u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        gm_tl874t2atnlh3 t3 = gm_l1;
        gm_t6j6smj7qoctu t4;
        gm_b16_8 t5 = GM_LD(gm_b16_8, gm_c3t36dlh4g5);
        GM_ST(gm_b16_8, (uint8_t *)&t4 + 0, t5);
        gm_b16_8 t6 = gm_l2;
        GM_ST(gm_b16_8, (uint8_t *)&t4 + 16, t6);
        gm_tabb710d9mq2e t7;
        gm_f_format_e79cvtl((void *)&t7, (void *)NULL, cx, NULL, &t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t8 = t7;
        if (((((uint8_t *)&t8))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t8) + 8, gm_srclv0dcbbfhves, 635, 19);
        gm_b16_8 t9 = GM_LD(gm_b16_8, (((uint8_t *)&t8) + 8));
        gm_b16_8 t10 = t9;
        gm_b16_8 t11 = GM_LD(gm_b16_8, ((uint8_t *)&t10));
        gm_b0_1 t12 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t2, t3, t11);
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
inline __attribute__((aligned(4))) uint32_t gm_f_add_const_8u8b1ja(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_add_const_8u8b1ja F;
    void * gm_l0; /* s */
    uint32_t gm_l2; /* id */
    GM_STACK_CHECK(&F, 121u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        uint64_t t3 = GM_LD(uintptr_t, ((((uint8_t *)t2) + 1968) + 0) + 8);
        uint32_t t4 = (uint32_t)t3;
        gm_l2 = t4;
    }
    {
        void * t5 = gm_l0;
        void * t6 = (void *)(((uint8_t *)t5) + 1968);
        gm_tftb1odc739t5 t7 = gm_f_push_8cr0sgp(t6, &(*(gm_tjcptv9sagfq8 *)((uint8_t *)a1)));
        gm_tftb1odc739t5 t8 = t7;
        if (((((uint8_t *)&t8))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t8) + 1, gm_srclv0dcbbfhves, 616, 5);
        gm_b0_1 t9 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
    }
    {
        void * t10 = gm_l0;
        void * t11 = (void *)(((uint8_t *)t10) + 2008);
        gm_tl874t2atnlh3 t12 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)a1) + 16));
        gm_t88rcucg1b7uk t13 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)a1) + 28));
        gm_trlabqsjjp0ds t14;
        GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t14 + 0), t12);
        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t14 + 12), t13);
        uint32_t t15 = gm_l2;
        gm_tftb1odc739t5 t16 = gm_f_put_qrf6f79(t11, t14, t15);
        gm_tftb1odc739t5 t17 = t16;
        if (((((uint8_t *)&t17))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t17) + 1, gm_srclv0dcbbfhves, 617, 5);
        gm_b0_1 t18 = GM_LD(gm_b0_1, (((uint8_t *)&t17) + 1));
    }
    {
        uint32_t t19 = gm_l2;
        uint8_t t20 = 0;
        F.ret = t19;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_adt_field_tys_oggq5ji(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_t9qkh0rsooc8d a2, void * a3) {
    struct gm_fr_adt_field_tys_oggq5ji F;
    void * gm_l0; /* s */
    uint32_t gm_l1; /* id */
    gm_t9qkh0rsooc8d gm_l2; /* vis */
    void * gm_l3; /* tys */
    gm_b16_8 gm_l5; /* fields */
    gm_b16_8 gm_l6; /* variants */
    gm_b16_8 gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    gm_t88rcucg1b7uk gm_l10; /* p */
    GM_STACK_CHECK(&F, 489u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        uint32_t t2 = gm_l1;
        uint64_t t3 = (uint64_t)t2;
        uint64_t t4 = (uintptr_t)t3;
        void * t5 = gm_l0;
        uint64_t t6 = GM_LD(uintptr_t, ((((uint8_t *)t5) + 1088) + 0) + 8);
        if (t4 >= (uintptr_t)(t6)) gm_trap_index(t4, t6, gm_srclv0dcbbfhves, 1257, 11);
        uint8_t t12 = GM_LD(uint8_t, ((((uint8_t *)GM_LD(void *, ((((uint8_t *)t5) + 1088) + 0)) + t4 * 184) + 24) + 0));
        switch (t12) { case ((uint8_t)0ULL): goto ma8; case ((uint8_t)1ULL): goto ma10; case ((uint8_t)2ULL): goto ma9; default: goto ma11; }
ma8:;
        uint8_t t14 = GM_LD(uint8_t, ((((uint8_t *)GM_LD(void *, ((((uint8_t *)t5) + 1088) + 0)) + t4 * 184) + 24) + 0));
        if (t14 != ((uint8_t)0ULL)) goto mn13;
        gm_tpautr4br7h3l t15 = GM_LD(gm_tpautr4br7h3l, ((((uint8_t *)GM_LD(void *, ((((uint8_t *)t5) + 1088) + 0)) + t4 * 184) + 24) + 8));
        F.l4 = t15;
        gm_b16_8 t16 = GM_LD(gm_b16_8, (((uint8_t *)&F.l4) + 0));
        gm_b16_8 t17 = t16;
        gm_b16_8 t18 = GM_LD(gm_b16_8, ((uint8_t *)&t17));
        gm_t9qkh0rsooc8d t19 = gm_l2;
        void * t20 = gm_l3;
        gm_b0_1 t21 = gm_f_push_field_tys_v4ep4t3((void *)NULL, cx, NULL, t18, t19, t20);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me7;
mn13:;
ma9:;
        uint8_t t23 = GM_LD(uint8_t, ((((uint8_t *)GM_LD(void *, ((((uint8_t *)t5) + 1088) + 0)) + t4 * 184) + 24) + 0));
        if (t23 != ((uint8_t)2ULL)) goto mn22;
        gm_b16_8 t24 = GM_LD(gm_b16_8, ((((uint8_t *)GM_LD(void *, ((((uint8_t *)t5) + 1088) + 0)) + t4 * 184) + 24) + 8));
        gm_l5 = t24;
        gm_b16_8 t25 = gm_l5;
        gm_b16_8 t26 = t25;
        gm_b16_8 t27 = GM_LD(gm_b16_8, ((uint8_t *)&t26));
        gm_t9qkh0rsooc8d t28 = gm_l2;
        void * t29 = gm_l3;
        gm_b0_1 t30 = gm_f_push_field_tys_v4ep4t3((void *)NULL, cx, NULL, t27, t28, t29);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me7;
mn22:;
ma10:;
        uint8_t t32 = GM_LD(uint8_t, ((((uint8_t *)GM_LD(void *, ((((uint8_t *)t5) + 1088) + 0)) + t4 * 184) + 24) + 0));
        if (t32 != ((uint8_t)1ULL)) goto mn31;
        gm_b16_8 t33 = GM_LD(gm_b16_8, (((((uint8_t *)GM_LD(void *, ((((uint8_t *)t5) + 1088) + 0)) + t4 * 184) + 24) + 8) + 8));
        gm_l6 = t33;
        {
            gm_b16_8 t35 = gm_l6;
            gm_l7 = t35;
        }
        {
            gm_l8 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t37 = gm_l8;
                uint64_t t38 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                uint8_t t39 = (t37 < t38);
                if (!t39) goto lb36;
                {
                    uint64_t t42 = gm_l8;
                    uint64_t t43 = (uintptr_t)t42;
                    uint64_t t44 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                    gm_tckdobunaulee t47 = GM_LD(gm_tckdobunaulee, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l7)) + t43 * 72));
                    F.l9 = t47;
                    {
                        if (!(((((uint8_t *)&F.l9) + 16))[4] != 0)) goto ie49;
                        gm_t88rcucg1b7uk t50 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l9) + 16));
                        gm_l10 = t50;
                        {
                            void * t52 = gm_l3;
                            gm_t88rcucg1b7uk t53 = gm_l10;
                            gm_t9qkh0rsooc8d t54 = gm_l2;
                            gm_t3jsf43p1pths t55;
                            GM_ST(uint8_t, (uint8_t *)&t55 + 5, 0);
                            GM_ST(uint16_t, (uint8_t *)&t55 + 6, 0);
                            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t55 + 0), t53);
                            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t55 + 4), t54);
                            gm_tftb1odc739t5 t56 = gm_f_push_h37silv(t52, t55);
                            gm_tftb1odc739t5 t57 = t56;
                            if (((((uint8_t *)&t57))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t57) + 1, gm_srclv0dcbbfhves, 1261, 33);
                            gm_b0_1 t58 = GM_LD(gm_b0_1, (((uint8_t *)&t57) + 1));
                        }
                        goto ix49;
ie49:;
ix49:;
                    }
                    goto me45;
mn46:;
                    gm_trap("no match arm matched the scrutinee", gm_srclv0dcbbfhves, 1260, 32);
me45:;
                }
lc36:;
                {
                    uint64_t t60 = gm_l8;
                    uint64_t r61; bool o62 = __builtin_add_overflow(t60, ((uint64_t)1ULL), &r61);
                    uint64_t t63 = r61;
                    gm_l8 = t63;
                }
            }
lb36:;
        }
        goto me7;
mn31:;
ma11:;
        gm_trap("no match arm matched the scrutinee", gm_srclv0dcbbfhves, 1257, 5);
me7:;
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
inline __attribute__((aligned(4))) void gm_f_aliased_decl_jv6ilmn(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2) {
    struct gm_fr_aliased_decl_jv6ilmn F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* module */
    void * gm_l2; /* e */
    gm_t08olq97rscn3 gm_l3; /* id */
    uint64_t gm_l4; /* d */
    void * gm_l5; /* inner */
    void * gm_l6; /* base */
    gm_t08olq97rscn3 gm_l7; /* name */
    uint64_t gm_l8; /* m */
    GM_STACK_CHECK(&F, 609u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l2;
        uint8_t t9 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        switch (t9) { case ((uint8_t)0ULL): goto ma7; case ((uint8_t)1ULL): goto ma7; case ((uint8_t)2ULL): goto ma7; case ((uint8_t)3ULL): goto ma7; case ((uint8_t)4ULL): goto ma7; case ((uint8_t)5ULL): goto ma7; case ((uint8_t)6ULL): goto ma4; case ((uint8_t)7ULL): goto ma7; case ((uint8_t)8ULL): goto ma7; case ((uint8_t)9ULL): goto ma5; case ((uint8_t)10ULL): goto ma7; case ((uint8_t)11ULL): goto ma7; case ((uint8_t)12ULL): goto ma7; case ((uint8_t)13ULL): goto ma7; case ((uint8_t)14ULL): goto ma7; case ((uint8_t)15ULL): goto ma7; case ((uint8_t)16ULL): goto ma7; case ((uint8_t)17ULL): goto ma7; case ((uint8_t)18ULL): goto ma7; case ((uint8_t)19ULL): goto ma7; case ((uint8_t)20ULL): goto ma7; case ((uint8_t)21ULL): goto ma7; case ((uint8_t)22ULL): goto ma6; case ((uint8_t)23ULL): goto ma7; case ((uint8_t)24ULL): goto ma7; case ((uint8_t)25ULL): goto ma7; case ((uint8_t)26ULL): goto ma7; case ((uint8_t)27ULL): goto ma7; case ((uint8_t)28ULL): goto ma7; case ((uint8_t)29ULL): goto ma7; case ((uint8_t)30ULL): goto ma7; case ((uint8_t)31ULL): goto ma7; case ((uint8_t)32ULL): goto ma7; case ((uint8_t)33ULL): goto ma7; case ((uint8_t)34ULL): goto ma7; case ((uint8_t)35ULL): goto ma7; case ((uint8_t)36ULL): goto ma7; case ((uint8_t)37ULL): goto ma7; case ((uint8_t)38ULL): goto ma7; case ((uint8_t)39ULL): goto ma7; case ((uint8_t)40ULL): goto ma7; case ((uint8_t)41ULL): goto ma7; case ((uint8_t)42ULL): goto ma7; case ((uint8_t)43ULL): goto ma7; case ((uint8_t)44ULL): goto ma7; case ((uint8_t)45ULL): goto ma7; case ((uint8_t)46ULL): goto ma7; case ((uint8_t)47ULL): goto ma7; case ((uint8_t)48ULL): goto ma7; case ((uint8_t)49ULL): goto ma7; case ((uint8_t)50ULL): goto ma7; default: goto ma7; }
ma4:;
        uint8_t t11 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t11 != ((uint8_t)6ULL)) goto mn10;
        gm_t08olq97rscn3 t12 = GM_LD(gm_t08olq97rscn3, ((((uint8_t *)t2) + 8) + 8));
        gm_l3 = t12;
        {
            void * t14 = gm_l0;
            uint64_t t15 = gm_l1;
            gm_b16_8 t16 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_tf5srh4mp4b3i t17 = gm_f_lookup_module_name_cukfp7t((void *)NULL, cx, NULL, t14, t15, t16);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tf5srh4mp4b3i t18 = t17;
            uint64_t t19;
            if (((((uint8_t *)&t18))[8] != 0)) {
                uint64_t t20 = GM_LD(uint64_t, (((uint8_t *)&t18) + 0));
                t19 = t20;
            } else {
                gm_t89h15e2c5o3p t21;
                memset(&t21, 0, sizeof t21);
                uint8_t t22 = 0;
                F.ret = t21;
                goto gm_fret;
            }
            gm_l4 = t19;
        }
        {
            uint64_t t23 = gm_l4;
            gm_tf5srh4mp4b3i t24;
            GM_ST(uint8_t, (uint8_t *)&t24 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t24 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t24 + 12, 0);
            GM_ST(uint64_t, ((uint8_t *)&t24), t23);
            ((uint8_t *)&t24)[8] = 1;
            gm_tba9024q2up98 t25;
            GM_ST(gm_tf5srh4mp4b3i, ((uint8_t *)&t25 + 0), t24);
            gm_t89h15e2c5o3p t26;
            GM_ST(uint8_t, (uint8_t *)&t26 + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&t26 + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&t26 + 20, 0);
            GM_ST(gm_tba9024q2up98, ((uint8_t *)&t26), t25);
            ((uint8_t *)&t26)[16] = 1;
            uint8_t t27 = 0;
            F.ret = t26;
            goto gm_fret;
        }
        goto me3;
mn10:;
ma5:;
        uint8_t t29 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t29 != ((uint8_t)9ULL)) goto mn28;
        void * t30 = GM_LD(void *, ((((uint8_t *)t2) + 8) + 8));
        gm_l5 = t30;
        void * t31 = gm_l0;
        uint64_t t32 = gm_l1;
        void * t33 = gm_l5;
        gm_f_aliased_decl_jv6ilmn((void *)&(*(gm_t89h15e2c5o3p *)gm_out), (void *)NULL, cx, NULL, t31, t32, t33);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto gm_fdone;
        goto me3;
mn28:;
ma6:;
        uint8_t t35 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t35 != ((uint8_t)22ULL)) goto mn34;
        void * t36 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 0));
        gm_l6 = t36;
        gm_t08olq97rscn3 t37 = GM_LD(gm_t08olq97rscn3, (((((uint8_t *)t2) + 8) + 8) + 8));
        gm_l7 = t37;
        {
            void * t39 = gm_l0;
            uint64_t t40 = gm_l1;
            void * t41 = gm_l6;
            gm_tf5srh4mp4b3i t42 = gm_f_module_named_n1mcl3b((void *)NULL, cx, NULL, t39, t40, t41);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tf5srh4mp4b3i t43 = t42;
            uint64_t t44;
            if (((((uint8_t *)&t43))[8] != 0)) {
                uint64_t t45 = GM_LD(uint64_t, (((uint8_t *)&t43) + 0));
                t44 = t45;
            } else {
                gm_t89h15e2c5o3p t46;
                memset(&t46, 0, sizeof t46);
                uint8_t t47 = 0;
                F.ret = t46;
                goto gm_fret;
            }
            gm_l8 = t44;
        }
        {
            void * t48 = gm_l0;
            uint64_t t49 = gm_l8;
            uint64_t t50 = gm_l1;
            gm_tf5srh4mp4b3i t51 = gm_f_module_member_5fd2qm9((void *)NULL, cx, NULL, t48, t49, &(*(gm_t08olq97rscn3 *)((uint8_t *)&gm_l7)), t50);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tba9024q2up98 t52;
            GM_ST(gm_tf5srh4mp4b3i, ((uint8_t *)&t52 + 0), t51);
            gm_t89h15e2c5o3p t53;
            GM_ST(uint8_t, (uint8_t *)&t53 + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&t53 + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&t53 + 20, 0);
            GM_ST(gm_tba9024q2up98, ((uint8_t *)&t53), t52);
            ((uint8_t *)&t53)[16] = 1;
            uint8_t t54 = 0;
            F.ret = t53;
            goto gm_fret;
        }
        goto me3;
mn34:;
ma7:;
        gm_t89h15e2c5o3p t56;
        memset(&t56, 0, sizeof t56);
        uint8_t t57 = 0;
        F.ret = t56;
        goto gm_fret;
        goto me3;
mn55:;
ma8:;
        gm_trap("no match arm matched the scrutinee", gm_srclv0dcbbfhves, 1102, 5);
me3:;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_check_allow_attr_3321gm7(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_check_allow_attr_3321gm7 F;
    void * gm_l0; /* s */
    void * gm_l1; /* attr */
    gm_b16_8 gm_l2; /* args */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    GM_STACK_CHECK(&F, 412u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l1;
        gm_th2lqbo77pvsc t3 = GM_LD(gm_th2lqbo77pvsc, (((uint8_t *)t2) + 32));
        gm_th2lqbo77pvsc t4 = t3;
        gm_b16_8 t5;
        if (((((uint8_t *)&t4))[16] != 0)) {
            gm_b16_8 t6 = GM_LD(gm_b16_8, (((uint8_t *)&t4) + 0));
            t5 = t6;
        } else {
            gm_b16_8 t7 = gm_f_empty_slice_g3tddrb();
            t5 = t7;
        }
        gm_l2 = t5;
    }
    {
        uint64_t t9 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        uint8_t t10 = ((t9 == ((uint64_t)0ULL)));
        if (!t10) goto ie8;
        {
            void * t12 = gm_l0;
            void * t13 = gm_l1;
            gm_tl874t2atnlh3 t14 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t13) + 56));
            gm_b16_8 t15;
            GM_ST(void *, (uint8_t *)&t15, (void *)(gm_s8n5rdam5o1sm));
            GM_ST(uintptr_t, (uint8_t *)&t15 + 8, (uintptr_t)(68));
            gm_b0_1 t16 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t12, t14, t15);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix8;
ie8:;
ix8:;
    }
    {
        {
            gm_b16_8 t18 = gm_l2;
            gm_l3 = t18;
        }
        {
            gm_l4 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t20 = gm_l4;
                uint64_t t21 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                uint8_t t22 = (t20 < t21);
                if (!t22) goto lb19;
                {
                    uint64_t t25 = gm_l4;
                    uint64_t t26 = (uintptr_t)t25;
                    uint64_t t27 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                    gm_tc3oht4ksth9d t30 = GM_LD(gm_tc3oht4ksth9d, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t26 * 88));
                    F.l5 = t30;
                    {
                        uint8_t t33;
                        uint8_t t36 = GM_LD(uint8_t, ((((uint8_t *)&F.l5) + 8) + 0));
                        if (t36 != ((uint8_t)2ULL)) goto mn35;
                        t33 = ((uint8_t)0);
                        goto me34;
mn35:;
                        t33 = ((uint8_t)1);
                        goto me34;
mn37:;
                        gm_trap("no match arm matched the scrutinee", gm_srclv0dcbbfhves, 973, 12);
me34:;
                        if (!t33) goto ie32;
                        {
                            void * t39 = gm_l0;
                            gm_tl874t2atnlh3 t40 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l5) + 72));
                            gm_b16_8 t41;
                            GM_ST(void *, (uint8_t *)&t41, (void *)(gm_st6dd0k5nckh3));
                            GM_ST(uintptr_t, (uint8_t *)&t41 + 8, (uintptr_t)(57));
                            gm_b0_1 t42 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t39, t40, t41);
                            if (gm_unw.kind) {
                                goto gm_ll23;
                            }
                        }
                        goto ix32;
ie32:;
ix32:;
                    }
                    goto me28;
mn29:;
                    gm_trap("no match arm matched the scrutinee", gm_srclv0dcbbfhves, 972, 5);
me28:;
                }
lc19:;
                {
                    uint64_t t44 = gm_l4;
                    uint64_t r45; bool o46 = __builtin_add_overflow(t44, ((uint64_t)1ULL), &r45);
                    uint64_t t47 = r45;
                    gm_l4 = t47;
                }
            }
gm_ll23:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb19; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc19; }
            }
            goto gm_lad;
lb19:;
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
__attribute__((aligned(4))) gm_b0_1 gm_f_check_method_candidates_k08rvu6(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_check_method_candidates_k08rvu6 F;
    void * gm_l0; /* s */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    gm_tl874t2atnlh3 gm_l5; /* f */
    gm_tl874t2atnlh3 gm_l6; /* at */
    gm_tl874t2atnlh3 gm_l7; /* other */
    GM_STACK_CHECK(&F, 734u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_hash_map_gsp2k0o((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
    }
    {
        {
            void * t4 = gm_l0;
            gm_b16_8 t5 = GM_LD(gm_b16_8, ((((uint8_t *)t4) + 1872) + 0));
            gm_l2 = t5;
        }
        {
            gm_l3 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t7 = gm_l3;
                uint64_t t8 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                uint8_t t9 = (t7 < t8);
                if (!t9) goto lb6;
                {
                    uint64_t t12 = gm_l3;
                    uint64_t t13 = (uintptr_t)t12;
                    uint64_t t14 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                    gm_t79rbdv4rafe5 t17 = GM_LD(gm_t79rbdv4rafe5, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t13 * 40));
                    F.l4 = t17;
                    {
                        void * t19 = (void *)((uint8_t *)&F.l1);
                        gm_tiilmfd16v3fc t20;
                        memset((uint8_t *)&t20 + 4, 0, 4);
                        uint32_t t21 = GM_LD(uint32_t, (((uint8_t *)&F.l4) + 0));
                        GM_ST(uint32_t, (uint8_t *)&t20 + 0, t21);
                        gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)&F.l4) + 8));
                        GM_ST(gm_b16_8, (uint8_t *)&t20 + 8, t22);
                        gm_toceffb697p0u t23 = gm_f_get_qjsv97t(t19, &t20);
                        gm_toceffb697p0u t24 = t23;
                        gm_tl874t2atnlh3 t25;
                        if (((((uint8_t *)&t24))[12] != 0)) {
                            gm_tl874t2atnlh3 t26 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&t24) + 0));
                            t25 = t26;
                        } else {
                            {
                                void * t28 = (void *)((uint8_t *)&F.l1);
                                gm_tiilmfd16v3fc t29;
                                memset((uint8_t *)&t29 + 4, 0, 4);
                                uint32_t t30 = GM_LD(uint32_t, (((uint8_t *)&F.l4) + 0));
                                GM_ST(uint32_t, (uint8_t *)&t29 + 0, t30);
                                gm_b16_8 t31 = GM_LD(gm_b16_8, (((uint8_t *)&F.l4) + 8));
                                GM_ST(gm_b16_8, (uint8_t *)&t29 + 8, t31);
                                gm_tl874t2atnlh3 t32 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l4) + 24));
                                gm_tftb1odc739t5 t33 = gm_f_put_pjhbiaq(t28, &t29, GM_CONV(gm_cseahvg8v7p0vs, t32));
                                gm_tftb1odc739t5 t34 = t33;
                                if (((((uint8_t *)&t34))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t34) + 1, gm_srclv0dcbbfhves, 1169, 13);
                                gm_b0_1 t35 = GM_LD(gm_b0_1, (((uint8_t *)&t34) + 1));
                            }
                            goto lc6;
                        }
                        gm_l5 = t25;
                    }
                    {
                        gm_tl874t2atnlh3 t36 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l4) + 24));
                        gm_l6 = t36;
                    }
                    {
                        gm_tl874t2atnlh3 t37 = gm_l5;
                        gm_l7 = t37;
                    }
                    {
                        uint8_t t39 = 0;
                        uint32_t t40 = GM_LD(uint32_t, ((((uint8_t *)&F.l4) + 24) + 0));
                        uint32_t t41 = GM_LD(uint32_t, (((uint8_t *)&gm_l5) + 0));
                        uint8_t t42 = (t40 < t41);
                        if (!t42) {
                            uint8_t t43 = 0;
                            uint32_t t44 = GM_LD(uint32_t, ((((uint8_t *)&F.l4) + 24) + 0));
                            uint32_t t45 = GM_LD(uint32_t, (((uint8_t *)&gm_l5) + 0));
                            uint8_t t46 = ((t44 == t45));
                            if (t46) {
                                uint32_t t47 = GM_LD(uint32_t, ((((uint8_t *)&F.l4) + 24) + 4));
                                uint32_t t48 = GM_LD(uint32_t, (((uint8_t *)&gm_l5) + 4));
                                uint8_t t49 = (t47 < t48);
                                t43 = t49;
                            } else t43 = 0;
                            t39 = t43;
                        } else t39 = 1;
                        if (!t39) goto ie38;
                        {
                            gm_tl874t2atnlh3 t51 = gm_l5;
                            gm_l6 = t51;
                        }
                        {
                            gm_tl874t2atnlh3 t52 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l4) + 24));
                            gm_l7 = t52;
                        }
                        goto ix38;
ie38:;
ix38:;
                    }
                    {
                        gm_tl874t2atnlh3 t53 = gm_l6;
                        gm_t6j6smj7qoctu t54;
                        uint32_t t55 = GM_LD(uint32_t, (((uint8_t *)&F.l4) + 0));
                        uint64_t t56 = (uint64_t)t55;
                        uint64_t t57 = (uintptr_t)t56;
                        void * t58 = gm_l0;
                        uint64_t t59 = GM_LD(uintptr_t, ((((uint8_t *)t58) + 1088) + 0) + 8);
                        if (t57 >= (uintptr_t)(t59)) gm_trap_index(t57, t59, gm_srclv0dcbbfhves, 1179, 127);
                        gm_b16_8 t60 = GM_LD(gm_b16_8, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t58) + 1088) + 0)) + t57 * 184) + 0));
                        GM_ST(gm_b16_8, (uint8_t *)&t54 + 0, t60);
                        gm_b16_8 t61 = GM_LD(gm_b16_8, (((uint8_t *)&F.l4) + 8));
                        GM_ST(gm_b16_8, (uint8_t *)&t54 + 16, t61);
                        gm_tabb710d9mq2e t62;
                        gm_f_format_ck7vger((void *)&t62, (void *)NULL, cx, NULL, &t54);
                        if (gm_unw.kind) {
                            goto gm_ll10;
                        }
                        gm_tabb710d9mq2e t63 = t62;
                        if (((((uint8_t *)&t63))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t63) + 8, gm_srclv0dcbbfhves, 1179, 29);
                        gm_b16_8 t64 = GM_LD(gm_b16_8, (((uint8_t *)&t63) + 8));
                        gm_b16_8 t65 = t64;
                        gm_b16_8 t66 = GM_LD(gm_b16_8, ((uint8_t *)&t65));
                        gm_f_error_s6hn871((void *)&F.l8, (void *)NULL, cx, NULL, t53, t66);
                        if (gm_unw.kind) {
                            goto gm_ll10;
                        }
                    }
                    {
                        void * t67 = (void *)((uint8_t *)&F.l8);
                        gm_tl874t2atnlh3 t68 = gm_l7;
                        gm_b16_8 t69;
                        GM_ST(void *, (uint8_t *)&t69, (void *)(gm_srjo6vku5gka5));
                        GM_ST(uintptr_t, (uint8_t *)&t69 + 8, (uintptr_t)(19));
                        gm_f_add_label_50fj7vv(t67, GM_CONV(gm_cseahvg8v7p0vs, t68), t69);
                    }
                    {
                        void * t70 = gm_l0;
                        gm_b0_1 t71 = gm_f_emit_dph5v1e((void *)NULL, cx, NULL, t70, &(*(gm_t4gut2c9fj1kt *)((uint8_t *)&F.l8)));
                        if (gm_unw.kind) {
                            goto gm_ll10;
                        }
                    }
                    goto me15;
mn16:;
                    gm_trap("no match arm matched the scrutinee", gm_srclv0dcbbfhves, 1167, 5);
me15:;
                }
lc6:;
                {
                    uint64_t t73 = gm_l3;
                    uint64_t r74; bool o75 = __builtin_add_overflow(t73, ((uint64_t)1ULL), &r74);
                    uint64_t t76 = r74;
                    gm_l3 = t76;
                }
            }
gm_ll10:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb6; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc6; }
            }
            goto gm_ld1_1;
lb6:;
        }
    }
    {
        void * t77 = gm_l0;
        void * t78 = (void *)(((uint8_t *)t77) + 1872);
        gm_f_clear_ufjejrf(t78);
    }
    {
        void * t79 = (void *)((uint8_t *)&F.l1);
        gm_f_deinit_2l7c12h(t79);
    }
    goto gm_ls80;
gm_ld1_1:;
    {
        gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
        void * t81 = (void *)((uint8_t *)&F.l1);
        gm_f_deinit_2l7c12h(t81);
        gm_unw = gm_saved;
    }
    goto gm_lad;
gm_ls80:;
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
__attribute__((aligned(4))) gm_b0_1 gm_f_check_pub_signatures_plkds6r(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_check_pub_signatures_plkds6r F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* <tmp> */
    uint64_t gm_l2; /* <tmp> */
    uint64_t gm_l3; /* i */
    void * gm_l4; /* d */
    gm_t9qkh0rsooc8d gm_l5; /* vis */
    uint32_t gm_l8; /* f */
    void * gm_l9; /* sig */
    gm_b16_8 gm_l10; /* <tmp> */
    uint64_t gm_l11; /* <tmp> */
    uint32_t gm_l13; /* e */
    gm_b16_8 gm_l14; /* <tmp> */
    uint64_t gm_l15; /* <tmp> */
    gm_b16_8 gm_l17; /* <tmp> */
    uint64_t gm_l18; /* <tmp> */
    gm_tiilmfd16v3fc gm_l20; /* r */
    gm_tfe2ufaqq8re9 gm_l21; /* <temporary array> */
    gm_tnpifv20irnph gm_l22; /* set */
    gm_t88rcucg1b7uk gm_l23; /* t */
    uint32_t gm_l24; /* id */
    uint32_t gm_l25; /* id */
    gm_t88rcucg1b7uk gm_l26; /* t */
    gm_t88rcucg1b7uk gm_l27; /* t */
    gm_b16_8 gm_l28; /* <tmp> */
    uint64_t gm_l29; /* <tmp> */
    gm_t3jsf43p1pths gm_l30; /* m */
    gm_t3jsf43p1pths gm_l31; /* h */
    gm_b16_8 gm_l32; /* what */
    gm_b16_8 gm_l33; /* kind */
    GM_STACK_CHECK(&F, 2489u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        {
            gm_l2 = ((uint64_t)0ULL);
        }
        {
            void * t3 = gm_l0;
            void * t4 = GM_LD(void *, (((uint8_t *)t3) + 0));
            uint64_t t5 = GM_LD(uintptr_t, ((((uint8_t *)t4) + 240) + 0) + 8);
            gm_l1 = t5;
        }
        {
            for (;;) {
                uint64_t t7 = gm_l2;
                uint64_t t8 = gm_l1;
                uint8_t t9 = (t7 < t8);
                if (!t9) goto lb6;
                {
                    uint64_t t12 = gm_l2;
                    uint64_t t13 = t12;
                    uint64_t t16 = GM_LD(uint64_t, ((uint8_t *)&t13));
                    gm_l3 = t16;
                    {
                        uint64_t t18 = gm_l3;
                        uint64_t t19 = (uintptr_t)t18;
                        void * t20 = gm_l0;
                        void * t21 = GM_LD(void *, (((uint8_t *)t20) + 0));
                        uint64_t t22 = GM_LD(uintptr_t, ((((uint8_t *)t21) + 240) + 0) + 8);
                        if (t19 >= (uintptr_t)(t22)) gm_trap_index(t19, t22, gm_srclv0dcbbfhves, 1190, 15);
                        void * t23 = (void *)((uint8_t *)GM_LD(void *, ((((uint8_t *)t21) + 240) + 0)) + t19 * 216);
                        gm_l4 = t23;
                    }
                    {
                        void * t24 = gm_l4;
                        gm_t9qkh0rsooc8d t25 = GM_LD(gm_t9qkh0rsooc8d, ((((uint8_t *)t24) + 40) + 56));
                        gm_l5 = t25;
                    }
                    {
                        uint8_t t27 = 0;
                        uint8_t t28 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l5) + 0) == ((uint8_t)0ULL)));
                        if (!t28) {
                            void * t29 = gm_l4;
                            uint8_t t30;
                            uint8_t t33 = GM_LD(uint8_t, (((((uint8_t *)t29) + 40) + 64) + 0));
                            if (t33 != ((uint8_t)0ULL)) goto mn32;
                            t30 = ((uint8_t)0);
                            goto me31;
mn32:;
                            t30 = ((uint8_t)1);
                            goto me31;
mn34:;
                            gm_trap("no match arm matched the scrutinee", gm_srclv0dcbbfhves, 1192, 31);
me31:;
                            t27 = t30;
                        } else t27 = 1;
                        if (!t27) goto ie26;
                        {
                            goto lc6;
                        }
                        goto ix26;
ie26:;
ix26:;
                    }
                    {
                        uint64_t t37 = gm_l3;
                        void * t38 = gm_l0;
                        uint64_t t39 = GM_LD(uintptr_t, ((((uint8_t *)t38) + 304) + 0) + 8);
                        uint8_t t40 = (t37 >= t39);
                        if (!t40) goto ie36;
                        {
                            goto lc6;
                        }
                        goto ix36;
ie36:;
ix36:;
                    }
                    {
                        uint64_t t43 = gm_l3;
                        uint64_t t44 = (uintptr_t)t43;
                        void * t45 = gm_l0;
                        uint64_t t46 = GM_LD(uintptr_t, ((((uint8_t *)t45) + 304) + 0) + 8);
                        if (t44 >= (uintptr_t)(t46)) gm_trap_index(t44, t46, gm_srclv0dcbbfhves, 1194, 21);
                        uint8_t t47 = GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t45) + 304) + 0)) + t44 * 216) + 0));
                        if (t47 != ((uint8_t)2ULL)) goto ie42;
                        gm_t6qooom8qa2do t48 = GM_LD(gm_t6qooom8qa2do, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t45) + 304) + 0)) + t44 * 216) + 8));
                        F.l6 = t48;
                        {
                            gm_f_list_fam8ktt((void *)&F.l7, (void *)NULL, cx, NULL);
                            if (gm_unw.kind) {
                                goto gm_ll10;
                            }
                        }
                        {
                        }
                        {
                            uint8_t t60 = GM_LD(uint8_t, (((uint8_t *)&F.l6) + 0));
                            switch (t60) { case ((uint8_t)0ULL): goto ma55; case ((uint8_t)1ULL): goto ma56; case ((uint8_t)2ULL): goto ma52; case ((uint8_t)3ULL): goto ma53; case ((uint8_t)4ULL): goto ma54; case ((uint8_t)5ULL): goto ma51; case ((uint8_t)6ULL): goto ma58; case ((uint8_t)7ULL): goto ma57; case ((uint8_t)8ULL): goto ma58; case ((uint8_t)9ULL): goto ma58; case ((uint8_t)10ULL): goto ma57; case ((uint8_t)11ULL): goto ma58; default: goto ma58; }
ma51:;
                            uint8_t t62 = GM_LD(uint8_t, (((uint8_t *)&F.l6) + 0));
                            if (t62 != ((uint8_t)5ULL)) goto mn61;
                            uint32_t t63 = GM_LD(uint32_t, (((uint8_t *)&F.l6) + 8));
                            gm_l8 = t63;
                            {
                                uint32_t t65 = gm_l8;
                                uint64_t t66 = (uint64_t)t65;
                                uint64_t t67 = (uintptr_t)t66;
                                void * t68 = gm_l0;
                                uint64_t t69 = GM_LD(uintptr_t, ((((uint8_t *)t68) + 344) + 0) + 8);
                                if (t67 >= (uintptr_t)(t69)) gm_trap_index(t67, t69, gm_srclv0dcbbfhves, 1199, 25);
                                void * t70 = (void *)((uint8_t *)GM_LD(void *, ((((uint8_t *)t68) + 344) + 0)) + t67 * 136);
                                gm_l9 = t70;
                            }
                            {
                                {
                                    void * t72 = gm_l9;
                                    gm_b16_8 t73 = GM_LD(gm_b16_8, (((uint8_t *)t72) + 40));
                                    gm_l10 = t73;
                                }
                                {
                                    gm_l11 = ((uint64_t)0ULL);
                                }
                                {
                                    for (;;) {
                                        uint64_t t75 = gm_l11;
                                        uint64_t t76 = GM_LD(uintptr_t, ((uint8_t *)&gm_l10) + 8);
                                        uint8_t t77 = (t75 < t76);
                                        if (!t77) goto lb74;
                                        {
                                            uint64_t t80 = gm_l11;
                                            uint64_t t81 = (uintptr_t)t80;
                                            uint64_t t82 = GM_LD(uintptr_t, ((uint8_t *)&gm_l10) + 8);
                                            gm_t5v0c8ia6a5mo t85 = GM_LD(gm_t5v0c8ia6a5mo, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l10)) + t81 * 56));
                                            F.l12 = t85;
                                            {
                                                uint8_t t88;
                                                if ((((((uint8_t *)&F.l12) + 40))[4] != 0)) goto mn90;
                                                t88 = ((uint8_t)1);
                                                goto me89;
mn90:;
                                                t88 = ((uint8_t)0);
                                                goto me89;
mn91:;
                                                gm_trap("no match arm matched the scrutinee", gm_srclv0dcbbfhves, 1201, 24);
me89:;
                                                if (!t88) goto ie87;
                                                {
                                                    void * t93 = (void *)((uint8_t *)&F.l7);
                                                    gm_t88rcucg1b7uk t94 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l12) + 16));
                                                    gm_t9qkh0rsooc8d t95 = gm_l5;
                                                    gm_t3jsf43p1pths t96;
                                                    GM_ST(uint8_t, (uint8_t *)&t96 + 5, 0);
                                                    GM_ST(uint16_t, (uint8_t *)&t96 + 6, 0);
                                                    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t96 + 0), t94);
                                                    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t96 + 4), t95);
                                                    gm_tftb1odc739t5 t97 = gm_f_push_h37silv(t93, t96);
                                                    gm_tftb1odc739t5 t98 = t97;
                                                    if (((((uint8_t *)&t98))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t98) + 1, gm_srclv0dcbbfhves, 1201, 44);
                                                    gm_b0_1 t99 = GM_LD(gm_b0_1, (((uint8_t *)&t98) + 1));
                                                }
                                                goto ix87;
ie87:;
ix87:;
                                            }
                                            goto me83;
mn84:;
                                            gm_trap("no match arm matched the scrutinee", gm_srclv0dcbbfhves, 1200, 17);
me83:;
                                        }
lc74:;
                                        {
                                            uint64_t t101 = gm_l11;
                                            uint64_t r102; bool o103 = __builtin_add_overflow(t101, ((uint64_t)1ULL), &r102);
                                            uint64_t t104 = r102;
                                            gm_l11 = t104;
                                        }
                                    }
lb74:;
                                }
                            }
                            {
                                void * t105 = (void *)((uint8_t *)&F.l7);
                                void * t106 = gm_l9;
                                gm_t88rcucg1b7uk t107 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t106) + 56));
                                gm_t9qkh0rsooc8d t108 = gm_l5;
                                gm_t3jsf43p1pths t109;
                                GM_ST(uint8_t, (uint8_t *)&t109 + 5, 0);
                                GM_ST(uint16_t, (uint8_t *)&t109 + 6, 0);
                                GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t109 + 0), t107);
                                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t109 + 4), t108);
                                gm_tftb1odc739t5 t110 = gm_f_push_h37silv(t105, t109);
                                gm_tftb1odc739t5 t111 = t110;
                                if (((((uint8_t *)&t111))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t111) + 1, gm_srclv0dcbbfhves, 1203, 17);
                                gm_b0_1 t112 = GM_LD(gm_b0_1, (((uint8_t *)&t111) + 1));
                            }
                            {
                                void * t113 = gm_l9;
                                gm_b16_8 t114 = GM_LD(gm_b16_8, ((((uint8_t *)t113) + 64) + 0));
                                gm_t9qkh0rsooc8d t115 = gm_l5;
                                void * t116 = (void *)((uint8_t *)&F.l7);
                                gm_b0_1 t117 = gm_f_effect_arg_tys_2a7r8hh((void *)NULL, cx, NULL, t114, t115, t116);
                                if (gm_unw.kind) {
                                    goto gm_ll10;
                                }
                            }
                            goto me50;
mn61:;
ma52:;
                            uint8_t t119 = GM_LD(uint8_t, (((uint8_t *)&F.l6) + 0));
                            if (t119 != ((uint8_t)2ULL)) goto mn118;
                            uint32_t t120 = GM_LD(uint32_t, (((uint8_t *)&F.l6) + 8));
                            gm_l13 = t120;
                            {
                                {
                                    uint32_t t123 = gm_l13;
                                    uint64_t t124 = (uint64_t)t123;
                                    uint64_t t125 = (uintptr_t)t124;
                                    void * t126 = gm_l0;
                                    uint64_t t127 = GM_LD(uintptr_t, ((((uint8_t *)t126) + 1048) + 0) + 8);
                                    if (t125 >= (uintptr_t)(t127)) gm_trap_index(t125, t127, gm_srclv0dcbbfhves, 1207, 27);
                                    gm_b16_8 t128 = GM_LD(gm_b16_8, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t126) + 1048) + 0)) + t125 * 56) + 40));
                                    gm_l14 = t128;
                                }
                                {
                                    gm_l15 = ((uint64_t)0ULL);
                                }
                                {
                                    for (;;) {
                                        uint64_t t130 = gm_l15;
                                        uint64_t t131 = GM_LD(uintptr_t, ((uint8_t *)&gm_l14) + 8);
                                        uint8_t t132 = (t130 < t131);
                                        if (!t132) goto lb129;
                                        {
                                            uint64_t t135 = gm_l15;
                                            uint64_t t136 = (uintptr_t)t135;
                                            uint64_t t137 = GM_LD(uintptr_t, ((uint8_t *)&gm_l14) + 8);
                                            gm_tv0k2vg3054a6 t140 = GM_LD(gm_tv0k2vg3054a6, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l14)) + t136 * 48));
                                            F.l16 = t140;
                                            {
                                                {
                                                    gm_b16_8 t143 = GM_LD(gm_b16_8, (((uint8_t *)&F.l16) + 24));
                                                    gm_l17 = t143;
                                                }
                                                {
                                                    gm_l18 = ((uint64_t)0ULL);
                                                }
                                                {
                                                    for (;;) {
                                                        uint64_t t145 = gm_l18;
                                                        uint64_t t146 = GM_LD(uintptr_t, ((uint8_t *)&gm_l17) + 8);
                                                        uint8_t t147 = (t145 < t146);
                                                        if (!t147) goto lb144;
                                                        {
                                                            uint64_t t150 = gm_l18;
                                                            uint64_t t151 = (uintptr_t)t150;
                                                            uint64_t t152 = GM_LD(uintptr_t, ((uint8_t *)&gm_l17) + 8);
                                                            gm_t5v0c8ia6a5mo t155 = GM_LD(gm_t5v0c8ia6a5mo, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l17)) + t151 * 56));
                                                            F.l19 = t155;
                                                            {
                                                                uint8_t t158;
                                                                if ((((((uint8_t *)&F.l19) + 40))[4] != 0)) goto mn160;
                                                                t158 = ((uint8_t)1);
                                                                goto me159;
mn160:;
                                                                t158 = ((uint8_t)0);
                                                                goto me159;
mn161:;
                                                                gm_trap("no match arm matched the scrutinee", gm_srclv0dcbbfhves, 1209, 28);
me159:;
                                                                if (!t158) goto ie157;
                                                                {
                                                                    void * t163 = (void *)((uint8_t *)&F.l7);
                                                                    gm_t88rcucg1b7uk t164 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l19) + 16));
                                                                    gm_t9qkh0rsooc8d t165 = gm_l5;
                                                                    gm_t3jsf43p1pths t166;
                                                                    GM_ST(uint8_t, (uint8_t *)&t166 + 5, 0);
                                                                    GM_ST(uint16_t, (uint8_t *)&t166 + 6, 0);
                                                                    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t166 + 0), t164);
                                                                    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t166 + 4), t165);
                                                                    gm_tftb1odc739t5 t167 = gm_f_push_h37silv(t163, t166);
                                                                    gm_tftb1odc739t5 t168 = t167;
                                                                    if (((((uint8_t *)&t168))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t168) + 1, gm_srclv0dcbbfhves, 1209, 48);
                                                                    gm_b0_1 t169 = GM_LD(gm_b0_1, (((uint8_t *)&t168) + 1));
                                                                }
                                                                goto ix157;
ie157:;
ix157:;
                                                            }
                                                            goto me153;
mn154:;
                                                            gm_trap("no match arm matched the scrutinee", gm_srclv0dcbbfhves, 1208, 21);
me153:;
                                                        }
lc144:;
                                                        {
                                                            uint64_t t171 = gm_l18;
                                                            uint64_t r172; bool o173 = __builtin_add_overflow(t171, ((uint64_t)1ULL), &r172);
                                                            uint64_t t174 = r172;
                                                            gm_l18 = t174;
                                                        }
                                                    }
lb144:;
                                                }
                                            }
                                            {
                                                void * t175 = (void *)((uint8_t *)&F.l7);
                                                gm_t88rcucg1b7uk t176 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l16) + 40));
                                                gm_t9qkh0rsooc8d t177 = gm_l5;
                                                gm_t3jsf43p1pths t178;
                                                GM_ST(uint8_t, (uint8_t *)&t178 + 5, 0);
                                                GM_ST(uint16_t, (uint8_t *)&t178 + 6, 0);
                                                GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t178 + 0), t176);
                                                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t178 + 4), t177);
                                                gm_tftb1odc739t5 t179 = gm_f_push_h37silv(t175, t178);
                                                gm_tftb1odc739t5 t180 = t179;
                                                if (((((uint8_t *)&t180))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t180) + 1, gm_srclv0dcbbfhves, 1211, 21);
                                                gm_b0_1 t181 = GM_LD(gm_b0_1, (((uint8_t *)&t180) + 1));
                                            }
                                            goto me138;
mn139:;
                                            gm_trap("no match arm matched the scrutinee", gm_srclv0dcbbfhves, 1207, 17);
me138:;
                                        }
lc129:;
                                        {
                                            uint64_t t183 = gm_l15;
                                            uint64_t r184; bool o185 = __builtin_add_overflow(t183, ((uint64_t)1ULL), &r184);
                                            uint64_t t186 = r184;
                                            gm_l15 = t186;
                                        }
                                    }
lb129:;
                                }
                            }
                            goto me50;
mn118:;
ma53:;
                            uint8_t t188 = GM_LD(uint8_t, (((uint8_t *)&F.l6) + 0));
                            if (t188 != ((uint8_t)3ULL)) goto mn187;
                            gm_tiilmfd16v3fc t189 = GM_LD(gm_tiilmfd16v3fc, (((uint8_t *)&F.l6) + 8));
                            gm_l20 = t189;
                            {
                                gm_tiilmfd16v3fc t191 = gm_l20;
                                GM_ST(gm_tiilmfd16v3fc, (uint8_t *)&(*(gm_tfe2ufaqq8re9 *)((uint8_t *)&gm_l21)) + 0, t191);
                            }
                            void * t192 = ((uint8_t *)&gm_l21);
                            if ((uintptr_t)0 > (uintptr_t)1 || (uintptr_t)1 > (uintptr_t)1) gm_trap_slice((uintptr_t)0, (uintptr_t)1, 1, gm_srclv0dcbbfhves, 1214, 46);
                            gm_b16_8 t193;
                            GM_ST(void *, (uint8_t *)&t193, (void *)(((uint8_t *)t192 + (uintptr_t)0 * 24)));
                            GM_ST(uintptr_t, (uint8_t *)&t193 + 8, (uintptr_t)((uintptr_t)1 - (uintptr_t)0));
                            gm_b16_8 t194 = t193;
                            gm_t9qkh0rsooc8d t195 = gm_l5;
                            void * t196 = (void *)((uint8_t *)&F.l7);
                            gm_b0_1 t197 = gm_f_effect_arg_tys_2a7r8hh((void *)NULL, cx, NULL, t194, t195, t196);
                            if (gm_unw.kind) {
                                goto gm_ll10;
                            }
                            goto me50;
mn187:;
ma54:;
                            uint8_t t199 = GM_LD(uint8_t, (((uint8_t *)&F.l6) + 0));
                            if (t199 != ((uint8_t)4ULL)) goto mn198;
                            gm_tnpifv20irnph t200 = GM_LD(gm_tnpifv20irnph, (((uint8_t *)&F.l6) + 8));
                            gm_l22 = t200;
                            gm_b16_8 t201 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l22) + 0));
                            gm_t9qkh0rsooc8d t202 = gm_l5;
                            void * t203 = (void *)((uint8_t *)&F.l7);
                            gm_b0_1 t204 = gm_f_effect_arg_tys_2a7r8hh((void *)NULL, cx, NULL, t201, t202, t203);
                            if (gm_unw.kind) {
                                goto gm_ll10;
                            }
                            goto me50;
mn198:;
ma55:;
                            uint8_t t206 = GM_LD(uint8_t, (((uint8_t *)&F.l6) + 0));
                            if (t206 != ((uint8_t)0ULL)) goto mn205;
                            gm_t88rcucg1b7uk t207 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l6) + 8));
                            gm_l23 = t207;
                            {
                                void * t210 = gm_l0;
                                void * t211 = (void *)(((uint8_t *)t210) + 8);
                                gm_t88rcucg1b7uk t212 = gm_l23;
                                gm_t53at80pfdus3 t213;
                                gm_f_kind_jcnt4hm((void *)&t213, t211, GM_CONV(gm_csguankko4lmhd, t212));
                                gm_t53at80pfdus3 t214 = t213;
                                uint8_t t215 = GM_LD(uint8_t, (((uint8_t *)&t214) + 0));
                                if (t215 != ((uint8_t)17ULL)) goto ie209;
                                uint32_t t216 = GM_LD(uint32_t, ((((uint8_t *)&t214) + 8) + 0));
                                gm_l24 = t216;
                                uint32_t t217 = gm_l24;
                                uint64_t t218 = (uint64_t)t217;
                                uint64_t t219 = (uintptr_t)t218;
                                void * t220 = gm_l0;
                                uint64_t t221 = GM_LD(uintptr_t, ((((uint8_t *)t220) + 1088) + 0) + 8);
                                if (t219 >= (uintptr_t)(t221)) gm_trap_index(t219, t221, gm_srclv0dcbbfhves, 1217, 53);
                                gm_tf5srh4mp4b3i t222 = GM_LD(gm_tf5srh4mp4b3i, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t220) + 1088) + 0)) + t219 * 184) + 168));
                                uint64_t t223 = gm_l3;
                                gm_tf5srh4mp4b3i t224;
                                GM_ST(uint8_t, (uint8_t *)&t224 + 9, 0);
                                GM_ST(uint16_t, (uint8_t *)&t224 + 10, 0);
                                GM_ST(uint32_t, (uint8_t *)&t224 + 12, 0);
                                GM_ST(uint64_t, ((uint8_t *)&t224), t223);
                                ((uint8_t *)&t224)[8] = 1;
                                gm_tf5srh4mp4b3i t225 = t222;
                                gm_tf5srh4mp4b3i t226 = t224;
                                uint8_t t227 = (gm_eq7mri15hk78(((uint8_t *)&t225), ((uint8_t *)&t226)));
                                if (!t227) goto ie209;
                                {
                                    void * t229 = gm_l0;
                                    uint32_t t230 = gm_l24;
                                    gm_t9qkh0rsooc8d t231 = gm_l5;
                                    void * t232 = (void *)((uint8_t *)&F.l7);
                                    gm_b0_1 t233 = gm_f_adt_field_tys_oggq5ji((void *)NULL, cx, NULL, t229, t230, t231, t232);
                                    if (gm_unw.kind) {
                                        goto gm_ll10;
                                    }
                                }
                                goto ix209;
ie209:;
                                {
                                    void * t235 = (void *)((uint8_t *)&F.l7);
                                    gm_t88rcucg1b7uk t236 = gm_l23;
                                    gm_t9qkh0rsooc8d t237 = gm_l5;
                                    gm_t3jsf43p1pths t238;
                                    GM_ST(uint8_t, (uint8_t *)&t238 + 5, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t238 + 6, 0);
                                    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t238 + 0), t236);
                                    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t238 + 4), t237);
                                    gm_tftb1odc739t5 t239 = gm_f_push_h37silv(t235, t238);
                                    gm_tftb1odc739t5 t240 = t239;
                                    if (((((uint8_t *)&t240))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t240) + 1, gm_srclv0dcbbfhves, 1220, 21);
                                    gm_b0_1 t241 = GM_LD(gm_b0_1, (((uint8_t *)&t240) + 1));
                                }
ix209:;
                            }
                            goto me50;
mn205:;
ma56:;
                            uint8_t t243 = GM_LD(uint8_t, (((uint8_t *)&F.l6) + 0));
                            if (t243 != ((uint8_t)1ULL)) goto mn242;
                            uint32_t t244 = GM_LD(uint32_t, (((uint8_t *)&F.l6) + 8));
                            gm_l25 = t244;
                            {
                                uint32_t t247 = gm_l25;
                                uint64_t t248 = (uint64_t)t247;
                                uint64_t t249 = (uintptr_t)t248;
                                void * t250 = gm_l0;
                                uint64_t t251 = GM_LD(uintptr_t, ((((uint8_t *)t250) + 1088) + 0) + 8);
                                if (t249 >= (uintptr_t)(t251)) gm_trap_index(t249, t251, gm_srclv0dcbbfhves, 1224, 20);
                                gm_tf5srh4mp4b3i t252 = GM_LD(gm_tf5srh4mp4b3i, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t250) + 1088) + 0)) + t249 * 184) + 168));
                                uint64_t t253 = gm_l3;
                                gm_tf5srh4mp4b3i t254;
                                GM_ST(uint8_t, (uint8_t *)&t254 + 9, 0);
                                GM_ST(uint16_t, (uint8_t *)&t254 + 10, 0);
                                GM_ST(uint32_t, (uint8_t *)&t254 + 12, 0);
                                GM_ST(uint64_t, ((uint8_t *)&t254), t253);
                                ((uint8_t *)&t254)[8] = 1;
                                gm_tf5srh4mp4b3i t255 = t252;
                                gm_tf5srh4mp4b3i t256 = t254;
                                uint8_t t257 = (gm_eq7mri15hk78(((uint8_t *)&t255), ((uint8_t *)&t256)));
                                if (!t257) goto ie246;
                                {
                                    void * t259 = gm_l0;
                                    uint32_t t260 = gm_l25;
                                    gm_t9qkh0rsooc8d t261 = gm_l5;
                                    void * t262 = (void *)((uint8_t *)&F.l7);
                                    gm_b0_1 t263 = gm_f_adt_field_tys_oggq5ji((void *)NULL, cx, NULL, t259, t260, t261, t262);
                                    if (gm_unw.kind) {
                                        goto gm_ll10;
                                    }
                                }
                                goto ix246;
ie246:;
ix246:;
                            }
                            goto me50;
mn242:;
ma57:;
                            uint8_t t267 = GM_LD(uint8_t, (((uint8_t *)&F.l6) + 0));
                            if (t267 != ((uint8_t)7ULL)) goto pa266;
                            gm_t88rcucg1b7uk t268 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l6) + 8));
                            gm_l26 = t268;
                            goto po265;
pa266:;
                            uint8_t t270 = GM_LD(uint8_t, (((uint8_t *)&F.l6) + 0));
                            if (t270 != ((uint8_t)10ULL)) goto pa269;
                            gm_t88rcucg1b7uk t271 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l6) + 8));
                            gm_l26 = t271;
                            goto po265;
pa269:;
                            goto mn264;
po265:;
                            void * t272 = (void *)((uint8_t *)&F.l7);
                            gm_t88rcucg1b7uk t273 = gm_l26;
                            gm_t9qkh0rsooc8d t274 = gm_l5;
                            gm_t3jsf43p1pths t275;
                            GM_ST(uint8_t, (uint8_t *)&t275 + 5, 0);
                            GM_ST(uint16_t, (uint8_t *)&t275 + 6, 0);
                            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t275 + 0), t273);
                            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t275 + 4), t274);
                            gm_tftb1odc739t5 t276 = gm_f_push_h37silv(t272, t275);
                            gm_tftb1odc739t5 t277 = t276;
                            if (((((uint8_t *)&t277))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t277) + 1, gm_srclv0dcbbfhves, 1226, 38);
                            gm_b0_1 t278 = GM_LD(gm_b0_1, (((uint8_t *)&t277) + 1));
                            goto me50;
mn264:;
ma58:;
                            goto me50;
mn279:;
ma59:;
                            gm_trap("no match arm matched the scrutinee", gm_srclv0dcbbfhves, 1197, 9);
me50:;
                        }
                        {
                            {
                                gm_b16_8 t282 = GM_LD(gm_b16_8, (((uint8_t *)&F.l7) + 0));
                                gm_l28 = t282;
                            }
                            {
                                gm_l29 = ((uint64_t)0ULL);
                            }
                            {
                                for (;;) {
                                    uint64_t t284 = gm_l29;
                                    uint64_t t285 = GM_LD(uintptr_t, ((uint8_t *)&gm_l28) + 8);
                                    uint8_t t286 = (t284 < t285);
                                    if (!t286) goto lb283;
                                    {
                                        uint64_t t289 = gm_l29;
                                        uint64_t t290 = (uintptr_t)t289;
                                        uint64_t t291 = GM_LD(uintptr_t, ((uint8_t *)&gm_l28) + 8);
                                        gm_t3jsf43p1pths t294 = GM_LD(gm_t3jsf43p1pths, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l28)) + t290 * 8));
                                        gm_l30 = t294;
                                        {
                                            void * t297 = gm_l0;
                                            gm_t88rcucg1b7uk t298 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l30) + 0));
                                            gm_t9qkh0rsooc8d t299 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l30) + 4));
                                            gm_trcagkhgm1qdu t300 = gm_f_hidden_part_is5hgf9(t297, GM_CONV(gm_csguankko4lmhd, t298), t299);
                                            gm_trcagkhgm1qdu t301 = t300;
                                            if (!((((uint8_t *)&t301))[8] != 0)) goto ie296;
                                            gm_t3jsf43p1pths t302 = GM_LD(gm_t3jsf43p1pths, ((uint8_t *)&t301));
                                            gm_l31 = t302;
                                            {
                                                gm_b16_8 t304;
                                                uint8_t t306 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l5) + 0) == ((uint8_t)1ULL)));
                                                if (!t306) goto ie305;
                                                gm_b16_8 t308;
                                                GM_ST(void *, (uint8_t *)&t308, (void *)(gm_shofa8pdml9fb));
                                                GM_ST(uintptr_t, (uint8_t *)&t308 + 8, (uintptr_t)(12));
                                                gm_b16_8 t309 = t308;
                                                t304 = t309;
                                                goto ix305;
ie305:;
                                                gm_b16_8 t311;
                                                GM_ST(void *, (uint8_t *)&t311, (void *)(gm_sjqs7q3ostqbm));
                                                GM_ST(uintptr_t, (uint8_t *)&t311 + 8, (uintptr_t)(3));
                                                gm_b16_8 t312 = t311;
                                                t304 = t312;
ix305:;
                                                gm_l32 = t304;
                                            }
                                            {
                                                gm_b16_8 t313;
                                                uint8_t t315 = ((GM_LD(uint8_t, (((uint8_t *)&gm_l31) + 4) + 0) == ((uint8_t)1ULL)));
                                                if (!t315) goto ie314;
                                                gm_b16_8 t317;
                                                GM_ST(void *, (uint8_t *)&t317, (void *)(gm_scllp0lpbljde));
                                                GM_ST(uintptr_t, (uint8_t *)&t317 + 8, (uintptr_t)(14));
                                                gm_b16_8 t318 = t317;
                                                t313 = t318;
                                                goto ix314;
ie314:;
                                                gm_b16_8 t320;
                                                GM_ST(void *, (uint8_t *)&t320, (void *)(gm_sbrnv8n0dpinb));
                                                GM_ST(uintptr_t, (uint8_t *)&t320 + 8, (uintptr_t)(7));
                                                gm_b16_8 t321 = t320;
                                                t313 = t321;
ix314:;
                                                gm_l33 = t313;
                                            }
                                            {
                                                void * t322 = gm_l0;
                                                void * t323 = gm_l4;
                                                gm_tl874t2atnlh3 t324 = GM_LD(gm_tl874t2atnlh3, ((((uint8_t *)t323) + 0) + 16));
                                                gm_t0uuhaljsf410 t325;
                                                gm_b16_8 t326 = gm_l32;
                                                GM_ST(gm_b16_8, (uint8_t *)&t325 + 0, t326);
                                                void * t327 = gm_l4;
                                                gm_b16_8 t328 = GM_LD(gm_b16_8, ((((uint8_t *)t327) + 0) + 0));
                                                GM_ST(gm_b16_8, (uint8_t *)&t325 + 16, t328);
                                                gm_b16_8 t329 = gm_l33;
                                                GM_ST(gm_b16_8, (uint8_t *)&t325 + 32, t329);
                                                void * t330 = gm_l0;
                                                gm_t88rcucg1b7uk t331 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l31) + 0));
                                                gm_b16_8 t332 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t330, t331);
                                                if (gm_unw.kind) {
                                                    goto gm_ll287;
                                                }
                                                GM_ST(gm_b16_8, (uint8_t *)&t325 + 48, t332);
                                                gm_tabb710d9mq2e t333;
                                                gm_f_format_t5fmuah((void *)&t333, (void *)NULL, cx, NULL, &t325);
                                                if (gm_unw.kind) {
                                                    goto gm_ll287;
                                                }
                                                gm_tabb710d9mq2e t334 = t333;
                                                if (((((uint8_t *)&t334))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t334) + 8, gm_srclv0dcbbfhves, 1233, 38);
                                                gm_b16_8 t335 = GM_LD(gm_b16_8, (((uint8_t *)&t334) + 8));
                                                gm_b16_8 t336 = t335;
                                                gm_b16_8 t337 = GM_LD(gm_b16_8, ((uint8_t *)&t336));
                                                gm_b0_1 t338 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t322, t324, t337);
                                                if (gm_unw.kind) {
                                                    goto gm_ll287;
                                                }
                                            }
                                            {
                                                goto lb283;
                                            }
                                            goto ix296;
ie296:;
ix296:;
                                        }
                                        goto me292;
mn293:;
                                        gm_trap("no match arm matched the scrutinee", gm_srclv0dcbbfhves, 1229, 9);
me292:;
                                    }
lc283:;
                                    {
                                        uint64_t t340 = gm_l29;
                                        uint64_t r341; bool o342 = __builtin_add_overflow(t340, ((uint64_t)1ULL), &r341);
                                        uint64_t t343 = r341;
                                        gm_l29 = t343;
                                    }
                                }
gm_ll287:;
                                if (gm_unw.frame == &F.h && gm_unw.label == 4) {
                                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb283; }
                                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc283; }
                                }
                                goto gm_ll10;
lb283:;
                            }
                        }
                        goto ix42;
ie42:;
                        {
                            goto lc6;
                        }
ix42:;
                    }
                    goto me14;
mn15:;
                    gm_trap("no match arm matched the scrutinee", gm_srclv0dcbbfhves, 1189, 5);
me14:;
                }
lc6:;
                {
                    uint64_t t346 = gm_l2;
                    uint64_t r347; bool o348 = __builtin_add_overflow(t346, ((uint64_t)1ULL), &r347);
                    uint64_t t349 = r347;
                    gm_l2 = t349;
                }
            }
gm_ll10:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb6; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc6; }
            }
            goto gm_lad;
lb6:;
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
extern __attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_complex_of_kh32tib(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_compute_binding_cl3ogob(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, uint64_t a2, const void * a3, void * a4) {
    struct gm_fr_compute_binding_cl3ogob F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* id */
    uint64_t gm_l2; /* module */
    gm_t08olq97rscn3 gm_l3; /* name */
    void * gm_l4; /* b */
    gm_tl874t2atnlh3 gm_l6; /* between */
    gm_b16_8 gm_l7; /* text */
    void * gm_l8; /* init */
    uint64_t gm_l9; /* m */
    void * gm_l10; /* blk */
    void * gm_l11; /* lit */
    void * gm_l12; /* lit */
    gm_tnpifv20irnph gm_l13; /* set */
    gm_t88rcucg1b7uk gm_l14; /* ty */
    void * gm_l15; /* inner */
    gm_t88rcucg1b7uk gm_l16; /* ty */
    GM_STACK_CHECK(&F, 6860u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l4 = a4;
    {
        void * t3 = gm_l4;
        uint8_t t4 = ((GM_LD(uint8_t, (((uint8_t *)t3) + 32) + 0) == ((uint8_t)1ULL)));
        if (!t4) goto ie2;
        {
            void * t7 = gm_l4;
            void * t8 = GM_LD(void *, (((uint8_t *)t7) + 40));
            uint8_t t9;
            uint8_t t12 = GM_LD(uint8_t, ((((uint8_t *)t8) + 8) + 0));
            if (t12 != ((uint8_t)38ULL)) goto mn11;
            t9 = ((uint8_t)1);
            goto me10;
mn11:;
            t9 = ((uint8_t)0);
            goto me10;
mn13:;
            gm_trap("no match arm matched the scrutinee", gm_srclv0dcbbfhves, 979, 12);
me10:;
            if (!t9) goto ie6;
            {
                void * t15 = gm_l0;
                void * t16 = GM_LD(void *, (((uint8_t *)t15) + 0));
                uint64_t t17 = gm_l1;
                gm_b0_1 t18 = gm_f_resolve_import_944uqa9((void *)NULL, cx, NULL, t16, t17);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                gm_t6qooom8qa2do t19;
                memset(&t19, 0, sizeof t19);
                GM_ST(uint8_t, ((uint8_t *)&t19 + 0), ((uint8_t)11ULL));
                uint8_t t20 = 0;
                F.ret = t19;
                goto gm_fret;
            }
            goto ix6;
ie6:;
ix6:;
        }
        {
            void * t21 = gm_l4;
            gm_tl874t2atnlh3 t22 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t21) + 48));
            gm_b16_8 t23;
            GM_ST(void *, (uint8_t *)&t23, (void *)(gm_s6lnp3h2ojl1f));
            GM_ST(uintptr_t, (uint8_t *)&t23 + 8, (uintptr_t)(76));
            gm_f_error_s6hn871((void *)&F.l5, (void *)NULL, cx, NULL, t22, t23);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t24 = gm_l4;
            uint32_t t25 = GM_LD(uint32_t, ((((uint8_t *)t24) + 48) + 0));
            uint32_t t26 = GM_LD(uint32_t, ((((uint8_t *)a3) + 16) + 8));
            void * t27 = gm_l4;
            void * t28 = GM_LD(void *, (((uint8_t *)t27) + 40));
            uint32_t t29 = GM_LD(uint32_t, ((((uint8_t *)t28) + 72) + 4));
            gm_tl874t2atnlh3 t30;
            GM_ST(uint32_t, ((uint8_t *)&t30 + 0), t25);
            GM_ST(uint32_t, ((uint8_t *)&t30 + 4), t26);
            GM_ST(uint32_t, ((uint8_t *)&t30 + 8), t29);
            gm_l6 = t30;
        }
        {
            void * t31 = gm_l0;
            void * t32 = GM_LD(void *, (((uint8_t *)t31) + 0));
            void * t33 = GM_LD(void *, (((uint8_t *)t32) + 0));
            uint32_t t34 = GM_LD(uint32_t, (((uint8_t *)&gm_l6) + 0));
            void * t35 = gm_f_file_o15fqku(t33, t34);
            uint64_t t36 = GM_LD(uintptr_t, (((uint8_t *)t35) + 16) + 8);
            void * t37 = ((uint8_t *)GM_LD(void *, (((uint8_t *)t35) + 16)));
            uint32_t t38 = GM_LD(uint32_t, (((uint8_t *)&gm_l6) + 4));
            uint64_t t39 = (uint64_t)t38;
            uint64_t t40 = (uintptr_t)t39;
            uint32_t t41 = GM_LD(uint32_t, (((uint8_t *)&gm_l6) + 8));
            uint64_t t42 = (uint64_t)t41;
            uint64_t t43 = (uintptr_t)t42;
            if (t40 > t43 || t43 > (uintptr_t)t36) gm_trap_slice(t40, t43, t36, gm_srclv0dcbbfhves, 987, 17);
            gm_b16_8 t44;
            GM_ST(void *, (uint8_t *)&t44, (void *)(((uint8_t *)t37 + t40 * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t44 + 8, (uintptr_t)(t43 - t40));
            gm_l7 = t44;
        }
        {
            void * t46 = gm_l4;
            void * t47 = GM_LD(void *, (((uint8_t *)t46) + 24));
            void * t48 = t47;
            uint8_t t49;
            if ((GM_LD(void *, ((uint8_t *)&t48)) != 0)) goto mn51;
            t49 = ((uint8_t)1);
            goto me50;
mn51:;
            t49 = ((uint8_t)0);
            goto me50;
mn52:;
            gm_trap("no match arm matched the scrutinee", gm_srclv0dcbbfhves, 988, 12);
me50:;
            if (!t49) goto ie45;
            gm_b16_8 t53 = gm_l7;
            gm_b16_8 t54 = gm_f_trim_fdrfe6i(t53);
            gm_b16_8 t55 = t54;
            uint8_t t56 = (((GM_LD(uintptr_t, ((uint8_t *)&t55) + 8) == 2) && memcmp(GM_LD(void *, ((uint8_t *)&t55)), gm_shge5n743ua4h, 2) == 0));
            if (!t56) goto ie45;
            {
                void * t58 = (void *)((uint8_t *)&F.l5);
                gm_b16_8 t59;
                GM_ST(void *, (uint8_t *)&t59, (void *)(gm_spq5ja4c7ciga));
                GM_ST(uintptr_t, (uint8_t *)&t59 + 8, (uintptr_t)(10));
                gm_tl874t2atnlh3 t60 = gm_l6;
                gm_b16_8 t61;
                GM_ST(void *, (uint8_t *)&t61, (void *)(gm_s2urait735ofq));
                GM_ST(uintptr_t, (uint8_t *)&t61 + 8, (uintptr_t)(4));
                gm_b0_1 t62 = gm_f_add_fix_0gkcr4c((void *)NULL, cx, NULL, t58, t59, t60, t61);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix45;
ie45:;
ix45:;
        }
        {
            void * t63 = gm_l0;
            gm_b0_1 t64 = gm_f_emit_dph5v1e((void *)NULL, cx, NULL, t63, &(*(gm_t4gut2c9fj1kt *)((uint8_t *)&F.l5)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_t6qooom8qa2do t65;
            memset(&t65, 0, sizeof t65);
            GM_ST(uint8_t, ((uint8_t *)&t65 + 0), ((uint8_t)11ULL));
            uint8_t t66 = 0;
            F.ret = t65;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t67 = gm_l4;
        void * t68 = GM_LD(void *, (((uint8_t *)t67) + 40));
        gm_l8 = t68;
    }
    {
        void * t69 = gm_l8;
        uint8_t t81 = GM_LD(uint8_t, ((((uint8_t *)t69) + 8) + 0));
        switch (t81) { case ((uint8_t)0ULL): goto ma79; case ((uint8_t)1ULL): goto ma79; case ((uint8_t)2ULL): goto ma79; case ((uint8_t)3ULL): goto ma79; case ((uint8_t)4ULL): goto ma79; case ((uint8_t)5ULL): goto ma79; case ((uint8_t)6ULL): goto ma79; case ((uint8_t)7ULL): goto ma79; case ((uint8_t)8ULL): goto ma79; case ((uint8_t)9ULL): goto ma79; case ((uint8_t)10ULL): goto ma79; case ((uint8_t)11ULL): goto ma79; case ((uint8_t)12ULL): goto ma79; case ((uint8_t)13ULL): goto ma75; case ((uint8_t)14ULL): goto ma79; case ((uint8_t)15ULL): goto ma79; case ((uint8_t)16ULL): goto ma79; case ((uint8_t)17ULL): goto ma79; case ((uint8_t)18ULL): goto ma79; case ((uint8_t)19ULL): goto ma79; case ((uint8_t)20ULL): goto ma79; case ((uint8_t)21ULL): goto ma79; case ((uint8_t)22ULL): goto ma79; case ((uint8_t)23ULL): goto ma79; case ((uint8_t)24ULL): goto ma79; case ((uint8_t)25ULL): goto ma79; case ((uint8_t)26ULL): goto ma79; case ((uint8_t)27ULL): goto ma79; case ((uint8_t)28ULL): goto ma79; case ((uint8_t)29ULL): goto ma79; case ((uint8_t)30ULL): goto ma79; case ((uint8_t)31ULL): goto ma79; case ((uint8_t)32ULL): goto ma77; case ((uint8_t)33ULL): goto ma77; case ((uint8_t)34ULL): goto ma74; case ((uint8_t)35ULL): goto ma78; case ((uint8_t)36ULL): goto ma73; case ((uint8_t)37ULL): goto ma76; case ((uint8_t)38ULL): goto ma71; case ((uint8_t)39ULL): goto ma72; case ((uint8_t)40ULL): goto ma79; case ((uint8_t)41ULL): goto ma79; case ((uint8_t)42ULL): goto ma79; case ((uint8_t)43ULL): goto ma79; case ((uint8_t)44ULL): goto ma79; case ((uint8_t)45ULL): goto ma79; case ((uint8_t)46ULL): goto ma79; case ((uint8_t)47ULL): goto ma79; case ((uint8_t)48ULL): goto ma79; case ((uint8_t)49ULL): goto ma79; case ((uint8_t)50ULL): goto ma79; default: goto ma79; }
ma71:;
        uint8_t t83 = GM_LD(uint8_t, ((((uint8_t *)t69) + 8) + 0));
        if (t83 != ((uint8_t)38ULL)) goto mn82;
        {
            void * t85 = gm_l0;
            void * t86 = GM_LD(void *, (((uint8_t *)t85) + 0));
            uint64_t t87 = gm_l1;
            gm_b0_1 t88 = gm_f_resolve_import_944uqa9((void *)NULL, cx, NULL, t86, t87);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            uint64_t t89 = gm_l1;
            uint64_t t90 = (uintptr_t)t89;
            void * t91 = gm_l0;
            void * t92 = GM_LD(void *, (((uint8_t *)t91) + 0));
            uint64_t t93 = GM_LD(uintptr_t, ((((uint8_t *)t92) + 240) + 0) + 8);
            if (t90 >= (uintptr_t)(t93)) gm_trap_index(t90, t93, gm_srclv0dcbbfhves, 996, 26);
            gm_t6qooom8qa2do t94;
            uint8_t t97 = GM_LD(uint8_t, ((((uint8_t *)GM_LD(void *, ((((uint8_t *)t92) + 240) + 0)) + t90 * 216) + 200) + 0));
            if (t97 != ((uint8_t)1ULL)) goto mn96;
            uint64_t t98 = GM_LD(uint64_t, ((((uint8_t *)GM_LD(void *, ((((uint8_t *)t92) + 240) + 0)) + t90 * 216) + 200) + 8));
            gm_l9 = t98;
            uint64_t t99 = gm_l9;
            gm_t6qooom8qa2do t100;
            memset(&t100, 0, sizeof t100);
            GM_ST(uint8_t, ((uint8_t *)&t100 + 0), ((uint8_t)9ULL));
            GM_ST(uint64_t, ((uint8_t *)&t100 + 8), t99);
            t94 = t100;
            goto me95;
mn96:;
            gm_t6qooom8qa2do t102;
            memset(&t102, 0, sizeof t102);
            GM_ST(uint8_t, ((uint8_t *)&t102 + 0), ((uint8_t)11ULL));
            t94 = t102;
            goto me95;
mn101:;
            gm_trap("no match arm matched the scrutinee", gm_srclv0dcbbfhves, 996, 20);
me95:;
            uint8_t t103 = 0;
            F.ret = t94;
            goto gm_fret;
        }
        goto me70;
mn82:;
ma72:;
        uint8_t t105 = GM_LD(uint8_t, ((((uint8_t *)t69) + 8) + 0));
        if (t105 != ((uint8_t)39ULL)) goto mn104;
        void * t106 = GM_LD(void *, ((((uint8_t *)t69) + 8) + 8));
        gm_l10 = t106;
        void * t108 = gm_l4;
        void * t109 = GM_LD(void *, (((uint8_t *)t108) + 24));
        void * t110 = t109;
        uint8_t t111;
        if ((GM_LD(void *, ((uint8_t *)&t110)) != 0)) goto mn113;
        t111 = ((uint8_t)1);
        goto me112;
mn113:;
        t111 = ((uint8_t)0);
        goto me112;
mn114:;
        gm_trap("no match arm matched the scrutinee", gm_srclv0dcbbfhves, 1001, 29);
me112:;
        if (!t111) goto ie107;
        {
            void * t116 = gm_l0;
            uint64_t t117 = gm_l2;
            void * t118 = gm_l10;
            gm_f_declare_foreign_6s86jje((void *)&(*(gm_t6qooom8qa2do *)gm_out), (void *)NULL, cx, NULL, t116, t117, &(*(gm_t08olq97rscn3 *)((uint8_t *)a3)), t118);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
        }
        goto ix107;
ie107:;
ix107:;
        goto me70;
mn104:;
ma73:;
        uint8_t t120 = GM_LD(uint8_t, ((((uint8_t *)t69) + 8) + 0));
        if (t120 != ((uint8_t)36ULL)) goto mn119;
        void * t121 = GM_LD(void *, ((((uint8_t *)t69) + 8) + 8));
        gm_l11 = t121;
        void * t123 = gm_l4;
        void * t124 = GM_LD(void *, (((uint8_t *)t123) + 24));
        void * t125 = t124;
        uint8_t t126;
        if ((GM_LD(void *, ((uint8_t *)&t125)) != 0)) goto mn128;
        t126 = ((uint8_t)1);
        goto me127;
mn128:;
        t126 = ((uint8_t)0);
        goto me127;
mn129:;
        gm_trap("no match arm matched the scrutinee", gm_srclv0dcbbfhves, 1002, 24);
me127:;
        if (!t126) goto ie122;
        {
            void * t131 = gm_l0;
            uint64_t t132 = gm_l2;
            void * t133 = gm_l11;
            gm_f_declare_fn_6piolnr((void *)&(*(gm_t6qooom8qa2do *)gm_out), (void *)NULL, cx, NULL, t131, t132, &(*(gm_t08olq97rscn3 *)((uint8_t *)a3)), t133);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
        }
        goto ix122;
ie122:;
ix122:;
        goto me70;
mn119:;
ma74:;
        uint8_t t135 = GM_LD(uint8_t, ((((uint8_t *)t69) + 8) + 0));
        if (t135 != ((uint8_t)34ULL)) goto mn134;
        void * t136 = GM_LD(void *, ((((uint8_t *)t69) + 8) + 8));
        gm_l12 = t136;
        void * t138 = gm_l4;
        void * t139 = GM_LD(void *, (((uint8_t *)t138) + 24));
        void * t140 = t139;
        uint8_t t141;
        if ((GM_LD(void *, ((uint8_t *)&t140)) != 0)) goto mn143;
        t141 = ((uint8_t)1);
        goto me142;
mn143:;
        t141 = ((uint8_t)0);
        goto me142;
mn144:;
        gm_trap("no match arm matched the scrutinee", gm_srclv0dcbbfhves, 1003, 28);
me142:;
        if (!t141) goto ie137;
        {
            void * t146 = gm_l0;
            uint64_t t147 = gm_l2;
            uint64_t t148 = gm_l1;
            void * t149 = gm_l12;
            gm_f_declare_effect_9n12hc8((void *)&(*(gm_t6qooom8qa2do *)gm_out), (void *)NULL, cx, NULL, t146, t147, t148, &(*(gm_t08olq97rscn3 *)((uint8_t *)a3)), t149);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
        }
        goto ix137;
ie137:;
ix137:;
        goto me70;
mn134:;
ma75:;
        uint8_t t151 = GM_LD(uint8_t, ((((uint8_t *)t69) + 8) + 0));
        if (t151 != ((uint8_t)13ULL)) goto mn150;
        uint8_t t152 = GM_LD(uint8_t, ((((((uint8_t *)t69) + 8) + 8) + 0) + 0));
        if (t152 != ((uint8_t)0ULL)) goto mn150;
        uint8_t t153 = GM_LD(uint8_t, (((((((uint8_t *)t69) + 8) + 8) + 0) + 1) + 0));
        if (t153 != ((uint8_t)0ULL)) goto mn150;
        {
            void * t156 = gm_l4;
            void * t157 = GM_LD(void *, (((uint8_t *)t156) + 24));
            void * t158 = t157;
            uint8_t t159;
            if ((GM_LD(void *, ((uint8_t *)&t158)) != 0)) goto mn161;
            t159 = ((uint8_t)1);
            goto me160;
mn161:;
            t159 = ((uint8_t)0);
            goto me160;
mn162:;
            gm_trap("no match arm matched the scrutinee", gm_srclv0dcbbfhves, 1006, 16);
me160:;
            if (!t159) goto ie155;
            void * t163 = gm_l0;
            uint64_t t164 = gm_l2;
            void * t165 = gm_l8;
            uint8_t t166 = gm_f_is_effect_sum_dd9le16((void *)NULL, cx, NULL, t163, t164, t165);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (!t166) goto ie155;
            {
                void * t168 = gm_l0;
                uint64_t t169 = gm_l2;
                void * t170 = gm_l8;
                gm_b16_8 t171 = gm_f_effect_terms_bphj7th((void *)NULL, cx, NULL, t170);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_b16_8 t172 = t171;
                gm_b16_8 t173 = GM_LD(gm_b16_8, ((uint8_t *)&t172));
                gm_tnpifv20irnph t174 = gm_f_resolve_effect_set_o7rc5od((void *)NULL, cx, NULL, t168, t169, t173);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l13 = t174;
            }
            {
                gm_tnpifv20irnph t176 = gm_l13;
                uint8_t t177 = gm_f_has_splice_80pgqas(t176);
                if (!t177) goto ie175;
                {
                    void * t179 = gm_l0;
                    void * t180 = gm_l8;
                    gm_tl874t2atnlh3 t181 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t180) + 72));
                    gm_b16_8 t182;
                    GM_ST(void *, (uint8_t *)&t182, (void *)(gm_s9ocqto6ckbos));
                    GM_ST(uintptr_t, (uint8_t *)&t182 + 8, (uintptr_t)(57));
                    gm_b0_1 t183 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t179, t181, t182);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                {
                    gm_t6qooom8qa2do t184;
                    memset(&t184, 0, sizeof t184);
                    GM_ST(uint8_t, ((uint8_t *)&t184 + 0), ((uint8_t)11ULL));
                    uint8_t t185 = 0;
                    F.ret = t184;
                    goto gm_fret;
                }
                goto ix175;
ie175:;
ix175:;
            }
            {
                gm_tnpifv20irnph t186 = gm_l13;
                gm_t6qooom8qa2do t187;
                memset(&t187, 0, sizeof t187);
                GM_ST(uint8_t, ((uint8_t *)&t187 + 0), ((uint8_t)4ULL));
                GM_ST(gm_tnpifv20irnph, ((uint8_t *)&t187 + 8), t186);
                uint8_t t188 = 0;
                F.ret = t187;
                goto gm_fret;
            }
            goto ix155;
ie155:;
ix155:;
        }
        goto me70;
mn150:;
ma76:;
        uint8_t t190 = GM_LD(uint8_t, ((((uint8_t *)t69) + 8) + 0));
        if (t190 != ((uint8_t)37ULL)) goto mn189;
        {
            void * t192 = gm_l0;
            void * t193 = gm_l8;
            gm_tl874t2atnlh3 t194 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t193) + 72));
            gm_b16_8 t195;
            GM_ST(void *, (uint8_t *)&t195, (void *)(gm_s6ibuqseh9tt7));
            GM_ST(uintptr_t, (uint8_t *)&t195 + 8, (uintptr_t)(151));
            gm_b0_1 t196 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t192, t194, t195);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_t6qooom8qa2do t197;
            memset(&t197, 0, sizeof t197);
            GM_ST(uint8_t, ((uint8_t *)&t197 + 0), ((uint8_t)11ULL));
            uint8_t t198 = 0;
            F.ret = t197;
            goto gm_fret;
        }
        goto me70;
mn189:;
ma77:;
        uint8_t t202 = GM_LD(uint8_t, ((((uint8_t *)t69) + 8) + 0));
        if (t202 != ((uint8_t)32ULL)) goto pa201;
        goto po200;
pa201:;
        uint8_t t204 = GM_LD(uint8_t, ((((uint8_t *)t69) + 8) + 0));
        if (t204 != ((uint8_t)33ULL)) goto pa203;
        goto po200;
pa203:;
        goto mn199;
po200:;
        {
            void * t207 = gm_l4;
            void * t208 = GM_LD(void *, (((uint8_t *)t207) + 24));
            void * t209 = t208;
            uint8_t t210;
            if ((GM_LD(void *, ((uint8_t *)&t209)) != 0)) goto mn212;
            t210 = ((uint8_t)1);
            goto me211;
mn212:;
            t210 = ((uint8_t)0);
            goto me211;
mn213:;
            gm_trap("no match arm matched the scrutinee", gm_srclv0dcbbfhves, 1020, 16);
me211:;
            if (!t210) goto ie206;
            {
                void * t215 = gm_l0;
                uint64_t t216 = gm_l2;
                uint64_t t217 = gm_l1;
                gm_tf5srh4mp4b3i t218;
                GM_ST(uint8_t, (uint8_t *)&t218 + 9, 0);
                GM_ST(uint16_t, (uint8_t *)&t218 + 10, 0);
                GM_ST(uint32_t, (uint8_t *)&t218 + 12, 0);
                GM_ST(uint64_t, ((uint8_t *)&t218), t217);
                ((uint8_t *)&t218)[8] = 1;
                gm_b16_8 t219 = GM_LD(gm_b16_8, (((uint8_t *)a3) + 0));
                void * t220 = gm_l8;
                gm_t88rcucg1b7uk t221 = gm_f_declare_adt_iefkbkk((void *)NULL, cx, NULL, t215, t216, t218, t219, t220);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l14 = t221;
            }
            {
                gm_t6qooom8qa2do t222;
                void * t224 = gm_l0;
                void * t225 = (void *)(((uint8_t *)t224) + 8);
                gm_t88rcucg1b7uk t226 = gm_l14;
                uint8_t t227 = gm_f_is_error_8k27gd1(t225, GM_CONV(gm_csguankko4lmhd, t226));
                if (!t227) goto ie223;
                gm_t6qooom8qa2do t229;
                memset(&t229, 0, sizeof t229);
                GM_ST(uint8_t, ((uint8_t *)&t229 + 0), ((uint8_t)11ULL));
                gm_t6qooom8qa2do t230 = t229;
                t222 = t230;
                goto ix223;
ie223:;
                gm_t88rcucg1b7uk t232 = gm_l14;
                gm_t6qooom8qa2do t233;
                memset(&t233, 0, sizeof t233);
                GM_ST(uint8_t, ((uint8_t *)&t233 + 0), ((uint8_t)0ULL));
                GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t233 + 8), t232);
                gm_t6qooom8qa2do t234 = t233;
                t222 = t234;
ix223:;
                uint8_t t235 = 0;
                F.ret = t222;
                goto gm_fret;
            }
            goto ix206;
ie206:;
ix206:;
        }
        goto me70;
mn199:;
ma78:;
        uint8_t t237 = GM_LD(uint8_t, ((((uint8_t *)t69) + 8) + 0));
        if (t237 != ((uint8_t)35ULL)) goto mn236;
        void * t238 = GM_LD(void *, (((((uint8_t *)t69) + 8) + 8) + 16));
        gm_l15 = t238;
        {
            void * t241 = gm_l4;
            void * t242 = GM_LD(void *, (((uint8_t *)t241) + 24));
            void * t243 = t242;
            uint8_t t244;
            if ((GM_LD(void *, ((uint8_t *)&t243)) != 0)) goto mn246;
            t244 = ((uint8_t)1);
            goto me245;
mn246:;
            t244 = ((uint8_t)0);
            goto me245;
mn247:;
            gm_trap("no match arm matched the scrutinee", gm_srclv0dcbbfhves, 1026, 16);
me245:;
            if (!t244) goto ie240;
            void * t248 = gm_l15;
            uint8_t t249;
            uint8_t t255 = GM_LD(uint8_t, ((((uint8_t *)t248) + 8) + 0));
            switch (t255) { case ((uint8_t)0ULL): goto ma253; case ((uint8_t)1ULL): goto ma253; case ((uint8_t)2ULL): goto ma253; case ((uint8_t)3ULL): goto ma253; case ((uint8_t)4ULL): goto ma253; case ((uint8_t)5ULL): goto ma253; case ((uint8_t)6ULL): goto ma253; case ((uint8_t)7ULL): goto ma253; case ((uint8_t)8ULL): goto ma253; case ((uint8_t)9ULL): goto ma253; case ((uint8_t)10ULL): goto ma253; case ((uint8_t)11ULL): goto ma253; case ((uint8_t)12ULL): goto ma253; case ((uint8_t)13ULL): goto ma253; case ((uint8_t)14ULL): goto ma253; case ((uint8_t)15ULL): goto ma253; case ((uint8_t)16ULL): goto ma253; case ((uint8_t)17ULL): goto ma253; case ((uint8_t)18ULL): goto ma253; case ((uint8_t)19ULL): goto ma253; case ((uint8_t)20ULL): goto ma253; case ((uint8_t)21ULL): goto ma253; case ((uint8_t)22ULL): goto ma253; case ((uint8_t)23ULL): goto ma253; case ((uint8_t)24ULL): goto ma253; case ((uint8_t)25ULL): goto ma253; case ((uint8_t)26ULL): goto ma253; case ((uint8_t)27ULL): goto ma253; case ((uint8_t)28ULL): goto ma253; case ((uint8_t)29ULL): goto ma253; case ((uint8_t)30ULL): goto ma253; case ((uint8_t)31ULL): goto ma253; case ((uint8_t)32ULL): goto ma251; case ((uint8_t)33ULL): goto ma252; case ((uint8_t)34ULL): goto ma253; case ((uint8_t)35ULL): goto ma253; case ((uint8_t)36ULL): goto ma253; case ((uint8_t)37ULL): goto ma253; case ((uint8_t)38ULL): goto ma253; case ((uint8_t)39ULL): goto ma253; case ((uint8_t)40ULL): goto ma253; case ((uint8_t)41ULL): goto ma253; case ((uint8_t)42ULL): goto ma253; case ((uint8_t)43ULL): goto ma253; case ((uint8_t)44ULL): goto ma253; case ((uint8_t)45ULL): goto ma253; case ((uint8_t)46ULL): goto ma253; case ((uint8_t)47ULL): goto ma253; case ((uint8_t)48ULL): goto ma253; case ((uint8_t)49ULL): goto ma253; case ((uint8_t)50ULL): goto ma253; default: goto ma253; }
ma251:;
            uint8_t t257 = GM_LD(uint8_t, ((((uint8_t *)t248) + 8) + 0));
            if (t257 != ((uint8_t)32ULL)) goto mn256;
            t249 = ((uint8_t)1);
            goto me250;
mn256:;
ma252:;
            uint8_t t259 = GM_LD(uint8_t, ((((uint8_t *)t248) + 8) + 0));
            if (t259 != ((uint8_t)33ULL)) goto mn258;
            t249 = ((uint8_t)1);
            goto me250;
mn258:;
ma253:;
            t249 = ((uint8_t)0);
            goto me250;
mn260:;
ma254:;
            gm_trap("no match arm matched the scrutinee", gm_srclv0dcbbfhves, 1026, 33);
me250:;
            if (!t249) goto ie240;
            {
                void * t262 = gm_l0;
                uint64_t t263 = gm_l2;
                uint64_t t264 = gm_l1;
                gm_tf5srh4mp4b3i t265;
                GM_ST(uint8_t, (uint8_t *)&t265 + 9, 0);
                GM_ST(uint16_t, (uint8_t *)&t265 + 10, 0);
                GM_ST(uint32_t, (uint8_t *)&t265 + 12, 0);
                GM_ST(uint64_t, ((uint8_t *)&t265), t264);
                ((uint8_t *)&t265)[8] = 1;
                gm_b16_8 t266 = GM_LD(gm_b16_8, (((uint8_t *)a3) + 0));
                void * t267 = gm_l8;
                gm_t88rcucg1b7uk t268 = gm_f_declare_adt_iefkbkk((void *)NULL, cx, NULL, t262, t263, t265, t266, t267);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l16 = t268;
            }
            {
                gm_t6qooom8qa2do t269;
                void * t271 = gm_l0;
                void * t272 = (void *)(((uint8_t *)t271) + 8);
                gm_t88rcucg1b7uk t273 = gm_l16;
                uint8_t t274 = gm_f_is_error_8k27gd1(t272, GM_CONV(gm_csguankko4lmhd, t273));
                if (!t274) goto ie270;
                gm_t6qooom8qa2do t276;
                memset(&t276, 0, sizeof t276);
                GM_ST(uint8_t, ((uint8_t *)&t276 + 0), ((uint8_t)11ULL));
                gm_t6qooom8qa2do t277 = t276;
                t269 = t277;
                goto ix270;
ie270:;
                gm_t88rcucg1b7uk t279 = gm_l16;
                gm_t6qooom8qa2do t280;
                memset(&t280, 0, sizeof t280);
                GM_ST(uint8_t, ((uint8_t *)&t280 + 0), ((uint8_t)0ULL));
                GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t280 + 8), t279);
                gm_t6qooom8qa2do t281 = t280;
                t269 = t281;
ix270:;
                uint8_t t282 = 0;
                F.ret = t269;
                goto gm_fret;
            }
            goto ix240;
ie240:;
ix240:;
        }
        goto me70;
mn236:;
ma79:;
        goto me70;
mn283:;
ma80:;
        gm_trap("no match arm matched the scrutinee", gm_srclv0dcbbfhves, 993, 5);
me70:;
    }
    {
        void * t286 = gm_l4;
        void * t287 = GM_LD(void *, (((uint8_t *)t286) + 24));
        void * t288 = t287;
        uint8_t t289;
        if ((GM_LD(void *, ((uint8_t *)&t288)) != 0)) goto mn291;
        t289 = ((uint8_t)1);
        goto me290;
mn291:;
        t289 = ((uint8_t)0);
        goto me290;
mn292:;
        gm_trap("no match arm matched the scrutinee", gm_srclv0dcbbfhves, 1034, 8);
me290:;
        if (!t289) goto ie285;
        {
            void * t295 = gm_l0;
            uint64_t t296 = gm_l2;
            void * t297 = gm_l8;
            gm_tkv1vap90vht2 t298;
            gm_f_try_untyped_fj0h8i5((void *)&t298, (void *)NULL, cx, NULL, t295, t296, t297);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tkv1vap90vht2 t299 = t298;
            if (!((((uint8_t *)&t299))[208] != 0)) goto ie294;
            gm_t5vbkdq81qfs7 t300 = GM_LD(gm_t5vbkdq81qfs7, ((uint8_t *)&t299));
            F.l17 = t300;
            {
                gm_t6qooom8qa2do t302;
                uint8_t t305 = GM_LD(uint8_t, (((uint8_t *)&F.l17) + 0));
                if (t305 != ((uint8_t)0ULL)) goto mn304;
                gm_t0ialelq3ql1k t306 = GM_LD(gm_t0ialelq3ql1k, (((uint8_t *)&F.l17) + 8));
                F.l18 = t306;
                gm_t0ialelq3ql1k t307 = F.l18;
                gm_t6qooom8qa2do t308;
                GM_ST(uint8_t, (uint8_t *)&t308 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t308 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t308 + 4, 0);
                GM_ST(uint8_t, ((uint8_t *)&t308 + 0), ((uint8_t)8ULL));
                GM_ST(gm_t0ialelq3ql1k, ((uint8_t *)&t308 + 8), t307);
                t302 = t308;
                goto me303;
mn304:;
                uint8_t t310 = GM_LD(uint8_t, (((uint8_t *)&F.l17) + 0));
                if (t310 != ((uint8_t)1ULL)) goto mn309;
                gm_t6qooom8qa2do t311;
                memset(&t311, 0, sizeof t311);
                GM_ST(uint8_t, ((uint8_t *)&t311 + 0), ((uint8_t)11ULL));
                t302 = t311;
                goto me303;
mn309:;
                gm_trap("no match arm matched the scrutinee", gm_srclv0dcbbfhves, 1036, 20);
me303:;
                uint8_t t312 = 0;
                F.ret = t302;
                goto gm_fret;
            }
            goto ix294;
ie294:;
ix294:;
        }
        goto ix285;
ie285:;
ix285:;
    }
    {
        void * t313 = gm_l0;
        uint64_t t314 = gm_l2;
        void * t315 = gm_l4;
        void * t316 = GM_LD(void *, (((uint8_t *)t315) + 24));
        void * t317 = gm_l8;
        gm_f_declare_const_or_type_4jaj4k6((void *)&(*(gm_t6qooom8qa2do *)gm_out), (void *)NULL, cx, NULL, t313, t314, &(*(gm_t08olq97rscn3 *)((uint8_t *)a3)), t316, t317);
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
__attribute__((aligned(4))) void gm_f_compute_decl_gcf8mq9(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1) {
    struct gm_fr_compute_decl_gcf8mq9 F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* id */
    uint64_t gm_l4; /* module */
    gm_t08olq97rscn3 gm_l5; /* name */
    uint8_t gm_l7; /* global */
    uint8_t gm_l8; /* thread_local */
    gm_tf5srh4mp4b3i gm_l9; /* fuel */
    uint8_t gm_l11; /* inline */
    gm_tbcgm40aljf9h gm_l12; /* hint_set */
    gm_t84dv0cg48dut gm_l15; /* callconv */
    gm_toceffb697p0u gm_l16; /* trap_hook */
    gm_b16_8 gm_l17; /* <tmp> */
    uint64_t gm_l18; /* <tmp> */
    uint8_t gm_l20; /* ok */
    gm_b16_8 gm_l21; /* args */
    uint64_t gm_l24; /* n */
    uint8_t gm_l25; /* ok */
    gm_b16_8 gm_l26; /* args */
    gm_tibupt4dmcqai gm_l27; /* lit */
    gm_b16_8 gm_l28; /* args */
    uint8_t gm_l29; /* w */
    uint8_t gm_l30; /* ok */
    gm_b16_8 gm_l31; /* args */
    gm_b16_8 gm_l32; /* other */
    gm_tf5srh4mp4b3i gm_l33; /* global_align */
    uint64_t gm_l36; /* saved_fuel */
    gm_t88rcucg1b7uk gm_l37; /* ty */
    gm_tbvif938ns15l gm_l38; /* c */
    uint32_t gm_l39; /* f */
    uint32_t gm_l41; /* f */
    uint32_t gm_l42; /* f */
    gm_tl874t2atnlh3 gm_l43; /* span */
    uint32_t gm_l44; /* f */
    gm_t08olq97rscn3 gm_l45; /* e */
    uint32_t gm_l46; /* f */
    GM_STACK_CHECK(&F, 5213u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint64_t t2 = gm_l1;
        uint64_t t3 = (uintptr_t)t2;
        void * t4 = gm_l0;
        void * t5 = GM_LD(void *, (((uint8_t *)t4) + 0));
        uint64_t t6 = GM_LD(uintptr_t, ((((uint8_t *)t5) + 240) + 0) + 8);
        if (t3 >= (uintptr_t)(t6)) gm_trap_index(t3, t6, gm_srclv0dcbbfhves, 829, 13);
        gm_t52j636u3vgk3 t7 = GM_LD(gm_t52j636u3vgk3, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t5) + 240) + 0)) + t3 * 216));
        F.l2 = t7;
    }
    {
        gm_tvpnktv5fo8nl t8 = GM_LD(gm_tvpnktv5fo8nl, (((uint8_t *)&F.l2) + 40));
        F.l3 = t8;
    }
    {
        uint64_t t9 = GM_LD(uint64_t, (((uint8_t *)&F.l2) + 32));
        gm_l4 = t9;
    }
    {
        gm_t08olq97rscn3 t10 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&F.l2) + 0));
        gm_l5 = t10;
    }
    {
        uint8_t t12 = GM_LD(uint8_t, ((((uint8_t *)&F.l3) + 64) + 0));
        if (t12 != ((uint8_t)0ULL)) goto ie11;
        gm_tv2uf4leil5hq t13 = GM_LD(gm_tv2uf4leil5hq, ((((uint8_t *)&F.l3) + 64) + 8));
        F.l6 = t13;
        {
            gm_l7 = ((uint8_t)0);
        }
        {
            gm_l8 = ((uint8_t)0);
        }
        {
            gm_tf5srh4mp4b3i t15;
            memset(&t15, 0, sizeof t15);
            gm_l9 = t15;
        }
        {
            gm_tcus0chk5r9uq t16;
            memset(&t16, 0, sizeof t16);
            F.l10 = t16;
        }
        {
            gm_l11 = ((uint8_t)0);
        }
        {
            gm_tqs5p4n2tq19v t17;
            memset(&t17, 0, sizeof t17);
            gm_tbcgm40aljf9h t18;
            GM_ST(uint32_t, ((uint8_t *)&t18 + 0), ((uint32_t)0ULL));
            GM_ST(gm_tqs5p4n2tq19v, ((uint8_t *)&t18 + 4), t17);
            GM_ST(uint8_t, ((uint8_t *)&t18 + 6), ((uint8_t)0));
            GM_ST(uint8_t, ((uint8_t *)&t18 + 7), ((uint8_t)0));
            gm_l12 = t18;
        }
        {
            gm_tcus0chk5r9uq t19;
            memset(&t19, 0, sizeof t19);
            F.l13 = t19;
        }
        {
            gm_t1s75i08qtptd t20;
            memset(&t20, 0, sizeof t20);
            F.l14 = t20;
        }
        {
            gm_t84dv0cg48dut t21;
            memset(&t21, 0, sizeof t21);
            gm_l15 = t21;
        }
        {
            gm_toceffb697p0u t22;
            memset(&t22, 0, sizeof t22);
            gm_l16 = t22;
        }
        {
            {
                gm_b16_8 t24 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 40));
                gm_l17 = t24;
            }
            {
                gm_l18 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t26 = gm_l18;
                    uint64_t t27 = GM_LD(uintptr_t, ((uint8_t *)&gm_l17) + 8);
                    uint8_t t28 = (t26 < t27);
                    if (!t28) goto lb25;
                    {
                        uint64_t t31 = gm_l18;
                        uint64_t t32 = (uintptr_t)t31;
                        uint64_t t33 = GM_LD(uintptr_t, ((uint8_t *)&gm_l17) + 8);
                        gm_tkacitepg5fk0 t36 = GM_LD(gm_tkacitepg5fk0, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l17)) + t32 * 72));
                        F.l19 = t36;
                        {
                            if (!((GM_LD(uintptr_t, ((((uint8_t *)&F.l19) + 0) + 0) + 8) == 6) && memcmp(GM_LD(void *, ((((uint8_t *)&F.l19) + 0) + 0)), gm_sptnm8c6c52dr, 6) == 0)) goto mn39;
                            {
                                gm_l7 = ((uint8_t)1);
                            }
                            goto me38;
mn39:;
                            if (!((GM_LD(uintptr_t, ((((uint8_t *)&F.l19) + 0) + 0) + 8) == 5) && memcmp(GM_LD(void *, ((((uint8_t *)&F.l19) + 0) + 0)), gm_sj1k4t2de5144, 5) == 0)) goto mn41;
                            {
                                uint8_t t44;
                                if (((((uint8_t *)&F.l13))[72] != 0)) goto mn46;
                                t44 = ((uint8_t)1);
                                goto me45;
mn46:;
                                t44 = ((uint8_t)0);
                                goto me45;
mn47:;
                                gm_trap("no match arm matched the scrutinee", gm_srclv0dcbbfhves, 849, 20);
me45:;
                                if (!t44) goto ie43;
                                {
                                    gm_tkacitepg5fk0 t49 = F.l19;
                                    gm_tcus0chk5r9uq t50;
                                    GM_ST(uint8_t, (uint8_t *)&t50 + 73, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t50 + 74, 0);
                                    GM_ST(uint32_t, (uint8_t *)&t50 + 76, 0);
                                    GM_ST(gm_tkacitepg5fk0, ((uint8_t *)&t50), t49);
                                    ((uint8_t *)&t50)[72] = 1;
                                    F.l13 = t50;
                                }
                                goto ix43;
ie43:;
                                {
                                    void * t52 = gm_l0;
                                    gm_tl874t2atnlh3 t53 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l19) + 56));
                                    gm_b16_8 t54;
                                    GM_ST(void *, (uint8_t *)&t54, (void *)(gm_scacg345bm166));
                                    GM_ST(uintptr_t, (uint8_t *)&t54 + 8, (uintptr_t)(68));
                                    gm_b0_1 t55 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t52, t53, t54);
                                    if (gm_unw.kind) {
                                        goto gm_ll29;
                                    }
                                }
ix43:;
                            }
                            goto me38;
mn41:;
                            if (!((GM_LD(uintptr_t, ((((uint8_t *)&F.l19) + 0) + 0) + 8) == 12) && memcmp(GM_LD(void *, ((((uint8_t *)&F.l19) + 0) + 0)), gm_s523ivho5m3dq, 12) == 0)) goto mn56;
                            {
                                gm_l8 = ((uint8_t)1);
                            }
                            goto me38;
mn56:;
                            if (!((GM_LD(uintptr_t, ((((uint8_t *)&F.l19) + 0) + 0) + 8) == 4) && memcmp(GM_LD(void *, ((((uint8_t *)&F.l19) + 0) + 0)), gm_stjei6jfiba56, 4) == 0)) goto mn58;
                            {
                                gm_l20 = ((uint8_t)0);
                            }
                            {
                                if (!(((((uint8_t *)&F.l19) + 32))[16] != 0)) goto ie60;
                                gm_b16_8 t61 = GM_LD(gm_b16_8, (((uint8_t *)&F.l19) + 32));
                                gm_l21 = t61;
                                uint64_t t62 = GM_LD(uintptr_t, ((uint8_t *)&gm_l21) + 8);
                                uint8_t t63 = ((t62 == ((uint64_t)1ULL)));
                                if (!t63) goto ie60;
                                {
                                    void * t66 = gm_l0;
                                    uint64_t t67 = gm_l4;
                                    uint64_t t68 = (uintptr_t)((uint64_t)0ULL);
                                    uint64_t t69 = GM_LD(uintptr_t, ((uint8_t *)&gm_l21) + 8);
                                    if (t68 >= (uintptr_t)(t69)) gm_trap_index(t68, t69, gm_srclv0dcbbfhves, 860, 52);
                                    void * t70 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l21)) + t68 * 88);
                                    gm_tkv1vap90vht2 t71;
                                    gm_f_try_untyped_fj0h8i5((void *)&t71, (void *)NULL, cx, NULL, t66, t67, t70);
                                    if (gm_unw.kind) {
                                        goto gm_ll29;
                                    }
                                    gm_tkv1vap90vht2 t72 = t71;
                                    if (!((((uint8_t *)&t72))[208] != 0)) goto ie65;
                                    gm_t5vbkdq81qfs7 t73 = GM_LD(gm_t5vbkdq81qfs7, ((uint8_t *)&t72));
                                    F.l22 = t73;
                                    uint8_t t74 = GM_LD(uint8_t, (((uint8_t *)&F.l22) + 0));
                                    if (t74 != ((uint8_t)0ULL)) goto ie65;
                                    uint8_t t75 = GM_LD(uint8_t, ((((uint8_t *)&F.l22) + 8) + 0));
                                    if (t75 != ((uint8_t)0ULL)) goto ie65;
                                    gm_tbci5ospk5nnn t76 = GM_LD(gm_tbci5ospk5nnn, ((((uint8_t *)&F.l22) + 8) + 8));
                                    F.l23 = t76;
                                    uint8_t t77 = gm_f_is_positive_su660ab(&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l23)));
                                    if (!t77) goto ie65;
                                    {
                                        gm_tf5srh4mp4b3i t80 = gm_f_to_u64_m01t1da(&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l23)));
                                        gm_tf5srh4mp4b3i t81 = t80;
                                        if (!((((uint8_t *)&t81))[8] != 0)) goto ie79;
                                        uint64_t t82 = GM_LD(uint64_t, ((uint8_t *)&t81));
                                        gm_l24 = t82;
                                        {
                                            uint64_t t84 = gm_l24;
                                            gm_tf5srh4mp4b3i t85;
                                            GM_ST(uint8_t, (uint8_t *)&t85 + 9, 0);
                                            GM_ST(uint16_t, (uint8_t *)&t85 + 10, 0);
                                            GM_ST(uint32_t, (uint8_t *)&t85 + 12, 0);
                                            GM_ST(uint64_t, ((uint8_t *)&t85), t84);
                                            ((uint8_t *)&t85)[8] = 1;
                                            gm_l9 = t85;
                                        }
                                        {
                                            gm_l20 = ((uint8_t)1);
                                        }
                                        goto ix79;
ie79:;
ix79:;
                                    }
                                    goto ix65;
ie65:;
ix65:;
                                }
                                goto ix60;
ie60:;
ix60:;
                            }
                            {
                                uint8_t t87 = gm_l20;
                                uint8_t t88 = (uint8_t)!t87;
                                if (!t88) goto ie86;
                                {
                                    void * t90 = gm_l0;
                                    gm_tl874t2atnlh3 t91 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l19) + 56));
                                    gm_b16_8 t92;
                                    GM_ST(void *, (uint8_t *)&t92, (void *)(gm_st027n235d4us));
                                    GM_ST(uintptr_t, (uint8_t *)&t92 + 8, (uintptr_t)(44));
                                    gm_b0_1 t93 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t90, t91, t92);
                                    if (gm_unw.kind) {
                                        goto gm_ll29;
                                    }
                                }
                                goto ix86;
ie86:;
ix86:;
                            }
                            goto me38;
mn58:;
                            if (!((GM_LD(uintptr_t, ((((uint8_t *)&F.l19) + 0) + 0) + 8) == 6) && memcmp(GM_LD(void *, ((((uint8_t *)&F.l19) + 0) + 0)), gm_spe8t3e79vnal, 6) == 0)) goto mn94;
                            {
                                gm_l25 = ((uint8_t)0);
                            }
                            {
                                if (!(((((uint8_t *)&F.l19) + 32))[16] != 0)) goto ie96;
                                gm_b16_8 t97 = GM_LD(gm_b16_8, (((uint8_t *)&F.l19) + 32));
                                gm_l26 = t97;
                                uint64_t t98 = GM_LD(uintptr_t, ((uint8_t *)&gm_l26) + 8);
                                uint8_t t99 = ((t98 == ((uint64_t)1ULL)));
                                if (!t99) goto ie96;
                                uint64_t t100 = (uintptr_t)((uint64_t)0ULL);
                                uint64_t t101 = GM_LD(uintptr_t, ((uint8_t *)&gm_l26) + 8);
                                if (t100 >= (uintptr_t)(t101)) gm_trap_index(t100, t101, gm_srclv0dcbbfhves, 871, 73);
                                uint8_t t102 = GM_LD(uint8_t, ((((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l26)) + t100 * 88) + 8) + 0));
                                if (t102 != ((uint8_t)2ULL)) goto ie96;
                                gm_tibupt4dmcqai t103 = GM_LD(gm_tibupt4dmcqai, ((((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l26)) + t100 * 88) + 8) + 8));
                                gm_l27 = t103;
                                {
                                    gm_b16_8 t106 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l27) + 0));
                                    gm_b16_8 t107 = t106;
                                    gm_b16_8 t108 = GM_LD(gm_b16_8, ((uint8_t *)&t107));
                                    uint8_t t109 = gm_f_utf8_valid_a8oc1t8(t108);
                                    uint8_t t110 = (uint8_t)!t109;
                                    if (!t110) goto ie105;
                                    {
                                        void * t112 = gm_l0;
                                        uint64_t t113 = (uintptr_t)((uint64_t)0ULL);
                                        uint64_t t114 = GM_LD(uintptr_t, ((uint8_t *)&gm_l26) + 8);
                                        if (t113 >= (uintptr_t)(t114)) gm_trap_index(t113, t114, gm_srclv0dcbbfhves, 872, 67);
                                        gm_tl874t2atnlh3 t115 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l26)) + t113 * 88) + 72));
                                        gm_b16_8 t116;
                                        GM_ST(void *, (uint8_t *)&t116, (void *)(gm_shud0j6jb8ucc));
                                        GM_ST(uintptr_t, (uint8_t *)&t116 + 8, (uintptr_t)(36));
                                        gm_b0_1 t117 = gm_f_unsupported_t7lgvp9((void *)NULL, cx, NULL, t112, t115, t116);
                                        if (gm_unw.kind) {
                                            goto gm_ll29;
                                        }
                                    }
                                    goto ix105;
ie105:;
ix105:;
                                }
                                {
                                    gm_t08olq97rscn3 t118;
                                    memset((uint8_t *)&t118 + 28, 0, 4);
                                    gm_b16_8 t119 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l27) + 0));
                                    gm_b16_8 t120 = t119;
                                    gm_b16_8 t121 = GM_LD(gm_b16_8, ((uint8_t *)&t120));
                                    GM_ST(gm_b16_8, (uint8_t *)&t118 + 0, t121);
                                    gm_tl874t2atnlh3 t122 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l19) + 56));
                                    GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t118 + 16, t122);
                                    gm_t1s75i08qtptd t123;
                                    GM_ST(uint8_t, (uint8_t *)&t123 + 33, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t123 + 34, 0);
                                    GM_ST(uint32_t, (uint8_t *)&t123 + 36, 0);
                                    GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t123), t118);
                                    ((uint8_t *)&t123)[32] = 1;
                                    F.l14 = t123;
                                }
                                {
                                    gm_l25 = ((uint8_t)1);
                                }
                                goto ix96;
ie96:;
ix96:;
                            }
                            {
                                uint8_t t125 = gm_l25;
                                uint8_t t126 = (uint8_t)!t125;
                                if (!t126) goto ie124;
                                {
                                    void * t128 = gm_l0;
                                    gm_tl874t2atnlh3 t129 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l19) + 56));
                                    gm_b16_8 t130;
                                    GM_ST(void *, (uint8_t *)&t130, (void *)(gm_srsiesc4al3v4));
                                    GM_ST(uintptr_t, (uint8_t *)&t130 + 8, (uintptr_t)(49));
                                    gm_b0_1 t131 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t128, t129, t130);
                                    if (gm_unw.kind) {
                                        goto gm_ll29;
                                    }
                                }
                                goto ix124;
ie124:;
ix124:;
                            }
                            goto me38;
mn94:;
                            if (!((GM_LD(uintptr_t, ((((uint8_t *)&F.l19) + 0) + 0) + 8) == 9) && memcmp(GM_LD(void *, ((((uint8_t *)&F.l19) + 0) + 0)), gm_s1ft2nrvnhcbp, 9) == 0)) goto mn132;
                            {
                                if (!(((((uint8_t *)&F.l19) + 32))[16] != 0)) goto ie134;
                                gm_b16_8 t135 = GM_LD(gm_b16_8, (((uint8_t *)&F.l19) + 32));
                                gm_l28 = t135;
                                uint64_t t136 = GM_LD(uintptr_t, ((uint8_t *)&gm_l28) + 8);
                                uint8_t t137 = (t136 > ((uint64_t)0ULL));
                                if (!t137) goto ie134;
                                {
                                    void * t139 = gm_l0;
                                    gm_tl874t2atnlh3 t140 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l19) + 56));
                                    gm_b16_8 t141;
                                    GM_ST(void *, (uint8_t *)&t141, (void *)(gm_sj2f7h0km68ut));
                                    GM_ST(uintptr_t, (uint8_t *)&t141 + 8, (uintptr_t)(31));
                                    gm_b0_1 t142 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t139, t140, t141);
                                    if (gm_unw.kind) {
                                        goto gm_ll29;
                                    }
                                }
                                goto ix134;
ie134:;
ix134:;
                            }
                            {
                                gm_tl874t2atnlh3 t143 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l19) + 56));
                                gm_toceffb697p0u t144;
                                GM_ST(uint8_t, (uint8_t *)&t144 + 13, 0);
                                GM_ST(uint16_t, (uint8_t *)&t144 + 14, 0);
                                GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t144), t143);
                                ((uint8_t *)&t144)[12] = 1;
                                gm_l16 = t144;
                            }
                            goto me38;
mn132:;
                            if (!((GM_LD(uintptr_t, ((((uint8_t *)&F.l19) + 0) + 0) + 8) == 8) && memcmp(GM_LD(void *, ((((uint8_t *)&F.l19) + 0) + 0)), gm_spvg0rt0dloah, 8) == 0)) goto mn145;
                            {
                                uint8_t t148;
                                if (((((uint8_t *)&gm_l15))[16] != 0)) goto mn150;
                                t148 = ((uint8_t)0);
                                goto me149;
mn150:;
                                t148 = ((uint8_t)1);
                                goto me149;
mn151:;
                                gm_trap("no match arm matched the scrutinee", gm_srclv0dcbbfhves, 883, 20);
me149:;
                                if (!t148) goto ie147;
                                {
                                    void * t153 = gm_l0;
                                    gm_tl874t2atnlh3 t154 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l19) + 56));
                                    gm_b16_8 t155;
                                    GM_ST(void *, (uint8_t *)&t155, (void *)(gm_str26682vod5l));
                                    GM_ST(uintptr_t, (uint8_t *)&t155 + 8, (uintptr_t)(37));
                                    gm_b0_1 t156 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t153, t154, t155);
                                    if (gm_unw.kind) {
                                        goto gm_ll29;
                                    }
                                }
                                goto ix147;
ie147:;
ix147:;
                            }
                            {
                                gm_t84dv0cg48dut t157;
                                memset(&t157, 0, sizeof t157);
                                gm_l15 = t157;
                            }
                            {
                                void * t159 = gm_l0;
                                void * t160 = (void *)((uint8_t *)&F.l19);
                                gm_tqs5p4n2tq19v t161 = gm_f_callconv_attr_dd08ovc((void *)NULL, cx, NULL, t159, t160);
                                if (gm_unw.kind) {
                                    goto gm_ll29;
                                }
                                gm_tqs5p4n2tq19v t162 = t161;
                                if (!((((uint8_t *)&t162))[1] != 0)) goto ie158;
                                uint8_t t163 = GM_LD(uint8_t, ((uint8_t *)&t162));
                                gm_l29 = t163;
                                {
                                    uint8_t t165 = gm_l29;
                                    gm_tl874t2atnlh3 t166 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l19) + 56));
                                    gm_tbvif938ns15l t167;
                                    GM_ST(uint8_t, (uint8_t *)&t167 + 1, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t167 + 2, 0);
                                    GM_ST(uint8_t, ((uint8_t *)&t167 + 0), t165);
                                    GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t167 + 4), t166);
                                    gm_t84dv0cg48dut t168;
                                    GM_ST(uint8_t, (uint8_t *)&t168 + 17, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t168 + 18, 0);
                                    GM_ST(gm_tbvif938ns15l, ((uint8_t *)&t168), t167);
                                    ((uint8_t *)&t168)[16] = 1;
                                    gm_l15 = t168;
                                }
                                goto ix158;
ie158:;
ix158:;
                            }
                            goto me38;
mn145:;
                            if (!((GM_LD(uintptr_t, ((((uint8_t *)&F.l19) + 0) + 0) + 8) == 6) && memcmp(GM_LD(void *, ((((uint8_t *)&F.l19) + 0) + 0)), gm_s9e3360r5ee0o, 6) == 0)) goto pa171;
                            goto po170;
pa171:;
                            if (!((GM_LD(uintptr_t, ((((uint8_t *)&F.l19) + 0) + 0) + 8) == 8) && memcmp(GM_LD(void *, ((((uint8_t *)&F.l19) + 0) + 0)), gm_s7f3pdsji9g3a, 8) == 0)) goto pa172;
                            goto po170;
pa172:;
                            if (!((GM_LD(uintptr_t, ((((uint8_t *)&F.l19) + 0) + 0) + 8) == 4) && memcmp(GM_LD(void *, ((((uint8_t *)&F.l19) + 0) + 0)), gm_sq4sf3euttrc8, 4) == 0)) goto pa173;
                            goto po170;
pa173:;
                            if (!((GM_LD(uintptr_t, ((((uint8_t *)&F.l19) + 0) + 0) + 8) == 9) && memcmp(GM_LD(void *, ((((uint8_t *)&F.l19) + 0) + 0)), gm_sinoph5a0op58, 9) == 0)) goto pa174;
                            goto po170;
pa174:;
                            goto mn169;
po170:;
                            {
                                gm_b16_8 t177 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l19) + 0) + 0));
                                gm_t6j6smj7qoctu t178;
                                gm_b16_8 t179;
                                GM_ST(void *, (uint8_t *)&t179, (void *)(gm_s9e3360r5ee0o));
                                GM_ST(uintptr_t, (uint8_t *)&t179 + 8, (uintptr_t)(6));
                                GM_ST(gm_b16_8, (uint8_t *)&t178 + 0, t179);
                                gm_b16_8 t180;
                                GM_ST(void *, (uint8_t *)&t180, (void *)(gm_s7f3pdsji9g3a));
                                GM_ST(uintptr_t, (uint8_t *)&t180 + 8, (uintptr_t)(8));
                                GM_ST(gm_b16_8, (uint8_t *)&t178 + 16, t180);
                                gm_t6j6smj7qoctu t181 = t178;
                                uint8_t t182 = 0;
                                gm_b16_8 t183 = t177;
                                if (gm_eq0iqiubfm7e(((uint8_t *)&t181) + 0, ((uint8_t *)&t183))) t182 = 1;
                                if (gm_eq0iqiubfm7e(((uint8_t *)&t181) + 16, ((uint8_t *)&t183))) t182 = 1;
                                uint8_t t184 = (uint8_t)(t182);
                                if (!t184) goto ie176;
                                {
                                    uint8_t t186 = (((GM_LD(uintptr_t, ((((uint8_t *)&F.l19) + 0) + 0) + 8) == 6) && memcmp(GM_LD(void *, ((((uint8_t *)&F.l19) + 0) + 0)), gm_s9e3360r5ee0o, 6) == 0));
                                    gm_l11 = t186;
                                }
                                {
                                    uint8_t t187 = gm_l11;
                                    gm_tqs5p4n2tq19v t188;
                                    GM_ST(uint8_t, ((uint8_t *)&t188), t187);
                                    ((uint8_t *)&t188)[1] = 1;
                                    GM_ST(gm_tqs5p4n2tq19v, (((uint8_t *)&gm_l12) + 4), t188);
                                }
                                goto ix176;
ie176:;
                                uint8_t t190 = (((GM_LD(uintptr_t, ((((uint8_t *)&F.l19) + 0) + 0) + 8) == 4) && memcmp(GM_LD(void *, ((((uint8_t *)&F.l19) + 0) + 0)), gm_sq4sf3euttrc8, 4) == 0));
                                if (!t190) goto ie189;
                                {
                                    GM_ST(uint8_t, (((uint8_t *)&gm_l12) + 6), ((uint8_t)1));
                                }
                                goto ix189;
ie189:;
                                {
                                    GM_ST(uint8_t, (((uint8_t *)&gm_l12) + 7), ((uint8_t)1));
                                }
ix189:;
ix176:;
                            }
                            {
                                gm_tkacitepg5fk0 t193 = F.l19;
                                gm_tcus0chk5r9uq t194;
                                GM_ST(uint8_t, (uint8_t *)&t194 + 73, 0);
                                GM_ST(uint16_t, (uint8_t *)&t194 + 74, 0);
                                GM_ST(uint32_t, (uint8_t *)&t194 + 76, 0);
                                GM_ST(gm_tkacitepg5fk0, ((uint8_t *)&t194), t193);
                                ((uint8_t *)&t194)[72] = 1;
                                F.l10 = t194;
                            }
                            goto me38;
mn169:;
                            if (!((GM_LD(uintptr_t, ((((uint8_t *)&F.l19) + 0) + 0) + 8) == 5) && memcmp(GM_LD(void *, ((((uint8_t *)&F.l19) + 0) + 0)), gm_slb6cj7ndgvdm, 5) == 0)) goto mn195;
                            void * t196 = gm_l0;
                            void * t197 = (void *)((uint8_t *)&F.l19);
                            gm_b0_1 t198 = gm_f_check_allow_attr_3321gm7((void *)NULL, cx, NULL, t196, t197);
                            if (gm_unw.kind) {
                                goto gm_ll29;
                            }
                            goto me38;
mn195:;
                            if (!((GM_LD(uintptr_t, ((((uint8_t *)&F.l19) + 0) + 0) + 8) == 10) && memcmp(GM_LD(void *, ((((uint8_t *)&F.l19) + 0) + 0)), gm_smo0niiqr9onn, 10) == 0)) goto mn199;
                            {
                                gm_l30 = ((uint8_t)1);
                            }
                            {
                                if (!(((((uint8_t *)&F.l19) + 32))[16] != 0)) goto ie201;
                                gm_b16_8 t202 = GM_LD(gm_b16_8, (((uint8_t *)&F.l19) + 32));
                                gm_l31 = t202;
                                {
                                    uint8_t t204 = 0;
                                    uint64_t t205 = GM_LD(uintptr_t, ((uint8_t *)&gm_l31) + 8);
                                    uint8_t t206 = ((t205 == ((uint64_t)1ULL)));
                                    if (t206) {
                                        uint64_t t207 = (uintptr_t)((uint64_t)0ULL);
                                        uint64_t t208 = GM_LD(uintptr_t, ((uint8_t *)&gm_l31) + 8);
                                        if (t207 >= (uintptr_t)(t208)) gm_trap_index(t207, t208, gm_srclv0dcbbfhves, 905, 44);
                                        uint8_t t209;
                                        uint8_t t212 = GM_LD(uint8_t, ((((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l31)) + t207 * 88) + 8) + 0));
                                        if (t212 != ((uint8_t)2ULL)) goto mn211;
                                        t209 = ((uint8_t)1);
                                        goto me210;
mn211:;
                                        t209 = ((uint8_t)0);
                                        goto me210;
mn213:;
                                        gm_trap("no match arm matched the scrutinee", gm_srclv0dcbbfhves, 905, 44);
me210:;
                                        t204 = t209;
                                    } else t204 = 0;
                                    gm_l30 = t204;
                                }
                                goto ix201;
ie201:;
ix201:;
                            }
                            {
                                uint8_t t215 = gm_l30;
                                uint8_t t216 = (uint8_t)!t215;
                                if (!t216) goto ie214;
                                {
                                    void * t218 = gm_l0;
                                    gm_tl874t2atnlh3 t219 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l19) + 56));
                                    gm_b16_8 t220;
                                    GM_ST(void *, (uint8_t *)&t220, (void *)(gm_sppcsr92ukfol));
                                    GM_ST(uintptr_t, (uint8_t *)&t220 + 8, (uintptr_t)(63));
                                    gm_b0_1 t221 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t218, t219, t220);
                                    if (gm_unw.kind) {
                                        goto gm_ll29;
                                    }
                                }
                                goto ix214;
ie214:;
ix214:;
                            }
                            goto me38;
mn199:;
                            if (!((GM_LD(uintptr_t, ((((uint8_t *)&F.l19) + 0) + 0) + 8) == 6) && memcmp(GM_LD(void *, ((((uint8_t *)&F.l19) + 0) + 0)), gm_s2ph9f020upd2, 6) == 0)) goto pa224;
                            goto po223;
pa224:;
                            if (!((GM_LD(uintptr_t, ((((uint8_t *)&F.l19) + 0) + 0) + 8) == 4) && memcmp(GM_LD(void *, ((((uint8_t *)&F.l19) + 0) + 0)), gm_s6vvtrmtnt3j1, 4) == 0)) goto pa225;
                            goto po223;
pa225:;
                            if (!((GM_LD(uintptr_t, ((((uint8_t *)&F.l19) + 0) + 0) + 8) == 7) && memcmp(GM_LD(void *, ((((uint8_t *)&F.l19) + 0) + 0)), gm_suuv0qpi4v4e4, 7) == 0)) goto pa226;
                            goto po223;
pa226:;
                            goto mn222;
po223:;
                            void * t227 = gm_l0;
                            gm_tl874t2atnlh3 t228 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l19) + 56));
                            gm_b16_8 t229 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l19) + 0) + 0));
                            gm_tnpifv20irnph t230;
                            GM_ST(gm_b16_8, ((uint8_t *)&t230 + 0), t229);
                            gm_tabb710d9mq2e t231;
                            gm_f_format_0dvj28f((void *)&t231, (void *)NULL, cx, NULL, t230);
                            if (gm_unw.kind) {
                                goto gm_ll29;
                            }
                            gm_tabb710d9mq2e t232 = t231;
                            if (((((uint8_t *)&t232))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t232) + 8, gm_srclv0dcbbfhves, 909, 63);
                            gm_b16_8 t233 = GM_LD(gm_b16_8, (((uint8_t *)&t232) + 8));
                            gm_b16_8 t234 = t233;
                            gm_b16_8 t235 = GM_LD(gm_b16_8, ((uint8_t *)&t234));
                            gm_b0_1 t236 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t227, t228, t235);
                            if (gm_unw.kind) {
                                goto gm_ll29;
                            }
                            goto me38;
mn222:;
                            if (!((GM_LD(uintptr_t, ((((uint8_t *)&F.l19) + 0) + 0) + 8) == 9) && memcmp(GM_LD(void *, ((((uint8_t *)&F.l19) + 0) + 0)), gm_s80p0ld1j6rfg, 9) == 0)) goto mn237;
                            void * t238 = gm_l0;
                            gm_tl874t2atnlh3 t239 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l19) + 56));
                            gm_b16_8 t240;
                            GM_ST(void *, (uint8_t *)&t240, (void *)(gm_sg7i6m1frjk5n));
                            GM_ST(uintptr_t, (uint8_t *)&t240 + 8, (uintptr_t)(62));
                            gm_b0_1 t241 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t238, t239, t240);
                            if (gm_unw.kind) {
                                goto gm_ll29;
                            }
                            goto me38;
mn237:;
                            gm_b16_8 t243 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l19) + 0) + 0));
                            gm_l32 = t243;
                            void * t244 = gm_l0;
                            gm_tl874t2atnlh3 t245 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l19) + 56));
                            gm_b16_8 t246 = gm_l32;
                            gm_tnpifv20irnph t247;
                            GM_ST(gm_b16_8, ((uint8_t *)&t247 + 0), t246);
                            gm_tabb710d9mq2e t248;
                            gm_f_format_u2mas3v((void *)&t248, (void *)NULL, cx, NULL, t247);
                            if (gm_unw.kind) {
                                goto gm_ll29;
                            }
                            gm_tabb710d9mq2e t249 = t248;
                            if (((((uint8_t *)&t249))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t249) + 8, gm_srclv0dcbbfhves, 911, 41);
                            gm_b16_8 t250 = GM_LD(gm_b16_8, (((uint8_t *)&t249) + 8));
                            gm_b16_8 t251 = t250;
                            gm_b16_8 t252 = GM_LD(gm_b16_8, ((uint8_t *)&t251));
                            gm_b0_1 t253 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t244, t245, t252);
                            if (gm_unw.kind) {
                                goto gm_ll29;
                            }
                            goto me38;
mn242:;
                            gm_trap("no match arm matched the scrutinee", gm_srclv0dcbbfhves, 845, 9);
me38:;
                        }
                        goto me34;
mn35:;
                        gm_trap("no match arm matched the scrutinee", gm_srclv0dcbbfhves, 844, 5);
me34:;
                    }
lc25:;
                    {
                        uint64_t t255 = gm_l18;
                        uint64_t r256; bool o257 = __builtin_add_overflow(t255, ((uint64_t)1ULL), &r256);
                        uint64_t t258 = r256;
                        gm_l18 = t258;
                    }
                }
gm_ll29:;
                if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb25; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc25; }
                }
                goto gm_lad;
lb25:;
            }
        }
        {
            uint8_t t260 = gm_l8;
            if (!t260) goto ie259;
            uint8_t t261 = gm_l7;
            uint8_t t262 = (uint8_t)!t261;
            if (!t262) goto ie259;
            {
                void * t264 = gm_l0;
                gm_tl874t2atnlh3 t265 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l5) + 16));
                gm_b16_8 t266;
                GM_ST(void *, (uint8_t *)&t266, (void *)(gm_s8ro53eq54s60));
                GM_ST(uintptr_t, (uint8_t *)&t266 + 8, (uintptr_t)(43));
                gm_b0_1 t267 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t264, t265, t266);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix259;
ie259:;
ix259:;
        }
        {
            gm_tf5srh4mp4b3i t268;
            memset(&t268, 0, sizeof t268);
            gm_l33 = t268;
        }
        {
            if (!((((uint8_t *)&F.l13))[72] != 0)) goto ie269;
            gm_tkacitepg5fk0 t270 = GM_LD(gm_tkacitepg5fk0, ((uint8_t *)&F.l13));
            F.l34 = t270;
            {
                uint8_t t273 = gm_l7;
                if (!t273) goto ie272;
                {
                    void * t275 = gm_l0;
                    uint64_t t276 = gm_l4;
                    void * t277 = (void *)((uint8_t *)&F.l34);
                    gm_tf5srh4mp4b3i t278 = gm_f_attr_power_of_two_gj02n2t((void *)NULL, cx, NULL, t275, t276, t277);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l33 = t278;
                }
                goto ix272;
ie272:;
                {
                    void * t280 = gm_l0;
                    gm_tl874t2atnlh3 t281 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l34) + 56));
                    gm_b16_8 t282;
                    GM_ST(void *, (uint8_t *)&t282, (void *)(gm_sbqv06vnui8sl));
                    GM_ST(uintptr_t, (uint8_t *)&t282 + 8, (uintptr_t)(87));
                    gm_b0_1 t283 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t280, t281, t282);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
ix272:;
            }
            goto ix269;
ie269:;
ix269:;
        }
        {
            gm_t6qooom8qa2do t284;
            memset(&t284, 0, sizeof t284);
            GM_ST(uint8_t, ((uint8_t *)&t284 + 0), ((uint8_t)11ULL));
            F.l35 = t284;
        }
        {
            void * t285 = gm_l0;
            uint64_t t286 = GM_LD(uint64_t, (((uint8_t *)t285) + 2312));
            gm_l36 = t286;
        }
        {
            void * t287 = gm_l0;
            gm_tf5srh4mp4b3i t288 = gm_l9;
            gm_tf5srh4mp4b3i t289 = t288;
            uint64_t t290;
            if (((((uint8_t *)&t289))[8] != 0)) {
                uint64_t t291 = GM_LD(uint64_t, (((uint8_t *)&t289) + 0));
                t290 = t291;
            } else {
                uint64_t t292 = GM_LD(uint64_t, gm_c1n574q3spi);
                t290 = t292;
            }
            GM_ST(uint64_t, (((uint8_t *)t287) + 2312), t290);
        }
        {
            uint8_t t294 = gm_l7;
            if (!t294) goto ie293;
            {
                void * t296 = gm_l0;
                uint64_t t297 = gm_l4;
                void * t298 = (void *)((uint8_t *)&F.l6);
                gm_f_declare_global_729nttf((void *)&(*(gm_t6qooom8qa2do *)((uint8_t *)&F.l35)), (void *)NULL, cx, NULL, t296, t297, &(*(gm_t08olq97rscn3 *)((uint8_t *)&gm_l5)), t298);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                uint8_t t300 = GM_LD(uint8_t, (((uint8_t *)&F.l35) + 0));
                if (t300 != ((uint8_t)10ULL)) goto ie299;
                gm_t88rcucg1b7uk t301 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l35) + 8));
                gm_l37 = t301;
                {
                    void * t303 = gm_l0;
                    void * t304 = (void *)(((uint8_t *)t303) + 1448);
                    gm_tq7qjpkmqmc5s t305;
                    memset((uint8_t *)&t305 + 28, 0, 4);
                    memset((uint8_t *)&t305 + 49, 0, 7);
                    uint64_t t306 = gm_l1;
                    GM_ST(uint64_t, (uint8_t *)&t305 + 0, t306);
                    gm_b16_8 t307 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l5) + 0));
                    GM_ST(gm_b16_8, (uint8_t *)&t305 + 8, t307);
                    gm_t88rcucg1b7uk t308 = gm_l37;
                    GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t305 + 24, t308);
                    uint64_t t309 = gm_l4;
                    GM_ST(uint64_t, (uint8_t *)&t305 + 32, t309);
                    void * t310 = GM_LD(void *, (((uint8_t *)&F.l6) + 40));
                    GM_ST(void *, (uint8_t *)&t305 + 40, t310);
                    uint8_t t311 = gm_l8;
                    GM_ST(uint8_t, (uint8_t *)&t305 + 48, t311);
                    gm_th2lqbo77pvsc t312;
                    memset(&t312, 0, sizeof t312);
                    GM_ST(gm_th2lqbo77pvsc, (uint8_t *)&t305 + 56, t312);
                    gm_tf5srh4mp4b3i t313 = gm_l33;
                    GM_ST(gm_tf5srh4mp4b3i, (uint8_t *)&t305 + 80, t313);
                    GM_ST(void *, (uint8_t *)&t305 + 96, ((void *)0));
                    gm_tftb1odc739t5 t314 = gm_f_push_4p3ivmb(t304, &t305);
                    gm_tftb1odc739t5 t315 = t314;
                    if (((((uint8_t *)&t315))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t315) + 1, gm_srclv0dcbbfhves, 929, 13);
                    gm_b0_1 t316 = GM_LD(gm_b0_1, (((uint8_t *)&t315) + 1));
                }
                goto ix299;
ie299:;
ix299:;
            }
            goto ix293;
ie293:;
            {
                void * t318 = gm_l0;
                uint64_t t319 = gm_l1;
                uint64_t t320 = gm_l4;
                void * t321 = (void *)((uint8_t *)&F.l6);
                gm_f_compute_binding_cl3ogob((void *)&(*(gm_t6qooom8qa2do *)((uint8_t *)&F.l35)), (void *)NULL, cx, NULL, t318, t319, t320, &(*(gm_t08olq97rscn3 *)((uint8_t *)&gm_l5)), t321);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
ix293:;
        }
        {
            void * t322 = gm_l0;
            uint64_t t323 = gm_l36;
            GM_ST(uint64_t, (((uint8_t *)t322) + 2312), t323);
        }
        {
            if (!((((uint8_t *)&gm_l15))[16] != 0)) goto ie324;
            gm_tbvif938ns15l t325 = GM_LD(gm_tbvif938ns15l, ((uint8_t *)&gm_l15));
            gm_l38 = t325;
            {
                uint8_t t332 = GM_LD(uint8_t, (((uint8_t *)&F.l35) + 0));
                switch (t332) { case ((uint8_t)0ULL): goto ma330; case ((uint8_t)1ULL): goto ma330; case ((uint8_t)2ULL): goto ma330; case ((uint8_t)3ULL): goto ma330; case ((uint8_t)4ULL): goto ma330; case ((uint8_t)5ULL): goto ma328; case ((uint8_t)6ULL): goto ma330; case ((uint8_t)7ULL): goto ma330; case ((uint8_t)8ULL): goto ma330; case ((uint8_t)9ULL): goto ma330; case ((uint8_t)10ULL): goto ma330; case ((uint8_t)11ULL): goto ma329; default: goto ma330; }
ma328:;
                uint8_t t334 = GM_LD(uint8_t, (((uint8_t *)&F.l35) + 0));
                if (t334 != ((uint8_t)5ULL)) goto mn333;
                uint32_t t335 = GM_LD(uint32_t, (((uint8_t *)&F.l35) + 8));
                gm_l39 = t335;
                void * t336 = gm_l0;
                uint32_t t337 = gm_l39;
                uint8_t t338 = GM_LD(uint8_t, (((uint8_t *)&gm_l38) + 0));
                gm_tl874t2atnlh3 t339 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l38) + 4));
                gm_b0_1 t340 = gm_f_apply_callconv_197l87p((void *)NULL, cx, NULL, t336, t337, t338, t339);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                goto me327;
mn333:;
ma329:;
                uint8_t t342 = GM_LD(uint8_t, (((uint8_t *)&F.l35) + 0));
                if (t342 != ((uint8_t)11ULL)) goto mn341;
                goto me327;
mn341:;
ma330:;
                void * t345 = gm_l0;
                gm_tl874t2atnlh3 t346 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l38) + 4));
                gm_b16_8 t347;
                GM_ST(void *, (uint8_t *)&t347, (void *)(gm_srb1158jlk9u7));
                GM_ST(uintptr_t, (uint8_t *)&t347 + 8, (uintptr_t)(74));
                gm_b0_1 t348 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t345, t346, t347);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                goto me327;
mn344:;
ma331:;
                gm_trap("no match arm matched the scrutinee", gm_srclv0dcbbfhves, 936, 9);
me327:;
            }
            goto ix324;
ie324:;
ix324:;
        }
        {
            if (!((((uint8_t *)&F.l10))[72] != 0)) goto ie349;
            gm_tkacitepg5fk0 t350 = GM_LD(gm_tkacitepg5fk0, ((uint8_t *)&F.l10));
            F.l40 = t350;
            {
                uint8_t t353 = GM_LD(uint8_t, (((uint8_t *)&F.l35) + 0));
                if (t353 != ((uint8_t)5ULL)) goto ie352;
                uint32_t t354 = GM_LD(uint32_t, (((uint8_t *)&F.l35) + 8));
                gm_l41 = t354;
                uint8_t t355 = gm_l11;
                if (!t355) goto ie352;
                {
                    void * t357 = gm_l0;
                    void * t358 = (void *)(((uint8_t *)t357) + 800);
                    uint32_t t359 = gm_l41;
                    gm_tl874t2atnlh3 t360 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l40) + 56));
                    gm_t9d4sf0vqn7dt t361;
                    GM_ST(uint32_t, ((uint8_t *)&t361 + 0), t359);
                    GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t361 + 4), t360);
                    gm_tftb1odc739t5 t362 = gm_f_push_89d0k90(t358, GM_CONV(gm_csedogq9e4rp5r, t361));
                    gm_tftb1odc739t5 t363 = t362;
                    if (((((uint8_t *)&t363))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t363) + 1, gm_srclv0dcbbfhves, 943, 37);
                    gm_b0_1 t364 = GM_LD(gm_b0_1, (((uint8_t *)&t363) + 1));
                }
                goto ix352;
ie352:;
ix352:;
            }
            {
                uint8_t t366 = GM_LD(uint8_t, (((uint8_t *)&F.l35) + 0));
                if (t366 != ((uint8_t)5ULL)) goto ie365;
                uint32_t t367 = GM_LD(uint32_t, (((uint8_t *)&F.l35) + 8));
                gm_l42 = t367;
                {
                    uint32_t t369 = gm_l42;
                    GM_ST(uint32_t, (((uint8_t *)&gm_l12) + 0), t369);
                }
                {
                    void * t370 = gm_l0;
                    void * t371 = (void *)(((uint8_t *)t370) + 840);
                    gm_tbcgm40aljf9h t372 = gm_l12;
                    gm_tftb1odc739t5 t373 = gm_f_push_0fe09r3(t371, t372);
                    gm_tftb1odc739t5 t374 = t373;
                    if (((((uint8_t *)&t374))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t374) + 1, gm_srclv0dcbbfhves, 946, 13);
                    gm_b0_1 t375 = GM_LD(gm_b0_1, (((uint8_t *)&t374) + 1));
                }
                goto ix365;
ie365:;
ix365:;
            }
            {
                uint8_t t377;
                uint8_t t380 = GM_LD(uint8_t, (((uint8_t *)&F.l35) + 0));
                if (t380 != ((uint8_t)5ULL)) goto mn379;
                t377 = ((uint8_t)0);
                goto me378;
mn379:;
                t377 = ((uint8_t)1);
                goto me378;
mn381:;
                gm_trap("no match arm matched the scrutinee", gm_srclv0dcbbfhves, 948, 12);
me378:;
                if (!t377) goto ie376;
                uint8_t t382 = (!(GM_LD(uint8_t, ((uint8_t *)&F.l35) + 0) == ((uint8_t)11ULL)));
                if (!t382) goto ie376;
                {
                    void * t384 = gm_l0;
                    gm_tl874t2atnlh3 t385 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l40) + 56));
                    gm_b16_8 t386 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l40) + 0) + 0));
                    gm_tnpifv20irnph t387;
                    GM_ST(gm_b16_8, ((uint8_t *)&t387 + 0), t386);
                    gm_tabb710d9mq2e t388;
                    gm_f_format_28p84ds((void *)&t388, (void *)NULL, cx, NULL, t387);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tabb710d9mq2e t389 = t388;
                    if (((((uint8_t *)&t389))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t389) + 8, gm_srclv0dcbbfhves, 949, 29);
                    gm_b16_8 t390 = GM_LD(gm_b16_8, (((uint8_t *)&t389) + 8));
                    gm_b16_8 t391 = t390;
                    gm_b16_8 t392 = GM_LD(gm_b16_8, ((uint8_t *)&t391));
                    gm_b0_1 t393 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t384, t385, t392);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                goto ix376;
ie376:;
ix376:;
            }
            goto ix349;
ie349:;
ix349:;
        }
        {
            if (!((((uint8_t *)&gm_l16))[12] != 0)) goto ie394;
            gm_tl874t2atnlh3 t395 = GM_LD(gm_tl874t2atnlh3, ((uint8_t *)&gm_l16));
            gm_l43 = t395;
            {
                uint8_t t402 = GM_LD(uint8_t, (((uint8_t *)&F.l35) + 0));
                switch (t402) { case ((uint8_t)0ULL): goto ma400; case ((uint8_t)1ULL): goto ma400; case ((uint8_t)2ULL): goto ma400; case ((uint8_t)3ULL): goto ma400; case ((uint8_t)4ULL): goto ma400; case ((uint8_t)5ULL): goto ma398; case ((uint8_t)6ULL): goto ma400; case ((uint8_t)7ULL): goto ma400; case ((uint8_t)8ULL): goto ma400; case ((uint8_t)9ULL): goto ma400; case ((uint8_t)10ULL): goto ma400; case ((uint8_t)11ULL): goto ma399; default: goto ma400; }
ma398:;
                uint8_t t404 = GM_LD(uint8_t, (((uint8_t *)&F.l35) + 0));
                if (t404 != ((uint8_t)5ULL)) goto mn403;
                uint32_t t405 = GM_LD(uint32_t, (((uint8_t *)&F.l35) + 8));
                gm_l44 = t405;
                void * t406 = gm_l0;
                uint32_t t407 = gm_l44;
                gm_tl874t2atnlh3 t408 = gm_l43;
                gm_b0_1 t409 = gm_f_trap_hook_fn_5c0a4ds((void *)NULL, cx, NULL, t406, t407, t408);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                goto me397;
mn403:;
ma399:;
                uint8_t t411 = GM_LD(uint8_t, (((uint8_t *)&F.l35) + 0));
                if (t411 != ((uint8_t)11ULL)) goto mn410;
                goto me397;
mn410:;
ma400:;
                void * t414 = gm_l0;
                gm_tl874t2atnlh3 t415 = gm_l43;
                gm_b16_8 t416;
                GM_ST(void *, (uint8_t *)&t416, (void *)(gm_sv48vll3qortv));
                GM_ST(uintptr_t, (uint8_t *)&t416 + 8, (uintptr_t)(39));
                gm_b0_1 t417 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t414, t415, t416);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                goto me397;
mn413:;
ma401:;
                gm_trap("no match arm matched the scrutinee", gm_srclv0dcbbfhves, 953, 9);
me397:;
            }
            goto ix394;
ie394:;
ix394:;
        }
        {
            if (!((((uint8_t *)&F.l14))[32] != 0)) goto ie418;
            gm_t08olq97rscn3 t419 = GM_LD(gm_t08olq97rscn3, ((uint8_t *)&F.l14));
            gm_l45 = t419;
            {
                uint8_t t426 = GM_LD(uint8_t, (((uint8_t *)&F.l35) + 0));
                switch (t426) { case ((uint8_t)0ULL): goto ma424; case ((uint8_t)1ULL): goto ma424; case ((uint8_t)2ULL): goto ma424; case ((uint8_t)3ULL): goto ma424; case ((uint8_t)4ULL): goto ma424; case ((uint8_t)5ULL): goto ma422; case ((uint8_t)6ULL): goto ma424; case ((uint8_t)7ULL): goto ma424; case ((uint8_t)8ULL): goto ma424; case ((uint8_t)9ULL): goto ma424; case ((uint8_t)10ULL): goto ma424; case ((uint8_t)11ULL): goto ma423; default: goto ma424; }
ma422:;
                uint8_t t428 = GM_LD(uint8_t, (((uint8_t *)&F.l35) + 0));
                if (t428 != ((uint8_t)5ULL)) goto mn427;
                uint32_t t429 = GM_LD(uint32_t, (((uint8_t *)&F.l35) + 8));
                gm_l46 = t429;
                void * t430 = gm_l0;
                uint32_t t431 = gm_l46;
                gm_b16_8 t432 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l45) + 0));
                gm_tl874t2atnlh3 t433 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l45) + 16));
                gm_b0_1 t434 = gm_f_export_fn_8hcuasc((void *)NULL, cx, NULL, t430, t431, t432, t433);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                goto me421;
mn427:;
ma423:;
                uint8_t t436 = GM_LD(uint8_t, (((uint8_t *)&F.l35) + 0));
                if (t436 != ((uint8_t)11ULL)) goto mn435;
                goto me421;
mn435:;
ma424:;
                void * t439 = gm_l0;
                gm_tl874t2atnlh3 t440 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l45) + 16));
                gm_b16_8 t441;
                GM_ST(void *, (uint8_t *)&t441, (void *)(gm_sj3lfuqd4f1np));
                GM_ST(uintptr_t, (uint8_t *)&t441 + 8, (uintptr_t)(36));
                gm_b0_1 t442 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t439, t440, t441);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                goto me421;
mn438:;
ma425:;
                gm_trap("no match arm matched the scrutinee", gm_srclv0dcbbfhves, 960, 9);
me421:;
            }
            goto ix418;
ie418:;
ix418:;
        }
        {
            memmove(gm_out, ((uint8_t *)&F.l35), 208);
            goto gm_fdone;
        }
        goto ix11;
ie11:;
        {
            gm_t6qooom8qa2do t444;
            memset(&t444, 0, sizeof t444);
            GM_ST(uint8_t, ((uint8_t *)&t444 + 0), ((uint8_t)11ULL));
            uint8_t t445 = 0;
            F.ret = t444;
            goto gm_fret;
        }
ix11:;
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
extern __attribute__((aligned(4))) gm_t9805qtonbsag gm_f_const_id_2mlh0og(void * a0, gm_cseahvg8v7p0vs a1, gm_csguankko4lmhd a2);
__attribute__((aligned(4))) void gm_f_declare_const_or_type_4jaj4k6(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, const void * a2, void * a3, void * a4) {
    struct gm_fr_declare_const_or_type_4jaj4k6 F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* module */
    gm_t08olq97rscn3 gm_l2; /* name */
    void * gm_l3; /* annot */
    void * gm_l4; /* init */
    gm_tba9024q2up98 gm_l5; /* found */
    uint64_t gm_l6; /* d */
    void * gm_l8; /* callee */
    gm_tba9024q2up98 gm_l9; /* found */
    uint64_t gm_l10; /* d */
    gm_tiilmfd16v3fc gm_l11; /* r */
    gm_t88rcucg1b7uk gm_l12; /* ty */
    gm_tkn42u7na3t81 gm_l13; /* expected */
    void * gm_l14; /* a */
    gm_t88rcucg1b7uk gm_l15; /* t */
    void * gm_l16; /* hook */
    uint32_t gm_l17; /* id */
    gm_t88rcucg1b7uk gm_l18; /* ty */
    gm_t88rcucg1b7uk gm_l19; /* bad */
    GM_STACK_CHECK(&F, 5112u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l3 = a3;
    gm_l4 = a4;
    {
        void * t3 = gm_l3;
        void * t4 = t3;
        uint8_t t5;
        if ((GM_LD(void *, ((uint8_t *)&t4)) != 0)) goto mn7;
        t5 = ((uint8_t)1);
        goto me6;
mn7:;
        t5 = ((uint8_t)0);
        goto me6;
mn8:;
        gm_trap("no match arm matched the scrutinee", gm_srclv0dcbbfhves, 1047, 8);
me6:;
        if (!t5) goto ie2;
        {
            void * t11 = gm_l0;
            uint64_t t12 = gm_l1;
            void * t13 = gm_l4;
            gm_t89h15e2c5o3p t14;
            gm_f_aliased_decl_jv6ilmn((void *)&t14, (void *)NULL, cx, NULL, t11, t12, t13);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t89h15e2c5o3p t15 = t14;
            if (!((((uint8_t *)&t15))[16] != 0)) goto ie10;
            gm_tba9024q2up98 t16 = GM_LD(gm_tba9024q2up98, ((uint8_t *)&t15));
            gm_l5 = t16;
            {
                gm_tf5srh4mp4b3i t18 = GM_LD(gm_tf5srh4mp4b3i, (((uint8_t *)&gm_l5) + 0));
                gm_tf5srh4mp4b3i t19 = t18;
                uint64_t t20;
                if (((((uint8_t *)&t19))[8] != 0)) {
                    uint64_t t21 = GM_LD(uint64_t, (((uint8_t *)&t19) + 0));
                    t20 = t21;
                } else {
                    gm_t6qooom8qa2do t22;
                    memset(&t22, 0, sizeof t22);
                    GM_ST(uint8_t, ((uint8_t *)&t22 + 0), ((uint8_t)11ULL));
                    uint8_t t23 = 0;
                    F.ret = t22;
                    goto gm_fret;
                }
                gm_l6 = t20;
            }
            {
                void * t24 = gm_l0;
                uint64_t t25 = gm_l6;
                gm_f_resolve_decl_pus042v((void *)&F.l7, (void *)NULL, cx, NULL, t24, t25);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                uint8_t t30 = GM_LD(uint8_t, (((uint8_t *)&F.l7) + 0));
                if (t30 != ((uint8_t)7ULL)) goto pa29;
                goto po28;
pa29:;
                uint8_t t32 = GM_LD(uint8_t, (((uint8_t *)&F.l7) + 0));
                if (t32 != ((uint8_t)10ULL)) goto pa31;
                goto po28;
pa31:;
                goto mn27;
po28:;
                goto me26;
mn27:;
                memmove(gm_out, ((uint8_t *)&F.l7), 208);
                goto gm_fdone;
                goto me26;
mn34:;
                gm_trap("no match arm matched the scrutinee", gm_srclv0dcbbfhves, 1052, 13);
me26:;
            }
            goto ix10;
ie10:;
ix10:;
        }
        {
            void * t36 = gm_l4;
            uint8_t t37 = GM_LD(uint8_t, ((((uint8_t *)t36) + 8) + 0));
            if (t37 != ((uint8_t)19ULL)) goto ie35;
            void * t38 = GM_LD(void *, (((((uint8_t *)t36) + 8) + 8) + 0));
            gm_l8 = t38;
            {
                void * t41 = gm_l0;
                uint64_t t42 = gm_l1;
                void * t43 = gm_l8;
                gm_t89h15e2c5o3p t44;
                gm_f_aliased_decl_jv6ilmn((void *)&t44, (void *)NULL, cx, NULL, t41, t42, t43);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_t89h15e2c5o3p t45 = t44;
                if (!((((uint8_t *)&t45))[16] != 0)) goto ie40;
                gm_tba9024q2up98 t46 = GM_LD(gm_tba9024q2up98, ((uint8_t *)&t45));
                gm_l9 = t46;
                {
                    if (!(((((uint8_t *)&gm_l9) + 0))[8] != 0)) goto ie48;
                    uint64_t t49 = GM_LD(uint64_t, (((uint8_t *)&gm_l9) + 0));
                    gm_l10 = t49;
                    {
                        void * t52 = gm_l0;
                        uint64_t t53 = gm_l10;
                        gm_t6qooom8qa2do t54;
                        gm_f_resolve_decl_pus042v((void *)&t54, (void *)NULL, cx, NULL, t52, t53);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_t6qooom8qa2do t55 = t54;
                        uint8_t t56;
                        uint8_t t59 = GM_LD(uint8_t, (((uint8_t *)&t55) + 0));
                        if (t59 != ((uint8_t)2ULL)) goto mn58;
                        t56 = ((uint8_t)1);
                        goto me57;
mn58:;
                        t56 = ((uint8_t)0);
                        goto me57;
mn60:;
                        gm_trap("no match arm matched the scrutinee", gm_srclv0dcbbfhves, 1061, 24);
me57:;
                        if (!t56) goto ie51;
                        {
                            void * t62 = gm_l0;
                            uint64_t t63 = gm_l1;
                            void * t64 = gm_l4;
                            gm_tqu3m2l3n8mnc t65;
                            gm_f_resolve_effect_ref_bggirs2((void *)&t65, (void *)NULL, cx, NULL, t62, t63, t64);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_tqu3m2l3n8mnc t66 = t65;
                            gm_tiilmfd16v3fc t67;
                            if (((((uint8_t *)&t66))[24] != 0)) {
                                gm_tiilmfd16v3fc t68 = GM_LD(gm_tiilmfd16v3fc, (((uint8_t *)&t66) + 0));
                                t67 = t68;
                            } else {
                                gm_t6qooom8qa2do t69;
                                memset(&t69, 0, sizeof t69);
                                GM_ST(uint8_t, ((uint8_t *)&t69 + 0), ((uint8_t)11ULL));
                                uint8_t t70 = 0;
                                F.ret = t69;
                                goto gm_fret;
                            }
                            gm_l11 = t67;
                        }
                        {
                            gm_tiilmfd16v3fc t71 = gm_l11;
                            gm_t6qooom8qa2do t72;
                            memset(&t72, 0, sizeof t72);
                            GM_ST(uint8_t, ((uint8_t *)&t72 + 0), ((uint8_t)3ULL));
                            GM_ST(gm_tiilmfd16v3fc, ((uint8_t *)&t72 + 8), t71);
                            uint8_t t73 = 0;
                            F.ret = t72;
                            goto gm_fret;
                        }
                        goto ix51;
ie51:;
ix51:;
                    }
                    goto ix48;
ie48:;
ix48:;
                }
                goto ix40;
ie40:;
ix40:;
            }
            goto ix35;
ie35:;
ix35:;
        }
        {
            void * t75 = gm_l0;
            uint64_t t76 = gm_l1;
            void * t77 = gm_l4;
            uint8_t t78 = gm_f_looks_like_type_ho5qv1k((void *)NULL, cx, NULL, t75, t76, t77);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (!t78) goto ie74;
            {
                void * t80 = gm_l0;
                uint64_t t81 = gm_l1;
                void * t82 = gm_l4;
                gm_t88rcucg1b7uk t83 = gm_f_resolve_type_jjodbei((void *)NULL, cx, NULL, t80, t81, t82);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l12 = t83;
            }
            {
                gm_t6qooom8qa2do t84;
                void * t86 = gm_l0;
                void * t87 = (void *)(((uint8_t *)t86) + 8);
                gm_t88rcucg1b7uk t88 = gm_l12;
                uint8_t t89 = gm_f_is_error_8k27gd1(t87, GM_CONV(gm_csguankko4lmhd, t88));
                if (!t89) goto ie85;
                gm_t6qooom8qa2do t91;
                memset(&t91, 0, sizeof t91);
                GM_ST(uint8_t, ((uint8_t *)&t91 + 0), ((uint8_t)11ULL));
                gm_t6qooom8qa2do t92 = t91;
                t84 = t92;
                goto ix85;
ie85:;
                gm_t88rcucg1b7uk t94 = gm_l12;
                gm_t6qooom8qa2do t95;
                memset(&t95, 0, sizeof t95);
                GM_ST(uint8_t, ((uint8_t *)&t95 + 0), ((uint8_t)0ULL));
                GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t95 + 8), t94);
                gm_t6qooom8qa2do t96 = t95;
                t84 = t96;
ix85:;
                uint8_t t97 = 0;
                F.ret = t84;
                goto gm_fret;
            }
            goto ix74;
ie74:;
ix74:;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        gm_tkn42u7na3t81 t98;
        memset(&t98, 0, sizeof t98);
        gm_l13 = t98;
    }
    {
        void * t100 = gm_l3;
        void * t101 = t100;
        if (!(GM_LD(void *, ((uint8_t *)&t101)) != 0)) goto ie99;
        void * t102 = GM_LD(void *, ((uint8_t *)&t101));
        gm_l14 = t102;
        {
            void * t104 = gm_l0;
            uint64_t t105 = gm_l1;
            void * t106 = gm_l14;
            gm_t88rcucg1b7uk t107 = gm_f_resolve_type_jjodbei((void *)NULL, cx, NULL, t104, t105, t106);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tkn42u7na3t81 t108;
            GM_ST(uint8_t, (uint8_t *)&t108 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t108 + 6, 0);
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t108), t107);
            ((uint8_t *)&t108)[4] = 1;
            gm_l13 = t108;
        }
        goto ix99;
ie99:;
ix99:;
    }
    {
        if (!((((uint8_t *)&gm_l13))[4] != 0)) goto ie109;
        gm_t88rcucg1b7uk t110 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l13));
        gm_l15 = t110;
        void * t111 = gm_l0;
        void * t112 = (void *)(((uint8_t *)t111) + 8);
        gm_t88rcucg1b7uk t113 = gm_l15;
        uint8_t t114 = gm_f_is_error_8k27gd1(t112, GM_CONV(gm_csguankko4lmhd, t113));
        if (!t114) goto ie109;
        {
            gm_t6qooom8qa2do t116;
            memset(&t116, 0, sizeof t116);
            GM_ST(uint8_t, ((uint8_t *)&t116 + 0), ((uint8_t)11ULL));
            uint8_t t117 = 0;
            F.ret = t116;
            goto gm_fret;
        }
        goto ix109;
ie109:;
ix109:;
    }
    {
        void * t119 = gm_l0;
        void * t120 = GM_LD(void *, (((uint8_t *)t119) + 2168));
        void * t121 = t120;
        if (!(GM_LD(void *, ((uint8_t *)&t121)) != 0)) goto ie118;
        void * t122 = GM_LD(void *, ((uint8_t *)&t121));
        gm_l16 = t122;
        void * t123 = gm_l0;
        uint64_t t124 = gm_l1;
        void * t125 = gm_l4;
        gm_tkn42u7na3t81 t126 = gm_l13;
        gm_tkn42u7na3t81 t127 = gm_f_const_check_d48mnu7((void *)NULL, cx, NULL, t123, t124, t125, t126);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tkn42u7na3t81 t128 = t127;
        uint8_t t129;
        if (((((uint8_t *)&t128))[4] != 0)) goto mn131;
        t129 = ((uint8_t)1);
        goto me130;
mn131:;
        t129 = ((uint8_t)0);
        goto me130;
mn132:;
        gm_trap("no match arm matched the scrutinee", gm_srclv0dcbbfhves, 1078, 32);
me130:;
        if (!t129) goto ie118;
        {
            void * t134 = gm_l16;
            void * t135 = gm_l0;
            uint64_t t136 = gm_l1;
            void * t137 = gm_l4;
            gm_tkn42u7na3t81 t138 = gm_l13;
            gm_b16_8 t139 = GM_LD(gm_b16_8, (((uint8_t *)a2) + 0));
            gm_tl874t2atnlh3 t140 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)a2) + 16));
            void * t141 = t134;
            gm_t9805qtonbsag t142;
            if (GM_PTR_TAGGED(t141)) {
                t142 = ((gm_t9805qtonbsag (*)(void *, uint64_t, void *, gm_tkn42u7na3t81, gm_b16_8, gm_cseahvg8v7p0vs))GM_PTR_UNTAG(t141))(t135, t136, t137, t138, t139, GM_CONV(gm_cseahvg8v7p0vs, t140));
            } else {
                t142 = ((gm_t9805qtonbsag (*)(void *, gm_node *, gm_node *, void *, uint64_t, void *, gm_tkn42u7na3t81, gm_b16_8, gm_tl874t2atnlh3))t141)(NULL, cx, NULL, t135, t136, t137, t138, t139, t140);
            }
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t9805qtonbsag t143 = t142;
            uint32_t t144;
            if (((((uint8_t *)&t143))[4] != 0)) {
                uint32_t t145 = GM_LD(uint32_t, (((uint8_t *)&t143) + 0));
                t144 = t145;
            } else {
                gm_t6qooom8qa2do t146;
                memset(&t146, 0, sizeof t146);
                GM_ST(uint8_t, ((uint8_t *)&t146 + 0), ((uint8_t)11ULL));
                uint8_t t147 = 0;
                F.ret = t146;
                goto gm_fret;
            }
            gm_l17 = t144;
        }
        {
            uint32_t t148 = gm_l17;
            uint64_t t149 = (uint64_t)t148;
            uint64_t t150 = (uintptr_t)t149;
            void * t151 = gm_l0;
            uint64_t t152 = GM_LD(uintptr_t, ((((uint8_t *)t151) + 1968) + 0) + 8);
            if (t150 >= (uintptr_t)(t152)) gm_trap_index(t150, t152, gm_srclv0dcbbfhves, 1080, 23);
            gm_t88rcucg1b7uk t153 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t151) + 1968) + 0)) + t150 * 88) + 28));
            gm_t6qooom8qa2do t154;
            memset(&t154, 0, sizeof t154);
            GM_ST(uint8_t, ((uint8_t *)&t154 + 0), ((uint8_t)7ULL));
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t154 + 8), t153);
            uint8_t t155 = 0;
            F.ret = t154;
            goto gm_fret;
        }
        goto ix118;
ie118:;
ix118:;
    }
    {
        void * t156 = gm_l0;
        uint64_t t157 = gm_l1;
        void * t158 = gm_l4;
        gm_tkn42u7na3t81 t159 = gm_l13;
        gm_tkn42u7na3t81 t160 = gm_f_const_type_7rijs95((void *)NULL, cx, NULL, t156, t157, t158, t159);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tkn42u7na3t81 t161 = t160;
        gm_t88rcucg1b7uk t162;
        if (((((uint8_t *)&t161))[4] != 0)) {
            gm_t88rcucg1b7uk t163 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t161) + 0));
            t162 = t163;
        } else {
            gm_t6qooom8qa2do t164;
            memset(&t164, 0, sizeof t164);
            GM_ST(uint8_t, ((uint8_t *)&t164 + 0), ((uint8_t)11ULL));
            uint8_t t165 = 0;
            F.ret = t164;
            goto gm_fret;
        }
        gm_l18 = t162;
    }
    {
        void * t167 = gm_l0;
        void * t168 = GM_LD(void *, (((uint8_t *)t167) + 2168));
        void * t169 = t168;
        uint8_t t170;
        if ((GM_LD(void *, ((uint8_t *)&t169)) != 0)) goto mn172;
        t170 = ((uint8_t)0);
        goto me171;
mn172:;
        t170 = ((uint8_t)1);
        goto me171;
mn173:;
        gm_trap("no match arm matched the scrutinee", gm_srclv0dcbbfhves, 1083, 8);
me171:;
        if (!t170) goto ie166;
        {
            {
                void * t176 = gm_l0;
                gm_tjcptv9sagfq8 t177;
                memset((uint8_t *)&t177 + 73, 0, 7);
                gm_b16_8 t178 = GM_LD(gm_b16_8, (((uint8_t *)a2) + 0));
                GM_ST(gm_b16_8, (uint8_t *)&t177 + 0, t178);
                gm_tl874t2atnlh3 t179 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)a2) + 16));
                GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t177 + 16, t179);
                gm_t88rcucg1b7uk t180 = gm_l18;
                GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t177 + 28, t180);
                uint64_t t181 = gm_l1;
                GM_ST(uint64_t, (uint8_t *)&t177 + 32, t181);
                void * t182 = gm_l4;
                GM_ST(void *, (uint8_t *)&t177 + 40, t182);
                gm_tkn42u7na3t81 t183 = gm_l13;
                GM_ST(gm_tkn42u7na3t81, (uint8_t *)&t177 + 48, t183);
                void * t184 = gm_l0;
                uint64_t t185 = GM_LD(uint64_t, (((uint8_t *)t184) + 2312));
                GM_ST(uint64_t, (uint8_t *)&t177 + 56, t185);
                GM_ST(void *, (uint8_t *)&t177 + 64, ((void *)0));
                gm_t9qkh0rsooc8d t186 = GM_LD(gm_t9qkh0rsooc8d, gm_c0u09tvsmdv);
                GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&t177 + 72, t186);
                GM_ST(void *, (uint8_t *)&t177 + 80, ((void *)0));
                uint32_t t187 = gm_f_add_const_8u8b1ja((void *)NULL, cx, NULL, t176, &t177);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
        }
        goto ix166;
ie166:;
ix166:;
    }
    {
        void * t189 = gm_l0;
        void * t190 = (void *)(((uint8_t *)t189) + 8);
        gm_t88rcucg1b7uk t191 = gm_l18;
        uint8_t t192 = gm_f_is_error_8k27gd1(t190, GM_CONV(gm_csguankko4lmhd, t191));
        if (!t192) goto ie188;
        {
            gm_t6qooom8qa2do t194;
            memset(&t194, 0, sizeof t194);
            GM_ST(uint8_t, ((uint8_t *)&t194 + 0), ((uint8_t)11ULL));
            uint8_t t195 = 0;
            F.ret = t194;
            goto gm_fret;
        }
        goto ix188;
ie188:;
ix188:;
    }
    {
        void * t197 = gm_l0;
        gm_t88rcucg1b7uk t198 = gm_l18;
        gm_tkn42u7na3t81 t199 = gm_f_non_constant_part_itk7b0v(t197, GM_CONV(gm_csguankko4lmhd, t198));
        gm_tkn42u7na3t81 t200 = t199;
        if (!((((uint8_t *)&t200))[4] != 0)) goto ie196;
        gm_t88rcucg1b7uk t201 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&t200));
        gm_l19 = t201;
        {
            void * t203 = gm_l0;
            void * t204 = gm_l4;
            gm_tl874t2atnlh3 t205 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t204) + 72));
            gm_t6j6smj7qoctu t206;
            void * t207 = gm_l0;
            gm_t88rcucg1b7uk t208 = gm_l18;
            gm_b16_8 t209 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t207, t208);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(gm_b16_8, (uint8_t *)&t206 + 0, t209);
            void * t210 = gm_l0;
            gm_t88rcucg1b7uk t211 = gm_l19;
            gm_b16_8 t212 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t210, t211);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(gm_b16_8, (uint8_t *)&t206 + 16, t212);
            gm_tabb710d9mq2e t213;
            gm_f_format_j4sks15((void *)&t213, (void *)NULL, cx, NULL, &t206);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t214 = t213;
            if (((((uint8_t *)&t214))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t214) + 8, gm_srclv0dcbbfhves, 1088, 28);
            gm_b16_8 t215 = GM_LD(gm_b16_8, (((uint8_t *)&t214) + 8));
            gm_b16_8 t216 = t215;
            gm_b16_8 t217 = GM_LD(gm_b16_8, ((uint8_t *)&t216));
            gm_b0_1 t218 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t203, t205, t217);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_t6qooom8qa2do t219;
            memset(&t219, 0, sizeof t219);
            GM_ST(uint8_t, ((uint8_t *)&t219 + 0), ((uint8_t)11ULL));
            uint8_t t220 = 0;
            F.ret = t219;
            goto gm_fret;
        }
        goto ix196;
ie196:;
ix196:;
    }
    {
        gm_t88rcucg1b7uk t221 = gm_l18;
        gm_t6qooom8qa2do t222;
        memset(&t222, 0, sizeof t222);
        GM_ST(uint8_t, ((uint8_t *)&t222 + 0), ((uint8_t)7ULL));
        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t222 + 8), t221);
        uint8_t t223 = 0;
        F.ret = t222;
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
__attribute__((aligned(4))) gm_b0_1 gm_f_declare_method_candidate_e20bo1i(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, const void * a2, gm_t88rcucg1b7uk a3) {
    struct gm_fr_declare_method_candidate_e20bo1i F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* module */
    gm_t08olq97rscn3 gm_l2; /* name */
    gm_t88rcucg1b7uk gm_l3; /* first */
    gm_t88rcucg1b7uk gm_l4; /* t */
    gm_t88rcucg1b7uk gm_l5; /* p */
    uint32_t gm_l6; /* adt */
    gm_b16_8 gm_l8; /* <tmp> */
    uint64_t gm_l9; /* <tmp> */
    GM_STACK_CHECK(&F, 992u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l3 = a3;
    {
        gm_t88rcucg1b7uk t2 = gm_l3;
        gm_l4 = t2;
    }
    {
        void * t4 = gm_l0;
        void * t5 = (void *)(((uint8_t *)t4) + 8);
        gm_t88rcucg1b7uk t6 = gm_l3;
        gm_t53at80pfdus3 t7;
        gm_f_kind_jcnt4hm((void *)&t7, t5, GM_CONV(gm_csguankko4lmhd, t6));
        gm_t53at80pfdus3 t8 = t7;
        uint8_t t9 = GM_LD(uint8_t, (((uint8_t *)&t8) + 0));
        if (t9 != ((uint8_t)8ULL)) goto ie3;
        gm_t88rcucg1b7uk t10 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t8) + 8) + 4));
        gm_l5 = t10;
        {
            gm_t88rcucg1b7uk t12 = gm_l5;
            gm_l4 = t12;
        }
        goto ix3;
ie3:;
ix3:;
    }
    {
        void * t14 = gm_l0;
        void * t15 = (void *)(((uint8_t *)t14) + 8);
        gm_t88rcucg1b7uk t16 = gm_l4;
        gm_t53at80pfdus3 t17;
        gm_f_kind_jcnt4hm((void *)&t17, t15, GM_CONV(gm_csguankko4lmhd, t16));
        gm_t53at80pfdus3 t18 = t17;
        uint8_t t19 = GM_LD(uint8_t, (((uint8_t *)&t18) + 0));
        if (t19 != ((uint8_t)17ULL)) goto ie13;
        uint32_t t20 = GM_LD(uint32_t, ((((uint8_t *)&t18) + 8) + 0));
        gm_l6 = t20;
        {
            uint32_t t23 = gm_l6;
            uint64_t t24 = (uint64_t)t23;
            uint64_t t25 = (uintptr_t)t24;
            void * t26 = gm_l0;
            uint64_t t27 = GM_LD(uintptr_t, ((((uint8_t *)t26) + 1088) + 0) + 8);
            if (t25 >= (uintptr_t)(t27)) gm_trap_index(t25, t27, gm_srclv0dcbbfhves, 1150, 8);
            uint64_t t28 = GM_LD(uint64_t, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t26) + 1088) + 0)) + t25 * 184) + 16));
            uint64_t t29 = gm_l1;
            uint8_t t30 = (!(t28 == t29));
            if (!t30) goto ie22;
            {
                gm_b0_1 t32;
                uint8_t t33 = 0;
                F.ret = t32;
                goto gm_fret;
            }
            goto ix22;
ie22:;
ix22:;
        }
        {
            uint32_t t35 = gm_l6;
            uint64_t t36 = (uint64_t)t35;
            uint64_t t37 = (uintptr_t)t36;
            void * t38 = gm_l0;
            uint64_t t39 = GM_LD(uintptr_t, ((((uint8_t *)t38) + 1088) + 0) + 8);
            if (t37 >= (uintptr_t)(t39)) gm_trap_index(t37, t39, gm_srclv0dcbbfhves, 1151, 24);
            uint8_t t40 = GM_LD(uint8_t, ((((uint8_t *)GM_LD(void *, ((((uint8_t *)t38) + 1088) + 0)) + t37 * 184) + 24) + 0));
            if (t40 != ((uint8_t)0ULL)) goto ie34;
            gm_tpautr4br7h3l t41 = GM_LD(gm_tpautr4br7h3l, ((((uint8_t *)GM_LD(void *, ((((uint8_t *)t38) + 1088) + 0)) + t37 * 184) + 24) + 8));
            F.l7 = t41;
            {
                {
                    gm_b16_8 t44 = GM_LD(gm_b16_8, (((uint8_t *)&F.l7) + 0));
                    gm_l8 = t44;
                }
                {
                    gm_l9 = ((uint64_t)0ULL);
                }
                {
                    for (;;) {
                        uint64_t t46 = gm_l9;
                        uint64_t t47 = GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8);
                        uint8_t t48 = (t46 < t47);
                        if (!t48) goto lb45;
                        {
                            uint64_t t51 = gm_l9;
                            uint64_t t52 = (uintptr_t)t51;
                            uint64_t t53 = GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8);
                            gm_t0mglksrkfa5e t56 = GM_LD(gm_t0mglksrkfa5e, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l8)) + t52 * 40));
                            F.l10 = t56;
                            {
                                gm_b16_8 t59 = GM_LD(gm_b16_8, (((uint8_t *)&F.l10) + 0));
                                gm_b16_8 t60 = GM_LD(gm_b16_8, (((uint8_t *)a2) + 0));
                                gm_b16_8 t61 = t59;
                                gm_b16_8 t62 = t60;
                                uint8_t t63 = (gm_eq0iqiubfm7e(((uint8_t *)&t61), ((uint8_t *)&t62)));
                                if (!t63) goto ie58;
                                {
                                    void * t65 = gm_l0;
                                    gm_tl874t2atnlh3 t66 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)a2) + 16));
                                    gm_t6j6smj7qoctu t67;
                                    void * t68 = gm_l0;
                                    gm_t88rcucg1b7uk t69 = gm_l4;
                                    gm_b16_8 t70 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t68, t69);
                                    if (gm_unw.kind) {
                                        goto gm_ll49;
                                    }
                                    GM_ST(gm_b16_8, (uint8_t *)&t67 + 0, t70);
                                    gm_b16_8 t71 = GM_LD(gm_b16_8, (((uint8_t *)a2) + 0));
                                    GM_ST(gm_b16_8, (uint8_t *)&t67 + 16, t71);
                                    gm_tabb710d9mq2e t72;
                                    gm_f_format_v0s3sc7((void *)&t72, (void *)NULL, cx, NULL, &t67);
                                    if (gm_unw.kind) {
                                        goto gm_ll49;
                                    }
                                    gm_tabb710d9mq2e t73 = t72;
                                    if (((((uint8_t *)&t73))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t73) + 8, gm_srclv0dcbbfhves, 1154, 36);
                                    gm_b16_8 t74 = GM_LD(gm_b16_8, (((uint8_t *)&t73) + 8));
                                    gm_b16_8 t75 = t74;
                                    gm_b16_8 t76 = GM_LD(gm_b16_8, ((uint8_t *)&t75));
                                    gm_b0_1 t77 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t65, t66, t76);
                                    if (gm_unw.kind) {
                                        goto gm_ll49;
                                    }
                                }
                                {
                                    goto lb45;
                                }
                                goto ix58;
ie58:;
ix58:;
                            }
                            goto me54;
mn55:;
                            gm_trap("no match arm matched the scrutinee", gm_srclv0dcbbfhves, 1152, 9);
me54:;
                        }
lc45:;
                        {
                            uint64_t t79 = gm_l9;
                            uint64_t r80; bool o81 = __builtin_add_overflow(t79, ((uint64_t)1ULL), &r80);
                            uint64_t t82 = r80;
                            gm_l9 = t82;
                        }
                    }
gm_ll49:;
                    if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb45; }
                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc45; }
                    }
                    goto gm_lad;
lb45:;
                }
            }
            goto ix34;
ie34:;
ix34:;
        }
        {
            void * t83 = gm_l0;
            void * t84 = (void *)(((uint8_t *)t83) + 1872);
            gm_t79rbdv4rafe5 t85;
            memset((uint8_t *)&t85 + 4, 0, 4);
            memset((uint8_t *)&t85 + 36, 0, 4);
            void * t86 = gm_l0;
            void * t87 = (void *)(((uint8_t *)t86) + 8);
            uint32_t t88 = gm_l6;
            uint32_t t89 = gm_f_template_9o92cvv(t87, t88);
            GM_ST(uint32_t, (uint8_t *)&t85 + 0, t89);
            gm_b16_8 t90 = GM_LD(gm_b16_8, (((uint8_t *)a2) + 0));
            GM_ST(gm_b16_8, (uint8_t *)&t85 + 8, t90);
            gm_tl874t2atnlh3 t91 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)a2) + 16));
            GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t85 + 24, t91);
            gm_tftb1odc739t5 t92 = gm_f_push_fomabnf(t84, &t85);
            gm_tftb1odc739t5 t93 = t92;
            if (((((uint8_t *)&t93))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t93) + 1, gm_srclv0dcbbfhves, 1159, 5);
            gm_b0_1 t94 = GM_LD(gm_b0_1, (((uint8_t *)&t93) + 1));
        }
        goto ix13;
ie13:;
        {
            gm_b0_1 t96;
            uint8_t t97 = 0;
            F.ret = t96;
            goto gm_fret;
        }
ix13:;
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
extern __attribute__((aligned(4))) void gm_f_def_of_m512sbu(void *gm_out, void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_tdgfduqjheqdm gm_f_def_of_m512sbu_c(void * a0, uint32_t a1) { gm_tdgfduqjheqdm gm_r; gm_f_def_of_m512sbu(&gm_r, a0, a1); return gm_r; }
inline __attribute__((aligned(4))) gm_b0_1 gm_f_effect_arg_tys_2a7r8hh(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_t9qkh0rsooc8d a1, void * a2) {
    struct gm_fr_effect_arg_tys_2a7r8hh F;
    gm_b16_8 gm_l0; /* refs */
    gm_t9qkh0rsooc8d gm_l1; /* need */
    void * gm_l2; /* tys */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    gm_tiilmfd16v3fc gm_l5; /* r */
    gm_b16_8 gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    gm_t88rcucg1b7uk gm_l9; /* t */
    GM_STACK_CHECK(&F, 341u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        {
            gm_b16_8 t3 = gm_l0;
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
                    gm_tiilmfd16v3fc t15 = GM_LD(gm_tiilmfd16v3fc, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t11 * 24));
                    gm_l5 = t15;
                    {
                        {
                            gm_b16_8 t18 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l5) + 8));
                            gm_l6 = t18;
                        }
                        {
                            gm_l7 = ((uint64_t)0ULL);
                        }
                        {
                            for (;;) {
                                uint64_t t20 = gm_l7;
                                uint64_t t21 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                                uint8_t t22 = (t20 < t21);
                                if (!t22) goto lb19;
                                {
                                    uint64_t t25 = gm_l7;
                                    uint64_t t26 = (uintptr_t)t25;
                                    uint64_t t27 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                                    gm_toa2vhe3l4hfr t30 = GM_LD(gm_toa2vhe3l4hfr, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t26 * 40));
                                    F.l8 = t30;
                                    {
                                        uint8_t t33 = GM_LD(uint8_t, (((uint8_t *)&F.l8) + 0));
                                        if (t33 != ((uint8_t)0ULL)) goto ie32;
                                        gm_t88rcucg1b7uk t34 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l8) + 8));
                                        gm_l9 = t34;
                                        {
                                            void * t36 = gm_l2;
                                            gm_t88rcucg1b7uk t37 = gm_l9;
                                            gm_t9qkh0rsooc8d t38 = gm_l1;
                                            gm_t3jsf43p1pths t39;
                                            GM_ST(uint8_t, (uint8_t *)&t39 + 5, 0);
                                            GM_ST(uint16_t, (uint8_t *)&t39 + 6, 0);
                                            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t39 + 0), t37);
                                            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t39 + 4), t38);
                                            gm_tftb1odc739t5 t40 = gm_f_push_h37silv(t36, t39);
                                            gm_tftb1odc739t5 t41 = t40;
                                            if (((((uint8_t *)&t41))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t41) + 1, gm_srclv0dcbbfhves, 1249, 32);
                                            gm_b0_1 t42 = GM_LD(gm_b0_1, (((uint8_t *)&t41) + 1));
                                        }
                                        goto ix32;
ie32:;
ix32:;
                                    }
                                    goto me28;
mn29:;
                                    gm_trap("no match arm matched the scrutinee", gm_srclv0dcbbfhves, 1248, 9);
me28:;
                                }
lc19:;
                                {
                                    uint64_t t44 = gm_l7;
                                    uint64_t r45; bool o46 = __builtin_add_overflow(t44, ((uint64_t)1ULL), &r45);
                                    uint64_t t47 = r45;
                                    gm_l7 = t47;
                                }
                            }
lb19:;
                        }
                    }
                    goto me13;
mn14:;
                    gm_trap("no match arm matched the scrutinee", gm_srclv0dcbbfhves, 1247, 5);
me13:;
                }
lc4:;
                {
                    uint64_t t49 = gm_l4;
                    uint64_t r50; bool o51 = __builtin_add_overflow(t49, ((uint64_t)1ULL), &r50);
                    uint64_t t52 = r50;
                    gm_l4 = t52;
                }
            }
lb4:;
        }
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) gm_b0_1 gm_f_emit_dph5v1e(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_error_89glhjr(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void * gm_f_field_default_expr_tqfuiff(void * a0, uint32_t a1, uint64_t a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_field_default_expr_tqfuiff F;
    void * gm_l0; /* s */
    uint32_t gm_l1; /* id */
    uint64_t gm_l2; /* i */
    gm_b16_8 gm_l3; /* exprs */
    GM_STACK_CHECK_SHALLOW(&F, 155u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        uint32_t t2 = gm_l1;
        uint64_t t3 = (uint64_t)t2;
        uint64_t t4 = (uintptr_t)t3;
        void * t5 = gm_l0;
        uint64_t t6 = GM_LD(uintptr_t, ((((uint8_t *)t5) + 1088) + 0) + 8);
        if (t4 >= (uintptr_t)(t6)) gm_trap_index(t4, t6, gm_srclv0dcbbfhves, 708, 14);
        gm_b16_8 t7 = GM_LD(gm_b16_8, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t5) + 1088) + 0)) + t4 * 184) + 96));
        gm_l3 = t7;
    }
    {
        uint64_t t9 = gm_l2;
        uint64_t t10 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
        uint8_t t11 = (t9 >= t10);
        if (!t11) goto ie8;
        {
            uint8_t t13 = 0;
            F.ret = ((void *)0);
            goto gm_fret;
        }
        goto ix8;
ie8:;
ix8:;
    }
    {
        uint64_t t14 = gm_l2;
        uint64_t t15 = (uintptr_t)t14;
        uint64_t t16 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
        if (t15 >= (uintptr_t)(t16)) gm_trap_index(t15, t16, gm_srclv0dcbbfhves, 710, 12);
        void * t17 = GM_LD(void *, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t15 * 8));
        uint8_t t18 = 0;
        F.ret = t17;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_bfunt71(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_hcjjkrm(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0) {
    struct gm_fr_fn_hcjjkrm F;
    gm_t88rcucg1b7uk gm_l0; /* x */
    uint8_t gm_l1; /* c */
    uint8_t gm_l2; /* c */
    uint8_t gm_l3; /* c */
    uint32_t gm_l4; /* id */
    uint8_t gm_l5; /* is_bad */
    GM_STACK_CHECK_SHALLOW(&F, 228u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_non_constant_part_itk7b0v *)((&F.h)->up))->l0));
        void * t3 = (void *)(((uint8_t *)t2) + 8);
        gm_t88rcucg1b7uk t4 = gm_l0;
        gm_t53at80pfdus3 t5;
        gm_f_kind_jcnt4hm((void *)&t5, t3, GM_CONV(gm_csguankko4lmhd, t4));
        gm_t53at80pfdus3 t6 = t5;
        uint8_t t7;
        uint8_t t14 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
        switch (t14) { case ((uint8_t)0ULL): goto ma12; case ((uint8_t)1ULL): goto ma12; case ((uint8_t)2ULL): goto ma12; case ((uint8_t)3ULL): goto ma12; case ((uint8_t)4ULL): goto ma12; case ((uint8_t)5ULL): goto ma12; case ((uint8_t)6ULL): goto ma12; case ((uint8_t)7ULL): goto ma10; case ((uint8_t)8ULL): goto ma9; case ((uint8_t)9ULL): goto ma9; case ((uint8_t)10ULL): goto ma9; case ((uint8_t)11ULL): goto ma12; case ((uint8_t)12ULL): goto ma12; case ((uint8_t)13ULL): goto ma12; case ((uint8_t)14ULL): goto ma12; case ((uint8_t)15ULL): goto ma12; case ((uint8_t)16ULL): goto ma12; case ((uint8_t)17ULL): goto ma11; case ((uint8_t)18ULL): goto ma10; case ((uint8_t)19ULL): goto ma12; case ((uint8_t)20ULL): goto ma12; case ((uint8_t)21ULL): goto ma12; case ((uint8_t)22ULL): goto ma12; case ((uint8_t)23ULL): goto ma12; case ((uint8_t)24ULL): goto ma12; case ((uint8_t)25ULL): goto ma12; case ((uint8_t)26ULL): goto ma12; default: goto ma12; }
ma9:;
        uint8_t t18 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
        if (t18 != ((uint8_t)8ULL)) goto pa17;
        uint8_t t19 = GM_LD(uint8_t, ((((uint8_t *)&t6) + 8) + 0));
        gm_l1 = t19;
        goto po16;
pa17:;
        uint8_t t21 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
        if (t21 != ((uint8_t)9ULL)) goto pa20;
        uint8_t t22 = GM_LD(uint8_t, ((((uint8_t *)&t6) + 8) + 0));
        gm_l1 = t22;
        goto po16;
pa20:;
        uint8_t t24 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
        if (t24 != ((uint8_t)10ULL)) goto pa23;
        uint8_t t25 = GM_LD(uint8_t, ((((uint8_t *)&t6) + 8) + 0));
        gm_l1 = t25;
        goto po16;
pa23:;
        goto mn15;
po16:;
        uint8_t t26 = gm_l1;
        uint8_t t27 = (uint8_t)!t26;
        t7 = t27;
        goto me8;
mn15:;
ma10:;
        uint8_t t31 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
        if (t31 != ((uint8_t)7ULL)) goto pa30;
        goto po29;
pa30:;
        uint8_t t33 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
        if (t33 != ((uint8_t)18ULL)) goto pa32;
        goto po29;
pa32:;
        goto mn28;
po29:;
        t7 = ((uint8_t)1);
        goto me8;
mn28:;
ma11:;
        uint8_t t35 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
        if (t35 != ((uint8_t)17ULL)) goto mn34;
        uint32_t t36 = GM_LD(uint32_t, ((((uint8_t *)&t6) + 8) + 0));
        gm_l4 = t36;
        void * t37 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_non_constant_part_itk7b0v *)((&F.h)->up))->l0));
        void * t38 = (void *)(((uint8_t *)t37) + 8);
        uint32_t t39 = gm_l4;
        gm_b16_8 t40 = gm_f_adt_name_h7mjik7(t38, t39);
        gm_b16_8 t41 = t40;
        uint8_t t42 = (((GM_LD(uintptr_t, ((uint8_t *)&t41) + 8) == 9) && memcmp(GM_LD(void *, ((uint8_t *)&t41)), gm_s4eilu97sooh1, 9) == 0));
        t7 = t42;
        goto me8;
mn34:;
ma12:;
        t7 = ((uint8_t)0);
        goto me8;
mn43:;
ma13:;
        gm_trap("no match arm matched the scrutinee", gm_srclv0dcbbfhves, 1121, 19);
me8:;
        gm_l5 = t7;
    }
    {
        uint8_t t45 = gm_l5;
        if (!t45) goto ie44;
        uint8_t t46;
        if (((((uint8_t *)&((struct gm_fr_non_constant_part_itk7b0v *)((&F.h)->up))->l2))[4] != 0)) goto mn48;
        t46 = ((uint8_t)1);
        goto me47;
mn48:;
        t46 = ((uint8_t)0);
        goto me47;
mn49:;
        gm_trap("no match arm matched the scrutinee", gm_srclv0dcbbfhves, 1127, 23);
me47:;
        if (!t46) goto ie44;
        {
            gm_t88rcucg1b7uk t51 = gm_l0;
            gm_tkn42u7na3t81 t52;
            GM_ST(uint8_t, (uint8_t *)&t52 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t52 + 6, 0);
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t52), t51);
            ((uint8_t *)&t52)[4] = 1;
            GM_ST(gm_tkn42u7na3t81, ((uint8_t *)&((struct gm_fr_non_constant_part_itk7b0v *)((&F.h)->up))->l2), t52);
        }
        goto ix44;
ie44:;
ix44:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_repdalk(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0) {
    struct gm_fr_fn_repdalk F;
    gm_t88rcucg1b7uk gm_l0; /* x */
    uint32_t gm_l1; /* id */
    uint64_t gm_l2; /* d */
    gm_t9qkh0rsooc8d gm_l3; /* vis */
    GM_STACK_CHECK_SHALLOW(&F, 324u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3;
        if (((((uint8_t *)&((struct gm_fr_hidden_part_is5hgf9 *)((&F.h)->up))->l3))[8] != 0)) goto mn5;
        t3 = ((uint8_t)1);
        goto me4;
mn5:;
        t3 = ((uint8_t)0);
        goto me4;
mn6:;
        gm_trap("no match arm matched the scrutinee", gm_srclv0dcbbfhves, 1279, 12);
me4:;
        if (!t3) goto ie2;
        {
            void * t9 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_hidden_part_is5hgf9 *)((&F.h)->up))->l0));
            void * t10 = (void *)(((uint8_t *)t9) + 8);
            gm_t88rcucg1b7uk t11 = gm_l0;
            gm_t53at80pfdus3 t12;
            gm_f_kind_jcnt4hm((void *)&t12, t10, GM_CONV(gm_csguankko4lmhd, t11));
            gm_t53at80pfdus3 t13 = t12;
            uint8_t t14 = GM_LD(uint8_t, (((uint8_t *)&t13) + 0));
            if (t14 != ((uint8_t)17ULL)) goto ie8;
            uint32_t t15 = GM_LD(uint32_t, ((((uint8_t *)&t13) + 8) + 0));
            gm_l1 = t15;
            {
                void * t18 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_hidden_part_is5hgf9 *)((&F.h)->up))->l0));
                gm_b16_8 t19 = GM_LD(gm_b16_8, ((((uint8_t *)t18) + 1088) + 0));
                gm_b16_8 t20 = t19;
                void * t21 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_hidden_part_is5hgf9 *)((&F.h)->up))->l0));
                void * t22 = (void *)(((uint8_t *)t21) + 8);
                uint32_t t23 = gm_l1;
                uint32_t t24 = gm_f_template_9o92cvv(t22, t23);
                uint64_t t25 = (uint64_t)t24;
                uint64_t t26 = (uintptr_t)t25;
                gm_b16_8 t27 = t20;
                uint64_t t28 = GM_LD(uintptr_t, ((uint8_t *)&t27) + 8);
                if (t26 >= (uintptr_t)(t28)) gm_trap_index(t26, t28, gm_srclv0dcbbfhves, 1281, 25);
                if (!(((((uint8_t *)GM_LD(void *, ((uint8_t *)&t27)) + t26 * 184) + 168))[8] != 0)) goto ie17;
                uint64_t t29 = GM_LD(uint64_t, (((uint8_t *)GM_LD(void *, ((uint8_t *)&t27)) + t26 * 184) + 168));
                gm_l2 = t29;
                {
                    uint64_t t31 = gm_l2;
                    uint64_t t32 = (uintptr_t)t31;
                    void * t33 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_hidden_part_is5hgf9 *)((&F.h)->up))->l0));
                    void * t34 = GM_LD(void *, (((uint8_t *)t33) + 0));
                    uint64_t t35 = GM_LD(uintptr_t, ((((uint8_t *)t34) + 240) + 0) + 8);
                    if (t32 >= (uintptr_t)(t35)) gm_trap_index(t32, t35, gm_srclv0dcbbfhves, 1282, 28);
                    gm_t9qkh0rsooc8d t36 = GM_LD(gm_t9qkh0rsooc8d, ((((uint8_t *)GM_LD(void *, ((((uint8_t *)t34) + 240) + 0)) + t32 * 216) + 40) + 56));
                    gm_l3 = t36;
                }
                {
                    gm_t9qkh0rsooc8d t38 = gm_l3;
                    gm_t9qkh0rsooc8d t39 = t38;
                    uint8_t t40 = GM_LD(uint8_t, ((uint8_t *)&t39));
                    gm_t9qkh0rsooc8d t41 = GM_LD(gm_t9qkh0rsooc8d, ((uint8_t *)&((struct gm_fr_hidden_part_is5hgf9 *)((&F.h)->up))->l2));
                    gm_t9qkh0rsooc8d t42 = t41;
                    uint8_t t43 = GM_LD(uint8_t, ((uint8_t *)&t42));
                    uint8_t t44 = (t40 < t43);
                    if (!t44) goto ie37;
                    {
                        gm_t88rcucg1b7uk t46 = gm_l0;
                        gm_t9qkh0rsooc8d t47 = gm_l3;
                        gm_t3jsf43p1pths t48;
                        GM_ST(uint8_t, (uint8_t *)&t48 + 5, 0);
                        GM_ST(uint16_t, (uint8_t *)&t48 + 6, 0);
                        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t48 + 0), t46);
                        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t48 + 4), t47);
                        gm_trcagkhgm1qdu t49;
                        GM_ST(uint8_t, (uint8_t *)&t49 + 9, 0);
                        GM_ST(uint16_t, (uint8_t *)&t49 + 10, 0);
                        GM_ST(gm_t3jsf43p1pths, ((uint8_t *)&t49), t48);
                        ((uint8_t *)&t49)[8] = 1;
                        GM_ST(gm_trcagkhgm1qdu, ((uint8_t *)&((struct gm_fr_hidden_part_is5hgf9 *)((&F.h)->up))->l3), t49);
                    }
                    goto ix37;
ie37:;
ix37:;
                }
                goto ix17;
ie17:;
ix17:;
            }
            goto ix8;
ie8:;
ix8:;
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
inline __attribute__((aligned(4))) gm_t9805qtonbsag gm_f_frame_up_h0ddp1d(void * a0, uint32_t a1, uint32_t a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_frame_up_h0ddp1d F;
    void * gm_l0; /* s */
    uint32_t gm_l1; /* f */
    uint32_t gm_l2; /* levels */
    uint32_t gm_l3; /* cur */
    uint32_t gm_l4; /* <tmp> */
    uint32_t gm_l5; /* <tmp> */
    GM_STACK_CHECK_SHALLOW(&F, 197u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        uint32_t t2 = gm_l1;
        gm_l3 = t2;
    }
    {
        {
            gm_l5 = ((uint32_t)0ULL);
        }
        {
            uint32_t t4 = gm_l2;
            gm_l4 = t4;
        }
        {
            for (;;) {
                uint32_t t6 = gm_l5;
                uint32_t t7 = gm_l4;
                uint8_t t8 = (t6 < t7);
                if (!t8) goto lb5;
                {
                    uint32_t t11 = gm_l5;
                    uint32_t t12 = t11;
                    {
                        uint32_t t17 = gm_l3;
                        uint64_t t18 = (uint64_t)t17;
                        void * t19 = gm_l0;
                        uint64_t t20 = GM_LD(uintptr_t, ((((uint8_t *)t19) + 384) + 0) + 8);
                        uint8_t t21 = (t18 >= t20);
                        if (!t21) goto ie16;
                        {
                            gm_t9805qtonbsag t23;
                            memset(&t23, 0, sizeof t23);
                            uint8_t t24 = 0;
                            F.ret = t23;
                            goto gm_fret;
                        }
                        goto ix16;
ie16:;
ix16:;
                    }
                    {
                        uint32_t t25 = gm_l3;
                        uint64_t t26 = (uint64_t)t25;
                        uint64_t t27 = (uintptr_t)t26;
                        void * t28 = gm_l0;
                        uint64_t t29 = GM_LD(uintptr_t, ((((uint8_t *)t28) + 384) + 0) + 8);
                        if (t27 >= (uintptr_t)(t29)) gm_trap_index(t27, t29, gm_srclv0dcbbfhves, 600, 15);
                        gm_t9805qtonbsag t30 = GM_LD(gm_t9805qtonbsag, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t28) + 384) + 0)) + t27 * 64) + 48));
                        gm_t9805qtonbsag t31 = t30;
                        uint32_t t32;
                        if (((((uint8_t *)&t31))[4] != 0)) {
                            uint32_t t33 = GM_LD(uint32_t, (((uint8_t *)&t31) + 0));
                            t32 = t33;
                        } else {
                            gm_t9805qtonbsag t34;
                            memset(&t34, 0, sizeof t34);
                            uint8_t t35 = 0;
                            F.ret = t34;
                            goto gm_fret;
                        }
                        gm_l3 = t32;
                    }
                    goto me13;
mn14:;
                    gm_trap("no match arm matched the scrutinee", gm_srclv0dcbbfhves, 598, 5);
me13:;
                }
lc5:;
                {
                    uint32_t t37 = gm_l5;
                    uint32_t r38; bool o39 = __builtin_add_overflow(t37, ((uint32_t)1ULL), &r38);
                    uint32_t t40 = r38;
                    gm_l5 = t40;
                }
            }
lb5:;
        }
    }
    {
        uint32_t t41 = gm_l3;
        gm_t9805qtonbsag t42;
        GM_ST(uint8_t, (uint8_t *)&t42 + 5, 0);
        GM_ST(uint16_t, (uint8_t *)&t42 + 6, 0);
        GM_ST(uint32_t, ((uint8_t *)&t42), t41);
        ((uint8_t *)&t42)[4] = 1;
        uint8_t t43 = 0;
        F.ret = t42;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) uint32_t gm_f_generic_of_1861hm9(void * a0, uint32_t a1);
extern __attribute__((aligned(4))) gm_trcagkhgm1qdu gm_f_hidden_part_is5hgf9(void * a0, gm_csguankko4lmhd a1, gm_t9qkh0rsooc8d a2);
inline __attribute__((aligned(4))) uint8_t gm_f_in_generic_scope_rvb92ab(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_in_generic_scope_rvb92ab F;
    void * gm_l0; /* s */
    gm_b16_8 gm_l1; /* <tmp> */
    uint64_t gm_l2; /* <tmp> */
    gm_tep2drsl9408b gm_l3; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 204u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t3 = gm_l0;
        uint64_t t4 = GM_LD(uintptr_t, ((((uint8_t *)t3) + 1528) + 0) + 8);
        uint8_t t5 = (t4 > ((uint64_t)0ULL));
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
        {
            void * t9 = gm_l0;
            gm_b16_8 t10 = GM_LD(gm_b16_8, ((((uint8_t *)t9) + 1488) + 0));
            gm_l1 = t10;
        }
        {
            gm_l2 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t12 = gm_l2;
                uint64_t t13 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                uint8_t t14 = (t12 < t13);
                if (!t14) goto lb11;
                {
                    uint64_t t17 = gm_l2;
                    uint64_t t18 = (uintptr_t)t17;
                    uint64_t t19 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                    gm_tep2drsl9408b t22 = GM_LD(gm_tep2drsl9408b, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t18 * 24));
                    gm_l3 = t22;
                    {
                        void * t25 = gm_l0;
                        void * t26 = (void *)(((uint8_t *)t25) + 8);
                        gm_t88rcucg1b7uk t27 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l3) + 16));
                        uint8_t t28 = gm_f_has_param_u9t0efs(t26, GM_CONV(gm_csguankko4lmhd, t27));
                        if (!t28) goto ie24;
                        {
                            uint8_t t30 = 0;
                            F.ret = ((uint8_t)1);
                            goto gm_fret;
                        }
                        goto ix24;
ie24:;
ix24:;
                    }
                    goto me20;
mn21:;
                    gm_trap("no match arm matched the scrutinee", gm_srclv0dcbbfhves, 627, 5);
me20:;
                }
lc11:;
                {
                    uint64_t t32 = gm_l2;
                    uint64_t r33; bool o34 = __builtin_add_overflow(t32, ((uint64_t)1ULL), &r33);
                    uint64_t t35 = r33;
                    gm_l2 = t35;
                }
            }
lb11:;
        }
    }
    {
        uint8_t t36 = 0;
        F.ret = ((uint8_t)0);
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_ambient_m41pl1e(void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_ambient_m41pl1e F;
    void * gm_l0; /* s */
    uint32_t gm_l1; /* eff */
    void * gm_l2; /* info */
    GM_STACK_CHECK_SHALLOW(&F, 554u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint32_t t2 = gm_l1;
        uint64_t t3 = (uint64_t)t2;
        uint64_t t4 = (uintptr_t)t3;
        void * t5 = gm_l0;
        uint64_t t6 = GM_LD(uintptr_t, ((((uint8_t *)t5) + 1048) + 0) + 8);
        if (t4 >= (uintptr_t)(t6)) gm_trap_index(t4, t6, gm_srclv0dcbbfhves, 716, 14);
        void * t7 = (void *)((uint8_t *)GM_LD(void *, ((((uint8_t *)t5) + 1048) + 0)) + t4 * 56);
        gm_l2 = t7;
    }
    {
        void * t9 = gm_l2;
        uint64_t t10 = GM_LD(uint64_t, (((uint8_t *)t9) + 16));
        uint64_t t11 = (uintptr_t)t10;
        void * t12 = gm_l0;
        void * t13 = GM_LD(void *, (((uint8_t *)t12) + 0));
        uint64_t t14 = GM_LD(uintptr_t, ((((uint8_t *)t13) + 200) + 0) + 8);
        if (t11 >= (uintptr_t)(t14)) gm_trap_index(t11, t14, gm_srclv0dcbbfhves, 717, 12);
        uint8_t t15 = GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t13) + 200) + 0)) + t11 * 184) + 112));
        uint8_t t16 = (uint8_t)!t15;
        if (!t16) goto ie8;
        {
            uint8_t t18 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        goto ix8;
ie8:;
ix8:;
    }
    {
        uint8_t t19 = 0;
        void * t20 = gm_l2;
        uint8_t t21 = (!((GM_LD(uintptr_t, (((uint8_t *)t20) + 0) + 8) == 4) && memcmp(GM_LD(void *, (((uint8_t *)t20) + 0)), gm_ss6g2koqn63bf, 4) == 0));
        if (t21) {
            void * t22 = gm_l2;
            gm_b16_8 t23 = GM_LD(gm_b16_8, (((uint8_t *)t22) + 0));
            gm_t61mja4eihh2b t24 = GM_LD(gm_t61mja4eihh2b, gm_cp0okt01b9q);
            gm_t61mja4eihh2b t25 = t24;
            uint8_t t26 = 0;
            gm_b16_8 t27 = t23;
            for (uintptr_t i = 0; i < (uintptr_t)(12); i++) if (gm_eq0iqiubfm7e(((uint8_t *)&t25) + i * 16, ((uint8_t *)&t27))) { t26 = 1; break; }
            uint8_t t28 = (uint8_t)(t26);
            t19 = t28;
        } else t19 = 0;
        uint8_t t29 = 0;
        F.ret = t19;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_structural_alias_qpmv4mu(void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_structural_alias_qpmv4mu F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* id */
    void * gm_l2; /* d */
    GM_STACK_CHECK_SHALLOW(&F, 201u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint64_t t2 = gm_l1;
        uint64_t t3 = (uintptr_t)t2;
        void * t4 = gm_l0;
        void * t5 = GM_LD(void *, (((uint8_t *)t4) + 0));
        uint64_t t6 = GM_LD(uintptr_t, ((((uint8_t *)t5) + 240) + 0) + 8);
        if (t3 >= (uintptr_t)(t6)) gm_trap_index(t3, t6, gm_srclv0dcbbfhves, 806, 11);
        void * t7 = (void *)((uint8_t *)GM_LD(void *, ((((uint8_t *)t5) + 240) + 0)) + t3 * 216);
        gm_l2 = t7;
    }
    {
        void * t9 = gm_l2;
        uint64_t t10 = GM_LD(uintptr_t, ((((uint8_t *)t9) + 40) + 40) + 8);
        uint8_t t11 = (t10 > ((uint64_t)0ULL));
        if (!t11) goto ie8;
        {
            uint8_t t13 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        goto ix8;
ie8:;
ix8:;
    }
    {
        void * t15 = gm_l2;
        uint8_t t16 = GM_LD(uint8_t, (((((uint8_t *)t15) + 40) + 64) + 0));
        if (t16 != ((uint8_t)0ULL)) goto ie14;
        gm_tv2uf4leil5hq t17 = GM_LD(gm_tv2uf4leil5hq, (((((uint8_t *)t15) + 40) + 64) + 8));
        F.l3 = t17;
        {
            void * t20 = GM_LD(void *, (((uint8_t *)&F.l3) + 24));
            void * t21 = t20;
            uint8_t t22;
            if ((GM_LD(void *, ((uint8_t *)&t21)) != 0)) goto mn24;
            t22 = ((uint8_t)0);
            goto me23;
mn24:;
            t22 = ((uint8_t)1);
            goto me23;
mn25:;
            gm_trap("no match arm matched the scrutinee", gm_srclv0dcbbfhves, 809, 8);
me23:;
            if (!t22) goto ie19;
            {
                uint8_t t27 = 0;
                F.ret = ((uint8_t)0);
                goto gm_fret;
            }
            goto ix19;
ie19:;
ix19:;
        }
        {
            void * t28 = GM_LD(void *, (((uint8_t *)&F.l3) + 40));
            uint8_t t29;
            uint8_t t34 = GM_LD(uint8_t, ((((uint8_t *)t28) + 8) + 0));
            if (t34 != ((uint8_t)24ULL)) goto pa33;
            goto po32;
pa33:;
            uint8_t t36 = GM_LD(uint8_t, ((((uint8_t *)t28) + 8) + 0));
            if (t36 != ((uint8_t)25ULL)) goto pa35;
            goto po32;
pa35:;
            uint8_t t38 = GM_LD(uint8_t, ((((uint8_t *)t28) + 8) + 0));
            if (t38 != ((uint8_t)26ULL)) goto pa37;
            goto po32;
pa37:;
            uint8_t t40 = GM_LD(uint8_t, ((((uint8_t *)t28) + 8) + 0));
            if (t40 != ((uint8_t)27ULL)) goto pa39;
            goto po32;
pa39:;
            uint8_t t42 = GM_LD(uint8_t, ((((uint8_t *)t28) + 8) + 0));
            if (t42 != ((uint8_t)28ULL)) goto pa41;
            goto po32;
pa41:;
            uint8_t t44 = GM_LD(uint8_t, ((((uint8_t *)t28) + 8) + 0));
            if (t44 != ((uint8_t)29ULL)) goto pa43;
            goto po32;
pa43:;
            uint8_t t46 = GM_LD(uint8_t, ((((uint8_t *)t28) + 8) + 0));
            if (t46 != ((uint8_t)30ULL)) goto pa45;
            goto po32;
pa45:;
            goto mn31;
po32:;
            t29 = ((uint8_t)1);
            goto me30;
mn31:;
            t29 = ((uint8_t)0);
            goto me30;
mn47:;
            gm_trap("no match arm matched the scrutinee", gm_srclv0dcbbfhves, 810, 12);
me30:;
            uint8_t t48 = 0;
            F.ret = t29;
            goto gm_fret;
        }
        goto ix14;
ie14:;
        {
            uint8_t t50 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
ix14:;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_non_constant_part_itk7b0v(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void gm_f_prelude_effect_gilrmm5(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1) {
    struct gm_fr_prelude_effect_gilrmm5 F;
    void * gm_l0; /* s */
    gm_b16_8 gm_l1; /* name */
    uint64_t gm_l2; /* prelude */
    uint64_t gm_l3; /* d */
    uint32_t gm_l4; /* eff */
    GM_STACK_CHECK(&F, 841u);
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
            gm_tqu3m2l3n8mnc t8;
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
            gm_tqu3m2l3n8mnc t18;
            memset(&t18, 0, sizeof t18);
            uint8_t t19 = 0;
            F.ret = t18;
            goto gm_fret;
        }
        gm_l3 = t16;
    }
    {
        void * t21 = gm_l0;
        uint64_t t22 = gm_l3;
        gm_t6qooom8qa2do t23;
        gm_f_resolve_decl_pus042v((void *)&t23, (void *)NULL, cx, NULL, t21, t22);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t6qooom8qa2do t24 = t23;
        uint8_t t25 = GM_LD(uint8_t, (((uint8_t *)&t24) + 0));
        if (t25 != ((uint8_t)2ULL)) goto ie20;
        uint32_t t26 = GM_LD(uint32_t, (((uint8_t *)&t24) + 8));
        gm_l4 = t26;
        {
            gm_tiilmfd16v3fc t28;
            memset((uint8_t *)&t28 + 4, 0, 4);
            uint32_t t29 = gm_l4;
            GM_ST(uint32_t, (uint8_t *)&t28 + 0, t29);
            gm_b16_8 t30 = gm_f_empty_slice_fh7svmj();
            gm_b16_8 t31 = t30;
            gm_b16_8 t32 = GM_LD(gm_b16_8, ((uint8_t *)&t31));
            GM_ST(gm_b16_8, (uint8_t *)&t28 + 8, t32);
            gm_tqu3m2l3n8mnc t33;
            GM_ST(uint8_t, (uint8_t *)&t33 + 25, 0);
            GM_ST(uint16_t, (uint8_t *)&t33 + 26, 0);
            GM_ST(uint32_t, (uint8_t *)&t33 + 28, 0);
            GM_ST(gm_tiilmfd16v3fc, ((uint8_t *)&t33), t28);
            ((uint8_t *)&t33)[24] = 1;
            uint8_t t34 = 0;
            F.ret = t33;
            goto gm_fret;
        }
        goto ix20;
ie20:;
        {
            gm_tqu3m2l3n8mnc t36;
            memset(&t36, 0, sizeof t36);
            uint8_t t37 = 0;
            F.ret = t36;
            goto gm_fret;
        }
ix20:;
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
inline __attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_prelude_instance_arg_kh32dmf(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2) {
    struct gm_fr_prelude_instance_arg_kh32dmf F;
    void * gm_l0; /* s */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_b16_8 gm_l2; /* name */
    uint32_t gm_l3; /* id */
    gm_b16_8 gm_l4; /* args */
    uint32_t gm_l5; /* template */
    gm_t88rcucg1b7uk gm_l6; /* e */
    GM_STACK_CHECK(&F, 382u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t3 = gm_l0;
        void * t4 = (void *)(((uint8_t *)t3) + 8);
        gm_t88rcucg1b7uk t5 = gm_l1;
        gm_t53at80pfdus3 t6;
        gm_f_kind_jcnt4hm((void *)&t6, t4, GM_CONV(gm_csguankko4lmhd, t5));
        gm_t53at80pfdus3 t7 = t6;
        uint8_t t8 = GM_LD(uint8_t, (((uint8_t *)&t7) + 0));
        if (t8 != ((uint8_t)17ULL)) goto ie2;
        uint32_t t9 = GM_LD(uint32_t, ((((uint8_t *)&t7) + 8) + 0));
        gm_l3 = t9;
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((((uint8_t *)&t7) + 8) + 8));
        gm_l4 = t10;
        {
            void * t12 = gm_l0;
            gm_b16_8 t13 = gm_l2;
            gm_t9805qtonbsag t14 = gm_f_prelude_template_7s9nsp0((void *)NULL, cx, NULL, t12, t13);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t9805qtonbsag t15 = t14;
            uint32_t t16;
            if (((((uint8_t *)&t15))[4] != 0)) {
                uint32_t t17 = GM_LD(uint32_t, (((uint8_t *)&t15) + 0));
                t16 = t17;
            } else {
                gm_tkn42u7na3t81 t18;
                memset(&t18, 0, sizeof t18);
                uint8_t t19 = 0;
                F.ret = t18;
                goto gm_fret;
            }
            gm_l5 = t16;
        }
        {
            uint8_t t21 = 0;
            void * t22 = gm_l0;
            void * t23 = (void *)(((uint8_t *)t22) + 8);
            uint32_t t24 = gm_l3;
            uint32_t t25 = gm_f_template_9o92cvv(t23, t24);
            uint32_t t26 = gm_l5;
            uint8_t t27 = (!(t25 == t26));
            if (!t27) {
                uint64_t t28 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                uint8_t t29 = ((t28 == ((uint64_t)0ULL)));
                t21 = t29;
            } else t21 = 1;
            if (!t21) goto ie20;
            {
                gm_tkn42u7na3t81 t31;
                memset(&t31, 0, sizeof t31);
                uint8_t t32 = 0;
                F.ret = t31;
                goto gm_fret;
            }
            goto ix20;
ie20:;
ix20:;
        }
        {
            uint64_t t34 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t35 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
            if (t34 >= (uintptr_t)(t35)) gm_trap_index(t34, t35, gm_srclv0dcbbfhves, 692, 17);
            uint8_t t36 = GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t34 * 40) + 0));
            if (t36 != ((uint8_t)0ULL)) goto ie33;
            gm_t88rcucg1b7uk t37 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t34 * 40) + 8));
            gm_l6 = t37;
            {
                gm_t88rcucg1b7uk t39 = gm_l6;
                gm_tkn42u7na3t81 t40;
                GM_ST(uint8_t, (uint8_t *)&t40 + 5, 0);
                GM_ST(uint16_t, (uint8_t *)&t40 + 6, 0);
                GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t40), t39);
                ((uint8_t *)&t40)[4] = 1;
                uint8_t t41 = 0;
                F.ret = t40;
                goto gm_fret;
            }
            goto ix33;
ie33:;
            {
                gm_tkn42u7na3t81 t43;
                memset(&t43, 0, sizeof t43);
                uint8_t t44 = 0;
                F.ret = t43;
                goto gm_fret;
            }
ix33:;
        }
        goto ix2;
ie2:;
        {
            gm_tkn42u7na3t81 t46;
            memset(&t46, 0, sizeof t46);
            uint8_t t47 = 0;
            F.ret = t46;
            goto gm_fret;
        }
ix2:;
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
inline __attribute__((aligned(4))) gm_t9805qtonbsag gm_f_prelude_template_7s9nsp0(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1) {
    struct gm_fr_prelude_template_7s9nsp0 F;
    void * gm_l0; /* s */
    gm_b16_8 gm_l1; /* name */
    uint64_t gm_l2; /* prelude */
    uint64_t gm_l3; /* d */
    uint32_t gm_l4; /* template */
    GM_STACK_CHECK(&F, 689u);
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
            gm_t9805qtonbsag t8;
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
            gm_t9805qtonbsag t18;
            memset(&t18, 0, sizeof t18);
            uint8_t t19 = 0;
            F.ret = t18;
            goto gm_fret;
        }
        gm_l3 = t16;
    }
    {
        void * t21 = gm_l0;
        uint64_t t22 = gm_l3;
        gm_t6qooom8qa2do t23;
        gm_f_resolve_decl_pus042v((void *)&t23, (void *)NULL, cx, NULL, t21, t22);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t6qooom8qa2do t24 = t23;
        uint8_t t25 = GM_LD(uint8_t, (((uint8_t *)&t24) + 0));
        if (t25 != ((uint8_t)1ULL)) goto ie20;
        uint32_t t26 = GM_LD(uint32_t, (((uint8_t *)&t24) + 8));
        gm_l4 = t26;
        {
            uint32_t t28 = gm_l4;
            gm_t9805qtonbsag t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 6, 0);
            GM_ST(uint32_t, ((uint8_t *)&t29), t28);
            ((uint8_t *)&t29)[4] = 1;
            uint8_t t30 = 0;
            F.ret = t29;
            goto gm_fret;
        }
        goto ix20;
ie20:;
        {
            gm_t9805qtonbsag t32;
            memset(&t32, 0, sizeof t32);
            uint8_t t33 = 0;
            F.ret = t32;
            goto gm_fret;
        }
ix20:;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_push_field_tys_v4ep4t3(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_t9qkh0rsooc8d a1, void * a2) {
    struct gm_fr_push_field_tys_v4ep4t3 F;
    gm_b16_8 gm_l0; /* fields */
    gm_t9qkh0rsooc8d gm_l1; /* vis */
    void * gm_l2; /* tys */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    gm_t9qkh0rsooc8d gm_l6; /* need */
    GM_STACK_CHECK(&F, 201u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        {
            gm_b16_8 t3 = gm_l0;
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
                    gm_t0mglksrkfa5e t15 = GM_LD(gm_t0mglksrkfa5e, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t11 * 40));
                    F.l5 = t15;
                    {
                        uint8_t t18 = ((GM_LD(uint8_t, (((uint8_t *)&F.l5) + 20) + 0) == ((uint8_t)0ULL)));
                        if (!t18) goto ie17;
                        {
                            goto lc4;
                        }
                        goto ix17;
ie17:;
ix17:;
                    }
                    {
                        gm_t9qkh0rsooc8d t20;
                        gm_t9qkh0rsooc8d t22 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&F.l5) + 20));
                        gm_t9qkh0rsooc8d t23 = t22;
                        uint8_t t24 = GM_LD(uint8_t, ((uint8_t *)&t23));
                        gm_t9qkh0rsooc8d t25 = gm_l1;
                        gm_t9qkh0rsooc8d t26 = t25;
                        uint8_t t27 = GM_LD(uint8_t, ((uint8_t *)&t26));
                        uint8_t t28 = (t24 < t27);
                        if (!t28) goto ie21;
                        gm_t9qkh0rsooc8d t30 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&F.l5) + 20));
                        gm_t9qkh0rsooc8d t31 = t30;
                        t20 = t31;
                        goto ix21;
ie21:;
                        gm_t9qkh0rsooc8d t33 = gm_l1;
                        gm_t9qkh0rsooc8d t34 = t33;
                        t20 = t34;
ix21:;
                        gm_l6 = t20;
                    }
                    {
                        void * t35 = gm_l2;
                        gm_t88rcucg1b7uk t36 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l5) + 16));
                        gm_t9qkh0rsooc8d t37 = gm_l6;
                        gm_t3jsf43p1pths t38;
                        GM_ST(uint8_t, (uint8_t *)&t38 + 5, 0);
                        GM_ST(uint16_t, (uint8_t *)&t38 + 6, 0);
                        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t38 + 0), t36);
                        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t38 + 4), t37);
                        gm_tftb1odc739t5 t39 = gm_f_push_h37silv(t35, t38);
                        gm_tftb1odc739t5 t40 = t39;
                        if (((((uint8_t *)&t40))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t40) + 1, gm_srclv0dcbbfhves, 1270, 9);
                        gm_b0_1 t41 = GM_LD(gm_b0_1, (((uint8_t *)&t40) + 1));
                    }
                    goto me13;
mn14:;
                    gm_trap("no match arm matched the scrutinee", gm_srclv0dcbbfhves, 1267, 5);
me13:;
                }
lc4:;
                {
                    uint64_t t43 = gm_l4;
                    uint64_t r44; bool o45 = __builtin_add_overflow(t43, ((uint64_t)1ULL), &r44);
                    uint64_t t46 = r44;
                    gm_l4 = t46;
                }
            }
lb4:;
        }
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_thetuhtouve7f gm_f_range_of_dmmo768(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_range_of_dmmo768 F;
    void * gm_l0; /* s */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_t88rcucg1b7uk gm_l2; /* e */
    gm_t88rcucg1b7uk gm_l3; /* e */
    GM_STACK_CHECK(&F, 203u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t3 = gm_l0;
        gm_t88rcucg1b7uk t4 = gm_l1;
        gm_b16_8 t5;
        GM_ST(void *, (uint8_t *)&t5, (void *)(gm_seue7mq07v0oc));
        GM_ST(uintptr_t, (uint8_t *)&t5 + 8, (uintptr_t)(5));
        gm_tkn42u7na3t81 t6 = gm_f_prelude_instance_arg_kh32dmf((void *)NULL, cx, NULL, t3, t4, t5);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tkn42u7na3t81 t7 = t6;
        if (!((((uint8_t *)&t7))[4] != 0)) goto ie2;
        gm_t88rcucg1b7uk t8 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&t7));
        gm_l2 = t8;
        {
            gm_t88rcucg1b7uk t10 = gm_l2;
            gm_tbngattl1fo51 t11;
            GM_ST(uint8_t, (uint8_t *)&t11 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t11 + 2, 0);
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0));
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t11 + 4), t10);
            gm_thetuhtouve7f t12;
            GM_ST(uint8_t, (uint8_t *)&t12 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t12 + 10, 0);
            GM_ST(gm_tbngattl1fo51, ((uint8_t *)&t12), t11);
            ((uint8_t *)&t12)[8] = 1;
            uint8_t t13 = 0;
            F.ret = t12;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t15 = gm_l0;
        gm_t88rcucg1b7uk t16 = gm_l1;
        gm_b16_8 t17;
        GM_ST(void *, (uint8_t *)&t17, (void *)(gm_s43cq4krigs2b));
        GM_ST(uintptr_t, (uint8_t *)&t17 + 8, (uintptr_t)(9));
        gm_tkn42u7na3t81 t18 = gm_f_prelude_instance_arg_kh32dmf((void *)NULL, cx, NULL, t15, t16, t17);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tkn42u7na3t81 t19 = t18;
        if (!((((uint8_t *)&t19))[4] != 0)) goto ie14;
        gm_t88rcucg1b7uk t20 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&t19));
        gm_l3 = t20;
        {
            gm_t88rcucg1b7uk t22 = gm_l3;
            gm_tbngattl1fo51 t23;
            GM_ST(uint8_t, (uint8_t *)&t23 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t23 + 2, 0);
            GM_ST(uint8_t, ((uint8_t *)&t23 + 0), ((uint8_t)1));
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t23 + 4), t22);
            gm_thetuhtouve7f t24;
            GM_ST(uint8_t, (uint8_t *)&t24 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t24 + 10, 0);
            GM_ST(gm_tbngattl1fo51, ((uint8_t *)&t24), t23);
            ((uint8_t *)&t24)[8] = 1;
            uint8_t t25 = 0;
            F.ret = t24;
            goto gm_fret;
        }
        goto ix14;
ie14:;
ix14:;
    }
    {
        gm_thetuhtouve7f t26;
        memset(&t26, 0, sizeof t26);
        uint8_t t27 = 0;
        F.ret = t26;
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
extern __attribute__((aligned(4))) gm_t9805qtonbsag gm_f_range_template_dbt7218(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_report_cycle_mauubd4(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1) {
    struct gm_fr_report_cycle_mauubd4 F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* id */
    uint64_t gm_l2; /* start */
    gm_b16_8 gm_l3; /* cycle */
    gm_b16_8 gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    uint64_t gm_l7; /* d */
    gm_tl874t2atnlh3 gm_l8; /* span */
    gm_b16_8 gm_l10; /* <tmp> */
    uint64_t gm_l11; /* <tmp> */
    uint64_t gm_l12; /* c */
    gm_b16_8 gm_l13; /* <tmp> */
    uint64_t gm_l14; /* <tmp> */
    uint64_t gm_l15; /* c */
    GM_STACK_CHECK(&F, 1639u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        gm_b16_8 t3 = GM_LD(gm_b16_8, ((((uint8_t *)t2) + 1648) + 0));
        gm_b16_8 t4 = t3;
        gm_b16_8 t5 = GM_LD(gm_b16_8, ((uint8_t *)&t4));
        uint64_t t6 = gm_l1;
        gm_tf5srh4mp4b3i t7 = gm_f_index_of_s9trr09(t5, t6);
        gm_tf5srh4mp4b3i t8 = t7;
        uint64_t t9;
        if (((((uint8_t *)&t8))[8] != 0)) {
            uint64_t t10 = GM_LD(uint64_t, (((uint8_t *)&t8) + 0));
            t9 = t10;
        } else {
            t9 = ((uint64_t)0ULL);
        }
        gm_l2 = t9;
    }
    {
        void * t11 = gm_l0;
        uint64_t t12 = GM_LD(uintptr_t, ((((uint8_t *)t11) + 1648) + 0) + 8);
        void * t13 = ((uint8_t *)GM_LD(void *, ((((uint8_t *)t11) + 1648) + 0)));
        uint64_t t14 = gm_l2;
        uint64_t t15 = (uintptr_t)t14;
        if (t15 > (uintptr_t)t12 || (uintptr_t)t12 > (uintptr_t)t12) gm_trap_slice(t15, (uintptr_t)t12, t12, gm_srclv0dcbbfhves, 818, 14);
        gm_b16_8 t16;
        GM_ST(void *, (uint8_t *)&t16, (void *)(((uint8_t *)t13 + t15 * 8)));
        GM_ST(uintptr_t, (uint8_t *)&t16 + 8, (uintptr_t)((uintptr_t)t12 - t15));
        gm_l3 = t16;
    }
    {
        gm_f_list_epvducg((void *)&F.l4, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t18 = gm_l3;
            gm_l5 = t18;
        }
        {
            gm_l6 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t20 = gm_l6;
                uint64_t t21 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                uint8_t t22 = (t20 < t21);
                if (!t22) goto lb19;
                {
                    uint64_t t25 = gm_l6;
                    uint64_t t26 = (uintptr_t)t25;
                    uint64_t t27 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                    uint64_t t28 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l5)) + t26 * 8));
                    uint64_t t29 = t28;
                    uint64_t t32 = GM_LD(uint64_t, ((uint8_t *)&t29));
                    gm_l7 = t32;
                    {
                        void * t34 = (void *)((uint8_t *)&F.l4);
                        uint64_t t35 = gm_l7;
                        uint64_t t36 = (uintptr_t)t35;
                        void * t37 = gm_l0;
                        void * t38 = GM_LD(void *, (((uint8_t *)t37) + 0));
                        uint64_t t39 = GM_LD(uintptr_t, ((((uint8_t *)t38) + 240) + 0) + 8);
                        if (t36 >= (uintptr_t)(t39)) gm_trap_index(t36, t39, gm_srclv0dcbbfhves, 820, 52);
                        gm_b16_8 t40 = GM_LD(gm_b16_8, ((((uint8_t *)GM_LD(void *, ((((uint8_t *)t38) + 240) + 0)) + t36 * 216) + 0) + 0));
                        gm_tnpifv20irnph t41;
                        GM_ST(gm_b16_8, ((uint8_t *)&t41 + 0), t40);
                        gm_tabb710d9mq2e t42;
                        gm_f_format_h4jcodi((void *)&t42, (void *)NULL, cx, NULL, t41);
                        if (gm_unw.kind) {
                            goto gm_ll23;
                        }
                        gm_tabb710d9mq2e t43 = t42;
                        if (((((uint8_t *)&t43))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t43) + 8, gm_srclv0dcbbfhves, 820, 33);
                        gm_b16_8 t44 = GM_LD(gm_b16_8, (((uint8_t *)&t43) + 8));
                        gm_b16_8 t45 = t44;
                        gm_b16_8 t46 = GM_LD(gm_b16_8, ((uint8_t *)&t45));
                        gm_tftb1odc739t5 t47 = gm_f_push_3ef746b(t34, t46);
                        gm_tftb1odc739t5 t48 = t47;
                        if (((((uint8_t *)&t48))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t48) + 1, gm_srclv0dcbbfhves, 820, 22);
                        gm_b0_1 t49 = GM_LD(gm_b0_1, (((uint8_t *)&t48) + 1));
                    }
                    goto me30;
mn31:;
                    gm_trap("no match arm matched the scrutinee", gm_srclv0dcbbfhves, 820, 5);
me30:;
                }
lc19:;
                {
                    uint64_t t51 = gm_l6;
                    uint64_t r52; bool o53 = __builtin_add_overflow(t51, ((uint64_t)1ULL), &r52);
                    uint64_t t54 = r52;
                    gm_l6 = t54;
                }
            }
gm_ll23:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb19; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc19; }
            }
            goto gm_lad;
lb19:;
        }
    }
    {
        uint64_t t55 = gm_l1;
        uint64_t t56 = (uintptr_t)t55;
        void * t57 = gm_l0;
        void * t58 = GM_LD(void *, (((uint8_t *)t57) + 0));
        uint64_t t59 = GM_LD(uintptr_t, ((((uint8_t *)t58) + 240) + 0) + 8);
        if (t56 >= (uintptr_t)(t59)) gm_trap_index(t56, t59, gm_srclv0dcbbfhves, 821, 13);
        gm_tl874t2atnlh3 t60 = GM_LD(gm_tl874t2atnlh3, ((((uint8_t *)GM_LD(void *, ((((uint8_t *)t58) + 240) + 0)) + t56 * 216) + 0) + 16));
        gm_l8 = t60;
    }
    {
        gm_tl874t2atnlh3 t61 = gm_l8;
        gm_b16_8 t62 = GM_LD(gm_b16_8, (((uint8_t *)&F.l4) + 0));
        gm_b16_8 t63 = t62;
        gm_b16_8 t64 = GM_LD(gm_b16_8, ((uint8_t *)&t63));
        gm_b16_8 t65;
        GM_ST(void *, (uint8_t *)&t65, (void *)(gm_sdbvaa960ee0f));
        GM_ST(uintptr_t, (uint8_t *)&t65 + 8, (uintptr_t)(4));
        gm_tabb710d9mq2e t66;
        gm_f_join_sfknljb((void *)&t66, (void *)NULL, cx, NULL, t64, t65);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t67 = t66;
        if (((((uint8_t *)&t67))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t67) + 8, gm_srclv0dcbbfhves, 822, 83);
        gm_b16_8 t68 = GM_LD(gm_b16_8, (((uint8_t *)&t67) + 8));
        gm_tnpifv20irnph t69;
        GM_ST(gm_b16_8, ((uint8_t *)&t69 + 0), t68);
        gm_tabb710d9mq2e t70;
        gm_f_format_slm5ovr((void *)&t70, (void *)NULL, cx, NULL, t69);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t71 = t70;
        if (((((uint8_t *)&t71))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t71) + 8, gm_srclv0dcbbfhves, 822, 27);
        gm_b16_8 t72 = GM_LD(gm_b16_8, (((uint8_t *)&t71) + 8));
        gm_b16_8 t73 = t72;
        gm_b16_8 t74 = GM_LD(gm_b16_8, ((uint8_t *)&t73));
        gm_f_error_s6hn871((void *)&F.l9, (void *)NULL, cx, NULL, t61, t74);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            uint64_t t76 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
            void * t77 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)));
            uint64_t t78 = (uintptr_t)((uint64_t)1ULL);
            if (t78 > (uintptr_t)t76 || (uintptr_t)t76 > (uintptr_t)t76) gm_trap_slice(t78, (uintptr_t)t76, t76, gm_srclv0dcbbfhves, 823, 14);
            gm_b16_8 t79;
            GM_ST(void *, (uint8_t *)&t79, (void *)(((uint8_t *)t77 + t78 * 8)));
            GM_ST(uintptr_t, (uint8_t *)&t79 + 8, (uintptr_t)((uintptr_t)t76 - t78));
            gm_l10 = t79;
        }
        {
            gm_l11 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t81 = gm_l11;
                uint64_t t82 = GM_LD(uintptr_t, ((uint8_t *)&gm_l10) + 8);
                uint8_t t83 = (t81 < t82);
                if (!t83) goto lb80;
                {
                    uint64_t t86 = gm_l11;
                    uint64_t t87 = (uintptr_t)t86;
                    uint64_t t88 = GM_LD(uintptr_t, ((uint8_t *)&gm_l10) + 8);
                    uint64_t t89 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l10)) + t87 * 8));
                    uint64_t t90 = t89;
                    uint64_t t93 = GM_LD(uint64_t, ((uint8_t *)&t90));
                    gm_l12 = t93;
                    {
                        void * t95 = (void *)((uint8_t *)&F.l9);
                        uint64_t t96 = gm_l12;
                        uint64_t t97 = (uintptr_t)t96;
                        void * t98 = gm_l0;
                        void * t99 = GM_LD(void *, (((uint8_t *)t98) + 0));
                        uint64_t t100 = GM_LD(uintptr_t, ((((uint8_t *)t99) + 240) + 0) + 8);
                        if (t97 >= (uintptr_t)(t100)) gm_trap_index(t97, t100, gm_srclv0dcbbfhves, 823, 39);
                        gm_tl874t2atnlh3 t101 = GM_LD(gm_tl874t2atnlh3, ((((uint8_t *)GM_LD(void *, ((((uint8_t *)t99) + 240) + 0)) + t97 * 216) + 0) + 16));
                        gm_b16_8 t102;
                        GM_ST(void *, (uint8_t *)&t102, (void *)(gm_s68uiiatavvqh));
                        GM_ST(uintptr_t, (uint8_t *)&t102 + 8, (uintptr_t)(17));
                        gm_f_add_label_50fj7vv(t95, GM_CONV(gm_cseahvg8v7p0vs, t101), t102);
                    }
                    goto me91;
mn92:;
                    gm_trap("no match arm matched the scrutinee", gm_srclv0dcbbfhves, 823, 5);
me91:;
                }
lc80:;
                {
                    uint64_t t104 = gm_l11;
                    uint64_t r105; bool o106 = __builtin_add_overflow(t104, ((uint64_t)1ULL), &r105);
                    uint64_t t107 = r105;
                    gm_l11 = t107;
                }
            }
lb80:;
        }
    }
    {
        void * t108 = gm_l0;
        gm_b0_1 t109 = gm_f_emit_dph5v1e((void *)NULL, cx, NULL, t108, &(*(gm_t4gut2c9fj1kt *)((uint8_t *)&F.l9)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t111 = gm_l3;
            gm_l13 = t111;
        }
        {
            gm_l14 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t113 = gm_l14;
                uint64_t t114 = GM_LD(uintptr_t, ((uint8_t *)&gm_l13) + 8);
                uint8_t t115 = (t113 < t114);
                if (!t115) goto lb112;
                {
                    uint64_t t118 = gm_l14;
                    uint64_t t119 = (uintptr_t)t118;
                    uint64_t t120 = GM_LD(uintptr_t, ((uint8_t *)&gm_l13) + 8);
                    uint64_t t121 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l13)) + t119 * 8));
                    uint64_t t122 = t121;
                    uint64_t t125 = GM_LD(uint64_t, ((uint8_t *)&t122));
                    gm_l15 = t125;
                    {
                        void * t127 = gm_l0;
                        uint64_t t128 = gm_l15;
                        gm_t6qooom8qa2do t129;
                        memset(&t129, 0, sizeof t129);
                        GM_ST(uint8_t, ((uint8_t *)&t129 + 0), ((uint8_t)11ULL));
                        gm_tagrkbqh054h7 t130;
                        GM_ST(uint8_t, (uint8_t *)&t130 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t130 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t130 + 4, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t130 + 0), ((uint8_t)2ULL));
                        GM_ST(gm_t6qooom8qa2do, ((uint8_t *)&t130 + 8), t129);
                        gm_b0_1 t131 = gm_f_set_state_3j300fi((void *)NULL, cx, NULL, t127, t128, &t130);
                        if (gm_unw.kind) {
                            goto gm_ll116;
                        }
                    }
                    goto me123;
mn124:;
                    gm_trap("no match arm matched the scrutinee", gm_srclv0dcbbfhves, 825, 5);
me123:;
                }
lc112:;
                {
                    uint64_t t133 = gm_l14;
                    uint64_t r134; bool o135 = __builtin_add_overflow(t133, ((uint64_t)1ULL), &r134);
                    uint64_t t136 = r134;
                    gm_l14 = t136;
                }
            }
gm_ll116:;
            if (gm_unw.frame == &F.h && gm_unw.label == 2) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb112; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc112; }
            }
            goto gm_lad;
lb112:;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_resolve_ambient_6orl0vo(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_resolve_ambient_6orl0vo F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* prelude */
    uint64_t gm_l2; /* d */
    uint32_t gm_l3; /* e */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    gm_b16_8 gm_l6; /* name */
    uint64_t gm_l7; /* d */
    GM_STACK_CHECK(&F, 1079u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
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
            gm_b0_1 t8;
            uint8_t t9 = 0;
            F.ret = t8;
            goto gm_fret;
        }
        gm_l1 = t6;
    }
    {
        void * t11 = gm_l0;
        void * t12 = GM_LD(void *, (((uint8_t *)t11) + 0));
        uint64_t t13 = gm_l1;
        gm_b16_8 t14;
        GM_ST(void *, (uint8_t *)&t14, (void *)(gm_ses7tdrdt8a6u));
        GM_ST(uintptr_t, (uint8_t *)&t14 + 8, (uintptr_t)(5));
        gm_tf5srh4mp4b3i t15 = gm_f_lookup_59juqoo(t12, t13, t14);
        gm_tf5srh4mp4b3i t16 = t15;
        if (!((((uint8_t *)&t16))[8] != 0)) goto ie10;
        uint64_t t17 = GM_LD(uint64_t, ((uint8_t *)&t16));
        gm_l2 = t17;
        {
            void * t20 = gm_l0;
            uint64_t t21 = gm_l2;
            gm_t6qooom8qa2do t22;
            gm_f_resolve_decl_pus042v((void *)&t22, (void *)NULL, cx, NULL, t20, t21);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t6qooom8qa2do t23 = t22;
            uint8_t t24 = GM_LD(uint8_t, (((uint8_t *)&t23) + 0));
            if (t24 != ((uint8_t)2ULL)) goto ie19;
            uint32_t t25 = GM_LD(uint32_t, (((uint8_t *)&t23) + 8));
            gm_l3 = t25;
            {
                void * t27 = gm_l0;
                uint32_t t28 = gm_l3;
                gm_t9805qtonbsag t29;
                GM_ST(uint8_t, (uint8_t *)&t29 + 5, 0);
                GM_ST(uint16_t, (uint8_t *)&t29 + 6, 0);
                GM_ST(uint32_t, ((uint8_t *)&t29), t28);
                ((uint8_t *)&t29)[4] = 1;
                GM_ST(gm_t9805qtonbsag, (((uint8_t *)t27) + 1956), t29);
            }
            goto ix19;
ie19:;
ix19:;
        }
        goto ix10;
ie10:;
ix10:;
    }
    {
        {
            void * t31 = ((uint8_t *)gm_cp0okt01b9q);
            if ((uintptr_t)0 > (uintptr_t)12 || (uintptr_t)12 > (uintptr_t)12) gm_trap_slice((uintptr_t)0, (uintptr_t)12, 12, gm_srclv0dcbbfhves, 663, 5);
            gm_b16_8 t32;
            GM_ST(void *, (uint8_t *)&t32, (void *)(((uint8_t *)t31 + (uintptr_t)0 * 16)));
            GM_ST(uintptr_t, (uint8_t *)&t32 + 8, (uintptr_t)((uintptr_t)12 - (uintptr_t)0));
            gm_l4 = t32;
        }
        {
            gm_l5 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t34 = gm_l5;
                uint64_t t35 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                uint8_t t36 = (t34 < t35);
                if (!t36) goto lb33;
                {
                    uint64_t t39 = gm_l5;
                    uint64_t t40 = (uintptr_t)t39;
                    uint64_t t41 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                    gm_b16_8 t44 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t40 * 16));
                    gm_l6 = t44;
                    {
                        void * t47 = gm_l0;
                        void * t48 = GM_LD(void *, (((uint8_t *)t47) + 0));
                        uint64_t t49 = gm_l1;
                        gm_b16_8 t50 = gm_l6;
                        gm_tf5srh4mp4b3i t51 = gm_f_lookup_59juqoo(t48, t49, t50);
                        gm_tf5srh4mp4b3i t52 = t51;
                        if (!((((uint8_t *)&t52))[8] != 0)) goto ie46;
                        uint64_t t53 = GM_LD(uint64_t, ((uint8_t *)&t52));
                        gm_l7 = t53;
                        {
                            {
                                void * t56 = gm_l0;
                                uint64_t t57 = gm_l7;
                                gm_t6qooom8qa2do t58;
                                gm_f_resolve_decl_pus042v((void *)&t58, (void *)NULL, cx, NULL, t56, t57);
                                if (gm_unw.kind) {
                                    goto gm_ll37;
                                }
                            }
                        }
                        goto ix46;
ie46:;
ix46:;
                    }
                    goto me42;
mn43:;
                    gm_trap("no match arm matched the scrutinee", gm_srclv0dcbbfhves, 663, 5);
me42:;
                }
lc33:;
                {
                    uint64_t t60 = gm_l5;
                    uint64_t r61; bool o62 = __builtin_add_overflow(t60, ((uint64_t)1ULL), &r61);
                    uint64_t t63 = r61;
                    gm_l5 = t63;
                }
            }
gm_ll37:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb33; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc33; }
            }
            goto gm_lad;
lb33:;
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
__attribute__((aligned(4))) void gm_f_resolve_decl_pus042v(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1) {
    struct gm_fr_resolve_decl_pus042v F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* id */
    uint64_t gm_l4; /* i */
    uint8_t gm_l5; /* quiet */
    GM_STACK_CHECK(&F, 2905u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        uint64_t t3 = gm_l1;
        gm_tagrkbqh054h7 t4;
        gm_f_state_a5bnsjb((void *)&t4, (void *)NULL, cx, NULL, t2, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tagrkbqh054h7 t5 = t4;
        uint8_t t11 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        switch (t11) { case ((uint8_t)0ULL): goto ma9; case ((uint8_t)1ULL): goto ma8; case ((uint8_t)2ULL): goto ma7; default: goto ma10; }
ma7:;
        uint8_t t13 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t13 != ((uint8_t)2ULL)) goto mn12;
        gm_t6qooom8qa2do t14 = GM_LD(gm_t6qooom8qa2do, (((uint8_t *)&t5) + 8));
        F.l2 = t14;
        memmove(gm_out, ((uint8_t *)&F.l2), 208);
        goto gm_fdone;
        goto me6;
mn12:;
ma8:;
        uint8_t t16 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t16 != ((uint8_t)1ULL)) goto mn15;
        {
            void * t19 = gm_l0;
            uint64_t t20 = gm_l1;
            uint8_t t21 = gm_f_is_structural_alias_qpmv4mu(t19, t20);
            if (!t21) goto ie18;
            uint64_t t22 = gm_l1;
            void * t23 = gm_l0;
            gm_b16_8 t24 = GM_LD(gm_b16_8, ((((uint8_t *)t23) + 1688) + 0));
            gm_b16_8 t25 = t24;
            uint8_t t26 = 0;
            uint64_t t27 = t22;
            for (uintptr_t i = 0; i < (uintptr_t)(GM_LD(uintptr_t, ((uint8_t *)&t25) + 8)); i++) if (gm_eq6groq37ram(((uint8_t *)GM_LD(void *, ((uint8_t *)&t25))) + i * 8, ((uint8_t *)&t27))) { t26 = 1; break; }
            uint8_t t28 = (uint8_t)(!t26);
            if (!t28) goto ie18;
            {
                void * t30 = gm_l0;
                void * t31 = (void *)(((uint8_t *)t30) + 1688);
                uint64_t t32 = gm_l1;
                gm_tftb1odc739t5 t33 = gm_f_push_ucgmlo2(t31, t32);
                gm_tftb1odc739t5 t34 = t33;
                if (((((uint8_t *)&t34))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t34) + 1, gm_srclv0dcbbfhves, 764, 17);
                gm_b0_1 t35 = GM_LD(gm_b0_1, (((uint8_t *)&t34) + 1));
            }
            {
                void * t36 = gm_l0;
                uint64_t t37 = gm_l1;
                gm_f_compute_decl_gcf8mq9((void *)&F.l3, (void *)NULL, cx, NULL, t36, t37);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t39 = gm_l0;
                gm_b16_8 t40 = GM_LD(gm_b16_8, ((((uint8_t *)t39) + 1688) + 0));
                gm_b16_8 t41 = t40;
                gm_b16_8 t42 = GM_LD(gm_b16_8, ((uint8_t *)&t41));
                uint64_t t43 = gm_l1;
                gm_tf5srh4mp4b3i t44 = gm_f_index_of_s9trr09(t42, t43);
                gm_tf5srh4mp4b3i t45 = t44;
                if (!((((uint8_t *)&t45))[8] != 0)) goto ie38;
                uint64_t t46 = GM_LD(uint64_t, ((uint8_t *)&t45));
                gm_l4 = t46;
                {
                    {
                        void * t49 = gm_l0;
                        void * t50 = (void *)(((uint8_t *)t49) + 1688);
                        uint64_t t51 = gm_l4;
                        uint64_t t52 = gm_f_remove_om8q0q2(t50, t51);
                    }
                }
                goto ix38;
ie38:;
ix38:;
            }
            {
                memmove(gm_out, ((uint8_t *)&F.l3), 208);
                goto gm_fdone;
            }
            goto ix18;
ie18:;
ix18:;
        }
        {
            void * t53 = gm_l0;
            uint64_t t54 = gm_l1;
            gm_b0_1 t55 = gm_f_report_cycle_mauubd4((void *)NULL, cx, NULL, t53, t54);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_t6qooom8qa2do t56;
            memset(&t56, 0, sizeof t56);
            GM_ST(uint8_t, ((uint8_t *)&t56 + 0), ((uint8_t)11ULL));
            uint8_t t57 = 0;
            F.ret = t56;
            goto gm_fret;
        }
        goto me6;
mn15:;
ma9:;
        uint8_t t59 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t59 != ((uint8_t)0ULL)) goto mn58;
        goto me6;
mn58:;
ma10:;
        gm_trap("no match arm matched the scrutinee", gm_srclv0dcbbfhves, 757, 5);
me6:;
    }
    {
        void * t61 = gm_l0;
        uint64_t t62 = gm_l1;
        gm_tagrkbqh054h7 t63;
        memset(&t63, 0, sizeof t63);
        GM_ST(uint8_t, ((uint8_t *)&t63 + 0), ((uint8_t)1ULL));
        gm_b0_1 t64 = gm_f_set_state_3j300fi((void *)NULL, cx, NULL, t61, t62, &t63);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t65 = gm_l0;
        void * t66 = (void *)(((uint8_t *)t65) + 1648);
        uint64_t t67 = gm_l1;
        gm_tftb1odc739t5 t68 = gm_f_push_ucgmlo2(t66, t67);
        gm_tftb1odc739t5 t69 = t68;
        if (((((uint8_t *)&t69))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t69) + 1, gm_srclv0dcbbfhves, 775, 5);
        gm_b0_1 t70 = GM_LD(gm_b0_1, (((uint8_t *)&t69) + 1));
    }
    {
        void * t71 = gm_l0;
        uint8_t t72 = GM_LD(uint8_t, (((uint8_t *)t71) + 2536));
        gm_l5 = t72;
    }
    {
        void * t73 = gm_l0;
        GM_ST(uint8_t, (((uint8_t *)t73) + 2536), ((uint8_t)0));
    }
    {
    }
    {
        void * t77 = gm_l0;
        gm_trqfvmjtt4sij t78 = GM_LD(gm_trqfvmjtt4sij, (((uint8_t *)t77) + 1488));
        F.l6 = t78;
    }
    {
        void * t79 = gm_l0;
        gm_trqfvmjtt4sij t80 = GM_LD(gm_trqfvmjtt4sij, (((uint8_t *)t79) + 1528));
        F.l7 = t80;
    }
    {
        void * t81 = gm_l0;
        gm_trqfvmjtt4sij t82 = GM_LD(gm_trqfvmjtt4sij, (((uint8_t *)t81) + 1568));
        F.l8 = t82;
    }
    {
        void * t83 = gm_l0;
        gm_trqfvmjtt4sij t84 = GM_LD(gm_trqfvmjtt4sij, (((uint8_t *)t83) + 1608));
        F.l9 = t84;
    }
    {
        void * t85 = gm_l0;
        uint8_t *t86 = (((uint8_t *)t85) + 1488);
        gm_f_list_mvhhv5u((void *)&(*(gm_trqfvmjtt4sij *)t86), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_ld1_1;
        }
    }
    {
        void * t87 = gm_l0;
        uint8_t *t88 = (((uint8_t *)t87) + 1528);
        gm_f_list_ue2t2bm((void *)&(*(gm_trqfvmjtt4sij *)t88), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_ld1_1;
        }
    }
    {
        void * t89 = gm_l0;
        uint8_t *t90 = (((uint8_t *)t89) + 1568);
        gm_f_list_1jabh22((void *)&(*(gm_trqfvmjtt4sij *)t90), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_ld1_1;
        }
    }
    {
        void * t91 = gm_l0;
        uint8_t *t92 = (((uint8_t *)t91) + 1608);
        gm_f_list_0bj26n7((void *)&(*(gm_trqfvmjtt4sij *)t92), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_ld1_1;
        }
    }
    {
        void * t93 = gm_l0;
        uint64_t t94 = gm_l1;
        gm_f_compute_decl_gcf8mq9((void *)&F.l10, (void *)NULL, cx, NULL, t93, t94);
        if (gm_unw.kind) {
            goto gm_ld1_1;
        }
    }
    {
        void * t95 = gm_l0;
        gm_trqfvmjtt4sij t96 = F.l6;
        GM_ST(gm_trqfvmjtt4sij, (((uint8_t *)t95) + 1488), t96);
    }
    {
        void * t97 = gm_l0;
        gm_trqfvmjtt4sij t98 = F.l7;
        GM_ST(gm_trqfvmjtt4sij, (((uint8_t *)t97) + 1528), t98);
    }
    {
        void * t99 = gm_l0;
        gm_trqfvmjtt4sij t100 = F.l8;
        GM_ST(gm_trqfvmjtt4sij, (((uint8_t *)t99) + 1568), t100);
    }
    {
        void * t101 = gm_l0;
        gm_trqfvmjtt4sij t102 = F.l9;
        GM_ST(gm_trqfvmjtt4sij, (((uint8_t *)t101) + 1608), t102);
    }
    {
        {
            void * t104 = gm_l0;
            void * t105 = (void *)(((uint8_t *)t104) + 1648);
            gm_tf5srh4mp4b3i t106 = gm_f_pop_j4rpvm5(t105);
        }
    }
    {
        uint64_t t108 = gm_l1;
        uint64_t t109 = (uintptr_t)t108;
        void * t110 = gm_l0;
        uint64_t t111 = GM_LD(uintptr_t, ((((uint8_t *)t110) + 304) + 0) + 8);
        if (t109 >= (uintptr_t)(t111)) gm_trap_index(t109, t111, gm_srclv0dcbbfhves, 797, 8);
        uint8_t t112 = ((GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t110) + 304) + 0)) + t109 * 216) + 0) == ((uint8_t)1ULL)));
        if (!t112) goto ie107;
        {
            uint64_t t114 = gm_l1;
            uint64_t t115 = (uintptr_t)t114;
            void * t116 = gm_l0;
            uint64_t t117 = GM_LD(uintptr_t, ((((uint8_t *)t116) + 304) + 0) + 8);
            if (t115 >= (uintptr_t)(t117)) gm_trap_index(t115, t117, gm_srclv0dcbbfhves, 797, 44);
            gm_t6qooom8qa2do t118 = F.l10;
            gm_tagrkbqh054h7 t119;
            GM_ST(uint8_t, (uint8_t *)&t119 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t119 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t119 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t119 + 0), ((uint8_t)2ULL));
            GM_ST(gm_t6qooom8qa2do, ((uint8_t *)&t119 + 8), t118);
            GM_ST(gm_tagrkbqh054h7, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t116) + 304) + 0)) + t115 * 216), t119);
        }
        goto ix107;
ie107:;
ix107:;
    }
    {
        uint64_t t121 = gm_l1;
        uint64_t t122 = (uintptr_t)t121;
        void * t123 = gm_l0;
        uint64_t t124 = GM_LD(uintptr_t, ((((uint8_t *)t123) + 304) + 0) + 8);
        if (t122 >= (uintptr_t)(t124)) gm_trap_index(t122, t124, gm_srclv0dcbbfhves, 798, 20);
        uint8_t t125 = GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t123) + 304) + 0)) + t122 * 216) + 0));
        if (t125 != ((uint8_t)2ULL)) goto ie120;
        gm_t6qooom8qa2do t126 = GM_LD(gm_t6qooom8qa2do, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t123) + 304) + 0)) + t122 * 216) + 8));
        F.l11 = t126;
        {
            uint8_t t128 = 0;
            F.ret = (*(gm_t6qooom8qa2do *)((uint8_t *)&F.l11));
            gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = t128;
            goto gm_ld1_1;
        }
        goto ix120;
ie120:;
ix120:;
    }
    {
        gm_t6qooom8qa2do t129;
        memset(&t129, 0, sizeof t129);
        GM_ST(uint8_t, ((uint8_t *)&t129 + 0), ((uint8_t)11ULL));
        uint8_t t130 = 0;
        F.ret = t129;
        gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = t130;
        goto gm_ld1_1;
    }
    {
        {
            void * t132 = gm_l0;
            uint8_t t133 = gm_l5;
            GM_ST(uint8_t, (((uint8_t *)t132) + 2536), t133);
        }
    }
    goto gm_ls134;
gm_ld1_1:;
    {
        gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
        {
            void * t136 = gm_l0;
            uint8_t t137 = gm_l5;
            GM_ST(uint8_t, (((uint8_t *)t136) + 2536), t137);
        }
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
__attribute__((aligned(4))) gm_b0_1 gm_f_resolve_declarations_vrpg1fl(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_resolve_declarations_vrpg1fl F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* done */
    uint64_t gm_l2; /* m */
    uint64_t gm_l3; /* n */
    uint64_t gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    uint64_t gm_l6; /* i */
    GM_STACK_CHECK(&F, 587u);
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
            void * t4 = gm_l0;
            void * t5 = GM_LD(void *, (((uint8_t *)t4) + 0));
            uint64_t t6 = GM_LD(uintptr_t, ((((uint8_t *)t5) + 200) + 0) + 8);
            uint8_t t7 = (t3 < t6);
            if (!t7) goto lb2;
            {
                uint64_t t10 = gm_l1;
                gm_l2 = t10;
            }
            {
                uint64_t t11 = gm_l1;
                uint64_t r12; bool o13 = __builtin_add_overflow(t11, ((uint64_t)1ULL), &r12);
                if (o13) gm_trap("integer overflow in `+`", gm_srclv0dcbbfhves, 728, 9);
                uint64_t t14 = r12;
                gm_l1 = t14;
            }
            {
                uint64_t t16 = gm_l2;
                uint64_t t17 = (uintptr_t)t16;
                void * t18 = gm_l0;
                void * t19 = GM_LD(void *, (((uint8_t *)t18) + 0));
                uint64_t t20 = GM_LD(uintptr_t, ((((uint8_t *)t19) + 200) + 0) + 8);
                if (t17 >= (uintptr_t)(t20)) gm_trap_index(t17, t20, gm_srclv0dcbbfhves, 729, 12);
                uint8_t t21 = GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t19) + 200) + 0)) + t17 * 184) + 112));
                if (!t21) goto ie15;
                {
                    goto lc2;
                }
                goto ix15;
ie15:;
ix15:;
            }
            {
                uint64_t t23 = gm_l2;
                uint64_t t24 = (uintptr_t)t23;
                void * t25 = gm_l0;
                void * t26 = GM_LD(void *, (((uint8_t *)t25) + 0));
                uint64_t t27 = GM_LD(uintptr_t, ((((uint8_t *)t26) + 200) + 0) + 8);
                if (t24 >= (uintptr_t)(t27)) gm_trap_index(t24, t27, gm_srclv0dcbbfhves, 731, 14);
                uint64_t t28 = GM_LD(uintptr_t, ((((uint8_t *)GM_LD(void *, ((((uint8_t *)t26) + 200) + 0)) + t24 * 184) + 16) + 0) + 8);
                gm_l3 = t28;
            }
            {
                {
                    gm_l5 = ((uint64_t)0ULL);
                }
                {
                    uint64_t t30 = gm_l3;
                    gm_l4 = t30;
                }
                {
                    for (;;) {
                        uint64_t t32 = gm_l5;
                        uint64_t t33 = gm_l4;
                        uint8_t t34 = (t32 < t33);
                        if (!t34) goto lb31;
                        {
                            uint64_t t37 = gm_l5;
                            uint64_t t38 = t37;
                            uint64_t t41 = GM_LD(uint64_t, ((uint8_t *)&t38));
                            gm_l6 = t41;
                            {
                                {
                                    void * t44 = gm_l0;
                                    uint64_t t45 = gm_l6;
                                    uint64_t t46 = (uintptr_t)t45;
                                    uint64_t t47 = gm_l2;
                                    uint64_t t48 = (uintptr_t)t47;
                                    void * t49 = gm_l0;
                                    void * t50 = GM_LD(void *, (((uint8_t *)t49) + 0));
                                    uint64_t t51 = GM_LD(uintptr_t, ((((uint8_t *)t50) + 200) + 0) + 8);
                                    if (t48 >= (uintptr_t)(t51)) gm_trap_index(t48, t51, gm_srclv0dcbbfhves, 733, 32);
                                    uint64_t t52 = GM_LD(uintptr_t, ((((uint8_t *)GM_LD(void *, ((((uint8_t *)t50) + 200) + 0)) + t48 * 184) + 16) + 0) + 8);
                                    if (t46 >= (uintptr_t)(t52)) gm_trap_index(t46, t52, gm_srclv0dcbbfhves, 733, 32);
                                    uint64_t t53 = GM_LD(uint64_t, (((uint8_t *)GM_LD(void *, ((((uint8_t *)GM_LD(void *, ((((uint8_t *)t50) + 200) + 0)) + t48 * 184) + 16) + 0)) + t46 * 24) + 16));
                                    gm_t6qooom8qa2do t54;
                                    gm_f_resolve_decl_pus042v((void *)&t54, (void *)NULL, cx, NULL, t44, t53);
                                    if (gm_unw.kind) {
                                        goto gm_ll35;
                                    }
                                }
                            }
                            goto me39;
mn40:;
                            gm_trap("no match arm matched the scrutinee", gm_srclv0dcbbfhves, 732, 9);
me39:;
                        }
lc31:;
                        {
                            uint64_t t56 = gm_l5;
                            uint64_t r57; bool o58 = __builtin_add_overflow(t56, ((uint64_t)1ULL), &r57);
                            uint64_t t59 = r57;
                            gm_l5 = t59;
                        }
                    }
gm_ll35:;
                    if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb31; }
                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc31; }
                    }
                    goto gm_ll8;
lb31:;
                }
            }
lc2:;
        }
gm_ll8:;
        if (gm_unw.frame == &F.h && gm_unw.label == 0) {
            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb2; }
            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc2; }
        }
        goto gm_lad;
lb2:;
    }
    {
        void * t60 = gm_l0;
        gm_b0_1 t61 = gm_f_check_method_candidates_k08rvu6((void *)NULL, cx, NULL, t60);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t62 = gm_l0;
        void * t63 = GM_LD(void *, (((uint8_t *)t62) + 0));
        gm_b0_1 t64 = gm_f_check_import_cycles_2u7oqo0((void *)NULL, cx, NULL, t63);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t65 = gm_l0;
        gm_b0_1 t66 = gm_f_check_pub_signatures_plkds6r((void *)NULL, cx, NULL, t65);
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
inline __attribute__((aligned(4))) void gm_f_sema_r1nlnnm(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_sema_r1nlnnm F;
    void * gm_l0; /* l */
    GM_STACK_CHECK(&F, 156u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        memset((uint8_t *)&F.l1 + 585, 0, 7);
        memset((uint8_t *)&F.l1 + 673, 0, 7);
        memset((uint8_t *)&F.l1 + 1365, 0, 3);
        memset((uint8_t *)&F.l1 + 1772, 0, 4);
        memset((uint8_t *)&F.l1 + 1820, 0, 4);
        memset((uint8_t *)&F.l1 + 1865, 0, 7);
        memset((uint8_t *)&F.l1 + 1964, 0, 4);
        memset((uint8_t *)&F.l1 + 2164, 0, 4);
        memset((uint8_t *)&F.l1 + 2537, 0, 7);
        void * t2 = gm_l0;
        GM_ST(void *, (uint8_t *)&F.l1 + 0, t2);
        gm_f_types_8a61ga3((void *)&(*(gm_tpcj7kul6hlib *)((uint8_t *)&F.l1 + 8)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_list_eb5ua5r((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l1 + 304)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_list_dvp9pla((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l1 + 344)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_list_dvbol2p((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l1 + 384)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_list_0bg6tcp((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l1 + 424)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_list_atkrn3m((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l1 + 464)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_list_m7es0sh((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l1 + 504)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_list_qv7bq94((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l1 + 544)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(uint8_t, (uint8_t *)&F.l1 + 584, ((uint8_t)0));
        gm_f_list_vhqj1o4((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l1 + 592)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_list_sajcold((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l1 + 632)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(uint8_t, (uint8_t *)&F.l1 + 672, ((uint8_t)0));
        gm_f_list_4kh12ib((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l1 + 680)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_list_8r7pelg((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l1 + 720)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_list_9q64p2g((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l1 + 760)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_list_tqcq99u((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l1 + 800)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_list_uun92du((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l1 + 840)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_list_epvducg((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l1 + 880)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_list_go2qg0b((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l1 + 920)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t9805qtonbsag t3;
        memset(&t3, 0, sizeof t3);
        GM_ST(gm_t9805qtonbsag, (uint8_t *)&F.l1 + 960, t3);
        gm_f_list_c02589n((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l1 + 968)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_list_c02589n((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l1 + 1008)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_list_2343nn9((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l1 + 1048)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_list_h93u1dq((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l1 + 1088)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_list_t29l5ca((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l1 + 1128)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_list_au01ire((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l1 + 1168)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_hash_map_iug9p19((void *)&(*(gm_t4c7ipjj9s3fp *)((uint8_t *)&F.l1 + 1208)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_hash_map_iug9p19((void *)&(*(gm_t4c7ipjj9s3fp *)((uint8_t *)&F.l1 + 1264)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_list_406coeg((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l1 + 1320)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(uint32_t, (uint8_t *)&F.l1 + 1360, ((uint32_t)0ULL));
        GM_ST(uint8_t, (uint8_t *)&F.l1 + 1364, ((uint8_t)0));
        gm_f_list_go2qg0b((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l1 + 1368)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_list_dql1gah((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l1 + 1408)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_list_5pborc0((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l1 + 1448)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_list_mvhhv5u((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l1 + 1488)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_list_ue2t2bm((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l1 + 1528)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_list_1jabh22((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l1 + 1568)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_list_0bj26n7((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l1 + 1608)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_list_5dhgg4i((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l1 + 1648)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_list_5dhgg4i((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l1 + 1688)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_list_go2qg0b((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l1 + 1728)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(uint32_t, (uint8_t *)&F.l1 + 1768, ((uint32_t)0ULL));
        gm_f_list_ib5k4gb((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l1 + 1776)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(uint32_t, (uint8_t *)&F.l1 + 1816, ((uint32_t)0ULL));
        gm_f_list_au01ire((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l1 + 1824)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(uint8_t, (uint8_t *)&F.l1 + 1864, ((uint8_t)0));
        gm_f_list_v3cpbrb((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l1 + 1872)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_list_je8tl46((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l1 + 1912)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(uint32_t, (uint8_t *)&F.l1 + 1952, ((uint32_t)1ULL));
        gm_t9805qtonbsag t4;
        memset(&t4, 0, sizeof t4);
        GM_ST(gm_t9805qtonbsag, (uint8_t *)&F.l1 + 1956, t4);
        gm_f_list_l3jkoh2((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l1 + 1968)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_hash_map_84luf3l((void *)&(*(gm_t4c7ipjj9s3fp *)((uint8_t *)&F.l1 + 2008)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_hash_map_iibum07((void *)&(*(gm_t4c7ipjj9s3fp *)((uint8_t *)&F.l1 + 2064)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_list_go2qg0b((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l1 + 2120)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(uint32_t, (uint8_t *)&F.l1 + 2160, ((uint32_t)0ULL));
        GM_ST(void *, (uint8_t *)&F.l1 + 2168, ((void *)0));
        GM_ST(void *, (uint8_t *)&F.l1 + 2176, ((void *)0));
        GM_ST(void *, (uint8_t *)&F.l1 + 2184, ((void *)0));
        gm_f_list_tn37ahu((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l1 + 2192)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_list_i6iqfhd((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l1 + 2232)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_list_0npefvp((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l1 + 2272)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint64_t t5 = GM_LD(uint64_t, gm_cbbjgr0cpr9);
        GM_ST(uint64_t, (uint8_t *)&F.l1 + 2312, t5);
        gm_f_hash_map_0vd239u((void *)&(*(gm_t4c7ipjj9s3fp *)((uint8_t *)&F.l1 + 2320)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_list_1teief7((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l1 + 2376)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_list_kk91efj((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l1 + 2416)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_list_epvducg((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l1 + 2456)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_list_7s4om10((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l1 + 2496)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(uint8_t, (uint8_t *)&F.l1 + 2536, ((uint8_t)0));
        gm_f_list_s83jn2q((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l1 + 2544)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_list_s83jn2q((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l1 + 2584)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_hash_map_1so3e0v((void *)&(*(gm_t4c7ipjj9s3fp *)((uint8_t *)&F.l1 + 2624)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_hash_map_ve01i1t((void *)&(*(gm_t4c7ipjj9s3fp *)((uint8_t *)&F.l1 + 2680)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_hash_map_8lre197((void *)&(*(gm_t4c7ipjj9s3fp *)((uint8_t *)&F.l1 + 2736)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t6 = (void *)(((uint8_t *)&F.l1) + 1048);
        gm_tammvh1iespi3 t7;
        gm_b16_8 t8;
        GM_ST(void *, (uint8_t *)&t8, (void *)(gm_snenkhfev166s));
        GM_ST(uintptr_t, (uint8_t *)&t8 + 8, (uintptr_t)(2));
        GM_ST(gm_b16_8, (uint8_t *)&t7 + 0, t8);
        GM_ST(uint64_t, (uint8_t *)&t7 + 16, ((uint64_t)0ULL));
        gm_b16_8 t9 = gm_f_empty_slice_d914i5u();
        GM_ST(gm_b16_8, (uint8_t *)&t7 + 24, t9);
        gm_b16_8 t10 = gm_f_empty_slice_p6mn4us();
        GM_ST(gm_b16_8, (uint8_t *)&t7 + 40, t10);
        gm_tftb1odc739t5 t11 = gm_f_push_puil3ql(t6, &t7);
        gm_tftb1odc739t5 t12 = t11;
        if (((((uint8_t *)&t12))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t12) + 1, gm_srclv0dcbbfhves, 570, 5);
        gm_b0_1 t13 = GM_LD(gm_b0_1, (((uint8_t *)&t12) + 1));
    }
    {
        memmove(gm_out, ((uint8_t *)&F.l1), 2792);
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
extern __attribute__((aligned(4))) gm_b0_1 gm_f_set_inst_def_tcpabg0(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, const void * a2);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_set_state_3j300fi(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, const void * a2);
extern __attribute__((aligned(4))) gm_b16_8 gm_f_show_8uijkbs(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) void gm_f_state_a5bnsjb(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1) {
    struct gm_fr_state_a5bnsjb F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* id */
    uint64_t gm_l2; /* m */
    GM_STACK_CHECK(&F, 1122u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        for (;;) {
            void * t3 = gm_l0;
            uint64_t t4 = GM_LD(uintptr_t, ((((uint8_t *)t3) + 304) + 0) + 8);
            uint64_t t5 = gm_l1;
            uint8_t t6 = (t4 <= t5);
            if (!t6) goto lb2;
            {
                void * t9 = gm_l0;
                void * t10 = (void *)(((uint8_t *)t9) + 304);
                gm_tagrkbqh054h7 t11;
                memset(&t11, 0, sizeof t11);
                GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
                gm_tftb1odc739t5 t12 = gm_f_push_kpjuj6c(t10, &t11);
                gm_tftb1odc739t5 t13 = t12;
                if (((((uint8_t *)&t13))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t13) + 1, gm_srclv0dcbbfhves, 742, 38);
                gm_b0_1 t14 = GM_LD(gm_b0_1, (((uint8_t *)&t13) + 1));
            }
lc2:;
        }
lb2:;
    }
    {
        uint64_t t16 = gm_l1;
        uint64_t t17 = (uintptr_t)t16;
        void * t18 = gm_l0;
        uint64_t t19 = GM_LD(uintptr_t, ((((uint8_t *)t18) + 304) + 0) + 8);
        if (t17 >= (uintptr_t)(t19)) gm_trap_index(t17, t19, gm_srclv0dcbbfhves, 744, 8);
        uint8_t t20 = ((GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t18) + 304) + 0)) + t17 * 216) + 0) == ((uint8_t)0ULL)));
        if (!t20) goto ie15;
        uint64_t t21 = gm_l1;
        uint64_t t22 = (uintptr_t)t21;
        void * t23 = gm_l0;
        void * t24 = GM_LD(void *, (((uint8_t *)t23) + 0));
        uint64_t t25 = GM_LD(uintptr_t, ((((uint8_t *)t24) + 240) + 0) + 8);
        if (t22 >= (uintptr_t)(t25)) gm_trap_index(t22, t25, gm_srclv0dcbbfhves, 744, 46);
        uint8_t t26;
        uint8_t t29 = GM_LD(uint8_t, (((((uint8_t *)GM_LD(void *, ((((uint8_t *)t24) + 240) + 0)) + t22 * 216) + 40) + 64) + 0));
        if (t29 != ((uint8_t)4ULL)) goto mn28;
        t26 = ((uint8_t)1);
        goto me27;
mn28:;
        t26 = ((uint8_t)0);
        goto me27;
mn30:;
        gm_trap("no match arm matched the scrutinee", gm_srclv0dcbbfhves, 744, 46);
me27:;
        if (!t26) goto ie15;
        {
            uint64_t t33 = gm_l1;
            uint64_t t34 = (uintptr_t)t33;
            void * t35 = gm_l0;
            void * t36 = GM_LD(void *, (((uint8_t *)t35) + 0));
            uint64_t t37 = GM_LD(uintptr_t, ((((uint8_t *)t36) + 240) + 0) + 8);
            if (t34 >= (uintptr_t)(t37)) gm_trap_index(t34, t37, gm_srclv0dcbbfhves, 745, 26);
            uint8_t t38 = GM_LD(uint8_t, ((((uint8_t *)GM_LD(void *, ((((uint8_t *)t36) + 240) + 0)) + t34 * 216) + 200) + 0));
            if (t38 != ((uint8_t)1ULL)) goto ie32;
            uint64_t t39 = GM_LD(uint64_t, ((((uint8_t *)GM_LD(void *, ((((uint8_t *)t36) + 240) + 0)) + t34 * 216) + 200) + 8));
            gm_l2 = t39;
            {
                uint64_t t41 = gm_l1;
                uint64_t t42 = (uintptr_t)t41;
                void * t43 = gm_l0;
                uint64_t t44 = GM_LD(uintptr_t, ((((uint8_t *)t43) + 304) + 0) + 8);
                if (t42 >= (uintptr_t)(t44)) gm_trap_index(t42, t44, gm_srclv0dcbbfhves, 745, 54);
                uint64_t t45 = gm_l2;
                gm_t6qooom8qa2do t46;
                memset(&t46, 0, sizeof t46);
                GM_ST(uint8_t, ((uint8_t *)&t46 + 0), ((uint8_t)9ULL));
                GM_ST(uint64_t, ((uint8_t *)&t46 + 8), t45);
                gm_tagrkbqh054h7 t47;
                GM_ST(uint8_t, (uint8_t *)&t47 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t47 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t47 + 4, 0);
                GM_ST(uint8_t, ((uint8_t *)&t47 + 0), ((uint8_t)2ULL));
                GM_ST(gm_t6qooom8qa2do, ((uint8_t *)&t47 + 8), t46);
                GM_ST(gm_tagrkbqh054h7, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t43) + 304) + 0)) + t42 * 216), t47);
            }
            goto ix32;
ie32:;
ix32:;
        }
        goto ix15;
ie15:;
ix15:;
    }
    {
        uint64_t t48 = gm_l1;
        uint64_t t49 = (uintptr_t)t48;
        void * t50 = gm_l0;
        uint64_t t51 = GM_LD(uintptr_t, ((((uint8_t *)t50) + 304) + 0) + 8);
        if (t49 >= (uintptr_t)(t51)) gm_trap_index(t49, t51, gm_srclv0dcbbfhves, 747, 12);
        gm_tagrkbqh054h7 t52 = GM_LD(gm_tagrkbqh054h7, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t50) + 304) + 0)) + t49 * 216));
        uint8_t t53 = 0;
        F.ret = t52;
        goto gm_fret;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
extern __attribute__((aligned(4))) gm_b0_1 gm_f_unsupported_t7lgvp9(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_walk_v858co7(void * a0, gm_csguankko4lmhd a1, gm_closure a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_walk_v858co7 F;
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 116u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    F.l2 = a2;
    {
        gm_closure t2 = F.l2;
        gm_t88rcucg1b7uk t3 = gm_l1;
        gm_closure t4 = t2;
        gm_node *ccx5 = t4.cx;
        void * t6 = t4.code;
        gm_b0_1 t7;
        if (GM_PTR_TAGGED(t6)) {
            ((void (*)(gm_csguankko4lmhd))GM_PTR_UNTAG(t6))(GM_CONV(gm_csguankko4lmhd, t3));
        } else {
            t7 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, gm_t88rcucg1b7uk))t6)(t4.env, ccx5, NULL, t3);
        }
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t8 = F.l0;
        void * t9 = (void *)(((uint8_t *)t8) + 8);
        gm_t88rcucg1b7uk t10 = gm_l1;
        gm_f_kind_jcnt4hm((void *)&F.l3, t9, GM_CONV(gm_csguankko4lmhd, t10));
    }
    {
        void * t11 = (void *)((uint8_t *)&F.l3);
        gm_closure t12 = ((gm_closure){(void *)gm_f_fn_bfunt71, (void *)&F.h, cx});
        gm_f_for_each_component_o5cuh7c(t11, t12);
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
