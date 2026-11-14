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
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint8_t f0; uint8_t f1; uint8_t f2; uint8_t f3; uint8_t f4; uint8_t f5; } gm_t4q2b5ibsrdik;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint8_t a[1]; } gm_tivanu2mfcv6o;
typedef struct { uint8_t f0; uint8_t p1[3]; union { int32_t v0; } f1; } gm_toe5fmr3vvf4d;
typedef struct { uint8_t f0; uint8_t f1; } gm_tqs5p4n2tq19v;
typedef struct { uint32_t f0; uint32_t f1; gm_b16_8 f2; } gm_t2s5j3n0guq0t;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
_Static_assert(sizeof(gm_t4q2b5ibsrdik) == 6 && _Alignof(gm_t4q2b5ibsrdik) == 1 && offsetof(gm_t4q2b5ibsrdik, f0) == 0 && offsetof(gm_t4q2b5ibsrdik, f1) == 1 && offsetof(gm_t4q2b5ibsrdik, f2) == 2 && offsetof(gm_t4q2b5ibsrdik, f3) == 3 && offsetof(gm_t4q2b5ibsrdik, f4) == 4 && offsetof(gm_t4q2b5ibsrdik, f5) == 5, "layout of `OpenOptions`");
typedef struct { gm_b16_8 f0; uint32_t f1; uint32_t f2; } gm_t5hdnnftkup6m;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
typedef struct { gm_b16_8 f0; uint32_t f1; uint32_t f2; gm_b16_8 f3; } gm_tb40qn7hk0tl9;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; } gm_tfeab279c16rr;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_toe5fmr3vvf4d v0; uint64_t v1; } f1; } gm_thf5kr0is9u9i;
_Static_assert(sizeof(gm_tivanu2mfcv6o) == 1 && _Alignof(gm_tivanu2mfcv6o) == 1 && offsetof(gm_tivanu2mfcv6o, a) == 0, "layout of `[1]u8`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_toe5fmr3vvf4d v0; gm_t9qkh0rsooc8d v1; } f1; } gm_tj5jn79kq9pnm;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; uint8_t f2; uint8_t pe[7]; } gm_tk9n3vlila9qg;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; } f1; } gm_tl572d7579hns;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
_Static_assert(sizeof(gm_toe5fmr3vvf4d) == 8 && _Alignof(gm_toe5fmr3vvf4d) == 4 && offsetof(gm_toe5fmr3vvf4d, f0) == 0 && offsetof(gm_toe5fmr3vvf4d, f1) == 4, "layout of `IoError`");
typedef struct { gm_b16_8 f0; uint32_t f1; uint8_t pe[4]; } gm_tommf4j9upomc;
typedef struct { void * f0; void * f1; void * f2; void * f3; uint32_t f4; uint32_t f5; uint32_t f6; uint8_t p7[4]; gm_b16_8 f7; } gm_toqvmi7jqorid;
_Static_assert(sizeof(gm_tqs5p4n2tq19v) == 2 && _Alignof(gm_tqs5p4n2tq19v) == 1 && offsetof(gm_tqs5p4n2tq19v, f0) == 0 && offsetof(gm_tqs5p4n2tq19v, f1) == 1, "layout of `(u8, u8)`");
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; gm_th2lqbo77pvsc f1; uint64_t f2; } gm_t04fuf8kbk07t;
_Static_assert(sizeof(gm_t2s5j3n0guq0t) == 24 && _Alignof(gm_t2s5j3n0guq0t) == 8 && offsetof(gm_t2s5j3n0guq0t, f0) == 0 && offsetof(gm_t2s5j3n0guq0t, f1) == 4 && offsetof(gm_t2s5j3n0guq0t, f2) == 8, "layout of `Def`");
_Static_assert(sizeof(gm_t5hdnnftkup6m) == 24 && _Alignof(gm_t5hdnnftkup6m) == 8 && offsetof(gm_t5hdnnftkup6m, f0) == 0 && offsetof(gm_t5hdnnftkup6m, f1) == 16 && offsetof(gm_t5hdnnftkup6m, f2) == 20, "layout of `(string, u32, u32)`");
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
_Static_assert(sizeof(gm_tb40qn7hk0tl9) == 40 && _Alignof(gm_tb40qn7hk0tl9) == 8 && offsetof(gm_tb40qn7hk0tl9, f0) == 0 && offsetof(gm_tb40qn7hk0tl9, f1) == 16 && offsetof(gm_tb40qn7hk0tl9, f2) == 20 && offsetof(gm_tb40qn7hk0tl9, f3) == 24, "layout of `SourceLocation`");
typedef struct { gm_tfeab279c16rr f0; uint8_t f1; uint8_t pe[7]; } gm_td6svc7gs468a;
_Static_assert(sizeof(gm_tfeab279c16rr) == 48 && _Alignof(gm_tfeab279c16rr) == 8 && offsetof(gm_tfeab279c16rr, f0) == 0 && offsetof(gm_tfeab279c16rr, f1) == 16 && offsetof(gm_tfeab279c16rr, f2) == 32, "layout of `Toolchain`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tj5jn79kq9pnm v0; gm_b16_8 v1; } f1; } gm_tgvh15qshf643;
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
_Static_assert(sizeof(gm_thf5kr0is9u9i) == 16 && _Alignof(gm_thf5kr0is9u9i) == 8 && offsetof(gm_thf5kr0is9u9i, f0) == 0 && offsetof(gm_thf5kr0is9u9i, f1) == 8, "layout of `IoError!usize`");
typedef struct { gm_th2lqbo77pvsc f0; uint8_t f1; uint8_t pe[7]; } gm_ti5ramhglnvdt;
_Static_assert(sizeof(gm_tj5jn79kq9pnm) == 12 && _Alignof(gm_tj5jn79kq9pnm) == 4 && offsetof(gm_tj5jn79kq9pnm, f0) == 0 && offsetof(gm_tj5jn79kq9pnm, f1) == 4, "layout of `ReadError`");
_Static_assert(sizeof(gm_tk9n3vlila9qg) == 40 && _Alignof(gm_tk9n3vlila9qg) == 8 && offsetof(gm_tk9n3vlila9qg, f0) == 0 && offsetof(gm_tk9n3vlila9qg, f1) == 16 && offsetof(gm_tk9n3vlila9qg, f2) == 32, "layout of `(lib: string, link_name: string, variadic: bool)`");
_Static_assert(sizeof(gm_tl572d7579hns) == 24 && _Alignof(gm_tl572d7579hns) == 8 && offsetof(gm_tl572d7579hns, f0) == 0 && offsetof(gm_tl572d7579hns, f1) == 8, "layout of `string!()`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
_Static_assert(sizeof(gm_tommf4j9upomc) == 24 && _Alignof(gm_tommf4j9upomc) == 8 && offsetof(gm_tommf4j9upomc, f0) == 0 && offsetof(gm_tommf4j9upomc, f1) == 16, "layout of `(string, u32)`");
_Static_assert(sizeof(gm_toqvmi7jqorid) == 64 && _Alignof(gm_toqvmi7jqorid) == 8 && offsetof(gm_toqvmi7jqorid, f0) == 0 && offsetof(gm_toqvmi7jqorid, f1) == 8 && offsetof(gm_toqvmi7jqorid, f2) == 16 && offsetof(gm_toqvmi7jqorid, f3) == 24 && offsetof(gm_toqvmi7jqorid, f4) == 32 && offsetof(gm_toqvmi7jqorid, f5) == 36 && offsetof(gm_toqvmi7jqorid, f6) == 40 && offsetof(gm_toqvmi7jqorid, f7) == 48, "layout of `Compiled`");
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
typedef struct { uint8_t f0; gm_t9qkh0rsooc8d f1; gm_t9qkh0rsooc8d f2; uint8_t p3[5]; gm_th2lqbo77pvsc f3; gm_b16_8 f4; } gm_tvcg39brotose;
_Static_assert(sizeof(gm_t04fuf8kbk07t) == 40 && _Alignof(gm_t04fuf8kbk07t) == 8 && offsetof(gm_t04fuf8kbk07t, f0) == 0 && offsetof(gm_t04fuf8kbk07t, f1) == 8 && offsetof(gm_t04fuf8kbk07t, f2) == 32, "layout of `Hit`");
typedef struct { gm_th2lqbo77pvsc f0; gm_ti5ramhglnvdt f1; uint8_t f2; uint8_t pe[7]; } gm_t9feugf5hcdtg;
_Static_assert(sizeof(gm_td6svc7gs468a) == 56 && _Alignof(gm_td6svc7gs468a) == 8 && offsetof(gm_td6svc7gs468a, f0) == 0 && offsetof(gm_td6svc7gs468a, f1) == 48, "layout of `?Toolchain`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tvcg39brotose v0; gm_b16_8 v1; } f1; } gm_tdpp5715iet0a;
typedef struct { gm_tvcg39brotose f0; gm_trqfvmjtt4sij f1; gm_trqfvmjtt4sij f2; gm_trqfvmjtt4sij f3; uint32_t f4; uint8_t pe[4]; } gm_tec68k60rd12v;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t04fuf8kbk07t v0; gm_b16_8 v1; } f1; } gm_tepv5jklv667k;
_Static_assert(sizeof(gm_tgvh15qshf643) == 24 && _Alignof(gm_tgvh15qshf643) == 8 && offsetof(gm_tgvh15qshf643, f0) == 0 && offsetof(gm_tgvh15qshf643, f1) == 8, "layout of `ReadError![]u8`");
_Static_assert(sizeof(gm_ti5ramhglnvdt) == 32 && _Alignof(gm_ti5ramhglnvdt) == 8 && offsetof(gm_ti5ramhglnvdt, f0) == 0 && offsetof(gm_ti5ramhglnvdt, f1) == 24, "layout of `??string`");
_Static_assert(sizeof(gm_tvcg39brotose) == 48 && _Alignof(gm_tvcg39brotose) == 8 && offsetof(gm_tvcg39brotose, f0) == 0 && offsetof(gm_tvcg39brotose, f1) == 1 && offsetof(gm_tvcg39brotose, f2) == 2 && offsetof(gm_tvcg39brotose, f3) == 8 && offsetof(gm_tvcg39brotose, f4) == 32, "layout of `Options`");
_Static_assert(sizeof(gm_t9feugf5hcdtg) == 64 && _Alignof(gm_t9feugf5hcdtg) == 8 && offsetof(gm_t9feugf5hcdtg, f0) == 0 && offsetof(gm_t9feugf5hcdtg, f1) == 24 && offsetof(gm_t9feugf5hcdtg, f2) == 56, "layout of `Classified`");
_Static_assert(sizeof(gm_tdpp5715iet0a) == 56 && _Alignof(gm_tdpp5715iet0a) == 8 && offsetof(gm_tdpp5715iet0a, f0) == 0 && offsetof(gm_tdpp5715iet0a, f1) == 8, "layout of `Cli`");
_Static_assert(sizeof(gm_tec68k60rd12v) == 176 && _Alignof(gm_tec68k60rd12v) == 8 && offsetof(gm_tec68k60rd12v, f0) == 0 && offsetof(gm_tec68k60rd12v, f1) == 48 && offsetof(gm_tec68k60rd12v, f2) == 88 && offsetof(gm_tec68k60rd12v, f3) == 128 && offsetof(gm_tec68k60rd12v, f4) == 168, "layout of `Repl`");
_Static_assert(sizeof(gm_tepv5jklv667k) == 48 && _Alignof(gm_tepv5jklv667k) == 8 && offsetof(gm_tepv5jklv667k, f0) == 0 && offsetof(gm_tepv5jklv667k, f1) == 8, "layout of `Scan`");
struct gm_fr_clear_3ibm3tg {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_clear_gp35vgp {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_clear_h4369ui {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_clear_sl9fc3u {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_command_7gn4uju {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_command_run_06r7r92 {
    gm_frame h;
    gm_tvcg39brotose l0; /* options */
    gm_tvcg39brotose l1; /* opts */
    gm_tec68k60rd12v l4; /* r */
    gm_trqfvmjtt4sij l6; /* pending */
    gm_t9feugf5hcdtg l11; /* c */
    uint8_t ret;
};
struct gm_fr_compile_rj09gp7 {
    gm_frame h;
    gm_td6svc7gs468a l4; /* tail */
    gm_trqfvmjtt4sij l7; /* src */
    gm_tfeab279c16rr l24; /* t */
    gm_toqvmi7jqorid l25; /* c */
    gm_toqvmi7jqorid ret;
};
struct gm_fr_contains_gkki4kq {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_describe_read_error_uikkr59 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_destroy_602qeev {
    gm_frame h;
    gm_tb40qn7hk0tl9 l1; /* site */
    gm_b0_1 ret;
};
struct gm_fr_empty_slice_51k8cgl {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_5nn7ph0 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_icu3qs9 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ijtp9lo {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_lpi80k4_2 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_oldiiq9_2 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_q5bjns8 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_extend_0cs85gs {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_extend_e1j40m3 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_format_of_nbvl1cj {
    gm_frame h;
    gm_t9qkh0rsooc8d ret;
};
struct gm_fr_is_space_pq8dr45 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_list_1t36jvm {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_93qogsr {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_e6ie1fq_2 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_epvducg {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_f0gjjbh_2 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_glg30gi {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_session_6f2qqqv {
    gm_frame h;
    gm_tk9n3vlila9qg l6; /* rec */
    gm_b0_1 ret;
};
struct gm_fr_list_uvfu1k1 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_load_file_3kdl78m {
    gm_frame h;
    gm_t9feugf5hcdtg l6; /* c */
    gm_trqfvmjtt4sij l8; /* rest */
    gm_tk9n3vlila9qg l12; /* i */
    gm_trqfvmjtt4sij l18; /* parts */
    gm_tk9n3vlila9qg l21; /* i */
    uint8_t ret;
};
struct gm_fr_mode_of_pq04he2 {
    gm_frame h;
    gm_t9qkh0rsooc8d ret;
};
struct gm_fr_new_repl_hdsg2o4 {
    gm_frame h;
    gm_tvcg39brotose l0; /* opts */
    gm_tec68k60rd12v ret;
};
struct gm_fr_parse_options_9st61k7 {
    gm_frame h;
    gm_tvcg39brotose l1; /* o */
    gm_t04fuf8kbk07t l4; /* hit */
    gm_tdpp5715iet0a ret;
};
struct gm_fr_push_1e500nc_2 {
    gm_frame h;
    gm_tk9n3vlila9qg l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_3ef746b {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_52bt1f1 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_ej78v89 {
    gm_frame h;
    gm_tk9n3vlila9qg l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_f9bu01m {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_t6e85n8_2 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_t83rg2m {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_read_error_text_1lgi2bl {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_read_line_1oksar8 {
    gm_frame h;
    gm_trqfvmjtt4sij l0; /* bytes */
    gm_tl572d7579hns ret;
};
struct gm_fr_regrow_0jmnnpk {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_3cp9o8d {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_967kt0a_2 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_9pv3nuf {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_ko1se61 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_o86m6d7_2 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_pcenrov {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_release_mdh37fq {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_renamed_vars_fho5hvj {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* out */
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_replay_texts_3te9pi7 {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* out */
    gm_tk9n3vlila9qg l5; /* rec */
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_reset_pb2jqrj {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_set_var_pjsp5eq {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_show_861qdlm {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_shown_o0bbr93 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_starts_with_6vvt01e {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_statements_dafpns9 {
    gm_frame h;
    gm_trqfvmjtt4sij l3; /* defs */
    gm_trqfvmjtt4sij l4; /* next_vars */
    gm_trqfvmjtt4sij l9; /* uses */
    gm_trqfvmjtt4sij l15; /* replay */
    gm_tfeab279c16rr l16; /* raw */
    gm_toqvmi7jqorid l17; /* c_raw */
    gm_toqvmi7jqorid l18; /* chosen */
    gm_tfeab279c16rr l20; /* tail */
    gm_toqvmi7jqorid l21; /* c_val */
    gm_trqfvmjtt4sij l28; /* pure */
    gm_tfeab279c16rr l29; /* tail */
    gm_toqvmi7jqorid l30; /* probe */
    gm_trqfvmjtt4sij l33; /* names */
    uint8_t ret;
};
struct gm_fr_submit_pah0u77 {
    gm_frame h;
    gm_t9feugf5hcdtg l2; /* c */
    gm_tk9n3vlila9qg l9; /* i */
    gm_b0_1 ret;
};
struct gm_fr_submit_text_7dvfev6 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_trim_fdrfe6i {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_trim_newlines_9rj3i73 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_try_decls_23vcohd {
    gm_frame h;
    gm_trqfvmjtt4sij l3; /* names */
    gm_tk9n3vlila9qg l6; /* i */
    gm_trqfvmjtt4sij l10; /* replay */
    gm_toqvmi7jqorid l11; /* c */
    gm_trqfvmjtt4sij l12; /* kept */
    gm_tk9n3vlila9qg l25; /* i */
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_type_of_b1a5sco {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* replay */
    gm_trqfvmjtt4sij l3; /* uses */
    gm_tfeab279c16rr l5; /* direct */
    gm_toqvmi7jqorid l6; /* c */
    gm_tfeab279c16rr l7; /* bound */
    gm_toqvmi7jqorid l8; /* c2 */
    gm_b0_1 ret;
};
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b);
inline bool gm_eqjmccqdb90f(const uint8_t *a, const uint8_t *b);
__attribute__((aligned(4))) void gm_f_check_source_nh9qcnk(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_t9qkh0rsooc8d a1, uint32_t a2, gm_b16_8 a3);
__attribute__((aligned(4))) void gm_f_classify_rm38cme(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) void gm_f_clear_3ibm3tg(void * a0);
inline __attribute__((aligned(4))) void gm_f_clear_gp35vgp(void * a0);
inline __attribute__((aligned(4))) void gm_f_clear_h4369ui(void * a0);
inline __attribute__((aligned(4))) void gm_f_clear_sl9fc3u(void * a0);
__attribute__((aligned(4))) uint8_t gm_f_command_7gn4uju(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) uint8_t gm_f_command_run_06r7r92(void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_compile_rj09gp7(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_b16_8 a2, gm_b16_8 a3, const void * a4, gm_b16_8 a5, uint8_t a6);
inline __attribute__((aligned(4))) uint8_t gm_f_contains_gkki4kq(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_deinit_gvn47sj(void * a0);
__attribute__((aligned(4))) void gm_f_deinit_r70pji5(void * a0);
__attribute__((aligned(4))) void gm_f_deinit_sejdpqv(void * a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_describe_read_error_uikkr59(void *env, gm_node *cx, gm_node *inst, gm_tj5jn79kq9pnm a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_destroy_602qeev(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_dupe_g9iqpvn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, const void * a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_51k8cgl(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_5nn7ph0(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_icu3qs9(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_lpi80k4_2(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_oldiiq9_2(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_q5bjns8(void);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_extend_0cs85gs(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_extend_e1j40m3(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_find_nab29co(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_format_5a05gmg(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_j0ggm04(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) gm_t9qkh0rsooc8d gm_f_format_of_nbvl1cj(gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_format_uq5dnfv(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) uint8_t gm_f_is_blank_asoolmf(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_space_pq8dr45(uint8_t a0);
__attribute__((aligned(4))) void gm_f_join_sfknljb(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) void gm_f_list_1t36jvm(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_93qogsr(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_e6ie1fq_2(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_epvducg(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_f0gjjbh_2(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_glg30gi(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_b0_1 gm_f_list_session_6f2qqqv(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) uint8_t gm_f_load_file_3kdl78m(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_lookup_var_3vpm3u6(void *gm_out, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) uint8_t gm_f_may_mutate_uj4e7jp(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_t9qkh0rsooc8d gm_f_mode_of_pq04he2(gm_b16_8 a0);
inline __attribute__((aligned(4))) void gm_f_new_repl_hdsg2o4(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) uint8_t gm_f_open_brackets_hicehud(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_os_error_text_319eh6h(void *env, gm_node *cx, gm_node *inst, gm_toe5fmr3vvf4d a0);
__attribute__((aligned(4))) void gm_f_parse_options_9st61k7(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_1e500nc_2(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_3ef746b(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_52bt1f1(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_ej78v89(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_f9bu01m(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_t6e85n8_2(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_t83rg2m(void * a0, uint8_t a1);
__attribute__((aligned(4))) void gm_f_read_all_r9rh9qd(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_read_error_text_1lgi2bl(void *env, gm_node *cx, gm_node *inst, gm_tj5jn79kq9pnm a0);
__attribute__((aligned(4))) void gm_f_read_line_1oksar8(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_0jmnnpk(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_3cp9o8d(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_967kt0a_2(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_9pv3nuf(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_ko1se61(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_o86m6d7_2(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_pcenrov(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_raw_4ag1s1i(void * a0, uint64_t a1, void * a2, uint64_t a3, uint64_t a4, uint64_t a5);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_release_mdh37fq(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_rename_47b7og5(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_renamed_vars_fho5hvj(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_render_region_ppot3lm(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint32_t a2, uint32_t a3, gm_b16_8 a4, gm_t9qkh0rsooc8d a5);
__attribute__((aligned(4))) void gm_f_replay_texts_3te9pi7(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1);
inline __attribute__((aligned(4))) void gm_f_reset_pb2jqrj(void * a0);
__attribute__((aligned(4))) uint8_t gm_f_run_aqa5hss(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t9qkh0rsooc8d a1, uint8_t a2);
__attribute__((aligned(4))) void gm_f_scan_ll1gqkn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, uint64_t a1, uint8_t a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_set_var_pjsp5eq(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_show_861qdlm(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_shown_o0bbr93(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t9qkh0rsooc8d a1);
inline __attribute__((aligned(4))) uint8_t gm_f_starts_with_6vvt01e(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) uint8_t gm_f_statements_dafpns9(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, const void * a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_submit_pah0u77(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, const void * a2);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_submit_text_7dvfev6(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_syntax_errors_fk23f5t(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_t9qkh0rsooc8d a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_trim_end_6sjv6rg(gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_trim_fdrfe6i(gm_b16_8 a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_trim_newlines_9rj3i73(gm_b16_8 a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_trim_start_179crak(gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_try_decls_23vcohd(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_type_of_b1a5sco(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) uint8_t gm_f_utf8_valid_a8oc1t8(gm_b16_8 a0);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_0gtanrr(void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_nre2qb9(void * a0, const void * a1);
GM_NOINLINE void gm_pf0nmuupvugn(gm_node *cx, gm_b16_8 a, const char *file, uint32_t line, uint32_t col);
GM_NOINLINE bool gm_pf1m4b8q4e78(gm_node *cx, const void *d, uintptr_t n, const char *file, uint32_t line, uint32_t col);
GM_NOINLINE bool gm_pf454qfrpt6r(gm_node *cx, const void *d, uintptr_t n, const char *file, uint32_t line, uint32_t col);
GM_NOINLINE void gm_pfmaojsuds6a(gm_node *cx, gm_b16_8 a, const char *file, uint32_t line, uint32_t col);
GM_NOINLINE bool gm_pfu8dq5bikbh(gm_node *cx, const gm_type *d, const void *v, const char *file, uint32_t line, uint32_t col);
extern int32_t gm_x_isatty_dhpcvep(int32_t) __asm__(GM_SYM("isatty"));
extern uint8_t gm_c5dv10uirua[16] __attribute__((aligned(8)));
extern uint8_t gm_c7983l5vkhl[16] __attribute__((aligned(8)));
extern uint8_t gm_cd231ebi1kg[16] __attribute__((aligned(8)));
extern uint8_t gm_cdgbrlsjvrb[16] __attribute__((aligned(8)));
extern uint8_t gm_celpgpm1fi8[1] __attribute__((aligned(1)));
extern uint8_t gm_ckbflbrril1[1] __attribute__((aligned(1)));
extern uint8_t gm_cnohofor2q6[16] __attribute__((aligned(8)));
extern uint8_t gm_k093ni2ahjb[40] __attribute__((aligned(8)));
extern uint8_t gm_kdmasfkh2un[40] __attribute__((aligned(8)));
extern uint8_t gm_kh9bgce5chr[40] __attribute__((aligned(8)));
extern uint8_t gm_kif5rfegp72[40] __attribute__((aligned(8)));
extern const uint8_t gm_s02419ud122hi[8];
extern const uint8_t gm_s055t7hpo88bt[4];
extern const uint8_t gm_s0iid7nkkvf30[55];
extern const uint8_t gm_s0of2eragnise[6];
extern const uint8_t gm_s1qrqfh656g4q[6];
extern const uint8_t gm_s39mljh669qgl[2];
extern const uint8_t gm_s3a1oio07ri1p[3];
extern const uint8_t gm_s3qd7ldqb9526[29];
extern const uint8_t gm_s3vfg72f8aupk[20];
extern const uint8_t gm_s4chbmodjkinv[7];
extern const uint8_t gm_s4hd92lokmtot[4];
extern const uint8_t gm_s4nquq7iq8v5h[29];
extern const uint8_t gm_s4uditudns916[3];
extern const uint8_t gm_s527dqlmmaier[6];
extern const uint8_t gm_s54emlnl8lmoe[38];
extern const uint8_t gm_s684ivkkpni41[7];
extern const uint8_t gm_s68c5jkfvg5dd[86];
extern const uint8_t gm_s6iuue4hdrbt9[5];
extern const uint8_t gm_s6kq1mi4s1g58[3];
extern const uint8_t gm_s7cu0m5qc7arr[43];
extern const uint8_t gm_s8dbdqj1sqibj[3];
extern const uint8_t gm_s9pcjvamrhbn7[25];
extern const uint8_t gm_sdg1ovmjdibb5[6];
extern const uint8_t gm_sfdbfrv46lihd[2];
extern const uint8_t gm_sfeb57kq1r2n4[4];
extern const uint8_t gm_sg5gdnrith2i6[3];
extern const uint8_t gm_sggg4cm5q63i6[5];
extern const uint8_t gm_sgk14ir7dv1gf[6];
extern const uint8_t gm_sgvln0vb20p94[17];
extern const uint8_t gm_sho8l3vae133f[8];
extern const uint8_t gm_sidpdvffdra02[14];
extern const uint8_t gm_sii9tamh3vah6[32];
extern const uint8_t gm_sikpqtja8pqpv[3];
extern const uint8_t gm_sjbjet56e96co[17];
extern const uint8_t gm_sjsosmp24eqn0[36];
extern const uint8_t gm_skkiv5ia4t4au[39];
extern const uint8_t gm_skolmnfc0ccbd[2];
extern const uint8_t gm_sl0oflhpdomi7[5];
extern const uint8_t gm_slgmqgkioa4u6[3];
extern const uint8_t gm_slrfv83pestqp[31];
extern const uint8_t gm_smrc1u3r4i5hd[7];
extern const uint8_t gm_sn1sq59pu45vv[39];
extern const uint8_t gm_sn9nkqgu8isqo[3];
extern const uint8_t gm_snp0oqa4m79hk[6];
extern const uint8_t gm_so514oj5nbhcu[2];
extern const uint8_t gm_sog4rksrmsqd2[5];
extern const uint8_t gm_sojb2hhp0ln7r[31];
extern const uint8_t gm_som0q53jqvjh3[5];
extern const uint8_t gm_spun41oorb00o[30];
extern const uint8_t gm_squacltecdsom[5];
extern const uint8_t gm_sr4qbrk6tlgaa[1];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_src3d0v42kavhv7[];
extern const char gm_src3dhhc4laniae[];
extern const char gm_src3onm9035msn9[];
extern const char gm_srcc24pnihhgkug[];
extern const char gm_srcn623bb2dv5no[];
extern const char gm_srcrgc9bkquf6nr[];
extern const uint8_t gm_sreprot2s508e[3];
extern const uint8_t gm_ssfhrckkphjsd[6];
extern const uint8_t gm_ssfjmtpagd1pu[6];
extern const uint8_t gm_sshi9ffc6nkc4[10];
extern const uint8_t gm_ssulg3jrm3q2v[16];
extern const uint8_t gm_su5tga7voctrl[7];
extern const gm_type gm_tybihgc69jv3;
extern const gm_type gm_tyo86ks9qkvt;
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b) {
    uintptr_t n = GM_LD(uintptr_t, a + 8);
    if (n != GM_LD(uintptr_t, b + 8)) return false;
    const uint8_t *pa = GM_LD(void *, a), *pb = GM_LD(void *, b);
    return n == 0 || memcmp(pa, pb, n * 1) == 0;
}
inline bool gm_eqjmccqdb90f(const uint8_t *a, const uint8_t *b) {
    return GM_LD(uint8_t, a) == GM_LD(uint8_t, b);
}
inline __attribute__((aligned(4))) void gm_f_clear_3ibm3tg(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_clear_3ibm3tg F;
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
        if (t6 > t7 || t7 > (uintptr_t)t4) gm_trap_slice(t6, t7, t4, gm_srcrgc9bkquf6nr, 140, 15);
        gm_b16_8 t8;
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 32)));
        GM_ST(uintptr_t, (uint8_t *)&t8 + 8, (uintptr_t)(t7 - t6));
        GM_ST(gm_b16_8, (((uint8_t *)t2) + 0), t8);
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_clear_gp35vgp(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_clear_gp35vgp F;
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
        if (t6 > t7 || t7 > (uintptr_t)t4) gm_trap_slice(t6, t7, t4, gm_srcrgc9bkquf6nr, 140, 15);
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
inline __attribute__((aligned(4))) void gm_f_clear_h4369ui(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_clear_h4369ui F;
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
        if (t6 > t7 || t7 > (uintptr_t)t4) gm_trap_slice(t6, t7, t4, gm_srcrgc9bkquf6nr, 140, 15);
        gm_b16_8 t8;
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t8 + 8, (uintptr_t)(t7 - t6));
        GM_ST(gm_b16_8, (((uint8_t *)t2) + 0), t8);
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_clear_sl9fc3u(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_clear_sl9fc3u F;
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
        if (t6 > t7 || t7 > (uintptr_t)t4) gm_trap_slice(t6, t7, t4, gm_srcrgc9bkquf6nr, 140, 15);
        gm_b16_8 t8;
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 32)));
        GM_ST(uintptr_t, (uint8_t *)&t8 + 8, (uintptr_t)(t7 - t6));
        GM_ST(gm_b16_8, (((uint8_t *)t2) + 0), t8);
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) uint8_t gm_f_contains_gkki4kq(gm_b16_8 a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_contains_gkki4kq F;
    gm_b16_8 gm_l0; /* s */
    gm_b16_8 gm_l1; /* needle */
    GM_STACK_CHECK_SHALLOW(&F, 83u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_b16_8 t1 = gm_l0;
    gm_b16_8 t2 = gm_l1;
    gm_tf5srh4mp4b3i t3 = gm_f_find_nab29co(t1, t2);
    uint8_t t4;
    if (((((uint8_t *)&t3))[8] != 0)) goto mn6;
    t4 = ((uint8_t)0);
    goto me5;
mn6:;
    t4 = ((uint8_t)1);
    goto me5;
mn7:;
    gm_trap("no match arm matched the scrutinee", gm_src3d0v42kavhv7, 84, 50);
me5:;
    F.ret = t4;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_describe_read_error_uikkr59(void *env, gm_node *cx, gm_node *inst, gm_tj5jn79kq9pnm a0) {
    struct gm_fr_describe_read_error_uikkr59 F;
    gm_tj5jn79kq9pnm gm_l0; /* e */
    gm_toe5fmr3vvf4d gm_l1; /* io */
    GM_STACK_CHECK(&F, 118u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t1;
    uint8_t t4 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t4 != ((uint8_t)0ULL)) goto mn3;
    gm_toe5fmr3vvf4d t5 = GM_LD(gm_toe5fmr3vvf4d, (((uint8_t *)&gm_l0) + 4));
    gm_l1 = t5;
    gm_toe5fmr3vvf4d t6 = gm_l1;
    gm_b16_8 t7 = gm_f_os_error_text_319eh6h((void *)NULL, cx, NULL, t6);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    t1 = t7;
    goto me2;
mn3:;
    uint8_t t9 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t9 != ((uint8_t)1ULL)) goto mn8;
    gm_b16_8 t10;
    GM_ST(void *, (uint8_t *)&t10, (void *)(gm_sidpdvffdra02));
    GM_ST(uintptr_t, (uint8_t *)&t10 + 8, (uintptr_t)(13));
    t1 = t10;
    goto me2;
mn8:;
    gm_trap("no match arm matched the scrutinee", gm_src3onm9035msn9, 85, 73);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_destroy_602qeev(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_destroy_602qeev F;
    void * gm_l0; /* p */
    gm_t3mf58gv9kf3n gm_l2; /* a */
    GM_STACK_CHECK(&F, 88u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_node *pn2 = gm_find(cx, 587034433);
        if (!pn2) gm_trap("no handler installed for effect `Alloc`", gm_src2ah81i3aa8hm, 708, 10);
        gm_t3mf58gv9kf3n t3;
        t3 = ((gm_t3mf58gv9kf3n (*)(void *, gm_node *, gm_node *))pn2->clauses[0])(pn2->env, pn2->hctx, pn2);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l2 = t3;
    }
    {
    }
    {
        void * t4 = GM_LD(void *, (((uint8_t *)&gm_l2) + 8));
        void * t5 = GM_LD(void *, (((uint8_t *)t4) + 16));
        void * t6 = GM_LD(void *, (((uint8_t *)&gm_l2) + 0));
        void * t7 = gm_l0;
        void * t8 = t7;
        void * t9 = t5;
        gm_b0_1 t10;
        ((void (*)(void *, void *, uint64_t, uint64_t))GM_PTR_UNTAG(t9))(t6, t8, ((uint64_t)48ULL), ((uint64_t)8ULL));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_51k8cgl(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_51k8cgl F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_icu3qs9(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_icu3qs9 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_lpi80k4_2(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_lpi80k4_2 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_oldiiq9_2(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_oldiiq9_2 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_q5bjns8(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_q5bjns8 F;
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_extend_0cs85gs(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_extend_0cs85gs F;
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
            gm_tftb1odc739t5 t17 = gm_f_regrow_3cp9o8d(t14, t15, t16);
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
            GM_ST(void *, (uint8_t *)&t33, (void *)(((uint8_t *)t24 + t26 * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t33 + 8, (uintptr_t)(t32 - t26));
            gm_b16_8 t34 = gm_l1;
            if (GM_LD(uintptr_t, ((uint8_t *)&t33) + 8) != 0) memmove(GM_LD(void *, ((uint8_t *)&t33)), GM_LD(void *, ((uint8_t *)&t34)), GM_LD(uintptr_t, ((uint8_t *)&t33) + 8) * 1);
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
        GM_ST(void *, (uint8_t *)&t46, (void *)(((uint8_t *)t38 + t39 * 1)));
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_extend_e1j40m3(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_extend_e1j40m3 F;
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
            gm_tftb1odc739t5 t17 = gm_f_regrow_967kt0a_2(t14, t15, t16);
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
            GM_ST(void *, (uint8_t *)&t33, (void *)(((uint8_t *)t24 + t26 * 32)));
            GM_ST(uintptr_t, (uint8_t *)&t33 + 8, (uintptr_t)(t32 - t26));
            gm_b16_8 t34 = gm_l1;
            if (GM_LD(uintptr_t, ((uint8_t *)&t33) + 8) != 0) memmove(GM_LD(void *, ((uint8_t *)&t33)), GM_LD(void *, ((uint8_t *)&t34)), GM_LD(uintptr_t, ((uint8_t *)&t33) + 8) * 32);
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
        GM_ST(void *, (uint8_t *)&t46, (void *)(((uint8_t *)t38 + t39 * 32)));
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
inline __attribute__((aligned(4))) gm_t9qkh0rsooc8d gm_f_format_of_nbvl1cj(gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_format_of_nbvl1cj F;
    gm_b16_8 gm_l0; /* value */
    GM_STACK_CHECK_SHALLOW(&F, 22u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_t9qkh0rsooc8d t1;
    if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8) == 5) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l0)), gm_ssfhrckkphjsd, 5) == 0)) goto mn3;
    gm_t9qkh0rsooc8d t4;
    GM_ST(uint8_t, ((uint8_t *)&t4 + 0), ((uint8_t)1ULL));
    t1 = t4;
    goto me2;
mn3:;
    if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8) == 4) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l0)), gm_sggg4cm5q63i6, 4) == 0)) goto mn5;
    gm_t9qkh0rsooc8d t6;
    GM_ST(uint8_t, ((uint8_t *)&t6 + 0), ((uint8_t)2ULL));
    t1 = t6;
    goto me2;
mn5:;
    gm_t9qkh0rsooc8d t8;
    GM_ST(uint8_t, ((uint8_t *)&t8 + 0), ((uint8_t)0ULL));
    t1 = t8;
    goto me2;
mn7:;
    gm_trap("no match arm matched the scrutinee", gm_srcn623bb2dv5no, 322, 54);
me2:;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_space_pq8dr45(uint8_t a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_space_pq8dr45 F;
    uint8_t gm_l0; /* c */
    GM_STACK_CHECK_SHALLOW(&F, 12u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t t1 = gm_l0;
    gm_t4q2b5ibsrdik t2;
    GM_ST(uint8_t, ((uint8_t *)&t2 + 0), ((uint8_t)32ULL));
    GM_ST(uint8_t, ((uint8_t *)&t2 + 1), ((uint8_t)9ULL));
    GM_ST(uint8_t, ((uint8_t *)&t2 + 2), ((uint8_t)10ULL));
    GM_ST(uint8_t, ((uint8_t *)&t2 + 3), ((uint8_t)13ULL));
    GM_ST(uint8_t, ((uint8_t *)&t2 + 4), ((uint8_t)11ULL));
    GM_ST(uint8_t, ((uint8_t *)&t2 + 5), ((uint8_t)12ULL));
    uint8_t t3 = 0;
    if (gm_eqjmccqdb90f(((uint8_t *)&t2) + 0, ((uint8_t *)&t1))) t3 = 1;
    if (gm_eqjmccqdb90f(((uint8_t *)&t2) + 1, ((uint8_t *)&t1))) t3 = 1;
    if (gm_eqjmccqdb90f(((uint8_t *)&t2) + 2, ((uint8_t *)&t1))) t3 = 1;
    if (gm_eqjmccqdb90f(((uint8_t *)&t2) + 3, ((uint8_t *)&t1))) t3 = 1;
    if (gm_eqjmccqdb90f(((uint8_t *)&t2) + 4, ((uint8_t *)&t1))) t3 = 1;
    if (gm_eqjmccqdb90f(((uint8_t *)&t2) + 5, ((uint8_t *)&t1))) t3 = 1;
    uint8_t t4 = (uint8_t)(t3);
    F.ret = t4;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_list_1t36jvm(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_1t36jvm F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_51k8cgl();
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
inline __attribute__((aligned(4))) void gm_f_list_93qogsr(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_93qogsr F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_icu3qs9();
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
inline __attribute__((aligned(4))) void gm_f_list_e6ie1fq_2(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_e6ie1fq_2 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_oldiiq9_2();
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
inline __attribute__((aligned(4))) void gm_f_list_f0gjjbh_2(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_f0gjjbh_2 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_lpi80k4_2();
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
inline __attribute__((aligned(4))) void gm_f_list_glg30gi(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_glg30gi F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_q5bjns8();
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
inline __attribute__((aligned(4))) gm_t9qkh0rsooc8d gm_f_mode_of_pq04he2(gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_mode_of_pq04he2 F;
    gm_b16_8 gm_l0; /* value */
    GM_STACK_CHECK_SHALLOW(&F, 23u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_t9qkh0rsooc8d t1;
    if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8) == 7) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l0)), gm_s02419ud122hi, 7) == 0)) goto mn3;
    gm_t9qkh0rsooc8d t4;
    GM_ST(uint8_t, ((uint8_t *)&t4 + 0), ((uint8_t)1ULL));
    t1 = t4;
    goto me2;
mn3:;
    if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8) == 4) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l0)), gm_s6iuue4hdrbt9, 4) == 0)) goto mn5;
    gm_t9qkh0rsooc8d t6;
    GM_ST(uint8_t, ((uint8_t *)&t6 + 0), ((uint8_t)2ULL));
    t1 = t6;
    goto me2;
mn5:;
    if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8) == 4) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l0)), gm_sl0oflhpdomi7, 4) == 0)) goto mn7;
    gm_t9qkh0rsooc8d t8;
    GM_ST(uint8_t, ((uint8_t *)&t8 + 0), ((uint8_t)3ULL));
    t1 = t8;
    goto me2;
mn7:;
    gm_t9qkh0rsooc8d t10;
    GM_ST(uint8_t, ((uint8_t *)&t10 + 0), ((uint8_t)0ULL));
    t1 = t10;
    goto me2;
mn9:;
    gm_trap("no match arm matched the scrutinee", gm_srcn623bb2dv5no, 314, 45);
me2:;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_new_repl_hdsg2o4(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_new_repl_hdsg2o4 F;
    GM_STACK_CHECK(&F, 168u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memcpy(&F.l0, a0, sizeof F.l0);
    memset((uint8_t *)&(*(gm_tec68k60rd12v *)gm_out) + 172, 0, 4);
    gm_tvcg39brotose t1 = F.l0;
    gm_trqfvmjtt4sij t2;
    gm_f_list_1t36jvm((void *)&t2, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_trqfvmjtt4sij t3;
    gm_f_list_93qogsr((void *)&t3, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_trqfvmjtt4sij t4;
    gm_f_list_e6ie1fq_2((void *)&t4, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    GM_ST(gm_tvcg39brotose, (uint8_t *)&(*(gm_tec68k60rd12v *)gm_out) + 0, t1);
    GM_ST(gm_trqfvmjtt4sij, (uint8_t *)&(*(gm_tec68k60rd12v *)gm_out) + 48, t2);
    GM_ST(gm_trqfvmjtt4sij, (uint8_t *)&(*(gm_tec68k60rd12v *)gm_out) + 88, t3);
    GM_ST(gm_trqfvmjtt4sij, (uint8_t *)&(*(gm_tec68k60rd12v *)gm_out) + 128, t4);
    GM_ST(uint32_t, (uint8_t *)&(*(gm_tec68k60rd12v *)gm_out) + 168, ((uint32_t)0ULL));
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_1e500nc_2(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_1e500nc_2 F;
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
            gm_b16_8 t11 = gm_f_empty_slice_lpi80k4_2();
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
            gm_tftb1odc739t5 t13 = gm_f_regrow_o86m6d7_2(t10, ((uint64_t)1ULL), t12);
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
        gm_tk9n3vlila9qg t21 = GM_LD(gm_tk9n3vlila9qg, ((uint8_t *)a1));
        GM_ST(gm_tk9n3vlila9qg, ((uint8_t *)t20 + t18 * 40), t21);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_52bt1f1(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_52bt1f1 F;
    void * gm_l0; /* l */
    gm_t6j6smj7qoctu gm_l1; /* x */
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
            gm_b16_8 t11 = gm_f_empty_slice_51k8cgl();
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
            gm_tftb1odc739t5 t13 = gm_f_regrow_9pv3nuf(t10, ((uint64_t)1ULL), t12);
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
        gm_t6j6smj7qoctu t21 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a1));
        GM_ST(gm_t6j6smj7qoctu, ((uint8_t *)t20 + t18 * 32), t21);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_ej78v89(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_ej78v89 F;
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
            gm_b16_8 t11 = gm_f_empty_slice_icu3qs9();
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
            gm_tftb1odc739t5 t13 = gm_f_regrow_pcenrov(t10, ((uint64_t)1ULL), t12);
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
        gm_tk9n3vlila9qg t21 = GM_LD(gm_tk9n3vlila9qg, ((uint8_t *)a1));
        GM_ST(gm_tk9n3vlila9qg, ((uint8_t *)t20 + t18 * 40), t21);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_f9bu01m(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_f9bu01m F;
    void * gm_l0; /* l */
    gm_t2s5j3n0guq0t gm_l1; /* x */
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
            gm_b16_8 t11 = gm_f_empty_slice_q5bjns8();
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
            gm_tftb1odc739t5 t13 = gm_f_regrow_ko1se61(t10, ((uint64_t)1ULL), t12);
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
        gm_t2s5j3n0guq0t t21 = GM_LD(gm_t2s5j3n0guq0t, ((uint8_t *)a1));
        GM_ST(gm_t2s5j3n0guq0t, ((uint8_t *)t20 + t18 * 24), t21);
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
        GM_ST(void *, (uint8_t *)&t32, (void *)(((uint8_t *)t25 + t26 * 24)));
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_t6e85n8_2(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_t6e85n8_2 F;
    void * gm_l0; /* l */
    gm_t6j6smj7qoctu gm_l1; /* x */
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
            gm_b16_8 t11 = gm_f_empty_slice_oldiiq9_2();
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
            gm_tftb1odc739t5 t13 = gm_f_regrow_967kt0a_2(t10, ((uint64_t)1ULL), t12);
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
        gm_t6j6smj7qoctu t21 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a1));
        GM_ST(gm_t6j6smj7qoctu, ((uint8_t *)t20 + t18 * 32), t21);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_t83rg2m(void * a0, uint8_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_t83rg2m F;
    void * gm_l0; /* l */
    uint8_t gm_l1; /* x */
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
            gm_b16_8 t11 = gm_f_empty_slice_ijtp9lo();
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
            gm_tftb1odc739t5 t13 = gm_f_regrow_3cp9o8d(t10, ((uint64_t)1ULL), t12);
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
        uint8_t t21 = gm_l1;
        GM_ST(uint8_t, ((uint8_t *)t20 + t18 * 1), t21);
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
        GM_ST(void *, (uint8_t *)&t32, (void *)(((uint8_t *)t25 + t26 * 1)));
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_3cp9o8d(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_3cp9o8d F;
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
        gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)1ULL), ((uint64_t)1ULL));
        uint8_t t9 = ((((uint8_t *)&t8))[0] != 0);
        F.ret = t8;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_967kt0a_2(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_967kt0a_2 F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_9pv3nuf(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_9pv3nuf F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_ko1se61(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_ko1se61 F;
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
        gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)24ULL), ((uint64_t)8ULL));
        uint8_t t9 = ((((uint8_t *)&t8))[0] != 0);
        F.ret = t8;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_o86m6d7_2(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_o86m6d7_2 F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_pcenrov(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_pcenrov F;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_release_mdh37fq(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_release_mdh37fq F;
    void * gm_l0; /* c */
    void * gm_l1; /* ar */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 0));
        gm_l1 = t3;
    }
    {
        void * t4 = gm_l1;
        gm_f_deinit_sejdpqv(t4);
    }
    {
        void * t5 = gm_l1;
        gm_b0_1 t6 = gm_f_destroy_602qeev((void *)NULL, cx, NULL, t5, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kh9bgce5chr)));
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
inline __attribute__((aligned(4))) void gm_f_reset_pb2jqrj(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_reset_pb2jqrj F;
    void * gm_l0; /* r */
    GM_STACK_CHECK_SHALLOW(&F, 56u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        void * t3 = (void *)(((uint8_t *)t2) + 48);
        gm_f_clear_3ibm3tg(t3);
    }
    {
        void * t4 = gm_l0;
        void * t5 = (void *)(((uint8_t *)t4) + 88);
        gm_f_clear_gp35vgp(t5);
    }
    {
        void * t6 = gm_l0;
        void * t7 = (void *)(((uint8_t *)t6) + 128);
        gm_f_clear_sl9fc3u(t7);
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_show_861qdlm(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_show_861qdlm F;
    gm_b16_8 gm_l0; /* text */
    GM_STACK_CHECK(&F, 41u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint64_t t3 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        uint8_t t4 = (t3 > ((uint64_t)0ULL));
        if (!t4) goto ie2;
        {
            gm_b16_8 t6 = gm_l0;
            gm_pf0nmuupvugn(cx, t6, gm_src3dhhc4laniae, 209, 23);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix2;
ie2:;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_shown_o0bbr93(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t9qkh0rsooc8d a1) {
    struct gm_fr_shown_o0bbr93 F;
    void * gm_l0; /* c */
    gm_t9qkh0rsooc8d gm_l1; /* format */
    GM_STACK_CHECK(&F, 138u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    void * t2 = GM_LD(void *, (((uint8_t *)t1) + 8));
    void * t3 = gm_l0;
    void * t4 = GM_LD(void *, (((uint8_t *)t3) + 16));
    void * t5 = gm_l0;
    uint32_t t6 = GM_LD(uint32_t, (((uint8_t *)t5) + 36));
    void * t7 = gm_l0;
    uint32_t t8 = GM_LD(uint32_t, (((uint8_t *)t7) + 40));
    void * t9 = gm_l0;
    gm_b16_8 t10 = GM_LD(gm_b16_8, (((uint8_t *)t9) + 48));
    gm_t9qkh0rsooc8d t11 = gm_l1;
    gm_b16_8 t12 = gm_f_render_region_ppot3lm((void *)NULL, cx, NULL, t2, t4, t6, t8, t10, t11);
    if (gm_unw.kind) {
        goto gm_lad;
    }
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
inline __attribute__((aligned(4))) uint8_t gm_f_starts_with_6vvt01e(gm_b16_8 a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_starts_with_6vvt01e F;
    gm_b16_8 gm_l0; /* s */
    gm_b16_8 gm_l1; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 126u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint64_t t3 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        uint8_t t5 = (t3 > t4);
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
        uint64_t t8 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        void * t9 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)));
        uint64_t t10 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t11 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint64_t t12 = (uintptr_t)t11;
        if (t10 > t12 || t12 > (uintptr_t)t8) gm_trap_slice(t10, t12, t8, gm_srcc24pnihhgkug, 416, 12);
        gm_b16_8 t13;
        GM_ST(void *, (uint8_t *)&t13, (void *)(((uint8_t *)t9 + t10 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t13 + 8, (uintptr_t)(t12 - t10));
        gm_b16_8 t14 = gm_l1;
        uint8_t t15 = (gm_eq0iqiubfm7e(((uint8_t *)&t13), ((uint8_t *)&t14)));
        uint8_t t16 = 0;
        F.ret = t15;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_submit_text_7dvfev6(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1) {
    struct gm_fr_submit_text_7dvfev6 F;
    void * gm_l0; /* r */
    gm_b16_8 gm_l1; /* text */
    GM_STACK_CHECK(&F, 128u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        gm_b16_8 t3 = gm_l1;
        gm_b16_8 t4 = gm_l1;
        gm_t9feugf5hcdtg t5;
        gm_f_classify_rm38cme((void *)&t5, (void *)NULL, cx, NULL, t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b0_1 t6 = gm_f_submit_pah0u77((void *)NULL, cx, NULL, t2, t3, &t5);
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
__attribute__((aligned(4))) uint8_t gm_f_command_7gn4uju(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1) {
    struct gm_fr_command_7gn4uju F;
    void * gm_l0; /* r */
    gm_b16_8 gm_l1; /* line */
    gm_b16_8 gm_l2; /* cmd */
    gm_b16_8 gm_l3; /* arg */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    uint64_t gm_l6; /* i */
    uint8_t gm_l7; /* c */
    GM_STACK_CHECK(&F, 477u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        gm_b16_8 t2 = gm_l1;
        gm_l2 = t2;
    }
    {
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sr4qbrk6tlgaa));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(0));
        gm_l3 = t3;
    }
    {
        {
            gm_b16_8 t5 = gm_l1;
            gm_l4 = t5;
        }
        {
            gm_l5 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t7 = gm_l5;
                uint64_t t8 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                uint8_t t9 = (t7 < t8);
                if (!t9) goto lb6;
                {
                    uint64_t t12 = gm_l5;
                    uint64_t t13 = gm_l5;
                    uint64_t t14 = (uintptr_t)t13;
                    uint64_t t15 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                    uint8_t t16 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t14 * 1));
                    gm_tf5srh4mp4b3i t17;
                    GM_ST(uint8_t, (uint8_t *)&t17 + 9, 0);
                    GM_ST(uint16_t, (uint8_t *)&t17 + 10, 0);
                    GM_ST(uint32_t, (uint8_t *)&t17 + 12, 0);
                    GM_ST(uint64_t, ((uint8_t *)&t17 + 0), t12);
                    GM_ST(uint8_t, ((uint8_t *)&t17 + 8), t16);
                    uint64_t t20 = GM_LD(uint64_t, (((uint8_t *)&t17) + 0));
                    gm_l6 = t20;
                    uint8_t t21 = GM_LD(uint8_t, (((uint8_t *)&t17) + 8));
                    gm_l7 = t21;
                    {
                        uint8_t t24 = gm_l7;
                        uint8_t t25 = gm_f_is_space_pq8dr45(t24);
                        if (!t25) goto ie23;
                        {
                            uint64_t t27 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                            void * t28 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)));
                            uint64_t t29 = (uintptr_t)((uint64_t)0ULL);
                            uint64_t t30 = gm_l6;
                            uint64_t t31 = (uintptr_t)t30;
                            if (t29 > t31 || t31 > (uintptr_t)t27) gm_trap_slice(t29, t31, t27, gm_src3dhhc4laniae, 454, 19);
                            gm_b16_8 t32;
                            GM_ST(void *, (uint8_t *)&t32, (void *)(((uint8_t *)t28 + t29 * 1)));
                            GM_ST(uintptr_t, (uint8_t *)&t32 + 8, (uintptr_t)(t31 - t29));
                            gm_l2 = t32;
                        }
                        {
                            uint64_t t33 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                            void * t34 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)));
                            uint64_t t35 = gm_l6;
                            uint64_t r36; bool o37 = __builtin_add_overflow(t35, ((uint64_t)1ULL), &r36);
                            if (o37) gm_trap("integer overflow in `+`", gm_src3dhhc4laniae, 455, 33);
                            uint64_t t38 = r36;
                            uint64_t t39 = (uintptr_t)t38;
                            if (t39 > (uintptr_t)t33 || (uintptr_t)t33 > (uintptr_t)t33) gm_trap_slice(t39, (uintptr_t)t33, t33, gm_src3dhhc4laniae, 455, 28);
                            gm_b16_8 t40;
                            GM_ST(void *, (uint8_t *)&t40, (void *)(((uint8_t *)t34 + t39 * 1)));
                            GM_ST(uintptr_t, (uint8_t *)&t40 + 8, (uintptr_t)((uintptr_t)t33 - t39));
                            gm_b16_8 t41 = gm_f_trim_fdrfe6i(t40);
                            gm_l3 = t41;
                        }
                        {
                            goto lb6;
                        }
                        goto ix23;
ie23:;
ix23:;
                    }
                    goto me18;
mn19:;
                    gm_trap("no match arm matched the scrutinee", gm_src3dhhc4laniae, 452, 5);
me18:;
                }
lc6:;
                {
                    uint64_t t43 = gm_l5;
                    uint64_t r44; bool o45 = __builtin_add_overflow(t43, ((uint64_t)1ULL), &r44);
                    uint64_t t46 = r44;
                    gm_l5 = t46;
                }
            }
lb6:;
        }
    }
    {
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8) == 5) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l2)), gm_sdg1ovmjdibb5, 5) == 0)) goto pa50;
        goto po49;
pa50:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8) == 2) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l2)), gm_sreprot2s508e, 2) == 0)) goto pa51;
        goto po49;
pa51:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8) == 5) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l2)), gm_sgk14ir7dv1gf, 5) == 0)) goto pa52;
        goto po49;
pa52:;
        goto mn48;
po49:;
        uint8_t t53 = 0;
        F.ret = ((uint8_t)0);
        goto gm_fret;
        goto me47;
mn48:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8) == 5) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l2)), gm_s527dqlmmaier, 5) == 0)) goto pa56;
        goto po55;
pa56:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8) == 2) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l2)), gm_s4uditudns916, 2) == 0)) goto pa57;
        goto po55;
pa57:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8) == 2) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l2)), gm_s6kq1mi4s1g58, 2) == 0)) goto pa58;
        goto po55;
pa58:;
        goto mn54;
po55:;
        gm_b16_8 t59 = GM_LD(gm_b16_8, gm_c5dv10uirua);
        gm_pfmaojsuds6a(cx, t59, gm_src3dhhc4laniae, 461, 32);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me47;
mn54:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8) == 6) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l2)), gm_s684ivkkpni41, 6) == 0)) goto mn60;
        void * t61 = gm_l0;
        gm_f_reset_pb2jqrj(t61);
        goto me47;
mn60:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8) == 5) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l2)), gm_ssfjmtpagd1pu, 5) == 0)) goto mn62;
        void * t63 = gm_l0;
        gm_b0_1 t64 = gm_f_list_session_6f2qqqv((void *)NULL, cx, NULL, t63);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me47;
mn62:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8) == 5) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l2)), gm_s0of2eragnise, 5) == 0)) goto pa67;
        goto po66;
pa67:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8) == 2) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l2)), gm_sn9nkqgu8isqo, 2) == 0)) goto pa68;
        goto po66;
pa68:;
        goto mn65;
po66:;
        {
            uint64_t t71 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
            uint8_t t72 = ((t71 == ((uint64_t)0ULL)));
            if (!t72) goto ie70;
            {
                if (gm_pf454qfrpt6r(cx, gm_sjsosmp24eqn0, 35, gm_src3dhhc4laniae, 465, 31)) {
                    goto gm_lad;
                }
            }
            goto ix70;
ie70:;
            {
                void * t75 = gm_l0;
                gm_b16_8 t76 = gm_l3;
                gm_b0_1 t77 = gm_f_type_of_b1a5sco((void *)NULL, cx, NULL, t75, t76);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
ix70:;
        }
        goto me47;
mn65:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8) == 5) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l2)), gm_snp0oqa4m79hk, 5) == 0)) goto mn78;
        {
            uint64_t t81 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
            uint8_t t82 = ((t81 == ((uint64_t)0ULL)));
            if (!t82) goto ie80;
            {
                if (gm_pf454qfrpt6r(cx, gm_s4nquq7iq8v5h, 28, gm_src3dhhc4laniae, 468, 31)) {
                    goto gm_lad;
                }
            }
            goto ix80;
ie80:;
            {
                {
                    void * t86 = gm_l0;
                    gm_b16_8 t87 = gm_l3;
                    uint8_t t88 = gm_f_load_file_3kdl78m((void *)NULL, cx, NULL, t86, t87);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
            }
ix80:;
        }
        goto me47;
mn78:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8) == 7) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l2)), gm_sho8l3vae133f, 7) == 0)) goto mn89;
        goto me47;
mn89:;
        gm_b16_8 t92 = gm_l2;
        if (gm_pf454qfrpt6r(cx, gm_s9pcjvamrhbn7, 24, gm_src3dhhc4laniae, 471, 14)) {
            goto gm_lad;
        }
        if ((GM_LD(uintptr_t, ((uint8_t *)&t92) + 8) != 0 && gm_pf454qfrpt6r(cx, GM_LD(void *, ((uint8_t *)&t92)), GM_LD(uintptr_t, ((uint8_t *)&t92) + 8), gm_src3dhhc4laniae, 471, 14))) {
            goto gm_lad;
        }
        if (gm_pf454qfrpt6r(cx, gm_sii9tamh3vah6, 31, gm_src3dhhc4laniae, 471, 14)) {
            goto gm_lad;
        }
        goto me47;
mn91:;
        gm_trap("no match arm matched the scrutinee", gm_src3dhhc4laniae, 459, 5);
me47:;
    }
    {
        uint8_t t93 = 0;
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
__attribute__((aligned(4))) uint8_t gm_f_command_run_06r7r92(void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_command_run_06r7r92 F;
    uint8_t gm_l2; /* interactive */
    uint8_t gm_l3; /* quiet */
    gm_b16_8 gm_l5; /* path */
    gm_tl572d7579hns gm_l7; /* next */
    gm_b16_8 gm_l8; /* l */
    gm_b16_8 gm_l9; /* line_text */
    gm_b16_8 gm_l10; /* line */
    gm_b16_8 gm_l12; /* text */
    GM_STACK_CHECK(&F, 947u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memcpy(&F.l0, a0, sizeof F.l0);
    {
        gm_tvcg39brotose t2 = F.l0;
        F.l1 = t2;
    }
    {
        uint8_t t3 = 0;
        int32_t t4 = gm_x_isatty_dhpcvep(((int32_t)0LL));
        uint8_t t5 = (!(t4 == ((int32_t)0LL)));
        if (t5) {
            int32_t t6 = gm_x_isatty_dhpcvep(((int32_t)1LL));
            uint8_t t7 = (!(t6 == ((int32_t)0LL)));
            t3 = t7;
        } else t3 = 0;
        gm_l2 = t3;
    }
    {
        uint8_t t8 = 0;
        uint8_t t9 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
        if (!t9) {
            uint8_t t10 = gm_l2;
            uint8_t t11 = (uint8_t)!t10;
            t8 = t11;
        } else t8 = 1;
        GM_ST(uint8_t, (((uint8_t *)&F.l1) + 0), t8);
    }
    {
        uint8_t t12 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
        gm_l3 = t12;
    }
    {
        gm_f_new_repl_hdsg2o4((void *)&F.l4, (void *)NULL, cx, NULL, &(*(gm_tvcg39brotose *)((uint8_t *)&F.l1)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        if (!(((((uint8_t *)&F.l1) + 8))[16] != 0)) goto ie13;
        gm_b16_8 t14 = GM_LD(gm_b16_8, (((uint8_t *)&F.l1) + 8));
        gm_l5 = t14;
        {
            void * t17 = (void *)((uint8_t *)&F.l4);
            gm_b16_8 t18 = gm_l5;
            uint8_t t19 = gm_f_load_file_3kdl78m((void *)NULL, cx, NULL, t17, t18);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t20 = (uint8_t)!t19;
            if (!t20) goto ie16;
            {
                uint8_t t22 = 0;
                F.ret = ((uint8_t)1ULL);
                goto gm_fret;
            }
            goto ix16;
ie16:;
ix16:;
        }
        goto ix13;
ie13:;
ix13:;
    }
    {
        uint8_t t24 = gm_l3;
        uint8_t t25 = (uint8_t)!t24;
        if (!t25) goto ie23;
        {
            gm_b16_8 t27 = GM_LD(gm_b16_8, (((uint8_t *)&F.l1) + 32));
            if (gm_pf1m4b8q4e78(cx, gm_su5tga7voctrl, 6, gm_src3dhhc4laniae, 517, 20)) {
                goto gm_lad;
            }
            if ((GM_LD(uintptr_t, ((uint8_t *)&t27) + 8) != 0 && gm_pf1m4b8q4e78(cx, GM_LD(void *, ((uint8_t *)&t27)), GM_LD(uintptr_t, ((uint8_t *)&t27) + 8), gm_src3dhhc4laniae, 517, 20))) {
                goto gm_lad;
            }
            if (gm_pf1m4b8q4e78(cx, gm_s7cu0m5qc7arr, 42, gm_src3dhhc4laniae, 517, 20)) {
                goto gm_lad;
            }
        }
        goto ix23;
ie23:;
ix23:;
    }
    {
        gm_f_list_uvfu1k1((void *)&F.l6, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        for (;;) {
            if (!((uint8_t)1)) goto lb28;
            {
                uint8_t t32 = gm_l3;
                uint8_t t33 = (uint8_t)!t32;
                if (!t33) goto ie31;
                {
                    gm_b16_8 t35;
                    uint64_t t37 = GM_LD(uintptr_t, (((uint8_t *)&F.l6) + 0) + 8);
                    uint8_t t38 = ((t37 == ((uint64_t)0ULL)));
                    if (!t38) goto ie36;
                    gm_b16_8 t40;
                    GM_ST(void *, (uint8_t *)&t40, (void *)(gm_s4hd92lokmtot));
                    GM_ST(uintptr_t, (uint8_t *)&t40 + 8, (uintptr_t)(3));
                    gm_b16_8 t41 = t40;
                    t35 = t41;
                    goto ix36;
ie36:;
                    gm_b16_8 t43;
                    GM_ST(void *, (uint8_t *)&t43, (void *)(gm_sfeb57kq1r2n4));
                    GM_ST(uintptr_t, (uint8_t *)&t43 + 8, (uintptr_t)(3));
                    gm_b16_8 t44 = t43;
                    t35 = t44;
ix36:;
                    gm_pfmaojsuds6a(cx, t35, gm_src3dhhc4laniae, 520, 24);
                    if (gm_unw.kind) {
                        goto gm_ll29;
                    }
                }
                goto ix31;
ie31:;
ix31:;
            }
            {
                gm_f_read_line_1oksar8((void *)&gm_l7, (void *)NULL, cx, NULL);
                if (gm_unw.kind) {
                    goto gm_ll29;
                }
            }
            {
                gm_b16_8 t45;
                uint8_t t51 = GM_LD(uint8_t, (((uint8_t *)&gm_l7) + 0));
                switch (t51) { case ((uint8_t)0ULL): goto ma49; case ((uint8_t)1ULL): goto ma47; case ((uint8_t)2ULL): goto ma48; default: goto ma50; }
ma47:;
                uint8_t t53 = GM_LD(uint8_t, (((uint8_t *)&gm_l7) + 0));
                if (t53 != ((uint8_t)1ULL)) goto mn52;
                {
                    uint64_t t56 = GM_LD(uintptr_t, (((uint8_t *)&F.l6) + 0) + 8);
                    uint8_t t57 = (t56 > ((uint64_t)0ULL));
                    if (!t57) goto ie55;
                    {
                        void * t59 = (void *)((uint8_t *)&F.l4);
                        gm_b16_8 t60 = GM_LD(gm_b16_8, (((uint8_t *)&F.l6) + 0));
                        gm_b16_8 t61 = GM_LD(gm_b16_8, ((uint8_t *)&t60));
                        gm_tabb710d9mq2e t62;
                        gm_f_dupe_g9iqpvn((void *)&t62, (void *)NULL, cx, NULL, t61, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_k093ni2ahjb)));
                        if (gm_unw.kind) {
                            goto gm_ll29;
                        }
                        if (((((uint8_t *)&t62))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t62) + 8, gm_src3dhhc4laniae, 524, 58);
                        gm_b16_8 t63 = GM_LD(gm_b16_8, (((uint8_t *)&t62) + 8));
                        gm_b16_8 t64 = GM_LD(gm_b16_8, ((uint8_t *)&t63));
                        gm_b0_1 t65 = gm_f_submit_text_7dvfev6((void *)NULL, cx, NULL, t59, t64);
                        if (gm_unw.kind) {
                            goto gm_ll29;
                        }
                    }
                    goto ix55;
ie55:;
ix55:;
                }
                goto lb28;
                goto me46;
mn52:;
ma48:;
                uint8_t t67 = GM_LD(uint8_t, (((uint8_t *)&gm_l7) + 0));
                if (t67 != ((uint8_t)2ULL)) goto mn66;
                {
                    if (gm_pf454qfrpt6r(cx, gm_s0iid7nkkvf30, 54, gm_src3dhhc4laniae, 528, 17)) {
                        goto gm_ll29;
                    }
                }
                goto lc28;
                goto me46;
mn66:;
ma49:;
                uint8_t t70 = GM_LD(uint8_t, (((uint8_t *)&gm_l7) + 0));
                if (t70 != ((uint8_t)0ULL)) goto mn69;
                gm_b16_8 t71 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l7) + 8));
                gm_l8 = t71;
                gm_b16_8 t72 = gm_l8;
                t45 = t72;
                goto me46;
mn69:;
ma50:;
                gm_trap("no match arm matched the scrutinee", gm_src3dhhc4laniae, 522, 22);
me46:;
                gm_l9 = t45;
            }
            {
                gm_b16_8 t73 = gm_l9;
                gm_b16_8 t74 = gm_f_trim_newlines_9rj3i73(t73);
                gm_l10 = t74;
            }
            {
                uint64_t t76 = GM_LD(uintptr_t, (((uint8_t *)&F.l6) + 0) + 8);
                uint8_t t77 = ((t76 == ((uint64_t)0ULL)));
                if (!t77) goto ie75;
                {
                    gm_b16_8 t80 = gm_l10;
                    gm_b16_8 t81 = gm_f_trim_start_179crak(t80);
                    gm_b16_8 t82;
                    GM_ST(void *, (uint8_t *)&t82, (void *)(gm_s39mljh669qgl));
                    GM_ST(uintptr_t, (uint8_t *)&t82 + 8, (uintptr_t)(1));
                    uint8_t t83 = gm_f_starts_with_6vvt01e(t81, t82);
                    if (!t83) goto ie79;
                    {
                        void * t86 = (void *)((uint8_t *)&F.l4);
                        gm_b16_8 t87 = gm_l10;
                        gm_b16_8 t88 = gm_f_trim_fdrfe6i(t87);
                        uint8_t t89 = gm_f_command_7gn4uju((void *)NULL, cx, NULL, t86, t88);
                        if (gm_unw.kind) {
                            goto gm_ll29;
                        }
                        uint8_t t90 = (uint8_t)!t89;
                        if (!t90) goto ie85;
                        {
                            goto lb28;
                        }
                        goto ix85;
ie85:;
ix85:;
                    }
                    {
                        goto lc28;
                    }
                    goto ix79;
ie79:;
ix79:;
                }
                {
                    gm_b16_8 t93 = gm_l10;
                    uint8_t t94 = gm_f_is_blank_asoolmf((void *)NULL, cx, NULL, t93);
                    if (gm_unw.kind) {
                        goto gm_ll29;
                    }
                    if (!t94) goto ie92;
                    {
                        goto lc28;
                    }
                    goto ix92;
ie92:;
ix92:;
                }
                goto ix75;
ie75:;
                gm_b16_8 t97 = gm_l10;
                gm_b16_8 t98 = gm_f_trim_fdrfe6i(t97);
                uint8_t t99 = (((GM_LD(uintptr_t, ((uint8_t *)&t98) + 8) == 7) && memcmp(GM_LD(void *, ((uint8_t *)&t98)), gm_sho8l3vae133f, 7) == 0));
                if (!t99) goto ie96;
                {
                    void * t101 = (void *)((uint8_t *)&F.l6);
                    gm_f_clear_h4369ui(t101);
                }
                {
                    goto lc28;
                }
                goto ix96;
ie96:;
ix96:;
ix75:;
            }
            {
                uint64_t t103 = GM_LD(uintptr_t, (((uint8_t *)&F.l6) + 0) + 8);
                uint8_t t104 = (t103 > ((uint64_t)0ULL));
                if (!t104) goto ie102;
                {
                    void * t106 = (void *)((uint8_t *)&F.l6);
                    gm_tftb1odc739t5 t107 = gm_f_push_t83rg2m(t106, ((uint8_t)10ULL));
                    if (((((uint8_t *)&t107))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t107) + 1, gm_src3dhhc4laniae, 544, 36);
                    gm_b0_1 t108 = GM_LD(gm_b0_1, (((uint8_t *)&t107) + 1));
                }
                goto ix102;
ie102:;
ix102:;
            }
            {
                void * t109 = (void *)((uint8_t *)&F.l6);
                gm_b16_8 t110 = gm_l10;
                gm_tftb1odc739t5 t111 = gm_f_extend_0cs85gs(t109, t110);
                if (((((uint8_t *)&t111))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t111) + 1, gm_src3dhhc4laniae, 545, 9);
                gm_b0_1 t112 = GM_LD(gm_b0_1, (((uint8_t *)&t111) + 1));
            }
            {
                gm_b16_8 t113 = GM_LD(gm_b16_8, (((uint8_t *)&F.l6) + 0));
                gm_b16_8 t114 = GM_LD(gm_b16_8, ((uint8_t *)&t113));
                gm_f_classify_rm38cme((void *)&F.l11, (void *)NULL, cx, NULL, t114);
                if (gm_unw.kind) {
                    goto gm_ll29;
                }
            }
            {
                uint8_t t116 = GM_LD(uint8_t, (((uint8_t *)&F.l11) + 56));
                if (!t116) goto ie115;
                uint8_t t117 = 0;
                gm_b16_8 t118 = gm_l10;
                gm_b16_8 t119 = gm_f_trim_fdrfe6i(t118);
                uint64_t t120 = GM_LD(uintptr_t, ((uint8_t *)&t119) + 8);
                uint8_t t121 = ((t120 == ((uint64_t)0ULL)));
                if (t121) {
                    gm_b16_8 t122 = GM_LD(gm_b16_8, (((uint8_t *)&F.l6) + 0));
                    gm_b16_8 t123 = GM_LD(gm_b16_8, ((uint8_t *)&t122));
                    uint8_t t124 = gm_f_open_brackets_hicehud((void *)NULL, cx, NULL, t123);
                    if (gm_unw.kind) {
                        goto gm_ll29;
                    }
                    uint8_t t125 = (uint8_t)!t124;
                    t117 = t125;
                } else t117 = 0;
                uint8_t t126 = (uint8_t)!t117;
                if (!t126) goto ie115;
                {
                    goto lc28;
                }
                goto ix115;
ie115:;
ix115:;
            }
            {
                gm_b16_8 t128 = GM_LD(gm_b16_8, (((uint8_t *)&F.l6) + 0));
                gm_b16_8 t129 = GM_LD(gm_b16_8, ((uint8_t *)&t128));
                gm_tabb710d9mq2e t130;
                gm_f_dupe_g9iqpvn((void *)&t130, (void *)NULL, cx, NULL, t129, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kif5rfegp72)));
                if (gm_unw.kind) {
                    goto gm_ll29;
                }
                if (((((uint8_t *)&t130))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t130) + 8, gm_src3dhhc4laniae, 549, 17);
                gm_b16_8 t131 = GM_LD(gm_b16_8, (((uint8_t *)&t130) + 8));
                gm_l12 = t131;
            }
            {
                void * t132 = (void *)((uint8_t *)&F.l6);
                gm_f_clear_h4369ui(t132);
            }
            {
                void * t133 = (void *)((uint8_t *)&F.l4);
                gm_b16_8 t134 = gm_l12;
                gm_b16_8 t135 = GM_LD(gm_b16_8, ((uint8_t *)&t134));
                gm_b0_1 t136 = gm_f_submit_pah0u77((void *)NULL, cx, NULL, t133, t135, &(*(gm_t9feugf5hcdtg *)((uint8_t *)&F.l11)));
                if (gm_unw.kind) {
                    goto gm_ll29;
                }
            }
lc28:;
        }
gm_ll29:;
        if (gm_unw.frame == &F.h && gm_unw.label == 0) {
            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb28; }
            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc28; }
        }
        goto gm_lad;
lb28:;
    }
    {
        uint8_t t137 = 0;
        F.ret = ((uint8_t)0ULL);
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
__attribute__((aligned(4))) void gm_f_compile_rj09gp7(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_b16_8 a2, gm_b16_8 a3, const void * a4, gm_b16_8 a5, uint8_t a6) {
    struct gm_fr_compile_rj09gp7 F;
    void * gm_l0; /* r */
    gm_th2lqbo77pvsc gm_l1; /* new_decls */
    gm_b16_8 gm_l2; /* replaced */
    gm_b16_8 gm_l3; /* replay */
    gm_b16_8 gm_l5; /* effects */
    uint8_t gm_l6; /* sentinel */
    gm_b16_8 gm_l8; /* <tmp> */
    uint64_t gm_l9; /* <tmp> */
    gm_t6j6smj7qoctu gm_l10; /* c */
    uint8_t gm_l11; /* skip */
    gm_b16_8 gm_l12; /* <tmp> */
    uint64_t gm_l13; /* <tmp> */
    gm_b16_8 gm_l14; /* n */
    gm_b16_8 gm_l15; /* <tmp> */
    uint64_t gm_l16; /* <tmp> */
    gm_b16_8 gm_l17; /* x */
    uint64_t gm_l18; /* lo */
    gm_b16_8 gm_l19; /* input */
    gm_b16_8 gm_l20; /* d */
    gm_b16_8 gm_l21; /* <tmp> */
    uint64_t gm_l22; /* <tmp> */
    gm_b16_8 gm_l23; /* t */
    GM_STACK_CHECK(&F, 1339u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&gm_l1, a1, sizeof gm_l1);
    gm_l2 = a2;
    gm_l3 = a3;
    memcpy(&F.l4, a4, sizeof F.l4);
    gm_l5 = a5;
    gm_l6 = a6;
    {
        gm_f_list_uvfu1k1((void *)&F.l7, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l7);
        gm_b16_8 t3 = GM_LD(gm_b16_8, gm_cnohofor2q6);
        gm_tftb1odc739t5 t4 = gm_f_extend_0cs85gs(t2, t3);
        if (((((uint8_t *)&t4))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t4) + 1, gm_src3dhhc4laniae, 146, 5);
        gm_b0_1 t5 = GM_LD(gm_b0_1, (((uint8_t *)&t4) + 1));
    }
    {
        {
            void * t7 = gm_l0;
            gm_b16_8 t8 = GM_LD(gm_b16_8, ((((uint8_t *)t7) + 48) + 0));
            gm_l8 = t8;
        }
        {
            gm_l9 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t10 = gm_l9;
                uint64_t t11 = GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8);
                uint8_t t12 = (t10 < t11);
                if (!t12) goto lb9;
                {
                    uint64_t t15 = gm_l9;
                    uint64_t t16 = (uintptr_t)t15;
                    uint64_t t17 = GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8);
                    gm_t6j6smj7qoctu t20 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l8)) + t16 * 32));
                    gm_l10 = t20;
                    {
                        gm_l11 = ((uint8_t)0);
                    }
                    {
                        {
                            gm_b16_8 t23 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l10) + 0));
                            gm_l12 = t23;
                        }
                        {
                            gm_l13 = ((uint64_t)0ULL);
                        }
                        {
                            for (;;) {
                                uint64_t t25 = gm_l13;
                                uint64_t t26 = GM_LD(uintptr_t, ((uint8_t *)&gm_l12) + 8);
                                uint8_t t27 = (t25 < t26);
                                if (!t27) goto lb24;
                                {
                                    uint64_t t30 = gm_l13;
                                    uint64_t t31 = (uintptr_t)t30;
                                    uint64_t t32 = GM_LD(uintptr_t, ((uint8_t *)&gm_l12) + 8);
                                    gm_b16_8 t35 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l12)) + t31 * 16));
                                    gm_l14 = t35;
                                    {
                                        {
                                            gm_b16_8 t38 = gm_l2;
                                            gm_l15 = t38;
                                        }
                                        {
                                            gm_l16 = ((uint64_t)0ULL);
                                        }
                                        {
                                            for (;;) {
                                                uint64_t t40 = gm_l16;
                                                uint64_t t41 = GM_LD(uintptr_t, ((uint8_t *)&gm_l15) + 8);
                                                uint8_t t42 = (t40 < t41);
                                                if (!t42) goto lb39;
                                                {
                                                    uint64_t t45 = gm_l16;
                                                    uint64_t t46 = (uintptr_t)t45;
                                                    uint64_t t47 = GM_LD(uintptr_t, ((uint8_t *)&gm_l15) + 8);
                                                    gm_b16_8 t50 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l15)) + t46 * 16));
                                                    gm_l17 = t50;
                                                    {
                                                        gm_b16_8 t53 = gm_l14;
                                                        gm_b16_8 t54 = gm_l17;
                                                        uint8_t t55 = (gm_eq0iqiubfm7e(((uint8_t *)&t53), ((uint8_t *)&t54)));
                                                        if (!t55) goto ie52;
                                                        {
                                                            gm_l11 = ((uint8_t)1);
                                                        }
                                                        goto ix52;
ie52:;
ix52:;
                                                    }
                                                    goto me48;
mn49:;
                                                    gm_trap("no match arm matched the scrutinee", gm_src3dhhc4laniae, 150, 13);
me48:;
                                                }
lc39:;
                                                {
                                                    uint64_t t58 = gm_l16;
                                                    uint64_t r59; bool o60 = __builtin_add_overflow(t58, ((uint64_t)1ULL), &r59);
                                                    uint64_t t61 = r59;
                                                    gm_l16 = t61;
                                                }
                                            }
lb39:;
                                        }
                                    }
                                    goto me33;
mn34:;
                                    gm_trap("no match arm matched the scrutinee", gm_src3dhhc4laniae, 149, 9);
me33:;
                                }
lc24:;
                                {
                                    uint64_t t63 = gm_l13;
                                    uint64_t r64; bool o65 = __builtin_add_overflow(t63, ((uint64_t)1ULL), &r64);
                                    uint64_t t66 = r64;
                                    gm_l13 = t66;
                                }
                            }
lb24:;
                        }
                    }
                    {
                        uint8_t t68 = gm_l11;
                        if (!t68) goto ie67;
                        {
                            goto lc9;
                        }
                        goto ix67;
ie67:;
ix67:;
                    }
                    {
                        void * t70 = (void *)((uint8_t *)&F.l7);
                        gm_b16_8 t71 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l10) + 16));
                        gm_tftb1odc739t5 t72 = gm_f_extend_0cs85gs(t70, t71);
                        if (((((uint8_t *)&t72))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t72) + 1, gm_src3dhhc4laniae, 155, 9);
                        gm_b0_1 t73 = GM_LD(gm_b0_1, (((uint8_t *)&t72) + 1));
                    }
                    {
                        void * t74 = (void *)((uint8_t *)&F.l7);
                        gm_b16_8 t75;
                        GM_ST(void *, (uint8_t *)&t75, (void *)(gm_s8dbdqj1sqibj));
                        GM_ST(uintptr_t, (uint8_t *)&t75 + 8, (uintptr_t)(2));
                        gm_tftb1odc739t5 t76 = gm_f_extend_0cs85gs(t74, t75);
                        if (((((uint8_t *)&t76))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t76) + 1, gm_src3dhhc4laniae, 156, 9);
                        gm_b0_1 t77 = GM_LD(gm_b0_1, (((uint8_t *)&t76) + 1));
                    }
                    goto me18;
mn19:;
                    gm_trap("no match arm matched the scrutinee", gm_src3dhhc4laniae, 147, 5);
me18:;
                }
lc9:;
                {
                    uint64_t t79 = gm_l9;
                    uint64_t r80; bool o81 = __builtin_add_overflow(t79, ((uint64_t)1ULL), &r80);
                    uint64_t t82 = r80;
                    gm_l9 = t82;
                }
            }
lb9:;
        }
    }
    {
        gm_l18 = ((uint64_t)0ULL);
    }
    {
        gm_b16_8 t83;
        GM_ST(void *, (uint8_t *)&t83, (void *)(gm_sr4qbrk6tlgaa));
        GM_ST(uintptr_t, (uint8_t *)&t83 + 8, (uintptr_t)(0));
        gm_l19 = t83;
    }
    {
        if (!((((uint8_t *)&gm_l1))[16] != 0)) goto ie84;
        gm_b16_8 t85 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l1));
        gm_l20 = t85;
        {
            uint64_t t87 = GM_LD(uintptr_t, (((uint8_t *)&F.l7) + 0) + 8);
            gm_l18 = t87;
        }
        {
            gm_b16_8 t88 = gm_l20;
            gm_l19 = t88;
        }
        {
            void * t89 = (void *)((uint8_t *)&F.l7);
            gm_b16_8 t90 = gm_l20;
            gm_tftb1odc739t5 t91 = gm_f_extend_0cs85gs(t89, t90);
            if (((((uint8_t *)&t91))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t91) + 1, gm_src3dhhc4laniae, 163, 9);
            gm_b0_1 t92 = GM_LD(gm_b0_1, (((uint8_t *)&t91) + 1));
        }
        {
            void * t93 = (void *)((uint8_t *)&F.l7);
            gm_b16_8 t94;
            GM_ST(void *, (uint8_t *)&t94, (void *)(gm_s8dbdqj1sqibj));
            GM_ST(uintptr_t, (uint8_t *)&t94 + 8, (uintptr_t)(2));
            gm_tftb1odc739t5 t95 = gm_f_extend_0cs85gs(t93, t94);
            if (((((uint8_t *)&t95))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t95) + 1, gm_src3dhhc4laniae, 164, 9);
            gm_b0_1 t96 = GM_LD(gm_b0_1, (((uint8_t *)&t95) + 1));
        }
        goto ix84;
ie84:;
ix84:;
    }
    {
        void * t97 = (void *)((uint8_t *)&F.l7);
        gm_b16_8 t98 = gm_l5;
        gm_tnpifv20irnph t99;
        GM_ST(gm_b16_8, ((uint8_t *)&t99 + 0), t98);
        gm_tftb1odc739t5 t100 = gm_f_write_0gtanrr(t97, t99);
        if (((((uint8_t *)&t100))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t100) + 1, gm_src3dhhc4laniae, 166, 5);
        gm_b0_1 t101 = GM_LD(gm_b0_1, (((uint8_t *)&t100) + 1));
    }
    {
        {
            gm_b16_8 t103 = gm_l3;
            gm_l21 = t103;
        }
        {
            gm_l22 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t105 = gm_l22;
                uint64_t t106 = GM_LD(uintptr_t, ((uint8_t *)&gm_l21) + 8);
                uint8_t t107 = (t105 < t106);
                if (!t107) goto lb104;
                {
                    uint64_t t110 = gm_l22;
                    uint64_t t111 = (uintptr_t)t110;
                    uint64_t t112 = GM_LD(uintptr_t, ((uint8_t *)&gm_l21) + 8);
                    gm_b16_8 t115 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l21)) + t111 * 16));
                    gm_l23 = t115;
                    {
                        void * t117 = (void *)((uint8_t *)&F.l7);
                        gm_b16_8 t118;
                        GM_ST(void *, (uint8_t *)&t118, (void *)(gm_som0q53jqvjh3));
                        GM_ST(uintptr_t, (uint8_t *)&t118 + 8, (uintptr_t)(4));
                        gm_tftb1odc739t5 t119 = gm_f_extend_0cs85gs(t117, t118);
                        if (((((uint8_t *)&t119))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t119) + 1, gm_src3dhhc4laniae, 168, 9);
                        gm_b0_1 t120 = GM_LD(gm_b0_1, (((uint8_t *)&t119) + 1));
                    }
                    {
                        void * t121 = (void *)((uint8_t *)&F.l7);
                        gm_b16_8 t122 = gm_l23;
                        gm_tftb1odc739t5 t123 = gm_f_extend_0cs85gs(t121, t122);
                        if (((((uint8_t *)&t123))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t123) + 1, gm_src3dhhc4laniae, 169, 9);
                        gm_b0_1 t124 = GM_LD(gm_b0_1, (((uint8_t *)&t123) + 1));
                    }
                    {
                        void * t125 = (void *)((uint8_t *)&F.l7);
                        gm_tftb1odc739t5 t126 = gm_f_push_t83rg2m(t125, ((uint8_t)10ULL));
                        if (((((uint8_t *)&t126))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t126) + 1, gm_src3dhhc4laniae, 170, 9);
                        gm_b0_1 t127 = GM_LD(gm_b0_1, (((uint8_t *)&t126) + 1));
                    }
                    goto me113;
mn114:;
                    gm_trap("no match arm matched the scrutinee", gm_src3dhhc4laniae, 167, 5);
me113:;
                }
lc104:;
                {
                    uint64_t t129 = gm_l22;
                    uint64_t r130; bool o131 = __builtin_add_overflow(t129, ((uint64_t)1ULL), &r130);
                    uint64_t t132 = r130;
                    gm_l22 = t132;
                }
            }
lb104:;
        }
    }
    {
        uint8_t t134 = gm_l6;
        if (!t134) goto ie133;
        uint64_t t135 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
        uint8_t t136 = (t135 > ((uint64_t)0ULL));
        if (!t136) goto ie133;
        {
            void * t138 = (void *)((uint8_t *)&F.l7);
            gm_t6j6smj7qoctu t139;
            gm_b16_8 t140 = GM_LD(gm_b16_8, gm_cdgbrlsjvrb);
            GM_ST(gm_b16_8, (uint8_t *)&t139 + 0, t140);
            gm_b16_8 t141 = GM_LD(gm_b16_8, gm_cdgbrlsjvrb);
            GM_ST(gm_b16_8, (uint8_t *)&t139 + 16, t141);
            gm_tftb1odc739t5 t142 = gm_f_write_nre2qb9(t138, &t139);
            if (((((uint8_t *)&t142))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t142) + 1, gm_src3dhhc4laniae, 173, 9);
            gm_b0_1 t143 = GM_LD(gm_b0_1, (((uint8_t *)&t142) + 1));
        }
        goto ix133;
ie133:;
ix133:;
    }
    {
        if (!((((uint8_t *)&F.l4))[48] != 0)) goto ie144;
        gm_tfeab279c16rr t145 = GM_LD(gm_tfeab279c16rr, ((uint8_t *)&F.l4));
        F.l24 = t145;
        {
            void * t147 = (void *)((uint8_t *)&F.l7);
            gm_b16_8 t148 = GM_LD(gm_b16_8, (((uint8_t *)&F.l24) + 0));
            gm_tftb1odc739t5 t149 = gm_f_extend_0cs85gs(t147, t148);
            if (((((uint8_t *)&t149))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t149) + 1, gm_src3dhhc4laniae, 176, 9);
            gm_b0_1 t150 = GM_LD(gm_b0_1, (((uint8_t *)&t149) + 1));
        }
        {
            uint64_t t151 = GM_LD(uintptr_t, (((uint8_t *)&F.l7) + 0) + 8);
            gm_l18 = t151;
        }
        {
            gm_b16_8 t152 = GM_LD(gm_b16_8, (((uint8_t *)&F.l24) + 16));
            gm_l19 = t152;
        }
        {
            void * t153 = (void *)((uint8_t *)&F.l7);
            gm_b16_8 t154 = GM_LD(gm_b16_8, (((uint8_t *)&F.l24) + 16));
            gm_tftb1odc739t5 t155 = gm_f_extend_0cs85gs(t153, t154);
            if (((((uint8_t *)&t155))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t155) + 1, gm_src3dhhc4laniae, 179, 9);
            gm_b0_1 t156 = GM_LD(gm_b0_1, (((uint8_t *)&t155) + 1));
        }
        {
            void * t157 = (void *)((uint8_t *)&F.l7);
            gm_b16_8 t158 = GM_LD(gm_b16_8, (((uint8_t *)&F.l24) + 32));
            gm_tftb1odc739t5 t159 = gm_f_extend_0cs85gs(t157, t158);
            if (((((uint8_t *)&t159))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t159) + 1, gm_src3dhhc4laniae, 180, 9);
            gm_b0_1 t160 = GM_LD(gm_b0_1, (((uint8_t *)&t159) + 1));
        }
        goto ix144;
ie144:;
ix144:;
    }
    {
        void * t161 = (void *)((uint8_t *)&F.l7);
        gm_b16_8 t162;
        GM_ST(void *, (uint8_t *)&t162, (void *)(gm_sikpqtja8pqpv));
        GM_ST(uintptr_t, (uint8_t *)&t162 + 8, (uintptr_t)(2));
        gm_tftb1odc739t5 t163 = gm_f_extend_0cs85gs(t161, t162);
        if (((((uint8_t *)&t163))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t163) + 1, gm_src3dhhc4laniae, 182, 5);
        gm_b0_1 t164 = GM_LD(gm_b0_1, (((uint8_t *)&t163) + 1));
    }
    {
        gm_b16_8 t165 = GM_LD(gm_b16_8, (((uint8_t *)&F.l7) + 0));
        gm_b16_8 t166 = GM_LD(gm_b16_8, ((uint8_t *)&t165));
        void * t167 = gm_l0;
        gm_t9qkh0rsooc8d t168 = GM_LD(gm_t9qkh0rsooc8d, ((((uint8_t *)t167) + 0) + 1));
        uint64_t t169 = gm_l18;
        uint32_t t170 = (uint32_t)t169;
        gm_b16_8 t171 = gm_l19;
        gm_f_check_source_nh9qcnk((void *)&F.l25, (void *)NULL, cx, NULL, t166, t168, t170, t171);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t172 = (void *)((uint8_t *)&F.l7);
        gm_f_deinit_r70pji5(t172);
    }
    {
        memmove(gm_out, ((uint8_t *)&F.l25), 64);
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
extern __attribute__((aligned(4))) uint8_t gm_f_is_space_pq8dr45(uint8_t a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_list_session_6f2qqqv(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_list_session_6f2qqqv F;
    void * gm_l0; /* r */
    gm_b16_8 gm_l1; /* <tmp> */
    uint64_t gm_l2; /* <tmp> */
    gm_t6j6smj7qoctu gm_l3; /* c */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    GM_STACK_CHECK(&F, 354u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        {
            void * t3 = gm_l0;
            gm_b16_8 t4 = GM_LD(gm_b16_8, ((((uint8_t *)t3) + 48) + 0));
            gm_l1 = t4;
        }
        {
            gm_l2 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t6 = gm_l2;
                uint64_t t7 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                uint8_t t8 = (t6 < t7);
                if (!t8) goto lb5;
                {
                    uint64_t t11 = gm_l2;
                    uint64_t t12 = (uintptr_t)t11;
                    uint64_t t13 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                    gm_t6j6smj7qoctu t16 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t12 * 32));
                    gm_l3 = t16;
                    {
                        gm_b16_8 t18 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 16));
                        if ((GM_LD(uintptr_t, ((uint8_t *)&t18) + 8) != 0 && gm_pf1m4b8q4e78(cx, GM_LD(void *, ((uint8_t *)&t18)), GM_LD(uintptr_t, ((uint8_t *)&t18) + 8), gm_src3dhhc4laniae, 442, 31))) {
                            goto gm_ll9;
                        }
                        if (gm_pf1m4b8q4e78(cx, gm_so514oj5nbhcu, 1, gm_src3dhhc4laniae, 442, 31)) {
                            goto gm_ll9;
                        }
                    }
                    goto me14;
mn15:;
                    gm_trap("no match arm matched the scrutinee", gm_src3dhhc4laniae, 442, 5);
me14:;
                }
lc5:;
                {
                    uint64_t t20 = gm_l2;
                    uint64_t r21; bool o22 = __builtin_add_overflow(t20, ((uint64_t)1ULL), &r21);
                    uint64_t t23 = r21;
                    gm_l2 = t23;
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
    {
        {
            void * t25 = gm_l0;
            gm_b16_8 t26 = GM_LD(gm_b16_8, ((((uint8_t *)t25) + 88) + 0));
            gm_l4 = t26;
        }
        {
            gm_l5 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t28 = gm_l5;
                uint64_t t29 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                uint8_t t30 = (t28 < t29);
                if (!t30) goto lb27;
                {
                    uint64_t t33 = gm_l5;
                    uint64_t t34 = (uintptr_t)t33;
                    uint64_t t35 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                    gm_tk9n3vlila9qg t38 = GM_LD(gm_tk9n3vlila9qg, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t34 * 40));
                    F.l6 = t38;
                    {
                        gm_b16_8 t40 = GM_LD(gm_b16_8, (((uint8_t *)&F.l6) + 0));
                        if ((GM_LD(uintptr_t, ((uint8_t *)&t40) + 8) != 0 && gm_pf1m4b8q4e78(cx, GM_LD(void *, ((uint8_t *)&t40)), GM_LD(uintptr_t, ((uint8_t *)&t40) + 8), gm_src3dhhc4laniae, 443, 31))) {
                            goto gm_ll31;
                        }
                        if (gm_pf1m4b8q4e78(cx, gm_so514oj5nbhcu, 1, gm_src3dhhc4laniae, 443, 31)) {
                            goto gm_ll31;
                        }
                    }
                    goto me36;
mn37:;
                    gm_trap("no match arm matched the scrutinee", gm_src3dhhc4laniae, 443, 5);
me36:;
                }
lc27:;
                {
                    uint64_t t42 = gm_l5;
                    uint64_t r43; bool o44 = __builtin_add_overflow(t42, ((uint64_t)1ULL), &r43);
                    uint64_t t45 = r43;
                    gm_l5 = t45;
                }
            }
gm_ll31:;
            if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb27; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc27; }
            }
            goto gm_lad;
lb27:;
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
__attribute__((aligned(4))) uint8_t gm_f_load_file_3kdl78m(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1) {
    struct gm_fr_load_file_3kdl78m F;
    void * gm_l0; /* r */
    gm_b16_8 gm_l1; /* path_text */
    gm_b16_8 gm_l2; /* path */
    gm_tj5jn79kq9pnm gm_l3; /* e */
    gm_b16_8 gm_l4; /* bytes */
    gm_b16_8 gm_l5; /* text */
    gm_b16_8 gm_l7; /* items */
    uint8_t gm_l9; /* has_main */
    gm_b16_8 gm_l10; /* <tmp> */
    uint64_t gm_l11; /* <tmp> */
    uint8_t gm_l13; /* is_main */
    gm_b16_8 gm_l14; /* <tmp> */
    uint64_t gm_l15; /* <tmp> */
    gm_b16_8 gm_l16; /* n */
    gm_b16_8 gm_l17; /* load_text */
    gm_b16_8 gm_l19; /* <tmp> */
    uint64_t gm_l20; /* <tmp> */
    gm_b16_8 gm_l22; /* msg */
    GM_STACK_CHECK(&F, 1202u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        gm_b16_8 t2 = gm_l1;
        gm_b16_8 t3 = gm_f_trim_fdrfe6i(t2);
        gm_l2 = t3;
    }
    {
        gm_b16_8 t4 = gm_l2;
        gm_tgvh15qshf643 t5;
        gm_f_read_all_r9rh9qd((void *)&t5, (void *)NULL, cx, NULL, t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b16_8 t6;
        if (!((((uint8_t *)&t5))[0] != 0)) {
            gm_b16_8 t7 = GM_LD(gm_b16_8, (((uint8_t *)&t5) + 8));
            t6 = t7;
        } else {
            gm_tj5jn79kq9pnm t8 = GM_LD(gm_tj5jn79kq9pnm, (((uint8_t *)&t5) + 8));
            gm_l3 = t8;
            {
                gm_b16_8 t10 = gm_l2;
                gm_tj5jn79kq9pnm t11 = gm_l3;
                gm_b16_8 t12 = gm_f_read_error_text_1lgi2bl((void *)NULL, cx, NULL, t11);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (gm_pf454qfrpt6r(cx, gm_s3vfg72f8aupk, 19, gm_src3dhhc4laniae, 406, 9)) {
                    goto gm_lad;
                }
                if ((GM_LD(uintptr_t, ((uint8_t *)&t10) + 8) != 0 && gm_pf454qfrpt6r(cx, GM_LD(void *, ((uint8_t *)&t10)), GM_LD(uintptr_t, ((uint8_t *)&t10) + 8), gm_src3dhhc4laniae, 406, 9))) {
                    goto gm_lad;
                }
                if (gm_pf454qfrpt6r(cx, gm_slgmqgkioa4u6, 2, gm_src3dhhc4laniae, 406, 9)) {
                    goto gm_lad;
                }
                if ((GM_LD(uintptr_t, ((uint8_t *)&t12) + 8) != 0 && gm_pf454qfrpt6r(cx, GM_LD(void *, ((uint8_t *)&t12)), GM_LD(uintptr_t, ((uint8_t *)&t12) + 8), gm_src3dhhc4laniae, 406, 9))) {
                    goto gm_lad;
                }
                if (gm_pf454qfrpt6r(cx, gm_so514oj5nbhcu, 1, gm_src3dhhc4laniae, 406, 9)) {
                    goto gm_lad;
                }
            }
            uint8_t t13 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        gm_l4 = t6;
    }
    {
        gm_b16_8 t15 = gm_l4;
        gm_b16_8 t16 = GM_LD(gm_b16_8, ((uint8_t *)&t15));
        uint8_t t17 = gm_f_utf8_valid_a8oc1t8(t16);
        uint8_t t18 = (uint8_t)!t17;
        if (!t18) goto ie14;
        {
            gm_b16_8 t20 = gm_l2;
            if (gm_pf454qfrpt6r(cx, gm_s3vfg72f8aupk, 19, gm_src3dhhc4laniae, 410, 9)) {
                goto gm_lad;
            }
            if ((GM_LD(uintptr_t, ((uint8_t *)&t20) + 8) != 0 && gm_pf454qfrpt6r(cx, GM_LD(void *, ((uint8_t *)&t20)), GM_LD(uintptr_t, ((uint8_t *)&t20) + 8), gm_src3dhhc4laniae, 410, 9))) {
                goto gm_lad;
            }
            if (gm_pf454qfrpt6r(cx, gm_s54emlnl8lmoe, 37, gm_src3dhhc4laniae, 410, 9)) {
                goto gm_lad;
            }
        }
        {
            uint8_t t21 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        goto ix14;
ie14:;
ix14:;
    }
    {
        gm_b16_8 t22 = gm_l4;
        gm_b16_8 t23 = GM_LD(gm_b16_8, ((uint8_t *)&t22));
        gm_l5 = t23;
    }
    {
        gm_b16_8 t24 = gm_l5;
        gm_f_classify_rm38cme((void *)&F.l6, (void *)NULL, cx, NULL, t24);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_th2lqbo77pvsc t25 = GM_LD(gm_th2lqbo77pvsc, (((uint8_t *)&F.l6) + 0));
        gm_b16_8 t26;
        if (((((uint8_t *)&t25))[16] != 0)) {
            gm_b16_8 t27 = GM_LD(gm_b16_8, (((uint8_t *)&t25) + 0));
            t26 = t27;
        } else {
            {
                gm_b16_8 t29 = gm_l5;
                void * t30 = gm_l0;
                gm_t9qkh0rsooc8d t31 = GM_LD(gm_t9qkh0rsooc8d, ((((uint8_t *)t30) + 0) + 2));
                gm_b16_8 t32 = gm_f_syntax_errors_fk23f5t((void *)NULL, cx, NULL, t29, t31);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_b0_1 t33 = gm_f_show_861qdlm((void *)NULL, cx, NULL, t32);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            uint8_t t34 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        gm_l7 = t26;
    }
    {
        gm_f_list_f0gjjbh_2((void *)&F.l8, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_l9 = ((uint8_t)0);
    }
    {
        {
            gm_b16_8 t36 = gm_l7;
            gm_l10 = t36;
        }
        {
            gm_l11 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t38 = gm_l11;
                uint64_t t39 = GM_LD(uintptr_t, ((uint8_t *)&gm_l10) + 8);
                uint8_t t40 = (t38 < t39);
                if (!t40) goto lb37;
                {
                    uint64_t t43 = gm_l11;
                    uint64_t t44 = (uintptr_t)t43;
                    uint64_t t45 = GM_LD(uintptr_t, ((uint8_t *)&gm_l10) + 8);
                    gm_tk9n3vlila9qg t48 = GM_LD(gm_tk9n3vlila9qg, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l10)) + t44 * 40));
                    F.l12 = t48;
                    {
                        gm_l13 = ((uint8_t)0);
                    }
                    {
                        {
                            gm_b16_8 t51 = GM_LD(gm_b16_8, (((uint8_t *)&F.l12) + 0));
                            gm_l14 = t51;
                        }
                        {
                            gm_l15 = ((uint64_t)0ULL);
                        }
                        {
                            for (;;) {
                                uint64_t t53 = gm_l15;
                                uint64_t t54 = GM_LD(uintptr_t, ((uint8_t *)&gm_l14) + 8);
                                uint8_t t55 = (t53 < t54);
                                if (!t55) goto lb52;
                                {
                                    uint64_t t58 = gm_l15;
                                    uint64_t t59 = (uintptr_t)t58;
                                    uint64_t t60 = GM_LD(uintptr_t, ((uint8_t *)&gm_l14) + 8);
                                    gm_b16_8 t63 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l14)) + t59 * 16));
                                    gm_l16 = t63;
                                    {
                                        uint8_t t66 = (((GM_LD(uintptr_t, ((uint8_t *)&gm_l16) + 8) == 4) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l16)), gm_sog4rksrmsqd2, 4) == 0));
                                        if (!t66) goto ie65;
                                        {
                                            gm_l13 = ((uint8_t)1);
                                        }
                                        goto ix65;
ie65:;
ix65:;
                                    }
                                    goto me61;
mn62:;
                                    gm_trap("no match arm matched the scrutinee", gm_src3dhhc4laniae, 423, 9);
me61:;
                                }
lc52:;
                                {
                                    uint64_t t69 = gm_l15;
                                    uint64_t r70; bool o71 = __builtin_add_overflow(t69, ((uint64_t)1ULL), &r70);
                                    uint64_t t72 = r70;
                                    gm_l15 = t72;
                                }
                            }
lb52:;
                        }
                    }
                    {
                        uint8_t t74 = gm_l13;
                        if (!t74) goto ie73;
                        {
                            gm_l9 = ((uint8_t)1);
                        }
                        goto ix73;
ie73:;
                        {
                            void * t77 = (void *)((uint8_t *)&F.l8);
                            gm_tftb1odc739t5 t78 = gm_f_push_1e500nc_2(t77, &(*(gm_tk9n3vlila9qg *)((uint8_t *)&F.l12)));
                            if (((((uint8_t *)&t78))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t78) + 1, gm_src3dhhc4laniae, 426, 47);
                            gm_b0_1 t79 = GM_LD(gm_b0_1, (((uint8_t *)&t78) + 1));
                        }
ix73:;
                    }
                    goto me46;
mn47:;
                    gm_trap("no match arm matched the scrutinee", gm_src3dhhc4laniae, 421, 5);
me46:;
                }
lc37:;
                {
                    uint64_t t81 = gm_l11;
                    uint64_t r82; bool o83 = __builtin_add_overflow(t81, ((uint64_t)1ULL), &r82);
                    uint64_t t84 = r82;
                    gm_l11 = t84;
                }
            }
lb37:;
        }
    }
    {
        gm_b16_8 t85 = gm_l5;
        gm_l17 = t85;
    }
    {
        uint8_t t87 = gm_l9;
        if (!t87) goto ie86;
        {
            gm_b16_8 t89 = gm_l2;
            if (gm_pf454qfrpt6r(cx, gm_sjbjet56e96co, 16, gm_src3dhhc4laniae, 430, 9)) {
                goto gm_lad;
            }
            if ((GM_LD(uintptr_t, ((uint8_t *)&t89) + 8) != 0 && gm_pf454qfrpt6r(cx, GM_LD(void *, ((uint8_t *)&t89)), GM_LD(uintptr_t, ((uint8_t *)&t89) + 8), gm_src3dhhc4laniae, 430, 9))) {
                goto gm_lad;
            }
            if (gm_pf454qfrpt6r(cx, gm_ssulg3jrm3q2v, 15, gm_src3dhhc4laniae, 430, 9)) {
                goto gm_lad;
            }
        }
        {
            gm_f_list_epvducg((void *)&F.l18, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            {
                gm_b16_8 t91 = GM_LD(gm_b16_8, (((uint8_t *)&F.l8) + 0));
                gm_l19 = t91;
            }
            {
                gm_l20 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t93 = gm_l20;
                    uint64_t t94 = GM_LD(uintptr_t, ((uint8_t *)&gm_l19) + 8);
                    uint8_t t95 = (t93 < t94);
                    if (!t95) goto lb92;
                    {
                        uint64_t t98 = gm_l20;
                        uint64_t t99 = (uintptr_t)t98;
                        uint64_t t100 = GM_LD(uintptr_t, ((uint8_t *)&gm_l19) + 8);
                        gm_tk9n3vlila9qg t103 = GM_LD(gm_tk9n3vlila9qg, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l19)) + t99 * 40));
                        F.l21 = t103;
                        {
                            void * t105 = (void *)((uint8_t *)&F.l18);
                            gm_b16_8 t106 = GM_LD(gm_b16_8, (((uint8_t *)&F.l21) + 16));
                            gm_tftb1odc739t5 t107 = gm_f_push_3ef746b(t105, t106);
                            if (((((uint8_t *)&t107))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t107) + 1, gm_src3dhhc4laniae, 432, 31);
                            gm_b0_1 t108 = GM_LD(gm_b0_1, (((uint8_t *)&t107) + 1));
                        }
                        goto me101;
mn102:;
                        gm_trap("no match arm matched the scrutinee", gm_src3dhhc4laniae, 432, 9);
me101:;
                    }
lc92:;
                    {
                        uint64_t t110 = gm_l20;
                        uint64_t r111; bool o112 = __builtin_add_overflow(t110, ((uint64_t)1ULL), &r111);
                        uint64_t t113 = r111;
                        gm_l20 = t113;
                    }
                }
lb92:;
            }
        }
        {
            gm_b16_8 t114 = GM_LD(gm_b16_8, (((uint8_t *)&F.l18) + 0));
            gm_b16_8 t115 = GM_LD(gm_b16_8, ((uint8_t *)&t114));
            gm_b16_8 t116;
            GM_ST(void *, (uint8_t *)&t116, (void *)(gm_s8dbdqj1sqibj));
            GM_ST(uintptr_t, (uint8_t *)&t116 + 8, (uintptr_t)(2));
            gm_tabb710d9mq2e t117;
            gm_f_join_sfknljb((void *)&t117, (void *)NULL, cx, NULL, t115, t116);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t117))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t117) + 8, gm_src3dhhc4laniae, 433, 21);
            gm_b16_8 t118 = GM_LD(gm_b16_8, (((uint8_t *)&t117) + 8));
            gm_b16_8 t119 = GM_LD(gm_b16_8, ((uint8_t *)&t118));
            gm_l17 = t119;
        }
        goto ix86;
ie86:;
ix86:;
    }
    {
        void * t120 = gm_l0;
        gm_b16_8 t121 = gm_l17;
        gm_b16_8 t122 = GM_LD(gm_b16_8, (((uint8_t *)&F.l8) + 0));
        gm_b16_8 t123 = GM_LD(gm_b16_8, ((uint8_t *)&t122));
        gm_th2lqbo77pvsc t124;
        gm_f_try_decls_23vcohd((void *)&t124, (void *)NULL, cx, NULL, t120, t121, t123);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b16_8 t125;
        if (((((uint8_t *)&t124))[16] != 0)) {
            gm_b16_8 t126 = GM_LD(gm_b16_8, (((uint8_t *)&t124) + 0));
            t125 = t126;
        } else {
            uint8_t t127 = 0;
            F.ret = ((uint8_t)1);
            goto gm_fret;
        }
        gm_l22 = t125;
    }
    {
        gm_b16_8 t128 = gm_l22;
        gm_b0_1 t129 = gm_f_show_861qdlm((void *)NULL, cx, NULL, t128);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        uint8_t t130 = 0;
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
extern __attribute__((aligned(4))) void gm_f_new_repl_hdsg2o4(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_parse_options_9st61k7(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_parse_options_9st61k7 F;
    gm_b16_8 gm_l0; /* args */
    uint64_t gm_l2; /* i */
    gm_b16_8 gm_l3; /* a */
    gm_b16_8 gm_l5; /* value */
    gm_b16_8 gm_l6; /* message */
    GM_STACK_CHECK(&F, 657u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        memset((uint8_t *)&F.l1 + 3, 0, 5);
        GM_ST(uint8_t, (uint8_t *)&F.l1 + 0, ((uint8_t)0));
        gm_t9qkh0rsooc8d t2 = GM_LD(gm_t9qkh0rsooc8d, gm_celpgpm1fi8);
        GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&F.l1 + 1, t2);
        gm_t9qkh0rsooc8d t3 = GM_LD(gm_t9qkh0rsooc8d, gm_ckbflbrril1);
        GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&F.l1 + 2, t3);
        gm_th2lqbo77pvsc t4;
        memset(&t4, 0, sizeof t4);
        GM_ST(gm_th2lqbo77pvsc, (uint8_t *)&F.l1 + 8, t4);
        gm_b16_8 t5 = GM_LD(gm_b16_8, gm_c7983l5vkhl);
        GM_ST(gm_b16_8, (uint8_t *)&F.l1 + 32, t5);
    }
    {
        gm_l2 = ((uint64_t)0ULL);
    }
    {
        for (;;) {
            uint64_t t7 = gm_l2;
            uint64_t t8 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            uint8_t t9 = (t7 < t8);
            if (!t9) goto lb6;
            {
                uint64_t t12 = gm_l2;
                uint64_t t13 = (uintptr_t)t12;
                uint64_t t14 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                if (t13 >= (uintptr_t)(t14)) gm_trap_index(t13, t14, gm_src3dhhc4laniae, 58, 14);
                gm_b16_8 t15 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t13 * 16));
                gm_l3 = t15;
            }
            {
                gm_b16_8 t17 = gm_l3;
                gm_t6j6smj7qoctu t18;
                gm_b16_8 t19;
                GM_ST(void *, (uint8_t *)&t19, (void *)(gm_s4chbmodjkinv));
                GM_ST(uintptr_t, (uint8_t *)&t19 + 8, (uintptr_t)(6));
                GM_ST(gm_b16_8, (uint8_t *)&t18 + 0, t19);
                gm_b16_8 t20;
                GM_ST(void *, (uint8_t *)&t20, (void *)(gm_s3a1oio07ri1p));
                GM_ST(uintptr_t, (uint8_t *)&t20 + 8, (uintptr_t)(2));
                GM_ST(gm_b16_8, (uint8_t *)&t18 + 16, t20);
                uint8_t t21 = 0;
                if (gm_eq0iqiubfm7e(((uint8_t *)&t18) + 0, ((uint8_t *)&t17))) t21 = 1;
                if (gm_eq0iqiubfm7e(((uint8_t *)&t18) + 16, ((uint8_t *)&t17))) t21 = 1;
                uint8_t t22 = (uint8_t)(t21);
                if (!t22) goto ie16;
                {
                    memset(&(*(gm_tdpp5715iet0a *)gm_out), 0, sizeof (*(gm_tdpp5715iet0a *)gm_out));
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tdpp5715iet0a *)gm_out) + 0, ((uint8_t)1ULL));
                    goto gm_fdone;
                }
                goto ix16;
ie16:;
ix16:;
            }
            {
                gm_b16_8 t24 = gm_l0;
                uint64_t t25 = gm_l2;
                gm_tepv5jklv667k t26;
                gm_f_scan_ll1gqkn((void *)&t26, (void *)NULL, cx, NULL, t24, t25, ((uint8_t)1));
                if (gm_unw.kind) {
                    goto gm_ll10;
                }
                uint8_t t32 = GM_LD(uint8_t, (((uint8_t *)&t26) + 0));
                switch (t32) { case ((uint8_t)0ULL): goto ma28; case ((uint8_t)1ULL): goto ma30; case ((uint8_t)2ULL): goto ma29; default: goto ma31; }
ma28:;
                uint8_t t34 = GM_LD(uint8_t, (((uint8_t *)&t26) + 0));
                if (t34 != ((uint8_t)0ULL)) goto mn33;
                F.l4 = (*(gm_t04fuf8kbk07t *)(((uint8_t *)&t26) + 8));
                {
                    gm_th2lqbo77pvsc t36 = GM_LD(gm_th2lqbo77pvsc, (((uint8_t *)&F.l4) + 8));
                    gm_b16_8 t37;
                    if (((((uint8_t *)&t36))[16] != 0)) {
                        gm_b16_8 t38 = GM_LD(gm_b16_8, (((uint8_t *)&t36) + 0));
                        t37 = t38;
                    } else {
                        gm_b16_8 t39;
                        GM_ST(void *, (uint8_t *)&t39, (void *)(gm_sr4qbrk6tlgaa));
                        GM_ST(uintptr_t, (uint8_t *)&t39 + 8, (uintptr_t)(0));
                        t37 = t39;
                    }
                    gm_l5 = t37;
                }
                {
                    uint8_t t46 = GM_LD(uint8_t, ((((uint8_t *)&F.l4) + 0) + 0));
                    switch (t46) { case ((uint8_t)0ULL): goto ma43; case ((uint8_t)1ULL): goto ma41; case ((uint8_t)2ULL): goto ma44; case ((uint8_t)3ULL): goto ma44; case ((uint8_t)4ULL): goto ma42; case ((uint8_t)5ULL): goto ma44; case ((uint8_t)6ULL): goto ma44; case ((uint8_t)7ULL): goto ma44; case ((uint8_t)8ULL): goto ma44; case ((uint8_t)9ULL): goto ma44; case ((uint8_t)10ULL): goto ma44; case ((uint8_t)11ULL): goto ma44; case ((uint8_t)12ULL): goto ma44; case ((uint8_t)13ULL): goto ma44; case ((uint8_t)14ULL): goto ma44; case ((uint8_t)15ULL): goto ma44; case ((uint8_t)16ULL): goto ma44; case ((uint8_t)17ULL): goto ma44; case ((uint8_t)18ULL): goto ma44; case ((uint8_t)19ULL): goto ma44; case ((uint8_t)20ULL): goto ma44; case ((uint8_t)21ULL): goto ma44; default: goto ma44; }
ma41:;
                    uint8_t t48 = GM_LD(uint8_t, ((((uint8_t *)&F.l4) + 0) + 0));
                    if (t48 != ((uint8_t)1ULL)) goto mn47;
                    {
                        GM_ST(uint8_t, (((uint8_t *)&F.l1) + 0), ((uint8_t)1));
                    }
                    goto me40;
mn47:;
ma42:;
                    uint8_t t51 = GM_LD(uint8_t, ((((uint8_t *)&F.l4) + 0) + 0));
                    if (t51 != ((uint8_t)4ULL)) goto mn50;
                    {
                        gm_b16_8 t53 = gm_l5;
                        gm_t9qkh0rsooc8d t54 = gm_f_mode_of_pq04he2(t53);
                        GM_ST(gm_t9qkh0rsooc8d, (((uint8_t *)&F.l1) + 1), t54);
                    }
                    goto me40;
mn50:;
ma43:;
                    uint8_t t56 = GM_LD(uint8_t, ((((uint8_t *)&F.l4) + 0) + 0));
                    if (t56 != ((uint8_t)0ULL)) goto mn55;
                    {
                        gm_b16_8 t58 = gm_l5;
                        gm_t9qkh0rsooc8d t59 = gm_f_format_of_nbvl1cj(t58);
                        GM_ST(gm_t9qkh0rsooc8d, (((uint8_t *)&F.l1) + 2), t59);
                    }
                    goto me40;
mn55:;
ma44:;
                    goto me40;
mn60:;
ma45:;
                    gm_trap("no match arm matched the scrutinee", gm_src3dhhc4laniae, 63, 17);
me40:;
                }
                {
                    uint64_t t62 = GM_LD(uint64_t, (((uint8_t *)&F.l4) + 32));
                    gm_l2 = t62;
                }
                {
                    goto lc6;
                }
                goto me27;
mn33:;
ma29:;
                uint8_t t64 = GM_LD(uint8_t, (((uint8_t *)&t26) + 0));
                if (t64 != ((uint8_t)2ULL)) goto mn63;
                gm_b16_8 t65 = GM_LD(gm_b16_8, (((uint8_t *)&t26) + 8));
                gm_l6 = t65;
                gm_b16_8 t66 = gm_l6;
                memset(&(*(gm_tdpp5715iet0a *)gm_out), 0, sizeof (*(gm_tdpp5715iet0a *)gm_out));
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tdpp5715iet0a *)gm_out) + 0, ((uint8_t)2ULL));
                GM_ST(gm_b16_8, (uint8_t *)&(*(gm_tdpp5715iet0a *)gm_out) + 8, t66);
                goto gm_fdone;
                goto me27;
mn63:;
ma30:;
                uint8_t t68 = GM_LD(uint8_t, (((uint8_t *)&t26) + 0));
                if (t68 != ((uint8_t)1ULL)) goto mn67;
                goto me27;
mn67:;
ma31:;
                gm_trap("no match arm matched the scrutinee", gm_src3dhhc4laniae, 60, 9);
me27:;
            }
            {
                uint64_t t70 = gm_l2;
                uint64_t r71; bool o72 = __builtin_add_overflow(t70, ((uint64_t)1ULL), &r71);
                if (o72) gm_trap("integer overflow in `+`", gm_src3dhhc4laniae, 75, 9);
                uint64_t t73 = r71;
                gm_l2 = t73;
            }
            {
                gm_b16_8 t75 = gm_l3;
                gm_b16_8 t76;
                GM_ST(void *, (uint8_t *)&t76, (void *)(gm_skolmnfc0ccbd));
                GM_ST(uintptr_t, (uint8_t *)&t76 + 8, (uintptr_t)(1));
                uint8_t t77 = gm_f_starts_with_6vvt01e(t75, t76);
                if (!t77) goto ie74;
                {
                    gm_b16_8 t79 = gm_l3;
                    gm_tnpifv20irnph t80;
                    GM_ST(gm_b16_8, ((uint8_t *)&t80 + 0), t79);
                    gm_tabb710d9mq2e t81;
                    gm_f_format_j0ggm04((void *)&t81, (void *)NULL, cx, NULL, t80);
                    if (gm_unw.kind) {
                        goto gm_ll10;
                    }
                    if (((((uint8_t *)&t81))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t81) + 8, gm_src3dhhc4laniae, 76, 45);
                    gm_b16_8 t82 = GM_LD(gm_b16_8, (((uint8_t *)&t81) + 8));
                    gm_b16_8 t83 = GM_LD(gm_b16_8, ((uint8_t *)&t82));
                    memset(&(*(gm_tdpp5715iet0a *)gm_out), 0, sizeof (*(gm_tdpp5715iet0a *)gm_out));
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tdpp5715iet0a *)gm_out) + 0, ((uint8_t)2ULL));
                    GM_ST(gm_b16_8, (uint8_t *)&(*(gm_tdpp5715iet0a *)gm_out) + 8, t83);
                    goto gm_fdone;
                }
                goto ix74;
ie74:;
ix74:;
            }
            {
                uint8_t t85;
                if ((((((uint8_t *)&F.l1) + 8))[16] != 0)) goto mn87;
                t85 = ((uint8_t)0);
                goto me86;
mn87:;
                t85 = ((uint8_t)1);
                goto me86;
mn88:;
                gm_trap("no match arm matched the scrutinee", gm_src3dhhc4laniae, 77, 12);
me86:;
                if (!t85) goto ie84;
                {
                    gm_b16_8 t90;
                    GM_ST(void *, (uint8_t *)&t90, (void *)(gm_slrfv83pestqp));
                    GM_ST(uintptr_t, (uint8_t *)&t90 + 8, (uintptr_t)(30));
                    memset(&(*(gm_tdpp5715iet0a *)gm_out), 0, sizeof (*(gm_tdpp5715iet0a *)gm_out));
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tdpp5715iet0a *)gm_out) + 0, ((uint8_t)2ULL));
                    GM_ST(gm_b16_8, (uint8_t *)&(*(gm_tdpp5715iet0a *)gm_out) + 8, t90);
                    goto gm_fdone;
                }
                goto ix84;
ie84:;
ix84:;
            }
            {
                gm_b16_8 t91 = gm_l3;
                GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)(((uint8_t *)&F.l1) + 8)) + 17, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)(((uint8_t *)&F.l1) + 8)) + 18, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)(((uint8_t *)&F.l1) + 8)) + 20, 0);
                ((uint8_t *)&(*(gm_th2lqbo77pvsc *)(((uint8_t *)&F.l1) + 8)))[16] = 1;
                GM_ST(gm_b16_8, (uint8_t *)&(*(gm_th2lqbo77pvsc *)(((uint8_t *)&F.l1) + 8)) + 0, t91);
            }
lc6:;
        }
gm_ll10:;
        if (gm_unw.frame == &F.h && gm_unw.label == 0) {
            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb6; }
            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc6; }
        }
        goto gm_lad;
lb6:;
    }
    {
        const uint8_t *t92 = ((uint8_t *)&F.l1);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tdpp5715iet0a *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tdpp5715iet0a *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tdpp5715iet0a *)gm_out) + 4, 0);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tdpp5715iet0a *)gm_out) + 0, ((uint8_t)0ULL));
        memmove((uint8_t *)&(*(gm_tdpp5715iet0a *)gm_out) + 8, t92, 48);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_read_error_text_1lgi2bl(void *env, gm_node *cx, gm_node *inst, gm_tj5jn79kq9pnm a0) {
    struct gm_fr_read_error_text_1lgi2bl F;
    gm_tj5jn79kq9pnm gm_l0; /* e */
    int32_t gm_l1; /* code */
    GM_STACK_CHECK(&F, 105u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
        if (t3 != ((uint8_t)0ULL)) goto ie2;
        uint8_t t4 = GM_LD(uint8_t, ((((uint8_t *)&gm_l0) + 4) + 0));
        if (t4 != ((uint8_t)13ULL)) goto ie2;
        int32_t t5 = GM_LD(int32_t, ((((uint8_t *)&gm_l0) + 4) + 4));
        gm_l1 = t5;
        {
            int32_t t8 = gm_l1;
            uint8_t t9 = ((t8 == ((int32_t)21LL)));
            if (!t9) goto ie7;
            {
                gm_b16_8 t11;
                GM_ST(void *, (uint8_t *)&t11, (void *)(gm_s3qd7ldqb9526));
                GM_ST(uintptr_t, (uint8_t *)&t11 + 8, (uintptr_t)(28));
                uint8_t t12 = 0;
                F.ret = t11;
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
        gm_tj5jn79kq9pnm t13 = gm_l0;
        gm_b16_8 t14 = gm_f_describe_read_error_uikkr59((void *)NULL, cx, NULL, t13);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t15 = 0;
        F.ret = t14;
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
inline __attribute__((aligned(4))) void gm_f_read_line_1oksar8(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_read_line_1oksar8 F;
    gm_tivanu2mfcv6o gm_l1; /* one */
    uint64_t gm_l2; /* n */
    GM_STACK_CHECK(&F, 178u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l0, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
    }
    {
        for (;;) {
            if (!((uint8_t)1)) goto lb2;
            {
                void * t5 = ((uint8_t *)&gm_l1);
                if ((uintptr_t)0 > (uintptr_t)1 || (uintptr_t)1 > (uintptr_t)1) gm_trap_slice((uintptr_t)0, (uintptr_t)1, 1, gm_src3dhhc4laniae, 490, 22);
                gm_b16_8 t6;
                GM_ST(void *, (uint8_t *)&t6, (void *)(((uint8_t *)t5 + (uintptr_t)0 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t6 + 8, (uintptr_t)((uintptr_t)1 - (uintptr_t)0));
                gm_node *pn7 = gm_find(cx, 1018230276);
                if (!pn7) gm_trap("no handler installed for effect `In`", gm_src3dhhc4laniae, 490, 14);
                gm_thf5kr0is9u9i t8;
                t8 = ((gm_thf5kr0is9u9i (*)(void *, gm_node *, gm_node *, gm_b16_8))pn7->clauses[0])(pn7->env, pn7->hctx, pn7, t6);
                if (gm_unw.kind) {
                    goto gm_ll3;
                }
                uint64_t t9;
                if (!((((uint8_t *)&t8))[0] != 0)) {
                    uint64_t t10 = GM_LD(uint64_t, (((uint8_t *)&t8) + 8));
                    t9 = t10;
                } else {
                    t9 = ((uint64_t)0ULL);
                }
                gm_l2 = t9;
            }
            {
                uint64_t t12 = gm_l2;
                uint8_t t13 = ((t12 == ((uint64_t)0ULL)));
                if (!t13) goto ie11;
                {
                    goto lb2;
                }
                goto ix11;
ie11:;
ix11:;
            }
            {
                void * t15 = (void *)((uint8_t *)&F.l0);
                uint64_t t16 = (uintptr_t)((uint64_t)0ULL);
                if (t16 >= (uintptr_t)(1)) gm_trap_index(t16, 1, gm_src3dhhc4laniae, 492, 20);
                uint8_t t17 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + t16 * 1));
                gm_tftb1odc739t5 t18 = gm_f_push_t83rg2m(t15, t17);
                if (((((uint8_t *)&t18))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t18) + 1, gm_src3dhhc4laniae, 492, 9);
                gm_b0_1 t19 = GM_LD(gm_b0_1, (((uint8_t *)&t18) + 1));
            }
            {
                uint64_t t21 = (uintptr_t)((uint64_t)0ULL);
                if (t21 >= (uintptr_t)(1)) gm_trap_index(t21, 1, gm_src3dhhc4laniae, 493, 12);
                uint8_t t22 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + t21 * 1));
                uint8_t t23 = ((t22 == ((uint8_t)10ULL)));
                if (!t23) goto ie20;
                {
                    goto lb2;
                }
                goto ix20;
ie20:;
ix20:;
            }
lc2:;
        }
gm_ll3:;
        if (gm_unw.frame == &F.h && gm_unw.label == 0) {
            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb2; }
            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc2; }
        }
        goto gm_lad;
lb2:;
    }
    {
        uint64_t t26 = GM_LD(uintptr_t, (((uint8_t *)&F.l0) + 0) + 8);
        uint8_t t27 = ((t26 == ((uint64_t)0ULL)));
        if (!t27) goto ie25;
        {
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 8, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 16, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 0, ((uint8_t)1ULL));
            goto gm_fdone;
        }
        goto ix25;
ie25:;
ix25:;
    }
    {
        gm_b16_8 t30 = GM_LD(gm_b16_8, (((uint8_t *)&F.l0) + 0));
        gm_b16_8 t31 = GM_LD(gm_b16_8, ((uint8_t *)&t30));
        uint8_t t32 = gm_f_utf8_valid_a8oc1t8(t31);
        uint8_t t33 = (uint8_t)!t32;
        if (!t33) goto ie29;
        {
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 8, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 16, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 0, ((uint8_t)2ULL));
            goto gm_fdone;
        }
        goto ix29;
ie29:;
ix29:;
    }
    {
        gm_b16_8 t35 = GM_LD(gm_b16_8, (((uint8_t *)&F.l0) + 0));
        gm_b16_8 t36 = GM_LD(gm_b16_8, ((uint8_t *)&t35));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 4, 0);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 0, ((uint8_t)0ULL));
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 8, t36);
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
inline __attribute__((aligned(4))) void gm_f_renamed_vars_fho5hvj(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_renamed_vars_fho5hvj F;
    void * gm_l0; /* r */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    gm_t6j6smj7qoctu gm_l4; /* v */
    GM_STACK_CHECK(&F, 220u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_e6ie1fq_2((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
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
                    gm_t6j6smj7qoctu t16 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t12 * 32));
                    gm_l4 = t16;
                    {
                        gm_b16_8 t19 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l4) + 0));
                        gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l4) + 16));
                        uint8_t t21 = (!gm_eq0iqiubfm7e(((uint8_t *)&t19), ((uint8_t *)&t20)));
                        if (!t21) goto ie18;
                        {
                            void * t23 = (void *)((uint8_t *)&F.l1);
                            gm_tftb1odc739t5 t24 = gm_f_push_t6e85n8_2(t23, &(*(gm_t6j6smj7qoctu *)((uint8_t *)&gm_l4)));
                            if (((((uint8_t *)&t24))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t24) + 1, gm_src3dhhc4laniae, 202, 39);
                            gm_b0_1 t25 = GM_LD(gm_b0_1, (((uint8_t *)&t24) + 1));
                        }
                        goto ix18;
ie18:;
ix18:;
                    }
                    goto me14;
mn15:;
                    gm_trap("no match arm matched the scrutinee", gm_src3dhhc4laniae, 201, 5);
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
        memmove(gm_out, ((uint8_t *)&F.l1), 40);
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
inline __attribute__((aligned(4))) void gm_f_replay_texts_3te9pi7(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1) {
    struct gm_fr_replay_texts_3te9pi7 F;
    void * gm_l0; /* r */
    uint8_t gm_l1; /* pure_only */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    GM_STACK_CHECK(&F, 182u);
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
        {
            void * t3 = gm_l0;
            gm_b16_8 t4 = GM_LD(gm_b16_8, ((((uint8_t *)t3) + 88) + 0));
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
                    gm_tk9n3vlila9qg t16 = GM_LD(gm_tk9n3vlila9qg, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t12 * 40));
                    F.l5 = t16;
                    {
                        uint8_t t19 = gm_l1;
                        if (!t19) goto ie18;
                        uint8_t t20 = GM_LD(uint8_t, (((uint8_t *)&F.l5) + 32));
                        if (!t20) goto ie18;
                        {
                            goto lc5;
                        }
                        goto ix18;
ie18:;
ix18:;
                    }
                    {
                        void * t22 = (void *)((uint8_t *)&F.l2);
                        gm_b16_8 t23 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 16));
                        gm_tftb1odc739t5 t24 = gm_f_push_3ef746b(t22, t23);
                        if (((((uint8_t *)&t24))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t24) + 1, gm_src3dhhc4laniae, 193, 9);
                        gm_b0_1 t25 = GM_LD(gm_b0_1, (((uint8_t *)&t24) + 1));
                    }
                    goto me14;
mn15:;
                    gm_trap("no match arm matched the scrutinee", gm_src3dhhc4laniae, 191, 5);
me14:;
                }
lc5:;
                {
                    uint64_t t27 = gm_l4;
                    uint64_t r28; bool o29 = __builtin_add_overflow(t27, ((uint64_t)1ULL), &r28);
                    uint64_t t30 = r28;
                    gm_l4 = t30;
                }
            }
lb5:;
        }
    }
    {
        memmove(gm_out, ((uint8_t *)&F.l2), 40);
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
extern __attribute__((aligned(4))) void gm_f_reset_pb2jqrj(void * a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_set_var_pjsp5eq(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2) {
    struct gm_fr_set_var_pjsp5eq F;
    void * gm_l0; /* vars */
    gm_b16_8 gm_l1; /* name */
    gm_b16_8 gm_l2; /* internal */
    uint64_t gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    uint64_t gm_l5; /* i */
    GM_STACK_CHECK(&F, 317u);
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
                        if (t18 >= (uintptr_t)(t20)) gm_trap_index(t18, t20, gm_src3dhhc4laniae, 361, 12);
                        gm_b16_8 t21 = GM_LD(gm_b16_8, (((uint8_t *)GM_LD(void *, (((uint8_t *)t19) + 0)) + t18 * 32) + 0));
                        gm_b16_8 t22 = gm_l1;
                        uint8_t t23 = (gm_eq0iqiubfm7e(((uint8_t *)&t21), ((uint8_t *)&t22)));
                        if (!t23) goto ie16;
                        {
                            uint64_t t25 = gm_l5;
                            uint64_t t26 = (uintptr_t)t25;
                            void * t27 = gm_l0;
                            uint64_t t28 = GM_LD(uintptr_t, (((uint8_t *)t27) + 0) + 8);
                            if (t26 >= (uintptr_t)(t28)) gm_trap_index(t26, t28, gm_src3dhhc4laniae, 362, 13);
                            gm_b16_8 t29 = gm_l2;
                            GM_ST(gm_b16_8, (((uint8_t *)GM_LD(void *, (((uint8_t *)t27) + 0)) + t26 * 32) + 16), t29);
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
                    gm_trap("no match arm matched the scrutinee", gm_src3dhhc4laniae, 360, 5);
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
        gm_t6j6smj7qoctu t38;
        gm_b16_8 t39 = gm_l1;
        GM_ST(gm_b16_8, (uint8_t *)&t38 + 0, t39);
        gm_b16_8 t40 = gm_l2;
        GM_ST(gm_b16_8, (uint8_t *)&t38 + 16, t40);
        gm_tftb1odc739t5 t41 = gm_f_push_t6e85n8_2(t37, &t38);
        if (((((uint8_t *)&t41))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t41) + 1, gm_src3dhhc4laniae, 366, 5);
        gm_b0_1 t42 = GM_LD(gm_b0_1, (((uint8_t *)&t41) + 1));
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) gm_b0_1 gm_f_show_861qdlm(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) uint8_t gm_f_statements_dafpns9(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, const void * a2) {
    struct gm_fr_statements_dafpns9 F;
    void * gm_l0; /* r */
    gm_b16_8 gm_l1; /* text */
    gm_th2lqbo77pvsc gm_l2; /* info */
    gm_b16_8 gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    gm_t5hdnnftkup6m gm_l7; /* b */
    gm_b16_8 gm_l8; /* internal */
    gm_b16_8 gm_l10; /* body */
    uint8_t gm_l11; /* changed */
    gm_b16_8 gm_l12; /* <tmp> */
    uint64_t gm_l13; /* <tmp> */
    gm_t2s5j3n0guq0t gm_l14; /* d */
    uint8_t gm_l19; /* value */
    uint8_t gm_l22; /* unused */
    void * gm_l23; /* shown_c */
    gm_th2lqbo77pvsc gm_l24; /* kept */
    uint8_t gm_l25; /* impure */
    gm_th2lqbo77pvsc gm_l26; /* kept_text */
    gm_b16_8 gm_l27; /* k */
    uint8_t gm_l31; /* ok */
    gm_b16_8 gm_l32; /* k */
    gm_b16_8 gm_l34; /* <tmp> */
    uint64_t gm_l35; /* <tmp> */
    gm_t5hdnnftkup6m gm_l36; /* b */
    gm_b16_8 gm_l37; /* joined */
    GM_STACK_CHECK(&F, 3095u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        gm_f_list_glg30gi((void *)&F.l3, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_f_list_e6ie1fq_2((void *)&F.l4, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l4);
        void * t3 = gm_l0;
        gm_b16_8 t4 = GM_LD(gm_b16_8, ((((uint8_t *)t3) + 128) + 0));
        gm_b16_8 t5 = GM_LD(gm_b16_8, ((uint8_t *)&t4));
        gm_tftb1odc739t5 t6 = gm_f_extend_e1j40m3(t2, t5);
        if (((((uint8_t *)&t6))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t6) + 1, gm_src3dhhc4laniae, 273, 5);
        gm_b0_1 t7 = GM_LD(gm_b0_1, (((uint8_t *)&t6) + 1));
    }
    {
        {
            gm_b16_8 t9 = GM_LD(gm_b16_8, (((uint8_t *)a2) + 0));
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
                    uint64_t t17 = (uintptr_t)t16;
                    uint64_t t18 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                    gm_t5hdnnftkup6m t21 = GM_LD(gm_t5hdnnftkup6m, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l5)) + t17 * 24));
                    gm_l7 = t21;
                    {
                        uint8_t t24 = (((GM_LD(uintptr_t, (((uint8_t *)&gm_l7) + 0) + 8) == 1) && memcmp(GM_LD(void *, (((uint8_t *)&gm_l7) + 0)), gm_sfdbfrv46lihd, 1) == 0));
                        if (!t24) goto ie23;
                        {
                            goto lc10;
                        }
                        goto ix23;
ie23:;
ix23:;
                    }
                    {
                        gm_b16_8 t26 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l7) + 0));
                        gm_l8 = t26;
                    }
                    {
                        void * t28 = gm_l0;
                        gm_b16_8 t29 = GM_LD(gm_b16_8, ((((uint8_t *)t28) + 128) + 0));
                        gm_b16_8 t30 = GM_LD(gm_b16_8, ((uint8_t *)&t29));
                        gm_b16_8 t31 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l7) + 0));
                        gm_th2lqbo77pvsc t32;
                        gm_f_lookup_var_3vpm3u6((void *)&t32, t30, t31);
                        uint8_t t33;
                        if (((((uint8_t *)&t32))[16] != 0)) goto mn35;
                        t33 = ((uint8_t)0);
                        goto me34;
mn35:;
                        t33 = ((uint8_t)1);
                        goto me34;
mn36:;
                        gm_trap("no match arm matched the scrutinee", gm_src3dhhc4laniae, 277, 12);
me34:;
                        if (!t33) goto ie27;
                        {
                            void * t38 = gm_l0;
                            void * t39 = gm_l0;
                            uint32_t t40 = GM_LD(uint32_t, (((uint8_t *)t39) + 168));
                            uint32_t r41; bool o42 = __builtin_add_overflow(t40, ((uint32_t)1ULL), &r41);
                            if (o42) gm_trap("integer overflow in `+`", gm_src3dhhc4laniae, 278, 13);
                            uint32_t t43 = r41;
                            GM_ST(uint32_t, (((uint8_t *)t38) + 168), t43);
                        }
                        {
                            gm_tommf4j9upomc t44;
                            memset((uint8_t *)&t44 + 20, 0, 4);
                            gm_b16_8 t45 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l7) + 0));
                            GM_ST(gm_b16_8, (uint8_t *)&t44 + 0, t45);
                            void * t46 = gm_l0;
                            uint32_t t47 = GM_LD(uint32_t, (((uint8_t *)t46) + 168));
                            GM_ST(uint32_t, (uint8_t *)&t44 + 16, t47);
                            gm_tabb710d9mq2e t48;
                            gm_f_format_5a05gmg((void *)&t48, (void *)NULL, cx, NULL, &t44);
                            if (gm_unw.kind) {
                                goto gm_ll14;
                            }
                            if (((((uint8_t *)&t48))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t48) + 8, gm_src3dhhc4laniae, 279, 24);
                            gm_b16_8 t49 = GM_LD(gm_b16_8, (((uint8_t *)&t48) + 8));
                            gm_b16_8 t50 = GM_LD(gm_b16_8, ((uint8_t *)&t49));
                            gm_l8 = t50;
                        }
                        goto ix27;
ie27:;
ix27:;
                    }
                    {
                        void * t51 = (void *)((uint8_t *)&F.l3);
                        gm_t2s5j3n0guq0t t52;
                        uint32_t t53 = GM_LD(uint32_t, (((uint8_t *)&gm_l7) + 16));
                        GM_ST(uint32_t, (uint8_t *)&t52 + 0, t53);
                        uint32_t t54 = GM_LD(uint32_t, (((uint8_t *)&gm_l7) + 20));
                        GM_ST(uint32_t, (uint8_t *)&t52 + 4, t54);
                        gm_b16_8 t55 = gm_l8;
                        GM_ST(gm_b16_8, (uint8_t *)&t52 + 8, t55);
                        gm_tftb1odc739t5 t56 = gm_f_push_f9bu01m(t51, &t52);
                        if (((((uint8_t *)&t56))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t56) + 1, gm_src3dhhc4laniae, 281, 9);
                        gm_b0_1 t57 = GM_LD(gm_b0_1, (((uint8_t *)&t56) + 1));
                    }
                    {
                        void * t58 = (void *)((uint8_t *)&F.l4);
                        gm_b16_8 t59 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l7) + 0));
                        gm_b16_8 t60 = gm_l8;
                        gm_b0_1 t61 = gm_f_set_var_pjsp5eq((void *)NULL, cx, NULL, t58, t59, t60);
                        if (gm_unw.kind) {
                            goto gm_ll14;
                        }
                    }
                    goto me19;
mn20:;
                    gm_trap("no match arm matched the scrutinee", gm_src3dhhc4laniae, 274, 5);
me19:;
                }
lc10:;
                {
                    uint64_t t63 = gm_l6;
                    uint64_t r64; bool o65 = __builtin_add_overflow(t63, ((uint64_t)1ULL), &r64);
                    uint64_t t66 = r64;
                    gm_l6 = t66;
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
        void * t67 = gm_l0;
        gm_f_renamed_vars_fho5hvj((void *)&F.l9, (void *)NULL, cx, NULL, t67);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_b16_8 t68 = gm_l1;
        gm_l10 = t68;
    }
    {
        uint64_t t69 = GM_LD(uintptr_t, (((uint8_t *)&F.l9) + 0) + 8);
        uint8_t t70 = (t69 > ((uint64_t)0ULL));
        gm_l11 = t70;
    }
    {
        {
            gm_b16_8 t72 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
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
                    gm_t2s5j3n0guq0t t84 = GM_LD(gm_t2s5j3n0guq0t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l12)) + t80 * 24));
                    gm_l14 = t84;
                    {
                        gm_b16_8 t87 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l14) + 8));
                        uint64_t t88 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                        void * t89 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)));
                        uint32_t t90 = GM_LD(uint32_t, (((uint8_t *)&gm_l14) + 0));
                        uint64_t t91 = (uint64_t)t90;
                        uint64_t t92 = (uintptr_t)t91;
                        uint32_t t93 = GM_LD(uint32_t, (((uint8_t *)&gm_l14) + 4));
                        uint64_t t94 = (uint64_t)t93;
                        uint64_t t95 = (uintptr_t)t94;
                        if (t92 > t95 || t95 > (uintptr_t)t88) gm_trap_slice(t92, t95, t88, gm_src3dhhc4laniae, 288, 26);
                        gm_b16_8 t96;
                        GM_ST(void *, (uint8_t *)&t96, (void *)(((uint8_t *)t89 + t92 * 1)));
                        GM_ST(uintptr_t, (uint8_t *)&t96 + 8, (uintptr_t)(t95 - t92));
                        uint8_t t97 = (!gm_eq0iqiubfm7e(((uint8_t *)&t87), ((uint8_t *)&t96)));
                        if (!t97) goto ie86;
                        {
                            gm_l11 = ((uint8_t)1);
                        }
                        goto ix86;
ie86:;
ix86:;
                    }
                    goto me82;
mn83:;
                    gm_trap("no match arm matched the scrutinee", gm_src3dhhc4laniae, 287, 5);
me82:;
                }
lc73:;
                {
                    uint64_t t100 = gm_l13;
                    uint64_t r101; bool o102 = __builtin_add_overflow(t100, ((uint64_t)1ULL), &r101);
                    uint64_t t103 = r101;
                    gm_l13 = t103;
                }
            }
lb73:;
        }
    }
    {
        uint8_t t105 = gm_l11;
        if (!t105) goto ie104;
        {
            gm_b16_8 t107 = gm_l1;
            gm_b16_8 t108 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
            gm_b16_8 t109 = GM_LD(gm_b16_8, ((uint8_t *)&t108));
            gm_b16_8 t110 = GM_LD(gm_b16_8, (((uint8_t *)&F.l9) + 0));
            gm_b16_8 t111 = GM_LD(gm_b16_8, ((uint8_t *)&t110));
            gm_b16_8 t112 = gm_f_rename_47b7og5((void *)NULL, cx, NULL, t107, t109, t111);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l10 = t112;
        }
        goto ix104;
ie104:;
ix104:;
    }
    {
        void * t113 = gm_l0;
        gm_f_replay_texts_3te9pi7((void *)&F.l15, (void *)NULL, cx, NULL, t113, ((uint8_t)0));
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_b16_8 t114;
        GM_ST(void *, (uint8_t *)&t114, (void *)(gm_som0q53jqvjh3));
        GM_ST(uintptr_t, (uint8_t *)&t114 + 8, (uintptr_t)(4));
        GM_ST(gm_b16_8, (uint8_t *)&F.l16 + 0, t114);
        gm_b16_8 t115 = gm_l10;
        GM_ST(gm_b16_8, (uint8_t *)&F.l16 + 16, t115);
        gm_b16_8 t116;
        GM_ST(void *, (uint8_t *)&t116, (void *)(gm_so514oj5nbhcu));
        GM_ST(uintptr_t, (uint8_t *)&t116 + 8, (uintptr_t)(1));
        GM_ST(gm_b16_8, (uint8_t *)&F.l16 + 32, t116);
    }
    {
        void * t117 = gm_l0;
        gm_th2lqbo77pvsc t118;
        memset(&t118, 0, sizeof t118);
        gm_b16_8 t119 = gm_f_empty_slice_5nn7ph0();
        gm_b16_8 t120 = GM_LD(gm_b16_8, ((uint8_t *)&t119));
        gm_b16_8 t121 = GM_LD(gm_b16_8, (((uint8_t *)&F.l15) + 0));
        gm_b16_8 t122 = GM_LD(gm_b16_8, ((uint8_t *)&t121));
        gm_tfeab279c16rr t123 = F.l16;
        gm_td6svc7gs468a t124;
        GM_ST(uint8_t, (uint8_t *)&t124 + 49, 0);
        GM_ST(uint16_t, (uint8_t *)&t124 + 50, 0);
        GM_ST(uint32_t, (uint8_t *)&t124 + 52, 0);
        GM_ST(gm_tfeab279c16rr, ((uint8_t *)&t124), t123);
        ((uint8_t *)&t124)[48] = 1;
        gm_b16_8 t125 = GM_LD(gm_b16_8, gm_cd231ebi1kg);
        gm_f_compile_rj09gp7((void *)&F.l17, (void *)NULL, cx, NULL, t117, &t118, t120, t122, &t124, t125, ((uint8_t)1));
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_toqvmi7jqorid t126 = F.l17;
        F.l18 = t126;
    }
    {
        gm_l19 = ((uint8_t)0);
    }
    {
        void * t128 = GM_LD(void *, (((uint8_t *)&F.l17) + 24));
        uint8_t t129;
        if ((GM_LD(void *, ((uint8_t *)&t128)) != 0)) goto mn131;
        t129 = ((uint8_t)0);
        goto me130;
mn131:;
        t129 = ((uint8_t)1);
        goto me130;
mn132:;
        gm_trap("no match arm matched the scrutinee", gm_src3dhhc4laniae, 296, 8);
me130:;
        if (!t129) goto ie127;
        goto ix127;
ie127:;
        uint8_t t135 = GM_LD(uint8_t, (((uint8_t *)a2) + 16));
        if (!t135) goto ie134;
        {
            gm_b16_8 t137;
            GM_ST(void *, (uint8_t *)&t137, (void *)(gm_sgvln0vb20p94));
            GM_ST(uintptr_t, (uint8_t *)&t137 + 8, (uintptr_t)(16));
            GM_ST(gm_b16_8, (uint8_t *)&F.l20 + 0, t137);
            gm_b16_8 t138 = gm_l10;
            GM_ST(gm_b16_8, (uint8_t *)&F.l20 + 16, t138);
            gm_b16_8 t139;
            GM_ST(void *, (uint8_t *)&t139, (void *)(gm_spun41oorb00o));
            GM_ST(uintptr_t, (uint8_t *)&t139 + 8, (uintptr_t)(29));
            GM_ST(gm_b16_8, (uint8_t *)&F.l20 + 32, t139);
        }
        {
            void * t140 = gm_l0;
            gm_th2lqbo77pvsc t141;
            memset(&t141, 0, sizeof t141);
            gm_b16_8 t142 = gm_f_empty_slice_5nn7ph0();
            gm_b16_8 t143 = GM_LD(gm_b16_8, ((uint8_t *)&t142));
            gm_b16_8 t144 = GM_LD(gm_b16_8, (((uint8_t *)&F.l15) + 0));
            gm_b16_8 t145 = GM_LD(gm_b16_8, ((uint8_t *)&t144));
            gm_tfeab279c16rr t146 = F.l20;
            gm_td6svc7gs468a t147;
            GM_ST(uint8_t, (uint8_t *)&t147 + 49, 0);
            GM_ST(uint16_t, (uint8_t *)&t147 + 50, 0);
            GM_ST(uint32_t, (uint8_t *)&t147 + 52, 0);
            GM_ST(gm_tfeab279c16rr, ((uint8_t *)&t147), t146);
            ((uint8_t *)&t147)[48] = 1;
            gm_b16_8 t148 = GM_LD(gm_b16_8, gm_cd231ebi1kg);
            gm_f_compile_rj09gp7((void *)&F.l21, (void *)NULL, cx, NULL, t140, &t141, t143, t145, &t147, t148, ((uint8_t)1));
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t150 = GM_LD(void *, (((uint8_t *)&F.l21) + 24));
            uint8_t t151;
            if ((GM_LD(void *, ((uint8_t *)&t150)) != 0)) goto mn153;
            t151 = ((uint8_t)0);
            goto me152;
mn153:;
            t151 = ((uint8_t)1);
            goto me152;
mn154:;
            gm_trap("no match arm matched the scrutinee", gm_src3dhhc4laniae, 301, 12);
me152:;
            if (!t151) goto ie149;
            {
                gm_toqvmi7jqorid t156 = F.l21;
                F.l18 = t156;
            }
            {
                gm_l19 = ((uint8_t)1);
            }
            {
                void * t157 = (void *)((uint8_t *)&F.l17);
                gm_b0_1 t158 = gm_f_release_mdh37fq((void *)NULL, cx, NULL, t157);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix149;
ie149:;
            {
                gm_l22 = ((uint8_t)0);
            }
            {
                void * t161 = GM_LD(void *, (((uint8_t *)&F.l17) + 16));
                uint64_t t162 = GM_LD(uintptr_t, ((((uint8_t *)t161) + 0) + 0) + 8);
                uint8_t t163 = (t162 > ((uint64_t)0ULL));
                if (!t163) goto ie160;
                {
                    uint64_t t165 = (uintptr_t)((uint64_t)0ULL);
                    void * t166 = GM_LD(void *, (((uint8_t *)&F.l17) + 16));
                    uint64_t t167 = GM_LD(uintptr_t, ((((uint8_t *)t166) + 0) + 0) + 8);
                    if (t165 >= (uintptr_t)(t167)) gm_trap_index(t165, t167, gm_src3dhhc4laniae, 307, 71);
                    gm_b16_8 t168 = GM_LD(gm_b16_8, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t166) + 0) + 0)) + t165 * 184) + 8));
                    gm_b16_8 t169;
                    GM_ST(void *, (uint8_t *)&t169, (void *)(gm_sshi9ffc6nkc4));
                    GM_ST(uintptr_t, (uint8_t *)&t169 + 8, (uintptr_t)(9));
                    uint8_t t170 = gm_f_contains_gkki4kq(t168, t169);
                    gm_l22 = t170;
                }
                goto ix160;
ie160:;
ix160:;
            }
            {
                void * t171;
                uint8_t t173 = gm_l22;
                if (!t173) goto ie172;
                void * t175 = (void *)((uint8_t *)&F.l21);
                void * t176 = t175;
                t171 = t176;
                goto ix172;
ie172:;
                void * t178 = (void *)((uint8_t *)&F.l17);
                void * t179 = t178;
                t171 = t179;
ix172:;
                gm_l23 = t171;
            }
            {
                void * t180 = gm_l23;
                void * t181 = gm_l0;
                gm_t9qkh0rsooc8d t182 = GM_LD(gm_t9qkh0rsooc8d, ((((uint8_t *)t181) + 0) + 2));
                gm_b16_8 t183 = gm_f_shown_o0bbr93((void *)NULL, cx, NULL, t180, t182);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_b0_1 t184 = gm_f_show_861qdlm((void *)NULL, cx, NULL, t183);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t185 = (void *)((uint8_t *)&F.l21);
                gm_b0_1 t186 = gm_f_release_mdh37fq((void *)NULL, cx, NULL, t185);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t187 = (void *)((uint8_t *)&F.l17);
                gm_b0_1 t188 = gm_f_release_mdh37fq((void *)NULL, cx, NULL, t187);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t189 = (void *)((uint8_t *)&F.l15);
                gm_f_deinit_gvn47sj(t189);
            }
            {
                uint8_t t190 = 0;
                F.ret = ((uint8_t)0);
                goto gm_fret;
            }
ix149:;
        }
        goto ix134;
ie134:;
        {
            void * t192 = (void *)((uint8_t *)&F.l17);
            void * t193 = gm_l0;
            gm_t9qkh0rsooc8d t194 = GM_LD(gm_t9qkh0rsooc8d, ((((uint8_t *)t193) + 0) + 2));
            gm_b16_8 t195 = gm_f_shown_o0bbr93((void *)NULL, cx, NULL, t192, t194);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_b0_1 t196 = gm_f_show_861qdlm((void *)NULL, cx, NULL, t195);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t197 = (void *)((uint8_t *)&F.l17);
            gm_b0_1 t198 = gm_f_release_mdh37fq((void *)NULL, cx, NULL, t197);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t199 = (void *)((uint8_t *)&F.l15);
            gm_f_deinit_gvn47sj(t199);
        }
        {
            uint8_t t200 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
ix134:;
ix127:;
    }
    {
        void * t201 = (void *)((uint8_t *)&F.l15);
        gm_f_deinit_gvn47sj(t201);
    }
    {
    }
    {
        void * t204 = (void *)((uint8_t *)&F.l18);
        void * t205 = gm_l0;
        gm_t9qkh0rsooc8d t206 = GM_LD(gm_t9qkh0rsooc8d, ((((uint8_t *)t205) + 0) + 2));
        gm_b16_8 t207 = gm_f_shown_o0bbr93((void *)NULL, cx, NULL, t204, t206);
        if (gm_unw.kind) {
            goto gm_ld1_1;
        }
        gm_b0_1 t208 = gm_f_show_861qdlm((void *)NULL, cx, NULL, t207);
        if (gm_unw.kind) {
            goto gm_ld1_1;
        }
    }
    {
        gm_th2lqbo77pvsc t209;
        memset(&t209, 0, sizeof t209);
        gm_l24 = t209;
    }
    {
        uint8_t t211 = gm_l19;
        uint8_t t212 = (uint8_t)!t211;
        if (!t212) goto ie210;
        {
            gm_b16_8 t214 = gm_l10;
            GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l24)) + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l24)) + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l24)) + 20, 0);
            ((uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l24)))[16] = 1;
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l24)) + 0, t214);
        }
        goto ix210;
ie210:;
        gm_b16_8 t216 = gm_l10;
        uint8_t t217 = gm_f_may_mutate_uj4e7jp((void *)NULL, cx, NULL, t216);
        if (gm_unw.kind) {
            goto gm_ld1_1;
        }
        if (!t217) goto ie215;
        {
            gm_b16_8 t219 = gm_l10;
            gm_tnpifv20irnph t220;
            GM_ST(gm_b16_8, ((uint8_t *)&t220 + 0), t219);
            gm_tabb710d9mq2e t221;
            gm_f_format_uq5dnfv((void *)&t221, (void *)NULL, cx, NULL, t220);
            if (gm_unw.kind) {
                goto gm_ld1_1;
            }
            if (((((uint8_t *)&t221))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t221) + 8, gm_src3dhhc4laniae, 330, 16);
            gm_b16_8 t222 = GM_LD(gm_b16_8, (((uint8_t *)&t221) + 8));
            gm_b16_8 t223 = GM_LD(gm_b16_8, ((uint8_t *)&t222));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l24)) + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l24)) + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l24)) + 20, 0);
            ((uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l24)))[16] = 1;
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l24)) + 0, t223);
        }
        goto ix215;
ie215:;
ix215:;
ix210:;
    }
    {
        gm_l25 = ((uint8_t)0);
    }
    {
        gm_th2lqbo77pvsc t224;
        memset(&t224, 0, sizeof t224);
        gm_l26 = t224;
    }
    {
        if (!((((uint8_t *)&gm_l24))[16] != 0)) goto ie225;
        gm_b16_8 t226 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l24));
        gm_l27 = t226;
        {
            void * t228 = gm_l0;
            gm_f_replay_texts_3te9pi7((void *)&F.l28, (void *)NULL, cx, NULL, t228, ((uint8_t)1));
            if (gm_unw.kind) {
                goto gm_ld1_1;
            }
        }
        {
            gm_b16_8 t229;
            GM_ST(void *, (uint8_t *)&t229, (void *)(gm_som0q53jqvjh3));
            GM_ST(uintptr_t, (uint8_t *)&t229 + 8, (uintptr_t)(4));
            GM_ST(gm_b16_8, (uint8_t *)&F.l29 + 0, t229);
            gm_b16_8 t230 = gm_l27;
            GM_ST(gm_b16_8, (uint8_t *)&F.l29 + 16, t230);
            gm_b16_8 t231;
            GM_ST(void *, (uint8_t *)&t231, (void *)(gm_so514oj5nbhcu));
            GM_ST(uintptr_t, (uint8_t *)&t231 + 8, (uintptr_t)(1));
            GM_ST(gm_b16_8, (uint8_t *)&F.l29 + 32, t231);
        }
        {
            void * t232 = gm_l0;
            gm_th2lqbo77pvsc t233;
            memset(&t233, 0, sizeof t233);
            gm_b16_8 t234 = gm_f_empty_slice_5nn7ph0();
            gm_b16_8 t235 = GM_LD(gm_b16_8, ((uint8_t *)&t234));
            gm_b16_8 t236 = GM_LD(gm_b16_8, (((uint8_t *)&F.l28) + 0));
            gm_b16_8 t237 = GM_LD(gm_b16_8, ((uint8_t *)&t236));
            gm_tfeab279c16rr t238 = F.l29;
            gm_td6svc7gs468a t239;
            GM_ST(uint8_t, (uint8_t *)&t239 + 49, 0);
            GM_ST(uint16_t, (uint8_t *)&t239 + 50, 0);
            GM_ST(uint32_t, (uint8_t *)&t239 + 52, 0);
            GM_ST(gm_tfeab279c16rr, ((uint8_t *)&t239), t238);
            ((uint8_t *)&t239)[48] = 1;
            gm_b16_8 t240;
            GM_ST(void *, (uint8_t *)&t240, (void *)(gm_s1qrqfh656g4q));
            GM_ST(uintptr_t, (uint8_t *)&t240 + 8, (uintptr_t)(5));
            gm_f_compile_rj09gp7((void *)&F.l30, (void *)NULL, cx, NULL, t232, &t233, t235, t237, &t239, t240, ((uint8_t)0));
            if (gm_unw.kind) {
                goto gm_ld1_1;
            }
        }
        {
            void * t241 = GM_LD(void *, (((uint8_t *)&F.l30) + 24));
            uint8_t t242;
            if ((GM_LD(void *, ((uint8_t *)&t241)) != 0)) goto mn244;
            t242 = ((uint8_t)1);
            goto me243;
mn244:;
            t242 = ((uint8_t)0);
            goto me243;
mn245:;
            gm_trap("no match arm matched the scrutinee", gm_src3dhhc4laniae, 338, 18);
me243:;
            gm_l25 = t242;
        }
        {
            void * t246 = (void *)((uint8_t *)&F.l30);
            gm_b0_1 t247 = gm_f_release_mdh37fq((void *)NULL, cx, NULL, t246);
            if (gm_unw.kind) {
                goto gm_ld1_1;
            }
        }
        {
            void * t248 = (void *)((uint8_t *)&F.l28);
            gm_f_deinit_gvn47sj(t248);
        }
        {
            uint8_t t250 = 0;
            uint8_t t251 = gm_l25;
            uint8_t t252 = (uint8_t)!t251;
            if (!t252) {
                uint64_t t253 = GM_LD(uintptr_t, (((uint8_t *)a2) + 0) + 8);
                uint8_t t254 = (t253 > ((uint64_t)0ULL));
                t250 = t254;
            } else t250 = 1;
            if (!t250) goto ie249;
            {
                gm_b16_8 t256 = gm_l27;
                GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l26)) + 17, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l26)) + 18, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l26)) + 20, 0);
                ((uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l26)))[16] = 1;
                GM_ST(gm_b16_8, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l26)) + 0, t256);
            }
            goto ix249;
ie249:;
ix249:;
        }
        goto ix225;
ie225:;
ix225:;
    }
    {
        void * t257 = (void *)((uint8_t *)&F.l18);
        void * t258 = gm_l0;
        gm_t9qkh0rsooc8d t259 = GM_LD(gm_t9qkh0rsooc8d, ((((uint8_t *)t258) + 0) + 1));
        void * t260 = gm_l0;
        uint64_t t261 = GM_LD(uintptr_t, ((((uint8_t *)t260) + 88) + 0) + 8);
        uint8_t t262 = (t261 > ((uint64_t)0ULL));
        uint8_t t263 = gm_f_run_aqa5hss((void *)NULL, cx, NULL, t257, t259, t262);
        if (gm_unw.kind) {
            goto gm_ld1_1;
        }
        gm_l31 = t263;
    }
    {
        uint8_t t265 = gm_l31;
        uint8_t t266 = (uint8_t)!t265;
        if (!t266) goto ie264;
        {
            uint8_t t268 = 0;
            F.ret = ((uint8_t)0);
            gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = t268;
            goto gm_ld1_1;
        }
        goto ix264;
ie264:;
ix264:;
    }
    {
        if (!((((uint8_t *)&gm_l26))[16] != 0)) goto ie269;
        gm_b16_8 t270 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l26));
        gm_l32 = t270;
        {
            uint8_t t273 = gm_l25;
            if (!t273) goto ie272;
            {
                gm_f_list_epvducg((void *)&F.l33, (void *)NULL, cx, NULL);
                if (gm_unw.kind) {
                    goto gm_ld1_1;
                }
            }
            {
                {
                    gm_b16_8 t276 = GM_LD(gm_b16_8, (((uint8_t *)a2) + 0));
                    gm_l34 = t276;
                }
                {
                    gm_l35 = ((uint64_t)0ULL);
                }
                {
                    for (;;) {
                        uint64_t t278 = gm_l35;
                        uint64_t t279 = GM_LD(uintptr_t, ((uint8_t *)&gm_l34) + 8);
                        uint8_t t280 = (t278 < t279);
                        if (!t280) goto lb277;
                        {
                            uint64_t t283 = gm_l35;
                            uint64_t t284 = (uintptr_t)t283;
                            uint64_t t285 = GM_LD(uintptr_t, ((uint8_t *)&gm_l34) + 8);
                            gm_t5hdnnftkup6m t288 = GM_LD(gm_t5hdnnftkup6m, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l34)) + t284 * 24));
                            gm_l36 = t288;
                            {
                                void * t290 = (void *)((uint8_t *)&F.l33);
                                gm_b16_8 t291 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l36) + 0));
                                gm_tftb1odc739t5 t292 = gm_f_push_3ef746b(t290, t291);
                                if (((((uint8_t *)&t292))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t292) + 1, gm_src3dhhc4laniae, 348, 38);
                                gm_b0_1 t293 = GM_LD(gm_b0_1, (((uint8_t *)&t292) + 1));
                            }
                            goto me286;
mn287:;
                            gm_trap("no match arm matched the scrutinee", gm_src3dhhc4laniae, 348, 13);
me286:;
                        }
lc277:;
                        {
                            uint64_t t295 = gm_l35;
                            uint64_t r296; bool o297 = __builtin_add_overflow(t295, ((uint64_t)1ULL), &r296);
                            uint64_t t298 = r296;
                            gm_l35 = t298;
                        }
                    }
lb277:;
                }
            }
            {
                gm_b16_8 t299 = GM_LD(gm_b16_8, (((uint8_t *)&F.l33) + 0));
                gm_b16_8 t300 = GM_LD(gm_b16_8, ((uint8_t *)&t299));
                gm_b16_8 t301;
                GM_ST(void *, (uint8_t *)&t301, (void *)(gm_sg5gdnrith2i6));
                GM_ST(uintptr_t, (uint8_t *)&t301 + 8, (uintptr_t)(2));
                gm_tabb710d9mq2e t302;
                gm_f_join_sfknljb((void *)&t302, (void *)NULL, cx, NULL, t300, t301);
                if (gm_unw.kind) {
                    goto gm_ld1_1;
                }
                if (((((uint8_t *)&t302))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t302) + 8, gm_src3dhhc4laniae, 349, 23);
                gm_b16_8 t303 = GM_LD(gm_b16_8, (((uint8_t *)&t302) + 8));
                gm_l37 = t303;
            }
            {
                gm_b16_8 t304 = gm_l37;
                if (gm_pf454qfrpt6r(cx, gm_smrc1u3r4i5hd, 6, gm_src3dhhc4laniae, 350, 13)) {
                    goto gm_ld1_1;
                }
                if (gm_pfu8dq5bikbh(cx, &gm_tybihgc69jv3, ((uint8_t *)&t304), gm_src3dhhc4laniae, 350, 13)) {
                    goto gm_ld1_1;
                }
                if (gm_pf454qfrpt6r(cx, gm_s68c5jkfvg5dd, 85, gm_src3dhhc4laniae, 350, 13)) {
                    goto gm_ld1_1;
                }
            }
            goto ix272;
ie272:;
ix272:;
        }
        {
            void * t305 = gm_l0;
            void * t306 = (void *)(((uint8_t *)t305) + 88);
            gm_tk9n3vlila9qg t307;
            memset((uint8_t *)&t307 + 33, 0, 7);
            gm_b16_8 t308 = gm_l1;
            gm_tabb710d9mq2e t309;
            gm_f_dupe_g9iqpvn((void *)&t309, (void *)NULL, cx, NULL, t308, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kdmasfkh2un)));
            if (gm_unw.kind) {
                goto gm_ld1_1;
            }
            if (((((uint8_t *)&t309))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t309) + 8, gm_src3dhhc4laniae, 352, 32);
            gm_b16_8 t310 = GM_LD(gm_b16_8, (((uint8_t *)&t309) + 8));
            gm_b16_8 t311 = GM_LD(gm_b16_8, ((uint8_t *)&t310));
            GM_ST(gm_b16_8, (uint8_t *)&t307 + 0, t311);
            gm_b16_8 t312 = gm_l32;
            GM_ST(gm_b16_8, (uint8_t *)&t307 + 16, t312);
            uint8_t t313 = gm_l25;
            GM_ST(uint8_t, (uint8_t *)&t307 + 32, t313);
            gm_tftb1odc739t5 t314 = gm_f_push_ej78v89(t306, &t307);
            if (((((uint8_t *)&t314))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t314) + 1, gm_src3dhhc4laniae, 352, 9);
            gm_b0_1 t315 = GM_LD(gm_b0_1, (((uint8_t *)&t314) + 1));
        }
        goto ix269;
ie269:;
ix269:;
    }
    {
        void * t316 = gm_l0;
        gm_trqfvmjtt4sij t317 = F.l4;
        GM_ST(gm_trqfvmjtt4sij, (((uint8_t *)t316) + 128), t317);
    }
    {
        uint8_t t318 = 0;
        F.ret = ((uint8_t)1);
        gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = t318;
        goto gm_ld1_1;
    }
    {
        void * t319 = (void *)((uint8_t *)&F.l18);
        gm_b0_1 t320 = gm_f_release_mdh37fq((void *)NULL, cx, NULL, t319);
    }
    goto gm_ls321;
gm_ld1_1:;
    {
        gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
        void * t322 = (void *)((uint8_t *)&F.l18);
        gm_b0_1 t323 = gm_f_release_mdh37fq((void *)NULL, cx, NULL, t322);
        gm_unw = gm_saved;
    }
    goto gm_lad;
gm_ls321:;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_b0_1 gm_f_submit_pah0u77(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, const void * a2) {
    struct gm_fr_submit_pah0u77 F;
    void * gm_l0; /* r */
    gm_b16_8 gm_l1; /* text */
    gm_b16_8 gm_l3; /* items */
    gm_ti5ramhglnvdt gm_l4; /* local */
    gm_th2lqbo77pvsc gm_l5; /* s */
    uint8_t gm_l6; /* all */
    gm_b16_8 gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    gm_b16_8 gm_l10; /* msg */
    gm_th2lqbo77pvsc gm_l11; /* s */
    gm_th2lqbo77pvsc gm_l12; /* s */
    GM_STACK_CHECK(&F, 653u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint8_t t3;
        if ((((((uint8_t *)a2) + 0))[16] != 0)) goto mn5;
        t3 = ((uint8_t)1);
        goto me4;
mn5:;
        t3 = ((uint8_t)0);
        goto me4;
mn6:;
        gm_trap("no match arm matched the scrutinee", gm_src3dhhc4laniae, 213, 8);
me4:;
        if (!t3) goto ie2;
        uint8_t t7;
        if ((((((uint8_t *)a2) + 24))[24] != 0)) goto mn9;
        t7 = ((uint8_t)1);
        goto me8;
mn9:;
        t7 = ((uint8_t)0);
        goto me8;
mn10:;
        gm_trap("no match arm matched the scrutinee", gm_src3dhhc4laniae, 213, 28);
me8:;
        if (!t7) goto ie2;
        {
            gm_b16_8 t12 = gm_l1;
            void * t13 = gm_l0;
            gm_t9qkh0rsooc8d t14 = GM_LD(gm_t9qkh0rsooc8d, ((((uint8_t *)t13) + 0) + 2));
            gm_b16_8 t15 = gm_f_syntax_errors_fk23f5t((void *)NULL, cx, NULL, t12, t14);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_b0_1 t16 = gm_f_show_861qdlm((void *)NULL, cx, NULL, t15);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_b0_1 t17;
            uint8_t t18 = 0;
            F.ret = t17;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        if (!(((((uint8_t *)a2) + 0))[16] != 0)) goto ie19;
        gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)a2) + 0));
        gm_l3 = t20;
        {
            gm_ti5ramhglnvdt t22;
            memset(&t22, 0, sizeof t22);
            gm_l4 = t22;
        }
        {
            if (!(((((uint8_t *)a2) + 24))[24] != 0)) goto ie23;
            gm_th2lqbo77pvsc t24 = GM_LD(gm_th2lqbo77pvsc, (((uint8_t *)a2) + 24));
            gm_l5 = t24;
            {
                gm_l6 = ((uint8_t)1);
            }
            {
                {
                    gm_b16_8 t27 = gm_l3;
                    gm_l7 = t27;
                }
                {
                    gm_l8 = ((uint64_t)0ULL);
                }
                {
                    for (;;) {
                        uint64_t t29 = gm_l8;
                        uint64_t t30 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                        uint8_t t31 = (t29 < t30);
                        if (!t31) goto lb28;
                        {
                            uint64_t t34 = gm_l8;
                            uint64_t t35 = (uintptr_t)t34;
                            uint64_t t36 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                            gm_tk9n3vlila9qg t39 = GM_LD(gm_tk9n3vlila9qg, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l7)) + t35 * 40));
                            F.l9 = t39;
                            {
                                uint8_t t42 = GM_LD(uint8_t, (((uint8_t *)&F.l9) + 32));
                                uint8_t t43 = (uint8_t)!t42;
                                if (!t43) goto ie41;
                                {
                                    gm_l6 = ((uint8_t)0);
                                }
                                goto ix41;
ie41:;
ix41:;
                            }
                            goto me37;
mn38:;
                            gm_trap("no match arm matched the scrutinee", gm_src3dhhc4laniae, 222, 13);
me37:;
                        }
lc28:;
                        {
                            uint64_t t46 = gm_l8;
                            uint64_t r47; bool o48 = __builtin_add_overflow(t46, ((uint64_t)1ULL), &r47);
                            uint64_t t49 = r47;
                            gm_l8 = t49;
                        }
                    }
lb28:;
                }
            }
            {
                uint8_t t51 = gm_l6;
                if (!t51) goto ie50;
                {
                    const uint8_t *t53 = ((uint8_t *)&gm_l5);
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_ti5ramhglnvdt *)((uint8_t *)&gm_l4)) + 25, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_ti5ramhglnvdt *)((uint8_t *)&gm_l4)) + 26, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_ti5ramhglnvdt *)((uint8_t *)&gm_l4)) + 28, 0);
                    ((uint8_t *)&(*(gm_ti5ramhglnvdt *)((uint8_t *)&gm_l4)))[24] = 1;
                    memmove((uint8_t *)&(*(gm_ti5ramhglnvdt *)((uint8_t *)&gm_l4)) + 0, t53, 24);
                }
                goto ix50;
ie50:;
ix50:;
            }
            goto ix23;
ie23:;
ix23:;
        }
        {
            void * t54 = gm_l0;
            gm_b16_8 t55 = gm_l1;
            gm_b16_8 t56 = gm_l3;
            gm_b16_8 t57 = GM_LD(gm_b16_8, ((uint8_t *)&t56));
            gm_th2lqbo77pvsc t58;
            gm_f_try_decls_23vcohd((void *)&t58, (void *)NULL, cx, NULL, t54, t55, t57);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_b16_8 t59;
            if (((((uint8_t *)&t58))[16] != 0)) {
                gm_b16_8 t60 = GM_LD(gm_b16_8, (((uint8_t *)&t58) + 0));
                t59 = t60;
            } else {
                gm_b0_1 t61;
                uint8_t t62 = 0;
                F.ret = t61;
                goto gm_fret;
            }
            gm_l10 = t59;
        }
        {
            if (!((((uint8_t *)&gm_l4))[24] != 0)) goto ie63;
            gm_th2lqbo77pvsc t64 = GM_LD(gm_th2lqbo77pvsc, ((uint8_t *)&gm_l4));
            gm_l11 = t64;
            {
                {
                    void * t67 = gm_l0;
                    gm_b16_8 t68 = gm_l1;
                    uint8_t t69 = gm_f_statements_dafpns9((void *)NULL, cx, NULL, t67, t68, &(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l11)));
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
            }
            goto ix63;
ie63:;
            {
                gm_b16_8 t71 = gm_l10;
                gm_b0_1 t72 = gm_f_show_861qdlm((void *)NULL, cx, NULL, t71);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
ix63:;
        }
        {
            gm_b0_1 t73;
            uint8_t t74 = 0;
            F.ret = t73;
            goto gm_fret;
        }
        goto ix19;
ie19:;
ix19:;
    }
    {
        if (!(((((uint8_t *)a2) + 24))[24] != 0)) goto ie75;
        gm_th2lqbo77pvsc t76 = GM_LD(gm_th2lqbo77pvsc, (((uint8_t *)a2) + 24));
        gm_l12 = t76;
        {
            {
                void * t79 = gm_l0;
                gm_b16_8 t80 = gm_l1;
                uint8_t t81 = gm_f_statements_dafpns9((void *)NULL, cx, NULL, t79, t80, &(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l12)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
        }
        goto ix75;
ie75:;
ix75:;
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
extern __attribute__((aligned(4))) gm_b0_1 gm_f_submit_text_7dvfev6(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_trim_newlines_9rj3i73(gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_trim_newlines_9rj3i73 F;
    gm_b16_8 gm_l0; /* s */
    uint64_t gm_l1; /* n */
    GM_STACK_CHECK_SHALLOW(&F, 183u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint64_t t2 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        gm_l1 = t2;
    }
    {
        for (;;) {
            uint64_t t4 = gm_l1;
            uint8_t t5 = (t4 > ((uint64_t)0ULL));
            if (!t5) goto lb3;
            uint64_t t6 = gm_l1;
            uint64_t r7; bool o8 = __builtin_sub_overflow(t6, ((uint64_t)1ULL), &r7);
            if (o8) gm_trap("integer overflow in `-`", gm_src3dhhc4laniae, 503, 23);
            uint64_t t9 = r7;
            uint64_t t10 = (uintptr_t)t9;
            uint64_t t11 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            if (t10 >= (uintptr_t)(t11)) gm_trap_index(t10, t11, gm_src3dhhc4laniae, 503, 21);
            uint8_t t12 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t10 * 1));
            gm_tqs5p4n2tq19v t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)10ULL));
            GM_ST(uint8_t, ((uint8_t *)&t13 + 1), ((uint8_t)13ULL));
            uint8_t t14 = 0;
            if (gm_eqjmccqdb90f(((uint8_t *)&t13) + 0, ((uint8_t *)&t12))) t14 = 1;
            if (gm_eqjmccqdb90f(((uint8_t *)&t13) + 1, ((uint8_t *)&t12))) t14 = 1;
            uint8_t t15 = (uint8_t)(t14);
            if (!t15) goto lb3;
            {
                uint64_t t18 = gm_l1;
                uint64_t r19; bool o20 = __builtin_sub_overflow(t18, ((uint64_t)1ULL), &r19);
                if (o20) gm_trap("integer overflow in `-`", gm_src3dhhc4laniae, 503, 48);
                uint64_t t21 = r19;
                gm_l1 = t21;
            }
lc3:;
        }
lb3:;
    }
    {
        uint64_t t22 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        void * t23 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)));
        uint64_t t24 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t25 = gm_l1;
        uint64_t t26 = (uintptr_t)t25;
        if (t24 > t26 || t26 > (uintptr_t)t22) gm_trap_slice(t24, t26, t22, gm_src3dhhc4laniae, 504, 12);
        gm_b16_8 t27;
        GM_ST(void *, (uint8_t *)&t27, (void *)(((uint8_t *)t23 + t24 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t27 + 8, (uintptr_t)(t26 - t24));
        uint8_t t28 = 0;
        F.ret = t27;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) void gm_f_try_decls_23vcohd(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2) {
    struct gm_fr_try_decls_23vcohd F;
    void * gm_l0; /* r */
    gm_b16_8 gm_l1; /* text */
    gm_b16_8 gm_l2; /* items */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    gm_b16_8 gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    gm_b16_8 gm_l9; /* n */
    gm_b16_8 gm_l13; /* <tmp> */
    uint64_t gm_l14; /* <tmp> */
    gm_t6j6smj7qoctu gm_l15; /* ch */
    uint8_t gm_l16; /* replaced */
    gm_b16_8 gm_l17; /* <tmp> */
    uint64_t gm_l18; /* <tmp> */
    gm_b16_8 gm_l19; /* n */
    gm_b16_8 gm_l20; /* <tmp> */
    uint64_t gm_l21; /* <tmp> */
    gm_b16_8 gm_l22; /* x */
    gm_b16_8 gm_l23; /* <tmp> */
    uint64_t gm_l24; /* <tmp> */
    GM_STACK_CHECK(&F, 1406u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        gm_f_list_epvducg((void *)&F.l3, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t3 = gm_l2;
            gm_l4 = t3;
        }
        {
            gm_l5 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t5 = gm_l5;
                uint64_t t6 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                uint8_t t7 = (t5 < t6);
                if (!t7) goto lb4;
                {
                    uint64_t t10 = gm_l5;
                    uint64_t t11 = (uintptr_t)t10;
                    uint64_t t12 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                    gm_tk9n3vlila9qg t15 = GM_LD(gm_tk9n3vlila9qg, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t11 * 40));
                    F.l6 = t15;
                    {
                        {
                            gm_b16_8 t18 = GM_LD(gm_b16_8, (((uint8_t *)&F.l6) + 0));
                            gm_l7 = t18;
                        }
                        {
                            gm_l8 = ((uint64_t)0ULL);
                        }
                        {
                            for (;;) {
                                uint64_t t20 = gm_l8;
                                uint64_t t21 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                                uint8_t t22 = (t20 < t21);
                                if (!t22) goto lb19;
                                {
                                    uint64_t t25 = gm_l8;
                                    uint64_t t26 = (uintptr_t)t25;
                                    uint64_t t27 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                                    gm_b16_8 t30 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l7)) + t26 * 16));
                                    gm_l9 = t30;
                                    {
                                        uint8_t t33 = (((GM_LD(uintptr_t, ((uint8_t *)&gm_l9) + 8) == 4) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l9)), gm_sog4rksrmsqd2, 4) == 0));
                                        if (!t33) goto ie32;
                                        {
                                            gm_b16_8 t35;
                                            GM_ST(void *, (uint8_t *)&t35, (void *)(gm_sn1sq59pu45vv));
                                            GM_ST(uintptr_t, (uint8_t *)&t35 + 8, (uintptr_t)(38));
                                            GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 17, 0);
                                            GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 18, 0);
                                            GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 20, 0);
                                            ((uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out))[16] = 1;
                                            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 0, t35);
                                            goto gm_fdone;
                                        }
                                        goto ix32;
ie32:;
ix32:;
                                    }
                                    {
                                        void * t36 = (void *)((uint8_t *)&F.l3);
                                        gm_b16_8 t37 = gm_l9;
                                        gm_tftb1odc739t5 t38 = gm_f_push_3ef746b(t36, t37);
                                        if (((((uint8_t *)&t38))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t38) + 1, gm_src3dhhc4laniae, 244, 13);
                                        gm_b0_1 t39 = GM_LD(gm_b0_1, (((uint8_t *)&t38) + 1));
                                    }
                                    goto me28;
mn29:;
                                    gm_trap("no match arm matched the scrutinee", gm_src3dhhc4laniae, 242, 9);
me28:;
                                }
lc19:;
                                {
                                    uint64_t t41 = gm_l8;
                                    uint64_t r42; bool o43 = __builtin_add_overflow(t41, ((uint64_t)1ULL), &r42);
                                    uint64_t t44 = r42;
                                    gm_l8 = t44;
                                }
                            }
lb19:;
                        }
                    }
                    goto me13;
mn14:;
                    gm_trap("no match arm matched the scrutinee", gm_src3dhhc4laniae, 241, 5);
me13:;
                }
lc4:;
                {
                    uint64_t t46 = gm_l5;
                    uint64_t r47; bool o48 = __builtin_add_overflow(t46, ((uint64_t)1ULL), &r47);
                    uint64_t t49 = r47;
                    gm_l5 = t49;
                }
            }
lb4:;
        }
    }
    {
        void * t50 = gm_l0;
        gm_f_replay_texts_3te9pi7((void *)&F.l10, (void *)NULL, cx, NULL, t50, ((uint8_t)0));
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t51 = gm_l0;
        gm_b16_8 t52 = gm_l1;
        gm_th2lqbo77pvsc t53;
        GM_ST(uint8_t, (uint8_t *)&t53 + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&t53 + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&t53 + 20, 0);
        GM_ST(gm_b16_8, ((uint8_t *)&t53), t52);
        ((uint8_t *)&t53)[16] = 1;
        gm_b16_8 t54 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
        gm_b16_8 t55 = GM_LD(gm_b16_8, ((uint8_t *)&t54));
        gm_b16_8 t56 = GM_LD(gm_b16_8, (((uint8_t *)&F.l10) + 0));
        gm_b16_8 t57 = GM_LD(gm_b16_8, ((uint8_t *)&t56));
        gm_td6svc7gs468a t58;
        memset(&t58, 0, sizeof t58);
        gm_b16_8 t59 = GM_LD(gm_b16_8, gm_cd231ebi1kg);
        gm_f_compile_rj09gp7((void *)&F.l11, (void *)NULL, cx, NULL, t51, &t53, t55, t57, &t58, t59, ((uint8_t)0));
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t60 = (void *)((uint8_t *)&F.l10);
        gm_f_deinit_gvn47sj(t60);
    }
    {
    }
    {
        void * t64 = GM_LD(void *, (((uint8_t *)&F.l11) + 24));
        uint8_t t65;
        if ((GM_LD(void *, ((uint8_t *)&t64)) != 0)) goto mn67;
        t65 = ((uint8_t)1);
        goto me66;
mn67:;
        t65 = ((uint8_t)0);
        goto me66;
mn68:;
        gm_trap("no match arm matched the scrutinee", gm_src3dhhc4laniae, 251, 8);
me66:;
        if (!t65) goto ie63;
        {
            void * t70 = (void *)((uint8_t *)&F.l11);
            void * t71 = gm_l0;
            gm_t9qkh0rsooc8d t72 = GM_LD(gm_t9qkh0rsooc8d, ((((uint8_t *)t71) + 0) + 2));
            gm_b16_8 t73 = gm_f_shown_o0bbr93((void *)NULL, cx, NULL, t70, t72);
            if (gm_unw.kind) {
                goto gm_ld1_1;
            }
            GM_ST(uint8_t, (uint8_t *)&F.ret + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&F.ret + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&F.ret + 20, 0);
            ((uint8_t *)&F.ret)[16] = 1;
            GM_ST(gm_b16_8, (uint8_t *)&F.ret + 0, t73);
            uint8_t t74 = 0;
            gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = t74;
            goto gm_ld1_1;
        }
        goto ix63;
ie63:;
ix63:;
    }
    {
        void * t75 = (void *)((uint8_t *)&F.l11);
        void * t76 = gm_l0;
        gm_t9qkh0rsooc8d t77 = GM_LD(gm_t9qkh0rsooc8d, ((((uint8_t *)t76) + 0) + 2));
        gm_b16_8 t78 = gm_f_shown_o0bbr93((void *)NULL, cx, NULL, t75, t77);
        if (gm_unw.kind) {
            goto gm_ld1_1;
        }
        gm_b0_1 t79 = gm_f_show_861qdlm((void *)NULL, cx, NULL, t78);
        if (gm_unw.kind) {
            goto gm_ld1_1;
        }
    }
    {
        gm_f_list_1t36jvm((void *)&F.l12, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_ld1_1;
        }
    }
    {
        {
            void * t81 = gm_l0;
            gm_b16_8 t82 = GM_LD(gm_b16_8, ((((uint8_t *)t81) + 48) + 0));
            gm_l13 = t82;
        }
        {
            gm_l14 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t84 = gm_l14;
                uint64_t t85 = GM_LD(uintptr_t, ((uint8_t *)&gm_l13) + 8);
                uint8_t t86 = (t84 < t85);
                if (!t86) goto lb83;
                {
                    uint64_t t89 = gm_l14;
                    uint64_t t90 = (uintptr_t)t89;
                    uint64_t t91 = GM_LD(uintptr_t, ((uint8_t *)&gm_l13) + 8);
                    gm_t6j6smj7qoctu t94 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l13)) + t90 * 32));
                    gm_l15 = t94;
                    {
                        gm_l16 = ((uint8_t)0);
                    }
                    {
                        {
                            gm_b16_8 t97 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l15) + 0));
                            gm_l17 = t97;
                        }
                        {
                            gm_l18 = ((uint64_t)0ULL);
                        }
                        {
                            for (;;) {
                                uint64_t t99 = gm_l18;
                                uint64_t t100 = GM_LD(uintptr_t, ((uint8_t *)&gm_l17) + 8);
                                uint8_t t101 = (t99 < t100);
                                if (!t101) goto lb98;
                                {
                                    uint64_t t104 = gm_l18;
                                    uint64_t t105 = (uintptr_t)t104;
                                    uint64_t t106 = GM_LD(uintptr_t, ((uint8_t *)&gm_l17) + 8);
                                    gm_b16_8 t109 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l17)) + t105 * 16));
                                    gm_l19 = t109;
                                    {
                                        {
                                            gm_b16_8 t112 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
                                            gm_l20 = t112;
                                        }
                                        {
                                            gm_l21 = ((uint64_t)0ULL);
                                        }
                                        {
                                            for (;;) {
                                                uint64_t t114 = gm_l21;
                                                uint64_t t115 = GM_LD(uintptr_t, ((uint8_t *)&gm_l20) + 8);
                                                uint8_t t116 = (t114 < t115);
                                                if (!t116) goto lb113;
                                                {
                                                    uint64_t t119 = gm_l21;
                                                    uint64_t t120 = (uintptr_t)t119;
                                                    uint64_t t121 = GM_LD(uintptr_t, ((uint8_t *)&gm_l20) + 8);
                                                    gm_b16_8 t124 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l20)) + t120 * 16));
                                                    gm_l22 = t124;
                                                    {
                                                        gm_b16_8 t127 = gm_l19;
                                                        gm_b16_8 t128 = gm_l22;
                                                        uint8_t t129 = (gm_eq0iqiubfm7e(((uint8_t *)&t127), ((uint8_t *)&t128)));
                                                        if (!t129) goto ie126;
                                                        {
                                                            gm_l16 = ((uint8_t)1);
                                                        }
                                                        goto ix126;
ie126:;
ix126:;
                                                    }
                                                    goto me122;
mn123:;
                                                    gm_trap("no match arm matched the scrutinee", gm_src3dhhc4laniae, 257, 13);
me122:;
                                                }
lc113:;
                                                {
                                                    uint64_t t132 = gm_l21;
                                                    uint64_t r133; bool o134 = __builtin_add_overflow(t132, ((uint64_t)1ULL), &r133);
                                                    uint64_t t135 = r133;
                                                    gm_l21 = t135;
                                                }
                                            }
lb113:;
                                        }
                                    }
                                    goto me107;
mn108:;
                                    gm_trap("no match arm matched the scrutinee", gm_src3dhhc4laniae, 256, 9);
me107:;
                                }
lc98:;
                                {
                                    uint64_t t137 = gm_l18;
                                    uint64_t r138; bool o139 = __builtin_add_overflow(t137, ((uint64_t)1ULL), &r138);
                                    uint64_t t140 = r138;
                                    gm_l18 = t140;
                                }
                            }
lb98:;
                        }
                    }
                    {
                        uint8_t t142 = gm_l16;
                        uint8_t t143 = (uint8_t)!t142;
                        if (!t143) goto ie141;
                        {
                            void * t145 = (void *)((uint8_t *)&F.l12);
                            gm_tftb1odc739t5 t146 = gm_f_push_52bt1f1(t145, &(*(gm_t6j6smj7qoctu *)((uint8_t *)&gm_l15)));
                            if (((((uint8_t *)&t146))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t146) + 1, gm_src3dhhc4laniae, 261, 27);
                            gm_b0_1 t147 = GM_LD(gm_b0_1, (((uint8_t *)&t146) + 1));
                        }
                        goto ix141;
ie141:;
ix141:;
                    }
                    goto me92;
mn93:;
                    gm_trap("no match arm matched the scrutinee", gm_src3dhhc4laniae, 254, 5);
me92:;
                }
lc83:;
                {
                    uint64_t t149 = gm_l14;
                    uint64_t r150; bool o151 = __builtin_add_overflow(t149, ((uint64_t)1ULL), &r150);
                    uint64_t t152 = r150;
                    gm_l14 = t152;
                }
            }
lb83:;
        }
    }
    {
        {
            gm_b16_8 t154 = gm_l2;
            gm_l23 = t154;
        }
        {
            gm_l24 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t156 = gm_l24;
                uint64_t t157 = GM_LD(uintptr_t, ((uint8_t *)&gm_l23) + 8);
                uint8_t t158 = (t156 < t157);
                if (!t158) goto lb155;
                {
                    uint64_t t161 = gm_l24;
                    uint64_t t162 = (uintptr_t)t161;
                    uint64_t t163 = GM_LD(uintptr_t, ((uint8_t *)&gm_l23) + 8);
                    gm_tk9n3vlila9qg t166 = GM_LD(gm_tk9n3vlila9qg, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l23)) + t162 * 40));
                    F.l25 = t166;
                    {
                        void * t168 = (void *)((uint8_t *)&F.l12);
                        gm_t6j6smj7qoctu t169;
                        gm_b16_8 t170 = GM_LD(gm_b16_8, (((uint8_t *)&F.l25) + 0));
                        GM_ST(gm_b16_8, (uint8_t *)&t169 + 0, t170);
                        gm_b16_8 t171 = GM_LD(gm_b16_8, (((uint8_t *)&F.l25) + 16));
                        GM_ST(gm_b16_8, (uint8_t *)&t169 + 16, t171);
                        gm_tftb1odc739t5 t172 = gm_f_push_52bt1f1(t168, &t169);
                        if (((((uint8_t *)&t172))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t172) + 1, gm_src3dhhc4laniae, 263, 22);
                        gm_b0_1 t173 = GM_LD(gm_b0_1, (((uint8_t *)&t172) + 1));
                    }
                    goto me164;
mn165:;
                    gm_trap("no match arm matched the scrutinee", gm_src3dhhc4laniae, 263, 5);
me164:;
                }
lc155:;
                {
                    uint64_t t175 = gm_l24;
                    uint64_t r176; bool o177 = __builtin_add_overflow(t175, ((uint64_t)1ULL), &r176);
                    uint64_t t178 = r176;
                    gm_l24 = t178;
                }
            }
lb155:;
        }
    }
    {
        void * t179 = gm_l0;
        gm_trqfvmjtt4sij t180 = F.l12;
        GM_ST(gm_trqfvmjtt4sij, (((uint8_t *)t179) + 48), t180);
    }
    {
        gm_th2lqbo77pvsc t181;
        memset(&t181, 0, sizeof t181);
        uint8_t t182 = 0;
        F.ret = t181;
        gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = t182;
        goto gm_ld1_1;
    }
    {
        void * t183 = (void *)((uint8_t *)&F.l11);
        gm_b0_1 t184 = gm_f_release_mdh37fq((void *)NULL, cx, NULL, t183);
    }
    goto gm_ls185;
gm_ld1_1:;
    {
        gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
        void * t186 = (void *)((uint8_t *)&F.l11);
        gm_b0_1 t187 = gm_f_release_mdh37fq((void *)NULL, cx, NULL, t186);
        gm_unw = gm_saved;
    }
    goto gm_lad;
gm_ls185:;
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
__attribute__((aligned(4))) gm_b0_1 gm_f_type_of_b1a5sco(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1) {
    struct gm_fr_type_of_b1a5sco F;
    void * gm_l0; /* r */
    gm_b16_8 gm_l1; /* source_text */
    gm_b16_8 gm_l4; /* expr */
    GM_STACK_CHECK(&F, 912u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        gm_f_replay_texts_3te9pi7((void *)&F.l2, (void *)NULL, cx, NULL, t2, ((uint8_t)0));
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
    }
    {
        void * t4 = gm_l0;
        gm_f_renamed_vars_fho5hvj((void *)&F.l3, (void *)NULL, cx, NULL, t4);
        if (gm_unw.kind) {
            goto gm_ld1_1;
        }
    }
    {
        gm_b16_8 t5 = gm_l1;
        gm_l4 = t5;
    }
    {
        uint64_t t7 = GM_LD(uintptr_t, (((uint8_t *)&F.l3) + 0) + 8);
        uint8_t t8 = (t7 > ((uint64_t)0ULL));
        if (!t8) goto ie6;
        {
            gm_b16_8 t10 = gm_l1;
            gm_b16_8 t11 = gm_f_empty_slice_q5bjns8();
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
            gm_b16_8 t13 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
            gm_b16_8 t14 = GM_LD(gm_b16_8, ((uint8_t *)&t13));
            gm_b16_8 t15 = gm_f_rename_47b7og5((void *)NULL, cx, NULL, t10, t12, t14);
            if (gm_unw.kind) {
                goto gm_ld1_1;
            }
            gm_l4 = t15;
        }
        goto ix6;
ie6:;
ix6:;
    }
    {
        gm_b16_8 t16;
        GM_ST(void *, (uint8_t *)&t16, (void *)(gm_skkiv5ia4t4au));
        GM_ST(uintptr_t, (uint8_t *)&t16 + 8, (uintptr_t)(38));
        GM_ST(gm_b16_8, (uint8_t *)&F.l5 + 0, t16);
        gm_b16_8 t17 = gm_l4;
        GM_ST(gm_b16_8, (uint8_t *)&F.l5 + 16, t17);
        gm_b16_8 t18;
        GM_ST(void *, (uint8_t *)&t18, (void *)(gm_squacltecdsom));
        GM_ST(uintptr_t, (uint8_t *)&t18 + 8, (uintptr_t)(4));
        GM_ST(gm_b16_8, (uint8_t *)&F.l5 + 32, t18);
    }
    {
        void * t19 = gm_l0;
        gm_th2lqbo77pvsc t20;
        memset(&t20, 0, sizeof t20);
        gm_b16_8 t21 = gm_f_empty_slice_5nn7ph0();
        gm_b16_8 t22 = GM_LD(gm_b16_8, ((uint8_t *)&t21));
        gm_b16_8 t23 = GM_LD(gm_b16_8, (((uint8_t *)&F.l2) + 0));
        gm_b16_8 t24 = GM_LD(gm_b16_8, ((uint8_t *)&t23));
        gm_tfeab279c16rr t25 = F.l5;
        gm_td6svc7gs468a t26;
        GM_ST(uint8_t, (uint8_t *)&t26 + 49, 0);
        GM_ST(uint16_t, (uint8_t *)&t26 + 50, 0);
        GM_ST(uint32_t, (uint8_t *)&t26 + 52, 0);
        GM_ST(gm_tfeab279c16rr, ((uint8_t *)&t26), t25);
        ((uint8_t *)&t26)[48] = 1;
        gm_b16_8 t27 = GM_LD(gm_b16_8, gm_cd231ebi1kg);
        gm_f_compile_rj09gp7((void *)&F.l6, (void *)NULL, cx, NULL, t19, &t20, t22, t24, &t26, t27, ((uint8_t)1));
        if (gm_unw.kind) {
            goto gm_ld1_1;
        }
    }
    {
        void * t29 = GM_LD(void *, (((uint8_t *)&F.l6) + 24));
        uint8_t t30;
        if ((GM_LD(void *, ((uint8_t *)&t29)) != 0)) goto mn32;
        t30 = ((uint8_t)1);
        goto me31;
mn32:;
        t30 = ((uint8_t)0);
        goto me31;
mn33:;
        gm_trap("no match arm matched the scrutinee", gm_src3dhhc4laniae, 378, 8);
me31:;
        if (!t30) goto ie28;
        {
            gm_b16_8 t35;
            GM_ST(void *, (uint8_t *)&t35, (void *)(gm_sojb2hhp0ln7r));
            GM_ST(uintptr_t, (uint8_t *)&t35 + 8, (uintptr_t)(30));
            GM_ST(gm_b16_8, (uint8_t *)&F.l7 + 0, t35);
            gm_b16_8 t36 = gm_l4;
            GM_ST(gm_b16_8, (uint8_t *)&F.l7 + 16, t36);
            gm_b16_8 t37;
            GM_ST(void *, (uint8_t *)&t37, (void *)(gm_s055t7hpo88bt));
            GM_ST(uintptr_t, (uint8_t *)&t37 + 8, (uintptr_t)(3));
            GM_ST(gm_b16_8, (uint8_t *)&F.l7 + 32, t37);
        }
        {
            void * t38 = gm_l0;
            gm_th2lqbo77pvsc t39;
            memset(&t39, 0, sizeof t39);
            gm_b16_8 t40 = gm_f_empty_slice_5nn7ph0();
            gm_b16_8 t41 = GM_LD(gm_b16_8, ((uint8_t *)&t40));
            gm_b16_8 t42 = GM_LD(gm_b16_8, (((uint8_t *)&F.l2) + 0));
            gm_b16_8 t43 = GM_LD(gm_b16_8, ((uint8_t *)&t42));
            gm_tfeab279c16rr t44 = F.l7;
            gm_td6svc7gs468a t45;
            GM_ST(uint8_t, (uint8_t *)&t45 + 49, 0);
            GM_ST(uint16_t, (uint8_t *)&t45 + 50, 0);
            GM_ST(uint32_t, (uint8_t *)&t45 + 52, 0);
            GM_ST(gm_tfeab279c16rr, ((uint8_t *)&t45), t44);
            ((uint8_t *)&t45)[48] = 1;
            gm_b16_8 t46 = GM_LD(gm_b16_8, gm_cd231ebi1kg);
            gm_f_compile_rj09gp7((void *)&F.l8, (void *)NULL, cx, NULL, t38, &t39, t41, t43, &t45, t46, ((uint8_t)1));
            if (gm_unw.kind) {
                goto gm_ld1_1;
            }
        }
        {
            void * t48 = GM_LD(void *, (((uint8_t *)&F.l8) + 24));
            uint8_t t49;
            if ((GM_LD(void *, ((uint8_t *)&t48)) != 0)) goto mn51;
            t49 = ((uint8_t)1);
            goto me50;
mn51:;
            t49 = ((uint8_t)0);
            goto me50;
mn52:;
            gm_trap("no match arm matched the scrutinee", gm_src3dhhc4laniae, 381, 12);
me50:;
            if (!t49) goto ie47;
            {
                void * t54 = (void *)((uint8_t *)&F.l6);
                void * t55 = gm_l0;
                gm_t9qkh0rsooc8d t56 = GM_LD(gm_t9qkh0rsooc8d, ((((uint8_t *)t55) + 0) + 2));
                gm_b16_8 t57 = gm_f_shown_o0bbr93((void *)NULL, cx, NULL, t54, t56);
                if (gm_unw.kind) {
                    goto gm_ld1_1;
                }
                gm_b0_1 t58 = gm_f_show_861qdlm((void *)NULL, cx, NULL, t57);
                if (gm_unw.kind) {
                    goto gm_ld1_1;
                }
            }
            {
                void * t59 = (void *)((uint8_t *)&F.l6);
                gm_b0_1 t60 = gm_f_release_mdh37fq((void *)NULL, cx, NULL, t59);
                if (gm_unw.kind) {
                    goto gm_ld1_1;
                }
            }
            {
                void * t61 = (void *)((uint8_t *)&F.l8);
                gm_b0_1 t62 = gm_f_release_mdh37fq((void *)NULL, cx, NULL, t61);
                if (gm_unw.kind) {
                    goto gm_ld1_1;
                }
            }
            {
                gm_b0_1 t63;
                uint8_t t64 = 0;
                F.ret = t63;
                gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = t64;
                goto gm_ld1_1;
            }
            goto ix47;
ie47:;
ix47:;
        }
        {
            void * t65 = (void *)((uint8_t *)&F.l6);
            gm_b0_1 t66 = gm_f_release_mdh37fq((void *)NULL, cx, NULL, t65);
            if (gm_unw.kind) {
                goto gm_ld1_1;
            }
        }
        {
            gm_toqvmi7jqorid t67 = F.l8;
            F.l6 = t67;
        }
        goto ix28;
ie28:;
ix28:;
    }
    {
        {
            void * t69 = (void *)((uint8_t *)&F.l6);
            void * t70 = gm_l0;
            gm_t9qkh0rsooc8d t71 = GM_LD(gm_t9qkh0rsooc8d, ((((uint8_t *)t70) + 0) + 1));
            void * t72 = gm_l0;
            uint64_t t73 = GM_LD(uintptr_t, ((((uint8_t *)t72) + 88) + 0) + 8);
            uint8_t t74 = (t73 > ((uint64_t)0ULL));
            uint8_t t75 = gm_f_run_aqa5hss((void *)NULL, cx, NULL, t69, t71, t74);
            if (gm_unw.kind) {
                goto gm_ld1_1;
            }
        }
    }
    {
        void * t76 = (void *)((uint8_t *)&F.l6);
        gm_b0_1 t77 = gm_f_release_mdh37fq((void *)NULL, cx, NULL, t76);
        if (gm_unw.kind) {
            goto gm_ld1_1;
        }
    }
    {
        void * t78 = (void *)((uint8_t *)&F.l2);
        gm_f_deinit_gvn47sj(t78);
    }
    goto gm_ls79;
gm_ld1_1:;
    {
        gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
        void * t80 = (void *)((uint8_t *)&F.l2);
        gm_f_deinit_gvn47sj(t80);
        gm_unw = gm_saved;
    }
    goto gm_lad;
gm_ls79:;
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
