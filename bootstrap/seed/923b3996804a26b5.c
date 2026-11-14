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
   record at the top of the frame (x86-64, Clang on AArch64), and below them where it
   keeps it at the bottom (GCC on AArch64 without a stack protector): there the frame is
   above it already. The comparison with the frame `p` tells which, and folds away. */
#define GM_STACK_CHECK(p, n) do { \
    uintptr_t gm_fa = (uintptr_t)__builtin_frame_address(0); \
    if (gm_fa < gm_stack_limit + (gm_fa > (uintptr_t)(p) ? (uintptr_t)(n) + sizeof(*(p)) : 0)) gm_stack_overflow(); \
} while (0)
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
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint64_t f0; } gm_t4ce0jd3tdro4;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
_Static_assert(sizeof(gm_cseahvg8v7p0vs) == 12 && _Alignof(gm_cseahvg8v7p0vs) == 4 && offsetof(gm_cseahvg8v7p0vs, f0) == 0 && offsetof(gm_cseahvg8v7p0vs, f1) == 4 && offsetof(gm_cseahvg8v7p0vs, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
_Static_assert(sizeof(gm_t4ce0jd3tdro4) == 8 && _Alignof(gm_t4ce0jd3tdro4) == 8 && offsetof(gm_t4ce0jd3tdro4, f0) == 0, "layout of `Instant`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
typedef struct { gm_tl874t2atnlh3 f0; uint8_t f1; uint8_t pe[3]; } gm_toceffb697p0u;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { gm_b16_8 f0; gm_th2lqbo77pvsc f1; } gm_t6ok5tvcjc200;
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
typedef struct { gm_b16_8 f0; gm_t9qkh0rsooc8d f1; uint8_t p2[7]; gm_b16_8 f2; gm_th2lqbo77pvsc f3; } gm_tl5cuhjfnc4cr;
_Static_assert(sizeof(gm_toceffb697p0u) == 16 && _Alignof(gm_toceffb697p0u) == 4 && offsetof(gm_toceffb697p0u, f0) == 0 && offsetof(gm_toceffb697p0u, f1) == 12, "layout of `?Span`");
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
typedef struct { uint32_t f0; uint32_t f1; gm_toceffb697p0u f2; } gm_tu6ladb6432hk;
_Static_assert(sizeof(gm_t6ok5tvcjc200) == 40 && _Alignof(gm_t6ok5tvcjc200) == 8 && offsetof(gm_t6ok5tvcjc200, f0) == 0 && offsetof(gm_t6ok5tvcjc200, f1) == 16, "layout of `Key`");
typedef struct { gm_tl874t2atnlh3 f0; uint8_t p1[4]; gm_t6ok5tvcjc200 f1; } gm_t8scu1osgcra5;
typedef struct { gm_tl5cuhjfnc4cr f0; gm_tl874t2atnlh3 f1; uint32_t f2; uint32_t f3; uint8_t pe[4]; } gm_tfgiovingnnea;
_Static_assert(sizeof(gm_tl5cuhjfnc4cr) == 64 && _Alignof(gm_tl5cuhjfnc4cr) == 8 && offsetof(gm_tl5cuhjfnc4cr, f0) == 0 && offsetof(gm_tl5cuhjfnc4cr, f1) == 16 && offsetof(gm_tl5cuhjfnc4cr, f2) == 24 && offsetof(gm_tl5cuhjfnc4cr, f3) == 40, "layout of `Item`");
_Static_assert(sizeof(gm_tu6ladb6432hk) == 24 && _Alignof(gm_tu6ladb6432hk) == 4 && offsetof(gm_tu6ladb6432hk, f0) == 0 && offsetof(gm_tu6ladb6432hk, f1) == 4 && offsetof(gm_tu6ladb6432hk, f2) == 8, "layout of `Narrowest`");
_Static_assert(sizeof(gm_t8scu1osgcra5) == 56 && _Alignof(gm_t8scu1osgcra5) == 8 && offsetof(gm_t8scu1osgcra5, f0) == 0 && offsetof(gm_t8scu1osgcra5, f1) == 16, "layout of `HoverResult`");
_Static_assert(sizeof(gm_tfgiovingnnea) == 88 && _Alignof(gm_tfgiovingnnea) == 8 && offsetof(gm_tfgiovingnnea, f0) == 0 && offsetof(gm_tfgiovingnnea, f1) == 64 && offsetof(gm_tfgiovingnnea, f2) == 76 && offsetof(gm_tfgiovingnnea, f3) == 80, "layout of `LocalScope`");
struct gm_fr_declared_at_31b77n7 {
    gm_frame h;
    gm_tu6ladb6432hk l3; /* n */
    gm_closure l4; /* consider */
    gm_tfgiovingnnea l10; /* sc */
    gm_toceffb697p0u ret;
    gm_b0_1 w2;
};
struct gm_fr_for_body_3en4m89 {
    gm_frame h;
    gm_t8scu1osgcra5 l0; /* <item> */
    gm_t8scu1osgcra5 l1; /* kv */
    gm_b0_1 ret;
};
struct gm_fr_for_body_e61ld42 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_words_eeicdvm {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* out */
    gm_trqfvmjtt4sij ret;
    gm_b0_1 w0;
};
gm_b0_1 gm_dtdfeetqn95b_0(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a1);
gm_t4ce0jd3tdro4 gm_dtf4dspmusp5_0(void *env, gm_node *cx, gm_node *inst);
int64_t gm_dtf4dspmusp5_1(void *env, gm_node *cx, gm_node *inst);
gm_b0_1 gm_dtf4dspmusp5_2(void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_3en4m89(void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_for_body_6ellthc(void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_for_body_bq6tmqb(void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_e61ld42(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_for_body_jvjn1fs(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_for_body_m8t719h(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_for_body_qg33ouo(void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_for_body_vn00ddp(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_3ef746b(void * a0, gm_b16_8 a1);
extern uint8_t gm_c0ahkuor6r5[9] __attribute__((aligned(1)));
extern uint8_t gm_c0lqc5l8usq[6] __attribute__((aligned(1)));
extern uint8_t gm_c10mbbl74j5[3] __attribute__((aligned(1)));
extern uint8_t gm_c1n574q3spi[8] __attribute__((aligned(8)));
extern uint8_t gm_c2gnvqjl56t[4] __attribute__((aligned(1)));
extern uint8_t gm_c2t30gdg5i0[8] __attribute__((aligned(8)));
extern uint8_t gm_c445n46obmu[13] __attribute__((aligned(1)));
extern uint8_t gm_c4h9gubvsld[16] __attribute__((aligned(8)));
extern uint8_t gm_c4qsd90b3sp[3] __attribute__((aligned(1)));
extern uint8_t gm_c57p52av493[18] __attribute__((aligned(1)));
extern uint8_t gm_c5c4j291k0n[1] __attribute__((aligned(1)));
extern uint8_t gm_c78tgi0epnk[2] __attribute__((aligned(1)));
extern uint8_t gm_c7qpt44lqjv[8] __attribute__((aligned(1)));
extern uint8_t gm_c8ib2k3r9g2[61] __attribute__((aligned(1)));
extern uint8_t gm_c8nfq84m99e[8] __attribute__((aligned(1)));
extern uint8_t gm_c8umbcba0h7[5] __attribute__((aligned(1)));
extern uint8_t gm_c90gc8fsnn0[9] __attribute__((aligned(1)));
extern uint8_t gm_c9e8ns5bad4[16] __attribute__((aligned(8)));
extern uint8_t gm_c9vplruo2a2[12] __attribute__((aligned(1)));
extern uint8_t gm_cbg49j387l1[6] __attribute__((aligned(1)));
extern uint8_t gm_cbms0bo4q1a[16] __attribute__((aligned(8)));
extern uint8_t gm_ccg980jkcrh[1307] __attribute__((aligned(1)));
extern uint8_t gm_cd08h1gi9e5[7] __attribute__((aligned(1)));
extern uint8_t gm_cdkdqgvah3b[14] __attribute__((aligned(1)));
extern uint8_t gm_cegnsgpm600[10] __attribute__((aligned(1)));
extern uint8_t gm_cenqalictlb[7] __attribute__((aligned(1)));
extern uint8_t gm_cf28fp1b3j9[12] __attribute__((aligned(1)));
extern uint8_t gm_cf8dng56muv[12] __attribute__((aligned(1)));
extern uint8_t gm_cfhksknhucl[16] __attribute__((aligned(8)));
extern uint8_t gm_cfpt4ph1ii9[24] __attribute__((aligned(1)));
extern uint8_t gm_cgmufj2btu3[99] __attribute__((aligned(1)));
extern uint8_t gm_ci0uc4kk6nj[5] __attribute__((aligned(1)));
extern uint8_t gm_ci6akgm82k1[15] __attribute__((aligned(1)));
extern uint8_t gm_cic0r6uhrl9[14] __attribute__((aligned(1)));
extern uint8_t gm_cift5pqfpf8[5] __attribute__((aligned(1)));
extern uint8_t gm_cii3u4ppm6k[16] __attribute__((aligned(8)));
extern uint8_t gm_cj2g6e1rcfd[7] __attribute__((aligned(1)));
extern uint8_t gm_cj3i6am3onu[14] __attribute__((aligned(1)));
extern uint8_t gm_cjafjj2b83a[2] __attribute__((aligned(1)));
extern uint8_t gm_cjt60aq4qvt[8] __attribute__((aligned(8)));
extern uint8_t gm_cjup0hpsi95[12] __attribute__((aligned(1)));
extern uint8_t gm_ck5m2h3unvq[6] __attribute__((aligned(1)));
extern uint8_t gm_ckasdtpni1c[8] __attribute__((aligned(8)));
extern uint8_t gm_clp0sh2cgr2[16] __attribute__((aligned(8)));
extern uint8_t gm_cmdr2nifebi[9] __attribute__((aligned(1)));
extern uint8_t gm_cmsb06prlvh[14] __attribute__((aligned(1)));
extern uint8_t gm_cmuoir8all0[4] __attribute__((aligned(1)));
extern uint8_t gm_cn9jip912hb[16] __attribute__((aligned(1)));
extern uint8_t gm_cntetvl1ho0[28] __attribute__((aligned(1)));
extern uint8_t gm_cnupg9tkv4t[4] __attribute__((aligned(1)));
extern uint8_t gm_cnv5thfiqas[49] __attribute__((aligned(1)));
extern uint8_t gm_co3kl02erms[10] __attribute__((aligned(1)));
extern uint8_t gm_colgvso06jq[3] __attribute__((aligned(1)));
extern uint8_t gm_covm5e0en6l[12] __attribute__((aligned(1)));
extern uint8_t gm_cp3tou70mi3[8] __attribute__((aligned(1)));
extern uint8_t gm_cpm8oqgl16t[10] __attribute__((aligned(1)));
extern uint8_t gm_crmhomsqkn5[5] __attribute__((aligned(1)));
extern uint8_t gm_cs3r3u3eelc[6] __attribute__((aligned(1)));
extern uint8_t gm_ctk5347uqth[4] __attribute__((aligned(4)));
extern uint8_t gm_ctmi68o75hk[26] __attribute__((aligned(1)));
extern uint8_t gm_ctrudjfeksb[6] __attribute__((aligned(1)));
extern uint8_t gm_cvt51san54i[12] __attribute__((aligned(1)));
extern void *const gm_hddfeetqn95b[];
extern void *const gm_hdf4dspmusp5[];
extern void *const gm_hl3gh4gojekg[];
extern void *const gm_hl8sdnavrdjn[];
extern void *const gm_hlal1cg2mvd1[];
extern void *const gm_hlbfnanio71s[];
extern void *const gm_hlgmpdevos39[];
extern void *const gm_hlipnojuqrvq[];
extern void *const gm_hlpn5rfhnj54[];
extern void *const gm_hlrpdhe6kjfr[];
extern uint8_t gm_k0d09vor0rn[40] __attribute__((aligned(8)));
extern uint8_t gm_k0j6as2o5ac[40] __attribute__((aligned(8)));
extern uint8_t gm_k2rd7nce5lv[40] __attribute__((aligned(8)));
extern uint8_t gm_k3u24nfalvr[40] __attribute__((aligned(8)));
extern uint8_t gm_kat3hjq5aa3[40] __attribute__((aligned(8)));
extern uint8_t gm_kb4m9ri21ik[40] __attribute__((aligned(8)));
extern uint8_t gm_kb8poqrtf7g[40] __attribute__((aligned(8)));
extern uint8_t gm_kcaojba2jhr[40] __attribute__((aligned(8)));
extern uint8_t gm_ke6nl7d0rco[40] __attribute__((aligned(8)));
extern uint8_t gm_kf8nrh0sp8b[40] __attribute__((aligned(8)));
extern uint8_t gm_kinrtga7ov4[40] __attribute__((aligned(8)));
extern uint8_t gm_kl8do6h9dt6[40] __attribute__((aligned(8)));
extern uint8_t gm_knna6g3th5t[40] __attribute__((aligned(8)));
extern uint8_t gm_kns860jaqph[40] __attribute__((aligned(8)));
extern uint8_t gm_koeeht11j75[40] __attribute__((aligned(8)));
extern uint8_t gm_krkq6pebrgk[40] __attribute__((aligned(8)));
extern const uint8_t gm_s01tfkbjoijgc[23];
extern const uint8_t gm_s06kk1tae680i[27];
extern const uint8_t gm_s08olofbep0dd[14];
extern const uint8_t gm_s09fhtm8l4rep[4];
extern const uint8_t gm_s0c4i5le3mc2l[23];
extern const uint8_t gm_s0fdurfqutrtc[28];
extern const uint8_t gm_s0ghv8aql4j5t[19];
extern const uint8_t gm_s0h2dl2t3n8dh[25];
extern const uint8_t gm_s0kc1hjsjf2hu[11];
extern const uint8_t gm_s0ku6rqnajsui[24];
extern const uint8_t gm_s0m7e0jf69c0n[30];
extern const uint8_t gm_s0of2eragnise[6];
extern const uint8_t gm_s0pi0emdqd3vb[2];
extern const uint8_t gm_s0sgp82v8se7l[11];
extern const uint8_t gm_s0tnnm2dhgmh2[25];
extern const uint8_t gm_s11aakm4d1c2s[19];
extern const uint8_t gm_s11b339fv56g1[13];
extern const uint8_t gm_s13hosdv3k3g6[21];
extern const uint8_t gm_s14bn21gpuj3j[3];
extern const uint8_t gm_s16i1ikaguc63[41];
extern const uint8_t gm_s1740npcs2qqc[80];
extern const uint8_t gm_s18773ijuej6p[26];
extern const uint8_t gm_s197lbpp098pv[9];
extern const uint8_t gm_s19jg4ct3aq11[6];
extern const uint8_t gm_s1a10cqdk6nil[19];
extern const uint8_t gm_s1faagcukkgns[10];
extern const uint8_t gm_s1i48s61oiget[34];
extern const uint8_t gm_s1jim1okq5o2b[192];
extern const uint8_t gm_s1karb2397v3p[20];
extern const uint8_t gm_s1lvtjpshteb3[11];
extern const uint8_t gm_s1oqiahdao8ce[10];
extern const uint8_t gm_s1p9digtc20ub[7];
extern const uint8_t gm_s1q9ms0kviddj[4];
extern const uint8_t gm_s1tdsk62jj75n[8];
extern const uint8_t gm_s22g482qk7a40[3];
extern const uint8_t gm_s22tv9c5r20tl[4];
extern const uint8_t gm_s23ifilt9knnp[18];
extern const uint8_t gm_s2431f6f423u2[21];
extern const uint8_t gm_s24qpbg6rctcd[17];
extern const uint8_t gm_s26219ruuua5s[19];
extern const uint8_t gm_s2737i1iq57nf[5];
extern const uint8_t gm_s28fmt6o4roj7[10];
extern const uint8_t gm_s297r0ooe1bmq[29];
extern const uint8_t gm_s2b7lcpvalpdu[14];
extern const uint8_t gm_s2jg45h85jjgj[37];
extern const uint8_t gm_s2jvv6mjt9vs3[13];
extern const uint8_t gm_s2k1ajrnodb02[23];
extern const uint8_t gm_s2ke5iso9cho7[18];
extern const uint8_t gm_s2keeidsqic9l[41];
extern const uint8_t gm_s2kieedtscsk1[12];
extern const uint8_t gm_s2lav3n6geg46[17];
extern const uint8_t gm_s2lhokjucfplm[10];
extern const uint8_t gm_s2mvbldeg43ph[9];
extern const uint8_t gm_s2n2ugogq2cq2[24];
extern const uint8_t gm_s2s2hv5oav6fb[29];
extern const uint8_t gm_s2t6ld4n1pdgu[5];
extern const uint8_t gm_s2v5amvfkfoll[3];
extern const uint8_t gm_s30117bvhp3fe[17];
extern const uint8_t gm_s325452j0h962[17];
extern const uint8_t gm_s32vm0u5vgiu5[18];
extern const uint8_t gm_s338jbcmhh92n[20];
extern const uint8_t gm_s33esn1pg9lma[12];
extern const uint8_t gm_s34hhp1ucrags[12];
extern const uint8_t gm_s37otbiaffdaa[25];
extern const uint8_t gm_s38n4b5q0dbil[45];
extern const uint8_t gm_s3a91i4ku76eo[81];
extern const uint8_t gm_s3agd8o01q2ss[62];
extern const uint8_t gm_s3bfo4sq48nsb[5];
extern const uint8_t gm_s3bgi1rlb5ne1[8];
extern const uint8_t gm_s3h2hffrlol99[26];
extern const uint8_t gm_s3h586tvdle6o[31];
extern const uint8_t gm_s3ia9jm7npe6t[10];
extern const uint8_t gm_s3iij9a939gcv[15];
extern const uint8_t gm_s3nauht3nggr8[13];
extern const uint8_t gm_s3p4vlg0qjpu0[46];
extern const uint8_t gm_s3pmm119d0kjq[16];
extern const uint8_t gm_s3sfk8t93kgsv[13];
extern const uint8_t gm_s3suuflfjt6mu[11];
extern const uint8_t gm_s3tpa8q2q7llg[17];
extern const uint8_t gm_s3ukoldgd7ran[24];
extern const uint8_t gm_s43j3q24pq7de[26];
extern const uint8_t gm_s472t5rs14ilt[6];
extern const uint8_t gm_s476hgsnn8633[47];
extern const uint8_t gm_s4a1r52bl6d5j[87];
extern const uint8_t gm_s4aqikl44lhv8[4];
extern const uint8_t gm_s4cm3nbv0nhtk[21];
extern const uint8_t gm_s4hp8mhves75a[41];
extern const uint8_t gm_s4k2ktjht9l4a[13];
extern const uint8_t gm_s4k7tmsh4glbh[77];
extern const uint8_t gm_s4nquq7iq8v5h[29];
extern const uint8_t gm_s4pfhdi3f6f58[44];
extern const uint8_t gm_s4srjt2tusqhn[30];
extern const uint8_t gm_s4u8fr1tm8vl7[40];
extern const uint8_t gm_s50l7uhgcnu1t[7];
extern const uint8_t gm_s50uau4el7or4[17];
extern const uint8_t gm_s514u4evks75f[21];
extern const uint8_t gm_s51q2oivdrjj3[15];
extern const uint8_t gm_s54fgk81cum2e[18];
extern const uint8_t gm_s56ntjkdhhepu[4];
extern const uint8_t gm_s58ndvsqjhesf[6];
extern const uint8_t gm_s59dg443u5bte[41];
extern const uint8_t gm_s5bfofp3bg4br[9];
extern const uint8_t gm_s5bhk4dj6e492[73];
extern const uint8_t gm_s5c6gmkd367jj[46];
extern const uint8_t gm_s5dbqnns2epkg[6];
extern const uint8_t gm_s5el4up5fp2qc[45];
extern const uint8_t gm_s5f630j5b0vm8[22];
extern const uint8_t gm_s5gff7p92h71s[16];
extern const uint8_t gm_s5gt30aqh30fl[19];
extern const uint8_t gm_s5herb5iq95im[5];
extern const uint8_t gm_s5j185b1e4leb[89];
extern const uint8_t gm_s5k4jvr23rbdi[11];
extern const uint8_t gm_s5l94dg13r1qf[14];
extern const uint8_t gm_s5l9jdmrtaean[11];
extern const uint8_t gm_s5r4lidhg0cfv[64];
extern const uint8_t gm_s5s0ahr8bl55p[12];
extern const uint8_t gm_s60adpphft7s5[5];
extern const uint8_t gm_s60bni5maq4df[12];
extern const uint8_t gm_s61upj6a9rej6[22];
extern const uint8_t gm_s622mvvjraklk[43];
extern const uint8_t gm_s62k5o20indcj[12];
extern const uint8_t gm_s63vjuo83pn20[15];
extern const uint8_t gm_s64s5a4ht3148[69];
extern const uint8_t gm_s65fsr1v7i389[135];
extern const uint8_t gm_s66k76ki3e7an[22];
extern const uint8_t gm_s66uvortgrtsg[4];
extern const uint8_t gm_s670np414hdkp[8];
extern const uint8_t gm_s68avpqal8nbc[12];
extern const uint8_t gm_s68rktnu8slfa[25];
extern const uint8_t gm_s698sbcv3nmjk[69];
extern const uint8_t gm_s6bmkes68nfio[5];
extern const uint8_t gm_s6dk6h0otj4rd[13];
extern const uint8_t gm_s6gnskloov1dv[15];
extern const uint8_t gm_s6ifo0vt5mlt9[6];
extern const uint8_t gm_s6l0iqp1ptptl[17];
extern const uint8_t gm_s6p2jltb0rdds[6];
extern const uint8_t gm_s6rn45hsruulp[9];
extern const uint8_t gm_s6s96daqmhcs9[2];
extern const uint8_t gm_s6vfshjjhk2g7[19];
extern const uint8_t gm_s6vtoqhgjb0nd[6];
extern const uint8_t gm_s70nlbtnmugoc[10];
extern const uint8_t gm_s743265bqlq4a[14];
extern const uint8_t gm_s797e8kr2a9qh[14];
extern const uint8_t gm_s79dkmc2rhan7[12];
extern const uint8_t gm_s7ch91qn11t34[25];
extern const uint8_t gm_s7f3pdsji9g3a[9];
extern const uint8_t gm_s7hndfpk1u0p0[7];
extern const uint8_t gm_s7ilf06vf0fi1[22];
extern const uint8_t gm_s7mq585fepqnh[6];
extern const uint8_t gm_s823123ng02vo[44];
extern const uint8_t gm_s86bd4orcs15b[7];
extern const uint8_t gm_s87c4jti7ujk5[31];
extern const uint8_t gm_s87g1ufoaqi74[8];
extern const uint8_t gm_s8av3ru5o85v4[6];
extern const uint8_t gm_s8becutukvgrh[17];
extern const uint8_t gm_s8dbdqj1sqibj[3];
extern const uint8_t gm_s8e72gdumihb1[7];
extern const uint8_t gm_s8fhmkhb94g4l[8];
extern const uint8_t gm_s8hpuukc6o9qu[9];
extern const uint8_t gm_s8jc0a1p1kvtf[73];
extern const uint8_t gm_s8kki44ucv73v[11];
extern const uint8_t gm_s8lpasdulkmbv[28];
extern const uint8_t gm_s8p6h6db1ftub[32];
extern const uint8_t gm_s8uchl9nh4d11[33];
extern const uint8_t gm_s94n39ltlvmhe[36];
extern const uint8_t gm_s95g98ft0cnt1[155];
extern const uint8_t gm_s96uiluq5ufvd[66];
extern const uint8_t gm_s9akpbuv78p3m[109];
extern const uint8_t gm_s9b7nekk74u38[3];
extern const uint8_t gm_s9cb55ipdnrli[15];
extern const uint8_t gm_s9g30d9lksra5[3];
extern const uint8_t gm_s9h40dg66q82b[58];
extern const uint8_t gm_s9hst152mpfjl[42];
extern const uint8_t gm_s9j65lfldjn8j[20];
extern const uint8_t gm_s9jrvf2ogt807[17];
extern const uint8_t gm_s9kj8vgi6430a[6];
extern const uint8_t gm_s9krd4pni7gv1[6];
extern const uint8_t gm_s9lpec5ab0tsm[111];
extern const uint8_t gm_s9ocqto6ckbos[58];
extern const uint8_t gm_s9omga3p3drmu[51];
extern const uint8_t gm_s9qem7js6pnep[15];
extern const uint8_t gm_s9r72jgd5hb05[5];
extern const uint8_t gm_s9rkpo2ljmui9[50];
extern const uint8_t gm_s9sif80bjh9f7[12];
extern const uint8_t gm_sa2pnk1jujp5p[23];
extern const uint8_t gm_sa39uag0oed9a[16];
extern const uint8_t gm_sa56bd0p8nuib[143];
extern const uint8_t gm_sa68th8jv179j[9];
extern const uint8_t gm_sa82k8ai03q0v[41];
extern const uint8_t gm_sa8dse4jk5ibv[10];
extern const uint8_t gm_sa9kmm3ih11r5[28];
extern const uint8_t gm_saab9e9ochkfq[56];
extern const uint8_t gm_saicih5tjo4fl[43];
extern const uint8_t gm_saimepa8bfkkm[3];
extern const uint8_t gm_saja5hcbljm83[5];
extern const uint8_t gm_sajefbn69vf76[12];
extern const uint8_t gm_sao4mumvadoct[4];
extern const uint8_t gm_saojtqmfeak72[8];
extern const uint8_t gm_sarb7aqhpoiqu[13];
extern const uint8_t gm_savb5oasie77f[37];
extern const uint8_t gm_sb03flnhs22vb[3];
extern const uint8_t gm_sb1t57j9241lh[74];
extern const uint8_t gm_sb23l4chfnd3f[10];
extern const uint8_t gm_sb26s4qotngkv[27];
extern const uint8_t gm_sb5avtmeu66m7[144];
extern const uint8_t gm_sb6bik10m4ppo[74];
extern const uint8_t gm_sb6jpc12sa7uj[4];
extern const uint8_t gm_sbc0kjtg5aiab[5];
extern const uint8_t gm_sbdfikdtgvq9q[28];
extern const uint8_t gm_sbdp6ovojv1tg[10];
extern const uint8_t gm_sbe7dr7t400bh[16];
extern const uint8_t gm_sbiak9qhf4tcb[12];
extern const uint8_t gm_sbiergg61olmu[41];
extern const uint8_t gm_sbj0273u8beka[7];
extern const uint8_t gm_sbj0co7eet3k9[8];
extern const uint8_t gm_sbm0k1981kgpb[40];
extern const uint8_t gm_sbo7sr73hvg7d[10];
extern const uint8_t gm_sbqk0qmd2ampu[37];
extern const uint8_t gm_sc6gojr5sjib4[21];
extern const uint8_t gm_scffrkp2h220o[5];
extern const uint8_t gm_sch3p0ffmutvn[16];
extern const uint8_t gm_sciq0a78hirud[25];
extern const uint8_t gm_scj0sm2kpivk4[9];
extern const uint8_t gm_scmaavulfehmr[5];
extern const uint8_t gm_sco971hnpq7nk[49];
extern const uint8_t gm_scomp2tns6p6f[16];
extern const uint8_t gm_scs1evojsik8q[4];
extern const uint8_t gm_scttrk7b2nnqb[24];
extern const uint8_t gm_scv3jvaidk3vn[21];
extern const uint8_t gm_scv8i3jrgh2rk[65];
extern const uint8_t gm_scvj6up3gbolp[25];
extern const uint8_t gm_sd0pnvfh21o90[11];
extern const uint8_t gm_sd1jj257q6flv[17];
extern const uint8_t gm_sd2g8d0vbj98u[7];
extern const uint8_t gm_sd6a57g2jmtks[11];
extern const uint8_t gm_sd7spmqdlcohl[25];
extern const uint8_t gm_sd9i4iia5r4dp[72];
extern const uint8_t gm_sd9ri1vmgds82[9];
extern const uint8_t gm_sdealia24icjj[40];
extern const uint8_t gm_sdjh2jdveb2fb[41];
extern const uint8_t gm_sdmle2qsm85vh[11];
extern const uint8_t gm_sdnc8nilmmql1[23];
extern const uint8_t gm_sdpi8e9pb70dm[47];
extern const uint8_t gm_sds1asc07m30k[16];
extern const uint8_t gm_sdt2alvv2h6jh[48];
extern const uint8_t gm_sdvuu27hpddqp[11];
extern const uint8_t gm_se0ivu8cf46o8[12];
extern const uint8_t gm_se2fbs5jfigcc[11];
extern const uint8_t gm_se4629k8oa1uo[2];
extern const uint8_t gm_se48mcej0nimj[16];
extern const uint8_t gm_se4kub9a3omhd[5];
extern const uint8_t gm_se5118fq98e06[88];
extern const uint8_t gm_se64h924hq7dp[24];
extern const uint8_t gm_seaesh4v4toj4[76];
extern const uint8_t gm_seb76g152eseg[4];
extern const uint8_t gm_sej7fch3vkla7[24];
extern const uint8_t gm_sejc19hk8m1pk[20];
extern const uint8_t gm_sel4m8qdnq9n9[5];
extern const uint8_t gm_semhbv8seph85[21];
extern const uint8_t gm_seo41mk3n3ro5[13];
extern const uint8_t gm_seohfka1b0c2g[47];
extern const uint8_t gm_seosa3iobfc09[35];
extern const uint8_t gm_seqfup76f04qu[9];
extern const uint8_t gm_seqpkkqc4c757[14];
extern const uint8_t gm_seqrg7cf0sejb[33];
extern const uint8_t gm_seu8takp0sa1c[9];
extern const uint8_t gm_sf00qdbn1q8j8[9];
extern const uint8_t gm_sf8bp31mrq32l[19];
extern const uint8_t gm_sf9a2kg3eo1gs[2];
extern const uint8_t gm_sfbtgpom3l6k2[5];
extern const uint8_t gm_sfd839bbgsa7s[63];
extern const uint8_t gm_sfdbfrv46lihd[2];
extern const uint8_t gm_sffcmqsfdatal[47];
extern const uint8_t gm_sfht1gt5dn60n[11];
extern const uint8_t gm_sfms0h2mph067[11];
extern const uint8_t gm_sft71cpvvrvmk[12];
extern const uint8_t gm_sfvnlkgl40moh[72];
extern const uint8_t gm_sg26if17vtome[23];
extern const uint8_t gm_sg3453glm1u32[14];
extern const uint8_t gm_sg3fjsrtce2g0[41];
extern const uint8_t gm_sg5gdnrith2i6[3];
extern const uint8_t gm_sg6dkeojalp6f[14];
extern const uint8_t gm_sg87p4r5apham[36];
extern const uint8_t gm_sg90vqvamkjbo[11];
extern const uint8_t gm_sg96ffu32fbap[42];
extern const uint8_t gm_sgaj4glha8q4e[3];
extern const uint8_t gm_sgdk0atob5av0[6];
extern const uint8_t gm_sgetvvdiqkhsm[18];
extern const uint8_t gm_sgiqpj781s1a9[36];
extern const uint8_t gm_sgjc0ebf56hf4[8];
extern const uint8_t gm_sgjt1pifmqnsi[35];
extern const uint8_t gm_sgktm44r9e4ua[6];
extern const uint8_t gm_sgmcjv273u32m[11];
extern const uint8_t gm_sgo9an2jqt4mr[13];
extern const uint8_t gm_sgosg5fctcl72[17];
extern const uint8_t gm_sgrd6s4kl4dhp[29];
extern const uint8_t gm_sgtu622jht4g2[51];
extern const uint8_t gm_sgva3apqq9jo2[68];
extern const uint8_t gm_sh133eanuhqjo[10];
extern const uint8_t gm_sh7j5igqqg1id[10];
extern const uint8_t gm_sh8atl0agp75n[13];
extern const uint8_t gm_sh8l62ma11hq5[70];
extern const uint8_t gm_sh8osg42sl1gi[145];
extern const uint8_t gm_sh90fjlgqrlfc[7];
extern const uint8_t gm_shcail1g86pp0[26];
extern const uint8_t gm_shfckrpors679[49];
extern const uint8_t gm_shfvspt8ngibo[19];
extern const uint8_t gm_shhr1hlq1ethc[4];
extern const uint8_t gm_shi1s7rq8m35t[40];
extern const uint8_t gm_shsfl3e9phpsi[23];
extern const uint8_t gm_sht12dmgii44r[41];
extern const uint8_t gm_si1b582k23708[50];
extern const uint8_t gm_si2ih874n1gou[10];
extern const uint8_t gm_si42rotkq2ll8[14];
extern const uint8_t gm_si4cfd3bll781[9];
extern const uint8_t gm_si9mfda4b719e[63];
extern const uint8_t gm_sibidb2jab0ss[14];
extern const uint8_t gm_sijukd7vsocnt[68];
extern const uint8_t gm_sinve1cmean4f[24];
extern const uint8_t gm_sio9tu2ig98ap[21];
extern const uint8_t gm_siph7o6oug1g2[14];
extern const uint8_t gm_sir0litoa3fk0[18];
extern const uint8_t gm_sisa53plcfedi[14];
extern const uint8_t gm_siscc5ella8ma[4];
extern const uint8_t gm_sisjhd4gebqhp[25];
extern const uint8_t gm_sivm2ee5886a0[8];
extern const uint8_t gm_sj2gsb941ds9q[21];
extern const uint8_t gm_sj5fh226119gq[14];
extern const uint8_t gm_sj6enltl2qi3c[21];
extern const uint8_t gm_sj9krj49k3580[10];
extern const uint8_t gm_sjbb259gf4uuc[8];
extern const uint8_t gm_sjbl17pjsjm48[5];
extern const uint8_t gm_sjegrp8u9r3n3[8];
extern const uint8_t gm_sjg57h040l08d[56];
extern const uint8_t gm_sjhccr3h0np2l[15];
extern const uint8_t gm_sjhi1ic83fapl[1443];
extern const uint8_t gm_sjhsg0a8cb7s7[42];
extern const uint8_t gm_sjibpf3glbc54[9];
extern const uint8_t gm_sjifgfpiu6k52[6];
extern const uint8_t gm_sjjcm6bafumir[47];
extern const uint8_t gm_sjjehkhgpkn3v[10];
extern const uint8_t gm_sjltai3b82h79[4];
extern const uint8_t gm_sjm9r4dj90vdb[13];
extern const uint8_t gm_sjmqausipumva[26];
extern const uint8_t gm_sjnvtll1pqkj7[11];
extern const uint8_t gm_sjoj5mss14s83[6];
extern const uint8_t gm_sjp9oaluqrtr3[48];
extern const uint8_t gm_sjpvc4ek9dber[39];
extern const uint8_t gm_sjr44k79a48mq[4];
extern const uint8_t gm_sk024chguo1g1[9];
extern const uint8_t gm_sk3630dksnfoh[24];
extern const uint8_t gm_sk50arqpf74k6[6];
extern const uint8_t gm_sk7tgk6pi62vu[14];
extern const uint8_t gm_skarrfopl04v8[23];
extern const uint8_t gm_skbblqa84g6pp[57];
extern const uint8_t gm_skbosgm06mmhc[57];
extern const uint8_t gm_skhd03cqcl7er[28];
extern const uint8_t gm_ski6lj2hn0gg4[35];
extern const uint8_t gm_skiqr6ckq7kq3[12];
extern const uint8_t gm_skkuka4uqa99p[12];
extern const uint8_t gm_skm4ndn7op5l5[14];
extern const uint8_t gm_skmh83q8fkhmc[29];
extern const uint8_t gm_skocj074p3r97[9];
extern const uint8_t gm_skp02jnee3dfj[35];
extern const uint8_t gm_skp6qpvju2t5u[19];
extern const uint8_t gm_skriisqpofdin[3];
extern const uint8_t gm_sl52kab9m437h[74];
extern const uint8_t gm_sl5g3dl3mq5ih[48];
extern const uint8_t gm_sl6pi8la5phb6[20];
extern const uint8_t gm_sl85qrve5cdud[31];
extern const uint8_t gm_sl9jmb4urj4ud[13];
extern const uint8_t gm_slbdftqell5t4[10];
extern const uint8_t gm_slbgk65sat4bm[8];
extern const uint8_t gm_slc0htp15p4h9[41];
extern const uint8_t gm_sld7tlsq8opg9[15];
extern const uint8_t gm_sldd44oagbb0a[10];
extern const uint8_t gm_slesj9fnpbent[24];
extern const uint8_t gm_slk3h69j7tjn1[41];
extern const uint8_t gm_slkhlou8kve8h[32];
extern const uint8_t gm_slkoo2ifkem47[62];
extern const uint8_t gm_slmlmpuma600i[85];
extern const uint8_t gm_slp04vsc9cddi[49];
extern const uint8_t gm_slp60pbhoot2j[15];
extern const uint8_t gm_slrd70lo3b6iq[15];
extern const uint8_t gm_sltao39ib9870[6];
extern const uint8_t gm_sltvces0c7l80[14];
extern const uint8_t gm_sluaq8lcmp31d[68];
extern const uint8_t gm_sluvntf7gk99f[3];
extern const uint8_t gm_sm08k35r8tv0e[26];
extern const uint8_t gm_sm0gs6tv176le[8];
extern const uint8_t gm_sm1gsdt21ielk[41];
extern const uint8_t gm_sm2k5tp7dtg07[4];
extern const uint8_t gm_sm2qh69a2gq0m[51];
extern const uint8_t gm_sm3nel32emjc9[10];
extern const uint8_t gm_sm6gj1hkmo5et[15];
extern const uint8_t gm_sm6s5u9s5i759[41];
extern const uint8_t gm_sm7cboss659pv[51];
extern const uint8_t gm_sm9cv8e73t8v3[14];
extern const uint8_t gm_smbbaf3n1gml2[8];
extern const uint8_t gm_smddipuf83u9j[3];
extern const uint8_t gm_smdiuajagl2mn[5];
extern const uint8_t gm_sme977fiiha9g[56];
extern const uint8_t gm_smga32pj96fal[11];
extern const uint8_t gm_smit2phoauonm[61];
extern const uint8_t gm_smjr08rujacsf[9];
extern const uint8_t gm_smr3sbhcj1goc[7];
extern const uint8_t gm_smsvf99bboril[12];
extern const uint8_t gm_smvhngj8mlfjc[13];
extern const uint8_t gm_sn06hdjl3nknj[87];
extern const uint8_t gm_sn0il8abv9oq3[8];
extern const uint8_t gm_sn2t7jm5mf8pm[5];
extern const uint8_t gm_sn57nkh1rblgd[62];
extern const uint8_t gm_snbhl8dsqq3ln[7];
extern const uint8_t gm_snccvgkvg2rbv[66];
extern const uint8_t gm_snf4ams51ofsv[81];
extern const uint8_t gm_snfkk2foekc1n[25];
extern const uint8_t gm_snghle2hv1o0c[41];
extern const uint8_t gm_snhr2rebnnkc2[22];
extern const uint8_t gm_snk5sdi9p8rv7[5];
extern const uint8_t gm_snl38trih7qf5[19];
extern const uint8_t gm_snqevmbeuqcmu[10];
extern const uint8_t gm_snqtn18pd5oda[41];
extern const uint8_t gm_snr2qdtkg0l3t[76];
extern const uint8_t gm_snset5tb694as[15];
extern const uint8_t gm_snuh38jefs63k[8];
extern const uint8_t gm_so0ilvhoi1hom[5];
extern const uint8_t gm_so1u9tqjfpsmc[9];
extern const uint8_t gm_so1us53sh0cre[44];
extern const uint8_t gm_so3hcc3nm7078[40];
extern const uint8_t gm_so720814e9k2t[9];
extern const uint8_t gm_so8aucu8dsqis[33];
extern const uint8_t gm_soe4gvhg9ei97[4];
extern const uint8_t gm_sog4rksrmsqd2[5];
extern const uint8_t gm_soifq4vd92jh0[5];
extern const uint8_t gm_sopln0aep28du[47];
extern const uint8_t gm_soqpbc95nno1l[7];
extern const uint8_t gm_sovel03lmfl8u[9];
extern const uint8_t gm_sp2m434o2c1ha[3];
extern const uint8_t gm_sp3shlt3qeg4k[41];
extern const uint8_t gm_sp5lvq92b4in0[11];
extern const uint8_t gm_sp69n8gsguhfh[77];
extern const uint8_t gm_sp77et3djoa12[12];
extern const uint8_t gm_spgfhj218uj9k[8];
extern const uint8_t gm_spic5gjomd7lu[62];
extern const uint8_t gm_spjohm68l6glq[22];
extern const uint8_t gm_spljblud8deko[66];
extern const uint8_t gm_spmhbt07q6beu[9];
extern const uint8_t gm_spn26bfbaqp3j[32];
extern const uint8_t gm_spn8bpm132g2m[19];
extern const uint8_t gm_spq5ja4c7ciga[11];
extern const uint8_t gm_sq2qrq8ir5ihu[8];
extern const uint8_t gm_sq68mfjf87n5h[16];
extern const uint8_t gm_sq87o4fbigdsl[17];
extern const uint8_t gm_sqa7l1hr9cbsj[41];
extern const uint8_t gm_sqbhgmmp8kn4m[3];
extern const uint8_t gm_sqbq94vn3heqs[49];
extern const uint8_t gm_sqd102436v4lo[9];
extern const uint8_t gm_sqk2n9kn6vnik[25];
extern const uint8_t gm_sqqkkj50acup3[36];
extern const uint8_t gm_squbucrq6qmns[5];
extern const uint8_t gm_sr31o7m0aohvd[9];
extern const uint8_t gm_sr5or4g6pm2gc[26];
extern const uint8_t gm_sr7m6arab7jm9[41];
extern const char gm_src276s8986o4r3[];
extern const char gm_src2j6ljeja7ilp[];
extern const char gm_src3onm9035msn9[];
extern const char gm_src4a8lk7fnr77f[];
extern const char gm_src4mnsbede6fos[];
extern const char gm_src8os28n25rsa4[];
extern const char gm_src8qgs75cskrcf[];
extern const char gm_src93pm6ie34ftr[];
extern const char gm_srcbff5i4gvposu[];
extern const char gm_srcbq9k9kvdokbf[];
extern const char gm_srcdjp1iiokf034[];
extern const char gm_srcevv4ajdr5s4e[];
extern const char gm_srci5ni72s6oj5f[];
extern const char gm_srcjaa834k3ab73[];
extern const char gm_srcjeqrqq5omjoo[];
extern const char gm_srck9pqpjjfjkmq[];
extern const char gm_srckg9scnip7e8g[];
extern const char gm_srcn623bb2dv5no[];
extern const char gm_srcne58m9cqp42d[];
extern const char gm_srcoolrp2h5hhk8[];
extern const char gm_srcq7ohh8h77bn7[];
extern const char gm_srcrsgf2gf2b1dq[];
extern const char gm_srct4i4cmesm57g[];
extern const char gm_srcv5fdu116dh4c[];
extern const uint8_t gm_srdb7m29fcjm3[3];
extern const uint8_t gm_srjd0sq8k3d7s[11];
extern const uint8_t gm_srnee0q49ttns[32];
extern const uint8_t gm_srq9bjb7d7qo7[15];
extern const uint8_t gm_srsiesc4al3v4[50];
extern const uint8_t gm_srt7t7qcsma0n[6];
extern const uint8_t gm_srvi01p0as8h9[15];
extern const uint8_t gm_srvn31r258t3t[16];
extern const uint8_t gm_ss1mi142k161m[41];
extern const uint8_t gm_ss226ibsv86g3[15];
extern const uint8_t gm_ss496b65r9omj[6];
extern const uint8_t gm_ss5c184rv03vi[18];
extern const uint8_t gm_ss6k80g3hocsv[6];
extern const uint8_t gm_ssac51i2hapg3[12];
extern const uint8_t gm_sscfkpmei7e4o[29];
extern const uint8_t gm_ssen3ksgpcf7g[13];
extern const uint8_t gm_sspbbn4skfqau[8];
extern const uint8_t gm_ssqciuageol45[23];
extern const uint8_t gm_ssql9980vrfs8[21];
extern const uint8_t gm_sss33h9aa3bdf[19];
extern const uint8_t gm_sssursgo48qb3[8];
extern const uint8_t gm_sstmd1b5da98l[25];
extern const uint8_t gm_st13dppqqeu9b[41];
extern const uint8_t gm_st1igimogo531[41];
extern const uint8_t gm_st249rq5g1u30[41];
extern const uint8_t gm_st5r5vj6g8baj[321];
extern const uint8_t gm_st87qih3spnnu[15];
extern const uint8_t gm_sta91ot91ebrb[11];
extern const uint8_t gm_stb6fe3d6kn09[79];
extern const uint8_t gm_stcddnajembde[9];
extern const uint8_t gm_stf7lvoc7oe54[16];
extern const uint8_t gm_stfnc00lo70l5[41];
extern const uint8_t gm_sth0p0t3b0nta[24];
extern const uint8_t gm_sti4tkgslrkgo[26];
extern const uint8_t gm_stkqau5o4elnq[15];
extern const uint8_t gm_stpboephdcpba[14];
extern const uint8_t gm_stpc2dcrflnth[63];
extern const uint8_t gm_su0hvb2ne02u0[17];
extern const uint8_t gm_su0q4q446mvpj[30];
extern const uint8_t gm_su4op6lvvdiua[16];
extern const uint8_t gm_su7f9sbon2vv7[6];
extern const uint8_t gm_sub9t1fj6sv37[6];
extern const uint8_t gm_sufaam6561ikm[17];
extern const uint8_t gm_suff456m9tthn[6];
extern const uint8_t gm_sug1rl2kq22u3[9];
extern const uint8_t gm_sugq7dl55ocon[7];
extern const uint8_t gm_suhbskqupk9jm[15];
extern const uint8_t gm_suidg89j7tg2d[5];
extern const uint8_t gm_sump4nk3r1lgv[8];
extern const uint8_t gm_sunu1gosuevl4[15];
extern const uint8_t gm_supf808555d3d[13];
extern const uint8_t gm_supvu1ruutbov[9];
extern const uint8_t gm_suq81kgcl2c91[19];
extern const uint8_t gm_sura2ofp16u7l[16];
extern const uint8_t gm_surog9s0b8r6p[20];
extern const uint8_t gm_surrjo1qo53mh[3];
extern const uint8_t gm_sutk9gh8f6gmt[53];
extern const uint8_t gm_sutmkmalrac6l[45];
extern const uint8_t gm_suv79odpsscph[8];
extern const uint8_t gm_sv6b1v96egn1q[7];
extern const uint8_t gm_sves6iucf8e25[12];
extern const uint8_t gm_svgje5djlutuu[25];
extern const uint8_t gm_sviu2hfr78esg[6];
extern const uint8_t gm_svjhll1rghogq[41];
extern const uint8_t gm_svkdb6kaja59r[7];
extern const uint8_t gm_svn5ml3afd65j[18];
extern const uint8_t gm_svr1kffiohqd7[11];
extern const uint8_t gm_svu0pqir618v4[39];
extern const gm_type gm_tyo86ks9qkvt;
inline __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_3en4m89(void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_for_body_3en4m89 F;
    GM_STACK_CHECK(&F, 124u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    memcpy(&F.l0, a0, sizeof F.l0);
    gm_t8scu1osgcra5 t3 = GM_LD(gm_t8scu1osgcra5, ((uint8_t *)&F.l0));
    F.l1 = t3;
    {
        gm_closure t5 = GM_LD(gm_closure, ((uint8_t *)&((struct gm_fr_declared_at_31b77n7 *)((&F.h)->up))->l4));
        gm_tl874t2atnlh3 t6 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l1) + 0));
        gm_closure t7 = t5;
        gm_node *ccx8 = t7.cx;
        void * t9 = t7.code;
        gm_b0_1 t10;
        if (GM_PTR_TAGGED(t9)) {
            ((void (*)(gm_cseahvg8v7p0vs))GM_PTR_UNTAG(t9))(GM_CONV(gm_cseahvg8v7p0vs, t6));
        } else {
            t10 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, gm_tl874t2atnlh3))t9)(t7.env, ccx8, NULL, t6);
        }
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    goto me1;
mn2:;
    gm_trap("no match arm matched the scrutinee", gm_srck9pqpjjfjkmq, 231, 5);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_e61ld42(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_for_body_e61ld42 F;
    gm_b16_8 gm_l0; /* <item> */
    gm_b16_8 gm_l1; /* w */
    GM_STACK_CHECK(&F, 76u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t3 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l0));
    gm_l1 = t3;
    {
        void * t5 = (void *)((uint8_t *)&((struct gm_fr_words_eeicdvm *)((&F.h)->up))->l1);
        gm_b16_8 t6 = gm_l1;
        gm_tftb1odc739t5 t7 = gm_f_push_3ef746b(t5, t6);
        gm_tftb1odc739t5 t8 = t7;
        if (((((uint8_t *)&t8))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t8) + 1, gm_srcn623bb2dv5no, 302, 37);
        gm_b0_1 t9 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
    }
    goto me1;
mn2:;
    gm_trap("no match arm matched the scrutinee", gm_srcn623bb2dv5no, 302, 5);
me1:;
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
const char gm_src276s8986o4r3[] = "compiler/check/func.gm";
const char gm_src2j6ljeja7ilp[] = "compiler/cgen/builtins.gm";
const char gm_src3onm9035msn9[] = "compiler/load/os_error.gm";
const char gm_src4a8lk7fnr77f[] = "compiler/check/effect_check.gm";
const char gm_src4mnsbede6fos[] = "compiler/testing/report.gm";
const char gm_src8os28n25rsa4[] = "compiler/ast/ast.gm";
const char gm_src8qgs75cskrcf[] = "compiler/cgen/patterns.gm";
const char gm_src93pm6ie34ftr[] = "compiler/sema/runtime_names.gm";
const char gm_srcbff5i4gvposu[] = "compiler/interp/access.gm";
const char gm_srcbq9k9kvdokbf[] = "compiler/pkg/git.gm";
const char gm_srcdjp1iiokf034[] = "compiler/check/spread.gm";
const char gm_srcevv4ajdr5s4e[] = "compiler/driver/interp.gm";
const char gm_srci5ni72s6oj5f[] = "lib/std/ascii/ascii.gm";
const char gm_srcjaa834k3ab73[] = "compiler/load/package.gm";
const char gm_srcjeqrqq5omjoo[] = "compiler/lsp/position.gm";
const char gm_srckg9scnip7e8g[] = "compiler/cgen/helpers.gm";
const char gm_srcne58m9cqp42d[] = "compiler/load/paths.gm";
const char gm_srcoolrp2h5hhk8[] = "lib/std/collections/heap/heap.gm";
const char gm_srcq7ohh8h77bn7[] = "compiler/interp/memory.gm";
const char gm_srcrsgf2gf2b1dq[] = "lib/std/fmt/fmt.gm";
const char gm_srct4i4cmesm57g[] = "lib/std/process/process.gm";
const char gm_srcv5fdu116dh4c[] = "compiler/lint/options.gm";
const uint8_t gm_s01tfkbjoijgc[23] = "(void *req, gm_cont k)";
const uint8_t gm_s06kk1tae680i[27] = "EOF while parsing a string";
const uint8_t gm_s08olofbep0dd[14] = "pat-fields : ";
const uint8_t gm_s09fhtm8l4rep[4] = " */";
const uint8_t gm_s0c4i5le3mc2l[23] = "</tr></thead>\012<tbody>\012";
const uint8_t gm_s0fdurfqutrtc[28] = "an enclosing `with` handles";
const uint8_t gm_s0ghv8aql4j5t[19] = "two handlers for `";
const uint8_t gm_s0h2dl2t3n8dh[25] = "recursion limit exceeded";
const uint8_t gm_s0kc1hjsjf2hu[11] = " = false; ";
const uint8_t gm_s0ku6rqnajsui[24] = "({})gm_rotl({}, {}, {})";
const uint8_t gm_s0m7e0jf69c0n[30] = "missing Content-Length header";
const uint8_t gm_s0of2eragnise[6] = ":type";
const uint8_t gm_s0pi0emdqd3vb[2] = "\000";
const uint8_t gm_s0sgp82v8se7l[11] = "gm_fiber *";
const uint8_t gm_s0tnnm2dhgmh2[25] = "+refs/tags/*:refs/tags/*";
const uint8_t gm_s11aakm4d1c2s[19] = " cannot be ordered";
const uint8_t gm_s11b339fv56g1[13] = "when-members";
const uint8_t gm_s13hosdv3k3g6[21] = "assertion failed: {}";
const uint8_t gm_s14bn21gpuj3j[3] = "`\077";
const uint8_t gm_s16i1ikaguc63[41] = "\000\000\000\000\000\000\000\000\031\000\000\000\000\000\000\000\247\000\000\000\014\000\000\000\000\000\000\000\000\000\000\000\005\000\000\000\000\000\000\000";
const uint8_t gm_s1740npcs2qqc[80] = "cannot propagate an error of type `{}` from a function whose error type is `{}`";
const uint8_t gm_s18773ijuej6p[26] = "a store outside an object";
const uint8_t gm_s197lbpp098pv[9] = "align_of";
const uint8_t gm_s19jg4ct3aq11[6] = "gamma";
const uint8_t gm_s1a10cqdk6nil[19] = "the array length `";
const uint8_t gm_s1faagcukkgns[10] = "((double)";
const uint8_t gm_s1i48s61oiget[34] = " {\012    gm_node *pn = gm_find(cx, ";
const uint8_t gm_s1jim1okq5o2b[192] = "this program switches fibers, which on WASI needs Binaryen's `wasm-opt` (https://github.com/WebAssembly/binaryen), but `{}` cannot be run: {}; install Binaryen or set WASM_OPT to its wasm-opt";
const uint8_t gm_s1karb2397v3p[20] = "` that applies to `";
const uint8_t gm_s1lvtjpshteb3[11] = "gm_with_go";
const uint8_t gm_s1oqiahdao8ce[10] = "rename_in";
const uint8_t gm_s1p9digtc20ub[7] = ") | ((";
const uint8_t gm_s1q9ms0kviddj[4] = "Fma";
const uint8_t gm_s1tdsk62jj75n[8] = "int{}_t";
const uint8_t gm_s22g482qk7a40[3] = "\\t";
const uint8_t gm_s22tv9c5r20tl[4] = " - ";
const uint8_t gm_s23ifilt9knnp[18] = "{}.{} :: {}({}){}";
const uint8_t gm_s2431f6f423u2[21] = "textDocument/didOpen";
const uint8_t gm_s24qpbg6rctcd[17] = "=http,https,file";
const uint8_t gm_s26219ruuua5s[19] = "__FLT16_MANT_DIG__";
const uint8_t gm_s2737i1iq57nf[5] = "type";
const uint8_t gm_s28fmt6o4roj7[10] = "character";
const uint8_t gm_s297r0ooe1bmq[29] = "match any payload with `(_)`";
const uint8_t gm_s2b7lcpvalpdu[14] = "     Locking ";
const uint8_t gm_s2jg45h85jjgj[37] = "and an operation kind after the name";
const uint8_t gm_s2jvv6mjt9vs3[13] = "fs_read_link";
const uint8_t gm_s2k1ajrnodb02[23] = "->kind == GM_NATIVE) {";
const uint8_t gm_s2ke5iso9cho7[18] = "duplicate field `";
const uint8_t gm_s2keeidsqic9l[41] = "unexpected end of input while parsing {}";
const uint8_t gm_s2kieedtscsk1[12] = " >> (int)((";
const uint8_t gm_s2lav3n6geg46[17] = "\000\000\000\000\000\000\000\000\016\000\000\000\000\000\000\000";
const uint8_t gm_s2lhokjucfplm[10] = "` (\302\2474.5)";
const uint8_t gm_s2mvbldeg43ph[9] = "archive ";
const uint8_t gm_s2n2ugogq2cq2[24] = "compiler/repl/report.gm";
const uint8_t gm_s2s2hv5oav6fb[29] = "(uint8_t)__builtin_isfinite(";
const uint8_t gm_s2t6ld4n1pdgu[5] = "http";
const uint8_t gm_s2v5amvfkfoll[3] = "\\0";
const uint8_t gm_s30117bvhp3fe[17] = "gm_altstack_free";
const uint8_t gm_s325452j0h962[17] = "\000\000\000\000\000\000\000\000C\000\000\000\000\000\000\000";
const uint8_t gm_s32vm0u5vgiu5[18] = "it is a surrogate";
const uint8_t gm_s338jbcmhh92n[20] = ")__builtin_nan(\"\"))";
const uint8_t gm_s33esn1pg9lma[12] = "this `else`";
const uint8_t gm_s34hhp1ucrags[12] = "*(volatile ";
const uint8_t gm_s37otbiaffdaa[25] = "{} \077 {} : ({} \077 {} : {})";
const uint8_t gm_s38n4b5q0dbil[45] = "` does not apply to a local binding (\302\24720.2)";
const uint8_t gm_s3a91i4ku76eo[81] = "invalid memory access: {} byte(s) at address {:#x} are not inside any allocation";
const uint8_t gm_s3agd8o01q2ss[62] = "a local variable or pattern binding whose value is never read";
const uint8_t gm_s3bfo4sq48nsb[5] = "pub ";
const uint8_t gm_s3bgi1rlb5ne1[8] = "name = ";
const uint8_t gm_s3h2hffrlol99[26] = "((uint8_t *){} + {} * {})";
const uint8_t gm_s3h586tvdle6o[31] = "    // private fields omitted\012";
const uint8_t gm_s3ia9jm7npe6t[10] = "inclusive";
const uint8_t gm_s3iij9a939gcv[15] = " *r = (struct ";
const uint8_t gm_s3nauht3nggr8[13] = "this pointer";
const uint8_t gm_s3p4vlg0qjpu0[46] = "internal error: a function value without code";
const uint8_t gm_s3pmm119d0kjq[16] = "\012end_of_record\012";
const uint8_t gm_s3sfk8t93kgsv[13] = "{} warning{}";
const uint8_t gm_s3suuflfjt6mu[11] = "select_nth";
const uint8_t gm_s3tpa8q2q7llg[17] = "an argument name";
const uint8_t gm_s3ukoldgd7ran[24] = "compiler/parse/items.gm";
const uint8_t gm_s43j3q24pq7de[26] = "`{}` takes {} argument(s)";
const uint8_t gm_s472t5rs14ilt[6] = "{}={}";
const uint8_t gm_s476hgsnn8633[47] = "attributes cannot be applied to a `when` block";
const uint8_t gm_s4a1r52bl6d5j[87] = "gamma.lock is missing, but --locked was given; run `gamma fetch` and commit gamma.lock";
const uint8_t gm_s4aqikl44lhv8[4] = "\")\012";
const uint8_t gm_s4cm3nbv0nhtk[21] = "#pragma push_macro(\"";
const uint8_t gm_s4hp8mhves75a[41] = "` performed here is not known statically";
const uint8_t gm_s4k2ktjht9l4a[13] = "],\"labels\":[";
const uint8_t gm_s4k7tmsh4glbh[77] = "dependency `{}` must be a table: `{{ path = \"\342\200\246\" }}` or `{{ git = \"\342\200\246\" }}`";
const uint8_t gm_s4nquq7iq8v5h[29] = "error: `:load` needs a file\012";
const uint8_t gm_s4pfhdi3f6f58[44] = "invalid multi-line {} string, expected `{}`";
const uint8_t gm_s4srjt2tusqhn[30] = "`_` cannot be used as a value";
const uint8_t gm_s4u8fr1tm8vl7[40] = "unknown --vcs `{}`: use `git` or `none`";
const uint8_t gm_s50l7uhgcnu1t[7] = "_ = {}";
const uint8_t gm_s50uau4el7or4[17] = ")` required by `";
const uint8_t gm_s514u4evks75f[21] = "gm_run_export_fibers";
const uint8_t gm_s51q2oivdrjj3[15] = "Unknown error ";
const uint8_t gm_s54fgk81cum2e[18] = "\012pub ptr_bits :: ";
const uint8_t gm_s56ntjkdhhepu[4] = "GiB";
const uint8_t gm_s58ndvsqjhesf[6] = "` (U+";
const uint8_t gm_s59dg443u5bte[41] = "\000\000\000\000\000\000\000\000\031\000\000\000\000\000\000\0002\001\000\000$\000\000\000\000\000\000\000\000\000\000\000\014\000\000\000\000\000\000\000";
const uint8_t gm_s5bfofp3bg4br[9] = " < 0 \077 -";
const uint8_t gm_s5bhk4dj6e492[73] = "\001\000\000\000\000\000\000\000\003\000\000\000\000\000\000\000\006\000\000\000\000\000\000\000\011\000\000\000\000\000\000\000\015\000\000\000\000\000\000\000\020\000\000\000\000\000\000\000\023\000\000\000\000\000\000\000\027\000\000\000\000\000\000\000\032\000\000\000\000\000\000\000";
const uint8_t gm_s5c6gmkd367jj[46] = "this call may move the items `{}` points into";
const uint8_t gm_s5dbqnns2epkg[6] = "index";
const uint8_t gm_s5el4up5fp2qc[45] = ") gm_trap(\"no handler installed for effect `";
const uint8_t gm_s5f630j5b0vm8[22] = "gm_thread_stack_limit";
const uint8_t gm_s5gff7p92h71s[16] = "gm_fiber_finish";
const uint8_t gm_s5gt30aqh30fl[19] = "    uint8_t s = v[";
const uint8_t gm_s5herb5iq95im[5] = "edit";
const uint8_t gm_s5j185b1e4leb[89] = "revision {} of `{}` ({}) is not in the cache, and --offline was given; run `gamma fetch`";
const uint8_t gm_s5k4jvr23rbdi[11] = "this store";
const uint8_t gm_s5l94dg13r1qf[14] = "gm_pipe_ready";
const uint8_t gm_s5l9jdmrtaean[11] = "gm_dt{}_{}";
const uint8_t gm_s5r4lidhg0cfv[64] = "a literal is too large for `{}` (in an instantiation used here)";
const uint8_t gm_s5s0ahr8bl55p[12] = "} else if (";
const uint8_t gm_s60adpphft7s5[5] = "evok";
const uint8_t gm_s60bni5maq4df[12] = "` use `.to(";
const uint8_t gm_s61upj6a9rej6[22] = "--is-inside-work-tree";
const uint8_t gm_s622mvvjraklk[43] = "`: No such file or directory (os error 2)\012";
const uint8_t gm_s62k5o20indcj[12] = "    if (s) ";
const uint8_t gm_s63vjuo83pn20[15] = "`{}=` needs {}";
const uint8_t gm_s64s5a4ht3148[69] = "`; they have `dot`, `cross`, `norm`, `min`, `max` and `abs` (\302\24721.3)";
const uint8_t gm_s65fsr1v7i389[135] = "this compile-time evaluation calls `{}`, which is nested in the function being checked; it cannot run before that function is complete";
const uint8_t gm_s66k76ki3e7an[22] = "` (it has no default)";
const uint8_t gm_s66uvortgrtsg[4] = "ctl";
const uint8_t gm_s670np414hdkp[8] = "#soa []";
const uint8_t gm_s68avpqal8nbc[12] = "unresolved\012";
const uint8_t gm_s68rktnu8slfa[25] = "    h = gm_mix_bytes(h, ";
const uint8_t gm_s698sbcv3nmjk[69] = "    if (gm_unw.kind == GM_EXIT) { gm_flush(); return gm_unw.code; }\012";
const uint8_t gm_s6bmkes68nfio[5] = "Safe";
const uint8_t gm_s6dk6h0otj4rd[13] = "gm_cur_fiber";
const uint8_t gm_s6gnskloov1dv[15] = "the function `";
const uint8_t gm_s6ifo0vt5mlt9[6] = "0.2.0";
const uint8_t gm_s6l0iqp1ptptl[17] = "struct gm_dv{}{}";
const uint8_t gm_s6p2jltb0rdds[6] = "gm_n_";
const uint8_t gm_s6rn45hsruulp[9] = "\004\000\000\000\000\000\000\000";
const uint8_t gm_s6s96daqmhcs9[2] = "*";
const uint8_t gm_s6vfshjjhk2g7[19] = "remove the capture";
const uint8_t gm_s6vtoqhgjb0nd[6] = "dylib";
const uint8_t gm_s70nlbtnmugoc[10] = "the field";
const uint8_t gm_s743265bqlq4a[14] = "indexing `{}`";
const uint8_t gm_s797e8kr2a9qh[14] = "make_analysis";
const uint8_t gm_s79dkmc2rhan7[12] = "parse_match";
const uint8_t gm_s7ch91qn11t34[25] = "`--sha256` needs `--url`";
const uint8_t gm_s7f3pdsji9g3a[9] = "noinline";
const uint8_t gm_s7hndfpk1u0p0[7] = "index.";
const uint8_t gm_s7ilf06vf0fi1[22] = "integer overflow in `";
const uint8_t gm_s7mq585fepqnh[6] = "start";
const uint8_t gm_s823123ng02vo[44] = "`#soa` stores the fields of a struct, and `";
const uint8_t gm_s86bd4orcs15b[7] = "{}-gcc";
const uint8_t gm_s87c4jti7ujk5[31] = "({})((({})({} ^ {})) - ({}){})";
const uint8_t gm_s87g1ufoaqi74[8] = "Sub({})";
const uint8_t gm_s8av3ru5o85v4[6] = "break";
const uint8_t gm_s8becutukvgrh[17] = "memmove(gm_out, ";
const uint8_t gm_s8dbdqj1sqibj[3] = "\012\012";
const uint8_t gm_s8e72gdumihb1[7] = "binary";
const uint8_t gm_s8fhmkhb94g4l[8] = "gm_ls{}";
const uint8_t gm_s8hpuukc6o9qu[9] = "std/time";
const uint8_t gm_s8jc0a1p1kvtf[73] = "typedef union {{ _Alignas({}) uint8_t b[{}]; void *gm_lanes[{}]; }} {};\012";
const uint8_t gm_s8kki44ucv73v[11] = "(uint8_t)!";
const uint8_t gm_s8lpasdulkmbv[28] = "insert: index out of bounds";
const uint8_t gm_s8p6h6db1ftub[32] = "operation parameters need types";
const uint8_t gm_s8uchl9nh4d11[33] = "the parameter `{}` is never used";
const uint8_t gm_s94n39ltlvmhe[36] = "`bits` of this type at compile time";
const uint8_t gm_s95g98ft0cnt1[155] = "`{}` is not available on vectors of `{}`: targets have no instruction that divides integer lanes, so the cost would be hidden; divide the lanes one by one";
const uint8_t gm_s96uiluq5ufvd[66] = "the package in {} has no dependency `{}`; its dependencies are {}";
const uint8_t gm_s9akpbuv78p3m[109] = "a binding with a pattern that can fail needs `else` to leave the block: `.V(x) := e else return \342\200\246` (\302\2474.5)";
const uint8_t gm_s9b7nekk74u38[3] = " \"";
const uint8_t gm_s9cb55ipdnrli[15] = "volatile_store";
const uint8_t gm_s9g30d9lksra5[3] = ".p";
const uint8_t gm_s9h40dg66q82b[58] = "internal error: control flow escaped a deferred statement";
const uint8_t gm_s9hst152mpfjl[42] = "` is declared twice in this foreign block";
const uint8_t gm_s9j65lfldjn8j[20] = "` depends on itself";
const uint8_t gm_s9jrvf2ogt807[17] = "; use {} (\302\2476.6)";
const uint8_t gm_s9kj8vgi6430a[6] = "]; } ";
const uint8_t gm_s9krd4pni7gv1[6] = " (*)(";
const uint8_t gm_s9lpec5ab0tsm[111] = "static void gm_start(void) {\012    static int started;\012    if (started) return;\012    started = 1;\012    gm_debug = ";
const uint8_t gm_s9ocqto6ckbos[58] = "a named effect set cannot contain an effect-set parameter";
const uint8_t gm_s9omga3p3drmu[51] = "a boolean pattern needs a `bool`, but this is `{}`";
const uint8_t gm_s9qem7js6pnep[15] = "gm_quote_bytes";
const uint8_t gm_s9r72jgd5hb05[5] = "'{}'";
const uint8_t gm_s9rkpo2ljmui9[50] = "array lengths that are typed constant expressions";
const uint8_t gm_s9sif80bjh9f7[12] = "isPreferred";
const uint8_t gm_sa2pnk1jujp5p[23] = "unknown container type";
const uint8_t gm_sa39uag0oed9a[16] = "` of the module";
const uint8_t gm_sa56bd0p8nuib[143] = "fibers (`with \342\200\246 on`) need a context switch, which the runtime has only for x86-64, AArch64 and s390x; this freestanding target ({}) has none";
const uint8_t gm_sa68th8jv179j[9] = "gm_with ";
const uint8_t gm_sa82k8ai03q0v[41] = "\000\000\000\000\000\000\000\000\031\000\000\000\000\000\000\000U\001\000\000\022\000\000\000\000\000\000\000\000\000\000\000\005\000\000\000\000\000\000\000";
const uint8_t gm_sa8dse4jk5ibv[10] = "ClockWall";
const uint8_t gm_sa9kmm3ih11r5[28] = "file or directory not found";
const uint8_t gm_saab9e9ochkfq[56] = "type parameters must be named and typed, like `T: type`";
const uint8_t gm_saicih5tjo4fl[43] = "`remove` needs exactly one dependency name";
const uint8_t gm_saimepa8bfkkm[3] = "|=";
const uint8_t gm_saja5hcbljm83[5] = "gw{}";
const uint8_t gm_sajefbn69vf76[12] = " lanes of `";
const uint8_t gm_sao4mumvadoct[4] = " = ";
const uint8_t gm_saojtqmfeak72[8] = "a block";
const uint8_t gm_sarb7aqhpoiqu[13] = "gm_make_pipe";
const uint8_t gm_savb5oasie77f[37] = "expected one payload value of type `";
const uint8_t gm_sb03flnhs22vb[3] = "Ok";
const uint8_t gm_sb1t57j9241lh[74] = "` is already declared; a type may match only one declaration of an effect";
const uint8_t gm_sb23l4chfnd3f[10] = "    goto ";
const uint8_t gm_sb26s4qotngkv[27] = "the `path` of dependency `";
const uint8_t gm_sb5avtmeu66m7[144] = "a block body's last expression is not its value: write `return \342\200\246`, or `=> \342\200\246` for a body that is one expression (this function returns `{}`)";
const uint8_t gm_sb6bik10m4ppo[74] = "this address of `{}` is passed to a call that may keep it after `{}` ends";
const uint8_t gm_sb6jpc12sa7uj[4] = "j16";
const uint8_t gm_sbc0kjtg5aiab[5] = "WASI";
const uint8_t gm_sbdfikdtgvq9q[28] = ": the heap block there has ";
const uint8_t gm_sbdp6ovojv1tg[10] = "__sparc__";
const uint8_t gm_sbe7dr7t400bh[16] = "first used here";
const uint8_t gm_sbiak9qhf4tcb[12] = " != 0) == (";
const uint8_t gm_sbiergg61olmu[41] = "\000\000\000\000\000\000\000\000\025\000\000\000\000\000\000\000\251\000\000\000&\000\000\000\000\000\000\000\000\000\000\000\004\000\000\000\000\000\000\000";
const uint8_t gm_sbj0273u8beka[7] = "field ";
const uint8_t gm_sbj0co7eet3k9[8] = "gm_dirs";
const uint8_t gm_sbm0k1981kgpb[40] = "    uintptr_t n = GM_LD(uintptr_t, a + ";
const uint8_t gm_sbo7sr73hvg7d[10] = "version `";
const uint8_t gm_sbqk0qmd2ampu[37] = "gamma: {}: no such file or directory";
const uint8_t gm_sc6gojr5sjib4[21] = ".byte 0\012.popsection\012";
const uint8_t gm_scffrkp2h220o[5] = "join";
const uint8_t gm_sch3p0ffmutvn[16] = "a success value";
const uint8_t gm_sciq0a78hirud[25] = "        return h;\012    }\012";
const uint8_t gm_scj0sm2kpivk4[9] = "_Float16";
const uint8_t gm_scmaavulfehmr[5] = "text";
const uint8_t gm_sco971hnpq7nk[49] = "internal error: upvar without an enclosing frame";
const uint8_t gm_scomp2tns6p6f[16] = "__builtin_ceil(";
const uint8_t gm_scs1evojsik8q[4] = " \077 ";
const uint8_t gm_scttrk7b2nnqb[24] = "gm_asyncify_stop_unwind";
const uint8_t gm_scv3jvaidk3vn[21] = "</code></a></td><td>";
const uint8_t gm_scv8i3jrgh2rk[65] = "(\342\200\246)`) or pass it to a function that runs it, such as `collect(";
const uint8_t gm_scvj6up3gbolp[25] = "compiler/sema/foreign.gm";
const uint8_t gm_sd0pnvfh21o90[11] = "utf8_valid";
const uint8_t gm_sd1jj257q6flv[17] = "present_bindings";
const uint8_t gm_sd2g8d0vbj98u[7] = "float ";
const uint8_t gm_sd6a57g2jmtks[11] = "parameters";
const uint8_t gm_sd7spmqdlcohl[25] = "({} >> (int)(({}) & {}))";
const uint8_t gm_sd9i4iia5r4dp[72] = ", !(gm_unw.kind == GM_BREAK && gm_unw.frame == &F.h && gm_unw.label == ";
const uint8_t gm_sd9ri1vmgds82[9] = "memset(&";
const uint8_t gm_sdealia24icjj[40] = "` cannot appear in a deferred statement";
const uint8_t gm_sdjh2jdveb2fb[41] = "\000\000\000\000\000\000\000\000\031\000\000\000\000\000\000\000\250\000\000\000\011\000\000\000\000\000\000\000\000\000\000\000\005\000\000\000\000\000\000\000";
const uint8_t gm_sdmle2qsm85vh[11] = "version = ";
const uint8_t gm_sdnc8nilmmql1[23] = "` does not apply to it";
const uint8_t gm_sdpi8e9pb70dm[47] = "an import that nothing in its module refers to";
const uint8_t gm_sds1asc07m30k[16] = "the package in ";
const uint8_t gm_sdt2alvv2h6jh[48] = "main :: fn() needs Out {\012    print(\"hello from ";
const uint8_t gm_sdvuu27hpddqp[11] = "they take ";
const uint8_t gm_se0ivu8cf46o8[12] = "TupleRename";
const uint8_t gm_se2fbs5jfigcc[11] = "union body";
const uint8_t gm_se4629k8oa1uo[2] = "x";
const uint8_t gm_se48mcej0nimj[16] = "        Note: `";
const uint8_t gm_se4kub9a3omhd[5] = "wasi";
const uint8_t gm_se5118fq98e06[88] = "an array length is a constant `usize` or an enum type (`[E]T`), and `{}` is not an enum";
const uint8_t gm_se64h924hq7dp[24] = "Interrupted system call";
const uint8_t gm_seaesh4v4toj4[76] = "cannot rename `{}` to `{}`: `{}` is a local name where `{}` is used ({}:{})";
const uint8_t gm_seb76g152eseg[4] = "neg";
const uint8_t gm_sej7fch3vkla7[24] = "] != 0)) return false;\012";
const uint8_t gm_sejc19hk8m1pk[20] = "gm_overflow_handler";
const uint8_t gm_sel4m8qdnq9n9[5] = " N\000\000";
const uint8_t gm_semhbv8seph85[21] = "(unsigned __int128)(";
const uint8_t gm_seo41mk3n3ro5[13] = "` (\302\24712.7.1)";
const uint8_t gm_seohfka1b0c2g[47] = "`main` must return `()`, `u8` or `E!()`, not `";
const uint8_t gm_seosa3iobfc09[35] = "int main(int argc, char **argv) {\012";
const uint8_t gm_seqfup76f04qu[9] = "--emit-c";
const uint8_t gm_seqpkkqc4c757[14] = "gamma-bench: ";
const uint8_t gm_seqrg7cf0sejb[33] = ") gm_trap_pow((const uint8_t *)&";
const uint8_t gm_seu8takp0sa1c[9] = "gm_mkdir";
const uint8_t gm_sf00qdbn1q8j8[9] = ".ascii \"";
const uint8_t gm_sf8bp31mrq32l[19] = "__asm__ volatile (";
const uint8_t gm_sf9a2kg3eo1gs[2] = "!";
const uint8_t gm_sfbtgpom3l6k2[5] = "unit";
const uint8_t gm_sfd839bbgsa7s[63] = "decimal literals may not have a leading zero; use 0o for octal";
const uint8_t gm_sfdbfrv46lihd[2] = "_";
const uint8_t gm_sffcmqsfdatal[47] = "`.cross` needs three elements, and `{}` has {}";
const uint8_t gm_sfht1gt5dn60n[11] = "stack_size";
const uint8_t gm_sfms0h2mph067[11] = "(({} *){})";
const uint8_t gm_sft71cpvvrvmk[12] = "gm_fs_write";
const uint8_t gm_sfvnlkgl40moh[72] = "this call yields more than one type; `for` needs exactly one `Yield(T)`";
const uint8_t gm_sg26if17vtome[23] = " needs the fields of `";
const uint8_t gm_sg3453glm1u32[14] = "vm_value_site";
const uint8_t gm_sg3fjsrtce2g0[41] = "\000\000\000\000\000\000\000\000\030\000\000\000\000\000\000\000\315\000\000\000\015\000\000\000\000\000\000\000\000\000\000\000\016\000\000\000\000\000\000\000";
const uint8_t gm_sg5gdnrith2i6[3] = ", ";
const uint8_t gm_sg6dkeojalp6f[14] = "declared here";
const uint8_t gm_sg87p4r5apham[36] = "gamma: cannot read the runtime in `";
const uint8_t gm_sg90vqvamkjbo[11] = "aarch64_be";
const uint8_t gm_sg96ffu32fbap[42] = "`else { if \342\200\246 }`, which is `else if \342\200\246`";
const uint8_t gm_sgaj4glha8q4e[3] = "<h";
const uint8_t gm_sgdk0atob5av0[6] = "->up)";
const uint8_t gm_sgetvvdiqkhsm[18] = "bad checkout path";
const uint8_t gm_sgiqpj781s1a9[36] = "a compile-time pointer of this type";
const uint8_t gm_sgjc0ebf56hf4[8] = "gm_fmax";
const uint8_t gm_sgjt1pifmqnsi[35] = "stream did not contain valid UTF-8";
const uint8_t gm_sgktm44r9e4ua[6] = "--all";
const uint8_t gm_sgmcjv273u32m[11] = "when-items";
const uint8_t gm_sgo9an2jqt4mr[13] = "expect_false";
const uint8_t gm_sgosg5fctcl72[17] = "(gm_fiber *fb);\012";
const uint8_t gm_sgrd6s4kl4dhp[29] = "discard the value with `_ =`";
const uint8_t gm_sgtu622jht4g2[51] = "`@hash` is defined only for types with `is`, and `";
const uint8_t gm_sgva3apqq9jo2[68] = "`^const fn` is not a type; function pointers are written `^fn(\342\200\246)`";
const uint8_t gm_sh133eanuhqjo[10] = "--jobs -j";
const uint8_t gm_sh7j5igqqg1id[10] = "min_stack";
const uint8_t gm_sh8atl0agp75n[13] = "gm_table_add";
const uint8_t gm_sh8l62ma11hq5[70] = "-gcc` and `clang` are not on PATH; install a cross compiler or set CC";
const uint8_t gm_sh8osg42sl1gi[145] = "\000\000\000\000\000\000\000\000\004\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\003\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\003\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\006\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\007\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\003\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\006\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\003\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\006\000\000\000\000\000\000\000";
const uint8_t gm_sh90fjlgqrlfc[7] = "test \"";
const uint8_t gm_shcail1g86pp0[26] = "in the instance used here";
const uint8_t gm_shfckrpors679[49] = ") must be the only comparator in the version req";
const uint8_t gm_shfvspt8ngibo[19] = "the iterated value";
const uint8_t gm_shhr1hlq1ethc[4] = "err";
const uint8_t gm_shi1s7rq8m35t[40] = "expected one payload value of type `{}`";
const uint8_t gm_shsfl3e9phpsi[23] = "write `try (x else d)`";
const uint8_t gm_sht12dmgii44r[41] = "\000\000\000\000\000\000\000\000\024\000\000\000\000\000\000\000M\000\000\000\025\000\000\000\000\000\000\000\000\000\000\000\005\000\000\000\000\000\000\000";
const uint8_t gm_si1b582k23708[50] = "variants `{}` and `{}` have the same tag value {}";
const uint8_t gm_si2ih874n1gou[10] = " (\302\24716.5)";
const uint8_t gm_si42rotkq2ll8[14] = "gm_rt_release";
const uint8_t gm_si4cfd3bll781[9] = "\376\377\377\377\377\377\377\377";
const uint8_t gm_si9mfda4b719e[63] = "); gm_buf_push(&mb, mp, ml); } gm_trap_bytes(mb.data, mb.len, ";
const uint8_t gm_sibidb2jab0ss[14] = "__loongarch64";
const uint8_t gm_sijukd7vsocnt[68] = "`print` takes a template string and the values for its placeholders";
const uint8_t gm_sinve1cmean4f[24] = "{} at line {} column {}";
const uint8_t gm_sio9tu2ig98ap[21] = "    gm_with_clause(&";
const uint8_t gm_siph7o6oug1g2[14] = "fs_remove_dir";
const uint8_t gm_sir0litoa3fk0[18] = "-lsynchronization";
const uint8_t gm_sisa53plcfedi[14] = "StaticAssert(";
const uint8_t gm_siscc5ella8ma[4] = ") {";
const uint8_t gm_sisjhd4gebqhp[25] = "`--format` needs a value";
const uint8_t gm_sivm2ee5886a0[8] = ".md) | ";
const uint8_t gm_sj2gsb941ds9q[21] = "-fno-strict-aliasing";
const uint8_t gm_sj5fh226119gq[14] = "`{}` needs {}";
const uint8_t gm_sj6enltl2qi3c[21] = "` is not written in ";
const uint8_t gm_sj9krj49k3580[10] = "package `";
const uint8_t gm_sjbb259gf4uuc[8] = "\"code\":";
const uint8_t gm_sjbl17pjsjm48[5] = "Rotr";
const uint8_t gm_sjegrp8u9r3n3[8] = "size-of";
const uint8_t gm_sjg57h040l08d[56] = "compile-time arguments of this type in nested functions";
const uint8_t gm_sjhccr3h0np2l[15] = "signature_help";
const uint8_t gm_sjhi1ic83fapl[1443] = "package commands:\012  new     create a package in a new directory: gamma new <dir> [--name=<name>] [--vcs=none]\012  init    create a package in an existing directory: gamma init [dir] [--name=<name>] [--vcs=none]\012  fetch   resolve and download the git and archive dependencies, and write gamma.lock:\012          gamma fetch [--dir <dir>] [--offline] [--locked]\012  update  move git dependencies to the newest revisions their requirements allow:\012          gamma update [name...] [--dir <dir>]\012  add     add a dependency to gamma.toml and fetch it:\012          gamma add <name> --path <dir> [--dir <dir>]\012          gamma add <name> --git <url> [--tag <tag> | --branch <branch> | --rev <rev> | --version <req>]\012                    [--subdir <path>] [--dir <dir>]\012          gamma add <name> --url <url.tar.gz> [--sha256 <hex>] [--subdir <path>] [--dir <dir>]\012  remove  remove a dependency: gamma remove <name> [--dir <dir>]\012  cache   the cache of fetched packages and native builds:\012          gamma cache dir                     print the cache directory\012          gamma cache clean [--older-than=<days>] [--all] [--dry-run]\012                    remove package checkouts and builds unused for <days> (default 30),\012                    and mirrors no checkout needs; --all removes everything\012\012build settings (`[build]`, `[profile.<name>]`) are described in docs/guide/15-packages.md;\012`--profile=<name>` selects a profile in check, run, build, test and bench.\012";
const uint8_t gm_sjhsg0a8cb7s7[42] = "how to print diagnostics (default: human)";
const uint8_t gm_sjibpf3glbc54[9] = "gm_fprop";
const uint8_t gm_sjifgfpiu6k52[6] = "` is ";
const uint8_t gm_sjjcm6bafumir[47] = "`{}` = {} is a `usize`, but `{}` has type `{}`";
const uint8_t gm_sjjehkhgpkn3v[10] = " needs {}";
const uint8_t gm_sjltai3b82h79[4] = " % ";
const uint8_t gm_sjm9r4dj90vdb[13] = "    union { ";
const uint8_t gm_sjmqausipumva[26] = "(uint8_t)__builtin_isinf(";
const uint8_t gm_sjnvtll1pqkj7[11] = "this bound";
const uint8_t gm_sjoj5mss14s83[6] = ", 0);";
const uint8_t gm_sjp9oaluqrtr3[48] = "`--target` is not supported by `lint` and `fix`";
const uint8_t gm_sjpvc4ek9dber[39] = "a deferred statement may not perform `";
const uint8_t gm_sjr44k79a48mq[4] = "r{}";
const uint8_t gm_sk024chguo1g1[9] = "add_coro";
const uint8_t gm_sk3630dksnfoh[24] = "` needs the layout of `";
const uint8_t gm_sk50arqpf74k6[6] = "amd64";
const uint8_t gm_sk7tgk6pi62vu[14] = "this variable";
const uint8_t gm_skarrfopl04v8[23] = "; const uint8_t *mp = ";
const uint8_t gm_skbblqa84g6pp[57] = "    for (uintptr_t i = 0; i < na && i < nb; i++) { if (!";
const uint8_t gm_skbosgm06mmhc[57] = "`rev` of dependency `{}` must be a hexadecimal commit id";
const uint8_t gm_skhd03cqcl7er[28] = "{} more calls are not shown";
const uint8_t gm_ski6lj2hn0gg4[35] = "a second-class function cannot be ";
const uint8_t gm_skiqr6ckq7kq3[12] = "ctl_perform";
const uint8_t gm_skkuka4uqa99p[12] = "gm_veh_once";
const uint8_t gm_skm4ndn7op5l5[14] = "proc_try_wait";
const uint8_t gm_skmh83q8fkhmc[29] = "not supported on this system";
const uint8_t gm_skocj074p3r97[9] = "builtin ";
const uint8_t gm_skp02jnee3dfj[35] = "    if (n != GM_LD(uintptr_t, b + ";
const uint8_t gm_skp6qpvju2t5u[19] = "lldb_formatters.py";
const uint8_t gm_skriisqpofdin[3] = "+%";
const uint8_t gm_sl52kab9m437h[74] = "a fiber stack needs at least {} bytes (`@min_stack`), but this one has {}";
const uint8_t gm_sl5g3dl3mq5ih[48] = "calls of core library functions at compile time";
const uint8_t gm_sl6pi8la5phb6[20] = "(uint64_t)(int64_t)";
const uint8_t gm_sl85qrve5cdud[31] = "`main` must take no parameters";
const uint8_t gm_sl9jmb4urj4ud[13] = "with {} : {}";
const uint8_t gm_slbdftqell5t4[10] = "gm_fmt_ch";
const uint8_t gm_slbgk65sat4bm[8] = "type_of";
const uint8_t gm_slc0htp15p4h9[41] = "\000\000\000\000\000\000\000\000\031\000\000\000\000\000\000\000r\000\000\000G\000\000\000\000\000\000\000\000\000\000\000\010\000\000\000\000\000\000\000";
const uint8_t gm_sld7tlsq8opg9[15] = "</code></pre>\012";
const uint8_t gm_sldd44oagbb0a[10] = "\"column\":";
const uint8_t gm_slesj9fnpbent[24] = "(({})__builtin_nan(\"\"))";
const uint8_t gm_slk3h69j7tjn1[41] = "\000\000\000\000\000\000\000\000\031\000\000\000\000\000\000\000r\000\000\0002\000\000\000\000\000\000\000\000\000\000\000\010\000\000\000\000\000\000\000";
const uint8_t gm_slkhlou8kve8h[32] = "`@{}({})` needs `where Int({})`";
const uint8_t gm_slkoo2ifkem47[62] = "{}:{}: dependency `{}` is a git dependency; run `gamma fetch`";
const uint8_t gm_slmlmpuma600i[85] = "`--backend=interp` is supported by `run` only: the other commands use the C back end";
const uint8_t gm_slp04vsc9cddi[49] = "`try` cannot be an operand here; parenthesize it";
const uint8_t gm_slp60pbhoot2j[15] = " = 1; gm_ginit";
const uint8_t gm_slrd70lo3b6iq[15] = " (finished in ";
const uint8_t gm_sltao39ib9870[6] = "Some(";
const uint8_t gm_sltvces0c7l80[14] = "AtomicRmw({})";
const uint8_t gm_sluaq8lcmp31d[68] = "package `{}`: `archive` must be `sha256:` and 64 hexadecimal digits";
const uint8_t gm_sluvntf7gk99f[3] = "-I";
const uint8_t gm_sm08k35r8tv0e[26] = "the `url` of dependency `";
const uint8_t gm_sm0gs6tv176le[8] = "NetPeer";
const uint8_t gm_sm1gsdt21ielk[41] = "\000\000\000\000\000\000\000\000\031\000\000\000\000\000\000\000L\001\000\000,\000\000\000\000\000\000\000\000\000\000\000\005\000\000\000\000\000\000\000";
const uint8_t gm_sm2k5tp7dtg07[4] = "len";
const uint8_t gm_sm2qh69a2gq0m[51] = "{} has no default branch to use (required by `{}`)";
const uint8_t gm_sm3nel32emjc9[10] = "uint8_t *";
const uint8_t gm_sm6gj1hkmo5et[15] = "gm_utf8_seq_ok";
const uint8_t gm_sm6s5u9s5i759[41] = "\000\000\000\000\000\000\000\000\025\000\000\000\000\000\000\000\364\000\000\000$\000\000\000\000\000\000\000\000\000\000\000\004\000\000\000\000\000\000\000";
const uint8_t gm_sm7cboss659pv[51] = "a thread started from an invalid function pointer ";
const uint8_t gm_sm9cv8e73t8v3[14] = "<blockquote>\012";
const uint8_t gm_smbbaf3n1gml2[8] = "return ";
const uint8_t gm_smddipuf83u9j[3] = "\"\012";
const uint8_t gm_smdiuajagl2mn[5] = "();\012";
const uint8_t gm_sme977fiiha9g[56] = "` writes through the pointer, so it needs a `^T`, not `";
const uint8_t gm_smga32pj96fal[11] = "initialize";
const uint8_t gm_smit2phoauonm[61] = "invalid `\\x` escape: expected exactly two hexadecimal digits";
const uint8_t gm_smjr08rujacsf[9] = "<format>";
const uint8_t gm_smr3sbhcj1goc[7] = "source";
const uint8_t gm_smsvf99bboril[12] = "gm_net_peer";
const uint8_t gm_smvhngj8mlfjc[13] = "fs_canonical";
const uint8_t gm_sn06hdjl3nknj[87] = "`{}` cannot be spread: its elements are named by its variants, not positions (\302\2475.6.1)";
const uint8_t gm_sn0il8abv9oq3[8] = "Pow({})";
const uint8_t gm_sn2t7jm5mf8pm[5] = "tag=";
const uint8_t gm_sn57nkh1rblgd[62] = "the level of `{}` in [lint] must be \"allow\", \"warn\" or \"deny\"";
const uint8_t gm_snbhl8dsqq3ln[7] = "BitAnd";
const uint8_t gm_snccvgkvg2rbv[66] = " a temporary array, which dies when the function returns (\302\24715.1)";
const uint8_t gm_snf4ams51ofsv[81] = "`{}` has no method `.{}`; its columns, `a.f`, are arrays and slices with methods";
const uint8_t gm_snfkk2foekc1n[25] = "max-wasm-stack=268435456";
const uint8_t gm_snghle2hv1o0c[41] = "\000\000\000\000\000\000\000\000\020\000\000\000\000\000\000\000|\000\000\000\027\000\000\000\000\000\000\000\000\000\000\000\010\000\000\000\000\000\000\000";
const uint8_t gm_snhr2rebnnkc2[22] = "--no-same-permissions";
const uint8_t gm_snk5sdi9p8rv7[5] = "<arg";
const uint8_t gm_snl38trih7qf5[19] = "`--{}` given twice";
const uint8_t gm_snqevmbeuqcmu[10] = "^{commit}";
const uint8_t gm_snqtn18pd5oda[41] = "\000\000\000\000\000\000\000\000\027\000\000\000\000\000\000\000\341\000\000\000\014\000\000\000\000\000\000\000\000\000\000\000\013\000\000\000\000\000\000\000";
const uint8_t gm_snr2qdtkg0l3t[76] = "cannot run `git` (is it installed\077): No such file or directory (os error 2)";
const uint8_t gm_snset5tb694as[15] = " - (uintptr_t)";
const uint8_t gm_snuh38jefs63k[8] = "gm_path";
const uint8_t gm_so0ilvhoi1hom[5] = "pats";
const uint8_t gm_so1u9tqjfpsmc[9] = "ProcWait";
const uint8_t gm_so1us53sh0cre[44] = "too many arguments: the function takes {}{}";
const uint8_t gm_so3hcc3nm7078[40] = ") gm_trap(\"integer overflow in `abs`\", ";
const uint8_t gm_so720814e9k2t[9] = "\364\377\377\377\377\377\377\377";
const uint8_t gm_so8aucu8dsqis[33] = "missing argument for parameter `";
const uint8_t gm_soe4gvhg9ei97[4] = "Not";
const uint8_t gm_sog4rksrmsqd2[5] = "main";
const uint8_t gm_soifq4vd92jh0[5] = "imag";
const uint8_t gm_sopln0aep28du[47] = "gm_mix128(h, (unsigned __int128)GM_LD({}, {}))";
const uint8_t gm_soqpbc95nno1l[7] = "url+{}";
const uint8_t gm_sovel03lmfl8u[9] = "pat-null";
const uint8_t gm_sp2m434o2c1ha[3] = "eq";
const uint8_t gm_sp3shlt3qeg4k[41] = "\000\000\000\000\000\000\000\000\020\000\000\000\000\000\000\000/\000\000\000\015\000\000\000\000\000\000\000\000\000\000\000\010\000\000\000\000\000\000\000";
const uint8_t gm_sp5lvq92b4in0[11] = "NULL, 0, 0";
const uint8_t gm_sp69n8gsguhfh[77] = "an arm's body is an expression; put an assignment in a block: `=> { x = v }`";
const uint8_t gm_sp77et3djoa12[12] = " ... FAILED";
const uint8_t gm_spgfhj218uj9k[8] = ") & (((";
const uint8_t gm_spic5gjomd7lu[62] = "internal error: an expression left over from generic checking";
const uint8_t gm_spjohm68l6glq[22] = "` needs a payload: `.";
const uint8_t gm_spljblud8deko[66] = "`try` needs the function to return an optional or a result, not `";
const uint8_t gm_spmhbt07q6beu[9] = "__asm__(";
const uint8_t gm_spn26bfbaqp3j[32] = "operation `{}` is handled twice";
const uint8_t gm_spn8bpm132g2m[19] = "unused-declaration";
const uint8_t gm_spq5ja4c7ciga[11] = "write `::`";
const uint8_t gm_sq2qrq8ir5ihu[8] = "&r->a{}";
const uint8_t gm_sq68mfjf87n5h[16] = "` declaration `";
const uint8_t gm_sq87o4fbigdsl[17] = "the builtin `{}`";
const uint8_t gm_sqa7l1hr9cbsj[41] = "\000\000\000\000\000\000\000\000\031\000\000\000\000\000\000\000i\000\000\000\022\000\000\000\000\000\000\000\000\000\000\000\010\000\000\000\000\000\000\000";
const uint8_t gm_sqbhgmmp8kn4m[3] = "ty";
const uint8_t gm_sqbq94vn3heqs[49] = "(({}){}->clauses[{}])({}{}->env, {}->hctx, {}{})";
const uint8_t gm_sqd102436v4lo[9] = "-lbcrypt";
const uint8_t gm_sqk2n9kn6vnik[25] = "{} \077 ({} \077 {} : {}) : {}";
const uint8_t gm_sqqkkj50acup3[36] = "fmt: a prefix needs base 2, 8 or 16";
const uint8_t gm_squbucrq6qmns[5] = "\\u00";
const uint8_t gm_sr31o7m0aohvd[9] = "continue";
const uint8_t gm_sr5or4g6pm2gc[26] = "an immutable `#soa` array";
const uint8_t gm_sr7m6arab7jm9[41] = "\000\000\000\000\000\000\000\000\031\000\000\000\000\000\000\000@\001\000\000\005\000\000\000\000\000\000\000\000\000\000\000\014\000\000\000\000\000\000\000";
const uint8_t gm_srdb7m29fcjm3[3] = "-R";
const uint8_t gm_srjd0sq8k3d7s[11] = "AtomicLoad";
const uint8_t gm_srnee0q49ttns[32] = "unterminated raw string literal";
const uint8_t gm_srq9bjb7d7qo7[15] = "trailing-comma";
const uint8_t gm_srsiesc4al3v4[50] = "`#export` takes the symbol name: `#export(\"sym\")`";
const uint8_t gm_srt7t7qcsma0n[6] = "chmod";
const uint8_t gm_srvi01p0as8h9[15] = " = 1; break; }";
const uint8_t gm_srvn31r258t3t[16] = "bool-comparison";
const uint8_t gm_ss1mi142k161m[41] = "call through an invalid function pointer";
const uint8_t gm_ss226ibsv86g3[15] = "range_incl_len";
const uint8_t gm_ss496b65r9omj[6] = "slice";
const uint8_t gm_ss5c184rv03vi[18] = "the operands of `";
const uint8_t gm_ss6k80g3hocsv[6] = "`git ";
const uint8_t gm_ssac51i2hapg3[12] = "bind_module";
const uint8_t gm_sscfkpmei7e4o[29] = "this `break` leaves the loop";
const uint8_t gm_ssen3ksgpcf7g[13] = "\000\000\000\000\000\000\000\000\000\000\000\000";
const uint8_t gm_sspbbn4skfqau[8] = ")GM_LD(";
const uint8_t gm_ssqciuageol45[23] = "gm_ord[GM_LD(uint8_t, ";
const uint8_t gm_ssql9980vrfs8[21] = "the rest parameter `";
const uint8_t gm_sss33h9aa3bdf[19] = "GM_LD({}, {} + {})";
const uint8_t gm_sssursgo48qb3[8] = "Release";
const uint8_t gm_sstmd1b5da98l[25] = "compiler/check/consts.gm";
const uint8_t gm_st13dppqqeu9b[41] = "\000\000\000\000\000\000\000\000\025\000\000\000\000\000\000\000\212\003\000\000\022\000\000\000\000\000\000\000\000\000\000\000\015\000\000\000\000\000\000\000";
const uint8_t gm_st1igimogo531[41] = "\000\000\000\000\000\000\000\000\020\000\000\000\000\000\000\000\201\000\000\000\015\000\000\000\000\000\000\000\000\000\000\000\010\000\000\000\000\000\000\000";
const uint8_t gm_st249rq5g1u30[41] = "\000\000\000\000\000\000\000\000\032\000\000\000\000\000\000\0004\001\000\000-\000\000\000\000\000\000\000\000\000\000\000\013\000\000\000\000\000\000\000";
const uint8_t gm_st5r5vj6g8baj[321] = "\000\000\000\000\000\000\000\000\004\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\007\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\003\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\003\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\003\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\003\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\003\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\003\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\002\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\005\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\005\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\006\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\004\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\006\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\004\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\003\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\003\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\003\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\002\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\005\000\000\000\000\000\000\000";
const uint8_t gm_st87qih3spnnu[15] = "gm_trap_bytes(";
const uint8_t gm_sta91ot91ebrb[11] = "constant `";
const uint8_t gm_stb6fe3d6kn09[79] = "`{}` cannot be spread: its length depends on a compile-time parameter (\302\24711.4)";
const uint8_t gm_stcddnajembde[9] = "gm_table";
const uint8_t gm_stf7lvoc7oe54[16] = "GM_CONV({}, {})";
const uint8_t gm_stfnc00lo70l5[41] = "\000\000\000\000\000\000\000\000\031\000\000\000\000\000\000\000\314\000\000\000%\000\000\000\000\000\000\000\000\000\000\000\006\000\000\000\000\000\000\000";
const uint8_t gm_sth0p0t3b0nta[24] = "`: use `dir` or `clean`";
const uint8_t gm_sti4tkgslrkgo[26] = "rotation by a non-integer";
const uint8_t gm_stkqau5o4elnq[15] = "(gm_fiber *fb)";
const uint8_t gm_stpboephdcpba[14] = "gm_heap_free(";
const uint8_t gm_stpc2dcrflnth[63] = "control character (\\u0000-\\u001F) found while parsing a string";
const uint8_t gm_su0hvb2ne02u0[17] = "dependencies = [";
const uint8_t gm_su0q4q446mvpj[30] = "internal error: not a builtin";
const uint8_t gm_su4op6lvvdiua[16] = "dependencies.{}";
const uint8_t gm_su7f9sbon2vv7[6] = ") >> ";
const uint8_t gm_sub9t1fj6sv37[6] = "Clock";
const uint8_t gm_sufaam6561ikm[17] = "gm_fs_next_entry";
const uint8_t gm_suff456m9tthn[6] = "if ((";
const uint8_t gm_sug1rl2kq22u3[9] = "GM_BREAK";
const uint8_t gm_sugq7dl55ocon[7] = "assign";
const uint8_t gm_suhbskqupk9jm[15] = "heap_allocator";
const uint8_t gm_suidg89j7tg2d[5] = ")));";
const uint8_t gm_sump4nk3r1lgv[8] = "complex";
const uint8_t gm_sunu1gosuevl4[15] = "contentChanges";
const uint8_t gm_supf808555d3d[13] = "  at <repl>:";
const uint8_t gm_supvu1ruutbov[9] = "CopyFrom";
const uint8_t gm_suq81kgcl2c91[19] = "cannot read CC: {}";
const uint8_t gm_sura2ofp16u7l[16] = "gm_node *gm_hcx";
const uint8_t gm_surog9s0b8r6p[20] = "textDocument/rename";
const uint8_t gm_surrjo1qo53mh[3] = "lt";
const uint8_t gm_sutk9gh8f6gmt[53] = "the archive contains {} (`{}`), which is not allowed";
const uint8_t gm_sutmkmalrac6l[45] = "a format width or precision is at most 65536";
const uint8_t gm_suv79odpsscph[8] = ", \342\200\246)`";
const uint8_t gm_sv6b1v96egn1q[7] = "insert";
const uint8_t gm_sves6iucf8e25[12] = "gm_is_batch";
const uint8_t gm_svgje5djlutuu[25] = "</style>\012</head>\012<body>\012";
const uint8_t gm_sviu2hfr78esg[6] = "gm_fs";
const uint8_t gm_svjhll1rghogq[41] = "\000\000\000\000\000\000\000\000\024\000\000\000\000\000\000\000\013\001\000\000$\000\000\000\000\000\000\000\000\000\000\000\006\000\000\000\000\000\000\000";
const uint8_t gm_svkdb6kaja59r[7] = "gamma\012";
const uint8_t gm_svn5ml3afd65j[18] = "]) return false;\012";
const uint8_t gm_svr1kffiohqd7[11] = "more than ";
const uint8_t gm_svu0pqir618v4[39] = "internal error: not a thread operation";
uint8_t gm_c0ahkuor6r5[9] __attribute__((aligned(1)));
uint8_t gm_c0lqc5l8usq[6] __attribute__((aligned(1)));
uint8_t gm_c10mbbl74j5[3] __attribute__((aligned(1)));
uint8_t gm_c1n574q3spi[8] __attribute__((aligned(8)));
uint8_t gm_c2gnvqjl56t[4] __attribute__((aligned(1)));
uint8_t gm_c2t30gdg5i0[8] __attribute__((aligned(8)));
uint8_t gm_c445n46obmu[13] __attribute__((aligned(1)));
uint8_t gm_c4h9gubvsld[16] __attribute__((aligned(8)));
uint8_t gm_c4qsd90b3sp[3] __attribute__((aligned(1)));
uint8_t gm_c57p52av493[18] __attribute__((aligned(1)));
uint8_t gm_c5c4j291k0n[1] __attribute__((aligned(1)));
uint8_t gm_c78tgi0epnk[2] __attribute__((aligned(1)));
uint8_t gm_c7qpt44lqjv[8] __attribute__((aligned(1)));
uint8_t gm_c8ib2k3r9g2[61] __attribute__((aligned(1)));
uint8_t gm_c8nfq84m99e[8] __attribute__((aligned(1)));
uint8_t gm_c8umbcba0h7[5] __attribute__((aligned(1)));
uint8_t gm_c90gc8fsnn0[9] __attribute__((aligned(1)));
uint8_t gm_c9e8ns5bad4[16] __attribute__((aligned(8)));
uint8_t gm_c9vplruo2a2[12] __attribute__((aligned(1)));
uint8_t gm_cbg49j387l1[6] __attribute__((aligned(1)));
uint8_t gm_cbms0bo4q1a[16] __attribute__((aligned(8)));
uint8_t gm_ccg980jkcrh[1307] __attribute__((aligned(1)));
uint8_t gm_cd08h1gi9e5[7] __attribute__((aligned(1)));
uint8_t gm_cdkdqgvah3b[14] __attribute__((aligned(1)));
uint8_t gm_cegnsgpm600[10] __attribute__((aligned(1)));
uint8_t gm_cenqalictlb[7] __attribute__((aligned(1)));
uint8_t gm_cf28fp1b3j9[12] __attribute__((aligned(1)));
uint8_t gm_cf8dng56muv[12] __attribute__((aligned(1)));
uint8_t gm_cfhksknhucl[16] __attribute__((aligned(8)));
uint8_t gm_cfpt4ph1ii9[24] __attribute__((aligned(1)));
uint8_t gm_cgmufj2btu3[99] __attribute__((aligned(1)));
uint8_t gm_ci0uc4kk6nj[5] __attribute__((aligned(1)));
uint8_t gm_ci6akgm82k1[15] __attribute__((aligned(1)));
uint8_t gm_cic0r6uhrl9[14] __attribute__((aligned(1)));
uint8_t gm_cift5pqfpf8[5] __attribute__((aligned(1)));
uint8_t gm_cii3u4ppm6k[16] __attribute__((aligned(8)));
uint8_t gm_cj2g6e1rcfd[7] __attribute__((aligned(1)));
uint8_t gm_cj3i6am3onu[14] __attribute__((aligned(1)));
uint8_t gm_cjafjj2b83a[2] __attribute__((aligned(1)));
uint8_t gm_cjt60aq4qvt[8] __attribute__((aligned(8)));
uint8_t gm_cjup0hpsi95[12] __attribute__((aligned(1)));
uint8_t gm_ck5m2h3unvq[6] __attribute__((aligned(1)));
uint8_t gm_ckasdtpni1c[8] __attribute__((aligned(8)));
uint8_t gm_clp0sh2cgr2[16] __attribute__((aligned(8)));
uint8_t gm_cmdr2nifebi[9] __attribute__((aligned(1)));
uint8_t gm_cmsb06prlvh[14] __attribute__((aligned(1)));
uint8_t gm_cmuoir8all0[4] __attribute__((aligned(1)));
uint8_t gm_cn9jip912hb[16] __attribute__((aligned(1)));
uint8_t gm_cntetvl1ho0[28] __attribute__((aligned(1)));
uint8_t gm_cnupg9tkv4t[4] __attribute__((aligned(1)));
uint8_t gm_cnv5thfiqas[49] __attribute__((aligned(1)));
uint8_t gm_co3kl02erms[10] __attribute__((aligned(1)));
uint8_t gm_colgvso06jq[3] __attribute__((aligned(1)));
uint8_t gm_covm5e0en6l[12] __attribute__((aligned(1)));
uint8_t gm_cp3tou70mi3[8] __attribute__((aligned(1)));
uint8_t gm_cpm8oqgl16t[10] __attribute__((aligned(1)));
uint8_t gm_crmhomsqkn5[5] __attribute__((aligned(1)));
uint8_t gm_cs3r3u3eelc[6] __attribute__((aligned(1)));
uint8_t gm_ctk5347uqth[4] __attribute__((aligned(4)));
uint8_t gm_ctmi68o75hk[26] __attribute__((aligned(1)));
uint8_t gm_ctrudjfeksb[6] __attribute__((aligned(1)));
uint8_t gm_cvt51san54i[12] __attribute__((aligned(1)));
uint8_t gm_k0d09vor0rn[40] __attribute__((aligned(8)));
uint8_t gm_k0j6as2o5ac[40] __attribute__((aligned(8)));
uint8_t gm_k2rd7nce5lv[40] __attribute__((aligned(8)));
uint8_t gm_k3u24nfalvr[40] __attribute__((aligned(8)));
uint8_t gm_kat3hjq5aa3[40] __attribute__((aligned(8)));
uint8_t gm_kb4m9ri21ik[40] __attribute__((aligned(8)));
uint8_t gm_kb8poqrtf7g[40] __attribute__((aligned(8)));
uint8_t gm_kcaojba2jhr[40] __attribute__((aligned(8)));
uint8_t gm_ke6nl7d0rco[40] __attribute__((aligned(8)));
uint8_t gm_kf8nrh0sp8b[40] __attribute__((aligned(8)));
uint8_t gm_kinrtga7ov4[40] __attribute__((aligned(8)));
uint8_t gm_kl8do6h9dt6[40] __attribute__((aligned(8)));
uint8_t gm_knna6g3th5t[40] __attribute__((aligned(8)));
uint8_t gm_kns860jaqph[40] __attribute__((aligned(8)));
uint8_t gm_koeeht11j75[40] __attribute__((aligned(8)));
uint8_t gm_krkq6pebrgk[40] __attribute__((aligned(8)));
void *const gm_hddfeetqn95b[] = {(void *)gm_dtdfeetqn95b_0};
void *const gm_hdf4dspmusp5[] = {(void *)gm_dtf4dspmusp5_0, (void *)gm_dtf4dspmusp5_1, (void *)gm_dtf4dspmusp5_2};
void *const gm_hl3gh4gojekg[] = {(void *)gm_f_for_body_bq6tmqb};
void *const gm_hl8sdnavrdjn[] = {(void *)gm_f_for_body_vn00ddp};
void *const gm_hlal1cg2mvd1[] = {(void *)gm_f_for_body_qg33ouo};
void *const gm_hlbfnanio71s[] = {(void *)gm_f_for_body_jvjn1fs};
void *const gm_hlgmpdevos39[] = {(void *)gm_f_for_body_6ellthc};
void *const gm_hlipnojuqrvq[] = {(void *)gm_f_for_body_e61ld42};
void *const gm_hlpn5rfhnj54[] = {(void *)gm_f_for_body_3en4m89};
void *const gm_hlrpdhe6kjfr[] = {(void *)gm_f_for_body_m8t719h};
