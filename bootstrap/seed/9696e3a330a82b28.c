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
typedef struct gm_csia7oevr2u0fk { _Bool f0; unsigned __int128 f1; } gm_csia7oevr2u0fk;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint8_t f0; uint8_t p1[15]; unsigned __int128 f1; } gm_t86h6hhm6d76n;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint32_t f0; uint32_t f1; } gm_tcc1rq4n1avb2;
typedef struct { uint8_t f0; uint8_t p1[1]; uint16_t f1; } gm_tjcj4j2gbh6c8;
typedef struct { uint8_t f0; uint8_t p1[3]; uint32_t f1; } gm_ttovhfee8mf28;
_Static_assert(sizeof(gm_csguankko4lmhd) == 4 && _Alignof(gm_csguankko4lmhd) == 4 && offsetof(gm_csguankko4lmhd, f0) == 0, "layout of `Mutex`");
_Static_assert(sizeof(gm_csia7oevr2u0fk) == 32 && _Alignof(gm_csia7oevr2u0fk) == 16 && offsetof(gm_csia7oevr2u0fk, f0) == 0 && offsetof(gm_csia7oevr2u0fk, f1) == 16, "layout of `NumVal`");
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
_Static_assert(sizeof(gm_t86h6hhm6d76n) == 32 && _Alignof(gm_t86h6hhm6d76n) == 16 && offsetof(gm_t86h6hhm6d76n, f0) == 0 && offsetof(gm_t86h6hhm6d76n, f1) == 16, "layout of `NumVal`");
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { uint8_t f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; } gm_tbngattl1fo51;
_Static_assert(sizeof(gm_tcc1rq4n1avb2) == 8 && _Alignof(gm_tcc1rq4n1avb2) == 4 && offsetof(gm_tcc1rq4n1avb2, f0) == 0 && offsetof(gm_tcc1rq4n1avb2, f1) == 4, "layout of `LineCol`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tcmkompbl09il;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; gm_t88rcucg1b7uk f2; } gm_tcmtrmhj7ak4u;
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; } gm_tgglga8ro3isc;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
_Static_assert(sizeof(gm_tjcj4j2gbh6c8) == 4 && _Alignof(gm_tjcj4j2gbh6c8) == 2 && offsetof(gm_tjcj4j2gbh6c8, f0) == 0 && offsetof(gm_tjcj4j2gbh6c8, f1) == 2, "layout of `(signed: bool, bits: u16)`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t88rcucg1b7uk v0; } f1; } gm_tnnjeerolos0a;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { gm_t86h6hhm6d76n f0; uint8_t f1; uint8_t pe[15]; } gm_tsp9ul70rnkg6;
_Static_assert(sizeof(gm_ttovhfee8mf28) == 8 && _Alignof(gm_ttovhfee8mf28) == 4 && offsetof(gm_ttovhfee8mf28, f0) == 0 && offsetof(gm_ttovhfee8mf28, f1) == 4, "layout of `(signed: bool, bits: u32)`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_tcc1rq4n1avb2 v3; uint32_t v4; uint32_t v5; } f1; } gm_tvdg94bk7aa22;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tcmkompbl09il v0; gm_tiilmfd16v3fc v1; gm_b16_8 v2; } f1; } gm_t428ue2u3hf5g;
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; gm_tnnjeerolos0a f2; uint8_t pe[4]; } gm_t8nckeqfenl8a;
typedef struct { uint8_t f0; uint8_t p1[7]; gm_trqfvmjtt4sij f1; } gm_tbci5ospk5nnn;
_Static_assert(sizeof(gm_tbngattl1fo51) == 8 && _Alignof(gm_tbngattl1fo51) == 4 && offsetof(gm_tbngattl1fo51, f0) == 0 && offsetof(gm_tbngattl1fo51, f1) == 4, "layout of `(is_const: bool, pointee: Ty)`");
_Static_assert(sizeof(gm_tcmkompbl09il) == 24 && _Alignof(gm_tcmkompbl09il) == 8 && offsetof(gm_tcmkompbl09il, f0) == 0 && offsetof(gm_tcmkompbl09il, f1) == 8, "layout of `(ty: Ty, bytes: string)`");
_Static_assert(sizeof(gm_tcmtrmhj7ak4u) == 12 && _Alignof(gm_tcmtrmhj7ak4u) == 4 && offsetof(gm_tcmtrmhj7ak4u, f0) == 0 && offsetof(gm_tcmtrmhj7ak4u, f1) == 4 && offsetof(gm_tcmtrmhj7ak4u, f2) == 8, "layout of `(op: LenOp, lhs: Ty, rhs: Ty)`");
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t p2[4]; gm_tnpifv20irnph f2; uint8_t f3; uint8_t pe[7]; } gm_tddtjfggdat0d;
_Static_assert(sizeof(gm_tgglga8ro3isc) == 8 && _Alignof(gm_tgglga8ro3isc) == 4 && offsetof(gm_tgglga8ro3isc, f0) == 0 && offsetof(gm_tgglga8ro3isc, f1) == 4, "layout of `(err: Ty, ok: Ty)`");
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
_Static_assert(sizeof(gm_tnnjeerolos0a) == 8 && _Alignof(gm_tnnjeerolos0a) == 4 && offsetof(gm_tnnjeerolos0a, f0) == 0 && offsetof(gm_tnnjeerolos0a, f1) == 4, "layout of `ArrayKind`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_tsp9ul70rnkg6) == 48 && _Alignof(gm_tsp9ul70rnkg6) == 16 && offsetof(gm_tsp9ul70rnkg6, f0) == 0 && offsetof(gm_tsp9ul70rnkg6, f1) == 32, "layout of `?NumVal`");
_Static_assert(sizeof(gm_tvdg94bk7aa22) == 12 && _Alignof(gm_tvdg94bk7aa22) == 4 && offsetof(gm_tvdg94bk7aa22, f0) == 0 && offsetof(gm_tvdg94bk7aa22, f1) == 4, "layout of `Builtin`");
_Static_assert(sizeof(gm_t428ue2u3hf5g) == 32 && _Alignof(gm_t428ue2u3hf5g) == 8 && offsetof(gm_t428ue2u3hf5g, f0) == 0 && offsetof(gm_t428ue2u3hf5g, f1) == 8, "layout of `ConstKey`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tjcj4j2gbh6c8 v0; uint16_t v1; gm_tbngattl1fo51 v2; gm_tbngattl1fo51 v3; gm_tbngattl1fo51 v4; gm_t8nckeqfenl8a v5; gm_t6j6smj7qoctu v6; gm_t88rcucg1b7uk v7; gm_tgglga8ro3isc v8; gm_tddtjfggdat0d v9; gm_tddtjfggdat0d v10; gm_tiilmfd16v3fc v11; gm_t88rcucg1b7uk v12; uint32_t v13; gm_tgglga8ro3isc v14; gm_t428ue2u3hf5g v15; gm_tcmtrmhj7ak4u v16; uint32_t v17; gm_tnpifv20irnph v18; uint32_t v19; } f1; } gm_t53at80pfdus3;
_Static_assert(sizeof(gm_t8nckeqfenl8a) == 24 && _Alignof(gm_t8nckeqfenl8a) == 8 && offsetof(gm_t8nckeqfenl8a, f0) == 0 && offsetof(gm_t8nckeqfenl8a, f1) == 8 && offsetof(gm_t8nckeqfenl8a, f2) == 12, "layout of `(len: u64, elem: Ty, kind: ArrayKind)`");
_Static_assert(sizeof(gm_tbci5ospk5nnn) == 48 && _Alignof(gm_tbci5ospk5nnn) == 8 && offsetof(gm_tbci5ospk5nnn, f0) == 0 && offsetof(gm_tbci5ospk5nnn, f1) == 8, "layout of `Int`");
_Static_assert(sizeof(gm_tddtjfggdat0d) == 48 && _Alignof(gm_tddtjfggdat0d) == 8 && offsetof(gm_tddtjfggdat0d, f0) == 0 && offsetof(gm_tddtjfggdat0d, f1) == 16 && offsetof(gm_tddtjfggdat0d, f2) == 24 && offsetof(gm_tddtjfggdat0d, f3) == 40, "layout of `FnTy`");
typedef struct { gm_tbci5ospk5nnn f0; gm_tbci5ospk5nnn f1; } gm_tngtuiuskn938;
_Static_assert(sizeof(gm_t53at80pfdus3) == 56 && _Alignof(gm_t53at80pfdus3) == 8 && offsetof(gm_t53at80pfdus3, f0) == 0 && offsetof(gm_t53at80pfdus3, f1) == 8, "layout of `TyKind`");
_Static_assert(sizeof(gm_tngtuiuskn938) == 96 && _Alignof(gm_tngtuiuskn938) == 8 && offsetof(gm_tngtuiuskn938, f0) == 0 && offsetof(gm_tngtuiuskn938, f1) == 48, "layout of `Rat`");
struct gm_fr_big_float_lh5u3gu {
    gm_frame h;
    gm_tbci5ospk5nnn l1; /* i */
    double ret;
};
struct gm_fr_float_width_a6a9p1v {
    gm_frame h;
    uint16_t ret;
};
struct gm_fr_from_int_ofjuoqh {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* i */
    gm_tngtuiuskn938 ret;
};
struct gm_fr_get_float_grmhlm4 {
    gm_frame h;
    double ret;
};
struct gm_fr_kind_htv6093 {
    gm_frame h;
    gm_t53at80pfdus3 ret;
};
struct gm_fr_kind_jcnt4hm {
    gm_frame h;
    gm_t53at80pfdus3 ret;
};
struct gm_fr_num_bits_onl28vp {
    gm_frame h;
    unsigned __int128 ret;
};
struct gm_fr_num_ext_348kmbu {
    gm_frame h;
    unsigned __int128 ret;
};
struct gm_fr_num_f16_to_f64_h70suv4 {
    gm_frame h;
    double ret;
};
struct gm_fr_num_f64_to_f16_3g33m05 {
    gm_frame h;
    uint16_t ret;
};
struct gm_fr_num_fits_1n7pcsh {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_num_float_int_ba5kc1t {
    gm_frame h;
    gm_tsp9ul70rnkg6 ret;
};
struct gm_fr_num_fmax_0qau2jp {
    gm_frame h;
    double ret;
};
struct gm_fr_num_fmin_1k5uqp0 {
    gm_frame h;
    double ret;
};
struct gm_fr_num_int_to_float_4hrvis6 {
    gm_frame h;
    double ret;
};
struct gm_fr_num_integral_qbts4a6 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_num_is_nan_b0ma7mo {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_num_mask_do3naah {
    gm_frame h;
    unsigned __int128 ret;
};
struct gm_fr_num_round_2pna7m3 {
    gm_frame h;
    double ret;
};
struct gm_fr_num_sat_66dqcpl {
    gm_frame h;
    gm_t86h6hhm6d76n ret;
};
struct gm_fr_num_val_i3upeur {
    gm_frame h;
    gm_t86h6hhm6d76n ret;
};
struct gm_fr_one_08qg1op {
    gm_frame h;
    gm_tbci5ospk5nnn ret;
};
struct gm_fr_put_float_fiad72d {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_rd_int_iuioodt {
    gm_frame h;
    unsigned __int128 ret;
};
struct gm_fr_rd_uint_n7l65o8 {
    gm_frame h;
    unsigned __int128 ret;
};
struct gm_fr_wr_uint_rs9ado5 {
    gm_frame h;
    gm_b0_1 ret;
};
inline __attribute__((aligned(4))) double gm_f_big_float_lh5u3gu(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) uint16_t gm_f_float_width_a6a9p1v(void * a0, gm_csguankko4lmhd a1, uint64_t a2);
inline __attribute__((aligned(4))) void gm_f_from_int_ofjuoqh(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_from_u64_ud13q5v(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint64_t a0);
__attribute__((aligned(4))) double gm_f_get_float_grmhlm4(void * a0, gm_csguankko4lmhd a1, gm_b16_8 a2);
__attribute__((aligned(4))) unsigned __int128 gm_f_get_uint_bq8c4b7(void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) void gm_f_kind_htv6093(void *gm_out, void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void gm_f_kind_jcnt4hm(void *gm_out, void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) unsigned __int128 gm_f_num_bits_onl28vp(const void * a0);
__attribute__((aligned(4))) unsigned __int128 gm_f_num_bits_onl28vp_c(gm_csia7oevr2u0fk a0);
__attribute__((aligned(4))) unsigned __int128 gm_f_num_ext_348kmbu(uint8_t a0, uint32_t a1, unsigned __int128 a2);
__attribute__((aligned(4))) double gm_f_num_f16_to_f64_h70suv4(uint16_t a0);
__attribute__((aligned(4))) uint16_t gm_f_num_f64_to_f16_3g33m05(double a0);
__attribute__((aligned(4))) uint8_t gm_f_num_fits_1n7pcsh(const void * a0, uint8_t a1, uint32_t a2);
__attribute__((aligned(4))) uint8_t gm_f_num_fits_1n7pcsh_c(gm_csia7oevr2u0fk a0, uint8_t a1, uint32_t a2);
__attribute__((aligned(4))) void gm_f_num_float_int_ba5kc1t(void *gm_out, double a0, gm_tvdg94bk7aa22 a1);
__attribute__((aligned(4))) gm_tsp9ul70rnkg6 gm_f_num_float_int_ba5kc1t_c(double a0, gm_tvdg94bk7aa22 a1);
__attribute__((aligned(4))) double gm_f_num_fmax_0qau2jp(double a0, double a1);
__attribute__((aligned(4))) double gm_f_num_fmin_1k5uqp0(double a0, double a1);
__attribute__((aligned(4))) double gm_f_num_int_to_float_4hrvis6(const void * a0, uint16_t a1);
__attribute__((aligned(4))) double gm_f_num_int_to_float_4hrvis6_c(gm_csia7oevr2u0fk a0, uint16_t a1);
__attribute__((aligned(4))) uint8_t gm_f_num_integral_qbts4a6(double a0);
inline __attribute__((aligned(4))) uint8_t gm_f_num_is_nan_b0ma7mo(double a0);
inline __attribute__((aligned(4))) unsigned __int128 gm_f_num_mask_do3naah(uint32_t a0);
inline __attribute__((aligned(4))) double gm_f_num_round_2pna7m3(double a0, uint16_t a1);
__attribute__((aligned(4))) void gm_f_num_sat_66dqcpl(void *gm_out, const void * a0, uint8_t a1, uint32_t a2);
__attribute__((aligned(4))) gm_csia7oevr2u0fk gm_f_num_sat_66dqcpl_c(gm_csia7oevr2u0fk a0, uint8_t a1, uint32_t a2);
__attribute__((aligned(4))) void gm_f_num_val_i3upeur(void *gm_out, uint8_t a0, unsigned __int128 a1);
__attribute__((aligned(4))) gm_csia7oevr2u0fk gm_f_num_val_i3upeur_c(uint8_t a0, unsigned __int128 a1);
inline __attribute__((aligned(4))) void gm_f_one_08qg1op(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_put_float_fiad72d(void * a0, gm_csguankko4lmhd a1, double a2, gm_b16_8 a3);
__attribute__((aligned(4))) void gm_f_put_uint_cddin99(void * a0, gm_b16_8 a1, unsigned __int128 a2);
__attribute__((aligned(4))) unsigned __int128 gm_f_rd_int_iuioodt(void * a0, gm_ttovhfee8mf28 a1, gm_b16_8 a2);
__attribute__((aligned(4))) unsigned __int128 gm_f_rd_uint_n7l65o8(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) double gm_f_to_f64_4avhrht(void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_wr_uint_rs9ado5(void * a0, gm_b16_8 a1, unsigned __int128 a2);
extern const char gm_src0ih77f43mhf2[];
extern const char gm_src9pvgnpibl2nm[];
inline __attribute__((aligned(4))) double gm_f_big_float_lh5u3gu(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_big_float_lh5u3gu F;
    void * gm_l0; /* _m */
    GM_STACK_CHECK(&F, 128u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tngtuiuskn938 t1;
    gm_f_from_int_ofjuoqh((void *)&t1, (void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)((uint8_t *)a1)));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    double t2 = gm_f_to_f64_4avhrht((void *)NULL, cx, NULL, &t1);
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
inline __attribute__((aligned(4))) void gm_f_from_int_ofjuoqh(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_from_int_ofjuoqh F;
    GM_STACK_CHECK(&F, 96u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memcpy(&F.l0, a0, sizeof F.l0);
    gm_tbci5ospk5nnn t1 = F.l0;
    gm_tbci5ospk5nnn t2;
    gm_f_one_08qg1op((void *)&t2, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    GM_ST(gm_tbci5ospk5nnn, (uint8_t *)&(*(gm_tngtuiuskn938 *)gm_out) + 0, t1);
    GM_ST(gm_tbci5ospk5nnn, (uint8_t *)&(*(gm_tngtuiuskn938 *)gm_out) + 48, t2);
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
inline __attribute__((aligned(4))) void gm_f_kind_htv6093(void *gm_out, void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_kind_htv6093 F;
    void * gm_l0; /* m */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK_SHALLOW(&F, 40u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    void * t1 = gm_l0;
    void * t2 = GM_LD(void *, (((uint8_t *)t1) + 8));
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
inline __attribute__((aligned(4))) unsigned __int128 gm_f_num_bits_onl28vp(const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_num_bits_onl28vp F;
    gm_t86h6hhm6d76n gm_l0; /* x */
    GM_STACK_CHECK_SHALLOW(&F, 161u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    unsigned __int128 t1;
    uint8_t t3 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (!t3) goto ie2;
    unsigned __int128 t5 = GM_LD(unsigned __int128, (((uint8_t *)a0) + 16));
    unsigned __int128 r6; bool o7 = __builtin_sub_overflow(((unsigned __int128)(((unsigned __int128)0u << 64) | 0u)), t5, &r6);
    unsigned __int128 t8 = r6;
    unsigned __int128 t9 = t8;
    t1 = t9;
    goto ix2;
ie2:;
    unsigned __int128 t11 = GM_LD(unsigned __int128, (((uint8_t *)a0) + 16));
    unsigned __int128 t12 = t11;
    t1 = t12;
ix2:;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_num_is_nan_b0ma7mo(double a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_num_is_nan_b0ma7mo F;
    double gm_l0; /* x */
    GM_STACK_CHECK_SHALLOW(&F, 27u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    double t1 = gm_l0;
    double t2 = gm_l0;
    uint8_t t3 = (!(t1 == t2));
    F.ret = t3;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) unsigned __int128 gm_f_num_mask_do3naah(uint32_t a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_num_mask_do3naah F;
    uint32_t gm_l0; /* bits */
    GM_STACK_CHECK_SHALLOW(&F, 157u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    unsigned __int128 t1;
    uint32_t t3 = gm_l0;
    uint8_t t4 = (t3 >= ((uint32_t)128ULL));
    if (!t4) goto ie2;
    unsigned __int128 t6 = (unsigned __int128)~((unsigned __int128)(((unsigned __int128)0u << 64) | 0u));
    unsigned __int128 t7 = t6;
    t1 = t7;
    goto ix2;
ie2:;
    uint32_t t9 = gm_l0;
    unsigned __int128 t10 = (unsigned __int128)t9;
    unsigned __int128 n11 = (unsigned __int128)t10;
    if (n11 >= 128) gm_trap_shift((uint64_t)n11, "u128", gm_src9pvgnpibl2nm, 8, 74);
    unsigned __int128 r12 = (unsigned __int128)((unsigned __int128)((unsigned __int128)(((unsigned __int128)0u << 64) | 1u)) << (int)n11);
    bool o13 = (unsigned __int128)(r12 >> (int)n11) != ((unsigned __int128)(((unsigned __int128)0u << 64) | 1u));
    if (o13) gm_trap("integer overflow in `<<`", gm_src9pvgnpibl2nm, 8, 74);
    unsigned __int128 t14 = r12;
    unsigned __int128 r15; bool o16 = __builtin_sub_overflow(t14, ((unsigned __int128)(((unsigned __int128)0u << 64) | 1u)), &r15);
    if (o16) gm_trap("integer overflow in `-`", gm_src9pvgnpibl2nm, 8, 73);
    unsigned __int128 t17 = r15;
    unsigned __int128 t18 = t17;
    t1 = t18;
ix2:;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) double gm_f_num_round_2pna7m3(double a0, uint16_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_num_round_2pna7m3 F;
    double gm_l0; /* x */
    uint16_t gm_l1; /* width */
    GM_STACK_CHECK_SHALLOW(&F, 81u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint16_t t3 = gm_l1;
        uint8_t t4 = ((t3 == ((uint16_t)32ULL)));
        if (!t4) goto ie2;
        {
            double t6 = gm_l0;
            float t7 = (float)((double)t6);
            double t8 = (double)t7;
            uint8_t t9 = 0;
            F.ret = t8;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        uint16_t t11 = gm_l1;
        uint8_t t12 = ((t11 == ((uint16_t)16ULL)));
        if (!t12) goto ie10;
        {
            double t14 = gm_l0;
            uint16_t t15 = gm_f_num_f64_to_f16_3g33m05(t14);
            double t16 = gm_f_num_f16_to_f64_h70suv4(t15);
            uint8_t t17 = 0;
            F.ret = t16;
            goto gm_fret;
        }
        goto ix10;
ie10:;
ix10:;
    }
    {
        double t18 = gm_l0;
        uint8_t t19 = 0;
        F.ret = t18;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_one_08qg1op(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_one_08qg1op F;
    GM_STACK_CHECK(&F, 0u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_f_from_u64_ud13q5v((void *)&(*(gm_tbci5ospk5nnn *)gm_out), (void *)NULL, cx, NULL, ((uint64_t)1ULL));
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
extern __attribute__((aligned(4))) double gm_f_big_float_lh5u3gu(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
inline __attribute__((aligned(4))) uint16_t gm_f_float_width_a6a9p1v(void * a0, gm_csguankko4lmhd a1, uint64_t a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_float_width_a6a9p1v F;
    void * gm_l0; /* m */
    gm_t88rcucg1b7uk gm_l1; /* t */
    uint64_t gm_l2; /* n */
    uint16_t gm_l3; /* w */
    GM_STACK_CHECK_SHALLOW(&F, 185u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    gm_l2 = a2;
    {
        void * t3 = gm_l0;
        gm_t88rcucg1b7uk t4 = gm_l1;
        gm_t53at80pfdus3 t5;
        gm_f_kind_htv6093((void *)&t5, t3, GM_CONV(gm_csguankko4lmhd, t4));
        gm_t53at80pfdus3 t6 = t5;
        uint8_t t7 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
        if (t7 != ((uint8_t)3ULL)) goto ie2;
        uint16_t t8 = GM_LD(uint16_t, (((uint8_t *)&t6) + 8));
        gm_l3 = t8;
        {
            uint16_t t10 = gm_l3;
            uint8_t t11 = 0;
            F.ret = t10;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        uint16_t t12;
        uint64_t t14 = gm_l2;
        uint8_t t15 = ((t14 == ((uint64_t)2ULL)));
        if (!t15) goto ie13;
        uint16_t t17 = ((uint16_t)16ULL);
        t12 = t17;
        goto ix13;
ie13:;
        uint16_t t18;
        uint64_t t20 = gm_l2;
        uint8_t t21 = ((t20 == ((uint64_t)4ULL)));
        if (!t21) goto ie19;
        uint16_t t23 = ((uint16_t)32ULL);
        t18 = t23;
        goto ix19;
ie19:;
        uint16_t t25 = ((uint16_t)64ULL);
        t18 = t25;
ix19:;
        t12 = t18;
ix13:;
        uint8_t t26 = 0;
        F.ret = t12;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) double gm_f_get_float_grmhlm4(void * a0, gm_csguankko4lmhd a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_get_float_grmhlm4 F;
    void * gm_l0; /* m */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_b16_8 gm_l2; /* b */
    uint16_t gm_l3; /* w */
    GM_STACK_CHECK_SHALLOW(&F, 347u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        gm_t88rcucg1b7uk t3 = gm_l1;
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        uint16_t t5 = gm_f_float_width_a6a9p1v(t2, GM_CONV(gm_csguankko4lmhd, t3), t4);
        gm_l3 = t5;
    }
    {
        uint16_t t7 = gm_l3;
        uint8_t t8 = ((t7 == ((uint16_t)16ULL)));
        if (!t8) goto ie6;
        {
            void * t10 = gm_l0;
            uint64_t t11 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
            void * t12 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)));
            uint64_t t13 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t14 = (uintptr_t)((uint64_t)2ULL);
            if (t13 > t14 || t14 > (uintptr_t)t11) gm_trap_slice(t13, t14, t11, gm_src9pvgnpibl2nm, 181, 51);
            gm_b16_8 t15;
            GM_ST(void *, (uint8_t *)&t15, (void *)(((uint8_t *)t12 + t13 * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t15 + 8, (uintptr_t)(t14 - t13));
            unsigned __int128 t16 = gm_f_get_uint_bq8c4b7(t10, t15);
            uint16_t t17 = (uint16_t)t16;
            double t18 = gm_f_num_f16_to_f64_h70suv4(t17);
            uint8_t t19 = 0;
            F.ret = t18;
            goto gm_fret;
        }
        goto ix6;
ie6:;
ix6:;
    }
    {
        uint16_t t21 = gm_l3;
        uint8_t t22 = ((t21 == ((uint16_t)32ULL)));
        if (!t22) goto ie20;
        {
            void * t24 = gm_l0;
            uint64_t t25 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
            void * t26 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)));
            uint64_t t27 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t28 = (uintptr_t)((uint64_t)4ULL);
            if (t27 > t28 || t28 > (uintptr_t)t25) gm_trap_slice(t27, t28, t25, gm_src9pvgnpibl2nm, 182, 40);
            gm_b16_8 t29;
            GM_ST(void *, (uint8_t *)&t29, (void *)(((uint8_t *)t26 + t27 * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t29 + 8, (uintptr_t)(t28 - t27));
            unsigned __int128 t30 = gm_f_get_uint_bq8c4b7(t24, t29);
            uint32_t t31 = (uint32_t)t30;
            uint32_t t32 = t31;
            float t33;
            memset(&t33, 0, sizeof t33); memcpy(&t33, ((uint8_t *)&t32), sizeof t33 < 4 ? sizeof t33 : 4);
            double t34 = (double)t33;
            uint8_t t35 = 0;
            F.ret = t34;
            goto gm_fret;
        }
        goto ix20;
ie20:;
ix20:;
    }
    {
        void * t36 = gm_l0;
        uint64_t t37 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        void * t38 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)));
        uint64_t t39 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t40 = (uintptr_t)((uint64_t)8ULL);
        if (t39 > t40 || t40 > (uintptr_t)t37) gm_trap_slice(t39, t40, t37, gm_src9pvgnpibl2nm, 183, 23);
        gm_b16_8 t41;
        GM_ST(void *, (uint8_t *)&t41, (void *)(((uint8_t *)t38 + t39 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t41 + 8, (uintptr_t)(t40 - t39));
        unsigned __int128 t42 = gm_f_get_uint_bq8c4b7(t36, t41);
        uint64_t t43 = (uint64_t)t42;
        uint64_t t44 = t43;
        double t45;
        memset(&t45, 0, sizeof t45); memcpy(&t45, ((uint8_t *)&t44), sizeof t45 < 8 ? sizeof t45 : 8);
        uint8_t t46 = 0;
        F.ret = t45;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) unsigned __int128 gm_f_num_bits_onl28vp(const void * a0);
__attribute__((aligned(4))) unsigned __int128 gm_f_num_bits_onl28vp_c(gm_csia7oevr2u0fk a0) { return gm_f_num_bits_onl28vp(&a0); }
inline __attribute__((aligned(4))) unsigned __int128 gm_f_num_ext_348kmbu(uint8_t a0, uint32_t a1, unsigned __int128 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_num_ext_348kmbu F;
    uint8_t gm_l0; /* signed */
    uint32_t gm_l1; /* bits */
    unsigned __int128 gm_l2; /* v */
    unsigned __int128 gm_l3; /* masked */
    GM_STACK_CHECK_SHALLOW(&F, 303u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        uint32_t t3 = gm_l1;
        uint8_t t4 = (t3 >= ((uint32_t)128ULL));
        if (!t4) goto ie2;
        {
            unsigned __int128 t6 = gm_l2;
            uint8_t t7 = 0;
            F.ret = t6;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        unsigned __int128 t8 = gm_l2;
        uint32_t t9 = gm_l1;
        unsigned __int128 t10 = gm_f_num_mask_do3naah(t9);
        unsigned __int128 t11 = (unsigned __int128)(t8 & t10);
        gm_l3 = t11;
    }
    {
        uint8_t t13 = gm_l0;
        if (!t13) goto ie12;
        unsigned __int128 t14 = gm_l3;
        uint32_t t15 = gm_l1;
        uint32_t r16; bool o17 = __builtin_sub_overflow(t15, ((uint32_t)1ULL), &r16);
        if (o17) gm_trap("integer overflow in `-`", gm_src9pvgnpibl2nm, 15, 35);
        uint32_t t18 = r16;
        unsigned __int128 t19 = (unsigned __int128)t18;
        unsigned __int128 n20 = (unsigned __int128)t19;
        if (n20 >= 128) gm_trap_shift((uint64_t)n20, "u128", gm_src9pvgnpibl2nm, 15, 20);
        unsigned __int128 t21 = (unsigned __int128)(t14 >> (int)n20);
        unsigned __int128 t22 = (unsigned __int128)(t21 & ((unsigned __int128)(((unsigned __int128)0u << 64) | 1u)));
        uint8_t t23 = ((t22 == ((unsigned __int128)(((unsigned __int128)0u << 64) | 1u))));
        if (!t23) goto ie12;
        {
            unsigned __int128 t25 = gm_l3;
            uint32_t t26 = gm_l1;
            unsigned __int128 t27 = gm_f_num_mask_do3naah(t26);
            unsigned __int128 t28 = (unsigned __int128)~t27;
            unsigned __int128 t29 = (unsigned __int128)(t25 | t28);
            uint8_t t30 = 0;
            F.ret = t29;
            goto gm_fret;
        }
        goto ix12;
ie12:;
ix12:;
    }
    {
        unsigned __int128 t31 = gm_l3;
        uint8_t t32 = 0;
        F.ret = t31;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) double gm_f_num_f16_to_f64_h70suv4(uint16_t a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_num_f16_to_f64_h70suv4 F;
    uint16_t gm_l0; /* h */
    uint64_t gm_l1; /* sign */
    uint64_t gm_l2; /* exp */
    uint64_t gm_l3; /* man */
    double gm_l4; /* v */
    GM_STACK_CHECK_SHALLOW(&F, 376u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint16_t t2 = gm_l0;
        uint16_t t3 = (uint16_t)(t2 & ((uint16_t)32768ULL));
        uint64_t t4 = (uint64_t)t3;
        unsigned __int128 n5 = (unsigned __int128)((uint64_t)48ULL);
        if (n5 >= 64) gm_trap_shift((uint64_t)n5, "u64", gm_src9pvgnpibl2nm, 115, 13);
        uint64_t r6 = (uint64_t)((uint64_t)t4 << (int)n5);
        bool o7 = (uint64_t)(r6 >> (int)n5) != t4;
        if (o7) gm_trap("integer overflow in `<<`", gm_src9pvgnpibl2nm, 115, 13);
        uint64_t t8 = r6;
        gm_l1 = t8;
    }
    {
        uint16_t t9 = gm_l0;
        unsigned __int128 n10 = (unsigned __int128)((uint64_t)10ULL);
        if (n10 >= 16) gm_trap_shift((uint64_t)n10, "u16", gm_src9pvgnpibl2nm, 116, 17);
        uint16_t t11 = (uint16_t)(t9 >> (int)n10);
        uint16_t t12 = (uint16_t)(t11 & ((uint16_t)31ULL));
        uint64_t t13 = (uint64_t)t12;
        gm_l2 = t13;
    }
    {
        uint16_t t14 = gm_l0;
        uint16_t t15 = (uint16_t)(t14 & ((uint16_t)1023ULL));
        uint64_t t16 = (uint64_t)t15;
        gm_l3 = t16;
    }
    {
        uint64_t t18 = gm_l2;
        uint8_t t19 = ((t18 == ((uint64_t)0ULL)));
        if (!t19) goto ie17;
        {
            uint64_t t21 = gm_l3;
            double t22 = (double)(t21);
            double t23 = (t22 * ((double)5.960464477539063e-8));
            gm_l4 = t23;
        }
        {
            double t24;
            uint64_t t26 = gm_l1;
            uint8_t t27 = ((t26 == ((uint64_t)0ULL)));
            if (!t27) goto ie25;
            double t29 = gm_l4;
            double t30 = t29;
            t24 = t30;
            goto ix25;
ie25:;
            double t32 = gm_l4;
            double t33 = -t32;
            double t34 = t33;
            t24 = t34;
ix25:;
            uint8_t t35 = 0;
            F.ret = t24;
            goto gm_fret;
        }
        goto ix17;
ie17:;
ix17:;
    }
    {
        uint64_t t37 = gm_l2;
        uint8_t t38 = ((t37 == ((uint64_t)31ULL)));
        if (!t38) goto ie36;
        {
            uint64_t t41 = gm_l3;
            uint8_t t42 = ((t41 == ((uint64_t)0ULL)));
            if (!t42) goto ie40;
            {
                uint64_t t44 = gm_l1;
                uint64_t t45 = (uint64_t)(t44 | ((uint64_t)9218868437227405312ULL));
                uint64_t t46 = t45;
                double t47;
                memset(&t47, 0, sizeof t47); memcpy(&t47, ((uint8_t *)&t46), sizeof t47 < 8 ? sizeof t47 : 8);
                uint8_t t48 = 0;
                F.ret = t47;
                goto gm_fret;
            }
            goto ix40;
ie40:;
ix40:;
        }
        {
            uint64_t t49 = gm_l1;
            uint64_t t50 = (uint64_t)(t49 | ((uint64_t)9221120237041090560ULL));
            uint64_t t51 = gm_l3;
            unsigned __int128 n52 = (unsigned __int128)((uint64_t)42ULL);
            if (n52 >= 64) gm_trap_shift((uint64_t)n52, "u64", gm_src9pvgnpibl2nm, 125, 49);
            uint64_t r53 = (uint64_t)((uint64_t)t51 << (int)n52);
            bool o54 = (uint64_t)(r53 >> (int)n52) != t51;
            if (o54) gm_trap("integer overflow in `<<`", gm_src9pvgnpibl2nm, 125, 49);
            uint64_t t55 = r53;
            uint64_t t56 = (uint64_t)(t50 | t55);
            uint64_t t57 = t56;
            double t58;
            memset(&t58, 0, sizeof t58); memcpy(&t58, ((uint8_t *)&t57), sizeof t58 < 8 ? sizeof t58 : 8);
            uint8_t t59 = 0;
            F.ret = t58;
            goto gm_fret;
        }
        goto ix36;
ie36:;
ix36:;
    }
    {
        uint64_t t60 = gm_l1;
        uint64_t t61 = gm_l2;
        uint64_t r62; bool o63 = __builtin_add_overflow(t61, ((uint64_t)1008ULL), &r62);
        if (o63) gm_trap("integer overflow in `+`", gm_src9pvgnpibl2nm, 127, 22);
        uint64_t t64 = r62;
        unsigned __int128 n65 = (unsigned __int128)((uint64_t)52ULL);
        if (n65 >= 64) gm_trap_shift((uint64_t)n65, "u64", gm_src9pvgnpibl2nm, 127, 21);
        uint64_t r66 = (uint64_t)((uint64_t)t64 << (int)n65);
        bool o67 = (uint64_t)(r66 >> (int)n65) != t64;
        if (o67) gm_trap("integer overflow in `<<`", gm_src9pvgnpibl2nm, 127, 21);
        uint64_t t68 = r66;
        uint64_t t69 = (uint64_t)(t60 | t68);
        uint64_t t70 = gm_l3;
        unsigned __int128 n71 = (unsigned __int128)((uint64_t)42ULL);
        if (n71 >= 64) gm_trap_shift((uint64_t)n71, "u64", gm_src9pvgnpibl2nm, 127, 44);
        uint64_t r72 = (uint64_t)((uint64_t)t70 << (int)n71);
        bool o73 = (uint64_t)(r72 >> (int)n71) != t70;
        if (o73) gm_trap("integer overflow in `<<`", gm_src9pvgnpibl2nm, 127, 44);
        uint64_t t74 = r72;
        uint64_t t75 = (uint64_t)(t69 | t74);
        uint64_t t76 = t75;
        double t77;
        memset(&t77, 0, sizeof t77); memcpy(&t77, ((uint8_t *)&t76), sizeof t77 < 8 ? sizeof t77 : 8);
        uint8_t t78 = 0;
        F.ret = t77;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) uint16_t gm_f_num_f64_to_f16_3g33m05(double a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_num_f64_to_f16_3g33m05 F;
    double gm_l0; /* x */
    uint64_t gm_l1; /* bits */
    uint16_t gm_l2; /* sign */
    int64_t gm_l3; /* exp */
    uint64_t gm_l4; /* man */
    int64_t gm_l5; /* e */
    int64_t gm_l6; /* he */
    uint32_t gm_l7; /* keep */
    uint64_t gm_l8; /* rem */
    uint64_t gm_l9; /* half */
    uint32_t gm_l10; /* r */
    unsigned __int128 gm_l11; /* mm */
    int64_t gm_l12; /* shift */
    unsigned __int128 gm_l13; /* s */
    unsigned __int128 gm_l14; /* q */
    unsigned __int128 gm_l15; /* rem */
    unsigned __int128 gm_l16; /* half */
    uint32_t gm_l17; /* r */
    GM_STACK_CHECK_SHALLOW(&F, 937u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        double t2 = gm_l0;
        double t3 = t2;
        uint64_t t4;
        memset(&t4, 0, sizeof t4); memcpy(&t4, ((uint8_t *)&t3), sizeof t4 < 8 ? sizeof t4 : 8);
        gm_l1 = t4;
    }
    {
        uint64_t t5 = gm_l1;
        unsigned __int128 n6 = (unsigned __int128)((uint64_t)63ULL);
        if (n6 >= 64) gm_trap_shift((uint64_t)n6, "u64", gm_src9pvgnpibl2nm, 133, 14);
        uint64_t t7 = (uint64_t)(t5 >> (int)n6);
        uint16_t t8 = (uint16_t)t7;
        unsigned __int128 n9 = (unsigned __int128)((uint64_t)15ULL);
        if (n9 >= 16) gm_trap_shift((uint64_t)n9, "u16", gm_src9pvgnpibl2nm, 133, 13);
        uint16_t r10 = (uint16_t)((uint16_t)t8 << (int)n9);
        bool o11 = (uint16_t)(r10 >> (int)n9) != t8;
        if (o11) gm_trap("integer overflow in `<<`", gm_src9pvgnpibl2nm, 133, 13);
        uint16_t t12 = r10;
        gm_l2 = t12;
    }
    {
        uint64_t t13 = gm_l1;
        unsigned __int128 n14 = (unsigned __int128)((uint64_t)52ULL);
        if (n14 >= 64) gm_trap_shift((uint64_t)n14, "u64", gm_src9pvgnpibl2nm, 134, 14);
        uint64_t t15 = (uint64_t)(t13 >> (int)n14);
        uint64_t t16 = (uint64_t)(t15 & ((uint64_t)2047ULL));
        uint64_t t17 = t16;
        int64_t t18;
        memset(&t18, 0, sizeof t18); memcpy(&t18, ((uint8_t *)&t17), sizeof t18 < 8 ? sizeof t18 : 8);
        gm_l3 = t18;
    }
    {
        uint64_t t19 = gm_l1;
        uint64_t t20 = (uint64_t)(t19 & ((uint64_t)4503599627370495ULL));
        gm_l4 = t20;
    }
    {
        int64_t t22 = gm_l3;
        uint8_t t23 = ((t22 == ((int64_t)2047LL)));
        if (!t23) goto ie21;
        {
            uint64_t t26 = gm_l4;
            uint8_t t27 = ((t26 == ((uint64_t)0ULL)));
            if (!t27) goto ie25;
            {
                uint16_t t29 = gm_l2;
                uint16_t t30 = (uint16_t)(t29 | ((uint16_t)31744ULL));
                uint8_t t31 = 0;
                F.ret = t30;
                goto gm_fret;
            }
            goto ix25;
ie25:;
ix25:;
        }
        {
            uint16_t t32 = gm_l2;
            uint16_t t33 = (uint16_t)(t32 | ((uint16_t)32256ULL));
            uint64_t t34 = gm_l4;
            unsigned __int128 n35 = (unsigned __int128)((uint64_t)42ULL);
            if (n35 >= 64) gm_trap_shift((uint64_t)n35, "u64", gm_src9pvgnpibl2nm, 139, 33);
            uint64_t t36 = (uint64_t)(t34 >> (int)n35);
            uint16_t t37 = (uint16_t)t36;
            uint16_t t38 = (uint16_t)(t33 | t37);
            uint8_t t39 = 0;
            F.ret = t38;
            goto gm_fret;
        }
        goto ix21;
ie21:;
ix21:;
    }
    {
        int64_t t41 = gm_l3;
        uint8_t t42 = ((t41 == ((int64_t)0LL)));
        if (!t42) goto ie40;
        {
            uint16_t t44 = gm_l2;
            uint8_t t45 = 0;
            F.ret = t44;
            goto gm_fret;
        }
        goto ix40;
ie40:;
ix40:;
    }
    {
        int64_t t46 = gm_l3;
        int64_t r47; bool o48 = __builtin_sub_overflow(t46, ((int64_t)1023LL), &r47);
        if (o48) gm_trap("integer overflow in `-`", gm_src9pvgnpibl2nm, 142, 10);
        int64_t t49 = r47;
        gm_l5 = t49;
    }
    {
        int64_t t50 = gm_l5;
        int64_t r51; bool o52 = __builtin_add_overflow(t50, ((int64_t)15LL), &r51);
        if (o52) gm_trap("integer overflow in `+`", gm_src9pvgnpibl2nm, 143, 11);
        int64_t t53 = r51;
        gm_l6 = t53;
    }
    {
        int64_t t55 = gm_l6;
        uint8_t t56 = (t55 >= ((int64_t)1LL));
        if (!t56) goto ie54;
        {
            int64_t t59 = gm_l6;
            uint8_t t60 = (t59 >= ((int64_t)31LL));
            if (!t60) goto ie58;
            {
                uint16_t t62 = gm_l2;
                uint16_t t63 = (uint16_t)(t62 | ((uint16_t)31744ULL));
                uint8_t t64 = 0;
                F.ret = t63;
                goto gm_fret;
            }
            goto ix58;
ie58:;
ix58:;
        }
        {
            uint64_t t65 = gm_l4;
            unsigned __int128 n66 = (unsigned __int128)((uint64_t)42ULL);
            if (n66 >= 64) gm_trap_shift((uint64_t)n66, "u64", gm_src9pvgnpibl2nm, 146, 18);
            uint64_t t67 = (uint64_t)(t65 >> (int)n66);
            uint32_t t68 = (uint32_t)t67;
            gm_l7 = t68;
        }
        {
            uint64_t t69 = gm_l4;
            unsigned __int128 n70 = (unsigned __int128)((uint64_t)42ULL);
            if (n70 >= 64) gm_trap_shift((uint64_t)n70, "u64", gm_src9pvgnpibl2nm, 147, 24);
            uint64_t r71 = (uint64_t)((uint64_t)((uint64_t)1ULL) << (int)n70);
            bool o72 = (uint64_t)(r71 >> (int)n70) != ((uint64_t)1ULL);
            if (o72) gm_trap("integer overflow in `<<`", gm_src9pvgnpibl2nm, 147, 24);
            uint64_t t73 = r71;
            uint64_t r74; bool o75 = __builtin_sub_overflow(t73, ((uint64_t)1ULL), &r74);
            if (o75) gm_trap("integer overflow in `-`", gm_src9pvgnpibl2nm, 147, 23);
            uint64_t t76 = r74;
            uint64_t t77 = (uint64_t)(t69 & t76);
            gm_l8 = t77;
        }
        {
            unsigned __int128 n78 = (unsigned __int128)((uint64_t)41ULL);
            if (n78 >= 64) gm_trap_shift((uint64_t)n78, "u64", gm_src9pvgnpibl2nm, 148, 17);
            uint64_t r79 = (uint64_t)((uint64_t)((uint64_t)1ULL) << (int)n78);
            bool o80 = (uint64_t)(r79 >> (int)n78) != ((uint64_t)1ULL);
            if (o80) gm_trap("integer overflow in `<<`", gm_src9pvgnpibl2nm, 148, 17);
            uint64_t t81 = r79;
            gm_l9 = t81;
        }
        {
            int64_t t82 = gm_l6;
            uint32_t t83 = (uint32_t)t82;
            unsigned __int128 n84 = (unsigned __int128)((uint64_t)10ULL);
            if (n84 >= 32) gm_trap_shift((uint64_t)n84, "u32", gm_src9pvgnpibl2nm, 149, 15);
            uint32_t r85 = (uint32_t)((uint32_t)t83 << (int)n84);
            bool o86 = (uint32_t)(r85 >> (int)n84) != t83;
            if (o86) gm_trap("integer overflow in `<<`", gm_src9pvgnpibl2nm, 149, 15);
            uint32_t t87 = r85;
            uint32_t t88 = gm_l7;
            uint32_t t89 = (uint32_t)(t87 | t88);
            gm_l10 = t89;
        }
        {
            uint8_t t91 = 0;
            uint64_t t92 = gm_l8;
            uint64_t t93 = gm_l9;
            uint8_t t94 = (t92 > t93);
            if (!t94) {
                uint8_t t95 = 0;
                uint64_t t96 = gm_l8;
                uint64_t t97 = gm_l9;
                uint8_t t98 = ((t96 == t97));
                if (t98) {
                    uint32_t t99 = gm_l7;
                    uint32_t t100 = (uint32_t)(t99 & ((uint32_t)1ULL));
                    uint8_t t101 = ((t100 == ((uint32_t)1ULL)));
                    t95 = t101;
                } else t95 = 0;
                t91 = t95;
            } else t91 = 1;
            if (!t91) goto ie90;
            {
                uint32_t t103 = gm_l10;
                uint32_t r104; bool o105 = __builtin_add_overflow(t103, ((uint32_t)1ULL), &r104);
                if (o105) gm_trap("integer overflow in `+`", gm_src9pvgnpibl2nm, 150, 60);
                uint32_t t106 = r104;
                gm_l10 = t106;
            }
            goto ix90;
ie90:;
ix90:;
        }
        {
            uint16_t t107 = gm_l2;
            uint32_t t108 = gm_l10;
            uint16_t t109 = (uint16_t)t108;
            uint16_t t110 = (uint16_t)(t107 | t109);
            uint8_t t111 = 0;
            F.ret = t110;
            goto gm_fret;
        }
        goto ix54;
ie54:;
ix54:;
    }
    {
        unsigned __int128 n112 = (unsigned __int128)((uint64_t)52ULL);
        if (n112 >= 128) gm_trap_shift((uint64_t)n112, "u128", gm_src9pvgnpibl2nm, 153, 12);
        unsigned __int128 r113 = (unsigned __int128)((unsigned __int128)((unsigned __int128)(((unsigned __int128)0u << 64) | 1u)) << (int)n112);
        bool o114 = (unsigned __int128)(r113 >> (int)n112) != ((unsigned __int128)(((unsigned __int128)0u << 64) | 1u));
        if (o114) gm_trap("integer overflow in `<<`", gm_src9pvgnpibl2nm, 153, 12);
        unsigned __int128 t115 = r113;
        uint64_t t116 = gm_l4;
        unsigned __int128 t117 = (unsigned __int128)t116;
        unsigned __int128 t118 = (unsigned __int128)(t115 | t117);
        gm_l11 = t118;
    }
    {
        int64_t t119 = gm_l5;
        int64_t r120; bool o121 = __builtin_sub_overflow(((int64_t)28LL), t119, &r120);
        if (o121) gm_trap("integer overflow in `-`", gm_src9pvgnpibl2nm, 154, 14);
        int64_t t122 = r120;
        gm_l12 = t122;
    }
    {
        int64_t t124 = gm_l12;
        uint8_t t125 = (t124 >= ((int64_t)120LL));
        if (!t125) goto ie123;
        {
            uint16_t t127 = gm_l2;
            uint8_t t128 = 0;
            F.ret = t127;
            goto gm_fret;
        }
        goto ix123;
ie123:;
ix123:;
    }
    {
        int64_t t129 = gm_l12;
        uint32_t t130 = (uint32_t)t129;
        unsigned __int128 t131 = (unsigned __int128)t130;
        gm_l13 = t131;
    }
    {
        unsigned __int128 t132 = gm_l11;
        unsigned __int128 t133 = gm_l13;
        unsigned __int128 n134 = (unsigned __int128)t133;
        if (n134 >= 128) gm_trap_shift((uint64_t)n134, "u128", gm_src9pvgnpibl2nm, 157, 10);
        unsigned __int128 t135 = (unsigned __int128)(t132 >> (int)n134);
        gm_l14 = t135;
    }
    {
        unsigned __int128 t136 = gm_l11;
        unsigned __int128 t137 = gm_l13;
        unsigned __int128 n138 = (unsigned __int128)t137;
        if (n138 >= 128) gm_trap_shift((uint64_t)n138, "u128", gm_src9pvgnpibl2nm, 158, 19);
        unsigned __int128 r139 = (unsigned __int128)((unsigned __int128)((unsigned __int128)(((unsigned __int128)0u << 64) | 1u)) << (int)n138);
        bool o140 = (unsigned __int128)(r139 >> (int)n138) != ((unsigned __int128)(((unsigned __int128)0u << 64) | 1u));
        if (o140) gm_trap("integer overflow in `<<`", gm_src9pvgnpibl2nm, 158, 19);
        unsigned __int128 t141 = r139;
        unsigned __int128 r142; bool o143 = __builtin_sub_overflow(t141, ((unsigned __int128)(((unsigned __int128)0u << 64) | 1u)), &r142);
        if (o143) gm_trap("integer overflow in `-`", gm_src9pvgnpibl2nm, 158, 18);
        unsigned __int128 t144 = r142;
        unsigned __int128 t145 = (unsigned __int128)(t136 & t144);
        gm_l15 = t145;
    }
    {
        unsigned __int128 t146 = gm_l13;
        unsigned __int128 r147; bool o148 = __builtin_sub_overflow(t146, ((unsigned __int128)(((unsigned __int128)0u << 64) | 1u)), &r147);
        if (o148) gm_trap("integer overflow in `-`", gm_src9pvgnpibl2nm, 159, 25);
        unsigned __int128 t149 = r147;
        unsigned __int128 n150 = (unsigned __int128)t149;
        if (n150 >= 128) gm_trap_shift((uint64_t)n150, "u128", gm_src9pvgnpibl2nm, 159, 13);
        unsigned __int128 r151 = (unsigned __int128)((unsigned __int128)((unsigned __int128)(((unsigned __int128)0u << 64) | 1u)) << (int)n150);
        bool o152 = (unsigned __int128)(r151 >> (int)n150) != ((unsigned __int128)(((unsigned __int128)0u << 64) | 1u));
        if (o152) gm_trap("integer overflow in `<<`", gm_src9pvgnpibl2nm, 159, 13);
        unsigned __int128 t153 = r151;
        gm_l16 = t153;
    }
    {
        unsigned __int128 t154 = gm_l14;
        uint32_t t155 = (uint32_t)t154;
        gm_l17 = t155;
    }
    {
        uint8_t t157 = 0;
        unsigned __int128 t158 = gm_l15;
        unsigned __int128 t159 = gm_l16;
        uint8_t t160 = (t158 > t159);
        if (!t160) {
            uint8_t t161 = 0;
            unsigned __int128 t162 = gm_l15;
            unsigned __int128 t163 = gm_l16;
            uint8_t t164 = ((t162 == t163));
            if (t164) {
                uint32_t t165 = gm_l17;
                uint32_t t166 = (uint32_t)(t165 & ((uint32_t)1ULL));
                uint8_t t167 = ((t166 == ((uint32_t)1ULL)));
                t161 = t167;
            } else t161 = 0;
            t157 = t161;
        } else t157 = 1;
        if (!t157) goto ie156;
        {
            uint32_t t169 = gm_l17;
            uint32_t r170; bool o171 = __builtin_add_overflow(t169, ((uint32_t)1ULL), &r170);
            if (o171) gm_trap("integer overflow in `+`", gm_src9pvgnpibl2nm, 161, 53);
            uint32_t t172 = r170;
            gm_l17 = t172;
        }
        goto ix156;
ie156:;
ix156:;
    }
    {
        uint16_t t173 = gm_l2;
        uint32_t t174 = gm_l17;
        uint16_t t175 = (uint16_t)t174;
        uint16_t t176 = (uint16_t)(t173 | t175);
        uint8_t t177 = 0;
        F.ret = t176;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_num_fits_1n7pcsh(const void * a0, uint8_t a1, uint32_t a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_num_fits_1n7pcsh F;
    gm_t86h6hhm6d76n gm_l0; /* x */
    uint8_t gm_l1; /* signed */
    uint32_t gm_l2; /* bits */
    unsigned __int128 gm_l3; /* lim */
    GM_STACK_CHECK_SHALLOW(&F, 208u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        uint8_t t3 = gm_l1;
        if (!t3) goto ie2;
        {
            uint32_t t5 = gm_l2;
            uint32_t r6; bool o7 = __builtin_sub_overflow(t5, ((uint32_t)1ULL), &r6);
            if (o7) gm_trap("integer overflow in `-`", gm_src9pvgnpibl2nm, 37, 32);
            uint32_t t8 = r6;
            unsigned __int128 t9 = (unsigned __int128)t8;
            unsigned __int128 n10 = (unsigned __int128)t9;
            if (n10 >= 128) gm_trap_shift((uint64_t)n10, "u128", gm_src9pvgnpibl2nm, 37, 16);
            unsigned __int128 r11 = (unsigned __int128)((unsigned __int128)((unsigned __int128)(((unsigned __int128)0u << 64) | 1u)) << (int)n10);
            bool o12 = (unsigned __int128)(r11 >> (int)n10) != ((unsigned __int128)(((unsigned __int128)0u << 64) | 1u));
            if (o12) gm_trap("integer overflow in `<<`", gm_src9pvgnpibl2nm, 37, 16);
            unsigned __int128 t13 = r11;
            gm_l3 = t13;
        }
        {
            uint8_t t14;
            uint8_t t16 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
            if (!t16) goto ie15;
            unsigned __int128 t18 = GM_LD(unsigned __int128, (((uint8_t *)a0) + 16));
            unsigned __int128 t19 = gm_l3;
            uint8_t t20 = (t18 <= t19);
            uint8_t t21 = t20;
            t14 = t21;
            goto ix15;
ie15:;
            unsigned __int128 t23 = GM_LD(unsigned __int128, (((uint8_t *)a0) + 16));
            unsigned __int128 t24 = gm_l3;
            uint8_t t25 = (t23 < t24);
            uint8_t t26 = t25;
            t14 = t26;
ix15:;
            uint8_t t27 = 0;
            F.ret = t14;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        uint8_t t28 = 0;
        uint8_t t29 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
        uint8_t t30 = (uint8_t)!t29;
        if (t30) {
            unsigned __int128 t31 = GM_LD(unsigned __int128, (((uint8_t *)a0) + 16));
            uint32_t t32 = gm_l2;
            unsigned __int128 t33 = gm_f_num_mask_do3naah(t32);
            uint8_t t34 = (t31 <= t33);
            t28 = t34;
        } else t28 = 0;
        uint8_t t35 = 0;
        F.ret = t28;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) uint8_t gm_f_num_fits_1n7pcsh_c(gm_csia7oevr2u0fk a0, uint8_t a1, uint32_t a2) { return gm_f_num_fits_1n7pcsh(&a0, a1, a2); }
__attribute__((aligned(4))) void gm_f_num_float_int_ba5kc1t(void *gm_out, double a0, gm_tvdg94bk7aa22 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_num_float_int_ba5kc1t F;
    double gm_l0; /* x */
    gm_tvdg94bk7aa22 gm_l1; /* op */
    uint64_t gm_l2; /* raw */
    uint8_t gm_l3; /* neg */
    int64_t gm_l4; /* e */
    uint64_t gm_l5; /* man */
    int64_t gm_l6; /* sh */
    unsigned __int128 gm_l7; /* mag */
    int64_t gm_l8; /* k */
    uint64_t gm_l9; /* q */
    uint64_t gm_l10; /* rem */
    uint64_t gm_l11; /* half */
    uint8_t gm_l12; /* up */
    uint8_t gm_l13; /* inexact */
    GM_STACK_CHECK_SHALLOW(&F, 912u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        double t2 = gm_l0;
        double t3 = t2;
        uint64_t t4;
        memset(&t4, 0, sizeof t4); memcpy(&t4, ((uint8_t *)&t3), sizeof t4 < 8 ? sizeof t4 : 8);
        gm_l2 = t4;
    }
    {
        uint64_t t5 = gm_l2;
        unsigned __int128 n6 = (unsigned __int128)((uint64_t)63ULL);
        if (n6 >= 64) gm_trap_shift((uint64_t)n6, "u64", gm_src9pvgnpibl2nm, 60, 13);
        uint64_t t7 = (uint64_t)(t5 >> (int)n6);
        uint8_t t8 = ((t7 == ((uint64_t)1ULL)));
        gm_l3 = t8;
    }
    {
        uint64_t t9 = gm_l2;
        unsigned __int128 n10 = (unsigned __int128)((uint64_t)52ULL);
        if (n10 >= 64) gm_trap_shift((uint64_t)n10, "u64", gm_src9pvgnpibl2nm, 61, 12);
        uint64_t t11 = (uint64_t)(t9 >> (int)n10);
        uint64_t t12 = (uint64_t)(t11 & ((uint64_t)2047ULL));
        uint64_t t13 = t12;
        int64_t t14;
        memset(&t14, 0, sizeof t14); memcpy(&t14, ((uint8_t *)&t13), sizeof t14 < 8 ? sizeof t14 : 8);
        gm_l4 = t14;
    }
    {
        uint64_t t15 = gm_l2;
        uint64_t t16 = (uint64_t)(t15 & ((uint64_t)4503599627370495ULL));
        gm_l5 = t16;
    }
    {
        int64_t t18 = gm_l4;
        uint8_t t19 = ((t18 == ((int64_t)0LL)));
        if (!t19) goto ie17;
        {
            gm_l4 = ((int64_t)1LL);
        }
        goto ix17;
ie17:;
        {
            uint64_t t22 = gm_l5;
            unsigned __int128 n23 = (unsigned __int128)((uint64_t)52ULL);
            if (n23 >= 64) gm_trap_shift((uint64_t)n23, "u64", gm_src9pvgnpibl2nm, 66, 16);
            uint64_t r24 = (uint64_t)((uint64_t)((uint64_t)1ULL) << (int)n23);
            bool o25 = (uint64_t)(r24 >> (int)n23) != ((uint64_t)1ULL);
            if (o25) gm_trap("integer overflow in `<<`", gm_src9pvgnpibl2nm, 66, 16);
            uint64_t t26 = r24;
            uint64_t t27 = (uint64_t)(t22 | t26);
            gm_l5 = t27;
        }
ix17:;
    }
    {
        int64_t t28 = gm_l4;
        int64_t r29; bool o30 = __builtin_sub_overflow(t28, ((int64_t)1075LL), &r29);
        if (o30) gm_trap("integer overflow in `-`", gm_src9pvgnpibl2nm, 69, 11);
        int64_t t31 = r29;
        gm_l6 = t31;
    }
    {
        gm_l7 = ((unsigned __int128)(((unsigned __int128)0u << 64) | 0u));
    }
    {
        int64_t t33 = gm_l6;
        uint8_t t34 = (t33 >= ((int64_t)0LL));
        if (!t34) goto ie32;
        {
            uint64_t t37 = gm_l5;
            uint8_t t38 = ((t37 == ((uint64_t)0ULL)));
            if (!t38) goto ie36;
            {
                gm_t86h6hhm6d76n t40;
                memset((uint8_t *)&t40 + 1, 0, 15);
                GM_ST(uint8_t, (uint8_t *)&t40 + 0, ((uint8_t)0));
                GM_ST(unsigned __int128, (uint8_t *)&t40 + 16, ((unsigned __int128)(((unsigned __int128)0u << 64) | 0u)));
                gm_tsp9ul70rnkg6 t41;
                GM_ST(uint8_t, (uint8_t *)&t41 + 33, 0);
                GM_ST(uint16_t, (uint8_t *)&t41 + 34, 0);
                GM_ST(uint32_t, (uint8_t *)&t41 + 36, 0);
                GM_ST(uint64_t, (uint8_t *)&t41 + 40, 0);
                GM_ST(gm_t86h6hhm6d76n, ((uint8_t *)&t41), t40);
                ((uint8_t *)&t41)[32] = 1;
                uint8_t t42 = 0;
                F.ret = t41;
                goto gm_fret;
            }
            goto ix36;
ie36:;
ix36:;
        }
        {
            uint8_t t44 = 0;
            int64_t t45 = gm_l6;
            uint8_t t46 = (t45 >= ((int64_t)128LL));
            if (!t46) {
                uint64_t t47 = gm_l5;
                uint32_t t48 = (uint32_t)gm_clz(((unsigned __int128)(uint64_t)(t47) & (((unsigned __int128)1 << 64) - 1)), 64);
                uint32_t r49; bool o50 = __builtin_sub_overflow(((uint32_t)64ULL), t48, &r49);
                if (o50) gm_trap("integer overflow in `-`", gm_src9pvgnpibl2nm, 73, 29);
                uint32_t t51 = r49;
                int64_t t52 = (int64_t)t51;
                int64_t t53 = gm_l6;
                int64_t r54; bool o55 = __builtin_add_overflow(t52, t53, &r54);
                if (o55) gm_trap("integer overflow in `+`", gm_src9pvgnpibl2nm, 73, 25);
                int64_t t56 = r54;
                uint8_t t57 = (t56 > ((int64_t)128LL));
                t44 = t57;
            } else t44 = 1;
            if (!t44) goto ie43;
            {
                gm_tsp9ul70rnkg6 t59;
                memset(&t59, 0, sizeof t59);
                uint8_t t60 = 0;
                F.ret = t59;
                goto gm_fret;
            }
            goto ix43;
ie43:;
ix43:;
        }
        {
            uint64_t t61 = gm_l5;
            unsigned __int128 t62 = (unsigned __int128)t61;
            int64_t t63 = gm_l6;
            unsigned __int128 t64 = (unsigned __int128)t63;
            unsigned __int128 n65 = (unsigned __int128)t64;
            if (n65 >= 128) gm_trap_shift((uint64_t)n65, "u128", gm_src9pvgnpibl2nm, 74, 15);
            unsigned __int128 r66 = (unsigned __int128)((unsigned __int128)t62 << (int)n65);
            bool o67 = (unsigned __int128)(r66 >> (int)n65) != t62;
            if (o67) gm_trap("integer overflow in `<<`", gm_src9pvgnpibl2nm, 74, 15);
            unsigned __int128 t68 = r66;
            gm_l7 = t68;
        }
        goto ix32;
ie32:;
        {
            int64_t t70 = gm_l6;
            if (t70 == ((int64_t)(-9223372036854775807LL - 1))) gm_trap("integer overflow in `-`", gm_src9pvgnpibl2nm, 76, 14);
            int64_t t71 = (int64_t)((uint64_t)0 - (uint64_t)t70);
            gm_l8 = t71;
        }
        {
            gm_l9 = ((uint64_t)0ULL);
        }
        {
            gm_l10 = ((uint64_t)0ULL);
        }
        {
            gm_l11 = ((uint64_t)0ULL);
        }
        {
            int64_t t73 = gm_l8;
            uint8_t t74 = (t73 >= ((int64_t)64LL));
            if (!t74) goto ie72;
            {
                uint64_t t76 = gm_l5;
                gm_l10 = t76;
            }
            {
                gm_l11 = ((uint64_t)0ULL);
            }
            goto ix72;
ie72:;
            {
                uint64_t t78 = gm_l5;
                int64_t t79 = gm_l8;
                uint64_t t80 = (uint64_t)t79;
                unsigned __int128 n81 = (unsigned __int128)t80;
                if (n81 >= 64) gm_trap_shift((uint64_t)n81, "u64", gm_src9pvgnpibl2nm, 85, 17);
                uint64_t t82 = (uint64_t)(t78 >> (int)n81);
                gm_l9 = t82;
            }
            {
                uint64_t t83 = gm_l5;
                int64_t t84 = gm_l8;
                uint64_t t85 = (uint64_t)t84;
                unsigned __int128 n86 = (unsigned __int128)t85;
                if (n86 >= 64) gm_trap_shift((uint64_t)n86, "u64", gm_src9pvgnpibl2nm, 86, 27);
                uint64_t r87 = (uint64_t)((uint64_t)((uint64_t)1ULL) << (int)n86);
                bool o88 = (uint64_t)(r87 >> (int)n86) != ((uint64_t)1ULL);
                if (o88) gm_trap("integer overflow in `<<`", gm_src9pvgnpibl2nm, 86, 27);
                uint64_t t89 = r87;
                uint64_t r90; bool o91 = __builtin_sub_overflow(t89, ((uint64_t)1ULL), &r90);
                if (o91) gm_trap("integer overflow in `-`", gm_src9pvgnpibl2nm, 86, 26);
                uint64_t t92 = r90;
                uint64_t t93 = (uint64_t)(t83 & t92);
                gm_l10 = t93;
            }
            {
                int64_t t94 = gm_l8;
                int64_t r95; bool o96 = __builtin_sub_overflow(t94, ((int64_t)1LL), &r95);
                if (o96) gm_trap("integer overflow in `-`", gm_src9pvgnpibl2nm, 87, 31);
                int64_t t97 = r95;
                uint64_t t98 = (uint64_t)t97;
                unsigned __int128 n99 = (unsigned __int128)t98;
                if (n99 >= 64) gm_trap_shift((uint64_t)n99, "u64", gm_src9pvgnpibl2nm, 87, 20);
                uint64_t r100 = (uint64_t)((uint64_t)((uint64_t)1ULL) << (int)n99);
                bool o101 = (uint64_t)(r100 >> (int)n99) != ((uint64_t)1ULL);
                if (o101) gm_trap("integer overflow in `<<`", gm_src9pvgnpibl2nm, 87, 20);
                uint64_t t102 = r100;
                gm_l11 = t102;
            }
ix72:;
        }
        {
            gm_l12 = ((uint8_t)0);
        }
        {
            uint64_t t103 = gm_l10;
            uint8_t t104 = (!(t103 == ((uint64_t)0ULL)));
            gm_l13 = t104;
        }
        {
            uint8_t t111 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
            switch (t111) { case ((uint8_t)0ULL): goto ma109; case ((uint8_t)1ULL): goto ma109; case ((uint8_t)2ULL): goto ma108; case ((uint8_t)3ULL): goto ma106; case ((uint8_t)4ULL): goto ma107; case ((uint8_t)5ULL): goto ma109; case ((uint8_t)6ULL): goto ma109; case ((uint8_t)7ULL): goto ma109; case ((uint8_t)8ULL): goto ma109; case ((uint8_t)9ULL): goto ma109; case ((uint8_t)10ULL): goto ma109; case ((uint8_t)11ULL): goto ma109; case ((uint8_t)12ULL): goto ma109; case ((uint8_t)13ULL): goto ma109; case ((uint8_t)14ULL): goto ma109; case ((uint8_t)15ULL): goto ma109; case ((uint8_t)16ULL): goto ma109; case ((uint8_t)17ULL): goto ma109; case ((uint8_t)18ULL): goto ma109; case ((uint8_t)19ULL): goto ma109; case ((uint8_t)20ULL): goto ma109; case ((uint8_t)21ULL): goto ma109; case ((uint8_t)22ULL): goto ma109; case ((uint8_t)23ULL): goto ma109; case ((uint8_t)24ULL): goto ma109; case ((uint8_t)25ULL): goto ma109; case ((uint8_t)26ULL): goto ma109; case ((uint8_t)27ULL): goto ma109; case ((uint8_t)28ULL): goto ma109; case ((uint8_t)29ULL): goto ma109; case ((uint8_t)30ULL): goto ma109; case ((uint8_t)31ULL): goto ma109; case ((uint8_t)32ULL): goto ma109; case ((uint8_t)33ULL): goto ma109; case ((uint8_t)34ULL): goto ma109; case ((uint8_t)35ULL): goto ma109; case ((uint8_t)36ULL): goto ma109; case ((uint8_t)37ULL): goto ma109; case ((uint8_t)38ULL): goto ma109; case ((uint8_t)39ULL): goto ma109; case ((uint8_t)40ULL): goto ma109; case ((uint8_t)41ULL): goto ma109; case ((uint8_t)42ULL): goto ma109; case ((uint8_t)43ULL): goto ma109; case ((uint8_t)44ULL): goto ma109; case ((uint8_t)45ULL): goto ma109; case ((uint8_t)46ULL): goto ma109; case ((uint8_t)47ULL): goto ma109; case ((uint8_t)48ULL): goto ma109; case ((uint8_t)49ULL): goto ma109; case ((uint8_t)50ULL): goto ma109; case ((uint8_t)51ULL): goto ma109; case ((uint8_t)52ULL): goto ma109; case ((uint8_t)53ULL): goto ma109; case ((uint8_t)54ULL): goto ma109; case ((uint8_t)55ULL): goto ma109; case ((uint8_t)56ULL): goto ma109; case ((uint8_t)57ULL): goto ma109; case ((uint8_t)58ULL): goto ma109; case ((uint8_t)59ULL): goto ma109; case ((uint8_t)60ULL): goto ma109; case ((uint8_t)61ULL): goto ma109; case ((uint8_t)62ULL): goto ma109; case ((uint8_t)63ULL): goto ma109; case ((uint8_t)64ULL): goto ma109; case ((uint8_t)65ULL): goto ma109; case ((uint8_t)66ULL): goto ma109; case ((uint8_t)67ULL): goto ma109; case ((uint8_t)68ULL): goto ma109; case ((uint8_t)69ULL): goto ma109; case ((uint8_t)70ULL): goto ma109; case ((uint8_t)71ULL): goto ma109; case ((uint8_t)72ULL): goto ma109; case ((uint8_t)73ULL): goto ma109; case ((uint8_t)74ULL): goto ma109; case ((uint8_t)75ULL): goto ma109; case ((uint8_t)76ULL): goto ma109; case ((uint8_t)77ULL): goto ma109; case ((uint8_t)78ULL): goto ma109; case ((uint8_t)79ULL): goto ma109; case ((uint8_t)80ULL): goto ma109; case ((uint8_t)81ULL): goto ma109; default: goto ma109; }
ma106:;
            uint8_t t113 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
            if (t113 != ((uint8_t)3ULL)) goto mn112;
            {
                uint8_t t115 = 0;
                uint8_t t116 = gm_l3;
                if (t116) {
                    uint8_t t117 = gm_l13;
                    t115 = t117;
                } else t115 = 0;
                gm_l12 = t115;
            }
            goto me105;
mn112:;
ma107:;
            uint8_t t119 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
            if (t119 != ((uint8_t)4ULL)) goto mn118;
            {
                uint8_t t121 = 0;
                uint8_t t122 = gm_l3;
                uint8_t t123 = (uint8_t)!t122;
                if (t123) {
                    uint8_t t124 = gm_l13;
                    t121 = t124;
                } else t121 = 0;
                gm_l12 = t121;
            }
            goto me105;
mn118:;
ma108:;
            uint8_t t126 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
            if (t126 != ((uint8_t)2ULL)) goto mn125;
            {
                int64_t t129 = gm_l8;
                uint8_t t130 = (t129 >= ((int64_t)64LL));
                if (!t130) goto ie128;
                {
                    gm_l12 = ((uint8_t)0);
                }
                goto ix128;
ie128:;
                {
                    uint8_t t133 = 0;
                    uint64_t t134 = gm_l10;
                    uint64_t t135 = gm_l11;
                    uint8_t t136 = (t134 > t135);
                    if (!t136) {
                        uint8_t t137 = 0;
                        uint64_t t138 = gm_l10;
                        uint64_t t139 = gm_l11;
                        uint8_t t140 = ((t138 == t139));
                        if (t140) {
                            uint64_t t141 = gm_l9;
                            uint64_t t142 = (uint64_t)(t141 & ((uint64_t)1ULL));
                            uint8_t t143 = ((t142 == ((uint64_t)1ULL)));
                            t137 = t143;
                        } else t137 = 0;
                        t133 = t137;
                    } else t133 = 1;
                    gm_l12 = t133;
                }
ix128:;
            }
            goto me105;
mn125:;
ma109:;
            goto me105;
mn144:;
ma110:;
            gm_trap("no match arm matched the scrutinee", gm_src9pvgnpibl2nm, 91, 9);
me105:;
        }
        {
            uint64_t t146 = gm_l9;
            unsigned __int128 t147 = (unsigned __int128)t146;
            gm_l7 = t147;
        }
        {
            uint8_t t149 = gm_l12;
            if (!t149) goto ie148;
            {
                unsigned __int128 t151 = gm_l7;
                unsigned __int128 r152; bool o153 = __builtin_add_overflow(t151, ((unsigned __int128)(((unsigned __int128)0u << 64) | 1u)), &r152);
                if (o153) gm_trap("integer overflow in `+`", gm_src9pvgnpibl2nm, 104, 17);
                unsigned __int128 t154 = r152;
                gm_l7 = t154;
            }
            goto ix148;
ie148:;
ix148:;
        }
ix32:;
    }
    {
        gm_t86h6hhm6d76n t155;
        memset((uint8_t *)&t155 + 1, 0, 15);
        uint8_t t156 = 0;
        uint8_t t157 = gm_l3;
        if (t157) {
            unsigned __int128 t158 = gm_l7;
            uint8_t t159 = (!(t158 == ((unsigned __int128)(((unsigned __int128)0u << 64) | 0u))));
            t156 = t159;
        } else t156 = 0;
        GM_ST(uint8_t, (uint8_t *)&t155 + 0, t156);
        unsigned __int128 t160 = gm_l7;
        GM_ST(unsigned __int128, (uint8_t *)&t155 + 16, t160);
        gm_tsp9ul70rnkg6 t161;
        GM_ST(uint8_t, (uint8_t *)&t161 + 33, 0);
        GM_ST(uint16_t, (uint8_t *)&t161 + 34, 0);
        GM_ST(uint32_t, (uint8_t *)&t161 + 36, 0);
        GM_ST(uint64_t, (uint8_t *)&t161 + 40, 0);
        GM_ST(gm_t86h6hhm6d76n, ((uint8_t *)&t161), t155);
        ((uint8_t *)&t161)[32] = 1;
        uint8_t t162 = 0;
        F.ret = t161;
        goto gm_fret;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_tsp9ul70rnkg6 gm_f_num_float_int_ba5kc1t_c(double a0, gm_tvdg94bk7aa22 a1) { gm_tsp9ul70rnkg6 gm_r; gm_f_num_float_int_ba5kc1t(&gm_r, a0, a1); return gm_r; }
inline __attribute__((aligned(4))) double gm_f_num_fmax_0qau2jp(double a0, double a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_num_fmax_0qau2jp F;
    double gm_l0; /* x */
    double gm_l1; /* y */
    GM_STACK_CHECK_SHALLOW(&F, 218u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        double t3 = gm_l0;
        uint8_t t4 = gm_f_num_is_nan_b0ma7mo(t3);
        if (!t4) goto ie2;
        {
            double t6 = gm_l1;
            uint8_t t7 = 0;
            F.ret = t6;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        double t9 = gm_l1;
        uint8_t t10 = gm_f_num_is_nan_b0ma7mo(t9);
        if (!t10) goto ie8;
        {
            double t12 = gm_l0;
            uint8_t t13 = 0;
            F.ret = t12;
            goto gm_fret;
        }
        goto ix8;
ie8:;
ix8:;
    }
    {
        double t15 = gm_l0;
        uint8_t t16 = ((t15 == ((double)0e0)));
        if (!t16) goto ie14;
        double t17 = gm_l1;
        uint8_t t18 = ((t17 == ((double)0e0)));
        if (!t18) goto ie14;
        {
            double t20;
            double t22 = gm_l0;
            double t23 = t22;
            uint64_t t24;
            memset(&t24, 0, sizeof t24); memcpy(&t24, ((uint8_t *)&t23), sizeof t24 < 8 ? sizeof t24 : 8);
            unsigned __int128 n25 = (unsigned __int128)((uint64_t)63ULL);
            if (n25 >= 64) gm_trap_shift((uint64_t)n25, "u64", gm_src9pvgnpibl2nm, 246, 42);
            uint64_t t26 = (uint64_t)(t24 >> (int)n25);
            uint8_t t27 = ((t26 == ((uint64_t)1ULL)));
            if (!t27) goto ie21;
            double t29 = gm_l1;
            double t30 = t29;
            t20 = t30;
            goto ix21;
ie21:;
            double t32 = gm_l0;
            double t33 = t32;
            t20 = t33;
ix21:;
            uint8_t t34 = 0;
            F.ret = t20;
            goto gm_fret;
        }
        goto ix14;
ie14:;
ix14:;
    }
    {
        double t35;
        double t37 = gm_l0;
        double t38 = gm_l1;
        uint8_t t39 = (t37 > t38);
        if (!t39) goto ie36;
        double t41 = gm_l0;
        double t42 = t41;
        t35 = t42;
        goto ix36;
ie36:;
        double t44 = gm_l1;
        double t45 = t44;
        t35 = t45;
ix36:;
        uint8_t t46 = 0;
        F.ret = t35;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) double gm_f_num_fmin_1k5uqp0(double a0, double a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_num_fmin_1k5uqp0 F;
    double gm_l0; /* x */
    double gm_l1; /* y */
    GM_STACK_CHECK_SHALLOW(&F, 218u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        double t3 = gm_l0;
        uint8_t t4 = gm_f_num_is_nan_b0ma7mo(t3);
        if (!t4) goto ie2;
        {
            double t6 = gm_l1;
            uint8_t t7 = 0;
            F.ret = t6;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        double t9 = gm_l1;
        uint8_t t10 = gm_f_num_is_nan_b0ma7mo(t9);
        if (!t10) goto ie8;
        {
            double t12 = gm_l0;
            uint8_t t13 = 0;
            F.ret = t12;
            goto gm_fret;
        }
        goto ix8;
ie8:;
ix8:;
    }
    {
        double t15 = gm_l0;
        uint8_t t16 = ((t15 == ((double)0e0)));
        if (!t16) goto ie14;
        double t17 = gm_l1;
        uint8_t t18 = ((t17 == ((double)0e0)));
        if (!t18) goto ie14;
        {
            double t20;
            double t22 = gm_l0;
            double t23 = t22;
            uint64_t t24;
            memset(&t24, 0, sizeof t24); memcpy(&t24, ((uint8_t *)&t23), sizeof t24 < 8 ? sizeof t24 : 8);
            unsigned __int128 n25 = (unsigned __int128)((uint64_t)63ULL);
            if (n25 >= 64) gm_trap_shift((uint64_t)n25, "u64", gm_src9pvgnpibl2nm, 238, 42);
            uint64_t t26 = (uint64_t)(t24 >> (int)n25);
            uint8_t t27 = ((t26 == ((uint64_t)1ULL)));
            if (!t27) goto ie21;
            double t29 = gm_l0;
            double t30 = t29;
            t20 = t30;
            goto ix21;
ie21:;
            double t32 = gm_l1;
            double t33 = t32;
            t20 = t33;
ix21:;
            uint8_t t34 = 0;
            F.ret = t20;
            goto gm_fret;
        }
        goto ix14;
ie14:;
ix14:;
    }
    {
        double t35;
        double t37 = gm_l0;
        double t38 = gm_l1;
        uint8_t t39 = (t37 < t38);
        if (!t39) goto ie36;
        double t41 = gm_l0;
        double t42 = t41;
        t35 = t42;
        goto ix36;
ie36:;
        double t44 = gm_l1;
        double t45 = t44;
        t35 = t45;
ix36:;
        uint8_t t46 = 0;
        F.ret = t35;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) double gm_f_num_int_to_float_4hrvis6(const void * a0, uint16_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_num_int_to_float_4hrvis6 F;
    gm_t86h6hhm6d76n gm_l0; /* x */
    uint16_t gm_l1; /* width */
    double gm_l2; /* v */
    double gm_l3; /* v */
    GM_STACK_CHECK_SHALLOW(&F, 240u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l1 = a1;
    {
        uint16_t t3 = gm_l1;
        uint8_t t4 = ((t3 == ((uint16_t)32ULL)));
        if (!t4) goto ie2;
        {
            unsigned __int128 t6 = GM_LD(unsigned __int128, (((uint8_t *)a0) + 16));
            float t7 = (float)(t6);
            double t8 = (double)t7;
            gm_l2 = t8;
        }
        {
            double t9;
            uint8_t t11 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
            if (!t11) goto ie10;
            double t13 = gm_l2;
            double t14 = -t13;
            double t15 = t14;
            t9 = t15;
            goto ix10;
ie10:;
            double t17 = gm_l2;
            double t18 = t17;
            t9 = t18;
ix10:;
            uint8_t t19 = 0;
            F.ret = t9;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        unsigned __int128 t20 = GM_LD(unsigned __int128, (((uint8_t *)a0) + 16));
        double t21 = (double)(t20);
        gm_l3 = t21;
    }
    {
        uint8_t t23 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
        if (!t23) goto ie22;
        {
            double t25 = gm_l3;
            double t26 = -t25;
            gm_l3 = t26;
        }
        goto ix22;
ie22:;
ix22:;
    }
    {
        double t27;
        uint16_t t29 = gm_l1;
        uint8_t t30 = ((t29 == ((uint16_t)16ULL)));
        if (!t30) goto ie28;
        double t32 = gm_l3;
        double t33 = gm_f_num_round_2pna7m3(t32, ((uint16_t)16ULL));
        double t34 = t33;
        t27 = t34;
        goto ix28;
ie28:;
        double t36 = gm_l3;
        double t37 = t36;
        t27 = t37;
ix28:;
        uint8_t t38 = 0;
        F.ret = t27;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) double gm_f_num_int_to_float_4hrvis6_c(gm_csia7oevr2u0fk a0, uint16_t a1) { return gm_f_num_int_to_float_4hrvis6(&a0, a1); }
inline __attribute__((aligned(4))) uint8_t gm_f_num_integral_qbts4a6(double a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_num_integral_qbts4a6 F;
    double gm_l0; /* x */
    uint64_t gm_l1; /* raw */
    int64_t gm_l2; /* e */
    uint64_t gm_l3; /* man */
    int64_t gm_l4; /* sh */
    int64_t gm_l5; /* k */
    GM_STACK_CHECK_SHALLOW(&F, 275u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        double t2 = gm_l0;
        double t3 = t2;
        uint64_t t4;
        memset(&t4, 0, sizeof t4); memcpy(&t4, ((uint8_t *)&t3), sizeof t4 < 8 ? sizeof t4 : 8);
        gm_l1 = t4;
    }
    {
        uint64_t t5 = gm_l1;
        unsigned __int128 n6 = (unsigned __int128)((uint64_t)52ULL);
        if (n6 >= 64) gm_trap_shift((uint64_t)n6, "u64", gm_src9pvgnpibl2nm, 216, 12);
        uint64_t t7 = (uint64_t)(t5 >> (int)n6);
        uint64_t t8 = (uint64_t)(t7 & ((uint64_t)2047ULL));
        uint64_t t9 = t8;
        int64_t t10;
        memset(&t10, 0, sizeof t10); memcpy(&t10, ((uint8_t *)&t9), sizeof t10 < 8 ? sizeof t10 : 8);
        gm_l2 = t10;
    }
    {
        uint64_t t11 = gm_l1;
        uint64_t t12 = (uint64_t)(t11 & ((uint64_t)4503599627370495ULL));
        gm_l3 = t12;
    }
    {
        int64_t t14 = gm_l2;
        uint8_t t15 = ((t14 == ((int64_t)0LL)));
        if (!t15) goto ie13;
        {
            gm_l2 = ((int64_t)1LL);
        }
        goto ix13;
ie13:;
        {
            uint64_t t18 = gm_l3;
            unsigned __int128 n19 = (unsigned __int128)((uint64_t)52ULL);
            if (n19 >= 64) gm_trap_shift((uint64_t)n19, "u64", gm_src9pvgnpibl2nm, 221, 16);
            uint64_t r20 = (uint64_t)((uint64_t)((uint64_t)1ULL) << (int)n19);
            bool o21 = (uint64_t)(r20 >> (int)n19) != ((uint64_t)1ULL);
            if (o21) gm_trap("integer overflow in `<<`", gm_src9pvgnpibl2nm, 221, 16);
            uint64_t t22 = r20;
            uint64_t t23 = (uint64_t)(t18 | t22);
            gm_l3 = t23;
        }
ix13:;
    }
    {
        uint64_t t25 = gm_l3;
        uint8_t t26 = ((t25 == ((uint64_t)0ULL)));
        if (!t26) goto ie24;
        {
            uint8_t t28 = 0;
            F.ret = ((uint8_t)1);
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        int64_t t29 = gm_l2;
        int64_t r30; bool o31 = __builtin_sub_overflow(t29, ((int64_t)1075LL), &r30);
        if (o31) gm_trap("integer overflow in `-`", gm_src9pvgnpibl2nm, 224, 11);
        int64_t t32 = r30;
        gm_l4 = t32;
    }
    {
        int64_t t34 = gm_l4;
        uint8_t t35 = (t34 >= ((int64_t)0LL));
        if (!t35) goto ie33;
        {
            uint8_t t37 = 0;
            F.ret = ((uint8_t)1);
            goto gm_fret;
        }
        goto ix33;
ie33:;
ix33:;
    }
    {
        int64_t t38 = gm_l4;
        if (t38 == ((int64_t)(-9223372036854775807LL - 1))) gm_trap("integer overflow in `-`", gm_src9pvgnpibl2nm, 226, 10);
        int64_t t39 = (int64_t)((uint64_t)0 - (uint64_t)t38);
        gm_l5 = t39;
    }
    {
        int64_t t41 = gm_l5;
        uint8_t t42 = (t41 >= ((int64_t)64LL));
        if (!t42) goto ie40;
        {
            uint8_t t44 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        goto ix40;
ie40:;
ix40:;
    }
    {
        uint64_t t45 = gm_l3;
        int64_t t46 = gm_l5;
        uint64_t t47 = (uint64_t)t46;
        unsigned __int128 n48 = (unsigned __int128)t47;
        if (n48 >= 64) gm_trap_shift((uint64_t)n48, "u64", gm_src9pvgnpibl2nm, 228, 21);
        uint64_t r49 = (uint64_t)((uint64_t)((uint64_t)1ULL) << (int)n48);
        bool o50 = (uint64_t)(r49 >> (int)n48) != ((uint64_t)1ULL);
        if (o50) gm_trap("integer overflow in `<<`", gm_src9pvgnpibl2nm, 228, 21);
        uint64_t t51 = r49;
        uint64_t r52; bool o53 = __builtin_sub_overflow(t51, ((uint64_t)1ULL), &r52);
        if (o53) gm_trap("integer overflow in `-`", gm_src9pvgnpibl2nm, 228, 20);
        uint64_t t54 = r52;
        uint64_t t55 = (uint64_t)(t45 & t54);
        uint8_t t56 = ((t55 == ((uint64_t)0ULL)));
        uint8_t t57 = 0;
        F.ret = t56;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) uint8_t gm_f_num_is_nan_b0ma7mo(double a0);
extern __attribute__((aligned(4))) unsigned __int128 gm_f_num_mask_do3naah(uint32_t a0);
extern __attribute__((aligned(4))) double gm_f_num_round_2pna7m3(double a0, uint16_t a1);
inline __attribute__((aligned(4))) void gm_f_num_sat_66dqcpl(void *gm_out, const void * a0, uint8_t a1, uint32_t a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_num_sat_66dqcpl F;
    gm_t86h6hhm6d76n gm_l0; /* x */
    uint8_t gm_l1; /* signed */
    uint32_t gm_l2; /* bits */
    unsigned __int128 gm_l3; /* lim */
    GM_STACK_CHECK_SHALLOW(&F, 269u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        uint8_t t3 = gm_l1;
        if (!t3) goto ie2;
        {
            uint32_t t5 = gm_l2;
            uint32_t r6; bool o7 = __builtin_sub_overflow(t5, ((uint32_t)1ULL), &r6);
            if (o7) gm_trap("integer overflow in `-`", gm_src9pvgnpibl2nm, 46, 32);
            uint32_t t8 = r6;
            unsigned __int128 t9 = (unsigned __int128)t8;
            unsigned __int128 n10 = (unsigned __int128)t9;
            if (n10 >= 128) gm_trap_shift((uint64_t)n10, "u128", gm_src9pvgnpibl2nm, 46, 16);
            unsigned __int128 r11 = (unsigned __int128)((unsigned __int128)((unsigned __int128)(((unsigned __int128)0u << 64) | 1u)) << (int)n10);
            bool o12 = (unsigned __int128)(r11 >> (int)n10) != ((unsigned __int128)(((unsigned __int128)0u << 64) | 1u));
            if (o12) gm_trap("integer overflow in `<<`", gm_src9pvgnpibl2nm, 46, 16);
            unsigned __int128 t13 = r11;
            gm_l3 = t13;
        }
        {
            uint8_t t15 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
            if (!t15) goto ie14;
            unsigned __int128 t16 = GM_LD(unsigned __int128, (((uint8_t *)a0) + 16));
            unsigned __int128 t17 = gm_l3;
            uint8_t t18 = (t16 > t17);
            if (!t18) goto ie14;
            {
                memset((uint8_t *)&(*(gm_t86h6hhm6d76n *)gm_out) + 1, 0, 15);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t86h6hhm6d76n *)gm_out) + 0, ((uint8_t)1));
                unsigned __int128 t20 = gm_l3;
                GM_ST(unsigned __int128, (uint8_t *)&(*(gm_t86h6hhm6d76n *)gm_out) + 16, t20);
                goto gm_fdone;
            }
            goto ix14;
ie14:;
ix14:;
        }
        {
            uint8_t t22 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
            uint8_t t23 = (uint8_t)!t22;
            if (!t23) goto ie21;
            unsigned __int128 t24 = GM_LD(unsigned __int128, (((uint8_t *)a0) + 16));
            unsigned __int128 t25 = gm_l3;
            uint8_t t26 = (t24 >= t25);
            if (!t26) goto ie21;
            {
                memset((uint8_t *)&(*(gm_t86h6hhm6d76n *)gm_out) + 1, 0, 15);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t86h6hhm6d76n *)gm_out) + 0, ((uint8_t)0));
                unsigned __int128 t28 = gm_l3;
                unsigned __int128 r29; bool o30 = __builtin_sub_overflow(t28, ((unsigned __int128)(((unsigned __int128)0u << 64) | 1u)), &r29);
                if (o30) gm_trap("integer overflow in `-`", gm_src9pvgnpibl2nm, 48, 72);
                unsigned __int128 t31 = r29;
                GM_ST(unsigned __int128, (uint8_t *)&(*(gm_t86h6hhm6d76n *)gm_out) + 16, t31);
                goto gm_fdone;
            }
            goto ix21;
ie21:;
ix21:;
        }
        {
            memmove(gm_out, ((uint8_t *)a0), 32);
            goto gm_fdone;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        uint8_t t33 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
        if (!t33) goto ie32;
        {
            memset((uint8_t *)&(*(gm_t86h6hhm6d76n *)gm_out) + 1, 0, 15);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t86h6hhm6d76n *)gm_out) + 0, ((uint8_t)0));
            GM_ST(unsigned __int128, (uint8_t *)&(*(gm_t86h6hhm6d76n *)gm_out) + 16, ((unsigned __int128)(((unsigned __int128)0u << 64) | 0u)));
            goto gm_fdone;
        }
        goto ix32;
ie32:;
ix32:;
    }
    {
        unsigned __int128 t36 = GM_LD(unsigned __int128, (((uint8_t *)a0) + 16));
        uint32_t t37 = gm_l2;
        unsigned __int128 t38 = gm_f_num_mask_do3naah(t37);
        uint8_t t39 = (t36 > t38);
        if (!t39) goto ie35;
        {
            memset((uint8_t *)&(*(gm_t86h6hhm6d76n *)gm_out) + 1, 0, 15);
            uint32_t t41 = gm_l2;
            unsigned __int128 t42 = gm_f_num_mask_do3naah(t41);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t86h6hhm6d76n *)gm_out) + 0, ((uint8_t)0));
            GM_ST(unsigned __int128, (uint8_t *)&(*(gm_t86h6hhm6d76n *)gm_out) + 16, t42);
            goto gm_fdone;
        }
        goto ix35;
ie35:;
ix35:;
    }
    {
        memmove(gm_out, ((uint8_t *)a0), 32);
        goto gm_fdone;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_csia7oevr2u0fk gm_f_num_sat_66dqcpl_c(gm_csia7oevr2u0fk a0, uint8_t a1, uint32_t a2) { gm_t86h6hhm6d76n gm_r; gm_f_num_sat_66dqcpl(&gm_r, &a0, a1, a2); return GM_CONV(gm_csia7oevr2u0fk, gm_r); }
inline __attribute__((aligned(4))) void gm_f_num_val_i3upeur(void *gm_out, uint8_t a0, unsigned __int128 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_num_val_i3upeur F;
    uint8_t gm_l0; /* signed */
    unsigned __int128 gm_l1; /* v */
    GM_STACK_CHECK_SHALLOW(&F, 99u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint8_t t3 = gm_l0;
        if (!t3) goto ie2;
        unsigned __int128 t4 = gm_l1;
        unsigned __int128 n5 = (unsigned __int128)((uint64_t)127ULL);
        if (n5 >= 128) gm_trap_shift((uint64_t)n5, "u128", gm_src9pvgnpibl2nm, 27, 20);
        unsigned __int128 t6 = (unsigned __int128)(t4 >> (int)n5);
        uint8_t t7 = ((t6 == ((unsigned __int128)(((unsigned __int128)0u << 64) | 1u))));
        if (!t7) goto ie2;
        {
            memset((uint8_t *)&(*(gm_t86h6hhm6d76n *)gm_out) + 1, 0, 15);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t86h6hhm6d76n *)gm_out) + 0, ((uint8_t)1));
            unsigned __int128 t9 = gm_l1;
            unsigned __int128 r10; bool o11 = __builtin_sub_overflow(((unsigned __int128)(((unsigned __int128)0u << 64) | 0u)), t9, &r10);
            unsigned __int128 t12 = r10;
            GM_ST(unsigned __int128, (uint8_t *)&(*(gm_t86h6hhm6d76n *)gm_out) + 16, t12);
            goto gm_fdone;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        memset((uint8_t *)&(*(gm_t86h6hhm6d76n *)gm_out) + 1, 0, 15);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t86h6hhm6d76n *)gm_out) + 0, ((uint8_t)0));
        unsigned __int128 t13 = gm_l1;
        GM_ST(unsigned __int128, (uint8_t *)&(*(gm_t86h6hhm6d76n *)gm_out) + 16, t13);
        goto gm_fdone;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_csia7oevr2u0fk gm_f_num_val_i3upeur_c(uint8_t a0, unsigned __int128 a1) { gm_t86h6hhm6d76n gm_r; gm_f_num_val_i3upeur(&gm_r, a0, a1); return GM_CONV(gm_csia7oevr2u0fk, gm_r); }
inline __attribute__((aligned(4))) void gm_f_put_float_fiad72d(void * a0, gm_csguankko4lmhd a1, double a2, gm_b16_8 a3) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_put_float_fiad72d F;
    void * gm_l0; /* m */
    gm_t88rcucg1b7uk gm_l1; /* t */
    double gm_l2; /* x */
    gm_b16_8 gm_l3; /* dst */
    uint16_t gm_l4; /* w */
    GM_STACK_CHECK_SHALLOW(&F, 240u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    gm_l2 = a2;
    gm_l3 = a3;
    {
        void * t2 = gm_l0;
        gm_t88rcucg1b7uk t3 = gm_l1;
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
        uint16_t t5 = gm_f_float_width_a6a9p1v(t2, GM_CONV(gm_csguankko4lmhd, t3), t4);
        gm_l4 = t5;
    }
    {
        uint16_t t7 = gm_l4;
        uint8_t t8 = ((t7 == ((uint16_t)16ULL)));
        if (!t8) goto ie6;
        {
            void * t10 = gm_l0;
            gm_b16_8 t11 = gm_l3;
            double t12 = gm_l2;
            uint16_t t13 = gm_f_num_f64_to_f16_3g33m05(t12);
            unsigned __int128 t14 = (unsigned __int128)t13;
            gm_f_put_uint_cddin99(t10, t11, t14);
        }
        goto ix6;
ie6:;
        uint16_t t16 = gm_l4;
        uint8_t t17 = ((t16 == ((uint16_t)32ULL)));
        if (!t17) goto ie15;
        {
            void * t19 = gm_l0;
            gm_b16_8 t20 = gm_l3;
            double t21 = gm_l2;
            float t22 = (float)((double)t21);
            float t23 = t22;
            uint32_t t24;
            memset(&t24, 0, sizeof t24); memcpy(&t24, ((uint8_t *)&t23), sizeof t24 < 4 ? sizeof t24 : 4);
            unsigned __int128 t25 = (unsigned __int128)t24;
            gm_f_put_uint_cddin99(t19, t20, t25);
        }
        goto ix15;
ie15:;
        {
            void * t27 = gm_l0;
            gm_b16_8 t28 = gm_l3;
            double t29 = gm_l2;
            double t30 = t29;
            uint64_t t31;
            memset(&t31, 0, sizeof t31); memcpy(&t31, ((uint8_t *)&t30), sizeof t31 < 8 ? sizeof t31 : 8);
            unsigned __int128 t32 = (unsigned __int128)t31;
            gm_f_put_uint_cddin99(t27, t28, t32);
        }
ix15:;
ix6:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) unsigned __int128 gm_f_rd_int_iuioodt(void * a0, gm_ttovhfee8mf28 a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_rd_int_iuioodt F;
    void * gm_l0; /* m */
    gm_ttovhfee8mf28 gm_l1; /* info */
    gm_b16_8 gm_l2; /* b */
    unsigned __int128 gm_l3; /* v */
    GM_STACK_CHECK_SHALLOW(&F, 338u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        gm_b16_8 t3 = gm_l2;
        unsigned __int128 t4 = gm_f_rd_uint_n7l65o8(t2, t3);
        gm_l3 = t4;
    }
    {
        uint32_t t6 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 4));
        uint8_t t7 = (t6 >= ((uint32_t)128ULL));
        if (!t7) goto ie5;
        {
            unsigned __int128 t9 = gm_l3;
            uint8_t t10 = 0;
            F.ret = t9;
            goto gm_fret;
        }
        goto ix5;
ie5:;
ix5:;
    }
    {
        uint8_t t12 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (!t12) goto ie11;
        unsigned __int128 t13 = gm_l3;
        uint32_t t14 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 4));
        uint32_t r15; bool o16 = __builtin_sub_overflow(t14, ((uint32_t)1ULL), &r15);
        if (o16) gm_trap("integer overflow in `-`", gm_src9pvgnpibl2nm, 304, 35);
        uint32_t t17 = r15;
        unsigned __int128 t18 = (unsigned __int128)t17;
        unsigned __int128 n19 = (unsigned __int128)t18;
        if (n19 >= 128) gm_trap_shift((uint64_t)n19, "u128", gm_src9pvgnpibl2nm, 304, 25);
        unsigned __int128 t20 = (unsigned __int128)(t13 >> (int)n19);
        unsigned __int128 t21 = (unsigned __int128)(t20 & ((unsigned __int128)(((unsigned __int128)0u << 64) | 1u)));
        uint8_t t22 = ((t21 == ((unsigned __int128)(((unsigned __int128)0u << 64) | 1u))));
        if (!t22) goto ie11;
        {
            unsigned __int128 t24 = gm_l3;
            uint32_t t25 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 4));
            unsigned __int128 t26 = gm_f_num_mask_do3naah(t25);
            unsigned __int128 t27 = (unsigned __int128)~t26;
            unsigned __int128 t28 = (unsigned __int128)(t24 | t27);
            uint8_t t29 = 0;
            F.ret = t28;
            goto gm_fret;
        }
        goto ix11;
ie11:;
ix11:;
    }
    {
        unsigned __int128 t30 = gm_l3;
        uint32_t t31 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 4));
        unsigned __int128 t32 = gm_f_num_mask_do3naah(t31);
        unsigned __int128 t33 = (unsigned __int128)(t30 & t32);
        uint8_t t34 = 0;
        F.ret = t33;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) unsigned __int128 gm_f_rd_uint_n7l65o8(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_rd_uint_n7l65o8 F;
    void * gm_l0; /* m */
    gm_b16_8 gm_l1; /* b */
    GM_STACK_CHECK_SHALLOW(&F, 689u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t3 = gm_l0;
        uint8_t t4 = GM_LD(uint8_t, (((uint8_t *)t3) + 89));
        if (!t4) goto ie2;
        {
            uint64_t t7 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            uint8_t t8 = ((t7 == ((uint64_t)8ULL)));
            if (!t8) goto ie6;
            {
                uint64_t t10 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t11 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t10 >= (uintptr_t)(t11)) gm_trap_index(t10, t11, gm_src9pvgnpibl2nm, 259, 29);
                uint8_t t12 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t10 * 1));
                uint64_t t13 = (uint64_t)t12;
                uint64_t t14 = (uintptr_t)((uint64_t)1ULL);
                uint64_t t15 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t14 >= (uintptr_t)(t15)) gm_trap_index(t14, t15, gm_src9pvgnpibl2nm, 259, 42);
                uint8_t t16 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t14 * 1));
                uint64_t t17 = (uint64_t)t16;
                unsigned __int128 n18 = (unsigned __int128)((uint64_t)8ULL);
                if (n18 >= 64) gm_trap_shift((uint64_t)n18, "u64", gm_src9pvgnpibl2nm, 259, 38);
                uint64_t r19 = (uint64_t)((uint64_t)t17 << (int)n18);
                bool o20 = (uint64_t)(r19 >> (int)n18) != t17;
                if (o20) gm_trap("integer overflow in `<<`", gm_src9pvgnpibl2nm, 259, 38);
                uint64_t t21 = r19;
                uint64_t t22 = (uint64_t)(t13 | t21);
                uint64_t t23 = (uintptr_t)((uint64_t)2ULL);
                uint64_t t24 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t23 >= (uintptr_t)(t24)) gm_trap_index(t23, t24, gm_src9pvgnpibl2nm, 259, 61);
                uint8_t t25 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t23 * 1));
                uint64_t t26 = (uint64_t)t25;
                unsigned __int128 n27 = (unsigned __int128)((uint64_t)16ULL);
                if (n27 >= 64) gm_trap_shift((uint64_t)n27, "u64", gm_src9pvgnpibl2nm, 259, 57);
                uint64_t r28 = (uint64_t)((uint64_t)t26 << (int)n27);
                bool o29 = (uint64_t)(r28 >> (int)n27) != t26;
                if (o29) gm_trap("integer overflow in `<<`", gm_src9pvgnpibl2nm, 259, 57);
                uint64_t t30 = r28;
                uint64_t t31 = (uint64_t)(t22 | t30);
                uint64_t t32 = (uintptr_t)((uint64_t)3ULL);
                uint64_t t33 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t32 >= (uintptr_t)(t33)) gm_trap_index(t32, t33, gm_src9pvgnpibl2nm, 259, 81);
                uint8_t t34 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t32 * 1));
                uint64_t t35 = (uint64_t)t34;
                unsigned __int128 n36 = (unsigned __int128)((uint64_t)24ULL);
                if (n36 >= 64) gm_trap_shift((uint64_t)n36, "u64", gm_src9pvgnpibl2nm, 259, 77);
                uint64_t r37 = (uint64_t)((uint64_t)t35 << (int)n36);
                bool o38 = (uint64_t)(r37 >> (int)n36) != t35;
                if (o38) gm_trap("integer overflow in `<<`", gm_src9pvgnpibl2nm, 259, 77);
                uint64_t t39 = r37;
                uint64_t t40 = (uint64_t)(t31 | t39);
                uint64_t t41 = (uintptr_t)((uint64_t)4ULL);
                uint64_t t42 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t41 >= (uintptr_t)(t42)) gm_trap_index(t41, t42, gm_src9pvgnpibl2nm, 259, 101);
                uint8_t t43 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t41 * 1));
                uint64_t t44 = (uint64_t)t43;
                unsigned __int128 n45 = (unsigned __int128)((uint64_t)32ULL);
                if (n45 >= 64) gm_trap_shift((uint64_t)n45, "u64", gm_src9pvgnpibl2nm, 259, 97);
                uint64_t r46 = (uint64_t)((uint64_t)t44 << (int)n45);
                bool o47 = (uint64_t)(r46 >> (int)n45) != t44;
                if (o47) gm_trap("integer overflow in `<<`", gm_src9pvgnpibl2nm, 259, 97);
                uint64_t t48 = r46;
                uint64_t t49 = (uint64_t)(t40 | t48);
                uint64_t t50 = (uintptr_t)((uint64_t)5ULL);
                uint64_t t51 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t50 >= (uintptr_t)(t51)) gm_trap_index(t50, t51, gm_src9pvgnpibl2nm, 259, 121);
                uint8_t t52 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t50 * 1));
                uint64_t t53 = (uint64_t)t52;
                unsigned __int128 n54 = (unsigned __int128)((uint64_t)40ULL);
                if (n54 >= 64) gm_trap_shift((uint64_t)n54, "u64", gm_src9pvgnpibl2nm, 259, 117);
                uint64_t r55 = (uint64_t)((uint64_t)t53 << (int)n54);
                bool o56 = (uint64_t)(r55 >> (int)n54) != t53;
                if (o56) gm_trap("integer overflow in `<<`", gm_src9pvgnpibl2nm, 259, 117);
                uint64_t t57 = r55;
                uint64_t t58 = (uint64_t)(t49 | t57);
                uint64_t t59 = (uintptr_t)((uint64_t)6ULL);
                uint64_t t60 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t59 >= (uintptr_t)(t60)) gm_trap_index(t59, t60, gm_src9pvgnpibl2nm, 259, 141);
                uint8_t t61 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t59 * 1));
                uint64_t t62 = (uint64_t)t61;
                unsigned __int128 n63 = (unsigned __int128)((uint64_t)48ULL);
                if (n63 >= 64) gm_trap_shift((uint64_t)n63, "u64", gm_src9pvgnpibl2nm, 259, 137);
                uint64_t r64 = (uint64_t)((uint64_t)t62 << (int)n63);
                bool o65 = (uint64_t)(r64 >> (int)n63) != t62;
                if (o65) gm_trap("integer overflow in `<<`", gm_src9pvgnpibl2nm, 259, 137);
                uint64_t t66 = r64;
                uint64_t t67 = (uint64_t)(t58 | t66);
                uint64_t t68 = (uintptr_t)((uint64_t)7ULL);
                uint64_t t69 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t68 >= (uintptr_t)(t69)) gm_trap_index(t68, t69, gm_src9pvgnpibl2nm, 259, 161);
                uint8_t t70 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t68 * 1));
                uint64_t t71 = (uint64_t)t70;
                unsigned __int128 n72 = (unsigned __int128)((uint64_t)56ULL);
                if (n72 >= 64) gm_trap_shift((uint64_t)n72, "u64", gm_src9pvgnpibl2nm, 259, 157);
                uint64_t r73 = (uint64_t)((uint64_t)t71 << (int)n72);
                bool o74 = (uint64_t)(r73 >> (int)n72) != t71;
                if (o74) gm_trap("integer overflow in `<<`", gm_src9pvgnpibl2nm, 259, 157);
                uint64_t t75 = r73;
                uint64_t t76 = (uint64_t)(t67 | t75);
                unsigned __int128 t77 = (unsigned __int128)t76;
                uint8_t t78 = 0;
                F.ret = t77;
                goto gm_fret;
            }
            goto ix6;
ie6:;
ix6:;
        }
        {
            uint64_t t80 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            uint8_t t81 = ((t80 == ((uint64_t)4ULL)));
            if (!t81) goto ie79;
            {
                uint64_t t83 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t84 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t83 >= (uintptr_t)(t84)) gm_trap_index(t83, t84, gm_src9pvgnpibl2nm, 261, 41);
                uint8_t t85 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t83 * 1));
                uint32_t t86 = (uint32_t)t85;
                uint64_t t87 = (uintptr_t)((uint64_t)1ULL);
                uint64_t t88 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t87 >= (uintptr_t)(t88)) gm_trap_index(t87, t88, gm_src9pvgnpibl2nm, 261, 54);
                uint8_t t89 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t87 * 1));
                uint32_t t90 = (uint32_t)t89;
                unsigned __int128 n91 = (unsigned __int128)((uint64_t)8ULL);
                if (n91 >= 32) gm_trap_shift((uint64_t)n91, "u32", gm_src9pvgnpibl2nm, 261, 50);
                uint32_t r92 = (uint32_t)((uint32_t)t90 << (int)n91);
                bool o93 = (uint32_t)(r92 >> (int)n91) != t90;
                if (o93) gm_trap("integer overflow in `<<`", gm_src9pvgnpibl2nm, 261, 50);
                uint32_t t94 = r92;
                uint32_t t95 = (uint32_t)(t86 | t94);
                uint64_t t96 = (uintptr_t)((uint64_t)2ULL);
                uint64_t t97 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t96 >= (uintptr_t)(t97)) gm_trap_index(t96, t97, gm_src9pvgnpibl2nm, 261, 73);
                uint8_t t98 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t96 * 1));
                uint32_t t99 = (uint32_t)t98;
                unsigned __int128 n100 = (unsigned __int128)((uint64_t)16ULL);
                if (n100 >= 32) gm_trap_shift((uint64_t)n100, "u32", gm_src9pvgnpibl2nm, 261, 69);
                uint32_t r101 = (uint32_t)((uint32_t)t99 << (int)n100);
                bool o102 = (uint32_t)(r101 >> (int)n100) != t99;
                if (o102) gm_trap("integer overflow in `<<`", gm_src9pvgnpibl2nm, 261, 69);
                uint32_t t103 = r101;
                uint32_t t104 = (uint32_t)(t95 | t103);
                uint64_t t105 = (uintptr_t)((uint64_t)3ULL);
                uint64_t t106 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t105 >= (uintptr_t)(t106)) gm_trap_index(t105, t106, gm_src9pvgnpibl2nm, 261, 93);
                uint8_t t107 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t105 * 1));
                uint32_t t108 = (uint32_t)t107;
                unsigned __int128 n109 = (unsigned __int128)((uint64_t)24ULL);
                if (n109 >= 32) gm_trap_shift((uint64_t)n109, "u32", gm_src9pvgnpibl2nm, 261, 89);
                uint32_t r110 = (uint32_t)((uint32_t)t108 << (int)n109);
                bool o111 = (uint32_t)(r110 >> (int)n109) != t108;
                if (o111) gm_trap("integer overflow in `<<`", gm_src9pvgnpibl2nm, 261, 89);
                uint32_t t112 = r110;
                uint32_t t113 = (uint32_t)(t104 | t112);
                unsigned __int128 t114 = (unsigned __int128)t113;
                uint8_t t115 = 0;
                F.ret = t114;
                goto gm_fret;
            }
            goto ix79;
ie79:;
ix79:;
        }
        {
            uint64_t t117 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            uint8_t t118 = ((t117 == ((uint64_t)1ULL)));
            if (!t118) goto ie116;
            {
                uint64_t t120 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t121 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t120 >= (uintptr_t)(t121)) gm_trap_index(t120, t121, gm_src9pvgnpibl2nm, 262, 37);
                uint8_t t122 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t120 * 1));
                unsigned __int128 t123 = (unsigned __int128)t122;
                uint8_t t124 = 0;
                F.ret = t123;
                goto gm_fret;
            }
            goto ix116;
ie116:;
ix116:;
        }
        {
            uint64_t t126 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            uint8_t t127 = ((t126 == ((uint64_t)2ULL)));
            if (!t127) goto ie125;
            {
                uint64_t t129 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t130 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t129 >= (uintptr_t)(t130)) gm_trap_index(t129, t130, gm_src9pvgnpibl2nm, 263, 41);
                uint8_t t131 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t129 * 1));
                uint16_t t132 = (uint16_t)t131;
                uint64_t t133 = (uintptr_t)((uint64_t)1ULL);
                uint64_t t134 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t133 >= (uintptr_t)(t134)) gm_trap_index(t133, t134, gm_src9pvgnpibl2nm, 263, 54);
                uint8_t t135 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t133 * 1));
                uint16_t t136 = (uint16_t)t135;
                unsigned __int128 n137 = (unsigned __int128)((uint64_t)8ULL);
                if (n137 >= 16) gm_trap_shift((uint64_t)n137, "u16", gm_src9pvgnpibl2nm, 263, 50);
                uint16_t r138 = (uint16_t)((uint16_t)t136 << (int)n137);
                bool o139 = (uint16_t)(r138 >> (int)n137) != t136;
                if (o139) gm_trap("integer overflow in `<<`", gm_src9pvgnpibl2nm, 263, 50);
                uint16_t t140 = r138;
                uint16_t t141 = (uint16_t)(t132 | t140);
                unsigned __int128 t142 = (unsigned __int128)t141;
                uint8_t t143 = 0;
                F.ret = t142;
                goto gm_fret;
            }
            goto ix125;
ie125:;
ix125:;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t144 = gm_l0;
        gm_b16_8 t145 = gm_l1;
        unsigned __int128 t146 = gm_f_get_uint_bq8c4b7(t144, t145);
        uint8_t t147 = 0;
        F.ret = t146;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) void gm_f_wr_uint_rs9ado5(void * a0, gm_b16_8 a1, unsigned __int128 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_wr_uint_rs9ado5 F;
    void * gm_l0; /* m */
    gm_b16_8 gm_l1; /* b */
    unsigned __int128 gm_l2; /* v */
    uint64_t gm_l3; /* x */
    uint32_t gm_l4; /* x */
    GM_STACK_CHECK_SHALLOW(&F, 560u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t3 = gm_l0;
        uint8_t t4 = GM_LD(uint8_t, (((uint8_t *)t3) + 89));
        if (!t4) goto ie2;
        {
            uint64_t t7 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            uint8_t t8 = ((t7 == ((uint64_t)8ULL)));
            if (!t8) goto ie6;
            {
                unsigned __int128 t10 = gm_l2;
                uint64_t t11 = (uint64_t)t10;
                gm_l3 = t11;
            }
            {
                uint64_t t12 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t13 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t12 >= (uintptr_t)(t13)) gm_trap_index(t12, t13, gm_src9pvgnpibl2nm, 273, 13);
                uint64_t t14 = gm_l3;
                uint8_t t15 = (uint8_t)t14;
                GM_ST(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t12 * 1), t15);
            }
            {
                uint64_t t16 = (uintptr_t)((uint64_t)1ULL);
                uint64_t t17 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t16 >= (uintptr_t)(t17)) gm_trap_index(t16, t17, gm_src9pvgnpibl2nm, 274, 13);
                uint64_t t18 = gm_l3;
                unsigned __int128 n19 = (unsigned __int128)((uint64_t)8ULL);
                if (n19 >= 64) gm_trap_shift((uint64_t)n19, "u64", gm_src9pvgnpibl2nm, 274, 21);
                uint64_t t20 = (uint64_t)(t18 >> (int)n19);
                uint8_t t21 = (uint8_t)t20;
                GM_ST(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t16 * 1), t21);
            }
            {
                uint64_t t22 = (uintptr_t)((uint64_t)2ULL);
                uint64_t t23 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t22 >= (uintptr_t)(t23)) gm_trap_index(t22, t23, gm_src9pvgnpibl2nm, 275, 13);
                uint64_t t24 = gm_l3;
                unsigned __int128 n25 = (unsigned __int128)((uint64_t)16ULL);
                if (n25 >= 64) gm_trap_shift((uint64_t)n25, "u64", gm_src9pvgnpibl2nm, 275, 21);
                uint64_t t26 = (uint64_t)(t24 >> (int)n25);
                uint8_t t27 = (uint8_t)t26;
                GM_ST(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t22 * 1), t27);
            }
            {
                uint64_t t28 = (uintptr_t)((uint64_t)3ULL);
                uint64_t t29 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t28 >= (uintptr_t)(t29)) gm_trap_index(t28, t29, gm_src9pvgnpibl2nm, 276, 13);
                uint64_t t30 = gm_l3;
                unsigned __int128 n31 = (unsigned __int128)((uint64_t)24ULL);
                if (n31 >= 64) gm_trap_shift((uint64_t)n31, "u64", gm_src9pvgnpibl2nm, 276, 21);
                uint64_t t32 = (uint64_t)(t30 >> (int)n31);
                uint8_t t33 = (uint8_t)t32;
                GM_ST(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t28 * 1), t33);
            }
            {
                uint64_t t34 = (uintptr_t)((uint64_t)4ULL);
                uint64_t t35 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t34 >= (uintptr_t)(t35)) gm_trap_index(t34, t35, gm_src9pvgnpibl2nm, 277, 13);
                uint64_t t36 = gm_l3;
                unsigned __int128 n37 = (unsigned __int128)((uint64_t)32ULL);
                if (n37 >= 64) gm_trap_shift((uint64_t)n37, "u64", gm_src9pvgnpibl2nm, 277, 21);
                uint64_t t38 = (uint64_t)(t36 >> (int)n37);
                uint8_t t39 = (uint8_t)t38;
                GM_ST(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t34 * 1), t39);
            }
            {
                uint64_t t40 = (uintptr_t)((uint64_t)5ULL);
                uint64_t t41 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t40 >= (uintptr_t)(t41)) gm_trap_index(t40, t41, gm_src9pvgnpibl2nm, 278, 13);
                uint64_t t42 = gm_l3;
                unsigned __int128 n43 = (unsigned __int128)((uint64_t)40ULL);
                if (n43 >= 64) gm_trap_shift((uint64_t)n43, "u64", gm_src9pvgnpibl2nm, 278, 21);
                uint64_t t44 = (uint64_t)(t42 >> (int)n43);
                uint8_t t45 = (uint8_t)t44;
                GM_ST(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t40 * 1), t45);
            }
            {
                uint64_t t46 = (uintptr_t)((uint64_t)6ULL);
                uint64_t t47 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t46 >= (uintptr_t)(t47)) gm_trap_index(t46, t47, gm_src9pvgnpibl2nm, 279, 13);
                uint64_t t48 = gm_l3;
                unsigned __int128 n49 = (unsigned __int128)((uint64_t)48ULL);
                if (n49 >= 64) gm_trap_shift((uint64_t)n49, "u64", gm_src9pvgnpibl2nm, 279, 21);
                uint64_t t50 = (uint64_t)(t48 >> (int)n49);
                uint8_t t51 = (uint8_t)t50;
                GM_ST(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t46 * 1), t51);
            }
            {
                uint64_t t52 = (uintptr_t)((uint64_t)7ULL);
                uint64_t t53 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t52 >= (uintptr_t)(t53)) gm_trap_index(t52, t53, gm_src9pvgnpibl2nm, 280, 13);
                uint64_t t54 = gm_l3;
                unsigned __int128 n55 = (unsigned __int128)((uint64_t)56ULL);
                if (n55 >= 64) gm_trap_shift((uint64_t)n55, "u64", gm_src9pvgnpibl2nm, 280, 21);
                uint64_t t56 = (uint64_t)(t54 >> (int)n55);
                uint8_t t57 = (uint8_t)t56;
                GM_ST(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t52 * 1), t57);
            }
            {
                gm_b0_1 t58;
                uint8_t t59 = 0;
                F.ret = t58;
                goto gm_fret;
            }
            goto ix6;
ie6:;
ix6:;
        }
        {
            uint64_t t61 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            uint8_t t62 = ((t61 == ((uint64_t)4ULL)));
            if (!t62) goto ie60;
            {
                unsigned __int128 t64 = gm_l2;
                uint32_t t65 = (uint32_t)t64;
                gm_l4 = t65;
            }
            {
                uint64_t t66 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t67 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t66 >= (uintptr_t)(t67)) gm_trap_index(t66, t67, gm_src9pvgnpibl2nm, 285, 13);
                uint32_t t68 = gm_l4;
                uint8_t t69 = (uint8_t)t68;
                GM_ST(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t66 * 1), t69);
            }
            {
                uint64_t t70 = (uintptr_t)((uint64_t)1ULL);
                uint64_t t71 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t70 >= (uintptr_t)(t71)) gm_trap_index(t70, t71, gm_src9pvgnpibl2nm, 286, 13);
                uint32_t t72 = gm_l4;
                unsigned __int128 n73 = (unsigned __int128)((uint64_t)8ULL);
                if (n73 >= 32) gm_trap_shift((uint64_t)n73, "u32", gm_src9pvgnpibl2nm, 286, 21);
                uint32_t t74 = (uint32_t)(t72 >> (int)n73);
                uint8_t t75 = (uint8_t)t74;
                GM_ST(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t70 * 1), t75);
            }
            {
                uint64_t t76 = (uintptr_t)((uint64_t)2ULL);
                uint64_t t77 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t76 >= (uintptr_t)(t77)) gm_trap_index(t76, t77, gm_src9pvgnpibl2nm, 287, 13);
                uint32_t t78 = gm_l4;
                unsigned __int128 n79 = (unsigned __int128)((uint64_t)16ULL);
                if (n79 >= 32) gm_trap_shift((uint64_t)n79, "u32", gm_src9pvgnpibl2nm, 287, 21);
                uint32_t t80 = (uint32_t)(t78 >> (int)n79);
                uint8_t t81 = (uint8_t)t80;
                GM_ST(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t76 * 1), t81);
            }
            {
                uint64_t t82 = (uintptr_t)((uint64_t)3ULL);
                uint64_t t83 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t82 >= (uintptr_t)(t83)) gm_trap_index(t82, t83, gm_src9pvgnpibl2nm, 288, 13);
                uint32_t t84 = gm_l4;
                unsigned __int128 n85 = (unsigned __int128)((uint64_t)24ULL);
                if (n85 >= 32) gm_trap_shift((uint64_t)n85, "u32", gm_src9pvgnpibl2nm, 288, 21);
                uint32_t t86 = (uint32_t)(t84 >> (int)n85);
                uint8_t t87 = (uint8_t)t86;
                GM_ST(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t82 * 1), t87);
            }
            {
                gm_b0_1 t88;
                uint8_t t89 = 0;
                F.ret = t88;
                goto gm_fret;
            }
            goto ix60;
ie60:;
ix60:;
        }
        {
            uint64_t t91 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            uint8_t t92 = ((t91 == ((uint64_t)1ULL)));
            if (!t92) goto ie90;
            {
                uint64_t t94 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t95 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t94 >= (uintptr_t)(t95)) gm_trap_index(t94, t95, gm_src9pvgnpibl2nm, 292, 13);
                unsigned __int128 t96 = gm_l2;
                uint8_t t97 = (uint8_t)t96;
                GM_ST(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t94 * 1), t97);
            }
            {
                gm_b0_1 t98;
                uint8_t t99 = 0;
                F.ret = t98;
                goto gm_fret;
            }
            goto ix90;
ie90:;
ix90:;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t100 = gm_l0;
        gm_b16_8 t101 = gm_l1;
        unsigned __int128 t102 = gm_l2;
        gm_f_put_uint_cddin99(t100, t101, t102);
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
