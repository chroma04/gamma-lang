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
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint64_t f0; } gm_t4ce0jd3tdro4;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint32_t f0; uint8_t f1; uint8_t pe[3]; } gm_t9805qtonbsag;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint8_t f0; uint8_t p1[7]; void * f1; void * f2; } gm_ta612fil812pm;
typedef struct { void * f0; void * f1; void * f2; uint8_t f3; uint8_t pe[7]; } gm_tao63pk3r18v5;
typedef struct { void * f0; uint32_t f1; uint8_t pe[4]; } gm_tbl8il92l97ut;
typedef struct { uint32_t f0; uint32_t f1; } gm_tcc1rq4n1avb2;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint8_t f0; uint8_t p1[7]; void * f1; } gm_tgtub0v94d5u5;
typedef struct { uint8_t f0; uint8_t p1[1]; uint16_t f1; } gm_tjcj4j2gbh6c8;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { void * f0; void * f1; uint8_t f2; uint8_t pe[7]; } gm_tlcl4u558l6p5;
typedef struct { void * f0; void * f1; uint32_t f2; uint32_t f3; } gm_tmm67r2c94d5j;
typedef struct { uint8_t f0; uint8_t p1[3]; union { uint32_t v0; } f1; } gm_tmn7n0n9raou5;
typedef struct { uint8_t f0; uint8_t f1; uint8_t f2; } gm_toh0ootodhjue;
typedef struct { uint8_t f0; uint8_t f1; } gm_tqs5p4n2tq19v;
typedef struct { void * f0; uint8_t f1; uint8_t pe[7]; } gm_tsplfr33dsvji;
typedef struct { uint32_t f0; uint8_t p1[4]; void * f1; } gm_ttggq7ng4kp3p;
_Static_assert(sizeof(gm_csguankko4lmhd) == 4 && _Alignof(gm_csguankko4lmhd) == 4 && offsetof(gm_csguankko4lmhd, f0) == 0, "layout of `Mutex`");
typedef struct { void * f0; gm_t9805qtonbsag f1; uint32_t f2; uint8_t pe[4]; } gm_t083kd7b1kkag;
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_t08olq97rscn3;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; } gm_t0uuhaljsf410;
typedef struct { gm_b16_8 f0; uint64_t f1; } gm_t3877pk7lv748;
typedef struct { void * f0; gm_t9805qtonbsag f1; void * f2; } gm_t3dcoc7077t1q;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; uint8_t f2; uint8_t pe[3]; } gm_t3rcjibl8kkg4;
_Static_assert(sizeof(gm_t4ce0jd3tdro4) == 8 && _Alignof(gm_t4ce0jd3tdro4) == 8 && offsetof(gm_t4ce0jd3tdro4, f0) == 0, "layout of `Instant`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
typedef struct { gm_tqs5p4n2tq19v f0; gm_tqs5p4n2tq19v f1; gm_tqs5p4n2tq19v f2; gm_tqs5p4n2tq19v f3; gm_tqs5p4n2tq19v f4; gm_tqs5p4n2tq19v f5; gm_tqs5p4n2tq19v f6; } gm_t6s1lqopkm7fs;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; void * f2; void * f3; } gm_t710lsrq1m8gk;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_t79rbdv4rafe5;
typedef struct { uint8_t f0; gm_t9qkh0rsooc8d f1; uint8_t f2; uint8_t f3; uint8_t f4; uint8_t p5[3]; uint32_t f5; gm_t9805qtonbsag f6; gm_t9qkh0rsooc8d f7; uint8_t pe[3]; } gm_t7g88i52rrtm5;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; void * f1; } gm_t8ghgl2jj0qha;
typedef struct { gm_tmn7n0n9raou5 f0; void * f1; } gm_t8jsc4n9vp2ib;
_Static_assert(sizeof(gm_t9805qtonbsag) == 8 && _Alignof(gm_t9805qtonbsag) == 4 && offsetof(gm_t9805qtonbsag, f0) == 0 && offsetof(gm_t9805qtonbsag, f1) == 4, "layout of `?u32`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
_Static_assert(sizeof(gm_ta612fil812pm) == 24 && _Alignof(gm_ta612fil812pm) == 8 && offsetof(gm_ta612fil812pm, f0) == 0 && offsetof(gm_ta612fil812pm, f1) == 8 && offsetof(gm_ta612fil812pm, f2) == 16, "layout of `(is_and: bool, lhs: ^Expr, rhs: ^Expr)`");
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t f1; } gm_taari1s0m51a7;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
_Static_assert(sizeof(gm_tao63pk3r18v5) == 32 && _Alignof(gm_tao63pk3r18v5) == 8 && offsetof(gm_tao63pk3r18v5, f0) == 0 && offsetof(gm_tao63pk3r18v5, f1) == 8 && offsetof(gm_tao63pk3r18v5, f2) == 16 && offsetof(gm_tao63pk3r18v5, f3) == 24, "layout of `SliceExpr`");
typedef struct { gm_b16_8 f0; uint32_t f1; uint32_t f2; gm_b16_8 f3; } gm_tb40qn7hk0tl9;
_Static_assert(sizeof(gm_tbl8il92l97ut) == 16 && _Alignof(gm_tbl8il92l97ut) == 8 && offsetof(gm_tbl8il92l97ut, f0) == 0 && offsetof(gm_tbl8il92l97ut, f1) == 8, "layout of `SpecCall`");
typedef struct { uint8_t f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; } gm_tbngattl1fo51;
_Static_assert(sizeof(gm_tcc1rq4n1avb2) == 8 && _Alignof(gm_tcc1rq4n1avb2) == 4 && offsetof(gm_tcc1rq4n1avb2, f0) == 0 && offsetof(gm_tcc1rq4n1avb2, f1) == 4, "layout of `LineCol`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tcmkompbl09il;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; gm_t88rcucg1b7uk f2; } gm_tcmtrmhj7ak4u;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_tcc1rq4n1avb2 v0; } f1; } gm_te9hrk0u35rev;
typedef struct { uint8_t f0; union { gm_toh0ootodhjue v0; } f1; } gm_tesaltf0mhcki;
typedef struct { uint64_t f0; gm_b16_8 f1; } gm_tf1bjg81u0glv;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; } gm_tfeab279c16rr;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_b16_8 f0; void * f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_tgabaikr08mjg;
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; } gm_tgglga8ro3isc;
_Static_assert(sizeof(gm_tgtub0v94d5u5) == 16 && _Alignof(gm_tgtub0v94d5u5) == 8 && offsetof(gm_tgtub0v94d5u5, f0) == 0 && offsetof(gm_tgtub0v94d5u5, f1) == 8, "layout of `(is_const: bool, inner: ^Expr)`");
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { gm_b16_8 f0; gm_t9qkh0rsooc8d f1; uint8_t pe[7]; } gm_tibupt4dmcqai;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
typedef struct { gm_b16_8 f0; gm_tf5srh4mp4b3i f1; } gm_tj64ui6kruni6;
_Static_assert(sizeof(gm_tjcj4j2gbh6c8) == 4 && _Alignof(gm_tjcj4j2gbh6c8) == 2 && offsetof(gm_tjcj4j2gbh6c8, f0) == 0 && offsetof(gm_tjcj4j2gbh6c8, f1) == 2, "layout of `(signed: bool, bits: u16)`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_tf5srh4mp4b3i f1; uint8_t f2; uint8_t pe[7]; } gm_tjpqvfd72o62u;
typedef struct { void * f0; gm_b16_8 f1; } gm_tkaapcii86udk;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t f1; uint8_t pe[3]; } gm_tkn42u7na3t81;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_tlcl4u558l6p5) == 24 && _Alignof(gm_tlcl4u558l6p5) == 8 && offsetof(gm_tlcl4u558l6p5, f0) == 0 && offsetof(gm_tlcl4u558l6p5, f1) == 8 && offsetof(gm_tlcl4u558l6p5, f2) == 16, "layout of `(lo: ^Expr, hi: ^Expr, inclusive: bool)`");
typedef struct { void * f0; gm_b16_8 f1; uint8_t f2; uint8_t pe[7]; } gm_tlljg61a2ctgq;
_Static_assert(sizeof(gm_tmm67r2c94d5j) == 24 && _Alignof(gm_tmm67r2c94d5j) == 8 && offsetof(gm_tmm67r2c94d5j, f0) == 0 && offsetof(gm_tmm67r2c94d5j, f1) == 8 && offsetof(gm_tmm67r2c94d5j, f2) == 16 && offsetof(gm_tmm67r2c94d5j, f3) == 20, "layout of `Printer`");
_Static_assert(sizeof(gm_tmn7n0n9raou5) == 8 && _Alignof(gm_tmn7n0n9raou5) == 4 && offsetof(gm_tmn7n0n9raou5, f0) == 0 && offsetof(gm_tmn7n0n9raou5, f1) == 4, "layout of `Coercion`");
typedef struct { gm_t9805qtonbsag f0; uint64_t f1; uint8_t f2; uint8_t pe[7]; } gm_tmnrj1ud20adj;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t88rcucg1b7uk v0; } f1; } gm_tnnjeerolos0a;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
_Static_assert(sizeof(gm_toh0ootodhjue) == 3 && _Alignof(gm_toh0ootodhjue) == 1 && offsetof(gm_toh0ootodhjue, f0) == 0 && offsetof(gm_toh0ootodhjue, f1) == 1 && offsetof(gm_toh0ootodhjue, f2) == 2, "layout of `Restr`");
typedef struct { gm_b16_8 f0; void * f1; } gm_tpe1hv3fekn05;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_b16_8 f2; } gm_tq3m5qgvkpje8;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; void * v1; } f1; } gm_tq7s98ihonkju;
_Static_assert(sizeof(gm_tqs5p4n2tq19v) == 2 && _Alignof(gm_tqs5p4n2tq19v) == 1 && offsetof(gm_tqs5p4n2tq19v, f0) == 0 && offsetof(gm_tqs5p4n2tq19v, f1) == 1, "layout of `(u8, u8)`");
typedef struct { gm_b16_8 f0; void * f1; void * f2; } gm_trl2khmuphncd;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_t9qkh0rsooc8d v3; } f1; } gm_ts6oqppjh0ba3;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_ts78ca2amthgu;
_Static_assert(sizeof(gm_tsplfr33dsvji) == 16 && _Alignof(gm_tsplfr33dsvji) == 8 && offsetof(gm_tsplfr33dsvji, f0) == 0 && offsetof(gm_tsplfr33dsvji, f1) == 8, "layout of `(expr: ^Expr, is_result: bool)`");
_Static_assert(sizeof(gm_ttggq7ng4kp3p) == 16 && _Alignof(gm_ttggq7ng4kp3p) == 8 && offsetof(gm_ttggq7ng4kp3p, f0) == 0 && offsetof(gm_ttggq7ng4kp3p, f1) == 8, "layout of `(index: u32, payload: ?^Value)`");
typedef struct { void * f0; uint32_t f1; gm_tl874t2atnlh3 f2; } gm_tupk62cr6lbdu;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; void * f2; gm_t9qkh0rsooc8d f3; uint8_t p4[7]; void * f4; gm_tl874t2atnlh3 f5; uint8_t pe[4]; } gm_tv2uf4leil5hq;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_tcc1rq4n1avb2 v3; uint32_t v4; uint32_t v5; } f1; } gm_tvdg94bk7aa22;
_Static_assert(sizeof(gm_t083kd7b1kkag) == 24 && _Alignof(gm_t083kd7b1kkag) == 8 && offsetof(gm_t083kd7b1kkag, f0) == 0 && offsetof(gm_t083kd7b1kkag, f1) == 8 && offsetof(gm_t083kd7b1kkag, f2) == 16, "layout of `TryExpr`");
_Static_assert(sizeof(gm_t08olq97rscn3) == 32 && _Alignof(gm_t08olq97rscn3) == 8 && offsetof(gm_t08olq97rscn3, f0) == 0 && offsetof(gm_t08olq97rscn3, f1) == 16, "layout of `File`");
_Static_assert(sizeof(gm_t0uuhaljsf410) == 64 && _Alignof(gm_t0uuhaljsf410) == 8 && offsetof(gm_t0uuhaljsf410, f0) == 0 && offsetof(gm_t0uuhaljsf410, f1) == 16 && offsetof(gm_t0uuhaljsf410, f2) == 32 && offsetof(gm_t0uuhaljsf410, f3) == 48, "layout of `(string, string, string, string)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tfeab279c16rr v0; gm_tj64ui6kruni6 v1; } f1; } gm_t1jll27j7ae34;
typedef struct { gm_t08olq97rscn3 f0; uint8_t f1; uint8_t pe[7]; } gm_t1s75i08qtptd;
_Static_assert(sizeof(gm_t3877pk7lv748) == 24 && _Alignof(gm_t3877pk7lv748) == 8 && offsetof(gm_t3877pk7lv748, f0) == 0 && offsetof(gm_t3877pk7lv748, f1) == 16, "layout of `WasmReader`");
_Static_assert(sizeof(gm_t3dcoc7077t1q) == 24 && _Alignof(gm_t3dcoc7077t1q) == 8 && offsetof(gm_t3dcoc7077t1q, f0) == 0 && offsetof(gm_t3dcoc7077t1q, f1) == 8 && offsetof(gm_t3dcoc7077t1q, f2) == 16, "layout of `ElseExpr`");
_Static_assert(sizeof(gm_t3rcjibl8kkg4) == 12 && _Alignof(gm_t3rcjibl8kkg4) == 4 && offsetof(gm_t3rcjibl8kkg4, f0) == 0 && offsetof(gm_t3rcjibl8kkg4, f1) == 4 && offsetof(gm_t3rcjibl8kkg4, f2) == 8, "layout of `(a: Ty, b: Ty, negated: bool)`");
typedef struct { gm_tiilmfd16v3fc f0; uint32_t f1; uint8_t pe[4]; } gm_t419mo45g7j0a;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tcmkompbl09il v0; gm_tiilmfd16v3fc v1; gm_b16_8 v2; } f1; } gm_t428ue2u3hf5g;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t p3[4]; gm_trqfvmjtt4sij f3; gm_trqfvmjtt4sij f4; gm_trqfvmjtt4sij f5; gm_th2lqbo77pvsc f6; } gm_t4gut2c9fj1kt;
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_t6s1lqopkm7fs) == 14 && _Alignof(gm_t6s1lqopkm7fs) == 1 && offsetof(gm_t6s1lqopkm7fs, f0) == 0 && offsetof(gm_t6s1lqopkm7fs, f1) == 2 && offsetof(gm_t6s1lqopkm7fs, f2) == 4 && offsetof(gm_t6s1lqopkm7fs, f3) == 6 && offsetof(gm_t6s1lqopkm7fs, f4) == 8 && offsetof(gm_t6s1lqopkm7fs, f5) == 10 && offsetof(gm_t6s1lqopkm7fs, f6) == 12, "layout of `(?u8, ?u8, ?u8, ?u8, ?u8, ?u8, ?u8)`");
_Static_assert(sizeof(gm_t710lsrq1m8gk) == 40 && _Alignof(gm_t710lsrq1m8gk) == 8 && offsetof(gm_t710lsrq1m8gk, f0) == 0 && offsetof(gm_t710lsrq1m8gk, f1) == 8 && offsetof(gm_t710lsrq1m8gk, f2) == 24 && offsetof(gm_t710lsrq1m8gk, f3) == 32, "layout of `WithExpr`");
_Static_assert(sizeof(gm_t79rbdv4rafe5) == 40 && _Alignof(gm_t79rbdv4rafe5) == 8 && offsetof(gm_t79rbdv4rafe5, f0) == 0 && offsetof(gm_t79rbdv4rafe5, f1) == 8 && offsetof(gm_t79rbdv4rafe5, f2) == 24, "layout of `Block`");
_Static_assert(sizeof(gm_t7g88i52rrtm5) == 24 && _Alignof(gm_t7g88i52rrtm5) == 4 && offsetof(gm_t7g88i52rrtm5, f0) == 0 && offsetof(gm_t7g88i52rrtm5, f1) == 1 && offsetof(gm_t7g88i52rrtm5, f2) == 2 && offsetof(gm_t7g88i52rrtm5, f3) == 3 && offsetof(gm_t7g88i52rrtm5, f4) == 4 && offsetof(gm_t7g88i52rrtm5, f5) == 8 && offsetof(gm_t7g88i52rrtm5, f6) == 12 && offsetof(gm_t7g88i52rrtm5, f7) == 20, "layout of `FmtSpec`");
typedef struct { gm_tjpqvfd72o62u f0; uint8_t f1; uint8_t pe[7]; } gm_t8cc1mtbrhlpt;
_Static_assert(sizeof(gm_t8ghgl2jj0qha) == 16 && _Alignof(gm_t8ghgl2jj0qha) == 8 && offsetof(gm_t8ghgl2jj0qha, f0) == 0 && offsetof(gm_t8ghgl2jj0qha, f1) == 8, "layout of `(op: UnOp, operand: ^Expr)`");
_Static_assert(sizeof(gm_t8jsc4n9vp2ib) == 16 && _Alignof(gm_t8jsc4n9vp2ib) == 8 && offsetof(gm_t8jsc4n9vp2ib, f0) == 0 && offsetof(gm_t8jsc4n9vp2ib, f1) == 8, "layout of `(kind: Coercion, expr: ^Expr)`");
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; gm_tnnjeerolos0a f2; uint8_t pe[4]; } gm_t8nckeqfenl8a;
_Static_assert(sizeof(gm_taari1s0m51a7) == 2 && _Alignof(gm_taari1s0m51a7) == 1 && offsetof(gm_taari1s0m51a7, f0) == 0 && offsetof(gm_taari1s0m51a7, f1) == 1, "layout of `?UnOp`");
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
typedef struct { gm_tesaltf0mhcki f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tandvbt63uk7n;
_Static_assert(sizeof(gm_tb40qn7hk0tl9) == 40 && _Alignof(gm_tb40qn7hk0tl9) == 8 && offsetof(gm_tb40qn7hk0tl9, f0) == 0 && offsetof(gm_tb40qn7hk0tl9, f1) == 16 && offsetof(gm_tb40qn7hk0tl9, f2) == 20 && offsetof(gm_tb40qn7hk0tl9, f3) == 24, "layout of `SourceLocation`");
typedef struct { uint8_t f0; uint8_t p1[7]; gm_trqfvmjtt4sij f1; } gm_tbci5ospk5nnn;
_Static_assert(sizeof(gm_tbngattl1fo51) == 8 && _Alignof(gm_tbngattl1fo51) == 4 && offsetof(gm_tbngattl1fo51, f0) == 0 && offsetof(gm_tbngattl1fo51, f1) == 4, "layout of `(is_const: bool, pointee: Ty)`");
_Static_assert(sizeof(gm_tcmkompbl09il) == 24 && _Alignof(gm_tcmkompbl09il) == 8 && offsetof(gm_tcmkompbl09il, f0) == 0 && offsetof(gm_tcmkompbl09il, f1) == 8, "layout of `(ty: Ty, bytes: string)`");
_Static_assert(sizeof(gm_tcmtrmhj7ak4u) == 12 && _Alignof(gm_tcmtrmhj7ak4u) == 4 && offsetof(gm_tcmtrmhj7ak4u, f0) == 0 && offsetof(gm_tcmtrmhj7ak4u, f1) == 4 && offsetof(gm_tcmtrmhj7ak4u, f2) == 8, "layout of `(op: LenOp, lhs: Ty, rhs: Ty)`");
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t p2[4]; gm_tnpifv20irnph f2; uint8_t f3; uint8_t pe[7]; } gm_tddtjfggdat0d;
_Static_assert(sizeof(gm_te9hrk0u35rev) == 12 && _Alignof(gm_te9hrk0u35rev) == 4 && offsetof(gm_te9hrk0u35rev, f0) == 0 && offsetof(gm_te9hrk0u35rev, f1) == 4, "layout of `Target`");
typedef struct { gm_te9hrk0u35rev f0; uint8_t p1[4]; void * f1; } gm_teo9d4mncjfdu;
_Static_assert(sizeof(gm_tesaltf0mhcki) == 4 && _Alignof(gm_tesaltf0mhcki) == 1 && offsetof(gm_tesaltf0mhcki, f0) == 0 && offsetof(gm_tesaltf0mhcki, f1) == 1, "layout of `SoaOp`");
typedef struct { gm_t08olq97rscn3 f0; gm_th2lqbo77pvsc f1; } gm_tet9guhev5r4u;
_Static_assert(sizeof(gm_tf1bjg81u0glv) == 24 && _Alignof(gm_tf1bjg81u0glv) == 8 && offsetof(gm_tf1bjg81u0glv, f0) == 0 && offsetof(gm_tf1bjg81u0glv, f1) == 8, "layout of `(i: usize, x: string)`");
_Static_assert(sizeof(gm_tfeab279c16rr) == 48 && _Alignof(gm_tfeab279c16rr) == 8 && offsetof(gm_tfeab279c16rr, f0) == 0 && offsetof(gm_tfeab279c16rr, f1) == 16 && offsetof(gm_tfeab279c16rr, f2) == 32, "layout of `Toolchain`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_tgabaikr08mjg) == 40 && _Alignof(gm_tgabaikr08mjg) == 8 && offsetof(gm_tgabaikr08mjg, f0) == 0 && offsetof(gm_tgabaikr08mjg, f1) == 16 && offsetof(gm_tgabaikr08mjg, f2) == 24, "layout of `Block`");
_Static_assert(sizeof(gm_tgglga8ro3isc) == 8 && _Alignof(gm_tgglga8ro3isc) == 4 && offsetof(gm_tgglga8ro3isc, f0) == 0 && offsetof(gm_tgglga8ro3isc, f1) == 4, "layout of `(err: Ty, ok: Ty)`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
typedef struct { gm_t0uuhaljsf410 f0; uint8_t f1; uint8_t pe[7]; } gm_ti6pdj2sunpe4;
_Static_assert(sizeof(gm_tibupt4dmcqai) == 24 && _Alignof(gm_tibupt4dmcqai) == 8 && offsetof(gm_tibupt4dmcqai, f0) == 0 && offsetof(gm_tibupt4dmcqai, f1) == 16, "layout of `StrLit`");
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
typedef struct { void * f0; gm_t08olq97rscn3 f1; } gm_tioie2ju3q4ul;
_Static_assert(sizeof(gm_tj64ui6kruni6) == 32 && _Alignof(gm_tj64ui6kruni6) == 8 && offsetof(gm_tj64ui6kruni6, f0) == 0 && offsetof(gm_tj64ui6kruni6, f1) == 16, "layout of `(string, ?usize)`");
_Static_assert(sizeof(gm_tjpqvfd72o62u) == 32 && _Alignof(gm_tjpqvfd72o62u) == 8 && offsetof(gm_tjpqvfd72o62u, f0) == 0 && offsetof(gm_tjpqvfd72o62u, f1) == 8 && offsetof(gm_tjpqvfd72o62u, f2) == 24, "layout of `SoaInfo`");
_Static_assert(sizeof(gm_tkaapcii86udk) == 24 && _Alignof(gm_tkaapcii86udk) == 8 && offsetof(gm_tkaapcii86udk, f0) == 0 && offsetof(gm_tkaapcii86udk, f1) == 8, "layout of `Place`");
_Static_assert(sizeof(gm_tkn42u7na3t81) == 8 && _Alignof(gm_tkn42u7na3t81) == 4 && offsetof(gm_tkn42u7na3t81, f0) == 0 && offsetof(gm_tkn42u7na3t81, f1) == 4, "layout of `?Ty`");
_Static_assert(sizeof(gm_tlljg61a2ctgq) == 32 && _Alignof(gm_tlljg61a2ctgq) == 8 && offsetof(gm_tlljg61a2ctgq, f0) == 0 && offsetof(gm_tlljg61a2ctgq, f1) == 8 && offsetof(gm_tlljg61a2ctgq, f2) == 24, "layout of `(callee: ^Expr, args: []Arg, trailing_comma: bool)`");
_Static_assert(sizeof(gm_tmnrj1ud20adj) == 24 && _Alignof(gm_tmnrj1ud20adj) == 8 && offsetof(gm_tmnrj1ud20adj, f0) == 0 && offsetof(gm_tmnrj1ud20adj, f1) == 8 && offsetof(gm_tmnrj1ud20adj, f2) == 16, "layout of `Number`");
_Static_assert(sizeof(gm_tnnjeerolos0a) == 8 && _Alignof(gm_tnnjeerolos0a) == 4 && offsetof(gm_tnnjeerolos0a, f0) == 0 && offsetof(gm_tnnjeerolos0a, f1) == 4, "layout of `ArrayKind`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
_Static_assert(sizeof(gm_tpe1hv3fekn05) == 24 && _Alignof(gm_tpe1hv3fekn05) == 8 && offsetof(gm_tpe1hv3fekn05, f0) == 0 && offsetof(gm_tpe1hv3fekn05, f1) == 16, "layout of `(attrs: []Attribute, inner: ^Expr)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t7g88i52rrtm5 v0; gm_b16_8 v1; } f1; } gm_tpu7cfmd3m0cd;
_Static_assert(sizeof(gm_tq3m5qgvkpje8) == 40 && _Alignof(gm_tq3m5qgvkpje8) == 8 && offsetof(gm_tq3m5qgvkpje8, f0) == 0 && offsetof(gm_tq3m5qgvkpje8, f1) == 16 && offsetof(gm_tq3m5qgvkpje8, f2) == 24, "layout of `(string, usize, string)`");
_Static_assert(sizeof(gm_tq7s98ihonkju) == 16 && _Alignof(gm_tq7s98ihonkju) == 8 && offsetof(gm_tq7s98ihonkju, f0) == 0 && offsetof(gm_tq7s98ihonkju, f1) == 8, "layout of `AllocError!^HandlerDecl`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_ttggq7ng4kp3p v0; gm_tpe1hv3fekn05 v1; gm_t3mf58gv9kf3n v2; gm_tpe1hv3fekn05 v3; void * v4; gm_tgtub0v94d5u5 v5; } f1; } gm_trcmmd12cefb1;
_Static_assert(sizeof(gm_trl2khmuphncd) == 32 && _Alignof(gm_trl2khmuphncd) == 8 && offsetof(gm_trl2khmuphncd, f0) == 0 && offsetof(gm_trl2khmuphncd, f1) == 16 && offsetof(gm_trl2khmuphncd, f2) == 24, "layout of `IfExpr`");
typedef struct { gm_ts6oqppjh0ba3 f0; uint8_t p1[6]; void * f1; void * f2; } gm_trl9d7m1k5lbd;
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_ts6oqppjh0ba3) == 2 && _Alignof(gm_ts6oqppjh0ba3) == 1 && offsetof(gm_ts6oqppjh0ba3, f0) == 0 && offsetof(gm_ts6oqppjh0ba3, f1) == 1, "layout of `BinOp`");
_Static_assert(sizeof(gm_ts78ca2amthgu) == 40 && _Alignof(gm_ts78ca2amthgu) == 8 && offsetof(gm_ts78ca2amthgu, f0) == 0 && offsetof(gm_ts78ca2amthgu, f1) == 8 && offsetof(gm_ts78ca2amthgu, f2) == 24, "layout of `(ty: Ty, name: string, span: Span)`");
typedef struct { gm_tvdg94bk7aa22 f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tt8o5kebou62r;
_Static_assert(sizeof(gm_tupk62cr6lbdu) == 24 && _Alignof(gm_tupk62cr6lbdu) == 8 && offsetof(gm_tupk62cr6lbdu, f0) == 0 && offsetof(gm_tupk62cr6lbdu, f1) == 8 && offsetof(gm_tupk62cr6lbdu, f2) == 12, "layout of `(base: ^Expr, index: u32, index_span: Span)`");
_Static_assert(sizeof(gm_tv2uf4leil5hq) == 64 && _Alignof(gm_tv2uf4leil5hq) == 8 && offsetof(gm_tv2uf4leil5hq, f0) == 0 && offsetof(gm_tv2uf4leil5hq, f1) == 8 && offsetof(gm_tv2uf4leil5hq, f2) == 24 && offsetof(gm_tv2uf4leil5hq, f3) == 32 && offsetof(gm_tv2uf4leil5hq, f4) == 40 && offsetof(gm_tv2uf4leil5hq, f5) == 48, "layout of `Binding`");
_Static_assert(sizeof(gm_tvdg94bk7aa22) == 12 && _Alignof(gm_tvdg94bk7aa22) == 4 && offsetof(gm_tvdg94bk7aa22, f0) == 0 && offsetof(gm_tvdg94bk7aa22, f1) == 4, "layout of `Builtin`");
_Static_assert(sizeof(gm_t1jll27j7ae34) == 56 && _Alignof(gm_t1jll27j7ae34) == 8 && offsetof(gm_t1jll27j7ae34, f0) == 0 && offsetof(gm_t1jll27j7ae34, f1) == 8, "layout of `Parsed`");
_Static_assert(sizeof(gm_t1s75i08qtptd) == 40 && _Alignof(gm_t1s75i08qtptd) == 8 && offsetof(gm_t1s75i08qtptd, f0) == 0 && offsetof(gm_t1s75i08qtptd, f1) == 32, "layout of `?Ident`");
_Static_assert(sizeof(gm_t419mo45g7j0a) == 32 && _Alignof(gm_t419mo45g7j0a) == 8 && offsetof(gm_t419mo45g7j0a, f0) == 0 && offsetof(gm_t419mo45g7j0a, f1) == 24, "layout of `(effect_: EffectRef, op: u32)`");
_Static_assert(sizeof(gm_t428ue2u3hf5g) == 32 && _Alignof(gm_t428ue2u3hf5g) == 8 && offsetof(gm_t428ue2u3hf5g, f0) == 0 && offsetof(gm_t428ue2u3hf5g, f1) == 8, "layout of `ConstKey`");
_Static_assert(sizeof(gm_t4gut2c9fj1kt) == 184 && _Alignof(gm_t4gut2c9fj1kt) == 8 && offsetof(gm_t4gut2c9fj1kt, f0) == 0 && offsetof(gm_t4gut2c9fj1kt, f1) == 8 && offsetof(gm_t4gut2c9fj1kt, f2) == 24 && offsetof(gm_t4gut2c9fj1kt, f3) == 40 && offsetof(gm_t4gut2c9fj1kt, f4) == 80 && offsetof(gm_t4gut2c9fj1kt, f5) == 120 && offsetof(gm_t4gut2c9fj1kt, f6) == 160, "layout of `Diagnostic`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tjcj4j2gbh6c8 v0; uint16_t v1; gm_tbngattl1fo51 v2; gm_tbngattl1fo51 v3; gm_tbngattl1fo51 v4; gm_t8nckeqfenl8a v5; gm_t6j6smj7qoctu v6; gm_t88rcucg1b7uk v7; gm_tgglga8ro3isc v8; gm_tddtjfggdat0d v9; gm_tddtjfggdat0d v10; gm_tiilmfd16v3fc v11; gm_t88rcucg1b7uk v12; uint32_t v13; gm_tgglga8ro3isc v14; gm_t428ue2u3hf5g v15; gm_tcmtrmhj7ak4u v16; uint32_t v17; gm_tnpifv20irnph v18; uint32_t v19; } f1; } gm_t53at80pfdus3;
_Static_assert(sizeof(gm_t8cc1mtbrhlpt) == 40 && _Alignof(gm_t8cc1mtbrhlpt) == 8 && offsetof(gm_t8cc1mtbrhlpt, f0) == 0 && offsetof(gm_t8cc1mtbrhlpt, f1) == 32, "layout of `?SoaInfo`");
_Static_assert(sizeof(gm_t8nckeqfenl8a) == 24 && _Alignof(gm_t8nckeqfenl8a) == 8 && offsetof(gm_t8nckeqfenl8a, f0) == 0 && offsetof(gm_t8nckeqfenl8a, f1) == 8 && offsetof(gm_t8nckeqfenl8a, f2) == 12, "layout of `(len: u64, elem: Ty, kind: ArrayKind)`");
_Static_assert(sizeof(gm_tandvbt63uk7n) == 24 && _Alignof(gm_tandvbt63uk7n) == 8 && offsetof(gm_tandvbt63uk7n, f0) == 0 && offsetof(gm_tandvbt63uk7n, f1) == 8, "layout of `(op: SoaOp, args: []Expr)`");
_Static_assert(sizeof(gm_tbci5ospk5nnn) == 48 && _Alignof(gm_tbci5ospk5nnn) == 8 && offsetof(gm_tbci5ospk5nnn, f0) == 0 && offsetof(gm_tbci5ospk5nnn, f1) == 8, "layout of `Int`");
typedef struct { gm_t1s75i08qtptd f0; void * f1; } gm_tchr52g0ema53;
_Static_assert(sizeof(gm_tddtjfggdat0d) == 48 && _Alignof(gm_tddtjfggdat0d) == 8 && offsetof(gm_tddtjfggdat0d, f0) == 0 && offsetof(gm_tddtjfggdat0d, f1) == 16 && offsetof(gm_tddtjfggdat0d, f2) == 24 && offsetof(gm_tddtjfggdat0d, f3) == 40, "layout of `FnTy`");
_Static_assert(sizeof(gm_teo9d4mncjfdu) == 24 && _Alignof(gm_teo9d4mncjfdu) == 8 && offsetof(gm_teo9d4mncjfdu, f0) == 0 && offsetof(gm_teo9d4mncjfdu, f1) == 16, "layout of `BreakExpr`");
_Static_assert(sizeof(gm_tet9guhev5r4u) == 56 && _Alignof(gm_tet9guhev5r4u) == 8 && offsetof(gm_tet9guhev5r4u, f0) == 0 && offsetof(gm_tet9guhev5r4u, f1) == 32, "layout of `IntrinsicExpr`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t419mo45g7j0a v0; void * v1; } f1; } gm_th1g69d7vpipg;
_Static_assert(sizeof(gm_ti6pdj2sunpe4) == 72 && _Alignof(gm_ti6pdj2sunpe4) == 8 && offsetof(gm_ti6pdj2sunpe4, f0) == 0 && offsetof(gm_ti6pdj2sunpe4, f1) == 64, "layout of `?TemplateArgs`");
typedef struct { void * f0; gm_t1s75i08qtptd f1; void * f2; } gm_tioi3jh9v22g5;
_Static_assert(sizeof(gm_tioie2ju3q4ul) == 40 && _Alignof(gm_tioie2ju3q4ul) == 8 && offsetof(gm_tioie2ju3q4ul, f0) == 0 && offsetof(gm_tioie2ju3q4ul, f1) == 8, "layout of `(base: ^Expr, name: Ident)`");
typedef struct { gm_tbci5ospk5nnn f0; gm_tbci5ospk5nnn f1; } gm_tngtuiuskn938;
_Static_assert(sizeof(gm_tpu7cfmd3m0cd) == 32 && _Alignof(gm_tpu7cfmd3m0cd) == 8 && offsetof(gm_tpu7cfmd3m0cd, f0) == 0 && offsetof(gm_tpu7cfmd3m0cd, f1) == 8, "layout of `SpecResult`");
_Static_assert(sizeof(gm_trcmmd12cefb1) == 32 && _Alignof(gm_trcmmd12cefb1) == 8 && offsetof(gm_trcmmd12cefb1, f0) == 0 && offsetof(gm_trcmmd12cefb1, f1) == 8, "layout of `Stmt`");
_Static_assert(sizeof(gm_trl9d7m1k5lbd) == 24 && _Alignof(gm_trl9d7m1k5lbd) == 8 && offsetof(gm_trl9d7m1k5lbd, f0) == 0 && offsetof(gm_trl9d7m1k5lbd, f1) == 8 && offsetof(gm_trl9d7m1k5lbd, f2) == 16, "layout of `(op: BinOp, lhs: ^Expr, rhs: ^Expr)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint32_t v0; gm_t419mo45g7j0a v1; void * v2; } f1; } gm_tsth9qu1dv0g3;
_Static_assert(sizeof(gm_tt8o5kebou62r) == 32 && _Alignof(gm_tt8o5kebou62r) == 8 && offsetof(gm_tt8o5kebou62r, f0) == 0 && offsetof(gm_tt8o5kebou62r, f1) == 16, "layout of `(op: Builtin, args: []Expr)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { void * v0; void * v1; gm_tibupt4dmcqai v2; uint8_t v3; gm_t08olq97rscn3 v4; gm_t08olq97rscn3 v5; void * v6; gm_b16_8 v7; void * v8; gm_t8ghgl2jj0qha v9; gm_trl9d7m1k5lbd v10; gm_tioi3jh9v22g5 v11; void * v12; void * v13; gm_t3mf58gv9kf3n v14; void * v15; gm_tlljg61a2ctgq v16; gm_t3mf58gv9kf3n v17; gm_tao63pk3r18v5 v18; gm_tioie2ju3q4ul v19; gm_tupk62cr6lbdu v20; void * v21; gm_tgtub0v94d5u5 v22; gm_tgtub0v94d5u5 v23; gm_tgtub0v94d5u5 v24; gm_t3mf58gv9kf3n v25; gm_t3mf58gv9kf3n v26; void * v27; void * v28; void * v29; void * v30; void * v31; gm_tpe1hv3fekn05 v32; void * v33; void * v34; gm_tibupt4dmcqai v35; void * v36; void * v37; void * v38; void * v39; void * v40; gm_t79rbdv4rafe5 v41; void * v42; gm_tchr52g0ema53 v43; gm_t1s75i08qtptd v44; gm_t3mf58gv9kf3n v45; gm_tet9guhev5r4u v46; } f1; } gm_t2gtbh39ptekd;
_Static_assert(sizeof(gm_t53at80pfdus3) == 56 && _Alignof(gm_t53at80pfdus3) == 8 && offsetof(gm_t53at80pfdus3, f0) == 0 && offsetof(gm_t53at80pfdus3, f1) == 8, "layout of `TyKind`");
typedef struct { gm_th1g69d7vpipg f0; void * f1; void * f2; uint32_t f3; uint8_t pe[4]; } gm_t9i38ih07bojq;
_Static_assert(sizeof(gm_tchr52g0ema53) == 48 && _Alignof(gm_tchr52g0ema53) == 8 && offsetof(gm_tchr52g0ema53, f0) == 0 && offsetof(gm_tchr52g0ema53, f1) == 40, "layout of `BreakExpr`");
_Static_assert(sizeof(gm_th1g69d7vpipg) == 40 && _Alignof(gm_th1g69d7vpipg) == 8 && offsetof(gm_th1g69d7vpipg, f0) == 0 && offsetof(gm_th1g69d7vpipg, f1) == 8, "layout of `WriteSink`");
_Static_assert(sizeof(gm_tioi3jh9v22g5) == 56 && _Alignof(gm_tioi3jh9v22g5) == 8 && offsetof(gm_tioi3jh9v22g5, f0) == 0 && offsetof(gm_tioi3jh9v22g5, f1) == 8 && offsetof(gm_tioi3jh9v22g5, f2) == 48, "layout of `ElseExpr`");
_Static_assert(sizeof(gm_tngtuiuskn938) == 96 && _Alignof(gm_tngtuiuskn938) == 8 && offsetof(gm_tngtuiuskn938, f0) == 0 && offsetof(gm_tngtuiuskn938, f1) == 48, "layout of `Rat`");
typedef struct { gm_th1g69d7vpipg f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; } gm_tntn2tf15puib;
typedef struct { gm_tsth9qu1dv0g3 f0; gm_b16_8 f1; } gm_to76jli3bqddq;
_Static_assert(sizeof(gm_tsth9qu1dv0g3) == 40 && _Alignof(gm_tsth9qu1dv0g3) == 8 && offsetof(gm_tsth9qu1dv0g3, f0) == 0 && offsetof(gm_tsth9qu1dv0g3, f1) == 8, "layout of `Callee`");
typedef struct { gm_tngtuiuskn938 f0; gm_tngtuiuskn938 f1; } gm_tu15p0hi2qlv6;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tbci5ospk5nnn v0; gm_tngtuiuskn938 v1; uint8_t v2; gm_tu15p0hi2qlv6 v3; } f1; } gm_t0ialelq3ql1k;
_Static_assert(sizeof(gm_t2gtbh39ptekd) == 64 && _Alignof(gm_t2gtbh39ptekd) == 8 && offsetof(gm_t2gtbh39ptekd, f0) == 0 && offsetof(gm_t2gtbh39ptekd, f1) == 8, "layout of `ExprKind`");
_Static_assert(sizeof(gm_t9i38ih07bojq) == 64 && _Alignof(gm_t9i38ih07bojq) == 8 && offsetof(gm_t9i38ih07bojq, f0) == 0 && offsetof(gm_t9i38ih07bojq, f1) == 40 && offsetof(gm_t9i38ih07bojq, f2) == 48 && offsetof(gm_t9i38ih07bojq, f3) == 56, "layout of `WriteDeferredExpr`");
typedef struct { uint32_t f0; uint8_t p1[4]; gm_t2gtbh39ptekd f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_tc3oht4ksth9d;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tbci5ospk5nnn v0; double v1; void * v2; void * v3; uint8_t v4; gm_b16_8 v5; gm_b16_8 v6; uint32_t v7; gm_tkn42u7na3t81 v8; gm_tkn42u7na3t81 v9; gm_t88rcucg1b7uk v10; gm_t3rcjibl8kkg4 v11; gm_t08olq97rscn3 v12; uint32_t v13; gm_tcc1rq4n1avb2 v14; gm_t3877pk7lv748 v15; void * v16; gm_tbl8il92l97ut v17; gm_tlcl4u558l6p5 v18; gm_tao63pk3r18v5 v19; void * v20; void * v21; void * v22; void * v23; uint32_t v24; uint32_t v25; uint32_t v26; gm_t8ghgl2jj0qha v27; gm_trl9d7m1k5lbd v28; gm_ta612fil812pm v29; gm_t8jsc4n9vp2ib v30; gm_tt8o5kebou62r v31; gm_to76jli3bqddq v32; gm_b16_8 v33; gm_b16_8 v34; void * v35; gm_ttggq7ng4kp3p v36; void * v37; gm_tsplfr33dsvji v38; gm_t3dcoc7077t1q v39; gm_t083kd7b1kkag v40; gm_tlcl4u558l6p5 v41; gm_tkaapcii86udk v42; gm_tlcl4u558l6p5 v43; void * v44; gm_trl2khmuphncd v45; gm_t710lsrq1m8gk v46; gm_teo9d4mncjfdu v47; gm_te9hrk0u35rev v48; gm_ttggq7ng4kp3p v49; void * v50; gm_t3mf58gv9kf3n v51; void * v52; void * v53; void * v54; gm_tandvbt63uk7n v55; gm_t710lsrq1m8gk v56; } f1; } gm_teti411eujvf4;
_Static_assert(sizeof(gm_tntn2tf15puib) == 88 && _Alignof(gm_tntn2tf15puib) == 8 && offsetof(gm_tntn2tf15puib, f0) == 0 && offsetof(gm_tntn2tf15puib, f1) == 40 && offsetof(gm_tntn2tf15puib, f2) == 56 && offsetof(gm_tntn2tf15puib, f3) == 72, "layout of `WriteExpr`");
_Static_assert(sizeof(gm_to76jli3bqddq) == 56 && _Alignof(gm_to76jli3bqddq) == 8 && offsetof(gm_to76jli3bqddq, f0) == 0 && offsetof(gm_to76jli3bqddq, f1) == 40, "layout of `(callee: Callee, args: []Expr)`");
_Static_assert(sizeof(gm_tu15p0hi2qlv6) == 192 && _Alignof(gm_tu15p0hi2qlv6) == 8 && offsetof(gm_tu15p0hi2qlv6, f0) == 0 && offsetof(gm_tu15p0hi2qlv6, f1) == 96, "layout of `(re: Rat, im: Rat)`");
_Static_assert(sizeof(gm_t0ialelq3ql1k) == 200 && _Alignof(gm_t0ialelq3ql1k) == 8 && offsetof(gm_t0ialelq3ql1k, f0) == 0 && offsetof(gm_t0ialelq3ql1k, f1) == 8, "layout of `Untyped`");
typedef struct { gm_t08olq97rscn3 f0; gm_tc3oht4ksth9d f1; } gm_t56mj5hnm2uc6;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_teti411eujvf4 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_t69m56meqefs8;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t88rcucg1b7uk v0; uint32_t v1; uint32_t v2; gm_tiilmfd16v3fc v3; gm_tnpifv20irnph v4; uint32_t v5; gm_tcc1rq4n1avb2 v6; gm_t88rcucg1b7uk v7; gm_t0ialelq3ql1k v8; uint64_t v9; gm_t88rcucg1b7uk v10; } f1; } gm_t6qooom8qa2do;
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint32_t v0; gm_t0ialelq3ql1k v1; uint32_t v2; gm_ts78ca2amthgu v3; gm_t88rcucg1b7uk v4; } f1; } gm_t930q5l38hdap;
_Static_assert(sizeof(gm_tc3oht4ksth9d) == 88 && _Alignof(gm_tc3oht4ksth9d) == 8 && offsetof(gm_tc3oht4ksth9d, f0) == 0 && offsetof(gm_tc3oht4ksth9d, f1) == 8 && offsetof(gm_tc3oht4ksth9d, f2) == 72, "layout of `Expr`");
_Static_assert(sizeof(gm_teti411eujvf4) == 64 && _Alignof(gm_teti411eujvf4) == 8 && offsetof(gm_teti411eujvf4, f0) == 0 && offsetof(gm_teti411eujvf4, f1) == 8, "layout of `ExprKind`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tc3oht4ksth9d v0; gm_t56mj5hnm2uc6 v1; gm_tc3oht4ksth9d v2; } f1; } gm_t22mtv3sd9prf;
_Static_assert(sizeof(gm_t56mj5hnm2uc6) == 120 && _Alignof(gm_t56mj5hnm2uc6) == 8 && offsetof(gm_t56mj5hnm2uc6, f0) == 0 && offsetof(gm_t56mj5hnm2uc6, f1) == 32, "layout of `(name: Ident, value: Expr)`");
_Static_assert(sizeof(gm_t69m56meqefs8) == 88 && _Alignof(gm_t69m56meqefs8) == 8 && offsetof(gm_t69m56meqefs8, f0) == 0 && offsetof(gm_t69m56meqefs8, f1) == 8 && offsetof(gm_t69m56meqefs8, f2) == 72, "layout of `Expr`");
_Static_assert(sizeof(gm_t6qooom8qa2do) == 208 && _Alignof(gm_t6qooom8qa2do) == 8 && offsetof(gm_t6qooom8qa2do, f0) == 0 && offsetof(gm_t6qooom8qa2do, f1) == 8, "layout of `DeclValue`");
_Static_assert(sizeof(gm_t930q5l38hdap) == 208 && _Alignof(gm_t930q5l38hdap) == 8 && offsetof(gm_t930q5l38hdap, f0) == 0 && offsetof(gm_t930q5l38hdap, f1) == 8, "layout of `LocalBinding`");
typedef struct { gm_t930q5l38hdap f0; uint8_t f1; uint8_t pe[7]; } gm_tqvu78rpi6q1h;
_Static_assert(sizeof(gm_t22mtv3sd9prf) == 128 && _Alignof(gm_t22mtv3sd9prf) == 8 && offsetof(gm_t22mtv3sd9prf, f0) == 0 && offsetof(gm_t22mtv3sd9prf, f1) == 8, "layout of `ArgKind`");
typedef struct { gm_t22mtv3sd9prf f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_t7te2tdpebbt4;
_Static_assert(sizeof(gm_tqvu78rpi6q1h) == 216 && _Alignof(gm_tqvu78rpi6q1h) == 8 && offsetof(gm_tqvu78rpi6q1h, f0) == 0 && offsetof(gm_tqvu78rpi6q1h, f1) == 208, "layout of `?LocalBinding`");
_Static_assert(sizeof(gm_t7te2tdpebbt4) == 144 && _Alignof(gm_t7te2tdpebbt4) == 8 && offsetof(gm_t7te2tdpebbt4, f0) == 0 && offsetof(gm_t7te2tdpebbt4, f1) == 128, "layout of `Arg`");
struct gm_fr_align_of_mks0ipd {
    gm_frame h;
    gm_taari1s0m51a7 ret;
};
struct gm_fr_block_of_2gjl2i1 {
    gm_frame h;
    void * ret;
};
struct gm_fr_build_write_k6dcf8r {
    gm_frame h;
    gm_th1g69d7vpipg l1; /* sink */
    gm_t0uuhaljsf410 l10; /* ta */
    gm_trqfvmjtt4sij l16; /* stmts */
    void * ret;
};
struct gm_fr_check_spec_4vp7jhg {
    gm_frame h;
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_const_template_5dvtt1m {
    gm_frame h;
    gm_tv2uf4leil5hq l10; /* b */
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_default_spec_9ntjq2s {
    gm_frame h;
    gm_t7g88i52rrtm5 ret;
};
struct gm_fr_emit_dph5v1e {
    gm_frame h;
    gm_t4gut2c9fj1kt l1; /* d */
    gm_b0_1 ret;
};
struct gm_fr_empty_slice_5nn7ph0 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_73v4ttb {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_g3tddrb {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_g3tddrb_2 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ijtp9lo {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_moftl2f {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_rcd2jln {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_vj3ckdj_2 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_error_89glhjr {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_error_9huj44a {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_error_expr_o6qt7e2 {
    gm_frame h;
    void * ret;
};
struct gm_fr_error_ki8i3k6 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_file_o15fqku {
    gm_frame h;
    void * ret;
};
struct gm_fr_has_param_u9t0efs {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_int_ci6airg {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_is_error_8k27gd1 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_integer_8d4f9ek {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_kind_jcnt4hm {
    gm_frame h;
    gm_t53at80pfdus3 ret;
};
struct gm_fr_list_5dhgg4i {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_epvducg {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_ir0h3tc_2 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_nof7vfj {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_qtk22f3 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_uvfu1k1 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_vp1djv1 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_vp1djv1_2 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_mk_9bc97fd {
    gm_frame h;
    gm_teti411eujvf4 l1; /* kind */
    void * ret;
};
struct gm_fr_number_at_esf7qkk {
    gm_frame h;
    gm_tmnrj1ud20adj ret;
};
struct gm_fr_numbered_error_07hll20 {
    gm_frame h;
    gm_tfeab279c16rr l0; /* t */
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_parse_spec_iod83cp {
    gm_frame h;
    gm_tpu7cfmd3m0cd ret;
};
struct gm_fr_parse_template_rlpl2k8 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* pieces */
    gm_trqfvmjtt4sij l2; /* specs */
    gm_trqfvmjtt4sij l3; /* cur */
    gm_trqfvmjtt4sij l4; /* indices */
    gm_t1jll27j7ae34 ret;
};
struct gm_fr_placeholders_text_rtvia6b {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_printer_h7jigd7 {
    gm_frame h;
    gm_tmm67r2c94d5j ret;
};
struct gm_fr_report_eardf07 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_resolve_write_e97fjom {
    gm_frame h;
    gm_tfeab279c16rr l7; /* t */
    gm_tfeab279c16rr l9; /* parsed */
    gm_trqfvmjtt4sij l25; /* stmts */
    gm_trqfvmjtt4sij l26; /* fields */
    gm_b0_1 ret;
};
struct gm_fr_show_8uijkbs {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_show_fsnl6fa {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_show_kmjeh8s {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* out */
    gm_b16_8 ret;
};
struct gm_fr_slice_5dt9121 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_source_offset_of_vlcjdq6 {
    gm_frame h;
    gm_tf5srh4mp4b3i ret;
};
struct gm_fr_string_ty_j74ff0o {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_temp_ilbvj8q {
    gm_frame h;
    uint32_t ret;
};
struct gm_fr_template_args_vf1gdn1 {
    gm_frame h;
    gm_tfeab279c16rr l6; /* t */
    gm_t4gut2c9fj1kt l8; /* d */
    gm_tfeab279c16rr l13; /* parsed */
    gm_trqfvmjtt4sij l16; /* stmts */
    gm_trqfvmjtt4sij l17; /* values */
    gm_tc3oht4ksth9d l18; /* a */
    gm_trqfvmjtt4sij l31; /* vals */
    gm_t7te2tdpebbt4 l34; /* a */
    gm_tc3oht4ksth9d l35; /* e */
    gm_trqfvmjtt4sij l37; /* reads */
    gm_ti6pdj2sunpe4 ret;
};
struct gm_fr_u8_ty_afkvj25 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_unit_2age34u {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_write_deferred_kbicf34 {
    gm_frame h;
    gm_th1g69d7vpipg l1; /* sink */
    gm_tc3oht4ksth9d l7; /* e */
    gm_trqfvmjtt4sij l10; /* elems */
    gm_t7te2tdpebbt4 l13; /* a */
    gm_tc3oht4ksth9d l14; /* e */
    gm_trqfvmjtt4sij l15; /* ts */
    gm_t69m56meqefs8 l18; /* x */
    void * ret;
};
inline bool gm_eq467d5cm1d5(const uint8_t *a, const uint8_t *b);
inline bool gm_eq5307j5k9ms(const uint8_t *a, const uint8_t *b);
inline bool gm_eq929lennfd3(const uint8_t *a, const uint8_t *b);
inline bool gm_eq9m6mvtmgde(const uint8_t *a, const uint8_t *b);
inline bool gm_eq9oqmmiu8qc(const uint8_t *a, const uint8_t *b);
inline bool gm_eqb44cb3cf1n(const uint8_t *a, const uint8_t *b);
inline bool gm_eqjmccqdb90f(const uint8_t *a, const uint8_t *b);
inline bool gm_eqo3v9599nus(const uint8_t *a, const uint8_t *b);
inline bool gm_equ1rt9sffme(const uint8_t *a, const uint8_t *b);
__attribute__((aligned(4))) gm_b0_1 gm_f_add_fix_0gkcr4c(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_tl874t2atnlh3 a2, gm_b16_8 a3);
__attribute__((aligned(4))) gm_taari1s0m51a7 gm_f_align_of_mks0ipd(uint8_t a0);
__attribute__((aligned(4))) void * gm_f_bind_temp_sa5rcbr(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2);
inline __attribute__((aligned(4))) void * gm_f_block_of_2gjl2i1(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) void * gm_f_block_typed_e7svg7n(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0, gm_b16_8 a1, void * a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) void * gm_f_build_write_k6dcf8r(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, void * a2, gm_b16_8 a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) void gm_f_check_spec_4vp7jhg(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) void gm_f_const_string_jdjj62s(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
__attribute__((aligned(4))) void gm_f_const_template_5dvtt1m(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint64_t a2, uint32_t a3);
inline __attribute__((aligned(4))) void gm_f_default_spec_9ntjq2s(void *gm_out);
__attribute__((aligned(4))) void * gm_f_elements_tuple_gdm5n6q(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_tl874t2atnlh3 a2);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_emit_dph5v1e(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_5nn7ph0(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_73v4ttb(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_g3tddrb(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_g3tddrb_2(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_moftl2f(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_rcd2jln(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_vj3ckdj_2(void);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_error_89glhjr(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_error_9huj44a(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void * gm_f_error_expr_o6qt7e2(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_error_ki8i3k6(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_error_s6hn871(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void * gm_f_expr_vmp704u(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_tkn42u7na3t81 a2);
inline __attribute__((aligned(4))) void * gm_f_file_o15fqku(void * a0, uint32_t a1);
__attribute__((aligned(4))) void gm_f_format_0t8rkc3(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_2h4f9a5(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_2rf8n5r(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_4duro1p(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
__attribute__((aligned(4))) void gm_f_format_68lf2sm(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_9r6mq5d(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
__attribute__((aligned(4))) void gm_f_format_cnhv2id(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_f2fgi8v(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_h02tha7(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
__attribute__((aligned(4))) void gm_f_format_jvf1ma2(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_k6j7893(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_ksi8bsm(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_les73rv(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_mlqiori(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_oq31u8d(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_td6patj(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
inline __attribute__((aligned(4))) uint8_t gm_f_has_param_u9t0efs(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_int_ci6airg(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, uint16_t a2);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_intern_ia19mhl(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) uint8_t gm_f_is_default_spec_cdd8jng(void * a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_error_8k27gd1(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_integer_8d4f9ek(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void gm_f_kind_jcnt4hm(void *gm_out, void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void gm_f_list_5dhgg4i(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_epvducg(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_ir0h3tc_2(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_nof7vfj(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_qtk22f3(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_vp1djv1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_vp1djv1_2(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_lookup_local_6f38t7p(void *gm_out, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_lookup_module_name_cukfp7t(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void * gm_f_mk_9bc97fd(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0, const void * a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_2pnaa87(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_dsbahke(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_ges19gd(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) uint32_t gm_f_new_local_003h9es(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_t88rcucg1b7uk a2, uint8_t a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_ve8bb01_2(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_number_at_esf7qkk(void *gm_out, gm_b16_8 a0, uint64_t a1);
__attribute__((aligned(4))) gm_tmnrj1ud20adj gm_f_number_at_esf7qkk_c(gm_b16_8 a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_numbered_error_07hll20(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_parse_spec_iod83cp(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_parse_template_rlpl2k8(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_placeholders_text_rtvia6b(void *env, gm_node *cx, gm_node *inst, uint64_t a0);
inline __attribute__((aligned(4))) void gm_f_printer_h7jigd7(void *gm_out, void * a0, void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_3ef746b(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_d6c76ud_2(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_d943vgp(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_d943vgp_2(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_k3l9nic(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_nbmosph(void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_push_o4ckdr1(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_t83rg2m(void * a0, uint8_t a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_ucgmlo2(void * a0, uint64_t a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_report_eardf07(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_resolve_decl_pus042v(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_resolve_t2kal61(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_resolve_write_e97fjom(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_settle_h9ikvs5(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_tl874t2atnlh3 a2);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_8uijkbs(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_fsnl6fa(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_kmjeh8s(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_slice_5dt9121(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) void * gm_f_soa_columns_tuple_ns477md(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_soa_of_jufdcj2(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_source_offset_of_vlcjdq6(gm_b16_8 a0, uint64_t a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_string_ty_j74ff0o(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) uint32_t gm_f_temp_ilbvj8q(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) void gm_f_template_args_vf1gdn1(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_tl874t2atnlh3 a2, gm_b16_8 a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) gm_b0_1 gm_f_ty_vka2r8h(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_u8_ty_afkvj25(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_t9qkh0rsooc8d gm_f_unify_t2qjv2p(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, gm_t88rcucg1b7uk a3);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_unit_2age34u(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_taari1s0m51a7 gm_f_var_kind_1sl5sig(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) void * gm_f_write_deferred_kbicf34(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, void * a2, gm_b16_8 a3, gm_tl874t2atnlh3 a4);
extern uint8_t gm_k08q1k9tq78[40] __attribute__((aligned(8)));
extern uint8_t gm_k0ed7r38qr4[40] __attribute__((aligned(8)));
extern uint8_t gm_k1g2gld0doi[40] __attribute__((aligned(8)));
extern uint8_t gm_k2uh8q3i8eo[40] __attribute__((aligned(8)));
extern uint8_t gm_k3tttsr3hp1[40] __attribute__((aligned(8)));
extern uint8_t gm_k76fiiae2n8[40] __attribute__((aligned(8)));
extern uint8_t gm_krg6ee1n6ld[40] __attribute__((aligned(8)));
extern const uint8_t gm_s23qsffuqlc1v[25];
extern const uint8_t gm_s378kfmdl5enf[17];
extern const uint8_t gm_s3on77ql0ndko[17];
extern const uint8_t gm_s4087kcl9bpus[29];
extern const uint8_t gm_s4k55k154s62m[120];
extern const uint8_t gm_s4kt3mhlp0or9[11];
extern const uint8_t gm_s6of1cedjphs7[3];
extern const uint8_t gm_s721l5hj93rdl[50];
extern const uint8_t gm_s7an5gtrdgen2[14];
extern const uint8_t gm_s8f0u3tjempi4[53];
extern const uint8_t gm_s93i7ga7kbfpu[34];
extern const uint8_t gm_sc4vvo9i5cgel[86];
extern const uint8_t gm_scne9acm49p4h[11];
extern const uint8_t gm_sfu0in59pituf[62];
extern const uint8_t gm_sggmh06s4b0cs[23];
extern const uint8_t gm_shv1k5m05egbe[6];
extern const uint8_t gm_sjil3d717dq6h[63];
extern const uint8_t gm_sl9bj93djc5da[38];
extern const uint8_t gm_sm35km518bh68[52];
extern const uint8_t gm_smom6v4b4s28c[52];
extern const uint8_t gm_smvgcg1j21t6v[27];
extern const uint8_t gm_soreld657hea2[25];
extern const uint8_t gm_sp920qd66qq8t[20];
extern const uint8_t gm_sppcm1qavsp1k[80];
extern const uint8_t gm_sqqi4njr3i0t4[46];
extern const uint8_t gm_sr4qbrk6tlgaa[1];
extern const char gm_src0ih77f43mhf2[];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_src6d57grm686e3[];
extern const char gm_srcod98slk3vnra[];
extern const char gm_srcrgc9bkquf6nr[];
extern const char gm_srcv1nri2247d2r[];
extern const uint8_t gm_srundlmqvm06p[60];
extern const uint8_t gm_sutmkmalrac6l[45];
extern const uint8_t gm_sves946fckobv[104];
extern const gm_type gm_tyo86ks9qkvt;
inline bool gm_eq467d5cm1d5(const uint8_t *a, const uint8_t *b) {
    return GM_LD(uint32_t, a) == GM_LD(uint32_t, b);
}
inline bool gm_eq5307j5k9ms(const uint8_t *a, const uint8_t *b) {
    if (a[1] != b[1]) return false;
    return !a[1] || gm_equ1rt9sffme(a, b);
}
inline bool gm_eq929lennfd3(const uint8_t *a, const uint8_t *b) {
    return (a[0] != 0) == (b[0] != 0);
}
inline bool gm_eq9m6mvtmgde(const uint8_t *a, const uint8_t *b) {
    if (!gm_eq467d5cm1d5(a + 0, b + 0)) return false;
    return true;
}
inline bool gm_eq9oqmmiu8qc(const uint8_t *a, const uint8_t *b) {
    return GM_LD(uint16_t, a) == GM_LD(uint16_t, b);
}
inline bool gm_eqb44cb3cf1n(const uint8_t *a, const uint8_t *b) {
    if (a[1] != b[1]) return false;
    return !a[1] || gm_eqjmccqdb90f(a, b);
}
inline bool gm_eqjmccqdb90f(const uint8_t *a, const uint8_t *b) {
    return GM_LD(uint8_t, a) == GM_LD(uint8_t, b);
}
inline bool gm_eqo3v9599nus(const uint8_t *a, const uint8_t *b) {
    if (!gm_eq929lennfd3(a + 0, b + 0)) return false;
    if (!gm_eq9oqmmiu8qc(a + 2, b + 2)) return false;
    return true;
}
inline bool gm_equ1rt9sffme(const uint8_t *a, const uint8_t *b) {
    uint8_t ta = GM_LD(uint8_t, a + 0), tb = GM_LD(uint8_t, b + 0);
    if (ta != tb) return false;
    return true;
}
inline __attribute__((aligned(4))) void * gm_f_block_of_2gjl2i1(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_tl874t2atnlh3 a2) {
    struct gm_fr_block_of_2gjl2i1 F;
    void * gm_l0; /* s */
    gm_b16_8 gm_l1; /* stmts */
    gm_tl874t2atnlh3 gm_l2; /* span */
    GM_STACK_CHECK(&F, 109u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        void * t3 = (void *)(((uint8_t *)t2) + 8);
        gm_t88rcucg1b7uk t4 = gm_f_unit_2age34u((void *)NULL, cx, NULL, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b16_8 t5 = gm_l1;
        gm_tl874t2atnlh3 t6 = gm_l2;
        void * t7 = gm_f_block_typed_e7svg7n((void *)NULL, cx, NULL, t4, t5, ((void *)0), t6);
        if (gm_unw.kind) {
            goto gm_lad;
        }
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
inline __attribute__((aligned(4))) void gm_f_default_spec_9ntjq2s(void *gm_out) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_default_spec_9ntjq2s F;
    GM_STACK_CHECK_SHALLOW(&F, 10u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memset((uint8_t *)&(*(gm_t7g88i52rrtm5 *)gm_out) + 5, 0, 3);
    memset((uint8_t *)&(*(gm_t7g88i52rrtm5 *)gm_out) + 21, 0, 3);
    gm_t9qkh0rsooc8d t1;
    GM_ST(uint8_t, ((uint8_t *)&t1 + 0), ((uint8_t)0ULL));
    gm_t9805qtonbsag t2;
    memset(&t2, 0, sizeof t2);
    gm_t9qkh0rsooc8d t3;
    GM_ST(uint8_t, ((uint8_t *)&t3 + 0), ((uint8_t)0ULL));
    GM_ST(uint8_t, (uint8_t *)&(*(gm_t7g88i52rrtm5 *)gm_out) + 0, ((uint8_t)32ULL));
    GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_t7g88i52rrtm5 *)gm_out) + 1, t1);
    GM_ST(uint8_t, (uint8_t *)&(*(gm_t7g88i52rrtm5 *)gm_out) + 2, ((uint8_t)0));
    GM_ST(uint8_t, (uint8_t *)&(*(gm_t7g88i52rrtm5 *)gm_out) + 3, ((uint8_t)0));
    GM_ST(uint8_t, (uint8_t *)&(*(gm_t7g88i52rrtm5 *)gm_out) + 4, ((uint8_t)0));
    GM_ST(uint32_t, (uint8_t *)&(*(gm_t7g88i52rrtm5 *)gm_out) + 8, ((uint32_t)0ULL));
    GM_ST(gm_t9805qtonbsag, (uint8_t *)&(*(gm_t7g88i52rrtm5 *)gm_out) + 12, t2);
    GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_t7g88i52rrtm5 *)gm_out) + 20, t3);
    goto gm_fdone;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_5nn7ph0(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_5nn7ph0 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_73v4ttb(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_73v4ttb F;
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
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 8)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_g3tddrb(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_g3tddrb F;
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
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 88)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_g3tddrb_2(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_g3tddrb_2 F;
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
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 88)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_moftl2f(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_moftl2f F;
    void * gm_l0; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 105u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        void * t2 = GM_INT_TO_PTR(((uint64_t)4ULL));
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
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 24)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_rcd2jln(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_rcd2jln F;
    void * gm_l0; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 105u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        void * t2 = GM_INT_TO_PTR(((uint64_t)4ULL));
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
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 4)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_vj3ckdj_2(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_vj3ckdj_2 F;
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
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 32)));
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_error_9huj44a(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2) {
    struct gm_fr_error_9huj44a F;
    void * gm_l0; /* cx */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_b16_8 gm_l2; /* msg */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 0));
        gm_tl874t2atnlh3 t4 = gm_l1;
        gm_b16_8 t5 = gm_l2;
        gm_b0_1 t6 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t3, t4, t5);
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
inline __attribute__((aligned(4))) void * gm_f_error_expr_o6qt7e2(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1) {
    struct gm_fr_error_expr_o6qt7e2 F;
    void * gm_l0; /* s */
    gm_tl874t2atnlh3 gm_l1; /* span */
    GM_STACK_CHECK(&F, 156u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    void * t2 = (void *)(((uint8_t *)t1) + 8);
    gm_t88rcucg1b7uk t3 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t2);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_b16_8 t4 = gm_f_empty_slice_g3tddrb_2();
    gm_teti411eujvf4 t5;
    memset(&t5, 0, sizeof t5);
    GM_ST(uint8_t, ((uint8_t *)&t5 + 0), ((uint8_t)35ULL));
    GM_ST(gm_b16_8, ((uint8_t *)&t5 + 8), t4);
    gm_tl874t2atnlh3 t6 = gm_l1;
    void * t7 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t3, &t5, t6);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.ret = t7;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_error_ki8i3k6(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_error_ki8i3k6 F;
    void * gm_l0; /* t */
    GM_STACK_CHECK(&F, 84u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    gm_t53at80pfdus3 t2;
    memset(&t2, 0, sizeof t2);
    GM_ST(uint8_t, ((uint8_t *)&t2 + 0), ((uint8_t)26ULL));
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_error_8k27gd1(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_error_8k27gd1 F;
    void * gm_l0; /* t */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    GM_STACK_CHECK_SHALLOW(&F, 83u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    void * t1 = gm_l0;
    gm_t88rcucg1b7uk t2 = gm_l1;
    gm_t53at80pfdus3 t3;
    gm_f_kind_jcnt4hm((void *)&t3, t1, GM_CONV(gm_csguankko4lmhd, t2));
    uint8_t t4 = ((GM_LD(uint8_t, ((uint8_t *)&t3) + 0) == ((uint8_t)26ULL)));
    F.ret = t4;
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
    GM_STACK_CHECK_SHALLOW(&F, 86u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    void * t1 = gm_l0;
    gm_t88rcucg1b7uk t2 = gm_l1;
    gm_t53at80pfdus3 t3;
    gm_f_kind_jcnt4hm((void *)&t3, t1, GM_CONV(gm_csguankko4lmhd, t2));
    uint8_t t4;
    uint8_t t9 = GM_LD(uint8_t, (((uint8_t *)&t3) + 0));
    if (t9 != ((uint8_t)0ULL)) goto pa8;
    goto po7;
pa8:;
    uint8_t t11 = GM_LD(uint8_t, (((uint8_t *)&t3) + 0));
    if (t11 != ((uint8_t)1ULL)) goto pa10;
    goto po7;
pa10:;
    uint8_t t13 = GM_LD(uint8_t, (((uint8_t *)&t3) + 0));
    if (t13 != ((uint8_t)2ULL)) goto pa12;
    goto po7;
pa12:;
    goto mn6;
po7:;
    t4 = ((uint8_t)1);
    goto me5;
mn6:;
    t4 = ((uint8_t)0);
    goto me5;
mn14:;
    gm_trap("no match arm matched the scrutinee", gm_src0ih77f43mhf2, 374, 58);
me5:;
    F.ret = t4;
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
inline __attribute__((aligned(4))) void gm_f_list_ir0h3tc_2(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_ir0h3tc_2 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_vj3ckdj_2();
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
inline __attribute__((aligned(4))) void gm_f_list_qtk22f3(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_qtk22f3 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_moftl2f();
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
inline __attribute__((aligned(4))) void gm_f_list_vp1djv1(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_vp1djv1 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_g3tddrb();
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
inline __attribute__((aligned(4))) void gm_f_list_vp1djv1_2(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_vp1djv1_2 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_g3tddrb_2();
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
inline __attribute__((aligned(4))) void * gm_f_mk_9bc97fd(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0, const void * a1, gm_tl874t2atnlh3 a2) {
    struct gm_fr_mk_9bc97fd F;
    gm_t88rcucg1b7uk gm_l0; /* ty */
    gm_tl874t2atnlh3 gm_l2; /* span */
    GM_STACK_CHECK(&F, 160u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&F.l1, a1, sizeof F.l1);
    gm_l2 = a2;
    gm_t69m56meqefs8 t1;
    memset((uint8_t *)&t1 + 4, 0, 4);
    memset((uint8_t *)&t1 + 84, 0, 4);
    gm_t88rcucg1b7uk t2 = gm_l0;
    GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t1 + 0, t2);
    memcpy((uint8_t *)&t1 + 8, ((uint8_t *)&F.l1), 64);
    gm_tl874t2atnlh3 t3 = gm_l2;
    GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t1 + 72, t3);
    gm_tq7s98ihonkju t4 = gm_f_new_ve8bb01_2((void *)NULL, cx, NULL, &t1, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_k3tttsr3hp1)));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    if (((((uint8_t *)&t4))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t4) + 8, gm_srcod98slk3vnra, 558, 81);
    void * t5 = GM_LD(void *, (((uint8_t *)&t4) + 8));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_placeholders_text_rtvia6b(void *env, gm_node *cx, gm_node *inst, uint64_t a0) {
    struct gm_fr_placeholders_text_rtvia6b F;
    uint64_t gm_l0; /* n */
    GM_STACK_CHECK(&F, 185u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t1;
    uint64_t t3 = gm_l0;
    uint8_t t4 = ((t3 == ((uint64_t)1ULL)));
    if (!t4) goto ie2;
    gm_b16_8 t6;
    GM_ST(void *, (uint8_t *)&t6, (void *)(gm_s7an5gtrdgen2));
    GM_ST(uintptr_t, (uint8_t *)&t6 + 8, (uintptr_t)(13));
    gm_b16_8 t7 = t6;
    t1 = t7;
    goto ix2;
ie2:;
    uint64_t t9 = gm_l0;
    gm_t4ce0jd3tdro4 t10;
    GM_ST(uint64_t, ((uint8_t *)&t10 + 0), t9);
    gm_tabb710d9mq2e t11;
    gm_f_format_9r6mq5d((void *)&t11, (void *)NULL, cx, NULL, t10);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    if (((((uint8_t *)&t11))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t11) + 8, gm_src6d57grm686e3, 348, 97);
    gm_b16_8 t12 = GM_LD(gm_b16_8, (((uint8_t *)&t11) + 8));
    gm_b16_8 t13 = t12;
    gm_b16_8 t14 = GM_LD(gm_b16_8, ((uint8_t *)&t13));
    t1 = t14;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_fsnl6fa(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_show_fsnl6fa F;
    void * gm_l0; /* cx */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    void * t2 = GM_LD(void *, (((uint8_t *)t1) + 0));
    gm_t88rcucg1b7uk t3 = gm_l1;
    gm_b16_8 t4 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t2, t3);
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
inline __attribute__((aligned(4))) uint32_t gm_f_temp_ilbvj8q(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_tl874t2atnlh3 a2) {
    struct gm_fr_temp_ilbvj8q F;
    void * gm_l0; /* cx */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    gm_tl874t2atnlh3 gm_l2; /* span */
    GM_STACK_CHECK(&F, 76u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t1 = gm_l0;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_shv1k5m05egbe));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(5));
    gm_t88rcucg1b7uk t3 = gm_l1;
    gm_tl874t2atnlh3 t4 = gm_l2;
    uint32_t t5 = gm_f_new_local_003h9es((void *)NULL, cx, NULL, t1, t2, t3, ((uint8_t)1), t4);
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_unit_2age34u(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_unit_2age34u F;
    void * gm_l0; /* t */
    GM_STACK_CHECK(&F, 180u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    gm_t6j6smj7qoctu t2;
    gm_b16_8 t3 = gm_f_empty_slice_rcd2jln();
    gm_b16_8 t4 = GM_LD(gm_b16_8, ((uint8_t *)&t3));
    GM_ST(gm_b16_8, (uint8_t *)&t2 + 0, t4);
    gm_b16_8 t5 = gm_f_empty_slice_5nn7ph0();
    gm_b16_8 t6 = GM_LD(gm_b16_8, ((uint8_t *)&t5));
    GM_ST(gm_b16_8, (uint8_t *)&t2 + 16, t6);
    gm_t53at80pfdus3 t7;
    GM_ST(uint8_t, (uint8_t *)&t7 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t7 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t7 + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&t7 + 40, 0);
    GM_ST(uint64_t, (uint8_t *)&t7 + 48, 0);
    GM_ST(uint8_t, ((uint8_t *)&t7 + 0), ((uint8_t)12ULL));
    GM_ST(gm_t6j6smj7qoctu, ((uint8_t *)&t7 + 8), t2);
    gm_t88rcucg1b7uk t8 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t1, &t7);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.ret = t8;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_taari1s0m51a7 gm_f_align_of_mks0ipd(uint8_t a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_align_of_mks0ipd F;
    uint8_t gm_l0; /* c */
    GM_STACK_CHECK_SHALLOW(&F, 22u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t t1 = gm_l0;
    gm_taari1s0m51a7 t2;
    uint8_t t5 = GM_LD(uint8_t, ((uint8_t *)&t1));
    if (!(t5 == ((uint8_t)60ULL))) goto mn4;
    gm_t9qkh0rsooc8d t6;
    GM_ST(uint8_t, ((uint8_t *)&t6 + 0), ((uint8_t)1ULL));
    gm_taari1s0m51a7 t7;
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t7), t6);
    ((uint8_t *)&t7)[1] = 1;
    t2 = t7;
    goto me3;
mn4:;
    uint8_t t9 = GM_LD(uint8_t, ((uint8_t *)&t1));
    if (!(t9 == ((uint8_t)62ULL))) goto mn8;
    gm_t9qkh0rsooc8d t10;
    GM_ST(uint8_t, ((uint8_t *)&t10 + 0), ((uint8_t)2ULL));
    gm_taari1s0m51a7 t11;
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t11), t10);
    ((uint8_t *)&t11)[1] = 1;
    t2 = t11;
    goto me3;
mn8:;
    uint8_t t13 = GM_LD(uint8_t, ((uint8_t *)&t1));
    if (!(t13 == ((uint8_t)94ULL))) goto mn12;
    gm_t9qkh0rsooc8d t14;
    GM_ST(uint8_t, ((uint8_t *)&t14 + 0), ((uint8_t)3ULL));
    gm_taari1s0m51a7 t15;
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t15), t14);
    ((uint8_t *)&t15)[1] = 1;
    t2 = t15;
    goto me3;
mn12:;
    gm_taari1s0m51a7 t17;
    memset(&t17, 0, sizeof t17);
    t2 = t17;
    goto me3;
mn16:;
    gm_trap("no match arm matched the scrutinee", gm_src6d57grm686e3, 113, 43);
me3:;
    F.ret = t2;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) void * gm_f_build_write_k6dcf8r(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, void * a2, gm_b16_8 a3, gm_tl874t2atnlh3 a4) {
    struct gm_fr_build_write_k6dcf8r F;
    void * gm_l0; /* cx */
    void * gm_l2; /* template */
    gm_b16_8 gm_l3; /* rest */
    gm_tl874t2atnlh3 gm_l4; /* span */
    void * gm_l5; /* t */
    gm_b16_8 gm_l6; /* bytes */
    gm_tibupt4dmcqai gm_l7; /* lit */
    gm_b16_8 gm_l8; /* b */
    gm_b16_8 gm_l9; /* b */
    uint64_t gm_l11; /* <tmp> */
    uint64_t gm_l12; /* <tmp> */
    uint64_t gm_l13; /* k */
    void * gm_l14; /* value */
    void * gm_l15; /* write */
    gm_b16_8 gm_l17; /* <tmp> */
    uint64_t gm_l18; /* <tmp> */
    gm_trcmmd12cefb1 gm_l19; /* st */
    GM_STACK_CHECK(&F, 1846u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&F.l1, a1, sizeof F.l1);
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    {
        void * t3 = gm_l2;
        uint8_t t4;
        uint8_t t7 = GM_LD(uint8_t, ((((uint8_t *)t3) + 8) + 0));
        if (t7 != ((uint8_t)6ULL)) goto mn6;
        t4 = ((uint8_t)1);
        goto me5;
mn6:;
        t4 = ((uint8_t)0);
        goto me5;
mn8:;
        gm_trap("no match arm matched the scrutinee", gm_src6d57grm686e3, 285, 8);
me5:;
        if (!t4) goto ie2;
        {
            void * t10 = gm_l0;
            void * t11 = gm_l2;
            void * t12 = gm_l0;
            void * t13 = GM_LD(void *, (((uint8_t *)t12) + 0));
            void * t14 = (void *)(((uint8_t *)t13) + 8);
            gm_t88rcucg1b7uk t15 = gm_f_string_ty_j74ff0o((void *)NULL, cx, NULL, t14);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tkn42u7na3t81 t16;
            GM_ST(uint8_t, (uint8_t *)&t16 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t16 + 6, 0);
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t16), t15);
            ((uint8_t *)&t16)[4] = 1;
            void * t17 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t10, t11, t16);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l5 = t17;
        }
        {
            void * t19 = gm_l5;
            uint8_t t20;
            uint8_t t23 = GM_LD(uint8_t, ((((uint8_t *)t19) + 8) + 0));
            if (t23 != ((uint8_t)9ULL)) goto mn22;
            t20 = ((uint8_t)1);
            goto me21;
mn22:;
            t20 = ((uint8_t)0);
            goto me21;
mn24:;
            gm_trap("no match arm matched the scrutinee", gm_src6d57grm686e3, 287, 12);
me21:;
            if (!t20) goto ie18;
            {
                void * t26 = gm_l0;
                void * t27 = gm_l5;
                gm_b16_8 t28 = gm_l3;
                gm_tl874t2atnlh3 t29 = gm_l4;
                void * t30 = gm_f_write_deferred_kbicf34((void *)NULL, cx, NULL, t26, &(*(gm_th1g69d7vpipg *)((uint8_t *)&F.l1)), t27, t28, t29);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t31 = 0;
                F.ret = t30;
                goto gm_fret;
            }
            goto ix18;
ie18:;
ix18:;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        gm_b16_8 t32 = gm_f_empty_slice_ijtp9lo();
        gm_b16_8 t33 = GM_LD(gm_b16_8, ((uint8_t *)&t32));
        gm_l6 = t33;
    }
    {
        void * t35 = gm_l2;
        uint8_t t36 = GM_LD(uint8_t, ((((uint8_t *)t35) + 8) + 0));
        if (t36 != ((uint8_t)2ULL)) goto ie34;
        gm_tibupt4dmcqai t37 = GM_LD(gm_tibupt4dmcqai, ((((uint8_t *)t35) + 8) + 8));
        gm_l7 = t37;
        {
            gm_b16_8 t39 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l7) + 0));
            gm_b16_8 t40 = GM_LD(gm_b16_8, ((uint8_t *)&t39));
            gm_l6 = t40;
        }
        goto ix34;
ie34:;
        void * t42 = gm_l0;
        void * t43 = gm_l2;
        void * t44 = gm_l0;
        uint64_t t45 = GM_LD(uint64_t, (((uint8_t *)t44) + 8));
        gm_th2lqbo77pvsc t46;
        gm_f_const_template_5dvtt1m((void *)&t46, (void *)NULL, cx, NULL, t42, t43, t45, ((uint32_t)0ULL));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (!((((uint8_t *)&t46))[16] != 0)) goto ie41;
        gm_b16_8 t47 = GM_LD(gm_b16_8, ((uint8_t *)&t46));
        gm_l8 = t47;
        {
            gm_b16_8 t49 = gm_l8;
            gm_l6 = t49;
        }
        goto ix41;
ie41:;
        void * t51 = gm_l0;
        void * t52 = GM_LD(void *, (((uint8_t *)t51) + 0));
        void * t53 = gm_l0;
        uint64_t t54 = GM_LD(uint64_t, (((uint8_t *)t53) + 8));
        void * t55 = gm_l2;
        gm_th2lqbo77pvsc t56;
        gm_f_const_string_jdjj62s((void *)&t56, (void *)NULL, cx, NULL, t52, t54, t55);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (!((((uint8_t *)&t56))[16] != 0)) goto ie50;
        gm_b16_8 t57 = GM_LD(gm_b16_8, ((uint8_t *)&t56));
        gm_l9 = t57;
        {
            gm_b16_8 t59 = gm_l9;
            gm_b16_8 t60 = GM_LD(gm_b16_8, ((uint8_t *)&t59));
            gm_l6 = t60;
        }
        goto ix50;
ie50:;
        {
            void * t62 = gm_l0;
            void * t63 = gm_l2;
            gm_tl874t2atnlh3 t64 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t63) + 72));
            gm_b16_8 t65;
            GM_ST(void *, (uint8_t *)&t65, (void *)(gm_sves946fckobv));
            GM_ST(uintptr_t, (uint8_t *)&t65 + 8, (uintptr_t)(103));
            gm_b0_1 t66 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t62, t64, t65);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t67 = gm_l0;
            void * t68 = GM_LD(void *, (((uint8_t *)t67) + 0));
            gm_tl874t2atnlh3 t69 = gm_l4;
            void * t70 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t68, t69);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t71 = 0;
            F.ret = t70;
            goto gm_fret;
        }
ix50:;
ix41:;
ix34:;
    }
    {
        void * t72 = gm_l0;
        gm_b16_8 t73 = gm_l6;
        void * t74 = gm_l2;
        gm_tl874t2atnlh3 t75 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t74) + 72));
        gm_b16_8 t76 = gm_l3;
        gm_tl874t2atnlh3 t77 = gm_l4;
        gm_ti6pdj2sunpe4 t78;
        gm_f_template_args_vf1gdn1((void *)&t78, (void *)NULL, cx, NULL, t72, t73, t75, t76, t77);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t0uuhaljsf410 t79;
        if (((((uint8_t *)&t78))[64] != 0)) {
            t79 = (*(gm_t0uuhaljsf410 *)(((uint8_t *)&t78) + 0));
        } else {
            void * t80 = gm_l0;
            void * t81 = GM_LD(void *, (((uint8_t *)t80) + 0));
            gm_tl874t2atnlh3 t82 = gm_l4;
            void * t83 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t81, t82);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t84 = 0;
            F.ret = t83;
            goto gm_fret;
        }
        F.l10 = t79;
    }
    {
        {
            gm_l12 = ((uint64_t)0ULL);
        }
        {
            uint64_t t86 = GM_LD(uintptr_t, (((uint8_t *)&F.l10) + 32) + 8);
            gm_l11 = t86;
        }
        {
            for (;;) {
                uint64_t t88 = gm_l12;
                uint64_t t89 = gm_l11;
                uint8_t t90 = (t88 < t89);
                if (!t90) goto lb87;
                {
                    uint64_t t93 = gm_l12;
                    uint64_t t96 = GM_LD(uint64_t, ((uint8_t *)&t93));
                    gm_l13 = t96;
                    {
                        void * t99 = gm_l0;
                        uint64_t t100 = gm_l13;
                        uint64_t t101 = (uintptr_t)t100;
                        uint64_t t102 = GM_LD(uintptr_t, (((uint8_t *)&F.l10) + 32) + 8);
                        if (t101 >= (uintptr_t)(t102)) gm_trap_index(t101, t102, gm_src6d57grm686e3, 303, 23);
                        void * t103 = (void *)((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l10) + 32)) + t101 * 88);
                        gm_t8cc1mtbrhlpt t104;
                        gm_f_soa_of_jufdcj2((void *)&t104, (void *)NULL, cx, NULL, t99, t103);
                        if (gm_unw.kind) {
                            goto gm_ll91;
                        }
                        uint8_t t105;
                        if (((((uint8_t *)&t104))[32] != 0)) goto mn107;
                        t105 = ((uint8_t)0);
                        goto me106;
mn107:;
                        t105 = ((uint8_t)1);
                        goto me106;
mn108:;
                        gm_trap("no match arm matched the scrutinee", gm_src6d57grm686e3, 303, 12);
me106:;
                        if (!t105) goto ie98;
                        {
                            uint64_t t110 = gm_l13;
                            uint64_t t111 = (uintptr_t)t110;
                            uint64_t t112 = GM_LD(uintptr_t, (((uint8_t *)&F.l10) + 32) + 8);
                            if (t111 >= (uintptr_t)(t112)) gm_trap_index(t111, t112, gm_src6d57grm686e3, 304, 26);
                            gm_tq7s98ihonkju t113 = gm_f_new_ve8bb01_2((void *)NULL, cx, NULL, &(*(gm_t69m56meqefs8 *)((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l10) + 32)) + t111 * 88)), &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_k08q1k9tq78)));
                            if (gm_unw.kind) {
                                goto gm_ll91;
                            }
                            if (((((uint8_t *)&t113))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t113) + 8, gm_src6d57grm686e3, 304, 22);
                            void * t114 = GM_LD(void *, (((uint8_t *)&t113) + 8));
                            gm_l14 = t114;
                        }
                        {
                            uint64_t t115 = gm_l13;
                            uint64_t t116 = (uintptr_t)t115;
                            uint64_t t117 = GM_LD(uintptr_t, (((uint8_t *)&F.l10) + 32) + 8);
                            if (t116 >= (uintptr_t)(t117)) gm_trap_index(t116, t117, gm_src6d57grm686e3, 305, 13);
                            uint8_t *t118 = ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l10) + 32)) + t116 * 88);
                            void * t119 = gm_l0;
                            void * t120 = gm_l14;
                            gm_b16_8 t121;
                            GM_ST(void *, (uint8_t *)&t121, (void *)(gm_s4kt3mhlp0or9));
                            GM_ST(uintptr_t, (uint8_t *)&t121 + 8, (uintptr_t)(10));
                            void * t122 = gm_f_soa_columns_tuple_ns477md((void *)NULL, cx, NULL, t119, t120, t121);
                            if (gm_unw.kind) {
                                goto gm_ll91;
                            }
                            gm_t69m56meqefs8 t123 = GM_LD(gm_t69m56meqefs8, ((uint8_t *)t122));
                            GM_ST(gm_t69m56meqefs8, t118, t123);
                        }
                        goto ix98;
ie98:;
ix98:;
                    }
                    goto me94;
mn95:;
                    gm_trap("no match arm matched the scrutinee", gm_src6d57grm686e3, 302, 5);
me94:;
                }
lc87:;
                {
                    uint64_t t125 = gm_l12;
                    uint64_t r126; bool o127 = __builtin_add_overflow(t125, ((uint64_t)1ULL), &r126);
                    uint64_t t128 = r126;
                    gm_l12 = t128;
                }
            }
gm_ll91:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb87; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc87; }
            }
            goto gm_lad;
lb87:;
        }
    }
    {
        void * t129 = gm_l0;
        void * t130 = GM_LD(void *, (((uint8_t *)t129) + 0));
        void * t131 = (void *)(((uint8_t *)t130) + 8);
        gm_t88rcucg1b7uk t132 = gm_f_unit_2age34u((void *)NULL, cx, NULL, t131);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tntn2tf15puib t133;
        memcpy((uint8_t *)&t133 + 0, ((uint8_t *)&F.l1), 40);
        gm_b16_8 t134 = GM_LD(gm_b16_8, (((uint8_t *)&F.l10) + 0));
        GM_ST(gm_b16_8, (uint8_t *)&t133 + 40, t134);
        gm_b16_8 t135 = GM_LD(gm_b16_8, (((uint8_t *)&F.l10) + 16));
        GM_ST(gm_b16_8, (uint8_t *)&t133 + 56, t135);
        gm_b16_8 t136 = GM_LD(gm_b16_8, (((uint8_t *)&F.l10) + 32));
        GM_ST(gm_b16_8, (uint8_t *)&t133 + 72, t136);
        gm_tq7s98ihonkju t137 = gm_f_new_2pnaa87((void *)NULL, cx, NULL, &t133, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_k2uh8q3i8eo)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t137))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t137) + 8, gm_src6d57grm686e3, 308, 45);
        void * t138 = GM_LD(void *, (((uint8_t *)&t137) + 8));
        gm_teti411eujvf4 t139;
        memset(&t139, 0, sizeof t139);
        GM_ST(uint8_t, ((uint8_t *)&t139 + 0), ((uint8_t)55ULL));
        GM_ST(void *, ((uint8_t *)&t139 + 8), t138);
        gm_tl874t2atnlh3 t140 = gm_l4;
        void * t141 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t132, &t139, t140);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l15 = t141;
    }
    {
        uint64_t t143 = GM_LD(uintptr_t, (((uint8_t *)&F.l10) + 48) + 8);
        uint8_t t144 = ((t143 == ((uint64_t)0ULL)));
        if (!t144) goto ie142;
        {
            void * t146 = gm_l15;
            uint8_t t147 = 0;
            F.ret = t146;
            goto gm_fret;
        }
        goto ix142;
ie142:;
ix142:;
    }
    {
        gm_f_list_ir0h3tc_2((void *)&F.l16, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t149 = GM_LD(gm_b16_8, (((uint8_t *)&F.l10) + 48));
            gm_l17 = t149;
        }
        {
            gm_l18 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t151 = gm_l18;
                uint64_t t152 = GM_LD(uintptr_t, ((uint8_t *)&gm_l17) + 8);
                uint8_t t153 = (t151 < t152);
                if (!t153) goto lb150;
                {
                    uint64_t t156 = gm_l18;
                    uint64_t t157 = (uintptr_t)t156;
                    uint64_t t158 = GM_LD(uintptr_t, ((uint8_t *)&gm_l17) + 8);
                    gm_trcmmd12cefb1 t161 = GM_LD(gm_trcmmd12cefb1, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l17)) + t157 * 32));
                    gm_l19 = t161;
                    {
                        void * t163 = (void *)((uint8_t *)&F.l16);
                        gm_tftb1odc739t5 t164 = gm_f_push_d6c76ud_2(t163, &(*(gm_trcmmd12cefb1 *)((uint8_t *)&gm_l19)));
                        if (((((uint8_t *)&t164))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t164) + 1, gm_src6d57grm686e3, 311, 26);
                        gm_b0_1 t165 = GM_LD(gm_b0_1, (((uint8_t *)&t164) + 1));
                    }
                    goto me159;
mn160:;
                    gm_trap("no match arm matched the scrutinee", gm_src6d57grm686e3, 311, 5);
me159:;
                }
lc150:;
                {
                    uint64_t t167 = gm_l18;
                    uint64_t r168; bool o169 = __builtin_add_overflow(t167, ((uint64_t)1ULL), &r168);
                    uint64_t t170 = r168;
                    gm_l18 = t170;
                }
            }
lb150:;
        }
    }
    {
        void * t171 = (void *)((uint8_t *)&F.l16);
        void * t172 = gm_l15;
        gm_trcmmd12cefb1 t173;
        GM_ST(uint8_t, (uint8_t *)&t173 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t173 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t173 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t173 + 16, 0);
        GM_ST(uint64_t, (uint8_t *)&t173 + 24, 0);
        GM_ST(uint8_t, ((uint8_t *)&t173 + 0), ((uint8_t)4ULL));
        GM_ST(void *, ((uint8_t *)&t173 + 8), t172);
        gm_tftb1odc739t5 t174 = gm_f_push_d6c76ud_2(t171, &t173);
        if (((((uint8_t *)&t174))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t174) + 1, gm_src6d57grm686e3, 312, 5);
        gm_b0_1 t175 = GM_LD(gm_b0_1, (((uint8_t *)&t174) + 1));
    }
    {
        void * t176 = gm_l0;
        void * t177 = GM_LD(void *, (((uint8_t *)t176) + 0));
        gm_b16_8 t178 = GM_LD(gm_b16_8, (((uint8_t *)&F.l16) + 0));
        gm_tl874t2atnlh3 t179 = gm_l4;
        void * t180 = gm_f_block_of_2gjl2i1((void *)NULL, cx, NULL, t177, t178, t179);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t181 = 0;
        F.ret = t180;
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
__attribute__((aligned(4))) void gm_f_check_spec_4vp7jhg(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2) {
    struct gm_fr_check_spec_4vp7jhg F;
    void * gm_l0; /* s */
    void * gm_l1; /* spec */
    gm_t88rcucg1b7uk gm_l2; /* t */
    void * gm_l3; /* tys */
    uint8_t gm_l4; /* int */
    uint8_t gm_l5; /* float */
    uint8_t gm_l6; /* based */
    uint8_t gm_l7; /* bytes */
    gm_t88rcucg1b7uk gm_l8; /* elem */
    gm_th2lqbo77pvsc gm_l9; /* want */
    gm_b16_8 gm_l10; /* w */
    GM_STACK_CHECK(&F, 774u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        void * t3 = (void *)(((uint8_t *)t2) + 8);
        gm_l3 = t3;
    }
    {
        void * t4 = gm_l3;
        gm_t88rcucg1b7uk t5 = gm_l2;
        uint8_t t6 = gm_f_is_integer_8d4f9ek(t4, GM_CONV(gm_csguankko4lmhd, t5));
        gm_l4 = t6;
    }
    {
        void * t7 = gm_l3;
        gm_t88rcucg1b7uk t8 = gm_l2;
        gm_t53at80pfdus3 t9;
        gm_f_kind_jcnt4hm((void *)&t9, t7, GM_CONV(gm_csguankko4lmhd, t8));
        uint8_t t10;
        uint8_t t13 = GM_LD(uint8_t, (((uint8_t *)&t9) + 0));
        if (t13 != ((uint8_t)3ULL)) goto mn12;
        t10 = ((uint8_t)1);
        goto me11;
mn12:;
        t10 = ((uint8_t)0);
        goto me11;
mn14:;
        gm_trap("no match arm matched the scrutinee", gm_src6d57grm686e3, 216, 14);
me11:;
        gm_l5 = t10;
    }
    {
        uint8_t t15 = 0;
        uint8_t t16 = 0;
        uint8_t t17 = 0;
        void * t18 = gm_l1;
        uint8_t t19 = ((GM_LD(uint8_t, (((uint8_t *)t18) + 20) + 0) == ((uint8_t)1ULL)));
        if (!t19) {
            void * t20 = gm_l1;
            uint8_t t21 = ((GM_LD(uint8_t, (((uint8_t *)t20) + 20) + 0) == ((uint8_t)2ULL)));
            t17 = t21;
        } else t17 = 1;
        if (!t17) {
            void * t22 = gm_l1;
            uint8_t t23 = ((GM_LD(uint8_t, (((uint8_t *)t22) + 20) + 0) == ((uint8_t)3ULL)));
            t16 = t23;
        } else t16 = 1;
        if (!t16) {
            void * t24 = gm_l1;
            uint8_t t25 = ((GM_LD(uint8_t, (((uint8_t *)t24) + 20) + 0) == ((uint8_t)4ULL)));
            t15 = t25;
        } else t15 = 1;
        gm_l6 = t15;
    }
    {
        gm_l7 = ((uint8_t)0);
    }
    {
        void * t27 = gm_l3;
        gm_t88rcucg1b7uk t28 = gm_l2;
        gm_t53at80pfdus3 t29;
        gm_f_kind_jcnt4hm((void *)&t29, t27, GM_CONV(gm_csguankko4lmhd, t28));
        uint8_t t30 = GM_LD(uint8_t, (((uint8_t *)&t29) + 0));
        if (t30 != ((uint8_t)10ULL)) goto ie26;
        gm_t88rcucg1b7uk t31 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t29) + 8) + 4));
        gm_l8 = t31;
        {
            void * t33 = gm_l3;
            gm_t88rcucg1b7uk t34 = gm_l8;
            gm_t53at80pfdus3 t35;
            gm_f_kind_jcnt4hm((void *)&t35, t33, GM_CONV(gm_csguankko4lmhd, t34));
            gm_tjcj4j2gbh6c8 t36;
            GM_ST(uint8_t, (uint8_t *)&t36 + 1, 0);
            GM_ST(uint8_t, ((uint8_t *)&t36 + 0), ((uint8_t)0));
            GM_ST(uint16_t, ((uint8_t *)&t36 + 2), ((uint16_t)8ULL));
            uint8_t t37 = (((GM_LD(uint8_t, ((uint8_t *)&t35) + 0) == ((uint8_t)0ULL)) && gm_eqo3v9599nus((((uint8_t *)&t35) + 8), ((uint8_t *)&t36))));
            gm_l7 = t37;
        }
        goto ix26;
ie26:;
ix26:;
    }
    {
        gm_th2lqbo77pvsc t38;
        memset(&t38, 0, sizeof t38);
        gm_l9 = t38;
    }
    {
        void * t40 = gm_l1;
        uint8_t t41 = ((GM_LD(uint8_t, (((uint8_t *)t40) + 20) + 0) == ((uint8_t)6ULL)));
        if (!t41) goto ie39;
        uint8_t t42 = gm_l7;
        uint8_t t43 = (uint8_t)!t42;
        if (!t43) goto ie39;
        {
            gm_b16_8 t45;
            GM_ST(void *, (uint8_t *)&t45, (void *)(gm_s93i7ga7kbfpu));
            GM_ST(uintptr_t, (uint8_t *)&t45 + 8, (uintptr_t)(33));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l9)) + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l9)) + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l9)) + 20, 0);
            ((uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l9)))[16] = 1;
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l9)) + 0, t45);
        }
        goto ix39;
ie39:;
        uint8_t t47 = gm_l6;
        if (!t47) goto ie46;
        uint8_t t48 = gm_l4;
        uint8_t t49 = (uint8_t)!t48;
        if (!t49) goto ie46;
        {
            gm_b16_8 t51;
            GM_ST(void *, (uint8_t *)&t51, (void *)(gm_sggmh06s4b0cs));
            GM_ST(uintptr_t, (uint8_t *)&t51 + 8, (uintptr_t)(22));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l9)) + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l9)) + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l9)) + 20, 0);
            ((uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l9)))[16] = 1;
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l9)) + 0, t51);
        }
        goto ix46;
ie46:;
        void * t53 = gm_l1;
        uint8_t t54 = ((GM_LD(uint8_t, (((uint8_t *)t53) + 20) + 0) == ((uint8_t)5ULL)));
        if (!t54) goto ie52;
        uint8_t t55 = gm_l5;
        uint8_t t56 = (uint8_t)!t55;
        if (!t56) goto ie52;
        {
            gm_b16_8 t58;
            GM_ST(void *, (uint8_t *)&t58, (void *)(gm_s3on77ql0ndko));
            GM_ST(uintptr_t, (uint8_t *)&t58 + 8, (uintptr_t)(16));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l9)) + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l9)) + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l9)) + 20, 0);
            ((uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l9)))[16] = 1;
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l9)) + 0, t58);
        }
        goto ix52;
ie52:;
        void * t60 = gm_l1;
        uint8_t t61;
        if ((((((uint8_t *)t60) + 12))[4] != 0)) goto mn63;
        t61 = ((uint8_t)0);
        goto me62;
mn63:;
        t61 = ((uint8_t)1);
        goto me62;
mn64:;
        gm_trap("no match arm matched the scrutinee", gm_src6d57grm686e3, 227, 15);
me62:;
        if (!t61) goto ie59;
        uint8_t t65 = gm_l5;
        uint8_t t66 = (uint8_t)!t65;
        if (!t66) goto ie59;
        {
            gm_b16_8 t68;
            GM_ST(void *, (uint8_t *)&t68, (void *)(gm_s23qsffuqlc1v));
            GM_ST(uintptr_t, (uint8_t *)&t68 + 8, (uintptr_t)(24));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l9)) + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l9)) + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l9)) + 20, 0);
            ((uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l9)))[16] = 1;
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l9)) + 0, t68);
        }
        goto ix59;
ie59:;
        uint8_t t70 = 0;
        void * t71 = gm_l1;
        uint8_t t72 = GM_LD(uint8_t, (((uint8_t *)t71) + 2));
        if (!t72) {
            void * t73 = gm_l1;
            uint8_t t74 = GM_LD(uint8_t, (((uint8_t *)t73) + 4));
            t70 = t74;
        } else t70 = 1;
        if (!t70) goto ie69;
        uint8_t t75 = 0;
        uint8_t t76 = gm_l4;
        if (!t76) {
            uint8_t t77 = gm_l5;
            t75 = t77;
        } else t75 = 1;
        uint8_t t78 = (uint8_t)!t75;
        if (!t78) goto ie69;
        {
            gm_b16_8 t80;
            GM_ST(void *, (uint8_t *)&t80, (void *)(gm_soreld657hea2));
            GM_ST(uintptr_t, (uint8_t *)&t80 + 8, (uintptr_t)(24));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l9)) + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l9)) + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l9)) + 20, 0);
            ((uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l9)))[16] = 1;
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l9)) + 0, t80);
        }
        goto ix69;
ie69:;
ix69:;
ix59:;
ix52:;
ix46:;
ix39:;
    }
    {
        gm_th2lqbo77pvsc t81 = gm_l9;
        gm_b16_8 t82;
        if (((((uint8_t *)&t81))[16] != 0)) {
            gm_b16_8 t83 = GM_LD(gm_b16_8, (((uint8_t *)&t81) + 0));
            t82 = t83;
        } else {
            gm_th2lqbo77pvsc t84;
            memset(&t84, 0, sizeof t84);
            uint8_t t85 = 0;
            F.ret = t84;
            goto gm_fret;
        }
        gm_l10 = t82;
    }
    {
        void * t87 = gm_l3;
        gm_t88rcucg1b7uk t88 = gm_l2;
        uint8_t t89 = gm_f_is_error_8k27gd1(t87, GM_CONV(gm_csguankko4lmhd, t88));
        if (!t89) goto ie86;
        {
            gm_th2lqbo77pvsc t91;
            memset(&t91, 0, sizeof t91);
            uint8_t t92 = 0;
            F.ret = t91;
            goto gm_fret;
        }
        goto ix86;
ie86:;
ix86:;
    }
    {
        gm_t6j6smj7qoctu t93;
        gm_b16_8 t94 = gm_l10;
        GM_ST(gm_b16_8, (uint8_t *)&t93 + 0, t94);
        void * t95 = gm_l0;
        gm_t88rcucg1b7uk t96 = gm_l2;
        gm_b16_8 t97 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t95, t96);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_b16_8, (uint8_t *)&t93 + 16, t97);
        gm_tabb710d9mq2e t98;
        gm_f_format_0t8rkc3((void *)&t98, (void *)NULL, cx, NULL, &t93);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t98))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t98) + 8, gm_src6d57grm686e3, 234, 12);
        gm_b16_8 t99 = GM_LD(gm_b16_8, (((uint8_t *)&t98) + 8));
        gm_b16_8 t100 = GM_LD(gm_b16_8, ((uint8_t *)&t99));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 20, 0);
        ((uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out))[16] = 1;
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 0, t100);
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
__attribute__((aligned(4))) void gm_f_const_template_5dvtt1m(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint64_t a2, uint32_t a3) {
    struct gm_fr_const_template_5dvtt1m F;
    void * gm_l0; /* cx */
    void * gm_l1; /* e */
    uint64_t gm_l2; /* module */
    uint32_t gm_l3; /* depth */
    void * gm_l4; /* inner */
    gm_tibupt4dmcqai gm_l5; /* lit */
    gm_t08olq97rscn3 gm_l6; /* id */
    uint64_t gm_l7; /* d */
    gm_t88rcucg1b7uk gm_l8; /* ty */
    void * gm_l9; /* item */
    GM_STACK_CHECK(&F, 1333u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        uint32_t t3 = gm_l3;
        uint8_t t4 = (t3 > ((uint32_t)8ULL));
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
        void * t8 = gm_l1;
        uint8_t t15 = GM_LD(uint8_t, ((((uint8_t *)t8) + 8) + 0));
        switch (t15) { case ((uint8_t)0ULL): goto ma13; case ((uint8_t)1ULL): goto ma13; case ((uint8_t)2ULL): goto ma11; case ((uint8_t)3ULL): goto ma13; case ((uint8_t)4ULL): goto ma13; case ((uint8_t)5ULL): goto ma13; case ((uint8_t)6ULL): goto ma12; case ((uint8_t)7ULL): goto ma13; case ((uint8_t)8ULL): goto ma13; case ((uint8_t)9ULL): goto ma10; case ((uint8_t)10ULL): goto ma13; case ((uint8_t)11ULL): goto ma13; case ((uint8_t)12ULL): goto ma13; case ((uint8_t)13ULL): goto ma13; case ((uint8_t)14ULL): goto ma13; case ((uint8_t)15ULL): goto ma13; case ((uint8_t)16ULL): goto ma13; case ((uint8_t)17ULL): goto ma13; case ((uint8_t)18ULL): goto ma13; case ((uint8_t)19ULL): goto ma13; case ((uint8_t)20ULL): goto ma13; case ((uint8_t)21ULL): goto ma13; case ((uint8_t)22ULL): goto ma13; case ((uint8_t)23ULL): goto ma13; case ((uint8_t)24ULL): goto ma13; case ((uint8_t)25ULL): goto ma13; case ((uint8_t)26ULL): goto ma13; case ((uint8_t)27ULL): goto ma13; case ((uint8_t)28ULL): goto ma13; case ((uint8_t)29ULL): goto ma13; case ((uint8_t)30ULL): goto ma13; case ((uint8_t)31ULL): goto ma13; case ((uint8_t)32ULL): goto ma13; case ((uint8_t)33ULL): goto ma13; case ((uint8_t)34ULL): goto ma13; case ((uint8_t)35ULL): goto ma13; case ((uint8_t)36ULL): goto ma13; case ((uint8_t)37ULL): goto ma13; case ((uint8_t)38ULL): goto ma13; case ((uint8_t)39ULL): goto ma13; case ((uint8_t)40ULL): goto ma13; case ((uint8_t)41ULL): goto ma13; case ((uint8_t)42ULL): goto ma13; case ((uint8_t)43ULL): goto ma13; case ((uint8_t)44ULL): goto ma13; case ((uint8_t)45ULL): goto ma13; case ((uint8_t)46ULL): goto ma13; case ((uint8_t)47ULL): goto ma13; case ((uint8_t)48ULL): goto ma13; case ((uint8_t)49ULL): goto ma13; case ((uint8_t)50ULL): goto ma13; default: goto ma13; }
ma10:;
        uint8_t t17 = GM_LD(uint8_t, ((((uint8_t *)t8) + 8) + 0));
        if (t17 != ((uint8_t)9ULL)) goto mn16;
        void * t18 = GM_LD(void *, ((((uint8_t *)t8) + 8) + 8));
        gm_l4 = t18;
        void * t19 = gm_l0;
        void * t20 = gm_l4;
        uint64_t t21 = gm_l2;
        uint32_t t22 = gm_l3;
        gm_f_const_template_5dvtt1m((void *)&(*(gm_th2lqbo77pvsc *)gm_out), (void *)NULL, cx, NULL, t19, t20, t21, t22);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto gm_fdone;
        goto me9;
mn16:;
ma11:;
        uint8_t t24 = GM_LD(uint8_t, ((((uint8_t *)t8) + 8) + 0));
        if (t24 != ((uint8_t)2ULL)) goto mn23;
        gm_tibupt4dmcqai t25 = GM_LD(gm_tibupt4dmcqai, ((((uint8_t *)t8) + 8) + 8));
        gm_l5 = t25;
        gm_th2lqbo77pvsc t26;
        uint8_t t28 = ((GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 16) + 0) == ((uint8_t)2ULL)));
        if (!t28) goto ie27;
        gm_th2lqbo77pvsc t30;
        memset(&t30, 0, sizeof t30);
        gm_th2lqbo77pvsc t31 = t30;
        t26 = t31;
        goto ix27;
ie27:;
        gm_b16_8 t33 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l5) + 0));
        gm_b16_8 t34 = t33;
        gm_b16_8 t35 = GM_LD(gm_b16_8, ((uint8_t *)&t34));
        gm_th2lqbo77pvsc t36;
        GM_ST(uint8_t, (uint8_t *)&t36 + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&t36 + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&t36 + 20, 0);
        GM_ST(gm_b16_8, ((uint8_t *)&t36), t35);
        ((uint8_t *)&t36)[16] = 1;
        t26 = t36;
ix27:;
        uint8_t t37 = 0;
        F.ret = t26;
        goto gm_fret;
        goto me9;
mn23:;
ma12:;
        uint8_t t39 = GM_LD(uint8_t, ((((uint8_t *)t8) + 8) + 0));
        if (t39 != ((uint8_t)6ULL)) goto mn38;
        gm_t08olq97rscn3 t40 = GM_LD(gm_t08olq97rscn3, ((((uint8_t *)t8) + 8) + 8));
        gm_l6 = t40;
        {
            uint32_t t43 = gm_l3;
            uint8_t t44 = ((t43 == ((uint32_t)0ULL)));
            if (!t44) goto ie42;
            void * t45 = gm_l0;
            gm_b16_8 t46 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l6) + 0));
            gm_tqvu78rpi6q1h t47;
            gm_f_lookup_local_6f38t7p((void *)&t47, t45, t46);
            uint8_t t48;
            if (((((uint8_t *)&t47))[208] != 0)) goto mn50;
            t48 = ((uint8_t)0);
            goto me49;
mn50:;
            t48 = ((uint8_t)1);
            goto me49;
mn51:;
            gm_trap("no match arm matched the scrutinee", gm_src6d57grm686e3, 267, 31);
me49:;
            if (!t48) goto ie42;
            {
                gm_th2lqbo77pvsc t53;
                memset(&t53, 0, sizeof t53);
                uint8_t t54 = 0;
                F.ret = t53;
                goto gm_fret;
            }
            goto ix42;
ie42:;
ix42:;
        }
        {
            void * t55 = gm_l0;
            void * t56 = GM_LD(void *, (((uint8_t *)t55) + 0));
            uint64_t t57 = gm_l2;
            gm_b16_8 t58 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l6) + 0));
            gm_tf5srh4mp4b3i t59 = gm_f_lookup_module_name_cukfp7t((void *)NULL, cx, NULL, t56, t57, t58);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint64_t t60;
            if (((((uint8_t *)&t59))[8] != 0)) {
                uint64_t t61 = GM_LD(uint64_t, (((uint8_t *)&t59) + 0));
                t60 = t61;
            } else {
                gm_th2lqbo77pvsc t62;
                memset(&t62, 0, sizeof t62);
                uint8_t t63 = 0;
                F.ret = t62;
                goto gm_fret;
            }
            gm_l7 = t60;
        }
        {
            void * t65 = gm_l0;
            void * t66 = GM_LD(void *, (((uint8_t *)t65) + 0));
            uint64_t t67 = gm_l7;
            gm_t6qooom8qa2do t68;
            gm_f_resolve_decl_pus042v((void *)&t68, (void *)NULL, cx, NULL, t66, t67);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t69 = GM_LD(uint8_t, (((uint8_t *)&t68) + 0));
            if (t69 != ((uint8_t)7ULL)) goto ie64;
            gm_t88rcucg1b7uk t70 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t68) + 8));
            gm_l8 = t70;
            {
                gm_t88rcucg1b7uk t73 = gm_l8;
                void * t74 = gm_l0;
                void * t75 = GM_LD(void *, (((uint8_t *)t74) + 0));
                void * t76 = (void *)(((uint8_t *)t75) + 8);
                gm_t88rcucg1b7uk t77 = gm_f_string_ty_j74ff0o((void *)NULL, cx, NULL, t76);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t78 = (!gm_eq9m6mvtmgde(((uint8_t *)&t73), ((uint8_t *)&t77)));
                if (!t78) goto ie72;
                {
                    gm_th2lqbo77pvsc t80;
                    memset(&t80, 0, sizeof t80);
                    uint8_t t81 = 0;
                    F.ret = t80;
                    goto gm_fret;
                }
                goto ix72;
ie72:;
ix72:;
            }
            {
                uint64_t t82 = gm_l7;
                uint64_t t83 = (uintptr_t)t82;
                void * t84 = gm_l0;
                void * t85 = GM_LD(void *, (((uint8_t *)t84) + 0));
                void * t86 = GM_LD(void *, (((uint8_t *)t85) + 0));
                uint64_t t87 = GM_LD(uintptr_t, ((((uint8_t *)t86) + 240) + 0) + 8);
                if (t83 >= (uintptr_t)(t87)) gm_trap_index(t83, t87, gm_src6d57grm686e3, 271, 22);
                void * t88 = (void *)((uint8_t *)GM_LD(void *, ((((uint8_t *)t86) + 240) + 0)) + t83 * 216);
                gm_l9 = t88;
            }
            {
                void * t90 = gm_l9;
                uint8_t t91 = GM_LD(uint8_t, (((((uint8_t *)t90) + 40) + 64) + 0));
                if (t91 != ((uint8_t)0ULL)) goto ie89;
                gm_tv2uf4leil5hq t92 = GM_LD(gm_tv2uf4leil5hq, (((((uint8_t *)t90) + 40) + 64) + 8));
                F.l10 = t92;
                {
                    uint64_t t95 = GM_LD(uintptr_t, (((uint8_t *)&F.l10) + 8) + 8);
                    uint8_t t96 = (!(t95 == ((uint64_t)1ULL)));
                    if (!t96) goto ie94;
                    {
                        gm_th2lqbo77pvsc t98;
                        memset(&t98, 0, sizeof t98);
                        uint8_t t99 = 0;
                        F.ret = t98;
                        goto gm_fret;
                    }
                    goto ix94;
ie94:;
ix94:;
                }
                {
                    void * t100 = gm_l0;
                    void * t101 = GM_LD(void *, (((uint8_t *)&F.l10) + 40));
                    void * t102 = gm_l9;
                    uint64_t t103 = GM_LD(uint64_t, (((uint8_t *)t102) + 32));
                    uint32_t t104 = gm_l3;
                    uint32_t r105; bool o106 = __builtin_add_overflow(t104, ((uint32_t)1ULL), &r105);
                    if (o106) gm_trap("integer overflow in `+`", gm_src6d57grm686e3, 274, 59);
                    uint32_t t107 = r105;
                    gm_f_const_template_5dvtt1m((void *)&(*(gm_th2lqbo77pvsc *)gm_out), (void *)NULL, cx, NULL, t100, t101, t103, t107);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_fdone;
                }
                goto ix89;
ie89:;
                {
                    gm_th2lqbo77pvsc t109;
                    memset(&t109, 0, sizeof t109);
                    uint8_t t110 = 0;
                    F.ret = t109;
                    goto gm_fret;
                }
ix89:;
            }
            goto ix64;
ie64:;
            {
                gm_th2lqbo77pvsc t112;
                memset(&t112, 0, sizeof t112);
                uint8_t t113 = 0;
                F.ret = t112;
                goto gm_fret;
            }
ix64:;
        }
        goto me9;
mn38:;
ma13:;
        gm_th2lqbo77pvsc t115;
        memset(&t115, 0, sizeof t115);
        uint8_t t116 = 0;
        F.ret = t115;
        goto gm_fret;
        goto me9;
mn114:;
ma14:;
        gm_trap("no match arm matched the scrutinee", gm_src6d57grm686e3, 263, 5);
me9:;
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
inline __attribute__((aligned(4))) void gm_f_number_at_esf7qkk(void *gm_out, gm_b16_8 a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_number_at_esf7qkk F;
    gm_b16_8 gm_l0; /* s */
    uint64_t gm_l1; /* start */
    uint64_t gm_l2; /* i */
    uint64_t gm_l3; /* n */
    uint8_t gm_l4; /* <tmp> */
    GM_STACK_CHECK_SHALLOW(&F, 266u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint64_t t2 = gm_l1;
        gm_l2 = t2;
    }
    {
        gm_l3 = ((uint64_t)0ULL);
    }
    {
        for (;;) {
            uint64_t t4 = gm_l2;
            uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            uint8_t t6 = (t4 < t5);
            if (!t6) goto lb3;
            {
                uint64_t t8 = gm_l2;
                uint64_t t9 = (uintptr_t)t8;
                uint64_t t10 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                if (t9 >= (uintptr_t)(t10)) gm_trap_index(t9, t10, gm_src6d57grm686e3, 131, 25);
                uint8_t t11 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t9 * 1));
                gm_l4 = t11;
            }
            uint8_t t12 = 0;
            uint8_t t13 = gm_l4;
            uint8_t t14 = (((uint8_t)48ULL) <= t13);
            if (t14) {
                uint8_t t15 = gm_l4;
                uint8_t t16 = (t15 <= ((uint8_t)57ULL));
                t12 = t16;
            } else t12 = 0;
            uint8_t t17 = t12;
            if (!t17) goto lb3;
            {
                uint64_t t20 = gm_l3;
                uint64_t r21; bool o22 = __builtin_mul_overflow(t20, ((uint64_t)10ULL), &r21);
                if (o22) gm_trap("integer overflow in `*`", gm_src6d57grm686e3, 132, 13);
                uint64_t t23 = r21;
                uint64_t t24 = gm_l2;
                uint64_t t25 = (uintptr_t)t24;
                uint64_t t26 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                if (t25 >= (uintptr_t)(t26)) gm_trap_index(t25, t26, gm_src6d57grm686e3, 132, 26);
                uint8_t t27 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t25 * 1));
                uint8_t r28; bool o29 = __builtin_sub_overflow(t27, ((uint8_t)48ULL), &r28);
                if (o29) gm_trap("integer overflow in `-`", gm_src6d57grm686e3, 132, 26);
                uint8_t t30 = r28;
                uint64_t t31 = (uint64_t)t30;
                uint64_t r32; bool o33 = __builtin_add_overflow(t23, t31, &r32);
                if (o33) gm_trap("integer overflow in `+`", gm_src6d57grm686e3, 132, 13);
                uint64_t t34 = r32;
                gm_l3 = t34;
            }
            {
                uint64_t t36 = gm_l3;
                uint8_t t37 = (t36 > ((uint64_t)65536ULL));
                if (!t37) goto ie35;
                {
                    gm_l3 = ((uint64_t)65537ULL);
                }
                goto ix35;
ie35:;
ix35:;
            }
            {
                uint64_t t39 = gm_l2;
                uint64_t r40; bool o41 = __builtin_add_overflow(t39, ((uint64_t)1ULL), &r40);
                if (o41) gm_trap("integer overflow in `+`", gm_src6d57grm686e3, 134, 9);
                uint64_t t42 = r40;
                gm_l2 = t42;
            }
lc3:;
        }
lb3:;
    }
    {
        uint64_t t44 = gm_l2;
        uint64_t t45 = gm_l1;
        uint8_t t46 = ((t44 == t45));
        if (!t46) goto ie43;
        {
            memset((uint8_t *)&(*(gm_tmnrj1ud20adj *)gm_out) + 17, 0, 7);
            gm_t9805qtonbsag t48;
            memset(&t48, 0, sizeof t48);
            GM_ST(gm_t9805qtonbsag, (uint8_t *)&(*(gm_tmnrj1ud20adj *)gm_out) + 0, t48);
            uint64_t t49 = gm_l1;
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tmnrj1ud20adj *)gm_out) + 8, t49);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tmnrj1ud20adj *)gm_out) + 16, ((uint8_t)0));
            goto gm_fdone;
        }
        goto ix43;
ie43:;
ix43:;
    }
    {
        uint64_t t51 = gm_l3;
        uint8_t t52 = (t51 > ((uint64_t)65536ULL));
        if (!t52) goto ie50;
        {
            memset((uint8_t *)&(*(gm_tmnrj1ud20adj *)gm_out) + 17, 0, 7);
            gm_t9805qtonbsag t54;
            memset(&t54, 0, sizeof t54);
            GM_ST(gm_t9805qtonbsag, (uint8_t *)&(*(gm_tmnrj1ud20adj *)gm_out) + 0, t54);
            uint64_t t55 = gm_l2;
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tmnrj1ud20adj *)gm_out) + 8, t55);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tmnrj1ud20adj *)gm_out) + 16, ((uint8_t)1));
            goto gm_fdone;
        }
        goto ix50;
ie50:;
ix50:;
    }
    {
        memset((uint8_t *)&(*(gm_tmnrj1ud20adj *)gm_out) + 17, 0, 7);
        uint64_t t56 = gm_l3;
        uint32_t t57 = (uint32_t)t56;
        gm_t9805qtonbsag t58;
        GM_ST(uint8_t, (uint8_t *)&t58 + 5, 0);
        GM_ST(uint16_t, (uint8_t *)&t58 + 6, 0);
        GM_ST(uint32_t, ((uint8_t *)&t58), t57);
        ((uint8_t *)&t58)[4] = 1;
        uint64_t t59 = gm_l2;
        GM_ST(gm_t9805qtonbsag, (uint8_t *)&(*(gm_tmnrj1ud20adj *)gm_out) + 0, t58);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_tmnrj1ud20adj *)gm_out) + 8, t59);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tmnrj1ud20adj *)gm_out) + 16, ((uint8_t)0));
        goto gm_fdone;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_tmnrj1ud20adj gm_f_number_at_esf7qkk_c(gm_b16_8 a0, uint64_t a1) { gm_tmnrj1ud20adj gm_r; gm_f_number_at_esf7qkk(&gm_r, a0, a1); return gm_r; }
__attribute__((aligned(4))) void gm_f_numbered_error_07hll20(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, uint64_t a1) {
    struct gm_fr_numbered_error_07hll20 F;
    uint64_t gm_l1; /* m */
    gm_b16_8 gm_l2; /* given */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    uint64_t gm_l5; /* ix */
    uint64_t gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    uint64_t gm_l8; /* k */
    uint8_t gm_l9; /* used */
    gm_b16_8 gm_l10; /* <tmp> */
    uint64_t gm_l11; /* <tmp> */
    uint64_t gm_l12; /* ix */
    GM_STACK_CHECK(&F, 787u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l1 = a1;
    {
        gm_b16_8 t2;
        uint64_t t4 = gm_l1;
        uint8_t t5 = ((t4 == ((uint64_t)0ULL)));
        if (!t5) goto ie3;
        gm_b16_8 t7;
        GM_ST(void *, (uint8_t *)&t7, (void *)(gm_sp920qd66qq8t));
        GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)(19));
        gm_b16_8 t8 = t7;
        t2 = t8;
        goto ix3;
ie3:;
        gm_b16_8 t9;
        uint64_t t11 = gm_l1;
        uint8_t t12 = ((t11 == ((uint64_t)1ULL)));
        if (!t12) goto ie10;
        gm_b16_8 t14;
        GM_ST(void *, (uint8_t *)&t14, (void *)(gm_s378kfmdl5enf));
        GM_ST(uintptr_t, (uint8_t *)&t14 + 8, (uintptr_t)(16));
        gm_b16_8 t15 = t14;
        t9 = t15;
        goto ix10;
ie10:;
        uint64_t t17 = gm_l1;
        gm_t4ce0jd3tdro4 t18;
        GM_ST(uint64_t, ((uint8_t *)&t18 + 0), t17);
        gm_tabb710d9mq2e t19;
        gm_f_format_h02tha7((void *)&t19, (void *)NULL, cx, NULL, t18);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t19))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t19) + 8, gm_src6d57grm686e3, 333, 9);
        gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&t19) + 8));
        gm_b16_8 t21 = t20;
        gm_b16_8 t22 = GM_LD(gm_b16_8, ((uint8_t *)&t21));
        t9 = t22;
ix10:;
        t2 = t9;
ix3:;
        gm_l2 = t2;
    }
    {
        {
            gm_b16_8 t24 = GM_LD(gm_b16_8, (((uint8_t *)a0) + 32));
            gm_l3 = t24;
        }
        {
            gm_l4 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t26 = gm_l4;
                uint64_t t27 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                uint8_t t28 = (t26 < t27);
                if (!t28) goto lb25;
                {
                    uint64_t t31 = gm_l4;
                    uint64_t t32 = (uintptr_t)t31;
                    uint64_t t33 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                    uint64_t t34 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t32 * 8));
                    uint64_t t37 = GM_LD(uint64_t, ((uint8_t *)&t34));
                    gm_l5 = t37;
                    {
                        uint64_t t40 = gm_l5;
                        uint64_t t41 = gm_l1;
                        uint8_t t42 = (t40 >= t41);
                        if (!t42) goto ie39;
                        {
                            gm_tf1bjg81u0glv t44;
                            uint64_t t45 = gm_l5;
                            GM_ST(uint64_t, (uint8_t *)&t44 + 0, t45);
                            gm_b16_8 t46 = gm_l2;
                            GM_ST(gm_b16_8, (uint8_t *)&t44 + 8, t46);
                            gm_tabb710d9mq2e t47;
                            gm_f_format_ksi8bsm((void *)&t47, (void *)NULL, cx, NULL, &t44);
                            if (gm_unw.kind) {
                                goto gm_ll29;
                            }
                            if (((((uint8_t *)&t47))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t47) + 8, gm_src6d57grm686e3, 336, 29);
                            gm_b16_8 t48 = GM_LD(gm_b16_8, (((uint8_t *)&t47) + 8));
                            gm_b16_8 t49 = GM_LD(gm_b16_8, ((uint8_t *)&t48));
                            GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 17, 0);
                            GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 18, 0);
                            GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 20, 0);
                            ((uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out))[16] = 1;
                            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 0, t49);
                            goto gm_fdone;
                        }
                        goto ix39;
ie39:;
ix39:;
                    }
                    goto me35;
mn36:;
                    gm_trap("no match arm matched the scrutinee", gm_src6d57grm686e3, 335, 5);
me35:;
                }
lc25:;
                {
                    uint64_t t51 = gm_l4;
                    uint64_t r52; bool o53 = __builtin_add_overflow(t51, ((uint64_t)1ULL), &r52);
                    uint64_t t54 = r52;
                    gm_l4 = t54;
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
        {
            gm_l7 = ((uint64_t)0ULL);
        }
        {
            uint64_t t56 = gm_l1;
            gm_l6 = t56;
        }
        {
            for (;;) {
                uint64_t t58 = gm_l7;
                uint64_t t59 = gm_l6;
                uint8_t t60 = (t58 < t59);
                if (!t60) goto lb57;
                {
                    uint64_t t63 = gm_l7;
                    uint64_t t66 = GM_LD(uint64_t, ((uint8_t *)&t63));
                    gm_l8 = t66;
                    {
                        gm_l9 = ((uint8_t)0);
                    }
                    {
                        {
                            gm_b16_8 t69 = GM_LD(gm_b16_8, (((uint8_t *)a0) + 32));
                            gm_l10 = t69;
                        }
                        {
                            gm_l11 = ((uint64_t)0ULL);
                        }
                        {
                            for (;;) {
                                uint64_t t71 = gm_l11;
                                uint64_t t72 = GM_LD(uintptr_t, ((uint8_t *)&gm_l10) + 8);
                                uint8_t t73 = (t71 < t72);
                                if (!t73) goto lb70;
                                {
                                    uint64_t t76 = gm_l11;
                                    uint64_t t77 = (uintptr_t)t76;
                                    uint64_t t78 = GM_LD(uintptr_t, ((uint8_t *)&gm_l10) + 8);
                                    uint64_t t79 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l10)) + t77 * 8));
                                    uint64_t t82 = GM_LD(uint64_t, ((uint8_t *)&t79));
                                    gm_l12 = t82;
                                    {
                                        uint64_t t85 = gm_l12;
                                        uint64_t t86 = gm_l8;
                                        uint8_t t87 = ((t85 == t86));
                                        if (!t87) goto ie84;
                                        {
                                            gm_l9 = ((uint8_t)1);
                                        }
                                        goto ix84;
ie84:;
ix84:;
                                    }
                                    goto me80;
mn81:;
                                    gm_trap("no match arm matched the scrutinee", gm_src6d57grm686e3, 340, 9);
me80:;
                                }
lc70:;
                                {
                                    uint64_t t90 = gm_l11;
                                    uint64_t r91; bool o92 = __builtin_add_overflow(t90, ((uint64_t)1ULL), &r91);
                                    uint64_t t93 = r91;
                                    gm_l11 = t93;
                                }
                            }
lb70:;
                        }
                    }
                    {
                        uint8_t t95 = gm_l9;
                        uint8_t t96 = (uint8_t)!t95;
                        if (!t96) goto ie94;
                        {
                            uint64_t t98 = gm_l8;
                            gm_t4ce0jd3tdro4 t99;
                            GM_ST(uint64_t, ((uint8_t *)&t99 + 0), t98);
                            gm_tabb710d9mq2e t100;
                            gm_f_format_4duro1p((void *)&t100, (void *)NULL, cx, NULL, t99);
                            if (gm_unw.kind) {
                                goto gm_ll61;
                            }
                            if (((((uint8_t *)&t100))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t100) + 8, gm_src6d57grm686e3, 343, 30);
                            gm_b16_8 t101 = GM_LD(gm_b16_8, (((uint8_t *)&t100) + 8));
                            gm_b16_8 t102 = GM_LD(gm_b16_8, ((uint8_t *)&t101));
                            GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 17, 0);
                            GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 18, 0);
                            GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 20, 0);
                            ((uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out))[16] = 1;
                            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 0, t102);
                            goto gm_fdone;
                        }
                        goto ix94;
ie94:;
ix94:;
                    }
                    goto me64;
mn65:;
                    gm_trap("no match arm matched the scrutinee", gm_src6d57grm686e3, 338, 5);
me64:;
                }
lc57:;
                {
                    uint64_t t104 = gm_l7;
                    uint64_t r105; bool o106 = __builtin_add_overflow(t104, ((uint64_t)1ULL), &r105);
                    uint64_t t107 = r105;
                    gm_l7 = t107;
                }
            }
gm_ll61:;
            if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb57; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc57; }
            }
            goto gm_lad;
lb57:;
        }
    }
    {
        gm_th2lqbo77pvsc t108;
        memset(&t108, 0, sizeof t108);
        uint8_t t109 = 0;
        F.ret = t108;
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
__attribute__((aligned(4))) void gm_f_parse_spec_iod83cp(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_parse_spec_iod83cp F;
    gm_b16_8 gm_l0; /* s */
    gm_t7g88i52rrtm5 gm_l1; /* spec */
    uint64_t gm_l2; /* i */
    gm_taari1s0m51a7 gm_l3; /* second */
    gm_taari1s0m51a7 gm_l4; /* first */
    gm_t9qkh0rsooc8d gm_l5; /* a */
    gm_t9qkh0rsooc8d gm_l6; /* a */
    gm_tmnrj1ud20adj gm_l7; /* w */
    uint32_t gm_l8; /* v */
    gm_tmnrj1ud20adj gm_l9; /* p */
    uint32_t gm_l10; /* pv */
    uint8_t gm_l11; /* c */
    uint8_t gm_l12; /* based */
    GM_STACK_CHECK(&F, 1080u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_default_spec_9ntjq2s((void *)&gm_l1);
    }
    {
        gm_l2 = ((uint64_t)0ULL);
    }
    {
        gm_taari1s0m51a7 t2;
        memset(&t2, 0, sizeof t2);
        gm_l3 = t2;
    }
    {
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        uint8_t t5 = (t4 >= ((uint64_t)2ULL));
        if (!t5) goto ie3;
        {
            uint64_t t7 = (uintptr_t)((uint64_t)1ULL);
            uint64_t t8 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            if (t7 >= (uintptr_t)(t8)) gm_trap_index(t7, t8, gm_src6d57grm686e3, 147, 39);
            uint8_t t9 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t7 * 1));
            gm_taari1s0m51a7 t10 = gm_f_align_of_mks0ipd(t9);
            gm_l3 = t10;
        }
        goto ix3;
ie3:;
ix3:;
    }
    {
        gm_taari1s0m51a7 t11;
        memset(&t11, 0, sizeof t11);
        gm_l4 = t11;
    }
    {
        uint64_t t13 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        uint8_t t14 = (t13 >= ((uint64_t)1ULL));
        if (!t14) goto ie12;
        {
            uint64_t t16 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t17 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            if (t16 >= (uintptr_t)(t17)) gm_trap_index(t16, t17, gm_src6d57grm686e3, 149, 38);
            uint8_t t18 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t16 * 1));
            gm_taari1s0m51a7 t19 = gm_f_align_of_mks0ipd(t18);
            gm_l4 = t19;
        }
        goto ix12;
ie12:;
ix12:;
    }
    {
        if (!((((uint8_t *)&gm_l3))[1] != 0)) goto ie20;
        gm_t9qkh0rsooc8d t21 = GM_LD(gm_t9qkh0rsooc8d, ((uint8_t *)&gm_l3));
        gm_l5 = t21;
        {
            uint8_t t24 = 0;
            uint8_t t25 = 0;
            uint8_t t26 = 0;
            uint64_t t27 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t28 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            if (t27 >= (uintptr_t)(t28)) gm_trap_index(t27, t28, gm_src6d57grm686e3, 151, 12);
            uint8_t t29 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t27 * 1));
            uint8_t t30 = ((t29 == ((uint8_t)123ULL)));
            if (!t30) {
                uint64_t t31 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t32 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                if (t31 >= (uintptr_t)(t32)) gm_trap_index(t31, t32, gm_src6d57grm686e3, 151, 27);
                uint8_t t33 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t31 * 1));
                uint8_t t34 = (t33 >= ((uint8_t)128ULL));
                t26 = t34;
            } else t26 = 1;
            if (!t26) {
                uint64_t t35 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t36 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                if (t35 >= (uintptr_t)(t36)) gm_trap_index(t35, t36, gm_src6d57grm686e3, 151, 42);
                uint8_t t37 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t35 * 1));
                uint8_t t38 = (t37 < ((uint8_t)32ULL));
                t25 = t38;
            } else t25 = 1;
            if (!t25) {
                uint64_t t39 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t40 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                if (t39 >= (uintptr_t)(t40)) gm_trap_index(t39, t40, gm_src6d57grm686e3, 151, 55);
                uint8_t t41 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t39 * 1));
                uint8_t t42 = ((t41 == ((uint8_t)127ULL)));
                t24 = t42;
            } else t24 = 1;
            if (!t24) goto ie23;
            {
                gm_b16_8 t44;
                GM_ST(void *, (uint8_t *)&t44, (void *)(gm_sc4vvo9i5cgel));
                GM_ST(uintptr_t, (uint8_t *)&t44 + 8, (uintptr_t)(85));
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tpu7cfmd3m0cd *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_tpu7cfmd3m0cd *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tpu7cfmd3m0cd *)gm_out) + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_tpu7cfmd3m0cd *)gm_out) + 24, 0);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tpu7cfmd3m0cd *)gm_out) + 0, ((uint8_t)1ULL));
                GM_ST(gm_b16_8, (uint8_t *)&(*(gm_tpu7cfmd3m0cd *)gm_out) + 8, t44);
                goto gm_fdone;
            }
            goto ix23;
ie23:;
ix23:;
        }
        {
            uint64_t t45 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t46 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            if (t45 >= (uintptr_t)(t46)) gm_trap_index(t45, t46, gm_src6d57grm686e3, 154, 21);
            uint8_t t47 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t45 * 1));
            GM_ST(uint8_t, (((uint8_t *)&gm_l1) + 0), t47);
        }
        {
            gm_t9qkh0rsooc8d t48 = gm_l5;
            GM_ST(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l1) + 1), t48);
        }
        {
            gm_l2 = ((uint64_t)2ULL);
        }
        goto ix20;
ie20:;
        if (!((((uint8_t *)&gm_l4))[1] != 0)) goto ie49;
        gm_t9qkh0rsooc8d t50 = GM_LD(gm_t9qkh0rsooc8d, ((uint8_t *)&gm_l4));
        gm_l6 = t50;
        {
            gm_t9qkh0rsooc8d t52 = gm_l6;
            GM_ST(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l1) + 1), t52);
        }
        {
            gm_l2 = ((uint64_t)1ULL);
        }
        goto ix49;
ie49:;
ix49:;
ix20:;
    }
    {
        gm_b16_8 t54 = gm_l0;
        void * t55 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t54)));
        uint64_t t56 = (uintptr_t)GM_LD(uintptr_t, ((uint8_t *)&t54) + 8);
        uint64_t t57 = gm_l2;
        uint64_t t58 = (uintptr_t)t57;
        gm_tqs5p4n2tq19v t59;
        if (t58 < t56) {
        uint8_t t60 = GM_LD(uint8_t, ((uint8_t *)t55 + t58 * 1));
        gm_tqs5p4n2tq19v t61;
        GM_ST(uint8_t, ((uint8_t *)&t61), t60);
        ((uint8_t *)&t61)[1] = 1;
            t59 = t61;
        } else {
        gm_tqs5p4n2tq19v t62;
        memset(&t62, 0, sizeof t62);
            t59 = t62;
        }
        gm_tqs5p4n2tq19v t63;
        GM_ST(uint8_t, ((uint8_t *)&t63), ((uint8_t)43ULL));
        ((uint8_t *)&t63)[1] = 1;
        uint8_t t64 = (gm_eqb44cb3cf1n(((uint8_t *)&t59), ((uint8_t *)&t63)));
        if (!t64) goto ie53;
        {
            GM_ST(uint8_t, (((uint8_t *)&gm_l1) + 2), ((uint8_t)1));
        }
        {
            uint64_t t66 = gm_l2;
            uint64_t r67; bool o68 = __builtin_add_overflow(t66, ((uint64_t)1ULL), &r67);
            if (o68) gm_trap("integer overflow in `+`", gm_src6d57grm686e3, 163, 9);
            uint64_t t69 = r67;
            gm_l2 = t69;
        }
        goto ix53;
ie53:;
ix53:;
    }
    {
        gm_b16_8 t71 = gm_l0;
        void * t72 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t71)));
        uint64_t t73 = (uintptr_t)GM_LD(uintptr_t, ((uint8_t *)&t71) + 8);
        uint64_t t74 = gm_l2;
        uint64_t t75 = (uintptr_t)t74;
        gm_tqs5p4n2tq19v t76;
        if (t75 < t73) {
        uint8_t t77 = GM_LD(uint8_t, ((uint8_t *)t72 + t75 * 1));
        gm_tqs5p4n2tq19v t78;
        GM_ST(uint8_t, ((uint8_t *)&t78), t77);
        ((uint8_t *)&t78)[1] = 1;
            t76 = t78;
        } else {
        gm_tqs5p4n2tq19v t79;
        memset(&t79, 0, sizeof t79);
            t76 = t79;
        }
        gm_tqs5p4n2tq19v t80;
        GM_ST(uint8_t, ((uint8_t *)&t80), ((uint8_t)35ULL));
        ((uint8_t *)&t80)[1] = 1;
        uint8_t t81 = (gm_eqb44cb3cf1n(((uint8_t *)&t76), ((uint8_t *)&t80)));
        if (!t81) goto ie70;
        {
            GM_ST(uint8_t, (((uint8_t *)&gm_l1) + 3), ((uint8_t)1));
        }
        {
            uint64_t t83 = gm_l2;
            uint64_t r84; bool o85 = __builtin_add_overflow(t83, ((uint64_t)1ULL), &r84);
            if (o85) gm_trap("integer overflow in `+`", gm_src6d57grm686e3, 167, 9);
            uint64_t t86 = r84;
            gm_l2 = t86;
        }
        goto ix70;
ie70:;
ix70:;
    }
    {
        gm_b16_8 t88 = gm_l0;
        void * t89 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t88)));
        uint64_t t90 = (uintptr_t)GM_LD(uintptr_t, ((uint8_t *)&t88) + 8);
        uint64_t t91 = gm_l2;
        uint64_t t92 = (uintptr_t)t91;
        gm_tqs5p4n2tq19v t93;
        if (t92 < t90) {
        uint8_t t94 = GM_LD(uint8_t, ((uint8_t *)t89 + t92 * 1));
        gm_tqs5p4n2tq19v t95;
        GM_ST(uint8_t, ((uint8_t *)&t95), t94);
        ((uint8_t *)&t95)[1] = 1;
            t93 = t95;
        } else {
        gm_tqs5p4n2tq19v t96;
        memset(&t96, 0, sizeof t96);
            t93 = t96;
        }
        gm_tqs5p4n2tq19v t97;
        GM_ST(uint8_t, ((uint8_t *)&t97), ((uint8_t)48ULL));
        ((uint8_t *)&t97)[1] = 1;
        uint8_t t98 = (gm_eqb44cb3cf1n(((uint8_t *)&t93), ((uint8_t *)&t97)));
        if (!t98) goto ie87;
        {
            GM_ST(uint8_t, (((uint8_t *)&gm_l1) + 4), ((uint8_t)1));
        }
        {
            uint64_t t100 = gm_l2;
            uint64_t r101; bool o102 = __builtin_add_overflow(t100, ((uint64_t)1ULL), &r101);
            if (o102) gm_trap("integer overflow in `+`", gm_src6d57grm686e3, 171, 9);
            uint64_t t103 = r101;
            gm_l2 = t103;
        }
        goto ix87;
ie87:;
ix87:;
    }
    {
        gm_b16_8 t104 = gm_l0;
        uint64_t t105 = gm_l2;
        gm_f_number_at_esf7qkk((void *)&gm_l7, t104, t105);
    }
    {
        uint8_t t107 = GM_LD(uint8_t, (((uint8_t *)&gm_l7) + 16));
        if (!t107) goto ie106;
        {
            gm_b16_8 t109;
            GM_ST(void *, (uint8_t *)&t109, (void *)(gm_sutmkmalrac6l));
            GM_ST(uintptr_t, (uint8_t *)&t109 + 8, (uintptr_t)(44));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tpu7cfmd3m0cd *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tpu7cfmd3m0cd *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tpu7cfmd3m0cd *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tpu7cfmd3m0cd *)gm_out) + 24, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tpu7cfmd3m0cd *)gm_out) + 0, ((uint8_t)1ULL));
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_tpu7cfmd3m0cd *)gm_out) + 8, t109);
            goto gm_fdone;
        }
        goto ix106;
ie106:;
ix106:;
    }
    {
        if (!(((((uint8_t *)&gm_l7) + 0))[4] != 0)) goto ie110;
        uint32_t t111 = GM_LD(uint32_t, (((uint8_t *)&gm_l7) + 0));
        gm_l8 = t111;
        {
            uint32_t t113 = gm_l8;
            GM_ST(uint32_t, (((uint8_t *)&gm_l1) + 8), t113);
        }
        goto ix110;
ie110:;
ix110:;
    }
    {
        uint64_t t114 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
        gm_l2 = t114;
    }
    {
        gm_b16_8 t116 = gm_l0;
        void * t117 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t116)));
        uint64_t t118 = (uintptr_t)GM_LD(uintptr_t, ((uint8_t *)&t116) + 8);
        uint64_t t119 = gm_l2;
        uint64_t t120 = (uintptr_t)t119;
        gm_tqs5p4n2tq19v t121;
        if (t120 < t118) {
        uint8_t t122 = GM_LD(uint8_t, ((uint8_t *)t117 + t120 * 1));
        gm_tqs5p4n2tq19v t123;
        GM_ST(uint8_t, ((uint8_t *)&t123), t122);
        ((uint8_t *)&t123)[1] = 1;
            t121 = t123;
        } else {
        gm_tqs5p4n2tq19v t124;
        memset(&t124, 0, sizeof t124);
            t121 = t124;
        }
        gm_tqs5p4n2tq19v t125;
        GM_ST(uint8_t, ((uint8_t *)&t125), ((uint8_t)46ULL));
        ((uint8_t *)&t125)[1] = 1;
        uint8_t t126 = (gm_eqb44cb3cf1n(((uint8_t *)&t121), ((uint8_t *)&t125)));
        if (!t126) goto ie115;
        {
            uint64_t t128 = gm_l2;
            uint64_t r129; bool o130 = __builtin_add_overflow(t128, ((uint64_t)1ULL), &r129);
            if (o130) gm_trap("integer overflow in `+`", gm_src6d57grm686e3, 178, 9);
            uint64_t t131 = r129;
            gm_l2 = t131;
        }
        {
            gm_b16_8 t132 = gm_l0;
            uint64_t t133 = gm_l2;
            gm_f_number_at_esf7qkk((void *)&gm_l9, t132, t133);
        }
        {
            uint8_t t135 = GM_LD(uint8_t, (((uint8_t *)&gm_l9) + 16));
            if (!t135) goto ie134;
            {
                gm_b16_8 t137;
                GM_ST(void *, (uint8_t *)&t137, (void *)(gm_sutmkmalrac6l));
                GM_ST(uintptr_t, (uint8_t *)&t137 + 8, (uintptr_t)(44));
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tpu7cfmd3m0cd *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_tpu7cfmd3m0cd *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tpu7cfmd3m0cd *)gm_out) + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_tpu7cfmd3m0cd *)gm_out) + 24, 0);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tpu7cfmd3m0cd *)gm_out) + 0, ((uint8_t)1ULL));
                GM_ST(gm_b16_8, (uint8_t *)&(*(gm_tpu7cfmd3m0cd *)gm_out) + 8, t137);
                goto gm_fdone;
            }
            goto ix134;
ie134:;
ix134:;
        }
        {
            gm_t9805qtonbsag t138 = GM_LD(gm_t9805qtonbsag, (((uint8_t *)&gm_l9) + 0));
            uint32_t t139;
            if (((((uint8_t *)&t138))[4] != 0)) {
                uint32_t t140 = GM_LD(uint32_t, (((uint8_t *)&t138) + 0));
                t139 = t140;
            } else {
                gm_b16_8 t141;
                GM_ST(void *, (uint8_t *)&t141, (void *)(gm_sm35km518bh68));
                GM_ST(uintptr_t, (uint8_t *)&t141 + 8, (uintptr_t)(51));
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tpu7cfmd3m0cd *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_tpu7cfmd3m0cd *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tpu7cfmd3m0cd *)gm_out) + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_tpu7cfmd3m0cd *)gm_out) + 24, 0);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tpu7cfmd3m0cd *)gm_out) + 0, ((uint8_t)1ULL));
                GM_ST(gm_b16_8, (uint8_t *)&(*(gm_tpu7cfmd3m0cd *)gm_out) + 8, t141);
                goto gm_fdone;
            }
            gm_l10 = t139;
        }
        {
            uint32_t t142 = gm_l10;
            gm_t9805qtonbsag t143;
            GM_ST(uint8_t, (uint8_t *)&t143 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t143 + 6, 0);
            GM_ST(uint32_t, ((uint8_t *)&t143), t142);
            ((uint8_t *)&t143)[4] = 1;
            GM_ST(gm_t9805qtonbsag, (((uint8_t *)&gm_l1) + 12), t143);
        }
        {
            uint64_t t144 = GM_LD(uint64_t, (((uint8_t *)&gm_l9) + 8));
            gm_l2 = t144;
        }
        goto ix115;
ie115:;
ix115:;
    }
    {
        gm_b16_8 t146 = gm_l0;
        void * t147 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t146)));
        uint64_t t148 = (uintptr_t)GM_LD(uintptr_t, ((uint8_t *)&t146) + 8);
        uint64_t t149 = gm_l2;
        uint64_t t150 = (uintptr_t)t149;
        gm_tqs5p4n2tq19v t151;
        if (t150 < t148) {
        uint8_t t152 = GM_LD(uint8_t, ((uint8_t *)t147 + t150 * 1));
        gm_tqs5p4n2tq19v t153;
        GM_ST(uint8_t, ((uint8_t *)&t153), t152);
        ((uint8_t *)&t153)[1] = 1;
            t151 = t153;
        } else {
        gm_tqs5p4n2tq19v t154;
        memset(&t154, 0, sizeof t154);
            t151 = t154;
        }
        if (!((((uint8_t *)&t151))[1] != 0)) goto ie145;
        uint8_t t155 = GM_LD(uint8_t, ((uint8_t *)&t151));
        gm_l11 = t155;
        {
            uint8_t t157 = gm_l11;
            gm_t9qkh0rsooc8d t158;
            uint8_t t161 = GM_LD(uint8_t, ((uint8_t *)&t157));
            if (!(t161 == ((uint8_t)120ULL))) goto mn160;
            gm_t9qkh0rsooc8d t162;
            GM_ST(uint8_t, ((uint8_t *)&t162 + 0), ((uint8_t)1ULL));
            t158 = t162;
            goto me159;
mn160:;
            uint8_t t164 = GM_LD(uint8_t, ((uint8_t *)&t157));
            if (!(t164 == ((uint8_t)88ULL))) goto mn163;
            gm_t9qkh0rsooc8d t165;
            GM_ST(uint8_t, ((uint8_t *)&t165 + 0), ((uint8_t)2ULL));
            t158 = t165;
            goto me159;
mn163:;
            uint8_t t167 = GM_LD(uint8_t, ((uint8_t *)&t157));
            if (!(t167 == ((uint8_t)111ULL))) goto mn166;
            gm_t9qkh0rsooc8d t168;
            GM_ST(uint8_t, ((uint8_t *)&t168 + 0), ((uint8_t)3ULL));
            t158 = t168;
            goto me159;
mn166:;
            uint8_t t170 = GM_LD(uint8_t, ((uint8_t *)&t157));
            if (!(t170 == ((uint8_t)98ULL))) goto mn169;
            gm_t9qkh0rsooc8d t171;
            GM_ST(uint8_t, ((uint8_t *)&t171 + 0), ((uint8_t)4ULL));
            t158 = t171;
            goto me159;
mn169:;
            uint8_t t173 = GM_LD(uint8_t, ((uint8_t *)&t157));
            if (!(t173 == ((uint8_t)101ULL))) goto mn172;
            gm_t9qkh0rsooc8d t174;
            GM_ST(uint8_t, ((uint8_t *)&t174 + 0), ((uint8_t)5ULL));
            t158 = t174;
            goto me159;
mn172:;
            uint8_t t176 = GM_LD(uint8_t, ((uint8_t *)&t157));
            if (!(t176 == ((uint8_t)113ULL))) goto mn175;
            gm_t9qkh0rsooc8d t177;
            GM_ST(uint8_t, ((uint8_t *)&t177 + 0), ((uint8_t)6ULL));
            t158 = t177;
            goto me159;
mn175:;
            gm_b16_8 t179 = gm_l0;
            gm_tnpifv20irnph t180;
            GM_ST(gm_b16_8, ((uint8_t *)&t180 + 0), t179);
            gm_tabb710d9mq2e t181;
            gm_f_format_oq31u8d((void *)&t181, (void *)NULL, cx, NULL, t180);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t181))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t181) + 8, gm_src6d57grm686e3, 193, 30);
            gm_b16_8 t182 = GM_LD(gm_b16_8, (((uint8_t *)&t181) + 8));
            gm_b16_8 t183 = GM_LD(gm_b16_8, ((uint8_t *)&t182));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tpu7cfmd3m0cd *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tpu7cfmd3m0cd *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tpu7cfmd3m0cd *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tpu7cfmd3m0cd *)gm_out) + 24, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tpu7cfmd3m0cd *)gm_out) + 0, ((uint8_t)1ULL));
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_tpu7cfmd3m0cd *)gm_out) + 8, t183);
            goto gm_fdone;
            goto me159;
mn178:;
            gm_trap("no match arm matched the scrutinee", gm_src6d57grm686e3, 186, 21);
me159:;
            GM_ST(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l1) + 20), t158);
        }
        {
            uint64_t t184 = gm_l2;
            uint64_t r185; bool o186 = __builtin_add_overflow(t184, ((uint64_t)1ULL), &r185);
            if (o186) gm_trap("integer overflow in `+`", gm_src6d57grm686e3, 195, 9);
            uint64_t t187 = r185;
            gm_l2 = t187;
        }
        goto ix145;
ie145:;
ix145:;
    }
    {
        uint64_t t189 = gm_l2;
        uint64_t t190 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        uint8_t t191 = (!(t189 == t190));
        if (!t191) goto ie188;
        {
            gm_b16_8 t193 = gm_l0;
            gm_tnpifv20irnph t194;
            GM_ST(gm_b16_8, ((uint8_t *)&t194 + 0), t193);
            gm_tabb710d9mq2e t195;
            gm_f_format_f2fgi8v((void *)&t195, (void *)NULL, cx, NULL, t194);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t195))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t195) + 8, gm_src6d57grm686e3, 198, 21);
            gm_b16_8 t196 = GM_LD(gm_b16_8, (((uint8_t *)&t195) + 8));
            gm_b16_8 t197 = GM_LD(gm_b16_8, ((uint8_t *)&t196));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tpu7cfmd3m0cd *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tpu7cfmd3m0cd *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tpu7cfmd3m0cd *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tpu7cfmd3m0cd *)gm_out) + 24, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tpu7cfmd3m0cd *)gm_out) + 0, ((uint8_t)1ULL));
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_tpu7cfmd3m0cd *)gm_out) + 8, t197);
            goto gm_fdone;
        }
        goto ix188;
ie188:;
ix188:;
    }
    {
        uint8_t t199 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 4));
        if (!t199) goto ie198;
        uint8_t t200 = (!(GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 1) + 0) == ((uint8_t)0ULL)));
        if (!t200) goto ie198;
        {
            gm_b16_8 t202;
            GM_ST(void *, (uint8_t *)&t202, (void *)(gm_sjil3d717dq6h));
            GM_ST(uintptr_t, (uint8_t *)&t202 + 8, (uintptr_t)(62));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tpu7cfmd3m0cd *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tpu7cfmd3m0cd *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tpu7cfmd3m0cd *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tpu7cfmd3m0cd *)gm_out) + 24, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tpu7cfmd3m0cd *)gm_out) + 0, ((uint8_t)1ULL));
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_tpu7cfmd3m0cd *)gm_out) + 8, t202);
            goto gm_fdone;
        }
        goto ix198;
ie198:;
ix198:;
    }
    {
        uint8_t t203 = 0;
        uint8_t t204 = 0;
        uint8_t t205 = 0;
        uint8_t t206 = ((GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 20) + 0) == ((uint8_t)1ULL)));
        if (!t206) {
            uint8_t t207 = ((GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 20) + 0) == ((uint8_t)2ULL)));
            t205 = t207;
        } else t205 = 1;
        if (!t205) {
            uint8_t t208 = ((GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 20) + 0) == ((uint8_t)3ULL)));
            t204 = t208;
        } else t204 = 1;
        if (!t204) {
            uint8_t t209 = ((GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 20) + 0) == ((uint8_t)4ULL)));
            t203 = t209;
        } else t203 = 1;
        gm_l12 = t203;
    }
    {
        uint8_t t211 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 3));
        if (!t211) goto ie210;
        uint8_t t212 = gm_l12;
        uint8_t t213 = (uint8_t)!t212;
        if (!t213) goto ie210;
        {
            gm_b16_8 t215;
            GM_ST(void *, (uint8_t *)&t215, (void *)(gm_s8f0u3tjempi4));
            GM_ST(uintptr_t, (uint8_t *)&t215 + 8, (uintptr_t)(52));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tpu7cfmd3m0cd *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tpu7cfmd3m0cd *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tpu7cfmd3m0cd *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tpu7cfmd3m0cd *)gm_out) + 24, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tpu7cfmd3m0cd *)gm_out) + 0, ((uint8_t)1ULL));
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_tpu7cfmd3m0cd *)gm_out) + 8, t215);
            goto gm_fdone;
        }
        goto ix210;
ie210:;
ix210:;
    }
    {
        uint8_t t217;
        if ((((((uint8_t *)&gm_l1) + 12))[4] != 0)) goto mn219;
        t217 = ((uint8_t)0);
        goto me218;
mn219:;
        t217 = ((uint8_t)1);
        goto me218;
mn220:;
        gm_trap("no match arm matched the scrutinee", gm_src6d57grm686e3, 205, 8);
me218:;
        if (!t217) goto ie216;
        uint8_t t221 = (!(GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 20) + 0) == ((uint8_t)0ULL)));
        if (!t221) goto ie216;
        uint8_t t222 = (!(GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 20) + 0) == ((uint8_t)5ULL)));
        if (!t222) goto ie216;
        {
            gm_b16_8 t224;
            GM_ST(void *, (uint8_t *)&t224, (void *)(gm_sfu0in59pituf));
            GM_ST(uintptr_t, (uint8_t *)&t224 + 8, (uintptr_t)(61));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tpu7cfmd3m0cd *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tpu7cfmd3m0cd *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tpu7cfmd3m0cd *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tpu7cfmd3m0cd *)gm_out) + 24, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tpu7cfmd3m0cd *)gm_out) + 0, ((uint8_t)1ULL));
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_tpu7cfmd3m0cd *)gm_out) + 8, t224);
            goto gm_fdone;
        }
        goto ix216;
ie216:;
ix216:;
    }
    {
        const uint8_t *t225 = ((uint8_t *)&gm_l1);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tpu7cfmd3m0cd *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tpu7cfmd3m0cd *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tpu7cfmd3m0cd *)gm_out) + 4, 0);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tpu7cfmd3m0cd *)gm_out) + 0, ((uint8_t)0ULL));
        memmove((uint8_t *)&(*(gm_tpu7cfmd3m0cd *)gm_out) + 8, t225, 24);
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
__attribute__((aligned(4))) void gm_f_parse_template_rlpl2k8(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_parse_template_rlpl2k8 F;
    gm_b16_8 gm_l0; /* t */
    uint64_t gm_l5; /* plain */
    uint64_t gm_l6; /* i */
    uint8_t gm_l7; /* c */
    gm_tqs5p4n2tq19v gm_l8; /* next */
    uint64_t gm_l9; /* j */
    gm_tf5srh4mp4b3i gm_l10; /* idx */
    uint8_t gm_l11; /* <tmp> */
    uint64_t gm_l12; /* start */
    uint64_t gm_l13; /* n */
    uint8_t gm_l14; /* <tmp> */
    gm_tf5srh4mp4b3i gm_l15; /* close */
    uint64_t gm_l16; /* k */
    uint64_t gm_l17; /* end */
    gm_tpu7cfmd3m0cd gm_l18; /* r */
    gm_t7g88i52rrtm5 gm_l19; /* sp */
    gm_b16_8 gm_l20; /* m */
    gm_t7g88i52rrtm5 gm_l21; /* spec */
    uint64_t gm_l22; /* v */
    GM_STACK_CHECK(&F, 1957u);
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
        gm_f_list_qtk22f3((void *)&F.l2, (void *)NULL, cx, NULL);
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
        gm_f_list_5dhgg4i((void *)&F.l4, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_l5 = ((uint64_t)0ULL);
    }
    {
        gm_l6 = ((uint64_t)0ULL);
    }
    {
        for (;;) {
            uint64_t t3 = gm_l6;
            uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            uint8_t t5 = (t3 < t4);
            if (!t5) goto lb2;
            {
                uint64_t t8 = gm_l6;
                uint64_t t9 = (uintptr_t)t8;
                uint64_t t10 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                if (t9 >= (uintptr_t)(t10)) gm_trap_index(t9, t10, gm_src6d57grm686e3, 41, 14);
                uint8_t t11 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t9 * 1));
                gm_l7 = t11;
            }
            {
                gm_tqs5p4n2tq19v t12;
                uint64_t t14 = gm_l6;
                uint64_t r15; bool o16 = __builtin_add_overflow(t14, ((uint64_t)1ULL), &r15);
                if (o16) gm_trap("integer overflow in `+`", gm_src6d57grm686e3, 42, 24);
                uint64_t t17 = r15;
                uint64_t t18 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                uint8_t t19 = (t17 < t18);
                if (!t19) goto ie13;
                uint64_t t21 = gm_l6;
                uint64_t r22; bool o23 = __builtin_add_overflow(t21, ((uint64_t)1ULL), &r22);
                if (o23) gm_trap("integer overflow in `+`", gm_src6d57grm686e3, 42, 42);
                uint64_t t24 = r22;
                uint64_t t25 = (uintptr_t)t24;
                uint64_t t26 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                if (t25 >= (uintptr_t)(t26)) gm_trap_index(t25, t26, gm_src6d57grm686e3, 42, 40);
                uint8_t t27 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t25 * 1));
                uint8_t t28 = t27;
                gm_tqs5p4n2tq19v t29;
                GM_ST(uint8_t, ((uint8_t *)&t29), t28);
                ((uint8_t *)&t29)[1] = 1;
                t12 = t29;
                goto ix13;
ie13:;
                gm_tqs5p4n2tq19v t31;
                memset(&t31, 0, sizeof t31);
                gm_tqs5p4n2tq19v t32 = t31;
                t12 = t32;
ix13:;
                gm_l8 = t12;
            }
            {
                uint8_t t34 = 0;
                uint8_t t35 = 0;
                uint8_t t36 = gm_l7;
                uint8_t t37 = ((t36 == ((uint8_t)123ULL)));
                if (t37) {
                    gm_tqs5p4n2tq19v t38 = gm_l8;
                    gm_tqs5p4n2tq19v t39;
                    GM_ST(uint8_t, ((uint8_t *)&t39), ((uint8_t)123ULL));
                    ((uint8_t *)&t39)[1] = 1;
                    uint8_t t40 = (gm_eqb44cb3cf1n(((uint8_t *)&t38), ((uint8_t *)&t39)));
                    t35 = t40;
                } else t35 = 0;
                if (!t35) {
                    uint8_t t41 = 0;
                    uint8_t t42 = gm_l7;
                    uint8_t t43 = ((t42 == ((uint8_t)125ULL)));
                    if (t43) {
                        gm_tqs5p4n2tq19v t44 = gm_l8;
                        gm_tqs5p4n2tq19v t45;
                        GM_ST(uint8_t, ((uint8_t *)&t45), ((uint8_t)125ULL));
                        ((uint8_t *)&t45)[1] = 1;
                        uint8_t t46 = (gm_eqb44cb3cf1n(((uint8_t *)&t44), ((uint8_t *)&t45)));
                        t41 = t46;
                    } else t41 = 0;
                    t34 = t41;
                } else t34 = 1;
                if (!t34) goto ie33;
                {
                    void * t48 = (void *)((uint8_t *)&F.l3);
                    uint8_t t49 = gm_l7;
                    gm_tftb1odc739t5 t50 = gm_f_push_t83rg2m(t48, t49);
                    if (((((uint8_t *)&t50))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t50) + 1, gm_src6d57grm686e3, 44, 13);
                    gm_b0_1 t51 = GM_LD(gm_b0_1, (((uint8_t *)&t50) + 1));
                }
                {
                    uint64_t t52 = gm_l6;
                    uint64_t r53; bool o54 = __builtin_add_overflow(t52, ((uint64_t)2ULL), &r53);
                    if (o54) gm_trap("integer overflow in `+`", gm_src6d57grm686e3, 45, 13);
                    uint64_t t55 = r53;
                    gm_l6 = t55;
                }
                goto ix33;
ie33:;
                uint8_t t57 = gm_l7;
                uint8_t t58 = ((t57 == ((uint8_t)123ULL)));
                if (!t58) goto ie56;
                {
                    uint64_t t60 = gm_l6;
                    uint64_t r61; bool o62 = __builtin_add_overflow(t60, ((uint64_t)1ULL), &r61);
                    if (o62) gm_trap("integer overflow in `+`", gm_src6d57grm686e3, 48, 18);
                    uint64_t t63 = r61;
                    gm_l9 = t63;
                }
                {
                    gm_tf5srh4mp4b3i t64;
                    memset(&t64, 0, sizeof t64);
                    gm_l10 = t64;
                }
                {
                    uint64_t t66 = gm_l9;
                    uint64_t t67 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                    uint8_t t68 = (t66 < t67);
                    if (!t68) goto ie65;
                    {
                        uint64_t t70 = gm_l9;
                        uint64_t t71 = (uintptr_t)t70;
                        uint64_t t72 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                        if (t71 >= (uintptr_t)(t72)) gm_trap_index(t71, t72, gm_src6d57grm686e3, 50, 30);
                        uint8_t t73 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t71 * 1));
                        gm_l11 = t73;
                    }
                    uint8_t t74 = 0;
                    uint8_t t75 = gm_l11;
                    uint8_t t76 = (((uint8_t)48ULL) <= t75);
                    if (t76) {
                        uint8_t t77 = gm_l11;
                        uint8_t t78 = (t77 <= ((uint8_t)57ULL));
                        t74 = t78;
                    } else t74 = 0;
                    uint8_t t79 = t74;
                    if (!t79) goto ie65;
                    {
                        uint64_t t81 = gm_l9;
                        gm_l12 = t81;
                    }
                    {
                        gm_l13 = ((uint64_t)0ULL);
                    }
                    {
                        for (;;) {
                            uint64_t t83 = gm_l9;
                            uint64_t t84 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                            uint8_t t85 = (t83 < t84);
                            if (!t85) goto lb82;
                            {
                                uint64_t t87 = gm_l9;
                                uint64_t t88 = (uintptr_t)t87;
                                uint64_t t89 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                                if (t88 >= (uintptr_t)(t89)) gm_trap_index(t88, t89, gm_src6d57grm686e3, 53, 37);
                                uint8_t t90 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t88 * 1));
                                gm_l14 = t90;
                            }
                            uint8_t t91 = 0;
                            uint8_t t92 = gm_l14;
                            uint8_t t93 = (((uint8_t)48ULL) <= t92);
                            if (t93) {
                                uint8_t t94 = gm_l14;
                                uint8_t t95 = (t94 <= ((uint8_t)57ULL));
                                t91 = t95;
                            } else t91 = 0;
                            uint8_t t96 = t91;
                            if (!t96) goto lb82;
                            {
                                uint64_t t99 = gm_l13;
                                uint64_t r100; bool o101 = __builtin_mul_overflow(t99, ((uint64_t)10ULL), &r100);
                                if (o101) gm_trap("integer overflow in `*`", gm_src6d57grm686e3, 54, 25);
                                uint64_t t102 = r100;
                                uint64_t t103 = gm_l9;
                                uint64_t t104 = (uintptr_t)t103;
                                uint64_t t105 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                                if (t104 >= (uintptr_t)(t105)) gm_trap_index(t104, t105, gm_src6d57grm686e3, 54, 40);
                                uint8_t t106 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t104 * 1));
                                uint8_t r107; bool o108 = __builtin_sub_overflow(t106, ((uint8_t)48ULL), &r107);
                                if (o108) gm_trap("integer overflow in `-`", gm_src6d57grm686e3, 54, 40);
                                uint8_t t109 = r107;
                                uint64_t t110 = (uint64_t)t109;
                                uint64_t r111; bool o112 = __builtin_add_overflow(t102, t110, &r111);
                                if (o112) gm_trap("integer overflow in `+`", gm_src6d57grm686e3, 54, 25);
                                uint64_t t113 = r111;
                                gm_l13 = t113;
                            }
                            {
                                uint64_t t115 = gm_l13;
                                uint8_t t116 = (t115 > ((uint64_t)65536ULL));
                                if (!t116) goto ie114;
                                {
                                    gm_l13 = ((uint64_t)65537ULL);
                                }
                                goto ix114;
ie114:;
ix114:;
                            }
                            {
                                uint64_t t118 = gm_l9;
                                uint64_t r119; bool o120 = __builtin_add_overflow(t118, ((uint64_t)1ULL), &r119);
                                if (o120) gm_trap("integer overflow in `+`", gm_src6d57grm686e3, 56, 21);
                                uint64_t t121 = r119;
                                gm_l9 = t121;
                            }
lc82:;
                        }
lb82:;
                    }
                    {
                        uint64_t t123 = gm_l12;
                        uint64_t t124 = (uintptr_t)t123;
                        uint64_t t125 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                        if (t124 >= (uintptr_t)(t125)) gm_trap_index(t124, t125, gm_src6d57grm686e3, 58, 20);
                        uint8_t t126 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t124 * 1));
                        uint8_t t127 = ((t126 == ((uint8_t)48ULL)));
                        if (!t127) goto ie122;
                        uint64_t t128 = gm_l9;
                        uint64_t t129 = gm_l12;
                        uint64_t r130; bool o131 = __builtin_sub_overflow(t128, t129, &r130);
                        if (o131) gm_trap("integer overflow in `-`", gm_src6d57grm686e3, 58, 40);
                        uint64_t t132 = r130;
                        uint8_t t133 = (t132 > ((uint64_t)1ULL));
                        if (!t133) goto ie122;
                        {
                            gm_tj64ui6kruni6 t135;
                            gm_b16_8 t136;
                            GM_ST(void *, (uint8_t *)&t136, (void *)(gm_s721l5hj93rdl));
                            GM_ST(uintptr_t, (uint8_t *)&t136 + 8, (uintptr_t)(49));
                            GM_ST(gm_b16_8, (uint8_t *)&t135 + 0, t136);
                            gm_tf5srh4mp4b3i t137;
                            memset(&t137, 0, sizeof t137);
                            GM_ST(gm_tf5srh4mp4b3i, (uint8_t *)&t135 + 16, t137);
                            GM_ST(uint8_t, (uint8_t *)&(*(gm_t1jll27j7ae34 *)gm_out) + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&(*(gm_t1jll27j7ae34 *)gm_out) + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&(*(gm_t1jll27j7ae34 *)gm_out) + 4, 0);
                            GM_ST(uint64_t, (uint8_t *)&(*(gm_t1jll27j7ae34 *)gm_out) + 40, 0);
                            GM_ST(uint64_t, (uint8_t *)&(*(gm_t1jll27j7ae34 *)gm_out) + 48, 0);
                            GM_ST(uint8_t, (uint8_t *)&(*(gm_t1jll27j7ae34 *)gm_out) + 0, ((uint8_t)1ULL));
                            GM_ST(gm_tj64ui6kruni6, (uint8_t *)&(*(gm_t1jll27j7ae34 *)gm_out) + 8, t135);
                            goto gm_fdone;
                        }
                        goto ix122;
ie122:;
ix122:;
                    }
                    {
                        uint64_t t139 = gm_l13;
                        uint8_t t140 = (t139 > ((uint64_t)65536ULL));
                        if (!t140) goto ie138;
                        {
                            gm_tj64ui6kruni6 t142;
                            gm_b16_8 t143;
                            GM_ST(void *, (uint8_t *)&t143, (void *)(gm_sqqi4njr3i0t4));
                            GM_ST(uintptr_t, (uint8_t *)&t143 + 8, (uintptr_t)(45));
                            GM_ST(gm_b16_8, (uint8_t *)&t142 + 0, t143);
                            gm_tf5srh4mp4b3i t144;
                            memset(&t144, 0, sizeof t144);
                            GM_ST(gm_tf5srh4mp4b3i, (uint8_t *)&t142 + 16, t144);
                            GM_ST(uint8_t, (uint8_t *)&(*(gm_t1jll27j7ae34 *)gm_out) + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&(*(gm_t1jll27j7ae34 *)gm_out) + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&(*(gm_t1jll27j7ae34 *)gm_out) + 4, 0);
                            GM_ST(uint64_t, (uint8_t *)&(*(gm_t1jll27j7ae34 *)gm_out) + 40, 0);
                            GM_ST(uint64_t, (uint8_t *)&(*(gm_t1jll27j7ae34 *)gm_out) + 48, 0);
                            GM_ST(uint8_t, (uint8_t *)&(*(gm_t1jll27j7ae34 *)gm_out) + 0, ((uint8_t)1ULL));
                            GM_ST(gm_tj64ui6kruni6, (uint8_t *)&(*(gm_t1jll27j7ae34 *)gm_out) + 8, t142);
                            goto gm_fdone;
                        }
                        goto ix138;
ie138:;
ix138:;
                    }
                    {
                        uint64_t t145 = gm_l13;
                        gm_tf5srh4mp4b3i t146;
                        GM_ST(uint8_t, (uint8_t *)&t146 + 9, 0);
                        GM_ST(uint16_t, (uint8_t *)&t146 + 10, 0);
                        GM_ST(uint32_t, (uint8_t *)&t146 + 12, 0);
                        GM_ST(uint64_t, ((uint8_t *)&t146), t145);
                        ((uint8_t *)&t146)[8] = 1;
                        gm_l10 = t146;
                    }
                    goto ix65;
ie65:;
ix65:;
                }
                {
                    uint64_t t148 = gm_l9;
                    uint64_t t149 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                    uint8_t t150 = (t148 < t149);
                    if (!t150) goto ie147;
                    uint64_t t151 = gm_l9;
                    uint64_t t152 = (uintptr_t)t151;
                    uint64_t t153 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                    if (t152 >= (uintptr_t)(t153)) gm_trap_index(t152, t153, gm_src6d57grm686e3, 66, 30);
                    uint8_t t154 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t152 * 1));
                    uint8_t t155 = ((t154 == ((uint8_t)125ULL)));
                    if (!t155) goto ie147;
                    {
                        void * t157 = (void *)((uint8_t *)&F.l1);
                        gm_b16_8 t158 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
                        gm_b16_8 t159 = GM_LD(gm_b16_8, ((uint8_t *)&t158));
                        gm_tftb1odc739t5 t160 = gm_f_push_3ef746b(t157, t159);
                        if (((((uint8_t *)&t160))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t160) + 1, gm_src6d57grm686e3, 67, 17);
                        gm_b0_1 t161 = GM_LD(gm_b0_1, (((uint8_t *)&t160) + 1));
                    }
                    {
                        gm_f_list_uvfu1k1((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l3)), (void *)NULL, cx, NULL);
                        if (gm_unw.kind) {
                            goto gm_ll6;
                        }
                    }
                    {
                        void * t162 = (void *)((uint8_t *)&F.l2);
                        gm_t7g88i52rrtm5 t163;
                        gm_f_default_spec_9ntjq2s((void *)&t163);
                        gm_tftb1odc739t5 t164 = gm_f_push_nbmosph(t162, &t163);
                        if (((((uint8_t *)&t164))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t164) + 1, gm_src6d57grm686e3, 69, 17);
                        gm_b0_1 t165 = GM_LD(gm_b0_1, (((uint8_t *)&t164) + 1));
                    }
                    {
                        uint64_t t166 = gm_l9;
                        uint64_t r167; bool o168 = __builtin_add_overflow(t166, ((uint64_t)1ULL), &r167);
                        if (o168) gm_trap("integer overflow in `+`", gm_src6d57grm686e3, 70, 21);
                        uint64_t t169 = r167;
                        gm_l6 = t169;
                    }
                    goto ix147;
ie147:;
                    uint64_t t171 = gm_l9;
                    uint64_t t172 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                    uint8_t t173 = (t171 < t172);
                    if (!t173) goto ie170;
                    uint64_t t174 = gm_l9;
                    uint64_t t175 = (uintptr_t)t174;
                    uint64_t t176 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                    if (t175 >= (uintptr_t)(t176)) gm_trap_index(t175, t176, gm_src6d57grm686e3, 71, 37);
                    uint8_t t177 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t175 * 1));
                    uint8_t t178 = ((t177 == ((uint8_t)58ULL)));
                    if (!t178) goto ie170;
                    {
                        gm_tf5srh4mp4b3i t180;
                        memset(&t180, 0, sizeof t180);
                        gm_l15 = t180;
                    }
                    {
                        uint64_t t181 = gm_l9;
                        uint64_t r182; bool o183 = __builtin_add_overflow(t181, ((uint64_t)1ULL), &r182);
                        if (o183) gm_trap("integer overflow in `+`", gm_src6d57grm686e3, 73, 22);
                        uint64_t t184 = r182;
                        gm_l16 = t184;
                    }
                    {
                        for (;;) {
                            uint64_t t186 = gm_l16;
                            uint64_t t187 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                            uint8_t t188 = (t186 < t187);
                            if (!t188) goto lb185;
                            {
                                uint64_t t192 = gm_l16;
                                uint64_t t193 = (uintptr_t)t192;
                                uint64_t t194 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                                if (t193 >= (uintptr_t)(t194)) gm_trap_index(t193, t194, gm_src6d57grm686e3, 75, 24);
                                uint8_t t195 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t193 * 1));
                                uint8_t t196 = ((t195 == ((uint8_t)125ULL)));
                                if (!t196) goto ie191;
                                {
                                    uint64_t t198 = gm_l16;
                                    gm_tf5srh4mp4b3i t199;
                                    GM_ST(uint8_t, (uint8_t *)&t199 + 9, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t199 + 10, 0);
                                    GM_ST(uint32_t, (uint8_t *)&t199 + 12, 0);
                                    GM_ST(uint64_t, ((uint8_t *)&t199), t198);
                                    ((uint8_t *)&t199)[8] = 1;
                                    gm_l15 = t199;
                                }
                                {
                                    goto lb185;
                                }
                                goto ix191;
ie191:;
ix191:;
                            }
                            {
                                uint64_t t200 = gm_l16;
                                uint64_t r201; bool o202 = __builtin_add_overflow(t200, ((uint64_t)1ULL), &r201);
                                if (o202) gm_trap("integer overflow in `+`", gm_src6d57grm686e3, 79, 21);
                                uint64_t t203 = r201;
                                gm_l16 = t203;
                            }
lc185:;
                        }
lb185:;
                    }
                    {
                        gm_tf5srh4mp4b3i t204 = gm_l15;
                        uint64_t t205;
                        if (((((uint8_t *)&t204))[8] != 0)) {
                            uint64_t t206 = GM_LD(uint64_t, (((uint8_t *)&t204) + 0));
                            t205 = t206;
                        } else {
                            gm_tj64ui6kruni6 t207;
                            gm_b16_8 t208;
                            GM_ST(void *, (uint8_t *)&t208, (void *)(gm_sl9bj93djc5da));
                            GM_ST(uintptr_t, (uint8_t *)&t208 + 8, (uintptr_t)(37));
                            GM_ST(gm_b16_8, (uint8_t *)&t207 + 0, t208);
                            gm_tf5srh4mp4b3i t209;
                            memset(&t209, 0, sizeof t209);
                            GM_ST(gm_tf5srh4mp4b3i, (uint8_t *)&t207 + 16, t209);
                            GM_ST(uint8_t, (uint8_t *)&(*(gm_t1jll27j7ae34 *)gm_out) + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&(*(gm_t1jll27j7ae34 *)gm_out) + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&(*(gm_t1jll27j7ae34 *)gm_out) + 4, 0);
                            GM_ST(uint64_t, (uint8_t *)&(*(gm_t1jll27j7ae34 *)gm_out) + 40, 0);
                            GM_ST(uint64_t, (uint8_t *)&(*(gm_t1jll27j7ae34 *)gm_out) + 48, 0);
                            GM_ST(uint8_t, (uint8_t *)&(*(gm_t1jll27j7ae34 *)gm_out) + 0, ((uint8_t)1ULL));
                            GM_ST(gm_tj64ui6kruni6, (uint8_t *)&(*(gm_t1jll27j7ae34 *)gm_out) + 8, t207);
                            goto gm_fdone;
                        }
                        gm_l17 = t205;
                    }
                    {
                        uint64_t t210 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                        void * t211 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)));
                        uint64_t t212 = gm_l9;
                        uint64_t r213; bool o214 = __builtin_add_overflow(t212, ((uint64_t)1ULL), &r213);
                        if (o214) gm_trap("integer overflow in `+`", gm_src6d57grm686e3, 82, 35);
                        uint64_t t215 = r213;
                        uint64_t t216 = (uintptr_t)t215;
                        uint64_t t217 = gm_l17;
                        uint64_t t218 = (uintptr_t)t217;
                        if (t216 > t218 || t218 > (uintptr_t)t210) gm_trap_slice(t216, t218, t210, gm_src6d57grm686e3, 82, 33);
                        gm_b16_8 t219;
                        GM_ST(void *, (uint8_t *)&t219, (void *)(((uint8_t *)t211 + t216 * 1)));
                        GM_ST(uintptr_t, (uint8_t *)&t219 + 8, (uintptr_t)(t218 - t216));
                        gm_f_parse_spec_iod83cp((void *)&gm_l18, (void *)NULL, cx, NULL, t219);
                        if (gm_unw.kind) {
                            goto gm_ll6;
                        }
                    }
                    {
                        gm_t7g88i52rrtm5 t220;
                        uint8_t t223 = GM_LD(uint8_t, (((uint8_t *)&gm_l18) + 0));
                        if (t223 != ((uint8_t)0ULL)) goto mn222;
                        gm_t7g88i52rrtm5 t224 = GM_LD(gm_t7g88i52rrtm5, (((uint8_t *)&gm_l18) + 8));
                        gm_l19 = t224;
                        gm_t7g88i52rrtm5 t225 = gm_l19;
                        t220 = t225;
                        goto me221;
mn222:;
                        uint8_t t227 = GM_LD(uint8_t, (((uint8_t *)&gm_l18) + 0));
                        if (t227 != ((uint8_t)1ULL)) goto mn226;
                        gm_b16_8 t228 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l18) + 8));
                        gm_l20 = t228;
                        gm_tj64ui6kruni6 t229;
                        gm_b16_8 t230 = gm_l20;
                        GM_ST(gm_b16_8, (uint8_t *)&t229 + 0, t230);
                        gm_tf5srh4mp4b3i t231;
                        memset(&t231, 0, sizeof t231);
                        GM_ST(gm_tf5srh4mp4b3i, (uint8_t *)&t229 + 16, t231);
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_t1jll27j7ae34 *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_t1jll27j7ae34 *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_t1jll27j7ae34 *)gm_out) + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_t1jll27j7ae34 *)gm_out) + 40, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_t1jll27j7ae34 *)gm_out) + 48, 0);
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_t1jll27j7ae34 *)gm_out) + 0, ((uint8_t)1ULL));
                        GM_ST(gm_tj64ui6kruni6, (uint8_t *)&(*(gm_t1jll27j7ae34 *)gm_out) + 8, t229);
                        goto gm_fdone;
                        goto me221;
mn226:;
                        gm_trap("no match arm matched the scrutinee", gm_src6d57grm686e3, 83, 25);
me221:;
                        gm_l21 = t220;
                    }
                    {
                        void * t232 = (void *)((uint8_t *)&F.l1);
                        gm_b16_8 t233 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
                        gm_b16_8 t234 = GM_LD(gm_b16_8, ((uint8_t *)&t233));
                        gm_tftb1odc739t5 t235 = gm_f_push_3ef746b(t232, t234);
                        if (((((uint8_t *)&t235))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t235) + 1, gm_src6d57grm686e3, 87, 17);
                        gm_b0_1 t236 = GM_LD(gm_b0_1, (((uint8_t *)&t235) + 1));
                    }
                    {
                        gm_f_list_uvfu1k1((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l3)), (void *)NULL, cx, NULL);
                        if (gm_unw.kind) {
                            goto gm_ll6;
                        }
                    }
                    {
                        void * t237 = (void *)((uint8_t *)&F.l2);
                        gm_tftb1odc739t5 t238 = gm_f_push_nbmosph(t237, &(*(gm_t7g88i52rrtm5 *)((uint8_t *)&gm_l21)));
                        if (((((uint8_t *)&t238))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t238) + 1, gm_src6d57grm686e3, 89, 17);
                        gm_b0_1 t239 = GM_LD(gm_b0_1, (((uint8_t *)&t238) + 1));
                    }
                    {
                        uint64_t t240 = gm_l17;
                        uint64_t r241; bool o242 = __builtin_add_overflow(t240, ((uint64_t)1ULL), &r241);
                        if (o242) gm_trap("integer overflow in `+`", gm_src6d57grm686e3, 90, 21);
                        uint64_t t243 = r241;
                        gm_l6 = t243;
                    }
                    goto ix170;
ie170:;
                    {
                        gm_tj64ui6kruni6 t245;
                        gm_b16_8 t246;
                        GM_ST(void *, (uint8_t *)&t246, (void *)(gm_s4k55k154s62m));
                        GM_ST(uintptr_t, (uint8_t *)&t246 + 8, (uintptr_t)(119));
                        GM_ST(gm_b16_8, (uint8_t *)&t245 + 0, t246);
                        gm_tf5srh4mp4b3i t247;
                        memset(&t247, 0, sizeof t247);
                        GM_ST(gm_tf5srh4mp4b3i, (uint8_t *)&t245 + 16, t247);
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_t1jll27j7ae34 *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_t1jll27j7ae34 *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_t1jll27j7ae34 *)gm_out) + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_t1jll27j7ae34 *)gm_out) + 40, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_t1jll27j7ae34 *)gm_out) + 48, 0);
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_t1jll27j7ae34 *)gm_out) + 0, ((uint8_t)1ULL));
                        GM_ST(gm_tj64ui6kruni6, (uint8_t *)&(*(gm_t1jll27j7ae34 *)gm_out) + 8, t245);
                        goto gm_fdone;
                    }
ix170:;
ix147:;
                }
                {
                    if (!((((uint8_t *)&gm_l10))[8] != 0)) goto ie248;
                    uint64_t t249 = GM_LD(uint64_t, ((uint8_t *)&gm_l10));
                    gm_l22 = t249;
                    {
                        void * t251 = (void *)((uint8_t *)&F.l4);
                        uint64_t t252 = gm_l22;
                        gm_tftb1odc739t5 t253 = gm_f_push_ucgmlo2(t251, t252);
                        if (((((uint8_t *)&t253))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t253) + 1, gm_src6d57grm686e3, 95, 17);
                        gm_b0_1 t254 = GM_LD(gm_b0_1, (((uint8_t *)&t253) + 1));
                    }
                    goto ix248;
ie248:;
                    {
                        uint64_t t256 = gm_l5;
                        uint64_t r257; bool o258 = __builtin_add_overflow(t256, ((uint64_t)1ULL), &r257);
                        if (o258) gm_trap("integer overflow in `+`", gm_src6d57grm686e3, 97, 17);
                        uint64_t t259 = r257;
                        gm_l5 = t259;
                    }
ix248:;
                }
                goto ix56;
ie56:;
                uint8_t t261 = gm_l7;
                uint8_t t262 = ((t261 == ((uint8_t)125ULL)));
                if (!t262) goto ie260;
                {
                    gm_tj64ui6kruni6 t264;
                    gm_b16_8 t265;
                    GM_ST(void *, (uint8_t *)&t265, (void *)(gm_smvgcg1j21t6v));
                    GM_ST(uintptr_t, (uint8_t *)&t265 + 8, (uintptr_t)(26));
                    GM_ST(gm_b16_8, (uint8_t *)&t264 + 0, t265);
                    uint64_t t266 = gm_l6;
                    gm_tf5srh4mp4b3i t267;
                    GM_ST(uint8_t, (uint8_t *)&t267 + 9, 0);
                    GM_ST(uint16_t, (uint8_t *)&t267 + 10, 0);
                    GM_ST(uint32_t, (uint8_t *)&t267 + 12, 0);
                    GM_ST(uint64_t, ((uint8_t *)&t267), t266);
                    ((uint8_t *)&t267)[8] = 1;
                    GM_ST(gm_tf5srh4mp4b3i, (uint8_t *)&t264 + 16, t267);
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_t1jll27j7ae34 *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_t1jll27j7ae34 *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_t1jll27j7ae34 *)gm_out) + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_t1jll27j7ae34 *)gm_out) + 40, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_t1jll27j7ae34 *)gm_out) + 48, 0);
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_t1jll27j7ae34 *)gm_out) + 0, ((uint8_t)1ULL));
                    GM_ST(gm_tj64ui6kruni6, (uint8_t *)&(*(gm_t1jll27j7ae34 *)gm_out) + 8, t264);
                    goto gm_fdone;
                }
                goto ix260;
ie260:;
                {
                    void * t269 = (void *)((uint8_t *)&F.l3);
                    uint8_t t270 = gm_l7;
                    gm_tftb1odc739t5 t271 = gm_f_push_t83rg2m(t269, t270);
                    if (((((uint8_t *)&t271))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t271) + 1, gm_src6d57grm686e3, 102, 13);
                    gm_b0_1 t272 = GM_LD(gm_b0_1, (((uint8_t *)&t271) + 1));
                }
                {
                    uint64_t t273 = gm_l6;
                    uint64_t r274; bool o275 = __builtin_add_overflow(t273, ((uint64_t)1ULL), &r274);
                    if (o275) gm_trap("integer overflow in `+`", gm_src6d57grm686e3, 103, 13);
                    uint64_t t276 = r274;
                    gm_l6 = t276;
                }
ix260:;
ix56:;
ix33:;
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
        void * t277 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t278 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
        gm_b16_8 t279 = GM_LD(gm_b16_8, ((uint8_t *)&t278));
        gm_tftb1odc739t5 t280 = gm_f_push_3ef746b(t277, t279);
        if (((((uint8_t *)&t280))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t280) + 1, gm_src6d57grm686e3, 106, 5);
        gm_b0_1 t281 = GM_LD(gm_b0_1, (((uint8_t *)&t280) + 1));
    }
    {
        uint64_t t283 = gm_l5;
        uint8_t t284 = (t283 > ((uint64_t)0ULL));
        if (!t284) goto ie282;
        uint64_t t285 = GM_LD(uintptr_t, (((uint8_t *)&F.l4) + 0) + 8);
        uint8_t t286 = (t285 > ((uint64_t)0ULL));
        if (!t286) goto ie282;
        {
            gm_tj64ui6kruni6 t288;
            gm_b16_8 t289;
            GM_ST(void *, (uint8_t *)&t289, (void *)(gm_sppcm1qavsp1k));
            GM_ST(uintptr_t, (uint8_t *)&t289 + 8, (uintptr_t)(79));
            GM_ST(gm_b16_8, (uint8_t *)&t288 + 0, t289);
            gm_tf5srh4mp4b3i t290;
            memset(&t290, 0, sizeof t290);
            GM_ST(gm_tf5srh4mp4b3i, (uint8_t *)&t288 + 16, t290);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t1jll27j7ae34 *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t1jll27j7ae34 *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t1jll27j7ae34 *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t1jll27j7ae34 *)gm_out) + 40, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t1jll27j7ae34 *)gm_out) + 48, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t1jll27j7ae34 *)gm_out) + 0, ((uint8_t)1ULL));
            GM_ST(gm_tj64ui6kruni6, (uint8_t *)&(*(gm_t1jll27j7ae34 *)gm_out) + 8, t288);
            goto gm_fdone;
        }
        goto ix282;
ie282:;
ix282:;
    }
    {
        gm_tfeab279c16rr t291;
        gm_b16_8 t292 = GM_LD(gm_b16_8, (((uint8_t *)&F.l1) + 0));
        gm_b16_8 t293 = GM_LD(gm_b16_8, ((uint8_t *)&t292));
        GM_ST(gm_b16_8, (uint8_t *)&t291 + 0, t293);
        gm_b16_8 t294 = GM_LD(gm_b16_8, (((uint8_t *)&F.l2) + 0));
        gm_b16_8 t295 = GM_LD(gm_b16_8, ((uint8_t *)&t294));
        GM_ST(gm_b16_8, (uint8_t *)&t291 + 16, t295);
        gm_b16_8 t296 = GM_LD(gm_b16_8, (((uint8_t *)&F.l4) + 0));
        gm_b16_8 t297 = GM_LD(gm_b16_8, ((uint8_t *)&t296));
        GM_ST(gm_b16_8, (uint8_t *)&t291 + 32, t297);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t1jll27j7ae34 *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t1jll27j7ae34 *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t1jll27j7ae34 *)gm_out) + 4, 0);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t1jll27j7ae34 *)gm_out) + 0, ((uint8_t)0ULL));
        GM_ST(gm_tfeab279c16rr, (uint8_t *)&(*(gm_t1jll27j7ae34 *)gm_out) + 8, t291);
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
extern __attribute__((aligned(4))) gm_b16_8 gm_f_placeholders_text_rtvia6b(void *env, gm_node *cx, gm_node *inst, uint64_t a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_resolve_write_e97fjom(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2) {
    struct gm_fr_resolve_write_e97fjom F;
    void * gm_l0; /* s */
    void * gm_l1; /* mc */
    void * gm_l2; /* e */
    void * gm_l3; /* w */
    gm_tl874t2atnlh3 gm_l4; /* span */
    void * gm_l5; /* tys */
    gm_b16_8 gm_l6; /* text */
    gm_tj64ui6kruni6 gm_l8; /* err */
    uint64_t gm_l10; /* n */
    gm_th2lqbo77pvsc gm_l11; /* elems */
    void * gm_l12; /* a */
    gm_b16_8 gm_l13; /* es */
    void * gm_l14; /* a */
    gm_b16_8 gm_l15; /* es */
    uint8_t gm_l16; /* numbered */
    gm_b16_8 gm_l17; /* msg */
    gm_b16_8 gm_l18; /* given */
    uint64_t gm_l19; /* <tmp> */
    uint64_t gm_l20; /* <tmp> */
    uint64_t gm_l21; /* k */
    uint64_t gm_l22; /* at */
    gm_b16_8 gm_l23; /* msg */
    void * gm_l24; /* a */
    uint64_t gm_l27; /* <tmp> */
    uint64_t gm_l28; /* <tmp> */
    uint64_t gm_l29; /* i */
    uint64_t gm_l30; /* at */
    void * gm_l31; /* base */
    void * gm_l32; /* write */
    GM_STACK_CHECK(&F, 3168u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t3 = gm_l2;
        uint8_t t4 = GM_LD(uint8_t, ((((uint8_t *)t3) + 8) + 0));
        if (t4 != ((uint8_t)56ULL)) goto ie2;
        void * t5 = GM_LD(void *, ((((uint8_t *)t3) + 8) + 8));
        gm_l3 = t5;
        {
            void * t7 = gm_l1;
            gm_tl874t2atnlh3 t8 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t7) + 96));
            gm_l4 = t8;
        }
        {
            void * t9 = gm_l0;
            void * t10 = (void *)(((uint8_t *)t9) + 8);
            gm_l5 = t10;
        }
        {
            void * t11 = gm_l2;
            uint8_t *t12 = (((uint8_t *)t11) + 8);
            gm_b16_8 t13 = gm_f_empty_slice_g3tddrb_2();
            memset(&(*(gm_teti411eujvf4 *)t12), 0, sizeof (*(gm_teti411eujvf4 *)t12));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_teti411eujvf4 *)t12) + 0, ((uint8_t)35ULL));
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_teti411eujvf4 *)t12) + 8, t13);
        }
        {
            void * t15 = gm_l3;
            void * t16 = GM_LD(void *, (((uint8_t *)t15) + 40));
            uint8_t t17 = GM_LD(uint8_t, ((((uint8_t *)t16) + 8) + 0));
            if (t17 != ((uint8_t)5ULL)) goto ie14;
            gm_b16_8 t18 = GM_LD(gm_b16_8, ((((uint8_t *)t16) + 8) + 8));
            gm_l6 = t18;
            {
                gm_b16_8 t20 = gm_l6;
                gm_t1jll27j7ae34 t21;
                gm_f_parse_template_rlpl2k8((void *)&t21, (void *)NULL, cx, NULL, t20);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tfeab279c16rr t22;
                uint8_t t25 = GM_LD(uint8_t, (((uint8_t *)&t21) + 0));
                if (t25 != ((uint8_t)0ULL)) goto mn24;
                F.l7 = (*(gm_tfeab279c16rr *)(((uint8_t *)&t21) + 8));
                gm_tfeab279c16rr t26 = F.l7;
                t22 = t26;
                goto me23;
mn24:;
                uint8_t t28 = GM_LD(uint8_t, (((uint8_t *)&t21) + 0));
                if (t28 != ((uint8_t)1ULL)) goto mn27;
                gm_l8 = (*(gm_tj64ui6kruni6 *)(((uint8_t *)&t21) + 8));
                {
                    void * t30 = gm_l0;
                    gm_tl874t2atnlh3 t31 = gm_l4;
                    gm_b16_8 t32 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l8) + 0));
                    gm_tnpifv20irnph t33;
                    GM_ST(gm_b16_8, ((uint8_t *)&t33 + 0), t32);
                    gm_tabb710d9mq2e t34;
                    gm_f_format_jvf1ma2((void *)&t34, (void *)NULL, cx, NULL, t33);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    if (((((uint8_t *)&t34))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t34) + 8, gm_src6d57grm686e3, 492, 27);
                    gm_b16_8 t35 = GM_LD(gm_b16_8, (((uint8_t *)&t34) + 8));
                    gm_b16_8 t36 = GM_LD(gm_b16_8, ((uint8_t *)&t35));
                    gm_b0_1 t37 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t30, t31, t36);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                gm_b0_1 t38;
                uint8_t t39 = 0;
                F.ret = t38;
                goto gm_fret;
                goto me23;
mn27:;
                gm_trap("no match arm matched the scrutinee", gm_src6d57grm686e3, 489, 15);
me23:;
                F.l9 = t22;
            }
            {
                uint64_t t40 = GM_LD(uintptr_t, (((uint8_t *)&F.l9) + 16) + 8);
                gm_l10 = t40;
            }
            {
                gm_b16_8 t41 = gm_f_empty_slice_rcd2jln();
                gm_b16_8 t42 = GM_LD(gm_b16_8, ((uint8_t *)&t41));
                GM_ST(uint8_t, (uint8_t *)&gm_l11 + 17, 0);
                GM_ST(uint16_t, (uint8_t *)&gm_l11 + 18, 0);
                GM_ST(uint32_t, (uint8_t *)&gm_l11 + 20, 0);
                ((uint8_t *)&gm_l11)[16] = 1;
                GM_ST(gm_b16_8, (uint8_t *)&gm_l11 + 0, t42);
            }
            {
                void * t44 = gm_l3;
                void * t45 = GM_LD(void *, (((uint8_t *)t44) + 48));
                if (!(GM_LD(void *, ((uint8_t *)&t45)) != 0)) goto ie43;
                void * t46 = GM_LD(void *, ((uint8_t *)&t45));
                gm_l12 = t46;
                {
                    gm_th2lqbo77pvsc t48;
                    memset(&t48, 0, sizeof t48);
                    gm_l11 = t48;
                }
                {
                    void * t50 = gm_l5;
                    void * t51 = gm_l12;
                    gm_t88rcucg1b7uk t52 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t51) + 0));
                    gm_t53at80pfdus3 t53;
                    gm_f_kind_jcnt4hm((void *)&t53, t50, GM_CONV(gm_csguankko4lmhd, t52));
                    uint8_t t54 = GM_LD(uint8_t, (((uint8_t *)&t53) + 0));
                    if (t54 != ((uint8_t)12ULL)) goto ie49;
                    gm_b16_8 t55 = GM_LD(gm_b16_8, ((((uint8_t *)&t53) + 8) + 0));
                    gm_l13 = t55;
                    {
                        gm_b16_8 t57 = gm_l13;
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l11)) + 17, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l11)) + 18, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l11)) + 20, 0);
                        ((uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l11)))[16] = 1;
                        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l11)) + 0, t57);
                    }
                    goto ix49;
ie49:;
ix49:;
                }
                goto ix43;
ie43:;
ix43:;
            }
            {
                gm_th2lqbo77pvsc t58 = gm_l11;
                gm_b16_8 t59;
                if (((((uint8_t *)&t58))[16] != 0)) {
                    gm_b16_8 t60 = GM_LD(gm_b16_8, (((uint8_t *)&t58) + 0));
                    t59 = t60;
                } else {
                    {
                        void * t62 = gm_l3;
                        void * t63 = GM_LD(void *, (((uint8_t *)t62) + 48));
                        void * t64;
                        if ((GM_LD(void *, ((uint8_t *)&t63)) != 0)) {
                            void * t65 = GM_LD(void *, (((uint8_t *)&t63) + 0));
                            t64 = t65;
                        } else {
                            gm_b0_1 t66;
                            uint8_t t67 = 0;
                            F.ret = t66;
                            goto gm_fret;
                        }
                        gm_l14 = t64;
                    }
                    {
                        void * t68 = gm_l0;
                        gm_tl874t2atnlh3 t69 = gm_l4;
                        gm_t6j6smj7qoctu t70;
                        uint64_t t71 = gm_l10;
                        gm_b16_8 t72 = gm_f_placeholders_text_rtvia6b((void *)NULL, cx, NULL, t71);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        GM_ST(gm_b16_8, (uint8_t *)&t70 + 0, t72);
                        void * t73 = gm_l0;
                        void * t74 = gm_l14;
                        gm_t88rcucg1b7uk t75 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t74) + 0));
                        gm_b16_8 t76 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t73, t75);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        GM_ST(gm_b16_8, (uint8_t *)&t70 + 16, t76);
                        gm_tabb710d9mq2e t77;
                        gm_f_format_cnhv2id((void *)&t77, (void *)NULL, cx, NULL, &t70);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        if (((((uint8_t *)&t77))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t77) + 8, gm_src6d57grm686e3, 505, 23);
                        gm_b16_8 t78 = GM_LD(gm_b16_8, (((uint8_t *)&t77) + 8));
                        gm_b16_8 t79 = GM_LD(gm_b16_8, ((uint8_t *)&t78));
                        gm_b0_1 t80 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t68, t69, t79);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    gm_b0_1 t81;
                    uint8_t t82 = 0;
                    F.ret = t81;
                    goto gm_fret;
                }
                gm_l15 = t59;
            }
            {
                uint64_t t83 = GM_LD(uintptr_t, (((uint8_t *)&F.l9) + 32) + 8);
                uint8_t t84 = (t83 > ((uint64_t)0ULL));
                gm_l16 = t84;
            }
            {
                uint8_t t86 = gm_l16;
                if (!t86) goto ie85;
                {
                    uint64_t t89 = GM_LD(uintptr_t, ((uint8_t *)&gm_l15) + 8);
                    gm_th2lqbo77pvsc t90;
                    gm_f_numbered_error_07hll20((void *)&t90, (void *)NULL, cx, NULL, &(*(gm_tfeab279c16rr *)((uint8_t *)&F.l9)), t89);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    if (!((((uint8_t *)&t90))[16] != 0)) goto ie88;
                    gm_b16_8 t91 = GM_LD(gm_b16_8, ((uint8_t *)&t90));
                    gm_l17 = t91;
                    {
                        void * t93 = gm_l0;
                        gm_tl874t2atnlh3 t94 = gm_l4;
                        gm_b16_8 t95 = gm_l17;
                        gm_tnpifv20irnph t96;
                        GM_ST(gm_b16_8, ((uint8_t *)&t96 + 0), t95);
                        gm_tabb710d9mq2e t97;
                        gm_f_format_mlqiori((void *)&t97, (void *)NULL, cx, NULL, t96);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        if (((((uint8_t *)&t97))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t97) + 8, gm_src6d57grm686e3, 511, 27);
                        gm_b16_8 t98 = GM_LD(gm_b16_8, (((uint8_t *)&t97) + 8));
                        gm_b16_8 t99 = GM_LD(gm_b16_8, ((uint8_t *)&t98));
                        gm_b0_1 t100 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t93, t94, t99);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    {
                        gm_b0_1 t101;
                        uint8_t t102 = 0;
                        F.ret = t101;
                        goto gm_fret;
                    }
                    goto ix88;
ie88:;
ix88:;
                }
                goto ix85;
ie85:;
                uint64_t t104 = GM_LD(uintptr_t, ((uint8_t *)&gm_l15) + 8);
                uint64_t t105 = gm_l10;
                uint8_t t106 = (!(t104 == t105));
                if (!t106) goto ie103;
                {
                    gm_b16_8 t108;
                    uint64_t t110 = GM_LD(uintptr_t, ((uint8_t *)&gm_l15) + 8);
                    uint8_t t111 = ((t110 == ((uint64_t)1ULL)));
                    if (!t111) goto ie109;
                    gm_b16_8 t113;
                    GM_ST(void *, (uint8_t *)&t113, (void *)(gm_scne9acm49p4h));
                    GM_ST(uintptr_t, (uint8_t *)&t113 + 8, (uintptr_t)(10));
                    gm_b16_8 t114 = t113;
                    t108 = t114;
                    goto ix109;
ie109:;
                    uint64_t t116 = GM_LD(uintptr_t, ((uint8_t *)&gm_l15) + 8);
                    gm_t4ce0jd3tdro4 t117;
                    GM_ST(uint64_t, ((uint8_t *)&t117 + 0), t116);
                    gm_tabb710d9mq2e t118;
                    gm_f_format_td6patj((void *)&t118, (void *)NULL, cx, NULL, t117);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    if (((((uint8_t *)&t118))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t118) + 8, gm_src6d57grm686e3, 515, 57);
                    gm_b16_8 t119 = GM_LD(gm_b16_8, (((uint8_t *)&t118) + 8));
                    gm_b16_8 t120 = t119;
                    gm_b16_8 t121 = GM_LD(gm_b16_8, ((uint8_t *)&t120));
                    t108 = t121;
ix109:;
                    gm_l18 = t108;
                }
                {
                    void * t122 = gm_l0;
                    gm_tl874t2atnlh3 t123 = gm_l4;
                    gm_t6j6smj7qoctu t124;
                    uint64_t t125 = gm_l10;
                    gm_b16_8 t126 = gm_f_placeholders_text_rtvia6b((void *)NULL, cx, NULL, t125);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    GM_ST(gm_b16_8, (uint8_t *)&t124 + 0, t126);
                    gm_b16_8 t127 = gm_l18;
                    GM_ST(gm_b16_8, (uint8_t *)&t124 + 16, t127);
                    gm_tabb710d9mq2e t128;
                    gm_f_format_2rf8n5r((void *)&t128, (void *)NULL, cx, NULL, &t124);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    if (((((uint8_t *)&t128))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t128) + 8, gm_src6d57grm686e3, 516, 23);
                    gm_b16_8 t129 = GM_LD(gm_b16_8, (((uint8_t *)&t128) + 8));
                    gm_b16_8 t130 = GM_LD(gm_b16_8, ((uint8_t *)&t129));
                    gm_b0_1 t131 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t122, t123, t130);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                {
                    gm_b0_1 t132;
                    uint8_t t133 = 0;
                    F.ret = t132;
                    goto gm_fret;
                }
                goto ix103;
ie103:;
ix103:;
ix85:;
            }
            {
                {
                    gm_l20 = ((uint64_t)0ULL);
                }
                {
                    uint64_t t135 = gm_l10;
                    gm_l19 = t135;
                }
                {
                    for (;;) {
                        uint64_t t137 = gm_l20;
                        uint64_t t138 = gm_l19;
                        uint8_t t139 = (t137 < t138);
                        if (!t139) goto lb136;
                        {
                            uint64_t t142 = gm_l20;
                            uint64_t t145 = GM_LD(uint64_t, ((uint8_t *)&t142));
                            gm_l21 = t145;
                            {
                                uint64_t t147;
                                uint8_t t149 = gm_l16;
                                if (!t149) goto ie148;
                                uint64_t t151 = gm_l21;
                                uint64_t t152 = (uintptr_t)t151;
                                uint64_t t153 = GM_LD(uintptr_t, (((uint8_t *)&F.l9) + 32) + 8);
                                if (t152 >= (uintptr_t)(t153)) gm_trap_index(t152, t153, gm_src6d57grm686e3, 520, 29);
                                uint64_t t154 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l9) + 32)) + t152 * 8));
                                uint64_t t155 = t154;
                                t147 = t155;
                                goto ix148;
ie148:;
                                uint64_t t157 = gm_l21;
                                uint64_t t158 = t157;
                                t147 = t158;
ix148:;
                                gm_l22 = t147;
                            }
                            {
                                void * t160 = gm_l0;
                                uint64_t t161 = gm_l21;
                                uint64_t t162 = (uintptr_t)t161;
                                uint64_t t163 = GM_LD(uintptr_t, (((uint8_t *)&F.l9) + 16) + 8);
                                if (t162 >= (uintptr_t)(t163)) gm_trap_index(t162, t163, gm_src6d57grm686e3, 521, 34);
                                void * t164 = (void *)((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l9) + 16)) + t162 * 24);
                                uint64_t t165 = gm_l22;
                                uint64_t t166 = (uintptr_t)t165;
                                uint64_t t167 = GM_LD(uintptr_t, ((uint8_t *)&gm_l15) + 8);
                                if (t166 >= (uintptr_t)(t167)) gm_trap_index(t166, t167, gm_src6d57grm686e3, 521, 51);
                                gm_t88rcucg1b7uk t168 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l15)) + t166 * 4));
                                gm_th2lqbo77pvsc t169;
                                gm_f_check_spec_4vp7jhg((void *)&t169, (void *)NULL, cx, NULL, t160, t164, t168);
                                if (gm_unw.kind) {
                                    goto gm_ll140;
                                }
                                if (!((((uint8_t *)&t169))[16] != 0)) goto ie159;
                                gm_b16_8 t170 = GM_LD(gm_b16_8, ((uint8_t *)&t169));
                                gm_l23 = t170;
                                {
                                    void * t172 = gm_l0;
                                    gm_tl874t2atnlh3 t173 = gm_l4;
                                    gm_b16_8 t174 = gm_l23;
                                    gm_tnpifv20irnph t175;
                                    GM_ST(gm_b16_8, ((uint8_t *)&t175 + 0), t174);
                                    gm_tabb710d9mq2e t176;
                                    gm_f_format_mlqiori((void *)&t176, (void *)NULL, cx, NULL, t175);
                                    if (gm_unw.kind) {
                                        goto gm_ll140;
                                    }
                                    if (((((uint8_t *)&t176))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t176) + 8, gm_src6d57grm686e3, 522, 27);
                                    gm_b16_8 t177 = GM_LD(gm_b16_8, (((uint8_t *)&t176) + 8));
                                    gm_b16_8 t178 = GM_LD(gm_b16_8, ((uint8_t *)&t177));
                                    gm_b0_1 t179 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t172, t173, t178);
                                    if (gm_unw.kind) {
                                        goto gm_ll140;
                                    }
                                }
                                goto ix159;
ie159:;
ix159:;
                            }
                            goto me143;
mn144:;
                            gm_trap("no match arm matched the scrutinee", gm_src6d57grm686e3, 519, 5);
me143:;
                        }
lc136:;
                        {
                            uint64_t t181 = gm_l20;
                            uint64_t r182; bool o183 = __builtin_add_overflow(t181, ((uint64_t)1ULL), &r182);
                            uint64_t t184 = r182;
                            gm_l20 = t184;
                        }
                    }
gm_ll140:;
                    if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb136; }
                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc136; }
                    }
                    goto gm_lad;
lb136:;
                }
            }
            {
                void * t185 = gm_l3;
                void * t186 = GM_LD(void *, (((uint8_t *)t185) + 48));
                void * t187;
                if ((GM_LD(void *, ((uint8_t *)&t186)) != 0)) {
                    void * t188 = GM_LD(void *, (((uint8_t *)&t186) + 0));
                    t187 = t188;
                } else {
                    {
                        void * t190 = gm_l2;
                        uint8_t *t191 = (((uint8_t *)t190) + 8);
                        gm_tntn2tf15puib t192;
                        void * t193 = gm_l3;
                        memcpy((uint8_t *)&t192 + 0, (((uint8_t *)t193) + 0), 40);
                        gm_b16_8 t194 = GM_LD(gm_b16_8, (((uint8_t *)&F.l9) + 0));
                        GM_ST(gm_b16_8, (uint8_t *)&t192 + 40, t194);
                        gm_b16_8 t195 = GM_LD(gm_b16_8, (((uint8_t *)&F.l9) + 16));
                        GM_ST(gm_b16_8, (uint8_t *)&t192 + 56, t195);
                        gm_b16_8 t196 = gm_f_empty_slice_g3tddrb_2();
                        GM_ST(gm_b16_8, (uint8_t *)&t192 + 72, t196);
                        gm_tq7s98ihonkju t197 = gm_f_new_2pnaa87((void *)NULL, cx, NULL, &t192, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_krg6ee1n6ld)));
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        if (((((uint8_t *)&t197))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t197) + 8, gm_src6d57grm686e3, 526, 25);
                        void * t198 = GM_LD(void *, (((uint8_t *)&t197) + 8));
                        memset(&(*(gm_teti411eujvf4 *)t191), 0, sizeof (*(gm_teti411eujvf4 *)t191));
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_teti411eujvf4 *)t191) + 0, ((uint8_t)55ULL));
                        GM_ST(void *, (uint8_t *)&(*(gm_teti411eujvf4 *)t191) + 8, t198);
                    }
                    gm_b0_1 t199;
                    uint8_t t200 = 0;
                    F.ret = t199;
                    goto gm_fret;
                }
                gm_l24 = t187;
            }
            {
                gm_f_list_ir0h3tc_2((void *)&F.l25, (void *)NULL, cx, NULL);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t201 = (void *)((uint8_t *)&F.l25);
                void * t202 = gm_l3;
                uint32_t t203 = GM_LD(uint32_t, (((uint8_t *)t202) + 56));
                void * t204 = gm_l24;
                gm_ttggq7ng4kp3p t205;
                GM_ST(uint32_t, (uint8_t *)&t205 + 4, 0);
                GM_ST(uint32_t, ((uint8_t *)&t205 + 0), t203);
                GM_ST(void *, ((uint8_t *)&t205 + 8), t204);
                gm_trcmmd12cefb1 t206;
                GM_ST(uint8_t, (uint8_t *)&t206 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t206 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t206 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t206 + 24, 0);
                GM_ST(uint8_t, ((uint8_t *)&t206 + 0), ((uint8_t)0ULL));
                GM_ST(gm_ttggq7ng4kp3p, ((uint8_t *)&t206 + 8), t205);
                gm_tftb1odc739t5 t207 = gm_f_push_d6c76ud_2(t201, &t206);
                if (((((uint8_t *)&t207))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t207) + 1, gm_src6d57grm686e3, 530, 5);
                gm_b0_1 t208 = GM_LD(gm_b0_1, (((uint8_t *)&t207) + 1));
            }
            {
                gm_f_list_vp1djv1_2((void *)&F.l26, (void *)NULL, cx, NULL);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                {
                    gm_l28 = ((uint64_t)0ULL);
                }
                {
                    uint64_t t210 = gm_l10;
                    gm_l27 = t210;
                }
                {
                    for (;;) {
                        uint64_t t212 = gm_l28;
                        uint64_t t213 = gm_l27;
                        uint8_t t214 = (t212 < t213);
                        if (!t214) goto lb211;
                        {
                            uint64_t t217 = gm_l28;
                            uint64_t t220 = GM_LD(uint64_t, ((uint8_t *)&t217));
                            gm_l29 = t220;
                            {
                                uint64_t t222;
                                uint8_t t224 = gm_l16;
                                if (!t224) goto ie223;
                                uint64_t t226 = gm_l29;
                                uint64_t t227 = (uintptr_t)t226;
                                uint64_t t228 = GM_LD(uintptr_t, (((uint8_t *)&F.l9) + 32) + 8);
                                if (t227 >= (uintptr_t)(t228)) gm_trap_index(t227, t228, gm_src6d57grm686e3, 533, 29);
                                uint64_t t229 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l9) + 32)) + t227 * 8));
                                uint64_t t230 = t229;
                                t222 = t230;
                                goto ix223;
ie223:;
                                uint64_t t232 = gm_l29;
                                uint64_t t233 = t232;
                                t222 = t233;
ix223:;
                                gm_l30 = t222;
                            }
                            {
                                void * t234 = gm_l24;
                                gm_t88rcucg1b7uk t235 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t234) + 0));
                                void * t236 = gm_l3;
                                uint32_t t237 = GM_LD(uint32_t, (((uint8_t *)t236) + 56));
                                gm_teti411eujvf4 t238;
                                memset(&t238, 0, sizeof t238);
                                GM_ST(uint8_t, ((uint8_t *)&t238 + 0), ((uint8_t)15ULL));
                                GM_ST(uint32_t, ((uint8_t *)&t238 + 8), t237);
                                void * t239 = gm_l2;
                                gm_tl874t2atnlh3 t240 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t239) + 72));
                                void * t241 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t235, &t238, t240);
                                if (gm_unw.kind) {
                                    goto gm_ll215;
                                }
                                gm_l31 = t241;
                            }
                            {
                                void * t242 = (void *)((uint8_t *)&F.l26);
                                uint64_t t243 = gm_l30;
                                uint64_t t244 = (uintptr_t)t243;
                                uint64_t t245 = GM_LD(uintptr_t, ((uint8_t *)&gm_l15) + 8);
                                if (t244 >= (uintptr_t)(t245)) gm_trap_index(t244, t245, gm_src6d57grm686e3, 535, 28);
                                gm_t88rcucg1b7uk t246 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l15)) + t244 * 4));
                                void * t247 = gm_l31;
                                uint64_t t248 = gm_l30;
                                uint32_t t249 = (uint32_t)t248;
                                gm_tbl8il92l97ut t250;
                                GM_ST(uint32_t, (uint8_t *)&t250 + 12, 0);
                                GM_ST(void *, ((uint8_t *)&t250 + 0), t247);
                                GM_ST(uint32_t, ((uint8_t *)&t250 + 8), t249);
                                gm_teti411eujvf4 t251;
                                memset(&t251, 0, sizeof t251);
                                GM_ST(uint8_t, ((uint8_t *)&t251 + 0), ((uint8_t)19ULL));
                                GM_ST(gm_tbl8il92l97ut, ((uint8_t *)&t251 + 8), t250);
                                void * t252 = gm_l2;
                                gm_tl874t2atnlh3 t253 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t252) + 72));
                                void * t254 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t246, &t251, t253);
                                if (gm_unw.kind) {
                                    goto gm_ll215;
                                }
                                gm_tftb1odc739t5 t255 = gm_f_push_d943vgp_2(t242, &(*(gm_t69m56meqefs8 *)((uint8_t *)t254)));
                                if (((((uint8_t *)&t255))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t255) + 1, gm_src6d57grm686e3, 535, 9);
                                gm_b0_1 t256 = GM_LD(gm_b0_1, (((uint8_t *)&t255) + 1));
                            }
                            goto me218;
mn219:;
                            gm_trap("no match arm matched the scrutinee", gm_src6d57grm686e3, 532, 5);
me218:;
                        }
lc211:;
                        {
                            uint64_t t258 = gm_l28;
                            uint64_t r259; bool o260 = __builtin_add_overflow(t258, ((uint64_t)1ULL), &r259);
                            uint64_t t261 = r259;
                            gm_l28 = t261;
                        }
                    }
gm_ll215:;
                    if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb211; }
                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc211; }
                    }
                    goto gm_lad;
lb211:;
                }
            }
            {
                void * t262 = gm_l0;
                void * t263 = (void *)(((uint8_t *)t262) + 8);
                gm_t88rcucg1b7uk t264 = gm_f_unit_2age34u((void *)NULL, cx, NULL, t263);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tntn2tf15puib t265;
                void * t266 = gm_l3;
                memcpy((uint8_t *)&t265 + 0, (((uint8_t *)t266) + 0), 40);
                gm_b16_8 t267 = GM_LD(gm_b16_8, (((uint8_t *)&F.l9) + 0));
                GM_ST(gm_b16_8, (uint8_t *)&t265 + 40, t267);
                gm_b16_8 t268 = GM_LD(gm_b16_8, (((uint8_t *)&F.l9) + 16));
                GM_ST(gm_b16_8, (uint8_t *)&t265 + 56, t268);
                gm_b16_8 t269 = GM_LD(gm_b16_8, (((uint8_t *)&F.l26) + 0));
                GM_ST(gm_b16_8, (uint8_t *)&t265 + 72, t269);
                gm_tq7s98ihonkju t270 = gm_f_new_2pnaa87((void *)NULL, cx, NULL, &t265, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_k0ed7r38qr4)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t270))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t270) + 8, gm_src6d57grm686e3, 537, 42);
                void * t271 = GM_LD(void *, (((uint8_t *)&t270) + 8));
                gm_teti411eujvf4 t272;
                memset(&t272, 0, sizeof t272);
                GM_ST(uint8_t, ((uint8_t *)&t272 + 0), ((uint8_t)55ULL));
                GM_ST(void *, ((uint8_t *)&t272 + 8), t271);
                void * t273 = gm_l2;
                gm_tl874t2atnlh3 t274 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t273) + 72));
                void * t275 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t264, &t272, t274);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l32 = t275;
            }
            {
                void * t276 = (void *)((uint8_t *)&F.l25);
                void * t277 = gm_l32;
                gm_trcmmd12cefb1 t278;
                GM_ST(uint8_t, (uint8_t *)&t278 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t278 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t278 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t278 + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&t278 + 24, 0);
                GM_ST(uint8_t, ((uint8_t *)&t278 + 0), ((uint8_t)4ULL));
                GM_ST(void *, ((uint8_t *)&t278 + 8), t277);
                gm_tftb1odc739t5 t279 = gm_f_push_d6c76ud_2(t276, &t278);
                if (((((uint8_t *)&t279))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t279) + 1, gm_src6d57grm686e3, 538, 5);
                gm_b0_1 t280 = GM_LD(gm_b0_1, (((uint8_t *)&t279) + 1));
            }
            {
                void * t281 = gm_l2;
                uint8_t *t282 = (((uint8_t *)t281) + 8);
                gm_tgabaikr08mjg t283;
                memset((uint8_t *)&t283 + 36, 0, 4);
                gm_b16_8 t284 = GM_LD(gm_b16_8, (((uint8_t *)&F.l25) + 0));
                GM_ST(gm_b16_8, (uint8_t *)&t283 + 0, t284);
                GM_ST(void *, (uint8_t *)&t283 + 16, ((void *)0));
                void * t285 = gm_l2;
                gm_tl874t2atnlh3 t286 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t285) + 72));
                GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t283 + 24, t286);
                gm_tq7s98ihonkju t287 = gm_f_new_dsbahke((void *)NULL, cx, NULL, &t283, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_k76fiiae2n8)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t287))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t287) + 8, gm_src6d57grm686e3, 539, 21);
                void * t288 = GM_LD(void *, (((uint8_t *)&t287) + 8));
                memset(&(*(gm_teti411eujvf4 *)t282), 0, sizeof (*(gm_teti411eujvf4 *)t282));
                GM_ST(uint8_t, (uint8_t *)&(*(gm_teti411eujvf4 *)t282) + 0, ((uint8_t)46ULL));
                GM_ST(void *, (uint8_t *)&(*(gm_teti411eujvf4 *)t282) + 8, t288);
            }
            goto ix14;
ie14:;
            {
                {
                    void * t291 = gm_l0;
                    gm_tl874t2atnlh3 t292 = gm_l4;
                    gm_b16_8 t293;
                    GM_ST(void *, (uint8_t *)&t293, (void *)(gm_srundlmqvm06p));
                    GM_ST(uintptr_t, (uint8_t *)&t293 + 8, (uintptr_t)(59));
                    gm_b0_1 t294 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t291, t292, t293);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                {
                    gm_b0_1 t295;
                    uint8_t t296 = 0;
                    F.ret = t295;
                    goto gm_fret;
                }
            }
ix14:;
        }
        goto ix2;
ie2:;
        {
            gm_b0_1 t298;
            uint8_t t299 = 0;
            F.ret = t298;
            goto gm_fret;
        }
ix2:;
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
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_source_offset_of_vlcjdq6(gm_b16_8 a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_source_offset_of_vlcjdq6 F;
    gm_b16_8 gm_l0; /* lit */
    uint64_t gm_l1; /* at */
    gm_b16_8 gm_l2; /* b */
    uint64_t gm_l3; /* i */
    uint64_t gm_l4; /* n */
    GM_STACK_CHECK_SHALLOW(&F, 596u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        gm_b16_8 t2 = gm_l0;
        gm_l2 = t2;
    }
    {
        uint8_t t4 = 0;
        uint8_t t5 = 0;
        uint64_t t6 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        uint8_t t7 = (t6 < ((uint64_t)2ULL));
        if (!t7) {
            uint64_t t8 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t9 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
            if (t8 >= (uintptr_t)(t9)) gm_trap_index(t8, t9, gm_src6d57grm686e3, 242, 21);
            uint8_t t10 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t8 * 1));
            uint8_t t11 = (!(t10 == ((uint8_t)34ULL)));
            t5 = t11;
        } else t5 = 1;
        if (!t5) {
            uint64_t t12 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
            uint64_t r13; bool o14 = __builtin_sub_overflow(t12, ((uint64_t)1ULL), &r13);
            if (o14) gm_trap("integer overflow in `-`", gm_src6d57grm686e3, 242, 42);
            uint64_t t15 = r13;
            uint64_t t16 = (uintptr_t)t15;
            uint64_t t17 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
            if (t16 >= (uintptr_t)(t17)) gm_trap_index(t16, t17, gm_src6d57grm686e3, 242, 40);
            uint8_t t18 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t16 * 1));
            uint8_t t19 = (!(t18 == ((uint8_t)34ULL)));
            t4 = t19;
        } else t4 = 1;
        if (!t4) goto ie3;
        {
            gm_tf5srh4mp4b3i t21;
            memset(&t21, 0, sizeof t21);
            uint8_t t22 = 0;
            F.ret = t21;
            goto gm_fret;
        }
        goto ix3;
ie3:;
ix3:;
    }
    {
        uint64_t t24 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        uint8_t t25 = (t24 >= ((uint64_t)3ULL));
        if (!t25) goto ie23;
        uint64_t t26 = (uintptr_t)((uint64_t)1ULL);
        uint64_t t27 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        if (t26 >= (uintptr_t)(t27)) gm_trap_index(t26, t27, gm_src6d57grm686e3, 243, 23);
        uint8_t t28 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t26 * 1));
        uint8_t t29 = ((t28 == ((uint8_t)34ULL)));
        if (!t29) goto ie23;
        uint64_t t30 = (uintptr_t)((uint64_t)2ULL);
        uint64_t t31 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        if (t30 >= (uintptr_t)(t31)) gm_trap_index(t30, t31, gm_src6d57grm686e3, 243, 39);
        uint8_t t32 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t30 * 1));
        uint8_t t33 = ((t32 == ((uint8_t)34ULL)));
        if (!t33) goto ie23;
        {
            gm_tf5srh4mp4b3i t35;
            memset(&t35, 0, sizeof t35);
            uint8_t t36 = 0;
            F.ret = t35;
            goto gm_fret;
        }
        goto ix23;
ie23:;
ix23:;
    }
    {
        gm_l3 = ((uint64_t)1ULL);
    }
    {
        gm_l4 = ((uint64_t)0ULL);
    }
    {
        for (;;) {
            uint64_t t38 = gm_l3;
            uint64_t t39 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
            uint64_t r40; bool o41 = __builtin_sub_overflow(t39, ((uint64_t)1ULL), &r40);
            if (o41) gm_trap("integer overflow in `-`", gm_src6d57grm686e3, 246, 15);
            uint64_t t42 = r40;
            uint8_t t43 = (t38 < t42);
            if (!t43) goto lb37;
            {
                uint64_t t47 = gm_l4;
                uint64_t t48 = gm_l1;
                uint8_t t49 = ((t47 == t48));
                if (!t49) goto ie46;
                {
                    gm_tf5srh4mp4b3i t51;
                    uint64_t t53 = gm_l3;
                    uint64_t t54 = (uintptr_t)t53;
                    uint64_t t55 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                    if (t54 >= (uintptr_t)(t55)) gm_trap_index(t54, t55, gm_src6d57grm686e3, 247, 32);
                    uint8_t t56 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t54 * 1));
                    uint8_t t57 = (!(t56 == ((uint8_t)92ULL)));
                    if (!t57) goto ie52;
                    uint64_t t59 = gm_l3;
                    uint64_t t60 = t59;
                    gm_tf5srh4mp4b3i t61;
                    GM_ST(uint8_t, (uint8_t *)&t61 + 9, 0);
                    GM_ST(uint16_t, (uint8_t *)&t61 + 10, 0);
                    GM_ST(uint32_t, (uint8_t *)&t61 + 12, 0);
                    GM_ST(uint64_t, ((uint8_t *)&t61), t60);
                    ((uint8_t *)&t61)[8] = 1;
                    t51 = t61;
                    goto ix52;
ie52:;
                    gm_tf5srh4mp4b3i t63;
                    memset(&t63, 0, sizeof t63);
                    gm_tf5srh4mp4b3i t64 = t63;
                    t51 = t64;
ix52:;
                    uint8_t t65 = 0;
                    F.ret = t51;
                    goto gm_fret;
                }
                goto ix46;
ie46:;
ix46:;
            }
            {
                uint64_t t67 = gm_l3;
                uint64_t t68 = (uintptr_t)t67;
                uint64_t t69 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                if (t68 >= (uintptr_t)(t69)) gm_trap_index(t68, t69, gm_src6d57grm686e3, 248, 12);
                uint8_t t70 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t68 * 1));
                uint8_t t71 = ((t70 == ((uint8_t)92ULL)));
                if (!t71) goto ie66;
                {
                    gm_b16_8 t74 = gm_l2;
                    void * t75 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t74)));
                    uint64_t t76 = (uintptr_t)GM_LD(uintptr_t, ((uint8_t *)&t74) + 8);
                    uint64_t t77 = gm_l3;
                    uint64_t r78; bool o79 = __builtin_add_overflow(t77, ((uint64_t)1ULL), &r78);
                    if (o79) gm_trap("integer overflow in `+`", gm_src6d57grm686e3, 249, 22);
                    uint64_t t80 = r78;
                    uint64_t t81 = (uintptr_t)t80;
                    gm_tqs5p4n2tq19v t82;
                    if (t81 < t76) {
                    uint8_t t83 = GM_LD(uint8_t, ((uint8_t *)t75 + t81 * 1));
                    gm_tqs5p4n2tq19v t84;
                    GM_ST(uint8_t, ((uint8_t *)&t84), t83);
                    ((uint8_t *)&t84)[1] = 1;
                        t82 = t84;
                    } else {
                    gm_tqs5p4n2tq19v t85;
                    memset(&t85, 0, sizeof t85);
                        t82 = t85;
                    }
                    gm_tqs5p4n2tq19v t86;
                    GM_ST(uint8_t, ((uint8_t *)&t86), ((uint8_t)110ULL));
                    ((uint8_t *)&t86)[1] = 1;
                    gm_tqs5p4n2tq19v t87;
                    GM_ST(uint8_t, ((uint8_t *)&t87), ((uint8_t)116ULL));
                    ((uint8_t *)&t87)[1] = 1;
                    gm_tqs5p4n2tq19v t88;
                    GM_ST(uint8_t, ((uint8_t *)&t88), ((uint8_t)114ULL));
                    ((uint8_t *)&t88)[1] = 1;
                    gm_tqs5p4n2tq19v t89;
                    GM_ST(uint8_t, ((uint8_t *)&t89), ((uint8_t)48ULL));
                    ((uint8_t *)&t89)[1] = 1;
                    gm_tqs5p4n2tq19v t90;
                    GM_ST(uint8_t, ((uint8_t *)&t90), ((uint8_t)92ULL));
                    ((uint8_t *)&t90)[1] = 1;
                    gm_tqs5p4n2tq19v t91;
                    GM_ST(uint8_t, ((uint8_t *)&t91), ((uint8_t)34ULL));
                    ((uint8_t *)&t91)[1] = 1;
                    gm_tqs5p4n2tq19v t92;
                    GM_ST(uint8_t, ((uint8_t *)&t92), ((uint8_t)39ULL));
                    ((uint8_t *)&t92)[1] = 1;
                    gm_t6s1lqopkm7fs t93;
                    GM_ST(gm_tqs5p4n2tq19v, ((uint8_t *)&t93 + 0), t86);
                    GM_ST(gm_tqs5p4n2tq19v, ((uint8_t *)&t93 + 2), t87);
                    GM_ST(gm_tqs5p4n2tq19v, ((uint8_t *)&t93 + 4), t88);
                    GM_ST(gm_tqs5p4n2tq19v, ((uint8_t *)&t93 + 6), t89);
                    GM_ST(gm_tqs5p4n2tq19v, ((uint8_t *)&t93 + 8), t90);
                    GM_ST(gm_tqs5p4n2tq19v, ((uint8_t *)&t93 + 10), t91);
                    GM_ST(gm_tqs5p4n2tq19v, ((uint8_t *)&t93 + 12), t92);
                    uint8_t t94 = 0;
                    if (gm_eqb44cb3cf1n(((uint8_t *)&t93) + 0, ((uint8_t *)&t82))) t94 = 1;
                    if (gm_eqb44cb3cf1n(((uint8_t *)&t93) + 2, ((uint8_t *)&t82))) t94 = 1;
                    if (gm_eqb44cb3cf1n(((uint8_t *)&t93) + 4, ((uint8_t *)&t82))) t94 = 1;
                    if (gm_eqb44cb3cf1n(((uint8_t *)&t93) + 6, ((uint8_t *)&t82))) t94 = 1;
                    if (gm_eqb44cb3cf1n(((uint8_t *)&t93) + 8, ((uint8_t *)&t82))) t94 = 1;
                    if (gm_eqb44cb3cf1n(((uint8_t *)&t93) + 10, ((uint8_t *)&t82))) t94 = 1;
                    if (gm_eqb44cb3cf1n(((uint8_t *)&t93) + 12, ((uint8_t *)&t82))) t94 = 1;
                    uint8_t t95 = (uint8_t)(!t94);
                    if (!t95) goto ie73;
                    {
                        gm_tf5srh4mp4b3i t97;
                        memset(&t97, 0, sizeof t97);
                        uint8_t t98 = 0;
                        F.ret = t97;
                        goto gm_fret;
                    }
                    goto ix73;
ie73:;
ix73:;
                }
                {
                    uint64_t t99 = gm_l3;
                    uint64_t r100; bool o101 = __builtin_add_overflow(t99, ((uint64_t)2ULL), &r100);
                    if (o101) gm_trap("integer overflow in `+`", gm_src6d57grm686e3, 250, 13);
                    uint64_t t102 = r100;
                    gm_l3 = t102;
                }
                goto ix66;
ie66:;
                {
                    uint64_t t104 = gm_l3;
                    uint64_t r105; bool o106 = __builtin_add_overflow(t104, ((uint64_t)1ULL), &r105);
                    if (o106) gm_trap("integer overflow in `+`", gm_src6d57grm686e3, 252, 13);
                    uint64_t t107 = r105;
                    gm_l3 = t107;
                }
ix66:;
            }
            {
                uint64_t t108 = gm_l4;
                uint64_t r109; bool o110 = __builtin_add_overflow(t108, ((uint64_t)1ULL), &r109);
                if (o110) gm_trap("integer overflow in `+`", gm_src6d57grm686e3, 254, 9);
                uint64_t t111 = r109;
                gm_l4 = t111;
            }
lc37:;
        }
lb37:;
    }
    {
        gm_tf5srh4mp4b3i t112;
        memset(&t112, 0, sizeof t112);
        uint8_t t113 = 0;
        F.ret = t112;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) void gm_f_template_args_vf1gdn1(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_tl874t2atnlh3 a2, gm_b16_8 a3, gm_tl874t2atnlh3 a4) {
    struct gm_fr_template_args_vf1gdn1 F;
    void * gm_l0; /* cx */
    gm_b16_8 gm_l1; /* bytes */
    gm_tl874t2atnlh3 gm_l2; /* template_span */
    gm_b16_8 gm_l3; /* rest */
    gm_tl874t2atnlh3 gm_l4; /* span */
    void * gm_l5; /* tys */
    gm_tj64ui6kruni6 gm_l7; /* err */
    uint64_t gm_l9; /* at */
    gm_b16_8 gm_l10; /* text */
    uint64_t gm_l11; /* off */
    uint32_t gm_l12; /* lo */
    uint64_t gm_l14; /* n */
    uint8_t gm_l15; /* numbered */
    void * gm_l19; /* v0 */
    gm_t88rcucg1b7uk gm_l20; /* written */
    void * gm_l21; /* v */
    gm_t88rcucg1b7uk gm_l22; /* t */
    gm_b16_8 gm_l23; /* elems */
    uint32_t gm_l24; /* tmp */
    uint64_t gm_l25; /* <tmp> */
    uint64_t gm_l26; /* <tmp> */
    uint64_t gm_l27; /* i */
    uint64_t gm_l28; /* at */
    void * gm_l29; /* base */
    gm_b16_8 gm_l30; /* elems */
    gm_b16_8 gm_l32; /* <tmp> */
    uint64_t gm_l33; /* <tmp> */
    gm_b16_8 gm_l36; /* msg */
    uint64_t gm_l38; /* <tmp> */
    uint64_t gm_l39; /* <tmp> */
    uint64_t gm_l40; /* k */
    void * gm_l41; /* v */
    gm_b16_8 gm_l42; /* <tmp> */
    uint64_t gm_l43; /* <tmp> */
    uint64_t gm_l44; /* ix */
    gm_b16_8 gm_l45; /* given */
    uint64_t gm_l46; /* <tmp> */
    uint64_t gm_l47; /* <tmp> */
    uint64_t gm_l48; /* k */
    uint64_t gm_l49; /* <tmp> */
    uint64_t gm_l50; /* <tmp> */
    uint64_t gm_l51; /* k */
    void * gm_l52; /* spec */
    void * gm_l53; /* v */
    uint8_t gm_l54; /* wants_float */
    gm_t88rcucg1b7uk gm_l55; /* t */
    gm_b16_8 gm_l56; /* msg */
    GM_STACK_CHECK(&F, 4767u);
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
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 0));
        void * t4 = (void *)(((uint8_t *)t3) + 8);
        gm_l5 = t4;
    }
    {
        gm_b16_8 t5 = gm_l1;
        gm_t1jll27j7ae34 t6;
        gm_f_parse_template_rlpl2k8((void *)&t6, (void *)NULL, cx, NULL, t5);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tfeab279c16rr t7;
        uint8_t t10 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
        if (t10 != ((uint8_t)0ULL)) goto mn9;
        F.l6 = (*(gm_tfeab279c16rr *)(((uint8_t *)&t6) + 8));
        gm_tfeab279c16rr t11 = F.l6;
        t7 = t11;
        goto me8;
mn9:;
        uint8_t t13 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
        if (t13 != ((uint8_t)1ULL)) goto mn12;
        gm_l7 = (*(gm_tj64ui6kruni6 *)(((uint8_t *)&t6) + 8));
        {
            gm_tl874t2atnlh3 t15 = gm_l2;
            gm_b16_8 t16 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l7) + 0));
            gm_tnpifv20irnph t17;
            GM_ST(gm_b16_8, ((uint8_t *)&t17 + 0), t16);
            gm_tabb710d9mq2e t18;
            gm_f_format_68lf2sm((void *)&t18, (void *)NULL, cx, NULL, t17);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t18))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t18) + 8, gm_src6d57grm686e3, 357, 44);
            gm_b16_8 t19 = GM_LD(gm_b16_8, (((uint8_t *)&t18) + 8));
            gm_b16_8 t20 = GM_LD(gm_b16_8, ((uint8_t *)&t19));
            gm_f_error_s6hn871((void *)&F.l8, (void *)NULL, cx, NULL, t15, t20);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            if (!(((((uint8_t *)&gm_l7) + 16))[8] != 0)) goto ie21;
            uint64_t t22 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 16));
            gm_l9 = t22;
            {
                void * t24 = gm_l0;
                void * t25 = GM_LD(void *, (((uint8_t *)t24) + 0));
                void * t26 = GM_LD(void *, (((uint8_t *)t25) + 0));
                void * t27 = GM_LD(void *, (((uint8_t *)t26) + 0));
                uint32_t t28 = GM_LD(uint32_t, (((uint8_t *)&gm_l2) + 0));
                void * t29 = gm_f_file_o15fqku(t27, t28);
                uint64_t t30 = GM_LD(uintptr_t, (((uint8_t *)t29) + 16) + 8);
                void * t31 = ((uint8_t *)GM_LD(void *, (((uint8_t *)t29) + 16)));
                uint32_t t32 = GM_LD(uint32_t, (((uint8_t *)&gm_l2) + 4));
                uint64_t t33 = (uint64_t)t32;
                uint64_t t34 = (uintptr_t)t33;
                uint32_t t35 = GM_LD(uint32_t, (((uint8_t *)&gm_l2) + 8));
                uint64_t t36 = (uint64_t)t35;
                uint64_t t37 = (uintptr_t)t36;
                if (t34 > t37 || t37 > (uintptr_t)t30) gm_trap_slice(t34, t37, t30, gm_src6d57grm686e3, 360, 25);
                gm_b16_8 t38;
                GM_ST(void *, (uint8_t *)&t38, (void *)(((uint8_t *)t31 + t34 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t38 + 8, (uintptr_t)(t37 - t34));
                gm_l10 = t38;
            }
            {
                gm_b16_8 t40 = gm_l10;
                uint64_t t41 = gm_l9;
                gm_tf5srh4mp4b3i t42 = gm_f_source_offset_of_vlcjdq6(t40, t41);
                if (!((((uint8_t *)&t42))[8] != 0)) goto ie39;
                uint64_t t43 = GM_LD(uint64_t, ((uint8_t *)&t42));
                gm_l11 = t43;
                {
                    uint32_t t45 = GM_LD(uint32_t, (((uint8_t *)&gm_l2) + 4));
                    uint64_t t46 = gm_l11;
                    uint32_t t47 = (uint32_t)t46;
                    uint32_t r48; bool o49 = __builtin_add_overflow(t45, t47, &r48);
                    if (o49) gm_trap("integer overflow in `+`", gm_src6d57grm686e3, 362, 27);
                    uint32_t t50 = r48;
                    gm_l12 = t50;
                }
                {
                    void * t51 = (void *)((uint8_t *)&F.l8);
                    gm_b16_8 t52;
                    GM_ST(void *, (uint8_t *)&t52, (void *)(gm_s4087kcl9bpus));
                    GM_ST(uintptr_t, (uint8_t *)&t52 + 8, (uintptr_t)(28));
                    uint32_t t53 = GM_LD(uint32_t, (((uint8_t *)&gm_l2) + 0));
                    uint32_t t54 = gm_l12;
                    uint32_t t55 = gm_l12;
                    uint32_t r56; bool o57 = __builtin_add_overflow(t55, ((uint32_t)1ULL), &r56);
                    if (o57) gm_trap("integer overflow in `+`", gm_src6d57grm686e3, 363, 113);
                    uint32_t t58 = r56;
                    gm_tl874t2atnlh3 t59;
                    GM_ST(uint32_t, ((uint8_t *)&t59 + 0), t53);
                    GM_ST(uint32_t, ((uint8_t *)&t59 + 4), t54);
                    GM_ST(uint32_t, ((uint8_t *)&t59 + 8), t58);
                    gm_b16_8 t60;
                    GM_ST(void *, (uint8_t *)&t60, (void *)(gm_s6of1cedjphs7));
                    GM_ST(uintptr_t, (uint8_t *)&t60 + 8, (uintptr_t)(2));
                    gm_b0_1 t61 = gm_f_add_fix_0gkcr4c((void *)NULL, cx, NULL, t51, t52, t59, t60);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                goto ix39;
ie39:;
ix39:;
            }
            goto ix21;
ie21:;
ix21:;
        }
        {
            void * t62 = gm_l0;
            void * t63 = GM_LD(void *, (((uint8_t *)t62) + 0));
            gm_b0_1 t64 = gm_f_emit_dph5v1e((void *)NULL, cx, NULL, t63, &(*(gm_t4gut2c9fj1kt *)((uint8_t *)&F.l8)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        gm_ti6pdj2sunpe4 t65;
        memset(&t65, 0, sizeof t65);
        uint8_t t66 = 0;
        F.ret = t65;
        goto gm_fret;
        goto me8;
mn12:;
        gm_trap("no match arm matched the scrutinee", gm_src6d57grm686e3, 354, 15);
me8:;
        F.l13 = t7;
    }
    {
        uint64_t t67 = GM_LD(uintptr_t, (((uint8_t *)&F.l13) + 16) + 8);
        gm_l14 = t67;
    }
    {
        uint64_t t68 = GM_LD(uintptr_t, (((uint8_t *)&F.l13) + 32) + 8);
        uint8_t t69 = (t68 > ((uint64_t)0ULL));
        gm_l15 = t69;
    }
    {
        gm_f_list_ir0h3tc_2((void *)&F.l16, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_f_list_vp1djv1_2((void *)&F.l17, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        uint64_t t71 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
        uint8_t t72 = ((t71 == ((uint64_t)1ULL)));
        if (!t72) goto ie70;
        uint64_t t73 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t74 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
        if (t73 >= (uintptr_t)(t74)) gm_trap_index(t73, t74, gm_src6d57grm686e3, 374, 40);
        uint8_t t75 = GM_LD(uint8_t, ((((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t73 * 144) + 0) + 0));
        if (t75 != ((uint8_t)2ULL)) goto ie70;
        gm_tc3oht4ksth9d t76 = GM_LD(gm_tc3oht4ksth9d, ((((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t73 * 144) + 0) + 8));
        F.l18 = t76;
        {
            void * t78 = gm_l0;
            void * t79 = (void *)((uint8_t *)&F.l18);
            gm_tkn42u7na3t81 t80;
            memset(&t80, 0, sizeof t80);
            void * t81 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t78, t79, t80);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l19 = t81;
        }
        {
            void * t82 = gm_l0;
            void * t83 = (void *)(((uint8_t *)t82) + 96);
            void * t84 = gm_l0;
            void * t85 = GM_LD(void *, (((uint8_t *)t84) + 0));
            void * t86 = gm_l19;
            gm_t88rcucg1b7uk t87 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t86) + 0));
            gm_t88rcucg1b7uk t88 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t83, t85, t87);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l20 = t88;
        }
        {
            void * t89 = gm_l0;
            void * t90 = gm_l19;
            gm_tl874t2atnlh3 t91 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l18) + 72));
            void * t92 = gm_f_elements_tuple_gdm5n6q((void *)NULL, cx, NULL, t89, t90, t91);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            void * t93;
            if ((GM_LD(void *, ((uint8_t *)&t92)) != 0)) {
                void * t94 = GM_LD(void *, (((uint8_t *)&t92) + 0));
                t93 = t94;
            } else {
                gm_ti6pdj2sunpe4 t95;
                memset(&t95, 0, sizeof t95);
                uint8_t t96 = 0;
                F.ret = t95;
                goto gm_fret;
            }
            gm_l21 = t93;
        }
        {
            void * t97 = gm_l0;
            void * t98 = (void *)(((uint8_t *)t97) + 96);
            void * t99 = gm_l0;
            void * t100 = GM_LD(void *, (((uint8_t *)t99) + 0));
            void * t101 = gm_l21;
            gm_t88rcucg1b7uk t102 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t101) + 0));
            gm_t88rcucg1b7uk t103 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t98, t100, t102);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l22 = t103;
        }
        {
            void * t105 = gm_l5;
            gm_t88rcucg1b7uk t106 = gm_l22;
            gm_t53at80pfdus3 t107;
            gm_f_kind_jcnt4hm((void *)&t107, t105, GM_CONV(gm_csguankko4lmhd, t106));
            uint8_t t108 = GM_LD(uint8_t, (((uint8_t *)&t107) + 0));
            if (t108 != ((uint8_t)12ULL)) goto ie104;
            gm_b16_8 t109 = GM_LD(gm_b16_8, ((((uint8_t *)&t107) + 8) + 0));
            gm_l23 = t109;
            uint8_t t110;
            uint8_t t112 = gm_l15;
            if (!t112) goto ie111;
            uint64_t t114 = GM_LD(uintptr_t, ((uint8_t *)&gm_l23) + 8);
            gm_th2lqbo77pvsc t115;
            gm_f_numbered_error_07hll20((void *)&t115, (void *)NULL, cx, NULL, &(*(gm_tfeab279c16rr *)((uint8_t *)&F.l13)), t114);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t116;
            if (((((uint8_t *)&t115))[16] != 0)) goto mn118;
            t116 = ((uint8_t)1);
            goto me117;
mn118:;
            t116 = ((uint8_t)0);
            goto me117;
mn119:;
            gm_trap("no match arm matched the scrutinee", gm_src6d57grm686e3, 380, 63);
me117:;
            uint8_t t120 = t116;
            t110 = t120;
            goto ix111;
ie111:;
            uint64_t t122 = GM_LD(uintptr_t, ((uint8_t *)&gm_l23) + 8);
            uint64_t t123 = gm_l14;
            uint8_t t124 = ((t122 == t123));
            uint8_t t125 = t124;
            t110 = t125;
ix111:;
            if (!t110) goto ie104;
            {
                void * t127 = gm_l0;
                gm_t88rcucg1b7uk t128 = gm_l22;
                gm_tl874t2atnlh3 t129 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l18) + 72));
                uint32_t t130 = gm_f_temp_ilbvj8q((void *)NULL, cx, NULL, t127, t128, t129);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l24 = t130;
            }
            {
                void * t131 = (void *)((uint8_t *)&F.l16);
                uint32_t t132 = gm_l24;
                void * t133 = gm_l21;
                gm_ttggq7ng4kp3p t134;
                GM_ST(uint32_t, (uint8_t *)&t134 + 4, 0);
                GM_ST(uint32_t, ((uint8_t *)&t134 + 0), t132);
                GM_ST(void *, ((uint8_t *)&t134 + 8), t133);
                gm_trcmmd12cefb1 t135;
                GM_ST(uint8_t, (uint8_t *)&t135 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t135 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t135 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t135 + 24, 0);
                GM_ST(uint8_t, ((uint8_t *)&t135 + 0), ((uint8_t)0ULL));
                GM_ST(gm_ttggq7ng4kp3p, ((uint8_t *)&t135 + 8), t134);
                gm_tftb1odc739t5 t136 = gm_f_push_d6c76ud_2(t131, &t135);
                if (((((uint8_t *)&t136))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t136) + 1, gm_src6d57grm686e3, 382, 13);
                gm_b0_1 t137 = GM_LD(gm_b0_1, (((uint8_t *)&t136) + 1));
            }
            {
                {
                    gm_l26 = ((uint64_t)0ULL);
                }
                {
                    uint64_t t139 = gm_l14;
                    gm_l25 = t139;
                }
                {
                    for (;;) {
                        uint64_t t141 = gm_l26;
                        uint64_t t142 = gm_l25;
                        uint8_t t143 = (t141 < t142);
                        if (!t143) goto lb140;
                        {
                            uint64_t t146 = gm_l26;
                            uint64_t t149 = GM_LD(uint64_t, ((uint8_t *)&t146));
                            gm_l27 = t149;
                            {
                                uint64_t t151;
                                uint8_t t153 = gm_l15;
                                if (!t153) goto ie152;
                                uint64_t t155 = gm_l27;
                                uint64_t t156 = (uintptr_t)t155;
                                uint64_t t157 = GM_LD(uintptr_t, (((uint8_t *)&F.l13) + 32) + 8);
                                if (t156 >= (uintptr_t)(t157)) gm_trap_index(t156, t157, gm_src6d57grm686e3, 384, 37);
                                uint64_t t158 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l13) + 32)) + t156 * 8));
                                uint64_t t159 = t158;
                                t151 = t159;
                                goto ix152;
ie152:;
                                uint64_t t161 = gm_l27;
                                uint64_t t162 = t161;
                                t151 = t162;
ix152:;
                                gm_l28 = t151;
                            }
                            {
                                gm_t88rcucg1b7uk t163 = gm_l22;
                                uint32_t t164 = gm_l24;
                                gm_teti411eujvf4 t165;
                                memset(&t165, 0, sizeof t165);
                                GM_ST(uint8_t, ((uint8_t *)&t165 + 0), ((uint8_t)15ULL));
                                GM_ST(uint32_t, ((uint8_t *)&t165 + 8), t164);
                                gm_tl874t2atnlh3 t166 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l18) + 72));
                                void * t167 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t163, &t165, t166);
                                if (gm_unw.kind) {
                                    goto gm_ll144;
                                }
                                gm_l29 = t167;
                            }
                            {
                                void * t168 = (void *)((uint8_t *)&F.l17);
                                uint64_t t169 = gm_l28;
                                uint64_t t170 = (uintptr_t)t169;
                                uint64_t t171 = GM_LD(uintptr_t, ((uint8_t *)&gm_l23) + 8);
                                if (t170 >= (uintptr_t)(t171)) gm_trap_index(t170, t171, gm_src6d57grm686e3, 386, 36);
                                gm_t88rcucg1b7uk t172 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l23)) + t170 * 4));
                                void * t173 = gm_l29;
                                uint64_t t174 = gm_l28;
                                uint32_t t175 = (uint32_t)t174;
                                gm_tbl8il92l97ut t176;
                                GM_ST(uint32_t, (uint8_t *)&t176 + 12, 0);
                                GM_ST(void *, ((uint8_t *)&t176 + 0), t173);
                                GM_ST(uint32_t, ((uint8_t *)&t176 + 8), t175);
                                gm_teti411eujvf4 t177;
                                memset(&t177, 0, sizeof t177);
                                GM_ST(uint8_t, ((uint8_t *)&t177 + 0), ((uint8_t)19ULL));
                                GM_ST(gm_tbl8il92l97ut, ((uint8_t *)&t177 + 8), t176);
                                gm_tl874t2atnlh3 t178 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l18) + 72));
                                void * t179 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t172, &t177, t178);
                                if (gm_unw.kind) {
                                    goto gm_ll144;
                                }
                                gm_tftb1odc739t5 t180 = gm_f_push_d943vgp_2(t168, &(*(gm_t69m56meqefs8 *)((uint8_t *)t179)));
                                if (((((uint8_t *)&t180))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t180) + 1, gm_src6d57grm686e3, 386, 17);
                                gm_b0_1 t181 = GM_LD(gm_b0_1, (((uint8_t *)&t180) + 1));
                            }
                            goto me147;
mn148:;
                            gm_trap("no match arm matched the scrutinee", gm_src6d57grm686e3, 383, 13);
me147:;
                        }
lc140:;
                        {
                            uint64_t t183 = gm_l26;
                            uint64_t r184; bool o185 = __builtin_add_overflow(t183, ((uint64_t)1ULL), &r184);
                            uint64_t t186 = r184;
                            gm_l26 = t186;
                        }
                    }
gm_ll144:;
                    if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb140; }
                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc140; }
                    }
                    goto gm_lad;
lb140:;
                }
            }
            goto ix104;
ie104:;
            void * t188 = gm_l5;
            gm_t88rcucg1b7uk t189 = gm_l22;
            uint8_t t190 = gm_f_is_error_8k27gd1(t188, GM_CONV(gm_csguankko4lmhd, t189));
            if (!t190) goto ie187;
            {
                gm_ti6pdj2sunpe4 t192;
                memset(&t192, 0, sizeof t192);
                uint8_t t193 = 0;
                F.ret = t192;
                goto gm_fret;
            }
            goto ix187;
ie187:;
            uint8_t t195 = gm_l15;
            if (!t195) goto ie194;
            {
                void * t198 = gm_l5;
                gm_t88rcucg1b7uk t199 = gm_l22;
                gm_t53at80pfdus3 t200;
                gm_f_kind_jcnt4hm((void *)&t200, t198, GM_CONV(gm_csguankko4lmhd, t199));
                uint8_t t201 = GM_LD(uint8_t, (((uint8_t *)&t200) + 0));
                if (t201 != ((uint8_t)12ULL)) goto ie197;
                gm_b16_8 t202 = GM_LD(gm_b16_8, ((((uint8_t *)&t200) + 8) + 0));
                gm_l30 = t202;
                {
                    void * t204 = gm_l0;
                    gm_tl874t2atnlh3 t205 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l18) + 72));
                    uint64_t t206 = GM_LD(uintptr_t, ((uint8_t *)&gm_l30) + 8);
                    gm_th2lqbo77pvsc t207;
                    gm_f_numbered_error_07hll20((void *)&t207, (void *)NULL, cx, NULL, &(*(gm_tfeab279c16rr *)((uint8_t *)&F.l13)), t206);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_b16_8 t208;
                    if (((((uint8_t *)&t207))[16] != 0)) {
                        gm_b16_8 t209 = GM_LD(gm_b16_8, (((uint8_t *)&t207) + 0));
                        t208 = t209;
                    } else {
                        gm_b16_8 t210;
                        GM_ST(void *, (uint8_t *)&t210, (void *)(gm_sr4qbrk6tlgaa));
                        GM_ST(uintptr_t, (uint8_t *)&t210 + 8, (uintptr_t)(0));
                        t208 = t210;
                    }
                    gm_b0_1 t211 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t204, t205, t208);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                goto ix197;
ie197:;
                {
                    void * t213 = gm_l0;
                    gm_tl874t2atnlh3 t214 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l18) + 72));
                    void * t215 = gm_l0;
                    gm_t88rcucg1b7uk t216 = gm_l20;
                    gm_b16_8 t217 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t215, t216);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tnpifv20irnph t218;
                    GM_ST(gm_b16_8, ((uint8_t *)&t218 + 0), t217);
                    gm_tabb710d9mq2e t219;
                    gm_f_format_les73rv((void *)&t219, (void *)NULL, cx, NULL, t218);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    if (((((uint8_t *)&t219))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t219) + 8, gm_src6d57grm686e3, 394, 34);
                    gm_b16_8 t220 = GM_LD(gm_b16_8, (((uint8_t *)&t219) + 8));
                    gm_b16_8 t221 = GM_LD(gm_b16_8, ((uint8_t *)&t220));
                    gm_b0_1 t222 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t213, t214, t221);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
ix197:;
            }
            {
                gm_ti6pdj2sunpe4 t223;
                memset(&t223, 0, sizeof t223);
                uint8_t t224 = 0;
                F.ret = t223;
                goto gm_fret;
            }
            goto ix194;
ie194:;
            {
                void * t226 = gm_l0;
                gm_tl874t2atnlh3 t227 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l18) + 72));
                gm_tq3m5qgvkpje8 t228;
                uint64_t t229 = gm_l14;
                gm_b16_8 t230 = gm_f_placeholders_text_rtvia6b((void *)NULL, cx, NULL, t229);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(gm_b16_8, (uint8_t *)&t228 + 0, t230);
                uint64_t t231 = gm_l14;
                GM_ST(uint64_t, (uint8_t *)&t228 + 16, t231);
                void * t232 = gm_l0;
                gm_t88rcucg1b7uk t233 = gm_l20;
                gm_b16_8 t234 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t232, t233);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(gm_b16_8, (uint8_t *)&t228 + 24, t234);
                gm_tabb710d9mq2e t235;
                gm_f_format_k6j7893((void *)&t235, (void *)NULL, cx, NULL, &t228);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t235))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t235) + 8, gm_src6d57grm686e3, 398, 30);
                gm_b16_8 t236 = GM_LD(gm_b16_8, (((uint8_t *)&t235) + 8));
                gm_b16_8 t237 = GM_LD(gm_b16_8, ((uint8_t *)&t236));
                gm_b0_1 t238 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t226, t227, t237);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                gm_ti6pdj2sunpe4 t239;
                memset(&t239, 0, sizeof t239);
                uint8_t t240 = 0;
                F.ret = t239;
                goto gm_fret;
            }
ix194:;
ix187:;
ix104:;
        }
        goto ix70;
ie70:;
        {
            gm_f_list_vp1djv1((void *)&F.l31, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            {
                gm_b16_8 t243 = gm_l3;
                gm_l32 = t243;
            }
            {
                gm_l33 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t245 = gm_l33;
                    uint64_t t246 = GM_LD(uintptr_t, ((uint8_t *)&gm_l32) + 8);
                    uint8_t t247 = (t245 < t246);
                    if (!t247) goto lb244;
                    {
                        uint64_t t250 = gm_l33;
                        uint64_t t251 = (uintptr_t)t250;
                        uint64_t t252 = GM_LD(uintptr_t, ((uint8_t *)&gm_l32) + 8);
                        gm_t7te2tdpebbt4 t255 = GM_LD(gm_t7te2tdpebbt4, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l32)) + t251 * 144));
                        F.l34 = t255;
                        {
                            uint8_t t258 = GM_LD(uint8_t, ((((uint8_t *)&F.l34) + 0) + 0));
                            if (t258 != ((uint8_t)0ULL)) goto ie257;
                            gm_tc3oht4ksth9d t259 = GM_LD(gm_tc3oht4ksth9d, ((((uint8_t *)&F.l34) + 0) + 8));
                            F.l35 = t259;
                            {
                                void * t261 = (void *)((uint8_t *)&F.l31);
                                gm_tftb1odc739t5 t262 = gm_f_push_d943vgp(t261, &(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l35)));
                                if (((((uint8_t *)&t262))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t262) + 1, gm_src6d57grm686e3, 408, 13);
                                gm_b0_1 t263 = GM_LD(gm_b0_1, (((uint8_t *)&t262) + 1));
                            }
                            goto ix257;
ie257:;
                            {
                                {
                                    void * t266 = gm_l0;
                                    gm_tl874t2atnlh3 t267 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l34) + 128));
                                    gm_b16_8 t268;
                                    GM_ST(void *, (uint8_t *)&t268, (void *)(gm_smom6v4b4s28c));
                                    GM_ST(uintptr_t, (uint8_t *)&t268 + 8, (uintptr_t)(51));
                                    gm_b0_1 t269 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t266, t267, t268);
                                    if (gm_unw.kind) {
                                        goto gm_ll248;
                                    }
                                }
                                {
                                    gm_ti6pdj2sunpe4 t270;
                                    memset(&t270, 0, sizeof t270);
                                    uint8_t t271 = 0;
                                    F.ret = t270;
                                    goto gm_fret;
                                }
                            }
ix257:;
                        }
                        goto me253;
mn254:;
                        gm_trap("no match arm matched the scrutinee", gm_src6d57grm686e3, 403, 9);
me253:;
                    }
lc244:;
                    {
                        uint64_t t273 = gm_l33;
                        uint64_t r274; bool o275 = __builtin_add_overflow(t273, ((uint64_t)1ULL), &r274);
                        uint64_t t276 = r274;
                        gm_l33 = t276;
                    }
                }
gm_ll248:;
                if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb244; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc244; }
                }
                goto gm_lad;
lb244:;
            }
        }
        {
            uint8_t t278 = gm_l15;
            if (!t278) goto ie277;
            {
                uint64_t t281 = GM_LD(uintptr_t, (((uint8_t *)&F.l31) + 0) + 8);
                gm_th2lqbo77pvsc t282;
                gm_f_numbered_error_07hll20((void *)&t282, (void *)NULL, cx, NULL, &(*(gm_tfeab279c16rr *)((uint8_t *)&F.l13)), t281);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (!((((uint8_t *)&t282))[16] != 0)) goto ie280;
                gm_b16_8 t283 = GM_LD(gm_b16_8, ((uint8_t *)&t282));
                gm_l36 = t283;
                {
                    void * t285 = gm_l0;
                    gm_tl874t2atnlh3 t286 = gm_l4;
                    gm_b16_8 t287 = gm_l36;
                    gm_b0_1 t288 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t285, t286, t287);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                {
                    gm_ti6pdj2sunpe4 t289;
                    memset(&t289, 0, sizeof t289);
                    uint8_t t290 = 0;
                    F.ret = t289;
                    goto gm_fret;
                }
                goto ix280;
ie280:;
ix280:;
            }
            {
                gm_f_list_vp1djv1_2((void *)&F.l37, (void *)NULL, cx, NULL);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                {
                    gm_l39 = ((uint64_t)0ULL);
                }
                {
                    uint64_t t292 = GM_LD(uintptr_t, (((uint8_t *)&F.l31) + 0) + 8);
                    gm_l38 = t292;
                }
                {
                    for (;;) {
                        uint64_t t294 = gm_l39;
                        uint64_t t295 = gm_l38;
                        uint8_t t296 = (t294 < t295);
                        if (!t296) goto lb293;
                        {
                            uint64_t t299 = gm_l39;
                            uint64_t t302 = GM_LD(uint64_t, ((uint8_t *)&t299));
                            gm_l40 = t302;
                            {
                                void * t304 = gm_l0;
                                uint64_t t305 = gm_l40;
                                uint64_t t306 = (uintptr_t)t305;
                                uint64_t t307 = GM_LD(uintptr_t, (((uint8_t *)&F.l31) + 0) + 8);
                                if (t306 >= (uintptr_t)(t307)) gm_trap_index(t306, t307, gm_src6d57grm686e3, 419, 31);
                                void * t308 = (void *)((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l31) + 0)) + t306 * 88);
                                gm_tkn42u7na3t81 t309;
                                memset(&t309, 0, sizeof t309);
                                void * t310 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t304, t308, t309);
                                if (gm_unw.kind) {
                                    goto gm_ll297;
                                }
                                gm_l41 = t310;
                            }
                            {
                                void * t311 = (void *)((uint8_t *)&F.l37);
                                void * t312 = gm_l0;
                                void * t313 = gm_l41;
                                void * t314 = (void *)((uint8_t *)&F.l16);
                                void * t315 = gm_f_bind_temp_sa5rcbr((void *)NULL, cx, NULL, t312, t313, t314);
                                if (gm_unw.kind) {
                                    goto gm_ll297;
                                }
                                gm_tftb1odc739t5 t316 = gm_f_push_d943vgp_2(t311, &(*(gm_t69m56meqefs8 *)((uint8_t *)t315)));
                                if (((((uint8_t *)&t316))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t316) + 1, gm_src6d57grm686e3, 420, 17);
                                gm_b0_1 t317 = GM_LD(gm_b0_1, (((uint8_t *)&t316) + 1));
                            }
                            goto me300;
mn301:;
                            gm_trap("no match arm matched the scrutinee", gm_src6d57grm686e3, 418, 13);
me300:;
                        }
lc293:;
                        {
                            uint64_t t319 = gm_l39;
                            uint64_t r320; bool o321 = __builtin_add_overflow(t319, ((uint64_t)1ULL), &r320);
                            uint64_t t322 = r320;
                            gm_l39 = t322;
                        }
                    }
gm_ll297:;
                    if (gm_unw.frame == &F.h && gm_unw.label == 2) {
                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb293; }
                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc293; }
                    }
                    goto gm_lad;
lb293:;
                }
            }
            {
                {
                    gm_b16_8 t324 = GM_LD(gm_b16_8, (((uint8_t *)&F.l13) + 32));
                    gm_l42 = t324;
                }
                {
                    gm_l43 = ((uint64_t)0ULL);
                }
                {
                    for (;;) {
                        uint64_t t326 = gm_l43;
                        uint64_t t327 = GM_LD(uintptr_t, ((uint8_t *)&gm_l42) + 8);
                        uint8_t t328 = (t326 < t327);
                        if (!t328) goto lb325;
                        {
                            uint64_t t331 = gm_l43;
                            uint64_t t332 = (uintptr_t)t331;
                            uint64_t t333 = GM_LD(uintptr_t, ((uint8_t *)&gm_l42) + 8);
                            uint64_t t334 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l42)) + t332 * 8));
                            uint64_t t337 = GM_LD(uint64_t, ((uint8_t *)&t334));
                            gm_l44 = t337;
                            {
                                void * t339 = (void *)((uint8_t *)&F.l17);
                                uint64_t t340 = gm_l44;
                                uint64_t t341 = (uintptr_t)t340;
                                uint64_t t342 = GM_LD(uintptr_t, (((uint8_t *)&F.l37) + 0) + 8);
                                if (t341 >= (uintptr_t)(t342)) gm_trap_index(t341, t342, gm_src6d57grm686e3, 422, 52);
                                gm_tftb1odc739t5 t343 = gm_f_push_d943vgp_2(t339, &(*(gm_t69m56meqefs8 *)((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l37) + 0)) + t341 * 88)));
                                if (((((uint8_t *)&t343))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t343) + 1, gm_src6d57grm686e3, 422, 40);
                                gm_b0_1 t344 = GM_LD(gm_b0_1, (((uint8_t *)&t343) + 1));
                            }
                            goto me335;
mn336:;
                            gm_trap("no match arm matched the scrutinee", gm_src6d57grm686e3, 422, 13);
me335:;
                        }
lc325:;
                        {
                            uint64_t t346 = gm_l43;
                            uint64_t r347; bool o348 = __builtin_add_overflow(t346, ((uint64_t)1ULL), &r347);
                            uint64_t t349 = r347;
                            gm_l43 = t349;
                        }
                    }
lb325:;
                }
            }
            goto ix277;
ie277:;
            uint64_t t351 = GM_LD(uintptr_t, (((uint8_t *)&F.l31) + 0) + 8);
            uint64_t t352 = gm_l14;
            uint8_t t353 = (!(t351 == t352));
            if (!t353) goto ie350;
            {
                gm_b16_8 t355;
                uint64_t t357 = GM_LD(uintptr_t, (((uint8_t *)&F.l31) + 0) + 8);
                uint8_t t358 = ((t357 == ((uint64_t)1ULL)));
                if (!t358) goto ie356;
                gm_b16_8 t360;
                GM_ST(void *, (uint8_t *)&t360, (void *)(gm_scne9acm49p4h));
                GM_ST(uintptr_t, (uint8_t *)&t360 + 8, (uintptr_t)(10));
                gm_b16_8 t361 = t360;
                t355 = t361;
                goto ix356;
ie356:;
                uint64_t t363 = GM_LD(uintptr_t, (((uint8_t *)&F.l31) + 0) + 8);
                gm_t4ce0jd3tdro4 t364;
                GM_ST(uint64_t, ((uint8_t *)&t364 + 0), t363);
                gm_tabb710d9mq2e t365;
                gm_f_format_td6patj((void *)&t365, (void *)NULL, cx, NULL, t364);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t365))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t365) + 8, gm_src6d57grm686e3, 424, 69);
                gm_b16_8 t366 = GM_LD(gm_b16_8, (((uint8_t *)&t365) + 8));
                gm_b16_8 t367 = t366;
                gm_b16_8 t368 = GM_LD(gm_b16_8, ((uint8_t *)&t367));
                t355 = t368;
ix356:;
                gm_l45 = t355;
            }
            {
                void * t369 = gm_l0;
                gm_tl874t2atnlh3 t370 = gm_l4;
                gm_t6j6smj7qoctu t371;
                uint64_t t372 = gm_l14;
                gm_b16_8 t373 = gm_f_placeholders_text_rtvia6b((void *)NULL, cx, NULL, t372);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(gm_b16_8, (uint8_t *)&t371 + 0, t373);
                gm_b16_8 t374 = gm_l45;
                GM_ST(gm_b16_8, (uint8_t *)&t371 + 16, t374);
                gm_tabb710d9mq2e t375;
                gm_f_format_2h4f9a5((void *)&t375, (void *)NULL, cx, NULL, &t371);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t375))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t375) + 8, gm_src6d57grm686e3, 425, 28);
                gm_b16_8 t376 = GM_LD(gm_b16_8, (((uint8_t *)&t375) + 8));
                gm_b16_8 t377 = GM_LD(gm_b16_8, ((uint8_t *)&t376));
                gm_b0_1 t378 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t369, t370, t377);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                gm_ti6pdj2sunpe4 t379;
                memset(&t379, 0, sizeof t379);
                uint8_t t380 = 0;
                F.ret = t379;
                goto gm_fret;
            }
            goto ix350;
ie350:;
ix350:;
ix277:;
        }
        {
            uint8_t t382 = gm_l15;
            uint8_t t383 = (uint8_t)!t382;
            if (!t383) goto ie381;
            {
                {
                    gm_l47 = ((uint64_t)0ULL);
                }
                {
                    uint64_t t386 = GM_LD(uintptr_t, (((uint8_t *)&F.l31) + 0) + 8);
                    gm_l46 = t386;
                }
                {
                    for (;;) {
                        uint64_t t388 = gm_l47;
                        uint64_t t389 = gm_l46;
                        uint8_t t390 = (t388 < t389);
                        if (!t390) goto lb387;
                        {
                            uint64_t t393 = gm_l47;
                            uint64_t t396 = GM_LD(uint64_t, ((uint8_t *)&t393));
                            gm_l48 = t396;
                            {
                                void * t398 = (void *)((uint8_t *)&F.l17);
                                void * t399 = gm_l0;
                                uint64_t t400 = gm_l48;
                                uint64_t t401 = (uintptr_t)t400;
                                uint64_t t402 = GM_LD(uintptr_t, (((uint8_t *)&F.l31) + 0) + 8);
                                if (t401 >= (uintptr_t)(t402)) gm_trap_index(t401, t402, gm_src6d57grm686e3, 429, 63);
                                void * t403 = (void *)((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l31) + 0)) + t401 * 88);
                                gm_tkn42u7na3t81 t404;
                                memset(&t404, 0, sizeof t404);
                                void * t405 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t399, t403, t404);
                                if (gm_unw.kind) {
                                    goto gm_ll391;
                                }
                                gm_tftb1odc739t5 t406 = gm_f_push_d943vgp_2(t398, &(*(gm_t69m56meqefs8 *)((uint8_t *)t405)));
                                if (((((uint8_t *)&t406))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t406) + 1, gm_src6d57grm686e3, 429, 42);
                                gm_b0_1 t407 = GM_LD(gm_b0_1, (((uint8_t *)&t406) + 1));
                            }
                            goto me394;
mn395:;
                            gm_trap("no match arm matched the scrutinee", gm_src6d57grm686e3, 429, 13);
me394:;
                        }
lc387:;
                        {
                            uint64_t t409 = gm_l47;
                            uint64_t r410; bool o411 = __builtin_add_overflow(t409, ((uint64_t)1ULL), &r410);
                            uint64_t t412 = r410;
                            gm_l47 = t412;
                        }
                    }
gm_ll391:;
                    if (gm_unw.frame == &F.h && gm_unw.label == 4) {
                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb387; }
                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc387; }
                    }
                    goto gm_lad;
lb387:;
                }
            }
            goto ix381;
ie381:;
ix381:;
        }
ix70:;
    }
    {
        {
            gm_l50 = ((uint64_t)0ULL);
        }
        {
            uint64_t t414 = gm_l14;
            gm_l49 = t414;
        }
        {
            for (;;) {
                uint64_t t416 = gm_l50;
                uint64_t t417 = gm_l49;
                uint8_t t418 = (t416 < t417);
                if (!t418) goto lb415;
                {
                    uint64_t t421 = gm_l50;
                    uint64_t t424 = GM_LD(uint64_t, ((uint8_t *)&t421));
                    gm_l51 = t424;
                    {
                        uint64_t t426 = gm_l51;
                        uint64_t t427 = (uintptr_t)t426;
                        uint64_t t428 = GM_LD(uintptr_t, (((uint8_t *)&F.l13) + 16) + 8);
                        if (t427 >= (uintptr_t)(t428)) gm_trap_index(t427, t428, gm_src6d57grm686e3, 433, 18);
                        void * t429 = (void *)((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l13) + 16)) + t427 * 24);
                        gm_l52 = t429;
                    }
                    {
                        void * t431 = gm_l52;
                        uint8_t t432 = gm_f_is_default_spec_cdd8jng(t431);
                        if (!t432) goto ie430;
                        {
                            goto lc415;
                        }
                        goto ix430;
ie430:;
ix430:;
                    }
                    {
                        uint64_t t434 = gm_l51;
                        uint64_t t435 = (uintptr_t)t434;
                        uint64_t t436 = GM_LD(uintptr_t, (((uint8_t *)&F.l17) + 0) + 8);
                        if (t435 >= (uintptr_t)(t436)) gm_trap_index(t435, t436, gm_src6d57grm686e3, 435, 15);
                        void * t437 = (void *)((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l17) + 0)) + t435 * 88);
                        gm_l53 = t437;
                    }
                    {
                        uint8_t t438 = 0;
                        void * t439 = gm_l52;
                        uint8_t t440 = ((GM_LD(uint8_t, (((uint8_t *)t439) + 20) + 0) == ((uint8_t)5ULL)));
                        if (!t440) {
                            void * t441 = gm_l52;
                            uint8_t t442;
                            if ((((((uint8_t *)t441) + 12))[4] != 0)) goto mn444;
                            t442 = ((uint8_t)0);
                            goto me443;
mn444:;
                            t442 = ((uint8_t)1);
                            goto me443;
mn445:;
                            gm_trap("no match arm matched the scrutinee", gm_src6d57grm686e3, 437, 45);
me443:;
                            t438 = t442;
                        } else t438 = 1;
                        gm_l54 = t438;
                    }
                    {
                        uint8_t t447 = gm_l54;
                        if (!t447) goto ie446;
                        void * t448 = gm_l0;
                        void * t449 = (void *)(((uint8_t *)t448) + 96);
                        void * t450 = gm_l5;
                        void * t451 = gm_l53;
                        gm_t88rcucg1b7uk t452 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t451) + 0));
                        gm_taari1s0m51a7 t453 = gm_f_var_kind_1sl5sig((void *)NULL, cx, NULL, t449, t450, t452);
                        if (gm_unw.kind) {
                            goto gm_ll419;
                        }
                        gm_t9qkh0rsooc8d t454;
                        GM_ST(uint8_t, ((uint8_t *)&t454 + 0), ((uint8_t)0ULL));
                        gm_taari1s0m51a7 t455;
                        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t455), t454);
                        ((uint8_t *)&t455)[1] = 1;
                        uint8_t t456 = (gm_eq5307j5k9ms(((uint8_t *)&t453), ((uint8_t *)&t455)));
                        if (!t456) goto ie446;
                        {
                            {
                                void * t459 = gm_l0;
                                void * t460 = (void *)(((uint8_t *)t459) + 96);
                                void * t461 = gm_l5;
                                void * t462 = gm_l53;
                                gm_t88rcucg1b7uk t463 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t462) + 0));
                                void * t464 = gm_l5;
                                gm_t53at80pfdus3 t465;
                                memset(&t465, 0, sizeof t465);
                                GM_ST(uint8_t, ((uint8_t *)&t465 + 0), ((uint8_t)3ULL));
                                GM_ST(uint16_t, ((uint8_t *)&t465 + 8), ((uint16_t)64ULL));
                                gm_t88rcucg1b7uk t466 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t464, &t465);
                                if (gm_unw.kind) {
                                    goto gm_ll419;
                                }
                                gm_t9qkh0rsooc8d t467 = gm_f_unify_t2qjv2p((void *)NULL, cx, NULL, t460, t461, t463, t466);
                                if (gm_unw.kind) {
                                    goto gm_ll419;
                                }
                            }
                        }
                        goto ix446;
ie446:;
ix446:;
                    }
                    {
                        void * t468 = gm_l0;
                        void * t469 = gm_l53;
                        gm_t88rcucg1b7uk t470 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t469) + 0));
                        void * t471 = gm_l53;
                        gm_tl874t2atnlh3 t472 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t471) + 72));
                        gm_t88rcucg1b7uk t473 = gm_f_settle_h9ikvs5((void *)NULL, cx, NULL, t468, t470, t472);
                        if (gm_unw.kind) {
                            goto gm_ll419;
                        }
                        gm_l55 = t473;
                    }
                    {
                        void * t475 = gm_l5;
                        gm_t88rcucg1b7uk t476 = gm_l55;
                        uint8_t t477 = gm_f_has_param_u9t0efs(t475, GM_CONV(gm_csguankko4lmhd, t476));
                        if (!t477) goto ie474;
                        {
                            goto lc415;
                        }
                        goto ix474;
ie474:;
ix474:;
                    }
                    {
                        void * t480 = gm_l0;
                        void * t481 = GM_LD(void *, (((uint8_t *)t480) + 0));
                        void * t482 = gm_l52;
                        gm_t88rcucg1b7uk t483 = gm_l55;
                        gm_th2lqbo77pvsc t484;
                        gm_f_check_spec_4vp7jhg((void *)&t484, (void *)NULL, cx, NULL, t481, t482, t483);
                        if (gm_unw.kind) {
                            goto gm_ll419;
                        }
                        if (!((((uint8_t *)&t484))[16] != 0)) goto ie479;
                        gm_b16_8 t485 = GM_LD(gm_b16_8, ((uint8_t *)&t484));
                        gm_l56 = t485;
                        {
                            void * t487 = gm_l0;
                            void * t488 = gm_l53;
                            gm_tl874t2atnlh3 t489 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t488) + 72));
                            gm_b16_8 t490 = gm_l56;
                            gm_b0_1 t491 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t487, t489, t490);
                            if (gm_unw.kind) {
                                goto gm_ll419;
                            }
                        }
                        goto ix479;
ie479:;
ix479:;
                    }
                    goto me422;
mn423:;
                    gm_trap("no match arm matched the scrutinee", gm_src6d57grm686e3, 432, 5);
me422:;
                }
lc415:;
                {
                    uint64_t t493 = gm_l50;
                    uint64_t r494; bool o495 = __builtin_add_overflow(t493, ((uint64_t)1ULL), &r494);
                    uint64_t t496 = r494;
                    gm_l50 = t496;
                }
            }
gm_ll419:;
            if (gm_unw.frame == &F.h && gm_unw.label == 5) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb415; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc415; }
            }
            goto gm_lad;
lb415:;
        }
    }
    {
        gm_t0uuhaljsf410 t497;
        gm_b16_8 t498 = GM_LD(gm_b16_8, (((uint8_t *)&F.l13) + 0));
        GM_ST(gm_b16_8, (uint8_t *)&t497 + 0, t498);
        gm_b16_8 t499 = GM_LD(gm_b16_8, (((uint8_t *)&F.l13) + 16));
        GM_ST(gm_b16_8, (uint8_t *)&t497 + 16, t499);
        gm_b16_8 t500 = GM_LD(gm_b16_8, (((uint8_t *)&F.l17) + 0));
        GM_ST(gm_b16_8, (uint8_t *)&t497 + 32, t500);
        gm_b16_8 t501 = GM_LD(gm_b16_8, (((uint8_t *)&F.l16) + 0));
        GM_ST(gm_b16_8, (uint8_t *)&t497 + 48, t501);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_ti6pdj2sunpe4 *)gm_out) + 65, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_ti6pdj2sunpe4 *)gm_out) + 66, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_ti6pdj2sunpe4 *)gm_out) + 68, 0);
        ((uint8_t *)&(*(gm_ti6pdj2sunpe4 *)gm_out))[64] = 1;
        GM_ST(gm_t0uuhaljsf410, (uint8_t *)&(*(gm_ti6pdj2sunpe4 *)gm_out) + 0, t497);
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
__attribute__((aligned(4))) void * gm_f_write_deferred_kbicf34(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, void * a2, gm_b16_8 a3, gm_tl874t2atnlh3 a4) {
    struct gm_fr_write_deferred_kbicf34 F;
    void * gm_l0; /* cx */
    void * gm_l2; /* t */
    gm_b16_8 gm_l3; /* rest */
    gm_tl874t2atnlh3 gm_l4; /* span */
    void * gm_l5; /* tys */
    void * gm_l6; /* args */
    void * gm_l8; /* v */
    void * gm_l9; /* a */
    gm_b16_8 gm_l11; /* <tmp> */
    uint64_t gm_l12; /* <tmp> */
    gm_b16_8 gm_l16; /* <tmp> */
    uint64_t gm_l17; /* <tmp> */
    gm_t88rcucg1b7uk gm_l19; /* ty */
    void * gm_l20; /* a */
    gm_t88rcucg1b7uk gm_l21; /* ty */
    uint32_t gm_l22; /* tmp */
    GM_STACK_CHECK(&F, 1657u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    {
        void * t2 = gm_l0;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 0));
        void * t4 = (void *)(((uint8_t *)t3) + 8);
        gm_l5 = t4;
    }
    {
        gm_l6 = ((void *)0);
    }
    {
        uint64_t t6 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
        uint8_t t7 = ((t6 == ((uint64_t)1ULL)));
        if (!t7) goto ie5;
        uint64_t t8 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t9 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
        if (t8 >= (uintptr_t)(t9)) gm_trap_index(t8, t9, gm_src6d57grm686e3, 454, 40);
        uint8_t t10 = GM_LD(uint8_t, ((((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t8 * 144) + 0) + 0));
        if (t10 != ((uint8_t)2ULL)) goto ie5;
        gm_tc3oht4ksth9d t11 = GM_LD(gm_tc3oht4ksth9d, ((((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t8 * 144) + 0) + 8));
        F.l7 = t11;
        {
            void * t13 = gm_l0;
            void * t14 = (void *)((uint8_t *)&F.l7);
            gm_tkn42u7na3t81 t15;
            memset(&t15, 0, sizeof t15);
            void * t16 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t13, t14, t15);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l8 = t16;
        }
        {
            void * t17 = gm_l0;
            void * t18 = gm_l8;
            gm_tl874t2atnlh3 t19 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l7) + 72));
            void * t20 = gm_f_elements_tuple_gdm5n6q((void *)NULL, cx, NULL, t17, t18, t19);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            void * t21;
            if ((GM_LD(void *, ((uint8_t *)&t20)) != 0)) {
                void * t22 = GM_LD(void *, (((uint8_t *)&t20) + 0));
                t21 = t22;
            } else {
                void * t23 = gm_l0;
                void * t24 = GM_LD(void *, (((uint8_t *)t23) + 0));
                gm_tl874t2atnlh3 t25 = gm_l4;
                void * t26 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t24, t25);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t27 = 0;
                F.ret = t26;
                goto gm_fret;
            }
            gm_l9 = t21;
        }
        {
            void * t28 = gm_l9;
            gm_l6 = t28;
        }
        goto ix5;
ie5:;
        uint64_t t30 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
        uint8_t t31 = (t30 > ((uint64_t)0ULL));
        if (!t31) goto ie29;
        {
            gm_f_list_vp1djv1_2((void *)&F.l10, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            {
                gm_b16_8 t34 = gm_l3;
                gm_l11 = t34;
            }
            {
                gm_l12 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t36 = gm_l12;
                    uint64_t t37 = GM_LD(uintptr_t, ((uint8_t *)&gm_l11) + 8);
                    uint8_t t38 = (t36 < t37);
                    if (!t38) goto lb35;
                    {
                        uint64_t t41 = gm_l12;
                        uint64_t t42 = (uintptr_t)t41;
                        uint64_t t43 = GM_LD(uintptr_t, ((uint8_t *)&gm_l11) + 8);
                        gm_t7te2tdpebbt4 t46 = GM_LD(gm_t7te2tdpebbt4, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l11)) + t42 * 144));
                        F.l13 = t46;
                        {
                            uint8_t t49 = GM_LD(uint8_t, ((((uint8_t *)&F.l13) + 0) + 0));
                            if (t49 != ((uint8_t)0ULL)) goto ie48;
                            gm_tc3oht4ksth9d t50 = GM_LD(gm_tc3oht4ksth9d, ((((uint8_t *)&F.l13) + 0) + 8));
                            F.l14 = t50;
                            {
                                void * t52 = (void *)((uint8_t *)&F.l10);
                                void * t53 = gm_l0;
                                void * t54 = (void *)((uint8_t *)&F.l14);
                                gm_tkn42u7na3t81 t55;
                                memset(&t55, 0, sizeof t55);
                                void * t56 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t53, t54, t55);
                                if (gm_unw.kind) {
                                    goto gm_ll39;
                                }
                                gm_tftb1odc739t5 t57 = gm_f_push_d943vgp_2(t52, &(*(gm_t69m56meqefs8 *)((uint8_t *)t56)));
                                if (((((uint8_t *)&t57))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t57) + 1, gm_src6d57grm686e3, 465, 13);
                                gm_b0_1 t58 = GM_LD(gm_b0_1, (((uint8_t *)&t57) + 1));
                            }
                            goto ix48;
ie48:;
                            {
                                {
                                    void * t61 = gm_l0;
                                    gm_tl874t2atnlh3 t62 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l13) + 128));
                                    gm_b16_8 t63;
                                    GM_ST(void *, (uint8_t *)&t63, (void *)(gm_smom6v4b4s28c));
                                    GM_ST(uintptr_t, (uint8_t *)&t63 + 8, (uintptr_t)(51));
                                    gm_b0_1 t64 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t61, t62, t63);
                                    if (gm_unw.kind) {
                                        goto gm_ll39;
                                    }
                                }
                                {
                                    void * t65 = gm_l0;
                                    void * t66 = GM_LD(void *, (((uint8_t *)t65) + 0));
                                    gm_tl874t2atnlh3 t67 = gm_l4;
                                    void * t68 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t66, t67);
                                    if (gm_unw.kind) {
                                        goto gm_ll39;
                                    }
                                    uint8_t t69 = 0;
                                    F.ret = t68;
                                    goto gm_fret;
                                }
                            }
ix48:;
                        }
                        goto me44;
mn45:;
                        gm_trap("no match arm matched the scrutinee", gm_src6d57grm686e3, 460, 9);
me44:;
                    }
lc35:;
                    {
                        uint64_t t71 = gm_l12;
                        uint64_t r72; bool o73 = __builtin_add_overflow(t71, ((uint64_t)1ULL), &r72);
                        uint64_t t74 = r72;
                        gm_l12 = t74;
                    }
                }
gm_ll39:;
                if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb35; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc35; }
                }
                goto gm_lad;
lb35:;
            }
        }
        {
            gm_f_list_nof7vfj((void *)&F.l15, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            {
                gm_b16_8 t76 = GM_LD(gm_b16_8, (((uint8_t *)&F.l10) + 0));
                gm_l16 = t76;
            }
            {
                gm_l17 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t78 = gm_l17;
                    uint64_t t79 = GM_LD(uintptr_t, ((uint8_t *)&gm_l16) + 8);
                    uint8_t t80 = (t78 < t79);
                    if (!t80) goto lb77;
                    {
                        uint64_t t83 = gm_l17;
                        uint64_t t84 = (uintptr_t)t83;
                        uint64_t t85 = GM_LD(uintptr_t, ((uint8_t *)&gm_l16) + 8);
                        gm_t69m56meqefs8 t88 = GM_LD(gm_t69m56meqefs8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l16)) + t84 * 88));
                        F.l18 = t88;
                        {
                            void * t90 = (void *)((uint8_t *)&F.l15);
                            void * t91 = gm_l0;
                            void * t92 = (void *)(((uint8_t *)t91) + 96);
                            void * t93 = gm_l0;
                            void * t94 = GM_LD(void *, (((uint8_t *)t93) + 0));
                            gm_t88rcucg1b7uk t95 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l18) + 0));
                            gm_t88rcucg1b7uk t96 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t92, t94, t95);
                            if (gm_unw.kind) {
                                goto gm_ll81;
                            }
                            gm_tftb1odc739t5 t97 = gm_f_push_k3l9nic(t90, GM_CONV(gm_csguankko4lmhd, t96));
                            if (((((uint8_t *)&t97))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t97) + 1, gm_src6d57grm686e3, 468, 32);
                            gm_b0_1 t98 = GM_LD(gm_b0_1, (((uint8_t *)&t97) + 1));
                        }
                        goto me86;
mn87:;
                        gm_trap("no match arm matched the scrutinee", gm_src6d57grm686e3, 468, 9);
me86:;
                    }
lc77:;
                    {
                        uint64_t t100 = gm_l17;
                        uint64_t r101; bool o102 = __builtin_add_overflow(t100, ((uint64_t)1ULL), &r101);
                        uint64_t t103 = r101;
                        gm_l17 = t103;
                    }
                }
gm_ll81:;
                if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb77; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc77; }
                }
                goto gm_lad;
lb77:;
            }
        }
        {
            void * t104 = gm_l5;
            gm_t6j6smj7qoctu t105;
            gm_b16_8 t106 = GM_LD(gm_b16_8, (((uint8_t *)&F.l15) + 0));
            gm_b16_8 t107 = GM_LD(gm_b16_8, ((uint8_t *)&t106));
            GM_ST(gm_b16_8, (uint8_t *)&t105 + 0, t107);
            gm_b16_8 t108 = gm_f_empty_slice_5nn7ph0();
            gm_b16_8 t109 = GM_LD(gm_b16_8, ((uint8_t *)&t108));
            GM_ST(gm_b16_8, (uint8_t *)&t105 + 16, t109);
            gm_t53at80pfdus3 t110;
            GM_ST(uint8_t, (uint8_t *)&t110 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t110 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t110 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t110 + 40, 0);
            GM_ST(uint64_t, (uint8_t *)&t110 + 48, 0);
            GM_ST(uint8_t, ((uint8_t *)&t110 + 0), ((uint8_t)12ULL));
            GM_ST(gm_t6j6smj7qoctu, ((uint8_t *)&t110 + 8), t105);
            gm_t88rcucg1b7uk t111 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t104, &t110);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l19 = t111;
        }
        {
            gm_t88rcucg1b7uk t112 = gm_l19;
            gm_b16_8 t113 = GM_LD(gm_b16_8, (((uint8_t *)&F.l10) + 0));
            gm_teti411eujvf4 t114;
            memset(&t114, 0, sizeof t114);
            GM_ST(uint8_t, ((uint8_t *)&t114 + 0), ((uint8_t)35ULL));
            GM_ST(gm_b16_8, ((uint8_t *)&t114 + 8), t113);
            gm_tl874t2atnlh3 t115 = gm_l4;
            void * t116 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t112, &t114, t115);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l6 = t116;
        }
        goto ix29;
ie29:;
ix29:;
ix5:;
    }
    {
        gm_t88rcucg1b7uk t117;
        void * t119 = gm_l6;
        if (!(GM_LD(void *, ((uint8_t *)&t119)) != 0)) goto ie118;
        void * t120 = GM_LD(void *, ((uint8_t *)&t119));
        gm_l20 = t120;
        void * t122 = gm_l0;
        void * t123 = (void *)(((uint8_t *)t122) + 96);
        void * t124 = gm_l0;
        void * t125 = GM_LD(void *, (((uint8_t *)t124) + 0));
        void * t126 = gm_l20;
        gm_t88rcucg1b7uk t127 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t126) + 0));
        gm_t88rcucg1b7uk t128 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t123, t125, t127);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t88rcucg1b7uk t129 = t128;
        t117 = t129;
        goto ix118;
ie118:;
        void * t131 = gm_l5;
        gm_t88rcucg1b7uk t132 = gm_f_unit_2age34u((void *)NULL, cx, NULL, t131);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t88rcucg1b7uk t133 = t132;
        t117 = t133;
ix118:;
        gm_l21 = t117;
    }
    {
        void * t134 = gm_l0;
        gm_t88rcucg1b7uk t135 = gm_l21;
        gm_tl874t2atnlh3 t136 = gm_l4;
        uint32_t t137 = gm_f_temp_ilbvj8q((void *)NULL, cx, NULL, t134, t135, t136);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l22 = t137;
    }
    {
        void * t138 = gm_l5;
        gm_t88rcucg1b7uk t139 = gm_f_unit_2age34u((void *)NULL, cx, NULL, t138);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t9i38ih07bojq t140;
        memset((uint8_t *)&t140 + 60, 0, 4);
        memcpy((uint8_t *)&t140 + 0, ((uint8_t *)a1), 40);
        void * t141 = gm_l2;
        GM_ST(void *, (uint8_t *)&t140 + 40, t141);
        void * t142 = gm_l6;
        GM_ST(void *, (uint8_t *)&t140 + 48, t142);
        uint32_t t143 = gm_l22;
        GM_ST(uint32_t, (uint8_t *)&t140 + 56, t143);
        gm_tq7s98ihonkju t144 = gm_f_new_ges19gd((void *)NULL, cx, NULL, &t140, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_k1g2gld0doi)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t144))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t144) + 8, gm_src6d57grm686e3, 474, 46);
        void * t145 = GM_LD(void *, (((uint8_t *)&t144) + 8));
        gm_teti411eujvf4 t146;
        memset(&t146, 0, sizeof t146);
        GM_ST(uint8_t, ((uint8_t *)&t146 + 0), ((uint8_t)56ULL));
        GM_ST(void *, ((uint8_t *)&t146 + 8), t145);
        gm_tl874t2atnlh3 t147 = gm_l4;
        void * t148 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t139, &t146, t147);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t149 = 0;
        F.ret = t148;
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
