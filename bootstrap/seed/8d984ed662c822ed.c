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
typedef struct gm_cs3cuou245tr5f { _Bool f0; _Bool f1; _Bool f2; } gm_cs3cuou245tr5f;
typedef struct gm_cseahvg8v7p0vs { uint32_t f0; uint32_t f1; uint32_t f2; } gm_cseahvg8v7p0vs;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { void * f0; void * f1; void * f2; uint8_t f3; uint8_t pe[7]; } gm_tao63pk3r18v5;
typedef struct { uint8_t f0; uint8_t p1[7]; void * f1; } gm_tgtub0v94d5u5;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { uint8_t f0; uint8_t f1; uint8_t f2; } gm_toh0ootodhjue;
_Static_assert(sizeof(gm_cs3cuou245tr5f) == 3 && _Alignof(gm_cs3cuou245tr5f) == 1 && offsetof(gm_cs3cuou245tr5f, f0) == 0 && offsetof(gm_cs3cuou245tr5f, f1) == 1 && offsetof(gm_cs3cuou245tr5f, f2) == 2, "layout of `Restr`");
_Static_assert(sizeof(gm_cseahvg8v7p0vs) == 12 && _Alignof(gm_cseahvg8v7p0vs) == 4 && offsetof(gm_cseahvg8v7p0vs, f0) == 0 && offsetof(gm_cseahvg8v7p0vs, f1) == 4 && offsetof(gm_cseahvg8v7p0vs, f2) == 8, "layout of `Span`");
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_t08olq97rscn3;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_tl874t2atnlh3 v1; } f1; } gm_t0u0j6voh3loe;
typedef struct { gm_b16_8 f0; gm_t9qkh0rsooc8d f1; uint8_t f2; uint8_t p3[6]; gm_b16_8 f3; void * f4; void * f5; gm_tl874t2atnlh3 f6; uint8_t pe[4]; } gm_t24301fas1b2a;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_t79rbdv4rafe5;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; void * f1; } gm_t8ghgl2jj0qha;
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
_Static_assert(sizeof(gm_tao63pk3r18v5) == 32 && _Alignof(gm_tao63pk3r18v5) == 8 && offsetof(gm_tao63pk3r18v5, f0) == 0 && offsetof(gm_tao63pk3r18v5, f1) == 8 && offsetof(gm_tao63pk3r18v5, f2) == 16 && offsetof(gm_tao63pk3r18v5, f3) == 24, "layout of `SliceExpr`");
typedef struct { gm_b16_8 f0; uint32_t f1; uint32_t f2; gm_b16_8 f3; } gm_tb40qn7hk0tl9;
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t f2; uint8_t p3[6]; void * f3; void * f4; gm_tl874t2atnlh3 f5; uint8_t pe[4]; } gm_tf2orqcrphmek;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
_Static_assert(sizeof(gm_tgtub0v94d5u5) == 16 && _Alignof(gm_tgtub0v94d5u5) == 8 && offsetof(gm_tgtub0v94d5u5, f0) == 0 && offsetof(gm_tgtub0v94d5u5, f1) == 8, "layout of `(is_const: bool, inner: ^Expr)`");
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { void * f0; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_tiacef8jroje2;
typedef struct { gm_b16_8 f0; gm_t9qkh0rsooc8d f1; uint8_t pe[7]; } gm_tibupt4dmcqai;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
typedef struct { void * f0; gm_b16_8 f1; uint8_t f2; uint8_t pe[7]; } gm_tlljg61a2ctgq;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
_Static_assert(sizeof(gm_toh0ootodhjue) == 3 && _Alignof(gm_toh0ootodhjue) == 1 && offsetof(gm_toh0ootodhjue, f0) == 0 && offsetof(gm_toh0ootodhjue, f1) == 1 && offsetof(gm_toh0ootodhjue, f2) == 2, "layout of `Restr`");
typedef struct { gm_b16_8 f0; void * f1; } gm_tpe1hv3fekn05;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v1; } f1; } gm_tpvsmearkkqf7;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; void * v1; } f1; } gm_tq7s98ihonkju;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_t9qkh0rsooc8d v3; } f1; } gm_ts6oqppjh0ba3;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v1; } f1; } gm_tt7ek1a8bopcl;
typedef struct { void * f0; uint32_t f1; gm_tl874t2atnlh3 f2; } gm_tupk62cr6lbdu;
_Static_assert(sizeof(gm_t08olq97rscn3) == 32 && _Alignof(gm_t08olq97rscn3) == 8 && offsetof(gm_t08olq97rscn3, f0) == 0 && offsetof(gm_t08olq97rscn3, f1) == 16, "layout of `File`");
_Static_assert(sizeof(gm_t0u0j6voh3loe) == 16 && _Alignof(gm_t0u0j6voh3loe) == 4 && offsetof(gm_t0u0j6voh3loe, f0) == 0 && offsetof(gm_t0u0j6voh3loe, f1) == 4, "layout of `Abort!Span`");
typedef struct { gm_t08olq97rscn3 f0; uint8_t f1; uint8_t pe[7]; } gm_t1s75i08qtptd;
_Static_assert(sizeof(gm_t24301fas1b2a) == 72 && _Alignof(gm_t24301fas1b2a) == 8 && offsetof(gm_t24301fas1b2a, f0) == 0 && offsetof(gm_t24301fas1b2a, f1) == 16 && offsetof(gm_t24301fas1b2a, f2) == 17 && offsetof(gm_t24301fas1b2a, f3) == 24 && offsetof(gm_t24301fas1b2a, f4) == 40 && offsetof(gm_t24301fas1b2a, f5) == 48 && offsetof(gm_t24301fas1b2a, f6) == 56, "layout of `FieldDecl`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; gm_t9qkh0rsooc8d v1; void * v2; void * v3; gm_tibupt4dmcqai v4; gm_b16_8 v5; gm_b16_8 v6; gm_t9qkh0rsooc8d v7; } f1; } gm_t3fcbuehh3bs2;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t p3[4]; gm_trqfvmjtt4sij f3; gm_trqfvmjtt4sij f4; gm_trqfvmjtt4sij f5; gm_th2lqbo77pvsc f6; } gm_t4gut2c9fj1kt;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t08olq97rscn3 v1; } f1; } gm_t4jn8fl3lmk5k;
_Static_assert(sizeof(gm_t79rbdv4rafe5) == 40 && _Alignof(gm_t79rbdv4rafe5) == 8 && offsetof(gm_t79rbdv4rafe5, f0) == 0 && offsetof(gm_t79rbdv4rafe5, f1) == 8 && offsetof(gm_t79rbdv4rafe5, f2) == 24, "layout of `Block`");
typedef struct { void * f0; gm_b16_8 f1; gm_th2lqbo77pvsc f2; gm_tl874t2atnlh3 f3; uint8_t pe[4]; } gm_t7bjbmukv26e7;
_Static_assert(sizeof(gm_t8ghgl2jj0qha) == 16 && _Alignof(gm_t8ghgl2jj0qha) == 8 && offsetof(gm_t8ghgl2jj0qha, f0) == 0 && offsetof(gm_t8ghgl2jj0qha, f1) == 8, "layout of `(op: UnOp, operand: ^Expr)`");
typedef struct { gm_t08olq97rscn3 f0; gm_b16_8 f1; void * f2; gm_tl874t2atnlh3 f3; uint8_t pe[4]; } gm_t99vmhfllumbh;
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
_Static_assert(sizeof(gm_tb40qn7hk0tl9) == 40 && _Alignof(gm_tb40qn7hk0tl9) == 8 && offsetof(gm_tb40qn7hk0tl9, f0) == 0 && offsetof(gm_tb40qn7hk0tl9, f1) == 16 && offsetof(gm_tb40qn7hk0tl9, f2) == 20 && offsetof(gm_tb40qn7hk0tl9, f3) == 24, "layout of `SourceLocation`");
typedef struct { gm_t08olq97rscn3 f0; gm_t9qkh0rsooc8d f1; uint8_t p2[7]; gm_b16_8 f2; void * f3; gm_tl874t2atnlh3 f4; uint8_t pe[4]; } gm_tc0sbs1tp6d7f;
typedef struct { gm_t08olq97rscn3 f0; gm_th2lqbo77pvsc f1; } gm_tet9guhev5r4u;
_Static_assert(sizeof(gm_tf2orqcrphmek) == 56 && _Alignof(gm_tf2orqcrphmek) == 8 && offsetof(gm_tf2orqcrphmek, f0) == 0 && offsetof(gm_tf2orqcrphmek, f1) == 16 && offsetof(gm_tf2orqcrphmek, f2) == 17 && offsetof(gm_tf2orqcrphmek, f3) == 24 && offsetof(gm_tf2orqcrphmek, f4) == 32 && offsetof(gm_tf2orqcrphmek, f5) == 40, "layout of `Param`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
typedef struct { void * f0; gm_th2lqbo77pvsc f1; gm_b16_8 f2; gm_b16_8 f3; gm_tl874t2atnlh3 f4; uint8_t pe[4]; } gm_tg8732ngbnf0p;
typedef struct { gm_th2lqbo77pvsc f0; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_th0lu7is1il78;
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_th2lqbo77pvsc v1; } f1; } gm_thk954m1ol1ig;
_Static_assert(sizeof(gm_tiacef8jroje2) == 40 && _Alignof(gm_tiacef8jroje2) == 8 && offsetof(gm_tiacef8jroje2, f0) == 0 && offsetof(gm_tiacef8jroje2, f1) == 8 && offsetof(gm_tiacef8jroje2, f2) == 24, "layout of `MatchExpr`");
_Static_assert(sizeof(gm_tibupt4dmcqai) == 24 && _Alignof(gm_tibupt4dmcqai) == 8 && offsetof(gm_tibupt4dmcqai, f0) == 0 && offsetof(gm_tibupt4dmcqai, f1) == 16, "layout of `StrLit`");
typedef struct { void * f0; gm_t08olq97rscn3 f1; } gm_tioie2ju3q4ul;
typedef struct { gm_th2lqbo77pvsc f0; gm_b16_8 f1; gm_b16_8 f2; gm_tl874t2atnlh3 f3; uint8_t pe[4]; } gm_tj7j8jssama8n;
typedef struct { gm_tibupt4dmcqai f0; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_tjbbfnqt6296n;
typedef struct { gm_t08olq97rscn3 f0; gm_th2lqbo77pvsc f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_tkacitepg5fk0;
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t p2[7]; void * f2; gm_th2lqbo77pvsc f3; gm_tl874t2atnlh3 f4; uint8_t pe[4]; } gm_tlivgedjs5ps3;
_Static_assert(sizeof(gm_tlljg61a2ctgq) == 32 && _Alignof(gm_tlljg61a2ctgq) == 8 && offsetof(gm_tlljg61a2ctgq, f0) == 0 && offsetof(gm_tlljg61a2ctgq, f1) == 8 && offsetof(gm_tlljg61a2ctgq, f2) == 24, "layout of `(callee: ^Expr, args: []Arg, trailing_comma: bool)`");
typedef struct { gm_t08olq97rscn3 f0; void * f1; void * f2; gm_tl874t2atnlh3 f3; uint8_t pe[4]; } gm_tnljbma3s3h16;
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
_Static_assert(sizeof(gm_tpe1hv3fekn05) == 24 && _Alignof(gm_tpe1hv3fekn05) == 8 && offsetof(gm_tpe1hv3fekn05, f0) == 0 && offsetof(gm_tpe1hv3fekn05, f1) == 16, "layout of `(attrs: []Attribute, inner: ^Expr)`");
_Static_assert(sizeof(gm_tpvsmearkkqf7) == 2 && _Alignof(gm_tpvsmearkkqf7) == 1 && offsetof(gm_tpvsmearkkqf7, f0) == 0 && offsetof(gm_tpvsmearkkqf7, f1) == 1, "layout of `Abort!Visibility`");
_Static_assert(sizeof(gm_tq7s98ihonkju) == 16 && _Alignof(gm_tq7s98ihonkju) == 8 && offsetof(gm_tq7s98ihonkju, f0) == 0 && offsetof(gm_tq7s98ihonkju, f1) == 8, "layout of `AllocError!^HandlerDecl`");
typedef struct { gm_ts6oqppjh0ba3 f0; uint8_t p1[6]; void * f1; void * f2; } gm_trl9d7m1k5lbd;
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
typedef struct { gm_t08olq97rscn3 f0; void * f1; } gm_ts686ckv8ehm2;
_Static_assert(sizeof(gm_ts6oqppjh0ba3) == 2 && _Alignof(gm_ts6oqppjh0ba3) == 1 && offsetof(gm_ts6oqppjh0ba3, f0) == 0 && offsetof(gm_ts6oqppjh0ba3, f1) == 1, "layout of `BinOp`");
_Static_assert(sizeof(gm_tt7ek1a8bopcl) == 24 && _Alignof(gm_tt7ek1a8bopcl) == 8 && offsetof(gm_tt7ek1a8bopcl, f0) == 0 && offsetof(gm_tt7ek1a8bopcl, f1) == 8, "layout of `Abort![]Attribute`");
_Static_assert(sizeof(gm_tupk62cr6lbdu) == 24 && _Alignof(gm_tupk62cr6lbdu) == 8 && offsetof(gm_tupk62cr6lbdu, f0) == 0 && offsetof(gm_tupk62cr6lbdu, f1) == 8 && offsetof(gm_tupk62cr6lbdu, f2) == 12, "layout of `(base: ^Expr, index: u32, index_span: Span)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tnljbma3s3h16 v0; gm_t7bjbmukv26e7 v1; } f1; } gm_t03a3imodqi32;
_Static_assert(sizeof(gm_t1s75i08qtptd) == 40 && _Alignof(gm_t1s75i08qtptd) == 8 && offsetof(gm_t1s75i08qtptd, f0) == 0 && offsetof(gm_t1s75i08qtptd, f1) == 32, "layout of `?Ident`");
typedef struct { gm_t3fcbuehh3bs2 f0; gm_t3fcbuehh3bs2 f1; } gm_t23tohc57ebhi;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t99vmhfllumbh v1; } f1; } gm_t2av9t33q6vt2;
_Static_assert(sizeof(gm_t3fcbuehh3bs2) == 32 && _Alignof(gm_t3fcbuehh3bs2) == 8 && offsetof(gm_t3fcbuehh3bs2, f0) == 0 && offsetof(gm_t3fcbuehh3bs2, f1) == 8, "layout of `TokenKind`");
_Static_assert(sizeof(gm_t4gut2c9fj1kt) == 184 && _Alignof(gm_t4gut2c9fj1kt) == 8 && offsetof(gm_t4gut2c9fj1kt, f0) == 0 && offsetof(gm_t4gut2c9fj1kt, f1) == 8 && offsetof(gm_t4gut2c9fj1kt, f2) == 24 && offsetof(gm_t4gut2c9fj1kt, f3) == 40 && offsetof(gm_t4gut2c9fj1kt, f4) == 80 && offsetof(gm_t4gut2c9fj1kt, f5) == 120 && offsetof(gm_t4gut2c9fj1kt, f6) == 160, "layout of `Diagnostic`");
_Static_assert(sizeof(gm_t4jn8fl3lmk5k) == 40 && _Alignof(gm_t4jn8fl3lmk5k) == 8 && offsetof(gm_t4jn8fl3lmk5k, f0) == 0 && offsetof(gm_t4jn8fl3lmk5k, f1) == 8, "layout of `Abort!Ident`");
typedef struct { gm_t08olq97rscn3 f0; gm_tlivgedjs5ps3 f1; } gm_t70h2c775g4n7;
_Static_assert(sizeof(gm_t7bjbmukv26e7) == 64 && _Alignof(gm_t7bjbmukv26e7) == 8 && offsetof(gm_t7bjbmukv26e7, f0) == 0 && offsetof(gm_t7bjbmukv26e7, f1) == 8 && offsetof(gm_t7bjbmukv26e7, f2) == 24 && offsetof(gm_t7bjbmukv26e7, f3) == 48, "layout of `WhenItems`");
_Static_assert(sizeof(gm_t99vmhfllumbh) == 72 && _Alignof(gm_t99vmhfllumbh) == 8 && offsetof(gm_t99vmhfllumbh, f0) == 0 && offsetof(gm_t99vmhfllumbh, f1) == 32 && offsetof(gm_t99vmhfllumbh, f2) == 48 && offsetof(gm_t99vmhfllumbh, f3) == 56, "layout of `Clause`");
_Static_assert(sizeof(gm_tc0sbs1tp6d7f) == 80 && _Alignof(gm_tc0sbs1tp6d7f) == 8 && offsetof(gm_tc0sbs1tp6d7f, f0) == 0 && offsetof(gm_tc0sbs1tp6d7f, f1) == 32 && offsetof(gm_tc0sbs1tp6d7f, f2) == 40 && offsetof(gm_tc0sbs1tp6d7f, f3) == 56 && offsetof(gm_tc0sbs1tp6d7f, f4) == 64, "layout of `OpDecl`");
typedef struct { gm_t1s75i08qtptd f0; void * f1; } gm_tchr52g0ema53;
typedef struct { gm_tkacitepg5fk0 f0; uint8_t f1; uint8_t pe[7]; } gm_tcus0chk5r9uq;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tlivgedjs5ps3 v1; } f1; } gm_tdmdfjrg7d59n;
_Static_assert(sizeof(gm_tet9guhev5r4u) == 56 && _Alignof(gm_tet9guhev5r4u) == 8 && offsetof(gm_tet9guhev5r4u, f0) == 0 && offsetof(gm_tet9guhev5r4u, f1) == 32, "layout of `IntrinsicExpr`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t24301fas1b2a v0; gm_t7bjbmukv26e7 v1; } f1; } gm_tfc3oljlofib6;
_Static_assert(sizeof(gm_tg8732ngbnf0p) == 80 && _Alignof(gm_tg8732ngbnf0p) == 8 && offsetof(gm_tg8732ngbnf0p, f0) == 0 && offsetof(gm_tg8732ngbnf0p, f1) == 8 && offsetof(gm_tg8732ngbnf0p, f2) == 32 && offsetof(gm_tg8732ngbnf0p, f3) == 48 && offsetof(gm_tg8732ngbnf0p, f4) == 64, "layout of `UnionLit`");
_Static_assert(sizeof(gm_th0lu7is1il78) == 56 && _Alignof(gm_th0lu7is1il78) == 8 && offsetof(gm_th0lu7is1il78, f0) == 0 && offsetof(gm_th0lu7is1il78, f1) == 24 && offsetof(gm_th0lu7is1il78, f2) == 40, "layout of `EffectLit`");
_Static_assert(sizeof(gm_thk954m1ol1ig) == 32 && _Alignof(gm_thk954m1ol1ig) == 8 && offsetof(gm_thk954m1ol1ig, f0) == 0 && offsetof(gm_thk954m1ol1ig, f1) == 8, "layout of `Abort!CallArgs`");
typedef struct { void * f0; gm_t1s75i08qtptd f1; void * f2; } gm_tioi3jh9v22g5;
_Static_assert(sizeof(gm_tioie2ju3q4ul) == 40 && _Alignof(gm_tioie2ju3q4ul) == 8 && offsetof(gm_tioie2ju3q4ul, f0) == 0 && offsetof(gm_tioie2ju3q4ul, f1) == 8, "layout of `(base: ^Expr, name: Ident)`");
_Static_assert(sizeof(gm_tj7j8jssama8n) == 72 && _Alignof(gm_tj7j8jssama8n) == 8 && offsetof(gm_tj7j8jssama8n, f0) == 0 && offsetof(gm_tj7j8jssama8n, f1) == 24 && offsetof(gm_tj7j8jssama8n, f2) == 40 && offsetof(gm_tj7j8jssama8n, f3) == 56, "layout of `StructLit`");
_Static_assert(sizeof(gm_tjbbfnqt6296n) == 56 && _Alignof(gm_tjbbfnqt6296n) == 8 && offsetof(gm_tjbbfnqt6296n, f0) == 0 && offsetof(gm_tjbbfnqt6296n, f1) == 24 && offsetof(gm_tjbbfnqt6296n, f2) == 40, "layout of `ForeignBlock`");
_Static_assert(sizeof(gm_tkacitepg5fk0) == 72 && _Alignof(gm_tkacitepg5fk0) == 8 && offsetof(gm_tkacitepg5fk0, f0) == 0 && offsetof(gm_tkacitepg5fk0, f1) == 32 && offsetof(gm_tkacitepg5fk0, f2) == 56, "layout of `Attribute`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tc0sbs1tp6d7f v1; } f1; } gm_tkkip668684tl;
_Static_assert(sizeof(gm_tlivgedjs5ps3) == 72 && _Alignof(gm_tlivgedjs5ps3) == 8 && offsetof(gm_tlivgedjs5ps3, f0) == 0 && offsetof(gm_tlivgedjs5ps3, f1) == 16 && offsetof(gm_tlivgedjs5ps3, f2) == 24 && offsetof(gm_tlivgedjs5ps3, f3) == 32 && offsetof(gm_tlivgedjs5ps3, f4) == 56, "layout of `FnSig`");
typedef struct { gm_t3fcbuehh3bs2 f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_tm4pc0emhrv1g;
_Static_assert(sizeof(gm_tnljbma3s3h16) == 64 && _Alignof(gm_tnljbma3s3h16) == 8 && offsetof(gm_tnljbma3s3h16, f0) == 0 && offsetof(gm_tnljbma3s3h16, f1) == 32 && offsetof(gm_tnljbma3s3h16, f2) == 40 && offsetof(gm_tnljbma3s3h16, f3) == 48, "layout of `Variant`");
_Static_assert(sizeof(gm_trl9d7m1k5lbd) == 24 && _Alignof(gm_trl9d7m1k5lbd) == 8 && offsetof(gm_trl9d7m1k5lbd, f0) == 0 && offsetof(gm_trl9d7m1k5lbd, f1) == 8 && offsetof(gm_trl9d7m1k5lbd, f2) == 16, "layout of `(op: BinOp, lhs: ^Expr, rhs: ^Expr)`");
_Static_assert(sizeof(gm_ts686ckv8ehm2) == 40 && _Alignof(gm_ts686ckv8ehm2) == 8 && offsetof(gm_ts686ckv8ehm2, f0) == 0 && offsetof(gm_ts686ckv8ehm2, f1) == 32, "layout of `(name: Ident, ty: ^Expr)`");
_Static_assert(sizeof(gm_t03a3imodqi32) == 72 && _Alignof(gm_t03a3imodqi32) == 8 && offsetof(gm_t03a3imodqi32, f0) == 0 && offsetof(gm_t03a3imodqi32, f1) == 8, "layout of `UnionMember`");
_Static_assert(sizeof(gm_t23tohc57ebhi) == 64 && _Alignof(gm_t23tohc57ebhi) == 8 && offsetof(gm_t23tohc57ebhi, f0) == 0 && offsetof(gm_t23tohc57ebhi, f1) == 32, "layout of `(TokenKind, TokenKind)`");
_Static_assert(sizeof(gm_t2av9t33q6vt2) == 80 && _Alignof(gm_t2av9t33q6vt2) == 8 && offsetof(gm_t2av9t33q6vt2, f0) == 0 && offsetof(gm_t2av9t33q6vt2, f1) == 8, "layout of `Abort!Clause`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { void * v0; void * v1; gm_tibupt4dmcqai v2; uint8_t v3; gm_t08olq97rscn3 v4; gm_t08olq97rscn3 v5; void * v6; gm_b16_8 v7; void * v8; gm_t8ghgl2jj0qha v9; gm_trl9d7m1k5lbd v10; gm_tioi3jh9v22g5 v11; void * v12; void * v13; gm_t3mf58gv9kf3n v14; void * v15; gm_tlljg61a2ctgq v16; gm_t3mf58gv9kf3n v17; gm_tao63pk3r18v5 v18; gm_tioie2ju3q4ul v19; gm_tupk62cr6lbdu v20; void * v21; gm_tgtub0v94d5u5 v22; gm_tgtub0v94d5u5 v23; gm_tgtub0v94d5u5 v24; gm_t3mf58gv9kf3n v25; gm_t3mf58gv9kf3n v26; void * v27; void * v28; void * v29; void * v30; void * v31; gm_tpe1hv3fekn05 v32; void * v33; void * v34; gm_tibupt4dmcqai v35; void * v36; void * v37; void * v38; void * v39; void * v40; gm_t79rbdv4rafe5 v41; void * v42; gm_tchr52g0ema53 v43; gm_t1s75i08qtptd v44; gm_t3mf58gv9kf3n v45; gm_tet9guhev5r4u v46; } f1; } gm_t2gtbh39ptekd;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tfc3oljlofib6 v1; } f1; } gm_t6gdobsbntbpm;
_Static_assert(sizeof(gm_t70h2c775g4n7) == 104 && _Alignof(gm_t70h2c775g4n7) == 8 && offsetof(gm_t70h2c775g4n7, f0) == 0 && offsetof(gm_t70h2c775g4n7, f1) == 32, "layout of `(name: Ident, sig: FnSig)`");
_Static_assert(sizeof(gm_tchr52g0ema53) == 48 && _Alignof(gm_tchr52g0ema53) == 8 && offsetof(gm_tchr52g0ema53, f0) == 0 && offsetof(gm_tchr52g0ema53, f1) == 40, "layout of `BreakExpr`");
_Static_assert(sizeof(gm_tcus0chk5r9uq) == 80 && _Alignof(gm_tcus0chk5r9uq) == 8 && offsetof(gm_tcus0chk5r9uq, f0) == 0 && offsetof(gm_tcus0chk5r9uq, f1) == 72, "layout of `?Attribute`");
_Static_assert(sizeof(gm_tdmdfjrg7d59n) == 80 && _Alignof(gm_tdmdfjrg7d59n) == 8 && offsetof(gm_tdmdfjrg7d59n, f0) == 0 && offsetof(gm_tdmdfjrg7d59n, f1) == 8, "layout of `Abort!FnSig`");
_Static_assert(sizeof(gm_tfc3oljlofib6) == 80 && _Alignof(gm_tfc3oljlofib6) == 8 && offsetof(gm_tfc3oljlofib6, f0) == 0 && offsetof(gm_tfc3oljlofib6, f1) == 8, "layout of `StructMember`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t70h2c775g4n7 v0; gm_ts686ckv8ehm2 v1; } f1; } gm_tg69e9d4krfe7;
_Static_assert(sizeof(gm_tioi3jh9v22g5) == 56 && _Alignof(gm_tioi3jh9v22g5) == 8 && offsetof(gm_tioi3jh9v22g5, f0) == 0 && offsetof(gm_tioi3jh9v22g5, f1) == 8 && offsetof(gm_tioi3jh9v22g5, f2) == 48, "layout of `ElseExpr`");
_Static_assert(sizeof(gm_tkkip668684tl) == 88 && _Alignof(gm_tkkip668684tl) == 8 && offsetof(gm_tkkip668684tl, f0) == 0 && offsetof(gm_tkkip668684tl, f1) == 8, "layout of `Abort!OpDecl`");
_Static_assert(sizeof(gm_tm4pc0emhrv1g) == 48 && _Alignof(gm_tm4pc0emhrv1g) == 8 && offsetof(gm_tm4pc0emhrv1g, f0) == 0 && offsetof(gm_tm4pc0emhrv1g, f1) == 32, "layout of `Token`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t03a3imodqi32 v1; } f1; } gm_tup3hijlfo1vq;
_Static_assert(sizeof(gm_t2gtbh39ptekd) == 64 && _Alignof(gm_t2gtbh39ptekd) == 8 && offsetof(gm_t2gtbh39ptekd, f0) == 0 && offsetof(gm_t2gtbh39ptekd, f1) == 8, "layout of `ExprKind`");
_Static_assert(sizeof(gm_t6gdobsbntbpm) == 88 && _Alignof(gm_t6gdobsbntbpm) == 8 && offsetof(gm_t6gdobsbntbpm, f0) == 0 && offsetof(gm_t6gdobsbntbpm, f1) == 8, "layout of `Abort!StructMember`");
typedef struct { gm_b16_8 f0; gm_tg69e9d4krfe7 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_t7rh4gj19r55v;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_t2gtbh39ptekd f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_tc3oht4ksth9d;
_Static_assert(sizeof(gm_tg69e9d4krfe7) == 112 && _Alignof(gm_tg69e9d4krfe7) == 8 && offsetof(gm_tg69e9d4krfe7, f0) == 0 && offsetof(gm_tg69e9d4krfe7, f1) == 8, "layout of `ForeignMemberKind`");
_Static_assert(sizeof(gm_tup3hijlfo1vq) == 80 && _Alignof(gm_tup3hijlfo1vq) == 8 && offsetof(gm_tup3hijlfo1vq, f0) == 0 && offsetof(gm_tup3hijlfo1vq, f1) == 8, "layout of `Abort!UnionMember`");
_Static_assert(sizeof(gm_t7rh4gj19r55v) == 144 && _Alignof(gm_t7rh4gj19r55v) == 8 && offsetof(gm_t7rh4gj19r55v, f0) == 0 && offsetof(gm_t7rh4gj19r55v, f1) == 16 && offsetof(gm_t7rh4gj19r55v, f2) == 128, "layout of `ForeignMember`");
_Static_assert(sizeof(gm_tc3oht4ksth9d) == 88 && _Alignof(gm_tc3oht4ksth9d) == 8 && offsetof(gm_tc3oht4ksth9d, f0) == 0 && offsetof(gm_tc3oht4ksth9d, f1) == 8 && offsetof(gm_tc3oht4ksth9d, f2) == 72, "layout of `Expr`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tc3oht4ksth9d v1; } f1; } gm_to1cssm8dfiv5;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t7rh4gj19r55v v1; } f1; } gm_tspns3qg8u35u;
_Static_assert(sizeof(gm_to1cssm8dfiv5) == 96 && _Alignof(gm_to1cssm8dfiv5) == 8 && offsetof(gm_to1cssm8dfiv5, f0) == 0 && offsetof(gm_to1cssm8dfiv5, f1) == 8, "layout of `Abort!Expr`");
_Static_assert(sizeof(gm_tspns3qg8u35u) == 152 && _Alignof(gm_tspns3qg8u35u) == 8 && offsetof(gm_tspns3qg8u35u, f0) == 0 && offsetof(gm_tspns3qg8u35u, f1) == 8, "layout of `Abort!ForeignMember`");
struct gm_fr_at_54906rm {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_at_eof_ebcpkv0 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_at_kw_p0r3sr9 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_at_newline_94n732e {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_box_j4urmed {
    gm_frame h;
    gm_tc3oht4ksth9d l0; /* e */
    void * ret;
};
struct gm_fr_eat_kw_tvv0v3e {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_eat_qmock9g {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_empty_slice_65q32rn {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_atlcs4j {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_d53jqcq {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_flce2bb {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ppj8ikt {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_u4e74ha {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ure7n5c {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_fn_6ihve2o {
    gm_frame h;
    gm_t9qkh0rsooc8d ret;
};
struct gm_fr_fn_gp5mf74 {
    gm_frame h;
    gm_t9qkh0rsooc8d ret;
};
struct gm_fr_fn_qh4flda {
    gm_frame h;
    gm_t9qkh0rsooc8d ret;
};
struct gm_fr_fn_rupmstg {
    gm_frame h;
    gm_t9qkh0rsooc8d ret;
};
struct gm_fr_fn_uom6vil {
    gm_frame h;
    gm_t9qkh0rsooc8d ret;
};
struct gm_fr_kind_k239kpg {
    gm_frame h;
    gm_t3fcbuehh3bs2 ret;
};
struct gm_fr_list_18sm81j {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_3odrho7 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_89j9i66 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_g2q4cds {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_mho6jfm {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_ps4qcsk {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_s24cnj6 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_lo_fp82gt4 {
    gm_frame h;
    uint32_t ret;
};
struct gm_fr_members_oq7gs8i {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_mk_expr_53qj7nd {
    gm_frame h;
    gm_t2gtbh39ptekd l1; /* k */
    gm_tc3oht4ksth9d ret;
};
struct gm_fr_next_id_a7upaf4 {
    gm_frame h;
    uint32_t ret;
};
struct gm_fr_nth_c0r2nvs {
    gm_frame h;
    gm_t3fcbuehh3bs2 ret;
};
struct gm_fr_nth_is_3fgqmcb {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_paren_is_param_list_0bbahvv {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_parse_clause_cqi8t7k {
    gm_frame h;
    gm_trqfvmjtt4sij l3; /* params */
    gm_tc3oht4ksth9d l4; /* body */
    gm_t2av9t33q6vt2 ret;
};
struct gm_fr_parse_clauses_2n3uc9a {
    gm_frame h;
    void * l0; /* p */
    gm_trqfvmjtt4sij l1; /* out */
    gm_b16_8 ret;
};
struct gm_fr_parse_decl_params_ftnplpo {
    gm_frame h;
    gm_tf2orqcrphmek l4; /* param */
    gm_tt7ek1a8bopcl ret;
};
struct gm_fr_parse_effect_lit_cqei081 {
    gm_frame h;
    void * l0; /* p */
    gm_trqfvmjtt4sij l3; /* ops */
    gm_to1cssm8dfiv5 ret;
};
struct gm_fr_parse_effect_ref_8cmc20n {
    gm_frame h;
    gm_to1cssm8dfiv5 ret;
};
struct gm_fr_parse_foreign_l4091na {
    gm_frame h;
    void * l0; /* p */
    gm_trqfvmjtt4sij l3; /* ms */
    gm_to1cssm8dfiv5 ret;
};
struct gm_fr_parse_foreign_member_rjqa59g {
    gm_frame h;
    gm_trqfvmjtt4sij l3; /* attrs */
    gm_tkacitepg5fk0 l7; /* a */
    gm_tlivgedjs5ps3 l11; /* sig */
    gm_tspns3qg8u35u ret;
};
struct gm_fr_parse_handler_lit_1nkojsu {
    gm_frame h;
    gm_to1cssm8dfiv5 ret;
};
struct gm_fr_parse_op_decl_681ptb5 {
    gm_frame h;
    gm_tkkip668684tl ret;
};
struct gm_fr_parse_struct_lit_86pp62c {
    gm_frame h;
    gm_to1cssm8dfiv5 ret;
};
struct gm_fr_parse_struct_member_7aem2to {
    gm_frame h;
    gm_tkacitepg5fk0 l3; /* a */
    gm_trqfvmjtt4sij l6; /* names */
    gm_t6gdobsbntbpm ret;
};
struct gm_fr_parse_struct_members_rj26glk {
    gm_frame h;
    void * l0; /* p */
    gm_trqfvmjtt4sij l1; /* out */
    gm_b16_8 ret;
};
struct gm_fr_parse_struct_when_4imfqc2 {
    gm_frame h;
    gm_t6gdobsbntbpm ret;
};
struct gm_fr_parse_union_lit_39ap2o5 {
    gm_frame h;
    gm_to1cssm8dfiv5 ret;
};
struct gm_fr_parse_union_member_dba1nar {
    gm_frame h;
    gm_tup3hijlfo1vq ret;
};
struct gm_fr_parse_union_members_g8ptfng {
    gm_frame h;
    void * l0; /* p */
    gm_trqfvmjtt4sij l1; /* out */
    gm_b16_8 ret;
};
struct gm_fr_report_djvi2hf {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_report_eardf07 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_set_restr_p85qqmv {
    gm_frame h;
    gm_toh0ootodhjue ret;
};
struct gm_fr_skip_newlines_0ap7sq2 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_span_bm4ll8d {
    gm_frame h;
    gm_tl874t2atnlh3 ret;
};
struct gm_fr_span_from_1sqap0o {
    gm_frame h;
    gm_tl874t2atnlh3 ret;
};
struct gm_fr_tok_ai6sc12 {
    gm_frame h;
    void * ret;
};
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b);
inline bool gm_eq2ceic3f8rp(const uint8_t *a, const uint8_t *b);
inline bool gm_eqbdg26q4aml(const uint8_t *a, const uint8_t *b);
inline bool gm_eqbihgc69jv3(const uint8_t *a, const uint8_t *b);
inline bool gm_eqk7n1cdvnss(const uint8_t *a, const uint8_t *b);
inline bool gm_eqpa2btif7eh(const uint8_t *a, const uint8_t *b);
inline bool gm_eqrv7v3j3o54(const uint8_t *a, const uint8_t *b);
inline bool gm_eqt7ftn1841u(const uint8_t *a, const uint8_t *b);
inline bool gm_eqv3tj1pu43g(const uint8_t *a, const uint8_t *b);
inline __attribute__((aligned(4))) uint8_t gm_f_at_54906rm(void * a0, gm_t9qkh0rsooc8d a1);
inline __attribute__((aligned(4))) uint8_t gm_f_at_eof_ebcpkv0(void * a0);
inline __attribute__((aligned(4))) uint8_t gm_f_at_kw_p0r3sr9(void * a0, gm_t9qkh0rsooc8d a1);
inline __attribute__((aligned(4))) uint8_t gm_f_at_newline_94n732e(void * a0);
inline __attribute__((aligned(4))) void * gm_f_box_j4urmed(void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_bump_q2i40re(void *gm_out, void * a0);
__attribute__((aligned(4))) gm_t9qkh0rsooc8d gm_f_deny_in_head_06d9o22(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) uint8_t gm_f_eat_kw_tvv0v3e(void * a0, gm_t9qkh0rsooc8d a1);
inline __attribute__((aligned(4))) uint8_t gm_f_eat_qmock9g(void * a0, gm_t9qkh0rsooc8d a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_65q32rn(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_atlcs4j(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_d53jqcq(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_flce2bb(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ppj8ikt(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_u4e74ha(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ure7n5c(void);
__attribute__((aligned(4))) void gm_f_error_s6hn871(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_t0u0j6voh3loe gm_f_expect_ijgfrs1(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t9qkh0rsooc8d a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_expect_terminator_f7kp9sk(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_expected_5famfb9(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_t9qkh0rsooc8d gm_f_fn_6ihve2o(void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_t9qkh0rsooc8d gm_f_fn_gp5mf74(void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_t9qkh0rsooc8d gm_f_fn_qh4flda(void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_t9qkh0rsooc8d gm_f_fn_rupmstg(void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_t9qkh0rsooc8d gm_f_fn_uom6vil(void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_format_iqcmufv(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_kind_k239kpg(void *gm_out, void * a0);
inline __attribute__((aligned(4))) void gm_f_list_18sm81j(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_3odrho7(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_89j9i66(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_g2q4cds(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_mho6jfm(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_ps4qcsk(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_s24cnj6(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) uint32_t gm_f_lo_fp82gt4(void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_members_oq7gs8i(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_closure a2);
inline __attribute__((aligned(4))) void gm_f_mk_expr_53qj7nd(void *gm_out, void * a0, const void * a1, gm_cseahvg8v7p0vs a2);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_asf83v8(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_ejgnaq4(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_hq21sop(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_jpqes5m(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_jrcm1co(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_ve8bb01(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
inline __attribute__((aligned(4))) uint32_t gm_f_next_id_a7upaf4(void * a0);
inline __attribute__((aligned(4))) void gm_f_nth_c0r2nvs(void *gm_out, void * a0, uint64_t a1);
inline __attribute__((aligned(4))) uint8_t gm_f_nth_is_3fgqmcb(void * a0, uint64_t a1, gm_t9qkh0rsooc8d a2);
__attribute__((aligned(4))) uint8_t gm_f_nth_is_name_oqhoobd(void * a0, uint64_t a1);
__attribute__((aligned(4))) uint8_t gm_f_paren_is_param_list_0bbahvv(void * a0);
__attribute__((aligned(4))) void gm_f_parse_attrs_imvfdlq(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_block_expr_0j6b78q(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_clause_cqi8t7k(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_parse_clauses_2n3uc9a(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_decl_ident_31rb1id(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_parse_decl_params_ftnplpo(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_effect_lit_cqei081(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_effect_ref_8cmc20n(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_expr_bkb2n6e(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_fn_type_sig_jrv5v82(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1);
__attribute__((aligned(4))) void gm_f_parse_foreign_l4091na(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_foreign_member_rjqa59g(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_handler_lit_1nkojsu(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_head_expr_6sofdhn(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_name_figjvbe(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_parse_op_decl_681ptb5(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_params_3g43t5f(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_postfix_ao4kstr(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1);
__attribute__((aligned(4))) void gm_f_parse_struct_lit_86pp62c(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_struct_member_7aem2to(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_parse_struct_members_rj26glk(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_struct_when_4imfqc2(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_type_2m93dtq(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_type_in_bvi7uav(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_toh0ootodhjue a1);
__attribute__((aligned(4))) void gm_f_parse_union_lit_39ap2o5(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_union_member_dba1nar(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_parse_union_members_g8ptfng(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_tpvsmearkkqf7 gm_f_parse_visibility_j571nt0(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_where_opt_a5p2ja6(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_8isgrsr(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_9cfi95o(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_a7r9dj6(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_imcrlr1(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_mnuql9a(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_np5t726(void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_push_o4ckdr1(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_s5rtia7(void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_recover_to_stmt_end_freq9f3(void * a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_report_djvi2hf(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_report_eardf07(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_cs3cuou245tr5f gm_f_set_restr_p85qqmv(void * a0, gm_cs3cuou245tr5f a1);
inline __attribute__((aligned(4))) void gm_f_skip_newlines_0ap7sq2(void * a0);
inline __attribute__((aligned(4))) gm_cseahvg8v7p0vs gm_f_span_bm4ll8d(void * a0);
inline __attribute__((aligned(4))) gm_cseahvg8v7p0vs gm_f_span_from_1sqap0o(void * a0, uint32_t a1);
inline __attribute__((aligned(4))) void * gm_f_tok_ai6sc12(void * a0);
extern uint8_t gm_kbamu2ordri[40] __attribute__((aligned(8)));
extern uint8_t gm_kdpnk3k16op[40] __attribute__((aligned(8)));
extern uint8_t gm_keokac8bchm[40] __attribute__((aligned(8)));
extern uint8_t gm_kplh4t9vgh7[40] __attribute__((aligned(8)));
extern uint8_t gm_krr4pbvd2tj[40] __attribute__((aligned(8)));
extern uint8_t gm_kt1fbj7k4jp[40] __attribute__((aligned(8)));
extern const uint8_t gm_s01mi6tf86obv[13];
extern const uint8_t gm_s122r7j2imgmj[22];
extern const uint8_t gm_s244ts3klchej[25];
extern const uint8_t gm_s2jg45h85jjgj[37];
extern const uint8_t gm_s2pa39hqq5hrk[12];
extern const uint8_t gm_s3qnfqte7c8r2[35];
extern const uint8_t gm_s476hgsnn8633[47];
extern const uint8_t gm_s5om28rj8ddf9[28];
extern const uint8_t gm_s70nlbtnmugoc[10];
extern const uint8_t gm_s8l7qb8s3942u[35];
extern const uint8_t gm_s8qn6r2vquv2d[24];
extern const uint8_t gm_s9b8bk12k1acq[53];
extern const uint8_t gm_s9hi7i7cl3e2t[78];
extern const uint8_t gm_sa0amdcmvej1l[14];
extern const uint8_t gm_saab9e9ochkfq[56];
extern const uint8_t gm_sac45tip59t28[26];
extern const uint8_t gm_saf3o40admn10[25];
extern const uint8_t gm_sapag1m4a00vt[16];
extern const uint8_t gm_saqb7i299utsd[18];
extern const uint8_t gm_sauu2s1pbjqsh[11];
extern const uint8_t gm_sb3efvtufksie[37];
extern const uint8_t gm_sbsjrunf4id25[12];
extern const uint8_t gm_scmbb8a9enevr[16];
extern const uint8_t gm_scmob1k27904r[25];
extern const uint8_t gm_se2fbs5jfigcc[11];
extern const uint8_t gm_se7h6nse0tc33[26];
extern const uint8_t gm_se84lb2ddmuai[21];
extern const uint8_t gm_semavgcmso3pp[33];
extern const uint8_t gm_sfiqishktrbg2[27];
extern const uint8_t gm_sfuh3dki1la7r[32];
extern const uint8_t gm_shhtc33mlfd4u[31];
extern const uint8_t gm_skusvrbuvvp37[13];
extern const uint8_t gm_sl34v6fvasecs[31];
extern const uint8_t gm_slgf61fmeeof0[28];
extern const uint8_t gm_snud2jia3fe8d[18];
extern const uint8_t gm_sobecpce3c56n[35];
extern const uint8_t gm_sptnm8c6c52dr[7];
extern const uint8_t gm_sqsb0jek355o9[17];
extern const uint8_t gm_sr4qbrk6tlgaa[1];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_src8m8lg034q48k[];
extern const char gm_srcrgc9bkquf6nr[];
extern const char gm_srcuiab49hlgv7g[];
extern const uint8_t gm_sri0h4dm5d3ul[17];
extern const uint8_t gm_srk1d80eamfhd[15];
extern const uint8_t gm_ssgjh7i7oqvi4[24];
extern const uint8_t gm_stjafkt8gs4e7[13];
extern const uint8_t gm_sucoj11hr978o[14];
extern const uint8_t gm_svtkcim3vundp[18];
extern const gm_type gm_tyo86ks9qkvt;
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b) {
    uintptr_t n = GM_LD(uintptr_t, a + 8);
    if (n != GM_LD(uintptr_t, b + 8)) return false;
    const uint8_t *pa = GM_LD(void *, a), *pb = GM_LD(void *, b);
    return n == 0 || memcmp(pa, pb, n * 1) == 0;
}
inline bool gm_eq2ceic3f8rp(const uint8_t *a, const uint8_t *b) {
    uint8_t ta = GM_LD(uint8_t, a + 0), tb = GM_LD(uint8_t, b + 0);
    if (ta != tb) return false;
    return true;
}
inline bool gm_eqbdg26q4aml(const uint8_t *a, const uint8_t *b) {
    if (!gm_eqbihgc69jv3(a + 0, b + 0)) return false;
    if (!gm_eqk7n1cdvnss(a + 16, b + 16)) return false;
    return true;
}
inline bool gm_eqbihgc69jv3(const uint8_t *a, const uint8_t *b) {
    uintptr_t n = GM_LD(uintptr_t, a + 8);
    if (n != GM_LD(uintptr_t, b + 8)) return false;
    const uint8_t *pa = GM_LD(void *, a), *pb = GM_LD(void *, b);
    return n == 0 || memcmp(pa, pb, n * 1) == 0;
}
inline bool gm_eqk7n1cdvnss(const uint8_t *a, const uint8_t *b) {
    uint8_t ta = GM_LD(uint8_t, a + 0), tb = GM_LD(uint8_t, b + 0);
    if (ta != tb) return false;
    return true;
}
inline bool gm_eqpa2btif7eh(const uint8_t *a, const uint8_t *b) {
    return GM_LD(void *, a) == GM_LD(void *, b);
}
inline bool gm_eqrv7v3j3o54(const uint8_t *a, const uint8_t *b) {
    return GM_LD(void *, a) == GM_LD(void *, b);
}
inline bool gm_eqt7ftn1841u(const uint8_t *a, const uint8_t *b) {
    uint8_t ta = GM_LD(uint8_t, a + 0), tb = GM_LD(uint8_t, b + 0);
    if (ta != tb) return false;
    if (ta == (uint8_t)0) return gm_eq0iqiubfm7e(a + 8, b + 8);
    if (ta == (uint8_t)2) return gm_eq2ceic3f8rp(a + 8, b + 8);
    if (ta == (uint8_t)3) return gm_eqrv7v3j3o54(a + 8, b + 8);
    if (ta == (uint8_t)4) return gm_eqpa2btif7eh(a + 8, b + 8);
    if (ta == (uint8_t)5) return gm_eqbdg26q4aml(a + 8, b + 8);
    if (ta == (uint8_t)6) return gm_eq0iqiubfm7e(a + 8, b + 8);
    if (ta == (uint8_t)7) return gm_eq0iqiubfm7e(a + 8, b + 8);
    if (ta == (uint8_t)8) return gm_eqv3tj1pu43g(a + 8, b + 8);
    return true;
}
inline bool gm_eqv3tj1pu43g(const uint8_t *a, const uint8_t *b) {
    uint8_t ta = GM_LD(uint8_t, a + 0), tb = GM_LD(uint8_t, b + 0);
    if (ta != tb) return false;
    return true;
}
inline __attribute__((aligned(4))) uint8_t gm_f_at_54906rm(void * a0, gm_t9qkh0rsooc8d a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_at_54906rm F;
    void * gm_l0; /* p */
    gm_t9qkh0rsooc8d gm_l1; /* x */
    GM_STACK_CHECK_SHALLOW(&F, 86u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    gm_t3fcbuehh3bs2 t2;
    gm_f_kind_k239kpg((void *)&t2, t1);
    gm_t3fcbuehh3bs2 t3 = t2;
    gm_t9qkh0rsooc8d t4 = gm_l1;
    gm_t9qkh0rsooc8d t5 = t4;
    uint8_t t6 = (((GM_LD(uint8_t, ((uint8_t *)&t3) + 0) == ((uint8_t)8ULL)) && gm_eqv3tj1pu43g((((uint8_t *)&t3) + 8), ((uint8_t *)&t5))));
    F.ret = t6;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_at_eof_ebcpkv0(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_at_eof_ebcpkv0 F;
    void * gm_l0; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 83u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    gm_t3fcbuehh3bs2 t2;
    gm_f_kind_k239kpg((void *)&t2, t1);
    gm_t3fcbuehh3bs2 t3 = t2;
    uint8_t t4 = ((GM_LD(uint8_t, ((uint8_t *)&t3) + 0) == ((uint8_t)10ULL)));
    F.ret = t4;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_at_kw_p0r3sr9(void * a0, gm_t9qkh0rsooc8d a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_at_kw_p0r3sr9 F;
    void * gm_l0; /* p */
    gm_t9qkh0rsooc8d gm_l1; /* k */
    GM_STACK_CHECK_SHALLOW(&F, 86u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    gm_t3fcbuehh3bs2 t2;
    gm_f_kind_k239kpg((void *)&t2, t1);
    gm_t3fcbuehh3bs2 t3 = t2;
    gm_t9qkh0rsooc8d t4 = gm_l1;
    gm_t9qkh0rsooc8d t5 = t4;
    uint8_t t6 = (((GM_LD(uint8_t, ((uint8_t *)&t3) + 0) == ((uint8_t)2ULL)) && gm_eq2ceic3f8rp((((uint8_t *)&t3) + 8), ((uint8_t *)&t5))));
    F.ret = t6;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_at_newline_94n732e(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_at_newline_94n732e F;
    void * gm_l0; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 83u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    gm_t3fcbuehh3bs2 t2;
    gm_f_kind_k239kpg((void *)&t2, t1);
    gm_t3fcbuehh3bs2 t3 = t2;
    uint8_t t4 = ((GM_LD(uint8_t, ((uint8_t *)&t3) + 0) == ((uint8_t)9ULL)));
    F.ret = t4;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void * gm_f_box_j4urmed(void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_box_j4urmed F;
    GM_STACK_CHECK(&F, 56u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memcpy(&F.l0, a0, sizeof F.l0);
    gm_tq7s98ihonkju t1 = gm_f_new_ve8bb01((void *)NULL, cx, NULL, &(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l0)), &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kdpnk3k16op)));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tq7s98ihonkju t2 = t1;
    if (((((uint8_t *)&t2))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t2) + 8, gm_srcuiab49hlgv7g, 452, 52);
    void * t3 = GM_LD(void *, (((uint8_t *)&t2) + 8));
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
inline __attribute__((aligned(4))) uint8_t gm_f_eat_kw_tvv0v3e(void * a0, gm_t9qkh0rsooc8d a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_eat_kw_tvv0v3e F;
    void * gm_l0; /* p */
    gm_t9qkh0rsooc8d gm_l1; /* k */
    GM_STACK_CHECK_SHALLOW(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t3 = gm_l0;
        gm_t9qkh0rsooc8d t4 = gm_l1;
        uint8_t t5 = gm_f_at_kw_p0r3sr9(t3, t4);
        uint8_t t6 = (uint8_t)!t5;
        if (!t6) goto ie2;
        {
            uint8_t t8 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        {
            void * t10 = gm_l0;
            gm_tm4pc0emhrv1g t11;
            gm_f_bump_q2i40re((void *)&t11, t10);
        }
    }
    {
        uint8_t t12 = 0;
        F.ret = ((uint8_t)1);
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_eat_qmock9g(void * a0, gm_t9qkh0rsooc8d a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_eat_qmock9g F;
    void * gm_l0; /* p */
    gm_t9qkh0rsooc8d gm_l1; /* x */
    GM_STACK_CHECK_SHALLOW(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t3 = gm_l0;
        gm_t9qkh0rsooc8d t4 = gm_l1;
        uint8_t t5 = gm_f_at_54906rm(t3, t4);
        uint8_t t6 = (uint8_t)!t5;
        if (!t6) goto ie2;
        {
            uint8_t t8 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        {
            void * t10 = gm_l0;
            gm_tm4pc0emhrv1g t11;
            gm_f_bump_q2i40re((void *)&t11, t10);
        }
    }
    {
        uint8_t t12 = 0;
        F.ret = ((uint8_t)1);
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_65q32rn(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_65q32rn F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_atlcs4j(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_atlcs4j F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_d53jqcq(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_d53jqcq F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_flce2bb(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_flce2bb F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ppj8ikt(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_ppj8ikt F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_u4e74ha(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_u4e74ha F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ure7n5c(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_ure7n5c F;
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
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 144)));
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
inline __attribute__((aligned(4))) void gm_f_kind_k239kpg(void *gm_out, void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_kind_k239kpg F;
    void * gm_l0; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 56u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    void * t2 = gm_f_tok_ai6sc12(t1);
    gm_t3fcbuehh3bs2 t3 = GM_LD(gm_t3fcbuehh3bs2, (((uint8_t *)t2) + 0));
    F.ret = t3;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_list_18sm81j(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_18sm81j F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_atlcs4j();
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
inline __attribute__((aligned(4))) void gm_f_list_3odrho7(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_3odrho7 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_d53jqcq();
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
inline __attribute__((aligned(4))) void gm_f_list_89j9i66(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_89j9i66 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_ppj8ikt();
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
inline __attribute__((aligned(4))) void gm_f_list_g2q4cds(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_g2q4cds F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_u4e74ha();
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
inline __attribute__((aligned(4))) void gm_f_list_mho6jfm(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_mho6jfm F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_65q32rn();
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
inline __attribute__((aligned(4))) void gm_f_list_ps4qcsk(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_ps4qcsk F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_ure7n5c();
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
inline __attribute__((aligned(4))) void gm_f_list_s24cnj6(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_s24cnj6 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_flce2bb();
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
inline __attribute__((aligned(4))) uint32_t gm_f_lo_fp82gt4(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_lo_fp82gt4 F;
    void * gm_l0; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 52u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    gm_tl874t2atnlh3 t2 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_bm4ll8d(t1));
    gm_tl874t2atnlh3 t3 = t2;
    uint32_t t4 = GM_LD(uint32_t, (((uint8_t *)&t3) + 4));
    F.ret = t4;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_mk_expr_53qj7nd(void *gm_out, void * a0, const void * a1, gm_cseahvg8v7p0vs a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_mk_expr_53qj7nd F;
    void * gm_l0; /* p */
    gm_tl874t2atnlh3 gm_l2; /* at */
    GM_STACK_CHECK_SHALLOW(&F, 108u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&F.l1, a1, sizeof F.l1);
    gm_l2 = GM_CONV(gm_tl874t2atnlh3, a2);
    memset((uint8_t *)&(*(gm_tc3oht4ksth9d *)gm_out) + 4, 0, 4);
    memset((uint8_t *)&(*(gm_tc3oht4ksth9d *)gm_out) + 84, 0, 4);
    void * t1 = gm_l0;
    uint32_t t2 = gm_f_next_id_a7upaf4(t1);
    gm_t2gtbh39ptekd t3 = F.l1;
    gm_tl874t2atnlh3 t4 = gm_l2;
    GM_ST(uint32_t, (uint8_t *)&(*(gm_tc3oht4ksth9d *)gm_out) + 0, t2);
    GM_ST(gm_t2gtbh39ptekd, (uint8_t *)&(*(gm_tc3oht4ksth9d *)gm_out) + 8, t3);
    GM_ST(gm_tl874t2atnlh3, (uint8_t *)&(*(gm_tc3oht4ksth9d *)gm_out) + 72, t4);
    goto gm_fdone;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) uint32_t gm_f_next_id_a7upaf4(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_next_id_a7upaf4 F;
    void * gm_l0; /* p */
    uint32_t gm_l1; /* id */
    void * gm_l2; /* <tmp> */
    GM_STACK_CHECK_SHALLOW(&F, 101u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 56));
        uint32_t t4 = GM_LD(uint32_t, ((uint8_t *)t3));
        gm_l1 = t4;
    }
    {
        {
            void * t6 = gm_l0;
            void * t7 = GM_LD(void *, (((uint8_t *)t6) + 56));
            void * t8 = (void *)((uint8_t *)t7);
            gm_l2 = t8;
        }
        {
            void * t9 = gm_l2;
            void * t10 = gm_l2;
            uint32_t t11 = GM_LD(uint32_t, ((uint8_t *)t10));
            uint32_t r12; bool o13 = __builtin_add_overflow(t11, ((uint32_t)1ULL), &r12);
            if (o13) gm_trap("integer overflow in `+`", gm_srcuiab49hlgv7g, 443, 5);
            uint32_t t14 = r12;
            GM_ST(uint32_t, ((uint8_t *)t9), t14);
        }
    }
    {
        uint32_t t15 = gm_l1;
        uint8_t t16 = 0;
        F.ret = t15;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_nth_c0r2nvs(void *gm_out, void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_nth_c0r2nvs F;
    void * gm_l0; /* p */
    uint64_t gm_l1; /* n */
    GM_STACK_CHECK_SHALLOW(&F, 168u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    gm_b16_8 t2 = GM_LD(gm_b16_8, (((uint8_t *)t1) + 8));
    gm_b16_8 t3 = t2;
    void * t4 = gm_l0;
    uint64_t t5 = GM_LD(uint64_t, (((uint8_t *)t4) + 72));
    uint64_t t6 = gm_l1;
    uint64_t r7; bool o8 = __builtin_add_overflow(t5, t6, &r7);
    if (o8) gm_trap("integer overflow in `+`", gm_srcuiab49hlgv7g, 126, 70);
    uint64_t t9 = r7;
    void * t10 = gm_l0;
    uint64_t t11 = GM_LD(uintptr_t, (((uint8_t *)t10) + 8) + 8);
    uint64_t r12; bool o13 = __builtin_sub_overflow(t11, ((uint64_t)1ULL), &r12);
    if (o13) gm_trap("integer overflow in `-`", gm_srcuiab49hlgv7g, 126, 85);
    uint64_t t14 = r12;
    uint64_t t15 = (t9 < t14 ? t9 : t14);
    uint64_t t16 = (uintptr_t)t15;
    gm_b16_8 t17 = t3;
    uint64_t t18 = GM_LD(uintptr_t, ((uint8_t *)&t17) + 8);
    if (t16 >= (uintptr_t)(t18)) gm_trap_index(t16, t18, gm_srcuiab49hlgv7g, 126, 62);
    gm_t3fcbuehh3bs2 t19 = GM_LD(gm_t3fcbuehh3bs2, (((uint8_t *)GM_LD(void *, ((uint8_t *)&t17)) + t16 * 48) + 0));
    F.ret = t19;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) uint8_t gm_f_nth_is_3fgqmcb(void * a0, uint64_t a1, gm_t9qkh0rsooc8d a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_nth_is_3fgqmcb F;
    void * gm_l0; /* p */
    uint64_t gm_l1; /* n */
    gm_t9qkh0rsooc8d gm_l2; /* x */
    GM_STACK_CHECK_SHALLOW(&F, 102u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t1 = gm_l0;
    uint64_t t2 = gm_l1;
    gm_t3fcbuehh3bs2 t3;
    gm_f_nth_c0r2nvs((void *)&t3, t1, t2);
    gm_t3fcbuehh3bs2 t4 = t3;
    gm_t9qkh0rsooc8d t5 = gm_l2;
    gm_t9qkh0rsooc8d t6 = t5;
    uint8_t t7 = (((GM_LD(uint8_t, ((uint8_t *)&t4) + 0) == ((uint8_t)8ULL)) && gm_eqv3tj1pu43g((((uint8_t *)&t4) + 8), ((uint8_t *)&t6))));
    F.ret = t7;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_parse_clauses_2n3uc9a(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_parse_clauses_2n3uc9a F;
    GM_STACK_CHECK(&F, 97u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    {
        gm_f_list_s24cnj6((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = F.l0;
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s01mi6tf86obv));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(12));
        gm_closure t4 = ((gm_closure){(void *)gm_f_fn_6ihve2o, (void *)&F.h, cx});
        gm_b0_1 t5 = gm_f_members_oq7gs8i((void *)NULL, cx, NULL, t2, t3, t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_b16_8 t6 = GM_LD(gm_b16_8, (((uint8_t *)&F.l1) + 0));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_parse_struct_members_rj26glk(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_parse_struct_members_rj26glk F;
    GM_STACK_CHECK(&F, 97u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    {
        gm_f_list_3odrho7((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = F.l0;
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s2pa39hqq5hrk));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(11));
        gm_closure t4 = ((gm_closure){(void *)gm_f_fn_uom6vil, (void *)&F.h, cx});
        gm_b0_1 t5 = gm_f_members_oq7gs8i((void *)NULL, cx, NULL, t2, t3, t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_b16_8 t6 = GM_LD(gm_b16_8, (((uint8_t *)&F.l1) + 0));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_parse_union_members_g8ptfng(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_parse_union_members_g8ptfng F;
    GM_STACK_CHECK(&F, 97u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    {
        gm_f_list_g2q4cds((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = F.l0;
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_se2fbs5jfigcc));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(10));
        gm_closure t4 = ((gm_closure){(void *)gm_f_fn_gp5mf74, (void *)&F.h, cx});
        gm_b0_1 t5 = gm_f_members_oq7gs8i((void *)NULL, cx, NULL, t2, t3, t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_b16_8 t6 = GM_LD(gm_b16_8, (((uint8_t *)&F.l1) + 0));
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_report_djvi2hf(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2) {
    struct gm_fr_report_djvi2hf F;
    void * gm_l0; /* p */
    gm_tl874t2atnlh3 gm_l1; /* at */
    gm_b16_8 gm_l2; /* message */
    GM_STACK_CHECK(&F, 112u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        void * t3 = gm_l0;
        uint64_t t4 = GM_LD(uint64_t, (((uint8_t *)t3) + 136));
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)1ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_srcuiab49hlgv7g, 253, 5);
        uint64_t t7 = r5;
        GM_ST(uint64_t, (((uint8_t *)t2) + 136), t7);
    }
    {
        void * t8 = gm_l0;
        void * t9 = GM_LD(void *, (((uint8_t *)t8) + 64));
        gm_tl874t2atnlh3 t10 = gm_l1;
        gm_b16_8 t11 = gm_l2;
        gm_b0_1 t12 = gm_f_report_eardf07((void *)NULL, cx, NULL, t9, t10, t11);
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
inline __attribute__((aligned(4))) gm_cs3cuou245tr5f gm_f_set_restr_p85qqmv(void * a0, gm_cs3cuou245tr5f a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_set_restr_p85qqmv F;
    void * gm_l0; /* p */
    gm_toh0ootodhjue gm_l1; /* r */
    gm_toh0ootodhjue gm_l2; /* old */
    GM_STACK_CHECK_SHALLOW(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_toh0ootodhjue, a1);
    {
        void * t2 = gm_l0;
        gm_toh0ootodhjue t3 = GM_LD(gm_toh0ootodhjue, (((uint8_t *)t2) + 128));
        gm_l2 = t3;
    }
    {
        void * t4 = gm_l0;
        gm_toh0ootodhjue t5 = gm_l1;
        GM_ST(gm_toh0ootodhjue, (((uint8_t *)t4) + 128), t5);
    }
    {
        gm_toh0ootodhjue t6 = gm_l2;
        uint8_t t7 = 0;
        F.ret = t6;
        goto gm_fret;
    }
gm_fret:
    return GM_CONV(gm_cs3cuou245tr5f, F.ret);
gm_fprop:
    return GM_CONV(gm_cs3cuou245tr5f, F.ret);
}
inline __attribute__((aligned(4))) void gm_f_skip_newlines_0ap7sq2(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_skip_newlines_0ap7sq2 F;
    void * gm_l0; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 73u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        for (;;) {
            void * t3 = gm_l0;
            uint8_t t4 = gm_f_at_newline_94n732e(t3);
            if (!t4) goto lb2;
            {
                {
                    void * t8 = gm_l0;
                    gm_tm4pc0emhrv1g t9;
                    gm_f_bump_q2i40re((void *)&t9, t8);
                }
            }
lc2:;
        }
lb2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) gm_cseahvg8v7p0vs gm_f_span_bm4ll8d(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_span_bm4ll8d F;
    void * gm_l0; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 60u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    void * t2 = gm_f_tok_ai6sc12(t1);
    gm_tl874t2atnlh3 t3 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t2) + 32));
    F.ret = t3;
gm_fret:
    return GM_CONV(gm_cseahvg8v7p0vs, F.ret);
gm_fprop:
    return GM_CONV(gm_cseahvg8v7p0vs, F.ret);
}
inline __attribute__((aligned(4))) gm_cseahvg8v7p0vs gm_f_span_from_1sqap0o(void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_span_from_1sqap0o F;
    void * gm_l0; /* p */
    uint32_t gm_l1; /* start */
    GM_STACK_CHECK_SHALLOW(&F, 84u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    uint32_t t2 = GM_LD(uint32_t, (((uint8_t *)t1) + 0));
    uint32_t t3 = gm_l1;
    void * t4 = gm_l0;
    uint32_t t5 = GM_LD(uint32_t, (((uint8_t *)t4) + 80));
    uint32_t t6 = gm_l1;
    uint32_t t7 = (t5 > t6 ? t5 : t6);
    gm_tl874t2atnlh3 t8;
    GM_ST(uint32_t, ((uint8_t *)&t8 + 0), t2);
    GM_ST(uint32_t, ((uint8_t *)&t8 + 4), t3);
    GM_ST(uint32_t, ((uint8_t *)&t8 + 8), t7);
    F.ret = t8;
gm_fret:
    return GM_CONV(gm_cseahvg8v7p0vs, F.ret);
gm_fprop:
    return GM_CONV(gm_cseahvg8v7p0vs, F.ret);
}
inline __attribute__((aligned(4))) void * gm_f_tok_ai6sc12(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_tok_ai6sc12 F;
    void * gm_l0; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 136u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    gm_b16_8 t2 = GM_LD(gm_b16_8, (((uint8_t *)t1) + 8));
    gm_b16_8 t3 = t2;
    void * t4 = gm_l0;
    uint64_t t5 = GM_LD(uint64_t, (((uint8_t *)t4) + 72));
    void * t6 = gm_l0;
    uint64_t t7 = GM_LD(uintptr_t, (((uint8_t *)t6) + 8) + 8);
    uint64_t r8; bool o9 = __builtin_sub_overflow(t7, ((uint64_t)1ULL), &r8);
    if (o9) gm_trap("integer overflow in `-`", gm_srcuiab49hlgv7g, 122, 73);
    uint64_t t10 = r8;
    uint64_t t11 = (t5 < t10 ? t5 : t10);
    uint64_t t12 = (uintptr_t)t11;
    gm_b16_8 t13 = t3;
    uint64_t t14 = GM_LD(uintptr_t, ((uint8_t *)&t13) + 8);
    if (t12 >= (uintptr_t)(t14)) gm_trap_index(t12, t14, gm_srcuiab49hlgv7g, 122, 56);
    void * t15 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&t13)) + t12 * 48);
    F.ret = t15;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_t9qkh0rsooc8d gm_f_fn_6ihve2o(void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_fn_6ihve2o F;
    GM_STACK_CHECK(&F, 282u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        void * t2 = (void *)((uint8_t *)&((struct gm_fr_parse_clauses_2n3uc9a *)((&F.h)->up))->l1);
        void * t3 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_parse_clauses_2n3uc9a *)((&F.h)->up))->l0));
        gm_t2av9t33q6vt2 t4;
        gm_f_parse_clause_cqi8t7k((void *)&t4, (void *)NULL, cx, NULL, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t2av9t33q6vt2 t5 = t4;
        if (((((uint8_t *)&t5))[0] != 0)) {
            gm_b0_1 t6 = GM_LD(gm_b0_1, (((uint8_t *)&t5) + 8));
            gm_t9qkh0rsooc8d t7;
            ((uint8_t *)&t7)[0] = 1;
            GM_ST(gm_b0_1, ((uint8_t *)&t7 + 1), t6);
            F.ret = t7;
            goto gm_fret;
        }
        gm_t99vmhfllumbh t8 = GM_LD(gm_t99vmhfllumbh, (((uint8_t *)&t5) + 8));
        gm_tftb1odc739t5 t9 = gm_f_push_np5t726(t2, &t8);
        gm_tftb1odc739t5 t10 = t9;
        if (((((uint8_t *)&t10))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t10) + 1, gm_src8m8lg034q48k, 246, 9);
        gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
    }
    {
        void * t12 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_parse_clauses_2n3uc9a *)((&F.h)->up))->l0));
        gm_b16_8 t13;
        GM_ST(void *, (uint8_t *)&t13, (void *)(gm_sauu2s1pbjqsh));
        GM_ST(uintptr_t, (uint8_t *)&t13 + 8, (uintptr_t)(10));
        gm_b0_1 t14 = gm_f_expect_terminator_f7kp9sk((void *)NULL, cx, NULL, t12, t13);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_b0_1 t15;
        gm_t9qkh0rsooc8d t16;
        ((uint8_t *)&t16)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t16 + 1), t15);
        gm_t9qkh0rsooc8d t17 = t16;
        uint8_t t18 = ((((uint8_t *)&t17))[0] != 0);
        F.ret = t16;
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
inline __attribute__((aligned(4))) gm_t9qkh0rsooc8d gm_f_fn_gp5mf74(void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_fn_gp5mf74 F;
    uint8_t gm_l0; /* sep */
    GM_STACK_CHECK(&F, 389u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        void * t2 = (void *)((uint8_t *)&((struct gm_fr_parse_union_members_g8ptfng *)((&F.h)->up))->l1);
        void * t3 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_parse_union_members_g8ptfng *)((&F.h)->up))->l0));
        gm_tup3hijlfo1vq t4;
        gm_f_parse_union_member_dba1nar((void *)&t4, (void *)NULL, cx, NULL, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tup3hijlfo1vq t5 = t4;
        if (((((uint8_t *)&t5))[0] != 0)) {
            gm_b0_1 t6 = GM_LD(gm_b0_1, (((uint8_t *)&t5) + 8));
            gm_t9qkh0rsooc8d t7;
            ((uint8_t *)&t7)[0] = 1;
            GM_ST(gm_b0_1, ((uint8_t *)&t7 + 1), t6);
            F.ret = t7;
            goto gm_fret;
        }
        gm_t03a3imodqi32 t8 = GM_LD(gm_t03a3imodqi32, (((uint8_t *)&t5) + 8));
        gm_tftb1odc739t5 t9 = gm_f_push_imcrlr1(t2, &t8);
        gm_tftb1odc739t5 t10 = t9;
        if (((((uint8_t *)&t10))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t10) + 1, gm_src8m8lg034q48k, 146, 9);
        gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
    }
    {
        void * t12 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_parse_union_members_g8ptfng *)((&F.h)->up))->l0));
        gm_t9qkh0rsooc8d t13;
        GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)57ULL));
        uint8_t t14 = gm_f_eat_qmock9g(t12, t13);
        gm_l0 = t14;
    }
    {
        for (;;) {
            void * t16 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_parse_union_members_g8ptfng *)((&F.h)->up))->l0));
            uint8_t t17 = gm_f_at_newline_94n732e(t16);
            if (!t17) goto lb15;
            {
                {
                    void * t21 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_parse_union_members_g8ptfng *)((&F.h)->up))->l0));
                    gm_tm4pc0emhrv1g t22;
                    gm_f_bump_q2i40re((void *)&t22, t21);
                }
            }
            {
                gm_l0 = ((uint8_t)1);
            }
lc15:;
        }
lb15:;
    }
    {
        uint8_t t24 = gm_l0;
        uint8_t t25 = (uint8_t)!t24;
        if (!t25) goto ie23;
        void * t26 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_parse_union_members_g8ptfng *)((&F.h)->up))->l0));
        gm_t9qkh0rsooc8d t27;
        GM_ST(uint8_t, ((uint8_t *)&t27 + 0), ((uint8_t)66ULL));
        uint8_t t28 = gm_f_at_54906rm(t26, t27);
        uint8_t t29 = (uint8_t)!t28;
        if (!t29) goto ie23;
        void * t30 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_parse_union_members_g8ptfng *)((&F.h)->up))->l0));
        uint8_t t31 = gm_f_at_eof_ebcpkv0(t30);
        uint8_t t32 = (uint8_t)!t31;
        if (!t32) goto ie23;
        {
            {
                void * t35 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_parse_union_members_g8ptfng *)((&F.h)->up))->l0));
                gm_b16_8 t36;
                GM_ST(void *, (uint8_t *)&t36, (void *)(gm_sobecpce3c56n));
                GM_ST(uintptr_t, (uint8_t *)&t36 + 8, (uintptr_t)(34));
                gm_b0_1 t37 = gm_f_expected_5famfb9((void *)NULL, cx, NULL, t35, t36);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
        }
        {
            void * t38 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_parse_union_members_g8ptfng *)((&F.h)->up))->l0));
            gm_f_recover_to_stmt_end_freq9f3(t38);
        }
        goto ix23;
ie23:;
ix23:;
    }
    {
        gm_b0_1 t39;
        gm_t9qkh0rsooc8d t40;
        ((uint8_t *)&t40)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t40 + 1), t39);
        gm_t9qkh0rsooc8d t41 = t40;
        uint8_t t42 = ((((uint8_t *)&t41))[0] != 0);
        F.ret = t40;
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
inline __attribute__((aligned(4))) gm_t9qkh0rsooc8d gm_f_fn_qh4flda(void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_fn_qh4flda F;
    GM_STACK_CHECK(&F, 498u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        void * t2 = (void *)((uint8_t *)&((struct gm_fr_parse_foreign_l4091na *)((&F.h)->up))->l3);
        void * t3 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_parse_foreign_l4091na *)((&F.h)->up))->l0));
        gm_tspns3qg8u35u t4;
        gm_f_parse_foreign_member_rjqa59g((void *)&t4, (void *)NULL, cx, NULL, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tspns3qg8u35u t5 = t4;
        if (((((uint8_t *)&t5))[0] != 0)) {
            gm_b0_1 t6 = GM_LD(gm_b0_1, (((uint8_t *)&t5) + 8));
            gm_t9qkh0rsooc8d t7;
            ((uint8_t *)&t7)[0] = 1;
            GM_ST(gm_b0_1, ((uint8_t *)&t7 + 1), t6);
            F.ret = t7;
            goto gm_fret;
        }
        gm_t7rh4gj19r55v t8 = GM_LD(gm_t7rh4gj19r55v, (((uint8_t *)&t5) + 8));
        gm_tftb1odc739t5 t9 = gm_f_push_8isgrsr(t2, &t8);
        gm_tftb1odc739t5 t10 = t9;
        if (((((uint8_t *)&t10))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t10) + 1, gm_src8m8lg034q48k, 281, 9);
        gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
    }
    {
        void * t12 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_parse_foreign_l4091na *)((&F.h)->up))->l0));
        gm_b16_8 t13;
        GM_ST(void *, (uint8_t *)&t13, (void *)(gm_ssgjh7i7oqvi4));
        GM_ST(uintptr_t, (uint8_t *)&t13 + 8, (uintptr_t)(23));
        gm_b0_1 t14 = gm_f_expect_terminator_f7kp9sk((void *)NULL, cx, NULL, t12, t13);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_b0_1 t15;
        gm_t9qkh0rsooc8d t16;
        ((uint8_t *)&t16)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t16 + 1), t15);
        gm_t9qkh0rsooc8d t17 = t16;
        uint8_t t18 = ((((uint8_t *)&t17))[0] != 0);
        F.ret = t16;
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
inline __attribute__((aligned(4))) gm_t9qkh0rsooc8d gm_f_fn_rupmstg(void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_fn_rupmstg F;
    GM_STACK_CHECK(&F, 306u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        void * t2 = (void *)((uint8_t *)&((struct gm_fr_parse_effect_lit_cqei081 *)((&F.h)->up))->l3);
        void * t3 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_parse_effect_lit_cqei081 *)((&F.h)->up))->l0));
        gm_tkkip668684tl t4;
        gm_f_parse_op_decl_681ptb5((void *)&t4, (void *)NULL, cx, NULL, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tkkip668684tl t5 = t4;
        if (((((uint8_t *)&t5))[0] != 0)) {
            gm_b0_1 t6 = GM_LD(gm_b0_1, (((uint8_t *)&t5) + 8));
            gm_t9qkh0rsooc8d t7;
            ((uint8_t *)&t7)[0] = 1;
            GM_ST(gm_b0_1, ((uint8_t *)&t7 + 1), t6);
            F.ret = t7;
            goto gm_fret;
        }
        gm_tc0sbs1tp6d7f t8 = GM_LD(gm_tc0sbs1tp6d7f, (((uint8_t *)&t5) + 8));
        gm_tftb1odc739t5 t9 = gm_f_push_mnuql9a(t2, &t8);
        gm_tftb1odc739t5 t10 = t9;
        if (((((uint8_t *)&t10))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t10) + 1, gm_src8m8lg034q48k, 193, 9);
        gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
    }
    {
        void * t12 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_parse_effect_lit_cqei081 *)((&F.h)->up))->l0));
        gm_b16_8 t13;
        GM_ST(void *, (uint8_t *)&t13, (void *)(gm_sucoj11hr978o));
        GM_ST(uintptr_t, (uint8_t *)&t13 + 8, (uintptr_t)(13));
        gm_b0_1 t14 = gm_f_expect_terminator_f7kp9sk((void *)NULL, cx, NULL, t12, t13);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_b0_1 t15;
        gm_t9qkh0rsooc8d t16;
        ((uint8_t *)&t16)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t16 + 1), t15);
        gm_t9qkh0rsooc8d t17 = t16;
        uint8_t t18 = ((((uint8_t *)&t17))[0] != 0);
        F.ret = t16;
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
inline __attribute__((aligned(4))) gm_t9qkh0rsooc8d gm_f_fn_uom6vil(void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_fn_uom6vil F;
    GM_STACK_CHECK(&F, 317u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        void * t2 = (void *)((uint8_t *)&((struct gm_fr_parse_struct_members_rj26glk *)((&F.h)->up))->l1);
        void * t3 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_parse_struct_members_rj26glk *)((&F.h)->up))->l0));
        gm_t6gdobsbntbpm t4;
        gm_f_parse_struct_member_7aem2to((void *)&t4, (void *)NULL, cx, NULL, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t6gdobsbntbpm t5 = t4;
        if (((((uint8_t *)&t5))[0] != 0)) {
            gm_b0_1 t6 = GM_LD(gm_b0_1, (((uint8_t *)&t5) + 8));
            gm_t9qkh0rsooc8d t7;
            ((uint8_t *)&t7)[0] = 1;
            GM_ST(gm_b0_1, ((uint8_t *)&t7 + 1), t6);
            F.ret = t7;
            goto gm_fret;
        }
        gm_tfc3oljlofib6 t8 = GM_LD(gm_tfc3oljlofib6, (((uint8_t *)&t5) + 8));
        gm_tftb1odc739t5 t9 = gm_f_push_a7r9dj6(t2, &t8);
        gm_tftb1odc739t5 t10 = t9;
        if (((((uint8_t *)&t10))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t10) + 1, gm_src8m8lg034q48k, 57, 9);
        gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
    }
    {
        void * t13 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_parse_struct_members_rj26glk *)((&F.h)->up))->l0));
        gm_t9qkh0rsooc8d t14;
        GM_ST(uint8_t, ((uint8_t *)&t14 + 0), ((uint8_t)57ULL));
        uint8_t t15 = gm_f_eat_qmock9g(t13, t14);
        uint8_t t16 = (uint8_t)!t15;
        if (!t16) goto ie12;
        {
            void * t18 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_parse_struct_members_rj26glk *)((&F.h)->up))->l0));
            gm_b16_8 t19;
            GM_ST(void *, (uint8_t *)&t19, (void *)(gm_s70nlbtnmugoc));
            GM_ST(uintptr_t, (uint8_t *)&t19 + 8, (uintptr_t)(9));
            gm_b0_1 t20 = gm_f_expect_terminator_f7kp9sk((void *)NULL, cx, NULL, t18, t19);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix12;
ie12:;
ix12:;
    }
    {
        gm_b0_1 t21;
        gm_t9qkh0rsooc8d t22;
        ((uint8_t *)&t22)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t22 + 1), t21);
        gm_t9qkh0rsooc8d t23 = t22;
        uint8_t t24 = ((((uint8_t *)&t23))[0] != 0);
        F.ret = t22;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_members_oq7gs8i(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_closure a2) {
    struct gm_fr_members_oq7gs8i F;
    void * gm_l0; /* p */
    gm_b16_8 gm_l1; /* what */
    gm_closure gm_l2; /* body */
    gm_toh0ootodhjue gm_l3; /* old */
    uint64_t gm_l4; /* before */
    GM_STACK_CHECK(&F, 433u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        gm_toh0ootodhjue t3;
        GM_ST(uint8_t, ((uint8_t *)&t3 + 0), ((uint8_t)0));
        GM_ST(uint8_t, ((uint8_t *)&t3 + 1), ((uint8_t)0));
        GM_ST(uint8_t, ((uint8_t *)&t3 + 2), ((uint8_t)0));
        gm_toh0ootodhjue t4 = GM_CONV(gm_toh0ootodhjue, gm_f_set_restr_p85qqmv(t2, GM_CONV(gm_cs3cuou245tr5f, t3)));
        gm_l3 = t4;
    }
    {
    }
    {
        for (;;) {
            if (!((uint8_t)1)) goto lb8;
            {
                void * t11 = gm_l0;
                gm_f_skip_newlines_0ap7sq2(t11);
            }
            {
                void * t13 = gm_l0;
                gm_t9qkh0rsooc8d t14;
                GM_ST(uint8_t, ((uint8_t *)&t14 + 0), ((uint8_t)66ULL));
                uint8_t t15 = gm_f_eat_qmock9g(t13, t14);
                if (!t15) goto ie12;
                {
                    goto lb8;
                }
                goto ix12;
ie12:;
ix12:;
            }
            {
                void * t18 = gm_l0;
                uint8_t t19 = gm_f_at_eof_ebcpkv0(t18);
                if (!t19) goto ie17;
                {
                    {
                        void * t22 = gm_l0;
                        gm_b16_8 t23 = gm_l1;
                        gm_tnpifv20irnph t24;
                        GM_ST(gm_b16_8, ((uint8_t *)&t24 + 0), t23);
                        gm_tabb710d9mq2e t25;
                        gm_f_format_iqcmufv((void *)&t25, (void *)NULL, cx, NULL, t24);
                        if (gm_unw.kind) {
                            goto gm_ll9;
                        }
                        gm_tabb710d9mq2e t26 = t25;
                        if (((((uint8_t *)&t26))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t26) + 8, gm_src8m8lg034q48k, 28, 28);
                        gm_b16_8 t27 = GM_LD(gm_b16_8, (((uint8_t *)&t26) + 8));
                        gm_b16_8 t28 = t27;
                        gm_b16_8 t29 = GM_LD(gm_b16_8, ((uint8_t *)&t28));
                        gm_b0_1 t30 = gm_f_expected_5famfb9((void *)NULL, cx, NULL, t22, t29);
                        if (gm_unw.kind) {
                            goto gm_ll9;
                        }
                    }
                }
                {
                    goto lb8;
                }
                goto ix17;
ie17:;
ix17:;
            }
            {
                void * t31 = gm_l0;
                uint64_t t32 = GM_LD(uint64_t, (((uint8_t *)t31) + 72));
                gm_l4 = t32;
            }
            {
                gm_closure t34 = gm_l2;
                gm_closure t35 = t34;
                gm_node *ccx36 = t35.cx;
                gm_node cn37; { gm_node *f = gm_find(cx, 587034433); if (f) { cn37 = *f; cn37.next = ccx36; ccx36 = &cn37; } }
                void * t38 = t35.code;
                gm_t9qkh0rsooc8d t39;
                if (GM_PTR_TAGGED(t38)) {
                    t39 = ((gm_t9qkh0rsooc8d (*)(void))GM_PTR_UNTAG(t38))();
                } else {
                    t39 = ((gm_t9qkh0rsooc8d (*)(void *, gm_node *, gm_node *))t38)(t35.env, ccx36, NULL);
                }
                if (gm_unw.kind) {
                    goto gm_ll9;
                }
                gm_t9qkh0rsooc8d t40 = t39;
                uint8_t t41;
                if (!((((uint8_t *)&t40))[0] != 0)) goto mn43;
                t41 = ((uint8_t)1);
                goto me42;
mn43:;
                t41 = ((uint8_t)0);
                goto me42;
mn44:;
                gm_trap("no match arm matched the scrutinee", gm_src8m8lg034q48k, 32, 12);
me42:;
                if (!t41) goto ie33;
                {
                    void * t46 = gm_l0;
                    gm_f_recover_to_stmt_end_freq9f3(t46);
                }
                goto ix33;
ie33:;
ix33:;
            }
            {
                void * t48 = gm_l0;
                uint64_t t49 = GM_LD(uint64_t, (((uint8_t *)t48) + 72));
                uint64_t t50 = gm_l4;
                uint8_t t51 = ((t49 == t50));
                if (!t51) goto ie47;
                {
                    {
                        void * t54 = gm_l0;
                        gm_tm4pc0emhrv1g t55;
                        gm_f_bump_q2i40re((void *)&t55, t54);
                    }
                }
                goto ix47;
ie47:;
ix47:;
            }
lc8:;
        }
gm_ll9:;
        if (gm_unw.frame == &F.h && gm_unw.label == 0) {
            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb8; }
            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc8; }
        }
        goto gm_ld1_1;
lb8:;
    }
    {
        {
            void * t57 = gm_l0;
            gm_toh0ootodhjue t58 = gm_l3;
            GM_ST(gm_toh0ootodhjue, (((uint8_t *)t57) + 128), t58);
        }
    }
    goto gm_ls59;
gm_ld1_1:;
    {
        gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
        {
            void * t61 = gm_l0;
            gm_toh0ootodhjue t62 = gm_l3;
            GM_ST(gm_toh0ootodhjue, (((uint8_t *)t61) + 128), t62);
        }
        gm_unw = gm_saved;
    }
    goto gm_lad;
gm_ls59:;
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
inline __attribute__((aligned(4))) uint8_t gm_f_paren_is_param_list_0bbahvv(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_paren_is_param_list_0bbahvv F;
    void * gm_l0; /* p */
    uint64_t gm_l1; /* k */
    GM_STACK_CHECK_SHALLOW(&F, 410u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t3 = gm_l0;
        gm_t9qkh0rsooc8d t4;
        GM_ST(uint8_t, ((uint8_t *)&t4 + 0), ((uint8_t)62ULL));
        uint8_t t5 = gm_f_nth_is_3fgqmcb(t3, ((uint64_t)1ULL), t4);
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
        gm_l1 = ((uint64_t)1ULL);
    }
    {
        for (;;) {
            void * t9 = gm_l0;
            uint64_t t10 = gm_l1;
            uint8_t t11 = gm_f_nth_is_name_oqhoobd(t9, t10);
            if (!t11) goto lb8;
            {
                uint64_t t14 = gm_l1;
                uint64_t r15; bool o16 = __builtin_add_overflow(t14, ((uint64_t)1ULL), &r15);
                if (o16) gm_trap("integer overflow in `+`", gm_src8m8lg034q48k, 113, 9);
                uint64_t t17 = r15;
                gm_l1 = t17;
            }
            {
                uint8_t t19 = 0;
                void * t20 = gm_l0;
                uint64_t t21 = gm_l1;
                gm_t9qkh0rsooc8d t22;
                GM_ST(uint8_t, ((uint8_t *)&t22 + 0), ((uint8_t)57ULL));
                uint8_t t23 = gm_f_nth_is_3fgqmcb(t20, t21, t22);
                if (t23) {
                    void * t24 = gm_l0;
                    uint64_t t25 = gm_l1;
                    uint64_t r26; bool o27 = __builtin_add_overflow(t25, ((uint64_t)1ULL), &r26);
                    if (o27) gm_trap("integer overflow in `+`", gm_src8m8lg034q48k, 114, 55);
                    uint64_t t28 = r26;
                    uint8_t t29 = gm_f_nth_is_name_oqhoobd(t24, t28);
                    t19 = t29;
                } else t19 = 0;
                uint8_t t30 = (uint8_t)!t19;
                if (!t30) goto ie18;
                {
                    goto lb8;
                }
                goto ix18;
ie18:;
ix18:;
            }
            {
                uint64_t t32 = gm_l1;
                uint64_t r33; bool o34 = __builtin_add_overflow(t32, ((uint64_t)1ULL), &r33);
                if (o34) gm_trap("integer overflow in `+`", gm_src8m8lg034q48k, 115, 9);
                uint64_t t35 = r33;
                gm_l1 = t35;
            }
lc8:;
        }
lb8:;
    }
    {
        uint8_t t36 = 0;
        uint64_t t37 = gm_l1;
        uint8_t t38 = (t37 > ((uint64_t)1ULL));
        if (t38) {
            void * t39 = gm_l0;
            uint64_t t40 = gm_l1;
            gm_t3fcbuehh3bs2 t41;
            gm_f_nth_c0r2nvs((void *)&t41, t39, t40);
            gm_t23tohc57ebhi t42;
            gm_t9qkh0rsooc8d t43;
            GM_ST(uint8_t, ((uint8_t *)&t43 + 0), ((uint8_t)56ULL));
            gm_t3fcbuehh3bs2 t44;
            GM_ST(uint8_t, (uint8_t *)&t44 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t44 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t44 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t44 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t44 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t44 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t44 + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&t44 + 24, 0);
            GM_ST(uint8_t, ((uint8_t *)&t44 + 0), ((uint8_t)8ULL));
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t44 + 8), t43);
            GM_ST(gm_t3fcbuehh3bs2, (uint8_t *)&t42 + 0, t44);
            gm_t9qkh0rsooc8d t45;
            GM_ST(uint8_t, ((uint8_t *)&t45 + 0), ((uint8_t)54ULL));
            gm_t3fcbuehh3bs2 t46;
            GM_ST(uint8_t, (uint8_t *)&t46 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t46 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t46 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t46 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t46 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t46 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t46 + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&t46 + 24, 0);
            GM_ST(uint8_t, ((uint8_t *)&t46 + 0), ((uint8_t)8ULL));
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t46 + 8), t45);
            GM_ST(gm_t3fcbuehh3bs2, (uint8_t *)&t42 + 32, t46);
            gm_t23tohc57ebhi t47 = t42;
            uint8_t t48 = 0;
            gm_t3fcbuehh3bs2 t49 = t41;
            if (gm_eqt7ftn1841u(((uint8_t *)&t47) + 0, ((uint8_t *)&t49))) t48 = 1;
            if (gm_eqt7ftn1841u(((uint8_t *)&t47) + 32, ((uint8_t *)&t49))) t48 = 1;
            uint8_t t50 = (uint8_t)(t48);
            t36 = t50;
        } else t36 = 0;
        uint8_t t51 = 0;
        F.ret = t36;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) void gm_f_parse_clause_cqi8t7k(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_parse_clause_cqi8t7k F;
    void * gm_l0; /* p */
    uint32_t gm_l1; /* start */
    gm_t08olq97rscn3 gm_l2; /* name */
    GM_STACK_CHECK(&F, 2020u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        uint32_t t3 = gm_f_lo_fp82gt4(t2);
        gm_l1 = t3;
    }
    {
        void * t4 = gm_l0;
        gm_b16_8 t5;
        GM_ST(void *, (uint8_t *)&t5, (void *)(gm_svtkcim3vundp));
        GM_ST(uintptr_t, (uint8_t *)&t5 + 8, (uintptr_t)(17));
        gm_t4jn8fl3lmk5k t6;
        gm_f_parse_decl_ident_31rb1id((void *)&t6, (void *)NULL, cx, NULL, t4, t5);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t4jn8fl3lmk5k t7 = t6;
        if (((((uint8_t *)&t7))[0] != 0)) {
            gm_b0_1 t8 = GM_LD(gm_b0_1, (((uint8_t *)&t7) + 8));
            gm_t2av9t33q6vt2 t9;
            memset(&t9, 0, sizeof t9);
            ((uint8_t *)&t9)[0] = 1;
            GM_ST(gm_b0_1, ((uint8_t *)&t9 + 8), t8);
            F.ret = t9;
            goto gm_fret;
        }
        gm_t08olq97rscn3 t10 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&t7) + 8));
        gm_l2 = t10;
    }
    {
        {
            void * t12 = gm_l0;
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)61ULL));
            gm_b16_8 t14;
            GM_ST(void *, (uint8_t *)&t14, (void *)(gm_saf3o40admn10));
            GM_ST(uintptr_t, (uint8_t *)&t14 + 8, (uintptr_t)(24));
            gm_t0u0j6voh3loe t15 = gm_f_expect_ijgfrs1((void *)NULL, cx, NULL, t12, t13, t14);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t0u0j6voh3loe t16 = t15;
            if (((((uint8_t *)&t16))[0] != 0)) {
                gm_b0_1 t17 = GM_LD(gm_b0_1, (((uint8_t *)&t16) + 4));
                gm_t2av9t33q6vt2 t18;
                memset(&t18, 0, sizeof t18);
                ((uint8_t *)&t18)[0] = 1;
                GM_ST(gm_b0_1, ((uint8_t *)&t18 + 8), t17);
                F.ret = t18;
                goto gm_fret;
            }
            gm_tl874t2atnlh3 t19 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&t16) + 4));
        }
    }
    {
        gm_f_list_mho6jfm((void *)&F.l3, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        for (;;) {
            void * t21 = gm_l0;
            gm_t9qkh0rsooc8d t22;
            GM_ST(uint8_t, ((uint8_t *)&t22 + 0), ((uint8_t)62ULL));
            uint8_t t23 = gm_f_at_54906rm(t21, t22);
            uint8_t t24 = (uint8_t)!t23;
            if (!t24) goto lb20;
            {
                void * t27 = (void *)((uint8_t *)&F.l3);
                void * t28 = gm_l0;
                gm_b16_8 t29;
                GM_ST(void *, (uint8_t *)&t29, (void *)(gm_sri0h4dm5d3ul));
                GM_ST(uintptr_t, (uint8_t *)&t29 + 8, (uintptr_t)(16));
                gm_t4jn8fl3lmk5k t30;
                gm_f_parse_name_figjvbe((void *)&t30, (void *)NULL, cx, NULL, t28, t29);
                if (gm_unw.kind) {
                    goto gm_ll25;
                }
                gm_t4jn8fl3lmk5k t31 = t30;
                if (((((uint8_t *)&t31))[0] != 0)) {
                    gm_b0_1 t32 = GM_LD(gm_b0_1, (((uint8_t *)&t31) + 8));
                    gm_t2av9t33q6vt2 t33;
                    memset(&t33, 0, sizeof t33);
                    ((uint8_t *)&t33)[0] = 1;
                    GM_ST(gm_b0_1, ((uint8_t *)&t33 + 8), t32);
                    F.ret = t33;
                    goto gm_fret;
                }
                gm_t08olq97rscn3 t34 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&t31) + 8));
                gm_tftb1odc739t5 t35 = gm_f_push_s5rtia7(t27, &t34);
                gm_tftb1odc739t5 t36 = t35;
                if (((((uint8_t *)&t36))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t36) + 1, gm_src8m8lg034q48k, 260, 9);
                gm_b0_1 t37 = GM_LD(gm_b0_1, (((uint8_t *)&t36) + 1));
            }
            {
                void * t39 = gm_l0;
                gm_t9qkh0rsooc8d t40;
                GM_ST(uint8_t, ((uint8_t *)&t40 + 0), ((uint8_t)57ULL));
                uint8_t t41 = gm_f_eat_qmock9g(t39, t40);
                uint8_t t42 = (uint8_t)!t41;
                if (!t42) goto ie38;
                {
                    goto lb20;
                }
                goto ix38;
ie38:;
ix38:;
            }
lc20:;
        }
gm_ll25:;
        if (gm_unw.frame == &F.h && gm_unw.label == 0) {
            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb20; }
            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc20; }
        }
        goto gm_lad;
lb20:;
    }
    {
        {
            void * t45 = gm_l0;
            gm_t9qkh0rsooc8d t46;
            GM_ST(uint8_t, ((uint8_t *)&t46 + 0), ((uint8_t)62ULL));
            gm_b16_8 t47;
            GM_ST(void *, (uint8_t *)&t47, (void *)(gm_sl34v6fvasecs));
            GM_ST(uintptr_t, (uint8_t *)&t47 + 8, (uintptr_t)(30));
            gm_t0u0j6voh3loe t48 = gm_f_expect_ijgfrs1((void *)NULL, cx, NULL, t45, t46, t47);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t0u0j6voh3loe t49 = t48;
            if (((((uint8_t *)&t49))[0] != 0)) {
                gm_b0_1 t50 = GM_LD(gm_b0_1, (((uint8_t *)&t49) + 4));
                gm_t2av9t33q6vt2 t51;
                memset(&t51, 0, sizeof t51);
                ((uint8_t *)&t51)[0] = 1;
                GM_ST(gm_b0_1, ((uint8_t *)&t51 + 8), t50);
                F.ret = t51;
                goto gm_fret;
            }
            gm_tl874t2atnlh3 t52 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&t49) + 4));
        }
    }
    {
        {
            void * t54 = gm_l0;
            gm_t9qkh0rsooc8d t55;
            GM_ST(uint8_t, ((uint8_t *)&t55 + 0), ((uint8_t)59ULL));
            gm_b16_8 t56;
            GM_ST(void *, (uint8_t *)&t56, (void *)(gm_s5om28rj8ddf9));
            GM_ST(uintptr_t, (uint8_t *)&t56 + 8, (uintptr_t)(27));
            gm_t0u0j6voh3loe t57 = gm_f_expect_ijgfrs1((void *)NULL, cx, NULL, t54, t55, t56);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t0u0j6voh3loe t58 = t57;
            if (((((uint8_t *)&t58))[0] != 0)) {
                gm_b0_1 t59 = GM_LD(gm_b0_1, (((uint8_t *)&t58) + 4));
                gm_t2av9t33q6vt2 t60;
                memset(&t60, 0, sizeof t60);
                ((uint8_t *)&t60)[0] = 1;
                GM_ST(gm_b0_1, ((uint8_t *)&t60 + 8), t59);
                F.ret = t60;
                goto gm_fret;
            }
            gm_tl874t2atnlh3 t61 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&t58) + 4));
        }
    }
    {
        gm_to1cssm8dfiv5 t62;
        void * t64 = gm_l0;
        gm_t9qkh0rsooc8d t65;
        GM_ST(uint8_t, ((uint8_t *)&t65 + 0), ((uint8_t)65ULL));
        uint8_t t66 = gm_f_at_54906rm(t64, t65);
        if (!t66) goto ie63;
        void * t68 = gm_l0;
        gm_to1cssm8dfiv5 t69;
        gm_f_parse_block_expr_0j6b78q((void *)&t69, (void *)NULL, cx, NULL, t68);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_to1cssm8dfiv5 t70 = t69;
        t62 = t70;
        goto ix63;
ie63:;
        void * t72 = gm_l0;
        gm_to1cssm8dfiv5 t73;
        gm_f_parse_expr_bkb2n6e((void *)&t73, (void *)NULL, cx, NULL, t72);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_to1cssm8dfiv5 t74 = t73;
        t62 = t74;
ix63:;
        gm_to1cssm8dfiv5 t75 = t62;
        if (((((uint8_t *)&t75))[0] != 0)) {
            gm_b0_1 t76 = GM_LD(gm_b0_1, (((uint8_t *)&t75) + 8));
            gm_t2av9t33q6vt2 t77;
            memset(&t77, 0, sizeof t77);
            ((uint8_t *)&t77)[0] = 1;
            GM_ST(gm_b0_1, ((uint8_t *)&t77 + 8), t76);
            F.ret = t77;
            goto gm_fret;
        }
        gm_tc3oht4ksth9d t78 = GM_LD(gm_tc3oht4ksth9d, (((uint8_t *)&t75) + 8));
        F.l4 = t78;
    }
    {
        gm_t99vmhfllumbh t79;
        memset((uint8_t *)&t79 + 68, 0, 4);
        memcpy((uint8_t *)&t79 + 0, ((uint8_t *)&gm_l2), 32);
        gm_b16_8 t80 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
        GM_ST(gm_b16_8, (uint8_t *)&t79 + 32, t80);
        void * t81 = gm_f_box_j4urmed((void *)NULL, cx, NULL, &(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l4)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(void *, (uint8_t *)&t79 + 48, t81);
        void * t82 = gm_l0;
        uint32_t t83 = gm_l1;
        gm_tl874t2atnlh3 t84 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_from_1sqap0o(t82, t83));
        GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t79 + 56, t84);
        gm_t2av9t33q6vt2 t85;
        GM_ST(uint8_t, (uint8_t *)&t85 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t85 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t85 + 4, 0);
        ((uint8_t *)&t85)[0] = 0;
        GM_ST(gm_t99vmhfllumbh, ((uint8_t *)&t85 + 8), t79);
        gm_t2av9t33q6vt2 t86 = t85;
        uint8_t t87 = ((((uint8_t *)&t86))[0] != 0);
        F.ret = t85;
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
extern __attribute__((aligned(4))) gm_b16_8 gm_f_parse_clauses_2n3uc9a(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) void gm_f_parse_decl_params_ftnplpo(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_parse_decl_params_ftnplpo F;
    void * gm_l0; /* p */
    gm_b16_8 gm_l1; /* params */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 595u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        {
            void * t3 = gm_l0;
            gm_t9qkh0rsooc8d t4;
            GM_ST(uint8_t, ((uint8_t *)&t4 + 0), ((uint8_t)61ULL));
            gm_b16_8 t5;
            GM_ST(void *, (uint8_t *)&t5, (void *)(gm_sr4qbrk6tlgaa));
            GM_ST(uintptr_t, (uint8_t *)&t5 + 8, (uintptr_t)(0));
            gm_t0u0j6voh3loe t6 = gm_f_expect_ijgfrs1((void *)NULL, cx, NULL, t3, t4, t5);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t0u0j6voh3loe t7 = t6;
            if (((((uint8_t *)&t7))[0] != 0)) {
                gm_b0_1 t8 = GM_LD(gm_b0_1, (((uint8_t *)&t7) + 4));
                gm_tt7ek1a8bopcl t9;
                GM_ST(uint8_t, (uint8_t *)&t9 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t9 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t9 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t9 + 8, 0);
                GM_ST(uint64_t, (uint8_t *)&t9 + 16, 0);
                ((uint8_t *)&t9)[0] = 1;
                GM_ST(gm_b0_1, ((uint8_t *)&t9 + 8), t8);
                F.ret = t9;
                goto gm_fret;
            }
            gm_tl874t2atnlh3 t10 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&t7) + 4));
        }
    }
    {
        void * t11 = gm_l0;
        gm_thk954m1ol1ig t12;
        gm_f_parse_params_3g43t5f((void *)&t12, (void *)NULL, cx, NULL, t11);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_thk954m1ol1ig t13 = t12;
        if (((((uint8_t *)&t13))[0] != 0)) {
            gm_b0_1 t14 = GM_LD(gm_b0_1, (((uint8_t *)&t13) + 8));
            gm_tt7ek1a8bopcl t15;
            GM_ST(uint8_t, (uint8_t *)&t15 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t15 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t15 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t15 + 8, 0);
            GM_ST(uint64_t, (uint8_t *)&t15 + 16, 0);
            ((uint8_t *)&t15)[0] = 1;
            GM_ST(gm_b0_1, ((uint8_t *)&t15 + 8), t14);
            F.ret = t15;
            goto gm_fret;
        }
        gm_th2lqbo77pvsc t16 = GM_LD(gm_th2lqbo77pvsc, (((uint8_t *)&t13) + 8));
        gm_th2lqbo77pvsc t17 = t16;
        gm_b16_8 t18 = GM_LD(gm_b16_8, (((uint8_t *)&t17) + 0));
        gm_l1 = t18;
    }
    {
        {
            gm_b16_8 t20 = gm_l1;
            gm_l2 = t20;
        }
        {
            gm_l3 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t22 = gm_l3;
                uint64_t t23 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                uint8_t t24 = (t22 < t23);
                if (!t24) goto lb21;
                {
                    uint64_t t27 = gm_l3;
                    uint64_t t28 = (uintptr_t)t27;
                    uint64_t t29 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                    gm_tf2orqcrphmek t32 = GM_LD(gm_tf2orqcrphmek, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t28 * 56));
                    F.l4 = t32;
                    {
                        uint8_t t35 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 16));
                        if (!t35) goto ie34;
                        {
                            void * t37 = gm_l0;
                            gm_tl874t2atnlh3 t38 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l4) + 40));
                            gm_b16_8 t39;
                            GM_ST(void *, (uint8_t *)&t39, (void *)(gm_s9hi7i7cl3e2t));
                            GM_ST(uintptr_t, (uint8_t *)&t39 + 8, (uintptr_t)(77));
                            gm_b0_1 t40 = gm_f_report_djvi2hf((void *)NULL, cx, NULL, t37, t38, t39);
                            if (gm_unw.kind) {
                                goto gm_ll25;
                            }
                        }
                        goto ix34;
ie34:;
                        uint8_t t42 = 0;
                        uint64_t t43 = GM_LD(uintptr_t, (((uint8_t *)&F.l4) + 0) + 8);
                        uint8_t t44 = ((t43 == ((uint64_t)0ULL)));
                        if (!t44) {
                            void * t45 = GM_LD(void *, (((uint8_t *)&F.l4) + 24));
                            void * t46 = t45;
                            uint8_t t47;
                            if ((GM_LD(void *, ((uint8_t *)&t46)) != 0)) goto mn49;
                            t47 = ((uint8_t)1);
                            goto me48;
mn49:;
                            t47 = ((uint8_t)0);
                            goto me48;
mn50:;
                            gm_trap("no match arm matched the scrutinee", gm_src8m8lg034q48k, 12, 43);
me48:;
                            t42 = t47;
                        } else t42 = 1;
                        if (!t42) goto ie41;
                        {
                            void * t52 = gm_l0;
                            gm_tl874t2atnlh3 t53 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l4) + 40));
                            gm_b16_8 t54;
                            GM_ST(void *, (uint8_t *)&t54, (void *)(gm_saab9e9ochkfq));
                            GM_ST(uintptr_t, (uint8_t *)&t54 + 8, (uintptr_t)(55));
                            gm_b0_1 t55 = gm_f_report_djvi2hf((void *)NULL, cx, NULL, t52, t53, t54);
                            if (gm_unw.kind) {
                                goto gm_ll25;
                            }
                        }
                        goto ix41;
ie41:;
ix41:;
ix34:;
                    }
                    goto me30;
mn31:;
                    gm_trap("no match arm matched the scrutinee", gm_src8m8lg034q48k, 9, 5);
me30:;
                }
lc21:;
                {
                    uint64_t t57 = gm_l3;
                    uint64_t r58; bool o59 = __builtin_add_overflow(t57, ((uint64_t)1ULL), &r58);
                    uint64_t t60 = r58;
                    gm_l3 = t60;
                }
            }
gm_ll25:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb21; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc21; }
            }
            goto gm_lad;
lb21:;
        }
    }
    {
        gm_b16_8 t61 = gm_l1;
        gm_tt7ek1a8bopcl t62;
        GM_ST(uint8_t, (uint8_t *)&t62 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t62 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t62 + 4, 0);
        ((uint8_t *)&t62)[0] = 0;
        GM_ST(gm_b16_8, ((uint8_t *)&t62 + 8), t61);
        gm_tt7ek1a8bopcl t63 = t62;
        uint8_t t64 = ((((uint8_t *)&t63))[0] != 0);
        F.ret = t62;
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
inline __attribute__((aligned(4))) void gm_f_parse_effect_lit_cqei081(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_parse_effect_lit_cqei081 F;
    uint32_t gm_l1; /* start */
    gm_th2lqbo77pvsc gm_l2; /* params */
    gm_tl874t2atnlh3 gm_l4; /* whole */
    GM_STACK_CHECK(&F, 1182u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    {
        void * t2 = F.l0;
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_saqb7i299utsd));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(17));
        gm_t9qkh0rsooc8d t4 = gm_f_deny_in_head_06d9o22((void *)NULL, cx, NULL, t2, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t9qkh0rsooc8d t5 = t4;
        if (((((uint8_t *)&t5))[0] != 0)) {
            gm_b0_1 t6 = GM_LD(gm_b0_1, (((uint8_t *)&t5) + 1));
            gm_to1cssm8dfiv5 t7;
            memset(&t7, 0, sizeof t7);
            ((uint8_t *)&t7)[0] = 1;
            GM_ST(gm_b0_1, ((uint8_t *)&t7 + 8), t6);
            F.ret = t7;
            goto gm_fret;
        }
        gm_b0_1 t8 = GM_LD(gm_b0_1, (((uint8_t *)&t5) + 1));
    }
    {
        void * t9 = F.l0;
        uint32_t t10 = gm_f_lo_fp82gt4(t9);
        gm_l1 = t10;
    }
    {
        {
            void * t12 = F.l0;
            gm_tm4pc0emhrv1g t13;
            gm_f_bump_q2i40re((void *)&t13, t12);
        }
    }
    {
        gm_th2lqbo77pvsc t14;
        memset(&t14, 0, sizeof t14);
        gm_l2 = t14;
    }
    {
        void * t16 = F.l0;
        gm_t9qkh0rsooc8d t17;
        GM_ST(uint8_t, ((uint8_t *)&t17 + 0), ((uint8_t)61ULL));
        uint8_t t18 = gm_f_at_54906rm(t16, t17);
        if (!t18) goto ie15;
        {
            void * t20 = F.l0;
            gm_tt7ek1a8bopcl t21;
            gm_f_parse_decl_params_ftnplpo((void *)&t21, (void *)NULL, cx, NULL, t20);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tt7ek1a8bopcl t22 = t21;
            if (((((uint8_t *)&t22))[0] != 0)) {
                gm_b0_1 t23 = GM_LD(gm_b0_1, (((uint8_t *)&t22) + 8));
                gm_to1cssm8dfiv5 t24;
                memset(&t24, 0, sizeof t24);
                ((uint8_t *)&t24)[0] = 1;
                GM_ST(gm_b0_1, ((uint8_t *)&t24 + 8), t23);
                F.ret = t24;
                goto gm_fret;
            }
            gm_b16_8 t25 = GM_LD(gm_b16_8, (((uint8_t *)&t22) + 8));
            gm_th2lqbo77pvsc t26;
            GM_ST(uint8_t, (uint8_t *)&t26 + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&t26 + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&t26 + 20, 0);
            GM_ST(gm_b16_8, ((uint8_t *)&t26), t25);
            ((uint8_t *)&t26)[16] = 1;
            gm_l2 = t26;
        }
        goto ix15;
ie15:;
ix15:;
    }
    {
        {
            void * t28 = F.l0;
            gm_t9qkh0rsooc8d t29;
            GM_ST(uint8_t, ((uint8_t *)&t29 + 0), ((uint8_t)65ULL));
            gm_b16_8 t30;
            GM_ST(void *, (uint8_t *)&t30, (void *)(gm_s244ts3klchej));
            GM_ST(uintptr_t, (uint8_t *)&t30 + 8, (uintptr_t)(24));
            gm_t0u0j6voh3loe t31 = gm_f_expect_ijgfrs1((void *)NULL, cx, NULL, t28, t29, t30);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t0u0j6voh3loe t32 = t31;
            if (((((uint8_t *)&t32))[0] != 0)) {
                gm_b0_1 t33 = GM_LD(gm_b0_1, (((uint8_t *)&t32) + 4));
                gm_to1cssm8dfiv5 t34;
                memset(&t34, 0, sizeof t34);
                ((uint8_t *)&t34)[0] = 1;
                GM_ST(gm_b0_1, ((uint8_t *)&t34 + 8), t33);
                F.ret = t34;
                goto gm_fret;
            }
            gm_tl874t2atnlh3 t35 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&t32) + 4));
        }
    }
    {
        gm_f_list_89j9i66((void *)&F.l3, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t36 = F.l0;
        gm_b16_8 t37;
        GM_ST(void *, (uint8_t *)&t37, (void *)(gm_sbsjrunf4id25));
        GM_ST(uintptr_t, (uint8_t *)&t37 + 8, (uintptr_t)(11));
        gm_closure t38 = ((gm_closure){(void *)gm_f_fn_rupmstg, (void *)&F.h, cx});
        gm_b0_1 t39 = gm_f_members_oq7gs8i((void *)NULL, cx, NULL, t36, t37, t38);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t40 = F.l0;
        uint32_t t41 = gm_l1;
        gm_tl874t2atnlh3 t42 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_from_1sqap0o(t40, t41));
        gm_l4 = t42;
    }
    {
        void * t43 = F.l0;
        gm_th0lu7is1il78 t44;
        memset((uint8_t *)&t44 + 52, 0, 4);
        memcpy((uint8_t *)&t44 + 0, ((uint8_t *)&gm_l2), 24);
        gm_b16_8 t45 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
        GM_ST(gm_b16_8, (uint8_t *)&t44 + 24, t45);
        gm_tl874t2atnlh3 t46 = gm_l4;
        GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t44 + 40, t46);
        gm_tq7s98ihonkju t47 = gm_f_new_jrcm1co((void *)NULL, cx, NULL, &t44, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kbamu2ordri)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tq7s98ihonkju t48 = t47;
        if (((((uint8_t *)&t48))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t48) + 8, gm_src8m8lg034q48k, 198, 30);
        void * t49 = GM_LD(void *, (((uint8_t *)&t48) + 8));
        gm_t2gtbh39ptekd t50;
        memset(&t50, 0, sizeof t50);
        GM_ST(uint8_t, ((uint8_t *)&t50 + 0), ((uint8_t)34ULL));
        GM_ST(void *, ((uint8_t *)&t50 + 8), t49);
        gm_tl874t2atnlh3 t51 = gm_l4;
        gm_tc3oht4ksth9d t52;
        gm_f_mk_expr_53qj7nd((void *)&t52, t43, &t50, GM_CONV(gm_cseahvg8v7p0vs, t51));
        gm_to1cssm8dfiv5 t53;
        GM_ST(uint8_t, (uint8_t *)&t53 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t53 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t53 + 4, 0);
        ((uint8_t *)&t53)[0] = 0;
        GM_ST(gm_tc3oht4ksth9d, ((uint8_t *)&t53 + 8), t52);
        gm_to1cssm8dfiv5 t54 = t53;
        uint8_t t55 = ((((uint8_t *)&t54))[0] != 0);
        F.ret = t53;
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
inline __attribute__((aligned(4))) void gm_f_parse_effect_ref_8cmc20n(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_parse_effect_ref_8cmc20n F;
    void * gm_l0; /* p */
    gm_toh0ootodhjue gm_l1; /* old */
    GM_STACK_CHECK(&F, 67u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        gm_toh0ootodhjue t3;
        GM_ST(uint8_t, ((uint8_t *)&t3 + 0), ((uint8_t)1));
        GM_ST(uint8_t, ((uint8_t *)&t3 + 1), ((uint8_t)0));
        GM_ST(uint8_t, ((uint8_t *)&t3 + 2), ((uint8_t)0));
        gm_toh0ootodhjue t4 = GM_CONV(gm_toh0ootodhjue, gm_f_set_restr_p85qqmv(t2, GM_CONV(gm_cs3cuou245tr5f, t3)));
        gm_l1 = t4;
    }
    {
    }
    {
        void * t8 = gm_l0;
        gm_f_parse_postfix_ao4kstr((void *)&F.ret, (void *)NULL, cx, NULL, t8, ((uint8_t)0));
        if (gm_unw.kind) {
            goto gm_ld1_1;
        }
        uint8_t t9 = ((((uint8_t *)&F.ret))[0] != 0);
        gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = t9;
        goto gm_ld1_1;
    }
    {
        {
            void * t11 = gm_l0;
            gm_toh0ootodhjue t12 = gm_l1;
            GM_ST(gm_toh0ootodhjue, (((uint8_t *)t11) + 128), t12);
        }
    }
    goto gm_ls13;
gm_ld1_1:;
    {
        gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
        {
            void * t15 = gm_l0;
            gm_toh0ootodhjue t16 = gm_l1;
            GM_ST(gm_toh0ootodhjue, (((uint8_t *)t15) + 128), t16);
        }
        gm_unw = gm_saved;
    }
    goto gm_lad;
gm_ls13:;
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
inline __attribute__((aligned(4))) void gm_f_parse_foreign_l4091na(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_parse_foreign_l4091na F;
    uint32_t gm_l1; /* start */
    gm_tibupt4dmcqai gm_l2; /* lib */
    gm_tl874t2atnlh3 gm_l4; /* whole */
    GM_STACK_CHECK(&F, 1326u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    {
        void * t2 = F.l0;
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_scmbb8a9enevr));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(15));
        gm_t9qkh0rsooc8d t4 = gm_f_deny_in_head_06d9o22((void *)NULL, cx, NULL, t2, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t9qkh0rsooc8d t5 = t4;
        if (((((uint8_t *)&t5))[0] != 0)) {
            gm_b0_1 t6 = GM_LD(gm_b0_1, (((uint8_t *)&t5) + 1));
            gm_to1cssm8dfiv5 t7;
            memset(&t7, 0, sizeof t7);
            ((uint8_t *)&t7)[0] = 1;
            GM_ST(gm_b0_1, ((uint8_t *)&t7 + 8), t6);
            F.ret = t7;
            goto gm_fret;
        }
        gm_b0_1 t8 = GM_LD(gm_b0_1, (((uint8_t *)&t5) + 1));
    }
    {
        void * t9 = F.l0;
        uint32_t t10 = gm_f_lo_fp82gt4(t9);
        gm_l1 = t10;
    }
    {
        {
            void * t12 = F.l0;
            gm_tm4pc0emhrv1g t13;
            gm_f_bump_q2i40re((void *)&t13, t12);
        }
    }
    {
        void * t15 = F.l0;
        gm_t3fcbuehh3bs2 t16;
        gm_f_kind_k239kpg((void *)&t16, t15);
        gm_t3fcbuehh3bs2 t17 = t16;
        uint8_t t18 = GM_LD(uint8_t, (((uint8_t *)&t17) + 0));
        if (t18 != ((uint8_t)5ULL)) goto ie14;
        gm_tibupt4dmcqai t19 = GM_LD(gm_tibupt4dmcqai, (((uint8_t *)&t17) + 8));
        gm_l2 = t19;
        {
            {
                void * t22 = F.l0;
                gm_tm4pc0emhrv1g t23;
                gm_f_bump_q2i40re((void *)&t23, t22);
            }
        }
        {
            {
                void * t25 = F.l0;
                gm_t9qkh0rsooc8d t26;
                GM_ST(uint8_t, ((uint8_t *)&t26 + 0), ((uint8_t)65ULL));
                gm_b16_8 t27;
                GM_ST(void *, (uint8_t *)&t27, (void *)(gm_sfiqishktrbg2));
                GM_ST(uintptr_t, (uint8_t *)&t27 + 8, (uintptr_t)(26));
                gm_t0u0j6voh3loe t28 = gm_f_expect_ijgfrs1((void *)NULL, cx, NULL, t25, t26, t27);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_t0u0j6voh3loe t29 = t28;
                if (((((uint8_t *)&t29))[0] != 0)) {
                    gm_b0_1 t30 = GM_LD(gm_b0_1, (((uint8_t *)&t29) + 4));
                    gm_to1cssm8dfiv5 t31;
                    memset(&t31, 0, sizeof t31);
                    ((uint8_t *)&t31)[0] = 1;
                    GM_ST(gm_b0_1, ((uint8_t *)&t31 + 8), t30);
                    F.ret = t31;
                    goto gm_fret;
                }
                gm_tl874t2atnlh3 t32 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&t29) + 4));
            }
        }
        {
            gm_f_list_ps4qcsk((void *)&F.l3, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t33 = F.l0;
            gm_b16_8 t34;
            GM_ST(void *, (uint8_t *)&t34, (void *)(gm_sa0amdcmvej1l));
            GM_ST(uintptr_t, (uint8_t *)&t34 + 8, (uintptr_t)(13));
            gm_closure t35 = ((gm_closure){(void *)gm_f_fn_qh4flda, (void *)&F.h, cx});
            gm_b0_1 t36 = gm_f_members_oq7gs8i((void *)NULL, cx, NULL, t33, t34, t35);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t37 = F.l0;
            uint32_t t38 = gm_l1;
            gm_tl874t2atnlh3 t39 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_from_1sqap0o(t37, t38));
            gm_l4 = t39;
        }
        {
            void * t40 = F.l0;
            gm_tjbbfnqt6296n t41;
            memset((uint8_t *)&t41 + 52, 0, 4);
            memcpy((uint8_t *)&t41 + 0, ((uint8_t *)&gm_l2), 24);
            gm_b16_8 t42 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
            GM_ST(gm_b16_8, (uint8_t *)&t41 + 24, t42);
            gm_tl874t2atnlh3 t43 = gm_l4;
            GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t41 + 40, t43);
            gm_tq7s98ihonkju t44 = gm_f_new_asf83v8((void *)NULL, cx, NULL, &t41, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kplh4t9vgh7)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tq7s98ihonkju t45 = t44;
            if (((((uint8_t *)&t45))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t45) + 8, gm_src8m8lg034q48k, 286, 31);
            void * t46 = GM_LD(void *, (((uint8_t *)&t45) + 8));
            gm_t2gtbh39ptekd t47;
            memset(&t47, 0, sizeof t47);
            GM_ST(uint8_t, ((uint8_t *)&t47 + 0), ((uint8_t)39ULL));
            GM_ST(void *, ((uint8_t *)&t47 + 8), t46);
            gm_tl874t2atnlh3 t48 = gm_l4;
            gm_tc3oht4ksth9d t49;
            gm_f_mk_expr_53qj7nd((void *)&t49, t40, &t47, GM_CONV(gm_cseahvg8v7p0vs, t48));
            gm_to1cssm8dfiv5 t50;
            GM_ST(uint8_t, (uint8_t *)&t50 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t50 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t50 + 4, 0);
            ((uint8_t *)&t50)[0] = 0;
            GM_ST(gm_tc3oht4ksth9d, ((uint8_t *)&t50 + 8), t49);
            gm_to1cssm8dfiv5 t51 = t50;
            uint8_t t52 = ((((uint8_t *)&t51))[0] != 0);
            F.ret = t50;
            goto gm_fret;
        }
        goto ix14;
ie14:;
        {
            void * t54 = F.l0;
            gm_b16_8 t55;
            GM_ST(void *, (uint8_t *)&t55, (void *)(gm_s9b8bk12k1acq));
            GM_ST(uintptr_t, (uint8_t *)&t55 + 8, (uintptr_t)(52));
            gm_b0_1 t56 = gm_f_expected_5famfb9((void *)NULL, cx, NULL, t54, t55);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_to1cssm8dfiv5 t57;
            memset(&t57, 0, sizeof t57);
            ((uint8_t *)&t57)[0] = 1;
            GM_ST(gm_b0_1, ((uint8_t *)&t57 + 8), t56);
            gm_to1cssm8dfiv5 t58 = t57;
            uint8_t t59 = ((((uint8_t *)&t58))[0] != 0);
            F.ret = t57;
            goto gm_fret;
        }
ix14:;
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
__attribute__((aligned(4))) void gm_f_parse_foreign_member_rjqa59g(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_parse_foreign_member_rjqa59g F;
    void * gm_l0; /* p */
    uint32_t gm_l1; /* start */
    gm_b16_8 gm_l2; /* all */
    uint8_t gm_l4; /* global */
    gm_b16_8 gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    gm_t08olq97rscn3 gm_l8; /* name */
    void * gm_l9; /* ty */
    gm_t08olq97rscn3 gm_l10; /* name */
    GM_STACK_CHECK(&F, 4074u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        uint32_t t3 = gm_f_lo_fp82gt4(t2);
        gm_l1 = t3;
    }
    {
        void * t4 = gm_l0;
        gm_tt7ek1a8bopcl t5;
        gm_f_parse_attrs_imvfdlq((void *)&t5, (void *)NULL, cx, NULL, t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tt7ek1a8bopcl t6 = t5;
        if (((((uint8_t *)&t6))[0] != 0)) {
            gm_b0_1 t7 = GM_LD(gm_b0_1, (((uint8_t *)&t6) + 8));
            gm_tspns3qg8u35u t8;
            memset(&t8, 0, sizeof t8);
            ((uint8_t *)&t8)[0] = 1;
            GM_ST(gm_b0_1, ((uint8_t *)&t8 + 8), t7);
            F.ret = t8;
            goto gm_fret;
        }
        gm_b16_8 t9 = GM_LD(gm_b16_8, (((uint8_t *)&t6) + 8));
        gm_l2 = t9;
    }
    {
        gm_f_list_18sm81j((void *)&F.l3, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_l4 = ((uint8_t)0);
    }
    {
        {
            gm_b16_8 t11 = gm_l2;
            gm_l5 = t11;
        }
        {
            gm_l6 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t13 = gm_l6;
                uint64_t t14 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                uint8_t t15 = (t13 < t14);
                if (!t15) goto lb12;
                {
                    uint64_t t18 = gm_l6;
                    uint64_t t19 = (uintptr_t)t18;
                    uint64_t t20 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                    gm_tkacitepg5fk0 t23 = GM_LD(gm_tkacitepg5fk0, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l5)) + t19 * 72));
                    F.l7 = t23;
                    {
                        uint8_t t26 = (((GM_LD(uintptr_t, ((((uint8_t *)&F.l7) + 0) + 0) + 8) == 6) && memcmp(GM_LD(void *, ((((uint8_t *)&F.l7) + 0) + 0)), gm_sptnm8c6c52dr, 6) == 0));
                        if (!t26) goto ie25;
                        uint8_t t27 = gm_l4;
                        uint8_t t28 = (uint8_t)!t27;
                        if (!t28) goto ie25;
                        {
                            gm_l4 = ((uint8_t)1);
                        }
                        goto ix25;
ie25:;
                        {
                            void * t31 = (void *)((uint8_t *)&F.l3);
                            gm_tftb1odc739t5 t32 = gm_f_push_9cfi95o(t31, &(*(gm_tkacitepg5fk0 *)((uint8_t *)&F.l7)));
                            gm_tftb1odc739t5 t33 = t32;
                            if (((((uint8_t *)&t33))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t33) + 1, gm_src8m8lg034q48k, 299, 13);
                            gm_b0_1 t34 = GM_LD(gm_b0_1, (((uint8_t *)&t33) + 1));
                        }
ix25:;
                    }
                    goto me21;
mn22:;
                    gm_trap("no match arm matched the scrutinee", gm_src8m8lg034q48k, 295, 5);
me21:;
                }
lc12:;
                {
                    uint64_t t36 = gm_l6;
                    uint64_t r37; bool o38 = __builtin_add_overflow(t36, ((uint64_t)1ULL), &r37);
                    uint64_t t39 = r37;
                    gm_l6 = t39;
                }
            }
lb12:;
        }
    }
    {
        uint8_t t41 = gm_l4;
        if (!t41) goto ie40;
        {
            void * t43 = gm_l0;
            gm_b16_8 t44;
            GM_ST(void *, (uint8_t *)&t44, (void *)(gm_semavgcmso3pp));
            GM_ST(uintptr_t, (uint8_t *)&t44 + 8, (uintptr_t)(32));
            gm_t4jn8fl3lmk5k t45;
            gm_f_parse_decl_ident_31rb1id((void *)&t45, (void *)NULL, cx, NULL, t43, t44);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t4jn8fl3lmk5k t46 = t45;
            if (((((uint8_t *)&t46))[0] != 0)) {
                gm_b0_1 t47 = GM_LD(gm_b0_1, (((uint8_t *)&t46) + 8));
                gm_tspns3qg8u35u t48;
                memset(&t48, 0, sizeof t48);
                ((uint8_t *)&t48)[0] = 1;
                GM_ST(gm_b0_1, ((uint8_t *)&t48 + 8), t47);
                F.ret = t48;
                goto gm_fret;
            }
            gm_t08olq97rscn3 t49 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&t46) + 8));
            gm_l8 = t49;
        }
        {
            {
                void * t51 = gm_l0;
                gm_t9qkh0rsooc8d t52;
                GM_ST(uint8_t, ((uint8_t *)&t52 + 0), ((uint8_t)56ULL));
                gm_b16_8 t53;
                GM_ST(void *, (uint8_t *)&t53, (void *)(gm_s3qnfqte7c8r2));
                GM_ST(uintptr_t, (uint8_t *)&t53 + 8, (uintptr_t)(34));
                gm_t0u0j6voh3loe t54 = gm_f_expect_ijgfrs1((void *)NULL, cx, NULL, t51, t52, t53);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_t0u0j6voh3loe t55 = t54;
                if (((((uint8_t *)&t55))[0] != 0)) {
                    gm_b0_1 t56 = GM_LD(gm_b0_1, (((uint8_t *)&t55) + 4));
                    gm_tspns3qg8u35u t57;
                    memset(&t57, 0, sizeof t57);
                    ((uint8_t *)&t57)[0] = 1;
                    GM_ST(gm_b0_1, ((uint8_t *)&t57 + 8), t56);
                    F.ret = t57;
                    goto gm_fret;
                }
                gm_tl874t2atnlh3 t58 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&t55) + 4));
            }
        }
        {
            void * t59 = gm_l0;
            gm_to1cssm8dfiv5 t60;
            gm_f_parse_type_2m93dtq((void *)&t60, (void *)NULL, cx, NULL, t59);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_to1cssm8dfiv5 t61 = t60;
            if (((((uint8_t *)&t61))[0] != 0)) {
                gm_b0_1 t62 = GM_LD(gm_b0_1, (((uint8_t *)&t61) + 8));
                gm_tspns3qg8u35u t63;
                memset(&t63, 0, sizeof t63);
                ((uint8_t *)&t63)[0] = 1;
                GM_ST(gm_b0_1, ((uint8_t *)&t63 + 8), t62);
                F.ret = t63;
                goto gm_fret;
            }
            gm_tc3oht4ksth9d t64 = GM_LD(gm_tc3oht4ksth9d, (((uint8_t *)&t61) + 8));
            void * t65 = gm_f_box_j4urmed((void *)NULL, cx, NULL, &t64);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l9 = t65;
        }
        {
            gm_t7rh4gj19r55v t66;
            memset((uint8_t *)&t66 + 140, 0, 4);
            gm_b16_8 t67 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
            GM_ST(gm_b16_8, (uint8_t *)&t66 + 0, t67);
            gm_ts686ckv8ehm2 t68;
            memcpy((uint8_t *)&t68 + 0, ((uint8_t *)&gm_l8), 32);
            void * t69 = gm_l9;
            GM_ST(void *, (uint8_t *)&t68 + 32, t69);
            gm_tg69e9d4krfe7 t70;
            memset(&t70, 0, sizeof t70);
            GM_ST(uint8_t, ((uint8_t *)&t70 + 0), ((uint8_t)1ULL));
            GM_ST(gm_ts686ckv8ehm2, ((uint8_t *)&t70 + 8), t68);
            GM_ST(gm_tg69e9d4krfe7, (uint8_t *)&t66 + 16, t70);
            void * t71 = gm_l0;
            uint32_t t72 = gm_l1;
            gm_tl874t2atnlh3 t73 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_from_1sqap0o(t71, t72));
            GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t66 + 128, t73);
            gm_tspns3qg8u35u t74;
            GM_ST(uint8_t, (uint8_t *)&t74 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t74 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t74 + 4, 0);
            ((uint8_t *)&t74)[0] = 0;
            GM_ST(gm_t7rh4gj19r55v, ((uint8_t *)&t74 + 8), t66);
            gm_tspns3qg8u35u t75 = t74;
            uint8_t t76 = ((((uint8_t *)&t75))[0] != 0);
            F.ret = t74;
            goto gm_fret;
        }
        goto ix40;
ie40:;
ix40:;
    }
    {
        void * t77 = gm_l0;
        gm_b16_8 t78;
        GM_ST(void *, (uint8_t *)&t78, (void *)(gm_shhtc33mlfd4u));
        GM_ST(uintptr_t, (uint8_t *)&t78 + 8, (uintptr_t)(30));
        gm_t4jn8fl3lmk5k t79;
        gm_f_parse_decl_ident_31rb1id((void *)&t79, (void *)NULL, cx, NULL, t77, t78);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t4jn8fl3lmk5k t80 = t79;
        if (((((uint8_t *)&t80))[0] != 0)) {
            gm_b0_1 t81 = GM_LD(gm_b0_1, (((uint8_t *)&t80) + 8));
            gm_tspns3qg8u35u t82;
            memset(&t82, 0, sizeof t82);
            ((uint8_t *)&t82)[0] = 1;
            GM_ST(gm_b0_1, ((uint8_t *)&t82 + 8), t81);
            F.ret = t82;
            goto gm_fret;
        }
        gm_t08olq97rscn3 t83 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&t80) + 8));
        gm_l10 = t83;
    }
    {
        {
            void * t85 = gm_l0;
            gm_t9qkh0rsooc8d t86;
            GM_ST(uint8_t, ((uint8_t *)&t86 + 0), ((uint8_t)54ULL));
            gm_b16_8 t87;
            GM_ST(void *, (uint8_t *)&t87, (void *)(gm_s8l7qb8s3942u));
            GM_ST(uintptr_t, (uint8_t *)&t87 + 8, (uintptr_t)(34));
            gm_t0u0j6voh3loe t88 = gm_f_expect_ijgfrs1((void *)NULL, cx, NULL, t85, t86, t87);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t0u0j6voh3loe t89 = t88;
            if (((((uint8_t *)&t89))[0] != 0)) {
                gm_b0_1 t90 = GM_LD(gm_b0_1, (((uint8_t *)&t89) + 4));
                gm_tspns3qg8u35u t91;
                memset(&t91, 0, sizeof t91);
                ((uint8_t *)&t91)[0] = 1;
                GM_ST(gm_b0_1, ((uint8_t *)&t91 + 8), t90);
                F.ret = t91;
                goto gm_fret;
            }
            gm_tl874t2atnlh3 t92 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&t89) + 4));
        }
    }
    {
        void * t94 = gm_l0;
        gm_t9qkh0rsooc8d t95;
        GM_ST(uint8_t, ((uint8_t *)&t95 + 0), ((uint8_t)11ULL));
        uint8_t t96 = gm_f_at_kw_p0r3sr9(t94, t95);
        uint8_t t97 = (uint8_t)!t96;
        if (!t97) goto ie93;
        {
            void * t99 = gm_l0;
            gm_b16_8 t100;
            GM_ST(void *, (uint8_t *)&t100, (void *)(gm_sb3efvtufksie));
            GM_ST(uintptr_t, (uint8_t *)&t100 + 8, (uintptr_t)(36));
            gm_b0_1 t101 = gm_f_expected_5famfb9((void *)NULL, cx, NULL, t99, t100);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tspns3qg8u35u t102;
            memset(&t102, 0, sizeof t102);
            ((uint8_t *)&t102)[0] = 1;
            GM_ST(gm_b0_1, ((uint8_t *)&t102 + 8), t101);
            gm_tspns3qg8u35u t103 = t102;
            uint8_t t104 = ((((uint8_t *)&t103))[0] != 0);
            F.ret = t102;
            goto gm_fret;
        }
        goto ix93;
ie93:;
ix93:;
    }
    {
        void * t105 = gm_l0;
        gm_tdmdfjrg7d59n t106;
        gm_f_parse_fn_type_sig_jrv5v82((void *)&t106, (void *)NULL, cx, NULL, t105, ((uint8_t)1));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tdmdfjrg7d59n t107 = t106;
        if (((((uint8_t *)&t107))[0] != 0)) {
            gm_b0_1 t108 = GM_LD(gm_b0_1, (((uint8_t *)&t107) + 8));
            gm_tspns3qg8u35u t109;
            memset(&t109, 0, sizeof t109);
            ((uint8_t *)&t109)[0] = 1;
            GM_ST(gm_b0_1, ((uint8_t *)&t109 + 8), t108);
            F.ret = t109;
            goto gm_fret;
        }
        gm_tlivgedjs5ps3 t110 = GM_LD(gm_tlivgedjs5ps3, (((uint8_t *)&t107) + 8));
        F.l11 = t110;
    }
    {
        gm_t7rh4gj19r55v t111;
        memset((uint8_t *)&t111 + 140, 0, 4);
        gm_b16_8 t112 = gm_l2;
        GM_ST(gm_b16_8, (uint8_t *)&t111 + 0, t112);
        gm_t70h2c775g4n7 t113;
        memcpy((uint8_t *)&t113 + 0, ((uint8_t *)&gm_l10), 32);
        memcpy((uint8_t *)&t113 + 32, ((uint8_t *)&F.l11), 72);
        gm_tg69e9d4krfe7 t114;
        GM_ST(uint8_t, (uint8_t *)&t114 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t114 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t114 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t114 + 0), ((uint8_t)0ULL));
        GM_ST(gm_t70h2c775g4n7, ((uint8_t *)&t114 + 8), t113);
        GM_ST(gm_tg69e9d4krfe7, (uint8_t *)&t111 + 16, t114);
        void * t115 = gm_l0;
        uint32_t t116 = gm_l1;
        gm_tl874t2atnlh3 t117 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_from_1sqap0o(t115, t116));
        GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t111 + 128, t117);
        gm_tspns3qg8u35u t118;
        GM_ST(uint8_t, (uint8_t *)&t118 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t118 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t118 + 4, 0);
        ((uint8_t *)&t118)[0] = 0;
        GM_ST(gm_t7rh4gj19r55v, ((uint8_t *)&t118 + 8), t111);
        gm_tspns3qg8u35u t119 = t118;
        uint8_t t120 = ((((uint8_t *)&t119))[0] != 0);
        F.ret = t118;
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
inline __attribute__((aligned(4))) void gm_f_parse_handler_lit_1nkojsu(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_parse_handler_lit_1nkojsu F;
    void * gm_l0; /* p */
    uint32_t gm_l1; /* start */
    void * gm_l2; /* effect_ */
    gm_b16_8 gm_l3; /* clauses */
    gm_tl874t2atnlh3 gm_l4; /* whole */
    GM_STACK_CHECK(&F, 1324u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_snud2jia3fe8d));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(17));
        gm_t9qkh0rsooc8d t4 = gm_f_deny_in_head_06d9o22((void *)NULL, cx, NULL, t2, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t9qkh0rsooc8d t5 = t4;
        if (((((uint8_t *)&t5))[0] != 0)) {
            gm_b0_1 t6 = GM_LD(gm_b0_1, (((uint8_t *)&t5) + 1));
            gm_to1cssm8dfiv5 t7;
            memset(&t7, 0, sizeof t7);
            ((uint8_t *)&t7)[0] = 1;
            GM_ST(gm_b0_1, ((uint8_t *)&t7 + 8), t6);
            F.ret = t7;
            goto gm_fret;
        }
        gm_b0_1 t8 = GM_LD(gm_b0_1, (((uint8_t *)&t5) + 1));
    }
    {
        void * t9 = gm_l0;
        uint32_t t10 = gm_f_lo_fp82gt4(t9);
        gm_l1 = t10;
    }
    {
        {
            void * t12 = gm_l0;
            gm_tm4pc0emhrv1g t13;
            gm_f_bump_q2i40re((void *)&t13, t12);
        }
    }
    {
        void * t14 = gm_l0;
        gm_to1cssm8dfiv5 t15;
        gm_f_parse_effect_ref_8cmc20n((void *)&t15, (void *)NULL, cx, NULL, t14);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_to1cssm8dfiv5 t16 = t15;
        if (((((uint8_t *)&t16))[0] != 0)) {
            gm_b0_1 t17 = GM_LD(gm_b0_1, (((uint8_t *)&t16) + 8));
            gm_to1cssm8dfiv5 t18;
            memset(&t18, 0, sizeof t18);
            ((uint8_t *)&t18)[0] = 1;
            GM_ST(gm_b0_1, ((uint8_t *)&t18 + 8), t17);
            F.ret = t18;
            goto gm_fret;
        }
        gm_tc3oht4ksth9d t19 = GM_LD(gm_tc3oht4ksth9d, (((uint8_t *)&t16) + 8));
        void * t20 = gm_f_box_j4urmed((void *)NULL, cx, NULL, &t19);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l2 = t20;
    }
    {
        {
            void * t22 = gm_l0;
            gm_t9qkh0rsooc8d t23;
            GM_ST(uint8_t, ((uint8_t *)&t23 + 0), ((uint8_t)65ULL));
            gm_b16_8 t24;
            GM_ST(void *, (uint8_t *)&t24, (void *)(gm_sac45tip59t28));
            GM_ST(uintptr_t, (uint8_t *)&t24 + 8, (uintptr_t)(25));
            gm_t0u0j6voh3loe t25 = gm_f_expect_ijgfrs1((void *)NULL, cx, NULL, t22, t23, t24);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t0u0j6voh3loe t26 = t25;
            if (((((uint8_t *)&t26))[0] != 0)) {
                gm_b0_1 t27 = GM_LD(gm_b0_1, (((uint8_t *)&t26) + 4));
                gm_to1cssm8dfiv5 t28;
                memset(&t28, 0, sizeof t28);
                ((uint8_t *)&t28)[0] = 1;
                GM_ST(gm_b0_1, ((uint8_t *)&t28 + 8), t27);
                F.ret = t28;
                goto gm_fret;
            }
            gm_tl874t2atnlh3 t29 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&t26) + 4));
        }
    }
    {
        void * t30 = gm_l0;
        gm_b16_8 t31 = gm_f_parse_clauses_2n3uc9a((void *)NULL, cx, NULL, t30);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l3 = t31;
    }
    {
        void * t32 = gm_l0;
        uint32_t t33 = gm_l1;
        gm_tl874t2atnlh3 t34 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_from_1sqap0o(t32, t33));
        gm_l4 = t34;
    }
    {
        void * t35 = gm_l0;
        gm_tiacef8jroje2 t36;
        memset((uint8_t *)&t36 + 36, 0, 4);
        void * t37 = gm_l2;
        GM_ST(void *, (uint8_t *)&t36 + 0, t37);
        gm_b16_8 t38 = gm_l3;
        GM_ST(gm_b16_8, (uint8_t *)&t36 + 8, t38);
        gm_tl874t2atnlh3 t39 = gm_l4;
        GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t36 + 24, t39);
        gm_tq7s98ihonkju t40 = gm_f_new_ejgnaq4((void *)NULL, cx, NULL, &t36, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_keokac8bchm)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tq7s98ihonkju t41 = t40;
        if (((((uint8_t *)&t41))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t41) + 8, gm_src8m8lg034q48k, 231, 31);
        void * t42 = GM_LD(void *, (((uint8_t *)&t41) + 8));
        gm_t2gtbh39ptekd t43;
        memset(&t43, 0, sizeof t43);
        GM_ST(uint8_t, ((uint8_t *)&t43 + 0), ((uint8_t)37ULL));
        GM_ST(void *, ((uint8_t *)&t43 + 8), t42);
        gm_tl874t2atnlh3 t44 = gm_l4;
        gm_tc3oht4ksth9d t45;
        gm_f_mk_expr_53qj7nd((void *)&t45, t35, &t43, GM_CONV(gm_cseahvg8v7p0vs, t44));
        gm_to1cssm8dfiv5 t46;
        GM_ST(uint8_t, (uint8_t *)&t46 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t46 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t46 + 4, 0);
        ((uint8_t *)&t46)[0] = 0;
        GM_ST(gm_tc3oht4ksth9d, ((uint8_t *)&t46 + 8), t45);
        gm_to1cssm8dfiv5 t47 = t46;
        uint8_t t48 = ((((uint8_t *)&t47))[0] != 0);
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
__attribute__((aligned(4))) void gm_f_parse_op_decl_681ptb5(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_parse_op_decl_681ptb5 F;
    void * gm_l0; /* p */
    uint32_t gm_l1; /* start */
    gm_t08olq97rscn3 gm_l2; /* name */
    gm_t9qkh0rsooc8d gm_l3; /* k */
    gm_b16_8 gm_l4; /* params */
    void * gm_l5; /* ret */
    GM_STACK_CHECK(&F, 1883u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        uint32_t t3 = gm_f_lo_fp82gt4(t2);
        gm_l1 = t3;
    }
    {
        void * t4 = gm_l0;
        gm_b16_8 t5;
        GM_ST(void *, (uint8_t *)&t5, (void *)(gm_svtkcim3vundp));
        GM_ST(uintptr_t, (uint8_t *)&t5 + 8, (uintptr_t)(17));
        gm_t4jn8fl3lmk5k t6;
        gm_f_parse_decl_ident_31rb1id((void *)&t6, (void *)NULL, cx, NULL, t4, t5);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t4jn8fl3lmk5k t7 = t6;
        if (((((uint8_t *)&t7))[0] != 0)) {
            gm_b0_1 t8 = GM_LD(gm_b0_1, (((uint8_t *)&t7) + 8));
            gm_tkkip668684tl t9;
            memset(&t9, 0, sizeof t9);
            ((uint8_t *)&t9)[0] = 1;
            GM_ST(gm_b0_1, ((uint8_t *)&t9 + 8), t8);
            F.ret = t9;
            goto gm_fret;
        }
        gm_t08olq97rscn3 t10 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&t7) + 8));
        gm_l2 = t10;
    }
    {
        {
            void * t12 = gm_l0;
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)56ULL));
            gm_b16_8 t14;
            GM_ST(void *, (uint8_t *)&t14, (void *)(gm_s2jg45h85jjgj));
            GM_ST(uintptr_t, (uint8_t *)&t14 + 8, (uintptr_t)(36));
            gm_t0u0j6voh3loe t15 = gm_f_expect_ijgfrs1((void *)NULL, cx, NULL, t12, t13, t14);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t0u0j6voh3loe t16 = t15;
            if (((((uint8_t *)&t16))[0] != 0)) {
                gm_b0_1 t17 = GM_LD(gm_b0_1, (((uint8_t *)&t16) + 4));
                gm_tkkip668684tl t18;
                memset(&t18, 0, sizeof t18);
                ((uint8_t *)&t18)[0] = 1;
                GM_ST(gm_b0_1, ((uint8_t *)&t18 + 8), t17);
                F.ret = t18;
                goto gm_fret;
            }
            gm_tl874t2atnlh3 t19 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&t16) + 4));
        }
    }
    {
        void * t20 = gm_l0;
        gm_t3fcbuehh3bs2 t21;
        gm_f_kind_k239kpg((void *)&t21, t20);
        gm_t3fcbuehh3bs2 t22 = t21;
        gm_t9qkh0rsooc8d t23;
        uint8_t t30 = GM_LD(uint8_t, (((uint8_t *)&t22) + 0));
        switch (t30) { case ((uint8_t)0ULL): goto ma28; case ((uint8_t)1ULL): goto ma28; case ((uint8_t)2ULL): goto ma25; case ((uint8_t)3ULL): goto ma28; case ((uint8_t)4ULL): goto ma28; case ((uint8_t)5ULL): goto ma28; case ((uint8_t)6ULL): goto ma28; case ((uint8_t)7ULL): goto ma28; case ((uint8_t)8ULL): goto ma28; case ((uint8_t)9ULL): goto ma28; case ((uint8_t)10ULL): goto ma28; default: goto ma28; }
ma25:;
        uint8_t t32 = GM_LD(uint8_t, (((uint8_t *)&t22) + 0));
        if (t32 != ((uint8_t)2ULL)) goto mn31;
        uint8_t t33 = GM_LD(uint8_t, ((((uint8_t *)&t22) + 8) + 0));
        if (t33 != ((uint8_t)11ULL)) goto mn31;
        gm_t9qkh0rsooc8d t34;
        GM_ST(uint8_t, ((uint8_t *)&t34 + 0), ((uint8_t)0ULL));
        t23 = t34;
        goto me24;
mn31:;
ma26:;
        uint8_t t36 = GM_LD(uint8_t, (((uint8_t *)&t22) + 0));
        if (t36 != ((uint8_t)2ULL)) goto mn35;
        uint8_t t37 = GM_LD(uint8_t, ((((uint8_t *)&t22) + 8) + 0));
        if (t37 != ((uint8_t)9ULL)) goto mn35;
        gm_t9qkh0rsooc8d t38;
        GM_ST(uint8_t, ((uint8_t *)&t38 + 0), ((uint8_t)1ULL));
        t23 = t38;
        goto me24;
mn35:;
ma27:;
        uint8_t t40 = GM_LD(uint8_t, (((uint8_t *)&t22) + 0));
        if (t40 != ((uint8_t)2ULL)) goto mn39;
        uint8_t t41 = GM_LD(uint8_t, ((((uint8_t *)&t22) + 8) + 0));
        if (t41 != ((uint8_t)4ULL)) goto mn39;
        gm_t9qkh0rsooc8d t42;
        GM_ST(uint8_t, ((uint8_t *)&t42 + 0), ((uint8_t)2ULL));
        t23 = t42;
        goto me24;
mn39:;
ma28:;
        void * t44 = gm_l0;
        gm_b16_8 t45;
        GM_ST(void *, (uint8_t *)&t45, (void *)(gm_se84lb2ddmuai));
        GM_ST(uintptr_t, (uint8_t *)&t45 + 8, (uintptr_t)(20));
        gm_b0_1 t46 = gm_f_expected_5famfb9((void *)NULL, cx, NULL, t44, t45);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tkkip668684tl t47;
        memset(&t47, 0, sizeof t47);
        ((uint8_t *)&t47)[0] = 1;
        GM_ST(gm_b0_1, ((uint8_t *)&t47 + 8), t46);
        gm_tkkip668684tl t48 = t47;
        uint8_t t49 = ((((uint8_t *)&t48))[0] != 0);
        F.ret = t47;
        goto gm_fret;
        goto me24;
mn43:;
ma29:;
        gm_trap("no match arm matched the scrutinee", gm_src8m8lg034q48k, 206, 10);
me24:;
        gm_l3 = t23;
    }
    {
        {
            void * t51 = gm_l0;
            gm_tm4pc0emhrv1g t52;
            gm_f_bump_q2i40re((void *)&t52, t51);
        }
    }
    {
        {
            void * t54 = gm_l0;
            gm_t9qkh0rsooc8d t55;
            GM_ST(uint8_t, ((uint8_t *)&t55 + 0), ((uint8_t)61ULL));
            gm_b16_8 t56;
            GM_ST(void *, (uint8_t *)&t56, (void *)(gm_slgf61fmeeof0));
            GM_ST(uintptr_t, (uint8_t *)&t56 + 8, (uintptr_t)(27));
            gm_t0u0j6voh3loe t57 = gm_f_expect_ijgfrs1((void *)NULL, cx, NULL, t54, t55, t56);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t0u0j6voh3loe t58 = t57;
            if (((((uint8_t *)&t58))[0] != 0)) {
                gm_b0_1 t59 = GM_LD(gm_b0_1, (((uint8_t *)&t58) + 4));
                gm_tkkip668684tl t60;
                memset(&t60, 0, sizeof t60);
                ((uint8_t *)&t60)[0] = 1;
                GM_ST(gm_b0_1, ((uint8_t *)&t60 + 8), t59);
                F.ret = t60;
                goto gm_fret;
            }
            gm_tl874t2atnlh3 t61 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&t58) + 4));
        }
    }
    {
        void * t62 = gm_l0;
        gm_thk954m1ol1ig t63;
        gm_f_parse_params_3g43t5f((void *)&t63, (void *)NULL, cx, NULL, t62);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_thk954m1ol1ig t64 = t63;
        if (((((uint8_t *)&t64))[0] != 0)) {
            gm_b0_1 t65 = GM_LD(gm_b0_1, (((uint8_t *)&t64) + 8));
            gm_tkkip668684tl t66;
            memset(&t66, 0, sizeof t66);
            ((uint8_t *)&t66)[0] = 1;
            GM_ST(gm_b0_1, ((uint8_t *)&t66 + 8), t65);
            F.ret = t66;
            goto gm_fret;
        }
        gm_th2lqbo77pvsc t67 = GM_LD(gm_th2lqbo77pvsc, (((uint8_t *)&t64) + 8));
        gm_th2lqbo77pvsc t68 = t67;
        gm_b16_8 t69 = GM_LD(gm_b16_8, (((uint8_t *)&t68) + 0));
        gm_l4 = t69;
    }
    {
        gm_l5 = ((void *)0);
    }
    {
        void * t71 = gm_l0;
        gm_t9qkh0rsooc8d t72;
        GM_ST(uint8_t, ((uint8_t *)&t72 + 0), ((uint8_t)60ULL));
        uint8_t t73 = gm_f_eat_qmock9g(t71, t72);
        if (!t73) goto ie70;
        {
            void * t75 = gm_l0;
            gm_to1cssm8dfiv5 t76;
            gm_f_parse_type_2m93dtq((void *)&t76, (void *)NULL, cx, NULL, t75);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_to1cssm8dfiv5 t77 = t76;
            if (((((uint8_t *)&t77))[0] != 0)) {
                gm_b0_1 t78 = GM_LD(gm_b0_1, (((uint8_t *)&t77) + 8));
                gm_tkkip668684tl t79;
                memset(&t79, 0, sizeof t79);
                ((uint8_t *)&t79)[0] = 1;
                GM_ST(gm_b0_1, ((uint8_t *)&t79 + 8), t78);
                F.ret = t79;
                goto gm_fret;
            }
            gm_tc3oht4ksth9d t80 = GM_LD(gm_tc3oht4ksth9d, (((uint8_t *)&t77) + 8));
            void * t81 = gm_f_box_j4urmed((void *)NULL, cx, NULL, &t80);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l5 = t81;
        }
        goto ix70;
ie70:;
ix70:;
    }
    {
        gm_tc0sbs1tp6d7f t82;
        memset((uint8_t *)&t82 + 33, 0, 7);
        memset((uint8_t *)&t82 + 76, 0, 4);
        memcpy((uint8_t *)&t82 + 0, ((uint8_t *)&gm_l2), 32);
        gm_t9qkh0rsooc8d t83 = gm_l3;
        GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&t82 + 32, t83);
        gm_b16_8 t84 = gm_l4;
        GM_ST(gm_b16_8, (uint8_t *)&t82 + 40, t84);
        void * t85 = gm_l5;
        GM_ST(void *, (uint8_t *)&t82 + 56, t85);
        void * t86 = gm_l0;
        uint32_t t87 = gm_l1;
        gm_tl874t2atnlh3 t88 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_from_1sqap0o(t86, t87));
        GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t82 + 64, t88);
        gm_tkkip668684tl t89;
        GM_ST(uint8_t, (uint8_t *)&t89 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t89 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t89 + 4, 0);
        ((uint8_t *)&t89)[0] = 0;
        GM_ST(gm_tc0sbs1tp6d7f, ((uint8_t *)&t89 + 8), t82);
        gm_tkkip668684tl t90 = t89;
        uint8_t t91 = ((((uint8_t *)&t90))[0] != 0);
        F.ret = t89;
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
inline __attribute__((aligned(4))) void gm_f_parse_struct_lit_86pp62c(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_parse_struct_lit_86pp62c F;
    void * gm_l0; /* p */
    uint32_t gm_l1; /* start */
    gm_th2lqbo77pvsc gm_l2; /* params */
    gm_b16_8 gm_l3; /* where_ */
    gm_b16_8 gm_l4; /* ms */
    gm_tl874t2atnlh3 gm_l5; /* whole */
    GM_STACK_CHECK(&F, 1398u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sqsb0jek355o9));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(16));
        gm_t9qkh0rsooc8d t4 = gm_f_deny_in_head_06d9o22((void *)NULL, cx, NULL, t2, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t9qkh0rsooc8d t5 = t4;
        if (((((uint8_t *)&t5))[0] != 0)) {
            gm_b0_1 t6 = GM_LD(gm_b0_1, (((uint8_t *)&t5) + 1));
            gm_to1cssm8dfiv5 t7;
            memset(&t7, 0, sizeof t7);
            ((uint8_t *)&t7)[0] = 1;
            GM_ST(gm_b0_1, ((uint8_t *)&t7 + 8), t6);
            F.ret = t7;
            goto gm_fret;
        }
        gm_b0_1 t8 = GM_LD(gm_b0_1, (((uint8_t *)&t5) + 1));
    }
    {
        void * t9 = gm_l0;
        uint32_t t10 = gm_f_lo_fp82gt4(t9);
        gm_l1 = t10;
    }
    {
        {
            void * t12 = gm_l0;
            gm_tm4pc0emhrv1g t13;
            gm_f_bump_q2i40re((void *)&t13, t12);
        }
    }
    {
        gm_th2lqbo77pvsc t14;
        memset(&t14, 0, sizeof t14);
        gm_l2 = t14;
    }
    {
        void * t16 = gm_l0;
        gm_t9qkh0rsooc8d t17;
        GM_ST(uint8_t, ((uint8_t *)&t17 + 0), ((uint8_t)61ULL));
        uint8_t t18 = gm_f_at_54906rm(t16, t17);
        if (!t18) goto ie15;
        {
            void * t20 = gm_l0;
            gm_tt7ek1a8bopcl t21;
            gm_f_parse_decl_params_ftnplpo((void *)&t21, (void *)NULL, cx, NULL, t20);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tt7ek1a8bopcl t22 = t21;
            if (((((uint8_t *)&t22))[0] != 0)) {
                gm_b0_1 t23 = GM_LD(gm_b0_1, (((uint8_t *)&t22) + 8));
                gm_to1cssm8dfiv5 t24;
                memset(&t24, 0, sizeof t24);
                ((uint8_t *)&t24)[0] = 1;
                GM_ST(gm_b0_1, ((uint8_t *)&t24 + 8), t23);
                F.ret = t24;
                goto gm_fret;
            }
            gm_b16_8 t25 = GM_LD(gm_b16_8, (((uint8_t *)&t22) + 8));
            gm_th2lqbo77pvsc t26;
            GM_ST(uint8_t, (uint8_t *)&t26 + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&t26 + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&t26 + 20, 0);
            GM_ST(gm_b16_8, ((uint8_t *)&t26), t25);
            ((uint8_t *)&t26)[16] = 1;
            gm_l2 = t26;
        }
        goto ix15;
ie15:;
ix15:;
    }
    {
        void * t27 = gm_l0;
        gm_tt7ek1a8bopcl t28;
        gm_f_parse_where_opt_a5p2ja6((void *)&t28, (void *)NULL, cx, NULL, t27);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tt7ek1a8bopcl t29 = t28;
        if (((((uint8_t *)&t29))[0] != 0)) {
            gm_b0_1 t30 = GM_LD(gm_b0_1, (((uint8_t *)&t29) + 8));
            gm_to1cssm8dfiv5 t31;
            memset(&t31, 0, sizeof t31);
            ((uint8_t *)&t31)[0] = 1;
            GM_ST(gm_b0_1, ((uint8_t *)&t31 + 8), t30);
            F.ret = t31;
            goto gm_fret;
        }
        gm_b16_8 t32 = GM_LD(gm_b16_8, (((uint8_t *)&t29) + 8));
        gm_l3 = t32;
    }
    {
        {
            void * t34 = gm_l0;
            gm_t9qkh0rsooc8d t35;
            GM_ST(uint8_t, ((uint8_t *)&t35 + 0), ((uint8_t)65ULL));
            gm_b16_8 t36;
            GM_ST(void *, (uint8_t *)&t36, (void *)(gm_scmob1k27904r));
            GM_ST(uintptr_t, (uint8_t *)&t36 + 8, (uintptr_t)(24));
            gm_t0u0j6voh3loe t37 = gm_f_expect_ijgfrs1((void *)NULL, cx, NULL, t34, t35, t36);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t0u0j6voh3loe t38 = t37;
            if (((((uint8_t *)&t38))[0] != 0)) {
                gm_b0_1 t39 = GM_LD(gm_b0_1, (((uint8_t *)&t38) + 4));
                gm_to1cssm8dfiv5 t40;
                memset(&t40, 0, sizeof t40);
                ((uint8_t *)&t40)[0] = 1;
                GM_ST(gm_b0_1, ((uint8_t *)&t40 + 8), t39);
                F.ret = t40;
                goto gm_fret;
            }
            gm_tl874t2atnlh3 t41 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&t38) + 4));
        }
    }
    {
        void * t42 = gm_l0;
        gm_b16_8 t43 = gm_f_parse_struct_members_rj26glk((void *)NULL, cx, NULL, t42);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l4 = t43;
    }
    {
        void * t44 = gm_l0;
        uint32_t t45 = gm_l1;
        gm_tl874t2atnlh3 t46 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_from_1sqap0o(t44, t45));
        gm_l5 = t46;
    }
    {
        void * t47 = gm_l0;
        gm_tj7j8jssama8n t48;
        memset((uint8_t *)&t48 + 68, 0, 4);
        memcpy((uint8_t *)&t48 + 0, ((uint8_t *)&gm_l2), 24);
        gm_b16_8 t49 = gm_l3;
        GM_ST(gm_b16_8, (uint8_t *)&t48 + 24, t49);
        gm_b16_8 t50 = gm_l4;
        GM_ST(gm_b16_8, (uint8_t *)&t48 + 40, t50);
        gm_tl874t2atnlh3 t51 = gm_l5;
        GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t48 + 56, t51);
        gm_tq7s98ihonkju t52 = gm_f_new_hq21sop((void *)NULL, cx, NULL, &t48, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_krr4pbvd2tj)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tq7s98ihonkju t53 = t52;
        if (((((uint8_t *)&t53))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t53) + 8, gm_src8m8lg034q48k, 49, 30);
        void * t54 = GM_LD(void *, (((uint8_t *)&t53) + 8));
        gm_t2gtbh39ptekd t55;
        memset(&t55, 0, sizeof t55);
        GM_ST(uint8_t, ((uint8_t *)&t55 + 0), ((uint8_t)32ULL));
        GM_ST(void *, ((uint8_t *)&t55 + 8), t54);
        gm_tl874t2atnlh3 t56 = gm_l5;
        gm_tc3oht4ksth9d t57;
        gm_f_mk_expr_53qj7nd((void *)&t57, t47, &t55, GM_CONV(gm_cseahvg8v7p0vs, t56));
        gm_to1cssm8dfiv5 t58;
        GM_ST(uint8_t, (uint8_t *)&t58 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t58 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t58 + 4, 0);
        ((uint8_t *)&t58)[0] = 0;
        GM_ST(gm_tc3oht4ksth9d, ((uint8_t *)&t58 + 8), t57);
        gm_to1cssm8dfiv5 t59 = t58;
        uint8_t t60 = ((((uint8_t *)&t59))[0] != 0);
        F.ret = t58;
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
__attribute__((aligned(4))) void gm_f_parse_struct_member_7aem2to(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_parse_struct_member_7aem2to F;
    void * gm_l0; /* p */
    uint32_t gm_l1; /* start */
    gm_b16_8 gm_l2; /* attrs */
    gm_t9qkh0rsooc8d gm_l4; /* vis */
    uint8_t gm_l5; /* is_using */
    void * gm_l7; /* ty */
    void * gm_l8; /* default */
    GM_STACK_CHECK(&F, 2723u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        uint32_t t3 = gm_f_lo_fp82gt4(t2);
        gm_l1 = t3;
    }
    {
        void * t4 = gm_l0;
        gm_tt7ek1a8bopcl t5;
        gm_f_parse_attrs_imvfdlq((void *)&t5, (void *)NULL, cx, NULL, t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tt7ek1a8bopcl t6 = t5;
        if (((((uint8_t *)&t6))[0] != 0)) {
            gm_b0_1 t7 = GM_LD(gm_b0_1, (((uint8_t *)&t6) + 8));
            gm_t6gdobsbntbpm t8;
            memset(&t8, 0, sizeof t8);
            ((uint8_t *)&t8)[0] = 1;
            GM_ST(gm_b0_1, ((uint8_t *)&t8 + 8), t7);
            F.ret = t8;
            goto gm_fret;
        }
        gm_b16_8 t9 = GM_LD(gm_b16_8, (((uint8_t *)&t6) + 8));
        gm_l2 = t9;
    }
    {
        void * t11 = gm_l0;
        gm_t9qkh0rsooc8d t12;
        GM_ST(uint8_t, ((uint8_t *)&t12 + 0), ((uint8_t)29ULL));
        uint8_t t13 = gm_f_at_kw_p0r3sr9(t11, t12);
        if (!t13) goto ie10;
        {
            gm_b16_8 t16 = gm_l2;
            gm_b16_8 t17 = t16;
            void * t18 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t17)));
            uint64_t t19 = (uintptr_t)GM_LD(uintptr_t, ((uint8_t *)&t17) + 8);
            uint64_t t20 = (uintptr_t)0;
            gm_tcus0chk5r9uq t21;
            if (t20 < t19) {
            gm_tkacitepg5fk0 t22 = GM_LD(gm_tkacitepg5fk0, ((uint8_t *)t18 + t20 * 72));
            gm_tcus0chk5r9uq t23;
            GM_ST(uint8_t, (uint8_t *)&t23 + 73, 0);
            GM_ST(uint16_t, (uint8_t *)&t23 + 74, 0);
            GM_ST(uint32_t, (uint8_t *)&t23 + 76, 0);
            GM_ST(gm_tkacitepg5fk0, ((uint8_t *)&t23), t22);
            ((uint8_t *)&t23)[72] = 1;
                t21 = t23;
            } else {
            gm_tcus0chk5r9uq t24;
            memset(&t24, 0, sizeof t24);
                t21 = t24;
            }
            gm_tcus0chk5r9uq t25 = t21;
            if (!((((uint8_t *)&t25))[72] != 0)) goto ie15;
            gm_tkacitepg5fk0 t26 = GM_LD(gm_tkacitepg5fk0, ((uint8_t *)&t25));
            F.l3 = t26;
            {
                void * t28 = gm_l0;
                gm_tl874t2atnlh3 t29 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l3) + 56));
                gm_b16_8 t30;
                GM_ST(void *, (uint8_t *)&t30, (void *)(gm_s476hgsnn8633));
                GM_ST(uintptr_t, (uint8_t *)&t30 + 8, (uintptr_t)(46));
                gm_b0_1 t31 = gm_f_report_djvi2hf((void *)NULL, cx, NULL, t28, t29, t30);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix15;
ie15:;
ix15:;
        }
        {
            void * t32 = gm_l0;
            gm_f_parse_struct_when_4imfqc2((void *)&(*(gm_t6gdobsbntbpm *)gm_out), (void *)NULL, cx, NULL, t32);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
        }
        goto ix10;
ie10:;
ix10:;
    }
    {
        void * t33 = gm_l0;
        gm_tpvsmearkkqf7 t34 = gm_f_parse_visibility_j571nt0((void *)NULL, cx, NULL, t33);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tpvsmearkkqf7 t35 = t34;
        if (((((uint8_t *)&t35))[0] != 0)) {
            gm_b0_1 t36 = GM_LD(gm_b0_1, (((uint8_t *)&t35) + 1));
            gm_t6gdobsbntbpm t37;
            memset(&t37, 0, sizeof t37);
            ((uint8_t *)&t37)[0] = 1;
            GM_ST(gm_b0_1, ((uint8_t *)&t37 + 8), t36);
            F.ret = t37;
            goto gm_fret;
        }
        gm_t9qkh0rsooc8d t38 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&t35) + 1));
        gm_l4 = t38;
    }
    {
        void * t39 = gm_l0;
        gm_t9qkh0rsooc8d t40;
        GM_ST(uint8_t, ((uint8_t *)&t40 + 0), ((uint8_t)28ULL));
        uint8_t t41 = gm_f_eat_kw_tvv0v3e(t39, t40);
        gm_l5 = t41;
    }
    {
        gm_f_list_mho6jfm((void *)&F.l6, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t42 = (void *)((uint8_t *)&F.l6);
        void * t43 = gm_l0;
        gm_b16_8 t44;
        GM_ST(void *, (uint8_t *)&t44, (void *)(gm_stjafkt8gs4e7));
        GM_ST(uintptr_t, (uint8_t *)&t44 + 8, (uintptr_t)(12));
        gm_t4jn8fl3lmk5k t45;
        gm_f_parse_name_figjvbe((void *)&t45, (void *)NULL, cx, NULL, t43, t44);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t4jn8fl3lmk5k t46 = t45;
        if (((((uint8_t *)&t46))[0] != 0)) {
            gm_b0_1 t47 = GM_LD(gm_b0_1, (((uint8_t *)&t46) + 8));
            gm_t6gdobsbntbpm t48;
            memset(&t48, 0, sizeof t48);
            ((uint8_t *)&t48)[0] = 1;
            GM_ST(gm_b0_1, ((uint8_t *)&t48 + 8), t47);
            F.ret = t48;
            goto gm_fret;
        }
        gm_t08olq97rscn3 t49 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&t46) + 8));
        gm_tftb1odc739t5 t50 = gm_f_push_s5rtia7(t42, &t49);
        gm_tftb1odc739t5 t51 = t50;
        if (((((uint8_t *)&t51))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t51) + 1, gm_src8m8lg034q48k, 74, 5);
        gm_b0_1 t52 = GM_LD(gm_b0_1, (((uint8_t *)&t51) + 1));
    }
    {
        for (;;) {
            void * t54 = gm_l0;
            gm_t9qkh0rsooc8d t55;
            GM_ST(uint8_t, ((uint8_t *)&t55 + 0), ((uint8_t)57ULL));
            uint8_t t56 = gm_f_eat_qmock9g(t54, t55);
            if (!t56) goto lb53;
            {
                void * t59 = (void *)((uint8_t *)&F.l6);
                void * t60 = gm_l0;
                gm_b16_8 t61;
                GM_ST(void *, (uint8_t *)&t61, (void *)(gm_stjafkt8gs4e7));
                GM_ST(uintptr_t, (uint8_t *)&t61 + 8, (uintptr_t)(12));
                gm_t4jn8fl3lmk5k t62;
                gm_f_parse_name_figjvbe((void *)&t62, (void *)NULL, cx, NULL, t60, t61);
                if (gm_unw.kind) {
                    goto gm_ll57;
                }
                gm_t4jn8fl3lmk5k t63 = t62;
                if (((((uint8_t *)&t63))[0] != 0)) {
                    gm_b0_1 t64 = GM_LD(gm_b0_1, (((uint8_t *)&t63) + 8));
                    gm_t6gdobsbntbpm t65;
                    memset(&t65, 0, sizeof t65);
                    ((uint8_t *)&t65)[0] = 1;
                    GM_ST(gm_b0_1, ((uint8_t *)&t65 + 8), t64);
                    F.ret = t65;
                    goto gm_fret;
                }
                gm_t08olq97rscn3 t66 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&t63) + 8));
                gm_tftb1odc739t5 t67 = gm_f_push_s5rtia7(t59, &t66);
                gm_tftb1odc739t5 t68 = t67;
                if (((((uint8_t *)&t68))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t68) + 1, gm_src8m8lg034q48k, 75, 27);
                gm_b0_1 t69 = GM_LD(gm_b0_1, (((uint8_t *)&t68) + 1));
            }
lc53:;
        }
gm_ll57:;
        if (gm_unw.frame == &F.h && gm_unw.label == 0) {
            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb53; }
            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc53; }
        }
        goto gm_lad;
lb53:;
    }
    {
        {
            void * t71 = gm_l0;
            gm_t9qkh0rsooc8d t72;
            GM_ST(uint8_t, ((uint8_t *)&t72 + 0), ((uint8_t)56ULL));
            gm_b16_8 t73;
            GM_ST(void *, (uint8_t *)&t73, (void *)(gm_sfuh3dki1la7r));
            GM_ST(uintptr_t, (uint8_t *)&t73 + 8, (uintptr_t)(31));
            gm_t0u0j6voh3loe t74 = gm_f_expect_ijgfrs1((void *)NULL, cx, NULL, t71, t72, t73);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t0u0j6voh3loe t75 = t74;
            if (((((uint8_t *)&t75))[0] != 0)) {
                gm_b0_1 t76 = GM_LD(gm_b0_1, (((uint8_t *)&t75) + 4));
                gm_t6gdobsbntbpm t77;
                memset(&t77, 0, sizeof t77);
                ((uint8_t *)&t77)[0] = 1;
                GM_ST(gm_b0_1, ((uint8_t *)&t77 + 8), t76);
                F.ret = t77;
                goto gm_fret;
            }
            gm_tl874t2atnlh3 t78 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&t75) + 4));
        }
    }
    {
        void * t79 = gm_l0;
        gm_to1cssm8dfiv5 t80;
        gm_f_parse_type_2m93dtq((void *)&t80, (void *)NULL, cx, NULL, t79);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_to1cssm8dfiv5 t81 = t80;
        if (((((uint8_t *)&t81))[0] != 0)) {
            gm_b0_1 t82 = GM_LD(gm_b0_1, (((uint8_t *)&t81) + 8));
            gm_t6gdobsbntbpm t83;
            memset(&t83, 0, sizeof t83);
            ((uint8_t *)&t83)[0] = 1;
            GM_ST(gm_b0_1, ((uint8_t *)&t83 + 8), t82);
            F.ret = t83;
            goto gm_fret;
        }
        gm_tc3oht4ksth9d t84 = GM_LD(gm_tc3oht4ksth9d, (((uint8_t *)&t81) + 8));
        void * t85 = gm_f_box_j4urmed((void *)NULL, cx, NULL, &t84);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l7 = t85;
    }
    {
        gm_l8 = ((void *)0);
    }
    {
        void * t87 = gm_l0;
        gm_t9qkh0rsooc8d t88;
        GM_ST(uint8_t, ((uint8_t *)&t88 + 0), ((uint8_t)22ULL));
        uint8_t t89 = gm_f_eat_qmock9g(t87, t88);
        if (!t89) goto ie86;
        {
            void * t91 = gm_l0;
            gm_to1cssm8dfiv5 t92;
            gm_f_parse_expr_bkb2n6e((void *)&t92, (void *)NULL, cx, NULL, t91);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_to1cssm8dfiv5 t93 = t92;
            if (((((uint8_t *)&t93))[0] != 0)) {
                gm_b0_1 t94 = GM_LD(gm_b0_1, (((uint8_t *)&t93) + 8));
                gm_t6gdobsbntbpm t95;
                memset(&t95, 0, sizeof t95);
                ((uint8_t *)&t95)[0] = 1;
                GM_ST(gm_b0_1, ((uint8_t *)&t95 + 8), t94);
                F.ret = t95;
                goto gm_fret;
            }
            gm_tc3oht4ksth9d t96 = GM_LD(gm_tc3oht4ksth9d, (((uint8_t *)&t93) + 8));
            void * t97 = gm_f_box_j4urmed((void *)NULL, cx, NULL, &t96);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l8 = t97;
        }
        goto ix86;
ie86:;
ix86:;
    }
    {
        gm_t24301fas1b2a t98;
        memset((uint8_t *)&t98 + 18, 0, 6);
        memset((uint8_t *)&t98 + 68, 0, 4);
        gm_b16_8 t99 = gm_l2;
        GM_ST(gm_b16_8, (uint8_t *)&t98 + 0, t99);
        gm_t9qkh0rsooc8d t100 = gm_l4;
        GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&t98 + 16, t100);
        uint8_t t101 = gm_l5;
        GM_ST(uint8_t, (uint8_t *)&t98 + 17, t101);
        gm_b16_8 t102 = GM_LD(gm_b16_8, (((uint8_t *)&F.l6) + 0));
        GM_ST(gm_b16_8, (uint8_t *)&t98 + 24, t102);
        void * t103 = gm_l7;
        GM_ST(void *, (uint8_t *)&t98 + 40, t103);
        void * t104 = gm_l8;
        GM_ST(void *, (uint8_t *)&t98 + 48, t104);
        void * t105 = gm_l0;
        uint32_t t106 = gm_l1;
        gm_tl874t2atnlh3 t107 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_from_1sqap0o(t105, t106));
        GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t98 + 56, t107);
        gm_tfc3oljlofib6 t108;
        GM_ST(uint8_t, (uint8_t *)&t108 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t108 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t108 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t108 + 0), ((uint8_t)0ULL));
        GM_ST(gm_t24301fas1b2a, ((uint8_t *)&t108 + 8), t98);
        gm_t6gdobsbntbpm t109;
        GM_ST(uint8_t, (uint8_t *)&t109 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t109 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t109 + 4, 0);
        ((uint8_t *)&t109)[0] = 0;
        GM_ST(gm_tfc3oljlofib6, ((uint8_t *)&t109 + 8), t108);
        gm_t6gdobsbntbpm t110 = t109;
        uint8_t t111 = ((((uint8_t *)&t110))[0] != 0);
        F.ret = t109;
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
extern __attribute__((aligned(4))) gm_b16_8 gm_f_parse_struct_members_rj26glk(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) void gm_f_parse_struct_when_4imfqc2(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_parse_struct_when_4imfqc2 F;
    void * gm_l0; /* p */
    uint32_t gm_l1; /* start */
    void * gm_l2; /* cond */
    gm_b16_8 gm_l3; /* then */
    gm_th2lqbo77pvsc gm_l4; /* else_ */
    GM_STACK_CHECK(&F, 1301u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        uint32_t t3 = gm_f_lo_fp82gt4(t2);
        gm_l1 = t3;
    }
    {
        {
            void * t5 = gm_l0;
            gm_tm4pc0emhrv1g t6;
            gm_f_bump_q2i40re((void *)&t6, t5);
        }
    }
    {
        void * t7 = gm_l0;
        gm_to1cssm8dfiv5 t8;
        gm_f_parse_head_expr_6sofdhn((void *)&t8, (void *)NULL, cx, NULL, t7);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_to1cssm8dfiv5 t9 = t8;
        if (((((uint8_t *)&t9))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 8));
            gm_t6gdobsbntbpm t11;
            memset(&t11, 0, sizeof t11);
            ((uint8_t *)&t11)[0] = 1;
            GM_ST(gm_b0_1, ((uint8_t *)&t11 + 8), t10);
            F.ret = t11;
            goto gm_fret;
        }
        gm_tc3oht4ksth9d t12 = GM_LD(gm_tc3oht4ksth9d, (((uint8_t *)&t9) + 8));
        void * t13 = gm_f_box_j4urmed((void *)NULL, cx, NULL, &t12);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l2 = t13;
    }
    {
        {
            void * t15 = gm_l0;
            gm_t9qkh0rsooc8d t16;
            GM_ST(uint8_t, ((uint8_t *)&t16 + 0), ((uint8_t)65ULL));
            gm_b16_8 t17;
            GM_ST(void *, (uint8_t *)&t17, (void *)(gm_se7h6nse0tc33));
            GM_ST(uintptr_t, (uint8_t *)&t17 + 8, (uintptr_t)(25));
            gm_t0u0j6voh3loe t18 = gm_f_expect_ijgfrs1((void *)NULL, cx, NULL, t15, t16, t17);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t0u0j6voh3loe t19 = t18;
            if (((((uint8_t *)&t19))[0] != 0)) {
                gm_b0_1 t20 = GM_LD(gm_b0_1, (((uint8_t *)&t19) + 4));
                gm_t6gdobsbntbpm t21;
                memset(&t21, 0, sizeof t21);
                ((uint8_t *)&t21)[0] = 1;
                GM_ST(gm_b0_1, ((uint8_t *)&t21 + 8), t20);
                F.ret = t21;
                goto gm_fret;
            }
            gm_tl874t2atnlh3 t22 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&t19) + 4));
        }
    }
    {
        void * t23 = gm_l0;
        gm_b16_8 t24 = gm_f_parse_struct_members_rj26glk((void *)NULL, cx, NULL, t23);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l3 = t24;
    }
    {
        gm_th2lqbo77pvsc t25;
        memset(&t25, 0, sizeof t25);
        gm_l4 = t25;
    }
    {
        void * t27 = gm_l0;
        gm_t9qkh0rsooc8d t28;
        GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)7ULL));
        uint8_t t29 = gm_f_eat_kw_tvv0v3e(t27, t28);
        if (!t29) goto ie26;
        {
            {
                void * t32 = gm_l0;
                gm_t9qkh0rsooc8d t33;
                GM_ST(uint8_t, ((uint8_t *)&t33 + 0), ((uint8_t)65ULL));
                gm_b16_8 t34;
                GM_ST(void *, (uint8_t *)&t34, (void *)(gm_skusvrbuvvp37));
                GM_ST(uintptr_t, (uint8_t *)&t34 + 8, (uintptr_t)(12));
                gm_t0u0j6voh3loe t35 = gm_f_expect_ijgfrs1((void *)NULL, cx, NULL, t32, t33, t34);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_t0u0j6voh3loe t36 = t35;
                if (((((uint8_t *)&t36))[0] != 0)) {
                    gm_b0_1 t37 = GM_LD(gm_b0_1, (((uint8_t *)&t36) + 4));
                    gm_t6gdobsbntbpm t38;
                    memset(&t38, 0, sizeof t38);
                    ((uint8_t *)&t38)[0] = 1;
                    GM_ST(gm_b0_1, ((uint8_t *)&t38 + 8), t37);
                    F.ret = t38;
                    goto gm_fret;
                }
                gm_tl874t2atnlh3 t39 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&t36) + 4));
            }
        }
        {
            void * t40 = gm_l0;
            gm_b16_8 t41 = gm_f_parse_struct_members_rj26glk((void *)NULL, cx, NULL, t40);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_th2lqbo77pvsc t42;
            GM_ST(uint8_t, (uint8_t *)&t42 + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&t42 + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&t42 + 20, 0);
            GM_ST(gm_b16_8, ((uint8_t *)&t42), t41);
            ((uint8_t *)&t42)[16] = 1;
            gm_l4 = t42;
        }
        goto ix26;
ie26:;
ix26:;
    }
    {
        gm_t7bjbmukv26e7 t43;
        memset((uint8_t *)&t43 + 60, 0, 4);
        void * t44 = gm_l2;
        GM_ST(void *, (uint8_t *)&t43 + 0, t44);
        gm_b16_8 t45 = gm_l3;
        GM_ST(gm_b16_8, (uint8_t *)&t43 + 8, t45);
        memcpy((uint8_t *)&t43 + 24, ((uint8_t *)&gm_l4), 24);
        void * t46 = gm_l0;
        uint32_t t47 = gm_l1;
        gm_tl874t2atnlh3 t48 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_from_1sqap0o(t46, t47));
        GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t43 + 48, t48);
        gm_tfc3oljlofib6 t49;
        GM_ST(uint8_t, (uint8_t *)&t49 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t49 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t49 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t49 + 72, 0);
        GM_ST(uint8_t, ((uint8_t *)&t49 + 0), ((uint8_t)1ULL));
        GM_ST(gm_t7bjbmukv26e7, ((uint8_t *)&t49 + 8), t43);
        gm_t6gdobsbntbpm t50;
        GM_ST(uint8_t, (uint8_t *)&t50 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t50 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t50 + 4, 0);
        ((uint8_t *)&t50)[0] = 0;
        GM_ST(gm_tfc3oljlofib6, ((uint8_t *)&t50 + 8), t49);
        gm_t6gdobsbntbpm t51 = t50;
        uint8_t t52 = ((((uint8_t *)&t51))[0] != 0);
        F.ret = t50;
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
__attribute__((aligned(4))) void gm_f_parse_union_lit_39ap2o5(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_parse_union_lit_39ap2o5 F;
    void * gm_l0; /* p */
    uint32_t gm_l1; /* start */
    void * gm_l2; /* tag_type */
    gm_th2lqbo77pvsc gm_l3; /* params */
    gm_b16_8 gm_l4; /* where_ */
    gm_b16_8 gm_l5; /* ms */
    gm_tl874t2atnlh3 gm_l6; /* whole */
    GM_STACK_CHECK(&F, 2047u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sapag1m4a00vt));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(15));
        gm_t9qkh0rsooc8d t4 = gm_f_deny_in_head_06d9o22((void *)NULL, cx, NULL, t2, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t9qkh0rsooc8d t5 = t4;
        if (((((uint8_t *)&t5))[0] != 0)) {
            gm_b0_1 t6 = GM_LD(gm_b0_1, (((uint8_t *)&t5) + 1));
            gm_to1cssm8dfiv5 t7;
            memset(&t7, 0, sizeof t7);
            ((uint8_t *)&t7)[0] = 1;
            GM_ST(gm_b0_1, ((uint8_t *)&t7 + 8), t6);
            F.ret = t7;
            goto gm_fret;
        }
        gm_b0_1 t8 = GM_LD(gm_b0_1, (((uint8_t *)&t5) + 1));
    }
    {
        void * t9 = gm_l0;
        uint32_t t10 = gm_f_lo_fp82gt4(t9);
        gm_l1 = t10;
    }
    {
        {
            void * t12 = gm_l0;
            gm_tm4pc0emhrv1g t13;
            gm_f_bump_q2i40re((void *)&t13, t12);
        }
    }
    {
        gm_l2 = ((void *)0);
    }
    {
        gm_th2lqbo77pvsc t14;
        memset(&t14, 0, sizeof t14);
        gm_l3 = t14;
    }
    {
        void * t16 = gm_l0;
        gm_t9qkh0rsooc8d t17;
        GM_ST(uint8_t, ((uint8_t *)&t17 + 0), ((uint8_t)61ULL));
        uint8_t t18 = gm_f_at_54906rm(t16, t17);
        if (!t18) goto ie15;
        {
            void * t21 = gm_l0;
            uint8_t t22 = gm_f_paren_is_param_list_0bbahvv(t21);
            if (!t22) goto ie20;
            {
                void * t24 = gm_l0;
                gm_tt7ek1a8bopcl t25;
                gm_f_parse_decl_params_ftnplpo((void *)&t25, (void *)NULL, cx, NULL, t24);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tt7ek1a8bopcl t26 = t25;
                if (((((uint8_t *)&t26))[0] != 0)) {
                    gm_b0_1 t27 = GM_LD(gm_b0_1, (((uint8_t *)&t26) + 8));
                    gm_to1cssm8dfiv5 t28;
                    memset(&t28, 0, sizeof t28);
                    ((uint8_t *)&t28)[0] = 1;
                    GM_ST(gm_b0_1, ((uint8_t *)&t28 + 8), t27);
                    F.ret = t28;
                    goto gm_fret;
                }
                gm_b16_8 t29 = GM_LD(gm_b16_8, (((uint8_t *)&t26) + 8));
                gm_th2lqbo77pvsc t30;
                GM_ST(uint8_t, (uint8_t *)&t30 + 17, 0);
                GM_ST(uint16_t, (uint8_t *)&t30 + 18, 0);
                GM_ST(uint32_t, (uint8_t *)&t30 + 20, 0);
                GM_ST(gm_b16_8, ((uint8_t *)&t30), t29);
                ((uint8_t *)&t30)[16] = 1;
                gm_l3 = t30;
            }
            goto ix20;
ie20:;
            {
                {
                    void * t33 = gm_l0;
                    gm_tm4pc0emhrv1g t34;
                    gm_f_bump_q2i40re((void *)&t34, t33);
                }
            }
            {
                void * t35 = gm_l0;
                gm_toh0ootodhjue t36;
                GM_ST(uint8_t, ((uint8_t *)&t36 + 0), ((uint8_t)0));
                GM_ST(uint8_t, ((uint8_t *)&t36 + 1), ((uint8_t)0));
                GM_ST(uint8_t, ((uint8_t *)&t36 + 2), ((uint8_t)0));
                gm_to1cssm8dfiv5 t37;
                gm_f_parse_type_in_bvi7uav((void *)&t37, (void *)NULL, cx, NULL, t35, t36);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_to1cssm8dfiv5 t38 = t37;
                if (((((uint8_t *)&t38))[0] != 0)) {
                    gm_b0_1 t39 = GM_LD(gm_b0_1, (((uint8_t *)&t38) + 8));
                    gm_to1cssm8dfiv5 t40;
                    memset(&t40, 0, sizeof t40);
                    ((uint8_t *)&t40)[0] = 1;
                    GM_ST(gm_b0_1, ((uint8_t *)&t40 + 8), t39);
                    F.ret = t40;
                    goto gm_fret;
                }
                gm_tc3oht4ksth9d t41 = GM_LD(gm_tc3oht4ksth9d, (((uint8_t *)&t38) + 8));
                void * t42 = gm_f_box_j4urmed((void *)NULL, cx, NULL, &t41);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l2 = t42;
            }
            {
                {
                    void * t44 = gm_l0;
                    gm_t9qkh0rsooc8d t45;
                    GM_ST(uint8_t, ((uint8_t *)&t45 + 0), ((uint8_t)62ULL));
                    gm_b16_8 t46;
                    GM_ST(void *, (uint8_t *)&t46, (void *)(gm_s122r7j2imgmj));
                    GM_ST(uintptr_t, (uint8_t *)&t46 + 8, (uintptr_t)(21));
                    gm_t0u0j6voh3loe t47 = gm_f_expect_ijgfrs1((void *)NULL, cx, NULL, t44, t45, t46);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_t0u0j6voh3loe t48 = t47;
                    if (((((uint8_t *)&t48))[0] != 0)) {
                        gm_b0_1 t49 = GM_LD(gm_b0_1, (((uint8_t *)&t48) + 4));
                        gm_to1cssm8dfiv5 t50;
                        memset(&t50, 0, sizeof t50);
                        ((uint8_t *)&t50)[0] = 1;
                        GM_ST(gm_b0_1, ((uint8_t *)&t50 + 8), t49);
                        F.ret = t50;
                        goto gm_fret;
                    }
                    gm_tl874t2atnlh3 t51 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&t48) + 4));
                }
            }
ix20:;
        }
        goto ix15;
ie15:;
ix15:;
    }
    {
        void * t52 = gm_l0;
        gm_tt7ek1a8bopcl t53;
        gm_f_parse_where_opt_a5p2ja6((void *)&t53, (void *)NULL, cx, NULL, t52);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tt7ek1a8bopcl t54 = t53;
        if (((((uint8_t *)&t54))[0] != 0)) {
            gm_b0_1 t55 = GM_LD(gm_b0_1, (((uint8_t *)&t54) + 8));
            gm_to1cssm8dfiv5 t56;
            memset(&t56, 0, sizeof t56);
            ((uint8_t *)&t56)[0] = 1;
            GM_ST(gm_b0_1, ((uint8_t *)&t56 + 8), t55);
            F.ret = t56;
            goto gm_fret;
        }
        gm_b16_8 t57 = GM_LD(gm_b16_8, (((uint8_t *)&t54) + 8));
        gm_l4 = t57;
    }
    {
        {
            void * t59 = gm_l0;
            gm_t9qkh0rsooc8d t60;
            GM_ST(uint8_t, ((uint8_t *)&t60 + 0), ((uint8_t)65ULL));
            gm_b16_8 t61;
            GM_ST(void *, (uint8_t *)&t61, (void *)(gm_s8qn6r2vquv2d));
            GM_ST(uintptr_t, (uint8_t *)&t61 + 8, (uintptr_t)(23));
            gm_t0u0j6voh3loe t62 = gm_f_expect_ijgfrs1((void *)NULL, cx, NULL, t59, t60, t61);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t0u0j6voh3loe t63 = t62;
            if (((((uint8_t *)&t63))[0] != 0)) {
                gm_b0_1 t64 = GM_LD(gm_b0_1, (((uint8_t *)&t63) + 4));
                gm_to1cssm8dfiv5 t65;
                memset(&t65, 0, sizeof t65);
                ((uint8_t *)&t65)[0] = 1;
                GM_ST(gm_b0_1, ((uint8_t *)&t65 + 8), t64);
                F.ret = t65;
                goto gm_fret;
            }
            gm_tl874t2atnlh3 t66 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&t63) + 4));
        }
    }
    {
        void * t67 = gm_l0;
        gm_b16_8 t68 = gm_f_parse_union_members_g8ptfng((void *)NULL, cx, NULL, t67);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l5 = t68;
    }
    {
        void * t69 = gm_l0;
        uint32_t t70 = gm_l1;
        gm_tl874t2atnlh3 t71 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_from_1sqap0o(t69, t70));
        gm_l6 = t71;
    }
    {
        void * t72 = gm_l0;
        gm_tg8732ngbnf0p t73;
        memset((uint8_t *)&t73 + 76, 0, 4);
        void * t74 = gm_l2;
        GM_ST(void *, (uint8_t *)&t73 + 0, t74);
        memcpy((uint8_t *)&t73 + 8, ((uint8_t *)&gm_l3), 24);
        gm_b16_8 t75 = gm_l4;
        GM_ST(gm_b16_8, (uint8_t *)&t73 + 32, t75);
        gm_b16_8 t76 = gm_l5;
        GM_ST(gm_b16_8, (uint8_t *)&t73 + 48, t76);
        gm_tl874t2atnlh3 t77 = gm_l6;
        GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t73 + 64, t77);
        gm_tq7s98ihonkju t78 = gm_f_new_jpqes5m((void *)NULL, cx, NULL, &t73, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kt1fbj7k4jp)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tq7s98ihonkju t79 = t78;
        if (((((uint8_t *)&t79))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t79) + 8, gm_src8m8lg034q48k, 139, 29);
        void * t80 = GM_LD(void *, (((uint8_t *)&t79) + 8));
        gm_t2gtbh39ptekd t81;
        memset(&t81, 0, sizeof t81);
        GM_ST(uint8_t, ((uint8_t *)&t81 + 0), ((uint8_t)33ULL));
        GM_ST(void *, ((uint8_t *)&t81 + 8), t80);
        gm_tl874t2atnlh3 t82 = gm_l6;
        gm_tc3oht4ksth9d t83;
        gm_f_mk_expr_53qj7nd((void *)&t83, t72, &t81, GM_CONV(gm_cseahvg8v7p0vs, t82));
        gm_to1cssm8dfiv5 t84;
        GM_ST(uint8_t, (uint8_t *)&t84 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t84 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t84 + 4, 0);
        ((uint8_t *)&t84)[0] = 0;
        GM_ST(gm_tc3oht4ksth9d, ((uint8_t *)&t84 + 8), t83);
        gm_to1cssm8dfiv5 t85 = t84;
        uint8_t t86 = ((((uint8_t *)&t85))[0] != 0);
        F.ret = t84;
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
__attribute__((aligned(4))) void gm_f_parse_union_member_dba1nar(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_parse_union_member_dba1nar F;
    void * gm_l0; /* p */
    uint32_t gm_l1; /* start */
    void * gm_l2; /* cond */
    gm_b16_8 gm_l3; /* then */
    gm_th2lqbo77pvsc gm_l4; /* else_ */
    gm_t08olq97rscn3 gm_l5; /* name */
    void * gm_l6; /* payload */
    void * gm_l7; /* value */
    GM_STACK_CHECK(&F, 2580u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        uint32_t t3 = gm_f_lo_fp82gt4(t2);
        gm_l1 = t3;
    }
    {
        void * t5 = gm_l0;
        gm_t9qkh0rsooc8d t6;
        GM_ST(uint8_t, ((uint8_t *)&t6 + 0), ((uint8_t)29ULL));
        uint8_t t7 = gm_f_eat_kw_tvv0v3e(t5, t6);
        if (!t7) goto ie4;
        {
            void * t9 = gm_l0;
            gm_to1cssm8dfiv5 t10;
            gm_f_parse_head_expr_6sofdhn((void *)&t10, (void *)NULL, cx, NULL, t9);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_to1cssm8dfiv5 t11 = t10;
            if (((((uint8_t *)&t11))[0] != 0)) {
                gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t11) + 8));
                gm_tup3hijlfo1vq t13;
                memset(&t13, 0, sizeof t13);
                ((uint8_t *)&t13)[0] = 1;
                GM_ST(gm_b0_1, ((uint8_t *)&t13 + 8), t12);
                F.ret = t13;
                goto gm_fret;
            }
            gm_tc3oht4ksth9d t14 = GM_LD(gm_tc3oht4ksth9d, (((uint8_t *)&t11) + 8));
            void * t15 = gm_f_box_j4urmed((void *)NULL, cx, NULL, &t14);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l2 = t15;
        }
        {
            {
                void * t17 = gm_l0;
                gm_t9qkh0rsooc8d t18;
                GM_ST(uint8_t, ((uint8_t *)&t18 + 0), ((uint8_t)65ULL));
                gm_b16_8 t19;
                GM_ST(void *, (uint8_t *)&t19, (void *)(gm_se7h6nse0tc33));
                GM_ST(uintptr_t, (uint8_t *)&t19 + 8, (uintptr_t)(25));
                gm_t0u0j6voh3loe t20 = gm_f_expect_ijgfrs1((void *)NULL, cx, NULL, t17, t18, t19);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_t0u0j6voh3loe t21 = t20;
                if (((((uint8_t *)&t21))[0] != 0)) {
                    gm_b0_1 t22 = GM_LD(gm_b0_1, (((uint8_t *)&t21) + 4));
                    gm_tup3hijlfo1vq t23;
                    memset(&t23, 0, sizeof t23);
                    ((uint8_t *)&t23)[0] = 1;
                    GM_ST(gm_b0_1, ((uint8_t *)&t23 + 8), t22);
                    F.ret = t23;
                    goto gm_fret;
                }
                gm_tl874t2atnlh3 t24 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&t21) + 4));
            }
        }
        {
            void * t25 = gm_l0;
            gm_b16_8 t26 = gm_f_parse_union_members_g8ptfng((void *)NULL, cx, NULL, t25);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l3 = t26;
        }
        {
            gm_th2lqbo77pvsc t27;
            memset(&t27, 0, sizeof t27);
            gm_l4 = t27;
        }
        {
            void * t29 = gm_l0;
            gm_t9qkh0rsooc8d t30;
            GM_ST(uint8_t, ((uint8_t *)&t30 + 0), ((uint8_t)7ULL));
            uint8_t t31 = gm_f_eat_kw_tvv0v3e(t29, t30);
            if (!t31) goto ie28;
            {
                {
                    void * t34 = gm_l0;
                    gm_t9qkh0rsooc8d t35;
                    GM_ST(uint8_t, ((uint8_t *)&t35 + 0), ((uint8_t)65ULL));
                    gm_b16_8 t36;
                    GM_ST(void *, (uint8_t *)&t36, (void *)(gm_skusvrbuvvp37));
                    GM_ST(uintptr_t, (uint8_t *)&t36 + 8, (uintptr_t)(12));
                    gm_t0u0j6voh3loe t37 = gm_f_expect_ijgfrs1((void *)NULL, cx, NULL, t34, t35, t36);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_t0u0j6voh3loe t38 = t37;
                    if (((((uint8_t *)&t38))[0] != 0)) {
                        gm_b0_1 t39 = GM_LD(gm_b0_1, (((uint8_t *)&t38) + 4));
                        gm_tup3hijlfo1vq t40;
                        memset(&t40, 0, sizeof t40);
                        ((uint8_t *)&t40)[0] = 1;
                        GM_ST(gm_b0_1, ((uint8_t *)&t40 + 8), t39);
                        F.ret = t40;
                        goto gm_fret;
                    }
                    gm_tl874t2atnlh3 t41 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&t38) + 4));
                }
            }
            {
                void * t42 = gm_l0;
                gm_b16_8 t43 = gm_f_parse_union_members_g8ptfng((void *)NULL, cx, NULL, t42);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_th2lqbo77pvsc t44;
                GM_ST(uint8_t, (uint8_t *)&t44 + 17, 0);
                GM_ST(uint16_t, (uint8_t *)&t44 + 18, 0);
                GM_ST(uint32_t, (uint8_t *)&t44 + 20, 0);
                GM_ST(gm_b16_8, ((uint8_t *)&t44), t43);
                ((uint8_t *)&t44)[16] = 1;
                gm_l4 = t44;
            }
            goto ix28;
ie28:;
ix28:;
        }
        {
            gm_t7bjbmukv26e7 t45;
            memset((uint8_t *)&t45 + 60, 0, 4);
            void * t46 = gm_l2;
            GM_ST(void *, (uint8_t *)&t45 + 0, t46);
            gm_b16_8 t47 = gm_l3;
            GM_ST(gm_b16_8, (uint8_t *)&t45 + 8, t47);
            memcpy((uint8_t *)&t45 + 24, ((uint8_t *)&gm_l4), 24);
            void * t48 = gm_l0;
            uint32_t t49 = gm_l1;
            gm_tl874t2atnlh3 t50 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_from_1sqap0o(t48, t49));
            GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t45 + 48, t50);
            gm_t03a3imodqi32 t51;
            GM_ST(uint8_t, (uint8_t *)&t51 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t51 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t51 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t51 + 0), ((uint8_t)1ULL));
            GM_ST(gm_t7bjbmukv26e7, ((uint8_t *)&t51 + 8), t45);
            gm_tup3hijlfo1vq t52;
            GM_ST(uint8_t, (uint8_t *)&t52 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t52 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t52 + 4, 0);
            ((uint8_t *)&t52)[0] = 0;
            GM_ST(gm_t03a3imodqi32, ((uint8_t *)&t52 + 8), t51);
            gm_tup3hijlfo1vq t53 = t52;
            uint8_t t54 = ((((uint8_t *)&t53))[0] != 0);
            F.ret = t52;
            goto gm_fret;
        }
        goto ix4;
ie4:;
ix4:;
    }
    {
        void * t55 = gm_l0;
        gm_b16_8 t56;
        GM_ST(void *, (uint8_t *)&t56, (void *)(gm_srk1d80eamfhd));
        GM_ST(uintptr_t, (uint8_t *)&t56 + 8, (uintptr_t)(14));
        gm_t4jn8fl3lmk5k t57;
        gm_f_parse_decl_ident_31rb1id((void *)&t57, (void *)NULL, cx, NULL, t55, t56);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t4jn8fl3lmk5k t58 = t57;
        if (((((uint8_t *)&t58))[0] != 0)) {
            gm_b0_1 t59 = GM_LD(gm_b0_1, (((uint8_t *)&t58) + 8));
            gm_tup3hijlfo1vq t60;
            memset(&t60, 0, sizeof t60);
            ((uint8_t *)&t60)[0] = 1;
            GM_ST(gm_b0_1, ((uint8_t *)&t60 + 8), t59);
            F.ret = t60;
            goto gm_fret;
        }
        gm_t08olq97rscn3 t61 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&t58) + 8));
        gm_l5 = t61;
    }
    {
        gm_l6 = ((void *)0);
    }
    {
        void * t63 = gm_l0;
        gm_t9qkh0rsooc8d t64;
        GM_ST(uint8_t, ((uint8_t *)&t64 + 0), ((uint8_t)56ULL));
        uint8_t t65 = gm_f_eat_qmock9g(t63, t64);
        if (!t65) goto ie62;
        {
            void * t67 = gm_l0;
            gm_to1cssm8dfiv5 t68;
            gm_f_parse_type_2m93dtq((void *)&t68, (void *)NULL, cx, NULL, t67);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_to1cssm8dfiv5 t69 = t68;
            if (((((uint8_t *)&t69))[0] != 0)) {
                gm_b0_1 t70 = GM_LD(gm_b0_1, (((uint8_t *)&t69) + 8));
                gm_tup3hijlfo1vq t71;
                memset(&t71, 0, sizeof t71);
                ((uint8_t *)&t71)[0] = 1;
                GM_ST(gm_b0_1, ((uint8_t *)&t71 + 8), t70);
                F.ret = t71;
                goto gm_fret;
            }
            gm_tc3oht4ksth9d t72 = GM_LD(gm_tc3oht4ksth9d, (((uint8_t *)&t69) + 8));
            void * t73 = gm_f_box_j4urmed((void *)NULL, cx, NULL, &t72);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l6 = t73;
        }
        goto ix62;
ie62:;
ix62:;
    }
    {
        gm_l7 = ((void *)0);
    }
    {
        void * t75 = gm_l0;
        gm_t9qkh0rsooc8d t76;
        GM_ST(uint8_t, ((uint8_t *)&t76 + 0), ((uint8_t)22ULL));
        uint8_t t77 = gm_f_eat_qmock9g(t75, t76);
        if (!t77) goto ie74;
        {
            void * t79 = gm_l0;
            gm_to1cssm8dfiv5 t80;
            gm_f_parse_expr_bkb2n6e((void *)&t80, (void *)NULL, cx, NULL, t79);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_to1cssm8dfiv5 t81 = t80;
            if (((((uint8_t *)&t81))[0] != 0)) {
                gm_b0_1 t82 = GM_LD(gm_b0_1, (((uint8_t *)&t81) + 8));
                gm_tup3hijlfo1vq t83;
                memset(&t83, 0, sizeof t83);
                ((uint8_t *)&t83)[0] = 1;
                GM_ST(gm_b0_1, ((uint8_t *)&t83 + 8), t82);
                F.ret = t83;
                goto gm_fret;
            }
            gm_tc3oht4ksth9d t84 = GM_LD(gm_tc3oht4ksth9d, (((uint8_t *)&t81) + 8));
            void * t85 = gm_f_box_j4urmed((void *)NULL, cx, NULL, &t84);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l7 = t85;
        }
        goto ix74;
ie74:;
ix74:;
    }
    {
        gm_tnljbma3s3h16 t86;
        memset((uint8_t *)&t86 + 60, 0, 4);
        memcpy((uint8_t *)&t86 + 0, ((uint8_t *)&gm_l5), 32);
        void * t87 = gm_l6;
        GM_ST(void *, (uint8_t *)&t86 + 32, t87);
        void * t88 = gm_l7;
        GM_ST(void *, (uint8_t *)&t86 + 40, t88);
        void * t89 = gm_l0;
        uint32_t t90 = gm_l1;
        gm_tl874t2atnlh3 t91 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_from_1sqap0o(t89, t90));
        GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t86 + 48, t91);
        gm_t03a3imodqi32 t92;
        GM_ST(uint8_t, (uint8_t *)&t92 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t92 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t92 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t92 + 0), ((uint8_t)0ULL));
        GM_ST(gm_tnljbma3s3h16, ((uint8_t *)&t92 + 8), t86);
        gm_tup3hijlfo1vq t93;
        GM_ST(uint8_t, (uint8_t *)&t93 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t93 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t93 + 4, 0);
        ((uint8_t *)&t93)[0] = 0;
        GM_ST(gm_t03a3imodqi32, ((uint8_t *)&t93 + 8), t92);
        gm_tup3hijlfo1vq t94 = t93;
        uint8_t t95 = ((((uint8_t *)&t94))[0] != 0);
        F.ret = t93;
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
extern __attribute__((aligned(4))) gm_b16_8 gm_f_parse_union_members_g8ptfng(void *env, gm_node *cx, gm_node *inst, void * a0);
