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
typedef struct gm_cs9ufftihtbpo2 { uint32_t f0; uint32_t f1; } gm_cs9ufftihtbpo2;
typedef struct gm_cseahvg8v7p0vs { uint32_t f0; uint32_t f1; uint32_t f2; } gm_cseahvg8v7p0vs;
typedef struct gm_csguankko4lmhd { uint32_t f0; } gm_csguankko4lmhd;
typedef struct gm_cskjv6t053tkje { uint32_t f0; uint64_t f1; } gm_cskjv6t053tkje;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint32_t f0; uint8_t f1; uint8_t pe[3]; } gm_t9805qtonbsag;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint32_t f0; uint8_t p1[4]; uint64_t f1; } gm_tb31k5l794t7p;
typedef struct { uint32_t f0; uint32_t f1; } gm_tcc1rq4n1avb2;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint8_t f0; uint8_t p1[7]; void * f1; } gm_tgtub0v94d5u5;
typedef struct { uint8_t f0; uint8_t p1[1]; uint16_t f1; } gm_tjcj4j2gbh6c8;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { void * f0; void * f1; uint32_t f2; uint32_t f3; } gm_tmm67r2c94d5j;
typedef struct { uint64_t f0; uint32_t f1; uint8_t pe[4]; } gm_tn68el8d14vku;
typedef struct { uint32_t f0; uint8_t p1[4]; void * f1; } gm_ttggq7ng4kp3p;
_Static_assert(sizeof(gm_cs9ufftihtbpo2) == 8 && _Alignof(gm_cs9ufftihtbpo2) == 4 && offsetof(gm_cs9ufftihtbpo2, f0) == 0 && offsetof(gm_cs9ufftihtbpo2, f1) == 4, "layout of `LineCol`");
_Static_assert(sizeof(gm_cseahvg8v7p0vs) == 12 && _Alignof(gm_cseahvg8v7p0vs) == 4 && offsetof(gm_cseahvg8v7p0vs, f0) == 0 && offsetof(gm_cseahvg8v7p0vs, f1) == 4 && offsetof(gm_cseahvg8v7p0vs, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_csguankko4lmhd) == 4 && _Alignof(gm_csguankko4lmhd) == 4 && offsetof(gm_csguankko4lmhd, f0) == 0, "layout of `Mutex`");
_Static_assert(sizeof(gm_cskjv6t053tkje) == 16 && _Alignof(gm_cskjv6t053tkje) == 8 && offsetof(gm_cskjv6t053tkje, f0) == 0 && offsetof(gm_cskjv6t053tkje, f1) == 8, "layout of `Char`");
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; void * f2; void * f3; } gm_t710lsrq1m8gk;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
_Static_assert(sizeof(gm_t9805qtonbsag) == 8 && _Alignof(gm_t9805qtonbsag) == 4 && offsetof(gm_t9805qtonbsag, f0) == 0 && offsetof(gm_t9805qtonbsag, f1) == 4, "layout of `?u32`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
_Static_assert(sizeof(gm_tb31k5l794t7p) == 16 && _Alignof(gm_tb31k5l794t7p) == 8 && offsetof(gm_tb31k5l794t7p, f0) == 0 && offsetof(gm_tb31k5l794t7p, f1) == 8, "layout of `Char`");
typedef struct { gm_tl874t2atnlh3 f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tbgd14esf0jvi;
typedef struct { uint8_t f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; } gm_tbngattl1fo51;
_Static_assert(sizeof(gm_tcc1rq4n1avb2) == 8 && _Alignof(gm_tcc1rq4n1avb2) == 4 && offsetof(gm_tcc1rq4n1avb2, f0) == 0 && offsetof(gm_tcc1rq4n1avb2, f1) == 4, "layout of `LineCol`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tcmkompbl09il;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; gm_t88rcucg1b7uk f2; } gm_tcmtrmhj7ak4u;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; gm_b16_8 f1; gm_b16_8 f2; void * f3; gm_t9805qtonbsag f4; uint8_t f5; uint8_t pe[7]; } gm_tdgfduqjheqdm;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; } gm_tgglga8ro3isc;
_Static_assert(sizeof(gm_tgtub0v94d5u5) == 16 && _Alignof(gm_tgtub0v94d5u5) == 8 && offsetof(gm_tgtub0v94d5u5, f0) == 0 && offsetof(gm_tgtub0v94d5u5, f1) == 8, "layout of `(is_const: bool, inner: ^Expr)`");
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
_Static_assert(sizeof(gm_tjcj4j2gbh6c8) == 4 && _Alignof(gm_tjcj4j2gbh6c8) == 2 && offsetof(gm_tjcj4j2gbh6c8, f0) == 0 && offsetof(gm_tjcj4j2gbh6c8, f1) == 2, "layout of `(signed: bool, bits: u16)`");
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_tmm67r2c94d5j) == 24 && _Alignof(gm_tmm67r2c94d5j) == 8 && offsetof(gm_tmm67r2c94d5j, f0) == 0 && offsetof(gm_tmm67r2c94d5j, f1) == 8 && offsetof(gm_tmm67r2c94d5j, f2) == 16 && offsetof(gm_tmm67r2c94d5j, f3) == 20, "layout of `Printer`");
_Static_assert(sizeof(gm_tn68el8d14vku) == 16 && _Alignof(gm_tn68el8d14vku) == 8 && offsetof(gm_tn68el8d14vku, f0) == 0 && offsetof(gm_tn68el8d14vku, f1) == 8, "layout of `(i: usize, x: u32)`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t88rcucg1b7uk v0; } f1; } gm_tnnjeerolos0a;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
typedef struct { gm_b16_8 f0; void * f1; } gm_tpe1hv3fekn05;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
_Static_assert(sizeof(gm_ttggq7ng4kp3p) == 16 && _Alignof(gm_ttggq7ng4kp3p) == 8 && offsetof(gm_ttggq7ng4kp3p, f0) == 0 && offsetof(gm_ttggq7ng4kp3p, f1) == 8, "layout of `(index: u32, payload: ?^Value)`");
typedef struct { gm_tiilmfd16v3fc f0; gm_b16_8 f1; } gm_t1cs2fjnrqap1;
typedef struct { gm_tiilmfd16v3fc f0; uint32_t f1; uint8_t pe[4]; } gm_t419mo45g7j0a;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tcmkompbl09il v0; gm_tiilmfd16v3fc v1; gm_b16_8 v2; } f1; } gm_t428ue2u3hf5g;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t p3[4]; gm_trqfvmjtt4sij f3; gm_trqfvmjtt4sij f4; gm_trqfvmjtt4sij f5; gm_th2lqbo77pvsc f6; } gm_t4gut2c9fj1kt;
typedef struct { uint8_t f0; uint8_t p1[7]; union { void * v0; gm_tpe1hv3fekn05 v1; } f1; } gm_t4lnf71mruqpt;
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_t710lsrq1m8gk) == 40 && _Alignof(gm_t710lsrq1m8gk) == 8 && offsetof(gm_t710lsrq1m8gk, f0) == 0 && offsetof(gm_t710lsrq1m8gk, f1) == 8 && offsetof(gm_t710lsrq1m8gk, f2) == 24 && offsetof(gm_t710lsrq1m8gk, f3) == 32, "layout of `WithExpr`");
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; gm_tnnjeerolos0a f2; uint8_t pe[4]; } gm_t8nckeqfenl8a;
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
typedef struct { void * f0; gm_trqfvmjtt4sij f1; gm_trqfvmjtt4sij f2; gm_trqfvmjtt4sij f3; gm_trqfvmjtt4sij f4; } gm_tb8ondtf8obk4;
_Static_assert(sizeof(gm_tbgd14esf0jvi) == 32 && _Alignof(gm_tbgd14esf0jvi) == 8 && offsetof(gm_tbgd14esf0jvi, f0) == 0 && offsetof(gm_tbgd14esf0jvi, f1) == 16, "layout of `Edit`");
_Static_assert(sizeof(gm_tbngattl1fo51) == 8 && _Alignof(gm_tbngattl1fo51) == 4 && offsetof(gm_tbngattl1fo51, f0) == 0 && offsetof(gm_tbngattl1fo51, f1) == 4, "layout of `(is_const: bool, pointee: Ty)`");
_Static_assert(sizeof(gm_tcmkompbl09il) == 24 && _Alignof(gm_tcmkompbl09il) == 8 && offsetof(gm_tcmkompbl09il, f0) == 0 && offsetof(gm_tcmkompbl09il, f1) == 8, "layout of `(ty: Ty, bytes: string)`");
_Static_assert(sizeof(gm_tcmtrmhj7ak4u) == 12 && _Alignof(gm_tcmtrmhj7ak4u) == 4 && offsetof(gm_tcmtrmhj7ak4u, f0) == 0 && offsetof(gm_tcmtrmhj7ak4u, f1) == 4 && offsetof(gm_tcmtrmhj7ak4u, f2) == 8, "layout of `(op: LenOp, lhs: Ty, rhs: Ty)`");
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t p2[4]; gm_tnpifv20irnph f2; uint8_t f3; uint8_t pe[7]; } gm_tddtjfggdat0d;
_Static_assert(sizeof(gm_tdgfduqjheqdm) == 64 && _Alignof(gm_tdgfduqjheqdm) == 8 && offsetof(gm_tdgfduqjheqdm, f0) == 0 && offsetof(gm_tdgfduqjheqdm, f1) == 8 && offsetof(gm_tdgfduqjheqdm, f2) == 24 && offsetof(gm_tdgfduqjheqdm, f3) == 40 && offsetof(gm_tdgfduqjheqdm, f4) == 48 && offsetof(gm_tdgfduqjheqdm, f5) == 56, "layout of `FnDef`");
typedef struct { gm_tdgfduqjheqdm f0; uint8_t f1; uint8_t pe[7]; } gm_tdn4erebgflsv;
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tbgd14esf0jvi v0; } f1; } gm_tgeh6n0c7c8kb;
_Static_assert(sizeof(gm_tgglga8ro3isc) == 8 && _Alignof(gm_tgglga8ro3isc) == 4 && offsetof(gm_tgglga8ro3isc, f0) == 0 && offsetof(gm_tgglga8ro3isc, f1) == 4, "layout of `(err: Ty, ok: Ty)`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
typedef struct { uint32_t f0; uint8_t p1[4]; gm_trqfvmjtt4sij f1; } gm_tha5675rv512q;
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
_Static_assert(sizeof(gm_tnnjeerolos0a) == 8 && _Alignof(gm_tnnjeerolos0a) == 4 && offsetof(gm_tnnjeerolos0a, f0) == 0 && offsetof(gm_tnnjeerolos0a, f1) == 4, "layout of `ArrayKind`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
_Static_assert(sizeof(gm_tpe1hv3fekn05) == 24 && _Alignof(gm_tpe1hv3fekn05) == 8 && offsetof(gm_tpe1hv3fekn05, f0) == 0 && offsetof(gm_tpe1hv3fekn05, f1) == 16, "layout of `(attrs: []Attribute, inner: ^Expr)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_ttggq7ng4kp3p v0; gm_tpe1hv3fekn05 v1; gm_t3mf58gv9kf3n v2; gm_tpe1hv3fekn05 v3; void * v4; gm_tgtub0v94d5u5 v5; } f1; } gm_trcmmd12cefb1;
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
typedef struct { gm_tiilmfd16v3fc f0; gm_t88rcucg1b7uk f1; uint8_t p2[4]; gm_tnpifv20irnph f2; gm_b16_8 f3; } gm_tvc83pqnepc2e;
_Static_assert(sizeof(gm_t1cs2fjnrqap1) == 40 && _Alignof(gm_t1cs2fjnrqap1) == 8 && offsetof(gm_t1cs2fjnrqap1, f0) == 0 && offsetof(gm_t1cs2fjnrqap1, f1) == 24, "layout of `HandlerLit`");
_Static_assert(sizeof(gm_t419mo45g7j0a) == 32 && _Alignof(gm_t419mo45g7j0a) == 8 && offsetof(gm_t419mo45g7j0a, f0) == 0 && offsetof(gm_t419mo45g7j0a, f1) == 24, "layout of `(effect_: EffectRef, op: u32)`");
_Static_assert(sizeof(gm_t428ue2u3hf5g) == 32 && _Alignof(gm_t428ue2u3hf5g) == 8 && offsetof(gm_t428ue2u3hf5g, f0) == 0 && offsetof(gm_t428ue2u3hf5g, f1) == 8, "layout of `ConstKey`");
_Static_assert(sizeof(gm_t4gut2c9fj1kt) == 184 && _Alignof(gm_t4gut2c9fj1kt) == 8 && offsetof(gm_t4gut2c9fj1kt, f0) == 0 && offsetof(gm_t4gut2c9fj1kt, f1) == 8 && offsetof(gm_t4gut2c9fj1kt, f2) == 24 && offsetof(gm_t4gut2c9fj1kt, f3) == 40 && offsetof(gm_t4gut2c9fj1kt, f4) == 80 && offsetof(gm_t4gut2c9fj1kt, f5) == 120 && offsetof(gm_t4gut2c9fj1kt, f6) == 160, "layout of `Diagnostic`");
_Static_assert(sizeof(gm_t4lnf71mruqpt) == 32 && _Alignof(gm_t4lnf71mruqpt) == 8 && offsetof(gm_t4lnf71mruqpt, f0) == 0 && offsetof(gm_t4lnf71mruqpt, f1) == 8, "layout of `HandlerValue`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tjcj4j2gbh6c8 v0; uint16_t v1; gm_tbngattl1fo51 v2; gm_tbngattl1fo51 v3; gm_tbngattl1fo51 v4; gm_t8nckeqfenl8a v5; gm_t6j6smj7qoctu v6; gm_t88rcucg1b7uk v7; gm_tgglga8ro3isc v8; gm_tddtjfggdat0d v9; gm_tddtjfggdat0d v10; gm_tiilmfd16v3fc v11; gm_t88rcucg1b7uk v12; uint32_t v13; gm_tgglga8ro3isc v14; gm_t428ue2u3hf5g v15; gm_tcmtrmhj7ak4u v16; uint32_t v17; gm_tnpifv20irnph v18; uint32_t v19; } f1; } gm_t53at80pfdus3;
_Static_assert(sizeof(gm_t8nckeqfenl8a) == 24 && _Alignof(gm_t8nckeqfenl8a) == 8 && offsetof(gm_t8nckeqfenl8a, f0) == 0 && offsetof(gm_t8nckeqfenl8a, f1) == 8 && offsetof(gm_t8nckeqfenl8a, f2) == 12, "layout of `(len: u64, elem: Ty, kind: ArrayKind)`");
_Static_assert(sizeof(gm_tb8ondtf8obk4) == 168 && _Alignof(gm_tb8ondtf8obk4) == 8 && offsetof(gm_tb8ondtf8obk4, f0) == 0 && offsetof(gm_tb8ondtf8obk4, f1) == 8 && offsetof(gm_tb8ondtf8obk4, f2) == 48 && offsetof(gm_tb8ondtf8obk4, f3) == 88 && offsetof(gm_tb8ondtf8obk4, f4) == 128, "layout of `StackAnalysis`");
_Static_assert(sizeof(gm_tddtjfggdat0d) == 48 && _Alignof(gm_tddtjfggdat0d) == 8 && offsetof(gm_tddtjfggdat0d, f0) == 0 && offsetof(gm_tddtjfggdat0d, f1) == 16 && offsetof(gm_tddtjfggdat0d, f2) == 24 && offsetof(gm_tddtjfggdat0d, f3) == 40, "layout of `FnTy`");
_Static_assert(sizeof(gm_tdn4erebgflsv) == 72 && _Alignof(gm_tdn4erebgflsv) == 8 && offsetof(gm_tdn4erebgflsv, f0) == 0 && offsetof(gm_tdn4erebgflsv, f1) == 64, "layout of `?FnDef`");
_Static_assert(sizeof(gm_tgeh6n0c7c8kb) == 40 && _Alignof(gm_tgeh6n0c7c8kb) == 8 && offsetof(gm_tgeh6n0c7c8kb, f0) == 0 && offsetof(gm_tgeh6n0c7c8kb, f1) == 8, "layout of `StackFail`");
_Static_assert(sizeof(gm_tha5675rv512q) == 48 && _Alignof(gm_tha5675rv512q) == 8 && offsetof(gm_tha5675rv512q, f0) == 0 && offsetof(gm_tha5675rv512q, f1) == 8, "layout of `StackFrame`");
typedef struct { gm_tha5675rv512q f0; uint8_t f1; uint8_t pe[7]; } gm_tijr008e33o2m;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tgeh6n0c7c8kb v0; uint64_t v1; } f1; } gm_toabfckqu3onu;
_Static_assert(sizeof(gm_trcmmd12cefb1) == 32 && _Alignof(gm_trcmmd12cefb1) == 8 && offsetof(gm_trcmmd12cefb1, f0) == 0 && offsetof(gm_trcmmd12cefb1, f1) == 8, "layout of `Stmt`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint32_t v0; gm_t419mo45g7j0a v1; void * v2; } f1; } gm_tsth9qu1dv0g3;
_Static_assert(sizeof(gm_tvc83pqnepc2e) == 64 && _Alignof(gm_tvc83pqnepc2e) == 8 && offsetof(gm_tvc83pqnepc2e, f0) == 0 && offsetof(gm_tvc83pqnepc2e, f1) == 24 && offsetof(gm_tvc83pqnepc2e, f2) == 32 && offsetof(gm_tvc83pqnepc2e, f3) == 48, "layout of `HandlerDeclInfo`");
_Static_assert(sizeof(gm_t53at80pfdus3) == 56 && _Alignof(gm_t53at80pfdus3) == 8 && offsetof(gm_t53at80pfdus3, f0) == 0 && offsetof(gm_t53at80pfdus3, f1) == 8, "layout of `TyKind`");
_Static_assert(sizeof(gm_tijr008e33o2m) == 56 && _Alignof(gm_tijr008e33o2m) == 8 && offsetof(gm_tijr008e33o2m, f0) == 0 && offsetof(gm_tijr008e33o2m, f1) == 48, "layout of `?StackFrame`");
_Static_assert(sizeof(gm_toabfckqu3onu) == 48 && _Alignof(gm_toabfckqu3onu) == 8 && offsetof(gm_toabfckqu3onu, f0) == 0 && offsetof(gm_toabfckqu3onu, f1) == 8, "layout of `StackFail!u64`");
_Static_assert(sizeof(gm_tsth9qu1dv0g3) == 40 && _Alignof(gm_tsth9qu1dv0g3) == 8 && offsetof(gm_tsth9qu1dv0g3, f0) == 0 && offsetof(gm_tsth9qu1dv0g3, f1) == 8, "layout of `Callee`");
struct gm_fr_add_label_50fj7vv {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_add_note_46v502s {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_bind_fn_pefln9r {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_bound_of_sn02ojm {
    gm_frame h;
    gm_t9805qtonbsag ret;
};
struct gm_fr_def_of_m512sbu {
    gm_frame h;
    gm_tdgfduqjheqdm l2; /* d */
    gm_tdgfduqjheqdm ret;
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
struct gm_fr_empty_slice_fv6i5s8 {
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
struct gm_fr_empty_slice_jbu9fo3 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_laia5g6 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_pp58tj3 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_uk3cnn9 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_vndn3a4 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_error_89glhjr {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_extend_2kurgou {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_fn_t3eo0l3 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_kind_jcnt4hm {
    gm_frame h;
    gm_t53at80pfdus3 ret;
};
struct gm_fr_list_epvducg {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_msjtuv4 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_o00nc4s {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_pe70gj7 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_s1kpkpr {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_uvfu1k1 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_vk99g9b {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_mentions_error_37knu7r {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_pop_d7svgs5 {
    gm_frame h;
    gm_tha5675rv512q l2; /* x */
    gm_tijr008e33o2m ret;
};
struct gm_fr_printer_h7jigd7 {
    gm_frame h;
    gm_tmm67r2c94d5j ret;
};
struct gm_fr_push_12ij5jo {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_1mcf4ec {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_3ef746b {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_9gqhmau {
    gm_frame h;
    gm_tha5675rv512q l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_kiclmcn {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_njfep1h {
    gm_frame h;
    gm_t1cs2fjnrqap1 l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_pf02nv6 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_0jmnnpk {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_1jnet6e {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_5719f1b {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_6u2ahnp {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_8skjmj4 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_gjdu58l {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_k2r4f5o {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_report_eardf07 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_show_ref_aqt1t7c {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* out */
    gm_b16_8 ret;
};
struct gm_fr_stack_call_poqmlno {
    gm_frame h;
    gm_tha5675rv512q l8; /* fr */
    gm_tdgfduqjheqdm l9; /* def */
    gm_trqfvmjtt4sij l11; /* bound */
    gm_trqfvmjtt4sij l21; /* saved */
    gm_toabfckqu3onu l22; /* r */
    gm_toabfckqu3onu ret;
};
struct gm_fr_stack_children_dedraea {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* kids */
    gm_b16_8 ret;
};
struct gm_fr_stack_expr_33aekgt {
    gm_frame h;
    gm_tsth9qu1dv0g3 l3; /* callee */
    gm_t710lsrq1m8gk l23; /* w */
    gm_trqfvmjtt4sij l24; /* installed */
    gm_t1cs2fjnrqap1 l30; /* lit */
    gm_tvc83pqnepc2e l36; /* info */
    gm_t1cs2fjnrqap1 l40; /* h */
    gm_toabfckqu3onu l47; /* r */
    gm_tha5675rv512q l49; /* top */
    gm_tdgfduqjheqdm l50; /* def */
    gm_toabfckqu3onu ret;
};
struct gm_fr_stack_frame_68512ca {
    gm_frame h;
    gm_tdgfduqjheqdm l5; /* def */
    uint64_t ret;
};
struct gm_fr_stack_perform_soivemm {
    gm_frame h;
    gm_t1cs2fjnrqap1 l8; /* h */
    gm_toabfckqu3onu ret;
};
struct gm_fr_stack_push_expr_bdl56rb {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* bound */
    gm_toabfckqu3onu l4; /* r */
    gm_toabfckqu3onu ret;
};
struct gm_fr_stack_size_0gijrqq {
    gm_frame h;
    gm_tb8ondtf8obk4 l3; /* a */
    gm_toabfckqu3onu l4; /* r */
    gm_trqfvmjtt4sij l7; /* chain */
    gm_tha5675rv512q l10; /* fr */
    gm_t4gut2c9fj1kt l12; /* d */
    gm_tf5srh4mp4b3i ret;
};
struct gm_fr_stack_target_7vr78v6 {
    gm_frame h;
    gm_tha5675rv512q l6; /* top */
    gm_tha5675rv512q l9; /* top */
    gm_t9805qtonbsag ret;
};
struct gm_fr_truncate_31h1fac {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_unsupported_t7lgvp9 {
    gm_frame h;
    gm_b0_1 ret;
};
inline __attribute__((aligned(4))) void gm_f_add_label_50fj7vv(void * a0, gm_cseahvg8v7p0vs a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_add_note_46v502s(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_bind_fn_pefln9r(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, uint32_t a2);
__attribute__((aligned(4))) gm_t9805qtonbsag gm_f_bound_of_sn02ojm(void * a0, uint32_t a1);
__attribute__((aligned(4))) int32_t gm_f_cmp_ref_8ntrqll(void * a0, void * a1);
inline __attribute__((aligned(4))) void gm_f_def_of_m512sbu(void *gm_out, void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_effect_ref_6sjtehp(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_emit_dph5v1e(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_5nn7ph0(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_fv6i5s8(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_g3tddrb_2(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_jbu9fo3(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_laia5g6(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_pp58tj3(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_uk3cnn9(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_vndn3a4(void);
__attribute__((aligned(4))) uint8_t gm_f_ensure_body_tbtojbv(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_tl874t2atnlh3 a2);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_error_89glhjr(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_error_s6hn871(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0, gm_b16_8 a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_extend_2kurgou(void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_t3eo0l3(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_for_each_child_fm49s73(void * a0, gm_closure a1);
__attribute__((aligned(4))) void gm_f_format_845n386(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_e79cvtl(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_h4jcodi(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_hp1rthv(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_koh61mc(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_n18n34e(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_t68frbc(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) uint64_t gm_f_frame_cost_oei64tv(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, void * a2);
__attribute__((aligned(4))) void gm_f_get_mk00hfd(void *gm_out, void * a0, uint32_t a1);
__attribute__((aligned(4))) void gm_f_join_sfknljb(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) void gm_f_kind_jcnt4hm(void *gm_out, void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void gm_f_list_epvducg(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_msjtuv4(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_o00nc4s(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_pe70gj7(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_s1kpkpr(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_vk99g9b(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) uint8_t gm_f_mentions_error_37knu7r(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_pop_d7svgs5(void *gm_out, void * a0);
inline __attribute__((aligned(4))) void gm_f_printer_h7jigd7(void *gm_out, void * a0, void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_12ij5jo(void * a0, gm_cs9ufftihtbpo2 a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_1mcf4ec(void * a0, gm_cskjv6t053tkje a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_3ef746b(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_9gqhmau(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_kiclmcn(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_njfep1h(void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_push_o4ckdr1(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_pf02nv6(void * a0, void * a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_0jmnnpk(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_1jnet6e(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_5719f1b(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_6u2ahnp(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_8skjmj4(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_gjdu58l(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_k2r4f5o(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_raw_4ag1s1i(void * a0, uint64_t a1, void * a2, uint64_t a3, uint64_t a4, uint64_t a5);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_report_eardf07(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_ref_aqt1t7c(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) void gm_f_stack_call_poqmlno(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_b16_8 a2, gm_b16_8 a3, gm_tl874t2atnlh3 a4);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_stack_children_dedraea(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_stack_expr_33aekgt(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) uint64_t gm_f_stack_frame_68512ca(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
__attribute__((aligned(4))) void gm_f_stack_perform_soivemm(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint32_t a2, gm_b16_8 a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) void gm_f_stack_push_expr_bdl56rb(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, const void * a2, void * a3);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_stack_size_0gijrqq(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) gm_t9805qtonbsag gm_f_stack_target_7vr78v6(void * a0, void * a1);
inline __attribute__((aligned(4))) void gm_f_truncate_31h1fac(void * a0, uint64_t a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_unsupported_t7lgvp9(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
extern uint8_t gm_c3t36dlh4g5[16] __attribute__((aligned(8)));
extern uint8_t gm_cb7r52n9k7f[8] __attribute__((aligned(8)));
extern uint8_t gm_cdlh2b67kra[8] __attribute__((aligned(8)));
extern uint8_t gm_cilec57f4ei[8] __attribute__((aligned(8)));
extern const uint8_t gm_s2sodn6uclrlt[63];
extern const uint8_t gm_s9impgj2b5t1c[50];
extern const uint8_t gm_sdscrog2npict[23];
extern const uint8_t gm_sdsrtcf8ng48d[6];
extern const uint8_t gm_sg0997vvtiund[65];
extern const char gm_src0ih77f43mhf2[];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_src4tam33qo20j7[];
extern const char gm_srcgfglnv8p059r[];
extern const char gm_srclv0dcbbfhves[];
extern const char gm_srcrgc9bkquf6nr[];
extern const gm_type gm_tyo86ks9qkvt;
inline __attribute__((aligned(4))) void gm_f_add_label_50fj7vv(void * a0, gm_cseahvg8v7p0vs a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_add_label_50fj7vv F;
    void * gm_l0; /* d */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_b16_8 gm_l2; /* message */
    GM_STACK_CHECK(&F, 114u);
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
        if (((((uint8_t *)&t7))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t7) + 1, gm_srcgfglnv8p059r, 92, 5);
        gm_b0_1 t8 = GM_LD(gm_b0_1, (((uint8_t *)&t7) + 1));
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_add_note_46v502s(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_add_note_46v502s F;
    void * gm_l0; /* d */
    gm_b16_8 gm_l1; /* note */
    GM_STACK_CHECK(&F, 58u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        void * t3 = (void *)(((uint8_t *)t2) + 80);
        gm_b16_8 t4 = gm_l1;
        gm_tftb1odc739t5 t5 = gm_f_push_3ef746b(t3, t4);
        if (((((uint8_t *)&t5))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t5) + 1, gm_srcgfglnv8p059r, 96, 5);
        gm_b0_1 t6 = GM_LD(gm_b0_1, (((uint8_t *)&t5) + 1));
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_def_of_m512sbu(void *gm_out, void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_def_of_m512sbu F;
    void * gm_l0; /* s */
    uint32_t gm_l1; /* f */
    GM_STACK_CHECK(&F, 140u);
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
        if (!((((uint8_t *)&t6))[64] != 0)) goto ie2;
        F.l2 = (*(gm_tdgfduqjheqdm *)(((uint8_t *)&t6) + 0));
        {
            memmove(gm_out, ((uint8_t *)&F.l2), 64);
            goto gm_fdone;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        uint32_t t8 = gm_l1;
        uint64_t t9 = (uint64_t)t8;
        uint64_t t10 = (uintptr_t)t9;
        void * t11 = gm_l0;
        uint64_t t12 = GM_LD(uintptr_t, ((((uint8_t *)t11) + 384) + 0) + 8);
        if (t10 >= (uintptr_t)(t12)) gm_trap_index(t10, t12, gm_srclv0dcbbfhves, 581, 12);
        memmove(gm_out, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t11) + 384) + 0)) + t10 * 64), 64);
        goto gm_fdone;
    }
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_fv6i5s8(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_fv6i5s8 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_jbu9fo3(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_jbu9fo3 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_laia5g6(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_laia5g6 F;
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
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 48)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_pp58tj3(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_pp58tj3 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_uk3cnn9(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_uk3cnn9 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_vndn3a4(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_vndn3a4 F;
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
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 40)));
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_extend_2kurgou(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_extend_2kurgou F;
    void * gm_l0; /* l */
    gm_b16_8 gm_l1; /* xs */
    uint64_t gm_l2; /* n */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        uint64_t t3 = GM_LD(uintptr_t, (((uint8_t *)t2) + 0) + 8);
        gm_l2 = t3;
    }
    {
        void * t5 = gm_l0;
        uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)t5) + 16));
        uint64_t t7 = gm_l2;
        uint64_t r8; bool o9 = __builtin_sub_overflow(t6, t7, &r8);
        if (o9) gm_trap("integer overflow in `-`", gm_srcrgc9bkquf6nr, 80, 8);
        uint64_t t10 = r8;
        uint64_t t11 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint8_t t12 = (t10 < t11);
        if (!t12) goto ie4;
        {
            void * t14 = gm_l0;
            uint64_t t15 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            gm_b16_8 t16 = gm_l1;
            gm_tftb1odc739t5 t17 = gm_f_regrow_6u2ahnp(t14, t15, t16);
            if (((((uint8_t *)&t17))[0] != 0)) {
                gm_t9qkh0rsooc8d t18 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&t17) + 1));
                gm_tftb1odc739t5 t19;
                ((uint8_t *)&t19)[0] = 1;
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t19 + 1), t18);
                F.ret = t19;
                goto gm_fret;
            }
            gm_b0_1 t20 = GM_LD(gm_b0_1, (((uint8_t *)&t17) + 1));
        }
        goto ix4;
ie4:;
        {
            void * t22 = gm_l0;
            void * t23 = GM_LD(void *, (((uint8_t *)t22) + 0));
            void * t24 = ((uint8_t *)t23);
            uint64_t t25 = gm_l2;
            uint64_t t26 = (uintptr_t)t25;
            uint64_t t27 = gm_l2;
            uint64_t t28 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            uint64_t r29; bool o30 = __builtin_add_overflow(t27, t28, &r29);
            if (o30) gm_trap("integer overflow in `+`", gm_srcrgc9bkquf6nr, 84, 36);
            uint64_t t31 = r29;
            uint64_t t32 = (uintptr_t)t31;
            gm_b16_8 t33;
            GM_ST(void *, (uint8_t *)&t33, (void *)(((uint8_t *)t24 + t26 * 40)));
            GM_ST(uintptr_t, (uint8_t *)&t33 + 8, (uintptr_t)(t32 - t26));
            gm_b16_8 t34 = gm_l1;
            if (GM_LD(uintptr_t, ((uint8_t *)&t33) + 8) != 0) memmove(GM_LD(void *, ((uint8_t *)&t33)), GM_LD(void *, ((uint8_t *)&t34)), GM_LD(uintptr_t, ((uint8_t *)&t33) + 8) * 40);
        }
ix4:;
    }
    {
        void * t35 = gm_l0;
        void * t36 = gm_l0;
        void * t37 = GM_LD(void *, (((uint8_t *)t36) + 0));
        void * t38 = ((uint8_t *)t37);
        uint64_t t39 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t40 = gm_l2;
        uint64_t t41 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint64_t r42; bool o43 = __builtin_add_overflow(t40, t41, &r42);
        if (o43) gm_trap("integer overflow in `+`", gm_srcrgc9bkquf6nr, 86, 30);
        uint64_t t44 = r42;
        uint64_t t45 = (uintptr_t)t44;
        gm_b16_8 t46;
        GM_ST(void *, (uint8_t *)&t46, (void *)(((uint8_t *)t38 + t39 * 40)));
        GM_ST(uintptr_t, (uint8_t *)&t46 + 8, (uintptr_t)(t45 - t39));
        GM_ST(gm_b16_8, (((uint8_t *)t35) + 0), t46);
    }
    {
        gm_b0_1 t47;
        gm_tftb1odc739t5 t48;
        GM_ST(uint8_t, (uint8_t *)&t48 + 1, 0);
        ((uint8_t *)&t48)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t48 + 1), t47);
        uint8_t t49 = ((((uint8_t *)&t48))[0] != 0);
        F.ret = t48;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_t3eo0l3(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_fn_t3eo0l3 F;
    void * gm_l0; /* c */
    GM_STACK_CHECK(&F, 26u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = (void *)((uint8_t *)&((struct gm_fr_stack_children_dedraea *)((&F.h)->up))->l1);
        void * t3 = gm_l0;
        gm_tftb1odc739t5 t4 = gm_f_push_pf02nv6(t2, t3);
        if (((((uint8_t *)&t4))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t4) + 1, gm_src4tam33qo20j7, 283, 42);
        gm_b0_1 t5 = GM_LD(gm_b0_1, (((uint8_t *)&t4) + 1));
    }
    F.ret = GM_ZERO(gm_b0_1);
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
inline __attribute__((aligned(4))) void gm_f_list_msjtuv4(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_msjtuv4 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_pp58tj3();
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
inline __attribute__((aligned(4))) void gm_f_list_o00nc4s(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_o00nc4s F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_vndn3a4();
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
inline __attribute__((aligned(4))) void gm_f_list_pe70gj7(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_pe70gj7 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_laia5g6();
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
inline __attribute__((aligned(4))) void gm_f_list_s1kpkpr(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_s1kpkpr F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_fv6i5s8();
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
inline __attribute__((aligned(4))) void gm_f_list_vk99g9b(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_vk99g9b F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_uk3cnn9();
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
inline __attribute__((aligned(4))) uint8_t gm_f_mentions_error_37knu7r(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_mentions_error_37knu7r F;
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
    if (t3 >= (uintptr_t)(t5)) gm_trap_index(t3, t5, gm_src0ih77f43mhf2, 255, 62);
    uint8_t t6 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t4) + 40) + 0)) + t3 * 1));
    uint8_t t7 = (uint8_t)(t6 & ((uint8_t)4ULL));
    uint8_t t8 = (!(t7 == ((uint8_t)0ULL)));
    F.ret = t8;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_pop_d7svgs5(void *gm_out, void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_pop_d7svgs5 F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* n */
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
            gm_tijr008e33o2m t8;
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
        if (o12) gm_trap("integer overflow in `-`", gm_srcrgc9bkquf6nr, 133, 18);
        uint64_t t13 = r11;
        uint64_t t14 = (uintptr_t)t13;
        void * t15 = gm_l0;
        uint64_t t16 = GM_LD(uintptr_t, (((uint8_t *)t15) + 0) + 8);
        if (t14 >= (uintptr_t)(t16)) gm_trap_index(t14, t16, gm_srcrgc9bkquf6nr, 133, 10);
        gm_tha5675rv512q t17 = GM_LD(gm_tha5675rv512q, ((uint8_t *)GM_LD(void *, (((uint8_t *)t15) + 0)) + t14 * 48));
        F.l2 = t17;
    }
    {
        void * t18 = gm_l0;
        void * t19 = gm_l0;
        uint64_t t20 = GM_LD(uintptr_t, (((uint8_t *)t19) + 0) + 8);
        void * t21 = ((uint8_t *)GM_LD(void *, (((uint8_t *)t19) + 0)));
        uint64_t t22 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t23 = gm_l1;
        uint64_t r24; bool o25 = __builtin_sub_overflow(t23, ((uint64_t)1ULL), &r24);
        if (o25) gm_trap("integer overflow in `-`", gm_srcrgc9bkquf6nr, 134, 26);
        uint64_t t26 = r24;
        uint64_t t27 = (uintptr_t)t26;
        if (t22 > t27 || t27 > (uintptr_t)t20) gm_trap_slice(t22, t27, t20, gm_srcrgc9bkquf6nr, 134, 15);
        gm_b16_8 t28;
        GM_ST(void *, (uint8_t *)&t28, (void *)(((uint8_t *)t21 + t22 * 48)));
        GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(t27 - t22));
        GM_ST(gm_b16_8, (((uint8_t *)t18) + 0), t28);
    }
    {
        const uint8_t *t29 = ((uint8_t *)&F.l2);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tijr008e33o2m *)gm_out) + 49, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tijr008e33o2m *)gm_out) + 50, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tijr008e33o2m *)gm_out) + 52, 0);
        ((uint8_t *)&(*(gm_tijr008e33o2m *)gm_out))[48] = 1;
        memmove((uint8_t *)&(*(gm_tijr008e33o2m *)gm_out) + 0, t29, 48);
        goto gm_fdone;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_12ij5jo(void * a0, gm_cs9ufftihtbpo2 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_12ij5jo F;
    void * gm_l0; /* l */
    gm_tcc1rq4n1avb2 gm_l1; /* x */
    uint64_t gm_l2; /* n */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_tcc1rq4n1avb2, a1);
    {
        void * t2 = gm_l0;
        uint64_t t3 = GM_LD(uintptr_t, (((uint8_t *)t2) + 0) + 8);
        gm_l2 = t3;
    }
    {
        void * t5 = gm_l0;
        uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)t5) + 16));
        uint64_t t7 = gm_l2;
        uint8_t t8 = ((t6 == t7));
        if (!t8) goto ie4;
        {
            void * t10 = gm_l0;
            gm_b16_8 t11 = gm_f_empty_slice_uk3cnn9();
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
            gm_tftb1odc739t5 t13 = gm_f_regrow_1jnet6e(t10, ((uint64_t)1ULL), t12);
            if (((((uint8_t *)&t13))[0] != 0)) {
                gm_t9qkh0rsooc8d t14 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&t13) + 1));
                gm_tftb1odc739t5 t15;
                ((uint8_t *)&t15)[0] = 1;
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t15 + 1), t14);
                F.ret = t15;
                goto gm_fret;
            }
            gm_b0_1 t16 = GM_LD(gm_b0_1, (((uint8_t *)&t13) + 1));
        }
        goto ix4;
ie4:;
ix4:;
    }
    {
        uint64_t t17 = gm_l2;
        uint64_t t18 = (uintptr_t)t17;
        void * t19 = gm_l0;
        void * t20 = GM_LD(void *, (((uint8_t *)t19) + 0));
        gm_tcc1rq4n1avb2 t21 = gm_l1;
        GM_ST(gm_tcc1rq4n1avb2, ((uint8_t *)t20 + t18 * 8), t21);
    }
    {
        void * t22 = gm_l0;
        void * t23 = gm_l0;
        void * t24 = GM_LD(void *, (((uint8_t *)t23) + 0));
        void * t25 = ((uint8_t *)t24);
        uint64_t t26 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t27 = gm_l2;
        uint64_t r28; bool o29 = __builtin_add_overflow(t27, ((uint64_t)1ULL), &r28);
        uint64_t t30 = r28;
        uint64_t t31 = (uintptr_t)t30;
        gm_b16_8 t32;
        GM_ST(void *, (uint8_t *)&t32, (void *)(((uint8_t *)t25 + t26 * 8)));
        GM_ST(uintptr_t, (uint8_t *)&t32 + 8, (uintptr_t)(t31 - t26));
        GM_ST(gm_b16_8, (((uint8_t *)t22) + 0), t32);
    }
    {
        gm_b0_1 t33;
        gm_tftb1odc739t5 t34;
        GM_ST(uint8_t, (uint8_t *)&t34 + 1, 0);
        ((uint8_t *)&t34)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t34 + 1), t33);
        uint8_t t35 = ((((uint8_t *)&t34))[0] != 0);
        F.ret = t34;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_1mcf4ec(void * a0, gm_cskjv6t053tkje a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_1mcf4ec F;
    void * gm_l0; /* l */
    gm_tb31k5l794t7p gm_l1; /* x */
    uint64_t gm_l2; /* n */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_tb31k5l794t7p, a1);
    {
        void * t2 = gm_l0;
        uint64_t t3 = GM_LD(uintptr_t, (((uint8_t *)t2) + 0) + 8);
        gm_l2 = t3;
    }
    {
        void * t5 = gm_l0;
        uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)t5) + 16));
        uint64_t t7 = gm_l2;
        uint8_t t8 = ((t6 == t7));
        if (!t8) goto ie4;
        {
            void * t10 = gm_l0;
            gm_b16_8 t11 = gm_f_empty_slice_pp58tj3();
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
            gm_tftb1odc739t5 t13 = gm_f_regrow_k2r4f5o(t10, ((uint64_t)1ULL), t12);
            if (((((uint8_t *)&t13))[0] != 0)) {
                gm_t9qkh0rsooc8d t14 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&t13) + 1));
                gm_tftb1odc739t5 t15;
                ((uint8_t *)&t15)[0] = 1;
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t15 + 1), t14);
                F.ret = t15;
                goto gm_fret;
            }
            gm_b0_1 t16 = GM_LD(gm_b0_1, (((uint8_t *)&t13) + 1));
        }
        goto ix4;
ie4:;
ix4:;
    }
    {
        uint64_t t17 = gm_l2;
        uint64_t t18 = (uintptr_t)t17;
        void * t19 = gm_l0;
        void * t20 = GM_LD(void *, (((uint8_t *)t19) + 0));
        gm_tb31k5l794t7p t21 = gm_l1;
        GM_ST(gm_tb31k5l794t7p, ((uint8_t *)t20 + t18 * 16), t21);
    }
    {
        void * t22 = gm_l0;
        void * t23 = gm_l0;
        void * t24 = GM_LD(void *, (((uint8_t *)t23) + 0));
        void * t25 = ((uint8_t *)t24);
        uint64_t t26 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t27 = gm_l2;
        uint64_t r28; bool o29 = __builtin_add_overflow(t27, ((uint64_t)1ULL), &r28);
        uint64_t t30 = r28;
        uint64_t t31 = (uintptr_t)t30;
        gm_b16_8 t32;
        GM_ST(void *, (uint8_t *)&t32, (void *)(((uint8_t *)t25 + t26 * 16)));
        GM_ST(uintptr_t, (uint8_t *)&t32 + 8, (uintptr_t)(t31 - t26));
        GM_ST(gm_b16_8, (((uint8_t *)t22) + 0), t32);
    }
    {
        gm_b0_1 t33;
        gm_tftb1odc739t5 t34;
        GM_ST(uint8_t, (uint8_t *)&t34 + 1, 0);
        ((uint8_t *)&t34)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t34 + 1), t33);
        uint8_t t35 = ((((uint8_t *)&t34))[0] != 0);
        F.ret = t34;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_3ef746b(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_3ef746b F;
    void * gm_l0; /* l */
    gm_b16_8 gm_l1; /* x */
    uint64_t gm_l2; /* n */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        uint64_t t3 = GM_LD(uintptr_t, (((uint8_t *)t2) + 0) + 8);
        gm_l2 = t3;
    }
    {
        void * t5 = gm_l0;
        uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)t5) + 16));
        uint64_t t7 = gm_l2;
        uint8_t t8 = ((t6 == t7));
        if (!t8) goto ie4;
        {
            void * t10 = gm_l0;
            gm_b16_8 t11 = gm_f_empty_slice_5nn7ph0();
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
            gm_tftb1odc739t5 t13 = gm_f_regrow_0jmnnpk(t10, ((uint64_t)1ULL), t12);
            if (((((uint8_t *)&t13))[0] != 0)) {
                gm_t9qkh0rsooc8d t14 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&t13) + 1));
                gm_tftb1odc739t5 t15;
                ((uint8_t *)&t15)[0] = 1;
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t15 + 1), t14);
                F.ret = t15;
                goto gm_fret;
            }
            gm_b0_1 t16 = GM_LD(gm_b0_1, (((uint8_t *)&t13) + 1));
        }
        goto ix4;
ie4:;
ix4:;
    }
    {
        uint64_t t17 = gm_l2;
        uint64_t t18 = (uintptr_t)t17;
        void * t19 = gm_l0;
        void * t20 = GM_LD(void *, (((uint8_t *)t19) + 0));
        gm_b16_8 t21 = gm_l1;
        GM_ST(gm_b16_8, ((uint8_t *)t20 + t18 * 16), t21);
    }
    {
        void * t22 = gm_l0;
        void * t23 = gm_l0;
        void * t24 = GM_LD(void *, (((uint8_t *)t23) + 0));
        void * t25 = ((uint8_t *)t24);
        uint64_t t26 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t27 = gm_l2;
        uint64_t r28; bool o29 = __builtin_add_overflow(t27, ((uint64_t)1ULL), &r28);
        uint64_t t30 = r28;
        uint64_t t31 = (uintptr_t)t30;
        gm_b16_8 t32;
        GM_ST(void *, (uint8_t *)&t32, (void *)(((uint8_t *)t25 + t26 * 16)));
        GM_ST(uintptr_t, (uint8_t *)&t32 + 8, (uintptr_t)(t31 - t26));
        GM_ST(gm_b16_8, (((uint8_t *)t22) + 0), t32);
    }
    {
        gm_b0_1 t33;
        gm_tftb1odc739t5 t34;
        GM_ST(uint8_t, (uint8_t *)&t34 + 1, 0);
        ((uint8_t *)&t34)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t34 + 1), t33);
        uint8_t t35 = ((((uint8_t *)&t34))[0] != 0);
        F.ret = t34;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_9gqhmau(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_9gqhmau F;
    void * gm_l0; /* l */
    uint64_t gm_l2; /* n */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        uint64_t t3 = GM_LD(uintptr_t, (((uint8_t *)t2) + 0) + 8);
        gm_l2 = t3;
    }
    {
        void * t5 = gm_l0;
        uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)t5) + 16));
        uint64_t t7 = gm_l2;
        uint8_t t8 = ((t6 == t7));
        if (!t8) goto ie4;
        {
            void * t10 = gm_l0;
            gm_b16_8 t11 = gm_f_empty_slice_laia5g6();
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
            gm_tftb1odc739t5 t13 = gm_f_regrow_gjdu58l(t10, ((uint64_t)1ULL), t12);
            if (((((uint8_t *)&t13))[0] != 0)) {
                gm_t9qkh0rsooc8d t14 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&t13) + 1));
                gm_tftb1odc739t5 t15;
                ((uint8_t *)&t15)[0] = 1;
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t15 + 1), t14);
                F.ret = t15;
                goto gm_fret;
            }
            gm_b0_1 t16 = GM_LD(gm_b0_1, (((uint8_t *)&t13) + 1));
        }
        goto ix4;
ie4:;
ix4:;
    }
    {
        uint64_t t17 = gm_l2;
        uint64_t t18 = (uintptr_t)t17;
        void * t19 = gm_l0;
        void * t20 = GM_LD(void *, (((uint8_t *)t19) + 0));
        gm_tha5675rv512q t21 = GM_LD(gm_tha5675rv512q, ((uint8_t *)a1));
        GM_ST(gm_tha5675rv512q, ((uint8_t *)t20 + t18 * 48), t21);
    }
    {
        void * t22 = gm_l0;
        void * t23 = gm_l0;
        void * t24 = GM_LD(void *, (((uint8_t *)t23) + 0));
        void * t25 = ((uint8_t *)t24);
        uint64_t t26 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t27 = gm_l2;
        uint64_t r28; bool o29 = __builtin_add_overflow(t27, ((uint64_t)1ULL), &r28);
        uint64_t t30 = r28;
        uint64_t t31 = (uintptr_t)t30;
        gm_b16_8 t32;
        GM_ST(void *, (uint8_t *)&t32, (void *)(((uint8_t *)t25 + t26 * 48)));
        GM_ST(uintptr_t, (uint8_t *)&t32 + 8, (uintptr_t)(t31 - t26));
        GM_ST(gm_b16_8, (((uint8_t *)t22) + 0), t32);
    }
    {
        gm_b0_1 t33;
        gm_tftb1odc739t5 t34;
        GM_ST(uint8_t, (uint8_t *)&t34 + 1, 0);
        ((uint8_t *)&t34)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t34 + 1), t33);
        uint8_t t35 = ((((uint8_t *)&t34))[0] != 0);
        F.ret = t34;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_kiclmcn(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_kiclmcn F;
    void * gm_l0; /* l */
    gm_tbgd14esf0jvi gm_l1; /* x */
    uint64_t gm_l2; /* n */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        uint64_t t3 = GM_LD(uintptr_t, (((uint8_t *)t2) + 0) + 8);
        gm_l2 = t3;
    }
    {
        void * t5 = gm_l0;
        uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)t5) + 16));
        uint64_t t7 = gm_l2;
        uint8_t t8 = ((t6 == t7));
        if (!t8) goto ie4;
        {
            void * t10 = gm_l0;
            gm_b16_8 t11 = gm_f_empty_slice_jbu9fo3();
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
            gm_tftb1odc739t5 t13 = gm_f_regrow_8skjmj4(t10, ((uint64_t)1ULL), t12);
            if (((((uint8_t *)&t13))[0] != 0)) {
                gm_t9qkh0rsooc8d t14 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&t13) + 1));
                gm_tftb1odc739t5 t15;
                ((uint8_t *)&t15)[0] = 1;
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t15 + 1), t14);
                F.ret = t15;
                goto gm_fret;
            }
            gm_b0_1 t16 = GM_LD(gm_b0_1, (((uint8_t *)&t13) + 1));
        }
        goto ix4;
ie4:;
ix4:;
    }
    {
        uint64_t t17 = gm_l2;
        uint64_t t18 = (uintptr_t)t17;
        void * t19 = gm_l0;
        void * t20 = GM_LD(void *, (((uint8_t *)t19) + 0));
        gm_tbgd14esf0jvi t21 = GM_LD(gm_tbgd14esf0jvi, ((uint8_t *)a1));
        GM_ST(gm_tbgd14esf0jvi, ((uint8_t *)t20 + t18 * 32), t21);
    }
    {
        void * t22 = gm_l0;
        void * t23 = gm_l0;
        void * t24 = GM_LD(void *, (((uint8_t *)t23) + 0));
        void * t25 = ((uint8_t *)t24);
        uint64_t t26 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t27 = gm_l2;
        uint64_t r28; bool o29 = __builtin_add_overflow(t27, ((uint64_t)1ULL), &r28);
        uint64_t t30 = r28;
        uint64_t t31 = (uintptr_t)t30;
        gm_b16_8 t32;
        GM_ST(void *, (uint8_t *)&t32, (void *)(((uint8_t *)t25 + t26 * 32)));
        GM_ST(uintptr_t, (uint8_t *)&t32 + 8, (uintptr_t)(t31 - t26));
        GM_ST(gm_b16_8, (((uint8_t *)t22) + 0), t32);
    }
    {
        gm_b0_1 t33;
        gm_tftb1odc739t5 t34;
        GM_ST(uint8_t, (uint8_t *)&t34 + 1, 0);
        ((uint8_t *)&t34)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t34 + 1), t33);
        uint8_t t35 = ((((uint8_t *)&t34))[0] != 0);
        F.ret = t34;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_njfep1h(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_njfep1h F;
    void * gm_l0; /* l */
    uint64_t gm_l2; /* n */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        uint64_t t3 = GM_LD(uintptr_t, (((uint8_t *)t2) + 0) + 8);
        gm_l2 = t3;
    }
    {
        void * t5 = gm_l0;
        uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)t5) + 16));
        uint64_t t7 = gm_l2;
        uint8_t t8 = ((t6 == t7));
        if (!t8) goto ie4;
        {
            void * t10 = gm_l0;
            gm_b16_8 t11 = gm_f_empty_slice_vndn3a4();
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
            gm_tftb1odc739t5 t13 = gm_f_regrow_6u2ahnp(t10, ((uint64_t)1ULL), t12);
            if (((((uint8_t *)&t13))[0] != 0)) {
                gm_t9qkh0rsooc8d t14 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&t13) + 1));
                gm_tftb1odc739t5 t15;
                ((uint8_t *)&t15)[0] = 1;
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t15 + 1), t14);
                F.ret = t15;
                goto gm_fret;
            }
            gm_b0_1 t16 = GM_LD(gm_b0_1, (((uint8_t *)&t13) + 1));
        }
        goto ix4;
ie4:;
ix4:;
    }
    {
        uint64_t t17 = gm_l2;
        uint64_t t18 = (uintptr_t)t17;
        void * t19 = gm_l0;
        void * t20 = GM_LD(void *, (((uint8_t *)t19) + 0));
        gm_t1cs2fjnrqap1 t21 = GM_LD(gm_t1cs2fjnrqap1, ((uint8_t *)a1));
        GM_ST(gm_t1cs2fjnrqap1, ((uint8_t *)t20 + t18 * 40), t21);
    }
    {
        void * t22 = gm_l0;
        void * t23 = gm_l0;
        void * t24 = GM_LD(void *, (((uint8_t *)t23) + 0));
        void * t25 = ((uint8_t *)t24);
        uint64_t t26 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t27 = gm_l2;
        uint64_t r28; bool o29 = __builtin_add_overflow(t27, ((uint64_t)1ULL), &r28);
        uint64_t t30 = r28;
        uint64_t t31 = (uintptr_t)t30;
        gm_b16_8 t32;
        GM_ST(void *, (uint8_t *)&t32, (void *)(((uint8_t *)t25 + t26 * 40)));
        GM_ST(uintptr_t, (uint8_t *)&t32 + 8, (uintptr_t)(t31 - t26));
        GM_ST(gm_b16_8, (((uint8_t *)t22) + 0), t32);
    }
    {
        gm_b0_1 t33;
        gm_tftb1odc739t5 t34;
        GM_ST(uint8_t, (uint8_t *)&t34 + 1, 0);
        ((uint8_t *)&t34)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t34 + 1), t33);
        uint8_t t35 = ((((uint8_t *)&t34))[0] != 0);
        F.ret = t34;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_pf02nv6(void * a0, void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_pf02nv6 F;
    void * gm_l0; /* l */
    void * gm_l1; /* x */
    uint64_t gm_l2; /* n */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        uint64_t t3 = GM_LD(uintptr_t, (((uint8_t *)t2) + 0) + 8);
        gm_l2 = t3;
    }
    {
        void * t5 = gm_l0;
        uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)t5) + 16));
        uint64_t t7 = gm_l2;
        uint8_t t8 = ((t6 == t7));
        if (!t8) goto ie4;
        {
            void * t10 = gm_l0;
            gm_b16_8 t11 = gm_f_empty_slice_fv6i5s8();
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
            gm_tftb1odc739t5 t13 = gm_f_regrow_5719f1b(t10, ((uint64_t)1ULL), t12);
            if (((((uint8_t *)&t13))[0] != 0)) {
                gm_t9qkh0rsooc8d t14 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&t13) + 1));
                gm_tftb1odc739t5 t15;
                ((uint8_t *)&t15)[0] = 1;
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t15 + 1), t14);
                F.ret = t15;
                goto gm_fret;
            }
            gm_b0_1 t16 = GM_LD(gm_b0_1, (((uint8_t *)&t13) + 1));
        }
        goto ix4;
ie4:;
ix4:;
    }
    {
        uint64_t t17 = gm_l2;
        uint64_t t18 = (uintptr_t)t17;
        void * t19 = gm_l0;
        void * t20 = GM_LD(void *, (((uint8_t *)t19) + 0));
        void * t21 = gm_l1;
        GM_ST(void *, ((uint8_t *)t20 + t18 * 8), t21);
    }
    {
        void * t22 = gm_l0;
        void * t23 = gm_l0;
        void * t24 = GM_LD(void *, (((uint8_t *)t23) + 0));
        void * t25 = ((uint8_t *)t24);
        uint64_t t26 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t27 = gm_l2;
        uint64_t r28; bool o29 = __builtin_add_overflow(t27, ((uint64_t)1ULL), &r28);
        uint64_t t30 = r28;
        uint64_t t31 = (uintptr_t)t30;
        gm_b16_8 t32;
        GM_ST(void *, (uint8_t *)&t32, (void *)(((uint8_t *)t25 + t26 * 8)));
        GM_ST(uintptr_t, (uint8_t *)&t32 + 8, (uintptr_t)(t31 - t26));
        GM_ST(gm_b16_8, (((uint8_t *)t22) + 0), t32);
    }
    {
        gm_b0_1 t33;
        gm_tftb1odc739t5 t34;
        GM_ST(uint8_t, (uint8_t *)&t34 + 1, 0);
        ((uint8_t *)&t34)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t34 + 1), t33);
        uint8_t t35 = ((((uint8_t *)&t34))[0] != 0);
        F.ret = t34;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_0jmnnpk(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_0jmnnpk F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* extra */
    gm_b16_8 gm_l2; /* tail */
    GM_STACK_CHECK(&F, 87u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        void * t3 = t2;
        uint64_t t4 = gm_l1;
        void * t5 = GM_LD(void *, ((uint8_t *)&gm_l2));
        void * t6 = t5;
        uint64_t t7 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)16ULL), ((uint64_t)8ULL));
        uint8_t t9 = ((((uint8_t *)&t8))[0] != 0);
        F.ret = t8;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_1jnet6e(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_1jnet6e F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* extra */
    gm_b16_8 gm_l2; /* tail */
    GM_STACK_CHECK(&F, 87u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        void * t3 = t2;
        uint64_t t4 = gm_l1;
        void * t5 = GM_LD(void *, ((uint8_t *)&gm_l2));
        void * t6 = t5;
        uint64_t t7 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)8ULL), ((uint64_t)4ULL));
        uint8_t t9 = ((((uint8_t *)&t8))[0] != 0);
        F.ret = t8;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_5719f1b(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_5719f1b F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* extra */
    gm_b16_8 gm_l2; /* tail */
    GM_STACK_CHECK(&F, 87u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        void * t3 = t2;
        uint64_t t4 = gm_l1;
        void * t5 = GM_LD(void *, ((uint8_t *)&gm_l2));
        void * t6 = t5;
        uint64_t t7 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)8ULL), ((uint64_t)8ULL));
        uint8_t t9 = ((((uint8_t *)&t8))[0] != 0);
        F.ret = t8;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_6u2ahnp(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_6u2ahnp F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* extra */
    gm_b16_8 gm_l2; /* tail */
    GM_STACK_CHECK(&F, 87u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        void * t3 = t2;
        uint64_t t4 = gm_l1;
        void * t5 = GM_LD(void *, ((uint8_t *)&gm_l2));
        void * t6 = t5;
        uint64_t t7 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)40ULL), ((uint64_t)8ULL));
        uint8_t t9 = ((((uint8_t *)&t8))[0] != 0);
        F.ret = t8;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_8skjmj4(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_8skjmj4 F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* extra */
    gm_b16_8 gm_l2; /* tail */
    GM_STACK_CHECK(&F, 87u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        void * t3 = t2;
        uint64_t t4 = gm_l1;
        void * t5 = GM_LD(void *, ((uint8_t *)&gm_l2));
        void * t6 = t5;
        uint64_t t7 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)32ULL), ((uint64_t)8ULL));
        uint8_t t9 = ((((uint8_t *)&t8))[0] != 0);
        F.ret = t8;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_gjdu58l(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_gjdu58l F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* extra */
    gm_b16_8 gm_l2; /* tail */
    GM_STACK_CHECK(&F, 87u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        void * t3 = t2;
        uint64_t t4 = gm_l1;
        void * t5 = GM_LD(void *, ((uint8_t *)&gm_l2));
        void * t6 = t5;
        uint64_t t7 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)48ULL), ((uint64_t)8ULL));
        uint8_t t9 = ((((uint8_t *)&t8))[0] != 0);
        F.ret = t8;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_k2r4f5o(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_k2r4f5o F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* extra */
    gm_b16_8 gm_l2; /* tail */
    GM_STACK_CHECK(&F, 87u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        void * t3 = t2;
        uint64_t t4 = gm_l1;
        void * t5 = GM_LD(void *, ((uint8_t *)&gm_l2));
        void * t6 = t5;
        uint64_t t7 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)16ULL), ((uint64_t)8ULL));
        uint8_t t9 = ((((uint8_t *)&t8))[0] != 0);
        F.ret = t8;
        goto gm_fret;
    }
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_ref_aqt1t7c(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_show_ref_aqt1t7c F;
    void * gm_l0; /* t */
    void * gm_l1; /* e */
    gm_tmm67r2c94d5j gm_l3; /* p */
    GM_STACK_CHECK(&F, 137u);
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
        void * t5 = gm_l1;
        gm_b0_1 t6 = gm_f_effect_ref_6sjtehp((void *)NULL, cx, NULL, t4, t5);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_stack_children_dedraea(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_stack_children_dedraea F;
    void * gm_l0; /* e */
    GM_STACK_CHECK(&F, 89u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_s1kpkpr((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = gm_l0;
        gm_closure t3 = ((gm_closure){(void *)gm_f_fn_t3eo0l3, (void *)&F.h, cx});
        gm_f_for_each_child_fm49s73(t2, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_b16_8 t4 = GM_LD(gm_b16_8, (((uint8_t *)&F.l1) + 0));
        uint8_t t5 = 0;
        F.ret = t4;
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
inline __attribute__((aligned(4))) void gm_f_truncate_31h1fac(void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_truncate_31h1fac F;
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
            if (t12 > t14 || t14 > (uintptr_t)t10) gm_trap_slice(t12, t14, t10, gm_srcrgc9bkquf6nr, 125, 36);
            gm_b16_8 t15;
            GM_ST(void *, (uint8_t *)&t15, (void *)(((uint8_t *)t11 + t12 * 40)));
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_unsupported_t7lgvp9(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2) {
    struct gm_fr_unsupported_t7lgvp9 F;
    void * gm_l0; /* s */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_b16_8 gm_l2; /* what */
    GM_STACK_CHECK(&F, 176u);
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
        if (((((uint8_t *)&t7))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t7) + 8, gm_srclv0dcbbfhves, 635, 19);
        gm_b16_8 t8 = GM_LD(gm_b16_8, (((uint8_t *)&t7) + 8));
        gm_b16_8 t9 = GM_LD(gm_b16_8, ((uint8_t *)&t8));
        gm_b0_1 t10 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t2, t3, t9);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_bind_fn_pefln9r(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, uint32_t a2) {
    struct gm_fr_bind_fn_pefln9r F;
    void * gm_l0; /* bound */
    uint32_t gm_l1; /* local */
    uint32_t gm_l2; /* f */
    uint64_t gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    uint64_t gm_l5; /* i */
    GM_STACK_CHECK(&F, 209u);
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
                    uint64_t t14 = GM_LD(uint64_t, ((uint8_t *)&t11));
                    gm_l5 = t14;
                    {
                        uint64_t t17 = gm_l5;
                        uint64_t t18 = (uintptr_t)t17;
                        void * t19 = gm_l0;
                        uint64_t t20 = GM_LD(uintptr_t, (((uint8_t *)t19) + 0) + 8);
                        if (t18 >= (uintptr_t)(t20)) gm_trap_index(t18, t20, gm_src4tam33qo20j7, 272, 12);
                        uint32_t t21 = GM_LD(uint32_t, (((uint8_t *)GM_LD(void *, (((uint8_t *)t19) + 0)) + t18 * 8) + 0));
                        uint32_t t22 = gm_l1;
                        uint8_t t23 = ((t21 == t22));
                        if (!t23) goto ie16;
                        {
                            uint64_t t25 = gm_l5;
                            uint64_t t26 = (uintptr_t)t25;
                            void * t27 = gm_l0;
                            uint64_t t28 = GM_LD(uintptr_t, (((uint8_t *)t27) + 0) + 8);
                            if (t26 >= (uintptr_t)(t28)) gm_trap_index(t26, t28, gm_src4tam33qo20j7, 273, 13);
                            uint32_t t29 = gm_l2;
                            GM_ST(uint32_t, (((uint8_t *)GM_LD(void *, (((uint8_t *)t27) + 0)) + t26 * 8) + 4), t29);
                        }
                        {
                            gm_b0_1 t30;
                            uint8_t t31 = 0;
                            F.ret = t30;
                            goto gm_fret;
                        }
                        goto ix16;
ie16:;
ix16:;
                    }
                    goto me12;
mn13:;
                    gm_trap("no match arm matched the scrutinee", gm_src4tam33qo20j7, 271, 5);
me12:;
                }
lc5:;
                {
                    uint64_t t33 = gm_l4;
                    uint64_t r34; bool o35 = __builtin_add_overflow(t33, ((uint64_t)1ULL), &r34);
                    uint64_t t36 = r34;
                    gm_l4 = t36;
                }
            }
lb5:;
        }
    }
    {
        void * t37 = gm_l0;
        uint32_t t38 = gm_l1;
        uint32_t t39 = gm_l2;
        gm_tcc1rq4n1avb2 t40;
        GM_ST(uint32_t, ((uint8_t *)&t40 + 0), t38);
        GM_ST(uint32_t, ((uint8_t *)&t40 + 4), t39);
        gm_tftb1odc739t5 t41 = gm_f_push_12ij5jo(t37, GM_CONV(gm_cs9ufftihtbpo2, t40));
        if (((((uint8_t *)&t41))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t41) + 1, gm_src4tam33qo20j7, 277, 5);
        gm_b0_1 t42 = GM_LD(gm_b0_1, (((uint8_t *)&t41) + 1));
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_t9805qtonbsag gm_f_bound_of_sn02ojm(void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_bound_of_sn02ojm F;
    void * gm_l0; /* bound */
    uint32_t gm_l1; /* local */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    gm_tcc1rq4n1avb2 gm_l4; /* b */
    GM_STACK_CHECK_SHALLOW(&F, 180u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        {
            void * t3 = gm_l0;
            gm_b16_8 t4 = GM_LD(gm_b16_8, (((uint8_t *)t3) + 0));
            gm_l2 = t4;
        }
        {
            gm_l3 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t6 = gm_l3;
                uint64_t t7 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                uint8_t t8 = (t6 < t7);
                if (!t8) goto lb5;
                {
                    uint64_t t11 = gm_l3;
                    uint64_t t12 = (uintptr_t)t11;
                    uint64_t t13 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                    gm_tcc1rq4n1avb2 t16 = GM_LD(gm_tcc1rq4n1avb2, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t12 * 8));
                    gm_l4 = t16;
                    {
                        uint32_t t19 = GM_LD(uint32_t, (((uint8_t *)&gm_l4) + 0));
                        uint32_t t20 = gm_l1;
                        uint8_t t21 = ((t19 == t20));
                        if (!t21) goto ie18;
                        {
                            uint32_t t23 = GM_LD(uint32_t, (((uint8_t *)&gm_l4) + 4));
                            gm_t9805qtonbsag t24;
                            GM_ST(uint8_t, (uint8_t *)&t24 + 5, 0);
                            GM_ST(uint16_t, (uint8_t *)&t24 + 6, 0);
                            GM_ST(uint32_t, ((uint8_t *)&t24), t23);
                            ((uint8_t *)&t24)[4] = 1;
                            uint8_t t25 = 0;
                            F.ret = t24;
                            goto gm_fret;
                        }
                        goto ix18;
ie18:;
ix18:;
                    }
                    goto me14;
mn15:;
                    gm_trap("no match arm matched the scrutinee", gm_src4tam33qo20j7, 168, 5);
me14:;
                }
lc5:;
                {
                    uint64_t t27 = gm_l3;
                    uint64_t r28; bool o29 = __builtin_add_overflow(t27, ((uint64_t)1ULL), &r28);
                    uint64_t t30 = r28;
                    gm_l3 = t30;
                }
            }
lb5:;
        }
    }
    {
        gm_t9805qtonbsag t31;
        memset(&t31, 0, sizeof t31);
        uint8_t t32 = 0;
        F.ret = t31;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_t3eo0l3(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_stack_call_poqmlno(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_b16_8 a2, gm_b16_8 a3, gm_tl874t2atnlh3 a4) {
    struct gm_fr_stack_call_poqmlno F;
    void * gm_l0; /* a */
    uint32_t gm_l1; /* g */
    gm_b16_8 gm_l2; /* args */
    gm_b16_8 gm_l3; /* env */
    gm_tl874t2atnlh3 gm_l4; /* span */
    gm_b16_8 gm_l5; /* name */
    gm_b16_8 gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    void * gm_l10; /* body */
    gm_b16_8 gm_l12; /* <tmp> */
    uint64_t gm_l13; /* <tmp> */
    uint64_t gm_l14; /* i */
    uint32_t gm_l15; /* p */
    uint32_t gm_l16; /* t */
    uint8_t gm_l17; /* memoizable */
    gm_b16_8 gm_l18; /* <tmp> */
    uint64_t gm_l19; /* <tmp> */
    gm_tb31k5l794t7p gm_l20; /* m */
    uint64_t gm_l23; /* c */
    GM_STACK_CHECK(&F, 1651u);
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
        if (t4 >= (uintptr_t)(t7)) gm_trap_index(t4, t7, gm_src4tam33qo20j7, 86, 13);
        gm_b16_8 t8 = GM_LD(gm_b16_8, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t6) + 344) + 0)) + t4 * 136) + 0));
        gm_l5 = t8;
    }
    {
        {
            void * t10 = gm_l0;
            gm_b16_8 t11 = GM_LD(gm_b16_8, ((((uint8_t *)t10) + 8) + 0));
            gm_l6 = t11;
        }
        {
            gm_l7 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t13 = gm_l7;
                uint64_t t14 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                uint8_t t15 = (t13 < t14);
                if (!t15) goto lb12;
                {
                    uint64_t t18 = gm_l7;
                    uint64_t t19 = (uintptr_t)t18;
                    uint64_t t20 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                    gm_tha5675rv512q t23 = GM_LD(gm_tha5675rv512q, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t19 * 48));
                    F.l8 = t23;
                    {
                        uint32_t t26 = GM_LD(uint32_t, (((uint8_t *)&F.l8) + 0));
                        uint32_t t27 = gm_l1;
                        uint8_t t28 = ((t26 == t27));
                        if (!t28) goto ie25;
                        {
                            gm_tbgd14esf0jvi t30;
                            memset((uint8_t *)&t30 + 12, 0, 4);
                            gm_tl874t2atnlh3 t31 = gm_l4;
                            GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t30 + 0, t31);
                            gm_b16_8 t32 = gm_l5;
                            gm_tnpifv20irnph t33;
                            GM_ST(gm_b16_8, ((uint8_t *)&t33 + 0), t32);
                            gm_tabb710d9mq2e t34;
                            gm_f_format_n18n34e((void *)&t34, (void *)NULL, cx, NULL, t33);
                            if (gm_unw.kind) {
                                goto gm_ll16;
                            }
                            if (((((uint8_t *)&t34))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t34) + 8, gm_src4tam33qo20j7, 88, 51);
                            gm_b16_8 t35 = GM_LD(gm_b16_8, (((uint8_t *)&t34) + 8));
                            gm_b16_8 t36 = GM_LD(gm_b16_8, ((uint8_t *)&t35));
                            GM_ST(gm_b16_8, (uint8_t *)&t30 + 16, t36);
                            gm_tgeh6n0c7c8kb t37;
                            GM_ST(uint8_t, (uint8_t *)&t37 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t37 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t37 + 4, 0);
                            GM_ST(uint8_t, ((uint8_t *)&t37 + 0), ((uint8_t)1ULL));
                            GM_ST(gm_tbgd14esf0jvi, ((uint8_t *)&t37 + 8), t30);
                            GM_ST(uint8_t, (uint8_t *)&(*(gm_toabfckqu3onu *)gm_out) + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&(*(gm_toabfckqu3onu *)gm_out) + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&(*(gm_toabfckqu3onu *)gm_out) + 4, 0);
                            ((uint8_t *)&(*(gm_toabfckqu3onu *)gm_out))[0] = 1;
                            GM_ST(gm_tgeh6n0c7c8kb, (uint8_t *)&(*(gm_toabfckqu3onu *)gm_out) + 8, t37);
                            goto gm_fdone;
                        }
                        goto ix25;
ie25:;
ix25:;
                    }
                    goto me21;
mn22:;
                    gm_trap("no match arm matched the scrutinee", gm_src4tam33qo20j7, 87, 5);
me21:;
                }
lc12:;
                {
                    uint64_t t39 = gm_l7;
                    uint64_t r40; bool o41 = __builtin_add_overflow(t39, ((uint64_t)1ULL), &r40);
                    uint64_t t42 = r40;
                    gm_l7 = t42;
                }
            }
gm_ll16:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb12; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc12; }
            }
            goto gm_lad;
lb12:;
        }
    }
    {
        void * t44 = gm_l0;
        void * t45 = GM_LD(void *, (((uint8_t *)t44) + 0));
        uint32_t t46 = gm_l1;
        gm_tl874t2atnlh3 t47 = gm_l4;
        uint8_t t48 = gm_f_ensure_body_tbtojbv((void *)NULL, cx, NULL, t45, t46, t47);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t49 = (uint8_t)!t48;
        if (!t49) goto ie43;
        {
            gm_tgeh6n0c7c8kb t51;
            memset(&t51, 0, sizeof t51);
            GM_ST(uint8_t, ((uint8_t *)&t51 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_toabfckqu3onu *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_toabfckqu3onu *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_toabfckqu3onu *)gm_out) + 4, 0);
            ((uint8_t *)&(*(gm_toabfckqu3onu *)gm_out))[0] = 1;
            GM_ST(gm_tgeh6n0c7c8kb, (uint8_t *)&(*(gm_toabfckqu3onu *)gm_out) + 8, t51);
            goto gm_fdone;
        }
        goto ix43;
ie43:;
ix43:;
    }
    {
        void * t52 = gm_l0;
        void * t53 = GM_LD(void *, (((uint8_t *)t52) + 0));
        uint32_t t54 = gm_l1;
        gm_f_def_of_m512sbu((void *)&F.l9, t53, t54);
    }
    {
        void * t55 = GM_LD(void *, (((uint8_t *)&F.l9) + 40));
        void * t56;
        if ((GM_LD(void *, ((uint8_t *)&t55)) != 0)) {
            void * t57 = GM_LD(void *, (((uint8_t *)&t55) + 0));
            t56 = t57;
        } else {
            {
                uint8_t t60 = ((GM_LD(uint8_t, (((uint8_t *)&F.l9) + 0) + 0) == ((uint8_t)3ULL)));
                if (!t60) goto ie59;
                {
                    gm_tbgd14esf0jvi t62;
                    memset((uint8_t *)&t62 + 12, 0, 4);
                    gm_tl874t2atnlh3 t63 = gm_l4;
                    GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t62 + 0, t63);
                    gm_b16_8 t64 = gm_l5;
                    gm_tnpifv20irnph t65;
                    GM_ST(gm_b16_8, ((uint8_t *)&t65 + 0), t64);
                    gm_tabb710d9mq2e t66;
                    gm_f_format_koh61mc((void *)&t66, (void *)NULL, cx, NULL, t65);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    if (((((uint8_t *)&t66))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t66) + 8, gm_src4tam33qo20j7, 93, 62);
                    gm_b16_8 t67 = GM_LD(gm_b16_8, (((uint8_t *)&t66) + 8));
                    gm_b16_8 t68 = GM_LD(gm_b16_8, ((uint8_t *)&t67));
                    GM_ST(gm_b16_8, (uint8_t *)&t62 + 16, t68);
                    gm_tgeh6n0c7c8kb t69;
                    GM_ST(uint8_t, (uint8_t *)&t69 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t69 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t69 + 4, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t69 + 0), ((uint8_t)1ULL));
                    GM_ST(gm_tbgd14esf0jvi, ((uint8_t *)&t69 + 8), t62);
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_toabfckqu3onu *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_toabfckqu3onu *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_toabfckqu3onu *)gm_out) + 4, 0);
                    ((uint8_t *)&(*(gm_toabfckqu3onu *)gm_out))[0] = 1;
                    GM_ST(gm_tgeh6n0c7c8kb, (uint8_t *)&(*(gm_toabfckqu3onu *)gm_out) + 8, t69);
                    goto gm_fdone;
                }
                goto ix59;
ie59:;
ix59:;
            }
            {
                void * t70 = gm_l0;
                void * t71 = GM_LD(void *, (((uint8_t *)t70) + 0));
                gm_tl874t2atnlh3 t72 = gm_l4;
                gm_b16_8 t73;
                GM_ST(void *, (uint8_t *)&t73, (void *)(gm_s9impgj2b5t1c));
                GM_ST(uintptr_t, (uint8_t *)&t73 + 8, (uintptr_t)(49));
                gm_b0_1 t74 = gm_f_unsupported_t7lgvp9((void *)NULL, cx, NULL, t71, t72, t73);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_tgeh6n0c7c8kb t75;
            memset(&t75, 0, sizeof t75);
            GM_ST(uint8_t, ((uint8_t *)&t75 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_toabfckqu3onu *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_toabfckqu3onu *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_toabfckqu3onu *)gm_out) + 4, 0);
            ((uint8_t *)&(*(gm_toabfckqu3onu *)gm_out))[0] = 1;
            GM_ST(gm_tgeh6n0c7c8kb, (uint8_t *)&(*(gm_toabfckqu3onu *)gm_out) + 8, t75);
            goto gm_fdone;
        }
        gm_l10 = t56;
    }
    {
        gm_f_list_vk99g9b((void *)&F.l11, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t77 = GM_LD(gm_b16_8, (((uint8_t *)&F.l9) + 8));
            gm_b16_8 t78 = GM_LD(gm_b16_8, ((uint8_t *)&t77));
            gm_l12 = t78;
        }
        {
            gm_l13 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t80 = gm_l13;
                uint64_t t81 = GM_LD(uintptr_t, ((uint8_t *)&gm_l12) + 8);
                uint8_t t82 = (t80 < t81);
                if (!t82) goto lb79;
                {
                    uint64_t t85 = gm_l13;
                    uint64_t t86 = gm_l13;
                    uint64_t t87 = (uintptr_t)t86;
                    uint64_t t88 = GM_LD(uintptr_t, ((uint8_t *)&gm_l12) + 8);
                    uint32_t t89 = GM_LD(uint32_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l12)) + t87 * 4));
                    gm_tn68el8d14vku t90;
                    GM_ST(uint32_t, (uint8_t *)&t90 + 12, 0);
                    GM_ST(uint64_t, ((uint8_t *)&t90 + 0), t85);
                    GM_ST(uint32_t, ((uint8_t *)&t90 + 8), t89);
                    uint64_t t93 = GM_LD(uint64_t, (((uint8_t *)&t90) + 0));
                    gm_l14 = t93;
                    uint32_t t94 = GM_LD(uint32_t, (((uint8_t *)&t90) + 8));
                    gm_l15 = t94;
                    {
                        uint64_t t97 = gm_l14;
                        uint64_t t98 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                        uint8_t t99 = (t97 < t98);
                        if (!t99) goto ie96;
                        {
                            void * t102 = gm_l0;
                            uint64_t t103 = gm_l14;
                            uint64_t t104 = (uintptr_t)t103;
                            uint64_t t105 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                            if (t104 >= (uintptr_t)(t105)) gm_trap_index(t104, t105, gm_src4tam33qo20j7, 100, 38);
                            void * t106 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t104 * 88);
                            gm_t9805qtonbsag t107 = gm_f_stack_target_7vr78v6(t102, t106);
                            if (!((((uint8_t *)&t107))[4] != 0)) goto ie101;
                            uint32_t t108 = GM_LD(uint32_t, ((uint8_t *)&t107));
                            gm_l16 = t108;
                            {
                                void * t110 = (void *)((uint8_t *)&F.l11);
                                uint32_t t111 = gm_l15;
                                uint32_t t112 = gm_l16;
                                gm_tcc1rq4n1avb2 t113;
                                GM_ST(uint32_t, ((uint8_t *)&t113 + 0), t111);
                                GM_ST(uint32_t, ((uint8_t *)&t113 + 4), t112);
                                gm_tftb1odc739t5 t114 = gm_f_push_12ij5jo(t110, GM_CONV(gm_cs9ufftihtbpo2, t113));
                                if (((((uint8_t *)&t114))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t114) + 1, gm_src4tam33qo20j7, 100, 49);
                                gm_b0_1 t115 = GM_LD(gm_b0_1, (((uint8_t *)&t114) + 1));
                            }
                            goto ix101;
ie101:;
ix101:;
                        }
                        goto ix96;
ie96:;
ix96:;
                    }
                    goto me91;
mn92:;
                    gm_trap("no match arm matched the scrutinee", gm_src4tam33qo20j7, 98, 5);
me91:;
                }
lc79:;
                {
                    uint64_t t117 = gm_l13;
                    uint64_t r118; bool o119 = __builtin_add_overflow(t117, ((uint64_t)1ULL), &r118);
                    uint64_t t120 = r118;
                    gm_l13 = t120;
                }
            }
lb79:;
        }
    }
    {
        uint8_t t121 = 0;
        uint8_t t122 = 0;
        uint8_t t123;
        if ((((((uint8_t *)&F.l9) + 48))[4] != 0)) goto mn125;
        t123 = ((uint8_t)1);
        goto me124;
mn125:;
        t123 = ((uint8_t)0);
        goto me124;
mn126:;
        gm_trap("no match arm matched the scrutinee", gm_src4tam33qo20j7, 105, 19);
me124:;
        if (t123) {
            uint64_t t127 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
            uint8_t t128 = ((t127 == ((uint64_t)0ULL)));
            t122 = t128;
        } else t122 = 0;
        if (t122) {
            uint64_t t129 = GM_LD(uintptr_t, (((uint8_t *)&F.l11) + 0) + 8);
            uint8_t t130 = ((t129 == ((uint64_t)0ULL)));
            t121 = t130;
        } else t121 = 0;
        gm_l17 = t121;
    }
    {
        uint8_t t132 = gm_l17;
        if (!t132) goto ie131;
        {
            {
                void * t135 = gm_l0;
                gm_b16_8 t136 = GM_LD(gm_b16_8, ((((uint8_t *)t135) + 88) + 0));
                gm_l18 = t136;
            }
            {
                gm_l19 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t138 = gm_l19;
                    uint64_t t139 = GM_LD(uintptr_t, ((uint8_t *)&gm_l18) + 8);
                    uint8_t t140 = (t138 < t139);
                    if (!t140) goto lb137;
                    {
                        uint64_t t143 = gm_l19;
                        uint64_t t144 = (uintptr_t)t143;
                        uint64_t t145 = GM_LD(uintptr_t, ((uint8_t *)&gm_l18) + 8);
                        gm_tb31k5l794t7p t148 = GM_LD(gm_tb31k5l794t7p, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l18)) + t144 * 16));
                        gm_l20 = t148;
                        {
                            uint32_t t151 = GM_LD(uint32_t, (((uint8_t *)&gm_l20) + 0));
                            uint32_t t152 = gm_l1;
                            uint8_t t153 = ((t151 == t152));
                            if (!t153) goto ie150;
                            {
                                uint64_t t155 = GM_LD(uint64_t, (((uint8_t *)&gm_l20) + 8));
                                memset(&(*(gm_toabfckqu3onu *)gm_out), 0, sizeof (*(gm_toabfckqu3onu *)gm_out));
                                ((uint8_t *)&(*(gm_toabfckqu3onu *)gm_out))[0] = 0;
                                GM_ST(uint64_t, (uint8_t *)&(*(gm_toabfckqu3onu *)gm_out) + 8, t155);
                                goto gm_fdone;
                            }
                            goto ix150;
ie150:;
ix150:;
                        }
                        goto me146;
mn147:;
                        gm_trap("no match arm matched the scrutinee", gm_src4tam33qo20j7, 107, 9);
me146:;
                    }
lc137:;
                    {
                        uint64_t t157 = gm_l19;
                        uint64_t r158; bool o159 = __builtin_add_overflow(t157, ((uint64_t)1ULL), &r158);
                        uint64_t t160 = r158;
                        gm_l19 = t160;
                    }
                }
lb137:;
            }
        }
        goto ix131;
ie131:;
ix131:;
    }
    {
        void * t161 = gm_l0;
        gm_trqfvmjtt4sij t162 = GM_LD(gm_trqfvmjtt4sij, (((uint8_t *)t161) + 48));
        F.l21 = t162;
    }
    {
        void * t163 = gm_l0;
        uint8_t *t164 = (((uint8_t *)t163) + 48);
        gm_f_list_o00nc4s((void *)&(*(gm_trqfvmjtt4sij *)t164), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t165 = gm_l0;
        void * t166 = (void *)(((uint8_t *)t165) + 48);
        gm_b16_8 t167 = gm_l3;
        gm_tftb1odc739t5 t168 = gm_f_extend_2kurgou(t166, t167);
        if (((((uint8_t *)&t168))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t168) + 1, gm_src4tam33qo20j7, 113, 5);
        gm_b0_1 t169 = GM_LD(gm_b0_1, (((uint8_t *)&t168) + 1));
    }
    {
        void * t170 = gm_l0;
        uint32_t t171 = gm_l1;
        void * t172 = gm_l10;
        gm_f_stack_push_expr_bdl56rb((void *)&F.l22, (void *)NULL, cx, NULL, t170, t171, &(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l11)), t172);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t173 = gm_l0;
        gm_trqfvmjtt4sij t174 = F.l21;
        GM_ST(gm_trqfvmjtt4sij, (((uint8_t *)t173) + 48), t174);
    }
    {
        gm_toabfckqu3onu t175 = F.l22;
        if (((((uint8_t *)&t175))[0] != 0)) {
            gm_toabfckqu3onu t176;
            GM_ST(uint8_t, (uint8_t *)&t176 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t176 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t176 + 4, 0);
            ((uint8_t *)&t176)[0] = 1;
            GM_ST(gm_tgeh6n0c7c8kb, ((uint8_t *)&t176 + 8), (*(gm_tgeh6n0c7c8kb *)(((uint8_t *)&t175) + 8)));
            F.ret = t176;
            goto gm_fret;
        }
        uint64_t t177 = GM_LD(uint64_t, (((uint8_t *)&t175) + 8));
        void * t178 = gm_l0;
        uint32_t t179 = gm_l1;
        uint64_t t180 = gm_f_stack_frame_68512ca((void *)NULL, cx, NULL, t178, t179);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint64_t r181; bool o182 = __builtin_add_overflow(t177, t180, &r181);
        uint64_t t183 = o182 ? (1 ? ((uint64_t)18446744073709551615ULL) : ((uint64_t)0ULL)) : r181;
        gm_l23 = t183;
    }
    {
        uint8_t t185 = gm_l17;
        if (!t185) goto ie184;
        {
            void * t187 = gm_l0;
            void * t188 = (void *)(((uint8_t *)t187) + 88);
            uint32_t t189 = gm_l1;
            uint64_t t190 = gm_l23;
            gm_tb31k5l794t7p t191;
            GM_ST(uint32_t, (uint8_t *)&t191 + 4, 0);
            GM_ST(uint32_t, ((uint8_t *)&t191 + 0), t189);
            GM_ST(uint64_t, ((uint8_t *)&t191 + 8), t190);
            gm_tftb1odc739t5 t192 = gm_f_push_1mcf4ec(t188, GM_CONV(gm_cskjv6t053tkje, t191));
            if (((((uint8_t *)&t192))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t192) + 1, gm_src4tam33qo20j7, 117, 21);
            gm_b0_1 t193 = GM_LD(gm_b0_1, (((uint8_t *)&t192) + 1));
        }
        goto ix184;
ie184:;
ix184:;
    }
    {
        uint64_t t194 = gm_l23;
        memset(&(*(gm_toabfckqu3onu *)gm_out), 0, sizeof (*(gm_toabfckqu3onu *)gm_out));
        ((uint8_t *)&(*(gm_toabfckqu3onu *)gm_out))[0] = 0;
        GM_ST(uint64_t, (uint8_t *)&(*(gm_toabfckqu3onu *)gm_out) + 8, t194);
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
extern __attribute__((aligned(4))) gm_b16_8 gm_f_stack_children_dedraea(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_stack_expr_33aekgt(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_stack_expr_33aekgt F;
    void * gm_l0; /* a */
    void * gm_l1; /* e */
    uint64_t gm_l2; /* m */
    gm_b16_8 gm_l4; /* args */
    uint64_t gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    uint64_t gm_l7; /* i */
    uint64_t gm_l8; /* c */
    uint32_t gm_l9; /* g */
    void * gm_l10; /* f */
    uint32_t gm_l11; /* g */
    gm_tiilmfd16v3fc gm_l12; /* eref */
    uint32_t gm_l13; /* op */
    void * gm_l14; /* w */
    uint64_t gm_l15; /* <tmp> */
    uint64_t gm_l16; /* <tmp> */
    uint64_t gm_l17; /* i */
    uint64_t gm_l18; /* c */
    void * gm_l19; /* f */
    uint32_t gm_l20; /* g */
    gm_tiilmfd16v3fc gm_l21; /* eref */
    uint32_t gm_l22; /* op */
    gm_b16_8 gm_l25; /* <tmp> */
    uint64_t gm_l26; /* <tmp> */
    gm_t4lnf71mruqpt gm_l27; /* h */
    void * gm_l28; /* x */
    uint32_t gm_l29; /* id */
    gm_b16_8 gm_l31; /* decls */
    void * gm_l32; /* value */
    gm_b16_8 gm_l33; /* <tmp> */
    uint64_t gm_l34; /* <tmp> */
    uint32_t gm_l35; /* d */
    void * gm_l37; /* stack */
    gm_b16_8 gm_l38; /* <tmp> */
    uint64_t gm_l39; /* <tmp> */
    gm_b16_8 gm_l41; /* ops */
    gm_b16_8 gm_l42; /* <tmp> */
    uint64_t gm_l43; /* <tmp> */
    uint64_t gm_l44; /* op */
    uint32_t gm_l45; /* c */
    uint64_t gm_l46; /* depth */
    void * gm_l48; /* b */
    gm_b16_8 gm_l51; /* <tmp> */
    uint64_t gm_l52; /* <tmp> */
    gm_trcmmd12cefb1 gm_l53; /* st */
    uint32_t gm_l54; /* local */
    void * gm_l55; /* init */
    uint32_t gm_l56; /* t */
    gm_b16_8 gm_l57; /* <tmp> */
    uint64_t gm_l58; /* <tmp> */
    void * gm_l59; /* c */
    gm_b16_8 gm_l60; /* <tmp> */
    uint64_t gm_l61; /* <tmp> */
    void * gm_l62; /* c */
    GM_STACK_CHECK(&F, 5863u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t3 = gm_l0;
        void * t4 = GM_LD(void *, (((uint8_t *)t3) + 0));
        void * t5 = (void *)(((uint8_t *)t4) + 8);
        void * t6 = gm_l1;
        gm_t88rcucg1b7uk t7 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t6) + 0));
        uint8_t t8 = gm_f_mentions_error_37knu7r(t5, GM_CONV(gm_csguankko4lmhd, t7));
        if (!t8) goto ie2;
        {
            gm_tgeh6n0c7c8kb t10;
            memset(&t10, 0, sizeof t10);
            GM_ST(uint8_t, ((uint8_t *)&t10 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_toabfckqu3onu *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_toabfckqu3onu *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_toabfckqu3onu *)gm_out) + 4, 0);
            ((uint8_t *)&(*(gm_toabfckqu3onu *)gm_out))[0] = 1;
            GM_ST(gm_tgeh6n0c7c8kb, (uint8_t *)&(*(gm_toabfckqu3onu *)gm_out) + 8, t10);
            goto gm_fdone;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        gm_l2 = ((uint64_t)0ULL);
    }
    {
        void * t11 = gm_l1;
        uint8_t t19 = GM_LD(uint8_t, ((((uint8_t *)t11) + 8) + 0));
        switch (t19) { case ((uint8_t)0ULL): goto ma17; case ((uint8_t)1ULL): goto ma17; case ((uint8_t)2ULL): goto ma17; case ((uint8_t)3ULL): goto ma17; case ((uint8_t)4ULL): goto ma17; case ((uint8_t)5ULL): goto ma17; case ((uint8_t)6ULL): goto ma17; case ((uint8_t)7ULL): goto ma17; case ((uint8_t)8ULL): goto ma17; case ((uint8_t)9ULL): goto ma17; case ((uint8_t)10ULL): goto ma17; case ((uint8_t)11ULL): goto ma17; case ((uint8_t)12ULL): goto ma17; case ((uint8_t)13ULL): goto ma17; case ((uint8_t)14ULL): goto ma17; case ((uint8_t)15ULL): goto ma17; case ((uint8_t)16ULL): goto ma17; case ((uint8_t)17ULL): goto ma17; case ((uint8_t)18ULL): goto ma17; case ((uint8_t)19ULL): goto ma17; case ((uint8_t)20ULL): goto ma17; case ((uint8_t)21ULL): goto ma17; case ((uint8_t)22ULL): goto ma17; case ((uint8_t)23ULL): goto ma17; case ((uint8_t)24ULL): goto ma17; case ((uint8_t)25ULL): goto ma17; case ((uint8_t)26ULL): goto ma17; case ((uint8_t)27ULL): goto ma17; case ((uint8_t)28ULL): goto ma17; case ((uint8_t)29ULL): goto ma17; case ((uint8_t)30ULL): goto ma17; case ((uint8_t)31ULL): goto ma17; case ((uint8_t)32ULL): goto ma17; case ((uint8_t)33ULL): goto ma17; case ((uint8_t)34ULL): goto ma13; case ((uint8_t)35ULL): goto ma17; case ((uint8_t)36ULL): goto ma17; case ((uint8_t)37ULL): goto ma17; case ((uint8_t)38ULL): goto ma17; case ((uint8_t)39ULL): goto ma17; case ((uint8_t)40ULL): goto ma17; case ((uint8_t)41ULL): goto ma17; case ((uint8_t)42ULL): goto ma17; case ((uint8_t)43ULL): goto ma17; case ((uint8_t)44ULL): goto ma17; case ((uint8_t)45ULL): goto ma17; case ((uint8_t)46ULL): goto ma16; case ((uint8_t)47ULL): goto ma17; case ((uint8_t)48ULL): goto ma17; case ((uint8_t)49ULL): goto ma17; case ((uint8_t)50ULL): goto ma17; case ((uint8_t)51ULL): goto ma17; case ((uint8_t)52ULL): goto ma17; case ((uint8_t)53ULL): goto ma17; case ((uint8_t)54ULL): goto ma17; case ((uint8_t)55ULL): goto ma14; case ((uint8_t)56ULL): goto ma17; case ((uint8_t)57ULL): goto ma17; case ((uint8_t)58ULL): goto ma15; default: goto ma17; }
ma13:;
        uint8_t t21 = GM_LD(uint8_t, ((((uint8_t *)t11) + 8) + 0));
        if (t21 != ((uint8_t)34ULL)) goto mn20;
        gm_tsth9qu1dv0g3 t22 = GM_LD(gm_tsth9qu1dv0g3, (((((uint8_t *)t11) + 8) + 8) + 0));
        F.l3 = t22;
        gm_b16_8 t23 = GM_LD(gm_b16_8, (((((uint8_t *)t11) + 8) + 8) + 40));
        gm_l4 = t23;
        {
            {
                gm_l6 = ((uint64_t)0ULL);
            }
            {
                uint64_t t26 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                gm_l5 = t26;
            }
            {
                for (;;) {
                    uint64_t t28 = gm_l6;
                    uint64_t t29 = gm_l5;
                    uint8_t t30 = (t28 < t29);
                    if (!t30) goto lb27;
                    {
                        uint64_t t33 = gm_l6;
                        uint64_t t36 = GM_LD(uint64_t, ((uint8_t *)&t33));
                        gm_l7 = t36;
                        {
                            uint64_t t38 = gm_l2;
                            void * t39 = gm_l0;
                            uint64_t t40 = gm_l7;
                            uint64_t t41 = (uintptr_t)t40;
                            uint64_t t42 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                            if (t41 >= (uintptr_t)(t42)) gm_trap_index(t41, t42, gm_src4tam33qo20j7, 180, 65);
                            void * t43 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t41 * 88);
                            gm_toabfckqu3onu t44;
                            gm_f_stack_expr_33aekgt((void *)&t44, (void *)NULL, cx, NULL, t39, t43);
                            if (gm_unw.kind) {
                                goto gm_ll31;
                            }
                            if (((((uint8_t *)&t44))[0] != 0)) {
                                gm_toabfckqu3onu t45;
                                GM_ST(uint8_t, (uint8_t *)&t45 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t45 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t45 + 4, 0);
                                ((uint8_t *)&t45)[0] = 1;
                                GM_ST(gm_tgeh6n0c7c8kb, ((uint8_t *)&t45 + 8), (*(gm_tgeh6n0c7c8kb *)(((uint8_t *)&t44) + 8)));
                                F.ret = t45;
                                goto gm_fret;
                            }
                            uint64_t t46 = GM_LD(uint64_t, (((uint8_t *)&t44) + 8));
                            uint64_t t47 = (t38 > t46 ? t38 : t46);
                            gm_l2 = t47;
                        }
                        goto me34;
mn35:;
                        gm_trap("no match arm matched the scrutinee", gm_src4tam33qo20j7, 180, 13);
me34:;
                    }
lc27:;
                    {
                        uint64_t t49 = gm_l6;
                        uint64_t r50; bool o51 = __builtin_add_overflow(t49, ((uint64_t)1ULL), &r50);
                        uint64_t t52 = r50;
                        gm_l6 = t52;
                    }
                }
gm_ll31:;
                if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb27; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc27; }
                }
                goto gm_lad;
lb27:;
            }
        }
        {
            gm_l8 = ((uint64_t)0ULL);
        }
        {
            uint8_t t58 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 0));
            switch (t58) { case ((uint8_t)0ULL): goto ma54; case ((uint8_t)1ULL): goto ma56; case ((uint8_t)2ULL): goto ma55; default: goto ma57; }
ma54:;
            uint8_t t60 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 0));
            if (t60 != ((uint8_t)0ULL)) goto mn59;
            uint32_t t61 = GM_LD(uint32_t, (((uint8_t *)&F.l3) + 8));
            gm_l9 = t61;
            {
                void * t63 = gm_l0;
                uint32_t t64 = gm_l9;
                gm_b16_8 t65 = gm_l4;
                gm_b16_8 t66 = GM_LD(gm_b16_8, ((uint8_t *)&t65));
                void * t67 = gm_l0;
                gm_b16_8 t68 = GM_LD(gm_b16_8, ((((uint8_t *)t67) + 48) + 0));
                gm_b16_8 t69 = GM_LD(gm_b16_8, ((uint8_t *)&t68));
                void * t70 = gm_l1;
                gm_tl874t2atnlh3 t71 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t70) + 72));
                gm_toabfckqu3onu t72;
                gm_f_stack_call_poqmlno((void *)&t72, (void *)NULL, cx, NULL, t63, t64, t66, t69, t71);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t72))[0] != 0)) {
                    gm_toabfckqu3onu t73;
                    GM_ST(uint8_t, (uint8_t *)&t73 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t73 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t73 + 4, 0);
                    ((uint8_t *)&t73)[0] = 1;
                    GM_ST(gm_tgeh6n0c7c8kb, ((uint8_t *)&t73 + 8), (*(gm_tgeh6n0c7c8kb *)(((uint8_t *)&t72) + 8)));
                    F.ret = t73;
                    goto gm_fret;
                }
                uint64_t t74 = GM_LD(uint64_t, (((uint8_t *)&t72) + 8));
                gm_l8 = t74;
            }
            goto me53;
mn59:;
ma55:;
            uint8_t t76 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 0));
            if (t76 != ((uint8_t)2ULL)) goto mn75;
            void * t77 = GM_LD(void *, (((uint8_t *)&F.l3) + 8));
            gm_l10 = t77;
            {
                uint64_t t79 = gm_l2;
                void * t80 = gm_l0;
                void * t81 = gm_l10;
                gm_toabfckqu3onu t82;
                gm_f_stack_expr_33aekgt((void *)&t82, (void *)NULL, cx, NULL, t80, t81);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t82))[0] != 0)) {
                    gm_toabfckqu3onu t83;
                    GM_ST(uint8_t, (uint8_t *)&t83 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t83 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t83 + 4, 0);
                    ((uint8_t *)&t83)[0] = 1;
                    GM_ST(gm_tgeh6n0c7c8kb, ((uint8_t *)&t83 + 8), (*(gm_tgeh6n0c7c8kb *)(((uint8_t *)&t82) + 8)));
                    F.ret = t83;
                    goto gm_fret;
                }
                uint64_t t84 = GM_LD(uint64_t, (((uint8_t *)&t82) + 8));
                uint64_t t85 = (t79 > t84 ? t79 : t84);
                gm_l2 = t85;
            }
            {
                void * t86 = gm_l0;
                void * t87 = gm_l10;
                gm_t9805qtonbsag t88 = gm_f_stack_target_7vr78v6(t86, t87);
                uint32_t t89;
                if (((((uint8_t *)&t88))[4] != 0)) {
                    uint32_t t90 = GM_LD(uint32_t, (((uint8_t *)&t88) + 0));
                    t89 = t90;
                } else {
                    gm_tbgd14esf0jvi t92;
                    memset((uint8_t *)&t92 + 12, 0, 4);
                    void * t93 = gm_l1;
                    gm_tl874t2atnlh3 t94 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t93) + 72));
                    GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t92 + 0, t94);
                    gm_b16_8 t95;
                    GM_ST(void *, (uint8_t *)&t95, (void *)(gm_sg0997vvtiund));
                    GM_ST(uintptr_t, (uint8_t *)&t95 + 8, (uintptr_t)(64));
                    GM_ST(gm_b16_8, (uint8_t *)&t92 + 16, t95);
                    gm_tgeh6n0c7c8kb t96;
                    GM_ST(uint8_t, (uint8_t *)&t96 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t96 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t96 + 4, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t96 + 0), ((uint8_t)1ULL));
                    GM_ST(gm_tbgd14esf0jvi, ((uint8_t *)&t96 + 8), t92);
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_toabfckqu3onu *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_toabfckqu3onu *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_toabfckqu3onu *)gm_out) + 4, 0);
                    ((uint8_t *)&(*(gm_toabfckqu3onu *)gm_out))[0] = 1;
                    GM_ST(gm_tgeh6n0c7c8kb, (uint8_t *)&(*(gm_toabfckqu3onu *)gm_out) + 8, t96);
                    goto gm_fdone;
                }
                gm_l11 = t89;
            }
            {
                void * t97 = gm_l0;
                uint32_t t98 = gm_l11;
                gm_b16_8 t99 = gm_l4;
                gm_b16_8 t100 = GM_LD(gm_b16_8, ((uint8_t *)&t99));
                void * t101 = gm_l0;
                gm_b16_8 t102 = GM_LD(gm_b16_8, ((((uint8_t *)t101) + 48) + 0));
                gm_b16_8 t103 = GM_LD(gm_b16_8, ((uint8_t *)&t102));
                void * t104 = gm_l1;
                gm_tl874t2atnlh3 t105 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t104) + 72));
                gm_toabfckqu3onu t106;
                gm_f_stack_call_poqmlno((void *)&t106, (void *)NULL, cx, NULL, t97, t98, t100, t103, t105);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t106))[0] != 0)) {
                    gm_toabfckqu3onu t107;
                    GM_ST(uint8_t, (uint8_t *)&t107 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t107 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t107 + 4, 0);
                    ((uint8_t *)&t107)[0] = 1;
                    GM_ST(gm_tgeh6n0c7c8kb, ((uint8_t *)&t107 + 8), (*(gm_tgeh6n0c7c8kb *)(((uint8_t *)&t106) + 8)));
                    F.ret = t107;
                    goto gm_fret;
                }
                uint64_t t108 = GM_LD(uint64_t, (((uint8_t *)&t106) + 8));
                gm_l8 = t108;
            }
            goto me53;
mn75:;
ma56:;
            uint8_t t110 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 0));
            if (t110 != ((uint8_t)1ULL)) goto mn109;
            gm_tiilmfd16v3fc t111 = GM_LD(gm_tiilmfd16v3fc, ((((uint8_t *)&F.l3) + 8) + 0));
            gm_l12 = t111;
            uint32_t t112 = GM_LD(uint32_t, ((((uint8_t *)&F.l3) + 8) + 24));
            gm_l13 = t112;
            {
                void * t114 = gm_l0;
                void * t115 = (void *)((uint8_t *)&gm_l12);
                uint32_t t116 = gm_l13;
                gm_b16_8 t117 = gm_l4;
                gm_b16_8 t118 = GM_LD(gm_b16_8, ((uint8_t *)&t117));
                void * t119 = gm_l1;
                gm_tl874t2atnlh3 t120 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t119) + 72));
                gm_toabfckqu3onu t121;
                gm_f_stack_perform_soivemm((void *)&t121, (void *)NULL, cx, NULL, t114, t115, t116, t118, t120);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t121))[0] != 0)) {
                    gm_toabfckqu3onu t122;
                    GM_ST(uint8_t, (uint8_t *)&t122 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t122 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t122 + 4, 0);
                    ((uint8_t *)&t122)[0] = 1;
                    GM_ST(gm_tgeh6n0c7c8kb, ((uint8_t *)&t122 + 8), (*(gm_tgeh6n0c7c8kb *)(((uint8_t *)&t121) + 8)));
                    F.ret = t122;
                    goto gm_fret;
                }
                uint64_t t123 = GM_LD(uint64_t, (((uint8_t *)&t121) + 8));
                gm_l8 = t123;
            }
            goto me53;
mn109:;
ma57:;
            gm_trap("no match arm matched the scrutinee", gm_src4tam33qo20j7, 182, 13);
me53:;
        }
        {
            uint64_t t124 = gm_l2;
            uint64_t t125 = gm_l8;
            uint64_t t126 = (t124 > t125 ? t124 : t125);
            gm_l2 = t126;
        }
        goto me12;
mn20:;
ma14:;
        uint8_t t128 = GM_LD(uint8_t, ((((uint8_t *)t11) + 8) + 0));
        if (t128 != ((uint8_t)55ULL)) goto mn127;
        void * t129 = GM_LD(void *, ((((uint8_t *)t11) + 8) + 8));
        gm_l14 = t129;
        {
            {
                gm_l16 = ((uint64_t)0ULL);
            }
            {
                void * t132 = gm_l14;
                uint64_t t133 = GM_LD(uintptr_t, (((uint8_t *)t132) + 72) + 8);
                gm_l15 = t133;
            }
            {
                for (;;) {
                    uint64_t t135 = gm_l16;
                    uint64_t t136 = gm_l15;
                    uint8_t t137 = (t135 < t136);
                    if (!t137) goto lb134;
                    {
                        uint64_t t140 = gm_l16;
                        uint64_t t143 = GM_LD(uint64_t, ((uint8_t *)&t140));
                        gm_l17 = t143;
                        {
                            uint64_t t145 = gm_l2;
                            void * t146 = gm_l0;
                            uint64_t t147 = gm_l17;
                            uint64_t t148 = (uintptr_t)t147;
                            void * t149 = gm_l14;
                            uint64_t t150 = GM_LD(uintptr_t, (((uint8_t *)t149) + 72) + 8);
                            if (t148 >= (uintptr_t)(t150)) gm_trap_index(t148, t150, gm_src4tam33qo20j7, 196, 67);
                            void * t151 = (void *)((uint8_t *)GM_LD(void *, (((uint8_t *)t149) + 72)) + t148 * 88);
                            gm_toabfckqu3onu t152;
                            gm_f_stack_expr_33aekgt((void *)&t152, (void *)NULL, cx, NULL, t146, t151);
                            if (gm_unw.kind) {
                                goto gm_ll138;
                            }
                            if (((((uint8_t *)&t152))[0] != 0)) {
                                gm_toabfckqu3onu t153;
                                GM_ST(uint8_t, (uint8_t *)&t153 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t153 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t153 + 4, 0);
                                ((uint8_t *)&t153)[0] = 1;
                                GM_ST(gm_tgeh6n0c7c8kb, ((uint8_t *)&t153 + 8), (*(gm_tgeh6n0c7c8kb *)(((uint8_t *)&t152) + 8)));
                                F.ret = t153;
                                goto gm_fret;
                            }
                            uint64_t t154 = GM_LD(uint64_t, (((uint8_t *)&t152) + 8));
                            uint64_t t155 = (t145 > t154 ? t145 : t154);
                            gm_l2 = t155;
                        }
                        goto me141;
mn142:;
                        gm_trap("no match arm matched the scrutinee", gm_src4tam33qo20j7, 196, 13);
me141:;
                    }
lc134:;
                    {
                        uint64_t t157 = gm_l16;
                        uint64_t r158; bool o159 = __builtin_add_overflow(t157, ((uint64_t)1ULL), &r158);
                        uint64_t t160 = r158;
                        gm_l16 = t160;
                    }
                }
gm_ll138:;
                if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb134; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc134; }
                }
                goto gm_lad;
lb134:;
            }
        }
        {
            gm_l18 = ((uint64_t)0ULL);
        }
        {
            void * t161 = gm_l14;
            uint8_t t164 = GM_LD(uint8_t, ((((uint8_t *)t161) + 0) + 0));
            if (t164 != ((uint8_t)1ULL)) goto mn163;
            void * t165 = GM_LD(void *, ((((uint8_t *)t161) + 0) + 8));
            gm_l19 = t165;
            {
                uint64_t t167 = gm_l2;
                void * t168 = gm_l0;
                void * t169 = gm_l19;
                gm_toabfckqu3onu t170;
                gm_f_stack_expr_33aekgt((void *)&t170, (void *)NULL, cx, NULL, t168, t169);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t170))[0] != 0)) {
                    gm_toabfckqu3onu t171;
                    GM_ST(uint8_t, (uint8_t *)&t171 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t171 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t171 + 4, 0);
                    ((uint8_t *)&t171)[0] = 1;
                    GM_ST(gm_tgeh6n0c7c8kb, ((uint8_t *)&t171 + 8), (*(gm_tgeh6n0c7c8kb *)(((uint8_t *)&t170) + 8)));
                    F.ret = t171;
                    goto gm_fret;
                }
                uint64_t t172 = GM_LD(uint64_t, (((uint8_t *)&t170) + 8));
                uint64_t t173 = (t167 > t172 ? t167 : t172);
                gm_l2 = t173;
            }
            {
                void * t174 = gm_l0;
                void * t175 = gm_l19;
                gm_t9805qtonbsag t176 = gm_f_stack_target_7vr78v6(t174, t175);
                uint32_t t177;
                if (((((uint8_t *)&t176))[4] != 0)) {
                    uint32_t t178 = GM_LD(uint32_t, (((uint8_t *)&t176) + 0));
                    t177 = t178;
                } else {
                    gm_tbgd14esf0jvi t180;
                    memset((uint8_t *)&t180 + 12, 0, 4);
                    void * t181 = gm_l1;
                    gm_tl874t2atnlh3 t182 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t181) + 72));
                    GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t180 + 0, t182);
                    gm_b16_8 t183;
                    GM_ST(void *, (uint8_t *)&t183, (void *)(gm_s2sodn6uclrlt));
                    GM_ST(uintptr_t, (uint8_t *)&t183 + 8, (uintptr_t)(62));
                    GM_ST(gm_b16_8, (uint8_t *)&t180 + 16, t183);
                    gm_tgeh6n0c7c8kb t184;
                    GM_ST(uint8_t, (uint8_t *)&t184 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t184 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t184 + 4, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t184 + 0), ((uint8_t)1ULL));
                    GM_ST(gm_tbgd14esf0jvi, ((uint8_t *)&t184 + 8), t180);
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_toabfckqu3onu *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_toabfckqu3onu *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_toabfckqu3onu *)gm_out) + 4, 0);
                    ((uint8_t *)&(*(gm_toabfckqu3onu *)gm_out))[0] = 1;
                    GM_ST(gm_tgeh6n0c7c8kb, (uint8_t *)&(*(gm_toabfckqu3onu *)gm_out) + 8, t184);
                    goto gm_fdone;
                }
                gm_l20 = t177;
            }
            {
                void * t185 = gm_l0;
                uint32_t t186 = gm_l20;
                gm_b16_8 t187 = gm_f_empty_slice_g3tddrb_2();
                gm_b16_8 t188 = GM_LD(gm_b16_8, ((uint8_t *)&t187));
                void * t189 = gm_l0;
                gm_b16_8 t190 = GM_LD(gm_b16_8, ((((uint8_t *)t189) + 48) + 0));
                gm_b16_8 t191 = GM_LD(gm_b16_8, ((uint8_t *)&t190));
                void * t192 = gm_l1;
                gm_tl874t2atnlh3 t193 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t192) + 72));
                gm_toabfckqu3onu t194;
                gm_f_stack_call_poqmlno((void *)&t194, (void *)NULL, cx, NULL, t185, t186, t188, t191, t193);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t194))[0] != 0)) {
                    gm_toabfckqu3onu t195;
                    GM_ST(uint8_t, (uint8_t *)&t195 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t195 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t195 + 4, 0);
                    ((uint8_t *)&t195)[0] = 1;
                    GM_ST(gm_tgeh6n0c7c8kb, ((uint8_t *)&t195 + 8), (*(gm_tgeh6n0c7c8kb *)(((uint8_t *)&t194) + 8)));
                    F.ret = t195;
                    goto gm_fret;
                }
                uint64_t t196 = GM_LD(uint64_t, (((uint8_t *)&t194) + 8));
                gm_l18 = t196;
            }
            goto me162;
mn163:;
            uint8_t t198 = GM_LD(uint8_t, ((((uint8_t *)t161) + 0) + 0));
            if (t198 != ((uint8_t)0ULL)) goto mn197;
            gm_tiilmfd16v3fc t199 = GM_LD(gm_tiilmfd16v3fc, (((((uint8_t *)t161) + 0) + 8) + 0));
            gm_l21 = t199;
            uint32_t t200 = GM_LD(uint32_t, (((((uint8_t *)t161) + 0) + 8) + 24));
            gm_l22 = t200;
            {
                void * t202 = gm_l0;
                void * t203 = (void *)((uint8_t *)&gm_l21);
                uint32_t t204 = gm_l22;
                gm_b16_8 t205 = gm_f_empty_slice_g3tddrb_2();
                gm_b16_8 t206 = GM_LD(gm_b16_8, ((uint8_t *)&t205));
                void * t207 = gm_l1;
                gm_tl874t2atnlh3 t208 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t207) + 72));
                gm_toabfckqu3onu t209;
                gm_f_stack_perform_soivemm((void *)&t209, (void *)NULL, cx, NULL, t202, t203, t204, t206, t208);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t209))[0] != 0)) {
                    gm_toabfckqu3onu t210;
                    GM_ST(uint8_t, (uint8_t *)&t210 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t210 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t210 + 4, 0);
                    ((uint8_t *)&t210)[0] = 1;
                    GM_ST(gm_tgeh6n0c7c8kb, ((uint8_t *)&t210 + 8), (*(gm_tgeh6n0c7c8kb *)(((uint8_t *)&t209) + 8)));
                    F.ret = t210;
                    goto gm_fret;
                }
                uint64_t t211 = GM_LD(uint64_t, (((uint8_t *)&t209) + 8));
                gm_l18 = t211;
            }
            goto me162;
mn197:;
            gm_trap("no match arm matched the scrutinee", gm_src4tam33qo20j7, 198, 13);
me162:;
        }
        {
            uint64_t t212 = gm_l2;
            uint64_t t213 = gm_l18;
            uint64_t t214 = (t212 > t213 ? t212 : t213);
            gm_l2 = t214;
        }
        goto me12;
mn127:;
ma15:;
        uint8_t t216 = GM_LD(uint8_t, ((((uint8_t *)t11) + 8) + 0));
        if (t216 != ((uint8_t)58ULL)) goto mn215;
        gm_t710lsrq1m8gk t217 = GM_LD(gm_t710lsrq1m8gk, ((((uint8_t *)t11) + 8) + 8));
        F.l23 = t217;
        {
            gm_f_list_o00nc4s((void *)&F.l24, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            {
                gm_b16_8 t220 = GM_LD(gm_b16_8, (((uint8_t *)&F.l23) + 8));
                gm_l25 = t220;
            }
            {
                gm_l26 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t222 = gm_l26;
                    uint64_t t223 = GM_LD(uintptr_t, ((uint8_t *)&gm_l25) + 8);
                    uint8_t t224 = (t222 < t223);
                    if (!t224) goto lb221;
                    {
                        uint64_t t227 = gm_l26;
                        uint64_t t228 = (uintptr_t)t227;
                        uint64_t t229 = GM_LD(uintptr_t, ((uint8_t *)&gm_l25) + 8);
                        gm_t4lnf71mruqpt t232 = GM_LD(gm_t4lnf71mruqpt, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l25)) + t228 * 32));
                        gm_l27 = t232;
                        {
                            uint8_t t236 = GM_LD(uint8_t, (((uint8_t *)&gm_l27) + 0));
                            if (t236 != ((uint8_t)0ULL)) goto mn235;
                            void * t237 = GM_LD(void *, (((uint8_t *)&gm_l27) + 8));
                            gm_l28 = t237;
                            {
                                uint64_t t239 = gm_l2;
                                void * t240 = gm_l0;
                                void * t241 = gm_l28;
                                gm_toabfckqu3onu t242;
                                gm_f_stack_expr_33aekgt((void *)&t242, (void *)NULL, cx, NULL, t240, t241);
                                if (gm_unw.kind) {
                                    goto gm_ll225;
                                }
                                if (((((uint8_t *)&t242))[0] != 0)) {
                                    gm_toabfckqu3onu t243;
                                    GM_ST(uint8_t, (uint8_t *)&t243 + 1, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t243 + 2, 0);
                                    GM_ST(uint32_t, (uint8_t *)&t243 + 4, 0);
                                    ((uint8_t *)&t243)[0] = 1;
                                    GM_ST(gm_tgeh6n0c7c8kb, ((uint8_t *)&t243 + 8), (*(gm_tgeh6n0c7c8kb *)(((uint8_t *)&t242) + 8)));
                                    F.ret = t243;
                                    goto gm_fret;
                                }
                                uint64_t t244 = GM_LD(uint64_t, (((uint8_t *)&t242) + 8));
                                uint64_t t245 = (t239 > t244 ? t239 : t244);
                                gm_l2 = t245;
                            }
                            {
                                void * t247 = gm_l0;
                                void * t248 = GM_LD(void *, (((uint8_t *)t247) + 0));
                                void * t249 = (void *)(((uint8_t *)t248) + 8);
                                void * t250 = gm_l28;
                                gm_t88rcucg1b7uk t251 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t250) + 0));
                                gm_t53at80pfdus3 t252;
                                gm_f_kind_jcnt4hm((void *)&t252, t249, GM_CONV(gm_csguankko4lmhd, t251));
                                uint8_t t253 = GM_LD(uint8_t, (((uint8_t *)&t252) + 0));
                                if (t253 != ((uint8_t)25ULL)) goto ie246;
                                uint32_t t254 = GM_LD(uint32_t, (((uint8_t *)&t252) + 8));
                                gm_l29 = t254;
                                {
                                    uint32_t t256 = gm_l29;
                                    uint64_t t257 = (uint64_t)t256;
                                    uint64_t t258 = (uintptr_t)t257;
                                    void * t259 = gm_l0;
                                    void * t260 = GM_LD(void *, (((uint8_t *)t259) + 0));
                                    uint64_t t261 = GM_LD(uintptr_t, ((((uint8_t *)t260) + 504) + 0) + 8);
                                    if (t258 >= (uintptr_t)(t261)) gm_trap_index(t258, t261, gm_src4tam33qo20j7, 217, 32);
                                    gm_t1cs2fjnrqap1 t262 = GM_LD(gm_t1cs2fjnrqap1, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t260) + 504) + 0)) + t258 * 40));
                                    F.l30 = t262;
                                }
                                {
                                    void * t263 = (void *)((uint8_t *)&F.l24);
                                    gm_t1cs2fjnrqap1 t264;
                                    memcpy((uint8_t *)&t264 + 0, (((uint8_t *)&F.l30) + 0), 24);
                                    gm_b16_8 t265 = GM_LD(gm_b16_8, (((uint8_t *)&F.l30) + 24));
                                    GM_ST(gm_b16_8, (uint8_t *)&t264 + 24, t265);
                                    gm_tftb1odc739t5 t266 = gm_f_push_njfep1h(t263, &t264);
                                    if (((((uint8_t *)&t266))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t266) + 1, gm_src4tam33qo20j7, 218, 25);
                                    gm_b0_1 t267 = GM_LD(gm_b0_1, (((uint8_t *)&t266) + 1));
                                }
                                goto ix246;
ie246:;
                                {
                                    gm_tgeh6n0c7c8kb t269;
                                    memset(&t269, 0, sizeof t269);
                                    GM_ST(uint8_t, ((uint8_t *)&t269 + 0), ((uint8_t)0ULL));
                                    GM_ST(uint8_t, (uint8_t *)&(*(gm_toabfckqu3onu *)gm_out) + 1, 0);
                                    GM_ST(uint16_t, (uint8_t *)&(*(gm_toabfckqu3onu *)gm_out) + 2, 0);
                                    GM_ST(uint32_t, (uint8_t *)&(*(gm_toabfckqu3onu *)gm_out) + 4, 0);
                                    ((uint8_t *)&(*(gm_toabfckqu3onu *)gm_out))[0] = 1;
                                    GM_ST(gm_tgeh6n0c7c8kb, (uint8_t *)&(*(gm_toabfckqu3onu *)gm_out) + 8, t269);
                                    goto gm_fdone;
                                }
ix246:;
                            }
                            goto me234;
mn235:;
                            uint8_t t271 = GM_LD(uint8_t, (((uint8_t *)&gm_l27) + 0));
                            if (t271 != ((uint8_t)1ULL)) goto mn270;
                            gm_b16_8 t272 = GM_LD(gm_b16_8, ((((uint8_t *)&gm_l27) + 8) + 0));
                            gm_l31 = t272;
                            void * t273 = GM_LD(void *, ((((uint8_t *)&gm_l27) + 8) + 16));
                            gm_l32 = t273;
                            {
                                uint64_t t275 = gm_l2;
                                void * t276 = gm_l0;
                                void * t277 = gm_l32;
                                gm_toabfckqu3onu t278;
                                gm_f_stack_expr_33aekgt((void *)&t278, (void *)NULL, cx, NULL, t276, t277);
                                if (gm_unw.kind) {
                                    goto gm_ll225;
                                }
                                if (((((uint8_t *)&t278))[0] != 0)) {
                                    gm_toabfckqu3onu t279;
                                    GM_ST(uint8_t, (uint8_t *)&t279 + 1, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t279 + 2, 0);
                                    GM_ST(uint32_t, (uint8_t *)&t279 + 4, 0);
                                    ((uint8_t *)&t279)[0] = 1;
                                    GM_ST(gm_tgeh6n0c7c8kb, ((uint8_t *)&t279 + 8), (*(gm_tgeh6n0c7c8kb *)(((uint8_t *)&t278) + 8)));
                                    F.ret = t279;
                                    goto gm_fret;
                                }
                                uint64_t t280 = GM_LD(uint64_t, (((uint8_t *)&t278) + 8));
                                uint64_t t281 = (t275 > t280 ? t275 : t280);
                                gm_l2 = t281;
                            }
                            {
                                {
                                    gm_b16_8 t283 = gm_l31;
                                    gm_l33 = t283;
                                }
                                {
                                    gm_l34 = ((uint64_t)0ULL);
                                }
                                {
                                    for (;;) {
                                        uint64_t t285 = gm_l34;
                                        uint64_t t286 = GM_LD(uintptr_t, ((uint8_t *)&gm_l33) + 8);
                                        uint8_t t287 = (t285 < t286);
                                        if (!t287) goto lb284;
                                        {
                                            uint64_t t290 = gm_l34;
                                            uint64_t t291 = (uintptr_t)t290;
                                            uint64_t t292 = GM_LD(uintptr_t, ((uint8_t *)&gm_l33) + 8);
                                            uint32_t t293 = GM_LD(uint32_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l33)) + t291 * 4));
                                            uint32_t t296 = GM_LD(uint32_t, ((uint8_t *)&t293));
                                            gm_l35 = t296;
                                            {
                                                uint32_t t298 = gm_l35;
                                                uint64_t t299 = (uint64_t)t298;
                                                uint64_t t300 = (uintptr_t)t299;
                                                void * t301 = gm_l0;
                                                void * t302 = GM_LD(void *, (((uint8_t *)t301) + 0));
                                                uint64_t t303 = GM_LD(uintptr_t, ((((uint8_t *)t302) + 544) + 0) + 8);
                                                if (t300 >= (uintptr_t)(t303)) gm_trap_index(t300, t303, gm_src4tam33qo20j7, 223, 37);
                                                gm_tvc83pqnepc2e t304 = GM_LD(gm_tvc83pqnepc2e, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t302) + 544) + 0)) + t300 * 64));
                                                F.l36 = t304;
                                            }
                                            {
                                                void * t305 = (void *)((uint8_t *)&F.l24);
                                                gm_t1cs2fjnrqap1 t306;
                                                memcpy((uint8_t *)&t306 + 0, (((uint8_t *)&F.l36) + 0), 24);
                                                gm_b16_8 t307 = GM_LD(gm_b16_8, (((uint8_t *)&F.l36) + 48));
                                                GM_ST(gm_b16_8, (uint8_t *)&t306 + 24, t307);
                                                gm_tftb1odc739t5 t308 = gm_f_push_njfep1h(t305, &t306);
                                                if (((((uint8_t *)&t308))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t308) + 1, gm_src4tam33qo20j7, 224, 29);
                                                gm_b0_1 t309 = GM_LD(gm_b0_1, (((uint8_t *)&t308) + 1));
                                            }
                                            goto me294;
mn295:;
                                            gm_trap("no match arm matched the scrutinee", gm_src4tam33qo20j7, 222, 25);
me294:;
                                        }
lc284:;
                                        {
                                            uint64_t t311 = gm_l34;
                                            uint64_t r312; bool o313 = __builtin_add_overflow(t311, ((uint64_t)1ULL), &r312);
                                            uint64_t t314 = r312;
                                            gm_l34 = t314;
                                        }
                                    }
lb284:;
                                }
                            }
                            goto me234;
mn270:;
                            gm_trap("no match arm matched the scrutinee", gm_src4tam33qo20j7, 213, 17);
me234:;
                        }
                        goto me230;
mn231:;
                        gm_trap("no match arm matched the scrutinee", gm_src4tam33qo20j7, 212, 13);
me230:;
                    }
lc221:;
                    {
                        uint64_t t316 = gm_l26;
                        uint64_t r317; bool o318 = __builtin_add_overflow(t316, ((uint64_t)1ULL), &r317);
                        uint64_t t319 = r317;
                        gm_l26 = t319;
                    }
                }
gm_ll225:;
                if (gm_unw.frame == &F.h && gm_unw.label == 2) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb221; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc221; }
                }
                goto gm_lad;
lb221:;
            }
        }
        {
            void * t321 = GM_LD(void *, (((uint8_t *)&F.l23) + 24));
            if (!(GM_LD(void *, ((uint8_t *)&t321)) != 0)) goto ie320;
            void * t322 = GM_LD(void *, ((uint8_t *)&t321));
            gm_l37 = t322;
            {
                uint64_t t324 = gm_l2;
                void * t325 = gm_l0;
                void * t326 = gm_l37;
                gm_toabfckqu3onu t327;
                gm_f_stack_expr_33aekgt((void *)&t327, (void *)NULL, cx, NULL, t325, t326);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t327))[0] != 0)) {
                    gm_toabfckqu3onu t328;
                    GM_ST(uint8_t, (uint8_t *)&t328 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t328 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t328 + 4, 0);
                    ((uint8_t *)&t328)[0] = 1;
                    GM_ST(gm_tgeh6n0c7c8kb, ((uint8_t *)&t328 + 8), (*(gm_tgeh6n0c7c8kb *)(((uint8_t *)&t327) + 8)));
                    F.ret = t328;
                    goto gm_fret;
                }
                uint64_t t329 = GM_LD(uint64_t, (((uint8_t *)&t327) + 8));
                uint64_t t330 = (t324 > t329 ? t324 : t329);
                gm_l2 = t330;
            }
            {
                {
                    gm_b16_8 t332 = GM_LD(gm_b16_8, (((uint8_t *)&F.l24) + 0));
                    gm_l38 = t332;
                }
                {
                    gm_l39 = ((uint64_t)0ULL);
                }
                {
                    for (;;) {
                        uint64_t t334 = gm_l39;
                        uint64_t t335 = GM_LD(uintptr_t, ((uint8_t *)&gm_l38) + 8);
                        uint8_t t336 = (t334 < t335);
                        if (!t336) goto lb333;
                        {
                            uint64_t t339 = gm_l39;
                            uint64_t t340 = (uintptr_t)t339;
                            uint64_t t341 = GM_LD(uintptr_t, ((uint8_t *)&gm_l38) + 8);
                            gm_t1cs2fjnrqap1 t344 = GM_LD(gm_t1cs2fjnrqap1, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l38)) + t340 * 40));
                            F.l40 = t344;
                            {
                                uint32_t t346 = GM_LD(uint32_t, ((((uint8_t *)&F.l40) + 0) + 0));
                                uint64_t t347 = (uint64_t)t346;
                                uint64_t t348 = (uintptr_t)t347;
                                void * t349 = gm_l0;
                                void * t350 = GM_LD(void *, (((uint8_t *)t349) + 0));
                                uint64_t t351 = GM_LD(uintptr_t, ((((uint8_t *)t350) + 1048) + 0) + 8);
                                if (t348 >= (uintptr_t)(t351)) gm_trap_index(t348, t351, gm_src4tam33qo20j7, 234, 28);
                                gm_b16_8 t352 = GM_LD(gm_b16_8, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t350) + 1048) + 0)) + t348 * 56) + 40));
                                gm_l41 = t352;
                            }
                            {
                                {
                                    gm_b16_8 t354 = GM_LD(gm_b16_8, (((uint8_t *)&F.l40) + 24));
                                    gm_b16_8 t355 = GM_LD(gm_b16_8, ((uint8_t *)&t354));
                                    gm_l42 = t355;
                                }
                                {
                                    gm_l43 = ((uint64_t)0ULL);
                                }
                                {
                                    for (;;) {
                                        uint64_t t357 = gm_l43;
                                        uint64_t t358 = GM_LD(uintptr_t, ((uint8_t *)&gm_l42) + 8);
                                        uint8_t t359 = (t357 < t358);
                                        if (!t359) goto lb356;
                                        {
                                            uint64_t t362 = gm_l43;
                                            uint64_t t363 = gm_l43;
                                            uint64_t t364 = (uintptr_t)t363;
                                            uint64_t t365 = GM_LD(uintptr_t, ((uint8_t *)&gm_l42) + 8);
                                            uint32_t t366 = GM_LD(uint32_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l42)) + t364 * 4));
                                            gm_tn68el8d14vku t367;
                                            GM_ST(uint32_t, (uint8_t *)&t367 + 12, 0);
                                            GM_ST(uint64_t, ((uint8_t *)&t367 + 0), t362);
                                            GM_ST(uint32_t, ((uint8_t *)&t367 + 8), t366);
                                            uint64_t t370 = GM_LD(uint64_t, (((uint8_t *)&t367) + 0));
                                            gm_l44 = t370;
                                            uint32_t t371 = GM_LD(uint32_t, (((uint8_t *)&t367) + 8));
                                            gm_l45 = t371;
                                            {
                                                uint64_t t374 = gm_l44;
                                                uint64_t t375 = GM_LD(uintptr_t, ((uint8_t *)&gm_l41) + 8);
                                                uint8_t t376 = (t374 < t375);
                                                if (!t376) goto ie373;
                                                uint64_t t377 = gm_l44;
                                                uint64_t t378 = (uintptr_t)t377;
                                                uint64_t t379 = GM_LD(uintptr_t, ((uint8_t *)&gm_l41) + 8);
                                                if (t378 >= (uintptr_t)(t379)) gm_trap_index(t378, t379, gm_src4tam33qo20j7, 236, 45);
                                                uint8_t t380 = ((GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l41)) + t378 * 48) + 16) + 0) == ((uint8_t)2ULL)));
                                                if (!t380) goto ie373;
                                                {
                                                    uint64_t t382 = gm_l2;
                                                    void * t383 = gm_l0;
                                                    uint32_t t384 = gm_l45;
                                                    gm_b16_8 t385 = gm_f_empty_slice_g3tddrb_2();
                                                    gm_b16_8 t386 = GM_LD(gm_b16_8, ((uint8_t *)&t385));
                                                    void * t387 = gm_l0;
                                                    gm_b16_8 t388 = GM_LD(gm_b16_8, ((((uint8_t *)t387) + 48) + 0));
                                                    gm_b16_8 t389 = GM_LD(gm_b16_8, ((uint8_t *)&t388));
                                                    void * t390 = gm_l1;
                                                    gm_tl874t2atnlh3 t391 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t390) + 72));
                                                    gm_toabfckqu3onu t392;
                                                    gm_f_stack_call_poqmlno((void *)&t392, (void *)NULL, cx, NULL, t383, t384, t386, t389, t391);
                                                    if (gm_unw.kind) {
                                                        goto gm_ll360;
                                                    }
                                                    if (((((uint8_t *)&t392))[0] != 0)) {
                                                        gm_toabfckqu3onu t393;
                                                        GM_ST(uint8_t, (uint8_t *)&t393 + 1, 0);
                                                        GM_ST(uint16_t, (uint8_t *)&t393 + 2, 0);
                                                        GM_ST(uint32_t, (uint8_t *)&t393 + 4, 0);
                                                        ((uint8_t *)&t393)[0] = 1;
                                                        GM_ST(gm_tgeh6n0c7c8kb, ((uint8_t *)&t393 + 8), (*(gm_tgeh6n0c7c8kb *)(((uint8_t *)&t392) + 8)));
                                                        F.ret = t393;
                                                        goto gm_fret;
                                                    }
                                                    uint64_t t394 = GM_LD(uint64_t, (((uint8_t *)&t392) + 8));
                                                    uint64_t t395 = (t382 > t394 ? t382 : t394);
                                                    gm_l2 = t395;
                                                }
                                                goto ix373;
ie373:;
ix373:;
                                            }
                                            goto me368;
mn369:;
                                            gm_trap("no match arm matched the scrutinee", gm_src4tam33qo20j7, 235, 21);
me368:;
                                        }
lc356:;
                                        {
                                            uint64_t t397 = gm_l43;
                                            uint64_t r398; bool o399 = __builtin_add_overflow(t397, ((uint64_t)1ULL), &r398);
                                            uint64_t t400 = r398;
                                            gm_l43 = t400;
                                        }
                                    }
gm_ll360:;
                                    if (gm_unw.frame == &F.h && gm_unw.label == 5) {
                                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb356; }
                                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc356; }
                                    }
                                    goto gm_ll337;
lb356:;
                                }
                            }
                            goto me342;
mn343:;
                            gm_trap("no match arm matched the scrutinee", gm_src4tam33qo20j7, 233, 17);
me342:;
                        }
lc333:;
                        {
                            uint64_t t402 = gm_l39;
                            uint64_t r403; bool o404 = __builtin_add_overflow(t402, ((uint64_t)1ULL), &r403);
                            uint64_t t405 = r403;
                            gm_l39 = t405;
                        }
                    }
gm_ll337:;
                    if (gm_unw.frame == &F.h && gm_unw.label == 4) {
                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb333; }
                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc333; }
                    }
                    goto gm_lad;
lb333:;
                }
            }
            goto ix320;
ie320:;
            {
                void * t407 = gm_l0;
                uint64_t t408 = GM_LD(uintptr_t, ((((uint8_t *)t407) + 48) + 0) + 8);
                gm_l46 = t408;
            }
            {
                void * t409 = gm_l0;
                void * t410 = (void *)(((uint8_t *)t409) + 48);
                gm_b16_8 t411 = GM_LD(gm_b16_8, (((uint8_t *)&F.l24) + 0));
                gm_b16_8 t412 = GM_LD(gm_b16_8, ((uint8_t *)&t411));
                gm_tftb1odc739t5 t413 = gm_f_extend_2kurgou(t410, t412);
                if (((((uint8_t *)&t413))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t413) + 1, gm_src4tam33qo20j7, 241, 17);
                gm_b0_1 t414 = GM_LD(gm_b0_1, (((uint8_t *)&t413) + 1));
            }
            {
                void * t415 = gm_l0;
                void * t416 = GM_LD(void *, (((uint8_t *)&F.l23) + 32));
                gm_f_stack_expr_33aekgt((void *)&F.l47, (void *)NULL, cx, NULL, t415, t416);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t417 = gm_l0;
                void * t418 = (void *)(((uint8_t *)t417) + 48);
                uint64_t t419 = gm_l46;
                gm_f_truncate_31h1fac(t418, t419);
            }
            {
                uint64_t t420 = gm_l2;
                gm_toabfckqu3onu t421 = F.l47;
                if (((((uint8_t *)&t421))[0] != 0)) {
                    gm_toabfckqu3onu t422;
                    GM_ST(uint8_t, (uint8_t *)&t422 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t422 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t422 + 4, 0);
                    ((uint8_t *)&t422)[0] = 1;
                    GM_ST(gm_tgeh6n0c7c8kb, ((uint8_t *)&t422 + 8), (*(gm_tgeh6n0c7c8kb *)(((uint8_t *)&t421) + 8)));
                    F.ret = t422;
                    goto gm_fret;
                }
                uint64_t t423 = GM_LD(uint64_t, (((uint8_t *)&t421) + 8));
                uint64_t t424 = (t420 > t423 ? t420 : t423);
                gm_l2 = t424;
            }
ix320:;
        }
        goto me12;
mn215:;
ma16:;
        uint8_t t426 = GM_LD(uint8_t, ((((uint8_t *)t11) + 8) + 0));
        if (t426 != ((uint8_t)46ULL)) goto mn425;
        void * t427 = GM_LD(void *, ((((uint8_t *)t11) + 8) + 8));
        gm_l48 = t427;
        {
            void * t429 = gm_l0;
            gm_b16_8 t430 = GM_LD(gm_b16_8, ((((uint8_t *)t429) + 8) + 0));
            void * t431 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t430)));
            uint64_t t432 = (uintptr_t)GM_LD(uintptr_t, ((uint8_t *)&t430) + 8);
            uint64_t t433 = (t432 - 1);
            gm_tijr008e33o2m t434;
            if (t433 < t432) {
            gm_tha5675rv512q t435 = GM_LD(gm_tha5675rv512q, ((uint8_t *)t431 + t433 * 48));
            gm_tijr008e33o2m t436;
            GM_ST(uint8_t, (uint8_t *)&t436 + 49, 0);
            GM_ST(uint16_t, (uint8_t *)&t436 + 50, 0);
            GM_ST(uint32_t, (uint8_t *)&t436 + 52, 0);
            GM_ST(gm_tha5675rv512q, ((uint8_t *)&t436), t435);
            ((uint8_t *)&t436)[48] = 1;
                t434 = t436;
            } else {
            gm_tijr008e33o2m t437;
            memset(&t437, 0, sizeof t437);
                t434 = t437;
            }
            gm_tha5675rv512q t438;
            if (((((uint8_t *)&t434))[48] != 0)) {
                t438 = (*(gm_tha5675rv512q *)(((uint8_t *)&t434) + 0));
            } else {
                gm_tgeh6n0c7c8kb t439;
                memset(&t439, 0, sizeof t439);
                GM_ST(uint8_t, ((uint8_t *)&t439 + 0), ((uint8_t)0ULL));
                GM_ST(uint8_t, (uint8_t *)&(*(gm_toabfckqu3onu *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_toabfckqu3onu *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_toabfckqu3onu *)gm_out) + 4, 0);
                ((uint8_t *)&(*(gm_toabfckqu3onu *)gm_out))[0] = 1;
                GM_ST(gm_tgeh6n0c7c8kb, (uint8_t *)&(*(gm_toabfckqu3onu *)gm_out) + 8, t439);
                goto gm_fdone;
            }
            F.l49 = t438;
        }
        {
            void * t440 = gm_l0;
            void * t441 = GM_LD(void *, (((uint8_t *)t440) + 0));
            uint32_t t442 = GM_LD(uint32_t, (((uint8_t *)&F.l49) + 0));
            gm_f_def_of_m512sbu((void *)&F.l50, t441, t442);
        }
        {
            {
                void * t444 = gm_l48;
                gm_b16_8 t445 = GM_LD(gm_b16_8, (((uint8_t *)t444) + 0));
                gm_l51 = t445;
            }
            {
                gm_l52 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t447 = gm_l52;
                    uint64_t t448 = GM_LD(uintptr_t, ((uint8_t *)&gm_l51) + 8);
                    uint8_t t449 = (t447 < t448);
                    if (!t449) goto lb446;
                    {
                        uint64_t t452 = gm_l52;
                        uint64_t t453 = (uintptr_t)t452;
                        uint64_t t454 = GM_LD(uintptr_t, ((uint8_t *)&gm_l51) + 8);
                        gm_trcmmd12cefb1 t457 = GM_LD(gm_trcmmd12cefb1, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l51)) + t453 * 32));
                        gm_l53 = t457;
                        {
                            uint8_t t460 = GM_LD(uint8_t, (((uint8_t *)&gm_l53) + 0));
                            if (t460 != ((uint8_t)0ULL)) goto ie459;
                            uint32_t t461 = GM_LD(uint32_t, ((((uint8_t *)&gm_l53) + 8) + 0));
                            gm_l54 = t461;
                            if (!(GM_LD(void *, ((((uint8_t *)&gm_l53) + 8) + 8)) != 0)) goto ie459;
                            void * t462 = GM_LD(void *, ((((uint8_t *)&gm_l53) + 8) + 8));
                            gm_l55 = t462;
                            {
                                uint32_t t465 = gm_l54;
                                uint64_t t466 = (uint64_t)t465;
                                uint64_t t467 = (uintptr_t)t466;
                                uint64_t t468 = GM_LD(uintptr_t, (((uint8_t *)&F.l50) + 24) + 8);
                                if (t467 >= (uintptr_t)(t468)) gm_trap_index(t467, t468, gm_src4tam33qo20j7, 254, 28);
                                uint8_t t469 = GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l50) + 24)) + t467 * 40) + 20));
                                uint8_t t470 = (uint8_t)!t469;
                                if (!t470) goto ie464;
                                {
                                    void * t473 = gm_l0;
                                    void * t474 = gm_l55;
                                    gm_t9805qtonbsag t475 = gm_f_stack_target_7vr78v6(t473, t474);
                                    if (!((((uint8_t *)&t475))[4] != 0)) goto ie472;
                                    uint32_t t476 = GM_LD(uint32_t, ((uint8_t *)&t475));
                                    gm_l56 = t476;
                                    {
                                        void * t478 = gm_l0;
                                        uint64_t t479 = GM_LD(uintptr_t, ((((uint8_t *)t478) + 8) + 0) + 8);
                                        uint64_t r480; bool o481 = __builtin_sub_overflow(t479, ((uint64_t)1ULL), &r480);
                                        if (o481) gm_trap("integer overflow in `-`", gm_src4tam33qo20j7, 256, 51);
                                        uint64_t t482 = r480;
                                        uint64_t t483 = (uintptr_t)t482;
                                        void * t484 = gm_l0;
                                        uint64_t t485 = GM_LD(uintptr_t, ((((uint8_t *)t484) + 8) + 0) + 8);
                                        if (t483 >= (uintptr_t)(t485)) gm_trap_index(t483, t485, gm_src4tam33qo20j7, 256, 38);
                                        void * t486 = (void *)(((uint8_t *)GM_LD(void *, ((((uint8_t *)t484) + 8) + 0)) + t483 * 48) + 8);
                                        uint32_t t487 = gm_l54;
                                        uint32_t t488 = gm_l56;
                                        gm_b0_1 t489 = gm_f_bind_fn_pefln9r((void *)NULL, cx, NULL, t486, t487, t488);
                                        if (gm_unw.kind) {
                                            goto gm_ll450;
                                        }
                                    }
                                    goto ix472;
ie472:;
ix472:;
                                }
                                goto ix464;
ie464:;
ix464:;
                            }
                            goto ix459;
ie459:;
ix459:;
                        }
                        goto me455;
mn456:;
                        gm_trap("no match arm matched the scrutinee", gm_src4tam33qo20j7, 252, 13);
me455:;
                    }
lc446:;
                    {
                        uint64_t t491 = gm_l52;
                        uint64_t r492; bool o493 = __builtin_add_overflow(t491, ((uint64_t)1ULL), &r492);
                        uint64_t t494 = r492;
                        gm_l52 = t494;
                    }
                }
gm_ll450:;
                if (gm_unw.frame == &F.h && gm_unw.label == 6) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb446; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc446; }
                }
                goto gm_lad;
lb446:;
            }
        }
        {
            {
                void * t496 = gm_l1;
                gm_b16_8 t497 = gm_f_stack_children_dedraea((void *)NULL, cx, NULL, t496);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l57 = t497;
            }
            {
                gm_l58 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t499 = gm_l58;
                    uint64_t t500 = GM_LD(uintptr_t, ((uint8_t *)&gm_l57) + 8);
                    uint8_t t501 = (t499 < t500);
                    if (!t501) goto lb498;
                    {
                        uint64_t t504 = gm_l58;
                        uint64_t t505 = (uintptr_t)t504;
                        uint64_t t506 = GM_LD(uintptr_t, ((uint8_t *)&gm_l57) + 8);
                        void * t507 = GM_LD(void *, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l57)) + t505 * 8));
                        void * t510 = GM_LD(void *, ((uint8_t *)&t507));
                        gm_l59 = t510;
                        {
                            uint64_t t512 = gm_l2;
                            void * t513 = gm_l0;
                            void * t514 = gm_l59;
                            gm_toabfckqu3onu t515;
                            gm_f_stack_expr_33aekgt((void *)&t515, (void *)NULL, cx, NULL, t513, t514);
                            if (gm_unw.kind) {
                                goto gm_ll502;
                            }
                            if (((((uint8_t *)&t515))[0] != 0)) {
                                gm_toabfckqu3onu t516;
                                GM_ST(uint8_t, (uint8_t *)&t516 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t516 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t516 + 4, 0);
                                ((uint8_t *)&t516)[0] = 1;
                                GM_ST(gm_tgeh6n0c7c8kb, ((uint8_t *)&t516 + 8), (*(gm_tgeh6n0c7c8kb *)(((uint8_t *)&t515) + 8)));
                                F.ret = t516;
                                goto gm_fret;
                            }
                            uint64_t t517 = GM_LD(uint64_t, (((uint8_t *)&t515) + 8));
                            uint64_t t518 = (t512 > t517 ? t512 : t517);
                            gm_l2 = t518;
                        }
                        goto me508;
mn509:;
                        gm_trap("no match arm matched the scrutinee", gm_src4tam33qo20j7, 261, 13);
me508:;
                    }
lc498:;
                    {
                        uint64_t t520 = gm_l58;
                        uint64_t r521; bool o522 = __builtin_add_overflow(t520, ((uint64_t)1ULL), &r521);
                        uint64_t t523 = r521;
                        gm_l58 = t523;
                    }
                }
gm_ll502:;
                if (gm_unw.frame == &F.h && gm_unw.label == 7) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb498; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc498; }
                }
                goto gm_lad;
lb498:;
            }
        }
        goto me12;
mn425:;
ma17:;
        {
            {
                void * t527 = gm_l1;
                gm_b16_8 t528 = gm_f_stack_children_dedraea((void *)NULL, cx, NULL, t527);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l60 = t528;
            }
            {
                gm_l61 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t530 = gm_l61;
                    uint64_t t531 = GM_LD(uintptr_t, ((uint8_t *)&gm_l60) + 8);
                    uint8_t t532 = (t530 < t531);
                    if (!t532) goto lb529;
                    {
                        uint64_t t535 = gm_l61;
                        uint64_t t536 = (uintptr_t)t535;
                        uint64_t t537 = GM_LD(uintptr_t, ((uint8_t *)&gm_l60) + 8);
                        void * t538 = GM_LD(void *, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l60)) + t536 * 8));
                        void * t541 = GM_LD(void *, ((uint8_t *)&t538));
                        gm_l62 = t541;
                        {
                            uint64_t t543 = gm_l2;
                            void * t544 = gm_l0;
                            void * t545 = gm_l62;
                            gm_toabfckqu3onu t546;
                            gm_f_stack_expr_33aekgt((void *)&t546, (void *)NULL, cx, NULL, t544, t545);
                            if (gm_unw.kind) {
                                goto gm_ll533;
                            }
                            if (((((uint8_t *)&t546))[0] != 0)) {
                                gm_toabfckqu3onu t547;
                                GM_ST(uint8_t, (uint8_t *)&t547 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t547 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t547 + 4, 0);
                                ((uint8_t *)&t547)[0] = 1;
                                GM_ST(gm_tgeh6n0c7c8kb, ((uint8_t *)&t547 + 8), (*(gm_tgeh6n0c7c8kb *)(((uint8_t *)&t546) + 8)));
                                F.ret = t547;
                                goto gm_fret;
                            }
                            uint64_t t548 = GM_LD(uint64_t, (((uint8_t *)&t546) + 8));
                            uint64_t t549 = (t543 > t548 ? t543 : t548);
                            gm_l2 = t549;
                        }
                        goto me539;
mn540:;
                        gm_trap("no match arm matched the scrutinee", gm_src4tam33qo20j7, 264, 13);
me539:;
                    }
lc529:;
                    {
                        uint64_t t551 = gm_l61;
                        uint64_t r552; bool o553 = __builtin_add_overflow(t551, ((uint64_t)1ULL), &r552);
                        uint64_t t554 = r552;
                        gm_l61 = t554;
                    }
                }
gm_ll533:;
                if (gm_unw.frame == &F.h && gm_unw.label == 8) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb529; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc529; }
                }
                goto gm_lad;
lb529:;
            }
        }
        goto me12;
mn524:;
ma18:;
        gm_trap("no match arm matched the scrutinee", gm_src4tam33qo20j7, 178, 5);
me12:;
    }
    {
        uint64_t t555 = gm_l2;
        memset(&(*(gm_toabfckqu3onu *)gm_out), 0, sizeof (*(gm_toabfckqu3onu *)gm_out));
        ((uint8_t *)&(*(gm_toabfckqu3onu *)gm_out))[0] = 0;
        GM_ST(uint64_t, (uint8_t *)&(*(gm_toabfckqu3onu *)gm_out) + 8, t555);
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
inline __attribute__((aligned(4))) uint64_t gm_f_stack_frame_68512ca(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1) {
    struct gm_fr_stack_frame_68512ca F;
    void * gm_l0; /* a */
    uint32_t gm_l1; /* g */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    gm_tb31k5l794t7p gm_l4; /* c */
    uint64_t gm_l6; /* c */
    GM_STACK_CHECK(&F, 330u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        {
            void * t3 = gm_l0;
            gm_b16_8 t4 = GM_LD(gm_b16_8, ((((uint8_t *)t3) + 128) + 0));
            gm_l2 = t4;
        }
        {
            gm_l3 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t6 = gm_l3;
                uint64_t t7 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                uint8_t t8 = (t6 < t7);
                if (!t8) goto lb5;
                {
                    uint64_t t11 = gm_l3;
                    uint64_t t12 = (uintptr_t)t11;
                    uint64_t t13 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                    gm_tb31k5l794t7p t16 = GM_LD(gm_tb31k5l794t7p, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t12 * 16));
                    gm_l4 = t16;
                    {
                        uint32_t t19 = GM_LD(uint32_t, (((uint8_t *)&gm_l4) + 0));
                        uint32_t t20 = gm_l1;
                        uint8_t t21 = ((t19 == t20));
                        if (!t21) goto ie18;
                        {
                            uint64_t t23 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                            uint8_t t24 = 0;
                            F.ret = t23;
                            goto gm_fret;
                        }
                        goto ix18;
ie18:;
ix18:;
                    }
                    goto me14;
mn15:;
                    gm_trap("no match arm matched the scrutinee", gm_src4tam33qo20j7, 132, 5);
me14:;
                }
lc5:;
                {
                    uint64_t t26 = gm_l3;
                    uint64_t r27; bool o28 = __builtin_add_overflow(t26, ((uint64_t)1ULL), &r27);
                    uint64_t t29 = r27;
                    gm_l3 = t29;
                }
            }
lb5:;
        }
    }
    {
        void * t30 = gm_l0;
        void * t31 = GM_LD(void *, (((uint8_t *)t30) + 0));
        uint32_t t32 = gm_l1;
        gm_f_def_of_m512sbu((void *)&F.l5, t31, t32);
    }
    {
        void * t33 = gm_l0;
        void * t34 = GM_LD(void *, (((uint8_t *)t33) + 0));
        gm_b16_8 t35 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 24));
        gm_b16_8 t36 = GM_LD(gm_b16_8, ((uint8_t *)&t35));
        void * t37 = GM_LD(void *, (((uint8_t *)&F.l5) + 40));
        uint64_t t38 = gm_f_frame_cost_oei64tv((void *)NULL, cx, NULL, t34, t36, t37);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l6 = t38;
    }
    {
        void * t39 = gm_l0;
        void * t40 = (void *)(((uint8_t *)t39) + 128);
        uint32_t t41 = gm_l1;
        uint64_t t42 = gm_l6;
        gm_tb31k5l794t7p t43;
        GM_ST(uint32_t, (uint8_t *)&t43 + 4, 0);
        GM_ST(uint32_t, ((uint8_t *)&t43 + 0), t41);
        GM_ST(uint64_t, ((uint8_t *)&t43 + 8), t42);
        gm_tftb1odc739t5 t44 = gm_f_push_1mcf4ec(t40, GM_CONV(gm_cskjv6t053tkje, t43));
        if (((((uint8_t *)&t44))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t44) + 1, gm_src4tam33qo20j7, 137, 5);
        gm_b0_1 t45 = GM_LD(gm_b0_1, (((uint8_t *)&t44) + 1));
    }
    {
        uint64_t t46 = gm_l6;
        uint8_t t47 = 0;
        F.ret = t46;
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
__attribute__((aligned(4))) void gm_f_stack_perform_soivemm(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint32_t a2, gm_b16_8 a3, gm_tl874t2atnlh3 a4) {
    struct gm_fr_stack_perform_soivemm F;
    void * gm_l0; /* a */
    void * gm_l1; /* eref */
    uint32_t gm_l2; /* op */
    gm_b16_8 gm_l3; /* args */
    gm_tl874t2atnlh3 gm_l4; /* span */
    gm_b16_8 gm_l5; /* ops */
    gm_b16_8 gm_l6; /* unknown */
    uint64_t gm_l7; /* k */
    GM_STACK_CHECK(&F, 869u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    {
        void * t2 = gm_l1;
        uint32_t t3 = GM_LD(uint32_t, (((uint8_t *)t2) + 0));
        uint64_t t4 = (uint64_t)t3;
        uint64_t t5 = (uintptr_t)t4;
        void * t6 = gm_l0;
        void * t7 = GM_LD(void *, (((uint8_t *)t6) + 0));
        uint64_t t8 = GM_LD(uintptr_t, ((((uint8_t *)t7) + 1048) + 0) + 8);
        if (t5 >= (uintptr_t)(t8)) gm_trap_index(t5, t8, gm_src4tam33qo20j7, 289, 12);
        gm_b16_8 t9 = GM_LD(gm_b16_8, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t7) + 1048) + 0)) + t5 * 56) + 40));
        gm_l5 = t9;
    }
    {
        uint32_t t11 = gm_l2;
        uint64_t t12 = (uint64_t)t11;
        uint64_t t13 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
        uint8_t t14 = (t12 < t13);
        if (!t14) goto ie10;
        uint32_t t15 = gm_l2;
        uint64_t t16 = (uint64_t)t15;
        uint64_t t17 = (uintptr_t)t16;
        uint64_t t18 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
        if (t17 >= (uintptr_t)(t18)) gm_trap_index(t17, t18, gm_src4tam33qo20j7, 291, 32);
        uint8_t t19 = ((GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l5)) + t17 * 48) + 16) + 0) == ((uint8_t)2ULL)));
        if (!t19) goto ie10;
        {
            memset(&(*(gm_toabfckqu3onu *)gm_out), 0, sizeof (*(gm_toabfckqu3onu *)gm_out));
            ((uint8_t *)&(*(gm_toabfckqu3onu *)gm_out))[0] = 0;
            GM_ST(uint64_t, (uint8_t *)&(*(gm_toabfckqu3onu *)gm_out) + 8, ((uint64_t)0ULL));
            goto gm_fdone;
        }
        goto ix10;
ie10:;
ix10:;
    }
    {
        void * t21 = gm_l0;
        void * t22 = GM_LD(void *, (((uint8_t *)t21) + 0));
        void * t23 = (void *)(((uint8_t *)t22) + 8);
        void * t24 = gm_l1;
        gm_b16_8 t25 = gm_f_show_ref_aqt1t7c((void *)NULL, cx, NULL, t23, t24);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tnpifv20irnph t26;
        GM_ST(gm_b16_8, ((uint8_t *)&t26 + 0), t25);
        gm_tabb710d9mq2e t27;
        gm_f_format_845n386((void *)&t27, (void *)NULL, cx, NULL, t26);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t27))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t27) + 8, gm_src4tam33qo20j7, 292, 16);
        gm_b16_8 t28 = GM_LD(gm_b16_8, (((uint8_t *)&t27) + 8));
        gm_l6 = t28;
    }
    {
        void * t29 = gm_l0;
        uint64_t t30 = GM_LD(uintptr_t, ((((uint8_t *)t29) + 48) + 0) + 8);
        gm_l7 = t30;
    }
    {
        for (;;) {
            uint64_t t32 = gm_l7;
            uint8_t t33 = (t32 > ((uint64_t)0ULL));
            if (!t33) goto lb31;
            {
                uint64_t t36 = gm_l7;
                uint64_t r37; bool o38 = __builtin_sub_overflow(t36, ((uint64_t)1ULL), &r37);
                if (o38) gm_trap("integer overflow in `-`", gm_src4tam33qo20j7, 295, 9);
                uint64_t t39 = r37;
                gm_l7 = t39;
            }
            {
                uint64_t t41 = gm_l7;
                uint64_t t42 = (uintptr_t)t41;
                void * t43 = gm_l0;
                uint64_t t44 = GM_LD(uintptr_t, ((((uint8_t *)t43) + 48) + 0) + 8);
                if (t42 >= (uintptr_t)(t44)) gm_trap_index(t42, t44, gm_src4tam33qo20j7, 296, 27);
                void * t45 = (void *)(((uint8_t *)GM_LD(void *, ((((uint8_t *)t43) + 48) + 0)) + t42 * 40) + 0);
                void * t46 = gm_l1;
                int32_t t47 = gm_f_cmp_ref_8ntrqll(t45, t46);
                uint8_t t48 = (!(t47 == ((int32_t)0LL)));
                if (!t48) goto ie40;
                {
                    goto lc31;
                }
                goto ix40;
ie40:;
ix40:;
            }
            {
                uint64_t t50 = gm_l7;
                uint64_t t51 = (uintptr_t)t50;
                void * t52 = gm_l0;
                uint64_t t53 = GM_LD(uintptr_t, ((((uint8_t *)t52) + 48) + 0) + 8);
                if (t51 >= (uintptr_t)(t53)) gm_trap_index(t51, t53, gm_src4tam33qo20j7, 297, 14);
                gm_t1cs2fjnrqap1 t54 = GM_LD(gm_t1cs2fjnrqap1, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t52) + 48) + 0)) + t51 * 40));
                F.l8 = t54;
            }
            {
                uint32_t t56 = gm_l2;
                uint64_t t57 = (uint64_t)t56;
                uint64_t t58 = GM_LD(uintptr_t, (((uint8_t *)&F.l8) + 24) + 8);
                uint8_t t59 = (t57 >= t58);
                if (!t59) goto ie55;
                {
                    gm_tbgd14esf0jvi t61;
                    memset((uint8_t *)&t61 + 12, 0, 4);
                    gm_tl874t2atnlh3 t62 = gm_l4;
                    GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t61 + 0, t62);
                    gm_b16_8 t63 = gm_l6;
                    gm_b16_8 t64 = GM_LD(gm_b16_8, ((uint8_t *)&t63));
                    GM_ST(gm_b16_8, (uint8_t *)&t61 + 16, t64);
                    gm_tgeh6n0c7c8kb t65;
                    GM_ST(uint8_t, (uint8_t *)&t65 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t65 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t65 + 4, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t65 + 0), ((uint8_t)1ULL));
                    GM_ST(gm_tbgd14esf0jvi, ((uint8_t *)&t65 + 8), t61);
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_toabfckqu3onu *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_toabfckqu3onu *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_toabfckqu3onu *)gm_out) + 4, 0);
                    ((uint8_t *)&(*(gm_toabfckqu3onu *)gm_out))[0] = 1;
                    GM_ST(gm_tgeh6n0c7c8kb, (uint8_t *)&(*(gm_toabfckqu3onu *)gm_out) + 8, t65);
                    goto gm_fdone;
                }
                goto ix55;
ie55:;
ix55:;
            }
            {
                void * t66 = gm_l0;
                uint32_t t67 = gm_l2;
                uint64_t t68 = (uint64_t)t67;
                uint64_t t69 = (uintptr_t)t68;
                uint64_t t70 = GM_LD(uintptr_t, (((uint8_t *)&F.l8) + 24) + 8);
                if (t69 >= (uintptr_t)(t70)) gm_trap_index(t69, t70, gm_src4tam33qo20j7, 299, 30);
                uint32_t t71 = GM_LD(uint32_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l8) + 24)) + t69 * 4));
                gm_b16_8 t72 = gm_l3;
                void * t73 = gm_l0;
                uint64_t t74 = GM_LD(uintptr_t, ((((uint8_t *)t73) + 48) + 0) + 8);
                void * t75 = ((uint8_t *)GM_LD(void *, ((((uint8_t *)t73) + 48) + 0)));
                uint64_t t76 = gm_l7;
                uint64_t t77 = (uintptr_t)t76;
                if ((uintptr_t)0 > t77 || t77 > (uintptr_t)t74) gm_trap_slice((uintptr_t)0, t77, t74, gm_src4tam33qo20j7, 299, 58);
                gm_b16_8 t78;
                GM_ST(void *, (uint8_t *)&t78, (void *)(((uint8_t *)t75 + (uintptr_t)0 * 40)));
                GM_ST(uintptr_t, (uint8_t *)&t78 + 8, (uintptr_t)(t77 - (uintptr_t)0));
                gm_b16_8 t79 = GM_LD(gm_b16_8, ((uint8_t *)&t78));
                gm_tl874t2atnlh3 t80 = gm_l4;
                gm_f_stack_call_poqmlno((void *)&(*(gm_toabfckqu3onu *)gm_out), (void *)NULL, cx, NULL, t66, t71, t72, t79, t80);
                if (gm_unw.kind) {
                    goto gm_ll34;
                }
                goto gm_fdone;
            }
lc31:;
        }
gm_ll34:;
        if (gm_unw.frame == &F.h && gm_unw.label == 0) {
            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb31; }
            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc31; }
        }
        goto gm_lad;
lb31:;
    }
    {
        gm_tbgd14esf0jvi t81;
        memset((uint8_t *)&t81 + 12, 0, 4);
        gm_tl874t2atnlh3 t82 = gm_l4;
        GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t81 + 0, t82);
        gm_b16_8 t83 = gm_l6;
        gm_b16_8 t84 = GM_LD(gm_b16_8, ((uint8_t *)&t83));
        GM_ST(gm_b16_8, (uint8_t *)&t81 + 16, t84);
        gm_tgeh6n0c7c8kb t85;
        GM_ST(uint8_t, (uint8_t *)&t85 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t85 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t85 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t85 + 0), ((uint8_t)1ULL));
        GM_ST(gm_tbgd14esf0jvi, ((uint8_t *)&t85 + 8), t81);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_toabfckqu3onu *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_toabfckqu3onu *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_toabfckqu3onu *)gm_out) + 4, 0);
        ((uint8_t *)&(*(gm_toabfckqu3onu *)gm_out))[0] = 1;
        GM_ST(gm_tgeh6n0c7c8kb, (uint8_t *)&(*(gm_toabfckqu3onu *)gm_out) + 8, t85);
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
inline __attribute__((aligned(4))) void gm_f_stack_push_expr_bdl56rb(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, const void * a2, void * a3) {
    struct gm_fr_stack_push_expr_bdl56rb F;
    void * gm_l0; /* a */
    uint32_t gm_l1; /* g */
    void * gm_l3; /* body */
    GM_STACK_CHECK(&F, 179u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    memcpy(&F.l2, a2, sizeof F.l2);
    gm_l3 = a3;
    {
        void * t2 = gm_l0;
        void * t3 = (void *)(((uint8_t *)t2) + 8);
        gm_tha5675rv512q t4;
        memset((uint8_t *)&t4 + 4, 0, 4);
        uint32_t t5 = gm_l1;
        GM_ST(uint32_t, (uint8_t *)&t4 + 0, t5);
        memcpy((uint8_t *)&t4 + 8, ((uint8_t *)&F.l2), 40);
        gm_tftb1odc739t5 t6 = gm_f_push_9gqhmau(t3, &t4);
        if (((((uint8_t *)&t6))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t6) + 1, gm_src4tam33qo20j7, 124, 5);
        gm_b0_1 t7 = GM_LD(gm_b0_1, (((uint8_t *)&t6) + 1));
    }
    {
        void * t8 = gm_l0;
        void * t9 = gm_l3;
        gm_f_stack_expr_33aekgt((void *)&F.l4, (void *)NULL, cx, NULL, t8, t9);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        uint8_t t11;
        if (!((((uint8_t *)&F.l4))[0] != 0)) goto mn13;
        t11 = ((uint8_t)0);
        goto me12;
mn13:;
        t11 = ((uint8_t)1);
        goto me12;
mn14:;
        gm_trap("no match arm matched the scrutinee", gm_src4tam33qo20j7, 126, 8);
me12:;
        if (!t11) goto ie10;
        {
            {
                void * t17 = gm_l0;
                void * t18 = (void *)(((uint8_t *)t17) + 8);
                gm_tijr008e33o2m t19;
                gm_f_pop_d7svgs5((void *)&t19, t18);
            }
        }
        goto ix10;
ie10:;
ix10:;
    }
    {
        memmove(gm_out, ((uint8_t *)&F.l4), 48);
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
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_stack_size_0gijrqq(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_tl874t2atnlh3 a2) {
    struct gm_fr_stack_size_0gijrqq F;
    void * gm_l0; /* s */
    uint32_t gm_l1; /* f */
    gm_tl874t2atnlh3 gm_l2; /* span */
    gm_tl874t2atnlh3 gm_l5; /* at */
    gm_b16_8 gm_l6; /* why */
    gm_b16_8 gm_l8; /* <tmp> */
    uint64_t gm_l9; /* <tmp> */
    gm_b16_8 gm_l11; /* name */
    uint64_t gm_l13; /* frames */
    GM_STACK_CHECK(&F, 1033u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        GM_ST(void *, (uint8_t *)&F.l3 + 0, t2);
        gm_f_list_pe70gj7((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l3 + 8)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_list_o00nc4s((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l3 + 48)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_list_msjtuv4((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l3 + 88)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_list_msjtuv4((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l3 + 128)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t3 = (void *)((uint8_t *)&F.l3);
        uint32_t t4 = gm_l1;
        gm_b16_8 t5 = gm_f_empty_slice_g3tddrb_2();
        gm_b16_8 t6 = GM_LD(gm_b16_8, ((uint8_t *)&t5));
        gm_b16_8 t7 = gm_f_empty_slice_vndn3a4();
        gm_b16_8 t8 = GM_LD(gm_b16_8, ((uint8_t *)&t7));
        gm_tl874t2atnlh3 t9 = gm_l2;
        gm_f_stack_call_poqmlno((void *)&F.l4, (void *)NULL, cx, NULL, t3, t4, t6, t8, t9);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        if (!((((uint8_t *)&F.l4))[0] != 0)) goto mn11;
        uint8_t t12 = GM_LD(uint8_t, ((((uint8_t *)&F.l4) + 8) + 0));
        if (t12 != ((uint8_t)0ULL)) goto mn11;
        gm_tf5srh4mp4b3i t13;
        memset(&t13, 0, sizeof t13);
        uint8_t t14 = 0;
        F.ret = t13;
        goto gm_fret;
        goto me10;
mn11:;
        if (!((((uint8_t *)&F.l4))[0] != 0)) goto mn15;
        uint8_t t16 = GM_LD(uint8_t, ((((uint8_t *)&F.l4) + 8) + 0));
        if (t16 != ((uint8_t)1ULL)) goto mn15;
        gm_tl874t2atnlh3 t17 = GM_LD(gm_tl874t2atnlh3, (((((uint8_t *)&F.l4) + 8) + 8) + 0));
        gm_l5 = t17;
        gm_b16_8 t18 = GM_LD(gm_b16_8, (((((uint8_t *)&F.l4) + 8) + 8) + 16));
        gm_l6 = t18;
        {
            gm_f_list_epvducg((void *)&F.l7, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            {
                gm_b16_8 t21 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l3) + 8) + 0));
                gm_l8 = t21;
            }
            {
                gm_l9 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t23 = gm_l9;
                    uint64_t t24 = GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8);
                    uint8_t t25 = (t23 < t24);
                    if (!t25) goto lb22;
                    {
                        uint64_t t28 = gm_l9;
                        uint64_t t29 = (uintptr_t)t28;
                        uint64_t t30 = GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8);
                        gm_tha5675rv512q t33 = GM_LD(gm_tha5675rv512q, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l8)) + t29 * 48));
                        F.l10 = t33;
                        {
                            void * t35 = (void *)((uint8_t *)&F.l7);
                            uint32_t t36 = GM_LD(uint32_t, (((uint8_t *)&F.l10) + 0));
                            uint64_t t37 = (uint64_t)t36;
                            uint64_t t38 = (uintptr_t)t37;
                            void * t39 = gm_l0;
                            uint64_t t40 = GM_LD(uintptr_t, ((((uint8_t *)t39) + 344) + 0) + 8);
                            if (t38 >= (uintptr_t)(t40)) gm_trap_index(t38, t40, gm_src4tam33qo20j7, 71, 68);
                            gm_b16_8 t41 = GM_LD(gm_b16_8, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t39) + 344) + 0)) + t38 * 136) + 0));
                            gm_tnpifv20irnph t42;
                            GM_ST(gm_b16_8, ((uint8_t *)&t42 + 0), t41);
                            gm_tabb710d9mq2e t43;
                            gm_f_format_h4jcodi((void *)&t43, (void *)NULL, cx, NULL, t42);
                            if (gm_unw.kind) {
                                goto gm_ll26;
                            }
                            if (((((uint8_t *)&t43))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t43) + 8, gm_src4tam33qo20j7, 71, 49);
                            gm_b16_8 t44 = GM_LD(gm_b16_8, (((uint8_t *)&t43) + 8));
                            gm_b16_8 t45 = GM_LD(gm_b16_8, ((uint8_t *)&t44));
                            gm_tftb1odc739t5 t46 = gm_f_push_3ef746b(t35, t45);
                            if (((((uint8_t *)&t46))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t46) + 1, gm_src4tam33qo20j7, 71, 38);
                            gm_b0_1 t47 = GM_LD(gm_b0_1, (((uint8_t *)&t46) + 1));
                        }
                        goto me31;
mn32:;
                        gm_trap("no match arm matched the scrutinee", gm_src4tam33qo20j7, 71, 13);
me31:;
                    }
lc22:;
                    {
                        uint64_t t49 = gm_l9;
                        uint64_t r50; bool o51 = __builtin_add_overflow(t49, ((uint64_t)1ULL), &r50);
                        uint64_t t52 = r50;
                        gm_l9 = t52;
                    }
                }
gm_ll26:;
                if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb22; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc22; }
                }
                goto gm_lad;
lb22:;
            }
        }
        {
            uint32_t t53 = gm_l1;
            uint64_t t54 = (uint64_t)t53;
            uint64_t t55 = (uintptr_t)t54;
            void * t56 = gm_l0;
            uint64_t t57 = GM_LD(uintptr_t, ((((uint8_t *)t56) + 344) + 0) + 8);
            if (t55 >= (uintptr_t)(t57)) gm_trap_index(t55, t57, gm_src4tam33qo20j7, 72, 21);
            gm_b16_8 t58 = GM_LD(gm_b16_8, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t56) + 344) + 0)) + t55 * 136) + 0));
            gm_l11 = t58;
        }
        {
            gm_tl874t2atnlh3 t59 = gm_l5;
            gm_t6j6smj7qoctu t60;
            gm_b16_8 t61 = gm_l11;
            GM_ST(gm_b16_8, (uint8_t *)&t60 + 0, t61);
            gm_b16_8 t62 = gm_l6;
            GM_ST(gm_b16_8, (uint8_t *)&t60 + 16, t62);
            gm_tabb710d9mq2e t63;
            gm_f_format_t68frbc((void *)&t63, (void *)NULL, cx, NULL, &t60);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t63))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t63) + 8, gm_src4tam33qo20j7, 73, 33);
            gm_b16_8 t64 = GM_LD(gm_b16_8, (((uint8_t *)&t63) + 8));
            gm_b16_8 t65 = GM_LD(gm_b16_8, ((uint8_t *)&t64));
            gm_f_error_s6hn871((void *)&F.l12, (void *)NULL, cx, NULL, t59, t65);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t66 = (void *)((uint8_t *)&F.l12);
            gm_tl874t2atnlh3 t67 = gm_l2;
            gm_b16_8 t68;
            GM_ST(void *, (uint8_t *)&t68, (void *)(gm_sdscrog2npict));
            GM_ST(uintptr_t, (uint8_t *)&t68 + 8, (uintptr_t)(22));
            gm_f_add_label_50fj7vv(t66, GM_CONV(gm_cseahvg8v7p0vs, t67), t68);
        }
        {
            uint64_t t70 = GM_LD(uintptr_t, (((uint8_t *)&F.l7) + 0) + 8);
            uint8_t t71 = (t70 > ((uint64_t)1ULL));
            if (!t71) goto ie69;
            {
                void * t73 = (void *)((uint8_t *)&F.l12);
                gm_b16_8 t74 = GM_LD(gm_b16_8, (((uint8_t *)&F.l7) + 0));
                gm_b16_8 t75 = GM_LD(gm_b16_8, ((uint8_t *)&t74));
                gm_b16_8 t76;
                GM_ST(void *, (uint8_t *)&t76, (void *)(gm_sdsrtcf8ng48d));
                GM_ST(uintptr_t, (uint8_t *)&t76 + 8, (uintptr_t)(5));
                gm_tabb710d9mq2e t77;
                gm_f_join_sfknljb((void *)&t77, (void *)NULL, cx, NULL, t75, t76);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t77))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t77) + 8, gm_src4tam33qo20j7, 75, 82);
                gm_b16_8 t78 = GM_LD(gm_b16_8, (((uint8_t *)&t77) + 8));
                gm_tnpifv20irnph t79;
                GM_ST(gm_b16_8, ((uint8_t *)&t79 + 0), t78);
                gm_tabb710d9mq2e t80;
                gm_f_format_hp1rthv((void *)&t80, (void *)NULL, cx, NULL, t79);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t80))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t80) + 8, gm_src4tam33qo20j7, 75, 49);
                gm_b16_8 t81 = GM_LD(gm_b16_8, (((uint8_t *)&t80) + 8));
                gm_b16_8 t82 = GM_LD(gm_b16_8, ((uint8_t *)&t81));
                gm_f_add_note_46v502s(t73, t82);
            }
            goto ix69;
ie69:;
ix69:;
        }
        {
            void * t83 = gm_l0;
            gm_b0_1 t84 = gm_f_emit_dph5v1e((void *)NULL, cx, NULL, t83, &(*(gm_t4gut2c9fj1kt *)((uint8_t *)&F.l12)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_tf5srh4mp4b3i t85;
            memset(&t85, 0, sizeof t85);
            uint8_t t86 = 0;
            F.ret = t85;
            goto gm_fret;
        }
        goto me10;
mn15:;
        if (((((uint8_t *)&F.l4))[0] != 0)) goto mn87;
        uint64_t t88 = GM_LD(uint64_t, (((uint8_t *)&F.l4) + 8));
        gm_l13 = t88;
        uint64_t t89 = GM_LD(uint64_t, gm_cb7r52n9k7f);
        uint64_t t90 = GM_LD(uint64_t, gm_cilec57f4ei);
        uint64_t r91; bool o92 = __builtin_add_overflow(t89, t90, &r91);
        if (o92) gm_trap("integer overflow in `+`", gm_src4tam33qo20j7, 79, 28);
        uint64_t t93 = r91;
        uint64_t t94 = gm_l13;
        uint64_t r95; bool o96 = __builtin_add_overflow(t93, t94, &r95);
        uint64_t t97 = o96 ? (1 ? ((uint64_t)18446744073709551615ULL) : ((uint64_t)0ULL)) : r95;
        uint64_t t98 = GM_LD(uint64_t, gm_cdlh2b67kra);
        uint64_t t99 = (t97 > t98 ? t97 : t98);
        gm_tf5srh4mp4b3i t100;
        GM_ST(uint8_t, (uint8_t *)&t100 + 9, 0);
        GM_ST(uint16_t, (uint8_t *)&t100 + 10, 0);
        GM_ST(uint32_t, (uint8_t *)&t100 + 12, 0);
        GM_ST(uint64_t, ((uint8_t *)&t100), t99);
        ((uint8_t *)&t100)[8] = 1;
        uint8_t t101 = 0;
        F.ret = t100;
        goto gm_fret;
        goto me10;
mn87:;
        gm_trap("no match arm matched the scrutinee", gm_src4tam33qo20j7, 67, 5);
me10:;
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
__attribute__((aligned(4))) gm_t9805qtonbsag gm_f_stack_target_7vr78v6(void * a0, void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_stack_target_7vr78v6 F;
    void * gm_l0; /* a */
    void * gm_l1; /* e */
    uint32_t gm_l2; /* f */
    uint32_t gm_l3; /* f */
    void * gm_l4; /* x */
    uint32_t gm_l5; /* l */
    uint32_t gm_l7; /* levels */
    uint32_t gm_l8; /* local */
    uint32_t gm_l10; /* owner */
    uint32_t gm_l11; /* <tmp> */
    uint32_t gm_l12; /* <tmp> */
    uint64_t gm_l13; /* k */
    GM_STACK_CHECK(&F, 1114u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l1;
        uint8_t t10 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        switch (t10) { case ((uint8_t)0ULL): goto ma8; case ((uint8_t)1ULL): goto ma8; case ((uint8_t)2ULL): goto ma8; case ((uint8_t)3ULL): goto ma8; case ((uint8_t)4ULL): goto ma8; case ((uint8_t)5ULL): goto ma8; case ((uint8_t)6ULL): goto ma8; case ((uint8_t)7ULL): goto ma8; case ((uint8_t)8ULL): goto ma8; case ((uint8_t)9ULL): goto ma8; case ((uint8_t)10ULL): goto ma8; case ((uint8_t)11ULL): goto ma8; case ((uint8_t)12ULL): goto ma8; case ((uint8_t)13ULL): goto ma8; case ((uint8_t)14ULL): goto ma8; case ((uint8_t)15ULL): goto ma6; case ((uint8_t)16ULL): goto ma7; case ((uint8_t)17ULL): goto ma8; case ((uint8_t)18ULL): goto ma8; case ((uint8_t)19ULL): goto ma8; case ((uint8_t)20ULL): goto ma8; case ((uint8_t)21ULL): goto ma8; case ((uint8_t)22ULL): goto ma8; case ((uint8_t)23ULL): goto ma8; case ((uint8_t)24ULL): goto ma8; case ((uint8_t)25ULL): goto ma8; case ((uint8_t)26ULL): goto ma4; case ((uint8_t)27ULL): goto ma4; case ((uint8_t)28ULL): goto ma8; case ((uint8_t)29ULL): goto ma8; case ((uint8_t)30ULL): goto ma8; case ((uint8_t)31ULL): goto ma8; case ((uint8_t)32ULL): goto ma5; case ((uint8_t)33ULL): goto ma8; case ((uint8_t)34ULL): goto ma8; case ((uint8_t)35ULL): goto ma8; case ((uint8_t)36ULL): goto ma8; case ((uint8_t)37ULL): goto ma8; case ((uint8_t)38ULL): goto ma8; case ((uint8_t)39ULL): goto ma8; case ((uint8_t)40ULL): goto ma8; case ((uint8_t)41ULL): goto ma8; case ((uint8_t)42ULL): goto ma8; case ((uint8_t)43ULL): goto ma8; case ((uint8_t)44ULL): goto ma8; case ((uint8_t)45ULL): goto ma8; case ((uint8_t)46ULL): goto ma8; case ((uint8_t)47ULL): goto ma8; case ((uint8_t)48ULL): goto ma8; case ((uint8_t)49ULL): goto ma8; case ((uint8_t)50ULL): goto ma8; case ((uint8_t)51ULL): goto ma8; case ((uint8_t)52ULL): goto ma8; case ((uint8_t)53ULL): goto ma8; case ((uint8_t)54ULL): goto ma8; case ((uint8_t)55ULL): goto ma8; case ((uint8_t)56ULL): goto ma8; case ((uint8_t)57ULL): goto ma8; case ((uint8_t)58ULL): goto ma8; default: goto ma8; }
ma4:;
        uint8_t t14 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t14 != ((uint8_t)26ULL)) goto pa13;
        uint32_t t15 = GM_LD(uint32_t, ((((uint8_t *)t2) + 8) + 8));
        gm_l2 = t15;
        goto po12;
pa13:;
        uint8_t t17 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t17 != ((uint8_t)27ULL)) goto pa16;
        uint32_t t18 = GM_LD(uint32_t, ((((uint8_t *)t2) + 8) + 8));
        gm_l2 = t18;
        goto po12;
pa16:;
        goto mn11;
po12:;
        uint32_t t19 = gm_l2;
        gm_t9805qtonbsag t20;
        GM_ST(uint8_t, (uint8_t *)&t20 + 5, 0);
        GM_ST(uint16_t, (uint8_t *)&t20 + 6, 0);
        GM_ST(uint32_t, ((uint8_t *)&t20), t19);
        ((uint8_t *)&t20)[4] = 1;
        uint8_t t21 = 0;
        F.ret = t20;
        goto gm_fret;
        goto me3;
mn11:;
ma5:;
        uint8_t t23 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t23 != ((uint8_t)32ULL)) goto mn22;
        void * t24 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 8));
        gm_l4 = t24;
        void * t25 = gm_l0;
        void * t26 = gm_l4;
        gm_t9805qtonbsag t27 = gm_f_stack_target_7vr78v6(t25, t26);
        uint8_t t28 = 0;
        F.ret = t27;
        goto gm_fret;
        goto me3;
mn22:;
ma6:;
        uint8_t t30 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t30 != ((uint8_t)15ULL)) goto mn29;
        uint32_t t31 = GM_LD(uint32_t, ((((uint8_t *)t2) + 8) + 8));
        gm_l5 = t31;
        {
            void * t33 = gm_l0;
            gm_b16_8 t34 = GM_LD(gm_b16_8, ((((uint8_t *)t33) + 8) + 0));
            void * t35 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t34)));
            uint64_t t36 = (uintptr_t)GM_LD(uintptr_t, ((uint8_t *)&t34) + 8);
            uint64_t t37 = (t36 - 1);
            gm_tijr008e33o2m t38;
            if (t37 < t36) {
            gm_tha5675rv512q t39 = GM_LD(gm_tha5675rv512q, ((uint8_t *)t35 + t37 * 48));
            gm_tijr008e33o2m t40;
            GM_ST(uint8_t, (uint8_t *)&t40 + 49, 0);
            GM_ST(uint16_t, (uint8_t *)&t40 + 50, 0);
            GM_ST(uint32_t, (uint8_t *)&t40 + 52, 0);
            GM_ST(gm_tha5675rv512q, ((uint8_t *)&t40), t39);
            ((uint8_t *)&t40)[48] = 1;
                t38 = t40;
            } else {
            gm_tijr008e33o2m t41;
            memset(&t41, 0, sizeof t41);
                t38 = t41;
            }
            gm_tha5675rv512q t42;
            if (((((uint8_t *)&t38))[48] != 0)) {
                t42 = (*(gm_tha5675rv512q *)(((uint8_t *)&t38) + 0));
            } else {
                gm_t9805qtonbsag t43;
                memset(&t43, 0, sizeof t43);
                uint8_t t44 = 0;
                F.ret = t43;
                goto gm_fret;
            }
            F.l6 = t42;
        }
        {
            void * t45 = (void *)(((uint8_t *)&F.l6) + 8);
            uint32_t t46 = gm_l5;
            gm_t9805qtonbsag t47 = gm_f_bound_of_sn02ojm(t45, t46);
            uint8_t t48 = 0;
            F.ret = t47;
            goto gm_fret;
        }
        goto me3;
mn29:;
ma7:;
        uint8_t t50 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t50 != ((uint8_t)16ULL)) goto mn49;
        uint32_t t51 = GM_LD(uint32_t, (((((uint8_t *)t2) + 8) + 8) + 0));
        gm_l7 = t51;
        uint32_t t52 = GM_LD(uint32_t, (((((uint8_t *)t2) + 8) + 8) + 4));
        gm_l8 = t52;
        {
            void * t54 = gm_l0;
            gm_b16_8 t55 = GM_LD(gm_b16_8, ((((uint8_t *)t54) + 8) + 0));
            void * t56 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t55)));
            uint64_t t57 = (uintptr_t)GM_LD(uintptr_t, ((uint8_t *)&t55) + 8);
            uint64_t t58 = (t57 - 1);
            gm_tijr008e33o2m t59;
            if (t58 < t57) {
            gm_tha5675rv512q t60 = GM_LD(gm_tha5675rv512q, ((uint8_t *)t56 + t58 * 48));
            gm_tijr008e33o2m t61;
            GM_ST(uint8_t, (uint8_t *)&t61 + 49, 0);
            GM_ST(uint16_t, (uint8_t *)&t61 + 50, 0);
            GM_ST(uint32_t, (uint8_t *)&t61 + 52, 0);
            GM_ST(gm_tha5675rv512q, ((uint8_t *)&t61), t60);
            ((uint8_t *)&t61)[48] = 1;
                t59 = t61;
            } else {
            gm_tijr008e33o2m t62;
            memset(&t62, 0, sizeof t62);
                t59 = t62;
            }
            gm_tha5675rv512q t63;
            if (((((uint8_t *)&t59))[48] != 0)) {
                t63 = (*(gm_tha5675rv512q *)(((uint8_t *)&t59) + 0));
            } else {
                gm_t9805qtonbsag t64;
                memset(&t64, 0, sizeof t64);
                uint8_t t65 = 0;
                F.ret = t64;
                goto gm_fret;
            }
            F.l9 = t63;
        }
        {
            uint32_t t66 = GM_LD(uint32_t, (((uint8_t *)&F.l9) + 0));
            gm_l10 = t66;
        }
        {
            {
                gm_l12 = ((uint32_t)0ULL);
            }
            {
                uint32_t t68 = gm_l7;
                gm_l11 = t68;
            }
            {
                for (;;) {
                    uint32_t t70 = gm_l12;
                    uint32_t t71 = gm_l11;
                    uint8_t t72 = (t70 < t71);
                    if (!t72) goto lb69;
                    {
                        uint32_t t75 = gm_l12;
                        {
                            void * t79 = gm_l0;
                            void * t80 = GM_LD(void *, (((uint8_t *)t79) + 0));
                            uint32_t t81 = gm_l10;
                            gm_tdgfduqjheqdm t82;
                            gm_f_def_of_m512sbu((void *)&t82, t80, t81);
                            gm_t9805qtonbsag t83 = GM_LD(gm_t9805qtonbsag, (((uint8_t *)&t82) + 48));
                            uint32_t t84;
                            if (((((uint8_t *)&t83))[4] != 0)) {
                                uint32_t t85 = GM_LD(uint32_t, (((uint8_t *)&t83) + 0));
                                t84 = t85;
                            } else {
                                gm_t9805qtonbsag t86;
                                memset(&t86, 0, sizeof t86);
                                uint8_t t87 = 0;
                                F.ret = t86;
                                goto gm_fret;
                            }
                            gm_l10 = t84;
                        }
                        goto me76;
mn77:;
                        gm_trap("no match arm matched the scrutinee", gm_src4tam33qo20j7, 153, 13);
me76:;
                    }
lc69:;
                    {
                        uint32_t t89 = gm_l12;
                        uint32_t r90; bool o91 = __builtin_add_overflow(t89, ((uint32_t)1ULL), &r90);
                        uint32_t t92 = r90;
                        gm_l12 = t92;
                    }
                }
lb69:;
            }
        }
        {
            void * t93 = gm_l0;
            uint64_t t94 = GM_LD(uintptr_t, ((((uint8_t *)t93) + 8) + 0) + 8);
            gm_l13 = t94;
        }
        {
            for (;;) {
                uint64_t t96 = gm_l13;
                uint8_t t97 = (t96 > ((uint64_t)0ULL));
                if (!t97) goto lb95;
                {
                    uint64_t t100 = gm_l13;
                    uint64_t r101; bool o102 = __builtin_sub_overflow(t100, ((uint64_t)1ULL), &r101);
                    if (o102) gm_trap("integer overflow in `-`", gm_src4tam33qo20j7, 158, 17);
                    uint64_t t103 = r101;
                    gm_l13 = t103;
                }
                {
                    uint64_t t105 = gm_l13;
                    uint64_t t106 = (uintptr_t)t105;
                    void * t107 = gm_l0;
                    uint64_t t108 = GM_LD(uintptr_t, ((((uint8_t *)t107) + 8) + 0) + 8);
                    if (t106 >= (uintptr_t)(t108)) gm_trap_index(t106, t108, gm_src4tam33qo20j7, 159, 20);
                    uint32_t t109 = GM_LD(uint32_t, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t107) + 8) + 0)) + t106 * 48) + 0));
                    uint32_t t110 = gm_l10;
                    uint8_t t111 = ((t109 == t110));
                    if (!t111) goto ie104;
                    {
                        uint64_t t113 = gm_l13;
                        uint64_t t114 = (uintptr_t)t113;
                        void * t115 = gm_l0;
                        uint64_t t116 = GM_LD(uintptr_t, ((((uint8_t *)t115) + 8) + 0) + 8);
                        if (t114 >= (uintptr_t)(t116)) gm_trap_index(t114, t116, gm_src4tam33qo20j7, 159, 66);
                        void * t117 = (void *)(((uint8_t *)GM_LD(void *, ((((uint8_t *)t115) + 8) + 0)) + t114 * 48) + 8);
                        uint32_t t118 = gm_l8;
                        gm_t9805qtonbsag t119 = gm_f_bound_of_sn02ojm(t117, t118);
                        uint8_t t120 = 0;
                        F.ret = t119;
                        goto gm_fret;
                    }
                    goto ix104;
ie104:;
ix104:;
                }
lc95:;
            }
lb95:;
        }
        {
            gm_t9805qtonbsag t121;
            memset(&t121, 0, sizeof t121);
            uint8_t t122 = 0;
            F.ret = t121;
            goto gm_fret;
        }
        goto me3;
mn49:;
ma8:;
        gm_t9805qtonbsag t124;
        memset(&t124, 0, sizeof t124);
        uint8_t t125 = 0;
        F.ret = t124;
        goto gm_fret;
        goto me3;
mn123:;
ma9:;
        gm_trap("no match arm matched the scrutinee", gm_src4tam33qo20j7, 143, 5);
me3:;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
