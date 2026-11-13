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
typedef struct gm_cs9ufftihtbpo2 { uint32_t f0; uint32_t f1; } gm_cs9ufftihtbpo2;
typedef struct gm_cseahvg8v7p0vs { uint32_t f0; uint32_t f1; uint32_t f2; } gm_cseahvg8v7p0vs;
typedef struct gm_csguankko4lmhd { uint32_t f0; } gm_csguankko4lmhd;
typedef struct { uint64_t f0; uint64_t f1; } gm_t2lqu8jsj511d;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint32_t f0; uint8_t f1; uint8_t pe[3]; } gm_t9805qtonbsag;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint8_t f0; uint8_t p1[7]; void * f1; void * f2; } gm_ta612fil812pm;
typedef struct { void * f0; void * f1; void * f2; uint8_t f3; uint8_t pe[7]; } gm_tao63pk3r18v5;
typedef struct { void * f0; uint32_t f1; uint8_t pe[4]; } gm_tbl8il92l97ut;
typedef struct { uint32_t f0; uint32_t f1; } gm_tcc1rq4n1avb2;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint8_t f0; uint8_t p1[7]; void * f1; } gm_tgtub0v94d5u5;
typedef struct { uint8_t f0; uint8_t p1[3]; union { uint32_t v0; uint32_t v1; } f1; } gm_tj2m9dbh1ekv6;
typedef struct { uint8_t f0; uint8_t p1[1]; uint16_t f1; } gm_tjcj4j2gbh6c8;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { void * f0; void * f1; uint8_t f2; uint8_t pe[7]; } gm_tlcl4u558l6p5;
typedef struct { uint8_t f0; uint8_t p1[3]; union { uint32_t v0; } f1; } gm_tmn7n0n9raou5;
typedef struct { uint64_t f0; uint32_t f1; uint8_t pe[4]; } gm_tn68el8d14vku;
typedef struct { uint8_t f0; uint8_t f1; uint8_t f2; } gm_toh0ootodhjue;
typedef struct { void * f0; uint8_t f1; uint8_t pe[7]; } gm_tsplfr33dsvji;
typedef struct { uint32_t f0; uint8_t p1[4]; void * f1; } gm_ttggq7ng4kp3p;
typedef struct gm_cs7e9ppsd384bj { uint32_t f0; gm_csguankko4lmhd f1; } gm_cs7e9ppsd384bj;
_Static_assert(sizeof(gm_cs9ufftihtbpo2) == 8 && _Alignof(gm_cs9ufftihtbpo2) == 4 && offsetof(gm_cs9ufftihtbpo2, f0) == 0 && offsetof(gm_cs9ufftihtbpo2, f1) == 4, "layout of `LineCol`");
_Static_assert(sizeof(gm_cseahvg8v7p0vs) == 12 && _Alignof(gm_cseahvg8v7p0vs) == 4 && offsetof(gm_cseahvg8v7p0vs, f0) == 0 && offsetof(gm_cseahvg8v7p0vs, f1) == 4 && offsetof(gm_cseahvg8v7p0vs, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_csguankko4lmhd) == 4 && _Alignof(gm_csguankko4lmhd) == 4 && offsetof(gm_csguankko4lmhd, f0) == 0, "layout of `Mutex`");
typedef struct { void * f0; gm_t9805qtonbsag f1; uint32_t f2; uint8_t pe[4]; } gm_t083kd7b1kkag;
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_t08olq97rscn3;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; } gm_t0uuhaljsf410;
_Static_assert(sizeof(gm_t2lqu8jsj511d) == 16 && _Alignof(gm_t2lqu8jsj511d) == 8 && offsetof(gm_t2lqu8jsj511d, f0) == 0 && offsetof(gm_t2lqu8jsj511d, f1) == 8, "layout of `(u64, u64)`");
typedef struct { gm_b16_8 f0; uint64_t f1; } gm_t3877pk7lv748;
typedef struct { void * f0; gm_t9805qtonbsag f1; void * f2; } gm_t3dcoc7077t1q;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; uint8_t f2; uint8_t pe[3]; } gm_t3rcjibl8kkg4;
typedef struct { gm_tj2m9dbh1ekv6 f0; uint8_t f1; uint8_t pe[3]; } gm_t46lmulm8gaj3;
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; gm_tl874t2atnlh3 f2; void * f3; gm_t9805qtonbsag f4; uint8_t f5; uint8_t pe[7]; } gm_t5v0c8ia6a5mo;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; void * f2; void * f3; } gm_t710lsrq1m8gk;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_t79rbdv4rafe5;
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
typedef struct { gm_tl874t2atnlh3 f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tbgd14esf0jvi;
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
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; } gm_tgglga8ro3isc;
_Static_assert(sizeof(gm_tgtub0v94d5u5) == 16 && _Alignof(gm_tgtub0v94d5u5) == 8 && offsetof(gm_tgtub0v94d5u5, f0) == 0 && offsetof(gm_tgtub0v94d5u5, f1) == 8, "layout of `(is_const: bool, inner: ^Expr)`");
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; gm_tl874t2atnlh3 f2; } gm_thsh8t5u55gps;
typedef struct { gm_b16_8 f0; gm_t9qkh0rsooc8d f1; uint8_t pe[7]; } gm_tibupt4dmcqai;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
_Static_assert(sizeof(gm_tj2m9dbh1ekv6) == 8 && _Alignof(gm_tj2m9dbh1ekv6) == 4 && offsetof(gm_tj2m9dbh1ekv6, f0) == 0 && offsetof(gm_tj2m9dbh1ekv6, f1) == 4, "layout of `IntSuffix`");
_Static_assert(sizeof(gm_tjcj4j2gbh6c8) == 4 && _Alignof(gm_tjcj4j2gbh6c8) == 2 && offsetof(gm_tjcj4j2gbh6c8, f0) == 0 && offsetof(gm_tjcj4j2gbh6c8, f1) == 2, "layout of `(signed: bool, bits: u16)`");
typedef struct { void * f0; gm_b16_8 f1; } gm_tkaapcii86udk;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t f1; uint8_t pe[3]; } gm_tkn42u7na3t81;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_tlcl4u558l6p5) == 24 && _Alignof(gm_tlcl4u558l6p5) == 8 && offsetof(gm_tlcl4u558l6p5, f0) == 0 && offsetof(gm_tlcl4u558l6p5, f1) == 8 && offsetof(gm_tlcl4u558l6p5, f2) == 16, "layout of `(lo: ^Expr, hi: ^Expr, inclusive: bool)`");
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; uint8_t pe[4]; } gm_tlcm4lvu3g1a8;
typedef struct { void * f0; gm_b16_8 f1; uint8_t f2; uint8_t pe[7]; } gm_tlljg61a2ctgq;
_Static_assert(sizeof(gm_tmn7n0n9raou5) == 8 && _Alignof(gm_tmn7n0n9raou5) == 4 && offsetof(gm_tmn7n0n9raou5, f0) == 0 && offsetof(gm_tmn7n0n9raou5, f1) == 4, "layout of `Coercion`");
_Static_assert(sizeof(gm_tn68el8d14vku) == 16 && _Alignof(gm_tn68el8d14vku) == 8 && offsetof(gm_tn68el8d14vku, f0) == 0 && offsetof(gm_tn68el8d14vku, f1) == 8, "layout of `(i: usize, x: u32)`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t88rcucg1b7uk v0; } f1; } gm_tnnjeerolos0a;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
_Static_assert(sizeof(gm_toh0ootodhjue) == 3 && _Alignof(gm_toh0ootodhjue) == 1 && offsetof(gm_toh0ootodhjue, f0) == 0 && offsetof(gm_toh0ootodhjue, f1) == 1 && offsetof(gm_toh0ootodhjue, f2) == 2, "layout of `Restr`");
typedef struct { gm_b16_8 f0; void * f1; } gm_tpe1hv3fekn05;
typedef struct { uint32_t f0; gm_t88rcucg1b7uk f1; } gm_tplpbqf3duvn3;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; void * v1; } f1; } gm_tq7s98ihonkju;
typedef struct { gm_b16_8 f0; void * f1; void * f2; } gm_trl2khmuphncd;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_t9qkh0rsooc8d v3; } f1; } gm_ts6oqppjh0ba3;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_ts78ca2amthgu;
_Static_assert(sizeof(gm_tsplfr33dsvji) == 16 && _Alignof(gm_tsplfr33dsvji) == 8 && offsetof(gm_tsplfr33dsvji, f0) == 0 && offsetof(gm_tsplfr33dsvji, f1) == 8, "layout of `(expr: ^Expr, is_result: bool)`");
_Static_assert(sizeof(gm_ttggq7ng4kp3p) == 16 && _Alignof(gm_ttggq7ng4kp3p) == 8 && offsetof(gm_ttggq7ng4kp3p, f0) == 0 && offsetof(gm_ttggq7ng4kp3p, f1) == 8, "layout of `(index: u32, payload: ?^Value)`");
typedef struct { void * f0; uint32_t f1; gm_tl874t2atnlh3 f2; } gm_tupk62cr6lbdu;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_tcc1rq4n1avb2 v3; uint32_t v4; uint32_t v5; } f1; } gm_tvdg94bk7aa22;
_Static_assert(sizeof(gm_cs7e9ppsd384bj) == 8 && _Alignof(gm_cs7e9ppsd384bj) == 4 && offsetof(gm_cs7e9ppsd384bj, f0) == 0 && offsetof(gm_cs7e9ppsd384bj, f1) == 4, "layout of `WithSlot`");
_Static_assert(sizeof(gm_t083kd7b1kkag) == 24 && _Alignof(gm_t083kd7b1kkag) == 8 && offsetof(gm_t083kd7b1kkag, f0) == 0 && offsetof(gm_t083kd7b1kkag, f1) == 8 && offsetof(gm_t083kd7b1kkag, f2) == 16, "layout of `TryExpr`");
_Static_assert(sizeof(gm_t08olq97rscn3) == 32 && _Alignof(gm_t08olq97rscn3) == 8 && offsetof(gm_t08olq97rscn3, f0) == 0 && offsetof(gm_t08olq97rscn3, f1) == 16, "layout of `File`");
_Static_assert(sizeof(gm_t0uuhaljsf410) == 64 && _Alignof(gm_t0uuhaljsf410) == 8 && offsetof(gm_t0uuhaljsf410, f0) == 0 && offsetof(gm_t0uuhaljsf410, f1) == 16 && offsetof(gm_t0uuhaljsf410, f2) == 32 && offsetof(gm_t0uuhaljsf410, f3) == 48, "layout of `(string, string, string, string)`");
typedef struct { gm_t08olq97rscn3 f0; uint8_t f1; uint8_t pe[7]; } gm_t1s75i08qtptd;
_Static_assert(sizeof(gm_t3877pk7lv748) == 24 && _Alignof(gm_t3877pk7lv748) == 8 && offsetof(gm_t3877pk7lv748, f0) == 0 && offsetof(gm_t3877pk7lv748, f1) == 16, "layout of `WasmReader`");
_Static_assert(sizeof(gm_t3dcoc7077t1q) == 24 && _Alignof(gm_t3dcoc7077t1q) == 8 && offsetof(gm_t3dcoc7077t1q, f0) == 0 && offsetof(gm_t3dcoc7077t1q, f1) == 8 && offsetof(gm_t3dcoc7077t1q, f2) == 16, "layout of `ElseExpr`");
_Static_assert(sizeof(gm_t3rcjibl8kkg4) == 12 && _Alignof(gm_t3rcjibl8kkg4) == 4 && offsetof(gm_t3rcjibl8kkg4, f0) == 0 && offsetof(gm_t3rcjibl8kkg4, f1) == 4 && offsetof(gm_t3rcjibl8kkg4, f2) == 8, "layout of `(a: Ty, b: Ty, negated: bool)`");
typedef struct { gm_tiilmfd16v3fc f0; uint32_t f1; uint8_t pe[4]; } gm_t419mo45g7j0a;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tcmkompbl09il v0; gm_tiilmfd16v3fc v1; gm_b16_8 v2; } f1; } gm_t428ue2u3hf5g;
_Static_assert(sizeof(gm_t46lmulm8gaj3) == 12 && _Alignof(gm_t46lmulm8gaj3) == 4 && offsetof(gm_t46lmulm8gaj3, f0) == 0 && offsetof(gm_t46lmulm8gaj3, f1) == 8, "layout of `?IntSuffix`");
_Static_assert(sizeof(gm_t5v0c8ia6a5mo) == 56 && _Alignof(gm_t5v0c8ia6a5mo) == 8 && offsetof(gm_t5v0c8ia6a5mo, f0) == 0 && offsetof(gm_t5v0c8ia6a5mo, f1) == 16 && offsetof(gm_t5v0c8ia6a5mo, f2) == 20 && offsetof(gm_t5v0c8ia6a5mo, f3) == 32 && offsetof(gm_t5v0c8ia6a5mo, f4) == 40 && offsetof(gm_t5v0c8ia6a5mo, f5) == 48, "layout of `ParamInfo`");
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_t710lsrq1m8gk) == 40 && _Alignof(gm_t710lsrq1m8gk) == 8 && offsetof(gm_t710lsrq1m8gk, f0) == 0 && offsetof(gm_t710lsrq1m8gk, f1) == 8 && offsetof(gm_t710lsrq1m8gk, f2) == 24 && offsetof(gm_t710lsrq1m8gk, f3) == 32, "layout of `WithExpr`");
_Static_assert(sizeof(gm_t79rbdv4rafe5) == 40 && _Alignof(gm_t79rbdv4rafe5) == 8 && offsetof(gm_t79rbdv4rafe5, f0) == 0 && offsetof(gm_t79rbdv4rafe5, f1) == 8 && offsetof(gm_t79rbdv4rafe5, f2) == 24, "layout of `Block`");
_Static_assert(sizeof(gm_t8ghgl2jj0qha) == 16 && _Alignof(gm_t8ghgl2jj0qha) == 8 && offsetof(gm_t8ghgl2jj0qha, f0) == 0 && offsetof(gm_t8ghgl2jj0qha, f1) == 8, "layout of `(op: UnOp, operand: ^Expr)`");
_Static_assert(sizeof(gm_t8jsc4n9vp2ib) == 16 && _Alignof(gm_t8jsc4n9vp2ib) == 8 && offsetof(gm_t8jsc4n9vp2ib, f0) == 0 && offsetof(gm_t8jsc4n9vp2ib, f1) == 8, "layout of `(kind: Coercion, expr: ^Expr)`");
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; gm_tnnjeerolos0a f2; uint8_t pe[4]; } gm_t8nckeqfenl8a;
_Static_assert(sizeof(gm_taari1s0m51a7) == 2 && _Alignof(gm_taari1s0m51a7) == 1 && offsetof(gm_taari1s0m51a7, f0) == 0 && offsetof(gm_taari1s0m51a7, f1) == 1, "layout of `?FloatSuffix`");
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
typedef struct { gm_tesaltf0mhcki f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tandvbt63uk7n;
_Static_assert(sizeof(gm_tb40qn7hk0tl9) == 40 && _Alignof(gm_tb40qn7hk0tl9) == 8 && offsetof(gm_tb40qn7hk0tl9, f0) == 0 && offsetof(gm_tb40qn7hk0tl9, f1) == 16 && offsetof(gm_tb40qn7hk0tl9, f2) == 20 && offsetof(gm_tb40qn7hk0tl9, f3) == 24, "layout of `SourceLocation`");
typedef struct { uint8_t f0; uint8_t p1[7]; gm_trqfvmjtt4sij f1; } gm_tbci5ospk5nnn;
_Static_assert(sizeof(gm_tbgd14esf0jvi) == 32 && _Alignof(gm_tbgd14esf0jvi) == 8 && offsetof(gm_tbgd14esf0jvi, f0) == 0 && offsetof(gm_tbgd14esf0jvi, f1) == 16, "layout of `Edit`");
_Static_assert(sizeof(gm_tbngattl1fo51) == 8 && _Alignof(gm_tbngattl1fo51) == 4 && offsetof(gm_tbngattl1fo51, f0) == 0 && offsetof(gm_tbngattl1fo51, f1) == 4, "layout of `(is_const: bool, pointee: Ty)`");
_Static_assert(sizeof(gm_tcmkompbl09il) == 24 && _Alignof(gm_tcmkompbl09il) == 8 && offsetof(gm_tcmkompbl09il, f0) == 0 && offsetof(gm_tcmkompbl09il, f1) == 8, "layout of `(ty: Ty, bytes: string)`");
_Static_assert(sizeof(gm_tcmtrmhj7ak4u) == 12 && _Alignof(gm_tcmtrmhj7ak4u) == 4 && offsetof(gm_tcmtrmhj7ak4u, f0) == 0 && offsetof(gm_tcmtrmhj7ak4u, f1) == 4 && offsetof(gm_tcmtrmhj7ak4u, f2) == 8, "layout of `(op: LenOp, lhs: Ty, rhs: Ty)`");
typedef struct { uint64_t f0; gm_tf1bjg81u0glv f1; } gm_td38of7a349nj;
typedef struct { gm_trqfvmjtt4sij f0; gm_trqfvmjtt4sij f1; } gm_td7pkamr64l0c;
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t p2[4]; gm_tnpifv20irnph f2; uint8_t f3; uint8_t pe[7]; } gm_tddtjfggdat0d;
_Static_assert(sizeof(gm_te9hrk0u35rev) == 12 && _Alignof(gm_te9hrk0u35rev) == 4 && offsetof(gm_te9hrk0u35rev, f0) == 0 && offsetof(gm_te9hrk0u35rev, f1) == 4, "layout of `Target`");
typedef struct { gm_te9hrk0u35rev f0; uint8_t p1[4]; void * f1; } gm_teo9d4mncjfdu;
_Static_assert(sizeof(gm_tesaltf0mhcki) == 4 && _Alignof(gm_tesaltf0mhcki) == 1 && offsetof(gm_tesaltf0mhcki, f0) == 0 && offsetof(gm_tesaltf0mhcki, f1) == 1, "layout of `SoaOp`");
typedef struct { gm_t08olq97rscn3 f0; gm_th2lqbo77pvsc f1; } gm_tet9guhev5r4u;
_Static_assert(sizeof(gm_tf1bjg81u0glv) == 24 && _Alignof(gm_tf1bjg81u0glv) == 8 && offsetof(gm_tf1bjg81u0glv, f0) == 0 && offsetof(gm_tf1bjg81u0glv, f1) == 8, "layout of `(i: usize, x: string)`");
_Static_assert(sizeof(gm_tfeab279c16rr) == 48 && _Alignof(gm_tfeab279c16rr) == 8 && offsetof(gm_tfeab279c16rr, f0) == 0 && offsetof(gm_tfeab279c16rr, f1) == 16 && offsetof(gm_tfeab279c16rr, f2) == 32, "layout of `Toolchain`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_tgglga8ro3isc) == 8 && _Alignof(gm_tgglga8ro3isc) == 4 && offsetof(gm_tgglga8ro3isc, f0) == 0 && offsetof(gm_tgglga8ro3isc, f1) == 4, "layout of `(err: Ty, ok: Ty)`");
typedef struct { uint64_t f0; gm_t5v0c8ia6a5mo f1; } gm_tgsvmddj3e85e;
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
_Static_assert(sizeof(gm_thsh8t5u55gps) == 20 && _Alignof(gm_thsh8t5u55gps) == 4 && offsetof(gm_thsh8t5u55gps, f0) == 0 && offsetof(gm_thsh8t5u55gps, f1) == 4 && offsetof(gm_thsh8t5u55gps, f2) == 8, "layout of `(from: Ty, to: Ty, span: Span)`");
_Static_assert(sizeof(gm_tibupt4dmcqai) == 24 && _Alignof(gm_tibupt4dmcqai) == 8 && offsetof(gm_tibupt4dmcqai, f0) == 0 && offsetof(gm_tibupt4dmcqai, f1) == 16, "layout of `StrLit`");
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
typedef struct { void * f0; gm_t08olq97rscn3 f1; } gm_tioie2ju3q4ul;
_Static_assert(sizeof(gm_tkaapcii86udk) == 24 && _Alignof(gm_tkaapcii86udk) == 8 && offsetof(gm_tkaapcii86udk, f0) == 0 && offsetof(gm_tkaapcii86udk, f1) == 8, "layout of `Place`");
_Static_assert(sizeof(gm_tkn42u7na3t81) == 8 && _Alignof(gm_tkn42u7na3t81) == 4 && offsetof(gm_tkn42u7na3t81, f0) == 0 && offsetof(gm_tkn42u7na3t81, f1) == 4, "layout of `?Ty`");
_Static_assert(sizeof(gm_tlcm4lvu3g1a8) == 16 && _Alignof(gm_tlcm4lvu3g1a8) == 8 && offsetof(gm_tlcm4lvu3g1a8, f0) == 0 && offsetof(gm_tlcm4lvu3g1a8, f1) == 8, "layout of `(i: usize, x: Ty)`");
_Static_assert(sizeof(gm_tlljg61a2ctgq) == 32 && _Alignof(gm_tlljg61a2ctgq) == 8 && offsetof(gm_tlljg61a2ctgq, f0) == 0 && offsetof(gm_tlljg61a2ctgq, f1) == 8 && offsetof(gm_tlljg61a2ctgq, f2) == 24, "layout of `(callee: ^Expr, args: []Arg, trailing_comma: bool)`");
_Static_assert(sizeof(gm_tnnjeerolos0a) == 8 && _Alignof(gm_tnnjeerolos0a) == 4 && offsetof(gm_tnnjeerolos0a, f0) == 0 && offsetof(gm_tnnjeerolos0a, f1) == 4, "layout of `ArrayKind`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
_Static_assert(sizeof(gm_tpe1hv3fekn05) == 24 && _Alignof(gm_tpe1hv3fekn05) == 8 && offsetof(gm_tpe1hv3fekn05, f0) == 0 && offsetof(gm_tpe1hv3fekn05, f1) == 16, "layout of `(attrs: []Attribute, inner: ^Expr)`");
_Static_assert(sizeof(gm_tplpbqf3duvn3) == 8 && _Alignof(gm_tplpbqf3duvn3) == 4 && offsetof(gm_tplpbqf3duvn3, f0) == 0 && offsetof(gm_tplpbqf3duvn3, f1) == 4, "layout of `WithSlot`");
_Static_assert(sizeof(gm_tq7s98ihonkju) == 16 && _Alignof(gm_tq7s98ihonkju) == 8 && offsetof(gm_tq7s98ihonkju, f0) == 0 && offsetof(gm_tq7s98ihonkju, f1) == 8, "layout of `AllocError!^HandlerDecl`");
typedef struct { gm_tiilmfd16v3fc f0; uint8_t f1; uint8_t pe[7]; } gm_tqu3m2l3n8mnc;
_Static_assert(sizeof(gm_trl2khmuphncd) == 32 && _Alignof(gm_trl2khmuphncd) == 8 && offsetof(gm_trl2khmuphncd, f0) == 0 && offsetof(gm_trl2khmuphncd, f1) == 16 && offsetof(gm_trl2khmuphncd, f2) == 24, "layout of `IfExpr`");
typedef struct { gm_ts6oqppjh0ba3 f0; uint8_t p1[6]; void * f1; void * f2; } gm_trl9d7m1k5lbd;
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; gm_tbgd14esf0jvi v1; } f1; } gm_ts5t31j0bugq1;
_Static_assert(sizeof(gm_ts6oqppjh0ba3) == 2 && _Alignof(gm_ts6oqppjh0ba3) == 1 && offsetof(gm_ts6oqppjh0ba3, f0) == 0 && offsetof(gm_ts6oqppjh0ba3, f1) == 1, "layout of `BinOp`");
_Static_assert(sizeof(gm_ts78ca2amthgu) == 40 && _Alignof(gm_ts78ca2amthgu) == 8 && offsetof(gm_ts78ca2amthgu, f0) == 0 && offsetof(gm_ts78ca2amthgu, f1) == 8 && offsetof(gm_ts78ca2amthgu, f2) == 24, "layout of `(ty: Ty, name: string, span: Span)`");
typedef struct { gm_tvdg94bk7aa22 f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tt8o5kebou62r;
_Static_assert(sizeof(gm_tupk62cr6lbdu) == 24 && _Alignof(gm_tupk62cr6lbdu) == 8 && offsetof(gm_tupk62cr6lbdu, f0) == 0 && offsetof(gm_tupk62cr6lbdu, f1) == 8 && offsetof(gm_tupk62cr6lbdu, f2) == 12, "layout of `(base: ^Expr, index: u32, index_span: Span)`");
_Static_assert(sizeof(gm_tvdg94bk7aa22) == 12 && _Alignof(gm_tvdg94bk7aa22) == 4 && offsetof(gm_tvdg94bk7aa22, f0) == 0 && offsetof(gm_tvdg94bk7aa22, f1) == 4, "layout of `Builtin`");
_Static_assert(sizeof(gm_t1s75i08qtptd) == 40 && _Alignof(gm_t1s75i08qtptd) == 8 && offsetof(gm_t1s75i08qtptd, f0) == 0 && offsetof(gm_t1s75i08qtptd, f1) == 32, "layout of `?Ident`");
_Static_assert(sizeof(gm_t419mo45g7j0a) == 32 && _Alignof(gm_t419mo45g7j0a) == 8 && offsetof(gm_t419mo45g7j0a, f0) == 0 && offsetof(gm_t419mo45g7j0a, f1) == 24, "layout of `(effect_: EffectRef, op: u32)`");
_Static_assert(sizeof(gm_t428ue2u3hf5g) == 32 && _Alignof(gm_t428ue2u3hf5g) == 8 && offsetof(gm_t428ue2u3hf5g, f0) == 0 && offsetof(gm_t428ue2u3hf5g, f1) == 8, "layout of `ConstKey`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tjcj4j2gbh6c8 v0; uint16_t v1; gm_tbngattl1fo51 v2; gm_tbngattl1fo51 v3; gm_tbngattl1fo51 v4; gm_t8nckeqfenl8a v5; gm_t6j6smj7qoctu v6; gm_t88rcucg1b7uk v7; gm_tgglga8ro3isc v8; gm_tddtjfggdat0d v9; gm_tddtjfggdat0d v10; gm_tiilmfd16v3fc v11; gm_t88rcucg1b7uk v12; uint32_t v13; gm_tgglga8ro3isc v14; gm_t428ue2u3hf5g v15; gm_tcmtrmhj7ak4u v16; uint32_t v17; gm_tnpifv20irnph v18; uint32_t v19; } f1; } gm_t53at80pfdus3;
_Static_assert(sizeof(gm_t8nckeqfenl8a) == 24 && _Alignof(gm_t8nckeqfenl8a) == 8 && offsetof(gm_t8nckeqfenl8a, f0) == 0 && offsetof(gm_t8nckeqfenl8a, f1) == 8 && offsetof(gm_t8nckeqfenl8a, f2) == 12, "layout of `(len: u64, elem: Ty, kind: ArrayKind)`");
typedef struct { gm_tbci5ospk5nnn f0; gm_t46lmulm8gaj3 f1; uint8_t f2; uint8_t pe[3]; } gm_ta42iqot1auae;
_Static_assert(sizeof(gm_tandvbt63uk7n) == 24 && _Alignof(gm_tandvbt63uk7n) == 8 && offsetof(gm_tandvbt63uk7n, f0) == 0 && offsetof(gm_tandvbt63uk7n, f1) == 8, "layout of `(op: SoaOp, args: []Expr)`");
_Static_assert(sizeof(gm_tbci5ospk5nnn) == 48 && _Alignof(gm_tbci5ospk5nnn) == 8 && offsetof(gm_tbci5ospk5nnn, f0) == 0 && offsetof(gm_tbci5ospk5nnn, f1) == 8, "layout of `Int`");
typedef struct { gm_t1s75i08qtptd f0; void * f1; } gm_tchr52g0ema53;
typedef struct { gm_ts5t31j0bugq1 f0; uint8_t f1; uint8_t pe[7]; } gm_tck5nas0nf5v8;
_Static_assert(sizeof(gm_td38of7a349nj) == 32 && _Alignof(gm_td38of7a349nj) == 8 && offsetof(gm_td38of7a349nj, f0) == 0 && offsetof(gm_td38of7a349nj, f1) == 8, "layout of `(i: usize, x: IterPre)`");
_Static_assert(sizeof(gm_td7pkamr64l0c) == 80 && _Alignof(gm_td7pkamr64l0c) == 8 && offsetof(gm_td7pkamr64l0c, f0) == 0 && offsetof(gm_td7pkamr64l0c, f1) == 40, "layout of `(q: List(u32), r: List(u32))`");
_Static_assert(sizeof(gm_tddtjfggdat0d) == 48 && _Alignof(gm_tddtjfggdat0d) == 8 && offsetof(gm_tddtjfggdat0d, f0) == 0 && offsetof(gm_tddtjfggdat0d, f1) == 16 && offsetof(gm_tddtjfggdat0d, f2) == 24 && offsetof(gm_tddtjfggdat0d, f3) == 40, "layout of `FnTy`");
_Static_assert(sizeof(gm_teo9d4mncjfdu) == 24 && _Alignof(gm_teo9d4mncjfdu) == 8 && offsetof(gm_teo9d4mncjfdu, f0) == 0 && offsetof(gm_teo9d4mncjfdu, f1) == 16, "layout of `BreakExpr`");
_Static_assert(sizeof(gm_tet9guhev5r4u) == 56 && _Alignof(gm_tet9guhev5r4u) == 8 && offsetof(gm_tet9guhev5r4u, f0) == 0 && offsetof(gm_tet9guhev5r4u, f1) == 32, "layout of `IntrinsicExpr`");
_Static_assert(sizeof(gm_tgsvmddj3e85e) == 64 && _Alignof(gm_tgsvmddj3e85e) == 8 && offsetof(gm_tgsvmddj3e85e, f0) == 0 && offsetof(gm_tgsvmddj3e85e, f1) == 8, "layout of `(i: usize, x: ParamInfo)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t419mo45g7j0a v0; void * v1; } f1; } gm_th1g69d7vpipg;
typedef struct { gm_tbci5ospk5nnn f0; uint8_t f1; uint8_t pe[7]; } gm_thrdpodrs8pgt;
typedef struct { void * f0; gm_t1s75i08qtptd f1; void * f2; } gm_tioi3jh9v22g5;
_Static_assert(sizeof(gm_tioie2ju3q4ul) == 40 && _Alignof(gm_tioie2ju3q4ul) == 8 && offsetof(gm_tioie2ju3q4ul, f0) == 0 && offsetof(gm_tioie2ju3q4ul, f1) == 8, "layout of `(base: ^Expr, name: Ident)`");
typedef struct { gm_tbci5ospk5nnn f0; gm_tbci5ospk5nnn f1; } gm_tngtuiuskn938;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t88rcucg1b7uk v0; gm_t428ue2u3hf5g v1; } f1; } gm_toa2vhe3l4hfr;
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t p2[4]; uint64_t f2; gm_b16_8 f3; gm_t88rcucg1b7uk f4; uint8_t p5[4]; gm_tnpifv20irnph f5; gm_b16_8 f6; gm_tqu3m2l3n8mnc f7; uint8_t f8; uint8_t pe[7]; } gm_tqqhoivahirr0;
_Static_assert(sizeof(gm_tqu3m2l3n8mnc) == 32 && _Alignof(gm_tqu3m2l3n8mnc) == 8 && offsetof(gm_tqu3m2l3n8mnc, f0) == 0 && offsetof(gm_tqu3m2l3n8mnc, f1) == 24, "layout of `?FnInstance`");
_Static_assert(sizeof(gm_trl9d7m1k5lbd) == 24 && _Alignof(gm_trl9d7m1k5lbd) == 8 && offsetof(gm_trl9d7m1k5lbd, f0) == 0 && offsetof(gm_trl9d7m1k5lbd, f1) == 8 && offsetof(gm_trl9d7m1k5lbd, f2) == 16, "layout of `(op: BinOp, lhs: ^Expr, rhs: ^Expr)`");
_Static_assert(sizeof(gm_ts5t31j0bugq1) == 40 && _Alignof(gm_ts5t31j0bugq1) == 8 && offsetof(gm_ts5t31j0bugq1, f0) == 0 && offsetof(gm_ts5t31j0bugq1, f1) == 8, "layout of `Packed`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint32_t v0; gm_t419mo45g7j0a v1; void * v2; } f1; } gm_tsth9qu1dv0g3;
_Static_assert(sizeof(gm_tt8o5kebou62r) == 32 && _Alignof(gm_tt8o5kebou62r) == 8 && offsetof(gm_tt8o5kebou62r, f0) == 0 && offsetof(gm_tt8o5kebou62r, f1) == 16, "layout of `(op: Builtin, args: []Expr)`");
_Static_assert(sizeof(gm_t53at80pfdus3) == 56 && _Alignof(gm_t53at80pfdus3) == 8 && offsetof(gm_t53at80pfdus3, f0) == 0 && offsetof(gm_t53at80pfdus3, f1) == 8, "layout of `TyKind`");
typedef struct { gm_th1g69d7vpipg f0; void * f1; void * f2; uint32_t f3; uint8_t pe[4]; } gm_t9i38ih07bojq;
_Static_assert(sizeof(gm_ta42iqot1auae) == 64 && _Alignof(gm_ta42iqot1auae) == 8 && offsetof(gm_ta42iqot1auae, f0) == 0 && offsetof(gm_ta42iqot1auae, f1) == 48 && offsetof(gm_ta42iqot1auae, f2) == 60, "layout of `IntLit`");
_Static_assert(sizeof(gm_tchr52g0ema53) == 48 && _Alignof(gm_tchr52g0ema53) == 8 && offsetof(gm_tchr52g0ema53, f0) == 0 && offsetof(gm_tchr52g0ema53, f1) == 40, "layout of `BreakExpr`");
_Static_assert(sizeof(gm_tck5nas0nf5v8) == 48 && _Alignof(gm_tck5nas0nf5v8) == 8 && offsetof(gm_tck5nas0nf5v8, f0) == 0 && offsetof(gm_tck5nas0nf5v8, f1) == 40, "layout of `?Packed`");
typedef struct { uint64_t f0; gm_toa2vhe3l4hfr f1; } gm_tdr8bh2bm3fc8;
typedef struct { gm_tngtuiuskn938 f0; gm_taari1s0m51a7 f1; uint8_t f2; uint8_t pe[5]; } gm_tghmb53v8qi1e;
_Static_assert(sizeof(gm_th1g69d7vpipg) == 40 && _Alignof(gm_th1g69d7vpipg) == 8 && offsetof(gm_th1g69d7vpipg, f0) == 0 && offsetof(gm_th1g69d7vpipg, f1) == 8, "layout of `WriteSink`");
_Static_assert(sizeof(gm_thrdpodrs8pgt) == 56 && _Alignof(gm_thrdpodrs8pgt) == 8 && offsetof(gm_thrdpodrs8pgt, f0) == 0 && offsetof(gm_thrdpodrs8pgt, f1) == 48, "layout of `?Int`");
_Static_assert(sizeof(gm_tioi3jh9v22g5) == 56 && _Alignof(gm_tioi3jh9v22g5) == 8 && offsetof(gm_tioi3jh9v22g5, f0) == 0 && offsetof(gm_tioi3jh9v22g5, f1) == 8 && offsetof(gm_tioi3jh9v22g5, f2) == 48, "layout of `ElseExpr`");
_Static_assert(sizeof(gm_tngtuiuskn938) == 96 && _Alignof(gm_tngtuiuskn938) == 8 && offsetof(gm_tngtuiuskn938, f0) == 0 && offsetof(gm_tngtuiuskn938, f1) == 48, "layout of `Rat`");
typedef struct { gm_th1g69d7vpipg f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; } gm_tntn2tf15puib;
typedef struct { gm_tsth9qu1dv0g3 f0; gm_b16_8 f1; } gm_to76jli3bqddq;
_Static_assert(sizeof(gm_toa2vhe3l4hfr) == 40 && _Alignof(gm_toa2vhe3l4hfr) == 8 && offsetof(gm_toa2vhe3l4hfr, f0) == 0 && offsetof(gm_toa2vhe3l4hfr, f1) == 8, "layout of `GenericArg`");
_Static_assert(sizeof(gm_tqqhoivahirr0) == 136 && _Alignof(gm_tqqhoivahirr0) == 8 && offsetof(gm_tqqhoivahirr0, f0) == 0 && offsetof(gm_tqqhoivahirr0, f1) == 16 && offsetof(gm_tqqhoivahirr0, f2) == 32 && offsetof(gm_tqqhoivahirr0, f3) == 40 && offsetof(gm_tqqhoivahirr0, f4) == 56 && offsetof(gm_tqqhoivahirr0, f5) == 64 && offsetof(gm_tqqhoivahirr0, f6) == 80 && offsetof(gm_tqqhoivahirr0, f7) == 96 && offsetof(gm_tqqhoivahirr0, f8) == 128, "layout of `FnSig`");
_Static_assert(sizeof(gm_tsth9qu1dv0g3) == 40 && _Alignof(gm_tsth9qu1dv0g3) == 8 && offsetof(gm_tsth9qu1dv0g3, f0) == 0 && offsetof(gm_tsth9qu1dv0g3, f1) == 8, "layout of `Callee`");
typedef struct { gm_tngtuiuskn938 f0; gm_tngtuiuskn938 f1; } gm_tu15p0hi2qlv6;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tbci5ospk5nnn v0; gm_tngtuiuskn938 v1; uint8_t v2; gm_tu15p0hi2qlv6 v3; } f1; } gm_t0ialelq3ql1k;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tbci5ospk5nnn v0; double v1; gm_tngtuiuskn938 v2; gm_tu15p0hi2qlv6 v3; uint8_t v4; gm_b16_8 v5; gm_b16_8 v6; uint32_t v7; gm_tkn42u7na3t81 v8; gm_tkn42u7na3t81 v9; gm_t88rcucg1b7uk v10; gm_t3rcjibl8kkg4 v11; gm_t08olq97rscn3 v12; uint32_t v13; gm_tcc1rq4n1avb2 v14; gm_t3877pk7lv748 v15; void * v16; gm_tbl8il92l97ut v17; gm_tlcl4u558l6p5 v18; gm_tao63pk3r18v5 v19; void * v20; void * v21; void * v22; void * v23; uint32_t v24; uint32_t v25; uint32_t v26; gm_t8ghgl2jj0qha v27; gm_trl9d7m1k5lbd v28; gm_ta612fil812pm v29; gm_t8jsc4n9vp2ib v30; gm_tt8o5kebou62r v31; gm_to76jli3bqddq v32; gm_b16_8 v33; gm_b16_8 v34; void * v35; gm_ttggq7ng4kp3p v36; void * v37; gm_tsplfr33dsvji v38; gm_t3dcoc7077t1q v39; gm_t083kd7b1kkag v40; gm_tlcl4u558l6p5 v41; gm_tkaapcii86udk v42; gm_tlcl4u558l6p5 v43; void * v44; gm_trl2khmuphncd v45; gm_t710lsrq1m8gk v46; gm_teo9d4mncjfdu v47; gm_te9hrk0u35rev v48; gm_ttggq7ng4kp3p v49; void * v50; gm_t3mf58gv9kf3n v51; void * v52; gm_tntn2tf15puib v53; gm_t9i38ih07bojq v54; gm_tandvbt63uk7n v55; gm_t710lsrq1m8gk v56; } f1; } gm_t5brlu7fal70j;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_ta42iqot1auae v0; gm_tghmb53v8qi1e v1; gm_tibupt4dmcqai v2; uint8_t v3; gm_t08olq97rscn3 v4; gm_t08olq97rscn3 v5; void * v6; gm_b16_8 v7; void * v8; gm_t8ghgl2jj0qha v9; gm_trl9d7m1k5lbd v10; gm_tioi3jh9v22g5 v11; void * v12; void * v13; gm_t3mf58gv9kf3n v14; void * v15; gm_tlljg61a2ctgq v16; gm_t3mf58gv9kf3n v17; gm_tao63pk3r18v5 v18; gm_tioie2ju3q4ul v19; gm_tupk62cr6lbdu v20; void * v21; gm_tgtub0v94d5u5 v22; gm_tgtub0v94d5u5 v23; gm_tgtub0v94d5u5 v24; gm_t3mf58gv9kf3n v25; gm_t3mf58gv9kf3n v26; void * v27; void * v28; void * v29; void * v30; void * v31; gm_tpe1hv3fekn05 v32; void * v33; void * v34; gm_tibupt4dmcqai v35; void * v36; void * v37; void * v38; void * v39; void * v40; gm_t79rbdv4rafe5 v41; void * v42; gm_tchr52g0ema53 v43; gm_t1s75i08qtptd v44; gm_t3mf58gv9kf3n v45; gm_tet9guhev5r4u v46; } f1; } gm_t6e9d80inoppc;
_Static_assert(sizeof(gm_t9i38ih07bojq) == 64 && _Alignof(gm_t9i38ih07bojq) == 8 && offsetof(gm_t9i38ih07bojq, f0) == 0 && offsetof(gm_t9i38ih07bojq, f1) == 40 && offsetof(gm_t9i38ih07bojq, f2) == 48 && offsetof(gm_t9i38ih07bojq, f3) == 56, "layout of `WriteDeferredExpr`");
_Static_assert(sizeof(gm_tdr8bh2bm3fc8) == 48 && _Alignof(gm_tdr8bh2bm3fc8) == 8 && offsetof(gm_tdr8bh2bm3fc8, f0) == 0 && offsetof(gm_tdr8bh2bm3fc8, f1) == 8, "layout of `(i: usize, x: GenericArg)`");
_Static_assert(sizeof(gm_tghmb53v8qi1e) == 104 && _Alignof(gm_tghmb53v8qi1e) == 8 && offsetof(gm_tghmb53v8qi1e, f0) == 0 && offsetof(gm_tghmb53v8qi1e, f1) == 96 && offsetof(gm_tghmb53v8qi1e, f2) == 98, "layout of `FloatLit`");
_Static_assert(sizeof(gm_tntn2tf15puib) == 88 && _Alignof(gm_tntn2tf15puib) == 8 && offsetof(gm_tntn2tf15puib, f0) == 0 && offsetof(gm_tntn2tf15puib, f1) == 40 && offsetof(gm_tntn2tf15puib, f2) == 56 && offsetof(gm_tntn2tf15puib, f3) == 72, "layout of `WriteExpr`");
_Static_assert(sizeof(gm_to76jli3bqddq) == 56 && _Alignof(gm_to76jli3bqddq) == 8 && offsetof(gm_to76jli3bqddq, f0) == 0 && offsetof(gm_to76jli3bqddq, f1) == 40, "layout of `(callee: Callee, args: []Expr)`");
_Static_assert(sizeof(gm_tu15p0hi2qlv6) == 192 && _Alignof(gm_tu15p0hi2qlv6) == 8 && offsetof(gm_tu15p0hi2qlv6, f0) == 0 && offsetof(gm_tu15p0hi2qlv6, f1) == 96, "layout of `(re: Rat, im: Rat)`");
_Static_assert(sizeof(gm_t0ialelq3ql1k) == 200 && _Alignof(gm_t0ialelq3ql1k) == 8 && offsetof(gm_t0ialelq3ql1k, f0) == 0 && offsetof(gm_t0ialelq3ql1k, f1) == 8, "layout of `Untyped`");
_Static_assert(sizeof(gm_t5brlu7fal70j) == 200 && _Alignof(gm_t5brlu7fal70j) == 8 && offsetof(gm_t5brlu7fal70j, f0) == 0 && offsetof(gm_t5brlu7fal70j, f1) == 8, "layout of `ExprKind`");
_Static_assert(sizeof(gm_t6e9d80inoppc) == 112 && _Alignof(gm_t6e9d80inoppc) == 8 && offsetof(gm_t6e9d80inoppc, f0) == 0 && offsetof(gm_t6e9d80inoppc, f1) == 8, "layout of `ExprKind`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint32_t v0; gm_t0ialelq3ql1k v1; uint32_t v2; gm_ts78ca2amthgu v3; gm_t88rcucg1b7uk v4; } f1; } gm_t930q5l38hdap;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_t6e9d80inoppc f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_tfbvnc90lci8r;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_t5brlu7fal70j f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_tfpf0s89fpb3r;
typedef struct { gm_t08olq97rscn3 f0; gm_tfbvnc90lci8r f1; } gm_t2tlhjbsqg5eb;
typedef struct { uint64_t f0; gm_tfbvnc90lci8r f1; uint8_t f2; uint8_t pe[7]; } gm_t5retglboi70s;
typedef struct { uint64_t f0; gm_tfbvnc90lci8r f1; } gm_t8ip9lbbvm8be;
_Static_assert(sizeof(gm_t930q5l38hdap) == 208 && _Alignof(gm_t930q5l38hdap) == 8 && offsetof(gm_t930q5l38hdap, f0) == 0 && offsetof(gm_t930q5l38hdap, f1) == 8, "layout of `LocalBinding`");
_Static_assert(sizeof(gm_tfbvnc90lci8r) == 136 && _Alignof(gm_tfbvnc90lci8r) == 8 && offsetof(gm_tfbvnc90lci8r, f0) == 0 && offsetof(gm_tfbvnc90lci8r, f1) == 8 && offsetof(gm_tfbvnc90lci8r, f2) == 120, "layout of `Expr`");
_Static_assert(sizeof(gm_tfpf0s89fpb3r) == 224 && _Alignof(gm_tfpf0s89fpb3r) == 8 && offsetof(gm_tfpf0s89fpb3r, f0) == 0 && offsetof(gm_tfpf0s89fpb3r, f1) == 8 && offsetof(gm_tfpf0s89fpb3r, f2) == 208, "layout of `Expr`");
typedef struct { gm_tfbvnc90lci8r f0; uint8_t f1; uint8_t pe[7]; } gm_tj2dlstgqla1c;
typedef struct { gm_t930q5l38hdap f0; uint8_t f1; uint8_t pe[7]; } gm_tqvu78rpi6q1h;
_Static_assert(sizeof(gm_t2tlhjbsqg5eb) == 168 && _Alignof(gm_t2tlhjbsqg5eb) == 8 && offsetof(gm_t2tlhjbsqg5eb, f0) == 0 && offsetof(gm_t2tlhjbsqg5eb, f1) == 32, "layout of `(name: Ident, value: Expr)`");
_Static_assert(sizeof(gm_t5retglboi70s) == 152 && _Alignof(gm_t5retglboi70s) == 8 && offsetof(gm_t5retglboi70s, f0) == 0 && offsetof(gm_t5retglboi70s, f1) == 8 && offsetof(gm_t5retglboi70s, f2) == 144, "layout of `ArgSource`");
_Static_assert(sizeof(gm_t8ip9lbbvm8be) == 144 && _Alignof(gm_t8ip9lbbvm8be) == 8 && offsetof(gm_t8ip9lbbvm8be, f0) == 0 && offsetof(gm_t8ip9lbbvm8be, f1) == 8, "layout of `(i: usize, x: Expr)`");
_Static_assert(sizeof(gm_tj2dlstgqla1c) == 144 && _Alignof(gm_tj2dlstgqla1c) == 8 && offsetof(gm_tj2dlstgqla1c, f0) == 0 && offsetof(gm_tj2dlstgqla1c, f1) == 136, "layout of `?Expr`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tfbvnc90lci8r v0; gm_t2tlhjbsqg5eb v1; gm_tfbvnc90lci8r v2; } f1; } gm_tq6gu4l68u3rv;
_Static_assert(sizeof(gm_tqvu78rpi6q1h) == 216 && _Alignof(gm_tqvu78rpi6q1h) == 8 && offsetof(gm_tqvu78rpi6q1h, f0) == 0 && offsetof(gm_tqvu78rpi6q1h, f1) == 208, "layout of `?LocalBinding`");
typedef struct { gm_tq6gu4l68u3rv f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_tpff59ptr3s92;
_Static_assert(sizeof(gm_tq6gu4l68u3rv) == 176 && _Alignof(gm_tq6gu4l68u3rv) == 8 && offsetof(gm_tq6gu4l68u3rv, f0) == 0 && offsetof(gm_tq6gu4l68u3rv, f1) == 8, "layout of `ArgKind`");
_Static_assert(sizeof(gm_tpff59ptr3s92) == 192 && _Alignof(gm_tpff59ptr3s92) == 8 && offsetof(gm_tpff59ptr3s92, f0) == 0 && offsetof(gm_tpff59ptr3s92, f1) == 176, "layout of `Arg`");
struct gm_fr_bind_available_effects_uav06po {
    gm_frame h;
    gm_toa2vhe3l4hfr l13; /* x */
    gm_b0_1 ret;
};
struct gm_fr_bind_literal_fallbacks_r2021qp {
    gm_frame h;
    void * l0; /* cx */
    void * l1; /* sig */
    void * l2; /* binds */
    uint8_t l11; /* mentions */
    gm_trqfvmjtt4sij l13; /* found */
    gm_b0_1 ret;
};
struct gm_fr_bind_unit_defaults_htn8poj {
    gm_frame h;
    gm_t5v0c8ia6a5mo l8; /* p */
    gm_b0_1 ret;
};
struct gm_fr_check_generic_constraint_6138rrh {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_clearly_value_f7icrt5 {
    gm_frame h;
    gm_t930q5l38hdap l3; /* b */
    uint8_t ret;
};
struct gm_fr_coerce_generic_args_9ald24q {
    gm_frame h;
    gm_trqfvmjtt4sij l5; /* out */
    gm_trqfvmjtt4sij l6; /* slot */
    gm_t5v0c8ia6a5mo l10; /* p */
    gm_td7pkamr64l0c ret;
};
struct gm_fr_comptime_arg_vtq6sop {
    gm_frame h;
    gm_t930q5l38hdap l4; /* b */
    gm_tkn42u7na3t81 ret;
};
struct gm_fr_denotes_type_jgfet92 {
    gm_frame h;
    gm_tet9guhev5r4u l4; /* i */
    uint8_t ret;
};
struct gm_fr_effect_args_name_jgklot1 {
    gm_frame h;
    void * l0; /* s */
    uint32_t l2; /* pid */
    uint8_t l3; /* found */
    gm_toa2vhe3l4hfr l6; /* a */
    uint8_t ret;
};
struct gm_fr_fn_44t30pu {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_dgpelc4 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_fn_dink6tf {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_fn_pjcgmfa {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_qtpj77g {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_tqlm0ij {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_uualn6s {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_generic_call_2tlofdi {
    gm_frame h;
    gm_tqqhoivahirr0 l5; /* sig */
    gm_t5v0c8ia6a5mo l10; /* p */
    gm_t5v0c8ia6a5mo l15; /* p */
    gm_trqfvmjtt4sij l16; /* named */
    gm_tpff59ptr3s92 l19; /* a */
    gm_trqfvmjtt4sij l21; /* slot */
    gm_t5v0c8ia6a5mo l25; /* p */
    gm_ts5t31j0bugq1 l26; /* packed */
    gm_tpff59ptr3s92 l32; /* a */
    gm_trqfvmjtt4sij l33; /* stmts */
    void * ret;
};
struct gm_fr_generic_call_packed_h7ssi9n {
    gm_frame h;
    void * l0; /* cx */
    gm_tqqhoivahirr0 l5; /* sig */
    gm_t5v0c8ia6a5mo l11; /* p */
    gm_trqfvmjtt4sij l12; /* named */
    gm_trqfvmjtt4sij l13; /* positional */
    gm_tpff59ptr3s92 l16; /* a */
    gm_tfbvnc90lci8r l18; /* e */
    gm_trqfvmjtt4sij l20; /* targets */
    gm_t5v0c8ia6a5mo l24; /* p */
    gm_t5v0c8ia6a5mo l29; /* p */
    gm_trqfvmjtt4sij l30; /* binds */
    gm_trqfvmjtt4sij l31; /* literal_args */
    gm_trqfvmjtt4sij l33; /* values */
    gm_trqfvmjtt4sij l36; /* sources */
    gm_tfbvnc90lci8r l40; /* e */
    gm_tpff59ptr3s92 l43; /* a */
    gm_tfbvnc90lci8r l45; /* e */
    gm_trqfvmjtt4sij l47; /* source_order */
    gm_t5retglboi70s l50; /* src */
    gm_trqfvmjtt4sij l51; /* iter_pre */
    gm_trqfvmjtt4sij l59; /* fixed */
    gm_t5v0c8ia6a5mo l68; /* p */
    uint8_t l73; /* unbound */
    gm_t5v0c8ia6a5mo l77; /* q */
    gm_tj2dlstgqla1c l78; /* lit */
    gm_tfbvnc90lci8r l79; /* l */
    gm_tfbvnc90lci8r l80; /* arg */
    uint8_t l82; /* mentions_free */
    gm_trqfvmjtt4sij l84; /* found */
    gm_trqfvmjtt4sij l90; /* map */
    gm_trqfvmjtt4sij l95; /* written */
    gm_t5retglboi70s l99; /* src */
    gm_td7pkamr64l0c l100; /* coerced */
    gm_trqfvmjtt4sij l102; /* slot */
    void * ret;
};
struct gm_fr_generic_fn_from_expected_ok2k7go {
    gm_frame h;
    gm_tddtjfggdat0d l5; /* want */
    gm_tqqhoivahirr0 l6; /* sig */
    gm_trqfvmjtt4sij l7; /* runtime */
    gm_t5v0c8ia6a5mo l10; /* p */
    gm_trqfvmjtt4sij l11; /* binds */
    gm_trqfvmjtt4sij l16; /* targs */
    void * ret;
};
struct gm_fr_generic_type_args_mj9hh1l {
    gm_frame h;
    gm_trqfvmjtt4sij l11; /* targs */
    gm_t5v0c8ia6a5mo l19; /* p */
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_given_by_name_pd5ma4t {
    gm_frame h;
    gm_tpff59ptr3s92 l3; /* a */
    uint8_t ret;
};
struct gm_fr_inferable_param_f80lrdi {
    gm_frame h;
    void * l0; /* s */
    uint32_t l3; /* pid */
    gm_t5v0c8ia6a5mo l10; /* q */
    uint32_t l11; /* all */
    uint32_t l12; /* computed */
    uint8_t ret;
};
struct gm_fr_is_instantiation_vgsqc8f {
    gm_frame h;
    gm_b16_8 l2; /* args */
    gm_t5v0c8ia6a5mo l8; /* p */
    gm_trqfvmjtt4sij l9; /* targets */
    gm_t5v0c8ia6a5mo l13; /* p */
    gm_t5v0c8ia6a5mo l20; /* p */
    gm_tfbvnc90lci8r l24; /* e */
    gm_t5v0c8ia6a5mo l25; /* p */
    uint8_t ret;
};
struct gm_fr_location_function_thau9ff {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_normalize_value_fgo7ofj {
    gm_frame h;
    gm_tbci5ospk5nnn l10; /* i */
    gm_tkn42u7na3t81 ret;
};
struct gm_fr_order_generic_args_m8gcs0c {
    gm_frame h;
    gm_b16_8 l5; /* source_order */
    gm_trqfvmjtt4sij l6; /* pre */
    gm_b16_8 ret;
};
struct gm_fr_param_default_h93ci1m {
    gm_frame h;
    void * ret;
};
struct gm_fr_report_cannot_infer_tor1q4u {
    gm_frame h;
    gm_t5v0c8ia6a5mo l11; /* p */
    gm_t5v0c8ia6a5mo l19; /* q */
    gm_b0_1 ret;
};
struct gm_fr_same_int_value_ngrqsok {
    gm_frame h;
    gm_tbci5ospk5nnn l5; /* i */
    gm_tbci5ospk5nnn l6; /* j */
    uint8_t ret;
};
struct gm_fr_set_bind_nsjnlvq {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_source_location_nc1oola {
    gm_frame h;
    gm_trqfvmjtt4sij l7; /* fields */
    void * ret;
};
struct gm_fr_source_position_taggjb0 {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_span_text_sv6qv18 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_too_many_generic_args_bs99n7s {
    gm_frame h;
    gm_t5v0c8ia6a5mo l13; /* p */
    gm_b0_1 ret;
};
bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b);
bool gm_eq467d5cm1d5(const uint8_t *a, const uint8_t *b);
bool gm_eq6groq37ram(const uint8_t *a, const uint8_t *b);
bool gm_eq7mri15hk78(const uint8_t *a, const uint8_t *b);
bool gm_eq9m6mvtmgde(const uint8_t *a, const uint8_t *b);
bool gm_eqkricceou4u(const uint8_t *a, const uint8_t *b);
__attribute__((aligned(4))) void * gm_f_arg_value_gavegdu(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) void * gm_f_array_as_slice_gvflfg2(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) gm_b16_8 gm_f_available_7okjiog(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_bind_available_effects_uav06po(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_bind_literal_fallbacks_r2021qp(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2, gm_b16_8 a3);
__attribute__((aligned(4))) void * gm_f_bind_temp_sa5rcbr(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_bind_unit_defaults_htn8poj(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_b16_8 a2, void * a3);
__attribute__((aligned(4))) void * gm_f_block_typed_e7svg7n(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0, gm_b16_8 a1, void * a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_bound_to_5c11t0h(gm_b16_8 a0, uint32_t a1);
__attribute__((aligned(4))) uint8_t gm_f_check_generic_constraint_6138rrh(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_b16_8 a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) uint8_t gm_f_clearly_value_f7icrt5(void * a0, void * a1);
__attribute__((aligned(4))) void gm_f_coerce_generic_args_9ald24q(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_b16_8 a2, gm_b16_8 a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) void * gm_f_coerce_sudc6c1(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_comptime_arg_vtq6sop(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, void * a2);
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_comptime_value_l5orc93(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_t88rcucg1b7uk a2, void * a3);
__attribute__((aligned(4))) gm_b16_8 gm_f_constraint_name_ombut3o(gm_t9qkh0rsooc8d a0);
__attribute__((aligned(4))) void * gm_f_default_value_lo723gi(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2, gm_t88rcucg1b7uk a3);
__attribute__((aligned(4))) uint8_t gm_f_denotes_type_jgfet92(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) uint8_t gm_f_effect_args_name_jgklot1(void * a0, void * a1, uint32_t a2);
__attribute__((aligned(4))) gm_tnpifv20irnph gm_f_empty_effects_r0iv3pv(void);
__attribute__((aligned(4))) uint8_t gm_f_eq_60fjn21(const void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_error_9huj44a(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) void * gm_f_error_expr_o6qt7e2(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1);
__attribute__((aligned(4))) void * gm_f_expr_vmp704u(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_tkn42u7na3t81 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_extend_nkql463(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void * gm_f_file_o15fqku(void * a0, uint32_t a1);
__attribute__((aligned(4))) void * gm_f_finish_call_kjknkfg(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, void * a2, uint8_t a3);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_44t30pu(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
__attribute__((aligned(4))) uint8_t gm_f_fn_dgpelc4(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) uint8_t gm_f_fn_dink6tf(void * a0, void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_pjcgmfa(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_qtpj77g(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
__attribute__((aligned(4))) void * gm_f_fn_ref_aqs2fl4(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_tqlm0ij(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_uualn6s(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
__attribute__((aligned(4))) void gm_f_format_3ifsvek(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_41hm2he(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_7u7i1da(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_8tl219j(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_9n2qn66(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_d2d7bae(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_dvcvfgj(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_itam0lj(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_kj30rlk(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_obibpjh(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_qk20moi(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_qv493su(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_r57fu0b(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_s1gurul(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_vns89mr(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_from_u64_ud13q5v(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint64_t a0);
__attribute__((aligned(4))) void * gm_f_generic_call_2tlofdi(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, void * a2, gm_b16_8 a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) void * gm_f_generic_call_packed_h7ssi9n(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, void * a2, gm_b16_8 a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) void * gm_f_generic_fn_from_expected_ok2k7go(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_tkn42u7na3t81 a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) void gm_f_generic_type_args_mj9hh1l(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_b16_8 a2, gm_b16_8 a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) uint8_t gm_f_given_by_name_pd5ma4t(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) uint8_t gm_f_has_infer_3o82a0b(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) uint8_t gm_f_has_param_u9t0efs(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) uint8_t gm_f_inferable_param_f80lrdi(void * a0, void * a1, uint64_t a2);
__attribute__((aligned(4))) uint32_t gm_f_instantiate_35r5pb2(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_b16_8 a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_int_ci6airg(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, uint16_t a2);
__attribute__((aligned(4))) uint8_t gm_f_int_fits_imbqv0c(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) void gm_f_int_key_dmcraup(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, const void * a2);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_intern_ia19mhl(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) uint8_t gm_f_is_const_ref_arg_s0f37he(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) uint8_t gm_f_is_error_8k27gd1(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) uint8_t gm_f_is_instantiation_vgsqc8f(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_b16_8 a2, gm_b16_8 a3);
__attribute__((aligned(4))) uint8_t gm_f_is_integer_8d4f9ek(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) uint8_t gm_f_is_site_8sr7vam(void * a0);
__attribute__((aligned(4))) void gm_f_iterator_arg_r0if28f(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) uint8_t gm_f_iterator_call_as_value_00i5mch(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2, gm_t88rcucg1b7uk a3);
__attribute__((aligned(4))) void gm_f_key_int_pqscuvn(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_kind_jcnt4hm(void *gm_out, void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) gm_cs9ufftihtbpo2 gm_f_line_col_9vn24mu(void * a0, uint32_t a1);
__attribute__((aligned(4))) void gm_f_list_5dhgg4i(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_list_89e90o6(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_list_epvducg(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_list_go2qg0b(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_list_ir0h3tc_2(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_list_lnmdod1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_list_nof7vfj(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_list_oalq03d(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_list_p2dh8s2(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_list_pmp06kq(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_list_s83jn2q(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_list_vp1djv1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_list_vp1djv1_2(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_b16_8 gm_f_location_function_thau9ff(void * a0);
__attribute__((aligned(4))) uint8_t gm_f_looks_like_type_ho5qv1k(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
__attribute__((aligned(4))) void gm_f_lookup_local_6f38t7p(void *gm_out, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_match_params_6vndkud(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_t88rcucg1b7uk a2, void * a3);
__attribute__((aligned(4))) uint8_t gm_f_mentions_error_37knu7r(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void * gm_f_mk_9bc97fd(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0, const void * a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_ve8bb01_2(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_normalize_value_fgo7ofj(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) gm_b16_8 gm_f_not_first_class_dve75oi(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) gm_b16_8 gm_f_order_generic_args_m8gcs0c(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_b16_8 a2, gm_b16_8 a3, void * a4, gm_b16_8 a5);
__attribute__((aligned(4))) void gm_f_pack_rest_args_cknfrfl(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1, gm_b16_8 a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) gm_b16_8 gm_f_param_constraints_2k5fnnl(void * a0, uint32_t a1);
__attribute__((aligned(4))) void * gm_f_param_default_h93ci1m(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint64_t a2, void * a3, gm_t88rcucg1b7uk a4, gm_tl874t2atnlh3 a5);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_param_index_78k4iut(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tnnjeerolos0a gm_f_param_kind_a0aikmb(void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_param_name_rbnop5l(void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_0ee93e4(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_14olj4s(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_3ef746b(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_cl1fihl(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_d943vgp(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_d943vgp_2(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_h69n5fi(void * a0, gm_cs7e9ppsd384bj a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_im9d4ro(void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_k3l9nic(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_r2k6rjf(void * a0, void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_ucgmlo2(void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_vd1290p(void * a0, uint8_t a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_record_effects_0eel5vu(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) void gm_f_remove_iqb0btt(void *gm_out, void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_report_cannot_infer_tor1q4u(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint32_t a2, gm_b16_8 a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_resolve_effect_arg_4h6q4qv(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_resolve_t2kal61(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_resolve_ty_uk6c1lp(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) uint8_t gm_f_same_int_value_ngrqsok(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) uint8_t gm_f_satisfies_p0n2er8(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_t9qkh0rsooc8d a2);
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_second_class_part_k6i54mk(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_set_bind_nsjnlvq(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_settle_h9ikvs5(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) gm_b16_8 gm_f_show_fsnl6fa(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_show_int_ub6npub(void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void * gm_f_source_location_nc1oola(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1);
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_source_location_ty_slllard(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) uint64_t gm_f_source_position_taggjb0(gm_b16_8 a0, uint64_t a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_span_text_sv6qv18(void * a0, gm_cseahvg8v7p0vs a1);
__attribute__((aligned(4))) void gm_f_spread_generic_args_8qtof2f(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, void * a2);
__attribute__((aligned(4))) void gm_f_stable_sort_by_3nevrtc(gm_b16_8 a0, gm_closure a1);
__attribute__((aligned(4))) void gm_f_stable_sort_by_spa1mja(gm_b16_8 a0, gm_closure a1);
__attribute__((aligned(4))) uint8_t gm_f_starts_with_6vvt01e(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_string_ty_j74ff0o(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_subst_a2s1mb3(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tnpifv20irnph gm_f_subst_effects_jnlbu5m(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_too_many_generic_args_bs99n7s(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint8_t a2, gm_b16_8 a3, gm_b16_8 a4, uint64_t a5, gm_tl874t2atnlh3 a6);
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_type_of_name_oqtujpn(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_type_param_3av4r8i(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_unit_2age34u(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_usize_ty_qnrd30f(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_walk_v858co7(void * a0, gm_csguankko4lmhd a1, gm_closure a2);
extern uint8_t gm_kc84hfpu27t[40] __attribute__((aligned(8)));
extern uint8_t gm_kkh67d5mpb1[40] __attribute__((aligned(8)));
extern const uint8_t gm_s1f440st4mnkp[2];
extern const uint8_t gm_s26emr7fhbmjr[22];
extern const uint8_t gm_s8pjuqfuormq3[51];
extern const uint8_t gm_sg7ronlh5k7js[84];
extern const uint8_t gm_sgj7bt7nru3s1[27];
extern const uint8_t gm_slbgk65sat4bm[8];
extern const uint8_t gm_sr4qbrk6tlgaa[1];
extern const char gm_srcrorgjlg51brd[];
extern const uint8_t gm_srv4cua6q51nq[5];
extern const uint8_t gm_stl5olh2jfr0d[40];
extern const gm_type gm_tyo86ks9qkvt;
__attribute__((aligned(4))) gm_b0_1 gm_f_bind_available_effects_uav06po(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2) {
    struct gm_fr_bind_available_effects_uav06po F;
    void * gm_l0; /* cx */
    void * gm_l1; /* sig */
    void * gm_l2; /* binds */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    uint32_t gm_l5; /* pid */
    gm_b16_8 gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    gm_tiilmfd16v3fc gm_l8; /* e */
    gm_b16_8 gm_l9; /* avail */
    gm_b16_8 gm_l10; /* <tmp> */
    uint64_t gm_l11; /* <tmp> */
    uint64_t gm_l12; /* i */
    gm_t88rcucg1b7uk gm_l14; /* xt */
    gm_t88rcucg1b7uk gm_l15; /* yt */
    GM_STACK_CHECK(&F, 876u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        {
            void * t3 = gm_l1;
            gm_b16_8 t4 = GM_LD(gm_b16_8, (((uint8_t *)t3) + 80));
            gm_l3 = t4;
        }
        {
            gm_l4 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t6 = gm_l4;
                uint64_t t7 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                uint8_t t8 = (t6 < t7);
                if (!t8) goto lb5;
                {
                    uint64_t t11 = gm_l4;
                    uint64_t t12 = (uintptr_t)t11;
                    uint64_t t13 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                    uint32_t t14 = GM_LD(uint32_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t12 * 4));
                    uint32_t t15 = t14;
                    uint32_t t18 = GM_LD(uint32_t, ((uint8_t *)&t15));
                    gm_l5 = t18;
                    {
                        uint8_t t21 = 0;
                        void * t22 = gm_l2;
                        gm_b16_8 t23 = GM_LD(gm_b16_8, (((uint8_t *)t22) + 0));
                        gm_b16_8 t24 = t23;
                        gm_b16_8 t25 = GM_LD(gm_b16_8, ((uint8_t *)&t24));
                        uint32_t t26 = gm_l5;
                        gm_tkn42u7na3t81 t27 = gm_f_bound_to_5c11t0h(t25, t26);
                        gm_tkn42u7na3t81 t28 = t27;
                        uint8_t t29;
                        if (((((uint8_t *)&t28))[4] != 0)) goto mn31;
                        t29 = ((uint8_t)0);
                        goto me30;
mn31:;
                        t29 = ((uint8_t)1);
                        goto me30;
mn32:;
                        gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 319, 12);
me30:;
                        if (!t29) {
                            void * t33 = gm_l0;
                            void * t34 = GM_LD(void *, (((uint8_t *)t33) + 0));
                            uint32_t t35 = gm_l5;
                            gm_tnnjeerolos0a t36 = gm_f_param_kind_a0aikmb(t34, t35);
                            gm_tnnjeerolos0a t37 = t36;
                            uint8_t t38 = (!(GM_LD(uint8_t, ((uint8_t *)&t37) + 0) == ((uint8_t)1ULL)));
                            t21 = t38;
                        } else t21 = 1;
                        if (!t21) goto ie20;
                        {
                            goto lc5;
                        }
                        goto ix20;
ie20:;
ix20:;
                    }
                    {
                        {
                            void * t41 = gm_l1;
                            gm_b16_8 t42 = GM_LD(gm_b16_8, ((((uint8_t *)t41) + 64) + 0));
                            gm_l6 = t42;
                        }
                        {
                            gm_l7 = ((uint64_t)0ULL);
                        }
                        {
                            for (;;) {
                                uint64_t t44 = gm_l7;
                                uint64_t t45 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                                uint8_t t46 = (t44 < t45);
                                if (!t46) goto lb43;
                                {
                                    uint64_t t49 = gm_l7;
                                    uint64_t t50 = (uintptr_t)t49;
                                    uint64_t t51 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                                    gm_tiilmfd16v3fc t54 = GM_LD(gm_tiilmfd16v3fc, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t50 * 24));
                                    gm_l8 = t54;
                                    {
                                        void * t57 = gm_l0;
                                        void * t58 = GM_LD(void *, (((uint8_t *)t57) + 0));
                                        void * t59 = (void *)((uint8_t *)&gm_l8);
                                        uint32_t t60 = gm_l5;
                                        uint8_t t61 = gm_f_effect_args_name_jgklot1(t58, t59, t60);
                                        uint8_t t62 = (uint8_t)!t61;
                                        if (!t62) goto ie56;
                                        {
                                            goto lc43;
                                        }
                                        goto ix56;
ie56:;
ix56:;
                                    }
                                    {
                                        void * t64 = gm_l0;
                                        uint32_t t65 = GM_LD(uint32_t, (((uint8_t *)&gm_l8) + 0));
                                        gm_b16_8 t66 = gm_f_available_7okjiog((void *)NULL, cx, NULL, t64, t65);
                                        if (gm_unw.kind) {
                                            goto gm_ll47;
                                        }
                                        gm_l9 = t66;
                                    }
                                    {
                                        uint64_t t68 = GM_LD(uintptr_t, ((uint8_t *)&gm_l9) + 8);
                                        uint8_t t69 = (!(t68 == ((uint64_t)1ULL)));
                                        if (!t69) goto ie67;
                                        {
                                            goto lc43;
                                        }
                                        goto ix67;
ie67:;
ix67:;
                                    }
                                    {
                                        {
                                            gm_b16_8 t72 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l8) + 8));
                                            gm_l10 = t72;
                                        }
                                        {
                                            gm_l11 = ((uint64_t)0ULL);
                                        }
                                        {
                                            for (;;) {
                                                uint64_t t74 = gm_l11;
                                                uint64_t t75 = GM_LD(uintptr_t, ((uint8_t *)&gm_l10) + 8);
                                                uint8_t t76 = (t74 < t75);
                                                if (!t76) goto lb73;
                                                {
                                                    gm_tdr8bh2bm3fc8 t79;
                                                    uint64_t t80 = gm_l11;
                                                    GM_ST(uint64_t, (uint8_t *)&t79 + 0, t80);
                                                    uint64_t t81 = gm_l11;
                                                    uint64_t t82 = (uintptr_t)t81;
                                                    uint64_t t83 = GM_LD(uintptr_t, ((uint8_t *)&gm_l10) + 8);
                                                    memcpy((uint8_t *)&t79 + 8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l10)) + t82 * 40), 40);
                                                    gm_tdr8bh2bm3fc8 t84 = t79;
                                                    uint64_t t87 = GM_LD(uint64_t, (((uint8_t *)&t84) + 0));
                                                    gm_l12 = t87;
                                                    gm_toa2vhe3l4hfr t88 = GM_LD(gm_toa2vhe3l4hfr, (((uint8_t *)&t84) + 8));
                                                    F.l13 = t88;
                                                    {
                                                        uint64_t t91 = gm_l12;
                                                        uint64_t t92 = (uintptr_t)((uint64_t)0ULL);
                                                        uint64_t t93 = GM_LD(uintptr_t, ((uint8_t *)&gm_l9) + 8);
                                                        if (t92 >= (uintptr_t)(t93)) gm_trap_index(t92, t93, gm_srcrorgjlg51brd, 325, 24);
                                                        uint64_t t94 = GM_LD(uintptr_t, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l9)) + t92 * 24) + 8) + 8);
                                                        uint8_t t95 = (t91 < t94);
                                                        if (!t95) goto ie90;
                                                        uint8_t t96 = GM_LD(uint8_t, (((uint8_t *)&F.l13) + 0));
                                                        if (t96 != ((uint8_t)0ULL)) goto ie90;
                                                        gm_t88rcucg1b7uk t97 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l13) + 8));
                                                        gm_l14 = t97;
                                                        uint64_t t98 = gm_l12;
                                                        uint64_t t99 = (uintptr_t)t98;
                                                        uint64_t t100 = (uintptr_t)((uint64_t)0ULL);
                                                        uint64_t t101 = GM_LD(uintptr_t, ((uint8_t *)&gm_l9) + 8);
                                                        if (t100 >= (uintptr_t)(t101)) gm_trap_index(t100, t101, gm_srcrorgjlg51brd, 325, 78);
                                                        uint64_t t102 = GM_LD(uintptr_t, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l9)) + t100 * 24) + 8) + 8);
                                                        if (t99 >= (uintptr_t)(t102)) gm_trap_index(t99, t102, gm_srcrorgjlg51brd, 325, 78);
                                                        uint8_t t103 = GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l9)) + t100 * 24) + 8)) + t99 * 40) + 0));
                                                        if (t103 != ((uint8_t)0ULL)) goto ie90;
                                                        gm_t88rcucg1b7uk t104 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l9)) + t100 * 24) + 8)) + t99 * 40) + 8));
                                                        gm_l15 = t104;
                                                        {
                                                            void * t106 = gm_l0;
                                                            void * t107 = GM_LD(void *, (((uint8_t *)t106) + 0));
                                                            gm_t88rcucg1b7uk t108 = gm_l14;
                                                            gm_t88rcucg1b7uk t109 = gm_l15;
                                                            void * t110 = gm_l2;
                                                            gm_b0_1 t111 = gm_f_match_params_6vndkud((void *)NULL, cx, NULL, t107, t108, t109, t110);
                                                            if (gm_unw.kind) {
                                                                goto gm_ll77;
                                                            }
                                                        }
                                                        goto ix90;
ie90:;
ix90:;
                                                    }
                                                    goto me85;
mn86:;
                                                    gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 324, 13);
me85:;
                                                }
lc73:;
                                                {
                                                    uint64_t t113 = gm_l11;
                                                    uint64_t r114; bool o115 = __builtin_add_overflow(t113, ((uint64_t)1ULL), &r114);
                                                    uint64_t t116 = r114;
                                                    gm_l11 = t116;
                                                }
                                            }
gm_ll77:;
                                            if (gm_unw.frame == &F.h && gm_unw.label == 2) {
                                                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb73; }
                                                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc73; }
                                            }
                                            goto gm_ll47;
lb73:;
                                        }
                                    }
                                    goto me52;
mn53:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 320, 9);
me52:;
                                }
lc43:;
                                {
                                    uint64_t t118 = gm_l7;
                                    uint64_t r119; bool o120 = __builtin_add_overflow(t118, ((uint64_t)1ULL), &r119);
                                    uint64_t t121 = r119;
                                    gm_l7 = t121;
                                }
                            }
gm_ll47:;
                            if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb43; }
                                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc43; }
                            }
                            goto gm_ll9;
lb43:;
                        }
                    }
                    goto me16;
mn17:;
                    gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 318, 5);
me16:;
                }
lc5:;
                {
                    uint64_t t123 = gm_l4;
                    uint64_t r124; bool o125 = __builtin_add_overflow(t123, ((uint64_t)1ULL), &r124);
                    uint64_t t126 = r124;
                    gm_l4 = t126;
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
__attribute__((aligned(4))) gm_b0_1 gm_f_bind_literal_fallbacks_r2021qp(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2, gm_b16_8 a3) {
    struct gm_fr_bind_literal_fallbacks_r2021qp F;
    gm_b16_8 gm_l3; /* literal_args */
    uint8_t gm_l4; /* any_free */
    gm_b16_8 gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    uint32_t gm_l7; /* g */
    gm_b16_8 gm_l8; /* <tmp> */
    uint64_t gm_l9; /* <tmp> */
    gm_thsh8t5u55gps gm_l10; /* la */
    gm_t88rcucg1b7uk gm_l12; /* t */
    gm_b16_8 gm_l14; /* <tmp> */
    uint64_t gm_l15; /* <tmp> */
    gm_tplpbqf3duvn3 gm_l16; /* b */
    GM_STACK_CHECK(&F, 711u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    F.l1 = a1;
    F.l2 = a2;
    gm_l3 = a3;
    {
        gm_l4 = ((uint8_t)0);
    }
    {
        {
            void * t3 = F.l1;
            gm_b16_8 t4 = GM_LD(gm_b16_8, (((uint8_t *)t3) + 80));
            gm_l5 = t4;
        }
        {
            gm_l6 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t6 = gm_l6;
                uint64_t t7 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                uint8_t t8 = (t6 < t7);
                if (!t8) goto lb5;
                {
                    uint64_t t11 = gm_l6;
                    uint64_t t12 = (uintptr_t)t11;
                    uint64_t t13 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                    uint32_t t14 = GM_LD(uint32_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l5)) + t12 * 4));
                    uint32_t t15 = t14;
                    uint32_t t18 = GM_LD(uint32_t, ((uint8_t *)&t15));
                    gm_l7 = t18;
                    {
                        void * t21 = F.l2;
                        gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)t21) + 0));
                        gm_b16_8 t23 = t22;
                        gm_b16_8 t24 = GM_LD(gm_b16_8, ((uint8_t *)&t23));
                        uint32_t t25 = gm_l7;
                        gm_tkn42u7na3t81 t26 = gm_f_bound_to_5c11t0h(t24, t25);
                        gm_tkn42u7na3t81 t27 = t26;
                        uint8_t t28;
                        if (((((uint8_t *)&t27))[4] != 0)) goto mn30;
                        t28 = ((uint8_t)1);
                        goto me29;
mn30:;
                        t28 = ((uint8_t)0);
                        goto me29;
mn31:;
                        gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 283, 12);
me29:;
                        if (!t28) goto ie20;
                        void * t32 = F.l0;
                        void * t33 = GM_LD(void *, (((uint8_t *)t32) + 0));
                        uint32_t t34 = gm_l7;
                        gm_tnnjeerolos0a t35 = gm_f_param_kind_a0aikmb(t33, t34);
                        gm_tnnjeerolos0a t36 = t35;
                        uint8_t t37;
                        uint8_t t40 = GM_LD(uint8_t, (((uint8_t *)&t36) + 0));
                        if (t40 != ((uint8_t)2ULL)) goto mn39;
                        t37 = ((uint8_t)0);
                        goto me38;
mn39:;
                        t37 = ((uint8_t)1);
                        goto me38;
mn41:;
                        gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 283, 54);
me38:;
                        if (!t37) goto ie20;
                        {
                            gm_l4 = ((uint8_t)1);
                        }
                        goto ix20;
ie20:;
ix20:;
                    }
                    goto me16;
mn17:;
                    gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 282, 5);
me16:;
                }
lc5:;
                {
                    uint64_t t44 = gm_l6;
                    uint64_t r45; bool o46 = __builtin_add_overflow(t44, ((uint64_t)1ULL), &r45);
                    uint64_t t47 = r45;
                    gm_l6 = t47;
                }
            }
lb5:;
        }
    }
    {
        uint8_t t49 = gm_l4;
        if (!t49) goto ie48;
        {
            {
                gm_b16_8 t52 = gm_l3;
                gm_l8 = t52;
            }
            {
                gm_l9 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t54 = gm_l9;
                    uint64_t t55 = GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8);
                    uint8_t t56 = (t54 < t55);
                    if (!t56) goto lb53;
                    {
                        uint64_t t59 = gm_l9;
                        uint64_t t60 = (uintptr_t)t59;
                        uint64_t t61 = GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8);
                        gm_thsh8t5u55gps t64 = GM_LD(gm_thsh8t5u55gps, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l8)) + t60 * 20));
                        gm_l10 = t64;
                        {
                            F.l11 = ((uint8_t)0);
                        }
                        {
                            void * t66 = F.l0;
                            void * t67 = GM_LD(void *, (((uint8_t *)t66) + 0));
                            gm_t88rcucg1b7uk t68 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l10) + 0));
                            gm_closure t69 = ((gm_closure){(void *)gm_f_fn_uualn6s, (void *)&F.h, cx});
                            gm_f_walk_v858co7(t67, GM_CONV(gm_csguankko4lmhd, t68), t69);
                            if (gm_unw.kind) {
                                goto gm_ll57;
                            }
                        }
                        {
                            uint8_t t71 = F.l11;
                            uint8_t t72 = (uint8_t)!t71;
                            if (!t72) goto ie70;
                            {
                                goto lc53;
                            }
                            goto ix70;
ie70:;
ix70:;
                        }
                        {
                            void * t74 = F.l0;
                            gm_t88rcucg1b7uk t75 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l10) + 4));
                            gm_tl874t2atnlh3 t76 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l10) + 8));
                            gm_t88rcucg1b7uk t77 = gm_f_settle_h9ikvs5((void *)NULL, cx, NULL, t74, t75, t76);
                            if (gm_unw.kind) {
                                goto gm_ll57;
                            }
                            gm_l12 = t77;
                        }
                        {
                            gm_f_list_oalq03d((void *)&F.l13, (void *)NULL, cx, NULL);
                            if (gm_unw.kind) {
                                goto gm_ll57;
                            }
                        }
                        {
                            void * t78 = F.l0;
                            void * t79 = GM_LD(void *, (((uint8_t *)t78) + 0));
                            gm_t88rcucg1b7uk t80 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l10) + 0));
                            gm_t88rcucg1b7uk t81 = gm_l12;
                            void * t82 = (void *)((uint8_t *)&F.l13);
                            gm_b0_1 t83 = gm_f_match_params_6vndkud((void *)NULL, cx, NULL, t79, t80, t81, t82);
                            if (gm_unw.kind) {
                                goto gm_ll57;
                            }
                        }
                        {
                            {
                                gm_b16_8 t85 = GM_LD(gm_b16_8, (((uint8_t *)&F.l13) + 0));
                                gm_l14 = t85;
                            }
                            {
                                gm_l15 = ((uint64_t)0ULL);
                            }
                            {
                                for (;;) {
                                    uint64_t t87 = gm_l15;
                                    uint64_t t88 = GM_LD(uintptr_t, ((uint8_t *)&gm_l14) + 8);
                                    uint8_t t89 = (t87 < t88);
                                    if (!t89) goto lb86;
                                    {
                                        uint64_t t92 = gm_l15;
                                        uint64_t t93 = (uintptr_t)t92;
                                        uint64_t t94 = GM_LD(uintptr_t, ((uint8_t *)&gm_l14) + 8);
                                        gm_tplpbqf3duvn3 t97 = GM_LD(gm_tplpbqf3duvn3, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l14)) + t93 * 8));
                                        gm_l16 = t97;
                                        {
                                            void * t100 = F.l2;
                                            gm_b16_8 t101 = GM_LD(gm_b16_8, (((uint8_t *)t100) + 0));
                                            gm_b16_8 t102 = t101;
                                            gm_b16_8 t103 = GM_LD(gm_b16_8, ((uint8_t *)&t102));
                                            uint32_t t104 = GM_LD(uint32_t, (((uint8_t *)&gm_l16) + 0));
                                            gm_tkn42u7na3t81 t105 = gm_f_bound_to_5c11t0h(t103, t104);
                                            gm_tkn42u7na3t81 t106 = t105;
                                            uint8_t t107;
                                            if (((((uint8_t *)&t106))[4] != 0)) goto mn109;
                                            t107 = ((uint8_t)1);
                                            goto me108;
mn109:;
                                            t107 = ((uint8_t)0);
                                            goto me108;
mn110:;
                                            gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 296, 20);
me108:;
                                            if (!t107) goto ie99;
                                            {
                                                void * t112 = F.l2;
                                                gm_tplpbqf3duvn3 t113 = gm_l16;
                                                gm_tftb1odc739t5 t114 = gm_f_push_h69n5fi(t112, GM_CONV(gm_cs7e9ppsd384bj, t113));
                                                gm_tftb1odc739t5 t115 = t114;
                                                if (((((uint8_t *)&t115))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t115) + 1, gm_srcrorgjlg51brd, 296, 66);
                                                gm_b0_1 t116 = GM_LD(gm_b0_1, (((uint8_t *)&t115) + 1));
                                            }
                                            goto ix99;
ie99:;
ix99:;
                                        }
                                        goto me95;
mn96:;
                                        gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 295, 13);
me95:;
                                    }
lc86:;
                                    {
                                        uint64_t t118 = gm_l15;
                                        uint64_t r119; bool o120 = __builtin_add_overflow(t118, ((uint64_t)1ULL), &r119);
                                        uint64_t t121 = r119;
                                        gm_l15 = t121;
                                    }
                                }
lb86:;
                            }
                        }
                        goto me62;
mn63:;
                        gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 286, 9);
me62:;
                    }
lc53:;
                    {
                        uint64_t t123 = gm_l9;
                        uint64_t r124; bool o125 = __builtin_add_overflow(t123, ((uint64_t)1ULL), &r124);
                        uint64_t t126 = r124;
                        gm_l9 = t126;
                    }
                }
gm_ll57:;
                if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb53; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc53; }
                }
                goto gm_lad;
lb53:;
            }
        }
        goto ix48;
ie48:;
ix48:;
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
__attribute__((aligned(4))) gm_b0_1 gm_f_bind_unit_defaults_htn8poj(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_b16_8 a2, void * a3) {
    struct gm_fr_bind_unit_defaults_htn8poj F;
    void * gm_l0; /* cx */
    void * gm_l1; /* sig */
    gm_b16_8 gm_l2; /* values */
    void * gm_l3; /* binds */
    void * gm_l4; /* tys */
    gm_b16_8 gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    uint64_t gm_l7; /* i */
    uint32_t gm_l9; /* pid */
    void * gm_l10; /* d */
    gm_b16_8 gm_l11; /* elems */
    GM_STACK_CHECK(&F, 794u);
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
        void * t4 = (void *)(((uint8_t *)t3) + 8);
        gm_l4 = t4;
    }
    {
        {
            void * t6 = gm_l1;
            gm_b16_8 t7 = GM_LD(gm_b16_8, (((uint8_t *)t6) + 40));
            gm_b16_8 t8 = t7;
            gm_b16_8 t9 = GM_LD(gm_b16_8, ((uint8_t *)&t8));
            gm_l5 = t9;
        }
        {
            gm_l6 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t11 = gm_l6;
                uint64_t t12 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                uint8_t t13 = (t11 < t12);
                if (!t13) goto lb10;
                {
                    gm_tgsvmddj3e85e t16;
                    uint64_t t17 = gm_l6;
                    GM_ST(uint64_t, (uint8_t *)&t16 + 0, t17);
                    uint64_t t18 = gm_l6;
                    uint64_t t19 = (uintptr_t)t18;
                    uint64_t t20 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                    memcpy((uint8_t *)&t16 + 8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l5)) + t19 * 56), 56);
                    gm_tgsvmddj3e85e t21 = t16;
                    uint64_t t24 = GM_LD(uint64_t, (((uint8_t *)&t21) + 0));
                    gm_l7 = t24;
                    gm_t5v0c8ia6a5mo t25 = GM_LD(gm_t5v0c8ia6a5mo, (((uint8_t *)&t21) + 8));
                    F.l8 = t25;
                    {
                        void * t28 = gm_l4;
                        gm_t88rcucg1b7uk t29 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l8) + 16));
                        gm_t53at80pfdus3 t30;
                        gm_f_kind_jcnt4hm((void *)&t30, t28, GM_CONV(gm_csguankko4lmhd, t29));
                        gm_t53at80pfdus3 t31 = t30;
                        uint8_t t32 = GM_LD(uint8_t, (((uint8_t *)&t31) + 0));
                        if (t32 != ((uint8_t)19ULL)) goto ie27;
                        uint32_t t33 = GM_LD(uint32_t, (((uint8_t *)&t31) + 8));
                        gm_l9 = t33;
                        {
                            uint64_t t36 = gm_l7;
                            uint64_t t37 = (uintptr_t)t36;
                            uint64_t t38 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                            if (t37 >= (uintptr_t)(t38)) gm_trap_index(t37, t38, gm_srcrorgjlg51brd, 308, 12);
                            void * t39 = GM_LD(void *, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t37 * 8));
                            void * t40 = t39;
                            uint8_t t41;
                            if ((GM_LD(void *, ((uint8_t *)&t40)) != 0)) goto mn43;
                            t41 = ((uint8_t)0);
                            goto me42;
mn43:;
                            t41 = ((uint8_t)1);
                            goto me42;
mn44:;
                            gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 308, 12);
me42:;
                            if (!t41) goto ie35;
                            {
                                goto lc10;
                            }
                            goto ix35;
ie35:;
ix35:;
                        }
                        {
                            void * t46 = GM_LD(void *, (((uint8_t *)&F.l8) + 32));
                            void * t47 = t46;
                            void * t48;
                            if ((GM_LD(void *, ((uint8_t *)&t47)) != 0)) {
                                void * t49 = GM_LD(void *, (((uint8_t *)&t47) + 0));
                                t48 = t49;
                            } else {
                                goto lc10;
                            }
                            gm_l10 = t48;
                        }
                        {
                            void * t51 = gm_l10;
                            uint8_t t52 = GM_LD(uint8_t, ((((uint8_t *)t51) + 8) + 0));
                            if (t52 != ((uint8_t)10ULL)) goto ie50;
                            gm_b16_8 t53 = GM_LD(gm_b16_8, ((((uint8_t *)t51) + 8) + 8));
                            gm_l11 = t53;
                            uint64_t t54 = GM_LD(uintptr_t, ((uint8_t *)&gm_l11) + 8);
                            uint8_t t55 = ((t54 == ((uint64_t)0ULL)));
                            if (!t55) goto ie50;
                            void * t56 = gm_l3;
                            gm_b16_8 t57 = GM_LD(gm_b16_8, (((uint8_t *)t56) + 0));
                            gm_b16_8 t58 = t57;
                            gm_b16_8 t59 = GM_LD(gm_b16_8, ((uint8_t *)&t58));
                            uint32_t t60 = gm_l9;
                            gm_tkn42u7na3t81 t61 = gm_f_bound_to_5c11t0h(t59, t60);
                            gm_tkn42u7na3t81 t62 = t61;
                            uint8_t t63;
                            if (((((uint8_t *)&t62))[4] != 0)) goto mn65;
                            t63 = ((uint8_t)1);
                            goto me64;
mn65:;
                            t63 = ((uint8_t)0);
                            goto me64;
mn66:;
                            gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 310, 59);
me64:;
                            if (!t63) goto ie50;
                            {
                                void * t68 = gm_l3;
                                uint32_t t69 = gm_l9;
                                void * t70 = gm_l4;
                                gm_t88rcucg1b7uk t71 = gm_f_unit_2age34u((void *)NULL, cx, NULL, t70);
                                if (gm_unw.kind) {
                                    goto gm_ll14;
                                }
                                gm_tplpbqf3duvn3 t72;
                                GM_ST(uint32_t, ((uint8_t *)&t72 + 0), t69);
                                GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t72 + 4), t71);
                                gm_tftb1odc739t5 t73 = gm_f_push_h69n5fi(t68, GM_CONV(gm_cs7e9ppsd384bj, t72));
                                gm_tftb1odc739t5 t74 = t73;
                                if (((((uint8_t *)&t74))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t74) + 1, gm_srcrorgjlg51brd, 310, 101);
                                gm_b0_1 t75 = GM_LD(gm_b0_1, (((uint8_t *)&t74) + 1));
                            }
                            goto ix50;
ie50:;
ix50:;
                        }
                        goto ix27;
ie27:;
                        {
                            goto lc10;
                        }
ix27:;
                    }
                    goto me22;
mn23:;
                    gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 306, 5);
me22:;
                }
lc10:;
                {
                    uint64_t t78 = gm_l6;
                    uint64_t r79; bool o80 = __builtin_add_overflow(t78, ((uint64_t)1ULL), &r79);
                    uint64_t t81 = r79;
                    gm_l6 = t81;
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
__attribute__((aligned(4))) uint8_t gm_f_check_generic_constraint_6138rrh(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_b16_8 a2, gm_tl874t2atnlh3 a3) {
    struct gm_fr_check_generic_constraint_6138rrh F;
    void * gm_l0; /* cx */
    void * gm_l1; /* sig */
    gm_b16_8 gm_l2; /* targs */
    gm_tl874t2atnlh3 gm_l3; /* span */
    void * gm_l4; /* tys */
    gm_b16_8 gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    uint64_t gm_l7; /* k */
    uint32_t gm_l8; /* pid */
    gm_t88rcucg1b7uk gm_l9; /* t */
    gm_t88rcucg1b7uk gm_l10; /* part */
    gm_b16_8 gm_l11; /* <tmp> */
    uint64_t gm_l12; /* <tmp> */
    gm_t9qkh0rsooc8d gm_l13; /* c */
    GM_STACK_CHECK(&F, 813u);
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
        void * t4 = (void *)(((uint8_t *)t3) + 8);
        gm_l4 = t4;
    }
    {
        {
            void * t6 = gm_l1;
            gm_b16_8 t7 = GM_LD(gm_b16_8, (((uint8_t *)t6) + 80));
            gm_b16_8 t8 = t7;
            gm_b16_8 t9 = GM_LD(gm_b16_8, ((uint8_t *)&t8));
            gm_l5 = t9;
        }
        {
            gm_l6 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t11 = gm_l6;
                uint64_t t12 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                uint8_t t13 = (t11 < t12);
                if (!t13) goto lb10;
                {
                    uint64_t t16 = gm_l6;
                    uint64_t t17 = gm_l6;
                    uint64_t t18 = (uintptr_t)t17;
                    uint64_t t19 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                    uint32_t t20 = GM_LD(uint32_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l5)) + t18 * 4));
                    gm_tn68el8d14vku t21;
                    GM_ST(uint32_t, (uint8_t *)&t21 + 12, 0);
                    GM_ST(uint64_t, ((uint8_t *)&t21 + 0), t16);
                    GM_ST(uint32_t, ((uint8_t *)&t21 + 8), t20);
                    gm_tn68el8d14vku t22 = t21;
                    uint64_t t25 = GM_LD(uint64_t, (((uint8_t *)&t22) + 0));
                    gm_l7 = t25;
                    uint32_t t26 = GM_LD(uint32_t, (((uint8_t *)&t22) + 8));
                    gm_l8 = t26;
                    {
                        uint64_t t28 = gm_l7;
                        uint64_t t29 = (uintptr_t)t28;
                        uint64_t t30 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                        if (t29 >= (uintptr_t)(t30)) gm_trap_index(t29, t30, gm_srcrorgjlg51brd, 414, 14);
                        gm_t88rcucg1b7uk t31 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t29 * 4));
                        gm_l9 = t31;
                    }
                    {
                        void * t33 = gm_l4;
                        gm_t88rcucg1b7uk t34 = gm_l9;
                        uint8_t t35 = gm_f_is_error_8k27gd1(t33, GM_CONV(gm_csguankko4lmhd, t34));
                        if (!t35) goto ie32;
                        {
                            uint8_t t37 = 0;
                            F.ret = ((uint8_t)0);
                            goto gm_fret;
                        }
                        goto ix32;
ie32:;
ix32:;
                    }
                    {
                        void * t39 = gm_l0;
                        void * t40 = GM_LD(void *, (((uint8_t *)t39) + 0));
                        gm_t88rcucg1b7uk t41 = gm_l9;
                        gm_tkn42u7na3t81 t42 = gm_f_second_class_part_k6i54mk(t40, GM_CONV(gm_csguankko4lmhd, t41));
                        gm_tkn42u7na3t81 t43 = t42;
                        if (!((((uint8_t *)&t43))[4] != 0)) goto ie38;
                        gm_t88rcucg1b7uk t44 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&t43));
                        gm_l10 = t44;
                        {
                            void * t46 = gm_l0;
                            gm_tl874t2atnlh3 t47 = gm_l3;
                            void * t48 = gm_l0;
                            void * t49 = GM_LD(void *, (((uint8_t *)t48) + 0));
                            gm_t88rcucg1b7uk t50 = gm_l9;
                            gm_t88rcucg1b7uk t51 = gm_l10;
                            gm_b16_8 t52 = gm_f_not_first_class_dve75oi((void *)NULL, cx, NULL, t49, t50, t51);
                            if (gm_unw.kind) {
                                goto gm_ll14;
                            }
                            gm_b0_1 t53 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t46, t47, t52);
                            if (gm_unw.kind) {
                                goto gm_ll14;
                            }
                        }
                        goto ix38;
ie38:;
ix38:;
                    }
                    {
                        {
                            void * t55 = gm_l0;
                            void * t56 = GM_LD(void *, (((uint8_t *)t55) + 0));
                            uint32_t t57 = gm_l8;
                            gm_b16_8 t58 = gm_f_param_constraints_2k5fnnl(t56, t57);
                            gm_l11 = t58;
                        }
                        {
                            gm_l12 = ((uint64_t)0ULL);
                        }
                        {
                            for (;;) {
                                uint64_t t60 = gm_l12;
                                uint64_t t61 = GM_LD(uintptr_t, ((uint8_t *)&gm_l11) + 8);
                                uint8_t t62 = (t60 < t61);
                                if (!t62) goto lb59;
                                {
                                    uint64_t t65 = gm_l12;
                                    uint64_t t66 = (uintptr_t)t65;
                                    uint64_t t67 = GM_LD(uintptr_t, ((uint8_t *)&gm_l11) + 8);
                                    gm_t9qkh0rsooc8d t70 = GM_LD(gm_t9qkh0rsooc8d, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l11)) + t66 * 1));
                                    gm_l13 = t70;
                                    {
                                        void * t73 = gm_l0;
                                        void * t74 = GM_LD(void *, (((uint8_t *)t73) + 0));
                                        gm_t88rcucg1b7uk t75 = gm_l9;
                                        gm_t9qkh0rsooc8d t76 = gm_l13;
                                        uint8_t t77 = gm_f_satisfies_p0n2er8((void *)NULL, cx, NULL, t74, t75, t76);
                                        if (gm_unw.kind) {
                                            goto gm_ll63;
                                        }
                                        uint8_t t78 = (uint8_t)!t77;
                                        if (!t78) goto ie72;
                                        {
                                            void * t80 = gm_l0;
                                            gm_tl874t2atnlh3 t81 = gm_l3;
                                            gm_tfeab279c16rr t82;
                                            void * t83 = gm_l0;
                                            gm_t88rcucg1b7uk t84 = gm_l9;
                                            gm_b16_8 t85 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t83, t84);
                                            if (gm_unw.kind) {
                                                goto gm_ll63;
                                            }
                                            GM_ST(gm_b16_8, (uint8_t *)&t82 + 0, t85);
                                            gm_t9qkh0rsooc8d t86 = gm_l13;
                                            gm_b16_8 t87 = gm_f_constraint_name_ombut3o(t86);
                                            GM_ST(gm_b16_8, (uint8_t *)&t82 + 16, t87);
                                            void * t88 = gm_l4;
                                            uint32_t t89 = gm_l8;
                                            gm_b16_8 t90 = gm_f_param_name_rbnop5l(t88, t89);
                                            GM_ST(gm_b16_8, (uint8_t *)&t82 + 32, t90);
                                            gm_tabb710d9mq2e t91;
                                            gm_f_format_d2d7bae((void *)&t91, (void *)NULL, cx, NULL, &t82);
                                            if (gm_unw.kind) {
                                                goto gm_ll63;
                                            }
                                            gm_tabb710d9mq2e t92 = t91;
                                            if (((((uint8_t *)&t92))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t92) + 8, gm_srcrorgjlg51brd, 419, 32);
                                            gm_b16_8 t93 = GM_LD(gm_b16_8, (((uint8_t *)&t92) + 8));
                                            gm_b16_8 t94 = t93;
                                            gm_b16_8 t95 = GM_LD(gm_b16_8, ((uint8_t *)&t94));
                                            gm_b0_1 t96 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t80, t81, t95);
                                            if (gm_unw.kind) {
                                                goto gm_ll63;
                                            }
                                        }
                                        goto ix72;
ie72:;
ix72:;
                                    }
                                    goto me68;
mn69:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 417, 9);
me68:;
                                }
lc59:;
                                {
                                    uint64_t t98 = gm_l12;
                                    uint64_t r99; bool o100 = __builtin_add_overflow(t98, ((uint64_t)1ULL), &r99);
                                    uint64_t t101 = r99;
                                    gm_l12 = t101;
                                }
                            }
gm_ll63:;
                            if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb59; }
                                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc59; }
                            }
                            goto gm_ll14;
lb59:;
                        }
                    }
                    goto me23;
mn24:;
                    gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 413, 5);
me23:;
                }
lc10:;
                {
                    uint64_t t103 = gm_l6;
                    uint64_t r104; bool o105 = __builtin_add_overflow(t103, ((uint64_t)1ULL), &r104);
                    uint64_t t106 = r104;
                    gm_l6 = t106;
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
        uint8_t t107 = 0;
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
inline __attribute__((aligned(4))) uint8_t gm_f_clearly_value_f7icrt5(void * a0, void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_clearly_value_f7icrt5 F;
    void * gm_l0; /* cx */
    void * gm_l1; /* e */
    gm_t08olq97rscn3 gm_l2; /* id */
    GM_STACK_CHECK(&F, 766u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l1;
    uint8_t t2;
    uint8_t t8 = GM_LD(uint8_t, ((((uint8_t *)t1) + 8) + 0));
    switch (t8) { case ((uint8_t)0ULL): goto ma5; case ((uint8_t)1ULL): goto ma5; case ((uint8_t)2ULL): goto ma5; case ((uint8_t)3ULL): goto ma5; case ((uint8_t)4ULL): goto ma6; case ((uint8_t)5ULL): goto ma6; case ((uint8_t)6ULL): goto ma4; case ((uint8_t)7ULL): goto ma6; case ((uint8_t)8ULL): goto ma6; case ((uint8_t)9ULL): goto ma6; case ((uint8_t)10ULL): goto ma6; case ((uint8_t)11ULL): goto ma6; case ((uint8_t)12ULL): goto ma6; case ((uint8_t)13ULL): goto ma6; case ((uint8_t)14ULL): goto ma6; case ((uint8_t)15ULL): goto ma6; case ((uint8_t)16ULL): goto ma6; case ((uint8_t)17ULL): goto ma6; case ((uint8_t)18ULL): goto ma6; case ((uint8_t)19ULL): goto ma6; case ((uint8_t)20ULL): goto ma6; case ((uint8_t)21ULL): goto ma6; case ((uint8_t)22ULL): goto ma6; case ((uint8_t)23ULL): goto ma6; case ((uint8_t)24ULL): goto ma6; case ((uint8_t)25ULL): goto ma6; case ((uint8_t)26ULL): goto ma6; case ((uint8_t)27ULL): goto ma6; case ((uint8_t)28ULL): goto ma6; case ((uint8_t)29ULL): goto ma6; case ((uint8_t)30ULL): goto ma6; case ((uint8_t)31ULL): goto ma6; case ((uint8_t)32ULL): goto ma6; case ((uint8_t)33ULL): goto ma6; case ((uint8_t)34ULL): goto ma6; case ((uint8_t)35ULL): goto ma6; case ((uint8_t)36ULL): goto ma6; case ((uint8_t)37ULL): goto ma6; case ((uint8_t)38ULL): goto ma6; case ((uint8_t)39ULL): goto ma6; case ((uint8_t)40ULL): goto ma6; case ((uint8_t)41ULL): goto ma6; case ((uint8_t)42ULL): goto ma6; case ((uint8_t)43ULL): goto ma6; case ((uint8_t)44ULL): goto ma6; case ((uint8_t)45ULL): goto ma6; case ((uint8_t)46ULL): goto ma6; case ((uint8_t)47ULL): goto ma6; case ((uint8_t)48ULL): goto ma6; case ((uint8_t)49ULL): goto ma6; case ((uint8_t)50ULL): goto ma6; default: goto ma6; }
ma4:;
    uint8_t t10 = GM_LD(uint8_t, ((((uint8_t *)t1) + 8) + 0));
    if (t10 != ((uint8_t)6ULL)) goto mn9;
    gm_t08olq97rscn3 t11 = GM_LD(gm_t08olq97rscn3, ((((uint8_t *)t1) + 8) + 8));
    gm_l2 = t11;
    uint8_t t12;
    void * t14 = gm_l0;
    gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l2) + 0));
    gm_tqvu78rpi6q1h t16;
    gm_f_lookup_local_6f38t7p((void *)&t16, t14, t15);
    gm_tqvu78rpi6q1h t17 = t16;
    if (!((((uint8_t *)&t17))[208] != 0)) goto ie13;
    gm_t930q5l38hdap t18 = GM_LD(gm_t930q5l38hdap, ((uint8_t *)&t17));
    F.l3 = t18;
    uint8_t t20;
    uint8_t t23 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 0));
    if (t23 != ((uint8_t)5ULL)) goto mn22;
    t20 = ((uint8_t)0);
    goto me21;
mn22:;
    t20 = ((uint8_t)1);
    goto me21;
mn24:;
    gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 679, 54);
me21:;
    uint8_t t25 = t20;
    t12 = t25;
    goto ix13;
ie13:;
    uint8_t t27 = ((uint8_t)0);
    t12 = t27;
ix13:;
    t2 = t12;
    goto me3;
mn9:;
ma5:;
    uint8_t t31 = GM_LD(uint8_t, ((((uint8_t *)t1) + 8) + 0));
    if (t31 != ((uint8_t)0ULL)) goto pa30;
    goto po29;
pa30:;
    uint8_t t33 = GM_LD(uint8_t, ((((uint8_t *)t1) + 8) + 0));
    if (t33 != ((uint8_t)1ULL)) goto pa32;
    goto po29;
pa32:;
    uint8_t t35 = GM_LD(uint8_t, ((((uint8_t *)t1) + 8) + 0));
    if (t35 != ((uint8_t)2ULL)) goto pa34;
    goto po29;
pa34:;
    uint8_t t37 = GM_LD(uint8_t, ((((uint8_t *)t1) + 8) + 0));
    if (t37 != ((uint8_t)3ULL)) goto pa36;
    goto po29;
pa36:;
    goto mn28;
po29:;
    t2 = ((uint8_t)1);
    goto me3;
mn28:;
ma6:;
    t2 = ((uint8_t)0);
    goto me3;
mn38:;
ma7:;
    gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 678, 63);
me3:;
    F.ret = t2;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) void gm_f_coerce_generic_args_9ald24q(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_b16_8 a2, gm_b16_8 a3, gm_tl874t2atnlh3 a4) {
    struct gm_fr_coerce_generic_args_9ald24q F;
    void * gm_l0; /* cx */
    void * gm_l1; /* sig */
    gm_b16_8 gm_l2; /* values */
    gm_b16_8 gm_l3; /* map */
    gm_tl874t2atnlh3 gm_l4; /* span */
    gm_b16_8 gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    uint64_t gm_l9; /* i */
    gm_t88rcucg1b7uk gm_l11; /* pty */
    void * gm_l12; /* v */
    void * gm_l13; /* d */
    GM_STACK_CHECK(&F, 870u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    {
        gm_f_list_vp1djv1_2((void *)&F.l5, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_f_list_5dhgg4i((void *)&F.l6, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            void * t3 = gm_l1;
            gm_b16_8 t4 = GM_LD(gm_b16_8, (((uint8_t *)t3) + 40));
            gm_b16_8 t5 = t4;
            gm_b16_8 t6 = GM_LD(gm_b16_8, ((uint8_t *)&t5));
            gm_l7 = t6;
        }
        {
            gm_l8 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t8 = gm_l8;
                uint64_t t9 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                uint8_t t10 = (t8 < t9);
                if (!t10) goto lb7;
                {
                    gm_tgsvmddj3e85e t13;
                    uint64_t t14 = gm_l8;
                    GM_ST(uint64_t, (uint8_t *)&t13 + 0, t14);
                    uint64_t t15 = gm_l8;
                    uint64_t t16 = (uintptr_t)t15;
                    uint64_t t17 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                    memcpy((uint8_t *)&t13 + 8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l7)) + t16 * 56), 56);
                    gm_tgsvmddj3e85e t18 = t13;
                    uint64_t t21 = GM_LD(uint64_t, (((uint8_t *)&t18) + 0));
                    gm_l9 = t21;
                    gm_t5v0c8ia6a5mo t22 = GM_LD(gm_t5v0c8ia6a5mo, (((uint8_t *)&t18) + 8));
                    F.l10 = t22;
                    {
                        void * t24 = (void *)((uint8_t *)&F.l6);
                        uint64_t t25 = GM_LD(uintptr_t, (((uint8_t *)&F.l5) + 0) + 8);
                        gm_tftb1odc739t5 t26 = gm_f_push_ucgmlo2(t24, t25);
                        gm_tftb1odc739t5 t27 = t26;
                        if (((((uint8_t *)&t27))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t27) + 1, gm_srcrorgjlg51brd, 434, 9);
                        gm_b0_1 t28 = GM_LD(gm_b0_1, (((uint8_t *)&t27) + 1));
                    }
                    {
                        uint8_t t30;
                        if ((((((uint8_t *)&F.l10) + 40))[4] != 0)) goto mn32;
                        t30 = ((uint8_t)0);
                        goto me31;
mn32:;
                        t30 = ((uint8_t)1);
                        goto me31;
mn33:;
                        gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 435, 12);
me31:;
                        if (!t30) goto ie29;
                        {
                            goto lc7;
                        }
                        goto ix29;
ie29:;
ix29:;
                    }
                    {
                        void * t35 = gm_l0;
                        void * t36 = GM_LD(void *, (((uint8_t *)t35) + 0));
                        gm_t88rcucg1b7uk t37 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l10) + 16));
                        gm_b16_8 t38 = gm_l3;
                        gm_t88rcucg1b7uk t39 = gm_f_subst_a2s1mb3((void *)NULL, cx, NULL, t36, t37, t38);
                        if (gm_unw.kind) {
                            goto gm_ll11;
                        }
                        gm_l11 = t39;
                    }
                    {
                        uint64_t t41 = gm_l9;
                        uint64_t t42 = (uintptr_t)t41;
                        uint64_t t43 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                        if (t42 >= (uintptr_t)(t43)) gm_trap_index(t42, t43, gm_srcrorgjlg51brd, 437, 17);
                        void * t44 = GM_LD(void *, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t42 * 8));
                        void * t45 = t44;
                        if (!(GM_LD(void *, ((uint8_t *)&t45)) != 0)) goto ie40;
                        void * t46 = GM_LD(void *, ((uint8_t *)&t45));
                        gm_l12 = t46;
                        {
                            void * t48 = (void *)((uint8_t *)&F.l5);
                            void * t49 = gm_l0;
                            void * t50 = gm_l12;
                            gm_t88rcucg1b7uk t51 = gm_l11;
                            void * t52 = gm_f_coerce_sudc6c1((void *)NULL, cx, NULL, t49, t50, t51);
                            if (gm_unw.kind) {
                                goto gm_ll11;
                            }
                            gm_tftb1odc739t5 t53 = gm_f_push_d943vgp_2(t48, &(*(gm_tfpf0s89fpb3r *)((uint8_t *)t52)));
                            gm_tftb1odc739t5 t54 = t53;
                            if (((((uint8_t *)&t54))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t54) + 1, gm_srcrorgjlg51brd, 438, 13);
                            gm_b0_1 t55 = GM_LD(gm_b0_1, (((uint8_t *)&t54) + 1));
                        }
                        goto ix40;
ie40:;
                        void * t57 = GM_LD(void *, (((uint8_t *)&F.l10) + 32));
                        void * t58 = t57;
                        if (!(GM_LD(void *, ((uint8_t *)&t58)) != 0)) goto ie56;
                        void * t59 = GM_LD(void *, ((uint8_t *)&t58));
                        gm_l13 = t59;
                        {
                            void * t61 = (void *)((uint8_t *)&F.l5);
                            void * t62 = gm_l0;
                            void * t63 = (void *)((uint8_t *)&F.l10);
                            void * t64 = gm_l1;
                            uint64_t t65 = GM_LD(uint64_t, (((uint8_t *)t64) + 32));
                            void * t66 = gm_l13;
                            gm_t88rcucg1b7uk t67 = gm_l11;
                            gm_tl874t2atnlh3 t68 = gm_l4;
                            void * t69 = gm_f_param_default_h93ci1m((void *)NULL, cx, NULL, t62, t63, t65, t66, t67, t68);
                            if (gm_unw.kind) {
                                goto gm_ll11;
                            }
                            gm_tftb1odc739t5 t70 = gm_f_push_d943vgp_2(t61, &(*(gm_tfpf0s89fpb3r *)((uint8_t *)t69)));
                            gm_tftb1odc739t5 t71 = t70;
                            if (((((uint8_t *)&t71))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t71) + 1, gm_srcrorgjlg51brd, 440, 13);
                            gm_b0_1 t72 = GM_LD(gm_b0_1, (((uint8_t *)&t71) + 1));
                        }
                        goto ix56;
ie56:;
                        {
                            void * t74 = gm_l0;
                            gm_tl874t2atnlh3 t75 = gm_l4;
                            gm_b16_8 t76 = GM_LD(gm_b16_8, (((uint8_t *)&F.l10) + 0));
                            gm_tnpifv20irnph t77;
                            GM_ST(gm_b16_8, ((uint8_t *)&t77 + 0), t76);
                            gm_tabb710d9mq2e t78;
                            gm_f_format_qk20moi((void *)&t78, (void *)NULL, cx, NULL, t77);
                            if (gm_unw.kind) {
                                goto gm_ll11;
                            }
                            gm_tabb710d9mq2e t79 = t78;
                            if (((((uint8_t *)&t79))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t79) + 8, gm_srcrorgjlg51brd, 442, 28);
                            gm_b16_8 t80 = GM_LD(gm_b16_8, (((uint8_t *)&t79) + 8));
                            gm_b16_8 t81 = t80;
                            gm_b16_8 t82 = GM_LD(gm_b16_8, ((uint8_t *)&t81));
                            gm_b0_1 t83 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t74, t75, t82);
                            if (gm_unw.kind) {
                                goto gm_ll11;
                            }
                        }
                        {
                            void * t84 = (void *)((uint8_t *)&F.l5);
                            void * t85 = gm_l0;
                            void * t86 = GM_LD(void *, (((uint8_t *)t85) + 0));
                            gm_tl874t2atnlh3 t87 = gm_l4;
                            void * t88 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t86, t87);
                            if (gm_unw.kind) {
                                goto gm_ll11;
                            }
                            gm_tftb1odc739t5 t89 = gm_f_push_d943vgp_2(t84, &(*(gm_tfpf0s89fpb3r *)((uint8_t *)t88)));
                            gm_tftb1odc739t5 t90 = t89;
                            if (((((uint8_t *)&t90))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t90) + 1, gm_srcrorgjlg51brd, 443, 13);
                            gm_b0_1 t91 = GM_LD(gm_b0_1, (((uint8_t *)&t90) + 1));
                        }
ix56:;
ix40:;
                    }
                    goto me19;
mn20:;
                    gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 433, 5);
me19:;
                }
lc7:;
                {
                    uint64_t t93 = gm_l8;
                    uint64_t r94; bool o95 = __builtin_add_overflow(t93, ((uint64_t)1ULL), &r94);
                    uint64_t t96 = r94;
                    gm_l8 = t96;
                }
            }
gm_ll11:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb7; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc7; }
            }
            goto gm_lad;
lb7:;
        }
    }
    {
        memcpy((uint8_t *)&(*(gm_td7pkamr64l0c *)gm_out) + 0, ((uint8_t *)&F.l5), 40);
        memcpy((uint8_t *)&(*(gm_td7pkamr64l0c *)gm_out) + 40, ((uint8_t *)&F.l6), 40);
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
inline __attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_comptime_arg_vtq6sop(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, void * a2) {
    struct gm_fr_comptime_arg_vtq6sop F;
    void * gm_l0; /* cx */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    void * gm_l2; /* e */
    gm_t08olq97rscn3 gm_l3; /* id */
    GM_STACK_CHECK(&F, 996u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t3 = gm_l2;
        uint8_t t4 = GM_LD(uint8_t, ((((uint8_t *)t3) + 8) + 0));
        if (t4 != ((uint8_t)6ULL)) goto ie2;
        gm_t08olq97rscn3 t5 = GM_LD(gm_t08olq97rscn3, ((((uint8_t *)t3) + 8) + 8));
        gm_l3 = t5;
        {
            void * t8 = gm_l0;
            gm_b16_8 t9 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_tqvu78rpi6q1h t10;
            gm_f_lookup_local_6f38t7p((void *)&t10, t8, t9);
            gm_tqvu78rpi6q1h t11 = t10;
            if (!((((uint8_t *)&t11))[208] != 0)) goto ie7;
            gm_t930q5l38hdap t12 = GM_LD(gm_t930q5l38hdap, ((uint8_t *)&t11));
            F.l4 = t12;
            uint8_t t13;
            uint8_t t20 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 0));
            switch (t20) { case ((uint8_t)0ULL): goto ma18; case ((uint8_t)1ULL): goto ma16; case ((uint8_t)2ULL): goto ma18; case ((uint8_t)3ULL): goto ma15; case ((uint8_t)4ULL): goto ma17; case ((uint8_t)5ULL): goto ma18; default: goto ma18; }
ma15:;
            uint8_t t22 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 0));
            if (t22 != ((uint8_t)3ULL)) goto mn21;
            t13 = ((uint8_t)0);
            goto me14;
mn21:;
ma16:;
            uint8_t t24 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 0));
            if (t24 != ((uint8_t)1ULL)) goto mn23;
            t13 = ((uint8_t)0);
            goto me14;
mn23:;
ma17:;
            uint8_t t26 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 0));
            if (t26 != ((uint8_t)4ULL)) goto mn25;
            t13 = ((uint8_t)0);
            goto me14;
mn25:;
ma18:;
            t13 = ((uint8_t)1);
            goto me14;
mn27:;
ma19:;
            gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 506, 46);
me14:;
            if (!t13) goto ie7;
            {
                void * t29 = gm_l0;
                void * t30 = gm_l2;
                gm_tl874t2atnlh3 t31 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t30) + 120));
                gm_b16_8 t32 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
                gm_tnpifv20irnph t33;
                GM_ST(gm_b16_8, ((uint8_t *)&t33 + 0), t32);
                gm_tabb710d9mq2e t34;
                gm_f_format_8tl219j((void *)&t34, (void *)NULL, cx, NULL, t33);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t35 = t34;
                if (((((uint8_t *)&t35))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t35) + 8, gm_srcrorgjlg51brd, 507, 30);
                gm_b16_8 t36 = GM_LD(gm_b16_8, (((uint8_t *)&t35) + 8));
                gm_b16_8 t37 = t36;
                gm_b16_8 t38 = GM_LD(gm_b16_8, ((uint8_t *)&t37));
                gm_b0_1 t39 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t29, t31, t38);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                gm_tkn42u7na3t81 t40;
                memset(&t40, 0, sizeof t40);
                uint8_t t41 = 0;
                F.ret = t40;
                goto gm_fret;
            }
            goto ix7;
ie7:;
ix7:;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t42 = gm_l0;
        void * t43 = GM_LD(void *, (((uint8_t *)t42) + 0));
        void * t44 = gm_l0;
        uint64_t t45 = GM_LD(uint64_t, (((uint8_t *)t44) + 8));
        gm_t88rcucg1b7uk t46 = gm_l1;
        void * t47 = gm_l2;
        gm_tkn42u7na3t81 t48 = gm_f_comptime_value_l5orc93((void *)NULL, cx, NULL, t43, t45, t46, t47);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t49 = 0;
        F.ret = t48;
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
__attribute__((aligned(4))) uint8_t gm_f_denotes_type_jgfet92(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_denotes_type_jgfet92 F;
    void * gm_l0; /* cx */
    void * gm_l1; /* e */
    void * gm_l2; /* inner */
    gm_t08olq97rscn3 gm_l3; /* id */
    GM_STACK_CHECK(&F, 806u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l1;
        uint8_t t10 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        switch (t10) { case ((uint8_t)0ULL): goto ma8; case ((uint8_t)1ULL): goto ma8; case ((uint8_t)2ULL): goto ma8; case ((uint8_t)3ULL): goto ma8; case ((uint8_t)4ULL): goto ma8; case ((uint8_t)5ULL): goto ma8; case ((uint8_t)6ULL): goto ma5; case ((uint8_t)7ULL): goto ma8; case ((uint8_t)8ULL): goto ma8; case ((uint8_t)9ULL): goto ma4; case ((uint8_t)10ULL): goto ma8; case ((uint8_t)11ULL): goto ma8; case ((uint8_t)12ULL): goto ma8; case ((uint8_t)13ULL): goto ma8; case ((uint8_t)14ULL): goto ma8; case ((uint8_t)15ULL): goto ma8; case ((uint8_t)16ULL): goto ma8; case ((uint8_t)17ULL): goto ma8; case ((uint8_t)18ULL): goto ma8; case ((uint8_t)19ULL): goto ma6; case ((uint8_t)20ULL): goto ma8; case ((uint8_t)21ULL): goto ma8; case ((uint8_t)22ULL): goto ma6; case ((uint8_t)23ULL): goto ma8; case ((uint8_t)24ULL): goto ma8; case ((uint8_t)25ULL): goto ma8; case ((uint8_t)26ULL): goto ma8; case ((uint8_t)27ULL): goto ma8; case ((uint8_t)28ULL): goto ma8; case ((uint8_t)29ULL): goto ma8; case ((uint8_t)30ULL): goto ma8; case ((uint8_t)31ULL): goto ma8; case ((uint8_t)32ULL): goto ma8; case ((uint8_t)33ULL): goto ma8; case ((uint8_t)34ULL): goto ma8; case ((uint8_t)35ULL): goto ma8; case ((uint8_t)36ULL): goto ma8; case ((uint8_t)37ULL): goto ma8; case ((uint8_t)38ULL): goto ma8; case ((uint8_t)39ULL): goto ma8; case ((uint8_t)40ULL): goto ma8; case ((uint8_t)41ULL): goto ma8; case ((uint8_t)42ULL): goto ma8; case ((uint8_t)43ULL): goto ma8; case ((uint8_t)44ULL): goto ma8; case ((uint8_t)45ULL): goto ma8; case ((uint8_t)46ULL): goto ma8; case ((uint8_t)47ULL): goto ma8; case ((uint8_t)48ULL): goto ma8; case ((uint8_t)49ULL): goto ma7; case ((uint8_t)50ULL): goto ma8; default: goto ma8; }
ma4:;
        uint8_t t12 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t12 != ((uint8_t)9ULL)) goto mn11;
        void * t13 = GM_LD(void *, ((((uint8_t *)t2) + 8) + 8));
        gm_l2 = t13;
        void * t14 = gm_l0;
        void * t15 = gm_l2;
        uint8_t t16 = gm_f_denotes_type_jgfet92((void *)NULL, cx, NULL, t14, t15);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t17 = 0;
        F.ret = t16;
        goto gm_fret;
        goto me3;
mn11:;
ma5:;
        uint8_t t19 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t19 != ((uint8_t)6ULL)) goto mn18;
        gm_t08olq97rscn3 t20 = GM_LD(gm_t08olq97rscn3, ((((uint8_t *)t2) + 8) + 8));
        gm_l3 = t20;
        {
            void * t23 = gm_l0;
            gm_b16_8 t24 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_tqvu78rpi6q1h t25;
            gm_f_lookup_local_6f38t7p((void *)&t25, t23, t24);
            gm_tqvu78rpi6q1h t26 = t25;
            uint8_t t27;
            if (((((uint8_t *)&t26))[208] != 0)) goto mn29;
            t27 = ((uint8_t)0);
            goto me28;
mn29:;
            t27 = ((uint8_t)1);
            goto me28;
mn30:;
            gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 689, 16);
me28:;
            if (!t27) goto ie22;
            {
                uint8_t t32 = 0;
                F.ret = ((uint8_t)0);
                goto gm_fret;
            }
            goto ix22;
ie22:;
ix22:;
        }
        {
            uint8_t t33 = 0;
            void * t34 = gm_l0;
            void * t35 = GM_LD(void *, (((uint8_t *)t34) + 0));
            gm_b16_8 t36 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_tkn42u7na3t81 t37 = gm_f_type_param_3av4r8i(t35, t36);
            gm_tkn42u7na3t81 t38 = t37;
            uint8_t t39;
            if (((((uint8_t *)&t38))[4] != 0)) goto mn41;
            t39 = ((uint8_t)0);
            goto me40;
mn41:;
            t39 = ((uint8_t)1);
            goto me40;
mn42:;
            gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 690, 20);
me40:;
            if (!t39) {
                void * t43 = gm_l0;
                void * t44 = gm_l1;
                gm_tkn42u7na3t81 t45 = gm_f_type_of_name_oqtujpn((void *)NULL, cx, NULL, t43, t44);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tkn42u7na3t81 t46 = t45;
                uint8_t t47;
                if (((((uint8_t *)&t46))[4] != 0)) goto mn49;
                t47 = ((uint8_t)0);
                goto me48;
mn49:;
                t47 = ((uint8_t)1);
                goto me48;
mn50:;
                gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 690, 60);
me48:;
                t33 = t47;
            } else t33 = 1;
            uint8_t t51 = 0;
            F.ret = t33;
            goto gm_fret;
        }
        goto me3;
mn18:;
ma6:;
        uint8_t t55 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t55 != ((uint8_t)19ULL)) goto pa54;
        goto po53;
pa54:;
        uint8_t t57 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t57 != ((uint8_t)22ULL)) goto pa56;
        goto po53;
pa56:;
        goto mn52;
po53:;
        void * t58 = gm_l0;
        void * t59 = gm_l1;
        gm_tkn42u7na3t81 t60 = gm_f_type_of_name_oqtujpn((void *)NULL, cx, NULL, t58, t59);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tkn42u7na3t81 t61 = t60;
        uint8_t t62;
        if (((((uint8_t *)&t61))[4] != 0)) goto mn64;
        t62 = ((uint8_t)0);
        goto me63;
mn64:;
        t62 = ((uint8_t)1);
        goto me63;
mn65:;
        gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 692, 39);
me63:;
        uint8_t t66 = 0;
        F.ret = t62;
        goto gm_fret;
        goto me3;
mn52:;
ma7:;
        uint8_t t68 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t68 != ((uint8_t)49ULL)) goto mn67;
        gm_tet9guhev5r4u t69 = GM_LD(gm_tet9guhev5r4u, ((((uint8_t *)t2) + 8) + 8));
        F.l4 = t69;
        uint8_t t70 = (((GM_LD(uintptr_t, ((((uint8_t *)&F.l4) + 0) + 0) + 8) == 7) && memcmp(GM_LD(void *, ((((uint8_t *)&F.l4) + 0) + 0)), gm_slbgk65sat4bm, 7) == 0));
        uint8_t t71 = 0;
        F.ret = t70;
        goto gm_fret;
        goto me3;
mn67:;
ma8:;
        void * t73 = gm_l0;
        void * t74 = GM_LD(void *, (((uint8_t *)t73) + 0));
        void * t75 = gm_l0;
        uint64_t t76 = GM_LD(uint64_t, (((uint8_t *)t75) + 8));
        void * t77 = gm_l1;
        uint8_t t78 = gm_f_looks_like_type_ho5qv1k((void *)NULL, cx, NULL, t74, t76, t77);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t79 = 0;
        F.ret = t78;
        goto gm_fret;
        goto me3;
mn72:;
ma9:;
        gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 686, 5);
me3:;
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
inline __attribute__((aligned(4))) uint8_t gm_f_effect_args_name_jgklot1(void * a0, void * a1, uint32_t a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_effect_args_name_jgklot1 F;
    void * gm_l1; /* e */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    gm_t88rcucg1b7uk gm_l7; /* t */
    GM_STACK_CHECK(&F, 216u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    gm_l1 = a1;
    F.l2 = a2;
    {
        void * t3 = gm_l1;
        uint32_t t4 = GM_LD(uint32_t, (((uint8_t *)t3) + 0));
        uint8_t t5 = ((t4 == ((uint32_t)0ULL)));
        if (!t5) goto ie2;
        {
            uint8_t t7 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        F.l3 = ((uint8_t)0);
    }
    {
        {
            void * t9 = gm_l1;
            gm_b16_8 t10 = GM_LD(gm_b16_8, (((uint8_t *)t9) + 8));
            gm_l4 = t10;
        }
        {
            gm_l5 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t12 = gm_l5;
                uint64_t t13 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                uint8_t t14 = (t12 < t13);
                if (!t14) goto lb11;
                {
                    uint64_t t17 = gm_l5;
                    uint64_t t18 = (uintptr_t)t17;
                    uint64_t t19 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                    gm_toa2vhe3l4hfr t22 = GM_LD(gm_toa2vhe3l4hfr, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t18 * 40));
                    F.l6 = t22;
                    {
                        uint8_t t25 = GM_LD(uint8_t, (((uint8_t *)&F.l6) + 0));
                        if (t25 != ((uint8_t)0ULL)) goto ie24;
                        gm_t88rcucg1b7uk t26 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l6) + 8));
                        gm_l7 = t26;
                        {
                            void * t28 = F.l0;
                            gm_t88rcucg1b7uk t29 = gm_l7;
                            gm_closure t30 = ((gm_closure){(void *)gm_f_fn_tqlm0ij, (void *)&F.h, cx});
                            gm_f_walk_v858co7(t28, GM_CONV(gm_csguankko4lmhd, t29), t30);
                            if (gm_unw.kind) {
                                goto gm_ll15;
                            }
                        }
                        goto ix24;
ie24:;
                        {
                            goto lc11;
                        }
ix24:;
                    }
                    goto me20;
mn21:;
                    gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 598, 5);
me20:;
                }
lc11:;
                {
                    uint64_t t33 = gm_l5;
                    uint64_t r34; bool o35 = __builtin_add_overflow(t33, ((uint64_t)1ULL), &r34);
                    uint64_t t36 = r34;
                    gm_l5 = t36;
                }
            }
gm_ll15:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb11; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc11; }
            }
            goto gm_lad;
lb11:;
        }
    }
    {
        uint8_t t37 = F.l3;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_44t30pu(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0) {
    struct gm_fr_fn_44t30pu F;
    gm_t88rcucg1b7uk gm_l0; /* x */
    uint32_t gm_l1; /* q */
    GM_STACK_CHECK(&F, 195u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t3 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_generic_call_packed_h7ssi9n *)((&F.h)->up))->l0));
        void * t4 = GM_LD(void *, (((uint8_t *)t3) + 0));
        void * t5 = (void *)(((uint8_t *)t4) + 8);
        gm_t88rcucg1b7uk t6 = gm_l0;
        gm_t53at80pfdus3 t7;
        gm_f_kind_jcnt4hm((void *)&t7, t5, GM_CONV(gm_csguankko4lmhd, t6));
        gm_t53at80pfdus3 t8 = t7;
        uint8_t t9 = GM_LD(uint8_t, (((uint8_t *)&t8) + 0));
        if (t9 != ((uint8_t)19ULL)) goto ie2;
        uint32_t t10 = GM_LD(uint32_t, (((uint8_t *)&t8) + 8));
        gm_l1 = t10;
        uint32_t t11 = gm_l1;
        gm_b16_8 t12 = GM_LD(gm_b16_8, (((uint8_t *)&((struct gm_fr_generic_call_packed_h7ssi9n *)((&F.h)->up))->l59) + 0));
        gm_b16_8 t13 = t12;
        uint8_t t14 = 0;
        uint32_t t15 = t11;
        for (uintptr_t i = 0; i < (uintptr_t)(GM_LD(uintptr_t, ((uint8_t *)&t13) + 8)); i++) if (gm_eq467d5cm1d5(((uint8_t *)GM_LD(void *, ((uint8_t *)&t13))) + i * 4, ((uint8_t *)&t15))) { t14 = 1; break; }
        uint8_t t16 = (uint8_t)(!t14);
        if (!t16) goto ie2;
        {
            GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_generic_call_packed_h7ssi9n *)((&F.h)->up))->l82), ((uint8_t)1));
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
inline __attribute__((aligned(4))) uint8_t gm_f_fn_dgpelc4(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_fn_dgpelc4 F;
    void * gm_l0; /* a */
    void * gm_l1; /* b */
    GM_STACK_CHECK(&F, 99u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_b16_8 t1 = GM_LD(gm_b16_8, ((uint8_t *)&((struct gm_fr_order_generic_args_m8gcs0c *)((&F.h)->up))->l5));
    void * t2 = gm_l0;
    uint64_t t3 = GM_LD(uint64_t, ((uint8_t *)t2));
    uint64_t t4 = gm_f_source_position_taggjb0(t1, t3);
    gm_b16_8 t5 = GM_LD(gm_b16_8, ((uint8_t *)&((struct gm_fr_order_generic_args_m8gcs0c *)((&F.h)->up))->l5));
    void * t6 = gm_l1;
    uint64_t t7 = GM_LD(uint64_t, ((uint8_t *)t6));
    uint64_t t8 = gm_f_source_position_taggjb0(t5, t7);
    uint8_t t9 = (t4 < t8);
    F.ret = t9;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_fn_dink6tf(void * a0, void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_fn_dink6tf F;
    void * gm_l0; /* a */
    void * gm_l1; /* b */
    GM_STACK_CHECK(&F, 37u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    uint8_t t2 = GM_LD(uint8_t, (((uint8_t *)t1) + 144));
    void * t3 = gm_l1;
    uint8_t t4 = GM_LD(uint8_t, (((uint8_t *)t3) + 144));
    uint8_t t5 = (t2 < t4);
    F.ret = t5;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_pjcgmfa(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0) {
    struct gm_fr_fn_pjcgmfa F;
    gm_t88rcucg1b7uk gm_l0; /* x */
    uint32_t gm_l1; /* q */
    GM_STACK_CHECK(&F, 264u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t3 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_generic_call_packed_h7ssi9n *)((&F.h)->up))->l0));
        void * t4 = GM_LD(void *, (((uint8_t *)t3) + 0));
        void * t5 = (void *)(((uint8_t *)t4) + 8);
        gm_t88rcucg1b7uk t6 = gm_l0;
        gm_t53at80pfdus3 t7;
        gm_f_kind_jcnt4hm((void *)&t7, t5, GM_CONV(gm_csguankko4lmhd, t6));
        gm_t53at80pfdus3 t8 = t7;
        uint8_t t9 = GM_LD(uint8_t, (((uint8_t *)&t8) + 0));
        if (t9 != ((uint8_t)19ULL)) goto ie2;
        uint32_t t10 = GM_LD(uint32_t, (((uint8_t *)&t8) + 8));
        gm_l1 = t10;
        uint32_t t11 = gm_l1;
        gm_b16_8 t12 = GM_LD(gm_b16_8, (((uint8_t *)&((struct gm_fr_generic_call_packed_h7ssi9n *)((&F.h)->up))->l5) + 80));
        gm_b16_8 t13 = t12;
        uint8_t t14 = 0;
        uint32_t t15 = t11;
        for (uintptr_t i = 0; i < (uintptr_t)(GM_LD(uintptr_t, ((uint8_t *)&t13) + 8)); i++) if (gm_eq467d5cm1d5(((uint8_t *)GM_LD(void *, ((uint8_t *)&t13))) + i * 4, ((uint8_t *)&t15))) { t14 = 1; break; }
        uint8_t t16 = (uint8_t)(t14);
        if (!t16) goto ie2;
        gm_b16_8 t17 = GM_LD(gm_b16_8, (((uint8_t *)&((struct gm_fr_generic_call_packed_h7ssi9n *)((&F.h)->up))->l30) + 0));
        gm_b16_8 t18 = t17;
        gm_b16_8 t19 = GM_LD(gm_b16_8, ((uint8_t *)&t18));
        uint32_t t20 = gm_l1;
        gm_tkn42u7na3t81 t21 = gm_f_bound_to_5c11t0h(t19, t20);
        gm_tkn42u7na3t81 t22 = t21;
        uint8_t t23;
        if (((((uint8_t *)&t22))[4] != 0)) goto mn25;
        t23 = ((uint8_t)1);
        goto me24;
mn25:;
        t23 = ((uint8_t)0);
        goto me24;
mn26:;
        gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 182, 72);
me24:;
        if (!t23) goto ie2;
        {
            GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_generic_call_packed_h7ssi9n *)((&F.h)->up))->l73), ((uint8_t)1));
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_qtpj77g(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0) {
    struct gm_fr_fn_qtpj77g F;
    gm_t88rcucg1b7uk gm_l0; /* t */
    gm_t88rcucg1b7uk gm_l1; /* l */
    gm_t88rcucg1b7uk gm_l2; /* r */
    uint32_t gm_l3; /* p */
    GM_STACK_CHECK(&F, 470u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_inferable_param_f80lrdi *)((&F.h)->up))->l0));
        void * t3 = (void *)(((uint8_t *)t2) + 8);
        gm_t88rcucg1b7uk t4 = gm_l0;
        gm_t53at80pfdus3 t5;
        gm_f_kind_jcnt4hm((void *)&t5, t3, GM_CONV(gm_csguankko4lmhd, t4));
        gm_t53at80pfdus3 t6 = t5;
        uint8_t t12 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
        switch (t12) { case ((uint8_t)0ULL): goto ma10; case ((uint8_t)1ULL): goto ma10; case ((uint8_t)2ULL): goto ma10; case ((uint8_t)3ULL): goto ma10; case ((uint8_t)4ULL): goto ma10; case ((uint8_t)5ULL): goto ma10; case ((uint8_t)6ULL): goto ma10; case ((uint8_t)7ULL): goto ma10; case ((uint8_t)8ULL): goto ma10; case ((uint8_t)9ULL): goto ma10; case ((uint8_t)10ULL): goto ma10; case ((uint8_t)11ULL): goto ma10; case ((uint8_t)12ULL): goto ma10; case ((uint8_t)13ULL): goto ma10; case ((uint8_t)14ULL): goto ma10; case ((uint8_t)15ULL): goto ma10; case ((uint8_t)16ULL): goto ma10; case ((uint8_t)17ULL): goto ma10; case ((uint8_t)18ULL): goto ma10; case ((uint8_t)19ULL): goto ma9; case ((uint8_t)20ULL): goto ma10; case ((uint8_t)21ULL): goto ma10; case ((uint8_t)22ULL): goto ma8; case ((uint8_t)23ULL): goto ma10; case ((uint8_t)24ULL): goto ma10; case ((uint8_t)25ULL): goto ma10; case ((uint8_t)26ULL): goto ma10; default: goto ma10; }
ma8:;
        uint8_t t14 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
        if (t14 != ((uint8_t)22ULL)) goto mn13;
        gm_t88rcucg1b7uk t15 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t6) + 8) + 4));
        gm_l1 = t15;
        gm_t88rcucg1b7uk t16 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t6) + 8) + 8));
        gm_l2 = t16;
        {
            void * t19 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_inferable_param_f80lrdi *)((&F.h)->up))->l0));
            void * t20 = (void *)(((uint8_t *)t19) + 8);
            gm_t88rcucg1b7uk t21 = gm_l1;
            gm_t53at80pfdus3 t22;
            gm_f_kind_jcnt4hm((void *)&t22, t20, GM_CONV(gm_csguankko4lmhd, t21));
            gm_t53at80pfdus3 t23 = t22;
            uint32_t t24 = GM_LD(uint32_t, ((uint8_t *)&((struct gm_fr_inferable_param_f80lrdi *)((&F.h)->up))->l3));
            uint8_t t25 = (((GM_LD(uint8_t, ((uint8_t *)&t23) + 0) == ((uint8_t)19ULL)) && (GM_LD(uint32_t, (((uint8_t *)&t23) + 8)) == t24)));
            if (!t25) goto ie18;
            {
                uint32_t t27 = GM_LD(uint32_t, ((uint8_t *)&((struct gm_fr_inferable_param_f80lrdi *)((&F.h)->up))->l12));
                uint32_t r28; bool o29 = __builtin_add_overflow(t27, ((uint32_t)1ULL), &r28);
                if (o29) gm_trap("integer overflow in `+`", gm_srcrorgjlg51brd, 581, 55);
                uint32_t t30 = r28;
                GM_ST(uint32_t, ((uint8_t *)&((struct gm_fr_inferable_param_f80lrdi *)((&F.h)->up))->l12), t30);
            }
            goto ix18;
ie18:;
ix18:;
        }
        {
            void * t32 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_inferable_param_f80lrdi *)((&F.h)->up))->l0));
            void * t33 = (void *)(((uint8_t *)t32) + 8);
            gm_t88rcucg1b7uk t34 = gm_l2;
            gm_t53at80pfdus3 t35;
            gm_f_kind_jcnt4hm((void *)&t35, t33, GM_CONV(gm_csguankko4lmhd, t34));
            gm_t53at80pfdus3 t36 = t35;
            uint32_t t37 = GM_LD(uint32_t, ((uint8_t *)&((struct gm_fr_inferable_param_f80lrdi *)((&F.h)->up))->l3));
            uint8_t t38 = (((GM_LD(uint8_t, ((uint8_t *)&t36) + 0) == ((uint8_t)19ULL)) && (GM_LD(uint32_t, (((uint8_t *)&t36) + 8)) == t37)));
            if (!t38) goto ie31;
            {
                uint32_t t40 = GM_LD(uint32_t, ((uint8_t *)&((struct gm_fr_inferable_param_f80lrdi *)((&F.h)->up))->l12));
                uint32_t r41; bool o42 = __builtin_add_overflow(t40, ((uint32_t)1ULL), &r41);
                if (o42) gm_trap("integer overflow in `+`", gm_srcrorgjlg51brd, 582, 55);
                uint32_t t43 = r41;
                GM_ST(uint32_t, ((uint8_t *)&((struct gm_fr_inferable_param_f80lrdi *)((&F.h)->up))->l12), t43);
            }
            goto ix31;
ie31:;
ix31:;
        }
        goto me7;
mn13:;
ma9:;
        uint8_t t45 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
        if (t45 != ((uint8_t)19ULL)) goto mn44;
        uint32_t t46 = GM_LD(uint32_t, (((uint8_t *)&t6) + 8));
        gm_l3 = t46;
        uint32_t t48 = gm_l3;
        uint32_t t49 = GM_LD(uint32_t, ((uint8_t *)&((struct gm_fr_inferable_param_f80lrdi *)((&F.h)->up))->l3));
        uint8_t t50 = ((t48 == t49));
        if (!t50) goto ie47;
        {
            uint32_t t52 = GM_LD(uint32_t, ((uint8_t *)&((struct gm_fr_inferable_param_f80lrdi *)((&F.h)->up))->l11));
            uint32_t r53; bool o54 = __builtin_add_overflow(t52, ((uint32_t)1ULL), &r53);
            if (o54) gm_trap("integer overflow in `+`", gm_srcrorgjlg51brd, 584, 44);
            uint32_t t55 = r53;
            GM_ST(uint32_t, ((uint8_t *)&((struct gm_fr_inferable_param_f80lrdi *)((&F.h)->up))->l11), t55);
        }
        goto ix47;
ie47:;
ix47:;
        goto me7;
mn44:;
ma10:;
        goto me7;
mn56:;
ma11:;
        gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 579, 13);
me7:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_tqlm0ij(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0) {
    struct gm_fr_fn_tqlm0ij F;
    gm_t88rcucg1b7uk gm_l0; /* x */
    GM_STACK_CHECK(&F, 141u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t3 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_effect_args_name_jgklot1 *)((&F.h)->up))->l0));
        void * t4 = (void *)(((uint8_t *)t3) + 8);
        gm_t88rcucg1b7uk t5 = gm_l0;
        gm_t53at80pfdus3 t6;
        gm_f_kind_jcnt4hm((void *)&t6, t4, GM_CONV(gm_csguankko4lmhd, t5));
        gm_t53at80pfdus3 t7 = t6;
        uint32_t t8 = GM_LD(uint32_t, ((uint8_t *)&((struct gm_fr_effect_args_name_jgklot1 *)((&F.h)->up))->l2));
        uint8_t t9 = (((GM_LD(uint8_t, ((uint8_t *)&t7) + 0) == ((uint8_t)19ULL)) && (GM_LD(uint32_t, (((uint8_t *)&t7) + 8)) == t8)));
        if (!t9) goto ie2;
        {
            GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_effect_args_name_jgklot1 *)((&F.h)->up))->l3), ((uint8_t)1));
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_uualn6s(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0) {
    struct gm_fr_fn_uualn6s F;
    gm_t88rcucg1b7uk gm_l0; /* x */
    uint32_t gm_l1; /* q */
    GM_STACK_CHECK(&F, 280u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t3 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_bind_literal_fallbacks_r2021qp *)((&F.h)->up))->l0));
        void * t4 = GM_LD(void *, (((uint8_t *)t3) + 0));
        void * t5 = (void *)(((uint8_t *)t4) + 8);
        gm_t88rcucg1b7uk t6 = gm_l0;
        gm_t53at80pfdus3 t7;
        gm_f_kind_jcnt4hm((void *)&t7, t5, GM_CONV(gm_csguankko4lmhd, t6));
        gm_t53at80pfdus3 t8 = t7;
        uint8_t t9 = GM_LD(uint8_t, (((uint8_t *)&t8) + 0));
        if (t9 != ((uint8_t)19ULL)) goto ie2;
        uint32_t t10 = GM_LD(uint32_t, (((uint8_t *)&t8) + 8));
        gm_l1 = t10;
        uint32_t t11 = gm_l1;
        void * t12 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_bind_literal_fallbacks_r2021qp *)((&F.h)->up))->l1));
        gm_b16_8 t13 = GM_LD(gm_b16_8, (((uint8_t *)t12) + 80));
        gm_b16_8 t14 = t13;
        uint8_t t15 = 0;
        uint32_t t16 = t11;
        for (uintptr_t i = 0; i < (uintptr_t)(GM_LD(uintptr_t, ((uint8_t *)&t14) + 8)); i++) if (gm_eq467d5cm1d5(((uint8_t *)GM_LD(void *, ((uint8_t *)&t14))) + i * 4, ((uint8_t *)&t16))) { t15 = 1; break; }
        uint8_t t17 = (uint8_t)(t15);
        if (!t17) goto ie2;
        void * t18 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_bind_literal_fallbacks_r2021qp *)((&F.h)->up))->l2));
        gm_b16_8 t19 = GM_LD(gm_b16_8, (((uint8_t *)t18) + 0));
        gm_b16_8 t20 = t19;
        gm_b16_8 t21 = GM_LD(gm_b16_8, ((uint8_t *)&t20));
        uint32_t t22 = gm_l1;
        gm_tkn42u7na3t81 t23 = gm_f_bound_to_5c11t0h(t21, t22);
        gm_tkn42u7na3t81 t24 = t23;
        uint8_t t25;
        if (((((uint8_t *)&t24))[4] != 0)) goto mn27;
        t25 = ((uint8_t)1);
        goto me26;
mn27:;
        t25 = ((uint8_t)0);
        goto me26;
mn28:;
        gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 289, 76);
me26:;
        if (!t25) goto ie2;
        {
            GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_bind_literal_fallbacks_r2021qp *)((&F.h)->up))->l11), ((uint8_t)1));
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
__attribute__((aligned(4))) void * gm_f_generic_call_2tlofdi(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, void * a2, gm_b16_8 a3, gm_tl874t2atnlh3 a4) {
    struct gm_fr_generic_call_2tlofdi F;
    void * gm_l0; /* cx */
    uint32_t gm_l1; /* f */
    void * gm_l2; /* receiver */
    gm_b16_8 gm_l3; /* args */
    gm_tl874t2atnlh3 gm_l4; /* span */
    gm_b16_8 gm_l6; /* written */
    uint8_t gm_l7; /* rest */
    gm_b16_8 gm_l8; /* <tmp> */
    uint64_t gm_l9; /* <tmp> */
    gm_tf5srh4mp4b3i gm_l11; /* recv */
    gm_b16_8 gm_l12; /* <tmp> */
    uint64_t gm_l13; /* <tmp> */
    uint64_t gm_l14; /* i */
    gm_b16_8 gm_l17; /* <tmp> */
    uint64_t gm_l18; /* <tmp> */
    gm_t08olq97rscn3 gm_l20; /* n */
    gm_b16_8 gm_l22; /* <tmp> */
    uint64_t gm_l23; /* <tmp> */
    uint64_t gm_l24; /* i */
    gm_b16_8 gm_l27; /* a */
    gm_tl874t2atnlh3 gm_l28; /* sp */
    gm_b16_8 gm_l29; /* msg */
    gm_b16_8 gm_l30; /* <tmp> */
    uint64_t gm_l31; /* <tmp> */
    void * gm_l34; /* r */
    void * gm_l35; /* x */
    gm_b16_8 gm_l36; /* spread */
    void * gm_l37; /* call */
    GM_STACK_CHECK(&F, 2969u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    {
        uint32_t t2 = gm_l1;
        uint64_t t3 = (uint64_t)t2;
        uint64_t t4 = (uintptr_t)t3;
        void * t5 = gm_l0;
        void * t6 = GM_LD(void *, (((uint8_t *)t5) + 0));
        uint64_t t7 = GM_LD(uintptr_t, ((((uint8_t *)t6) + 344) + 0) + 8);
        if (t4 >= (uintptr_t)(t7)) gm_trap_index(t4, t7, gm_srcrorgjlg51brd, 27, 12);
        gm_tqqhoivahirr0 t8 = GM_LD(gm_tqqhoivahirr0, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t6) + 344) + 0)) + t4 * 136));
        F.l5 = t8;
    }
    {
        gm_b16_8 t9 = gm_l3;
        gm_l6 = t9;
    }
    {
        gm_l7 = ((uint8_t)0);
    }
    {
        {
            gm_b16_8 t11 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 40));
            gm_l8 = t11;
        }
        {
            gm_l9 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t13 = gm_l9;
                uint64_t t14 = GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8);
                uint8_t t15 = (t13 < t14);
                if (!t15) goto lb12;
                {
                    uint64_t t18 = gm_l9;
                    uint64_t t19 = (uintptr_t)t18;
                    uint64_t t20 = GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8);
                    gm_t5v0c8ia6a5mo t23 = GM_LD(gm_t5v0c8ia6a5mo, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l8)) + t19 * 56));
                    F.l10 = t23;
                    {
                        uint8_t t26 = GM_LD(uint8_t, (((uint8_t *)&F.l10) + 48));
                        if (!t26) goto ie25;
                        {
                            gm_l7 = ((uint8_t)1);
                        }
                        goto ix25;
ie25:;
ix25:;
                    }
                    goto me21;
mn22:;
                    gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 31, 5);
me21:;
                }
lc12:;
                {
                    uint64_t t29 = gm_l9;
                    uint64_t r30; bool o31 = __builtin_add_overflow(t29, ((uint64_t)1ULL), &r30);
                    uint64_t t32 = r30;
                    gm_l9 = t32;
                }
            }
lb12:;
        }
    }
    {
        uint8_t t34 = gm_l7;
        if (!t34) goto ie33;
        {
            gm_tf5srh4mp4b3i t36;
            memset(&t36, 0, sizeof t36);
            gm_l11 = t36;
        }
        {
            void * t38 = gm_l2;
            void * t39 = t38;
            uint8_t t40;
            if ((GM_LD(void *, ((uint8_t *)&t39)) != 0)) goto mn42;
            t40 = ((uint8_t)0);
            goto me41;
mn42:;
            t40 = ((uint8_t)1);
            goto me41;
mn43:;
            gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 36, 12);
me41:;
            if (!t40) goto ie37;
            {
                {
                    gm_b16_8 t46 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 40));
                    gm_b16_8 t47 = t46;
                    gm_b16_8 t48 = GM_LD(gm_b16_8, ((uint8_t *)&t47));
                    gm_l12 = t48;
                }
                {
                    gm_l13 = ((uint64_t)0ULL);
                }
                {
                    for (;;) {
                        uint64_t t50 = gm_l13;
                        uint64_t t51 = GM_LD(uintptr_t, ((uint8_t *)&gm_l12) + 8);
                        uint8_t t52 = (t50 < t51);
                        if (!t52) goto lb49;
                        {
                            gm_tgsvmddj3e85e t55;
                            uint64_t t56 = gm_l13;
                            GM_ST(uint64_t, (uint8_t *)&t55 + 0, t56);
                            uint64_t t57 = gm_l13;
                            uint64_t t58 = (uintptr_t)t57;
                            uint64_t t59 = GM_LD(uintptr_t, ((uint8_t *)&gm_l12) + 8);
                            memcpy((uint8_t *)&t55 + 8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l12)) + t58 * 56), 56);
                            gm_tgsvmddj3e85e t60 = t55;
                            uint64_t t63 = GM_LD(uint64_t, (((uint8_t *)&t60) + 0));
                            gm_l14 = t63;
                            gm_t5v0c8ia6a5mo t64 = GM_LD(gm_t5v0c8ia6a5mo, (((uint8_t *)&t60) + 8));
                            F.l15 = t64;
                            {
                                uint8_t t67;
                                if ((((((uint8_t *)&F.l15) + 40))[4] != 0)) goto mn69;
                                t67 = ((uint8_t)1);
                                goto me68;
mn69:;
                                t67 = ((uint8_t)0);
                                goto me68;
mn70:;
                                gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 38, 20);
me68:;
                                if (!t67) goto ie66;
                                {
                                    uint64_t t72 = gm_l14;
                                    gm_tf5srh4mp4b3i t73;
                                    GM_ST(uint8_t, (uint8_t *)&t73 + 9, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t73 + 10, 0);
                                    GM_ST(uint32_t, (uint8_t *)&t73 + 12, 0);
                                    GM_ST(uint64_t, ((uint8_t *)&t73), t72);
                                    ((uint8_t *)&t73)[8] = 1;
                                    gm_l11 = t73;
                                }
                                {
                                    goto lb49;
                                }
                                goto ix66;
ie66:;
ix66:;
                            }
                            goto me61;
mn62:;
                            gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 37, 13);
me61:;
                        }
lc49:;
                        {
                            uint64_t t75 = gm_l13;
                            uint64_t r76; bool o77 = __builtin_add_overflow(t75, ((uint64_t)1ULL), &r76);
                            uint64_t t78 = r76;
                            gm_l13 = t78;
                        }
                    }
lb49:;
                }
            }
            goto ix37;
ie37:;
ix37:;
        }
        {
            gm_f_list_epvducg((void *)&F.l16, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            {
                gm_b16_8 t80 = gm_l3;
                gm_l17 = t80;
            }
            {
                gm_l18 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t82 = gm_l18;
                    uint64_t t83 = GM_LD(uintptr_t, ((uint8_t *)&gm_l17) + 8);
                    uint8_t t84 = (t82 < t83);
                    if (!t84) goto lb81;
                    {
                        uint64_t t87 = gm_l18;
                        uint64_t t88 = (uintptr_t)t87;
                        uint64_t t89 = GM_LD(uintptr_t, ((uint8_t *)&gm_l17) + 8);
                        gm_tpff59ptr3s92 t92 = GM_LD(gm_tpff59ptr3s92, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l17)) + t88 * 192));
                        F.l19 = t92;
                        {
                            uint8_t t95 = GM_LD(uint8_t, ((((uint8_t *)&F.l19) + 0) + 0));
                            if (t95 != ((uint8_t)1ULL)) goto ie94;
                            gm_t08olq97rscn3 t96 = GM_LD(gm_t08olq97rscn3, (((((uint8_t *)&F.l19) + 0) + 8) + 0));
                            gm_l20 = t96;
                            {
                                void * t98 = (void *)((uint8_t *)&F.l16);
                                gm_b16_8 t99 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l20) + 0));
                                gm_tftb1odc739t5 t100 = gm_f_push_3ef746b(t98, t99);
                                gm_tftb1odc739t5 t101 = t100;
                                if (((((uint8_t *)&t101))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t101) + 1, gm_srcrorgjlg51brd, 46, 41);
                                gm_b0_1 t102 = GM_LD(gm_b0_1, (((uint8_t *)&t101) + 1));
                            }
                            goto ix94;
ie94:;
ix94:;
                        }
                        goto me90;
mn91:;
                        gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 45, 9);
me90:;
                    }
lc81:;
                    {
                        uint64_t t104 = gm_l18;
                        uint64_t r105; bool o106 = __builtin_add_overflow(t104, ((uint64_t)1ULL), &r105);
                        uint64_t t107 = r105;
                        gm_l18 = t107;
                    }
                }
lb81:;
            }
        }
        {
            gm_f_list_s83jn2q((void *)&F.l21, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            {
                gm_b16_8 t109 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 40));
                gm_b16_8 t110 = t109;
                gm_b16_8 t111 = GM_LD(gm_b16_8, ((uint8_t *)&t110));
                gm_l22 = t111;
            }
            {
                gm_l23 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t113 = gm_l23;
                    uint64_t t114 = GM_LD(uintptr_t, ((uint8_t *)&gm_l22) + 8);
                    uint8_t t115 = (t113 < t114);
                    if (!t115) goto lb112;
                    {
                        gm_tgsvmddj3e85e t118;
                        uint64_t t119 = gm_l23;
                        GM_ST(uint64_t, (uint8_t *)&t118 + 0, t119);
                        uint64_t t120 = gm_l23;
                        uint64_t t121 = (uintptr_t)t120;
                        uint64_t t122 = GM_LD(uintptr_t, ((uint8_t *)&gm_l22) + 8);
                        memcpy((uint8_t *)&t118 + 8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l22)) + t121 * 56), 56);
                        gm_tgsvmddj3e85e t123 = t118;
                        uint64_t t126 = GM_LD(uint64_t, (((uint8_t *)&t123) + 0));
                        gm_l24 = t126;
                        gm_t5v0c8ia6a5mo t127 = GM_LD(gm_t5v0c8ia6a5mo, (((uint8_t *)&t123) + 8));
                        F.l25 = t127;
                        {
                            void * t129 = (void *)((uint8_t *)&F.l21);
                            uint8_t t130 = 0;
                            uint8_t t131 = 0;
                            uint8_t t132 = 0;
                            gm_tf5srh4mp4b3i t133 = gm_l11;
                            uint64_t t134 = gm_l24;
                            gm_tf5srh4mp4b3i t135;
                            GM_ST(uint8_t, (uint8_t *)&t135 + 9, 0);
                            GM_ST(uint16_t, (uint8_t *)&t135 + 10, 0);
                            GM_ST(uint32_t, (uint8_t *)&t135 + 12, 0);
                            GM_ST(uint64_t, ((uint8_t *)&t135), t134);
                            ((uint8_t *)&t135)[8] = 1;
                            gm_tf5srh4mp4b3i t136 = t133;
                            gm_tf5srh4mp4b3i t137 = t135;
                            uint8_t t138 = (!gm_eq7mri15hk78(((uint8_t *)&t136), ((uint8_t *)&t137)));
                            if (t138) {
                                void * t139 = gm_l0;
                                void * t140 = GM_LD(void *, (((uint8_t *)t139) + 0));
                                void * t141 = (void *)((uint8_t *)&F.l5);
                                uint64_t t142 = gm_l24;
                                uint8_t t143 = gm_f_inferable_param_f80lrdi(t140, t141, t142);
                                uint8_t t144 = (uint8_t)!t143;
                                t132 = t144;
                            } else t132 = 0;
                            if (t132) {
                                void * t145 = (void *)((uint8_t *)&F.l25);
                                uint8_t t146 = gm_f_is_site_8sr7vam(t145);
                                uint8_t t147 = (uint8_t)!t146;
                                t131 = t147;
                            } else t131 = 0;
                            if (t131) {
                                gm_b16_8 t148 = GM_LD(gm_b16_8, (((uint8_t *)&F.l25) + 0));
                                gm_b16_8 t149 = GM_LD(gm_b16_8, (((uint8_t *)&F.l16) + 0));
                                gm_b16_8 t150 = t149;
                                uint8_t t151 = 0;
                                gm_b16_8 t152 = t148;
                                for (uintptr_t i = 0; i < (uintptr_t)(GM_LD(uintptr_t, ((uint8_t *)&t150) + 8)); i++) if (gm_eq0iqiubfm7e(((uint8_t *)GM_LD(void *, ((uint8_t *)&t150))) + i * 16, ((uint8_t *)&t152))) { t151 = 1; break; }
                                uint8_t t153 = (uint8_t)(!t151);
                                t130 = t153;
                            } else t130 = 0;
                            gm_tftb1odc739t5 t154 = gm_f_push_vd1290p(t129, t130);
                            gm_tftb1odc739t5 t155 = t154;
                            if (((((uint8_t *)&t155))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t155) + 1, gm_srcrorgjlg51brd, 49, 45);
                            gm_b0_1 t156 = GM_LD(gm_b0_1, (((uint8_t *)&t155) + 1));
                        }
                        goto me124;
mn125:;
                        gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 49, 9);
me124:;
                    }
lc112:;
                    {
                        uint64_t t158 = gm_l23;
                        uint64_t r159; bool o160 = __builtin_add_overflow(t158, ((uint64_t)1ULL), &r159);
                        uint64_t t161 = r159;
                        gm_l23 = t161;
                    }
                }
lb112:;
            }
        }
        {
            gm_b16_8 t163 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 40));
            gm_b16_8 t164 = t163;
            gm_b16_8 t165 = GM_LD(gm_b16_8, ((uint8_t *)&t164));
            gm_b16_8 t166 = GM_LD(gm_b16_8, (((uint8_t *)&F.l21) + 0));
            gm_b16_8 t167 = t166;
            gm_b16_8 t168 = GM_LD(gm_b16_8, ((uint8_t *)&t167));
            gm_b16_8 t169 = gm_l3;
            gm_tl874t2atnlh3 t170 = gm_l4;
            gm_tck5nas0nf5v8 t171;
            gm_f_pack_rest_args_cknfrfl((void *)&t171, (void *)NULL, cx, NULL, t165, t168, t169, t170);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tck5nas0nf5v8 t172 = t171;
            if (!((((uint8_t *)&t172))[40] != 0)) goto ie162;
            gm_ts5t31j0bugq1 t173 = GM_LD(gm_ts5t31j0bugq1, ((uint8_t *)&t172));
            F.l26 = t173;
            {
                uint8_t t177 = GM_LD(uint8_t, (((uint8_t *)&F.l26) + 0));
                if (t177 != ((uint8_t)0ULL)) goto mn176;
                gm_b16_8 t178 = GM_LD(gm_b16_8, (((uint8_t *)&F.l26) + 8));
                gm_l27 = t178;
                {
                    gm_b16_8 t180 = gm_l27;
                    gm_b16_8 t181 = t180;
                    gm_b16_8 t182 = GM_LD(gm_b16_8, ((uint8_t *)&t181));
                    gm_l6 = t182;
                }
                goto me175;
mn176:;
                uint8_t t184 = GM_LD(uint8_t, (((uint8_t *)&F.l26) + 0));
                if (t184 != ((uint8_t)1ULL)) goto mn183;
                gm_tl874t2atnlh3 t185 = GM_LD(gm_tl874t2atnlh3, ((((uint8_t *)&F.l26) + 8) + 0));
                gm_l28 = t185;
                gm_b16_8 t186 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l26) + 8) + 16));
                gm_l29 = t186;
                {
                    void * t188 = gm_l0;
                    gm_tl874t2atnlh3 t189 = gm_l28;
                    gm_b16_8 t190 = gm_l29;
                    gm_b0_1 t191 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t188, t189, t190);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                {
                    void * t192 = gm_l0;
                    void * t193 = GM_LD(void *, (((uint8_t *)t192) + 0));
                    gm_tl874t2atnlh3 t194 = gm_l4;
                    void * t195 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t193, t194);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t196 = 0;
                    F.ret = t195;
                    goto gm_fret;
                }
                goto me175;
mn183:;
                gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 51, 13);
me175:;
            }
            goto ix162;
ie162:;
ix162:;
        }
        goto ix33;
ie33:;
ix33:;
    }
    {
        {
            gm_b16_8 t198 = gm_l6;
            gm_l30 = t198;
        }
        {
            gm_l31 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t200 = gm_l31;
                uint64_t t201 = GM_LD(uintptr_t, ((uint8_t *)&gm_l30) + 8);
                uint8_t t202 = (t200 < t201);
                if (!t202) goto lb199;
                {
                    uint64_t t205 = gm_l31;
                    uint64_t t206 = (uintptr_t)t205;
                    uint64_t t207 = GM_LD(uintptr_t, ((uint8_t *)&gm_l30) + 8);
                    gm_tpff59ptr3s92 t210 = GM_LD(gm_tpff59ptr3s92, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l30)) + t206 * 192));
                    F.l32 = t210;
                    {
                        uint8_t t213;
                        uint8_t t216 = GM_LD(uint8_t, ((((uint8_t *)&F.l32) + 0) + 0));
                        if (t216 != ((uint8_t)2ULL)) goto mn215;
                        t213 = ((uint8_t)1);
                        goto me214;
mn215:;
                        t213 = ((uint8_t)0);
                        goto me214;
mn217:;
                        gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 61, 12);
me214:;
                        if (!t213) goto ie212;
                        {
                            gm_f_list_ir0h3tc_2((void *)&F.l33, (void *)NULL, cx, NULL);
                            if (gm_unw.kind) {
                                goto gm_ll203;
                            }
                        }
                        {
                            gm_l34 = ((void *)0);
                        }
                        {
                            void * t220 = gm_l2;
                            void * t221 = t220;
                            if (!(GM_LD(void *, ((uint8_t *)&t221)) != 0)) goto ie219;
                            void * t222 = GM_LD(void *, ((uint8_t *)&t221));
                            gm_l35 = t222;
                            {
                                void * t224 = gm_l0;
                                void * t225 = gm_l35;
                                void * t226 = (void *)((uint8_t *)&F.l33);
                                void * t227 = gm_f_bind_temp_sa5rcbr((void *)NULL, cx, NULL, t224, t225, t226);
                                if (gm_unw.kind) {
                                    goto gm_ll203;
                                }
                                gm_l34 = t227;
                            }
                            goto ix219;
ie219:;
ix219:;
                        }
                        {
                            void * t228 = gm_l0;
                            gm_b16_8 t229 = gm_l6;
                            void * t230 = (void *)((uint8_t *)&F.l33);
                            gm_th2lqbo77pvsc t231;
                            gm_f_spread_generic_args_8qtof2f((void *)&t231, (void *)NULL, cx, NULL, t228, t229, t230);
                            if (gm_unw.kind) {
                                goto gm_ll203;
                            }
                            gm_th2lqbo77pvsc t232 = t231;
                            gm_b16_8 t233;
                            if (((((uint8_t *)&t232))[16] != 0)) {
                                gm_b16_8 t234 = GM_LD(gm_b16_8, (((uint8_t *)&t232) + 0));
                                t233 = t234;
                            } else {
                                void * t235 = gm_l0;
                                void * t236 = GM_LD(void *, (((uint8_t *)t235) + 0));
                                gm_tl874t2atnlh3 t237 = gm_l4;
                                void * t238 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t236, t237);
                                if (gm_unw.kind) {
                                    goto gm_ll203;
                                }
                                uint8_t t239 = 0;
                                F.ret = t238;
                                goto gm_fret;
                            }
                            gm_l36 = t233;
                        }
                        {
                            void * t240 = gm_l0;
                            uint32_t t241 = gm_l1;
                            void * t242 = gm_l34;
                            gm_b16_8 t243 = gm_l36;
                            gm_b16_8 t244 = t243;
                            gm_b16_8 t245 = GM_LD(gm_b16_8, ((uint8_t *)&t244));
                            gm_tl874t2atnlh3 t246 = gm_l4;
                            void * t247 = gm_f_generic_call_packed_h7ssi9n((void *)NULL, cx, NULL, t240, t241, t242, t245, t246);
                            if (gm_unw.kind) {
                                goto gm_ll203;
                            }
                            gm_l37 = t247;
                        }
                        {
                            void * t248 = gm_l37;
                            gm_t88rcucg1b7uk t249 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t248) + 0));
                            gm_b16_8 t250 = GM_LD(gm_b16_8, (((uint8_t *)&F.l33) + 0));
                            void * t251 = gm_l37;
                            gm_tl874t2atnlh3 t252 = gm_l4;
                            void * t253 = gm_f_block_typed_e7svg7n((void *)NULL, cx, NULL, t249, t250, t251, t252);
                            if (gm_unw.kind) {
                                goto gm_ll203;
                            }
                            uint8_t t254 = 0;
                            F.ret = t253;
                            goto gm_fret;
                        }
                        goto ix212;
ie212:;
ix212:;
                    }
                    goto me208;
mn209:;
                    gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 60, 5);
me208:;
                }
lc199:;
                {
                    uint64_t t256 = gm_l31;
                    uint64_t r257; bool o258 = __builtin_add_overflow(t256, ((uint64_t)1ULL), &r257);
                    uint64_t t259 = r257;
                    gm_l31 = t259;
                }
            }
gm_ll203:;
            if (gm_unw.frame == &F.h && gm_unw.label == 4) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb199; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc199; }
            }
            goto gm_lad;
lb199:;
        }
    }
    {
        void * t260 = gm_l0;
        uint32_t t261 = gm_l1;
        void * t262 = gm_l2;
        gm_b16_8 t263 = gm_l6;
        gm_tl874t2atnlh3 t264 = gm_l4;
        void * t265 = gm_f_generic_call_packed_h7ssi9n((void *)NULL, cx, NULL, t260, t261, t262, t263, t264);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t266 = 0;
        F.ret = t265;
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
__attribute__((aligned(4))) void * gm_f_generic_call_packed_h7ssi9n(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, void * a2, gm_b16_8 a3, gm_tl874t2atnlh3 a4) {
    struct gm_fr_generic_call_packed_h7ssi9n F;
    uint32_t gm_l1; /* f */
    void * gm_l2; /* receiver */
    gm_b16_8 gm_l3; /* args */
    gm_tl874t2atnlh3 gm_l4; /* span */
    void * gm_l6; /* tys */
    gm_tf5srh4mp4b3i gm_l7; /* recv_index */
    gm_b16_8 gm_l8; /* <tmp> */
    uint64_t gm_l9; /* <tmp> */
    uint64_t gm_l10; /* i */
    gm_b16_8 gm_l14; /* <tmp> */
    uint64_t gm_l15; /* <tmp> */
    gm_t08olq97rscn3 gm_l17; /* n */
    uint8_t gm_l19; /* inst_only */
    gm_b16_8 gm_l21; /* <tmp> */
    uint64_t gm_l22; /* <tmp> */
    uint64_t gm_l23; /* i */
    uint8_t gm_l25; /* explicit */
    gm_b16_8 gm_l26; /* <tmp> */
    uint64_t gm_l27; /* <tmp> */
    uint64_t gm_l28; /* i */
    uint8_t gm_l32; /* conflict */
    gm_b16_8 gm_l34; /* <tmp> */
    uint64_t gm_l35; /* <tmp> */
    gm_b16_8 gm_l37; /* <tmp> */
    uint64_t gm_l38; /* <tmp> */
    uint64_t gm_l39; /* k */
    gm_b16_8 gm_l41; /* <tmp> */
    uint64_t gm_l42; /* <tmp> */
    gm_t08olq97rscn3 gm_l44; /* nm */
    uint64_t gm_l46; /* i */
    gm_b16_8 gm_l48; /* <tmp> */
    uint64_t gm_l49; /* <tmp> */
    uint64_t gm_l52; /* <tmp> */
    uint64_t gm_l53; /* <tmp> */
    uint64_t gm_l54; /* k */
    void * gm_l55; /* src */
    uint32_t gm_l56; /* g */
    uint64_t gm_l57; /* i */
    void * gm_l58; /* r */
    gm_b16_8 gm_l60; /* <tmp> */
    uint64_t gm_l61; /* <tmp> */
    gm_tplpbqf3duvn3 gm_l62; /* b */
    uint64_t gm_l63; /* <tmp> */
    uint64_t gm_l64; /* <tmp> */
    uint64_t gm_l65; /* k */
    uint64_t gm_l66; /* i */
    void * gm_l67; /* e */
    uint32_t gm_l69; /* pid */
    gm_t88rcucg1b7uk gm_l70; /* v */
    gm_t88rcucg1b7uk gm_l71; /* expect */
    uint8_t gm_l72; /* partial */
    gm_b16_8 gm_l74; /* <tmp> */
    uint64_t gm_l75; /* <tmp> */
    uint64_t gm_l76; /* j */
    void * gm_l81; /* v */
    gm_t88rcucg1b7uk gm_l83; /* t */
    gm_b16_8 gm_l85; /* <tmp> */
    uint64_t gm_l86; /* <tmp> */
    gm_tplpbqf3duvn3 gm_l87; /* b */
    gm_t88rcucg1b7uk gm_l88; /* prev */
    gm_b16_8 gm_l89; /* targs */
    gm_b16_8 gm_l91; /* <tmp> */
    uint64_t gm_l92; /* <tmp> */
    uint64_t gm_l93; /* k */
    uint32_t gm_l94; /* g */
    uint64_t gm_l96; /* i */
    gm_b16_8 gm_l97; /* <tmp> */
    uint64_t gm_l98; /* <tmp> */
    void * gm_l101; /* out */
    gm_tnpifv20irnph gm_l103; /* effects */
    gm_b16_8 gm_l104; /* pre */
    uint32_t gm_l105; /* inst */
    void * gm_l106; /* call */
    GM_STACK_CHECK(&F, 11302u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    {
        uint32_t t2 = gm_l1;
        uint64_t t3 = (uint64_t)t2;
        uint64_t t4 = (uintptr_t)t3;
        void * t5 = F.l0;
        void * t6 = GM_LD(void *, (((uint8_t *)t5) + 0));
        uint64_t t7 = GM_LD(uintptr_t, ((((uint8_t *)t6) + 344) + 0) + 8);
        if (t4 >= (uintptr_t)(t7)) gm_trap_index(t4, t7, gm_srcrorgjlg51brd, 77, 12);
        gm_tqqhoivahirr0 t8 = GM_LD(gm_tqqhoivahirr0, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t6) + 344) + 0)) + t4 * 136));
        F.l5 = t8;
    }
    {
        void * t9 = F.l0;
        void * t10 = GM_LD(void *, (((uint8_t *)t9) + 0));
        void * t11 = (void *)(((uint8_t *)t10) + 8);
        gm_l6 = t11;
    }
    {
        gm_tf5srh4mp4b3i t12;
        memset(&t12, 0, sizeof t12);
        gm_l7 = t12;
    }
    {
        void * t14 = gm_l2;
        void * t15 = t14;
        uint8_t t16;
        if ((GM_LD(void *, ((uint8_t *)&t15)) != 0)) goto mn18;
        t16 = ((uint8_t)0);
        goto me17;
mn18:;
        t16 = ((uint8_t)1);
        goto me17;
mn19:;
        gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 80, 8);
me17:;
        if (!t16) goto ie13;
        {
            {
                gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 40));
                gm_b16_8 t23 = t22;
                gm_b16_8 t24 = GM_LD(gm_b16_8, ((uint8_t *)&t23));
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
                        gm_tgsvmddj3e85e t31;
                        uint64_t t32 = gm_l9;
                        GM_ST(uint64_t, (uint8_t *)&t31 + 0, t32);
                        uint64_t t33 = gm_l9;
                        uint64_t t34 = (uintptr_t)t33;
                        uint64_t t35 = GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8);
                        memcpy((uint8_t *)&t31 + 8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l8)) + t34 * 56), 56);
                        gm_tgsvmddj3e85e t36 = t31;
                        uint64_t t39 = GM_LD(uint64_t, (((uint8_t *)&t36) + 0));
                        gm_l10 = t39;
                        gm_t5v0c8ia6a5mo t40 = GM_LD(gm_t5v0c8ia6a5mo, (((uint8_t *)&t36) + 8));
                        F.l11 = t40;
                        {
                            uint8_t t43;
                            if ((((((uint8_t *)&F.l11) + 40))[4] != 0)) goto mn45;
                            t43 = ((uint8_t)1);
                            goto me44;
mn45:;
                            t43 = ((uint8_t)0);
                            goto me44;
mn46:;
                            gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 82, 16);
me44:;
                            if (!t43) goto ie42;
                            {
                                uint64_t t48 = gm_l10;
                                gm_tf5srh4mp4b3i t49;
                                GM_ST(uint8_t, (uint8_t *)&t49 + 9, 0);
                                GM_ST(uint16_t, (uint8_t *)&t49 + 10, 0);
                                GM_ST(uint32_t, (uint8_t *)&t49 + 12, 0);
                                GM_ST(uint64_t, ((uint8_t *)&t49), t48);
                                ((uint8_t *)&t49)[8] = 1;
                                gm_l7 = t49;
                            }
                            {
                                goto lb25;
                            }
                            goto ix42;
ie42:;
ix42:;
                        }
                        goto me37;
mn38:;
                        gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 81, 9);
me37:;
                    }
lc25:;
                    {
                        uint64_t t51 = gm_l9;
                        uint64_t r52; bool o53 = __builtin_add_overflow(t51, ((uint64_t)1ULL), &r52);
                        uint64_t t54 = r52;
                        gm_l9 = t54;
                    }
                }
lb25:;
            }
        }
        goto ix13;
ie13:;
ix13:;
    }
    {
        gm_f_list_epvducg((void *)&F.l12, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_f_list_vp1djv1((void *)&F.l13, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t56 = gm_l3;
            gm_l14 = t56;
        }
        {
            gm_l15 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t58 = gm_l15;
                uint64_t t59 = GM_LD(uintptr_t, ((uint8_t *)&gm_l14) + 8);
                uint8_t t60 = (t58 < t59);
                if (!t60) goto lb57;
                {
                    uint64_t t63 = gm_l15;
                    uint64_t t64 = (uintptr_t)t63;
                    uint64_t t65 = GM_LD(uintptr_t, ((uint8_t *)&gm_l14) + 8);
                    gm_tpff59ptr3s92 t68 = GM_LD(gm_tpff59ptr3s92, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l14)) + t64 * 192));
                    F.l16 = t68;
                    {
                        uint8_t t75 = GM_LD(uint8_t, ((((uint8_t *)&F.l16) + 0) + 0));
                        switch (t75) { case ((uint8_t)0ULL): goto ma72; case ((uint8_t)1ULL): goto ma71; case ((uint8_t)2ULL): goto ma73; default: goto ma74; }
ma71:;
                        uint8_t t77 = GM_LD(uint8_t, ((((uint8_t *)&F.l16) + 0) + 0));
                        if (t77 != ((uint8_t)1ULL)) goto mn76;
                        gm_t08olq97rscn3 t78 = GM_LD(gm_t08olq97rscn3, (((((uint8_t *)&F.l16) + 0) + 8) + 0));
                        gm_l17 = t78;
                        void * t79 = (void *)((uint8_t *)&F.l12);
                        gm_b16_8 t80 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l17) + 0));
                        gm_tftb1odc739t5 t81 = gm_f_push_3ef746b(t79, t80);
                        gm_tftb1odc739t5 t82 = t81;
                        if (((((uint8_t *)&t82))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t82) + 1, gm_srcrorgjlg51brd, 92, 29);
                        gm_b0_1 t83 = GM_LD(gm_b0_1, (((uint8_t *)&t82) + 1));
                        goto me70;
mn76:;
ma72:;
                        uint8_t t85 = GM_LD(uint8_t, ((((uint8_t *)&F.l16) + 0) + 0));
                        if (t85 != ((uint8_t)0ULL)) goto mn84;
                        gm_tfbvnc90lci8r t86 = GM_LD(gm_tfbvnc90lci8r, ((((uint8_t *)&F.l16) + 0) + 8));
                        F.l18 = t86;
                        void * t87 = (void *)((uint8_t *)&F.l13);
                        gm_tftb1odc739t5 t88 = gm_f_push_d943vgp(t87, &(*(gm_tfbvnc90lci8r *)((uint8_t *)&F.l18)));
                        gm_tftb1odc739t5 t89 = t88;
                        if (((((uint8_t *)&t89))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t89) + 1, gm_srcrorgjlg51brd, 93, 31);
                        gm_b0_1 t90 = GM_LD(gm_b0_1, (((uint8_t *)&t89) + 1));
                        goto me70;
mn84:;
ma73:;
                        uint8_t t92 = GM_LD(uint8_t, ((((uint8_t *)&F.l16) + 0) + 0));
                        if (t92 != ((uint8_t)2ULL)) goto mn91;
                        goto me70;
mn91:;
ma74:;
                        gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 91, 9);
me70:;
                    }
                    goto me66;
mn67:;
                    gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 90, 5);
me66:;
                }
lc57:;
                {
                    uint64_t t95 = gm_l15;
                    uint64_t r96; bool o97 = __builtin_add_overflow(t95, ((uint64_t)1ULL), &r96);
                    uint64_t t98 = r96;
                    gm_l15 = t98;
                }
            }
lb57:;
        }
    }
    {
        uint8_t t99 = 0;
        void * t100 = gm_l2;
        void * t101 = t100;
        uint8_t t102;
        if ((GM_LD(void *, ((uint8_t *)&t101)) != 0)) goto mn104;
        t102 = ((uint8_t)1);
        goto me103;
mn104:;
        t102 = ((uint8_t)0);
        goto me103;
mn105:;
        gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 98, 18);
me103:;
        if (t102) {
            void * t106 = F.l0;
            void * t107 = (void *)((uint8_t *)&F.l5);
            gm_b16_8 t108 = gm_l3;
            gm_b16_8 t109 = GM_LD(gm_b16_8, (((uint8_t *)&F.l13) + 0));
            gm_b16_8 t110 = t109;
            gm_b16_8 t111 = GM_LD(gm_b16_8, ((uint8_t *)&t110));
            uint8_t t112 = gm_f_is_instantiation_vgsqc8f((void *)NULL, cx, NULL, t106, t107, t108, t111);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            t99 = t112;
        } else t99 = 0;
        gm_l19 = t99;
    }
    {
        gm_f_list_5dhgg4i((void *)&F.l20, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t114 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 40));
            gm_b16_8 t115 = t114;
            gm_b16_8 t116 = GM_LD(gm_b16_8, ((uint8_t *)&t115));
            gm_l21 = t116;
        }
        {
            gm_l22 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t118 = gm_l22;
                uint64_t t119 = GM_LD(uintptr_t, ((uint8_t *)&gm_l21) + 8);
                uint8_t t120 = (t118 < t119);
                if (!t120) goto lb117;
                {
                    gm_tgsvmddj3e85e t123;
                    uint64_t t124 = gm_l22;
                    GM_ST(uint64_t, (uint8_t *)&t123 + 0, t124);
                    uint64_t t125 = gm_l22;
                    uint64_t t126 = (uintptr_t)t125;
                    uint64_t t127 = GM_LD(uintptr_t, ((uint8_t *)&gm_l21) + 8);
                    memcpy((uint8_t *)&t123 + 8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l21)) + t126 * 56), 56);
                    gm_tgsvmddj3e85e t128 = t123;
                    uint64_t t131 = GM_LD(uint64_t, (((uint8_t *)&t128) + 0));
                    gm_l23 = t131;
                    gm_t5v0c8ia6a5mo t132 = GM_LD(gm_t5v0c8ia6a5mo, (((uint8_t *)&t128) + 8));
                    F.l24 = t132;
                    {
                        uint8_t t135 = 0;
                        uint8_t t136 = 0;
                        uint8_t t137 = 0;
                        void * t138 = F.l0;
                        void * t139 = GM_LD(void *, (((uint8_t *)t138) + 0));
                        void * t140 = (void *)((uint8_t *)&F.l5);
                        uint64_t t141 = gm_l23;
                        uint8_t t142 = gm_f_inferable_param_f80lrdi(t139, t140, t141);
                        if (!t142) {
                            gm_b16_8 t143 = GM_LD(gm_b16_8, (((uint8_t *)&F.l24) + 0));
                            gm_b16_8 t144 = GM_LD(gm_b16_8, (((uint8_t *)&F.l12) + 0));
                            gm_b16_8 t145 = t144;
                            uint8_t t146 = 0;
                            gm_b16_8 t147 = t143;
                            for (uintptr_t i = 0; i < (uintptr_t)(GM_LD(uintptr_t, ((uint8_t *)&t145) + 8)); i++) if (gm_eq0iqiubfm7e(((uint8_t *)GM_LD(void *, ((uint8_t *)&t145))) + i * 16, ((uint8_t *)&t147))) { t146 = 1; break; }
                            uint8_t t148 = (uint8_t)(t146);
                            t137 = t148;
                        } else t137 = 1;
                        if (!t137) {
                            gm_tf5srh4mp4b3i t149 = gm_l7;
                            uint64_t t150 = gm_l23;
                            gm_tf5srh4mp4b3i t151;
                            GM_ST(uint8_t, (uint8_t *)&t151 + 9, 0);
                            GM_ST(uint16_t, (uint8_t *)&t151 + 10, 0);
                            GM_ST(uint32_t, (uint8_t *)&t151 + 12, 0);
                            GM_ST(uint64_t, ((uint8_t *)&t151), t150);
                            ((uint8_t *)&t151)[8] = 1;
                            gm_tf5srh4mp4b3i t152 = t149;
                            gm_tf5srh4mp4b3i t153 = t151;
                            uint8_t t154 = (gm_eq7mri15hk78(((uint8_t *)&t152), ((uint8_t *)&t153)));
                            t136 = t154;
                        } else t136 = 1;
                        if (!t136) {
                            void * t155 = (void *)((uint8_t *)&F.l24);
                            uint8_t t156 = gm_f_is_site_8sr7vam(t155);
                            t135 = t156;
                        } else t135 = 1;
                        if (!t135) goto ie134;
                        {
                            goto lc117;
                        }
                        goto ix134;
ie134:;
ix134:;
                    }
                    {
                        void * t158 = (void *)((uint8_t *)&F.l20);
                        uint64_t t159 = gm_l23;
                        gm_tftb1odc739t5 t160 = gm_f_push_ucgmlo2(t158, t159);
                        gm_tftb1odc739t5 t161 = t160;
                        if (((((uint8_t *)&t161))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t161) + 1, gm_srcrorgjlg51brd, 105, 9);
                        gm_b0_1 t162 = GM_LD(gm_b0_1, (((uint8_t *)&t161) + 1));
                    }
                    goto me129;
mn130:;
                    gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 103, 5);
me129:;
                }
lc117:;
                {
                    uint64_t t164 = gm_l22;
                    uint64_t r165; bool o166 = __builtin_add_overflow(t164, ((uint64_t)1ULL), &r165);
                    uint64_t t167 = r165;
                    gm_l22 = t167;
                }
            }
lb117:;
        }
    }
    {
        uint64_t t169 = GM_LD(uintptr_t, (((uint8_t *)&F.l13) + 0) + 8);
        uint64_t t170 = GM_LD(uintptr_t, (((uint8_t *)&F.l20) + 0) + 8);
        uint8_t t171 = (t169 > t170);
        if (!t171) goto ie168;
        {
            void * t173 = F.l0;
            void * t174 = (void *)((uint8_t *)&F.l5);
            void * t175 = gm_l2;
            void * t176 = t175;
            uint8_t t177;
            if ((GM_LD(void *, ((uint8_t *)&t176)) != 0)) goto mn179;
            t177 = ((uint8_t)0);
            goto me178;
mn179:;
            t177 = ((uint8_t)1);
            goto me178;
mn180:;
            gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 108, 40);
me178:;
            gm_b16_8 t181 = GM_LD(gm_b16_8, (((uint8_t *)&F.l12) + 0));
            gm_b16_8 t182 = t181;
            gm_b16_8 t183 = GM_LD(gm_b16_8, ((uint8_t *)&t182));
            gm_b16_8 t184 = GM_LD(gm_b16_8, (((uint8_t *)&F.l13) + 0));
            gm_b16_8 t185 = t184;
            gm_b16_8 t186 = GM_LD(gm_b16_8, ((uint8_t *)&t185));
            uint64_t t187 = GM_LD(uintptr_t, (((uint8_t *)&F.l20) + 0) + 8);
            gm_tl874t2atnlh3 t188 = gm_l4;
            gm_b0_1 t189 = gm_f_too_many_generic_args_bs99n7s((void *)NULL, cx, NULL, t173, t174, t177, t183, t186, t187, t188);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t190 = F.l0;
            void * t191 = GM_LD(void *, (((uint8_t *)t190) + 0));
            gm_tl874t2atnlh3 t192 = gm_l4;
            void * t193 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t191, t192);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t194 = 0;
            F.ret = t193;
            goto gm_fret;
        }
        goto ix168;
ie168:;
ix168:;
    }
    {
        gm_l25 = ((uint8_t)0);
    }
    {
        {
            gm_b16_8 t196 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 40));
            gm_b16_8 t197 = t196;
            gm_b16_8 t198 = GM_LD(gm_b16_8, ((uint8_t *)&t197));
            gm_l26 = t198;
        }
        {
            gm_l27 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t200 = gm_l27;
                uint64_t t201 = GM_LD(uintptr_t, ((uint8_t *)&gm_l26) + 8);
                uint8_t t202 = (t200 < t201);
                if (!t202) goto lb199;
                {
                    gm_tgsvmddj3e85e t205;
                    uint64_t t206 = gm_l27;
                    GM_ST(uint64_t, (uint8_t *)&t205 + 0, t206);
                    uint64_t t207 = gm_l27;
                    uint64_t t208 = (uintptr_t)t207;
                    uint64_t t209 = GM_LD(uintptr_t, ((uint8_t *)&gm_l26) + 8);
                    memcpy((uint8_t *)&t205 + 8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l26)) + t208 * 56), 56);
                    gm_tgsvmddj3e85e t210 = t205;
                    uint64_t t213 = GM_LD(uint64_t, (((uint8_t *)&t210) + 0));
                    gm_l28 = t213;
                    gm_t5v0c8ia6a5mo t214 = GM_LD(gm_t5v0c8ia6a5mo, (((uint8_t *)&t210) + 8));
                    F.l29 = t214;
                    {
                        void * t217 = F.l0;
                        void * t218 = GM_LD(void *, (((uint8_t *)t217) + 0));
                        void * t219 = (void *)((uint8_t *)&F.l5);
                        uint64_t t220 = gm_l28;
                        uint8_t t221 = gm_f_inferable_param_f80lrdi(t218, t219, t220);
                        if (!t221) goto ie216;
                        gm_b16_8 t222 = GM_LD(gm_b16_8, (((uint8_t *)&F.l29) + 0));
                        gm_b16_8 t223 = GM_LD(gm_b16_8, (((uint8_t *)&F.l12) + 0));
                        gm_b16_8 t224 = t223;
                        uint8_t t225 = 0;
                        gm_b16_8 t226 = t222;
                        for (uintptr_t i = 0; i < (uintptr_t)(GM_LD(uintptr_t, ((uint8_t *)&t224) + 8)); i++) if (gm_eq0iqiubfm7e(((uint8_t *)GM_LD(void *, ((uint8_t *)&t224))) + i * 16, ((uint8_t *)&t226))) { t225 = 1; break; }
                        uint8_t t227 = (uint8_t)(t225);
                        if (!t227) goto ie216;
                        {
                            gm_l25 = ((uint8_t)1);
                        }
                        goto ix216;
ie216:;
ix216:;
                    }
                    goto me211;
mn212:;
                    gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 114, 5);
me211:;
                }
lc199:;
                {
                    uint64_t t230 = gm_l27;
                    uint64_t r231; bool o232 = __builtin_add_overflow(t230, ((uint64_t)1ULL), &r231);
                    uint64_t t233 = r231;
                    gm_l27 = t233;
                }
            }
lb199:;
        }
    }
    {
        gm_f_list_oalq03d((void *)&F.l30, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_f_list_89e90o6((void *)&F.l31, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_l32 = ((uint8_t)0);
    }
    {
        gm_f_list_pmp06kq((void *)&F.l33, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t235 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 40));
            gm_l34 = t235;
        }
        {
            gm_l35 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t237 = gm_l35;
                uint64_t t238 = GM_LD(uintptr_t, ((uint8_t *)&gm_l34) + 8);
                uint8_t t239 = (t237 < t238);
                if (!t239) goto lb236;
                {
                    uint64_t t242 = gm_l35;
                    uint64_t t243 = (uintptr_t)t242;
                    uint64_t t244 = GM_LD(uintptr_t, ((uint8_t *)&gm_l34) + 8);
                    {
                        void * t248 = (void *)((uint8_t *)&F.l33);
                        gm_tftb1odc739t5 t249 = gm_f_push_r2k6rjf(t248, ((void *)0));
                        gm_tftb1odc739t5 t250 = t249;
                        if (((((uint8_t *)&t250))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t250) + 1, gm_srcrorgjlg51brd, 122, 27);
                        gm_b0_1 t251 = GM_LD(gm_b0_1, (((uint8_t *)&t250) + 1));
                    }
                    goto me245;
mn246:;
                    gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 122, 5);
me245:;
                }
lc236:;
                {
                    uint64_t t253 = gm_l35;
                    uint64_t r254; bool o255 = __builtin_add_overflow(t253, ((uint64_t)1ULL), &r254);
                    uint64_t t256 = r254;
                    gm_l35 = t256;
                }
            }
lb236:;
        }
    }
    {
        gm_f_list_lnmdod1((void *)&F.l36, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t258 = GM_LD(gm_b16_8, (((uint8_t *)&F.l13) + 0));
            gm_b16_8 t259 = t258;
            gm_b16_8 t260 = GM_LD(gm_b16_8, ((uint8_t *)&t259));
            gm_l37 = t260;
        }
        {
            gm_l38 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t262 = gm_l38;
                uint64_t t263 = GM_LD(uintptr_t, ((uint8_t *)&gm_l37) + 8);
                uint8_t t264 = (t262 < t263);
                if (!t264) goto lb261;
                {
                    gm_t8ip9lbbvm8be t267;
                    uint64_t t268 = gm_l38;
                    GM_ST(uint64_t, (uint8_t *)&t267 + 0, t268);
                    uint64_t t269 = gm_l38;
                    uint64_t t270 = (uintptr_t)t269;
                    uint64_t t271 = GM_LD(uintptr_t, ((uint8_t *)&gm_l37) + 8);
                    memcpy((uint8_t *)&t267 + 8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l37)) + t270 * 136), 136);
                    gm_t8ip9lbbvm8be t272 = t267;
                    uint64_t t275 = GM_LD(uint64_t, (((uint8_t *)&t272) + 0));
                    gm_l39 = t275;
                    gm_tfbvnc90lci8r t276 = GM_LD(gm_tfbvnc90lci8r, (((uint8_t *)&t272) + 8));
                    F.l40 = t276;
                    {
                        void * t278 = (void *)((uint8_t *)&F.l36);
                        gm_t5retglboi70s t279;
                        memset((uint8_t *)&t279 + 145, 0, 7);
                        uint64_t t280 = gm_l39;
                        uint64_t t281 = (uintptr_t)t280;
                        uint64_t t282 = GM_LD(uintptr_t, (((uint8_t *)&F.l20) + 0) + 8);
                        if (t281 >= (uintptr_t)(t282)) gm_trap_index(t281, t282, gm_srcrorgjlg51brd, 124, 77);
                        uint64_t t283 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l20) + 0)) + t281 * 8));
                        GM_ST(uint64_t, (uint8_t *)&t279 + 0, t283);
                        memcpy((uint8_t *)&t279 + 8, ((uint8_t *)&F.l40), 136);
                        GM_ST(uint8_t, (uint8_t *)&t279 + 144, ((uint8_t)0ULL));
                        gm_tftb1odc739t5 t284 = gm_f_push_0ee93e4(t278, &t279);
                        gm_tftb1odc739t5 t285 = t284;
                        if (((((uint8_t *)&t285))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t285) + 1, gm_srcrorgjlg51brd, 124, 47);
                        gm_b0_1 t286 = GM_LD(gm_b0_1, (((uint8_t *)&t285) + 1));
                    }
                    goto me273;
mn274:;
                    gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 124, 5);
me273:;
                }
lc261:;
                {
                    uint64_t t288 = gm_l38;
                    uint64_t r289; bool o290 = __builtin_add_overflow(t288, ((uint64_t)1ULL), &r289);
                    uint64_t t291 = r289;
                    gm_l38 = t291;
                }
            }
lb261:;
        }
    }
    {
        {
            gm_b16_8 t293 = gm_l3;
            gm_l41 = t293;
        }
        {
            gm_l42 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t295 = gm_l42;
                uint64_t t296 = GM_LD(uintptr_t, ((uint8_t *)&gm_l41) + 8);
                uint8_t t297 = (t295 < t296);
                if (!t297) goto lb294;
                {
                    uint64_t t300 = gm_l42;
                    uint64_t t301 = (uintptr_t)t300;
                    uint64_t t302 = GM_LD(uintptr_t, ((uint8_t *)&gm_l41) + 8);
                    gm_tpff59ptr3s92 t305 = GM_LD(gm_tpff59ptr3s92, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l41)) + t301 * 192));
                    F.l43 = t305;
                    {
                        uint8_t t308 = GM_LD(uint8_t, ((((uint8_t *)&F.l43) + 0) + 0));
                        if (t308 != ((uint8_t)1ULL)) goto ie307;
                        gm_t08olq97rscn3 t309 = GM_LD(gm_t08olq97rscn3, (((((uint8_t *)&F.l43) + 0) + 8) + 0));
                        gm_l44 = t309;
                        gm_tfbvnc90lci8r t310 = GM_LD(gm_tfbvnc90lci8r, (((((uint8_t *)&F.l43) + 0) + 8) + 32));
                        F.l45 = t310;
                        {
                            gm_b16_8 t312 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 40));
                            gm_b16_8 t313 = t312;
                            gm_b16_8 t314 = GM_LD(gm_b16_8, ((uint8_t *)&t313));
                            gm_b16_8 t315 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l44) + 0));
                            gm_tf5srh4mp4b3i t316 = gm_f_param_index_78k4iut(t314, t315);
                            gm_tf5srh4mp4b3i t317 = t316;
                            uint64_t t318;
                            if (((((uint8_t *)&t317))[8] != 0)) {
                                uint64_t t319 = GM_LD(uint64_t, (((uint8_t *)&t317) + 0));
                                t318 = t319;
                            } else {
                                {
                                    void * t321 = F.l0;
                                    gm_tl874t2atnlh3 t322 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l44) + 16));
                                    gm_b16_8 t323 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l44) + 0));
                                    gm_tnpifv20irnph t324;
                                    GM_ST(gm_b16_8, ((uint8_t *)&t324 + 0), t323);
                                    gm_tabb710d9mq2e t325;
                                    gm_f_format_itam0lj((void *)&t325, (void *)NULL, cx, NULL, t324);
                                    if (gm_unw.kind) {
                                        goto gm_ll298;
                                    }
                                    gm_tabb710d9mq2e t326 = t325;
                                    if (((((uint8_t *)&t326))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t326) + 8, gm_srcrorgjlg51brd, 128, 31);
                                    gm_b16_8 t327 = GM_LD(gm_b16_8, (((uint8_t *)&t326) + 8));
                                    gm_b16_8 t328 = t327;
                                    gm_b16_8 t329 = GM_LD(gm_b16_8, ((uint8_t *)&t328));
                                    gm_b0_1 t330 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t321, t322, t329);
                                    if (gm_unw.kind) {
                                        goto gm_ll298;
                                    }
                                }
                                goto lc294;
                            }
                            gm_l46 = t318;
                        }
                        {
                            void * t331 = (void *)((uint8_t *)&F.l36);
                            gm_t5retglboi70s t332;
                            memset((uint8_t *)&t332 + 145, 0, 7);
                            uint64_t t333 = gm_l46;
                            GM_ST(uint64_t, (uint8_t *)&t332 + 0, t333);
                            memcpy((uint8_t *)&t332 + 8, ((uint8_t *)&F.l45), 136);
                            GM_ST(uint8_t, (uint8_t *)&t332 + 144, ((uint8_t)0ULL));
                            gm_tftb1odc739t5 t334 = gm_f_push_0ee93e4(t331, &t332);
                            gm_tftb1odc739t5 t335 = t334;
                            if (((((uint8_t *)&t335))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t335) + 1, gm_srcrorgjlg51brd, 131, 9);
                            gm_b0_1 t336 = GM_LD(gm_b0_1, (((uint8_t *)&t335) + 1));
                        }
                        goto ix307;
ie307:;
                        {
                            goto lc294;
                        }
ix307:;
                    }
                    goto me303;
mn304:;
                    gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 125, 5);
me303:;
                }
lc294:;
                {
                    uint64_t t339 = gm_l42;
                    uint64_t r340; bool o341 = __builtin_add_overflow(t339, ((uint64_t)1ULL), &r340);
                    uint64_t t342 = r340;
                    gm_l42 = t342;
                }
            }
gm_ll298:;
            if (gm_unw.frame == &F.h && gm_unw.label == 6) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb294; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc294; }
            }
            goto gm_lad;
lb294:;
        }
    }
    {
        gm_f_list_5dhgg4i((void *)&F.l47, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t344 = GM_LD(gm_b16_8, (((uint8_t *)&F.l36) + 0));
            gm_l48 = t344;
        }
        {
            gm_l49 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t346 = gm_l49;
                uint64_t t347 = GM_LD(uintptr_t, ((uint8_t *)&gm_l48) + 8);
                uint8_t t348 = (t346 < t347);
                if (!t348) goto lb345;
                {
                    uint64_t t351 = gm_l49;
                    uint64_t t352 = (uintptr_t)t351;
                    uint64_t t353 = GM_LD(uintptr_t, ((uint8_t *)&gm_l48) + 8);
                    gm_t5retglboi70s t356 = GM_LD(gm_t5retglboi70s, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l48)) + t352 * 152));
                    F.l50 = t356;
                    {
                        void * t358 = (void *)((uint8_t *)&F.l47);
                        uint64_t t359 = GM_LD(uint64_t, (((uint8_t *)&F.l50) + 0));
                        gm_tftb1odc739t5 t360 = gm_f_push_ucgmlo2(t358, t359);
                        gm_tftb1odc739t5 t361 = t360;
                        if (((((uint8_t *)&t361))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t361) + 1, gm_srcrorgjlg51brd, 134, 32);
                        gm_b0_1 t362 = GM_LD(gm_b0_1, (((uint8_t *)&t361) + 1));
                    }
                    goto me354;
mn355:;
                    gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 134, 5);
me354:;
                }
lc345:;
                {
                    uint64_t t364 = gm_l49;
                    uint64_t r365; bool o366 = __builtin_add_overflow(t364, ((uint64_t)1ULL), &r365);
                    uint64_t t367 = r365;
                    gm_l49 = t367;
                }
            }
lb345:;
        }
    }
    {
        gm_f_list_p2dh8s2((void *)&F.l51, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_l53 = ((uint64_t)0ULL);
        }
        {
            uint64_t t369 = GM_LD(uintptr_t, (((uint8_t *)&F.l36) + 0) + 8);
            gm_l52 = t369;
        }
        {
            for (;;) {
                uint64_t t371 = gm_l53;
                uint64_t t372 = gm_l52;
                uint8_t t373 = (t371 < t372);
                if (!t373) goto lb370;
                {
                    uint64_t t376 = gm_l53;
                    uint64_t t377 = t376;
                    uint64_t t380 = GM_LD(uint64_t, ((uint8_t *)&t377));
                    gm_l54 = t380;
                    {
                        uint64_t t382 = gm_l54;
                        uint64_t t383 = (uintptr_t)t382;
                        uint64_t t384 = GM_LD(uintptr_t, (((uint8_t *)&F.l36) + 0) + 8);
                        if (t383 >= (uintptr_t)(t384)) gm_trap_index(t383, t384, gm_srcrorgjlg51brd, 138, 17);
                        void * t385 = (void *)((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l36) + 0)) + t383 * 152);
                        gm_l55 = t385;
                    }
                    {
                        void * t386 = gm_l55;
                        uint8_t *t387 = (((uint8_t *)t386) + 144);
                        uint8_t t388;
                        void * t390 = gm_l55;
                        uint64_t t391 = GM_LD(uint64_t, (((uint8_t *)t390) + 0));
                        uint64_t t392 = (uintptr_t)t391;
                        uint64_t t393 = GM_LD(uintptr_t, (((uint8_t *)&F.l5) + 40) + 8);
                        if (t392 >= (uintptr_t)(t393)) gm_trap_index(t392, t393, gm_srcrorgjlg51brd, 139, 28);
                        if (!(((((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l5) + 40)) + t392 * 56) + 40))[4] != 0)) goto ie389;
                        uint32_t t394 = GM_LD(uint32_t, (((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l5) + 40)) + t392 * 56) + 40));
                        gm_l56 = t394;
                        uint8_t t396;
                        void * t398 = F.l0;
                        void * t399 = GM_LD(void *, (((uint8_t *)t398) + 0));
                        uint32_t t400 = gm_l56;
                        gm_tnnjeerolos0a t401 = gm_f_param_kind_a0aikmb(t399, t400);
                        gm_tnnjeerolos0a t402 = t401;
                        uint8_t t403;
                        uint8_t t406 = GM_LD(uint8_t, (((uint8_t *)&t402) + 0));
                        if (t406 != ((uint8_t)2ULL)) goto mn405;
                        t403 = ((uint8_t)1);
                        goto me404;
mn405:;
                        t403 = ((uint8_t)0);
                        goto me404;
mn407:;
                        gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 140, 16);
me404:;
                        if (!t403) goto ie397;
                        uint8_t t409 = ((uint8_t)3ULL);
                        t396 = t409;
                        goto ix397;
ie397:;
                        uint8_t t411 = ((uint8_t)0ULL);
                        t396 = t411;
ix397:;
                        uint8_t t412 = t396;
                        t388 = t412;
                        goto ix389;
ie389:;
                        uint8_t t413;
                        void * t415 = gm_l55;
                        uint8_t t416;
                        uint8_t t419 = GM_LD(uint8_t, (((((uint8_t *)t415) + 8) + 8) + 0));
                        if (t419 != ((uint8_t)36ULL)) goto mn418;
                        t416 = ((uint8_t)1);
                        goto me417;
mn418:;
                        t416 = ((uint8_t)0);
                        goto me417;
mn420:;
                        gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 141, 19);
me417:;
                        if (!t416) goto ie414;
                        uint8_t t422 = ((uint8_t)2ULL);
                        t413 = t422;
                        goto ix414;
ie414:;
                        uint8_t t424 = ((uint8_t)1ULL);
                        t413 = t424;
ix414:;
                        t388 = t413;
ix389:;
                        GM_ST(uint8_t, t387, t388);
                    }
                    goto me378;
mn379:;
                    gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 137, 5);
me378:;
                }
lc370:;
                {
                    uint64_t t426 = gm_l53;
                    uint64_t r427; bool o428 = __builtin_add_overflow(t426, ((uint64_t)1ULL), &r427);
                    uint64_t t429 = r427;
                    gm_l53 = t429;
                }
            }
lb370:;
        }
    }
    {
        gm_b16_8 t430 = GM_LD(gm_b16_8, (((uint8_t *)&F.l36) + 0));
        gm_closure t431 = ((gm_closure){GM_PTR_TAG(gm_f_fn_dink6tf), NULL, cx});
        gm_f_stable_sort_by_3nevrtc(t430, t431);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        if (!((((uint8_t *)&gm_l7))[8] != 0)) goto ie432;
        uint64_t t433 = GM_LD(uint64_t, ((uint8_t *)&gm_l7));
        gm_l57 = t433;
        {
            void * t436 = gm_l2;
            void * t437 = t436;
            if (!(GM_LD(void *, ((uint8_t *)&t437)) != 0)) goto ie435;
            void * t438 = GM_LD(void *, ((uint8_t *)&t437));
            gm_l58 = t438;
            {
                void * t440 = F.l0;
                void * t441 = GM_LD(void *, (((uint8_t *)t440) + 0));
                uint64_t t442 = gm_l57;
                uint64_t t443 = (uintptr_t)t442;
                uint64_t t444 = GM_LD(uintptr_t, (((uint8_t *)&F.l5) + 40) + 8);
                if (t443 >= (uintptr_t)(t444)) gm_trap_index(t443, t444, gm_srcrorgjlg51brd, 151, 31);
                gm_t88rcucg1b7uk t445 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l5) + 40)) + t443 * 56) + 16));
                void * t446 = F.l0;
                void * t447 = (void *)(((uint8_t *)t446) + 96);
                void * t448 = F.l0;
                void * t449 = GM_LD(void *, (((uint8_t *)t448) + 0));
                void * t450 = gm_l58;
                gm_t88rcucg1b7uk t451 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t450) + 0));
                gm_t88rcucg1b7uk t452 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t447, t449, t451);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                void * t453 = (void *)((uint8_t *)&F.l30);
                gm_b0_1 t454 = gm_f_match_params_6vndkud((void *)NULL, cx, NULL, t441, t445, t452, t453);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                uint64_t t455 = gm_l57;
                uint64_t t456 = (uintptr_t)t455;
                uint64_t t457 = GM_LD(uintptr_t, (((uint8_t *)&F.l33) + 0) + 8);
                if (t456 >= (uintptr_t)(t457)) gm_trap_index(t456, t457, gm_srcrorgjlg51brd, 152, 13);
                void * t458 = gm_l58;
                GM_ST(void *, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l33) + 0)) + t456 * 8), t458);
            }
            goto ix435;
ie435:;
ix435:;
        }
        goto ix432;
ie432:;
ix432:;
    }
    {
        gm_f_list_go2qg0b((void *)&F.l59, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t460 = GM_LD(gm_b16_8, (((uint8_t *)&F.l30) + 0));
            gm_l60 = t460;
        }
        {
            gm_l61 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t462 = gm_l61;
                uint64_t t463 = GM_LD(uintptr_t, ((uint8_t *)&gm_l60) + 8);
                uint8_t t464 = (t462 < t463);
                if (!t464) goto lb461;
                {
                    uint64_t t467 = gm_l61;
                    uint64_t t468 = (uintptr_t)t467;
                    uint64_t t469 = GM_LD(uintptr_t, ((uint8_t *)&gm_l60) + 8);
                    gm_tplpbqf3duvn3 t472 = GM_LD(gm_tplpbqf3duvn3, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l60)) + t468 * 8));
                    gm_l62 = t472;
                    {
                        void * t474 = (void *)((uint8_t *)&F.l59);
                        uint32_t t475 = GM_LD(uint32_t, (((uint8_t *)&gm_l62) + 0));
                        gm_tftb1odc739t5 t476 = gm_f_push_im9d4ro(t474, t475);
                        gm_tftb1odc739t5 t477 = t476;
                        if (((((uint8_t *)&t477))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t477) + 1, gm_srcrorgjlg51brd, 157, 28);
                        gm_b0_1 t478 = GM_LD(gm_b0_1, (((uint8_t *)&t477) + 1));
                    }
                    goto me470;
mn471:;
                    gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 157, 5);
me470:;
                }
lc461:;
                {
                    uint64_t t480 = gm_l61;
                    uint64_t r481; bool o482 = __builtin_add_overflow(t480, ((uint64_t)1ULL), &r481);
                    uint64_t t483 = r481;
                    gm_l61 = t483;
                }
            }
lb461:;
        }
    }
    {
        {
            gm_l64 = ((uint64_t)0ULL);
        }
        {
            uint64_t t485 = GM_LD(uintptr_t, (((uint8_t *)&F.l36) + 0) + 8);
            gm_l63 = t485;
        }
        {
            for (;;) {
                uint64_t t487 = gm_l64;
                uint64_t t488 = gm_l63;
                uint8_t t489 = (t487 < t488);
                if (!t489) goto lb486;
                {
                    uint64_t t492 = gm_l64;
                    uint64_t t493 = t492;
                    uint64_t t496 = GM_LD(uint64_t, ((uint8_t *)&t493));
                    gm_l65 = t496;
                    {
                        uint64_t t498 = gm_l65;
                        uint64_t t499 = (uintptr_t)t498;
                        uint64_t t500 = GM_LD(uintptr_t, (((uint8_t *)&F.l36) + 0) + 8);
                        if (t499 >= (uintptr_t)(t500)) gm_trap_index(t499, t500, gm_srcrorgjlg51brd, 159, 14);
                        uint64_t t501 = GM_LD(uint64_t, (((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l36) + 0)) + t499 * 152) + 0));
                        gm_l66 = t501;
                    }
                    {
                        uint64_t t502 = gm_l65;
                        uint64_t t503 = (uintptr_t)t502;
                        uint64_t t504 = GM_LD(uintptr_t, (((uint8_t *)&F.l36) + 0) + 8);
                        if (t503 >= (uintptr_t)(t504)) gm_trap_index(t503, t504, gm_srcrorgjlg51brd, 160, 15);
                        void * t505 = (void *)(((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l36) + 0)) + t503 * 152) + 8);
                        gm_l67 = t505;
                    }
                    {
                        uint64_t t506 = gm_l66;
                        uint64_t t507 = (uintptr_t)t506;
                        uint64_t t508 = GM_LD(uintptr_t, (((uint8_t *)&F.l5) + 40) + 8);
                        if (t507 >= (uintptr_t)(t508)) gm_trap_index(t507, t508, gm_srcrorgjlg51brd, 161, 14);
                        gm_t5v0c8ia6a5mo t509 = GM_LD(gm_t5v0c8ia6a5mo, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l5) + 40)) + t507 * 56));
                        F.l68 = t509;
                    }
                    {
                        if (!(((((uint8_t *)&F.l68) + 40))[4] != 0)) goto ie510;
                        uint32_t t511 = GM_LD(uint32_t, (((uint8_t *)&F.l68) + 40));
                        gm_l69 = t511;
                        {
                            void * t514 = F.l0;
                            void * t515 = GM_LD(void *, (((uint8_t *)t514) + 0));
                            uint32_t t516 = gm_l69;
                            gm_tnnjeerolos0a t517 = gm_f_param_kind_a0aikmb(t515, t516);
                            gm_tnnjeerolos0a t518 = t517;
                            uint8_t t519;
                            uint8_t t522 = GM_LD(uint8_t, (((uint8_t *)&t518) + 0));
                            if (t522 != ((uint8_t)2ULL)) goto mn521;
                            t519 = ((uint8_t)1);
                            goto me520;
mn521:;
                            t519 = ((uint8_t)0);
                            goto me520;
mn523:;
                            gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 163, 16);
me520:;
                            if (!t519) goto ie513;
                            {
                                void * t525 = F.l0;
                                void * t526 = F.l0;
                                void * t527 = GM_LD(void *, (((uint8_t *)t526) + 0));
                                gm_t88rcucg1b7uk t528 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l68) + 16));
                                gm_b16_8 t529 = GM_LD(gm_b16_8, (((uint8_t *)&F.l30) + 0));
                                gm_b16_8 t530 = t529;
                                gm_b16_8 t531 = GM_LD(gm_b16_8, ((uint8_t *)&t530));
                                gm_t88rcucg1b7uk t532 = gm_f_subst_a2s1mb3((void *)NULL, cx, NULL, t527, t528, t531);
                                if (gm_unw.kind) {
                                    goto gm_ll490;
                                }
                                void * t533 = gm_l67;
                                gm_tkn42u7na3t81 t534 = gm_f_comptime_arg_vtq6sop((void *)NULL, cx, NULL, t525, t532, t533);
                                if (gm_unw.kind) {
                                    goto gm_ll490;
                                }
                                gm_tkn42u7na3t81 t535 = t534;
                                gm_t88rcucg1b7uk t536;
                                if (((((uint8_t *)&t535))[4] != 0)) {
                                    gm_t88rcucg1b7uk t537 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t535) + 0));
                                    t536 = t537;
                                } else {
                                    void * t538 = F.l0;
                                    void * t539 = GM_LD(void *, (((uint8_t *)t538) + 0));
                                    gm_tl874t2atnlh3 t540 = gm_l4;
                                    void * t541 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t539, t540);
                                    if (gm_unw.kind) {
                                        goto gm_ll490;
                                    }
                                    uint8_t t542 = 0;
                                    F.ret = t541;
                                    goto gm_fret;
                                }
                                gm_l70 = t536;
                            }
                            {
                                void * t543 = (void *)((uint8_t *)&F.l30);
                                uint32_t t544 = gm_l69;
                                gm_t88rcucg1b7uk t545 = gm_l70;
                                gm_b0_1 t546 = gm_f_set_bind_nsjnlvq((void *)NULL, cx, NULL, t543, t544, t545);
                                if (gm_unw.kind) {
                                    goto gm_ll490;
                                }
                            }
                            goto ix513;
ie513:;
                            void * t548 = F.l0;
                            void * t549 = GM_LD(void *, (((uint8_t *)t548) + 0));
                            uint32_t t550 = gm_l69;
                            gm_tnnjeerolos0a t551 = gm_f_param_kind_a0aikmb(t549, t550);
                            gm_tnnjeerolos0a t552 = t551;
                            uint8_t t553 = ((GM_LD(uint8_t, ((uint8_t *)&t552) + 0) == ((uint8_t)1ULL)));
                            if (!t553) goto ie547;
                            {
                                void * t555 = (void *)((uint8_t *)&F.l30);
                                uint32_t t556 = gm_l69;
                                void * t557 = F.l0;
                                void * t558 = GM_LD(void *, (((uint8_t *)t557) + 0));
                                void * t559 = F.l0;
                                uint64_t t560 = GM_LD(uint64_t, (((uint8_t *)t559) + 8));
                                void * t561 = gm_l67;
                                gm_t88rcucg1b7uk t562 = gm_f_resolve_effect_arg_4h6q4qv((void *)NULL, cx, NULL, t558, t560, t561);
                                if (gm_unw.kind) {
                                    goto gm_ll490;
                                }
                                gm_b0_1 t563 = gm_f_set_bind_nsjnlvq((void *)NULL, cx, NULL, t555, t556, t562);
                                if (gm_unw.kind) {
                                    goto gm_ll490;
                                }
                            }
                            goto ix547;
ie547:;
                            {
                                void * t565 = (void *)((uint8_t *)&F.l30);
                                uint32_t t566 = gm_l69;
                                void * t567 = F.l0;
                                void * t568 = gm_l67;
                                gm_t88rcucg1b7uk t569 = gm_f_resolve_ty_uk6c1lp((void *)NULL, cx, NULL, t567, t568);
                                if (gm_unw.kind) {
                                    goto gm_ll490;
                                }
                                gm_b0_1 t570 = gm_f_set_bind_nsjnlvq((void *)NULL, cx, NULL, t565, t566, t569);
                                if (gm_unw.kind) {
                                    goto gm_ll490;
                                }
                            }
ix547:;
ix513:;
                        }
                        {
                            goto lc486;
                        }
                        goto ix510;
ie510:;
ix510:;
                    }
                    {
                        void * t571 = F.l0;
                        void * t572 = GM_LD(void *, (((uint8_t *)t571) + 0));
                        gm_t88rcucg1b7uk t573 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l68) + 16));
                        gm_b16_8 t574 = GM_LD(gm_b16_8, (((uint8_t *)&F.l30) + 0));
                        gm_b16_8 t575 = t574;
                        gm_b16_8 t576 = GM_LD(gm_b16_8, ((uint8_t *)&t575));
                        gm_t88rcucg1b7uk t577 = gm_f_subst_a2s1mb3((void *)NULL, cx, NULL, t572, t573, t576);
                        if (gm_unw.kind) {
                            goto gm_ll490;
                        }
                        gm_l71 = t577;
                    }
                    {
                        void * t578 = gm_l6;
                        gm_t88rcucg1b7uk t579 = gm_l71;
                        gm_t53at80pfdus3 t580;
                        gm_f_kind_jcnt4hm((void *)&t580, t578, GM_CONV(gm_csguankko4lmhd, t579));
                        gm_t53at80pfdus3 t581 = t580;
                        uint8_t t582;
                        uint8_t t588 = GM_LD(uint8_t, (((uint8_t *)&t581) + 0));
                        switch (t588) { case ((uint8_t)0ULL): goto ma586; case ((uint8_t)1ULL): goto ma586; case ((uint8_t)2ULL): goto ma586; case ((uint8_t)3ULL): goto ma586; case ((uint8_t)4ULL): goto ma586; case ((uint8_t)5ULL): goto ma586; case ((uint8_t)6ULL): goto ma586; case ((uint8_t)7ULL): goto ma586; case ((uint8_t)8ULL): goto ma586; case ((uint8_t)9ULL): goto ma586; case ((uint8_t)10ULL): goto ma586; case ((uint8_t)11ULL): goto ma586; case ((uint8_t)12ULL): goto ma586; case ((uint8_t)13ULL): goto ma586; case ((uint8_t)14ULL): goto ma586; case ((uint8_t)15ULL): goto ma584; case ((uint8_t)16ULL): goto ma585; case ((uint8_t)17ULL): goto ma586; case ((uint8_t)18ULL): goto ma586; case ((uint8_t)19ULL): goto ma586; case ((uint8_t)20ULL): goto ma586; case ((uint8_t)21ULL): goto ma586; case ((uint8_t)22ULL): goto ma586; case ((uint8_t)23ULL): goto ma586; case ((uint8_t)24ULL): goto ma586; case ((uint8_t)25ULL): goto ma586; case ((uint8_t)26ULL): goto ma586; default: goto ma586; }
ma584:;
                        uint8_t t590 = GM_LD(uint8_t, (((uint8_t *)&t581) + 0));
                        if (t590 != ((uint8_t)15ULL)) goto mn589;
                        t582 = ((uint8_t)1);
                        goto me583;
mn589:;
ma585:;
                        uint8_t t592 = GM_LD(uint8_t, (((uint8_t *)&t581) + 0));
                        if (t592 != ((uint8_t)16ULL)) goto mn591;
                        t582 = ((uint8_t)1);
                        goto me583;
mn591:;
ma586:;
                        t582 = ((uint8_t)0);
                        goto me583;
mn593:;
ma587:;
                        gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 176, 20);
me583:;
                        gm_l72 = t582;
                    }
                    {
                        F.l73 = ((uint8_t)0);
                    }
                    {
                        void * t594 = F.l0;
                        void * t595 = GM_LD(void *, (((uint8_t *)t594) + 0));
                        gm_t88rcucg1b7uk t596 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l68) + 16));
                        gm_closure t597 = ((gm_closure){(void *)gm_f_fn_pjcgmfa, (void *)&F.h, cx});
                        gm_f_walk_v858co7(t595, GM_CONV(gm_csguankko4lmhd, t596), t597);
                        if (gm_unw.kind) {
                            goto gm_ll490;
                        }
                    }
                    {
                        void * t599 = gm_l6;
                        gm_t88rcucg1b7uk t600 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l68) + 16));
                        gm_t53at80pfdus3 t601;
                        gm_f_kind_jcnt4hm((void *)&t601, t599, GM_CONV(gm_csguankko4lmhd, t600));
                        gm_t53at80pfdus3 t602 = t601;
                        uint8_t t603 = (!(GM_LD(uint8_t, ((uint8_t *)&t602) + 0) == ((uint8_t)6ULL)));
                        if (!t603) goto ie598;
                        void * t604 = F.l0;
                        void * t605 = gm_l67;
                        uint8_t t606 = gm_f_clearly_value_f7icrt5(t604, t605);
                        uint8_t t607 = (uint8_t)!t606;
                        if (!t607) goto ie598;
                        void * t608 = gm_l67;
                        uint8_t t609;
                        uint8_t t612 = GM_LD(uint8_t, ((((uint8_t *)t608) + 8) + 0));
                        if (t612 != ((uint8_t)10ULL)) goto mn611;
                        t609 = ((uint8_t)0);
                        goto me610;
mn611:;
                        t609 = ((uint8_t)1);
                        goto me610;
mn613:;
                        gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 186, 72);
me610:;
                        if (!t609) goto ie598;
                        void * t614 = gm_l67;
                        uint8_t t615;
                        uint8_t t618 = GM_LD(uint8_t, ((((uint8_t *)t614) + 8) + 0));
                        if (t618 != ((uint8_t)9ULL)) goto mn617;
                        t615 = ((uint8_t)0);
                        goto me616;
mn617:;
                        t615 = ((uint8_t)1);
                        goto me616;
mn619:;
                        gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 186, 100);
me616:;
                        if (!t615) goto ie598;
                        void * t620 = F.l0;
                        void * t621 = gm_l67;
                        uint8_t t622 = gm_f_denotes_type_jgfet92((void *)NULL, cx, NULL, t620, t621);
                        if (gm_unw.kind) {
                            goto gm_ll490;
                        }
                        if (!t622) goto ie598;
                        {
                            {
                                gm_b16_8 t625 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 40));
                                gm_b16_8 t626 = t625;
                                gm_b16_8 t627 = GM_LD(gm_b16_8, ((uint8_t *)&t626));
                                gm_l74 = t627;
                            }
                            {
                                gm_l75 = ((uint64_t)0ULL);
                            }
                            {
                                for (;;) {
                                    uint64_t t629 = gm_l75;
                                    uint64_t t630 = GM_LD(uintptr_t, ((uint8_t *)&gm_l74) + 8);
                                    uint8_t t631 = (t629 < t630);
                                    if (!t631) goto lb628;
                                    {
                                        gm_tgsvmddj3e85e t634;
                                        uint64_t t635 = gm_l75;
                                        GM_ST(uint64_t, (uint8_t *)&t634 + 0, t635);
                                        uint64_t t636 = gm_l75;
                                        uint64_t t637 = (uintptr_t)t636;
                                        uint64_t t638 = GM_LD(uintptr_t, ((uint8_t *)&gm_l74) + 8);
                                        memcpy((uint8_t *)&t634 + 8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l74)) + t637 * 56), 56);
                                        gm_tgsvmddj3e85e t639 = t634;
                                        uint64_t t642 = GM_LD(uint64_t, (((uint8_t *)&t639) + 0));
                                        gm_l76 = t642;
                                        gm_t5v0c8ia6a5mo t643 = GM_LD(gm_t5v0c8ia6a5mo, (((uint8_t *)&t639) + 8));
                                        F.l77 = t643;
                                        {
                                            uint8_t t646;
                                            if ((((((uint8_t *)&F.l77) + 40))[4] != 0)) goto mn648;
                                            t646 = ((uint8_t)0);
                                            goto me647;
mn648:;
                                            t646 = ((uint8_t)1);
                                            goto me647;
mn649:;
                                            gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 188, 20);
me647:;
                                            if (!t646) goto ie645;
                                            void * t650 = F.l0;
                                            void * t651 = GM_LD(void *, (((uint8_t *)t650) + 0));
                                            void * t652 = (void *)((uint8_t *)&F.l5);
                                            uint64_t t653 = gm_l76;
                                            uint8_t t654 = gm_f_inferable_param_f80lrdi(t651, t652, t653);
                                            if (!t654) goto ie645;
                                            gm_b16_8 t655 = GM_LD(gm_b16_8, (((uint8_t *)&F.l77) + 0));
                                            gm_b16_8 t656 = GM_LD(gm_b16_8, (((uint8_t *)&F.l12) + 0));
                                            gm_b16_8 t657 = t656;
                                            uint8_t t658 = 0;
                                            gm_b16_8 t659 = t655;
                                            for (uintptr_t i = 0; i < (uintptr_t)(GM_LD(uintptr_t, ((uint8_t *)&t657) + 8)); i++) if (gm_eq0iqiubfm7e(((uint8_t *)GM_LD(void *, ((uint8_t *)&t657))) + i * 16, ((uint8_t *)&t659))) { t658 = 1; break; }
                                            uint8_t t660 = (uint8_t)(!t658);
                                            if (!t660) goto ie645;
                                            {
                                                void * t662 = F.l0;
                                                void * t663 = gm_l67;
                                                gm_tl874t2atnlh3 t664 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t663) + 120));
                                                gm_tfeab279c16rr t665;
                                                gm_b16_8 t666 = GM_LD(gm_b16_8, (((uint8_t *)&F.l68) + 0));
                                                GM_ST(gm_b16_8, (uint8_t *)&t665 + 0, t666);
                                                gm_b16_8 t667 = GM_LD(gm_b16_8, (((uint8_t *)&F.l77) + 0));
                                                GM_ST(gm_b16_8, (uint8_t *)&t665 + 16, t667);
                                                void * t668 = F.l0;
                                                void * t669 = gm_l67;
                                                gm_tl874t2atnlh3 t670 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t669) + 120));
                                                gm_b16_8 t671 = gm_f_span_text_sv6qv18(t668, GM_CONV(gm_cseahvg8v7p0vs, t670));
                                                GM_ST(gm_b16_8, (uint8_t *)&t665 + 32, t671);
                                                gm_tabb710d9mq2e t672;
                                                gm_f_format_s1gurul((void *)&t672, (void *)NULL, cx, NULL, &t665);
                                                if (gm_unw.kind) {
                                                    goto gm_ll632;
                                                }
                                                gm_tabb710d9mq2e t673 = t672;
                                                if (((((uint8_t *)&t673))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t673) + 8, gm_srcrorgjlg51brd, 189, 38);
                                                gm_b16_8 t674 = GM_LD(gm_b16_8, (((uint8_t *)&t673) + 8));
                                                gm_b16_8 t675 = t674;
                                                gm_b16_8 t676 = GM_LD(gm_b16_8, ((uint8_t *)&t675));
                                                gm_b0_1 t677 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t662, t664, t676);
                                                if (gm_unw.kind) {
                                                    goto gm_ll632;
                                                }
                                            }
                                            {
                                                void * t678 = F.l0;
                                                void * t679 = GM_LD(void *, (((uint8_t *)t678) + 0));
                                                gm_tl874t2atnlh3 t680 = gm_l4;
                                                void * t681 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t679, t680);
                                                if (gm_unw.kind) {
                                                    goto gm_ll632;
                                                }
                                                uint8_t t682 = 0;
                                                F.ret = t681;
                                                goto gm_fret;
                                            }
                                            goto ix645;
ie645:;
ix645:;
                                        }
                                        goto me640;
mn641:;
                                        gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 187, 13);
me640:;
                                    }
lc628:;
                                    {
                                        uint64_t t684 = gm_l75;
                                        uint64_t r685; bool o686 = __builtin_add_overflow(t684, ((uint64_t)1ULL), &r685);
                                        uint64_t t687 = r685;
                                        gm_l75 = t687;
                                    }
                                }
gm_ll632:;
                                if (gm_unw.frame == &F.h && gm_unw.label == 11) {
                                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb628; }
                                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc628; }
                                }
                                goto gm_ll490;
lb628:;
                            }
                        }
                        goto ix598;
ie598:;
ix598:;
                    }
                    {
                        void * t689 = gm_l67;
                        uint8_t t690 = ((GM_LD(uint8_t, (((uint8_t *)t689) + 8) + 0) == ((uint8_t)5ULL)));
                        if (!t690) goto ie688;
                        uint8_t t691 = F.l73;
                        uint8_t t692 = (uint8_t)!t691;
                        if (!t692) goto ie688;
                        {
                            uint64_t t694 = gm_l66;
                            uint64_t t695 = (uintptr_t)t694;
                            uint64_t t696 = GM_LD(uintptr_t, (((uint8_t *)&F.l33) + 0) + 8);
                            if (t695 >= (uintptr_t)(t696)) gm_trap_index(t695, t696, gm_srcrorgjlg51brd, 196, 13);
                            uint8_t *t697 = ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l33) + 0)) + t695 * 8);
                            void * t698 = F.l0;
                            void * t699 = gm_l67;
                            gm_t88rcucg1b7uk t700 = gm_l71;
                            void * t701 = gm_f_arg_value_gavegdu((void *)NULL, cx, NULL, t698, t699, t700);
                            if (gm_unw.kind) {
                                goto gm_ll490;
                            }
                            GM_ST(void *, t697, t701);
                        }
                        {
                            goto lc486;
                        }
                        goto ix688;
ie688:;
ix688:;
                    }
                    {
                        void * t702 = F.l0;
                        void * t703 = gm_l67;
                        gm_t88rcucg1b7uk t704 = gm_l71;
                        gm_f_iterator_arg_r0if28f((void *)&F.l78, (void *)NULL, cx, NULL, t702, t703, t704);
                        if (gm_unw.kind) {
                            goto gm_ll490;
                        }
                    }
                    {
                        void * t706 = F.l0;
                        uint64_t t707 = GM_LD(uintptr_t, ((((uint8_t *)t706) + 1160) + 0) + 8);
                        uint8_t t708 = (t707 > ((uint64_t)0ULL));
                        if (!t708) goto ie705;
                        {
                            void * t710 = (void *)((uint8_t *)&F.l51);
                            gm_tf1bjg81u0glv t711;
                            uint64_t t712 = gm_l66;
                            GM_ST(uint64_t, (uint8_t *)&t711 + 0, t712);
                            void * t713 = F.l0;
                            gm_b16_8 t714 = GM_LD(gm_b16_8, ((((uint8_t *)t713) + 1160) + 0));
                            GM_ST(gm_b16_8, (uint8_t *)&t711 + 8, t714);
                            gm_tftb1odc739t5 t715 = gm_f_push_14olj4s(t710, &t711);
                            gm_tftb1odc739t5 t716 = t715;
                            if (((((uint8_t *)&t716))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t716) + 1, gm_srcrorgjlg51brd, 201, 13);
                            gm_b0_1 t717 = GM_LD(gm_b0_1, (((uint8_t *)&t716) + 1));
                        }
                        {
                            void * t718 = F.l0;
                            uint8_t *t719 = (((uint8_t *)t718) + 1160);
                            gm_f_list_ir0h3tc_2((void *)&(*(gm_trqfvmjtt4sij *)t719), (void *)NULL, cx, NULL);
                            if (gm_unw.kind) {
                                goto gm_ll490;
                            }
                        }
                        goto ix705;
ie705:;
ix705:;
                    }
                    {
                        gm_tfbvnc90lci8r t720;
                        if (!((((uint8_t *)&F.l78))[136] != 0)) goto ie721;
                        gm_tfbvnc90lci8r t722 = GM_LD(gm_tfbvnc90lci8r, ((uint8_t *)&F.l78));
                        F.l79 = t722;
                        gm_tfbvnc90lci8r t724 = F.l79;
                        gm_tfbvnc90lci8r t725 = t724;
                        t720 = t725;
                        goto ix721;
ie721:;
                        void * t727 = gm_l67;
                        gm_tfbvnc90lci8r t728 = GM_LD(gm_tfbvnc90lci8r, ((uint8_t *)t727));
                        gm_tfbvnc90lci8r t729 = t728;
                        t720 = t729;
ix721:;
                        F.l80 = t720;
                    }
                    {
                        void * t730 = F.l0;
                        uint8_t *t731 = (((uint8_t *)t730) + 472);
                        void * t732 = F.l0;
                        void * t733 = GM_LD(void *, (((uint8_t *)t732) + 0));
                        void * t734 = (void *)((uint8_t *)&F.l80);
                        gm_t88rcucg1b7uk t735 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l68) + 16));
                        uint8_t t736 = gm_f_is_const_ref_arg_s0f37he((void *)NULL, cx, NULL, t733, t734, t735);
                        if (gm_unw.kind) {
                            goto gm_ll490;
                        }
                        GM_ST(uint8_t, t731, t736);
                    }
                    {
                        void * t737 = F.l0;
                        void * t738 = (void *)((uint8_t *)&F.l80);
                        gm_tkn42u7na3t81 t739;
                        uint8_t t741 = 0;
                        uint8_t t742 = F.l73;
                        uint8_t t743 = (uint8_t)!t742;
                        if (!t743) {
                            uint8_t t744 = gm_l72;
                            t741 = t744;
                        } else t741 = 1;
                        if (!t741) goto ie740;
                        gm_t88rcucg1b7uk t746 = gm_l71;
                        gm_t88rcucg1b7uk t747 = t746;
                        gm_tkn42u7na3t81 t748;
                        GM_ST(uint8_t, (uint8_t *)&t748 + 5, 0);
                        GM_ST(uint16_t, (uint8_t *)&t748 + 6, 0);
                        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t748), t747);
                        ((uint8_t *)&t748)[4] = 1;
                        t739 = t748;
                        goto ix740;
ie740:;
                        gm_tkn42u7na3t81 t750;
                        memset(&t750, 0, sizeof t750);
                        gm_tkn42u7na3t81 t751 = t750;
                        t739 = t751;
ix740:;
                        void * t752 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t737, t738, t739);
                        if (gm_unw.kind) {
                            goto gm_ll490;
                        }
                        gm_l81 = t752;
                    }
                    {
                        void * t753 = F.l0;
                        GM_ST(uint8_t, (((uint8_t *)t753) + 472), ((uint8_t)0));
                    }
                    {
                        uint8_t t755;
                        if (((((uint8_t *)&F.l78))[136] != 0)) goto mn757;
                        t755 = ((uint8_t)1);
                        goto me756;
mn757:;
                        t755 = ((uint8_t)0);
                        goto me756;
mn758:;
                        gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 208, 12);
me756:;
                        if (!t755) goto ie754;
                        void * t759 = F.l0;
                        void * t760 = (void *)((uint8_t *)&F.l80);
                        void * t761 = gm_l81;
                        gm_t88rcucg1b7uk t762 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l68) + 16));
                        uint8_t t763 = gm_f_iterator_call_as_value_00i5mch((void *)NULL, cx, NULL, t759, t760, t761, t762);
                        if (gm_unw.kind) {
                            goto gm_ll490;
                        }
                        if (!t763) goto ie754;
                        {
                            void * t765 = F.l0;
                            void * t766 = GM_LD(void *, (((uint8_t *)t765) + 0));
                            gm_tl874t2atnlh3 t767 = gm_l4;
                            void * t768 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t766, t767);
                            if (gm_unw.kind) {
                                goto gm_ll490;
                            }
                            uint8_t t769 = 0;
                            F.ret = t768;
                            goto gm_fret;
                        }
                        goto ix754;
ie754:;
ix754:;
                    }
                    {
                        void * t770 = F.l0;
                        void * t771 = gm_l81;
                        gm_t88rcucg1b7uk t772 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l68) + 16));
                        void * t773 = gm_f_array_as_slice_gvflfg2((void *)NULL, cx, NULL, t770, t771, t772);
                        if (gm_unw.kind) {
                            goto gm_ll490;
                        }
                        gm_l81 = t773;
                    }
                    {
                        F.l82 = ((uint8_t)0);
                    }
                    {
                        void * t774 = F.l0;
                        void * t775 = GM_LD(void *, (((uint8_t *)t774) + 0));
                        gm_t88rcucg1b7uk t776 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l68) + 16));
                        gm_closure t777 = ((gm_closure){(void *)gm_f_fn_44t30pu, (void *)&F.h, cx});
                        gm_f_walk_v858co7(t775, GM_CONV(gm_csguankko4lmhd, t776), t777);
                        if (gm_unw.kind) {
                            goto gm_ll490;
                        }
                    }
                    {
                        uint64_t t779 = GM_LD(uintptr_t, (((uint8_t *)&F.l59) + 0) + 8);
                        uint8_t t780 = (t779 > ((uint64_t)0ULL));
                        if (!t780) goto ie778;
                        uint8_t t781 = F.l82;
                        uint8_t t782 = (uint8_t)!t781;
                        if (!t782) goto ie778;
                        void * t783 = gm_l6;
                        gm_t88rcucg1b7uk t784 = gm_l71;
                        uint8_t t785 = gm_f_has_param_u9t0efs(t783, GM_CONV(gm_csguankko4lmhd, t784));
                        uint8_t t786 = (uint8_t)!t785;
                        if (!t786) goto ie778;
                        {
                            uint64_t t788 = gm_l66;
                            uint64_t t789 = (uintptr_t)t788;
                            uint64_t t790 = GM_LD(uintptr_t, (((uint8_t *)&F.l33) + 0) + 8);
                            if (t789 >= (uintptr_t)(t790)) gm_trap_index(t789, t790, gm_srcrorgjlg51brd, 216, 13);
                            void * t791 = gm_l81;
                            GM_ST(void *, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l33) + 0)) + t789 * 8), t791);
                        }
                        {
                            goto lc486;
                        }
                        goto ix778;
ie778:;
ix778:;
                    }
                    {
                        void * t792 = F.l0;
                        void * t793 = (void *)(((uint8_t *)t792) + 96);
                        void * t794 = F.l0;
                        void * t795 = GM_LD(void *, (((uint8_t *)t794) + 0));
                        void * t796 = gm_l81;
                        gm_t88rcucg1b7uk t797 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t796) + 0));
                        gm_t88rcucg1b7uk t798 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t793, t795, t797);
                        if (gm_unw.kind) {
                            goto gm_ll490;
                        }
                        gm_l83 = t798;
                    }
                    {
                        void * t800 = gm_l6;
                        gm_t88rcucg1b7uk t801 = gm_l83;
                        uint8_t t802 = gm_f_has_infer_3o82a0b(t800, GM_CONV(gm_csguankko4lmhd, t801));
                        if (!t802) goto ie799;
                        {
                            void * t804 = (void *)((uint8_t *)&F.l31);
                            gm_thsh8t5u55gps t805;
                            gm_t88rcucg1b7uk t806 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l68) + 16));
                            GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t805 + 0, t806);
                            void * t807 = gm_l81;
                            gm_t88rcucg1b7uk t808 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t807) + 0));
                            GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t805 + 4, t808);
                            void * t809 = gm_l81;
                            gm_tl874t2atnlh3 t810 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t809) + 208));
                            GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t805 + 8, t810);
                            gm_tftb1odc739t5 t811 = gm_f_push_cl1fihl(t804, &t805);
                            gm_tftb1odc739t5 t812 = t811;
                            if (((((uint8_t *)&t812))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t812) + 1, gm_srcrorgjlg51brd, 220, 31);
                            gm_b0_1 t813 = GM_LD(gm_b0_1, (((uint8_t *)&t812) + 1));
                        }
                        goto ix799;
ie799:;
ix799:;
                    }
                    {
                        gm_f_list_oalq03d((void *)&F.l84, (void *)NULL, cx, NULL);
                        if (gm_unw.kind) {
                            goto gm_ll490;
                        }
                    }
                    {
                        void * t814 = F.l0;
                        void * t815 = GM_LD(void *, (((uint8_t *)t814) + 0));
                        gm_t88rcucg1b7uk t816 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l68) + 16));
                        gm_t88rcucg1b7uk t817 = gm_l83;
                        void * t818 = (void *)((uint8_t *)&F.l84);
                        gm_b0_1 t819 = gm_f_match_params_6vndkud((void *)NULL, cx, NULL, t815, t816, t817, t818);
                        if (gm_unw.kind) {
                            goto gm_ll490;
                        }
                    }
                    {
                        {
                            gm_b16_8 t821 = GM_LD(gm_b16_8, (((uint8_t *)&F.l84) + 0));
                            gm_l85 = t821;
                        }
                        {
                            gm_l86 = ((uint64_t)0ULL);
                        }
                        {
                            for (;;) {
                                uint64_t t823 = gm_l86;
                                uint64_t t824 = GM_LD(uintptr_t, ((uint8_t *)&gm_l85) + 8);
                                uint8_t t825 = (t823 < t824);
                                if (!t825) goto lb822;
                                {
                                    uint64_t t828 = gm_l86;
                                    uint64_t t829 = (uintptr_t)t828;
                                    uint64_t t830 = GM_LD(uintptr_t, ((uint8_t *)&gm_l85) + 8);
                                    gm_tplpbqf3duvn3 t833 = GM_LD(gm_tplpbqf3duvn3, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l85)) + t829 * 8));
                                    gm_l87 = t833;
                                    {
                                        gm_b16_8 t835 = GM_LD(gm_b16_8, (((uint8_t *)&F.l30) + 0));
                                        gm_b16_8 t836 = t835;
                                        gm_b16_8 t837 = GM_LD(gm_b16_8, ((uint8_t *)&t836));
                                        uint32_t t838 = GM_LD(uint32_t, (((uint8_t *)&gm_l87) + 0));
                                        gm_tkn42u7na3t81 t839 = gm_f_bound_to_5c11t0h(t837, t838);
                                        gm_tkn42u7na3t81 t840 = t839;
                                        gm_t88rcucg1b7uk t841;
                                        if (((((uint8_t *)&t840))[4] != 0)) {
                                            gm_t88rcucg1b7uk t842 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t840) + 0));
                                            t841 = t842;
                                        } else {
                                            {
                                                void * t844 = (void *)((uint8_t *)&F.l30);
                                                gm_tplpbqf3duvn3 t845 = gm_l87;
                                                gm_tftb1odc739t5 t846 = gm_f_push_h69n5fi(t844, GM_CONV(gm_cs7e9ppsd384bj, t845));
                                                gm_tftb1odc739t5 t847 = t846;
                                                if (((((uint8_t *)&t847))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t847) + 1, gm_srcrorgjlg51brd, 225, 17);
                                                gm_b0_1 t848 = GM_LD(gm_b0_1, (((uint8_t *)&t847) + 1));
                                            }
                                            goto lc822;
                                        }
                                        gm_l88 = t841;
                                    }
                                    {
                                        uint8_t t850 = 0;
                                        uint8_t t851 = 0;
                                        gm_t88rcucg1b7uk t852 = gm_l88;
                                        gm_t88rcucg1b7uk t853 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l87) + 4));
                                        gm_t88rcucg1b7uk t854 = t852;
                                        gm_t88rcucg1b7uk t855 = t853;
                                        uint8_t t856 = (gm_eq9m6mvtmgde(((uint8_t *)&t854), ((uint8_t *)&t855)));
                                        if (!t856) {
                                            uint8_t t857 = gm_l25;
                                            t851 = t857;
                                        } else t851 = 1;
                                        if (!t851) {
                                            void * t858 = F.l0;
                                            void * t859 = GM_LD(void *, (((uint8_t *)t858) + 0));
                                            gm_t88rcucg1b7uk t860 = gm_l88;
                                            gm_t88rcucg1b7uk t861 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l87) + 4));
                                            uint8_t t862 = gm_f_same_int_value_ngrqsok((void *)NULL, cx, NULL, t859, t860, t861);
                                            if (gm_unw.kind) {
                                                goto gm_ll826;
                                            }
                                            t850 = t862;
                                        } else t850 = 1;
                                        if (!t850) goto ie849;
                                        {
                                            goto lc822;
                                        }
                                        goto ix849;
ie849:;
ix849:;
                                    }
                                    {
                                        gm_l32 = ((uint8_t)1);
                                    }
                                    {
                                        void * t864 = F.l0;
                                        void * t865 = gm_l67;
                                        gm_tl874t2atnlh3 t866 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t865) + 120));
                                        gm_tfeab279c16rr t867;
                                        void * t868 = gm_l6;
                                        uint32_t t869 = GM_LD(uint32_t, (((uint8_t *)&gm_l87) + 0));
                                        gm_b16_8 t870 = gm_f_param_name_rbnop5l(t868, t869);
                                        GM_ST(gm_b16_8, (uint8_t *)&t867 + 0, t870);
                                        void * t871 = F.l0;
                                        gm_t88rcucg1b7uk t872 = gm_l88;
                                        gm_b16_8 t873 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t871, t872);
                                        if (gm_unw.kind) {
                                            goto gm_ll826;
                                        }
                                        GM_ST(gm_b16_8, (uint8_t *)&t867 + 16, t873);
                                        void * t874 = F.l0;
                                        gm_t88rcucg1b7uk t875 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l87) + 4));
                                        gm_b16_8 t876 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t874, t875);
                                        if (gm_unw.kind) {
                                            goto gm_ll826;
                                        }
                                        GM_ST(gm_b16_8, (uint8_t *)&t867 + 32, t876);
                                        gm_tabb710d9mq2e t877;
                                        gm_f_format_7u7i1da((void *)&t877, (void *)NULL, cx, NULL, &t867);
                                        if (gm_unw.kind) {
                                            goto gm_ll826;
                                        }
                                        gm_tabb710d9mq2e t878 = t877;
                                        if (((((uint8_t *)&t878))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t878) + 8, gm_srcrorgjlg51brd, 230, 30);
                                        gm_b16_8 t879 = GM_LD(gm_b16_8, (((uint8_t *)&t878) + 8));
                                        gm_b16_8 t880 = t879;
                                        gm_b16_8 t881 = GM_LD(gm_b16_8, ((uint8_t *)&t880));
                                        gm_b0_1 t882 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t864, t866, t881);
                                        if (gm_unw.kind) {
                                            goto gm_ll826;
                                        }
                                    }
                                    goto me831;
mn832:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 223, 9);
me831:;
                                }
lc822:;
                                {
                                    uint64_t t884 = gm_l86;
                                    uint64_t r885; bool o886 = __builtin_add_overflow(t884, ((uint64_t)1ULL), &r885);
                                    uint64_t t887 = r885;
                                    gm_l86 = t887;
                                }
                            }
gm_ll826:;
                            if (gm_unw.frame == &F.h && gm_unw.label == 12) {
                                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb822; }
                                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc822; }
                            }
                            goto gm_ll490;
lb822:;
                        }
                    }
                    {
                        uint64_t t888 = gm_l66;
                        uint64_t t889 = (uintptr_t)t888;
                        uint64_t t890 = GM_LD(uintptr_t, (((uint8_t *)&F.l33) + 0) + 8);
                        if (t889 >= (uintptr_t)(t890)) gm_trap_index(t889, t890, gm_srcrorgjlg51brd, 232, 9);
                        void * t891 = gm_l81;
                        GM_ST(void *, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l33) + 0)) + t889 * 8), t891);
                    }
                    goto me494;
mn495:;
                    gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 158, 5);
me494:;
                }
lc486:;
                {
                    uint64_t t893 = gm_l64;
                    uint64_t r894; bool o895 = __builtin_add_overflow(t893, ((uint64_t)1ULL), &r894);
                    uint64_t t896 = r894;
                    gm_l64 = t896;
                }
            }
gm_ll490:;
            if (gm_unw.frame == &F.h && gm_unw.label == 10) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb486; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc486; }
            }
            goto gm_lad;
lb486:;
        }
    }
    {
        uint8_t t898 = gm_l32;
        if (!t898) goto ie897;
        {
            void * t900 = F.l0;
            void * t901 = GM_LD(void *, (((uint8_t *)t900) + 0));
            gm_tl874t2atnlh3 t902 = gm_l4;
            void * t903 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t901, t902);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t904 = 0;
            F.ret = t903;
            goto gm_fret;
        }
        goto ix897;
ie897:;
ix897:;
    }
    {
        void * t905 = F.l0;
        void * t906 = (void *)((uint8_t *)&F.l5);
        void * t907 = (void *)((uint8_t *)&F.l30);
        gm_b16_8 t908 = GM_LD(gm_b16_8, (((uint8_t *)&F.l31) + 0));
        gm_b16_8 t909 = t908;
        gm_b16_8 t910 = GM_LD(gm_b16_8, ((uint8_t *)&t909));
        gm_b0_1 t911 = gm_f_bind_literal_fallbacks_r2021qp((void *)NULL, cx, NULL, t905, t906, t907, t910);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t912 = F.l0;
        void * t913 = (void *)((uint8_t *)&F.l5);
        gm_b16_8 t914 = GM_LD(gm_b16_8, (((uint8_t *)&F.l33) + 0));
        gm_b16_8 t915 = t914;
        gm_b16_8 t916 = GM_LD(gm_b16_8, ((uint8_t *)&t915));
        void * t917 = (void *)((uint8_t *)&F.l30);
        gm_b0_1 t918 = gm_f_bind_unit_defaults_htn8poj((void *)NULL, cx, NULL, t912, t913, t916, t917);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t919 = F.l0;
        void * t920 = (void *)((uint8_t *)&F.l5);
        void * t921 = (void *)((uint8_t *)&F.l30);
        gm_b0_1 t922 = gm_f_bind_available_effects_uav06po((void *)NULL, cx, NULL, t919, t920, t921);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t923 = F.l0;
        void * t924 = (void *)((uint8_t *)&F.l5);
        gm_b16_8 t925 = GM_LD(gm_b16_8, (((uint8_t *)&F.l30) + 0));
        gm_b16_8 t926 = t925;
        gm_b16_8 t927 = GM_LD(gm_b16_8, ((uint8_t *)&t926));
        gm_b16_8 t928 = GM_LD(gm_b16_8, (((uint8_t *)&F.l33) + 0));
        gm_b16_8 t929 = t928;
        gm_b16_8 t930 = GM_LD(gm_b16_8, ((uint8_t *)&t929));
        gm_tl874t2atnlh3 t931 = gm_l4;
        gm_th2lqbo77pvsc t932;
        gm_f_generic_type_args_mj9hh1l((void *)&t932, (void *)NULL, cx, NULL, t923, t924, t927, t930, t931);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_th2lqbo77pvsc t933 = t932;
        gm_b16_8 t934;
        if (((((uint8_t *)&t933))[16] != 0)) {
            gm_b16_8 t935 = GM_LD(gm_b16_8, (((uint8_t *)&t933) + 0));
            t934 = t935;
        } else {
            void * t936 = F.l0;
            void * t937 = GM_LD(void *, (((uint8_t *)t936) + 0));
            gm_tl874t2atnlh3 t938 = gm_l4;
            void * t939 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t937, t938);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t940 = 0;
            F.ret = t939;
            goto gm_fret;
        }
        gm_l89 = t934;
    }
    {
        void * t942 = F.l0;
        void * t943 = (void *)((uint8_t *)&F.l5);
        gm_b16_8 t944 = gm_l89;
        gm_b16_8 t945 = t944;
        gm_b16_8 t946 = GM_LD(gm_b16_8, ((uint8_t *)&t945));
        gm_tl874t2atnlh3 t947 = gm_l4;
        uint8_t t948 = gm_f_check_generic_constraint_6138rrh((void *)NULL, cx, NULL, t942, t943, t946, t947);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t949 = (uint8_t)!t948;
        if (!t949) goto ie941;
        {
            void * t951 = F.l0;
            void * t952 = GM_LD(void *, (((uint8_t *)t951) + 0));
            gm_tl874t2atnlh3 t953 = gm_l4;
            void * t954 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t952, t953);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t955 = 0;
            F.ret = t954;
            goto gm_fret;
        }
        goto ix941;
ie941:;
ix941:;
    }
    {
        uint8_t t957 = gm_l19;
        if (!t957) goto ie956;
        {
            void * t959 = F.l0;
            void * t960 = F.l0;
            void * t961 = GM_LD(void *, (((uint8_t *)t960) + 0));
            uint32_t t962 = gm_l1;
            gm_b16_8 t963 = gm_l89;
            gm_b16_8 t964 = t963;
            gm_b16_8 t965 = GM_LD(gm_b16_8, ((uint8_t *)&t964));
            gm_tl874t2atnlh3 t966 = gm_l4;
            uint32_t t967 = gm_f_instantiate_35r5pb2((void *)NULL, cx, NULL, t961, t962, t965, t966);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tl874t2atnlh3 t968 = gm_l4;
            void * t969 = gm_f_fn_ref_aqs2fl4((void *)NULL, cx, NULL, t959, t967, t968);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t970 = 0;
            F.ret = t969;
            goto gm_fret;
        }
        goto ix956;
ie956:;
ix956:;
    }
    {
        gm_f_list_oalq03d((void *)&F.l90, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t972 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 80));
            gm_b16_8 t973 = t972;
            gm_b16_8 t974 = GM_LD(gm_b16_8, ((uint8_t *)&t973));
            gm_l91 = t974;
        }
        {
            gm_l92 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t976 = gm_l92;
                uint64_t t977 = GM_LD(uintptr_t, ((uint8_t *)&gm_l91) + 8);
                uint8_t t978 = (t976 < t977);
                if (!t978) goto lb975;
                {
                    uint64_t t981 = gm_l92;
                    uint64_t t982 = gm_l92;
                    uint64_t t983 = (uintptr_t)t982;
                    uint64_t t984 = GM_LD(uintptr_t, ((uint8_t *)&gm_l91) + 8);
                    uint32_t t985 = GM_LD(uint32_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l91)) + t983 * 4));
                    gm_tn68el8d14vku t986;
                    GM_ST(uint32_t, (uint8_t *)&t986 + 12, 0);
                    GM_ST(uint64_t, ((uint8_t *)&t986 + 0), t981);
                    GM_ST(uint32_t, ((uint8_t *)&t986 + 8), t985);
                    gm_tn68el8d14vku t987 = t986;
                    uint64_t t990 = GM_LD(uint64_t, (((uint8_t *)&t987) + 0));
                    gm_l93 = t990;
                    uint32_t t991 = GM_LD(uint32_t, (((uint8_t *)&t987) + 8));
                    gm_l94 = t991;
                    {
                        void * t993 = (void *)((uint8_t *)&F.l90);
                        uint32_t t994 = gm_l94;
                        uint64_t t995 = gm_l93;
                        uint64_t t996 = (uintptr_t)t995;
                        uint64_t t997 = GM_LD(uintptr_t, ((uint8_t *)&gm_l89) + 8);
                        if (t996 >= (uintptr_t)(t997)) gm_trap_index(t996, t997, gm_srcrorgjlg51brd, 242, 72);
                        gm_t88rcucg1b7uk t998 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l89)) + t996 * 4));
                        gm_tplpbqf3duvn3 t999;
                        GM_ST(uint32_t, ((uint8_t *)&t999 + 0), t994);
                        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t999 + 4), t998);
                        gm_tftb1odc739t5 t1000 = gm_f_push_h69n5fi(t993, GM_CONV(gm_cs7e9ppsd384bj, t999));
                        gm_tftb1odc739t5 t1001 = t1000;
                        if (((((uint8_t *)&t1001))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t1001) + 1, gm_srcrorgjlg51brd, 242, 43);
                        gm_b0_1 t1002 = GM_LD(gm_b0_1, (((uint8_t *)&t1001) + 1));
                    }
                    goto me988;
mn989:;
                    gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 242, 5);
me988:;
                }
lc975:;
                {
                    uint64_t t1004 = gm_l92;
                    uint64_t r1005; bool o1006 = __builtin_add_overflow(t1004, ((uint64_t)1ULL), &r1005);
                    uint64_t t1007 = r1005;
                    gm_l92 = t1007;
                }
            }
lb975:;
        }
    }
    {
        gm_f_list_5dhgg4i((void *)&F.l95, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        if (!((((uint8_t *)&gm_l7))[8] != 0)) goto ie1008;
        uint64_t t1009 = GM_LD(uint64_t, ((uint8_t *)&gm_l7));
        gm_l96 = t1009;
        {
            void * t1011 = (void *)((uint8_t *)&F.l95);
            uint64_t t1012 = gm_l96;
            gm_tftb1odc739t5 t1013 = gm_f_push_ucgmlo2(t1011, t1012);
            gm_tftb1odc739t5 t1014 = t1013;
            if (((((uint8_t *)&t1014))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t1014) + 1, gm_srcrorgjlg51brd, 245, 26);
            gm_b0_1 t1015 = GM_LD(gm_b0_1, (((uint8_t *)&t1014) + 1));
        }
        goto ix1008;
ie1008:;
ix1008:;
    }
    {
        {
            gm_b16_8 t1017 = GM_LD(gm_b16_8, (((uint8_t *)&F.l36) + 0));
            gm_l97 = t1017;
        }
        {
            gm_l98 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t1019 = gm_l98;
                uint64_t t1020 = GM_LD(uintptr_t, ((uint8_t *)&gm_l97) + 8);
                uint8_t t1021 = (t1019 < t1020);
                if (!t1021) goto lb1018;
                {
                    uint64_t t1024 = gm_l98;
                    uint64_t t1025 = (uintptr_t)t1024;
                    uint64_t t1026 = GM_LD(uintptr_t, ((uint8_t *)&gm_l97) + 8);
                    gm_t5retglboi70s t1029 = GM_LD(gm_t5retglboi70s, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l97)) + t1025 * 152));
                    F.l99 = t1029;
                    {
                        uint64_t t1032 = GM_LD(uint64_t, (((uint8_t *)&F.l99) + 0));
                        uint64_t t1033 = (uintptr_t)t1032;
                        uint64_t t1034 = GM_LD(uintptr_t, (((uint8_t *)&F.l5) + 40) + 8);
                        if (t1033 >= (uintptr_t)(t1034)) gm_trap_index(t1033, t1034, gm_srcrorgjlg51brd, 247, 12);
                        uint8_t t1035;
                        if ((((((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l5) + 40)) + t1033 * 56) + 40))[4] != 0)) goto mn1037;
                        t1035 = ((uint8_t)1);
                        goto me1036;
mn1037:;
                        t1035 = ((uint8_t)0);
                        goto me1036;
mn1038:;
                        gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 247, 12);
me1036:;
                        if (!t1035) goto ie1031;
                        uint64_t t1039 = GM_LD(uint64_t, (((uint8_t *)&F.l99) + 0));
                        uint64_t t1040 = (uintptr_t)t1039;
                        uint64_t t1041 = GM_LD(uintptr_t, (((uint8_t *)&F.l33) + 0) + 8);
                        if (t1040 >= (uintptr_t)(t1041)) gm_trap_index(t1040, t1041, gm_srcrorgjlg51brd, 247, 54);
                        void * t1042 = GM_LD(void *, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l33) + 0)) + t1040 * 8));
                        void * t1043 = t1042;
                        uint8_t t1044;
                        if ((GM_LD(void *, ((uint8_t *)&t1043)) != 0)) goto mn1046;
                        t1044 = ((uint8_t)0);
                        goto me1045;
mn1046:;
                        t1044 = ((uint8_t)1);
                        goto me1045;
mn1047:;
                        gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 247, 54);
me1045:;
                        if (!t1044) goto ie1031;
                        {
                            void * t1049 = (void *)((uint8_t *)&F.l95);
                            uint64_t t1050 = GM_LD(uint64_t, (((uint8_t *)&F.l99) + 0));
                            gm_tftb1odc739t5 t1051 = gm_f_push_ucgmlo2(t1049, t1050);
                            gm_tftb1odc739t5 t1052 = t1051;
                            if (((((uint8_t *)&t1052))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t1052) + 1, gm_srcrorgjlg51brd, 247, 92);
                            gm_b0_1 t1053 = GM_LD(gm_b0_1, (((uint8_t *)&t1052) + 1));
                        }
                        goto ix1031;
ie1031:;
ix1031:;
                    }
                    goto me1027;
mn1028:;
                    gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 246, 5);
me1027:;
                }
lc1018:;
                {
                    uint64_t t1055 = gm_l98;
                    uint64_t r1056; bool o1057 = __builtin_add_overflow(t1055, ((uint64_t)1ULL), &r1056);
                    uint64_t t1058 = r1056;
                    gm_l98 = t1058;
                }
            }
lb1018:;
        }
    }
    {
        void * t1059 = F.l0;
        void * t1060 = (void *)((uint8_t *)&F.l5);
        gm_b16_8 t1061 = GM_LD(gm_b16_8, (((uint8_t *)&F.l33) + 0));
        gm_b16_8 t1062 = t1061;
        gm_b16_8 t1063 = GM_LD(gm_b16_8, ((uint8_t *)&t1062));
        gm_b16_8 t1064 = GM_LD(gm_b16_8, (((uint8_t *)&F.l90) + 0));
        gm_b16_8 t1065 = t1064;
        gm_b16_8 t1066 = GM_LD(gm_b16_8, ((uint8_t *)&t1065));
        gm_tl874t2atnlh3 t1067 = gm_l4;
        gm_f_coerce_generic_args_9ald24q((void *)&F.l100, (void *)NULL, cx, NULL, t1059, t1060, t1063, t1066, t1067);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t1068 = (void *)(((uint8_t *)&F.l100) + 0);
        gm_l101 = t1068;
    }
    {
        gm_trqfvmjtt4sij t1069 = GM_LD(gm_trqfvmjtt4sij, (((uint8_t *)&F.l100) + 40));
        F.l102 = t1069;
    }
    {
        void * t1070 = F.l0;
        void * t1071 = GM_LD(void *, (((uint8_t *)t1070) + 0));
        void * t1072 = (void *)(((uint8_t *)&F.l5) + 64);
        gm_b16_8 t1073 = GM_LD(gm_b16_8, (((uint8_t *)&F.l90) + 0));
        gm_b16_8 t1074 = t1073;
        gm_b16_8 t1075 = GM_LD(gm_b16_8, ((uint8_t *)&t1074));
        gm_tnpifv20irnph t1076 = gm_f_subst_effects_jnlbu5m((void *)NULL, cx, NULL, t1071, t1072, t1075);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l103 = t1076;
    }
    {
        void * t1077 = F.l0;
        void * t1078 = (void *)((uint8_t *)&gm_l103);
        gm_tl874t2atnlh3 t1079 = gm_l4;
        gm_b0_1 t1080 = gm_f_record_effects_0eel5vu((void *)NULL, cx, NULL, t1077, t1078, t1079);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t1081 = F.l0;
        void * t1082 = gm_l101;
        gm_b16_8 t1083 = GM_LD(gm_b16_8, (((uint8_t *)&F.l102) + 0));
        gm_b16_8 t1084 = t1083;
        gm_b16_8 t1085 = GM_LD(gm_b16_8, ((uint8_t *)&t1084));
        gm_b16_8 t1086 = GM_LD(gm_b16_8, (((uint8_t *)&F.l95) + 0));
        void * t1087 = (void *)((uint8_t *)&F.l51);
        gm_b16_8 t1088 = GM_LD(gm_b16_8, (((uint8_t *)&F.l47) + 0));
        gm_b16_8 t1089 = t1088;
        gm_b16_8 t1090 = GM_LD(gm_b16_8, ((uint8_t *)&t1089));
        gm_b16_8 t1091 = gm_f_order_generic_args_m8gcs0c((void *)NULL, cx, NULL, t1081, t1082, t1085, t1086, t1087, t1090);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l104 = t1091;
    }
    {
        void * t1092 = F.l0;
        void * t1093 = GM_LD(void *, (((uint8_t *)t1092) + 0));
        uint32_t t1094 = gm_l1;
        gm_b16_8 t1095 = gm_l89;
        gm_b16_8 t1096 = t1095;
        gm_b16_8 t1097 = GM_LD(gm_b16_8, ((uint8_t *)&t1096));
        gm_tl874t2atnlh3 t1098 = gm_l4;
        uint32_t t1099 = gm_f_instantiate_35r5pb2((void *)NULL, cx, NULL, t1093, t1094, t1097, t1098);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l105 = t1099;
    }
    {
        void * t1100 = F.l0;
        void * t1101 = GM_LD(void *, (((uint8_t *)t1100) + 0));
        gm_t88rcucg1b7uk t1102 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l5) + 56));
        gm_b16_8 t1103 = GM_LD(gm_b16_8, (((uint8_t *)&F.l90) + 0));
        gm_b16_8 t1104 = t1103;
        gm_b16_8 t1105 = GM_LD(gm_b16_8, ((uint8_t *)&t1104));
        gm_t88rcucg1b7uk t1106 = gm_f_subst_a2s1mb3((void *)NULL, cx, NULL, t1101, t1102, t1105);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_to76jli3bqddq t1107;
        uint32_t t1108 = gm_l105;
        gm_tsth9qu1dv0g3 t1109;
        memset(&t1109, 0, sizeof t1109);
        GM_ST(uint8_t, ((uint8_t *)&t1109 + 0), ((uint8_t)0ULL));
        GM_ST(uint32_t, ((uint8_t *)&t1109 + 8), t1108);
        GM_ST(gm_tsth9qu1dv0g3, (uint8_t *)&t1107 + 0, t1109);
        void * t1110 = gm_l101;
        gm_b16_8 t1111 = GM_LD(gm_b16_8, (((uint8_t *)t1110) + 0));
        GM_ST(gm_b16_8, (uint8_t *)&t1107 + 40, t1111);
        gm_t5brlu7fal70j t1112;
        memset(&t1112, 0, sizeof t1112);
        GM_ST(uint8_t, ((uint8_t *)&t1112 + 0), ((uint8_t)34ULL));
        GM_ST(gm_to76jli3bqddq, ((uint8_t *)&t1112 + 8), t1107);
        gm_tl874t2atnlh3 t1113 = gm_l4;
        void * t1114 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t1106, &t1112, t1113);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l106 = t1114;
    }
    {
        void * t1115 = F.l0;
        gm_b16_8 t1116 = gm_l104;
        void * t1117 = gm_l106;
        void * t1118 = gm_f_finish_call_kjknkfg((void *)NULL, cx, NULL, t1115, t1116, t1117, ((uint8_t)0));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t1119 = 0;
        F.ret = t1118;
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
__attribute__((aligned(4))) void * gm_f_generic_fn_from_expected_ok2k7go(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_tkn42u7na3t81 a2, gm_tl874t2atnlh3 a3) {
    struct gm_fr_generic_fn_from_expected_ok2k7go F;
    void * gm_l0; /* cx */
    uint32_t gm_l1; /* f */
    gm_tkn42u7na3t81 gm_l2; /* expect */
    gm_tl874t2atnlh3 gm_l3; /* span */
    gm_t88rcucg1b7uk gm_l4; /* e */
    gm_b16_8 gm_l8; /* <tmp> */
    uint64_t gm_l9; /* <tmp> */
    gm_b16_8 gm_l12; /* <tmp> */
    uint64_t gm_l13; /* <tmp> */
    uint64_t gm_l14; /* i */
    gm_t88rcucg1b7uk gm_l15; /* t */
    gm_b16_8 gm_l17; /* <tmp> */
    uint64_t gm_l18; /* <tmp> */
    uint32_t gm_l19; /* pid */
    gm_b16_8 gm_l20; /* <tmp> */
    uint64_t gm_l21; /* <tmp> */
    uint64_t gm_l22; /* k */
    uint32_t gm_l23; /* pid */
    gm_t88rcucg1b7uk gm_l24; /* t */
    gm_t88rcucg1b7uk gm_l25; /* part */
    gm_b16_8 gm_l26; /* <tmp> */
    uint64_t gm_l27; /* <tmp> */
    gm_t9qkh0rsooc8d gm_l28; /* c */
    GM_STACK_CHECK(&F, 2038u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        gm_tkn42u7na3t81 t2 = gm_l2;
        gm_tkn42u7na3t81 t3 = t2;
        gm_t88rcucg1b7uk t4;
        if (((((uint8_t *)&t3))[4] != 0)) {
            gm_t88rcucg1b7uk t5 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t3) + 0));
            t4 = t5;
        } else {
            uint8_t t6 = 0;
            F.ret = ((void *)0);
            goto gm_fret;
        }
        gm_l4 = t4;
    }
    {
        void * t8 = gm_l0;
        void * t9 = GM_LD(void *, (((uint8_t *)t8) + 0));
        void * t10 = (void *)(((uint8_t *)t9) + 8);
        void * t11 = gm_l0;
        void * t12 = (void *)(((uint8_t *)t11) + 96);
        void * t13 = gm_l0;
        void * t14 = GM_LD(void *, (((uint8_t *)t13) + 0));
        gm_t88rcucg1b7uk t15 = gm_l4;
        gm_t88rcucg1b7uk t16 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t12, t14, t15);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t53at80pfdus3 t17;
        gm_f_kind_jcnt4hm((void *)&t17, t10, GM_CONV(gm_csguankko4lmhd, t16));
        gm_t53at80pfdus3 t18 = t17;
        uint8_t t19 = GM_LD(uint8_t, (((uint8_t *)&t18) + 0));
        if (t19 != ((uint8_t)16ULL)) goto ie7;
        gm_tddtjfggdat0d t20 = GM_LD(gm_tddtjfggdat0d, (((uint8_t *)&t18) + 8));
        F.l5 = t20;
        {
            uint32_t t22 = gm_l1;
            uint64_t t23 = (uint64_t)t22;
            uint64_t t24 = (uintptr_t)t23;
            void * t25 = gm_l0;
            void * t26 = GM_LD(void *, (((uint8_t *)t25) + 0));
            uint64_t t27 = GM_LD(uintptr_t, ((((uint8_t *)t26) + 344) + 0) + 8);
            if (t24 >= (uintptr_t)(t27)) gm_trap_index(t24, t27, gm_srcrorgjlg51brd, 651, 12);
            gm_tqqhoivahirr0 t28 = GM_LD(gm_tqqhoivahirr0, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t26) + 344) + 0)) + t24 * 136));
            F.l6 = t28;
        }
        {
            gm_f_list_nof7vfj((void *)&F.l7, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            {
                gm_b16_8 t30 = GM_LD(gm_b16_8, (((uint8_t *)&F.l6) + 40));
                gm_l8 = t30;
            }
            {
                gm_l9 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t32 = gm_l9;
                    uint64_t t33 = GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8);
                    uint8_t t34 = (t32 < t33);
                    if (!t34) goto lb31;
                    {
                        uint64_t t37 = gm_l9;
                        uint64_t t38 = (uintptr_t)t37;
                        uint64_t t39 = GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8);
                        gm_t5v0c8ia6a5mo t42 = GM_LD(gm_t5v0c8ia6a5mo, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l8)) + t38 * 56));
                        F.l10 = t42;
                        {
                            uint8_t t45;
                            if ((((((uint8_t *)&F.l10) + 40))[4] != 0)) goto mn47;
                            t45 = ((uint8_t)1);
                            goto me46;
mn47:;
                            t45 = ((uint8_t)0);
                            goto me46;
mn48:;
                            gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 654, 12);
me46:;
                            if (!t45) goto ie44;
                            {
                                void * t50 = (void *)((uint8_t *)&F.l7);
                                gm_t88rcucg1b7uk t51 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l10) + 16));
                                gm_tftb1odc739t5 t52 = gm_f_push_k3l9nic(t50, GM_CONV(gm_csguankko4lmhd, t51));
                                gm_tftb1odc739t5 t53 = t52;
                                if (((((uint8_t *)&t53))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t53) + 1, gm_srcrorgjlg51brd, 654, 32);
                                gm_b0_1 t54 = GM_LD(gm_b0_1, (((uint8_t *)&t53) + 1));
                            }
                            goto ix44;
ie44:;
ix44:;
                        }
                        goto me40;
mn41:;
                        gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 653, 5);
me40:;
                    }
lc31:;
                    {
                        uint64_t t56 = gm_l9;
                        uint64_t r57; bool o58 = __builtin_add_overflow(t56, ((uint64_t)1ULL), &r57);
                        uint64_t t59 = r57;
                        gm_l9 = t59;
                    }
                }
lb31:;
            }
        }
        {
            uint64_t t61 = GM_LD(uintptr_t, (((uint8_t *)&F.l7) + 0) + 8);
            uint64_t t62 = GM_LD(uintptr_t, (((uint8_t *)&F.l5) + 0) + 8);
            uint8_t t63 = (!(t61 == t62));
            if (!t63) goto ie60;
            {
                uint8_t t65 = 0;
                F.ret = ((void *)0);
                goto gm_fret;
            }
            goto ix60;
ie60:;
ix60:;
        }
        {
            gm_f_list_oalq03d((void *)&F.l11, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            {
                gm_b16_8 t67 = GM_LD(gm_b16_8, (((uint8_t *)&F.l7) + 0));
                gm_b16_8 t68 = t67;
                gm_b16_8 t69 = GM_LD(gm_b16_8, ((uint8_t *)&t68));
                gm_l12 = t69;
            }
            {
                gm_l13 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t71 = gm_l13;
                    uint64_t t72 = GM_LD(uintptr_t, ((uint8_t *)&gm_l12) + 8);
                    uint8_t t73 = (t71 < t72);
                    if (!t73) goto lb70;
                    {
                        uint64_t t76 = gm_l13;
                        uint64_t t77 = gm_l13;
                        uint64_t t78 = (uintptr_t)t77;
                        uint64_t t79 = GM_LD(uintptr_t, ((uint8_t *)&gm_l12) + 8);
                        gm_t88rcucg1b7uk t80 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l12)) + t78 * 4));
                        gm_tlcm4lvu3g1a8 t81;
                        GM_ST(uint32_t, (uint8_t *)&t81 + 12, 0);
                        GM_ST(uint64_t, ((uint8_t *)&t81 + 0), t76);
                        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t81 + 8), t80);
                        gm_tlcm4lvu3g1a8 t82 = t81;
                        uint64_t t85 = GM_LD(uint64_t, (((uint8_t *)&t82) + 0));
                        gm_l14 = t85;
                        gm_t88rcucg1b7uk t86 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t82) + 8));
                        gm_l15 = t86;
                        {
                            void * t88 = gm_l0;
                            void * t89 = GM_LD(void *, (((uint8_t *)t88) + 0));
                            gm_t88rcucg1b7uk t90 = gm_l15;
                            uint64_t t91 = gm_l14;
                            uint64_t t92 = (uintptr_t)t91;
                            uint64_t t93 = GM_LD(uintptr_t, (((uint8_t *)&F.l5) + 0) + 8);
                            if (t92 >= (uintptr_t)(t93)) gm_trap_index(t92, t93, gm_srcrorgjlg51brd, 658, 65);
                            gm_t88rcucg1b7uk t94 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l5) + 0)) + t92 * 4));
                            void * t95 = (void *)((uint8_t *)&F.l11);
                            gm_b0_1 t96 = gm_f_match_params_6vndkud((void *)NULL, cx, NULL, t89, t90, t94, t95);
                            if (gm_unw.kind) {
                                goto gm_ll74;
                            }
                        }
                        goto me83;
mn84:;
                        gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 658, 5);
me83:;
                    }
lc70:;
                    {
                        uint64_t t98 = gm_l13;
                        uint64_t r99; bool o100 = __builtin_add_overflow(t98, ((uint64_t)1ULL), &r99);
                        uint64_t t101 = r99;
                        gm_l13 = t101;
                    }
                }
gm_ll74:;
                if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb70; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc70; }
                }
                goto gm_lad;
lb70:;
            }
        }
        {
            void * t102 = gm_l0;
            void * t103 = GM_LD(void *, (((uint8_t *)t102) + 0));
            gm_t88rcucg1b7uk t104 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l6) + 56));
            gm_t88rcucg1b7uk t105 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l5) + 16));
            void * t106 = (void *)((uint8_t *)&F.l11);
            gm_b0_1 t107 = gm_f_match_params_6vndkud((void *)NULL, cx, NULL, t103, t104, t105, t106);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_f_list_nof7vfj((void *)&F.l16, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            {
                gm_b16_8 t109 = GM_LD(gm_b16_8, (((uint8_t *)&F.l6) + 80));
                gm_l17 = t109;
            }
            {
                gm_l18 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t111 = gm_l18;
                    uint64_t t112 = GM_LD(uintptr_t, ((uint8_t *)&gm_l17) + 8);
                    uint8_t t113 = (t111 < t112);
                    if (!t113) goto lb110;
                    {
                        uint64_t t116 = gm_l18;
                        uint64_t t117 = (uintptr_t)t116;
                        uint64_t t118 = GM_LD(uintptr_t, ((uint8_t *)&gm_l17) + 8);
                        uint32_t t119 = GM_LD(uint32_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l17)) + t117 * 4));
                        uint32_t t120 = t119;
                        uint32_t t123 = GM_LD(uint32_t, ((uint8_t *)&t120));
                        gm_l19 = t123;
                        {
                            void * t126 = gm_l0;
                            void * t127 = GM_LD(void *, (((uint8_t *)t126) + 0));
                            uint32_t t128 = gm_l19;
                            gm_tnnjeerolos0a t129 = gm_f_param_kind_a0aikmb(t127, t128);
                            gm_tnnjeerolos0a t130 = t129;
                            uint8_t t131;
                            uint8_t t134 = GM_LD(uint8_t, (((uint8_t *)&t130) + 0));
                            if (t134 != ((uint8_t)2ULL)) goto mn133;
                            t131 = ((uint8_t)1);
                            goto me132;
mn133:;
                            t131 = ((uint8_t)0);
                            goto me132;
mn135:;
                            gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 662, 12);
me132:;
                            if (!t131) goto ie125;
                            {
                                uint8_t t137 = 0;
                                F.ret = ((void *)0);
                                goto gm_fret;
                            }
                            goto ix125;
ie125:;
ix125:;
                        }
                        {
                            void * t138 = (void *)((uint8_t *)&F.l16);
                            gm_b16_8 t139 = GM_LD(gm_b16_8, (((uint8_t *)&F.l11) + 0));
                            gm_b16_8 t140 = t139;
                            gm_b16_8 t141 = GM_LD(gm_b16_8, ((uint8_t *)&t140));
                            uint32_t t142 = gm_l19;
                            gm_tkn42u7na3t81 t143 = gm_f_bound_to_5c11t0h(t141, t142);
                            gm_tkn42u7na3t81 t144 = t143;
                            gm_t88rcucg1b7uk t145;
                            if (((((uint8_t *)&t144))[4] != 0)) {
                                gm_t88rcucg1b7uk t146 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t144) + 0));
                                t145 = t146;
                            } else {
                                uint8_t t147 = 0;
                                F.ret = ((void *)0);
                                goto gm_fret;
                            }
                            gm_tftb1odc739t5 t148 = gm_f_push_k3l9nic(t138, GM_CONV(gm_csguankko4lmhd, t145));
                            gm_tftb1odc739t5 t149 = t148;
                            if (((((uint8_t *)&t149))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t149) + 1, gm_srcrorgjlg51brd, 663, 9);
                            gm_b0_1 t150 = GM_LD(gm_b0_1, (((uint8_t *)&t149) + 1));
                        }
                        goto me121;
mn122:;
                        gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 661, 5);
me121:;
                    }
lc110:;
                    {
                        uint64_t t152 = gm_l18;
                        uint64_t r153; bool o154 = __builtin_add_overflow(t152, ((uint64_t)1ULL), &r153);
                        uint64_t t155 = r153;
                        gm_l18 = t155;
                    }
                }
lb110:;
            }
        }
        {
            {
                gm_b16_8 t157 = GM_LD(gm_b16_8, (((uint8_t *)&F.l6) + 80));
                gm_b16_8 t158 = t157;
                gm_b16_8 t159 = GM_LD(gm_b16_8, ((uint8_t *)&t158));
                gm_l20 = t159;
            }
            {
                gm_l21 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t161 = gm_l21;
                    uint64_t t162 = GM_LD(uintptr_t, ((uint8_t *)&gm_l20) + 8);
                    uint8_t t163 = (t161 < t162);
                    if (!t163) goto lb160;
                    {
                        uint64_t t166 = gm_l21;
                        uint64_t t167 = gm_l21;
                        uint64_t t168 = (uintptr_t)t167;
                        uint64_t t169 = GM_LD(uintptr_t, ((uint8_t *)&gm_l20) + 8);
                        uint32_t t170 = GM_LD(uint32_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l20)) + t168 * 4));
                        gm_tn68el8d14vku t171;
                        GM_ST(uint32_t, (uint8_t *)&t171 + 12, 0);
                        GM_ST(uint64_t, ((uint8_t *)&t171 + 0), t166);
                        GM_ST(uint32_t, ((uint8_t *)&t171 + 8), t170);
                        gm_tn68el8d14vku t172 = t171;
                        uint64_t t175 = GM_LD(uint64_t, (((uint8_t *)&t172) + 0));
                        gm_l22 = t175;
                        uint32_t t176 = GM_LD(uint32_t, (((uint8_t *)&t172) + 8));
                        gm_l23 = t176;
                        {
                            uint64_t t178 = gm_l22;
                            uint64_t t179 = (uintptr_t)t178;
                            uint64_t t180 = GM_LD(uintptr_t, (((uint8_t *)&F.l16) + 0) + 8);
                            if (t179 >= (uintptr_t)(t180)) gm_trap_index(t179, t180, gm_srcrorgjlg51brd, 666, 14);
                            gm_t88rcucg1b7uk t181 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l16) + 0)) + t179 * 4));
                            gm_l24 = t181;
                        }
                        {
                            void * t183 = gm_l0;
                            void * t184 = GM_LD(void *, (((uint8_t *)t183) + 0));
                            gm_t88rcucg1b7uk t185 = gm_l24;
                            gm_tkn42u7na3t81 t186 = gm_f_second_class_part_k6i54mk(t184, GM_CONV(gm_csguankko4lmhd, t185));
                            gm_tkn42u7na3t81 t187 = t186;
                            if (!((((uint8_t *)&t187))[4] != 0)) goto ie182;
                            gm_t88rcucg1b7uk t188 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&t187));
                            gm_l25 = t188;
                            {
                                void * t190 = gm_l0;
                                gm_tl874t2atnlh3 t191 = gm_l3;
                                void * t192 = gm_l0;
                                void * t193 = GM_LD(void *, (((uint8_t *)t192) + 0));
                                gm_t88rcucg1b7uk t194 = gm_l24;
                                gm_t88rcucg1b7uk t195 = gm_l25;
                                gm_b16_8 t196 = gm_f_not_first_class_dve75oi((void *)NULL, cx, NULL, t193, t194, t195);
                                if (gm_unw.kind) {
                                    goto gm_ll164;
                                }
                                gm_b0_1 t197 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t190, t191, t196);
                                if (gm_unw.kind) {
                                    goto gm_ll164;
                                }
                            }
                            goto ix182;
ie182:;
ix182:;
                        }
                        {
                            {
                                void * t199 = gm_l0;
                                void * t200 = GM_LD(void *, (((uint8_t *)t199) + 0));
                                uint32_t t201 = gm_l23;
                                gm_b16_8 t202 = gm_f_param_constraints_2k5fnnl(t200, t201);
                                gm_l26 = t202;
                            }
                            {
                                gm_l27 = ((uint64_t)0ULL);
                            }
                            {
                                for (;;) {
                                    uint64_t t204 = gm_l27;
                                    uint64_t t205 = GM_LD(uintptr_t, ((uint8_t *)&gm_l26) + 8);
                                    uint8_t t206 = (t204 < t205);
                                    if (!t206) goto lb203;
                                    {
                                        uint64_t t209 = gm_l27;
                                        uint64_t t210 = (uintptr_t)t209;
                                        uint64_t t211 = GM_LD(uintptr_t, ((uint8_t *)&gm_l26) + 8);
                                        gm_t9qkh0rsooc8d t214 = GM_LD(gm_t9qkh0rsooc8d, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l26)) + t210 * 1));
                                        gm_l28 = t214;
                                        {
                                            void * t217 = gm_l0;
                                            void * t218 = GM_LD(void *, (((uint8_t *)t217) + 0));
                                            gm_t88rcucg1b7uk t219 = gm_l24;
                                            gm_t9qkh0rsooc8d t220 = gm_l28;
                                            uint8_t t221 = gm_f_satisfies_p0n2er8((void *)NULL, cx, NULL, t218, t219, t220);
                                            if (gm_unw.kind) {
                                                goto gm_ll207;
                                            }
                                            uint8_t t222 = (uint8_t)!t221;
                                            if (!t222) goto ie216;
                                            {
                                                void * t224 = gm_l0;
                                                gm_tl874t2atnlh3 t225 = gm_l3;
                                                gm_tfeab279c16rr t226;
                                                void * t227 = gm_l0;
                                                gm_t88rcucg1b7uk t228 = gm_l24;
                                                gm_b16_8 t229 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t227, t228);
                                                if (gm_unw.kind) {
                                                    goto gm_ll207;
                                                }
                                                GM_ST(gm_b16_8, (uint8_t *)&t226 + 0, t229);
                                                gm_t9qkh0rsooc8d t230 = gm_l28;
                                                gm_b16_8 t231 = gm_f_constraint_name_ombut3o(t230);
                                                GM_ST(gm_b16_8, (uint8_t *)&t226 + 16, t231);
                                                void * t232 = gm_l0;
                                                void * t233 = GM_LD(void *, (((uint8_t *)t232) + 0));
                                                void * t234 = (void *)(((uint8_t *)t233) + 8);
                                                uint32_t t235 = gm_l23;
                                                gm_b16_8 t236 = gm_f_param_name_rbnop5l(t234, t235);
                                                GM_ST(gm_b16_8, (uint8_t *)&t226 + 32, t236);
                                                gm_tabb710d9mq2e t237;
                                                gm_f_format_d2d7bae((void *)&t237, (void *)NULL, cx, NULL, &t226);
                                                if (gm_unw.kind) {
                                                    goto gm_ll207;
                                                }
                                                gm_tabb710d9mq2e t238 = t237;
                                                if (((((uint8_t *)&t238))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t238) + 8, gm_srcrorgjlg51brd, 670, 32);
                                                gm_b16_8 t239 = GM_LD(gm_b16_8, (((uint8_t *)&t238) + 8));
                                                gm_b16_8 t240 = t239;
                                                gm_b16_8 t241 = GM_LD(gm_b16_8, ((uint8_t *)&t240));
                                                gm_b0_1 t242 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t224, t225, t241);
                                                if (gm_unw.kind) {
                                                    goto gm_ll207;
                                                }
                                            }
                                            goto ix216;
ie216:;
ix216:;
                                        }
                                        goto me212;
mn213:;
                                        gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 668, 9);
me212:;
                                    }
lc203:;
                                    {
                                        uint64_t t244 = gm_l27;
                                        uint64_t r245; bool o246 = __builtin_add_overflow(t244, ((uint64_t)1ULL), &r245);
                                        uint64_t t247 = r245;
                                        gm_l27 = t247;
                                    }
                                }
gm_ll207:;
                                if (gm_unw.frame == &F.h && gm_unw.label == 4) {
                                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb203; }
                                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc203; }
                                }
                                goto gm_ll164;
lb203:;
                            }
                        }
                        goto me173;
mn174:;
                        gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 665, 5);
me173:;
                    }
lc160:;
                    {
                        uint64_t t249 = gm_l21;
                        uint64_t r250; bool o251 = __builtin_add_overflow(t249, ((uint64_t)1ULL), &r250);
                        uint64_t t252 = r250;
                        gm_l21 = t252;
                    }
                }
gm_ll164:;
                if (gm_unw.frame == &F.h && gm_unw.label == 3) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb160; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc160; }
                }
                goto gm_lad;
lb160:;
            }
        }
        {
            void * t253 = gm_l0;
            void * t254 = gm_l0;
            void * t255 = GM_LD(void *, (((uint8_t *)t254) + 0));
            uint32_t t256 = gm_l1;
            gm_b16_8 t257 = GM_LD(gm_b16_8, (((uint8_t *)&F.l16) + 0));
            gm_b16_8 t258 = t257;
            gm_b16_8 t259 = GM_LD(gm_b16_8, ((uint8_t *)&t258));
            gm_tl874t2atnlh3 t260 = gm_l3;
            uint32_t t261 = gm_f_instantiate_35r5pb2((void *)NULL, cx, NULL, t255, t256, t259, t260);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tl874t2atnlh3 t262 = gm_l3;
            void * t263 = gm_f_fn_ref_aqs2fl4((void *)NULL, cx, NULL, t253, t261, t262);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t264 = 0;
            F.ret = t263;
            goto gm_fret;
        }
        goto ix7;
ie7:;
        {
            uint8_t t266 = 0;
            F.ret = ((void *)0);
            goto gm_fret;
        }
ix7:;
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
__attribute__((aligned(4))) void gm_f_generic_type_args_mj9hh1l(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_b16_8 a2, gm_b16_8 a3, gm_tl874t2atnlh3 a4) {
    struct gm_fr_generic_type_args_mj9hh1l F;
    void * gm_l0; /* cx */
    void * gm_l1; /* sig */
    gm_b16_8 gm_l2; /* binds */
    gm_b16_8 gm_l3; /* values */
    gm_tl874t2atnlh3 gm_l4; /* span */
    void * gm_l5; /* tys */
    uint8_t gm_l6; /* failed_arg */
    gm_b16_8 gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    void * gm_l9; /* v */
    void * gm_l10; /* x */
    gm_b16_8 gm_l12; /* <tmp> */
    uint64_t gm_l13; /* <tmp> */
    uint32_t gm_l14; /* pid */
    gm_t88rcucg1b7uk gm_l15; /* vty */
    gm_t88rcucg1b7uk gm_l16; /* t */
    gm_b16_8 gm_l17; /* <tmp> */
    uint64_t gm_l18; /* <tmp> */
    void * gm_l20; /* d */
    gm_t88rcucg1b7uk gm_l21; /* t */
    GM_STACK_CHECK(&F, 1508u);
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
        gm_l6 = ((uint8_t)0);
    }
    {
        {
            gm_b16_8 t6 = gm_l3;
            gm_l7 = t6;
        }
        {
            gm_l8 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t8 = gm_l8;
                uint64_t t9 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                uint8_t t10 = (t8 < t9);
                if (!t10) goto lb7;
                {
                    uint64_t t13 = gm_l8;
                    uint64_t t14 = (uintptr_t)t13;
                    uint64_t t15 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                    void * t16 = GM_LD(void *, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l7)) + t14 * 8));
                    void * t17 = t16;
                    void * t20 = GM_LD(void *, ((uint8_t *)&t17));
                    gm_l9 = t20;
                    {
                        void * t22 = gm_l9;
                        void * t23 = t22;
                        void * t24;
                        if ((GM_LD(void *, ((uint8_t *)&t23)) != 0)) {
                            void * t25 = GM_LD(void *, (((uint8_t *)&t23) + 0));
                            t24 = t25;
                        } else {
                            goto lc7;
                        }
                        gm_l10 = t24;
                    }
                    {
                        void * t27 = gm_l5;
                        void * t28 = gm_l0;
                        void * t29 = (void *)(((uint8_t *)t28) + 96);
                        void * t30 = gm_l0;
                        void * t31 = GM_LD(void *, (((uint8_t *)t30) + 0));
                        void * t32 = gm_l10;
                        gm_t88rcucg1b7uk t33 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t32) + 0));
                        gm_t88rcucg1b7uk t34 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t29, t31, t33);
                        if (gm_unw.kind) {
                            goto gm_ll11;
                        }
                        uint8_t t35 = gm_f_mentions_error_37knu7r(t27, GM_CONV(gm_csguankko4lmhd, t34));
                        if (!t35) goto ie26;
                        {
                            gm_l6 = ((uint8_t)1);
                        }
                        goto ix26;
ie26:;
ix26:;
                    }
                    goto me18;
mn19:;
                    gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 338, 5);
me18:;
                }
lc7:;
                {
                    uint64_t t38 = gm_l8;
                    uint64_t r39; bool o40 = __builtin_add_overflow(t38, ((uint64_t)1ULL), &r39);
                    uint64_t t41 = r39;
                    gm_l8 = t41;
                }
            }
gm_ll11:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb7; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc7; }
            }
            goto gm_lad;
lb7:;
        }
    }
    {
        gm_f_list_nof7vfj((void *)&F.l11, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            void * t43 = gm_l1;
            gm_b16_8 t44 = GM_LD(gm_b16_8, (((uint8_t *)t43) + 80));
            gm_l12 = t44;
        }
        {
            gm_l13 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t46 = gm_l13;
                uint64_t t47 = GM_LD(uintptr_t, ((uint8_t *)&gm_l12) + 8);
                uint8_t t48 = (t46 < t47);
                if (!t48) goto lb45;
                {
                    uint64_t t51 = gm_l13;
                    uint64_t t52 = (uintptr_t)t51;
                    uint64_t t53 = GM_LD(uintptr_t, ((uint8_t *)&gm_l12) + 8);
                    uint32_t t54 = GM_LD(uint32_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l12)) + t52 * 4));
                    uint32_t t55 = t54;
                    uint32_t t58 = GM_LD(uint32_t, ((uint8_t *)&t55));
                    gm_l14 = t58;
                    {
                        uint8_t t61 = gm_l6;
                        if (!t61) goto ie60;
                        gm_b16_8 t62 = gm_l2;
                        uint32_t t63 = gm_l14;
                        gm_tkn42u7na3t81 t64 = gm_f_bound_to_5c11t0h(t62, t63);
                        gm_tkn42u7na3t81 t65 = t64;
                        uint8_t t66;
                        if (((((uint8_t *)&t65))[4] != 0)) goto mn68;
                        t66 = ((uint8_t)1);
                        goto me67;
mn68:;
                        t66 = ((uint8_t)0);
                        goto me67;
mn69:;
                        gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 344, 27);
me67:;
                        if (!t66) goto ie60;
                        {
                            gm_th2lqbo77pvsc t71;
                            memset(&t71, 0, sizeof t71);
                            uint8_t t72 = 0;
                            F.ret = t71;
                            goto gm_fret;
                        }
                        goto ix60;
ie60:;
ix60:;
                    }
                    {
                        void * t74 = gm_l0;
                        void * t75 = GM_LD(void *, (((uint8_t *)t74) + 0));
                        uint32_t t76 = gm_l14;
                        gm_tnnjeerolos0a t77 = gm_f_param_kind_a0aikmb(t75, t76);
                        gm_tnnjeerolos0a t78 = t77;
                        uint8_t t79 = GM_LD(uint8_t, (((uint8_t *)&t78) + 0));
                        if (t79 != ((uint8_t)2ULL)) goto ie73;
                        gm_t88rcucg1b7uk t80 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t78) + 4));
                        gm_l15 = t80;
                        {
                            gm_b16_8 t83 = gm_l2;
                            uint32_t t84 = gm_l14;
                            gm_tkn42u7na3t81 t85 = gm_f_bound_to_5c11t0h(t83, t84);
                            gm_tkn42u7na3t81 t86 = t85;
                            if (!((((uint8_t *)&t86))[4] != 0)) goto ie82;
                            gm_t88rcucg1b7uk t87 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&t86));
                            gm_l16 = t87;
                            {
                                void * t89 = (void *)((uint8_t *)&F.l11);
                                void * t90 = gm_l0;
                                gm_t88rcucg1b7uk t91 = gm_l16;
                                gm_t88rcucg1b7uk t92 = gm_l15;
                                void * t93 = gm_l5;
                                uint32_t t94 = gm_l14;
                                gm_b16_8 t95 = gm_f_param_name_rbnop5l(t93, t94);
                                gm_tl874t2atnlh3 t96 = gm_l4;
                                gm_tkn42u7na3t81 t97 = gm_f_normalize_value_fgo7ofj((void *)NULL, cx, NULL, t90, t91, t92, t95, t96);
                                if (gm_unw.kind) {
                                    goto gm_ll49;
                                }
                                gm_tkn42u7na3t81 t98 = t97;
                                gm_t88rcucg1b7uk t99;
                                if (((((uint8_t *)&t98))[4] != 0)) {
                                    gm_t88rcucg1b7uk t100 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t98) + 0));
                                    t99 = t100;
                                } else {
                                    gm_th2lqbo77pvsc t101;
                                    memset(&t101, 0, sizeof t101);
                                    uint8_t t102 = 0;
                                    F.ret = t101;
                                    goto gm_fret;
                                }
                                gm_tftb1odc739t5 t103 = gm_f_push_k3l9nic(t89, GM_CONV(gm_csguankko4lmhd, t99));
                                gm_tftb1odc739t5 t104 = t103;
                                if (((((uint8_t *)&t104))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t104) + 1, gm_srcrorgjlg51brd, 347, 17);
                                gm_b0_1 t105 = GM_LD(gm_b0_1, (((uint8_t *)&t104) + 1));
                            }
                            {
                                goto lc45;
                            }
                            goto ix82;
ie82:;
ix82:;
                        }
                        {
                            {
                                void * t107 = gm_l1;
                                gm_b16_8 t108 = GM_LD(gm_b16_8, (((uint8_t *)t107) + 40));
                                gm_l17 = t108;
                            }
                            {
                                gm_l18 = ((uint64_t)0ULL);
                            }
                            {
                                for (;;) {
                                    uint64_t t110 = gm_l18;
                                    uint64_t t111 = GM_LD(uintptr_t, ((uint8_t *)&gm_l17) + 8);
                                    uint8_t t112 = (t110 < t111);
                                    if (!t112) goto lb109;
                                    {
                                        uint64_t t115 = gm_l18;
                                        uint64_t t116 = (uintptr_t)t115;
                                        uint64_t t117 = GM_LD(uintptr_t, ((uint8_t *)&gm_l17) + 8);
                                        gm_t5v0c8ia6a5mo t120 = GM_LD(gm_t5v0c8ia6a5mo, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l17)) + t116 * 56));
                                        F.l19 = t120;
                                        {
                                            gm_t9805qtonbsag t123 = GM_LD(gm_t9805qtonbsag, (((uint8_t *)&F.l19) + 40));
                                            uint32_t t124 = gm_l14;
                                            gm_t9805qtonbsag t125;
                                            GM_ST(uint8_t, (uint8_t *)&t125 + 5, 0);
                                            GM_ST(uint16_t, (uint8_t *)&t125 + 6, 0);
                                            GM_ST(uint32_t, ((uint8_t *)&t125), t124);
                                            ((uint8_t *)&t125)[4] = 1;
                                            gm_t9805qtonbsag t126 = t123;
                                            gm_t9805qtonbsag t127 = t125;
                                            uint8_t t128 = (!gm_eqkricceou4u(((uint8_t *)&t126), ((uint8_t *)&t127)));
                                            if (!t128) goto ie122;
                                            {
                                                goto lc109;
                                            }
                                            goto ix122;
ie122:;
ix122:;
                                        }
                                        {
                                            void * t130 = GM_LD(void *, (((uint8_t *)&F.l19) + 32));
                                            void * t131 = t130;
                                            void * t132;
                                            if ((GM_LD(void *, ((uint8_t *)&t131)) != 0)) {
                                                void * t133 = GM_LD(void *, (((uint8_t *)&t131) + 0));
                                                t132 = t133;
                                            } else {
                                                {
                                                    void * t135 = gm_l0;
                                                    gm_tl874t2atnlh3 t136 = gm_l4;
                                                    gm_b16_8 t137 = GM_LD(gm_b16_8, (((uint8_t *)&F.l19) + 0));
                                                    gm_tnpifv20irnph t138;
                                                    GM_ST(gm_b16_8, ((uint8_t *)&t138 + 0), t137);
                                                    gm_tabb710d9mq2e t139;
                                                    gm_f_format_qv493su((void *)&t139, (void *)NULL, cx, NULL, t138);
                                                    if (gm_unw.kind) {
                                                        goto gm_ll113;
                                                    }
                                                    gm_tabb710d9mq2e t140 = t139;
                                                    if (((((uint8_t *)&t140))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t140) + 8, gm_srcrorgjlg51brd, 353, 36);
                                                    gm_b16_8 t141 = GM_LD(gm_b16_8, (((uint8_t *)&t140) + 8));
                                                    gm_b16_8 t142 = t141;
                                                    gm_b16_8 t143 = GM_LD(gm_b16_8, ((uint8_t *)&t142));
                                                    gm_b0_1 t144 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t135, t136, t143);
                                                    if (gm_unw.kind) {
                                                        goto gm_ll113;
                                                    }
                                                }
                                                gm_th2lqbo77pvsc t145;
                                                memset(&t145, 0, sizeof t145);
                                                uint8_t t146 = 0;
                                                F.ret = t145;
                                                goto gm_fret;
                                            }
                                            gm_l20 = t132;
                                        }
                                        {
                                            void * t147 = (void *)((uint8_t *)&F.l11);
                                            void * t148 = gm_l0;
                                            gm_t88rcucg1b7uk t149 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l19) + 16));
                                            void * t150 = gm_l20;
                                            gm_tkn42u7na3t81 t151 = gm_f_comptime_arg_vtq6sop((void *)NULL, cx, NULL, t148, t149, t150);
                                            if (gm_unw.kind) {
                                                goto gm_ll113;
                                            }
                                            gm_tkn42u7na3t81 t152 = t151;
                                            gm_t88rcucg1b7uk t153;
                                            if (((((uint8_t *)&t152))[4] != 0)) {
                                                gm_t88rcucg1b7uk t154 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t152) + 0));
                                                t153 = t154;
                                            } else {
                                                gm_th2lqbo77pvsc t155;
                                                memset(&t155, 0, sizeof t155);
                                                uint8_t t156 = 0;
                                                F.ret = t155;
                                                goto gm_fret;
                                            }
                                            gm_tftb1odc739t5 t157 = gm_f_push_k3l9nic(t147, GM_CONV(gm_csguankko4lmhd, t153));
                                            gm_tftb1odc739t5 t158 = t157;
                                            if (((((uint8_t *)&t158))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t158) + 1, gm_srcrorgjlg51brd, 356, 17);
                                            gm_b0_1 t159 = GM_LD(gm_b0_1, (((uint8_t *)&t158) + 1));
                                        }
                                        {
                                            goto lb109;
                                        }
                                        goto me118;
mn119:;
                                        gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 350, 13);
me118:;
                                    }
lc109:;
                                    {
                                        uint64_t t161 = gm_l18;
                                        uint64_t r162; bool o163 = __builtin_add_overflow(t161, ((uint64_t)1ULL), &r162);
                                        uint64_t t164 = r162;
                                        gm_l18 = t164;
                                    }
                                }
gm_ll113:;
                                if (gm_unw.frame == &F.h && gm_unw.label == 2) {
                                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb109; }
                                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc109; }
                                }
                                goto gm_ll49;
lb109:;
                            }
                        }
                        {
                            goto lc45;
                        }
                        goto ix73;
ie73:;
ix73:;
                    }
                    {
                        gm_b16_8 t166 = gm_l2;
                        uint32_t t167 = gm_l14;
                        gm_tkn42u7na3t81 t168 = gm_f_bound_to_5c11t0h(t166, t167);
                        gm_tkn42u7na3t81 t169 = t168;
                        if (!((((uint8_t *)&t169))[4] != 0)) goto ie165;
                        gm_t88rcucg1b7uk t170 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&t169));
                        gm_l21 = t170;
                        {
                            void * t172 = (void *)((uint8_t *)&F.l11);
                            gm_t88rcucg1b7uk t173 = gm_l21;
                            gm_tftb1odc739t5 t174 = gm_f_push_k3l9nic(t172, GM_CONV(gm_csguankko4lmhd, t173));
                            gm_tftb1odc739t5 t175 = t174;
                            if (((((uint8_t *)&t175))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t175) + 1, gm_srcrorgjlg51brd, 362, 13);
                            gm_b0_1 t176 = GM_LD(gm_b0_1, (((uint8_t *)&t175) + 1));
                        }
                        {
                            goto lc45;
                        }
                        goto ix165;
ie165:;
ix165:;
                    }
                    {
                        void * t178 = gm_l0;
                        void * t179 = GM_LD(void *, (((uint8_t *)t178) + 0));
                        uint32_t t180 = gm_l14;
                        gm_tnnjeerolos0a t181 = gm_f_param_kind_a0aikmb(t179, t180);
                        gm_tnnjeerolos0a t182 = t181;
                        uint8_t t183 = ((GM_LD(uint8_t, ((uint8_t *)&t182) + 0) == ((uint8_t)1ULL)));
                        if (!t183) goto ie177;
                        {
                            void * t185 = (void *)((uint8_t *)&F.l11);
                            void * t186 = gm_l5;
                            gm_tnpifv20irnph t187 = gm_f_empty_effects_r0iv3pv();
                            gm_t53at80pfdus3 t188;
                            memset(&t188, 0, sizeof t188);
                            GM_ST(uint8_t, ((uint8_t *)&t188 + 0), ((uint8_t)24ULL));
                            GM_ST(gm_tnpifv20irnph, ((uint8_t *)&t188 + 8), t187);
                            gm_t88rcucg1b7uk t189 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t186, &t188);
                            if (gm_unw.kind) {
                                goto gm_ll49;
                            }
                            gm_tftb1odc739t5 t190 = gm_f_push_k3l9nic(t185, GM_CONV(gm_csguankko4lmhd, t189));
                            gm_tftb1odc739t5 t191 = t190;
                            if (((((uint8_t *)&t191))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t191) + 1, gm_srcrorgjlg51brd, 367, 13);
                            gm_b0_1 t192 = GM_LD(gm_b0_1, (((uint8_t *)&t191) + 1));
                        }
                        {
                            goto lc45;
                        }
                        goto ix177;
ie177:;
ix177:;
                    }
                    {
                        void * t193 = gm_l0;
                        void * t194 = gm_l1;
                        uint32_t t195 = gm_l14;
                        gm_b16_8 t196 = gm_l3;
                        gm_tl874t2atnlh3 t197 = gm_l4;
                        gm_b0_1 t198 = gm_f_report_cannot_infer_tor1q4u((void *)NULL, cx, NULL, t193, t194, t195, t196, t197);
                        if (gm_unw.kind) {
                            goto gm_ll49;
                        }
                    }
                    {
                        gm_th2lqbo77pvsc t199;
                        memset(&t199, 0, sizeof t199);
                        uint8_t t200 = 0;
                        F.ret = t199;
                        goto gm_fret;
                    }
                    goto me56;
mn57:;
                    gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 343, 5);
me56:;
                }
lc45:;
                {
                    uint64_t t202 = gm_l13;
                    uint64_t r203; bool o204 = __builtin_add_overflow(t202, ((uint64_t)1ULL), &r203);
                    uint64_t t205 = r203;
                    gm_l13 = t205;
                }
            }
gm_ll49:;
            if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb45; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc45; }
            }
            goto gm_lad;
lb45:;
        }
    }
    {
        gm_b16_8 t206 = GM_LD(gm_b16_8, (((uint8_t *)&F.l11) + 0));
        gm_th2lqbo77pvsc t207;
        GM_ST(uint8_t, (uint8_t *)&t207 + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&t207 + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&t207 + 20, 0);
        GM_ST(gm_b16_8, ((uint8_t *)&t207), t206);
        ((uint8_t *)&t207)[16] = 1;
        uint8_t t208 = 0;
        F.ret = t207;
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
inline __attribute__((aligned(4))) uint8_t gm_f_given_by_name_pd5ma4t(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_given_by_name_pd5ma4t F;
    void * gm_l0; /* p */
    gm_b16_8 gm_l1; /* <tmp> */
    uint64_t gm_l2; /* <tmp> */
    gm_t08olq97rscn3 gm_l4; /* n */
    GM_STACK_CHECK(&F, 439u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        {
            gm_b16_8 t3 = GM_LD(gm_b16_8, ((uint8_t *)&((struct gm_fr_is_instantiation_vgsqc8f *)((&F.h)->up))->l2));
            gm_l1 = t3;
        }
        {
            gm_l2 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t5 = gm_l2;
                uint64_t t6 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                uint8_t t7 = (t5 < t6);
                if (!t7) goto lb4;
                {
                    uint64_t t10 = gm_l2;
                    uint64_t t11 = (uintptr_t)t10;
                    uint64_t t12 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                    gm_tpff59ptr3s92 t15 = GM_LD(gm_tpff59ptr3s92, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t11 * 192));
                    F.l3 = t15;
                    {
                        uint8_t t18 = GM_LD(uint8_t, ((((uint8_t *)&F.l3) + 0) + 0));
                        if (t18 != ((uint8_t)1ULL)) goto ie17;
                        gm_t08olq97rscn3 t19 = GM_LD(gm_t08olq97rscn3, (((((uint8_t *)&F.l3) + 0) + 8) + 0));
                        gm_l4 = t19;
                        gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l4) + 0));
                        void * t21 = gm_l0;
                        gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)t21) + 0));
                        gm_b16_8 t23 = t20;
                        gm_b16_8 t24 = t22;
                        uint8_t t25 = (gm_eq0iqiubfm7e(((uint8_t *)&t23), ((uint8_t *)&t24)));
                        if (!t25) goto ie17;
                        {
                            uint8_t t27 = 0;
                            F.ret = ((uint8_t)1);
                            goto gm_fret;
                        }
                        goto ix17;
ie17:;
ix17:;
                    }
                    goto me13;
mn14:;
                    gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 613, 9);
me13:;
                }
lc4:;
                {
                    uint64_t t29 = gm_l2;
                    uint64_t r30; bool o31 = __builtin_add_overflow(t29, ((uint64_t)1ULL), &r30);
                    uint64_t t32 = r30;
                    gm_l2 = t32;
                }
            }
lb4:;
        }
    }
    {
        uint8_t t33 = 0;
        F.ret = ((uint8_t)0);
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) uint8_t gm_f_inferable_param_f80lrdi(void * a0, void * a1, uint64_t a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_inferable_param_f80lrdi F;
    void * gm_l1; /* sig */
    uint64_t gm_l2; /* i */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    gm_tiilmfd16v3fc gm_l6; /* e */
    gm_b16_8 gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    uint64_t gm_l9; /* j */
    GM_STACK_CHECK(&F, 688u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        uint64_t t2 = gm_l2;
        uint64_t t3 = (uintptr_t)t2;
        void * t4 = gm_l1;
        uint64_t t5 = GM_LD(uintptr_t, (((uint8_t *)t4) + 40) + 8);
        if (t3 >= (uintptr_t)(t5)) gm_trap_index(t3, t5, gm_srcrorgjlg51brd, 568, 12);
        gm_t9805qtonbsag t6 = GM_LD(gm_t9805qtonbsag, (((uint8_t *)GM_LD(void *, (((uint8_t *)t4) + 40)) + t3 * 56) + 40));
        gm_t9805qtonbsag t7 = t6;
        uint32_t t8;
        if (((((uint8_t *)&t7))[4] != 0)) {
            uint32_t t9 = GM_LD(uint32_t, (((uint8_t *)&t7) + 0));
            t8 = t9;
        } else {
            uint8_t t10 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        F.l3 = t8;
    }
    {
        void * t12 = F.l0;
        uint32_t t13 = F.l3;
        gm_tnnjeerolos0a t14 = gm_f_param_kind_a0aikmb(t12, t13);
        gm_tnnjeerolos0a t15 = t14;
        uint8_t t16 = ((GM_LD(uint8_t, ((uint8_t *)&t15) + 0) == ((uint8_t)1ULL)));
        if (!t16) goto ie11;
        {
            {
                void * t19 = gm_l1;
                gm_b16_8 t20 = GM_LD(gm_b16_8, ((((uint8_t *)t19) + 64) + 0));
                gm_l4 = t20;
            }
            {
                gm_l5 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t22 = gm_l5;
                    uint64_t t23 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                    uint8_t t24 = (t22 < t23);
                    if (!t24) goto lb21;
                    {
                        uint64_t t27 = gm_l5;
                        uint64_t t28 = (uintptr_t)t27;
                        uint64_t t29 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                        gm_tiilmfd16v3fc t32 = GM_LD(gm_tiilmfd16v3fc, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t28 * 24));
                        gm_l6 = t32;
                        {
                            void * t35 = F.l0;
                            void * t36 = (void *)((uint8_t *)&gm_l6);
                            uint32_t t37 = F.l3;
                            uint8_t t38 = gm_f_effect_args_name_jgklot1(t35, t36, t37);
                            if (!t38) goto ie34;
                            {
                                uint8_t t40 = 0;
                                F.ret = ((uint8_t)1);
                                goto gm_fret;
                            }
                            goto ix34;
ie34:;
ix34:;
                        }
                        goto me30;
mn31:;
                        gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 570, 9);
me30:;
                    }
lc21:;
                    {
                        uint64_t t42 = gm_l5;
                        uint64_t r43; bool o44 = __builtin_add_overflow(t42, ((uint64_t)1ULL), &r43);
                        uint64_t t45 = r43;
                        gm_l5 = t45;
                    }
                }
lb21:;
            }
        }
        goto ix11;
ie11:;
ix11:;
    }
    {
        {
            void * t47 = gm_l1;
            gm_b16_8 t48 = GM_LD(gm_b16_8, (((uint8_t *)t47) + 40));
            gm_b16_8 t49 = t48;
            gm_b16_8 t50 = GM_LD(gm_b16_8, ((uint8_t *)&t49));
            gm_l7 = t50;
        }
        {
            gm_l8 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t52 = gm_l8;
                uint64_t t53 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                uint8_t t54 = (t52 < t53);
                if (!t54) goto lb51;
                {
                    gm_tgsvmddj3e85e t57;
                    uint64_t t58 = gm_l8;
                    GM_ST(uint64_t, (uint8_t *)&t57 + 0, t58);
                    uint64_t t59 = gm_l8;
                    uint64_t t60 = (uintptr_t)t59;
                    uint64_t t61 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                    memcpy((uint8_t *)&t57 + 8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l7)) + t60 * 56), 56);
                    gm_tgsvmddj3e85e t62 = t57;
                    uint64_t t65 = GM_LD(uint64_t, (((uint8_t *)&t62) + 0));
                    gm_l9 = t65;
                    gm_t5v0c8ia6a5mo t66 = GM_LD(gm_t5v0c8ia6a5mo, (((uint8_t *)&t62) + 8));
                    F.l10 = t66;
                    {
                        uint64_t t69 = gm_l9;
                        uint64_t t70 = gm_l2;
                        uint8_t t71 = ((t69 == t70));
                        if (!t71) goto ie68;
                        {
                            goto lc51;
                        }
                        goto ix68;
ie68:;
ix68:;
                    }
                    {
                        F.l11 = ((uint32_t)0ULL);
                    }
                    {
                        F.l12 = ((uint32_t)0ULL);
                    }
                    {
                        void * t73 = F.l0;
                        gm_t88rcucg1b7uk t74 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l10) + 16));
                        gm_closure t75 = ((gm_closure){(void *)gm_f_fn_qtpj77g, (void *)&F.h, cx});
                        gm_f_walk_v858co7(t73, GM_CONV(gm_csguankko4lmhd, t74), t75);
                        if (gm_unw.kind) {
                            goto gm_ll55;
                        }
                    }
                    {
                        uint32_t t77 = F.l11;
                        uint32_t t78 = F.l12;
                        uint8_t t79 = (t77 > t78);
                        if (!t79) goto ie76;
                        {
                            uint8_t t81 = 0;
                            F.ret = ((uint8_t)1);
                            goto gm_fret;
                        }
                        goto ix76;
ie76:;
ix76:;
                    }
                    goto me63;
mn64:;
                    gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 574, 5);
me63:;
                }
lc51:;
                {
                    uint64_t t83 = gm_l8;
                    uint64_t r84; bool o85 = __builtin_add_overflow(t83, ((uint64_t)1ULL), &r84);
                    uint64_t t86 = r84;
                    gm_l8 = t86;
                }
            }
gm_ll55:;
            if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb51; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc51; }
            }
            goto gm_lad;
lb51:;
        }
    }
    {
        uint8_t t87 = 0;
        F.ret = ((uint8_t)0);
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
__attribute__((aligned(4))) uint8_t gm_f_is_instantiation_vgsqc8f(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_b16_8 a2, gm_b16_8 a3) {
    struct gm_fr_is_instantiation_vgsqc8f F;
    void * gm_l0; /* cx */
    void * gm_l1; /* sig */
    gm_b16_8 gm_l3; /* positional */
    gm_closure gm_l4; /* given_by_name */
    uint8_t gm_l5; /* needs_runtime */
    gm_b16_8 gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    gm_b16_8 gm_l10; /* <tmp> */
    uint64_t gm_l11; /* <tmp> */
    uint64_t gm_l12; /* i */
    gm_b16_8 gm_l14; /* <tmp> */
    uint64_t gm_l15; /* <tmp> */
    uint64_t gm_l16; /* i */
    gm_b16_8 gm_l17; /* <tmp> */
    uint64_t gm_l18; /* <tmp> */
    uint64_t gm_l19; /* i */
    gm_b16_8 gm_l21; /* <tmp> */
    uint64_t gm_l22; /* <tmp> */
    uint64_t gm_l23; /* k */
    uint32_t gm_l26; /* g */
    uint8_t gm_l27; /* is_value */
    GM_STACK_CHECK(&F, 2282u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    F.l2 = a2;
    gm_l3 = a3;
    {
        gm_closure t2 = ((gm_closure){(void *)gm_f_given_by_name_pd5ma4t, (void *)&F.h, cx});
        gm_l4 = t2;
    }
    {
        gm_l5 = ((uint8_t)0);
    }
    {
        {
            void * t4 = gm_l1;
            gm_b16_8 t5 = GM_LD(gm_b16_8, (((uint8_t *)t4) + 40));
            gm_l6 = t5;
        }
        {
            gm_l7 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t7 = gm_l7;
                uint64_t t8 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                uint8_t t9 = (t7 < t8);
                if (!t9) goto lb6;
                {
                    uint64_t t12 = gm_l7;
                    uint64_t t13 = (uintptr_t)t12;
                    uint64_t t14 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                    gm_t5v0c8ia6a5mo t17 = GM_LD(gm_t5v0c8ia6a5mo, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t13 * 56));
                    F.l8 = t17;
                    {
                        uint8_t t20;
                        if ((((((uint8_t *)&F.l8) + 40))[4] != 0)) goto mn22;
                        t20 = ((uint8_t)1);
                        goto me21;
mn22:;
                        t20 = ((uint8_t)0);
                        goto me21;
mn23:;
                        gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 620, 12);
me21:;
                        if (!t20) goto ie19;
                        gm_closure t24 = gm_l4;
                        void * t25 = (void *)((uint8_t *)&F.l8);
                        gm_closure t26 = t24;
                        gm_node *ccx27 = t26.cx;
                        void * t28 = t26.code;
                        uint8_t t29;
                        if (GM_PTR_TAGGED(t28)) {
                            t29 = ((uint8_t (*)(void *))GM_PTR_UNTAG(t28))(t25);
                        } else {
                            t29 = ((uint8_t (*)(void *, gm_node *, gm_node *, void *))t28)(t26.env, ccx27, NULL, t25);
                        }
                        if (gm_unw.kind) {
                            goto gm_ll10;
                        }
                        if (!t29) goto ie19;
                        {
                            uint8_t t31 = 0;
                            F.ret = ((uint8_t)0);
                            goto gm_fret;
                        }
                        goto ix19;
ie19:;
ix19:;
                    }
                    {
                        uint8_t t33;
                        if ((((((uint8_t *)&F.l8) + 40))[4] != 0)) goto mn35;
                        t33 = ((uint8_t)1);
                        goto me34;
mn35:;
                        t33 = ((uint8_t)0);
                        goto me34;
mn36:;
                        gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 621, 12);
me34:;
                        if (!t33) goto ie32;
                        void * t37 = GM_LD(void *, (((uint8_t *)&F.l8) + 32));
                        void * t38 = t37;
                        uint8_t t39;
                        if ((GM_LD(void *, ((uint8_t *)&t38)) != 0)) goto mn41;
                        t39 = ((uint8_t)1);
                        goto me40;
mn41:;
                        t39 = ((uint8_t)0);
                        goto me40;
mn42:;
                        gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 621, 34);
me40:;
                        if (!t39) goto ie32;
                        void * t43 = (void *)((uint8_t *)&F.l8);
                        uint8_t t44 = gm_f_is_site_8sr7vam(t43);
                        uint8_t t45 = (uint8_t)!t44;
                        if (!t45) goto ie32;
                        {
                            gm_l5 = ((uint8_t)1);
                        }
                        goto ix32;
ie32:;
ix32:;
                    }
                    goto me15;
mn16:;
                    gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 619, 5);
me15:;
                }
lc6:;
                {
                    uint64_t t48 = gm_l7;
                    uint64_t r49; bool o50 = __builtin_add_overflow(t48, ((uint64_t)1ULL), &r49);
                    uint64_t t51 = r49;
                    gm_l7 = t51;
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
    {
        uint8_t t53 = 0;
        uint8_t t54 = gm_l5;
        uint8_t t55 = (uint8_t)!t54;
        if (!t55) {
            uint64_t t56 = GM_LD(uintptr_t, ((uint8_t *)&F.l2) + 8);
            uint8_t t57 = ((t56 == ((uint64_t)0ULL)));
            t53 = t57;
        } else t53 = 1;
        if (!t53) goto ie52;
        {
            uint8_t t59 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        goto ix52;
ie52:;
ix52:;
    }
    {
        gm_f_list_5dhgg4i((void *)&F.l9, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            void * t61 = gm_l1;
            gm_b16_8 t62 = GM_LD(gm_b16_8, (((uint8_t *)t61) + 40));
            gm_b16_8 t63 = t62;
            gm_b16_8 t64 = GM_LD(gm_b16_8, ((uint8_t *)&t63));
            gm_l10 = t64;
        }
        {
            gm_l11 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t66 = gm_l11;
                uint64_t t67 = GM_LD(uintptr_t, ((uint8_t *)&gm_l10) + 8);
                uint8_t t68 = (t66 < t67);
                if (!t68) goto lb65;
                {
                    gm_tgsvmddj3e85e t71;
                    uint64_t t72 = gm_l11;
                    GM_ST(uint64_t, (uint8_t *)&t71 + 0, t72);
                    uint64_t t73 = gm_l11;
                    uint64_t t74 = (uintptr_t)t73;
                    uint64_t t75 = GM_LD(uintptr_t, ((uint8_t *)&gm_l10) + 8);
                    memcpy((uint8_t *)&t71 + 8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l10)) + t74 * 56), 56);
                    gm_tgsvmddj3e85e t76 = t71;
                    uint64_t t79 = GM_LD(uint64_t, (((uint8_t *)&t76) + 0));
                    gm_l12 = t79;
                    gm_t5v0c8ia6a5mo t80 = GM_LD(gm_t5v0c8ia6a5mo, (((uint8_t *)&t76) + 8));
                    F.l13 = t80;
                    {
                        void * t83 = gm_l0;
                        void * t84 = GM_LD(void *, (((uint8_t *)t83) + 0));
                        void * t85 = gm_l1;
                        uint64_t t86 = gm_l12;
                        uint8_t t87 = gm_f_inferable_param_f80lrdi(t84, t85, t86);
                        uint8_t t88 = (uint8_t)!t87;
                        if (!t88) goto ie82;
                        gm_closure t89 = gm_l4;
                        void * t90 = (void *)((uint8_t *)&F.l13);
                        gm_closure t91 = t89;
                        gm_node *ccx92 = t91.cx;
                        void * t93 = t91.code;
                        uint8_t t94;
                        if (GM_PTR_TAGGED(t93)) {
                            t94 = ((uint8_t (*)(void *))GM_PTR_UNTAG(t93))(t90);
                        } else {
                            t94 = ((uint8_t (*)(void *, gm_node *, gm_node *, void *))t93)(t91.env, ccx92, NULL, t90);
                        }
                        if (gm_unw.kind) {
                            goto gm_ll69;
                        }
                        uint8_t t95 = (uint8_t)!t94;
                        if (!t95) goto ie82;
                        void * t96 = (void *)((uint8_t *)&F.l13);
                        uint8_t t97 = gm_f_is_site_8sr7vam(t96);
                        uint8_t t98 = (uint8_t)!t97;
                        if (!t98) goto ie82;
                        {
                            void * t100 = (void *)((uint8_t *)&F.l9);
                            uint64_t t101 = gm_l12;
                            gm_tftb1odc739t5 t102 = gm_f_push_ucgmlo2(t100, t101);
                            gm_tftb1odc739t5 t103 = t102;
                            if (((((uint8_t *)&t103))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t103) + 1, gm_srcrorgjlg51brd, 628, 99);
                            gm_b0_1 t104 = GM_LD(gm_b0_1, (((uint8_t *)&t103) + 1));
                        }
                        goto ix82;
ie82:;
ix82:;
                    }
                    goto me77;
mn78:;
                    gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 627, 5);
me77:;
                }
lc65:;
                {
                    uint64_t t106 = gm_l11;
                    uint64_t r107; bool o108 = __builtin_add_overflow(t106, ((uint64_t)1ULL), &r107);
                    uint64_t t109 = r107;
                    gm_l11 = t109;
                }
            }
gm_ll69:;
            if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb65; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc65; }
            }
            goto gm_lad;
lb65:;
        }
    }
    {
        uint64_t t111 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
        uint64_t t112 = GM_LD(uintptr_t, (((uint8_t *)&F.l9) + 0) + 8);
        uint8_t t113 = (t111 > t112);
        if (!t113) goto ie110;
        {
            uint8_t t115 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        goto ix110;
ie110:;
ix110:;
    }
    {
        {
            uint64_t t117 = GM_LD(uintptr_t, (((uint8_t *)&F.l9) + 0) + 8);
            void * t118 = ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l9) + 0)));
            uint64_t t119 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
            uint64_t t120 = (uintptr_t)t119;
            if ((uintptr_t)0 > t120 || t120 > (uintptr_t)t117) gm_trap_slice((uintptr_t)0, t120, t117, gm_srcrorgjlg51brd, 631, 14);
            gm_b16_8 t121;
            GM_ST(void *, (uint8_t *)&t121, (void *)(((uint8_t *)t118 + (uintptr_t)0 * 8)));
            GM_ST(uintptr_t, (uint8_t *)&t121 + 8, (uintptr_t)(t120 - (uintptr_t)0));
            gm_l14 = t121;
        }
        {
            gm_l15 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t123 = gm_l15;
                uint64_t t124 = GM_LD(uintptr_t, ((uint8_t *)&gm_l14) + 8);
                uint8_t t125 = (t123 < t124);
                if (!t125) goto lb122;
                {
                    uint64_t t128 = gm_l15;
                    uint64_t t129 = (uintptr_t)t128;
                    uint64_t t130 = GM_LD(uintptr_t, ((uint8_t *)&gm_l14) + 8);
                    uint64_t t131 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l14)) + t129 * 8));
                    uint64_t t132 = t131;
                    uint64_t t135 = GM_LD(uint64_t, ((uint8_t *)&t132));
                    gm_l16 = t135;
                    {
                        uint64_t t138 = gm_l16;
                        uint64_t t139 = (uintptr_t)t138;
                        void * t140 = gm_l1;
                        uint64_t t141 = GM_LD(uintptr_t, (((uint8_t *)t140) + 40) + 8);
                        if (t139 >= (uintptr_t)(t141)) gm_trap_index(t139, t141, gm_srcrorgjlg51brd, 632, 12);
                        uint8_t t142;
                        if ((((((uint8_t *)GM_LD(void *, (((uint8_t *)t140) + 40)) + t139 * 56) + 40))[4] != 0)) goto mn144;
                        t142 = ((uint8_t)1);
                        goto me143;
mn144:;
                        t142 = ((uint8_t)0);
                        goto me143;
mn145:;
                        gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 632, 12);
me143:;
                        if (!t142) goto ie137;
                        {
                            uint8_t t147 = 0;
                            F.ret = ((uint8_t)0);
                            goto gm_fret;
                        }
                        goto ix137;
ie137:;
ix137:;
                    }
                    goto me133;
mn134:;
                    gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 631, 5);
me133:;
                }
lc122:;
                {
                    uint64_t t149 = gm_l15;
                    uint64_t r150; bool o151 = __builtin_add_overflow(t149, ((uint64_t)1ULL), &r150);
                    uint64_t t152 = r150;
                    gm_l15 = t152;
                }
            }
lb122:;
        }
    }
    {
        {
            void * t154 = gm_l1;
            gm_b16_8 t155 = GM_LD(gm_b16_8, (((uint8_t *)t154) + 40));
            gm_b16_8 t156 = t155;
            gm_b16_8 t157 = GM_LD(gm_b16_8, ((uint8_t *)&t156));
            gm_l17 = t157;
        }
        {
            gm_l18 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t159 = gm_l18;
                uint64_t t160 = GM_LD(uintptr_t, ((uint8_t *)&gm_l17) + 8);
                uint8_t t161 = (t159 < t160);
                if (!t161) goto lb158;
                {
                    gm_tgsvmddj3e85e t164;
                    uint64_t t165 = gm_l18;
                    GM_ST(uint64_t, (uint8_t *)&t164 + 0, t165);
                    uint64_t t166 = gm_l18;
                    uint64_t t167 = (uintptr_t)t166;
                    uint64_t t168 = GM_LD(uintptr_t, ((uint8_t *)&gm_l17) + 8);
                    memcpy((uint8_t *)&t164 + 8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l17)) + t167 * 56), 56);
                    gm_tgsvmddj3e85e t169 = t164;
                    uint64_t t172 = GM_LD(uint64_t, (((uint8_t *)&t169) + 0));
                    gm_l19 = t172;
                    gm_t5v0c8ia6a5mo t173 = GM_LD(gm_t5v0c8ia6a5mo, (((uint8_t *)&t169) + 8));
                    F.l20 = t173;
                    {
                        uint8_t t176;
                        if ((((((uint8_t *)&F.l20) + 40))[4] != 0)) goto mn178;
                        t176 = ((uint8_t)0);
                        goto me177;
mn178:;
                        t176 = ((uint8_t)1);
                        goto me177;
mn179:;
                        gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 635, 12);
me177:;
                        if (!t176) goto ie175;
                        gm_closure t180 = gm_l4;
                        void * t181 = (void *)((uint8_t *)&F.l20);
                        gm_closure t182 = t180;
                        gm_node *ccx183 = t182.cx;
                        void * t184 = t182.code;
                        uint8_t t185;
                        if (GM_PTR_TAGGED(t184)) {
                            t185 = ((uint8_t (*)(void *))GM_PTR_UNTAG(t184))(t181);
                        } else {
                            t185 = ((uint8_t (*)(void *, gm_node *, gm_node *, void *))t184)(t182.env, ccx183, NULL, t181);
                        }
                        if (gm_unw.kind) {
                            goto gm_ll162;
                        }
                        uint8_t t186 = (uint8_t)!t185;
                        if (!t186) goto ie175;
                        uint64_t t187 = gm_l19;
                        uint64_t t188 = GM_LD(uintptr_t, (((uint8_t *)&F.l9) + 0) + 8);
                        void * t189 = ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l9) + 0)));
                        uint64_t t190 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                        uint64_t t191 = (uintptr_t)t190;
                        if ((uintptr_t)0 > t191 || t191 > (uintptr_t)t188) gm_trap_slice((uintptr_t)0, t191, t188, gm_srcrorgjlg51brd, 635, 73);
                        gm_b16_8 t192;
                        GM_ST(void *, (uint8_t *)&t192, (void *)(((uint8_t *)t189 + (uintptr_t)0 * 8)));
                        GM_ST(uintptr_t, (uint8_t *)&t192 + 8, (uintptr_t)(t191 - (uintptr_t)0));
                        gm_b16_8 t193 = t192;
                        uint8_t t194 = 0;
                        uint64_t t195 = t187;
                        for (uintptr_t i = 0; i < (uintptr_t)(GM_LD(uintptr_t, ((uint8_t *)&t193) + 8)); i++) if (gm_eq6groq37ram(((uint8_t *)GM_LD(void *, ((uint8_t *)&t193))) + i * 8, ((uint8_t *)&t195))) { t194 = 1; break; }
                        uint8_t t196 = (uint8_t)(!t194);
                        if (!t196) goto ie175;
                        {
                            uint8_t t198 = 0;
                            F.ret = ((uint8_t)0);
                            goto gm_fret;
                        }
                        goto ix175;
ie175:;
ix175:;
                    }
                    goto me170;
mn171:;
                    gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 634, 5);
me170:;
                }
lc158:;
                {
                    uint64_t t200 = gm_l18;
                    uint64_t r201; bool o202 = __builtin_add_overflow(t200, ((uint64_t)1ULL), &r201);
                    uint64_t t203 = r201;
                    gm_l18 = t203;
                }
            }
gm_ll162:;
            if (gm_unw.frame == &F.h && gm_unw.label == 3) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb158; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc158; }
            }
            goto gm_lad;
lb158:;
        }
    }
    {
        {
            gm_b16_8 t205 = gm_l3;
            gm_l21 = t205;
        }
        {
            gm_l22 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t207 = gm_l22;
                uint64_t t208 = GM_LD(uintptr_t, ((uint8_t *)&gm_l21) + 8);
                uint8_t t209 = (t207 < t208);
                if (!t209) goto lb206;
                {
                    gm_t8ip9lbbvm8be t212;
                    uint64_t t213 = gm_l22;
                    GM_ST(uint64_t, (uint8_t *)&t212 + 0, t213);
                    uint64_t t214 = gm_l22;
                    uint64_t t215 = (uintptr_t)t214;
                    uint64_t t216 = GM_LD(uintptr_t, ((uint8_t *)&gm_l21) + 8);
                    memcpy((uint8_t *)&t212 + 8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l21)) + t215 * 136), 136);
                    gm_t8ip9lbbvm8be t217 = t212;
                    uint64_t t220 = GM_LD(uint64_t, (((uint8_t *)&t217) + 0));
                    gm_l23 = t220;
                    gm_tfbvnc90lci8r t221 = GM_LD(gm_tfbvnc90lci8r, (((uint8_t *)&t217) + 8));
                    F.l24 = t221;
                    {
                        uint64_t t223 = gm_l23;
                        uint64_t t224 = (uintptr_t)t223;
                        uint64_t t225 = GM_LD(uintptr_t, (((uint8_t *)&F.l9) + 0) + 8);
                        if (t224 >= (uintptr_t)(t225)) gm_trap_index(t224, t225, gm_srcrorgjlg51brd, 638, 25);
                        uint64_t t226 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l9) + 0)) + t224 * 8));
                        uint64_t t227 = (uintptr_t)t226;
                        void * t228 = gm_l1;
                        uint64_t t229 = GM_LD(uintptr_t, (((uint8_t *)t228) + 40) + 8);
                        if (t227 >= (uintptr_t)(t229)) gm_trap_index(t227, t229, gm_srcrorgjlg51brd, 638, 14);
                        gm_t5v0c8ia6a5mo t230 = GM_LD(gm_t5v0c8ia6a5mo, ((uint8_t *)GM_LD(void *, (((uint8_t *)t228) + 40)) + t227 * 56));
                        F.l25 = t230;
                    }
                    {
                        gm_t9805qtonbsag t231 = GM_LD(gm_t9805qtonbsag, (((uint8_t *)&F.l25) + 40));
                        gm_t9805qtonbsag t232 = t231;
                        uint32_t t233;
                        if (((((uint8_t *)&t232))[4] != 0)) {
                            uint32_t t234 = GM_LD(uint32_t, (((uint8_t *)&t232) + 0));
                            t233 = t234;
                        } else {
                            uint8_t t235 = 0;
                            F.ret = ((uint8_t)0);
                            goto gm_fret;
                        }
                        gm_l26 = t233;
                    }
                    {
                        void * t236 = gm_l0;
                        void * t237 = GM_LD(void *, (((uint8_t *)t236) + 0));
                        uint32_t t238 = gm_l26;
                        gm_tnnjeerolos0a t239 = gm_f_param_kind_a0aikmb(t237, t238);
                        gm_tnnjeerolos0a t240 = t239;
                        uint8_t t241;
                        uint8_t t244 = GM_LD(uint8_t, (((uint8_t *)&t240) + 0));
                        if (t244 != ((uint8_t)2ULL)) goto mn243;
                        t241 = ((uint8_t)1);
                        goto me242;
mn243:;
                        t241 = ((uint8_t)0);
                        goto me242;
mn245:;
                        gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 640, 21);
me242:;
                        gm_l27 = t241;
                    }
                    {
                        uint8_t t247 = gm_l27;
                        uint8_t t248 = (uint8_t)!t247;
                        if (!t248) goto ie246;
                        void * t249 = gm_l0;
                        void * t250 = (void *)((uint8_t *)&F.l24);
                        uint8_t t251 = gm_f_denotes_type_jgfet92((void *)NULL, cx, NULL, t249, t250);
                        if (gm_unw.kind) {
                            goto gm_ll210;
                        }
                        uint8_t t252 = (uint8_t)!t251;
                        if (!t252) goto ie246;
                        {
                            uint8_t t254 = 0;
                            F.ret = ((uint8_t)0);
                            goto gm_fret;
                        }
                        goto ix246;
ie246:;
ix246:;
                    }
                    goto me218;
mn219:;
                    gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 637, 5);
me218:;
                }
lc206:;
                {
                    uint64_t t256 = gm_l22;
                    uint64_t r257; bool o258 = __builtin_add_overflow(t256, ((uint64_t)1ULL), &r257);
                    uint64_t t259 = r257;
                    gm_l22 = t259;
                }
            }
gm_ll210:;
            if (gm_unw.frame == &F.h && gm_unw.label == 4) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb206; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc206; }
            }
            goto gm_lad;
lb206:;
        }
    }
    {
        uint8_t t260 = 0;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_location_function_thau9ff(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_location_function_thau9ff F;
    void * gm_l0; /* cx */
    gm_b16_8 gm_l1; /* last */
    uint32_t gm_l2; /* f */
    uint64_t gm_l3; /* k */
    uint32_t gm_l4; /* f */
    GM_STACK_CHECK(&F, 436u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_b16_8 t2;
        GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sr4qbrk6tlgaa));
        GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(0));
        gm_l1 = t2;
    }
    {
        void * t4 = gm_l0;
        if (!(((((uint8_t *)t4) + 400))[4] != 0)) goto ie3;
        uint32_t t5 = GM_LD(uint32_t, (((uint8_t *)t4) + 400));
        gm_l2 = t5;
        {
            uint32_t t7 = gm_l2;
            uint64_t t8 = (uint64_t)t7;
            uint64_t t9 = (uintptr_t)t8;
            void * t10 = gm_l0;
            void * t11 = GM_LD(void *, (((uint8_t *)t10) + 0));
            uint64_t t12 = GM_LD(uintptr_t, ((((uint8_t *)t11) + 344) + 0) + 8);
            if (t9 >= (uintptr_t)(t12)) gm_trap_index(t9, t12, gm_srcrorgjlg51brd, 707, 16);
            gm_b16_8 t13 = GM_LD(gm_b16_8, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t11) + 344) + 0)) + t9 * 136) + 0));
            gm_l1 = t13;
        }
        {
            gm_b16_8 t15 = gm_l1;
            gm_b16_8 t16;
            GM_ST(void *, (uint8_t *)&t16, (void *)(gm_s1f440st4mnkp));
            GM_ST(uintptr_t, (uint8_t *)&t16 + 8, (uintptr_t)(1));
            uint8_t t17 = gm_f_starts_with_6vvt01e(t15, t16);
            uint8_t t18 = (uint8_t)!t17;
            if (!t18) goto ie14;
            {
                gm_b16_8 t20 = gm_l1;
                uint8_t t21 = 0;
                F.ret = t20;
                goto gm_fret;
            }
            goto ix14;
ie14:;
ix14:;
        }
        goto ix3;
ie3:;
ix3:;
    }
    {
        void * t22 = gm_l0;
        uint64_t t23 = GM_LD(uintptr_t, ((((uint8_t *)t22) + 576) + 0) + 8);
        gm_l3 = t23;
    }
    {
        for (;;) {
            uint64_t t25 = gm_l3;
            uint8_t t26 = (t25 > ((uint64_t)0ULL));
            if (!t26) goto lb24;
            {
                uint64_t t29 = gm_l3;
                uint64_t r30; bool o31 = __builtin_sub_overflow(t29, ((uint64_t)1ULL), &r30);
                if (o31) gm_trap("integer overflow in `-`", gm_srcrorgjlg51brd, 712, 9);
                uint64_t t32 = r30;
                gm_l3 = t32;
            }
            {
                uint64_t t33 = gm_l3;
                uint64_t t34 = (uintptr_t)t33;
                void * t35 = gm_l0;
                uint64_t t36 = GM_LD(uintptr_t, ((((uint8_t *)t35) + 576) + 0) + 8);
                if (t34 >= (uintptr_t)(t36)) gm_trap_index(t34, t36, gm_srcrorgjlg51brd, 713, 14);
                gm_t9805qtonbsag t37 = GM_LD(gm_t9805qtonbsag, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t35) + 576) + 0)) + t34 * 384) + 160));
                gm_t9805qtonbsag t38 = t37;
                uint32_t t39;
                if (((((uint8_t *)&t38))[4] != 0)) {
                    uint32_t t40 = GM_LD(uint32_t, (((uint8_t *)&t38) + 0));
                    t39 = t40;
                } else {
                    goto lc24;
                }
                gm_l4 = t39;
            }
            {
                uint32_t t41 = gm_l4;
                uint64_t t42 = (uint64_t)t41;
                uint64_t t43 = (uintptr_t)t42;
                void * t44 = gm_l0;
                void * t45 = GM_LD(void *, (((uint8_t *)t44) + 0));
                uint64_t t46 = GM_LD(uintptr_t, ((((uint8_t *)t45) + 344) + 0) + 8);
                if (t43 >= (uintptr_t)(t46)) gm_trap_index(t43, t46, gm_srcrorgjlg51brd, 714, 16);
                gm_b16_8 t47 = GM_LD(gm_b16_8, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t45) + 344) + 0)) + t43 * 136) + 0));
                gm_l1 = t47;
            }
            {
                gm_b16_8 t49 = gm_l1;
                gm_b16_8 t50;
                GM_ST(void *, (uint8_t *)&t50, (void *)(gm_s1f440st4mnkp));
                GM_ST(uintptr_t, (uint8_t *)&t50 + 8, (uintptr_t)(1));
                uint8_t t51 = gm_f_starts_with_6vvt01e(t49, t50);
                uint8_t t52 = (uint8_t)!t51;
                if (!t52) goto ie48;
                {
                    gm_b16_8 t54 = gm_l1;
                    uint8_t t55 = 0;
                    F.ret = t54;
                    goto gm_fret;
                }
                goto ix48;
ie48:;
ix48:;
            }
lc24:;
        }
lb24:;
    }
    {
        gm_b16_8 t56 = gm_l1;
        uint8_t t57 = 0;
        F.ret = t56;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_normalize_value_fgo7ofj(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3, gm_tl874t2atnlh3 a4) {
    struct gm_fr_normalize_value_fgo7ofj F;
    void * gm_l0; /* cx */
    gm_t88rcucg1b7uk gm_l1; /* v */
    gm_t88rcucg1b7uk gm_l2; /* ty */
    gm_b16_8 gm_l3; /* name */
    gm_tl874t2atnlh3 gm_l4; /* span */
    void * gm_l5; /* tys */
    gm_t428ue2u3hf5g gm_l6; /* k */
    gm_t428ue2u3hf5g gm_l7; /* x */
    uint32_t gm_l8; /* p */
    gm_t88rcucg1b7uk gm_l9; /* t */
    GM_STACK_CHECK(&F, 1736u);
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
        gm_t428ue2u3hf5g t5;
        GM_ST(uint8_t, (uint8_t *)&t5 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t5 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t5 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t5 + 8, 0);
        GM_ST(uint64_t, (uint8_t *)&t5 + 16, 0);
        GM_ST(uint64_t, (uint8_t *)&t5 + 24, 0);
        GM_ST(uint8_t, ((uint8_t *)&t5 + 0), ((uint8_t)2ULL));
        gm_l6 = t5;
    }
    {
        void * t6 = gm_l5;
        gm_t88rcucg1b7uk t7 = gm_l1;
        gm_t53at80pfdus3 t8;
        gm_f_kind_jcnt4hm((void *)&t8, t6, GM_CONV(gm_csguankko4lmhd, t7));
        gm_t53at80pfdus3 t9 = t8;
        uint8_t t16 = GM_LD(uint8_t, (((uint8_t *)&t9) + 0));
        switch (t16) { case ((uint8_t)0ULL): goto ma14; case ((uint8_t)1ULL): goto ma14; case ((uint8_t)2ULL): goto ma14; case ((uint8_t)3ULL): goto ma14; case ((uint8_t)4ULL): goto ma14; case ((uint8_t)5ULL): goto ma14; case ((uint8_t)6ULL): goto ma14; case ((uint8_t)7ULL): goto ma14; case ((uint8_t)8ULL): goto ma14; case ((uint8_t)9ULL): goto ma14; case ((uint8_t)10ULL): goto ma14; case ((uint8_t)11ULL): goto ma14; case ((uint8_t)12ULL): goto ma14; case ((uint8_t)13ULL): goto ma14; case ((uint8_t)14ULL): goto ma14; case ((uint8_t)15ULL): goto ma14; case ((uint8_t)16ULL): goto ma14; case ((uint8_t)17ULL): goto ma14; case ((uint8_t)18ULL): goto ma14; case ((uint8_t)19ULL): goto ma12; case ((uint8_t)20ULL): goto ma14; case ((uint8_t)21ULL): goto ma11; case ((uint8_t)22ULL): goto ma13; case ((uint8_t)23ULL): goto ma14; case ((uint8_t)24ULL): goto ma14; case ((uint8_t)25ULL): goto ma14; case ((uint8_t)26ULL): goto ma14; default: goto ma14; }
ma11:;
        uint8_t t18 = GM_LD(uint8_t, (((uint8_t *)&t9) + 0));
        if (t18 != ((uint8_t)21ULL)) goto mn17;
        gm_t428ue2u3hf5g t19 = GM_LD(gm_t428ue2u3hf5g, (((uint8_t *)&t9) + 8));
        gm_l7 = t19;
        {
            gm_t428ue2u3hf5g t21 = gm_l7;
            gm_l6 = t21;
        }
        goto me10;
mn17:;
ma12:;
        uint8_t t23 = GM_LD(uint8_t, (((uint8_t *)&t9) + 0));
        if (t23 != ((uint8_t)19ULL)) goto mn22;
        uint32_t t24 = GM_LD(uint32_t, (((uint8_t *)&t9) + 8));
        gm_l8 = t24;
        void * t26 = gm_l0;
        void * t27 = GM_LD(void *, (((uint8_t *)t26) + 0));
        uint32_t t28 = gm_l8;
        gm_tnnjeerolos0a t29 = gm_f_param_kind_a0aikmb(t27, t28);
        gm_tnnjeerolos0a t30 = t29;
        uint8_t t31;
        uint8_t t34 = GM_LD(uint8_t, (((uint8_t *)&t30) + 0));
        if (t34 != ((uint8_t)2ULL)) goto mn33;
        t31 = ((uint8_t)1);
        goto me32;
mn33:;
        t31 = ((uint8_t)0);
        goto me32;
mn35:;
        gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 520, 25);
me32:;
        if (!t31) goto ie25;
        {
            gm_t88rcucg1b7uk t37 = gm_l1;
            gm_tkn42u7na3t81 t38;
            GM_ST(uint8_t, (uint8_t *)&t38 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t38 + 6, 0);
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t38), t37);
            ((uint8_t *)&t38)[4] = 1;
            uint8_t t39 = 0;
            F.ret = t38;
            goto gm_fret;
        }
        goto ix25;
ie25:;
ix25:;
        goto me10;
mn22:;
ma13:;
        uint8_t t41 = GM_LD(uint8_t, (((uint8_t *)&t9) + 0));
        if (t41 != ((uint8_t)22ULL)) goto mn40;
        {
            gm_t88rcucg1b7uk t44 = gm_l2;
            void * t45 = gm_l5;
            gm_t88rcucg1b7uk t46 = gm_f_usize_ty_qnrd30f((void *)NULL, cx, NULL, t45);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t88rcucg1b7uk t47 = t44;
            gm_t88rcucg1b7uk t48 = t46;
            uint8_t t49 = (gm_eq9m6mvtmgde(((uint8_t *)&t47), ((uint8_t *)&t48)));
            if (!t49) goto ie43;
            {
                gm_t88rcucg1b7uk t51 = gm_l1;
                gm_tkn42u7na3t81 t52;
                GM_ST(uint8_t, (uint8_t *)&t52 + 5, 0);
                GM_ST(uint16_t, (uint8_t *)&t52 + 6, 0);
                GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t52), t51);
                ((uint8_t *)&t52)[4] = 1;
                uint8_t t53 = 0;
                F.ret = t52;
                goto gm_fret;
            }
            goto ix43;
ie43:;
ix43:;
        }
        {
            void * t54 = gm_l0;
            gm_tl874t2atnlh3 t55 = gm_l4;
            gm_t0uuhaljsf410 t56;
            gm_b16_8 t57 = gm_l3;
            GM_ST(gm_b16_8, (uint8_t *)&t56 + 0, t57);
            void * t58 = gm_l0;
            gm_t88rcucg1b7uk t59 = gm_l1;
            gm_b16_8 t60 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t58, t59);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(gm_b16_8, (uint8_t *)&t56 + 16, t60);
            gm_b16_8 t61 = gm_l3;
            GM_ST(gm_b16_8, (uint8_t *)&t56 + 32, t61);
            void * t62 = gm_l0;
            gm_t88rcucg1b7uk t63 = gm_l2;
            gm_b16_8 t64 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t62, t63);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(gm_b16_8, (uint8_t *)&t56 + 48, t64);
            gm_tabb710d9mq2e t65;
            gm_f_format_kj30rlk((void *)&t65, (void *)NULL, cx, NULL, &t56);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t66 = t65;
            if (((((uint8_t *)&t66))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t66) + 8, gm_srcrorgjlg51brd, 524, 28);
            gm_b16_8 t67 = GM_LD(gm_b16_8, (((uint8_t *)&t66) + 8));
            gm_b16_8 t68 = t67;
            gm_b16_8 t69 = GM_LD(gm_b16_8, ((uint8_t *)&t68));
            gm_b0_1 t70 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t54, t55, t69);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_tkn42u7na3t81 t71;
            memset(&t71, 0, sizeof t71);
            uint8_t t72 = 0;
            F.ret = t71;
            goto gm_fret;
        }
        goto me10;
mn40:;
ma14:;
        goto me10;
mn73:;
ma15:;
        gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 518, 5);
me10:;
    }
    {
        void * t76 = gm_l5;
        gm_t88rcucg1b7uk t77 = gm_l1;
        gm_t53at80pfdus3 t78;
        gm_f_kind_jcnt4hm((void *)&t78, t76, GM_CONV(gm_csguankko4lmhd, t77));
        gm_t53at80pfdus3 t79 = t78;
        uint8_t t80;
        uint8_t t83 = GM_LD(uint8_t, (((uint8_t *)&t79) + 0));
        if (t83 != ((uint8_t)21ULL)) goto mn82;
        t80 = ((uint8_t)0);
        goto me81;
mn82:;
        t80 = ((uint8_t)1);
        goto me81;
mn84:;
        gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 529, 8);
me81:;
        if (!t80) goto ie75;
        {
            void * t86 = gm_l0;
            gm_tl874t2atnlh3 t87 = gm_l4;
            gm_b16_8 t88 = gm_l3;
            gm_tnpifv20irnph t89;
            GM_ST(gm_b16_8, ((uint8_t *)&t89 + 0), t88);
            gm_tabb710d9mq2e t90;
            gm_f_format_3ifsvek((void *)&t90, (void *)NULL, cx, NULL, t89);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t91 = t90;
            if (((((uint8_t *)&t91))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t91) + 8, gm_srcrorgjlg51brd, 530, 24);
            gm_b16_8 t92 = GM_LD(gm_b16_8, (((uint8_t *)&t91) + 8));
            gm_b16_8 t93 = t92;
            gm_b16_8 t94 = GM_LD(gm_b16_8, ((uint8_t *)&t93));
            gm_b0_1 t95 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t86, t87, t94);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_tkn42u7na3t81 t96;
            memset(&t96, 0, sizeof t96);
            uint8_t t97 = 0;
            F.ret = t96;
            goto gm_fret;
        }
        goto ix75;
ie75:;
ix75:;
    }
    {
        uint8_t t99 = GM_LD(uint8_t, (((uint8_t *)&gm_l6) + 0));
        if (t99 != ((uint8_t)0ULL)) goto ie98;
        gm_t88rcucg1b7uk t100 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l6) + 8) + 0));
        gm_l9 = t100;
        {
            uint8_t t103 = 0;
            uint8_t t104 = 0;
            gm_t88rcucg1b7uk t105 = gm_l9;
            gm_t88rcucg1b7uk t106 = gm_l2;
            gm_t88rcucg1b7uk t107 = t105;
            gm_t88rcucg1b7uk t108 = t106;
            uint8_t t109 = (gm_eq9m6mvtmgde(((uint8_t *)&t107), ((uint8_t *)&t108)));
            if (!t109) {
                void * t110 = gm_l5;
                gm_t88rcucg1b7uk t111 = gm_l9;
                uint8_t t112 = gm_f_is_integer_8d4f9ek(t110, GM_CONV(gm_csguankko4lmhd, t111));
                uint8_t t113 = (uint8_t)!t112;
                t104 = t113;
            } else t104 = 1;
            if (!t104) {
                void * t114 = gm_l5;
                gm_t88rcucg1b7uk t115 = gm_l2;
                uint8_t t116 = gm_f_is_integer_8d4f9ek(t114, GM_CONV(gm_csguankko4lmhd, t115));
                uint8_t t117 = (uint8_t)!t116;
                t103 = t117;
            } else t103 = 1;
            if (!t103) goto ie102;
            {
                gm_t88rcucg1b7uk t119 = gm_l1;
                gm_tkn42u7na3t81 t120;
                GM_ST(uint8_t, (uint8_t *)&t120 + 5, 0);
                GM_ST(uint16_t, (uint8_t *)&t120 + 6, 0);
                GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t120), t119);
                ((uint8_t *)&t120)[4] = 1;
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
            void * t123 = GM_LD(void *, (((uint8_t *)t122) + 0));
            gm_thrdpodrs8pgt t124;
            gm_f_key_int_pqscuvn((void *)&t124, (void *)NULL, cx, NULL, t123, &(*(gm_t428ue2u3hf5g *)((uint8_t *)&gm_l6)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_thrdpodrs8pgt t125 = t124;
            gm_tbci5ospk5nnn t126;
            if (((((uint8_t *)&t125))[48] != 0)) {
                gm_tbci5ospk5nnn t127 = GM_LD(gm_tbci5ospk5nnn, (((uint8_t *)&t125) + 0));
                t126 = t127;
            } else {
                gm_tkn42u7na3t81 t128;
                memset(&t128, 0, sizeof t128);
                uint8_t t129 = 0;
                F.ret = t128;
                goto gm_fret;
            }
            F.l10 = t126;
        }
        {
            void * t131 = gm_l0;
            void * t132 = GM_LD(void *, (((uint8_t *)t131) + 0));
            gm_t88rcucg1b7uk t133 = gm_l2;
            uint8_t t134 = gm_f_int_fits_imbqv0c((void *)NULL, cx, NULL, t132, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l10)), t133);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t135 = (uint8_t)!t134;
            if (!t135) goto ie130;
            {
                void * t137 = gm_l0;
                gm_tl874t2atnlh3 t138 = gm_l4;
                gm_tfeab279c16rr t139;
                gm_b16_8 t140 = gm_l3;
                GM_ST(gm_b16_8, (uint8_t *)&t139 + 0, t140);
                gm_b16_8 t141 = gm_f_show_int_ub6npub((void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l10)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(gm_b16_8, (uint8_t *)&t139 + 16, t141);
                void * t142 = gm_l0;
                gm_t88rcucg1b7uk t143 = gm_l2;
                gm_b16_8 t144 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t142, t143);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(gm_b16_8, (uint8_t *)&t139 + 32, t144);
                gm_tabb710d9mq2e t145;
                gm_f_format_r57fu0b((void *)&t145, (void *)NULL, cx, NULL, &t139);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t146 = t145;
                if (((((uint8_t *)&t146))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t146) + 8, gm_srcrorgjlg51brd, 537, 24);
                gm_b16_8 t147 = GM_LD(gm_b16_8, (((uint8_t *)&t146) + 8));
                gm_b16_8 t148 = t147;
                gm_b16_8 t149 = GM_LD(gm_b16_8, ((uint8_t *)&t148));
                gm_b0_1 t150 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t137, t138, t149);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                gm_tkn42u7na3t81 t151;
                memset(&t151, 0, sizeof t151);
                uint8_t t152 = 0;
                F.ret = t151;
                goto gm_fret;
            }
            goto ix130;
ie130:;
ix130:;
        }
        {
            void * t153 = gm_l5;
            void * t154 = gm_l0;
            void * t155 = GM_LD(void *, (((uint8_t *)t154) + 0));
            gm_t88rcucg1b7uk t156 = gm_l2;
            gm_t428ue2u3hf5g t157;
            gm_f_int_key_dmcraup((void *)&t157, (void *)NULL, cx, NULL, t155, t156, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l10)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t53at80pfdus3 t158;
            GM_ST(uint8_t, (uint8_t *)&t158 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t158 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t158 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t158 + 40, 0);
            GM_ST(uint64_t, (uint8_t *)&t158 + 48, 0);
            GM_ST(uint8_t, ((uint8_t *)&t158 + 0), ((uint8_t)21ULL));
            GM_ST(gm_t428ue2u3hf5g, ((uint8_t *)&t158 + 8), t157);
            gm_t88rcucg1b7uk t159 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t153, &t158);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tkn42u7na3t81 t160;
            GM_ST(uint8_t, (uint8_t *)&t160 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t160 + 6, 0);
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t160), t159);
            ((uint8_t *)&t160)[4] = 1;
            uint8_t t161 = 0;
            F.ret = t160;
            goto gm_fret;
        }
        goto ix98;
ie98:;
        {
            gm_t88rcucg1b7uk t163 = gm_l1;
            gm_tkn42u7na3t81 t164;
            GM_ST(uint8_t, (uint8_t *)&t164 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t164 + 6, 0);
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t164), t163);
            ((uint8_t *)&t164)[4] = 1;
            uint8_t t165 = 0;
            F.ret = t164;
            goto gm_fret;
        }
ix98:;
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
__attribute__((aligned(4))) gm_b16_8 gm_f_order_generic_args_m8gcs0c(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_b16_8 a2, gm_b16_8 a3, void * a4, gm_b16_8 a5) {
    struct gm_fr_order_generic_args_m8gcs0c F;
    void * gm_l0; /* cx */
    void * gm_l1; /* out */
    gm_b16_8 gm_l2; /* slot */
    gm_b16_8 gm_l3; /* written */
    void * gm_l4; /* iter_pre */
    uint8_t gm_l7; /* reordered */
    uint64_t gm_l8; /* <tmp> */
    uint64_t gm_l9; /* <tmp> */
    uint64_t gm_l10; /* k */
    gm_b16_8 gm_l11; /* order */
    gm_b16_8 gm_l12; /* <tmp> */
    uint64_t gm_l13; /* <tmp> */
    uint64_t gm_l14; /* i */
    gm_tf5srh4mp4b3i gm_l15; /* found */
    gm_b16_8 gm_l16; /* <tmp> */
    uint64_t gm_l17; /* <tmp> */
    uint64_t gm_l18; /* n */
    gm_tf1bjg81u0glv gm_l19; /* ip */
    uint64_t gm_l20; /* n */
    void * gm_l21; /* v */
    gm_b16_8 gm_l22; /* <tmp> */
    uint64_t gm_l23; /* <tmp> */
    uint64_t gm_l24; /* i */
    void * gm_l25; /* v */
    GM_STACK_CHECK(&F, 1966u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    F.l5 = a5;
    {
        gm_f_list_ir0h3tc_2((void *)&F.l6, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_l7 = ((uint8_t)0);
    }
    {
        {
            gm_l9 = ((uint64_t)1ULL);
        }
        {
            uint64_t t3 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
            uint64_t t4 = (t3 > ((uint64_t)1ULL) ? t3 : ((uint64_t)1ULL));
            gm_l8 = t4;
        }
        {
            for (;;) {
                uint64_t t6 = gm_l9;
                uint64_t t7 = gm_l8;
                uint8_t t8 = (t6 < t7);
                if (!t8) goto lb5;
                {
                    uint64_t t11 = gm_l9;
                    uint64_t t12 = t11;
                    uint64_t t15 = GM_LD(uint64_t, ((uint8_t *)&t12));
                    gm_l10 = t15;
                    {
                        uint64_t t18 = gm_l10;
                        uint64_t r19; bool o20 = __builtin_sub_overflow(t18, ((uint64_t)1ULL), &r19);
                        if (o20) gm_trap("integer overflow in `-`", gm_srcrorgjlg51brd, 459, 20);
                        uint64_t t21 = r19;
                        uint64_t t22 = (uintptr_t)t21;
                        uint64_t t23 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                        if (t22 >= (uintptr_t)(t23)) gm_trap_index(t22, t23, gm_srcrorgjlg51brd, 459, 12);
                        uint64_t t24 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t22 * 8));
                        uint64_t t25 = gm_l10;
                        uint64_t t26 = (uintptr_t)t25;
                        uint64_t t27 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                        if (t26 >= (uintptr_t)(t27)) gm_trap_index(t26, t27, gm_srcrorgjlg51brd, 459, 29);
                        uint64_t t28 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t26 * 8));
                        uint8_t t29 = (t24 > t28);
                        if (!t29) goto ie17;
                        {
                            gm_l7 = ((uint8_t)1);
                        }
                        goto ix17;
ie17:;
ix17:;
                    }
                    goto me13;
mn14:;
                    gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 458, 5);
me13:;
                }
lc5:;
                {
                    uint64_t t32 = gm_l9;
                    uint64_t r33; bool o34 = __builtin_add_overflow(t32, ((uint64_t)1ULL), &r33);
                    uint64_t t35 = r33;
                    gm_l9 = t35;
                }
            }
lb5:;
        }
    }
    {
        void * t37 = gm_l4;
        uint64_t t38 = GM_LD(uintptr_t, (((uint8_t *)t37) + 0) + 8);
        uint8_t t39 = (t38 > ((uint64_t)0ULL));
        if (!t39) goto ie36;
        {
            gm_b16_8 t41 = gm_l3;
            gm_l11 = t41;
        }
        {
            gm_b16_8 t42 = gm_l11;
            gm_closure t43 = ((gm_closure){(void *)gm_f_fn_dgpelc4, (void *)&F.h, cx});
            gm_f_stable_sort_by_spa1mja(t42, t43);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            {
                gm_b16_8 t45 = gm_l11;
                gm_l12 = t45;
            }
            {
                gm_l13 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t47 = gm_l13;
                    uint64_t t48 = GM_LD(uintptr_t, ((uint8_t *)&gm_l12) + 8);
                    uint8_t t49 = (t47 < t48);
                    if (!t49) goto lb46;
                    {
                        uint64_t t52 = gm_l13;
                        uint64_t t53 = (uintptr_t)t52;
                        uint64_t t54 = GM_LD(uintptr_t, ((uint8_t *)&gm_l12) + 8);
                        uint64_t t55 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l12)) + t53 * 8));
                        uint64_t t56 = t55;
                        uint64_t t59 = GM_LD(uint64_t, ((uint8_t *)&t56));
                        gm_l14 = t59;
                        {
                            gm_tf5srh4mp4b3i t61;
                            memset(&t61, 0, sizeof t61);
                            gm_l15 = t61;
                        }
                        {
                            {
                                void * t63 = gm_l4;
                                gm_b16_8 t64 = GM_LD(gm_b16_8, (((uint8_t *)t63) + 0));
                                gm_b16_8 t65 = t64;
                                gm_b16_8 t66 = GM_LD(gm_b16_8, ((uint8_t *)&t65));
                                gm_l16 = t66;
                            }
                            {
                                gm_l17 = ((uint64_t)0ULL);
                            }
                            {
                                for (;;) {
                                    uint64_t t68 = gm_l17;
                                    uint64_t t69 = GM_LD(uintptr_t, ((uint8_t *)&gm_l16) + 8);
                                    uint8_t t70 = (t68 < t69);
                                    if (!t70) goto lb67;
                                    {
                                        gm_td38of7a349nj t73;
                                        uint64_t t74 = gm_l17;
                                        GM_ST(uint64_t, (uint8_t *)&t73 + 0, t74);
                                        uint64_t t75 = gm_l17;
                                        uint64_t t76 = (uintptr_t)t75;
                                        uint64_t t77 = GM_LD(uintptr_t, ((uint8_t *)&gm_l16) + 8);
                                        memcpy((uint8_t *)&t73 + 8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l16)) + t76 * 24), 24);
                                        gm_td38of7a349nj t78 = t73;
                                        uint64_t t81 = GM_LD(uint64_t, (((uint8_t *)&t78) + 0));
                                        gm_l18 = t81;
                                        gm_tf1bjg81u0glv t82 = GM_LD(gm_tf1bjg81u0glv, (((uint8_t *)&t78) + 8));
                                        gm_l19 = t82;
                                        {
                                            uint64_t t85 = GM_LD(uint64_t, (((uint8_t *)&gm_l19) + 0));
                                            uint64_t t86 = gm_l14;
                                            uint8_t t87 = ((t85 == t86));
                                            if (!t87) goto ie84;
                                            {
                                                uint64_t t89 = gm_l18;
                                                gm_tf5srh4mp4b3i t90;
                                                GM_ST(uint8_t, (uint8_t *)&t90 + 9, 0);
                                                GM_ST(uint16_t, (uint8_t *)&t90 + 10, 0);
                                                GM_ST(uint32_t, (uint8_t *)&t90 + 12, 0);
                                                GM_ST(uint64_t, ((uint8_t *)&t90), t89);
                                                ((uint8_t *)&t90)[8] = 1;
                                                gm_l15 = t90;
                                            }
                                            goto ix84;
ie84:;
ix84:;
                                        }
                                        goto me79;
mn80:;
                                        gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 467, 13);
me79:;
                                    }
lc67:;
                                    {
                                        uint64_t t92 = gm_l17;
                                        uint64_t r93; bool o94 = __builtin_add_overflow(t92, ((uint64_t)1ULL), &r93);
                                        uint64_t t95 = r93;
                                        gm_l17 = t95;
                                    }
                                }
lb67:;
                            }
                        }
                        {
                            if (!((((uint8_t *)&gm_l15))[8] != 0)) goto ie96;
                            uint64_t t97 = GM_LD(uint64_t, ((uint8_t *)&gm_l15));
                            gm_l20 = t97;
                            {
                                void * t99 = (void *)((uint8_t *)&F.l6);
                                uint64_t t100 = gm_l20;
                                uint64_t t101 = (uintptr_t)t100;
                                void * t102 = gm_l4;
                                uint64_t t103 = GM_LD(uintptr_t, (((uint8_t *)t102) + 0) + 8);
                                if (t101 >= (uintptr_t)(t103)) gm_trap_index(t101, t103, gm_srcrorgjlg51brd, 471, 28);
                                gm_b16_8 t104 = GM_LD(gm_b16_8, (((uint8_t *)GM_LD(void *, (((uint8_t *)t102) + 0)) + t101 * 24) + 8));
                                gm_b16_8 t105 = t104;
                                gm_b16_8 t106 = GM_LD(gm_b16_8, ((uint8_t *)&t105));
                                gm_tftb1odc739t5 t107 = gm_f_extend_nkql463(t99, t106);
                                gm_tftb1odc739t5 t108 = t107;
                                if (((((uint8_t *)&t108))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t108) + 1, gm_srcrorgjlg51brd, 471, 17);
                                gm_b0_1 t109 = GM_LD(gm_b0_1, (((uint8_t *)&t108) + 1));
                            }
                            {
                                {
                                    void * t111 = gm_l4;
                                    uint64_t t112 = gm_l20;
                                    gm_tf1bjg81u0glv t113;
                                    gm_f_remove_iqb0btt((void *)&t113, t111, t112);
                                }
                            }
                            goto ix96;
ie96:;
                            {
                                uint64_t t115 = gm_l14;
                                uint64_t t116 = (uintptr_t)t115;
                                uint64_t t117 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                                if (t116 >= (uintptr_t)(t117)) gm_trap_index(t116, t117, gm_srcrorgjlg51brd, 474, 36);
                                uint64_t t118 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t116 * 8));
                                uint64_t t119 = (uintptr_t)t118;
                                void * t120 = gm_l1;
                                uint64_t t121 = GM_LD(uintptr_t, (((uint8_t *)t120) + 0) + 8);
                                if (t119 >= (uintptr_t)(t121)) gm_trap_index(t119, t121, gm_srcrorgjlg51brd, 474, 26);
                                gm_tq7s98ihonkju t122 = gm_f_new_ve8bb01_2((void *)NULL, cx, NULL, &(*(gm_tfpf0s89fpb3r *)((uint8_t *)GM_LD(void *, (((uint8_t *)t120) + 0)) + t119 * 224)), &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kkh67d5mpb1)));
                                if (gm_unw.kind) {
                                    goto gm_ll50;
                                }
                                gm_tq7s98ihonkju t123 = t122;
                                if (((((uint8_t *)&t123))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t123) + 8, gm_srcrorgjlg51brd, 474, 22);
                                void * t124 = GM_LD(void *, (((uint8_t *)&t123) + 8));
                                gm_l21 = t124;
                            }
                            {
                                uint64_t t125 = gm_l14;
                                uint64_t t126 = (uintptr_t)t125;
                                uint64_t t127 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                                if (t126 >= (uintptr_t)(t127)) gm_trap_index(t126, t127, gm_srcrorgjlg51brd, 475, 27);
                                uint64_t t128 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t126 * 8));
                                uint64_t t129 = (uintptr_t)t128;
                                void * t130 = gm_l1;
                                uint64_t t131 = GM_LD(uintptr_t, (((uint8_t *)t130) + 0) + 8);
                                if (t129 >= (uintptr_t)(t131)) gm_trap_index(t129, t131, gm_srcrorgjlg51brd, 475, 17);
                                uint8_t *t132 = ((uint8_t *)GM_LD(void *, (((uint8_t *)t130) + 0)) + t129 * 224);
                                void * t133 = gm_l0;
                                void * t134 = gm_l21;
                                void * t135 = (void *)((uint8_t *)&F.l6);
                                void * t136 = gm_f_bind_temp_sa5rcbr((void *)NULL, cx, NULL, t133, t134, t135);
                                if (gm_unw.kind) {
                                    goto gm_ll50;
                                }
                                gm_tfpf0s89fpb3r t137 = GM_LD(gm_tfpf0s89fpb3r, ((uint8_t *)t136));
                                GM_ST(gm_tfpf0s89fpb3r, t132, t137);
                            }
ix96:;
                        }
                        goto me57;
mn58:;
                        gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 465, 9);
me57:;
                    }
lc46:;
                    {
                        uint64_t t139 = gm_l13;
                        uint64_t r140; bool o141 = __builtin_add_overflow(t139, ((uint64_t)1ULL), &r140);
                        uint64_t t142 = r140;
                        gm_l13 = t142;
                    }
                }
gm_ll50:;
                if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb46; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc46; }
                }
                goto gm_lad;
lb46:;
            }
        }
        goto ix36;
ie36:;
        uint8_t t144 = gm_l7;
        if (!t144) goto ie143;
        {
            {
                gm_b16_8 t147 = gm_l3;
                gm_l22 = t147;
            }
            {
                gm_l23 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t149 = gm_l23;
                    uint64_t t150 = GM_LD(uintptr_t, ((uint8_t *)&gm_l22) + 8);
                    uint8_t t151 = (t149 < t150);
                    if (!t151) goto lb148;
                    {
                        uint64_t t154 = gm_l23;
                        uint64_t t155 = (uintptr_t)t154;
                        uint64_t t156 = GM_LD(uintptr_t, ((uint8_t *)&gm_l22) + 8);
                        uint64_t t157 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l22)) + t155 * 8));
                        uint64_t t158 = t157;
                        uint64_t t161 = GM_LD(uint64_t, ((uint8_t *)&t158));
                        gm_l24 = t161;
                        {
                            uint64_t t163 = gm_l24;
                            uint64_t t164 = (uintptr_t)t163;
                            uint64_t t165 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                            if (t164 >= (uintptr_t)(t165)) gm_trap_index(t164, t165, gm_srcrorgjlg51brd, 480, 32);
                            uint64_t t166 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t164 * 8));
                            uint64_t t167 = (uintptr_t)t166;
                            void * t168 = gm_l1;
                            uint64_t t169 = GM_LD(uintptr_t, (((uint8_t *)t168) + 0) + 8);
                            if (t167 >= (uintptr_t)(t169)) gm_trap_index(t167, t169, gm_srcrorgjlg51brd, 480, 22);
                            gm_tq7s98ihonkju t170 = gm_f_new_ve8bb01_2((void *)NULL, cx, NULL, &(*(gm_tfpf0s89fpb3r *)((uint8_t *)GM_LD(void *, (((uint8_t *)t168) + 0)) + t167 * 224)), &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kc84hfpu27t)));
                            if (gm_unw.kind) {
                                goto gm_ll152;
                            }
                            gm_tq7s98ihonkju t171 = t170;
                            if (((((uint8_t *)&t171))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t171) + 8, gm_srcrorgjlg51brd, 480, 18);
                            void * t172 = GM_LD(void *, (((uint8_t *)&t171) + 8));
                            gm_l25 = t172;
                        }
                        {
                            uint64_t t173 = gm_l24;
                            uint64_t t174 = (uintptr_t)t173;
                            uint64_t t175 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                            if (t174 >= (uintptr_t)(t175)) gm_trap_index(t174, t175, gm_srcrorgjlg51brd, 481, 23);
                            uint64_t t176 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t174 * 8));
                            uint64_t t177 = (uintptr_t)t176;
                            void * t178 = gm_l1;
                            uint64_t t179 = GM_LD(uintptr_t, (((uint8_t *)t178) + 0) + 8);
                            if (t177 >= (uintptr_t)(t179)) gm_trap_index(t177, t179, gm_srcrorgjlg51brd, 481, 13);
                            uint8_t *t180 = ((uint8_t *)GM_LD(void *, (((uint8_t *)t178) + 0)) + t177 * 224);
                            void * t181 = gm_l0;
                            void * t182 = gm_l25;
                            void * t183 = (void *)((uint8_t *)&F.l6);
                            void * t184 = gm_f_bind_temp_sa5rcbr((void *)NULL, cx, NULL, t181, t182, t183);
                            if (gm_unw.kind) {
                                goto gm_ll152;
                            }
                            gm_tfpf0s89fpb3r t185 = GM_LD(gm_tfpf0s89fpb3r, ((uint8_t *)t184));
                            GM_ST(gm_tfpf0s89fpb3r, t180, t185);
                        }
                        goto me159;
mn160:;
                        gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 479, 9);
me159:;
                    }
lc148:;
                    {
                        uint64_t t187 = gm_l23;
                        uint64_t r188; bool o189 = __builtin_add_overflow(t187, ((uint64_t)1ULL), &r188);
                        uint64_t t190 = r188;
                        gm_l23 = t190;
                    }
                }
gm_ll152:;
                if (gm_unw.frame == &F.h && gm_unw.label == 3) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb148; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc148; }
                }
                goto gm_lad;
lb148:;
            }
        }
        goto ix143;
ie143:;
ix143:;
ix36:;
    }
    {
        gm_b16_8 t191 = GM_LD(gm_b16_8, (((uint8_t *)&F.l6) + 0));
        uint8_t t192 = 0;
        F.ret = t191;
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
inline __attribute__((aligned(4))) void * gm_f_param_default_h93ci1m(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint64_t a2, void * a3, gm_t88rcucg1b7uk a4, gm_tl874t2atnlh3 a5) {
    struct gm_fr_param_default_h93ci1m F;
    void * gm_l0; /* cx */
    void * gm_l1; /* p */
    uint64_t gm_l2; /* module */
    void * gm_l3; /* d */
    gm_t88rcucg1b7uk gm_l4; /* ty */
    gm_tl874t2atnlh3 gm_l5; /* span */
    GM_STACK_CHECK(&F, 140u);
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
        void * t3 = gm_l1;
        uint8_t t4 = gm_f_is_site_8sr7vam(t3);
        uint8_t t5 = (uint8_t)!t4;
        if (!t5) goto ie2;
        {
            void * t7 = gm_l0;
            uint64_t t8 = gm_l2;
            void * t9 = gm_l3;
            gm_t88rcucg1b7uk t10 = gm_l4;
            void * t11 = gm_f_default_value_lo723gi((void *)NULL, cx, NULL, t7, t8, t9, t10);
            if (gm_unw.kind) {
                goto gm_lad;
            }
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
        gm_tl874t2atnlh3 t14 = gm_l5;
        void * t15 = gm_f_source_location_nc1oola((void *)NULL, cx, NULL, t13, t14);
        if (gm_unw.kind) {
            goto gm_lad;
        }
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
__attribute__((aligned(4))) gm_b0_1 gm_f_report_cannot_infer_tor1q4u(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint32_t a2, gm_b16_8 a3, gm_tl874t2atnlh3 a4) {
    struct gm_fr_report_cannot_infer_tor1q4u F;
    void * gm_l0; /* cx */
    void * gm_l1; /* sig */
    uint32_t gm_l2; /* pid */
    gm_b16_8 gm_l3; /* values */
    gm_tl874t2atnlh3 gm_l4; /* span */
    void * gm_l5; /* tys */
    uint8_t gm_l6; /* array_for_slice */
    uint8_t gm_l7; /* pointer_for_slice */
    gm_b16_8 gm_l8; /* <tmp> */
    uint64_t gm_l9; /* <tmp> */
    uint64_t gm_l10; /* i */
    void * gm_l12; /* v */
    gm_b16_8 gm_l13; /* hint */
    gm_b16_8 gm_l14; /* pname */
    gm_b16_8 gm_l15; /* how */
    gm_b16_8 gm_l16; /* <tmp> */
    uint64_t gm_l17; /* <tmp> */
    uint64_t gm_l18; /* k */
    GM_STACK_CHECK(&F, 2148u);
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
        gm_l6 = ((uint8_t)0);
    }
    {
        gm_l7 = ((uint8_t)0);
    }
    {
        {
            void * t6 = gm_l1;
            gm_b16_8 t7 = GM_LD(gm_b16_8, (((uint8_t *)t6) + 40));
            gm_b16_8 t8 = t7;
            gm_b16_8 t9 = GM_LD(gm_b16_8, ((uint8_t *)&t8));
            gm_l8 = t9;
        }
        {
            gm_l9 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t11 = gm_l9;
                uint64_t t12 = GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8);
                uint8_t t13 = (t11 < t12);
                if (!t13) goto lb10;
                {
                    gm_tgsvmddj3e85e t16;
                    uint64_t t17 = gm_l9;
                    GM_ST(uint64_t, (uint8_t *)&t16 + 0, t17);
                    uint64_t t18 = gm_l9;
                    uint64_t t19 = (uintptr_t)t18;
                    uint64_t t20 = GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8);
                    memcpy((uint8_t *)&t16 + 8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l8)) + t19 * 56), 56);
                    gm_tgsvmddj3e85e t21 = t16;
                    uint64_t t24 = GM_LD(uint64_t, (((uint8_t *)&t21) + 0));
                    gm_l10 = t24;
                    gm_t5v0c8ia6a5mo t25 = GM_LD(gm_t5v0c8ia6a5mo, (((uint8_t *)&t21) + 8));
                    F.l11 = t25;
                    {
                        uint64_t t27 = gm_l10;
                        uint64_t t28 = (uintptr_t)t27;
                        uint64_t t29 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                        if (t28 >= (uintptr_t)(t29)) gm_trap_index(t28, t29, gm_srcrorgjlg51brd, 385, 14);
                        void * t30 = GM_LD(void *, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t28 * 8));
                        void * t31 = t30;
                        void * t32;
                        if ((GM_LD(void *, ((uint8_t *)&t31)) != 0)) {
                            void * t33 = GM_LD(void *, (((uint8_t *)&t31) + 0));
                            t32 = t33;
                        } else {
                            goto lc10;
                        }
                        gm_l12 = t32;
                    }
                    {
                        void * t35 = gm_l5;
                        gm_t88rcucg1b7uk t36 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l11) + 16));
                        gm_t53at80pfdus3 t37;
                        gm_f_kind_jcnt4hm((void *)&t37, t35, GM_CONV(gm_csguankko4lmhd, t36));
                        gm_t53at80pfdus3 t38 = t37;
                        uint8_t t39;
                        uint8_t t42 = GM_LD(uint8_t, (((uint8_t *)&t38) + 0));
                        if (t42 != ((uint8_t)10ULL)) goto mn41;
                        t39 = ((uint8_t)0);
                        goto me40;
mn41:;
                        t39 = ((uint8_t)1);
                        goto me40;
mn43:;
                        gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 386, 12);
me40:;
                        if (!t39) goto ie34;
                        {
                            goto lc10;
                        }
                        goto ix34;
ie34:;
ix34:;
                    }
                    {
                        void * t46 = gm_l5;
                        void * t47 = gm_l12;
                        gm_t88rcucg1b7uk t48 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t47) + 0));
                        gm_t53at80pfdus3 t49;
                        gm_f_kind_jcnt4hm((void *)&t49, t46, GM_CONV(gm_csguankko4lmhd, t48));
                        gm_t53at80pfdus3 t50 = t49;
                        uint8_t t51;
                        uint8_t t54 = GM_LD(uint8_t, (((uint8_t *)&t50) + 0));
                        if (t54 != ((uint8_t)11ULL)) goto mn53;
                        t51 = ((uint8_t)1);
                        goto me52;
mn53:;
                        t51 = ((uint8_t)0);
                        goto me52;
mn55:;
                        gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 387, 12);
me52:;
                        if (!t51) goto ie45;
                        {
                            gm_l6 = ((uint8_t)1);
                        }
                        goto ix45;
ie45:;
ix45:;
                    }
                    {
                        void * t58 = gm_l5;
                        void * t59 = gm_l0;
                        void * t60 = (void *)(((uint8_t *)t59) + 96);
                        void * t61 = gm_l0;
                        void * t62 = GM_LD(void *, (((uint8_t *)t61) + 0));
                        void * t63 = gm_l12;
                        gm_t88rcucg1b7uk t64 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t63) + 0));
                        gm_t88rcucg1b7uk t65 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t60, t62, t64);
                        if (gm_unw.kind) {
                            goto gm_ll14;
                        }
                        gm_t53at80pfdus3 t66;
                        gm_f_kind_jcnt4hm((void *)&t66, t58, GM_CONV(gm_csguankko4lmhd, t65));
                        gm_t53at80pfdus3 t67 = t66;
                        uint8_t t68;
                        uint8_t t71 = GM_LD(uint8_t, (((uint8_t *)&t67) + 0));
                        if (t71 != ((uint8_t)8ULL)) goto mn70;
                        t68 = ((uint8_t)1);
                        goto me69;
mn70:;
                        t68 = ((uint8_t)0);
                        goto me69;
mn72:;
                        gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 388, 12);
me69:;
                        if (!t68) goto ie57;
                        {
                            gm_l7 = ((uint8_t)1);
                        }
                        goto ix57;
ie57:;
ix57:;
                    }
                    goto me22;
mn23:;
                    gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 384, 5);
me22:;
                }
lc10:;
                {
                    uint64_t t75 = gm_l9;
                    uint64_t r76; bool o77 = __builtin_add_overflow(t75, ((uint64_t)1ULL), &r76);
                    uint64_t t78 = r76;
                    gm_l9 = t78;
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
        gm_b16_8 t79;
        uint8_t t81 = gm_l6;
        if (!t81) goto ie80;
        gm_b16_8 t83;
        GM_ST(void *, (uint8_t *)&t83, (void *)(gm_stl5olh2jfr0d));
        GM_ST(uintptr_t, (uint8_t *)&t83 + 8, (uintptr_t)(39));
        gm_b16_8 t84 = t83;
        t79 = t84;
        goto ix80;
ie80:;
        gm_b16_8 t85;
        uint8_t t87 = gm_l7;
        if (!t87) goto ie86;
        void * t88 = gm_l1;
        uint8_t t89 = (((GM_LD(uintptr_t, (((uint8_t *)t88) + 0) + 8) == 4) && memcmp(GM_LD(void *, (((uint8_t *)t88) + 0)), gm_srv4cua6q51nq, 4) == 0));
        if (!t89) goto ie86;
        gm_b16_8 t91;
        GM_ST(void *, (uint8_t *)&t91, (void *)(gm_sg7ronlh5k7js));
        GM_ST(uintptr_t, (uint8_t *)&t91 + 8, (uintptr_t)(83));
        gm_b16_8 t92 = t91;
        t85 = t92;
        goto ix86;
ie86:;
        gm_b16_8 t93;
        uint8_t t95 = gm_l7;
        if (!t95) goto ie94;
        gm_b16_8 t97;
        GM_ST(void *, (uint8_t *)&t97, (void *)(gm_sgj7bt7nru3s1));
        GM_ST(uintptr_t, (uint8_t *)&t97 + 8, (uintptr_t)(26));
        gm_b16_8 t98 = t97;
        t93 = t98;
        goto ix94;
ie94:;
        gm_b16_8 t100;
        GM_ST(void *, (uint8_t *)&t100, (void *)(gm_sr4qbrk6tlgaa));
        GM_ST(uintptr_t, (uint8_t *)&t100 + 8, (uintptr_t)(0));
        gm_b16_8 t101 = t100;
        t93 = t101;
ix94:;
        t85 = t93;
ix86:;
        t79 = t85;
ix80:;
        gm_l13 = t79;
    }
    {
        void * t102 = gm_l5;
        uint32_t t103 = gm_l2;
        gm_b16_8 t104 = gm_f_param_name_rbnop5l(t102, t103);
        gm_l14 = t104;
    }
    {
        gm_t6j6smj7qoctu t105;
        void * t106 = gm_l1;
        gm_b16_8 t107 = GM_LD(gm_b16_8, (((uint8_t *)t106) + 0));
        GM_ST(gm_b16_8, (uint8_t *)&t105 + 0, t107);
        gm_b16_8 t108 = gm_l14;
        GM_ST(gm_b16_8, (uint8_t *)&t105 + 16, t108);
        gm_tabb710d9mq2e t109;
        gm_f_format_vns89mr((void *)&t109, (void *)NULL, cx, NULL, &t105);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t110 = t109;
        if (((((uint8_t *)&t110))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t110) + 8, gm_srcrorgjlg51brd, 400, 19);
        gm_b16_8 t111 = GM_LD(gm_b16_8, (((uint8_t *)&t110) + 8));
        gm_b16_8 t112 = t111;
        gm_b16_8 t113 = GM_LD(gm_b16_8, ((uint8_t *)&t112));
        gm_l15 = t113;
    }
    {
        {
            void * t115 = gm_l1;
            gm_b16_8 t116 = GM_LD(gm_b16_8, (((uint8_t *)t115) + 40));
            gm_b16_8 t117 = t116;
            gm_b16_8 t118 = GM_LD(gm_b16_8, ((uint8_t *)&t117));
            gm_l16 = t118;
        }
        {
            gm_l17 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t120 = gm_l17;
                uint64_t t121 = GM_LD(uintptr_t, ((uint8_t *)&gm_l16) + 8);
                uint8_t t122 = (t120 < t121);
                if (!t122) goto lb119;
                {
                    gm_tgsvmddj3e85e t125;
                    uint64_t t126 = gm_l17;
                    GM_ST(uint64_t, (uint8_t *)&t125 + 0, t126);
                    uint64_t t127 = gm_l17;
                    uint64_t t128 = (uintptr_t)t127;
                    uint64_t t129 = GM_LD(uintptr_t, ((uint8_t *)&gm_l16) + 8);
                    memcpy((uint8_t *)&t125 + 8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l16)) + t128 * 56), 56);
                    gm_tgsvmddj3e85e t130 = t125;
                    uint64_t t133 = GM_LD(uint64_t, (((uint8_t *)&t130) + 0));
                    gm_l18 = t133;
                    gm_t5v0c8ia6a5mo t134 = GM_LD(gm_t5v0c8ia6a5mo, (((uint8_t *)&t130) + 8));
                    F.l19 = t134;
                    {
                        gm_t9805qtonbsag t137 = GM_LD(gm_t9805qtonbsag, (((uint8_t *)&F.l19) + 40));
                        uint32_t t138 = gm_l2;
                        gm_t9805qtonbsag t139;
                        GM_ST(uint8_t, (uint8_t *)&t139 + 5, 0);
                        GM_ST(uint16_t, (uint8_t *)&t139 + 6, 0);
                        GM_ST(uint32_t, ((uint8_t *)&t139), t138);
                        ((uint8_t *)&t139)[4] = 1;
                        gm_t9805qtonbsag t140 = t137;
                        gm_t9805qtonbsag t141 = t139;
                        uint8_t t142 = (!gm_eqkricceou4u(((uint8_t *)&t140), ((uint8_t *)&t141)));
                        if (!t142) goto ie136;
                        {
                            goto lc119;
                        }
                        goto ix136;
ie136:;
ix136:;
                    }
                    {
                        void * t145 = gm_l0;
                        void * t146 = GM_LD(void *, (((uint8_t *)t145) + 0));
                        void * t147 = gm_l1;
                        uint64_t t148 = gm_l18;
                        uint8_t t149 = gm_f_inferable_param_f80lrdi(t146, t147, t148);
                        if (!t149) goto ie144;
                        {
                            gm_t6j6smj7qoctu t151;
                            void * t152 = gm_l1;
                            gm_b16_8 t153 = GM_LD(gm_b16_8, (((uint8_t *)t152) + 0));
                            GM_ST(gm_b16_8, (uint8_t *)&t151 + 0, t153);
                            gm_b16_8 t154 = gm_l14;
                            GM_ST(gm_b16_8, (uint8_t *)&t151 + 16, t154);
                            gm_tabb710d9mq2e t155;
                            gm_f_format_9n2qn66((void *)&t155, (void *)NULL, cx, NULL, &t151);
                            if (gm_unw.kind) {
                                goto gm_ll123;
                            }
                            gm_tabb710d9mq2e t156 = t155;
                            if (((((uint8_t *)&t156))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t156) + 8, gm_srcrorgjlg51brd, 403, 50);
                            gm_b16_8 t157 = GM_LD(gm_b16_8, (((uint8_t *)&t156) + 8));
                            gm_b16_8 t158 = t157;
                            gm_b16_8 t159 = GM_LD(gm_b16_8, ((uint8_t *)&t158));
                            gm_l15 = t159;
                        }
                        goto ix144;
ie144:;
ix144:;
                    }
                    {
                        goto lb119;
                    }
                    goto me131;
mn132:;
                    gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 401, 5);
me131:;
                }
lc119:;
                {
                    uint64_t t161 = gm_l17;
                    uint64_t r162; bool o163 = __builtin_add_overflow(t161, ((uint64_t)1ULL), &r162);
                    uint64_t t164 = r162;
                    gm_l17 = t164;
                }
            }
gm_ll123:;
            if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb119; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc119; }
            }
            goto gm_lad;
lb119:;
        }
    }
    {
        void * t165 = gm_l0;
        gm_tl874t2atnlh3 t166 = gm_l4;
        gm_tfeab279c16rr t167;
        gm_b16_8 t168 = gm_l14;
        GM_ST(gm_b16_8, (uint8_t *)&t167 + 0, t168);
        gm_b16_8 t169 = gm_l15;
        GM_ST(gm_b16_8, (uint8_t *)&t167 + 16, t169);
        gm_b16_8 t170 = gm_l13;
        GM_ST(gm_b16_8, (uint8_t *)&t167 + 32, t170);
        gm_tabb710d9mq2e t171;
        gm_f_format_obibpjh((void *)&t171, (void *)NULL, cx, NULL, &t167);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t172 = t171;
        if (((((uint8_t *)&t172))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t172) + 8, gm_srcrorgjlg51brd, 406, 20);
        gm_b16_8 t173 = GM_LD(gm_b16_8, (((uint8_t *)&t172) + 8));
        gm_b16_8 t174 = t173;
        gm_b16_8 t175 = GM_LD(gm_b16_8, ((uint8_t *)&t174));
        gm_b0_1 t176 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t165, t166, t175);
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
inline __attribute__((aligned(4))) uint8_t gm_f_same_int_value_ngrqsok(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_t88rcucg1b7uk a2) {
    struct gm_fr_same_int_value_ngrqsok F;
    void * gm_l0; /* s */
    gm_t88rcucg1b7uk gm_l1; /* a */
    gm_t88rcucg1b7uk gm_l2; /* b */
    gm_t428ue2u3hf5g gm_l3; /* x */
    gm_t428ue2u3hf5g gm_l4; /* y */
    GM_STACK_CHECK(&F, 850u);
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
        if (t8 != ((uint8_t)21ULL)) goto ie2;
        gm_t428ue2u3hf5g t9 = GM_LD(gm_t428ue2u3hf5g, (((uint8_t *)&t7) + 8));
        gm_l3 = t9;
        {
            void * t12 = gm_l0;
            void * t13 = (void *)(((uint8_t *)t12) + 8);
            gm_t88rcucg1b7uk t14 = gm_l2;
            gm_t53at80pfdus3 t15;
            gm_f_kind_jcnt4hm((void *)&t15, t13, GM_CONV(gm_csguankko4lmhd, t14));
            gm_t53at80pfdus3 t16 = t15;
            uint8_t t17 = GM_LD(uint8_t, (((uint8_t *)&t16) + 0));
            if (t17 != ((uint8_t)21ULL)) goto ie11;
            gm_t428ue2u3hf5g t18 = GM_LD(gm_t428ue2u3hf5g, (((uint8_t *)&t16) + 8));
            gm_l4 = t18;
            {
                void * t20 = gm_l0;
                gm_thrdpodrs8pgt t21;
                gm_f_key_int_pqscuvn((void *)&t21, (void *)NULL, cx, NULL, t20, &(*(gm_t428ue2u3hf5g *)((uint8_t *)&gm_l3)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_thrdpodrs8pgt t22 = t21;
                gm_tbci5ospk5nnn t23;
                if (((((uint8_t *)&t22))[48] != 0)) {
                    gm_tbci5ospk5nnn t24 = GM_LD(gm_tbci5ospk5nnn, (((uint8_t *)&t22) + 0));
                    t23 = t24;
                } else {
                    uint8_t t25 = 0;
                    F.ret = ((uint8_t)0);
                    goto gm_fret;
                }
                F.l5 = t23;
            }
            {
                void * t26 = gm_l0;
                gm_thrdpodrs8pgt t27;
                gm_f_key_int_pqscuvn((void *)&t27, (void *)NULL, cx, NULL, t26, &(*(gm_t428ue2u3hf5g *)((uint8_t *)&gm_l4)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_thrdpodrs8pgt t28 = t27;
                gm_tbci5ospk5nnn t29;
                if (((((uint8_t *)&t28))[48] != 0)) {
                    gm_tbci5ospk5nnn t30 = GM_LD(gm_tbci5ospk5nnn, (((uint8_t *)&t28) + 0));
                    t29 = t30;
                } else {
                    uint8_t t31 = 0;
                    F.ret = ((uint8_t)0);
                    goto gm_fret;
                }
                F.l6 = t29;
            }
            {
                uint8_t t32 = gm_f_eq_60fjn21(&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l5)), &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l6)));
                uint8_t t33 = 0;
                F.ret = t32;
                goto gm_fret;
            }
            goto ix11;
ie11:;
            {
                uint8_t t35 = 0;
                F.ret = ((uint8_t)0);
                goto gm_fret;
            }
ix11:;
        }
        goto ix2;
ie2:;
        {
            uint8_t t37 = 0;
            F.ret = ((uint8_t)0);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_set_bind_nsjnlvq(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_t88rcucg1b7uk a2) {
    struct gm_fr_set_bind_nsjnlvq F;
    void * gm_l0; /* binds */
    uint32_t gm_l1; /* p */
    gm_t88rcucg1b7uk gm_l2; /* t */
    uint64_t gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    uint64_t gm_l5; /* i */
    GM_STACK_CHECK(&F, 219u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        {
            gm_l4 = ((uint64_t)0ULL);
        }
        {
            void * t3 = gm_l0;
            uint64_t t4 = GM_LD(uintptr_t, (((uint8_t *)t3) + 0) + 8);
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
                    uint64_t t12 = t11;
                    uint64_t t15 = GM_LD(uint64_t, ((uint8_t *)&t12));
                    gm_l5 = t15;
                    {
                        uint64_t t18 = gm_l5;
                        uint64_t t19 = (uintptr_t)t18;
                        void * t20 = gm_l0;
                        uint64_t t21 = GM_LD(uintptr_t, (((uint8_t *)t20) + 0) + 8);
                        if (t19 >= (uintptr_t)(t21)) gm_trap_index(t19, t21, gm_srcrorgjlg51brd, 546, 12);
                        uint32_t t22 = GM_LD(uint32_t, (((uint8_t *)GM_LD(void *, (((uint8_t *)t20) + 0)) + t19 * 8) + 0));
                        uint32_t t23 = gm_l1;
                        uint8_t t24 = ((t22 == t23));
                        if (!t24) goto ie17;
                        {
                            uint64_t t26 = gm_l5;
                            uint64_t t27 = (uintptr_t)t26;
                            void * t28 = gm_l0;
                            uint64_t t29 = GM_LD(uintptr_t, (((uint8_t *)t28) + 0) + 8);
                            if (t27 >= (uintptr_t)(t29)) gm_trap_index(t27, t29, gm_srcrorgjlg51brd, 547, 13);
                            gm_t88rcucg1b7uk t30 = gm_l2;
                            GM_ST(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, (((uint8_t *)t28) + 0)) + t27 * 8) + 4), t30);
                        }
                        {
                            gm_b0_1 t31;
                            uint8_t t32 = 0;
                            F.ret = t31;
                            goto gm_fret;
                        }
                        goto ix17;
ie17:;
ix17:;
                    }
                    goto me13;
mn14:;
                    gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 545, 5);
me13:;
                }
lc5:;
                {
                    uint64_t t34 = gm_l4;
                    uint64_t r35; bool o36 = __builtin_add_overflow(t34, ((uint64_t)1ULL), &r35);
                    uint64_t t37 = r35;
                    gm_l4 = t37;
                }
            }
lb5:;
        }
    }
    {
        void * t38 = gm_l0;
        uint32_t t39 = gm_l1;
        gm_t88rcucg1b7uk t40 = gm_l2;
        gm_tplpbqf3duvn3 t41;
        GM_ST(uint32_t, ((uint8_t *)&t41 + 0), t39);
        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t41 + 4), t40);
        gm_tftb1odc739t5 t42 = gm_f_push_h69n5fi(t38, GM_CONV(gm_cs7e9ppsd384bj, t41));
        gm_tftb1odc739t5 t43 = t42;
        if (((((uint8_t *)&t43))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t43) + 1, gm_srcrorgjlg51brd, 551, 5);
        gm_b0_1 t44 = GM_LD(gm_b0_1, (((uint8_t *)&t43) + 1));
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) void * gm_f_source_location_nc1oola(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1) {
    struct gm_fr_source_location_nc1oola F;
    void * gm_l0; /* cx */
    gm_tl874t2atnlh3 gm_l1; /* span */
    void * gm_l2; /* tys */
    gm_t88rcucg1b7uk gm_l3; /* ty */
    void * gm_l4; /* file */
    gm_tcc1rq4n1avb2 gm_l5; /* lc */
    gm_b16_8 gm_l6; /* function */
    GM_STACK_CHECK(&F, 1682u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 0));
        void * t4 = (void *)(((uint8_t *)t3) + 8);
        gm_l2 = t4;
    }
    {
        void * t5 = gm_l0;
        void * t6 = GM_LD(void *, (((uint8_t *)t5) + 0));
        gm_tkn42u7na3t81 t7 = gm_f_source_location_ty_slllard((void *)NULL, cx, NULL, t6);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tkn42u7na3t81 t8 = t7;
        gm_t88rcucg1b7uk t9;
        if (((((uint8_t *)&t8))[4] != 0)) {
            gm_t88rcucg1b7uk t10 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t8) + 0));
            t9 = t10;
        } else {
            {
                void * t12 = gm_l0;
                gm_tl874t2atnlh3 t13 = gm_l1;
                gm_b16_8 t14;
                GM_ST(void *, (uint8_t *)&t14, (void *)(gm_s8pjuqfuormq3));
                GM_ST(uintptr_t, (uint8_t *)&t14 + 8, (uintptr_t)(50));
                gm_b0_1 t15 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t12, t13, t14);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            void * t16 = gm_l0;
            void * t17 = GM_LD(void *, (((uint8_t *)t16) + 0));
            gm_tl874t2atnlh3 t18 = gm_l1;
            void * t19 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t17, t18);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t20 = 0;
            F.ret = t19;
            goto gm_fret;
        }
        gm_l3 = t9;
    }
    {
        void * t21 = gm_l0;
        void * t22 = GM_LD(void *, (((uint8_t *)t21) + 0));
        void * t23 = GM_LD(void *, (((uint8_t *)t22) + 0));
        void * t24 = GM_LD(void *, (((uint8_t *)t23) + 0));
        uint32_t t25 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 0));
        void * t26 = gm_f_file_o15fqku(t24, t25);
        gm_l4 = t26;
    }
    {
        void * t27 = gm_l4;
        uint32_t t28 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 4));
        gm_tcc1rq4n1avb2 t29 = GM_CONV(gm_tcc1rq4n1avb2, gm_f_line_col_9vn24mu(t27, t28));
        gm_l5 = t29;
    }
    {
        void * t30 = gm_l0;
        gm_b16_8 t31 = gm_f_location_function_thau9ff(t30);
        gm_l6 = t31;
    }
    {
        gm_f_list_vp1djv1_2((void *)&F.l7, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l7);
        void * t33 = gm_l2;
        gm_t88rcucg1b7uk t34 = gm_f_string_ty_j74ff0o((void *)NULL, cx, NULL, t33);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t35 = gm_l4;
        gm_b16_8 t36 = GM_LD(gm_b16_8, (((uint8_t *)t35) + 0));
        gm_t5brlu7fal70j t37;
        memset(&t37, 0, sizeof t37);
        GM_ST(uint8_t, ((uint8_t *)&t37 + 0), ((uint8_t)5ULL));
        GM_ST(gm_b16_8, ((uint8_t *)&t37 + 8), t36);
        gm_tl874t2atnlh3 t38 = gm_l1;
        void * t39 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t34, &t37, t38);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tftb1odc739t5 t40 = gm_f_push_d943vgp_2(t32, &(*(gm_tfpf0s89fpb3r *)((uint8_t *)t39)));
        gm_tftb1odc739t5 t41 = t40;
        if (((((uint8_t *)&t41))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t41) + 1, gm_srcrorgjlg51brd, 738, 5);
        gm_b0_1 t42 = GM_LD(gm_b0_1, (((uint8_t *)&t41) + 1));
    }
    {
        void * t43 = (void *)((uint8_t *)&F.l7);
        void * t44 = gm_l2;
        gm_t88rcucg1b7uk t45 = gm_f_int_ci6airg((void *)NULL, cx, NULL, t44, ((uint8_t)0), ((uint16_t)32ULL));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint32_t t46 = GM_LD(uint32_t, (((uint8_t *)&gm_l5) + 0));
        uint64_t t47 = (uint64_t)t46;
        gm_tbci5ospk5nnn t48;
        gm_f_from_u64_ud13q5v((void *)&t48, (void *)NULL, cx, NULL, t47);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t5brlu7fal70j t49;
        memset(&t49, 0, sizeof t49);
        GM_ST(uint8_t, ((uint8_t *)&t49 + 0), ((uint8_t)0ULL));
        GM_ST(gm_tbci5ospk5nnn, ((uint8_t *)&t49 + 8), t48);
        gm_tl874t2atnlh3 t50 = gm_l1;
        void * t51 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t45, &t49, t50);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tftb1odc739t5 t52 = gm_f_push_d943vgp_2(t43, &(*(gm_tfpf0s89fpb3r *)((uint8_t *)t51)));
        gm_tftb1odc739t5 t53 = t52;
        if (((((uint8_t *)&t53))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t53) + 1, gm_srcrorgjlg51brd, 739, 5);
        gm_b0_1 t54 = GM_LD(gm_b0_1, (((uint8_t *)&t53) + 1));
    }
    {
        void * t55 = (void *)((uint8_t *)&F.l7);
        void * t56 = gm_l2;
        gm_t88rcucg1b7uk t57 = gm_f_int_ci6airg((void *)NULL, cx, NULL, t56, ((uint8_t)0), ((uint16_t)32ULL));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint32_t t58 = GM_LD(uint32_t, (((uint8_t *)&gm_l5) + 4));
        uint64_t t59 = (uint64_t)t58;
        gm_tbci5ospk5nnn t60;
        gm_f_from_u64_ud13q5v((void *)&t60, (void *)NULL, cx, NULL, t59);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t5brlu7fal70j t61;
        memset(&t61, 0, sizeof t61);
        GM_ST(uint8_t, ((uint8_t *)&t61 + 0), ((uint8_t)0ULL));
        GM_ST(gm_tbci5ospk5nnn, ((uint8_t *)&t61 + 8), t60);
        gm_tl874t2atnlh3 t62 = gm_l1;
        void * t63 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t57, &t61, t62);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tftb1odc739t5 t64 = gm_f_push_d943vgp_2(t55, &(*(gm_tfpf0s89fpb3r *)((uint8_t *)t63)));
        gm_tftb1odc739t5 t65 = t64;
        if (((((uint8_t *)&t65))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t65) + 1, gm_srcrorgjlg51brd, 740, 5);
        gm_b0_1 t66 = GM_LD(gm_b0_1, (((uint8_t *)&t65) + 1));
    }
    {
        void * t67 = (void *)((uint8_t *)&F.l7);
        void * t68 = gm_l2;
        gm_t88rcucg1b7uk t69 = gm_f_string_ty_j74ff0o((void *)NULL, cx, NULL, t68);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b16_8 t70 = gm_l6;
        gm_t5brlu7fal70j t71;
        memset(&t71, 0, sizeof t71);
        GM_ST(uint8_t, ((uint8_t *)&t71 + 0), ((uint8_t)5ULL));
        GM_ST(gm_b16_8, ((uint8_t *)&t71 + 8), t70);
        gm_tl874t2atnlh3 t72 = gm_l1;
        void * t73 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t69, &t71, t72);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tftb1odc739t5 t74 = gm_f_push_d943vgp_2(t67, &(*(gm_tfpf0s89fpb3r *)((uint8_t *)t73)));
        gm_tftb1odc739t5 t75 = t74;
        if (((((uint8_t *)&t75))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t75) + 1, gm_srcrorgjlg51brd, 741, 5);
        gm_b0_1 t76 = GM_LD(gm_b0_1, (((uint8_t *)&t75) + 1));
    }
    {
        gm_t88rcucg1b7uk t77 = gm_l3;
        gm_b16_8 t78 = GM_LD(gm_b16_8, (((uint8_t *)&F.l7) + 0));
        gm_t5brlu7fal70j t79;
        memset(&t79, 0, sizeof t79);
        GM_ST(uint8_t, ((uint8_t *)&t79 + 0), ((uint8_t)35ULL));
        GM_ST(gm_b16_8, ((uint8_t *)&t79 + 8), t78);
        gm_tl874t2atnlh3 t80 = gm_l1;
        void * t81 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t77, &t79, t80);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t82 = 0;
        F.ret = t81;
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
inline __attribute__((aligned(4))) uint64_t gm_f_source_position_taggjb0(gm_b16_8 a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_source_position_taggjb0 F;
    gm_b16_8 gm_l0; /* order */
    uint64_t gm_l1; /* i */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    uint64_t gm_l4; /* k */
    uint64_t gm_l5; /* p */
    GM_STACK_CHECK(&F, 252u);
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
                    uint64_t t11 = gm_l3;
                    uint64_t t12 = (uintptr_t)t11;
                    uint64_t t13 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                    uint64_t t14 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t12 * 8));
                    gm_t2lqu8jsj511d t15;
                    GM_ST(uint64_t, ((uint8_t *)&t15 + 0), t10);
                    GM_ST(uint64_t, ((uint8_t *)&t15 + 8), t14);
                    gm_t2lqu8jsj511d t16 = t15;
                    uint64_t t19 = GM_LD(uint64_t, (((uint8_t *)&t16) + 0));
                    gm_l4 = t19;
                    uint64_t t20 = GM_LD(uint64_t, (((uint8_t *)&t16) + 8));
                    gm_l5 = t20;
                    {
                        uint64_t t23 = gm_l5;
                        uint64_t t24 = gm_l1;
                        uint8_t t25 = ((t23 == t24));
                        if (!t25) goto ie22;
                        {
                            uint64_t t27 = gm_l4;
                            uint64_t r28; bool o29 = __builtin_add_overflow(t27, ((uint64_t)1ULL), &r28);
                            if (o29) gm_trap("integer overflow in `+`", gm_srcrorgjlg51brd, 498, 28);
                            uint64_t t30 = r28;
                            uint8_t t31 = 0;
                            F.ret = t30;
                            goto gm_fret;
                        }
                        goto ix22;
ie22:;
ix22:;
                    }
                    goto me17;
mn18:;
                    gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 497, 5);
me17:;
                }
lc4:;
                {
                    uint64_t t33 = gm_l3;
                    uint64_t r34; bool o35 = __builtin_add_overflow(t33, ((uint64_t)1ULL), &r34);
                    uint64_t t36 = r34;
                    gm_l3 = t36;
                }
            }
lb4:;
        }
    }
    {
        uint8_t t37 = 0;
        F.ret = ((uint64_t)0ULL);
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_span_text_sv6qv18(void * a0, gm_cseahvg8v7p0vs a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_span_text_sv6qv18 F;
    void * gm_l0; /* cx */
    gm_tl874t2atnlh3 gm_l1; /* span */
    GM_STACK_CHECK(&F, 168u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_tl874t2atnlh3, a1);
    void * t1 = gm_l0;
    void * t2 = GM_LD(void *, (((uint8_t *)t1) + 0));
    void * t3 = GM_LD(void *, (((uint8_t *)t2) + 0));
    void * t4 = GM_LD(void *, (((uint8_t *)t3) + 0));
    uint32_t t5 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 0));
    void * t6 = gm_f_file_o15fqku(t4, t5);
    uint64_t t7 = GM_LD(uintptr_t, (((uint8_t *)t6) + 16) + 8);
    void * t8 = ((uint8_t *)GM_LD(void *, (((uint8_t *)t6) + 16)));
    uint32_t t9 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 4));
    uint64_t t10 = (uint64_t)t9;
    uint64_t t11 = (uintptr_t)t10;
    uint32_t t12 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 8));
    uint64_t t13 = (uint64_t)t12;
    uint64_t t14 = (uintptr_t)t13;
    if (t11 > t14 || t14 > (uintptr_t)t7) gm_trap_slice(t11, t14, t7, gm_srcrorgjlg51brd, 699, 60);
    gm_b16_8 t15;
    GM_ST(void *, (uint8_t *)&t15, (void *)(((uint8_t *)t8 + t11 * 1)));
    GM_ST(uintptr_t, (uint8_t *)&t15 + 8, (uintptr_t)(t14 - t11));
    F.ret = t15;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_b0_1 gm_f_too_many_generic_args_bs99n7s(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint8_t a2, gm_b16_8 a3, gm_b16_8 a4, uint64_t a5, gm_tl874t2atnlh3 a6) {
    struct gm_fr_too_many_generic_args_bs99n7s F;
    void * gm_l0; /* cx */
    void * gm_l1; /* sig */
    uint8_t gm_l2; /* has_receiver */
    gm_b16_8 gm_l3; /* named */
    gm_b16_8 gm_l4; /* positional */
    uint64_t gm_l5; /* ntargets */
    gm_tl874t2atnlh3 gm_l6; /* span */
    gm_b16_8 gm_l7; /* what */
    gm_b16_8 gm_l8; /* msg */
    void * gm_l9; /* first */
    gm_b16_8 gm_l10; /* <tmp> */
    uint64_t gm_l11; /* <tmp> */
    uint64_t gm_l12; /* i */
    GM_STACK_CHECK(&F, 1061u);
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
    {
        gm_b16_8 t2;
        uint8_t t4 = gm_l2;
        if (!t4) goto ie3;
        gm_b16_8 t6;
        GM_ST(void *, (uint8_t *)&t6, (void *)(gm_s26emr7fhbmjr));
        GM_ST(uintptr_t, (uint8_t *)&t6 + 8, (uintptr_t)(21));
        gm_b16_8 t7 = t6;
        t2 = t7;
        goto ix3;
ie3:;
        gm_b16_8 t9;
        GM_ST(void *, (uint8_t *)&t9, (void *)(gm_sr4qbrk6tlgaa));
        GM_ST(uintptr_t, (uint8_t *)&t9 + 8, (uintptr_t)(0));
        gm_b16_8 t10 = t9;
        t2 = t10;
ix3:;
        gm_l7 = t2;
    }
    {
        gm_tf1bjg81u0glv t11;
        uint64_t t12 = gm_l5;
        GM_ST(uint64_t, (uint8_t *)&t11 + 0, t12);
        gm_b16_8 t13 = gm_l7;
        GM_ST(gm_b16_8, (uint8_t *)&t11 + 8, t13);
        gm_tabb710d9mq2e t14;
        gm_f_format_dvcvfgj((void *)&t14, (void *)NULL, cx, NULL, &t11);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t15 = t14;
        if (((((uint8_t *)&t15))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t15) + 8, gm_srcrorgjlg51brd, 264, 12);
        gm_b16_8 t16 = GM_LD(gm_b16_8, (((uint8_t *)&t15) + 8));
        gm_l8 = t16;
    }
    {
        uint64_t t17 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t18 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
        if (t17 >= (uintptr_t)(t18)) gm_trap_index(t17, t18, gm_srcrorgjlg51brd, 266, 15);
        void * t19 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t17 * 136);
        gm_l9 = t19;
    }
    {
        void * t21 = gm_l0;
        void * t22 = gm_l9;
        uint8_t t23 = gm_f_clearly_value_f7icrt5(t21, t22);
        uint8_t t24 = (uint8_t)!t23;
        if (!t24) goto ie20;
        void * t25 = gm_l0;
        void * t26 = gm_l9;
        uint8_t t27 = gm_f_denotes_type_jgfet92((void *)NULL, cx, NULL, t25, t26);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (!t27) goto ie20;
        {
            {
                void * t30 = gm_l1;
                gm_b16_8 t31 = GM_LD(gm_b16_8, (((uint8_t *)t30) + 40));
                gm_b16_8 t32 = t31;
                gm_b16_8 t33 = GM_LD(gm_b16_8, ((uint8_t *)&t32));
                gm_l10 = t33;
            }
            {
                gm_l11 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t35 = gm_l11;
                    uint64_t t36 = GM_LD(uintptr_t, ((uint8_t *)&gm_l10) + 8);
                    uint8_t t37 = (t35 < t36);
                    if (!t37) goto lb34;
                    {
                        gm_tgsvmddj3e85e t40;
                        uint64_t t41 = gm_l11;
                        GM_ST(uint64_t, (uint8_t *)&t40 + 0, t41);
                        uint64_t t42 = gm_l11;
                        uint64_t t43 = (uintptr_t)t42;
                        uint64_t t44 = GM_LD(uintptr_t, ((uint8_t *)&gm_l10) + 8);
                        memcpy((uint8_t *)&t40 + 8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l10)) + t43 * 56), 56);
                        gm_tgsvmddj3e85e t45 = t40;
                        uint64_t t48 = GM_LD(uint64_t, (((uint8_t *)&t45) + 0));
                        gm_l12 = t48;
                        gm_t5v0c8ia6a5mo t49 = GM_LD(gm_t5v0c8ia6a5mo, (((uint8_t *)&t45) + 8));
                        F.l13 = t49;
                        {
                            void * t52 = gm_l0;
                            void * t53 = GM_LD(void *, (((uint8_t *)t52) + 0));
                            void * t54 = gm_l1;
                            uint64_t t55 = gm_l12;
                            uint8_t t56 = gm_f_inferable_param_f80lrdi(t53, t54, t55);
                            if (!t56) goto ie51;
                            gm_b16_8 t57 = GM_LD(gm_b16_8, (((uint8_t *)&F.l13) + 0));
                            gm_b16_8 t58 = gm_l3;
                            gm_b16_8 t59 = t58;
                            uint8_t t60 = 0;
                            gm_b16_8 t61 = t57;
                            for (uintptr_t i = 0; i < (uintptr_t)(GM_LD(uintptr_t, ((uint8_t *)&t59) + 8)); i++) if (gm_eq0iqiubfm7e(((uint8_t *)GM_LD(void *, ((uint8_t *)&t59))) + i * 16, ((uint8_t *)&t61))) { t60 = 1; break; }
                            uint8_t t62 = (uint8_t)(!t60);
                            if (!t62) goto ie51;
                            {
                                gm_tfeab279c16rr t64;
                                gm_b16_8 t65 = gm_l8;
                                GM_ST(gm_b16_8, (uint8_t *)&t64 + 0, t65);
                                gm_b16_8 t66 = GM_LD(gm_b16_8, (((uint8_t *)&F.l13) + 0));
                                GM_ST(gm_b16_8, (uint8_t *)&t64 + 16, t66);
                                void * t67 = gm_l0;
                                void * t68 = gm_l9;
                                gm_tl874t2atnlh3 t69 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t68) + 120));
                                gm_b16_8 t70 = gm_f_span_text_sv6qv18(t67, GM_CONV(gm_cseahvg8v7p0vs, t69));
                                GM_ST(gm_b16_8, (uint8_t *)&t64 + 32, t70);
                                gm_tabb710d9mq2e t71;
                                gm_f_format_41hm2he((void *)&t71, (void *)NULL, cx, NULL, &t64);
                                if (gm_unw.kind) {
                                    goto gm_ll38;
                                }
                                gm_tabb710d9mq2e t72 = t71;
                                if (((((uint8_t *)&t72))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t72) + 8, gm_srcrorgjlg51brd, 270, 23);
                                gm_b16_8 t73 = GM_LD(gm_b16_8, (((uint8_t *)&t72) + 8));
                                gm_l8 = t73;
                            }
                            {
                                goto lb34;
                            }
                            goto ix51;
ie51:;
ix51:;
                        }
                        goto me46;
mn47:;
                        gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 268, 9);
me46:;
                    }
lc34:;
                    {
                        uint64_t t75 = gm_l11;
                        uint64_t r76; bool o77 = __builtin_add_overflow(t75, ((uint64_t)1ULL), &r76);
                        uint64_t t78 = r76;
                        gm_l11 = t78;
                    }
                }
gm_ll38:;
                if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb34; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc34; }
                }
                goto gm_lad;
lb34:;
            }
        }
        goto ix20;
ie20:;
ix20:;
    }
    {
        void * t79 = gm_l0;
        gm_tl874t2atnlh3 t80 = gm_l6;
        gm_b16_8 t81 = gm_l8;
        gm_b16_8 t82 = t81;
        gm_b16_8 t83 = GM_LD(gm_b16_8, ((uint8_t *)&t82));
        gm_b0_1 t84 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t79, t80, t83);
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
