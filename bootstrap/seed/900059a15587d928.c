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
typedef struct { uint8_t f0; uint8_t p1[3]; union { uint32_t v0; uint32_t v1; } f1; } gm_tj2m9dbh1ekv6;
typedef struct { uint8_t f0; uint8_t p1[1]; uint16_t f1; } gm_tjcj4j2gbh6c8;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { void * f0; void * f1; uint8_t f2; uint8_t pe[7]; } gm_tlcl4u558l6p5;
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
typedef struct { gm_tj2m9dbh1ekv6 f0; uint8_t f1; uint8_t pe[3]; } gm_t46lmulm8gaj3;
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
_Static_assert(sizeof(gm_tj2m9dbh1ekv6) == 8 && _Alignof(gm_tj2m9dbh1ekv6) == 4 && offsetof(gm_tj2m9dbh1ekv6, f0) == 0 && offsetof(gm_tj2m9dbh1ekv6, f1) == 4, "layout of `IntSuffix`");
typedef struct { gm_b16_8 f0; gm_tf5srh4mp4b3i f1; } gm_tj64ui6kruni6;
_Static_assert(sizeof(gm_tjcj4j2gbh6c8) == 4 && _Alignof(gm_tjcj4j2gbh6c8) == 2 && offsetof(gm_tjcj4j2gbh6c8, f0) == 0 && offsetof(gm_tjcj4j2gbh6c8, f1) == 2, "layout of `(signed: bool, bits: u16)`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_tf5srh4mp4b3i f1; uint8_t f2; uint8_t pe[7]; } gm_tjpqvfd72o62u;
typedef struct { void * f0; gm_b16_8 f1; } gm_tkaapcii86udk;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t f1; uint8_t pe[3]; } gm_tkn42u7na3t81;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_tlcl4u558l6p5) == 24 && _Alignof(gm_tlcl4u558l6p5) == 8 && offsetof(gm_tlcl4u558l6p5, f0) == 0 && offsetof(gm_tlcl4u558l6p5, f1) == 8 && offsetof(gm_tlcl4u558l6p5, f2) == 16, "layout of `(lo: ^Expr, hi: ^Expr, inclusive: bool)`");
typedef struct { void * f0; gm_b16_8 f1; uint8_t f2; uint8_t pe[7]; } gm_tlljg61a2ctgq;
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
_Static_assert(sizeof(gm_t46lmulm8gaj3) == 12 && _Alignof(gm_t46lmulm8gaj3) == 4 && offsetof(gm_t46lmulm8gaj3, f0) == 0 && offsetof(gm_t46lmulm8gaj3, f1) == 8, "layout of `?IntSuffix`");
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
_Static_assert(sizeof(gm_taari1s0m51a7) == 2 && _Alignof(gm_taari1s0m51a7) == 1 && offsetof(gm_taari1s0m51a7, f0) == 0 && offsetof(gm_taari1s0m51a7, f1) == 1, "layout of `?FloatSuffix`");
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
typedef struct { gm_tbci5ospk5nnn f0; gm_t46lmulm8gaj3 f1; uint8_t f2; uint8_t pe[3]; } gm_ta42iqot1auae;
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
_Static_assert(sizeof(gm_t53at80pfdus3) == 56 && _Alignof(gm_t53at80pfdus3) == 8 && offsetof(gm_t53at80pfdus3, f0) == 0 && offsetof(gm_t53at80pfdus3, f1) == 8, "layout of `TyKind`");
typedef struct { gm_th1g69d7vpipg f0; void * f1; void * f2; uint32_t f3; uint8_t pe[4]; } gm_t9i38ih07bojq;
_Static_assert(sizeof(gm_ta42iqot1auae) == 64 && _Alignof(gm_ta42iqot1auae) == 8 && offsetof(gm_ta42iqot1auae, f0) == 0 && offsetof(gm_ta42iqot1auae, f1) == 48 && offsetof(gm_ta42iqot1auae, f2) == 60, "layout of `IntLit`");
_Static_assert(sizeof(gm_tchr52g0ema53) == 48 && _Alignof(gm_tchr52g0ema53) == 8 && offsetof(gm_tchr52g0ema53, f0) == 0 && offsetof(gm_tchr52g0ema53, f1) == 40, "layout of `BreakExpr`");
typedef struct { gm_tngtuiuskn938 f0; gm_taari1s0m51a7 f1; uint8_t f2; uint8_t pe[5]; } gm_tghmb53v8qi1e;
_Static_assert(sizeof(gm_th1g69d7vpipg) == 40 && _Alignof(gm_th1g69d7vpipg) == 8 && offsetof(gm_th1g69d7vpipg, f0) == 0 && offsetof(gm_th1g69d7vpipg, f1) == 8, "layout of `WriteSink`");
_Static_assert(sizeof(gm_tioi3jh9v22g5) == 56 && _Alignof(gm_tioi3jh9v22g5) == 8 && offsetof(gm_tioi3jh9v22g5, f0) == 0 && offsetof(gm_tioi3jh9v22g5, f1) == 8 && offsetof(gm_tioi3jh9v22g5, f2) == 48, "layout of `ElseExpr`");
_Static_assert(sizeof(gm_tngtuiuskn938) == 96 && _Alignof(gm_tngtuiuskn938) == 8 && offsetof(gm_tngtuiuskn938, f0) == 0 && offsetof(gm_tngtuiuskn938, f1) == 48, "layout of `Rat`");
typedef struct { gm_th1g69d7vpipg f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; } gm_tntn2tf15puib;
typedef struct { gm_tsth9qu1dv0g3 f0; gm_b16_8 f1; } gm_to76jli3bqddq;
_Static_assert(sizeof(gm_tsth9qu1dv0g3) == 40 && _Alignof(gm_tsth9qu1dv0g3) == 8 && offsetof(gm_tsth9qu1dv0g3, f0) == 0 && offsetof(gm_tsth9qu1dv0g3, f1) == 8, "layout of `Callee`");
typedef struct { gm_tngtuiuskn938 f0; gm_tngtuiuskn938 f1; } gm_tu15p0hi2qlv6;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tbci5ospk5nnn v0; gm_tngtuiuskn938 v1; uint8_t v2; gm_tu15p0hi2qlv6 v3; } f1; } gm_t0ialelq3ql1k;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tbci5ospk5nnn v0; double v1; gm_tngtuiuskn938 v2; gm_tu15p0hi2qlv6 v3; uint8_t v4; gm_b16_8 v5; gm_b16_8 v6; uint32_t v7; gm_tkn42u7na3t81 v8; gm_tkn42u7na3t81 v9; gm_t88rcucg1b7uk v10; gm_t3rcjibl8kkg4 v11; gm_t08olq97rscn3 v12; uint32_t v13; gm_tcc1rq4n1avb2 v14; gm_t3877pk7lv748 v15; void * v16; gm_tbl8il92l97ut v17; gm_tlcl4u558l6p5 v18; gm_tao63pk3r18v5 v19; void * v20; void * v21; void * v22; void * v23; uint32_t v24; uint32_t v25; uint32_t v26; gm_t8ghgl2jj0qha v27; gm_trl9d7m1k5lbd v28; gm_ta612fil812pm v29; gm_t8jsc4n9vp2ib v30; gm_tt8o5kebou62r v31; gm_to76jli3bqddq v32; gm_b16_8 v33; gm_b16_8 v34; void * v35; gm_ttggq7ng4kp3p v36; void * v37; gm_tsplfr33dsvji v38; gm_t3dcoc7077t1q v39; gm_t083kd7b1kkag v40; gm_tlcl4u558l6p5 v41; gm_tkaapcii86udk v42; gm_tlcl4u558l6p5 v43; void * v44; gm_trl2khmuphncd v45; gm_t710lsrq1m8gk v46; gm_teo9d4mncjfdu v47; gm_te9hrk0u35rev v48; gm_ttggq7ng4kp3p v49; void * v50; gm_t3mf58gv9kf3n v51; void * v52; gm_tntn2tf15puib v53; gm_t9i38ih07bojq v54; gm_tandvbt63uk7n v55; gm_t710lsrq1m8gk v56; } f1; } gm_t5brlu7fal70j;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_ta42iqot1auae v0; gm_tghmb53v8qi1e v1; gm_tibupt4dmcqai v2; uint8_t v3; gm_t08olq97rscn3 v4; gm_t08olq97rscn3 v5; void * v6; gm_b16_8 v7; void * v8; gm_t8ghgl2jj0qha v9; gm_trl9d7m1k5lbd v10; gm_tioi3jh9v22g5 v11; void * v12; void * v13; gm_t3mf58gv9kf3n v14; void * v15; gm_tlljg61a2ctgq v16; gm_t3mf58gv9kf3n v17; gm_tao63pk3r18v5 v18; gm_tioie2ju3q4ul v19; gm_tupk62cr6lbdu v20; void * v21; gm_tgtub0v94d5u5 v22; gm_tgtub0v94d5u5 v23; gm_tgtub0v94d5u5 v24; gm_t3mf58gv9kf3n v25; gm_t3mf58gv9kf3n v26; void * v27; void * v28; void * v29; void * v30; void * v31; gm_tpe1hv3fekn05 v32; void * v33; void * v34; gm_tibupt4dmcqai v35; void * v36; void * v37; void * v38; void * v39; void * v40; gm_t79rbdv4rafe5 v41; void * v42; gm_tchr52g0ema53 v43; gm_t1s75i08qtptd v44; gm_t3mf58gv9kf3n v45; gm_tet9guhev5r4u v46; } f1; } gm_t6e9d80inoppc;
_Static_assert(sizeof(gm_t9i38ih07bojq) == 64 && _Alignof(gm_t9i38ih07bojq) == 8 && offsetof(gm_t9i38ih07bojq, f0) == 0 && offsetof(gm_t9i38ih07bojq, f1) == 40 && offsetof(gm_t9i38ih07bojq, f2) == 48 && offsetof(gm_t9i38ih07bojq, f3) == 56, "layout of `WriteDeferredExpr`");
_Static_assert(sizeof(gm_tghmb53v8qi1e) == 104 && _Alignof(gm_tghmb53v8qi1e) == 8 && offsetof(gm_tghmb53v8qi1e, f0) == 0 && offsetof(gm_tghmb53v8qi1e, f1) == 96 && offsetof(gm_tghmb53v8qi1e, f2) == 98, "layout of `FloatLit`");
_Static_assert(sizeof(gm_tntn2tf15puib) == 88 && _Alignof(gm_tntn2tf15puib) == 8 && offsetof(gm_tntn2tf15puib, f0) == 0 && offsetof(gm_tntn2tf15puib, f1) == 40 && offsetof(gm_tntn2tf15puib, f2) == 56 && offsetof(gm_tntn2tf15puib, f3) == 72, "layout of `WriteExpr`");
_Static_assert(sizeof(gm_to76jli3bqddq) == 56 && _Alignof(gm_to76jli3bqddq) == 8 && offsetof(gm_to76jli3bqddq, f0) == 0 && offsetof(gm_to76jli3bqddq, f1) == 40, "layout of `(callee: Callee, args: []Expr)`");
_Static_assert(sizeof(gm_tu15p0hi2qlv6) == 192 && _Alignof(gm_tu15p0hi2qlv6) == 8 && offsetof(gm_tu15p0hi2qlv6, f0) == 0 && offsetof(gm_tu15p0hi2qlv6, f1) == 96, "layout of `(re: Rat, im: Rat)`");
_Static_assert(sizeof(gm_t0ialelq3ql1k) == 200 && _Alignof(gm_t0ialelq3ql1k) == 8 && offsetof(gm_t0ialelq3ql1k, f0) == 0 && offsetof(gm_t0ialelq3ql1k, f1) == 8, "layout of `Untyped`");
_Static_assert(sizeof(gm_t5brlu7fal70j) == 200 && _Alignof(gm_t5brlu7fal70j) == 8 && offsetof(gm_t5brlu7fal70j, f0) == 0 && offsetof(gm_t5brlu7fal70j, f1) == 8, "layout of `ExprKind`");
_Static_assert(sizeof(gm_t6e9d80inoppc) == 112 && _Alignof(gm_t6e9d80inoppc) == 8 && offsetof(gm_t6e9d80inoppc, f0) == 0 && offsetof(gm_t6e9d80inoppc, f1) == 8, "layout of `ExprKind`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t88rcucg1b7uk v0; uint32_t v1; uint32_t v2; gm_tiilmfd16v3fc v3; gm_tnpifv20irnph v4; uint32_t v5; gm_tcc1rq4n1avb2 v6; gm_t88rcucg1b7uk v7; gm_t0ialelq3ql1k v8; uint64_t v9; gm_t88rcucg1b7uk v10; } f1; } gm_t6qooom8qa2do;
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint32_t v0; gm_t0ialelq3ql1k v1; uint32_t v2; gm_ts78ca2amthgu v3; gm_t88rcucg1b7uk v4; } f1; } gm_t930q5l38hdap;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_t6e9d80inoppc f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_tfbvnc90lci8r;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_t5brlu7fal70j f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_tfpf0s89fpb3r;
typedef struct { gm_t08olq97rscn3 f0; gm_tfbvnc90lci8r f1; } gm_t2tlhjbsqg5eb;
_Static_assert(sizeof(gm_t6qooom8qa2do) == 208 && _Alignof(gm_t6qooom8qa2do) == 8 && offsetof(gm_t6qooom8qa2do, f0) == 0 && offsetof(gm_t6qooom8qa2do, f1) == 8, "layout of `DeclValue`");
_Static_assert(sizeof(gm_t930q5l38hdap) == 208 && _Alignof(gm_t930q5l38hdap) == 8 && offsetof(gm_t930q5l38hdap, f0) == 0 && offsetof(gm_t930q5l38hdap, f1) == 8, "layout of `LocalBinding`");
_Static_assert(sizeof(gm_tfbvnc90lci8r) == 136 && _Alignof(gm_tfbvnc90lci8r) == 8 && offsetof(gm_tfbvnc90lci8r, f0) == 0 && offsetof(gm_tfbvnc90lci8r, f1) == 8 && offsetof(gm_tfbvnc90lci8r, f2) == 120, "layout of `Expr`");
_Static_assert(sizeof(gm_tfpf0s89fpb3r) == 224 && _Alignof(gm_tfpf0s89fpb3r) == 8 && offsetof(gm_tfpf0s89fpb3r, f0) == 0 && offsetof(gm_tfpf0s89fpb3r, f1) == 8 && offsetof(gm_tfpf0s89fpb3r, f2) == 208, "layout of `Expr`");
typedef struct { gm_t930q5l38hdap f0; uint8_t f1; uint8_t pe[7]; } gm_tqvu78rpi6q1h;
_Static_assert(sizeof(gm_t2tlhjbsqg5eb) == 168 && _Alignof(gm_t2tlhjbsqg5eb) == 8 && offsetof(gm_t2tlhjbsqg5eb, f0) == 0 && offsetof(gm_t2tlhjbsqg5eb, f1) == 32, "layout of `(name: Ident, value: Expr)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tfbvnc90lci8r v0; gm_t2tlhjbsqg5eb v1; gm_tfbvnc90lci8r v2; } f1; } gm_tq6gu4l68u3rv;
_Static_assert(sizeof(gm_tqvu78rpi6q1h) == 216 && _Alignof(gm_tqvu78rpi6q1h) == 8 && offsetof(gm_tqvu78rpi6q1h, f0) == 0 && offsetof(gm_tqvu78rpi6q1h, f1) == 208, "layout of `?LocalBinding`");
typedef struct { gm_tq6gu4l68u3rv f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_tpff59ptr3s92;
_Static_assert(sizeof(gm_tq6gu4l68u3rv) == 176 && _Alignof(gm_tq6gu4l68u3rv) == 8 && offsetof(gm_tq6gu4l68u3rv, f0) == 0 && offsetof(gm_tq6gu4l68u3rv, f1) == 8, "layout of `ArgKind`");
_Static_assert(sizeof(gm_tpff59ptr3s92) == 192 && _Alignof(gm_tpff59ptr3s92) == 8 && offsetof(gm_tpff59ptr3s92, f0) == 0 && offsetof(gm_tpff59ptr3s92, f1) == 176, "layout of `Arg`");
struct gm_fr_align_of_mks0ipd {
    gm_frame h;
    gm_taari1s0m51a7 ret;
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
struct gm_fr_resolve_write_e97fjom {
    gm_frame h;
    gm_t9i38ih07bojq l3; /* w */
    gm_tfeab279c16rr l7; /* t */
    gm_tfeab279c16rr l9; /* parsed */
    gm_trqfvmjtt4sij l25; /* stmts */
    gm_trqfvmjtt4sij l26; /* fields */
    gm_b0_1 ret;
};
struct gm_fr_source_offset_of_vlcjdq6 {
    gm_frame h;
    gm_tf5srh4mp4b3i ret;
};
struct gm_fr_template_args_vf1gdn1 {
    gm_frame h;
    gm_tfeab279c16rr l6; /* t */
    gm_t4gut2c9fj1kt l8; /* d */
    gm_tfeab279c16rr l13; /* parsed */
    gm_trqfvmjtt4sij l16; /* stmts */
    gm_trqfvmjtt4sij l17; /* values */
    gm_tfbvnc90lci8r l18; /* a */
    gm_trqfvmjtt4sij l31; /* vals */
    gm_tpff59ptr3s92 l34; /* a */
    gm_tfbvnc90lci8r l35; /* e */
    gm_trqfvmjtt4sij l37; /* reads */
    gm_ti6pdj2sunpe4 ret;
};
struct gm_fr_write_deferred_kbicf34 {
    gm_frame h;
    gm_th1g69d7vpipg l1; /* sink */
    gm_tfbvnc90lci8r l7; /* e */
    gm_trqfvmjtt4sij l10; /* elems */
    gm_tpff59ptr3s92 l13; /* a */
    gm_tfbvnc90lci8r l14; /* e */
    gm_trqfvmjtt4sij l15; /* ts */
    gm_tfpf0s89fpb3r l18; /* x */
    void * ret;
};
bool gm_eq5307j5k9ms(const uint8_t *a, const uint8_t *b);
bool gm_eq9m6mvtmgde(const uint8_t *a, const uint8_t *b);
bool gm_eqb44cb3cf1n(const uint8_t *a, const uint8_t *b);
bool gm_eqo3v9599nus(const uint8_t *a, const uint8_t *b);
__attribute__((aligned(4))) gm_b0_1 gm_f_add_fix_0gkcr4c(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_tl874t2atnlh3 a2, gm_b16_8 a3);
__attribute__((aligned(4))) gm_taari1s0m51a7 gm_f_align_of_mks0ipd(uint8_t a0);
__attribute__((aligned(4))) void * gm_f_bind_temp_sa5rcbr(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2);
__attribute__((aligned(4))) void * gm_f_block_of_2gjl2i1(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) void * gm_f_build_write_k6dcf8r(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, void * a2, gm_b16_8 a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) void gm_f_check_spec_4vp7jhg(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) void gm_f_const_string_jdjj62s(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
__attribute__((aligned(4))) void gm_f_const_template_5dvtt1m(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint64_t a2, uint32_t a3);
__attribute__((aligned(4))) void gm_f_default_spec_9ntjq2s(void *gm_out);
__attribute__((aligned(4))) void * gm_f_elements_tuple_gdm5n6q(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_emit_dph5v1e(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_5nn7ph0(void);
__attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_g3tddrb_2(void);
__attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
__attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_rcd2jln(void);
__attribute__((aligned(4))) gm_b0_1 gm_f_error_89glhjr(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_error_9huj44a(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) void * gm_f_error_expr_o6qt7e2(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1);
__attribute__((aligned(4))) void gm_f_error_s6hn871(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void * gm_f_expr_vmp704u(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_tkn42u7na3t81 a2);
__attribute__((aligned(4))) void * gm_f_file_o15fqku(void * a0, uint32_t a1);
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
__attribute__((aligned(4))) uint8_t gm_f_has_param_u9t0efs(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_intern_ia19mhl(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) uint8_t gm_f_is_default_spec_cdd8jng(void * a0);
__attribute__((aligned(4))) uint8_t gm_f_is_error_8k27gd1(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) uint8_t gm_f_is_integer_8d4f9ek(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void gm_f_kind_jcnt4hm(void *gm_out, void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void gm_f_list_5dhgg4i(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_list_epvducg(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_list_ir0h3tc_2(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_list_nof7vfj(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_list_qtk22f3(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_list_vp1djv1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_list_vp1djv1_2(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_lookup_local_6f38t7p(void *gm_out, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_lookup_module_name_cukfp7t(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) void * gm_f_mk_9bc97fd(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0, const void * a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_dsbahke(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_ve8bb01_2(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_number_at_esf7qkk(void *gm_out, gm_b16_8 a0, uint64_t a1);
__attribute__((aligned(4))) gm_tmnrj1ud20adj gm_f_number_at_esf7qkk_c(gm_b16_8 a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_numbered_error_07hll20(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_parse_spec_iod83cp(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_parse_template_rlpl2k8(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_placeholders_text_rtvia6b(void *env, gm_node *cx, gm_node *inst, uint64_t a0);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_3ef746b(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_d6c76ud_2(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_d943vgp(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_d943vgp_2(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_k3l9nic(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_nbmosph(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_t83rg2m(void * a0, uint8_t a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_ucgmlo2(void * a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_resolve_decl_pus042v(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_resolve_t2kal61(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_resolve_write_e97fjom(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_settle_h9ikvs5(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) gm_b16_8 gm_f_show_8uijkbs(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_show_fsnl6fa(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void * gm_f_soa_columns_tuple_ns477md(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_soa_of_jufdcj2(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_source_offset_of_vlcjdq6(gm_b16_8 a0, uint64_t a1);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_string_ty_j74ff0o(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) uint32_t gm_f_temp_ilbvj8q(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) void gm_f_template_args_vf1gdn1(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_tl874t2atnlh3 a2, gm_b16_8 a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) gm_t9qkh0rsooc8d gm_f_unify_t2qjv2p(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, gm_t88rcucg1b7uk a3);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_unit_2age34u(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_taari1s0m51a7 gm_f_var_kind_1sl5sig(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) void * gm_f_write_deferred_kbicf34(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, void * a2, gm_b16_8 a3, gm_tl874t2atnlh3 a4);
extern uint8_t gm_k08q1k9tq78[40] __attribute__((aligned(8)));
extern uint8_t gm_k76fiiae2n8[40] __attribute__((aligned(8)));
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
extern const char gm_src6d57grm686e3[];
extern const uint8_t gm_srundlmqvm06p[60];
extern const uint8_t gm_sutmkmalrac6l[45];
extern const uint8_t gm_sves946fckobv[104];
extern const gm_type gm_tyo86ks9qkvt;
inline __attribute__((aligned(4))) gm_taari1s0m51a7 gm_f_align_of_mks0ipd(uint8_t a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_align_of_mks0ipd F;
    uint8_t gm_l0; /* c */
    GM_STACK_CHECK(&F, 23u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t t1 = gm_l0;
    uint8_t t2 = t1;
    gm_taari1s0m51a7 t3;
    uint8_t t6 = GM_LD(uint8_t, ((uint8_t *)&t2));
    if (!(t6 == ((uint8_t)60ULL))) goto mn5;
    gm_t9qkh0rsooc8d t7;
    GM_ST(uint8_t, ((uint8_t *)&t7 + 0), ((uint8_t)1ULL));
    gm_taari1s0m51a7 t8;
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t8), t7);
    ((uint8_t *)&t8)[1] = 1;
    t3 = t8;
    goto me4;
mn5:;
    uint8_t t10 = GM_LD(uint8_t, ((uint8_t *)&t2));
    if (!(t10 == ((uint8_t)62ULL))) goto mn9;
    gm_t9qkh0rsooc8d t11;
    GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)2ULL));
    gm_taari1s0m51a7 t12;
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t12), t11);
    ((uint8_t *)&t12)[1] = 1;
    t3 = t12;
    goto me4;
mn9:;
    uint8_t t14 = GM_LD(uint8_t, ((uint8_t *)&t2));
    if (!(t14 == ((uint8_t)94ULL))) goto mn13;
    gm_t9qkh0rsooc8d t15;
    GM_ST(uint8_t, ((uint8_t *)&t15 + 0), ((uint8_t)3ULL));
    gm_taari1s0m51a7 t16;
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t16), t15);
    ((uint8_t *)&t16)[1] = 1;
    t3 = t16;
    goto me4;
mn13:;
    gm_taari1s0m51a7 t18;
    memset(&t18, 0, sizeof t18);
    t3 = t18;
    goto me4;
mn17:;
    gm_trap("no match arm matched the scrutinee", gm_src6d57grm686e3, 113, 43);
me4:;
    F.ret = t3;
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
    GM_STACK_CHECK(&F, 2394u);
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
        gm_b16_8 t33 = t32;
        gm_b16_8 t34 = GM_LD(gm_b16_8, ((uint8_t *)&t33));
        gm_l6 = t34;
    }
    {
        void * t36 = gm_l2;
        uint8_t t37 = GM_LD(uint8_t, ((((uint8_t *)t36) + 8) + 0));
        if (t37 != ((uint8_t)2ULL)) goto ie35;
        gm_tibupt4dmcqai t38 = GM_LD(gm_tibupt4dmcqai, ((((uint8_t *)t36) + 8) + 8));
        gm_l7 = t38;
        {
            gm_b16_8 t40 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l7) + 0));
            gm_b16_8 t41 = t40;
            gm_b16_8 t42 = GM_LD(gm_b16_8, ((uint8_t *)&t41));
            gm_l6 = t42;
        }
        goto ix35;
ie35:;
        void * t44 = gm_l0;
        void * t45 = gm_l2;
        void * t46 = gm_l0;
        uint64_t t47 = GM_LD(uint64_t, (((uint8_t *)t46) + 8));
        gm_th2lqbo77pvsc t48;
        gm_f_const_template_5dvtt1m((void *)&t48, (void *)NULL, cx, NULL, t44, t45, t47, ((uint32_t)0ULL));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_th2lqbo77pvsc t49 = t48;
        if (!((((uint8_t *)&t49))[16] != 0)) goto ie43;
        gm_b16_8 t50 = GM_LD(gm_b16_8, ((uint8_t *)&t49));
        gm_l8 = t50;
        {
            gm_b16_8 t52 = gm_l8;
            gm_l6 = t52;
        }
        goto ix43;
ie43:;
        void * t54 = gm_l0;
        void * t55 = GM_LD(void *, (((uint8_t *)t54) + 0));
        void * t56 = gm_l0;
        uint64_t t57 = GM_LD(uint64_t, (((uint8_t *)t56) + 8));
        void * t58 = gm_l2;
        gm_th2lqbo77pvsc t59;
        gm_f_const_string_jdjj62s((void *)&t59, (void *)NULL, cx, NULL, t55, t57, t58);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_th2lqbo77pvsc t60 = t59;
        if (!((((uint8_t *)&t60))[16] != 0)) goto ie53;
        gm_b16_8 t61 = GM_LD(gm_b16_8, ((uint8_t *)&t60));
        gm_l9 = t61;
        {
            gm_b16_8 t63 = gm_l9;
            gm_b16_8 t64 = t63;
            gm_b16_8 t65 = GM_LD(gm_b16_8, ((uint8_t *)&t64));
            gm_l6 = t65;
        }
        goto ix53;
ie53:;
        {
            void * t67 = gm_l0;
            void * t68 = gm_l2;
            gm_tl874t2atnlh3 t69 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t68) + 120));
            gm_b16_8 t70;
            GM_ST(void *, (uint8_t *)&t70, (void *)(gm_sves946fckobv));
            GM_ST(uintptr_t, (uint8_t *)&t70 + 8, (uintptr_t)(103));
            gm_b0_1 t71 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t67, t69, t70);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t72 = gm_l0;
            void * t73 = GM_LD(void *, (((uint8_t *)t72) + 0));
            gm_tl874t2atnlh3 t74 = gm_l4;
            void * t75 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t73, t74);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t76 = 0;
            F.ret = t75;
            goto gm_fret;
        }
ix53:;
ix43:;
ix35:;
    }
    {
        void * t77 = gm_l0;
        gm_b16_8 t78 = gm_l6;
        void * t79 = gm_l2;
        gm_tl874t2atnlh3 t80 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t79) + 120));
        gm_b16_8 t81 = gm_l3;
        gm_tl874t2atnlh3 t82 = gm_l4;
        gm_ti6pdj2sunpe4 t83;
        gm_f_template_args_vf1gdn1((void *)&t83, (void *)NULL, cx, NULL, t77, t78, t80, t81, t82);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_ti6pdj2sunpe4 t84 = t83;
        gm_t0uuhaljsf410 t85;
        if (((((uint8_t *)&t84))[64] != 0)) {
            gm_t0uuhaljsf410 t86 = GM_LD(gm_t0uuhaljsf410, (((uint8_t *)&t84) + 0));
            t85 = t86;
        } else {
            void * t87 = gm_l0;
            void * t88 = GM_LD(void *, (((uint8_t *)t87) + 0));
            gm_tl874t2atnlh3 t89 = gm_l4;
            void * t90 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t88, t89);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t91 = 0;
            F.ret = t90;
            goto gm_fret;
        }
        F.l10 = t85;
    }
    {
        {
            gm_l12 = ((uint64_t)0ULL);
        }
        {
            uint64_t t93 = GM_LD(uintptr_t, (((uint8_t *)&F.l10) + 32) + 8);
            gm_l11 = t93;
        }
        {
            for (;;) {
                uint64_t t95 = gm_l12;
                uint64_t t96 = gm_l11;
                uint8_t t97 = (t95 < t96);
                if (!t97) goto lb94;
                {
                    uint64_t t100 = gm_l12;
                    uint64_t t101 = t100;
                    uint64_t t104 = GM_LD(uint64_t, ((uint8_t *)&t101));
                    gm_l13 = t104;
                    {
                        void * t107 = gm_l0;
                        uint64_t t108 = gm_l13;
                        uint64_t t109 = (uintptr_t)t108;
                        uint64_t t110 = GM_LD(uintptr_t, (((uint8_t *)&F.l10) + 32) + 8);
                        if (t109 >= (uintptr_t)(t110)) gm_trap_index(t109, t110, gm_src6d57grm686e3, 303, 23);
                        void * t111 = (void *)((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l10) + 32)) + t109 * 224);
                        gm_t8cc1mtbrhlpt t112;
                        gm_f_soa_of_jufdcj2((void *)&t112, (void *)NULL, cx, NULL, t107, t111);
                        if (gm_unw.kind) {
                            goto gm_ll98;
                        }
                        gm_t8cc1mtbrhlpt t113 = t112;
                        uint8_t t114;
                        if (((((uint8_t *)&t113))[32] != 0)) goto mn116;
                        t114 = ((uint8_t)0);
                        goto me115;
mn116:;
                        t114 = ((uint8_t)1);
                        goto me115;
mn117:;
                        gm_trap("no match arm matched the scrutinee", gm_src6d57grm686e3, 303, 12);
me115:;
                        if (!t114) goto ie106;
                        {
                            uint64_t t119 = gm_l13;
                            uint64_t t120 = (uintptr_t)t119;
                            uint64_t t121 = GM_LD(uintptr_t, (((uint8_t *)&F.l10) + 32) + 8);
                            if (t120 >= (uintptr_t)(t121)) gm_trap_index(t120, t121, gm_src6d57grm686e3, 304, 26);
                            gm_tq7s98ihonkju t122 = gm_f_new_ve8bb01_2((void *)NULL, cx, NULL, &(*(gm_tfpf0s89fpb3r *)((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l10) + 32)) + t120 * 224)), &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_k08q1k9tq78)));
                            if (gm_unw.kind) {
                                goto gm_ll98;
                            }
                            gm_tq7s98ihonkju t123 = t122;
                            if (((((uint8_t *)&t123))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t123) + 8, gm_src6d57grm686e3, 304, 22);
                            void * t124 = GM_LD(void *, (((uint8_t *)&t123) + 8));
                            gm_l14 = t124;
                        }
                        {
                            uint64_t t125 = gm_l13;
                            uint64_t t126 = (uintptr_t)t125;
                            uint64_t t127 = GM_LD(uintptr_t, (((uint8_t *)&F.l10) + 32) + 8);
                            if (t126 >= (uintptr_t)(t127)) gm_trap_index(t126, t127, gm_src6d57grm686e3, 305, 13);
                            uint8_t *t128 = ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l10) + 32)) + t126 * 224);
                            void * t129 = gm_l0;
                            void * t130 = gm_l14;
                            gm_b16_8 t131;
                            GM_ST(void *, (uint8_t *)&t131, (void *)(gm_s4kt3mhlp0or9));
                            GM_ST(uintptr_t, (uint8_t *)&t131 + 8, (uintptr_t)(10));
                            void * t132 = gm_f_soa_columns_tuple_ns477md((void *)NULL, cx, NULL, t129, t130, t131);
                            if (gm_unw.kind) {
                                goto gm_ll98;
                            }
                            gm_tfpf0s89fpb3r t133 = GM_LD(gm_tfpf0s89fpb3r, ((uint8_t *)t132));
                            GM_ST(gm_tfpf0s89fpb3r, t128, t133);
                        }
                        goto ix106;
ie106:;
ix106:;
                    }
                    goto me102;
mn103:;
                    gm_trap("no match arm matched the scrutinee", gm_src6d57grm686e3, 302, 5);
me102:;
                }
lc94:;
                {
                    uint64_t t135 = gm_l12;
                    uint64_t r136; bool o137 = __builtin_add_overflow(t135, ((uint64_t)1ULL), &r136);
                    uint64_t t138 = r136;
                    gm_l12 = t138;
                }
            }
gm_ll98:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb94; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc94; }
            }
            goto gm_lad;
lb94:;
        }
    }
    {
        void * t139 = gm_l0;
        void * t140 = GM_LD(void *, (((uint8_t *)t139) + 0));
        void * t141 = (void *)(((uint8_t *)t140) + 8);
        gm_t88rcucg1b7uk t142 = gm_f_unit_2age34u((void *)NULL, cx, NULL, t141);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tntn2tf15puib t143;
        memcpy((uint8_t *)&t143 + 0, ((uint8_t *)&F.l1), 40);
        gm_b16_8 t144 = GM_LD(gm_b16_8, (((uint8_t *)&F.l10) + 0));
        GM_ST(gm_b16_8, (uint8_t *)&t143 + 40, t144);
        gm_b16_8 t145 = GM_LD(gm_b16_8, (((uint8_t *)&F.l10) + 16));
        GM_ST(gm_b16_8, (uint8_t *)&t143 + 56, t145);
        gm_b16_8 t146 = GM_LD(gm_b16_8, (((uint8_t *)&F.l10) + 32));
        GM_ST(gm_b16_8, (uint8_t *)&t143 + 72, t146);
        gm_t5brlu7fal70j t147;
        memset(&t147, 0, sizeof t147);
        GM_ST(uint8_t, ((uint8_t *)&t147 + 0), ((uint8_t)55ULL));
        GM_ST(gm_tntn2tf15puib, ((uint8_t *)&t147 + 8), t143);
        gm_tl874t2atnlh3 t148 = gm_l4;
        void * t149 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t142, &t147, t148);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l15 = t149;
    }
    {
        uint64_t t151 = GM_LD(uintptr_t, (((uint8_t *)&F.l10) + 48) + 8);
        uint8_t t152 = ((t151 == ((uint64_t)0ULL)));
        if (!t152) goto ie150;
        {
            void * t154 = gm_l15;
            uint8_t t155 = 0;
            F.ret = t154;
            goto gm_fret;
        }
        goto ix150;
ie150:;
ix150:;
    }
    {
        gm_f_list_ir0h3tc_2((void *)&F.l16, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t157 = GM_LD(gm_b16_8, (((uint8_t *)&F.l10) + 48));
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
                    uint64_t t164 = gm_l18;
                    uint64_t t165 = (uintptr_t)t164;
                    uint64_t t166 = GM_LD(uintptr_t, ((uint8_t *)&gm_l17) + 8);
                    gm_trcmmd12cefb1 t169 = GM_LD(gm_trcmmd12cefb1, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l17)) + t165 * 32));
                    gm_l19 = t169;
                    {
                        void * t171 = (void *)((uint8_t *)&F.l16);
                        gm_tftb1odc739t5 t172 = gm_f_push_d6c76ud_2(t171, &(*(gm_trcmmd12cefb1 *)((uint8_t *)&gm_l19)));
                        gm_tftb1odc739t5 t173 = t172;
                        if (((((uint8_t *)&t173))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t173) + 1, gm_src6d57grm686e3, 311, 26);
                        gm_b0_1 t174 = GM_LD(gm_b0_1, (((uint8_t *)&t173) + 1));
                    }
                    goto me167;
mn168:;
                    gm_trap("no match arm matched the scrutinee", gm_src6d57grm686e3, 311, 5);
me167:;
                }
lc158:;
                {
                    uint64_t t176 = gm_l18;
                    uint64_t r177; bool o178 = __builtin_add_overflow(t176, ((uint64_t)1ULL), &r177);
                    uint64_t t179 = r177;
                    gm_l18 = t179;
                }
            }
lb158:;
        }
    }
    {
        void * t180 = (void *)((uint8_t *)&F.l16);
        void * t181 = gm_l15;
        gm_trcmmd12cefb1 t182;
        GM_ST(uint8_t, (uint8_t *)&t182 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t182 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t182 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t182 + 16, 0);
        GM_ST(uint64_t, (uint8_t *)&t182 + 24, 0);
        GM_ST(uint8_t, ((uint8_t *)&t182 + 0), ((uint8_t)4ULL));
        GM_ST(void *, ((uint8_t *)&t182 + 8), t181);
        gm_tftb1odc739t5 t183 = gm_f_push_d6c76ud_2(t180, &t182);
        gm_tftb1odc739t5 t184 = t183;
        if (((((uint8_t *)&t184))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t184) + 1, gm_src6d57grm686e3, 312, 5);
        gm_b0_1 t185 = GM_LD(gm_b0_1, (((uint8_t *)&t184) + 1));
    }
    {
        void * t186 = gm_l0;
        void * t187 = GM_LD(void *, (((uint8_t *)t186) + 0));
        gm_b16_8 t188 = GM_LD(gm_b16_8, (((uint8_t *)&F.l16) + 0));
        gm_tl874t2atnlh3 t189 = gm_l4;
        void * t190 = gm_f_block_of_2gjl2i1((void *)NULL, cx, NULL, t187, t188, t189);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t191 = 0;
        F.ret = t190;
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
    GM_STACK_CHECK(&F, 1155u);
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
        gm_t53at80pfdus3 t10 = t9;
        uint8_t t11;
        uint8_t t14 = GM_LD(uint8_t, (((uint8_t *)&t10) + 0));
        if (t14 != ((uint8_t)3ULL)) goto mn13;
        t11 = ((uint8_t)1);
        goto me12;
mn13:;
        t11 = ((uint8_t)0);
        goto me12;
mn15:;
        gm_trap("no match arm matched the scrutinee", gm_src6d57grm686e3, 216, 14);
me12:;
        gm_l5 = t11;
    }
    {
        uint8_t t16 = 0;
        uint8_t t17 = 0;
        uint8_t t18 = 0;
        void * t19 = gm_l1;
        uint8_t t20 = ((GM_LD(uint8_t, (((uint8_t *)t19) + 20) + 0) == ((uint8_t)1ULL)));
        if (!t20) {
            void * t21 = gm_l1;
            uint8_t t22 = ((GM_LD(uint8_t, (((uint8_t *)t21) + 20) + 0) == ((uint8_t)2ULL)));
            t18 = t22;
        } else t18 = 1;
        if (!t18) {
            void * t23 = gm_l1;
            uint8_t t24 = ((GM_LD(uint8_t, (((uint8_t *)t23) + 20) + 0) == ((uint8_t)3ULL)));
            t17 = t24;
        } else t17 = 1;
        if (!t17) {
            void * t25 = gm_l1;
            uint8_t t26 = ((GM_LD(uint8_t, (((uint8_t *)t25) + 20) + 0) == ((uint8_t)4ULL)));
            t16 = t26;
        } else t16 = 1;
        gm_l6 = t16;
    }
    {
        gm_l7 = ((uint8_t)0);
    }
    {
        void * t28 = gm_l3;
        gm_t88rcucg1b7uk t29 = gm_l2;
        gm_t53at80pfdus3 t30;
        gm_f_kind_jcnt4hm((void *)&t30, t28, GM_CONV(gm_csguankko4lmhd, t29));
        gm_t53at80pfdus3 t31 = t30;
        uint8_t t32 = GM_LD(uint8_t, (((uint8_t *)&t31) + 0));
        if (t32 != ((uint8_t)10ULL)) goto ie27;
        gm_t88rcucg1b7uk t33 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t31) + 8) + 4));
        gm_l8 = t33;
        {
            void * t35 = gm_l3;
            gm_t88rcucg1b7uk t36 = gm_l8;
            gm_t53at80pfdus3 t37;
            gm_f_kind_jcnt4hm((void *)&t37, t35, GM_CONV(gm_csguankko4lmhd, t36));
            gm_t53at80pfdus3 t38 = t37;
            gm_tjcj4j2gbh6c8 t39;
            GM_ST(uint8_t, (uint8_t *)&t39 + 1, 0);
            GM_ST(uint8_t, ((uint8_t *)&t39 + 0), ((uint8_t)0));
            GM_ST(uint16_t, ((uint8_t *)&t39 + 2), ((uint16_t)8ULL));
            gm_tjcj4j2gbh6c8 t40 = t39;
            uint8_t t41 = (((GM_LD(uint8_t, ((uint8_t *)&t38) + 0) == ((uint8_t)0ULL)) && gm_eqo3v9599nus((((uint8_t *)&t38) + 8), ((uint8_t *)&t40))));
            gm_l7 = t41;
        }
        goto ix27;
ie27:;
ix27:;
    }
    {
        gm_th2lqbo77pvsc t42;
        memset(&t42, 0, sizeof t42);
        gm_l9 = t42;
    }
    {
        void * t44 = gm_l1;
        uint8_t t45 = ((GM_LD(uint8_t, (((uint8_t *)t44) + 20) + 0) == ((uint8_t)6ULL)));
        if (!t45) goto ie43;
        uint8_t t46 = gm_l7;
        uint8_t t47 = (uint8_t)!t46;
        if (!t47) goto ie43;
        {
            gm_b16_8 t49;
            GM_ST(void *, (uint8_t *)&t49, (void *)(gm_s93i7ga7kbfpu));
            GM_ST(uintptr_t, (uint8_t *)&t49 + 8, (uintptr_t)(33));
            gm_th2lqbo77pvsc t50;
            GM_ST(uint8_t, (uint8_t *)&t50 + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&t50 + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&t50 + 20, 0);
            GM_ST(gm_b16_8, ((uint8_t *)&t50), t49);
            ((uint8_t *)&t50)[16] = 1;
            gm_l9 = t50;
        }
        goto ix43;
ie43:;
        uint8_t t52 = gm_l6;
        if (!t52) goto ie51;
        uint8_t t53 = gm_l4;
        uint8_t t54 = (uint8_t)!t53;
        if (!t54) goto ie51;
        {
            gm_b16_8 t56;
            GM_ST(void *, (uint8_t *)&t56, (void *)(gm_sggmh06s4b0cs));
            GM_ST(uintptr_t, (uint8_t *)&t56 + 8, (uintptr_t)(22));
            gm_th2lqbo77pvsc t57;
            GM_ST(uint8_t, (uint8_t *)&t57 + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&t57 + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&t57 + 20, 0);
            GM_ST(gm_b16_8, ((uint8_t *)&t57), t56);
            ((uint8_t *)&t57)[16] = 1;
            gm_l9 = t57;
        }
        goto ix51;
ie51:;
        void * t59 = gm_l1;
        uint8_t t60 = ((GM_LD(uint8_t, (((uint8_t *)t59) + 20) + 0) == ((uint8_t)5ULL)));
        if (!t60) goto ie58;
        uint8_t t61 = gm_l5;
        uint8_t t62 = (uint8_t)!t61;
        if (!t62) goto ie58;
        {
            gm_b16_8 t64;
            GM_ST(void *, (uint8_t *)&t64, (void *)(gm_s3on77ql0ndko));
            GM_ST(uintptr_t, (uint8_t *)&t64 + 8, (uintptr_t)(16));
            gm_th2lqbo77pvsc t65;
            GM_ST(uint8_t, (uint8_t *)&t65 + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&t65 + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&t65 + 20, 0);
            GM_ST(gm_b16_8, ((uint8_t *)&t65), t64);
            ((uint8_t *)&t65)[16] = 1;
            gm_l9 = t65;
        }
        goto ix58;
ie58:;
        void * t67 = gm_l1;
        uint8_t t68;
        if ((((((uint8_t *)t67) + 12))[4] != 0)) goto mn70;
        t68 = ((uint8_t)0);
        goto me69;
mn70:;
        t68 = ((uint8_t)1);
        goto me69;
mn71:;
        gm_trap("no match arm matched the scrutinee", gm_src6d57grm686e3, 227, 15);
me69:;
        if (!t68) goto ie66;
        uint8_t t72 = gm_l5;
        uint8_t t73 = (uint8_t)!t72;
        if (!t73) goto ie66;
        {
            gm_b16_8 t75;
            GM_ST(void *, (uint8_t *)&t75, (void *)(gm_s23qsffuqlc1v));
            GM_ST(uintptr_t, (uint8_t *)&t75 + 8, (uintptr_t)(24));
            gm_th2lqbo77pvsc t76;
            GM_ST(uint8_t, (uint8_t *)&t76 + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&t76 + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&t76 + 20, 0);
            GM_ST(gm_b16_8, ((uint8_t *)&t76), t75);
            ((uint8_t *)&t76)[16] = 1;
            gm_l9 = t76;
        }
        goto ix66;
ie66:;
        uint8_t t78 = 0;
        void * t79 = gm_l1;
        uint8_t t80 = GM_LD(uint8_t, (((uint8_t *)t79) + 2));
        if (!t80) {
            void * t81 = gm_l1;
            uint8_t t82 = GM_LD(uint8_t, (((uint8_t *)t81) + 4));
            t78 = t82;
        } else t78 = 1;
        if (!t78) goto ie77;
        uint8_t t83 = 0;
        uint8_t t84 = gm_l4;
        if (!t84) {
            uint8_t t85 = gm_l5;
            t83 = t85;
        } else t83 = 1;
        uint8_t t86 = (uint8_t)!t83;
        if (!t86) goto ie77;
        {
            gm_b16_8 t88;
            GM_ST(void *, (uint8_t *)&t88, (void *)(gm_soreld657hea2));
            GM_ST(uintptr_t, (uint8_t *)&t88 + 8, (uintptr_t)(24));
            gm_th2lqbo77pvsc t89;
            GM_ST(uint8_t, (uint8_t *)&t89 + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&t89 + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&t89 + 20, 0);
            GM_ST(gm_b16_8, ((uint8_t *)&t89), t88);
            ((uint8_t *)&t89)[16] = 1;
            gm_l9 = t89;
        }
        goto ix77;
ie77:;
ix77:;
ix66:;
ix58:;
ix51:;
ix43:;
    }
    {
        gm_th2lqbo77pvsc t90 = gm_l9;
        gm_th2lqbo77pvsc t91 = t90;
        gm_b16_8 t92;
        if (((((uint8_t *)&t91))[16] != 0)) {
            gm_b16_8 t93 = GM_LD(gm_b16_8, (((uint8_t *)&t91) + 0));
            t92 = t93;
        } else {
            gm_th2lqbo77pvsc t94;
            memset(&t94, 0, sizeof t94);
            uint8_t t95 = 0;
            F.ret = t94;
            goto gm_fret;
        }
        gm_l10 = t92;
    }
    {
        void * t97 = gm_l3;
        gm_t88rcucg1b7uk t98 = gm_l2;
        uint8_t t99 = gm_f_is_error_8k27gd1(t97, GM_CONV(gm_csguankko4lmhd, t98));
        if (!t99) goto ie96;
        {
            gm_th2lqbo77pvsc t101;
            memset(&t101, 0, sizeof t101);
            uint8_t t102 = 0;
            F.ret = t101;
            goto gm_fret;
        }
        goto ix96;
ie96:;
ix96:;
    }
    {
        gm_t6j6smj7qoctu t103;
        gm_b16_8 t104 = gm_l10;
        GM_ST(gm_b16_8, (uint8_t *)&t103 + 0, t104);
        void * t105 = gm_l0;
        gm_t88rcucg1b7uk t106 = gm_l2;
        gm_b16_8 t107 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t105, t106);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_b16_8, (uint8_t *)&t103 + 16, t107);
        gm_tabb710d9mq2e t108;
        gm_f_format_0t8rkc3((void *)&t108, (void *)NULL, cx, NULL, &t103);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t109 = t108;
        if (((((uint8_t *)&t109))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t109) + 8, gm_src6d57grm686e3, 234, 12);
        gm_b16_8 t110 = GM_LD(gm_b16_8, (((uint8_t *)&t109) + 8));
        gm_b16_8 t111 = t110;
        gm_b16_8 t112 = GM_LD(gm_b16_8, ((uint8_t *)&t111));
        gm_th2lqbo77pvsc t113;
        GM_ST(uint8_t, (uint8_t *)&t113 + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&t113 + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&t113 + 20, 0);
        GM_ST(gm_b16_8, ((uint8_t *)&t113), t112);
        ((uint8_t *)&t113)[16] = 1;
        uint8_t t114 = 0;
        F.ret = t113;
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
    GM_STACK_CHECK(&F, 1797u);
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
        gm_b16_8 t35 = t34;
        gm_b16_8 t36 = GM_LD(gm_b16_8, ((uint8_t *)&t35));
        gm_th2lqbo77pvsc t37;
        GM_ST(uint8_t, (uint8_t *)&t37 + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&t37 + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&t37 + 20, 0);
        GM_ST(gm_b16_8, ((uint8_t *)&t37), t36);
        ((uint8_t *)&t37)[16] = 1;
        t26 = t37;
ix27:;
        uint8_t t38 = 0;
        F.ret = t26;
        goto gm_fret;
        goto me9;
mn23:;
ma12:;
        uint8_t t40 = GM_LD(uint8_t, ((((uint8_t *)t8) + 8) + 0));
        if (t40 != ((uint8_t)6ULL)) goto mn39;
        gm_t08olq97rscn3 t41 = GM_LD(gm_t08olq97rscn3, ((((uint8_t *)t8) + 8) + 8));
        gm_l6 = t41;
        {
            uint32_t t44 = gm_l3;
            uint8_t t45 = ((t44 == ((uint32_t)0ULL)));
            if (!t45) goto ie43;
            void * t46 = gm_l0;
            gm_b16_8 t47 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l6) + 0));
            gm_tqvu78rpi6q1h t48;
            gm_f_lookup_local_6f38t7p((void *)&t48, t46, t47);
            gm_tqvu78rpi6q1h t49 = t48;
            uint8_t t50;
            if (((((uint8_t *)&t49))[208] != 0)) goto mn52;
            t50 = ((uint8_t)0);
            goto me51;
mn52:;
            t50 = ((uint8_t)1);
            goto me51;
mn53:;
            gm_trap("no match arm matched the scrutinee", gm_src6d57grm686e3, 267, 31);
me51:;
            if (!t50) goto ie43;
            {
                gm_th2lqbo77pvsc t55;
                memset(&t55, 0, sizeof t55);
                uint8_t t56 = 0;
                F.ret = t55;
                goto gm_fret;
            }
            goto ix43;
ie43:;
ix43:;
        }
        {
            void * t57 = gm_l0;
            void * t58 = GM_LD(void *, (((uint8_t *)t57) + 0));
            uint64_t t59 = gm_l2;
            gm_b16_8 t60 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l6) + 0));
            gm_tf5srh4mp4b3i t61 = gm_f_lookup_module_name_cukfp7t((void *)NULL, cx, NULL, t58, t59, t60);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tf5srh4mp4b3i t62 = t61;
            uint64_t t63;
            if (((((uint8_t *)&t62))[8] != 0)) {
                uint64_t t64 = GM_LD(uint64_t, (((uint8_t *)&t62) + 0));
                t63 = t64;
            } else {
                gm_th2lqbo77pvsc t65;
                memset(&t65, 0, sizeof t65);
                uint8_t t66 = 0;
                F.ret = t65;
                goto gm_fret;
            }
            gm_l7 = t63;
        }
        {
            void * t68 = gm_l0;
            void * t69 = GM_LD(void *, (((uint8_t *)t68) + 0));
            uint64_t t70 = gm_l7;
            gm_t6qooom8qa2do t71;
            gm_f_resolve_decl_pus042v((void *)&t71, (void *)NULL, cx, NULL, t69, t70);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t6qooom8qa2do t72 = t71;
            uint8_t t73 = GM_LD(uint8_t, (((uint8_t *)&t72) + 0));
            if (t73 != ((uint8_t)7ULL)) goto ie67;
            gm_t88rcucg1b7uk t74 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t72) + 8));
            gm_l8 = t74;
            {
                gm_t88rcucg1b7uk t77 = gm_l8;
                void * t78 = gm_l0;
                void * t79 = GM_LD(void *, (((uint8_t *)t78) + 0));
                void * t80 = (void *)(((uint8_t *)t79) + 8);
                gm_t88rcucg1b7uk t81 = gm_f_string_ty_j74ff0o((void *)NULL, cx, NULL, t80);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_t88rcucg1b7uk t82 = t77;
                gm_t88rcucg1b7uk t83 = t81;
                uint8_t t84 = (!gm_eq9m6mvtmgde(((uint8_t *)&t82), ((uint8_t *)&t83)));
                if (!t84) goto ie76;
                {
                    gm_th2lqbo77pvsc t86;
                    memset(&t86, 0, sizeof t86);
                    uint8_t t87 = 0;
                    F.ret = t86;
                    goto gm_fret;
                }
                goto ix76;
ie76:;
ix76:;
            }
            {
                uint64_t t88 = gm_l7;
                uint64_t t89 = (uintptr_t)t88;
                void * t90 = gm_l0;
                void * t91 = GM_LD(void *, (((uint8_t *)t90) + 0));
                void * t92 = GM_LD(void *, (((uint8_t *)t91) + 0));
                uint64_t t93 = GM_LD(uintptr_t, ((((uint8_t *)t92) + 240) + 0) + 8);
                if (t89 >= (uintptr_t)(t93)) gm_trap_index(t89, t93, gm_src6d57grm686e3, 271, 22);
                void * t94 = (void *)((uint8_t *)GM_LD(void *, ((((uint8_t *)t92) + 240) + 0)) + t89 * 216);
                gm_l9 = t94;
            }
            {
                void * t96 = gm_l9;
                uint8_t t97 = GM_LD(uint8_t, (((((uint8_t *)t96) + 40) + 64) + 0));
                if (t97 != ((uint8_t)0ULL)) goto ie95;
                gm_tv2uf4leil5hq t98 = GM_LD(gm_tv2uf4leil5hq, (((((uint8_t *)t96) + 40) + 64) + 8));
                F.l10 = t98;
                {
                    uint64_t t101 = GM_LD(uintptr_t, (((uint8_t *)&F.l10) + 8) + 8);
                    uint8_t t102 = (!(t101 == ((uint64_t)1ULL)));
                    if (!t102) goto ie100;
                    {
                        gm_th2lqbo77pvsc t104;
                        memset(&t104, 0, sizeof t104);
                        uint8_t t105 = 0;
                        F.ret = t104;
                        goto gm_fret;
                    }
                    goto ix100;
ie100:;
ix100:;
                }
                {
                    void * t106 = gm_l0;
                    void * t107 = GM_LD(void *, (((uint8_t *)&F.l10) + 40));
                    void * t108 = gm_l9;
                    uint64_t t109 = GM_LD(uint64_t, (((uint8_t *)t108) + 32));
                    uint32_t t110 = gm_l3;
                    uint32_t r111; bool o112 = __builtin_add_overflow(t110, ((uint32_t)1ULL), &r111);
                    if (o112) gm_trap("integer overflow in `+`", gm_src6d57grm686e3, 274, 59);
                    uint32_t t113 = r111;
                    gm_f_const_template_5dvtt1m((void *)&(*(gm_th2lqbo77pvsc *)gm_out), (void *)NULL, cx, NULL, t106, t107, t109, t113);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_fdone;
                }
                goto ix95;
ie95:;
                {
                    gm_th2lqbo77pvsc t115;
                    memset(&t115, 0, sizeof t115);
                    uint8_t t116 = 0;
                    F.ret = t115;
                    goto gm_fret;
                }
ix95:;
            }
            goto ix67;
ie67:;
            {
                gm_th2lqbo77pvsc t118;
                memset(&t118, 0, sizeof t118);
                uint8_t t119 = 0;
                F.ret = t118;
                goto gm_fret;
            }
ix67:;
        }
        goto me9;
mn39:;
ma13:;
        gm_th2lqbo77pvsc t121;
        memset(&t121, 0, sizeof t121);
        uint8_t t122 = 0;
        F.ret = t121;
        goto gm_fret;
        goto me9;
mn120:;
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
    GM_STACK_CHECK(&F, 266u);
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
    GM_STACK_CHECK(&F, 981u);
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
        gm_tabb710d9mq2e t20 = t19;
        if (((((uint8_t *)&t20))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t20) + 8, gm_src6d57grm686e3, 333, 9);
        gm_b16_8 t21 = GM_LD(gm_b16_8, (((uint8_t *)&t20) + 8));
        gm_b16_8 t22 = t21;
        gm_b16_8 t23 = t22;
        gm_b16_8 t24 = GM_LD(gm_b16_8, ((uint8_t *)&t23));
        t9 = t24;
ix10:;
        t2 = t9;
ix3:;
        gm_l2 = t2;
    }
    {
        {
            gm_b16_8 t26 = GM_LD(gm_b16_8, (((uint8_t *)a0) + 32));
            gm_l3 = t26;
        }
        {
            gm_l4 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t28 = gm_l4;
                uint64_t t29 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                uint8_t t30 = (t28 < t29);
                if (!t30) goto lb27;
                {
                    uint64_t t33 = gm_l4;
                    uint64_t t34 = (uintptr_t)t33;
                    uint64_t t35 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                    uint64_t t36 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t34 * 8));
                    uint64_t t37 = t36;
                    uint64_t t40 = GM_LD(uint64_t, ((uint8_t *)&t37));
                    gm_l5 = t40;
                    {
                        uint64_t t43 = gm_l5;
                        uint64_t t44 = gm_l1;
                        uint8_t t45 = (t43 >= t44);
                        if (!t45) goto ie42;
                        {
                            gm_tf1bjg81u0glv t47;
                            uint64_t t48 = gm_l5;
                            GM_ST(uint64_t, (uint8_t *)&t47 + 0, t48);
                            gm_b16_8 t49 = gm_l2;
                            GM_ST(gm_b16_8, (uint8_t *)&t47 + 8, t49);
                            gm_tabb710d9mq2e t50;
                            gm_f_format_ksi8bsm((void *)&t50, (void *)NULL, cx, NULL, &t47);
                            if (gm_unw.kind) {
                                goto gm_ll31;
                            }
                            gm_tabb710d9mq2e t51 = t50;
                            if (((((uint8_t *)&t51))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t51) + 8, gm_src6d57grm686e3, 336, 29);
                            gm_b16_8 t52 = GM_LD(gm_b16_8, (((uint8_t *)&t51) + 8));
                            gm_b16_8 t53 = t52;
                            gm_b16_8 t54 = GM_LD(gm_b16_8, ((uint8_t *)&t53));
                            gm_th2lqbo77pvsc t55;
                            GM_ST(uint8_t, (uint8_t *)&t55 + 17, 0);
                            GM_ST(uint16_t, (uint8_t *)&t55 + 18, 0);
                            GM_ST(uint32_t, (uint8_t *)&t55 + 20, 0);
                            GM_ST(gm_b16_8, ((uint8_t *)&t55), t54);
                            ((uint8_t *)&t55)[16] = 1;
                            uint8_t t56 = 0;
                            F.ret = t55;
                            goto gm_fret;
                        }
                        goto ix42;
ie42:;
ix42:;
                    }
                    goto me38;
mn39:;
                    gm_trap("no match arm matched the scrutinee", gm_src6d57grm686e3, 335, 5);
me38:;
                }
lc27:;
                {
                    uint64_t t58 = gm_l4;
                    uint64_t r59; bool o60 = __builtin_add_overflow(t58, ((uint64_t)1ULL), &r59);
                    uint64_t t61 = r59;
                    gm_l4 = t61;
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
        {
            gm_l7 = ((uint64_t)0ULL);
        }
        {
            uint64_t t63 = gm_l1;
            gm_l6 = t63;
        }
        {
            for (;;) {
                uint64_t t65 = gm_l7;
                uint64_t t66 = gm_l6;
                uint8_t t67 = (t65 < t66);
                if (!t67) goto lb64;
                {
                    uint64_t t70 = gm_l7;
                    uint64_t t71 = t70;
                    uint64_t t74 = GM_LD(uint64_t, ((uint8_t *)&t71));
                    gm_l8 = t74;
                    {
                        gm_l9 = ((uint8_t)0);
                    }
                    {
                        {
                            gm_b16_8 t77 = GM_LD(gm_b16_8, (((uint8_t *)a0) + 32));
                            gm_l10 = t77;
                        }
                        {
                            gm_l11 = ((uint64_t)0ULL);
                        }
                        {
                            for (;;) {
                                uint64_t t79 = gm_l11;
                                uint64_t t80 = GM_LD(uintptr_t, ((uint8_t *)&gm_l10) + 8);
                                uint8_t t81 = (t79 < t80);
                                if (!t81) goto lb78;
                                {
                                    uint64_t t84 = gm_l11;
                                    uint64_t t85 = (uintptr_t)t84;
                                    uint64_t t86 = GM_LD(uintptr_t, ((uint8_t *)&gm_l10) + 8);
                                    uint64_t t87 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l10)) + t85 * 8));
                                    uint64_t t88 = t87;
                                    uint64_t t91 = GM_LD(uint64_t, ((uint8_t *)&t88));
                                    gm_l12 = t91;
                                    {
                                        uint64_t t94 = gm_l12;
                                        uint64_t t95 = gm_l8;
                                        uint8_t t96 = ((t94 == t95));
                                        if (!t96) goto ie93;
                                        {
                                            gm_l9 = ((uint8_t)1);
                                        }
                                        goto ix93;
ie93:;
ix93:;
                                    }
                                    goto me89;
mn90:;
                                    gm_trap("no match arm matched the scrutinee", gm_src6d57grm686e3, 340, 9);
me89:;
                                }
lc78:;
                                {
                                    uint64_t t99 = gm_l11;
                                    uint64_t r100; bool o101 = __builtin_add_overflow(t99, ((uint64_t)1ULL), &r100);
                                    uint64_t t102 = r100;
                                    gm_l11 = t102;
                                }
                            }
lb78:;
                        }
                    }
                    {
                        uint8_t t104 = gm_l9;
                        uint8_t t105 = (uint8_t)!t104;
                        if (!t105) goto ie103;
                        {
                            uint64_t t107 = gm_l8;
                            gm_t4ce0jd3tdro4 t108;
                            GM_ST(uint64_t, ((uint8_t *)&t108 + 0), t107);
                            gm_tabb710d9mq2e t109;
                            gm_f_format_4duro1p((void *)&t109, (void *)NULL, cx, NULL, t108);
                            if (gm_unw.kind) {
                                goto gm_ll68;
                            }
                            gm_tabb710d9mq2e t110 = t109;
                            if (((((uint8_t *)&t110))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t110) + 8, gm_src6d57grm686e3, 343, 30);
                            gm_b16_8 t111 = GM_LD(gm_b16_8, (((uint8_t *)&t110) + 8));
                            gm_b16_8 t112 = t111;
                            gm_b16_8 t113 = GM_LD(gm_b16_8, ((uint8_t *)&t112));
                            gm_th2lqbo77pvsc t114;
                            GM_ST(uint8_t, (uint8_t *)&t114 + 17, 0);
                            GM_ST(uint16_t, (uint8_t *)&t114 + 18, 0);
                            GM_ST(uint32_t, (uint8_t *)&t114 + 20, 0);
                            GM_ST(gm_b16_8, ((uint8_t *)&t114), t113);
                            ((uint8_t *)&t114)[16] = 1;
                            uint8_t t115 = 0;
                            F.ret = t114;
                            goto gm_fret;
                        }
                        goto ix103;
ie103:;
ix103:;
                    }
                    goto me72;
mn73:;
                    gm_trap("no match arm matched the scrutinee", gm_src6d57grm686e3, 338, 5);
me72:;
                }
lc64:;
                {
                    uint64_t t117 = gm_l7;
                    uint64_t r118; bool o119 = __builtin_add_overflow(t117, ((uint64_t)1ULL), &r118);
                    uint64_t t120 = r118;
                    gm_l7 = t120;
                }
            }
gm_ll68:;
            if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb64; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc64; }
            }
            goto gm_lad;
lb64:;
        }
    }
    {
        gm_th2lqbo77pvsc t121;
        memset(&t121, 0, sizeof t121);
        uint8_t t122 = 0;
        F.ret = t121;
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
    GM_STACK_CHECK(&F, 1613u);
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
                gm_tpu7cfmd3m0cd t45;
                GM_ST(uint8_t, (uint8_t *)&t45 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t45 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t45 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t45 + 24, 0);
                GM_ST(uint8_t, ((uint8_t *)&t45 + 0), ((uint8_t)1ULL));
                GM_ST(gm_b16_8, ((uint8_t *)&t45 + 8), t44);
                uint8_t t46 = 0;
                F.ret = t45;
                goto gm_fret;
            }
            goto ix23;
ie23:;
ix23:;
        }
        {
            uint64_t t47 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t48 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            if (t47 >= (uintptr_t)(t48)) gm_trap_index(t47, t48, gm_src6d57grm686e3, 154, 21);
            uint8_t t49 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t47 * 1));
            GM_ST(uint8_t, (((uint8_t *)&gm_l1) + 0), t49);
        }
        {
            gm_t9qkh0rsooc8d t50 = gm_l5;
            GM_ST(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l1) + 1), t50);
        }
        {
            gm_l2 = ((uint64_t)2ULL);
        }
        goto ix20;
ie20:;
        if (!((((uint8_t *)&gm_l4))[1] != 0)) goto ie51;
        gm_t9qkh0rsooc8d t52 = GM_LD(gm_t9qkh0rsooc8d, ((uint8_t *)&gm_l4));
        gm_l6 = t52;
        {
            gm_t9qkh0rsooc8d t54 = gm_l6;
            GM_ST(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l1) + 1), t54);
        }
        {
            gm_l2 = ((uint64_t)1ULL);
        }
        goto ix51;
ie51:;
ix51:;
ix20:;
    }
    {
        gm_b16_8 t56 = gm_l0;
        gm_b16_8 t57 = t56;
        void * t58 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t57)));
        uint64_t t59 = (uintptr_t)GM_LD(uintptr_t, ((uint8_t *)&t57) + 8);
        uint64_t t60 = gm_l2;
        uint64_t t61 = (uintptr_t)t60;
        gm_tqs5p4n2tq19v t62;
        if (t61 < t59) {
        uint8_t t63 = GM_LD(uint8_t, ((uint8_t *)t58 + t61 * 1));
        gm_tqs5p4n2tq19v t64;
        GM_ST(uint8_t, ((uint8_t *)&t64), t63);
        ((uint8_t *)&t64)[1] = 1;
            t62 = t64;
        } else {
        gm_tqs5p4n2tq19v t65;
        memset(&t65, 0, sizeof t65);
            t62 = t65;
        }
        gm_tqs5p4n2tq19v t66;
        GM_ST(uint8_t, ((uint8_t *)&t66), ((uint8_t)43ULL));
        ((uint8_t *)&t66)[1] = 1;
        gm_tqs5p4n2tq19v t67 = t62;
        gm_tqs5p4n2tq19v t68 = t66;
        uint8_t t69 = (gm_eqb44cb3cf1n(((uint8_t *)&t67), ((uint8_t *)&t68)));
        if (!t69) goto ie55;
        {
            GM_ST(uint8_t, (((uint8_t *)&gm_l1) + 2), ((uint8_t)1));
        }
        {
            uint64_t t71 = gm_l2;
            uint64_t r72; bool o73 = __builtin_add_overflow(t71, ((uint64_t)1ULL), &r72);
            if (o73) gm_trap("integer overflow in `+`", gm_src6d57grm686e3, 163, 9);
            uint64_t t74 = r72;
            gm_l2 = t74;
        }
        goto ix55;
ie55:;
ix55:;
    }
    {
        gm_b16_8 t76 = gm_l0;
        gm_b16_8 t77 = t76;
        void * t78 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t77)));
        uint64_t t79 = (uintptr_t)GM_LD(uintptr_t, ((uint8_t *)&t77) + 8);
        uint64_t t80 = gm_l2;
        uint64_t t81 = (uintptr_t)t80;
        gm_tqs5p4n2tq19v t82;
        if (t81 < t79) {
        uint8_t t83 = GM_LD(uint8_t, ((uint8_t *)t78 + t81 * 1));
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
        GM_ST(uint8_t, ((uint8_t *)&t86), ((uint8_t)35ULL));
        ((uint8_t *)&t86)[1] = 1;
        gm_tqs5p4n2tq19v t87 = t82;
        gm_tqs5p4n2tq19v t88 = t86;
        uint8_t t89 = (gm_eqb44cb3cf1n(((uint8_t *)&t87), ((uint8_t *)&t88)));
        if (!t89) goto ie75;
        {
            GM_ST(uint8_t, (((uint8_t *)&gm_l1) + 3), ((uint8_t)1));
        }
        {
            uint64_t t91 = gm_l2;
            uint64_t r92; bool o93 = __builtin_add_overflow(t91, ((uint64_t)1ULL), &r92);
            if (o93) gm_trap("integer overflow in `+`", gm_src6d57grm686e3, 167, 9);
            uint64_t t94 = r92;
            gm_l2 = t94;
        }
        goto ix75;
ie75:;
ix75:;
    }
    {
        gm_b16_8 t96 = gm_l0;
        gm_b16_8 t97 = t96;
        void * t98 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t97)));
        uint64_t t99 = (uintptr_t)GM_LD(uintptr_t, ((uint8_t *)&t97) + 8);
        uint64_t t100 = gm_l2;
        uint64_t t101 = (uintptr_t)t100;
        gm_tqs5p4n2tq19v t102;
        if (t101 < t99) {
        uint8_t t103 = GM_LD(uint8_t, ((uint8_t *)t98 + t101 * 1));
        gm_tqs5p4n2tq19v t104;
        GM_ST(uint8_t, ((uint8_t *)&t104), t103);
        ((uint8_t *)&t104)[1] = 1;
            t102 = t104;
        } else {
        gm_tqs5p4n2tq19v t105;
        memset(&t105, 0, sizeof t105);
            t102 = t105;
        }
        gm_tqs5p4n2tq19v t106;
        GM_ST(uint8_t, ((uint8_t *)&t106), ((uint8_t)48ULL));
        ((uint8_t *)&t106)[1] = 1;
        gm_tqs5p4n2tq19v t107 = t102;
        gm_tqs5p4n2tq19v t108 = t106;
        uint8_t t109 = (gm_eqb44cb3cf1n(((uint8_t *)&t107), ((uint8_t *)&t108)));
        if (!t109) goto ie95;
        {
            GM_ST(uint8_t, (((uint8_t *)&gm_l1) + 4), ((uint8_t)1));
        }
        {
            uint64_t t111 = gm_l2;
            uint64_t r112; bool o113 = __builtin_add_overflow(t111, ((uint64_t)1ULL), &r112);
            if (o113) gm_trap("integer overflow in `+`", gm_src6d57grm686e3, 171, 9);
            uint64_t t114 = r112;
            gm_l2 = t114;
        }
        goto ix95;
ie95:;
ix95:;
    }
    {
        gm_b16_8 t115 = gm_l0;
        uint64_t t116 = gm_l2;
        gm_f_number_at_esf7qkk((void *)&gm_l7, t115, t116);
    }
    {
        uint8_t t118 = GM_LD(uint8_t, (((uint8_t *)&gm_l7) + 16));
        if (!t118) goto ie117;
        {
            gm_b16_8 t120;
            GM_ST(void *, (uint8_t *)&t120, (void *)(gm_sutmkmalrac6l));
            GM_ST(uintptr_t, (uint8_t *)&t120 + 8, (uintptr_t)(44));
            gm_tpu7cfmd3m0cd t121;
            GM_ST(uint8_t, (uint8_t *)&t121 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t121 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t121 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t121 + 24, 0);
            GM_ST(uint8_t, ((uint8_t *)&t121 + 0), ((uint8_t)1ULL));
            GM_ST(gm_b16_8, ((uint8_t *)&t121 + 8), t120);
            uint8_t t122 = 0;
            F.ret = t121;
            goto gm_fret;
        }
        goto ix117;
ie117:;
ix117:;
    }
    {
        if (!(((((uint8_t *)&gm_l7) + 0))[4] != 0)) goto ie123;
        uint32_t t124 = GM_LD(uint32_t, (((uint8_t *)&gm_l7) + 0));
        gm_l8 = t124;
        {
            uint32_t t126 = gm_l8;
            GM_ST(uint32_t, (((uint8_t *)&gm_l1) + 8), t126);
        }
        goto ix123;
ie123:;
ix123:;
    }
    {
        uint64_t t127 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
        gm_l2 = t127;
    }
    {
        gm_b16_8 t129 = gm_l0;
        gm_b16_8 t130 = t129;
        void * t131 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t130)));
        uint64_t t132 = (uintptr_t)GM_LD(uintptr_t, ((uint8_t *)&t130) + 8);
        uint64_t t133 = gm_l2;
        uint64_t t134 = (uintptr_t)t133;
        gm_tqs5p4n2tq19v t135;
        if (t134 < t132) {
        uint8_t t136 = GM_LD(uint8_t, ((uint8_t *)t131 + t134 * 1));
        gm_tqs5p4n2tq19v t137;
        GM_ST(uint8_t, ((uint8_t *)&t137), t136);
        ((uint8_t *)&t137)[1] = 1;
            t135 = t137;
        } else {
        gm_tqs5p4n2tq19v t138;
        memset(&t138, 0, sizeof t138);
            t135 = t138;
        }
        gm_tqs5p4n2tq19v t139;
        GM_ST(uint8_t, ((uint8_t *)&t139), ((uint8_t)46ULL));
        ((uint8_t *)&t139)[1] = 1;
        gm_tqs5p4n2tq19v t140 = t135;
        gm_tqs5p4n2tq19v t141 = t139;
        uint8_t t142 = (gm_eqb44cb3cf1n(((uint8_t *)&t140), ((uint8_t *)&t141)));
        if (!t142) goto ie128;
        {
            uint64_t t144 = gm_l2;
            uint64_t r145; bool o146 = __builtin_add_overflow(t144, ((uint64_t)1ULL), &r145);
            if (o146) gm_trap("integer overflow in `+`", gm_src6d57grm686e3, 178, 9);
            uint64_t t147 = r145;
            gm_l2 = t147;
        }
        {
            gm_b16_8 t148 = gm_l0;
            uint64_t t149 = gm_l2;
            gm_f_number_at_esf7qkk((void *)&gm_l9, t148, t149);
        }
        {
            uint8_t t151 = GM_LD(uint8_t, (((uint8_t *)&gm_l9) + 16));
            if (!t151) goto ie150;
            {
                gm_b16_8 t153;
                GM_ST(void *, (uint8_t *)&t153, (void *)(gm_sutmkmalrac6l));
                GM_ST(uintptr_t, (uint8_t *)&t153 + 8, (uintptr_t)(44));
                gm_tpu7cfmd3m0cd t154;
                GM_ST(uint8_t, (uint8_t *)&t154 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t154 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t154 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t154 + 24, 0);
                GM_ST(uint8_t, ((uint8_t *)&t154 + 0), ((uint8_t)1ULL));
                GM_ST(gm_b16_8, ((uint8_t *)&t154 + 8), t153);
                uint8_t t155 = 0;
                F.ret = t154;
                goto gm_fret;
            }
            goto ix150;
ie150:;
ix150:;
        }
        {
            gm_t9805qtonbsag t156 = GM_LD(gm_t9805qtonbsag, (((uint8_t *)&gm_l9) + 0));
            gm_t9805qtonbsag t157 = t156;
            uint32_t t158;
            if (((((uint8_t *)&t157))[4] != 0)) {
                uint32_t t159 = GM_LD(uint32_t, (((uint8_t *)&t157) + 0));
                t158 = t159;
            } else {
                gm_b16_8 t160;
                GM_ST(void *, (uint8_t *)&t160, (void *)(gm_sm35km518bh68));
                GM_ST(uintptr_t, (uint8_t *)&t160 + 8, (uintptr_t)(51));
                gm_tpu7cfmd3m0cd t161;
                GM_ST(uint8_t, (uint8_t *)&t161 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t161 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t161 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t161 + 24, 0);
                GM_ST(uint8_t, ((uint8_t *)&t161 + 0), ((uint8_t)1ULL));
                GM_ST(gm_b16_8, ((uint8_t *)&t161 + 8), t160);
                uint8_t t162 = 0;
                F.ret = t161;
                goto gm_fret;
            }
            gm_l10 = t158;
        }
        {
            uint32_t t163 = gm_l10;
            gm_t9805qtonbsag t164;
            GM_ST(uint8_t, (uint8_t *)&t164 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t164 + 6, 0);
            GM_ST(uint32_t, ((uint8_t *)&t164), t163);
            ((uint8_t *)&t164)[4] = 1;
            GM_ST(gm_t9805qtonbsag, (((uint8_t *)&gm_l1) + 12), t164);
        }
        {
            uint64_t t165 = GM_LD(uint64_t, (((uint8_t *)&gm_l9) + 8));
            gm_l2 = t165;
        }
        goto ix128;
ie128:;
ix128:;
    }
    {
        gm_b16_8 t167 = gm_l0;
        gm_b16_8 t168 = t167;
        void * t169 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t168)));
        uint64_t t170 = (uintptr_t)GM_LD(uintptr_t, ((uint8_t *)&t168) + 8);
        uint64_t t171 = gm_l2;
        uint64_t t172 = (uintptr_t)t171;
        gm_tqs5p4n2tq19v t173;
        if (t172 < t170) {
        uint8_t t174 = GM_LD(uint8_t, ((uint8_t *)t169 + t172 * 1));
        gm_tqs5p4n2tq19v t175;
        GM_ST(uint8_t, ((uint8_t *)&t175), t174);
        ((uint8_t *)&t175)[1] = 1;
            t173 = t175;
        } else {
        gm_tqs5p4n2tq19v t176;
        memset(&t176, 0, sizeof t176);
            t173 = t176;
        }
        gm_tqs5p4n2tq19v t177 = t173;
        if (!((((uint8_t *)&t177))[1] != 0)) goto ie166;
        uint8_t t178 = GM_LD(uint8_t, ((uint8_t *)&t177));
        gm_l11 = t178;
        {
            uint8_t t180 = gm_l11;
            uint8_t t181 = t180;
            gm_t9qkh0rsooc8d t182;
            uint8_t t185 = GM_LD(uint8_t, ((uint8_t *)&t181));
            if (!(t185 == ((uint8_t)120ULL))) goto mn184;
            gm_t9qkh0rsooc8d t186;
            GM_ST(uint8_t, ((uint8_t *)&t186 + 0), ((uint8_t)1ULL));
            t182 = t186;
            goto me183;
mn184:;
            uint8_t t188 = GM_LD(uint8_t, ((uint8_t *)&t181));
            if (!(t188 == ((uint8_t)88ULL))) goto mn187;
            gm_t9qkh0rsooc8d t189;
            GM_ST(uint8_t, ((uint8_t *)&t189 + 0), ((uint8_t)2ULL));
            t182 = t189;
            goto me183;
mn187:;
            uint8_t t191 = GM_LD(uint8_t, ((uint8_t *)&t181));
            if (!(t191 == ((uint8_t)111ULL))) goto mn190;
            gm_t9qkh0rsooc8d t192;
            GM_ST(uint8_t, ((uint8_t *)&t192 + 0), ((uint8_t)3ULL));
            t182 = t192;
            goto me183;
mn190:;
            uint8_t t194 = GM_LD(uint8_t, ((uint8_t *)&t181));
            if (!(t194 == ((uint8_t)98ULL))) goto mn193;
            gm_t9qkh0rsooc8d t195;
            GM_ST(uint8_t, ((uint8_t *)&t195 + 0), ((uint8_t)4ULL));
            t182 = t195;
            goto me183;
mn193:;
            uint8_t t197 = GM_LD(uint8_t, ((uint8_t *)&t181));
            if (!(t197 == ((uint8_t)101ULL))) goto mn196;
            gm_t9qkh0rsooc8d t198;
            GM_ST(uint8_t, ((uint8_t *)&t198 + 0), ((uint8_t)5ULL));
            t182 = t198;
            goto me183;
mn196:;
            uint8_t t200 = GM_LD(uint8_t, ((uint8_t *)&t181));
            if (!(t200 == ((uint8_t)113ULL))) goto mn199;
            gm_t9qkh0rsooc8d t201;
            GM_ST(uint8_t, ((uint8_t *)&t201 + 0), ((uint8_t)6ULL));
            t182 = t201;
            goto me183;
mn199:;
            gm_b16_8 t203 = gm_l0;
            gm_tnpifv20irnph t204;
            GM_ST(gm_b16_8, ((uint8_t *)&t204 + 0), t203);
            gm_tabb710d9mq2e t205;
            gm_f_format_oq31u8d((void *)&t205, (void *)NULL, cx, NULL, t204);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t206 = t205;
            if (((((uint8_t *)&t206))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t206) + 8, gm_src6d57grm686e3, 193, 30);
            gm_b16_8 t207 = GM_LD(gm_b16_8, (((uint8_t *)&t206) + 8));
            gm_b16_8 t208 = t207;
            gm_b16_8 t209 = GM_LD(gm_b16_8, ((uint8_t *)&t208));
            gm_tpu7cfmd3m0cd t210;
            GM_ST(uint8_t, (uint8_t *)&t210 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t210 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t210 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t210 + 24, 0);
            GM_ST(uint8_t, ((uint8_t *)&t210 + 0), ((uint8_t)1ULL));
            GM_ST(gm_b16_8, ((uint8_t *)&t210 + 8), t209);
            uint8_t t211 = 0;
            F.ret = t210;
            goto gm_fret;
            goto me183;
mn202:;
            gm_trap("no match arm matched the scrutinee", gm_src6d57grm686e3, 186, 21);
me183:;
            GM_ST(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l1) + 20), t182);
        }
        {
            uint64_t t212 = gm_l2;
            uint64_t r213; bool o214 = __builtin_add_overflow(t212, ((uint64_t)1ULL), &r213);
            if (o214) gm_trap("integer overflow in `+`", gm_src6d57grm686e3, 195, 9);
            uint64_t t215 = r213;
            gm_l2 = t215;
        }
        goto ix166;
ie166:;
ix166:;
    }
    {
        uint64_t t217 = gm_l2;
        uint64_t t218 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        uint8_t t219 = (!(t217 == t218));
        if (!t219) goto ie216;
        {
            gm_b16_8 t221 = gm_l0;
            gm_tnpifv20irnph t222;
            GM_ST(gm_b16_8, ((uint8_t *)&t222 + 0), t221);
            gm_tabb710d9mq2e t223;
            gm_f_format_f2fgi8v((void *)&t223, (void *)NULL, cx, NULL, t222);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t224 = t223;
            if (((((uint8_t *)&t224))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t224) + 8, gm_src6d57grm686e3, 198, 21);
            gm_b16_8 t225 = GM_LD(gm_b16_8, (((uint8_t *)&t224) + 8));
            gm_b16_8 t226 = t225;
            gm_b16_8 t227 = GM_LD(gm_b16_8, ((uint8_t *)&t226));
            gm_tpu7cfmd3m0cd t228;
            GM_ST(uint8_t, (uint8_t *)&t228 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t228 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t228 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t228 + 24, 0);
            GM_ST(uint8_t, ((uint8_t *)&t228 + 0), ((uint8_t)1ULL));
            GM_ST(gm_b16_8, ((uint8_t *)&t228 + 8), t227);
            uint8_t t229 = 0;
            F.ret = t228;
            goto gm_fret;
        }
        goto ix216;
ie216:;
ix216:;
    }
    {
        uint8_t t231 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 4));
        if (!t231) goto ie230;
        uint8_t t232 = (!(GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 1) + 0) == ((uint8_t)0ULL)));
        if (!t232) goto ie230;
        {
            gm_b16_8 t234;
            GM_ST(void *, (uint8_t *)&t234, (void *)(gm_sjil3d717dq6h));
            GM_ST(uintptr_t, (uint8_t *)&t234 + 8, (uintptr_t)(62));
            gm_tpu7cfmd3m0cd t235;
            GM_ST(uint8_t, (uint8_t *)&t235 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t235 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t235 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t235 + 24, 0);
            GM_ST(uint8_t, ((uint8_t *)&t235 + 0), ((uint8_t)1ULL));
            GM_ST(gm_b16_8, ((uint8_t *)&t235 + 8), t234);
            uint8_t t236 = 0;
            F.ret = t235;
            goto gm_fret;
        }
        goto ix230;
ie230:;
ix230:;
    }
    {
        uint8_t t237 = 0;
        uint8_t t238 = 0;
        uint8_t t239 = 0;
        uint8_t t240 = ((GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 20) + 0) == ((uint8_t)1ULL)));
        if (!t240) {
            uint8_t t241 = ((GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 20) + 0) == ((uint8_t)2ULL)));
            t239 = t241;
        } else t239 = 1;
        if (!t239) {
            uint8_t t242 = ((GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 20) + 0) == ((uint8_t)3ULL)));
            t238 = t242;
        } else t238 = 1;
        if (!t238) {
            uint8_t t243 = ((GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 20) + 0) == ((uint8_t)4ULL)));
            t237 = t243;
        } else t237 = 1;
        gm_l12 = t237;
    }
    {
        uint8_t t245 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 3));
        if (!t245) goto ie244;
        uint8_t t246 = gm_l12;
        uint8_t t247 = (uint8_t)!t246;
        if (!t247) goto ie244;
        {
            gm_b16_8 t249;
            GM_ST(void *, (uint8_t *)&t249, (void *)(gm_s8f0u3tjempi4));
            GM_ST(uintptr_t, (uint8_t *)&t249 + 8, (uintptr_t)(52));
            gm_tpu7cfmd3m0cd t250;
            GM_ST(uint8_t, (uint8_t *)&t250 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t250 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t250 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t250 + 24, 0);
            GM_ST(uint8_t, ((uint8_t *)&t250 + 0), ((uint8_t)1ULL));
            GM_ST(gm_b16_8, ((uint8_t *)&t250 + 8), t249);
            uint8_t t251 = 0;
            F.ret = t250;
            goto gm_fret;
        }
        goto ix244;
ie244:;
ix244:;
    }
    {
        uint8_t t253;
        if ((((((uint8_t *)&gm_l1) + 12))[4] != 0)) goto mn255;
        t253 = ((uint8_t)0);
        goto me254;
mn255:;
        t253 = ((uint8_t)1);
        goto me254;
mn256:;
        gm_trap("no match arm matched the scrutinee", gm_src6d57grm686e3, 205, 8);
me254:;
        if (!t253) goto ie252;
        uint8_t t257 = (!(GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 20) + 0) == ((uint8_t)0ULL)));
        if (!t257) goto ie252;
        uint8_t t258 = (!(GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 20) + 0) == ((uint8_t)5ULL)));
        if (!t258) goto ie252;
        {
            gm_b16_8 t260;
            GM_ST(void *, (uint8_t *)&t260, (void *)(gm_sfu0in59pituf));
            GM_ST(uintptr_t, (uint8_t *)&t260 + 8, (uintptr_t)(61));
            gm_tpu7cfmd3m0cd t261;
            GM_ST(uint8_t, (uint8_t *)&t261 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t261 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t261 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t261 + 24, 0);
            GM_ST(uint8_t, ((uint8_t *)&t261 + 0), ((uint8_t)1ULL));
            GM_ST(gm_b16_8, ((uint8_t *)&t261 + 8), t260);
            uint8_t t262 = 0;
            F.ret = t261;
            goto gm_fret;
        }
        goto ix252;
ie252:;
ix252:;
    }
    {
        gm_t7g88i52rrtm5 t263 = gm_l1;
        gm_tpu7cfmd3m0cd t264;
        GM_ST(uint8_t, (uint8_t *)&t264 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t264 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t264 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t264 + 0), ((uint8_t)0ULL));
        GM_ST(gm_t7g88i52rrtm5, ((uint8_t *)&t264 + 8), t263);
        uint8_t t265 = 0;
        F.ret = t264;
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
    GM_STACK_CHECK(&F, 2549u);
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
                    gm_tqs5p4n2tq19v t40 = t38;
                    gm_tqs5p4n2tq19v t41 = t39;
                    uint8_t t42 = (gm_eqb44cb3cf1n(((uint8_t *)&t40), ((uint8_t *)&t41)));
                    t35 = t42;
                } else t35 = 0;
                if (!t35) {
                    uint8_t t43 = 0;
                    uint8_t t44 = gm_l7;
                    uint8_t t45 = ((t44 == ((uint8_t)125ULL)));
                    if (t45) {
                        gm_tqs5p4n2tq19v t46 = gm_l8;
                        gm_tqs5p4n2tq19v t47;
                        GM_ST(uint8_t, ((uint8_t *)&t47), ((uint8_t)125ULL));
                        ((uint8_t *)&t47)[1] = 1;
                        gm_tqs5p4n2tq19v t48 = t46;
                        gm_tqs5p4n2tq19v t49 = t47;
                        uint8_t t50 = (gm_eqb44cb3cf1n(((uint8_t *)&t48), ((uint8_t *)&t49)));
                        t43 = t50;
                    } else t43 = 0;
                    t34 = t43;
                } else t34 = 1;
                if (!t34) goto ie33;
                {
                    void * t52 = (void *)((uint8_t *)&F.l3);
                    uint8_t t53 = gm_l7;
                    gm_tftb1odc739t5 t54 = gm_f_push_t83rg2m(t52, t53);
                    gm_tftb1odc739t5 t55 = t54;
                    if (((((uint8_t *)&t55))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t55) + 1, gm_src6d57grm686e3, 44, 13);
                    gm_b0_1 t56 = GM_LD(gm_b0_1, (((uint8_t *)&t55) + 1));
                }
                {
                    uint64_t t57 = gm_l6;
                    uint64_t r58; bool o59 = __builtin_add_overflow(t57, ((uint64_t)2ULL), &r58);
                    if (o59) gm_trap("integer overflow in `+`", gm_src6d57grm686e3, 45, 13);
                    uint64_t t60 = r58;
                    gm_l6 = t60;
                }
                goto ix33;
ie33:;
                uint8_t t62 = gm_l7;
                uint8_t t63 = ((t62 == ((uint8_t)123ULL)));
                if (!t63) goto ie61;
                {
                    uint64_t t65 = gm_l6;
                    uint64_t r66; bool o67 = __builtin_add_overflow(t65, ((uint64_t)1ULL), &r66);
                    if (o67) gm_trap("integer overflow in `+`", gm_src6d57grm686e3, 48, 18);
                    uint64_t t68 = r66;
                    gm_l9 = t68;
                }
                {
                    gm_tf5srh4mp4b3i t69;
                    memset(&t69, 0, sizeof t69);
                    gm_l10 = t69;
                }
                {
                    uint64_t t71 = gm_l9;
                    uint64_t t72 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                    uint8_t t73 = (t71 < t72);
                    if (!t73) goto ie70;
                    {
                        uint64_t t75 = gm_l9;
                        uint64_t t76 = (uintptr_t)t75;
                        uint64_t t77 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                        if (t76 >= (uintptr_t)(t77)) gm_trap_index(t76, t77, gm_src6d57grm686e3, 50, 30);
                        uint8_t t78 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t76 * 1));
                        gm_l11 = t78;
                    }
                    uint8_t t79 = 0;
                    uint8_t t80 = gm_l11;
                    uint8_t t81 = (((uint8_t)48ULL) <= t80);
                    if (t81) {
                        uint8_t t82 = gm_l11;
                        uint8_t t83 = (t82 <= ((uint8_t)57ULL));
                        t79 = t83;
                    } else t79 = 0;
                    uint8_t t84 = t79;
                    if (!t84) goto ie70;
                    {
                        uint64_t t86 = gm_l9;
                        gm_l12 = t86;
                    }
                    {
                        gm_l13 = ((uint64_t)0ULL);
                    }
                    {
                        for (;;) {
                            uint64_t t88 = gm_l9;
                            uint64_t t89 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                            uint8_t t90 = (t88 < t89);
                            if (!t90) goto lb87;
                            {
                                uint64_t t92 = gm_l9;
                                uint64_t t93 = (uintptr_t)t92;
                                uint64_t t94 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                                if (t93 >= (uintptr_t)(t94)) gm_trap_index(t93, t94, gm_src6d57grm686e3, 53, 37);
                                uint8_t t95 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t93 * 1));
                                gm_l14 = t95;
                            }
                            uint8_t t96 = 0;
                            uint8_t t97 = gm_l14;
                            uint8_t t98 = (((uint8_t)48ULL) <= t97);
                            if (t98) {
                                uint8_t t99 = gm_l14;
                                uint8_t t100 = (t99 <= ((uint8_t)57ULL));
                                t96 = t100;
                            } else t96 = 0;
                            uint8_t t101 = t96;
                            if (!t101) goto lb87;
                            {
                                uint64_t t104 = gm_l13;
                                uint64_t r105; bool o106 = __builtin_mul_overflow(t104, ((uint64_t)10ULL), &r105);
                                if (o106) gm_trap("integer overflow in `*`", gm_src6d57grm686e3, 54, 25);
                                uint64_t t107 = r105;
                                uint64_t t108 = gm_l9;
                                uint64_t t109 = (uintptr_t)t108;
                                uint64_t t110 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                                if (t109 >= (uintptr_t)(t110)) gm_trap_index(t109, t110, gm_src6d57grm686e3, 54, 40);
                                uint8_t t111 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t109 * 1));
                                uint8_t r112; bool o113 = __builtin_sub_overflow(t111, ((uint8_t)48ULL), &r112);
                                if (o113) gm_trap("integer overflow in `-`", gm_src6d57grm686e3, 54, 40);
                                uint8_t t114 = r112;
                                uint64_t t115 = (uint64_t)t114;
                                uint64_t r116; bool o117 = __builtin_add_overflow(t107, t115, &r116);
                                if (o117) gm_trap("integer overflow in `+`", gm_src6d57grm686e3, 54, 25);
                                uint64_t t118 = r116;
                                gm_l13 = t118;
                            }
                            {
                                uint64_t t120 = gm_l13;
                                uint8_t t121 = (t120 > ((uint64_t)65536ULL));
                                if (!t121) goto ie119;
                                {
                                    gm_l13 = ((uint64_t)65537ULL);
                                }
                                goto ix119;
ie119:;
ix119:;
                            }
                            {
                                uint64_t t123 = gm_l9;
                                uint64_t r124; bool o125 = __builtin_add_overflow(t123, ((uint64_t)1ULL), &r124);
                                if (o125) gm_trap("integer overflow in `+`", gm_src6d57grm686e3, 56, 21);
                                uint64_t t126 = r124;
                                gm_l9 = t126;
                            }
lc87:;
                        }
lb87:;
                    }
                    {
                        uint64_t t128 = gm_l12;
                        uint64_t t129 = (uintptr_t)t128;
                        uint64_t t130 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                        if (t129 >= (uintptr_t)(t130)) gm_trap_index(t129, t130, gm_src6d57grm686e3, 58, 20);
                        uint8_t t131 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t129 * 1));
                        uint8_t t132 = ((t131 == ((uint8_t)48ULL)));
                        if (!t132) goto ie127;
                        uint64_t t133 = gm_l9;
                        uint64_t t134 = gm_l12;
                        uint64_t r135; bool o136 = __builtin_sub_overflow(t133, t134, &r135);
                        if (o136) gm_trap("integer overflow in `-`", gm_src6d57grm686e3, 58, 40);
                        uint64_t t137 = r135;
                        uint8_t t138 = (t137 > ((uint64_t)1ULL));
                        if (!t138) goto ie127;
                        {
                            gm_tj64ui6kruni6 t140;
                            gm_b16_8 t141;
                            GM_ST(void *, (uint8_t *)&t141, (void *)(gm_s721l5hj93rdl));
                            GM_ST(uintptr_t, (uint8_t *)&t141 + 8, (uintptr_t)(49));
                            GM_ST(gm_b16_8, (uint8_t *)&t140 + 0, t141);
                            gm_tf5srh4mp4b3i t142;
                            memset(&t142, 0, sizeof t142);
                            GM_ST(gm_tf5srh4mp4b3i, (uint8_t *)&t140 + 16, t142);
                            gm_t1jll27j7ae34 t143;
                            GM_ST(uint8_t, (uint8_t *)&t143 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t143 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t143 + 4, 0);
                            GM_ST(uint64_t, (uint8_t *)&t143 + 40, 0);
                            GM_ST(uint64_t, (uint8_t *)&t143 + 48, 0);
                            GM_ST(uint8_t, ((uint8_t *)&t143 + 0), ((uint8_t)1ULL));
                            GM_ST(gm_tj64ui6kruni6, ((uint8_t *)&t143 + 8), t140);
                            uint8_t t144 = 0;
                            F.ret = t143;
                            goto gm_fret;
                        }
                        goto ix127;
ie127:;
ix127:;
                    }
                    {
                        uint64_t t146 = gm_l13;
                        uint8_t t147 = (t146 > ((uint64_t)65536ULL));
                        if (!t147) goto ie145;
                        {
                            gm_tj64ui6kruni6 t149;
                            gm_b16_8 t150;
                            GM_ST(void *, (uint8_t *)&t150, (void *)(gm_sqqi4njr3i0t4));
                            GM_ST(uintptr_t, (uint8_t *)&t150 + 8, (uintptr_t)(45));
                            GM_ST(gm_b16_8, (uint8_t *)&t149 + 0, t150);
                            gm_tf5srh4mp4b3i t151;
                            memset(&t151, 0, sizeof t151);
                            GM_ST(gm_tf5srh4mp4b3i, (uint8_t *)&t149 + 16, t151);
                            gm_t1jll27j7ae34 t152;
                            GM_ST(uint8_t, (uint8_t *)&t152 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t152 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t152 + 4, 0);
                            GM_ST(uint64_t, (uint8_t *)&t152 + 40, 0);
                            GM_ST(uint64_t, (uint8_t *)&t152 + 48, 0);
                            GM_ST(uint8_t, ((uint8_t *)&t152 + 0), ((uint8_t)1ULL));
                            GM_ST(gm_tj64ui6kruni6, ((uint8_t *)&t152 + 8), t149);
                            uint8_t t153 = 0;
                            F.ret = t152;
                            goto gm_fret;
                        }
                        goto ix145;
ie145:;
ix145:;
                    }
                    {
                        uint64_t t154 = gm_l13;
                        gm_tf5srh4mp4b3i t155;
                        GM_ST(uint8_t, (uint8_t *)&t155 + 9, 0);
                        GM_ST(uint16_t, (uint8_t *)&t155 + 10, 0);
                        GM_ST(uint32_t, (uint8_t *)&t155 + 12, 0);
                        GM_ST(uint64_t, ((uint8_t *)&t155), t154);
                        ((uint8_t *)&t155)[8] = 1;
                        gm_l10 = t155;
                    }
                    goto ix70;
ie70:;
ix70:;
                }
                {
                    uint64_t t157 = gm_l9;
                    uint64_t t158 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                    uint8_t t159 = (t157 < t158);
                    if (!t159) goto ie156;
                    uint64_t t160 = gm_l9;
                    uint64_t t161 = (uintptr_t)t160;
                    uint64_t t162 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                    if (t161 >= (uintptr_t)(t162)) gm_trap_index(t161, t162, gm_src6d57grm686e3, 66, 30);
                    uint8_t t163 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t161 * 1));
                    uint8_t t164 = ((t163 == ((uint8_t)125ULL)));
                    if (!t164) goto ie156;
                    {
                        void * t166 = (void *)((uint8_t *)&F.l1);
                        gm_b16_8 t167 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
                        gm_b16_8 t168 = t167;
                        gm_b16_8 t169 = GM_LD(gm_b16_8, ((uint8_t *)&t168));
                        gm_tftb1odc739t5 t170 = gm_f_push_3ef746b(t166, t169);
                        gm_tftb1odc739t5 t171 = t170;
                        if (((((uint8_t *)&t171))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t171) + 1, gm_src6d57grm686e3, 67, 17);
                        gm_b0_1 t172 = GM_LD(gm_b0_1, (((uint8_t *)&t171) + 1));
                    }
                    {
                        gm_f_list_uvfu1k1((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l3)), (void *)NULL, cx, NULL);
                        if (gm_unw.kind) {
                            goto gm_ll6;
                        }
                    }
                    {
                        void * t173 = (void *)((uint8_t *)&F.l2);
                        gm_t7g88i52rrtm5 t174;
                        gm_f_default_spec_9ntjq2s((void *)&t174);
                        gm_tftb1odc739t5 t175 = gm_f_push_nbmosph(t173, &t174);
                        gm_tftb1odc739t5 t176 = t175;
                        if (((((uint8_t *)&t176))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t176) + 1, gm_src6d57grm686e3, 69, 17);
                        gm_b0_1 t177 = GM_LD(gm_b0_1, (((uint8_t *)&t176) + 1));
                    }
                    {
                        uint64_t t178 = gm_l9;
                        uint64_t r179; bool o180 = __builtin_add_overflow(t178, ((uint64_t)1ULL), &r179);
                        if (o180) gm_trap("integer overflow in `+`", gm_src6d57grm686e3, 70, 21);
                        uint64_t t181 = r179;
                        gm_l6 = t181;
                    }
                    goto ix156;
ie156:;
                    uint64_t t183 = gm_l9;
                    uint64_t t184 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                    uint8_t t185 = (t183 < t184);
                    if (!t185) goto ie182;
                    uint64_t t186 = gm_l9;
                    uint64_t t187 = (uintptr_t)t186;
                    uint64_t t188 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                    if (t187 >= (uintptr_t)(t188)) gm_trap_index(t187, t188, gm_src6d57grm686e3, 71, 37);
                    uint8_t t189 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t187 * 1));
                    uint8_t t190 = ((t189 == ((uint8_t)58ULL)));
                    if (!t190) goto ie182;
                    {
                        gm_tf5srh4mp4b3i t192;
                        memset(&t192, 0, sizeof t192);
                        gm_l15 = t192;
                    }
                    {
                        uint64_t t193 = gm_l9;
                        uint64_t r194; bool o195 = __builtin_add_overflow(t193, ((uint64_t)1ULL), &r194);
                        if (o195) gm_trap("integer overflow in `+`", gm_src6d57grm686e3, 73, 22);
                        uint64_t t196 = r194;
                        gm_l16 = t196;
                    }
                    {
                        for (;;) {
                            uint64_t t198 = gm_l16;
                            uint64_t t199 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                            uint8_t t200 = (t198 < t199);
                            if (!t200) goto lb197;
                            {
                                uint64_t t204 = gm_l16;
                                uint64_t t205 = (uintptr_t)t204;
                                uint64_t t206 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                                if (t205 >= (uintptr_t)(t206)) gm_trap_index(t205, t206, gm_src6d57grm686e3, 75, 24);
                                uint8_t t207 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t205 * 1));
                                uint8_t t208 = ((t207 == ((uint8_t)125ULL)));
                                if (!t208) goto ie203;
                                {
                                    uint64_t t210 = gm_l16;
                                    gm_tf5srh4mp4b3i t211;
                                    GM_ST(uint8_t, (uint8_t *)&t211 + 9, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t211 + 10, 0);
                                    GM_ST(uint32_t, (uint8_t *)&t211 + 12, 0);
                                    GM_ST(uint64_t, ((uint8_t *)&t211), t210);
                                    ((uint8_t *)&t211)[8] = 1;
                                    gm_l15 = t211;
                                }
                                {
                                    goto lb197;
                                }
                                goto ix203;
ie203:;
ix203:;
                            }
                            {
                                uint64_t t212 = gm_l16;
                                uint64_t r213; bool o214 = __builtin_add_overflow(t212, ((uint64_t)1ULL), &r213);
                                if (o214) gm_trap("integer overflow in `+`", gm_src6d57grm686e3, 79, 21);
                                uint64_t t215 = r213;
                                gm_l16 = t215;
                            }
lc197:;
                        }
lb197:;
                    }
                    {
                        gm_tf5srh4mp4b3i t216 = gm_l15;
                        gm_tf5srh4mp4b3i t217 = t216;
                        uint64_t t218;
                        if (((((uint8_t *)&t217))[8] != 0)) {
                            uint64_t t219 = GM_LD(uint64_t, (((uint8_t *)&t217) + 0));
                            t218 = t219;
                        } else {
                            gm_tj64ui6kruni6 t220;
                            gm_b16_8 t221;
                            GM_ST(void *, (uint8_t *)&t221, (void *)(gm_sl9bj93djc5da));
                            GM_ST(uintptr_t, (uint8_t *)&t221 + 8, (uintptr_t)(37));
                            GM_ST(gm_b16_8, (uint8_t *)&t220 + 0, t221);
                            gm_tf5srh4mp4b3i t222;
                            memset(&t222, 0, sizeof t222);
                            GM_ST(gm_tf5srh4mp4b3i, (uint8_t *)&t220 + 16, t222);
                            gm_t1jll27j7ae34 t223;
                            GM_ST(uint8_t, (uint8_t *)&t223 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t223 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t223 + 4, 0);
                            GM_ST(uint64_t, (uint8_t *)&t223 + 40, 0);
                            GM_ST(uint64_t, (uint8_t *)&t223 + 48, 0);
                            GM_ST(uint8_t, ((uint8_t *)&t223 + 0), ((uint8_t)1ULL));
                            GM_ST(gm_tj64ui6kruni6, ((uint8_t *)&t223 + 8), t220);
                            uint8_t t224 = 0;
                            F.ret = t223;
                            goto gm_fret;
                        }
                        gm_l17 = t218;
                    }
                    {
                        uint64_t t225 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                        void * t226 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)));
                        uint64_t t227 = gm_l9;
                        uint64_t r228; bool o229 = __builtin_add_overflow(t227, ((uint64_t)1ULL), &r228);
                        if (o229) gm_trap("integer overflow in `+`", gm_src6d57grm686e3, 82, 35);
                        uint64_t t230 = r228;
                        uint64_t t231 = (uintptr_t)t230;
                        uint64_t t232 = gm_l17;
                        uint64_t t233 = (uintptr_t)t232;
                        if (t231 > t233 || t233 > (uintptr_t)t225) gm_trap_slice(t231, t233, t225, gm_src6d57grm686e3, 82, 33);
                        gm_b16_8 t234;
                        GM_ST(void *, (uint8_t *)&t234, (void *)(((uint8_t *)t226 + t231 * 1)));
                        GM_ST(uintptr_t, (uint8_t *)&t234 + 8, (uintptr_t)(t233 - t231));
                        gm_f_parse_spec_iod83cp((void *)&gm_l18, (void *)NULL, cx, NULL, t234);
                        if (gm_unw.kind) {
                            goto gm_ll6;
                        }
                    }
                    {
                        gm_t7g88i52rrtm5 t235;
                        uint8_t t238 = GM_LD(uint8_t, (((uint8_t *)&gm_l18) + 0));
                        if (t238 != ((uint8_t)0ULL)) goto mn237;
                        gm_t7g88i52rrtm5 t239 = GM_LD(gm_t7g88i52rrtm5, (((uint8_t *)&gm_l18) + 8));
                        gm_l19 = t239;
                        gm_t7g88i52rrtm5 t240 = gm_l19;
                        t235 = t240;
                        goto me236;
mn237:;
                        uint8_t t242 = GM_LD(uint8_t, (((uint8_t *)&gm_l18) + 0));
                        if (t242 != ((uint8_t)1ULL)) goto mn241;
                        gm_b16_8 t243 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l18) + 8));
                        gm_l20 = t243;
                        gm_tj64ui6kruni6 t244;
                        gm_b16_8 t245 = gm_l20;
                        GM_ST(gm_b16_8, (uint8_t *)&t244 + 0, t245);
                        gm_tf5srh4mp4b3i t246;
                        memset(&t246, 0, sizeof t246);
                        GM_ST(gm_tf5srh4mp4b3i, (uint8_t *)&t244 + 16, t246);
                        gm_t1jll27j7ae34 t247;
                        GM_ST(uint8_t, (uint8_t *)&t247 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t247 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t247 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t247 + 40, 0);
                        GM_ST(uint64_t, (uint8_t *)&t247 + 48, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t247 + 0), ((uint8_t)1ULL));
                        GM_ST(gm_tj64ui6kruni6, ((uint8_t *)&t247 + 8), t244);
                        uint8_t t248 = 0;
                        F.ret = t247;
                        goto gm_fret;
                        goto me236;
mn241:;
                        gm_trap("no match arm matched the scrutinee", gm_src6d57grm686e3, 83, 25);
me236:;
                        gm_l21 = t235;
                    }
                    {
                        void * t249 = (void *)((uint8_t *)&F.l1);
                        gm_b16_8 t250 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
                        gm_b16_8 t251 = t250;
                        gm_b16_8 t252 = GM_LD(gm_b16_8, ((uint8_t *)&t251));
                        gm_tftb1odc739t5 t253 = gm_f_push_3ef746b(t249, t252);
                        gm_tftb1odc739t5 t254 = t253;
                        if (((((uint8_t *)&t254))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t254) + 1, gm_src6d57grm686e3, 87, 17);
                        gm_b0_1 t255 = GM_LD(gm_b0_1, (((uint8_t *)&t254) + 1));
                    }
                    {
                        gm_f_list_uvfu1k1((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l3)), (void *)NULL, cx, NULL);
                        if (gm_unw.kind) {
                            goto gm_ll6;
                        }
                    }
                    {
                        void * t256 = (void *)((uint8_t *)&F.l2);
                        gm_tftb1odc739t5 t257 = gm_f_push_nbmosph(t256, &(*(gm_t7g88i52rrtm5 *)((uint8_t *)&gm_l21)));
                        gm_tftb1odc739t5 t258 = t257;
                        if (((((uint8_t *)&t258))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t258) + 1, gm_src6d57grm686e3, 89, 17);
                        gm_b0_1 t259 = GM_LD(gm_b0_1, (((uint8_t *)&t258) + 1));
                    }
                    {
                        uint64_t t260 = gm_l17;
                        uint64_t r261; bool o262 = __builtin_add_overflow(t260, ((uint64_t)1ULL), &r261);
                        if (o262) gm_trap("integer overflow in `+`", gm_src6d57grm686e3, 90, 21);
                        uint64_t t263 = r261;
                        gm_l6 = t263;
                    }
                    goto ix182;
ie182:;
                    {
                        gm_tj64ui6kruni6 t265;
                        gm_b16_8 t266;
                        GM_ST(void *, (uint8_t *)&t266, (void *)(gm_s4k55k154s62m));
                        GM_ST(uintptr_t, (uint8_t *)&t266 + 8, (uintptr_t)(119));
                        GM_ST(gm_b16_8, (uint8_t *)&t265 + 0, t266);
                        gm_tf5srh4mp4b3i t267;
                        memset(&t267, 0, sizeof t267);
                        GM_ST(gm_tf5srh4mp4b3i, (uint8_t *)&t265 + 16, t267);
                        gm_t1jll27j7ae34 t268;
                        GM_ST(uint8_t, (uint8_t *)&t268 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t268 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t268 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t268 + 40, 0);
                        GM_ST(uint64_t, (uint8_t *)&t268 + 48, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t268 + 0), ((uint8_t)1ULL));
                        GM_ST(gm_tj64ui6kruni6, ((uint8_t *)&t268 + 8), t265);
                        uint8_t t269 = 0;
                        F.ret = t268;
                        goto gm_fret;
                    }
ix182:;
ix156:;
                }
                {
                    if (!((((uint8_t *)&gm_l10))[8] != 0)) goto ie270;
                    uint64_t t271 = GM_LD(uint64_t, ((uint8_t *)&gm_l10));
                    gm_l22 = t271;
                    {
                        void * t273 = (void *)((uint8_t *)&F.l4);
                        uint64_t t274 = gm_l22;
                        gm_tftb1odc739t5 t275 = gm_f_push_ucgmlo2(t273, t274);
                        gm_tftb1odc739t5 t276 = t275;
                        if (((((uint8_t *)&t276))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t276) + 1, gm_src6d57grm686e3, 95, 17);
                        gm_b0_1 t277 = GM_LD(gm_b0_1, (((uint8_t *)&t276) + 1));
                    }
                    goto ix270;
ie270:;
                    {
                        uint64_t t279 = gm_l5;
                        uint64_t r280; bool o281 = __builtin_add_overflow(t279, ((uint64_t)1ULL), &r280);
                        if (o281) gm_trap("integer overflow in `+`", gm_src6d57grm686e3, 97, 17);
                        uint64_t t282 = r280;
                        gm_l5 = t282;
                    }
ix270:;
                }
                goto ix61;
ie61:;
                uint8_t t284 = gm_l7;
                uint8_t t285 = ((t284 == ((uint8_t)125ULL)));
                if (!t285) goto ie283;
                {
                    gm_tj64ui6kruni6 t287;
                    gm_b16_8 t288;
                    GM_ST(void *, (uint8_t *)&t288, (void *)(gm_smvgcg1j21t6v));
                    GM_ST(uintptr_t, (uint8_t *)&t288 + 8, (uintptr_t)(26));
                    GM_ST(gm_b16_8, (uint8_t *)&t287 + 0, t288);
                    uint64_t t289 = gm_l6;
                    gm_tf5srh4mp4b3i t290;
                    GM_ST(uint8_t, (uint8_t *)&t290 + 9, 0);
                    GM_ST(uint16_t, (uint8_t *)&t290 + 10, 0);
                    GM_ST(uint32_t, (uint8_t *)&t290 + 12, 0);
                    GM_ST(uint64_t, ((uint8_t *)&t290), t289);
                    ((uint8_t *)&t290)[8] = 1;
                    GM_ST(gm_tf5srh4mp4b3i, (uint8_t *)&t287 + 16, t290);
                    gm_t1jll27j7ae34 t291;
                    GM_ST(uint8_t, (uint8_t *)&t291 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t291 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t291 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t291 + 40, 0);
                    GM_ST(uint64_t, (uint8_t *)&t291 + 48, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t291 + 0), ((uint8_t)1ULL));
                    GM_ST(gm_tj64ui6kruni6, ((uint8_t *)&t291 + 8), t287);
                    uint8_t t292 = 0;
                    F.ret = t291;
                    goto gm_fret;
                }
                goto ix283;
ie283:;
                {
                    void * t294 = (void *)((uint8_t *)&F.l3);
                    uint8_t t295 = gm_l7;
                    gm_tftb1odc739t5 t296 = gm_f_push_t83rg2m(t294, t295);
                    gm_tftb1odc739t5 t297 = t296;
                    if (((((uint8_t *)&t297))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t297) + 1, gm_src6d57grm686e3, 102, 13);
                    gm_b0_1 t298 = GM_LD(gm_b0_1, (((uint8_t *)&t297) + 1));
                }
                {
                    uint64_t t299 = gm_l6;
                    uint64_t r300; bool o301 = __builtin_add_overflow(t299, ((uint64_t)1ULL), &r300);
                    if (o301) gm_trap("integer overflow in `+`", gm_src6d57grm686e3, 103, 13);
                    uint64_t t302 = r300;
                    gm_l6 = t302;
                }
ix283:;
ix61:;
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
        void * t303 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t304 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
        gm_b16_8 t305 = t304;
        gm_b16_8 t306 = GM_LD(gm_b16_8, ((uint8_t *)&t305));
        gm_tftb1odc739t5 t307 = gm_f_push_3ef746b(t303, t306);
        gm_tftb1odc739t5 t308 = t307;
        if (((((uint8_t *)&t308))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t308) + 1, gm_src6d57grm686e3, 106, 5);
        gm_b0_1 t309 = GM_LD(gm_b0_1, (((uint8_t *)&t308) + 1));
    }
    {
        uint64_t t311 = gm_l5;
        uint8_t t312 = (t311 > ((uint64_t)0ULL));
        if (!t312) goto ie310;
        uint64_t t313 = GM_LD(uintptr_t, (((uint8_t *)&F.l4) + 0) + 8);
        uint8_t t314 = (t313 > ((uint64_t)0ULL));
        if (!t314) goto ie310;
        {
            gm_tj64ui6kruni6 t316;
            gm_b16_8 t317;
            GM_ST(void *, (uint8_t *)&t317, (void *)(gm_sppcm1qavsp1k));
            GM_ST(uintptr_t, (uint8_t *)&t317 + 8, (uintptr_t)(79));
            GM_ST(gm_b16_8, (uint8_t *)&t316 + 0, t317);
            gm_tf5srh4mp4b3i t318;
            memset(&t318, 0, sizeof t318);
            GM_ST(gm_tf5srh4mp4b3i, (uint8_t *)&t316 + 16, t318);
            gm_t1jll27j7ae34 t319;
            GM_ST(uint8_t, (uint8_t *)&t319 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t319 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t319 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t319 + 40, 0);
            GM_ST(uint64_t, (uint8_t *)&t319 + 48, 0);
            GM_ST(uint8_t, ((uint8_t *)&t319 + 0), ((uint8_t)1ULL));
            GM_ST(gm_tj64ui6kruni6, ((uint8_t *)&t319 + 8), t316);
            uint8_t t320 = 0;
            F.ret = t319;
            goto gm_fret;
        }
        goto ix310;
ie310:;
ix310:;
    }
    {
        gm_tfeab279c16rr t321;
        gm_b16_8 t322 = GM_LD(gm_b16_8, (((uint8_t *)&F.l1) + 0));
        gm_b16_8 t323 = t322;
        gm_b16_8 t324 = GM_LD(gm_b16_8, ((uint8_t *)&t323));
        GM_ST(gm_b16_8, (uint8_t *)&t321 + 0, t324);
        gm_b16_8 t325 = GM_LD(gm_b16_8, (((uint8_t *)&F.l2) + 0));
        gm_b16_8 t326 = t325;
        gm_b16_8 t327 = GM_LD(gm_b16_8, ((uint8_t *)&t326));
        GM_ST(gm_b16_8, (uint8_t *)&t321 + 16, t327);
        gm_b16_8 t328 = GM_LD(gm_b16_8, (((uint8_t *)&F.l4) + 0));
        gm_b16_8 t329 = t328;
        gm_b16_8 t330 = GM_LD(gm_b16_8, ((uint8_t *)&t329));
        GM_ST(gm_b16_8, (uint8_t *)&t321 + 32, t330);
        gm_t1jll27j7ae34 t331;
        GM_ST(uint8_t, (uint8_t *)&t331 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t331 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t331 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t331 + 0), ((uint8_t)0ULL));
        GM_ST(gm_tfeab279c16rr, ((uint8_t *)&t331 + 8), t321);
        uint8_t t332 = 0;
        F.ret = t331;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_placeholders_text_rtvia6b(void *env, gm_node *cx, gm_node *inst, uint64_t a0) {
    struct gm_fr_placeholders_text_rtvia6b F;
    uint64_t gm_l0; /* n */
    GM_STACK_CHECK(&F, 225u);
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
    gm_tabb710d9mq2e t12 = t11;
    if (((((uint8_t *)&t12))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t12) + 8, gm_src6d57grm686e3, 348, 97);
    gm_b16_8 t13 = GM_LD(gm_b16_8, (((uint8_t *)&t12) + 8));
    gm_b16_8 t14 = t13;
    gm_b16_8 t15 = t14;
    gm_b16_8 t16 = GM_LD(gm_b16_8, ((uint8_t *)&t15));
    t1 = t16;
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
__attribute__((aligned(4))) gm_b0_1 gm_f_resolve_write_e97fjom(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2) {
    struct gm_fr_resolve_write_e97fjom F;
    void * gm_l0; /* s */
    void * gm_l1; /* mc */
    void * gm_l2; /* e */
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
    GM_STACK_CHECK(&F, 4742u);
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
        gm_t9i38ih07bojq t5 = GM_LD(gm_t9i38ih07bojq, ((((uint8_t *)t3) + 8) + 8));
        F.l3 = t5;
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
            gm_t5brlu7fal70j t14;
            memset(&t14, 0, sizeof t14);
            GM_ST(uint8_t, ((uint8_t *)&t14 + 0), ((uint8_t)35ULL));
            GM_ST(gm_b16_8, ((uint8_t *)&t14 + 8), t13);
            GM_ST(gm_t5brlu7fal70j, t12, t14);
        }
        {
            void * t16 = GM_LD(void *, (((uint8_t *)&F.l3) + 40));
            uint8_t t17 = GM_LD(uint8_t, ((((uint8_t *)t16) + 8) + 0));
            if (t17 != ((uint8_t)5ULL)) goto ie15;
            gm_b16_8 t18 = GM_LD(gm_b16_8, ((((uint8_t *)t16) + 8) + 8));
            gm_l6 = t18;
            {
                gm_b16_8 t20 = gm_l6;
                gm_t1jll27j7ae34 t21;
                gm_f_parse_template_rlpl2k8((void *)&t21, (void *)NULL, cx, NULL, t20);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_t1jll27j7ae34 t22 = t21;
                gm_tfeab279c16rr t23;
                uint8_t t26 = GM_LD(uint8_t, (((uint8_t *)&t22) + 0));
                if (t26 != ((uint8_t)0ULL)) goto mn25;
                gm_tfeab279c16rr t27 = GM_LD(gm_tfeab279c16rr, (((uint8_t *)&t22) + 8));
                F.l7 = t27;
                gm_tfeab279c16rr t28 = F.l7;
                t23 = t28;
                goto me24;
mn25:;
                uint8_t t30 = GM_LD(uint8_t, (((uint8_t *)&t22) + 0));
                if (t30 != ((uint8_t)1ULL)) goto mn29;
                gm_tj64ui6kruni6 t31 = GM_LD(gm_tj64ui6kruni6, (((uint8_t *)&t22) + 8));
                gm_l8 = t31;
                {
                    void * t33 = gm_l0;
                    gm_tl874t2atnlh3 t34 = gm_l4;
                    gm_b16_8 t35 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l8) + 0));
                    gm_tnpifv20irnph t36;
                    GM_ST(gm_b16_8, ((uint8_t *)&t36 + 0), t35);
                    gm_tabb710d9mq2e t37;
                    gm_f_format_jvf1ma2((void *)&t37, (void *)NULL, cx, NULL, t36);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tabb710d9mq2e t38 = t37;
                    if (((((uint8_t *)&t38))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t38) + 8, gm_src6d57grm686e3, 492, 27);
                    gm_b16_8 t39 = GM_LD(gm_b16_8, (((uint8_t *)&t38) + 8));
                    gm_b16_8 t40 = t39;
                    gm_b16_8 t41 = GM_LD(gm_b16_8, ((uint8_t *)&t40));
                    gm_b0_1 t42 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t33, t34, t41);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                gm_b0_1 t43;
                uint8_t t44 = 0;
                F.ret = t43;
                goto gm_fret;
                goto me24;
mn29:;
                gm_trap("no match arm matched the scrutinee", gm_src6d57grm686e3, 489, 15);
me24:;
                F.l9 = t23;
            }
            {
                uint64_t t45 = GM_LD(uintptr_t, (((uint8_t *)&F.l9) + 16) + 8);
                gm_l10 = t45;
            }
            {
                gm_b16_8 t46 = gm_f_empty_slice_rcd2jln();
                gm_b16_8 t47 = t46;
                gm_b16_8 t48 = GM_LD(gm_b16_8, ((uint8_t *)&t47));
                gm_th2lqbo77pvsc t49;
                GM_ST(uint8_t, (uint8_t *)&t49 + 17, 0);
                GM_ST(uint16_t, (uint8_t *)&t49 + 18, 0);
                GM_ST(uint32_t, (uint8_t *)&t49 + 20, 0);
                GM_ST(gm_b16_8, ((uint8_t *)&t49), t48);
                ((uint8_t *)&t49)[16] = 1;
                gm_l11 = t49;
            }
            {
                void * t51 = GM_LD(void *, (((uint8_t *)&F.l3) + 48));
                void * t52 = t51;
                if (!(GM_LD(void *, ((uint8_t *)&t52)) != 0)) goto ie50;
                void * t53 = GM_LD(void *, ((uint8_t *)&t52));
                gm_l12 = t53;
                {
                    gm_th2lqbo77pvsc t55;
                    memset(&t55, 0, sizeof t55);
                    gm_l11 = t55;
                }
                {
                    void * t57 = gm_l5;
                    void * t58 = gm_l12;
                    gm_t88rcucg1b7uk t59 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t58) + 0));
                    gm_t53at80pfdus3 t60;
                    gm_f_kind_jcnt4hm((void *)&t60, t57, GM_CONV(gm_csguankko4lmhd, t59));
                    gm_t53at80pfdus3 t61 = t60;
                    uint8_t t62 = GM_LD(uint8_t, (((uint8_t *)&t61) + 0));
                    if (t62 != ((uint8_t)12ULL)) goto ie56;
                    gm_b16_8 t63 = GM_LD(gm_b16_8, ((((uint8_t *)&t61) + 8) + 0));
                    gm_l13 = t63;
                    {
                        gm_b16_8 t65 = gm_l13;
                        gm_th2lqbo77pvsc t66;
                        GM_ST(uint8_t, (uint8_t *)&t66 + 17, 0);
                        GM_ST(uint16_t, (uint8_t *)&t66 + 18, 0);
                        GM_ST(uint32_t, (uint8_t *)&t66 + 20, 0);
                        GM_ST(gm_b16_8, ((uint8_t *)&t66), t65);
                        ((uint8_t *)&t66)[16] = 1;
                        gm_l11 = t66;
                    }
                    goto ix56;
ie56:;
ix56:;
                }
                goto ix50;
ie50:;
ix50:;
            }
            {
                gm_th2lqbo77pvsc t67 = gm_l11;
                gm_th2lqbo77pvsc t68 = t67;
                gm_b16_8 t69;
                if (((((uint8_t *)&t68))[16] != 0)) {
                    gm_b16_8 t70 = GM_LD(gm_b16_8, (((uint8_t *)&t68) + 0));
                    t69 = t70;
                } else {
                    {
                        void * t72 = GM_LD(void *, (((uint8_t *)&F.l3) + 48));
                        void * t73 = t72;
                        void * t74;
                        if ((GM_LD(void *, ((uint8_t *)&t73)) != 0)) {
                            void * t75 = GM_LD(void *, (((uint8_t *)&t73) + 0));
                            t74 = t75;
                        } else {
                            gm_b0_1 t76;
                            uint8_t t77 = 0;
                            F.ret = t76;
                            goto gm_fret;
                        }
                        gm_l14 = t74;
                    }
                    {
                        void * t78 = gm_l0;
                        gm_tl874t2atnlh3 t79 = gm_l4;
                        gm_t6j6smj7qoctu t80;
                        uint64_t t81 = gm_l10;
                        gm_b16_8 t82 = gm_f_placeholders_text_rtvia6b((void *)NULL, cx, NULL, t81);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        GM_ST(gm_b16_8, (uint8_t *)&t80 + 0, t82);
                        void * t83 = gm_l0;
                        void * t84 = gm_l14;
                        gm_t88rcucg1b7uk t85 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t84) + 0));
                        gm_b16_8 t86 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t83, t85);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        GM_ST(gm_b16_8, (uint8_t *)&t80 + 16, t86);
                        gm_tabb710d9mq2e t87;
                        gm_f_format_cnhv2id((void *)&t87, (void *)NULL, cx, NULL, &t80);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_tabb710d9mq2e t88 = t87;
                        if (((((uint8_t *)&t88))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t88) + 8, gm_src6d57grm686e3, 505, 23);
                        gm_b16_8 t89 = GM_LD(gm_b16_8, (((uint8_t *)&t88) + 8));
                        gm_b16_8 t90 = t89;
                        gm_b16_8 t91 = GM_LD(gm_b16_8, ((uint8_t *)&t90));
                        gm_b0_1 t92 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t78, t79, t91);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    gm_b0_1 t93;
                    uint8_t t94 = 0;
                    F.ret = t93;
                    goto gm_fret;
                }
                gm_l15 = t69;
            }
            {
                uint64_t t95 = GM_LD(uintptr_t, (((uint8_t *)&F.l9) + 32) + 8);
                uint8_t t96 = (t95 > ((uint64_t)0ULL));
                gm_l16 = t96;
            }
            {
                uint8_t t98 = gm_l16;
                if (!t98) goto ie97;
                {
                    uint64_t t101 = GM_LD(uintptr_t, ((uint8_t *)&gm_l15) + 8);
                    gm_th2lqbo77pvsc t102;
                    gm_f_numbered_error_07hll20((void *)&t102, (void *)NULL, cx, NULL, &(*(gm_tfeab279c16rr *)((uint8_t *)&F.l9)), t101);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_th2lqbo77pvsc t103 = t102;
                    if (!((((uint8_t *)&t103))[16] != 0)) goto ie100;
                    gm_b16_8 t104 = GM_LD(gm_b16_8, ((uint8_t *)&t103));
                    gm_l17 = t104;
                    {
                        void * t106 = gm_l0;
                        gm_tl874t2atnlh3 t107 = gm_l4;
                        gm_b16_8 t108 = gm_l17;
                        gm_tnpifv20irnph t109;
                        GM_ST(gm_b16_8, ((uint8_t *)&t109 + 0), t108);
                        gm_tabb710d9mq2e t110;
                        gm_f_format_mlqiori((void *)&t110, (void *)NULL, cx, NULL, t109);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_tabb710d9mq2e t111 = t110;
                        if (((((uint8_t *)&t111))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t111) + 8, gm_src6d57grm686e3, 511, 27);
                        gm_b16_8 t112 = GM_LD(gm_b16_8, (((uint8_t *)&t111) + 8));
                        gm_b16_8 t113 = t112;
                        gm_b16_8 t114 = GM_LD(gm_b16_8, ((uint8_t *)&t113));
                        gm_b0_1 t115 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t106, t107, t114);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    {
                        gm_b0_1 t116;
                        uint8_t t117 = 0;
                        F.ret = t116;
                        goto gm_fret;
                    }
                    goto ix100;
ie100:;
ix100:;
                }
                goto ix97;
ie97:;
                uint64_t t119 = GM_LD(uintptr_t, ((uint8_t *)&gm_l15) + 8);
                uint64_t t120 = gm_l10;
                uint8_t t121 = (!(t119 == t120));
                if (!t121) goto ie118;
                {
                    gm_b16_8 t123;
                    uint64_t t125 = GM_LD(uintptr_t, ((uint8_t *)&gm_l15) + 8);
                    uint8_t t126 = ((t125 == ((uint64_t)1ULL)));
                    if (!t126) goto ie124;
                    gm_b16_8 t128;
                    GM_ST(void *, (uint8_t *)&t128, (void *)(gm_scne9acm49p4h));
                    GM_ST(uintptr_t, (uint8_t *)&t128 + 8, (uintptr_t)(10));
                    gm_b16_8 t129 = t128;
                    t123 = t129;
                    goto ix124;
ie124:;
                    uint64_t t131 = GM_LD(uintptr_t, ((uint8_t *)&gm_l15) + 8);
                    gm_t4ce0jd3tdro4 t132;
                    GM_ST(uint64_t, ((uint8_t *)&t132 + 0), t131);
                    gm_tabb710d9mq2e t133;
                    gm_f_format_td6patj((void *)&t133, (void *)NULL, cx, NULL, t132);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tabb710d9mq2e t134 = t133;
                    if (((((uint8_t *)&t134))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t134) + 8, gm_src6d57grm686e3, 515, 57);
                    gm_b16_8 t135 = GM_LD(gm_b16_8, (((uint8_t *)&t134) + 8));
                    gm_b16_8 t136 = t135;
                    gm_b16_8 t137 = t136;
                    gm_b16_8 t138 = GM_LD(gm_b16_8, ((uint8_t *)&t137));
                    t123 = t138;
ix124:;
                    gm_l18 = t123;
                }
                {
                    void * t139 = gm_l0;
                    gm_tl874t2atnlh3 t140 = gm_l4;
                    gm_t6j6smj7qoctu t141;
                    uint64_t t142 = gm_l10;
                    gm_b16_8 t143 = gm_f_placeholders_text_rtvia6b((void *)NULL, cx, NULL, t142);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    GM_ST(gm_b16_8, (uint8_t *)&t141 + 0, t143);
                    gm_b16_8 t144 = gm_l18;
                    GM_ST(gm_b16_8, (uint8_t *)&t141 + 16, t144);
                    gm_tabb710d9mq2e t145;
                    gm_f_format_2rf8n5r((void *)&t145, (void *)NULL, cx, NULL, &t141);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tabb710d9mq2e t146 = t145;
                    if (((((uint8_t *)&t146))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t146) + 8, gm_src6d57grm686e3, 516, 23);
                    gm_b16_8 t147 = GM_LD(gm_b16_8, (((uint8_t *)&t146) + 8));
                    gm_b16_8 t148 = t147;
                    gm_b16_8 t149 = GM_LD(gm_b16_8, ((uint8_t *)&t148));
                    gm_b0_1 t150 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t139, t140, t149);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                {
                    gm_b0_1 t151;
                    uint8_t t152 = 0;
                    F.ret = t151;
                    goto gm_fret;
                }
                goto ix118;
ie118:;
ix118:;
ix97:;
            }
            {
                {
                    gm_l20 = ((uint64_t)0ULL);
                }
                {
                    uint64_t t154 = gm_l10;
                    gm_l19 = t154;
                }
                {
                    for (;;) {
                        uint64_t t156 = gm_l20;
                        uint64_t t157 = gm_l19;
                        uint8_t t158 = (t156 < t157);
                        if (!t158) goto lb155;
                        {
                            uint64_t t161 = gm_l20;
                            uint64_t t162 = t161;
                            uint64_t t165 = GM_LD(uint64_t, ((uint8_t *)&t162));
                            gm_l21 = t165;
                            {
                                uint64_t t167;
                                uint8_t t169 = gm_l16;
                                if (!t169) goto ie168;
                                uint64_t t171 = gm_l21;
                                uint64_t t172 = (uintptr_t)t171;
                                uint64_t t173 = GM_LD(uintptr_t, (((uint8_t *)&F.l9) + 32) + 8);
                                if (t172 >= (uintptr_t)(t173)) gm_trap_index(t172, t173, gm_src6d57grm686e3, 520, 29);
                                uint64_t t174 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l9) + 32)) + t172 * 8));
                                uint64_t t175 = t174;
                                t167 = t175;
                                goto ix168;
ie168:;
                                uint64_t t177 = gm_l21;
                                uint64_t t178 = t177;
                                t167 = t178;
ix168:;
                                gm_l22 = t167;
                            }
                            {
                                void * t180 = gm_l0;
                                uint64_t t181 = gm_l21;
                                uint64_t t182 = (uintptr_t)t181;
                                uint64_t t183 = GM_LD(uintptr_t, (((uint8_t *)&F.l9) + 16) + 8);
                                if (t182 >= (uintptr_t)(t183)) gm_trap_index(t182, t183, gm_src6d57grm686e3, 521, 34);
                                void * t184 = (void *)((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l9) + 16)) + t182 * 24);
                                uint64_t t185 = gm_l22;
                                uint64_t t186 = (uintptr_t)t185;
                                uint64_t t187 = GM_LD(uintptr_t, ((uint8_t *)&gm_l15) + 8);
                                if (t186 >= (uintptr_t)(t187)) gm_trap_index(t186, t187, gm_src6d57grm686e3, 521, 51);
                                gm_t88rcucg1b7uk t188 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l15)) + t186 * 4));
                                gm_th2lqbo77pvsc t189;
                                gm_f_check_spec_4vp7jhg((void *)&t189, (void *)NULL, cx, NULL, t180, t184, t188);
                                if (gm_unw.kind) {
                                    goto gm_ll159;
                                }
                                gm_th2lqbo77pvsc t190 = t189;
                                if (!((((uint8_t *)&t190))[16] != 0)) goto ie179;
                                gm_b16_8 t191 = GM_LD(gm_b16_8, ((uint8_t *)&t190));
                                gm_l23 = t191;
                                {
                                    void * t193 = gm_l0;
                                    gm_tl874t2atnlh3 t194 = gm_l4;
                                    gm_b16_8 t195 = gm_l23;
                                    gm_tnpifv20irnph t196;
                                    GM_ST(gm_b16_8, ((uint8_t *)&t196 + 0), t195);
                                    gm_tabb710d9mq2e t197;
                                    gm_f_format_mlqiori((void *)&t197, (void *)NULL, cx, NULL, t196);
                                    if (gm_unw.kind) {
                                        goto gm_ll159;
                                    }
                                    gm_tabb710d9mq2e t198 = t197;
                                    if (((((uint8_t *)&t198))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t198) + 8, gm_src6d57grm686e3, 522, 27);
                                    gm_b16_8 t199 = GM_LD(gm_b16_8, (((uint8_t *)&t198) + 8));
                                    gm_b16_8 t200 = t199;
                                    gm_b16_8 t201 = GM_LD(gm_b16_8, ((uint8_t *)&t200));
                                    gm_b0_1 t202 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t193, t194, t201);
                                    if (gm_unw.kind) {
                                        goto gm_ll159;
                                    }
                                }
                                goto ix179;
ie179:;
ix179:;
                            }
                            goto me163;
mn164:;
                            gm_trap("no match arm matched the scrutinee", gm_src6d57grm686e3, 519, 5);
me163:;
                        }
lc155:;
                        {
                            uint64_t t204 = gm_l20;
                            uint64_t r205; bool o206 = __builtin_add_overflow(t204, ((uint64_t)1ULL), &r205);
                            uint64_t t207 = r205;
                            gm_l20 = t207;
                        }
                    }
gm_ll159:;
                    if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb155; }
                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc155; }
                    }
                    goto gm_lad;
lb155:;
                }
            }
            {
                void * t208 = GM_LD(void *, (((uint8_t *)&F.l3) + 48));
                void * t209 = t208;
                void * t210;
                if ((GM_LD(void *, ((uint8_t *)&t209)) != 0)) {
                    void * t211 = GM_LD(void *, (((uint8_t *)&t209) + 0));
                    t210 = t211;
                } else {
                    {
                        void * t213 = gm_l2;
                        uint8_t *t214 = (((uint8_t *)t213) + 8);
                        gm_tntn2tf15puib t215;
                        memcpy((uint8_t *)&t215 + 0, (((uint8_t *)&F.l3) + 0), 40);
                        gm_b16_8 t216 = GM_LD(gm_b16_8, (((uint8_t *)&F.l9) + 0));
                        GM_ST(gm_b16_8, (uint8_t *)&t215 + 40, t216);
                        gm_b16_8 t217 = GM_LD(gm_b16_8, (((uint8_t *)&F.l9) + 16));
                        GM_ST(gm_b16_8, (uint8_t *)&t215 + 56, t217);
                        gm_b16_8 t218 = gm_f_empty_slice_g3tddrb_2();
                        GM_ST(gm_b16_8, (uint8_t *)&t215 + 72, t218);
                        gm_t5brlu7fal70j t219;
                        memset(&t219, 0, sizeof t219);
                        GM_ST(uint8_t, ((uint8_t *)&t219 + 0), ((uint8_t)55ULL));
                        GM_ST(gm_tntn2tf15puib, ((uint8_t *)&t219 + 8), t215);
                        GM_ST(gm_t5brlu7fal70j, t214, t219);
                    }
                    gm_b0_1 t220;
                    uint8_t t221 = 0;
                    F.ret = t220;
                    goto gm_fret;
                }
                gm_l24 = t210;
            }
            {
                gm_f_list_ir0h3tc_2((void *)&F.l25, (void *)NULL, cx, NULL);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t222 = (void *)((uint8_t *)&F.l25);
                uint32_t t223 = GM_LD(uint32_t, (((uint8_t *)&F.l3) + 56));
                void * t224 = gm_l24;
                gm_ttggq7ng4kp3p t225;
                GM_ST(uint32_t, (uint8_t *)&t225 + 4, 0);
                GM_ST(uint32_t, ((uint8_t *)&t225 + 0), t223);
                GM_ST(void *, ((uint8_t *)&t225 + 8), t224);
                gm_trcmmd12cefb1 t226;
                GM_ST(uint8_t, (uint8_t *)&t226 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t226 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t226 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t226 + 24, 0);
                GM_ST(uint8_t, ((uint8_t *)&t226 + 0), ((uint8_t)0ULL));
                GM_ST(gm_ttggq7ng4kp3p, ((uint8_t *)&t226 + 8), t225);
                gm_tftb1odc739t5 t227 = gm_f_push_d6c76ud_2(t222, &t226);
                gm_tftb1odc739t5 t228 = t227;
                if (((((uint8_t *)&t228))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t228) + 1, gm_src6d57grm686e3, 530, 5);
                gm_b0_1 t229 = GM_LD(gm_b0_1, (((uint8_t *)&t228) + 1));
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
                    uint64_t t231 = gm_l10;
                    gm_l27 = t231;
                }
                {
                    for (;;) {
                        uint64_t t233 = gm_l28;
                        uint64_t t234 = gm_l27;
                        uint8_t t235 = (t233 < t234);
                        if (!t235) goto lb232;
                        {
                            uint64_t t238 = gm_l28;
                            uint64_t t239 = t238;
                            uint64_t t242 = GM_LD(uint64_t, ((uint8_t *)&t239));
                            gm_l29 = t242;
                            {
                                uint64_t t244;
                                uint8_t t246 = gm_l16;
                                if (!t246) goto ie245;
                                uint64_t t248 = gm_l29;
                                uint64_t t249 = (uintptr_t)t248;
                                uint64_t t250 = GM_LD(uintptr_t, (((uint8_t *)&F.l9) + 32) + 8);
                                if (t249 >= (uintptr_t)(t250)) gm_trap_index(t249, t250, gm_src6d57grm686e3, 533, 29);
                                uint64_t t251 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l9) + 32)) + t249 * 8));
                                uint64_t t252 = t251;
                                t244 = t252;
                                goto ix245;
ie245:;
                                uint64_t t254 = gm_l29;
                                uint64_t t255 = t254;
                                t244 = t255;
ix245:;
                                gm_l30 = t244;
                            }
                            {
                                void * t256 = gm_l24;
                                gm_t88rcucg1b7uk t257 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t256) + 0));
                                uint32_t t258 = GM_LD(uint32_t, (((uint8_t *)&F.l3) + 56));
                                gm_t5brlu7fal70j t259;
                                memset(&t259, 0, sizeof t259);
                                GM_ST(uint8_t, ((uint8_t *)&t259 + 0), ((uint8_t)15ULL));
                                GM_ST(uint32_t, ((uint8_t *)&t259 + 8), t258);
                                void * t260 = gm_l2;
                                gm_tl874t2atnlh3 t261 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t260) + 208));
                                void * t262 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t257, &t259, t261);
                                if (gm_unw.kind) {
                                    goto gm_ll236;
                                }
                                gm_l31 = t262;
                            }
                            {
                                void * t263 = (void *)((uint8_t *)&F.l26);
                                uint64_t t264 = gm_l30;
                                uint64_t t265 = (uintptr_t)t264;
                                uint64_t t266 = GM_LD(uintptr_t, ((uint8_t *)&gm_l15) + 8);
                                if (t265 >= (uintptr_t)(t266)) gm_trap_index(t265, t266, gm_src6d57grm686e3, 535, 28);
                                gm_t88rcucg1b7uk t267 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l15)) + t265 * 4));
                                void * t268 = gm_l31;
                                uint64_t t269 = gm_l30;
                                uint32_t t270 = (uint32_t)t269;
                                gm_tbl8il92l97ut t271;
                                GM_ST(uint32_t, (uint8_t *)&t271 + 12, 0);
                                GM_ST(void *, ((uint8_t *)&t271 + 0), t268);
                                GM_ST(uint32_t, ((uint8_t *)&t271 + 8), t270);
                                gm_t5brlu7fal70j t272;
                                memset(&t272, 0, sizeof t272);
                                GM_ST(uint8_t, ((uint8_t *)&t272 + 0), ((uint8_t)19ULL));
                                GM_ST(gm_tbl8il92l97ut, ((uint8_t *)&t272 + 8), t271);
                                void * t273 = gm_l2;
                                gm_tl874t2atnlh3 t274 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t273) + 208));
                                void * t275 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t267, &t272, t274);
                                if (gm_unw.kind) {
                                    goto gm_ll236;
                                }
                                gm_tftb1odc739t5 t276 = gm_f_push_d943vgp_2(t263, &(*(gm_tfpf0s89fpb3r *)((uint8_t *)t275)));
                                gm_tftb1odc739t5 t277 = t276;
                                if (((((uint8_t *)&t277))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t277) + 1, gm_src6d57grm686e3, 535, 9);
                                gm_b0_1 t278 = GM_LD(gm_b0_1, (((uint8_t *)&t277) + 1));
                            }
                            goto me240;
mn241:;
                            gm_trap("no match arm matched the scrutinee", gm_src6d57grm686e3, 532, 5);
me240:;
                        }
lc232:;
                        {
                            uint64_t t280 = gm_l28;
                            uint64_t r281; bool o282 = __builtin_add_overflow(t280, ((uint64_t)1ULL), &r281);
                            uint64_t t283 = r281;
                            gm_l28 = t283;
                        }
                    }
gm_ll236:;
                    if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb232; }
                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc232; }
                    }
                    goto gm_lad;
lb232:;
                }
            }
            {
                void * t284 = gm_l0;
                void * t285 = (void *)(((uint8_t *)t284) + 8);
                gm_t88rcucg1b7uk t286 = gm_f_unit_2age34u((void *)NULL, cx, NULL, t285);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tntn2tf15puib t287;
                memcpy((uint8_t *)&t287 + 0, (((uint8_t *)&F.l3) + 0), 40);
                gm_b16_8 t288 = GM_LD(gm_b16_8, (((uint8_t *)&F.l9) + 0));
                GM_ST(gm_b16_8, (uint8_t *)&t287 + 40, t288);
                gm_b16_8 t289 = GM_LD(gm_b16_8, (((uint8_t *)&F.l9) + 16));
                GM_ST(gm_b16_8, (uint8_t *)&t287 + 56, t289);
                gm_b16_8 t290 = GM_LD(gm_b16_8, (((uint8_t *)&F.l26) + 0));
                GM_ST(gm_b16_8, (uint8_t *)&t287 + 72, t290);
                gm_t5brlu7fal70j t291;
                memset(&t291, 0, sizeof t291);
                GM_ST(uint8_t, ((uint8_t *)&t291 + 0), ((uint8_t)55ULL));
                GM_ST(gm_tntn2tf15puib, ((uint8_t *)&t291 + 8), t287);
                void * t292 = gm_l2;
                gm_tl874t2atnlh3 t293 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t292) + 208));
                void * t294 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t286, &t291, t293);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l32 = t294;
            }
            {
                void * t295 = (void *)((uint8_t *)&F.l25);
                void * t296 = gm_l32;
                gm_trcmmd12cefb1 t297;
                GM_ST(uint8_t, (uint8_t *)&t297 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t297 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t297 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t297 + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&t297 + 24, 0);
                GM_ST(uint8_t, ((uint8_t *)&t297 + 0), ((uint8_t)4ULL));
                GM_ST(void *, ((uint8_t *)&t297 + 8), t296);
                gm_tftb1odc739t5 t298 = gm_f_push_d6c76ud_2(t295, &t297);
                gm_tftb1odc739t5 t299 = t298;
                if (((((uint8_t *)&t299))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t299) + 1, gm_src6d57grm686e3, 538, 5);
                gm_b0_1 t300 = GM_LD(gm_b0_1, (((uint8_t *)&t299) + 1));
            }
            {
                void * t301 = gm_l2;
                uint8_t *t302 = (((uint8_t *)t301) + 8);
                gm_tgabaikr08mjg t303;
                memset((uint8_t *)&t303 + 36, 0, 4);
                gm_b16_8 t304 = GM_LD(gm_b16_8, (((uint8_t *)&F.l25) + 0));
                GM_ST(gm_b16_8, (uint8_t *)&t303 + 0, t304);
                GM_ST(void *, (uint8_t *)&t303 + 16, ((void *)0));
                void * t305 = gm_l2;
                gm_tl874t2atnlh3 t306 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t305) + 208));
                GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t303 + 24, t306);
                gm_tq7s98ihonkju t307 = gm_f_new_dsbahke((void *)NULL, cx, NULL, &t303, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_k76fiiae2n8)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tq7s98ihonkju t308 = t307;
                if (((((uint8_t *)&t308))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t308) + 8, gm_src6d57grm686e3, 539, 21);
                void * t309 = GM_LD(void *, (((uint8_t *)&t308) + 8));
                gm_t5brlu7fal70j t310;
                memset(&t310, 0, sizeof t310);
                GM_ST(uint8_t, ((uint8_t *)&t310 + 0), ((uint8_t)46ULL));
                GM_ST(void *, ((uint8_t *)&t310 + 8), t309);
                GM_ST(gm_t5brlu7fal70j, t302, t310);
            }
            goto ix15;
ie15:;
            {
                {
                    void * t313 = gm_l0;
                    gm_tl874t2atnlh3 t314 = gm_l4;
                    gm_b16_8 t315;
                    GM_ST(void *, (uint8_t *)&t315, (void *)(gm_srundlmqvm06p));
                    GM_ST(uintptr_t, (uint8_t *)&t315 + 8, (uintptr_t)(59));
                    gm_b0_1 t316 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t313, t314, t315);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                {
                    gm_b0_1 t317;
                    uint8_t t318 = 0;
                    F.ret = t317;
                    goto gm_fret;
                }
            }
ix15:;
        }
        goto ix2;
ie2:;
        {
            gm_b0_1 t320;
            uint8_t t321 = 0;
            F.ret = t320;
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
    GM_STACK_CHECK(&F, 628u);
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
                    gm_b16_8 t75 = t74;
                    void * t76 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t75)));
                    uint64_t t77 = (uintptr_t)GM_LD(uintptr_t, ((uint8_t *)&t75) + 8);
                    uint64_t t78 = gm_l3;
                    uint64_t r79; bool o80 = __builtin_add_overflow(t78, ((uint64_t)1ULL), &r79);
                    if (o80) gm_trap("integer overflow in `+`", gm_src6d57grm686e3, 249, 22);
                    uint64_t t81 = r79;
                    uint64_t t82 = (uintptr_t)t81;
                    gm_tqs5p4n2tq19v t83;
                    if (t82 < t77) {
                    uint8_t t84 = GM_LD(uint8_t, ((uint8_t *)t76 + t82 * 1));
                    gm_tqs5p4n2tq19v t85;
                    GM_ST(uint8_t, ((uint8_t *)&t85), t84);
                    ((uint8_t *)&t85)[1] = 1;
                        t83 = t85;
                    } else {
                    gm_tqs5p4n2tq19v t86;
                    memset(&t86, 0, sizeof t86);
                        t83 = t86;
                    }
                    gm_tqs5p4n2tq19v t87;
                    GM_ST(uint8_t, ((uint8_t *)&t87), ((uint8_t)110ULL));
                    ((uint8_t *)&t87)[1] = 1;
                    gm_tqs5p4n2tq19v t88;
                    GM_ST(uint8_t, ((uint8_t *)&t88), ((uint8_t)116ULL));
                    ((uint8_t *)&t88)[1] = 1;
                    gm_tqs5p4n2tq19v t89;
                    GM_ST(uint8_t, ((uint8_t *)&t89), ((uint8_t)114ULL));
                    ((uint8_t *)&t89)[1] = 1;
                    gm_tqs5p4n2tq19v t90;
                    GM_ST(uint8_t, ((uint8_t *)&t90), ((uint8_t)48ULL));
                    ((uint8_t *)&t90)[1] = 1;
                    gm_tqs5p4n2tq19v t91;
                    GM_ST(uint8_t, ((uint8_t *)&t91), ((uint8_t)92ULL));
                    ((uint8_t *)&t91)[1] = 1;
                    gm_tqs5p4n2tq19v t92;
                    GM_ST(uint8_t, ((uint8_t *)&t92), ((uint8_t)34ULL));
                    ((uint8_t *)&t92)[1] = 1;
                    gm_tqs5p4n2tq19v t93;
                    GM_ST(uint8_t, ((uint8_t *)&t93), ((uint8_t)39ULL));
                    ((uint8_t *)&t93)[1] = 1;
                    gm_t6s1lqopkm7fs t94;
                    GM_ST(gm_tqs5p4n2tq19v, ((uint8_t *)&t94 + 0), t87);
                    GM_ST(gm_tqs5p4n2tq19v, ((uint8_t *)&t94 + 2), t88);
                    GM_ST(gm_tqs5p4n2tq19v, ((uint8_t *)&t94 + 4), t89);
                    GM_ST(gm_tqs5p4n2tq19v, ((uint8_t *)&t94 + 6), t90);
                    GM_ST(gm_tqs5p4n2tq19v, ((uint8_t *)&t94 + 8), t91);
                    GM_ST(gm_tqs5p4n2tq19v, ((uint8_t *)&t94 + 10), t92);
                    GM_ST(gm_tqs5p4n2tq19v, ((uint8_t *)&t94 + 12), t93);
                    gm_t6s1lqopkm7fs t95 = t94;
                    uint8_t t96 = 0;
                    gm_tqs5p4n2tq19v t97 = t83;
                    if (gm_eqb44cb3cf1n(((uint8_t *)&t95) + 0, ((uint8_t *)&t97))) t96 = 1;
                    if (gm_eqb44cb3cf1n(((uint8_t *)&t95) + 2, ((uint8_t *)&t97))) t96 = 1;
                    if (gm_eqb44cb3cf1n(((uint8_t *)&t95) + 4, ((uint8_t *)&t97))) t96 = 1;
                    if (gm_eqb44cb3cf1n(((uint8_t *)&t95) + 6, ((uint8_t *)&t97))) t96 = 1;
                    if (gm_eqb44cb3cf1n(((uint8_t *)&t95) + 8, ((uint8_t *)&t97))) t96 = 1;
                    if (gm_eqb44cb3cf1n(((uint8_t *)&t95) + 10, ((uint8_t *)&t97))) t96 = 1;
                    if (gm_eqb44cb3cf1n(((uint8_t *)&t95) + 12, ((uint8_t *)&t97))) t96 = 1;
                    uint8_t t98 = (uint8_t)(!t96);
                    if (!t98) goto ie73;
                    {
                        gm_tf5srh4mp4b3i t100;
                        memset(&t100, 0, sizeof t100);
                        uint8_t t101 = 0;
                        F.ret = t100;
                        goto gm_fret;
                    }
                    goto ix73;
ie73:;
ix73:;
                }
                {
                    uint64_t t102 = gm_l3;
                    uint64_t r103; bool o104 = __builtin_add_overflow(t102, ((uint64_t)2ULL), &r103);
                    if (o104) gm_trap("integer overflow in `+`", gm_src6d57grm686e3, 250, 13);
                    uint64_t t105 = r103;
                    gm_l3 = t105;
                }
                goto ix66;
ie66:;
                {
                    uint64_t t107 = gm_l3;
                    uint64_t r108; bool o109 = __builtin_add_overflow(t107, ((uint64_t)1ULL), &r108);
                    if (o109) gm_trap("integer overflow in `+`", gm_src6d57grm686e3, 252, 13);
                    uint64_t t110 = r108;
                    gm_l3 = t110;
                }
ix66:;
            }
            {
                uint64_t t111 = gm_l4;
                uint64_t r112; bool o113 = __builtin_add_overflow(t111, ((uint64_t)1ULL), &r112);
                if (o113) gm_trap("integer overflow in `+`", gm_src6d57grm686e3, 254, 9);
                uint64_t t114 = r112;
                gm_l4 = t114;
            }
lc37:;
        }
lb37:;
    }
    {
        gm_tf5srh4mp4b3i t115;
        memset(&t115, 0, sizeof t115);
        uint8_t t116 = 0;
        F.ret = t115;
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
    GM_STACK_CHECK(&F, 5880u);
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
        gm_t1jll27j7ae34 t7 = t6;
        gm_tfeab279c16rr t8;
        uint8_t t11 = GM_LD(uint8_t, (((uint8_t *)&t7) + 0));
        if (t11 != ((uint8_t)0ULL)) goto mn10;
        gm_tfeab279c16rr t12 = GM_LD(gm_tfeab279c16rr, (((uint8_t *)&t7) + 8));
        F.l6 = t12;
        gm_tfeab279c16rr t13 = F.l6;
        t8 = t13;
        goto me9;
mn10:;
        uint8_t t15 = GM_LD(uint8_t, (((uint8_t *)&t7) + 0));
        if (t15 != ((uint8_t)1ULL)) goto mn14;
        gm_tj64ui6kruni6 t16 = GM_LD(gm_tj64ui6kruni6, (((uint8_t *)&t7) + 8));
        gm_l7 = t16;
        {
            gm_tl874t2atnlh3 t18 = gm_l2;
            gm_b16_8 t19 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l7) + 0));
            gm_tnpifv20irnph t20;
            GM_ST(gm_b16_8, ((uint8_t *)&t20 + 0), t19);
            gm_tabb710d9mq2e t21;
            gm_f_format_68lf2sm((void *)&t21, (void *)NULL, cx, NULL, t20);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t22 = t21;
            if (((((uint8_t *)&t22))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t22) + 8, gm_src6d57grm686e3, 357, 44);
            gm_b16_8 t23 = GM_LD(gm_b16_8, (((uint8_t *)&t22) + 8));
            gm_b16_8 t24 = t23;
            gm_b16_8 t25 = GM_LD(gm_b16_8, ((uint8_t *)&t24));
            gm_f_error_s6hn871((void *)&F.l8, (void *)NULL, cx, NULL, t18, t25);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            if (!(((((uint8_t *)&gm_l7) + 16))[8] != 0)) goto ie26;
            uint64_t t27 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 16));
            gm_l9 = t27;
            {
                void * t29 = gm_l0;
                void * t30 = GM_LD(void *, (((uint8_t *)t29) + 0));
                void * t31 = GM_LD(void *, (((uint8_t *)t30) + 0));
                void * t32 = GM_LD(void *, (((uint8_t *)t31) + 0));
                uint32_t t33 = GM_LD(uint32_t, (((uint8_t *)&gm_l2) + 0));
                void * t34 = gm_f_file_o15fqku(t32, t33);
                uint64_t t35 = GM_LD(uintptr_t, (((uint8_t *)t34) + 16) + 8);
                void * t36 = ((uint8_t *)GM_LD(void *, (((uint8_t *)t34) + 16)));
                uint32_t t37 = GM_LD(uint32_t, (((uint8_t *)&gm_l2) + 4));
                uint64_t t38 = (uint64_t)t37;
                uint64_t t39 = (uintptr_t)t38;
                uint32_t t40 = GM_LD(uint32_t, (((uint8_t *)&gm_l2) + 8));
                uint64_t t41 = (uint64_t)t40;
                uint64_t t42 = (uintptr_t)t41;
                if (t39 > t42 || t42 > (uintptr_t)t35) gm_trap_slice(t39, t42, t35, gm_src6d57grm686e3, 360, 25);
                gm_b16_8 t43;
                GM_ST(void *, (uint8_t *)&t43, (void *)(((uint8_t *)t36 + t39 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t43 + 8, (uintptr_t)(t42 - t39));
                gm_l10 = t43;
            }
            {
                gm_b16_8 t45 = gm_l10;
                uint64_t t46 = gm_l9;
                gm_tf5srh4mp4b3i t47 = gm_f_source_offset_of_vlcjdq6(t45, t46);
                gm_tf5srh4mp4b3i t48 = t47;
                if (!((((uint8_t *)&t48))[8] != 0)) goto ie44;
                uint64_t t49 = GM_LD(uint64_t, ((uint8_t *)&t48));
                gm_l11 = t49;
                {
                    uint32_t t51 = GM_LD(uint32_t, (((uint8_t *)&gm_l2) + 4));
                    uint64_t t52 = gm_l11;
                    uint32_t t53 = (uint32_t)t52;
                    uint32_t r54; bool o55 = __builtin_add_overflow(t51, t53, &r54);
                    if (o55) gm_trap("integer overflow in `+`", gm_src6d57grm686e3, 362, 27);
                    uint32_t t56 = r54;
                    gm_l12 = t56;
                }
                {
                    void * t57 = (void *)((uint8_t *)&F.l8);
                    gm_b16_8 t58;
                    GM_ST(void *, (uint8_t *)&t58, (void *)(gm_s4087kcl9bpus));
                    GM_ST(uintptr_t, (uint8_t *)&t58 + 8, (uintptr_t)(28));
                    uint32_t t59 = GM_LD(uint32_t, (((uint8_t *)&gm_l2) + 0));
                    uint32_t t60 = gm_l12;
                    uint32_t t61 = gm_l12;
                    uint32_t r62; bool o63 = __builtin_add_overflow(t61, ((uint32_t)1ULL), &r62);
                    if (o63) gm_trap("integer overflow in `+`", gm_src6d57grm686e3, 363, 113);
                    uint32_t t64 = r62;
                    gm_tl874t2atnlh3 t65;
                    GM_ST(uint32_t, ((uint8_t *)&t65 + 0), t59);
                    GM_ST(uint32_t, ((uint8_t *)&t65 + 4), t60);
                    GM_ST(uint32_t, ((uint8_t *)&t65 + 8), t64);
                    gm_b16_8 t66;
                    GM_ST(void *, (uint8_t *)&t66, (void *)(gm_s6of1cedjphs7));
                    GM_ST(uintptr_t, (uint8_t *)&t66 + 8, (uintptr_t)(2));
                    gm_b0_1 t67 = gm_f_add_fix_0gkcr4c((void *)NULL, cx, NULL, t57, t58, t65, t66);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                goto ix44;
ie44:;
ix44:;
            }
            goto ix26;
ie26:;
ix26:;
        }
        {
            void * t68 = gm_l0;
            void * t69 = GM_LD(void *, (((uint8_t *)t68) + 0));
            gm_b0_1 t70 = gm_f_emit_dph5v1e((void *)NULL, cx, NULL, t69, &(*(gm_t4gut2c9fj1kt *)((uint8_t *)&F.l8)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        gm_ti6pdj2sunpe4 t71;
        memset(&t71, 0, sizeof t71);
        uint8_t t72 = 0;
        F.ret = t71;
        goto gm_fret;
        goto me9;
mn14:;
        gm_trap("no match arm matched the scrutinee", gm_src6d57grm686e3, 354, 15);
me9:;
        F.l13 = t8;
    }
    {
        uint64_t t73 = GM_LD(uintptr_t, (((uint8_t *)&F.l13) + 16) + 8);
        gm_l14 = t73;
    }
    {
        uint64_t t74 = GM_LD(uintptr_t, (((uint8_t *)&F.l13) + 32) + 8);
        uint8_t t75 = (t74 > ((uint64_t)0ULL));
        gm_l15 = t75;
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
        uint64_t t77 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
        uint8_t t78 = ((t77 == ((uint64_t)1ULL)));
        if (!t78) goto ie76;
        uint64_t t79 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t80 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
        if (t79 >= (uintptr_t)(t80)) gm_trap_index(t79, t80, gm_src6d57grm686e3, 374, 40);
        uint8_t t81 = GM_LD(uint8_t, ((((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t79 * 192) + 0) + 0));
        if (t81 != ((uint8_t)2ULL)) goto ie76;
        gm_tfbvnc90lci8r t82 = GM_LD(gm_tfbvnc90lci8r, ((((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t79 * 192) + 0) + 8));
        F.l18 = t82;
        {
            void * t84 = gm_l0;
            void * t85 = (void *)((uint8_t *)&F.l18);
            gm_tkn42u7na3t81 t86;
            memset(&t86, 0, sizeof t86);
            void * t87 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t84, t85, t86);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l19 = t87;
        }
        {
            void * t88 = gm_l0;
            void * t89 = (void *)(((uint8_t *)t88) + 96);
            void * t90 = gm_l0;
            void * t91 = GM_LD(void *, (((uint8_t *)t90) + 0));
            void * t92 = gm_l19;
            gm_t88rcucg1b7uk t93 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t92) + 0));
            gm_t88rcucg1b7uk t94 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t89, t91, t93);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l20 = t94;
        }
        {
            void * t95 = gm_l0;
            void * t96 = gm_l19;
            gm_tl874t2atnlh3 t97 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l18) + 120));
            void * t98 = gm_f_elements_tuple_gdm5n6q((void *)NULL, cx, NULL, t95, t96, t97);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            void * t99 = t98;
            void * t100;
            if ((GM_LD(void *, ((uint8_t *)&t99)) != 0)) {
                void * t101 = GM_LD(void *, (((uint8_t *)&t99) + 0));
                t100 = t101;
            } else {
                gm_ti6pdj2sunpe4 t102;
                memset(&t102, 0, sizeof t102);
                uint8_t t103 = 0;
                F.ret = t102;
                goto gm_fret;
            }
            gm_l21 = t100;
        }
        {
            void * t104 = gm_l0;
            void * t105 = (void *)(((uint8_t *)t104) + 96);
            void * t106 = gm_l0;
            void * t107 = GM_LD(void *, (((uint8_t *)t106) + 0));
            void * t108 = gm_l21;
            gm_t88rcucg1b7uk t109 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t108) + 0));
            gm_t88rcucg1b7uk t110 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t105, t107, t109);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l22 = t110;
        }
        {
            void * t112 = gm_l5;
            gm_t88rcucg1b7uk t113 = gm_l22;
            gm_t53at80pfdus3 t114;
            gm_f_kind_jcnt4hm((void *)&t114, t112, GM_CONV(gm_csguankko4lmhd, t113));
            gm_t53at80pfdus3 t115 = t114;
            uint8_t t116 = GM_LD(uint8_t, (((uint8_t *)&t115) + 0));
            if (t116 != ((uint8_t)12ULL)) goto ie111;
            gm_b16_8 t117 = GM_LD(gm_b16_8, ((((uint8_t *)&t115) + 8) + 0));
            gm_l23 = t117;
            uint8_t t118;
            uint8_t t120 = gm_l15;
            if (!t120) goto ie119;
            uint64_t t122 = GM_LD(uintptr_t, ((uint8_t *)&gm_l23) + 8);
            gm_th2lqbo77pvsc t123;
            gm_f_numbered_error_07hll20((void *)&t123, (void *)NULL, cx, NULL, &(*(gm_tfeab279c16rr *)((uint8_t *)&F.l13)), t122);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_th2lqbo77pvsc t124 = t123;
            uint8_t t125;
            if (((((uint8_t *)&t124))[16] != 0)) goto mn127;
            t125 = ((uint8_t)1);
            goto me126;
mn127:;
            t125 = ((uint8_t)0);
            goto me126;
mn128:;
            gm_trap("no match arm matched the scrutinee", gm_src6d57grm686e3, 380, 63);
me126:;
            uint8_t t129 = t125;
            t118 = t129;
            goto ix119;
ie119:;
            uint64_t t131 = GM_LD(uintptr_t, ((uint8_t *)&gm_l23) + 8);
            uint64_t t132 = gm_l14;
            uint8_t t133 = ((t131 == t132));
            uint8_t t134 = t133;
            t118 = t134;
ix119:;
            if (!t118) goto ie111;
            {
                void * t136 = gm_l0;
                gm_t88rcucg1b7uk t137 = gm_l22;
                gm_tl874t2atnlh3 t138 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l18) + 120));
                uint32_t t139 = gm_f_temp_ilbvj8q((void *)NULL, cx, NULL, t136, t137, t138);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l24 = t139;
            }
            {
                void * t140 = (void *)((uint8_t *)&F.l16);
                uint32_t t141 = gm_l24;
                void * t142 = gm_l21;
                gm_ttggq7ng4kp3p t143;
                GM_ST(uint32_t, (uint8_t *)&t143 + 4, 0);
                GM_ST(uint32_t, ((uint8_t *)&t143 + 0), t141);
                GM_ST(void *, ((uint8_t *)&t143 + 8), t142);
                gm_trcmmd12cefb1 t144;
                GM_ST(uint8_t, (uint8_t *)&t144 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t144 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t144 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t144 + 24, 0);
                GM_ST(uint8_t, ((uint8_t *)&t144 + 0), ((uint8_t)0ULL));
                GM_ST(gm_ttggq7ng4kp3p, ((uint8_t *)&t144 + 8), t143);
                gm_tftb1odc739t5 t145 = gm_f_push_d6c76ud_2(t140, &t144);
                gm_tftb1odc739t5 t146 = t145;
                if (((((uint8_t *)&t146))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t146) + 1, gm_src6d57grm686e3, 382, 13);
                gm_b0_1 t147 = GM_LD(gm_b0_1, (((uint8_t *)&t146) + 1));
            }
            {
                {
                    gm_l26 = ((uint64_t)0ULL);
                }
                {
                    uint64_t t149 = gm_l14;
                    gm_l25 = t149;
                }
                {
                    for (;;) {
                        uint64_t t151 = gm_l26;
                        uint64_t t152 = gm_l25;
                        uint8_t t153 = (t151 < t152);
                        if (!t153) goto lb150;
                        {
                            uint64_t t156 = gm_l26;
                            uint64_t t157 = t156;
                            uint64_t t160 = GM_LD(uint64_t, ((uint8_t *)&t157));
                            gm_l27 = t160;
                            {
                                uint64_t t162;
                                uint8_t t164 = gm_l15;
                                if (!t164) goto ie163;
                                uint64_t t166 = gm_l27;
                                uint64_t t167 = (uintptr_t)t166;
                                uint64_t t168 = GM_LD(uintptr_t, (((uint8_t *)&F.l13) + 32) + 8);
                                if (t167 >= (uintptr_t)(t168)) gm_trap_index(t167, t168, gm_src6d57grm686e3, 384, 37);
                                uint64_t t169 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l13) + 32)) + t167 * 8));
                                uint64_t t170 = t169;
                                t162 = t170;
                                goto ix163;
ie163:;
                                uint64_t t172 = gm_l27;
                                uint64_t t173 = t172;
                                t162 = t173;
ix163:;
                                gm_l28 = t162;
                            }
                            {
                                gm_t88rcucg1b7uk t174 = gm_l22;
                                uint32_t t175 = gm_l24;
                                gm_t5brlu7fal70j t176;
                                memset(&t176, 0, sizeof t176);
                                GM_ST(uint8_t, ((uint8_t *)&t176 + 0), ((uint8_t)15ULL));
                                GM_ST(uint32_t, ((uint8_t *)&t176 + 8), t175);
                                gm_tl874t2atnlh3 t177 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l18) + 120));
                                void * t178 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t174, &t176, t177);
                                if (gm_unw.kind) {
                                    goto gm_ll154;
                                }
                                gm_l29 = t178;
                            }
                            {
                                void * t179 = (void *)((uint8_t *)&F.l17);
                                uint64_t t180 = gm_l28;
                                uint64_t t181 = (uintptr_t)t180;
                                uint64_t t182 = GM_LD(uintptr_t, ((uint8_t *)&gm_l23) + 8);
                                if (t181 >= (uintptr_t)(t182)) gm_trap_index(t181, t182, gm_src6d57grm686e3, 386, 36);
                                gm_t88rcucg1b7uk t183 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l23)) + t181 * 4));
                                void * t184 = gm_l29;
                                uint64_t t185 = gm_l28;
                                uint32_t t186 = (uint32_t)t185;
                                gm_tbl8il92l97ut t187;
                                GM_ST(uint32_t, (uint8_t *)&t187 + 12, 0);
                                GM_ST(void *, ((uint8_t *)&t187 + 0), t184);
                                GM_ST(uint32_t, ((uint8_t *)&t187 + 8), t186);
                                gm_t5brlu7fal70j t188;
                                memset(&t188, 0, sizeof t188);
                                GM_ST(uint8_t, ((uint8_t *)&t188 + 0), ((uint8_t)19ULL));
                                GM_ST(gm_tbl8il92l97ut, ((uint8_t *)&t188 + 8), t187);
                                gm_tl874t2atnlh3 t189 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l18) + 120));
                                void * t190 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t183, &t188, t189);
                                if (gm_unw.kind) {
                                    goto gm_ll154;
                                }
                                gm_tftb1odc739t5 t191 = gm_f_push_d943vgp_2(t179, &(*(gm_tfpf0s89fpb3r *)((uint8_t *)t190)));
                                gm_tftb1odc739t5 t192 = t191;
                                if (((((uint8_t *)&t192))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t192) + 1, gm_src6d57grm686e3, 386, 17);
                                gm_b0_1 t193 = GM_LD(gm_b0_1, (((uint8_t *)&t192) + 1));
                            }
                            goto me158;
mn159:;
                            gm_trap("no match arm matched the scrutinee", gm_src6d57grm686e3, 383, 13);
me158:;
                        }
lc150:;
                        {
                            uint64_t t195 = gm_l26;
                            uint64_t r196; bool o197 = __builtin_add_overflow(t195, ((uint64_t)1ULL), &r196);
                            uint64_t t198 = r196;
                            gm_l26 = t198;
                        }
                    }
gm_ll154:;
                    if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb150; }
                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc150; }
                    }
                    goto gm_lad;
lb150:;
                }
            }
            goto ix111;
ie111:;
            void * t200 = gm_l5;
            gm_t88rcucg1b7uk t201 = gm_l22;
            uint8_t t202 = gm_f_is_error_8k27gd1(t200, GM_CONV(gm_csguankko4lmhd, t201));
            if (!t202) goto ie199;
            {
                gm_ti6pdj2sunpe4 t204;
                memset(&t204, 0, sizeof t204);
                uint8_t t205 = 0;
                F.ret = t204;
                goto gm_fret;
            }
            goto ix199;
ie199:;
            uint8_t t207 = gm_l15;
            if (!t207) goto ie206;
            {
                void * t210 = gm_l5;
                gm_t88rcucg1b7uk t211 = gm_l22;
                gm_t53at80pfdus3 t212;
                gm_f_kind_jcnt4hm((void *)&t212, t210, GM_CONV(gm_csguankko4lmhd, t211));
                gm_t53at80pfdus3 t213 = t212;
                uint8_t t214 = GM_LD(uint8_t, (((uint8_t *)&t213) + 0));
                if (t214 != ((uint8_t)12ULL)) goto ie209;
                gm_b16_8 t215 = GM_LD(gm_b16_8, ((((uint8_t *)&t213) + 8) + 0));
                gm_l30 = t215;
                {
                    void * t217 = gm_l0;
                    gm_tl874t2atnlh3 t218 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l18) + 120));
                    uint64_t t219 = GM_LD(uintptr_t, ((uint8_t *)&gm_l30) + 8);
                    gm_th2lqbo77pvsc t220;
                    gm_f_numbered_error_07hll20((void *)&t220, (void *)NULL, cx, NULL, &(*(gm_tfeab279c16rr *)((uint8_t *)&F.l13)), t219);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_th2lqbo77pvsc t221 = t220;
                    gm_b16_8 t222;
                    if (((((uint8_t *)&t221))[16] != 0)) {
                        gm_b16_8 t223 = GM_LD(gm_b16_8, (((uint8_t *)&t221) + 0));
                        t222 = t223;
                    } else {
                        gm_b16_8 t224;
                        GM_ST(void *, (uint8_t *)&t224, (void *)(gm_sr4qbrk6tlgaa));
                        GM_ST(uintptr_t, (uint8_t *)&t224 + 8, (uintptr_t)(0));
                        t222 = t224;
                    }
                    gm_b0_1 t225 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t217, t218, t222);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                goto ix209;
ie209:;
                {
                    void * t227 = gm_l0;
                    gm_tl874t2atnlh3 t228 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l18) + 120));
                    void * t229 = gm_l0;
                    gm_t88rcucg1b7uk t230 = gm_l20;
                    gm_b16_8 t231 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t229, t230);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tnpifv20irnph t232;
                    GM_ST(gm_b16_8, ((uint8_t *)&t232 + 0), t231);
                    gm_tabb710d9mq2e t233;
                    gm_f_format_les73rv((void *)&t233, (void *)NULL, cx, NULL, t232);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tabb710d9mq2e t234 = t233;
                    if (((((uint8_t *)&t234))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t234) + 8, gm_src6d57grm686e3, 394, 34);
                    gm_b16_8 t235 = GM_LD(gm_b16_8, (((uint8_t *)&t234) + 8));
                    gm_b16_8 t236 = t235;
                    gm_b16_8 t237 = GM_LD(gm_b16_8, ((uint8_t *)&t236));
                    gm_b0_1 t238 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t227, t228, t237);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
ix209:;
            }
            {
                gm_ti6pdj2sunpe4 t239;
                memset(&t239, 0, sizeof t239);
                uint8_t t240 = 0;
                F.ret = t239;
                goto gm_fret;
            }
            goto ix206;
ie206:;
            {
                void * t242 = gm_l0;
                gm_tl874t2atnlh3 t243 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l18) + 120));
                gm_tq3m5qgvkpje8 t244;
                uint64_t t245 = gm_l14;
                gm_b16_8 t246 = gm_f_placeholders_text_rtvia6b((void *)NULL, cx, NULL, t245);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(gm_b16_8, (uint8_t *)&t244 + 0, t246);
                uint64_t t247 = gm_l14;
                GM_ST(uint64_t, (uint8_t *)&t244 + 16, t247);
                void * t248 = gm_l0;
                gm_t88rcucg1b7uk t249 = gm_l20;
                gm_b16_8 t250 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t248, t249);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(gm_b16_8, (uint8_t *)&t244 + 24, t250);
                gm_tabb710d9mq2e t251;
                gm_f_format_k6j7893((void *)&t251, (void *)NULL, cx, NULL, &t244);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t252 = t251;
                if (((((uint8_t *)&t252))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t252) + 8, gm_src6d57grm686e3, 398, 30);
                gm_b16_8 t253 = GM_LD(gm_b16_8, (((uint8_t *)&t252) + 8));
                gm_b16_8 t254 = t253;
                gm_b16_8 t255 = GM_LD(gm_b16_8, ((uint8_t *)&t254));
                gm_b0_1 t256 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t242, t243, t255);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                gm_ti6pdj2sunpe4 t257;
                memset(&t257, 0, sizeof t257);
                uint8_t t258 = 0;
                F.ret = t257;
                goto gm_fret;
            }
ix206:;
ix199:;
ix111:;
        }
        goto ix76;
ie76:;
        {
            gm_f_list_vp1djv1((void *)&F.l31, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            {
                gm_b16_8 t261 = gm_l3;
                gm_l32 = t261;
            }
            {
                gm_l33 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t263 = gm_l33;
                    uint64_t t264 = GM_LD(uintptr_t, ((uint8_t *)&gm_l32) + 8);
                    uint8_t t265 = (t263 < t264);
                    if (!t265) goto lb262;
                    {
                        uint64_t t268 = gm_l33;
                        uint64_t t269 = (uintptr_t)t268;
                        uint64_t t270 = GM_LD(uintptr_t, ((uint8_t *)&gm_l32) + 8);
                        gm_tpff59ptr3s92 t273 = GM_LD(gm_tpff59ptr3s92, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l32)) + t269 * 192));
                        F.l34 = t273;
                        {
                            uint8_t t276 = GM_LD(uint8_t, ((((uint8_t *)&F.l34) + 0) + 0));
                            if (t276 != ((uint8_t)0ULL)) goto ie275;
                            gm_tfbvnc90lci8r t277 = GM_LD(gm_tfbvnc90lci8r, ((((uint8_t *)&F.l34) + 0) + 8));
                            F.l35 = t277;
                            {
                                void * t279 = (void *)((uint8_t *)&F.l31);
                                gm_tftb1odc739t5 t280 = gm_f_push_d943vgp(t279, &(*(gm_tfbvnc90lci8r *)((uint8_t *)&F.l35)));
                                gm_tftb1odc739t5 t281 = t280;
                                if (((((uint8_t *)&t281))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t281) + 1, gm_src6d57grm686e3, 408, 13);
                                gm_b0_1 t282 = GM_LD(gm_b0_1, (((uint8_t *)&t281) + 1));
                            }
                            goto ix275;
ie275:;
                            {
                                {
                                    void * t285 = gm_l0;
                                    gm_tl874t2atnlh3 t286 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l34) + 176));
                                    gm_b16_8 t287;
                                    GM_ST(void *, (uint8_t *)&t287, (void *)(gm_smom6v4b4s28c));
                                    GM_ST(uintptr_t, (uint8_t *)&t287 + 8, (uintptr_t)(51));
                                    gm_b0_1 t288 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t285, t286, t287);
                                    if (gm_unw.kind) {
                                        goto gm_ll266;
                                    }
                                }
                                {
                                    gm_ti6pdj2sunpe4 t289;
                                    memset(&t289, 0, sizeof t289);
                                    uint8_t t290 = 0;
                                    F.ret = t289;
                                    goto gm_fret;
                                }
                            }
ix275:;
                        }
                        goto me271;
mn272:;
                        gm_trap("no match arm matched the scrutinee", gm_src6d57grm686e3, 403, 9);
me271:;
                    }
lc262:;
                    {
                        uint64_t t292 = gm_l33;
                        uint64_t r293; bool o294 = __builtin_add_overflow(t292, ((uint64_t)1ULL), &r293);
                        uint64_t t295 = r293;
                        gm_l33 = t295;
                    }
                }
gm_ll266:;
                if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb262; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc262; }
                }
                goto gm_lad;
lb262:;
            }
        }
        {
            uint8_t t297 = gm_l15;
            if (!t297) goto ie296;
            {
                uint64_t t300 = GM_LD(uintptr_t, (((uint8_t *)&F.l31) + 0) + 8);
                gm_th2lqbo77pvsc t301;
                gm_f_numbered_error_07hll20((void *)&t301, (void *)NULL, cx, NULL, &(*(gm_tfeab279c16rr *)((uint8_t *)&F.l13)), t300);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_th2lqbo77pvsc t302 = t301;
                if (!((((uint8_t *)&t302))[16] != 0)) goto ie299;
                gm_b16_8 t303 = GM_LD(gm_b16_8, ((uint8_t *)&t302));
                gm_l36 = t303;
                {
                    void * t305 = gm_l0;
                    gm_tl874t2atnlh3 t306 = gm_l4;
                    gm_b16_8 t307 = gm_l36;
                    gm_b0_1 t308 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t305, t306, t307);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                {
                    gm_ti6pdj2sunpe4 t309;
                    memset(&t309, 0, sizeof t309);
                    uint8_t t310 = 0;
                    F.ret = t309;
                    goto gm_fret;
                }
                goto ix299;
ie299:;
ix299:;
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
                    uint64_t t312 = GM_LD(uintptr_t, (((uint8_t *)&F.l31) + 0) + 8);
                    gm_l38 = t312;
                }
                {
                    for (;;) {
                        uint64_t t314 = gm_l39;
                        uint64_t t315 = gm_l38;
                        uint8_t t316 = (t314 < t315);
                        if (!t316) goto lb313;
                        {
                            uint64_t t319 = gm_l39;
                            uint64_t t320 = t319;
                            uint64_t t323 = GM_LD(uint64_t, ((uint8_t *)&t320));
                            gm_l40 = t323;
                            {
                                void * t325 = gm_l0;
                                uint64_t t326 = gm_l40;
                                uint64_t t327 = (uintptr_t)t326;
                                uint64_t t328 = GM_LD(uintptr_t, (((uint8_t *)&F.l31) + 0) + 8);
                                if (t327 >= (uintptr_t)(t328)) gm_trap_index(t327, t328, gm_src6d57grm686e3, 419, 31);
                                void * t329 = (void *)((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l31) + 0)) + t327 * 136);
                                gm_tkn42u7na3t81 t330;
                                memset(&t330, 0, sizeof t330);
                                void * t331 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t325, t329, t330);
                                if (gm_unw.kind) {
                                    goto gm_ll317;
                                }
                                gm_l41 = t331;
                            }
                            {
                                void * t332 = (void *)((uint8_t *)&F.l37);
                                void * t333 = gm_l0;
                                void * t334 = gm_l41;
                                void * t335 = (void *)((uint8_t *)&F.l16);
                                void * t336 = gm_f_bind_temp_sa5rcbr((void *)NULL, cx, NULL, t333, t334, t335);
                                if (gm_unw.kind) {
                                    goto gm_ll317;
                                }
                                gm_tftb1odc739t5 t337 = gm_f_push_d943vgp_2(t332, &(*(gm_tfpf0s89fpb3r *)((uint8_t *)t336)));
                                gm_tftb1odc739t5 t338 = t337;
                                if (((((uint8_t *)&t338))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t338) + 1, gm_src6d57grm686e3, 420, 17);
                                gm_b0_1 t339 = GM_LD(gm_b0_1, (((uint8_t *)&t338) + 1));
                            }
                            goto me321;
mn322:;
                            gm_trap("no match arm matched the scrutinee", gm_src6d57grm686e3, 418, 13);
me321:;
                        }
lc313:;
                        {
                            uint64_t t341 = gm_l39;
                            uint64_t r342; bool o343 = __builtin_add_overflow(t341, ((uint64_t)1ULL), &r342);
                            uint64_t t344 = r342;
                            gm_l39 = t344;
                        }
                    }
gm_ll317:;
                    if (gm_unw.frame == &F.h && gm_unw.label == 2) {
                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb313; }
                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc313; }
                    }
                    goto gm_lad;
lb313:;
                }
            }
            {
                {
                    gm_b16_8 t346 = GM_LD(gm_b16_8, (((uint8_t *)&F.l13) + 32));
                    gm_l42 = t346;
                }
                {
                    gm_l43 = ((uint64_t)0ULL);
                }
                {
                    for (;;) {
                        uint64_t t348 = gm_l43;
                        uint64_t t349 = GM_LD(uintptr_t, ((uint8_t *)&gm_l42) + 8);
                        uint8_t t350 = (t348 < t349);
                        if (!t350) goto lb347;
                        {
                            uint64_t t353 = gm_l43;
                            uint64_t t354 = (uintptr_t)t353;
                            uint64_t t355 = GM_LD(uintptr_t, ((uint8_t *)&gm_l42) + 8);
                            uint64_t t356 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l42)) + t354 * 8));
                            uint64_t t357 = t356;
                            uint64_t t360 = GM_LD(uint64_t, ((uint8_t *)&t357));
                            gm_l44 = t360;
                            {
                                void * t362 = (void *)((uint8_t *)&F.l17);
                                uint64_t t363 = gm_l44;
                                uint64_t t364 = (uintptr_t)t363;
                                uint64_t t365 = GM_LD(uintptr_t, (((uint8_t *)&F.l37) + 0) + 8);
                                if (t364 >= (uintptr_t)(t365)) gm_trap_index(t364, t365, gm_src6d57grm686e3, 422, 52);
                                gm_tftb1odc739t5 t366 = gm_f_push_d943vgp_2(t362, &(*(gm_tfpf0s89fpb3r *)((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l37) + 0)) + t364 * 224)));
                                gm_tftb1odc739t5 t367 = t366;
                                if (((((uint8_t *)&t367))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t367) + 1, gm_src6d57grm686e3, 422, 40);
                                gm_b0_1 t368 = GM_LD(gm_b0_1, (((uint8_t *)&t367) + 1));
                            }
                            goto me358;
mn359:;
                            gm_trap("no match arm matched the scrutinee", gm_src6d57grm686e3, 422, 13);
me358:;
                        }
lc347:;
                        {
                            uint64_t t370 = gm_l43;
                            uint64_t r371; bool o372 = __builtin_add_overflow(t370, ((uint64_t)1ULL), &r371);
                            uint64_t t373 = r371;
                            gm_l43 = t373;
                        }
                    }
lb347:;
                }
            }
            goto ix296;
ie296:;
            uint64_t t375 = GM_LD(uintptr_t, (((uint8_t *)&F.l31) + 0) + 8);
            uint64_t t376 = gm_l14;
            uint8_t t377 = (!(t375 == t376));
            if (!t377) goto ie374;
            {
                gm_b16_8 t379;
                uint64_t t381 = GM_LD(uintptr_t, (((uint8_t *)&F.l31) + 0) + 8);
                uint8_t t382 = ((t381 == ((uint64_t)1ULL)));
                if (!t382) goto ie380;
                gm_b16_8 t384;
                GM_ST(void *, (uint8_t *)&t384, (void *)(gm_scne9acm49p4h));
                GM_ST(uintptr_t, (uint8_t *)&t384 + 8, (uintptr_t)(10));
                gm_b16_8 t385 = t384;
                t379 = t385;
                goto ix380;
ie380:;
                uint64_t t387 = GM_LD(uintptr_t, (((uint8_t *)&F.l31) + 0) + 8);
                gm_t4ce0jd3tdro4 t388;
                GM_ST(uint64_t, ((uint8_t *)&t388 + 0), t387);
                gm_tabb710d9mq2e t389;
                gm_f_format_td6patj((void *)&t389, (void *)NULL, cx, NULL, t388);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t390 = t389;
                if (((((uint8_t *)&t390))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t390) + 8, gm_src6d57grm686e3, 424, 69);
                gm_b16_8 t391 = GM_LD(gm_b16_8, (((uint8_t *)&t390) + 8));
                gm_b16_8 t392 = t391;
                gm_b16_8 t393 = t392;
                gm_b16_8 t394 = GM_LD(gm_b16_8, ((uint8_t *)&t393));
                t379 = t394;
ix380:;
                gm_l45 = t379;
            }
            {
                void * t395 = gm_l0;
                gm_tl874t2atnlh3 t396 = gm_l4;
                gm_t6j6smj7qoctu t397;
                uint64_t t398 = gm_l14;
                gm_b16_8 t399 = gm_f_placeholders_text_rtvia6b((void *)NULL, cx, NULL, t398);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(gm_b16_8, (uint8_t *)&t397 + 0, t399);
                gm_b16_8 t400 = gm_l45;
                GM_ST(gm_b16_8, (uint8_t *)&t397 + 16, t400);
                gm_tabb710d9mq2e t401;
                gm_f_format_2h4f9a5((void *)&t401, (void *)NULL, cx, NULL, &t397);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t402 = t401;
                if (((((uint8_t *)&t402))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t402) + 8, gm_src6d57grm686e3, 425, 28);
                gm_b16_8 t403 = GM_LD(gm_b16_8, (((uint8_t *)&t402) + 8));
                gm_b16_8 t404 = t403;
                gm_b16_8 t405 = GM_LD(gm_b16_8, ((uint8_t *)&t404));
                gm_b0_1 t406 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t395, t396, t405);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                gm_ti6pdj2sunpe4 t407;
                memset(&t407, 0, sizeof t407);
                uint8_t t408 = 0;
                F.ret = t407;
                goto gm_fret;
            }
            goto ix374;
ie374:;
ix374:;
ix296:;
        }
        {
            uint8_t t410 = gm_l15;
            uint8_t t411 = (uint8_t)!t410;
            if (!t411) goto ie409;
            {
                {
                    gm_l47 = ((uint64_t)0ULL);
                }
                {
                    uint64_t t414 = GM_LD(uintptr_t, (((uint8_t *)&F.l31) + 0) + 8);
                    gm_l46 = t414;
                }
                {
                    for (;;) {
                        uint64_t t416 = gm_l47;
                        uint64_t t417 = gm_l46;
                        uint8_t t418 = (t416 < t417);
                        if (!t418) goto lb415;
                        {
                            uint64_t t421 = gm_l47;
                            uint64_t t422 = t421;
                            uint64_t t425 = GM_LD(uint64_t, ((uint8_t *)&t422));
                            gm_l48 = t425;
                            {
                                void * t427 = (void *)((uint8_t *)&F.l17);
                                void * t428 = gm_l0;
                                uint64_t t429 = gm_l48;
                                uint64_t t430 = (uintptr_t)t429;
                                uint64_t t431 = GM_LD(uintptr_t, (((uint8_t *)&F.l31) + 0) + 8);
                                if (t430 >= (uintptr_t)(t431)) gm_trap_index(t430, t431, gm_src6d57grm686e3, 429, 63);
                                void * t432 = (void *)((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l31) + 0)) + t430 * 136);
                                gm_tkn42u7na3t81 t433;
                                memset(&t433, 0, sizeof t433);
                                void * t434 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t428, t432, t433);
                                if (gm_unw.kind) {
                                    goto gm_ll419;
                                }
                                gm_tftb1odc739t5 t435 = gm_f_push_d943vgp_2(t427, &(*(gm_tfpf0s89fpb3r *)((uint8_t *)t434)));
                                gm_tftb1odc739t5 t436 = t435;
                                if (((((uint8_t *)&t436))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t436) + 1, gm_src6d57grm686e3, 429, 42);
                                gm_b0_1 t437 = GM_LD(gm_b0_1, (((uint8_t *)&t436) + 1));
                            }
                            goto me423;
mn424:;
                            gm_trap("no match arm matched the scrutinee", gm_src6d57grm686e3, 429, 13);
me423:;
                        }
lc415:;
                        {
                            uint64_t t439 = gm_l47;
                            uint64_t r440; bool o441 = __builtin_add_overflow(t439, ((uint64_t)1ULL), &r440);
                            uint64_t t442 = r440;
                            gm_l47 = t442;
                        }
                    }
gm_ll419:;
                    if (gm_unw.frame == &F.h && gm_unw.label == 4) {
                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb415; }
                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc415; }
                    }
                    goto gm_lad;
lb415:;
                }
            }
            goto ix409;
ie409:;
ix409:;
        }
ix76:;
    }
    {
        {
            gm_l50 = ((uint64_t)0ULL);
        }
        {
            uint64_t t444 = gm_l14;
            gm_l49 = t444;
        }
        {
            for (;;) {
                uint64_t t446 = gm_l50;
                uint64_t t447 = gm_l49;
                uint8_t t448 = (t446 < t447);
                if (!t448) goto lb445;
                {
                    uint64_t t451 = gm_l50;
                    uint64_t t452 = t451;
                    uint64_t t455 = GM_LD(uint64_t, ((uint8_t *)&t452));
                    gm_l51 = t455;
                    {
                        uint64_t t457 = gm_l51;
                        uint64_t t458 = (uintptr_t)t457;
                        uint64_t t459 = GM_LD(uintptr_t, (((uint8_t *)&F.l13) + 16) + 8);
                        if (t458 >= (uintptr_t)(t459)) gm_trap_index(t458, t459, gm_src6d57grm686e3, 433, 18);
                        void * t460 = (void *)((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l13) + 16)) + t458 * 24);
                        gm_l52 = t460;
                    }
                    {
                        void * t462 = gm_l52;
                        uint8_t t463 = gm_f_is_default_spec_cdd8jng(t462);
                        if (!t463) goto ie461;
                        {
                            goto lc445;
                        }
                        goto ix461;
ie461:;
ix461:;
                    }
                    {
                        uint64_t t465 = gm_l51;
                        uint64_t t466 = (uintptr_t)t465;
                        uint64_t t467 = GM_LD(uintptr_t, (((uint8_t *)&F.l17) + 0) + 8);
                        if (t466 >= (uintptr_t)(t467)) gm_trap_index(t466, t467, gm_src6d57grm686e3, 435, 15);
                        void * t468 = (void *)((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l17) + 0)) + t466 * 224);
                        gm_l53 = t468;
                    }
                    {
                        uint8_t t469 = 0;
                        void * t470 = gm_l52;
                        uint8_t t471 = ((GM_LD(uint8_t, (((uint8_t *)t470) + 20) + 0) == ((uint8_t)5ULL)));
                        if (!t471) {
                            void * t472 = gm_l52;
                            uint8_t t473;
                            if ((((((uint8_t *)t472) + 12))[4] != 0)) goto mn475;
                            t473 = ((uint8_t)0);
                            goto me474;
mn475:;
                            t473 = ((uint8_t)1);
                            goto me474;
mn476:;
                            gm_trap("no match arm matched the scrutinee", gm_src6d57grm686e3, 437, 45);
me474:;
                            t469 = t473;
                        } else t469 = 1;
                        gm_l54 = t469;
                    }
                    {
                        uint8_t t478 = gm_l54;
                        if (!t478) goto ie477;
                        void * t479 = gm_l0;
                        void * t480 = (void *)(((uint8_t *)t479) + 96);
                        void * t481 = gm_l5;
                        void * t482 = gm_l53;
                        gm_t88rcucg1b7uk t483 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t482) + 0));
                        gm_taari1s0m51a7 t484 = gm_f_var_kind_1sl5sig((void *)NULL, cx, NULL, t480, t481, t483);
                        if (gm_unw.kind) {
                            goto gm_ll449;
                        }
                        gm_t9qkh0rsooc8d t485;
                        GM_ST(uint8_t, ((uint8_t *)&t485 + 0), ((uint8_t)0ULL));
                        gm_taari1s0m51a7 t486;
                        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t486), t485);
                        ((uint8_t *)&t486)[1] = 1;
                        gm_taari1s0m51a7 t487 = t484;
                        gm_taari1s0m51a7 t488 = t486;
                        uint8_t t489 = (gm_eq5307j5k9ms(((uint8_t *)&t487), ((uint8_t *)&t488)));
                        if (!t489) goto ie477;
                        {
                            {
                                void * t492 = gm_l0;
                                void * t493 = (void *)(((uint8_t *)t492) + 96);
                                void * t494 = gm_l5;
                                void * t495 = gm_l53;
                                gm_t88rcucg1b7uk t496 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t495) + 0));
                                void * t497 = gm_l5;
                                gm_t53at80pfdus3 t498;
                                memset(&t498, 0, sizeof t498);
                                GM_ST(uint8_t, ((uint8_t *)&t498 + 0), ((uint8_t)3ULL));
                                GM_ST(uint16_t, ((uint8_t *)&t498 + 8), ((uint16_t)64ULL));
                                gm_t88rcucg1b7uk t499 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t497, &t498);
                                if (gm_unw.kind) {
                                    goto gm_ll449;
                                }
                                gm_t9qkh0rsooc8d t500 = gm_f_unify_t2qjv2p((void *)NULL, cx, NULL, t493, t494, t496, t499);
                                if (gm_unw.kind) {
                                    goto gm_ll449;
                                }
                            }
                        }
                        goto ix477;
ie477:;
ix477:;
                    }
                    {
                        void * t501 = gm_l0;
                        void * t502 = gm_l53;
                        gm_t88rcucg1b7uk t503 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t502) + 0));
                        void * t504 = gm_l53;
                        gm_tl874t2atnlh3 t505 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t504) + 208));
                        gm_t88rcucg1b7uk t506 = gm_f_settle_h9ikvs5((void *)NULL, cx, NULL, t501, t503, t505);
                        if (gm_unw.kind) {
                            goto gm_ll449;
                        }
                        gm_l55 = t506;
                    }
                    {
                        void * t508 = gm_l5;
                        gm_t88rcucg1b7uk t509 = gm_l55;
                        uint8_t t510 = gm_f_has_param_u9t0efs(t508, GM_CONV(gm_csguankko4lmhd, t509));
                        if (!t510) goto ie507;
                        {
                            goto lc445;
                        }
                        goto ix507;
ie507:;
ix507:;
                    }
                    {
                        void * t513 = gm_l0;
                        void * t514 = GM_LD(void *, (((uint8_t *)t513) + 0));
                        void * t515 = gm_l52;
                        gm_t88rcucg1b7uk t516 = gm_l55;
                        gm_th2lqbo77pvsc t517;
                        gm_f_check_spec_4vp7jhg((void *)&t517, (void *)NULL, cx, NULL, t514, t515, t516);
                        if (gm_unw.kind) {
                            goto gm_ll449;
                        }
                        gm_th2lqbo77pvsc t518 = t517;
                        if (!((((uint8_t *)&t518))[16] != 0)) goto ie512;
                        gm_b16_8 t519 = GM_LD(gm_b16_8, ((uint8_t *)&t518));
                        gm_l56 = t519;
                        {
                            void * t521 = gm_l0;
                            void * t522 = gm_l53;
                            gm_tl874t2atnlh3 t523 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t522) + 208));
                            gm_b16_8 t524 = gm_l56;
                            gm_b0_1 t525 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t521, t523, t524);
                            if (gm_unw.kind) {
                                goto gm_ll449;
                            }
                        }
                        goto ix512;
ie512:;
ix512:;
                    }
                    goto me453;
mn454:;
                    gm_trap("no match arm matched the scrutinee", gm_src6d57grm686e3, 432, 5);
me453:;
                }
lc445:;
                {
                    uint64_t t527 = gm_l50;
                    uint64_t r528; bool o529 = __builtin_add_overflow(t527, ((uint64_t)1ULL), &r528);
                    uint64_t t530 = r528;
                    gm_l50 = t530;
                }
            }
gm_ll449:;
            if (gm_unw.frame == &F.h && gm_unw.label == 5) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb445; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc445; }
            }
            goto gm_lad;
lb445:;
        }
    }
    {
        gm_t0uuhaljsf410 t531;
        gm_b16_8 t532 = GM_LD(gm_b16_8, (((uint8_t *)&F.l13) + 0));
        GM_ST(gm_b16_8, (uint8_t *)&t531 + 0, t532);
        gm_b16_8 t533 = GM_LD(gm_b16_8, (((uint8_t *)&F.l13) + 16));
        GM_ST(gm_b16_8, (uint8_t *)&t531 + 16, t533);
        gm_b16_8 t534 = GM_LD(gm_b16_8, (((uint8_t *)&F.l17) + 0));
        GM_ST(gm_b16_8, (uint8_t *)&t531 + 32, t534);
        gm_b16_8 t535 = GM_LD(gm_b16_8, (((uint8_t *)&F.l16) + 0));
        GM_ST(gm_b16_8, (uint8_t *)&t531 + 48, t535);
        gm_ti6pdj2sunpe4 t536;
        GM_ST(uint8_t, (uint8_t *)&t536 + 65, 0);
        GM_ST(uint16_t, (uint8_t *)&t536 + 66, 0);
        GM_ST(uint32_t, (uint8_t *)&t536 + 68, 0);
        GM_ST(gm_t0uuhaljsf410, ((uint8_t *)&t536), t531);
        ((uint8_t *)&t536)[64] = 1;
        uint8_t t537 = 0;
        F.ret = t536;
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
    GM_STACK_CHECK(&F, 2237u);
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
        uint8_t t10 = GM_LD(uint8_t, ((((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t8 * 192) + 0) + 0));
        if (t10 != ((uint8_t)2ULL)) goto ie5;
        gm_tfbvnc90lci8r t11 = GM_LD(gm_tfbvnc90lci8r, ((((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t8 * 192) + 0) + 8));
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
            gm_tl874t2atnlh3 t19 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l7) + 120));
            void * t20 = gm_f_elements_tuple_gdm5n6q((void *)NULL, cx, NULL, t17, t18, t19);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            void * t21 = t20;
            void * t22;
            if ((GM_LD(void *, ((uint8_t *)&t21)) != 0)) {
                void * t23 = GM_LD(void *, (((uint8_t *)&t21) + 0));
                t22 = t23;
            } else {
                void * t24 = gm_l0;
                void * t25 = GM_LD(void *, (((uint8_t *)t24) + 0));
                gm_tl874t2atnlh3 t26 = gm_l4;
                void * t27 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t25, t26);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t28 = 0;
                F.ret = t27;
                goto gm_fret;
            }
            gm_l9 = t22;
        }
        {
            void * t29 = gm_l9;
            gm_l6 = t29;
        }
        goto ix5;
ie5:;
        uint64_t t31 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
        uint8_t t32 = (t31 > ((uint64_t)0ULL));
        if (!t32) goto ie30;
        {
            gm_f_list_vp1djv1_2((void *)&F.l10, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            {
                gm_b16_8 t35 = gm_l3;
                gm_l11 = t35;
            }
            {
                gm_l12 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t37 = gm_l12;
                    uint64_t t38 = GM_LD(uintptr_t, ((uint8_t *)&gm_l11) + 8);
                    uint8_t t39 = (t37 < t38);
                    if (!t39) goto lb36;
                    {
                        uint64_t t42 = gm_l12;
                        uint64_t t43 = (uintptr_t)t42;
                        uint64_t t44 = GM_LD(uintptr_t, ((uint8_t *)&gm_l11) + 8);
                        gm_tpff59ptr3s92 t47 = GM_LD(gm_tpff59ptr3s92, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l11)) + t43 * 192));
                        F.l13 = t47;
                        {
                            uint8_t t50 = GM_LD(uint8_t, ((((uint8_t *)&F.l13) + 0) + 0));
                            if (t50 != ((uint8_t)0ULL)) goto ie49;
                            gm_tfbvnc90lci8r t51 = GM_LD(gm_tfbvnc90lci8r, ((((uint8_t *)&F.l13) + 0) + 8));
                            F.l14 = t51;
                            {
                                void * t53 = (void *)((uint8_t *)&F.l10);
                                void * t54 = gm_l0;
                                void * t55 = (void *)((uint8_t *)&F.l14);
                                gm_tkn42u7na3t81 t56;
                                memset(&t56, 0, sizeof t56);
                                void * t57 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t54, t55, t56);
                                if (gm_unw.kind) {
                                    goto gm_ll40;
                                }
                                gm_tftb1odc739t5 t58 = gm_f_push_d943vgp_2(t53, &(*(gm_tfpf0s89fpb3r *)((uint8_t *)t57)));
                                gm_tftb1odc739t5 t59 = t58;
                                if (((((uint8_t *)&t59))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t59) + 1, gm_src6d57grm686e3, 465, 13);
                                gm_b0_1 t60 = GM_LD(gm_b0_1, (((uint8_t *)&t59) + 1));
                            }
                            goto ix49;
ie49:;
                            {
                                {
                                    void * t63 = gm_l0;
                                    gm_tl874t2atnlh3 t64 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l13) + 176));
                                    gm_b16_8 t65;
                                    GM_ST(void *, (uint8_t *)&t65, (void *)(gm_smom6v4b4s28c));
                                    GM_ST(uintptr_t, (uint8_t *)&t65 + 8, (uintptr_t)(51));
                                    gm_b0_1 t66 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t63, t64, t65);
                                    if (gm_unw.kind) {
                                        goto gm_ll40;
                                    }
                                }
                                {
                                    void * t67 = gm_l0;
                                    void * t68 = GM_LD(void *, (((uint8_t *)t67) + 0));
                                    gm_tl874t2atnlh3 t69 = gm_l4;
                                    void * t70 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t68, t69);
                                    if (gm_unw.kind) {
                                        goto gm_ll40;
                                    }
                                    uint8_t t71 = 0;
                                    F.ret = t70;
                                    goto gm_fret;
                                }
                            }
ix49:;
                        }
                        goto me45;
mn46:;
                        gm_trap("no match arm matched the scrutinee", gm_src6d57grm686e3, 460, 9);
me45:;
                    }
lc36:;
                    {
                        uint64_t t73 = gm_l12;
                        uint64_t r74; bool o75 = __builtin_add_overflow(t73, ((uint64_t)1ULL), &r74);
                        uint64_t t76 = r74;
                        gm_l12 = t76;
                    }
                }
gm_ll40:;
                if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb36; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc36; }
                }
                goto gm_lad;
lb36:;
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
                gm_b16_8 t78 = GM_LD(gm_b16_8, (((uint8_t *)&F.l10) + 0));
                gm_l16 = t78;
            }
            {
                gm_l17 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t80 = gm_l17;
                    uint64_t t81 = GM_LD(uintptr_t, ((uint8_t *)&gm_l16) + 8);
                    uint8_t t82 = (t80 < t81);
                    if (!t82) goto lb79;
                    {
                        uint64_t t85 = gm_l17;
                        uint64_t t86 = (uintptr_t)t85;
                        uint64_t t87 = GM_LD(uintptr_t, ((uint8_t *)&gm_l16) + 8);
                        gm_tfpf0s89fpb3r t90 = GM_LD(gm_tfpf0s89fpb3r, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l16)) + t86 * 224));
                        F.l18 = t90;
                        {
                            void * t92 = (void *)((uint8_t *)&F.l15);
                            void * t93 = gm_l0;
                            void * t94 = (void *)(((uint8_t *)t93) + 96);
                            void * t95 = gm_l0;
                            void * t96 = GM_LD(void *, (((uint8_t *)t95) + 0));
                            gm_t88rcucg1b7uk t97 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l18) + 0));
                            gm_t88rcucg1b7uk t98 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t94, t96, t97);
                            if (gm_unw.kind) {
                                goto gm_ll83;
                            }
                            gm_tftb1odc739t5 t99 = gm_f_push_k3l9nic(t92, GM_CONV(gm_csguankko4lmhd, t98));
                            gm_tftb1odc739t5 t100 = t99;
                            if (((((uint8_t *)&t100))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t100) + 1, gm_src6d57grm686e3, 468, 32);
                            gm_b0_1 t101 = GM_LD(gm_b0_1, (((uint8_t *)&t100) + 1));
                        }
                        goto me88;
mn89:;
                        gm_trap("no match arm matched the scrutinee", gm_src6d57grm686e3, 468, 9);
me88:;
                    }
lc79:;
                    {
                        uint64_t t103 = gm_l17;
                        uint64_t r104; bool o105 = __builtin_add_overflow(t103, ((uint64_t)1ULL), &r104);
                        uint64_t t106 = r104;
                        gm_l17 = t106;
                    }
                }
gm_ll83:;
                if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb79; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc79; }
                }
                goto gm_lad;
lb79:;
            }
        }
        {
            void * t107 = gm_l5;
            gm_t6j6smj7qoctu t108;
            gm_b16_8 t109 = GM_LD(gm_b16_8, (((uint8_t *)&F.l15) + 0));
            gm_b16_8 t110 = t109;
            gm_b16_8 t111 = GM_LD(gm_b16_8, ((uint8_t *)&t110));
            GM_ST(gm_b16_8, (uint8_t *)&t108 + 0, t111);
            gm_b16_8 t112 = gm_f_empty_slice_5nn7ph0();
            gm_b16_8 t113 = t112;
            gm_b16_8 t114 = GM_LD(gm_b16_8, ((uint8_t *)&t113));
            GM_ST(gm_b16_8, (uint8_t *)&t108 + 16, t114);
            gm_t53at80pfdus3 t115;
            GM_ST(uint8_t, (uint8_t *)&t115 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t115 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t115 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t115 + 40, 0);
            GM_ST(uint64_t, (uint8_t *)&t115 + 48, 0);
            GM_ST(uint8_t, ((uint8_t *)&t115 + 0), ((uint8_t)12ULL));
            GM_ST(gm_t6j6smj7qoctu, ((uint8_t *)&t115 + 8), t108);
            gm_t88rcucg1b7uk t116 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t107, &t115);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l19 = t116;
        }
        {
            gm_t88rcucg1b7uk t117 = gm_l19;
            gm_b16_8 t118 = GM_LD(gm_b16_8, (((uint8_t *)&F.l10) + 0));
            gm_t5brlu7fal70j t119;
            memset(&t119, 0, sizeof t119);
            GM_ST(uint8_t, ((uint8_t *)&t119 + 0), ((uint8_t)35ULL));
            GM_ST(gm_b16_8, ((uint8_t *)&t119 + 8), t118);
            gm_tl874t2atnlh3 t120 = gm_l4;
            void * t121 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t117, &t119, t120);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l6 = t121;
        }
        goto ix30;
ie30:;
ix30:;
ix5:;
    }
    {
        gm_t88rcucg1b7uk t122;
        void * t124 = gm_l6;
        void * t125 = t124;
        if (!(GM_LD(void *, ((uint8_t *)&t125)) != 0)) goto ie123;
        void * t126 = GM_LD(void *, ((uint8_t *)&t125));
        gm_l20 = t126;
        void * t128 = gm_l0;
        void * t129 = (void *)(((uint8_t *)t128) + 96);
        void * t130 = gm_l0;
        void * t131 = GM_LD(void *, (((uint8_t *)t130) + 0));
        void * t132 = gm_l20;
        gm_t88rcucg1b7uk t133 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t132) + 0));
        gm_t88rcucg1b7uk t134 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t129, t131, t133);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t88rcucg1b7uk t135 = t134;
        t122 = t135;
        goto ix123;
ie123:;
        void * t137 = gm_l5;
        gm_t88rcucg1b7uk t138 = gm_f_unit_2age34u((void *)NULL, cx, NULL, t137);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t88rcucg1b7uk t139 = t138;
        t122 = t139;
ix123:;
        gm_l21 = t122;
    }
    {
        void * t140 = gm_l0;
        gm_t88rcucg1b7uk t141 = gm_l21;
        gm_tl874t2atnlh3 t142 = gm_l4;
        uint32_t t143 = gm_f_temp_ilbvj8q((void *)NULL, cx, NULL, t140, t141, t142);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l22 = t143;
    }
    {
        void * t144 = gm_l5;
        gm_t88rcucg1b7uk t145 = gm_f_unit_2age34u((void *)NULL, cx, NULL, t144);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t9i38ih07bojq t146;
        memset((uint8_t *)&t146 + 60, 0, 4);
        memcpy((uint8_t *)&t146 + 0, ((uint8_t *)a1), 40);
        void * t147 = gm_l2;
        GM_ST(void *, (uint8_t *)&t146 + 40, t147);
        void * t148 = gm_l6;
        GM_ST(void *, (uint8_t *)&t146 + 48, t148);
        uint32_t t149 = gm_l22;
        GM_ST(uint32_t, (uint8_t *)&t146 + 56, t149);
        gm_t5brlu7fal70j t150;
        memset(&t150, 0, sizeof t150);
        GM_ST(uint8_t, ((uint8_t *)&t150 + 0), ((uint8_t)56ULL));
        GM_ST(gm_t9i38ih07bojq, ((uint8_t *)&t150 + 8), t146);
        gm_tl874t2atnlh3 t151 = gm_l4;
        void * t152 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t145, &t150, t151);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t153 = 0;
        F.ret = t152;
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
